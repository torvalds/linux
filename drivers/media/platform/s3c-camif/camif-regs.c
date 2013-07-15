/*
 * Samsung s3c24xx/s3c64xx SoC CAMIF driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/delay.h>
#include "camif-regs.h"

#define camif_write(_camif, _off, _val)	writel(_val, (_camif)->io_base + (_off))
#define camif_read(_camif, _off)	readl((_camif)->io_base + (_off))

void camif_hw_reset(struct camif_dev *camif)
{
	u32 cfg;

	cfg = camif_read(camif, S3C_CAMIF_REG_CISRCFMT);
	cfg |= CISRCFMT_ITU601_8BIT;
	camif_write(camif, S3C_CAMIF_REG_CISRCFMT, cfg);

	/* S/W reset */
	cfg = camif_read(camif, S3C_CAMIF_REG_CIGCTRL);
	cfg |= CIGCTRL_SWRST;
	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV)
		cfg |= CIGCTRL_IRQ_LEVEL;
	camif_write(camif, S3C_CAMIF_REG_CIGCTRL, cfg);
	udelay(10);

	cfg = camif_read(camif, S3C_CAMIF_REG_CIGCTRL);
	cfg &= ~CIGCTRL_SWRST;
	camif_write(camif, S3C_CAMIF_REG_CIGCTRL, cfg);
	udelay(10);
}

void camif_hw_clear_pending_irq(struct camif_vp *vp)
{
	u32 cfg = camif_read(vp->camif, S3C_CAMIF_REG_CIGCTRL);
	cfg |= CIGCTRL_IRQ_CLR(vp->id);
	camif_write(vp->camif, S3C_CAMIF_REG_CIGCTRL, cfg);
}

/*
 * Sets video test pattern (off, color bar, horizontal or vertical gradient).
 * External sensor pixel clock must be active for the test pattern to work.
 */
void camif_hw_set_test_pattern(struct camif_dev *camif, unsigned int pattern)
{
	u32 cfg = camif_read(camif, S3C_CAMIF_REG_CIGCTRL);
	cfg &= ~CIGCTRL_TESTPATTERN_MASK;
	cfg |= (pattern << 27);
	camif_write(camif, S3C_CAMIF_REG_CIGCTRL, cfg);
}

void camif_hw_set_effect(struct camif_dev *camif, unsigned int effect,
			unsigned int cr, unsigned int cb)
{
	static const struct v4l2_control colorfx[] = {
		{ V4L2_COLORFX_NONE,		CIIMGEFF_FIN_BYPASS },
		{ V4L2_COLORFX_BW,		CIIMGEFF_FIN_ARBITRARY },
		{ V4L2_COLORFX_SEPIA,		CIIMGEFF_FIN_ARBITRARY },
		{ V4L2_COLORFX_NEGATIVE,	CIIMGEFF_FIN_NEGATIVE },
		{ V4L2_COLORFX_ART_FREEZE,	CIIMGEFF_FIN_ARTFREEZE },
		{ V4L2_COLORFX_EMBOSS,		CIIMGEFF_FIN_EMBOSSING },
		{ V4L2_COLORFX_SILHOUETTE,	CIIMGEFF_FIN_SILHOUETTE },
		{ V4L2_COLORFX_SET_CBCR,	CIIMGEFF_FIN_ARBITRARY },
	};
	unsigned int i, cfg;

	for (i = 0; i < ARRAY_SIZE(colorfx); i++)
		if (colorfx[i].id == effect)
			break;

	if (i == ARRAY_SIZE(colorfx))
		return;

	cfg = camif_read(camif, S3C_CAMIF_REG_CIIMGEFF(camif->vp->offset));
	/* Set effect */
	cfg &= ~CIIMGEFF_FIN_MASK;
	cfg |= colorfx[i].value;
	/* Set both paths */
	if (camif->variant->ip_revision >= S3C6400_CAMIF_IP_REV) {
		if (effect == V4L2_COLORFX_NONE)
			cfg &= ~CIIMGEFF_IE_ENABLE_MASK;
		else
			cfg |= CIIMGEFF_IE_ENABLE_MASK;
	}
	cfg &= ~CIIMGEFF_PAT_CBCR_MASK;
	cfg |= cr | (cb << 13);
	camif_write(camif, S3C_CAMIF_REG_CIIMGEFF(camif->vp->offset), cfg);
}

