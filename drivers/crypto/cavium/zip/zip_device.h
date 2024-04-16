/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#ifndef __ZIP_DEVICE_H__
#define __ZIP_DEVICE_H__

#include <linux/types.h>
#include "zip_main.h"

struct sg_info {
	/*
	 * Pointer to the input data when scatter_gather == 0 and
	 * pointer to the input gather list buffer when scatter_gather == 1
	 */
	union zip_zptr_s *gather;

	/*
	 * Pointer to the output data when scatter_gather == 0 and
	 * pointer to the output scatter list buffer when scatter_gather == 1
	 */
	union zip_zptr_s *scatter;

	/*
	 * Holds size of the output buffer pointed by scatter list
	 * when scatter_gather == 1
	 */
	u64 scatter_buf_size;

	/* for gather data */
	u64 gather_enable;

	/* for scatter data */
	u64 scatter_enable;

	/* Number of gather list pointers for gather data */
	u32 gbuf_cnt;

	/* Number of scatter list pointers for scatter data */
	u32 sbuf_cnt;

	/* Buffers allocation state */
	u8 alloc_state;
};

/**
 * struct zip_state - Structure representing the required information related
 *                    to a command
 * @zip_cmd: Pointer to zip instruction structure
 * @result:  Pointer to zip result structure
 * @ctx:     Context pointer for inflate
 * @history: Decompression history pointer
 * @sginfo:  Scatter-gather info structure
 */
struct zip_state {
	union zip_inst_s zip_cmd;
	union zip_zres_s result;
	union zip_zptr_s *ctx;
	union zip_zptr_s *history;
	struct sg_info   sginfo;
};

#define ZIP_CONTEXT_SIZE          2048
#define ZIP_INFLATE_HISTORY_SIZE  32768
#define ZIP_DEFLATE_HISTORY_SIZE  32768

#endif
