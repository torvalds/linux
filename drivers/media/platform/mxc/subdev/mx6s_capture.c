/*
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file mx6s_csi.c
 *
 * @brief mx6sx CMOS Sensor interface functions
 *
 * @ingroup CSI
 */
#include <asm/dma.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gcd.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/media-bus-format.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-of.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#define MX6S_CAM_DRV_NAME "mx6s-csi"
#define MX6S_CAM_VERSION "0.0.1"
#define MX6S_CAM_DRIVER_DESCRIPTION "i.MX6S_CSI"

#define MAX_VIDEO_MEM 64

/* reset values */
#define CSICR1_RESET_VAL	0x40000800
#define CSICR2_RESET_VAL	0x0
#define CSICR3_RESET_VAL	0x0

/* csi control reg 1 */
#define BIT_SWAP16_EN		(0x1 << 31)
#define BIT_EXT_VSYNC		(0x1 << 30)
#define BIT_EOF_INT_EN		(0x1 << 29)
#define BIT_PRP_IF_EN		(0x1 << 28)
#define BIT_CCIR_MODE		(0x1 << 27)
#define BIT_COF_INT_EN		(0x1 << 26)
#define BIT_SF_OR_INTEN		(0x1 << 25)
#define BIT_RF_OR_INTEN		(0x1 << 24)
#define BIT_SFF_DMA_DONE_INTEN  (0x1 << 22)
#define BIT_STATFF_INTEN	(0x1 << 21)
#define BIT_FB2_DMA_DONE_INTEN  (0x1 << 20)
#define BIT_FB1_DMA_DONE_INTEN  (0x1 << 19)
#define BIT_RXFF_INTEN		(0x1 << 18)
#define BIT_SOF_POL		(0x1 << 17)
#define BIT_SOF_INTEN		(0x1 << 16)
#define BIT_MCLKDIV		(0xF << 12)
#define BIT_HSYNC_POL		(0x1 << 11)
#define BIT_CCIR_EN		(0x1 << 10)
#define BIT_MCLKEN		(0x1 << 9)
#define BIT_FCC			(0x1 << 8)
#define BIT_PACK_DIR		(0x1 << 7)
#define BIT_CLR_STATFIFO	(0x1 << 6)
#define BIT_CLR_RXFIFO		(0x1 << 5)
#define BIT_GCLK_MODE		(0x1 << 4)
#define BIT_INV_DATA		(0x1 << 3)
#define BIT_INV_PCLK		(0x1 << 2)
#define BIT_REDGE		(0x1 << 1)
#define BIT_PIXEL_BIT		(0x1 << 0)

#define SHIFT_MCLKDIV		12

/* control reg 3 */
#define BIT_FRMCNT		(0xFFFF << 16)
#define BIT_FRMCNT_RST		(0x1 << 15)
#define BIT_DMA_REFLASH_RFF	(0x1 << 14)
#define BIT_DMA_REFLASH_SFF	(0x1 << 13)
#define BIT_DMA_REQ_EN_RFF	(0x1 << 12)
#define BIT_DMA_REQ_EN_SFF	(0x1 << 11)
#define BIT_STATFF_LEVEL	(0x7 << 8)
#define BIT_HRESP_ERR_EN	(0x1 << 7)
#define BIT_RXFF_LEVEL		(0x7 << 4)
#define BIT_TWO_8BIT_SENSOR	(0x1 << 3)
#define BIT_ZERO_PACK_EN	(0x1 << 2)
#define BIT_ECC_INT_EN		(0x1 << 1)
#define BIT_ECC_AUTO_EN		(0x1 << 0)

#define SHIFT_FRMCNT		16
#define SHIFT_RXFIFO_LEVEL	4

/* csi status reg */
#define BIT_ADDR_CH_ERR_INT (0x1 << 28)
#define BIT_FIELD0_INT      (0x1 << 27)
#define BIT_FIELD1_INT      (0x1 << 26)
#define BIT_SFF_OR_INT		(0x1 << 25)
#define BIT_RFF_OR_INT		(0x1 << 24)
#define BIT_DMA_TSF_DONE_SFF	(0x1 << 22)
#define BIT_STATFF_INT		(0x1 << 21)
#define BIT_DMA_TSF_DONE_FB2	(0x1 << 20)
#define BIT_DMA_TSF_DONE_FB1	(0x1 << 19)
#define BIT_RXFF_INT		(0x1 << 18)
#define BIT_EOF_INT		(0x1 << 17)
#define BIT_SOF_INT		(0x1 << 16)
#define BIT_F2_INT		(0x1 << 15)
#define BIT_F1_INT		(0x1 << 14)
#define BIT_COF_INT		(0x1 << 13)
#define BIT_HRESP_ERR_INT	(0x1 << 7)
#define BIT_ECC_INT		(0x1 << 1)
#define BIT_DRDY		(0x1 << 0)

/* csi control reg 18 */
#define BIT_CSI_ENABLE			(0x1 << 31)
#define BIT_MIPI_DATA_FORMAT_RAW8		(0x2a << 25)
#define BIT_MIPI_DATA_FORMAT_RAW10		(0x2b << 25)
#define BIT_MIPI_DATA_FORMAT_YUV422_8B	(0x1e << 25)
#define BIT_MIPI_DATA_FORMAT_MASK	(0x3F << 25)
#define BIT_MIPI_DATA_FORMAT_OFFSET	25
#define BIT_DATA_FROM_MIPI		(0x1 << 22)
#define BIT_MIPI_YU_SWAP		(0x1 << 21)
#define BIT_MIPI_DOUBLE_CMPNT	(0x1 << 20)
#define BIT_BASEADDR_CHG_ERR_EN	(0x1 << 9)
#define BIT_BASEADDR_SWITCH_SEL	(0x1 << 5)
#define BIT_BASEADDR_SWITCH_EN	(0x1 << 4)
#define BIT_PARALLEL24_EN		(0x1 << 3)
#define BIT_DEINTERLACE_EN		(0x1 << 2)
#define BIT_TVDECODER_IN_EN		(0x1 << 1)
#define BIT_NTSC_EN				(0x1 << 0)

#define CSI_MCLK_VF		1
#define CSI_MCLK_ENC		2
#define CSI_MCLK_RAW		4
#define CSI_MCLK_I2C		8

#define CSI_CSICR1		0x0
#define CSI_CSICR2		0x4
#define CSI_CSICR3		0x8
#define CSI_STATFIFO		0xC
#define CSI_CSIRXFIFO		0x10
#define CSI_CSIRXCNT		0x14
#define CSI_CSISR		0x18

#define CSI_CSIDBG		0x1C
#define CSI_CSIDMASA_STATFIFO	0x20
#define CSI_CSIDMATS_STATFIFO	0x24
#define CSI_CSIDMASA_FB1	0x28
#define CSI_CSIDMASA_FB2	0x2C
#define CSI_CSIFBUF_PARA	0x30
#define CSI_CSIIMAG_PARA	0x34

