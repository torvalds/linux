/*
 *  arch/ppc64/kernel/cputable.c
 *
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
extern void __setup_cpu_power3(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_power4(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_ppc970(unsigned long offset, struct cpu_spec* spec);
extern void __setup_cpu_be(unsigned long offset, struct cpu_spec* spec);


/* We only set the altivec features if the kernel was compiled with altivec
 * support
 */
#ifdef CONFIG_ALTIVEC
#define CPU_FTR_ALTIVEC_COMP	CPU_FTR_ALTIVEC
#define PPC_FEATURE_HAS_ALTIVEC_COMP PPC_FEATURE_HAS_ALTIVEC
#else
#define CPU_FTR_ALTIVEC_COMP	0
#define PPC_FEATURE_HAS_ALTIVEC_COMP    0
#endif

struct cpu_spec	cpu_specs[] = {
	{	/* Power3 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00400000,
		.cpu_name		= "POWER3 (630)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR,
		.cpu_user_features = COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* Power3+ */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00410000,
		.cpu_name		= "POWER3 (630+)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power3",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* Northstar */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00330000,
		.cpu_name		= "RS64-II (northstar)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR |
			CPU_FTR_MMCRA | CPU_FTR_CTRL,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* Pulsar */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00340000,
		.cpu_name		= "RS64-III (pulsar)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR |
			CPU_FTR_MMCRA | CPU_FTR_CTRL,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* I-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00360000,
		.cpu_name		= "RS64-III (icestar)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR |
			CPU_FTR_MMCRA | CPU_FTR_CTRL,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* S-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00370000,
		.cpu_name		= "RS64-IV (sstar)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE | CPU_FTR_IABR |
			CPU_FTR_MMCRA | CPU_FTR_CTRL,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/rs64",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* Power4 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00350000,
		.cpu_name		= "POWER4 (gp)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_MMCRA,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power4,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_model		= &op_model_rs64,
#endif
	},
	{	/* Power4+ */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00380000,
		.cpu_name		= "POWER4+ (gq)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_MMCRA,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power4,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power4",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* PPC970 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00390000,
		.cpu_name		= "PPC970",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_ALTIVEC_COMP |
			CPU_FTR_CAN_NAP | CPU_FTR_MMCRA,
		.cpu_user_features	= COMMON_USER_PPC64 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* PPC970FX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003c0000,
		.cpu_name		= "PPC970FX",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_ALTIVEC_COMP |
			CPU_FTR_CAN_NAP | CPU_FTR_MMCRA,
		.cpu_user_features	= COMMON_USER_PPC64 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* PPC970MP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00440000,
		.cpu_name		= "PPC970MP",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_ALTIVEC_COMP |
			CPU_FTR_CAN_NAP | CPU_FTR_MMCRA,
		.cpu_user_features	= COMMON_USER_PPC64 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.cpu_setup		= __setup_cpu_ppc970,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/970",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* Power5 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003a0000,
		.cpu_name		= "POWER5 (gr)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_MMCRA | CPU_FTR_SMT |
			CPU_FTR_COHERENT_ICACHE | CPU_FTR_LOCKLESS_TLBIE |
			CPU_FTR_MMCRA_SIHV,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_power4,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power5",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* Power5 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003b0000,
		.cpu_name		= "POWER5 (gs)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_MMCRA | CPU_FTR_SMT |
			CPU_FTR_COHERENT_ICACHE | CPU_FTR_LOCKLESS_TLBIE |
			CPU_FTR_MMCRA_SIHV,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_power4,
#ifdef CONFIG_OPROFILE
		.oprofile_cpu_type	= "ppc64/power5",
		.oprofile_model		= &op_model_power4,
#endif
	},
	{	/* BE DD1.x */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00700000,
		.cpu_name		= "Broadband Engine",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_ALTIVEC_COMP |
			CPU_FTR_SMT,
		.cpu_user_features	= COMMON_USER_PPC64 |
			PPC_FEATURE_HAS_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.cpu_setup		= __setup_cpu_be,
	},
	{	/* default match */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "POWER4 (compatible)",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_HPTE_TABLE |
			CPU_FTR_PPCAS_ARCH_V2,
		.cpu_user_features	= COMMON_USER_PPC64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_power4,
	}
};
