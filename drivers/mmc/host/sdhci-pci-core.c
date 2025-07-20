// SPDX-License-Identifier: GPL-2.0-or-later
/*  linux/drivers/mmc/host/sdhci-pci.c - SDHCI on PCI bus interface
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 *
 * Thanks to the following companies for their support:
 *
 *     - JMicron (hardware and technical support)
 */

#include <linux/bitfield.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/debugfs.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>

#ifdef CONFIG_X86
#include <asm/iosf_mbi.h>
#endif

#include "cqhci.h"

#include "sdhci.h"
#include "sdhci-cqhci.h"
#include "sdhci-pci.h"
#include "sdhci-uhs2.h"

static void sdhci_pci_hw_reset(struct sdhci_host *host);

#ifdef CONFIG_PM_SLEEP
static int sdhci_pci_init_wakeup(struct sdhci_pci_chip *chip)
{
	mmc_pm_flag_t pm_flags = 0;
	bool cap_cd_wake = false;
	int i;

	for (i = 0; i < chip->num_slots; i++) {
		struct sdhci_pci_slot *slot = chip->slots[i];

		if (slot) {
			pm_flags |= slot->host->mmc->pm_flags;
			if (slot->host->mmc->caps & MMC_CAP_CD_WAKE)
				cap_cd_wake = true;
		}
	}

	if ((pm_flags & MMC_PM_KEEP_POWER) && (pm_flags & MMC_PM_WAKE_SDIO_IRQ))
		return device_wakeup_enable(&chip->pdev->dev);
	else if (!cap_cd_wake)
		device_wakeup_disable(&chip->pdev->dev);

	return 0;
}

static int sdhci_pci_suspend_host(struct sdhci_pci_chip *chip)
{
	int i, ret;

	sdhci_pci_init_wakeup(chip);

	for (i = 0; i < chip->num_slots; i++) {
		struct sdhci_pci_slot *slot = chip->slots[i];
		struct sdhci_host *host;

		if (!slot)
			continue;

		host = slot->host;

		if (chip->pm_retune && host->tuning_mode != SDHCI_TUNING_MODE_3)
			mmc_retune_needed(host->mmc);

		ret = sdhci_suspend_host(host);
		if (ret)
			goto err_pci_suspend;

		if (device_may_wakeup(&chip->pdev->dev))
			mmc_gpio_set_cd_wake(host->mmc, true);
	}

	return 0;

err_pci_suspend:
	while (--i >= 0)
		sdhci_resume_host(chip->slots[i]->host);
	return ret;
}

int sdhci_pci_resume_host(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot;
	int i, ret;

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_resume_host(slot->host);
		if (ret)
			return ret;

		mmc_gpio_set_cd_wake(slot->host->mmc, false);
	}

	return 0;
}

static int sdhci_cqhci_suspend(struct sdhci_pci_chip *chip)
{
	int ret;

	ret = cqhci_suspend(chip->slots[0]->host->mmc);
	if (ret)
		return ret;

	return sdhci_pci_suspend_host(chip);
}

static int sdhci_cqhci_resume(struct sdhci_pci_chip *chip)
{
	int ret;

	ret = sdhci_pci_resume_host(chip);
	if (ret)
		return ret;

	return cqhci_resume(chip->slots[0]->host->mmc);
}
#endif

#ifdef CONFIG_PM
static int sdhci_pci_runtime_suspend_host(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot;
	struct sdhci_host *host;
	int i, ret;

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		host = slot->host;

		ret = sdhci_runtime_suspend_host(host);
		if (ret)
			goto err_pci_runtime_suspend;

		if (chip->rpm_retune &&
		    host->tuning_mode != SDHCI_TUNING_MODE_3)
			mmc_retune_needed(host->mmc);
	}

	return 0;

err_pci_runtime_suspend:
	while (--i >= 0)
		sdhci_runtime_resume_host(chip->slots[i]->host, 0);
	return ret;
}

static int sdhci_pci_runtime_resume_host(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot;
	int i, ret;

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_runtime_resume_host(slot->host, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int sdhci_cqhci_runtime_suspend(struct sdhci_pci_chip *chip)
{
	int ret;

	ret = cqhci_suspend(chip->slots[0]->host->mmc);
	if (ret)
		return ret;

	return sdhci_pci_runtime_suspend_host(chip);
}

static int sdhci_cqhci_runtime_resume(struct sdhci_pci_chip *chip)
{
	int ret;

	ret = sdhci_pci_runtime_resume_host(chip);
	if (ret)
		return ret;

	return cqhci_resume(chip->slots[0]->host->mmc);
}
#endif

static u32 sdhci_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

static void sdhci_pci_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

/*****************************************************************************\
 *                                                                           *
 * Hardware specific quirk handling                                          *
 *                                                                           *
\*****************************************************************************/

static int ricoh_probe(struct sdhci_pci_chip *chip)
{
	if (chip->pdev->subsystem_vendor == PCI_VENDOR_ID_SAMSUNG ||
	    chip->pdev->subsystem_vendor == PCI_VENDOR_ID_SONY)
		chip->quirks |= SDHCI_QUIRK_NO_CARD_NO_RESET;
	return 0;
}

static int ricoh_mmc_probe_slot(struct sdhci_pci_slot *slot)
{
	u32 caps =
		FIELD_PREP(SDHCI_TIMEOUT_CLK_MASK, 0x21) |
		FIELD_PREP(SDHCI_CLOCK_BASE_MASK, 0x21) |
		SDHCI_TIMEOUT_CLK_UNIT |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_DO_HISPD |
		SDHCI_CAN_DO_SDMA;
	u32 caps1 = 0;

	__sdhci_read_caps(slot->host, NULL, &caps, &caps1);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ricoh_mmc_resume(struct sdhci_pci_chip *chip)
{
	/* Apply a delay to allow controller to settle */
	/* Otherwise it becomes confused if card state changed
		during suspend */
	msleep(500);
	return sdhci_pci_resume_host(chip);
}
#endif

static const struct sdhci_pci_fixes sdhci_ricoh = {
	.probe		= ricoh_probe,
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_FORCE_DMA |
			  SDHCI_QUIRK_CLOCK_BEFORE_RESET,
};

static const struct sdhci_pci_fixes sdhci_ricoh_mmc = {
	.probe_slot	= ricoh_mmc_probe_slot,
#ifdef CONFIG_PM_SLEEP
	.resume		= ricoh_mmc_resume,
#endif
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_CLOCK_BEFORE_RESET |
			  SDHCI_QUIRK_NO_CARD_NO_RESET,
};

static void ene_714_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	sdhci_set_ios(mmc, ios);

	/*
	 * Some (ENE) controllers misbehave on some ios operations,
	 * signalling timeout and CRC errors even on CMD0. Resetting
	 * it on each ios seems to solve the problem.
	 */
	if (!(host->flags & SDHCI_DEVICE_DEAD))
		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
}

static int ene_714_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc_host_ops.set_ios = ene_714_set_ios;
	return 0;
}

static const struct sdhci_pci_fixes sdhci_ene_712 = {
	.quirks		= SDHCI_QUIRK_SINGLE_POWER_WRITE |
			  SDHCI_QUIRK_BROKEN_DMA,
};

static const struct sdhci_pci_fixes sdhci_ene_714 = {
	.quirks		= SDHCI_QUIRK_SINGLE_POWER_WRITE |
			  SDHCI_QUIRK_BROKEN_DMA,
	.probe_slot	= ene_714_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_cafe = {
	.quirks		= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
			  SDHCI_QUIRK_NO_BUSY_IRQ |
			  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
			  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
};

static const struct sdhci_pci_fixes sdhci_intel_qrk = {
	.quirks		= SDHCI_QUIRK_NO_HISPD_BIT,
};

static int mrst_hc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA;
	return 0;
}

/*
 * ADMA operation is disabled for Moorestown platform due to
 * hardware bugs.
 */
static int mrst_hc_probe(struct sdhci_pci_chip *chip)
{
	/*
	 * slots number is fixed here for MRST as SDIO3/5 are never used and
	 * have hardware bugs.
	 */
	chip->num_slots = 1;
	return 0;
}

static int pch_hc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA;
	return 0;
}

static int mfd_emmc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE;
	slot->host->mmc->caps2 |= MMC_CAP2_BOOTPART_NOACC;
	return 0;
}

static int mfd_sdio_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_POWER_OFF_CARD | MMC_CAP_NONREMOVABLE;
	return 0;
}

static const struct sdhci_pci_fixes sdhci_intel_mrst_hc0 = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_NO_HISPD_BIT,
	.probe_slot	= mrst_hc_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_mrst_hc1_hc2 = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_NO_HISPD_BIT,
	.probe		= mrst_hc_probe,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_sd = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.allow_runtime_pm = true,
	.own_cd_for_runtime_pm = true,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_sdio = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2	= SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.allow_runtime_pm = true,
	.probe_slot	= mfd_sdio_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_emmc = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.allow_runtime_pm = true,
	.probe_slot	= mfd_emmc_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_pch_sdio = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA,
	.probe_slot	= pch_hc_probe_slot,
};

#ifdef CONFIG_X86

#define BYT_IOSF_SCCEP			0x63
#define BYT_IOSF_OCP_NETCTRL0		0x1078
#define BYT_IOSF_OCP_TIMEOUT_BASE	GENMASK(10, 8)

