/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*
 * Based on STMP378X PxP driver
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>
#include <linux/dmaengine.h>
#include <linux/pxp_dma.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/mxcfb.h>

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "mxc_pxp_v4l2.h"

#define PXP_DRIVER_NAME			"pxp-v4l2"
#define PXP_DRIVER_MAJOR		2
#define PXP_DRIVER_MINOR		0

#define PXP_DEF_BUFS			2
#define PXP_MIN_PIX			8

#define V4L2_OUTPUT_TYPE_INTERNAL	4

static int video_nr = -1;	/* -1 ==> auto assign */
static struct pxp_data_format pxp_s0_formats[] = {
	{
		.name = "24-bit RGB",
		.bpp = 4,
		.fourcc = V4L2_PIX_FMT_RGB24,
		.colorspace = V4L2_COLORSPACE_SRGB,
	}, {
		.name = "16-bit RGB 5:6:5",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.colorspace = V4L2_COLORSPACE_SRGB,
	}, {
		.name = "16-bit RGB 5:5:5",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_RGB555,
		.colorspace = V4L2_COLORSPACE_SRGB,
	}, {
		.name = "YUV 4:2:0 Planar",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_YUV420,
		.colorspace = V4L2_COLORSPACE_JPEG,
	}, {
		.name = "YUV 4:2:2 Planar",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.colorspace = V4L2_COLORSPACE_JPEG,
	}, {
		.name = "UYVY",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_UYVY,
		.colorspace = V4L2_COLORSPACE_JPEG,
	}, {
		.name = "YUYV",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_YUYV,
		.colorspace = V4L2_COLORSPACE_JPEG,
	}, {
		.name = "YUV32",
		.bpp = 4,
		.fourcc = V4L2_PIX_FMT_YUV32,
		.colorspace = V4L2_COLORSPACE_JPEG,
	},
};

static unsigned int v4l2_fmt_to_pxp_fmt(u32 v4l2_pix_fmt)
{
	u32 pxp_fmt = 0;

	if (v4l2_pix_fmt == V4L2_PIX_FMT_RGB24)
		pxp_fmt = PXP_PIX_FMT_RGB24;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_RGB565)
		pxp_fmt = PXP_PIX_FMT_RGB565;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_RGB555)
		pxp_fmt = PXP_PIX_FMT_RGB555;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_YUV420)
		pxp_fmt = PXP_PIX_FMT_YUV420P;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_YUV422P)
		pxp_fmt = PXP_PIX_FMT_YUV422P;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_UYVY)
		pxp_fmt = PXP_PIX_FMT_UYVY;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_YUV32)
		pxp_fmt = PXP_PIX_FMT_VUY444;
	else if (v4l2_pix_fmt == V4L2_PIX_FMT_YUYV)
		pxp_fmt = PXP_PIX_FMT_YUYV;

	return pxp_fmt;
}
struct v4l2_queryctrl pxp_controls[] = {
	{
		.id 		= V4L2_CID_HFLIP,
		.type 		= V4L2_CTRL_TYPE_BOOLEAN,
		.name 		= "Horizontal Flip",
		.minimum 	= 0,
		.maximum 	= 1,
		.step 		= 1,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Vertical Flip",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Rotation",
		.minimum	= 0,
		.maximum	= 270,
		.step		= 90,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE + 1,
		.name		= "Background Color",
		.minimum	= 0,
		.maximum	= 0xFFFFFF,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE + 2,
		.name		= "Set S0 Chromakey",
		.minimum	= -1,
		.maximum	= 0xFFFFFF,
		.step		= 1,
		.default_value	= -1,
		.flags		= 0,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE + 3,
		.name		= "YUV Colorspace",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	},
};

static void free_dma_buf(struct pxps *pxp, struct dma_mem *buf)
{
	dma_free_coherent(&pxp->pdev->dev, buf->size, buf->vaddr, buf->paddr);
	dev_dbg(&pxp->pdev->dev,
			"free dma size:0x%x, paddr:0x%x\n",
			buf->size, buf->paddr);
	memset(buf, 0, sizeof(*buf));
}

static int alloc_dma_buf(struct pxps *pxp, struct dma_mem *buf)
{

	buf->vaddr = dma_alloc_coherent(&pxp->pdev->dev, buf->size, &buf->paddr,
						GFP_DMA | GFP_KERNEL);
	if (!buf->vaddr) {
		dev_err(&pxp->pdev->dev,
			"cannot get dma buf size:0x%x\n", buf->size);
		return -ENOMEM;
	}
	dev_dbg(&pxp->pdev->dev,
		"alloc dma buf size:0x%x, paddr:0x%x\n", buf->size, buf->paddr);
	return 0;
}

