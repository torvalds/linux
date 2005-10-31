/*
 * c 2001 PPC 64 Team, IBM Corp
 * 
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/semaphore.h>
#include <asm/imalloc.h>
#include <asm/cacheflush.h>

static DECLARE_MUTEX(imlist_sem);
struct vm_struct * imlist = NULL;

static int get_free_im_addr(unsigned long size, unsigned long *im_addr)
{
	unsigned long addr;
	struct vm_struct **p, *tmp;

	addr = ioremap_bot;
	for (p = &imlist; (tmp = *p) ; p = &tmp->next) {
		if (size + addr < (unsigned long) tmp->addr)
			break;
		if ((unsigned long)tmp->addr >= ioremap_bot)
			addr = tmp->size + (unsigned long) tmp->addr;
		if (addr >= IMALLOC_END-size)
			return 1;
	}
	*im_addr = addr;

	return 0;
}

/* Return whether the region described by v_addr and size is a subset
 * of the region described by parent
 */
static inline int im_region_is_subset(unsigned long v_addr, unsigned long size,
			struct vm_struct *parent)
{
	return (int) (v_addr >= (unsigned long) parent->addr &&
	              v_addr < (unsigned long) parent->addr + parent->size &&
	    	      size < parent->size);
}

/* Return whether the region described by v_addr and size is a superset
 * of the region described by child
 */
static int im_region_is_superset(unsigned long v_addr, unsigned long size,
		struct vm_struct *child)
{
	struct vm_struct parent;

	parent.addr = (void *) v_addr;
	parent.size = size;

	return im_region_is_subset((unsigned long) child->addr, child->size,
			&parent);
}

/* Return whether the region described by v_addr and size overlaps
 * the region described by vm.  Overlapping regions meet the
 * following conditions:
 * 1) The regions share some part of the address space
 * 2) The regions aren't identical
 * 3) Neither region is a subset of the other
 */
static int im_region_overlaps(unsigned long v_addr, unsigned long size,
		     struct vm_struct *vm)
{
	if (im_region_is_superset(v_addr, size, vm))
		return 0;

	return (v_addr + size > (unsigned long) vm->addr + vm->size &&
		v_addr < (unsigned long) vm->addr + vm->size) ||
	       (v_addr < (unsigned long) vm->addr &&
		v_addr + size > (unsigned long) vm->addr);
}

/* Determine imalloc status of region described by v_addr and size.
 * Can return one of the following:
 * IM_REGION_UNUSED   -  Entire region is unallocated in imalloc space.
 * IM_REGION_SUBSET -    Region is a subset of a region that is already
 * 			 allocated in imalloc space.
 * 		         vm will be assigned to a ptr to the parent region.
 * IM_REGION_EXISTS -    Exact region already allocated in imalloc space.
 *                       vm will be assigned to a ptr to the existing imlist
 *                       member.
 * IM_REGION_OVERLAPS -  Region overlaps an allocated region in imalloc space.
 * IM_REGION_SUPERSET -  Region is a superset of a region that is already
 *                       allocated in imalloc space.
 */
static int im_region_status(unsigned long v_addr, unsigned long size,
		    struct vm_struct **vm)
{
	struct vm_struct *tmp;

	for (tmp = imlist; tmp; tmp = tmp->next)
		if (v_addr < (unsigned long) tmp->addr + tmp->size)
			break;

	if (tmp) {
		if (im_region_overlaps(v_addr, size, tmp))
			return IM_REGION_OVERLAP;

		*vm = tmp;
		if (im_region_is_subset(v_addr, size, tmp)) {
			/* Return with tmp pointing to superset */
			return IM_REGION_SUBSET;
		}
		if (im_region_is_superset(v_addr, size, tmp)) {
			/* Return with tmp pointing to first subset */
			return IM_REGION_SUPERSET;
		}
		else if (v_addr == (unsigned long) tmp->addr &&
		 	 size == tmp->size) {
			/* Return with tmp pointing to exact region */
			return IM_REGION_EXISTS;
		}
	}

	*vm = NULL;
	return IM_REGION_UNUSED;
}

