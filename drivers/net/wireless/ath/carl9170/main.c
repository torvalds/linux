/*
 * Atheros CARL9170 driver
 *
 * mac80211 interaction code
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "hw.h"
#include "carl9170.h"
#include "cmd.h"

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware crypto offload.");

int modparam_noht;
module_param_named(noht, modparam_noht, int, S_IRUGO);
MODULE_PARM_DESC(noht, "Disable MPDU aggregation.");

#define RATE(_bitrate, _hw_rate, _txpidx, _flags) {	\
	.bitrate	= (_bitrate),			\
	.flags		= (_flags),			\
	.hw_value	= (_hw_rate) | (_txpidx) << 4,	\
}

struct ieee80211_rate __carl9170_ratetable[] = {
	RATE(10, 0, 0, 0),
	RATE(20, 1, 1, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, 2, 2, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, 3, 3, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0xb, 0, 0),
	RATE(90, 0xf, 0, 0),
	RATE(120, 0xa, 0, 0),
	RATE(180, 0xe, 0, 0),
	RATE(240, 0x9, 0, 0),
	RATE(360, 0xd, 1, 0),
	RATE(480, 0x8, 2, 0),
	RATE(540, 0xc, 3, 0),
};
#undef RATE

#define carl9170_g_ratetable	(__carl9170_ratetable + 0)
#define carl9170_g_ratetable_size	12
#define carl9170_a_ratetable	(__carl9170_ratetable + 4)
#define carl9170_a_ratetable_size	8

/*
 * NB: The hw_value is used as an index into the carl9170_phy_freq_params
 *     array in phy.c so that we don't have to do frequency lookups!
 */
#define CHAN(_freq, _idx) {		\
	.center_freq	= (_freq),	\
	.hw_value	= (_idx),	\
	.max_power	= 18, /* XXX */	\
}

static struct ieee80211_channel carl9170_2ghz_chantable[] = {
	CHAN(2412,  0),
	CHAN(2417,  1),
	CHAN(2422,  2),
	CHAN(2427,  3),
	CHAN(2432,  4),
	CHAN(2437,  5),
	CHAN(2442,  6),
	CHAN(2447,  7),
	CHAN(2452,  8),
	CHAN(2457,  9),
	CHAN(2462, 10),
	CHAN(2467, 11),
	CHAN(2472, 12),
	CHAN(2484, 13),
};

static struct ieee80211_channel carl9170_5ghz_chantable[] = {
	CHAN(4920, 14),
	CHAN(4940, 15),
	CHAN(4960, 16),
	CHAN(4980, 17),
	CHAN(5040, 18),
	CHAN(5060, 19),
	CHAN(5080, 20),
	CHAN(5180, 21),
	CHAN(5200, 22),
	CHAN(5220, 23),
	CHAN(5240, 24),
	CHAN(5260, 25),
	CHAN(5280, 26),
	CHAN(5300, 27),
	CHAN(5320, 28),
	CHAN(5500, 29),
	CHAN(5520, 30),
	CHAN(5540, 31),
	CHAN(5560, 32),
	CHAN(5580, 33),
	CHAN(5600, 34),
	CHAN(5620, 35),
	CHAN(5640, 36),
	CHAN(5660, 37),
	CHAN(5680, 38),
	CHAN(5700, 39),
	CHAN(5745, 40),
	CHAN(5765, 41),
	CHAN(5785, 42),
	CHAN(5805, 43),
	CHAN(5825, 44),
	CHAN(5170, 45),
	CHAN(5190, 46),
	CHAN(5210, 47),
	CHAN(5230, 48),
};
#undef CHAN

#define CARL9170_HT_CAP							\
{									\
	.ht_supported	= true,						\
	.cap		= IEEE80211_HT_CAP_MAX_AMSDU |			\
			  IEEE80211_HT_CAP_SUP_WIDTH_20_40 |		\
			  IEEE80211_HT_CAP_SGI_40 |			\
			  IEEE80211_HT_CAP_DSSSCCK40 |			\
			  IEEE80211_HT_CAP_SM_PS,			\
	.ampdu_factor	= IEEE80211_HT_MAX_AMPDU_64K,			\
	.ampdu_density	= IEEE80211_HT_MPDU_DENSITY_8,			\
	.mcs		= {						\
		.rx_mask = { 0xff, 0xff, 0, 0, 0x1, 0, 0, 0, 0, 0, },	\
		.rx_highest = cpu_to_le16(300),				\
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,		\
	},								\
}

static struct ieee80211_supported_band carl9170_band_2GHz = {
	.channels	= carl9170_2ghz_chantable,
	.n_channels	= ARRAY_SIZE(carl9170_2ghz_chantable),
	.bitrates	= carl9170_g_ratetable,
	.n_bitrates	= carl9170_g_ratetable_size,
	.ht_cap		= CARL9170_HT_CAP,
};

static struct ieee80211_supported_band carl9170_band_5GHz = {
	.channels	= carl9170_5ghz_chantable,
	.n_channels	= ARRAY_SIZE(carl9170_5ghz_chantable),
	.bitrates	= carl9170_a_ratetable,
	.n_bitrates	= carl9170_a_ratetable_size,
	.ht_cap		= CARL9170_HT_CAP,
};

static void carl9170_ampdu_gc(struct ar9170 *ar)
{
	struct carl9170_sta_tid *tid_info;
	LIST_HEAD(tid_gc);

	rcu_read_lock();
	list_for_each_entry_rcu(tid_info, &ar->tx_ampdu_list, list) {
		spin_lock_bh(&ar->tx_ampdu_list_lock);
		if (tid_info->state == CARL9170_TID_STATE_SHUTDOWN) {
			tid_info->state = CARL9170_TID_STATE_KILLED;
			list_del_rcu(&tid_info->list);
			ar->tx_ampdu_list_len--;
			list_add_tail(&tid_info->tmp_list, &tid_gc);
		}
		spin_unlock_bh(&ar->tx_ampdu_list_lock);

	}
	rcu_assign_pointer(ar->tx_ampdu_iter, tid_info);
	rcu_read_unlock();

	synchronize_rcu();

	while (!list_empty(&tid_gc)) {
		struct sk_buff *skb;
		tid_info = list_first_entry(&tid_gc, struct carl9170_sta_tid,
					    tmp_list);

		while ((skb = __skb_dequeue(&tid_info->queue)))
			carl9170_tx_status(ar, skb, false);

		list_del_init(&tid_info->tmp_list);
		kfree(tid_info);
	}
}

