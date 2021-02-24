// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <linux/pm_runtime.h>
#include <linux/rkispp-config.h>

#include "hw.h"
#include "ispp.h"
#include "regs.h"
#include "stream.h"
#include "common.h"

static const struct vb2_mem_ops *g_ops = &vb2_dma_contig_memops;

static int fec_running(struct rkispp_fec_dev *fec,
			struct rkispp_fec_in_out *buf)
{
	u32 in_fmt, out_fmt, in_mult = 1, out_mult = 1;
	u32 in_size, in_offs, out_size, out_offs, val;
	u32 w = buf->width, h = buf->height, density, mesh_size;
	struct dma_buf *in_dbuf, *out_dbuf;
	struct dma_buf *xint_dbuf, *xfra_dbuf, *yint_dbuf, *yfra_dbuf;
	void *in_mem, *out_mem;
	void *xint_mem, *xfra_mem, *yint_mem, *yfra_mem;
	void __iomem *base = fec->hw->base_addr;
	int ret = -EINVAL;

	v4l2_dbg(3, rkispp_debug, &fec->v4l2_dev,
		 "%s enter %dx%d format(in:%c%c%c%c out:%c%c%c%c)\n",
		 __func__, w, h,
		 buf->in_fourcc, buf->in_fourcc >> 8,
		 buf->in_fourcc >> 16, buf->in_fourcc >> 24,
		 buf->out_fourcc, buf->out_fourcc >> 8,
		 buf->out_fourcc >> 16, buf->out_fourcc >> 24);

	if (clk_get_rate(fec->hw->clks[0]) <= fec->hw->core_clk_min)
		rkispp_set_clk_rate(fec->hw->clks[0], fec->hw->core_clk_max);

	init_completion(&fec->cmpl);
	density = w > 1920 ? SW_MESH_DENSITY : 0;
	mesh_size = cal_fec_mesh(w, h, !!density);

	switch (buf->in_fourcc) {
	case V4L2_PIX_FMT_YUYV:
		in_fmt = FMT_YC_SWAP | FMT_YUYV | FMT_YUV422;
		in_mult = 2;
		break;
	case V4L2_PIX_FMT_UYVY:
		in_fmt = FMT_YUYV | FMT_YUV422;
		in_mult = 2;
		break;
	case V4L2_PIX_FMT_NV16:
		in_fmt = FMT_YUV422;
		break;
	case V4L2_PIX_FMT_NV12:
		in_fmt = FMT_YUV420;
		break;
	default:
		v4l2_err(&fec->v4l2_dev,
			 "no support in format:%c%c%c%c\n",
			 buf->in_fourcc, buf->in_fourcc >> 8,
			 buf->in_fourcc >> 16, buf->in_fourcc >> 24);
		ret = -EINVAL;
		goto end;
	}
	in_offs = w * h;
	in_size = (in_fmt & FMT_YUV422) ?
		w * h * 2 : w * h * 3 / 2;

	switch (buf->out_fourcc) {
	case V4L2_PIX_FMT_YUYV:
		out_fmt = FMT_YC_SWAP | FMT_YUYV | FMT_YUV422;
		out_mult = 2;
		break;
	case V4L2_PIX_FMT_UYVY:
		out_fmt = FMT_YUYV | FMT_YUV422;
		out_mult = 2;
		break;
	case V4L2_PIX_FMT_NV16:
		out_fmt = FMT_YUV422;
		break;
	case V4L2_PIX_FMT_NV12:
		out_fmt = FMT_YUV420;
		break;
	case V4L2_PIX_FMT_FBC2:
		out_fmt = FMT_YUV422 | FMT_FBC;
		break;
	case V4L2_PIX_FMT_FBC0:
		out_fmt = FMT_YUV420 | FMT_FBC;
		break;
	default:
		v4l2_err(&fec->v4l2_dev, "no support out format:%c%c%c%c\n",
			 buf->out_fourcc, buf->out_fourcc >> 8,
			 buf->out_fourcc >> 16, buf->out_fourcc >> 24);
		ret = -EINVAL;
		goto end;
	}
	out_size = 0;
	out_offs = w * h;
	if (out_fmt & FMT_FBC) {
		w = ALIGN(w, 16);
		h = ALIGN(h, 16);
		out_offs = w * h >> 4;
		out_size = out_offs;
	}
	out_size += (out_fmt & FMT_YUV422) ?
		w * h * 2 : w * h * 3 / 2;

