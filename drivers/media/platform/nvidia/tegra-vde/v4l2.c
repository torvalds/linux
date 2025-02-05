// SPDX-License-Identifier: GPL-2.0+
/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (C) 2019-2022 Dmitry Osipenko <digetx@gmail.com>
 *
 * Based on Cedrus driver by Bootlin.
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 *
 * Based on Rockchip driver by Collabora.
 * Copyright (C) 2019 Boris Brezillon <boris.brezillon@collabora.com>
 */

#include <linux/err.h>
#include <linux/slab.h>

#include "vde.h"

static const struct v4l2_ctrl_config ctrl_cfgs[] = {
	{	.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,	},
	{	.id = V4L2_CID_STATELESS_H264_SPS,		},
	{	.id = V4L2_CID_STATELESS_H264_PPS,		},
	{
		.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
		.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
	},
	{
		.id = V4L2_CID_STATELESS_H264_START_CODE,
		.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
		.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
	},
};

static inline struct tegra_ctx *fh_to_tegra_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct tegra_ctx, fh);
}

static void tegra_set_control_data(struct tegra_ctx *ctx, void *data, u32 id)
{
	switch (id) {
	case V4L2_CID_STATELESS_H264_DECODE_PARAMS:
		ctx->h264.decode_params = data;
		break;
	case V4L2_CID_STATELESS_H264_SPS:
		ctx->h264.sps = data;
		break;
	case V4L2_CID_STATELESS_H264_PPS:
		ctx->h264.pps = data;
		break;
	}
}

void tegra_vde_prepare_control_data(struct tegra_ctx *ctx, u32 id)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ctrl_cfgs); i++) {
		if (ctx->ctrls[i]->id == id) {
			tegra_set_control_data(ctx, ctx->ctrls[i]->p_cur.p, id);
			return;
		}
	}

	tegra_set_control_data(ctx, NULL, id);
}

static int tegra_queue_setup(struct vb2_queue *vq,
			     unsigned int *nbufs,
			     unsigned int *num_planes,
			     unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct tegra_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes)
			return -EINVAL;

		for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
			if (sizes[i] < f->fmt.pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*num_planes = f->fmt.pix_mp.num_planes;

		for (i = 0; i < f->fmt.pix_mp.num_planes; i++)
			sizes[i] = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	return 0;
}

static int tegra_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static void __tegra_buf_cleanup(struct vb2_buffer *vb, unsigned int i)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct tegra_ctx *ctx = vb2_get_drv_priv(vq);
	struct tegra_m2m_buffer *tb = vb_to_tegra_buf(vb);

	while (i--) {
		if (tb->a[i]) {
			tegra_vde_dmabuf_cache_unmap(ctx->vde, tb->a[i], true);
			tb->a[i] = NULL;
		}

		if (tb->iova[i]) {
			tegra_vde_iommu_unmap(ctx->vde, tb->iova[i]);
			tb->iova[i] = NULL;
		}
	}

	if (tb->aux) {
		tegra_vde_free_bo(tb->aux);
		tb->aux = NULL;
	}
}

