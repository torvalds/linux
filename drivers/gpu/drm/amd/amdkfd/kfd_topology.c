// SPDX-License-Identifier: GPL-2.0 OR MIT
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/hash.h>
#include <linux/cpufreq.h>
#include <linux/log2.h>
#include <linux/dmi.h>
#include <linux/atomic.h>

#include "kfd_priv.h"
#include "kfd_crat.h"
#include "kfd_topology.h"
#include "kfd_device_queue_manager.h"
#include "kfd_iommu.h"
#include "kfd_svm.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_ras.h"
#include "amdgpu.h"

/* topology_device_list - Master list of all topology devices */
static struct list_head topology_device_list;
static struct kfd_system_properties sys_props;

static DECLARE_RWSEM(topology_lock);
static uint32_t topology_crat_proximity_domain;

struct kfd_topology_device *kfd_topology_device_by_proximity_domain_no_lock(
						uint32_t proximity_domain)
{
	struct kfd_topology_device *top_dev;
	struct kfd_topology_device *device = NULL;

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->proximity_domain == proximity_domain) {
			device = top_dev;
			break;
		}

	return device;
}

struct kfd_topology_device *kfd_topology_device_by_proximity_domain(
						uint32_t proximity_domain)
{
	struct kfd_topology_device *device = NULL;

	down_read(&topology_lock);

	device = kfd_topology_device_by_proximity_domain_no_lock(
							proximity_domain);
	up_read(&topology_lock);

	return device;
}

struct kfd_topology_device *kfd_topology_device_by_id(uint32_t gpu_id)
{
	struct kfd_topology_device *top_dev = NULL;
	struct kfd_topology_device *ret = NULL;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->gpu_id == gpu_id) {
			ret = top_dev;
			break;
		}

	up_read(&topology_lock);

	return ret;
}

struct kfd_dev *kfd_device_by_id(uint32_t gpu_id)
{
	struct kfd_topology_device *top_dev;

	top_dev = kfd_topology_device_by_id(gpu_id);
	if (!top_dev)
		return NULL;

	return top_dev->gpu;
}

struct kfd_dev *kfd_device_by_pci_dev(const struct pci_dev *pdev)
{
	struct kfd_topology_device *top_dev;
	struct kfd_dev *device = NULL;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->gpu && top_dev->gpu->pdev == pdev) {
			device = top_dev->gpu;
			break;
		}

	up_read(&topology_lock);

	return device;
}

struct kfd_dev *kfd_device_by_adev(const struct amdgpu_device *adev)
{
	struct kfd_topology_device *top_dev;
	struct kfd_dev *device = NULL;

	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list)
		if (top_dev->gpu && top_dev->gpu->adev == adev) {
			device = top_dev->gpu;
			break;
		}

	up_read(&topology_lock);

	return device;
}

/* Called with write topology_lock acquired */
static void kfd_release_topology_device(struct kfd_topology_device *dev)
{
	struct kfd_mem_properties *mem;
	struct kfd_cache_properties *cache;
	struct kfd_iolink_properties *iolink;
	struct kfd_iolink_properties *p2plink;
	struct kfd_perf_properties *perf;

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

	while (dev->p2p_link_props.next != &dev->p2p_link_props) {
		p2plink = container_of(dev->p2p_link_props.next,
				struct kfd_iolink_properties, list);
		list_del(&p2plink->list);
		kfree(p2plink);
	}

	while (dev->perf_props.next != &dev->perf_props) {
		perf = container_of(dev->perf_props.next,
				struct kfd_perf_properties, list);
		list_del(&perf->list);
		kfree(perf);
	}

	kfree(dev);
}

void kfd_release_topology_device_list(struct list_head *device_list)
{
	struct kfd_topology_device *dev;

	while (!list_empty(device_list)) {
		dev = list_first_entry(device_list,
				       struct kfd_topology_device, list);
		kfd_release_topology_device(dev);
	}
}

static void kfd_release_live_view(void)
{
	kfd_release_topology_device_list(&topology_device_list);
	memset(&sys_props, 0, sizeof(sys_props));
}

struct kfd_topology_device *kfd_create_topology_device(
				struct list_head *device_list)
{
	struct kfd_topology_device *dev;

	dev = kfd_alloc_struct(dev);
	if (!dev) {
		pr_err("No memory to allocate a topology device");
		return NULL;
	}

	INIT_LIST_HEAD(&dev->mem_props);
	INIT_LIST_HEAD(&dev->cache_props);
	INIT_LIST_HEAD(&dev->io_link_props);
	INIT_LIST_HEAD(&dev->p2p_link_props);
	INIT_LIST_HEAD(&dev->perf_props);

	list_add_tail(&dev->list, device_list);

	return dev;
}


#define sysfs_show_gen_prop(buffer, offs, fmt, ...)		\
		(offs += snprintf(buffer+offs, PAGE_SIZE-offs,	\
				  fmt, __VA_ARGS__))
#define sysfs_show_32bit_prop(buffer, offs, name, value) \
		sysfs_show_gen_prop(buffer, offs, "%s %u\n", name, value)
#define sysfs_show_64bit_prop(buffer, offs, name, value) \
		sysfs_show_gen_prop(buffer, offs, "%s %llu\n", name, value)
#define sysfs_show_32bit_val(buffer, offs, value) \
		sysfs_show_gen_prop(buffer, offs, "%u\n", value)
#define sysfs_show_str_val(buffer, offs, value) \
		sysfs_show_gen_prop(buffer, offs, "%s\n", value)

static ssize_t sysprops_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	int offs = 0;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	if (attr == &sys_props.attr_genid) {
		sysfs_show_32bit_val(buffer, offs,
				     sys_props.generation_count);
	} else if (attr == &sys_props.attr_props) {
		sysfs_show_64bit_prop(buffer, offs, "platform_oem",
				      sys_props.platform_oem);
		sysfs_show_64bit_prop(buffer, offs, "platform_id",
				      sys_props.platform_id);
		sysfs_show_64bit_prop(buffer, offs, "platform_rev",
				      sys_props.platform_rev);
	} else {
		offs = -EINVAL;
	}

	return offs;
}

static void kfd_topology_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct sysfs_ops sysprops_ops = {
	.show = sysprops_show,
};

static struct kobj_type sysprops_type = {
	.release = kfd_topology_kobj_release,
	.sysfs_ops = &sysprops_ops,
};

static ssize_t iolink_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	int offs = 0;
	struct kfd_iolink_properties *iolink;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	iolink = container_of(attr, struct kfd_iolink_properties, attr);
	if (iolink->gpu && kfd_devcgroup_check_permission(iolink->gpu))
		return -EPERM;
	sysfs_show_32bit_prop(buffer, offs, "type", iolink->iolink_type);
	sysfs_show_32bit_prop(buffer, offs, "version_major", iolink->ver_maj);
	sysfs_show_32bit_prop(buffer, offs, "version_minor", iolink->ver_min);
	sysfs_show_32bit_prop(buffer, offs, "node_from", iolink->node_from);
	sysfs_show_32bit_prop(buffer, offs, "node_to", iolink->node_to);
	sysfs_show_32bit_prop(buffer, offs, "weight", iolink->weight);
	sysfs_show_32bit_prop(buffer, offs, "min_latency", iolink->min_latency);
	sysfs_show_32bit_prop(buffer, offs, "max_latency", iolink->max_latency);
	sysfs_show_32bit_prop(buffer, offs, "min_bandwidth",
			      iolink->min_bandwidth);
	sysfs_show_32bit_prop(buffer, offs, "max_bandwidth",
			      iolink->max_bandwidth);
	sysfs_show_32bit_prop(buffer, offs, "recommended_transfer_size",
			      iolink->rec_transfer_size);
	sysfs_show_32bit_prop(buffer, offs, "flags", iolink->flags);

	return offs;
}

static const struct sysfs_ops iolink_ops = {
	.show = iolink_show,
};

static struct kobj_type iolink_type = {
	.release = kfd_topology_kobj_release,
	.sysfs_ops = &iolink_ops,
};

static ssize_t mem_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	int offs = 0;
	struct kfd_mem_properties *mem;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	mem = container_of(attr, struct kfd_mem_properties, attr);
	if (mem->gpu && kfd_devcgroup_check_permission(mem->gpu))
		return -EPERM;
	sysfs_show_32bit_prop(buffer, offs, "heap_type", mem->heap_type);
	sysfs_show_64bit_prop(buffer, offs, "size_in_bytes",
			      mem->size_in_bytes);
	sysfs_show_32bit_prop(buffer, offs, "flags", mem->flags);
	sysfs_show_32bit_prop(buffer, offs, "width", mem->width);
	sysfs_show_32bit_prop(buffer, offs, "mem_clk_max",
			      mem->mem_clk_max);

	return offs;
}