/* callback function */
static void video_dma_done(void *arg)
{
	struct pxp_tx_desc *tx_desc = to_tx_desc(arg);
	struct dma_chan *chan = tx_desc->txd.chan;
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	struct pxps *pxp = pxp_chan->client;
	struct videobuf_buffer *vb;

	dev_dbg(chan->device->dev, "callback cookie %d, active DMA 0x%08x\n",
			tx_desc->txd.cookie,
			pxp->active ? sg_dma_address(&pxp->active->sg[0]) : 0);

	spin_lock(&pxp->lock);
	if (pxp->active) {
		vb = &pxp->active->vb;

		list_del_init(&vb->queue);
		vb->state = VIDEOBUF_DONE;
		do_gettimeofday(&vb->ts);
		vb->field_count++;
		wake_up(&vb->done);
	}

	if (list_empty(&pxp->outq)) {
		pxp->active = NULL;
		spin_unlock(&pxp->lock);

		return;
	}

	pxp->active = list_entry(pxp->outq.next,
				     struct pxp_buffer, vb.queue);
	pxp->active->vb.state = VIDEOBUF_ACTIVE;
	spin_unlock(&pxp->lock);
}

static int acquire_dma_channel(struct pxps *pxp)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct pxp_channel **pchan = &pxp->pxp_channel[0];

	if (*pchan) {
		struct videobuf_buffer *vb, *_vb;
		dma_release_channel(&(*pchan)->dma_chan);
		*pchan = NULL;
		pxp->active = NULL;
		list_for_each_entry_safe(vb, _vb, &pxp->outq, queue) {
			list_del_init(&vb->queue);
			vb->state = VIDEOBUF_ERROR;
			wake_up(&vb->done);
		}
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan)
		return -EBUSY;

	*pchan = to_pxp_channel(chan);
	(*pchan)->client = pxp;

	return 0;
}

static int _get_fbinfo(struct fb_info **fbi)
{
	int i;
	for (i = 0; i < num_registered_fb; i++) {
		char *idstr = registered_fb[i]->fix.id;
		if (strncmp(idstr, "mxs", 3) == 0) {
			*fbi = registered_fb[i];
			return 0;
		}
	}

	return -ENODEV;
}

static int pxp_set_fbinfo(struct pxps *pxp)
{
	struct v4l2_framebuffer *fb = &pxp->fb;
	int err;

	err = _get_fbinfo(&pxp->fbi);
	if (err)
		return err;

	fb->fmt.width = pxp->fbi->var.xres;
	fb->fmt.height = pxp->fbi->var.yres;
	pxp->pxp_conf.out_param.stride = pxp->fbi->var.xres;
	if (pxp->fbi->var.bits_per_pixel == 16)
		fb->fmt.pixelformat = V4L2_PIX_FMT_RGB565;
	else
		fb->fmt.pixelformat = V4L2_PIX_FMT_RGB24;

	fb->base = (void *)pxp->fbi->fix.smem_start;

	return 0;
}

static int _get_cur_fb_blank(struct pxps *pxp)
{
	struct fb_info *fbi;
	mm_segment_t old_fs;
	int err = 0;

	err = _get_fbinfo(&fbi);
	if (err)
		return err;

	if (fbi->fbops->fb_ioctl) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = fbi->fbops->fb_ioctl(fbi, MXCFB_GET_FB_BLANK,
				(unsigned int)(&pxp->fb_blank));
		set_fs(old_fs);
	}

	return err;
}

static int pxp_show_buf(struct pxps *pxp, unsigned long paddr)
{
	struct fb_info *fbi = pxp->fbi;
	int ret = -EINVAL;

	if (paddr == 0) {
		dev_err(&pxp->pdev->dev, "Invalid paddr\n");
		return ret;
	}

	console_lock();
	fbi->fix.smem_start = paddr;
	ret = fb_pan_display(fbi, &fbi->var);
	console_unlock();

	return ret;
}

static int set_fb_blank(int blank)
{
	struct fb_info *fbi;
	int err = 0;

	err = _get_fbinfo(&fbi);
	if (err)
		return err;

	console_lock();
	fb_blank(fbi, blank);
	console_unlock();

	return err;
}

static int pxp_set_cstate(struct pxps *pxp, struct v4l2_control *vc)
{

	if (vc->id == V4L2_CID_HFLIP) {
		pxp->pxp_conf.proc_data.hflip = vc->value;
	} else if (vc->id == V4L2_CID_VFLIP) {
		pxp->pxp_conf.proc_data.vflip = vc->value;
	} else if (vc->id == V4L2_CID_PRIVATE_BASE) {
		if (vc->value % 90)
			return -ERANGE;
		pxp->pxp_conf.proc_data.rotate = vc->value;
	} else if (vc->id == V4L2_CID_PRIVATE_BASE + 1) {
		pxp->pxp_conf.proc_data.bgcolor = vc->value;
	} else if (vc->id == V4L2_CID_PRIVATE_BASE + 2) {
		pxp->pxp_conf.s0_param.color_key = vc->value;
	} else if (vc->id == V4L2_CID_PRIVATE_BASE + 3) {
		pxp->pxp_conf.proc_data.yuv = vc->value;
	}

	return 0;
}