#define CSI_CSICR18		0x48
#define CSI_CSICR19		0x4c

#define NUM_FORMATS ARRAY_SIZE(formats)
#define MX6SX_MAX_SENSORS    1

struct csi_signal_cfg_t {
	unsigned data_width:3;
	unsigned clk_mode:2;
	unsigned ext_vsync:1;
	unsigned Vsync_pol:1;
	unsigned Hsync_pol:1;
	unsigned pixclk_pol:1;
	unsigned data_pol:1;
	unsigned sens_clksrc:1;
};

struct csi_config_t {
	/* control reg 1 */
	unsigned int swap16_en:1;
	unsigned int ext_vsync:1;
	unsigned int eof_int_en:1;
	unsigned int prp_if_en:1;
	unsigned int ccir_mode:1;
	unsigned int cof_int_en:1;
	unsigned int sf_or_inten:1;
	unsigned int rf_or_inten:1;
	unsigned int sff_dma_done_inten:1;
	unsigned int statff_inten:1;
	unsigned int fb2_dma_done_inten:1;
	unsigned int fb1_dma_done_inten:1;
	unsigned int rxff_inten:1;
	unsigned int sof_pol:1;
	unsigned int sof_inten:1;
	unsigned int mclkdiv:4;
	unsigned int hsync_pol:1;
	unsigned int ccir_en:1;
	unsigned int mclken:1;
	unsigned int fcc:1;
	unsigned int pack_dir:1;
	unsigned int gclk_mode:1;
	unsigned int inv_data:1;
	unsigned int inv_pclk:1;
	unsigned int redge:1;
	unsigned int pixel_bit:1;

	/* control reg 3 */
	unsigned int frmcnt:16;
	unsigned int frame_reset:1;
	unsigned int dma_reflash_rff:1;
	unsigned int dma_reflash_sff:1;
	unsigned int dma_req_en_rff:1;
	unsigned int dma_req_en_sff:1;
	unsigned int statff_level:3;
	unsigned int hresp_err_en:1;
	unsigned int rxff_level:3;
	unsigned int two_8bit_sensor:1;
	unsigned int zero_pack_en:1;
	unsigned int ecc_int_en:1;
	unsigned int ecc_auto_en:1;
	/* fifo counter */
	unsigned int rxcnt;
};

/*
 * Basic structures
 */
struct mx6s_fmt {
	char  name[32];
	u32   fourcc;		/* v4l2 format id */
	u32   pixelformat;
	u32   mbus_code;
	int   bpp;
};

static struct mx6s_fmt formats[] = {
	{
		.name		= "UYVY-16",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp		= 2,
	}, {
		.name		= "YUYV-16",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp		= 2,
	}, {
		.name		= "YUV32 (X-Y-U-V)",
		.fourcc		= V4L2_PIX_FMT_YUV32,
		.pixelformat	= V4L2_PIX_FMT_YUV32,
		.mbus_code	= MEDIA_BUS_FMT_AYUV8_1X32,
		.bpp		= 4,
	}, {
		.name		= "RAWRGB8 (SBGGR8)",
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.pixelformat	= V4L2_PIX_FMT_SBGGR8,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.bpp		= 1,
	}
};

struct mx6s_buf_internal {
	struct list_head	queue;
	int					bufnum;
	bool				discard;
};

/* buffer for one video frame */
struct mx6s_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer			vb;
	struct mx6s_buf_internal	internal;
};

struct mx6s_csi_mux {
	struct regmap *gpr;
	u8 req_gpr;
	u8 req_bit;
};

struct mx6s_csi_dev {
	struct device		*dev;
	struct video_device *vdev;
	struct v4l2_subdev	*sd;
	struct v4l2_device	v4l2_dev;

	struct vb2_queue			vb2_vidq;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct v4l2_ctrl_handler	ctrl_handler;

	struct mutex		lock;
	spinlock_t			slock;

	/* clock */
	struct clk	*clk_disp_axi;
	struct clk	*clk_disp_dcic;
	struct clk	*clk_csi_mclk;

	void __iomem *regbase;
	int irq;

	u32	 type;
	u32 bytesperline;
	v4l2_std_id std;
	struct mx6s_fmt		*fmt;
	struct v4l2_pix_format pix;
	u32 mbus_code;

	unsigned int frame_count;

	struct list_head	capture;
	struct list_head	active_bufs;
	struct list_head	discard;

	void						*discard_buffer;
	dma_addr_t					discard_buffer_dma;
	size_t						discard_size;
	struct mx6s_buf_internal	buf_discard[2];

	struct v4l2_async_subdev	asd;
	struct v4l2_async_notifier	subdev_notifier;
	struct v4l2_async_subdev	*async_subdevs[2];

	bool csi_mux_mipi;
	struct mx6s_csi_mux csi_mux;
};

static inline int csi_read(struct mx6s_csi_dev *csi, unsigned int offset)
{
	return __raw_readl(csi->regbase + offset);
}
static inline void csi_write(struct mx6s_csi_dev *csi, unsigned int value,
			     unsigned int offset)
{
	__raw_writel(value, csi->regbase + offset);
}

static inline struct mx6s_csi_dev
				*notifier_to_mx6s_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct mx6s_csi_dev, subdev_notifier);
}

struct mx6s_fmt *format_by_fourcc(int fourcc)
{
	int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == fourcc)
			return formats + i;
	}

	pr_err("unknown pixelformat:'%4.4s'\n", (char *)&fourcc);
	return NULL;
}

struct mx6s_fmt *format_by_mbus(u32 code)
{
	int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].mbus_code == code)
			return formats + i;
	}

	pr_err("unknown mbus:0x%x\n", code);
	return NULL;
}

static struct mx6s_buffer *mx6s_ibuf_to_buf(struct mx6s_buf_internal *int_buf)
{
	return container_of(int_buf, struct mx6s_buffer, internal);
}

void csi_clk_enable(struct mx6s_csi_dev *csi_dev)
{
	clk_prepare_enable(csi_dev->clk_disp_axi);
	clk_prepare_enable(csi_dev->clk_disp_dcic);
	clk_prepare_enable(csi_dev->clk_csi_mclk);
}

void csi_clk_disable(struct mx6s_csi_dev *csi_dev)
{
	clk_disable_unprepare(csi_dev->clk_csi_mclk);
	clk_disable_unprepare(csi_dev->clk_disp_dcic);
	clk_disable_unprepare(csi_dev->clk_disp_axi);
}

static void csihw_reset(struct mx6s_csi_dev *csi_dev)
{
	__raw_writel(__raw_readl(csi_dev->regbase + CSI_CSICR3)
			| BIT_FRMCNT_RST,
			csi_dev->regbase + CSI_CSICR3);

	__raw_writel(CSICR1_RESET_VAL, csi_dev->regbase + CSI_CSICR1);
	__raw_writel(CSICR2_RESET_VAL, csi_dev->regbase + CSI_CSICR2);
	__raw_writel(CSICR3_RESET_VAL, csi_dev->regbase + CSI_CSICR3);
}

