// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "ipu3-css.h"
#include "ipu3-css-fw.h"
#include "ipu3-css-params.h"
#include "ipu3-dmamap.h"
#include "ipu3-tables.h"

/* IRQ configuration */
#define IMGU_IRQCTRL_IRQ_MASK	(IMGU_IRQCTRL_IRQ_SP1 | \
				 IMGU_IRQCTRL_IRQ_SP2 | \
				 IMGU_IRQCTRL_IRQ_SW_PIN(0) | \
				 IMGU_IRQCTRL_IRQ_SW_PIN(1))

#define IPU3_CSS_FORMAT_BPP_DEN	50	/* Denominator */

/* Some sane limits for resolutions */
#define IPU3_CSS_MIN_RES	32
#define IPU3_CSS_MAX_H		3136
#define IPU3_CSS_MAX_W		4224

/* minimal envelope size(GDC in - out) should be 4 */
#define MIN_ENVELOPE            4

/*
 * pre-allocated buffer size for CSS ABI, auxiliary frames
 * after BDS and before GDC. Those values should be tuned
 * to big enough to avoid buffer re-allocation when
 * streaming to lower streaming latency.
 */
#define CSS_ABI_SIZE    136
#define CSS_BDS_SIZE    (4480 * 3200 * 3)
#define CSS_GDC_SIZE    (4224 * 3200 * 12 / 8)

#define IPU3_CSS_QUEUE_TO_FLAGS(q)	(1 << (q))
#define IPU3_CSS_FORMAT_FL_IN		\
			IPU3_CSS_QUEUE_TO_FLAGS(IPU3_CSS_QUEUE_IN)
#define IPU3_CSS_FORMAT_FL_OUT		\
			IPU3_CSS_QUEUE_TO_FLAGS(IPU3_CSS_QUEUE_OUT)
#define IPU3_CSS_FORMAT_FL_VF		\
			IPU3_CSS_QUEUE_TO_FLAGS(IPU3_CSS_QUEUE_VF)

/* Formats supported by IPU3 Camera Sub System */
static const struct imgu_css_format imgu_css_formats[] = {
	{
		.pixelformat = V4L2_PIX_FMT_NV12,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.frame_format = IMGU_ABI_FRAME_FORMAT_NV12,
		.osys_format = IMGU_ABI_OSYS_FORMAT_NV12,
		.osys_tiling = IMGU_ABI_OSYS_TILING_NONE,
		.bytesperpixel_num = 1 * IPU3_CSS_FORMAT_BPP_DEN,
		.chroma_decim = 4,
		.width_align = IPU3_UAPI_ISP_VEC_ELEMS,
		.flags = IPU3_CSS_FORMAT_FL_OUT | IPU3_CSS_FORMAT_FL_VF,
	}, {
		/* Each 32 bytes contains 25 10-bit pixels */
		.pixelformat = V4L2_PIX_FMT_IPU3_SBGGR10,
		.colorspace = V4L2_COLORSPACE_RAW,
		.frame_format = IMGU_ABI_FRAME_FORMAT_RAW_PACKED,
		.bayer_order = IMGU_ABI_BAYER_ORDER_BGGR,
		.bit_depth = 10,
		.bytesperpixel_num = 64,
		.width_align = 2 * IPU3_UAPI_ISP_VEC_ELEMS,
		.flags = IPU3_CSS_FORMAT_FL_IN,
	}, {
		.pixelformat = V4L2_PIX_FMT_IPU3_SGBRG10,
		.colorspace = V4L2_COLORSPACE_RAW,
		.frame_format = IMGU_ABI_FRAME_FORMAT_RAW_PACKED,
		.bayer_order = IMGU_ABI_BAYER_ORDER_GBRG,
		.bit_depth = 10,
		.bytesperpixel_num = 64,
		.width_align = 2 * IPU3_UAPI_ISP_VEC_ELEMS,
		.flags = IPU3_CSS_FORMAT_FL_IN,
	}, {
		.pixelformat = V4L2_PIX_FMT_IPU3_SGRBG10,
		.colorspace = V4L2_COLORSPACE_RAW,
		.frame_format = IMGU_ABI_FRAME_FORMAT_RAW_PACKED,
		.bayer_order = IMGU_ABI_BAYER_ORDER_GRBG,
		.bit_depth = 10,
		.bytesperpixel_num = 64,
		.width_align = 2 * IPU3_UAPI_ISP_VEC_ELEMS,
		.flags = IPU3_CSS_FORMAT_FL_IN,
	}, {
		.pixelformat = V4L2_PIX_FMT_IPU3_SRGGB10,
		.colorspace = V4L2_COLORSPACE_RAW,
		.frame_format = IMGU_ABI_FRAME_FORMAT_RAW_PACKED,
		.bayer_order = IMGU_ABI_BAYER_ORDER_RGGB,
		.bit_depth = 10,
		.bytesperpixel_num = 64,
		.width_align = 2 * IPU3_UAPI_ISP_VEC_ELEMS,
		.flags = IPU3_CSS_FORMAT_FL_IN,
	},
};

static const struct {
	enum imgu_abi_queue_id qid;
	size_t ptr_ofs;
} imgu_css_queues[IPU3_CSS_QUEUES] = {
	[IPU3_CSS_QUEUE_IN] = {
		IMGU_ABI_QUEUE_C_ID,
		offsetof(struct imgu_abi_buffer, payload.frame.frame_data)
	},
	[IPU3_CSS_QUEUE_OUT] = {
		IMGU_ABI_QUEUE_D_ID,
		offsetof(struct imgu_abi_buffer, payload.frame.frame_data)
	},
	[IPU3_CSS_QUEUE_VF] = {
		IMGU_ABI_QUEUE_E_ID,
		offsetof(struct imgu_abi_buffer, payload.frame.frame_data)
	},
	[IPU3_CSS_QUEUE_STAT_3A] = {
		IMGU_ABI_QUEUE_F_ID,
		offsetof(struct imgu_abi_buffer, payload.s3a.data_ptr)
	},
};

/* Initialize queue based on given format, adjust format as needed */
static int imgu_css_queue_init(struct imgu_css_queue *queue,
			       struct v4l2_pix_format_mplane *fmt, u32 flags)
{
	struct v4l2_pix_format_mplane *const f = &queue->fmt.mpix;
	unsigned int i;
	u32 sizeimage;

	INIT_LIST_HEAD(&queue->bufs);

	queue->css_fmt = NULL;	/* Disable */
	if (!fmt)
		return 0;

	for (i = 0; i < ARRAY_SIZE(imgu_css_formats); i++) {
		if (!(imgu_css_formats[i].flags & flags))
			continue;
		queue->css_fmt = &imgu_css_formats[i];
		if (imgu_css_formats[i].pixelformat == fmt->pixelformat)
			break;
	}
	if (!queue->css_fmt)
		return -EINVAL;	/* Could not find any suitable format */

	queue->fmt.mpix = *fmt;

	f->width = ALIGN(clamp_t(u32, f->width,
				 IPU3_CSS_MIN_RES, IPU3_CSS_MAX_W), 2);
	f->height = ALIGN(clamp_t(u32, f->height,
				  IPU3_CSS_MIN_RES, IPU3_CSS_MAX_H), 2);
	queue->width_pad = ALIGN(f->width, queue->css_fmt->width_align);
	if (queue->css_fmt->frame_format != IMGU_ABI_FRAME_FORMAT_RAW_PACKED)
		f->plane_fmt[0].bytesperline = DIV_ROUND_UP(queue->width_pad *
					queue->css_fmt->bytesperpixel_num,
					IPU3_CSS_FORMAT_BPP_DEN);
	else
		/* For packed raw, alignment for bpl is by 50 to the width */
		f->plane_fmt[0].bytesperline =
				DIV_ROUND_UP(f->width,
					     IPU3_CSS_FORMAT_BPP_DEN) *
					     queue->css_fmt->bytesperpixel_num;

	sizeimage = f->height * f->plane_fmt[0].bytesperline;
	if (queue->css_fmt->chroma_decim)
		sizeimage += 2 * sizeimage / queue->css_fmt->chroma_decim;