static int tegra_buf_init(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct tegra_ctx *ctx = vb2_get_drv_priv(vq);
	struct tegra_m2m_buffer *tb = vb_to_tegra_buf(vb);
	struct tegra_vde *vde = ctx->vde;
	enum dma_data_direction dma_dir;
	struct sg_table *sgt;
	unsigned int i;
	int err;

	if (V4L2_TYPE_IS_CAPTURE(vq->type) && vb->num_planes > 1) {
		/*
		 * Tegra decoder writes auxiliary data for I/P frames.
		 * This data is needed for decoding of B frames.
		 */
		err = tegra_vde_alloc_bo(vde, &tb->aux, DMA_FROM_DEVICE,
					 vb2_plane_size(vb, 1));
		if (err)
			return err;
	}

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		dma_dir = DMA_TO_DEVICE;
	else
		dma_dir = DMA_FROM_DEVICE;

	for (i = 0; i < vb->num_planes; i++) {
		if (vq->memory == VB2_MEMORY_DMABUF) {
			get_dma_buf(vb->planes[i].dbuf);

			err = tegra_vde_dmabuf_cache_map(vde, vb->planes[i].dbuf,
							 dma_dir, &tb->a[i],
							 &tb->dma_base[i]);
			if (err) {
				dma_buf_put(vb->planes[i].dbuf);
				goto cleanup;
			}

			continue;
		}

		if (vde->domain) {
			sgt = vb2_dma_sg_plane_desc(vb, i);

			err = tegra_vde_iommu_map(vde, sgt, &tb->iova[i],
						  vb2_plane_size(vb, i));
			if (err)
				goto cleanup;

			tb->dma_base[i] = iova_dma_addr(&vde->iova, tb->iova[i]);
		} else {
			tb->dma_base[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}

	return 0;

cleanup:
	__tegra_buf_cleanup(vb, i);

	return err;
}

static void tegra_buf_cleanup(struct vb2_buffer *vb)
{
	__tegra_buf_cleanup(vb, vb->num_planes);
}

static int tegra_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct tegra_ctx *ctx = vb2_get_drv_priv(vq);
	struct tegra_m2m_buffer *tb = vb_to_tegra_buf(vb);
	size_t hw_align, hw_size, hw_payload, size, offset;
	struct v4l2_pix_format_mplane *pixfmt;
	unsigned int i;
	void *vb_data;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		hw_align = BSEV_ALIGN;
		pixfmt = &ctx->coded_fmt.fmt.pix_mp;
	} else {
		hw_align = FRAMEID_ALIGN;
		pixfmt = &ctx->decoded_fmt.fmt.pix_mp;
	}

	for (i = 0; i < vb->num_planes; i++) {
		offset = vb->planes[i].data_offset;

		if (offset & (hw_align - 1))
			return -EINVAL;

		if (V4L2_TYPE_IS_CAPTURE(vq->type)) {
			size = pixfmt->plane_fmt[i].sizeimage;
			hw_payload = ALIGN(size, VDE_ATOM);
		} else {
			size = vb2_get_plane_payload(vb, i) - offset;
			hw_payload = ALIGN(size + VDE_ATOM, SXE_BUFFER);
		}

		hw_size = offset + hw_payload;

		if (vb2_plane_size(vb, i) < hw_size)
			return -EINVAL;

		vb2_set_plane_payload(vb, i, hw_payload);

		if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
			vb_data = vb2_plane_vaddr(vb, i);

			/*
			 * Hardware requires zero-padding of coded data.
			 * Otherwise it will fail to parse the trailing
			 * data and abort the decoding.
			 */
			if (vb_data)
				memset(vb_data + offset + size, 0,
				       hw_size - offset - size);
		}

		tb->dma_addr[i] = tb->dma_base[i] + offset;
	}

	switch (pixfmt->pixelformat) {
	case V4L2_PIX_FMT_YVU420M:
		swap(tb->dma_addr[1], tb->dma_addr[2]);
		break;
	}

	return 0;
}

static void tegra_buf_queue(struct vb2_buffer *vb)
{
	struct tegra_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void tegra_buf_request_complete(struct vb2_buffer *vb)
{
	struct tegra_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->hdl);
}

static int tegra_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	return 0;
}

