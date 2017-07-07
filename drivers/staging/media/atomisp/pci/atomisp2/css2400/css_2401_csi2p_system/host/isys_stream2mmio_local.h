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
	stream2mmio_sid_state_t 	sid_state[N_STREAM2MMIO_SID_ID];
};
#endif /* __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__ */
