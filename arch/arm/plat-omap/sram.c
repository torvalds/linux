/*
 * linux/arch/arm/plat-omap/sram.c
 *
 * OMAP SRAM detection and management
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/tlb.h>
#include <asm/cacheflush.h>

#include <asm/mach/map.h>

#include <plat/sram.h>
#include <plat/board.h>
#include <plat/cpu.h>

#include "sram.h"

/* XXX These "sideways" includes will disappear when sram.c becomes a driver */
#include "../mach-omap2/iomap.h"
#include "../mach-omap2/prm2xxx_3xxx.h"
#include "../mach-omap2/sdrc.h"

#define OMAP1_SRAM_PA		0x20000000
#define OMAP2_SRAM_PUB_PA	(OMAP2_SRAM_PA + 0xf800)
#define OMAP3_SRAM_PUB_PA       (OMAP3_SRAM_PA + 0x8000)
#ifdef CONFIG_OMAP4_ERRATA_I688
#define OMAP4_SRAM_PUB_PA	OMAP4_SRAM_PA
#else
#define OMAP4_SRAM_PUB_PA	(OMAP4_SRAM_PA + 0x4000)
#endif

#if defined(CONFIG_ARCH_OMAP2PLUS)
#define SRAM_BOOTLOADER_SZ	0x00
#else
#define SRAM_BOOTLOADER_SZ	0x80
#endif

#define OMAP24XX_VA_REQINFOPERM0	OMAP2_L3_IO_ADDRESS(0x68005048)
#define OMAP24XX_VA_READPERM0		OMAP2_L3_IO_ADDRESS(0x68005050)
#define OMAP24XX_VA_WRITEPERM0		OMAP2_L3_IO_ADDRESS(0x68005058)

#define OMAP34XX_VA_REQINFOPERM0	OMAP2_L3_IO_ADDRESS(0x68012848)
#define OMAP34XX_VA_READPERM0		OMAP2_L3_IO_ADDRESS(0x68012850)
#define OMAP34XX_VA_WRITEPERM0		OMAP2_L3_IO_ADDRESS(0x68012858)
#define OMAP34XX_VA_ADDR_MATCH2		OMAP2_L3_IO_ADDRESS(0x68012880)
#define OMAP34XX_VA_SMS_RG_ATT0		OMAP2_L3_IO_ADDRESS(0x6C000048)

#define GP_DEVICE		0x300

#define ROUND_DOWN(value,boundary)	((value) & (~((boundary)-1)))

static unsigned long omap_sram_start;
static void __iomem *omap_sram_base;
static unsigned long omap_sram_size;
static void __iomem *omap_sram_ceil;

/*
 * Depending on the target RAMFS firewall setup, the public usable amount of
 * SRAM varies.  The default accessible size for all device types is 2k. A GP
 * device allows ARM11 but not other initiators for full size. This
 * functionality seems ok until some nice security API happens.
 */
static int is_sram_locked(void)
{
	if (OMAP2_DEVICE_TYPE_GP == omap_type()) {
		/* RAMFW: R/W access to all initiators for all qualifier sets */
		if (cpu_is_omap242x()) {
			__raw_writel(0xFF, OMAP24XX_VA_REQINFOPERM0); /* all q-vects */
			__raw_writel(0xCFDE, OMAP24XX_VA_READPERM0);  /* all i-read */
			__raw_writel(0xCFDE, OMAP24XX_VA_WRITEPERM0); /* all i-write */
		}
		if (cpu_is_omap34xx()) {
			__raw_writel(0xFFFF, OMAP34XX_VA_REQINFOPERM0); /* all q-vects */
			__raw_writel(0xFFFF, OMAP34XX_VA_READPERM0);  /* all i-read */
			__raw_writel(0xFFFF, OMAP34XX_VA_WRITEPERM0); /* all i-write */
			__raw_writel(0x0, OMAP34XX_VA_ADDR_MATCH2);
			__raw_writel(0xFFFFFFFF, OMAP34XX_VA_SMS_RG_ATT0);
		}
		return 0;
	} else
		return 1; /* assume locked with no PPA or security driver */
}

