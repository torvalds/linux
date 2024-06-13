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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>

/* Device specific zlib function definitions */
#include "zip_device.h"

/* ZIP device definitions */
#include "zip_main.h"

/* ZIP memory allocation/deallocation related definitions */
#include "zip_mem.h"

/* Device specific structure definitions */
#include "zip_regs.h"

#define ZIP_ERROR    -1

#define ZIP_FLUSH_FINISH  4

#define RAW_FORMAT		0  /* for rawpipe */
#define ZLIB_FORMAT		1  /* for zpipe */
#define GZIP_FORMAT		2  /* for gzpipe */
#define LZS_FORMAT		3  /* for lzspipe */

/* Max number of ZIP devices supported */
#define MAX_ZIP_DEVICES		2

/* Configures the number of zip queues to be used */
#define ZIP_NUM_QUEUES		2

#define DYNAMIC_STOP_EXCESS	1024

/* Maximum buffer sizes in direct mode */
#define MAX_INPUT_BUFFER_SIZE   (64 * 1024)
#define MAX_OUTPUT_BUFFER_SIZE  (64 * 1024)

/**
 * struct zip_operation - common data structure for comp and decomp operations
 * @input:               Next input byte is read from here
 * @output:              Next output byte written here
 * @ctx_addr:            Inflate context buffer address
 * @history:             Pointer to the history buffer
 * @input_len:           Number of bytes available at next_in
 * @input_total_len:     Total number of input bytes read
 * @output_len:          Remaining free space at next_out
 * @output_total_len:    Total number of bytes output so far
 * @csum:                Checksum value of the uncompressed data
 * @flush:               Flush flag
 * @format:              Format (depends on stream's wrap)
 * @speed:               Speed depends on stream's level
 * @ccode:               Compression code ( stream's strategy)
 * @lzs_flag:            Flag for LZS support
 * @begin_file:          Beginning of file indication for inflate
 * @history_len:         Size of the history data
 * @end_file:            Ending of the file indication for inflate
 * @compcode:            Completion status of the ZIP invocation
 * @bytes_read:          Input bytes read in current instruction
 * @bits_processed:      Total bits processed for entire file
 * @sizeofptr:           To distinguish between ILP32 and LP64
 * @sizeofzops:          Optional just for padding
 *
 * This structure is used to maintain the required meta data for the
 * comp and decomp operations.
 */
struct zip_operation {
	u8    *input;
	u8    *output;
	u64   ctx_addr;
	u64   history;

	u32   input_len;
	u32   input_total_len;

	u32   output_len;
	u32   output_total_len;

	u32   csum;
	u32   flush;

	u32   format;
	u32   speed;
	u32   ccode;
	u32   lzs_flag;

	u32   begin_file;
	u32   history_len;

	u32   end_file;
	u32   compcode;
	u32   bytes_read;
	u32   bits_processed;

	u32   sizeofptr;
	u32   sizeofzops;
};

static inline int zip_poll_result(union zip_zres_s *result)
{
	int retries = 1000;

	while (!result->s.compcode) {
		if (!--retries) {
			pr_err("ZIP ERR: request timed out");
			return -ETIMEDOUT;
		}
		udelay(10);
		/*
		 * Force re-reading of compcode which is updated
		 * by the ZIP coprocessor.
		 */
		rmb();
	}
	return 0;
}

/* error messages */
#define zip_err(fmt, args...) pr_err("ZIP ERR:%s():%d: " \
			      fmt "\n", __func__, __LINE__, ## args)

#ifdef MSG_ENABLE
/* Enable all messages */
#define zip_msg(fmt, args...) pr_info("ZIP_MSG:" fmt "\n", ## args)
#else
#define zip_msg(fmt, args...)
#endif

#if defined(ZIP_DEBUG_ENABLE) && defined(MSG_ENABLE)

#ifdef DEBUG_LEVEL

#define FILE_NAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : \
	strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#if DEBUG_LEVEL >= 4

#define zip_dbg(fmt, args...) pr_info("ZIP DBG: %s: %s() : %d: " \
			      fmt "\n", FILE_NAME, __func__, __LINE__, ## args)

#elif DEBUG_LEVEL >= 3

#define zip_dbg(fmt, args...) pr_info("ZIP DBG: %s: %s() : %d: " \
			      fmt "\n", FILE_NAME, __func__, __LINE__, ## args)

#elif DEBUG_LEVEL >= 2

#define zip_dbg(fmt, args...) pr_info("ZIP DBG: %s() : %d: " \
			      fmt "\n", __func__, __LINE__, ## args)

#else

#define zip_dbg(fmt, args...) pr_info("ZIP DBG:" fmt "\n", ## args)

#endif /* DEBUG LEVEL >=4 */

#else

#define zip_dbg(fmt, args...) pr_info("ZIP DBG:" fmt "\n", ## args)

#endif /* DEBUG_LEVEL */
#else

#define zip_dbg(fmt, args...)

#endif /* ZIP_DEBUG_ENABLE && MSG_ENABLE*/

#endif