static int pxp_get_cstate(struct pxps *pxp, struct v4l2_control *vc)
{
	if (vc->id == V4L2_CID_HFLIP)
		vc->value = pxp->pxp_conf.proc_data.hflip;
	else if (vc->id == V4L2_CID_VFLIP)
		vc->value = pxp->pxp_conf.proc_data.vflip;
	else if (vc->id == V4L2_CID_PRIVATE_BASE)
		vc->value = pxp->pxp_conf.proc_data.rotate;
	else if (vc->id == V4L2_CID_PRIVATE_BASE + 1)
		vc->value = pxp->pxp_conf.proc_data.bgcolor;
	else if (vc->id == V4L2_CID_PRIVATE_BASE + 2)
		vc->value = pxp->pxp_conf.s0_param.color_key;
	else if (vc->id == V4L2_CID_PRIVATE_BASE + 3)
		vc->value = pxp->pxp_conf.proc_data.yuv;

	return 0;
}

static int pxp_enumoutput(struct file *file, void *fh,
			struct v4l2_output *o)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	if (o->index > 1)
		return -EINVAL;

	memset(o, 0, sizeof(struct v4l2_output));
	if (o->index == 0) {
		strcpy(o->name, "PxP Display Output");
		pxp->output = 0;
	} else {
		strcpy(o->name, "PxP Virtual Output");
		pxp->output = 1;
	}
	o->type = V4L2_OUTPUT_TYPE_INTERNAL;
	o->std = 0;
	o->reserved[0] = pxp->outbuf.paddr;

	return 0;
}

static int pxp_g_output(struct file *file, void *fh,
			unsigned int *i)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	*i = pxp->output;

	return 0;
}

static int pxp_s_output(struct file *file, void *fh,
			unsigned int i)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_pix_format *fmt = (struct v4l2_pix_format*)(&pxp->fb.fmt);
	u32 size;
	int ret, bpp;

	if (i > 1)
		return -EINVAL;

	/* Output buffer is same format as fbdev */
	if (fmt->pixelformat == V4L2_PIX_FMT_RGB24  ||
		fmt->pixelformat == V4L2_PIX_FMT_YUV32)
		bpp = 4;
	else
		bpp = 2;

	size = fmt->width * fmt->height * bpp;
	if (size > pxp->outbuf.size) {
		if (pxp->outbuf.vaddr)
			free_dma_buf(pxp, &pxp->outbuf);
		pxp->outbuf.size = size;
		ret = alloc_dma_buf(pxp, &pxp->outbuf);
		if (ret < 0)
			return ret;
	}
	memset(pxp->outbuf.vaddr, 0x0, pxp->outbuf.size);

	pxp->pxp_conf.out_param.width = fmt->width;
	pxp->pxp_conf.out_param.height = fmt->height;
	if (fmt->pixelformat == V4L2_PIX_FMT_RGB24)
		pxp->pxp_conf.out_param.pixel_fmt = PXP_PIX_FMT_RGB24;
	else
		pxp->pxp_conf.out_param.pixel_fmt = PXP_PIX_FMT_RGB565;

	return 0;
}

static int pxp_enum_fmt_video_output(struct file *file, void *fh,
				struct v4l2_fmtdesc *fmt)
{
	enum v4l2_buf_type type = fmt->type;
	unsigned int index = fmt->index;

	if (fmt->index >= ARRAY_SIZE(pxp_s0_formats))
		return -EINVAL;

	memset(fmt, 0, sizeof(struct v4l2_fmtdesc));
	fmt->index = index;
	fmt->type = type;
	fmt->pixelformat = pxp_s0_formats[index].fourcc;
	strcpy(fmt->description, pxp_s0_formats[index].name);

	return 0;
}

static int pxp_g_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct v4l2_pix_format *pf = &f->fmt.pix;
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct pxp_data_format *fmt = pxp->s0_fmt;

	pf->width = pxp->pxp_conf.s0_param.width;
	pf->height = pxp->pxp_conf.s0_param.height;
	pf->pixelformat = fmt->fourcc;
	pf->field = V4L2_FIELD_NONE;
	pf->bytesperline = fmt->bpp * pf->width;
	pf->sizeimage = pf->bytesperline * pf->height;
	pf->colorspace = fmt->colorspace;
	pf->priv = 0;

	return 0;
}

