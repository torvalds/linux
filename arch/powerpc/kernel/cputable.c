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

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/oprofile_impl.h>
#include <asm/cputable.h>
#include <asm/prom.h>		/* for PTRRELOC on ARCH=ppc */

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
extern void __setup_cpu_440ep(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_440epx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_440gx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_440grx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_440spe(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_603(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_604(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750cx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_750fx(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_7400(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_7410(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_745x(unsigned long offset, struct cpu_spec* spec);
#endif /* CONFIG_PPC32 */
#ifdef CONFIG_PPC64
extern void __setup_cpu_ppc970(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_ppc970MP(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_pa6t(unsigned long offset, struct cpu_spec* spec);
extern void __restore_cpu_pa6t(void);
extern void __restore_cpu_ppc970(void);
#endif /* CONFIG_PPC64 */

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
#define COMMON_USER_PA6T	(COMMON_USER_PPC64 | PPC_FEATURE_PA6T |\
				 PPC_FEATURE_TRUE_LE | \
				 PPC_FEATURE_HAS_ALTIVEC_COMP)
#define COMMON_USER_BOOKE	(PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU | \
				 PPC_FEATURE_BOOKE)

static struct cpu_spec __initdata cpu_specs[] = {
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_type		= PPC_OPROFILE_RS64,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.cpu_setup		= __setup_cpu_ppc970,
		.cpu_restore		= __restore_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
		.platform		= "ppc970",
	},
	{	/* PPC970FX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003c0000,
		.cpu_name		= "PPC970FX",
		.cpu_features		= CPU_FTRS_PPC970,
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.pmc_type		= PPC_PMC_IBM,
		.cpu_setup		= __setup_cpu_ppc970,
		.cpu_restore		= __restore_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
		.platform		= "ppc970",
	},
	{	/* PPC970MP DD1.0 - no DEEPNAP, use regular 970 init */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x00440100,
		.cpu_name		= "PPC970MP",
		.cpu_features		= CPU_FTRS_PPC970,
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.pmc_type		= PPC_PMC_IBM,
		.cpu_setup		= __setup_cpu_ppc970,
		.cpu_restore		= __restore_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970MP",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
		.platform		= "ppc970",
	},
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
		.pmc_type		= PPC_PMC_IBM,
		.cpu_setup		= __setup_cpu_ppc970MP,
		.cpu_restore		= __restore_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970MP",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
		.platform		= "ppc970",
	},
	{	/* PPC970GX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00450000,
		.cpu_name		= "PPC970GX",
		.cpu_features		= CPU_FTRS_PPC970,
		.cpu_user_features	= COMMON_USER_POWER4 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.pmc_type		= PPC_PMC_IBM,
		.cpu_setup		= __setup_cpu_ppc970,
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.machine_check		= machine_check_generic,
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power5",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		/* SIHV / SIPR bits are implemented on POWER4+ (GQ)
		 * and above but only works on POWER5 and above
		 */
		.oprofile_mmcra_sihv	= MMCRA_SIHV,
		.oprofile_mmcra_sipr	= MMCRA_SIPR,
		.machine_check		= machine_check_generic,
		.platform		= "power5",
	},
	{	/* Power5++ */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x003b0300,
		.cpu_name		= "POWER5+ (gs)",
		.cpu_features		= CPU_FTRS_POWER5,
		.cpu_user_features	= COMMON_USER_POWER5_PLUS,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.oprofile_cpu_type	= "ppc64/power5++",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.oprofile_mmcra_sihv	= MMCRA_SIHV,
		.oprofile_mmcra_sipr	= MMCRA_SIPR,
		.machine_check		= machine_check_generic,
		.platform		= "power5+",
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
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power5+",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.oprofile_mmcra_sihv	= MMCRA_SIHV,
		.oprofile_mmcra_sipr	= MMCRA_SIPR,
		.machine_check		= machine_check_generic,
		.platform		= "power5+",
	},
	{	/* POWER6 in P5+ mode; 2.04-compliant processor */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x0f000001,
		.cpu_name		= "POWER5+",
		.cpu_features		= CPU_FTRS_POWER5,
		.cpu_user_features	= COMMON_USER_POWER5_PLUS,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.machine_check		= machine_check_generic,
		.platform		= "power5+",
	},
	{	/* Power6 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003e0000,
		.cpu_name		= "POWER6 (raw)",
		.cpu_features		= CPU_FTRS_POWER6,
		.cpu_user_features	= COMMON_USER_POWER6 |
			PPC_FEATURE_POWER6_EXT,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/power6",
		.oprofile_type		= PPC_OPROFILE_POWER4,
		.oprofile_mmcra_sihv	= POWER6_MMCRA_SIHV,
		.oprofile_mmcra_sipr	= POWER6_MMCRA_SIPR,
		.oprofile_mmcra_clear	= POWER6_MMCRA_THRM |
			POWER6_MMCRA_OTHER,
		.machine_check		= machine_check_generic,
		.platform		= "power6x",
	},
	{	/* 2.05-compliant processor, i.e. Power6 "architected" mode */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x0f000002,
		.cpu_name		= "POWER6 (architected)",
		.cpu_features		= CPU_FTRS_POWER6,
		.cpu_user_features	= COMMON_USER_POWER6,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.machine_check		= machine_check_generic,
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
		.num_pmcs		= 4,
		.pmc_type		= PPC_PMC_IBM,
		.oprofile_cpu_type	= "ppc64/cell-be",
		.oprofile_type		= PPC_OPROFILE_CELL,
		.machine_check		= machine_check_generic,
		.platform		= "ppc-cell-be",
	},
	{	/* PA Semi PA6T */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00900000,
		.cpu_name		= "PA6T",
		.cpu_features		= CPU_FTRS_PA6T,
		.cpu_user_features	= COMMON_USER_PA6T,
		.icache_bsize		= 64,
		.dcache_bsize		= 64,
		.num_pmcs		= 6,
		.pmc_type		= PPC_PMC_PA6T,
		.cpu_setup		= __setup_cpu_pa6t,
		.cpu_restore		= __restore_cpu_pa6t,
		.oprofile_cpu_type	= "ppc64/pa6t",
		.oprofile_type		= PPC_OPROFILE_PA6T,
		.machine_check		= machine_check_generic,
		.platform		= "pa6t",
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
		.pmc_type		= PPC_PMC_IBM,
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
		.platform		= "ppc750",
	},
	{	/* 750CL */
		.pvr_mask		= 0xfffff0f0,
		.pvr_value		= 0x00087010,
		.cpu_name		= "750CL",
		.cpu_features		= CPU_FTRS_750CL,
		.cpu_user_features	= COMMON_USER | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750,
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
		.platform		= "ppc7450",
	},
	{	/* 7448 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80040000,
		.cpu_name		= "7448",
		.cpu_features		= CPU_FTRS_7448,
		.cpu_user_features	= COMMON_USER |
			PPC_FEATURE_HAS_ALTIVEC_COMP | PPC_FEATURE_PPC_LE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x,
		.oprofile_cpu_type      = "ppc/7450",
		.oprofile_type		= PPC_OPROFILE_G4,
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
		.platform		= "ppc603",
	},
	{	/* e300c1 (a 603e core, plus some) on 83xx */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00830000,
		.cpu_name		= "e300c1",
		.cpu_features		= CPU_FTRS_E300,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.machine_check		= machine_check_generic,
		.platform		= "ppc603",
	},
	{	/* e300c2 (an e300c1 core, plus some, minus FPU) on 83xx */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00840000,
		.cpu_name		= "e300c2",
		.cpu_features		= CPU_FTRS_E300C2,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.machine_check		= machine_check_generic,
		.platform		= "ppc603",
	},
	{	/* e300c3 (e300c1, plus one IU, half cache size) on 83xx */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00850000,
		.cpu_name		= "e300c3",
		.cpu_features		= CPU_FTRS_E300,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.platform		= "ppc603",
	},
	{	/* e300c4 (e300c1, plus one IU) */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00860000,
		.cpu_name		= "e300c4",
		.cpu_features		= CPU_FTRS_E300,
		.cpu_user_features	= COMMON_USER,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603,
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_generic,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
		.platform		= "ppc405",
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
		.machine_check		= machine_check_4xx,
		.platform		= "ppc405",
	},
	{	/* 405EX */
		.pvr_mask		= 0xffff0004,
		.pvr_value		= 0x12910004,
		.cpu_name		= "405EX",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc405",
	},
	{	/* 405EXr */
		.pvr_mask		= 0xffff0004,
		.pvr_value		= 0x12910000,
		.cpu_name		= "405EXr",
		.cpu_features		= CPU_FTRS_40X,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc405",
	},

#endif /* CONFIG_40x */
#ifdef CONFIG_44x
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000850,
		.cpu_name		= "440GR Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{ /* Use logical PVR for 440EP (logical pvr = pvr | 0x8) */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000858,
		.cpu_name		= "440EP Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440ep,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x400008d3,
		.cpu_name		= "440GR Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{ /* Matches both physical and logical PVR for 440EP (logical pvr = pvr | 0x8) */
		.pvr_mask		= 0xf0000ff7,
		.pvr_value		= 0x400008d4,
		.cpu_name		= "440EP Rev. C",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440ep,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{ /* Use logical PVR for 440EP (logical pvr = pvr | 0x8) */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x400008db,
		.cpu_name		= "440EP Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440ep,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{ /* 440GRX */
		.pvr_mask		= 0xf0000ffb,
		.pvr_value		= 0x200008D0,
		.cpu_name		= "440GRX",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440grx,
		.machine_check		= machine_check_440A,
		.platform		= "ppc440",
	},
	{ /* Use logical PVR for 440EPx (logical pvr = pvr | 0x8) */
		.pvr_mask		= 0xf0000ffb,
		.pvr_value		= 0x200008D8,
		.cpu_name		= "440EPX",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440epx,
		.machine_check		= machine_check_440A,
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
		.machine_check		= machine_check_4xx,
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
		.machine_check		= machine_check_4xx,
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
		.cpu_setup		= __setup_cpu_440gx,
		.machine_check		= machine_check_440A,
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
		.cpu_setup		= __setup_cpu_440gx,
		.machine_check		= machine_check_440A,
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
		.cpu_setup		= __setup_cpu_440gx,
		.machine_check		= machine_check_440A,
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
		.cpu_setup		= __setup_cpu_440gx,
		.machine_check		= machine_check_440A,
		.platform		= "ppc440",
	},
	{ /* 440SP Rev. A */
		.pvr_mask		= 0xfff00fff,
		.pvr_value		= 0x53200891,
		.cpu_name		= "440SP Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_4xx,
		.platform		= "ppc440",
	},
	{ /* 440SPe Rev. A */
		.pvr_mask               = 0xfff00fff,
		.pvr_value              = 0x53400890,
		.cpu_name               = "440SPe Rev. A",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features      = COMMON_USER_BOOKE,
		.icache_bsize           = 32,
		.dcache_bsize           = 32,
		.cpu_setup		= __setup_cpu_440spe,
		.machine_check		= machine_check_440A,
		.platform               = "ppc440",
	},
	{ /* 440SPe Rev. B */
		.pvr_mask		= 0xfff00fff,
		.pvr_value		= 0x53400891,
		.cpu_name		= "440SPe Rev. B",
		.cpu_features		= CPU_FTRS_44X,
		.cpu_user_features	= COMMON_USER_BOOKE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_440spe,
		.machine_check		= machine_check_440A,
		.platform		= "ppc440",
	},
#endif /* CONFIG_44x */
#ifdef CONFIG_FSL_BOOKE
#ifdef CONFIG_E200
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
		.machine_check		= machine_check_e200,
		.platform		= "ppc5554",
	},
	{	/* e200z6 */
		.pvr_mask		= 0xfff00000,
		.pvr_value		= 0x81100000,
		.cpu_name		= "e200z6",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E200,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_HAS_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE_COMP |
			PPC_FEATURE_UNIFIED_CACHE,
		.dcache_bsize		= 32,
		.machine_check		= machine_check_e200,
		.platform		= "ppc5554",
	},
#elif defined(CONFIG_E500)
	{	/* e500 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80200000,
		.cpu_name		= "e500",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E500,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_HAS_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.oprofile_cpu_type	= "ppc/e500",
		.oprofile_type		= PPC_OPROFILE_BOOKE,
		.machine_check		= machine_check_e500,
		.platform		= "ppc8540",
	},
	{	/* e500v2 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80210000,
		.cpu_name		= "e500v2",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTRS_E500_2,
		.cpu_user_features	= COMMON_USER_BOOKE |
			PPC_FEATURE_HAS_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE_COMP |
			PPC_FEATURE_HAS_EFP_DOUBLE_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.oprofile_cpu_type	= "ppc/e500",
		.oprofile_type		= PPC_OPROFILE_BOOKE,
		.machine_check		= machine_check_e500,
		.platform		= "ppc8548",
	},
#endif
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

static struct cpu_spec the_cpu_spec;

struct cpu_spec * __init identify_cpu(unsigned long offset, unsigned int pvr)
{
	struct cpu_spec *s = cpu_specs;
	struct cpu_spec *t = &the_cpu_spec;
	int i;

	s = PTRRELOC(s);
	t = PTRRELOC(t);

	for (i = 0; i < ARRAY_SIZE(cpu_specs); i++,s++)
		if ((pvr & s->pvr_mask) == s->pvr_value) {
			/*
			 * If we are overriding a previous value derived
			 * from the real PVR with a new value obtained
			 * using a logical PVR value, don't modify the
			 * performance monitor fields.
			 */
			if (t->num_pmcs && !s->num_pmcs) {
				t->cpu_name = s->cpu_name;
				t->cpu_features = s->cpu_features;
				t->cpu_user_features = s->cpu_user_features;
				t->icache_bsize = s->icache_bsize;
				t->dcache_bsize = s->dcache_bsize;
				t->cpu_setup = s->cpu_setup;
				t->cpu_restore = s->cpu_restore;
				t->platform = s->platform;
			} else
				*t = *s;
			*PTRRELOC(&cur_cpu_spec) = &the_cpu_spec;
#if defined(CONFIG_PPC64) || defined(CONFIG_BOOKE)
			/* ppc64 and booke expect identify_cpu to also call 
			 * setup_cpu for that processor. I will consolidate
			 * that at a later time, for now, just use #ifdef.
			 * we also don't need to PTRRELOC the function pointer
			 * on ppc64 and booke as we are running at 0 in real
			 * mode on ppc64 and reloc_offset is always 0 on booke.
			 */
			if (s->cpu_setup) {
				s->cpu_setup(offset, s);
			}
#endif /* CONFIG_PPC64 || CONFIG_BOOKE */
			return s;
		}
	BUG();
	return NULL;
}

void do_feature_fixups(unsigned long value, void *fixup_start, void *fixup_end)
{
	struct fixup_entry {
		unsigned long	mask;
		unsigned long	value;
		long		start_off;
		long		end_off;
	} *fcur, *fend;

	fcur = fixup_start;
	fend = fixup_end;

	for (; fcur < fend; fcur++) {
		unsigned int *pstart, *pend, *p;

		if ((value & fcur->mask) == fcur->value)
			continue;

		/* These PTRRELOCs will disappear once the new scheme for
		 * modules and vdso is implemented
		 */
		pstart = ((unsigned int *)fcur) + (fcur->start_off / 4);
		pend = ((unsigned int *)fcur) + (fcur->end_off / 4);

		for (p = pstart; p < pend; p++) {
			*p = 0x60000000u;
			asm volatile ("dcbst 0, %0" : : "r" (p));
		}
		asm volatile ("sync" : : : "memory");
		for (p = pstart; p < pend; p++)
			asm volatile ("icbi 0,%0" : : "r" (p));
		asm volatile ("sync; isync" : : : "memory");
	}
}
