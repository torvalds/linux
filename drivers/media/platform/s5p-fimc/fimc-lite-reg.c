/*
 * Register interface file for EXYNOS FIMC-LITE (camera interface) driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/delay.h>
#include <media/s5p_fimc.h>

#include "fimc-lite-reg.h"
#include "fimc-lite.h"
#include "fimc-core.h"

#define FLITE_RESET_TIMEOUT 50 /* in ms */

void flite_hw_reset(struct fimc_lite *dev)
{
	unsigned long end = jiffies + msecs_to_jiffies(FLITE_RESET_TIMEOUT);
	u32 cfg;

	cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_SWRST_REQ;
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);

	while (time_is_after_jiffies(end)) {
		cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
		if (cfg & FLITE_REG_CIGCTRL_SWRST_RDY)
			break;
		usleep_range(1000, 5000);
	}

	cfg |= FLITE_REG_CIGCTRL_SWRST;
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);
}

void flite_hw_clear_pending_irq(struct fimc_lite *dev)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CISTATUS);
	cfg &= ~FLITE_REG_CISTATUS_IRQ_CAM;
	writel(cfg, dev->regs + FLITE_REG_CISTATUS);
}

u32 flite_hw_get_interrupt_source(struct fimc_lite *dev)
{
	u32 intsrc = readl(dev->regs + FLITE_REG_CISTATUS);
	return intsrc & FLITE_REG_CISTATUS_IRQ_MASK;
}

void flite_hw_clear_last_capture_end(struct fimc_lite *dev)
{

	u32 cfg = readl(dev->regs + FLITE_REG_CISTATUS2);
	cfg &= ~FLITE_REG_CISTATUS2_LASTCAPEND;
	writel(cfg, dev->regs + FLITE_REG_CISTATUS2);
}

void flite_hw_set_interrupt_mask(struct fimc_lite *dev)
{
	u32 cfg, intsrc;

	/* Select interrupts to be enabled for each output mode */
	if (atomic_read(&dev->out_path) == FIMC_IO_DMA) {
		intsrc = FLITE_REG_CIGCTRL_IRQ_OVFEN |
			 FLITE_REG_CIGCTRL_IRQ_LASTEN |
			 FLITE_REG_CIGCTRL_IRQ_STARTEN;
	} else {
		/* An output to the FIMC-IS */
		intsrc = FLITE_REG_CIGCTRL_IRQ_OVFEN |
			 FLITE_REG_CIGCTRL_IRQ_LASTEN;
	}

	cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_IRQ_DISABLE_MASK;
	cfg &= ~intsrc;
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);
}

void flite_hw_capture_start(struct fimc_lite *dev)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIIMGCPT);
	cfg |= FLITE_REG_CIIMGCPT_IMGCPTEN;
	writel(cfg, dev->regs + FLITE_REG_CIIMGCPT);
}

void flite_hw_capture_stop(struct fimc_lite *dev)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIIMGCPT);
	cfg &= ~FLITE_REG_CIIMGCPT_IMGCPTEN;
	writel(cfg, dev->regs + FLITE_REG_CIIMGCPT);
}

/*
 * Test pattern (color bars) enable/disable. External sensor
 * pixel clock must be active for the test pattern to work.
 */
void flite_hw_set_test_pattern(struct fimc_lite *dev, bool on)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
	if (on)
		cfg |= FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR;
	else
		cfg &= ~FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR;
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);
}

