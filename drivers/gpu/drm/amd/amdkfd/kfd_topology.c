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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/hash.h>
#include <linux/cpufreq.h>
#include <linux/log2.h>

#include "kfd_priv.h"
#include "kfd_crat.h"
#include "kfd_topology.h"

static struct list_head topology_device_list;
static int topology_crat_parsed;
static struct kfd_system_properties sys_props;

static DECLARE_RWSEM(topology_lock);

struct kfd_dev *kfd_device_by_id(uint32_t gpu_id)
{
	struct kfd_topology_device *top_dev;
	struct kfd_dev *device = NULL;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->gpu_id == gpu_id) {
			device = top_dev->gpu;
			break;
		}

	up_read(&topology_lock);

	return device;
}

struct kfd_dev *kfd_device_by_pci_dev(const struct pci_dev *pdev)
{
	struct kfd_topology_device *top_dev;
	struct kfd_dev *device = NULL;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->gpu->pdev == pdev) {
			device = top_dev->gpu;
			break;
		}

	up_read(&topology_lock);

	return device;
}

static int kfd_topology_get_crat_acpi(void *crat_image, size_t *size)
{
	struct acpi_table_header *crat_table;
	acpi_status status;

	if (!size)
		return -EINVAL;

	/*
	 * Fetch the CRAT table from ACPI
	 */
	status = acpi_get_table(CRAT_SIGNATURE, 0, &crat_table);
	if (status == AE_NOT_FOUND) {
		pr_warn("CRAT table not found\n");
		return -ENODATA;
	} else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);

		pr_err("CRAT table error: %s\n", err);
		return -EINVAL;
	}

	if (*size >= crat_table->length && crat_image != NULL)
		memcpy(crat_image, crat_table, crat_table->length);

	*size = crat_table->length;

	return 0;
}

static void kfd_populated_cu_info_cpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	BUG_ON(!dev);
	BUG_ON(!cu);

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
	BUG_ON(!dev);
	BUG_ON(!cu);

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
	pr_info("CU GPU: simds=%d id_base=%d\n", cu->num_simd_cores,
				cu->processor_id_low);
}

