// SPDX-License-Identifier: GPL-2.0
/*
 * sdhci_am654.c - SDHCI driver for TI's AM654 SOCs
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com
 *
 */
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>

#include "cqhci.h"
#include "sdhci-cqhci.h"
#include "sdhci-pltfm.h"

/* CTL_CFG Registers */
#define CTL_CFG_2		0x14
#define CTL_CFG_3		0x18

#define SLOTTYPE_MASK		GENMASK(31, 30)
#define SLOTTYPE_EMBEDDED	BIT(30)
#define TUNINGFORSDR50_MASK	BIT(13)

/* PHY Registers */
#define PHY_CTRL1	0x100
#define PHY_CTRL2	0x104
#define PHY_CTRL3	0x108
#define PHY_CTRL4	0x10C
#define PHY_CTRL5	0x110
#define PHY_CTRL6	0x114
#define PHY_STAT1	0x130
#define PHY_STAT2	0x134

#define IOMUX_ENABLE_SHIFT	31
#define IOMUX_ENABLE_MASK	BIT(IOMUX_ENABLE_SHIFT)
#define OTAPDLYENA_SHIFT	20
#define OTAPDLYENA_MASK		BIT(OTAPDLYENA_SHIFT)
#define OTAPDLYSEL_SHIFT	12
#define OTAPDLYSEL_MASK		GENMASK(15, 12)
#define STRBSEL_SHIFT		24
#define STRBSEL_4BIT_MASK	GENMASK(27, 24)
#define STRBSEL_8BIT_MASK	GENMASK(31, 24)
#define SEL50_SHIFT		8
#define SEL50_MASK		BIT(SEL50_SHIFT)
#define SEL100_SHIFT		9
#define SEL100_MASK		BIT(SEL100_SHIFT)
#define FREQSEL_SHIFT		8
#define FREQSEL_MASK		GENMASK(10, 8)
#define CLKBUFSEL_SHIFT		0
#define CLKBUFSEL_MASK		GENMASK(2, 0)
#define DLL_TRIM_ICP_SHIFT	4
#define DLL_TRIM_ICP_MASK	GENMASK(7, 4)
#define DR_TY_SHIFT		20
#define DR_TY_MASK		GENMASK(22, 20)
#define ENDLL_SHIFT		1
#define ENDLL_MASK		BIT(ENDLL_SHIFT)
#define DLLRDY_SHIFT		0
#define DLLRDY_MASK		BIT(DLLRDY_SHIFT)
#define PDB_SHIFT		0
#define PDB_MASK		BIT(PDB_SHIFT)
#define CALDONE_SHIFT		1
#define CALDONE_MASK		BIT(CALDONE_SHIFT)
#define RETRIM_SHIFT		17
#define RETRIM_MASK		BIT(RETRIM_SHIFT)
#define SELDLYTXCLK_SHIFT	17
#define SELDLYTXCLK_MASK	BIT(SELDLYTXCLK_SHIFT)
#define SELDLYRXCLK_SHIFT	16
#define SELDLYRXCLK_MASK	BIT(SELDLYRXCLK_SHIFT)
#define ITAPDLYSEL_SHIFT	0
#define ITAPDLYSEL_MASK		GENMASK(4, 0)
#define ITAPDLYENA_SHIFT	8
#define ITAPDLYENA_MASK		BIT(ITAPDLYENA_SHIFT)
#define ITAPCHGWIN_SHIFT	9
#define ITAPCHGWIN_MASK		BIT(ITAPCHGWIN_SHIFT)

#define DRIVER_STRENGTH_50_OHM	0x0
#define DRIVER_STRENGTH_33_OHM	0x1
#define DRIVER_STRENGTH_66_OHM	0x2
#define DRIVER_STRENGTH_100_OHM	0x3
#define DRIVER_STRENGTH_40_OHM	0x4

#define CLOCK_TOO_SLOW_HZ	50000000
#define SDHCI_AM654_AUTOSUSPEND_DELAY	-1
#define RETRY_TUNING_MAX	10

/* Command Queue Host Controller Interface Base address */
#define SDHCI_AM654_CQE_BASE_ADDR 0x200

static const struct regmap_config sdhci_am654_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
};

struct timing_data {
	const char *otap_binding;
	const char *itap_binding;
	u32 capability;
};

