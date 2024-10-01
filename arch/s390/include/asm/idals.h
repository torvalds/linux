/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 2000
 *
 * History of changes
 * 07/24/00 new file
 * 05/04/02 code restructuring.
 */

#ifndef _S390_IDALS_H
#define _S390_IDALS_H

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/dma-types.h>
#include <asm/cio.h>

#define IDA_SIZE_SHIFT		12
#define IDA_BLOCK_SIZE		(1UL << IDA_SIZE_SHIFT)

#define IDA_2K_SIZE_SHIFT	11
#define IDA_2K_BLOCK_SIZE	(1UL << IDA_2K_SIZE_SHIFT)

/*
 * Test if an address/length pair needs an idal list.
 */
static inline bool idal_is_needed(void *vaddr, unsigned int length)
{
	dma64_t paddr = virt_to_dma64(vaddr);

	return (((__force unsigned long)(paddr) + length - 1) >> 31) != 0;
}

/*
 * Return the number of idal words needed for an address/length pair.
 */
static inline unsigned int idal_nr_words(void *vaddr, unsigned int length)
{
	unsigned int cidaw;

	cidaw = (unsigned long)vaddr & (IDA_BLOCK_SIZE - 1);
	cidaw += length + IDA_BLOCK_SIZE - 1;
	cidaw >>= IDA_SIZE_SHIFT;
	return cidaw;
}

/*
 * Return the number of 2K IDA words needed for an address/length pair.
 */
static inline unsigned int idal_2k_nr_words(void *vaddr, unsigned int length)
{
	unsigned int cidaw;

	cidaw = (unsigned long)vaddr & (IDA_2K_BLOCK_SIZE - 1);
	cidaw += length + IDA_2K_BLOCK_SIZE - 1;
	cidaw >>= IDA_2K_SIZE_SHIFT;
	return cidaw;
}

/*
 * Create the list of idal words for an address/length pair.
 */
static inline dma64_t *idal_create_words(dma64_t *idaws, void *vaddr, unsigned int length)
{
	dma64_t paddr = virt_to_dma64(vaddr);
	unsigned int cidaw;

	*idaws++ = paddr;
	cidaw = idal_nr_words(vaddr, length);
	paddr = dma64_and(paddr, -IDA_BLOCK_SIZE);
	while (--cidaw > 0) {
		paddr = dma64_add(paddr, IDA_BLOCK_SIZE);
		*idaws++ = paddr;
	}
	return idaws;
}

/*
 * Sets the address of the data in CCW.
 * If necessary it allocates an IDAL and sets the appropriate flags.
 */
static inline int set_normalized_cda(struct ccw1 *ccw, void *vaddr)
{
	unsigned int nridaws;
	dma64_t *idal;

	if (ccw->flags & CCW_FLAG_IDA)
		return -EINVAL;
	nridaws = idal_nr_words(vaddr, ccw->count);
	if (nridaws > 0) {
		idal = kcalloc(nridaws, sizeof(*idal), GFP_ATOMIC | GFP_DMA);
		if (!idal)
			return -ENOMEM;
		idal_create_words(idal, vaddr, ccw->count);
		ccw->flags |= CCW_FLAG_IDA;
		vaddr = idal;
	}
	ccw->cda = virt_to_dma32(vaddr);
	return 0;
}

/*
 * Releases any allocated IDAL related to the CCW.
 */
static inline void clear_normalized_cda(struct ccw1 *ccw)
{
	if (ccw->flags & CCW_FLAG_IDA) {
		kfree(dma32_to_virt(ccw->cda));
		ccw->flags &= ~CCW_FLAG_IDA;
	}
	ccw->cda = 0;
}

/*
 * Idal buffer extension
 */
struct idal_buffer {
	size_t size;
	size_t page_order;
	dma64_t data[];
};

/*
 * Allocate an idal buffer
 */