static void byt_ocp_setting(struct pci_dev *pdev)
{
	u32 val = 0;

	if (pdev->device != PCI_DEVICE_ID_INTEL_BYT_EMMC &&
	    pdev->device != PCI_DEVICE_ID_INTEL_BYT_SDIO &&
	    pdev->device != PCI_DEVICE_ID_INTEL_BYT_SD &&
	    pdev->device != PCI_DEVICE_ID_INTEL_BYT_EMMC2)
		return;

	if (iosf_mbi_read(BYT_IOSF_SCCEP, MBI_CR_READ, BYT_IOSF_OCP_NETCTRL0,
			  &val)) {
		dev_err(&pdev->dev, "%s read error\n", __func__);
		return;
	}

	if (!(val & BYT_IOSF_OCP_TIMEOUT_BASE))
		return;

	val &= ~BYT_IOSF_OCP_TIMEOUT_BASE;

	if (iosf_mbi_write(BYT_IOSF_SCCEP, MBI_CR_WRITE, BYT_IOSF_OCP_NETCTRL0,
			   val)) {
		dev_err(&pdev->dev, "%s write error\n", __func__);
		return;
	}

	dev_dbg(&pdev->dev, "%s completed\n", __func__);
}

#else

static inline void byt_ocp_setting(struct pci_dev *pdev)
{
}

#endif

enum {
	INTEL_DSM_FNS		=  0,
	INTEL_DSM_V18_SWITCH	=  3,
	INTEL_DSM_V33_SWITCH	=  4,
	INTEL_DSM_DRV_STRENGTH	=  9,
	INTEL_DSM_D3_RETUNE	= 10,
};

struct intel_host {
	u32	dsm_fns;
	int	drv_strength;
	bool	d3_retune;
	bool	rpm_retune_ok;
	bool	needs_pwr_off;
	u32	glk_rx_ctrl1;
	u32	glk_tun_val;
	u32	active_ltr;
	u32	idle_ltr;
};

static const guid_t intel_dsm_guid =
	GUID_INIT(0xF6C13EA5, 0x65CD, 0x461F,
		  0xAB, 0x7A, 0x29, 0xF7, 0xE8, 0xD5, 0xBD, 0x61);

static int __intel_dsm(struct intel_host *intel_host, struct device *dev,
		       unsigned int fn, u32 *result)
{
	union acpi_object *obj;
	int err = 0;
	size_t len;

	obj = acpi_evaluate_dsm_typed(ACPI_HANDLE(dev), &intel_dsm_guid, 0, fn, NULL,
				      ACPI_TYPE_BUFFER);
	if (!obj)
		return -EOPNOTSUPP;

	if (obj->buffer.length < 1) {
		err = -EINVAL;
		goto out;
	}

	len = min_t(size_t, obj->buffer.length, 4);

	*result = 0;
	memcpy(result, obj->buffer.pointer, len);
out:
	ACPI_FREE(obj);

	return err;
}

static int intel_dsm(struct intel_host *intel_host, struct device *dev,
		     unsigned int fn, u32 *result)
{
	if (fn > 31 || !(intel_host->dsm_fns & (1 << fn)))
		return -EOPNOTSUPP;

	return __intel_dsm(intel_host, dev, fn, result);
}

static void intel_dsm_init(struct intel_host *intel_host, struct device *dev,
			   struct mmc_host *mmc)
{
	int err;
	u32 val;

	intel_host->d3_retune = true;

	err = __intel_dsm(intel_host, dev, INTEL_DSM_FNS, &intel_host->dsm_fns);
	if (err) {
		pr_debug("%s: DSM not supported, error %d\n",
			 mmc_hostname(mmc), err);
		return;
	}

	pr_debug("%s: DSM function mask %#x\n",
		 mmc_hostname(mmc), intel_host->dsm_fns);

	err = intel_dsm(intel_host, dev, INTEL_DSM_DRV_STRENGTH, &val);
	intel_host->drv_strength = err ? 0 : val;

	err = intel_dsm(intel_host, dev, INTEL_DSM_D3_RETUNE, &val);
	intel_host->d3_retune = err ? true : !!val;
}

static void sdhci_pci_int_hw_reset(struct sdhci_host *host)
{
	u8 reg;

	reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
	reg |= 0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 1us but give it 9us for good measure */
	udelay(9);
	reg &= ~0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static int intel_select_drive_strength(struct mmc_card *card,
				       unsigned int max_dtr, int host_drv,
				       int card_drv, int *drv_type)
{
	struct sdhci_host *host = mmc_priv(card->host);
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct intel_host *intel_host = sdhci_pci_priv(slot);

	if (!(mmc_driver_type_mask(intel_host->drv_strength) & card_drv))
		return 0;

	return intel_host->drv_strength;
}

static int bxt_get_cd(struct mmc_host *mmc)
{
	int gpio_cd = mmc_gpio_get_cd(mmc);

	if (!gpio_cd)
		return 0;

	return sdhci_get_cd_nogpio(mmc);
}

static int mrfld_get_cd(struct mmc_host *mmc)
{
	return sdhci_get_cd_nogpio(mmc);
}

#define SDHCI_INTEL_PWR_TIMEOUT_CNT	20
#define SDHCI_INTEL_PWR_TIMEOUT_UDELAY	100

static void sdhci_intel_set_power(struct sdhci_host *host, unsigned char mode,
				  unsigned short vdd)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	int cntr;
	u8 reg;

	/*
	 * Bus power may control card power, but a full reset still may not
	 * reset the power, whereas a direct write to SDHCI_POWER_CONTROL can.
	 * That might be needed to initialize correctly, if the card was left
	 * powered on previously.
	 */
	if (intel_host->needs_pwr_off) {
		intel_host->needs_pwr_off = false;
		if (mode != MMC_POWER_OFF) {
			sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);
			usleep_range(10000, 12500);
		}
	}

	sdhci_set_power(host, mode, vdd);

	if (mode == MMC_POWER_OFF) {
		if (slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_APL_SD ||
		    slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_BYT_SD)
			usleep_range(15000, 17500);
		return;
	}

	/*
	 * Bus power might not enable after D3 -> D0 transition due to the
	 * present state not yet having propagated. Retry for up to 2ms.
	 */
	for (cntr = 0; cntr < SDHCI_INTEL_PWR_TIMEOUT_CNT; cntr++) {
		reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
		if (reg & SDHCI_POWER_ON)
			break;
		udelay(SDHCI_INTEL_PWR_TIMEOUT_UDELAY);
		reg |= SDHCI_POWER_ON;
		sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	}
}

static void sdhci_intel_set_uhs_signaling(struct sdhci_host *host,
					  unsigned int timing)
{
	/* Set UHS timing to SDR25 for High Speed mode */
	if (timing == MMC_TIMING_MMC_HS || timing == MMC_TIMING_SD_HS)
		timing = MMC_TIMING_UHS_SDR25;
	sdhci_set_uhs_signaling(host, timing);
}

#define INTEL_HS400_ES_REG 0x78
#define INTEL_HS400_ES_BIT BIT(0)

static void intel_hs400_enhanced_strobe(struct mmc_host *mmc,
					struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 val;

	val = sdhci_readl(host, INTEL_HS400_ES_REG);
	if (ios->enhanced_strobe)
		val |= INTEL_HS400_ES_BIT;
	else
		val &= ~INTEL_HS400_ES_BIT;
	sdhci_writel(host, val, INTEL_HS400_ES_REG);
}

static int intel_start_signal_voltage_switch(struct mmc_host *mmc,
					     struct mmc_ios *ios)
{
	struct device *dev = mmc_dev(mmc);
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	unsigned int fn;
	u32 result = 0;
	int err;

	err = sdhci_start_signal_voltage_switch(mmc, ios);
	if (err)
		return err;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		fn = INTEL_DSM_V33_SWITCH;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		fn = INTEL_DSM_V18_SWITCH;
		break;
	default:
		return 0;
	}

	err = intel_dsm(intel_host, dev, fn, &result);
	pr_debug("%s: %s DSM fn %u error %d result %u\n",
		 mmc_hostname(mmc), __func__, fn, err, result);

	return 0;
}

static const struct sdhci_ops sdhci_intel_byt_ops = {
	.set_clock		= sdhci_set_clock,
	.set_power		= sdhci_intel_set_power,
	.enable_dma		= sdhci_pci_enable_dma,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_reset,
	.set_uhs_signaling	= sdhci_intel_set_uhs_signaling,
	.hw_reset		= sdhci_pci_hw_reset,
};

static const struct sdhci_ops sdhci_intel_glk_ops = {
	.set_clock		= sdhci_set_clock,
	.set_power		= sdhci_intel_set_power,
	.enable_dma		= sdhci_pci_enable_dma,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_and_cqhci_reset,
	.set_uhs_signaling	= sdhci_intel_set_uhs_signaling,
	.hw_reset		= sdhci_pci_hw_reset,
	.irq			= sdhci_cqhci_irq,
};

static void byt_read_dsm(struct sdhci_pci_slot *slot)
{
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	struct device *dev = &slot->chip->pdev->dev;
	struct mmc_host *mmc = slot->host->mmc;

	intel_dsm_init(intel_host, dev, mmc);
	slot->chip->rpm_retune = intel_host->d3_retune;
}

