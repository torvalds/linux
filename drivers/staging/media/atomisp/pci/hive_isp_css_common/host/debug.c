// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2016, Intel Corporation.
 */

#include "debug.h"

#include "hmm.h"

#ifndef __INLINE_DEBUG__
#include "debug_private.h"
#endif /* __INLINE_DEBUG__ */

#define __INLINE_SP__
#include "sp.h"

#include "assert_support.h"

/* The address of the remote copy */
hrt_address	debug_buffer_address = (hrt_address) - 1;
ia_css_ptr	debug_buffer_ddr_address = (ia_css_ptr)-1;
/* The local copy */
static debug_data_t		debug_data;
debug_data_t		*debug_data_ptr = &debug_data;

void debug_buffer_init(const hrt_address addr)
{
	debug_buffer_address = addr;

	debug_data.head = 0;
	debug_data.tail = 0;
}

void debug_buffer_ddr_init(const ia_css_ptr addr)
{
	debug_buf_mode_t mode = DEBUG_BUFFER_MODE_LINEAR;
	u32 enable = 1;
	u32 head = 0;
	u32 tail = 0;
	/* set the ddr queue */
	debug_buffer_ddr_address = addr;
	hmm_store(addr + DEBUG_DATA_BUF_MODE_DDR_ADDR,
		   &mode, sizeof(debug_buf_mode_t));
	hmm_store(addr + DEBUG_DATA_HEAD_DDR_ADDR,
		   &head, sizeof(uint32_t));
	hmm_store(addr + DEBUG_DATA_TAIL_DDR_ADDR,
		   &tail, sizeof(uint32_t));
	hmm_store(addr + DEBUG_DATA_ENABLE_DDR_ADDR,
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
