/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ath9k.h"

static void ath9k_tx99_stop(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	ath_drain_all_txq(sc);
	ath_startrecv(sc);

	ath9k_hw_set_interrupts(ah);
	ath9k_hw_enable_interrupts(ah);

	ieee80211_wake_queues(sc->hw);

	kfree_skb(sc->tx99_skb);
	sc->tx99_skb = NULL;
	sc->tx99_state = false;

	ath9k_hw_tx99_stop(sc->sc_ah);
	ath_dbg(common, XMIT, "TX99 stopped\n");
}

static struct sk_buff *ath9k_build_tx99_skb(struct ath_softc *sc)
{
	static u8 PN9Data[] = {0xff, 0x87, 0xb8, 0x59, 0xb7, 0xa1, 0xcc, 0x24,
			       0x57, 0x5e, 0x4b, 0x9c, 0x0e, 0xe9, 0xea, 0x50,
			       0x2a, 0xbe, 0xb4, 0x1b, 0xb6, 0xb0, 0x5d, 0xf1,
			       0xe6, 0x9a, 0xe3, 0x45, 0xfd, 0x2c, 0x53, 0x18,
			       0x0c, 0xca, 0xc9, 0xfb, 0x49, 0x37, 0xe5, 0xa8,
			       0x51, 0x3b, 0x2f, 0x61, 0xaa, 0x72, 0x18, 0x84,
			       0x02, 0x23, 0x23, 0xab, 0x63, 0x89, 0x51, 0xb3,
			       0xe7, 0x8b, 0x72, 0x90, 0x4c, 0xe8, 0xfb, 0xc0};
	u32 len = 1200;
	struct ieee80211_tx_rate *rate;
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *tx_info;
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_put(skb, len);

	memset(skb->data, 0, len);

	hdr = (struct ieee80211_hdr *)skb->data;
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA);
	hdr->duration_id = 0;

	memcpy(hdr->addr1, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr2, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr3, hw->wiphy->perm_addr, ETH_ALEN);

	hdr->seq_ctrl |= cpu_to_le16(sc->tx.seq_no);

	tx_info = IEEE80211_SKB_CB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	rate = &tx_info->control.rates[0];
	tx_info->band = hw->conf.chandef.chan->band;
	tx_info->flags = IEEE80211_TX_CTL_NO_ACK;
	tx_info->control.vif = sc->tx99_vif;
	rate->count = 1;
	if (ah->curchan && IS_CHAN_HT(ah->curchan)) {
		rate->flags |= IEEE80211_TX_RC_MCS;
		if (IS_CHAN_HT40(ah->curchan))
			rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	}

	memcpy(skb->data + sizeof(*hdr), PN9Data, sizeof(PN9Data));

	return skb;
}

static void ath9k_tx99_deinit(struct ath_softc *sc)
{
	ath_reset(sc);

	ath9k_ps_wakeup(sc);
	ath9k_tx99_stop(sc);
	ath9k_ps_restore(sc);
}

static int ath9k_tx99_init(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_tx_control txctl;
	int r;

	if (test_bit(SC_OP_INVALID, &sc->sc_flags)) {
		ath_err(common,
			"driver is in invalid state unable to use TX99");
		return -EINVAL;
	}

	sc->tx99_skb = ath9k_build_tx99_skb(sc);
	if (!sc->tx99_skb)
		return -ENOMEM;

	memset(&txctl, 0, sizeof(txctl));
	txctl.txq = sc->tx.txq_map[IEEE80211_AC_VO];

	ath_reset(sc);

	ath9k_ps_wakeup(sc);

	ath9k_hw_disable_interrupts(ah);
	atomic_set(&ah->intr_ref_cnt, -1);
	ath_drain_all_txq(sc);
	ath_stoprecv(sc);

	sc->tx99_state = true;

	ieee80211_stop_queues(hw);

	if (sc->tx99_power == MAX_RATE_POWER + 1)
		sc->tx99_power = MAX_RATE_POWER;

	ath9k_hw_tx99_set_txpower(ah, sc->tx99_power);
	r = ath9k_tx99_send(sc, sc->tx99_skb, &txctl);
	if (r) {
		ath_dbg(common, XMIT, "Failed to xmit TX99 skb\n");
		return r;
	}

	ath_dbg(common, XMIT, "TX99 xmit started using %d ( %ddBm)\n",
		sc->tx99_power,
		sc->tx99_power / 2);

	/* We leave the harware awake as it will be chugging on */

	return 0;
}

static ssize_t read_file_tx99(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[3];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->tx99_state);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_tx99(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	bool start;
	ssize_t len;
	int r;

	if (sc->nvifs > 1)
		return -EOPNOTSUPP;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	if (strtobool(buf, &start))
		return -EINVAL;

	if (start == sc->tx99_state) {
		if (!start)
			return count;
		ath_dbg(common, XMIT, "Resetting TX99\n");
		ath9k_tx99_deinit(sc);
	}

	if (!start) {
		ath9k_tx99_deinit(sc);
		return count;
	}

	r = ath9k_tx99_init(sc);
	if (r)
		return r;

	return count;
}

static const struct file_operations fops_tx99 = {
	.read = read_file_tx99,
	.write = write_file_tx99,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_tx99_power(struct file *file,
				    char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d (%d dBm)\n",
		      sc->tx99_power,
		      sc->tx99_power / 2);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_tx99_power(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	int r;
	u8 tx_power;

	r = kstrtou8_from_user(user_buf, count, 0, &tx_power);
	if (r)
		return r;

	if (tx_power > MAX_RATE_POWER)
		return -EINVAL;

	sc->tx99_power = tx_power;

	ath9k_ps_wakeup(sc);
	ath9k_hw_tx99_set_txpower(sc->sc_ah, sc->tx99_power);
	ath9k_ps_restore(sc);

	return count;
}

static const struct file_operations fops_tx99_power = {
	.read = read_file_tx99_power,
	.write = write_file_tx99_power,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_tx99_init_debug(struct ath_softc *sc)
{
	if (!AR_SREV_9300_20_OR_LATER(sc->sc_ah))
		return;

	debugfs_create_file("tx99", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_tx99);
	debugfs_create_file("tx99_power", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_tx99_power);
}