static void csisw_reset(struct mx6s_csi_dev *csi_dev)
{
	int cr1, cr3, cr18, isr;

	/* Disable csi  */
	cr18 = csi_read(csi_dev, CSI_CSICR18);
	cr18 &= ~BIT_CSI_ENABLE;
	csi_write(csi_dev, cr18, CSI_CSICR18);

	/* Clear RX FIFO */
	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 & ~BIT_FCC, CSI_CSICR1);
	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);

	/* DMA reflash */
	cr3 = csi_read(csi_dev, CSI_CSICR3);
	cr3 |= BIT_DMA_REFLASH_RFF | BIT_FRMCNT_RST;
	csi_write(csi_dev, cr3, CSI_CSICR3);

	msleep(2);

	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 | BIT_FCC, CSI_CSICR1);

	isr = csi_read(csi_dev, CSI_CSISR);
	csi_write(csi_dev, isr, CSI_CSISR);

	/* Ensable csi  */
	cr18 |= BIT_CSI_ENABLE;
	csi_write(csi_dev, cr18, CSI_CSICR18);
}

/*!
 * csi_init_interface
 *    Init csi interface
 */
static void csi_init_interface(struct mx6s_csi_dev *csi_dev)
{
	unsigned int val = 0;
	unsigned int imag_para;

	val |= BIT_SOF_POL;
	val |= BIT_REDGE;
	val |= BIT_GCLK_MODE;
	val |= BIT_HSYNC_POL;
	val |= BIT_FCC;
	val |= 1 << SHIFT_MCLKDIV;
	val |= BIT_MCLKEN;
	__raw_writel(val, csi_dev->regbase + CSI_CSICR1);

	imag_para = (640 << 16) | 960;
	__raw_writel(imag_para, csi_dev->regbase + CSI_CSIIMAG_PARA);

	val = BIT_DMA_REFLASH_RFF;
	__raw_writel(val, csi_dev->regbase + CSI_CSICR3);
}

static void csi_enable_int(struct mx6s_csi_dev *csi_dev, int arg)
{
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	cr1 |= BIT_SOF_INTEN;
	cr1 |= BIT_RFF_OR_INT;
	if (arg == 1) {
		/* still capture needs DMA intterrupt */
		cr1 |= BIT_FB1_DMA_DONE_INTEN;
		cr1 |= BIT_FB2_DMA_DONE_INTEN;
	}
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

static void csi_disable_int(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	cr1 &= ~BIT_SOF_INTEN;
	cr1 &= ~BIT_RFF_OR_INT;
	cr1 &= ~BIT_FB1_DMA_DONE_INTEN;
	cr1 &= ~BIT_FB2_DMA_DONE_INTEN;
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

static void csi_enable(struct mx6s_csi_dev *csi_dev, int arg)
{
	unsigned long cr = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (arg == 1)
		cr |= BIT_CSI_ENABLE;
	else
		cr &= ~BIT_CSI_ENABLE;
	__raw_writel(cr, csi_dev->regbase + CSI_CSICR18);
}

static void csi_buf_stride_set(struct mx6s_csi_dev *csi_dev, u32 stride)
{
	__raw_writel(stride, csi_dev->regbase + CSI_CSIFBUF_PARA);
}

static void csi_deinterlace_enable(struct mx6s_csi_dev *csi_dev, bool enable)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (enable == true)
		cr18 |= BIT_DEINTERLACE_EN;
	else
		cr18 &= ~BIT_DEINTERLACE_EN;

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
}

static void csi_deinterlace_mode(struct mx6s_csi_dev *csi_dev, int mode)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (mode == V4L2_STD_NTSC)
		cr18 |= BIT_NTSC_EN;
	else
		cr18 &= ~BIT_NTSC_EN;

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
}

static void csi_tvdec_enable(struct mx6s_csi_dev *csi_dev, bool enable)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	if (enable == true) {
		cr18 |= (BIT_TVDECODER_IN_EN |
				BIT_BASEADDR_SWITCH_EN |
				BIT_BASEADDR_SWITCH_SEL |
				BIT_BASEADDR_CHG_ERR_EN);
		cr1 |= BIT_CCIR_MODE;
		cr1 &= ~(BIT_SOF_POL | BIT_REDGE);
	} else {
		cr18 &= ~(BIT_TVDECODER_IN_EN |
				BIT_BASEADDR_SWITCH_EN |
				BIT_BASEADDR_SWITCH_SEL |
				BIT_BASEADDR_CHG_ERR_EN);
		cr1 &= ~BIT_CCIR_MODE;
		cr1 |= BIT_SOF_POL | BIT_REDGE;
	}

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

static void csi_dmareq_rff_enable(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr3 = __raw_readl(csi_dev->regbase + CSI_CSICR3);
	unsigned long cr2 = __raw_readl(csi_dev->regbase + CSI_CSICR2);

	/* Burst Type of DMA Transfer from RxFIFO. INCR16 */
	cr2 |= 0xC0000000;

	cr3 |= BIT_DMA_REQ_EN_RFF;
	cr3 |= BIT_HRESP_ERR_EN;
	cr3 &= ~BIT_RXFF_LEVEL;
	cr3 |= 0x2 << 4;

	__raw_writel(cr3, csi_dev->regbase + CSI_CSICR3);
	__raw_writel(cr2, csi_dev->regbase + CSI_CSICR2);
}

static void csi_dmareq_rff_disable(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr3 = __raw_readl(csi_dev->regbase + CSI_CSICR3);

	cr3 &= ~BIT_DMA_REQ_EN_RFF;
	cr3 &= ~BIT_HRESP_ERR_EN;
	__raw_writel(cr3, csi_dev->regbase + CSI_CSICR3);
}

static void csi_set_imagpara(struct mx6s_csi_dev *csi,
					int width, int height)
{
	int imag_para = 0;
	unsigned long cr3 = __raw_readl(csi->regbase + CSI_CSICR3);

	imag_para = (width << 16) | height;
	__raw_writel(imag_para, csi->regbase + CSI_CSIIMAG_PARA);

	/* reflash the embeded DMA controller */
	__raw_writel(cr3 | BIT_DMA_REFLASH_RFF, csi->regbase + CSI_CSICR3);
}

/*
 *  Videobuf operations
 */
static int mx6s_videobuf_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *count, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);

	dev_dbg(csi_dev->dev, "count=%d, size=%d\n", *count, sizes[0]);

	/* TODO: support for VIDIOC_CREATE_BUFS not ready */
	if (fmt != NULL)
		return -ENOTTY;

	alloc_ctxs[0] = csi_dev->alloc_ctx;

	sizes[0] = csi_dev->pix.sizeimage;

	pr_debug("size=%d\n", sizes[0]);
	if (0 == *count)
		*count = 32;
	if (!*num_planes &&
	    sizes[0] * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / sizes[0];

	*num_planes = 1;

	return 0;
}

