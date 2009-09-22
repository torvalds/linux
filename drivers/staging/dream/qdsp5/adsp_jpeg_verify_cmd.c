/* arch/arm/mach-msm/qdsp5/adsp_jpeg_verify_cmd.c
 *
 * Verification code for aDSP JPEG packets from userspace.
 *
 * Copyright (c) 2008 QUALCOMM Incorporated
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/qdsp5/qdsp5jpegcmdi.h>
#include "adsp.h"

static uint32_t dec_fmt;

static inline void get_sizes(jpeg_cmd_enc_cfg *cmd, uint32_t *luma_size,
			     uint32_t *chroma_size)
{
	uint32_t fmt, luma_width, luma_height;

	fmt = cmd->process_cfg & JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_M;
	luma_width = (cmd->ip_size_cfg & JPEG_CMD_IP_SIZE_CFG_LUMA_WIDTH_M)
		      >> 16;
	luma_height = cmd->frag_cfg & JPEG_CMD_FRAG_SIZE_LUMA_HEIGHT_M;
	*luma_size = luma_width * luma_height;
	if (fmt == JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_H2V2)
		*chroma_size = *luma_size/2;
	else
		*chroma_size = *luma_size;
}

static inline int verify_jpeg_cmd_enc_cfg(struct msm_adsp_module *module,
                             		  void *cmd_data, size_t cmd_size)
{
	jpeg_cmd_enc_cfg *cmd = (jpeg_cmd_enc_cfg *)cmd_data;
	uint32_t luma_size, chroma_size;
	int i, num_frags;

	if (cmd_size != sizeof(jpeg_cmd_enc_cfg)) {
		printk(KERN_ERR "adsp: module %s: JPEG ENC CFG invalid cmd_size %d\n",
			module->name, cmd_size);
		return -1;
	}

	get_sizes(cmd, &luma_size, &chroma_size);
	num_frags = (cmd->process_cfg >> 10) & 0xf;
	num_frags = ((num_frags == 1) ? num_frags : num_frags * 2);
	for (i = 0; i < num_frags; i += 2) {
		if (adsp_pmem_fixup(module, (void **)(&cmd->frag_cfg_part[i]), luma_size) ||
		    adsp_pmem_fixup(module, (void **)(&cmd->frag_cfg_part[i+1]), chroma_size))
			return -1;
	}

	if (adsp_pmem_fixup(module, (void **)&cmd->op_buf_0_cfg_part1,
			    cmd->op_buf_0_cfg_part2) ||
	    adsp_pmem_fixup(module, (void **)&cmd->op_buf_1_cfg_part1,
			    cmd->op_buf_1_cfg_part2))
		return -1;
	return 0;
}

static inline int verify_jpeg_cmd_dec_cfg(struct msm_adsp_module *module,
					  void *cmd_data, size_t cmd_size)
{
	jpeg_cmd_dec_cfg *cmd = (jpeg_cmd_dec_cfg *)cmd_data;
	uint32_t div;

	if (cmd_size != sizeof(jpeg_cmd_dec_cfg)) {
		printk(KERN_ERR "adsp: module %s: JPEG DEC CFG invalid cmd_size %d\n",
			module->name, cmd_size);
		return -1;
	}

	if (adsp_pmem_fixup(module, (void **)&cmd->ip_stream_buf_cfg_part1,
			    cmd->ip_stream_buf_cfg_part2) ||
	    adsp_pmem_fixup(module, (void **)&cmd->op_stream_buf_0_cfg_part1,
			    cmd->op_stream_buf_0_cfg_part2) ||
	    adsp_pmem_fixup(module, (void **)&cmd->op_stream_buf_1_cfg_part1,
			    cmd->op_stream_buf_1_cfg_part2))
		return -1;
	dec_fmt = cmd->op_data_format &
		JPEG_CMD_DEC_OP_DATA_FORMAT_M;
	div = (dec_fmt == JPEG_CMD_DEC_OP_DATA_FORMAT_H2V2) ? 2 : 1;
	if (adsp_pmem_fixup(module, (void **)&cmd->op_stream_buf_0_cfg_part3,
			    cmd->op_stream_buf_0_cfg_part2 / div) ||
	    adsp_pmem_fixup(module, (void **)&cmd->op_stream_buf_1_cfg_part3,
			    cmd->op_stream_buf_1_cfg_part2 / div))
		return -1;
	return 0;
}

static int verify_jpeg_cfg_cmd(struct msm_adsp_module *module,
			       void *cmd_data, size_t cmd_size)
{
	uint32_t cmd_id = ((uint32_t *)cmd_data)[0];
	switch(cmd_id) {
	case JPEG_CMD_ENC_CFG:
		return verify_jpeg_cmd_enc_cfg(module, cmd_data, cmd_size);
	case JPEG_CMD_DEC_CFG:
		return verify_jpeg_cmd_dec_cfg(module, cmd_data, cmd_size);
	default:
		if (cmd_id > 1) {
			printk(KERN_ERR "adsp: module %s: invalid JPEG CFG cmd_id %d\n", module->name, cmd_id);
			return -1;
		}
	}
	return 0;
}

static int verify_jpeg_action_cmd(struct msm_adsp_module *module,
				  void *cmd_data, size_t cmd_size)
{
	uint32_t cmd_id = ((uint32_t *)cmd_data)[0];
	switch (cmd_id) {
	case JPEG_CMD_ENC_OP_CONSUMED:
	{
		jpeg_cmd_enc_op_consumed *cmd =
			(jpeg_cmd_enc_op_consumed *)cmd_data;

		if (cmd_size != sizeof(jpeg_cmd_enc_op_consumed)) {
			printk(KERN_ERR "adsp: module %s: JPEG_CMD_ENC_OP_CONSUMED invalid size %d\n",
				module->name, cmd_size);
			return -1;
		}

		if (adsp_pmem_fixup(module, (void **)&cmd->op_buf_addr,
				    cmd->op_buf_size))
			return -1;
	}
	break;
	case JPEG_CMD_DEC_OP_CONSUMED:
	{
		uint32_t div;
		jpeg_cmd_dec_op_consumed *cmd =
			(jpeg_cmd_dec_op_consumed *)cmd_data;

		if (cmd_size != sizeof(jpeg_cmd_enc_op_consumed)) {
			printk(KERN_ERR "adsp: module %s: JPEG_CMD_DEC_OP_CONSUMED invalid size %d\n",
				module->name, cmd_size);
			return -1;
		}

		div = (dec_fmt == JPEG_CMD_DEC_OP_DATA_FORMAT_H2V2) ?  2 : 1;
		if (adsp_pmem_fixup(module, (void **)&cmd->luma_op_buf_addr,
				    cmd->luma_op_buf_size) ||
		    adsp_pmem_fixup(module, (void **)&cmd->chroma_op_buf_addr,
				    cmd->luma_op_buf_size / div))
			return -1;
	}
	break;
	default:
		if (cmd_id > 7) {
			printk(KERN_ERR "adsp: module %s: invalid cmd_id %d\n",
				module->name, cmd_id);
			return -1;
		}
	}
	return 0;
}

int adsp_jpeg_verify_cmd(struct msm_adsp_module *module,
			 unsigned int queue_id, void *cmd_data,
			 size_t cmd_size)
{
	switch(queue_id) {
	case QDSP_uPJpegCfgCmdQueue:
		return verify_jpeg_cfg_cmd(module, cmd_data, cmd_size);
	case QDSP_uPJpegActionCmdQueue:
		return verify_jpeg_action_cmd(module, cmd_data, cmd_size);
	default:
		return -1;
	}
}

