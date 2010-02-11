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

#define REG_WRITE_D(_ah, _reg, _val) \
	ath9k_hw_common(_ah)->ops->write((_ah), (_val), (_reg))
#define REG_READ_D(_ah, _reg) \
	ath9k_hw_common(_ah)->ops->read((_ah), (_reg))

static struct dentry *ath9k_debugfs_root;

static int ath9k_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#ifdef CONFIG_ATH_DEBUG

static ssize_t read_file_debug(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = snprintf(buf, sizeof(buf), "0x%08x\n", common->debug_mask);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_debug(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long mask;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EINVAL;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &mask))
		return -EINVAL;

	common->debug_mask = mask;
	return count;
}

static const struct file_operations fops_debug = {
	.read = read_file_debug,
	.write = write_file_debug,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

#endif

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

	ath9k_ps_wakeup(sc);

	REG_WRITE_D(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	len += snprintf(buf + len, sizeof(buf) - len,
			"Raw DMA Debug values:\n");

	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			len += snprintf(buf + len, sizeof(buf) - len, "\n");

		val[i] = REG_READ_D(ah, AR_DMADBG_0 + (i * sizeof(u32)));
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
			REG_READ_D(ah, AR_OBS_BUS_1));
	len += snprintf(buf + len, sizeof(buf) - len,
			"AR_CR: 0x%x \n", REG_READ_D(ah, AR_CR));

	ath9k_ps_restore(sc);

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

void ath_debug_stat_rc(struct ath_softc *sc, int final_rate)
{
	struct ath_rc_stats *stats;

	stats = &sc->debug.stats.rcstats[final_rate];
	stats->success++;
}

void ath_debug_stat_retries(struct ath_softc *sc, int rix,
			    int xretries, int retries, u8 per)
{
	struct ath_rc_stats *stats = &sc->debug.stats.rcstats[rix];

	stats->xretries += xretries;
	stats->retries += retries;
	stats->per = per;
}

static ssize_t read_file_rcstat(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, max;
	int i = 0;
	ssize_t retval;

	if (sc->cur_rate_table == NULL)
		return 0;

	max = 80 + sc->cur_rate_table->rate_cnt * 64;
	buf = kmalloc(max + 1, GFP_KERNEL);
	if (buf == NULL)
		return 0;
	buf[max] = 0;

	len += sprintf(buf, "%5s %15s %8s %9s %3s\n\n", "Rate", "Success",
		       "Retries", "XRetries", "PER");

	for (i = 0; i < sc->cur_rate_table->rate_cnt; i++) {
		u32 ratekbps = sc->cur_rate_table->info[i].ratekbps;
		struct ath_rc_stats *stats = &sc->debug.stats.rcstats[i];

		len += snprintf(buf + len, max - len,
			"%3u.%d: %8u %8u %8u %8u\n", ratekbps / 1000,
			(ratekbps % 1000) / 100, stats->success,
			stats->retries, stats->xretries,
			stats->per);
	}

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return retval;
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

	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_STA_ID0), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_STA_ID1) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addr: %pM\n", addr);
	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_BSSMSKL), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_BSSMSKU) & 0xffff, addr + 4);
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

#define PR(str, elem)							\
	do {								\
		len += snprintf(buf + len, size - len,			\
				"%s%13u%11u%10u%10u\n", str,		\
		sc->debug.stats.txstats[sc->tx.hwq_map[ATH9K_WME_AC_BE]].elem, \
		sc->debug.stats.txstats[sc->tx.hwq_map[ATH9K_WME_AC_BK]].elem, \
		sc->debug.stats.txstats[sc->tx.hwq_map[ATH9K_WME_AC_VI]].elem, \
		sc->debug.stats.txstats[sc->tx.hwq_map[ATH9K_WME_AC_VO]].elem); \
} while(0)