static int mx6s_videobuf_prepare(struct vb2_buffer *vb)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vb->vb2_queue);
	int ret = 0;

	dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	if (vb2_plane_vaddr(vb, 0))
		memset((void *)vb2_plane_vaddr(vb, 0),
		       0xaa, vb2_get_plane_payload(vb, 0));
#endif

	vb2_set_plane_payload(vb, 0, csi_dev->pix.sizeimage);
	if (vb2_plane_vaddr(vb, 0) &&
	    vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		ret = -EINVAL;
		goto out;
	}

	return 0;

out:
	return ret;
}

static void mx6s_videobuf_queue(struct vb2_buffer *vb)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct mx6s_buffer *buf = container_of(vb, struct mx6s_buffer, vb);
	unsigned long flags;

	dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	spin_lock_irqsave(&csi_dev->slock, flags);

	list_add_tail(&buf->internal.queue, &csi_dev->capture);

	spin_unlock_irqrestore(&csi_dev->slock, flags);
}

static void mx6s_update_csi_buf(struct mx6s_csi_dev *csi_dev,
				 unsigned long phys, int bufnum)
{
	if (bufnum == 1)
		csi_write(csi_dev, phys, CSI_CSIDMASA_FB2);
	else
		csi_write(csi_dev, phys, CSI_CSIDMASA_FB1);
}

static void mx6s_csi_init(struct mx6s_csi_dev *csi_dev)
{
	csi_clk_enable(csi_dev);
	csihw_reset(csi_dev);
	csi_init_interface(csi_dev);
	csi_dmareq_rff_disable(csi_dev);
}

static void mx6s_csi_deinit(struct mx6s_csi_dev *csi_dev)
{
	csihw_reset(csi_dev);
	csi_init_interface(csi_dev);
	csi_dmareq_rff_disable(csi_dev);
	csi_clk_disable(csi_dev);
}

static int mx6s_csi_enable(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;
	unsigned long flags;
	unsigned long val;
	int timeout, timeout2;

	csisw_reset(csi_dev);

	if (pix->field == V4L2_FIELD_INTERLACED)
		csi_tvdec_enable(csi_dev, true);

	/* For mipi csi input only */
	if (csi_dev->csi_mux_mipi == true) {
		csi_dmareq_rff_enable(csi_dev);
		csi_enable_int(csi_dev, 1);
		csi_enable(csi_dev, 1);
		return 0;
	}

	local_irq_save(flags);
	for (timeout = 10000000; timeout > 0; timeout--) {
		if (csi_read(csi_dev, CSI_CSISR) & BIT_SOF_INT) {
			val = csi_read(csi_dev, CSI_CSICR3);
			csi_write(csi_dev, val | BIT_DMA_REFLASH_RFF,
					CSI_CSICR3);
			/* Wait DMA reflash done */
			for (timeout2 = 1000000; timeout2 > 0; timeout2--) {
				if (csi_read(csi_dev, CSI_CSICR3) &
					BIT_DMA_REFLASH_RFF)
					cpu_relax();
				else
					break;
			}
			if (timeout2 <= 0) {
				pr_err("timeout when wait for reflash done.\n");
				local_irq_restore(flags);
				return -ETIME;
			}
			/* For imx6sl csi, DMA FIFO will auto start when sensor ready to work,
			 * so DMA should enable right after FIFO reset, otherwise dma will lost data
			 * and image will split.
			 */
			csi_dmareq_rff_enable(csi_dev);
			csi_enable_int(csi_dev, 1);
			csi_enable(csi_dev, 1);
			break;
		} else
			cpu_relax();
	}
	if (timeout <= 0) {
		pr_err("timeout when wait for SOF\n");
		local_irq_restore(flags);
		return -ETIME;
	}
	local_irq_restore(flags);

	return 0;
}

static void mx6s_csi_disable(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;

	csi_dmareq_rff_disable(csi_dev);
	csi_disable_int(csi_dev);

	/* set CSI_CSIDMASA_FB1 and CSI_CSIDMASA_FB2 to default value */
	csi_write(csi_dev, 0, CSI_CSIDMASA_FB1);
	csi_write(csi_dev, 0, CSI_CSIDMASA_FB2);

	csi_buf_stride_set(csi_dev, 0);

	if (pix->field == V4L2_FIELD_INTERLACED) {
		csi_deinterlace_enable(csi_dev, false);
		csi_tvdec_enable(csi_dev, false);
	}

	csi_enable(csi_dev, 0);
}

static int mx6s_configure_csi(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;
	u32 cr1, cr18;
	u32 width;

	if (pix->field == V4L2_FIELD_INTERLACED) {
		csi_deinterlace_enable(csi_dev, true);
		csi_buf_stride_set(csi_dev, csi_dev->pix.width);
		csi_deinterlace_mode(csi_dev, csi_dev->std);
	} else {
		csi_deinterlace_enable(csi_dev, false);
		csi_buf_stride_set(csi_dev, 0);
	}

	switch (csi_dev->fmt->pixelformat) {
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_SBGGR8:
		width = pix->width;
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
		if (csi_dev->csi_mux_mipi == true)
			width = pix->width;
		else
			/* For parallel 8-bit sensor input */
			width = pix->width * 2;
		break;
	default:
		pr_debug("   case not supported\n");
		return -EINVAL;
	}
	csi_set_imagpara(csi_dev, width, pix->height);

	if (csi_dev->csi_mux_mipi == true) {
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		cr1 &= ~BIT_GCLK_MODE;
		csi_write(csi_dev, cr1, CSI_CSICR1);

		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= BIT_MIPI_DATA_FORMAT_MASK;
		cr18 |= BIT_DATA_FROM_MIPI;

		switch (csi_dev->fmt->pixelformat) {
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUYV:
			cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B;
			break;
		case V4L2_PIX_FMT_SBGGR8:
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW8;
			break;
		default:
			pr_debug("   fmt not supported\n");
			return -EINVAL;
		}

		csi_write(csi_dev, cr18, CSI_CSICR18);
	}
	return 0;
}