static const u32 src_pixfmt_map[8][3] = {
	{ V4L2_MBUS_FMT_YUYV8_2X8, FLITE_REG_CISRCSIZE_ORDER422_IN_YCBYCR,
	  FLITE_REG_CIGCTRL_YUV422_1P },
	{ V4L2_MBUS_FMT_YVYU8_2X8, FLITE_REG_CISRCSIZE_ORDER422_IN_YCRYCB,
	  FLITE_REG_CIGCTRL_YUV422_1P },
	{ V4L2_MBUS_FMT_UYVY8_2X8, FLITE_REG_CISRCSIZE_ORDER422_IN_CBYCRY,
	  FLITE_REG_CIGCTRL_YUV422_1P },
	{ V4L2_MBUS_FMT_VYUY8_2X8, FLITE_REG_CISRCSIZE_ORDER422_IN_CRYCBY,
	  FLITE_REG_CIGCTRL_YUV422_1P },
	{ V4L2_MBUS_FMT_SGRBG8_1X8, 0, FLITE_REG_CIGCTRL_RAW8 },
	{ V4L2_MBUS_FMT_SGRBG10_1X10, 0, FLITE_REG_CIGCTRL_RAW10 },
	{ V4L2_MBUS_FMT_SGRBG12_1X12, 0, FLITE_REG_CIGCTRL_RAW12 },
	{ V4L2_MBUS_FMT_JPEG_1X8, 0, FLITE_REG_CIGCTRL_USER(1) },
};

/* Set camera input pixel format and resolution */
void flite_hw_set_source_format(struct fimc_lite *dev, struct flite_frame *f)
{
	enum v4l2_mbus_pixelcode pixelcode = dev->fmt->mbus_code;
	int i = ARRAY_SIZE(src_pixfmt_map);
	u32 cfg;

	while (--i >= 0) {
		if (src_pixfmt_map[i][0] == pixelcode)
			break;
	}

	if (i == 0 && src_pixfmt_map[i][0] != pixelcode) {
		v4l2_err(&dev->vfd,
			 "Unsupported pixel code, falling back to %#08x\n",
			 src_pixfmt_map[i][0]);
	}

	cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
	cfg &= ~FLITE_REG_CIGCTRL_FMT_MASK;
	cfg |= src_pixfmt_map[i][2];
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);

	cfg = readl(dev->regs + FLITE_REG_CISRCSIZE);
	cfg &= ~(FLITE_REG_CISRCSIZE_ORDER422_MASK |
		 FLITE_REG_CISRCSIZE_SIZE_CAM_MASK);
	cfg |= (f->f_width << 16) | f->f_height;
	cfg |= src_pixfmt_map[i][1];
	writel(cfg, dev->regs + FLITE_REG_CISRCSIZE);
}

/* Set the camera host input window offsets (cropping) */
void flite_hw_set_window_offset(struct fimc_lite *dev, struct flite_frame *f)
{
	u32 hoff2, voff2;
	u32 cfg;

	cfg = readl(dev->regs + FLITE_REG_CIWDOFST);
	cfg &= ~FLITE_REG_CIWDOFST_OFST_MASK;
	cfg |= (f->rect.left << 16) | f->rect.top;
	cfg |= FLITE_REG_CIWDOFST_WINOFSEN;
	writel(cfg, dev->regs + FLITE_REG_CIWDOFST);

	hoff2 = f->f_width - f->rect.width - f->rect.left;
	voff2 = f->f_height - f->rect.height - f->rect.top;

	cfg = (hoff2 << 16) | voff2;
	writel(cfg, dev->regs + FLITE_REG_CIWDOFST2);
}

/* Select camera port (A, B) */
static void flite_hw_set_camera_port(struct fimc_lite *dev, int id)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIGENERAL);
	if (id == 0)
		cfg &= ~FLITE_REG_CIGENERAL_CAM_B;
	else
		cfg |= FLITE_REG_CIGENERAL_CAM_B;
	writel(cfg, dev->regs + FLITE_REG_CIGENERAL);
}

/* Select serial or parallel bus, camera port (A,B) and set signals polarity */
void flite_hw_set_camera_bus(struct fimc_lite *dev,
			     struct fimc_source_info *si)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIGCTRL);
	unsigned int flags = si->flags;

	if (si->sensor_bus_type != FIMC_BUS_TYPE_MIPI_CSI2) {
		cfg &= ~(FLITE_REG_CIGCTRL_SELCAM_MIPI |
			 FLITE_REG_CIGCTRL_INVPOLPCLK |
			 FLITE_REG_CIGCTRL_INVPOLVSYNC |
			 FLITE_REG_CIGCTRL_INVPOLHREF);

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			cfg |= FLITE_REG_CIGCTRL_INVPOLPCLK;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			cfg |= FLITE_REG_CIGCTRL_INVPOLVSYNC;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			cfg |= FLITE_REG_CIGCTRL_INVPOLHREF;
	} else {
		cfg |= FLITE_REG_CIGCTRL_SELCAM_MIPI;
	}

	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);

	flite_hw_set_camera_port(dev, si->mux_id);
}