/*
 * The amount of SRAM depends on the core type.
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 */
static void __init omap_detect_sram(void)
{
	if (cpu_class_is_omap2()) {
		if (is_sram_locked()) {
			if (cpu_is_omap34xx()) {
				omap_sram_start = OMAP3_SRAM_PUB_PA;
				if ((omap_type() == OMAP2_DEVICE_TYPE_EMU) ||
				    (omap_type() == OMAP2_DEVICE_TYPE_SEC)) {
					omap_sram_size = 0x7000; /* 28K */
				} else {
					omap_sram_size = 0x8000; /* 32K */
				}
			} else if (cpu_is_omap44xx()) {
				omap_sram_start = OMAP4_SRAM_PUB_PA;
				omap_sram_size = 0xa000; /* 40K */
			} else {
				omap_sram_start = OMAP2_SRAM_PUB_PA;
				omap_sram_size = 0x800; /* 2K */
			}
		} else {
			if (soc_is_am33xx()) {
				omap_sram_start = AM33XX_SRAM_PA;
				omap_sram_size = 0x10000; /* 64K */
			} else if (cpu_is_omap34xx()) {
				omap_sram_start = OMAP3_SRAM_PA;
				omap_sram_size = 0x10000; /* 64K */
			} else if (cpu_is_omap44xx()) {
				omap_sram_start = OMAP4_SRAM_PA;
				omap_sram_size = 0xe000; /* 56K */
			} else {
				omap_sram_start = OMAP2_SRAM_PA;
				if (cpu_is_omap242x())
					omap_sram_size = 0xa0000; /* 640K */
				else if (cpu_is_omap243x())
					omap_sram_size = 0x10000; /* 64K */
			}
		}
	} else {
		omap_sram_start = OMAP1_SRAM_PA;

		if (cpu_is_omap7xx())
			omap_sram_size = 0x32000;	/* 200K */
		else if (cpu_is_omap15xx())
			omap_sram_size = 0x30000;	/* 192K */
		else if (cpu_is_omap1610() || cpu_is_omap1611() ||
				cpu_is_omap1621() || cpu_is_omap1710())
			omap_sram_size = 0x4000;	/* 16K */
		else {
			pr_err("Could not detect SRAM size\n");
			omap_sram_size = 0x4000;
		}
	}
}

/*
 * Note that we cannot use ioremap for SRAM, as clock init needs SRAM early.
 */
static void __init omap_map_sram(void)
{
	int cached = 1;

	if (omap_sram_size == 0)
		return;

#ifdef CONFIG_OMAP4_ERRATA_I688
		omap_sram_start += PAGE_SIZE;
		omap_sram_size -= SZ_16K;
#endif
	if (cpu_is_omap34xx()) {
		/*
		 * SRAM must be marked as non-cached on OMAP3 since the
		 * CORE DPLL M2 divider change code (in SRAM) runs with the
		 * SDRAM controller disabled, and if it is marked cached,
		 * the ARM may attempt to write cache lines back to SDRAM
		 * which will cause the system to hang.
		 */
		cached = 0;
	}

	omap_sram_start = ROUND_DOWN(omap_sram_start, PAGE_SIZE);
	omap_sram_base = __arm_ioremap_exec(omap_sram_start, omap_sram_size,
						cached);
	if (!omap_sram_base) {
		pr_err("SRAM: Could not map\n");
		return;
	}

	omap_sram_ceil = omap_sram_base + omap_sram_size;

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset_io(omap_sram_base + SRAM_BOOTLOADER_SZ, 0,
		  omap_sram_size - SRAM_BOOTLOADER_SZ);
}

/*
 * Memory allocator for SRAM: calculates the new ceiling address
 * for pushing a function using the fncpy API.
 *
 * Note that fncpy requires the returned address to be aligned
 * to an 8-byte boundary.
 */
void *omap_sram_push_address(unsigned long size)
{
	unsigned long available, new_ceil = (unsigned long)omap_sram_ceil;

	available = omap_sram_ceil - (omap_sram_base + SRAM_BOOTLOADER_SZ);

	if (size > available) {
		pr_err("Not enough space in SRAM\n");
		return NULL;
	}

	new_ceil -= size;
	new_ceil = ROUND_DOWN(new_ceil, FNCPY_ALIGN);
	omap_sram_ceil = IOMEM(new_ceil);

	return (void *)omap_sram_ceil;
}

#ifdef CONFIG_ARCH_OMAP1

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	BUG_ON(!_omap_sram_reprogram_clock);
	/* On 730, bit 13 must always be 1 */
	if (cpu_is_omap7xx())
		ckctl |= 0x2000;
	_omap_sram_reprogram_clock(dpllctl, ckctl);
}

static int __init omap1_sram_init(void)
{
	_omap_sram_reprogram_clock =
			omap_sram_push(omap1_sram_reprogram_clock,
					omap1_sram_reprogram_clock_sz);

	return 0;
}

#else
#define omap1_sram_init()	do {} while (0)
#endif

#if defined(CONFIG_ARCH_OMAP2)

static void (*_omap2_sram_ddr_init)(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
			      u32 base_cs, u32 force_unlock);

void omap2_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
		   u32 base_cs, u32 force_unlock)
{
	BUG_ON(!_omap2_sram_ddr_init);
	_omap2_sram_ddr_init(slow_dll_ctrl, fast_dll_ctrl,
			     base_cs, force_unlock);
}

