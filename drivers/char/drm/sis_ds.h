/* sis_ds.h -- Private header for Direct Rendering Manager -*- linux-c -*-
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

#ifndef __SIS_DS_H__
#define __SIS_DS_H__

/* Set Data Structure */

#define SET_SIZE 5000

typedef unsigned int ITEM_TYPE;

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

set_t *setInit(void);
int setAdd(set_t *set, ITEM_TYPE item);
int setDel(set_t *set, ITEM_TYPE item);
int setFirst(set_t *set, ITEM_TYPE *item);
int setNext(set_t *set, ITEM_TYPE *item);
int setDestroy(set_t *set);

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

struct mem_block_t {
	struct mem_block_t *next;
	struct mem_block_t *heap;
	int ofs,size;
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
memHeap_t *mmInit( int ofs, int size );

/*
 * Allocate 'size' bytes with 2^align2 bytes alignment,
 * restrict the search to free memory after 'startSearch'
 * depth and back buffers should be in different 4mb banks
 * to get better page hits if possible
 * input:	size = size of block
 *       	align2 = 2^align2 bytes alignment
 *		startSearch = linear offset from start of heap to begin search
 * return: pointer to the allocated block, 0 if error
 */
PMemBlock mmAllocMem( memHeap_t *heap, int size, int align2, int startSearch );

/*
 * Returns 1 if the block 'b' is part of the heap 'heap'
 */
int mmBlockInHeap( PMemBlock heap, PMemBlock b );

/*
 * Free block starts at offset
 * input: pointer to a block
 * return: 0 if OK, -1 if error
 */
int mmFreeMem( PMemBlock b );

/* For debuging purpose. */
void mmDumpMemInfo( memHeap_t *mmInit );

#endif /* __SIS_DS_H__ */
