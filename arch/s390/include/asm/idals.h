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
#include <asm/cio.h>
#include <linux/uaccess.h>

#define IDA_SIZE_LOG 12 /* 11 for 2k , 12 for 4k */
#define IDA_BLOCK_SIZE (1L<<IDA_SIZE_LOG)

/*
 * Test if an address/length pair needs an idal list.
 */
static inline int
idal_is_needed(void *vaddr, unsigned int length)
{
	return ((__pa(vaddr) + length - 1) >> 31) != 0;
}


/*
 * Return the number of idal words needed for an address/length pair.
 */
static inline unsigned int idal_nr_words(void *vaddr, unsigned int length)
{
	return ((__pa(vaddr) & (IDA_BLOCK_SIZE-1)) + length +
		(IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
}

/*
 * Create the list of idal words for an address/length pair.
 */
static inline unsigned long *idal_create_words(unsigned long *idaws,
					       void *vaddr, unsigned int length)
{
	unsigned long paddr;
	unsigned int cidaw;

	paddr = __pa(vaddr);
	cidaw = ((paddr & (IDA_BLOCK_SIZE-1)) + length + 
		 (IDA_BLOCK_SIZE-1)) >> IDA_SIZE_LOG;
	*idaws++ = paddr;
	paddr &= -IDA_BLOCK_SIZE;
	while (--cidaw > 0) {
		paddr += IDA_BLOCK_SIZE;
		*idaws++ = paddr;
	}
	return idaws;
}

/*
 * Sets the address of the data in CCW.
 * If necessary it allocates an IDAL and sets the appropriate flags.
 */
static inline int
set_normalized_cda(struct ccw1 * ccw, void *vaddr)
{
	unsigned int nridaws;
	unsigned long *idal;

	if (ccw->flags & CCW_FLAG_IDA)
		return -EINVAL;
	nridaws = idal_nr_words(vaddr, ccw->count);
	if (nridaws > 0) {
		idal = kmalloc(nridaws * sizeof(unsigned long),
			       GFP_ATOMIC | GFP_DMA );
		if (idal == NULL)
			return -ENOMEM;
		idal_create_words(idal, vaddr, ccw->count);
		ccw->flags |= CCW_FLAG_IDA;
		vaddr = idal;
	}
	ccw->cda = (__u32)(unsigned long) vaddr;
	return 0;
}

/*
 * Releases any allocated IDAL related to the CCW.
 */
static inline void
clear_normalized_cda(struct ccw1 * ccw)
{
	if (ccw->flags & CCW_FLAG_IDA) {
		kfree((void *)(unsigned long) ccw->cda);
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
	void *data[0];
};

/*
 * Allocate an idal buffer
 */
static inline struct idal_buffer *
idal_buffer_alloc(size_t size, int page_order)
{
	struct idal_buffer *ib;
	int nr_chunks, nr_ptrs, i;

	nr_ptrs = (size + IDA_BLOCK_SIZE - 1) >> IDA_SIZE_LOG;
	nr_chunks = (4096 << page_order) >> IDA_SIZE_LOG;
	ib = kmalloc(sizeof(struct idal_buffer) + nr_ptrs*sizeof(void *),
		     GFP_DMA | GFP_KERNEL);
	if (ib == NULL)
		return ERR_PTR(-ENOMEM);
	ib->size = size;
	ib->page_order = page_order;
	for (i = 0; i < nr_ptrs; i++) {
		if ((i & (nr_chunks - 1)) != 0) {
			ib->data[i] = ib->data[i-1] + IDA_BLOCK_SIZE;
			continue;
		}
		ib->data[i] = (void *)
			__get_free_pages(GFP_KERNEL, page_order);
		if (ib->data[i] != NULL)
			continue;
		// Not enough memory
		while (i >= nr_chunks) {
			i -= nr_chunks;
			free_pages((unsigned long) ib->data[i],
				   ib->page_order);
		}
		kfree(ib);
		return ERR_PTR(-ENOMEM);
	}
	return ib;
}

/*
 * Free an idal buffer.
 */
static inline void
idal_buffer_free(struct idal_buffer *ib)
{
	int nr_chunks, nr_ptrs, i;

	nr_ptrs = (ib->size + IDA_BLOCK_SIZE - 1) >> IDA_SIZE_LOG;
	nr_chunks = (4096 << ib->page_order) >> IDA_SIZE_LOG;
	for (i = 0; i < nr_ptrs; i += nr_chunks)
		free_pages((unsigned long) ib->data[i], ib->page_order);
	kfree(ib);
}

/*
 * Test if a idal list is really needed.
 */
static inline int
__idal_buffer_is_needed(struct idal_buffer *ib)
{
	return ib->size > (4096ul << ib->page_order) ||
		idal_is_needed(ib->data[0], ib->size);
}

/*
 * Set channel data address to idal buffer.
 */
static inline void
idal_buffer_set_cda(struct idal_buffer *ib, struct ccw1 *ccw)
{
	if (__idal_buffer_is_needed(ib)) {
		// setup idals;
		ccw->cda = (u32)(addr_t) ib->data;
		ccw->flags |= CCW_FLAG_IDA;
	} else
		// we do not need idals - use direct addressing
		ccw->cda = (u32)(addr_t) ib->data[0];
	ccw->count = ib->size;
}

/*
 * Copy count bytes from an idal buffer to user memory
 */
static inline size_t
idal_buffer_to_user(struct idal_buffer *ib, void __user *to, size_t count)
{
	size_t left;
	int i;

	BUG_ON(count > ib->size);
	for (i = 0; count > IDA_BLOCK_SIZE; i++) {
		left = copy_to_user(to, ib->data[i], IDA_BLOCK_SIZE);
		if (left)
			return left + count - IDA_BLOCK_SIZE;
		to = (void __user *) to + IDA_BLOCK_SIZE;
		count -= IDA_BLOCK_SIZE;
	}
	return copy_to_user(to, ib->data[i], count);
}

/*
 * Copy count bytes from user memory to an idal buffer
 */
static inline size_t
idal_buffer_from_user(struct idal_buffer *ib, const void __user *from, size_t count)
{
	size_t left;
	int i;

	BUG_ON(count > ib->size);
	for (i = 0; count > IDA_BLOCK_SIZE; i++) {
		left = copy_from_user(ib->data[i], from, IDA_BLOCK_SIZE);
		if (left)
			return left + count - IDA_BLOCK_SIZE;
		from = (void __user *) from + IDA_BLOCK_SIZE;
		count -= IDA_BLOCK_SIZE;
	}
	return copy_from_user(ib->data[i], from, count);
}

#endif
