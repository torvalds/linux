// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby <jrigby@freescale.com>
 *
 * Description:
 * MPC512x Shared code
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/fsl-diu-fb.h>
#include <linux/memblock.h>
#include <sysdev/fsl_soc.h>

#include <asm/cacheflush.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/time.h>
#include <asm/mpc5121.h>
#include <asm/mpc52xx_psc.h>

#include "mpc512x.h"

static struct mpc512x_reset_module __iomem *reset_module_base;

static void __init mpc512x_restart_init(void)
{
	struct device_node *np;
	const char *reset_compat;

	reset_compat = mpc512x_select_reset_compat();
	np = of_find_compatible_node(NULL, NULL, reset_compat);
	if (!np)
		return;

	reset_module_base = of_iomap(np, 0);
	of_node_put(np);
}

void __noreturn mpc512x_restart(char *cmd)
{
	if (reset_module_base) {
		/* Enable software reset "RSTE" */
		out_be32(&reset_module_base->rpr, 0x52535445);
		/* Set software hard reset */
		out_be32(&reset_module_base->rcr, 0x2);
	} else {
		pr_err("Restart module not mapped.\n");
	}
	for (;;)
		;
}

struct fsl_diu_shared_fb {
	u8		gamma[0x300];	/* 32-bit aligned! */
	struct diu_ad	ad0;		/* 32-bit aligned! */
	phys_addr_t	fb_phys;
	size_t		fb_len;
	bool		in_use;
};

/* receives a pixel clock spec in pico seconds, adjusts the DIU clock rate */
static void mpc512x_set_pixel_clock(unsigned int pixclock)
{
	struct device_node *np;
	struct clk *clk_diu;
	unsigned long epsilon, minpixclock, maxpixclock;
	unsigned long offset, want, got, delta;

	/* lookup and enable the DIU clock */
	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-diu");
	if (!np) {
		pr_err("Could not find DIU device tree node.\n");
		return;
	}
	clk_diu = of_clk_get(np, 0);
	if (IS_ERR(clk_diu)) {
		/* backwards compat with device trees that lack clock specs */
		clk_diu = clk_get_sys(np->name, "ipg");
	}
	of_node_put(np);
	if (IS_ERR(clk_diu)) {
		pr_err("Could not lookup DIU clock.\n");
		return;
	}
	if (clk_prepare_enable(clk_diu)) {
		pr_err("Could not enable DIU clock.\n");
		return;
	}

	/*
	 * convert the picoseconds spec into the desired clock rate,
	 * determine the acceptable clock range for the monitor (+/- 5%),
	 * do the calculation in steps to avoid integer overflow
	 */
	pr_debug("DIU pixclock in ps - %u\n", pixclock);
	pixclock = (1000000000 / pixclock) * 1000;
	pr_debug("DIU pixclock freq  - %u\n", pixclock);
	epsilon = pixclock / 20; /* pixclock * 0.05 */
	pr_debug("DIU deviation      - %lu\n", epsilon);
	minpixclock = pixclock - epsilon;
	maxpixclock = pixclock + epsilon;
	pr_debug("DIU minpixclock    - %lu\n", minpixclock);
	pr_debug("DIU maxpixclock    - %lu\n", maxpixclock);

	/*
	 * check whether the DIU supports the desired pixel clock
	 *
	 * - simply request the desired clock and see what the
	 *   platform's clock driver will make of it, assuming that it
	 *   will setup the best approximation of the requested value
	 * - try other candidate frequencies in the order of decreasing
	 *   preference (i.e. with increasing distance from the desired
	 *   pixel clock, and checking the lower frequency before the
	 *   higher frequency to not overload the hardware) until the
	 *   first match is found -- any potential subsequent match
	 *   would only be as good as the former match or typically
	 *   would be less preferrable
	 *
	 * the offset increment of pixelclock divided by 64 is an
	 * arbitrary choice -- it's simple to calculate, in the typical
	 * case we expect the first check to succeed already, in the
	 * worst case seven frequencies get tested (the exact center and
	 * three more values each to the left and to the right) before
	 * the 5% tolerance window is exceeded, resulting in fast enough
	 * execution yet high enough probability of finding a suitable
	 * value, while the error rate will be in the order of single
	 * percents
	 */
	for (offset = 0; offset <= epsilon; offset += pixclock / 64) {
		want = pixclock - offset;
		pr_debug("DIU checking clock - %lu\n", want);
		clk_set_rate(clk_diu, want);
		got = clk_get_rate(clk_diu);
		delta = abs(pixclock - got);
		if (delta < epsilon)
			break;
		if (!offset)
			continue;
		want = pixclock + offset;
		pr_debug("DIU checking clock - %lu\n", want);
		clk_set_rate(clk_diu, want);
		got = clk_get_rate(clk_diu);
		delta = abs(pixclock - got);
		if (delta < epsilon)
			break;
	}
	if (offset <= epsilon) {
		pr_debug("DIU clock accepted - %lu\n", want);
		pr_debug("DIU pixclock want %u, got %lu, delta %lu, eps %lu\n",
			 pixclock, got, delta, epsilon);
		return;
	}
	pr_warn("DIU pixclock auto search unsuccessful\n");

	/*
	 * what is the most appropriate action to take when the search
	 * for an available pixel clock which is acceptable to the
	 * monitor has failed?  disable the DIU (clock) or just provide
	 * a "best effort"?  we go with the latter
	 */
	pr_warn("DIU pixclock best effort fallback (backend's choice)\n");
	clk_set_rate(clk_diu, pixclock);
	got = clk_get_rate(clk_diu);
	delta = abs(pixclock - got);
	pr_debug("DIU pixclock want %u, got %lu, delta %lu, eps %lu\n",
		 pixclock, got, delta, epsilon);
}

