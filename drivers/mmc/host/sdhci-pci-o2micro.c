// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 BayHub Technology Ltd.
 *
 * Authors: Peter Guo <peter.guo@bayhubtech.com>
 *          Adam Lee <adam.lee@canonical.com>
 *          Ernest Zhang <ernest.zhang@bayhubtech.com>
 */

#include <linux/pci.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/delay.h>
#include <linux/iopoll.h>

#include "sdhci.h"
#include "sdhci-pci.h"

/*
 * O2Micro device registers
 */

#define O2_SD_MISC_REG5		0x64
#define O2_SD_LD0_CTRL		0x68
#define O2_SD_DEV_CTRL		0x88
#define O2_SD_LOCK_WP		0xD3
#define O2_SD_TEST_REG		0xD4
#define O2_SD_FUNC_REG0		0xDC
#define O2_SD_MULTI_VCC3V	0xEE
#define O2_SD_CLKREQ		0xEC
#define O2_SD_CAPS		0xE0
#define O2_SD_ADMA1		0xE2
#define O2_SD_ADMA2		0xE7
#define O2_SD_INF_MOD		0xF1
#define O2_SD_MISC_CTRL4	0xFC
#define O2_SD_MISC_CTRL		0x1C0
#define O2_SD_PWR_FORCE_L0	0x0002
#define O2_SD_TUNING_CTRL	0x300
#define O2_SD_PLL_SETTING	0x304
#define O2_SD_MISC_SETTING	0x308
#define O2_SD_CLK_SETTING	0x328
#define O2_SD_CAP_REG2		0x330
#define O2_SD_CAP_REG0		0x334
#define O2_SD_UHS1_CAP_SETTING	0x33C
#define O2_SD_DELAY_CTRL	0x350
#define O2_SD_UHS2_L1_CTRL	0x35C
#define O2_SD_FUNC_REG3		0x3E0
#define O2_SD_FUNC_REG4		0x3E4
#define O2_SD_LED_ENABLE	BIT(6)
#define O2_SD_FREG0_LEDOFF	BIT(13)
#define O2_SD_FREG4_ENABLE_CLK_SET	BIT(22)

#define O2_SD_VENDOR_SETTING	0x110
#define O2_SD_VENDOR_SETTING2	0x1C8
#define O2_SD_HW_TUNING_DISABLE	BIT(4)

#define O2_PLL_DLL_WDT_CONTROL1	0x1CC
#define  O2_PLL_FORCE_ACTIVE	BIT(18)
#define  O2_PLL_LOCK_STATUS	BIT(14)
#define  O2_PLL_SOFT_RESET	BIT(12)
#define  O2_DLL_LOCK_STATUS	BIT(11)

#define O2_SD_DETECT_SETTING 0x324

static const u32 dmdn_table[] = {0x2B1C0000,
	0x2C1A0000, 0x371B0000, 0x35100000};
#define DMDN_SZ ARRAY_SIZE(dmdn_table)

struct o2_host {
	u8 dll_adjust_count;
};