static const struct timing_data td[] = {
	[MMC_TIMING_LEGACY]	= {"ti,otap-del-sel-legacy",
				   "ti,itap-del-sel-legacy",
				   0},
	[MMC_TIMING_MMC_HS]	= {"ti,otap-del-sel-mmc-hs",
				   "ti,itap-del-sel-mmc-hs",
				   MMC_CAP_MMC_HIGHSPEED},
	[MMC_TIMING_SD_HS]	= {"ti,otap-del-sel-sd-hs",
				   "ti,itap-del-sel-sd-hs",
				   MMC_CAP_SD_HIGHSPEED},
	[MMC_TIMING_UHS_SDR12]	= {"ti,otap-del-sel-sdr12",
				   "ti,itap-del-sel-sdr12",
				   MMC_CAP_UHS_SDR12},
	[MMC_TIMING_UHS_SDR25]	= {"ti,otap-del-sel-sdr25",
				   "ti,itap-del-sel-sdr25",
				   MMC_CAP_UHS_SDR25},
	[MMC_TIMING_UHS_SDR50]	= {"ti,otap-del-sel-sdr50",
				   NULL,
				   MMC_CAP_UHS_SDR50},
	[MMC_TIMING_UHS_SDR104]	= {"ti,otap-del-sel-sdr104",
				   NULL,
				   MMC_CAP_UHS_SDR104},
	[MMC_TIMING_UHS_DDR50]	= {"ti,otap-del-sel-ddr50",
				   NULL,
				   MMC_CAP_UHS_DDR50},
	[MMC_TIMING_MMC_DDR52]	= {"ti,otap-del-sel-ddr52",
				   "ti,itap-del-sel-ddr52",
				   MMC_CAP_DDR},
	[MMC_TIMING_MMC_HS200]	= {"ti,otap-del-sel-hs200",
				   NULL,
				   MMC_CAP2_HS200},
	[MMC_TIMING_MMC_HS400]	= {"ti,otap-del-sel-hs400",
				   NULL,
				   MMC_CAP2_HS400},
};

struct sdhci_am654_data {
	struct regmap *base;
	u32 otap_del_sel[ARRAY_SIZE(td)];
	u32 itap_del_sel[ARRAY_SIZE(td)];
	u32 itap_del_ena[ARRAY_SIZE(td)];
	int clkbuf_sel;
	int trm_icp;
	int drv_strength;
	int strb_sel;
	u32 flags;
	u32 quirks;
	bool dll_enable;
	u32 tuning_loop;

#define SDHCI_AM654_QUIRK_FORCE_CDTEST BIT(0)
#define SDHCI_AM654_QUIRK_SUPPRESS_V1P8_ENA BIT(1)
};

struct window {
	u8 start;
	u8 end;
	u8 length;
};

struct sdhci_am654_driver_data {
	const struct sdhci_pltfm_data *pdata;
	u32 flags;
	u32 quirks;
#define IOMUX_PRESENT	(1 << 0)
#define FREQSEL_2_BIT	(1 << 1)
#define STRBSEL_4_BIT	(1 << 2)
#define DLL_PRESENT	(1 << 3)
#define DLL_CALIB	(1 << 4)
};

static void sdhci_am654_setup_dll(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	int sel50, sel100, freqsel;
	u32 mask, val;
	int ret;

	/* Disable delay chain mode */
	regmap_update_bits(sdhci_am654->base, PHY_CTRL5,
			   SELDLYTXCLK_MASK | SELDLYRXCLK_MASK, 0);

	if (sdhci_am654->flags & FREQSEL_2_BIT) {
		switch (clock) {
		case 200000000:
			sel50 = 0;
			sel100 = 0;
			break;
		case 100000000:
			sel50 = 0;
			sel100 = 1;
			break;
		default:
			sel50 = 1;
			sel100 = 0;
		}

		/* Configure PHY DLL frequency */
		mask = SEL50_MASK | SEL100_MASK;
		val = (sel50 << SEL50_SHIFT) | (sel100 << SEL100_SHIFT);
		regmap_update_bits(sdhci_am654->base, PHY_CTRL5, mask, val);

	} else {
		switch (clock) {
		case 200000000:
			freqsel = 0x0;
			break;
		default:
			freqsel = 0x4;
		}

		regmap_update_bits(sdhci_am654->base, PHY_CTRL5, FREQSEL_MASK,
				   freqsel << FREQSEL_SHIFT);
	}
	/* Configure DLL TRIM */
	mask = DLL_TRIM_ICP_MASK;
	val = sdhci_am654->trm_icp << DLL_TRIM_ICP_SHIFT;

	/* Configure DLL driver strength */
	mask |= DR_TY_MASK;
	val |= sdhci_am654->drv_strength << DR_TY_SHIFT;
	regmap_update_bits(sdhci_am654->base, PHY_CTRL1, mask, val);

	/* Enable DLL */
	regmap_update_bits(sdhci_am654->base, PHY_CTRL1, ENDLL_MASK,
			   0x1 << ENDLL_SHIFT);
	/*
	 * Poll for DLL ready. Use a one second timeout.
	 * Works in all experiments done so far
	 */
	ret = regmap_read_poll_timeout(sdhci_am654->base, PHY_STAT1, val,
				       val & DLLRDY_MASK, 1000, 1000000);
	if (ret) {
		dev_err(mmc_dev(host->mmc), "DLL failed to relock\n");
		return;
	}
}