static const u32 src_pixfmt_map[8][2] = {
	{ V4L2_MBUS_FMT_YUYV8_2X8, CISRCFMT_ORDER422_YCBYCR },
	{ V4L2_MBUS_FMT_YVYU8_2X8, CISRCFMT_ORDER422_YCRYCB },
	{ V4L2_MBUS_FMT_UYVY8_2X8, CISRCFMT_ORDER422_CBYCRY },
	{ V4L2_MBUS_FMT_VYUY8_2X8, CISRCFMT_ORDER422_CRYCBY },
};

/* Set camera input pixel format and resolution */
void camif_hw_set_source_format(struct camif_dev *camif)
{
	struct v4l2_mbus_framefmt *mf = &camif->mbus_fmt;
	unsigned int i = ARRAY_SIZE(src_pixfmt_map);
	u32 cfg;

	while (i-- >= 0) {
		if (src_pixfmt_map[i][0] == mf->code)
			break;
	}

	if (i == 0 && src_pixfmt_map[i][0] != mf->code) {
		dev_err(camif->dev,
			"Unsupported pixel code, falling back to %#08x\n",
			src_pixfmt_map[i][0]);
	}

	cfg = camif_read(camif, S3C_CAMIF_REG_CISRCFMT);
	cfg &= ~(CISRCFMT_ORDER422_MASK | CISRCFMT_SIZE_CAM_MASK);
	cfg |= (mf->width << 16) | mf->height;
	cfg |= src_pixfmt_map[i][1];
	camif_write(camif, S3C_CAMIF_REG_CISRCFMT, cfg);
}

/* Set the camera host input window offsets (cropping) */
void camif_hw_set_camera_crop(struct camif_dev *camif)
{
	struct v4l2_mbus_framefmt *mf = &camif->mbus_fmt;
	struct v4l2_rect *crop = &camif->camif_crop;
	u32 hoff2, voff2;
	u32 cfg;

	/* Note: s3c244x requirement: left = f_width - rect.width / 2 */
	cfg = camif_read(camif, S3C_CAMIF_REG_CIWDOFST);
	cfg &= ~(CIWDOFST_OFST_MASK | CIWDOFST_WINOFSEN);
	cfg |= (crop->left << 16) | crop->top;
	if (crop->left != 0 || crop->top != 0)
		cfg |= CIWDOFST_WINOFSEN;
	camif_write(camif, S3C_CAMIF_REG_CIWDOFST, cfg);

	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV) {
		hoff2 = mf->width - crop->width - crop->left;
		voff2 = mf->height - crop->height - crop->top;
		cfg = (hoff2 << 16) | voff2;
		camif_write(camif, S3C_CAMIF_REG_CIWDOFST2, cfg);
	}
}

void camif_hw_clear_fifo_overflow(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	u32 cfg;

	cfg = camif_read(camif, S3C_CAMIF_REG_CIWDOFST);
	if (vp->id == 0)
		cfg |= (CIWDOFST_CLROVCOFIY | CIWDOFST_CLROVCOFICB |
			CIWDOFST_CLROVCOFICR);
	else
		cfg |= (/* CIWDOFST_CLROVPRFIY | */ CIWDOFST_CLROVPRFICB |
			CIWDOFST_CLROVPRFICR);
	camif_write(camif, S3C_CAMIF_REG_CIWDOFST, cfg);
}

/* Set video bus signals polarity */
void camif_hw_set_camera_bus(struct camif_dev *camif)
{
	unsigned int flags = camif->pdata.sensor.flags;

	u32 cfg = camif_read(camif, S3C_CAMIF_REG_CIGCTRL);

	cfg &= ~(CIGCTRL_INVPOLPCLK | CIGCTRL_INVPOLVSYNC |
		 CIGCTRL_INVPOLHREF | CIGCTRL_INVPOLFIELD);

	if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		cfg |= CIGCTRL_INVPOLPCLK;

	if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		cfg |= CIGCTRL_INVPOLVSYNC;
	/*
	 * HREF is normally high during frame active data
	 * transmission and low during horizontal synchronization
	 * period. Thus HREF active high means HSYNC active low.
	 */
	if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		cfg |= CIGCTRL_INVPOLHREF; /* HREF active low */

	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV) {
		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			cfg |= CIGCTRL_INVPOLFIELD;
		cfg |= CIGCTRL_FIELDMODE;
	}

	pr_debug("Setting CIGCTRL to: %#x\n", cfg);

	camif_write(camif, S3C_CAMIF_REG_CIGCTRL, cfg);
}

