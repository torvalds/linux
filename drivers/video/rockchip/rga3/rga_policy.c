// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_policy: " fmt

#include "rga_job.h"
#include "rga_common.h"
#include "rga_hw_config.h"
#include "rga_debugger.h"

#define GET_GCD(n1, n2) \
	({ \
		int i; \
		int gcd = 0; \
		for (i = 1; i <= (n1) && i <= (n2); i++) { \
			if ((n1) % i == 0 && (n2) % i == 0) \
				gcd = i; \
		} \
		gcd; \
	})
#define GET_LCM(n1, n2, gcd) (((n1) * (n2)) / gcd)

static int rga_set_feature(struct rga_req *rga_base)
{
	int feature = 0;

	if (rga_base->render_mode == COLOR_FILL_MODE)
		feature |= RGA_COLOR_FILL;

	if (rga_base->render_mode == COLOR_PALETTE_MODE)
		feature |= RGA_COLOR_PALETTE;

	if (rga_base->color_key_max > 0 || rga_base->color_key_min > 0)
		feature |= RGA_COLOR_KEY;

	if ((rga_base->alpha_rop_flag >> 1) & 1)
		feature |= RGA_ROP_CALCULATE;

	if ((rga_base->alpha_rop_flag >> 8) & 1)
		feature |= RGA_NN_QUANTIZE;

	return feature;
}

static bool rga_check_resolution(const struct rga_rect_range *range, int width, int height)
{
	if (width > range->max.width || height > range->max.height)
		return false;

	if (width < range->min.width || height < range->min.height)
		return false;

	return true;
}

static bool rga_check_format(const struct rga_hw_data *data,
		int rd_mode, int format, int win_num)
{
	int i;
	bool matched = false;

	if (rd_mode == RGA_RASTER_MODE) {
		for (i = 0; i < data->win[win_num].num_of_raster_formats; i++) {
			if (format == data->win[win_num].raster_formats[i]) {
				matched = true;
				break;
			}
		}
	} else if (rd_mode == RGA_FBC_MODE) {
		for (i = 0; i < data->win[win_num].num_of_fbc_formats; i++) {
			if (format == data->win[win_num].fbc_formats[i]) {
				matched = true;
				break;
			}
		}
	} else if (rd_mode == RGA_TILE_MODE) {
		for (i = 0; i < data->win[win_num].num_of_tile_formats; i++) {
			if (format == data->win[win_num].tile_formats[i]) {
				matched = true;
				break;
			}
		}
	}

	return matched;
}

static bool rga_check_align(uint32_t byte_stride_align, uint32_t format, uint16_t w_stride)
{
	int bit_stride, pixel_stride, align, gcd;

	pixel_stride = rga_get_pixel_stride_from_format(format);
	if (pixel_stride <= 0)
		return false;

	bit_stride = pixel_stride * w_stride;

	if (bit_stride % (byte_stride_align * 8) == 0)
		return true;

	if (DEBUGGER_EN(MSG)) {
		gcd = GET_GCD(pixel_stride, byte_stride_align * 8);
		align = GET_LCM(pixel_stride, byte_stride_align * 8, gcd) / pixel_stride;
		pr_info("unsupported width stride %d, 0x%x should be %d aligned!",
			w_stride, format, align);
	}

	return false;
}

static bool rga_check_src0(const struct rga_hw_data *data,
			 struct rga_img_info_t *src0)
{
	if (!rga_check_resolution(&data->input_range, src0->act_w, src0->act_h))
		return false;

	if (data == &rga3_data &&
	    !rga_check_resolution(&data->input_range,
				  src0->act_w + src0->x_offset,
				  src0->act_h + src0->y_offset))
		return false;

	if (!rga_check_format(data, src0->rd_mode, src0->format, 0))
		return false;

	if (!rga_check_align(data->byte_stride_align, src0->format, src0->vir_w))
		return false;

	return true;
}

static bool rga_check_src1(const struct rga_hw_data *data,
			 struct rga_img_info_t *src1)
{
	if (!rga_check_resolution(&data->input_range, src1->act_w, src1->act_h))
		return false;

	if (data == &rga3_data &&
	    !rga_check_resolution(&data->input_range,
				  src1->act_w + src1->x_offset,
				  src1->act_h + src1->y_offset))
		return false;

	if (!rga_check_format(data, src1->rd_mode, src1->format, 1))
		return false;

	if (!rga_check_align(data->byte_stride_align, src1->format, src1->vir_w))
		return false;

	return true;
}

static bool rga_check_dst(const struct rga_hw_data *data,
			 struct rga_img_info_t *dst)
{
	if (!rga_check_resolution(&data->output_range, dst->act_w, dst->act_h))
		return false;

	if (data == &rga3_data &&
	    !rga_check_resolution(&data->output_range,
				  dst->act_w + dst->x_offset,
				  dst->act_h + dst->y_offset))
		return false;

	if (!rga_check_format(data, dst->rd_mode, dst->format, 2))
		return false;

	if (!rga_check_align(data->byte_stride_align, dst->format, dst->vir_w))
		return false;

	return true;
}

static bool rga_check_scale(const struct rga_hw_data *data,
				struct rga_req *rga_base)
{
	struct rga_img_info_t *src0 = &rga_base->src;
	struct rga_img_info_t *dst = &rga_base->dst;

	int sw, sh;
	int dw, dh;

