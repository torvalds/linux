/*
 * linux/arch/arm/plat-omap/sram.c
 *
 * OMAP SRAM detection and management
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include <asm/arch/sram.h>

#define OMAP1_SRAM_PA		0x20000000
#define OMAP1_SRAM_VA		0xd0000000
#define OMAP2_SRAM_PA		0x40200000
#define OMAP2_SRAM_VA		0xd0000000

#define SRAM_BOOTLOADER_SZ	0x80

static unsigned long omap_sram_base;
static unsigned long omap_sram_size;
static unsigned long omap_sram_ceil;

/*
 * The amount of SRAM depends on the core type.
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 */
void __init omap_detect_sram(void)
{
	if (!cpu_is_omap24xx())
		omap_sram_base = OMAP1_SRAM_VA;
	else
		omap_sram_base = OMAP2_SRAM_VA;

	if (cpu_is_omap730())
		omap_sram_size = 0x32000;	/* 200K */
	else if (cpu_is_omap15xx())
		omap_sram_size = 0x30000;	/* 192K */
	else if (cpu_is_omap1610() || cpu_is_omap1621() || cpu_is_omap1710())
		omap_sram_size = 0x4000;	/* 16K */
	else if (cpu_is_omap1611())
		omap_sram_size = 0x3e800;	/* 250K */
	else if (cpu_is_omap2420())
		omap_sram_size = 0xa0014;	/* 640K */
	else {
		printk(KERN_ERR "Could not detect SRAM size\n");
		omap_sram_size = 0x4000;
	}

	omap_sram_ceil = omap_sram_base + omap_sram_size;
}

static struct map_desc omap_sram_io_desc[] __initdata = {
	{	/* .length gets filled in at runtime */
		.virtual	= OMAP1_SRAM_VA,
		.pfn		= __phys_to_pfn(OMAP1_SRAM_PA),
		.type		= MT_DEVICE
	}
};

/*
 * In order to use last 2kB of SRAM on 1611b, we must round the size
 * up to multiple of PAGE_SIZE. We cannot use ioremap for SRAM, as
 * clock init needs SRAM early.
 */
void __init omap_map_sram(void)
{
	if (omap_sram_size == 0)
		return;

	if (cpu_is_omap24xx()) {
		omap_sram_io_desc[0].virtual = OMAP2_SRAM_VA;
		omap_sram_io_desc[0].pfn = __phys_to_pfn(OMAP2_SRAM_PA);
	}

	omap_sram_io_desc[0].length = (omap_sram_size + PAGE_SIZE-1)/PAGE_SIZE;
	omap_sram_io_desc[0].length *= PAGE_SIZE;
	iotable_init(omap_sram_io_desc, ARRAY_SIZE(omap_sram_io_desc));

	printk(KERN_INFO "SRAM: Mapped pa 0x%08lx to va 0x%08lx size: 0x%lx\n",
	       omap_sram_io_desc[0].pfn, omap_sram_io_desc[0].virtual,
	       omap_sram_io_desc[0].length);

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset((void *)omap_sram_base + SRAM_BOOTLOADER_SZ, 0,
	       omap_sram_size - SRAM_BOOTLOADER_SZ);
}

void * omap_sram_push(void * start, unsigned long size)
{
	if (size > (omap_sram_ceil - (omap_sram_base + SRAM_BOOTLOADER_SZ))) {
		printk(KERN_ERR "Not enough space in SRAM\n");
		return NULL;
	}
	omap_sram_ceil -= size;
	omap_sram_ceil &= ~0x3;
	memcpy((void *)omap_sram_ceil, start, size);

	return (void *)omap_sram_ceil;
}

static void omap_sram_error(void)
{
	panic("Uninitialized SRAM function\n");
}

#ifdef CONFIG_ARCH_OMAP1

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	if (!_omap_sram_reprogram_clock)
		omap_sram_error();

	return _omap_sram_reprogram_clock(dpllctl, ckctl);
}

int __init omap1_sram_init(void)
{
	_omap_sram_reprogram_clock = omap_sram_push(sram_reprogram_clock,
						    sram_reprogram_clock_sz);

	return 0;
}

#else
#define omap1_sram_init()	do {} while (0)
#endif

#ifdef CONFIG_ARCH_OMAP2

static void (*_omap2_sram_ddr_init)(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
			      u32 base_cs, u32 force_unlock);

void omap2_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
		   u32 base_cs, u32 force_unlock)
{
	if (!_omap2_sram_ddr_init)
		omap_sram_error();

	return _omap2_sram_ddr_init(slow_dll_ctrl, fast_dll_ctrl,
				    base_cs, force_unlock);
}

static void (*_omap2_sram_reprogram_sdrc)(u32 perf_level, u32 dll_val,
					  u32 mem_type);

void omap2_sram_reprogram_sdrc(u32 perf_level, u32 dll_val, u32 mem_type)
{
	if (!_omap2_sram_reprogram_sdrc)
		omap_sram_error();

	return _omap2_sram_reprogram_sdrc(perf_level, dll_val, mem_type);
}

static u32 (*_omap2_set_prcm)(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass);

u32 omap2_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass)
{
	if (!_omap2_set_prcm)
		omap_sram_error();

	return _omap2_set_prcm(dpll_ctrl_val, sdrc_rfr_val, bypass);
}

int __init omap2_sram_init(void)
{
	_omap2_sram_ddr_init = omap_sram_push(sram_ddr_init, sram_ddr_init_sz);

	_omap2_sram_reprogram_sdrc = omap_sram_push(sram_reprogram_sdrc,
						    sram_reprogram_sdrc_sz);
	_omap2_set_prcm = omap_sram_push(sram_set_prcm, sram_set_prcm_sz);

	return 0;
}
#else
#define omap2_sram_init()	do {} while (0)
#endif

int __init omap_sram_init(void)
{
	omap_detect_sram();
	omap_map_sram();

	if (!cpu_is_omap24xx())
		omap1_sram_init();
	else
		omap2_sram_init();

	return 0;
}