static inline struct idal_buffer *idal_buffer_alloc(size_t size, int page_order)
{
	int nr_chunks, nr_ptrs, i;
	struct idal_buffer *ib;
	void *vaddr;

	nr_ptrs = (size + IDA_BLOCK_SIZE - 1) >> IDA_SIZE_SHIFT;
	nr_chunks = (PAGE_SIZE << page_order) >> IDA_SIZE_SHIFT;
	ib = kmalloc(struct_size(ib, data, nr_ptrs), GFP_DMA | GFP_KERNEL);
	if (!ib)
		return ERR_PTR(-ENOMEM);
	ib->size = size;
	ib->page_order = page_order;
	for (i = 0; i < nr_ptrs; i++) {
		if (i & (nr_chunks - 1)) {
			ib->data[i] = dma64_add(ib->data[i - 1], IDA_BLOCK_SIZE);
			continue;
		}
		vaddr = (void *)__get_free_pages(GFP_KERNEL, page_order);
		if (!vaddr)
			goto error;
		ib->data[i] = virt_to_dma64(vaddr);
	}
	return ib;
error:
	while (i >= nr_chunks) {
		i -= nr_chunks;
		vaddr = dma64_to_virt(ib->data[i]);
		free_pages((unsigned long)vaddr, ib->page_order);
	}
	kfree(ib);
	return ERR_PTR(-ENOMEM);
}

/*
 * Free an idal buffer.
 */
static inline void idal_buffer_free(struct idal_buffer *ib)
{
	int nr_chunks, nr_ptrs, i;
	void *vaddr;

	nr_ptrs = (ib->size + IDA_BLOCK_SIZE - 1) >> IDA_SIZE_SHIFT;
	nr_chunks = (PAGE_SIZE << ib->page_order) >> IDA_SIZE_SHIFT;
	for (i = 0; i < nr_ptrs; i += nr_chunks) {
		vaddr = dma64_to_virt(ib->data[i]);
		free_pages((unsigned long)vaddr, ib->page_order);
	}
	kfree(ib);
}

/*
 * Test if a idal list is really needed.
 */
static inline bool __idal_buffer_is_needed(struct idal_buffer *ib)
{
	if (ib->size > (PAGE_SIZE << ib->page_order))
		return true;
	return idal_is_needed(dma64_to_virt(ib->data[0]), ib->size);
}

/*
 * Set channel data address to idal buffer.
 */
static inline void idal_buffer_set_cda(struct idal_buffer *ib, struct ccw1 *ccw)
{
	void *vaddr;

	if (__idal_buffer_is_needed(ib)) {
		/* Setup idals */
		ccw->cda = virt_to_dma32(ib->data);
		ccw->flags |= CCW_FLAG_IDA;
	} else {
		/*
		 * No idals needed - use direct addressing. Convert from
		 * dma64_t to virt and then to dma32_t only because of type
		 * checking. The physical address is known to be below 2GB.
		 */
		vaddr = dma64_to_virt(ib->data[0]);
		ccw->cda = virt_to_dma32(vaddr);
	}
	ccw->count = ib->size;
}

/*
 * Copy count bytes from an idal buffer to user memory
 */
static inline size_t idal_buffer_to_user(struct idal_buffer *ib, void __user *to, size_t count)
{
	size_t left;
	void *vaddr;
	int i;

	BUG_ON(count > ib->size);
	for (i = 0; count > IDA_BLOCK_SIZE; i++) {
		vaddr = dma64_to_virt(ib->data[i]);
		left = copy_to_user(to, vaddr, IDA_BLOCK_SIZE);
		if (left)
			return left + count - IDA_BLOCK_SIZE;
		to = (void __user *)to + IDA_BLOCK_SIZE;
		count -= IDA_BLOCK_SIZE;
	}
	vaddr = dma64_to_virt(ib->data[i]);
	return copy_to_user(to, vaddr, count);
}

/*
 * Copy count bytes from user memory to an idal buffer
 */
static inline size_t idal_buffer_from_user(struct idal_buffer *ib, const void __user *from, size_t count)
{
	size_t left;
	void *vaddr;
	int i;

	BUG_ON(count > ib->size);
	for (i = 0; count > IDA_BLOCK_SIZE; i++) {
		vaddr = dma64_to_virt(ib->data[i]);
		left = copy_from_user(vaddr, from, IDA_BLOCK_SIZE);
		if (left)
			return left + count - IDA_BLOCK_SIZE;
		from = (void __user *)from + IDA_BLOCK_SIZE;
		count -= IDA_BLOCK_SIZE;
	}
	vaddr = dma64_to_virt(ib->data[i]);
	return copy_from_user(vaddr, from, count);
}

#endif
