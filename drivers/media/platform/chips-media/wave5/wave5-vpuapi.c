// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - helper functions
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include <linux/bug.h>
#include "wave5-vpuapi.h"
#include "wave5-regdefine.h"
#include "wave5.h"

#define DECODE_ALL_TEMPORAL_LAYERS 0
#define DECODE_ALL_SPATIAL_LAYERS 0

static int wave5_initialize_vpu(struct device *dev, u8 *code, size_t size)
{
	int ret;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (wave5_vpu_is_init(vpu_dev)) {
		wave5_vpu_re_init(dev, (void *)code, size);
		ret = -EBUSY;
		goto err_out;
	}

	ret = wave5_vpu_reset(dev, SW_RESET_ON_BOOT);
	if (ret)
		goto err_out;

	ret = wave5_vpu_init(dev, (void *)code, size);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);
	return ret;
}

int wave5_vpu_init_with_bitcode(struct device *dev, u8 *bitcode, size_t size)
{
	if (!bitcode || size == 0)
		return -EINVAL;

	return wave5_initialize_vpu(dev, bitcode, size);
}

int wave5_vpu_flush_instance(struct vpu_instance *inst)
{
	int ret = 0;
	int retry = 0;

	ret = mutex_lock_interruptible(&inst->dev->hw_lock);
	if (ret)
		return ret;
	do {
		/*
		 * Repeat the FLUSH command until the firmware reports that the
		 * VPU isn't running anymore
		 */
		ret = wave5_vpu_hw_flush_instance(inst);
		if (ret < 0 && ret != -EBUSY) {
			dev_warn(inst->dev->dev, "Flush of %s instance with id: %d fail: %d\n",
				 inst->type == VPU_INST_TYPE_DEC ? "DECODER" : "ENCODER", inst->id,
				 ret);
			mutex_unlock(&inst->dev->hw_lock);
			return ret;
		}
		if (ret == -EBUSY && retry++ >= MAX_FIRMWARE_CALL_RETRY) {
			dev_warn(inst->dev->dev, "Flush of %s instance with id: %d timed out!\n",
				 inst->type == VPU_INST_TYPE_DEC ? "DECODER" : "ENCODER", inst->id);
			mutex_unlock(&inst->dev->hw_lock);
			return -ETIMEDOUT;
		}
	} while (ret != 0);
	mutex_unlock(&inst->dev->hw_lock);

	return ret;
}

int wave5_vpu_get_version_info(struct device *dev, u32 *revision, unsigned int *product_id)
{
	int ret;
	struct vpu_device *vpu_dev = dev_get_drvdata(dev);

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (!wave5_vpu_is_init(vpu_dev)) {
		ret = -EINVAL;
		goto err_out;
	}

	if (product_id)
		*product_id = vpu_dev->product;
	ret = wave5_vpu_get_version(vpu_dev, revision);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);
	return ret;
}

static int wave5_check_dec_open_param(struct vpu_instance *inst, struct dec_open_param *param)
{
	if (inst->id >= MAX_NUM_INSTANCE) {
		dev_err(inst->dev->dev, "Too many simultaneous instances: %d (max: %u)\n",
			inst->id, MAX_NUM_INSTANCE);
		return -EOPNOTSUPP;
	}

	if (param->bitstream_buffer % 8) {
		dev_err(inst->dev->dev,
			"Bitstream buffer must be aligned to a multiple of 8\n");
		return -EINVAL;
	}

	if (param->bitstream_buffer_size % 1024 ||
	    param->bitstream_buffer_size < MIN_BITSTREAM_BUFFER_SIZE) {
		dev_err(inst->dev->dev,
			"Bitstream buffer size must be aligned to a multiple of 1024 and have a minimum size of %d\n",
			MIN_BITSTREAM_BUFFER_SIZE);
		return -EINVAL;
	}

	return 0;
}