static void tegra_stop_streaming(struct vb2_queue *vq)
{
	struct tegra_ctx *ctx = vb2_get_drv_priv(vq);

	while (true) {
		struct vb2_v4l2_buffer *vbuf;

		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req, &ctx->hdl);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops tegra_qops = {
	.queue_setup = tegra_queue_setup,
	.buf_init = tegra_buf_init,
	.buf_cleanup = tegra_buf_cleanup,
	.buf_prepare = tegra_buf_prepare,
	.buf_queue = tegra_buf_queue,
	.buf_out_validate = tegra_buf_out_validate,
	.buf_request_complete = tegra_buf_request_complete,
	.start_streaming = tegra_start_streaming,
	.stop_streaming = tegra_stop_streaming,
};

static int tegra_queue_init(void *priv,
			    struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq)
{
	struct tegra_ctx *ctx = priv;
	struct tegra_vde *vde = ctx->vde;
	const struct vb2_mem_ops *mem_ops;
	unsigned long dma_attrs;
	int err;

	/*
	 * TODO: Switch to use of vb2_dma_contig_memops uniformly once we
	 * will add IOMMU_DOMAIN support for video decoder to tegra-smmu
	 * driver. For now we need to stick with SG ops in order to be able
	 * to get SGT table easily. This is suboptimal since SG mappings are
	 * wasting CPU cache and we don't need that caching.
	 */
	if (vde->domain)
		mem_ops = &vb2_dma_sg_memops;
	else
		mem_ops = &vb2_dma_contig_memops;

	dma_attrs = DMA_ATTR_WRITE_COMBINE;

	src_vq->buf_struct_size = sizeof(struct tegra_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;
	src_vq->lock = &vde->v4l2_lock;
	src_vq->dma_attrs = dma_attrs;
	src_vq->mem_ops = mem_ops;
	src_vq->ops = &tegra_qops;
	src_vq->drv_priv = ctx;
	src_vq->dev = vde->dev;

	err = vb2_queue_init(src_vq);
	if (err) {
		v4l2_err(&vde->v4l2_dev,
			 "failed to initialize src queue: %d\n", err);
		return err;
	}

	/*
	 * We may need to zero the end of bitstream in kernel if userspace
	 * doesn't do that, hence kmap is needed for the coded data. It's not
	 * needed for framebuffers.
	 */
	dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

	dst_vq->buf_struct_size = sizeof(struct tegra_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->lock = &vde->v4l2_lock;
	dst_vq->dma_attrs = dma_attrs;
	dst_vq->mem_ops = mem_ops;
	dst_vq->ops = &tegra_qops;
	dst_vq->drv_priv = ctx;
	dst_vq->dev = vde->dev;

	err = vb2_queue_init(dst_vq);
	if (err) {
		v4l2_err(&vde->v4l2_dev,
			 "failed to initialize dst queue: %d\n", err);
		return err;
	}

	return 0;
}

static void tegra_reset_fmt(struct tegra_ctx *ctx, struct v4l2_format *f,
			    u32 fourcc)
{
	memset(f, 0, sizeof(*f));
	f->fmt.pix_mp.pixelformat = fourcc;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void tegra_reset_coded_fmt(struct tegra_ctx *ctx)
{
	const struct tegra_vde_soc *soc = ctx->vde->soc;
	struct v4l2_format *f = &ctx->coded_fmt;

	ctx->coded_fmt_desc = &soc->coded_fmts[0];
	tegra_reset_fmt(ctx, f, ctx->coded_fmt_desc->fourcc);

	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.width = ctx->coded_fmt_desc->frmsize.min_width;
	f->fmt.pix_mp.height = ctx->coded_fmt_desc->frmsize.min_height;
}

static void tegra_fill_pixfmt_mp(struct v4l2_pix_format_mplane *pixfmt,
				 u32 pixelformat, u32 width, u32 height)
{
	const struct v4l2_format_info *info = v4l2_format_info(pixelformat);
	struct v4l2_plane_pix_format *plane;
	unsigned int i;

	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		pixfmt->width = width;
		pixfmt->height = height;
		pixfmt->pixelformat = pixelformat;
		pixfmt->num_planes = info->mem_planes;

		for (i = 0; i < pixfmt->num_planes; i++) {
			unsigned int hdiv = (i == 0) ? 1 : 2;
			unsigned int vdiv = (i == 0) ? 1 : 2;

			/*
			 * VDE is connected to Graphics Memory using 128bit port,
			 * all memory accesses are made using 16B atoms.
			 *
			 * V4L requires Cb/Cr strides to be exactly half of the
			 * Y stride, hence we're aligning Y to 16B x 2.
			 */
			plane = &pixfmt->plane_fmt[i];
			plane->bytesperline = ALIGN(width, VDE_ATOM * 2) / hdiv;
			plane->sizeimage = plane->bytesperline * height / vdiv;
		}

		break;
	}
}

static void tegra_reset_decoded_fmt(struct tegra_ctx *ctx)
{
	struct v4l2_format *f = &ctx->decoded_fmt;

	tegra_reset_fmt(ctx, f, ctx->coded_fmt_desc->decoded_fmts[0]);
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	tegra_fill_pixfmt_mp(&f->fmt.pix_mp,
			     ctx->coded_fmt_desc->decoded_fmts[0],
			     ctx->coded_fmt.fmt.pix_mp.width,
			     ctx->coded_fmt.fmt.pix_mp.height);
}

static void tegra_job_finish(struct tegra_ctx *ctx,
			     enum vb2_buffer_state result)
{
	v4l2_m2m_buf_done_and_job_finish(ctx->vde->m2m, ctx->fh.m2m_ctx,
					 result);
}

static void tegra_decode_complete(struct work_struct *work)
{
	struct tegra_ctx *ctx = container_of(work, struct tegra_ctx, work);
	int err;

	err = ctx->coded_fmt_desc->decode_wait(ctx);
	if (err)
		tegra_job_finish(ctx, VB2_BUF_STATE_ERROR);
	else
		tegra_job_finish(ctx, VB2_BUF_STATE_DONE);
}

static int tegra_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	strscpy(cap->bus_info, "platform:tegra-vde", sizeof(cap->bus_info));
	strscpy(cap->driver, "tegra-vde", sizeof(cap->driver));
	strscpy(cap->card, "tegra-vde", sizeof(cap->card));

	return 0;
}

static int tegra_enum_decoded_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);

	if (WARN_ON(!ctx->coded_fmt_desc))
		return -EINVAL;

	if (f->index >= ctx->coded_fmt_desc->num_decoded_fmts)
		return -EINVAL;

	f->pixelformat = ctx->coded_fmt_desc->decoded_fmts[f->index];

	return 0;
}

