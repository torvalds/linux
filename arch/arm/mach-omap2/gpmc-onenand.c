/*
 * linux/arch/arm/mach-omap2/gpmc-onenand.c
 *
 * Copyright (C) 2006 - 2009 Nokia Corporation
 * Contacts:	Juha Yrjola
 *		Tony Lindgren
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/onenand_regs.h>
#include <linux/io.h>
#include <linux/omap-gpmc.h>
#include <linux/platform_data/mtd-onenand-omap2.h>
#include <linux/err.h>

#include <asm/mach/flash.h>

#include "soc.h"

#define	ONENAND_IO_SIZE	SZ_128K

#define	ONENAND_FLAG_SYNCREAD	(1 << 0)
#define	ONENAND_FLAG_SYNCWRITE	(1 << 1)
#define	ONENAND_FLAG_HF		(1 << 2)
#define	ONENAND_FLAG_VHF	(1 << 3)

static unsigned onenand_flags;
static unsigned latency;

static struct omap_onenand_platform_data *gpmc_onenand_data;

static struct resource gpmc_onenand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device gpmc_onenand_device = {
	.name		= "omap2-onenand",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &gpmc_onenand_resource,
};

static struct gpmc_settings onenand_async = {
	.device_width	= GPMC_DEVWIDTH_16BIT,
	.mux_add_data	= GPMC_MUX_AD,
};

static struct gpmc_settings onenand_sync = {
	.burst_read	= true,
	.burst_wrap	= true,
	.burst_len	= GPMC_BURST_16,
	.device_width	= GPMC_DEVWIDTH_16BIT,
	.mux_add_data	= GPMC_MUX_AD,
	.wait_pin	= 0,
};

static void omap2_onenand_calc_async_timings(struct gpmc_timings *t)
{
	struct gpmc_device_timings dev_t;
	const int t_cer = 15;
	const int t_avdp = 12;
	const int t_aavdh = 7;
	const int t_ce = 76;
	const int t_aa = 76;
	const int t_oe = 20;
	const int t_cez = 20; /* max of t_cez, t_oez */
	const int t_wpl = 40;
	const int t_wph = 30;

	memset(&dev_t, 0, sizeof(dev_t));

	dev_t.t_avdp_r = max_t(int, t_avdp, t_cer) * 1000;
	dev_t.t_avdp_w = dev_t.t_avdp_r;
	dev_t.t_aavdh = t_aavdh * 1000;
	dev_t.t_aa = t_aa * 1000;
	dev_t.t_ce = t_ce * 1000;
	dev_t.t_oe = t_oe * 1000;
	dev_t.t_cez_r = t_cez * 1000;
	dev_t.t_cez_w = dev_t.t_cez_r;
	dev_t.t_wpl = t_wpl * 1000;
	dev_t.t_wph = t_wph * 1000;

	gpmc_calc_timings(t, &onenand_async, &dev_t);
}

static void omap2_onenand_set_async_mode(void __iomem *onenand_base)
{
	u32 reg;

	/* Ensure sync read and sync write are disabled */
	reg = readw(onenand_base + ONENAND_REG_SYS_CFG1);
	reg &= ~ONENAND_SYS_CFG1_SYNC_READ & ~ONENAND_SYS_CFG1_SYNC_WRITE;
	writew(reg, onenand_base + ONENAND_REG_SYS_CFG1);
}

static void set_onenand_cfg(void __iomem *onenand_base)
{
	u32 reg = ONENAND_SYS_CFG1_RDY | ONENAND_SYS_CFG1_INT;

	reg |=	(latency << ONENAND_SYS_CFG1_BRL_SHIFT) |
		ONENAND_SYS_CFG1_BL_16;
	if (onenand_flags & ONENAND_FLAG_SYNCREAD)
		reg |= ONENAND_SYS_CFG1_SYNC_READ;
	else
		reg &= ~ONENAND_SYS_CFG1_SYNC_READ;
	if (onenand_flags & ONENAND_FLAG_SYNCWRITE)
		reg |= ONENAND_SYS_CFG1_SYNC_WRITE;
	else
		reg &= ~ONENAND_SYS_CFG1_SYNC_WRITE;
	if (onenand_flags & ONENAND_FLAG_HF)
		reg |= ONENAND_SYS_CFG1_HF;
	else
		reg &= ~ONENAND_SYS_CFG1_HF;
	if (onenand_flags & ONENAND_FLAG_VHF)
		reg |= ONENAND_SYS_CFG1_VHF;
	else
		reg &= ~ONENAND_SYS_CFG1_VHF;

	writew(reg, onenand_base + ONENAND_REG_SYS_CFG1);
}

