/* arch/arm/mach-msm/qdsp5/adsp_video_verify_cmd.c
 *
 * Verificion code for aDSP VENC packets from userspace.
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
#include <linux/io.h>

#define ADSP_DEBUG_MSGS 0
#if ADSP_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
	     ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif

#include <mach/qdsp5/qdsp5venccmdi.h>
#include "adsp.h"


static unsigned short x_dimension, y_dimension;

static inline void *high_low_short_to_ptr(unsigned short high,
					  unsigned short low)
{
	return (void *)((((unsigned long)high) << 16) | ((unsigned long)low));
}

static inline void ptr_to_high_low_short(void *ptr, unsigned short *high,
					 unsigned short *low)
{
	*high = (unsigned short)((((unsigned long)ptr) >> 16) & 0xffff);
	*low = (unsigned short)((unsigned long)ptr & 0xffff);
}

static int pmem_fixup_high_low(unsigned short *high,
				unsigned short *low,
				unsigned short size_high,
				unsigned short size_low,
				struct msm_adsp_module *module,
				unsigned long *addr, unsigned long *size)
{
	void *phys_addr;
	unsigned long phys_size;
	unsigned long kvaddr;

	phys_addr = high_low_short_to_ptr(*high, *low);
	phys_size = (unsigned long)high_low_short_to_ptr(size_high, size_low);
	DLOG("virt %x %x\n", phys_addr, phys_size);
	if (adsp_pmem_fixup_kvaddr(module, &phys_addr, &kvaddr, phys_size)) {
		DLOG("ah%x al%x sh%x sl%x addr %x size %x\n",
			*high, *low, size_high, size_low, phys_addr, phys_size);
		return -1;
	}
	ptr_to_high_low_short(phys_addr, high, low);
	DLOG("phys %x %x\n", phys_addr, phys_size);
	if (addr)
		*addr = kvaddr;
	if (size)
		*size = phys_size;
	return 0;
}

static int verify_venc_cmd(struct msm_adsp_module *module,
			       void *cmd_data, size_t cmd_size)
{
	unsigned short cmd_id = ((unsigned short *)cmd_data)[0];
	unsigned long frame_buf_size, luma_buf_size, chroma_buf_size;
	unsigned short frame_buf_size_high, frame_buf_size_low;
	unsigned short luma_buf_size_high, luma_buf_size_low;
	unsigned short chroma_buf_size_high, chroma_buf_size_low;
	videnc_cmd_cfg *config_cmd;
	videnc_cmd_frame_start *frame_cmd;
	videnc_cmd_dis *dis_cmd;

	DLOG("cmd_size %d cmd_id %d cmd_data %x\n", cmd_size, cmd_id, cmd_data);
	switch (cmd_id) {
	case VIDENC_CMD_ACTIVE:
		if (cmd_size < sizeof(videnc_cmd_active))
			return -1;
		break;
	case VIDENC_CMD_IDLE:
		if (cmd_size < sizeof(videnc_cmd_idle))
			return -1;
		x_dimension = y_dimension = 0;
		break;
	case VIDENC_CMD_STATUS_QUERY:
		if (cmd_size < sizeof(videnc_cmd_status_query))
			return -1;
		break;
	case VIDENC_CMD_RC_CFG:
		if (cmd_size < sizeof(videnc_cmd_rc_cfg))
			return -1;
		break;
	case VIDENC_CMD_INTRA_REFRESH:
		if (cmd_size < sizeof(videnc_cmd_intra_refresh))
			return -1;
		break;
	case VIDENC_CMD_DIGITAL_ZOOM:
		if (cmd_size < sizeof(videnc_cmd_digital_zoom))
			return -1;
		break;
	case VIDENC_CMD_DIS_CFG:
		if (cmd_size < sizeof(videnc_cmd_dis_cfg))
			return -1;
		break;
	case VIDENC_CMD_CFG:
		if (cmd_size < sizeof(videnc_cmd_cfg))
			return -1;
		config_cmd = (videnc_cmd_cfg *)cmd_data;
		x_dimension = ((config_cmd->venc_frame_dim) & 0xFF00)>>8;
		x_dimension = x_dimension*16;
		y_dimension = (config_cmd->venc_frame_dim) & 0xFF;
		y_dimension = y_dimension * 16;
		break;
	case VIDENC_CMD_FRAME_START:
		if (cmd_size < sizeof(videnc_cmd_frame_start))
			return -1;
		frame_cmd = (videnc_cmd_frame_start *)cmd_data;
		luma_buf_size = x_dimension * y_dimension;
		chroma_buf_size = luma_buf_size>>1;
		frame_buf_size = luma_buf_size + chroma_buf_size;
		ptr_to_high_low_short((void *)luma_buf_size,
			      &luma_buf_size_high,
			      &luma_buf_size_low);
		ptr_to_high_low_short((void *)chroma_buf_size,
			      &chroma_buf_size_high,
			      &chroma_buf_size_low);
		ptr_to_high_low_short((void *)frame_buf_size,
			      &frame_buf_size_high,
			      &frame_buf_size_low);
		/* Address of raw Y data. */
		if (pmem_fixup_high_low(&frame_cmd->input_luma_addr_high,
					&frame_cmd->input_luma_addr_low,
					luma_buf_size_high,
					luma_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		/* Address of raw CbCr data */
		if (pmem_fixup_high_low(&frame_cmd->input_chroma_addr_high,
					&frame_cmd->input_chroma_addr_low,
					chroma_buf_size_high,
					chroma_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		/* Reference VOP */
		if (pmem_fixup_high_low(&frame_cmd->ref_vop_buf_ptr_high,
					&frame_cmd->ref_vop_buf_ptr_low,
					frame_buf_size_high,
					frame_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		/* Encoded Packet Address */
		if (pmem_fixup_high_low(&frame_cmd->enc_pkt_buf_ptr_high,
					&frame_cmd->enc_pkt_buf_ptr_low,
					frame_cmd->enc_pkt_buf_size_high,
					frame_cmd->enc_pkt_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		/* Unfiltered VOP Buffer Address */
		if (pmem_fixup_high_low(
				&frame_cmd->unfilt_recon_vop_buf_ptr_high,
				&frame_cmd->unfilt_recon_vop_buf_ptr_low,
				frame_buf_size_high,
				frame_buf_size_low,
				module,
				NULL, NULL))
			return -1;
		/* Filtered VOP Buffer Address */
		if (pmem_fixup_high_low(&frame_cmd->filt_recon_vop_buf_ptr_high,
					&frame_cmd->filt_recon_vop_buf_ptr_low,
					frame_buf_size_high,
					frame_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		break;
	case VIDENC_CMD_DIS:
		if (cmd_size < sizeof(videnc_cmd_dis))
			return -1;
		dis_cmd = (videnc_cmd_dis *)cmd_data;
		luma_buf_size = x_dimension * y_dimension;
		ptr_to_high_low_short((void *)luma_buf_size,
			      &luma_buf_size_high,
			      &luma_buf_size_low);
		/* Prev VFE Luma Output Address */
		if (pmem_fixup_high_low(&dis_cmd->vfe_out_prev_luma_addr_high,
					&dis_cmd->vfe_out_prev_luma_addr_low,
					luma_buf_size_high,
					luma_buf_size_low,
					module,
					NULL, NULL))
			return -1;
		break;
	default:
		printk(KERN_INFO "adsp_video:unknown encoder video command %u\n",
			cmd_id);
		return 0;
	}

	return 0;
}


int adsp_videoenc_verify_cmd(struct msm_adsp_module *module,
			 unsigned int queue_id, void *cmd_data,
			 size_t cmd_size)
{
	switch (queue_id) {
	case QDSP_mpuVEncCmdQueue:
		DLOG("\n");
		return verify_venc_cmd(module, cmd_data, cmd_size);
	default:
		printk(KERN_INFO "unknown video queue %u\n", queue_id);
		return 0;
	}
}

