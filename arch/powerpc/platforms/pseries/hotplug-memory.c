/*
 * pseries Memory Hotplug infrastructure.
 *
 * Copyright (C) 2008 Badari Pulavarty, IBM Corporation
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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

static struct property *dlpar_clone_drconf_property(struct device_node *dn)
{
	struct property *prop, *new_prop;
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int i;

	prop = of_find_property(dn, "ibm,dynamic-memory", NULL);
	if (!prop)
		return NULL;

	new_prop = dlpar_clone_property(prop, prop->length);
	if (!new_prop)
		return NULL;

	/* Convert the property to cpu endian-ness */
	p = new_prop->value;
	*p = be32_to_cpu(*p);

	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	for (i = 0; i < num_lmbs; i++) {
		lmbs[i].base_addr = be64_to_cpu(lmbs[i].base_addr);
		lmbs[i].drc_index = be32_to_cpu(lmbs[i].drc_index);
		lmbs[i].aa_index = be32_to_cpu(lmbs[i].aa_index);
		lmbs[i].flags = be32_to_cpu(lmbs[i].flags);
	}

	return new_prop;
}

static void dlpar_update_drconf_property(struct device_node *dn,
					 struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int i;

	/* Convert the property back to BE */
	p = prop->value;
	num_lmbs = *p;
	*p = cpu_to_be32(*p);
	p++;

	lmbs = (struct of_drconf_cell *)p;
	for (i = 0; i < num_lmbs; i++) {
		lmbs[i].base_addr = cpu_to_be64(lmbs[i].base_addr);
		lmbs[i].drc_index = cpu_to_be32(lmbs[i].drc_index);
		lmbs[i].aa_index = cpu_to_be32(lmbs[i].aa_index);
		lmbs[i].flags = cpu_to_be32(lmbs[i].flags);
	}

	rtas_hp_event = true;
	of_update_property(dn, prop);
	rtas_hp_event = false;
}

static int dlpar_update_device_tree_lmb(struct of_drconf_cell *lmb)
{
	struct device_node *dn;
	struct property *prop;
	struct of_drconf_cell *lmbs;
	u32 *p, num_lmbs;
	int i;

	dn = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!dn)
		return -ENODEV;

	prop = dlpar_clone_drconf_property(dn);
	if (!prop) {
		of_node_put(dn);
		return -ENODEV;
	}

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	for (i = 0; i < num_lmbs; i++) {
		if (lmbs[i].drc_index == lmb->drc_index) {
			lmbs[i].flags = lmb->flags;
			lmbs[i].aa_index = lmb->aa_index;

			dlpar_update_drconf_property(dn, prop);
			break;
		}
	}

	of_node_put(dn);
	return 0;
}

static u32 find_aa_index(struct device_node *dr_node,
			 struct property *ala_prop, const u32 *lmb_assoc)
{
	u32 *assoc_arrays;
	u32 aa_index;
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

	aa_index = -1;
	for (i = 0; i < aa_arrays; i++) {
		index = (i * aa_array_entries) + 2;

		if (memcmp(&assoc_arrays[index], &lmb_assoc[1], aa_array_sz))
			continue;

		aa_index = i;
		break;
	}

	if (aa_index == -1) {
		struct property *new_prop;
		u32 new_prop_size;

		new_prop_size = ala_prop->length + aa_array_sz;
		new_prop = dlpar_clone_property(ala_prop, new_prop_size);
		if (!new_prop)
			return -1;

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
		aa_index = be32_to_cpu(assoc_arrays[0]) - 1;
	}

	return aa_index;
}

static u32 lookup_lmb_associativity_index(struct of_drconf_cell *lmb)
{
	struct device_node *parent, *lmb_node, *dr_node;
	struct property *ala_prop;
	const u32 *lmb_assoc;
	u32 aa_index;

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

	aa_index = find_aa_index(dr_node, ala_prop, lmb_assoc);

	dlpar_free_cc_nodes(lmb_node);
	return aa_index;
}

