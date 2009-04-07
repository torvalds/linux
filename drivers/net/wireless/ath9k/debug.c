/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#include <asm/unaligned.h>

#include "ath9k.h"

static unsigned int ath9k_debug = DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);

static struct dentry *ath9k_debugfs_root;

void DPRINTF(struct ath_softc *sc, int dbg_mask, const char *fmt, ...)
{
	if (!sc)
		return;

	if (sc->debug.debug_mask & dbg_mask) {
		va_list args;

		va_start(args, fmt);
		printk(KERN_DEBUG "ath9k: ");
		vprintk(fmt, args);
		va_end(args);
	}
}

static int ath9k_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t read_file_dma(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	char buf[1024];
	unsigned int len = 0;
	u32 val[ATH9K_NUM_DMA_DEBUG_REGS];
	int i, qcuOffset = 0, dcuOffset = 0;
	u32 *qcuBase = &val[0], *dcuBase = &val[4];

	REG_WRITE(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	len += snprintf(buf + len, sizeof(buf) - len,
			"Raw DMA Debug values:\n");

	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			len += snprintf(buf + len, sizeof(buf) - len, "\n");

		val[i] = REG_READ(ah, AR_DMADBG_0 + (i * sizeof(u32)));
		len += snprintf(buf + len, sizeof(buf) - len, "%d: %08x ",
				i, val[i]);
	}

	len += snprintf(buf + len, sizeof(buf) - len, "\n\n");
	len += snprintf(buf + len, sizeof(buf) - len,
			"Num QCU: chain_st fsp_ok fsp_st DCU: chain_st\n");

	for (i = 0; i < ATH9K_NUM_QUEUES; i++, qcuOffset += 4, dcuOffset += 5) {
		if (i == 8) {
			qcuOffset = 0;
			qcuBase++;
		}

		if (i == 6) {
			dcuOffset = 0;
			dcuBase++;
		}

		len += snprintf(buf + len, sizeof(buf) - len,
			"%2d          %2x      %1x     %2x           %2x\n",
			i, (*qcuBase & (0x7 << qcuOffset)) >> qcuOffset,
			(*qcuBase & (0x8 << qcuOffset)) >> (qcuOffset + 3),
			val[2] & (0x7 << (i * 3)) >> (i * 3),
			(*dcuBase & (0x1f << dcuOffset)) >> dcuOffset);
	}

	len += snprintf(buf + len, sizeof(buf) - len, "\n");

	len += snprintf(buf + len, sizeof(buf) - len,
		"qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
		(val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22);
	len += snprintf(buf + len, sizeof(buf) - len,
		"qcu_complete state: %2x    dcu_complete state:     %2x\n",
		(val[3] & 0x1c000000) >> 26, (val[6] & 0x3));
	len += snprintf(buf + len, sizeof(buf) - len,
		"dcu_arb state:      %2x    dcu_fp state:           %2x\n",
		(val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27);
	len += snprintf(buf + len, sizeof(buf) - len,
		"chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
		(val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10);
	len += snprintf(buf + len, sizeof(buf) - len,
		"txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
		(val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12);
	len += snprintf(buf + len, sizeof(buf) - len,
		"txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
		(val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17);

	len += snprintf(buf + len, sizeof(buf) - len, "pcu observe: 0x%x \n",
			REG_READ(ah, AR_OBS_BUS_1));
	len += snprintf(buf + len, sizeof(buf) - len,
			"AR_CR: 0x%x \n", REG_READ(ah, AR_CR));

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_dma = {
	.read = read_file_dma,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};


void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status)
{
	if (status)
		sc->debug.stats.istats.total++;
	if (status & ATH9K_INT_RX)
		sc->debug.stats.istats.rxok++;
	if (status & ATH9K_INT_RXEOL)
		sc->debug.stats.istats.rxeol++;
	if (status & ATH9K_INT_RXORN)
		sc->debug.stats.istats.rxorn++;
	if (status & ATH9K_INT_TX)
		sc->debug.stats.istats.txok++;
	if (status & ATH9K_INT_TXURN)
		sc->debug.stats.istats.txurn++;
	if (status & ATH9K_INT_MIB)
		sc->debug.stats.istats.mib++;
	if (status & ATH9K_INT_RXPHY)
		sc->debug.stats.istats.rxphyerr++;
	if (status & ATH9K_INT_RXKCM)
		sc->debug.stats.istats.rx_keycache_miss++;
	if (status & ATH9K_INT_SWBA)
		sc->debug.stats.istats.swba++;
	if (status & ATH9K_INT_BMISS)
		sc->debug.stats.istats.bmiss++;
	if (status & ATH9K_INT_BNR)
		sc->debug.stats.istats.bnr++;
	if (status & ATH9K_INT_CST)
		sc->debug.stats.istats.cst++;
	if (status & ATH9K_INT_GTT)
		sc->debug.stats.istats.gtt++;
	if (status & ATH9K_INT_TIM)
		sc->debug.stats.istats.tim++;
	if (status & ATH9K_INT_CABEND)
		sc->debug.stats.istats.cabend++;
	if (status & ATH9K_INT_DTIMSYNC)
		sc->debug.stats.istats.dtimsync++;
	if (status & ATH9K_INT_DTIM)
		sc->debug.stats.istats.dtim++;
}

static ssize_t read_file_interrupt(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[512];
	unsigned int len = 0;

	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RX", sc->debug.stats.istats.rxok);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXEOL", sc->debug.stats.istats.rxeol);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXORN", sc->debug.stats.istats.rxorn);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TX", sc->debug.stats.istats.txok);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TXURN", sc->debug.stats.istats.txurn);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "MIB", sc->debug.stats.istats.mib);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXPHY", sc->debug.stats.istats.rxphyerr);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXKCM", sc->debug.stats.istats.rx_keycache_miss);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "SWBA", sc->debug.stats.istats.swba);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "BMISS", sc->debug.stats.istats.bmiss);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "BNR", sc->debug.stats.istats.bnr);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "CST", sc->debug.stats.istats.cst);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "GTT", sc->debug.stats.istats.gtt);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TIM", sc->debug.stats.istats.tim);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "CABEND", sc->debug.stats.istats.cabend);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "DTIMSYNC", sc->debug.stats.istats.dtimsync);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "DTIM", sc->debug.stats.istats.dtim);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TOTAL", sc->debug.stats.istats.total);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_interrupt = {
	.read = read_file_interrupt,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

