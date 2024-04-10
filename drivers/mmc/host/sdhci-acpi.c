// SPDX-License-Identifier: GPL-2.0-only
/*
 * Secure Digital Host Controller Interface ACPI driver.
 *
 * Copyright (c) 2012, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/dmi.h>

#include <linux/mmc/host.h>
#include <linux/mmc/pm.h>
#include <linux/mmc/slot-gpio.h>

#ifdef CONFIG_X86
#include <linux/platform_data/x86/soc.h>
#include <asm/iosf_mbi.h>
#endif

#include "sdhci.h"

enum {
	SDHCI_ACPI_SD_CD		= BIT(0),
	SDHCI_ACPI_RUNTIME_PM		= BIT(1),
	SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL	= BIT(2),
};

struct sdhci_acpi_chip {
	const struct	sdhci_ops *ops;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
};

struct sdhci_acpi_slot {
	const struct	sdhci_acpi_chip *chip;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
	unsigned int	flags;
	size_t		priv_size;
	int (*probe_slot)(struct platform_device *, struct acpi_device *);
	int (*remove_slot)(struct platform_device *);
	int (*free_slot)(struct platform_device *pdev);
	int (*setup_host)(struct platform_device *pdev);
};

struct sdhci_acpi_host {
	struct sdhci_host		*host;
	const struct sdhci_acpi_slot	*slot;
	struct platform_device		*pdev;
	bool				use_runtime_pm;
	bool				is_intel;
	bool				reset_signal_volt_on_suspend;
	unsigned long			private[] ____cacheline_aligned;
};

enum {
	DMI_QUIRK_RESET_SD_SIGNAL_VOLT_ON_SUSP			= BIT(0),
	DMI_QUIRK_SD_NO_WRITE_PROTECT				= BIT(1),
	DMI_QUIRK_SD_CD_ACTIVE_HIGH				= BIT(2),
};

static inline void *sdhci_acpi_priv(struct sdhci_acpi_host *c)
{
	return (void *)c->private;
}

static inline bool sdhci_acpi_flag(struct sdhci_acpi_host *c, unsigned int flag)
{
	return c->slot && (c->slot->flags & flag);
}

#define INTEL_DSM_HS_CAPS_SDR25		BIT(0)
#define INTEL_DSM_HS_CAPS_DDR50		BIT(1)
#define INTEL_DSM_HS_CAPS_SDR50		BIT(2)
#define INTEL_DSM_HS_CAPS_SDR104	BIT(3)

enum {
	INTEL_DSM_FNS		=  0,
	INTEL_DSM_V18_SWITCH	=  3,
	INTEL_DSM_V33_SWITCH	=  4,
	INTEL_DSM_HS_CAPS	=  8,
};

struct intel_host {
	u32	dsm_fns;
	u32	hs_caps;
};

static const guid_t intel_dsm_guid =
	GUID_INIT(0xF6C13EA5, 0x65CD, 0x461F,
		  0xAB, 0x7A, 0x29, 0xF7, 0xE8, 0xD5, 0xBD, 0x61);

static int __intel_dsm(struct intel_host *intel_host, struct device *dev,
		       unsigned int fn, u32 *result)
{
	union acpi_object *obj;
	int err = 0;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &intel_dsm_guid, 0, fn, NULL);
	if (!obj)
		return -EOPNOTSUPP;

	if (obj->type == ACPI_TYPE_INTEGER) {
		*result = obj->integer.value;
	} else if (obj->type == ACPI_TYPE_BUFFER && obj->buffer.length > 0) {
		size_t len = min_t(size_t, obj->buffer.length, 4);

		*result = 0;
		memcpy(result, obj->buffer.pointer, len);
	} else {
		dev_err(dev, "%s DSM fn %u obj->type %d obj->buffer.length %d\n",
			__func__, fn, obj->type, obj->buffer.length);
		err = -EINVAL;
	}

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

	intel_host->hs_caps = ~0;

	err = __intel_dsm(intel_host, dev, INTEL_DSM_FNS, &intel_host->dsm_fns);
	if (err) {
		pr_debug("%s: DSM not supported, error %d\n",
			 mmc_hostname(mmc), err);
		return;
	}

	pr_debug("%s: DSM function mask %#x\n",
		 mmc_hostname(mmc), intel_host->dsm_fns);

	intel_dsm(intel_host, dev, INTEL_DSM_HS_CAPS, &intel_host->hs_caps);
}

static int intel_start_signal_voltage_switch(struct mmc_host *mmc,
					     struct mmc_ios *ios)
{
	struct device *dev = mmc_dev(mmc);
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);
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

static void sdhci_acpi_int_hw_reset(struct sdhci_host *host)
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

static const struct sdhci_ops sdhci_acpi_ops_dflt = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_acpi_ops_int = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.hw_reset   = sdhci_acpi_int_hw_reset,
};

static const struct sdhci_acpi_chip sdhci_acpi_chip_int = {
	.ops = &sdhci_acpi_ops_int,
};

#ifdef CONFIG_X86

#define BYT_IOSF_SCCEP			0x63
#define BYT_IOSF_OCP_NETCTRL0		0x1078
#define BYT_IOSF_OCP_TIMEOUT_BASE	GENMASK(10, 8)

static void sdhci_acpi_byt_setting(struct device *dev)
{
	u32 val = 0;

	if (!soc_intel_is_byt())
		return;

	if (iosf_mbi_read(BYT_IOSF_SCCEP, MBI_CR_READ, BYT_IOSF_OCP_NETCTRL0,
			  &val)) {
		dev_err(dev, "%s read error\n", __func__);
		return;
	}

	if (!(val & BYT_IOSF_OCP_TIMEOUT_BASE))
		return;

	val &= ~BYT_IOSF_OCP_TIMEOUT_BASE;

	if (iosf_mbi_write(BYT_IOSF_SCCEP, MBI_CR_WRITE, BYT_IOSF_OCP_NETCTRL0,
			   val)) {
		dev_err(dev, "%s write error\n", __func__);
		return;
	}

	dev_dbg(dev, "%s completed\n", __func__);
}

static bool sdhci_acpi_byt_defer(struct device *dev)
{
	if (!soc_intel_is_byt())
		return false;

	if (!iosf_mbi_available())
		return true;

	sdhci_acpi_byt_setting(dev);

	return false;
}

#else

static inline void sdhci_acpi_byt_setting(struct device *dev)
{
}

static inline bool sdhci_acpi_byt_defer(struct device *dev)
{
	return false;
}

#endif

static int bxt_get_cd(struct mmc_host *mmc)
{
	int gpio_cd = mmc_gpio_get_cd(mmc);

	if (!gpio_cd)
		return 0;

	return sdhci_get_cd_nogpio(mmc);
}

static int intel_probe_slot(struct platform_device *pdev, struct acpi_device *adev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);
	struct sdhci_host *host = c->host;

	if (acpi_dev_hid_uid_match(adev, "80860F14", "1") &&
	    sdhci_readl(host, SDHCI_CAPABILITIES) == 0x446cc8b2 &&
	    sdhci_readl(host, SDHCI_CAPABILITIES_1) == 0x00000807)
		host->timeout_clk = 1000; /* 1000 kHz i.e. 1 MHz */

	if (acpi_dev_hid_uid_match(adev, "80865ACA", NULL))
		host->mmc_host_ops.get_cd = bxt_get_cd;

	intel_dsm_init(intel_host, &pdev->dev, host->mmc);

	host->mmc_host_ops.start_signal_voltage_switch =
					intel_start_signal_voltage_switch;

	c->is_intel = true;

	return 0;
}

