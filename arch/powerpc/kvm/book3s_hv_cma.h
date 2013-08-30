/*
 * Contiguous Memory Allocator for ppc KVM hash pagetable  based on CMA
 * for DMA mapping framework
 *
 * Copyright IBM Corporation, 2013
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 *
 */

#ifndef __POWERPC_KVM_CMA_ALLOC_H__
#define __POWERPC_KVM_CMA_ALLOC_H__
/*
 * Both RMA and Hash page allocation will be multiple of 256K.
 */
#define KVM_CMA_CHUNK_ORDER	18

extern struct page *kvm_alloc_cma(unsigned long nr_pages,
				  unsigned long align_pages);
extern bool kvm_release_cma(struct page *pages, unsigned long nr_pages);
extern long kvm_cma_declare_contiguous(phys_addr_t size,
				       phys_addr_t alignment) __init;
#endif