static void ath_debug_stat_11n_rc(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ath_tx_info_priv *tx_info_priv = NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->status.rates;
	int final_ts_idx, idx;

	tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	final_ts_idx = tx_info_priv->tx.ts_rateindex;
	idx = sc->cur_rate_table->info[rates[final_ts_idx].idx].dot11rate;

	sc->debug.stats.n_rcstats[idx].success++;
}

static void ath_debug_stat_legacy_rc(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ath_tx_info_priv *tx_info_priv = NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->status.rates;
	int final_ts_idx, idx;

	tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	final_ts_idx = tx_info_priv->tx.ts_rateindex;
	idx = rates[final_ts_idx].idx;

	sc->debug.stats.legacy_rcstats[idx].success++;
}

void ath_debug_stat_rc(struct ath_softc *sc, struct sk_buff *skb)
{
	if (conf_is_ht(&sc->hw->conf))
		ath_debug_stat_11n_rc(sc, skb);
	else
		ath_debug_stat_legacy_rc(sc, skb);
}

/* FIXME: legacy rates, later on .. */
void ath_debug_stat_retries(struct ath_softc *sc, int rix,
			    int xretries, int retries, u8 per)
{
	if (conf_is_ht(&sc->hw->conf)) {
		int idx = sc->cur_rate_table->info[rix].dot11rate;

		sc->debug.stats.n_rcstats[idx].xretries += xretries;
		sc->debug.stats.n_rcstats[idx].retries += retries;
		sc->debug.stats.n_rcstats[idx].per = per;
	}
}