static void carl9170_flush(struct ar9170 *ar, bool drop_queued)
{
	if (drop_queued) {
		int i;

		/*
		 * We can only drop frames which have not been uploaded
		 * to the device yet.
		 */

		for (i = 0; i < ar->hw->queues; i++) {
			struct sk_buff *skb;

			while ((skb = skb_dequeue(&ar->tx_pending[i]))) {
				struct ieee80211_tx_info *info;

				info = IEEE80211_SKB_CB(skb);
				if (info->flags & IEEE80211_TX_CTL_AMPDU)
					atomic_dec(&ar->tx_ampdu_upload);

				carl9170_tx_status(ar, skb, false);
			}
		}
	}

	/* Wait for all other outstanding frames to timeout. */
	if (atomic_read(&ar->tx_total_queued))
		WARN_ON(wait_for_completion_timeout(&ar->tx_flush, HZ) == 0);
}

static void carl9170_flush_ba(struct ar9170 *ar)
{
	struct sk_buff_head free;
	struct carl9170_sta_tid *tid_info;
	struct sk_buff *skb;

	__skb_queue_head_init(&free);

	rcu_read_lock();
	spin_lock_bh(&ar->tx_ampdu_list_lock);
	list_for_each_entry_rcu(tid_info, &ar->tx_ampdu_list, list) {
		if (tid_info->state > CARL9170_TID_STATE_SUSPEND) {
			tid_info->state = CARL9170_TID_STATE_SUSPEND;

			spin_lock(&tid_info->lock);
			while ((skb = __skb_dequeue(&tid_info->queue)))
				__skb_queue_tail(&free, skb);
			spin_unlock(&tid_info->lock);
		}
	}
	spin_unlock_bh(&ar->tx_ampdu_list_lock);
	rcu_read_unlock();

	while ((skb = __skb_dequeue(&free)))
		carl9170_tx_status(ar, skb, false);
}

static void carl9170_zap_queues(struct ar9170 *ar)
{
	struct carl9170_vif_info *cvif;
	unsigned int i;

	carl9170_ampdu_gc(ar);

	carl9170_flush_ba(ar);
	carl9170_flush(ar, true);

	for (i = 0; i < ar->hw->queues; i++) {
		spin_lock_bh(&ar->tx_status[i].lock);
		while (!skb_queue_empty(&ar->tx_status[i])) {
			struct sk_buff *skb;

			skb = skb_peek(&ar->tx_status[i]);
			carl9170_tx_get_skb(skb);
			spin_unlock_bh(&ar->tx_status[i].lock);
			carl9170_tx_drop(ar, skb);
			spin_lock_bh(&ar->tx_status[i].lock);
			carl9170_tx_put_skb(skb);
		}
		spin_unlock_bh(&ar->tx_status[i].lock);
	}

	BUILD_BUG_ON(CARL9170_NUM_TX_LIMIT_SOFT < 1);
	BUILD_BUG_ON(CARL9170_NUM_TX_LIMIT_HARD < CARL9170_NUM_TX_LIMIT_SOFT);
	BUILD_BUG_ON(CARL9170_NUM_TX_LIMIT_HARD >= CARL9170_BAW_BITS);

	/* reinitialize queues statistics */
	memset(&ar->tx_stats, 0, sizeof(ar->tx_stats));
	for (i = 0; i < ar->hw->queues; i++)
		ar->tx_stats[i].limit = CARL9170_NUM_TX_LIMIT_HARD;

	for (i = 0; i < DIV_ROUND_UP(ar->fw.mem_blocks, BITS_PER_LONG); i++)
		ar->mem_bitmap[i] = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(cvif, &ar->vif_list, list) {
		spin_lock_bh(&ar->beacon_lock);
		dev_kfree_skb_any(cvif->beacon);
		cvif->beacon = NULL;
		spin_unlock_bh(&ar->beacon_lock);
	}
	rcu_read_unlock();

	atomic_set(&ar->tx_ampdu_upload, 0);
	atomic_set(&ar->tx_ampdu_scheduler, 0);
	atomic_set(&ar->tx_total_pending, 0);
	atomic_set(&ar->tx_total_queued, 0);
	atomic_set(&ar->mem_free_blocks, ar->fw.mem_blocks);
}

#define CARL9170_FILL_QUEUE(queue, ai_fs, cwmin, cwmax, _txop)		\
do {									\
	queue.aifs = ai_fs;						\
	queue.cw_min = cwmin;						\
	queue.cw_max = cwmax;						\
	queue.txop = _txop;						\
} while (0)

static int carl9170_op_start(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;
	int err, i;

	mutex_lock(&ar->mutex);

	carl9170_zap_queues(ar);

	/* reset QoS defaults */
	CARL9170_FILL_QUEUE(ar->edcf[AR9170_TXQ_VO], 2, 3,     7, 47);
	CARL9170_FILL_QUEUE(ar->edcf[AR9170_TXQ_VI], 2, 7,    15, 94);
	CARL9170_FILL_QUEUE(ar->edcf[AR9170_TXQ_BE], 3, 15, 1023,  0);
	CARL9170_FILL_QUEUE(ar->edcf[AR9170_TXQ_BK], 7, 15, 1023,  0);
	CARL9170_FILL_QUEUE(ar->edcf[AR9170_TXQ_SPECIAL], 2, 3, 7, 0);

	ar->current_factor = ar->current_density = -1;
	/* "The first key is unique." */
	ar->usedkeys = 1;
	ar->filter_state = 0;
	ar->ps.last_action = jiffies;
	ar->ps.last_slept = jiffies;
	ar->erp_mode = CARL9170_ERP_AUTO;
	ar->rx_software_decryption = false;
	ar->disable_offload = false;

	for (i = 0; i < ar->hw->queues; i++) {
		ar->queue_stop_timeout[i] = jiffies;
		ar->max_queue_stop_timeout[i] = 0;
	}

	atomic_set(&ar->mem_allocs, 0);

	err = carl9170_usb_open(ar);
	if (err)
		goto out;

	err = carl9170_init_mac(ar);
	if (err)
		goto out;

	err = carl9170_set_qos(ar);
	if (err)
		goto out;

	if (ar->fw.rx_filter) {
		err = carl9170_rx_filter(ar, CARL9170_RX_FILTER_OTHER_RA |
			CARL9170_RX_FILTER_CTL_OTHER | CARL9170_RX_FILTER_BAD);
		if (err)
			goto out;
	}

	err = carl9170_write_reg(ar, AR9170_MAC_REG_DMA_TRIGGER,
				 AR9170_DMA_TRIGGER_RXQ);
	if (err)
		goto out;

	/* Clear key-cache */
	for (i = 0; i < AR9170_CAM_MAX_USER + 4; i++) {
		err = carl9170_upload_key(ar, i, NULL, AR9170_ENC_ALG_NONE,
					  0, NULL, 0);
		if (err)
			goto out;

		err = carl9170_upload_key(ar, i, NULL, AR9170_ENC_ALG_NONE,
					  1, NULL, 0);
		if (err)
			goto out;

		if (i < AR9170_CAM_MAX_USER) {
			err = carl9170_disable_key(ar, i);
			if (err)
				goto out;
		}
	}

	carl9170_set_state_when(ar, CARL9170_IDLE, CARL9170_STARTED);

	ieee80211_wake_queues(ar->hw);
	err = 0;

out:
	mutex_unlock(&ar->mutex);
	return err;
}