static const struct sysfs_ops mem_ops = {
	.show = mem_show,
};

static struct kobj_type mem_type = {
	.release = kfd_topology_kobj_release,
	.sysfs_ops = &mem_ops,
};

static ssize_t kfd_cache_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	int offs = 0;
	uint32_t i, j;
	struct kfd_cache_properties *cache;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;
	cache = container_of(attr, struct kfd_cache_properties, attr);
	if (cache->gpu && kfd_devcgroup_check_permission(cache->gpu))
		return -EPERM;
	sysfs_show_32bit_prop(buffer, offs, "processor_id_low",
			cache->processor_id_low);
	sysfs_show_32bit_prop(buffer, offs, "level", cache->cache_level);
	sysfs_show_32bit_prop(buffer, offs, "size", cache->cache_size);
	sysfs_show_32bit_prop(buffer, offs, "cache_line_size",
			      cache->cacheline_size);
	sysfs_show_32bit_prop(buffer, offs, "cache_lines_per_tag",
			      cache->cachelines_per_tag);
	sysfs_show_32bit_prop(buffer, offs, "association", cache->cache_assoc);
	sysfs_show_32bit_prop(buffer, offs, "latency", cache->cache_latency);
	sysfs_show_32bit_prop(buffer, offs, "type", cache->cache_type);

	offs += snprintf(buffer+offs, PAGE_SIZE-offs, "sibling_map ");
	for (i = 0; i < cache->sibling_map_size; i++)
		for (j = 0; j < sizeof(cache->sibling_map[0])*8; j++)
			/* Check each bit */
			offs += snprintf(buffer+offs, PAGE_SIZE-offs, "%d,",
						(cache->sibling_map[i] >> j) & 1);

	/* Replace the last "," with end of line */
	buffer[offs-1] = '\n';
	return offs;
}

static const struct sysfs_ops cache_ops = {
	.show = kfd_cache_show,
};

static struct kobj_type cache_type = {
	.release = kfd_topology_kobj_release,
	.sysfs_ops = &cache_ops,
};

/****** Sysfs of Performance Counters ******/

struct kfd_perf_attr {
	struct kobj_attribute attr;
	uint32_t data;
};

static ssize_t perf_show(struct kobject *kobj, struct kobj_attribute *attrs,
			char *buf)
{
	int offs = 0;
	struct kfd_perf_attr *attr;

	buf[0] = 0;
	attr = container_of(attrs, struct kfd_perf_attr, attr);
	if (!attr->data) /* invalid data for PMC */
		return 0;
	else
		return sysfs_show_32bit_val(buf, offs, attr->data);
}

#define KFD_PERF_DESC(_name, _data)			\
{							\
	.attr  = __ATTR(_name, 0444, perf_show, NULL),	\
	.data = _data,					\
}

static struct kfd_perf_attr perf_attr_iommu[] = {
	KFD_PERF_DESC(max_concurrent, 0),
	KFD_PERF_DESC(num_counters, 0),
	KFD_PERF_DESC(counter_ids, 0),
};
/****************************************/

static ssize_t node_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	int offs = 0;
	struct kfd_topology_device *dev;
	uint32_t log_max_watch_addr;

	/* Making sure that the buffer is an empty string */
	buffer[0] = 0;

	if (strcmp(attr->name, "gpu_id") == 0) {
		dev = container_of(attr, struct kfd_topology_device,
				attr_gpuid);
		if (dev->gpu && kfd_devcgroup_check_permission(dev->gpu))
			return -EPERM;
		return sysfs_show_32bit_val(buffer, offs, dev->gpu_id);
	}

	if (strcmp(attr->name, "name") == 0) {
		dev = container_of(attr, struct kfd_topology_device,
				attr_name);

		if (dev->gpu && kfd_devcgroup_check_permission(dev->gpu))
			return -EPERM;
		return sysfs_show_str_val(buffer, offs, dev->node_props.name);
	}

	dev = container_of(attr, struct kfd_topology_device,
			attr_props);
	if (dev->gpu && kfd_devcgroup_check_permission(dev->gpu))
		return -EPERM;
	sysfs_show_32bit_prop(buffer, offs, "cpu_cores_count",
			      dev->node_props.cpu_cores_count);
	sysfs_show_32bit_prop(buffer, offs, "simd_count",
			      dev->gpu ? dev->node_props.simd_count : 0);
	sysfs_show_32bit_prop(buffer, offs, "mem_banks_count",
			      dev->node_props.mem_banks_count);
	sysfs_show_32bit_prop(buffer, offs, "caches_count",
			      dev->node_props.caches_count);
	sysfs_show_32bit_prop(buffer, offs, "io_links_count",
			      dev->node_props.io_links_count);
	sysfs_show_32bit_prop(buffer, offs, "p2p_links_count",
			      dev->node_props.p2p_links_count);
	sysfs_show_32bit_prop(buffer, offs, "cpu_core_id_base",
			      dev->node_props.cpu_core_id_base);
	sysfs_show_32bit_prop(buffer, offs, "simd_id_base",
			      dev->node_props.simd_id_base);
	sysfs_show_32bit_prop(buffer, offs, "max_waves_per_simd",
			      dev->node_props.max_waves_per_simd);
	sysfs_show_32bit_prop(buffer, offs, "lds_size_in_kb",
			      dev->node_props.lds_size_in_kb);
	sysfs_show_32bit_prop(buffer, offs, "gds_size_in_kb",
			      dev->node_props.gds_size_in_kb);
	sysfs_show_32bit_prop(buffer, offs, "num_gws",
			      dev->node_props.num_gws);
	sysfs_show_32bit_prop(buffer, offs, "wave_front_size",
			      dev->node_props.wave_front_size);
	sysfs_show_32bit_prop(buffer, offs, "array_count",
			      dev->node_props.array_count);
	sysfs_show_32bit_prop(buffer, offs, "simd_arrays_per_engine",
			      dev->node_props.simd_arrays_per_engine);
	sysfs_show_32bit_prop(buffer, offs, "cu_per_simd_array",
			      dev->node_props.cu_per_simd_array);
	sysfs_show_32bit_prop(buffer, offs, "simd_per_cu",
			      dev->node_props.simd_per_cu);
	sysfs_show_32bit_prop(buffer, offs, "max_slots_scratch_cu",
			      dev->node_props.max_slots_scratch_cu);
	sysfs_show_32bit_prop(buffer, offs, "gfx_target_version",
			      dev->node_props.gfx_target_version);
	sysfs_show_32bit_prop(buffer, offs, "vendor_id",
			      dev->node_props.vendor_id);
	sysfs_show_32bit_prop(buffer, offs, "device_id",
			      dev->node_props.device_id);
	sysfs_show_32bit_prop(buffer, offs, "location_id",
			      dev->node_props.location_id);
	sysfs_show_32bit_prop(buffer, offs, "domain",
			      dev->node_props.domain);
	sysfs_show_32bit_prop(buffer, offs, "drm_render_minor",
			      dev->node_props.drm_render_minor);
	sysfs_show_64bit_prop(buffer, offs, "hive_id",
			      dev->node_props.hive_id);
	sysfs_show_32bit_prop(buffer, offs, "num_sdma_engines",
			      dev->node_props.num_sdma_engines);
	sysfs_show_32bit_prop(buffer, offs, "num_sdma_xgmi_engines",
			      dev->node_props.num_sdma_xgmi_engines);
	sysfs_show_32bit_prop(buffer, offs, "num_sdma_queues_per_engine",
			      dev->node_props.num_sdma_queues_per_engine);
	sysfs_show_32bit_prop(buffer, offs, "num_cp_queues",
			      dev->node_props.num_cp_queues);

	if (dev->gpu) {
		log_max_watch_addr =
			__ilog2_u32(dev->gpu->device_info.num_of_watch_points);

		if (log_max_watch_addr) {
			dev->node_props.capability |=
					HSA_CAP_WATCH_POINTS_SUPPORTED;

			dev->node_props.capability |=
				((log_max_watch_addr <<
					HSA_CAP_WATCH_POINTS_TOTALBITS_SHIFT) &
				HSA_CAP_WATCH_POINTS_TOTALBITS_MASK);
		}

		if (dev->gpu->adev->asic_type == CHIP_TONGA)
			dev->node_props.capability |=
					HSA_CAP_AQL_QUEUE_DOUBLE_MAP;

		sysfs_show_32bit_prop(buffer, offs, "max_engine_clk_fcompute",
			dev->node_props.max_engine_clk_fcompute);

		sysfs_show_64bit_prop(buffer, offs, "local_mem_size", 0ULL);

		sysfs_show_32bit_prop(buffer, offs, "fw_version",
				      dev->gpu->mec_fw_version);
		sysfs_show_32bit_prop(buffer, offs, "capability",
				      dev->node_props.capability);
		sysfs_show_32bit_prop(buffer, offs, "sdma_fw_version",
				      dev->gpu->sdma_fw_version);
		sysfs_show_64bit_prop(buffer, offs, "unique_id",
				      dev->gpu->adev->unique_id);

	}

	return sysfs_show_32bit_prop(buffer, offs, "max_engine_clk_ccompute",
				     cpufreq_quick_get_max(0)/1000);
}

