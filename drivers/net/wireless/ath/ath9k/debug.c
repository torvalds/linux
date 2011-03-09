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

#include <linux/slab.h>
#include <asm/unaligned.h>

#include "ath9k.h"

#define REG_WRITE_D(_ah, _reg, _val) \
	ath9k_hw_common(_ah)->ops->write((_ah), (_val), (_reg))
#define REG_READ_D(_ah, _reg) \
	ath9k_hw_common(_ah)->ops->read((_ah), (_reg))

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

	len = sprintf(buf, "0x%08x\n", common->debug_mask);
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
		return -EFAULT;

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
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#endif

#define DMA_BUF_LEN 1024

static ssize_t read_file_tx_chainmask(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%08x\n", common->tx_chainmask);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_tx_chainmask(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long mask;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &mask))
		return -EINVAL;

	common->tx_chainmask = mask;
	sc->sc_ah->caps.tx_chainmask = mask;
	return count;
}

static const struct file_operations fops_tx_chainmask = {
	.read = read_file_tx_chainmask,
	.write = write_file_tx_chainmask,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};


static ssize_t read_file_rx_chainmask(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%08x\n", common->rx_chainmask);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_rx_chainmask(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long mask;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &mask))
		return -EINVAL;

	common->rx_chainmask = mask;
	sc->sc_ah->caps.rx_chainmask = mask;
	return count;
}

static const struct file_operations fops_rx_chainmask = {
	.read = read_file_rx_chainmask,
	.write = write_file_rx_chainmask,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};


