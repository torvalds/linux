// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_rpf.c  --  R-Car VSP1 Read Pixel Formatter
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define RPF_MAX_WIDTH				8190
#define RPF_MAX_HEIGHT				8190

/* Pre extended display list command data structure. */
struct vsp1_extcmd_auto_fld_body {
	u32 top_y0;
	u32 bottom_y0;
	u32 top_c0;
	u32 bottom_c0;
	u32 top_c1;
	u32 bottom_c1;
	u32 reserved0;
	u32 reserved1;
} __packed;

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_rpf_write(struct vsp1_rwpf *rpf,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg + rpf->entity.index * VI6_RPF_OFFSET,
			       data);
}

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void rpf_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);
	const struct vsp1_format_info *fmtinfo = rpf->fmtinfo;
	const struct v4l2_pix_format_mplane *format = &rpf->format;
	const struct v4l2_mbus_framefmt *source_format;
	const struct v4l2_mbus_framefmt *sink_format;
	unsigned int left = 0;
	unsigned int top = 0;
	u32 pstride;
	u32 infmt;

	/* Stride */
	pstride = format->plane_fmt[0].bytesperline
		<< VI6_RPF_SRCM_PSTRIDE_Y_SHIFT;
	if (format->num_planes > 1)
		pstride |= format->plane_fmt[1].bytesperline
			<< VI6_RPF_SRCM_PSTRIDE_C_SHIFT;

	/*
	 * pstride has both STRIDE_Y and STRIDE_C, but multiplying the whole
	 * of pstride by 2 is conveniently OK here as we are multiplying both
	 * values.
	 */
	if (pipe->interlaced)
		pstride *= 2;

	vsp1_rpf_write(rpf, dlb, VI6_RPF_SRCM_PSTRIDE, pstride);

	/* Format */
	sink_format = vsp1_entity_get_pad_format(&rpf->entity,
						 rpf->entity.state,
						 RWPF_PAD_SINK);
	source_format = vsp1_entity_get_pad_format(&rpf->entity,
						   rpf->entity.state,
						   RWPF_PAD_SOURCE);

	infmt = VI6_RPF_INFMT_CIPM
	      | (fmtinfo->hwfmt << VI6_RPF_INFMT_RDFMT_SHIFT);

	if (fmtinfo->swap_yc)
		infmt |= VI6_RPF_INFMT_SPYCS;
	if (fmtinfo->swap_uv)
		infmt |= VI6_RPF_INFMT_SPUVS;

	if (sink_format->code != source_format->code)
		infmt |= VI6_RPF_INFMT_CSC;

	vsp1_rpf_write(rpf, dlb, VI6_RPF_INFMT, infmt);
	vsp1_rpf_write(rpf, dlb, VI6_RPF_DSWAP, fmtinfo->swap);

	if (entity->vsp1->info->gen == 4) {
		u32 ext_infmt0;
		u32 ext_infmt1;
		u32 ext_infmt2;

		switch (fmtinfo->fourcc) {
		case V4L2_PIX_FMT_RGBX1010102:
			ext_infmt0 = VI6_RPF_EXT_INFMT0_BYPP_M1_RGB10;
			ext_infmt1 = VI6_RPF_EXT_INFMT1_PACK_CPOS(0, 10, 20, 0);
			ext_infmt2 = VI6_RPF_EXT_INFMT2_PACK_CLEN(10, 10, 10, 0);
			break;

		case V4L2_PIX_FMT_RGBA1010102:
			ext_infmt0 = VI6_RPF_EXT_INFMT0_BYPP_M1_RGB10;
			ext_infmt1 = VI6_RPF_EXT_INFMT1_PACK_CPOS(0, 10, 20, 30);
			ext_infmt2 = VI6_RPF_EXT_INFMT2_PACK_CLEN(10, 10, 10, 2);
			break;

		case V4L2_PIX_FMT_ARGB2101010:
			ext_infmt0 = VI6_RPF_EXT_INFMT0_BYPP_M1_RGB10;
			ext_infmt1 = VI6_RPF_EXT_INFMT1_PACK_CPOS(2, 12, 22, 0);
			ext_infmt2 = VI6_RPF_EXT_INFMT2_PACK_CLEN(10, 10, 10, 2);
			break;

		case V4L2_PIX_FMT_Y210:
			ext_infmt0 = VI6_RPF_EXT_INFMT0_F2B |
				     VI6_RPF_EXT_INFMT0_IPBD_Y_10 |
				     VI6_RPF_EXT_INFMT0_IPBD_C_10;
			ext_infmt1 = 0x0;
			ext_infmt2 = 0x0;
			break;

		case V4L2_PIX_FMT_Y212:
			ext_infmt0 = VI6_RPF_EXT_INFMT0_F2B |
				     VI6_RPF_EXT_INFMT0_IPBD_Y_12 |
				     VI6_RPF_EXT_INFMT0_IPBD_C_12;
			ext_infmt1 = 0x0;
			ext_infmt2 = 0x0;
			break;

		default:
			ext_infmt0 = 0;
			ext_infmt1 = 0;
			ext_infmt2 = 0;
			break;
		}

		vsp1_rpf_write(rpf, dlb, VI6_RPF_EXT_INFMT0, ext_infmt0);
		vsp1_rpf_write(rpf, dlb, VI6_RPF_EXT_INFMT1, ext_infmt1);
		vsp1_rpf_write(rpf, dlb, VI6_RPF_EXT_INFMT2, ext_infmt2);
	}

	/* Output location. */
	if (pipe->brx) {
		const struct v4l2_rect *compose;

		compose = vsp1_entity_get_pad_selection(pipe->brx,
							pipe->brx->state,
							rpf->brx_input,
							V4L2_SEL_TGT_COMPOSE);
		left = compose->left;
		top = compose->top;
	}

	if (pipe->interlaced)
		top /= 2;

	vsp1_rpf_write(rpf, dlb, VI6_RPF_LOC,
		       (left << VI6_RPF_LOC_HCOORD_SHIFT) |
		       (top << VI6_RPF_LOC_VCOORD_SHIFT));

	/*
	 * On Gen2 use the alpha channel (extended to 8 bits) when available or
	 * a fixed alpha value set through the V4L2_CID_ALPHA_COMPONENT control
	 * otherwise.
	 *
	 * The Gen3+ RPF has extended alpha capability and can both multiply the
	 * alpha channel by a fixed global alpha value, and multiply the pixel
	 * components to convert the input to premultiplied alpha.
	 *
	 * As alpha premultiplication is available in the BRx for both Gen2 and
	 * Gen3+ we handle it there and use the Gen3 alpha multiplier for global
	 * alpha multiplication only. This however prevents conversion to
	 * premultiplied alpha if no BRx is present in the pipeline. If that use
	 * case turns out to be useful we will revisit the implementation (for
	 * Gen3 only).
	 *
	 * We enable alpha multiplication on Gen3+ using the fixed alpha value
	 * set through the V4L2_CID_ALPHA_COMPONENT control when the input
	 * contains an alpha channel. On Gen2 the global alpha is ignored in
	 * that case.
	 *
	 * In all cases, disable color keying.
	 */
	vsp1_rpf_write(rpf, dlb, VI6_RPF_ALPH_SEL, VI6_RPF_ALPH_SEL_AEXT_EXT |
		       (fmtinfo->alpha ? VI6_RPF_ALPH_SEL_ASEL_PACKED
				       : VI6_RPF_ALPH_SEL_ASEL_FIXED));

	if (entity->vsp1->info->gen >= 3) {
		u32 mult;

		if (fmtinfo->alpha) {
			/*
			 * When the input contains an alpha channel enable the
			 * alpha multiplier. If the input is premultiplied we
			 * need to multiply both the alpha channel and the pixel
			 * components by the global alpha value to keep them
			 * premultiplied. Otherwise multiply the alpha channel
			 * only.
			 */
			bool premultiplied = format->flags
					   & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;

			mult = VI6_RPF_MULT_ALPHA_A_MMD_RATIO
			     | (premultiplied ?
				VI6_RPF_MULT_ALPHA_P_MMD_RATIO :
				VI6_RPF_MULT_ALPHA_P_MMD_NONE);
		} else {
			/*
			 * When the input doesn't contain an alpha channel the
			 * global alpha value is applied in the unpacking unit,
			 * the alpha multiplier isn't needed and must be
			 * disabled.
			 */
			mult = VI6_RPF_MULT_ALPHA_A_MMD_NONE
			     | VI6_RPF_MULT_ALPHA_P_MMD_NONE;
		}

		rpf->mult_alpha = mult;
	}

	vsp1_rpf_write(rpf, dlb, VI6_RPF_MSK_CTRL, 0);
	vsp1_rpf_write(rpf, dlb, VI6_RPF_CKEY_CTRL, 0);

}