static struct pxp_data_format *pxp_get_format(struct v4l2_format *f)
{
	struct pxp_data_format *fmt;
	int i;

	for (i = 0; i < ARRAY_SIZE(pxp_s0_formats); i++) {
		fmt = &pxp_s0_formats[i];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(pxp_s0_formats))
		return NULL;

	return &pxp_s0_formats[i];
}

static int pxp_try_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	int w = f->fmt.pix.width;
	int h = f->fmt.pix.height;
	struct pxp_data_format *fmt = pxp_get_format(f);

	if (!fmt)
		return -EINVAL;

	w = min(w, 2040);
	w = max(w, 8);
	h = min(h, 2040);
	h = max(h, 8);
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = w;
	f->fmt.pix.height = h;
	f->fmt.pix.pixelformat = fmt->fourcc;

	return 0;
}

static int pxp_s_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_pix_format *pf = &f->fmt.pix;
	int ret;

	ret = acquire_dma_channel(pxp);
	if (ret < 0)
		return ret;

	ret = pxp_try_fmt_video_output(file, fh, f);
	if (ret == 0) {
		pxp->s0_fmt = pxp_get_format(f);
		pxp->pxp_conf.s0_param.pixel_fmt =
			v4l2_fmt_to_pxp_fmt(pxp->s0_fmt->fourcc);
		pxp->pxp_conf.s0_param.width = pf->width;
		pxp->pxp_conf.s0_param.height = pf->height;
	}


	return ret;
}

static int pxp_g_fmt_output_overlay(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;

	memset(wf, 0, sizeof(struct v4l2_window));
	wf->chromakey = pxp->s1_chromakey;
	wf->global_alpha = pxp->global_alpha;
	wf->field = V4L2_FIELD_NONE;
	wf->clips = NULL;
	wf->clipcount = 0;
	wf->bitmap = NULL;
	wf->w.left = pxp->pxp_conf.proc_data.srect.left;
	wf->w.top = pxp->pxp_conf.proc_data.srect.top;
	wf->w.width = pxp->pxp_conf.proc_data.srect.width;
	wf->w.height = pxp->pxp_conf.proc_data.srect.height;

	return 0;
}

static int pxp_try_fmt_output_overlay(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;
	struct v4l2_rect srect;
	u32 s1_chromakey = wf->chromakey;
	u8 global_alpha = wf->global_alpha;

	memcpy(&srect, &(wf->w), sizeof(struct v4l2_rect));

	pxp_g_fmt_output_overlay(file, fh, f);

	wf->chromakey = s1_chromakey;
	wf->global_alpha = global_alpha;

	/* Constrain parameters to the input buffer */
	wf->w.left = srect.left;
	wf->w.top = srect.top;
	wf->w.width = min(srect.width,
			((__u32)pxp->pxp_conf.s0_param.width - wf->w.left));
	wf->w.height = min(srect.height,
			((__u32)pxp->pxp_conf.s0_param.height - wf->w.top));

	return 0;
}

static int pxp_s_fmt_output_overlay(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;
	int ret = pxp_try_fmt_output_overlay(file, fh, f);

	if (ret == 0) {
		pxp->global_alpha = wf->global_alpha;
		pxp->s1_chromakey = wf->chromakey;
		pxp->pxp_conf.proc_data.srect.left = wf->w.left;
		pxp->pxp_conf.proc_data.srect.top = wf->w.top;
		pxp->pxp_conf.proc_data.srect.width = wf->w.width;
		pxp->pxp_conf.proc_data.srect.height = wf->w.height;
		pxp->pxp_conf.ol_param[0].global_alpha = pxp->global_alpha;
		pxp->pxp_conf.ol_param[0].color_key = pxp->s1_chromakey;
		pxp->pxp_conf.ol_param[0].color_key_enable =
					pxp->s1_chromakey_state;
	}

	return ret;
}

static int pxp_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *r)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_reqbufs(&pxp->s0_vbq, r);
}

static int pxp_querybuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	int ret;
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	ret = videobuf_querybuf(&pxp->s0_vbq, b);
	if (!ret) {
		struct videobuf_buffer *vb = pxp->s0_vbq.bufs[b->index];
		if (b->flags & V4L2_BUF_FLAG_MAPPED)
			b->m.offset = videobuf_to_dma_contig(vb);
	}

	return ret;
}

static int pxp_qbuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_qbuf(&pxp->s0_vbq, b);
}

static int pxp_dqbuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_dqbuf(&pxp->s0_vbq, b, file->f_flags & O_NONBLOCK);
}