static void sdhci_o2_wait_card_detect_stable(struct sdhci_host *host)
{
	ktime_t timeout;
	u32 scratch32;

	/* Wait max 50 ms */
	timeout = ktime_add_ms(ktime_get(), 50);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		scratch32 = sdhci_readl(host, SDHCI_PRESENT_STATE);
		if ((scratch32 & SDHCI_CARD_PRESENT) >> SDHCI_CARD_PRES_SHIFT
		    == (scratch32 & SDHCI_CD_LVL) >> SDHCI_CD_LVL_SHIFT)
			break;

		if (timedout) {
			pr_err("%s: Card Detect debounce never finished.\n",
			       mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}
}

static void sdhci_o2_enable_internal_clock(struct sdhci_host *host)
{
	ktime_t timeout;
	u16 scratch;
	u32 scratch32;

	/* PLL software reset */
	scratch32 = sdhci_readl(host, O2_PLL_DLL_WDT_CONTROL1);
	scratch32 |= O2_PLL_SOFT_RESET;
	sdhci_writel(host, scratch32, O2_PLL_DLL_WDT_CONTROL1);
	udelay(1);
	scratch32 &= ~(O2_PLL_SOFT_RESET);
	sdhci_writel(host, scratch32, O2_PLL_DLL_WDT_CONTROL1);

	/* PLL force active */
	scratch32 |= O2_PLL_FORCE_ACTIVE;
	sdhci_writel(host, scratch32, O2_PLL_DLL_WDT_CONTROL1);

	/* Wait max 20 ms */
	timeout = ktime_add_ms(ktime_get(), 20);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		scratch = sdhci_readw(host, O2_PLL_DLL_WDT_CONTROL1);
		if (scratch & O2_PLL_LOCK_STATUS)
			break;
		if (timedout) {
			pr_err("%s: Internal clock never stabilised.\n",
			       mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			goto out;
		}
		udelay(10);
	}

	/* Wait for card detect finish */
	udelay(1);
	sdhci_o2_wait_card_detect_stable(host);

out:
	/* Cancel PLL force active */
	scratch32 = sdhci_readl(host, O2_PLL_DLL_WDT_CONTROL1);
	scratch32 &= ~O2_PLL_FORCE_ACTIVE;
	sdhci_writel(host, scratch32, O2_PLL_DLL_WDT_CONTROL1);
}

static int sdhci_o2_get_cd(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	if (!(sdhci_readw(host, O2_PLL_DLL_WDT_CONTROL1) & O2_PLL_LOCK_STATUS))
		sdhci_o2_enable_internal_clock(host);

	return !!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
}

static void o2_pci_set_baseclk(struct sdhci_pci_chip *chip, u32 value)
{
	u32 scratch_32;

	pci_read_config_dword(chip->pdev,
			      O2_SD_PLL_SETTING, &scratch_32);

	scratch_32 &= 0x0000FFFF;
	scratch_32 |= value;

	pci_write_config_dword(chip->pdev,
			       O2_SD_PLL_SETTING, scratch_32);
}

static u32 sdhci_o2_pll_dll_wdt_control(struct sdhci_host *host)
{
	return sdhci_readl(host, O2_PLL_DLL_WDT_CONTROL1);
}

/*
 * This function is used to detect dll lock status.
 * Since the dll lock status bit will toggle randomly
 * with very short interval which needs to be polled
 * as fast as possible. Set sleep_us as 1 microsecond.
 */
static int sdhci_o2_wait_dll_detect_lock(struct sdhci_host *host)
{
	u32	scratch32 = 0;

	return readx_poll_timeout(sdhci_o2_pll_dll_wdt_control, host,
		scratch32, !(scratch32 & O2_DLL_LOCK_STATUS), 1, 1000000);
}

static void sdhci_o2_set_tuning_mode(struct sdhci_host *host)
{
	u16 reg;

	/* enable hardware tuning */
	reg = sdhci_readw(host, O2_SD_VENDOR_SETTING);
	reg &= ~O2_SD_HW_TUNING_DISABLE;
	sdhci_writew(host, reg, O2_SD_VENDOR_SETTING);
}

static void __sdhci_o2_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int i;

	sdhci_send_tuning(host, opcode);

	for (i = 0; i < 150; i++) {
		u16 ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
			if (ctrl & SDHCI_CTRL_TUNED_CLK) {
				host->tuning_done = true;
				return;
			}
			pr_warn("%s: HW tuning failed !\n",
				mmc_hostname(host->mmc));
			break;
		}

		mdelay(1);
	}

	pr_info("%s: Tuning failed, falling back to fixed sampling clock\n",
		mmc_hostname(host->mmc));
	sdhci_reset_tuning(host);
}

/*
 * This function is used to fix o2 dll shift issue.
 * It isn't necessary to detect card present before recovery.
 * Firstly, it is used by bht emmc card, which is embedded.
 * Second, before call recovery card present will be detected
 * outside of the execute tuning function.
 */