static ssize_t read_file_dma(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	char *buf;
	int retval;
	unsigned int len = 0;
	u32 val[ATH9K_NUM_DMA_DEBUG_REGS];
	int i, qcuOffset = 0, dcuOffset = 0;
	u32 *qcuBase = &val[0], *dcuBase = &val[4];

	buf = kmalloc(DMA_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ath9k_ps_wakeup(sc);

	REG_WRITE_D(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	len += snprintf(buf + len, DMA_BUF_LEN - len,
			"Raw DMA Debug values:\n");

	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			len += snprintf(buf + len, DMA_BUF_LEN - len, "\n");

		val[i] = REG_READ_D(ah, AR_DMADBG_0 + (i * sizeof(u32)));
		len += snprintf(buf + len, DMA_BUF_LEN - len, "%d: %08x ",
				i, val[i]);
	}

	len += snprintf(buf + len, DMA_BUF_LEN - len, "\n\n");
	len += snprintf(buf + len, DMA_BUF_LEN - len,
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

		len += snprintf(buf + len, DMA_BUF_LEN - len,
			"%2d          %2x      %1x     %2x           %2x\n",
			i, (*qcuBase & (0x7 << qcuOffset)) >> qcuOffset,
			(*qcuBase & (0x8 << qcuOffset)) >> (qcuOffset + 3),
			val[2] & (0x7 << (i * 3)) >> (i * 3),
			(*dcuBase & (0x1f << dcuOffset)) >> dcuOffset);
	}

	len += snprintf(buf + len, DMA_BUF_LEN - len, "\n");

	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
		(val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22);
	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"qcu_complete state: %2x    dcu_complete state:     %2x\n",
		(val[3] & 0x1c000000) >> 26, (val[6] & 0x3));
	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"dcu_arb state:      %2x    dcu_fp state:           %2x\n",
		(val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27);
	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
		(val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10);
	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
		(val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12);
	len += snprintf(buf + len, DMA_BUF_LEN - len,
		"txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
		(val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17);

	len += snprintf(buf + len, DMA_BUF_LEN - len, "pcu observe: 0x%x\n",
			REG_READ_D(ah, AR_OBS_BUS_1));
	len += snprintf(buf + len, DMA_BUF_LEN - len,
			"AR_CR: 0x%x\n", REG_READ_D(ah, AR_CR));

	ath9k_ps_restore(sc);

	if (len > DMA_BUF_LEN)
		len = DMA_BUF_LEN;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return retval;
}

static const struct file_operations fops_dma = {
	.read = read_file_dma,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};


void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status)
{
	if (status)
		sc->debug.stats.istats.total++;
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		if (status & ATH9K_INT_RXLP)
			sc->debug.stats.istats.rxlp++;
		if (status & ATH9K_INT_RXHP)
			sc->debug.stats.istats.rxhp++;
		if (status & ATH9K_INT_BB_WATCHDOG)
			sc->debug.stats.istats.bb_watchdog++;
	} else {
		if (status & ATH9K_INT_RX)
			sc->debug.stats.istats.rxok++;
	}
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

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		len += snprintf(buf + len, sizeof(buf) - len,
			"%8s: %10u\n", "RXLP", sc->debug.stats.istats.rxlp);
		len += snprintf(buf + len, sizeof(buf) - len,
			"%8s: %10u\n", "RXHP", sc->debug.stats.istats.rxhp);
		len += snprintf(buf + len, sizeof(buf) - len,
			"%8s: %10u\n", "WATCHDOG",
			sc->debug.stats.istats.bb_watchdog);
	} else {
		len += snprintf(buf + len, sizeof(buf) - len,
			"%8s: %10u\n", "RX", sc->debug.stats.istats.rxok);
	}
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

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_interrupt = {
	.read = read_file_interrupt,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
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
	struct ath_wiphy *aphy = sc->pri_wiphy;
	struct ieee80211_channel *chan = aphy->hw->conf.channel;
	char buf[512];
	unsigned int len = 0;
	int i;
	u8 addr[ETH_ALEN];
	u32 tmp;

	len += snprintf(buf + len, sizeof(buf) - len,
			"primary: %s (%s chan=%d ht=%d)\n",
			wiphy_name(sc->pri_wiphy->hw->wiphy),
			ath_wiphy_state_str(sc->pri_wiphy->state),
			ieee80211_frequency_to_channel(chan->center_freq),
			aphy->chan_is_ht);

	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_STA_ID0), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_STA_ID1) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addr: %pM\n", addr);
	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_BSSMSKL), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_BSSMSKU) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addrmask: %pM\n", addr);
	tmp = ath9k_hw_getrxfilter(sc->sc_ah);
	len += snprintf(buf + len, sizeof(buf) - len,
			"rfilt: 0x%x", tmp);
	if (tmp & ATH9K_RX_FILTER_UCAST)
		len += snprintf(buf + len, sizeof(buf) - len, " UCAST");
	if (tmp & ATH9K_RX_FILTER_MCAST)
		len += snprintf(buf + len, sizeof(buf) - len, " MCAST");
	if (tmp & ATH9K_RX_FILTER_BCAST)
		len += snprintf(buf + len, sizeof(buf) - len, " BCAST");
	if (tmp & ATH9K_RX_FILTER_CONTROL)
		len += snprintf(buf + len, sizeof(buf) - len, " CONTROL");
	if (tmp & ATH9K_RX_FILTER_BEACON)
		len += snprintf(buf + len, sizeof(buf) - len, " BEACON");
	if (tmp & ATH9K_RX_FILTER_PROM)
		len += snprintf(buf + len, sizeof(buf) - len, " PROM");
	if (tmp & ATH9K_RX_FILTER_PROBEREQ)
		len += snprintf(buf + len, sizeof(buf) - len, " PROBEREQ");
	if (tmp & ATH9K_RX_FILTER_PHYERR)
		len += snprintf(buf + len, sizeof(buf) - len, " PHYERR");
	if (tmp & ATH9K_RX_FILTER_MYBEACON)
		len += snprintf(buf + len, sizeof(buf) - len, " MYBEACON");
	if (tmp & ATH9K_RX_FILTER_COMP_BAR)
		len += snprintf(buf + len, sizeof(buf) - len, " COMP_BAR");
	if (tmp & ATH9K_RX_FILTER_PSPOLL)
		len += snprintf(buf + len, sizeof(buf) - len, " PSPOLL");
	if (tmp & ATH9K_RX_FILTER_PHYRADAR)
		len += snprintf(buf + len, sizeof(buf) - len, " PHYRADAR");
	if (tmp & ATH9K_RX_FILTER_MCAST_BCAST_ALL)
		len += snprintf(buf + len, sizeof(buf) - len, " MCAST_BCAST_ALL\n");
	else
		len += snprintf(buf + len, sizeof(buf) - len, "\n");

	/* Put variable-length stuff down here, and check for overflows. */
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy_tmp = sc->sec_wiphy[i];
		if (aphy_tmp == NULL)
			continue;
		chan = aphy_tmp->hw->conf.channel;
		len += snprintf(buf + len, sizeof(buf) - len,
			"secondary: %s (%s chan=%d ht=%d)\n",
			wiphy_name(aphy_tmp->hw->wiphy),
			ath_wiphy_state_str(aphy_tmp->state),
			ieee80211_frequency_to_channel(chan->center_freq),
						       aphy_tmp->chan_is_ht);
	}
	if (len > sizeof(buf))
		len = sizeof(buf);

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
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define PR(str, elem)							\
	do {								\
		len += snprintf(buf + len, size - len,			\
				"%s%13u%11u%10u%10u\n", str,		\
		sc->debug.stats.txstats[WME_AC_BE].elem, \
		sc->debug.stats.txstats[WME_AC_BK].elem, \
		sc->debug.stats.txstats[WME_AC_VI].elem, \
		sc->debug.stats.txstats[WME_AC_VO].elem); \
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
		return -ENOMEM;

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
	PR("TX-Pkts-All:     ", tx_pkts_all);
	PR("TX-Bytes-All:    ", tx_bytes_all);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts)
{
	int qnum = skb_get_queue_mapping(bf->bf_mpdu);

	TX_STAT_INC(qnum, tx_pkts_all);
	sc->debug.stats.txstats[qnum].tx_bytes_all += bf->bf_mpdu->len;

	if (bf_isampdu(bf)) {
		if (bf_isxretried(bf))
			TX_STAT_INC(qnum, a_xretries);
		else
			TX_STAT_INC(qnum, a_completed);
	} else {
		TX_STAT_INC(qnum, completed);
	}

	if (ts->ts_status & ATH9K_TXERR_FIFO)
		TX_STAT_INC(qnum, fifo_underrun);
	if (ts->ts_status & ATH9K_TXERR_XTXOP)
		TX_STAT_INC(qnum, xtxop);
	if (ts->ts_status & ATH9K_TXERR_TIMER_EXPIRED)
		TX_STAT_INC(qnum, timer_exp);
	if (ts->ts_flags & ATH9K_TX_DESC_CFG_ERR)
		TX_STAT_INC(qnum, desc_cfg_err);
	if (ts->ts_flags & ATH9K_TX_DATA_UNDERRUN)
		TX_STAT_INC(qnum, data_underrun);
	if (ts->ts_flags & ATH9K_TX_DELIM_UNDERRUN)
		TX_STAT_INC(qnum, delim_underrun);
}