static int intel_setup_host(struct platform_device *pdev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR25))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR25;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR50))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR50;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_DDR50))
		c->host->mmc->caps &= ~MMC_CAP_UHS_DDR50;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR104))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR104;

	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_emmc = {
	.chip    = &sdhci_acpi_chip_int,
	.caps    = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE |
		   MMC_CAP_HW_RESET | MMC_CAP_1_8V_DDR |
		   MMC_CAP_CMD_DURING_TFR | MMC_CAP_WAIT_WHILE_BUSY,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.quirks  = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		   SDHCI_QUIRK_NO_LED,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_STOP_WITH_TC |
		   SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sdio = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		   SDHCI_QUIRK_NO_LED |
		   SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.caps    = MMC_CAP_NONREMOVABLE | MMC_CAP_POWER_OFF_CARD |
		   MMC_CAP_WAIT_WHILE_BUSY,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.pm_caps = MMC_PM_KEEP_POWER,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sd = {
	.flags   = SDHCI_ACPI_SD_CD | SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL |
		   SDHCI_ACPI_RUNTIME_PM,
	.quirks  = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		   SDHCI_QUIRK_NO_LED,
	.quirks2 = SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON |
		   SDHCI_QUIRK2_STOP_WITH_TC,
	.caps    = MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_AGGRESSIVE_PM,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

#define VENDOR_SPECIFIC_PWRCTL_CLEAR_REG	0x1a8
#define VENDOR_SPECIFIC_PWRCTL_CTL_REG		0x1ac
static irqreturn_t sdhci_acpi_qcom_handler(int irq, void *ptr)
{
	struct sdhci_host *host = ptr;

	sdhci_writel(host, 0x3, VENDOR_SPECIFIC_PWRCTL_CLEAR_REG);
	sdhci_writel(host, 0x1, VENDOR_SPECIFIC_PWRCTL_CTL_REG);

	return IRQ_HANDLED;
}

static int qcom_probe_slot(struct platform_device *pdev, struct acpi_device *adev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host = c->host;
	int *irq = sdhci_acpi_priv(c);

	*irq = -EINVAL;

	if (!acpi_dev_hid_uid_match(adev, "QCOM8051", NULL))
		return 0;

	*irq = platform_get_irq(pdev, 1);
	if (*irq < 0)
		return 0;

	return request_threaded_irq(*irq, NULL, sdhci_acpi_qcom_handler,
				    IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				    "sdhci_qcom", host);
}

static int qcom_free_slot(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host = c->host;
	struct acpi_device *adev;
	int *irq = sdhci_acpi_priv(c);

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	if (!acpi_dev_hid_uid_match(adev, "QCOM8051", NULL))
		return 0;

	if (*irq < 0)
		return 0;

	free_irq(*irq, host);
	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_qcom_sd_3v = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.quirks2 = SDHCI_QUIRK2_NO_1_8_V,
	.caps    = MMC_CAP_NONREMOVABLE,
	.priv_size	= sizeof(int),
	.probe_slot	= qcom_probe_slot,
	.free_slot	= qcom_free_slot,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_qcom_sd = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.caps    = MMC_CAP_NONREMOVABLE,
};

struct amd_sdhci_host {
	bool	tuned_clock;
	bool	dll_enabled;
};

/* AMD sdhci reset dll register. */
#define SDHCI_AMD_RESET_DLL_REGISTER    0x908

static int amd_select_drive_strength(struct mmc_card *card,
				     unsigned int max_dtr, int host_drv,
				     int card_drv, int *host_driver_strength)
{
	struct sdhci_host *host = mmc_priv(card->host);
	u16 preset, preset_driver_strength;

	/*
	 * This method is only called by mmc_select_hs200 so we only need to
	 * read from the HS200 (SDR104) preset register.
	 *
	 * Firmware that has "invalid/default" presets return a driver strength
	 * of A. This matches the previously hard coded value.
	 */
	preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR104);
	preset_driver_strength = FIELD_GET(SDHCI_PRESET_DRV_MASK, preset);

	/*
	 * We want the controller driver strength to match the card's driver
	 * strength so they have similar rise/fall times.
	 *
	 * The controller driver strength set by this method is sticky for all
	 * timings after this method is called. This unfortunately means that
	 * while HS400 tuning is in progress we end up with mismatched driver
	 * strengths between the controller and the card. HS400 tuning requires
	 * switching from HS400->DDR52->HS->HS200->HS400. So the driver mismatch
	 * happens while in DDR52 and HS modes. This has not been observed to
	 * cause problems. Enabling presets would fix this issue.
	 */
	*host_driver_strength = preset_driver_strength;

	/*
	 * The resulting card driver strength is only set when switching the
	 * card's timing to HS200 or HS400. The card will use the default driver
	 * strength (B) for any other mode.
	 */
	return preset_driver_strength;
}

static void sdhci_acpi_amd_hs400_dll(struct sdhci_host *host, bool enable)
{
	struct sdhci_acpi_host *acpi_host = sdhci_priv(host);
	struct amd_sdhci_host *amd_host = sdhci_acpi_priv(acpi_host);

	/* AMD Platform requires dll setting */
	sdhci_writel(host, 0x40003210, SDHCI_AMD_RESET_DLL_REGISTER);
	usleep_range(10, 20);
	if (enable)
		sdhci_writel(host, 0x40033210, SDHCI_AMD_RESET_DLL_REGISTER);

	amd_host->dll_enabled = enable;
}

/*
 * The initialization sequence for HS400 is:
 *     HS->HS200->Perform Tuning->HS->HS400
 *
 * The re-tuning sequence is:
 *     HS400->DDR52->HS->HS200->Perform Tuning->HS->HS400
 *
 * The AMD eMMC Controller can only use the tuned clock while in HS200 and HS400
 * mode. If we switch to a different mode, we need to disable the tuned clock.
 * If we have previously performed tuning and switch back to HS200 or
 * HS400, we can re-enable the tuned clock.
 *
 */
static void amd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_acpi_host *acpi_host = sdhci_priv(host);
	struct amd_sdhci_host *amd_host = sdhci_acpi_priv(acpi_host);
	unsigned int old_timing = host->timing;
	u16 val;

	sdhci_set_ios(mmc, ios);

	if (old_timing != host->timing && amd_host->tuned_clock) {
		if (host->timing == MMC_TIMING_MMC_HS400 ||
		    host->timing == MMC_TIMING_MMC_HS200) {
			val = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			val |= SDHCI_CTRL_TUNED_CLK;
			sdhci_writew(host, val, SDHCI_HOST_CONTROL2);
		} else {
			val = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			val &= ~SDHCI_CTRL_TUNED_CLK;
			sdhci_writew(host, val, SDHCI_HOST_CONTROL2);
		}

		/* DLL is only required for HS400 */
		if (host->timing == MMC_TIMING_MMC_HS400 &&
		    !amd_host->dll_enabled)
			sdhci_acpi_amd_hs400_dll(host, true);
	}
}

