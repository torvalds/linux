// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/mmc/host/sdhci_uhs2.c - Secure Digital Host Controller
 *  Interface driver
 *
 *  Copyright (C) 2014 Intel Corp, All Rights Reserved.
 *  Copyright (C) 2020 Genesys Logic, Inc.
 *  Authors: Ben Chuang <ben.chuang@genesyslogic.com.tw>
 *  Copyright (C) 2020 Linaro Limited
 *  Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/bitfield.h>
#include <linux/regulator/consumer.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>

#include "sdhci.h"
#include "sdhci-uhs2.h"

#define DRIVER_NAME "sdhci_uhs2"
#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__, ## x)
#define SDHCI_UHS2_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

#define UHS2_RESET_TIMEOUT_100MS		100000
#define UHS2_CHECK_DORMANT_TIMEOUT_100MS	100000
#define UHS2_INTERFACE_DETECT_TIMEOUT_100MS	100000
#define UHS2_LANE_SYNC_TIMEOUT_150MS		150000

#define UHS2_ARG_IOADR_MASK 0xfff

void sdhci_uhs2_dump_regs(struct sdhci_host *host)
{
	if (!(mmc_card_uhs2(host->mmc)))
		return;

	SDHCI_UHS2_DUMP("==================== UHS2 ==================\n");
	SDHCI_UHS2_DUMP("Blk Size:  0x%08x | Blk Cnt:  0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_BLOCK_SIZE),
			sdhci_readl(host, SDHCI_UHS2_BLOCK_COUNT));
	SDHCI_UHS2_DUMP("Cmd:       0x%08x | Trn mode: 0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_CMD),
			sdhci_readw(host, SDHCI_UHS2_TRANS_MODE));
	SDHCI_UHS2_DUMP("Int Stat:  0x%08x | Dev Sel : 0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_DEV_INT_STATUS),
			sdhci_readb(host, SDHCI_UHS2_DEV_SELECT));
	SDHCI_UHS2_DUMP("Dev Int Code:  0x%08x\n",
			sdhci_readb(host, SDHCI_UHS2_DEV_INT_CODE));
	SDHCI_UHS2_DUMP("Reset:     0x%08x | Timer:    0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_SW_RESET),
			sdhci_readw(host, SDHCI_UHS2_TIMER_CTRL));
	SDHCI_UHS2_DUMP("ErrInt:    0x%08x | ErrIntEn: 0x%08x\n",
			sdhci_readl(host, SDHCI_UHS2_INT_STATUS),
			sdhci_readl(host, SDHCI_UHS2_INT_STATUS_ENABLE));
	SDHCI_UHS2_DUMP("ErrSigEn:  0x%08x\n",
			sdhci_readl(host, SDHCI_UHS2_INT_SIGNAL_ENABLE));
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_dump_regs);

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static inline u16 uhs2_dev_cmd(struct mmc_command *cmd)
{
	return be16_to_cpu((__force __be16)cmd->uhs2_cmd->arg) & UHS2_ARG_IOADR_MASK;
}

static inline int mmc_opt_regulator_set_ocr(struct mmc_host *mmc,
					    struct regulator *supply,
					    unsigned short vdd_bit)
{
	return IS_ERR_OR_NULL(supply) ? 0 : mmc_regulator_set_ocr(mmc, supply, vdd_bit);
}

/**
 * sdhci_uhs2_reset - invoke SW reset
 * @host: SDHCI host
 * @mask: Control mask
 *
 * Invoke SW reset, depending on a bit in @mask and wait for completion.
 */
void sdhci_uhs2_reset(struct sdhci_host *host, u16 mask)
{
	u32 val;

	sdhci_writew(host, mask, SDHCI_UHS2_SW_RESET);

	if (mask & SDHCI_UHS2_SW_RESET_FULL)
		host->clock = 0;

	/* hw clears the bit when it's done */
	if (read_poll_timeout_atomic(sdhci_readw, val, !(val & mask), 10,
				     UHS2_RESET_TIMEOUT_100MS, true, host, SDHCI_UHS2_SW_RESET)) {
		pr_debug("%s: %s: Reset 0x%x never completed. %s: clean reset bit.\n", __func__,
			 mmc_hostname(host->mmc), (int)mask, mmc_hostname(host->mmc));
		sdhci_writeb(host, 0, SDHCI_UHS2_SW_RESET);
		return;
	}
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_reset);

static void sdhci_uhs2_reset_cmd_data(struct sdhci_host *host)
{
	sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	if (host->mmc->uhs2_sd_tran) {
		sdhci_uhs2_reset(host, SDHCI_UHS2_SW_RESET_SD);

		sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
		sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
		sdhci_uhs2_clear_set_irqs(host, SDHCI_INT_ALL_MASK, SDHCI_UHS2_INT_ERROR_MASK);
	}
}

void sdhci_uhs2_set_power(struct sdhci_host *host, unsigned char mode, unsigned short vdd)
{
	struct mmc_host *mmc = host->mmc;
	u8 pwr = 0;

	if (mode != MMC_POWER_OFF) {
		pwr = sdhci_get_vdd_value(vdd);
		if (!pwr)
			WARN(1, "%s: Invalid vdd %#x\n",
			     mmc_hostname(host->mmc), vdd);
		pwr |= SDHCI_VDD2_POWER_180;
	}

	if (host->pwr == pwr)
		return;
	host->pwr = pwr;

	if (pwr == 0) {
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);

		mmc_opt_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		mmc_regulator_set_vqmmc2(mmc, &mmc->ios);
	} else {
		mmc_opt_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
		/* support 1.8v only for now */
		mmc_regulator_set_vqmmc2(mmc, &mmc->ios);

		/* Clear the power reg before setting a new value */
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);

		/* vdd first */
		pwr |= SDHCI_POWER_ON;
		sdhci_writeb(host, pwr & 0xf, SDHCI_POWER_CONTROL);
		mdelay(5);

		pwr |= SDHCI_VDD2_POWER_ON;
		sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);
		mdelay(5);
	}
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_set_power);

