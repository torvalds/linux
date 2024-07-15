// SPDX-License-Identifier: GPL-2.0-only
/* adi_64.c: support for ADI (Application Data Integrity) feature on
 * sparc m7 and newer processors. This feature is also known as
 * SSM (Silicon Secured Memory).
 *
 * Copyright (C) 2016 Oracle and/or its affiliates. All rights reserved.
 * Author: Khalid Aziz (khalid.aziz@oracle.com)
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm_types.h>
#include <asm/mdesc.h>
#include <asm/adi_64.h>
#include <asm/mmu_64.h>
#include <asm/pgtable_64.h>

/* Each page of storage for ADI tags can accommodate tags for 128
 * pages. When ADI enabled pages are being swapped out, it would be
 * prudent to allocate at least enough tag storage space to accommodate
 * SWAPFILE_CLUSTER number of pages. Allocate enough tag storage to
 * store tags for four SWAPFILE_CLUSTER pages to reduce need for
 * further allocations for same vma.
 */
#define TAG_STORAGE_PAGES	8

struct adi_config adi_state;
EXPORT_SYMBOL(adi_state);

/* mdesc_adi_init() : Parse machine description provided by the
 *	hypervisor to detect ADI capabilities
 *
 * Hypervisor reports ADI capabilities of platform in "hwcap-list" property
 * for "cpu" node. If the platform supports ADI, "hwcap-list" property
 * contains the keyword "adp". If the platform supports ADI, "platform"
 * node will contain "adp-blksz", "adp-nbits" and "ue-on-adp" properties
 * to describe the ADI capabilities.
 */
