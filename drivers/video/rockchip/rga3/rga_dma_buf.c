// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_dma_buf: " fmt

#include "rga_dma_buf.h"
#include "rga.h"

#if CONFIG_ROCKCHIP_RGA_DEBUGGER
extern int RGA_DEBUG_CHECK_MODE;
#endif

static struct rga_scheduler_t *get_scheduler(int core)
{
	struct rga_scheduler_t *scheduler = NULL;
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		if (core == rga_drvdata->rga_scheduler[i]->core) {
			scheduler = rga_drvdata->rga_scheduler[i];
			if (RGA_DEBUG_MSG)
				pr_info("choose core: %d\n",
					rga_drvdata->rga_scheduler[i]->core);
			break;
		}
	}

	return scheduler;
}

static bool is_yuv422p_format(u32 format)
{
	bool ret = false;

	switch (format) {
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_P:
		ret = true;
		break;
	}
	return ret;
}

static void rga_convert_addr(struct rga_img_info_t *img)
{
	/*
	 * If it is not using dma fd, the virtual/phyical address is assigned
	 * to the address of the corresponding channel.
	 */

	//img->yrgb_addr = img->uv_addr;

	if (img->rd_mode != RGA_FBC_MODE) {
		img->uv_addr = img->yrgb_addr + (img->vir_w * img->vir_h);

		//warning: rga3 may need /2 for all
		if (is_yuv422p_format(img->format))
			img->v_addr =
				img->uv_addr + (img->vir_w * img->vir_h) / 2;
		else
			img->v_addr =
				img->uv_addr + (img->vir_w * img->vir_h) / 4;
	} else {
		img->uv_addr = img->yrgb_addr;
		img->v_addr = 0;
	}
}

int rga_get_format_bits(u32 format)
{
	int bits = 0;

	switch (format) {
	case RGA2_FORMAT_RGBA_8888:
	case RGA2_FORMAT_RGBX_8888:
	case RGA2_FORMAT_BGRA_8888:
	case RGA2_FORMAT_BGRX_8888:
	case RGA2_FORMAT_ARGB_8888:
	case RGA2_FORMAT_XRGB_8888:
	case RGA2_FORMAT_ABGR_8888:
	case RGA2_FORMAT_XBGR_8888:
		bits = 32;
		break;
	case RGA2_FORMAT_RGB_888:
	case RGA2_FORMAT_BGR_888:
		bits = 24;
		break;
	case RGA2_FORMAT_RGB_565:
	case RGA2_FORMAT_RGBA_5551:
	case RGA2_FORMAT_RGBA_4444:
	case RGA2_FORMAT_BGR_565:
	case RGA2_FORMAT_YCbCr_422_SP:
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_SP:
	case RGA2_FORMAT_YCrCb_422_P:
	case RGA2_FORMAT_BGRA_5551:
	case RGA2_FORMAT_BGRA_4444:
	case RGA2_FORMAT_ARGB_5551:
	case RGA2_FORMAT_ARGB_4444:
	case RGA2_FORMAT_ABGR_5551:
	case RGA2_FORMAT_ABGR_4444:
		bits = 16;
		break;
	case RGA2_FORMAT_YCbCr_420_SP:
	case RGA2_FORMAT_YCbCr_420_P:
	case RGA2_FORMAT_YCrCb_420_SP:
	case RGA2_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RGA2_FORMAT_YCbCr_420_SP_10B:
	case RGA2_FORMAT_YCrCb_420_SP_10B:
	case RGA2_FORMAT_YCbCr_422_SP_10B:
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		bits = 15;
		break;
	default:
		pr_err("unknown format [%d]\n", format);
		return -1;
	}

	return bits;
}

static int rga_virtual_memory_check(void *vaddr, u32 w, u32 h, u32 format,
					int fd)
{
	int bits = 32;
	int temp_data = 0;
	void *one_line = NULL;

	bits = rga_get_format_bits(format);
	if (bits < 0)
		return -1;

	one_line = kzalloc(w * 4, GFP_KERNEL);
	if (!one_line) {
		pr_err("kzalloc fail %s[%d]\n", __func__, __LINE__);
		return 0;
	}

	temp_data = w * (h - 1) * bits >> 3;
	if (fd > 0) {
		pr_info("vaddr is%p, bits is %d, fd check\n", vaddr, bits);
		memcpy(one_line, (char *)vaddr + temp_data, w * bits >> 3);
		pr_info("fd check ok\n");
	} else {
		pr_info("vir addr memory check.\n");
		memcpy((void *)((char *)vaddr + temp_data), one_line,
			 w * bits >> 3);
		pr_info("vir addr check ok.\n");
	}

	kfree(one_line);
	return 0;
}

static int rga_dma_memory_check(struct rga_dma_buffer_t *rga_dma_buffer,
				struct rga_img_info_t *img)
{
	int ret = 0;
	void *vaddr;
	struct dma_buf *dma_buf;

	dma_buf = rga_dma_buffer->dma_buf;