/* kfd_parse_subtype_cu is called when the topology mutex is already acquired */
static int kfd_parse_subtype_cu(struct crat_subtype_computeunit *cu)
{
	struct kfd_topology_device *dev;
	int i = 0;

	BUG_ON(!cu);

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

	BUG_ON(!mem);

	pr_info("Found memory entry in CRAT table with proximity_domain=%d\n",
			mem->promixity_domain);
	list_for_each_entry(dev, &topology_device_list, list) {
		if (mem->promixity_domain == i) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
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

	BUG_ON(!cache);

	id = cache->processor_id_low;

	pr_info("Found cache entry in CRAT table with processor_id=%d\n", id);
	list_for_each_entry(dev, &topology_device_list, list)
		if (id == dev->node_props.cpu_core_id_base ||
		    id == dev->node_props.simd_id_base) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
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

	BUG_ON(!iolink);

	id_from = iolink->proximity_domain_from;
	id_to = iolink->proximity_domain_to;

	pr_info("Found IO link entry in CRAT table with id_from=%d\n", id_from);
	list_for_each_entry(dev, &topology_device_list, list) {
		if (id_from == i) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
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

	BUG_ON(!sub_type_hdr);

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
		pr_warn("Unknown subtype (%d) in CRAT\n",
				sub_type_hdr->type);
	}

	return ret;
}

static void kfd_release_topology_device(struct kfd_topology_device *dev)
{
	struct kfd_mem_properties *mem;
	struct kfd_cache_properties *cache;
	struct kfd_iolink_properties *iolink;

	BUG_ON(!dev);

	list_del(&dev->list);

	while (dev->mem_props.next != &dev->mem_props) {
		mem = container_of(dev->mem_props.next,
				struct kfd_mem_properties, list);
		list_del(&mem->list);
		kfree(mem);
	}

	while (dev->cache_props.next != &dev->cache_props) {
		cache = container_of(dev->cache_props.next,
				struct kfd_cache_properties, list);
		list_del(&cache->list);
		kfree(cache);
	}

	while (dev->io_link_props.next != &dev->io_link_props) {
		iolink = container_of(dev->io_link_props.next,
				struct kfd_iolink_properties, list);
		list_del(&iolink->list);
		kfree(iolink);
	}

	kfree(dev);

	sys_props.num_devices--;
}

static void kfd_release_live_view(void)
{
	struct kfd_topology_device *dev;

	while (topology_device_list.next != &topology_device_list) {
		dev = container_of(topology_device_list.next,
				 struct kfd_topology_device, list);
		kfd_release_topology_device(dev);
}

	memset(&sys_props, 0, sizeof(sys_props));
}

static struct kfd_topology_device *kfd_create_topology_device(void)
{
	struct kfd_topology_device *dev;

	dev = kfd_alloc_struct(dev);
	if (dev == NULL) {
		pr_err("No memory to allocate a topology device");
		return NULL;
	}

	INIT_LIST_HEAD(&dev->mem_props);
	INIT_LIST_HEAD(&dev->cache_props);
	INIT_LIST_HEAD(&dev->io_link_props);

	list_add_tail(&dev->list, &topology_device_list);
	sys_props.num_devices++;

	return dev;
}

static int kfd_parse_crat_table(void *crat_image)
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


#define sysfs_show_gen_prop(buffer, fmt, ...) \
		snprintf(buffer, PAGE_SIZE, "%s"fmt, buffer, __VA_ARGS__)
#define sysfs_show_32bit_prop(buffer, name, value) \
		sysfs_show_gen_prop(buffer, "%s %u\n", name, value)
#define sysfs_show_64bit_prop(buffer, name, value) \
		sysfs_show_gen_prop(buffer, "%s %llu\n", name, value)
#define sysfs_show_32bit_val(buffer, value) \
		sysfs_show_gen_prop(buffer, "%u\n", value)
#define sysfs_show_str_val(buffer, value) \
		sysfs_show_gen_prop(buffer, "%s\n", value)

static ssize_t sysprops_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	ssize_t ret;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	if (attr == &sys_props.attr_genid) {
		ret = sysfs_show_32bit_val(buffer, sys_props.generation_count);
	} else if (attr == &sys_props.attr_props) {
		sysfs_show_64bit_prop(buffer, "platform_oem",
				sys_props.platform_oem);
		sysfs_show_64bit_prop(buffer, "platform_id",
				sys_props.platform_id);
		ret = sysfs_show_64bit_prop(buffer, "platform_rev",
				sys_props.platform_rev);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static const struct sysfs_ops sysprops_ops = {
	.show = sysprops_show,
};

static struct kobj_type sysprops_type = {
	.sysfs_ops = &sysprops_ops,
};

static ssize_t iolink_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	ssize_t ret;
	struct kfd_iolink_properties *iolink;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	iolink = container_of(attr, struct kfd_iolink_properties, attr);
	sysfs_show_32bit_prop(buffer, "type", iolink->iolink_type);
	sysfs_show_32bit_prop(buffer, "version_major", iolink->ver_maj);
	sysfs_show_32bit_prop(buffer, "version_minor", iolink->ver_min);
	sysfs_show_32bit_prop(buffer, "node_from", iolink->node_from);
	sysfs_show_32bit_prop(buffer, "node_to", iolink->node_to);
	sysfs_show_32bit_prop(buffer, "weight", iolink->weight);
	sysfs_show_32bit_prop(buffer, "min_latency", iolink->min_latency);
	sysfs_show_32bit_prop(buffer, "max_latency", iolink->max_latency);
	sysfs_show_32bit_prop(buffer, "min_bandwidth", iolink->min_bandwidth);
	sysfs_show_32bit_prop(buffer, "max_bandwidth", iolink->max_bandwidth);
	sysfs_show_32bit_prop(buffer, "recommended_transfer_size",
			iolink->rec_transfer_size);
	ret = sysfs_show_32bit_prop(buffer, "flags", iolink->flags);

	return ret;
}

static const struct sysfs_ops iolink_ops = {
	.show = iolink_show,
};

static struct kobj_type iolink_type = {
	.sysfs_ops = &iolink_ops,
};

static ssize_t mem_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	ssize_t ret;
	struct kfd_mem_properties *mem;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	mem = container_of(attr, struct kfd_mem_properties, attr);
	sysfs_show_32bit_prop(buffer, "heap_type", mem->heap_type);
	sysfs_show_64bit_prop(buffer, "size_in_bytes", mem->size_in_bytes);
	sysfs_show_32bit_prop(buffer, "flags", mem->flags);
	sysfs_show_32bit_prop(buffer, "width", mem->width);
	ret = sysfs_show_32bit_prop(buffer, "mem_clk_max", mem->mem_clk_max);

	return ret;
}