static int tegra_g_decoded_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);

	*f = ctx->decoded_fmt;
	return 0;
}

static int tegra_try_decoded_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	const struct tegra_coded_fmt_desc *coded_desc;
	unsigned int i;

	/*
	 * The codec context should point to a coded format desc, if the format
	 * on the coded end has not been set yet, it should point to the
	 * default value.
	 */
	coded_desc = ctx->coded_fmt_desc;
	if (WARN_ON(!coded_desc))
		return -EINVAL;

	if (!coded_desc->num_decoded_fmts)
		return -EINVAL;

	for (i = 0; i < coded_desc->num_decoded_fmts; i++) {
		if (coded_desc->decoded_fmts[i] == pix_mp->pixelformat)
			break;
	}

	if (i == coded_desc->num_decoded_fmts)
		pix_mp->pixelformat = coded_desc->decoded_fmts[0];

	/* always apply the frmsize constraint of the coded end */
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	tegra_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat,
			     pix_mp->width, pix_mp->height);
	pix_mp->field = V4L2_FIELD_NONE;

	return 0;
}

static int tegra_s_decoded_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	struct vb2_queue *vq;
	int err;

	/* change not allowed if queue is busy */
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(vq))
		return -EBUSY;

	err = tegra_try_decoded_fmt(file, priv, f);
	if (err)
		return err;

	ctx->decoded_fmt = *f;

	return 0;
}