static struct vm_struct * split_im_region(unsigned long v_addr, 
		unsigned long size, struct vm_struct *parent)
{
	struct vm_struct *vm1 = NULL;
	struct vm_struct *vm2 = NULL;
	struct vm_struct *new_vm = NULL;
	
	vm1 = (struct vm_struct *) kmalloc(sizeof(*vm1), GFP_KERNEL);
	if (vm1	== NULL) {
		printk(KERN_ERR "%s() out of memory\n", __FUNCTION__);
		return NULL;
	}

	if (v_addr == (unsigned long) parent->addr) {
	        /* Use existing parent vm_struct to represent child, allocate
		 * new one for the remainder of parent range
		 */
		vm1->size = parent->size - size;
		vm1->addr = (void *) (v_addr + size);
		vm1->next = parent->next;

		parent->size = size;
		parent->next = vm1;
		new_vm = parent;
	} else if (v_addr + size == (unsigned long) parent->addr + 
			parent->size) {
		/* Allocate new vm_struct to represent child, use existing
		 * parent one for remainder of parent range
		 */
		vm1->size = size;
		vm1->addr = (void *) v_addr;
		vm1->next = parent->next;
		new_vm = vm1;

		parent->size -= size;
		parent->next = vm1;
	} else {
	        /* Allocate two new vm_structs for the new child and 
		 * uppermost remainder, and use existing parent one for the
		 * lower remainder of parent range
		 */
		vm2 = (struct vm_struct *) kmalloc(sizeof(*vm2), GFP_KERNEL);
		if (vm2 == NULL) {
			printk(KERN_ERR "%s() out of memory\n", __FUNCTION__);
			kfree(vm1);
			return NULL;
		}

		vm1->size = size;
		vm1->addr = (void *) v_addr;
		vm1->next = vm2;
		new_vm = vm1;

		vm2->size = ((unsigned long) parent->addr + parent->size) - 
				(v_addr + size);
		vm2->addr = (void *) v_addr + size;
		vm2->next = parent->next;

		parent->size = v_addr - (unsigned long) parent->addr;
		parent->next = vm1;
	}

	return new_vm;
}

static struct vm_struct * __add_new_im_area(unsigned long req_addr, 
					    unsigned long size)
{
	struct vm_struct **p, *tmp, *area;
		
	for (p = &imlist; (tmp = *p) ; p = &tmp->next) {
		if (req_addr + size <= (unsigned long)tmp->addr)
			break;
	}
	
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	area->flags = 0;
	area->addr = (void *)req_addr;
	area->size = size;
	area->next = *p;
	*p = area;

	return area;
}

static struct vm_struct * __im_get_area(unsigned long req_addr, 
					unsigned long size,
					int criteria)
{
	struct vm_struct *tmp;
	int status;

	status = im_region_status(req_addr, size, &tmp);
	if ((criteria & status) == 0) {
		return NULL;
	}
	
	switch (status) {
	case IM_REGION_UNUSED:
		tmp = __add_new_im_area(req_addr, size);
		break;
	case IM_REGION_SUBSET:
		tmp = split_im_region(req_addr, size, tmp);
		break;
	case IM_REGION_EXISTS:
		/* Return requested region */
		break;
	case IM_REGION_SUPERSET:
		/* Return first existing subset of requested region */
		break;
	default:
		printk(KERN_ERR "%s() unexpected imalloc region status\n",
				__FUNCTION__);
		tmp = NULL;
	}

	return tmp;
}

struct vm_struct * im_get_free_area(unsigned long size)
{
	struct vm_struct *area;
	unsigned long addr;
	
	down(&imlist_sem);
	if (get_free_im_addr(size, &addr)) {
		printk(KERN_ERR "%s() cannot obtain addr for size 0x%lx\n",
				__FUNCTION__, size);
		area = NULL;
		goto next_im_done;
	}

	area = __im_get_area(addr, size, IM_REGION_UNUSED);
	if (area == NULL) {
		printk(KERN_ERR 
		       "%s() cannot obtain area for addr 0x%lx size 0x%lx\n",
			__FUNCTION__, addr, size);
	}
next_im_done:
	up(&imlist_sem);
	return area;
}

struct vm_struct * im_get_area(unsigned long v_addr, unsigned long size,
		int criteria)
{
	struct vm_struct *area;

	down(&imlist_sem);
	area = __im_get_area(v_addr, size, criteria);
	up(&imlist_sem);
	return area;
}

void im_free(void * addr)
{
	struct vm_struct **p, *tmp;
  
	if (!addr)
		return;
	if ((unsigned long) addr & ~PAGE_MASK) {
		printk(KERN_ERR "Trying to %s bad address (%p)\n", __FUNCTION__,			addr);
		return;
	}
	down(&imlist_sem);
	for (p = &imlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			unmap_vm_area(tmp);
			kfree(tmp);
			up(&imlist_sem);
			return;
		}
	}
	up(&imlist_sem);
	printk(KERN_ERR "Trying to %s nonexistent area (%p)\n", __FUNCTION__,
			addr);
}