static void carl9170_cancel_worker(struct ar9170 *ar)
{
	cancel_delayed_work_sync(&ar->tx_janitor);
#ifdef CONFIG_CARL9170_LEDS
	cancel_delayed_work_sync(&ar->led_work);
#endif /* CONFIG_CARL9170_LEDS */
	cancel_work_sync(&ar->ps_work);
	cancel_work_sync(&ar->ping_work);
	cancel_work_sync(&ar->ampdu_work);
}

static void carl9170_op_stop(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;

	carl9170_set_state_when(ar, CARL9170_STARTED, CARL9170_IDLE);

	ieee80211_stop_queues(ar->hw);

	mutex_lock(&ar->mutex);
	if (IS_ACCEPTING_CMD(ar)) {
		rcu_assign_pointer(ar->beacon_iter, NULL);

		carl9170_led_set_state(ar, 0);

		/* stop DMA */
		carl9170_write_reg(ar, AR9170_MAC_REG_DMA_TRIGGER, 0);
		carl9170_usb_stop(ar);
	}

	carl9170_zap_queues(ar);
	mutex_unlock(&ar->mutex);

	carl9170_cancel_worker(ar);
}

static void carl9170_restart_work(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 restart_work);
	int err;

	ar->usedkeys = 0;
	ar->filter_state = 0;
	carl9170_cancel_worker(ar);

	mutex_lock(&ar->mutex);
	err = carl9170_usb_restart(ar);
	if (net_ratelimit()) {
		if (err) {
			dev_err(&ar->udev->dev, "Failed to restart device "
				" (%d).\n", err);
		 } else {
			dev_info(&ar->udev->dev, "device restarted "
				 "successfully.\n");
		}
	}

	carl9170_zap_queues(ar);
	mutex_unlock(&ar->mutex);
	if (!err) {
		ar->restart_counter++;
		atomic_set(&ar->pending_restarts, 0);

		ieee80211_restart_hw(ar->hw);
	} else {
		/*
		 * The reset was unsuccessful and the device seems to
		 * be dead. But there's still one option: a low-level
		 * usb subsystem reset...
		 */

		carl9170_usb_reset(ar);
	}
}

void carl9170_restart(struct ar9170 *ar, const enum carl9170_restart_reasons r)
{
	carl9170_set_state_when(ar, CARL9170_STARTED, CARL9170_IDLE);

	/*
	 * Sometimes, an error can trigger several different reset events.
	 * By ignoring these *surplus* reset events, the device won't be
	 * killed again, right after it has recovered.
	 */
	if (atomic_inc_return(&ar->pending_restarts) > 1) {
		dev_dbg(&ar->udev->dev, "ignoring restart (%d)\n", r);
		return;
	}

	ieee80211_stop_queues(ar->hw);

	dev_err(&ar->udev->dev, "restart device (%d)\n", r);

	if (!WARN_ON(r == CARL9170_RR_NO_REASON) ||
	    !WARN_ON(r >= __CARL9170_RR_LAST))
		ar->last_reason = r;

	if (!ar->registered)
		return;

	if (IS_ACCEPTING_CMD(ar) && !ar->needs_full_reset)
		ieee80211_queue_work(ar->hw, &ar->restart_work);
	else
		carl9170_usb_reset(ar);

	/*
	 * At this point, the device instance might have vanished/disabled.
	 * So, don't put any code which access the ar9170 struct
	 * without proper protection.
	 */
}

static void carl9170_ping_work(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170, ping_work);
	int err;

	if (!IS_STARTED(ar))
		return;

	mutex_lock(&ar->mutex);
	err = carl9170_echo_test(ar, 0xdeadbeef);
	if (err)
		carl9170_restart(ar, CARL9170_RR_UNRESPONSIVE_DEVICE);
	mutex_unlock(&ar->mutex);
}

static int carl9170_init_interface(struct ar9170 *ar,
				   struct ieee80211_vif *vif)
{
	struct ath_common *common = &ar->common;
	int err;

	if (!vif) {
		WARN_ON_ONCE(IS_STARTED(ar));
		return 0;
	}

	memcpy(common->macaddr, vif->addr, ETH_ALEN);

	if (modparam_nohwcrypt ||
	    ((vif->type != NL80211_IFTYPE_STATION) &&
	     (vif->type != NL80211_IFTYPE_AP))) {
		ar->rx_software_decryption = true;
		ar->disable_offload = true;
	}

	err = carl9170_set_operating_mode(ar);
	return err;
}

static int carl9170_op_add_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct carl9170_vif_info *vif_priv = (void *) vif->drv_priv;
	struct ieee80211_vif *main_vif;
	struct ar9170 *ar = hw->priv;
	int vif_id = -1, err = 0;

	mutex_lock(&ar->mutex);
	rcu_read_lock();
	if (vif_priv->active) {
		/*
		 * Skip the interface structure initialization,
		 * if the vif survived the _restart call.
		 */
		vif_id = vif_priv->id;
		vif_priv->enable_beacon = false;

		spin_lock_bh(&ar->beacon_lock);
		dev_kfree_skb_any(vif_priv->beacon);
		vif_priv->beacon = NULL;
		spin_unlock_bh(&ar->beacon_lock);

		goto init;
	}

	main_vif = carl9170_get_main_vif(ar);

	if (main_vif) {
		switch (main_vif->type) {
		case NL80211_IFTYPE_STATION:
			if (vif->type == NL80211_IFTYPE_STATION)
				break;

			err = -EBUSY;
			rcu_read_unlock();

			goto unlock;

		case NL80211_IFTYPE_AP:
			if ((vif->type == NL80211_IFTYPE_STATION) ||
			    (vif->type == NL80211_IFTYPE_WDS) ||
			    (vif->type == NL80211_IFTYPE_AP))
				break;

			err = -EBUSY;
			rcu_read_unlock();
			goto unlock;

		default:
			rcu_read_unlock();
			goto unlock;
		}
	}

	vif_id = bitmap_find_free_region(&ar->vif_bitmap, ar->fw.vif_num, 0);

	if (vif_id < 0) {
		rcu_read_unlock();

		err = -ENOSPC;
		goto unlock;
	}

	BUG_ON(ar->vif_priv[vif_id].id != vif_id);

	vif_priv->active = true;
	vif_priv->id = vif_id;
	vif_priv->enable_beacon = false;
	ar->vifs++;
	list_add_tail_rcu(&vif_priv->list, &ar->vif_list);
	rcu_assign_pointer(ar->vif_priv[vif_id].vif, vif);