static u8 sdhci_calc_timeout_uhs2(struct sdhci_host *host, u8 *cmd_res, u8 *dead_lock)
{
	/* timeout in us */
	unsigned int dead_lock_timeout = 1 * 1000 * 1000;
	unsigned int cmd_res_timeout = 5 * 1000;
	unsigned int current_timeout;
	u8 count;

	/*
	 * Figure out needed cycles.
	 * We do this in steps in order to fit inside a 32 bit int.
	 * The first step is the minimum timeout, which will have a
	 * minimum resolution of 6 bits:
	 * (1) 2^13*1000 > 2^22,
	 * (2) host->timeout_clk < 2^16
	 *     =>
	 *     (1) / (2) > 2^6
	 */
	count = 0;
	current_timeout = (1 << 13) * 1000 / host->timeout_clk;
	while (current_timeout < cmd_res_timeout) {
		count++;
		current_timeout <<= 1;
		if (count >= 0xF)
			break;
	}

	if (count >= 0xF) {
		DBG("%s: Too large timeout 0x%x requested for CMD_RES!\n",
		    mmc_hostname(host->mmc), count);
		count = 0xE;
	}
	*cmd_res = count;

	count = 0;
	current_timeout = (1 << 13) * 1000 / host->timeout_clk;
	while (current_timeout < dead_lock_timeout) {
		count++;
		current_timeout <<= 1;
		if (count >= 0xF)
			break;
	}

	if (count >= 0xF) {
		DBG("%s: Too large timeout 0x%x requested for DEADLOCK!\n",
		    mmc_hostname(host->mmc), count);
		count = 0xE;
	}
	*dead_lock = count;

	return count;
}

static void __sdhci_uhs2_set_timeout(struct sdhci_host *host)
{
	u8 cmd_res, dead_lock;

	sdhci_calc_timeout_uhs2(host, &cmd_res, &dead_lock);
	cmd_res |= FIELD_PREP(SDHCI_UHS2_TIMER_CTRL_DEADLOCK_MASK, dead_lock);
	sdhci_writeb(host, cmd_res, SDHCI_UHS2_TIMER_CTRL);
}

void sdhci_uhs2_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	__sdhci_set_timeout(host, cmd);

	if (mmc_card_uhs2(host->mmc))
		__sdhci_uhs2_set_timeout(host);
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_set_timeout);

/**
 * sdhci_uhs2_clear_set_irqs - set Error Interrupt Status Enable register
 * @host:	SDHCI host
 * @clear:	bit-wise clear mask
 * @set:	bit-wise set mask
 *
 * Set/unset bits in UHS-II Error Interrupt Status Enable register
 */
void sdhci_uhs2_clear_set_irqs(struct sdhci_host *host, u32 clear, u32 set)
{
	u32 ier;

	ier = sdhci_readl(host, SDHCI_UHS2_INT_STATUS_ENABLE);
	ier &= ~clear;
	ier |= set;
	sdhci_writel(host, ier, SDHCI_UHS2_INT_STATUS_ENABLE);
	sdhci_writel(host, ier, SDHCI_UHS2_INT_SIGNAL_ENABLE);
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_clear_set_irqs);

static void __sdhci_uhs2_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u8 cmd_res, dead_lock;
	u16 ctrl_2;

	/* UHS2 Timeout Control */
	sdhci_calc_timeout_uhs2(host, &cmd_res, &dead_lock);

	/* change to use calculate value */
	cmd_res |= FIELD_PREP(SDHCI_UHS2_TIMER_CTRL_DEADLOCK_MASK, dead_lock);

	sdhci_uhs2_clear_set_irqs(host,
				  SDHCI_UHS2_INT_CMD_TIMEOUT |
				  SDHCI_UHS2_INT_DEADLOCK_TIMEOUT,
				  0);
	sdhci_writeb(host, cmd_res, SDHCI_UHS2_TIMER_CTRL);
	sdhci_uhs2_clear_set_irqs(host, 0,
				  SDHCI_UHS2_INT_CMD_TIMEOUT |
				  SDHCI_UHS2_INT_DEADLOCK_TIMEOUT);

	/* UHS2 timing. Note, UHS2 timing is disabled when powering off */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	if (ios->power_mode != MMC_POWER_OFF &&
	    (ios->timing == MMC_TIMING_UHS2_SPEED_A ||
	     ios->timing == MMC_TIMING_UHS2_SPEED_A_HD ||
	     ios->timing == MMC_TIMING_UHS2_SPEED_B ||
	     ios->timing == MMC_TIMING_UHS2_SPEED_B_HD))
		ctrl_2 |= SDHCI_CTRL_UHS2 | SDHCI_CTRL_UHS2_ENABLE;
	else
		ctrl_2 &= ~(SDHCI_CTRL_UHS2 | SDHCI_CTRL_UHS2_ENABLE);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	host->timing = ios->timing;

	if (!(host->quirks2 & SDHCI_QUIRK2_PRESET_VALUE_BROKEN))
		sdhci_enable_preset_value(host, true);

	if (host->ops->set_power)
		host->ops->set_power(host, ios->power_mode, ios->vdd);
	else
		sdhci_uhs2_set_power(host, ios->power_mode, ios->vdd);

	host->ops->set_clock(host, ios->clock);
	host->clock = ios->clock;
}

