/*
 * resourcecleanup.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <dspbridge/nodepriv.h>
#include <dspbridge/drv.h>

extern int drv_remove_all_dmm_res_elements(void *process_ctxt);

extern int drv_remove_all_node_res_elements(void *process_ctxt);

extern int drv_remove_all_resources(void *process_ctxt);

extern int drv_insert_node_res_element(void *hnode, void *node_resource,
					      void *process_ctxt);

extern void drv_proc_node_update_heap_status(void *node_resource, s32 status);

extern void drv_proc_node_update_status(void *node_resource, s32 status);

extern int drv_proc_update_strm_res(u32 num_bufs, void *strm_resources);

extern int drv_proc_insert_strm_res_element(void *stream_obj,
						   void *strm_res,
						   void *process_ctxt);

extern int drv_remove_all_strm_res_elements(void *process_ctxt);

extern enum node_state node_get_state(void *hnode);
