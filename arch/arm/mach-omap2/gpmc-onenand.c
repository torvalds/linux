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
#include <linux/platform_data/mtd-onenand-omap2.h>

#include <asm/mach/flash.h>

#include <plat/gpmc.h>

#include "soc.h"

#define	ONENAND_IO_SIZE	SZ_128K

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

static int omap2_onenand_set_async_mode(int cs, void __iomem *onenand_base)
{
	struct gpmc_timings t;
	u32 reg;
	int err;

	const int t_cer = 15;
	const int t_avdp = 12;
	const int t_aavdh = 7;
	const int t_ce = 76;
	const int t_aa = 76;
	const int t_oe = 20;
	const int t_cez = 20; /* max of t_cez, t_oez */
	const int t_ds = 30;
	const int t_wpl = 40;
	const int t_wph = 30;

	/* Ensure sync read and sync write are disabled */
	reg = readw(onenand_base + ONENAND_REG_SYS_CFG1);
	reg &= ~ONENAND_SYS_CFG1_SYNC_READ & ~ONENAND_SYS_CFG1_SYNC_WRITE;
	writew(reg, onenand_base + ONENAND_REG_SYS_CFG1);

	memset(&t, 0, sizeof(t));
	t.sync_clk = 0;
	t.cs_on = 0;
	t.adv_on = 0;

	/* Read */
	t.adv_rd_off = gpmc_round_ns_to_ticks(max_t(int, t_avdp, t_cer));
	t.oe_on  = t.adv_rd_off + gpmc_round_ns_to_ticks(t_aavdh);
	t.access = t.adv_on + gpmc_round_ns_to_ticks(t_aa);
	t.access = max_t(int, t.access, t.cs_on + gpmc_round_ns_to_ticks(t_ce));
	t.access = max_t(int, t.access, t.oe_on + gpmc_round_ns_to_ticks(t_oe));
	t.oe_off = t.access + gpmc_round_ns_to_ticks(1);
	t.cs_rd_off = t.oe_off;
	t.rd_cycle  = t.cs_rd_off + gpmc_round_ns_to_ticks(t_cez);

	/* Write */
	t.adv_wr_off = t.adv_rd_off;
	t.we_on  = t.oe_on;
	if (cpu_is_omap34xx()) {
		t.wr_data_mux_bus = t.we_on;
		t.wr_access = t.we_on + gpmc_round_ns_to_ticks(t_ds);
	}
	t.we_off = t.we_on + gpmc_round_ns_to_ticks(t_wpl);
	t.cs_wr_off = t.we_off + gpmc_round_ns_to_ticks(t_wph);
	t.wr_cycle  = t.cs_wr_off + gpmc_round_ns_to_ticks(t_cez);

	/* Configure GPMC for asynchronous read */
	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1,
			  GPMC_CONFIG1_DEVICESIZE_16 |
			  GPMC_CONFIG1_MUXADDDATA);

	err = gpmc_cs_set_timings(cs, &t);
	if (err)
		return err;

	/* Ensure sync read and sync write are disabled */
	reg = readw(onenand_base + ONENAND_REG_SYS_CFG1);
	reg &= ~ONENAND_SYS_CFG1_SYNC_READ & ~ONENAND_SYS_CFG1_SYNC_WRITE;
	writew(reg, onenand_base + ONENAND_REG_SYS_CFG1);

	return 0;
}

static void set_onenand_cfg(void __iomem *onenand_base, int latency,
				int sync_read, int sync_write, int hf, int vhf)
{
	u32 reg;

	reg = readw(onenand_base + ONENAND_REG_SYS_CFG1);
	reg &= ~((0x7 << ONENAND_SYS_CFG1_BRL_SHIFT) | (0x7 << 9));
	reg |=	(latency << ONENAND_SYS_CFG1_BRL_SHIFT) |
		ONENAND_SYS_CFG1_BL_16;
	if (sync_read)
		reg |= ONENAND_SYS_CFG1_SYNC_READ;
	else
		reg &= ~ONENAND_SYS_CFG1_SYNC_READ;
	if (sync_write)
		reg |= ONENAND_SYS_CFG1_SYNC_WRITE;
	else
		reg &= ~ONENAND_SYS_CFG1_SYNC_WRITE;
	if (hf)
		reg |= ONENAND_SYS_CFG1_HF;
	else
		reg &= ~ONENAND_SYS_CFG1_HF;
	if (vhf)
		reg |= ONENAND_SYS_CFG1_VHF;
	else
		reg &= ~ONENAND_SYS_CFG1_VHF;
	writew(reg, onenand_base + ONENAND_REG_SYS_CFG1);
}

