#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef __QUEUE_ACCESS_H
#define __QUEUE_ACCESS_H

#include <type_support.h>
#include <ia_css_queue_comm.h>
#include <ia_css_circbuf.h>
#include <error_support.h>

#define QUEUE_IGNORE_START_FLAG	0x0001
#define QUEUE_IGNORE_END_FLAG	0x0002
#define QUEUE_IGNORE_SIZE_FLAG	0x0004
#define QUEUE_IGNORE_STEP_FLAG	0x0008
#define QUEUE_IGNORE_DESC_FLAGS_MAX 0x000f

#define QUEUE_IGNORE_SIZE_START_STEP_FLAGS \
	(QUEUE_IGNORE_SIZE_FLAG | \
	QUEUE_IGNORE_START_FLAG | \
	QUEUE_IGNORE_STEP_FLAG)

#define QUEUE_IGNORE_SIZE_END_STEP_FLAGS \
	(QUEUE_IGNORE_SIZE_FLAG | \
	QUEUE_IGNORE_END_FLAG   | \
	QUEUE_IGNORE_STEP_FLAG)

#define QUEUE_IGNORE_START_END_STEP_FLAGS \
	(QUEUE_IGNORE_START_FLAG | \
	QUEUE_IGNORE_END_FLAG	  | \
	QUEUE_IGNORE_STEP_FLAG)

#define QUEUE_CB_DESC_INIT(cb_desc)	\
	do {				\
		(cb_desc)->size  = 0;	\
		(cb_desc)->step  = 0;	\
		(cb_desc)->start = 0;	\
		(cb_desc)->end   = 0;	\
	} while(0)

struct ia_css_queue {
	uint8_t type;        /* Specify remote/local type of access */
	uint8_t location;    /* Cell location for queue */
	uint8_t proc_id;     /* Processor id for queue access */
	union {
		ia_css_circbuf_t cb_local;
		struct {
			uint32_t cb_desc_addr; /*Circbuf desc address for remote queues*/
			uint32_t cb_elems_addr; /*Circbuf elements addr for remote queue*/
		}	remote;
	} desc;
};

extern int ia_css_queue_load(
		struct ia_css_queue *rdesc,
		ia_css_circbuf_desc_t *cb_desc,
		uint32_t ignore_desc_flags);

extern int ia_css_queue_store(
		struct ia_css_queue *rdesc,
		ia_css_circbuf_desc_t *cb_desc,
		uint32_t ignore_desc_flags);

extern int ia_css_queue_item_load(
		struct ia_css_queue *rdesc,
		uint8_t position,
		ia_css_circbuf_elem_t *item);

extern int ia_css_queue_item_store(
		struct ia_css_queue *rdesc,
		uint8_t position,
		ia_css_circbuf_elem_t *item);

#endif /* __QUEUE_ACCESS_H */