static void sdhci_am654_write_itapdly(struct sdhci_am654_data *sdhci_am654,
				      u32 itapdly, u32 enable)
{
	/* Set ITAPCHGWIN before writing to ITAPDLY */
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPCHGWIN_MASK,
			   1 << ITAPCHGWIN_SHIFT);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPDLYENA_MASK,
			   enable << ITAPDLYENA_SHIFT);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPDLYSEL_MASK,
			   itapdly << ITAPDLYSEL_SHIFT);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPCHGWIN_MASK, 0);
}

static void sdhci_am654_setup_delay_chain(struct sdhci_am654_data *sdhci_am654,
					  unsigned char timing)
{
	u32 mask, val;

	regmap_update_bits(sdhci_am654->base, PHY_CTRL1, ENDLL_MASK, 0);

	val = 1 << SELDLYTXCLK_SHIFT | 1 << SELDLYRXCLK_SHIFT;
	mask = SELDLYTXCLK_MASK | SELDLYRXCLK_MASK;
	regmap_update_bits(sdhci_am654->base, PHY_CTRL5, mask, val);

	sdhci_am654_write_itapdly(sdhci_am654, sdhci_am654->itap_del_sel[timing],
				  sdhci_am654->itap_del_ena[timing]);
}

static void sdhci_am654_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	unsigned char timing = host->mmc->ios.timing;
	u32 otap_del_sel;
	u32 mask, val;

	regmap_update_bits(sdhci_am654->base, PHY_CTRL1, ENDLL_MASK, 0);

	sdhci_set_clock(host, clock);

	/* Setup Output TAP delay */
	otap_del_sel = sdhci_am654->otap_del_sel[timing];

	mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
	val = (0x1 << OTAPDLYENA_SHIFT) |
	      (otap_del_sel << OTAPDLYSEL_SHIFT);

	/* Write to STRBSEL for HS400 speed mode */
	if (timing == MMC_TIMING_MMC_HS400) {
		if (sdhci_am654->flags & STRBSEL_4_BIT)
			mask |= STRBSEL_4BIT_MASK;
		else
			mask |= STRBSEL_8BIT_MASK;

		val |= sdhci_am654->strb_sel << STRBSEL_SHIFT;
	}

	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, val);

	if (timing > MMC_TIMING_UHS_SDR25 && clock >= CLOCK_TOO_SLOW_HZ) {
		sdhci_am654_setup_dll(host, clock);
		sdhci_am654->dll_enable = true;

		if (timing == MMC_TIMING_MMC_HS400) {
			sdhci_am654->itap_del_ena[timing] = 0x1;
			sdhci_am654->itap_del_sel[timing] = sdhci_am654->itap_del_sel[timing - 1];
		}

		sdhci_am654_write_itapdly(sdhci_am654, sdhci_am654->itap_del_sel[timing],
					  sdhci_am654->itap_del_ena[timing]);
	} else {
		sdhci_am654_setup_delay_chain(sdhci_am654, timing);
		sdhci_am654->dll_enable = false;
	}

	regmap_update_bits(sdhci_am654->base, PHY_CTRL5, CLKBUFSEL_MASK,
			   sdhci_am654->clkbuf_sel);
}

static void sdhci_j721e_4bit_set_clock(struct sdhci_host *host,
				       unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	unsigned char timing = host->mmc->ios.timing;
	u32 otap_del_sel;
	u32 itap_del_ena;
	u32 itap_del_sel;
	u32 mask, val;

	/* Setup Output TAP delay */
	otap_del_sel = sdhci_am654->otap_del_sel[timing];

	mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
	val = (0x1 << OTAPDLYENA_SHIFT) |
	      (otap_del_sel << OTAPDLYSEL_SHIFT);

	/* Setup Input TAP delay */
	itap_del_ena = sdhci_am654->itap_del_ena[timing];
	itap_del_sel = sdhci_am654->itap_del_sel[timing];

	mask |= ITAPDLYENA_MASK | ITAPDLYSEL_MASK;
	val |= (itap_del_ena << ITAPDLYENA_SHIFT) |
	       (itap_del_sel << ITAPDLYSEL_SHIFT);

	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPCHGWIN_MASK,
			   1 << ITAPCHGWIN_SHIFT);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, val);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, ITAPCHGWIN_MASK, 0);
	regmap_update_bits(sdhci_am654->base, PHY_CTRL5, CLKBUFSEL_MASK,
			   sdhci_am654->clkbuf_sel);

	sdhci_set_clock(host, clock);
}