init:
	if (carl9170_get_main_vif(ar) == vif) {
		rcu_assign_pointer(ar->beacon_iter, vif_priv);
		rcu_read_unlock();

		err = carl9170_init_interface(ar, vif);
		if (err)
			goto unlock;
	} else {
		rcu_read_unlock();
		err = carl9170_mod_virtual_mac(ar, vif_id, vif->addr);

		if (err)
			goto unlock;
	}

	if (ar->fw.tx_seq_table) {
		err = carl9170_write_reg(ar, ar->fw.tx_seq_table + vif_id * 4,
					 0);
		if (err)
			goto unlock;
	}

unlock:
	if (err && (vif_id >= 0)) {
		vif_priv->active = false;
		bitmap_release_region(&ar->vif_bitmap, vif_id, 0);
		ar->vifs--;
		rcu_assign_pointer(ar->vif_priv[vif_id].vif, NULL);
		list_del_rcu(&vif_priv->list);
		mutex_unlock(&ar->mutex);
		synchronize_rcu();
	} else {
		if (ar->vifs > 1)
			ar->ps.off_override |= PS_OFF_VIF;

		mutex_unlock(&ar->mutex);
	}

	return err;
}

static void carl9170_op_remove_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct carl9170_vif_info *vif_priv = (void *) vif->drv_priv;
	struct ieee80211_vif *main_vif;
	struct ar9170 *ar = hw->priv;
	unsigned int id;

	mutex_lock(&ar->mutex);

	if (WARN_ON_ONCE(!vif_priv->active))
		goto unlock;

	ar->vifs--;

	rcu_read_lock();
	main_vif = carl9170_get_main_vif(ar);

	id = vif_priv->id;

	vif_priv->active = false;
	WARN_ON(vif_priv->enable_beacon);
	vif_priv->enable_beacon = false;
	list_del_rcu(&vif_priv->list);
	rcu_assign_pointer(ar->vif_priv[id].vif, NULL);

	if (vif == main_vif) {
		rcu_read_unlock();

		if (ar->vifs) {
			WARN_ON(carl9170_init_interface(ar,
					carl9170_get_main_vif(ar)));
		} else {
			carl9170_set_operating_mode(ar);
		}
	} else {
		rcu_read_unlock();

		WARN_ON(carl9170_mod_virtual_mac(ar, id, NULL));
	}

	carl9170_update_beacon(ar, false);
	carl9170_flush_cab(ar, id);

	spin_lock_bh(&ar->beacon_lock);
	dev_kfree_skb_any(vif_priv->beacon);
	vif_priv->beacon = NULL;
	spin_unlock_bh(&ar->beacon_lock);

	bitmap_release_region(&ar->vif_bitmap, id, 0);

	carl9170_set_beacon_timers(ar);

	if (ar->vifs == 1)
		ar->ps.off_override &= ~PS_OFF_VIF;

unlock:
	mutex_unlock(&ar->mutex);

	synchronize_rcu();
}

void carl9170_ps_check(struct ar9170 *ar)
{
	ieee80211_queue_work(ar->hw, &ar->ps_work);
}

/* caller must hold ar->mutex */
static int carl9170_ps_update(struct ar9170 *ar)
{
	bool ps = false;
	int err = 0;

	if (!ar->ps.off_override)
		ps = (ar->hw->conf.flags & IEEE80211_CONF_PS);

	if (ps != ar->ps.state) {
		err = carl9170_powersave(ar, ps);
		if (err)
			return err;

		if (ar->ps.state && !ps) {
			ar->ps.sleep_ms = jiffies_to_msecs(jiffies -
				ar->ps.last_action);
		}

		if (ps)
			ar->ps.last_slept = jiffies;

		ar->ps.last_action = jiffies;
		ar->ps.state = ps;
	}

	return 0;
}

static void carl9170_ps_work(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 ps_work);
	mutex_lock(&ar->mutex);
	if (IS_STARTED(ar))
		WARN_ON_ONCE(carl9170_ps_update(ar) != 0);
	mutex_unlock(&ar->mutex);
}


static int carl9170_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ar9170 *ar = hw->priv;
	int err = 0;

	mutex_lock(&ar->mutex);
	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		err = carl9170_ps_update(ar);
		if (err)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_SMPS) {
		/* TODO */
		err = 0;
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		/* adjust slot time for 5 GHz */
		err = carl9170_set_slot_time(ar);
		if (err)
			goto out;

		err = carl9170_set_channel(ar, hw->conf.channel,
			hw->conf.channel_type, CARL9170_RFI_NONE);
		if (err)
			goto out;

		err = carl9170_set_dyn_sifs_ack(ar);
		if (err)
			goto out;

		err = carl9170_set_rts_cts_rate(ar);
		if (err)
			goto out;
	}

out:
	mutex_unlock(&ar->mutex);
	return err;
}

static u64 carl9170_op_prepare_multicast(struct ieee80211_hw *hw,
					 struct netdev_hw_addr_list *mc_list)
{
	struct netdev_hw_addr *ha;
	u64 mchash;

	/* always get broadcast frames */
	mchash = 1ULL << (0xff >> 2);

	netdev_hw_addr_list_for_each(ha, mc_list)
		mchash |= 1ULL << (ha->addr[5] >> 2);

	return mchash;
}

static void carl9170_op_configure_filter(struct ieee80211_hw *hw,
					 unsigned int changed_flags,
					 unsigned int *new_flags,
					 u64 multicast)
{
	struct ar9170 *ar = hw->priv;

	/* mask supported flags */
	*new_flags &= FIF_ALLMULTI | ar->rx_filter_caps;

	if (!IS_ACCEPTING_CMD(ar))
		return;

	mutex_lock(&ar->mutex);

	ar->filter_state = *new_flags;
	/*
	 * We can support more by setting the sniffer bit and
	 * then checking the error flags, later.
	 */

	if (*new_flags & FIF_ALLMULTI)
		multicast = ~0ULL;

	if (multicast != ar->cur_mc_hash)
		WARN_ON(carl9170_update_multicast(ar, multicast));

	if (changed_flags & (FIF_OTHER_BSS | FIF_PROMISC_IN_BSS)) {
		ar->sniffer_enabled = !!(*new_flags &
			(FIF_OTHER_BSS | FIF_PROMISC_IN_BSS));

		WARN_ON(carl9170_set_operating_mode(ar));
	}

	if (ar->fw.rx_filter && changed_flags & ar->rx_filter_caps) {
		u32 rx_filter = 0;

		if (!(*new_flags & (FIF_FCSFAIL | FIF_PLCPFAIL)))
			rx_filter |= CARL9170_RX_FILTER_BAD;

		if (!(*new_flags & FIF_CONTROL))
			rx_filter |= CARL9170_RX_FILTER_CTL_OTHER;

		if (!(*new_flags & FIF_PSPOLL))
			rx_filter |= CARL9170_RX_FILTER_CTL_PSPOLL;

		if (!(*new_flags & (FIF_OTHER_BSS | FIF_PROMISC_IN_BSS))) {
			rx_filter |= CARL9170_RX_FILTER_OTHER_RA;
			rx_filter |= CARL9170_RX_FILTER_DECRY_FAIL;
		}

		WARN_ON(carl9170_rx_filter(ar, rx_filter));
	}

	mutex_unlock(&ar->mutex);
}


