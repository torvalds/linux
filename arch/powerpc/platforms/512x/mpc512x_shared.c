/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby <jrigby@freescale.com>
 *
 * Description:
 * MPC512x Shared code
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/fsl-diu-fb.h>
#include <linux/bootmem.h>
#include <sysdev/fsl_soc.h>

#include <asm/cacheflush.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/mpc5121.h>
#include <asm/mpc52xx_psc.h>

#include "mpc512x.h"

static struct mpc512x_reset_module __iomem *reset_module_base;

static void __init mpc512x_restart_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-reset");
	if (!np)
		return;

	reset_module_base = of_iomap(np, 0);
	of_node_put(np);
}

void mpc512x_restart(char *cmd)
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

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

struct fsl_diu_shared_fb {
	u8		gamma[0x300];	/* 32-bit aligned! */
	struct diu_ad	ad0;		/* 32-bit aligned! */
	phys_addr_t	fb_phys;
	size_t		fb_len;
	bool		in_use;
};

void mpc512x_set_monitor_port(enum fsl_diu_monitor_port port)
{
}

#define DIU_DIV_MASK	0x000000ff
void mpc512x_set_pixel_clock(unsigned int pixclock)
{
	unsigned long bestval, bestfreq, speed, busfreq;
	unsigned long minpixclock, maxpixclock, pixval;
	struct mpc512x_ccm __iomem *ccm;
	struct device_node *np;
	u32 temp;
	long err;
	int i;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-clock");
	if (!np) {
		pr_err("Can't find clock control module.\n");
		return;
	}

	ccm = of_iomap(np, 0);
	of_node_put(np);
	if (!ccm) {
		pr_err("Can't map clock control module reg.\n");
		return;
	}

	np = of_find_node_by_type(NULL, "cpu");
	if (np) {
		const unsigned int *prop =
			of_get_property(np, "bus-frequency", NULL);

		of_node_put(np);
		if (prop) {
			busfreq = *prop;
		} else {
			pr_err("Can't get bus-frequency property\n");
			return;
		}
	} else {
		pr_err("Can't find 'cpu' node.\n");
		return;
	}

	/* Pixel Clock configuration */
	pr_debug("DIU: Bus Frequency = %lu\n", busfreq);
	speed = busfreq * 4; /* DIU_DIV ratio is 4 * CSB_CLK / DIU_CLK */

	/* Calculate the pixel clock with the smallest error */
	/* calculate the following in steps to avoid overflow */
	pr_debug("DIU pixclock in ps - %d\n", pixclock);
	temp = (1000000000 / pixclock) * 1000;
	pixclock = temp;
	pr_debug("DIU pixclock freq - %u\n", pixclock);

	temp = temp / 20; /* pixclock * 0.05 */
	pr_debug("deviation = %d\n", temp);
	minpixclock = pixclock - temp;
	maxpixclock = pixclock + temp;
	pr_debug("DIU minpixclock - %lu\n", minpixclock);
	pr_debug("DIU maxpixclock - %lu\n", maxpixclock);
	pixval = speed/pixclock;
	pr_debug("DIU pixval = %lu\n", pixval);

	err = LONG_MAX;
	bestval = pixval;
	pr_debug("DIU bestval = %lu\n", bestval);

	bestfreq = 0;
	for (i = -1; i <= 1; i++) {
		temp = speed / (pixval+i);
		pr_debug("DIU test pixval i=%d, pixval=%lu, temp freq. = %u\n",
			i, pixval, temp);
		if ((temp < minpixclock) || (temp > maxpixclock))
			pr_debug("DIU exceeds monitor range (%lu to %lu)\n",
				minpixclock, maxpixclock);
		else if (abs(temp - pixclock) < err) {
			pr_debug("Entered the else if block %d\n", i);
			err = abs(temp - pixclock);
			bestval = pixval + i;
			bestfreq = temp;
		}
	}

	pr_debug("DIU chose = %lx\n", bestval);
	pr_debug("DIU error = %ld\n NomPixClk ", err);
	pr_debug("DIU: Best Freq = %lx\n", bestfreq);
	/* Modify DIU_DIV in CCM SCFR1 */
	temp = in_be32(&ccm->scfr1);
	pr_debug("DIU: Current value of SCFR1: 0x%08x\n", temp);
	temp &= ~DIU_DIV_MASK;
	temp |= (bestval & DIU_DIV_MASK);
	out_be32(&ccm->scfr1, temp);
	pr_debug("DIU: Modified value of SCFR1: 0x%08x\n", temp);
	iounmap(ccm);
}

