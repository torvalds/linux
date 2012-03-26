/*
 * V4L2 Driver for i.MX27/i.MX25 camera host
 *
 * Copyright (C) 2008, Sascha Hauer, Pengutronix
 * Copyright (C) 2010, Baruch Siach, Orex Computed Radiography
 * Copyright (C) 2012, Javier Martin, Vista Silicon S.L.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/clk.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>

#include <linux/videodev2.h>

#include <mach/mx2_cam.h>
#include <mach/hardware.h>

#include <asm/dma.h>

#define MX2_CAM_DRV_NAME "mx2-camera"
#define MX2_CAM_VERSION "0.0.6"
#define MX2_CAM_DRIVER_DESCRIPTION "i.MX2x_Camera"

/* reset values */
#define CSICR1_RESET_VAL	0x40000800
#define CSICR2_RESET_VAL	0x0
#define CSICR3_RESET_VAL	0x0

/* csi control reg 1 */
#define CSICR1_SWAP16_EN	(1 << 31)
#define CSICR1_EXT_VSYNC	(1 << 30)
#define CSICR1_EOF_INTEN	(1 << 29)
#define CSICR1_PRP_IF_EN	(1 << 28)
#define CSICR1_CCIR_MODE	(1 << 27)
#define CSICR1_COF_INTEN	(1 << 26)
#define CSICR1_SF_OR_INTEN	(1 << 25)
#define CSICR1_RF_OR_INTEN	(1 << 24)
#define CSICR1_STATFF_LEVEL	(3 << 22)
#define CSICR1_STATFF_INTEN	(1 << 21)
#define CSICR1_RXFF_LEVEL(l)	(((l) & 3) << 19)	/* MX27 */
#define CSICR1_FB2_DMA_INTEN	(1 << 20)		/* MX25 */
#define CSICR1_FB1_DMA_INTEN	(1 << 19)		/* MX25 */
#define CSICR1_RXFF_INTEN	(1 << 18)
#define CSICR1_SOF_POL		(1 << 17)
#define CSICR1_SOF_INTEN	(1 << 16)
#define CSICR1_MCLKDIV(d)	(((d) & 0xF) << 12)
#define CSICR1_HSYNC_POL	(1 << 11)
#define CSICR1_CCIR_EN		(1 << 10)
#define CSICR1_MCLKEN		(1 << 9)
#define CSICR1_FCC		(1 << 8)
#define CSICR1_PACK_DIR		(1 << 7)
#define CSICR1_CLR_STATFIFO	(1 << 6)
#define CSICR1_CLR_RXFIFO	(1 << 5)
#define CSICR1_GCLK_MODE	(1 << 4)
#define CSICR1_INV_DATA		(1 << 3)
#define CSICR1_INV_PCLK		(1 << 2)
#define CSICR1_REDGE		(1 << 1)

#define SHIFT_STATFF_LEVEL	22
#define SHIFT_RXFF_LEVEL	19
#define SHIFT_MCLKDIV		12

/* control reg 3 */
#define CSICR3_FRMCNT		(0xFFFF << 16)
#define CSICR3_FRMCNT_RST	(1 << 15)
#define CSICR3_DMA_REFLASH_RFF	(1 << 14)
#define CSICR3_DMA_REFLASH_SFF	(1 << 13)
#define CSICR3_DMA_REQ_EN_RFF	(1 << 12)
#define CSICR3_DMA_REQ_EN_SFF	(1 << 11)
#define CSICR3_RXFF_LEVEL(l)	(((l) & 7) << 4)	/* MX25 */
#define CSICR3_CSI_SUP		(1 << 3)
#define CSICR3_ZERO_PACK_EN	(1 << 2)
#define CSICR3_ECC_INT_EN	(1 << 1)
#define CSICR3_ECC_AUTO_EN	(1 << 0)

#define SHIFT_FRMCNT		16

/* csi status reg */
#define CSISR_SFF_OR_INT	(1 << 25)
#define CSISR_RFF_OR_INT	(1 << 24)
#define CSISR_STATFF_INT	(1 << 21)
#define CSISR_DMA_TSF_FB2_INT	(1 << 20)	/* MX25 */
#define CSISR_DMA_TSF_FB1_INT	(1 << 19)	/* MX25 */
#define CSISR_RXFF_INT		(1 << 18)
#define CSISR_EOF_INT		(1 << 17)
#define CSISR_SOF_INT		(1 << 16)
#define CSISR_F2_INT		(1 << 15)
#define CSISR_F1_INT		(1 << 14)
#define CSISR_COF_INT		(1 << 13)
#define CSISR_ECC_INT		(1 << 1)
#define CSISR_DRDY		(1 << 0)

#define CSICR1			0x00
#define CSICR2			0x04
#define CSISR			(cpu_is_mx27() ? 0x08 : 0x18)
#define CSISTATFIFO		0x0c
#define CSIRFIFO		0x10
#define CSIRXCNT		0x14
#define CSICR3			(cpu_is_mx27() ? 0x1C : 0x08)
#define CSIDMASA_STATFIFO	0x20
#define CSIDMATA_STATFIFO	0x24
#define CSIDMASA_FB1		0x28
#define CSIDMASA_FB2		0x2c
#define CSIFBUF_PARA		0x30
#define CSIIMAG_PARA		0x34

/* EMMA PrP */
#define PRP_CNTL			0x00
#define PRP_INTR_CNTL			0x04
#define PRP_INTRSTATUS			0x08
#define PRP_SOURCE_Y_PTR		0x0c
#define PRP_SOURCE_CB_PTR		0x10
#define PRP_SOURCE_CR_PTR		0x14
#define PRP_DEST_RGB1_PTR		0x18
#define PRP_DEST_RGB2_PTR		0x1c
#define PRP_DEST_Y_PTR			0x20
#define PRP_DEST_CB_PTR			0x24
#define PRP_DEST_CR_PTR			0x28
#define PRP_SRC_FRAME_SIZE		0x2c
#define PRP_DEST_CH1_LINE_STRIDE	0x30
#define PRP_SRC_PIXEL_FORMAT_CNTL	0x34
#define PRP_CH1_PIXEL_FORMAT_CNTL	0x38
#define PRP_CH1_OUT_IMAGE_SIZE		0x3c
#define PRP_CH2_OUT_IMAGE_SIZE		0x40
#define PRP_SRC_LINE_STRIDE		0x44
#define PRP_CSC_COEF_012		0x48
#define PRP_CSC_COEF_345		0x4c
#define PRP_CSC_COEF_678		0x50
#define PRP_CH1_RZ_HORI_COEF1		0x54
#define PRP_CH1_RZ_HORI_COEF2		0x58
#define PRP_CH1_RZ_HORI_VALID		0x5c
#define PRP_CH1_RZ_VERT_COEF1		0x60
#define PRP_CH1_RZ_VERT_COEF2		0x64
#define PRP_CH1_RZ_VERT_VALID		0x68
#define PRP_CH2_RZ_HORI_COEF1		0x6c
#define PRP_CH2_RZ_HORI_COEF2		0x70
#define PRP_CH2_RZ_HORI_VALID		0x74
#define PRP_CH2_RZ_VERT_COEF1		0x78
#define PRP_CH2_RZ_VERT_COEF2		0x7c
#define PRP_CH2_RZ_VERT_VALID		0x80

