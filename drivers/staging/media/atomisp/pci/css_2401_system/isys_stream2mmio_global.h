/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__
#define __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__

#include <type_support.h>

typedef struct stream2mmio_cfg_s stream2mmio_cfg_t;
struct stream2mmio_cfg_s {
	u32				bits_per_pixel;
	u32				enable_blocking;
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