static int mx6s_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);
	struct vb2_buffer *vb;
	struct mx6s_buffer *buf;
	unsigned long phys;
	unsigned long flags;

	if (count < 2)
		return -ENOBUFS;

	/*
	 * I didn't manage to properly enable/disable
	 * a per frame basis during running transfers,
	 * thus we allocate a buffer here and use it to
	 * discard frames when no buffer is available.
	 * Feel free to work on this ;)
	 */
	csi_dev->discard_size = csi_dev->pix.sizeimage;
	csi_dev->discard_buffer = dma_alloc_coherent(csi_dev->v4l2_dev.dev,
					PAGE_ALIGN(csi_dev->discard_size),
					&csi_dev->discard_buffer_dma,
					GFP_DMA | GFP_KERNEL);
	if (!csi_dev->discard_buffer)
		return -ENOMEM;

	spin_lock_irqsave(&csi_dev->slock, flags);

	csi_dev->buf_discard[0].discard = true;
	list_add_tail(&csi_dev->buf_discard[0].queue,
		      &csi_dev->discard);

	csi_dev->buf_discard[1].discard = true;
	list_add_tail(&csi_dev->buf_discard[1].queue,
		      &csi_dev->discard);

	/* csi buf 0 */
	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);
	buf->internal.bufnum = 0;
	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);

	mx6s_update_csi_buf(csi_dev, phys, buf->internal.bufnum);
	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	/* csi buf 1 */
	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);
	buf->internal.bufnum = 1;
	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);
	mx6s_update_csi_buf(csi_dev, phys, buf->internal.bufnum);
	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	spin_unlock_irqrestore(&csi_dev->slock, flags);

	return mx6s_csi_enable(csi_dev);
}

static void mx6s_stop_streaming(struct vb2_queue *vq)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);
	unsigned long flags;
	struct mx6s_buffer *buf, *tmp;
	void *b;

	mx6s_csi_disable(csi_dev);

	spin_lock_irqsave(&csi_dev->slock, flags);

	list_for_each_entry_safe(buf, tmp,
				&csi_dev->active_bufs, internal.queue) {
		list_del_init(&buf->internal.queue);
		if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	list_for_each_entry_safe(buf, tmp,
				&csi_dev->capture, internal.queue) {
		list_del_init(&buf->internal.queue);
		if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	INIT_LIST_HEAD(&csi_dev->capture);
	INIT_LIST_HEAD(&csi_dev->active_bufs);
	INIT_LIST_HEAD(&csi_dev->discard);

	b = csi_dev->discard_buffer;
	csi_dev->discard_buffer = NULL;

	spin_unlock_irqrestore(&csi_dev->slock, flags);

	dma_free_coherent(csi_dev->v4l2_dev.dev,
				csi_dev->discard_size, b,
				csi_dev->discard_buffer_dma);
}

static struct vb2_ops mx6s_videobuf_ops = {
	.queue_setup     = mx6s_videobuf_setup,
	.buf_prepare     = mx6s_videobuf_prepare,
	.buf_queue       = mx6s_videobuf_queue,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.start_streaming = mx6s_start_streaming,
	.stop_streaming	 = mx6s_stop_streaming,
};

static void mx6s_csi_frame_done(struct mx6s_csi_dev *csi_dev,
		int bufnum, bool err)
{
	struct mx6s_buf_internal *ibuf;
	struct mx6s_buffer *buf;
	struct vb2_buffer *vb;
	unsigned long phys;

	ibuf = list_first_entry(&csi_dev->active_bufs, struct mx6s_buf_internal,
			       queue);

	if (ibuf->discard) {
		/*
		 * Discard buffer must not be returned to user space.
		 * Just return it to the discard queue.
		 */
		list_move_tail(csi_dev->active_bufs.next, &csi_dev->discard);
	} else {
		buf = mx6s_ibuf_to_buf(ibuf);

		vb = &buf->vb;
		phys = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (bufnum == 1) {
			if (csi_read(csi_dev, CSI_CSIDMASA_FB2) != phys) {
				dev_err(csi_dev->dev, "%lx != %x\n", phys,
					csi_read(csi_dev, CSI_CSIDMASA_FB2));
			}
		} else {
			if (csi_read(csi_dev, CSI_CSIDMASA_FB1) != phys) {
				dev_err(csi_dev->dev, "%lx != %x\n", phys,
					csi_read(csi_dev, CSI_CSIDMASA_FB1));
			}
		}
		dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__, vb,
				vb2_plane_vaddr(vb, 0),
				vb2_get_plane_payload(vb, 0));

		list_del_init(&buf->internal.queue);
		v4l2_get_timestamp(&vb->v4l2_buf.timestamp);
		vb->v4l2_buf.sequence = csi_dev->frame_count;
		if (err)
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		else
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}

	csi_dev->frame_count++;

	/* Config discard buffer to active_bufs */
	if (list_empty(&csi_dev->capture)) {
		if (list_empty(&csi_dev->discard)) {
			dev_warn(csi_dev->dev,
					"%s: trying to access empty discard list\n",
					__func__);
			return;
		}

		ibuf = list_first_entry(&csi_dev->discard,
					struct mx6s_buf_internal, queue);
		ibuf->bufnum = bufnum;

		list_move_tail(csi_dev->discard.next, &csi_dev->active_bufs);

		mx6s_update_csi_buf(csi_dev,
					csi_dev->discard_buffer_dma, bufnum);
		return;
	}

	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);

	buf->internal.bufnum = bufnum;

	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);
	mx6s_update_csi_buf(csi_dev, phys, bufnum);
}

static irqreturn_t mx6s_csi_irq_handler(int irq, void *data)
{
	struct mx6s_csi_dev *csi_dev =  data;
	unsigned long status;
	u32 cr1, cr3, cr18;

	spin_lock(&csi_dev->slock);

	status = csi_read(csi_dev, CSI_CSISR);
	csi_write(csi_dev, status, CSI_CSISR);

	if (list_empty(&csi_dev->active_bufs)) {
		dev_warn(csi_dev->dev,
				"%s: called while active list is empty\n",
				__func__);

		spin_unlock(&csi_dev->slock);
		return IRQ_HANDLED;
	}

	if (status & BIT_RFF_OR_INT)
		dev_warn(csi_dev->dev, "%s Rx fifo overflow\n", __func__);
	if (status & BIT_HRESP_ERR_INT)
		dev_warn(csi_dev->dev, "%s Hresponse error detected\n",
			__func__);

	if (status & (BIT_RFF_OR_INT|BIT_HRESP_ERR_INT)) {
		/* software reset */

		/* Disable csi  */
		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= ~BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		/* Clear RX FIFO */
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 & ~BIT_FCC, CSI_CSICR1);
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);

		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 | BIT_FCC, CSI_CSICR1);

		/* DMA reflash */
		cr3 = csi_read(csi_dev, CSI_CSICR3);
		cr3 |= BIT_DMA_REFLASH_RFF;
		csi_write(csi_dev, cr3, CSI_CSICR3);

		/* Ensable csi  */
		cr18 |= BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);
	}

	if (status & BIT_ADDR_CH_ERR_INT) {
		/* Disable csi  */
		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= ~BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		/* DMA reflash */
		cr3 = csi_read(csi_dev, CSI_CSICR3);
		cr3 |= BIT_DMA_REFLASH_RFF;
		csi_write(csi_dev, cr3, CSI_CSICR3);

		/* Ensable csi  */
		cr18 |= BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		pr_debug("base address switching Change Err.\n");
	}

	if ((status & BIT_DMA_TSF_DONE_FB1) &&
		(status & BIT_DMA_TSF_DONE_FB2)) {
		/* For both FB1 and FB2 interrupter bits set case,
		 * CSI DMA is work in one of FB1 and FB2 buffer,
		 * but software can not know the state.
		 * Skip it to avoid base address updated
		 * when csi work in field0 and field1 will write to
		 * new base address.
		 * PDM TKT230775 */
		pr_debug("Skip two frames\n");
	} else if (status & BIT_DMA_TSF_DONE_FB1) {
		mx6s_csi_frame_done(csi_dev, 0, false);
	} else if (status & BIT_DMA_TSF_DONE_FB2) {
		mx6s_csi_frame_done(csi_dev, 1, false);
	}

	spin_unlock(&csi_dev->slock);

	return IRQ_HANDLED;
}