#define PRP_CNTL_CH1EN		(1 << 0)
#define PRP_CNTL_CH2EN		(1 << 1)
#define PRP_CNTL_CSIEN		(1 << 2)
#define PRP_CNTL_DATA_IN_YUV420	(0 << 3)
#define PRP_CNTL_DATA_IN_YUV422	(1 << 3)
#define PRP_CNTL_DATA_IN_RGB16	(2 << 3)
#define PRP_CNTL_DATA_IN_RGB32	(3 << 3)
#define PRP_CNTL_CH1_OUT_RGB8	(0 << 5)
#define PRP_CNTL_CH1_OUT_RGB16	(1 << 5)
#define PRP_CNTL_CH1_OUT_RGB32	(2 << 5)
#define PRP_CNTL_CH1_OUT_YUV422	(3 << 5)
#define PRP_CNTL_CH2_OUT_YUV420	(0 << 7)
#define PRP_CNTL_CH2_OUT_YUV422 (1 << 7)
#define PRP_CNTL_CH2_OUT_YUV444	(2 << 7)
#define PRP_CNTL_CH1_LEN	(1 << 9)
#define PRP_CNTL_CH2_LEN	(1 << 10)
#define PRP_CNTL_SKIP_FRAME	(1 << 11)
#define PRP_CNTL_SWRST		(1 << 12)
#define PRP_CNTL_CLKEN		(1 << 13)
#define PRP_CNTL_WEN		(1 << 14)
#define PRP_CNTL_CH1BYP		(1 << 15)
#define PRP_CNTL_IN_TSKIP(x)	((x) << 16)
#define PRP_CNTL_CH1_TSKIP(x)	((x) << 19)
#define PRP_CNTL_CH2_TSKIP(x)	((x) << 22)
#define PRP_CNTL_INPUT_FIFO_LEVEL(x)	((x) << 25)
#define PRP_CNTL_RZ_FIFO_LEVEL(x)	((x) << 27)
#define PRP_CNTL_CH2B1EN	(1 << 29)
#define PRP_CNTL_CH2B2EN	(1 << 30)
#define PRP_CNTL_CH2FEN		(1 << 31)

/* IRQ Enable and status register */
#define PRP_INTR_RDERR		(1 << 0)
#define PRP_INTR_CH1WERR	(1 << 1)
#define PRP_INTR_CH2WERR	(1 << 2)
#define PRP_INTR_CH1FC		(1 << 3)
#define PRP_INTR_CH2FC		(1 << 5)
#define PRP_INTR_LBOVF		(1 << 7)
#define PRP_INTR_CH2OVF		(1 << 8)

/* Resizing registers */
#define PRP_RZ_VALID_TBL_LEN(x)	((x) << 24)
#define PRP_RZ_VALID_BILINEAR	(1 << 31)

#define MAX_VIDEO_MEM	16

#define RESIZE_NUM_MIN	1
#define RESIZE_NUM_MAX	20
#define BC_COEF		3
#define SZ_COEF		(1 << BC_COEF)

#define RESIZE_DIR_H	0
#define RESIZE_DIR_V	1

#define RESIZE_ALGO_BILINEAR 0
#define RESIZE_ALGO_AVERAGING 1

struct mx2_prp_cfg {
	int channel;
	u32 in_fmt;
	u32 out_fmt;
	u32 src_pixel;
	u32 ch1_pixel;
	u32 irq_flags;
};

/* prp resizing parameters */
struct emma_prp_resize {
	int		algo; /* type of algorithm used */
	int		len; /* number of coefficients */
	unsigned char	s[RESIZE_NUM_MAX]; /* table of coefficients */
};

/* prp configuration for a client-host fmt pair */
struct mx2_fmt_cfg {
	enum v4l2_mbus_pixelcode	in_fmt;
	u32				out_fmt;
	struct mx2_prp_cfg		cfg;
};

enum mx2_buffer_state {
	MX2_STATE_QUEUED,
	MX2_STATE_ACTIVE,
	MX2_STATE_DONE,
};

struct mx2_buf_internal {
	struct list_head	queue;
	int			bufnum;
	bool			discard;
};

/* buffer for one video frame */
struct mx2_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer		vb;
	enum mx2_buffer_state		state;
	struct mx2_buf_internal		internal;
};

struct mx2_camera_dev {
	struct device		*dev;
	struct soc_camera_host	soc_host;
	struct soc_camera_device *icd;
	struct clk		*clk_csi, *clk_emma;

	unsigned int		irq_csi, irq_emma;
	void __iomem		*base_csi, *base_emma;
	unsigned long		base_dma;

	struct mx2_camera_platform_data *pdata;
	struct resource		*res_csi, *res_emma;
	unsigned long		platform_flags;

	struct list_head	capture;
	struct list_head	active_bufs;
	struct list_head	discard;

	spinlock_t		lock;

	int			dma;
	struct mx2_buffer	*active;
	struct mx2_buffer	*fb1_active;
	struct mx2_buffer	*fb2_active;

	u32			csicr1;

	struct mx2_buf_internal buf_discard[2];
	void			*discard_buffer;
	dma_addr_t		discard_buffer_dma;
	size_t			discard_size;
	struct mx2_fmt_cfg	*emma_prp;
	struct emma_prp_resize	resizing[2];
	unsigned int		s_width, s_height;
	u32			frame_count;
	struct vb2_alloc_ctx	*alloc_ctx;
};

static struct mx2_buffer *mx2_ibuf_to_buf(struct mx2_buf_internal *int_buf)
{
	return container_of(int_buf, struct mx2_buffer, internal);
}

static struct mx2_fmt_cfg mx27_emma_prp_table[] = {
	/*
	 * This is a generic configuration which is valid for most
	 * prp input-output format combinations.
	 * We set the incomming and outgoing pixelformat to a
	 * 16 Bit wide format and adjust the bytesperline
	 * accordingly. With this configuration the inputdata
	 * will not be changed by the emma and could be any type
	 * of 16 Bit Pixelformat.
	 */
	{
		.in_fmt		= 0,
		.out_fmt	= 0,
		.cfg		= {
			.channel	= 1,
			.in_fmt		= PRP_CNTL_DATA_IN_RGB16,
			.out_fmt	= PRP_CNTL_CH1_OUT_RGB16,
			.src_pixel	= 0x2ca00565, /* RGB565 */
			.ch1_pixel	= 0x2ca00565, /* RGB565 */
			.irq_flags	= PRP_INTR_RDERR | PRP_INTR_CH1WERR |
						PRP_INTR_CH1FC | PRP_INTR_LBOVF,
		}
	},
	{
		.in_fmt		= V4L2_MBUS_FMT_YUYV8_2X8,
		.out_fmt	= V4L2_PIX_FMT_YUV420,
		.cfg		= {
			.channel	= 2,
			.in_fmt		= PRP_CNTL_DATA_IN_YUV422,
			.out_fmt	= PRP_CNTL_CH2_OUT_YUV420,
			.src_pixel	= 0x22000888, /* YUV422 (YUYV) */
			.irq_flags	= PRP_INTR_RDERR | PRP_INTR_CH2WERR |
					PRP_INTR_CH2FC | PRP_INTR_LBOVF |
					PRP_INTR_CH2OVF,
		}
	},
	{
		.in_fmt		= V4L2_MBUS_FMT_UYVY8_2X8,
		.out_fmt	= V4L2_PIX_FMT_YUV420,
		.cfg		= {
			.channel	= 2,
			.in_fmt		= PRP_CNTL_DATA_IN_YUV422,
			.out_fmt	= PRP_CNTL_CH2_OUT_YUV420,
			.src_pixel	= 0x22000888, /* YUV422 (YUYV) */
			.irq_flags	= PRP_INTR_RDERR | PRP_INTR_CH2WERR |
					PRP_INTR_CH2FC | PRP_INTR_LBOVF |
					PRP_INTR_CH2OVF,
		}
	},
};