static void vsp1_rpf_configure_autofld(struct vsp1_rwpf *rpf,
				       struct vsp1_dl_list *dl)
{
	const struct v4l2_pix_format_mplane *format = &rpf->format;
	struct vsp1_dl_ext_cmd *cmd;
	struct vsp1_extcmd_auto_fld_body *auto_fld;
	u32 offset_y, offset_c;

	cmd = vsp1_dl_get_pre_cmd(dl);
	if (WARN_ONCE(!cmd, "Failed to obtain an autofld cmd"))
		return;

	/* Re-index our auto_fld to match the current RPF. */
	auto_fld = cmd->data;
	auto_fld = &auto_fld[rpf->entity.index];

	auto_fld->top_y0 = rpf->mem.addr[0];
	auto_fld->top_c0 = rpf->mem.addr[1];
	auto_fld->top_c1 = rpf->mem.addr[2];

	offset_y = format->plane_fmt[0].bytesperline;
	offset_c = format->plane_fmt[1].bytesperline;

	auto_fld->bottom_y0 = rpf->mem.addr[0] + offset_y;
	auto_fld->bottom_c0 = rpf->mem.addr[1] + offset_c;
	auto_fld->bottom_c1 = rpf->mem.addr[2] + offset_c;

	cmd->flags |= VI6_DL_EXT_AUTOFLD_INT | BIT(16 + rpf->entity.index);
}