static int omap2_onenand_get_freq(struct omap_onenand_platform_data *cfg,
				  void __iomem *onenand_base)
{
	u16 ver = readw(onenand_base + ONENAND_REG_VERSION_ID);
	int freq;

	switch ((ver >> 4) & 0xf) {
	case 0:
		freq = 40;
		break;
	case 1:
		freq = 54;
		break;
	case 2:
		freq = 66;
		break;
	case 3:
		freq = 83;
		break;
	case 4:
		freq = 104;
		break;
	default:
		pr_err("onenand rate not detected, bad GPMC async timings?\n");
		freq = 0;
	}

	return freq;
}

static void omap2_onenand_calc_sync_timings(struct gpmc_timings *t,
					    unsigned int flags,
					    int freq)
{
	struct gpmc_device_timings dev_t;
	const int t_cer  = 15;
	const int t_avdp = 12;
	const int t_cez  = 20; /* max of t_cez, t_oez */
	const int t_wpl  = 40;
	const int t_wph  = 30;
	int min_gpmc_clk_period, t_ces, t_avds, t_avdh, t_ach, t_aavdh, t_rdyo;
	int div, gpmc_clk_ns;

	if (flags & ONENAND_SYNC_READ)
		onenand_flags = ONENAND_FLAG_SYNCREAD;
	else if (flags & ONENAND_SYNC_READWRITE)
		onenand_flags = ONENAND_FLAG_SYNCREAD | ONENAND_FLAG_SYNCWRITE;

	switch (freq) {
	case 104:
		min_gpmc_clk_period = 9600; /* 104 MHz */
		t_ces   = 3;
		t_avds  = 4;
		t_avdh  = 2;
		t_ach   = 3;
		t_aavdh = 6;
		t_rdyo  = 6;
		break;
	case 83:
		min_gpmc_clk_period = 12000; /* 83 MHz */
		t_ces   = 5;
		t_avds  = 4;
		t_avdh  = 2;
		t_ach   = 6;
		t_aavdh = 6;
		t_rdyo  = 9;
		break;
	case 66:
		min_gpmc_clk_period = 15000; /* 66 MHz */
		t_ces   = 6;
		t_avds  = 5;
		t_avdh  = 2;
		t_ach   = 6;
		t_aavdh = 6;
		t_rdyo  = 11;
		break;
	default:
		min_gpmc_clk_period = 18500; /* 54 MHz */
		t_ces   = 7;
		t_avds  = 7;
		t_avdh  = 7;
		t_ach   = 9;
		t_aavdh = 7;
		t_rdyo  = 15;
		onenand_flags &= ~ONENAND_FLAG_SYNCWRITE;
		break;
	}

	div = gpmc_calc_divider(min_gpmc_clk_period);
	gpmc_clk_ns = gpmc_ticks_to_ns(div);
	if (gpmc_clk_ns < 15) /* >66MHz */
		onenand_flags |= ONENAND_FLAG_HF;
	else
		onenand_flags &= ~ONENAND_FLAG_HF;
	if (gpmc_clk_ns < 12) /* >83MHz */
		onenand_flags |= ONENAND_FLAG_VHF;
	else
		onenand_flags &= ~ONENAND_FLAG_VHF;
	if (onenand_flags & ONENAND_FLAG_VHF)
		latency = 8;
	else if (onenand_flags & ONENAND_FLAG_HF)
		latency = 6;
	else if (gpmc_clk_ns >= 25) /* 40 MHz*/
		latency = 3;
	else
		latency = 4;

	/* Set synchronous read timings */
	memset(&dev_t, 0, sizeof(dev_t));

	if (onenand_flags & ONENAND_FLAG_SYNCREAD)
		onenand_sync.sync_read = true;
	if (onenand_flags & ONENAND_FLAG_SYNCWRITE) {
		onenand_sync.sync_write = true;
		onenand_sync.burst_write = true;
	} else {
		dev_t.t_avdp_w = max(t_avdp, t_cer) * 1000;
		dev_t.t_wpl = t_wpl * 1000;
		dev_t.t_wph = t_wph * 1000;
		dev_t.t_aavdh = t_aavdh * 1000;
	}
	dev_t.ce_xdelay = true;
	dev_t.avd_xdelay = true;
	dev_t.oe_xdelay = true;
	dev_t.we_xdelay = true;
	dev_t.clk = min_gpmc_clk_period;
	dev_t.t_bacc = dev_t.clk;
	dev_t.t_ces = t_ces * 1000;
	dev_t.t_avds = t_avds * 1000;
	dev_t.t_avdh = t_avdh * 1000;
	dev_t.t_ach = t_ach * 1000;
	dev_t.cyc_iaa = (latency + 1);
	dev_t.t_cez_r = t_cez * 1000;
	dev_t.t_cez_w = dev_t.t_cez_r;
	dev_t.cyc_aavdh_oe = 1;
	dev_t.t_rdyo = t_rdyo * 1000 + min_gpmc_clk_period;

	gpmc_calc_timings(t, &onenand_sync, &dev_t);
}