static const struct sysfs_ops node_ops = {
	.show = node_show,
};

static struct kobj_type node_type = {
	.release = kfd_topology_kobj_release,
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
	struct kfd_iolink_properties *p2plink;
	struct kfd_iolink_properties *iolink;
	struct kfd_cache_properties *cache;
	struct kfd_mem_properties *mem;
	struct kfd_perf_properties *perf;

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

	if (dev->kobj_p2plink) {
		list_for_each_entry(p2plink, &dev->p2p_link_props, list)
			if (p2plink->kobj) {
				kfd_remove_sysfs_file(p2plink->kobj,
							&p2plink->attr);
				p2plink->kobj = NULL;
			}
		kobject_del(dev->kobj_p2plink);
		kobject_put(dev->kobj_p2plink);
		dev->kobj_p2plink = NULL;
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

	if (dev->kobj_perf) {
		list_for_each_entry(perf, &dev->perf_props, list) {
			kfree(perf->attr_group);
			perf->attr_group = NULL;
		}
		kobject_del(dev->kobj_perf);
		kobject_put(dev->kobj_perf);
		dev->kobj_perf = NULL;
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
	struct kfd_iolink_properties *p2plink;
	struct kfd_iolink_properties *iolink;
	struct kfd_cache_properties *cache;
	struct kfd_mem_properties *mem;
	struct kfd_perf_properties *perf;
	int ret;
	uint32_t i, num_attrs;
	struct attribute **attrs;

	if (WARN_ON(dev->kobj_node))
		return -EEXIST;

	/*
	 * Creating the sysfs folders
	 */
	dev->kobj_node = kfd_alloc_struct(dev->kobj_node);
	if (!dev->kobj_node)
		return -ENOMEM;

	ret = kobject_init_and_add(dev->kobj_node, &node_type,
			sys_props.kobj_nodes, "%d", id);
	if (ret < 0) {
		kobject_put(dev->kobj_node);
		return ret;
	}

	dev->kobj_mem = kobject_create_and_add("mem_banks", dev->kobj_node);
	if (!dev->kobj_mem)
		return -ENOMEM;

	dev->kobj_cache = kobject_create_and_add("caches", dev->kobj_node);
	if (!dev->kobj_cache)
		return -ENOMEM;

	dev->kobj_iolink = kobject_create_and_add("io_links", dev->kobj_node);
	if (!dev->kobj_iolink)
		return -ENOMEM;

	dev->kobj_p2plink = kobject_create_and_add("p2p_links", dev->kobj_node);
	if (!dev->kobj_p2plink)
		return -ENOMEM;

	dev->kobj_perf = kobject_create_and_add("perf", dev->kobj_node);
	if (!dev->kobj_perf)
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
		if (ret < 0) {
			kobject_put(mem->kobj);
			return ret;
		}

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
		if (ret < 0) {
			kobject_put(cache->kobj);
			return ret;
		}

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
		if (ret < 0) {
			kobject_put(iolink->kobj);
			return ret;
		}

		iolink->attr.name = "properties";
		iolink->attr.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&iolink->attr);
		ret = sysfs_create_file(iolink->kobj, &iolink->attr);
		if (ret < 0)
			return ret;
		i++;
	}