	f->plane_fmt[0].sizeimage = sizeimage;
	f->field = V4L2_FIELD_NONE;
	f->num_planes = 1;
	f->colorspace = queue->css_fmt->colorspace;
	f->flags = 0;
	f->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->quantization = V4L2_QUANTIZATION_DEFAULT;
	f->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

static bool imgu_css_queue_enabled(struct imgu_css_queue *q)
{
	return q->css_fmt;
}

/******************* css hw *******************/

/* In the style of writesl() defined in include/asm-generic/io.h */
static inline void writes(const void *mem, ssize_t count, void __iomem *addr)
{
	if (count >= 4) {
		const u32 *buf = mem;

		count /= 4;
		do {
			writel(*buf++, addr);
			addr += 4;
		} while (--count);
	}
}

/* Wait until register `reg', masked with `mask', becomes `cmp' */
static int imgu_hw_wait(void __iomem *base, int reg, u32 mask, u32 cmp)
{
	u32 val;

	return readl_poll_timeout(base + reg, val, (val & mask) == cmp,
				  1000, 100 * 1000);
}

/* Initialize the IPU3 CSS hardware and associated h/w blocks */

int imgu_css_set_powerup(struct device *dev, void __iomem *base)
{
	static const unsigned int freq = 450;
	u32 pm_ctrl, state, val;

	dev_dbg(dev, "%s\n", __func__);
	/* Clear the CSS busy signal */
	readl(base + IMGU_REG_GP_BUSY);
	writel(0, base + IMGU_REG_GP_BUSY);

	/* Wait for idle signal */
	if (imgu_hw_wait(base, IMGU_REG_STATE, IMGU_STATE_IDLE_STS,
			 IMGU_STATE_IDLE_STS)) {
		dev_err(dev, "failed to set CSS idle\n");
		goto fail;
	}

	/* Reset the css */
	writel(readl(base + IMGU_REG_PM_CTRL) | IMGU_PM_CTRL_FORCE_RESET,
	       base + IMGU_REG_PM_CTRL);

	usleep_range(200, 300);

	/** Prepare CSS */

	pm_ctrl = readl(base + IMGU_REG_PM_CTRL);
	state = readl(base + IMGU_REG_STATE);

	dev_dbg(dev, "CSS pm_ctrl 0x%x state 0x%x (power %s)\n",
		pm_ctrl, state, state & IMGU_STATE_POWER_DOWN ? "down" : "up");

	/* Power up CSS using wrapper */
	if (state & IMGU_STATE_POWER_DOWN) {
		writel(IMGU_PM_CTRL_RACE_TO_HALT | IMGU_PM_CTRL_START,
		       base + IMGU_REG_PM_CTRL);
		if (imgu_hw_wait(base, IMGU_REG_PM_CTRL,
				 IMGU_PM_CTRL_START, 0)) {
			dev_err(dev, "failed to power up CSS\n");
			goto fail;
		}
		usleep_range(2000, 3000);
	} else {
		writel(IMGU_PM_CTRL_RACE_TO_HALT, base + IMGU_REG_PM_CTRL);
	}

	/* Set the busy bit */
	writel(readl(base + IMGU_REG_GP_BUSY) | 1, base + IMGU_REG_GP_BUSY);

	/* Set CSS clock frequency */
	pm_ctrl = readl(base + IMGU_REG_PM_CTRL);
	val = pm_ctrl & ~(IMGU_PM_CTRL_CSS_PWRDN | IMGU_PM_CTRL_RST_AT_EOF);
	writel(val, base + IMGU_REG_PM_CTRL);
	writel(0, base + IMGU_REG_GP_BUSY);
	if (imgu_hw_wait(base, IMGU_REG_STATE,
			 IMGU_STATE_PWRDNM_FSM_MASK, 0)) {
		dev_err(dev, "failed to pwrdn CSS\n");
		goto fail;
	}
	val = (freq / IMGU_SYSTEM_REQ_FREQ_DIVIDER) & IMGU_SYSTEM_REQ_FREQ_MASK;
	writel(val, base + IMGU_REG_SYSTEM_REQ);
	writel(1, base + IMGU_REG_GP_BUSY);
	writel(readl(base + IMGU_REG_PM_CTRL) | IMGU_PM_CTRL_FORCE_HALT,
	       base + IMGU_REG_PM_CTRL);
	if (imgu_hw_wait(base, IMGU_REG_STATE, IMGU_STATE_HALT_STS,
			 IMGU_STATE_HALT_STS)) {
		dev_err(dev, "failed to halt CSS\n");
		goto fail;
	}

	writel(readl(base + IMGU_REG_PM_CTRL) | IMGU_PM_CTRL_START,
	       base + IMGU_REG_PM_CTRL);
	if (imgu_hw_wait(base, IMGU_REG_PM_CTRL, IMGU_PM_CTRL_START, 0)) {
		dev_err(dev, "failed to start CSS\n");
		goto fail;
	}
	writel(readl(base + IMGU_REG_PM_CTRL) | IMGU_PM_CTRL_FORCE_UNHALT,
	       base + IMGU_REG_PM_CTRL);

	val = readl(base + IMGU_REG_PM_CTRL);	/* get pm_ctrl */
	val &= ~(IMGU_PM_CTRL_CSS_PWRDN | IMGU_PM_CTRL_RST_AT_EOF);
	val |= pm_ctrl & (IMGU_PM_CTRL_CSS_PWRDN | IMGU_PM_CTRL_RST_AT_EOF);
	writel(val, base + IMGU_REG_PM_CTRL);

	return 0;

fail:
	imgu_css_set_powerdown(dev, base);
	return -EIO;
}

void imgu_css_set_powerdown(struct device *dev, void __iomem *base)
{
	dev_dbg(dev, "%s\n", __func__);
	/* wait for cio idle signal */
	if (imgu_hw_wait(base, IMGU_REG_CIO_GATE_BURST_STATE,
			 IMGU_CIO_GATE_BURST_MASK, 0))
		dev_warn(dev, "wait cio gate idle timeout");

	/* wait for css idle signal */
	if (imgu_hw_wait(base, IMGU_REG_STATE, IMGU_STATE_IDLE_STS,
			 IMGU_STATE_IDLE_STS))
		dev_warn(dev, "wait css idle timeout\n");

	/* do halt-halted handshake with css */
	writel(1, base + IMGU_REG_GP_HALT);
	if (imgu_hw_wait(base, IMGU_REG_STATE, IMGU_STATE_HALT_STS,
			 IMGU_STATE_HALT_STS))
		dev_warn(dev, "failed to halt css");

	/* de-assert the busy bit */
	writel(0, base + IMGU_REG_GP_BUSY);
}

static void imgu_css_hw_enable_irq(struct imgu_css *css)
{
	void __iomem *const base = css->base;
	u32 val, i;

	/* Set up interrupts */

	/*
	 * Enable IRQ on the SP which signals that SP goes to idle
	 * (aka ready state) and set trigger to pulse
	 */
	val = readl(base + IMGU_REG_SP_CTRL(0)) | IMGU_CTRL_IRQ_READY;
	writel(val, base + IMGU_REG_SP_CTRL(0));
	writel(val | IMGU_CTRL_IRQ_CLEAR, base + IMGU_REG_SP_CTRL(0));

	/* Enable IRQs from the IMGU wrapper */
	writel(IMGU_REG_INT_CSS_IRQ, base + IMGU_REG_INT_ENABLE);
	/* Clear */
	writel(IMGU_REG_INT_CSS_IRQ, base + IMGU_REG_INT_STATUS);

	/* Enable IRQs from main IRQ controller */
	writel(~0, base + IMGU_REG_IRQCTRL_EDGE_NOT_PULSE(IMGU_IRQCTRL_MAIN));
	writel(0, base + IMGU_REG_IRQCTRL_MASK(IMGU_IRQCTRL_MAIN));
	writel(IMGU_IRQCTRL_IRQ_MASK,
	       base + IMGU_REG_IRQCTRL_EDGE(IMGU_IRQCTRL_MAIN));
	writel(IMGU_IRQCTRL_IRQ_MASK,
	       base + IMGU_REG_IRQCTRL_ENABLE(IMGU_IRQCTRL_MAIN));
	writel(IMGU_IRQCTRL_IRQ_MASK,
	       base + IMGU_REG_IRQCTRL_CLEAR(IMGU_IRQCTRL_MAIN));
	writel(IMGU_IRQCTRL_IRQ_MASK,
	       base + IMGU_REG_IRQCTRL_MASK(IMGU_IRQCTRL_MAIN));
	/* Wait for write complete */
	readl(base + IMGU_REG_IRQCTRL_ENABLE(IMGU_IRQCTRL_MAIN));

	/* Enable IRQs from SP0 and SP1 controllers */
	for (i = IMGU_IRQCTRL_SP0; i <= IMGU_IRQCTRL_SP1; i++) {
		writel(~0, base + IMGU_REG_IRQCTRL_EDGE_NOT_PULSE(i));
		writel(0, base + IMGU_REG_IRQCTRL_MASK(i));
		writel(IMGU_IRQCTRL_IRQ_MASK, base + IMGU_REG_IRQCTRL_EDGE(i));
		writel(IMGU_IRQCTRL_IRQ_MASK,
		       base + IMGU_REG_IRQCTRL_ENABLE(i));
		writel(IMGU_IRQCTRL_IRQ_MASK, base + IMGU_REG_IRQCTRL_CLEAR(i));
		writel(IMGU_IRQCTRL_IRQ_MASK, base + IMGU_REG_IRQCTRL_MASK(i));
		/* Wait for write complete */
		readl(base + IMGU_REG_IRQCTRL_ENABLE(i));
	}
}

static int imgu_css_hw_init(struct imgu_css *css)
{
	/* For checking that streaming monitor statuses are valid */
	static const struct {
		u32 reg;
		u32 mask;
		const char *name;
	} stream_monitors[] = {
		{
			IMGU_REG_GP_SP1_STRMON_STAT,
			IMGU_GP_STRMON_STAT_ISP_PORT_SP12ISP,
			"ISP0 to SP0"
		}, {
			IMGU_REG_GP_ISP_STRMON_STAT,
			IMGU_GP_STRMON_STAT_SP1_PORT_ISP2SP1,
			"SP0 to ISP0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_ISP2DMA,
			"ISP0 to DMA0"
		}, {
			IMGU_REG_GP_ISP_STRMON_STAT,
			IMGU_GP_STRMON_STAT_ISP_PORT_DMA2ISP,
			"DMA0 to ISP0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_CELLS2GDC,
			"ISP0 to GDC0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_GDC2CELLS,
			"GDC0 to ISP0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_SP12DMA,
			"SP0 to DMA0"
		}, {
			IMGU_REG_GP_SP1_STRMON_STAT,
			IMGU_GP_STRMON_STAT_SP1_PORT_DMA2SP1,
			"DMA0 to SP0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_CELLS2GDC,
			"SP0 to GDC0"
		}, {
			IMGU_REG_GP_MOD_STRMON_STAT,
			IMGU_GP_STRMON_STAT_MOD_PORT_GDC2CELLS,
			"GDC0 to SP0"
		},
	};

	struct device *dev = css->dev;
	void __iomem *const base = css->base;
	u32 val, i;

	/* Set instruction cache address and inv bit for ISP, SP, and SP1 */
	for (i = 0; i < IMGU_NUM_SP; i++) {
		struct imgu_fw_info *bi =
					&css->fwp->binary_header[css->fw_sp[i]];

		writel(css->binary[css->fw_sp[i]].daddr,
		       base + IMGU_REG_SP_ICACHE_ADDR(bi->type));
		writel(readl(base + IMGU_REG_SP_CTRL(bi->type)) |
		       IMGU_CTRL_ICACHE_INV,
		       base + IMGU_REG_SP_CTRL(bi->type));
	}
	writel(css->binary[css->fw_bl].daddr, base + IMGU_REG_ISP_ICACHE_ADDR);
	writel(readl(base + IMGU_REG_ISP_CTRL) | IMGU_CTRL_ICACHE_INV,
	       base + IMGU_REG_ISP_CTRL);

	/* Check that IMGU hardware is ready */

	if (!(readl(base + IMGU_REG_SP_CTRL(0)) & IMGU_CTRL_IDLE)) {
		dev_err(dev, "SP is not idle\n");
		return -EIO;
	}
	if (!(readl(base + IMGU_REG_ISP_CTRL) & IMGU_CTRL_IDLE)) {
		dev_err(dev, "ISP is not idle\n");
		return -EIO;
	}

	for (i = 0; i < ARRAY_SIZE(stream_monitors); i++) {
		val = readl(base + stream_monitors[i].reg);
		if (val & stream_monitors[i].mask) {
			dev_err(dev, "error: Stream monitor %s is valid\n",
				stream_monitors[i].name);
			return -EIO;
		}
	}

	/* Initialize GDC with default values */

	for (i = 0; i < ARRAY_SIZE(imgu_css_gdc_lut[0]); i++) {
		u32 val0 = imgu_css_gdc_lut[0][i] & IMGU_GDC_LUT_MASK;
		u32 val1 = imgu_css_gdc_lut[1][i] & IMGU_GDC_LUT_MASK;
		u32 val2 = imgu_css_gdc_lut[2][i] & IMGU_GDC_LUT_MASK;
		u32 val3 = imgu_css_gdc_lut[3][i] & IMGU_GDC_LUT_MASK;

		writel(val0 | (val1 << 16),
		       base + IMGU_REG_GDC_LUT_BASE + i * 8);
		writel(val2 | (val3 << 16),
		       base + IMGU_REG_GDC_LUT_BASE + i * 8 + 4);
	}

	return 0;
}

/* Boot the given IPU3 CSS SP */
static int imgu_css_hw_start_sp(struct imgu_css *css, int sp)
{
	void __iomem *const base = css->base;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[sp]];
	struct imgu_abi_sp_init_dmem_cfg dmem_cfg = {
		.ddr_data_addr = css->binary[css->fw_sp[sp]].daddr
			+ bi->blob.data_source,
		.dmem_data_addr = bi->blob.data_target,
		.dmem_bss_addr = bi->blob.bss_target,
		.data_size = bi->blob.data_size,
		.bss_size = bi->blob.bss_size,
		.sp_id = sp,
	};

	writes(&dmem_cfg, sizeof(dmem_cfg), base +
	       IMGU_REG_SP_DMEM_BASE(sp) + bi->info.sp.init_dmem_data);

	writel(bi->info.sp.sp_entry, base + IMGU_REG_SP_START_ADDR(sp));

	writel(readl(base + IMGU_REG_SP_CTRL(sp))
		| IMGU_CTRL_START | IMGU_CTRL_RUN, base + IMGU_REG_SP_CTRL(sp));

	if (imgu_hw_wait(css->base, IMGU_REG_SP_DMEM_BASE(sp)
			 + bi->info.sp.sw_state,
			 ~0, IMGU_ABI_SP_SWSTATE_INITIALIZED))
		return -EIO;

	return 0;
}