int wave5_vpu_dec_open(struct vpu_instance *inst, struct dec_open_param *open_param)
{
	struct dec_info *p_dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;
	dma_addr_t buffer_addr;
	size_t buffer_size;

	ret = wave5_check_dec_open_param(inst, open_param);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (!wave5_vpu_is_init(vpu_dev)) {
		mutex_unlock(&vpu_dev->hw_lock);
		return -ENODEV;
	}

	p_dec_info = &inst->codec_info->dec_info;
	memcpy(&p_dec_info->open_param, open_param, sizeof(struct dec_open_param));

	buffer_addr = open_param->bitstream_buffer;
	buffer_size = open_param->bitstream_buffer_size;
	p_dec_info->stream_wr_ptr = buffer_addr;
	p_dec_info->stream_rd_ptr = buffer_addr;
	p_dec_info->stream_buf_start_addr = buffer_addr;
	p_dec_info->stream_buf_size = buffer_size;
	p_dec_info->stream_buf_end_addr = buffer_addr + buffer_size;
	p_dec_info->reorder_enable = TRUE;
	p_dec_info->temp_id_select_mode = TEMPORAL_ID_MODE_ABSOLUTE;
	p_dec_info->target_temp_id = DECODE_ALL_TEMPORAL_LAYERS;
	p_dec_info->target_spatial_id = DECODE_ALL_SPATIAL_LAYERS;

	ret = wave5_vpu_build_up_dec_param(inst, open_param);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

static int reset_auxiliary_buffers(struct vpu_instance *inst, unsigned int index)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;

	if (index >= MAX_REG_FRAME)
		return 1;

	if (p_dec_info->vb_mv[index].size == 0 && p_dec_info->vb_fbc_y_tbl[index].size == 0 &&
	    p_dec_info->vb_fbc_c_tbl[index].size == 0)
		return 1;

	wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_mv[index]);
	wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_fbc_y_tbl[index]);
	wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_fbc_c_tbl[index]);

	return 0;
}

int wave5_vpu_dec_close(struct vpu_instance *inst, u32 *fail_res)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;
	int retry = 0;
	struct vpu_device *vpu_dev = inst->dev;
	int i;

	*fail_res = 0;
	if (!inst->codec_info)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	do {
		ret = wave5_vpu_dec_finish_seq(inst, fail_res);
		if (ret < 0 && *fail_res != WAVE5_SYSERR_VPU_STILL_RUNNING) {
			dev_warn(inst->dev->dev, "dec_finish_seq timed out\n");
			goto unlock_and_return;
		}

		if (*fail_res == WAVE5_SYSERR_VPU_STILL_RUNNING &&
		    retry++ >= MAX_FIRMWARE_CALL_RETRY) {
			ret = -ETIMEDOUT;
			goto unlock_and_return;
		}
	} while (ret != 0);

	dev_dbg(inst->dev->dev, "%s: dec_finish_seq complete\n", __func__);

	wave5_vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_work);

	for (i = 0 ; i < MAX_REG_FRAME; i++) {
		ret = reset_auxiliary_buffers(inst, i);
		if (ret) {
			ret = 0;
			break;
		}
	}

	wave5_vdi_free_dma_memory(vpu_dev, &p_dec_info->vb_task);

