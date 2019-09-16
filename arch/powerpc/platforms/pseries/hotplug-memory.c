// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pseries Memory Hotplug infrastructure.
 *
 * Copyright (C) 2008 Badari Pulavarty, IBM Corporation
 */

#define pr_fmt(fmt)	"pseries-hotplug-mem: " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/slab.h>

#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/sparsemem.h>
#include <asm/fadump.h>
#include <asm/drmem.h>
#include "pseries.h"

static bool rtas_hp_event;

unsigned long pseries_memory_block_size(void)
{
	struct device_node *np;
	unsigned int memblock_size = MIN_MEMORY_BLOCK_SIZE;
	struct resource r;

	np = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (np) {
		const __be64 *size;

		size = of_get_property(np, "ibm,lmb-size", NULL);
		if (size)
			memblock_size = be64_to_cpup(size);
		of_node_put(np);
	} else  if (machine_is(pseries)) {
		/* This fallback really only applies to pseries */
		unsigned int memzero_size = 0;

		np = of_find_node_by_path("/memory@0");
		if (np) {
			if (!of_address_to_resource(np, 0, &r))
				memzero_size = resource_size(&r);
			of_node_put(np);
		}

		if (memzero_size) {
			/* We now know the size of memory@0, use this to find
			 * the first memoryblock and get its size.
			 */
			char buf[64];

			sprintf(buf, "/memory@%x", memzero_size);
			np = of_find_node_by_path(buf);
			if (np) {
				if (!of_address_to_resource(np, 0, &r))
					memblock_size = resource_size(&r);
				of_node_put(np);
			}
		}
	}
	return memblock_size;
}

static void dlpar_free_property(struct property *prop)
{
	kfree(prop->name);
	kfree(prop->value);
	kfree(prop);
}

static struct property *dlpar_clone_property(struct property *prop,
					     u32 prop_size)
{
	struct property *new_prop;

	new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
	if (!new_prop)
		return NULL;

	new_prop->name = kstrdup(prop->name, GFP_KERNEL);
	new_prop->value = kzalloc(prop_size, GFP_KERNEL);
	if (!new_prop->name || !new_prop->value) {
		dlpar_free_property(new_prop);
		return NULL;
	}

	memcpy(new_prop->value, prop->value, prop->length);
	new_prop->length = prop_size;

	of_property_set_flag(new_prop, OF_DYNAMIC);
	return new_prop;
}

static bool find_aa_index(struct device_node *dr_node,
			 struct property *ala_prop,
			 const u32 *lmb_assoc, u32 *aa_index)
{
	u32 *assoc_arrays, new_prop_size;
	struct property *new_prop;
	int aa_arrays, aa_array_entries, aa_array_sz;
	int i, index;

	/*
	 * The ibm,associativity-lookup-arrays property is defined to be
	 * a 32-bit value specifying the number of associativity arrays
	 * followed by a 32-bitvalue specifying the number of entries per
	 * array, followed by the associativity arrays.
	 */
	assoc_arrays = ala_prop->value;

	aa_arrays = be32_to_cpu(assoc_arrays[0]);
	aa_array_entries = be32_to_cpu(assoc_arrays[1]);
	aa_array_sz = aa_array_entries * sizeof(u32);

	for (i = 0; i < aa_arrays; i++) {
		index = (i * aa_array_entries) + 2;

		if (memcmp(&assoc_arrays[index], &lmb_assoc[1], aa_array_sz))
			continue;

		*aa_index = i;
		return true;
	}

	new_prop_size = ala_prop->length + aa_array_sz;
	new_prop = dlpar_clone_property(ala_prop, new_prop_size);
	if (!new_prop)
		return false;

	assoc_arrays = new_prop->value;

	/* increment the number of entries in the lookup array */
	assoc_arrays[0] = cpu_to_be32(aa_arrays + 1);

	/* copy the new associativity into the lookup array */
	index = aa_arrays * aa_array_entries + 2;
	memcpy(&assoc_arrays[index], &lmb_assoc[1], aa_array_sz);

	of_update_property(dr_node, new_prop);

	/*
	 * The associativity lookup array index for this lmb is
	 * number of entries - 1 since we added its associativity
	 * to the end of the lookup array.
	 */
	*aa_index = be32_to_cpu(assoc_arrays[0]) - 1;
	return true;
}

