/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 */

static struct cpu_spec cpu_specs[] __initdata = {
	{	/* 8xx */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= PVR_8xx,
		.cpu_name		= "8xx",
		/*
		 * CPU_FTR_MAYBE_CAN_DOZE is possible,
		 * if the 8xx code is there....
		 */
		.cpu_features		= CPU_FTRS_8XX,
		.cpu_user_features	= PPC_FEATURE_32 | PPC_FEATURE_HAS_MMU,
		.mmu_features		= MMU_FTR_TYPE_8xx,
		.icache_bsize		= 16,
		.dcache_bsize		= 16,
		.machine_check		= machine_check_8xx,
		.platform		= "ppc823",
	},
};