static int sdhci_uhs2_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	pr_debug("%s: clock %uHz powermode %u Vdd %u timing %u\n",
		 mmc_hostname(mmc), ios->clock, ios->power_mode, ios->vdd, ios->timing);

	if (!mmc_card_uhs2(mmc)) {
		sdhci_set_ios(mmc, ios);
		return 0;
	}

	if (ios->power_mode == MMC_POWER_UNDEFINED)
		return 0;

	if (host->flags & SDHCI_DEVICE_DEAD) {
		if (ios->power_mode == MMC_POWER_OFF) {
			mmc_opt_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
			mmc_regulator_set_vqmmc2(mmc, ios);
		}
		return -1;
	}

	sdhci_set_ios_common(mmc, ios);

	__sdhci_uhs2_set_ios(mmc, ios);

	return 0;
}

static int sdhci_uhs2_interface_detect(struct sdhci_host *host)
{
	u32 val;

	if (read_poll_timeout(sdhci_readl, val, (val & SDHCI_UHS2_IF_DETECT),
			      100, UHS2_INTERFACE_DETECT_TIMEOUT_100MS, true,
			      host, SDHCI_PRESENT_STATE)) {
		pr_debug("%s: not detect UHS2 interface in 100ms.\n", mmc_hostname(host->mmc));
		sdhci_dbg_dumpregs(host, "UHS2 interface detect timeout in 100ms");
		return -EIO;
	}

	/* Enable UHS2 error interrupts */
	sdhci_uhs2_clear_set_irqs(host, SDHCI_INT_ALL_MASK, SDHCI_UHS2_INT_ERROR_MASK);

	if (read_poll_timeout(sdhci_readl, val, (val & SDHCI_UHS2_LANE_SYNC),
			      100, UHS2_LANE_SYNC_TIMEOUT_150MS, true, host, SDHCI_PRESENT_STATE)) {
		pr_debug("%s: UHS2 Lane sync fail in 150ms.\n", mmc_hostname(host->mmc));
		sdhci_dbg_dumpregs(host, "UHS2 Lane sync fail in 150ms");
		return -EIO;
	}

	DBG("%s: UHS2 Lane synchronized in UHS2 mode, PHY is initialized.\n",
	    mmc_hostname(host->mmc));
	return 0;
}

static int sdhci_uhs2_init(struct sdhci_host *host)
{
	u16 caps_ptr = 0;
	u32 caps_gen = 0;
	u32 caps_phy = 0;
	u32 caps_tran[2] = {0, 0};
	struct mmc_host *mmc = host->mmc;

	caps_ptr = sdhci_readw(host, SDHCI_UHS2_CAPS_PTR);
	if (caps_ptr < 0x100 || caps_ptr > 0x1FF) {
		pr_err("%s: SDHCI_UHS2_CAPS_PTR(%d) is wrong.\n",
		       mmc_hostname(mmc), caps_ptr);
		return -ENODEV;
	}
	caps_gen = sdhci_readl(host, caps_ptr + SDHCI_UHS2_CAPS_OFFSET);
	caps_phy = sdhci_readl(host, caps_ptr + SDHCI_UHS2_CAPS_PHY_OFFSET);
	caps_tran[0] = sdhci_readl(host, caps_ptr + SDHCI_UHS2_CAPS_TRAN_OFFSET);
	caps_tran[1] = sdhci_readl(host, caps_ptr + SDHCI_UHS2_CAPS_TRAN_1_OFFSET);

	/* General Caps */
	mmc->uhs2_caps.dap = caps_gen & SDHCI_UHS2_CAPS_DAP_MASK;
	mmc->uhs2_caps.gap = FIELD_GET(SDHCI_UHS2_CAPS_GAP_MASK, caps_gen);
	mmc->uhs2_caps.n_lanes = FIELD_GET(SDHCI_UHS2_CAPS_LANE_MASK, caps_gen);
	mmc->uhs2_caps.addr64 =	(caps_gen & SDHCI_UHS2_CAPS_ADDR_64) ? 1 : 0;
	mmc->uhs2_caps.card_type = FIELD_GET(SDHCI_UHS2_CAPS_DEV_TYPE_MASK, caps_gen);

	/* PHY Caps */
	mmc->uhs2_caps.phy_rev = caps_phy & SDHCI_UHS2_CAPS_PHY_REV_MASK;
	mmc->uhs2_caps.speed_range = FIELD_GET(SDHCI_UHS2_CAPS_PHY_RANGE_MASK, caps_phy);
	mmc->uhs2_caps.n_lss_sync = FIELD_GET(SDHCI_UHS2_CAPS_PHY_N_LSS_SYN_MASK, caps_phy);
	mmc->uhs2_caps.n_lss_dir = FIELD_GET(SDHCI_UHS2_CAPS_PHY_N_LSS_DIR_MASK, caps_phy);
	if (mmc->uhs2_caps.n_lss_sync == 0)
		mmc->uhs2_caps.n_lss_sync = 16 << 2;
	else
		mmc->uhs2_caps.n_lss_sync <<= 2;
	if (mmc->uhs2_caps.n_lss_dir == 0)
		mmc->uhs2_caps.n_lss_dir = 16 << 3;
	else
		mmc->uhs2_caps.n_lss_dir <<= 3;

	/* LINK/TRAN Caps */
	mmc->uhs2_caps.link_rev = caps_tran[0] & SDHCI_UHS2_CAPS_TRAN_LINK_REV_MASK;
	mmc->uhs2_caps.n_fcu = FIELD_GET(SDHCI_UHS2_CAPS_TRAN_N_FCU_MASK, caps_tran[0]);
	if (mmc->uhs2_caps.n_fcu == 0)
		mmc->uhs2_caps.n_fcu = 256;
	mmc->uhs2_caps.host_type = FIELD_GET(SDHCI_UHS2_CAPS_TRAN_HOST_TYPE_MASK, caps_tran[0]);
	mmc->uhs2_caps.maxblk_len = FIELD_GET(SDHCI_UHS2_CAPS_TRAN_BLK_LEN_MASK, caps_tran[0]);
	mmc->uhs2_caps.n_data_gap = caps_tran[1] & SDHCI_UHS2_CAPS_TRAN_1_N_DATA_GAP_MASK;

	return 0;
}