static void rpf_configure_frame(struct vsp1_entity *entity,
				struct vsp1_pipeline *pipe,
				struct vsp1_dl_list *dl,
				struct vsp1_dl_body *dlb)
{
	struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);

	vsp1_rpf_write(rpf, dlb, VI6_RPF_VRTCOL_SET,
		       rpf->alpha << VI6_RPF_VRTCOL_SET_LAYA_SHIFT);
	vsp1_rpf_write(rpf, dlb, VI6_RPF_MULT_ALPHA, rpf->mult_alpha |
		       (rpf->alpha << VI6_RPF_MULT_ALPHA_RATIO_SHIFT));

	vsp1_pipeline_propagate_alpha(pipe, dlb, rpf->alpha);
}

static void rpf_configure_partition(struct vsp1_entity *entity,
				    struct vsp1_pipeline *pipe,
				    struct vsp1_dl_list *dl,
				    struct vsp1_dl_body *dlb)
{
	struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);
	struct vsp1_rwpf_memory mem = rpf->mem;
	struct vsp1_device *vsp1 = rpf->entity.vsp1;
	const struct vsp1_format_info *fmtinfo = rpf->fmtinfo;
	const struct v4l2_pix_format_mplane *format = &rpf->format;
	struct v4l2_rect crop;

	/*
	 * Source size and crop offsets.
	 *
	 * The crop offsets correspond to the location of the crop
	 * rectangle top left corner in the plane buffer. Only two
	 * offsets are needed, as planes 2 and 3 always have identical
	 * strides.
	 */
	crop = *vsp1_rwpf_get_crop(rpf, rpf->entity.state);

	/*
	 * Partition Algorithm Control
	 *
	 * The partition algorithm can split this frame into multiple
	 * slices. We must scale our partition window based on the pipe
	 * configuration to match the destination partition window.
	 * To achieve this, we adjust our crop to provide a 'sub-crop'
	 * matching the expected partition window. Only 'left' and
	 * 'width' need to be adjusted.
	 */
	if (pipe->partitions > 1) {
		crop.width = pipe->partition->rpf.width;
		crop.left += pipe->partition->rpf.left;
	}

	if (pipe->interlaced) {
		crop.height = round_down(crop.height / 2, fmtinfo->vsub);
		crop.top = round_down(crop.top / 2, fmtinfo->vsub);
	}

	vsp1_rpf_write(rpf, dlb, VI6_RPF_SRC_BSIZE,
		       (crop.width << VI6_RPF_SRC_BSIZE_BHSIZE_SHIFT) |
		       (crop.height << VI6_RPF_SRC_BSIZE_BVSIZE_SHIFT));
	vsp1_rpf_write(rpf, dlb, VI6_RPF_SRC_ESIZE,
		       (crop.width << VI6_RPF_SRC_ESIZE_EHSIZE_SHIFT) |
		       (crop.height << VI6_RPF_SRC_ESIZE_EVSIZE_SHIFT));

	mem.addr[0] += crop.top * format->plane_fmt[0].bytesperline
		     + crop.left * fmtinfo->bpp[0] / 8;

	if (format->num_planes > 1) {
		unsigned int bpl = format->plane_fmt[1].bytesperline;
		unsigned int offset;

		offset = crop.top / fmtinfo->vsub * bpl
		       + crop.left / fmtinfo->hsub * fmtinfo->bpp[1] / 8;
		mem.addr[1] += offset;
		mem.addr[2] += offset;
	}

	/*
	 * On Gen3+ hardware the SPUVS bit has no effect on 3-planar
	 * formats. Swap the U and V planes manually in that case.
	 */
	if (vsp1->info->gen >= 3 && format->num_planes == 3 &&
	    fmtinfo->swap_uv)
		swap(mem.addr[1], mem.addr[2]);

	/*
	 * Interlaced pipelines will use the extended pre-cmd to process
	 * SRCM_ADDR_{Y,C0,C1}.
	 */
	if (pipe->interlaced) {
		vsp1_rpf_configure_autofld(rpf, dl);
	} else {
		vsp1_rpf_write(rpf, dlb, VI6_RPF_SRCM_ADDR_Y, mem.addr[0]);
		vsp1_rpf_write(rpf, dlb, VI6_RPF_SRCM_ADDR_C0, mem.addr[1]);
		vsp1_rpf_write(rpf, dlb, VI6_RPF_SRCM_ADDR_C1, mem.addr[2]);
	}
}