static const struct file_operations fops_xmit = {
	.read = read_file_xmit,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_recv(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
#define PHY_ERR(s, p) \
	len += snprintf(buf + len, size - len, "%18s : %10u\n", s, \
			sc->debug.stats.rxstats.phy_err_stats[p]);

	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 1152;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "CRC ERR",
			sc->debug.stats.rxstats.crc_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "DECRYPT CRC ERR",
			sc->debug.stats.rxstats.decrypt_crc_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "PHY ERR",
			sc->debug.stats.rxstats.phy_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "MIC ERR",
			sc->debug.stats.rxstats.mic_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "PRE-DELIM CRC ERR",
			sc->debug.stats.rxstats.pre_delim_crc_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "POST-DELIM CRC ERR",
			sc->debug.stats.rxstats.post_delim_crc_err);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "DECRYPT BUSY ERR",
			sc->debug.stats.rxstats.decrypt_busy_err);

	PHY_ERR("UNDERRUN", ATH9K_PHYERR_UNDERRUN);
	PHY_ERR("TIMING", ATH9K_PHYERR_TIMING);
	PHY_ERR("PARITY", ATH9K_PHYERR_PARITY);
	PHY_ERR("RATE", ATH9K_PHYERR_RATE);
	PHY_ERR("LENGTH", ATH9K_PHYERR_LENGTH);
	PHY_ERR("RADAR", ATH9K_PHYERR_RADAR);
	PHY_ERR("SERVICE", ATH9K_PHYERR_SERVICE);
	PHY_ERR("TOR", ATH9K_PHYERR_TOR);
	PHY_ERR("OFDM-TIMING", ATH9K_PHYERR_OFDM_TIMING);
	PHY_ERR("OFDM-SIGNAL-PARITY", ATH9K_PHYERR_OFDM_SIGNAL_PARITY);
	PHY_ERR("OFDM-RATE", ATH9K_PHYERR_OFDM_RATE_ILLEGAL);
	PHY_ERR("OFDM-LENGTH", ATH9K_PHYERR_OFDM_LENGTH_ILLEGAL);
	PHY_ERR("OFDM-POWER-DROP", ATH9K_PHYERR_OFDM_POWER_DROP);
	PHY_ERR("OFDM-SERVICE", ATH9K_PHYERR_OFDM_SERVICE);
	PHY_ERR("OFDM-RESTART", ATH9K_PHYERR_OFDM_RESTART);
	PHY_ERR("FALSE-RADAR-EXT", ATH9K_PHYERR_FALSE_RADAR_EXT);
	PHY_ERR("CCK-TIMING", ATH9K_PHYERR_CCK_TIMING);
	PHY_ERR("CCK-HEADER-CRC", ATH9K_PHYERR_CCK_HEADER_CRC);
	PHY_ERR("CCK-RATE", ATH9K_PHYERR_CCK_RATE_ILLEGAL);
	PHY_ERR("CCK-SERVICE", ATH9K_PHYERR_CCK_SERVICE);
	PHY_ERR("CCK-RESTART", ATH9K_PHYERR_CCK_RESTART);
	PHY_ERR("CCK-LENGTH", ATH9K_PHYERR_CCK_LENGTH_ILLEGAL);
	PHY_ERR("CCK-POWER-DROP", ATH9K_PHYERR_CCK_POWER_DROP);
	PHY_ERR("HT-CRC", ATH9K_PHYERR_HT_CRC_ERROR);
	PHY_ERR("HT-LENGTH", ATH9K_PHYERR_HT_LENGTH_ILLEGAL);
	PHY_ERR("HT-RATE", ATH9K_PHYERR_HT_RATE_ILLEGAL);

	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "RX-Pkts-All",
			sc->debug.stats.rxstats.rx_pkts_all);
	len += snprintf(buf + len, size - len,
			"%18s : %10u\n", "RX-Bytes-All",
			sc->debug.stats.rxstats.rx_bytes_all);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;

