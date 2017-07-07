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

#ifndef __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__
#define __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__

#include <type_support.h>

typedef struct stream2mmio_cfg_s stream2mmio_cfg_t;
struct stream2mmio_cfg_s {
	uint32_t				bits_per_pixel;
	uint32_t				enable_blocking;
};

/* Stream2MMIO limits  per ID*/
/*
 * Stream2MMIO 0 has 8 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID7_ID].
 *
 * Stream2MMIO 1 has 4 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...TREAM2MMIO_SID3_ID].
 *
 * Stream2MMIO 2 has 4 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID3_ID].
 */
extern const stream2mmio_sid_ID_t N_STREAM2MMIO_SID_PROCS[N_STREAM2MMIO_ID];

#endif /* __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__ */
