/*
 * Copyright 2015-2017 Advanced Micro Devices, Inc.
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
#include <linux/acpi.h>
#include "kfd_crat.h"
#include "kfd_topology.h"

static int topology_crat_parsed;
extern struct list_head topology_device_list;
extern struct kfd_system_properties sys_props;

static void kfd_populated_cu_info_cpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	dev->node_props.cpu_cores_count = cu->num_cpu_cores;
	dev->node_props.cpu_core_id_base = cu->processor_id_low;
	if (cu->hsa_capability & CRAT_CU_FLAGS_IOMMU_PRESENT)
		dev->node_props.capability |= HSA_CAP_ATS_PRESENT;

	pr_info("CU CPU: cores=%d id_base=%d\n", cu->num_cpu_cores,
			cu->processor_id_low);
}

static void kfd_populated_cu_info_gpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	dev->node_props.simd_id_base = cu->processor_id_low;
	dev->node_props.simd_count = cu->num_simd_cores;
	dev->node_props.lds_size_in_kb = cu->lds_size_in_kb;
	dev->node_props.max_waves_per_simd = cu->max_waves_simd;
	dev->node_props.wave_front_size = cu->wave_front_size;
	dev->node_props.mem_banks_count = cu->num_banks;
	dev->node_props.array_count = cu->num_arrays;
	dev->node_props.cu_per_simd_array = cu->num_cu_per_array;
	dev->node_props.simd_per_cu = cu->num_simd_per_cu;
	dev->node_props.max_slots_scratch_cu = cu->max_slots_scatch_cu;
	if (cu->hsa_capability & CRAT_CU_FLAGS_HOT_PLUGGABLE)
		dev->node_props.capability |= HSA_CAP_HOT_PLUGGABLE;
	pr_info("CU GPU: id_base=%d\n", cu->processor_id_low);
}

/* kfd_parse_subtype_cu is called when the topology mutex is already acquired */
static int kfd_parse_subtype_cu(struct crat_subtype_computeunit *cu)
{
	struct kfd_topology_device *dev;
	int i = 0;

	pr_info("Found CU entry in CRAT table with proximity_domain=%d caps=%x\n",
			cu->proximity_domain, cu->hsa_capability);
	list_for_each_entry(dev, &topology_device_list, list) {
		if (cu->proximity_domain == i) {
			if (cu->flags & CRAT_CU_FLAGS_CPU_PRESENT)
				kfd_populated_cu_info_cpu(dev, cu);

			if (cu->flags & CRAT_CU_FLAGS_GPU_PRESENT)
				kfd_populated_cu_info_gpu(dev, cu);
			break;
		}
		i++;
	}

	return 0;
}

/*
 * kfd_parse_subtype_mem is called when the topology mutex is
 * already acquired
 */
static int kfd_parse_subtype_mem(struct crat_subtype_memory *mem)
{
	struct kfd_mem_properties *props;
	struct kfd_topology_device *dev;
	int i = 0;

	pr_info("Found memory entry in CRAT table with proximity_domain=%d\n",
			mem->proximity_domain);
	list_for_each_entry(dev, &topology_device_list, list) {
		if (mem->proximity_domain == i) {
			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			if (dev->node_props.cpu_cores_count == 0)
				props->heap_type = HSA_MEM_HEAP_TYPE_FB_PRIVATE;
			else
				props->heap_type = HSA_MEM_HEAP_TYPE_SYSTEM;

			if (mem->flags & CRAT_MEM_FLAGS_HOT_PLUGGABLE)
				props->flags |= HSA_MEM_FLAGS_HOT_PLUGGABLE;
			if (mem->flags & CRAT_MEM_FLAGS_NON_VOLATILE)
				props->flags |= HSA_MEM_FLAGS_NON_VOLATILE;

			props->size_in_bytes =
				((uint64_t)mem->length_high << 32) +
							mem->length_low;
			props->width = mem->width;

			dev->mem_bank_count++;
			list_add_tail(&props->list, &dev->mem_props);

			break;
		}
		i++;
	}

	return 0;
}

/*
 * kfd_parse_subtype_cache is called when the topology mutex
 * is already acquired
 */