static int amd_sdhci_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	int err;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_acpi_host *acpi_host = sdhci_priv(host);
	struct amd_sdhci_host *amd_host = sdhci_acpi_priv(acpi_host);

	amd_host->tuned_clock = false;

	err = sdhci_execute_tuning(mmc, opcode);

	if (!err && !host->tuning_err)
		amd_host->tuned_clock = true;

	return err;
}

static void amd_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_acpi_host *acpi_host = sdhci_priv(host);
	struct amd_sdhci_host *amd_host = sdhci_acpi_priv(acpi_host);

	if (mask & SDHCI_RESET_ALL) {
		amd_host->tuned_clock = false;
		sdhci_acpi_amd_hs400_dll(host, false);
	}

	sdhci_reset(host, mask);
}

static const struct sdhci_ops sdhci_acpi_ops_amd = {
	.set_clock	= sdhci_set_clock,
	.set_bus_width	= sdhci_set_bus_width,
	.reset		= amd_sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_acpi_chip sdhci_acpi_chip_amd = {
	.ops = &sdhci_acpi_ops_amd,
};

static int sdhci_acpi_emmc_amd_probe_slot(struct platform_device *pdev,
					  struct acpi_device *adev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host   = c->host;

	sdhci_read_caps(host);
	if (host->caps1 & SDHCI_SUPPORT_DDR50)
		host->mmc->caps = MMC_CAP_1_8V_DDR;

	if ((host->caps1 & SDHCI_SUPPORT_SDR104) &&
	    (host->mmc->caps & MMC_CAP_1_8V_DDR))
		host->mmc->caps2 = MMC_CAP2_HS400_1_8V;

	/*
	 * There are two types of presets out in the wild:
	 * 1) Default/broken presets.
	 *    These presets have two sets of problems:
	 *    a) The clock divisor for SDR12, SDR25, and SDR50 is too small.
	 *       This results in clock frequencies that are 2x higher than
	 *       acceptable. i.e., SDR12 = 25 MHz, SDR25 = 50 MHz, SDR50 =
	 *       100 MHz.x
	 *    b) The HS200 and HS400 driver strengths don't match.
	 *       By default, the SDR104 preset register has a driver strength of
	 *       A, but the (internal) HS400 preset register has a driver
	 *       strength of B. As part of initializing HS400, HS200 tuning
	 *       needs to be performed. Having different driver strengths
	 *       between tuning and operation is wrong. It results in different
	 *       rise/fall times that lead to incorrect sampling.
	 * 2) Firmware with properly initialized presets.
	 *    These presets have proper clock divisors. i.e., SDR12 => 12MHz,
	 *    SDR25 => 25 MHz, SDR50 => 50 MHz. Additionally the HS200 and
	 *    HS400 preset driver strengths match.
	 *
	 *    Enabling presets for HS400 doesn't work for the following reasons:
	 *    1) sdhci_set_ios has a hard coded list of timings that are used
	 *       to determine if presets should be enabled.
	 *    2) sdhci_get_preset_value is using a non-standard register to
	 *       read out HS400 presets. The AMD controller doesn't support this
	 *       non-standard register. In fact, it doesn't expose the HS400
	 *       preset register anywhere in the SDHCI memory map. This results
	 *       in reading a garbage value and using the wrong presets.
	 *
	 *       Since HS400 and HS200 presets must be identical, we could
	 *       instead use the SDR104 preset register.
	 *
	 *    If the above issues are resolved we could remove this quirk for
	 *    firmware that has valid presets (i.e., SDR12 <= 12 MHz).
	 */
	host->quirks2 |= SDHCI_QUIRK2_PRESET_VALUE_BROKEN;

	host->mmc_host_ops.select_drive_strength = amd_select_drive_strength;
	host->mmc_host_ops.set_ios = amd_set_ios;
	host->mmc_host_ops.execute_tuning = amd_sdhci_execute_tuning;
	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_amd_emmc = {
	.chip		= &sdhci_acpi_chip_amd,
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_32BIT_ADMA_SIZE,
	.quirks2	= SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
	.probe_slot     = sdhci_acpi_emmc_amd_probe_slot,
	.priv_size	= sizeof(struct amd_sdhci_host),
};

struct sdhci_acpi_uid_slot {
	const char *hid;
	const char *uid;
	const struct sdhci_acpi_slot *slot;
};

static const struct sdhci_acpi_uid_slot sdhci_acpi_uids[] = {
	{ "80865ACA", NULL, &sdhci_acpi_slot_int_sd },
	{ "80865ACC", NULL, &sdhci_acpi_slot_int_emmc },
	{ "80865AD0", NULL, &sdhci_acpi_slot_int_sdio },
	{ "80860F14" , "1" , &sdhci_acpi_slot_int_emmc },
	{ "80860F14" , "2" , &sdhci_acpi_slot_int_sdio },
	{ "80860F14" , "3" , &sdhci_acpi_slot_int_sd   },
	{ "80860F16" , NULL, &sdhci_acpi_slot_int_sd   },
	{ "INT33BB"  , "2" , &sdhci_acpi_slot_int_sdio },
	{ "INT33BB"  , "3" , &sdhci_acpi_slot_int_sd },
	{ "INT33C6"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "INT3436"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "INT344D"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "PNP0FFF"  , "3" , &sdhci_acpi_slot_int_sd   },
	{ "PNP0D40"  },
	{ "QCOM8051", NULL, &sdhci_acpi_slot_qcom_sd_3v },
	{ "QCOM8052", NULL, &sdhci_acpi_slot_qcom_sd },
	{ "AMDI0040", NULL, &sdhci_acpi_slot_amd_emmc },
	{ "AMDI0041", NULL, &sdhci_acpi_slot_amd_emmc },
	{ },
};

static const struct acpi_device_id sdhci_acpi_ids[] = {
	{ "80865ACA" },
	{ "80865ACC" },
	{ "80865AD0" },
	{ "80860F14" },
	{ "80860F16" },
	{ "INT33BB"  },
	{ "INT33C6"  },
	{ "INT3436"  },
	{ "INT344D"  },
	{ "PNP0D40"  },
	{ "QCOM8051" },
	{ "QCOM8052" },
	{ "AMDI0040" },
	{ "AMDI0041" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, sdhci_acpi_ids);

/* Please keep this list sorted alphabetically */
static const struct dmi_system_id sdhci_acpi_quirks[] = {
	{
		/*
		 * The Acer Aspire Switch 10 (SW5-012) microSD slot always
		 * reports the card being write-protected even though microSD
		 * cards do not have a write-protect switch at all.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire SW5-012"),
		},
		.driver_data = (void *)DMI_QUIRK_SD_NO_WRITE_PROTECT,
	},
	{
		/*
		 * The Lenovo Miix 320-10ICR has a bug in the _PS0 method of
		 * the SHC1 ACPI device, this bug causes it to reprogram the
		 * wrong LDO (DLDO3) to 1.8V if 1.8V modes are used and the
		 * card is (runtime) suspended + resumed. DLDO3 is used for
		 * the LCD and setting it to 1.8V causes the LCD to go black.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo MIIX 320-10ICR"),
		},
		.driver_data = (void *)DMI_QUIRK_RESET_SD_SIGNAL_VOLT_ON_SUSP,
	},
	{
		/*
		 * Lenovo Yoga Tablet 2 Pro 1380F/L (13" Android version) this
		 * has broken WP reporting and an inverted CD signal.
		 * Note this has more or less the same BIOS as the Lenovo Yoga
		 * Tablet 2 830F/L or 1050F/L (8" and 10" Android), but unlike
		 * the 830 / 1050 models which share the same mainboard this
		 * model has a different mainboard and the inverted CD and
		 * broken WP are unique to this board.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
			/* Full match so as to NOT match the 830/1050 BIOS */
			DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21.X64.0005.R00.1504101516"),
		},
		.driver_data = (void *)(DMI_QUIRK_SD_NO_WRITE_PROTECT |
					DMI_QUIRK_SD_CD_ACTIVE_HIGH),
	},
	{
		/*
		 * The Toshiba WT8-B's microSD slot always reports the card being
		 * write-protected.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TOSHIBA ENCORE 2 WT8-B"),
		},
		.driver_data = (void *)DMI_QUIRK_SD_NO_WRITE_PROTECT,
	},
	{
		/*
		 * The Toshiba WT10-A's microSD slot always reports the card being
		 * write-protected.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TOSHIBA WT10-A"),
		},
		.driver_data = (void *)DMI_QUIRK_SD_NO_WRITE_PROTECT,
	},
	{} /* Terminating entry */
};

static const struct sdhci_acpi_slot *sdhci_acpi_get_slot(struct acpi_device *adev)
{
	const struct sdhci_acpi_uid_slot *u;

	for (u = sdhci_acpi_uids; u->hid; u++) {
		if (acpi_dev_hid_uid_match(adev, u->hid, u->uid))
			return u->slot;
	}
	return NULL;
}

static int sdhci_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sdhci_acpi_slot *slot;
	const struct dmi_system_id *id;
	struct acpi_device *device;
	struct sdhci_acpi_host *c;
	struct sdhci_host *host;
	struct resource *iomem;
	resource_size_t len;
	size_t priv_size;
	int quirks = 0;
	int err;

	device = ACPI_COMPANION(dev);
	if (!device)
		return -ENODEV;

	id = dmi_first_match(sdhci_acpi_quirks);
	if (id)
		quirks = (long)id->driver_data;

	slot = sdhci_acpi_get_slot(device);

	/* Power on the SDHCI controller and its children */
	acpi_device_fix_up_power_extended(device);

	if (sdhci_acpi_byt_defer(dev))
		return -EPROBE_DEFER;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -ENOMEM;

	len = resource_size(iomem);
	if (len < 0x100)
		dev_err(dev, "Invalid iomem size!\n");

	if (!devm_request_mem_region(dev, iomem->start, len, dev_name(dev)))
		return -ENOMEM;

	priv_size = slot ? slot->priv_size : 0;
	host = sdhci_alloc_host(dev, sizeof(struct sdhci_acpi_host) + priv_size);
	if (IS_ERR(host))
		return PTR_ERR(host);

	c = sdhci_priv(host);
	c->host = host;
	c->slot = slot;
	c->pdev = pdev;
	c->use_runtime_pm = sdhci_acpi_flag(c, SDHCI_ACPI_RUNTIME_PM);

	platform_set_drvdata(pdev, c);

	host->hw_name	= "ACPI";
	host->ops	= &sdhci_acpi_ops_dflt;
	host->irq	= platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		err = host->irq;
		goto err_free;
	}

	host->ioaddr = devm_ioremap(dev, iomem->start,
					    resource_size(iomem));
	if (host->ioaddr == NULL) {
		err = -ENOMEM;
		goto err_free;
	}

	if (c->slot) {
		if (c->slot->probe_slot) {
			err = c->slot->probe_slot(pdev, device);
			if (err)
				goto err_free;
		}
		if (c->slot->chip) {
			host->ops            = c->slot->chip->ops;
			host->quirks        |= c->slot->chip->quirks;
			host->quirks2       |= c->slot->chip->quirks2;
			host->mmc->caps     |= c->slot->chip->caps;
			host->mmc->caps2    |= c->slot->chip->caps2;
			host->mmc->pm_caps  |= c->slot->chip->pm_caps;
		}
		host->quirks        |= c->slot->quirks;
		host->quirks2       |= c->slot->quirks2;
		host->mmc->caps     |= c->slot->caps;
		host->mmc->caps2    |= c->slot->caps2;
		host->mmc->pm_caps  |= c->slot->pm_caps;
	}

	host->mmc->caps2 |= MMC_CAP2_NO_PRESCAN_POWERUP;

	if (sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD)) {
		bool v = sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL);

		if (quirks & DMI_QUIRK_SD_CD_ACTIVE_HIGH)
			host->mmc->caps2 |= MMC_CAP2_CD_ACTIVE_HIGH;

		err = mmc_gpiod_request_cd(host->mmc, NULL, 0, v, 0);
		if (err) {
			if (err == -EPROBE_DEFER)
				goto err_free;
			dev_warn(dev, "failed to setup card detect gpio\n");
			c->use_runtime_pm = false;
		}

		if (quirks & DMI_QUIRK_RESET_SD_SIGNAL_VOLT_ON_SUSP)
			c->reset_signal_volt_on_suspend = true;

		if (quirks & DMI_QUIRK_SD_NO_WRITE_PROTECT)
			host->mmc->caps2 |= MMC_CAP2_NO_WRITE_PROTECT;
	}

	err = sdhci_setup_host(host);
	if (err)
		goto err_free;

	if (c->slot && c->slot->setup_host) {
		err = c->slot->setup_host(pdev);
		if (err)
			goto err_cleanup;
	}

	err = __sdhci_add_host(host);
	if (err)
		goto err_cleanup;

	if (c->use_runtime_pm) {
		pm_runtime_set_active(dev);
		pm_suspend_ignore_children(dev, 1);
		pm_runtime_set_autosuspend_delay(dev, 50);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_enable(dev);
	}

	device_enable_async_suspend(dev);

	return 0;