/*
 * File operations for the device
 */
static int mx6s_csi_open(struct file *file)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct vb2_queue *q = &csi_dev->vb2_vidq;
	int ret = 0;

	file->private_data = csi_dev;

	if (mutex_lock_interruptible(&csi_dev->lock))
		return -ERESTARTSYS;

	csi_dev->alloc_ctx = vb2_dma_contig_init_ctx(csi_dev->dev);
	if (IS_ERR(csi_dev->alloc_ctx))
		goto unlock;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = csi_dev;
	q->ops = &mx6s_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct mx6s_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &csi_dev->lock;

	ret = vb2_queue_init(q);
	if (ret < 0)
		goto eallocctx;

	pm_runtime_get_sync(csi_dev->dev);

	request_bus_freq(BUS_FREQ_HIGH);

	v4l2_subdev_call(sd, core, s_power, 1);
	mx6s_csi_init(csi_dev);

	mutex_unlock(&csi_dev->lock);

	return ret;
eallocctx:
	vb2_dma_contig_cleanup_ctx(csi_dev->alloc_ctx);
unlock:
	mutex_unlock(&csi_dev->lock);
	return ret;
}

static int mx6s_csi_close(struct file *file)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	mutex_lock(&csi_dev->lock);

	vb2_queue_release(&csi_dev->vb2_vidq);

	mx6s_csi_deinit(csi_dev);
	v4l2_subdev_call(sd, core, s_power, 0);

	vb2_dma_contig_cleanup_ctx(csi_dev->alloc_ctx);
	mutex_unlock(&csi_dev->lock);

	file->private_data = NULL;

	release_bus_freq(BUS_FREQ_HIGH);

	pm_runtime_put_sync_suspend(csi_dev->dev);
	return 0;
}

static ssize_t mx6s_csi_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	dev_dbg(csi_dev->dev, "read called, buf %p\n", buf);

	mutex_lock(&csi_dev->lock);
	ret = vb2_read(&csi_dev->vb2_vidq, buf, count, ppos,
				file->f_flags & O_NONBLOCK);
	mutex_unlock(&csi_dev->lock);
	return ret;
}

static int mx6s_csi_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&csi_dev->lock))
		return -ERESTARTSYS;
	ret = vb2_mmap(&csi_dev->vb2_vidq, vma);
	mutex_unlock(&csi_dev->lock);

	pr_debug("vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static struct v4l2_file_operations mx6s_csi_fops = {
	.owner		= THIS_MODULE,
	.open		= mx6s_csi_open,
	.release	= mx6s_csi_close,
	.read		= mx6s_csi_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2, /* V4L2 ioctl handler */
	.mmap		= mx6s_csi_mmap,
};

/*
 * Video node IOCTLs
 */
static int mx6s_vidioc_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	/* default is camera */
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(inp->name, "Camera");

	return 0;
}

static int mx6s_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int mx6s_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

static int mx6s_vidioc_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, querystd, a);
}

static int mx6s_vidioc_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, s_std, a);
}

static int mx6s_vidioc_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, g_std, a);
}

static int mx6s_vidioc_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_reqbufs(&csi_dev->vb2_vidq, p);
}

static int mx6s_vidioc_querybuf(struct file *file, void *priv,
			       struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	WARN_ON(priv != file->private_data);

	ret = vb2_querybuf(&csi_dev->vb2_vidq, p);

	if (!ret) {
		/* return physical address */
		struct vb2_buffer *vb = csi_dev->vb2_vidq.bufs[p->index];
		if (p->flags & V4L2_BUF_FLAG_MAPPED)
			p->m.offset = vb2_dma_contig_plane_dma_addr(vb, 0);
	}
	return ret;
}

static int mx6s_vidioc_qbuf(struct file *file, void *priv,
			   struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_qbuf(&csi_dev->vb2_vidq, p);
}

static int mx6s_vidioc_dqbuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_dqbuf(&csi_dev->vb2_vidq, p, file->f_flags & O_NONBLOCK);
}

static int mx6s_vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
				       struct v4l2_fmtdesc *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	u32 code;
	struct mx6s_fmt *fmt;
	int ret;

	int index = f->index;

	WARN_ON(priv != file->private_data);

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, index, &code);
	if (ret < 0) {
		/* no more formats */
		dev_dbg(csi_dev->dev, "No more fmt\n");
		return -EINVAL;
	}

	fmt = format_by_mbus(code);
	if (!fmt) {
		dev_err(csi_dev->dev, "mbus (0x%08x) invalid.\n", code);
		return -EINVAL;
	}

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->pixelformat;

	return 0;
}

static int mx6s_vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct mx6s_fmt *fmt;
	int ret;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (!fmt) {
		dev_err(csi_dev->dev, "Fourcc format (0x%08x) invalid.",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	if (f->fmt.pix.width == 0 || f->fmt.pix.height == 0) {
		dev_err(csi_dev->dev, "width %d, height %d is too small.\n",
			f->fmt.pix.width, f->fmt.pix.height);
		return -EINVAL;
	}

	v4l2_fill_mbus_format(&mbus_fmt, pix, fmt->mbus_code);
	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt);
	v4l2_fill_pix_format(pix, &mbus_fmt);

	if (pix->field != V4L2_FIELD_INTERLACED)
		pix->field = V4L2_FIELD_NONE;

	pix->sizeimage = fmt->bpp * pix->height * pix->width;
	pix->bytesperline = fmt->bpp * pix->width;

	return ret;
}

/*
 * The real work of figuring out a workable format.
 */

static int mx6s_vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	ret = mx6s_vidioc_try_fmt_vid_cap(file, csi_dev, f);
	if (ret < 0)
		return ret;

	csi_dev->fmt           = format_by_fourcc(f->fmt.pix.pixelformat);
	csi_dev->mbus_code     = csi_dev->fmt->mbus_code;
	csi_dev->pix.width     = f->fmt.pix.width;
	csi_dev->pix.height    = f->fmt.pix.height;
	csi_dev->pix.sizeimage = f->fmt.pix.sizeimage;
	csi_dev->pix.field     = f->fmt.pix.field;
	csi_dev->type          = f->type;
	dev_dbg(csi_dev->dev, "set to pixelformat '%4.6s'\n",
			(char *)&csi_dev->fmt->name);

	/* Config csi */
	mx6s_configure_csi(csi_dev);

	return 0;
}

