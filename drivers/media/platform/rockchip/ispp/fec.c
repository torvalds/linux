// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <linux/pm_runtime.h>
#include <linux/rkispp-config.h>
#include <uapi/linux/rk-video-format.h>

#include "hw.h"
#include "ispp.h"
#include "regs.h"
#include "stream.h"
#include "common.h"

struct rkispp_fec_buf {
	struct list_head list;
	struct file *file;
	int fd;
	struct dma_buf *dbuf;
	void *mem;
};

static const struct vb2_mem_ops *g_ops = &vb2_dma_contig_memops;

static void *fec_buf_add(struct file *file, int fd, int size)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);
	struct rkispp_fec_buf *buf = NULL;
	struct dma_buf *dbuf;
	void *mem = NULL;
	bool is_add = true;

	dbuf = dma_buf_get(fd);
	v4l2_dbg(4, rkispp_debug, &fec->v4l2_dev,
		 "%s file:%p fd:%d dbuf:%p\n", __func__, file, fd, dbuf);
	if (IS_ERR_OR_NULL(dbuf)) {
		v4l2_err(&fec->v4l2_dev, "invalid dmabuf fd:%d for in picture", fd);
		return mem;
	}
	if (size && dbuf->size < size) {
		v4l2_err(&fec->v4l2_dev,
			 "input fd:%d size error:%zu < %u\n", fd, dbuf->size, size);
		dma_buf_put(dbuf);
		return mem;
	}

	mutex_lock(&fec->hw->dev_lock);
	list_for_each_entry(buf, &fec->list, list) {
		if (buf->file == file && buf->fd == fd && buf->dbuf == dbuf) {
			is_add = false;
			break;
		}
	}

	if (is_add) {
		mem = g_ops->attach_dmabuf(fec->hw->dev, dbuf, dbuf->size, DMA_BIDIRECTIONAL);
		if (IS_ERR(mem)) {
			v4l2_err(&fec->v4l2_dev, "failed to attach dmabuf, fd:%d\n", fd);
			dma_buf_put(dbuf);
			goto end;
		}
		if (g_ops->map_dmabuf(mem)) {
			v4l2_err(&fec->v4l2_dev, "failed to map, fd:%d\n", fd);
			g_ops->detach_dmabuf(mem);
			dma_buf_put(dbuf);
			mem = NULL;
			goto end;
		}
		buf = kzalloc(sizeof(struct rkispp_fec_buf), GFP_KERNEL);
		if (!buf) {
			g_ops->unmap_dmabuf(mem);
			g_ops->detach_dmabuf(mem);
			dma_buf_put(dbuf);
			mem = NULL;
			goto end;
		}
		buf->fd = fd;
		buf->file = file;
		buf->dbuf = dbuf;
		buf->mem = mem;
		list_add_tail(&buf->list, &fec->list);
	} else {
		dma_buf_put(dbuf);
		mem = buf->mem;
	}
end:
	mutex_unlock(&fec->hw->dev_lock);
	return mem;
}

static void fec_buf_del(struct file *file, int fd, bool is_all)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);
	struct rkispp_fec_buf *buf, *next;

	mutex_lock(&fec->hw->dev_lock);
	list_for_each_entry_safe(buf, next, &fec->list, list) {
		if (buf->file == file && (is_all || buf->fd == fd)) {
			v4l2_dbg(4, rkispp_debug, &fec->v4l2_dev,
				 "%s file:%p fd:%d dbuf:%p\n",
				 __func__, file, buf->fd, buf->dbuf);
			g_ops->unmap_dmabuf(buf->mem);
			g_ops->detach_dmabuf(buf->mem);
			dma_buf_put(buf->dbuf);
			buf->file = NULL;
			buf->mem = NULL;
			buf->dbuf = NULL;
			buf->fd = -1;
			list_del(&buf->list);
			kfree(buf);
		}
	}
	mutex_unlock(&fec->hw->dev_lock);
}