err_cleanup:
	sdhci_cleanup_host(c->host);
err_free:
	if (c->slot && c->slot->free_slot)
		c->slot->free_slot(pdev);

	sdhci_free_host(c->host);
	return err;
}

static void sdhci_acpi_remove(struct platform_device *pdev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int dead;

	if (c->use_runtime_pm) {
		pm_runtime_get_sync(dev);
		pm_runtime_disable(dev);
		pm_runtime_put_noidle(dev);
	}

	if (c->slot && c->slot->remove_slot)
		c->slot->remove_slot(pdev);

	dead = (sdhci_readl(c->host, SDHCI_INT_STATUS) == ~0);
	sdhci_remove_host(c->host, dead);

	if (c->slot && c->slot->free_slot)
		c->slot->free_slot(pdev);

	sdhci_free_host(c->host);
}

static void __maybe_unused sdhci_acpi_reset_signal_voltage_if_needed(
	struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct sdhci_host *host = c->host;

	if (c->is_intel && c->reset_signal_volt_on_suspend &&
	    host->mmc->ios.signal_voltage != MMC_SIGNAL_VOLTAGE_330) {
		struct intel_host *intel_host = sdhci_acpi_priv(c);
		unsigned int fn = INTEL_DSM_V33_SWITCH;
		u32 result = 0;

		intel_dsm(intel_host, dev, fn, &result);
	}
}