static int update_lmb_associativity_index(struct drmem_lmb *lmb)
{
	struct device_node *parent, *lmb_node, *dr_node;
	struct property *ala_prop;
	const u32 *lmb_assoc;
	u32 aa_index;
	bool found;

	parent = of_find_node_by_path("/");
	if (!parent)
		return -ENODEV;

	lmb_node = dlpar_configure_connector(cpu_to_be32(lmb->drc_index),
					     parent);
	of_node_put(parent);
	if (!lmb_node)
		return -EINVAL;

	lmb_assoc = of_get_property(lmb_node, "ibm,associativity", NULL);
	if (!lmb_assoc) {
		dlpar_free_cc_nodes(lmb_node);
		return -ENODEV;
	}

	dr_node = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!dr_node) {
		dlpar_free_cc_nodes(lmb_node);
		return -ENODEV;
	}

	ala_prop = of_find_property(dr_node, "ibm,associativity-lookup-arrays",
				    NULL);
	if (!ala_prop) {
		of_node_put(dr_node);
		dlpar_free_cc_nodes(lmb_node);
		return -ENODEV;
	}

	found = find_aa_index(dr_node, ala_prop, lmb_assoc, &aa_index);

	of_node_put(dr_node);
	dlpar_free_cc_nodes(lmb_node);

	if (!found) {
		pr_err("Could not find LMB associativity\n");
		return -1;
	}

	lmb->aa_index = aa_index;
	return 0;
}

static struct memory_block *lmb_to_memblock(struct drmem_lmb *lmb)
{
	unsigned long section_nr;
	struct mem_section *mem_sect;
	struct memory_block *mem_block;

	section_nr = pfn_to_section_nr(PFN_DOWN(lmb->base_addr));
	mem_sect = __nr_to_section(section_nr);

	mem_block = find_memory_block(mem_sect);
	return mem_block;
}

static int get_lmb_range(u32 drc_index, int n_lmbs,
			 struct drmem_lmb **start_lmb,
			 struct drmem_lmb **end_lmb)
{
	struct drmem_lmb *lmb, *start, *end;
	struct drmem_lmb *last_lmb;

	start = NULL;
	for_each_drmem_lmb(lmb) {
		if (lmb->drc_index == drc_index) {
			start = lmb;
			break;
		}
	}

	if (!start)
		return -EINVAL;

	end = &start[n_lmbs - 1];

	last_lmb = &drmem_info->lmbs[drmem_info->n_lmbs - 1];
	if (end > last_lmb)
		return -EINVAL;

	*start_lmb = start;
	*end_lmb = end;
	return 0;
}

static int dlpar_change_lmb_state(struct drmem_lmb *lmb, bool online)
{
	struct memory_block *mem_block;
	int rc;

	mem_block = lmb_to_memblock(lmb);
	if (!mem_block)
		return -EINVAL;

	if (online && mem_block->dev.offline)
		rc = device_online(&mem_block->dev);
	else if (!online && !mem_block->dev.offline)
		rc = device_offline(&mem_block->dev);
	else
		rc = 0;

	put_device(&mem_block->dev);

	return rc;
}

