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

#include "sdhci.h"
#include "sdhci-uhs2.h"

#define DRIVER_NAME "sdhci_uhs2"
#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__, ## x)
#define SDHCI_UHS2_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

#define UHS2_RESET_TIMEOUT_100MS		100000

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
		pr_warn("%s: %s: Reset 0x%x never completed. %s: clean reset bit.\n", __func__,
			mmc_hostname(host->mmc), (int)mask, mmc_hostname(host->mmc));
		sdhci_writeb(host, 0, SDHCI_UHS2_SW_RESET);
		return;
	}
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_reset);

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

	sdhci_set_clock(host, host->clock);
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

static int sdhci_uhs2_control(struct mmc_host *mmc, enum sd_uhs2_operation op)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_ios *ios = &mmc->ios;
	int err = 0;

	DBG("Begin uhs2 control, act %d.\n", op);

	switch (op) {
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
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int sdhci_uhs2_host_ops_init(struct sdhci_host *host)
{
	host->mmc_host_ops.uhs2_control = sdhci_uhs2_control;

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