static int sdhci_uhs2_do_detect_init(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	DBG("Begin do uhs2 detect init.\n");

	if (host->ops->uhs2_pre_detect_init)
		host->ops->uhs2_pre_detect_init(host);

	if (sdhci_uhs2_interface_detect(host)) {
		pr_debug("%s: cannot detect UHS2 interface.\n", mmc_hostname(host->mmc));
		return -EIO;
	}

	if (sdhci_uhs2_init(host)) {
		pr_debug("%s: UHS2 init fail.\n", mmc_hostname(host->mmc));
		return -EIO;
	}

	/* Init complete, do soft reset and enable UHS2 error irqs. */
	sdhci_uhs2_reset(host, SDHCI_UHS2_SW_RESET_SD);
	sdhci_uhs2_clear_set_irqs(host, SDHCI_INT_ALL_MASK, SDHCI_UHS2_INT_ERROR_MASK);
	/*
	 * N.B SDHCI_INT_ENABLE and SDHCI_SIGNAL_ENABLE was cleared
	 * by SDHCI_UHS2_SW_RESET_SD
	 */
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

	return 0;
}

static int sdhci_uhs2_disable_clk(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	return 0;
}

static int sdhci_uhs2_enable_clk(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	int timeout_us = 20000; /* 20ms */
	u32 val;

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	if (read_poll_timeout(sdhci_readw, val, (val & SDHCI_CLOCK_INT_STABLE),
			      10, timeout_us, true, host, SDHCI_CLOCK_CONTROL)) {
		pr_err("%s: Internal clock never stabilised.\n", mmc_hostname(host->mmc));
		sdhci_dumpregs(host);
		return -EIO;
	}
	return 0;
}

static void sdhci_uhs2_set_config(struct sdhci_host *host)
{
	u32 value;
	u16 sdhci_uhs2_set_ptr = sdhci_readw(host, SDHCI_UHS2_SETTINGS_PTR);
	u16 sdhci_uhs2_gen_set_reg	= sdhci_uhs2_set_ptr;
	u16 sdhci_uhs2_phy_set_reg	= sdhci_uhs2_set_ptr + 4;
	u16 sdhci_uhs2_tran_set_reg	= sdhci_uhs2_set_ptr + 8;
	u16 sdhci_uhs2_tran_set_1_reg	= sdhci_uhs2_set_ptr + 12;

	/* Set Gen Settings */
	value = FIELD_PREP(SDHCI_UHS2_GEN_SETTINGS_N_LANES_MASK, host->mmc->uhs2_caps.n_lanes_set);
	sdhci_writel(host, value, sdhci_uhs2_gen_set_reg);

	/* Set PHY Settings */
	value = FIELD_PREP(SDHCI_UHS2_PHY_N_LSS_DIR_MASK, host->mmc->uhs2_caps.n_lss_dir_set) |
		FIELD_PREP(SDHCI_UHS2_PHY_N_LSS_SYN_MASK, host->mmc->uhs2_caps.n_lss_sync_set);
	if (host->mmc->ios.timing == MMC_TIMING_UHS2_SPEED_B ||
	    host->mmc->ios.timing == MMC_TIMING_UHS2_SPEED_B_HD)
		value |= SDHCI_UHS2_PHY_SET_SPEED_B;
	sdhci_writel(host, value, sdhci_uhs2_phy_set_reg);

	/* Set LINK-TRAN Settings */
	value = FIELD_PREP(SDHCI_UHS2_TRAN_RETRY_CNT_MASK, host->mmc->uhs2_caps.max_retry_set) |
		FIELD_PREP(SDHCI_UHS2_TRAN_N_FCU_MASK, host->mmc->uhs2_caps.n_fcu_set);
	sdhci_writel(host, value, sdhci_uhs2_tran_set_reg);
	sdhci_writel(host, host->mmc->uhs2_caps.n_data_gap_set, sdhci_uhs2_tran_set_1_reg);
}

static int sdhci_uhs2_check_dormant(struct sdhci_host *host)
{
	u32 val;

	if (read_poll_timeout(sdhci_readl, val, (val & SDHCI_UHS2_IN_DORMANT_STATE),
			      100, UHS2_CHECK_DORMANT_TIMEOUT_100MS, true, host,
			      SDHCI_PRESENT_STATE)) {
		pr_debug("%s: UHS2 IN_DORMANT fail in 100ms.\n", mmc_hostname(host->mmc));
		sdhci_dbg_dumpregs(host, "UHS2 IN_DORMANT fail in 100ms");
		return -EIO;
	}
	return 0;
}

static int sdhci_uhs2_control(struct mmc_host *mmc, enum sd_uhs2_operation op)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_ios *ios = &mmc->ios;
	int err = 0;

	DBG("Begin uhs2 control, act %d.\n", op);

	switch (op) {
	case UHS2_PHY_INIT:
		err = sdhci_uhs2_do_detect_init(mmc);
		break;
	case UHS2_SET_CONFIG:
		sdhci_uhs2_set_config(host);
		break;
	case UHS2_ENABLE_INT:
		sdhci_uhs2_clear_set_irqs(host, 0, SDHCI_INT_CARD_INT);
		break;
	case UHS2_DISABLE_INT:
		sdhci_uhs2_clear_set_irqs(host, SDHCI_INT_CARD_INT, 0);
		break;
	case UHS2_CHECK_DORMANT:
		err = sdhci_uhs2_check_dormant(host);
		break;
	case UHS2_DISABLE_CLK:
		err = sdhci_uhs2_disable_clk(mmc);
		break;
	case UHS2_ENABLE_CLK:
		err = sdhci_uhs2_enable_clk(mmc);
		break;
	case UHS2_SET_IOS:
		err = sdhci_uhs2_set_ios(mmc, ios);
		break;
	default:
		pr_err("%s: input sd uhs2 operation %d is wrong!\n",
		       mmc_hostname(host->mmc), op);
		err = -EIO;
		break;
	}

	return err;
}

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static void sdhci_uhs2_prepare_data(struct sdhci_host *host, struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;

	sdhci_initialize_data(host, data);

	sdhci_prepare_dma(host, data);

	sdhci_writew(host, data->blksz, SDHCI_UHS2_BLOCK_SIZE);
	sdhci_writew(host, data->blocks, SDHCI_UHS2_BLOCK_COUNT);
}

