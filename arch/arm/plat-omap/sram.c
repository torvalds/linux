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
#include <linux/omapfb.h>

#include <asm/tlb.h>
#include <asm/cacheflush.h>

#include <asm/mach/map.h>

#include <plat/sram.h>
#include <plat/board.h>
#include <plat/cpu.h>
#include <plat/vram.h>

#include "sram.h"
#include "fb.h"

/* XXX These "sideways" includes are a sign that something is wrong */
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
# include "../mach-omap2/prm2xxx_3xxx.h"
# include "../mach-omap2/sdrc.h"
#endif

#define OMAP1_SRAM_PA		0x20000000
#define OMAP1_SRAM_VA		VMALLOC_END
#define OMAP2_SRAM_PUB_PA	(OMAP2_SRAM_PA + 0xf800)
#define OMAP2_SRAM_VA		0xfe400000
#define OMAP2_SRAM_PUB_VA	(OMAP2_SRAM_VA + 0x800)
#define OMAP3_SRAM_VA           0xfe400000
#define OMAP3_SRAM_PUB_PA       (OMAP3_SRAM_PA + 0x8000)
#define OMAP3_SRAM_PUB_VA       (OMAP3_SRAM_VA + 0x8000)
#define OMAP4_SRAM_VA		0xfe400000
#define OMAP4_SRAM_PUB_PA	(OMAP4_SRAM_PA + 0x4000)
#define OMAP4_SRAM_PUB_VA	(OMAP4_SRAM_VA + 0x4000)

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
static unsigned long omap_sram_base;
static unsigned long omap_sram_size;
static unsigned long omap_sram_ceil;

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
	unsigned long reserved;

	if (cpu_class_is_omap2()) {
		if (is_sram_locked()) {
			if (cpu_is_omap34xx()) {
				omap_sram_base = OMAP3_SRAM_PUB_VA;
				omap_sram_start = OMAP3_SRAM_PUB_PA;
				if ((omap_type() == OMAP2_DEVICE_TYPE_EMU) ||
				    (omap_type() == OMAP2_DEVICE_TYPE_SEC)) {
					omap_sram_size = 0x7000; /* 28K */
				} else {
					omap_sram_size = 0x8000; /* 32K */
				}
			} else if (cpu_is_omap44xx()) {
				omap_sram_base = OMAP4_SRAM_PUB_VA;
				omap_sram_start = OMAP4_SRAM_PUB_PA;
				omap_sram_size = 0xa000; /* 40K */
			} else {
				omap_sram_base = OMAP2_SRAM_PUB_VA;
				omap_sram_start = OMAP2_SRAM_PUB_PA;
				omap_sram_size = 0x800; /* 2K */
			}
		} else {
			if (cpu_is_omap34xx()) {
				omap_sram_base = OMAP3_SRAM_VA;
				omap_sram_start = OMAP3_SRAM_PA;
				omap_sram_size = 0x10000; /* 64K */
			} else if (cpu_is_omap44xx()) {
				omap_sram_base = OMAP4_SRAM_VA;
				omap_sram_start = OMAP4_SRAM_PA;
				omap_sram_size = 0xe000; /* 56K */
			} else {
				omap_sram_base = OMAP2_SRAM_VA;
				omap_sram_start = OMAP2_SRAM_PA;
				if (cpu_is_omap242x())
					omap_sram_size = 0xa0000; /* 640K */
				else if (cpu_is_omap243x())
					omap_sram_size = 0x10000; /* 64K */
			}
		}
	} else {
		omap_sram_base = OMAP1_SRAM_VA;
		omap_sram_start = OMAP1_SRAM_PA;

		if (cpu_is_omap7xx())
			omap_sram_size = 0x32000;	/* 200K */
		else if (cpu_is_omap15xx())
			omap_sram_size = 0x30000;	/* 192K */
		else if (cpu_is_omap1610() || cpu_is_omap1621() ||
		     cpu_is_omap1710())
			omap_sram_size = 0x4000;	/* 16K */
		else if (cpu_is_omap1611())
			omap_sram_size = SZ_256K;
		else {
			pr_err("Could not detect SRAM size\n");
			omap_sram_size = 0x4000;
		}
	}
	reserved = omapfb_reserve_sram(omap_sram_start, omap_sram_base,
				       omap_sram_size,
				       omap_sram_start + SRAM_BOOTLOADER_SZ,
				       omap_sram_size - SRAM_BOOTLOADER_SZ);
	omap_sram_size -= reserved;

	reserved = omap_vram_reserve_sram(omap_sram_start, omap_sram_base,
			omap_sram_size,
			omap_sram_start + SRAM_BOOTLOADER_SZ,
			omap_sram_size - SRAM_BOOTLOADER_SZ);
	omap_sram_size -= reserved;

	omap_sram_ceil = omap_sram_base + omap_sram_size;
}