static const struct sysfs_ops mem_ops = {
	.show = mem_show,
};

static struct kobj_type mem_type = {
	.sysfs_ops = &mem_ops,
};

static ssize_t kfd_cache_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	ssize_t ret;
	uint32_t i;
	struct kfd_cache_properties *cache;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	cache = container_of(attr, struct kfd_cache_properties, attr);
	sysfs_show_32bit_prop(buffer, "processor_id_low",
			cache->processor_id_low);
	sysfs_show_32bit_prop(buffer, "level", cache->cache_level);
	sysfs_show_32bit_prop(buffer, "size", cache->cache_size);
	sysfs_show_32bit_prop(buffer, "cache_line_size", cache->cacheline_size);
	sysfs_show_32bit_prop(buffer, "cache_lines_per_tag",
			cache->cachelines_per_tag);
	sysfs_show_32bit_prop(buffer, "association", cache->cache_assoc);
	sysfs_show_32bit_prop(buffer, "latency", cache->cache_latency);
	sysfs_show_32bit_prop(buffer, "type", cache->cache_type);
	snprintf(buffer, PAGE_SIZE, "%ssibling_map ", buffer);
	for (i = 0; i < KFD_TOPOLOGY_CPU_SIBLINGS; i++)
		ret = snprintf(buffer, PAGE_SIZE, "%s%d%s",
				buffer, cache->sibling_map[i],
				(i == KFD_TOPOLOGY_CPU_SIBLINGS-1) ?
						"\n" : ",");

	return ret;
}

static const struct sysfs_ops cache_ops = {
	.show = kfd_cache_show,
};

static struct kobj_type cache_type = {
	.sysfs_ops = &cache_ops,
};

static ssize_t node_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	struct kfd_topology_device *dev;
	char public_name[KFD_TOPOLOGY_PUBLIC_NAME_SIZE];
	uint32_t i;
	uint32_t log_max_watch_addr;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	if (strcmp(attr->name, "gpu_id") == 0) {
		dev = container_of(attr, struct kfd_topology_device,
				attr_gpuid);
		return sysfs_show_32bit_val(buffer, dev->gpu_id);
	}

	if (strcmp(attr->name, "name") == 0) {
		dev = container_of(attr, struct kfd_topology_device,
				attr_name);
		for (i = 0; i < KFD_TOPOLOGY_PUBLIC_NAME_SIZE; i++) {
			public_name[i] =
					(char)dev->node_props.marketing_name[i];
			if (dev->node_props.marketing_name[i] == 0)
				break;
		}
		public_name[KFD_TOPOLOGY_PUBLIC_NAME_SIZE-1] = 0x0;
		return sysfs_show_str_val(buffer, public_name);
	}

	dev = container_of(attr, struct kfd_topology_device,
			attr_props);
	sysfs_show_32bit_prop(buffer, "cpu_cores_count",
			dev->node_props.cpu_cores_count);
	sysfs_show_32bit_prop(buffer, "simd_count",
			dev->node_props.simd_count);

	if (dev->mem_bank_count < dev->node_props.mem_banks_count) {
		pr_info_once("kfd: mem_banks_count truncated from %d to %d\n",
				dev->node_props.mem_banks_count,
				dev->mem_bank_count);
		sysfs_show_32bit_prop(buffer, "mem_banks_count",
				dev->mem_bank_count);
	} else {
		sysfs_show_32bit_prop(buffer, "mem_banks_count",
				dev->node_props.mem_banks_count);
	}

	sysfs_show_32bit_prop(buffer, "caches_count",
			dev->node_props.caches_count);
	sysfs_show_32bit_prop(buffer, "io_links_count",
			dev->node_props.io_links_count);
	sysfs_show_32bit_prop(buffer, "cpu_core_id_base",
			dev->node_props.cpu_core_id_base);
	sysfs_show_32bit_prop(buffer, "simd_id_base",
			dev->node_props.simd_id_base);
	sysfs_show_32bit_prop(buffer, "max_waves_per_simd",
			dev->node_props.max_waves_per_simd);
	sysfs_show_32bit_prop(buffer, "lds_size_in_kb",
			dev->node_props.lds_size_in_kb);
	sysfs_show_32bit_prop(buffer, "gds_size_in_kb",
			dev->node_props.gds_size_in_kb);
	sysfs_show_32bit_prop(buffer, "wave_front_size",
			dev->node_props.wave_front_size);
	sysfs_show_32bit_prop(buffer, "array_count",
			dev->node_props.array_count);
	sysfs_show_32bit_prop(buffer, "simd_arrays_per_engine",
			dev->node_props.simd_arrays_per_engine);
	sysfs_show_32bit_prop(buffer, "cu_per_simd_array",
			dev->node_props.cu_per_simd_array);
	sysfs_show_32bit_prop(buffer, "simd_per_cu",
			dev->node_props.simd_per_cu);
	sysfs_show_32bit_prop(buffer, "max_slots_scratch_cu",
			dev->node_props.max_slots_scratch_cu);
	sysfs_show_32bit_prop(buffer, "vendor_id",
			dev->node_props.vendor_id);
	sysfs_show_32bit_prop(buffer, "device_id",
			dev->node_props.device_id);
	sysfs_show_32bit_prop(buffer, "location_id",
			dev->node_props.location_id);

	if (dev->gpu) {
		log_max_watch_addr =
			__ilog2_u32(dev->gpu->device_info->num_of_watch_points);

		if (log_max_watch_addr) {
			dev->node_props.capability |=
					HSA_CAP_WATCH_POINTS_SUPPORTED;

			dev->node_props.capability |=
				((log_max_watch_addr <<
					HSA_CAP_WATCH_POINTS_TOTALBITS_SHIFT) &
				HSA_CAP_WATCH_POINTS_TOTALBITS_MASK);
		}

		sysfs_show_32bit_prop(buffer, "max_engine_clk_fcompute",
			dev->gpu->kfd2kgd->get_max_engine_clock_in_mhz(
					dev->gpu->kgd));

		sysfs_show_64bit_prop(buffer, "local_mem_size",
				(unsigned long long int) 0);

		sysfs_show_32bit_prop(buffer, "fw_version",
			dev->gpu->kfd2kgd->get_fw_version(
						dev->gpu->kgd,
						KGD_ENGINE_MEC1));
		sysfs_show_32bit_prop(buffer, "capability",
				dev->node_props.capability);
	}

	return sysfs_show_32bit_prop(buffer, "max_engine_clk_ccompute",
					cpufreq_quick_get_max(0)/1000);
}