static void carl9170_op_bss_info_changed(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_bss_conf *bss_conf,
					 u32 changed)
{
	struct ar9170 *ar = hw->priv;
	struct ath_common *common = &ar->common;
	int err = 0;
	struct carl9170_vif_info *vif_priv;
	struct ieee80211_vif *main_vif;

	mutex_lock(&ar->mutex);
	vif_priv = (void *) vif->drv_priv;
	main_vif = carl9170_get_main_vif(ar);
	if (WARN_ON(!main_vif))
		goto out;

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		struct carl9170_vif_info *iter;
		int i = 0;

		vif_priv->enable_beacon = bss_conf->enable_beacon;
		rcu_read_lock();
		list_for_each_entry_rcu(iter, &ar->vif_list, list) {
			if (iter->active && iter->enable_beacon)
				i++;

		}
		rcu_read_unlock();

		ar->beacon_enabled = i;
	}

	if (changed & BSS_CHANGED_BEACON) {
		err = carl9170_update_beacon(ar, false);
		if (err)
			goto out;
	}

	if (changed & (BSS_CHANGED_BEACON_ENABLED | BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_INT)) {

		if (main_vif != vif) {
			bss_conf->beacon_int = main_vif->bss_conf.beacon_int;
			bss_conf->dtim_period = main_vif->bss_conf.dtim_period;
		}

		/*
		 * Therefore a hard limit for the broadcast traffic should
		 * prevent false alarms.
		 */
		if (vif->type != NL80211_IFTYPE_STATION &&
		    (bss_conf->beacon_int * bss_conf->dtim_period >=
		     (CARL9170_QUEUE_STUCK_TIMEOUT / 2))) {
			err = -EINVAL;
			goto out;
		}

		err = carl9170_set_beacon_timers(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_HT) {
		/* TODO */
		err = 0;
		if (err)
			goto out;
	}

	if (main_vif != vif)
		goto out;

	/*
	 * The following settings can only be changed by the
	 * master interface.
	 */

	if (changed & BSS_CHANGED_BSSID) {
		memcpy(common->curbssid, bss_conf->bssid, ETH_ALEN);
		err = carl9170_set_operating_mode(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_ASSOC) {
		ar->common.curaid = bss_conf->aid;
		err = carl9170_set_beacon_timers(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		err = carl9170_set_slot_time(ar);
		if (err)
			goto out;
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		err = carl9170_set_mac_rates(ar);
		if (err)
			goto out;
	}

out:
	WARN_ON_ONCE(err && IS_STARTED(ar));
	mutex_unlock(&ar->mutex);
}

static u64 carl9170_op_get_tsf(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;
	struct carl9170_tsf_rsp tsf;
	int err;

	mutex_lock(&ar->mutex);
	err = carl9170_exec_cmd(ar, CARL9170_CMD_READ_TSF,
				0, NULL, sizeof(tsf), &tsf);
	mutex_unlock(&ar->mutex);
	if (WARN_ON(err))
		return 0;

	return le64_to_cpu(tsf.tsf_64);
}

static int carl9170_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct ar9170 *ar = hw->priv;
	int err = 0, i;
	u8 ktype;

	if (ar->disable_offload || !vif)
		return -EOPNOTSUPP;

	/*
	 * We have to fall back to software encryption, whenever
	 * the user choose to participates in an IBSS or is connected
	 * to more than one network.
	 *
	 * This is very unfortunate, because some machines cannot handle
	 * the high througput speed in 802.11n networks.
	 */

	if (!is_main_vif(ar, vif)) {
		mutex_lock(&ar->mutex);
		goto err_softw;
	}

	/*
	 * While the hardware supports *catch-all* key, for offloading
	 * group-key en-/de-cryption. The way of how the hardware
	 * decides which keyId maps to which key, remains a mystery...
	 */
	if ((vif->type != NL80211_IFTYPE_STATION &&
	     vif->type != NL80211_IFTYPE_ADHOC) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		ktype = AR9170_ENC_ALG_WEP64;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		ktype = AR9170_ENC_ALG_WEP128;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ktype = AR9170_ENC_ALG_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		ktype = AR9170_ENC_ALG_AESCCMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&ar->mutex);
	if (cmd == SET_KEY) {
		if (!IS_STARTED(ar)) {
			err = -EOPNOTSUPP;
			goto out;
		}

		if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
			sta = NULL;

			i = 64 + key->keyidx;
		} else {
			for (i = 0; i < 64; i++)
				if (!(ar->usedkeys & BIT(i)))
					break;
			if (i == 64)
				goto err_softw;
		}

		key->hw_key_idx = i;

		err = carl9170_upload_key(ar, i, sta ? sta->addr : NULL,
					  ktype, 0, key->key,
					  min_t(u8, 16, key->keylen));
		if (err)
			goto out;

		if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			err = carl9170_upload_key(ar, i, sta ? sta->addr :
						  NULL, ktype, 1,
						  key->key + 16, 16);
			if (err)
				goto out;

			/*
			 * hardware is not capable generating MMIC
			 * of fragmented frames!
			 */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		}

		if (i < 64)
			ar->usedkeys |= BIT(i);

		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	} else {
		if (!IS_STARTED(ar)) {
			/* The device is gone... together with the key ;-) */
			err = 0;
			goto out;
		}

		if (key->hw_key_idx < 64) {
			ar->usedkeys &= ~BIT(key->hw_key_idx);
		} else {
			err = carl9170_upload_key(ar, key->hw_key_idx, NULL,
						  AR9170_ENC_ALG_NONE, 0,
						  NULL, 0);
			if (err)
				goto out;

			if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
				err = carl9170_upload_key(ar, key->hw_key_idx,
							  NULL,
							  AR9170_ENC_ALG_NONE,
							  1, NULL, 0);
				if (err)
					goto out;
			}

		}

		err = carl9170_disable_key(ar, key->hw_key_idx);
		if (err)
			goto out;
	}

out:
	mutex_unlock(&ar->mutex);
	return err;

err_softw:
	if (!ar->rx_software_decryption) {
		ar->rx_software_decryption = true;
		carl9170_set_operating_mode(ar);
	}
	mutex_unlock(&ar->mutex);
	return -ENOSPC;
}