	sw = src0->act_w;
	sh = src0->act_h;

	if ((rga_base->sina == 65536 && rga_base->cosa == 0)
		|| (rga_base->sina == -65536 && rga_base->cosa == 0)) {
		dw = dst->act_h;
		dh = dst->act_w;
	} else {
		dw = dst->act_w;
		dh = dst->act_h;
	}

	if (sw > dw) {
		if ((sw >> data->max_downscale_factor) > dw)
			return false;
	} else if (sw < dw) {
		if ((sw << data->max_upscale_factor) < dw)
			return false;
	}

	if (sh > dh) {
		if ((sh >> data->max_downscale_factor) > dh)
			return false;
	} else if (sh < dh) {
		if ((sh << data->max_upscale_factor) < dh)
			return false;
	}

	return true;
}

int rga_job_assign(struct rga_job *job)
{
	struct rga_img_info_t *src0 = &job->rga_command_base.src;
	struct rga_img_info_t *src1 = &job->rga_command_base.pat;
	struct rga_img_info_t *dst = &job->rga_command_base.dst;

	struct rga_req *rga_base = &job->rga_command_base;
	const struct rga_hw_data *data;
	struct rga_scheduler_t *scheduler = NULL;

	int feature;
	int core = RGA_NONE_CORE;
	int optional_cores = RGA_NONE_CORE;
	int specified_cores = RGA_NONE_CORE;
	int i;
	int min_of_job_count = -1;
	unsigned long flags;

	/* assigned by userspace */
	if (rga_base->core > RGA_NONE_CORE) {
		if (rga_base->core > RGA_CORE_MASK) {
			pr_err("invalid setting core by user\n");
			goto finish;
		} else if (rga_base->core & RGA_CORE_MASK)
			specified_cores = rga_base->core;
	}

	feature = rga_set_feature(rga_base);

	/* function */
	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		data = rga_drvdata->scheduler[i]->data;
		scheduler = rga_drvdata->scheduler[i];

		if ((specified_cores != RGA_NONE_CORE) &&
			(!(scheduler->core & specified_cores)))
			continue;

		if (DEBUGGER_EN(MSG))
			pr_info("start policy on core = %d", scheduler->core);

		if (scheduler->data->mmu == RGA_MMU &&
		    job->flags & RGA_JOB_UNSUPPORT_RGA_MMU) {
			if (DEBUGGER_EN(MSG))
				pr_info("RGA2 only support under 4G memory!\n");
			continue;
		}

		if (feature > 0) {
			if (!(feature & data->feature)) {
				if (DEBUGGER_EN(MSG))
					pr_info("core = %d, break on feature",
						scheduler->core);
				continue;
			}
		}

		/* only colorfill need single win (colorpalette?) */
		if (!(feature & 1)) {
			if (src1->yrgb_addr > 0) {
				if ((!(src0->rd_mode & data->win[0].rd_mode)) ||
					(!(src1->rd_mode & data->win[1].rd_mode)) ||
					(!(dst->rd_mode & data->win[2].rd_mode))) {
					if (DEBUGGER_EN(MSG))
						pr_info("core = %d, ABC break on rd_mode",
							scheduler->core);
					continue;
				}
			} else {
				if ((!(src0->rd_mode & data->win[0].rd_mode)) ||
					(!(dst->rd_mode & data->win[2].rd_mode))) {
					if (DEBUGGER_EN(MSG))
						pr_info("core = %d, ABB break on rd_mode",
							scheduler->core);
					continue;
				}
			}

			if (!rga_check_scale(data, rga_base)) {
				if (DEBUGGER_EN(MSG))
					pr_info("core = %d, break on rga_check_scale",
						scheduler->core);
				continue;
			}

			if (!rga_check_src0(data, src0)) {
				if (DEBUGGER_EN(MSG))
					pr_info("core = %d, break on rga_check_src0",
						scheduler->core);
				continue;
			}

			if (src1->yrgb_addr > 0) {
				if (!rga_check_src1(data, src1)) {
					if (DEBUGGER_EN(MSG))
						pr_info("core = %d, break on rga_check_src1",
							scheduler->core);
					continue;
				}
			}
		}

		if (!rga_check_dst(data, dst)) {
			if (DEBUGGER_EN(MSG))
				pr_info("core = %d, break on rga_check_dst",
					scheduler->core);
			continue;
		}

		optional_cores |= scheduler->core;
	}

	if (DEBUGGER_EN(MSG))
		pr_info("optional_cores = %d\n", optional_cores);

	if (optional_cores == 0) {
		core = -1;
		pr_err("invalid function policy\n");
		goto finish;
	}

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		if (optional_cores & scheduler->core) {
			spin_lock_irqsave(&scheduler->irq_lock, flags);

			if (scheduler->running_job == NULL) {
				core = scheduler->core;
				job->scheduler = scheduler;
				spin_unlock_irqrestore(&scheduler->irq_lock,
							 flags);
				break;
			} else {
				if ((min_of_job_count == -1) ||
				    (min_of_job_count > scheduler->job_count)) {
					min_of_job_count = scheduler->job_count;
					core = scheduler->core;
					job->scheduler = scheduler;
				}
			}

			spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		}
	}

	/* TODO: need consider full load */
finish:
	if (DEBUGGER_EN(MSG))
		pr_info("assign core: %d\n", core);

	return core;
}