void camif_hw_set_output_addr(struct camif_vp *vp,
			      struct camif_addr *paddr, int i)
{
	struct camif_dev *camif = vp->camif;

	camif_write(camif, S3C_CAMIF_REG_CIYSA(vp->id, i), paddr->y);
	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV
		|| vp->id == VP_CODEC) {
		camif_write(camif, S3C_CAMIF_REG_CICBSA(vp->id, i),
								paddr->cb);
		camif_write(camif, S3C_CAMIF_REG_CICRSA(vp->id, i),
								paddr->cr);
	}

	pr_debug("dst_buf[%d]: %#X, cb: %#X, cr: %#X\n",
		 i, paddr->y, paddr->cb, paddr->cr);
}

static void camif_hw_set_out_dma_size(struct camif_vp *vp)
{
	struct camif_frame *frame = &vp->out_frame;
	u32 cfg;

	cfg = camif_read(vp->camif, S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset));
	cfg &= ~CITRGFMT_TARGETSIZE_MASK;
	cfg |= (frame->f_width << 16) | frame->f_height;
	camif_write(vp->camif, S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset), cfg);
}

static void camif_get_dma_burst(u32 width, u32 ybpp, u32 *mburst, u32 *rburst)
{
	unsigned int nwords = width * ybpp / 4;
	unsigned int div, rem;

	if (WARN_ON(width < 8 || (width * ybpp) & 7))
		return;

	for (div = 16; div >= 2; div /= 2) {
		if (nwords < div)
			continue;

		rem = nwords & (div - 1);
		if (rem == 0) {
			*mburst = div;
			*rburst = div;
			break;
		}
		if (rem == div / 2 || rem == div / 4) {
			*mburst = div;
			*rburst = rem;
			break;
		}
	}
}

void camif_hw_set_output_dma(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_frame *frame = &vp->out_frame;
	const struct camif_fmt *fmt = vp->out_fmt;
	unsigned int ymburst = 0, yrburst = 0;
	u32 cfg;

	camif_hw_set_out_dma_size(vp);

	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV) {
		struct camif_dma_offset *offset = &frame->dma_offset;
		/* Set the input dma offsets. */
		cfg = S3C_CISS_OFFS_INITIAL(offset->initial);
		cfg |= S3C_CISS_OFFS_LINE(offset->line);
		camif_write(camif, S3C_CAMIF_REG_CISSY(vp->id), cfg);
		camif_write(camif, S3C_CAMIF_REG_CISSCB(vp->id), cfg);
		camif_write(camif, S3C_CAMIF_REG_CISSCR(vp->id), cfg);
	}

	/* Configure DMA burst values */
	camif_get_dma_burst(frame->rect.width, fmt->ybpp, &ymburst, &yrburst);

	cfg = camif_read(camif, S3C_CAMIF_REG_CICTRL(vp->id, vp->offset));
	cfg &= ~CICTRL_BURST_MASK;

	cfg |= CICTRL_YBURST1(ymburst) | CICTRL_YBURST2(yrburst);
	cfg |= CICTRL_CBURST1(ymburst / 2) | CICTRL_CBURST2(yrburst / 2);

	camif_write(camif, S3C_CAMIF_REG_CICTRL(vp->id, vp->offset), cfg);

	pr_debug("ymburst: %u, yrburst: %u\n", ymburst, yrburst);
}

void camif_hw_set_input_path(struct camif_vp *vp)
{
	u32 cfg = camif_read(vp->camif, S3C_CAMIF_REG_MSCTRL(vp->id));
	cfg &= ~MSCTRL_SEL_DMA_CAM;
	camif_write(vp->camif, S3C_CAMIF_REG_MSCTRL(vp->id), cfg);
}