	/* input picture buf */
	in_dbuf = dma_buf_get(buf->in_pic_fd);
	if (IS_ERR_OR_NULL(in_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd:%d for in picture", buf->in_pic_fd);
		ret = -EINVAL;
		goto end;
	}
	if (in_dbuf->size < in_size) {
		v4l2_err(&fec->v4l2_dev,
			 "in picture size error:%zu < %u\n", in_dbuf->size, in_size);
		goto put_in_dbuf;
	}
	in_mem = g_ops->attach_dmabuf(fec->hw->dev, in_dbuf,
				in_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(in_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach in dmabuf\n");
		goto put_in_dbuf;
	}
	if (g_ops->map_dmabuf(in_mem))
		goto detach_in_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(in_mem));
	writel(val, base + RKISPP_FEC_RD_Y_BASE);
	val += in_offs;
	writel(val, base + RKISPP_FEC_RD_UV_BASE);

	/* output picture buf */
	out_dbuf = dma_buf_get(buf->out_pic_fd);
	if (IS_ERR_OR_NULL(out_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd:%d for out picture", buf->out_pic_fd);
		goto unmap_in_dbuf;
	}
	if (out_dbuf->size < out_size) {
		v4l2_err(&fec->v4l2_dev,
			 "out picture size error:%zu < %u\n", out_dbuf->size, out_size);
		goto put_out_dbuf;
	}
	out_mem = g_ops->attach_dmabuf(fec->hw->dev, out_dbuf,
				out_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(out_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach out dmabuf\n");
		goto put_out_dbuf;
	}
	if (g_ops->map_dmabuf(out_mem))
		goto detach_out_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(out_mem));
	writel(val, base + RKISPP_FEC_WR_Y_BASE);
	val += out_offs;
	writel(val, base + RKISPP_FEC_WR_UV_BASE);

	/* mesh xint buf */
	xint_dbuf = dma_buf_get(buf->mesh_xint_fd);
	if (IS_ERR_OR_NULL(xint_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd for xint picture");
		goto unmap_out_dbuf;
	}
	if (xint_dbuf->size < mesh_size * 2) {
		v4l2_err(&fec->v4l2_dev,
			 "mesh xint size error:%zu < %u\n", xint_dbuf->size, mesh_size * 2);
		goto put_xint_dbuf;
	}
	xint_mem = g_ops->attach_dmabuf(fec->hw->dev, xint_dbuf,
				xint_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(xint_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach xint dmabuf\n");
		goto put_xint_dbuf;
	}
	if (g_ops->map_dmabuf(xint_mem))
		goto detach_xint_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(xint_mem));
	writel(val, base + RKISPP_FEC_MESH_XINT_BASE);

	/* mesh xfra buf */
	xfra_dbuf = dma_buf_get(buf->mesh_xfra_fd);
	if (IS_ERR_OR_NULL(xfra_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd for xfra picture");
		goto unmap_xint_dbuf;
	}
	if (xfra_dbuf->size < mesh_size) {
		v4l2_err(&fec->v4l2_dev,
			 "mesh xfra size error:%zu < %u\n", xfra_dbuf->size, mesh_size);
		goto put_xfra_dbuf;
	}
	xfra_mem = g_ops->attach_dmabuf(fec->hw->dev, xfra_dbuf,
				xfra_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(xfra_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach xfra dmabuf\n");
		goto put_xfra_dbuf;
	}
	if (g_ops->map_dmabuf(xfra_mem))
		goto detach_xfra_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(xfra_mem));
	writel(val, base + RKISPP_FEC_MESH_XFRA_BASE);

	/* mesh yint buf */
	yint_dbuf = dma_buf_get(buf->mesh_yint_fd);
	if (IS_ERR_OR_NULL(yint_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd for yint picture");
		goto unmap_xfra_dbuf;
	}
	if (yint_dbuf->size < mesh_size * 2) {
		v4l2_err(&fec->v4l2_dev,
			 "mesh yint size error:%zu < %u\n", yint_dbuf->size, mesh_size * 2);
		goto put_yint_dbuf;
	}
	yint_mem = g_ops->attach_dmabuf(fec->hw->dev, yint_dbuf,
				yint_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(yint_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach yint dmabuf\n");
		goto put_yint_dbuf;
	}
	if (g_ops->map_dmabuf(yint_mem))
		goto detach_yint_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(yint_mem));
	writel(val, base + RKISPP_FEC_MESH_YINT_BASE);

	/* mesh yfra buf */
	yfra_dbuf = dma_buf_get(buf->mesh_yfra_fd);
	if (IS_ERR_OR_NULL(yfra_dbuf)) {
		v4l2_err(&fec->v4l2_dev,
			 "invalid dmabuf fd for yfra picture");
		goto unmap_yint_dbuf;
	}
	if (yfra_dbuf->size < mesh_size) {
		v4l2_err(&fec->v4l2_dev,
			 "mesh yfra size error:%zu < %u\n", yfra_dbuf->size, mesh_size);
		goto put_yfra_dbuf;
	}
	yfra_mem = g_ops->attach_dmabuf(fec->hw->dev, yfra_dbuf,
				yfra_dbuf->size, DMA_BIDIRECTIONAL);
	if (IS_ERR(yfra_mem)) {
		v4l2_err(&fec->v4l2_dev,
			 "failed to attach yfra dmabuf\n");
		goto put_yfra_dbuf;
	}
	if (g_ops->map_dmabuf(yfra_mem))
		goto detach_yfra_dbuf;
	val = *((dma_addr_t *)g_ops->cookie(yfra_mem));
	writel(val, base + RKISPP_FEC_MESH_YFRA_BASE);

	val = out_fmt << 4 | in_fmt;
	writel(val, base + RKISPP_FEC_CTRL);
	val = ALIGN(buf->width * in_mult, 16) >> 2;
	writel(val, base + RKISPP_FEC_RD_VIR_STRIDE);
	val = ALIGN(buf->width * out_mult, 16) >> 2;
	writel(val, base + RKISPP_FEC_WR_VIR_STRIDE);
	val = buf->height << 16 | buf->width;
	writel(val, base + RKISPP_FEC_PIC_SIZE);
	writel(mesh_size, base + RKISPP_FEC_MESH_SIZE);
	val = SW_FEC_EN | density;
	writel(val, base + RKISPP_FEC_CORE_CTRL);

	writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
	v4l2_dbg(3, rkispp_debug, &fec->v4l2_dev,
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n"
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n"
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n",
		 RKISPP_CTRL_SYS_STATUS, readl(base + RKISPP_CTRL_SYS_STATUS),
		 RKISPP_FEC_CTRL, readl(base + RKISPP_FEC_CTRL),
		 RKISPP_FEC_RD_VIR_STRIDE, readl(base + RKISPP_FEC_RD_VIR_STRIDE),
		 RKISPP_FEC_WR_VIR_STRIDE, readl(base + RKISPP_FEC_WR_VIR_STRIDE),
		 RKISPP_FEC_RD_Y_BASE_SHD, readl(base + RKISPP_FEC_RD_Y_BASE_SHD),
		 RKISPP_FEC_RD_UV_BASE_SHD, readl(base + RKISPP_FEC_RD_UV_BASE_SHD),
		 RKISPP_FEC_MESH_XINT_BASE_SHD, readl(base + RKISPP_FEC_MESH_XINT_BASE_SHD),
		 RKISPP_FEC_MESH_XFRA_BASE_SHD, readl(base + RKISPP_FEC_MESH_XFRA_BASE_SHD),
		 RKISPP_FEC_MESH_YINT_BASE_SHD, readl(base + RKISPP_FEC_MESH_YINT_BASE_SHD),
		 RKISPP_FEC_MESH_YFRA_BASE_SHD, readl(base + RKISPP_FEC_MESH_YFRA_BASE_SHD),
		 RKISPP_FEC_WR_Y_BASE_SHD, readl(base + RKISPP_FEC_WR_Y_BASE_SHD),
		 RKISPP_FEC_WR_UV_BASE_SHD, readl(base + RKISPP_FEC_WR_UV_BASE_SHD),
		 RKISPP_FEC_CORE_CTRL, readl(base + RKISPP_FEC_CORE_CTRL),
		 RKISPP_FEC_PIC_SIZE, readl(base + RKISPP_FEC_PIC_SIZE),
		 RKISPP_FEC_MESH_SIZE, readl(base + RKISPP_FEC_MESH_SIZE));
	if (!fec->hw->is_shutdown)
		writel(FEC_ST, base + RKISPP_CTRL_STRT);

	ret = wait_for_completion_timeout(&fec->cmpl, msecs_to_jiffies(300));
	if (!ret) {
		v4l2_err(&fec->v4l2_dev, "fec working timeout\n");
		ret = -EAGAIN;
	} else {
		ret = 0;
	}
	writel(SW_FEC2DDR_DIS, base + RKISPP_FEC_CORE_CTRL);

	g_ops->unmap_dmabuf(yfra_mem);
detach_yfra_dbuf:
	g_ops->detach_dmabuf(yfra_mem);
put_yfra_dbuf:
	dma_buf_put(yfra_dbuf);
unmap_yint_dbuf:
	g_ops->unmap_dmabuf(yint_mem);
detach_yint_dbuf:
	g_ops->detach_dmabuf(yint_mem);
put_yint_dbuf:
	dma_buf_put(yint_dbuf);
unmap_xfra_dbuf:
	g_ops->unmap_dmabuf(xfra_mem);
detach_xfra_dbuf:
	g_ops->detach_dmabuf(xfra_mem);
put_xfra_dbuf:
	dma_buf_put(xfra_dbuf);
unmap_xint_dbuf:
	g_ops->unmap_dmabuf(xint_mem);
detach_xint_dbuf:
	g_ops->detach_dmabuf(xint_mem);
put_xint_dbuf:
	dma_buf_put(xint_dbuf);
unmap_out_dbuf:
	g_ops->unmap_dmabuf(out_mem);
detach_out_dbuf:
	g_ops->detach_dmabuf(out_mem);
put_out_dbuf:
	dma_buf_put(out_dbuf);
unmap_in_dbuf:
	g_ops->unmap_dmabuf(in_mem);
detach_in_dbuf:
	g_ops->detach_dmabuf(in_mem);
put_in_dbuf:
	dma_buf_put(in_dbuf);
end:
	v4l2_dbg(3, rkispp_debug, &fec->v4l2_dev,
		 "%s exit ret:%d\n", __func__, ret);
	return ret;
}

static long fec_ioctl_default(struct file *file, void *fh,
			bool valid_prio, unsigned int cmd, void *arg)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);
	long ret = 0;

	if (!arg)
		return -EINVAL;

	switch (cmd) {
	case RKISPP_CMD_FEC_IN_OUT:
		ret = fec_running(fec, arg);
		break;
	default:
		ret = -EFAULT;
	}

	return ret;
}

static const struct v4l2_ioctl_ops m2m_ioctl_ops = {
	.vidioc_default = fec_ioctl_default,
};

static int fec_open(struct file *file)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);
	int ret;

	ret = v4l2_fh_open(file);
	if (ret)
		goto end;

	mutex_lock(&fec->hw->dev_lock);
	ret = pm_runtime_get_sync(fec->hw->dev);
	mutex_unlock(&fec->hw->dev_lock);
	if (ret < 0)
		v4l2_fh_release(file);
end:
	v4l2_dbg(1, rkispp_debug, &fec->v4l2_dev,
		 "%s ret:%d\n", __func__, ret);
	return (ret > 0) ? 0 : ret;
}

static int fec_release(struct file *file)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);

	v4l2_dbg(1, rkispp_debug, &fec->v4l2_dev, "%s\n", __func__);

	v4l2_fh_release(file);
	mutex_lock(&fec->hw->dev_lock);
	pm_runtime_put_sync(fec->hw->dev);
	mutex_unlock(&fec->hw->dev_lock);
	return 0;
}

