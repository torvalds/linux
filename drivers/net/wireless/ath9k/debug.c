/*
 * Copyright (c) 2008 Atheros Communications Inc.
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

#include "core.h"
#include "reg.h"
#include "hw.h"

static unsigned int ath9k_debug = DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);

void DPRINTF(struct ath_softc *sc, int dbg_mask, const char *fmt, ...)
{
	if (!sc)
		return;

	if (sc->sc_debug.debug_mask & dbg_mask) {
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
	struct ath_hal *ah = sc->sc_ah;
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
		sc->sc_debug.stats.istats.total++;
	if (status & ATH9K_INT_RX)
		sc->sc_debug.stats.istats.rxok++;
	if (status & ATH9K_INT_RXEOL)
		sc->sc_debug.stats.istats.rxeol++;
	if (status & ATH9K_INT_RXORN)
		sc->sc_debug.stats.istats.rxorn++;
	if (status & ATH9K_INT_TX)
		sc->sc_debug.stats.istats.txok++;
	if (status & ATH9K_INT_TXURN)
		sc->sc_debug.stats.istats.txurn++;
	if (status & ATH9K_INT_MIB)
		sc->sc_debug.stats.istats.mib++;
	if (status & ATH9K_INT_RXPHY)
		sc->sc_debug.stats.istats.rxphyerr++;
	if (status & ATH9K_INT_RXKCM)
		sc->sc_debug.stats.istats.rx_keycache_miss++;
	if (status & ATH9K_INT_SWBA)
		sc->sc_debug.stats.istats.swba++;
	if (status & ATH9K_INT_BMISS)
		sc->sc_debug.stats.istats.bmiss++;
	if (status & ATH9K_INT_BNR)
		sc->sc_debug.stats.istats.bnr++;
	if (status & ATH9K_INT_CST)
		sc->sc_debug.stats.istats.cst++;
	if (status & ATH9K_INT_GTT)
		sc->sc_debug.stats.istats.gtt++;
	if (status & ATH9K_INT_TIM)
		sc->sc_debug.stats.istats.tim++;
	if (status & ATH9K_INT_CABEND)
		sc->sc_debug.stats.istats.cabend++;
	if (status & ATH9K_INT_DTIMSYNC)
		sc->sc_debug.stats.istats.dtimsync++;
	if (status & ATH9K_INT_DTIM)
		sc->sc_debug.stats.istats.dtim++;
}

static ssize_t read_file_interrupt(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char buf[512];
	unsigned int len = 0;

	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RX", sc->sc_debug.stats.istats.rxok);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXEOL", sc->sc_debug.stats.istats.rxeol);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXORN", sc->sc_debug.stats.istats.rxorn);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TX", sc->sc_debug.stats.istats.txok);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TXURN", sc->sc_debug.stats.istats.txurn);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "MIB", sc->sc_debug.stats.istats.mib);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXPHY", sc->sc_debug.stats.istats.rxphyerr);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "RXKCM", sc->sc_debug.stats.istats.rx_keycache_miss);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "SWBA", sc->sc_debug.stats.istats.swba);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "BMISS", sc->sc_debug.stats.istats.bmiss);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "BNR", sc->sc_debug.stats.istats.bnr);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "CST", sc->sc_debug.stats.istats.cst);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "GTT", sc->sc_debug.stats.istats.gtt);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TIM", sc->sc_debug.stats.istats.tim);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "CABEND", sc->sc_debug.stats.istats.cabend);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "DTIMSYNC", sc->sc_debug.stats.istats.dtimsync);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "DTIM", sc->sc_debug.stats.istats.dtim);
	len += snprintf(buf + len, sizeof(buf) - len,
		"%8s: %10u\n", "TOTAL", sc->sc_debug.stats.istats.total);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_interrupt = {
	.read = read_file_interrupt,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

int ath9k_init_debug(struct ath_softc *sc)
{
	sc->sc_debug.debug_mask = ath9k_debug;

	sc->sc_debug.debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!sc->sc_debug.debugfs_root)
		goto err;

	sc->sc_debug.debugfs_phy = debugfs_create_dir(wiphy_name(sc->hw->wiphy),
						      sc->sc_debug.debugfs_root);
	if (!sc->sc_debug.debugfs_phy)
		goto err;

	sc->sc_debug.debugfs_dma = debugfs_create_file("dma", S_IRUGO,
				       sc->sc_debug.debugfs_phy, sc, &fops_dma);
	if (!sc->sc_debug.debugfs_dma)
		goto err;

	sc->sc_debug.debugfs_interrupt = debugfs_create_file("interrupt",
						     S_IRUGO,
						     sc->sc_debug.debugfs_phy,
						     sc, &fops_interrupt);
	if (!sc->sc_debug.debugfs_interrupt)
		goto err;

	return 0;
err:
	ath9k_exit_debug(sc);
	return -ENOMEM;
}

void ath9k_exit_debug(struct ath_softc *sc)
{
	debugfs_remove(sc->sc_debug.debugfs_interrupt);
	debugfs_remove(sc->sc_debug.debugfs_dma);
	debugfs_remove(sc->sc_debug.debugfs_phy);
	debugfs_remove(sc->sc_debug.debugfs_root);
}
