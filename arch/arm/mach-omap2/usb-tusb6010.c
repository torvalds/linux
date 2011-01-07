/*
 * linux/arch/arm/mach-omap2/usb-tusb6010.c
 *
 * Copyright (C) 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <linux/usb/musb.h>

#include <plat/gpmc.h>

#include "mux.h"

static u8		async_cs, sync_cs;
static unsigned		refclk_psec;


/* t2_ps, when quantized to fclk units, must happen no earlier than
 * the clock after after t1_NS.
 *
 * Return a possibly updated value of t2_ps, converted to nsec.
 */
static unsigned
next_clk(unsigned t1_NS, unsigned t2_ps, unsigned fclk_ps)
{
	unsigned	t1_ps = t1_NS * 1000;
	unsigned	t1_f, t2_f;

	if ((t1_ps + fclk_ps) < t2_ps)
		return t2_ps / 1000;

	t1_f = (t1_ps + fclk_ps - 1) / fclk_ps;
	t2_f = (t2_ps + fclk_ps - 1) / fclk_ps;

	if (t1_f >= t2_f)
		t2_f = t1_f + 1;

	return (t2_f * fclk_ps) / 1000;
}

/* NOTE:  timings are from tusb 6010 datasheet Rev 1.8, 12-Sept 2006 */

static int tusb_set_async_mode(unsigned sysclk_ps, unsigned fclk_ps)
{
	struct gpmc_timings	t;
	unsigned		t_acsnh_advnh = sysclk_ps + 3000;
	unsigned		tmp;

	memset(&t, 0, sizeof(t));

	/* CS_ON = t_acsnh_acsnl */
	t.cs_on = 8;
	/* ADV_ON = t_acsnh_advnh - t_advn */
	t.adv_on = next_clk(t.cs_on, t_acsnh_advnh - 7000, fclk_ps);

	/*
	 * READ ... from omap2420 TRM fig 12-13
	 */

	/* ADV_RD_OFF = t_acsnh_advnh */
	t.adv_rd_off = next_clk(t.adv_on, t_acsnh_advnh, fclk_ps);

	/* OE_ON = t_acsnh_advnh + t_advn_oen (then wait for nRDY) */
	t.oe_on = next_clk(t.adv_on, t_acsnh_advnh + 1000, fclk_ps);

	/* ACCESS = counters continue only after nRDY */
	tmp = t.oe_on * 1000 + 300;
	t.access = next_clk(t.oe_on, tmp, fclk_ps);

	/* OE_OFF = after data gets sampled */
	tmp = t.access * 1000;
	t.oe_off = next_clk(t.access, tmp, fclk_ps);

	t.cs_rd_off = t.oe_off;

	tmp = t.cs_rd_off * 1000 + 7000 /* t_acsn_rdy_z */;
	t.rd_cycle = next_clk(t.cs_rd_off, tmp, fclk_ps);

	/*
	 * WRITE ... from omap2420 TRM fig 12-15
	 */

	/* ADV_WR_OFF = t_acsnh_advnh */
	t.adv_wr_off = t.adv_rd_off;

	/* WE_ON = t_acsnh_advnh + t_advn_wen (then wait for nRDY) */
	t.we_on = next_clk(t.adv_wr_off, t_acsnh_advnh + 1000, fclk_ps);

	/* WE_OFF = after data gets sampled */
	tmp = t.we_on * 1000 + 300;
	t.we_off = next_clk(t.we_on, tmp, fclk_ps);

	t.cs_wr_off = t.we_off;

	tmp = t.cs_wr_off * 1000 + 7000 /* t_acsn_rdy_z */;
	t.wr_cycle = next_clk(t.cs_wr_off, tmp, fclk_ps);

	return gpmc_cs_set_timings(async_cs, &t);
}

