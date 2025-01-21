// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "system_global.h"

#include "assert_support.h"
#include "platform_support.h"
#include "ia_css_isys.h"
#include "ibuf_ctrl_rmgr.h"

static ibuf_rsrc_t	ibuf_rsrc;

static ibuf_handle_t *getHandle(uint16_t index)
{
	ibuf_handle_t *handle = NULL;

	if (index < MAX_IBUF_HANDLES)
		handle = &ibuf_rsrc.handles[index];
	return handle;
}

void ia_css_isys_ibuf_rmgr_init(void)
{
	memset(&ibuf_rsrc, 0, sizeof(ibuf_rsrc));
	ibuf_rsrc.free_size = MAX_INPUT_BUFFER_SIZE;
}

void ia_css_isys_ibuf_rmgr_uninit(void)
{
	memset(&ibuf_rsrc, 0, sizeof(ibuf_rsrc));
	ibuf_rsrc.free_size = MAX_INPUT_BUFFER_SIZE;
}

bool ia_css_isys_ibuf_rmgr_acquire(
    u32	size,
    uint32_t	*start_addr)
{
	bool retval = false;
	bool input_buffer_found = false;
	u32 aligned_size;
	ibuf_handle_t *handle = NULL;
	u16 i;

	assert(start_addr);
	assert(size > 0);

	aligned_size = (size + (IBUF_ALIGN - 1)) & ~(IBUF_ALIGN - 1);

	/* Check if there is an available un-used handle with the size
	 * that will fulfill the request.
	 */
	if (ibuf_rsrc.num_active < ibuf_rsrc.num_allocated) {
		for (i = 0; i < ibuf_rsrc.num_allocated; i++) {
			handle = getHandle(i);
			if (!handle->active) {
				if (handle->size >= aligned_size) {
					handle->active = true;
					input_buffer_found = true;
					ibuf_rsrc.num_active++;
					break;
				}
			}
		}
	}

	if (!input_buffer_found) {
		/* There were no available handles that fulfilled the
		 * request. Allocate a new handle with the requested size.
		 */
		if ((ibuf_rsrc.num_allocated < MAX_IBUF_HANDLES) &&
		    (ibuf_rsrc.free_size >= aligned_size)) {
			handle = getHandle(ibuf_rsrc.num_allocated);
			handle->start_addr	= ibuf_rsrc.free_start_addr;
			handle->size		= aligned_size;
			handle->active		= true;

			ibuf_rsrc.free_start_addr += aligned_size;
			ibuf_rsrc.free_size -= aligned_size;
			ibuf_rsrc.num_active++;
			ibuf_rsrc.num_allocated++;

			input_buffer_found = true;
		}
	}

	if (input_buffer_found && handle) {
		*start_addr = handle->start_addr;
		retval = true;
	}

	return retval;
}

void ia_css_isys_ibuf_rmgr_release(
    uint32_t	*start_addr)
{
	u16 i;
	ibuf_handle_t *handle = NULL;

	assert(start_addr);

	for (i = 0; i < ibuf_rsrc.num_allocated; i++) {
		handle = getHandle(i);
		if (handle->active && handle->start_addr == *start_addr) {
			handle->active = false;
			ibuf_rsrc.num_active--;
			break;
		}
	}
}