void camif_hw_set_target_format(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_frame *frame = &vp->out_frame;
	u32 cfg;

	pr_debug("fw: %d, fh: %d color: %d\n", frame->f_width,
		 frame->f_height, vp->out_fmt->color);

	cfg = camif_read(camif, S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset));
	cfg &= ~CITRGFMT_TARGETSIZE_MASK;

	if (camif->variant->ip_revision == S3C244X_CAMIF_IP_REV) {
		/* We currently support only YCbCr 4:2:2 at the camera input */
		cfg |= CITRGFMT_IN422;
		cfg &= ~CITRGFMT_OUT422;
		if (vp->out_fmt->color == IMG_FMT_YCBCR422P)
			cfg |= CITRGFMT_OUT422;
	} else {
		cfg &= ~CITRGFMT_OUTFORMAT_MASK;
		switch (vp->out_fmt->color) {
		case IMG_FMT_RGB565...IMG_FMT_XRGB8888:
			cfg |= CITRGFMT_OUTFORMAT_RGB;
			break;
		case IMG_FMT_YCBCR420...IMG_FMT_YCRCB420:
			cfg |= CITRGFMT_OUTFORMAT_YCBCR420;
			break;
		case IMG_FMT_YCBCR422P:
			cfg |= CITRGFMT_OUTFORMAT_YCBCR422;
			break;
		case IMG_FMT_YCBYCR422...IMG_FMT_CRYCBY422:
			cfg |= CITRGFMT_OUTFORMAT_YCBCR422I;
			break;
		}
	}

	/* Rotation is only supported by s3c64xx */
	if (vp->rotation == 90 || vp->rotation == 270)
		cfg |= (frame->f_height << 16) | frame->f_width;
	else
		cfg |= (frame->f_width << 16) | frame->f_height;
	camif_write(camif, S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset), cfg);

	/* Target area, output pixel width * height */
	cfg = camif_read(camif, S3C_CAMIF_REG_CITAREA(vp->id, vp->offset));
	cfg &= ~CITAREA_MASK;
	cfg |= (frame->f_width * frame->f_height);
	camif_write(camif, S3C_CAMIF_REG_CITAREA(vp->id, vp->offset), cfg);
}

void camif_hw_set_flip(struct camif_vp *vp)
{
	u32 cfg = camif_read(vp->camif,
				S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset));

	cfg &= ~CITRGFMT_FLIP_MASK;

	if (vp->hflip)
		cfg |= CITRGFMT_FLIP_Y_MIRROR;
	if (vp->vflip)
		cfg |= CITRGFMT_FLIP_X_MIRROR;

	camif_write(vp->camif, S3C_CAMIF_REG_CITRGFMT(vp->id, vp->offset), cfg);
}

static void camif_hw_set_prescaler(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_scaler *sc = &vp->scaler;
	u32 cfg, shfactor, addr;

	addr = S3C_CAMIF_REG_CISCPRERATIO(vp->id, vp->offset);

	shfactor = 10 - (sc->h_shift + sc->v_shift);
	cfg = shfactor << 28;

	cfg |= (sc->pre_h_ratio << 16) | sc->pre_v_ratio;
	camif_write(camif, addr, cfg);

	cfg = (sc->pre_dst_width << 16) | sc->pre_dst_height;
	camif_write(camif, S3C_CAMIF_REG_CISCPREDST(vp->id, vp->offset), cfg);
}

static void camif_s3c244x_hw_set_scaler(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_scaler *scaler = &vp->scaler;
	unsigned int color = vp->out_fmt->color;
	u32 cfg;

	camif_hw_set_prescaler(vp);

	cfg = camif_read(camif, S3C_CAMIF_REG_CISCCTRL(vp->id, vp->offset));

	cfg &= ~(CISCCTRL_SCALEUP_MASK | CISCCTRL_SCALERBYPASS |
		 CISCCTRL_MAIN_RATIO_MASK | CIPRSCCTRL_RGB_FORMAT_24BIT);

	if (scaler->enable) {
		if (scaler->scaleup_h) {
			if (vp->id == VP_CODEC)
				cfg |= CISCCTRL_SCALEUP_H;
			else
				cfg |= CIPRSCCTRL_SCALEUP_H;
		}
		if (scaler->scaleup_v) {
			if (vp->id == VP_CODEC)
				cfg |= CISCCTRL_SCALEUP_V;
			else
				cfg |= CIPRSCCTRL_SCALEUP_V;
		}
	} else {
		if (vp->id == VP_CODEC)
			cfg |= CISCCTRL_SCALERBYPASS;
	}

	cfg |= ((scaler->main_h_ratio & 0x1ff) << 16);
	cfg |= scaler->main_v_ratio & 0x1ff;

	if (vp->id == VP_PREVIEW) {
		if (color == IMG_FMT_XRGB8888)
			cfg |= CIPRSCCTRL_RGB_FORMAT_24BIT;
		cfg |= CIPRSCCTRL_SAMPLE;
	}

	camif_write(camif, S3C_CAMIF_REG_CISCCTRL(vp->id, vp->offset), cfg);

	pr_debug("main: h_ratio: %#x, v_ratio: %#x",
		 scaler->main_h_ratio, scaler->main_v_ratio);
}