static int intel_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	int err = sdhci_execute_tuning(mmc, opcode);
	struct sdhci_host *host = mmc_priv(mmc);

	if (err)
		return err;

	/*
	 * Tuning can leave the IP in an active state (Buffer Read Enable bit
	 * set) which prevents the entry to low power states (i.e. S0i3). Data
	 * reset will clear it.
	 */
	sdhci_reset(host, SDHCI_RESET_DATA);

	return 0;
}

#define INTEL_ACTIVELTR		0x804
#define INTEL_IDLELTR		0x808

#define INTEL_LTR_REQ		BIT(15)
#define INTEL_LTR_SCALE_MASK	GENMASK(11, 10)
#define INTEL_LTR_SCALE_1US	(2 << 10)
#define INTEL_LTR_SCALE_32US	(3 << 10)
#define INTEL_LTR_VALUE_MASK	GENMASK(9, 0)

static void intel_cache_ltr(struct sdhci_pci_slot *slot)
{
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	struct sdhci_host *host = slot->host;

	intel_host->active_ltr = readl(host->ioaddr + INTEL_ACTIVELTR);
	intel_host->idle_ltr = readl(host->ioaddr + INTEL_IDLELTR);
}

static void intel_ltr_set(struct device *dev, s32 val)
{
	struct sdhci_pci_chip *chip = dev_get_drvdata(dev);
	struct sdhci_pci_slot *slot = chip->slots[0];
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	struct sdhci_host *host = slot->host;
	u32 ltr;

	pm_runtime_get_sync(dev);

	/*
	 * Program latency tolerance (LTR) accordingly what has been asked
	 * by the PM QoS layer or disable it in case we were passed
	 * negative value or PM_QOS_LATENCY_ANY.
	 */
	ltr = readl(host->ioaddr + INTEL_ACTIVELTR);

	if (val == PM_QOS_LATENCY_ANY || val < 0) {
		ltr &= ~INTEL_LTR_REQ;
	} else {
		ltr |= INTEL_LTR_REQ;
		ltr &= ~INTEL_LTR_SCALE_MASK;
		ltr &= ~INTEL_LTR_VALUE_MASK;

		if (val > INTEL_LTR_VALUE_MASK) {
			val >>= 5;
			if (val > INTEL_LTR_VALUE_MASK)
				val = INTEL_LTR_VALUE_MASK;
			ltr |= INTEL_LTR_SCALE_32US | val;
		} else {
			ltr |= INTEL_LTR_SCALE_1US | val;
		}
	}

	if (ltr == intel_host->active_ltr)
		goto out;

	writel(ltr, host->ioaddr + INTEL_ACTIVELTR);
	writel(ltr, host->ioaddr + INTEL_IDLELTR);

	/* Cache the values into lpss structure */
	intel_cache_ltr(slot);
out:
	pm_runtime_put_autosuspend(dev);
}

static bool intel_use_ltr(struct sdhci_pci_chip *chip)
{
	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_INTEL_BYT_EMMC:
	case PCI_DEVICE_ID_INTEL_BYT_EMMC2:
	case PCI_DEVICE_ID_INTEL_BYT_SDIO:
	case PCI_DEVICE_ID_INTEL_BYT_SD:
	case PCI_DEVICE_ID_INTEL_BSW_EMMC:
	case PCI_DEVICE_ID_INTEL_BSW_SDIO:
	case PCI_DEVICE_ID_INTEL_BSW_SD:
		return false;
	default:
		return true;
	}
}

static void intel_ltr_expose(struct sdhci_pci_chip *chip)
{
	struct device *dev = &chip->pdev->dev;

	if (!intel_use_ltr(chip))
		return;

	dev->power.set_latency_tolerance = intel_ltr_set;
	dev_pm_qos_expose_latency_tolerance(dev);
}

static void intel_ltr_hide(struct sdhci_pci_chip *chip)
{
	struct device *dev = &chip->pdev->dev;

	if (!intel_use_ltr(chip))
		return;

	dev_pm_qos_hide_latency_tolerance(dev);
	dev->power.set_latency_tolerance = NULL;
}

static void byt_probe_slot(struct sdhci_pci_slot *slot)
{
	struct mmc_host_ops *ops = &slot->host->mmc_host_ops;
	struct device *dev = &slot->chip->pdev->dev;
	struct mmc_host *mmc = slot->host->mmc;

	byt_read_dsm(slot);

	byt_ocp_setting(slot->chip->pdev);

	ops->execute_tuning = intel_execute_tuning;
	ops->start_signal_voltage_switch = intel_start_signal_voltage_switch;

	device_property_read_u32(dev, "max-frequency", &mmc->f_max);

	if (!mmc->slotno) {
		slot->chip->slots[mmc->slotno] = slot;
		intel_ltr_expose(slot->chip);
	}
}

static void byt_add_debugfs(struct sdhci_pci_slot *slot)
{
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	struct mmc_host *mmc = slot->host->mmc;
	struct dentry *dir = mmc->debugfs_root;

	if (!intel_use_ltr(slot->chip))
		return;

	debugfs_create_x32("active_ltr", 0444, dir, &intel_host->active_ltr);
	debugfs_create_x32("idle_ltr", 0444, dir, &intel_host->idle_ltr);

	intel_cache_ltr(slot);
}

static int byt_add_host(struct sdhci_pci_slot *slot)
{
	int ret = sdhci_add_host(slot->host);

	if (!ret)
		byt_add_debugfs(slot);
	return ret;
}

static void byt_remove_slot(struct sdhci_pci_slot *slot, int dead)
{
	struct mmc_host *mmc = slot->host->mmc;

	if (!mmc->slotno)
		intel_ltr_hide(slot->chip);
}

static int byt_emmc_probe_slot(struct sdhci_pci_slot *slot)
{
	byt_probe_slot(slot);
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE |
				 MMC_CAP_HW_RESET | MMC_CAP_1_8V_DDR |
				 MMC_CAP_CMD_DURING_TFR |
				 MMC_CAP_WAIT_WHILE_BUSY;
	slot->hw_reset = sdhci_pci_int_hw_reset;
	if (slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_BSW_EMMC)
		slot->host->timeout_clk = 1000; /* 1000 kHz i.e. 1 MHz */
	slot->host->mmc_host_ops.select_drive_strength =
						intel_select_drive_strength;
	return 0;
}

static bool glk_broken_cqhci(struct sdhci_pci_slot *slot)
{
	return slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_GLK_EMMC &&
	       (dmi_match(DMI_BIOS_VENDOR, "LENOVO") ||
		dmi_match(DMI_SYS_VENDOR, "IRBIS") ||
		dmi_match(DMI_SYS_VENDOR, "Positivo Tecnologia SA"));
}

static bool jsl_broken_hs400es(struct sdhci_pci_slot *slot)
{
	return slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_JSL_EMMC &&
			dmi_match(DMI_BIOS_VENDOR, "ASUSTeK COMPUTER INC.");
}

static int glk_emmc_probe_slot(struct sdhci_pci_slot *slot)
{
	int ret = byt_emmc_probe_slot(slot);

	if (!glk_broken_cqhci(slot))
		slot->host->mmc->caps2 |= MMC_CAP2_CQE;

	if (slot->chip->pdev->device != PCI_DEVICE_ID_INTEL_GLK_EMMC) {
		if (!jsl_broken_hs400es(slot)) {
			slot->host->mmc->caps2 |= MMC_CAP2_HS400_ES;
			slot->host->mmc_host_ops.hs400_enhanced_strobe =
							intel_hs400_enhanced_strobe;
		}
		slot->host->mmc->caps2 |= MMC_CAP2_CQE_DCMD;
	}

	return ret;
}

static const struct cqhci_host_ops glk_cqhci_ops = {
	.enable		= sdhci_cqe_enable,
	.disable	= sdhci_cqe_disable,
	.dumpregs	= sdhci_pci_dumpregs,
};

