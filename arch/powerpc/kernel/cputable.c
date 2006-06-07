/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/oprofile_impl.h>
#include <asm/cputable.h>

struct cpu_spec* cur_cpu_spec = NULL;
EXPORT_SYMBOL(cur_cpu_spec);

/* NOTE:
 * Unlike ppc32, ppc64 will only call this once for the boot CPU, it's
 * the responsibility of the appropriate CPU save/restore functions to
 * eventually copy these settings over. Those save/restore aren't yet
 * part of the cputable though. That has to be fixed for both ppc32
 * and ppc64
 */
#ifdef CONFIG_PPC32
extern void __setup_cpu_603(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_604(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750cx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750fx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_7400(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_7410(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_745x(unsigned long offset, struct cpu_spec* spec);
#endif /* CONFIG_PPC32 */
extern void __setup_cpu_ppc970(unsigned long offset, struct cpu_spec* spec);

/* This table only contains "desktop" CPUs, it need to be filled with embedded
 * ones as well...
 */
#define COMMON_USER		(PPC_FEATURE_32 | PPC_FEATURE_HAS_FPU | \
				 PPC_FEATURE_HAS_MMU)
#define COMMON_USER_PPC64	(COMMON_USER | PPC_FEATURE_64)
#define COMMON_USER_POWER4	(COMMON_USER_PPC64 | PPC_FEATURE_POWER4)
#define COMMON_USER_POWER5	(COMMON_USER_PPC64 | PPC_FEATURE_POWER5 |\
				 PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP)
#define COMMON_USER_POWER5_PLUS	(COMMON_USER_PPC64 | PPC_FEATURE_POWER5_PLUS|\
				 PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP)
#define COMMON_USER_POWER6	(COMMON_USER_PPC64 | PPC_FEATURE_ARCH_2_05 |\
				 PPC_FEATURE_SMT | PPC_FEATURE_ICACHE_SNOOP | \
				 PPC_FEATURE_TRUE_LE)
#define COMMON_USER_BOOKE	(PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU | \
				 PPC_FEATURE_BOOKE)

/* We only set the spe features if the kernel was compiled with
 * spe support
 */
#ifdef CONFIG_SPE
#define PPC_FEATURE_SPE_COMP	PPC_FEATURE_HAS_SPE
#else
#define PPC_FEATURE_SPE_COMP	0
#endif