	if (!IS_ERR_OR_NULL(dma_buf)) {
		vaddr = dma_buf_vmap(dma_buf);
		if (vaddr) {
			ret = rga_virtual_memory_check(vaddr, img->vir_w,
				img->vir_h, img->format, img->yrgb_addr);
		} else {
			pr_err("can't vmap the dma buffer!\n");
			return -EINVAL;
		}

		dma_buf_vunmap(dma_buf, vaddr);
	}

	return ret;
}

static int rga_dma_map_buffer(struct dma_buf *dma_buf,
				 struct rga_dma_buffer_t *rga_dma_buffer,
				 enum dma_data_direction dir, struct device *rga_dev)
{
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;

	int ret = 0;

	attach = dma_buf_attach(dma_buf, rga_dev);
	if (IS_ERR(attach)) {
		ret = -EINVAL;
		pr_err("Failed to attach dma_buf\n");
		goto err_get_attach;
	}

	sgt = dma_buf_map_attachment(attach, dir);
	if (IS_ERR(sgt)) {
		ret = -EINVAL;
		pr_err("Failed to map src attachment\n");
		goto err_get_sg;
	}

	rga_dma_buffer->dma_buf = dma_buf;
	rga_dma_buffer->attach = attach;
	rga_dma_buffer->sgt = sgt;
	rga_dma_buffer->iova = sg_dma_address(sgt->sgl);

	/* TODO: size for check */
	rga_dma_buffer->size = sg_dma_len(sgt->sgl);
	rga_dma_buffer->dir = dir;

	return ret;

err_get_sg:
	if (sgt)
		dma_buf_unmap_attachment(attach, sgt, dir);
	if (attach)
		dma_buf_detach(dma_buf, attach);
err_get_attach:
	if (dma_buf && (rga_dma_buffer->use_dma_buf == false))
		dma_buf_put(dma_buf);

	return ret;
}

static void rga_dma_unmap_buffer(struct rga_dma_buffer_t *rga_dma_buffer)
{
	if (rga_dma_buffer->attach && rga_dma_buffer->sgt) {
		dma_buf_unmap_attachment(rga_dma_buffer->attach,
			rga_dma_buffer->sgt, rga_dma_buffer->dir);
	}

	if (rga_dma_buffer->attach)
		dma_buf_detach(rga_dma_buffer->dma_buf, rga_dma_buffer->attach);
}

static int rga_dma_buf_get_channel_info(struct rga_img_info_t *channel_info,
			struct rga_dma_buffer_t **rga_dma_buffer, int mmu_flag,
			struct dma_buf **dma_buf, int core)
{
	int ret;
	struct rga_dma_buffer_t *alloc_buffer;
	struct rga_scheduler_t *scheduler = NULL;

	if (unlikely(!mmu_flag && *dma_buf)) {
		pr_err("Fix it please enable mmu on dma buf channel\n");
		return -EINVAL;
	} else if (mmu_flag && *dma_buf) {
		/* perform a single mapping to dma buffer */
		alloc_buffer =
			kmalloc(sizeof(struct rga_dma_buffer_t),
				GFP_KERNEL);
		if (alloc_buffer == NULL) {
			pr_err("rga2_dma_import_fd alloc error!\n");
			return -ENOMEM;
		}

		alloc_buffer->use_dma_buf = false;

		scheduler = get_scheduler(core);
		if (scheduler == NULL) {
			pr_err("failed to get scheduler, %s(%d)\n", __func__,
				 __LINE__);
			kfree(alloc_buffer);
			ret = -EINVAL;
			return ret;
		}

		ret =
			rga_dma_map_buffer(*dma_buf, alloc_buffer,
						DMA_BIDIRECTIONAL, scheduler->dev);
		if (ret < 0) {
			pr_err("Can't map dma-buf\n");
			kfree(alloc_buffer);
			return ret;
		}

		*rga_dma_buffer = alloc_buffer;
	}

#if CONFIG_ROCKCHIP_RGA_DEBUGGER
	if (RGA_DEBUG_CHECK_MODE) {
		ret = rga_dma_memory_check(*rga_dma_buffer,
			channel_info);
		if (ret < 0) {
			pr_err("Channel check memory error!\n");
			/*
			 * Note: This error is released by external
			 *	 rga_dma_put_channel_info().
			 */
			return ret;
		}
	}
#endif

	if (core == RGA2_SCHEDULER_CORE0)
		channel_info->yrgb_addr = channel_info->uv_addr;
	else if (core == RGA3_SCHEDULER_CORE0 || core == RGA3_SCHEDULER_CORE1)
		if (*rga_dma_buffer)
			channel_info->yrgb_addr = (*rga_dma_buffer)->iova;

	rga_convert_addr(channel_info);

	return 0;
}

static void rga_dma_put_channel_info(struct rga_dma_buffer_t **rga_dma_buffer, struct dma_buf **dma_buf)
{
	struct rga_dma_buffer_t *buffer;

	buffer = *rga_dma_buffer;
	if (buffer == NULL)
		return;

	rga_dma_unmap_buffer(buffer);
	dma_buf_put(*dma_buf);
	kfree(buffer);

	*rga_dma_buffer = NULL;
	*dma_buf = NULL;
}

