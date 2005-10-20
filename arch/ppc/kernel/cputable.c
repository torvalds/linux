/*
 *  arch/ppc/kernel/cputable.c
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
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
#include <asm/cputable.h>

struct cpu_spec* cur_cpu_spec[NR_CPUS];

extern void __setup_cpu_601(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_603(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_604(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_750(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_750cx(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_750fx(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_7400(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_7410(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_745x(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_power3(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_power4(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_ppc970(unsigned long offset, int cpu_nr, struct cpu_spec* spec);
extern void __setup_cpu_generic(unsigned long offset, int cpu_nr, struct cpu_spec* spec);

#define CLASSIC_PPC (!defined(CONFIG_8xx) && !defined(CONFIG_4xx) && \
		     !defined(CONFIG_POWER3) && !defined(CONFIG_POWER4) && \
		     !defined(CONFIG_BOOKE))

/* This table only contains "desktop" CPUs, it need to be filled with embedded
 * ones as well...
 */
#define COMMON_PPC	(PPC_FEATURE_32 | PPC_FEATURE_HAS_FPU | \
			 PPC_FEATURE_HAS_MMU)

/* We only set the altivec features if the kernel was compiled with altivec
 * support
 */
#ifdef CONFIG_ALTIVEC
#define CPU_FTR_ALTIVEC_COMP		CPU_FTR_ALTIVEC
#define PPC_FEATURE_ALTIVEC_COMP    	PPC_FEATURE_HAS_ALTIVEC
#else
#define CPU_FTR_ALTIVEC_COMP		0
#define PPC_FEATURE_ALTIVEC_COMP       	0
#endif

/* We only set the spe features if the kernel was compiled with
 * spe support
 */
#ifdef CONFIG_SPE
#define PPC_FEATURE_SPE_COMP    	PPC_FEATURE_HAS_SPE
#else
#define PPC_FEATURE_SPE_COMP       	0
#endif

/* We need to mark all pages as being coherent if we're SMP or we
 * have a 74[45]x and an MPC107 host bridge.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_MPC10X_BRIDGE)
#define CPU_FTR_COMMON                  CPU_FTR_NEED_COHERENT
#else
#define CPU_FTR_COMMON                  0
#endif

/* The powersave features NAP & DOZE seems to confuse BDI when
   debugging. So if a BDI is used, disable theses
 */
#ifndef CONFIG_BDI_SWITCH
#define CPU_FTR_MAYBE_CAN_DOZE	CPU_FTR_CAN_DOZE
#define CPU_FTR_MAYBE_CAN_NAP	CPU_FTR_CAN_NAP
#else
#define CPU_FTR_MAYBE_CAN_DOZE	0
#define CPU_FTR_MAYBE_CAN_NAP	0
#endif