static const struct sysfs_ops node_ops = {
	.show = node_show,
};

static struct kobj_type node_type = {
	.sysfs_ops = &node_ops,
};

static void kfd_remove_sysfs_file(struct kobject *kobj, struct attribute *attr)
{
	sysfs_remove_file(kobj, attr);
	kobject_del(kobj);
	kobject_put(kobj);
}

static void kfd_remove_sysfs_node_entry(struct kfd_topology_device *dev)
{
	struct kfd_iolink_properties *iolink;
	struct kfd_cache_properties *cache;
	struct kfd_mem_properties *mem;

	BUG_ON(!dev);

	if (dev->kobj_iolink) {
		list_for_each_entry(iolink, &dev->io_link_props, list)
			if (iolink->kobj) {
				kfd_remove_sysfs_file(iolink->kobj,
							&iolink->attr);
				iolink->kobj = NULL;
			}
		kobject_del(dev->kobj_iolink);
		kobject_put(dev->kobj_iolink);
		dev->kobj_iolink = NULL;
	}

	if (dev->kobj_cache) {
		list_for_each_entry(cache, &dev->cache_props, list)
			if (cache->kobj) {
				kfd_remove_sysfs_file(cache->kobj,
							&cache->attr);
				cache->kobj = NULL;
			}
		kobject_del(dev->kobj_cache);
		kobject_put(dev->kobj_cache);
		dev->kobj_cache = NULL;
	}

	if (dev->kobj_mem) {
		list_for_each_entry(mem, &dev->mem_props, list)
			if (mem->kobj) {
				kfd_remove_sysfs_file(mem->kobj, &mem->attr);
				mem->kobj = NULL;
			}
		kobject_del(dev->kobj_mem);
		kobject_put(dev->kobj_mem);
		dev->kobj_mem = NULL;
	}

	if (dev->kobj_node) {
		sysfs_remove_file(dev->kobj_node, &dev->attr_gpuid);
		sysfs_remove_file(dev->kobj_node, &dev->attr_name);
		sysfs_remove_file(dev->kobj_node, &dev->attr_props);
		kobject_del(dev->kobj_node);
		kobject_put(dev->kobj_node);
		dev->kobj_node = NULL;
	}
}