static int sdhci_am654_start_signal_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if ((sdhci_am654->quirks & SDHCI_AM654_QUIRK_SUPPRESS_V1P8_ENA) &&
	    ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180) {
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret < 0) {
				pr_err("%s: Switching to 1.8V signalling voltage failed,\n",
				       mmc_hostname(mmc));
				return -EIO;
			}
		}
		return 0;
	}

	return sdhci_start_signal_voltage_switch(mmc, ios);
}

static u8 sdhci_am654_write_power_on(struct sdhci_host *host, u8 val, int reg)
{
	writeb(val, host->ioaddr + reg);
	usleep_range(1000, 10000);
	return readb(host->ioaddr + reg);
}

#define MAX_POWER_ON_TIMEOUT	1500000 /* us */
static void sdhci_am654_write_b(struct sdhci_host *host, u8 val, int reg)
{
	unsigned char timing = host->mmc->ios.timing;
	u8 pwr;
	int ret;

	if (reg == SDHCI_HOST_CONTROL) {
		switch (timing) {
		/*
		 * According to the data manual, HISPD bit
		 * should not be set in these speed modes.
		 */
		case MMC_TIMING_SD_HS:
		case MMC_TIMING_MMC_HS:
			val &= ~SDHCI_CTRL_HISPD;
		}
	}

	writeb(val, host->ioaddr + reg);
	if (reg == SDHCI_POWER_CONTROL && (val & SDHCI_POWER_ON)) {
		/*
		 * Power on will not happen until the card detect debounce
		 * timer expires. Wait at least 1.5 seconds for the power on
		 * bit to be set
		 */
		ret = read_poll_timeout(sdhci_am654_write_power_on, pwr,
					pwr & SDHCI_POWER_ON, 0,
					MAX_POWER_ON_TIMEOUT, false, host, val,
					reg);
		if (ret)
			dev_info(mmc_dev(host->mmc), "Power on failed\n");
	}
}

static void sdhci_am654_reset(struct sdhci_host *host, u8 mask)
{
	u8 ctrl;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);

	sdhci_and_cqhci_reset(host, mask);

	if (sdhci_am654->quirks & SDHCI_AM654_QUIRK_FORCE_CDTEST) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl |= SDHCI_CTRL_CDTEST_INS | SDHCI_CTRL_CDTEST_EN;
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}
}

static int sdhci_am654_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int err = sdhci_execute_tuning(mmc, opcode);

	if (err)
		return err;
	/*
	 * Tuning data remains in the buffer after tuning.
	 * Do a command and data reset to get rid of it
	 */
	sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	return 0;
}

static u32 sdhci_am654_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

#define ITAPDLY_LENGTH 32
#define ITAPDLY_LAST_INDEX (ITAPDLY_LENGTH - 1)

static int sdhci_am654_calculate_itap(struct sdhci_host *host, struct window
			  *fail_window, u8 num_fails, bool circular_buffer)
{
	u8 itap = 0, start_fail = 0, end_fail = 0, pass_length = 0;
	u8 first_fail_start = 0, last_fail_end = 0;
	struct device *dev = mmc_dev(host->mmc);
	struct window pass_window = {0, 0, 0};
	int prev_fail_end = -1;
	u8 i;

	if (!num_fails) {
		/* Retry tuning */
		dev_dbg(dev, "No failing region found, retry tuning\n");
		return -1;
	}

	if (fail_window->length == ITAPDLY_LENGTH) {
		/* Retry tuning */
		dev_dbg(dev, "No passing itapdly, retry tuning\n");
		return -1;
	}

	first_fail_start = fail_window->start;
	last_fail_end = fail_window[num_fails - 1].end;

	for (i = 0; i < num_fails; i++) {
		start_fail = fail_window[i].start;
		end_fail = fail_window[i].end;
		pass_length = start_fail - (prev_fail_end + 1);

		if (pass_length > pass_window.length) {
			pass_window.start = prev_fail_end + 1;
			pass_window.length = pass_length;
		}
		prev_fail_end = end_fail;
	}

	if (!circular_buffer)
		pass_length = ITAPDLY_LAST_INDEX - last_fail_end;
	else
		pass_length = ITAPDLY_LAST_INDEX - last_fail_end + first_fail_start;

	if (pass_length > pass_window.length) {
		pass_window.start = last_fail_end + 1;
		pass_window.length = pass_length;
	}

	if (!circular_buffer)
		itap = pass_window.start + (pass_window.length >> 1);
	else
		itap = (pass_window.start + (pass_window.length >> 1)) % ITAPDLY_LENGTH;

	return (itap > ITAPDLY_LAST_INDEX) ? ITAPDLY_LAST_INDEX >> 1 : itap;
}

