/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef KFD_CRAT_H_INCLUDED
#define KFD_CRAT_H_INCLUDED

#include <linux/types.h>

#pragma pack(1)

/*
 * 4CC signature value for the CRAT ACPI table
 */

#define CRAT_SIGNATURE	"CRAT"

/*
 * Component Resource Association Table (CRAT)
 */

#define CRAT_OEMID_LENGTH	6
#define CRAT_OEMTABLEID_LENGTH	8
#define CRAT_RESERVED_LENGTH	6

#define CRAT_OEMID_64BIT_MASK ((1ULL << (CRAT_OEMID_LENGTH * 8)) - 1)

/* Compute Unit flags */
#define COMPUTE_UNIT_CPU	(1 << 0)  /* Create Virtual CRAT for CPU */
#define COMPUTE_UNIT_GPU	(1 << 1)  /* Create Virtual CRAT for GPU */

struct crat_header {
	uint32_t	signature;
	uint32_t	length;
	uint8_t		revision;
	uint8_t		checksum;
	uint8_t		oem_id[CRAT_OEMID_LENGTH];
	uint8_t		oem_table_id[CRAT_OEMTABLEID_LENGTH];
	uint32_t	oem_revision;
	uint32_t	creator_id;
	uint32_t	creator_revision;
	uint32_t	total_entries;
	uint16_t	num_domains;
	uint8_t		reserved[CRAT_RESERVED_LENGTH];
};

/*
 * The header structure is immediately followed by total_entries of the
 * data definitions
 */

/*
 * The currently defined subtype entries in the CRAT
 */
#define CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY	0
#define CRAT_SUBTYPE_MEMORY_AFFINITY		1
#define CRAT_SUBTYPE_CACHE_AFFINITY		2
#define CRAT_SUBTYPE_TLB_AFFINITY		3
#define CRAT_SUBTYPE_CCOMPUTE_AFFINITY		4
#define CRAT_SUBTYPE_IOLINK_AFFINITY		5
#define CRAT_SUBTYPE_MAX			6

/*
 * Do not change the value of CRAT_SIBLINGMAP_SIZE from 32
 * as it breaks the ABI.
 */
#define CRAT_SIBLINGMAP_SIZE	32

/*
 * ComputeUnit Affinity structure and definitions
 */
#define CRAT_CU_FLAGS_ENABLED		0x00000001
#define CRAT_CU_FLAGS_HOT_PLUGGABLE	0x00000002
#define CRAT_CU_FLAGS_CPU_PRESENT	0x00000004
#define CRAT_CU_FLAGS_GPU_PRESENT	0x00000008
#define CRAT_CU_FLAGS_IOMMU_PRESENT	0x00000010
#define CRAT_CU_FLAGS_RESERVED		0xffffffe0

#define CRAT_COMPUTEUNIT_RESERVED_LENGTH 4

struct crat_subtype_computeunit {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	proximity_domain;
	uint32_t	processor_id_low;
	uint16_t	num_cpu_cores;
	uint16_t	num_simd_cores;
	uint16_t	max_waves_simd;
	uint16_t	io_count;
	uint16_t	hsa_capability;
	uint16_t	lds_size_in_kb;
	uint8_t		wave_front_size;
	uint8_t		num_banks;
	uint16_t	micro_engine_id;
	uint8_t		array_count;
	uint8_t		num_cu_per_array;
	uint8_t		num_simd_per_cu;
	uint8_t		max_slots_scatch_cu;
	uint8_t		reserved2[CRAT_COMPUTEUNIT_RESERVED_LENGTH];
};

/*
 * HSA Memory Affinity structure and definitions
 */
#define CRAT_MEM_FLAGS_ENABLED		0x00000001
#define CRAT_MEM_FLAGS_HOT_PLUGGABLE	0x00000002
#define CRAT_MEM_FLAGS_NON_VOLATILE	0x00000004
#define CRAT_MEM_FLAGS_RESERVED		0xfffffff8

#define CRAT_MEMORY_RESERVED_LENGTH 8

struct crat_subtype_memory {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	proximity_domain;
	uint32_t	base_addr_low;
	uint32_t	base_addr_high;
	uint32_t	length_low;
	uint32_t	length_high;
	uint32_t	width;
	uint8_t		visibility_type; /* for virtual (dGPU) CRAT */
	uint8_t		reserved2[CRAT_MEMORY_RESERVED_LENGTH - 1];
};

/*
 * HSA Cache Affinity structure and definitions
 */
#define CRAT_CACHE_FLAGS_ENABLED	0x00000001
#define CRAT_CACHE_FLAGS_DATA_CACHE	0x00000002
#define CRAT_CACHE_FLAGS_INST_CACHE	0x00000004
#define CRAT_CACHE_FLAGS_CPU_CACHE	0x00000008
#define CRAT_CACHE_FLAGS_SIMD_CACHE	0x00000010
#define CRAT_CACHE_FLAGS_RESERVED	0xffffffe0

#define CRAT_CACHE_RESERVED_LENGTH 8

struct crat_subtype_cache {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	processor_id_low;
	uint8_t		sibling_map[CRAT_SIBLINGMAP_SIZE];
	uint32_t	cache_size;
	uint8_t		cache_level;
	uint8_t		lines_per_tag;
	uint16_t	cache_line_size;
	uint8_t		associativity;
	uint8_t		cache_properties;
	uint16_t	cache_latency;
	uint8_t		reserved2[CRAT_CACHE_RESERVED_LENGTH];
};

/*
 * HSA TLB Affinity structure and definitions
 */