static int kfd_build_sysfs_node_entry(struct kfd_topology_device *dev,
		uint32_t id)
{
	struct kfd_iolink_properties *iolink;
	struct kfd_cache_properties *cache;
	struct kfd_mem_properties *mem;
	int ret;
	uint32_t i;

	BUG_ON(!dev);

	/*
	 * Creating the sysfs folders
	 */
	BUG_ON(dev->kobj_node);
	dev->kobj_node = kfd_alloc_struct(dev->kobj_node);
	if (!dev->kobj_node)
		return -ENOMEM;

	ret = kobject_init_and_add(dev->kobj_node, &node_type,
			sys_props.kobj_nodes, "%d", id);
	if (ret < 0)
		return ret;

	dev->kobj_mem = kobject_create_and_add("mem_banks", dev->kobj_node);
	if (!dev->kobj_mem)
		return -ENOMEM;

	dev->kobj_cache = kobject_create_and_add("caches", dev->kobj_node);
	if (!dev->kobj_cache)
		return -ENOMEM;

	dev->kobj_iolink = kobject_create_and_add("io_links", dev->kobj_node);
	if (!dev->kobj_iolink)
		return -ENOMEM;

	/*
	 * Creating sysfs files for node properties
	 */
	dev->attr_gpuid.name = "gpu_id";
	dev->attr_gpuid.mode = KFD_SYSFS_FILE_MODE;
	sysfs_attr_init(&dev->attr_gpuid);
	dev->attr_name.name = "name";
	dev->attr_name.mode = KFD_SYSFS_FILE_MODE;
	sysfs_attr_init(&dev->attr_name);
	dev->attr_props.name = "properties";
	dev->attr_props.mode = KFD_SYSFS_FILE_MODE;
	sysfs_attr_init(&dev->attr_props);
	ret = sysfs_create_file(dev->kobj_node, &dev->attr_gpuid);
	if (ret < 0)
		return ret;
	ret = sysfs_create_file(dev->kobj_node, &dev->attr_name);
	if (ret < 0)
		return ret;
	ret = sysfs_create_file(dev->kobj_node, &dev->attr_props);
	if (ret < 0)
		return ret;

	i = 0;
	list_for_each_entry(mem, &dev->mem_props, list) {
		mem->kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
		if (!mem->kobj)
			return -ENOMEM;
		ret = kobject_init_and_add(mem->kobj, &mem_type,
				dev->kobj_mem, "%d", i);
		if (ret < 0)
			return ret;

		mem->attr.name = "properties";
		mem->attr.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&mem->attr);
		ret = sysfs_create_file(mem->kobj, &mem->attr);
		if (ret < 0)
			return ret;
		i++;
	}

	i = 0;
	list_for_each_entry(cache, &dev->cache_props, list) {
		cache->kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
		if (!cache->kobj)
			return -ENOMEM;
		ret = kobject_init_and_add(cache->kobj, &cache_type,
				dev->kobj_cache, "%d", i);
		if (ret < 0)
			return ret;

		cache->attr.name = "properties";
		cache->attr.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&cache->attr);
		ret = sysfs_create_file(cache->kobj, &cache->attr);
		if (ret < 0)
			return ret;
		i++;
	}

	i = 0;
	list_for_each_entry(iolink, &dev->io_link_props, list) {
		iolink->kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
		if (!iolink->kobj)
			return -ENOMEM;
		ret = kobject_init_and_add(iolink->kobj, &iolink_type,
				dev->kobj_iolink, "%d", i);
		if (ret < 0)
			return ret;

		iolink->attr.name = "properties";
		iolink->attr.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&iolink->attr);
		ret = sysfs_create_file(iolink->kobj, &iolink->attr);
		if (ret < 0)
			return ret;
		i++;
}

	return 0;
}

static int kfd_build_sysfs_node_tree(void)
{
	struct kfd_topology_device *dev;
	int ret;
	uint32_t i = 0;

	list_for_each_entry(dev, &topology_device_list, list) {
		ret = kfd_build_sysfs_node_entry(dev, i);
		if (ret < 0)
			return ret;
		i++;
	}

	return 0;
}

static void kfd_remove_sysfs_node_tree(void)
{
	struct kfd_topology_device *dev;

	list_for_each_entry(dev, &topology_device_list, list)
		kfd_remove_sysfs_node_entry(dev);
}

