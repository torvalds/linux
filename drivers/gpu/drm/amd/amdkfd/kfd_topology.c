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
#include "kfd_device_queue_manager.h"

/* topology_device_list - Master list of all topology devices */
static struct list_head topology_device_list;
struct kfd_system_properties sys_props;

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

static void kfd_release_topology_device(struct kfd_topology_device *dev)
{
	struct kfd_mem_properties *mem;
	struct kfd_cache_properties *cache;
	struct kfd_iolink_properties *iolink;

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

	list_add_tail(&dev->list, device_list);

	return dev;
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
	.release = kfd_topology_kobj_release,
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
	.release = kfd_topology_kobj_release,
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
	.release = kfd_topology_kobj_release,
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
		pr_info_once("mem_banks_count truncated from %d to %d\n",
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
	struct kfd_iolink_properties *iolink;
	struct kfd_cache_properties *cache;
	struct kfd_mem_properties *mem;

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
	if (!sys_props.kobj_topology) {
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

/* Called with write topology_lock acquired */
static void kfd_topology_update_device_list(struct list_head *temp_list,
					struct list_head *master_list)
{
	while (!list_empty(temp_list)) {
		list_move_tail(temp_list->next, master_list);
		sys_props.num_devices++;
	}
}

int kfd_topology_init(void)
{
	void *crat_image = NULL;
	size_t image_size = 0;
	int ret;
	struct list_head temp_topology_device_list;

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

	/*
	 * Get the CRAT image from the ACPI
	 */
	ret = kfd_create_crat_image_acpi(&crat_image, &image_size);
	if (!ret) {
		ret = kfd_parse_crat_table(crat_image,
					   &temp_topology_device_list, 0);
		if (ret)
			goto err;
	} else if (ret == -ENODATA) {
		/* TODO: Create fake CRAT table */
		ret = 0;
		goto err;
	} else {
		pr_err("Couldn't get CRAT table size from ACPI\n");
		goto err;
	}

	down_write(&topology_lock);
	kfd_topology_update_device_list(&temp_topology_device_list,
					&topology_device_list);
	ret = kfd_topology_update_sysfs();
	up_write(&topology_lock);

	if (!ret) {
		sys_props.generation_count++;
		pr_info("Finished initializing topology\n");
	} else
		pr_err("Failed to update topology in sysfs ret=%d\n", ret);

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

static void kfd_debug_print_topology(void)
{
	struct kfd_topology_device *dev;
	uint32_t i = 0;

	pr_info("DEBUG PRINT OF TOPOLOGY:");
	list_for_each_entry(dev, &topology_device_list, list) {
		pr_info("Node: %d\n", i);
		pr_info("\tGPU assigned: %s\n", (dev->gpu ? "yes" : "no"));
		pr_info("\tCPU count: %d\n", dev->node_props.cpu_cores_count);
		pr_info("\tSIMD count: %d\n", dev->node_props.simd_count);
		i++;
	}
}

static uint32_t kfd_generate_gpu_id(struct kfd_dev *gpu)
{
	uint32_t hashout;
	uint32_t buf[7];
	uint64_t local_mem_size;
	int i;
	struct kfd_local_mem_info local_mem_info;

	if (!gpu)
		return 0;

	gpu->kfd2kgd->get_local_mem_info(gpu->kgd, &local_mem_info);

	local_mem_size = local_mem_info.local_mem_size_private +
			local_mem_info.local_mem_size_public;

	buf[0] = gpu->pdev->devfn;
	buf[1] = gpu->pdev->subsystem_vendor;
	buf[2] = gpu->pdev->subsystem_device;
	buf[3] = gpu->pdev->device;
	buf[4] = gpu->pdev->bus->number;
	buf[5] = lower_32_bits(local_mem_size);
	buf[6] = upper_32_bits(local_mem_size);

	for (i = 0, hashout = 0; i < 7; i++)
		hashout ^= hash_32(buf[i], KFD_GPU_ID_HASH_WIDTH);

	return hashout;
}

static struct kfd_topology_device *kfd_assign_gpu(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev;
	struct kfd_topology_device *out_dev = NULL;

	list_for_each_entry(dev, &topology_device_list, list)
		if (!dev->gpu && (dev->node_props.simd_count > 0)) {
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
	struct kfd_cu_info cu_info;
	int res = 0;
	struct list_head temp_topology_device_list;

	INIT_LIST_HEAD(&temp_topology_device_list);

	gpu_id = kfd_generate_gpu_id(gpu);

	pr_debug("Adding new GPU (ID: 0x%x) to topology\n", gpu_id);

	/*
	 * Try to assign the GPU to existing topology device (generated from
	 * CRAT table
	 */
	dev = kfd_assign_gpu(gpu);
	if (!dev) {
		pr_info("GPU was not found in the current topology. Extending.\n");
		kfd_debug_print_topology();
		dev = kfd_create_topology_device(&temp_topology_device_list);
		if (!dev) {
			res = -ENOMEM;
			goto err;
		}

		dev->gpu = gpu;

		/*
		 * TODO: Make a call to retrieve topology information from the
		 * GPU vBIOS
		 */

		down_write(&topology_lock);
		kfd_topology_update_device_list(&temp_topology_device_list,
			&topology_device_list);

		/* Update the SYSFS tree, since we added another topology
		 * device
		 */
		if (kfd_topology_update_sysfs() < 0)
			kfd_topology_release_sysfs();

		up_write(&topology_lock);

	}

	dev->gpu_id = gpu_id;
	gpu->id = gpu_id;
	dev->gpu->kfd2kgd->get_cu_info(dev->gpu->kgd, &cu_info);
	dev->node_props.simd_count = dev->node_props.simd_per_cu *
			cu_info.cu_active_number;
	dev->node_props.vendor_id = gpu->pdev->vendor;
	dev->node_props.device_id = gpu->pdev->device;
	dev->node_props.location_id = PCI_DEVID(gpu->pdev->bus->number,
		gpu->pdev->devfn);
	/*
	 * TODO: Retrieve max engine clock values from KGD
	 */

	if (dev->gpu->device_info->asic_family == CHIP_CARRIZO) {
		dev->node_props.capability |= HSA_CAP_DOORBELL_PACKET_TYPE;
		pr_debug("Adding doorbell packet type capability\n");
	}

	if (!res)
		kfd_notify_gpu_change(gpu_id, 1);
err:
	return res;
}

int kfd_topology_remove_device(struct kfd_dev *gpu)
{
	struct kfd_topology_device *dev, *tmp;
	uint32_t gpu_id;
	int res = -ENODEV;

	down_write(&topology_lock);

	list_for_each_entry_safe(dev, tmp, &topology_device_list, list)
		if (dev->gpu == gpu) {
			gpu_id = dev->gpu_id;
			kfd_remove_sysfs_node_entry(dev);
			kfd_release_topology_device(dev);
			sys_props.num_devices--;
			res = 0;
			if (kfd_topology_update_sysfs() < 0)
				kfd_topology_release_sysfs();
			break;
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
		r = pm_debugfs_runlist(m, &dev->gpu->dqm->packets);
		if (r)
			break;
	}

	up_read(&topology_lock);

	return r;
}

#endif