static ssize_t read_file_xmit(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 2048;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return 0;

	len += sprintf(buf, "%30s %10s%10s%10s\n\n", "BE", "BK", "VI", "VO");

	PR("MPDUs Queued:    ", queued);
	PR("MPDUs Completed: ", completed);
	PR("Aggregates:      ", a_aggr);
	PR("AMPDUs Queued:   ", a_queued);
	PR("AMPDUs Completed:", a_completed);
	PR("AMPDUs Retried:  ", a_retries);
	PR("AMPDUs XRetried: ", a_xretries);
	PR("FIFO Underrun:   ", fifo_underrun);
	PR("TXOP Exceeded:   ", xtxop);
	PR("TXTIMER Expiry:  ", timer_exp);
	PR("DESC CFG Error:  ", desc_cfg_err);
	PR("DATA Underrun:   ", data_underrun);
	PR("DELIM Underrun:  ", delim_underrun);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

void ath_debug_stat_tx(struct ath_softc *sc, struct ath_txq *txq,
		       struct ath_buf *bf)
{
	struct ath_desc *ds = bf->bf_desc;

	if (bf_isampdu(bf)) {
		if (bf_isxretried(bf))
			TX_STAT_INC(txq->axq_qnum, a_xretries);
		else
			TX_STAT_INC(txq->axq_qnum, a_completed);
	} else {
		TX_STAT_INC(txq->axq_qnum, completed);
	}

	if (ds->ds_txstat.ts_status & ATH9K_TXERR_FIFO)
		TX_STAT_INC(txq->axq_qnum, fifo_underrun);
	if (ds->ds_txstat.ts_status & ATH9K_TXERR_XTXOP)
		TX_STAT_INC(txq->axq_qnum, xtxop);
	if (ds->ds_txstat.ts_status & ATH9K_TXERR_TIMER_EXPIRED)
		TX_STAT_INC(txq->axq_qnum, timer_exp);
	if (ds->ds_txstat.ts_flags & ATH9K_TX_DESC_CFG_ERR)
		TX_STAT_INC(txq->axq_qnum, desc_cfg_err);
	if (ds->ds_txstat.ts_flags & ATH9K_TX_DATA_UNDERRUN)
		TX_STAT_INC(txq->axq_qnum, data_underrun);
	if (ds->ds_txstat.ts_flags & ATH9K_TX_DELIM_UNDERRUN)
		TX_STAT_INC(txq->axq_qnum, delim_underrun);
}

static const struct file_operations fops_xmit = {
	.read = read_file_xmit,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

int ath9k_init_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	if (!ath9k_debugfs_root)
		return -ENOENT;

	sc->debug.debugfs_phy = debugfs_create_dir(wiphy_name(sc->hw->wiphy),
						      ath9k_debugfs_root);
	if (!sc->debug.debugfs_phy)
		goto err;

#ifdef CONFIG_ATH_DEBUG
	sc->debug.debugfs_debug = debugfs_create_file("debug",
		S_IRUSR | S_IWUSR, sc->debug.debugfs_phy, sc, &fops_debug);
	if (!sc->debug.debugfs_debug)
		goto err;
#endif

	sc->debug.debugfs_dma = debugfs_create_file("dma", S_IRUSR,
				       sc->debug.debugfs_phy, sc, &fops_dma);
	if (!sc->debug.debugfs_dma)
		goto err;

	sc->debug.debugfs_interrupt = debugfs_create_file("interrupt",
						     S_IRUSR,
						     sc->debug.debugfs_phy,
						     sc, &fops_interrupt);
	if (!sc->debug.debugfs_interrupt)
		goto err;

	sc->debug.debugfs_rcstat = debugfs_create_file("rcstat",
						  S_IRUSR,
						  sc->debug.debugfs_phy,
						  sc, &fops_rcstat);
	if (!sc->debug.debugfs_rcstat)
		goto err;

	sc->debug.debugfs_wiphy = debugfs_create_file(
		"wiphy", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy, sc,
		&fops_wiphy);
	if (!sc->debug.debugfs_wiphy)
		goto err;

	sc->debug.debugfs_xmit = debugfs_create_file("xmit",
						     S_IRUSR,
						     sc->debug.debugfs_phy,
						     sc, &fops_xmit);
	if (!sc->debug.debugfs_xmit)
		goto err;

	return 0;
err:
	ath9k_exit_debug(ah);
	return -ENOMEM;
}

void ath9k_exit_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	debugfs_remove(sc->debug.debugfs_xmit);
	debugfs_remove(sc->debug.debugfs_wiphy);
	debugfs_remove(sc->debug.debugfs_rcstat);
	debugfs_remove(sc->debug.debugfs_interrupt);
	debugfs_remove(sc->debug.debugfs_dma);
	debugfs_remove(sc->debug.debugfs_debug);
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