#undef PHY_ERR
}

void ath_debug_stat_rx(struct ath_softc *sc, struct ath_rx_status *rs)
{
#define RX_STAT_INC(c) sc->debug.stats.rxstats.c++
#define RX_PHY_ERR_INC(c) sc->debug.stats.rxstats.phy_err_stats[c]++

	u32 phyerr;

	RX_STAT_INC(rx_pkts_all);
	sc->debug.stats.rxstats.rx_bytes_all += rs->rs_datalen;

	if (rs->rs_status & ATH9K_RXERR_CRC)
		RX_STAT_INC(crc_err);
	if (rs->rs_status & ATH9K_RXERR_DECRYPT)
		RX_STAT_INC(decrypt_crc_err);
	if (rs->rs_status & ATH9K_RXERR_MIC)
		RX_STAT_INC(mic_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_PRE)
		RX_STAT_INC(pre_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DELIM_CRC_POST)
		RX_STAT_INC(post_delim_crc_err);
	if (rs->rs_status & ATH9K_RX_DECRYPT_BUSY)
		RX_STAT_INC(decrypt_busy_err);

	if (rs->rs_status & ATH9K_RXERR_PHY) {
		RX_STAT_INC(phy_err);
		phyerr = rs->rs_phyerr & 0x24;
		RX_PHY_ERR_INC(phyerr);
	}

#undef RX_STAT_INC
#undef RX_PHY_ERR_INC
}

static const struct file_operations fops_recv = {
	.read = read_file_recv,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_regidx(struct file *file, char __user *user_buf,
                                size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "0x%08x\n", sc->debug.regidx);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_regidx(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long regidx;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &regidx))
		return -EINVAL;

	sc->debug.regidx = regidx;
	return count;
}

static const struct file_operations fops_regidx = {
	.read = read_file_regidx,
	.write = write_file_regidx,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_regval(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	char buf[32];
	unsigned int len;
	u32 regval;

	regval = REG_READ_D(ah, sc->debug.regidx);
	len = sprintf(buf, "0x%08x\n", regval);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_regval(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	unsigned long regval;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &regval))
		return -EINVAL;

	REG_WRITE_D(ah, sc->debug.regidx, regval);
	return count;
}

static const struct file_operations fops_regval = {
	.read = read_file_regval,
	.write = write_file_regval,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath9k_init_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	sc->debug.debugfs_phy = debugfs_create_dir("ath9k",
						   sc->hw->wiphy->debugfsdir);
	if (!sc->debug.debugfs_phy)
		return -ENOMEM;

#ifdef CONFIG_ATH_DEBUG
	if (!debugfs_create_file("debug", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_debug))
		goto err;
#endif

	if (!debugfs_create_file("dma", S_IRUSR, sc->debug.debugfs_phy,
			sc, &fops_dma))
		goto err;

	if (!debugfs_create_file("interrupt", S_IRUSR, sc->debug.debugfs_phy,
			sc, &fops_interrupt))
		goto err;

	if (!debugfs_create_file("wiphy", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_wiphy))
		goto err;

	if (!debugfs_create_file("xmit", S_IRUSR, sc->debug.debugfs_phy,
			sc, &fops_xmit))
		goto err;

	if (!debugfs_create_file("recv", S_IRUSR, sc->debug.debugfs_phy,
			sc, &fops_recv))
		goto err;

	if (!debugfs_create_file("rx_chainmask", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_rx_chainmask))
		goto err;

	if (!debugfs_create_file("tx_chainmask", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_tx_chainmask))
		goto err;

	if (!debugfs_create_file("regidx", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_regidx))
		goto err;

	if (!debugfs_create_file("regval", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, sc, &fops_regval))
		goto err;

	if (!debugfs_create_bool("ignore_extcca", S_IRUSR | S_IWUSR,
			sc->debug.debugfs_phy, &ah->config.cwm_ignore_extcca))
		goto err;

	sc->debug.regidx = 0;
	return 0;
err:
	debugfs_remove_recursive(sc->debug.debugfs_phy);
	sc->debug.debugfs_phy = NULL;
	return -ENOMEM;
}
