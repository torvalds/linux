/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 */

#ifdef CONFIG_PPC64
#define COMMON_USER_BOOKE	(PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU | \
				 PPC_FEATURE_HAS_FPU | PPC_FEATURE_64 | \
				 PPC_FEATURE_BOOKE)
#else
#define COMMON_USER_BOOKE	(PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU | \
				 PPC_FEATURE_BOOKE)
#endif

static struct cpu_spec cpu_specs[] __initdata = {
#ifdef CONFIG_PPC32
	{	/* e500mc */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80230000,
		.cpu_name		= "e500mc",
		.cpu_features		= CPU_FTRS_E500MC,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.cpu_user_features2	= PPC_FEATURE2_ISEL,
		.mmu_features		= MMU_FTR_TYPE_FSL_E | MMU_FTR_BIG_PHYS | MMU_FTR_USE_TLBILX,
		.icache_bsize		= 64,
		.dcache_bsize		= 64,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_e500mc,
		.machine_check		= machine_check_e500mc,
		.platform		= "ppce500mc",
		.cpu_down_flush		= cpu_down_flush_e500mc,
	},
#endif /* CONFIG_PPC32 */
	{	/* e5500 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80240000,
		.cpu_name		= "e5500",
		.cpu_features		= CPU_FTRS_E5500,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU,
		.cpu_user_features2	= PPC_FEATURE2_ISEL,
		.mmu_features		= MMU_FTR_TYPE_FSL_E | MMU_FTR_BIG_PHYS | MMU_FTR_USE_TLBILX,
		.icache_bsize		= 64,
		.dcache_bsize		= 64,
		.num_pmcs		= 4,
		.cpu_setup		= __setup_cpu_e5500,
#ifndef CONFIG_PPC32
		.cpu_restore		= __restore_cpu_e5500,
#endif
		.machine_check		= machine_check_e500mc,
		.platform		= "ppce5500",
		.cpu_down_flush		= cpu_down_flush_e5500,
	},
	{	/* e6500 */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x80400000,
		.cpu_name		= "e6500",
		.cpu_features		= CPU_FTRS_E6500,
		.cpu_user_features	= COMMON_USER_BOOKE | PPC_FEATURE_HAS_FPU |
					  PPC_FEATURE_HAS_ALTIVEC_COMP,
		.cpu_user_features2	= PPC_FEATURE2_ISEL,
		.mmu_features		= MMU_FTR_TYPE_FSL_E | MMU_FTR_BIG_PHYS | MMU_FTR_USE_TLBILX,
		.icache_bsize		= 64,
		.dcache_bsize		= 64,
		.num_pmcs		= 6,
		.cpu_setup		= __setup_cpu_e6500,
#ifndef CONFIG_PPC32
		.cpu_restore		= __restore_cpu_e6500,
#endif
		.machine_check		= machine_check_e500mc,
		.platform		= "ppce6500",
		.cpu_down_flush		= cpu_down_flush_e6500,
	},
};