int rga_dma_buf_get(struct rga_job *job)
{
	int ret = -EINVAL;
	int mmu_flag;

	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &job->rga_command_base.src;
	dst = &job->rga_command_base.dst;
	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &job->rga_command_base.pat;
	else
		els = &job->rga_command_base.pat;

	if (likely(src0 != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		if (mmu_flag && src0->yrgb_addr) {
			job->dma_buf_src0 = dma_buf_get(src0->yrgb_addr);
			if (IS_ERR(job->dma_buf_src0)) {
				ret = -EINVAL;
				pr_err("%s src0 dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)src0->yrgb_addr);
				return ret;
			}
		}
	}

	if (likely(dst != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		if (mmu_flag && dst->yrgb_addr) {
			job->dma_buf_dst = dma_buf_get(dst->yrgb_addr);
			if (IS_ERR(job->dma_buf_dst)) {
				ret = -EINVAL;
				pr_err("%s dst dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)dst->yrgb_addr);
				return ret;
			}
		}
	}

	if (src1 != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		if (mmu_flag && src1->yrgb_addr) {
			job->dma_buf_src1 = dma_buf_get(src1->yrgb_addr);
			if (IS_ERR(job->dma_buf_src0)) {
				ret = -EINVAL;
				pr_err("%s src1 dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)src1->yrgb_addr);
				return ret;
			}
		}
	}

	if (els != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		if (mmu_flag && els->yrgb_addr) {
			job->dma_buf_els = dma_buf_get(els->yrgb_addr);
			if (IS_ERR(job->dma_buf_els)) {
				ret = -EINVAL;
				pr_err("%s els dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)els->yrgb_addr);
				return ret;
			}
		}
	}

	return 0;
}

int rga_dma_get_info(struct rga_job *job)
{
	int ret = 0;
	uint32_t mmu_flag;
	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &job->rga_command_base.src;
	dst = &job->rga_command_base.dst;
	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &job->rga_command_base.pat;
	else
		els = &job->rga_command_base.pat;

	/* src0 channel */
	if (likely(src0 != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		if (job->dma_buf_src0 != NULL) {
			ret = rga_dma_buf_get_channel_info(src0,
				&job->rga_dma_buffer_src0, mmu_flag,
				&job->dma_buf_src0, job->core);
		}

		if (unlikely(ret < 0)) {
			pr_err("src0 channel get info error!\n");
			goto src0_channel_err;
		}

		if (src0->yrgb_addr <= 0 && job->rga_dma_buffer_src0 != NULL)
			job->rga_dma_buffer_src0->use_dma_buf = true;
	}

	/* dst channel */
	if (likely(dst != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		if (job->dma_buf_dst != NULL) {
			ret = rga_dma_buf_get_channel_info(dst,
				&job->rga_dma_buffer_dst, mmu_flag,
				&job->dma_buf_dst, job->core);
		}

		if (unlikely(ret < 0)) {
			pr_err("dst channel get info error!\n");
			goto dst_channel_err;
		}

		if (dst->yrgb_addr <= 0 && job->rga_dma_buffer_dst != NULL)
			job->rga_dma_buffer_dst->use_dma_buf = true;
	}

	/* src1 channel */
	if (src1 != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		if (job->dma_buf_src1 != NULL) {
			ret = rga_dma_buf_get_channel_info(src1,
				&job->rga_dma_buffer_src1, mmu_flag,
				&job->dma_buf_src1, job->core);
		}

		if (unlikely(ret < 0)) {
			pr_err("src1 channel get info error!\n");
			goto src1_channel_err;
		}

		if (src1->yrgb_addr <= 0 && job->rga_dma_buffer_src1 != NULL)
			job->rga_dma_buffer_src1->use_dma_buf = true;
	}

	/* els channel */
	if (els != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		if (job->dma_buf_els != NULL) {
			ret = rga_dma_buf_get_channel_info(els,
				&job->rga_dma_buffer_els, mmu_flag,
				&job->dma_buf_els, job->core);
		}

		if (unlikely(ret < 0)) {
			pr_err("els channel get info error!\n");
			goto els_channel_err;
		}

		if (els->yrgb_addr <= 0 && job->rga_dma_buffer_els != NULL)
			job->rga_dma_buffer_els->use_dma_buf = true;
	}

	return 0;

els_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_els, &job->dma_buf_els);
dst_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_dst, &job->dma_buf_dst);
src1_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_src1, &job->dma_buf_src1);
src0_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_src0, &job->dma_buf_src0);

	return ret;
}

void rga_dma_put_info(struct rga_job *job)
{
	rga_dma_put_channel_info(&job->rga_dma_buffer_src0, &job->dma_buf_src0);
	rga_dma_put_channel_info(&job->rga_dma_buffer_src1, &job->dma_buf_src1);
	rga_dma_put_channel_info(&job->rga_dma_buffer_dst, &job->dma_buf_dst);
	rga_dma_put_channel_info(&job->rga_dma_buffer_els, &job->dma_buf_els);
}