static int sdhci_am654_do_tuning(struct sdhci_host *host,
				 u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	unsigned char timing = host->mmc->ios.timing;
	struct window fail_window[ITAPDLY_LENGTH];
	struct device *dev = mmc_dev(host->mmc);
	u8 curr_pass, itap;
	u8 fail_index = 0;
	u8 prev_pass = 1;

	memset(fail_window, 0, sizeof(fail_window));

	/* Enable ITAPDLY */
	sdhci_am654->itap_del_ena[timing] = 0x1;

	for (itap = 0; itap < ITAPDLY_LENGTH; itap++) {
		sdhci_am654_write_itapdly(sdhci_am654, itap, sdhci_am654->itap_del_ena[timing]);

		curr_pass = !mmc_send_tuning(host->mmc, opcode, NULL);

		if (!curr_pass && prev_pass)
			fail_window[fail_index].start = itap;

		if (!curr_pass) {
			fail_window[fail_index].end = itap;
			fail_window[fail_index].length++;
			dev_dbg(dev, "Failed itapdly=%d\n", itap);
		}

		if (curr_pass && !prev_pass)
			fail_index++;

		prev_pass = curr_pass;
	}

	if (fail_window[fail_index].length != 0)
		fail_index++;

	return sdhci_am654_calculate_itap(host, fail_window, fail_index,
					 sdhci_am654->dll_enable);
}

static int sdhci_am654_platform_execute_tuning(struct sdhci_host *host,
					       u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	unsigned char timing = host->mmc->ios.timing;
	struct device *dev = mmc_dev(host->mmc);
	int itapdly;

	do {
		itapdly = sdhci_am654_do_tuning(host, opcode);
		if (itapdly >= 0)
			break;
	} while (++sdhci_am654->tuning_loop < RETRY_TUNING_MAX);

	if (itapdly < 0) {
		dev_err(dev, "Failed to find itapdly, fail tuning\n");
		return -1;
	}

	dev_dbg(dev, "Passed tuning, final itapdly=%d\n", itapdly);
	sdhci_am654_write_itapdly(sdhci_am654, itapdly, sdhci_am654->itap_del_ena[timing]);
	/* Save ITAPDLY */
	sdhci_am654->itap_del_sel[timing] = itapdly;

	return 0;
}

static const struct sdhci_ops sdhci_am654_ops = {
	.platform_execute_tuning = sdhci_am654_platform_execute_tuning,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_set_power_and_bus_voltage,
	.set_clock = sdhci_am654_set_clock,
	.write_b = sdhci_am654_write_b,
	.irq = sdhci_am654_cqhci_irq,
	.reset = sdhci_and_cqhci_reset,
};

static const struct sdhci_pltfm_data sdhci_am654_pdata = {
	.ops = &sdhci_am654_ops,
	.quirks = SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_DISABLE_HW_TIMEOUT,
};

static const struct sdhci_am654_driver_data sdhci_am654_sr1_drvdata = {
	.pdata = &sdhci_am654_pdata,
	.flags = IOMUX_PRESENT | FREQSEL_2_BIT | STRBSEL_4_BIT | DLL_PRESENT |
		 DLL_CALIB,
};

static const struct sdhci_am654_driver_data sdhci_am654_drvdata = {
	.pdata = &sdhci_am654_pdata,
	.flags = IOMUX_PRESENT | FREQSEL_2_BIT | STRBSEL_4_BIT | DLL_PRESENT,
};

static const struct sdhci_ops sdhci_j721e_8bit_ops = {
	.platform_execute_tuning = sdhci_am654_platform_execute_tuning,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_set_power_and_bus_voltage,
	.set_clock = sdhci_am654_set_clock,
	.write_b = sdhci_am654_write_b,
	.irq = sdhci_am654_cqhci_irq,
	.reset = sdhci_and_cqhci_reset,
};

static const struct sdhci_pltfm_data sdhci_j721e_8bit_pdata = {
	.ops = &sdhci_j721e_8bit_ops,
	.quirks = SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_DISABLE_HW_TIMEOUT,
};

static const struct sdhci_am654_driver_data sdhci_j721e_8bit_drvdata = {
	.pdata = &sdhci_j721e_8bit_pdata,
	.flags = DLL_PRESENT | DLL_CALIB,
};