/* Start the IPU3 CSS ImgU (Imaging Unit) and all the SPs */
static int imgu_css_hw_start(struct imgu_css *css)
{
	static const u32 event_mask =
		((1 << IMGU_ABI_EVTTYPE_OUT_FRAME_DONE) |
		(1 << IMGU_ABI_EVTTYPE_2ND_OUT_FRAME_DONE) |
		(1 << IMGU_ABI_EVTTYPE_VF_OUT_FRAME_DONE) |
		(1 << IMGU_ABI_EVTTYPE_2ND_VF_OUT_FRAME_DONE) |
		(1 << IMGU_ABI_EVTTYPE_3A_STATS_DONE) |
		(1 << IMGU_ABI_EVTTYPE_DIS_STATS_DONE) |
		(1 << IMGU_ABI_EVTTYPE_PIPELINE_DONE) |
		(1 << IMGU_ABI_EVTTYPE_FRAME_TAGGED) |
		(1 << IMGU_ABI_EVTTYPE_INPUT_FRAME_DONE) |
		(1 << IMGU_ABI_EVTTYPE_METADATA_DONE) |
		(1 << IMGU_ABI_EVTTYPE_ACC_STAGE_COMPLETE))
		<< IMGU_ABI_SP_COMM_EVENT_IRQ_MASK_OR_SHIFT;

	void __iomem *const base = css->base;
	struct imgu_fw_info *bi, *bl = &css->fwp->binary_header[css->fw_bl];
	unsigned int i;

	writel(IMGU_TLB_INVALIDATE, base + IMGU_REG_TLB_INVALIDATE);

	/* Start bootloader */

	writel(IMGU_ABI_BL_SWSTATE_BUSY,
	       base + IMGU_REG_ISP_DMEM_BASE + bl->info.bl.sw_state);
	writel(IMGU_NUM_SP,
	       base + IMGU_REG_ISP_DMEM_BASE + bl->info.bl.num_dma_cmds);

	for (i = 0; i < IMGU_NUM_SP; i++) {
		int j = IMGU_NUM_SP - i - 1;	/* load sp1 first, then sp0 */
		struct imgu_fw_info *sp =
					&css->fwp->binary_header[css->fw_sp[j]];
		struct imgu_abi_bl_dma_cmd_entry dma_cmd = {
			.src_addr = css->binary[css->fw_sp[j]].daddr
				+ sp->blob.text_source,
			.size = sp->blob.text_size,
			.dst_type = IMGU_ABI_BL_DMACMD_TYPE_SP_PMEM,
			.dst_addr = IMGU_SP_PMEM_BASE(j),
		};

		writes(&dma_cmd, sizeof(dma_cmd),
		       base + IMGU_REG_ISP_DMEM_BASE + i * sizeof(dma_cmd) +
		       bl->info.bl.dma_cmd_list);
	}

	writel(bl->info.bl.bl_entry, base + IMGU_REG_ISP_START_ADDR);

	writel(readl(base + IMGU_REG_ISP_CTRL)
		| IMGU_CTRL_START | IMGU_CTRL_RUN, base + IMGU_REG_ISP_CTRL);
	if (imgu_hw_wait(css->base, IMGU_REG_ISP_DMEM_BASE
			 + bl->info.bl.sw_state, ~0,
			 IMGU_ABI_BL_SWSTATE_OK)) {
		dev_err(css->dev, "failed to start bootloader\n");
		return -EIO;
	}

	/* Start ISP */

	memset(css->xmem_sp_group_ptrs.vaddr, 0,
	       sizeof(struct imgu_abi_sp_group));

	bi = &css->fwp->binary_header[css->fw_sp[0]];

	writel(css->xmem_sp_group_ptrs.daddr,
	       base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.per_frame_data);

	writel(IMGU_ABI_SP_SWSTATE_TERMINATED,
	       base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.sw_state);
	writel(1, base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.invalidate_tlb);

	if (imgu_css_hw_start_sp(css, 0))
		return -EIO;

	writel(0, base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.isp_started);
	writel(0, base + IMGU_REG_SP_DMEM_BASE(0) +
		bi->info.sp.host_sp_queues_initialized);
	writel(0, base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.sleep_mode);
	writel(0, base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.invalidate_tlb);
	writel(IMGU_ABI_SP_COMM_COMMAND_READY, base + IMGU_REG_SP_DMEM_BASE(0)
		+ bi->info.sp.host_sp_com + IMGU_ABI_SP_COMM_COMMAND);

	/* Enable all events for all queues */

	for (i = 0; i < IPU3_CSS_PIPE_ID_NUM; i++)
		writel(event_mask, base + IMGU_REG_SP_DMEM_BASE(0)
			+ bi->info.sp.host_sp_com
			+ IMGU_ABI_SP_COMM_EVENT_IRQ_MASK(i));
	writel(1, base + IMGU_REG_SP_DMEM_BASE(0) +
		bi->info.sp.host_sp_queues_initialized);

	/* Start SP1 */

	bi = &css->fwp->binary_header[css->fw_sp[1]];

	writel(IMGU_ABI_SP_SWSTATE_TERMINATED,
	       base + IMGU_REG_SP_DMEM_BASE(1) + bi->info.sp.sw_state);

	if (imgu_css_hw_start_sp(css, 1))
		return -EIO;

	writel(IMGU_ABI_SP_COMM_COMMAND_READY, base + IMGU_REG_SP_DMEM_BASE(1)
		+ bi->info.sp.host_sp_com + IMGU_ABI_SP_COMM_COMMAND);

	return 0;
}

static void imgu_css_hw_stop(struct imgu_css *css)
{
	void __iomem *const base = css->base;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[0]];

	/* Stop fw */
	writel(IMGU_ABI_SP_COMM_COMMAND_TERMINATE,
	       base + IMGU_REG_SP_DMEM_BASE(0) +
	       bi->info.sp.host_sp_com + IMGU_ABI_SP_COMM_COMMAND);
	if (imgu_hw_wait(css->base, IMGU_REG_SP_CTRL(0),
			 IMGU_CTRL_IDLE, IMGU_CTRL_IDLE))
		dev_err(css->dev, "wait sp0 idle timeout.\n");
	if (readl(base + IMGU_REG_SP_DMEM_BASE(0) + bi->info.sp.sw_state) !=
		  IMGU_ABI_SP_SWSTATE_TERMINATED)
		dev_err(css->dev, "sp0 is not terminated.\n");
	if (imgu_hw_wait(css->base, IMGU_REG_ISP_CTRL,
			 IMGU_CTRL_IDLE, IMGU_CTRL_IDLE))
		dev_err(css->dev, "wait isp idle timeout\n");
}

static void imgu_css_hw_cleanup(struct imgu_css *css)
{
	void __iomem *const base = css->base;

	/** Reset CSS **/

	/* Clear the CSS busy signal */
	readl(base + IMGU_REG_GP_BUSY);
	writel(0, base + IMGU_REG_GP_BUSY);

	/* Wait for idle signal */
	if (imgu_hw_wait(css->base, IMGU_REG_STATE, IMGU_STATE_IDLE_STS,
			 IMGU_STATE_IDLE_STS))
		dev_err(css->dev, "failed to shut down hw cleanly\n");

	/* Reset the css */
	writel(readl(base + IMGU_REG_PM_CTRL) | IMGU_PM_CTRL_FORCE_RESET,
	       base + IMGU_REG_PM_CTRL);

	usleep_range(200, 300);
}

static void imgu_css_pipeline_cleanup(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	unsigned int i;

	imgu_css_pool_cleanup(imgu,
			      &css->pipes[pipe].pool.parameter_set_info);
	imgu_css_pool_cleanup(imgu, &css->pipes[pipe].pool.acc);
	imgu_css_pool_cleanup(imgu, &css->pipes[pipe].pool.gdc);
	imgu_css_pool_cleanup(imgu, &css->pipes[pipe].pool.obgrid);

	for (i = 0; i < IMGU_ABI_NUM_MEMORIES; i++)
		imgu_css_pool_cleanup(imgu,
				      &css->pipes[pipe].pool.binary_params_p[i]);
}

/*
 * This function initializes various stages of the
 * IPU3 CSS ISP pipeline
 */