static int mx6s_vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	f->fmt.pix = csi_dev->pix;

	return 0;
}

static int mx6s_vidioc_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	/* cap->name is set by the friendly caller:-> */
	strlcpy(cap->driver, MX6S_CAM_DRV_NAME, sizeof(cap->driver));
	strlcpy(cap->card, MX6S_CAM_DRIVER_DESCRIPTION, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(csi_dev->dev));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int mx6s_vidioc_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	int ret;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = vb2_streamon(&csi_dev->vb2_vidq, i);
	if (!ret)
		v4l2_subdev_call(sd, video, s_stream, 1);

	return ret;
}

static int mx6s_vidioc_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type i)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/*
	 * This calls buf_release from host driver's videobuf_queue_ops for all
	 * remaining buffers. When the last buffer is freed, stop capture
	 */
	vb2_streamoff(&csi_dev->vb2_vidq, i);

	v4l2_subdev_call(sd, video, s_stream, 0);

	return 0;
}

static int mx6s_vidioc_cropcap(struct file *file, void *fh,
			      struct v4l2_cropcap *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	dev_dbg(csi_dev->dev, "VIDIOC_CROPCAP not implemented\n");

	return 0;
}

static int mx6s_vidioc_g_crop(struct file *file, void *priv,
			     struct v4l2_crop *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	dev_dbg(csi_dev->dev, "VIDIOC_G_CROP not implemented\n");

	return 0;
}

static int mx6s_vidioc_s_crop(struct file *file, void *priv,
			     const struct v4l2_crop *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	dev_dbg(csi_dev->dev, "VIDIOC_S_CROP not implemented\n");

	return 0;
}

static int mx6s_vidioc_g_parm(struct file *file, void *priv,
			     struct v4l2_streamparm *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, g_parm, a);
}

static int mx6s_vidioc_s_parm(struct file *file, void *priv,
				struct v4l2_streamparm *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, s_parm, a);
}

static int mx6s_vidioc_enum_framesizes(struct file *file, void *priv,
					 struct v4l2_frmsizeenum *fsize)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct mx6s_fmt *fmt;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = format_by_fourcc(fsize->pixel_format);
	if (fmt->pixelformat != fsize->pixel_format)
		return -EINVAL;
	fse.code = fmt->mbus_code;

	ret = v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	if (fse.min_width == fse.max_width &&
	    fse.min_height == fse.max_height) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
		return 0;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->stepwise.min_width = fse.min_width;
	fsize->stepwise.max_width = fse.max_width;
	fsize->stepwise.min_height = fse.min_height;
	fsize->stepwise.max_height = fse.max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int mx6s_vidioc_enum_frameintervals(struct file *file, void *priv,
		struct v4l2_frmivalenum *interval)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct mx6s_fmt *fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = interval->index,
		.width = interval->width,
		.height = interval->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = format_by_fourcc(interval->pixel_format);
	if (fmt->pixelformat != interval->pixel_format)
		return -EINVAL;
	fie.code = fmt->mbus_code;

	ret = v4l2_subdev_call(sd, pad, enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;
	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete = fie.interval;
	return 0;
}

static const struct v4l2_ioctl_ops mx6s_csi_ioctl_ops = {
	.vidioc_querycap          = mx6s_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = mx6s_vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = mx6s_vidioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = mx6s_vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = mx6s_vidioc_s_fmt_vid_cap,
	.vidioc_cropcap       = mx6s_vidioc_cropcap,
	.vidioc_s_crop        = mx6s_vidioc_s_crop,
	.vidioc_g_crop        = mx6s_vidioc_g_crop,
	.vidioc_reqbufs       = mx6s_vidioc_reqbufs,
	.vidioc_querybuf      = mx6s_vidioc_querybuf,
	.vidioc_qbuf          = mx6s_vidioc_qbuf,
	.vidioc_dqbuf         = mx6s_vidioc_dqbuf,
	.vidioc_g_std         = mx6s_vidioc_g_std,
	.vidioc_s_std         = mx6s_vidioc_s_std,
	.vidioc_querystd      = mx6s_vidioc_querystd,
	.vidioc_enum_input    = mx6s_vidioc_enum_input,
	.vidioc_g_input       = mx6s_vidioc_g_input,
	.vidioc_s_input       = mx6s_vidioc_s_input,
	.vidioc_streamon      = mx6s_vidioc_streamon,
	.vidioc_streamoff     = mx6s_vidioc_streamoff,
	.vidioc_g_parm        = mx6s_vidioc_g_parm,
	.vidioc_s_parm        = mx6s_vidioc_s_parm,
	.vidioc_enum_framesizes = mx6s_vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = mx6s_vidioc_enum_frameintervals,
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_subdev *asd)
{
	struct mx6s_csi_dev *csi_dev = notifier_to_mx6s_dev(notifier);

	/* Find platform data for this sensor subdev */
	if (csi_dev->asd.match.of.node == subdev->dev->of_node)
		csi_dev->sd = subdev;

	if (subdev == NULL)
		return -EINVAL;

	v4l2_info(&csi_dev->v4l2_dev, "Registered sensor subdevice: %s\n",
		  subdev->name);

	return 0;
}

static int mx6s_csi_mux_sel(struct mx6s_csi_dev *csi_dev)
{
	struct device_node *np = csi_dev->dev->of_node;
	struct device_node *node;
	phandle phandle;
	u32 out_val[3];
	int ret;

	ret = of_property_read_u32_array(np, "csi-mux-mipi", out_val, 3);
	if (ret) {
		dev_dbg(csi_dev->dev, "no csi-mux-mipi property found\n");
		csi_dev->csi_mux_mipi = false;
	} else {
		phandle = *out_val;

		node = of_find_node_by_phandle(phandle);
		if (!node) {
			dev_dbg(csi_dev->dev, "not find gpr node by phandle\n");
			ret = PTR_ERR(node);
		}
		csi_dev->csi_mux.gpr = syscon_node_to_regmap(node);
		if (IS_ERR(csi_dev->csi_mux.gpr)) {
			dev_err(csi_dev->dev, "failed to get gpr regmap\n");
			ret = PTR_ERR(csi_dev->csi_mux.gpr);
		}
		of_node_put(node);
		if (ret < 0)
			return ret;

		csi_dev->csi_mux.req_gpr = out_val[1];
		csi_dev->csi_mux.req_bit = out_val[2];

		regmap_update_bits(csi_dev->csi_mux.gpr, csi_dev->csi_mux.req_gpr,
			1 << csi_dev->csi_mux.req_bit, 1 << csi_dev->csi_mux.req_bit);

		csi_dev->csi_mux_mipi = true;
	}
	return ret;
}