static void sdhci_uhs2_finish_data(struct sdhci_host *host)
{
	struct mmc_data *data = host->data;

	__sdhci_finish_data_common(host, true);

	__sdhci_finish_mrq(host, data->mrq);
}

static void sdhci_uhs2_set_transfer_mode(struct sdhci_host *host, struct mmc_command *cmd)
{
	u16 mode;
	struct mmc_data *data = cmd->data;

	if (!data) {
		/* clear Auto CMD settings for no data CMDs */
		if (uhs2_dev_cmd(cmd) == UHS2_DEV_CMD_TRANS_ABORT) {
			mode =  0;
		} else {
			mode = sdhci_readw(host, SDHCI_UHS2_TRANS_MODE);
			if (cmd->opcode == MMC_STOP_TRANSMISSION || cmd->opcode == MMC_ERASE)
				mode |= SDHCI_UHS2_TRNS_WAIT_EBSY;
			else
				/* send status mode */
				if (cmd->opcode == MMC_SEND_STATUS)
					mode = 0;
		}

		DBG("UHS2 no data trans mode is 0x%x.\n", mode);

		sdhci_writew(host, mode, SDHCI_UHS2_TRANS_MODE);
		return;
	}

	WARN_ON(!host->data);

	mode = SDHCI_UHS2_TRNS_BLK_CNT_EN | SDHCI_UHS2_TRNS_WAIT_EBSY;
	if (data->flags & MMC_DATA_WRITE)
		mode |= SDHCI_UHS2_TRNS_DATA_TRNS_WRT;

	if (data->blocks == 1 &&
	    data->blksz != 512 &&
	    cmd->opcode != MMC_READ_SINGLE_BLOCK &&
	    cmd->opcode != MMC_WRITE_BLOCK) {
		mode &= ~SDHCI_UHS2_TRNS_BLK_CNT_EN;
		mode |= SDHCI_UHS2_TRNS_BLK_BYTE_MODE;
	}

	if (host->flags & SDHCI_REQ_USE_DMA)
		mode |= SDHCI_UHS2_TRNS_DMA;

	if (cmd->uhs2_cmd->tmode_half_duplex)
		mode |= SDHCI_UHS2_TRNS_2L_HD;

	sdhci_writew(host, mode, SDHCI_UHS2_TRANS_MODE);

	DBG("UHS2 trans mode is 0x%x.\n", mode);
}

static void __sdhci_uhs2_send_command(struct sdhci_host *host, struct mmc_command *cmd)
{
	int i, j;
	int cmd_reg;

	i = 0;
	sdhci_writel(host,
		     ((u32)cmd->uhs2_cmd->arg << 16) |
				(u32)cmd->uhs2_cmd->header,
		     SDHCI_UHS2_CMD_PACKET + i);
	i += 4;

	/*
	 * Per spec, payload (config) should be MSB before sending out.
	 * But we don't need convert here because had set payload as
	 * MSB when preparing config read/write commands.
	 */
	for (j = 0; j < cmd->uhs2_cmd->payload_len / sizeof(u32); j++) {
		sdhci_writel(host, *(__force u32 *)(cmd->uhs2_cmd->payload + j),
			     SDHCI_UHS2_CMD_PACKET + i);
		i += 4;
	}

	for ( ; i < SDHCI_UHS2_CMD_PACK_MAX_LEN; i += 4)
		sdhci_writel(host, 0, SDHCI_UHS2_CMD_PACKET + i);

	DBG("UHS2 CMD packet_len = %d.\n", cmd->uhs2_cmd->packet_len);
	for (i = 0; i < cmd->uhs2_cmd->packet_len; i++)
		DBG("UHS2 CMD_PACKET[%d] = 0x%x.\n", i,
		    sdhci_readb(host, SDHCI_UHS2_CMD_PACKET + i));

	cmd_reg = FIELD_PREP(SDHCI_UHS2_CMD_PACK_LEN_MASK, cmd->uhs2_cmd->packet_len);
	if ((cmd->flags & MMC_CMD_MASK) == MMC_CMD_ADTC)
		cmd_reg |= SDHCI_UHS2_CMD_DATA;
	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		cmd_reg |= SDHCI_UHS2_CMD_CMD12;

	/* UHS2 Native ABORT */
	if ((cmd->uhs2_cmd->header & UHS2_NATIVE_PACKET) &&
	    (uhs2_dev_cmd(cmd) == UHS2_DEV_CMD_TRANS_ABORT))
		cmd_reg |= SDHCI_UHS2_CMD_TRNS_ABORT;

	/* UHS2 Native DORMANT */
	if ((cmd->uhs2_cmd->header & UHS2_NATIVE_PACKET) &&
	    (uhs2_dev_cmd(cmd) == UHS2_DEV_CMD_GO_DORMANT_STATE))
		cmd_reg |= SDHCI_UHS2_CMD_DORMANT;

	DBG("0x%x is set to UHS2 CMD register.\n", cmd_reg);

	sdhci_writew(host, cmd_reg, SDHCI_UHS2_CMD);
}