static int dlpar_add_device_tree_lmb(struct of_drconf_cell *lmb)
{
	int aa_index;

	lmb->flags |= DRCONF_MEM_ASSIGNED;

	aa_index = lookup_lmb_associativity_index(lmb);
	if (aa_index < 0) {
		pr_err("Couldn't find associativity index for drc index %x\n",
		       lmb->drc_index);
		return aa_index;
	}

	lmb->aa_index = aa_index;
	return dlpar_update_device_tree_lmb(lmb);
}

static int dlpar_remove_device_tree_lmb(struct of_drconf_cell *lmb)
{
	lmb->flags &= ~DRCONF_MEM_ASSIGNED;
	lmb->aa_index = 0xffffffff;
	return dlpar_update_device_tree_lmb(lmb);
}

static struct memory_block *lmb_to_memblock(struct of_drconf_cell *lmb)
{
	unsigned long section_nr;
	struct mem_section *mem_sect;
	struct memory_block *mem_block;

	section_nr = pfn_to_section_nr(PFN_DOWN(lmb->base_addr));
	mem_sect = __nr_to_section(section_nr);

	mem_block = find_memory_block(mem_sect);
	return mem_block;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
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
		remove_memory(nid, base, MIN_MEMORY_BLOCK_SIZE);
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
	const char *type;
	const __be32 *regs;
	unsigned long base;
	unsigned int lmb_size;
	int ret = -EINVAL;

	/*
	 * Check to see if we are actually removing memory
	 */
	type = of_get_property(np, "device_type", NULL);
	if (type == NULL || strcmp(type, "memory") != 0)
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

static bool lmb_is_removable(struct of_drconf_cell *lmb)
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
	/* Don't hot-remove memory that falls in fadump boot memory area */
	if (is_fadump_boot_memory_area(phys_addr, block_sz))
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

static int dlpar_add_lmb(struct of_drconf_cell *);

static int dlpar_remove_lmb(struct of_drconf_cell *lmb)
{
	struct memory_block *mem_block;
	unsigned long block_sz;
	int nid, rc;

	if (!lmb_is_removable(lmb))
		return -EINVAL;

	mem_block = lmb_to_memblock(lmb);
	if (!mem_block)
		return -EINVAL;

	rc = device_offline(&mem_block->dev);
	put_device(&mem_block->dev);
	if (rc)
		return rc;

	block_sz = pseries_memory_block_size();
	nid = memory_add_physaddr_to_nid(lmb->base_addr);

	remove_memory(nid, lmb->base_addr, block_sz);

	/* Update memory regions for memory remove */
	memblock_remove(lmb->base_addr, block_sz);

	dlpar_remove_device_tree_lmb(lmb);
	return 0;
}

static int dlpar_memory_remove_by_count(u32 lmbs_to_remove,
					struct property *prop)
{
	struct of_drconf_cell *lmbs;
	int lmbs_removed = 0;
	int lmbs_available = 0;
	u32 num_lmbs, *p;
	int i, rc;

	pr_info("Attempting to hot-remove %d LMB(s)\n", lmbs_to_remove);

	if (lmbs_to_remove == 0)
		return -EINVAL;

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	/* Validate that there are enough LMBs to satisfy the request */
	for (i = 0; i < num_lmbs; i++) {
		if (lmb_is_removable(&lmbs[i]))
			lmbs_available++;
	}

	if (lmbs_available < lmbs_to_remove) {
		pr_info("Not enough LMBs available (%d of %d) to satisfy request\n",
			lmbs_available, lmbs_to_remove);
		return -EINVAL;
	}

	for (i = 0; i < num_lmbs && lmbs_removed < lmbs_to_remove; i++) {
		rc = dlpar_remove_lmb(&lmbs[i]);
		if (rc)
			continue;

		lmbs_removed++;

		/* Mark this lmb so we can add it later if all of the
		 * requested LMBs cannot be removed.
		 */
		lmbs[i].reserved = 1;
	}