static int glk_emmc_add_host(struct sdhci_pci_slot *slot)
{
	struct device *dev = &slot->chip->pdev->dev;
	struct sdhci_host *host = slot->host;
	struct cqhci_host *cq_host;
	bool dma64;
	int ret;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	cq_host = devm_kzalloc(dev, sizeof(*cq_host), GFP_KERNEL);
	if (!cq_host) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cq_host->mmio = host->ioaddr + 0x200;
	cq_host->quirks |= CQHCI_QUIRK_SHORT_TXFR_DESC_SZ;
	cq_host->ops = &glk_cqhci_ops;

	dma64 = host->flags & SDHCI_USE_64_BIT_DMA;
	if (dma64)
		cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

	ret = cqhci_init(cq_host, host->mmc, dma64);
	if (ret)
		goto cleanup;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	byt_add_debugfs(slot);

	return 0;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

#ifdef CONFIG_PM
#define GLK_RX_CTRL1	0x834
#define GLK_TUN_VAL	0x840
#define GLK_PATH_PLL	GENMASK(13, 8)
#define GLK_DLY		GENMASK(6, 0)
/* Workaround firmware failing to restore the tuning value */
static void glk_rpm_retune_wa(struct sdhci_pci_chip *chip, bool susp)
{
	struct sdhci_pci_slot *slot = chip->slots[0];
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	struct sdhci_host *host = slot->host;
	u32 glk_rx_ctrl1;
	u32 glk_tun_val;
	u32 dly;

	if (intel_host->rpm_retune_ok || !mmc_can_retune(host->mmc))
		return;

	glk_rx_ctrl1 = sdhci_readl(host, GLK_RX_CTRL1);
	glk_tun_val = sdhci_readl(host, GLK_TUN_VAL);

	if (susp) {
		intel_host->glk_rx_ctrl1 = glk_rx_ctrl1;
		intel_host->glk_tun_val = glk_tun_val;
		return;
	}

	if (!intel_host->glk_tun_val)
		return;

	if (glk_rx_ctrl1 != intel_host->glk_rx_ctrl1) {
		intel_host->rpm_retune_ok = true;
		return;
	}

	dly = FIELD_PREP(GLK_DLY, FIELD_GET(GLK_PATH_PLL, glk_rx_ctrl1) +
				  (intel_host->glk_tun_val << 1));
	if (dly == FIELD_GET(GLK_DLY, glk_rx_ctrl1))
		return;

	glk_rx_ctrl1 = (glk_rx_ctrl1 & ~GLK_DLY) | dly;
	sdhci_writel(host, glk_rx_ctrl1, GLK_RX_CTRL1);

	intel_host->rpm_retune_ok = true;
	chip->rpm_retune = true;
	mmc_retune_needed(host->mmc);
	pr_info("%s: Requiring re-tune after rpm resume", mmc_hostname(host->mmc));
}

static void glk_rpm_retune_chk(struct sdhci_pci_chip *chip, bool susp)
{
	if (chip->pdev->device == PCI_DEVICE_ID_INTEL_GLK_EMMC &&
	    !chip->rpm_retune)
		glk_rpm_retune_wa(chip, susp);
}

static int glk_runtime_suspend(struct sdhci_pci_chip *chip)
{
	glk_rpm_retune_chk(chip, true);

	return sdhci_cqhci_runtime_suspend(chip);
}

static int glk_runtime_resume(struct sdhci_pci_chip *chip)
{
	glk_rpm_retune_chk(chip, false);

	return sdhci_cqhci_runtime_resume(chip);
}
#endif

#ifdef CONFIG_ACPI
static int ni_set_max_freq(struct sdhci_pci_slot *slot)
{
	acpi_status status;
	unsigned long long max_freq;

	status = acpi_evaluate_integer(ACPI_HANDLE(&slot->chip->pdev->dev),
				       "MXFQ", NULL, &max_freq);
	if (ACPI_FAILURE(status)) {
		dev_err(&slot->chip->pdev->dev,
			"MXFQ not found in acpi table\n");
		return -EINVAL;
	}

	slot->host->mmc->f_max = max_freq * 1000000;

	return 0;
}
#else
static inline int ni_set_max_freq(struct sdhci_pci_slot *slot)
{
	return 0;
}
#endif

static int ni_byt_sdio_probe_slot(struct sdhci_pci_slot *slot)
{
	int err;

	byt_probe_slot(slot);

	err = ni_set_max_freq(slot);
	if (err)
		return err;

	slot->host->mmc->caps |= MMC_CAP_POWER_OFF_CARD | MMC_CAP_NONREMOVABLE |
				 MMC_CAP_WAIT_WHILE_BUSY;
	return 0;
}

static int byt_sdio_probe_slot(struct sdhci_pci_slot *slot)
{
	byt_probe_slot(slot);
	slot->host->mmc->caps |= MMC_CAP_POWER_OFF_CARD | MMC_CAP_NONREMOVABLE |
				 MMC_CAP_WAIT_WHILE_BUSY;
	return 0;
}

static void byt_needs_pwr_off(struct sdhci_pci_slot *slot)
{
	struct intel_host *intel_host = sdhci_pci_priv(slot);
	u8 reg = sdhci_readb(slot->host, SDHCI_POWER_CONTROL);

	intel_host->needs_pwr_off = reg  & SDHCI_POWER_ON;
}

static int byt_sd_probe_slot(struct sdhci_pci_slot *slot)
{
	byt_probe_slot(slot);
	slot->host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY |
				 MMC_CAP_AGGRESSIVE_PM | MMC_CAP_CD_WAKE;
	slot->cd_idx = 0;
	slot->cd_override_level = true;
	if (slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_BXT_SD ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_BXTM_SD ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_APL_SD ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_INTEL_GLK_SD)
		slot->host->mmc_host_ops.get_cd = bxt_get_cd;

	if (slot->chip->pdev->subsystem_vendor == PCI_VENDOR_ID_NI &&
	    slot->chip->pdev->subsystem_device == PCI_SUBDEVICE_ID_NI_78E3)
		slot->host->mmc->caps2 |= MMC_CAP2_AVOID_3_3V;

	byt_needs_pwr_off(slot);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int byt_resume(struct sdhci_pci_chip *chip)
{
	byt_ocp_setting(chip->pdev);

	return sdhci_pci_resume_host(chip);
}

#endif

#ifdef CONFIG_PM

static int byt_runtime_resume(struct sdhci_pci_chip *chip)
{
	byt_ocp_setting(chip->pdev);

	return sdhci_pci_runtime_resume_host(chip);
}

#endif

static const struct sdhci_pci_fixes sdhci_intel_byt_emmc = {
#ifdef CONFIG_PM_SLEEP
	.resume		= byt_resume,
#endif
#ifdef CONFIG_PM
	.runtime_resume	= byt_runtime_resume,
#endif
	.allow_runtime_pm = true,
	.probe_slot	= byt_emmc_probe_slot,
	.add_host	= byt_add_host,
	.remove_slot	= byt_remove_slot,
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			  SDHCI_QUIRK_NO_LED,
	.quirks2	= SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			  SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400 |
			  SDHCI_QUIRK2_STOP_WITH_TC,
	.ops		= &sdhci_intel_byt_ops,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_pci_fixes sdhci_intel_glk_emmc = {
	.allow_runtime_pm	= true,
	.probe_slot		= glk_emmc_probe_slot,
	.add_host		= glk_emmc_add_host,
	.remove_slot		= byt_remove_slot,
#ifdef CONFIG_PM_SLEEP
	.suspend		= sdhci_cqhci_suspend,
	.resume			= sdhci_cqhci_resume,
#endif
#ifdef CONFIG_PM
	.runtime_suspend	= glk_runtime_suspend,
	.runtime_resume		= glk_runtime_resume,
#endif
	.quirks			= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
				  SDHCI_QUIRK_NO_LED,
	.quirks2		= SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
				  SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400 |
				  SDHCI_QUIRK2_STOP_WITH_TC,
	.ops			= &sdhci_intel_glk_ops,
	.priv_size		= sizeof(struct intel_host),
};

static const struct sdhci_pci_fixes sdhci_ni_byt_sdio = {
#ifdef CONFIG_PM_SLEEP
	.resume		= byt_resume,
#endif
#ifdef CONFIG_PM
	.runtime_resume	= byt_runtime_resume,
#endif
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			  SDHCI_QUIRK_NO_LED,
	.quirks2	= SDHCI_QUIRK2_HOST_OFF_CARD_ON |
			  SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.allow_runtime_pm = true,
	.probe_slot	= ni_byt_sdio_probe_slot,
	.add_host	= byt_add_host,
	.remove_slot	= byt_remove_slot,
	.ops		= &sdhci_intel_byt_ops,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_pci_fixes sdhci_intel_byt_sdio = {
#ifdef CONFIG_PM_SLEEP
	.resume		= byt_resume,
#endif
#ifdef CONFIG_PM
	.runtime_resume	= byt_runtime_resume,
#endif
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			  SDHCI_QUIRK_NO_LED,
	.quirks2	= SDHCI_QUIRK2_HOST_OFF_CARD_ON |
			SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.allow_runtime_pm = true,
	.probe_slot	= byt_sdio_probe_slot,
	.add_host	= byt_add_host,
	.remove_slot	= byt_remove_slot,
	.ops		= &sdhci_intel_byt_ops,
	.priv_size	= sizeof(struct intel_host),
};

/* DMI quirks for devices with missing or broken CD GPIO info */
static const struct gpiod_lookup_table vexia_edu_atla10_cd_gpios = {
	.dev_id = "0000:00:12.0",
	.table = {
		GPIO_LOOKUP("INT33FC:00", 38, "cd", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static const struct dmi_system_id sdhci_intel_byt_cd_gpio_override[] = {
	{
		/* Vexia Edu Atla 10 tablet 9V version */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			/* Above strings are too generic, also match on BIOS date */
			DMI_MATCH(DMI_BIOS_DATE, "08/25/2014"),
		},
		.driver_data = (void *)&vexia_edu_atla10_cd_gpios,
	},
	{ }
};

static const struct sdhci_pci_fixes sdhci_intel_byt_sd = {
#ifdef CONFIG_PM_SLEEP
	.resume		= byt_resume,
#endif
#ifdef CONFIG_PM
	.runtime_resume	= byt_runtime_resume,
#endif
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			  SDHCI_QUIRK_NO_LED,
	.quirks2	= SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON |
			  SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			  SDHCI_QUIRK2_STOP_WITH_TC,
	.allow_runtime_pm = true,
	.own_cd_for_runtime_pm = true,
	.probe_slot	= byt_sd_probe_slot,
	.add_host	= byt_add_host,
	.remove_slot	= byt_remove_slot,
	.ops		= &sdhci_intel_byt_ops,
	.cd_gpio_override = sdhci_intel_byt_cd_gpio_override,
	.priv_size	= sizeof(struct intel_host),
};

/* Define Host controllers for Intel Merrifield platform */
#define INTEL_MRFLD_EMMC_0	0
#define INTEL_MRFLD_EMMC_1	1
#define INTEL_MRFLD_SD		2
#define INTEL_MRFLD_SDIO	3

#ifdef CONFIG_ACPI
static void intel_mrfld_mmc_fix_up_power_slot(struct sdhci_pci_slot *slot)
{
	struct acpi_device *device;

	device = ACPI_COMPANION(&slot->chip->pdev->dev);
	if (device)
		acpi_device_fix_up_power_extended(device);
}
#else
static inline void intel_mrfld_mmc_fix_up_power_slot(struct sdhci_pci_slot *slot) {}
#endif

static int intel_mrfld_mmc_probe_slot(struct sdhci_pci_slot *slot)
{
	unsigned int func = PCI_FUNC(slot->chip->pdev->devfn);

	switch (func) {
	case INTEL_MRFLD_EMMC_0:
	case INTEL_MRFLD_EMMC_1:
		slot->host->mmc->caps |= MMC_CAP_NONREMOVABLE |
					 MMC_CAP_8_BIT_DATA |
					 MMC_CAP_1_8V_DDR;
		break;
	case INTEL_MRFLD_SD:
		slot->cd_idx = 0;
		slot->cd_override_level = true;
		/*
		 * There are two PCB designs of SD card slot with the opposite
		 * card detection sense. Quirk this out by ignoring GPIO state
		 * completely in the custom ->get_cd() callback.
		 */
		slot->host->mmc_host_ops.get_cd = mrfld_get_cd;
		slot->host->quirks2 |= SDHCI_QUIRK2_NO_1_8_V;
		break;
	case INTEL_MRFLD_SDIO:
		/* Advertise 2.0v for compatibility with the SDIO card's OCR */
		slot->host->ocr_mask = MMC_VDD_20_21 | MMC_VDD_165_195;
		slot->host->mmc->caps |= MMC_CAP_NONREMOVABLE |
					 MMC_CAP_POWER_OFF_CARD;
		break;
	default:
		return -ENODEV;
	}

	intel_mrfld_mmc_fix_up_power_slot(slot);
	return 0;
}

static const struct sdhci_pci_fixes sdhci_intel_mrfld_mmc = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2	= SDHCI_QUIRK2_BROKEN_HS200 |
			SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.allow_runtime_pm = true,
	.probe_slot	= intel_mrfld_mmc_probe_slot,
};

#define JMB388_SAMPLE_COUNT	5

static int jmicron_jmb388_get_ro(struct mmc_host *mmc)
{
	int i, ro_count;

	ro_count = 0;
	for (i = 0; i < JMB388_SAMPLE_COUNT; i++) {
		if (sdhci_get_ro(mmc) > 0) {
			if (++ro_count > JMB388_SAMPLE_COUNT / 2)
				return 1;
		}
		msleep(30);
	}
	return 0;
}

static int jmicron_pmos(struct sdhci_pci_chip *chip, int on)
{
	u8 scratch;
	int ret;

	ret = pci_read_config_byte(chip->pdev, 0xAE, &scratch);
	if (ret)
		goto fail;

	/*
	 * Turn PMOS on [bit 0], set over current detection to 2.4 V
	 * [bit 1:2] and enable over current debouncing [bit 6].
	 */
	if (on)
		scratch |= 0x47;
	else
		scratch &= ~0x47;

	ret = pci_write_config_byte(chip->pdev, 0xAE, scratch);

fail:
	return pcibios_err_to_errno(ret);
}

static int jmicron_probe(struct sdhci_pci_chip *chip)
{
	int ret;
	u16 mmcdev = 0;

	if (chip->pdev->revision == 0) {
		chip->quirks |= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_32BIT_ADMA_SIZE |
			  SDHCI_QUIRK_RESET_AFTER_REQUEST |
			  SDHCI_QUIRK_BROKEN_SMALL_PIO;
	}

	/*
	 * JMicron chips can have two interfaces to the same hardware
	 * in order to work around limitations in Microsoft's driver.
	 * We need to make sure we only bind to one of them.
	 *
	 * This code assumes two things:
	 *
	 * 1. The PCI code adds subfunctions in order.
	 *
	 * 2. The MMC interface has a lower subfunction number
	 *    than the SD interface.
	 */
	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_SD)
		mmcdev = PCI_DEVICE_ID_JMICRON_JMB38X_MMC;
	else if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_SD)
		mmcdev = PCI_DEVICE_ID_JMICRON_JMB388_ESD;

	if (mmcdev) {
		struct pci_dev *sd_dev;

		sd_dev = NULL;
		while ((sd_dev = pci_get_device(PCI_VENDOR_ID_JMICRON,
						mmcdev, sd_dev)) != NULL) {
			if ((PCI_SLOT(chip->pdev->devfn) ==
				PCI_SLOT(sd_dev->devfn)) &&
				(chip->pdev->bus == sd_dev->bus))
				break;
		}

		if (sd_dev) {
			pci_dev_put(sd_dev);
			dev_info(&chip->pdev->dev, "Refusing to bind to "
				"secondary interface.\n");
			return -ENODEV;
		}
	}

	/*
	 * JMicron chips need a bit of a nudge to enable the power
	 * output pins.
	 */
	ret = jmicron_pmos(chip, 1);
	if (ret) {
		dev_err(&chip->pdev->dev, "Failure enabling card power\n");
		return ret;
	}

	return 0;
}

static void jmicron_enable_mmc(struct sdhci_host *host, int on)
{
	u8 scratch;

	scratch = readb(host->ioaddr + 0xC0);

	if (on)
		scratch |= 0x01;
	else
		scratch &= ~0x01;

	writeb(scratch, host->ioaddr + 0xC0);
}

static int jmicron_probe_slot(struct sdhci_pci_slot *slot)
{
	if (slot->chip->pdev->revision == 0) {
		u16 version;

		version = readl(slot->host->ioaddr + SDHCI_HOST_VERSION);
		version = (version & SDHCI_VENDOR_VER_MASK) >>
			SDHCI_VENDOR_VER_SHIFT;

		/*
		 * Older versions of the chip have lots of nasty glitches
		 * in the ADMA engine. It's best just to avoid it
		 * completely.
		 */
		if (version < 0xAC)
			slot->host->quirks |= SDHCI_QUIRK_BROKEN_ADMA;
	}

	/* JM388 MMC doesn't support 1.8V while SD supports it */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		slot->host->ocr_avail_sd = MMC_VDD_32_33 | MMC_VDD_33_34 |
			MMC_VDD_29_30 | MMC_VDD_30_31 |
			MMC_VDD_165_195; /* allow 1.8V */
		slot->host->ocr_avail_mmc = MMC_VDD_32_33 | MMC_VDD_33_34 |
			MMC_VDD_29_30 | MMC_VDD_30_31; /* no 1.8V for MMC */
	}

	/*
	 * The secondary interface requires a bit set to get the
	 * interrupts.
	 */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		jmicron_enable_mmc(slot->host, 1);

	slot->host->mmc->caps |= MMC_CAP_BUS_WIDTH_TEST;

	/* Handle unstable RO-detection on JM388 chips */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_SD ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		slot->host->mmc_host_ops.get_ro = jmicron_jmb388_get_ro;

	return 0;
}

static void jmicron_remove_slot(struct sdhci_pci_slot *slot, int dead)
{
	if (dead)
		return;

	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		jmicron_enable_mmc(slot->host, 0);
}

#ifdef CONFIG_PM_SLEEP
static int jmicron_suspend(struct sdhci_pci_chip *chip)
{
	int i, ret;

	ret = sdhci_pci_suspend_host(chip);
	if (ret)
		return ret;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		for (i = 0; i < chip->num_slots; i++)
			jmicron_enable_mmc(chip->slots[i]->host, 0);
	}

	return 0;
}

static int jmicron_resume(struct sdhci_pci_chip *chip)
{
	int ret, i;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		for (i = 0; i < chip->num_slots; i++)
			jmicron_enable_mmc(chip->slots[i]->host, 1);
	}

	ret = jmicron_pmos(chip, 1);
	if (ret) {
		dev_err(&chip->pdev->dev, "Failure enabling card power\n");
		return ret;
	}

	return sdhci_pci_resume_host(chip);
}
#endif

