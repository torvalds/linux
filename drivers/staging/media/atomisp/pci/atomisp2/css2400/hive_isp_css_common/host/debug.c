/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2016, Intel Corporation.
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

#include "debug.h"

#ifndef __INLINE_DEBUG__
#include "debug_private.h"
#endif /* __INLINE_DEBUG__ */

#include "memory_access.h"

#define __INLINE_SP__
#include "sp.h"

#include "assert_support.h"

/* The address of the remote copy */
hrt_address	debug_buffer_address = (hrt_address)-1;
hrt_vaddress	debug_buffer_ddr_address = (hrt_vaddress)-1;
/* The local copy */
static debug_data_t		debug_data;
debug_data_t		*debug_data_ptr = &debug_data;

void debug_buffer_init(const hrt_address addr)
{
	debug_buffer_address = addr;

	debug_data.head = 0;
	debug_data.tail = 0;
}

void debug_buffer_ddr_init(const hrt_vaddress addr)
{
	debug_buf_mode_t mode = DEBUG_BUFFER_MODE_LINEAR;
	uint32_t enable = 1;
	uint32_t head = 0;
	uint32_t tail = 0;
	/* set the ddr queue */
	debug_buffer_ddr_address = addr;
	mmgr_store(addr + DEBUG_DATA_BUF_MODE_DDR_ADDR,
				&mode, sizeof(debug_buf_mode_t));
	mmgr_store(addr + DEBUG_DATA_HEAD_DDR_ADDR,
				&head, sizeof(uint32_t));
	mmgr_store(addr + DEBUG_DATA_TAIL_DDR_ADDR,
				&tail, sizeof(uint32_t));
	mmgr_store(addr + DEBUG_DATA_ENABLE_DDR_ADDR,
				&enable, sizeof(uint32_t));

	/* set the local copy */
	debug_data.head = 0;
	debug_data.tail = 0;
}

void debug_buffer_setmode(const debug_buf_mode_t mode)
{
	assert(debug_buffer_address != ((hrt_address)-1));

	sp_dmem_store_uint32(SP0_ID,
		debug_buffer_address + DEBUG_DATA_BUF_MODE_ADDR, mode);
}