static int dlpar_online_lmb(struct drmem_lmb *lmb)
{
	return dlpar_change_lmb_state(lmb, true);
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static int dlpar_offline_lmb(struct drmem_lmb *lmb)
{
	return dlpar_change_lmb_state(lmb, false);
}

static int pseries_remove_memblock(unsigned long base, unsigned int memblock_size)
{
	unsigned long block_sz, start_pfn;
	int sections_per_block;
	int i, nid;

	start_pfn = base >> PAGE_SHIFT;

	lock_device_hotplug();

	if (!pfn_valid(start_pfn))
		goto out;

	block_sz = pseries_memory_block_size();
	sections_per_block = block_sz / MIN_MEMORY_BLOCK_SIZE;
	nid = memory_add_physaddr_to_nid(base);

	for (i = 0; i < sections_per_block; i++) {
		__remove_memory(nid, base, MIN_MEMORY_BLOCK_SIZE);
		base += MIN_MEMORY_BLOCK_SIZE;
	}

out:
	/* Update memory regions for memory remove */
	memblock_remove(base, memblock_size);
	unlock_device_hotplug();
	return 0;
}

static int pseries_remove_mem_node(struct device_node *np)
{
	const __be32 *regs;
	unsigned long base;
	unsigned int lmb_size;
	int ret = -EINVAL;

	/*
	 * Check to see if we are actually removing memory
	 */
	if (!of_node_is_type(np, "memory"))
		return 0;

	/*
	 * Find the base address and size of the memblock
	 */
	regs = of_get_property(np, "reg", NULL);
	if (!regs)
		return ret;

	base = be64_to_cpu(*(unsigned long *)regs);
	lmb_size = be32_to_cpu(regs[3]);

	pseries_remove_memblock(base, lmb_size);
	return 0;
}

static bool lmb_is_removable(struct drmem_lmb *lmb)
{
	int i, scns_per_block;
	int rc = 1;
	unsigned long pfn, block_sz;
	u64 phys_addr;

	if (!(lmb->flags & DRCONF_MEM_ASSIGNED))
		return false;

	block_sz = memory_block_size_bytes();
	scns_per_block = block_sz / MIN_MEMORY_BLOCK_SIZE;
	phys_addr = lmb->base_addr;

#ifdef CONFIG_FA_DUMP
	/*
	 * Don't hot-remove memory that falls in fadump boot memory area
	 * and memory that is reserved for capturing old kernel memory.
	 */
	if (is_fadump_memory_area(phys_addr, block_sz))
		return false;
#endif

	for (i = 0; i < scns_per_block; i++) {
		pfn = PFN_DOWN(phys_addr);
		if (!pfn_present(pfn))
			continue;

		rc &= is_mem_section_removable(pfn, PAGES_PER_SECTION);
		phys_addr += MIN_MEMORY_BLOCK_SIZE;
	}

	return rc ? true : false;
}

static int dlpar_add_lmb(struct drmem_lmb *);

static int dlpar_remove_lmb(struct drmem_lmb *lmb)
{
	unsigned long block_sz;
	int rc;

	if (!lmb_is_removable(lmb))
		return -EINVAL;

	rc = dlpar_offline_lmb(lmb);
	if (rc)
		return rc;

	block_sz = pseries_memory_block_size();

	__remove_memory(lmb->nid, lmb->base_addr, block_sz);

	/* Update memory regions for memory remove */
	memblock_remove(lmb->base_addr, block_sz);

	invalidate_lmb_associativity_index(lmb);
	lmb_clear_nid(lmb);
	lmb->flags &= ~DRCONF_MEM_ASSIGNED;

	return 0;
}

static int dlpar_memory_remove_by_count(u32 lmbs_to_remove)
{
	struct drmem_lmb *lmb;
	int lmbs_removed = 0;
	int lmbs_available = 0;
	int rc;

	pr_info("Attempting to hot-remove %d LMB(s)\n", lmbs_to_remove);

	if (lmbs_to_remove == 0)
		return -EINVAL;

	/* Validate that there are enough LMBs to satisfy the request */
	for_each_drmem_lmb(lmb) {
		if (lmb_is_removable(lmb))
			lmbs_available++;

		if (lmbs_available == lmbs_to_remove)
			break;
	}

	if (lmbs_available < lmbs_to_remove) {
		pr_info("Not enough LMBs available (%d of %d) to satisfy request\n",
			lmbs_available, lmbs_to_remove);
		return -EINVAL;
	}

	for_each_drmem_lmb(lmb) {
		rc = dlpar_remove_lmb(lmb);
		if (rc)
			continue;

		/* Mark this lmb so we can add it later if all of the
		 * requested LMBs cannot be removed.
		 */
		drmem_mark_lmb_reserved(lmb);

		lmbs_removed++;
		if (lmbs_removed == lmbs_to_remove)
			break;
	}

	if (lmbs_removed != lmbs_to_remove) {
		pr_err("Memory hot-remove failed, adding LMB's back\n");

		for_each_drmem_lmb(lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			rc = dlpar_add_lmb(lmb);
			if (rc)
				pr_err("Failed to add LMB back, drc index %x\n",
				       lmb->drc_index);

			drmem_remove_lmb_reservation(lmb);
		}

		rc = -EINVAL;
	} else {
		for_each_drmem_lmb(lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			dlpar_release_drc(lmb->drc_index);
			pr_info("Memory at %llx was hot-removed\n",
				lmb->base_addr);

			drmem_remove_lmb_reservation(lmb);
		}
		rc = 0;
	}

	return rc;
}

static int dlpar_memory_remove_by_index(u32 drc_index)
{
	struct drmem_lmb *lmb;
	int lmb_found;
	int rc;

	pr_info("Attempting to hot-remove LMB, drc index %x\n", drc_index);

	lmb_found = 0;
	for_each_drmem_lmb(lmb) {
		if (lmb->drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_remove_lmb(lmb);
			if (!rc)
				dlpar_release_drc(lmb->drc_index);

			break;
		}
	}

	if (!lmb_found)
		rc = -EINVAL;

	if (rc)
		pr_info("Failed to hot-remove memory at %llx\n",
			lmb->base_addr);
	else
		pr_info("Memory at %llx was hot-removed\n", lmb->base_addr);

	return rc;
}

static int dlpar_memory_readd_by_index(u32 drc_index)
{
	struct drmem_lmb *lmb;
	int lmb_found;
	int rc;

	pr_info("Attempting to update LMB, drc index %x\n", drc_index);

	lmb_found = 0;
	for_each_drmem_lmb(lmb) {
		if (lmb->drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_remove_lmb(lmb);
			if (!rc) {
				rc = dlpar_add_lmb(lmb);
				if (rc)
					dlpar_release_drc(lmb->drc_index);
			}
			break;
		}
	}

	if (!lmb_found)
		rc = -EINVAL;

	if (rc)
		pr_info("Failed to update memory at %llx\n",
			lmb->base_addr);
	else
		pr_info("Memory at %llx was updated\n", lmb->base_addr);

	return rc;
}

static int dlpar_memory_remove_by_ic(u32 lmbs_to_remove, u32 drc_index)
{
	struct drmem_lmb *lmb, *start_lmb, *end_lmb;
	int lmbs_available = 0;
	int rc;

	pr_info("Attempting to hot-remove %u LMB(s) at %x\n",
		lmbs_to_remove, drc_index);

	if (lmbs_to_remove == 0)
		return -EINVAL;

	rc = get_lmb_range(drc_index, lmbs_to_remove, &start_lmb, &end_lmb);
	if (rc)
		return -EINVAL;

	/* Validate that there are enough LMBs to satisfy the request */
	for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
		if (lmb->flags & DRCONF_MEM_RESERVED)
			break;

		lmbs_available++;
	}

	if (lmbs_available < lmbs_to_remove)
		return -EINVAL;

	for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
		if (!(lmb->flags & DRCONF_MEM_ASSIGNED))
			continue;

		rc = dlpar_remove_lmb(lmb);
		if (rc)
			break;

		drmem_mark_lmb_reserved(lmb);
	}

	if (rc) {
		pr_err("Memory indexed-count-remove failed, adding any removed LMBs\n");


		for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			rc = dlpar_add_lmb(lmb);
			if (rc)
				pr_err("Failed to add LMB, drc index %x\n",
				       lmb->drc_index);

			drmem_remove_lmb_reservation(lmb);
		}
		rc = -EINVAL;
	} else {
		for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			dlpar_release_drc(lmb->drc_index);
			pr_info("Memory at %llx (drc index %x) was hot-removed\n",
				lmb->base_addr, lmb->drc_index);

			drmem_remove_lmb_reservation(lmb);
		}
	}

	return rc;
}