#ifdef CONFIG_PM_SLEEP

static int sdhci_acpi_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct sdhci_host *host = c->host;
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	sdhci_acpi_reset_signal_voltage_if_needed(dev);
	return 0;
}

static int sdhci_acpi_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	sdhci_acpi_byt_setting(&c->pdev->dev);

	return sdhci_resume_host(c->host);
}

#endif

#ifdef CONFIG_PM

static int sdhci_acpi_runtime_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct sdhci_host *host = c->host;
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = sdhci_runtime_suspend_host(host);
	if (ret)
		return ret;

	sdhci_acpi_reset_signal_voltage_if_needed(dev);
	return 0;
}

static int sdhci_acpi_runtime_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	sdhci_acpi_byt_setting(&c->pdev->dev);

	return sdhci_runtime_resume_host(c->host, 0);
}

#endif

static const struct dev_pm_ops sdhci_acpi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_acpi_suspend, sdhci_acpi_resume)
	SET_RUNTIME_PM_OPS(sdhci_acpi_runtime_suspend,
			sdhci_acpi_runtime_resume, NULL)
};

static struct platform_driver sdhci_acpi_driver = {
	.driver = {
		.name			= "sdhci-acpi",
		.probe_type		= PROBE_PREFER_ASYNCHRONOUS,
		.acpi_match_table	= sdhci_acpi_ids,
		.pm			= &sdhci_acpi_pm_ops,
	},
	.probe	= sdhci_acpi_probe,
	.remove_new = sdhci_acpi_remove,
};

module_platform_driver(sdhci_acpi_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface ACPI driver");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL v2");