static struct map_desc omap_sram_io_desc[] __initdata = {
	{	/* .length gets filled in at runtime */
		.virtual	= OMAP1_SRAM_VA,
		.pfn		= __phys_to_pfn(OMAP1_SRAM_PA),
		.type		= MT_MEMORY
	}
};

/*
 * Note that we cannot use ioremap for SRAM, as clock init needs SRAM early.
 */
static void __init omap_map_sram(void)
{
	unsigned long base;

	if (omap_sram_size == 0)
		return;

	if (cpu_is_omap34xx()) {
		/*
		 * SRAM must be marked as non-cached on OMAP3 since the
		 * CORE DPLL M2 divider change code (in SRAM) runs with the
		 * SDRAM controller disabled, and if it is marked cached,
		 * the ARM may attempt to write cache lines back to SDRAM
		 * which will cause the system to hang.
		 */
		omap_sram_io_desc[0].type = MT_MEMORY_NONCACHED;
	}

	omap_sram_io_desc[0].virtual = omap_sram_base;
	base = omap_sram_start;
	base = ROUND_DOWN(base, PAGE_SIZE);
	omap_sram_io_desc[0].pfn = __phys_to_pfn(base);
	omap_sram_io_desc[0].length = ROUND_DOWN(omap_sram_size, PAGE_SIZE);
	iotable_init(omap_sram_io_desc, ARRAY_SIZE(omap_sram_io_desc));

	pr_info("SRAM: Mapped pa 0x%08llx to va 0x%08lx size: 0x%lx\n",
		(long long) __pfn_to_phys(omap_sram_io_desc[0].pfn),
		omap_sram_io_desc[0].virtual,
		omap_sram_io_desc[0].length);

	/*
	 * Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but since we're called from map_io(), we
	 * must do it here.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset((void *)omap_sram_base + SRAM_BOOTLOADER_SZ, 0,
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
	if (size > (omap_sram_ceil - (omap_sram_base + SRAM_BOOTLOADER_SZ))) {
		pr_err("Not enough space in SRAM\n");
		return NULL;
	}

	omap_sram_ceil -= size;
	omap_sram_ceil = ROUND_DOWN(omap_sram_ceil, FNCPY_ALIGN);

	return (void *)omap_sram_ceil;
}

#ifdef CONFIG_ARCH_OMAP1

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	BUG_ON(!_omap_sram_reprogram_clock);
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

#ifdef CONFIG_PM
void omap3_sram_restore_context(void)
{
	omap_sram_ceil = omap_sram_base + omap_sram_size;

	_omap3_sram_configure_core_dpll =
		omap_sram_push(omap3_sram_configure_core_dpll,
			       omap3_sram_configure_core_dpll_sz);
	omap_push_sram_idle();
}
#endif /* CONFIG_PM */

static int __init omap34xx_sram_init(void)
{
	_omap3_sram_configure_core_dpll =
		omap_sram_push(omap3_sram_configure_core_dpll,
			       omap3_sram_configure_core_dpll_sz);
	omap_push_sram_idle();
	return 0;
}
#else
static inline int omap34xx_sram_init(void)
{
	return 0;
}
#endif

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
	else if (cpu_is_omap34xx())
		omap34xx_sram_init();

	return 0;
}