static const struct sdhci_pci_fixes sdhci_jmicron = {
	.probe		= jmicron_probe,

	.probe_slot	= jmicron_probe_slot,
	.remove_slot	= jmicron_remove_slot,

#ifdef CONFIG_PM_SLEEP
	.suspend	= jmicron_suspend,
	.resume		= jmicron_resume,
#endif
};

/* SysKonnect CardBus2SDIO extra registers */
#define SYSKT_CTRL		0x200
#define SYSKT_RDFIFO_STAT	0x204
#define SYSKT_WRFIFO_STAT	0x208
#define SYSKT_POWER_DATA	0x20c
#define   SYSKT_POWER_330	0xef
#define   SYSKT_POWER_300	0xf8
#define   SYSKT_POWER_184	0xcc
#define SYSKT_POWER_CMD		0x20d
#define   SYSKT_POWER_START	(1 << 7)
#define SYSKT_POWER_STATUS	0x20e
#define   SYSKT_POWER_STATUS_OK	(1 << 0)
#define SYSKT_BOARD_REV		0x210
#define SYSKT_CHIP_REV		0x211
#define SYSKT_CONF_DATA		0x212
#define   SYSKT_CONF_DATA_1V8	(1 << 2)
#define   SYSKT_CONF_DATA_2V5	(1 << 1)
#define   SYSKT_CONF_DATA_3V3	(1 << 0)

