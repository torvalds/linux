/*
 *  arch/arm/mach-lh7a40x/clcd.c
 *
 *  Copyright (C) 2004 Marc Singer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>

//#include <linux/module.h>
//#include <linux/time.h>

//#include <asm/mach/time.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <asm/system.h>
#include <mach/hardware.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>

#define HRTFTC_HRSETUP		__REG(HRTFTC_PHYS + 0x00)
#define HRTFTC_HRCON		__REG(HRTFTC_PHYS + 0x04)
#define HRTFTC_HRTIMING1	__REG(HRTFTC_PHYS + 0x08)
#define HRTFTC_HRTIMING2	__REG(HRTFTC_PHYS + 0x0c)

#define ALI_SETUP		__REG(ALI_PHYS + 0x00)
#define ALI_CONTROL		__REG(ALI_PHYS + 0x04)
#define ALI_TIMING1		__REG(ALI_PHYS + 0x08)
#define ALI_TIMING2		__REG(ALI_PHYS + 0x0c)

#include "lcd-panel.h"

static void lh7a40x_clcd_disable (struct clcd_fb *fb)
{
#if defined (CONFIG_MACH_LPD7A400)
	CPLD_CONTROL &= ~(1<<1);	/* Disable LCD Vee */
#endif

#if defined (CONFIG_MACH_LPD7A404)
	GPIO_PCD  &= ~(1<<3);		/* Disable LCD Vee */
#endif

#if defined (CONFIG_ARCH_LH7A400)
	HRTFTC_HRSETUP &= ~(1<<13);	/* Disable HRTFT controller */
#endif

#if defined (CONFIG_ARCH_LH7A404)
	ALI_SETUP &= ~(1<<13);		/* Disable ALI */
#endif
}

static void lh7a40x_clcd_enable (struct clcd_fb *fb)
{
	struct clcd_panel_extra* extra
		= (struct clcd_panel_extra*) fb->board_data;

#if defined (CONFIG_MACH_LPD7A400)
	CPLD_CONTROL |= (1<<1);		/* Enable LCD Vee */
#endif

#if defined (CONFIG_MACH_LPD7A404)
	GPIO_PCDD &= ~(1<<3);		/* Enable LCD Vee */
	GPIO_PCD  |=  (1<<3);
#endif

#if defined (CONFIG_ARCH_LH7A400)

	if (extra) {
		HRTFTC_HRSETUP
			= (1 << 13)
			| ((fb->fb.var.xres - 1) << 4)
			| 0xc
			| (extra->hrmode ? 1 : 0);
		HRTFTC_HRCON
			= ((extra->clsen ? 1 : 0) << 1)
			| ((extra->spsen ? 1 : 0) << 0);
		HRTFTC_HRTIMING1
			= (extra->pcdel << 8)
			| (extra->revdel << 4)
			| (extra->lpdel << 0);
		HRTFTC_HRTIMING2
			= (extra->spldel << 9)
			| (extra->pc2del << 0);
	}
	else
		HRTFTC_HRSETUP
			= (1 << 13)
			| 0xc;
#endif

#if defined (CONFIG_ARCH_LH7A404)

	if (extra) {
		ALI_SETUP
			= (1 << 13)
			| ((fb->fb.var.xres - 1) << 4)
			| 0xc
			| (extra->hrmode ? 1 : 0);
		ALI_CONTROL
			= ((extra->clsen ? 1 : 0) << 1)
			| ((extra->spsen ? 1 : 0) << 0);
		ALI_TIMING1
			= (extra->pcdel << 8)
			| (extra->revdel << 4)
			| (extra->lpdel << 0);
		ALI_TIMING2
			= (extra->spldel << 9)
			| (extra->pc2del << 0);
	}
	else
		ALI_SETUP
			= (1 << 13)
			| 0xc;
#endif

}

#define FRAMESIZE(s) (((s) + PAGE_SIZE - 1)&PAGE_MASK)

static int lh7a40x_clcd_setup (struct clcd_fb *fb)
{
	dma_addr_t dma;
	u32 len = FRAMESIZE (lcd_panel.mode.xres*lcd_panel.mode.yres
			     *(lcd_panel.bpp/8));

	fb->panel = &lcd_panel;

		/* Enforce the sync polarity defaults */
	if (!(fb->panel->tim2 & TIM2_IHS))
		fb->fb.var.sync |= FB_SYNC_HOR_HIGH_ACT;
	if (!(fb->panel->tim2 & TIM2_IVS))
		fb->fb.var.sync |= FB_SYNC_VERT_HIGH_ACT;

#if defined (HAS_LCD_PANEL_EXTRA)
	fb->board_data = &lcd_panel_extra;
#endif

	fb->fb.screen_base
		= dma_alloc_writecombine (&fb->dev->dev, len,
					  &dma, GFP_KERNEL);
	printk ("CLCD: LCD setup fb virt 0x%p phys 0x%p l %x io 0x%p \n",
		fb->fb.screen_base, (void*) dma, len,
		(void*) io_p2v (CLCDC_PHYS));
	printk ("CLCD: pixclock %d\n", lcd_panel.mode.pixclock);

	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

#if defined (USE_RGB555)
	fb->fb.var.green.length = 5; /* Panel uses RGB 5:5:5 */
#endif

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = len;

		/* Drive PE4 high to prevent CPLD crash */
	GPIO_PEDD |= (1<<4);
	GPIO_PED  |= (1<<4);

	GPIO_PINMUX |= (1<<1) | (1<<0); /* LCDVD[15:4] */

//	fb->fb.fbops->fb_check_var (&fb->fb.var, &fb->fb);
//	fb->fb.fbops->fb_set_par (&fb->fb);

	return 0;
}

static int lh7a40x_clcd_mmap (struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
				     fb->fb.screen_base,
				     fb->fb.fix.smem_start,
				     fb->fb.fix.smem_len);
}

static void lh7a40x_clcd_remove (struct clcd_fb *fb)
{
	dma_free_writecombine (&fb->dev->dev, fb->fb.fix.smem_len,
			       fb->fb.screen_base, fb->fb.fix.smem_start);
}

static struct clcd_board clcd_platform_data = {
	.name		= "lh7a40x FB",
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.enable		= lh7a40x_clcd_enable,
	.setup		= lh7a40x_clcd_setup,
	.mmap		= lh7a40x_clcd_mmap,
	.remove		= lh7a40x_clcd_remove,
	.disable	= lh7a40x_clcd_disable,
};

#define IRQ_CLCDC (IRQ_LCDINTR)

#define AMBA_DEVICE(name,busid,base,plat,pid)			\
static struct amba_device name##_device = {			\
	.dev = {						\
		.coherent_dma_mask = ~0,			\
		.init_name = busid,				\
		.platform_data = plat,				\
		},						\
	.res = {						\
		.start	= base##_PHYS,				\
		.end	= (base##_PHYS) + (4*1024) - 1,		\
		.flags	= IORESOURCE_MEM,			\
		},						\
	.dma_mask	= ~0,					\
	.irq		= { IRQ_##base, },			\
	/* .dma		= base##_DMA,*/				\
	.periphid = pid,					\
}

AMBA_DEVICE(clcd,  "cldc-lh7a40x",  CLCDC,     &clcd_platform_data, 0x41110);

static struct amba_device *amba_devs[] __initdata = {
	&clcd_device,
};

void __init lh7a40x_clcd_init (void)
{
	int i;
	int result;
	printk ("CLCD: registering amba devices\n");
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		result = amba_device_register(d, &iomem_resource);
		printk ("  %d -> %d\n", i ,result);
	}
}