static int tegra_enum_coded_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	const struct tegra_vde_soc *soc = ctx->vde->soc;

	if (f->index >= soc->num_coded_fmts)
		return -EINVAL;

	f->pixelformat = soc->coded_fmts[f->index].fourcc;

	return 0;
}

static int tegra_g_coded_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);

	*f = ctx->coded_fmt;
	return 0;
}

static const struct tegra_coded_fmt_desc *
tegra_find_coded_fmt_desc(struct tegra_ctx *ctx, u32 fourcc)
{
	const struct tegra_vde_soc *soc = ctx->vde->soc;
	unsigned int i;

	for (i = 0; i < soc->num_coded_fmts; i++) {
		if (soc->coded_fmts[i].fourcc == fourcc)
			return &soc->coded_fmts[i];
	}

	return NULL;
}

static int tegra_try_coded_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	const struct tegra_vde_soc *soc = ctx->vde->soc;
	int size = pix_mp->plane_fmt[0].sizeimage;
	const struct tegra_coded_fmt_desc *desc;

	desc = tegra_find_coded_fmt_desc(ctx, pix_mp->pixelformat);
	if (!desc) {
		pix_mp->pixelformat = soc->coded_fmts[0].fourcc;
		desc = &soc->coded_fmts[0];
	}

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &desc->frmsize);

	pix_mp->plane_fmt[0].sizeimage = max(ALIGN(size, SXE_BUFFER), SZ_2M);
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->num_planes = 1;

	return 0;
}

static int tegra_s_coded_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct tegra_coded_fmt_desc *desc;
	struct vb2_queue *peer_vq, *vq;
	struct v4l2_format *cap_fmt;
	int err;

	/*
	 * In order to support dynamic resolution change, the decoder admits
	 * a resolution change, as long as the pixelformat remains. Can't be
	 * done if streaming.
	 */
	vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (vb2_is_streaming(vq) ||
	    (vb2_is_busy(vq) &&
	     f->fmt.pix_mp.pixelformat != ctx->coded_fmt.fmt.pix_mp.pixelformat))
		return -EBUSY;

	/*
	 * Since format change on the OUTPUT queue will reset the CAPTURE
	 * queue, we can't allow doing so when the CAPTURE queue has buffers
	 * allocated.
	 */
	peer_vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	err = tegra_try_coded_fmt(file, priv, f);
	if (err)
		return err;

	desc = tegra_find_coded_fmt_desc(ctx, f->fmt.pix_mp.pixelformat);
	if (!desc)
		return -EINVAL;

	ctx->coded_fmt_desc = desc;
	ctx->coded_fmt = *f;

	/*
	 * Current decoded format might have become invalid with newly
	 * selected codec, so reset it to default just to be safe and
	 * keep internal driver state sane. User is mandated to set
	 * the decoded format again after we return, so we don't need
	 * anything smarter.
	 *
	 * Note that this will propagates any size changes to the decoded format.
	 */
	tegra_reset_decoded_fmt(ctx);

	/* propagate colorspace information to capture */
	cap_fmt = &ctx->decoded_fmt;
	cap_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	cap_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	cap_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	cap_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	return 0;
}

static int tegra_enum_framesizes(struct file *file, void *priv,
				 struct v4l2_frmsizeenum *fsize)
{
	struct tegra_ctx *ctx = fh_to_tegra_ctx(priv);
	const struct tegra_coded_fmt_desc *fmt;

	if (fsize->index)
		return -EINVAL;