static int pxp_streamon(struct file *file, void *priv,
			enum v4l2_buf_type t)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	if (t != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	_get_cur_fb_blank(pxp);
	set_fb_blank(FB_BLANK_UNBLANK);

	ret = videobuf_streamon(&pxp->s0_vbq);

	if (!ret && (pxp->output == 0))
		pxp_show_buf(pxp, pxp->outbuf.paddr);

	return ret;
}

static int pxp_streamoff(struct file *file, void *priv,
			enum v4l2_buf_type t)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	if ((t != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	ret = videobuf_streamoff(&pxp->s0_vbq);

	pxp_show_buf(pxp, (unsigned long)pxp->fb.base);

	if (pxp->fb_blank)
		set_fb_blank(FB_BLANK_POWERDOWN);

	return ret;
}

static int pxp_buf_setup(struct videobuf_queue *q,
			unsigned int *count, unsigned *size)
{
	struct pxps *pxp = q->priv_data;

	*size = pxp->pxp_conf.s0_param.width *
		pxp->pxp_conf.s0_param.height * pxp->s0_fmt->bpp;

	if (0 == *count)
		*count = PXP_DEF_BUFS;

	return 0;
}

static void pxp_buf_free(struct videobuf_queue *q, struct pxp_buffer *buf)
{
	struct videobuf_buffer *vb = &buf->vb;

	BUG_ON(in_interrupt());

	pr_debug("%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	/*
	 * This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE
	 */
	videobuf_waiton(q, vb, 0, 0);

	videobuf_dma_contig_free(q, vb);
	buf->txd = NULL;

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int pxp_buf_prepare(struct videobuf_queue *q,
			struct videobuf_buffer *vb,
			enum v4l2_field field)
{
	struct pxps *pxp = q->priv_data;
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	struct pxp_buffer *buf = container_of(vb, struct pxp_buffer, vb);
	struct pxp_tx_desc *desc;
	int ret = 0;
	int i, length;

	if (!pxp->outbuf.paddr)  {
		dev_err(&pxp->pdev->dev, "Not allocate memory for "
			"PxP Out buffer?\n");
		return -ENOMEM;
	}

	vb->width = pxp->pxp_conf.s0_param.width;
	vb->height = pxp->pxp_conf.s0_param.height;
	vb->size = vb->width * vb->height * pxp->s0_fmt->bpp;
	vb->field = V4L2_FIELD_NONE;
	if (vb->state != VIDEOBUF_NEEDS_INIT)
		pxp_buf_free(q, buf);

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		struct pxp_channel *pchan = pxp->pxp_channel[0];
		struct scatterlist *sg = &buf->sg[0];

		/* This actually (allocates and) maps buffers */
		ret = videobuf_iolock(q, vb, NULL);
		if (ret) {
			pr_err("fail to call videobuf_iolock, ret = %d\n", ret);
			goto fail;
		}

		/*
		 * sg[0] for input(S0)
		 * Sg[1] for output
		 */
		sg_init_table(sg, 3);

		buf->txd = pchan->dma_chan.device->device_prep_slave_sg(
			&pchan->dma_chan, sg, 3, DMA_FROM_DEVICE,
			DMA_PREP_INTERRUPT, NULL);
		if (!buf->txd) {
			ret = -EIO;
			goto fail;
		}

		buf->txd->callback_param	= buf->txd;
		buf->txd->callback		= video_dma_done;

		desc = to_tx_desc(buf->txd);
		length = desc->len;
		for (i = 0; i < length; i++) {
			if (i == 0) {/* S0 */
				memcpy(&desc->proc_data, proc_data,
					sizeof(struct pxp_proc_data));
				pxp_conf->s0_param.paddr =
						videobuf_to_dma_contig(vb);
				memcpy(&desc->layer_param.s0_param,
					&pxp_conf->s0_param,
					sizeof(struct pxp_layer_param));
			} else if (i == 1) { /* Output */
				/* we should always pass the output
				 * width and height which is the value
				 * after been rotated.
				 */
				pxp_conf->out_param.width =
					pxp->fb.fmt.width;
				pxp_conf->out_param.height =
					pxp->fb.fmt.height;

				pxp_conf->out_param.paddr = pxp->outbuf.paddr;
				memcpy(&desc->layer_param.out_param,
					&pxp_conf->out_param,
					sizeof(struct pxp_layer_param));
			} else if (pxp_conf->ol_param[0].combine_enable) {
				/* Overlay */
				pxp_conf->ol_param[0].paddr =
						(dma_addr_t)pxp->fb.base;
				pxp_conf->ol_param[0].width = pxp->fb.fmt.width;
				pxp_conf->ol_param[0].height =
						pxp->fb.fmt.height;
				pxp_conf->ol_param[0].pixel_fmt =
						pxp_conf->out_param.pixel_fmt;
				memcpy(&desc->layer_param.ol_param,
				       &pxp_conf->ol_param[0],
				       sizeof(struct pxp_layer_param));
			}

			desc = desc->next;
		}

		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;

fail:
	pxp_buf_free(q, buf);
	return ret;
}


static void pxp_buf_queue(struct videobuf_queue *q,
			struct videobuf_buffer *vb)
{
	struct pxps *pxp = q->priv_data;
	struct pxp_buffer *buf = container_of(vb, struct pxp_buffer, vb);
	struct dma_async_tx_descriptor *txd = buf->txd;
	struct pxp_channel *pchan = pxp->pxp_channel[0];
	dma_cookie_t cookie;

	BUG_ON(!irqs_disabled());

	list_add_tail(&vb->queue, &pxp->outq);

	if (!pxp->active) {
		pxp->active = buf;
		vb->state = VIDEOBUF_ACTIVE;
	} else {
		vb->state = VIDEOBUF_QUEUED;
	}

	spin_unlock_irq(&pxp->lock);

	cookie = txd->tx_submit(txd);
	dev_dbg(&pxp->pdev->dev, "Submitted cookie %d DMA 0x%08x\n",
				cookie, sg_dma_address(&buf->sg[0]));
	mdelay(5);
	/* trigger ePxP */
	dma_async_issue_pending(&pchan->dma_chan);

	spin_lock_irq(&pxp->lock);

	if (cookie >= 0)
		return;

	/* Submit error */
	pr_err("%s: Submit error\n", __func__);
	vb->state = VIDEOBUF_PREPARED;

	list_del_init(&vb->queue);

	if (pxp->active == buf)
		pxp->active = NULL;
}

static void pxp_buf_release(struct videobuf_queue *q,
			struct videobuf_buffer *vb)
{
	struct pxps *pxp = q->priv_data;
	struct pxp_buffer *buf = container_of(vb, struct pxp_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&pxp->lock, flags);
	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED) &&
	    !list_empty(&vb->queue)) {
		vb->state = VIDEOBUF_ERROR;

		list_del_init(&vb->queue);
		if (pxp->active == buf)
			pxp->active = NULL;
	}
	spin_unlock_irqrestore(&pxp->lock, flags);

	pxp_buf_free(q, buf);
}

static struct videobuf_queue_ops pxp_vbq_ops = {
	.buf_setup	= pxp_buf_setup,
	.buf_prepare	= pxp_buf_prepare,
	.buf_queue	= pxp_buf_queue,
	.buf_release	= pxp_buf_release,
};

static int pxp_querycap(struct file *file, void *fh,
			struct v4l2_capability *cap)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	memset(cap, 0, sizeof(*cap));
	strcpy(cap->driver, "pxp");
	strcpy(cap->card, "pxp");
	strlcpy(cap->bus_info, dev_name(&pxp->pdev->dev),
		sizeof(cap->bus_info));

	cap->version = (PXP_DRIVER_MAJOR << 8) + PXP_DRIVER_MINOR;

	cap->device_caps = V4L2_CAP_STREAMING |	V4L2_CAP_VIDEO_OUTPUT |
				V4L2_CAP_VIDEO_OUTPUT_OVERLAY;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int pxp_g_fbuf(struct file *file, void *priv,
			struct v4l2_framebuffer *fb)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	memset(fb, 0, sizeof(*fb));

	fb->capability = V4L2_FBUF_CAP_EXTERNOVERLAY |
			 V4L2_FBUF_CAP_CHROMAKEY |
			 V4L2_FBUF_CAP_LOCAL_ALPHA |
			 V4L2_FBUF_CAP_GLOBAL_ALPHA;

	if (pxp->global_alpha_state)
		fb->flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;
	if (pxp->s1_chromakey_state)
		fb->flags |= V4L2_FBUF_FLAG_CHROMAKEY;

	return 0;
}