static int sdhci_o2_dll_recovery(struct sdhci_host *host)
{
	int ret = 0;
	u8 scratch_8 = 0;
	u32 scratch_32 = 0;
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct sdhci_pci_chip *chip = slot->chip;
	struct o2_host *o2_host = sdhci_pci_priv(slot);

	/* UnLock WP */
	pci_read_config_byte(chip->pdev,
			O2_SD_LOCK_WP, &scratch_8);
	scratch_8 &= 0x7f;
	pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch_8);
	while (o2_host->dll_adjust_count < DMDN_SZ && !ret) {
		/* Disable clock */
		sdhci_writeb(host, 0, SDHCI_CLOCK_CONTROL);

		/* PLL software reset */
		scratch_32 = sdhci_readl(host, O2_PLL_DLL_WDT_CONTROL1);
		scratch_32 |= O2_PLL_SOFT_RESET;
		sdhci_writel(host, scratch_32, O2_PLL_DLL_WDT_CONTROL1);

		pci_read_config_dword(chip->pdev,
					    O2_SD_FUNC_REG4,
					    &scratch_32);
		/* Enable Base Clk setting change */
		scratch_32 |= O2_SD_FREG4_ENABLE_CLK_SET;
		pci_write_config_dword(chip->pdev, O2_SD_FUNC_REG4, scratch_32);
		o2_pci_set_baseclk(chip, dmdn_table[o2_host->dll_adjust_count]);

		/* Enable internal clock */
		scratch_8 = SDHCI_CLOCK_INT_EN;
		sdhci_writeb(host, scratch_8, SDHCI_CLOCK_CONTROL);

		if (sdhci_o2_get_cd(host->mmc)) {
			/*
			 * need wait at least 5ms for dll status stable,
			 * after enable internal clock
			 */
			usleep_range(5000, 6000);
			if (sdhci_o2_wait_dll_detect_lock(host)) {
				scratch_8 |= SDHCI_CLOCK_CARD_EN;
				sdhci_writeb(host, scratch_8,
					SDHCI_CLOCK_CONTROL);
				ret = 1;
			} else {
				pr_warn("%s: DLL unlocked when dll_adjust_count is %d.\n",
					mmc_hostname(host->mmc),
					o2_host->dll_adjust_count);
			}
		} else {
			pr_err("%s: card present detect failed.\n",
				mmc_hostname(host->mmc));
			break;
		}

		o2_host->dll_adjust_count++;
	}
	if (!ret && o2_host->dll_adjust_count == DMDN_SZ)
		pr_err("%s: DLL adjust over max times\n",
		mmc_hostname(host->mmc));
	/* Lock WP */
	pci_read_config_byte(chip->pdev,
				   O2_SD_LOCK_WP, &scratch_8);
	scratch_8 |= 0x80;
	pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch_8);
	return ret;
}

