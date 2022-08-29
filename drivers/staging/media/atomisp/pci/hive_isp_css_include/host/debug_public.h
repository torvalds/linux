/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __DEBUG_PUBLIC_H_INCLUDED__
#define __DEBUG_PUBLIC_H_INCLUDED__

#include <type_support.h>
#include <ia_css_types.h>
#include "system_local.h"

/*! brief
 *
 * Simple queuing trace buffer for debug data
 * instantiatable in SP DMEM
 *
 * The buffer has a remote and a local store
 * which contain duplicate data (when in sync).
 * The buffers are automatically synched when the
 * user dequeues, or manualy using the synch function
 *
 * An alternative (storage efficient) implementation
 * could manage the buffers to contain unique data
 *
 * The buffer empty status is computed from local
 * state which does not reflect the presence of data
 * in the remote buffer (unless the alternative
 * implementation is followed)
 */

typedef struct debug_data_s		debug_data_t;
typedef struct debug_data_ddr_s	debug_data_ddr_t;

extern debug_data_t				*debug_data_ptr;
extern hrt_address				debug_buffer_address;
extern ia_css_ptr				debug_buffer_ddr_address;

/*! Check the empty state of the local debug data buffer

 \return isEmpty(buffer)
 */
STORAGE_CLASS_DEBUG_H bool is_debug_buffer_empty(void);

/*! Dequeue a token from the debug data buffer

 \return isEmpty(buffer)?0:buffer[head]
 */
STORAGE_CLASS_DEBUG_H hrt_data debug_dequeue(void);

/*! Synchronise the remote buffer to the local buffer

 \return none
 */
STORAGE_CLASS_DEBUG_H void debug_synch_queue(void);

/*! Synchronise the remote buffer to the local buffer

 \return none
 */
STORAGE_CLASS_DEBUG_H void debug_synch_queue_isp(void);

/*! Synchronise the remote buffer to the local buffer

 \return none
 */
STORAGE_CLASS_DEBUG_H void debug_synch_queue_ddr(void);

/*! Set the offset/address of the (remote) debug buffer

 \return none
 */
void debug_buffer_init(
    const hrt_address		addr);

/*! Set the offset/address of the (remote) debug buffer

 \return none
 */
void debug_buffer_ddr_init(
    const ia_css_ptr		addr);

/*! Set the (remote) operating mode of the debug buffer

 \return none
 */
void debug_buffer_setmode(
    const debug_buf_mode_t	mode);

#endif /* __DEBUG_PUBLIC_H_INCLUDED__ */