static int syskt_probe(struct sdhci_pci_chip *chip)
{
	if ((chip->pdev->class & 0x0000FF) == PCI_SDHCI_IFVENDOR) {
		chip->pdev->class &= ~0x0000FF;
		chip->pdev->class |= PCI_SDHCI_IFDMA;
	}
	return 0;
}

static int syskt_probe_slot(struct sdhci_pci_slot *slot)
{
	int tm, ps;

	u8 board_rev = readb(slot->host->ioaddr + SYSKT_BOARD_REV);
	u8  chip_rev = readb(slot->host->ioaddr + SYSKT_CHIP_REV);
	dev_info(&slot->chip->pdev->dev, "SysKonnect CardBus2SDIO, "
					 "board rev %d.%d, chip rev %d.%d\n",
					 board_rev >> 4, board_rev & 0xf,
					 chip_rev >> 4,  chip_rev & 0xf);
	if (chip_rev >= 0x20)
		slot->host->quirks |= SDHCI_QUIRK_FORCE_DMA;

	writeb(SYSKT_POWER_330, slot->host->ioaddr + SYSKT_POWER_DATA);
	writeb(SYSKT_POWER_START, slot->host->ioaddr + SYSKT_POWER_CMD);
	udelay(50);
	tm = 10;  /* Wait max 1 ms */
	do {
		ps = readw(slot->host->ioaddr + SYSKT_POWER_STATUS);
		if (ps & SYSKT_POWER_STATUS_OK)
			break;
		udelay(100);
	} while (--tm);
	if (!tm) {
		dev_err(&slot->chip->pdev->dev,
			"power regulator never stabilized");
		writeb(0, slot->host->ioaddr + SYSKT_POWER_CMD);
		return -ENODEV;
	}

	return 0;
}

static const struct sdhci_pci_fixes sdhci_syskt = {
	.quirks		= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER,
	.probe		= syskt_probe,
	.probe_slot	= syskt_probe_slot,
};

static int via_probe(struct sdhci_pci_chip *chip)
{
	if (chip->pdev->revision == 0x10)
		chip->quirks |= SDHCI_QUIRK_DELAY_AFTER_POWER;

	return 0;
}

static const struct sdhci_pci_fixes sdhci_via = {
	.probe		= via_probe,
};

static int rtsx_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps2 |= MMC_CAP2_HS200;
	return 0;
}

static const struct sdhci_pci_fixes sdhci_rtsx = {
	.quirks2	= SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_BROKEN_64_BIT_DMA |
			SDHCI_QUIRK2_BROKEN_DDR50,
	.probe_slot	= rtsx_probe_slot,
};

/*AMD chipset generation*/
enum amd_chipset_gen {
	AMD_CHIPSET_BEFORE_ML,
	AMD_CHIPSET_CZ,
	AMD_CHIPSET_NL,
	AMD_CHIPSET_UNKNOWN,
};

/* AMD registers */
#define AMD_SD_AUTO_PATTERN		0xB8
#define AMD_MSLEEP_DURATION		4
#define AMD_SD_MISC_CONTROL		0xD0
#define AMD_MAX_TUNE_VALUE		0x0B
#define AMD_AUTO_TUNE_SEL		0x10800
#define AMD_FIFO_PTR			0x30
#define AMD_BIT_MASK			0x1F

static void amd_tuning_reset(struct sdhci_host *host)
{
	unsigned int val;

	val = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	val |= SDHCI_CTRL_PRESET_VAL_ENABLE | SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, val, SDHCI_HOST_CONTROL2);

	val = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	val &= ~SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, val, SDHCI_HOST_CONTROL2);
}

static void amd_config_tuning_phase(struct pci_dev *pdev, u8 phase)
{
	unsigned int val;

	pci_read_config_dword(pdev, AMD_SD_AUTO_PATTERN, &val);
	val &= ~AMD_BIT_MASK;
	val |= (AMD_AUTO_TUNE_SEL | (phase << 1));
	pci_write_config_dword(pdev, AMD_SD_AUTO_PATTERN, val);
}

static void amd_enable_manual_tuning(struct pci_dev *pdev)
{
	unsigned int val;

	pci_read_config_dword(pdev, AMD_SD_MISC_CONTROL, &val);
	val |= AMD_FIFO_PTR;
	pci_write_config_dword(pdev, AMD_SD_MISC_CONTROL, val);
}

static int amd_execute_tuning_hs200(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct pci_dev *pdev = slot->chip->pdev;
	u8 valid_win = 0;
	u8 valid_win_max = 0;
	u8 valid_win_end = 0;
	u8 ctrl, tune_around;

	amd_tuning_reset(host);

	for (tune_around = 0; tune_around < 12; tune_around++) {
		amd_config_tuning_phase(pdev, tune_around);

		if (mmc_send_tuning(host->mmc, opcode, NULL)) {
			valid_win = 0;
			msleep(AMD_MSLEEP_DURATION);
			ctrl = SDHCI_RESET_CMD | SDHCI_RESET_DATA;
			sdhci_writeb(host, ctrl, SDHCI_SOFTWARE_RESET);
		} else if (++valid_win > valid_win_max) {
			valid_win_max = valid_win;
			valid_win_end = tune_around;
		}
	}

	if (!valid_win_max) {
		dev_err(&pdev->dev, "no tuning point found\n");
		return -EIO;
	}

	amd_config_tuning_phase(pdev, valid_win_end - valid_win_max / 2);

	amd_enable_manual_tuning(pdev);

	host->mmc->retune_period = 0;

	return 0;
}

static int amd_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);

	/* AMD requires custom HS200 tuning */
	if (host->timing == MMC_TIMING_MMC_HS200)
		return amd_execute_tuning_hs200(host, opcode);

	/* Otherwise perform standard SDHCI tuning */
	return sdhci_execute_tuning(mmc, opcode);
}

static int amd_probe_slot(struct sdhci_pci_slot *slot)
{
	struct mmc_host_ops *ops = &slot->host->mmc_host_ops;

	ops->execute_tuning = amd_execute_tuning;

	return 0;
}

static int amd_probe(struct sdhci_pci_chip *chip)
{
	struct pci_dev	*smbus_dev;
	enum amd_chipset_gen gen;

	smbus_dev = pci_get_device(PCI_VENDOR_ID_AMD,
			PCI_DEVICE_ID_AMD_HUDSON2_SMBUS, NULL);
	if (smbus_dev) {
		gen = AMD_CHIPSET_BEFORE_ML;
	} else {
		smbus_dev = pci_get_device(PCI_VENDOR_ID_AMD,
				PCI_DEVICE_ID_AMD_KERNCZ_SMBUS, NULL);
		if (smbus_dev) {
			if (smbus_dev->revision < 0x51)
				gen = AMD_CHIPSET_CZ;
			else
				gen = AMD_CHIPSET_NL;
		} else {
			gen = AMD_CHIPSET_UNKNOWN;
		}
	}

	pci_dev_put(smbus_dev);

	if (gen == AMD_CHIPSET_BEFORE_ML || gen == AMD_CHIPSET_CZ)
		chip->quirks2 |= SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD;

	return 0;
}

static u32 sdhci_read_present_state(struct sdhci_host *host)
{
	return sdhci_readl(host, SDHCI_PRESENT_STATE);
}

static void amd_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct pci_dev *pdev = slot->chip->pdev;
	u32 present_state;

	/*
	 * SDHC 0x7906 requires a hard reset to clear all internal state.
	 * Otherwise it can get into a bad state where the DATA lines are always
	 * read as zeros.
	 */
	if (pdev->device == 0x7906 && (mask & SDHCI_RESET_ALL)) {
		pci_clear_master(pdev);

		pci_save_state(pdev);

		pci_set_power_state(pdev, PCI_D3cold);
		pr_debug("%s: power_state=%u\n", mmc_hostname(host->mmc),
			pdev->current_state);
		pci_set_power_state(pdev, PCI_D0);

		pci_restore_state(pdev);

		/*
		 * SDHCI_RESET_ALL says the card detect logic should not be
		 * reset, but since we need to reset the entire controller
		 * we should wait until the card detect logic has stabilized.
		 *
		 * This normally takes about 40ms.
		 */
		readx_poll_timeout(
			sdhci_read_present_state,
			host,
			present_state,
			present_state & SDHCI_CD_STABLE,
			10000,
			100000
		);
	}

	return sdhci_reset(host, mask);
}

static const struct sdhci_ops amd_sdhci_pci_ops = {
	.set_clock			= sdhci_set_clock,
	.enable_dma			= sdhci_pci_enable_dma,
	.set_bus_width			= sdhci_set_bus_width,
	.reset				= amd_sdhci_reset,
	.set_uhs_signaling		= sdhci_set_uhs_signaling,
};

static const struct sdhci_pci_fixes sdhci_amd = {
	.probe		= amd_probe,
	.ops		= &amd_sdhci_pci_ops,
	.probe_slot	= amd_probe_slot,
};