struct cpu_spec	cpu_specs[] = {
#ifdef CONFIG_PPC64
	{	/* Power3 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00400000,
		.cpu_name		= "POWER3 (630)",
		.cpu_features		= CPU_FTRS_POWER3,
		.cpu_user_features	= COMMON_USER_PPC64|PPC_FEATURE_PPC_LE,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "power3",
	},
	{	/* Power3+ */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00410000,
		.cpu_name		= "POWER3 (630+)",
		.cpu_features		= CPU_FTRS_POWER3,
		.cpu_user_features	= COMMON_USER_PPC64|PPC_FEATURE_PPC_LE,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "power3",
	},
	{	/* Northstar */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00330000,
		.cpu_name		= "RS64-II (northstar)",
		.cpu_features		= CPU_FTRS_RS64,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "rs64",
	},
	{	/* Pulsar */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00340000,
		.cpu_name		= "RS64-III (pulsar)",
		.cpu_features		= CPU_FTRS_RS64,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "rs64",
	},
	{	/* I-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00360000,
		.cpu_name		= "RS64-III (icestar)",
		.cpu_features		= CPU_FTRS_RS64,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "rs64",
	},
	{	/* S-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00370000,
		.cpu_name		= "RS64-IV (sstar)",
		.cpu_features		= CPU_FTRS_RS64,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.platform		= "rs64",
	},
	{	/* Power4 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00350000,
		.cpu_name		= "POWER4 (gp)",
		.cpu_features		= CPU_FTRS_POWER4,
		.cpu_user_features	= COMMON_USER_POWER4,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.platform		= "power4",
	},
	{	/* Power4+ */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00380000,
		.cpu_name		= "POWER4+ (gq)",
		.cpu_features		= CPU_FTRS_POWER4,
		.cpu_user_features	= COMMON_USER_POWER4,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.platform		= "power4",
	},
	{	/* PPC970 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00390000,
		.cpu_name		= "PPC970",
		.cpu_features		= CPU_FTRS_PPC970,
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.platform		= "ppc970",
	},
#endif /* CONFIG_PPC64 */
#if defined(CONFIG_PPC64) || defined(CONFIG_POWER4)
	{	/* PPC970FX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003c0000,
		.cpu_name		= "PPC970FX",
#ifdef CONFIG_PPC32
		.cpu_features		= CPU_FTRS_970_32,
#else
		.cpu_features		= CPU_FTRS_PPC970,
#endif
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.platform		= "ppc970",
	},
#endif /* defined(CONFIG_PPC64) || defined(CONFIG_POWER4) */
#ifdef CONFIG_PPC64
	{	/* PPC970MP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00440000,
		.cpu_name		= "PPC970MP",
		.cpu_features		= CPU_FTRS_PPC970,
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.platform		= "ppc970",
	},
	{	/* Power5 GR */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003a0000,
		.cpu_name		= "POWER5 (gr)",
		.cpu_features		= CPU_FTRS_POWER5,
		.cpu_user_features	= COMMON_USER_POWER5,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.oprofile_cpu_type	= "ppc64/power5",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		/* SIHV / SIPR bits are implemented on POWER4+ (GQ)
		 * and above but only works on POWER5 and above
		 */
		.oprofile_mmcra_sihv	= MMCRA_SIHV,
		.oprofile_mmcra_sipr	= MMCRA_SIPR,
		.platform		= "power5",
	},
	{	/* Power5 GS */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003b0000,
		.cpu_name		= "POWER5+ (gs)",
		.cpu_features		= CPU_FTRS_POWER5,
		.cpu_user_features	= COMMON_USER_POWER5_PLUS,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.oprofile_cpu_type	= "ppc64/power5+",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.oprofile_mmcra_sihv	= MMCRA_SIHV,
		.oprofile_mmcra_sipr	= MMCRA_SIPR,
		.platform		= "power5+",
	},
	{	/* Power6 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003e0000,
		.cpu_name		= "POWER6",
		.cpu_features		= CPU_FTRS_POWER6,
		.cpu_user_features	= COMMON_USER_POWER6,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.oprofile_cpu_type	= "ppc64/power6",
		.oprofile_type		= PPC_OPROFILE_POWER4,
 		.oprofile_mmcra_sihv	= POWER6_MMCRA_SIHV,
 		.oprofile_mmcra_sipr	= POWER6_MMCRA_SIPR,
 		.oprofile_mmcra_clear	= POWER6_MMCRA_THRM |
 			POWER6_MMCRA_OTHER,
		.platform		= "power6",
	},
	{	/* Cell Broadband Engine */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00700000,
		.cpu_name		= "Cell Broadband Engine",
		.cpu_features		= CPU_FTRS_CELL,
		.cpu_user_features	= COMMON_USER_PPC64 |
			PPC_FEATURE_CELL | PPC_FEATURE_HAS_ALTIVEC_COMP |
			PPC_FEATURE_SMT,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.platform		= "ppc-cell-be",
	},
	{	/* default match */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "POWER4 (compatible)",
		.cpu_features		= CPU_FTRS_COMPATIBLE,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.platform		= "power4",
	}
#endif	/* CONFIG_PPC64 */
#ifdef CONFIG_PPC32
#if CLASSIC_PPC
	{	/* 601 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00010000,
		.cpu_name		= "601",
		.cpu_features		= CPU_FTRS_PPC601,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_601_INSTR |
			PPC_FEATURE_UNIFIED_CACHE | PPC_FEATURE_NO_TB,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc601",
	},
	{	/* 603 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00030000,
		.cpu_name		= "603",
		.cpu_features		= CPU_FTRS_603,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* 603e */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00060000,
		.cpu_name		= "603e",
		.cpu_features		= CPU_FTRS_603,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* 603ev */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00070000,
		.cpu_name		= "603ev",
		.cpu_features		= CPU_FTRS_603,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* 604 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00040000,
		.cpu_name		= "604",
		.cpu_features		= CPU_FTRS_604,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 2,
		.cpu_setup		= __setup_cpu_604,
		.platform		= "ppc604",
	},
	{	/* 604e */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x00090000,
		.cpu_name		= "604e",
		.cpu_features		= CPU_FTRS_604,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604,
		.platform		= "ppc604",
	},
	{	/* 604r */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00090000,
		.cpu_name		= "604r",
		.cpu_features		= CPU_FTRS_604,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604,
		.platform		= "ppc604",
	},
	{	/* 604ev */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x000a0000,
		.cpu_name		= "604ev",
		.cpu_features		= CPU_FTRS_604,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604,
		.platform		= "ppc604",
	},
	{	/* 740/750 (0x4202, don't support TAU ?) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x00084202,
		.cpu_name		= "740/750",
		.cpu_features		= CPU_FTRS_740_NOTAU,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.platform		= "ppc750",
	},
	{	/* 750CX (80100 and 8010x?) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00080100,
		.cpu_name		= "750CX",
		.cpu_features		= CPU_FTRS_750,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx,
		.platform		= "ppc750",
	},
	{	/* 750CX (82201 and 82202) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00082200,
		.cpu_name		= "750CX",
		.cpu_features		= CPU_FTRS_750,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx,
		.platform		= "ppc750",
	},
	{	/* 750CXe (82214) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00082210,
		.cpu_name		= "750CXe",
		.cpu_features		= CPU_FTRS_750,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx,
		.platform		= "ppc750",
	},
	{	/* 750CXe "Gekko" (83214) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x00083214,
		.cpu_name		= "750CXe",
		.cpu_features		= CPU_FTRS_750,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx,
		.platform		= "ppc750",
	},
	{	/* 745/755 */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x00083000,
		.cpu_name		= "745/755",
		.cpu_features		= CPU_FTRS_750,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.platform		= "ppc750",
	},
	{	/* 750FX rev 1.x */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x70000100,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTRS_750FX1,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.platform		= "ppc750",
	},
	{	/* 750FX rev 2.0 must disable HID0[DPM] */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x70000200,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTRS_750FX2,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.platform		= "ppc750",
	},
	{	/* 750FX (All revs except 2.0) */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x70000000,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTRS_750FX,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750fx,
		.platform		= "ppc750",
	},
	{	/* 750GX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x70020000,
		.cpu_name		= "750GX",
		.cpu_features		= CPU_FTRS_750GX,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750fx,
		.platform		= "ppc750",
	},
	{	/* 740/750 (L2CR bit need fixup for 740) */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00080000,
		.cpu_name		= "740/750",
		.cpu_features		= CPU_FTRS_740,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.platform		= "ppc750",
	},
	{	/* 7400 rev 1.1 ? (no TAU) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x000c1101,
		.cpu_name		= "7400 (1.1)",
		.cpu_features		= CPU_FTRS_7400_NOTAU,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7400,
		.platform		= "ppc7400",
	},
	{	/* 7400 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x000c0000,
		.cpu_name		= "7400",
		.cpu_features		= CPU_FTRS_7400,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7400,
		.platform		= "ppc7400",
	},
	{	/* 7410 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x800c0000,
		.cpu_name		= "7410",
		.cpu_features		= CPU_FTRS_7400,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7410,
		.platform		= "ppc7400",
	},
	{	/* 7450 2.0 - no doze/nap */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80000200,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTRS_7450_20,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7450 2.1 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80000201,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTRS_7450_21,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7450 2.3 and newer */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80000000,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTRS_7450_23,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7455 rev 1.x */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x80010100,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTRS_7455_1,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7455 rev 2.0 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80010200,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTRS_7455_20,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7455 others */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80010000,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTRS_7455,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7447/7457 Rev 1.0 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80020100,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTRS_7447_10,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7447/7457 Rev 1.1 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80020101,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTRS_7447_10,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7447/7457 Rev 1.2 and later */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80020000,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTRS_7447,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7447A */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80030000,
		.cpu_name		= "7447A",
		.cpu_features		= CPU_FTRS_7447A,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 7448 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80040000,
		.cpu_name		= "7448",
		.cpu_features		= CPU_FTRS_7447A,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.platform		= "ppc7450",
	},
	{	/* 82xx (8240, 8245, 8260 are all 603e cores) */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00810000,
		.cpu_name		= "82xx",
		.cpu_features		= CPU_FTRS_82XX,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* All G2_LE (603e core, plus some) have the same pvr */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00820000,
		.cpu_name		= "G2_LE",
		.cpu_features		= CPU_FTRS_G2_LE,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* e300 (a 603e core, plus some) on 83xx */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00830000,
		.cpu_name		= "e300",
		.cpu_features		= CPU_FTRS_E300,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* default match, we assume split I/D cache & TB (non-601)... */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "(generic PPC)",
		.cpu_features		= CPU_FTRS_CLASSIC32,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc603",
	},
#endif /* CLASSIC_PPC */
#ifdef CONFIG_8xx
	{	/* 8xx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00500000,
		.cpu_name		= "8xx",
		/* CPU_FTR_MAYBE_CAN_DOZE is possible,
		 * if the 8xx code is there.... */
		.cpu_features		= CPU_FTRS_8XX,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
		.platform		= "ppc823",
	},
#endif /* CONFIG_8xx */
#ifdef CONFIG_40x
	{	/* 403GC */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x00200200,
		.cpu_name		= "403GC",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
		.platform		= "ppc403",
	},
	{	/* 403GCX */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x00201400,
		.cpu_name		= "403GCX",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
		 	PPC_FEATURE_HAS_MMU | PPC_FEATURE_NO_TB,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
		.platform		= "ppc403",
	},
	{	/* 403G ?? */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00200000,
		.cpu_name		= "403G ??",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
		.platform		= "ppc403",
	},
	{	/* 405GP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40110000,
		.cpu_name		= "405GP",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* STB 03xxx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40130000,
		.cpu_name		= "STB03xxx",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* STB 04xxx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41810000,
		.cpu_name		= "STB04xxx",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* NP405L */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41610000,
		.cpu_name		= "NP405L",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* NP4GS3 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40B10000,
		.cpu_name		= "NP4GS3",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{   /* NP405H */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41410000,
		.cpu_name		= "NP405H",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* 405GPr */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x50910000,
		.cpu_name		= "405GPr",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{   /* STBx25xx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x51510000,
		.cpu_name		= "STBx25xx",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* 405LP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41F10000,
		.cpu_name		= "405LP",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* Xilinx Virtex-II Pro  */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x20010000,
		.cpu_name		= "Virtex-II Pro",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},
	{	/* Xilinx Virtex-4 FX */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x20011000,
		.cpu_name		= "Virtex-4 FX",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* 405EP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x51210000,
		.cpu_name		= "405EP",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc405",
	},

#endif /* CONFIG_40x */
#ifdef CONFIG_44x
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000850,
		.cpu_name		= "440EP Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x400008d3,
		.cpu_name		= "440EP Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{	/* 440GP Rev. B */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000440,
		.cpu_name		= "440GP Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440gp",
	},
	{	/* 440GP Rev. C */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000481,
		.cpu_name		= "440GP Rev. C",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440gp",
	},
	{ /* 440GX Rev. A */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000850,
		.cpu_name		= "440GX Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{ /* 440GX Rev. B */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000851,
		.cpu_name		= "440GX Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{ /* 440GX Rev. C */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000892,
		.cpu_name		= "440GX Rev. C",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{ /* 440GX Rev. F */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000894,
		.cpu_name		= "440GX Rev. F",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{ /* 440SP Rev. A */
		.pvr_mask		= 0xff000fff,
		.pvr_value		= 0x53000891,
		.cpu_name		= "440SP Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
	{ /* 440SPe Rev. A */
		.pvr_mask		= 0xff000fff,
		.pvr_value		= 0x53000890,
		.cpu_name		= "440SPe Rev. A",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "ppc440",
	},
#endif /* CONFIG_44x */
#ifdef CONFIG_FSL_BOOKE
	{	/* e200z5 */
		.pvr_mask		= 0xfff00000,
		.pvr_value		= 0x81000000,
		.cpu_name		= "e200z5",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E200,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_HAS_EFP_SINGLE |
			PPC_FEATURE_UNIFIED_CACHE,
		.dcache_bsize		= 32,
		.platform		= "ppc5554",
	},
	{	/* e200z6 */
		.pvr_mask		= 0xfff00000,
		.pvr_value		= 0x81100000,
		.cpu_name		= "e200z6",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E200,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE |
			PPC_FEATURE_UNIFIED_CACHE,
		.dcache_bsize		= 32,
		.platform		= "ppc5554",
	},
	{	/* e500 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80200000,
		.cpu_name		= "e500",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E500,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.oprofile_cpu_type	= "ppc/e500",
		.oprofile_type		= PPC_OPROFILE_BOOKE,
		.platform		= "ppc8540",
	},
	{	/* e500v2 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80210000,
		.cpu_name		= "e500v2",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E500_2,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE |
			PPC_FEATURE_HAS_EFP_DOUBLE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.oprofile_cpu_type	= "ppc/e500",
		.oprofile_type		= PPC_OPROFILE_BOOKE,
		.platform		= "ppc8548",
	},
#endif
#if !CLASSIC_PPC
	{	/* default match */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "(generic PPC)",
		.cpu_features		= CPU_FTRS_GENERIC_32,
		.cpu_user_features	= PPC_FEATURE_32,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.platform		= "powerpc",
	}
#endif /* !CLASSIC_PPC */
#endif /* CONFIG_PPC32 */
};