enum fsl_diu_monitor_port
mpc512x_valid_monitor_port(enum fsl_diu_monitor_port port)
{
	return FSL_DIU_PORT_DVI;
}

static struct fsl_diu_shared_fb __attribute__ ((__aligned__(8))) diu_shared_fb;

static inline void mpc512x_free_bootmem(struct page *page)
{
	__ClearPageReserved(page);
	BUG_ON(PageTail(page));
	BUG_ON(atomic_read(&page->_count) > 1);
	atomic_set(&page->_count, 1);
	__free_page(page);
	totalram_pages++;
}

void mpc512x_release_bootmem(void)
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
void __init mpc512x_init_diu(void)
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

void __init mpc512x_setup_diu(void)
{
	int ret;

	/*
	 * We do not allocate and configure new area for bitmap buffer
	 * because it would requere copying bitmap data (splash image)
	 * and so negatively affect boot time. Instead we reserve the
	 * already configured frame buffer area so that it won't be
	 * destroyed. The starting address of the area to reserve and
	 * also it's length is passed to reserve_bootmem(). It will be
	 * freed later on first open of fbdev, when splash image is not
	 * needed any more.
	 */
	if (diu_shared_fb.in_use) {
		ret = reserve_bootmem(diu_shared_fb.fb_phys,
				      diu_shared_fb.fb_len,
				      BOOTMEM_EXCLUSIVE);
		if (ret) {
			pr_err("%s: reserve bootmem failed\n", __func__);
			diu_shared_fb.in_use = false;
		}
	}

	diu_ops.set_monitor_port	= mpc512x_set_monitor_port;
	diu_ops.set_pixel_clock		= mpc512x_set_pixel_clock;
	diu_ops.valid_monitor_port	= mpc512x_valid_monitor_port;
	diu_ops.release_bootmem		= mpc512x_release_bootmem;
}

#endif

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
static struct of_device_id __initdata of_bus_ids[] = {
	{ .compatible = "fsl,mpc5121-immr", },
	{ .compatible = "fsl,mpc5121-localbus", },
	{},
};

void __init mpc512x_declare_of_platform_devices(void)
{
	struct device_node *np;

	if (of_platform_bus_probe(NULL, of_bus_ids, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-nfc");
	if (np) {
		of_platform_device_create(np, NULL, NULL);
		of_node_put(np);
	}
}

#define DEFAULT_FIFO_SIZE 16

static unsigned int __init get_fifo_size(struct device_node *np,
					 char *prop_name)
{
	const unsigned int *fp;

	fp = of_get_property(np, prop_name, NULL);
	if (fp)
		return *fp;

	pr_warning("no %s property in %s node, defaulting to %d\n",
		   prop_name, np->full_name, DEFAULT_FIFO_SIZE);

	return DEFAULT_FIFO_SIZE;
}

#define FIFOC(_base) ((struct mpc512x_psc_fifo __iomem *) \
		    ((u32)(_base) + sizeof(struct mpc52xx_psc)))

/* Init PSC FIFO space for TX and RX slices */
void __init mpc512x_psc_fifo_init(void)
{
	struct device_node *np;
	void __iomem *psc;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	int fifobase = 0; /* current fifo address in 32 bit words */

	for_each_compatible_node(np, NULL, "fsl,mpc5121-psc") {
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
			pr_err("%s: Can't map %s device\n",
				__func__, np->full_name);
			continue;
		}

		/* FIFO space is 4KiB, check if requested size is available */
		if ((fifobase + tx_fifo_size + rx_fifo_size) > 0x1000) {
			pr_err("%s: no fifo space available for %s\n",
				__func__, np->full_name);
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

void __init mpc512x_init(void)
{
	mpc512x_declare_of_platform_devices();
	mpc5121_clk_init();
	mpc512x_restart_init();
	mpc512x_psc_fifo_init();
}
