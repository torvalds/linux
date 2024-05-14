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

#include <linux/delay.h>
#include <linux/sched.h>

#include "common.h"
#include "zip_deflate.h"

/* Prepares the deflate zip command */
static int prepare_zip_command(struct zip_operation *zip_ops,
			       struct zip_state *s, union zip_inst_s *zip_cmd)
{
	union zip_zres_s *result_ptr = &s->result;

	memset(zip_cmd, 0, sizeof(s->zip_cmd));
	memset(result_ptr, 0, sizeof(s->result));

	/* IWORD #0 */
	/* History gather */
	zip_cmd->s.hg = 0;
	/* compression enable = 1 for deflate */
	zip_cmd->s.ce = 1;
	/* sf (sync flush) */
	zip_cmd->s.sf = 1;
	/* ef (end of file) */
	if (zip_ops->flush == ZIP_FLUSH_FINISH) {
		zip_cmd->s.ef = 1;
		zip_cmd->s.sf = 0;
	}

	zip_cmd->s.cc = zip_ops->ccode;
	/* ss (compression speed/storage) */
	zip_cmd->s.ss = zip_ops->speed;

	/* IWORD #1 */
	/* adler checksum */
	zip_cmd->s.adlercrc32 = zip_ops->csum;
	zip_cmd->s.historylength = zip_ops->history_len;
	zip_cmd->s.dg = 0;

	/* IWORD # 6 and 7 - compression input/history pointer */
	zip_cmd->s.inp_ptr_addr.s.addr  = __pa(zip_ops->input);
	zip_cmd->s.inp_ptr_ctl.s.length = (zip_ops->input_len +
					   zip_ops->history_len);
	zip_cmd->s.ds = 0;

	/* IWORD # 8 and 9 - Output pointer */
	zip_cmd->s.out_ptr_addr.s.addr  = __pa(zip_ops->output);
	zip_cmd->s.out_ptr_ctl.s.length = zip_ops->output_len;
	/* maximum number of output-stream bytes that can be written */
	zip_cmd->s.totaloutputlength    = zip_ops->output_len;

	/* IWORD # 10 and 11 - Result pointer */
	zip_cmd->s.res_ptr_addr.s.addr = __pa(result_ptr);
	/* Clearing completion code */
	result_ptr->s.compcode = 0;

	return 0;
}

/**
 * zip_deflate - API to offload deflate operation to hardware
 * @zip_ops: Pointer to zip operation structure
 * @s:       Pointer to the structure representing zip state
 * @zip_dev: Pointer to zip device structure
 *
 * This function prepares the zip deflate command and submits it to the zip
 * engine for processing.
 *
 * Return: 0 if successful or error code
 */
int zip_deflate(struct zip_operation *zip_ops, struct zip_state *s,
		struct zip_device *zip_dev)
{
	union zip_inst_s *zip_cmd = &s->zip_cmd;
	union zip_zres_s *result_ptr = &s->result;
	u32 queue;

	/* Prepares zip command based on the input parameters */
	prepare_zip_command(zip_ops, s, zip_cmd);

	atomic64_add(zip_ops->input_len, &zip_dev->stats.comp_in_bytes);
	/* Loads zip command into command queues and rings door bell */
	queue = zip_load_instr(zip_cmd, zip_dev);

	/* Stats update for compression requests submitted */
	atomic64_inc(&zip_dev->stats.comp_req_submit);

	/* Wait for completion or error */
	zip_poll_result(result_ptr);

	/* Stats update for compression requests completed */
	atomic64_inc(&zip_dev->stats.comp_req_complete);

	zip_ops->compcode = result_ptr->s.compcode;
	switch (zip_ops->compcode) {
	case ZIP_CMD_NOTDONE:
		zip_dbg("Zip instruction not yet completed");
		return ZIP_ERROR;

	case ZIP_CMD_SUCCESS:
		zip_dbg("Zip instruction completed successfully");
		zip_update_cmd_bufs(zip_dev, queue);
		break;

	case ZIP_CMD_DTRUNC:
		zip_dbg("Output Truncate error");
		/* Returning ZIP_ERROR to avoid copy to user */
		return ZIP_ERROR;

	default:
		zip_err("Zip instruction failed. Code:%d", zip_ops->compcode);
		return ZIP_ERROR;
	}

	/* Update the CRC depending on the format */
	switch (zip_ops->format) {
	case RAW_FORMAT:
		zip_dbg("RAW Format: %d ", zip_ops->format);
		/* Get checksum from engine, need to feed it again */
		zip_ops->csum = result_ptr->s.adler32;
		break;

	case ZLIB_FORMAT:
		zip_dbg("ZLIB Format: %d ", zip_ops->format);
		zip_ops->csum = result_ptr->s.adler32;
		break;

	case GZIP_FORMAT:
		zip_dbg("GZIP Format: %d ", zip_ops->format);
		zip_ops->csum = result_ptr->s.crc32;
		break;

	case LZS_FORMAT:
		zip_dbg("LZS Format: %d ", zip_ops->format);
		break;

	default:
		zip_err("Unknown Format:%d\n", zip_ops->format);
	}

	atomic64_add(result_ptr->s.totalbyteswritten,
		     &zip_dev->stats.comp_out_bytes);

	/* Update output_len */
	if (zip_ops->output_len < result_ptr->s.totalbyteswritten) {
		/* Dynamic stop && strm->output_len < zipconstants[onfsize] */
		zip_err("output_len (%d) < total bytes written(%d)\n",
			zip_ops->output_len, result_ptr->s.totalbyteswritten);
		zip_ops->output_len = 0;

	} else {
		zip_ops->output_len = result_ptr->s.totalbyteswritten;
	}

	return 0;
}