static int imgu_css_pipeline_init(struct imgu_css *css, unsigned int pipe)
{
	static const int BYPC = 2;	/* Bytes per component */
	static const struct imgu_abi_buffer_sp buffer_sp_init = {
		.buf_src = {.queue_id = IMGU_ABI_QUEUE_EVENT_ID},
		.buf_type = IMGU_ABI_BUFFER_TYPE_INVALID,
	};

	struct imgu_abi_isp_iterator_config *cfg_iter;
	struct imgu_abi_isp_ref_config *cfg_ref;
	struct imgu_abi_isp_dvs_config *cfg_dvs;
	struct imgu_abi_isp_tnr3_config *cfg_tnr;
	struct imgu_abi_isp_ref_dmem_state *cfg_ref_state;
	struct imgu_abi_isp_tnr3_dmem_state *cfg_tnr_state;

	const int stage = 0;
	unsigned int i, j;

	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];
	const struct imgu_fw_info *bi =
			&css->fwp->binary_header[css_pipe->bindex];
	const unsigned int stripes = bi->info.isp.sp.iterator.num_stripes;

	struct imgu_fw_config_memory_offsets *cofs = (void *)css->fwp +
		bi->blob.memory_offsets.offsets[IMGU_ABI_PARAM_CLASS_CONFIG];
	struct imgu_fw_state_memory_offsets *sofs = (void *)css->fwp +
		bi->blob.memory_offsets.offsets[IMGU_ABI_PARAM_CLASS_STATE];

	struct imgu_abi_isp_stage *isp_stage;
	struct imgu_abi_sp_stage *sp_stage;
	struct imgu_abi_sp_group *sp_group;

	const unsigned int bds_width_pad =
				ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width,
				      2 * IPU3_UAPI_ISP_VEC_ELEMS);

	const enum imgu_abi_memories m0 = IMGU_ABI_MEM_ISP_DMEM0;
	enum imgu_abi_param_class cfg = IMGU_ABI_PARAM_CLASS_CONFIG;
	void *vaddr = css_pipe->binary_params_cs[cfg - 1][m0].vaddr;

	struct imgu_device *imgu = dev_get_drvdata(css->dev);

	dev_dbg(css->dev, "%s for pipe %d", __func__, pipe);

	/* Configure iterator */

	cfg_iter = imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
					       &cofs->dmem.iterator,
					       sizeof(*cfg_iter), vaddr);
	if (!cfg_iter)
		goto bad_firmware;

	cfg_iter->input_info.res.width =
				css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.width;
	cfg_iter->input_info.res.height =
				css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.height;
	cfg_iter->input_info.padded_width =
				css_pipe->queue[IPU3_CSS_QUEUE_IN].width_pad;
	cfg_iter->input_info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->frame_format;
	cfg_iter->input_info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bit_depth;
	cfg_iter->input_info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order;
	cfg_iter->input_info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;

	cfg_iter->internal_info.res.width = css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	cfg_iter->internal_info.res.height =
					css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	cfg_iter->internal_info.padded_width = bds_width_pad;
	cfg_iter->internal_info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;
	cfg_iter->internal_info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bit_depth;
	cfg_iter->internal_info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bayer_order;
	cfg_iter->internal_info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;

	cfg_iter->output_info.res.width =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	cfg_iter->output_info.res.height =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	cfg_iter->output_info.padded_width =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].width_pad;
	cfg_iter->output_info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;
	cfg_iter->output_info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bit_depth;
	cfg_iter->output_info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bayer_order;
	cfg_iter->output_info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;

	cfg_iter->vf_info.res.width =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	cfg_iter->vf_info.res.height =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	cfg_iter->vf_info.padded_width =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad;
	cfg_iter->vf_info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->frame_format;
	cfg_iter->vf_info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->bit_depth;
	cfg_iter->vf_info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->bayer_order;
	cfg_iter->vf_info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;

	cfg_iter->dvs_envelope.width = css_pipe->rect[IPU3_CSS_RECT_ENVELOPE].width;
	cfg_iter->dvs_envelope.height =
				css_pipe->rect[IPU3_CSS_RECT_ENVELOPE].height;

	/* Configure reference (delay) frames */

	cfg_ref = imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
					      &cofs->dmem.ref,
					      sizeof(*cfg_ref), vaddr);
	if (!cfg_ref)
		goto bad_firmware;

	cfg_ref->port_b.crop = 0;
	cfg_ref->port_b.elems = IMGU_ABI_ISP_DDR_WORD_BYTES / BYPC;
	cfg_ref->port_b.width =
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].width;
	cfg_ref->port_b.stride =
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperline;
	cfg_ref->width_a_over_b =
				IPU3_UAPI_ISP_VEC_ELEMS / cfg_ref->port_b.elems;
	cfg_ref->dvs_frame_delay = IPU3_CSS_AUX_FRAMES - 1;
	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++) {
		cfg_ref->ref_frame_addr_y[i] =
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].mem[i].daddr;
		cfg_ref->ref_frame_addr_c[i] =
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].mem[i].daddr +
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperline *
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].height;
	}
	for (; i < IMGU_ABI_FRAMES_REF; i++) {
		cfg_ref->ref_frame_addr_y[i] = 0;
		cfg_ref->ref_frame_addr_c[i] = 0;
	}

	/* Configure DVS (digital video stabilization) */

	cfg_dvs = imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
					      &cofs->dmem.dvs, sizeof(*cfg_dvs),
					      vaddr);
	if (!cfg_dvs)
		goto bad_firmware;

	cfg_dvs->num_horizontal_blocks =
			ALIGN(DIV_ROUND_UP(css_pipe->rect[IPU3_CSS_RECT_GDC].width,
					   IMGU_DVS_BLOCK_W), 2);
	cfg_dvs->num_vertical_blocks =
			DIV_ROUND_UP(css_pipe->rect[IPU3_CSS_RECT_GDC].height,
				     IMGU_DVS_BLOCK_H);

	/* Configure TNR (temporal noise reduction) */

	if (css_pipe->pipe_id == IPU3_CSS_PIPE_ID_VIDEO) {
		cfg_tnr = imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
						      &cofs->dmem.tnr3,
						      sizeof(*cfg_tnr),
						      vaddr);
		if (!cfg_tnr)
			goto bad_firmware;

		cfg_tnr->port_b.crop = 0;
		cfg_tnr->port_b.elems = IMGU_ABI_ISP_DDR_WORD_BYTES;
		cfg_tnr->port_b.width =
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].width;
		cfg_tnr->port_b.stride =
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].bytesperline;
		cfg_tnr->width_a_over_b =
			IPU3_UAPI_ISP_VEC_ELEMS / cfg_tnr->port_b.elems;
		cfg_tnr->frame_height =
			css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].height;
		cfg_tnr->delay_frame = IPU3_CSS_AUX_FRAMES - 1;
		for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
			cfg_tnr->frame_addr[i] =
				css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR]
					.mem[i].daddr;
		for (; i < IMGU_ABI_FRAMES_TNR; i++)
			cfg_tnr->frame_addr[i] = 0;
	}

	/* Configure ref dmem state parameters */

	cfg = IMGU_ABI_PARAM_CLASS_STATE;
	vaddr = css_pipe->binary_params_cs[cfg - 1][m0].vaddr;

	cfg_ref_state = imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
						    &sofs->dmem.ref,
						    sizeof(*cfg_ref_state),
						    vaddr);
	if (!cfg_ref_state)
		goto bad_firmware;

	cfg_ref_state->ref_in_buf_idx = 0;
	cfg_ref_state->ref_out_buf_idx = 1;

	/* Configure tnr dmem state parameters */
	if (css_pipe->pipe_id == IPU3_CSS_PIPE_ID_VIDEO) {
		cfg_tnr_state =
			imgu_css_fw_pipeline_params(css, pipe, cfg, m0,
						    &sofs->dmem.tnr3,
						    sizeof(*cfg_tnr_state),
						    vaddr);
		if (!cfg_tnr_state)
			goto bad_firmware;

		cfg_tnr_state->in_bufidx = 0;
		cfg_tnr_state->out_bufidx = 1;
		cfg_tnr_state->bypass_filter = 0;
		cfg_tnr_state->total_frame_counter = 0;
		for (i = 0; i < IMGU_ABI_BUF_SETS_TNR; i++)
			cfg_tnr_state->buffer_frame_counter[i] = 0;
	}

	/* Configure ISP stage */

	isp_stage = css_pipe->xmem_isp_stage_ptrs[pipe][stage].vaddr;
	memset(isp_stage, 0, sizeof(*isp_stage));
	isp_stage->blob_info = bi->blob;
	isp_stage->binary_info = bi->info.isp.sp;
	strscpy(isp_stage->binary_name,
		(char *)css->fwp + bi->blob.prog_name_offset,
		sizeof(isp_stage->binary_name));
	isp_stage->mem_initializers = bi->info.isp.sp.mem_initializers;
	for (i = IMGU_ABI_PARAM_CLASS_CONFIG; i < IMGU_ABI_PARAM_CLASS_NUM; i++)
		for (j = 0; j < IMGU_ABI_NUM_MEMORIES; j++)
			isp_stage->mem_initializers.params[i][j].address =
					css_pipe->binary_params_cs[i - 1][j].daddr;

	/* Configure SP stage */

	sp_stage = css_pipe->xmem_sp_stage_ptrs[pipe][stage].vaddr;
	memset(sp_stage, 0, sizeof(*sp_stage));

	sp_stage->frames.in.buf_attr = buffer_sp_init;
	for (i = 0; i < IMGU_ABI_BINARY_MAX_OUTPUT_PORTS; i++)
		sp_stage->frames.out[i].buf_attr = buffer_sp_init;
	sp_stage->frames.out_vf.buf_attr = buffer_sp_init;
	sp_stage->frames.s3a_buf = buffer_sp_init;
	sp_stage->frames.dvs_buf = buffer_sp_init;

	sp_stage->stage_type = IMGU_ABI_STAGE_TYPE_ISP;
	sp_stage->num = stage;
	sp_stage->isp_online = 0;
	sp_stage->isp_copy_vf = 0;
	sp_stage->isp_copy_output = 0;

	sp_stage->enable.vf_output = css_pipe->vf_output_en;

	sp_stage->frames.effective_in_res.width =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].width;
	sp_stage->frames.effective_in_res.height =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height;
	sp_stage->frames.in.info.res.width =
				css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.width;
	sp_stage->frames.in.info.res.height =
				css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.height;
	sp_stage->frames.in.info.padded_width =
					css_pipe->queue[IPU3_CSS_QUEUE_IN].width_pad;
	sp_stage->frames.in.info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->frame_format;
	sp_stage->frames.in.info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bit_depth;
	sp_stage->frames.in.info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order;
	sp_stage->frames.in.info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;
	sp_stage->frames.in.buf_attr.buf_src.queue_id = IMGU_ABI_QUEUE_C_ID;
	sp_stage->frames.in.buf_attr.buf_type =
					IMGU_ABI_BUFFER_TYPE_INPUT_FRAME;

	sp_stage->frames.out[0].info.res.width =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	sp_stage->frames.out[0].info.res.height =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	sp_stage->frames.out[0].info.padded_width =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].width_pad;
	sp_stage->frames.out[0].info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;
	sp_stage->frames.out[0].info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bit_depth;
	sp_stage->frames.out[0].info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bayer_order;
	sp_stage->frames.out[0].info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;
	sp_stage->frames.out[0].planes.nv.uv.offset =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].width_pad *
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	sp_stage->frames.out[0].buf_attr.buf_src.queue_id = IMGU_ABI_QUEUE_D_ID;
	sp_stage->frames.out[0].buf_attr.buf_type =
					IMGU_ABI_BUFFER_TYPE_OUTPUT_FRAME;

	sp_stage->frames.out[1].buf_attr.buf_src.queue_id =
							IMGU_ABI_QUEUE_EVENT_ID;

	sp_stage->frames.internal_frame_info.res.width =
					css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	sp_stage->frames.internal_frame_info.res.height =
					css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	sp_stage->frames.internal_frame_info.padded_width = bds_width_pad;

	sp_stage->frames.internal_frame_info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;
	sp_stage->frames.internal_frame_info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bit_depth;
	sp_stage->frames.internal_frame_info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->bayer_order;
	sp_stage->frames.internal_frame_info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;

	sp_stage->frames.out_vf.info.res.width =
				css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	sp_stage->frames.out_vf.info.res.height =
				css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	sp_stage->frames.out_vf.info.padded_width =
					css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad;
	sp_stage->frames.out_vf.info.format =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->frame_format;
	sp_stage->frames.out_vf.info.raw_bit_depth =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->bit_depth;
	sp_stage->frames.out_vf.info.raw_bayer_order =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->bayer_order;
	sp_stage->frames.out_vf.info.raw_type = IMGU_ABI_RAW_TYPE_BAYER;
	sp_stage->frames.out_vf.planes.yuv.u.offset =
				css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad *
				css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	sp_stage->frames.out_vf.planes.yuv.v.offset =
			css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad *
			css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height * 5 / 4;
	sp_stage->frames.out_vf.buf_attr.buf_src.queue_id = IMGU_ABI_QUEUE_E_ID;
	sp_stage->frames.out_vf.buf_attr.buf_type =
					IMGU_ABI_BUFFER_TYPE_VF_OUTPUT_FRAME;

	sp_stage->frames.s3a_buf.buf_src.queue_id = IMGU_ABI_QUEUE_F_ID;
	sp_stage->frames.s3a_buf.buf_type = IMGU_ABI_BUFFER_TYPE_3A_STATISTICS;

	sp_stage->frames.dvs_buf.buf_src.queue_id = IMGU_ABI_QUEUE_G_ID;
	sp_stage->frames.dvs_buf.buf_type = IMGU_ABI_BUFFER_TYPE_DIS_STATISTICS;

	sp_stage->dvs_envelope.width = css_pipe->rect[IPU3_CSS_RECT_ENVELOPE].width;
	sp_stage->dvs_envelope.height =
				css_pipe->rect[IPU3_CSS_RECT_ENVELOPE].height;

	sp_stage->isp_pipe_version =
				bi->info.isp.sp.pipeline.isp_pipe_version;
	sp_stage->isp_deci_log_factor =
			clamp(max(fls(css_pipe->rect[IPU3_CSS_RECT_BDS].width /
				      IMGU_MAX_BQ_GRID_WIDTH),
				  fls(css_pipe->rect[IPU3_CSS_RECT_BDS].height /
				      IMGU_MAX_BQ_GRID_HEIGHT)) - 1, 3, 5);
	sp_stage->isp_vf_downscale_bits = 0;
	sp_stage->if_config_index = 255;
	sp_stage->sp_enable_xnr = 0;
	sp_stage->num_stripes = stripes;
	sp_stage->enable.s3a = 1;
	sp_stage->enable.dvs_stats = 0;

	sp_stage->xmem_bin_addr = css->binary[css_pipe->bindex].daddr;
	sp_stage->xmem_map_addr = css_pipe->sp_ddr_ptrs.daddr;
	sp_stage->isp_stage_addr =
		css_pipe->xmem_isp_stage_ptrs[pipe][stage].daddr;

	/* Configure SP group */

	sp_group = css->xmem_sp_group_ptrs.vaddr;
	memset(&sp_group->pipe[pipe], 0, sizeof(struct imgu_abi_sp_pipeline));

	sp_group->pipe[pipe].num_stages = 1;
	sp_group->pipe[pipe].pipe_id = css_pipe->pipe_id;
	sp_group->pipe[pipe].thread_id = pipe;
	sp_group->pipe[pipe].pipe_num = pipe;
	sp_group->pipe[pipe].num_execs = -1;
	sp_group->pipe[pipe].pipe_qos_config = -1;
	sp_group->pipe[pipe].required_bds_factor = 0;
	sp_group->pipe[pipe].dvs_frame_delay = IPU3_CSS_AUX_FRAMES - 1;
	sp_group->pipe[pipe].inout_port_config =
					IMGU_ABI_PORT_CONFIG_TYPE_INPUT_HOST |
					IMGU_ABI_PORT_CONFIG_TYPE_OUTPUT_HOST;
	sp_group->pipe[pipe].scaler_pp_lut = 0;
	sp_group->pipe[pipe].shading.internal_frame_origin_x_bqs_on_sctbl = 0;
	sp_group->pipe[pipe].shading.internal_frame_origin_y_bqs_on_sctbl = 0;
	sp_group->pipe[pipe].sp_stage_addr[stage] =
			css_pipe->xmem_sp_stage_ptrs[pipe][stage].daddr;
	sp_group->pipe[pipe].pipe_config =
			bi->info.isp.sp.enable.params ? (1 << pipe) : 0;
	sp_group->pipe[pipe].pipe_config |= IMGU_ABI_PIPE_CONFIG_ACQUIRE_ISP;

	/* Initialize parameter pools */

	if (imgu_css_pool_init(imgu, &css_pipe->pool.parameter_set_info,
			       sizeof(struct imgu_abi_parameter_set_info)) ||
	    imgu_css_pool_init(imgu, &css_pipe->pool.acc,
			       sizeof(struct imgu_abi_acc_param)) ||
	    imgu_css_pool_init(imgu, &css_pipe->pool.gdc,
			       sizeof(struct imgu_abi_gdc_warp_param) *
			       3 * cfg_dvs->num_horizontal_blocks / 2 *
			       cfg_dvs->num_vertical_blocks) ||
	    imgu_css_pool_init(imgu, &css_pipe->pool.obgrid,
			       imgu_css_fw_obgrid_size(
			       &css->fwp->binary_header[css_pipe->bindex])))
		goto out_of_memory;

	for (i = 0; i < IMGU_ABI_NUM_MEMORIES; i++)
		if (imgu_css_pool_init(imgu,
				       &css_pipe->pool.binary_params_p[i],
				       bi->info.isp.sp.mem_initializers.params
				       [IMGU_ABI_PARAM_CLASS_PARAM][i].size))
			goto out_of_memory;

	return 0;