unlock_and_return:
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_issue_seq_init(struct vpu_instance *inst)
{
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_dec_init_seq(inst);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_complete_seq_init(struct vpu_instance *inst, struct dec_initial_info *info)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_dec_get_seq_info(inst, info);
	if (!ret)
		p_dec_info->initial_info_obtained = true;

	info->rd_ptr = wave5_dec_get_rd_ptr(inst);
	info->wr_ptr = p_dec_info->stream_wr_ptr;

	p_dec_info->initial_info = *info;

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_register_frame_buffer_ex(struct vpu_instance *inst, int num_of_decoding_fbs,
					   int num_of_display_fbs, int stride, int height)
{
	struct dec_info *p_dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;
	struct frame_buffer *fb;

	if (num_of_decoding_fbs >= WAVE5_MAX_FBS || num_of_display_fbs >= WAVE5_MAX_FBS)
		return -EINVAL;

	p_dec_info = &inst->codec_info->dec_info;
	p_dec_info->num_of_decoding_fbs = num_of_decoding_fbs;
	p_dec_info->num_of_display_fbs = num_of_display_fbs;
	p_dec_info->stride = stride;

	if (!p_dec_info->initial_info_obtained)
		return -EINVAL;

	if (stride < p_dec_info->initial_info.pic_width || (stride % 8 != 0) ||
	    height < p_dec_info->initial_info.pic_height)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	fb = inst->frame_buf;
	ret = wave5_vpu_dec_register_framebuffer(inst, &fb[p_dec_info->num_of_decoding_fbs],
						 LINEAR_FRAME_MAP, p_dec_info->num_of_display_fbs);
	if (ret)
		goto err_out;

	ret = wave5_vpu_dec_register_framebuffer(inst, &fb[0], COMPRESSED_FRAME_MAP,
						 p_dec_info->num_of_decoding_fbs);

err_out:
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_get_bitstream_buffer(struct vpu_instance *inst, dma_addr_t *prd_ptr,
				       dma_addr_t *pwr_ptr, size_t *size)
{
	struct dec_info *p_dec_info;
	dma_addr_t rd_ptr;
	dma_addr_t wr_ptr;
	int room;
	struct vpu_device *vpu_dev = inst->dev;
	int ret;

	p_dec_info = &inst->codec_info->dec_info;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;
	rd_ptr = wave5_dec_get_rd_ptr(inst);
	mutex_unlock(&vpu_dev->hw_lock);

	wr_ptr = p_dec_info->stream_wr_ptr;

	if (wr_ptr < rd_ptr)
		room = rd_ptr - wr_ptr;
	else
		room = (p_dec_info->stream_buf_end_addr - wr_ptr) +
			(rd_ptr - p_dec_info->stream_buf_start_addr);
	room--;

	if (prd_ptr)
		*prd_ptr = rd_ptr;
	if (pwr_ptr)
		*pwr_ptr = wr_ptr;
	if (size)
		*size = room;

	return 0;
}

int wave5_vpu_dec_update_bitstream_buffer(struct vpu_instance *inst, size_t size)
{
	struct dec_info *p_dec_info;
	dma_addr_t wr_ptr;
	dma_addr_t rd_ptr;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (!inst->codec_info)
		return -EINVAL;

	p_dec_info = &inst->codec_info->dec_info;
	wr_ptr = p_dec_info->stream_wr_ptr;
	rd_ptr = p_dec_info->stream_rd_ptr;

	if (size > 0) {
		if (wr_ptr < rd_ptr && rd_ptr <= wr_ptr + size)
			return -EINVAL;

		wr_ptr += size;

		if (wr_ptr > p_dec_info->stream_buf_end_addr) {
			u32 room = wr_ptr - p_dec_info->stream_buf_end_addr;

			wr_ptr = p_dec_info->stream_buf_start_addr;
			wr_ptr += room;
		} else if (wr_ptr == p_dec_info->stream_buf_end_addr) {
			wr_ptr = p_dec_info->stream_buf_start_addr;
		}

		p_dec_info->stream_wr_ptr = wr_ptr;
		p_dec_info->stream_rd_ptr = rd_ptr;
	}

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;
	ret = wave5_vpu_dec_set_bitstream_flag(inst, (size == 0));
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_start_one_frame(struct vpu_instance *inst, u32 *res_fail)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (p_dec_info->stride == 0) /* this means frame buffers have not been registered. */
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_decode(inst, res_fail);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr, int update_wr_ptr)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_dec_set_rd_ptr(inst, addr);

	p_dec_info->stream_rd_ptr = addr;
	if (update_wr_ptr)
		p_dec_info->stream_wr_ptr = addr;

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

dma_addr_t wave5_vpu_dec_get_rd_ptr(struct vpu_instance *inst)
{
	int ret;
	dma_addr_t rd_ptr;

	ret = mutex_lock_interruptible(&inst->dev->hw_lock);
	if (ret)
		return ret;

	rd_ptr = wave5_dec_get_rd_ptr(inst);

	mutex_unlock(&inst->dev->hw_lock);

	return rd_ptr;
}

int wave5_vpu_dec_get_output_info(struct vpu_instance *inst, struct dec_output_info *info)
{
	struct dec_info *p_dec_info;
	int ret;
	struct vpu_rect rect_info;
	u32 val;
	u32 decoded_index;
	u32 disp_idx;
	u32 max_dec_index;
	struct vpu_device *vpu_dev = inst->dev;
	struct dec_output_info *disp_info;

	if (!info)
		return -EINVAL;

	p_dec_info = &inst->codec_info->dec_info;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	memset(info, 0, sizeof(*info));

	ret = wave5_vpu_dec_get_result(inst, info);
	if (ret) {
		info->rd_ptr = p_dec_info->stream_rd_ptr;
		info->wr_ptr = p_dec_info->stream_wr_ptr;
		goto err_out;
	}

	decoded_index = info->index_frame_decoded;

	/* calculate display frame region */
	val = 0;
	rect_info.left = 0;
	rect_info.right = 0;
	rect_info.top = 0;
	rect_info.bottom = 0;

	if (decoded_index < WAVE5_MAX_FBS) {
		if (inst->std == W_HEVC_DEC || inst->std == W_AVC_DEC)
			rect_info = p_dec_info->initial_info.pic_crop_rect;

		if (inst->std == W_HEVC_DEC)
			p_dec_info->dec_out_info[decoded_index].decoded_poc = info->decoded_poc;

		p_dec_info->dec_out_info[decoded_index].rc_decoded = rect_info;
	}
	info->rc_decoded = rect_info;

	disp_idx = info->index_frame_display;
	disp_info = &p_dec_info->dec_out_info[disp_idx];
	if (info->index_frame_display >= 0 && info->index_frame_display < WAVE5_MAX_FBS) {
		if (info->index_frame_display != info->index_frame_decoded) {
			/*
			 * when index_frame_decoded < 0, and index_frame_display >= 0
			 * info->dec_pic_width and info->dec_pic_height are still valid
			 * but those of p_dec_info->dec_out_info[disp_idx] are invalid in VP9
			 */
			info->disp_pic_width = disp_info->dec_pic_width;
			info->disp_pic_height = disp_info->dec_pic_height;
		} else {
			info->disp_pic_width = info->dec_pic_width;
			info->disp_pic_height = info->dec_pic_height;
		}

		info->rc_display = disp_info->rc_decoded;

	} else {
		info->rc_display.left = 0;
		info->rc_display.right = 0;
		info->rc_display.top = 0;
		info->rc_display.bottom = 0;
		info->disp_pic_width = 0;
		info->disp_pic_height = 0;
	}

	p_dec_info->stream_rd_ptr = wave5_dec_get_rd_ptr(inst);
	p_dec_info->frame_display_flag = vpu_read_reg(vpu_dev, W5_RET_DEC_DISP_IDC);

	val = p_dec_info->num_of_decoding_fbs; //fb_offset

	max_dec_index = (p_dec_info->num_of_decoding_fbs > p_dec_info->num_of_display_fbs) ?
		p_dec_info->num_of_decoding_fbs : p_dec_info->num_of_display_fbs;

	if (info->index_frame_display >= 0 &&
	    info->index_frame_display < (int)max_dec_index)
		info->disp_frame = inst->frame_buf[val + info->index_frame_display];

	info->rd_ptr = p_dec_info->stream_rd_ptr;
	info->wr_ptr = p_dec_info->stream_wr_ptr;
	info->frame_display_flag = p_dec_info->frame_display_flag;

	info->sequence_no = p_dec_info->initial_info.sequence_no;
	if (decoded_index < WAVE5_MAX_FBS)
		p_dec_info->dec_out_info[decoded_index] = *info;

	if (disp_idx < WAVE5_MAX_FBS)
		info->disp_frame.sequence_no = info->sequence_no;

	if (info->sequence_changed) {
		memcpy((void *)&p_dec_info->initial_info, (void *)&p_dec_info->new_seq_info,
		       sizeof(struct dec_initial_info));
		p_dec_info->initial_info.sequence_no++;
	}

err_out:
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_clr_disp_flag(struct vpu_instance *inst, int index)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (index >= p_dec_info->num_of_display_fbs)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;
	ret = wave5_dec_clr_disp_flag(inst, index);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_set_disp_flag(struct vpu_instance *inst, int index)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret = 0;
	struct vpu_device *vpu_dev = inst->dev;

	if (index >= p_dec_info->num_of_display_fbs)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;
	ret = wave5_dec_set_disp_flag(inst, index);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_dec_reset_framebuffer(struct vpu_instance *inst, unsigned int index)
{
	if (index >= MAX_REG_FRAME)
		return -EINVAL;

	if (inst->frame_vbuf[index].size == 0)
		return -EINVAL;

	wave5_vdi_free_dma_memory(inst->dev, &inst->frame_vbuf[index]);

	return 0;
}

int wave5_vpu_dec_give_command(struct vpu_instance *inst, enum codec_command cmd, void *parameter)
{
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	int ret = 0;

	switch (cmd) {
	case DEC_GET_QUEUE_STATUS: {
		struct queue_status_info *queue_info = parameter;

		queue_info->instance_queue_count = p_dec_info->instance_queue_count;
		queue_info->report_queue_count = p_dec_info->report_queue_count;
		break;
	}
	case DEC_RESET_FRAMEBUF_INFO: {
		int i;

		for (i = 0; i < MAX_REG_FRAME; i++) {
			ret = wave5_vpu_dec_reset_framebuffer(inst, i);
			if (ret)
				break;
		}

		for (i = 0; i < MAX_REG_FRAME; i++) {
			ret = reset_auxiliary_buffers(inst, i);
			if (ret)
				break;
		}

		wave5_vdi_free_dma_memory(inst->dev, &p_dec_info->vb_task);
		break;
	}
	case DEC_GET_SEQ_INFO: {
		struct dec_initial_info *seq_info = parameter;

		*seq_info = p_dec_info->initial_info;
		break;
	}

	default:
		return -EINVAL;
	}

	return ret;
}

int wave5_vpu_enc_open(struct vpu_instance *inst, struct enc_open_param *open_param)
{
	struct enc_info *p_enc_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = wave5_vpu_enc_check_open_param(inst, open_param);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	if (!wave5_vpu_is_init(vpu_dev)) {
		mutex_unlock(&vpu_dev->hw_lock);
		return -ENODEV;
	}

	p_enc_info = &inst->codec_info->enc_info;
	p_enc_info->open_param = *open_param;

	ret = wave5_vpu_build_up_enc_param(vpu_dev->dev, inst, open_param);
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_enc_close(struct vpu_instance *inst, u32 *fail_res)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	int ret;
	int retry = 0;
	struct vpu_device *vpu_dev = inst->dev;

	*fail_res = 0;
	if (!inst->codec_info)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	do {
		ret = wave5_vpu_enc_finish_seq(inst, fail_res);
		if (ret < 0 && *fail_res != WAVE5_SYSERR_VPU_STILL_RUNNING) {
			dev_warn(inst->dev->dev, "enc_finish_seq timed out\n");
			mutex_unlock(&vpu_dev->hw_lock);
			return ret;
		}

		if (*fail_res == WAVE5_SYSERR_VPU_STILL_RUNNING &&
		    retry++ >= MAX_FIRMWARE_CALL_RETRY) {
			mutex_unlock(&vpu_dev->hw_lock);
			return -ETIMEDOUT;
		}
	} while (ret != 0);

	dev_dbg(inst->dev->dev, "%s: enc_finish_seq complete\n", __func__);

	wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_work);

	if (inst->std == W_HEVC_ENC || inst->std == W_AVC_ENC) {
		wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_sub_sam_buf);
		wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_mv);
		wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_fbc_y_tbl);
		wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_fbc_c_tbl);
	}

	wave5_vdi_free_dma_memory(vpu_dev, &p_enc_info->vb_task);

	mutex_unlock(&vpu_dev->hw_lock);

	return 0;
}