static int omap2_onenand_get_freq(struct omap_onenand_platform_data *cfg,
				  void __iomem *onenand_base, bool *clk_dep)
{
	u16 ver = readw(onenand_base + ONENAND_REG_VERSION_ID);
	int freq = 0;

	if (cfg->get_freq) {
		struct onenand_freq_info fi;

		fi.maf_id = readw(onenand_base + ONENAND_REG_MANUFACTURER_ID);
		fi.dev_id = readw(onenand_base + ONENAND_REG_DEVICE_ID);
		fi.ver_id = ver;
		freq = cfg->get_freq(&fi, clk_dep);
		if (freq)
			return freq;
	}

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
		freq = 54;
		break;
	}

	return freq;
}

static int omap2_onenand_set_sync_mode(struct omap_onenand_platform_data *cfg,
					void __iomem *onenand_base,
					int *freq_ptr)
{
	struct gpmc_timings t;
	const int t_cer  = 15;
	const int t_avdp = 12;
	const int t_cez  = 20; /* max of t_cez, t_oez */
	const int t_ds   = 30;
	const int t_wpl  = 40;
	const int t_wph  = 30;
	int min_gpmc_clk_period, t_ces, t_avds, t_avdh, t_ach, t_aavdh, t_rdyo;
	int div, fclk_offset_ns, fclk_offset, gpmc_clk_ns, latency;
	int first_time = 0, hf = 0, vhf = 0, sync_read = 0, sync_write = 0;
	int err, ticks_cez;
	int cs = cfg->cs, freq = *freq_ptr;
	u32 reg;
	bool clk_dep = false;

	if (cfg->flags & ONENAND_SYNC_READ) {
		sync_read = 1;
	} else if (cfg->flags & ONENAND_SYNC_READWRITE) {
		sync_read = 1;
		sync_write = 1;
	} else
		return omap2_onenand_set_async_mode(cs, onenand_base);

	if (!freq) {
		/* Very first call freq is not known */
		err = omap2_onenand_set_async_mode(cs, onenand_base);
		if (err)
			return err;
		freq = omap2_onenand_get_freq(cfg, onenand_base, &clk_dep);
		first_time = 1;
	}

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
		sync_write = 0;
		break;
	}

	div = gpmc_cs_calc_divider(cs, min_gpmc_clk_period);
	gpmc_clk_ns = gpmc_ticks_to_ns(div);
	if (gpmc_clk_ns < 15) /* >66Mhz */
		hf = 1;
	if (gpmc_clk_ns < 12) /* >83Mhz */
		vhf = 1;
	if (vhf)
		latency = 8;
	else if (hf)
		latency = 6;
	else if (gpmc_clk_ns >= 25) /* 40 MHz*/
		latency = 3;
	else
		latency = 4;

	if (clk_dep) {
		if (gpmc_clk_ns < 12) { /* >83Mhz */
			t_ces   = 3;
			t_avds  = 4;
		} else if (gpmc_clk_ns < 15) { /* >66Mhz */
			t_ces   = 5;
			t_avds  = 4;
		} else if (gpmc_clk_ns < 25) { /* >40Mhz */
			t_ces   = 6;
			t_avds  = 5;
		} else {
			t_ces   = 7;
			t_avds  = 7;
		}
	}

	if (first_time)
		set_onenand_cfg(onenand_base, latency,
					sync_read, sync_write, hf, vhf);

	if (div == 1) {
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG2);
		reg |= (1 << 7);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG2, reg);
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG3);
		reg |= (1 << 7);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG3, reg);
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG4);
		reg |= (1 << 7);
		reg |= (1 << 23);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG4, reg);
	} else {
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG2);
		reg &= ~(1 << 7);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG2, reg);
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG3);
		reg &= ~(1 << 7);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG3, reg);
		reg = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG4);
		reg &= ~(1 << 7);
		reg &= ~(1 << 23);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG4, reg);
	}

	/* Set synchronous read timings */
	memset(&t, 0, sizeof(t));
	t.sync_clk = min_gpmc_clk_period;
	t.cs_on = 0;
	t.adv_on = 0;
	fclk_offset_ns = gpmc_round_ns_to_ticks(max_t(int, t_ces, t_avds));
	fclk_offset = gpmc_ns_to_ticks(fclk_offset_ns);
	t.page_burst_access = gpmc_clk_ns;

	/* Read */
	t.adv_rd_off = gpmc_ticks_to_ns(fclk_offset + gpmc_ns_to_ticks(t_avdh));
	t.oe_on = gpmc_ticks_to_ns(fclk_offset + gpmc_ns_to_ticks(t_ach));
	/* Force at least 1 clk between AVD High to OE Low */
	if (t.oe_on <= t.adv_rd_off)
		t.oe_on = t.adv_rd_off + gpmc_round_ns_to_ticks(1);
	t.access = gpmc_ticks_to_ns(fclk_offset + (latency + 1) * div);
	t.oe_off = t.access + gpmc_round_ns_to_ticks(1);
	t.cs_rd_off = t.oe_off;
	ticks_cez = ((gpmc_ns_to_ticks(t_cez) + div - 1) / div) * div;
	t.rd_cycle = gpmc_ticks_to_ns(fclk_offset + (latency + 1) * div +
		     ticks_cez);

	/* Write */
	if (sync_write) {
		t.adv_wr_off = t.adv_rd_off;
		t.we_on  = 0;
		t.we_off = t.cs_rd_off;
		t.cs_wr_off = t.cs_rd_off;
		t.wr_cycle  = t.rd_cycle;
		if (cpu_is_omap34xx()) {
			t.wr_data_mux_bus = gpmc_ticks_to_ns(fclk_offset +
					gpmc_ps_to_ticks(min_gpmc_clk_period +
					t_rdyo * 1000));
			t.wr_access = t.access;
		}
	} else {
		t.adv_wr_off = gpmc_round_ns_to_ticks(max_t(int,
							t_avdp, t_cer));
		t.we_on  = t.adv_wr_off + gpmc_round_ns_to_ticks(t_aavdh);
		t.we_off = t.we_on + gpmc_round_ns_to_ticks(t_wpl);
		t.cs_wr_off = t.we_off + gpmc_round_ns_to_ticks(t_wph);
		t.wr_cycle  = t.cs_wr_off + gpmc_round_ns_to_ticks(t_cez);
		if (cpu_is_omap34xx()) {
			t.wr_data_mux_bus = t.we_on;
			t.wr_access = t.we_on + gpmc_round_ns_to_ticks(t_ds);
		}
	}

	/* Configure GPMC for synchronous read */
	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1,
			  GPMC_CONFIG1_WRAPBURST_SUPP |
			  GPMC_CONFIG1_READMULTIPLE_SUPP |
			  (sync_read ? GPMC_CONFIG1_READTYPE_SYNC : 0) |
			  (sync_write ? GPMC_CONFIG1_WRITEMULTIPLE_SUPP : 0) |
			  (sync_write ? GPMC_CONFIG1_WRITETYPE_SYNC : 0) |
			  GPMC_CONFIG1_CLKACTIVATIONTIME(fclk_offset) |
			  GPMC_CONFIG1_PAGE_LEN(2) |
			  (cpu_is_omap34xx() ? 0 :
				(GPMC_CONFIG1_WAIT_READ_MON |
				 GPMC_CONFIG1_WAIT_PIN_SEL(0))) |
			  GPMC_CONFIG1_DEVICESIZE_16 |
			  GPMC_CONFIG1_DEVICETYPE_NOR |
			  GPMC_CONFIG1_MUXADDDATA);

	err = gpmc_cs_set_timings(cs, &t);
	if (err)
		return err;

	set_onenand_cfg(onenand_base, latency, sync_read, sync_write, hf, vhf);

	*freq_ptr = freq;

	return 0;
}