static bool sdhci_uhs2_send_command(struct sdhci_host *host, struct mmc_command *cmd)
{
	u32 mask;
	unsigned long timeout;

	WARN_ON(host->cmd);

	/* Initially, a command has no error */
	cmd->error = 0;

	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		cmd->flags |= MMC_RSP_BUSY;

	mask = SDHCI_CMD_INHIBIT;

	if (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask)
		return false;

	host->cmd = cmd;
	host->data_timeout = 0;
	if (sdhci_data_line_cmd(cmd)) {
		WARN_ON(host->data_cmd);
		host->data_cmd = cmd;
		__sdhci_uhs2_set_timeout(host);
	}

	if (cmd->data)
		sdhci_uhs2_prepare_data(host, cmd);

	sdhci_uhs2_set_transfer_mode(host, cmd);

	timeout = jiffies;
	if (host->data_timeout)
		timeout += nsecs_to_jiffies(host->data_timeout);
	else if (!cmd->data && cmd->busy_timeout > 9000)
		timeout += DIV_ROUND_UP(cmd->busy_timeout, 1000) * HZ + HZ;
	else
		timeout += 10 * HZ;
	sdhci_mod_timer(host, cmd->mrq, timeout);

	__sdhci_uhs2_send_command(host, cmd);

	return true;
}

static bool sdhci_uhs2_send_command_retry(struct sdhci_host *host,
					  struct mmc_command *cmd,
					  unsigned long flags)
	__releases(host->lock)
	__acquires(host->lock)
{
	struct mmc_command *deferred_cmd = host->deferred_cmd;
	int timeout = 10; /* Approx. 10 ms */
	bool present;

	while (!sdhci_uhs2_send_command(host, cmd)) {
		if (!timeout--) {
			pr_err("%s: Controller never released inhibit bit(s).\n",
			       mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			cmd->error = -EIO;
			return false;
		}

		spin_unlock_irqrestore(&host->lock, flags);

		usleep_range(1000, 1250);

		present = host->mmc->ops->get_cd(host->mmc);

		spin_lock_irqsave(&host->lock, flags);

		/* A deferred command might disappear, handle that */
		if (cmd == deferred_cmd && cmd != host->deferred_cmd)
			return true;

		if (sdhci_present_error(host, cmd, present))
			return false;
	}

	if (cmd == host->deferred_cmd)
		host->deferred_cmd = NULL;

	return true;
}

static void __sdhci_uhs2_finish_command(struct sdhci_host *host)
{
	struct mmc_command *cmd = host->cmd;
	u8 resp;
	u8 error_code;
	bool breada0 = 0;
	int i;

	if (host->mmc->uhs2_sd_tran) {
		resp = sdhci_readb(host, SDHCI_UHS2_RESPONSE + 2);
		if (resp & UHS2_RES_NACK_MASK) {
			error_code = (resp >> UHS2_RES_ECODE_POS) & UHS2_RES_ECODE_MASK;
			pr_err("%s: NACK response, ECODE=0x%x.\n",
			       mmc_hostname(host->mmc), error_code);
		}
		breada0 = 1;
	}

	if (cmd->uhs2_cmd->uhs2_resp_len) {
		int len = min_t(int, cmd->uhs2_cmd->uhs2_resp_len, UHS2_MAX_RESP_LEN);

		/* Get whole response of some native CCMD, like
		 * DEVICE_INIT, ENUMERATE.
		 */
		for (i = 0; i < len; i++)
			cmd->uhs2_cmd->uhs2_resp[i] = sdhci_readb(host, SDHCI_UHS2_RESPONSE + i);
	} else {
		/* Get SD CMD response and Payload for some read
		 * CCMD, like INQUIRY_CFG.
		 */
		/* Per spec (p136), payload field is divided into
		 * a unit of DWORD and transmission order within
		 * a DWORD is big endian.
		 */
		if (!breada0)
			sdhci_readl(host, SDHCI_UHS2_RESPONSE);
		for (i = 4; i < 20; i += 4) {
			cmd->resp[i / 4 - 1] =
				(sdhci_readb(host,
					     SDHCI_UHS2_RESPONSE + i) << 24) |
				(sdhci_readb(host,
					     SDHCI_UHS2_RESPONSE + i + 1)
					<< 16) |
				(sdhci_readb(host,
					     SDHCI_UHS2_RESPONSE + i + 2)
					<< 8) |
				sdhci_readb(host, SDHCI_UHS2_RESPONSE + i + 3);
		}
	}
}

static void sdhci_uhs2_finish_command(struct sdhci_host *host)
{
	struct mmc_command *cmd = host->cmd;

	__sdhci_uhs2_finish_command(host);

	host->cmd = NULL;

	if (cmd->mrq->cap_cmd_during_tfr && cmd == cmd->mrq->cmd)
		mmc_command_done(host->mmc, cmd->mrq);

	/*
	 * The host can send and interrupt when the busy state has
	 * ended, allowing us to wait without wasting CPU cycles.
	 * The busy signal uses DAT0 so this is similar to waiting
	 * for data to complete.
	 *
	 * Note: The 1.0 specification is a bit ambiguous about this
	 *       feature so there might be some problems with older
	 *       controllers.
	 */
	if (cmd->flags & MMC_RSP_BUSY) {
		if (cmd->data) {
			DBG("Cannot wait for busy signal when also doing a data transfer");
		} else if (!(host->quirks & SDHCI_QUIRK_NO_BUSY_IRQ) &&
			   cmd == host->data_cmd) {
			/* Command complete before busy is ended */
			return;
		}
	}

	/* Processed actual command. */
	if (host->data && host->data_early)
		sdhci_uhs2_finish_data(host);

	if (!cmd->data)
		__sdhci_finish_mrq(host, cmd->mrq);
}

static void sdhci_uhs2_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	unsigned long flags;
	bool present;

	if (!(mmc_card_uhs2(mmc))) {
		sdhci_request(mmc, mrq);
		return;
	}

	mrq->stop = NULL;
	mrq->sbc = NULL;
	if (mrq->data)
		mrq->data->stop = NULL;

	/* Firstly check card presence */
	present = mmc->ops->get_cd(mmc);

	spin_lock_irqsave(&host->lock, flags);

	if (sdhci_present_error(host, mrq->cmd, present))
		goto out_finish;

	cmd = mrq->cmd;

	if (!sdhci_uhs2_send_command_retry(host, cmd, flags))
		goto out_finish;

	spin_unlock_irqrestore(&host->lock, flags);

	return;

out_finish:
	sdhci_finish_mrq(host, mrq);
	spin_unlock_irqrestore(&host->lock, flags);
}