int wave5_vpu_enc_register_frame_buffer(struct vpu_instance *inst, unsigned int num,
					unsigned int stride, int height,
					enum tiled_map_type map_type)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;
	unsigned int size_luma, size_chroma;
	int i;

	if (p_enc_info->stride)
		return -EINVAL;

	if (!p_enc_info->initial_info_obtained)
		return -EINVAL;

	if (num < p_enc_info->initial_info.min_frame_buffer_count)
		return -EINVAL;

	if (stride == 0 || stride % 8 != 0)
		return -EINVAL;

	if (height <= 0)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	p_enc_info->num_frame_buffers = num;
	p_enc_info->stride = stride;

	size_luma = stride * height;
	size_chroma = ALIGN(stride / 2, 16) * height;

	for (i = 0; i < num; i++) {
		if (!inst->frame_buf[i].update_fb_info)
			continue;

		inst->frame_buf[i].update_fb_info = false;
		inst->frame_buf[i].stride = stride;
		inst->frame_buf[i].height = height;
		inst->frame_buf[i].map_type = COMPRESSED_FRAME_MAP;
		inst->frame_buf[i].buf_y_size = size_luma;
		inst->frame_buf[i].buf_cb = inst->frame_buf[i].buf_y + size_luma;
		inst->frame_buf[i].buf_cb_size = size_chroma;
		inst->frame_buf[i].buf_cr_size = 0;
	}

	ret = wave5_vpu_enc_register_framebuffer(inst->dev->dev, inst, &inst->frame_buf[0],
						 COMPRESSED_FRAME_MAP,
						 p_enc_info->num_frame_buffers);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

