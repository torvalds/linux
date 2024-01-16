/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
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

#ifndef __IBUF_CTRL_RMGR_H_INCLUDED__
#define __IBUF_CTRL_RMGR_H_INCLUDED__

#define MAX_IBUF_HANDLES	24
#define MAX_INPUT_BUFFER_SIZE	(64 * 1024)
#define IBUF_ALIGN		8

typedef struct ibuf_handle_s ibuf_handle_t;
struct ibuf_handle_s {
	u32	start_addr;
	u32	size;
	bool		active;
};

typedef struct ibuf_rsrc_s ibuf_rsrc_t;
struct ibuf_rsrc_s {
	u32	free_start_addr;
	u32	free_size;
	u16	num_active;
	u16	num_allocated;
	ibuf_handle_t	handles[MAX_IBUF_HANDLES];
};

#endif /* __IBUF_CTRL_RMGR_H_INCLUDED */