static int kfd_topology_update_sysfs(void)
{
	int ret;

	pr_info("Creating topology SYSFS entries\n");
	if (sys_props.kobj_topology == NULL) {
		sys_props.kobj_topology =
				kfd_alloc_struct(sys_props.kobj_topology);
		if (!sys_props.kobj_topology)
			return -ENOMEM;

		ret = kobject_init_and_add(sys_props.kobj_topology,
				&sysprops_type,  &kfd_device->kobj,
				"topology");
		if (ret < 0)
			return ret;

		sys_props.kobj_nodes = kobject_create_and_add("nodes",
				sys_props.kobj_topology);
		if (!sys_props.kobj_nodes)
			return -ENOMEM;

		sys_props.attr_genid.name = "generation_id";
		sys_props.attr_genid.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&sys_props.attr_genid);
		ret = sysfs_create_file(sys_props.kobj_topology,
				&sys_props.attr_genid);
		if (ret < 0)
			return ret;

		sys_props.attr_props.name = "system_properties";
		sys_props.attr_props.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&sys_props.attr_props);
		ret = sysfs_create_file(sys_props.kobj_topology,
				&sys_props.attr_props);
		if (ret < 0)
			return ret;
	}

	kfd_remove_sysfs_node_tree();

	return kfd_build_sysfs_node_tree();
}

static void kfd_topology_release_sysfs(void)
{
	kfd_remove_sysfs_node_tree();
	if (sys_props.kobj_topology) {
		sysfs_remove_file(sys_props.kobj_topology,
				&sys_props.attr_genid);
		sysfs_remove_file(sys_props.kobj_topology,
				&sys_props.attr_props);
		if (sys_props.kobj_nodes) {
			kobject_del(sys_props.kobj_nodes);
			kobject_put(sys_props.kobj_nodes);
			sys_props.kobj_nodes = NULL;
		}
		kobject_del(sys_props.kobj_topology);
		kobject_put(sys_props.kobj_topology);
		sys_props.kobj_topology = NULL;
	}
}

int kfd_topology_init(void)
{
	void *crat_image = NULL;
	size_t image_size = 0;
	int ret;

	/*
	 * Initialize the head for the topology device list
	 */
	INIT_LIST_HEAD(&topology_device_list);
	init_rwsem(&topology_lock);
	topology_crat_parsed = 0;

	memset(&sys_props, 0, sizeof(sys_props));

	/*
	 * Get the CRAT image from the ACPI
	 */
	ret = kfd_topology_get_crat_acpi(crat_image, &image_size);
	if (ret == 0 && image_size > 0) {
		pr_info("Found CRAT image with size=%zd\n", image_size);
		crat_image = kmalloc(image_size, GFP_KERNEL);
		if (!crat_image) {
			ret = -ENOMEM;
			pr_err("No memory for allocating CRAT image\n");
			goto err;
		}
		ret = kfd_topology_get_crat_acpi(crat_image, &image_size);

		if (ret == 0) {
			down_write(&topology_lock);
			ret = kfd_parse_crat_table(crat_image);
			if (ret == 0)
				ret = kfd_topology_update_sysfs();
			up_write(&topology_lock);
		} else {
			pr_err("Couldn't get CRAT table size from ACPI\n");
		}
		kfree(crat_image);
	} else if (ret == -ENODATA) {
		ret = 0;
	} else {
		pr_err("Couldn't get CRAT table size from ACPI\n");
	}

err:
	pr_info("Finished initializing topology ret=%d\n", ret);
	return ret;
}

void kfd_topology_shutdown(void)
{
	kfd_topology_release_sysfs();
	kfd_release_live_view();
}

static void kfd_debug_print_topology(void)
{
	struct kfd_topology_device *dev;
	uint32_t i = 0;

	pr_info("DEBUG PRINT OF TOPOLOGY:");
	list_for_each_entry(dev, &topology_device_list, list) {
		pr_info("Node: %d\n", i);
		pr_info("\tGPU assigned: %s\n", (dev->gpu ? "yes" : "no"));
		pr_info("\tCPU count: %d\n", dev->node_props.cpu_cores_count);
		pr_info("\tSIMD count: %d", dev->node_props.simd_count);
		i++;
	}
}

