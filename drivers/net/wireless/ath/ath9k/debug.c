/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include <linux/vmalloc.h>
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

static ssize_t ath9k_debugfs_read_buf(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	u8 *buf = file->private_data;
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static int ath9k_debugfs_release_buf(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
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

static ssize_t read_file_disable_ani(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", common->disable_ani);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_disable_ani(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long disable_ani;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &disable_ani))
		return -EINVAL;

	common->disable_ani = !!disable_ani;

	if (disable_ani) {
		sc->sc_flags &= ~SC_OP_ANI_RUN;
		del_timer_sync(&common->ani.timer);
	} else {
		sc->sc_flags |= SC_OP_ANI_RUN;
		ath_start_ani(common);
	}

	return count;
}

static const struct file_operations fops_disable_ani = {
	.read = read_file_disable_ani,
	.write = write_file_disable_ani,
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
	if (status & ATH9K_INT_TSFOOR)
		sc->debug.stats.istats.tsfoor++;
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
		"%8s: %10u\n", "TSFOOR", sc->debug.stats.istats.tsfoor);
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

static const char *channel_type_str(enum nl80211_channel_type t)
{
	switch (t) {
	case NL80211_CHAN_NO_HT:
		return "no ht";
	case NL80211_CHAN_HT20:
		return "ht20";
	case NL80211_CHAN_HT40MINUS:
		return "ht40-";
	case NL80211_CHAN_HT40PLUS:
		return "ht40+";
	default:
		return "???";
	}
}

static ssize_t read_file_wiphy(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ieee80211_channel *chan = sc->hw->conf.channel;
	struct ieee80211_conf *conf = &(sc->hw->conf);
	char buf[512];
	unsigned int len = 0;
	u8 addr[ETH_ALEN];
	u32 tmp;

	len += snprintf(buf + len, sizeof(buf) - len,
			"%s (chan=%d  center-freq: %d MHz  channel-type: %d (%s))\n",
			wiphy_name(sc->hw->wiphy),
			ieee80211_frequency_to_channel(chan->center_freq),
			chan->center_freq,
			conf->channel_type,
			channel_type_str(conf->channel_type));

	ath9k_ps_wakeup(sc);
	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_STA_ID0), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_STA_ID1) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addr: %pM\n", addr);
	put_unaligned_le32(REG_READ_D(sc->sc_ah, AR_BSSMSKL), addr);
	put_unaligned_le16(REG_READ_D(sc->sc_ah, AR_BSSMSKU) & 0xffff, addr + 4);
	len += snprintf(buf + len, sizeof(buf) - len,
			"addrmask: %pM\n", addr);
	tmp = ath9k_hw_getrxfilter(sc->sc_ah);
	ath9k_ps_restore(sc);
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

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_wiphy = {
	.read = read_file_wiphy,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define PR_QNUM(_n) sc->tx.txq_map[_n]->axq_qnum
#define PR(str, elem)							\
	do {								\
		len += snprintf(buf + len, size - len,			\
				"%s%13u%11u%10u%10u\n", str,		\
		sc->debug.stats.txstats[PR_QNUM(WME_AC_BE)].elem, \
		sc->debug.stats.txstats[PR_QNUM(WME_AC_BK)].elem, \
		sc->debug.stats.txstats[PR_QNUM(WME_AC_VI)].elem, \
		sc->debug.stats.txstats[PR_QNUM(WME_AC_VO)].elem); \
		if (len >= size)			  \
			goto done;			  \
} while(0)

#define PRX(str, elem)							\
do {									\
	len += snprintf(buf + len, size - len,				\
			"%s%13u%11u%10u%10u\n", str,			\
			(unsigned int)(sc->tx.txq_map[WME_AC_BE]->elem),	\
			(unsigned int)(sc->tx.txq_map[WME_AC_BK]->elem),	\
			(unsigned int)(sc->tx.txq_map[WME_AC_VI]->elem),	\
			(unsigned int)(sc->tx.txq_map[WME_AC_VO]->elem));	\
	if (len >= size)						\
		goto done;						\
} while(0)

#define PRQLE(str, elem)						\
do {									\
	len += snprintf(buf + len, size - len,				\
			"%s%13i%11i%10i%10i\n", str,			\
			list_empty(&sc->tx.txq_map[WME_AC_BE]->elem),	\
			list_empty(&sc->tx.txq_map[WME_AC_BK]->elem),	\
			list_empty(&sc->tx.txq_map[WME_AC_VI]->elem),	\
			list_empty(&sc->tx.txq_map[WME_AC_VO]->elem));	\
	if (len >= size)						\
		goto done;						\
} while (0)

static ssize_t read_file_xmit(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 8000;
	int i;
	ssize_t retval = 0;
	char tmp[32];

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += sprintf(buf, "Num-Tx-Queues: %i  tx-queues-setup: 0x%x"
		       " poll-work-seen: %u\n"
		       "%30s %10s%10s%10s\n\n",
		       ATH9K_NUM_TX_QUEUES, sc->tx.txqsetup,
		       sc->tx_complete_poll_work_seen,
		       "BE", "BK", "VI", "VO");

	PR("MPDUs Queued:    ", queued);
	PR("MPDUs Completed: ", completed);
	PR("MPDUs XRetried:  ", xretries);
	PR("Aggregates:      ", a_aggr);
	PR("AMPDUs Queued HW:", a_queued_hw);
	PR("AMPDUs Queued SW:", a_queued_sw);
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
	PR("hw-put-tx-buf:   ", puttxbuf);
	PR("hw-tx-start:     ", txstart);
	PR("hw-tx-proc-desc: ", txprocdesc);
	len += snprintf(buf + len, size - len,
			"%s%11p%11p%10p%10p\n", "txq-memory-address:",
			sc->tx.txq_map[WME_AC_BE],
			sc->tx.txq_map[WME_AC_BK],
			sc->tx.txq_map[WME_AC_VI],
			sc->tx.txq_map[WME_AC_VO]);
	if (len >= size)
		goto done;

	PRX("axq-qnum:        ", axq_qnum);
	PRX("axq-depth:       ", axq_depth);
	PRX("axq-ampdu_depth: ", axq_ampdu_depth);
	PRX("axq-stopped      ", stopped);
	PRX("tx-in-progress   ", axq_tx_inprogress);
	PRX("pending-frames   ", pending_frames);
	PRX("txq_headidx:     ", txq_headidx);
	PRX("txq_tailidx:     ", txq_headidx);

	PRQLE("axq_q empty:       ", axq_q);
	PRQLE("axq_acq empty:     ", axq_acq);
	for (i = 0; i < ATH_TXFIFO_DEPTH; i++) {
		snprintf(tmp, sizeof(tmp) - 1, "txq_fifo[%i] empty: ", i);
		PRQLE(tmp, txq_fifo[i]);
	}

	/* Print out more detailed queue-info */
	for (i = 0; i <= WME_AC_BK; i++) {
		struct ath_txq *txq = &(sc->tx.txq[i]);
		struct ath_atx_ac *ac;
		struct ath_atx_tid *tid;
		if (len >= size)
			goto done;
		spin_lock_bh(&txq->axq_lock);
		if (!list_empty(&txq->axq_acq)) {
			ac = list_first_entry(&txq->axq_acq, struct ath_atx_ac,
					      list);
			len += snprintf(buf + len, size - len,
					"txq[%i] first-ac: %p sched: %i\n",
					i, ac, ac->sched);
			if (list_empty(&ac->tid_q) || (len >= size))
				goto done_for;
			tid = list_first_entry(&ac->tid_q, struct ath_atx_tid,
					       list);
			len += snprintf(buf + len, size - len,
					" first-tid: %p sched: %i paused: %i\n",
					tid, tid->sched, tid->paused);
		}
	done_for:
		spin_unlock_bh(&txq->axq_lock);
	}

done:
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static ssize_t read_file_stations(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *buf;
	unsigned int len = 0, size = 64000;
	struct ath_node *an = NULL;
	ssize_t retval = 0;
	int q;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += snprintf(buf + len, size - len,
			"Stations:\n"
			" tid: addr sched paused buf_q-empty an ac\n"
			" ac: addr sched tid_q-empty txq\n");

	spin_lock(&sc->nodes_lock);
	list_for_each_entry(an, &sc->nodes, list) {
		len += snprintf(buf + len, size - len,
				"%pM\n", an->sta->addr);
		if (len >= size)
			goto done;

		for (q = 0; q < WME_NUM_TID; q++) {
			struct ath_atx_tid *tid = &(an->tid[q]);
			len += snprintf(buf + len, size - len,
					" tid: %p %s %s %i %p %p\n",
					tid, tid->sched ? "sched" : "idle",
					tid->paused ? "paused" : "running",
					list_empty(&tid->buf_q),
					tid->an, tid->ac);
			if (len >= size)
				goto done;
		}

		for (q = 0; q < WME_NUM_AC; q++) {
			struct ath_atx_ac *ac = &(an->ac[q]);
			len += snprintf(buf + len, size - len,
					" ac: %p %s %i %p\n",
					ac, ac->sched ? "sched" : "idle",
					list_empty(&ac->tid_q), ac->txq);
			if (len >= size)
				goto done;
		}
	}

done:
	spin_unlock(&sc->nodes_lock);
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static ssize_t read_file_misc(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_hw *hw = sc->hw;
	char *buf;
	unsigned int len = 0, size = 8000;
	ssize_t retval = 0;
	unsigned int reg;
	struct ath9k_vif_iter_data iter_data;

	ath9k_calculate_iter_data(hw, NULL, &iter_data);
	
	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	ath9k_ps_wakeup(sc);
	len += snprintf(buf + len, size - len,
			"curbssid: %pM\n"
			"OP-Mode: %s(%i)\n"
			"Beacon-Timer-Register: 0x%x\n",
			common->curbssid,
			ath_opmode_to_string(sc->sc_ah->opmode),
			(int)(sc->sc_ah->opmode),
			REG_READ(ah, AR_BEACON_PERIOD));

	reg = REG_READ(ah, AR_TIMER_MODE);
	ath9k_ps_restore(sc);
	len += snprintf(buf + len, size - len, "Timer-Mode-Register: 0x%x (",
			reg);
	if (reg & AR_TBTT_TIMER_EN)
		len += snprintf(buf + len, size - len, "TBTT ");
	if (reg & AR_DBA_TIMER_EN)
		len += snprintf(buf + len, size - len, "DBA ");
	if (reg & AR_SWBA_TIMER_EN)
		len += snprintf(buf + len, size - len, "SWBA ");
	if (reg & AR_HCF_TIMER_EN)
		len += snprintf(buf + len, size - len, "HCF ");
	if (reg & AR_TIM_TIMER_EN)
		len += snprintf(buf + len, size - len, "TIM ");
	if (reg & AR_DTIM_TIMER_EN)
		len += snprintf(buf + len, size - len, "DTIM ");
	len += snprintf(buf + len, size - len, ")\n");

	reg = sc->sc_ah->imask;
	len += snprintf(buf + len, size - len, "imask: 0x%x (", reg);
	if (reg & ATH9K_INT_SWBA)
		len += snprintf(buf + len, size - len, "SWBA ");
	if (reg & ATH9K_INT_BMISS)
		len += snprintf(buf + len, size - len, "BMISS ");
	if (reg & ATH9K_INT_CST)
		len += snprintf(buf + len, size - len, "CST ");
	if (reg & ATH9K_INT_RX)
		len += snprintf(buf + len, size - len, "RX ");
	if (reg & ATH9K_INT_RXHP)
		len += snprintf(buf + len, size - len, "RXHP ");
	if (reg & ATH9K_INT_RXLP)
		len += snprintf(buf + len, size - len, "RXLP ");
	if (reg & ATH9K_INT_BB_WATCHDOG)
		len += snprintf(buf + len, size - len, "BB_WATCHDOG ");
	/* there are other IRQs if one wanted to add them. */
	len += snprintf(buf + len, size - len, ")\n");

	len += snprintf(buf + len, size - len,
			"VIF Counts: AP: %i STA: %i MESH: %i WDS: %i"
			" ADHOC: %i OTHER: %i nvifs: %hi beacon-vifs: %hi\n",
			iter_data.naps, iter_data.nstations, iter_data.nmeshes,
			iter_data.nwds, iter_data.nadhocs, iter_data.nothers,
			sc->nvifs, sc->nbcnvifs);

	len += snprintf(buf + len, size - len,
			"Calculated-BSSID-Mask: %pM\n",
			iter_data.mask);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts, struct ath_txq *txq)
{
	int qnum = txq->axq_qnum;

	TX_STAT_INC(qnum, tx_pkts_all);
	sc->debug.stats.txstats[qnum].tx_bytes_all += bf->bf_mpdu->len;

	if (bf_isampdu(bf)) {
		if (bf_isxretried(bf))
			TX_STAT_INC(qnum, a_xretries);
		else
			TX_STAT_INC(qnum, a_completed);
	} else {
		if (bf_isxretried(bf))
			TX_STAT_INC(qnum, xretries);
		else
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

static const struct file_operations fops_stations = {
	.read = read_file_stations,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_misc = {
	.read = read_file_misc,
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
	unsigned int len = 0, size = 1400;
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

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-CTL0",
			sc->debug.stats.rxstats.rs_rssi_ctl0);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-CTL1",
			sc->debug.stats.rxstats.rs_rssi_ctl1);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-CTL2",
			sc->debug.stats.rxstats.rs_rssi_ctl2);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-EXT0",
			sc->debug.stats.rxstats.rs_rssi_ext0);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-EXT1",
			sc->debug.stats.rxstats.rs_rssi_ext1);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "RSSI-EXT2",
			sc->debug.stats.rxstats.rs_rssi_ext2);

	len += snprintf(buf + len, size - len,
			"%18s : %10d\n", "Rx Antenna",
			sc->debug.stats.rxstats.rs_antenna);

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

	sc->debug.stats.rxstats.rs_rssi_ctl0 = rs->rs_rssi_ctl0;
	sc->debug.stats.rxstats.rs_rssi_ctl1 = rs->rs_rssi_ctl1;
	sc->debug.stats.rxstats.rs_rssi_ctl2 = rs->rs_rssi_ctl2;

	sc->debug.stats.rxstats.rs_rssi_ext0 = rs->rs_rssi_ext0;
	sc->debug.stats.rxstats.rs_rssi_ext1 = rs->rs_rssi_ext1;
	sc->debug.stats.rxstats.rs_rssi_ext2 = rs->rs_rssi_ext2;

	sc->debug.stats.rxstats.rs_antenna = rs->rs_antenna;

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

	ath9k_ps_wakeup(sc);
	regval = REG_READ_D(ah, sc->debug.regidx);
	ath9k_ps_restore(sc);
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

	ath9k_ps_wakeup(sc);
	REG_WRITE_D(ah, sc->debug.regidx, regval);
	ath9k_ps_restore(sc);
	return count;
}

static const struct file_operations fops_regval = {
	.read = read_file_regval,
	.write = write_file_regval,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define REGDUMP_LINE_SIZE	20

static int open_file_regdump(struct inode *inode, struct file *file)
{
	struct ath_softc *sc = inode->i_private;
	unsigned int len = 0;
	u8 *buf;
	int i;
	unsigned long num_regs, regdump_len, max_reg_offset;

	max_reg_offset = AR_SREV_9300_20_OR_LATER(sc->sc_ah) ? 0x16bd4 : 0xb500;
	num_regs = max_reg_offset / 4 + 1;
	regdump_len = num_regs * REGDUMP_LINE_SIZE + 1;
	buf = vmalloc(regdump_len);
	if (!buf)
		return -ENOMEM;

	ath9k_ps_wakeup(sc);
	for (i = 0; i < num_regs; i++)
		len += scnprintf(buf + len, regdump_len - len,
			"0x%06x 0x%08x\n", i << 2, REG_READ(sc->sc_ah, i << 2));
	ath9k_ps_restore(sc);

	file->private_data = buf;

	return 0;
}

static const struct file_operations fops_regdump = {
	.open = open_file_regdump,
	.read = ath9k_debugfs_read_buf,
	.release = ath9k_debugfs_release_buf,
	.owner = THIS_MODULE,
	.llseek = default_llseek,/* read accesses f_pos */
};

static ssize_t read_file_base_eeprom(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	u32 len = 0, size = 1500;
	ssize_t retval = 0;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, true, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_base_eeprom = {
	.read = read_file_base_eeprom,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_modal_eeprom(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_hw *ah = sc->sc_ah;
	u32 len = 0, size = 6000;
	char *buf;
	size_t retval;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = ah->eep_ops->dump_eeprom(ah, false, buf, len, size);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_modal_eeprom = {
	.read = read_file_modal_eeprom,
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
	debugfs_create_file("debug", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_debug);
#endif
	debugfs_create_file("dma", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_dma);
	debugfs_create_file("interrupt", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_interrupt);
	debugfs_create_file("wiphy", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_wiphy);
	debugfs_create_file("xmit", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_xmit);
	debugfs_create_file("stations", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_stations);
	debugfs_create_file("misc", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_misc);
	debugfs_create_file("recv", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_recv);
	debugfs_create_file("rx_chainmask", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_rx_chainmask);
	debugfs_create_file("tx_chainmask", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_tx_chainmask);
	debugfs_create_file("disable_ani", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc, &fops_disable_ani);
	debugfs_create_file("regidx", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_regidx);
	debugfs_create_file("regval", S_IRUSR | S_IWUSR, sc->debug.debugfs_phy,
			    sc, &fops_regval);
	debugfs_create_bool("ignore_extcca", S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy,
			    &ah->config.cwm_ignore_extcca);
	debugfs_create_file("regdump", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_regdump);
	debugfs_create_file("base_eeprom", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_base_eeprom);
	debugfs_create_file("modal_eeprom", S_IRUSR, sc->debug.debugfs_phy, sc,
			    &fops_modal_eeprom);

	debugfs_create_u32("gpio_mask", S_IRUSR | S_IWUSR,
			   sc->debug.debugfs_phy, &sc->sc_ah->gpio_mask);

	debugfs_create_u32("gpio_val", S_IRUSR | S_IWUSR,
			   sc->debug.debugfs_phy, &sc->sc_ah->gpio_val);

	sc->debug.regidx = 0;
	return 0;
}
