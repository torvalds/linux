/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__
#define __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__

#include "isys_stream2mmio_global.h"

typedef struct stream2mmio_state_s		stream2mmio_state_t;
typedef struct stream2mmio_sid_state_s	stream2mmio_sid_state_t;

struct stream2mmio_sid_state_s {
	hrt_data rcv_ack;
	hrt_data pix_width_id;
	hrt_data start_addr;
	hrt_data end_addr;
	hrt_data strides;
	hrt_data num_items;
	hrt_data block_when_no_cmd;
};

struct stream2mmio_state_s {
	stream2mmio_sid_state_t	sid_state[N_STREAM2MMIO_SID_ID];
};
#endif /* __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__ */