static void camif_s3c64xx_hw_set_scaler(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	struct camif_scaler *scaler = &vp->scaler;
	unsigned int color = vp->out_fmt->color;
	u32 cfg;

	camif_hw_set_prescaler(vp);

	cfg = camif_read(camif, S3C_CAMIF_REG_CISCCTRL(vp->id, vp->offset));

	cfg &= ~(CISCCTRL_CSCR2Y_WIDE | CISCCTRL_CSCY2R_WIDE
		| CISCCTRL_SCALEUP_H | CISCCTRL_SCALEUP_V
		| CISCCTRL_SCALERBYPASS | CISCCTRL_ONE2ONE
		| CISCCTRL_INRGB_FMT_MASK | CISCCTRL_OUTRGB_FMT_MASK
		| CISCCTRL_INTERLACE | CISCCTRL_EXTRGB_EXTENSION
		| CISCCTRL_MAIN_RATIO_MASK);

	cfg |= (CISCCTRL_CSCR2Y_WIDE | CISCCTRL_CSCY2R_WIDE);

	if (!scaler->enable) {
		cfg |= CISCCTRL_SCALERBYPASS;
	} else {
		if (scaler->scaleup_h)
			cfg |= CISCCTRL_SCALEUP_H;
		if (scaler->scaleup_v)
			cfg |= CISCCTRL_SCALEUP_V;
		if (scaler->copy)
			cfg |= CISCCTRL_ONE2ONE;
	}

	switch (color) {
	case IMG_FMT_RGB666:
		cfg |= CISCCTRL_OUTRGB_FMT_RGB666;
		break;
	case IMG_FMT_XRGB8888:
		cfg |= CISCCTRL_OUTRGB_FMT_RGB888;
		break;
	}

	cfg |= (scaler->main_h_ratio & 0x1ff) << 16;
	cfg |= scaler->main_v_ratio & 0x1ff;

	camif_write(camif, S3C_CAMIF_REG_CISCCTRL(vp->id, vp->offset), cfg);

	pr_debug("main: h_ratio: %#x, v_ratio: %#x",
		 scaler->main_h_ratio, scaler->main_v_ratio);
}

void camif_hw_set_scaler(struct camif_vp *vp)
{
	unsigned int ip_rev = vp->camif->variant->ip_revision;

	if (ip_rev == S3C244X_CAMIF_IP_REV)
		camif_s3c244x_hw_set_scaler(vp);
	else
		camif_s3c64xx_hw_set_scaler(vp);
}

void camif_hw_enable_scaler(struct camif_vp *vp, bool on)
{
	u32 addr = S3C_CAMIF_REG_CISCCTRL(vp->id, vp->offset);
	u32 cfg;

	cfg = camif_read(vp->camif, addr);
	if (on)
		cfg |= CISCCTRL_SCALERSTART;
	else
		cfg &= ~CISCCTRL_SCALERSTART;
	camif_write(vp->camif, addr, cfg);
}

void camif_hw_set_lastirq(struct camif_vp *vp, int enable)
{
	u32 addr = S3C_CAMIF_REG_CICTRL(vp->id, vp->offset);
	u32 cfg;

	cfg = camif_read(vp->camif, addr);
	if (enable)
		cfg |= CICTRL_LASTIRQ_ENABLE;
	else
		cfg &= ~CICTRL_LASTIRQ_ENABLE;
	camif_write(vp->camif, addr, cfg);
}

void camif_hw_enable_capture(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	u32 cfg;

	cfg = camif_read(camif, S3C_CAMIF_REG_CIIMGCPT(vp->offset));
	camif->stream_count++;

	if (camif->variant->ip_revision == S3C6410_CAMIF_IP_REV)
		cfg |= CIIMGCPT_CPT_FREN_ENABLE(vp->id);

	if (vp->scaler.enable)
		cfg |= CIIMGCPT_IMGCPTEN_SC(vp->id);

	if (camif->stream_count == 1)
		cfg |= CIIMGCPT_IMGCPTEN;

	camif_write(camif, S3C_CAMIF_REG_CIIMGCPT(vp->offset), cfg);

	pr_debug("CIIMGCPT: %#x, camif->stream_count: %d\n",
		 cfg, camif->stream_count);
}