#define CRAT_TLB_FLAGS_ENABLED	0x00000001
#define CRAT_TLB_FLAGS_DATA_TLB	0x00000002
#define CRAT_TLB_FLAGS_INST_TLB	0x00000004
#define CRAT_TLB_FLAGS_CPU_TLB	0x00000008
#define CRAT_TLB_FLAGS_SIMD_TLB	0x00000010
#define CRAT_TLB_FLAGS_RESERVED	0xffffffe0

#define CRAT_TLB_RESERVED_LENGTH 4

struct crat_subtype_tlb {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	processor_id_low;
	uint8_t		sibling_map[CRAT_SIBLINGMAP_SIZE];
	uint32_t	tlb_level;
	uint8_t		data_tlb_associativity_2mb;
	uint8_t		data_tlb_size_2mb;
	uint8_t		instruction_tlb_associativity_2mb;
	uint8_t		instruction_tlb_size_2mb;
	uint8_t		data_tlb_associativity_4k;
	uint8_t		data_tlb_size_4k;
	uint8_t		instruction_tlb_associativity_4k;
	uint8_t		instruction_tlb_size_4k;
	uint8_t		data_tlb_associativity_1gb;
	uint8_t		data_tlb_size_1gb;
	uint8_t		instruction_tlb_associativity_1gb;
	uint8_t		instruction_tlb_size_1gb;
	uint8_t		reserved2[CRAT_TLB_RESERVED_LENGTH];
};

/*
 * HSA CCompute/APU Affinity structure and definitions
 */
#define CRAT_CCOMPUTE_FLAGS_ENABLED	0x00000001
#define CRAT_CCOMPUTE_FLAGS_RESERVED	0xfffffffe

#define CRAT_CCOMPUTE_RESERVED_LENGTH 16

struct crat_subtype_ccompute {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	processor_id_low;
	uint8_t		sibling_map[CRAT_SIBLINGMAP_SIZE];
	uint32_t	apu_size;
	uint8_t		reserved2[CRAT_CCOMPUTE_RESERVED_LENGTH];
};

/*
 * HSA IO Link Affinity structure and definitions
 */
#define CRAT_IOLINK_FLAGS_ENABLED		(1 << 0)
#define CRAT_IOLINK_FLAGS_NON_COHERENT		(1 << 1)
#define CRAT_IOLINK_FLAGS_NO_ATOMICS_32_BIT	(1 << 2)
#define CRAT_IOLINK_FLAGS_NO_ATOMICS_64_BIT	(1 << 3)
#define CRAT_IOLINK_FLAGS_NO_PEER_TO_PEER_DMA	(1 << 4)
#define CRAT_IOLINK_FLAGS_BI_DIRECTIONAL	(1 << 31)
#define CRAT_IOLINK_FLAGS_RESERVED_MASK		0x7fffffe0

/*
 * IO interface types
 */
#define CRAT_IOLINK_TYPE_UNDEFINED	0
#define CRAT_IOLINK_TYPE_HYPERTRANSPORT	1
#define CRAT_IOLINK_TYPE_PCIEXPRESS	2
#define CRAT_IOLINK_TYPE_AMBA		3
#define CRAT_IOLINK_TYPE_MIPI		4
#define CRAT_IOLINK_TYPE_QPI_1_1	5
#define CRAT_IOLINK_TYPE_RESERVED1	6
#define CRAT_IOLINK_TYPE_RESERVED2	7
#define CRAT_IOLINK_TYPE_RAPID_IO	8
#define CRAT_IOLINK_TYPE_INFINIBAND	9
#define CRAT_IOLINK_TYPE_RESERVED3	10
#define CRAT_IOLINK_TYPE_XGMI		11
#define CRAT_IOLINK_TYPE_XGOP		12
#define CRAT_IOLINK_TYPE_GZ		13
#define CRAT_IOLINK_TYPE_ETHERNET_RDMA	14
#define CRAT_IOLINK_TYPE_RDMA_OTHER	15
#define CRAT_IOLINK_TYPE_OTHER		16
#define CRAT_IOLINK_TYPE_MAX		255

#define CRAT_IOLINK_RESERVED_LENGTH	24

struct crat_subtype_iolink {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
	uint32_t	proximity_domain_from;
	uint32_t	proximity_domain_to;
	uint8_t		io_interface_type;
	uint8_t		version_major;
	uint16_t	version_minor;
	uint32_t	minimum_latency;
	uint32_t	maximum_latency;
	uint32_t	minimum_bandwidth_mbs;
	uint32_t	maximum_bandwidth_mbs;
	uint32_t	recommended_transfer_size;
	uint8_t		reserved2[CRAT_IOLINK_RESERVED_LENGTH - 1];
	uint8_t		weight_xgmi;
};

/*
 * HSA generic sub-type header
 */

#define CRAT_SUBTYPE_FLAGS_ENABLED 0x00000001

struct crat_subtype_generic {
	uint8_t		type;
	uint8_t		length;
	uint16_t	reserved;
	uint32_t	flags;
};

#pragma pack()

struct kfd_node;

/* Static table to describe GPU Cache information */
struct kfd_gpu_cache_info {
	uint32_t	cache_size;
	uint32_t	cache_level;
	uint32_t	cache_line_size;
	uint32_t	flags;
	/* Indicates how many Compute Units share this cache
	 * within a SA. Value = 1 indicates the cache is not shared
	 */
	uint32_t	num_cu_shared;
};
int kfd_get_gpu_cache_info(struct kfd_node *kdev, struct kfd_gpu_cache_info **pcache_info);

void kfd_destroy_crat_image(void *crat_image);
int kfd_parse_crat_table(void *crat_image, struct list_head *device_list,
			 uint32_t proximity_domain);
int kfd_create_crat_image_virtual(void **crat_image, size_t *size,
				  int flags, struct kfd_node *kdev,
				  uint32_t proximity_domain);

#endif /* KFD_CRAT_H_INCLUDED */
