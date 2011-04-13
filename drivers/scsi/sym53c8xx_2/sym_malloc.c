/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sym_glue.h"

/*
 *  Simple power of two buddy-like generic allocator.
 *  Provides naturally aligned memory chunks.
 *
 *  This simple code is not intended to be fast, but to 
 *  provide power of 2 aligned memory allocations.
 *  Since the SCRIPTS processor only supplies 8 bit arithmetic, 
 *  this allocator allows simple and fast address calculations  
 *  from the SCRIPTS code. In addition, cache line alignment 
 *  is guaranteed for power of 2 cache line size.
 *
 *  This allocator has been developed for the Linux sym53c8xx  
 *  driver, since this O/S does not provide naturally aligned 
 *  allocations.
 *  It has the advantage of allowing the driver to use private 
 *  pages of memory that will be useful if we ever need to deal 
 *  with IO MMUs for PCI.
 */
static void *___sym_malloc(m_pool_p mp, int size)
{
	int i = 0;
	int s = (1 << SYM_MEM_SHIFT);
	int j;
	void *a;
	m_link_p h = mp->h;

	if (size > SYM_MEM_CLUSTER_SIZE)
		return NULL;

	while (size > s) {
		s <<= 1;
		++i;
	}

	j = i;
	while (!h[j].next) {
		if (s == SYM_MEM_CLUSTER_SIZE) {
			h[j].next = (m_link_p) M_GET_MEM_CLUSTER();
			if (h[j].next)
				h[j].next->next = NULL;
			break;
		}
		++j;
		s <<= 1;
	}
	a = h[j].next;
	if (a) {
		h[j].next = h[j].next->next;
		while (j > i) {
			j -= 1;
			s >>= 1;
			h[j].next = (m_link_p) (a+s);
			h[j].next->next = NULL;
		}
	}
#ifdef DEBUG
	printf("___sym_malloc(%d) = %p\n", size, (void *) a);
#endif
	return a;
}

/*
 *  Counter-part of the generic allocator.
 */
static void ___sym_mfree(m_pool_p mp, void *ptr, int size)
{
	int i = 0;
	int s = (1 << SYM_MEM_SHIFT);
	m_link_p q;
	unsigned long a, b;
	m_link_p h = mp->h;

#ifdef DEBUG
	printf("___sym_mfree(%p, %d)\n", ptr, size);
#endif

	if (size > SYM_MEM_CLUSTER_SIZE)
		return;

	while (size > s) {
		s <<= 1;
		++i;
	}

	a = (unsigned long)ptr;

	while (1) {
		if (s == SYM_MEM_CLUSTER_SIZE) {
#ifdef SYM_MEM_FREE_UNUSED
			M_FREE_MEM_CLUSTER((void *)a);
#else
			((m_link_p) a)->next = h[i].next;
			h[i].next = (m_link_p) a;
#endif
			break;
		}
		b = a ^ s;
		q = &h[i];
		while (q->next && q->next != (m_link_p) b) {
			q = q->next;
		}
		if (!q->next) {
			((m_link_p) a)->next = h[i].next;
			h[i].next = (m_link_p) a;
			break;
		}
		q->next = q->next->next;
		a = a & b;
		s <<= 1;
		++i;
	}
}

/*
 *  Verbose and zeroing allocator that wrapps to the generic allocator.
 */
static void *__sym_calloc2(m_pool_p mp, int size, char *name, int uflags)
{
	void *p;

	p = ___sym_malloc(mp, size);

	if (DEBUG_FLAGS & DEBUG_ALLOC) {
		printf ("new %-10s[%4d] @%p.\n", name, size, p);
	}

	if (p)
		memset(p, 0, size);
	else if (uflags & SYM_MEM_WARN)
		printf ("__sym_calloc2: failed to allocate %s[%d]\n", name, size);
	return p;
}
#define __sym_calloc(mp, s, n)	__sym_calloc2(mp, s, n, SYM_MEM_WARN)

/*
 *  Its counter-part.
 */
static void __sym_mfree(m_pool_p mp, void *ptr, int size, char *name)
{
	if (DEBUG_FLAGS & DEBUG_ALLOC)
		printf ("freeing %-10s[%4d] @%p.\n", name, size, ptr);

	___sym_mfree(mp, ptr, size);
}

/*
 *  Default memory pool we donnot need to involve in DMA.
 *
 *  With DMA abstraction, we use functions (methods), to 
 *  distinguish between non DMAable memory and DMAable memory.
 */
static void *___mp0_get_mem_cluster(m_pool_p mp)
{
	void *m = sym_get_mem_cluster();
	if (m)
		++mp->nump;
	return m;
}

#ifdef	SYM_MEM_FREE_UNUSED
static void ___mp0_free_mem_cluster(m_pool_p mp, void *m)
{
	sym_free_mem_cluster(m);
	--mp->nump;
}
#else
#define ___mp0_free_mem_cluster NULL
#endif

static struct sym_m_pool mp0 = {
	NULL,
	___mp0_get_mem_cluster,
	___mp0_free_mem_cluster
};

/*
 *  Methods that maintains DMAable pools according to user allocations.
 *  New pools are created on the fly when a new pool id is provided.
 *  They are deleted on the fly when they get emptied.
 */