static int sdhci_o2_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int current_bus_width = 0;
	u32 scratch32 = 0;
	u16 scratch = 0;

	/*
	 * This handler only implements the eMMC tuning that is specific to
	 * this controller.  Fall back to the standard method for other TIMING.
	 */
	if ((host->timing != MMC_TIMING_MMC_HS200) &&
		(host->timing != MMC_TIMING_UHS_SDR104))
		return sdhci_execute_tuning(mmc, opcode);

	if (WARN_ON((opcode != MMC_SEND_TUNING_BLOCK_HS200) &&
			(opcode != MMC_SEND_TUNING_BLOCK)))
		return -EINVAL;

	/* Force power mode enter L0 */
	scratch = sdhci_readw(host, O2_SD_MISC_CTRL);
	scratch |= O2_SD_PWR_FORCE_L0;
	sdhci_writew(host, scratch, O2_SD_MISC_CTRL);

	/* wait DLL lock, timeout value 5ms */
	if (readx_poll_timeout(sdhci_o2_pll_dll_wdt_control, host,
		scratch32, (scratch32 & O2_DLL_LOCK_STATUS), 1, 5000))
		pr_warn("%s: DLL can't lock in 5ms after force L0 during tuning.\n",
				mmc_hostname(host->mmc));
	/*
	 * Judge the tuning reason, whether caused by dll shift
	 * If cause by dll shift, should call sdhci_o2_dll_recovery
	 */
	if (!sdhci_o2_wait_dll_detect_lock(host))
		if (!sdhci_o2_dll_recovery(host)) {
			pr_err("%s: o2 dll recovery failed\n",
				mmc_hostname(host->mmc));
			return -EINVAL;
		}
	/*
	 * o2 sdhci host didn't support 8bit emmc tuning
	 */
	if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
		current_bus_width = mmc->ios.bus_width;
		mmc->ios.bus_width = MMC_BUS_WIDTH_4;
		sdhci_set_bus_width(host, MMC_BUS_WIDTH_4);
	}

	sdhci_o2_set_tuning_mode(host);

	sdhci_start_tuning(host);

	__sdhci_o2_execute_tuning(host, opcode);

	sdhci_end_tuning(host);

	if (current_bus_width == MMC_BUS_WIDTH_8) {
		mmc->ios.bus_width = MMC_BUS_WIDTH_8;
		sdhci_set_bus_width(host, current_bus_width);
	}

	/* Cancel force power mode enter L0 */
	scratch = sdhci_readw(host, O2_SD_MISC_CTRL);
	scratch &= ~(O2_SD_PWR_FORCE_L0);
	sdhci_writew(host, scratch, O2_SD_MISC_CTRL);

	sdhci_reset(host, SDHCI_RESET_CMD);
	sdhci_reset(host, SDHCI_RESET_DATA);

	host->flags &= ~SDHCI_HS400_TUNING;
	return 0;
}

static void o2_pci_led_enable(struct sdhci_pci_chip *chip)
{
	int ret;
	u32 scratch_32;

	/* Set led of SD host function enable */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_FUNC_REG0, &scratch_32);
	if (ret)
		return;

	scratch_32 &= ~O2_SD_FREG0_LEDOFF;
	pci_write_config_dword(chip->pdev,
			       O2_SD_FUNC_REG0, scratch_32);

	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_TEST_REG, &scratch_32);
	if (ret)
		return;

	scratch_32 |= O2_SD_LED_ENABLE;
	pci_write_config_dword(chip->pdev,
			       O2_SD_TEST_REG, scratch_32);
}

static void sdhci_pci_o2_fujin2_pci_init(struct sdhci_pci_chip *chip)
{
	u32 scratch_32;
	int ret;
	/* Improve write performance for SD3.0 */
	ret = pci_read_config_dword(chip->pdev, O2_SD_DEV_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 12) | (1 << 13) | (1 << 14));
	pci_write_config_dword(chip->pdev, O2_SD_DEV_CTRL, scratch_32);

	/* Enable Link abnormal reset generating Reset */
	ret = pci_read_config_dword(chip->pdev, O2_SD_MISC_REG5, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 19) | (1 << 11));
	scratch_32 |= (1 << 10);
	pci_write_config_dword(chip->pdev, O2_SD_MISC_REG5, scratch_32);

	/* set card power over current protection */
	ret = pci_read_config_dword(chip->pdev, O2_SD_TEST_REG, &scratch_32);
	if (ret)
		return;
	scratch_32 |= (1 << 4);
	pci_write_config_dword(chip->pdev, O2_SD_TEST_REG, scratch_32);

	/* adjust the output delay for SD mode */
	pci_write_config_dword(chip->pdev, O2_SD_DELAY_CTRL, 0x00002492);

	/* Set the output voltage setting of Aux 1.2v LDO */
	ret = pci_read_config_dword(chip->pdev, O2_SD_LD0_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(3 << 12);
	pci_write_config_dword(chip->pdev, O2_SD_LD0_CTRL, scratch_32);

	/* Set Max power supply capability of SD host */
	ret = pci_read_config_dword(chip->pdev, O2_SD_CAP_REG0, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x01FE);
	scratch_32 |= 0x00CC;
	pci_write_config_dword(chip->pdev, O2_SD_CAP_REG0, scratch_32);
	/* Set DLL Tuning Window */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_TUNING_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000000FF);
	scratch_32 |= 0x00000066;
	pci_write_config_dword(chip->pdev, O2_SD_TUNING_CTRL, scratch_32);

	/* Set UHS2 T_EIDLE */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_UHS2_L1_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000000FC);
	scratch_32 |= 0x00000084;
	pci_write_config_dword(chip->pdev, O2_SD_UHS2_L1_CTRL, scratch_32);

	/* Set UHS2 Termination */
	ret = pci_read_config_dword(chip->pdev, O2_SD_FUNC_REG3, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 21) | (1 << 30));

	pci_write_config_dword(chip->pdev, O2_SD_FUNC_REG3, scratch_32);

	/* Set L1 Entrance Timer */
	ret = pci_read_config_dword(chip->pdev, O2_SD_CAPS, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0xf0000000);
	scratch_32 |= 0x30000000;
	pci_write_config_dword(chip->pdev, O2_SD_CAPS, scratch_32);

	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_MISC_CTRL4, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000f0000);
	scratch_32 |= 0x00080000;
	pci_write_config_dword(chip->pdev, O2_SD_MISC_CTRL4, scratch_32);
}

