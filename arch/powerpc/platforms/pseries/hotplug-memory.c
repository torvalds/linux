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

static void dlpar_free_drconf_property(struct property *prop)
{
	kfree(prop->name);
	kfree(prop->value);
	kfree(prop);
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

	new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
	if (!new_prop)
		return NULL;

	new_prop->name = kstrdup(prop->name, GFP_KERNEL);
	new_prop->value = kmemdup(prop->value, prop->length, GFP_KERNEL);
	if (!new_prop->name || !new_prop->value) {
		dlpar_free_drconf_property(new_prop);
		return NULL;
	}

	new_prop->length = prop->length;

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

	dlpar_release_drc(lmb->drc_index);

	lmb->flags &= ~DRCONF_MEM_ASSIGNED;
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
		if (lmbs[i].flags & DRCONF_MEM_ASSIGNED)
			lmbs_available++;
	}

	if (lmbs_available < lmbs_to_remove)
		return -EINVAL;

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

#endif /* CONFIG_MEMORY_HOTREMOVE */

static int dlpar_add_lmb(struct of_drconf_cell *lmb)
{
	struct memory_block *mem_block;
	unsigned long block_sz;
	int nid, rc;

	if (lmb->flags & DRCONF_MEM_ASSIGNED)
		return -EINVAL;

	block_sz = memory_block_size_bytes();

	rc = dlpar_acquire_drc(lmb->drc_index);
	if (rc)
		return rc;

	/* Find the node id for this address */
	nid = memory_add_physaddr_to_nid(lmb->base_addr);

	/* Add the memory */
	rc = add_memory(nid, lmb->base_addr, block_sz);
	if (rc) {
		dlpar_release_drc(lmb->drc_index);
		return rc;
	}

	/* Register this block of memory */
	rc = memblock_add(lmb->base_addr, block_sz);
	if (rc) {
		remove_memory(nid, lmb->base_addr, block_sz);
		dlpar_release_drc(lmb->drc_index);
		return rc;
	}

	mem_block = lmb_to_memblock(lmb);
	if (!mem_block) {
		remove_memory(nid, lmb->base_addr, block_sz);
		dlpar_release_drc(lmb->drc_index);
		return -EINVAL;
	}

	rc = device_online(&mem_block->dev);
	put_device(&mem_block->dev);
	if (rc) {
		remove_memory(nid, lmb->base_addr, block_sz);
		dlpar_release_drc(lmb->drc_index);
		return rc;
	}

	lmb->flags |= DRCONF_MEM_ASSIGNED;
	return 0;
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
		rc = dlpar_add_lmb(&lmbs[i]);
		if (rc)
			continue;

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
			rc = dlpar_add_lmb(&lmbs[i]);
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

int dlpar_memory(struct pseries_hp_errorlog *hp_elog)
{
	struct device_node *dn;
	struct property *prop;
	u32 count, drc_index;
	int rc;

	count = hp_elog->_drc_u.drc_count;
	drc_index = hp_elog->_drc_u.drc_index;

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
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT)
			rc = dlpar_memory_add_by_count(count, prop);
		else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX)
			rc = dlpar_memory_add_by_index(drc_index, prop);
		else
			rc = -EINVAL;
		break;
	case PSERIES_HP_ELOG_ACTION_REMOVE:
		if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_COUNT)
			rc = dlpar_memory_remove_by_count(count, prop);
		else if (hp_elog->id_type == PSERIES_HP_ELOG_ID_DRC_INDEX)
			rc = dlpar_memory_remove_by_index(drc_index, prop);
		else
			rc = -EINVAL;
		break;
	default:
		pr_err("Invalid action (%d) specified\n", hp_elog->action);
		rc = -EINVAL;
		break;
	}

	if (rc)
		dlpar_free_drconf_property(prop);
	else
		dlpar_update_drconf_property(dn, prop);

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