static int kfd_parse_subtype_cache(struct crat_subtype_cache *cache)
{
	struct kfd_cache_properties *props;
	struct kfd_topology_device *dev;
	uint32_t id;

	id = cache->processor_id_low;

	pr_info("Found cache entry in CRAT table with processor_id=%d\n", id);
	list_for_each_entry(dev, &topology_device_list, list)
		if (id == dev->node_props.cpu_core_id_base ||
		    id == dev->node_props.simd_id_base) {
			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			props->processor_id_low = id;
			props->cache_level = cache->cache_level;
			props->cache_size = cache->cache_size;
			props->cacheline_size = cache->cache_line_size;
			props->cachelines_per_tag = cache->lines_per_tag;
			props->cache_assoc = cache->associativity;
			props->cache_latency = cache->cache_latency;

			if (cache->flags & CRAT_CACHE_FLAGS_DATA_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_DATA;
			if (cache->flags & CRAT_CACHE_FLAGS_INST_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_INSTRUCTION;
			if (cache->flags & CRAT_CACHE_FLAGS_CPU_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_CPU;
			if (cache->flags & CRAT_CACHE_FLAGS_SIMD_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_HSACU;

			dev->cache_count++;
			dev->node_props.caches_count++;
			list_add_tail(&props->list, &dev->cache_props);

			break;
		}

	return 0;
}

/*
 * kfd_parse_subtype_iolink is called when the topology mutex
 * is already acquired
 */
static int kfd_parse_subtype_iolink(struct crat_subtype_iolink *iolink)
{
	struct kfd_iolink_properties *props;
	struct kfd_topology_device *dev;
	uint32_t i = 0;
	uint32_t id_from;
	uint32_t id_to;

	id_from = iolink->proximity_domain_from;
	id_to = iolink->proximity_domain_to;

	pr_info("Found IO link entry in CRAT table with id_from=%d\n", id_from);
	list_for_each_entry(dev, &topology_device_list, list) {
		if (id_from == i) {
			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			props->node_from = id_from;
			props->node_to = id_to;
			props->ver_maj = iolink->version_major;
			props->ver_min = iolink->version_minor;

			/*
			 * weight factor (derived from CDIR), currently always 1
			 */
			props->weight = 1;

			props->min_latency = iolink->minimum_latency;
			props->max_latency = iolink->maximum_latency;
			props->min_bandwidth = iolink->minimum_bandwidth_mbs;
			props->max_bandwidth = iolink->maximum_bandwidth_mbs;
			props->rec_transfer_size =
					iolink->recommended_transfer_size;

			dev->io_link_count++;
			dev->node_props.io_links_count++;
			list_add_tail(&props->list, &dev->io_link_props);

			break;
		}
		i++;
	}

	return 0;
}

static int kfd_parse_subtype(struct crat_subtype_generic *sub_type_hdr)
{
	struct crat_subtype_computeunit *cu;
	struct crat_subtype_memory *mem;
	struct crat_subtype_cache *cache;
	struct crat_subtype_iolink *iolink;
	int ret = 0;

	switch (sub_type_hdr->type) {
	case CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY:
		cu = (struct crat_subtype_computeunit *)sub_type_hdr;
		ret = kfd_parse_subtype_cu(cu);
		break;
	case CRAT_SUBTYPE_MEMORY_AFFINITY:
		mem = (struct crat_subtype_memory *)sub_type_hdr;
		ret = kfd_parse_subtype_mem(mem);
		break;
	case CRAT_SUBTYPE_CACHE_AFFINITY:
		cache = (struct crat_subtype_cache *)sub_type_hdr;
		ret = kfd_parse_subtype_cache(cache);
		break;
	case CRAT_SUBTYPE_TLB_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_info("Found TLB entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_CCOMPUTE_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_info("Found CCOMPUTE entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_IOLINK_AFFINITY:
		iolink = (struct crat_subtype_iolink *)sub_type_hdr;
		ret = kfd_parse_subtype_iolink(iolink);
		break;
	default:
		pr_warn("Unknown subtype %d in CRAT\n",
				sub_type_hdr->type);
	}

	return ret;
}

int kfd_parse_crat_table(void *crat_image)
{
	struct kfd_topology_device *top_dev;
	struct crat_subtype_generic *sub_type_hdr;
	uint16_t node_id;
	int ret;
	struct crat_header *crat_table = (struct crat_header *)crat_image;
	uint16_t num_nodes;
	uint32_t image_len;

	if (!crat_image)
		return -EINVAL;

	num_nodes = crat_table->num_domains;
	image_len = crat_table->length;

	pr_info("Parsing CRAT table with %d nodes\n", num_nodes);

	for (node_id = 0; node_id < num_nodes; node_id++) {
		top_dev = kfd_create_topology_device();
		if (!top_dev) {
			kfd_release_live_view();
			return -ENOMEM;
		}
	}

	sys_props.platform_id =
		(*((uint64_t *)crat_table->oem_id)) & CRAT_OEMID_64BIT_MASK;
	sys_props.platform_oem = *((uint64_t *)crat_table->oem_table_id);
	sys_props.platform_rev = crat_table->revision;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table+1);
	while ((char *)sub_type_hdr + sizeof(struct crat_subtype_generic) <
			((char *)crat_image) + image_len) {
		if (sub_type_hdr->flags & CRAT_SUBTYPE_FLAGS_ENABLED) {
			ret = kfd_parse_subtype(sub_type_hdr);
			if (ret != 0) {
				kfd_release_live_view();
				return ret;
			}
		}

		sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
				sub_type_hdr->length);
	}

	sys_props.generation_count++;
	topology_crat_parsed = 1;

	return 0;
}

/*
 * kfd_create_crat_image_acpi - Allocates memory for CRAT image and
 * copies CRAT from ACPI (if available).
 * NOTE: Call kfd_destroy_crat_image to free CRAT image memory
 *
 *	@crat_image: CRAT read from ACPI. If no CRAT in ACPI then
 *		     crat_image will be NULL
 *	@size: [OUT] size of crat_image
 *
 *	Return 0 if successful else return error code
 */
int kfd_create_crat_image_acpi(void **crat_image, size_t *size)
{
	struct acpi_table_header *crat_table;
	acpi_status status;
	void *pcrat_image;

	if (!crat_image)
		return -EINVAL;

	*crat_image = NULL;

	/* Fetch the CRAT table from ACPI */
	status = acpi_get_table(CRAT_SIGNATURE, 0, &crat_table);
	if (status == AE_NOT_FOUND) {
		pr_warn("CRAT table not found\n");
		return -ENODATA;
	} else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);

		pr_err("CRAT table error: %s\n", err);
		return -EINVAL;
	}

	pcrat_image = kmalloc(crat_table->length, GFP_KERNEL);
	if (!pcrat_image)
		return -ENOMEM;

	memcpy(pcrat_image, crat_table, crat_table->length);

	*crat_image = pcrat_image;
	*size = crat_table->length;

	return 0;
}

/*
 * kfd_destroy_crat_image
 *
 *	@crat_image: [IN] - crat_image from kfd_create_crat_image_xxx(..)
 *
 */
void kfd_destroy_crat_image(void *crat_image)
{
	kfree(crat_image);
}