static struct mx2_fmt_cfg *mx27_emma_prp_get_format(
					enum v4l2_mbus_pixelcode in_fmt,
					u32 out_fmt)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(mx27_emma_prp_table); i++)
		if ((mx27_emma_prp_table[i].in_fmt == in_fmt) &&
				(mx27_emma_prp_table[i].out_fmt == out_fmt)) {
			return &mx27_emma_prp_table[i];
		}
	/* If no match return the most generic configuration */
	return &mx27_emma_prp_table[0];
};

static void mx27_update_emma_buf(struct mx2_camera_dev *pcdev,
				 unsigned long phys, int bufnum)
{
	struct mx2_fmt_cfg *prp = pcdev->emma_prp;

	if (prp->cfg.channel == 1) {
		writel(phys, pcdev->base_emma +
				PRP_DEST_RGB1_PTR + 4 * bufnum);
	} else {
		writel(phys, pcdev->base_emma +
			PRP_DEST_Y_PTR - 0x14 * bufnum);
		if (prp->out_fmt == V4L2_PIX_FMT_YUV420) {
			u32 imgsize = pcdev->icd->user_height *
					pcdev->icd->user_width;

			writel(phys + imgsize, pcdev->base_emma +
				PRP_DEST_CB_PTR - 0x14 * bufnum);
			writel(phys + ((5 * imgsize) / 4), pcdev->base_emma +
				PRP_DEST_CR_PTR - 0x14 * bufnum);
		}
	}
}

static void mx2_camera_deactivate(struct mx2_camera_dev *pcdev)
{
	unsigned long flags;

	clk_disable(pcdev->clk_csi);
	writel(0, pcdev->base_csi + CSICR1);
	if (cpu_is_mx27()) {
		writel(0, pcdev->base_emma + PRP_CNTL);
	} else if (cpu_is_mx25()) {
		spin_lock_irqsave(&pcdev->lock, flags);
		pcdev->fb1_active = NULL;
		pcdev->fb2_active = NULL;
		writel(0, pcdev->base_csi + CSIDMASA_FB1);
		writel(0, pcdev->base_csi + CSIDMASA_FB2);
		spin_unlock_irqrestore(&pcdev->lock, flags);
	}
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be only one camera on mx2 camera sensor interface
 */
static int mx2_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	int ret;
	u32 csicr1;

	if (pcdev->icd)
		return -EBUSY;

	ret = clk_enable(pcdev->clk_csi);
	if (ret < 0)
		return ret;

	csicr1 = CSICR1_MCLKEN;

	if (cpu_is_mx27()) {
		csicr1 |= CSICR1_PRP_IF_EN | CSICR1_FCC |
			CSICR1_RXFF_LEVEL(0);
	} else if (cpu_is_mx27())
		csicr1 |= CSICR1_SOF_INTEN | CSICR1_RXFF_LEVEL(2);

	pcdev->csicr1 = csicr1;
	writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

	pcdev->icd = icd;
	pcdev->frame_count = 0;

	dev_info(icd->parent, "Camera driver attached to camera %d\n",
		 icd->devnum);

	return 0;
}

static void mx2_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	dev_info(icd->parent, "Camera driver detached from camera %d\n",
		 icd->devnum);

	mx2_camera_deactivate(pcdev);

	pcdev->icd = NULL;
}

static void mx25_camera_frame_done(struct mx2_camera_dev *pcdev, int fb,
		int state)
{
	struct vb2_buffer *vb;
	struct mx2_buffer *buf;
	struct mx2_buffer **fb_active = fb == 1 ? &pcdev->fb1_active :
		&pcdev->fb2_active;
	u32 fb_reg = fb == 1 ? CSIDMASA_FB1 : CSIDMASA_FB2;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (*fb_active == NULL)
		goto out;

	vb = &(*fb_active)->vb;
	dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	do_gettimeofday(&vb->v4l2_buf.timestamp);
	vb->v4l2_buf.sequence++;
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

	if (list_empty(&pcdev->capture)) {
		buf = NULL;
		writel(0, pcdev->base_csi + fb_reg);
	} else {
		buf = list_first_entry(&pcdev->capture, struct mx2_buffer,
				internal.queue);
		vb = &buf->vb;
		list_del(&buf->internal.queue);
		buf->state = MX2_STATE_ACTIVE;
		writel(vb2_dma_contig_plane_dma_addr(vb, 0),
		       pcdev->base_csi + fb_reg);
	}

	*fb_active = buf;

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static irqreturn_t mx25_camera_irq(int irq_csi, void *data)
{
	struct mx2_camera_dev *pcdev = data;
	u32 status = readl(pcdev->base_csi + CSISR);

	if (status & CSISR_DMA_TSF_FB1_INT)
		mx25_camera_frame_done(pcdev, 1, MX2_STATE_DONE);
	else if (status & CSISR_DMA_TSF_FB2_INT)
		mx25_camera_frame_done(pcdev, 2, MX2_STATE_DONE);

	/* FIXME: handle CSISR_RFF_OR_INT */

	writel(status, pcdev->base_csi + CSISR);

	return IRQ_HANDLED;
}

/*
 *  Videobuf operations
 */
static int mx2_videobuf_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *count, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);

	dev_dbg(icd->parent, "count=%d, size=%d\n", *count, sizes[0]);

	/* TODO: support for VIDIOC_CREATE_BUFS not ready */
	if (fmt != NULL)
		return -ENOTTY;

	if (bytes_per_line < 0)
		return bytes_per_line;

	alloc_ctxs[0] = pcdev->alloc_ctx;

	sizes[0] = bytes_per_line * icd->user_height;

	if (0 == *count)
		*count = 32;
	if (!*num_planes &&
	    sizes[0] * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / sizes[0];

	*num_planes = 1;

	return 0;
}

static int mx2_videobuf_prepare(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);
	int ret = 0;

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	if (bytes_per_line < 0)
		return bytes_per_line;

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	memset((void *)vb2_plane_vaddr(vb, 0),
	       0xaa, vb2_get_plane_payload(vb, 0));
#endif

	vb2_set_plane_payload(vb, 0, bytes_per_line * icd->user_height);
	if (vb2_plane_vaddr(vb, 0) &&
	    vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		ret = -EINVAL;
		goto out;
	}

	return 0;

out:
	return ret;
}