static int pxp_s_fbuf(struct file *file, void *priv,
			const struct v4l2_framebuffer *fb)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	pxp->overlay_state =
		(fb->flags & V4L2_FBUF_FLAG_OVERLAY) != 0;
	pxp->global_alpha_state =
		(fb->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) != 0;
	pxp->s1_chromakey_state =
		(fb->flags & V4L2_FBUF_FLAG_CHROMAKEY) != 0;

	pxp->pxp_conf.ol_param[0].combine_enable = pxp->overlay_state;
	pxp->pxp_conf.ol_param[0].global_alpha_enable = pxp->global_alpha_state;

	return 0;
}

static int pxp_g_crop(struct file *file, void *fh,
			struct v4l2_crop *c)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	if (c->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		return -EINVAL;

	c->c.left = pxp->pxp_conf.proc_data.drect.left;
	c->c.top = pxp->pxp_conf.proc_data.drect.top;
	c->c.width = pxp->pxp_conf.proc_data.drect.width;
	c->c.height = pxp->pxp_conf.proc_data.drect.height;

	return 0;
}

static int pxp_s_crop(struct file *file, void *fh,
			const struct v4l2_crop *c)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int l = c->c.left;
	int t = c->c.top;
	int w = c->c.width;
	int h = c->c.height;
	int fbw = pxp->fb.fmt.width;
	int fbh = pxp->fb.fmt.height;

	if (c->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		return -EINVAL;

	/* Constrain parameters to FB limits */
	w = min(w, fbw);
	w = max(w, PXP_MIN_PIX);
	h = min(h, fbh);
	h = max(h, PXP_MIN_PIX);

	/* Round up values to PxP pixel block */
	l = roundup(l, PXP_MIN_PIX);
	t = roundup(t, PXP_MIN_PIX);
	w = roundup(w, PXP_MIN_PIX);
	h = roundup(h, PXP_MIN_PIX);

	if ((l + w) > fbw)
		l = 0;
	if ((t + h) > fbh)
		t = 0;

	pxp->pxp_conf.proc_data.drect.left = l;
	pxp->pxp_conf.proc_data.drect.top = t;
	pxp->pxp_conf.proc_data.drect.width = w;
	pxp->pxp_conf.proc_data.drect.height = h;

	memset(pxp->outbuf.vaddr, 0x0, pxp->outbuf.size);

	return 0;
}