static int gpmc_onenand_setup(void __iomem *onenand_base, int *freq_ptr)
{
	struct device *dev = &gpmc_onenand_device.dev;

	/* Set sync timings in GPMC */
	if (omap2_onenand_set_sync_mode(gpmc_onenand_data, onenand_base,
			freq_ptr) < 0) {
		dev_err(dev, "Unable to set synchronous mode\n");
		return -EINVAL;
	}

	return 0;
}

void __init gpmc_onenand_init(struct omap_onenand_platform_data *_onenand_data)
{
	int err;

	gpmc_onenand_data = _onenand_data;
	gpmc_onenand_data->onenand_setup = gpmc_onenand_setup;
	gpmc_onenand_device.dev.platform_data = gpmc_onenand_data;

	if (cpu_is_omap24xx() &&
			(gpmc_onenand_data->flags & ONENAND_SYNC_READWRITE)) {
		printk(KERN_ERR "Onenand using only SYNC_READ on 24xx\n");
		gpmc_onenand_data->flags &= ~ONENAND_SYNC_READWRITE;
		gpmc_onenand_data->flags |= ONENAND_SYNC_READ;
	}

	err = gpmc_cs_request(gpmc_onenand_data->cs, ONENAND_IO_SIZE,
				(unsigned long *)&gpmc_onenand_resource.start);
	if (err < 0) {
		pr_err("%s: Cannot request GPMC CS\n", __func__);
		return;
	}

	gpmc_onenand_resource.end = gpmc_onenand_resource.start +
							ONENAND_IO_SIZE - 1;

	if (platform_device_register(&gpmc_onenand_device) < 0) {
		pr_err("%s: Unable to register OneNAND device\n", __func__);
		gpmc_cs_free(gpmc_onenand_data->cs);
		return;
	}
}