static void mx2_videobuf_queue(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_buffer *buf = container_of(vb, struct mx2_buffer, vb);
	unsigned long flags;

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	spin_lock_irqsave(&pcdev->lock, flags);

	buf->state = MX2_STATE_QUEUED;
	list_add_tail(&buf->internal.queue, &pcdev->capture);

	if (cpu_is_mx25()) {
		u32 csicr3, dma_inten = 0;

		if (pcdev->fb1_active == NULL) {
			writel(vb2_dma_contig_plane_dma_addr(vb, 0),
					pcdev->base_csi + CSIDMASA_FB1);
			pcdev->fb1_active = buf;
			dma_inten = CSICR1_FB1_DMA_INTEN;
		} else if (pcdev->fb2_active == NULL) {
			writel(vb2_dma_contig_plane_dma_addr(vb, 0),
					pcdev->base_csi + CSIDMASA_FB2);
			pcdev->fb2_active = buf;
			dma_inten = CSICR1_FB2_DMA_INTEN;
		}

		if (dma_inten) {
			list_del(&buf->internal.queue);
			buf->state = MX2_STATE_ACTIVE;

			csicr3 = readl(pcdev->base_csi + CSICR3);

			/* Reflash DMA */
			writel(csicr3 | CSICR3_DMA_REFLASH_RFF,
					pcdev->base_csi + CSICR3);

			/* clear & enable interrupts */
			writel(dma_inten, pcdev->base_csi + CSISR);
			pcdev->csicr1 |= dma_inten;
			writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

			/* enable DMA */
			csicr3 |= CSICR3_DMA_REQ_EN_RFF | CSICR3_RXFF_LEVEL(1);
			writel(csicr3, pcdev->base_csi + CSICR3);
		}
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void mx2_videobuf_release(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_buffer *buf = container_of(vb, struct mx2_buffer, vb);
	unsigned long flags;

#ifdef DEBUG
	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	switch (buf->state) {
	case MX2_STATE_ACTIVE:
		dev_info(icd->parent, "%s (active)\n", __func__);
		break;
	case MX2_STATE_QUEUED:
		dev_info(icd->parent, "%s (queued)\n", __func__);
		break;
	default:
		dev_info(icd->parent, "%s (unknown) %d\n", __func__,
				buf->state);
		break;
	}
#endif

	/*
	 * Terminate only queued but inactive buffers. Active buffers are
	 * released when they become inactive after videobuf_waiton().
	 *
	 * FIXME: implement forced termination of active buffers for mx27 and
	 * mx27 eMMA, so that the user won't get stuck in an uninterruptible
	 * state. This requires a specific handling for each of the these DMA
	 * types.
	 */

	spin_lock_irqsave(&pcdev->lock, flags);
	if (cpu_is_mx25() && buf->state == MX2_STATE_ACTIVE) {
		if (pcdev->fb1_active == buf) {
			pcdev->csicr1 &= ~CSICR1_FB1_DMA_INTEN;
			writel(0, pcdev->base_csi + CSIDMASA_FB1);
			pcdev->fb1_active = NULL;
		} else if (pcdev->fb2_active == buf) {
			pcdev->csicr1 &= ~CSICR1_FB2_DMA_INTEN;
			writel(0, pcdev->base_csi + CSIDMASA_FB2);
			pcdev->fb2_active = NULL;
		}
		writel(pcdev->csicr1, pcdev->base_csi + CSICR1);
	}
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void mx27_camera_emma_buf_init(struct soc_camera_device *icd,
		int bytesperline)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_fmt_cfg *prp = pcdev->emma_prp;

	writel((pcdev->s_width << 16) | pcdev->s_height,
	       pcdev->base_emma + PRP_SRC_FRAME_SIZE);
	writel(prp->cfg.src_pixel,
	       pcdev->base_emma + PRP_SRC_PIXEL_FORMAT_CNTL);
	if (prp->cfg.channel == 1) {
		writel((icd->user_width << 16) | icd->user_height,
			pcdev->base_emma + PRP_CH1_OUT_IMAGE_SIZE);
		writel(bytesperline,
			pcdev->base_emma + PRP_DEST_CH1_LINE_STRIDE);
		writel(prp->cfg.ch1_pixel,
			pcdev->base_emma + PRP_CH1_PIXEL_FORMAT_CNTL);
	} else { /* channel 2 */
		writel((icd->user_width << 16) | icd->user_height,
			pcdev->base_emma + PRP_CH2_OUT_IMAGE_SIZE);
	}

	/* Enable interrupts */
	writel(prp->cfg.irq_flags, pcdev->base_emma + PRP_INTR_CNTL);
}

static void mx2_prp_resize_commit(struct mx2_camera_dev *pcdev)
{
	int dir;

	for (dir = RESIZE_DIR_H; dir <= RESIZE_DIR_V; dir++) {
		unsigned char *s = pcdev->resizing[dir].s;
		int len = pcdev->resizing[dir].len;
		unsigned int coeff[2] = {0, 0};
		unsigned int valid  = 0;
		int i;

		if (len == 0)
			continue;

		for (i = RESIZE_NUM_MAX - 1; i >= 0; i--) {
			int j;

			j = i > 9 ? 1 : 0;
			coeff[j] = (coeff[j] << BC_COEF) |
					(s[i] & (SZ_COEF - 1));

			if (i == 5 || i == 15)
				coeff[j] <<= 1;

			valid = (valid << 1) | (s[i] >> BC_COEF);
		}

		valid |= PRP_RZ_VALID_TBL_LEN(len);

		if (pcdev->resizing[dir].algo == RESIZE_ALGO_BILINEAR)
			valid |= PRP_RZ_VALID_BILINEAR;

		if (pcdev->emma_prp->cfg.channel == 1) {
			if (dir == RESIZE_DIR_H) {
				writel(coeff[0], pcdev->base_emma +
							PRP_CH1_RZ_HORI_COEF1);
				writel(coeff[1], pcdev->base_emma +
							PRP_CH1_RZ_HORI_COEF2);
				writel(valid, pcdev->base_emma +
							PRP_CH1_RZ_HORI_VALID);
			} else {
				writel(coeff[0], pcdev->base_emma +
							PRP_CH1_RZ_VERT_COEF1);
				writel(coeff[1], pcdev->base_emma +
							PRP_CH1_RZ_VERT_COEF2);
				writel(valid, pcdev->base_emma +
							PRP_CH1_RZ_VERT_VALID);
			}
		} else {
			if (dir == RESIZE_DIR_H) {
				writel(coeff[0], pcdev->base_emma +
							PRP_CH2_RZ_HORI_COEF1);
				writel(coeff[1], pcdev->base_emma +
							PRP_CH2_RZ_HORI_COEF2);
				writel(valid, pcdev->base_emma +
							PRP_CH2_RZ_HORI_VALID);
			} else {
				writel(coeff[0], pcdev->base_emma +
							PRP_CH2_RZ_VERT_COEF1);
				writel(coeff[1], pcdev->base_emma +
							PRP_CH2_RZ_VERT_COEF2);
				writel(valid, pcdev->base_emma +
							PRP_CH2_RZ_VERT_VALID);
			}
		}
	}
}

static int mx2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(q);
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_fmt_cfg *prp = pcdev->emma_prp;
	struct vb2_buffer *vb;
	struct mx2_buffer *buf;
	unsigned long phys;
	int bytesperline;

	if (cpu_is_mx27()) {
		unsigned long flags;
		if (count < 2)
			return -EINVAL;

		spin_lock_irqsave(&pcdev->lock, flags);

		buf = list_first_entry(&pcdev->capture, struct mx2_buffer,
				       internal.queue);
		buf->internal.bufnum = 0;
		vb = &buf->vb;
		buf->state = MX2_STATE_ACTIVE;

		phys = vb2_dma_contig_plane_dma_addr(vb, 0);
		mx27_update_emma_buf(pcdev, phys, buf->internal.bufnum);
		list_move_tail(pcdev->capture.next, &pcdev->active_bufs);

		buf = list_first_entry(&pcdev->capture, struct mx2_buffer,
				       internal.queue);
		buf->internal.bufnum = 1;
		vb = &buf->vb;
		buf->state = MX2_STATE_ACTIVE;

		phys = vb2_dma_contig_plane_dma_addr(vb, 0);
		mx27_update_emma_buf(pcdev, phys, buf->internal.bufnum);
		list_move_tail(pcdev->capture.next, &pcdev->active_bufs);

		bytesperline = soc_mbus_bytes_per_line(icd->user_width,
				icd->current_fmt->host_fmt);
		if (bytesperline < 0)
			return bytesperline;

		/*
		 * I didn't manage to properly enable/disable the prp
		 * on a per frame basis during running transfers,
		 * thus we allocate a buffer here and use it to
		 * discard frames when no buffer is available.
		 * Feel free to work on this ;)
		 */
		pcdev->discard_size = icd->user_height * bytesperline;
		pcdev->discard_buffer = dma_alloc_coherent(ici->v4l2_dev.dev,
				pcdev->discard_size, &pcdev->discard_buffer_dma,
				GFP_KERNEL);
		if (!pcdev->discard_buffer)
			return -ENOMEM;

		pcdev->buf_discard[0].discard = true;
		list_add_tail(&pcdev->buf_discard[0].queue,
				      &pcdev->discard);

		pcdev->buf_discard[1].discard = true;
		list_add_tail(&pcdev->buf_discard[1].queue,
				      &pcdev->discard);

		mx2_prp_resize_commit(pcdev);

		mx27_camera_emma_buf_init(icd, bytesperline);

		if (prp->cfg.channel == 1) {
			writel(PRP_CNTL_CH1EN |
				PRP_CNTL_CSIEN |
				prp->cfg.in_fmt |
				prp->cfg.out_fmt |
				PRP_CNTL_CH1_LEN |
				PRP_CNTL_CH1BYP |
				PRP_CNTL_CH1_TSKIP(0) |
				PRP_CNTL_IN_TSKIP(0),
				pcdev->base_emma + PRP_CNTL);
		} else {
			writel(PRP_CNTL_CH2EN |
				PRP_CNTL_CSIEN |
				prp->cfg.in_fmt |
				prp->cfg.out_fmt |
				PRP_CNTL_CH2_LEN |
				PRP_CNTL_CH2_TSKIP(0) |
				PRP_CNTL_IN_TSKIP(0),
				pcdev->base_emma + PRP_CNTL);
		}
		spin_unlock_irqrestore(&pcdev->lock, flags);
	}

	return 0;
}

static int mx2_stop_streaming(struct vb2_queue *q)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(q);
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_fmt_cfg *prp = pcdev->emma_prp;
	unsigned long flags;
	void *b;
	u32 cntl;

	if (cpu_is_mx27()) {
		spin_lock_irqsave(&pcdev->lock, flags);

		cntl = readl(pcdev->base_emma + PRP_CNTL);
		if (prp->cfg.channel == 1) {
			writel(cntl & ~PRP_CNTL_CH1EN,
			       pcdev->base_emma + PRP_CNTL);
		} else {
			writel(cntl & ~PRP_CNTL_CH2EN,
			       pcdev->base_emma + PRP_CNTL);
		}
		INIT_LIST_HEAD(&pcdev->capture);
		INIT_LIST_HEAD(&pcdev->active_bufs);
		INIT_LIST_HEAD(&pcdev->discard);

		b = pcdev->discard_buffer;
		pcdev->discard_buffer = NULL;

		spin_unlock_irqrestore(&pcdev->lock, flags);

		dma_free_coherent(ici->v4l2_dev.dev,
			pcdev->discard_size, b, pcdev->discard_buffer_dma);
	}

	return 0;
}

