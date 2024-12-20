/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __IA_CSS_QUEUE_COMM_H
#define __IA_CSS_QUEUE_COMM_H

#include "type_support.h"
#include "ia_css_circbuf.h"
/*****************************************************************************
 * Queue Public Data Structures
 *****************************************************************************/

/* Queue location specifier */
/* Avoiding enums to save space */
#define IA_CSS_QUEUE_LOC_HOST 0
#define IA_CSS_QUEUE_LOC_SP   1
#define IA_CSS_QUEUE_LOC_ISP  2

/* Queue type specifier */
/* Avoiding enums to save space */
#define IA_CSS_QUEUE_TYPE_LOCAL  0
#define IA_CSS_QUEUE_TYPE_REMOTE 1

/* for DDR Allocated queues,
allocate minimum these many elements.
DDR->SP' DMEM DMA transfer needs 32byte aligned address.
Since each element size is 4 bytes, 8 elements need to be
DMAed to access single element.*/
#define IA_CSS_MIN_ELEM_COUNT    8
#define IA_CSS_DMA_XFER_MASK (IA_CSS_MIN_ELEM_COUNT - 1)

/* Remote Queue object descriptor */
struct ia_css_queue_remote {
	u32 cb_desc_addr; /*Circbuf desc address for remote queues*/
	u32 cb_elems_addr; /*Circbuf elements addr for remote queue*/
	u8 location;    /* Cell location for queue */
	u8 proc_id;     /* Processor id for queue access */
};

typedef struct ia_css_queue_remote ia_css_queue_remote_t;

#endif /* __IA_CSS_QUEUE_COMM_H */