struct cpu_spec	cpu_specs[] = {
#if CLASSIC_PPC
	{ 	/* 601 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00010000,
		.cpu_name		= "601",
		.cpu_features		= CPU_FTR_COMMON | CPU_FTR_601 |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features 	= COMMON_PPC | PPC_FEATURE_601_INSTR |
			PPC_FEATURE_UNIFIED_CACHE | PPC_FEATURE_NO_TB,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_601
	},
	{	/* 603 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00030000,
		.cpu_name		= "603",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* 603e */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00060000,
		.cpu_name		= "603e",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* 603ev */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00070000,
		.cpu_name		= "603ev",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* 604 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00040000,
		.cpu_name		= "604",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_604_PERF_MON | CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 2,
		.cpu_setup		= __setup_cpu_604
	},
	{	/* 604e */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x00090000,
		.cpu_name		= "604e",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_604_PERF_MON | CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604
	},
	{	/* 604r */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00090000,
		.cpu_name		= "604r",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_604_PERF_MON | CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604
	},
	{	/* 604ev */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x000a0000,
		.cpu_name		= "604ev",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_604_PERF_MON | CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_604
	},
	{	/* 740/750 (0x4202, don't support TAU ?) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x00084202,
		.cpu_name		= "740/750",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_HPTE_TABLE |
			CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750
	},
	{	/* 750CX (80100 and 8010x?) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00080100,
		.cpu_name		= "750CX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx
	},
	{	/* 750CX (82201 and 82202) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00082200,
		.cpu_name		= "750CX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx
	},
	{	/* 750CXe (82214) */
		.pvr_mask		= 0xfffffff0,
		.pvr_value		= 0x00082210,
		.cpu_name		= "750CXe",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx
	},
	{	/* 750CXe "Gekko" (83214) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x00083214,
		.cpu_name		= "750CXe",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750cx
	},
	{	/* 745/755 */
		.pvr_mask		= 0xfffff000,
		.pvr_value		= 0x00083000,
		.cpu_name		= "745/755",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750
	},
	{	/* 750FX rev 1.x */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x70000100,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP |
			CPU_FTR_DUAL_PLL_750FX | CPU_FTR_NO_DPM,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750
	},
	{	/* 750FX rev 2.0 must disable HID0[DPM] */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x70000200,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP |
			CPU_FTR_NO_DPM,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750
	},
	{	/* 750FX (All revs except 2.0) */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x70000000,
		.cpu_name		= "750FX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP |
			CPU_FTR_DUAL_PLL_750FX | CPU_FTR_HAS_HIGH_BATS,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750fx
	},
	{	/* 750GX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x70020000,
		.cpu_name		= "750GX",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_USE_TB |
			CPU_FTR_L2CR | CPU_FTR_TAU | CPU_FTR_HPTE_TABLE |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_DUAL_PLL_750FX |
			CPU_FTR_HAS_HIGH_BATS,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750fx
	},
	{	/* 740/750 (L2CR bit need fixup for 740) */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00080000,
		.cpu_name		= "740/750",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_750
	},
	{	/* 7400 rev 1.1 ? (no TAU) */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x000c1101,
		.cpu_name		= "7400 (1.1)",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP |
			CPU_FTR_HPTE_TABLE | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7400
	},
	{	/* 7400 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x000c0000,
		.cpu_name		= "7400",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_HPTE_TABLE |
			CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7400
	},
	{	/* 7410 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x800c0000,
		.cpu_name		= "7410",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB | CPU_FTR_L2CR | CPU_FTR_TAU |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_HPTE_TABLE |
			CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_7410
	},
	{	/* 7450 2.0 - no doze/nap */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80000200,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7450 2.1 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80000201,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_L3_DISABLE_NAP |
			CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7450 2.3 and newer */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80000000,
		.cpu_name		= "7450",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7455 rev 1.x */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x80010100,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_HAS_HIGH_BATS | CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7455 rev 2.0 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80010200,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_L3_DISABLE_NAP |
			CPU_FTR_NEED_COHERENT | CPU_FTR_HAS_HIGH_BATS,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7455 others */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80010000,
		.cpu_name		= "7455",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_HAS_HIGH_BATS |
			CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7447/7457 Rev 1.0 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80020100,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_HAS_HIGH_BATS |
			CPU_FTR_NEED_COHERENT | CPU_FTR_NO_BTIC,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7447/7457 Rev 1.1 */
		.pvr_mask		= 0xffffffff,
		.pvr_value		= 0x80020101,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_HAS_HIGH_BATS |
			CPU_FTR_NEED_COHERENT | CPU_FTR_NO_BTIC,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7447/7457 Rev 1.2 and later */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80020000,
		.cpu_name		= "7447/7457",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR |
			CPU_FTR_HPTE_TABLE | CPU_FTR_SPEC7450 |
			CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_HAS_HIGH_BATS |
			CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7447A */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80030000,
		.cpu_name		= "7447A",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_HPTE_TABLE |
			CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR |
			CPU_FTR_HAS_HIGH_BATS | CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 7448 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80040000,
		.cpu_name		= "7448",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_HPTE_TABLE |
			CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR |
			CPU_FTR_HAS_HIGH_BATS | CPU_FTR_NEED_COHERENT,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_745x
	},
	{	/* 82xx (8240, 8245, 8260 are all 603e cores) */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00810000,
		.cpu_name		= "82xx",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_MAYBE_CAN_DOZE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* All G2_LE (603e core, plus some) have the same pvr */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00820000,
		.cpu_name		= "G2_LE",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_HAS_HIGH_BATS,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* e300 (a 603e core, plus some) on 83xx */
		.pvr_mask		= 0x7fff0000,
		.pvr_value		= 0x00830000,
		.cpu_name		= "e300",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_USE_TB |
			CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_HAS_HIGH_BATS,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_603
	},
	{	/* default match, we assume split I/D cache & TB (non-601)... */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "(generic PPC)",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.cpu_setup		= __setup_cpu_generic
	},
#endif /* CLASSIC_PPC */
#ifdef CONFIG_PPC64BRIDGE
	{	/* Power3 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00400000,
		.cpu_name		= "Power3 (630)",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3
	},
	{	/* Power3+ */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00410000,
		.cpu_name		= "Power3 (630+)",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3
	},
	{	/* I-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00360000,
		.cpu_name		= "I-star",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3
	},
	{	/* S-star */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00370000,
		.cpu_name		= "S-star",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power3
	},
#endif /* CONFIG_PPC64BRIDGE */
#ifdef CONFIG_POWER4
	{	/* Power4 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00350000,
		.cpu_name		= "Power4",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_power4
	},
	{	/* PPC970 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00390000,
		.cpu_name		= "PPC970",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64 |
			PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970
	},
	{	/* PPC970FX */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x003c0000,
		.cpu_name		= "PPC970FX",
		.cpu_features		= CPU_FTR_COMMON |
			CPU_FTR_SPLIT_ID_CACHE | CPU_FTR_USE_TB |
			CPU_FTR_HPTE_TABLE |
			CPU_FTR_ALTIVEC_COMP | CPU_FTR_MAYBE_CAN_NAP,
		.cpu_user_features	= COMMON_PPC | PPC_FEATURE_64 |
			PPC_FEATURE_ALTIVEC_COMP,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.num_pmcs		= 8,
		.cpu_setup		= __setup_cpu_ppc970
	},
#endif /* CONFIG_POWER4 */
#ifdef CONFIG_8xx
	{	/* 8xx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00500000,
		.cpu_name		= "8xx",
		/* CPU_FTR_MAYBE_CAN_DOZE is possible,
		 * if the 8xx code is there.... */
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
	},
#endif /* CONFIG_8xx */
#ifdef CONFIG_40x
	{	/* 403GC */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x00200200,
		.cpu_name		= "403GC",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
	},
	{	/* 403GCX */
		.pvr_mask		= 0xffffff00,
		.pvr_value		= 0x00201400,
		.cpu_name		= "403GCX",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
		 	PPC_FEATURE_HAS_MMU | PPC_FEATURE_NO_TB,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
	},
	{	/* 403G ?? */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00200000,
		.cpu_name		= "403G ??",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
	},
	{	/* 405GP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40110000,
		.cpu_name		= "405GP",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* STB 03xxx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40130000,
		.cpu_name		= "STB03xxx",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* STB 04xxx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41810000,
		.cpu_name		= "STB04xxx",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* NP405L */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41610000,
		.cpu_name		= "NP405L",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* NP4GS3 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x40B10000,
		.cpu_name		= "NP4GS3",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{   /* NP405H */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41410000,
		.cpu_name		= "NP405H",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* 405GPr */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x50910000,
		.cpu_name		= "405GPr",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{   /* STBx25xx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x51510000,
		.cpu_name		= "STBx25xx",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* 405LP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x41F10000,
		.cpu_name		= "405LP",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* Xilinx Virtex-II Pro  */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x20010000,
		.cpu_name		= "Virtex-II Pro",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{	/* 405EP */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x51210000,
		.cpu_name		= "405EP",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_4xxMAC,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},

#endif /* CONFIG_40x */
#ifdef CONFIG_44x
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000850,
		.cpu_name		= "440EP Rev. A",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= COMMON_PPC, /* 440EP has an FPU */
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x400008d3,
		.cpu_name		= "440EP Rev. B",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= COMMON_PPC, /* 440EP has an FPU */
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ 	/* 440GP Rev. B */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000440,
		.cpu_name		= "440GP Rev. B",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ 	/* 440GP Rev. C */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x40000481,
		.cpu_name		= "440GP Rev. C",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ /* 440GX Rev. A */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000850,
		.cpu_name		= "440GX Rev. A",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ /* 440GX Rev. B */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000851,
		.cpu_name		= "440GX Rev. B",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ /* 440GX Rev. C */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000892,
		.cpu_name		= "440GX Rev. C",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ /* 440GX Rev. F */
		.pvr_mask		= 0xf0000fff,
		.pvr_value		= 0x50000894,
		.cpu_name		= "440GX Rev. F",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
	{ /* 440SP Rev. A */
		.pvr_mask		= 0xff000fff,
		.pvr_value		= 0x53000891,
		.cpu_name		= "440SP Rev. A",
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	},
#endif /* CONFIG_44x */
#ifdef CONFIG_FSL_BOOKE
	{ 	/* e200z5 */
		.pvr_mask		= 0xfff00000,
		.pvr_value		= 0x81000000,
		.cpu_name		= "e200z5",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_HAS_EFP_SINGLE |
			PPC_FEATURE_UNIFIED_CACHE,
		.dcache_bsize		= 32,
	},
	{ 	/* e200z6 */
		.pvr_mask		= 0xfff00000,
		.pvr_value		= 0x81100000,
		.cpu_name		= "e200z6",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE |
			PPC_FEATURE_UNIFIED_CACHE,
		.dcache_bsize		= 32,
	},
	{ 	/* e500 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80200000,
		.cpu_name		= "e500",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
	},
	{ 	/* e500v2 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80210000,
		.cpu_name		= "e500v2",
		/* xxx - galak: add CPU_FTR_MAYBE_CAN_DOZE */
		.cpu_features		= CPU_FTR_SPLIT_ID_CACHE |
			CPU_FTR_USE_TB | CPU_FTR_BIG_PHYS,
		.cpu_user_features	= PPC_FEATURE_32 |
			PPC_FEATURE_HAS_MMU | PPC_FEATURE_SPE_COMP |
			PPC_FEATURE_HAS_EFP_SINGLE | PPC_FEATURE_HAS_EFP_DOUBLE,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
		.num_pmcs		= 4,
	},
#endif
#if !CLASSIC_PPC
	{	/* default match */
		.pvr_mask		= 0x00000000,
		.pvr_value		= 0x00000000,
		.cpu_name		= "(generic PPC)",
		.cpu_features		= CPU_FTR_COMMON,
		.cpu_user_features	= PPC_FEATURE_32,
		.icache_bsize		= 32,
		.dcache_bsize		= 32,
	}
#endif /* !CLASSIC_PPC */
};