#else
static inline int pseries_remove_memblock(unsigned long base,
					  unsigned int memblock_size)
{
	return -EOPNOTSUPP;
}
static inline int pseries_remove_mem_node(struct device_node *np)
{
	return 0;
}
static inline int dlpar_memory_remove(struct pseries_hp_errorlog *hp_elog)
{
	return -EOPNOTSUPP;
}
static int dlpar_remove_lmb(struct drmem_lmb *lmb)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_remove_by_count(u32 lmbs_to_remove)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_remove_by_index(u32 drc_index)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_readd_by_index(u32 drc_index)
{
	return -EOPNOTSUPP;
}

static int dlpar_memory_remove_by_ic(u32 lmbs_to_remove, u32 drc_index)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static int dlpar_add_lmb(struct drmem_lmb *lmb)
{
	unsigned long block_sz;
	int rc;

	if (lmb->flags & DRCONF_MEM_ASSIGNED)
		return -EINVAL;

	rc = update_lmb_associativity_index(lmb);
	if (rc) {
		dlpar_release_drc(lmb->drc_index);
		return rc;
	}

	lmb_set_nid(lmb);
	block_sz = memory_block_size_bytes();

	/* Add the memory */
	rc = __add_memory(lmb->nid, lmb->base_addr, block_sz);
	if (rc) {
		invalidate_lmb_associativity_index(lmb);
		return rc;
	}

	rc = dlpar_online_lmb(lmb);
	if (rc) {
		__remove_memory(lmb->nid, lmb->base_addr, block_sz);
		invalidate_lmb_associativity_index(lmb);
		lmb_clear_nid(lmb);
	} else {
		lmb->flags |= DRCONF_MEM_ASSIGNED;
	}

	return rc;
}

