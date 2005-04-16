/* sis_ds.c -- Private header for Direct Rendering Manager -*- linux-c -*-
 * Created: Mon Jan  4 10:05:05 1999 by sclin@sis.com.tw
 *
 * Copyright 2000 Silicon Integrated Systems Corp, Inc., HsinChu, Taiwan.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * Authors:
 *    Sung-Ching Lin <sclin@sis.com.tw>
 * 
 */

#include "drmP.h"
#include "drm.h"
#include "sis_ds.h"

/* Set Data Structure, not check repeated value
 * temporarily used
 */

set_t *setInit(void)
{
	int i;
	set_t *set;

	set = (set_t *)drm_alloc(sizeof(set_t), DRM_MEM_DRIVER);
	if (set != NULL) {
		for (i = 0; i < SET_SIZE; i++) {
			set->list[i].free_next = i + 1;    
			set->list[i].alloc_next = -1;
		}
		set->list[SET_SIZE-1].free_next = -1;
		set->free = 0;
		set->alloc = -1;
		set->trace = -1;
	}
	return set;
}

int setAdd(set_t *set, ITEM_TYPE item)
{
	int free = set->free;
  
	if (free != -1) {
		set->list[free].val = item;
		set->free = set->list[free].free_next;
	} else {
		return 0;
	}

	set->list[free].alloc_next = set->alloc;
	set->alloc = free;  
	set->list[free].free_next = -1;    

	return 1;
}

int setDel(set_t *set, ITEM_TYPE item)
{
	int alloc = set->alloc;
	int prev = -1;  

	while (alloc != -1) {
		if (set->list[alloc].val == item) {
			if (prev != -1)
				set->list[prev].alloc_next =
				    set->list[alloc].alloc_next;
			else
				set->alloc = set->list[alloc].alloc_next;
			break;
		}
		prev = alloc;
		alloc = set->list[alloc].alloc_next;
	}

	if (alloc == -1)
		return 0;

	set->list[alloc].free_next = set->free;
	set->free = alloc;
	set->list[alloc].alloc_next = -1;

	return 1;
}

/* setFirst -> setAdd -> setNext is wrong */

int setFirst(set_t *set, ITEM_TYPE *item)
{
	if (set->alloc == -1)
		return 0;

	*item = set->list[set->alloc].val;
	set->trace = set->list[set->alloc].alloc_next;

	return 1;
}

int setNext(set_t *set, ITEM_TYPE *item)
{
	if (set->trace == -1)
		return 0;

	*item = set->list[set->trace].val;
	set->trace = set->list[set->trace].alloc_next;

	return 1;
}

int setDestroy(set_t *set)
{
	drm_free(set, sizeof(set_t), DRM_MEM_DRIVER);

	return 1;
}

/*
 * GLX Hardware Device Driver common code
 * Copyright (C) 1999 Wittawat Yamwong
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * WITTAWAT YAMWONG, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#define ISFREE(bptr) ((bptr)->free)

memHeap_t *mmInit(int ofs,
		  int size)
{
	PMemBlock blocks;

	if (size <= 0)
		return NULL;

	blocks = (TMemBlock *)drm_calloc(1, sizeof(TMemBlock), DRM_MEM_DRIVER);
	if (blocks != NULL) {
		blocks->ofs = ofs;
		blocks->size = size;
		blocks->free = 1;
		return (memHeap_t *)blocks;
	} else
		return NULL;
}

/* Checks if a pointer 'b' is part of the heap 'heap' */
int mmBlockInHeap(memHeap_t *heap, PMemBlock b)
{
	TMemBlock *p;

	if (heap == NULL || b == NULL)
		return 0;

	p = heap;
	while (p != NULL && p != b) {
		p = p->next;
	}
	if (p == b)
		return 1;
	else
		return 0;
}

static TMemBlock* SliceBlock(TMemBlock *p, 
			     int startofs, int size, 
			     int reserved, int alignment)
{
	TMemBlock *newblock;

	/* break left */
	if (startofs > p->ofs) {
		newblock = (TMemBlock*) drm_calloc(1, sizeof(TMemBlock),
		    DRM_MEM_DRIVER);
		newblock->ofs = startofs;
		newblock->size = p->size - (startofs - p->ofs);
		newblock->free = 1;
		newblock->next = p->next;
		p->size -= newblock->size;
		p->next = newblock;
		p = newblock;
	}

	/* break right */
	if (size < p->size) {
		newblock = (TMemBlock*) drm_calloc(1, sizeof(TMemBlock),
		    DRM_MEM_DRIVER);
		newblock->ofs = startofs + size;
		newblock->size = p->size - size;
		newblock->free = 1;
		newblock->next = p->next;
		p->size = size;
		p->next = newblock;
	}

	/* p = middle block */
	p->align = alignment;
	p->free = 0;
	p->reserved = reserved;
	return p;
}

PMemBlock mmAllocMem( memHeap_t *heap, int size, int align2, int startSearch)
{
	int mask,startofs, endofs;
	TMemBlock *p;
	
	if (heap == NULL || align2 < 0 || size <= 0)
		return NULL;

	mask = (1 << align2)-1;
	startofs = 0;
	p = (TMemBlock *)heap;
	while (p != NULL) {
		if (ISFREE(p)) {
			startofs = (p->ofs + mask) & ~mask;
			if ( startofs < startSearch ) {
				startofs = startSearch;
			}
			endofs = startofs+size;
			if (endofs <= (p->ofs+p->size))
				break;
		}
		p = p->next;
	}
	if (p == NULL)
		return NULL;
	p = SliceBlock(p,startofs,size,0,mask+1);
	p->heap = heap;
	return p;
}

static __inline__ int Join2Blocks(TMemBlock *p)
{
	if (p->free && p->next && p->next->free) {
		TMemBlock *q = p->next;
		p->size += q->size;
		p->next = q->next;
		drm_free(q, sizeof(TMemBlock), DRM_MEM_DRIVER);
		return 1;
	}
	return 0;
}

int mmFreeMem(PMemBlock b)
{
	TMemBlock *p, *prev;

	if (b == NULL)
		return 0;
	if (b->heap == NULL)
		return -1;

	p = b->heap;
	prev = NULL;
	while (p != NULL && p != b) {
		prev = p;
		p = p->next;
	}
	if (p == NULL || p->free || p->reserved)
		return -1;

	p->free = 1;
	Join2Blocks(p);
	if (prev)
	Join2Blocks(prev);
	return 0;
}

