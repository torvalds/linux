/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_MEM_SHARE_H
#define _LINUX_MEM_SHARE_H

#define MEM_SHARE_SERVICE_SVC_ID 0x00000034
#define MEM_SHARE_SERVICE_INS_ID 1
#define MEM_SHARE_SERVICE_VERS 1

#define MEMORY_CMA	1
#define MEMORY_NON_CMA	0
#define MAX_CLIENTS 10
#define GPS	0
#define CHECK	0
#define FREE	1
#define MEMSHARE_GUARD_BYTES	(4*1024)

struct memshare_hyp_mapping {
	u32 num_vmids;
	u32 vmids[2];
	u32 perms[2];
};

struct mem_blocks {
	/* Client Id information */
	uint32_t client_id;
	/* Peripheral associated with client */
	uint32_t peripheral;
	/* Sequence Id */
	uint32_t sequence_id;
	/* CMA or Non-CMA region */
	uint32_t memory_type;
	/* Guaranteed Memory */
	uint32_t guarantee;
	/* Memory alloted or not */
	uint32_t allotted;
	/* Memory allocation request received or not */
	uint32_t alloc_request;
	/* Allocation on request from a client*/
	uint32_t client_request;
	/* Guard band around the allotted region*/
	uint32_t guard_band;
	/* mapping to be assigned to memory region */
	struct memshare_hyp_mapping hyp_map_info;
	/* Size required for client */
	uint32_t size;
	/* Available memory size for client */
	uint32_t init_size;
	/*
	 * start address of the memory block reserved by server memory
	 * subsystem to client
	 */
	phys_addr_t phy_addr;
	/* Virtual address for the physical address allocated */
	void *virtual_addr;
	/* Release memory only when XPU is released*/
	uint8_t free_memory;
	/* Need Hypervisor mapping*/
	uint8_t hyp_mapping;
};

int memshare_alloc(struct device *dev,
					unsigned int block_size,
					struct mem_blocks *pblk);
void memshare_free(unsigned int block_size,
					struct mem_blocks *pblk);
#endif /* _LINUX_MEM_SHARE_H */
