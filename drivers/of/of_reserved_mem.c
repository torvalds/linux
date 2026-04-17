// SPDX-License-Identifier: GPL-2.0+
/*
 * Device tree based initialization code for reserved memory.
 *
 * Copyright (c) 2013, 2015 The Linux Foundation. All Rights Reserved.
 * Copyright (c) 2013,2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 * Author: Josh Cartwright <joshc@codeaurora.org>
 */

#define pr_fmt(fmt)	"OF: reserved mem: " fmt

#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/of_reserved_mem.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/kmemleak.h>

#include "of_private.h"

static struct reserved_mem reserved_mem_array[MAX_RESERVED_REGIONS] __initdata;
static struct reserved_mem *reserved_mem __refdata = reserved_mem_array;
static int total_reserved_mem_cnt = MAX_RESERVED_REGIONS;
static int reserved_mem_count;

static int __init early_init_dt_alloc_reserved_memory_arch(phys_addr_t size,
	phys_addr_t align, phys_addr_t start, phys_addr_t end, bool nomap,
	phys_addr_t *res_base)
{
	phys_addr_t base;
	int err = 0;

	end = !end ? MEMBLOCK_ALLOC_ANYWHERE : end;
	align = !align ? SMP_CACHE_BYTES : align;
	base = memblock_phys_alloc_range(size, align, start, end);
	if (!base)
		return -ENOMEM;

	*res_base = base;
	if (nomap) {
		err = memblock_mark_nomap(base, size);
		if (err)
			memblock_phys_free(base, size);
	}

	if (!err)
		kmemleak_ignore_phys(base);

	return err;
}

/*
 * alloc_reserved_mem_array() - allocate memory for the reserved_mem
 * array using memblock
 *
 * This function is used to allocate memory for the reserved_mem
 * array according to the total number of reserved memory regions
 * defined in the DT.
 * After the new array is allocated, the information stored in
 * the initial static array is copied over to this new array and
 * the new array is used from this point on.
 */
static void __init alloc_reserved_mem_array(void)
{
	struct reserved_mem *new_array;
	size_t alloc_size, copy_size, memset_size;

	alloc_size = array_size(total_reserved_mem_cnt, sizeof(*new_array));
	if (alloc_size == SIZE_MAX) {
		pr_err("Failed to allocate memory for reserved_mem array with err: %d", -EOVERFLOW);
		return;
	}

	new_array = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!new_array) {
		pr_err("Failed to allocate memory for reserved_mem array with err: %d", -ENOMEM);
		return;
	}

	copy_size = array_size(reserved_mem_count, sizeof(*new_array));
	if (copy_size == SIZE_MAX) {
		memblock_free(new_array, alloc_size);
		total_reserved_mem_cnt = MAX_RESERVED_REGIONS;
		pr_err("Failed to allocate memory for reserved_mem array with err: %d", -EOVERFLOW);
		return;
	}

	memset_size = alloc_size - copy_size;

	memcpy(new_array, reserved_mem, copy_size);
	memset(new_array + reserved_mem_count, 0, memset_size);

	reserved_mem = new_array;
}

static void fdt_init_reserved_mem_node(unsigned long node, const char *uname,
				       phys_addr_t base, phys_addr_t size);
static int fdt_validate_reserved_mem_node(unsigned long node,
					  phys_addr_t *align);
static int fdt_fixup_reserved_mem_node(unsigned long node,
				       phys_addr_t base, phys_addr_t size);

static int __init early_init_dt_reserve_memory(phys_addr_t base,
					       phys_addr_t size, bool nomap)
{
	if (nomap) {
		/*
		 * If the memory is already reserved (by another region), we
		 * should not allow it to be marked nomap, but don't worry
		 * if the region isn't memory as it won't be mapped.
		 */
		if (memblock_overlaps_region(&memblock.memory, base, size) &&
		    memblock_is_region_reserved(base, size))
			return -EBUSY;

		return memblock_mark_nomap(base, size);
	}
	return memblock_reserve(base, size);
}