static int fec_running(struct file *file, struct rkispp_fec_in_out *buf)
{
	struct rkispp_fec_dev *fec = video_drvdata(file);
	u32 in_fmt, out_fmt, in_mult = 1, out_mult = 1;
	u32 in_size, in_offs, out_size, out_offs, val;
	u32 w = buf->width, h = buf->height, density, mesh_size;
	void __iomem *base = fec->hw->base_addr;
	void *mem;
	int ret = -EINVAL;
	ktime_t t = 0;
	s64 us = 0;

	if (rkispp_debug)
		t = ktime_get();
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
		return -EINVAL;
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
		return -EINVAL;
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
	mem = fec_buf_add(file, buf->in_pic_fd, in_size);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_RD_Y_BASE);
	val += in_offs;
	writel(val, base + RKISPP_FEC_RD_UV_BASE);

	/* output picture buf */
	mem = fec_buf_add(file, buf->out_pic_fd, out_size);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_WR_Y_BASE);
	val += out_offs;
	writel(val, base + RKISPP_FEC_WR_UV_BASE);

	/* mesh xint buf */
	mem = fec_buf_add(file, buf->mesh_xint_fd, mesh_size * 2);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_MESH_XINT_BASE);

	/* mesh xfra buf */
	mem = fec_buf_add(file, buf->mesh_xfra_fd, mesh_size);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_MESH_XFRA_BASE);

	/* mesh yint buf */
	mem = fec_buf_add(file, buf->mesh_yint_fd, mesh_size * 2);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_MESH_YINT_BASE);

	/* mesh yfra buf */
	mem = fec_buf_add(file, buf->mesh_yfra_fd, mesh_size);
	if (!mem)
		goto free_buf;
	val = *((dma_addr_t *)g_ops->cookie(mem));
	writel(val, base + RKISPP_FEC_MESH_YFRA_BASE);

	val = out_fmt << 4 | in_fmt;
	writel(val, base + RKISPP_FEC_CTRL);
	val = ALIGN(buf->width * in_mult, 16) >> 2;
	writel(val, base + RKISPP_FEC_RD_VIR_STRIDE);
	val = ALIGN(buf->width * out_mult, 16) >> 2;
	writel(val, base + RKISPP_FEC_WR_VIR_STRIDE);
	val = buf->height << 16 | buf->width;
	writel(val, base + RKISPP_FEC_DST_SIZE);
	writel(val, base + RKISPP_FEC_SRC_SIZE);
	writel(mesh_size, base + RKISPP_FEC_MESH_SIZE);
	val = SW_FEC_EN | density;
	writel(val, base + RKISPP_FEC_CORE_CTRL);

	writel(FEC_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
	v4l2_dbg(3, rkispp_debug, &fec->v4l2_dev,
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n"
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n"
		 "0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x 0x%x:0x%x\n",
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
		 RKISPP_FEC_DST_SIZE, readl(base + RKISPP_FEC_DST_SIZE),
		 RKISPP_FEC_SRC_SIZE, readl(base + RKISPP_FEC_SRC_SIZE),
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

	if (rkispp_debug)
		us = ktime_us_delta(ktime_get(), t);
	v4l2_dbg(3, rkispp_debug, &fec->v4l2_dev,
		 "%s exit ret:%d, time:%lldus\n", __func__, ret, us);
	return ret;
free_buf:
	fec_buf_del(file, 0, true);
	return ret;
}

static long fec_ioctl_default(struct file *file, void *fh,
			bool valid_prio, unsigned int cmd, void *arg)
{
	long ret = 0;

	if (!arg)
		return -EINVAL;

	switch (cmd) {
	case RKISPP_CMD_FEC_IN_OUT:
		ret = fec_running(file, arg);
		break;
	case RKISPP_CMD_FEC_BUF_ADD:
		if (!fec_buf_add(file, *(int *)arg, 0))
			ret = -ENOMEM;
		break;
	case RKISPP_CMD_FEC_BUF_DEL:
		fec_buf_del(file, *(int *)arg, false);
		break;
	default:
		ret = -EFAULT;
	}

	return ret;
}

static const struct v4l2_ioctl_ops fec_ioctl_ops = {
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
	fec_buf_del(file, 0, true);
	mutex_lock(&fec->hw->dev_lock);
	pm_runtime_put_sync(fec->hw->dev);
	mutex_unlock(&fec->hw->dev_lock);
	return 0;
}

static const struct v4l2_file_operations fec_fops = {
	.owner = THIS_MODULE,
	.open = fec_open,
	.release = fec_release,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
};

static const struct video_device fec_videodev = {
	.name = "rkispp_fec",
	.vfl_dir = VFL_DIR_RX,
	.fops = &fec_fops,
	.ioctl_ops = &fec_ioctl_ops,
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

	mutex_init(&fec->apilock);
	fec->vfd = fec_videodev;
	vfd = &fec->vfd;
	vfd->device_caps = V4L2_CAP_STREAMING;
	vfd->lock = &fec->apilock;
	vfd->v4l2_dev = v4l2_dev;
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(v4l2_dev, "Failed to register video device\n");
		goto unreg_v4l2;
	}
	video_set_drvdata(vfd, fec);
	INIT_LIST_HEAD(&fec->list);
	return 0;
unreg_v4l2:
	mutex_destroy(&fec->apilock);
	v4l2_device_unregister(v4l2_dev);
	return ret;
}

void rkispp_unregister_fec(struct rkispp_hw_dev *hw)
{
	if (!IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_FEC))
		return;

	mutex_destroy(&hw->fec_dev.apilock);
	video_unregister_device(&hw->fec_dev.vfd);
	v4l2_device_unregister(&hw->fec_dev.v4l2_dev);
}