static const struct pci_device_id pci_ids[] = {
	SDHCI_PCI_DEVICE(RICOH, R5C822,  ricoh),
	SDHCI_PCI_DEVICE(RICOH, R5C843,  ricoh_mmc),
	SDHCI_PCI_DEVICE(RICOH, R5CE822, ricoh_mmc),
	SDHCI_PCI_DEVICE(RICOH, R5CE823, ricoh_mmc),
	SDHCI_PCI_DEVICE(ENE, CB712_SD,   ene_712),
	SDHCI_PCI_DEVICE(ENE, CB712_SD_2, ene_712),
	SDHCI_PCI_DEVICE(ENE, CB714_SD,   ene_714),
	SDHCI_PCI_DEVICE(ENE, CB714_SD_2, ene_714),
	SDHCI_PCI_DEVICE(MARVELL, 88ALP01_SD, cafe),
	SDHCI_PCI_DEVICE(JMICRON, JMB38X_SD,  jmicron),
	SDHCI_PCI_DEVICE(JMICRON, JMB38X_MMC, jmicron),
	SDHCI_PCI_DEVICE(JMICRON, JMB388_SD,  jmicron),
	SDHCI_PCI_DEVICE(JMICRON, JMB388_ESD, jmicron),
	SDHCI_PCI_DEVICE(SYSKONNECT, 8000, syskt),
	SDHCI_PCI_DEVICE(VIA, 95D0, via),
	SDHCI_PCI_DEVICE(REALTEK, 5250, rtsx),
	SDHCI_PCI_DEVICE(INTEL, QRK_SD,    intel_qrk),
	SDHCI_PCI_DEVICE(INTEL, MRST_SD0,  intel_mrst_hc0),
	SDHCI_PCI_DEVICE(INTEL, MRST_SD1,  intel_mrst_hc1_hc2),
	SDHCI_PCI_DEVICE(INTEL, MRST_SD2,  intel_mrst_hc1_hc2),
	SDHCI_PCI_DEVICE(INTEL, MFD_SD,    intel_mfd_sd),
	SDHCI_PCI_DEVICE(INTEL, MFD_SDIO1, intel_mfd_sdio),
	SDHCI_PCI_DEVICE(INTEL, MFD_SDIO2, intel_mfd_sdio),
	SDHCI_PCI_DEVICE(INTEL, MFD_EMMC0, intel_mfd_emmc),
	SDHCI_PCI_DEVICE(INTEL, MFD_EMMC1, intel_mfd_emmc),
	SDHCI_PCI_DEVICE(INTEL, PCH_SDIO0, intel_pch_sdio),
	SDHCI_PCI_DEVICE(INTEL, PCH_SDIO1, intel_pch_sdio),
	SDHCI_PCI_DEVICE(INTEL, BYT_EMMC,  intel_byt_emmc),
	SDHCI_PCI_SUBDEVICE(INTEL, BYT_SDIO, NI, 7884, ni_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, BYT_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, BYT_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, BYT_EMMC2, intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, BSW_EMMC,  intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, BSW_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, BSW_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, CLV_SDIO0, intel_mfd_sd),
	SDHCI_PCI_DEVICE(INTEL, CLV_SDIO1, intel_mfd_sdio),
	SDHCI_PCI_DEVICE(INTEL, CLV_SDIO2, intel_mfd_sdio),
	SDHCI_PCI_DEVICE(INTEL, CLV_EMMC0, intel_mfd_emmc),
	SDHCI_PCI_DEVICE(INTEL, CLV_EMMC1, intel_mfd_emmc),
	SDHCI_PCI_DEVICE(INTEL, MRFLD_MMC, intel_mrfld_mmc),
	SDHCI_PCI_DEVICE(INTEL, SPT_EMMC,  intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, SPT_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, SPT_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, DNV_EMMC,  intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, CDF_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, BXT_EMMC,  intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, BXT_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, BXT_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, BXTM_EMMC, intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, BXTM_SDIO, intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, BXTM_SD,   intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, APL_EMMC,  intel_byt_emmc),
	SDHCI_PCI_DEVICE(INTEL, APL_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, APL_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, GLK_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, GLK_SDIO,  intel_byt_sdio),
	SDHCI_PCI_DEVICE(INTEL, GLK_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, CNP_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, CNP_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, CNPH_SD,   intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, ICP_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, ICP_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, EHL_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, EHL_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, CML_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, CML_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, CMLH_SD,   intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, JSL_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, JSL_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, LKF_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(INTEL, LKF_SD,    intel_byt_sd),
	SDHCI_PCI_DEVICE(INTEL, ADL_EMMC,  intel_glk_emmc),
	SDHCI_PCI_DEVICE(O2, 8120,     o2),
	SDHCI_PCI_DEVICE(O2, 8220,     o2),
	SDHCI_PCI_DEVICE(O2, 8221,     o2),
	SDHCI_PCI_DEVICE(O2, 8320,     o2),
	SDHCI_PCI_DEVICE(O2, 8321,     o2),
	SDHCI_PCI_DEVICE(O2, FUJIN2,   o2),
	SDHCI_PCI_DEVICE(O2, SDS0,     o2),
	SDHCI_PCI_DEVICE(O2, SDS1,     o2),
	SDHCI_PCI_DEVICE(O2, SEABIRD0, o2),
	SDHCI_PCI_DEVICE(O2, SEABIRD1, o2),
	SDHCI_PCI_DEVICE(O2, GG8_9860, o2),
	SDHCI_PCI_DEVICE(O2, GG8_9861, o2),
	SDHCI_PCI_DEVICE(O2, GG8_9862, o2),
	SDHCI_PCI_DEVICE(O2, GG8_9863, o2),
	SDHCI_PCI_DEVICE(ARASAN, PHY_EMMC, arasan),
	SDHCI_PCI_DEVICE(SYNOPSYS, DWC_MSHC, snps),
	SDHCI_PCI_DEVICE(GLI, 9750, gl9750),
	SDHCI_PCI_DEVICE(GLI, 9755, gl9755),
	SDHCI_PCI_DEVICE(GLI, 9763E, gl9763e),
	SDHCI_PCI_DEVICE(GLI, 9767, gl9767),
	SDHCI_PCI_DEVICE_CLASS(AMD, SYSTEM_SDHCI, PCI_CLASS_MASK, amd),
	/* Generic SD host controller */
	{PCI_DEVICE_CLASS(SYSTEM_SDHCI, PCI_CLASS_MASK)},
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(pci, pci_ids);

/*****************************************************************************\
 *                                                                           *
 * SDHCI core callbacks                                                      *
 *                                                                           *
\*****************************************************************************/

int sdhci_pci_enable_dma(struct sdhci_host *host)
{
	struct sdhci_pci_slot *slot;
	struct pci_dev *pdev;

	slot = sdhci_priv(host);
	pdev = slot->chip->pdev;

	if (((pdev->class & 0xFFFF00) == (PCI_CLASS_SYSTEM_SDHCI << 8)) &&
		((pdev->class & 0x0000FF) != PCI_SDHCI_IFDMA) &&
		(host->flags & SDHCI_USE_SDMA)) {
		dev_warn(&pdev->dev, "Will use DMA mode even though HW "
			"doesn't fully claim to support it.\n");
	}

	pci_set_master(pdev);

	return 0;
}

static void sdhci_pci_hw_reset(struct sdhci_host *host)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);

	if (slot->hw_reset)
		slot->hw_reset(host);
}

static const struct sdhci_ops sdhci_pci_ops = {
	.set_clock	= sdhci_set_clock,
	.enable_dma	= sdhci_pci_enable_dma,
	.set_bus_width	= sdhci_set_bus_width,
	.reset		= sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.hw_reset		= sdhci_pci_hw_reset,
};

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM_SLEEP
static int sdhci_pci_suspend(struct device *dev)
{
	struct sdhci_pci_chip *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	if (chip->fixes && chip->fixes->suspend)
		return chip->fixes->suspend(chip);

	return sdhci_pci_suspend_host(chip);
}

static int sdhci_pci_resume(struct device *dev)
{
	struct sdhci_pci_chip *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	if (chip->fixes && chip->fixes->resume)
		return chip->fixes->resume(chip);

	return sdhci_pci_resume_host(chip);
}
#endif

#ifdef CONFIG_PM
static int sdhci_pci_runtime_suspend(struct device *dev)
{
	struct sdhci_pci_chip *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	if (chip->fixes && chip->fixes->runtime_suspend)
		return chip->fixes->runtime_suspend(chip);

	return sdhci_pci_runtime_suspend_host(chip);
}

static int sdhci_pci_runtime_resume(struct device *dev)
{
	struct sdhci_pci_chip *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	if (chip->fixes && chip->fixes->runtime_resume)
		return chip->fixes->runtime_resume(chip);

	return sdhci_pci_runtime_resume_host(chip);
}
#endif

static const struct dev_pm_ops sdhci_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_pci_suspend, sdhci_pci_resume)
	SET_RUNTIME_PM_OPS(sdhci_pci_runtime_suspend,
			sdhci_pci_runtime_resume, NULL)
};

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static struct gpiod_lookup_table *sdhci_pci_add_gpio_lookup_table(
	struct sdhci_pci_chip *chip)
{
	struct gpiod_lookup_table *cd_gpio_lookup_table;
	const struct dmi_system_id *dmi_id = NULL;
	size_t count;

	if (chip->fixes && chip->fixes->cd_gpio_override)
		dmi_id = dmi_first_match(chip->fixes->cd_gpio_override);