static int pxp_queryctrl(struct file *file, void *priv,
			 struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (qc->id && qc->id == pxp_controls[i].id) {
			memcpy(qc, &(pxp_controls[i]), sizeof(*qc));
			return 0;
		}

	return -EINVAL;
}

static int pxp_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *vc)
{
	int i;

	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (vc->id == pxp_controls[i].id)
			return pxp_get_cstate(pxp, vc);

	return -EINVAL;
}

static int pxp_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *vc)
{
	int i;
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (vc->id == pxp_controls[i].id) {
			if (vc->value < pxp_controls[i].minimum ||
			    vc->value > pxp_controls[i].maximum)
				return -ERANGE;
			return pxp_set_cstate(pxp, vc);
		}

	memset(pxp->outbuf.vaddr, 0x0, pxp->outbuf.size);

	return -EINVAL;
}

void pxp_release(struct video_device *vfd)
{
	struct pxps *pxp = video_get_drvdata(vfd);

	spin_lock(&pxp->lock);
	video_device_release(vfd);
	spin_unlock(&pxp->lock);
}

static int pxp_open(struct file *file)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	mutex_lock(&pxp->mutex);
	pxp->users++;

	if (pxp->users > 1) {
		pxp->users--;
		ret = -EBUSY;
		goto out;
	}
out:
	mutex_unlock(&pxp->mutex);
	if (ret)
		return ret;

	ret = pxp_set_fbinfo(pxp);
	if (ret) {
		dev_err(&pxp->pdev->dev, "failed to call pxp_set_fbinfo\n");
		return ret;
	}

	videobuf_queue_dma_contig_init(&pxp->s0_vbq,
				&pxp_vbq_ops,
				&pxp->pdev->dev,
				&pxp->lock,
				V4L2_BUF_TYPE_VIDEO_OUTPUT,
				V4L2_FIELD_NONE,
				sizeof(struct pxp_buffer),
				pxp,
				NULL);
	dev_dbg(&pxp->pdev->dev, "call pxp_open\n");

	return 0;
}

static int pxp_close(struct file *file)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	pxp_streamoff(file, NULL, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	videobuf_stop(&pxp->s0_vbq);
	videobuf_mmap_free(&pxp->s0_vbq);
	pxp->active = NULL;

	mutex_lock(&pxp->mutex);
	pxp->users--;
	mutex_unlock(&pxp->mutex);

	return 0;
}

static int pxp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret;

	ret = videobuf_mmap_mapper(&pxp->s0_vbq, vma);

	return ret;
}

static const struct v4l2_file_operations pxp_fops = {
	.owner		= THIS_MODULE,
	.open		= pxp_open,
	.release	= pxp_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= pxp_mmap,
};

