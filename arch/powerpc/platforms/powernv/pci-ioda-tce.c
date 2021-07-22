// SPDX-License-Identifier: GPL-2.0+
/*
 * TCE helpers for IODA PCI/PCIe on PowerNV platforms
 *
 * Copyright 2018 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/iommu.h>

#include <asm/iommu.h>
#include <asm/tce.h>
#include "pci.h"

unsigned long pnv_ioda_parse_tce_sizes(struct pnv_phb *phb)
{
	struct pci_controller *hose = phb->hose;
	struct device_node *dn = hose->dn;
	unsigned long mask = 0;
	int i, rc, count;
	u32 val;

	count = of_property_count_u32_elems(dn, "ibm,supported-tce-sizes");
	if (count <= 0) {
		mask = SZ_4K | SZ_64K;
		/* Add 16M for POWER8 by default */
		if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
				!cpu_has_feature(CPU_FTR_ARCH_300))
			mask |= SZ_16M | SZ_256M;
		return mask;
	}

	for (i = 0; i < count; i++) {
		rc = of_property_read_u32_index(dn, "ibm,supported-tce-sizes",
						i, &val);
		if (rc == 0)
			mask |= 1ULL << val;
	}

	return mask;
}

void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
		void *tce_mem, u64 tce_size,
		u64 dma_offset, unsigned int page_shift)
{
	tbl->it_blocksize = 16;
	tbl->it_base = (unsigned long)tce_mem;
	tbl->it_page_shift = page_shift;
	tbl->it_offset = dma_offset >> tbl->it_page_shift;
	tbl->it_index = 0;
	tbl->it_size = tce_size >> 3;
	tbl->it_busno = 0;
	tbl->it_type = TCE_PCI;
}

static __be64 *pnv_alloc_tce_level(int nid, unsigned int shift)
{
	struct page *tce_mem = NULL;
	__be64 *addr;

	tce_mem = alloc_pages_node(nid, GFP_ATOMIC | __GFP_NOWARN,
			shift - PAGE_SHIFT);
	if (!tce_mem) {
		pr_err("Failed to allocate a TCE memory, level shift=%d\n",
				shift);
		return NULL;
	}
	addr = page_address(tce_mem);
	memset(addr, 0, 1UL << shift);

	return addr;
}

static void pnv_pci_ioda2_table_do_free_pages(__be64 *addr,
		unsigned long size, unsigned int levels);

static __be64 *pnv_tce(struct iommu_table *tbl, bool user, long idx, bool alloc)
{
	__be64 *tmp = user ? tbl->it_userspace : (__be64 *) tbl->it_base;
	int  level = tbl->it_indirect_levels;
	const long shift = ilog2(tbl->it_level_size);
	unsigned long mask = (tbl->it_level_size - 1) << (level * shift);

	while (level) {
		int n = (idx & mask) >> (level * shift);
		unsigned long oldtce, tce = be64_to_cpu(READ_ONCE(tmp[n]));

		if (!tce) {
			__be64 *tmp2;

			if (!alloc)
				return NULL;

			tmp2 = pnv_alloc_tce_level(tbl->it_nid,
					ilog2(tbl->it_level_size) + 3);
			if (!tmp2)
				return NULL;

			tce = __pa(tmp2) | TCE_PCI_READ | TCE_PCI_WRITE;
			oldtce = be64_to_cpu(cmpxchg(&tmp[n], 0,
					cpu_to_be64(tce)));
			if (oldtce) {
				pnv_pci_ioda2_table_do_free_pages(tmp2,
					ilog2(tbl->it_level_size) + 3, 1);
				tce = oldtce;
			}
		}

		tmp = __va(tce & ~(TCE_PCI_READ | TCE_PCI_WRITE));
		idx &= ~mask;
		mask >>= shift;
		--level;
	}

	return tmp + idx;
}

int pnv_tce_build(struct iommu_table *tbl, long index, long npages,
		unsigned long uaddr, enum dma_data_direction direction,
		unsigned long attrs)
{
	u64 proto_tce = iommu_direction_to_tce_perm(direction);
	u64 rpn = __pa(uaddr) >> tbl->it_page_shift;
	long i;

	if (proto_tce & TCE_PCI_WRITE)
		proto_tce |= TCE_PCI_READ;

	for (i = 0; i < npages; i++) {
		unsigned long newtce = proto_tce |
			((rpn + i) << tbl->it_page_shift);
		unsigned long idx = index - tbl->it_offset + i;

		*(pnv_tce(tbl, false, idx, true)) = cpu_to_be64(newtce);
	}

	return 0;
}