/*****************************************************************************\
 *                                                                           *
 * Request done                                                              *
 *                                                                           *
\*****************************************************************************/

static bool sdhci_uhs2_needs_reset(struct sdhci_host *host, struct mmc_request *mrq)
{
	return sdhci_needs_reset(host, mrq) ||
	       (!(host->flags & SDHCI_DEVICE_DEAD) && mrq->data && mrq->data->error);
}

static bool sdhci_uhs2_request_done(struct sdhci_host *host)
{
	unsigned long flags;
	struct mmc_request *mrq;
	int i;

	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		mrq = host->mrqs_done[i];
		if (mrq)
			break;
	}

	if (!mrq) {
		spin_unlock_irqrestore(&host->lock, flags);
		return true;
	}

	/*
	 * Always unmap the data buffers if they were mapped by
	 * sdhci_prepare_data() whenever we finish with a request.
	 * This avoids leaking DMA mappings on error.
	 */
	if (host->flags & SDHCI_REQ_USE_DMA)
		sdhci_request_done_dma(host, mrq);

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if (sdhci_uhs2_needs_reset(host, mrq)) {
		/*
		 * Do not finish until command and data lines are available for
		 * reset. Note there can only be one other mrq, so it cannot
		 * also be in mrqs_done, otherwise host->cmd and host->data_cmd
		 * would both be null.
		 */
		if (host->cmd || host->data_cmd) {
			spin_unlock_irqrestore(&host->lock, flags);
			return true;
		}

		if (mrq->cmd->error || mrq->data->error)
			sdhci_uhs2_reset_cmd_data(host);
		else
			sdhci_uhs2_reset(host, SDHCI_UHS2_SW_RESET_SD);
		host->pending_reset = false;
	}

	host->mrqs_done[i] = NULL;

	spin_unlock_irqrestore(&host->lock, flags);

	if (host->ops->request_done)
		host->ops->request_done(host, mrq);
	else
		mmc_request_done(host->mmc, mrq);

	return false;
}