static int mx6sx_register_subdevs(struct mx6s_csi_dev *csi_dev)
{
	struct device_node *parent = csi_dev->dev->of_node;
	struct device_node *node, *port, *rem;
	int ret;

	/* Attach sensors linked to csi receivers */
	for_each_available_child_of_node(parent, node) {
		if (of_node_cmp(node->name, "port"))
			continue;

		/* The csi node can have only port subnode. */
		port = of_get_next_child(node, NULL);
		if (!port)
			continue;
		rem = of_graph_get_remote_port_parent(port);
		of_node_put(port);
		if (rem == NULL) {
			v4l2_info(&csi_dev->v4l2_dev,
						"Remote device at %s not found\n",
						port->full_name);
			return -1;
		}

		csi_dev->asd.match_type = V4L2_ASYNC_MATCH_OF;
		csi_dev->asd.match.of.node = rem;
		csi_dev->async_subdevs[0] = &csi_dev->asd;

		of_node_put(rem);
		break;
	}

	csi_dev->subdev_notifier.subdevs = csi_dev->async_subdevs;
	csi_dev->subdev_notifier.num_subdevs = 1;
	csi_dev->subdev_notifier.bound = subdev_notifier_bound;

	ret = v4l2_async_notifier_register(&csi_dev->v4l2_dev,
					&csi_dev->subdev_notifier);
	if (ret)
		dev_err(csi_dev->dev,
					"Error register async notifier regoster\n");

	return ret;
}

static int mx6s_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mx6s_csi_dev *csi_dev;
	struct video_device *vdev;
	struct resource *res;
	int ret = 0;

	dev_dbg(dev, "initialising\n");

	/* Prepare our private structure */
	csi_dev = devm_kzalloc(dev, sizeof(struct mx6s_csi_dev), GFP_ATOMIC);
	if (!csi_dev) {
		dev_err(dev, "Can't allocate private structure\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csi_dev->irq = platform_get_irq(pdev, 0);
	if (res == NULL || csi_dev->irq < 0) {
		dev_err(dev, "Missing platform resources data\n");
		return -ENODEV;
	}

	csi_dev->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(csi_dev->regbase)) {
		dev_err(dev, "Failed platform resources map\n");
		return -ENODEV;
	}

	/* init video dma queues */
	INIT_LIST_HEAD(&csi_dev->capture);
	INIT_LIST_HEAD(&csi_dev->active_bufs);
	INIT_LIST_HEAD(&csi_dev->discard);

	csi_dev->clk_disp_axi = devm_clk_get(dev, "disp-axi");
	if (IS_ERR(csi_dev->clk_disp_axi)) {
		dev_err(dev, "Could not get csi axi clock\n");
		return -ENODEV;
	}

	csi_dev->clk_disp_dcic = devm_clk_get(dev, "disp_dcic");
	if (IS_ERR(csi_dev->clk_disp_dcic)) {
		dev_err(dev, "Could not get disp dcic clock\n");
		return -ENODEV;
	}

	csi_dev->clk_csi_mclk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(csi_dev->clk_csi_mclk)) {
		dev_err(dev, "Could not get csi mclk clock\n");
		return -ENODEV;
	}

	csi_dev->dev = dev;

	mx6s_csi_mux_sel(csi_dev);

	snprintf(csi_dev->v4l2_dev.name,
		 sizeof(csi_dev->v4l2_dev.name), "CSI");

	ret = v4l2_device_register(dev, &csi_dev->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "v4l2_device_register() failed: %d\n", ret);
		return -ENODEV;
	}

	/* initialize locks */
	mutex_init(&csi_dev->lock);
	spin_lock_init(&csi_dev->slock);

	/* Allocate memory for video device */
	vdev = video_device_alloc();
	if (vdev == NULL) {
		ret = -ENOMEM;
		goto err_vdev;
	}

	snprintf(vdev->name, sizeof(vdev->name), "mx6s-csi");

	vdev->v4l2_dev		= &csi_dev->v4l2_dev;
	vdev->fops			= &mx6s_csi_fops;
	vdev->ioctl_ops		= &mx6s_csi_ioctl_ops;
	vdev->release		= video_device_release;
	vdev->lock			= &csi_dev->lock;

	vdev->queue = &csi_dev->vb2_vidq;

	csi_dev->vdev = vdev;

	video_set_drvdata(csi_dev->vdev, csi_dev);
	mutex_lock(&csi_dev->lock);

	ret = video_register_device(csi_dev->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		video_device_release(csi_dev->vdev);
		mutex_unlock(&csi_dev->lock);
		goto err_vdev;
	}

	/* install interrupt handler */
	if (devm_request_irq(dev, csi_dev->irq, mx6s_csi_irq_handler,
				0, "csi", (void *)csi_dev)) {
		mutex_unlock(&csi_dev->lock);
		dev_err(dev, "Request CSI IRQ failed.\n");
		ret = -ENODEV;
		goto err_irq;
	}

	mutex_unlock(&csi_dev->lock);

	ret = mx6sx_register_subdevs(csi_dev);
	if (ret < 0)
		goto err_irq;

	pm_runtime_enable(csi_dev->dev);
	return 0;

err_irq:
	video_unregister_device(csi_dev->vdev);
err_vdev:
	v4l2_device_unregister(&csi_dev->v4l2_dev);
	return ret;
}

static int mx6s_csi_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct mx6s_csi_dev *csi_dev =
				container_of(v4l2_dev, struct mx6s_csi_dev, v4l2_dev);

	v4l2_async_notifier_unregister(&csi_dev->subdev_notifier);

	video_unregister_device(csi_dev->vdev);
	v4l2_device_unregister(&csi_dev->v4l2_dev);

	pm_runtime_disable(csi_dev->dev);
	return 0;
}

static int mx6s_csi_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "csi v4l2 busfreq high release.\n");
	return 0;
}

static int mx6s_csi_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "csi v4l2 busfreq high request.\n");
	return 0;
}

static const struct dev_pm_ops mx6s_csi_pm_ops = {
	SET_RUNTIME_PM_OPS(mx6s_csi_runtime_suspend, mx6s_csi_runtime_resume, NULL)
};

static const struct of_device_id mx6s_csi_dt_ids[] = {
	{ .compatible = "fsl,imx6s-csi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx6s_csi_dt_ids);

static struct platform_driver mx6s_csi_driver = {
	.driver		= {
		.name	= MX6S_CAM_DRV_NAME,
		.of_match_table = of_match_ptr(mx6s_csi_dt_ids),
		.pm = &mx6s_csi_pm_ops,
	},
	.probe	= mx6s_csi_probe,
	.remove	= mx6s_csi_remove,
};

module_platform_driver(mx6s_csi_driver);

MODULE_DESCRIPTION("i.MX6Sx SoC Camera Host driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
MODULE_VERSION(MX6S_CAM_VERSION);