static struct vb2_ops mx2_videobuf_ops = {
	.queue_setup	 = mx2_videobuf_setup,
	.buf_prepare	 = mx2_videobuf_prepare,
	.buf_queue	 = mx2_videobuf_queue,
	.buf_cleanup	 = mx2_videobuf_release,
	.start_streaming = mx2_start_streaming,
	.stop_streaming	 = mx2_stop_streaming,
};

static int mx2_camera_init_videobuf(struct vb2_queue *q,
			      struct soc_camera_device *icd)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = icd;
	q->ops = &mx2_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct mx2_buffer);

	return vb2_queue_init(q);
}

#define MX2_BUS_FLAGS	(V4L2_MBUS_MASTER | \
			V4L2_MBUS_VSYNC_ACTIVE_HIGH | \
			V4L2_MBUS_VSYNC_ACTIVE_LOW | \
			V4L2_MBUS_HSYNC_ACTIVE_HIGH | \
			V4L2_MBUS_HSYNC_ACTIVE_LOW | \
			V4L2_MBUS_PCLK_SAMPLE_RISING | \
			V4L2_MBUS_PCLK_SAMPLE_FALLING | \
			V4L2_MBUS_DATA_ACTIVE_HIGH | \
			V4L2_MBUS_DATA_ACTIVE_LOW)

static int mx27_camera_emma_prp_reset(struct mx2_camera_dev *pcdev)
{
	u32 cntl;
	int count = 0;

	cntl = readl(pcdev->base_emma + PRP_CNTL);
	writel(PRP_CNTL_SWRST, pcdev->base_emma + PRP_CNTL);
	while (count++ < 100) {
		if (!(readl(pcdev->base_emma + PRP_CNTL) & PRP_CNTL_SWRST))
			return 0;
		barrier();
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int mx2_camera_set_bus_param(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	const struct soc_camera_format_xlate *xlate;
	unsigned long common_flags;
	int ret;
	int bytesperline;
	u32 csicr1 = pcdev->csicr1;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, MX2_BUS_FLAGS);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%x\n",
				 cfg.flags, MX2_BUS_FLAGS);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = MX2_BUS_FLAGS;
	}

	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & MX2_CAMERA_HSYNC_HIGH)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) &&
	    (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (pcdev->platform_flags & MX2_CAMERA_PCLK_SAMPLE_RISING)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_dbg(icd->parent, "camera s_mbus_config(0x%lx) returned %d\n",
			common_flags, ret);
		return ret;
	}

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	if (xlate->code == V4L2_MBUS_FMT_YUYV8_2X8) {
		csicr1 |= CSICR1_PACK_DIR;
		csicr1 &= ~CSICR1_SWAP16_EN;
		dev_dbg(icd->parent, "already yuyv format, don't convert\n");
	} else if (xlate->code == V4L2_MBUS_FMT_UYVY8_2X8) {
		csicr1 &= ~CSICR1_PACK_DIR;
		csicr1 |= CSICR1_SWAP16_EN;
		dev_dbg(icd->parent, "convert uyvy mbus format into yuyv\n");
	} else {
		dev_warn(icd->parent, "mbus format not supported\n");
		return -EINVAL;
	}

	if (common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		csicr1 |= CSICR1_REDGE;
	if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		csicr1 |= CSICR1_SOF_POL;
	if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		csicr1 |= CSICR1_HSYNC_POL;
	if (pcdev->platform_flags & MX2_CAMERA_EXT_VSYNC)
		csicr1 |= CSICR1_EXT_VSYNC;
	if (pcdev->platform_flags & MX2_CAMERA_CCIR)
		csicr1 |= CSICR1_CCIR_EN;
	if (pcdev->platform_flags & MX2_CAMERA_CCIR_INTERLACE)
		csicr1 |= CSICR1_CCIR_MODE;
	if (pcdev->platform_flags & MX2_CAMERA_GATED_CLOCK)
		csicr1 |= CSICR1_GCLK_MODE;
	if (pcdev->platform_flags & MX2_CAMERA_INV_DATA)
		csicr1 |= CSICR1_INV_DATA;

	pcdev->csicr1 = csicr1;

	bytesperline = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);
	if (bytesperline < 0)
		return bytesperline;

	if (cpu_is_mx27()) {
		ret = mx27_camera_emma_prp_reset(pcdev);
		if (ret)
			return ret;
	} else if (cpu_is_mx25()) {
		writel((bytesperline * icd->user_height) >> 2,
				pcdev->base_csi + CSIRXCNT);
		writel((bytesperline << 16) | icd->user_height,
				pcdev->base_csi + CSIIMAG_PARA);
	}

	writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

	return 0;
}