static enum fsl_diu_monitor_port
mpc512x_valid_monitor_port(enum fsl_diu_monitor_port port)
{
	return FSL_DIU_PORT_DVI;
}

static struct fsl_diu_shared_fb __attribute__ ((__aligned__(8))) diu_shared_fb;

static inline void mpc512x_free_bootmem(struct page *page)
{
	BUG_ON(PageTail(page));
	BUG_ON(page_ref_count(page) > 1);
	free_reserved_page(page);
}

static void mpc512x_release_bootmem(void)
{
	unsigned long addr = diu_shared_fb.fb_phys & PAGE_MASK;
	unsigned long size = diu_shared_fb.fb_len;
	unsigned long start, end;

	if (diu_shared_fb.in_use) {
		start = PFN_UP(addr);
		end = PFN_DOWN(addr + size);

		for (; start < end; start++)
			mpc512x_free_bootmem(pfn_to_page(start));

		diu_shared_fb.in_use = false;
	}
	diu_ops.release_bootmem	= NULL;
}

/*
 * Check if DIU was pre-initialized. If so, perform steps
 * needed to continue displaying through the whole boot process.
 * Move area descriptor and gamma table elsewhere, they are
 * destroyed by bootmem allocator otherwise. The frame buffer
 * address range will be reserved in setup_arch() after bootmem
 * allocator is up.
 */