static int dlpar_memory_add_by_count(u32 lmbs_to_add)
{
	struct drmem_lmb *lmb;
	int lmbs_available = 0;
	int lmbs_added = 0;
	int rc;

	pr_info("Attempting to hot-add %d LMB(s)\n", lmbs_to_add);

	if (lmbs_to_add == 0)
		return -EINVAL;

	/* Validate that there are enough LMBs to satisfy the request */
	for_each_drmem_lmb(lmb) {
		if (!(lmb->flags & DRCONF_MEM_ASSIGNED))
			lmbs_available++;

		if (lmbs_available == lmbs_to_add)
			break;
	}

	if (lmbs_available < lmbs_to_add)
		return -EINVAL;

	for_each_drmem_lmb(lmb) {
		if (lmb->flags & DRCONF_MEM_ASSIGNED)
			continue;

		rc = dlpar_acquire_drc(lmb->drc_index);
		if (rc)
			continue;

		rc = dlpar_add_lmb(lmb);
		if (rc) {
			dlpar_release_drc(lmb->drc_index);
			continue;
		}

		/* Mark this lmb so we can remove it later if all of the
		 * requested LMBs cannot be added.
		 */
		drmem_mark_lmb_reserved(lmb);

		lmbs_added++;
		if (lmbs_added == lmbs_to_add)
			break;
	}

	if (lmbs_added != lmbs_to_add) {
		pr_err("Memory hot-add failed, removing any added LMBs\n");

		for_each_drmem_lmb(lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			rc = dlpar_remove_lmb(lmb);
			if (rc)
				pr_err("Failed to remove LMB, drc index %x\n",
				       lmb->drc_index);
			else
				dlpar_release_drc(lmb->drc_index);

			drmem_remove_lmb_reservation(lmb);
		}
		rc = -EINVAL;
	} else {
		for_each_drmem_lmb(lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			pr_info("Memory at %llx (drc index %x) was hot-added\n",
				lmb->base_addr, lmb->drc_index);
			drmem_remove_lmb_reservation(lmb);
		}
		rc = 0;
	}

	return rc;
}