static void sdhci_pci_o2_enable_msi(struct sdhci_pci_chip *chip,
				    struct sdhci_host *host)
{
	int ret;

	ret = pci_find_capability(chip->pdev, PCI_CAP_ID_MSI);
	if (!ret) {
		pr_info("%s: unsupported MSI, use INTx irq\n",
			mmc_hostname(host->mmc));
		return;
	}

	ret = pci_alloc_irq_vectors(chip->pdev, 1, 1,
				    PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret < 0) {
		pr_err("%s: enable PCI MSI failed, err=%d\n",
		       mmc_hostname(host->mmc), ret);
		return;
	}

	host->irq = pci_irq_vector(chip->pdev, 0);
}

static void sdhci_o2_enable_clk(struct sdhci_host *host, u16 clk)
{
	/* Enable internal clock */
	clk |= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	sdhci_o2_enable_internal_clock(host);
	if (sdhci_o2_get_cd(host->mmc)) {
		clk |= SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
	}
}

static void sdhci_pci_o2_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	u8 scratch;
	u32 scratch_32;
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct sdhci_pci_chip *chip = slot->chip;

	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	if ((host->timing == MMC_TIMING_UHS_SDR104) && (clock == 200000000)) {
		pci_read_config_byte(chip->pdev, O2_SD_LOCK_WP, &scratch);

		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		pci_read_config_dword(chip->pdev, O2_SD_PLL_SETTING, &scratch_32);

		if ((scratch_32 & 0xFFFF0000) != 0x2c280000)
			o2_pci_set_baseclk(chip, 0x2c280000);

		pci_read_config_byte(chip->pdev, O2_SD_LOCK_WP, &scratch);

		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
	}

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);
	sdhci_o2_enable_clk(host, clk);
}