static void __init mpc512x_init_diu(void)
{
	struct device_node *np;
	struct diu __iomem *diu_reg;
	phys_addr_t desc;
	void __iomem *vaddr;
	unsigned long mode, pix_fmt, res, bpp;
	unsigned long dst;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-diu");
	if (!np) {
		pr_err("No DIU node\n");
		return;
	}

	diu_reg = of_iomap(np, 0);
	of_node_put(np);
	if (!diu_reg) {
		pr_err("Can't map DIU\n");
		return;
	}

	mode = in_be32(&diu_reg->diu_mode);
	if (mode == MFB_MODE0) {
		pr_info("%s: DIU OFF\n", __func__);
		goto out;
	}

	desc = in_be32(&diu_reg->desc[0]);
	vaddr = ioremap(desc, sizeof(struct diu_ad));
	if (!vaddr) {
		pr_err("Can't map DIU area desc.\n");
		goto out;
	}
	memcpy(&diu_shared_fb.ad0, vaddr, sizeof(struct diu_ad));
	/* flush fb area descriptor */
	dst = (unsigned long)&diu_shared_fb.ad0;
	flush_dcache_range(dst, dst + sizeof(struct diu_ad) - 1);

	res = in_be32(&diu_reg->disp_size);
	pix_fmt = in_le32(vaddr);
	bpp = ((pix_fmt >> 16) & 0x3) + 1;
	diu_shared_fb.fb_phys = in_le32(vaddr + 4);
	diu_shared_fb.fb_len = ((res & 0xfff0000) >> 16) * (res & 0xfff) * bpp;
	diu_shared_fb.in_use = true;
	iounmap(vaddr);

	desc = in_be32(&diu_reg->gamma);
	vaddr = ioremap(desc, sizeof(diu_shared_fb.gamma));
	if (!vaddr) {
		pr_err("Can't map DIU area desc.\n");
		diu_shared_fb.in_use = false;
		goto out;
	}
	memcpy(&diu_shared_fb.gamma, vaddr, sizeof(diu_shared_fb.gamma));
	/* flush gamma table */
	dst = (unsigned long)&diu_shared_fb.gamma;
	flush_dcache_range(dst, dst + sizeof(diu_shared_fb.gamma) - 1);

	iounmap(vaddr);
	out_be32(&diu_reg->gamma, virt_to_phys(&diu_shared_fb.gamma));
	out_be32(&diu_reg->desc[1], 0);
	out_be32(&diu_reg->desc[2], 0);
	out_be32(&diu_reg->desc[0], virt_to_phys(&diu_shared_fb.ad0));

out:
	iounmap(diu_reg);
}

static void __init mpc512x_setup_diu(void)
{
	int ret;

	/*
	 * We do not allocate and configure new area for bitmap buffer
	 * because it would require copying bitmap data (splash image)
	 * and so negatively affect boot time. Instead we reserve the
	 * already configured frame buffer area so that it won't be
	 * destroyed. The starting address of the area to reserve and
	 * also it's length is passed to memblock_reserve(). It will be
	 * freed later on first open of fbdev, when splash image is not
	 * needed any more.
	 */
	if (diu_shared_fb.in_use) {
		ret = memblock_reserve(diu_shared_fb.fb_phys,
				       diu_shared_fb.fb_len);
		if (ret) {
			pr_err("%s: reserve bootmem failed\n", __func__);
			diu_shared_fb.in_use = false;
		}
	}

	diu_ops.set_pixel_clock		= mpc512x_set_pixel_clock;
	diu_ops.valid_monitor_port	= mpc512x_valid_monitor_port;
	diu_ops.release_bootmem		= mpc512x_release_bootmem;
}

void __init mpc512x_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-ipic");
	if (!np)
		return;

	ipic_init(np, 0);
	of_node_put(np);

	/*
	 * Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
}

/*
 * Nodes to do bus probe on, soc and localbus
 */
static const struct of_device_id of_bus_ids[] __initconst = {
	{ .compatible = "fsl,mpc5121-immr", },
	{ .compatible = "fsl,mpc5121-localbus", },
	{ .compatible = "fsl,mpc5121-mbx", },
	{ .compatible = "fsl,mpc5121-nfc", },
	{ .compatible = "fsl,mpc5121-sram", },
	{ .compatible = "fsl,mpc5121-pci", },
	{ .compatible = "gpio-leds", },
	{},
};