void __init mdesc_adi_init(void)
{
	struct mdesc_handle *hp = mdesc_grab();
	const char *prop;
	u64 pn, *val;
	int len;

	if (!hp)
		goto adi_not_found;

	pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "cpu");
	if (pn == MDESC_NODE_NULL)
		goto adi_not_found;

	prop = mdesc_get_property(hp, pn, "hwcap-list", &len);
	if (!prop)
		goto adi_not_found;

	/*
	 * Look for "adp" keyword in hwcap-list which would indicate
	 * ADI support
	 */
	adi_state.enabled = false;
	while (len) {
		int plen;

		if (!strcmp(prop, "adp")) {
			adi_state.enabled = true;
			break;
		}

		plen = strlen(prop) + 1;
		prop += plen;
		len -= plen;
	}

	if (!adi_state.enabled)
		goto adi_not_found;

	/* Find the ADI properties in "platform" node. If all ADI
	 * properties are not found, ADI support is incomplete and
	 * do not enable ADI in the kernel.
	 */
	pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "platform");
	if (pn == MDESC_NODE_NULL)
		goto adi_not_found;

	val = (u64 *) mdesc_get_property(hp, pn, "adp-blksz", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.blksz = *val;

	val = (u64 *) mdesc_get_property(hp, pn, "adp-nbits", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.nbits = *val;

	val = (u64 *) mdesc_get_property(hp, pn, "ue-on-adp", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.ue_on_adi = *val;

	/* Some of the code to support swapping ADI tags is written
	 * assumption that two ADI tags can fit inside one byte. If
	 * this assumption is broken by a future architecture change,
	 * that code will have to be revisited. If that were to happen,
	 * disable ADI support so we do not get unpredictable results
	 * with programs trying to use ADI and their pages getting
	 * swapped out
	 */
	if (adi_state.caps.nbits > 4) {
		pr_warn("WARNING: ADI tag size >4 on this platform. Disabling AADI support\n");
		adi_state.enabled = false;
	}

	mdesc_release(hp);
	return;

adi_not_found:
	adi_state.enabled = false;
	adi_state.caps.blksz = 0;
	adi_state.caps.nbits = 0;
	if (hp)
		mdesc_release(hp);
}

static tag_storage_desc_t *find_tag_store(struct mm_struct *mm,
					  struct vm_area_struct *vma,
					  unsigned long addr)
{
	tag_storage_desc_t *tag_desc = NULL;
	unsigned long i, max_desc, flags;

	/* Check if this vma already has tag storage descriptor
	 * allocated for it.
	 */
	max_desc = PAGE_SIZE/sizeof(tag_storage_desc_t);
	if (mm->context.tag_store) {
		tag_desc = mm->context.tag_store;
		spin_lock_irqsave(&mm->context.tag_lock, flags);
		for (i = 0; i < max_desc; i++) {
			if ((addr >= tag_desc->start) &&
			    ((addr + PAGE_SIZE - 1) <= tag_desc->end))
				break;
			tag_desc++;
		}
		spin_unlock_irqrestore(&mm->context.tag_lock, flags);

		/* If no matching entries were found, this must be a
		 * freshly allocated page
		 */
		if (i >= max_desc)
			tag_desc = NULL;
	}

	return tag_desc;
}

static tag_storage_desc_t *alloc_tag_store(struct mm_struct *mm,
					   struct vm_area_struct *vma,
					   unsigned long addr)
{
	unsigned char *tags;
	unsigned long i, size, max_desc, flags;
	tag_storage_desc_t *tag_desc, *open_desc;
	unsigned long end_addr, hole_start, hole_end;

	max_desc = PAGE_SIZE/sizeof(tag_storage_desc_t);
	open_desc = NULL;
	hole_start = 0;
	hole_end = ULONG_MAX;
	end_addr = addr + PAGE_SIZE - 1;

	/* Check if this vma already has tag storage descriptor
	 * allocated for it.
	 */
	spin_lock_irqsave(&mm->context.tag_lock, flags);
	if (mm->context.tag_store) {
		tag_desc = mm->context.tag_store;

		/* Look for a matching entry for this address. While doing
		 * that, look for the first open slot as well and find
		 * the hole in already allocated range where this request
		 * will fit in.
		 */
		for (i = 0; i < max_desc; i++) {
			if (tag_desc->tag_users == 0) {
				if (open_desc == NULL)
					open_desc = tag_desc;
			} else {
				if ((addr >= tag_desc->start) &&
				    (tag_desc->end >= (addr + PAGE_SIZE - 1))) {
					tag_desc->tag_users++;
					goto out;
				}
			}
			if ((tag_desc->start > end_addr) &&
			    (tag_desc->start < hole_end))
				hole_end = tag_desc->start;
			if ((tag_desc->end < addr) &&
			    (tag_desc->end > hole_start))
				hole_start = tag_desc->end;
			tag_desc++;
		}

	} else {
		size = sizeof(tag_storage_desc_t)*max_desc;
		mm->context.tag_store = kzalloc(size, GFP_NOWAIT|__GFP_NOWARN);
		if (mm->context.tag_store == NULL) {
			tag_desc = NULL;
			goto out;
		}
		tag_desc = mm->context.tag_store;
		for (i = 0; i < max_desc; i++, tag_desc++)
			tag_desc->tag_users = 0;
		open_desc = mm->context.tag_store;
		i = 0;
	}

	/* Check if we ran out of tag storage descriptors */
	if (open_desc == NULL) {
		tag_desc = NULL;
		goto out;
	}

	/* Mark this tag descriptor slot in use and then initialize it */
	tag_desc = open_desc;
	tag_desc->tag_users = 1;

	/* Tag storage has not been allocated for this vma and space
	 * is available in tag storage descriptor. Since this page is
	 * being swapped out, there is high probability subsequent pages
	 * in the VMA will be swapped out as well. Allocate pages to
	 * store tags for as many pages in this vma as possible but not
	 * more than TAG_STORAGE_PAGES. Each byte in tag space holds
	 * two ADI tags since each ADI tag is 4 bits. Each ADI tag
	 * covers adi_blksize() worth of addresses. Check if the hole is
	 * big enough to accommodate full address range for using
	 * TAG_STORAGE_PAGES number of tag pages.
	 */
	size = TAG_STORAGE_PAGES * PAGE_SIZE;
	end_addr = addr + (size*2*adi_blksize()) - 1;
	/* Check for overflow. If overflow occurs, allocate only one page */
	if (end_addr < addr) {
		size = PAGE_SIZE;
		end_addr = addr + (size*2*adi_blksize()) - 1;
		/* If overflow happens with the minimum tag storage
		 * allocation as well, adjust ending address for this
		 * tag storage.
		 */
		if (end_addr < addr)
			end_addr = ULONG_MAX;
	}
	if (hole_end < end_addr) {
		/* Available hole is too small on the upper end of
		 * address. Can we expand the range towards the lower
		 * address and maximize use of this slot?
		 */
		unsigned long tmp_addr;

		end_addr = hole_end - 1;
		tmp_addr = end_addr - (size*2*adi_blksize()) + 1;
		/* Check for underflow. If underflow occurs, allocate
		 * only one page for storing ADI tags
		 */
		if (tmp_addr > addr) {
			size = PAGE_SIZE;
			tmp_addr = end_addr - (size*2*adi_blksize()) - 1;
			/* If underflow happens with the minimum tag storage
			 * allocation as well, adjust starting address for
			 * this tag storage.
			 */
			if (tmp_addr > addr)
				tmp_addr = 0;
		}
		if (tmp_addr < hole_start) {
			/* Available hole is restricted on lower address
			 * end as well
			 */
			tmp_addr = hole_start + 1;
		}
		addr = tmp_addr;
		size = (end_addr + 1 - addr)/(2*adi_blksize());
		size = (size + (PAGE_SIZE-adi_blksize()))/PAGE_SIZE;
		size = size * PAGE_SIZE;
	}
	tags = kzalloc(size, GFP_NOWAIT|__GFP_NOWARN);
	if (tags == NULL) {
		tag_desc->tag_users = 0;
		tag_desc = NULL;
		goto out;
	}
	tag_desc->start = addr;
	tag_desc->tags = tags;
	tag_desc->end = end_addr;

out:
	spin_unlock_irqrestore(&mm->context.tag_lock, flags);
	return tag_desc;
}

static void del_tag_store(tag_storage_desc_t *tag_desc, struct mm_struct *mm)
{
	unsigned long flags;
	unsigned char *tags = NULL;

	spin_lock_irqsave(&mm->context.tag_lock, flags);
	tag_desc->tag_users--;
	if (tag_desc->tag_users == 0) {
		tag_desc->start = tag_desc->end = 0;
		/* Do not free up the tag storage space allocated
		 * by the first descriptor. This is persistent
		 * emergency tag storage space for the task.
		 */
		if (tag_desc != mm->context.tag_store) {
			tags = tag_desc->tags;
			tag_desc->tags = NULL;
		}
	}
	spin_unlock_irqrestore(&mm->context.tag_lock, flags);
	kfree(tags);
}

#define tag_start(addr, tag_desc)		\
	((tag_desc)->tags + ((addr - (tag_desc)->start)/(2*adi_blksize())))

/* Retrieve any saved ADI tags for the page being swapped back in and
 * restore these tags to the newly allocated physical page.
 */
void adi_restore_tags(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, pte_t pte)
{
	unsigned char *tag;
	tag_storage_desc_t *tag_desc;
	unsigned long paddr, tmp, version1, version2;

	/* Check if the swapped out page has an ADI version
	 * saved. If yes, restore version tag to the newly
	 * allocated page.
	 */
	tag_desc = find_tag_store(mm, vma, addr);
	if (tag_desc == NULL)
		return;

	tag = tag_start(addr, tag_desc);
	paddr = pte_val(pte) & _PAGE_PADDR_4V;
	for (tmp = paddr; tmp < (paddr+PAGE_SIZE); tmp += adi_blksize()) {
		version1 = (*tag) >> 4;
		version2 = (*tag) & 0x0f;
		*tag++ = 0;
		asm volatile("stxa %0, [%1] %2\n\t"
			:
			: "r" (version1), "r" (tmp),
			  "i" (ASI_MCD_REAL));
		tmp += adi_blksize();
		asm volatile("stxa %0, [%1] %2\n\t"
			:
			: "r" (version2), "r" (tmp),
			  "i" (ASI_MCD_REAL));
	}
	asm volatile("membar #Sync\n\t");

	/* Check and mark this tag space for release later if
	 * the swapped in page was the last user of tag space
	 */
	del_tag_store(tag_desc, mm);
}

/* A page is about to be swapped out. Save any ADI tags associated with
 * this physical page so they can be restored later when the page is swapped
 * back in.
 */
int adi_save_tags(struct mm_struct *mm, struct vm_area_struct *vma,
		  unsigned long addr, pte_t oldpte)
{
	unsigned char *tag;
	tag_storage_desc_t *tag_desc;
	unsigned long version1, version2, paddr, tmp;

	tag_desc = alloc_tag_store(mm, vma, addr);
	if (tag_desc == NULL)
		return -1;

	tag = tag_start(addr, tag_desc);
	paddr = pte_val(oldpte) & _PAGE_PADDR_4V;
	for (tmp = paddr; tmp < (paddr+PAGE_SIZE); tmp += adi_blksize()) {
		asm volatile("ldxa [%1] %2, %0\n\t"
				: "=r" (version1)
				: "r" (tmp), "i" (ASI_MCD_REAL));
		tmp += adi_blksize();
		asm volatile("ldxa [%1] %2, %0\n\t"
				: "=r" (version2)
				: "r" (tmp), "i" (ASI_MCD_REAL));
		*tag = (version1 << 4) | version2;
		tag++;
	}

	return 0;
}