bad_firmware:
	imgu_css_pipeline_cleanup(css, pipe);
	return -EPROTO;

out_of_memory:
	imgu_css_pipeline_cleanup(css, pipe);
	return -ENOMEM;
}

static u8 imgu_css_queue_pos(struct imgu_css *css, int queue, int thread)
{
	static const unsigned int sp;
	void __iomem *const base = css->base;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[sp]];
	struct imgu_abi_queues __iomem *q = base + IMGU_REG_SP_DMEM_BASE(sp) +
						bi->info.sp.host_sp_queue;

	return queue >= 0 ? readb(&q->host2sp_bufq_info[thread][queue].end) :
			    readb(&q->host2sp_evtq_info.end);
}

/* Sent data to sp using given buffer queue, or if queue < 0, event queue. */
static int imgu_css_queue_data(struct imgu_css *css,
			       int queue, int thread, u32 data)
{
	static const unsigned int sp;
	void __iomem *const base = css->base;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[sp]];
	struct imgu_abi_queues __iomem *q = base + IMGU_REG_SP_DMEM_BASE(sp) +
						bi->info.sp.host_sp_queue;
	u8 size, start, end, end2;

	if (queue >= 0) {
		size = readb(&q->host2sp_bufq_info[thread][queue].size);
		start = readb(&q->host2sp_bufq_info[thread][queue].start);
		end = readb(&q->host2sp_bufq_info[thread][queue].end);
	} else {
		size = readb(&q->host2sp_evtq_info.size);
		start = readb(&q->host2sp_evtq_info.start);
		end = readb(&q->host2sp_evtq_info.end);
	}

	if (size == 0)
		return -EIO;

	end2 = (end + 1) % size;
	if (end2 == start)
		return -EBUSY;	/* Queue full */

	if (queue >= 0) {
		writel(data, &q->host2sp_bufq[thread][queue][end]);
		writeb(end2, &q->host2sp_bufq_info[thread][queue].end);
	} else {
		writel(data, &q->host2sp_evtq[end]);
		writeb(end2, &q->host2sp_evtq_info.end);
	}

	return 0;
}

/* Receive data using given buffer queue, or if queue < 0, event queue. */
static int imgu_css_dequeue_data(struct imgu_css *css, int queue, u32 *data)
{
	static const unsigned int sp;
	void __iomem *const base = css->base;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[sp]];
	struct imgu_abi_queues __iomem *q = base + IMGU_REG_SP_DMEM_BASE(sp) +
						bi->info.sp.host_sp_queue;
	u8 size, start, end, start2;

	if (queue >= 0) {
		size = readb(&q->sp2host_bufq_info[queue].size);
		start = readb(&q->sp2host_bufq_info[queue].start);
		end = readb(&q->sp2host_bufq_info[queue].end);
	} else {
		size = readb(&q->sp2host_evtq_info.size);
		start = readb(&q->sp2host_evtq_info.start);
		end = readb(&q->sp2host_evtq_info.end);
	}

	if (size == 0)
		return -EIO;

	if (end == start)
		return -EBUSY;	/* Queue empty */

	start2 = (start + 1) % size;

	if (queue >= 0) {
		*data = readl(&q->sp2host_bufq[queue][start]);
		writeb(start2, &q->sp2host_bufq_info[queue].start);
	} else {
		int r;

		*data = readl(&q->sp2host_evtq[start]);
		writeb(start2, &q->sp2host_evtq_info.start);

		/* Acknowledge events dequeued from event queue */
		r = imgu_css_queue_data(css, queue, 0,
					IMGU_ABI_EVENT_EVENT_DEQUEUED);
		if (r < 0)
			return r;
	}

	return 0;
}

/* Free binary-specific resources */
static void imgu_css_binary_cleanup(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	unsigned int i, j;

	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

	for (j = 0; j < IMGU_ABI_PARAM_CLASS_NUM - 1; j++)
		for (i = 0; i < IMGU_ABI_NUM_MEMORIES; i++)
			imgu_dmamap_free(imgu,
					 &css_pipe->binary_params_cs[j][i]);

	j = IPU3_CSS_AUX_FRAME_REF;
	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		imgu_dmamap_free(imgu,
				 &css_pipe->aux_frames[j].mem[i]);

	j = IPU3_CSS_AUX_FRAME_TNR;
	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		imgu_dmamap_free(imgu,
				 &css_pipe->aux_frames[j].mem[i]);
}

static int imgu_css_binary_preallocate(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	unsigned int i, j;

	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

	for (j = IMGU_ABI_PARAM_CLASS_CONFIG;
	     j < IMGU_ABI_PARAM_CLASS_NUM; j++)
		for (i = 0; i < IMGU_ABI_NUM_MEMORIES; i++)
			if (!imgu_dmamap_alloc(imgu,
					       &css_pipe->binary_params_cs[j - 1][i],
					       CSS_ABI_SIZE))
				goto out_of_memory;

	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		if (!imgu_dmamap_alloc(imgu,
				       &css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].
				       mem[i], CSS_BDS_SIZE))
			goto out_of_memory;

	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		if (!imgu_dmamap_alloc(imgu,
				       &css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].
				       mem[i], CSS_GDC_SIZE))
			goto out_of_memory;

	return 0;

out_of_memory:
	imgu_css_binary_cleanup(css, pipe);
	return -ENOMEM;
}

/* allocate binary-specific resources */
static int imgu_css_binary_setup(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];
	struct imgu_fw_info *bi = &css->fwp->binary_header[css_pipe->bindex];
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	int i, j, size;
	static const int BYPC = 2;	/* Bytes per component */
	unsigned int w, h;

	/* Allocate parameter memory blocks for this binary */

	for (j = IMGU_ABI_PARAM_CLASS_CONFIG; j < IMGU_ABI_PARAM_CLASS_NUM; j++)
		for (i = 0; i < IMGU_ABI_NUM_MEMORIES; i++) {
			if (imgu_css_dma_buffer_resize(
			    imgu,
			    &css_pipe->binary_params_cs[j - 1][i],
			    bi->info.isp.sp.mem_initializers.params[j][i].size))
				goto out_of_memory;
		}

	/* Allocate internal frame buffers */

	/* Reference frames for DVS, FRAME_FORMAT_YUV420_16 */
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperpixel = BYPC;
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].width =
					css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].height =
				ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].height,
				      IMGU_DVS_BLOCK_H) + 2 * IMGU_GDC_BUF_Y;
	h = css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].height;
	w = ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width,
		  2 * IPU3_UAPI_ISP_VEC_ELEMS) + 2 * IMGU_GDC_BUF_X;
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperline =
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperpixel * w;
	size = w * h * BYPC + (w / 2) * (h / 2) * BYPC * 2;
	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		if (imgu_css_dma_buffer_resize(
			imgu,
			&css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].mem[i],
			size))
			goto out_of_memory;

	/* TNR frames for temporal noise reduction, FRAME_FORMAT_YUV_LINE */
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].bytesperpixel = 1;
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].width =
			roundup(css_pipe->rect[IPU3_CSS_RECT_GDC].width,
				bi->info.isp.sp.block.block_width *
				IPU3_UAPI_ISP_VEC_ELEMS);
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].height =
			roundup(css_pipe->rect[IPU3_CSS_RECT_GDC].height,
				bi->info.isp.sp.block.output_block_height);

	w = css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].width;
	css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].bytesperline = w;
	h = css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].height;
	size = w * ALIGN(h * 3 / 2 + 3, 2);	/* +3 for vf_pp prefetch */
	for (i = 0; i < IPU3_CSS_AUX_FRAMES; i++)
		if (imgu_css_dma_buffer_resize(
			imgu,
			&css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_TNR].mem[i],
			size))
			goto out_of_memory;

	return 0;

out_of_memory:
	imgu_css_binary_cleanup(css, pipe);
	return -ENOMEM;
}

int imgu_css_start_streaming(struct imgu_css *css)
{
	u32 data;
	int r, pipe;

	if (css->streaming)
		return -EPROTO;

	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = imgu_css_binary_setup(css, pipe);
		if (r < 0)
			return r;
	}

	r = imgu_css_hw_init(css);
	if (r < 0)
		return r;

	r = imgu_css_hw_start(css);
	if (r < 0)
		goto fail;

	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = imgu_css_pipeline_init(css, pipe);
		if (r < 0)
			goto fail;
	}

	css->streaming = true;

	imgu_css_hw_enable_irq(css);

	/* Initialize parameters to default */
	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = imgu_css_set_parameters(css, pipe, NULL);
		if (r < 0)
			goto fail;
	}

	while (!(r = imgu_css_dequeue_data(css, IMGU_ABI_QUEUE_A_ID, &data)))
		;
	if (r != -EBUSY)
		goto fail;

	while (!(r = imgu_css_dequeue_data(css, IMGU_ABI_QUEUE_B_ID, &data)))
		;
	if (r != -EBUSY)
		goto fail;

	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe,
					IMGU_ABI_EVENT_START_STREAM |
					pipe << 16);
		if (r < 0)
			goto fail;
	}

	return 0;

fail:
	css->streaming = false;
	imgu_css_hw_cleanup(css);
	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		imgu_css_pipeline_cleanup(css, pipe);
		imgu_css_binary_cleanup(css, pipe);
	}

	return r;
}

void imgu_css_stop_streaming(struct imgu_css *css)
{
	struct imgu_css_buffer *b, *b0;
	int q, r, pipe;

	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe,
					IMGU_ABI_EVENT_STOP_STREAM);
		if (r < 0)
			dev_warn(css->dev, "failed on stop stream event\n");
	}

	if (!css->streaming)
		return;

	imgu_css_hw_stop(css);

	imgu_css_hw_cleanup(css);

	for_each_set_bit(pipe, css->enabled_pipes, IMGU_MAX_PIPE_NUM) {
		struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

		imgu_css_pipeline_cleanup(css, pipe);

		spin_lock(&css_pipe->qlock);
		for (q = 0; q < IPU3_CSS_QUEUES; q++)
			list_for_each_entry_safe(b, b0,
						 &css_pipe->queue[q].bufs,
						 list) {
				b->state = IPU3_CSS_BUFFER_FAILED;
				list_del(&b->list);
			}
		spin_unlock(&css_pipe->qlock);
	}

	css->streaming = false;
}

bool imgu_css_pipe_queue_empty(struct imgu_css *css, unsigned int pipe)
{
	int q;
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

	spin_lock(&css_pipe->qlock);
	for (q = 0; q < IPU3_CSS_QUEUES; q++)
		if (!list_empty(&css_pipe->queue[q].bufs))
			break;
	spin_unlock(&css_pipe->qlock);
	return (q == IPU3_CSS_QUEUES);
}

bool imgu_css_queue_empty(struct imgu_css *css)
{
	unsigned int pipe;
	bool ret = false;

	for (pipe = 0; pipe < IMGU_MAX_PIPE_NUM; pipe++)
		ret &= imgu_css_pipe_queue_empty(css, pipe);

	return ret;
}

bool imgu_css_is_streaming(struct imgu_css *css)
{
	return css->streaming;
}

static int imgu_css_map_init(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];
	unsigned int p, q, i;

	/* Allocate and map common structures with imgu hardware */
	for (p = 0; p < IPU3_CSS_PIPE_ID_NUM; p++)
		for (i = 0; i < IMGU_ABI_MAX_STAGES; i++) {
			if (!imgu_dmamap_alloc(imgu,
					       &css_pipe->
					       xmem_sp_stage_ptrs[p][i],
					       sizeof(struct imgu_abi_sp_stage)))
				return -ENOMEM;
			if (!imgu_dmamap_alloc(imgu,
					       &css_pipe->
					       xmem_isp_stage_ptrs[p][i],
					       sizeof(struct imgu_abi_isp_stage)))
				return -ENOMEM;
		}

	if (!imgu_dmamap_alloc(imgu, &css_pipe->sp_ddr_ptrs,
			       ALIGN(sizeof(struct imgu_abi_ddr_address_map),
				     IMGU_ABI_ISP_DDR_WORD_BYTES)))
		return -ENOMEM;

	for (q = 0; q < IPU3_CSS_QUEUES; q++) {
		unsigned int abi_buf_num = ARRAY_SIZE(css_pipe->abi_buffers[q]);

		for (i = 0; i < abi_buf_num; i++)
			if (!imgu_dmamap_alloc(imgu,
					       &css_pipe->abi_buffers[q][i],
					       sizeof(struct imgu_abi_buffer)))
				return -ENOMEM;
	}

	if (imgu_css_binary_preallocate(css, pipe)) {
		imgu_css_binary_cleanup(css, pipe);
		return -ENOMEM;
	}

	return 0;
}