	i = 0;
	list_for_each_entry(p2plink, &dev->p2p_link_props, list) {
		p2plink->kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
		if (!p2plink->kobj)
			return -ENOMEM;
		ret = kobject_init_and_add(p2plink->kobj, &iolink_type,
				dev->kobj_p2plink, "%d", i);
		if (ret < 0) {
			kobject_put(p2plink->kobj);
			return ret;
		}

		p2plink->attr.name = "properties";
		p2plink->attr.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&p2plink->attr);
		ret = sysfs_create_file(p2plink->kobj, &p2plink->attr);
		if (ret < 0)
			return ret;
		i++;
	}

	/* All hardware blocks have the same number of attributes. */
	num_attrs = ARRAY_SIZE(perf_attr_iommu);
	list_for_each_entry(perf, &dev->perf_props, list) {
		perf->attr_group = kzalloc(sizeof(struct kfd_perf_attr)
			* num_attrs + sizeof(struct attribute_group),
			GFP_KERNEL);
		if (!perf->attr_group)
			return -ENOMEM;

		attrs = (struct attribute **)(perf->attr_group + 1);
		if (!strcmp(perf->block_name, "iommu")) {
		/* Information of IOMMU's num_counters and counter_ids is shown
		 * under /sys/bus/event_source/devices/amd_iommu. We don't
		 * duplicate here.
		 */
			perf_attr_iommu[0].data = perf->max_concurrent;
			for (i = 0; i < num_attrs; i++)
				attrs[i] = &perf_attr_iommu[i].attr.attr;
		}
		perf->attr_group->name = perf->block_name;
		perf->attr_group->attrs = attrs;
		ret = sysfs_create_group(dev->kobj_perf, perf->attr_group);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* Called with write topology lock acquired */
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

/* Called with write topology lock acquired */
static void kfd_remove_sysfs_node_tree(void)
{
	struct kfd_topology_device *dev;

	list_for_each_entry(dev, &topology_device_list, list)
		kfd_remove_sysfs_node_entry(dev);
}

static int kfd_topology_update_sysfs(void)
{
	int ret;

	if (!sys_props.kobj_topology) {
		sys_props.kobj_topology =
				kfd_alloc_struct(sys_props.kobj_topology);
		if (!sys_props.kobj_topology)
			return -ENOMEM;

		ret = kobject_init_and_add(sys_props.kobj_topology,
				&sysprops_type,  &kfd_device->kobj,
				"topology");
		if (ret < 0) {
			kobject_put(sys_props.kobj_topology);
			return ret;
		}

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

/* Called with write topology_lock acquired */
static void kfd_topology_update_device_list(struct list_head *temp_list,
					struct list_head *master_list)
{
	while (!list_empty(temp_list)) {
		list_move_tail(temp_list->next, master_list);
		sys_props.num_devices++;
	}
}

static void kfd_debug_print_topology(void)
{
	struct kfd_topology_device *dev;

	down_read(&topology_lock);

	dev = list_last_entry(&topology_device_list,
			struct kfd_topology_device, list);
	if (dev) {
		if (dev->node_props.cpu_cores_count &&
				dev->node_props.simd_count) {
			pr_info("Topology: Add APU node [0x%0x:0x%0x]\n",
				dev->node_props.device_id,
				dev->node_props.vendor_id);
		} else if (dev->node_props.cpu_cores_count)
			pr_info("Topology: Add CPU node\n");
		else if (dev->node_props.simd_count)
			pr_info("Topology: Add dGPU node [0x%0x:0x%0x]\n",
				dev->node_props.device_id,
				dev->node_props.vendor_id);
	}
	up_read(&topology_lock);
}

/* Helper function for intializing platform_xx members of
 * kfd_system_properties. Uses OEM info from the last CPU/APU node.
 */
static void kfd_update_system_properties(void)
{
	struct kfd_topology_device *dev;

	down_read(&topology_lock);
	dev = list_last_entry(&topology_device_list,
			struct kfd_topology_device, list);
	if (dev) {
		sys_props.platform_id =
			(*((uint64_t *)dev->oem_id)) & CRAT_OEMID_64BIT_MASK;
		sys_props.platform_oem = *((uint64_t *)dev->oem_table_id);
		sys_props.platform_rev = dev->oem_revision;
	}
	up_read(&topology_lock);
}

static void find_system_memory(const struct dmi_header *dm,
	void *private)
{
	struct kfd_mem_properties *mem;
	u16 mem_width, mem_clock;
	struct kfd_topology_device *kdev =
		(struct kfd_topology_device *)private;
	const u8 *dmi_data = (const u8 *)(dm + 1);

	if (dm->type == DMI_ENTRY_MEM_DEVICE && dm->length >= 0x15) {
		mem_width = (u16)(*(const u16 *)(dmi_data + 0x6));
		mem_clock = (u16)(*(const u16 *)(dmi_data + 0x11));
		list_for_each_entry(mem, &kdev->mem_props, list) {
			if (mem_width != 0xFFFF && mem_width != 0)
				mem->width = mem_width;
			if (mem_clock != 0)
				mem->mem_clk_max = mem_clock;
		}
	}
}

/*
 * Performance counters information is not part of CRAT but we would like to
 * put them in the sysfs under topology directory for Thunk to get the data.
 * This function is called before updating the sysfs.
 */
static int kfd_add_perf_to_topology(struct kfd_topology_device *kdev)
{
	/* These are the only counters supported so far */
	return kfd_iommu_add_perf_counters(kdev);
}

/* kfd_add_non_crat_information - Add information that is not currently
 *	defined in CRAT but is necessary for KFD topology
 * @dev - topology device to which addition info is added
 */
static void kfd_add_non_crat_information(struct kfd_topology_device *kdev)
{
	/* Check if CPU only node. */
	if (!kdev->gpu) {
		/* Add system memory information */
		dmi_walk(find_system_memory, kdev);
	}
	/* TODO: For GPU node, rearrange code from kfd_topology_add_device */
}

/* kfd_is_acpi_crat_invalid - CRAT from ACPI is valid only for AMD APU devices.
 *	Ignore CRAT for all other devices. AMD APU is identified if both CPU
 *	and GPU cores are present.
 * @device_list - topology device list created by parsing ACPI CRAT table.
 * @return - TRUE if invalid, FALSE is valid.
 */
static bool kfd_is_acpi_crat_invalid(struct list_head *device_list)
{
	struct kfd_topology_device *dev;

	list_for_each_entry(dev, device_list, list) {
		if (dev->node_props.cpu_cores_count &&
			dev->node_props.simd_count)
			return false;
	}
	pr_info("Ignoring ACPI CRAT on non-APU system\n");
	return true;
}

int kfd_topology_init(void)
{
	void *crat_image = NULL;
	size_t image_size = 0;
	int ret;
	struct list_head temp_topology_device_list;
	int cpu_only_node = 0;
	struct kfd_topology_device *kdev;
	int proximity_domain;

	/* topology_device_list - Master list of all topology devices
	 * temp_topology_device_list - temporary list created while parsing CRAT
	 * or VCRAT. Once parsing is complete the contents of list is moved to
	 * topology_device_list
	 */

	/* Initialize the head for the both the lists */
	INIT_LIST_HEAD(&topology_device_list);
	INIT_LIST_HEAD(&temp_topology_device_list);
	init_rwsem(&topology_lock);

	memset(&sys_props, 0, sizeof(sys_props));

	/* Proximity domains in ACPI CRAT tables start counting at
	 * 0. The same should be true for virtual CRAT tables created
	 * at this stage. GPUs added later in kfd_topology_add_device
	 * use a counter.
	 */
	proximity_domain = 0;

	/*
	 * Get the CRAT image from the ACPI. If ACPI doesn't have one
	 * or if ACPI CRAT is invalid create a virtual CRAT.
	 * NOTE: The current implementation expects all AMD APUs to have
	 *	CRAT. If no CRAT is available, it is assumed to be a CPU
	 */
	ret = kfd_create_crat_image_acpi(&crat_image, &image_size);
	if (!ret) {
		ret = kfd_parse_crat_table(crat_image,
					   &temp_topology_device_list,
					   proximity_domain);
		if (ret ||
		    kfd_is_acpi_crat_invalid(&temp_topology_device_list)) {
			kfd_release_topology_device_list(
				&temp_topology_device_list);
			kfd_destroy_crat_image(crat_image);
			crat_image = NULL;
		}
	}

	if (!crat_image) {
		ret = kfd_create_crat_image_virtual(&crat_image, &image_size,
						    COMPUTE_UNIT_CPU, NULL,
						    proximity_domain);
		cpu_only_node = 1;
		if (ret) {
			pr_err("Error creating VCRAT table for CPU\n");
			return ret;
		}

		ret = kfd_parse_crat_table(crat_image,
					   &temp_topology_device_list,
					   proximity_domain);
		if (ret) {
			pr_err("Error parsing VCRAT table for CPU\n");
			goto err;
		}
	}

	kdev = list_first_entry(&temp_topology_device_list,
				struct kfd_topology_device, list);
	kfd_add_perf_to_topology(kdev);

	down_write(&topology_lock);
	kfd_topology_update_device_list(&temp_topology_device_list,
					&topology_device_list);
	topology_crat_proximity_domain = sys_props.num_devices-1;
	ret = kfd_topology_update_sysfs();
	up_write(&topology_lock);

	if (!ret) {
		sys_props.generation_count++;
		kfd_update_system_properties();
		kfd_debug_print_topology();
	} else
		pr_err("Failed to update topology in sysfs ret=%d\n", ret);

	/* For nodes with GPU, this information gets added
	 * when GPU is detected (kfd_topology_add_device).
	 */
	if (cpu_only_node) {
		/* Add additional information to CPU only node created above */
		down_write(&topology_lock);
		kdev = list_first_entry(&topology_device_list,
				struct kfd_topology_device, list);
		up_write(&topology_lock);
		kfd_add_non_crat_information(kdev);
	}

err:
	kfd_destroy_crat_image(crat_image);
	return ret;
}

void kfd_topology_shutdown(void)
{
	down_write(&topology_lock);
	kfd_topology_release_sysfs();
	kfd_release_live_view();
	up_write(&topology_lock);
}

static uint32_t kfd_generate_gpu_id(struct kfd_dev *gpu)
{
	uint32_t hashout;
	uint32_t buf[7];
	uint64_t local_mem_size;
	int i;

	if (!gpu)
		return 0;

	local_mem_size = gpu->local_mem_info.local_mem_size_private +
			gpu->local_mem_info.local_mem_size_public;

	buf[0] = gpu->pdev->devfn;
	buf[1] = gpu->pdev->subsystem_vendor |
		(gpu->pdev->subsystem_device << 16);
	buf[2] = pci_domain_nr(gpu->pdev->bus);
	buf[3] = gpu->pdev->device;
	buf[4] = gpu->pdev->bus->number;
	buf[5] = lower_32_bits(local_mem_size);
	buf[6] = upper_32_bits(local_mem_size);

	for (i = 0, hashout = 0; i < 7; i++)
		hashout ^= hash_32(buf[i], KFD_GPU_ID_HASH_WIDTH);

	return hashout;
}
/* kfd_assign_gpu - Attach @gpu to the correct kfd topology device. If
 *		the GPU device is not already present in the topology device
 *		list then return NULL. This means a new topology device has to
 *		be created for this GPU.
 */
static struct kfd_topology_device *kfd_assign_gpu(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev;
	struct kfd_topology_device *out_dev = NULL;
	struct kfd_mem_properties *mem;
	struct kfd_cache_properties *cache;
	struct kfd_iolink_properties *iolink;
	struct kfd_iolink_properties *p2plink;

	list_for_each_entry(dev, &topology_device_list, list) {
		/* Discrete GPUs need their own topology device list
		 * entries. Don't assign them to CPU/APU nodes.
		 */
		if (!gpu->use_iommu_v2 &&
		    dev->node_props.cpu_cores_count)
			continue;

		if (!dev->gpu && (dev->node_props.simd_count > 0)) {
			dev->gpu = gpu;
			out_dev = dev;

			list_for_each_entry(mem, &dev->mem_props, list)
				mem->gpu = dev->gpu;
			list_for_each_entry(cache, &dev->cache_props, list)
				cache->gpu = dev->gpu;
			list_for_each_entry(iolink, &dev->io_link_props, list)
				iolink->gpu = dev->gpu;
			list_for_each_entry(p2plink, &dev->p2p_link_props, list)
				p2plink->gpu = dev->gpu;
			break;
		}
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

/* kfd_fill_mem_clk_max_info - Since CRAT doesn't have memory clock info,
 *		patch this after CRAT parsing.
 */
static void kfd_fill_mem_clk_max_info(struct kfd_topology_device *dev)
{
	struct kfd_mem_properties *mem;
	struct kfd_local_mem_info local_mem_info;

	if (!dev)
		return;

	/* Currently, amdgpu driver (amdgpu_mc) deals only with GPUs with
	 * single bank of VRAM local memory.
	 * for dGPUs - VCRAT reports only one bank of Local Memory
	 * for APUs - If CRAT from ACPI reports more than one bank, then
	 *	all the banks will report the same mem_clk_max information
	 */
	amdgpu_amdkfd_get_local_mem_info(dev->gpu->adev, &local_mem_info);

	list_for_each_entry(mem, &dev->mem_props, list)
		mem->mem_clk_max = local_mem_info.mem_clk_max;
}

static void kfd_set_iolink_no_atomics(struct kfd_topology_device *dev,
					struct kfd_topology_device *target_gpu_dev,
					struct kfd_iolink_properties *link)
{
	/* xgmi always supports atomics between links. */
	if (link->iolink_type == CRAT_IOLINK_TYPE_XGMI)
		return;

	/* check pcie support to set cpu(dev) flags for target_gpu_dev link. */
	if (target_gpu_dev) {
		uint32_t cap;

		pcie_capability_read_dword(target_gpu_dev->gpu->pdev,
				PCI_EXP_DEVCAP2, &cap);

		if (!(cap & (PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
			     PCI_EXP_DEVCAP2_ATOMIC_COMP64)))
			link->flags |= CRAT_IOLINK_FLAGS_NO_ATOMICS_32_BIT |
				CRAT_IOLINK_FLAGS_NO_ATOMICS_64_BIT;
	/* set gpu (dev) flags. */
	} else {
		if (!dev->gpu->pci_atomic_requested ||
				dev->gpu->adev->asic_type == CHIP_HAWAII)
			link->flags |= CRAT_IOLINK_FLAGS_NO_ATOMICS_32_BIT |
				CRAT_IOLINK_FLAGS_NO_ATOMICS_64_BIT;
	}
}

static void kfd_set_iolink_non_coherent(struct kfd_topology_device *to_dev,
		struct kfd_iolink_properties *outbound_link,
		struct kfd_iolink_properties *inbound_link)
{
	/* CPU -> GPU with PCIe */
	if (!to_dev->gpu &&
	    inbound_link->iolink_type == CRAT_IOLINK_TYPE_PCIEXPRESS)
		inbound_link->flags |= CRAT_IOLINK_FLAGS_NON_COHERENT;

	if (to_dev->gpu) {
		/* GPU <-> GPU with PCIe and
		 * Vega20 with XGMI
		 */
		if (inbound_link->iolink_type == CRAT_IOLINK_TYPE_PCIEXPRESS ||
		    (inbound_link->iolink_type == CRAT_IOLINK_TYPE_XGMI &&
		    KFD_GC_VERSION(to_dev->gpu) == IP_VERSION(9, 4, 0))) {
			outbound_link->flags |= CRAT_IOLINK_FLAGS_NON_COHERENT;
			inbound_link->flags |= CRAT_IOLINK_FLAGS_NON_COHERENT;
		}
	}
}

static void kfd_fill_iolink_non_crat_info(struct kfd_topology_device *dev)
{
	struct kfd_iolink_properties *link, *inbound_link;
	struct kfd_topology_device *peer_dev;

	if (!dev || !dev->gpu)
		return;

	/* GPU only creates direct links so apply flags setting to all */
	list_for_each_entry(link, &dev->io_link_props, list) {
		link->flags = CRAT_IOLINK_FLAGS_ENABLED;
		kfd_set_iolink_no_atomics(dev, NULL, link);
		peer_dev = kfd_topology_device_by_proximity_domain(
				link->node_to);

		if (!peer_dev)
			continue;

		/* Include the CPU peer in GPU hive if connected over xGMI. */
		if (!peer_dev->gpu && !peer_dev->node_props.hive_id &&
				dev->node_props.hive_id &&
				dev->gpu->adev->gmc.xgmi.connected_to_cpu)
			peer_dev->node_props.hive_id = dev->node_props.hive_id;

		list_for_each_entry(inbound_link, &peer_dev->io_link_props,
									list) {
			if (inbound_link->node_to != link->node_from)
				continue;

			inbound_link->flags = CRAT_IOLINK_FLAGS_ENABLED;
			kfd_set_iolink_no_atomics(peer_dev, dev, inbound_link);
			kfd_set_iolink_non_coherent(peer_dev, link, inbound_link);
		}
	}

	/* Create indirect links so apply flags setting to all */
	list_for_each_entry(link, &dev->p2p_link_props, list) {
		link->flags = CRAT_IOLINK_FLAGS_ENABLED;
		kfd_set_iolink_no_atomics(dev, NULL, link);
		peer_dev = kfd_topology_device_by_proximity_domain(
				link->node_to);

		if (!peer_dev)
			continue;

		list_for_each_entry(inbound_link, &peer_dev->p2p_link_props,
									list) {
			if (inbound_link->node_to != link->node_from)
				continue;

			inbound_link->flags = CRAT_IOLINK_FLAGS_ENABLED;
			kfd_set_iolink_no_atomics(peer_dev, dev, inbound_link);
			kfd_set_iolink_non_coherent(peer_dev, link, inbound_link);
		}
	}
}

static int kfd_build_p2p_node_entry(struct kfd_topology_device *dev,
				struct kfd_iolink_properties *p2plink)
{
	int ret;

	p2plink->kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!p2plink->kobj)
		return -ENOMEM;

	ret = kobject_init_and_add(p2plink->kobj, &iolink_type,
			dev->kobj_p2plink, "%d", dev->node_props.p2p_links_count - 1);
	if (ret < 0) {
		kobject_put(p2plink->kobj);
		return ret;
	}

	p2plink->attr.name = "properties";
	p2plink->attr.mode = KFD_SYSFS_FILE_MODE;
	sysfs_attr_init(&p2plink->attr);
	ret = sysfs_create_file(p2plink->kobj, &p2plink->attr);
	if (ret < 0)
		return ret;

	return 0;
}

static int kfd_create_indirect_link_prop(struct kfd_topology_device *kdev, int gpu_node)
{
	struct kfd_iolink_properties *gpu_link, *tmp_link, *cpu_link;
	struct kfd_iolink_properties *props = NULL, *props2 = NULL;
	struct kfd_topology_device *cpu_dev;
	int ret = 0;
	int i, num_cpu;

	num_cpu = 0;
	list_for_each_entry(cpu_dev, &topology_device_list, list) {
		if (cpu_dev->gpu)
			break;
		num_cpu++;
	}

	if (list_empty(&kdev->io_link_props))
		return -ENODATA;

	gpu_link = list_first_entry(&kdev->io_link_props,
				    struct kfd_iolink_properties, list);

	for (i = 0; i < num_cpu; i++) {
		/* CPU <--> GPU */
		if (gpu_link->node_to == i)
			continue;

		/* find CPU <-->  CPU links */
		cpu_link = NULL;
		cpu_dev = kfd_topology_device_by_proximity_domain(i);
		if (cpu_dev) {
			list_for_each_entry(tmp_link,
					&cpu_dev->io_link_props, list) {
				if (tmp_link->node_to == gpu_link->node_to) {
					cpu_link = tmp_link;
					break;
				}
			}
		}

		if (!cpu_link)
			return -ENOMEM;

		/* CPU <--> CPU <--> GPU, GPU node*/
		props = kfd_alloc_struct(props);
		if (!props)
			return -ENOMEM;

		memcpy(props, gpu_link, sizeof(struct kfd_iolink_properties));
		props->weight = gpu_link->weight + cpu_link->weight;
		props->min_latency = gpu_link->min_latency + cpu_link->min_latency;
		props->max_latency = gpu_link->max_latency + cpu_link->max_latency;
		props->min_bandwidth = min(gpu_link->min_bandwidth, cpu_link->min_bandwidth);
		props->max_bandwidth = min(gpu_link->max_bandwidth, cpu_link->max_bandwidth);

		props->node_from = gpu_node;
		props->node_to = i;
		kdev->node_props.p2p_links_count++;
		list_add_tail(&props->list, &kdev->p2p_link_props);
		ret = kfd_build_p2p_node_entry(kdev, props);
		if (ret < 0)
			return ret;

		/* for small Bar, no CPU --> GPU in-direct links */
		if (kfd_dev_is_large_bar(kdev->gpu)) {
			/* CPU <--> CPU <--> GPU, CPU node*/
			props2 = kfd_alloc_struct(props2);
			if (!props2)
				return -ENOMEM;

			memcpy(props2, props, sizeof(struct kfd_iolink_properties));
			props2->node_from = i;
			props2->node_to = gpu_node;
			props2->kobj = NULL;
			cpu_dev->node_props.p2p_links_count++;
			list_add_tail(&props2->list, &cpu_dev->p2p_link_props);
			ret = kfd_build_p2p_node_entry(cpu_dev, props2);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

#if defined(CONFIG_HSA_AMD_P2P)
static int kfd_add_peer_prop(struct kfd_topology_device *kdev,
		struct kfd_topology_device *peer, int from, int to)
{
	struct kfd_iolink_properties *props = NULL;
	struct kfd_iolink_properties *iolink1, *iolink2, *iolink3;
	struct kfd_topology_device *cpu_dev;
	int ret = 0;

	if (!amdgpu_device_is_peer_accessible(
				kdev->gpu->adev,
				peer->gpu->adev))
		return ret;

	if (list_empty(&kdev->io_link_props))
		return -ENODATA;

	iolink1 = list_first_entry(&kdev->io_link_props,
				   struct kfd_iolink_properties, list);

	if (list_empty(&peer->io_link_props))
		return -ENODATA;

	iolink2 = list_first_entry(&peer->io_link_props,
				   struct kfd_iolink_properties, list);

	props = kfd_alloc_struct(props);
	if (!props)
		return -ENOMEM;

	memcpy(props, iolink1, sizeof(struct kfd_iolink_properties));

	props->weight = iolink1->weight + iolink2->weight;
	props->min_latency = iolink1->min_latency + iolink2->min_latency;
	props->max_latency = iolink1->max_latency + iolink2->max_latency;
	props->min_bandwidth = min(iolink1->min_bandwidth, iolink2->min_bandwidth);
	props->max_bandwidth = min(iolink2->max_bandwidth, iolink2->max_bandwidth);

	if (iolink1->node_to != iolink2->node_to) {
		/* CPU->CPU  link*/
		cpu_dev = kfd_topology_device_by_proximity_domain(iolink1->node_to);
		if (cpu_dev) {
			list_for_each_entry(iolink3, &cpu_dev->io_link_props, list)
				if (iolink3->node_to == iolink2->node_to)
					break;

			props->weight += iolink3->weight;
			props->min_latency += iolink3->min_latency;
			props->max_latency += iolink3->max_latency;
			props->min_bandwidth = min(props->min_bandwidth,
							iolink3->min_bandwidth);
			props->max_bandwidth = min(props->max_bandwidth,
							iolink3->max_bandwidth);
		} else {
			WARN(1, "CPU node not found");
		}
	}

	props->node_from = from;
	props->node_to = to;
	peer->node_props.p2p_links_count++;
	list_add_tail(&props->list, &peer->p2p_link_props);
	ret = kfd_build_p2p_node_entry(peer, props);

	return ret;
}
#endif

static int kfd_dev_create_p2p_links(void)
{
	struct kfd_topology_device *dev;
	struct kfd_topology_device *new_dev;
#if defined(CONFIG_HSA_AMD_P2P)
	uint32_t i;
#endif
	uint32_t k;
	int ret = 0;

	k = 0;
	list_for_each_entry(dev, &topology_device_list, list)
		k++;
	if (k < 2)
		return 0;

	new_dev = list_last_entry(&topology_device_list, struct kfd_topology_device, list);
	if (WARN_ON(!new_dev->gpu))
		return 0;

	k--;

	/* create in-direct links */
	ret = kfd_create_indirect_link_prop(new_dev, k);
	if (ret < 0)
		goto out;

	/* create p2p links */
#if defined(CONFIG_HSA_AMD_P2P)
	i = 0;
	list_for_each_entry(dev, &topology_device_list, list) {
		if (dev == new_dev)
			break;
		if (!dev->gpu || !dev->gpu->adev ||
		    (dev->gpu->hive_id &&
		     dev->gpu->hive_id == new_dev->gpu->hive_id))
			goto next;

		/* check if node(s) is/are peer accessible in one direction or bi-direction */
		ret = kfd_add_peer_prop(new_dev, dev, i, k);
		if (ret < 0)
			goto out;

		ret = kfd_add_peer_prop(dev, new_dev, k, i);
		if (ret < 0)
			goto out;
next:
		i++;
	}
#endif

out:
	return ret;
}


/* Helper function. See kfd_fill_gpu_cache_info for parameter description */
static int fill_in_l1_pcache(struct kfd_cache_properties **props_ext,
				struct kfd_gpu_cache_info *pcache_info,
				struct kfd_cu_info *cu_info,
				int cu_bitmask,
				int cache_type, unsigned int cu_processor_id,
				int cu_block)
{
	unsigned int cu_sibling_map_mask;
	int first_active_cu;
	struct kfd_cache_properties *pcache = NULL;

	cu_sibling_map_mask = cu_bitmask;
	cu_sibling_map_mask >>= cu_block;
	cu_sibling_map_mask &= ((1 << pcache_info[cache_type].num_cu_shared) - 1);
	first_active_cu = ffs(cu_sibling_map_mask);

	/* CU could be inactive. In case of shared cache find the first active
	 * CU. and incase of non-shared cache check if the CU is inactive. If
	 * inactive active skip it
	 */
	if (first_active_cu) {
		pcache = kfd_alloc_struct(pcache);
		if (!pcache)
			return -ENOMEM;

		memset(pcache, 0, sizeof(struct kfd_cache_properties));
		pcache->processor_id_low = cu_processor_id + (first_active_cu - 1);
		pcache->cache_level = pcache_info[cache_type].cache_level;
		pcache->cache_size = pcache_info[cache_type].cache_size;

		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_DATA_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_DATA;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_INST_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_INSTRUCTION;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_CPU_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_CPU;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_SIMD_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_HSACU;

		/* Sibling map is w.r.t processor_id_low, so shift out
		 * inactive CU
		 */
		cu_sibling_map_mask =
			cu_sibling_map_mask >> (first_active_cu - 1);

		pcache->sibling_map[0] = (uint8_t)(cu_sibling_map_mask & 0xFF);
		pcache->sibling_map[1] =
				(uint8_t)((cu_sibling_map_mask >> 8) & 0xFF);
		pcache->sibling_map[2] =
				(uint8_t)((cu_sibling_map_mask >> 16) & 0xFF);
		pcache->sibling_map[3] =
				(uint8_t)((cu_sibling_map_mask >> 24) & 0xFF);

		pcache->sibling_map_size = 4;
		*props_ext = pcache;

		return 0;
	}
	return 1;
}

/* Helper function. See kfd_fill_gpu_cache_info for parameter description */
static int fill_in_l2_l3_pcache(struct kfd_cache_properties **props_ext,
				struct kfd_gpu_cache_info *pcache_info,
				struct kfd_cu_info *cu_info,
				int cache_type, unsigned int cu_processor_id)
{
	unsigned int cu_sibling_map_mask;
	int first_active_cu;
	int i, j, k;
	struct kfd_cache_properties *pcache = NULL;

	cu_sibling_map_mask = cu_info->cu_bitmap[0][0];
	cu_sibling_map_mask &=
		((1 << pcache_info[cache_type].num_cu_shared) - 1);
	first_active_cu = ffs(cu_sibling_map_mask);

	/* CU could be inactive. In case of shared cache find the first active
	 * CU. and incase of non-shared cache check if the CU is inactive. If
	 * inactive active skip it
	 */
	if (first_active_cu) {
		pcache = kfd_alloc_struct(pcache);
		if (!pcache)
			return -ENOMEM;

		memset(pcache, 0, sizeof(struct kfd_cache_properties));
		pcache->processor_id_low = cu_processor_id
					+ (first_active_cu - 1);
		pcache->cache_level = pcache_info[cache_type].cache_level;
		pcache->cache_size = pcache_info[cache_type].cache_size;

		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_DATA_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_DATA;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_INST_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_INSTRUCTION;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_CPU_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_CPU;
		if (pcache_info[cache_type].flags & CRAT_CACHE_FLAGS_SIMD_CACHE)
			pcache->cache_type |= HSA_CACHE_TYPE_HSACU;

		/* Sibling map is w.r.t processor_id_low, so shift out
		 * inactive CU
		 */
		cu_sibling_map_mask = cu_sibling_map_mask >> (first_active_cu - 1);
		k = 0;

		for (i = 0; i < cu_info->num_shader_engines; i++) {
			for (j = 0; j < cu_info->num_shader_arrays_per_engine; j++) {
				pcache->sibling_map[k] = (uint8_t)(cu_sibling_map_mask & 0xFF);
				pcache->sibling_map[k+1] = (uint8_t)((cu_sibling_map_mask >> 8) & 0xFF);
				pcache->sibling_map[k+2] = (uint8_t)((cu_sibling_map_mask >> 16) & 0xFF);
				pcache->sibling_map[k+3] = (uint8_t)((cu_sibling_map_mask >> 24) & 0xFF);
				k += 4;

				cu_sibling_map_mask = cu_info->cu_bitmap[i % 4][j + i / 4];
				cu_sibling_map_mask &= ((1 << pcache_info[cache_type].num_cu_shared) - 1);
			}
		}
		pcache->sibling_map_size = k;
		*props_ext = pcache;
		return 0;
	}
	return 1;
}

#define KFD_MAX_CACHE_TYPES 6

/* kfd_fill_cache_non_crat_info - Fill GPU cache info using kfd_gpu_cache_info
 * tables
 */
void kfd_fill_cache_non_crat_info(struct kfd_topology_device *dev, struct kfd_dev *kdev)
{
	struct kfd_gpu_cache_info *pcache_info = NULL;
	int i, j, k;
	int ct = 0;
	unsigned int cu_processor_id;
	int ret;
	unsigned int num_cu_shared;
	struct kfd_cu_info cu_info;
	struct kfd_cu_info *pcu_info;
	int gpu_processor_id;
	struct kfd_cache_properties *props_ext;
	int num_of_entries = 0;
	int num_of_cache_types = 0;
	struct kfd_gpu_cache_info cache_info[KFD_MAX_CACHE_TYPES];

	amdgpu_amdkfd_get_cu_info(kdev->adev, &cu_info);
	pcu_info = &cu_info;

	gpu_processor_id = dev->node_props.simd_id_base;

	pcache_info = cache_info;
	num_of_cache_types = kfd_get_gpu_cache_info(kdev, &pcache_info);
	if (!num_of_cache_types) {
		pr_warn("no cache info found\n");
		return;
	}

	/* For each type of cache listed in the kfd_gpu_cache_info table,
	 * go through all available Compute Units.
	 * The [i,j,k] loop will
	 *		if kfd_gpu_cache_info.num_cu_shared = 1
	 *			will parse through all available CU
	 *		If (kfd_gpu_cache_info.num_cu_shared != 1)
	 *			then it will consider only one CU from
	 *			the shared unit
	 */
	for (ct = 0; ct < num_of_cache_types; ct++) {
		cu_processor_id = gpu_processor_id;
		if (pcache_info[ct].cache_level == 1) {
			for (i = 0; i < pcu_info->num_shader_engines; i++) {
				for (j = 0; j < pcu_info->num_shader_arrays_per_engine; j++) {
					for (k = 0; k < pcu_info->num_cu_per_sh; k += pcache_info[ct].num_cu_shared) {

						ret = fill_in_l1_pcache(&props_ext, pcache_info, pcu_info,
										pcu_info->cu_bitmap[i % 4][j + i / 4], ct,
										cu_processor_id, k);

						if (ret < 0)
							break;

						if (!ret) {
							num_of_entries++;
							list_add_tail(&props_ext->list, &dev->cache_props);
						}

						/* Move to next CU block */
						num_cu_shared = ((k + pcache_info[ct].num_cu_shared) <=
							pcu_info->num_cu_per_sh) ?
							pcache_info[ct].num_cu_shared :
							(pcu_info->num_cu_per_sh - k);
						cu_processor_id += num_cu_shared;
					}
				}
			}
		} else {
			ret = fill_in_l2_l3_pcache(&props_ext, pcache_info,
								pcu_info, ct, cu_processor_id);

			if (ret < 0)
				break;

			if (!ret) {
				num_of_entries++;
				list_add_tail(&props_ext->list, &dev->cache_props);
			}
		}
	}
	dev->node_props.caches_count += num_of_entries;
	pr_debug("Added [%d] GPU cache entries\n", num_of_entries);
}

int kfd_topology_add_device(struct kfd_dev *gpu)
{
	uint32_t gpu_id;
	struct kfd_topology_device *dev;
	struct kfd_cu_info cu_info;
	int res = 0;
	struct list_head temp_topology_device_list;
	void *crat_image = NULL;
	size_t image_size = 0;
	int proximity_domain;
	int i;
	const char *asic_name = amdgpu_asic_name[gpu->adev->asic_type];

	INIT_LIST_HEAD(&temp_topology_device_list);

	gpu_id = kfd_generate_gpu_id(gpu);
	pr_debug("Adding new GPU (ID: 0x%x) to topology\n", gpu_id);

	/* Check to see if this gpu device exists in the topology_device_list.
	 * If so, assign the gpu to that device,
	 * else create a Virtual CRAT for this gpu device and then parse that
	 * CRAT to create a new topology device. Once created assign the gpu to
	 * that topology device
	 */
	down_write(&topology_lock);
	dev = kfd_assign_gpu(gpu);
	if (!dev) {
		proximity_domain = ++topology_crat_proximity_domain;

		res = kfd_create_crat_image_virtual(&crat_image, &image_size,
						    COMPUTE_UNIT_GPU, gpu,
						    proximity_domain);
		if (res) {
			pr_err("Error creating VCRAT for GPU (ID: 0x%x)\n",
			       gpu_id);
			topology_crat_proximity_domain--;
			return res;
		}

		res = kfd_parse_crat_table(crat_image,
					   &temp_topology_device_list,
					   proximity_domain);
		if (res) {
			pr_err("Error parsing VCRAT for GPU (ID: 0x%x)\n",
			       gpu_id);
			topology_crat_proximity_domain--;
			goto err;
		}

		kfd_topology_update_device_list(&temp_topology_device_list,
			&topology_device_list);

		dev = kfd_assign_gpu(gpu);
		if (WARN_ON(!dev)) {
			res = -ENODEV;
			goto err;
		}

		/* Fill the cache affinity information here for the GPUs
		 * using VCRAT
		 */
		kfd_fill_cache_non_crat_info(dev, gpu);

		/* Update the SYSFS tree, since we added another topology
		 * device
		 */
		res = kfd_topology_update_sysfs();
		if (!res)
			sys_props.generation_count++;
		else
			pr_err("Failed to update GPU (ID: 0x%x) to sysfs topology. res=%d\n",
						gpu_id, res);
	}
	up_write(&topology_lock);

	dev->gpu_id = gpu_id;
	gpu->id = gpu_id;

	kfd_dev_create_p2p_links();

	/* TODO: Move the following lines to function
	 *	kfd_add_non_crat_information
	 */

	/* Fill-in additional information that is not available in CRAT but
	 * needed for the topology
	 */

	amdgpu_amdkfd_get_cu_info(dev->gpu->adev, &cu_info);

	for (i = 0; i < KFD_TOPOLOGY_PUBLIC_NAME_SIZE-1; i++) {
		dev->node_props.name[i] = __tolower(asic_name[i]);
		if (asic_name[i] == '\0')
			break;
	}
	dev->node_props.name[i] = '\0';

	dev->node_props.simd_arrays_per_engine =
		cu_info.num_shader_arrays_per_engine;

	dev->node_props.gfx_target_version = gpu->device_info.gfx_target_version;
	dev->node_props.vendor_id = gpu->pdev->vendor;
	dev->node_props.device_id = gpu->pdev->device;
	dev->node_props.capability |=
		((dev->gpu->adev->rev_id << HSA_CAP_ASIC_REVISION_SHIFT) &
			HSA_CAP_ASIC_REVISION_MASK);
	dev->node_props.location_id = pci_dev_id(gpu->pdev);
	dev->node_props.domain = pci_domain_nr(gpu->pdev->bus);
	dev->node_props.max_engine_clk_fcompute =
		amdgpu_amdkfd_get_max_engine_clock_in_mhz(dev->gpu->adev);
	dev->node_props.max_engine_clk_ccompute =
		cpufreq_quick_get_max(0) / 1000;
	dev->node_props.drm_render_minor =
		gpu->shared_resources.drm_render_minor;

	dev->node_props.hive_id = gpu->hive_id;
	dev->node_props.num_sdma_engines = kfd_get_num_sdma_engines(gpu);
	dev->node_props.num_sdma_xgmi_engines =
					kfd_get_num_xgmi_sdma_engines(gpu);
	dev->node_props.num_sdma_queues_per_engine =
				gpu->device_info.num_sdma_queues_per_engine -
				gpu->device_info.num_reserved_sdma_queues_per_engine;
	dev->node_props.num_gws = (dev->gpu->gws &&
		dev->gpu->dqm->sched_policy != KFD_SCHED_POLICY_NO_HWS) ?
		dev->gpu->adev->gds.gws_size : 0;
	dev->node_props.num_cp_queues = get_cp_queues_num(dev->gpu->dqm);

	kfd_fill_mem_clk_max_info(dev);
	kfd_fill_iolink_non_crat_info(dev);

	switch (dev->gpu->adev->asic_type) {
	case CHIP_KAVERI:
	case CHIP_HAWAII:
	case CHIP_TONGA:
		dev->node_props.capability |= ((HSA_CAP_DOORBELL_TYPE_PRE_1_0 <<
			HSA_CAP_DOORBELL_TYPE_TOTALBITS_SHIFT) &
			HSA_CAP_DOORBELL_TYPE_TOTALBITS_MASK);
		break;
	case CHIP_CARRIZO:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		pr_debug("Adding doorbell packet type capability\n");
		dev->node_props.capability |= ((HSA_CAP_DOORBELL_TYPE_1_0 <<
			HSA_CAP_DOORBELL_TYPE_TOTALBITS_SHIFT) &
			HSA_CAP_DOORBELL_TYPE_TOTALBITS_MASK);
		break;
	default:
		if (KFD_GC_VERSION(dev->gpu) >= IP_VERSION(9, 0, 1))
			dev->node_props.capability |= ((HSA_CAP_DOORBELL_TYPE_2_0 <<
				HSA_CAP_DOORBELL_TYPE_TOTALBITS_SHIFT) &
				HSA_CAP_DOORBELL_TYPE_TOTALBITS_MASK);
		else
			WARN(1, "Unexpected ASIC family %u",
			     dev->gpu->adev->asic_type);
	}

	/*
	 * Overwrite ATS capability according to needs_iommu_device to fix
	 * potential missing corresponding bit in CRAT of BIOS.
	 */
	if (dev->gpu->use_iommu_v2)
		dev->node_props.capability |= HSA_CAP_ATS_PRESENT;
	else
		dev->node_props.capability &= ~HSA_CAP_ATS_PRESENT;

	/* Fix errors in CZ CRAT.
	 * simd_count: Carrizo CRAT reports wrong simd_count, probably
	 *		because it doesn't consider masked out CUs
	 * max_waves_per_simd: Carrizo reports wrong max_waves_per_simd
	 */
	if (dev->gpu->adev->asic_type == CHIP_CARRIZO) {
		dev->node_props.simd_count =
			cu_info.simd_per_cu * cu_info.cu_active_number;
		dev->node_props.max_waves_per_simd = 10;
	}

	/* kfd only concerns sram ecc on GFX and HBM ecc on UMC */
	dev->node_props.capability |=
		((dev->gpu->adev->ras_enabled & BIT(AMDGPU_RAS_BLOCK__GFX)) != 0) ?
		HSA_CAP_SRAM_EDCSUPPORTED : 0;
	dev->node_props.capability |=
		((dev->gpu->adev->ras_enabled & BIT(AMDGPU_RAS_BLOCK__UMC)) != 0) ?
		HSA_CAP_MEM_EDCSUPPORTED : 0;

	if (KFD_GC_VERSION(dev->gpu) != IP_VERSION(9, 0, 1))
		dev->node_props.capability |= (dev->gpu->adev->ras_enabled != 0) ?
			HSA_CAP_RASEVENTNOTIFY : 0;

	if (KFD_IS_SVM_API_SUPPORTED(dev->gpu->adev->kfd.dev))
		dev->node_props.capability |= HSA_CAP_SVMAPI_SUPPORTED;

	kfd_debug_print_topology();

	if (!res)
		kfd_notify_gpu_change(gpu_id, 1);
err:
	kfd_destroy_crat_image(crat_image);
	return res;
}

/**
 * kfd_topology_update_io_links() - Update IO links after device removal.
 * @proximity_domain: Proximity domain value of the dev being removed.
 *
 * The topology list currently is arranged in increasing order of
 * proximity domain.
 *
 * Two things need to be done when a device is removed:
 * 1. All the IO links to this device need to be removed.
 * 2. All nodes after the current device node need to move
 *    up once this device node is removed from the topology
 *    list. As a result, the proximity domain values for
 *    all nodes after the node being deleted reduce by 1.
 *    This would also cause the proximity domain values for
 *    io links to be updated based on new proximity domain
 *    values.
 *
 * Context: The caller must hold write topology_lock.
 */
static void kfd_topology_update_io_links(int proximity_domain)
{
	struct kfd_topology_device *dev;
	struct kfd_iolink_properties *iolink, *p2plink, *tmp;

	list_for_each_entry(dev, &topology_device_list, list) {
		if (dev->proximity_domain > proximity_domain)
			dev->proximity_domain--;

		list_for_each_entry_safe(iolink, tmp, &dev->io_link_props, list) {
			/*
			 * If there is an io link to the dev being deleted
			 * then remove that IO link also.
			 */
			if (iolink->node_to == proximity_domain) {
				list_del(&iolink->list);
				dev->node_props.io_links_count--;
			} else {
				if (iolink->node_from > proximity_domain)
					iolink->node_from--;
				if (iolink->node_to > proximity_domain)
					iolink->node_to--;
			}
		}

		list_for_each_entry_safe(p2plink, tmp, &dev->p2p_link_props, list) {
			/*
			 * If there is a p2p link to the dev being deleted
			 * then remove that p2p link also.
			 */
			if (p2plink->node_to == proximity_domain) {
				list_del(&p2plink->list);
				dev->node_props.p2p_links_count--;
			} else {
				if (p2plink->node_from > proximity_domain)
					p2plink->node_from--;
				if (p2plink->node_to > proximity_domain)
					p2plink->node_to--;
			}
		}
	}
}

int kfd_topology_remove_device(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev, *tmp;
	uint32_t gpu_id;
	int res = -ENODEV;
	int i = 0;

	down_write(&topology_lock);

	list_for_each_entry_safe(dev, tmp, &topology_device_list, list) {
		if (dev->gpu == gpu) {
			gpu_id = dev->gpu_id;
			kfd_remove_sysfs_node_entry(dev);
			kfd_release_topology_device(dev);
			sys_props.num_devices--;
			kfd_topology_update_io_links(i);
			topology_crat_proximity_domain = sys_props.num_devices-1;
			sys_props.generation_count++;
			res = 0;
			if (kfd_topology_update_sysfs() < 0)
				kfd_topology_release_sysfs();
			break;
		}
		i++;
	}

	up_write(&topology_lock);

	if (!res)
		kfd_notify_gpu_change(gpu_id, 0);

	return res;
}

/* kfd_topology_enum_kfd_devices - Enumerate through all devices in KFD
 *	topology. If GPU device is found @idx, then valid kfd_dev pointer is
 *	returned through @kdev
 * Return -	0: On success (@kdev will be NULL for non GPU nodes)
 *		-1: If end of list
 */
int kfd_topology_enum_kfd_devices(uint8_t idx, struct kfd_dev **kdev)
{

	struct kfd_topology_device *top_dev;
	uint8_t device_idx = 0;

	*kdev = NULL;
	down_read(&topology_lock);

	list_for_each_entry(top_dev, &topology_device_list, list) {
		if (device_idx == idx) {
			*kdev = top_dev->gpu;
			up_read(&topology_lock);
			return 0;
		}

		device_idx++;
	}

	up_read(&topology_lock);

	return -1;

}

static int kfd_cpumask_to_apic_id(const struct cpumask *cpumask)
{
	int first_cpu_of_numa_node;

	if (!cpumask || cpumask == cpu_none_mask)
		return -1;
	first_cpu_of_numa_node = cpumask_first(cpumask);
	if (first_cpu_of_numa_node >= nr_cpu_ids)
		return -1;
#ifdef CONFIG_X86_64
	return cpu_data(first_cpu_of_numa_node).apicid;
#else
	return first_cpu_of_numa_node;
#endif
}

/* kfd_numa_node_to_apic_id - Returns the APIC ID of the first logical processor
 *	of the given NUMA node (numa_node_id)
 * Return -1 on failure
 */
int kfd_numa_node_to_apic_id(int numa_node_id)
{
	if (numa_node_id == -1) {
		pr_warn("Invalid NUMA Node. Use online CPU mask\n");
		return kfd_cpumask_to_apic_id(cpu_online_mask);
	}
	return kfd_cpumask_to_apic_id(cpumask_of_node(numa_node_id));
}

void kfd_double_confirm_iommu_support(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev;

	gpu->use_iommu_v2 = false;

	if (!gpu->device_info.needs_iommu_device)
		return;

	down_read(&topology_lock);

	/* Only use IOMMUv2 if there is an APU topology node with no GPU
	 * assigned yet. This GPU will be assigned to it.
	 */
	list_for_each_entry(dev, &topology_device_list, list)
		if (dev->node_props.cpu_cores_count &&
		    dev->node_props.simd_count &&
		    !dev->gpu)
			gpu->use_iommu_v2 = true;

	up_read(&topology_lock);
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_hqds_by_device(struct seq_file *m, void *data)
{
	struct kfd_topology_device *dev;
	unsigned int i = 0;
	int r = 0;

	down_read(&topology_lock);

	list_for_each_entry(dev, &topology_device_list, list) {
		if (!dev->gpu) {
			i++;
			continue;
		}

		seq_printf(m, "Node %u, gpu_id %x:\n", i++, dev->gpu->id);
		r = dqm_debugfs_hqds(m, dev->gpu->dqm);
		if (r)
			break;
	}

	up_read(&topology_lock);

	return r;
}

int kfd_debugfs_rls_by_device(struct seq_file *m, void *data)
{
	struct kfd_topology_device *dev;
	unsigned int i = 0;
	int r = 0;

	down_read(&topology_lock);

	list_for_each_entry(dev, &topology_device_list, list) {
		if (!dev->gpu) {
			i++;
			continue;
		}

		seq_printf(m, "Node %u, gpu_id %x:\n", i++, dev->gpu->id);
		r = pm_debugfs_runlist(m, &dev->gpu->dqm->packet_mgr);
		if (r)
			break;
	}

	up_read(&topology_lock);

	return r;
}

#endif