	if (!dmi_id)
		return NULL;

	cd_gpio_lookup_table = dmi_id->driver_data;
	for (count = 0; cd_gpio_lookup_table->table[count].key; count++)
		;

	cd_gpio_lookup_table = kmemdup(dmi_id->driver_data,
				       /* count + 1 terminating entry */
				       struct_size(cd_gpio_lookup_table, table, count + 1),
				       GFP_KERNEL);
	if (!cd_gpio_lookup_table)
		return ERR_PTR(-ENOMEM);

	gpiod_add_lookup_table(cd_gpio_lookup_table);
	return cd_gpio_lookup_table;
}

static void sdhci_pci_remove_gpio_lookup_table(struct gpiod_lookup_table *lookup_table)
{
	if (lookup_table) {
		gpiod_remove_lookup_table(lookup_table);
		kfree(lookup_table);
	}
}

static struct sdhci_pci_slot *sdhci_pci_probe_slot(
	struct pci_dev *pdev, struct sdhci_pci_chip *chip, int first_bar,
	int slotno)
{
	struct sdhci_pci_slot *slot;
	struct sdhci_host *host;
	int ret, bar = first_bar + slotno;
	size_t priv_size = chip->fixes ? chip->fixes->priv_size : 0;

	if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR %d is not iomem. Aborting.\n", bar);
		return ERR_PTR(-ENODEV);
	}

	if (pci_resource_len(pdev, bar) < 0x100) {
		dev_err(&pdev->dev, "Invalid iomem size. You may "
			"experience problems.\n");
	}

	if ((pdev->class & 0x0000FF) == PCI_SDHCI_IFVENDOR) {
		dev_err(&pdev->dev, "Vendor specific interface. Aborting.\n");
		return ERR_PTR(-ENODEV);
	}

	if ((pdev->class & 0x0000FF) > PCI_SDHCI_IFVENDOR) {
		dev_err(&pdev->dev, "Unknown interface. Aborting.\n");
		return ERR_PTR(-ENODEV);
	}

	host = sdhci_alloc_host(&pdev->dev, sizeof(*slot) + priv_size);
	if (IS_ERR(host)) {
		dev_err(&pdev->dev, "cannot allocate host\n");
		return ERR_CAST(host);
	}

	slot = sdhci_priv(host);

	slot->chip = chip;
	slot->host = host;
	slot->cd_idx = -1;

	host->hw_name = "PCI";
	host->ops = chip->fixes && chip->fixes->ops ?
		    chip->fixes->ops :
		    &sdhci_pci_ops;
	host->quirks = chip->quirks;
	host->quirks2 = chip->quirks2;

	host->irq = pdev->irq;

	ret = pcim_iomap_regions(pdev, BIT(bar), mmc_hostname(host->mmc));
	if (ret) {
		dev_err(&pdev->dev, "cannot request region\n");
		goto cleanup;
	}

	host->ioaddr = pcim_iomap_table(pdev)[bar];

	if (chip->fixes && chip->fixes->probe_slot) {
		ret = chip->fixes->probe_slot(slot);
		if (ret)
			goto cleanup;
	}

	host->mmc->pm_caps = MMC_PM_KEEP_POWER;
	host->mmc->slotno = slotno;
	host->mmc->caps2 |= MMC_CAP2_NO_PRESCAN_POWERUP;

	if (device_can_wakeup(&pdev->dev))
		host->mmc->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;

	if (host->mmc->caps & MMC_CAP_CD_WAKE)
		device_init_wakeup(&pdev->dev, true);

	if (slot->cd_idx >= 0) {
		struct gpiod_lookup_table *cd_gpio_lookup_table;

		cd_gpio_lookup_table = sdhci_pci_add_gpio_lookup_table(chip);
		if (IS_ERR(cd_gpio_lookup_table)) {
			ret = PTR_ERR(cd_gpio_lookup_table);
			goto remove;
		}

		ret = mmc_gpiod_request_cd(host->mmc, "cd", slot->cd_idx,
					   slot->cd_override_level, 0);

		sdhci_pci_remove_gpio_lookup_table(cd_gpio_lookup_table);

		if (ret && ret != -EPROBE_DEFER)
			ret = mmc_gpiod_request_cd(host->mmc, NULL,
						   slot->cd_idx,
						   slot->cd_override_level,
						   0);
		if (ret == -EPROBE_DEFER)
			goto remove;

		if (ret) {
			dev_warn(&pdev->dev, "failed to setup card detect gpio\n");
			slot->cd_idx = -1;
		}
	}

	if (chip->fixes && chip->fixes->add_host)
		ret = chip->fixes->add_host(slot);
	else
		ret = sdhci_add_host(host);
	if (ret)
		goto remove;

	/*
	 * Check if the chip needs a separate GPIO for card detect to wake up
	 * from runtime suspend.  If it is not there, don't allow runtime PM.
	 */
	if (chip->fixes && chip->fixes->own_cd_for_runtime_pm && slot->cd_idx < 0)
		chip->allow_runtime_pm = false;

	return slot;

remove:
	if (chip->fixes && chip->fixes->remove_slot)
		chip->fixes->remove_slot(slot, 0);

cleanup:
	sdhci_free_host(host);

	return ERR_PTR(ret);
}

static void sdhci_pci_remove_slot(struct sdhci_pci_slot *slot)
{
	int dead;
	u32 scratch;

	dead = 0;
	scratch = readl(slot->host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	if (slot->chip->fixes && slot->chip->fixes->remove_host)
		slot->chip->fixes->remove_host(slot, dead);
	else
		sdhci_remove_host(slot->host, dead);

	if (slot->chip->fixes && slot->chip->fixes->remove_slot)
		slot->chip->fixes->remove_slot(slot, dead);

	sdhci_free_host(slot->host);
}

int sdhci_pci_uhs2_add_host(struct sdhci_pci_slot *slot)
{
	return sdhci_uhs2_add_host(slot->host);
}

void sdhci_pci_uhs2_remove_host(struct sdhci_pci_slot *slot, int dead)
{
	sdhci_uhs2_remove_host(slot->host, dead);
}

static void sdhci_pci_runtime_pm_allow(struct device *dev)
{
	pm_suspend_ignore_children(dev, 1);
	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);
	/* Stay active until mmc core scans for a card */
	pm_runtime_put_noidle(dev);
}

static void sdhci_pci_runtime_pm_forbid(struct device *dev)
{
	pm_runtime_forbid(dev);
	pm_runtime_get_noresume(dev);
}

static int sdhci_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;

	u8 slots, first_bar;
	int ret, i;

	BUG_ON(pdev == NULL);
	BUG_ON(ent == NULL);

	dev_info(&pdev->dev, "SDHCI controller found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &slots);
	if (ret)
		return pcibios_err_to_errno(ret);

	slots = PCI_SLOT_INFO_SLOTS(slots) + 1;
	dev_dbg(&pdev->dev, "found %d slot(s)\n", slots);

	BUG_ON(slots > MAX_SLOTS);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &first_bar);
	if (ret)
		return pcibios_err_to_errno(ret);

	first_bar &= PCI_SLOT_INFO_FIRST_BAR_MASK;

	if (first_bar > 5) {
		dev_err(&pdev->dev, "Invalid first BAR. Aborting.\n");
		return -ENODEV;
	}

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->pdev = pdev;
	chip->fixes = (const struct sdhci_pci_fixes *)ent->driver_data;
	if (chip->fixes) {
		chip->quirks = chip->fixes->quirks;
		chip->quirks2 = chip->fixes->quirks2;
		chip->allow_runtime_pm = chip->fixes->allow_runtime_pm;
	}
	chip->num_slots = slots;
	chip->pm_retune = true;
	chip->rpm_retune = true;

	pci_set_drvdata(pdev, chip);

	if (chip->fixes && chip->fixes->probe) {
		ret = chip->fixes->probe(chip);
		if (ret)
			return ret;
	}

	slots = chip->num_slots;	/* Quirk may have changed this */

	for (i = 0; i < slots; i++) {
		slot = sdhci_pci_probe_slot(pdev, chip, first_bar, i);
		if (IS_ERR(slot)) {
			for (i--; i >= 0; i--)
				sdhci_pci_remove_slot(chip->slots[i]);
			return PTR_ERR(slot);
		}

		chip->slots[i] = slot;
	}

	if (chip->allow_runtime_pm)
		sdhci_pci_runtime_pm_allow(&pdev->dev);

	return 0;
}

static void sdhci_pci_remove(struct pci_dev *pdev)
{
	int i;
	struct sdhci_pci_chip *chip = pci_get_drvdata(pdev);

	if (chip->allow_runtime_pm)
		sdhci_pci_runtime_pm_forbid(&pdev->dev);

	for (i = 0; i < chip->num_slots; i++)
		sdhci_pci_remove_slot(chip->slots[i]);
}

static struct pci_driver sdhci_driver = {
	.name =		"sdhci-pci",
	.id_table =	pci_ids,
	.probe =	sdhci_pci_probe,
	.remove =	sdhci_pci_remove,
	.driver =	{
		.pm =   &sdhci_pci_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_pci_driver(sdhci_driver);

MODULE_AUTHOR("Pierre Ossman <pierre@ossman.eu>");
MODULE_DESCRIPTION("Secure Digital Host Controller Interface PCI driver");
MODULE_LICENSE("GPL");
