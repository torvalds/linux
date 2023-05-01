/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef K3_PSIL_PRIV_H_
#define K3_PSIL_PRIV_H_

#include <linux/dma/k3-psil.h>

struct psil_ep {
	u32 thread_id;
	struct psil_endpoint_config ep_config;
};

/**
 * struct psil_ep_map - PSI-L thread ID configuration maps
 * @name:	Name of the map, set it to the name of the SoC
 * @src:	Array of source PSI-L thread configurations
 * @src_count:	Number of entries in the src array
 * @dst:	Array of destination PSI-L thread configurations
 * @dst_count:	Number of entries in the dst array
 *
 * In case of symmetric configuration for a matching src/dst thread (for example
 * 0x4400 and 0xc400) only the src configuration can be present. If no dst
 * configuration found the code will look for (dst_thread_id & ~0x8000) to find
 * the symmetric match.
 */
struct psil_ep_map {
	char *name;
	struct psil_ep	*src;
	int src_count;
	struct psil_ep	*dst;
	int dst_count;
};

struct psil_endpoint_config *psil_get_ep_config(u32 thread_id);

/* SoC PSI-L endpoint maps */
extern struct psil_ep_map am654_ep_map;
extern struct psil_ep_map j721e_ep_map;
extern struct psil_ep_map j7200_ep_map;
extern struct psil_ep_map am64_ep_map;
extern struct psil_ep_map j721s2_ep_map;
extern struct psil_ep_map am62_ep_map;
extern struct psil_ep_map am62a_ep_map;

#endif /* K3_PSIL_PRIV_H_ */