static void rpf_partition(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_partition *partition,
			  unsigned int partition_idx,
			  struct vsp1_partition_window *window)
{
	partition->rpf = *window;
}

static const struct vsp1_entity_operations rpf_entity_ops = {
	.configure_stream = rpf_configure_stream,
	.configure_frame = rpf_configure_frame,
	.configure_partition = rpf_configure_partition,
	.partition = rpf_partition,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_rpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct vsp1_rwpf *rpf;
	char name[6];
	int ret;

	rpf = devm_kzalloc(vsp1->dev, sizeof(*rpf), GFP_KERNEL);
	if (rpf == NULL)
		return ERR_PTR(-ENOMEM);

	rpf->max_width = RPF_MAX_WIDTH;
	rpf->max_height = RPF_MAX_HEIGHT;

	rpf->entity.ops = &rpf_entity_ops;
	rpf->entity.type = VSP1_ENTITY_RPF;
	rpf->entity.index = index;

	sprintf(name, "rpf.%u", index);
	ret = vsp1_entity_init(vsp1, &rpf->entity, name, 2, &vsp1_rwpf_subdev_ops,
			       MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	ret = vsp1_rwpf_init_ctrls(rpf, 0);
	if (ret < 0) {
		dev_err(vsp1->dev, "rpf%u: failed to initialize controls\n",
			index);
		goto error;
	}

	v4l2_ctrl_handler_setup(&rpf->ctrls);

	return rpf;

error:
	vsp1_entity_destroy(&rpf->entity);
	return ERR_PTR(ret);
}