static int omap2_onenand_setup_async(void __iomem *onenand_base)
{
	struct gpmc_timings t;
	int ret;

	/*
	 * Note that we need to keep sync_write set for the call to
	 * omap2_onenand_set_async_mode() to work to detect the onenand
	 * supported clock rate for the sync timings.
	 */
	if (gpmc_onenand_data->of_node) {
		gpmc_read_settings_dt(gpmc_onenand_data->of_node,
				      &onenand_async);
		if (onenand_async.sync_read || onenand_async.sync_write) {
			if (onenand_async.sync_write)
				gpmc_onenand_data->flags |=
					ONENAND_SYNC_READWRITE;
			else
				gpmc_onenand_data->flags |= ONENAND_SYNC_READ;
			onenand_async.sync_read = false;
		}
	}

	onenand_async.sync_write = true;
	omap2_onenand_calc_async_timings(&t);

	ret = gpmc_cs_program_settings(gpmc_onenand_data->cs, &onenand_async);
	if (ret < 0)
		return ret;

	ret = gpmc_cs_set_timings(gpmc_onenand_data->cs, &t, &onenand_async);
	if (ret < 0)
		return ret;

	omap2_onenand_set_async_mode(onenand_base);

	return 0;
}

static int omap2_onenand_setup_sync(void __iomem *onenand_base, int *freq_ptr)
{
	int ret, freq = *freq_ptr;
	struct gpmc_timings t;

	if (!freq) {
		/* Very first call freq is not known */
		freq = omap2_onenand_get_freq(gpmc_onenand_data, onenand_base);
		if (!freq)
			return -ENODEV;
		set_onenand_cfg(onenand_base);
	}

	if (gpmc_onenand_data->of_node) {
		gpmc_read_settings_dt(gpmc_onenand_data->of_node,
				      &onenand_sync);
	} else {
		/*
		 * FIXME: Appears to be legacy code from initial ONENAND commit.
		 * Unclear what boards this is for and if this can be removed.
		 */
		if (!cpu_is_omap34xx())
			onenand_sync.wait_on_read = true;
	}

	omap2_onenand_calc_sync_timings(&t, gpmc_onenand_data->flags, freq);

	ret = gpmc_cs_program_settings(gpmc_onenand_data->cs, &onenand_sync);
	if (ret < 0)
		return ret;

	ret = gpmc_cs_set_timings(gpmc_onenand_data->cs, &t, &onenand_sync);
	if (ret < 0)
		return ret;

	set_onenand_cfg(onenand_base);

	*freq_ptr = freq;

	return 0;
}

static int gpmc_onenand_setup(void __iomem *onenand_base, int *freq_ptr)
{
	struct device *dev = &gpmc_onenand_device.dev;
	unsigned l = ONENAND_SYNC_READ | ONENAND_SYNC_READWRITE;
	int ret;

	ret = omap2_onenand_setup_async(onenand_base);
	if (ret) {
		dev_err(dev, "unable to set to async mode\n");
		return ret;
	}

	if (!(gpmc_onenand_data->flags & l))
		return 0;

	ret = omap2_onenand_setup_sync(onenand_base, freq_ptr);
	if (ret)
		dev_err(dev, "unable to set to sync mode\n");
	return ret;
}

int gpmc_onenand_init(struct omap_onenand_platform_data *_onenand_data)
{
	int err;
	struct device *dev = &gpmc_onenand_device.dev;

	gpmc_onenand_data = _onenand_data;
	gpmc_onenand_data->onenand_setup = gpmc_onenand_setup;
	gpmc_onenand_device.dev.platform_data = gpmc_onenand_data;

	if (cpu_is_omap24xx() &&
			(gpmc_onenand_data->flags & ONENAND_SYNC_READWRITE)) {
		dev_warn(dev, "OneNAND using only SYNC_READ on 24xx\n");
		gpmc_onenand_data->flags &= ~ONENAND_SYNC_READWRITE;
		gpmc_onenand_data->flags |= ONENAND_SYNC_READ;
	}

	if (cpu_is_omap34xx())
		gpmc_onenand_data->flags |= ONENAND_IN_OMAP34XX;
	else
		gpmc_onenand_data->flags &= ~ONENAND_IN_OMAP34XX;

	err = gpmc_cs_request(gpmc_onenand_data->cs, ONENAND_IO_SIZE,
				(unsigned long *)&gpmc_onenand_resource.start);
	if (err < 0) {
		dev_err(dev, "Cannot request GPMC CS %d, error %d\n",
			gpmc_onenand_data->cs, err);
		return err;
	}

	gpmc_onenand_resource.end = gpmc_onenand_resource.start +
							ONENAND_IO_SIZE - 1;

	err = platform_device_register(&gpmc_onenand_device);
	if (err) {
		dev_err(dev, "Unable to register OneNAND device\n");
		gpmc_cs_free(gpmc_onenand_data->cs);
	}

	return err;
}