static int wave5_check_enc_param(struct vpu_instance *inst, struct enc_param *param)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;

	if (!param)
		return -EINVAL;

	if (!param->source_frame)
		return -EINVAL;

	if (p_enc_info->open_param.bit_rate == 0 && inst->std == W_HEVC_ENC) {
		if (param->pic_stream_buffer_addr % 16 || param->pic_stream_buffer_size == 0)
			return -EINVAL;
	}
	if (param->pic_stream_buffer_addr % 8 || param->pic_stream_buffer_size == 0)
		return -EINVAL;

	return 0;
}

int wave5_vpu_enc_start_one_frame(struct vpu_instance *inst, struct enc_param *param, u32 *fail_res)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	*fail_res = 0;

	if (p_enc_info->stride == 0) /* this means frame buffers have not been registered. */
		return -EINVAL;

	ret = wave5_check_enc_param(inst, param);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	p_enc_info->pts_map[param->src_idx] = param->pts;

	ret = wave5_vpu_encode(inst, param, fail_res);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_enc_get_output_info(struct vpu_instance *inst, struct enc_output_info *info)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_enc_get_result(inst, info);
	if (ret) {
		info->pts = 0;
		goto unlock;
	}

	if (info->recon_frame_index >= 0)
		info->pts = p_enc_info->pts_map[info->enc_src_idx];