void camif_hw_disable_capture(struct camif_vp *vp)
{
	struct camif_dev *camif = vp->camif;
	u32 cfg;

	cfg = camif_read(camif, S3C_CAMIF_REG_CIIMGCPT(vp->offset));
	cfg &= ~CIIMGCPT_IMGCPTEN_SC(vp->id);

	if (WARN_ON(--(camif->stream_count) < 0))
		camif->stream_count = 0;

	if (camif->stream_count == 0)
		cfg &= ~CIIMGCPT_IMGCPTEN;

	pr_debug("CIIMGCPT: %#x, camif->stream_count: %d\n",
		 cfg, camif->stream_count);

	camif_write(camif, S3C_CAMIF_REG_CIIMGCPT(vp->offset), cfg);
}

void camif_hw_dump_regs(struct camif_dev *camif, const char *label)
{
	struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ S3C_CAMIF_REG_CISRCFMT,		"CISRCFMT" },
		{ S3C_CAMIF_REG_CIWDOFST,		"CIWDOFST" },
		{ S3C_CAMIF_REG_CIGCTRL,		"CIGCTRL" },
		{ S3C_CAMIF_REG_CIWDOFST2,		"CIWDOFST2" },
		{ S3C_CAMIF_REG_CIYSA(0, 0),		"CICOYSA0" },
		{ S3C_CAMIF_REG_CICBSA(0, 0),		"CICOCBSA0" },
		{ S3C_CAMIF_REG_CICRSA(0, 0),		"CICOCRSA0" },
		{ S3C_CAMIF_REG_CIYSA(0, 1),		"CICOYSA1" },
		{ S3C_CAMIF_REG_CICBSA(0, 1),		"CICOCBSA1" },
		{ S3C_CAMIF_REG_CICRSA(0, 1),		"CICOCRSA1" },
		{ S3C_CAMIF_REG_CIYSA(0, 2),		"CICOYSA2" },
		{ S3C_CAMIF_REG_CICBSA(0, 2),		"CICOCBSA2" },
		{ S3C_CAMIF_REG_CICRSA(0, 2),		"CICOCRSA2" },
		{ S3C_CAMIF_REG_CIYSA(0, 3),		"CICOYSA3" },
		{ S3C_CAMIF_REG_CICBSA(0, 3),		"CICOCBSA3" },
		{ S3C_CAMIF_REG_CICRSA(0, 3),		"CICOCRSA3" },
		{ S3C_CAMIF_REG_CIYSA(1, 0),		"CIPRYSA0" },
		{ S3C_CAMIF_REG_CIYSA(1, 1),		"CIPRYSA1" },
		{ S3C_CAMIF_REG_CIYSA(1, 2),		"CIPRYSA2" },
		{ S3C_CAMIF_REG_CIYSA(1, 3),		"CIPRYSA3" },
		{ S3C_CAMIF_REG_CITRGFMT(0, 0),		"CICOTRGFMT" },
		{ S3C_CAMIF_REG_CITRGFMT(1, 0),		"CIPRTRGFMT" },
		{ S3C_CAMIF_REG_CICTRL(0, 0),		"CICOCTRL" },
		{ S3C_CAMIF_REG_CICTRL(1, 0),		"CIPRCTRL" },
		{ S3C_CAMIF_REG_CISCPREDST(0, 0),	"CICOSCPREDST" },
		{ S3C_CAMIF_REG_CISCPREDST(1, 0),	"CIPRSCPREDST" },
		{ S3C_CAMIF_REG_CISCPRERATIO(0, 0),	"CICOSCPRERATIO" },
		{ S3C_CAMIF_REG_CISCPRERATIO(1, 0),	"CIPRSCPRERATIO" },
		{ S3C_CAMIF_REG_CISCCTRL(0, 0),		"CICOSCCTRL" },
		{ S3C_CAMIF_REG_CISCCTRL(1, 0),		"CIPRSCCTRL" },
		{ S3C_CAMIF_REG_CITAREA(0, 0),		"CICOTAREA" },
		{ S3C_CAMIF_REG_CITAREA(1, 0),		"CIPRTAREA" },
		{ S3C_CAMIF_REG_CISTATUS(0, 0),		"CICOSTATUS" },
		{ S3C_CAMIF_REG_CISTATUS(1, 0),		"CIPRSTATUS" },
		{ S3C_CAMIF_REG_CIIMGCPT(0),		"CIIMGCPT" },
	};
	u32 i;

	pr_info("--- %s ---\n", label);
	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		u32 cfg = readl(camif->io_base + registers[i].offset);
		dev_info(camif->dev, "%s:\t0x%08x\n", registers[i].name, cfg);
	}
}