#ifdef CONFIG_IOMMU_API
int pnv_tce_xchg(struct iommu_table *tbl, long index,
		unsigned long *hpa, enum dma_data_direction *direction,
		bool alloc)
{
	u64 proto_tce = iommu_direction_to_tce_perm(*direction);
	unsigned long newtce = *hpa | proto_tce, oldtce;
	unsigned long idx = index - tbl->it_offset;
	__be64 *ptce = NULL;

	BUG_ON(*hpa & ~IOMMU_PAGE_MASK(tbl));

	if (*direction == DMA_NONE) {
		ptce = pnv_tce(tbl, false, idx, false);
		if (!ptce) {
			*hpa = 0;
			return 0;
		}
	}

	if (!ptce) {
		ptce = pnv_tce(tbl, false, idx, alloc);
		if (!ptce)
			return -ENOMEM;
	}

	if (newtce & TCE_PCI_WRITE)
		newtce |= TCE_PCI_READ;

	oldtce = be64_to_cpu(xchg(ptce, cpu_to_be64(newtce)));
	*hpa = oldtce & ~(TCE_PCI_READ | TCE_PCI_WRITE);
	*direction = iommu_tce_direction(oldtce);

	return 0;
}

__be64 *pnv_tce_useraddrptr(struct iommu_table *tbl, long index, bool alloc)
{
	if (WARN_ON_ONCE(!tbl->it_userspace))
		return NULL;

	return pnv_tce(tbl, true, index - tbl->it_offset, alloc);
}
#endif

void pnv_tce_free(struct iommu_table *tbl, long index, long npages)
{
	long i;

	for (i = 0; i < npages; i++) {
		unsigned long idx = index - tbl->it_offset + i;
		__be64 *ptce = pnv_tce(tbl, false, idx,	false);

		if (ptce)
			*ptce = cpu_to_be64(0);
		else
			/* Skip the rest of the level */
			i |= tbl->it_level_size - 1;
	}
}

unsigned long pnv_tce_get(struct iommu_table *tbl, long index)
{
	__be64 *ptce = pnv_tce(tbl, false, index - tbl->it_offset, false);

	if (!ptce)
		return 0;

	return be64_to_cpu(*ptce);
}

static void pnv_pci_ioda2_table_do_free_pages(__be64 *addr,
		unsigned long size, unsigned int levels)
{
	const unsigned long addr_ul = (unsigned long) addr &
			~(TCE_PCI_READ | TCE_PCI_WRITE);

	if (levels) {
		long i;
		u64 *tmp = (u64 *) addr_ul;

		for (i = 0; i < size; ++i) {
			unsigned long hpa = be64_to_cpu(tmp[i]);

			if (!(hpa & (TCE_PCI_READ | TCE_PCI_WRITE)))
				continue;

			pnv_pci_ioda2_table_do_free_pages(__va(hpa), size,
					levels - 1);
		}
	}

	free_pages(addr_ul, get_order(size << 3));
}

void pnv_pci_ioda2_table_free_pages(struct iommu_table *tbl)
{
	const unsigned long size = tbl->it_indirect_levels ?
			tbl->it_level_size : tbl->it_size;

	if (!tbl->it_size)
		return;

	pnv_pci_ioda2_table_do_free_pages((__be64 *)tbl->it_base, size,
			tbl->it_indirect_levels);
	if (tbl->it_userspace) {
		pnv_pci_ioda2_table_do_free_pages(tbl->it_userspace, size,
				tbl->it_indirect_levels);
	}
}

static __be64 *pnv_pci_ioda2_table_do_alloc_pages(int nid, unsigned int shift,
		unsigned int levels, unsigned long limit,
		unsigned long *current_offset, unsigned long *total_allocated)
{
	__be64 *addr, *tmp;
	unsigned long allocated = 1UL << shift;
	unsigned int entries = 1UL << (shift - 3);
	long i;

	addr = pnv_alloc_tce_level(nid, shift);
	*total_allocated += allocated;

	--levels;
	if (!levels) {
		*current_offset += allocated;
		return addr;
	}

	for (i = 0; i < entries; ++i) {
		tmp = pnv_pci_ioda2_table_do_alloc_pages(nid, shift,
				levels, limit, current_offset, total_allocated);
		if (!tmp)
			break;

		addr[i] = cpu_to_be64(__pa(tmp) |
				TCE_PCI_READ | TCE_PCI_WRITE);

		if (*current_offset >= limit)
			break;
	}

	return addr;
}