static ssize_t ath_read_file_stat_11n_rc(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[1024];
	unsigned int len = 0;
	int i = 0;

	len += sprintf(buf, "%7s %13s %8s %8s %6s\n\n", "Rate", "Success",
		       "Retries", "XRetries", "PER");

	for (i = 0; i <= 15; i++) {
		len += snprintf(buf + len, sizeof(buf) - len,
				"%5s%3d: %8u %8u %8u %8u\n", "MCS", i,
				sc->debug.stats.n_rcstats[i].success,
				sc->debug.stats.n_rcstats[i].retries,
				sc->debug.stats.n_rcstats[i].xretries,
				sc->debug.stats.n_rcstats[i].per);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath_read_file_stat_legacy_rc(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[512];
	unsigned int len = 0;
	int i = 0;

	len += sprintf(buf, "%7s %13s\n\n", "Rate", "Success");

	for (i = 0; i < sc->cur_rate_table->rate_cnt; i++) {
		len += snprintf(buf + len, sizeof(buf) - len, "%5u: %12u\n",
				sc->cur_rate_table->info[i].ratekbps / 1000,
				sc->debug.stats.legacy_rcstats[i].success);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t read_file_rcstat(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;

	if (sc->cur_rate_table == NULL)
		return 0;

	if (conf_is_ht(&sc->hw->conf))
		return ath_read_file_stat_11n_rc(file, user_buf, count, ppos);
	else
		return ath_read_file_stat_legacy_rc(file, user_buf, count ,ppos);
}

static const struct file_operations fops_rcstat = {
	.read = read_file_rcstat,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

static const char * ath_wiphy_state_str(enum ath_wiphy_state state)
{
	switch (state) {
	case ATH_WIPHY_INACTIVE:
		return "INACTIVE";
	case ATH_WIPHY_ACTIVE:
		return "ACTIVE";
	case ATH_WIPHY_PAUSING:
		return "PAUSING";
	case ATH_WIPHY_PAUSED:
		return "PAUSED";
	case ATH_WIPHY_SCAN:
		return "SCAN";
	}
	return "?";
}

static ssize_t read_file_wiphy(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[512];
	unsigned int len = 0;
	int i;
	u8 addr[ETH_ALEN];

	len += snprintf(buf + len, sizeof(buf) - len,
			"primary: %s (%s chan=%d ht=%d)\n",
			wiphy_name(sc->pri_wiphy->hw->wiphy),
			ath_wiphy_state_str(sc->pri_wiphy->state),
			sc->pri_wiphy->chan_idx, sc->pri_wiphy->chan_is_ht);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (aphy == NULL)
			continue;
		len += snprintf(buf + len, sizeof(buf) - len,
				"secondary: %s (%s chan=%d ht=%d)\n",
				wiphy_name(aphy->hw->wiphy),
				ath_wiphy_state_str(aphy->state),
				aphy->chan_idx, aphy->chan_is_ht);
	}

	put_unaligned_le32(REG_READ(sc->sc_ah, AR_STA_ID0), addr);
	put_unaligned_le16(REG_READ(sc->sc_ah, AR_STA_ID1) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addr: %pM\n", addr);
	put_unaligned_le32(REG_READ(sc->sc_ah, AR_BSSMSKL), addr);
	put_unaligned_le16(REG_READ(sc->sc_ah, AR_BSSMSKU) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addrmask: %pM\n", addr);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static struct ath_wiphy * get_wiphy(struct ath_softc *sc, const char *name)
{
	int i;
	if (strcmp(name, wiphy_name(sc->pri_wiphy->hw->wiphy)) == 0)
		return sc->pri_wiphy;
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (aphy && strcmp(name, wiphy_name(aphy->hw->wiphy)) == 0)
			return aphy;
	}
	return NULL;
}

static int del_wiphy(struct ath_softc *sc, const char *name)
{
	struct ath_wiphy *aphy = get_wiphy(sc, name);
	if (!aphy)
		return -ENOENT;
	return ath9k_wiphy_del(aphy);
}

static int pause_wiphy(struct ath_softc *sc, const char *name)
{
	struct ath_wiphy *aphy = get_wiphy(sc, name);
	if (!aphy)
		return -ENOENT;
	return ath9k_wiphy_pause(aphy);
}

static int unpause_wiphy(struct ath_softc *sc, const char *name)
{
	struct ath_wiphy *aphy = get_wiphy(sc, name);
	if (!aphy)
		return -ENOENT;
	return ath9k_wiphy_unpause(aphy);
}

static int select_wiphy(struct ath_softc *sc, const char *name)
{
	struct ath_wiphy *aphy = get_wiphy(sc, name);
	if (!aphy)
		return -ENOENT;
	return ath9k_wiphy_select(aphy);
}

static int schedule_wiphy(struct ath_softc *sc, const char *msec)
{
	ath9k_wiphy_set_scheduler(sc, simple_strtoul(msec, NULL, 0));
	return 0;
}

static ssize_t write_file_wiphy(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[50];
	size_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	if (strncmp(buf, "add", 3) == 0) {
		int res = ath9k_wiphy_add(sc);
		if (res < 0)
			return res;
	} else if (strncmp(buf, "del=", 4) == 0) {
		int res = del_wiphy(sc, buf + 4);
		if (res < 0)
			return res;
	} else if (strncmp(buf, "pause=", 6) == 0) {
		int res = pause_wiphy(sc, buf + 6);
		if (res < 0)
			return res;
	} else if (strncmp(buf, "unpause=", 8) == 0) {
		int res = unpause_wiphy(sc, buf + 8);
		if (res < 0)
			return res;
	} else if (strncmp(buf, "select=", 7) == 0) {
		int res = select_wiphy(sc, buf + 7);
		if (res < 0)
			return res;
	} else if (strncmp(buf, "schedule=", 9) == 0) {
		int res = schedule_wiphy(sc, buf + 9);
		if (res < 0)
			return res;
	} else
		return -EOPNOTSUPP;

	return count;
}

static const struct file_operations fops_wiphy = {
	.read = read_file_wiphy,
	.write = write_file_wiphy,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};


int ath9k_init_debug(struct ath_softc *sc)
{
	sc->debug.debug_mask = ath9k_debug;

	sc->debug.debugfs_phy = debugfs_create_dir(wiphy_name(sc->hw->wiphy),
						      ath9k_debugfs_root);
	if (!sc->debug.debugfs_phy)
		goto err;

	sc->debug.debugfs_dma = debugfs_create_file("dma", S_IRUGO,
				       sc->debug.debugfs_phy, sc, &fops_dma);
	if (!sc->debug.debugfs_dma)
		goto err;

	sc->debug.debugfs_interrupt = debugfs_create_file("interrupt",
						     S_IRUGO,
						     sc->debug.debugfs_phy,
						     sc, &fops_interrupt);
	if (!sc->debug.debugfs_interrupt)
		goto err;

	sc->debug.debugfs_rcstat = debugfs_create_file("rcstat",
						  S_IRUGO,
						  sc->debug.debugfs_phy,
						  sc, &fops_rcstat);
	if (!sc->debug.debugfs_rcstat)
		goto err;

	sc->debug.debugfs_wiphy = debugfs_create_file(
		"wiphy", S_IRUGO | S_IWUSR, sc->debug.debugfs_phy, sc,
		&fops_wiphy);
	if (!sc->debug.debugfs_wiphy)
		goto err;

	return 0;
err:
	ath9k_exit_debug(sc);
	return -ENOMEM;
}

void ath9k_exit_debug(struct ath_softc *sc)
{
	debugfs_remove(sc->debug.debugfs_wiphy);
	debugfs_remove(sc->debug.debugfs_rcstat);
	debugfs_remove(sc->debug.debugfs_interrupt);
	debugfs_remove(sc->debug.debugfs_dma);
	debugfs_remove(sc->debug.debugfs_phy);
}

int ath9k_debug_create_root(void)
{
	ath9k_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!ath9k_debugfs_root)
		return -ENOENT;

	return 0;
}

void ath9k_debug_remove_root(void)
{
	debugfs_remove(ath9k_debugfs_root);
	ath9k_debugfs_root = NULL;
}