static int tusb_set_sync_mode(unsigned sysclk_ps, unsigned fclk_ps)
{
	struct gpmc_timings	t;
	unsigned		t_scsnh_advnh = sysclk_ps + 3000;
	unsigned		tmp;

	memset(&t, 0, sizeof(t));
	t.cs_on = 8;

	/* ADV_ON = t_acsnh_advnh - t_advn */
	t.adv_on = next_clk(t.cs_on, t_scsnh_advnh - 7000, fclk_ps);

	/* GPMC_CLK rate = fclk rate / div */
	t.sync_clk = 11100 /* 11.1 nsec */;
	tmp = (t.sync_clk + fclk_ps - 1) / fclk_ps;
	if (tmp > 4)
		return -ERANGE;
	if (tmp <= 0)
		tmp = 1;
	t.page_burst_access = (fclk_ps * tmp) / 1000;

	/*
	 * READ ... based on omap2420 TRM fig 12-19, 12-20
	 */

	/* ADV_RD_OFF = t_scsnh_advnh */
	t.adv_rd_off = next_clk(t.adv_on, t_scsnh_advnh, fclk_ps);

	/* OE_ON = t_scsnh_advnh + t_advn_oen * fclk_ps (then wait for nRDY) */
	tmp = (t.adv_rd_off * 1000) + (3 * fclk_ps);
	t.oe_on = next_clk(t.adv_on, tmp, fclk_ps);

	/* ACCESS = number of clock cycles after t_adv_eon */
	tmp = (t.oe_on * 1000) + (5 * fclk_ps);
	t.access = next_clk(t.oe_on, tmp, fclk_ps);

	/* OE_OFF = after data gets sampled */
	tmp = (t.access * 1000) + (1 * fclk_ps);
	t.oe_off = next_clk(t.access, tmp, fclk_ps);

	t.cs_rd_off = t.oe_off;

	tmp = t.cs_rd_off * 1000 + 7000 /* t_scsn_rdy_z */;
	t.rd_cycle = next_clk(t.cs_rd_off, tmp, fclk_ps);

	/*
	 * WRITE ... based on omap2420 TRM fig 12-21
	 */

	/* ADV_WR_OFF = t_scsnh_advnh */
	t.adv_wr_off = t.adv_rd_off;

	/* WE_ON = t_scsnh_advnh + t_advn_wen * fclk_ps (then wait for nRDY) */
	tmp = (t.adv_wr_off * 1000) + (3 * fclk_ps);
	t.we_on = next_clk(t.adv_wr_off, tmp, fclk_ps);

	/* WE_OFF = number of clock cycles after t_adv_wen */
	tmp = (t.we_on * 1000) + (6 * fclk_ps);
	t.we_off = next_clk(t.we_on, tmp, fclk_ps);

	t.cs_wr_off = t.we_off;

	tmp = t.cs_wr_off * 1000 + 7000 /* t_scsn_rdy_z */;
	t.wr_cycle = next_clk(t.cs_wr_off, tmp, fclk_ps);

	return gpmc_cs_set_timings(sync_cs, &t);
}

extern unsigned long gpmc_get_fclk_period(void);

/* tusb driver calls this when it changes the chip's clocking */
int tusb6010_platform_retime(unsigned is_refclk)
{
	static const char	error[] =
		KERN_ERR "tusb6010 %s retime error %d\n";

	unsigned	fclk_ps = gpmc_get_fclk_period();
	unsigned	sysclk_ps;
	int		status;

	if (!refclk_psec || fclk_ps == 0)
		return -ENODEV;

	sysclk_ps = is_refclk ? refclk_psec : TUSB6010_OSCCLK_60;

	status = tusb_set_async_mode(sysclk_ps, fclk_ps);
	if (status < 0) {
		printk(error, "async", status);
		goto done;
	}
	status = tusb_set_sync_mode(sysclk_ps, fclk_ps);
	if (status < 0)
		printk(error, "sync", status);
done:
	return status;
}
EXPORT_SYMBOL_GPL(tusb6010_platform_retime);