static int mx2_camera_set_crop(struct soc_camera_device *icd,
				struct v4l2_crop *a)
{
	struct v4l2_rect *rect = &a->c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	int ret;

	soc_camera_limit_side(&rect->left, &rect->width, 0, 2, 4096);
	soc_camera_limit_side(&rect->top, &rect->height, 0, 2, 4096);

	ret = v4l2_subdev_call(sd, video, s_crop, a);
	if (ret < 0)
		return ret;

	/* The capture device might have changed its output  */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	dev_dbg(icd->parent, "Sensor cropped %dx%d\n",
		mf.width, mf.height);

	icd->user_width		= mf.width;
	icd->user_height	= mf.height;

	return ret;
}

static int mx2_camera_get_formats(struct soc_camera_device *icd,
				  unsigned int idx,
				  struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_mbus_pixelfmt *fmt;
	struct device *dev = icd->parent;
	enum v4l2_mbus_pixelcode code;
	int ret, formats = 0;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* no more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code);
		return 0;
	}

	if (code == V4L2_MBUS_FMT_YUYV8_2X8 ||
	    code == V4L2_MBUS_FMT_UYVY8_2X8) {
		formats++;
		if (xlate) {
			/*
			 * CH2 can output YUV420 which is a standard format in
			 * soc_mediabus.c
			 */
			xlate->host_fmt =
				soc_mbus_get_fmtdesc(V4L2_MBUS_FMT_YUYV8_1_5X8);
			xlate->code	= code;
			dev_dbg(dev, "Providing host format %s for sensor code %d\n",
			       xlate->host_fmt->name, code);
			xlate++;
		}
	}

	if (code == V4L2_MBUS_FMT_UYVY8_2X8) {
		formats++;
		if (xlate) {
			xlate->host_fmt =
				soc_mbus_get_fmtdesc(V4L2_MBUS_FMT_YUYV8_2X8);
			xlate->code	= code;
			dev_dbg(dev, "Providing host format %s for sensor code %d\n",
				xlate->host_fmt->name, code);
			xlate++;
		}
	}

	/* Generic pass-trough */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code	= code;
		xlate++;
	}
	return formats;
}

static int mx2_emmaprp_resize(struct mx2_camera_dev *pcdev,
			      struct v4l2_mbus_framefmt *mf_in,
			      struct v4l2_pix_format *pix_out, bool apply)
{
	int num, den;
	unsigned long m;
	int i, dir;

	for (dir = RESIZE_DIR_H; dir <= RESIZE_DIR_V; dir++) {
		struct emma_prp_resize tmprsz;
		unsigned char *s = tmprsz.s;
		int len = 0;
		int in, out;

		if (dir == RESIZE_DIR_H) {
			in = mf_in->width;
			out = pix_out->width;
		} else {
			in = mf_in->height;
			out = pix_out->height;
		}

		if (in < out)
			return -EINVAL;
		else if (in == out)
			continue;

		/* Calculate ratio */
		m = gcd(in, out);
		num = in / m;
		den = out / m;
		if (num > RESIZE_NUM_MAX)
			return -EINVAL;

		if ((num >= 2 * den) && (den == 1) &&
		    (num < 9) && (!(num & 0x01))) {
			int sum = 0;
			int j;

			/* Average scaling for >= 2:1 ratios */
			/* Support can be added for num >=9 and odd values */

			tmprsz.algo = RESIZE_ALGO_AVERAGING;
			len = num;

			for (i = 0; i < (len / 2); i++)
				s[i] = 8;

			do {
				for (i = 0; i < (len / 2); i++) {
					s[i] = s[i] >> 1;
					sum = 0;
					for (j = 0; j < (len / 2); j++)
						sum += s[j];
					if (sum == 4)
						break;
				}
			} while (sum != 4);

			for (i = (len / 2); i < len; i++)
				s[i] = s[len - i - 1];

			s[len - 1] |= SZ_COEF;
		} else {
			/* bilinear scaling for < 2:1 ratios */
			int v; /* overflow counter */
			int coeff, nxt; /* table output */
			int in_pos_inc = 2 * den;
			int out_pos = num;
			int out_pos_inc = 2 * num;
			int init_carry = num - den;
			int carry = init_carry;

			tmprsz.algo = RESIZE_ALGO_BILINEAR;
			v = den + in_pos_inc;
			do {
				coeff = v - out_pos;
				out_pos += out_pos_inc;
				carry += out_pos_inc;
				for (nxt = 0; v < out_pos; nxt++) {
					v += in_pos_inc;
					carry -= in_pos_inc;
				}

				if (len > RESIZE_NUM_MAX)
					return -EINVAL;

				coeff = ((coeff << BC_COEF) +
					(in_pos_inc >> 1)) / in_pos_inc;

				if (coeff >= (SZ_COEF - 1))
					coeff--;

				coeff |= SZ_COEF;
				s[len] = (unsigned char)coeff;
				len++;

				for (i = 1; i < nxt; i++) {
					if (len >= RESIZE_NUM_MAX)
						return -EINVAL;
					s[len] = 0;
					len++;
				}
			} while (carry != init_carry);
		}
		tmprsz.len = len;
		if (dir == RESIZE_DIR_H)
			mf_in->width = pix_out->width;
		else
			mf_in->height = pix_out->height;

		if (apply)
			memcpy(&pcdev->resizing[dir], &tmprsz, sizeof(tmprsz));
	}
	return 0;
}