static const struct v4l2_file_operations fec_fops = {
	.owner = THIS_MODULE,
	.open = fec_open,
	.release = fec_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct video_device fec_videodev = {
	.name = "rkispp_fec",
	.vfl_dir = VFL_DIR_M2M,
	.fops = &fec_fops,
	.ioctl_ops = &m2m_ioctl_ops,
	.minor = -1,
	.release = video_device_release_empty,
};

void rkispp_fec_irq(struct rkispp_hw_dev *hw)
{
	v4l2_dbg(3, rkispp_debug, &hw->fec_dev.v4l2_dev,
		 "%s\n", __func__);

	if (!completion_done(&hw->fec_dev.cmpl))
		complete(&hw->fec_dev.cmpl);
}

int rkispp_register_fec(struct rkispp_hw_dev *hw)
{
	struct rkispp_fec_dev *fec = &hw->fec_dev;
	struct v4l2_device *v4l2_dev;
	struct video_device *vfd;
	int ret;

	if (!IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_FEC))
		return 0;

	fec->hw = hw;
	hw->is_fec_ext = true;
	v4l2_dev = &fec->v4l2_dev;
	strlcpy(v4l2_dev->name, fec_videodev.name, sizeof(v4l2_dev->name));
	ret = v4l2_device_register(hw->dev, v4l2_dev);
	if (ret)
		return ret;

	fec->vfd = fec_videodev;
	vfd = &fec->vfd;
	vfd->lock = &fec->apilock;
	vfd->v4l2_dev = v4l2_dev;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(v4l2_dev, "Failed to register video device\n");
		goto unreg_v4l2;
	}
	video_set_drvdata(vfd, fec);
	return 0;
unreg_v4l2:
	v4l2_device_unregister(v4l2_dev);
	return ret;
}

void rkispp_unregister_fec(struct rkispp_hw_dev *hw)
{
	if (!IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_FEC))
		return;

	video_unregister_device(&hw->fec_dev.vfd);
	v4l2_device_unregister(&hw->fec_dev.v4l2_dev);
}