static struct resource tusb_resources[] = {
	/* Order is significant!  The start/end fields
	 * are updated during setup..
	 */
	{ /* Asynchronous access */
		.flags	= IORESOURCE_MEM,
	},
	{ /* Synchronous access */
		.flags	= IORESOURCE_MEM,
	},
	{ /* IRQ */
		.name	= "mc",
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 tusb_dmamask = ~(u32)0;

static struct platform_device tusb_device = {
	.name		= "musb_hdrc",
	.id		= -1,
	.dev = {
		.dma_mask		= &tusb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(tusb_resources),
	.resource	= tusb_resources,
};


/* this may be called only from board-*.c setup code */
int __init
tusb6010_setup_interface(struct musb_hdrc_platform_data *data,
		unsigned ps_refclk, unsigned waitpin,
		unsigned async, unsigned sync,
		unsigned irq, unsigned dmachan)
{
	int		status;
	static char	error[] __initdata =
		KERN_ERR "tusb6010 init error %d, %d\n";

	/* ASYNC region, primarily for PIO */
	status = gpmc_cs_request(async, SZ_16M, (unsigned long *)
				&tusb_resources[0].start);
	if (status < 0) {
		printk(error, 1, status);
		return status;
	}
	tusb_resources[0].end = tusb_resources[0].start + 0x9ff;
	async_cs = async;
	gpmc_cs_write_reg(async, GPMC_CS_CONFIG1,
			  GPMC_CONFIG1_PAGE_LEN(2)
			| GPMC_CONFIG1_WAIT_READ_MON
			| GPMC_CONFIG1_WAIT_WRITE_MON
			| GPMC_CONFIG1_WAIT_PIN_SEL(waitpin)
			| GPMC_CONFIG1_READTYPE_ASYNC
			| GPMC_CONFIG1_WRITETYPE_ASYNC
			| GPMC_CONFIG1_DEVICESIZE_16
			| GPMC_CONFIG1_DEVICETYPE_NOR
			| GPMC_CONFIG1_MUXADDDATA);


	/* SYNC region, primarily for DMA */
	status = gpmc_cs_request(sync, SZ_16M, (unsigned long *)
				&tusb_resources[1].start);
	if (status < 0) {
		printk(error, 2, status);
		return status;
	}
	tusb_resources[1].end = tusb_resources[1].start + 0x9ff;
	sync_cs = sync;
	gpmc_cs_write_reg(sync, GPMC_CS_CONFIG1,
			  GPMC_CONFIG1_READMULTIPLE_SUPP
			| GPMC_CONFIG1_READTYPE_SYNC
			| GPMC_CONFIG1_WRITEMULTIPLE_SUPP
			| GPMC_CONFIG1_WRITETYPE_SYNC
			| GPMC_CONFIG1_CLKACTIVATIONTIME(1)
			| GPMC_CONFIG1_PAGE_LEN(2)
			| GPMC_CONFIG1_WAIT_READ_MON
			| GPMC_CONFIG1_WAIT_WRITE_MON
			| GPMC_CONFIG1_WAIT_PIN_SEL(waitpin)
			| GPMC_CONFIG1_DEVICESIZE_16
			| GPMC_CONFIG1_DEVICETYPE_NOR
			| GPMC_CONFIG1_MUXADDDATA
			/* fclk divider gets set later */
			);

	/* IRQ */
	status = gpio_request(irq, "TUSB6010 irq");
	if (status < 0) {
		printk(error, 3, status);
		return status;
	}
	gpio_direction_input(irq);
	tusb_resources[2].start = irq + IH_GPIO_BASE;

	/* set up memory timings ... can speed them up later */
	if (!ps_refclk) {
		printk(error, 4, status);
		return -ENODEV;
	}
	refclk_psec = ps_refclk;
	status = tusb6010_platform_retime(1);
	if (status < 0) {
		printk(error, 5, status);
		return status;
	}

	/* finish device setup ... */
	if (!data) {
		printk(error, 6, status);
		return -ENODEV;
	}
	tusb_device.dev.platform_data = data;

	/* REVISIT let the driver know what DMA channels work */
	if (!dmachan)
		tusb_device.dev.dma_mask = NULL;
	else {
		/* assume OMAP 2420 ES2.0 and later */
		if (dmachan & (1 << 0))
			omap_mux_init_signal("sys_ndmareq0", 0);
		if (dmachan & (1 << 1))
			omap_mux_init_signal("sys_ndmareq1", 0);
		if (dmachan & (1 << 2))
			omap_mux_init_signal("sys_ndmareq2", 0);
		if (dmachan & (1 << 3))
			omap_mux_init_signal("sys_ndmareq3", 0);
		if (dmachan & (1 << 4))
			omap_mux_init_signal("sys_ndmareq4", 0);
		if (dmachan & (1 << 5))
			omap_mux_init_signal("sys_ndmareq5", 0);
	}

	/* so far so good ... register the device */
	status = platform_device_register(&tusb_device);
	if (status < 0) {
		printk(error, 7, status);
		return status;
	}
	return 0;
}