static void imgu_css_pipe_cleanup(struct imgu_css *css, unsigned int pipe)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];
	unsigned int p, q, i, abi_buf_num;

	imgu_css_binary_cleanup(css, pipe);

	for (q = 0; q < IPU3_CSS_QUEUES; q++) {
		abi_buf_num = ARRAY_SIZE(css_pipe->abi_buffers[q]);
		for (i = 0; i < abi_buf_num; i++)
			imgu_dmamap_free(imgu, &css_pipe->abi_buffers[q][i]);
	}

	for (p = 0; p < IPU3_CSS_PIPE_ID_NUM; p++)
		for (i = 0; i < IMGU_ABI_MAX_STAGES; i++) {
			imgu_dmamap_free(imgu,
					 &css_pipe->xmem_sp_stage_ptrs[p][i]);
			imgu_dmamap_free(imgu,
					 &css_pipe->xmem_isp_stage_ptrs[p][i]);
		}

	imgu_dmamap_free(imgu, &css_pipe->sp_ddr_ptrs);
}

void imgu_css_cleanup(struct imgu_css *css)
{
	struct imgu_device *imgu = dev_get_drvdata(css->dev);
	unsigned int pipe;

	imgu_css_stop_streaming(css);
	for (pipe = 0; pipe < IMGU_MAX_PIPE_NUM; pipe++)
		imgu_css_pipe_cleanup(css, pipe);
	imgu_dmamap_free(imgu, &css->xmem_sp_group_ptrs);
	imgu_css_fw_cleanup(css);
}

int imgu_css_init(struct device *dev, struct imgu_css *css,
		  void __iomem *base, int length)
{
	struct imgu_device *imgu = dev_get_drvdata(dev);
	int r, q, pipe;

	/* Initialize main data structure */
	css->dev = dev;
	css->base = base;
	css->iomem_length = length;

	for (pipe = 0; pipe < IMGU_MAX_PIPE_NUM; pipe++) {
		struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

		css_pipe->vf_output_en = false;
		spin_lock_init(&css_pipe->qlock);
		css_pipe->bindex = IPU3_CSS_DEFAULT_BINARY;
		css_pipe->pipe_id = IPU3_CSS_PIPE_ID_VIDEO;
		for (q = 0; q < IPU3_CSS_QUEUES; q++) {
			r = imgu_css_queue_init(&css_pipe->queue[q], NULL, 0);
			if (r)
				return r;
		}
		r = imgu_css_map_init(css, pipe);
		if (r) {
			imgu_css_cleanup(css);
			return r;
		}
	}
	if (!imgu_dmamap_alloc(imgu, &css->xmem_sp_group_ptrs,
			       sizeof(struct imgu_abi_sp_group)))
		return -ENOMEM;

	r = imgu_css_fw_init(css);
	if (r)
		return r;

	return 0;
}

static u32 imgu_css_adjust(u32 res, u32 align)
{
	u32 val = max_t(u32, IPU3_CSS_MIN_RES, res);

	return DIV_ROUND_CLOSEST(val, align) * align;
}

/* Select a binary matching the required resolutions and formats */
static int imgu_css_find_binary(struct imgu_css *css,
				unsigned int pipe,
				struct imgu_css_queue queue[IPU3_CSS_QUEUES],
				struct v4l2_rect rects[IPU3_CSS_RECTS])
{
	const int binary_nr = css->fwp->file_header.binary_nr;
	unsigned int binary_mode =
		(css->pipes[pipe].pipe_id == IPU3_CSS_PIPE_ID_CAPTURE) ?
		IA_CSS_BINARY_MODE_PRIMARY : IA_CSS_BINARY_MODE_VIDEO;
	const struct v4l2_pix_format_mplane *in =
					&queue[IPU3_CSS_QUEUE_IN].fmt.mpix;
	const struct v4l2_pix_format_mplane *out =
					&queue[IPU3_CSS_QUEUE_OUT].fmt.mpix;
	const struct v4l2_pix_format_mplane *vf =
					&queue[IPU3_CSS_QUEUE_VF].fmt.mpix;
	u32 stripe_w = 0, stripe_h = 0;
	const char *name;
	int i, j;

	if (!imgu_css_queue_enabled(&queue[IPU3_CSS_QUEUE_IN]))
		return -EINVAL;

	/* Find out the strip size boundary */
	for (i = 0; i < binary_nr; i++) {
		struct imgu_fw_info *bi = &css->fwp->binary_header[i];

		u32 max_width = bi->info.isp.sp.output.max_width;
		u32 max_height = bi->info.isp.sp.output.max_height;

		if (bi->info.isp.sp.iterator.num_stripes <= 1) {
			stripe_w = stripe_w ?
				min(stripe_w, max_width) : max_width;
			stripe_h = stripe_h ?
				min(stripe_h, max_height) : max_height;
		}
	}

	for (i = 0; i < binary_nr; i++) {
		struct imgu_fw_info *bi = &css->fwp->binary_header[i];
		enum imgu_abi_frame_format q_fmt;

		name = (void *)css->fwp + bi->blob.prog_name_offset;

		/* Check that binary supports memory-to-memory processing */
		if (bi->info.isp.sp.input.source !=
		    IMGU_ABI_BINARY_INPUT_SOURCE_MEMORY)
			continue;

		/* Check that binary supports raw10 input */
		if (!bi->info.isp.sp.enable.input_feeder &&
		    !bi->info.isp.sp.enable.input_raw)
			continue;

		/* Check binary mode */
		if (bi->info.isp.sp.pipeline.mode != binary_mode)
			continue;

		/* Since input is RGGB bayer, need to process colors */
		if (bi->info.isp.sp.enable.luma_only)
			continue;

		if (in->width < bi->info.isp.sp.input.min_width ||
		    in->width > bi->info.isp.sp.input.max_width ||
		    in->height < bi->info.isp.sp.input.min_height ||
		    in->height > bi->info.isp.sp.input.max_height)
			continue;

		if (imgu_css_queue_enabled(&queue[IPU3_CSS_QUEUE_OUT])) {
			if (bi->info.isp.num_output_pins <= 0)
				continue;

			q_fmt = queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;
			for (j = 0; j < bi->info.isp.num_output_formats; j++)
				if (bi->info.isp.output_formats[j] == q_fmt)
					break;
			if (j >= bi->info.isp.num_output_formats)
				continue;

			if (out->width < bi->info.isp.sp.output.min_width ||
			    out->width > bi->info.isp.sp.output.max_width ||
			    out->height < bi->info.isp.sp.output.min_height ||
			    out->height > bi->info.isp.sp.output.max_height)
				continue;

			if (out->width > bi->info.isp.sp.internal.max_width ||
			    out->height > bi->info.isp.sp.internal.max_height)
				continue;
		}

		if (imgu_css_queue_enabled(&queue[IPU3_CSS_QUEUE_VF])) {
			if (bi->info.isp.num_output_pins <= 1)
				continue;

			q_fmt = queue[IPU3_CSS_QUEUE_VF].css_fmt->frame_format;
			for (j = 0; j < bi->info.isp.num_output_formats; j++)
				if (bi->info.isp.output_formats[j] == q_fmt)
					break;
			if (j >= bi->info.isp.num_output_formats)
				continue;

			if (vf->width < bi->info.isp.sp.output.min_width ||
			    vf->width > bi->info.isp.sp.output.max_width ||
			    vf->height < bi->info.isp.sp.output.min_height ||
			    vf->height > bi->info.isp.sp.output.max_height)
				continue;
		}

		/* All checks passed, select the binary */
		dev_dbg(css->dev, "using binary %s id = %u\n", name,
			bi->info.isp.sp.id);
		return i;
	}

	/* Can not find suitable binary for these parameters */
	return -EINVAL;
}

/*
 * Check that there is a binary matching requirements. Parameters may be
 * NULL indicating disabled input/output. Return negative if given
 * parameters can not be supported or on error, zero or positive indicating
 * found binary number. May modify the given parameters if not exact match
 * is found.
 */
int imgu_css_fmt_try(struct imgu_css *css,
		     struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES],
		     struct v4l2_rect *rects[IPU3_CSS_RECTS],
		     unsigned int pipe)
{
	static const u32 EFF_ALIGN_W = 2;
	static const u32 BDS_ALIGN_W = 4;
	static const u32 OUT_ALIGN_W = 8;
	static const u32 OUT_ALIGN_H = 4;
	static const u32 VF_ALIGN_W  = 2;
	static const char *qnames[IPU3_CSS_QUEUES] = {
		[IPU3_CSS_QUEUE_IN] = "in",
		[IPU3_CSS_QUEUE_PARAMS]    = "params",
		[IPU3_CSS_QUEUE_OUT] = "out",
		[IPU3_CSS_QUEUE_VF] = "vf",
		[IPU3_CSS_QUEUE_STAT_3A]   = "3a",
	};
	static const char *rnames[IPU3_CSS_RECTS] = {
		[IPU3_CSS_RECT_EFFECTIVE] = "effective resolution",
		[IPU3_CSS_RECT_BDS]       = "bayer-domain scaled resolution",
		[IPU3_CSS_RECT_ENVELOPE]  = "DVS envelope size",
		[IPU3_CSS_RECT_GDC]  = "GDC output res",
	};
	struct v4l2_rect r[IPU3_CSS_RECTS] = { };
	struct v4l2_rect *const eff = &r[IPU3_CSS_RECT_EFFECTIVE];
	struct v4l2_rect *const bds = &r[IPU3_CSS_RECT_BDS];
	struct v4l2_rect *const env = &r[IPU3_CSS_RECT_ENVELOPE];
	struct v4l2_rect *const gdc = &r[IPU3_CSS_RECT_GDC];
	struct imgu_css_queue *q;
	struct v4l2_pix_format_mplane *in, *out, *vf;
	int i, s, ret;

	q = kcalloc(IPU3_CSS_QUEUES, sizeof(struct imgu_css_queue), GFP_KERNEL);
	if (!q)
		return -ENOMEM;

	in  = &q[IPU3_CSS_QUEUE_IN].fmt.mpix;
	out = &q[IPU3_CSS_QUEUE_OUT].fmt.mpix;
	vf  = &q[IPU3_CSS_QUEUE_VF].fmt.mpix;

	/* Adjust all formats, get statistics buffer sizes and formats */
	for (i = 0; i < IPU3_CSS_QUEUES; i++) {
		if (fmts[i])
			dev_dbg(css->dev, "%s %s: (%i,%i) fmt 0x%x\n", __func__,
				qnames[i], fmts[i]->width, fmts[i]->height,
				fmts[i]->pixelformat);
		else
			dev_dbg(css->dev, "%s %s: (not set)\n", __func__,
				qnames[i]);
		if (imgu_css_queue_init(&q[i], fmts[i],
					IPU3_CSS_QUEUE_TO_FLAGS(i))) {
			dev_notice(css->dev, "can not initialize queue %s\n",
				   qnames[i]);
			ret = -EINVAL;
			goto out;
		}
	}
	for (i = 0; i < IPU3_CSS_RECTS; i++) {
		if (rects[i]) {
			dev_dbg(css->dev, "%s %s: (%i,%i)\n", __func__,
				rnames[i], rects[i]->width, rects[i]->height);
			r[i].width  = rects[i]->width;
			r[i].height = rects[i]->height;
		} else {
			dev_dbg(css->dev, "%s %s: (not set)\n", __func__,
				rnames[i]);
		}
		/* For now, force known good resolutions */
		r[i].left = 0;
		r[i].top  = 0;
	}

	/* Always require one input and vf only if out is also enabled */
	if (!imgu_css_queue_enabled(&q[IPU3_CSS_QUEUE_IN]) ||
	    !imgu_css_queue_enabled(&q[IPU3_CSS_QUEUE_OUT])) {
		dev_warn(css->dev, "required queues are disabled\n");
		ret = -EINVAL;
		goto out;
	}

	if (!imgu_css_queue_enabled(&q[IPU3_CSS_QUEUE_OUT])) {
		out->width = in->width;
		out->height = in->height;
	}
	if (eff->width <= 0 || eff->height <= 0) {
		eff->width = in->width;
		eff->height = in->height;
	}
	if (bds->width <= 0 || bds->height <= 0) {
		bds->width = out->width;
		bds->height = out->height;
	}
	if (gdc->width <= 0 || gdc->height <= 0) {
		gdc->width = out->width;
		gdc->height = out->height;
	}

	in->width   = imgu_css_adjust(in->width, 1);
	in->height  = imgu_css_adjust(in->height, 1);
	eff->width  = imgu_css_adjust(eff->width, EFF_ALIGN_W);
	eff->height = imgu_css_adjust(eff->height, 1);
	bds->width  = imgu_css_adjust(bds->width, BDS_ALIGN_W);
	bds->height = imgu_css_adjust(bds->height, 1);
	gdc->width  = imgu_css_adjust(gdc->width, OUT_ALIGN_W);
	gdc->height = imgu_css_adjust(gdc->height, OUT_ALIGN_H);
	out->width  = imgu_css_adjust(out->width, OUT_ALIGN_W);
	out->height = imgu_css_adjust(out->height, OUT_ALIGN_H);
	vf->width   = imgu_css_adjust(vf->width, VF_ALIGN_W);
	vf->height  = imgu_css_adjust(vf->height, 1);

	s = (bds->width - gdc->width) / 2;
	env->width = s < MIN_ENVELOPE ? MIN_ENVELOPE : s;
	s = (bds->height - gdc->height) / 2;
	env->height = s < MIN_ENVELOPE ? MIN_ENVELOPE : s;

	ret = imgu_css_find_binary(css, pipe, q, r);
	if (ret < 0) {
		dev_err(css->dev, "failed to find suitable binary\n");
		ret = -EINVAL;
		goto out;
	}
	css->pipes[pipe].bindex = ret;

	dev_dbg(css->dev, "Binary index %d for pipe %d found.",
		css->pipes[pipe].bindex, pipe);

	/* Final adjustment and set back the queried formats */
	for (i = 0; i < IPU3_CSS_QUEUES; i++) {
		if (fmts[i]) {
			if (imgu_css_queue_init(&q[i], &q[i].fmt.mpix,
						IPU3_CSS_QUEUE_TO_FLAGS(i))) {
				dev_err(css->dev,
					"final resolution adjustment failed\n");
				ret = -EINVAL;
				goto out;
			}
			*fmts[i] = q[i].fmt.mpix;
		}
	}

	for (i = 0; i < IPU3_CSS_RECTS; i++)
		if (rects[i])
			*rects[i] = r[i];

	dev_dbg(css->dev,
		"in(%u,%u) if(%u,%u) ds(%u,%u) gdc(%u,%u) out(%u,%u) vf(%u,%u)",
		 in->width, in->height, eff->width, eff->height,
		 bds->width, bds->height, gdc->width, gdc->height,
		 out->width, out->height, vf->width, vf->height);

	ret = 0;
out:
	kfree(q);
	return ret;
}