static void __init mpc512x_declare_of_platform_devices(void)
{
	if (of_platform_bus_probe(NULL, of_bus_ids, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");
}

#define DEFAULT_FIFO_SIZE 16

const char *__init mpc512x_select_psc_compat(void)
{
	if (of_machine_is_compatible("fsl,mpc5121"))
		return "fsl,mpc5121-psc";

	if (of_machine_is_compatible("fsl,mpc5125"))
		return "fsl,mpc5125-psc";

	return NULL;
}

const char *__init mpc512x_select_reset_compat(void)
{
	if (of_machine_is_compatible("fsl,mpc5121"))
		return "fsl,mpc5121-reset";

	if (of_machine_is_compatible("fsl,mpc5125"))
		return "fsl,mpc5125-reset";

	return NULL;
}

static unsigned int __init get_fifo_size(struct device_node *np,
					 char *prop_name)
{
	const unsigned int *fp;

	fp = of_get_property(np, prop_name, NULL);
	if (fp)
		return *fp;

	pr_warn("no %s property in %pOF node, defaulting to %d\n",
		prop_name, np, DEFAULT_FIFO_SIZE);

	return DEFAULT_FIFO_SIZE;
}

#define FIFOC(_base) ((struct mpc512x_psc_fifo __iomem *) \
		    ((u32)(_base) + sizeof(struct mpc52xx_psc)))

/* Init PSC FIFO space for TX and RX slices */
static void __init mpc512x_psc_fifo_init(void)
{
	struct device_node *np;
	void __iomem *psc;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	const char *psc_compat;
	int fifobase = 0; /* current fifo address in 32 bit words */

	psc_compat = mpc512x_select_psc_compat();
	if (!psc_compat) {
		pr_err("%s: no compatible devices found\n", __func__);
		return;
	}

	for_each_compatible_node(np, NULL, psc_compat) {
		tx_fifo_size = get_fifo_size(np, "fsl,tx-fifo-size");
		rx_fifo_size = get_fifo_size(np, "fsl,rx-fifo-size");

		/* size in register is in 4 byte units */
		tx_fifo_size /= 4;
		rx_fifo_size /= 4;
		if (!tx_fifo_size)
			tx_fifo_size = 1;
		if (!rx_fifo_size)
			rx_fifo_size = 1;

		psc = of_iomap(np, 0);
		if (!psc) {
			pr_err("%s: Can't map %pOF device\n",
				__func__, np);
			continue;
		}

		/* FIFO space is 4KiB, check if requested size is available */
		if ((fifobase + tx_fifo_size + rx_fifo_size) > 0x1000) {
			pr_err("%s: no fifo space available for %pOF\n",
				__func__, np);
			iounmap(psc);
			/*
			 * chances are that another device requests less
			 * fifo space, so we continue.
			 */
			continue;
		}

		/* set tx and rx fifo size registers */
		out_be32(&FIFOC(psc)->txsz, (fifobase << 16) | tx_fifo_size);
		fifobase += tx_fifo_size;
		out_be32(&FIFOC(psc)->rxsz, (fifobase << 16) | rx_fifo_size);
		fifobase += rx_fifo_size;

		/* reset and enable the slices */
		out_be32(&FIFOC(psc)->txcmd, 0x80);
		out_be32(&FIFOC(psc)->txcmd, 0x01);
		out_be32(&FIFOC(psc)->rxcmd, 0x80);
		out_be32(&FIFOC(psc)->rxcmd, 0x01);

		iounmap(psc);
	}
}

void __init mpc512x_init_early(void)
{
	mpc512x_restart_init();
	if (IS_ENABLED(CONFIG_FB_FSL_DIU))
		mpc512x_init_diu();
}

void __init mpc512x_init(void)
{
	mpc5121_clk_init();
	mpc512x_declare_of_platform_devices();
	mpc512x_psc_fifo_init();
}

void __init mpc512x_setup_arch(void)
{
	if (IS_ENABLED(CONFIG_FB_FSL_DIU))
		mpc512x_setup_diu();
}

/**
 * mpc512x_cs_config - Setup chip select configuration
 * @cs: chip select number
 * @val: chip select configuration value
 *
 * Perform chip select configuration for devices on LocalPlus Bus.
 * Intended to dynamically reconfigure the chip select parameters
 * for configurable devices on the bus.
 */
int mpc512x_cs_config(unsigned int cs, u32 val)
{
	static struct mpc512x_lpc __iomem *lpc;
	struct device_node *np;

	if (cs > 7)
		return -EINVAL;

	if (!lpc) {
		np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-lpc");
		lpc = of_iomap(np, 0);
		of_node_put(np);
		if (!lpc)
			return -ENOMEM;
	}

	out_be32(&lpc->cs_cfg[cs], val);
	return 0;
}
EXPORT_SYMBOL(mpc512x_cs_config);