static int dlpar_memory_add_by_index(u32 drc_index)
{
	struct drmem_lmb *lmb;
	int rc, lmb_found;

	pr_info("Attempting to hot-add LMB, drc index %x\n", drc_index);

	lmb_found = 0;
	for_each_drmem_lmb(lmb) {
		if (lmb->drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_acquire_drc(lmb->drc_index);
			if (!rc) {
				rc = dlpar_add_lmb(lmb);
				if (rc)
					dlpar_release_drc(lmb->drc_index);
			}

			break;
		}
	}

	if (!lmb_found)
		rc = -EINVAL;

	if (rc)
		pr_info("Failed to hot-add memory, drc index %x\n", drc_index);
	else
		pr_info("Memory at %llx (drc index %x) was hot-added\n",
			lmb->base_addr, drc_index);

	return rc;
}

static int dlpar_memory_add_by_ic(u32 lmbs_to_add, u32 drc_index)
{
	struct drmem_lmb *lmb, *start_lmb, *end_lmb;
	int lmbs_available = 0;
	int rc;

	pr_info("Attempting to hot-add %u LMB(s) at index %x\n",
		lmbs_to_add, drc_index);

	if (lmbs_to_add == 0)
		return -EINVAL;

	rc = get_lmb_range(drc_index, lmbs_to_add, &start_lmb, &end_lmb);
	if (rc)
		return -EINVAL;

	/* Validate that the LMBs in this range are not reserved */
	for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
		if (lmb->flags & DRCONF_MEM_RESERVED)
			break;

		lmbs_available++;
	}

	if (lmbs_available < lmbs_to_add)
		return -EINVAL;

	for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
		if (lmb->flags & DRCONF_MEM_ASSIGNED)
			continue;

		rc = dlpar_acquire_drc(lmb->drc_index);
		if (rc)
			break;

		rc = dlpar_add_lmb(lmb);
		if (rc) {
			dlpar_release_drc(lmb->drc_index);
			break;
		}

		drmem_mark_lmb_reserved(lmb);
	}

	if (rc) {
		pr_err("Memory indexed-count-add failed, removing any added LMBs\n");

		for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			rc = dlpar_remove_lmb(lmb);
			if (rc)
				pr_err("Failed to remove LMB, drc index %x\n",
				       lmb->drc_index);
			else
				dlpar_release_drc(lmb->drc_index);

			drmem_remove_lmb_reservation(lmb);
		}
		rc = -EINVAL;
	} else {
		for_each_drmem_lmb_in_range(lmb, start_lmb, end_lmb) {
			if (!drmem_lmb_reserved(lmb))
				continue;

			pr_info("Memory at %llx (drc index %x) was hot-added\n",
				lmb->base_addr, lmb->drc_index);
			drmem_remove_lmb_reservation(lmb);
		}
	}

	return rc;
}

int dlpar_memory(struct pseries_hp_errorlog *hp_elog)
{
	u32 count, drc_index;
	int rc;

	lock_device_hotplug();

	switch (hp_elog->action) {
	case PSERIES_HP_ELOG_ACTION_ADD:
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT) {
			count = hp_elog->_drc_u.drc_count;
			rc = dlpar_memory_add_by_count(count);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX) {
			drc_index = hp_elog->_drc_u.drc_index;
			rc = dlpar_memory_add_by_index(drc_index);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_IC) {
			count = hp_elog->_drc_u.ic.count;
			drc_index = hp_elog->_drc_u.ic.index;
			rc = dlpar_memory_add_by_ic(count, drc_index);
		} else {
			rc = -EINVAL;
		}

		break;
	case PSERIES_HP_ELOG_ACTION_REMOVE:
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT) {
			count = hp_elog->_drc_u.drc_count;
			rc = dlpar_memory_remove_by_count(count);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX) {
			drc_index = hp_elog->_drc_u.drc_index;
			rc = dlpar_memory_remove_by_index(drc_index);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_IC) {
			count = hp_elog->_drc_u.ic.count;
			drc_index = hp_elog->_drc_u.ic.index;
			rc = dlpar_memory_remove_by_ic(count, drc_index);
		} else {
			rc = -EINVAL;
		}

		break;
	case PSERIES_HP_ELOG_ACTION_READD:
		drc_index = hp_elog->_drc_u.drc_index;
		rc = dlpar_memory_readd_by_index(drc_index);
		break;
	default:
		pr_err("Invalid action (%d) specified\n", hp_elog->action);
		rc = -EINVAL;
		break;
	}

	if (!rc) {
		rtas_hp_event = true;
		rc = drmem_update_dt();
		rtas_hp_event = false;
	}

	unlock_device_hotplug();
	return rc;
}