static void flite_hw_set_out_order(struct fimc_lite *dev, struct flite_frame *f)
{
	static const u32 pixcode[4][2] = {
		{ V4L2_MBUS_FMT_YUYV8_2X8, FLITE_REG_CIODMAFMT_YCBYCR },
		{ V4L2_MBUS_FMT_YVYU8_2X8, FLITE_REG_CIODMAFMT_YCRYCB },
		{ V4L2_MBUS_FMT_UYVY8_2X8, FLITE_REG_CIODMAFMT_CBYCRY },
		{ V4L2_MBUS_FMT_VYUY8_2X8, FLITE_REG_CIODMAFMT_CRYCBY },
	};
	u32 cfg = readl(dev->regs + FLITE_REG_CIODMAFMT);
	int i = ARRAY_SIZE(pixcode);

	while (--i >= 0)
		if (pixcode[i][0] == dev->fmt->mbus_code)
			break;
	cfg &= ~FLITE_REG_CIODMAFMT_YCBCR_ORDER_MASK;
	writel(cfg | pixcode[i][1], dev->regs + FLITE_REG_CIODMAFMT);
}

void flite_hw_set_dma_window(struct fimc_lite *dev, struct flite_frame *f)
{
	u32 cfg;

	/* Maximum output pixel size */
	cfg = readl(dev->regs + FLITE_REG_CIOCAN);
	cfg &= ~FLITE_REG_CIOCAN_MASK;
	cfg = (f->f_height << 16) | f->f_width;
	writel(cfg, dev->regs + FLITE_REG_CIOCAN);

	/* DMA offsets */
	cfg = readl(dev->regs + FLITE_REG_CIOOFF);
	cfg &= ~FLITE_REG_CIOOFF_MASK;
	cfg |= (f->rect.top << 16) | f->rect.left;
	writel(cfg, dev->regs + FLITE_REG_CIOOFF);
}

/* Enable/disable output DMA, set output pixel size and offsets (composition) */
void flite_hw_set_output_dma(struct fimc_lite *dev, struct flite_frame *f,
			     bool enable)
{
	u32 cfg = readl(dev->regs + FLITE_REG_CIGCTRL);

	if (!enable) {
		cfg |= FLITE_REG_CIGCTRL_ODMA_DISABLE;
		writel(cfg, dev->regs + FLITE_REG_CIGCTRL);
		return;
	}

	cfg &= ~FLITE_REG_CIGCTRL_ODMA_DISABLE;
	writel(cfg, dev->regs + FLITE_REG_CIGCTRL);

	flite_hw_set_out_order(dev, f);
	flite_hw_set_dma_window(dev, f);
}

void flite_hw_dump_regs(struct fimc_lite *dev, const char *label)
{
	struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ 0x00, "CISRCSIZE" },
		{ 0x04, "CIGCTRL" },
		{ 0x08, "CIIMGCPT" },
		{ 0x0c, "CICPTSEQ" },
		{ 0x10, "CIWDOFST" },
		{ 0x14, "CIWDOFST2" },
		{ 0x18, "CIODMAFMT" },
		{ 0x20, "CIOCAN" },
		{ 0x24, "CIOOFF" },
		{ 0x30, "CIOSA" },
		{ 0x40, "CISTATUS" },
		{ 0x44, "CISTATUS2" },
		{ 0xf0, "CITHOLD" },
		{ 0xfc, "CIGENERAL" },
	};
	u32 i;

	v4l2_info(&dev->subdev, "--- %s ---\n", label);

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		u32 cfg = readl(dev->regs + registers[i].offset);
		v4l2_info(&dev->subdev, "%9s: 0x%08x\n",
			  registers[i].name, cfg);
	}
}