long pnv_pci_ioda2_table_alloc_pages(int nid, __u64 bus_offset,
		__u32 page_shift, __u64 window_size, __u32 levels,
		bool alloc_userspace_copy, struct iommu_table *tbl)
{
	void *addr, *uas = NULL;
	unsigned long offset = 0, level_shift, total_allocated = 0;
	unsigned long total_allocated_uas = 0;
	const unsigned int window_shift = ilog2(window_size);
	unsigned int entries_shift = window_shift - page_shift;
	unsigned int table_shift = max_t(unsigned int, entries_shift + 3,
			PAGE_SHIFT);
	const unsigned long tce_table_size = 1UL << table_shift;

	if (!levels || (levels > POWERNV_IOMMU_MAX_LEVELS))
		return -EINVAL;

	if (!is_power_of_2(window_size))
		return -EINVAL;

	/* Adjust direct table size from window_size and levels */
	entries_shift = (entries_shift + levels - 1) / levels;
	level_shift = entries_shift + 3;
	level_shift = max_t(unsigned int, level_shift, PAGE_SHIFT);

	if ((level_shift - 3) * levels + page_shift >= 55)
		return -EINVAL;

	/* Allocate TCE table */
	addr = pnv_pci_ioda2_table_do_alloc_pages(nid, level_shift,
			1, tce_table_size, &offset, &total_allocated);

	/* addr==NULL means that the first level allocation failed */
	if (!addr)
		return -ENOMEM;

	/*
	 * First level was allocated but some lower level failed as
	 * we did not allocate as much as we wanted,
	 * release partially allocated table.
	 */
	if (levels == 1 && offset < tce_table_size)
		goto free_tces_exit;

	/* Allocate userspace view of the TCE table */
	if (alloc_userspace_copy) {
		offset = 0;
		uas = pnv_pci_ioda2_table_do_alloc_pages(nid, level_shift,
				1, tce_table_size, &offset,
				&total_allocated_uas);
		if (!uas)
			goto free_tces_exit;
		if (levels == 1 && (offset < tce_table_size ||
				total_allocated_uas != total_allocated))
			goto free_uas_exit;
	}

	/* Setup linux iommu table */
	pnv_pci_setup_iommu_table(tbl, addr, tce_table_size, bus_offset,
			page_shift);
	tbl->it_level_size = 1ULL << (level_shift - 3);
	tbl->it_indirect_levels = levels - 1;
	tbl->it_userspace = uas;
	tbl->it_nid = nid;

	pr_debug("Created TCE table: ws=%08llx ts=%lx @%08llx base=%lx uas=%p levels=%d/%d\n",
			window_size, tce_table_size, bus_offset, tbl->it_base,
			tbl->it_userspace, 1, levels);

	return 0;

free_uas_exit:
	pnv_pci_ioda2_table_do_free_pages(uas,
			1ULL << (level_shift - 3), levels - 1);
free_tces_exit:
	pnv_pci_ioda2_table_do_free_pages(addr,
			1ULL << (level_shift - 3), levels - 1);

	return -ENOMEM;
}

void pnv_pci_unlink_table_and_group(struct iommu_table *tbl,
		struct iommu_table_group *table_group)
{
	long i;
	bool found;
	struct iommu_table_group_link *tgl;

	if (!tbl || !table_group)
		return;

	/* Remove link to a group from table's list of attached groups */
	found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(tgl, &tbl->it_group_list, next) {
		if (tgl->table_group == table_group) {
			list_del_rcu(&tgl->next);
			kfree_rcu(tgl, rcu);
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (WARN_ON(!found))
		return;

	/* Clean a pointer to iommu_table in iommu_table_group::tables[] */
	found = false;
	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		if (table_group->tables[i] == tbl) {
			iommu_tce_table_put(tbl);
			table_group->tables[i] = NULL;
			found = true;
			break;
		}
	}
	WARN_ON(!found);
}

long pnv_pci_link_table_and_group(int node, int num,
		struct iommu_table *tbl,
		struct iommu_table_group *table_group)
{
	struct iommu_table_group_link *tgl = NULL;

	if (WARN_ON(!tbl || !table_group))
		return -EINVAL;

	tgl = kzalloc_node(sizeof(struct iommu_table_group_link), GFP_KERNEL,
			node);
	if (!tgl)
		return -ENOMEM;

	tgl->table_group = table_group;
	list_add_rcu(&tgl->next, &tbl->it_group_list);

	table_group->tables[num] = iommu_tce_table_get(tbl);

	return 0;
}