	fmt = tegra_find_coded_fmt_desc(ctx, fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;

	return 0;
}

static const struct v4l2_ioctl_ops tegra_v4l2_ioctl_ops = {
	.vidioc_querycap = tegra_querycap,
	.vidioc_enum_framesizes = tegra_enum_framesizes,

	.vidioc_try_fmt_vid_out_mplane = tegra_try_coded_fmt,
	.vidioc_g_fmt_vid_out_mplane = tegra_g_coded_fmt,
	.vidioc_s_fmt_vid_out_mplane = tegra_s_coded_fmt,
	.vidioc_enum_fmt_vid_out = tegra_enum_coded_fmt,

	.vidioc_try_fmt_vid_cap_mplane = tegra_try_decoded_fmt,
	.vidioc_g_fmt_vid_cap_mplane = tegra_g_decoded_fmt,
	.vidioc_s_fmt_vid_cap_mplane = tegra_s_decoded_fmt,
	.vidioc_enum_fmt_vid_cap = tegra_enum_decoded_fmt,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int tegra_init_ctrls(struct tegra_ctx *ctx)
{
	unsigned int i;
	int err;

	err = v4l2_ctrl_handler_init(&ctx->hdl, ARRAY_SIZE(ctrl_cfgs));
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(ctrl_cfgs); i++) {
		ctx->ctrls[i] = v4l2_ctrl_new_custom(&ctx->hdl, &ctrl_cfgs[i],
						     NULL);
		if (ctx->hdl.error) {
			err = ctx->hdl.error;
			goto free_ctrls;
		}
	}

	err = v4l2_ctrl_handler_setup(&ctx->hdl);
	if (err)
		goto free_ctrls;

	ctx->fh.ctrl_handler = &ctx->hdl;

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&ctx->hdl);

	return err;
}

static int tegra_init_m2m(struct tegra_ctx *ctx)
{
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(ctx->vde->m2m,
					    ctx, tegra_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx))
		return PTR_ERR(ctx->fh.m2m_ctx);

	return 0;
}

static int tegra_open(struct file *file)
{
	struct tegra_vde *vde = video_drvdata(file);
	struct tegra_ctx *ctx;
	int err;

	ctx = kzalloc(struct_size(ctx, ctrls, ARRAY_SIZE(ctrl_cfgs)),
		      GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->vde = vde;
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	INIT_WORK(&ctx->work, tegra_decode_complete);

	err = tegra_init_ctrls(ctx);
	if (err) {
		v4l2_err(&vde->v4l2_dev, "failed to add controls: %d\n", err);
		goto free_ctx;
	}

	err = tegra_init_m2m(ctx);
	if (err) {
		v4l2_err(&vde->v4l2_dev, "failed to initialize m2m: %d\n", err);
		goto free_ctrls;
	}

	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	tegra_reset_coded_fmt(ctx);
	tegra_try_coded_fmt(file, file->private_data, &ctx->coded_fmt);

	tegra_reset_decoded_fmt(ctx);
	tegra_try_decoded_fmt(file, file->private_data, &ctx->decoded_fmt);

	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&ctx->hdl);
free_ctx:
	kfree(ctx);

	return err;
}

static int tegra_release(struct file *file)
{
	struct v4l2_fh *fh = file->private_data;
	struct tegra_ctx *ctx = fh_to_tegra_ctx(fh);
	struct tegra_vde *vde = ctx->vde;

	v4l2_fh_del(fh);
	v4l2_m2m_ctx_release(fh->m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_fh_exit(fh);
	kfree(ctx);

	tegra_vde_dmabuf_cache_unmap_sync(vde);

	return 0;
}

static const struct v4l2_file_operations tegra_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = tegra_open,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
	.release = tegra_release,
	.unlocked_ioctl = video_ioctl2,
};

static void tegra_device_run(void *priv)
{
	struct tegra_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	struct media_request *src_req = src->vb2_buf.req_obj.req;
	int err;

	v4l2_ctrl_request_setup(src_req, &ctx->hdl);

	err = ctx->coded_fmt_desc->decode_run(ctx);

	v4l2_ctrl_request_complete(src_req, &ctx->hdl);

	if (err)
		tegra_job_finish(ctx, VB2_BUF_STATE_ERROR);
	else
		queue_work(ctx->vde->wq, &ctx->work);
}