int imgu_css_fmt_set(struct imgu_css *css,
		     struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES],
		     struct v4l2_rect *rects[IPU3_CSS_RECTS],
		     unsigned int pipe)
{
	struct v4l2_rect rect_data[IPU3_CSS_RECTS];
	struct v4l2_rect *all_rects[IPU3_CSS_RECTS];
	int i, r;
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

	for (i = 0; i < IPU3_CSS_RECTS; i++) {
		if (rects[i])
			rect_data[i] = *rects[i];
		else
			memset(&rect_data[i], 0, sizeof(rect_data[i]));
		all_rects[i] = &rect_data[i];
	}
	r = imgu_css_fmt_try(css, fmts, all_rects, pipe);
	if (r < 0)
		return r;

	for (i = 0; i < IPU3_CSS_QUEUES; i++)
		if (imgu_css_queue_init(&css_pipe->queue[i], fmts[i],
					IPU3_CSS_QUEUE_TO_FLAGS(i)))
			return -EINVAL;
	for (i = 0; i < IPU3_CSS_RECTS; i++) {
		css_pipe->rect[i] = rect_data[i];
		if (rects[i])
			*rects[i] = rect_data[i];
	}

	return 0;
}

int imgu_css_meta_fmt_set(struct v4l2_meta_format *fmt)
{
	switch (fmt->dataformat) {
	case V4L2_META_FMT_IPU3_PARAMS:
		fmt->buffersize = sizeof(struct ipu3_uapi_params);
		break;
	case V4L2_META_FMT_IPU3_STAT_3A:
		fmt->buffersize = sizeof(struct ipu3_uapi_stats_3a);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Queue given buffer to CSS. imgu_css_buf_prepare() must have been first
 * called for the buffer. May be called from interrupt context.
 * Returns 0 on success, -EBUSY if the buffer queue is full, or some other
 * code on error conditions.
 */
int imgu_css_buf_queue(struct imgu_css *css, unsigned int pipe,
		       struct imgu_css_buffer *b)
{
	struct imgu_abi_buffer *abi_buf;
	struct imgu_addr_t *buf_addr;
	u32 data;
	int r;
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];

	if (!css->streaming)
		return -EPROTO;	/* CSS or buffer in wrong state */

	if (b->queue >= IPU3_CSS_QUEUES || !imgu_css_queues[b->queue].qid)
		return -EINVAL;

	b->queue_pos = imgu_css_queue_pos(css, imgu_css_queues[b->queue].qid,
					  pipe);

	if (b->queue_pos >= ARRAY_SIZE(css->pipes[pipe].abi_buffers[b->queue]))
		return -EIO;
	abi_buf = css->pipes[pipe].abi_buffers[b->queue][b->queue_pos].vaddr;

	/* Fill struct abi_buffer for firmware */
	memset(abi_buf, 0, sizeof(*abi_buf));

	buf_addr = (void *)abi_buf + imgu_css_queues[b->queue].ptr_ofs;
	*(imgu_addr_t *)buf_addr = b->daddr;

	if (b->queue == IPU3_CSS_QUEUE_STAT_3A)
		abi_buf->payload.s3a.data.dmem.s3a_tbl = b->daddr;

	if (b->queue == IPU3_CSS_QUEUE_OUT)
		abi_buf->payload.frame.padded_width =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].width_pad;

	if (b->queue == IPU3_CSS_QUEUE_VF)
		abi_buf->payload.frame.padded_width =
					css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad;

	spin_lock(&css_pipe->qlock);
	list_add_tail(&b->list, &css_pipe->queue[b->queue].bufs);
	spin_unlock(&css_pipe->qlock);
	b->state = IPU3_CSS_BUFFER_QUEUED;

	data = css->pipes[pipe].abi_buffers[b->queue][b->queue_pos].daddr;
	r = imgu_css_queue_data(css, imgu_css_queues[b->queue].qid,
				pipe, data);
	if (r < 0)
		goto queueing_failed;

	data = IMGU_ABI_EVENT_BUFFER_ENQUEUED(pipe,
					      imgu_css_queues[b->queue].qid);
	r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe, data);
	if (r < 0)
		goto queueing_failed;

	dev_dbg(css->dev, "queued buffer %p to css queue %i in pipe %d\n",
		b, b->queue, pipe);

	return 0;

queueing_failed:
	b->state = (r == -EBUSY || r == -EAGAIN) ?
		IPU3_CSS_BUFFER_NEW : IPU3_CSS_BUFFER_FAILED;
	list_del(&b->list);

	return r;
}

/*
 * Get next ready CSS buffer. Returns -EAGAIN in which case the function
 * should be called again, or -EBUSY which means that there are no more
 * buffers available. May be called from interrupt context.
 */
struct imgu_css_buffer *imgu_css_buf_dequeue(struct imgu_css *css)
{
	static const unsigned char evtype_to_queue[] = {
		[IMGU_ABI_EVTTYPE_INPUT_FRAME_DONE] = IPU3_CSS_QUEUE_IN,
		[IMGU_ABI_EVTTYPE_OUT_FRAME_DONE] = IPU3_CSS_QUEUE_OUT,
		[IMGU_ABI_EVTTYPE_VF_OUT_FRAME_DONE] = IPU3_CSS_QUEUE_VF,
		[IMGU_ABI_EVTTYPE_3A_STATS_DONE] = IPU3_CSS_QUEUE_STAT_3A,
	};
	struct imgu_css_buffer *b = ERR_PTR(-EAGAIN);
	u32 event, daddr;
	int evtype, pipe, pipeid, queue, qid, r;
	struct imgu_css_pipe *css_pipe;

	if (!css->streaming)
		return ERR_PTR(-EPROTO);

	r = imgu_css_dequeue_data(css, IMGU_ABI_QUEUE_EVENT_ID, &event);
	if (r < 0)
		return ERR_PTR(r);

	evtype = (event & IMGU_ABI_EVTTYPE_EVENT_MASK) >>
		  IMGU_ABI_EVTTYPE_EVENT_SHIFT;

	switch (evtype) {
	case IMGU_ABI_EVTTYPE_OUT_FRAME_DONE:
	case IMGU_ABI_EVTTYPE_VF_OUT_FRAME_DONE:
	case IMGU_ABI_EVTTYPE_3A_STATS_DONE:
	case IMGU_ABI_EVTTYPE_INPUT_FRAME_DONE:
		pipe = (event & IMGU_ABI_EVTTYPE_PIPE_MASK) >>
			IMGU_ABI_EVTTYPE_PIPE_SHIFT;
		pipeid = (event & IMGU_ABI_EVTTYPE_PIPEID_MASK) >>
			IMGU_ABI_EVTTYPE_PIPEID_SHIFT;
		queue = evtype_to_queue[evtype];
		qid = imgu_css_queues[queue].qid;

		if (pipe >= IMGU_MAX_PIPE_NUM) {
			dev_err(css->dev, "Invalid pipe: %i\n", pipe);
			return ERR_PTR(-EIO);
		}

		if (qid >= IMGU_ABI_QUEUE_NUM) {
			dev_err(css->dev, "Invalid qid: %i\n", qid);
			return ERR_PTR(-EIO);
		}
		css_pipe = &css->pipes[pipe];
		dev_dbg(css->dev,
			"event: buffer done 0x%x queue %i pipe %i pipeid %i\n",
			event, queue, pipe, pipeid);

		r = imgu_css_dequeue_data(css, qid, &daddr);
		if (r < 0) {
			dev_err(css->dev, "failed to dequeue buffer\n");
			/* Force real error, not -EBUSY */
			return ERR_PTR(-EIO);
		}

		r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe,
					IMGU_ABI_EVENT_BUFFER_DEQUEUED(qid));
		if (r < 0) {
			dev_err(css->dev, "failed to queue event\n");
			return ERR_PTR(-EIO);
		}

		spin_lock(&css_pipe->qlock);
		if (list_empty(&css_pipe->queue[queue].bufs)) {
			spin_unlock(&css_pipe->qlock);
			dev_err(css->dev, "event on empty queue\n");
			return ERR_PTR(-EIO);
		}
		b = list_first_entry(&css_pipe->queue[queue].bufs,
				     struct imgu_css_buffer, list);
		if (queue != b->queue ||
		    daddr != css_pipe->abi_buffers
			[b->queue][b->queue_pos].daddr) {
			spin_unlock(&css_pipe->qlock);
			dev_err(css->dev, "dequeued bad buffer 0x%x\n", daddr);
			return ERR_PTR(-EIO);
		}

		dev_dbg(css->dev, "buffer 0x%8x done from pipe %d\n", daddr, pipe);
		b->pipe = pipe;
		b->state = IPU3_CSS_BUFFER_DONE;
		list_del(&b->list);
		spin_unlock(&css_pipe->qlock);
		break;
	case IMGU_ABI_EVTTYPE_PIPELINE_DONE:
		pipe = (event & IMGU_ABI_EVTTYPE_PIPE_MASK) >>
			IMGU_ABI_EVTTYPE_PIPE_SHIFT;
		if (pipe >= IMGU_MAX_PIPE_NUM) {
			dev_err(css->dev, "Invalid pipe: %i\n", pipe);
			return ERR_PTR(-EIO);
		}

