// SPDX-License-Identifier: GPL-2.0
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

#include "system_global.h"


#include "assert_support.h"
#include "platform_support.h"
#include "ia_css_isys.h"
#include "bitop_support.h"
#include "isys_stream2mmio_rmgr.h"

static isys_stream2mmio_rsrc_t	isys_stream2mmio_rsrc[N_STREAM2MMIO_ID];

void ia_css_isys_stream2mmio_sid_rmgr_init(void)
{
	memset(isys_stream2mmio_rsrc, 0, sizeof(isys_stream2mmio_rsrc));
}

void ia_css_isys_stream2mmio_sid_rmgr_uninit(void)
{
	memset(isys_stream2mmio_rsrc, 0, sizeof(isys_stream2mmio_rsrc));
}

bool ia_css_isys_stream2mmio_sid_rmgr_acquire(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid)
{
	bool retval = false;
	stream2mmio_sid_ID_t max_sid;
	isys_stream2mmio_rsrc_t *cur_rsrc = NULL;
	stream2mmio_sid_ID_t	i;

	assert(stream2mmio < N_STREAM2MMIO_ID);
	assert(sid);

	if ((stream2mmio < N_STREAM2MMIO_ID) && (sid)) {
		max_sid = N_STREAM2MMIO_SID_PROCS[stream2mmio];
		cur_rsrc = &isys_stream2mmio_rsrc[stream2mmio];

		if (cur_rsrc->num_active < max_sid) {
			for (i = STREAM2MMIO_SID0_ID; i < max_sid; i++) {
				if (bitop_getbit(cur_rsrc->active_table, i) == 0) {
					bitop_setbit(cur_rsrc->active_table, i);
					*sid = i;
					cur_rsrc->num_active++;
					retval = true;
					break;
				}
			}
		}
	}
	return retval;
}

void ia_css_isys_stream2mmio_sid_rmgr_release(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid)
{
	stream2mmio_sid_ID_t max_sid;
	isys_stream2mmio_rsrc_t *cur_rsrc = NULL;

	assert(stream2mmio < N_STREAM2MMIO_ID);
	assert(sid);

	if ((stream2mmio < N_STREAM2MMIO_ID) && (sid)) {
		max_sid = N_STREAM2MMIO_SID_PROCS[stream2mmio];
		cur_rsrc = &isys_stream2mmio_rsrc[stream2mmio];
		if ((*sid < max_sid) && (cur_rsrc->num_active > 0)) {
			if (bitop_getbit(cur_rsrc->active_table, *sid) == 1) {
				bitop_clearbit(cur_rsrc->active_table, *sid);
				cur_rsrc->num_active--;
			}
		}
	}
}
