/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2010 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/slab.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"

#include <asm/octeon/cvmx-fpa.h>

/**
 * cvm_oct_fill_hw_skbuff - fill the supplied hardware pool with skbuffs
 * @pool:     Pool to allocate an skbuff for
 * @size:     Size of the buffer needed for the pool
 * @elements: Number of buffers to allocate
 *
 * Returns the actual number of buffers allocated.
 */
static int cvm_oct_fill_hw_skbuff(int pool, int size, int elements)
{
	int freed = elements;
	while (freed) {

		struct sk_buff *skb = dev_alloc_skb(size + 256);
		if (unlikely(skb == NULL)) {
			pr_warning
			    ("Failed to allocate skb for hardware pool %d\n",
			     pool);
			break;
		}

		skb_reserve(skb, 256 - (((unsigned long)skb->data) & 0x7f));
		*(struct sk_buff **)(skb->data - sizeof(void *)) = skb;
		cvmx_fpa_free(skb->data, pool, DONT_WRITEBACK(size / 128));
		freed--;
	}
	return elements - freed;
}

/**
 * cvm_oct_free_hw_skbuff- free hardware pool skbuffs
 * @pool:     Pool to allocate an skbuff for
 * @size:     Size of the buffer needed for the pool
 * @elements: Number of buffers to allocate
 */
static void cvm_oct_free_hw_skbuff(int pool, int size, int elements)
{
	char *memory;

	do {
		memory = cvmx_fpa_alloc(pool);
		if (memory) {
			struct sk_buff *skb =
			    *(struct sk_buff **)(memory - sizeof(void *));
			elements--;
			dev_kfree_skb(skb);
		}
	} while (memory);

	if (elements < 0)
		pr_warning("Freeing of pool %u had too many skbuffs (%d)\n",
		     pool, elements);
	else if (elements > 0)
		pr_warning("Freeing of pool %u is missing %d skbuffs\n",
		       pool, elements);
}

/**
 * cvm_oct_fill_hw_memory - fill a hardware pool with memory.
 * @pool:     Pool to populate
 * @size:     Size of each buffer in the pool
 * @elements: Number of buffers to allocate
 *
 * Returns the actual number of buffers allocated.
 */
static int cvm_oct_fill_hw_memory(int pool, int size, int elements)
{
	char *memory;
	char *fpa;
	int freed = elements;

	while (freed) {
		/*
		 * FPA memory must be 128 byte aligned.  Since we are
		 * aligning we need to save the original pointer so we
		 * can feed it to kfree when the memory is returned to
		 * the kernel.
		 *
		 * We allocate an extra 256 bytes to allow for
		 * alignment and space for the original pointer saved
		 * just before the block.
		 */
		memory = kmalloc(size + 256, GFP_ATOMIC);
		if (unlikely(memory == NULL)) {
			pr_warning("Unable to allocate %u bytes for FPA pool %d\n",
				   elements * size, pool);
			break;
		}
		fpa = (char *)(((unsigned long)memory + 256) & ~0x7fUL);
		*((char **)fpa - 1) = memory;
		cvmx_fpa_free(fpa, pool, 0);
		freed--;
	}
	return elements - freed;
}

/**
 * cvm_oct_free_hw_memory - Free memory allocated by cvm_oct_fill_hw_memory
 * @pool:     FPA pool to free
 * @size:     Size of each buffer in the pool
 * @elements: Number of buffers that should be in the pool
 */
static void cvm_oct_free_hw_memory(int pool, int size, int elements)
{
	char *memory;
	char *fpa;
	do {
		fpa = cvmx_fpa_alloc(pool);
		if (fpa) {
			elements--;
			fpa = (char *)phys_to_virt(cvmx_ptr_to_phys(fpa));
			memory = *((char **)fpa - 1);
			kfree(memory);
		}
	} while (fpa);

	if (elements < 0)
		pr_warning("Freeing of pool %u had too many buffers (%d)\n",
			pool, elements);
	else if (elements > 0)
		pr_warning("Warning: Freeing of pool %u is missing %d buffers\n",
			pool, elements);
}

int cvm_oct_mem_fill_fpa(int pool, int size, int elements)
{
	int freed;
	if (USE_SKBUFFS_IN_HW && pool == CVMX_FPA_PACKET_POOL)
		freed = cvm_oct_fill_hw_skbuff(pool, size, elements);
	else
		freed = cvm_oct_fill_hw_memory(pool, size, elements);
	return freed;
}

void cvm_oct_mem_empty_fpa(int pool, int size, int elements)
{
	if (USE_SKBUFFS_IN_HW && pool == CVMX_FPA_PACKET_POOL)
		cvm_oct_free_hw_skbuff(pool, size, elements);
	else
		cvm_oct_free_hw_memory(pool, size, elements);
}
