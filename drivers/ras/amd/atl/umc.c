// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * umc.c : Unified Memory Controller (UMC) topology helpers
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include "internal.h"

static u8 get_die_id(struct atl_err *err)
{
	/*
	 * For CPUs, this is the AMD Node ID modulo the number
	 * of AMD Nodes per socket.
	 */
	return topology_die_id(err->cpu) % amd_get_nodes_per_socket();
}

#define UMC_CHANNEL_NUM	GENMASK(31, 20)
static u8 get_coh_st_inst_id(struct atl_err *err)
{
	return FIELD_GET(UMC_CHANNEL_NUM, err->ipid);
}

unsigned long convert_umc_mca_addr_to_sys_addr(struct atl_err *err)
{
	u8 socket_id = topology_physical_package_id(err->cpu);
	u8 coh_st_inst_id = get_coh_st_inst_id(err);
	unsigned long addr = err->addr;
	u8 die_id = get_die_id(err);

	pr_debug("socket_id=0x%x die_id=0x%x coh_st_inst_id=0x%x addr=0x%016lx",
		 socket_id, die_id, coh_st_inst_id, addr);

	return norm_to_sys_addr(socket_id, die_id, coh_st_inst_id, addr);
}