/*
 * __reserved_mem_reserve_reg() - reserve all memory described in 'reg' property
 */
static int __init __reserved_mem_reserve_reg(unsigned long node,
					     const char *uname)
{
	phys_addr_t base, size;
	int i, len, err;
	const __be32 *prop;
	bool nomap;

	prop = of_flat_dt_get_addr_size_prop(node, "reg", &len);
	if (!prop)
		return -ENOENT;

	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	err = fdt_validate_reserved_mem_node(node, NULL);
	if (err && err != -ENODEV)
		return err;

	for (i = 0; i < len; i++) {
		u64 b, s;

		of_flat_dt_read_addr_size(prop, i, &b, &s);

		base = b;
		size = s;

		if (size && early_init_dt_reserve_memory(base, size, nomap) == 0) {
			fdt_fixup_reserved_mem_node(node, base, size);
			pr_debug("Reserved memory: reserved region for node '%s': base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));
		} else {
			pr_err("Reserved memory: failed to reserve memory for node '%s': base %pa, size %lu MiB\n",
			       uname, &base, (unsigned long)(size / SZ_1M));
		}
	}
	return 0;
}

/*
 * __reserved_mem_check_root() - check if #size-cells, #address-cells provided
 * in /reserved-memory matches the values supported by the current implementation,
 * also check if ranges property has been provided
 */
static int __init __reserved_mem_check_root(unsigned long node)
{
	const __be32 *prop;

	prop = of_get_flat_dt_prop(node, "#size-cells", NULL);
	if (!prop || be32_to_cpup(prop) != dt_root_size_cells)
		return -EINVAL;

	prop = of_get_flat_dt_prop(node, "#address-cells", NULL);
	if (!prop || be32_to_cpup(prop) != dt_root_addr_cells)
		return -EINVAL;

	prop = of_get_flat_dt_prop(node, "ranges", NULL);
	if (!prop)
		return -EINVAL;
	return 0;
}

static int __init __rmem_cmp(const void *a, const void *b)
{
	const struct reserved_mem *ra = a, *rb = b;

	if (ra->base < rb->base)
		return -1;

	if (ra->base > rb->base)
		return 1;

	/*
	 * Put the dynamic allocations (address == 0, size == 0) before static
	 * allocations at address 0x0 so that overlap detection works
	 * correctly.
	 */
	if (ra->size < rb->size)
		return -1;
	if (ra->size > rb->size)
		return 1;

	return 0;
}

static void __init __rmem_check_for_overlap(void)
{
	int i;

	if (reserved_mem_count < 2)
		return;

	sort(reserved_mem, reserved_mem_count, sizeof(reserved_mem[0]),
	     __rmem_cmp, NULL);
	for (i = 0; i < reserved_mem_count - 1; i++) {
		struct reserved_mem *this, *next;

		this = &reserved_mem[i];
		next = &reserved_mem[i + 1];

		if (this->base + this->size > next->base) {
			phys_addr_t this_end, next_end;

			this_end = this->base + this->size;
			next_end = next->base + next->size;
			pr_err("OVERLAP DETECTED!\n%s (%pa--%pa) overlaps with %s (%pa--%pa)\n",
			       this->name, &this->base, &this_end,
			       next->name, &next->base, &next_end);
		}
	}
}

/**
 * fdt_scan_reserved_mem_late() - Scan FDT and initialize remaining reserved
 * memory regions.
 *
 * This function is used to scan again through the DT and initialize the
 * "static" reserved memory regions, that are defined using the "reg"
 * property. Each such region is then initialized with its specific init
 * function and stored in the global reserved_mem array.
 */
void __init fdt_scan_reserved_mem_late(void)
{
	const void *fdt = initial_boot_params;
	phys_addr_t base, size;
	int node, child;

	if (!fdt)
		return;

	node = fdt_path_offset(fdt, "/reserved-memory");
	if (node < 0) {
		pr_info("Reserved memory: No reserved-memory node in the DT\n");
		return;
	}

	/* Attempt dynamic allocation of a new reserved_mem array */
	alloc_reserved_mem_array();

	if (__reserved_mem_check_root(node)) {
		pr_err("Reserved memory: unsupported node format, ignoring\n");
		return;
	}

	fdt_for_each_subnode(child, fdt, node) {
		const char *uname;
		u64 b, s;
		int ret;

		if (!of_fdt_device_is_available(fdt, child))
			continue;

		if (!of_flat_dt_get_addr_size(child, "reg", &b, &s))
			continue;

		ret = fdt_validate_reserved_mem_node(child, NULL);
		if (ret && ret != -ENODEV)
			continue;

		base = b;
		size = s;

		if (size) {
			uname = fdt_get_name(fdt, child, NULL);
			fdt_init_reserved_mem_node(child, uname, base, size);
		}
	}

	/* check for overlapping reserved regions */
	__rmem_check_for_overlap();
}

static int __init __reserved_mem_alloc_size(unsigned long node, const char *uname);

/*
 * fdt_scan_reserved_mem() - reserve and allocate memory occupied by
 * reserved memory regions.
 *
 * This function is used to scan through the FDT and mark memory occupied
 * by all static (defined by the "reg" property) reserved memory regions.
 * Then memory for all dynamic regions (defined by size & alignment) is
 * allocated, a region specific init function is called and region information
 * is stored in the reserved_mem array.
 */
int __init fdt_scan_reserved_mem(void)
{
	int node, child;
	int dynamic_nodes_cnt = 0, count = 0;
	int dynamic_nodes[MAX_RESERVED_REGIONS];
	const void *fdt = initial_boot_params;

	node = fdt_path_offset(fdt, "/reserved-memory");
	if (node < 0)
		return -ENODEV;

	if (__reserved_mem_check_root(node) != 0) {
		pr_err("Reserved memory: unsupported node format, ignoring\n");
		return -EINVAL;
	}

	fdt_for_each_subnode(child, fdt, node) {
		const char *uname;
		int err;

		if (!of_fdt_device_is_available(fdt, child))
			continue;

		uname = fdt_get_name(fdt, child, NULL);

		err = __reserved_mem_reserve_reg(child, uname);
		if (!err)
			count++;
		/*
		 * Save the nodes for the dynamically-placed regions
		 * into an array which will be used for allocation right
		 * after all the statically-placed regions are reserved
		 * or marked as no-map. This is done to avoid dynamically
		 * allocating from one of the statically-placed regions.
		 */
		if (err == -ENOENT && of_get_flat_dt_prop(child, "size", NULL)) {
			dynamic_nodes[dynamic_nodes_cnt] = child;
			dynamic_nodes_cnt++;
		}
	}
	for (int i = 0; i < dynamic_nodes_cnt; i++) {
		const char *uname;
		int err;

		child = dynamic_nodes[i];
		uname = fdt_get_name(fdt, child, NULL);
		err = __reserved_mem_alloc_size(child, uname);
		if (!err)
			count++;
	}
	total_reserved_mem_cnt = count;
	return 0;
}

/*
 * __reserved_mem_alloc_in_range() - allocate reserved memory described with
 *	'alloc-ranges'. Choose bottom-up/top-down depending on nearby existing
 *	reserved regions to keep the reserved memory contiguous if possible.
 */
static int __init __reserved_mem_alloc_in_range(phys_addr_t size,
	phys_addr_t align, phys_addr_t start, phys_addr_t end, bool nomap,
	phys_addr_t *res_base)
{
	bool prev_bottom_up = memblock_bottom_up();
	bool bottom_up = false, top_down = false;
	int ret, i;

	for (i = 0; i < reserved_mem_count; i++) {
		struct reserved_mem *rmem = &reserved_mem[i];

		/* Skip regions that were not reserved yet */
		if (rmem->size == 0)
			continue;

		/*
		 * If range starts next to an existing reservation, use bottom-up:
		 *	|....RRRR................RRRRRRRR..............|
		 *	       --RRRR------
		 */
		if (start >= rmem->base && start <= (rmem->base + rmem->size))
			bottom_up = true;

		/*
		 * If range ends next to an existing reservation, use top-down:
		 *	|....RRRR................RRRRRRRR..............|
		 *	              -------RRRR-----
		 */
		if (end >= rmem->base && end <= (rmem->base + rmem->size))
			top_down = true;
	}

	/* Change setting only if either bottom-up or top-down was selected */
	if (bottom_up != top_down)
		memblock_set_bottom_up(bottom_up);

	ret = early_init_dt_alloc_reserved_memory_arch(size, align,
			start, end, nomap, res_base);

	/* Restore old setting if needed */
	if (bottom_up != top_down)
		memblock_set_bottom_up(prev_bottom_up);

	return ret;
}

/*
 * __reserved_mem_alloc_size() - allocate reserved memory described by
 *	'size', 'alignment'  and 'alloc-ranges' properties.
 */
static int __init __reserved_mem_alloc_size(unsigned long node, const char *uname)
{
	phys_addr_t start = 0, end = 0;
	phys_addr_t base = 0, align = 0, size;
	int i, len;
	const __be32 *prop;
	bool nomap;
	int ret;

	prop = of_get_flat_dt_prop(node, "size", &len);
	if (!prop)
		return -EINVAL;

	if (len != dt_root_size_cells * sizeof(__be32)) {
		pr_err("invalid size property in '%s' node.\n", uname);
		return -EINVAL;
	}
	size = dt_mem_next_cell(dt_root_size_cells, &prop);

	prop = of_get_flat_dt_prop(node, "alignment", &len);
	if (prop) {
		if (len != dt_root_addr_cells * sizeof(__be32)) {
			pr_err("invalid alignment property in '%s' node.\n",
				uname);
			return -EINVAL;
		}
		align = dt_mem_next_cell(dt_root_addr_cells, &prop);
	}

	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	ret = fdt_validate_reserved_mem_node(node, &align);
	if (ret && ret != -ENODEV)
		return ret;

	prop = of_flat_dt_get_addr_size_prop(node, "alloc-ranges", &len);
	if (prop) {
		for (i = 0; i < len; i++) {
			u64 b, s;

			of_flat_dt_read_addr_size(prop, i, &b, &s);

			start = b;
			end = b + s;

			base = 0;
			ret = __reserved_mem_alloc_in_range(size, align,
					start, end, nomap, &base);
			if (ret == 0) {
				pr_debug("allocated memory for '%s' node: base %pa, size %lu MiB\n",
					uname, &base,
					(unsigned long)(size / SZ_1M));
				break;
			}
		}
	} else {
		ret = early_init_dt_alloc_reserved_memory_arch(size, align,
							0, 0, nomap, &base);
		if (ret == 0)
			pr_debug("allocated memory for '%s' node: base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));
	}

	if (base == 0) {
		pr_err("failed to allocate memory for node '%s': size %lu MiB\n",
		       uname, (unsigned long)(size / SZ_1M));
		return -ENOMEM;
	}

	fdt_fixup_reserved_mem_node(node, base, size);
	fdt_init_reserved_mem_node(node, uname, base, size);

	return 0;
}

extern const struct of_device_id __reservedmem_of_table[];
static const struct of_device_id __rmem_of_table_sentinel
	__used __section("__reservedmem_of_table_end");

/**
 * fdt_fixup_reserved_mem_node() - call fixup function for a reserved memory node
 * @node: FDT node to fixup
 * @base: base address of the reserved memory region
 * @size: size of the reserved memory region
 *
 * This function iterates through the reserved memory drivers and calls
 * the node_fixup callback for the compatible entry matching the node.
 *
 * Return: 0 on success, -ENODEV if no compatible match found
 */
static int __init fdt_fixup_reserved_mem_node(unsigned long node,
					phys_addr_t base, phys_addr_t size)
{
	const struct of_device_id *i;
	int ret = -ENODEV;

	for (i = __reservedmem_of_table; ret == -ENODEV &&
	     i < &__rmem_of_table_sentinel; i++) {
		const struct reserved_mem_ops *ops = i->data;

		if (!of_flat_dt_is_compatible(node, i->compatible))
			continue;

		if (ops->node_fixup)
			ret = ops->node_fixup(node, base, size);
	}
	return ret;
}

/**
 * fdt_validate_reserved_mem_node() - validate a reserved memory node
 * @node: FDT node to validate
 * @align: pointer to store the validated alignment (may be modified by callback)
 *
 * This function iterates through the reserved memory drivers and calls
 * the node_validate callback for the compatible entry matching the node.
 *
 * Return: 0 on success, -ENODEV if no compatible match found
 */
static int __init fdt_validate_reserved_mem_node(unsigned long node, phys_addr_t *align)
{
	const struct of_device_id *i;
	int ret = -ENODEV;

	for (i = __reservedmem_of_table; ret == -ENODEV &&
	     i < &__rmem_of_table_sentinel; i++) {
		const struct reserved_mem_ops *ops = i->data;

		if (!of_flat_dt_is_compatible(node, i->compatible))
			continue;

		if (ops->node_validate)
			ret = ops->node_validate(node, align);
	}
	return ret;
}

/**
 * __reserved_mem_init_node() - initialize a reserved memory region
 * @rmem: reserved_mem structure to initialize
 * @node: FDT node describing the reserved memory region
 *
 * This function iterates through the reserved memory drivers and calls the
 * node_init callback for the compatible entry matching the node. On success,
 * the operations pointer is stored in the reserved_mem structure.
 *
 * Return: 0 on success, -ENODEV if no compatible match found
 */
static int __init __reserved_mem_init_node(struct reserved_mem *rmem,
					   unsigned long node)
{
	const struct of_device_id *i;
	int ret = -ENODEV;

	for (i = __reservedmem_of_table; ret == -ENODEV &&
	     i < &__rmem_of_table_sentinel; i++) {
		const struct reserved_mem_ops *ops = i->data;
		const char *compat = i->compatible;

		if (!of_flat_dt_is_compatible(node, compat))
			continue;

		ret = ops->node_init(node, rmem);
		if (ret == 0) {
			rmem->ops = ops;
			pr_info("initialized node %s, compatible id %s\n",
				rmem->name, compat);
			return ret;
		}
	}
	return ret;
}

/**
 * fdt_init_reserved_mem_node() - Initialize a reserved memory region
 * @node: fdt node of the initialized region
 * @uname: name of the reserved memory node
 * @base: base address of the reserved memory region
 * @size: size of the reserved memory region
 *
 * This function calls the region-specific initialization function for a
 * reserved memory region and saves all region-specific data to the
 * reserved_mem array to allow of_reserved_mem_lookup() to find it.
 */
static void __init fdt_init_reserved_mem_node(unsigned long node, const char *uname,
					      phys_addr_t base, phys_addr_t size)
{
	int err = 0;
	bool nomap;

	struct reserved_mem *rmem = &reserved_mem[reserved_mem_count];

	if (reserved_mem_count == total_reserved_mem_cnt) {
		pr_err("not enough space for all defined regions.\n");
		return;
	}

	rmem->name = uname;
	rmem->base = base;
	rmem->size = size;

	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	err = __reserved_mem_init_node(rmem, node);
	if (err != 0 && err != -ENODEV) {
		pr_info("node %s compatible matching fail\n", rmem->name);
		rmem->name = NULL;

		if (nomap)
			memblock_clear_nomap(rmem->base, rmem->size);
		else
			memblock_phys_free(rmem->base, rmem->size);
		return;
	} else {
		phys_addr_t end = rmem->base + rmem->size - 1;
		bool reusable =
			(of_get_flat_dt_prop(node, "reusable", NULL)) != NULL;

		pr_info("%pa..%pa (%lu KiB) %s %s %s\n",
			&rmem->base, &end, (unsigned long)(rmem->size / SZ_1K),
			nomap ? "nomap" : "map",
			reusable ? "reusable" : "non-reusable",
			rmem->name ? rmem->name : "unknown");
	}

	reserved_mem_count++;
}

struct rmem_assigned_device {
	struct device *dev;
	struct reserved_mem *rmem;
	struct list_head list;
};

static LIST_HEAD(of_rmem_assigned_device_list);
static DEFINE_MUTEX(of_rmem_assigned_device_mutex);

/**
 * of_reserved_mem_device_init_by_idx() - assign reserved memory region to
 *					  given device
 * @dev:	Pointer to the device to configure
 * @np:		Pointer to the device_node with 'reserved-memory' property
 * @idx:	Index of selected region
 *
 * This function assigns respective DMA-mapping operations based on reserved
 * memory region specified by 'memory-region' property in @np node to the @dev
 * device. When driver needs to use more than one reserved memory region, it
 * should allocate child devices and initialize regions by name for each of
 * child device.
 *
 * Returns error code or zero on success.
 */
int of_reserved_mem_device_init_by_idx(struct device *dev,
				       struct device_node *np, int idx)
{
	struct rmem_assigned_device *rd;
	struct device_node *target;
	struct reserved_mem *rmem;
	int ret;

	if (!np || !dev)
		return -EINVAL;

	target = of_parse_phandle(np, "memory-region", idx);
	if (!target)
		return -ENODEV;

	if (!of_device_is_available(target)) {
		of_node_put(target);
		return 0;
	}

	rmem = of_reserved_mem_lookup(target);
	of_node_put(target);

	if (!rmem || !rmem->ops || !rmem->ops->device_init)
		return -EINVAL;

	rd = kmalloc_obj(struct rmem_assigned_device);
	if (!rd)
		return -ENOMEM;

	ret = rmem->ops->device_init(rmem, dev);
	if (ret == 0) {
		rd->dev = dev;
		rd->rmem = rmem;

		mutex_lock(&of_rmem_assigned_device_mutex);
		list_add(&rd->list, &of_rmem_assigned_device_list);
		mutex_unlock(&of_rmem_assigned_device_mutex);

		dev_info(dev, "assigned reserved memory node %s\n", rmem->name);
	} else {
		kfree(rd);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(of_reserved_mem_device_init_by_idx);

/**
 * of_reserved_mem_device_init_by_name() - assign named reserved memory region
 *					   to given device
 * @dev: pointer to the device to configure
 * @np: pointer to the device node with 'memory-region' property
 * @name: name of the selected memory region
 *
 * Returns: 0 on success or a negative error-code on failure.
 */
int of_reserved_mem_device_init_by_name(struct device *dev,
					struct device_node *np,
					const char *name)
{
	int idx = of_property_match_string(np, "memory-region-names", name);

	return of_reserved_mem_device_init_by_idx(dev, np, idx);
}
EXPORT_SYMBOL_GPL(of_reserved_mem_device_init_by_name);

/**
 * of_reserved_mem_device_release() - release reserved memory device structures
 * @dev:	Pointer to the device to deconfigure
 *
 * This function releases structures allocated for memory region handling for
 * the given device.
 */
void of_reserved_mem_device_release(struct device *dev)
{
	struct rmem_assigned_device *rd, *tmp;
	LIST_HEAD(release_list);

	mutex_lock(&of_rmem_assigned_device_mutex);
	list_for_each_entry_safe(rd, tmp, &of_rmem_assigned_device_list, list) {
		if (rd->dev == dev)
			list_move_tail(&rd->list, &release_list);
	}
	mutex_unlock(&of_rmem_assigned_device_mutex);

	list_for_each_entry_safe(rd, tmp, &release_list, list) {
		if (rd->rmem && rd->rmem->ops && rd->rmem->ops->device_release)
			rd->rmem->ops->device_release(rd->rmem, dev);

		kfree(rd);
	}
}
EXPORT_SYMBOL_GPL(of_reserved_mem_device_release);

/**
 * of_reserved_mem_lookup() - acquire reserved_mem from a device node
 * @np:		node pointer of the desired reserved-memory region
 *
 * This function allows drivers to acquire a reference to the reserved_mem
 * struct based on a device node handle.
 *
 * Returns a reserved_mem reference, or NULL on error.
 */
struct reserved_mem *of_reserved_mem_lookup(struct device_node *np)
{
	const char *name;
	int i;

	if (!np->full_name)
		return NULL;

	name = kbasename(np->full_name);
	for (i = 0; i < reserved_mem_count; i++)
		if (!strcmp(reserved_mem[i].name, name))
			return &reserved_mem[i];

	return NULL;
}
EXPORT_SYMBOL_GPL(of_reserved_mem_lookup);

/**
 * of_reserved_mem_region_to_resource() - Get a reserved memory region as a resource
 * @np:		node containing 'memory-region' property
 * @idx:	index of 'memory-region' property to lookup
 * @res:	Pointer to a struct resource to fill in with reserved region
 *
 * This function allows drivers to lookup a node's 'memory-region' property
 * entries by index and return a struct resource for the entry.
 *
 * Returns 0 on success with @res filled in. Returns -ENODEV if 'memory-region'
 * is missing or unavailable, -EINVAL for any other error.
 */
int of_reserved_mem_region_to_resource(const struct device_node *np,
				       unsigned int idx, struct resource *res)
{
	struct reserved_mem *rmem;

	if (!np)
		return -EINVAL;

	struct device_node *target __free(device_node) = of_parse_phandle(np, "memory-region", idx);
	if (!target || !of_device_is_available(target))
		return -ENODEV;

	rmem = of_reserved_mem_lookup(target);
	if (!rmem)
		return -EINVAL;

	resource_set_range(res, rmem->base, rmem->size);
	res->flags = IORESOURCE_MEM;
	res->name = rmem->name;
	return 0;
}
EXPORT_SYMBOL_GPL(of_reserved_mem_region_to_resource);

/**
 * of_reserved_mem_region_to_resource_byname() - Get a reserved memory region as a resource
 * @np:		node containing 'memory-region' property
 * @name:	name of 'memory-region' property entry to lookup
 * @res:	Pointer to a struct resource to fill in with reserved region
 *
 * This function allows drivers to lookup a node's 'memory-region' property
 * entries by name and return a struct resource for the entry.
 *
 * Returns 0 on success with @res filled in, or a negative error-code on
 * failure.
 */
int of_reserved_mem_region_to_resource_byname(const struct device_node *np,
					      const char *name,
					      struct resource *res)
{
	int idx;

	if (!name)
		return -EINVAL;

	idx = of_property_match_string(np, "memory-region-names", name);
	if (idx < 0)
		return idx;

	return of_reserved_mem_region_to_resource(np, idx, res);
}
EXPORT_SYMBOL_GPL(of_reserved_mem_region_to_resource_byname);

/**
 * of_reserved_mem_region_count() - Return the number of 'memory-region' entries
 * @np:		node containing 'memory-region' property
 *
 * This function allows drivers to retrieve the number of entries for a node's
 * 'memory-region' property.
 *
 * Returns the number of entries on success, or negative error code on a
 * malformed property.
 */
int of_reserved_mem_region_count(const struct device_node *np)
{
	return of_count_phandle_with_args(np, "memory-region", NULL);
}
EXPORT_SYMBOL_GPL(of_reserved_mem_region_count);