static uint32_t kfd_generate_gpu_id(struct kfd_dev *gpu)
{
	uint32_t hashout;
	uint32_t buf[7];
	int i;

	if (!gpu)
		return 0;

	buf[0] = gpu->pdev->devfn;
	buf[1] = gpu->pdev->subsystem_vendor;
	buf[2] = gpu->pdev->subsystem_device;
	buf[3] = gpu->pdev->device;
	buf[4] = gpu->pdev->bus->number;
	buf[5] = (uint32_t)(gpu->kfd2kgd->get_vmem_size(gpu->kgd)
			& 0xffffffff);
	buf[6] = (uint32_t)(gpu->kfd2kgd->get_vmem_size(gpu->kgd) >> 32);

	for (i = 0, hashout = 0; i < 7; i++)
		hashout ^= hash_32(buf[i], KFD_GPU_ID_HASH_WIDTH);

	return hashout;
}

static struct kfd_topology_device *kfd_assign_gpu(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev;
	struct kfd_topology_device *out_dev = NULL;

	BUG_ON(!gpu);

	list_for_each_entry(dev, &topology_device_list, list)
		if (dev->gpu == NULL && dev->node_props.simd_count > 0) {
			dev->gpu = gpu;
			out_dev = dev;
			break;
		}

	return out_dev;
}

static void kfd_notify_gpu_change(uint32_t gpu_id, int arrival)
{
	/*
	 * TODO: Generate an event for thunk about the arrival/removal
	 * of the GPU
	 */
}

int kfd_topology_add_device(struct kfd_dev *gpu)
{
	uint32_t gpu_id;
	struct kfd_topology_device *dev;
	int res;

	BUG_ON(!gpu);

	gpu_id = kfd_generate_gpu_id(gpu);

	pr_debug("kfd: Adding new GPU (ID: 0x%x) to topology\n", gpu_id);

	down_write(&topology_lock);
	/*
	 * Try to assign the GPU to existing topology device (generated from
	 * CRAT table
	 */
	dev = kfd_assign_gpu(gpu);
	if (!dev) {
		pr_info("GPU was not found in the current topology. Extending.\n");
		kfd_debug_print_topology();
		dev = kfd_create_topology_device();
		if (!dev) {
			res = -ENOMEM;
			goto err;
		}
		dev->gpu = gpu;

		/*
		 * TODO: Make a call to retrieve topology information from the
		 * GPU vBIOS
		 */

		/*
		 * Update the SYSFS tree, since we added another topology device
		 */
		if (kfd_topology_update_sysfs() < 0)
			kfd_topology_release_sysfs();

	}

	dev->gpu_id = gpu_id;
	gpu->id = gpu_id;
	dev->node_props.vendor_id = gpu->pdev->vendor;
	dev->node_props.device_id = gpu->pdev->device;
	dev->node_props.location_id = (gpu->pdev->bus->number << 24) +
			(gpu->pdev->devfn & 0xffffff);
	/*
	 * TODO: Retrieve max engine clock values from KGD
	 */

	if (dev->gpu->device_info->asic_family == CHIP_CARRIZO) {
		dev->node_props.capability |= HSA_CAP_DOORBELL_PACKET_TYPE;
		pr_info("amdkfd: adding doorbell packet type capability\n");
	}

	res = 0;

err:
	up_write(&topology_lock);

	if (res == 0)
		kfd_notify_gpu_change(gpu_id, 1);

	return res;
}

int kfd_topology_remove_device(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev;
	uint32_t gpu_id;
	int res = -ENODEV;

	BUG_ON(!gpu);

	down_write(&topology_lock);

	list_for_each_entry(dev, &topology_device_list, list)
		if (dev->gpu == gpu) {
			gpu_id = dev->gpu_id;
			kfd_remove_sysfs_node_entry(dev);
			kfd_release_topology_device(dev);
			res = 0;
			if (kfd_topology_update_sysfs() < 0)
				kfd_topology_release_sysfs();
			break;
		}

	up_write(&topology_lock);

	if (res == 0)
		kfd_notify_gpu_change(gpu_id, 0);

	return res;
}

/*
 * When idx is out of bounds, the function will return NULL
 */
struct kfd_dev *kfd_topology_enum_kfd_devices(uint8_t idx)
{

	struct kfd_topology_device *top_dev;
	struct kfd_dev *device = NULL;
	uint8_t device_idx = 0;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list) {
		if (device_idx == idx) {
			device = top_dev->gpu;
			break;
		}

		device_idx++;
	}

	up_read(&topology_lock);

	return device;

}