static int carl9170_op_sta_add(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta)
{
	struct carl9170_sta_info *sta_info = (void *) sta->drv_priv;
	unsigned int i;

	atomic_set(&sta_info->pending_frames, 0);

	if (sta->ht_cap.ht_supported) {
		if (sta->ht_cap.ampdu_density > 6) {
			/*
			 * HW does support 16us AMPDU density.
			 * No HT-Xmit for station.
			 */

			return 0;
		}

		for (i = 0; i < CARL9170_NUM_TID; i++)
			rcu_assign_pointer(sta_info->agg[i], NULL);

		sta_info->ampdu_max_len = 1 << (3 + sta->ht_cap.ampdu_factor);
		sta_info->ht_sta = true;
	}

	return 0;
}

static int carl9170_op_sta_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct ar9170 *ar = hw->priv;
	struct carl9170_sta_info *sta_info = (void *) sta->drv_priv;
	unsigned int i;
	bool cleanup = false;

	if (sta->ht_cap.ht_supported) {

		sta_info->ht_sta = false;

		rcu_read_lock();
		for (i = 0; i < CARL9170_NUM_TID; i++) {
			struct carl9170_sta_tid *tid_info;

			tid_info = rcu_dereference(sta_info->agg[i]);
			rcu_assign_pointer(sta_info->agg[i], NULL);

			if (!tid_info)
				continue;

			spin_lock_bh(&ar->tx_ampdu_list_lock);
			if (tid_info->state > CARL9170_TID_STATE_SHUTDOWN)
				tid_info->state = CARL9170_TID_STATE_SHUTDOWN;
			spin_unlock_bh(&ar->tx_ampdu_list_lock);
			cleanup = true;
		}
		rcu_read_unlock();

		if (cleanup)
			carl9170_ampdu_gc(ar);
	}

	return 0;
}

static int carl9170_op_conf_tx(struct ieee80211_hw *hw, u16 queue,
			       const struct ieee80211_tx_queue_params *param)
{
	struct ar9170 *ar = hw->priv;
	int ret;

	mutex_lock(&ar->mutex);
	if (queue < ar->hw->queues) {
		memcpy(&ar->edcf[ar9170_qmap[queue]], param, sizeof(*param));
		ret = carl9170_set_qos(ar);
	} else {
		ret = -EINVAL;
	}

	mutex_unlock(&ar->mutex);
	return ret;
}

static void carl9170_ampdu_work(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170,
					 ampdu_work);

	if (!IS_STARTED(ar))
		return;

	mutex_lock(&ar->mutex);
	carl9170_ampdu_gc(ar);
	mutex_unlock(&ar->mutex);
}

static int carl9170_op_ampdu_action(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    enum ieee80211_ampdu_mlme_action action,
				    struct ieee80211_sta *sta,
				    u16 tid, u16 *ssn, u8 buf_size)
{
	struct ar9170 *ar = hw->priv;
	struct carl9170_sta_info *sta_info = (void *) sta->drv_priv;
	struct carl9170_sta_tid *tid_info;

	if (modparam_noht)
		return -EOPNOTSUPP;

	switch (action) {
	case IEEE80211_AMPDU_TX_START:
		if (!sta_info->ht_sta)
			return -EOPNOTSUPP;

		rcu_read_lock();
		if (rcu_dereference(sta_info->agg[tid])) {
			rcu_read_unlock();
			return -EBUSY;
		}

		tid_info = kzalloc(sizeof(struct carl9170_sta_tid),
				   GFP_ATOMIC);
		if (!tid_info) {
			rcu_read_unlock();
			return -ENOMEM;
		}

		tid_info->hsn = tid_info->bsn = tid_info->snx = (*ssn);
		tid_info->state = CARL9170_TID_STATE_PROGRESS;
		tid_info->tid = tid;
		tid_info->max = sta_info->ampdu_max_len;

		INIT_LIST_HEAD(&tid_info->list);
		INIT_LIST_HEAD(&tid_info->tmp_list);
		skb_queue_head_init(&tid_info->queue);
		spin_lock_init(&tid_info->lock);

		spin_lock_bh(&ar->tx_ampdu_list_lock);
		ar->tx_ampdu_list_len++;
		list_add_tail_rcu(&tid_info->list, &ar->tx_ampdu_list);
		rcu_assign_pointer(sta_info->agg[tid], tid_info);
		spin_unlock_bh(&ar->tx_ampdu_list_lock);
		rcu_read_unlock();

		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	case IEEE80211_AMPDU_TX_STOP:
		rcu_read_lock();
		tid_info = rcu_dereference(sta_info->agg[tid]);
		if (tid_info) {
			spin_lock_bh(&ar->tx_ampdu_list_lock);
			if (tid_info->state > CARL9170_TID_STATE_SHUTDOWN)
				tid_info->state = CARL9170_TID_STATE_SHUTDOWN;
			spin_unlock_bh(&ar->tx_ampdu_list_lock);
		}

		rcu_assign_pointer(sta_info->agg[tid], NULL);
		rcu_read_unlock();

		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		ieee80211_queue_work(ar->hw, &ar->ampdu_work);
		break;

	case IEEE80211_AMPDU_TX_OPERATIONAL:
		rcu_read_lock();
		tid_info = rcu_dereference(sta_info->agg[tid]);

		sta_info->stats[tid].clear = true;
		sta_info->stats[tid].req = false;

		if (tid_info) {
			bitmap_zero(tid_info->bitmap, CARL9170_BAW_SIZE);
			tid_info->state = CARL9170_TID_STATE_IDLE;
		}
		rcu_read_unlock();

		if (WARN_ON_ONCE(!tid_info))
			return -EFAULT;

		break;

	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		/* Handled by hardware */
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

#ifdef CONFIG_CARL9170_WPC
static int carl9170_register_wps_button(struct ar9170 *ar)
{
	struct input_dev *input;
	int err;

	if (!(ar->features & CARL9170_WPS_BUTTON))
		return 0;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	snprintf(ar->wps.name, sizeof(ar->wps.name), "%s WPS Button",
		 wiphy_name(ar->hw->wiphy));

	snprintf(ar->wps.phys, sizeof(ar->wps.phys),
		 "ieee80211/%s/input0", wiphy_name(ar->hw->wiphy));

	input->name = ar->wps.name;
	input->phys = ar->wps.phys;
	input->id.bustype = BUS_USB;
	input->dev.parent = &ar->hw->wiphy->dev;

	input_set_capability(input, EV_KEY, KEY_WPS_BUTTON);

	err = input_register_device(input);
	if (err) {
		input_free_device(input);
		return err;
	}

	ar->wps.pbc = input;
	return 0;
}
#endif /* CONFIG_CARL9170_WPC */

static int carl9170_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct ar9170 *ar = hw->priv;
	int err;

	if (idx != 0)
		return -ENOENT;

	mutex_lock(&ar->mutex);
	err = carl9170_get_noisefloor(ar);
	mutex_unlock(&ar->mutex);
	if (err)
		return err;

	survey->channel = ar->channel;
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = ar->noise[0];
	return 0;
}

static void carl9170_op_flush(struct ieee80211_hw *hw, bool drop)
{
	struct ar9170 *ar = hw->priv;
	unsigned int vid;

	mutex_lock(&ar->mutex);
	for_each_set_bit(vid, &ar->vif_bitmap, ar->fw.vif_num)
		carl9170_flush_cab(ar, vid);

	carl9170_flush(ar, drop);
	mutex_unlock(&ar->mutex);
}

static int carl9170_op_get_stats(struct ieee80211_hw *hw,
				 struct ieee80211_low_level_stats *stats)
{
	struct ar9170 *ar = hw->priv;

	memset(stats, 0, sizeof(*stats));
	stats->dot11ACKFailureCount = ar->tx_ack_failures;
	stats->dot11FCSErrorCount = ar->tx_fcs_errors;
	return 0;
}

static void carl9170_op_sta_notify(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum sta_notify_cmd cmd,
				   struct ieee80211_sta *sta)
{
	struct carl9170_sta_info *sta_info = (void *) sta->drv_priv;

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		sta_info->sleeping = true;
		if (atomic_read(&sta_info->pending_frames))
			ieee80211_sta_block_awake(hw, sta, true);
		break;

	case STA_NOTIFY_AWAKE:
		sta_info->sleeping = false;
		break;
	}
}