static int mx2_camera_set_fmt(struct soc_camera_device *icd,
			       struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;

	dev_dbg(icd->parent, "%s: requested params: width = %d, height = %d\n",
		__func__, pix->width, pix->height);

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(icd->parent, "Format %x not found\n",
				pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	/* Store width and height returned by the sensor for resizing */
	pcdev->s_width = mf.width;
	pcdev->s_height = mf.height;
	dev_dbg(icd->parent, "%s: sensor params: width = %d, height = %d\n",
		__func__, pcdev->s_width, pcdev->s_height);

	pcdev->emma_prp = mx27_emma_prp_get_format(xlate->code,
						   xlate->host_fmt->fourcc);

	memset(pcdev->resizing, 0, sizeof(pcdev->resizing));
	if ((mf.width != pix->width || mf.height != pix->height) &&
		pcdev->emma_prp->cfg.in_fmt == PRP_CNTL_DATA_IN_YUV422) {
		if (mx2_emmaprp_resize(pcdev, &mf, pix, true) < 0)
			dev_dbg(icd->parent, "%s: can't resize\n", __func__);
	}

	if (mf.code != xlate->code)
		return -EINVAL;

	pix->width		= mf.width;
	pix->height		= mf.height;
	pix->field		= mf.field;
	pix->colorspace		= mf.colorspace;
	icd->current_fmt	= xlate;

	dev_dbg(icd->parent, "%s: returned params: width = %d, height = %d\n",
		__func__, pix->width, pix->height);

	return 0;
}

static int mx2_camera_try_fmt(struct soc_camera_device *icd,
				  struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	unsigned int width_limit;
	int ret;

	dev_dbg(icd->parent, "%s: requested params: width = %d, height = %d\n",
		__func__, pix->width, pix->height);

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (pixfmt && !xlate) {
		dev_warn(icd->parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* FIXME: implement MX27 limits */

	/* limit to MX25 hardware capabilities */
	if (cpu_is_mx25()) {
		if (xlate->host_fmt->bits_per_sample <= 8)
			width_limit = 0xffff * 4;
		else
			width_limit = 0xffff * 2;
		/* CSIIMAG_PARA limit */
		if (pix->width > width_limit)
			pix->width = width_limit;
		if (pix->height > 0xffff)
			pix->height = 0xffff;

		pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
				xlate->host_fmt);
		if (pix->bytesperline < 0)
			return pix->bytesperline;
		pix->sizeimage = pix->height * pix->bytesperline;
		/* Check against the CSIRXCNT limit */
		if (pix->sizeimage > 4 * 0x3ffff) {
			/* Adjust geometry, preserve aspect ratio */
			unsigned int new_height = int_sqrt(4 * 0x3ffff *
					pix->height / pix->bytesperline);
			pix->width = new_height * pix->width / pix->height;
			pix->height = new_height;
			pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
							xlate->host_fmt);
			BUG_ON(pix->bytesperline < 0);
		}
	}

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	dev_dbg(icd->parent, "%s: sensor params: width = %d, height = %d\n",
		__func__, pcdev->s_width, pcdev->s_height);

	/* If the sensor does not support image size try PrP resizing */
	pcdev->emma_prp = mx27_emma_prp_get_format(xlate->code,
						   xlate->host_fmt->fourcc);

	memset(pcdev->resizing, 0, sizeof(pcdev->resizing));
	if ((mf.width != pix->width || mf.height != pix->height) &&
		pcdev->emma_prp->cfg.in_fmt == PRP_CNTL_DATA_IN_YUV422) {
		if (mx2_emmaprp_resize(pcdev, &mf, pix, false) < 0)
			dev_dbg(icd->parent, "%s: can't resize\n", __func__);
	}

	if (mf.field == V4L2_FIELD_ANY)
		mf.field = V4L2_FIELD_NONE;
	/*
	 * Driver supports interlaced images provided they have
	 * both fields so that they can be processed as if they
	 * were progressive.
	 */
	if (mf.field != V4L2_FIELD_NONE && !V4L2_FIELD_HAS_BOTH(mf.field)) {
		dev_err(icd->parent, "Field type %d unsupported.\n",
				mf.field);
		return -EINVAL;
	}

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->field	= mf.field;
	pix->colorspace	= mf.colorspace;

	dev_dbg(icd->parent, "%s: returned params: width = %d, height = %d\n",
		__func__, pix->width, pix->height);

	return 0;
}

static int mx2_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the friendly caller:-> */
	strlcpy(cap->card, MX2_CAM_DRIVER_DESCRIPTION, sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static unsigned int mx2_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static struct soc_camera_host_ops mx2_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= mx2_camera_add_device,
	.remove		= mx2_camera_remove_device,
	.set_fmt	= mx2_camera_set_fmt,
	.set_crop	= mx2_camera_set_crop,
	.get_formats	= mx2_camera_get_formats,
	.try_fmt	= mx2_camera_try_fmt,
	.init_videobuf2	= mx2_camera_init_videobuf,
	.poll		= mx2_camera_poll,
	.querycap	= mx2_camera_querycap,
	.set_bus_param	= mx2_camera_set_bus_param,
};

static void mx27_camera_frame_done_emma(struct mx2_camera_dev *pcdev,
		int bufnum, bool err)
{
#ifdef DEBUG
	struct mx2_fmt_cfg *prp = pcdev->emma_prp;
#endif
	struct mx2_buf_internal *ibuf;
	struct mx2_buffer *buf;
	struct vb2_buffer *vb;
	unsigned long phys;

	ibuf = list_first_entry(&pcdev->active_bufs, struct mx2_buf_internal,
			       queue);

	BUG_ON(ibuf->bufnum != bufnum);

	if (ibuf->discard) {
		/*
		 * Discard buffer must not be returned to user space.
		 * Just return it to the discard queue.
		 */
		list_move_tail(pcdev->active_bufs.next, &pcdev->discard);
	} else {
		buf = mx2_ibuf_to_buf(ibuf);

		vb = &buf->vb;
#ifdef DEBUG
		phys = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (prp->cfg.channel == 1) {
			if (readl(pcdev->base_emma + PRP_DEST_RGB1_PTR +
				4 * bufnum) != phys) {
				dev_err(pcdev->dev, "%lx != %x\n", phys,
					readl(pcdev->base_emma +
					PRP_DEST_RGB1_PTR + 4 * bufnum));
			}
		} else {
			if (readl(pcdev->base_emma + PRP_DEST_Y_PTR -
				0x14 * bufnum) != phys) {
				dev_err(pcdev->dev, "%lx != %x\n", phys,
					readl(pcdev->base_emma +
					PRP_DEST_Y_PTR - 0x14 * bufnum));
			}
		}
#endif
		dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__, vb,
				vb2_plane_vaddr(vb, 0),
				vb2_get_plane_payload(vb, 0));

		list_del_init(&buf->internal.queue);
		do_gettimeofday(&vb->v4l2_buf.timestamp);
		vb->v4l2_buf.sequence = pcdev->frame_count;
		if (err)
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		else
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}

	pcdev->frame_count++;

	if (list_empty(&pcdev->capture)) {
		if (list_empty(&pcdev->discard)) {
			dev_warn(pcdev->dev, "%s: trying to access empty discard list\n",
				 __func__);
			return;
		}

		ibuf = list_first_entry(&pcdev->discard,
					struct mx2_buf_internal, queue);
		ibuf->bufnum = bufnum;

		list_move_tail(pcdev->discard.next, &pcdev->active_bufs);
		mx27_update_emma_buf(pcdev, pcdev->discard_buffer_dma, bufnum);
		return;
	}

	buf = list_first_entry(&pcdev->capture, struct mx2_buffer,
			       internal.queue);

	buf->internal.bufnum = bufnum;

	list_move_tail(pcdev->capture.next, &pcdev->active_bufs);

	vb = &buf->vb;
	buf->state = MX2_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);
	mx27_update_emma_buf(pcdev, phys, bufnum);
}

static irqreturn_t mx27_camera_emma_irq(int irq_emma, void *data)
{
	struct mx2_camera_dev *pcdev = data;
	unsigned int status = readl(pcdev->base_emma + PRP_INTRSTATUS);
	struct mx2_buf_internal *ibuf;

	spin_lock(&pcdev->lock);

	if (list_empty(&pcdev->active_bufs)) {
		dev_warn(pcdev->dev, "%s: called while active list is empty\n",
			__func__);

		if (!status) {
			spin_unlock(&pcdev->lock);
			return IRQ_NONE;
		}
	}

	if (status & (1 << 7)) { /* overflow */
		u32 cntl = readl(pcdev->base_emma + PRP_CNTL);
		writel(cntl & ~(PRP_CNTL_CH1EN | PRP_CNTL_CH2EN),
		       pcdev->base_emma + PRP_CNTL);
		writel(cntl, pcdev->base_emma + PRP_CNTL);

		ibuf = list_first_entry(&pcdev->active_bufs,
					struct mx2_buf_internal, queue);
		mx27_camera_frame_done_emma(pcdev,
					ibuf->bufnum, true);

		status &= ~(1 << 7);
	} else if (((status & (3 << 5)) == (3 << 5)) ||
		((status & (3 << 3)) == (3 << 3))) {
		/*
		 * Both buffers have triggered, process the one we're expecting
		 * to first
		 */
		ibuf = list_first_entry(&pcdev->active_bufs,
					struct mx2_buf_internal, queue);
		mx27_camera_frame_done_emma(pcdev, ibuf->bufnum, false);
		status &= ~(1 << (6 - ibuf->bufnum)); /* mark processed */
	} else if ((status & (1 << 6)) || (status & (1 << 4))) {
		mx27_camera_frame_done_emma(pcdev, 0, false);
	} else if ((status & (1 << 5)) || (status & (1 << 3))) {
		mx27_camera_frame_done_emma(pcdev, 1, false);
	}

	spin_unlock(&pcdev->lock);
	writel(status, pcdev->base_emma + PRP_INTRSTATUS);

	return IRQ_HANDLED;
}

