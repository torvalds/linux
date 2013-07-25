/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) IBM Corp. 2006
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <xen/xencomm.h>
#include <xen/interface/xen.h>
#include <asm/xen/xencomm.h>	/* for xencomm_is_phys_contiguous() */

static int xencomm_init(struct xencomm_desc *desc,
			void *buffer, unsigned long bytes)
{
	unsigned long recorded = 0;
	int i = 0;

	while ((recorded < bytes) && (i < desc->nr_addrs)) {
		unsigned long vaddr = (unsigned long)buffer + recorded;
		unsigned long paddr;
		int offset;
		int chunksz;

		offset = vaddr % PAGE_SIZE; /* handle partial pages */
		chunksz = min(PAGE_SIZE - offset, bytes - recorded);

		paddr = xencomm_vtop(vaddr);
		if (paddr == ~0UL) {
			printk(KERN_DEBUG "%s: couldn't translate vaddr %lx\n",
			       __func__, vaddr);
			return -EINVAL;
		}

		desc->address[i++] = paddr;
		recorded += chunksz;
	}

	if (recorded < bytes) {
		printk(KERN_DEBUG
		       "%s: could only translate %ld of %ld bytes\n",
		       __func__, recorded, bytes);
		return -ENOSPC;
	}

	/* mark remaining addresses invalid (just for safety) */
	while (i < desc->nr_addrs)
		desc->address[i++] = XENCOMM_INVALID;

	desc->magic = XENCOMM_MAGIC;

	return 0;
}

static struct xencomm_desc *xencomm_alloc(gfp_t gfp_mask,
					  void *buffer, unsigned long bytes)
{
	struct xencomm_desc *desc;
	unsigned long buffer_ulong = (unsigned long)buffer;
	unsigned long start = buffer_ulong & PAGE_MASK;
	unsigned long end = (buffer_ulong + bytes) | ~PAGE_MASK;
	unsigned long nr_addrs = (end - start + 1) >> PAGE_SHIFT;
	unsigned long size = sizeof(*desc) +
		sizeof(desc->address[0]) * nr_addrs;

	/*
	 * slab allocator returns at least sizeof(void*) aligned pointer.
	 * When sizeof(*desc) > sizeof(void*), struct xencomm_desc might
	 * cross page boundary.
	 */
	if (sizeof(*desc) > sizeof(void *)) {
		unsigned long order = get_order(size);
		desc = (struct xencomm_desc *)__get_free_pages(gfp_mask,
							       order);
		if (desc == NULL)
			return NULL;

		desc->nr_addrs =
			((PAGE_SIZE << order) - sizeof(struct xencomm_desc)) /
			sizeof(*desc->address);
	} else {
		desc = kmalloc(size, gfp_mask);
		if (desc == NULL)
			return NULL;

		desc->nr_addrs = nr_addrs;
	}
	return desc;
}

void xencomm_free(struct xencomm_handle *desc)
{
	if (desc && !((ulong)desc & XENCOMM_INLINE_FLAG)) {
		struct xencomm_desc *desc__ = (struct xencomm_desc *)desc;
		if (sizeof(*desc__) > sizeof(void *)) {
			unsigned long size = sizeof(*desc__) +
				sizeof(desc__->address[0]) * desc__->nr_addrs;
			unsigned long order = get_order(size);
			free_pages((unsigned long)__va(desc), order);
		} else
			kfree(__va(desc));
	}
}

static int xencomm_create(void *buffer, unsigned long bytes,
			  struct xencomm_desc **ret, gfp_t gfp_mask)
{
	struct xencomm_desc *desc;
	int rc;

	pr_debug("%s: %p[%ld]\n", __func__, buffer, bytes);

	if (bytes == 0) {
		/* don't create a descriptor; Xen recognizes NULL. */
		BUG_ON(buffer != NULL);
		*ret = NULL;
		return 0;
	}

	BUG_ON(buffer == NULL); /* 'bytes' is non-zero */

	desc = xencomm_alloc(gfp_mask, buffer, bytes);
	if (!desc) {
		printk(KERN_DEBUG "%s failure\n", "xencomm_alloc");
		return -ENOMEM;
	}

	rc = xencomm_init(desc, buffer, bytes);
	if (rc) {
		printk(KERN_DEBUG "%s failure: %d\n", "xencomm_init", rc);
		xencomm_free((struct xencomm_handle *)__pa(desc));
		return rc;
	}

	*ret = desc;
	return 0;
}

static struct xencomm_handle *xencomm_create_inline(void *ptr)
{
	unsigned long paddr;

	BUG_ON(!xencomm_is_phys_contiguous((unsigned long)ptr));

	paddr = (unsigned long)xencomm_pa(ptr);
	BUG_ON(paddr & XENCOMM_INLINE_FLAG);
	return (struct xencomm_handle *)(paddr | XENCOMM_INLINE_FLAG);
}

/* "mini" routine, for stack-based communications: */
static int xencomm_create_mini(void *buffer,
	unsigned long bytes, struct xencomm_mini *xc_desc,
	struct xencomm_desc **ret)
{
	int rc = 0;
	struct xencomm_desc *desc;
	BUG_ON(((unsigned long)xc_desc) % sizeof(*xc_desc) != 0);

	desc = (void *)xc_desc;

	desc->nr_addrs = XENCOMM_MINI_ADDRS;

	rc = xencomm_init(desc, buffer, bytes);
	if (!rc)
		*ret = desc;

	return rc;
}

struct xencomm_handle *xencomm_map(void *ptr, unsigned long bytes)
{
	int rc;
	struct xencomm_desc *desc;

	if (xencomm_is_phys_contiguous((unsigned long)ptr))
		return xencomm_create_inline(ptr);

	rc = xencomm_create(ptr, bytes, &desc, GFP_KERNEL);

	if (rc || desc == NULL)
		return NULL;

	return xencomm_pa(desc);
}

struct xencomm_handle *__xencomm_map_no_alloc(void *ptr, unsigned long bytes,
			struct xencomm_mini *xc_desc)
{
	int rc;
	struct xencomm_desc *desc = NULL;

	if (xencomm_is_phys_contiguous((unsigned long)ptr))
		return xencomm_create_inline(ptr);

	rc = xencomm_create_mini(ptr, bytes, xc_desc,
				&desc);

	if (rc)
		return NULL;

	return xencomm_pa(desc);
}