static const struct sdhci_ops sdhci_j721e_4bit_ops = {
	.platform_execute_tuning = sdhci_am654_platform_execute_tuning,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_bus_width = sdhci_set_bus_width,
	.set_power = sdhci_set_power_and_bus_voltage,
	.set_clock = sdhci_j721e_4bit_set_clock,
	.write_b = sdhci_am654_write_b,
	.irq = sdhci_am654_cqhci_irq,
	.reset = sdhci_am654_reset,
};

static const struct sdhci_pltfm_data sdhci_j721e_4bit_pdata = {
	.ops = &sdhci_j721e_4bit_ops,
	.quirks = SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_DISABLE_HW_TIMEOUT,
};

static const struct sdhci_am654_driver_data sdhci_j721e_4bit_drvdata = {
	.pdata = &sdhci_j721e_4bit_pdata,
	.flags = IOMUX_PRESENT,
};

static const struct sdhci_am654_driver_data sdhci_am62_4bit_drvdata = {
	.pdata = &sdhci_j721e_4bit_pdata,
	.flags = IOMUX_PRESENT,
	.quirks = SDHCI_AM654_QUIRK_SUPPRESS_V1P8_ENA,
};

static const struct soc_device_attribute sdhci_am654_devices[] = {
	{ .family = "AM65X",
	  .revision = "SR1.0",
	  .data = &sdhci_am654_sr1_drvdata
	},
	{/* sentinel */}
};

static void sdhci_am654_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static const struct cqhci_host_ops sdhci_am654_cqhci_ops = {
	.enable		= sdhci_cqe_enable,
	.disable	= sdhci_cqe_disable,
	.dumpregs	= sdhci_am654_dumpregs,
};

static int sdhci_am654_cqe_add_host(struct sdhci_host *host)
{
	struct cqhci_host *cq_host;

	cq_host = devm_kzalloc(mmc_dev(host->mmc), sizeof(struct cqhci_host),
			       GFP_KERNEL);
	if (!cq_host)
		return -ENOMEM;

	cq_host->mmio = host->ioaddr + SDHCI_AM654_CQE_BASE_ADDR;
	cq_host->quirks |= CQHCI_QUIRK_SHORT_TXFR_DESC_SZ;
	cq_host->caps |= CQHCI_TASK_DESC_SZ_128;
	cq_host->ops = &sdhci_am654_cqhci_ops;

	host->mmc->caps2 |= MMC_CAP2_CQE;

	return cqhci_init(cq_host, host->mmc, 1);
}

static int sdhci_am654_get_otap_delay(struct sdhci_host *host,
				      struct sdhci_am654_data *sdhci_am654)
{
	struct device *dev = mmc_dev(host->mmc);
	int i;
	int ret;

	for (i = MMC_TIMING_LEGACY; i <= MMC_TIMING_MMC_HS400; i++) {

		ret = device_property_read_u32(dev, td[i].otap_binding,
					       &sdhci_am654->otap_del_sel[i]);
		if (ret) {
			if (i == MMC_TIMING_LEGACY) {
				dev_err(dev, "Couldn't find mandatory ti,otap-del-sel-legacy\n");
				return ret;
			}
			dev_dbg(dev, "Couldn't find %s\n",
				td[i].otap_binding);
			/*
			 * Remove the corresponding capability
			 * if an otap-del-sel value is not found
			 */
			if (i <= MMC_TIMING_MMC_DDR52)
				host->mmc->caps &= ~td[i].capability;
			else
				host->mmc->caps2 &= ~td[i].capability;
		}

		if (td[i].itap_binding) {
			ret = device_property_read_u32(dev, td[i].itap_binding,
						       &sdhci_am654->itap_del_sel[i]);
			if (!ret)
				sdhci_am654->itap_del_ena[i] = 0x1;
		}
	}

	return 0;
}

