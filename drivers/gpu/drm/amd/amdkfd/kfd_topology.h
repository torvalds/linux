/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __KFD_TOPOLOGY_H__
#define __KFD_TOPOLOGY_H__

#include <linux/types.h>
#include <linux/list.h>
#include "kfd_priv.h"

#define KFD_TOPOLOGY_PUBLIC_NAME_SIZE 128

#define HSA_CAP_HOT_PLUGGABLE			0x00000001
#define HSA_CAP_ATS_PRESENT			0x00000002
#define HSA_CAP_SHARED_WITH_GRAPHICS		0x00000004
#define HSA_CAP_QUEUE_SIZE_POW2			0x00000008
#define HSA_CAP_QUEUE_SIZE_32BIT		0x00000010
#define HSA_CAP_QUEUE_IDLE_EVENT		0x00000020
#define HSA_CAP_VA_LIMIT			0x00000040
#define HSA_CAP_WATCH_POINTS_SUPPORTED		0x00000080
#define HSA_CAP_WATCH_POINTS_TOTALBITS_MASK	0x00000f00
#define HSA_CAP_WATCH_POINTS_TOTALBITS_SHIFT	8
#define HSA_CAP_RESERVED			0xfffff000
#define HSA_CAP_DOORBELL_PACKET_TYPE		0x00001000

struct kfd_node_properties {
	uint32_t cpu_cores_count;
	uint32_t simd_count;
	uint32_t mem_banks_count;
	uint32_t caches_count;
	uint32_t io_links_count;
	uint32_t cpu_core_id_base;
	uint32_t simd_id_base;
	uint32_t capability;
	uint32_t max_waves_per_simd;
	uint32_t lds_size_in_kb;
	uint32_t gds_size_in_kb;
	uint32_t wave_front_size;
	uint32_t array_count;
	uint32_t simd_arrays_per_engine;
	uint32_t cu_per_simd_array;
	uint32_t simd_per_cu;
	uint32_t max_slots_scratch_cu;
	uint32_t engine_id;
	uint32_t vendor_id;
	uint32_t device_id;
	uint32_t location_id;
	uint32_t max_engine_clk_fcompute;
	uint32_t max_engine_clk_ccompute;
	uint16_t marketing_name[KFD_TOPOLOGY_PUBLIC_NAME_SIZE];
};

#define HSA_MEM_HEAP_TYPE_SYSTEM	0
#define HSA_MEM_HEAP_TYPE_FB_PUBLIC	1
#define HSA_MEM_HEAP_TYPE_FB_PRIVATE	2
#define HSA_MEM_HEAP_TYPE_GPU_GDS	3
#define HSA_MEM_HEAP_TYPE_GPU_LDS	4
#define HSA_MEM_HEAP_TYPE_GPU_SCRATCH	5

#define HSA_MEM_FLAGS_HOT_PLUGGABLE	0x00000001
#define HSA_MEM_FLAGS_NON_VOLATILE	0x00000002
#define HSA_MEM_FLAGS_RESERVED		0xfffffffc

struct kfd_mem_properties {
	struct list_head	list;
	uint32_t		heap_type;
	uint64_t		size_in_bytes;
	uint32_t		flags;
	uint32_t		width;
	uint32_t		mem_clk_max;
	struct kobject		*kobj;
	struct attribute	attr;
};

#define KFD_TOPOLOGY_CPU_SIBLINGS 256

#define HSA_CACHE_TYPE_DATA		0x00000001
#define HSA_CACHE_TYPE_INSTRUCTION	0x00000002
#define HSA_CACHE_TYPE_CPU		0x00000004
#define HSA_CACHE_TYPE_HSACU		0x00000008
#define HSA_CACHE_TYPE_RESERVED		0xfffffff0

struct kfd_cache_properties {
	struct list_head	list;
	uint32_t		processor_id_low;
	uint32_t		cache_level;
	uint32_t		cache_size;
	uint32_t		cacheline_size;
	uint32_t		cachelines_per_tag;
	uint32_t		cache_assoc;
	uint32_t		cache_latency;
	uint32_t		cache_type;
	uint8_t			sibling_map[KFD_TOPOLOGY_CPU_SIBLINGS];
	struct kobject		*kobj;
	struct attribute	attr;
};

struct kfd_iolink_properties {
	struct list_head	list;
	uint32_t		iolink_type;
	uint32_t		ver_maj;
	uint32_t		ver_min;
	uint32_t		node_from;
	uint32_t		node_to;
	uint32_t		weight;
	uint32_t		min_latency;
	uint32_t		max_latency;
	uint32_t		min_bandwidth;
	uint32_t		max_bandwidth;
	uint32_t		rec_transfer_size;
	uint32_t		flags;
	struct kobject		*kobj;
	struct attribute	attr;
};

struct kfd_topology_device {
	struct list_head		list;
	uint32_t			gpu_id;
	struct kfd_node_properties	node_props;
	uint32_t			mem_bank_count;
	struct list_head		mem_props;
	uint32_t			cache_count;
	struct list_head		cache_props;
	uint32_t			io_link_count;
	struct list_head		io_link_props;
	struct kfd_dev			*gpu;
	struct kobject			*kobj_node;
	struct kobject			*kobj_mem;
	struct kobject			*kobj_cache;
	struct kobject			*kobj_iolink;
	struct attribute		attr_gpuid;
	struct attribute		attr_name;
	struct attribute		attr_props;
};

struct kfd_system_properties {
	uint32_t		num_devices;     /* Number of H-NUMA nodes */
	uint32_t		generation_count;
	uint64_t		platform_oem;
	uint64_t		platform_id;
	uint64_t		platform_rev;
	struct kobject		*kobj_topology;
	struct kobject		*kobj_nodes;
	struct attribute	attr_genid;
	struct attribute	attr_props;
};



#endif /* __KFD_TOPOLOGY_H__ */