/* Get a memory cluster that matches the DMA constraints of a given pool */
static void * ___get_dma_mem_cluster(m_pool_p mp)
{
	m_vtob_p vbp;
	void *vaddr;

	vbp = __sym_calloc(&mp0, sizeof(*vbp), "VTOB");
	if (!vbp)
		goto out_err;

	vaddr = sym_m_get_dma_mem_cluster(mp, vbp);
	if (vaddr) {
		int hc = VTOB_HASH_CODE(vaddr);
		vbp->next = mp->vtob[hc];
		mp->vtob[hc] = vbp;
		++mp->nump;
	}
	return vaddr;
out_err:
	return NULL;
}

#ifdef	SYM_MEM_FREE_UNUSED
/* Free a memory cluster and associated resources for DMA */
static void ___free_dma_mem_cluster(m_pool_p mp, void *m)
{
	m_vtob_p *vbpp, vbp;
	int hc = VTOB_HASH_CODE(m);

	vbpp = &mp->vtob[hc];
	while (*vbpp && (*vbpp)->vaddr != m)
		vbpp = &(*vbpp)->next;
	if (*vbpp) {
		vbp = *vbpp;
		*vbpp = (*vbpp)->next;
		sym_m_free_dma_mem_cluster(mp, vbp);
		__sym_mfree(&mp0, vbp, sizeof(*vbp), "VTOB");
		--mp->nump;
	}
}
#endif

/* Fetch the memory pool for a given pool id (i.e. DMA constraints) */
static inline m_pool_p ___get_dma_pool(m_pool_ident_t dev_dmat)
{
	m_pool_p mp;
	for (mp = mp0.next;
		mp && !sym_m_pool_match(mp->dev_dmat, dev_dmat);
			mp = mp->next);
	return mp;
}

/* Create a new memory DMAable pool (when fetch failed) */
static m_pool_p ___cre_dma_pool(m_pool_ident_t dev_dmat)
{
	m_pool_p mp = __sym_calloc(&mp0, sizeof(*mp), "MPOOL");
	if (mp) {
		mp->dev_dmat = dev_dmat;
		mp->get_mem_cluster = ___get_dma_mem_cluster;
#ifdef	SYM_MEM_FREE_UNUSED
		mp->free_mem_cluster = ___free_dma_mem_cluster;
#endif
		mp->next = mp0.next;
		mp0.next = mp;
		return mp;
	}
	return NULL;
}

#ifdef	SYM_MEM_FREE_UNUSED
/* Destroy a DMAable memory pool (when got emptied) */
static void ___del_dma_pool(m_pool_p p)
{
	m_pool_p *pp = &mp0.next;

	while (*pp && *pp != p)
		pp = &(*pp)->next;
	if (*pp) {
		*pp = (*pp)->next;
		__sym_mfree(&mp0, p, sizeof(*p), "MPOOL");
	}
}
#endif

/* This lock protects only the memory allocation/free.  */
static DEFINE_SPINLOCK(sym53c8xx_lock);

/*
 *  Actual allocator for DMAable memory.
 */
void *__sym_calloc_dma(m_pool_ident_t dev_dmat, int size, char *name)
{
	unsigned long flags;
	m_pool_p mp;
	void *m = NULL;

	spin_lock_irqsave(&sym53c8xx_lock, flags);
	mp = ___get_dma_pool(dev_dmat);
	if (!mp)
		mp = ___cre_dma_pool(dev_dmat);
	if (!mp)
		goto out;
	m = __sym_calloc(mp, size, name);
#ifdef	SYM_MEM_FREE_UNUSED
	if (!mp->nump)
		___del_dma_pool(mp);
#endif

 out:
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
	return m;
}

void __sym_mfree_dma(m_pool_ident_t dev_dmat, void *m, int size, char *name)
{
	unsigned long flags;
	m_pool_p mp;

	spin_lock_irqsave(&sym53c8xx_lock, flags);
	mp = ___get_dma_pool(dev_dmat);
	if (!mp)
		goto out;
	__sym_mfree(mp, m, size, name);
#ifdef	SYM_MEM_FREE_UNUSED
	if (!mp->nump)
		___del_dma_pool(mp);
#endif
 out:
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
}

/*
 *  Actual virtual to bus physical address translator 
 *  for 32 bit addressable DMAable memory.
 */
dma_addr_t __vtobus(m_pool_ident_t dev_dmat, void *m)
{
	unsigned long flags;
	m_pool_p mp;
	int hc = VTOB_HASH_CODE(m);
	m_vtob_p vp = NULL;
	void *a = (void *)((unsigned long)m & ~SYM_MEM_CLUSTER_MASK);
	dma_addr_t b;

	spin_lock_irqsave(&sym53c8xx_lock, flags);
	mp = ___get_dma_pool(dev_dmat);
	if (mp) {
		vp = mp->vtob[hc];
		while (vp && vp->vaddr != a)
			vp = vp->next;
	}
	if (!vp)
		panic("sym: VTOBUS FAILED!\n");
	b = vp->baddr + (m - a);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
	return b;
}