static int sdhci_am654_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	u32 ctl_cfg_2 = 0;
	u32 mask;
	u32 val;
	int ret;

	/* Reset OTAP to default value */
	mask = OTAPDLYENA_MASK | OTAPDLYSEL_MASK;
	regmap_update_bits(sdhci_am654->base, PHY_CTRL4, mask, 0x0);

	if (sdhci_am654->flags & DLL_CALIB) {
		regmap_read(sdhci_am654->base, PHY_STAT1, &val);
		if (~val & CALDONE_MASK) {
			/* Calibrate IO lines */
			regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
					   PDB_MASK, PDB_MASK);
			ret = regmap_read_poll_timeout(sdhci_am654->base,
						       PHY_STAT1, val,
						       val & CALDONE_MASK,
						       1, 20);
			if (ret)
				return ret;
		}
	}

	/* Enable pins by setting IO mux to 0 */
	if (sdhci_am654->flags & IOMUX_PRESENT)
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
				   IOMUX_ENABLE_MASK, 0);

	/* Set slot type based on SD or eMMC */
	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		ctl_cfg_2 = SLOTTYPE_EMBEDDED;

	regmap_update_bits(sdhci_am654->base, CTL_CFG_2, SLOTTYPE_MASK,
			   ctl_cfg_2);

	/* Enable tuning for SDR50 */
	regmap_update_bits(sdhci_am654->base, CTL_CFG_3, TUNINGFORSDR50_MASK,
			   TUNINGFORSDR50_MASK);

	/* Use to re-execute tuning */
	sdhci_am654->tuning_loop = 0;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	ret = sdhci_am654_cqe_add_host(host);
	if (ret)
		goto err_cleanup_host;

	ret = sdhci_am654_get_otap_delay(host, sdhci_am654);
	if (ret)
		goto err_cleanup_host;

	ret = __sdhci_add_host(host);
	if (ret)
		goto err_cleanup_host;

	return 0;

err_cleanup_host:
	sdhci_cleanup_host(host);
	return ret;
}

static int sdhci_am654_get_of_property(struct platform_device *pdev,
					struct sdhci_am654_data *sdhci_am654)
{
	struct device *dev = &pdev->dev;
	int drv_strength;
	int ret;

	if (sdhci_am654->flags & DLL_PRESENT) {
		ret = device_property_read_u32(dev, "ti,trm-icp",
					       &sdhci_am654->trm_icp);
		if (ret)
			return ret;

		ret = device_property_read_u32(dev, "ti,driver-strength-ohm",
					       &drv_strength);
		if (ret)
			return ret;

		switch (drv_strength) {
		case 50:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_50_OHM;
			break;
		case 33:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_33_OHM;
			break;
		case 66:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_66_OHM;
			break;
		case 100:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_100_OHM;
			break;
		case 40:
			sdhci_am654->drv_strength = DRIVER_STRENGTH_40_OHM;
			break;
		default:
			dev_err(dev, "Invalid driver strength\n");
			return -EINVAL;
		}
	}

	device_property_read_u32(dev, "ti,strobe-sel", &sdhci_am654->strb_sel);
	device_property_read_u32(dev, "ti,clkbuf-sel",
				 &sdhci_am654->clkbuf_sel);

	if (device_property_read_bool(dev, "ti,fails-without-test-cd"))
		sdhci_am654->quirks |= SDHCI_AM654_QUIRK_FORCE_CDTEST;

	sdhci_get_of_property(pdev);

	return 0;
}

static const struct of_device_id sdhci_am654_of_match[] = {
	{
		.compatible = "ti,am654-sdhci-5.1",
		.data = &sdhci_am654_drvdata,
	},
	{
		.compatible = "ti,j721e-sdhci-8bit",
		.data = &sdhci_j721e_8bit_drvdata,
	},
	{
		.compatible = "ti,j721e-sdhci-4bit",
		.data = &sdhci_j721e_4bit_drvdata,
	},
	{
		.compatible = "ti,am64-sdhci-8bit",
		.data = &sdhci_j721e_8bit_drvdata,
	},
	{
		.compatible = "ti,am64-sdhci-4bit",
		.data = &sdhci_j721e_4bit_drvdata,
	},
	{
		.compatible = "ti,am62-sdhci",
		.data = &sdhci_am62_4bit_drvdata,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_am654_of_match);

static int sdhci_am654_probe(struct platform_device *pdev)
{
	const struct sdhci_am654_driver_data *drvdata;
	const struct soc_device_attribute *soc;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_am654_data *sdhci_am654;
	const struct of_device_id *match;
	struct sdhci_host *host;
	struct clk *clk_xin;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret;

	match = of_match_node(sdhci_am654_of_match, pdev->dev.of_node);
	drvdata = match->data;

	/* Update drvdata based on SoC revision */
	soc = soc_device_match(sdhci_am654_devices);
	if (soc && soc->data)
		drvdata = soc->data;

	host = sdhci_pltfm_init(pdev, drvdata->pdata, sizeof(*sdhci_am654));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	sdhci_am654->flags = drvdata->flags;
	sdhci_am654->quirks = drvdata->quirks;

	clk_xin = devm_clk_get(dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		dev_err(dev, "clk_xin clock not found.\n");
		ret = PTR_ERR(clk_xin);
		goto err_pltfm_free;
	}

	pltfm_host->clk = clk_xin;

	base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err_pltfm_free;
	}

	sdhci_am654->base = devm_regmap_init_mmio(dev, base,
						  &sdhci_am654_regmap_config);
	if (IS_ERR(sdhci_am654->base)) {
		dev_err(dev, "Failed to initialize regmap\n");
		ret = PTR_ERR(sdhci_am654->base);
		goto err_pltfm_free;
	}

	ret = sdhci_am654_get_of_property(pdev, sdhci_am654);
	if (ret)
		goto err_pltfm_free;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err_probe(dev, ret, "parsing dt failed\n");
		goto err_pltfm_free;
	}

	host->mmc_host_ops.start_signal_voltage_switch = sdhci_am654_start_signal_voltage_switch;
	host->mmc_host_ops.execute_tuning = sdhci_am654_execute_tuning;

	pm_runtime_get_noresume(dev);
	ret = pm_runtime_set_active(dev);
	if (ret)
		goto pm_put;
	pm_runtime_enable(dev);
	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		goto pm_disable;

	ret = sdhci_am654_init(host);
	if (ret)
		goto clk_disable;

	/* Setting up autosuspend */
	pm_runtime_set_autosuspend_delay(dev, SDHCI_AM654_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;

clk_disable:
	clk_disable_unprepare(pltfm_host->clk);
pm_disable:
	pm_runtime_disable(dev);
pm_put:
	pm_runtime_put_noidle(dev);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static void sdhci_am654_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct device *dev = &pdev->dev;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		dev_err(dev, "pm_runtime_get_sync() Failed\n");

	sdhci_remove_host(host, true);
	clk_disable_unprepare(pltfm_host->clk);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	sdhci_pltfm_free(pdev);
}