static bool carl9170_tx_frames_pending(struct ieee80211_hw *hw)
{
	struct ar9170 *ar = hw->priv;

	return !!atomic_read(&ar->tx_total_queued);
}

static const struct ieee80211_ops carl9170_ops = {
	.start			= carl9170_op_start,
	.stop			= carl9170_op_stop,
	.tx			= carl9170_op_tx,
	.flush			= carl9170_op_flush,
	.add_interface		= carl9170_op_add_interface,
	.remove_interface	= carl9170_op_remove_interface,
	.config			= carl9170_op_config,
	.prepare_multicast	= carl9170_op_prepare_multicast,
	.configure_filter	= carl9170_op_configure_filter,
	.conf_tx		= carl9170_op_conf_tx,
	.bss_info_changed	= carl9170_op_bss_info_changed,
	.get_tsf		= carl9170_op_get_tsf,
	.set_key		= carl9170_op_set_key,
	.sta_add		= carl9170_op_sta_add,
	.sta_remove		= carl9170_op_sta_remove,
	.sta_notify		= carl9170_op_sta_notify,
	.get_survey		= carl9170_op_get_survey,
	.get_stats		= carl9170_op_get_stats,
	.ampdu_action		= carl9170_op_ampdu_action,
	.tx_frames_pending	= carl9170_tx_frames_pending,
};

void *carl9170_alloc(size_t priv_size)
{
	struct ieee80211_hw *hw;
	struct ar9170 *ar;
	struct sk_buff *skb;
	int i;

	/*
	 * this buffer is used for rx stream reconstruction.
	 * Under heavy load this device (or the transport layer?)
	 * tends to split the streams into separate rx descriptors.
	 */

	skb = __dev_alloc_skb(AR9170_RX_STREAM_MAX_SIZE, GFP_KERNEL);
	if (!skb)
		goto err_nomem;

	hw = ieee80211_alloc_hw(priv_size, &carl9170_ops);
	if (!hw)
		goto err_nomem;

	ar = hw->priv;
	ar->hw = hw;
	ar->rx_failover = skb;

	memset(&ar->rx_plcp, 0, sizeof(struct ar9170_rx_head));
	ar->rx_has_plcp = false;

	/*
	 * Here's a hidden pitfall!
	 *
	 * All 4 AC queues work perfectly well under _legacy_ operation.
	 * However as soon as aggregation is enabled, the traffic flow
	 * gets very bumpy. Therefore we have to _switch_ to a
	 * software AC with a single HW queue.
	 */
	hw->queues = __AR9170_NUM_TXQ;

	mutex_init(&ar->mutex);
	spin_lock_init(&ar->beacon_lock);
	spin_lock_init(&ar->cmd_lock);
	spin_lock_init(&ar->tx_stats_lock);
	spin_lock_init(&ar->tx_ampdu_list_lock);
	spin_lock_init(&ar->mem_lock);
	spin_lock_init(&ar->state_lock);
	atomic_set(&ar->pending_restarts, 0);
	ar->vifs = 0;
	for (i = 0; i < ar->hw->queues; i++) {
		skb_queue_head_init(&ar->tx_status[i]);
		skb_queue_head_init(&ar->tx_pending[i]);
	}
	INIT_WORK(&ar->ps_work, carl9170_ps_work);
	INIT_WORK(&ar->ping_work, carl9170_ping_work);
	INIT_WORK(&ar->restart_work, carl9170_restart_work);
	INIT_WORK(&ar->ampdu_work, carl9170_ampdu_work);
	INIT_DELAYED_WORK(&ar->tx_janitor, carl9170_tx_janitor);
	INIT_LIST_HEAD(&ar->tx_ampdu_list);
	rcu_assign_pointer(ar->tx_ampdu_iter,
			   (struct carl9170_sta_tid *) &ar->tx_ampdu_list);

	bitmap_zero(&ar->vif_bitmap, ar->fw.vif_num);
	INIT_LIST_HEAD(&ar->vif_list);
	init_completion(&ar->tx_flush);

	/* firmware decides which modes we support */
	hw->wiphy->interface_modes = 0;

	hw->flags |= IEEE80211_HW_RX_INCLUDES_FCS |
		     IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		     IEEE80211_HW_SUPPORTS_PS |
		     IEEE80211_HW_PS_NULLFUNC_STACK |
		     IEEE80211_HW_NEED_DTIM_PERIOD |
		     IEEE80211_HW_SIGNAL_DBM;

	if (!modparam_noht) {
		/*
		 * see the comment above, why we allow the user
		 * to disable HT by a module parameter.
		 */
		hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;
	}

	hw->extra_tx_headroom = sizeof(struct _carl9170_tx_superframe);
	hw->sta_data_size = sizeof(struct carl9170_sta_info);
	hw->vif_data_size = sizeof(struct carl9170_vif_info);

	hw->max_rates = CARL9170_TX_MAX_RATES;
	hw->max_rate_tries = CARL9170_TX_USER_RATE_TRIES;

	for (i = 0; i < ARRAY_SIZE(ar->noise); i++)
		ar->noise[i] = -95; /* ATH_DEFAULT_NOISE_FLOOR */

	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
	return ar;

err_nomem:
	kfree_skb(skb);
	return ERR_PTR(-ENOMEM);
}