static int sdhci_pci_o2_probe_slot(struct sdhci_pci_slot *slot)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_host *host;
	struct o2_host *o2_host = sdhci_pci_priv(slot);
	u32 reg, caps;
	int ret;

	chip = slot->chip;
	host = slot->host;

	o2_host->dll_adjust_count = 0;
	caps = sdhci_readl(host, SDHCI_CAPABILITIES);

	/*
	 * mmc_select_bus_width() will test the bus to determine the actual bus
	 * width.
	 */
	if (caps & SDHCI_CAN_DO_8BIT)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_O2_SDS0:
	case PCI_DEVICE_ID_O2_SEABIRD0:
	case PCI_DEVICE_ID_O2_SEABIRD1:
	case PCI_DEVICE_ID_O2_SDS1:
	case PCI_DEVICE_ID_O2_FUJIN2:
		reg = sdhci_readl(host, O2_SD_VENDOR_SETTING);
		if (reg & 0x1)
			host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

		sdhci_pci_o2_enable_msi(chip, host);

		if (chip->pdev->device == PCI_DEVICE_ID_O2_SEABIRD0) {
			ret = pci_read_config_dword(chip->pdev,
						    O2_SD_MISC_SETTING, &reg);
			if (ret)
				return -EIO;
			if (reg & (1 << 4)) {
				pr_info("%s: emmc 1.8v flag is set, force 1.8v signaling voltage\n",
					mmc_hostname(host->mmc));
				host->flags &= ~SDHCI_SIGNALING_330;
				host->flags |= SDHCI_SIGNALING_180;
				host->mmc->caps2 |= MMC_CAP2_NO_SD;
				host->mmc->caps2 |= MMC_CAP2_NO_SDIO;
				pci_write_config_dword(chip->pdev,
						       O2_SD_DETECT_SETTING, 3);
			}

			slot->host->mmc_host_ops.get_cd = sdhci_o2_get_cd;
		}

		if (chip->pdev->device == PCI_DEVICE_ID_O2_SEABIRD1) {
			slot->host->mmc_host_ops.get_cd = sdhci_o2_get_cd;
			host->mmc->caps2 |= MMC_CAP2_NO_SDIO;
			host->quirks2 |= SDHCI_QUIRK2_PRESET_VALUE_BROKEN;
		}

		host->mmc_host_ops.execute_tuning = sdhci_o2_execute_tuning;

		if (chip->pdev->device != PCI_DEVICE_ID_O2_FUJIN2)
			break;
		/* set dll watch dog timer */
		reg = sdhci_readl(host, O2_SD_VENDOR_SETTING2);
		reg |= (1 << 12);
		sdhci_writel(host, reg, O2_SD_VENDOR_SETTING2);

		break;
	default:
		break;
	}

	return 0;
}