static const struct v4l2_ioctl_ops pxp_ioctl_ops = {
	.vidioc_querycap		= pxp_querycap,

	.vidioc_reqbufs			= pxp_reqbufs,
	.vidioc_querybuf		= pxp_querybuf,
	.vidioc_qbuf			= pxp_qbuf,
	.vidioc_dqbuf			= pxp_dqbuf,

	.vidioc_streamon		= pxp_streamon,
	.vidioc_streamoff		= pxp_streamoff,

	.vidioc_enum_output		= pxp_enumoutput,
	.vidioc_g_output		= pxp_g_output,
	.vidioc_s_output		= pxp_s_output,

	.vidioc_enum_fmt_vid_out	= pxp_enum_fmt_video_output,
	.vidioc_try_fmt_vid_out		= pxp_try_fmt_video_output,
	.vidioc_g_fmt_vid_out		= pxp_g_fmt_video_output,
	.vidioc_s_fmt_vid_out		= pxp_s_fmt_video_output,

	.vidioc_try_fmt_vid_out_overlay	= pxp_try_fmt_output_overlay,
	.vidioc_g_fmt_vid_out_overlay	= pxp_g_fmt_output_overlay,
	.vidioc_s_fmt_vid_out_overlay	= pxp_s_fmt_output_overlay,

	.vidioc_g_fbuf			= pxp_g_fbuf,
	.vidioc_s_fbuf			= pxp_s_fbuf,

	.vidioc_g_crop			= pxp_g_crop,
	.vidioc_s_crop			= pxp_s_crop,

	.vidioc_queryctrl		= pxp_queryctrl,
	.vidioc_g_ctrl			= pxp_g_ctrl,
	.vidioc_s_ctrl			= pxp_s_ctrl,
};

static const struct video_device pxp_template = {
	.name				= "PxP",
	.vfl_type 			= V4L2_CAP_VIDEO_OUTPUT |
						V4L2_CAP_VIDEO_OVERLAY |
						V4L2_CAP_STREAMING,
	.vfl_dir			= VFL_DIR_TX,
	.fops				= &pxp_fops,
	.release			= pxp_release,
	.minor				= -1,
	.ioctl_ops			= &pxp_ioctl_ops,
};

static const struct of_device_id imx_pxpv4l2_dt_ids[] = {
	{ .compatible = "fsl,imx6sl-pxp-v4l2", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_pxpv4l2_dt_ids);

static int pxp_probe(struct platform_device *pdev)
{
	struct pxps *pxp;
	struct v4l2_device *v4l2_dev;
	int err = 0;

	pxp = kzalloc(sizeof(*pxp), GFP_KERNEL);
	if (!pxp) {
		dev_err(&pdev->dev, "failed to allocate control object\n");
		err = -ENOMEM;
		goto exit;
	}

	dev_set_drvdata(&pdev->dev, pxp);

	v4l2_dev = kzalloc(sizeof(*v4l2_dev), GFP_KERNEL);
	if (!v4l2_dev) {
		dev_err(&pdev->dev, "failed to allocate v4l2_dev structure\n");
		err = -ENOMEM;
		goto freeirq;
	}

	err = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (err) {
		dev_err(&pdev->dev, "register v4l2 device failed\n");
		goto freev4l2;
	}

	INIT_LIST_HEAD(&pxp->outq);
	spin_lock_init(&pxp->lock);
	mutex_init(&pxp->mutex);

	pxp->pdev = pdev;

	pxp->vdev = video_device_alloc();
	if (!pxp->vdev) {
		dev_err(&pdev->dev, "video_device_alloc() failed\n");
		err = -ENOMEM;
		goto relv4l2;
	}

	memcpy(pxp->vdev, &pxp_template, sizeof(pxp_template));
	pxp->vdev->v4l2_dev = v4l2_dev;
	video_set_drvdata(pxp->vdev, pxp);

	err = video_register_device(pxp->vdev, VFL_TYPE_GRABBER, video_nr);
	if (err) {
		dev_err(&pdev->dev, "failed to register video device\n");
		goto freevdev;
	}

	dev_info(&pdev->dev, "initialized\n");

exit:
	return err;

freevdev:
	video_device_release(pxp->vdev);
relv4l2:
	v4l2_device_unregister(v4l2_dev);
freev4l2:
	kfree(v4l2_dev);
freeirq:
	kfree(pxp);

	return err;
}

static int pxp_remove(struct platform_device *pdev)
{
	struct pxps *pxp = platform_get_drvdata(pdev);
	struct v4l2_device *v4l2_dev = pxp->vdev->v4l2_dev;

	video_unregister_device(pxp->vdev);
	video_device_release(pxp->vdev);
	v4l2_device_unregister(v4l2_dev);
	kfree(v4l2_dev);

	free_dma_buf(pxp, &pxp->outbuf);

	kfree(pxp);

	return 0;
}

static struct platform_driver pxp_driver = {
	.driver 	= {
		.name	= PXP_DRIVER_NAME,
		.of_match_table = of_match_ptr(imx_pxpv4l2_dt_ids),
	},
	.probe		= pxp_probe,
	.remove		= pxp_remove,
};

module_platform_driver(pxp_driver);

module_param(video_nr, int, 0444);
MODULE_DESCRIPTION("MXC PxP V4L2 driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