static int pseries_add_mem_node(struct device_node *np)
{
	const __be32 *regs;
	unsigned long base;
	unsigned int lmb_size;
	int ret = -EINVAL;

	/*
	 * Check to see if we are actually adding memory
	 */
	if (!of_node_is_type(np, "memory"))
		return 0;

	/*
	 * Find the base and size of the memblock
	 */
	regs = of_get_property(np, "reg", NULL);
	if (!regs)
		return ret;

	base = be64_to_cpu(*(unsigned long *)regs);
	lmb_size = be32_to_cpu(regs[3]);

	/*
	 * Update memory region to represent the memory add
	 */
	ret = memblock_add(base, lmb_size);
	return (ret < 0) ? -EINVAL : 0;
}

static int pseries_update_drconf_memory(struct of_reconfig_data *pr)
{
	struct of_drconf_cell_v1 *new_drmem, *old_drmem;
	unsigned long memblock_size;
	u32 entries;
	__be32 *p;
	int i, rc = -EINVAL;

	if (rtas_hp_event)
		return 0;

	memblock_size = pseries_memory_block_size();
	if (!memblock_size)
		return -EINVAL;

	if (!pr->old_prop)
		return 0;

	p = (__be32 *) pr->old_prop->value;
	if (!p)
		return -EINVAL;

	/* The first int of the property is the number of lmb's described
	 * by the property. This is followed by an array of of_drconf_cell
	 * entries. Get the number of entries and skip to the array of
	 * of_drconf_cell's.
	 */
	entries = be32_to_cpu(*p++);
	old_drmem = (struct of_drconf_cell_v1 *)p;

	p = (__be32 *)pr->prop->value;
	p++;
	new_drmem = (struct of_drconf_cell_v1 *)p;

	for (i = 0; i < entries; i++) {
		if ((be32_to_cpu(old_drmem[i].flags) & DRCONF_MEM_ASSIGNED) &&
		    (!(be32_to_cpu(new_drmem[i].flags) & DRCONF_MEM_ASSIGNED))) {
			rc = pseries_remove_memblock(
				be64_to_cpu(old_drmem[i].base_addr),
						     memblock_size);
			break;
		} else if ((!(be32_to_cpu(old_drmem[i].flags) &
			    DRCONF_MEM_ASSIGNED)) &&
			    (be32_to_cpu(new_drmem[i].flags) &
			    DRCONF_MEM_ASSIGNED)) {
			rc = memblock_add(be64_to_cpu(old_drmem[i].base_addr),
					  memblock_size);
			rc = (rc < 0) ? -EINVAL : 0;
			break;
		}
	}
	return rc;
}

static int pseries_memory_notifier(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct of_reconfig_data *rd = data;
	int err = 0;

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
		err = pseries_add_mem_node(rd->dn);
		break;
	case OF_RECONFIG_DETACH_NODE:
		err = pseries_remove_mem_node(rd->dn);
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		if (!strcmp(rd->prop->name, "ibm,dynamic-memory"))
			err = pseries_update_drconf_memory(rd);
		break;
	}
	return notifier_from_errno(err);
}

static struct notifier_block pseries_mem_nb = {
	.notifier_call = pseries_memory_notifier,
};

static int __init pseries_memory_hotplug_init(void)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		of_reconfig_notifier_register(&pseries_mem_nb);

	return 0;
}
machine_device_initcall(pseries, pseries_memory_hotplug_init);