static void (*_omap2_sram_reprogram_sdrc)(u32 perf_level, u32 dll_val,
					  u32 mem_type);

void omap2_sram_reprogram_sdrc(u32 perf_level, u32 dll_val, u32 mem_type)
{
	BUG_ON(!_omap2_sram_reprogram_sdrc);
	_omap2_sram_reprogram_sdrc(perf_level, dll_val, mem_type);
}

static u32 (*_omap2_set_prcm)(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass);

u32 omap2_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass)
{
	BUG_ON(!_omap2_set_prcm);
	return _omap2_set_prcm(dpll_ctrl_val, sdrc_rfr_val, bypass);
}
#endif

#ifdef CONFIG_SOC_OMAP2420
static int __init omap242x_sram_init(void)
{
	_omap2_sram_ddr_init = omap_sram_push(omap242x_sram_ddr_init,
					omap242x_sram_ddr_init_sz);

	_omap2_sram_reprogram_sdrc = omap_sram_push(omap242x_sram_reprogram_sdrc,
					    omap242x_sram_reprogram_sdrc_sz);

	_omap2_set_prcm = omap_sram_push(omap242x_sram_set_prcm,
					 omap242x_sram_set_prcm_sz);

	return 0;
}
#else
static inline int omap242x_sram_init(void)
{
	return 0;
}
#endif

#ifdef CONFIG_SOC_OMAP2430
static int __init omap243x_sram_init(void)
{
	_omap2_sram_ddr_init = omap_sram_push(omap243x_sram_ddr_init,
					omap243x_sram_ddr_init_sz);

	_omap2_sram_reprogram_sdrc = omap_sram_push(omap243x_sram_reprogram_sdrc,
					    omap243x_sram_reprogram_sdrc_sz);

	_omap2_set_prcm = omap_sram_push(omap243x_sram_set_prcm,
					 omap243x_sram_set_prcm_sz);

	return 0;
}
#else
static inline int omap243x_sram_init(void)
{
	return 0;
}
#endif

#ifdef CONFIG_ARCH_OMAP3

static u32 (*_omap3_sram_configure_core_dpll)(
			u32 m2, u32 unlock_dll, u32 f, u32 inc,
			u32 sdrc_rfr_ctrl_0, u32 sdrc_actim_ctrl_a_0,
			u32 sdrc_actim_ctrl_b_0, u32 sdrc_mr_0,
			u32 sdrc_rfr_ctrl_1, u32 sdrc_actim_ctrl_a_1,
			u32 sdrc_actim_ctrl_b_1, u32 sdrc_mr_1);

u32 omap3_configure_core_dpll(u32 m2, u32 unlock_dll, u32 f, u32 inc,
			u32 sdrc_rfr_ctrl_0, u32 sdrc_actim_ctrl_a_0,
			u32 sdrc_actim_ctrl_b_0, u32 sdrc_mr_0,
			u32 sdrc_rfr_ctrl_1, u32 sdrc_actim_ctrl_a_1,
			u32 sdrc_actim_ctrl_b_1, u32 sdrc_mr_1)
{
	BUG_ON(!_omap3_sram_configure_core_dpll);
	return _omap3_sram_configure_core_dpll(
			m2, unlock_dll, f, inc,
			sdrc_rfr_ctrl_0, sdrc_actim_ctrl_a_0,
			sdrc_actim_ctrl_b_0, sdrc_mr_0,
			sdrc_rfr_ctrl_1, sdrc_actim_ctrl_a_1,
			sdrc_actim_ctrl_b_1, sdrc_mr_1);
}

void omap3_sram_restore_context(void)
{
	omap_sram_ceil = omap_sram_base + omap_sram_size;

	_omap3_sram_configure_core_dpll =
		omap_sram_push(omap3_sram_configure_core_dpll,
			       omap3_sram_configure_core_dpll_sz);
	omap_push_sram_idle();
}

static inline int omap34xx_sram_init(void)
{
	omap3_sram_restore_context();
	return 0;
}
#else
static inline int omap34xx_sram_init(void)
{
	return 0;
}
#endif /* CONFIG_ARCH_OMAP3 */

static inline int am33xx_sram_init(void)
{
	return 0;
}

int __init omap_sram_init(void)
{
	omap_detect_sram();
	omap_map_sram();

	if (!(cpu_class_is_omap2()))
		omap1_sram_init();
	else if (cpu_is_omap242x())
		omap242x_sram_init();
	else if (cpu_is_omap2430())
		omap243x_sram_init();
	else if (soc_is_am33xx())
		am33xx_sram_init();
	else if (cpu_is_omap34xx())
		omap34xx_sram_init();

	return 0;
}