static int carl9170_read_eeprom(struct ar9170 *ar)
{
#define RW	8	/* number of words to read at once */
#define RB	(sizeof(u32) * RW)
	u8 *eeprom = (void *)&ar->eeprom;
	__le32 offsets[RW];
	int i, j, err;

	BUILD_BUG_ON(sizeof(ar->eeprom) & 3);

	BUILD_BUG_ON(RB > CARL9170_MAX_CMD_LEN - 4);
#ifndef __CHECKER__
	/* don't want to handle trailing remains */
	BUILD_BUG_ON(sizeof(ar->eeprom) % RB);
#endif

	for (i = 0; i < sizeof(ar->eeprom) / RB; i++) {
		for (j = 0; j < RW; j++)
			offsets[j] = cpu_to_le32(AR9170_EEPROM_START +
						 RB * i + 4 * j);

		err = carl9170_exec_cmd(ar, CARL9170_CMD_RREG,
					RB, (u8 *) &offsets,
					RB, eeprom + RB * i);
		if (err)
			return err;
	}

#undef RW
#undef RB
	return 0;
}

static int carl9170_parse_eeprom(struct ar9170 *ar)
{
	struct ath_regulatory *regulatory = &ar->common.regulatory;
	unsigned int rx_streams, tx_streams, tx_params = 0;
	int bands = 0;

	if (ar->eeprom.length == cpu_to_le16(0xffff))
		return -ENODATA;

	rx_streams = hweight8(ar->eeprom.rx_mask);
	tx_streams = hweight8(ar->eeprom.tx_mask);

	if (rx_streams != tx_streams) {
		tx_params = IEEE80211_HT_MCS_TX_RX_DIFF;

		WARN_ON(!(tx_streams >= 1 && tx_streams <=
			IEEE80211_HT_MCS_TX_MAX_STREAMS));

		tx_params = (tx_streams - 1) <<
			    IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;

		carl9170_band_2GHz.ht_cap.mcs.tx_params |= tx_params;
		carl9170_band_5GHz.ht_cap.mcs.tx_params |= tx_params;
	}

	if (ar->eeprom.operating_flags & AR9170_OPFLAG_2GHZ) {
		ar->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&carl9170_band_2GHz;
		bands++;
	}
	if (ar->eeprom.operating_flags & AR9170_OPFLAG_5GHZ) {
		ar->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&carl9170_band_5GHz;
		bands++;
	}

	/*
	 * I measured this, a bandswitch takes roughly
	 * 135 ms and a frequency switch about 80.
	 *
	 * FIXME: measure these values again once EEPROM settings
	 *	  are used, that will influence them!
	 */
	if (bands == 2)
		ar->hw->channel_change_time = 135 * 1000;
	else
		ar->hw->channel_change_time = 80 * 1000;

	regulatory->current_rd = le16_to_cpu(ar->eeprom.reg_domain[0]);
	regulatory->current_rd_ext = le16_to_cpu(ar->eeprom.reg_domain[1]);

	/* second part of wiphy init */
	SET_IEEE80211_PERM_ADDR(ar->hw, ar->eeprom.mac_address);

	return bands ? 0 : -EINVAL;
}

static int carl9170_reg_notifier(struct wiphy *wiphy,
				 struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ar9170 *ar = hw->priv;

	return ath_reg_notifier_apply(wiphy, request, &ar->common.regulatory);
}

int carl9170_register(struct ar9170 *ar)
{
	struct ath_regulatory *regulatory = &ar->common.regulatory;
	int err = 0, i;

	if (WARN_ON(ar->mem_bitmap))
		return -EINVAL;

	ar->mem_bitmap = kzalloc(roundup(ar->fw.mem_blocks, BITS_PER_LONG) *
				 sizeof(unsigned long), GFP_KERNEL);

	if (!ar->mem_bitmap)
		return -ENOMEM;

	/* try to read EEPROM, init MAC addr */
	err = carl9170_read_eeprom(ar);
	if (err)
		return err;

	err = carl9170_fw_fix_eeprom(ar);
	if (err)
		return err;

	err = carl9170_parse_eeprom(ar);
	if (err)
		return err;

	err = ath_regd_init(regulatory, ar->hw->wiphy,
			    carl9170_reg_notifier);
	if (err)
		return err;

	if (modparam_noht) {
		carl9170_band_2GHz.ht_cap.ht_supported = false;
		carl9170_band_5GHz.ht_cap.ht_supported = false;
	}

	for (i = 0; i < ar->fw.vif_num; i++) {
		ar->vif_priv[i].id = i;
		ar->vif_priv[i].vif = NULL;
	}

	err = ieee80211_register_hw(ar->hw);
	if (err)
		return err;

	/* mac80211 interface is now registered */
	ar->registered = true;

	if (!ath_is_world_regd(regulatory))
		regulatory_hint(ar->hw->wiphy, regulatory->alpha2);

#ifdef CONFIG_CARL9170_DEBUGFS
	carl9170_debugfs_register(ar);
#endif /* CONFIG_CARL9170_DEBUGFS */

	err = carl9170_led_init(ar);
	if (err)
		goto err_unreg;

#ifdef CONFIG_CARL9170_LEDS
	err = carl9170_led_register(ar);
	if (err)
		goto err_unreg;
#endif /* CONFIG_CARL9170_LEDS */

#ifdef CONFIG_CARL9170_WPC
	err = carl9170_register_wps_button(ar);
	if (err)
		goto err_unreg;
#endif /* CONFIG_CARL9170_WPC */

	dev_info(&ar->udev->dev, "Atheros AR9170 is registered as '%s'\n",
		 wiphy_name(ar->hw->wiphy));

	return 0;

err_unreg:
	carl9170_unregister(ar);
	return err;
}

void carl9170_unregister(struct ar9170 *ar)
{
	if (!ar->registered)
		return;

	ar->registered = false;

#ifdef CONFIG_CARL9170_LEDS
	carl9170_led_unregister(ar);
#endif /* CONFIG_CARL9170_LEDS */

#ifdef CONFIG_CARL9170_DEBUGFS
	carl9170_debugfs_unregister(ar);
#endif /* CONFIG_CARL9170_DEBUGFS */

#ifdef CONFIG_CARL9170_WPC
	if (ar->wps.pbc) {
		input_unregister_device(ar->wps.pbc);
		ar->wps.pbc = NULL;
	}
#endif /* CONFIG_CARL9170_WPC */

	carl9170_cancel_worker(ar);
	cancel_work_sync(&ar->restart_work);

	ieee80211_unregister_hw(ar->hw);
}

void carl9170_free(struct ar9170 *ar)
{
	WARN_ON(ar->registered);
	WARN_ON(IS_INITIALIZED(ar));

	kfree_skb(ar->rx_failover);
	ar->rx_failover = NULL;

	kfree(ar->mem_bitmap);
	ar->mem_bitmap = NULL;

	mutex_destroy(&ar->mutex);

	ieee80211_free_hw(ar->hw);
}