static int __devinit mx27_camera_emma_init(struct mx2_camera_dev *pcdev)
{
	struct resource *res_emma = pcdev->res_emma;
	int err = 0;

	if (!request_mem_region(res_emma->start, resource_size(res_emma),
				MX2_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto out;
	}

	pcdev->base_emma = ioremap(res_emma->start, resource_size(res_emma));
	if (!pcdev->base_emma) {
		err = -ENOMEM;
		goto exit_release;
	}

	err = request_irq(pcdev->irq_emma, mx27_camera_emma_irq, 0,
			MX2_CAM_DRV_NAME, pcdev);
	if (err) {
		dev_err(pcdev->dev, "Camera EMMA interrupt register failed \n");
		goto exit_iounmap;
	}

	pcdev->clk_emma = clk_get(NULL, "emma");
	if (IS_ERR(pcdev->clk_emma)) {
		err = PTR_ERR(pcdev->clk_emma);
		goto exit_free_irq;
	}

	clk_enable(pcdev->clk_emma);

	err = mx27_camera_emma_prp_reset(pcdev);
	if (err)
		goto exit_clk_emma_put;

	return err;

exit_clk_emma_put:
	clk_disable(pcdev->clk_emma);
	clk_put(pcdev->clk_emma);
exit_free_irq:
	free_irq(pcdev->irq_emma, pcdev);
exit_iounmap:
	iounmap(pcdev->base_emma);
exit_release:
	release_mem_region(res_emma->start, resource_size(res_emma));
out:
	return err;
}

static int __devinit mx2_camera_probe(struct platform_device *pdev)
{
	struct mx2_camera_dev *pcdev;
	struct resource *res_csi, *res_emma;
	void __iomem *base_csi;
	int irq_csi, irq_emma;
	int err = 0;

	dev_dbg(&pdev->dev, "initialising\n");

	res_csi = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_csi = platform_get_irq(pdev, 0);
	if (res_csi == NULL || irq_csi < 0) {
		dev_err(&pdev->dev, "Missing platform resources data\n");
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	pcdev->clk_csi = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk_csi)) {
		dev_err(&pdev->dev, "Could not get csi clock\n");
		err = PTR_ERR(pcdev->clk_csi);
		goto exit_kfree;
	}

	pcdev->res_csi = res_csi;
	pcdev->pdata = pdev->dev.platform_data;
	if (pcdev->pdata) {
		long rate;

		pcdev->platform_flags = pcdev->pdata->flags;

		rate = clk_round_rate(pcdev->clk_csi, pcdev->pdata->clk * 2);
		if (rate <= 0) {
			err = -ENODEV;
			goto exit_dma_free;
		}
		err = clk_set_rate(pcdev->clk_csi, rate);
		if (err < 0)
			goto exit_dma_free;
	}

	INIT_LIST_HEAD(&pcdev->capture);
	INIT_LIST_HEAD(&pcdev->active_bufs);
	INIT_LIST_HEAD(&pcdev->discard);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the regions.
	 */
	if (!request_mem_region(res_csi->start, resource_size(res_csi),
				MX2_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto exit_dma_free;
	}

	base_csi = ioremap(res_csi->start, resource_size(res_csi));
	if (!base_csi) {
		err = -ENOMEM;
		goto exit_release;
	}
	pcdev->irq_csi = irq_csi;
	pcdev->base_csi = base_csi;
	pcdev->base_dma = res_csi->start;
	pcdev->dev = &pdev->dev;

	if (cpu_is_mx25()) {
		err = request_irq(pcdev->irq_csi, mx25_camera_irq, 0,
				MX2_CAM_DRV_NAME, pcdev);
		if (err) {
			dev_err(pcdev->dev, "Camera interrupt register failed \n");
			goto exit_iounmap;
		}
	}

	if (cpu_is_mx27()) {
		/* EMMA support */
		res_emma = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		irq_emma = platform_get_irq(pdev, 1);

		if (!res_emma || !irq_emma) {
			dev_err(&pdev->dev, "no EMMA resources\n");
			goto exit_free_irq;
		}

		pcdev->res_emma = res_emma;
		pcdev->irq_emma = irq_emma;
		if (mx27_camera_emma_init(pcdev))
			goto exit_free_irq;
	}

	pcdev->soc_host.drv_name	= MX2_CAM_DRV_NAME,
	pcdev->soc_host.ops		= &mx2_soc_camera_host_ops,
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
	pcdev->soc_host.nr		= pdev->id;

	pcdev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(pcdev->alloc_ctx)) {
		err = PTR_ERR(pcdev->alloc_ctx);
		goto eallocctx;
	}
	err = soc_camera_host_register(&pcdev->soc_host);
	if (err)
		goto exit_free_emma;

	dev_info(&pdev->dev, "MX2 Camera (CSI) driver probed, clock frequency: %ld\n",
			clk_get_rate(pcdev->clk_csi));

	return 0;

exit_free_emma:
	vb2_dma_contig_cleanup_ctx(pcdev->alloc_ctx);
eallocctx:
	if (cpu_is_mx27()) {
		free_irq(pcdev->irq_emma, pcdev);
		clk_disable(pcdev->clk_emma);
		clk_put(pcdev->clk_emma);
		iounmap(pcdev->base_emma);
		release_mem_region(pcdev->res_emma->start, resource_size(pcdev->res_emma));
	}
exit_free_irq:
	if (cpu_is_mx25())
		free_irq(pcdev->irq_csi, pcdev);
exit_iounmap:
	iounmap(base_csi);
exit_release:
	release_mem_region(res_csi->start, resource_size(res_csi));
exit_dma_free:
	clk_put(pcdev->clk_csi);
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit mx2_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct mx2_camera_dev *pcdev = container_of(soc_host,
			struct mx2_camera_dev, soc_host);
	struct resource *res;

	clk_put(pcdev->clk_csi);
	if (cpu_is_mx25())
		free_irq(pcdev->irq_csi, pcdev);
	if (cpu_is_mx27())
		free_irq(pcdev->irq_emma, pcdev);

	soc_camera_host_unregister(&pcdev->soc_host);

	vb2_dma_contig_cleanup_ctx(pcdev->alloc_ctx);

	iounmap(pcdev->base_csi);

	if (cpu_is_mx27()) {
		clk_disable(pcdev->clk_emma);
		clk_put(pcdev->clk_emma);
		iounmap(pcdev->base_emma);
		res = pcdev->res_emma;
		release_mem_region(res->start, resource_size(res));
	}

	res = pcdev->res_csi;
	release_mem_region(res->start, resource_size(res));

	kfree(pcdev);

	dev_info(&pdev->dev, "MX2 Camera driver unloaded\n");

	return 0;
}

static struct platform_driver mx2_camera_driver = {
	.driver 	= {
		.name	= MX2_CAM_DRV_NAME,
	},
	.remove		= __devexit_p(mx2_camera_remove),
};


static int __init mx2_camera_init(void)
{
	return platform_driver_probe(&mx2_camera_driver, &mx2_camera_probe);
}

static void __exit mx2_camera_exit(void)
{
	return platform_driver_unregister(&mx2_camera_driver);
}

module_init(mx2_camera_init);
module_exit(mx2_camera_exit);

MODULE_DESCRIPTION("i.MX27/i.MX25 SoC Camera Host driver");
MODULE_AUTHOR("Sascha Hauer <sha@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(MX2_CAM_VERSION);