static const struct v4l2_m2m_ops tegra_v4l2_m2m_ops = {
	.device_run = tegra_device_run,
};

static int tegra_request_validate(struct media_request *req)
{
	unsigned int count;

	count = vb2_request_buffer_cnt(req);
	if (!count)
		return -ENOENT;
	else if (count > 1)
		return -EINVAL;

	return vb2_request_validate(req);
}

static const struct media_device_ops tegra_media_device_ops = {
	.req_validate = tegra_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

int tegra_vde_v4l2_init(struct tegra_vde *vde)
{
	struct device *dev = vde->dev;
	int err;

	mutex_init(&vde->v4l2_lock);
	media_device_init(&vde->mdev);
	video_set_drvdata(&vde->vdev, vde);

	vde->vdev.lock = &vde->v4l2_lock;
	vde->vdev.fops = &tegra_v4l2_fops;
	vde->vdev.vfl_dir = VFL_DIR_M2M;
	vde->vdev.release = video_device_release_empty;
	vde->vdev.v4l2_dev = &vde->v4l2_dev;
	vde->vdev.ioctl_ops = &tegra_v4l2_ioctl_ops;
	vde->vdev.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	vde->v4l2_dev.mdev = &vde->mdev;
	vde->mdev.ops = &tegra_media_device_ops;
	vde->mdev.dev = dev;

	strscpy(vde->mdev.model, "tegra-vde", sizeof(vde->mdev.model));
	strscpy(vde->vdev.name,  "tegra-vde", sizeof(vde->vdev.name));
	strscpy(vde->mdev.bus_info, "platform:tegra-vde",
		sizeof(vde->mdev.bus_info));

	vde->wq = create_workqueue("tegra-vde");
	if (!vde->wq)
		return -ENOMEM;

	err = media_device_register(&vde->mdev);
	if (err) {
		dev_err(dev, "failed to register media device: %d\n", err);
		goto clean_up_media_device;
	}

	err = v4l2_device_register(dev, &vde->v4l2_dev);
	if (err) {
		dev_err(dev, "failed to register v4l2 device: %d\n", err);
		goto unreg_media_device;
	}

	err = video_register_device(&vde->vdev, VFL_TYPE_VIDEO, -1);
	if (err) {
		dev_err(dev, "failed to register video device: %d\n", err);
		goto unreg_v4l2;
	}

	vde->m2m = v4l2_m2m_init(&tegra_v4l2_m2m_ops);
	err = PTR_ERR_OR_ZERO(vde->m2m);
	if (err) {
		dev_err(dev, "failed to initialize m2m device: %d\n", err);
		goto unreg_video_device;
	}

	err = v4l2_m2m_register_media_controller(vde->m2m, &vde->vdev,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (err) {
		dev_err(dev, "failed to register media controller: %d\n", err);
		goto release_m2m;
	}

	v4l2_info(&vde->v4l2_dev, "v4l2 device registered as /dev/video%d\n",
		  vde->vdev.num);

	return 0;

release_m2m:
	v4l2_m2m_release(vde->m2m);
unreg_video_device:
	video_unregister_device(&vde->vdev);
unreg_v4l2:
	v4l2_device_unregister(&vde->v4l2_dev);
unreg_media_device:
	media_device_unregister(&vde->mdev);
clean_up_media_device:
	media_device_cleanup(&vde->mdev);

	destroy_workqueue(vde->wq);

	return err;
}

void tegra_vde_v4l2_deinit(struct tegra_vde *vde)
{
	v4l2_m2m_unregister_media_controller(vde->m2m);
	v4l2_m2m_release(vde->m2m);

	video_unregister_device(&vde->vdev);
	v4l2_device_unregister(&vde->v4l2_dev);

	media_device_unregister(&vde->mdev);
	media_device_cleanup(&vde->mdev);

	destroy_workqueue(vde->wq);
}