static int sdhci_pci_o2_probe(struct sdhci_pci_chip *chip)
{
	int ret;
	u8 scratch;
	u32 scratch_32;

	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_O2_8220:
	case PCI_DEVICE_ID_O2_8221:
	case PCI_DEVICE_ID_O2_8320:
	case PCI_DEVICE_ID_O2_8321:
		/* This extra setup is required due to broken ADMA. */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		/* Set Multi 3 to VCC3V# */
		pci_write_config_byte(chip->pdev, O2_SD_MULTI_VCC3V, 0x08);

		/* Disable CLK_REQ# support after media DET */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_CLKREQ, &scratch);
		if (ret)
			return ret;
		scratch |= 0x20;
		pci_write_config_byte(chip->pdev, O2_SD_CLKREQ, scratch);

		/* Choose capabilities, enable SDMA.  We have to write 0x01
		 * to the capabilities register first to unlock it.
		 */
		ret = pci_read_config_byte(chip->pdev, O2_SD_CAPS, &scratch);
		if (ret)
			return ret;
		scratch |= 0x01;
		pci_write_config_byte(chip->pdev, O2_SD_CAPS, scratch);
		pci_write_config_byte(chip->pdev, O2_SD_CAPS, 0x73);

		/* Disable ADMA1/2 */
		pci_write_config_byte(chip->pdev, O2_SD_ADMA1, 0x39);
		pci_write_config_byte(chip->pdev, O2_SD_ADMA2, 0x08);

		/* Disable the infinite transfer mode */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_INF_MOD, &scratch);
		if (ret)
			return ret;
		scratch |= 0x08;
		pci_write_config_byte(chip->pdev, O2_SD_INF_MOD, scratch);

		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	case PCI_DEVICE_ID_O2_SDS0:
	case PCI_DEVICE_ID_O2_SDS1:
	case PCI_DEVICE_ID_O2_FUJIN2:
		/* UnLock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;

		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		/* DevId=8520 subId= 0x11 or 0x12  Type Chip support */
		if (chip->pdev->device == PCI_DEVICE_ID_O2_FUJIN2) {
			ret = pci_read_config_dword(chip->pdev,
						    O2_SD_FUNC_REG0,
						    &scratch_32);
			if (ret)
				return ret;
			scratch_32 = ((scratch_32 & 0xFF000000) >> 24);

			/* Check Whether subId is 0x11 or 0x12 */
			if ((scratch_32 == 0x11) || (scratch_32 == 0x12)) {
				scratch_32 = 0x25100000;

				o2_pci_set_baseclk(chip, scratch_32);
				ret = pci_read_config_dword(chip->pdev,
							    O2_SD_FUNC_REG4,
							    &scratch_32);
				if (ret)
					return ret;

				/* Enable Base Clk setting change */
				scratch_32 |= O2_SD_FREG4_ENABLE_CLK_SET;
				pci_write_config_dword(chip->pdev,
						       O2_SD_FUNC_REG4,
						       scratch_32);

				/* Set Tuning Window to 4 */
				pci_write_config_byte(chip->pdev,
						      O2_SD_TUNING_CTRL, 0x44);

				break;
			}
		}

		/* Enable 8520 led function */
		o2_pci_led_enable(chip);

		/* Set timeout CLK */
		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CLK_SETTING, &scratch_32);
		if (ret)
			return ret;

		scratch_32 &= ~(0xFF00);
		scratch_32 |= 0x07E0C800;
		pci_write_config_dword(chip->pdev,
				       O2_SD_CLK_SETTING, scratch_32);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CLKREQ, &scratch_32);
		if (ret)
			return ret;
		scratch_32 |= 0x3;
		pci_write_config_dword(chip->pdev, O2_SD_CLKREQ, scratch_32);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_PLL_SETTING, &scratch_32);
		if (ret)
			return ret;

		scratch_32 &= ~(0x1F3F070E);
		scratch_32 |= 0x18270106;
		pci_write_config_dword(chip->pdev,
				       O2_SD_PLL_SETTING, scratch_32);

		/* Disable UHS1 funciton */
		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CAP_REG2, &scratch_32);
		if (ret)
			return ret;
		scratch_32 &= ~(0xE0);
		pci_write_config_dword(chip->pdev,
				       O2_SD_CAP_REG2, scratch_32);

		if (chip->pdev->device == PCI_DEVICE_ID_O2_FUJIN2)
			sdhci_pci_o2_fujin2_pci_init(chip);

		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
					   O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	case PCI_DEVICE_ID_O2_SEABIRD0:
	case PCI_DEVICE_ID_O2_SEABIRD1:
		/* UnLock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;

		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_PLL_SETTING, &scratch_32);
		if (ret)
			return ret;

		if ((scratch_32 & 0xff000000) == 0x01000000) {
			scratch_32 &= 0x0000FFFF;
			scratch_32 |= 0x1F340000;

			pci_write_config_dword(chip->pdev,
					       O2_SD_PLL_SETTING, scratch_32);
		} else {
			scratch_32 &= 0x0000FFFF;
			scratch_32 |= 0x25100000;

			pci_write_config_dword(chip->pdev,
					       O2_SD_PLL_SETTING, scratch_32);

			ret = pci_read_config_dword(chip->pdev,
						    O2_SD_FUNC_REG4,
						    &scratch_32);
			if (ret)
				return ret;
			scratch_32 |= (1 << 22);
			pci_write_config_dword(chip->pdev,
					       O2_SD_FUNC_REG4, scratch_32);
		}

		/* Set Tuning Windows to 5 */
		pci_write_config_byte(chip->pdev,
				O2_SD_TUNING_CTRL, 0x55);
		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
					   O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_pci_o2_resume(struct sdhci_pci_chip *chip)
{
	sdhci_pci_o2_probe(chip);
	return sdhci_pci_resume_host(chip);
}
#endif

static const struct sdhci_ops sdhci_pci_o2_ops = {
	.set_clock = sdhci_pci_o2_set_clock,
	.enable_dma = sdhci_pci_enable_dma,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

const struct sdhci_pci_fixes sdhci_o2 = {
	.probe = sdhci_pci_o2_probe,
	.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD,
	.probe_slot = sdhci_pci_o2_probe_slot,
#ifdef CONFIG_PM_SLEEP
	.resume = sdhci_pci_o2_resume,
#endif
	.ops = &sdhci_pci_o2_ops,
	.priv_size = sizeof(struct o2_host),
};
