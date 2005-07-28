/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2000 Silicon Integrated Systems Corp, Inc., HsinChu, Taiwan.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _via_ds_h_
#define _via_ds_h_

#include "drmP.h"

/* Set Data Structure */
#define SET_SIZE 5000
typedef unsigned long ITEM_TYPE;

typedef struct {
	ITEM_TYPE val;
	int alloc_next, free_next;
} list_item_t;

typedef struct {
	int alloc;
	int free;
	int trace;
	list_item_t list[SET_SIZE];
} set_t;

set_t *via_setInit(void);
int via_setAdd(set_t * set, ITEM_TYPE item);
int via_setDel(set_t * set, ITEM_TYPE item);
int via_setFirst(set_t * set, ITEM_TYPE * item);
int via_setNext(set_t * set, ITEM_TYPE * item);
int via_setDestroy(set_t * set);

#endif

#ifndef MM_INC
#define MM_INC

struct mem_block_t {
	struct mem_block_t *next;
	struct mem_block_t *heap;
	int ofs, size;
	int align;
	unsigned int free:1;
	unsigned int reserved:1;
};
typedef struct mem_block_t TMemBlock;
typedef struct mem_block_t *PMemBlock;

/* a heap is just the first block in a chain */
typedef struct mem_block_t memHeap_t;

static __inline__ int mmBlockSize(PMemBlock b)
{
	return b->size;
}

static __inline__ int mmOffset(PMemBlock b)
{
	return b->ofs;
}

static __inline__ void mmMarkReserved(PMemBlock b)
{
	b->reserved = 1;
}

/*
 * input: total size in bytes
 * return: a heap pointer if OK, NULL if error
 */
memHeap_t *via_mmInit(int ofs, int size);

PMemBlock via_mmAllocMem(memHeap_t * heap, int size, int align2,
			 int startSearch);

/*
 * Free block starts at offset
 * input: pointer to a block
 * return: 0 if OK, -1 if error
 */
int via_mmFreeMem(PMemBlock b);

#endif
