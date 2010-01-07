/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
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

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"

#include "cvmx-fpa.h"

/**
 * Fill the supplied hardware pool with skbuffs
 *
 * @pool:     Pool to allocate an skbuff for
 * @size:     Size of the buffer needed for the pool
 * @elements: Number of buffers to allocate
 */
static int cvm_oct_fill_hw_skbuff(int pool, int size, int elements)
{
	int freed = elements;
	while (freed) {

		struct sk_buff *skb = dev_alloc_skb(size + 128);
		if (unlikely(skb == NULL)) {
			pr_warning
			    ("Failed to allocate skb for hardware pool %d\n",
			     pool);
			break;
		}

		skb_reserve(skb, 128 - (((unsigned long)skb->data) & 0x7f));
		*(struct sk_buff **)(skb->data - sizeof(void *)) = skb;
		cvmx_fpa_free(skb->data, pool, DONT_WRITEBACK(size / 128));
		freed--;
	}
	return elements - freed;
}

/**
 * Free the supplied hardware pool of skbuffs
 *
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
 * This function fills a hardware pool with memory. Depending
 * on the config defines, this memory might come from the
 * kernel or global 32bit memory allocated with
 * cvmx_bootmem_alloc.
 *
 * @pool:     Pool to populate
 * @size:     Size of each buffer in the pool
 * @elements: Number of buffers to allocate
 */
static int cvm_oct_fill_hw_memory(int pool, int size, int elements)
{
	char *memory;
	int freed = elements;

	while (freed) {
		/* We need to force alignment to 128 bytes here */
		memory = kmalloc(size + 127, GFP_ATOMIC);
		if (unlikely(memory == NULL)) {
			pr_warning("Unable to allocate %u bytes for FPA pool %d\n",
				   elements * size, pool);
			break;
		}
		memory = (char *)(((unsigned long)memory + 127) & -128);
		cvmx_fpa_free(memory, pool, 0);
		freed--;
	}
	return elements - freed;
}

/**
 * Free memory previously allocated with cvm_oct_fill_hw_memory
 *
 * @pool:     FPA pool to free
 * @size:     Size of each buffer in the pool
 * @elements: Number of buffers that should be in the pool
 */
static void cvm_oct_free_hw_memory(int pool, int size, int elements)
{
	char *memory;
	do {
		memory = cvmx_fpa_alloc(pool);
		if (memory) {
			elements--;
			kfree(phys_to_virt(cvmx_ptr_to_phys(memory)));
		}
	} while (memory);

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
	if (USE_SKBUFFS_IN_HW)
		freed = cvm_oct_fill_hw_skbuff(pool, size, elements);
	else
		freed = cvm_oct_fill_hw_memory(pool, size, elements);
	return freed;
}

void cvm_oct_mem_empty_fpa(int pool, int size, int elements)
{
	if (USE_SKBUFFS_IN_HW)
		cvm_oct_free_hw_skbuff(pool, size, elements);
	else
		cvm_oct_free_hw_memory(pool, size, elements);
}
