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

/*
 * MI300 has a fixed, model-specific mapping between a UMC instance and
 * its related Data Fabric Coherent Station instance.
 *
 * The MCA_IPID_UMC[InstanceId] field holds a unique identifier for the
 * UMC instance within a Node. Use this to find the appropriate Coherent
 * Station ID.
 *
 * Redundant bits were removed from the map below.
 */
static const u16 umc_coh_st_map[32] = {
	0x393, 0x293, 0x193, 0x093,
	0x392, 0x292, 0x192, 0x092,
	0x391, 0x291, 0x191, 0x091,
	0x390, 0x290, 0x190, 0x090,
	0x793, 0x693, 0x593, 0x493,
	0x792, 0x692, 0x592, 0x492,
	0x791, 0x691, 0x591, 0x491,
	0x790, 0x690, 0x590, 0x490,
};

#define UMC_ID_MI300 GENMASK(23, 12)
static u8 get_coh_st_inst_id_mi300(struct atl_err *err)
{
	u16 umc_id = FIELD_GET(UMC_ID_MI300, err->ipid);
	u8 i;

	for (i = 0; i < ARRAY_SIZE(umc_coh_st_map); i++) {
		if (umc_id == umc_coh_st_map[i])
			break;
	}

	WARN_ON_ONCE(i >= ARRAY_SIZE(umc_coh_st_map));

	return i;
}

#define MCA_IPID_INST_ID_HI	GENMASK_ULL(47, 44)
static u8 get_die_id(struct atl_err *err)
{
	/*
	 * AMD Node ID is provided in MCA_IPID[InstanceIdHi], and this
	 * needs to be divided by 4 to get the internal Die ID.
	 */
	if (df_cfg.rev == DF4p5 && df_cfg.flags.heterogeneous) {
		u8 node_id = FIELD_GET(MCA_IPID_INST_ID_HI, err->ipid);

		return node_id >> 2;
	}

	/*
	 * For CPUs, this is the AMD Node ID modulo the number
	 * of AMD Nodes per socket.
	 */
	return topology_die_id(err->cpu) % amd_get_nodes_per_socket();
}

#define UMC_CHANNEL_NUM	GENMASK(31, 20)
static u8 get_coh_st_inst_id(struct atl_err *err)
{
	if (df_cfg.rev == DF4p5 && df_cfg.flags.heterogeneous)
		return get_coh_st_inst_id_mi300(err);

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