#ifdef CONFIG_PM
static int sdhci_am654_restore(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_am654_data *sdhci_am654 = sdhci_pltfm_priv(pltfm_host);
	u32 ctl_cfg_2 = 0;
	u32 val;
	int ret;

	if (sdhci_am654->flags & DLL_CALIB) {
		regmap_read(sdhci_am654->base, PHY_STAT1, &val);
		if (~val & CALDONE_MASK) {
			/* Calibrate IO lines */
			regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
					   PDB_MASK, PDB_MASK);
			ret = regmap_read_poll_timeout(sdhci_am654->base,
						       PHY_STAT1, val,
						       val & CALDONE_MASK,
						       1, 20);
			if (ret)
				return ret;
		}
	}

	/* Enable pins by setting IO mux to 0 */
	if (sdhci_am654->flags & IOMUX_PRESENT)
		regmap_update_bits(sdhci_am654->base, PHY_CTRL1,
				   IOMUX_ENABLE_MASK, 0);

	/* Set slot type based on SD or eMMC */
	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		ctl_cfg_2 = SLOTTYPE_EMBEDDED;

	regmap_update_bits(sdhci_am654->base, CTL_CFG_2, SLOTTYPE_MASK,
			   ctl_cfg_2);

	regmap_read(sdhci_am654->base, CTL_CFG_3, &val);
	if (~val & TUNINGFORSDR50_MASK)
		/* Enable tuning for SDR50 */
		regmap_update_bits(sdhci_am654->base, CTL_CFG_3, TUNINGFORSDR50_MASK,
				   TUNINGFORSDR50_MASK);

	return 0;
}

static int sdhci_am654_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = cqhci_suspend(host->mmc);
	if (ret)
		return ret;

	ret = sdhci_runtime_suspend_host(host);
	if (ret)
		return ret;

	/* disable the clock */
	clk_disable_unprepare(pltfm_host->clk);
	return 0;
}

static int sdhci_am654_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	/* Enable the clock */
	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		return ret;

	ret = sdhci_am654_restore(host);
	if (ret)
		return ret;

	ret = sdhci_runtime_resume_host(host, 0);
	if (ret)
		return ret;

	ret = cqhci_resume(host->mmc);
	if (ret)
		return ret;

	return 0;
}
#endif

static const struct dev_pm_ops sdhci_am654_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(sdhci_am654_runtime_suspend,
			   sdhci_am654_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver sdhci_am654_driver = {
	.driver = {
		.name = "sdhci-am654",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &sdhci_am654_dev_pm_ops,
		.of_match_table = sdhci_am654_of_match,
	},
	.probe = sdhci_am654_probe,
	.remove = sdhci_am654_remove,
};

module_platform_driver(sdhci_am654_driver);

MODULE_DESCRIPTION("Driver for SDHCI Controller on TI's AM654 devices");
MODULE_AUTHOR("Faiz Abbas <faiz_abbas@ti.com>");
MODULE_LICENSE("GPL");
