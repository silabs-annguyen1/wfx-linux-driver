/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common private data for Silicon Labs WFx chips.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#ifndef WFX_H
#define WFX_H

#include <linux/version.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <net/mac80211.h>

#include "bh.h"
#include "data_tx.h"
#include "main.h"
#include "queue.h"
#include "secure_link.h"
#include "sta.h"
#include "scan.h"
#include "hif_tx.h"
#include "hif_api_general.h"

#if (KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE)
#define array_index_nospec(index, size) index
#else
#include <linux/nospec.h>
#endif

#if (KERNEL_VERSION(4, 2, 0) > LINUX_VERSION_CODE)
static inline void _ieee80211_hw_set(struct ieee80211_hw *hw,
				     enum ieee80211_hw_flags flg)
{
	hw->flags |= flg;
}
#define ieee80211_hw_set(hw, flg)	_ieee80211_hw_set(hw, IEEE80211_HW_##flg)
#endif

#if (KERNEL_VERSION(4, 7, 0) > LINUX_VERSION_CODE)
#define nl80211_band ieee80211_band
#define NL80211_BAND_2GHZ IEEE80211_BAND_2GHZ
#define NUM_NL80211_BANDS IEEE80211_NUM_BANDS
#endif

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}
#endif

#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
#define struct_size(p, member, n) \
	(n * sizeof(*(p)->member) + __must_be_array((p)->member) + sizeof(*(p)))
#endif

struct hwbus_ops;

struct wfx_dev {
	struct wfx_platform_data pdata;
	struct device		*dev;
	struct ieee80211_hw	*hw;
	struct ieee80211_vif	*vif[2];
	struct mac_address	addresses[2];
	const struct hwbus_ops	*hwbus_ops;
	void			*hwbus_priv;

	u8			keyset;
	struct completion	firmware_ready;
	struct hif_ind_startup	hw_caps;
	struct wfx_hif		hif;
	struct sl_context	sl;
	int			chip_frozen;
	struct mutex		conf_mutex;

	struct wfx_hif_cmd	hif_cmd;
	struct wfx_queue	tx_queue[4];
	struct wfx_queue_stats	tx_queue_stats;
	int			tx_burst_idx;
	atomic_t		tx_lock;

	u32			key_map;
	struct hif_req_add_key	keys[MAX_KEY_ENTRIES];

	struct hif_rx_stats	rx_stats;
	struct mutex		rx_stats_lock;

	int			output_power;
	atomic_t		scan_in_progress;
};

struct wfx_vif {
	struct wfx_dev		*wdev;
	struct ieee80211_vif	*vif;
	struct ieee80211_channel *channel;
	int			id;
	enum wfx_state		state;

	int			delayed_link_loss;
	int			bss_loss_state;
	u32			bss_loss_confirm_id;
	struct mutex		bss_loss_lock;
	struct delayed_work	bss_loss_work;

	u32			link_id_map;
	struct wfx_link_entry	link_id_db[WFX_MAX_STA_IN_AP_MODE];
	struct delayed_work	link_id_gc_work;
	struct work_struct	link_id_work;

	bool			aid0_bit_set;
	bool			mcast_tx;
	bool			mcast_buffered;
	struct wfx_grp_addr_table mcast_filter;
	struct timer_list	mcast_timeout;
	struct work_struct	mcast_start_work;
	struct work_struct	mcast_stop_work;

	s8			wep_default_key_id;
	struct sk_buff		*wep_pending_skb;
	struct work_struct	wep_key_work;

	struct tx_policy_cache	tx_policy_cache;
	struct work_struct	tx_policy_upload_work;

	u32			sta_asleep_mask;
	u32			pspoll_mask;
	spinlock_t		ps_state_lock;
	struct work_struct	set_tim_work;

	int			dtim_period;
	int			beacon_int;
	bool			enable_beacon;
	struct work_struct	set_beacon_wakeup_period_work;

	bool			filter_bssid;
	bool			fwd_probe_req;
	bool			disable_beacon_filter;
	struct work_struct	update_filtering_work;

	u32			erp_info;
	int			cqm_rssi_thold;
	bool			setbssparams_done;
	struct wfx_ht_info	ht_info;
	struct wfx_edca_params	edca;
	struct hif_mib_set_uapsd_information uapsd_info;
	struct hif_req_set_bss_params bss_params;
	struct work_struct	bss_params_work;
	struct work_struct	set_cts_work;

	int			join_complete_status;
	bool			delayed_unjoin;
	struct work_struct	unjoin_work;

	struct wfx_scan		scan;

	struct hif_req_set_pm_mode powersave_mode;
	struct completion	set_pm_mode_complete;

	struct list_head	event_queue;
	spinlock_t		event_queue_lock;
	struct work_struct	event_handler_work;
};

static inline struct wfx_vif *wdev_to_wvif(struct wfx_dev *wdev, int vif_id)
{
	if (vif_id >= ARRAY_SIZE(wdev->vif)) {
		dev_dbg(wdev->dev, "requesting non-existent vif: %d\n", vif_id);
		return NULL;
	}
	vif_id = array_index_nospec(vif_id, ARRAY_SIZE(wdev->vif));
	if (!wdev->vif[vif_id]) {
		dev_dbg(wdev->dev, "requesting non-allocated vif: %d\n", vif_id);
		return NULL;
	}
	return (struct wfx_vif *) wdev->vif[vif_id]->drv_priv;
}

static inline struct wfx_vif *wvif_iterate(struct wfx_dev *wdev, struct wfx_vif *cur)
{
	int i;
	int mark = 0;
	struct wfx_vif *tmp;

	if (!cur)
		mark = 1;
	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		tmp = wdev_to_wvif(wdev, i);
		if (mark && tmp)
			return tmp;
		if (tmp == cur)
			mark = 1;
	}
	return NULL;
}

static inline int wvif_count(struct wfx_dev *wdev)
{
	int i;
	int ret = 0;
	struct wfx_vif *wvif;

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		wvif = wdev_to_wvif(wdev, i);
		if (wvif)
			ret++;
	}
	return ret;
}

static inline void memreverse(u8 *src, u8 length)
{
	u8 *lo = src;
	u8 *hi = src + length - 1;
	u8 swap;

	while (lo < hi) {
		swap = *lo;
		*lo++ = *hi;
		*hi-- = swap;
	}
}

static inline int memzcmp(void *src, unsigned int size)
{
	u8 *buf = src;

	if (!size)
		return 0;
	if (*buf)
		return 1;
	return memcmp(buf, buf + 1, size - 1);
}

#endif /* WFX_H */