unlock:
	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_enc_give_command(struct vpu_instance *inst, enum codec_command cmd, void *parameter)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;

	switch (cmd) {
	case ENABLE_ROTATION:
		p_enc_info->rotation_enable = true;
		break;
	case ENABLE_MIRRORING:
		p_enc_info->mirror_enable = true;
		break;
	case SET_MIRROR_DIRECTION: {
		enum mirror_direction mir_dir;

		mir_dir = *(enum mirror_direction *)parameter;
		if (mir_dir != MIRDIR_NONE && mir_dir != MIRDIR_HOR &&
		    mir_dir != MIRDIR_VER && mir_dir != MIRDIR_HOR_VER)
			return -EINVAL;
		p_enc_info->mirror_direction = mir_dir;
		break;
	}
	case SET_ROTATION_ANGLE: {
		int angle;

		angle = *(int *)parameter;
		if (angle && angle != 90 && angle != 180 && angle != 270)
			return -EINVAL;
		if (p_enc_info->initial_info_obtained && (angle == 90 || angle == 270))
			return -EINVAL;
		p_enc_info->rotation_angle = angle;
		break;
	}
	case ENC_GET_QUEUE_STATUS: {
		struct queue_status_info *queue_info = parameter;

		queue_info->instance_queue_count = p_enc_info->instance_queue_count;
		queue_info->report_queue_count = p_enc_info->report_queue_count;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

int wave5_vpu_enc_issue_seq_init(struct vpu_instance *inst)
{
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_enc_init_seq(inst);

	mutex_unlock(&vpu_dev->hw_lock);

	return ret;
}

int wave5_vpu_enc_complete_seq_init(struct vpu_instance *inst, struct enc_initial_info *info)
{
	struct enc_info *p_enc_info = &inst->codec_info->enc_info;
	int ret;
	struct vpu_device *vpu_dev = inst->dev;

	if (!info)
		return -EINVAL;

	ret = mutex_lock_interruptible(&vpu_dev->hw_lock);
	if (ret)
		return ret;

	ret = wave5_vpu_enc_get_seq_info(inst, info);
	if (ret) {
		p_enc_info->initial_info_obtained = false;
		mutex_unlock(&vpu_dev->hw_lock);
		return ret;
	}

	p_enc_info->initial_info_obtained = true;
	p_enc_info->initial_info = *info;

	mutex_unlock(&vpu_dev->hw_lock);

	return 0;
}