	if (lmbs_removed != lmbs_to_remove) {
		pr_err("Memory hot-remove failed, adding LMB's back\n");

		for (i = 0; i < num_lmbs; i++) {
			if (!lmbs[i].reserved)
				continue;

			rc = dlpar_add_lmb(&lmbs[i]);
			if (rc)
				pr_err("Failed to add LMB back, drc index %x\n",
				       lmbs[i].drc_index);

			lmbs[i].reserved = 0;
		}

		rc = -EINVAL;
	} else {
		for (i = 0; i < num_lmbs; i++) {
			if (!lmbs[i].reserved)
				continue;

			dlpar_release_drc(lmbs[i].drc_index);
			pr_info("Memory at %llx was hot-removed\n",
				lmbs[i].base_addr);

			lmbs[i].reserved = 0;
		}
		rc = 0;
	}

	return rc;
}

static int dlpar_memory_remove_by_index(u32 drc_index, struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int lmb_found;
	int i, rc;

	pr_info("Attempting to hot-remove LMB, drc index %x\n", drc_index);

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	lmb_found = 0;
	for (i = 0; i < num_lmbs; i++) {
		if (lmbs[i].drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_remove_lmb(&lmbs[i]);
			if (!rc)
				dlpar_release_drc(lmbs[i].drc_index);

			break;
		}
	}

	if (!lmb_found)
		rc = -EINVAL;

	if (rc)
		pr_info("Failed to hot-remove memory at %llx\n",
			lmbs[i].base_addr);
	else
		pr_info("Memory at %llx was hot-removed\n", lmbs[i].base_addr);

	return rc;
}

static int dlpar_memory_readd_by_index(u32 drc_index, struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int lmb_found;
	int i, rc;

	pr_info("Attempting to update LMB, drc index %x\n", drc_index);

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	lmb_found = 0;
	for (i = 0; i < num_lmbs; i++) {
		if (lmbs[i].drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_remove_lmb(&lmbs[i]);
			if (!rc) {
				rc = dlpar_add_lmb(&lmbs[i]);
				if (rc)
					dlpar_release_drc(lmbs[i].drc_index);
			}
			break;
		}
	}

	if (!lmb_found)
		rc = -EINVAL;

	if (rc)
		pr_info("Failed to update memory at %llx\n",
			lmbs[i].base_addr);
	else
		pr_info("Memory at %llx was updated\n", lmbs[i].base_addr);

	return rc;
}

static int dlpar_memory_remove_by_ic(u32 lmbs_to_remove, u32 drc_index,
				     struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int i, rc, start_lmb_found;
	int lmbs_available = 0, start_index = 0, end_index;

	pr_info("Attempting to hot-remove %u LMB(s) at %x\n",
		lmbs_to_remove, drc_index);

	if (lmbs_to_remove == 0)
		return -EINVAL;

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;
	start_lmb_found = 0;

	/* Navigate to drc_index */
	while (start_index < num_lmbs) {
		if (lmbs[start_index].drc_index == drc_index) {
			start_lmb_found = 1;
			break;
		}

		start_index++;
	}

	if (!start_lmb_found)
		return -EINVAL;

	end_index = start_index + lmbs_to_remove;

	/* Validate that there are enough LMBs to satisfy the request */
	for (i = start_index; i < end_index; i++) {
		if (lmbs[i].flags & DRCONF_MEM_RESERVED)
			break;

		lmbs_available++;
	}

	if (lmbs_available < lmbs_to_remove)
		return -EINVAL;

	for (i = start_index; i < end_index; i++) {
		if (!(lmbs[i].flags & DRCONF_MEM_ASSIGNED))
			continue;

		rc = dlpar_remove_lmb(&lmbs[i]);
		if (rc)
			break;

		lmbs[i].reserved = 1;
	}

	if (rc) {
		pr_err("Memory indexed-count-remove failed, adding any removed LMBs\n");

		for (i = start_index; i < end_index; i++) {
			if (!lmbs[i].reserved)
				continue;

			rc = dlpar_add_lmb(&lmbs[i]);
			if (rc)
				pr_err("Failed to add LMB, drc index %x\n",
				       be32_to_cpu(lmbs[i].drc_index));

			lmbs[i].reserved = 0;
		}
		rc = -EINVAL;
	} else {
		for (i = start_index; i < end_index; i++) {
			if (!lmbs[i].reserved)
				continue;

			dlpar_release_drc(lmbs[i].drc_index);
			pr_info("Memory at %llx (drc index %x) was hot-removed\n",
				lmbs[i].base_addr, lmbs[i].drc_index);

			lmbs[i].reserved = 0;
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
static int dlpar_remove_lmb(struct of_drconf_cell *lmb)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_remove_by_count(u32 lmbs_to_remove,
					struct property *prop)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_remove_by_index(u32 drc_index, struct property *prop)
{
	return -EOPNOTSUPP;
}
static int dlpar_memory_readd_by_index(u32 drc_index, struct property *prop)
{
	return -EOPNOTSUPP;
}

static int dlpar_memory_remove_by_ic(u32 lmbs_to_remove, u32 drc_index,
				     struct property *prop)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static int dlpar_online_lmb(struct of_drconf_cell *lmb)
{
	struct memory_block *mem_block;
	int rc;

	mem_block = lmb_to_memblock(lmb);
	if (!mem_block)
		return -EINVAL;

	rc = device_online(&mem_block->dev);
	put_device(&mem_block->dev);
	return rc;
}

static int dlpar_add_lmb(struct of_drconf_cell *lmb)
{
	unsigned long block_sz;
	int nid, rc;

	if (lmb->flags & DRCONF_MEM_ASSIGNED)
		return -EINVAL;

	rc = dlpar_add_device_tree_lmb(lmb);
	if (rc) {
		pr_err("Couldn't update device tree for drc index %x\n",
		       lmb->drc_index);
		dlpar_release_drc(lmb->drc_index);
		return rc;
	}

	block_sz = memory_block_size_bytes();

	/* Find the node id for this address */
	nid = memory_add_physaddr_to_nid(lmb->base_addr);

	/* Add the memory */
	rc = add_memory(nid, lmb->base_addr, block_sz);
	if (rc) {
		dlpar_remove_device_tree_lmb(lmb);
		return rc;
	}

	rc = dlpar_online_lmb(lmb);
	if (rc) {
		remove_memory(nid, lmb->base_addr, block_sz);
		dlpar_remove_device_tree_lmb(lmb);
	} else {
		lmb->flags |= DRCONF_MEM_ASSIGNED;
	}

	return rc;
}

static int dlpar_memory_add_by_count(u32 lmbs_to_add, struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int lmbs_available = 0;
	int lmbs_added = 0;
	int i, rc;

	pr_info("Attempting to hot-add %d LMB(s)\n", lmbs_to_add);

	if (lmbs_to_add == 0)
		return -EINVAL;

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	/* Validate that there are enough LMBs to satisfy the request */
	for (i = 0; i < num_lmbs; i++) {
		if (!(lmbs[i].flags & DRCONF_MEM_ASSIGNED))
			lmbs_available++;
	}

	if (lmbs_available < lmbs_to_add)
		return -EINVAL;

	for (i = 0; i < num_lmbs && lmbs_to_add != lmbs_added; i++) {
		rc = dlpar_acquire_drc(lmbs[i].drc_index);
		if (rc)
			continue;

		rc = dlpar_add_lmb(&lmbs[i]);
		if (rc) {
			dlpar_release_drc(lmbs[i].drc_index);
			continue;
		}

		lmbs_added++;

		/* Mark this lmb so we can remove it later if all of the
		 * requested LMBs cannot be added.
		 */
		lmbs[i].reserved = 1;
	}

	if (lmbs_added != lmbs_to_add) {
		pr_err("Memory hot-add failed, removing any added LMBs\n");

		for (i = 0; i < num_lmbs; i++) {
			if (!lmbs[i].reserved)
				continue;

			rc = dlpar_remove_lmb(&lmbs[i]);
			if (rc)
				pr_err("Failed to remove LMB, drc index %x\n",
				       be32_to_cpu(lmbs[i].drc_index));
			else
				dlpar_release_drc(lmbs[i].drc_index);
		}
		rc = -EINVAL;
	} else {
		for (i = 0; i < num_lmbs; i++) {
			if (!lmbs[i].reserved)
				continue;

			pr_info("Memory at %llx (drc index %x) was hot-added\n",
				lmbs[i].base_addr, lmbs[i].drc_index);
			lmbs[i].reserved = 0;
		}
	}

	return rc;
}

static int dlpar_memory_add_by_index(u32 drc_index, struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int i, lmb_found;
	int rc;

	pr_info("Attempting to hot-add LMB, drc index %x\n", drc_index);

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;

	lmb_found = 0;
	for (i = 0; i < num_lmbs; i++) {
		if (lmbs[i].drc_index == drc_index) {
			lmb_found = 1;
			rc = dlpar_acquire_drc(lmbs[i].drc_index);
			if (!rc) {
				rc = dlpar_add_lmb(&lmbs[i]);
				if (rc)
					dlpar_release_drc(lmbs[i].drc_index);
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
			lmbs[i].base_addr, drc_index);

	return rc;
}

static int dlpar_memory_add_by_ic(u32 lmbs_to_add, u32 drc_index,
				  struct property *prop)
{
	struct of_drconf_cell *lmbs;
	u32 num_lmbs, *p;
	int i, rc, start_lmb_found;
	int lmbs_available = 0, start_index = 0, end_index;

	pr_info("Attempting to hot-add %u LMB(s) at index %x\n",
		lmbs_to_add, drc_index);

	if (lmbs_to_add == 0)
		return -EINVAL;

	p = prop->value;
	num_lmbs = *p++;
	lmbs = (struct of_drconf_cell *)p;
	start_lmb_found = 0;

	/* Navigate to drc_index */
	while (start_index < num_lmbs) {
		if (lmbs[start_index].drc_index == drc_index) {
			start_lmb_found = 1;
			break;
		}

		start_index++;
	}

	if (!start_lmb_found)
		return -EINVAL;

	end_index = start_index + lmbs_to_add;

	/* Validate that the LMBs in this range are not reserved */
	for (i = start_index; i < end_index; i++) {
		if (lmbs[i].flags & DRCONF_MEM_RESERVED)
			break;

		lmbs_available++;
	}

	if (lmbs_available < lmbs_to_add)
		return -EINVAL;

	for (i = start_index; i < end_index; i++) {
		if (lmbs[i].flags & DRCONF_MEM_ASSIGNED)
			continue;

		rc = dlpar_acquire_drc(lmbs[i].drc_index);
		if (rc)
			break;

		rc = dlpar_add_lmb(&lmbs[i]);
		if (rc) {
			dlpar_release_drc(lmbs[i].drc_index);
			break;
		}

		lmbs[i].reserved = 1;
	}

	if (rc) {
		pr_err("Memory indexed-count-add failed, removing any added LMBs\n");

		for (i = start_index; i < end_index; i++) {
			if (!lmbs[i].reserved)
				continue;

			rc = dlpar_remove_lmb(&lmbs[i]);
			if (rc)
				pr_err("Failed to remove LMB, drc index %x\n",
				       be32_to_cpu(lmbs[i].drc_index));
			else
				dlpar_release_drc(lmbs[i].drc_index);
		}
		rc = -EINVAL;
	} else {
		for (i = start_index; i < end_index; i++) {
			if (!lmbs[i].reserved)
				continue;

			pr_info("Memory at %llx (drc index %x) was hot-added\n",
				lmbs[i].base_addr, lmbs[i].drc_index);
			lmbs[i].reserved = 0;
		}
	}

	return rc;
}

int dlpar_memory(struct pseries_hp_errorlog *hp_elog)
{
	struct device_node *dn;
	struct property *prop;
	u32 count, drc_index;
	int rc;

	lock_device_hotplug();

	dn = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!dn) {
		rc = -EINVAL;
		goto dlpar_memory_out;
	}

	prop = dlpar_clone_drconf_property(dn);
	if (!prop) {
		rc = -EINVAL;
		goto dlpar_memory_out;
	}

	switch (hp_elog->action) {
	case PSERIES_HP_ELOG_ACTION_ADD:
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT) {
			count = hp_elog->_drc_u.drc_count;
			rc = dlpar_memory_add_by_count(count, prop);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX) {
			drc_index = hp_elog->_drc_u.drc_index;
			rc = dlpar_memory_add_by_index(drc_index, prop);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_IC) {
			count = hp_elog->_drc_u.ic.count;
			drc_index = hp_elog->_drc_u.ic.index;
			rc = dlpar_memory_add_by_ic(count, drc_index, prop);
		} else {
			rc = -EINVAL;
		}

		break;
	case PSERIES_HP_ELOG_ACTION_REMOVE:
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT) {
			count = hp_elog->_drc_u.drc_count;
			rc = dlpar_memory_remove_by_count(count, prop);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX) {
			drc_index = hp_elog->_drc_u.drc_index;
			rc = dlpar_memory_remove_by_index(drc_index, prop);
		} else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_IC) {
			count = hp_elog->_drc_u.ic.count;
			drc_index = hp_elog->_drc_u.ic.index;
			rc = dlpar_memory_remove_by_ic(count, drc_index, prop);
		} else {
			rc = -EINVAL;
		}

		break;
	case PSERIES_HP_ELOG_ACTION_READD:
		drc_index = hp_elog->_drc_u.drc_index;
		rc = dlpar_memory_readd_by_index(drc_index, prop);
		break;
	default:
		pr_err("Invalid action (%d) specified\n", hp_elog->action);
		rc = -EINVAL;
		break;
	}

	dlpar_free_property(prop);

dlpar_memory_out:
	of_node_put(dn);
	unlock_device_hotplug();
	return rc;
}

static int pseries_add_mem_node(struct device_node *np)
{
	const char *type;
	const __be32 *regs;
	unsigned long base;
	unsigned int lmb_size;
	int ret = -EINVAL;

	/*
	 * Check to see if we are actually adding memory
	 */
	type = of_get_property(np, "device_type", NULL);
	if (type == NULL || strcmp(type, "memory") != 0)
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
	struct of_drconf_cell *new_drmem, *old_drmem;
	unsigned long memblock_size;
	u32 entries;
	__be32 *p;
	int i, rc = -EINVAL;

	if (rtas_hp_event)
		return 0;

	memblock_size = pseries_memory_block_size();
	if (!memblock_size)
		return -EINVAL;

	p = (__be32 *) pr->old_prop->value;
	if (!p)
		return -EINVAL;

	/* The first int of the property is the number of lmb's described
	 * by the property. This is followed by an array of of_drconf_cell
	 * entries. Get the number of entries and skip to the array of
	 * of_drconf_cell's.
	 */
	entries = be32_to_cpu(*p++);
	old_drmem = (struct of_drconf_cell *)p;

	p = (__be32 *)pr->prop->value;
	p++;
	new_drmem = (struct of_drconf_cell *)p;

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