		css_pipe = &css->pipes[pipe];
		dev_dbg(css->dev, "event: pipeline done 0x%8x for pipe %d\n",
			event, pipe);
		break;
	case IMGU_ABI_EVTTYPE_TIMER:
		r = imgu_css_dequeue_data(css, IMGU_ABI_QUEUE_EVENT_ID, &event);
		if (r < 0)
			return ERR_PTR(r);

		if ((event & IMGU_ABI_EVTTYPE_EVENT_MASK) >>
		    IMGU_ABI_EVTTYPE_EVENT_SHIFT == IMGU_ABI_EVTTYPE_TIMER)
			dev_dbg(css->dev, "event: timer\n");
		else
			dev_warn(css->dev, "half of timer event missing\n");
		break;
	case IMGU_ABI_EVTTYPE_FW_WARNING:
		dev_warn(css->dev, "event: firmware warning 0x%x\n", event);
		break;
	case IMGU_ABI_EVTTYPE_FW_ASSERT:
		dev_err(css->dev,
			"event: firmware assert 0x%x module_id %i line_no %i\n",
			event,
			(event & IMGU_ABI_EVTTYPE_MODULEID_MASK) >>
			IMGU_ABI_EVTTYPE_MODULEID_SHIFT,
			swab16((event & IMGU_ABI_EVTTYPE_LINENO_MASK) >>
			       IMGU_ABI_EVTTYPE_LINENO_SHIFT));
		break;
	default:
		dev_warn(css->dev, "received unknown event 0x%x\n", event);
	}

	return b;
}

/*
 * Get a new set of parameters from pool and initialize them based on
 * the parameters params, gdc, and obgrid. Any of these may be NULL,
 * in which case the previously set parameters are used.
 * If parameters haven't been set previously, initialize from scratch.
 *
 * Return index to css->parameter_set_info which has the newly created
 * parameters or negative value on error.
 */
int imgu_css_set_parameters(struct imgu_css *css, unsigned int pipe,
			    struct ipu3_uapi_params *set_params)
{
	static const unsigned int queue_id = IMGU_ABI_QUEUE_A_ID;
	struct imgu_css_pipe *css_pipe = &css->pipes[pipe];
	const int stage = 0;
	const struct imgu_fw_info *bi;
	int obgrid_size;
	unsigned int stripes, i;
	struct ipu3_uapi_flags *use = set_params ? &set_params->use : NULL;

	/* Destination buffers which are filled here */
	struct imgu_abi_parameter_set_info *param_set;
	struct imgu_abi_acc_param *acc = NULL;
	struct imgu_abi_gdc_warp_param *gdc = NULL;
	struct ipu3_uapi_obgrid_param *obgrid = NULL;
	const struct imgu_css_map *map;
	void *vmem0 = NULL;
	void *dmem0 = NULL;

	enum imgu_abi_memories m;
	int r = -EBUSY;

	if (!css->streaming)
		return -EPROTO;

	dev_dbg(css->dev, "%s for pipe %d", __func__, pipe);

	bi = &css->fwp->binary_header[css_pipe->bindex];
	obgrid_size = imgu_css_fw_obgrid_size(bi);
	stripes = bi->info.isp.sp.iterator.num_stripes ? : 1;

	imgu_css_pool_get(&css_pipe->pool.parameter_set_info);
	param_set = imgu_css_pool_last(&css_pipe->pool.parameter_set_info,
				       0)->vaddr;

	/* Get a new acc only if new parameters given, or none yet */
	map = imgu_css_pool_last(&css_pipe->pool.acc, 0);
	if (set_params || !map->vaddr) {
		imgu_css_pool_get(&css_pipe->pool.acc);
		map = imgu_css_pool_last(&css_pipe->pool.acc, 0);
		acc = map->vaddr;
	}

	/* Get new VMEM0 only if needed, or none yet */
	m = IMGU_ABI_MEM_ISP_VMEM0;
	map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 0);
	if (!map->vaddr || (set_params && (set_params->use.lin_vmem_params ||
					   set_params->use.tnr3_vmem_params ||
					   set_params->use.xnr3_vmem_params))) {
		imgu_css_pool_get(&css_pipe->pool.binary_params_p[m]);
		map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 0);
		vmem0 = map->vaddr;
	}

	/* Get new DMEM0 only if needed, or none yet */
	m = IMGU_ABI_MEM_ISP_DMEM0;
	map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 0);
	if (!map->vaddr || (set_params && (set_params->use.tnr3_dmem_params ||
					   set_params->use.xnr3_dmem_params))) {
		imgu_css_pool_get(&css_pipe->pool.binary_params_p[m]);
		map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 0);
		dmem0 = map->vaddr;
	}

	/* Configure acc parameter cluster */
	if (acc) {
		/* get acc_old */
		map = imgu_css_pool_last(&css_pipe->pool.acc, 1);
		/* user acc */
		r = imgu_css_cfg_acc(css, pipe, use, acc, map->vaddr,
			set_params ? &set_params->acc_param : NULL);
		if (r < 0)
			goto fail;
	}

	/* Configure late binding parameters */
	if (vmem0) {
		m = IMGU_ABI_MEM_ISP_VMEM0;
		map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 1);
		r = imgu_css_cfg_vmem0(css, pipe, use, vmem0,
				       map->vaddr, set_params);
		if (r < 0)
			goto fail;
	}

	if (dmem0) {
		m = IMGU_ABI_MEM_ISP_DMEM0;
		map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 1);
		r = imgu_css_cfg_dmem0(css, pipe, use, dmem0,
				       map->vaddr, set_params);
		if (r < 0)
			goto fail;
	}

	/* Get a new gdc only if a new gdc is given, or none yet */
	if (bi->info.isp.sp.enable.dvs_6axis) {
		unsigned int a = IPU3_CSS_AUX_FRAME_REF;
		unsigned int g = IPU3_CSS_RECT_GDC;
		unsigned int e = IPU3_CSS_RECT_ENVELOPE;

		map = imgu_css_pool_last(&css_pipe->pool.gdc, 0);
		if (!map->vaddr) {
			imgu_css_pool_get(&css_pipe->pool.gdc);
			map = imgu_css_pool_last(&css_pipe->pool.gdc, 0);
			gdc = map->vaddr;
			imgu_css_cfg_gdc_table(map->vaddr,
				css_pipe->aux_frames[a].bytesperline /
				css_pipe->aux_frames[a].bytesperpixel,
				css_pipe->aux_frames[a].height,
				css_pipe->rect[g].width,
				css_pipe->rect[g].height,
				css_pipe->rect[e].width,
				css_pipe->rect[e].height);
		}
	}

	/* Get a new obgrid only if a new obgrid is given, or none yet */
	map = imgu_css_pool_last(&css_pipe->pool.obgrid, 0);
	if (!map->vaddr || (set_params && set_params->use.obgrid_param)) {
		imgu_css_pool_get(&css_pipe->pool.obgrid);
		map = imgu_css_pool_last(&css_pipe->pool.obgrid, 0);
		obgrid = map->vaddr;

		/* Configure optical black level grid (obgrid) */
		if (set_params && set_params->use.obgrid_param)
			for (i = 0; i < obgrid_size / sizeof(*obgrid); i++)
				obgrid[i] = set_params->obgrid_param;
		else
			memset(obgrid, 0, obgrid_size);
	}

	/* Configure parameter set info, queued to `queue_id' */

	memset(param_set, 0, sizeof(*param_set));
	map = imgu_css_pool_last(&css_pipe->pool.acc, 0);
	param_set->mem_map.acc_cluster_params_for_sp = map->daddr;

	map = imgu_css_pool_last(&css_pipe->pool.gdc, 0);
	param_set->mem_map.dvs_6axis_params_y = map->daddr;

	for (i = 0; i < stripes; i++) {
		map = imgu_css_pool_last(&css_pipe->pool.obgrid, 0);
		param_set->mem_map.obgrid_tbl[i] =
			map->daddr + (obgrid_size / stripes) * i;
	}

	for (m = 0; m < IMGU_ABI_NUM_MEMORIES; m++) {
		map = imgu_css_pool_last(&css_pipe->pool.binary_params_p[m], 0);
		param_set->mem_map.isp_mem_param[stage][m] = map->daddr;
	}

	/* Then queue the new parameter buffer */
	map = imgu_css_pool_last(&css_pipe->pool.parameter_set_info, 0);
	r = imgu_css_queue_data(css, queue_id, pipe, map->daddr);
	if (r < 0)
		goto fail;

	r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe,
				IMGU_ABI_EVENT_BUFFER_ENQUEUED(pipe,
							       queue_id));
	if (r < 0)
		goto fail_no_put;

	/* Finally dequeue all old parameter buffers */

	do {
		u32 daddr;

		r = imgu_css_dequeue_data(css, queue_id, &daddr);
		if (r == -EBUSY)
			break;
		if (r)
			goto fail_no_put;
		r = imgu_css_queue_data(css, IMGU_ABI_QUEUE_EVENT_ID, pipe,
					IMGU_ABI_EVENT_BUFFER_DEQUEUED
					(queue_id));
		if (r < 0) {
			dev_err(css->dev, "failed to queue parameter event\n");
			goto fail_no_put;
		}
	} while (1);

	return 0;

fail:
	/*
	 * A failure, most likely the parameter queue was full.
	 * Return error but continue streaming. User can try submitting new
	 * parameters again later.
	 */

	imgu_css_pool_put(&css_pipe->pool.parameter_set_info);
	if (acc)
		imgu_css_pool_put(&css_pipe->pool.acc);
	if (gdc)
		imgu_css_pool_put(&css_pipe->pool.gdc);
	if (obgrid)
		imgu_css_pool_put(&css_pipe->pool.obgrid);
	if (vmem0)
		imgu_css_pool_put(
			&css_pipe->pool.binary_params_p
			[IMGU_ABI_MEM_ISP_VMEM0]);
	if (dmem0)
		imgu_css_pool_put(
			&css_pipe->pool.binary_params_p
			[IMGU_ABI_MEM_ISP_DMEM0]);

fail_no_put:
	return r;
}

int imgu_css_irq_ack(struct imgu_css *css)
{
	static const int NUM_SWIRQS = 3;
	struct imgu_fw_info *bi = &css->fwp->binary_header[css->fw_sp[0]];
	void __iomem *const base = css->base;
	u32 irq_status[IMGU_IRQCTRL_NUM];
	int i;

	u32 imgu_status = readl(base + IMGU_REG_INT_STATUS);

	writel(imgu_status, base + IMGU_REG_INT_STATUS);
	for (i = 0; i < IMGU_IRQCTRL_NUM; i++)
		irq_status[i] = readl(base + IMGU_REG_IRQCTRL_STATUS(i));

	for (i = 0; i < NUM_SWIRQS; i++) {
		if (irq_status[IMGU_IRQCTRL_SP0] & IMGU_IRQCTRL_IRQ_SW_PIN(i)) {
			/* SP SW interrupt */
			u32 cnt = readl(base + IMGU_REG_SP_DMEM_BASE(0) +
					bi->info.sp.output);
			u32 val = readl(base + IMGU_REG_SP_DMEM_BASE(0) +
					bi->info.sp.output + 4 + 4 * i);

			dev_dbg(css->dev, "%s: swirq %i cnt %i val 0x%x\n",
				__func__, i, cnt, val);
		}
	}

	for (i = IMGU_IRQCTRL_NUM - 1; i >= 0; i--)
		if (irq_status[i]) {
			writel(irq_status[i], base + IMGU_REG_IRQCTRL_CLEAR(i));
			/* Wait for write to complete */
			readl(base + IMGU_REG_IRQCTRL_ENABLE(i));
		}

	dev_dbg(css->dev, "%s: imgu 0x%x main 0x%x sp0 0x%x sp1 0x%x\n",
		__func__, imgu_status, irq_status[IMGU_IRQCTRL_MAIN],
		irq_status[IMGU_IRQCTRL_SP0], irq_status[IMGU_IRQCTRL_SP1]);

	if (!imgu_status && !irq_status[IMGU_IRQCTRL_MAIN])
		return -ENOMSG;

	return 0;
}