static void sdhci_uhs2_complete_work(struct work_struct *work)
{
	struct sdhci_host *host = container_of(work, struct sdhci_host,
					       complete_work);

	if (!mmc_card_uhs2(host->mmc)) {
		sdhci_complete_work(work);
		return;
	}

	while (!sdhci_uhs2_request_done(host))
		;
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void __sdhci_uhs2_irq(struct sdhci_host *host, u32 uhs2mask)
{
	struct mmc_command *cmd = host->cmd;

	DBG("*** %s got UHS2 error interrupt: 0x%08x\n",
	    mmc_hostname(host->mmc), uhs2mask);

	if (uhs2mask & SDHCI_UHS2_INT_CMD_ERR_MASK) {
		if (!host->cmd) {
			pr_err("%s: Got cmd interrupt 0x%08x but no cmd.\n",
			       mmc_hostname(host->mmc),
			       (unsigned int)uhs2mask);
			sdhci_dumpregs(host);
			return;
		}
		host->cmd->error = -EILSEQ;
		if (uhs2mask & SDHCI_UHS2_INT_CMD_TIMEOUT)
			host->cmd->error = -ETIMEDOUT;
	}

	if (uhs2mask & SDHCI_UHS2_INT_DATA_ERR_MASK) {
		if (!host->data) {
			pr_err("%s: Got data interrupt 0x%08x but no data.\n",
			       mmc_hostname(host->mmc),
			       (unsigned int)uhs2mask);
			sdhci_dumpregs(host);
			return;
		}

		if (uhs2mask & SDHCI_UHS2_INT_DEADLOCK_TIMEOUT) {
			pr_err("%s: Got deadlock timeout interrupt 0x%08x\n",
			       mmc_hostname(host->mmc),
			       (unsigned int)uhs2mask);
			host->data->error = -ETIMEDOUT;
		} else if (uhs2mask & SDHCI_UHS2_INT_ADMA_ERROR) {
			pr_err("%s: ADMA error = 0x %x\n",
			       mmc_hostname(host->mmc),
			       sdhci_readb(host, SDHCI_ADMA_ERROR));
			host->data->error = -EIO;
		} else {
			host->data->error = -EILSEQ;
		}
	}

	if (host->data && host->data->error)
		sdhci_uhs2_finish_data(host);
	else
		sdhci_finish_mrq(host, cmd->mrq);

}

u32 sdhci_uhs2_irq(struct sdhci_host *host, u32 intmask)
{
	u32 mask = intmask, uhs2mask;

	if (!mmc_card_uhs2(host->mmc))
		goto out;

	if (intmask & SDHCI_INT_ERROR) {
		uhs2mask = sdhci_readl(host, SDHCI_UHS2_INT_STATUS);
		if (!(uhs2mask & SDHCI_UHS2_INT_ERROR_MASK))
			goto cmd_irq;

		/* Clear error interrupts */
		sdhci_writel(host, uhs2mask & SDHCI_UHS2_INT_ERROR_MASK,
			     SDHCI_UHS2_INT_STATUS);

		/* Handle error interrupts */
		__sdhci_uhs2_irq(host, uhs2mask);

		/* Caller, sdhci_irq(), doesn't have to care about UHS-2 errors */
		intmask &= ~SDHCI_INT_ERROR;
		mask &= SDHCI_INT_ERROR;
	}

cmd_irq:
	if (intmask & SDHCI_INT_CMD_MASK) {
		/* Clear command interrupt */
		sdhci_writel(host, intmask & SDHCI_INT_CMD_MASK, SDHCI_INT_STATUS);

		/* Handle command interrupt */
		if (intmask & SDHCI_INT_RESPONSE)
			sdhci_uhs2_finish_command(host);

		/* Caller, sdhci_irq(), doesn't have to care about UHS-2 commands */
		intmask &= ~SDHCI_INT_CMD_MASK;
		mask &= SDHCI_INT_CMD_MASK;
	}

	/* Clear already-handled interrupts. */
	sdhci_writel(host, mask, SDHCI_INT_STATUS);

out:
	return intmask;
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_irq);

static irqreturn_t sdhci_uhs2_thread_irq(int irq, void *dev_id)
{
	struct sdhci_host *host = dev_id;
	struct mmc_command *cmd;
	unsigned long flags;
	u32 isr;

	if (!mmc_card_uhs2(host->mmc))
		return sdhci_thread_irq(irq, dev_id);

	while (!sdhci_uhs2_request_done(host))
		;

	spin_lock_irqsave(&host->lock, flags);

	isr = host->thread_isr;
	host->thread_isr = 0;

	cmd = host->deferred_cmd;
	if (cmd && !sdhci_uhs2_send_command_retry(host, cmd, flags))
		sdhci_finish_mrq(host, cmd->mrq);

	spin_unlock_irqrestore(&host->lock, flags);

	if (isr & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		struct mmc_host *mmc = host->mmc;

		mmc->ops->card_event(mmc);
		mmc_detect_change(mmc, msecs_to_jiffies(200));
	}

	return IRQ_HANDLED;
}

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int sdhci_uhs2_host_ops_init(struct sdhci_host *host)
{
	host->mmc_host_ops.uhs2_control = sdhci_uhs2_control;
	host->mmc_host_ops.request = sdhci_uhs2_request;

	return 0;
}

static int __init sdhci_uhs2_mod_init(void)
{
	return 0;
}
module_init(sdhci_uhs2_mod_init);

static void __exit sdhci_uhs2_mod_exit(void)
{
}
module_exit(sdhci_uhs2_mod_exit);

/*****************************************************************************\
 *
 * Device allocation/registration                                            *
 *                                                                           *
\*****************************************************************************/

static void __sdhci_uhs2_add_host_v4(struct sdhci_host *host, u32 caps1)
{
	struct mmc_host *mmc;
	u32 max_current_caps2;

	mmc = host->mmc;

	/* Support UHS2 */
	if (caps1 & SDHCI_SUPPORT_UHS2)
		mmc->caps2 |= MMC_CAP2_SD_UHS2;

	max_current_caps2 = sdhci_readl(host, SDHCI_MAX_CURRENT_1);

	if ((caps1 & SDHCI_CAN_VDD2_180) &&
	    !max_current_caps2 &&
	    !IS_ERR(mmc->supply.vqmmc2)) {
		/* UHS2 - VDD2 */
		int curr = regulator_get_current_limit(mmc->supply.vqmmc2);

		if (curr > 0) {
			/* convert to SDHCI_MAX_CURRENT format */
			curr = curr / 1000;  /* convert to mA */
			curr = curr / SDHCI_MAX_CURRENT_MULTIPLIER;
			curr = min_t(u32, curr, SDHCI_MAX_CURRENT_LIMIT);
			max_current_caps2 = curr;
		}
	}

	if (!(caps1 & SDHCI_CAN_VDD2_180))
		mmc->caps2 &= ~MMC_CAP2_SD_UHS2;
}

static void __sdhci_uhs2_remove_host(struct sdhci_host *host, int dead)
{
	if (!mmc_card_uhs2(host->mmc))
		return;

	if (!dead)
		sdhci_uhs2_reset(host, SDHCI_UHS2_SW_RESET_FULL);
}

int sdhci_uhs2_add_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	int ret;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	if (host->version >= SDHCI_SPEC_400)
		__sdhci_uhs2_add_host_v4(host, host->caps1);

	if ((mmc->caps2 & MMC_CAP2_SD_UHS2) && !host->v4_mode)
		/* host doesn't want to enable UHS2 support */
		mmc->caps2 &= ~MMC_CAP2_SD_UHS2;

	/* overwrite ops */
	if (mmc->caps2 & MMC_CAP2_SD_UHS2)
		sdhci_uhs2_host_ops_init(host);

	host->complete_work_fn = sdhci_uhs2_complete_work;
	host->thread_irq_fn    = sdhci_uhs2_thread_irq;

	/* LED support not implemented for UHS2 */
	host->quirks |= SDHCI_QUIRK_NO_LED;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	if (host->version >= SDHCI_SPEC_400)
		__sdhci_uhs2_remove_host(host, 0);

	sdhci_cleanup_host(host);

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_add_host);

void sdhci_uhs2_remove_host(struct sdhci_host *host, int dead)
{
	__sdhci_uhs2_remove_host(host, dead);

	sdhci_remove_host(host, dead);
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_remove_host);

MODULE_AUTHOR("Intel, Genesys Logic, Linaro");
MODULE_DESCRIPTION("MMC UHS-II Support");
MODULE_LICENSE("GPL");
