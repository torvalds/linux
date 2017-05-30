/*
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include <linux/module.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#include <drm/drm_fourcc.h>

#include <video/imx-ipu-v3.h>
#include "ipu-prv.h"

static inline u32 ipu_cm_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->cm_reg + offset);
}

static inline void ipu_cm_write(struct ipu_soc *ipu, u32 value, unsigned offset)
{
	writel(value, ipu->cm_reg + offset);
}

int ipu_get_num(struct ipu_soc *ipu)
{
	return ipu->id;
}
EXPORT_SYMBOL_GPL(ipu_get_num);

void ipu_srm_dp_update(struct ipu_soc *ipu, bool sync)
{
	u32 val;

	val = ipu_cm_read(ipu, IPU_SRM_PRI2);
	val &= ~DP_S_SRM_MODE_MASK;
	val |= sync ? DP_S_SRM_MODE_NEXT_FRAME :
		      DP_S_SRM_MODE_NOW;
	ipu_cm_write(ipu, val, IPU_SRM_PRI2);
}
EXPORT_SYMBOL_GPL(ipu_srm_dp_update);

enum ipu_color_space ipu_drm_fourcc_to_colorspace(u32 drm_fourcc)
{
	switch (drm_fourcc) {
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_RGB565_A8:
	case DRM_FORMAT_BGR565_A8:
	case DRM_FORMAT_RGB888_A8:
	case DRM_FORMAT_BGR888_A8:
	case DRM_FORMAT_RGBX8888_A8:
	case DRM_FORMAT_BGRX8888_A8:
		return IPUV3_COLORSPACE_RGB;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		return IPUV3_COLORSPACE_YUV;
	default:
		return IPUV3_COLORSPACE_UNKNOWN;
	}
}
EXPORT_SYMBOL_GPL(ipu_drm_fourcc_to_colorspace);

enum ipu_color_space ipu_pixelformat_to_colorspace(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		return IPUV3_COLORSPACE_YUV;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB565:
		return IPUV3_COLORSPACE_RGB;
	default:
		return IPUV3_COLORSPACE_UNKNOWN;
	}
}
EXPORT_SYMBOL_GPL(ipu_pixelformat_to_colorspace);

bool ipu_pixelformat_is_planar(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(ipu_pixelformat_is_planar);

enum ipu_color_space ipu_mbus_code_to_colorspace(u32 mbus_code)
{
	switch (mbus_code & 0xf000) {
	case 0x1000:
		return IPUV3_COLORSPACE_RGB;
	case 0x2000:
		return IPUV3_COLORSPACE_YUV;
	default:
		return IPUV3_COLORSPACE_UNKNOWN;
	}
}
EXPORT_SYMBOL_GPL(ipu_mbus_code_to_colorspace);

int ipu_stride_to_bytes(u32 pixel_stride, u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		/*
		 * for the planar YUV formats, the stride passed to
		 * cpmem must be the stride in bytes of the Y plane.
		 * And all the planar YUV formats have an 8-bit
		 * Y component.
		 */
		return (8 * pixel_stride) >> 3;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
		return (16 * pixel_stride) >> 3;
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB24:
		return (24 * pixel_stride) >> 3;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_RGB32:
		return (32 * pixel_stride) >> 3;
	default:
		break;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ipu_stride_to_bytes);

int ipu_degrees_to_rot_mode(enum ipu_rotate_mode *mode, int degrees,
			    bool hflip, bool vflip)
{
	u32 r90, vf, hf;

	switch (degrees) {
	case 0:
		vf = hf = r90 = 0;
		break;
	case 90:
		vf = hf = 0;
		r90 = 1;
		break;
	case 180:
		vf = hf = 1;
		r90 = 0;
		break;
	case 270:
		vf = hf = r90 = 1;
		break;
	default:
		return -EINVAL;
	}

	hf ^= (u32)hflip;
	vf ^= (u32)vflip;

	*mode = (enum ipu_rotate_mode)((r90 << 2) | (hf << 1) | vf);
	return 0;
}
EXPORT_SYMBOL_GPL(ipu_degrees_to_rot_mode);

int ipu_rot_mode_to_degrees(int *degrees, enum ipu_rotate_mode mode,
			    bool hflip, bool vflip)
{
	u32 r90, vf, hf;

	r90 = ((u32)mode >> 2) & 0x1;
	hf = ((u32)mode >> 1) & 0x1;
	vf = ((u32)mode >> 0) & 0x1;
	hf ^= (u32)hflip;
	vf ^= (u32)vflip;

	switch ((enum ipu_rotate_mode)((r90 << 2) | (hf << 1) | vf)) {
	case IPU_ROTATE_NONE:
		*degrees = 0;
		break;
	case IPU_ROTATE_90_RIGHT:
		*degrees = 90;
		break;
	case IPU_ROTATE_180:
		*degrees = 180;
		break;
	case IPU_ROTATE_90_LEFT:
		*degrees = 270;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_rot_mode_to_degrees);

struct ipuv3_channel *ipu_idmac_get(struct ipu_soc *ipu, unsigned num)
{
	struct ipuv3_channel *channel;

	dev_dbg(ipu->dev, "%s %d\n", __func__, num);

	if (num > 63)
		return ERR_PTR(-ENODEV);

	mutex_lock(&ipu->channel_lock);

	channel = &ipu->channel[num];

	if (channel->busy) {
		channel = ERR_PTR(-EBUSY);
		goto out;
	}

	channel->busy = true;
	channel->num = num;

out:
	mutex_unlock(&ipu->channel_lock);

	return channel;
}
EXPORT_SYMBOL_GPL(ipu_idmac_get);

void ipu_idmac_put(struct ipuv3_channel *channel)
{
	struct ipu_soc *ipu = channel->ipu;

	dev_dbg(ipu->dev, "%s %d\n", __func__, channel->num);

	mutex_lock(&ipu->channel_lock);

	channel->busy = false;

	mutex_unlock(&ipu->channel_lock);
}
EXPORT_SYMBOL_GPL(ipu_idmac_put);

#define idma_mask(ch)			(1 << ((ch) & 0x1f))

/*
 * This is an undocumented feature, a write one to a channel bit in
 * IPU_CHA_CUR_BUF and IPU_CHA_TRIPLE_CUR_BUF will reset the channel's
 * internal current buffer pointer so that transfers start from buffer
 * 0 on the next channel enable (that's the theory anyway, the imx6 TRM
 * only says these are read-only registers). This operation is required
 * for channel linking to work correctly, for instance video capture
 * pipelines that carry out image rotations will fail after the first
 * streaming unless this function is called for each channel before
 * re-enabling the channels.
 */
static void __ipu_idmac_reset_current_buffer(struct ipuv3_channel *channel)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned int chno = channel->num;

	ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_CUR_BUF(chno));
}

void ipu_idmac_set_double_buffer(struct ipuv3_channel *channel,
		bool doublebuffer)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&ipu->lock, flags);

	reg = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(channel->num));
	if (doublebuffer)
		reg |= idma_mask(channel->num);
	else
		reg &= ~idma_mask(channel->num);
	ipu_cm_write(ipu, reg, IPU_CHA_DB_MODE_SEL(channel->num));

	__ipu_idmac_reset_current_buffer(channel);

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_idmac_set_double_buffer);

static const struct {
	int chnum;
	u32 reg;
	int shift;
} idmac_lock_en_info[] = {
	{ .chnum =  5, .reg = IDMAC_CH_LOCK_EN_1, .shift =  0, },
	{ .chnum = 11, .reg = IDMAC_CH_LOCK_EN_1, .shift =  2, },
	{ .chnum = 12, .reg = IDMAC_CH_LOCK_EN_1, .shift =  4, },
	{ .chnum = 14, .reg = IDMAC_CH_LOCK_EN_1, .shift =  6, },
	{ .chnum = 15, .reg = IDMAC_CH_LOCK_EN_1, .shift =  8, },
	{ .chnum = 20, .reg = IDMAC_CH_LOCK_EN_1, .shift = 10, },
	{ .chnum = 21, .reg = IDMAC_CH_LOCK_EN_1, .shift = 12, },
	{ .chnum = 22, .reg = IDMAC_CH_LOCK_EN_1, .shift = 14, },
	{ .chnum = 23, .reg = IDMAC_CH_LOCK_EN_1, .shift = 16, },
	{ .chnum = 27, .reg = IDMAC_CH_LOCK_EN_1, .shift = 18, },
	{ .chnum = 28, .reg = IDMAC_CH_LOCK_EN_1, .shift = 20, },
	{ .chnum = 45, .reg = IDMAC_CH_LOCK_EN_2, .shift =  0, },
	{ .chnum = 46, .reg = IDMAC_CH_LOCK_EN_2, .shift =  2, },
	{ .chnum = 47, .reg = IDMAC_CH_LOCK_EN_2, .shift =  4, },
	{ .chnum = 48, .reg = IDMAC_CH_LOCK_EN_2, .shift =  6, },
	{ .chnum = 49, .reg = IDMAC_CH_LOCK_EN_2, .shift =  8, },
	{ .chnum = 50, .reg = IDMAC_CH_LOCK_EN_2, .shift = 10, },
};

int ipu_idmac_lock_enable(struct ipuv3_channel *channel, int num_bursts)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned long flags;
	u32 bursts, regval;
	int i;

	switch (num_bursts) {
	case 0:
	case 1:
		bursts = 0x00; /* locking disabled */
		break;
	case 2:
		bursts = 0x01;
		break;
	case 4:
		bursts = 0x02;
		break;
	case 8:
		bursts = 0x03;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(idmac_lock_en_info); i++) {
		if (channel->num == idmac_lock_en_info[i].chnum)
			break;
	}
	if (i >= ARRAY_SIZE(idmac_lock_en_info))
		return -EINVAL;

	spin_lock_irqsave(&ipu->lock, flags);

	regval = ipu_idmac_read(ipu, idmac_lock_en_info[i].reg);
	regval &= ~(0x03 << idmac_lock_en_info[i].shift);
	regval |= (bursts << idmac_lock_en_info[i].shift);
	ipu_idmac_write(ipu, regval, idmac_lock_en_info[i].reg);

	spin_unlock_irqrestore(&ipu->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_idmac_lock_enable);

int ipu_module_enable(struct ipu_soc *ipu, u32 mask)
{
	unsigned long lock_flags;
	u32 val;

	spin_lock_irqsave(&ipu->lock, lock_flags);

	val = ipu_cm_read(ipu, IPU_DISP_GEN);

	if (mask & IPU_CONF_DI0_EN)
		val |= IPU_DI0_COUNTER_RELEASE;
	if (mask & IPU_CONF_DI1_EN)
		val |= IPU_DI1_COUNTER_RELEASE;

	ipu_cm_write(ipu, val, IPU_DISP_GEN);

	val = ipu_cm_read(ipu, IPU_CONF);
	val |= mask;
	ipu_cm_write(ipu, val, IPU_CONF);

	spin_unlock_irqrestore(&ipu->lock, lock_flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_module_enable);

int ipu_module_disable(struct ipu_soc *ipu, u32 mask)
{
	unsigned long lock_flags;
	u32 val;

	spin_lock_irqsave(&ipu->lock, lock_flags);

	val = ipu_cm_read(ipu, IPU_CONF);
	val &= ~mask;
	ipu_cm_write(ipu, val, IPU_CONF);

	val = ipu_cm_read(ipu, IPU_DISP_GEN);

	if (mask & IPU_CONF_DI0_EN)
		val &= ~IPU_DI0_COUNTER_RELEASE;
	if (mask & IPU_CONF_DI1_EN)
		val &= ~IPU_DI1_COUNTER_RELEASE;

	ipu_cm_write(ipu, val, IPU_DISP_GEN);

	spin_unlock_irqrestore(&ipu->lock, lock_flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_module_disable);

int ipu_idmac_get_current_buffer(struct ipuv3_channel *channel)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned int chno = channel->num;

	return (ipu_cm_read(ipu, IPU_CHA_CUR_BUF(chno)) & idma_mask(chno)) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(ipu_idmac_get_current_buffer);

bool ipu_idmac_buffer_is_ready(struct ipuv3_channel *channel, u32 buf_num)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned long flags;
	u32 reg = 0;

	spin_lock_irqsave(&ipu->lock, flags);
	switch (buf_num) {
	case 0:
		reg = ipu_cm_read(ipu, IPU_CHA_BUF0_RDY(channel->num));
		break;
	case 1:
		reg = ipu_cm_read(ipu, IPU_CHA_BUF1_RDY(channel->num));
		break;
	case 2:
		reg = ipu_cm_read(ipu, IPU_CHA_BUF2_RDY(channel->num));
		break;
	}
	spin_unlock_irqrestore(&ipu->lock, flags);

	return ((reg & idma_mask(channel->num)) != 0);
}
EXPORT_SYMBOL_GPL(ipu_idmac_buffer_is_ready);

void ipu_idmac_select_buffer(struct ipuv3_channel *channel, u32 buf_num)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned int chno = channel->num;
	unsigned long flags;

	spin_lock_irqsave(&ipu->lock, flags);

	/* Mark buffer as ready. */
	if (buf_num == 0)
		ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_BUF0_RDY(chno));
	else
		ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_BUF1_RDY(chno));

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_idmac_select_buffer);

void ipu_idmac_clear_buffer(struct ipuv3_channel *channel, u32 buf_num)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned int chno = channel->num;
	unsigned long flags;

	spin_lock_irqsave(&ipu->lock, flags);

	ipu_cm_write(ipu, 0xF0300000, IPU_GPR); /* write one to clear */
	switch (buf_num) {
	case 0:
		ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_BUF0_RDY(chno));
		break;
	case 1:
		ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_BUF1_RDY(chno));
		break;
	case 2:
		ipu_cm_write(ipu, idma_mask(chno), IPU_CHA_BUF2_RDY(chno));
		break;
	default:
		break;
	}
	ipu_cm_write(ipu, 0x0, IPU_GPR); /* write one to set */

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_idmac_clear_buffer);

int ipu_idmac_enable_channel(struct ipuv3_channel *channel)
{
	struct ipu_soc *ipu = channel->ipu;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&ipu->lock, flags);

	val = ipu_idmac_read(ipu, IDMAC_CHA_EN(channel->num));
	val |= idma_mask(channel->num);
	ipu_idmac_write(ipu, val, IDMAC_CHA_EN(channel->num));

	spin_unlock_irqrestore(&ipu->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_idmac_enable_channel);

bool ipu_idmac_channel_busy(struct ipu_soc *ipu, unsigned int chno)
{
	return (ipu_idmac_read(ipu, IDMAC_CHA_BUSY(chno)) & idma_mask(chno));
}
EXPORT_SYMBOL_GPL(ipu_idmac_channel_busy);

int ipu_idmac_wait_busy(struct ipuv3_channel *channel, int ms)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(ms);
	while (ipu_idmac_read(ipu, IDMAC_CHA_BUSY(channel->num)) &
			idma_mask(channel->num)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_idmac_wait_busy);

int ipu_wait_interrupt(struct ipu_soc *ipu, int irq, int ms)
{
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(ms);
	ipu_cm_write(ipu, BIT(irq % 32), IPU_INT_STAT(irq / 32));
	while (!(ipu_cm_read(ipu, IPU_INT_STAT(irq / 32) & BIT(irq % 32)))) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_wait_interrupt);

int ipu_idmac_disable_channel(struct ipuv3_channel *channel)
{
	struct ipu_soc *ipu = channel->ipu;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&ipu->lock, flags);

	/* Disable DMA channel(s) */
	val = ipu_idmac_read(ipu, IDMAC_CHA_EN(channel->num));
	val &= ~idma_mask(channel->num);
	ipu_idmac_write(ipu, val, IDMAC_CHA_EN(channel->num));

	__ipu_idmac_reset_current_buffer(channel);

	/* Set channel buffers NOT to be ready */
	ipu_cm_write(ipu, 0xf0000000, IPU_GPR); /* write one to clear */

	if (ipu_cm_read(ipu, IPU_CHA_BUF0_RDY(channel->num)) &
			idma_mask(channel->num)) {
		ipu_cm_write(ipu, idma_mask(channel->num),
			     IPU_CHA_BUF0_RDY(channel->num));
	}

	if (ipu_cm_read(ipu, IPU_CHA_BUF1_RDY(channel->num)) &
			idma_mask(channel->num)) {
		ipu_cm_write(ipu, idma_mask(channel->num),
			     IPU_CHA_BUF1_RDY(channel->num));
	}

	ipu_cm_write(ipu, 0x0, IPU_GPR); /* write one to set */

	/* Reset the double buffer */
	val = ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(channel->num));
	val &= ~idma_mask(channel->num);
	ipu_cm_write(ipu, val, IPU_CHA_DB_MODE_SEL(channel->num));

	spin_unlock_irqrestore(&ipu->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_idmac_disable_channel);

/*
 * The imx6 rev. D TRM says that enabling the WM feature will increase
 * a channel's priority. Refer to Table 36-8 Calculated priority value.
 * The sub-module that is the sink or source for the channel must enable
 * watermark signal for this to take effect (SMFC_WM for instance).
 */
void ipu_idmac_enable_watermark(struct ipuv3_channel *channel, bool enable)
{
	struct ipu_soc *ipu = channel->ipu;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ipu->lock, flags);

	val = ipu_idmac_read(ipu, IDMAC_WM_EN(channel->num));
	if (enable)
		val |= 1 << (channel->num % 32);
	else
		val &= ~(1 << (channel->num % 32));
	ipu_idmac_write(ipu, val, IDMAC_WM_EN(channel->num));

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_idmac_enable_watermark);

static int ipu_memory_reset(struct ipu_soc *ipu)
{
	unsigned long timeout;

	ipu_cm_write(ipu, 0x807FFFFF, IPU_MEM_RST);

	timeout = jiffies + msecs_to_jiffies(1000);
	while (ipu_cm_read(ipu, IPU_MEM_RST) & 0x80000000) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	return 0;
}

/*
 * Set the source mux for the given CSI. Selects either parallel or
 * MIPI CSI2 sources.
 */
void ipu_set_csi_src_mux(struct ipu_soc *ipu, int csi_id, bool mipi_csi2)
{
	unsigned long flags;
	u32 val, mask;

	mask = (csi_id == 1) ? IPU_CONF_CSI1_DATA_SOURCE :
		IPU_CONF_CSI0_DATA_SOURCE;

	spin_lock_irqsave(&ipu->lock, flags);

	val = ipu_cm_read(ipu, IPU_CONF);
	if (mipi_csi2)
		val |= mask;
	else
		val &= ~mask;
	ipu_cm_write(ipu, val, IPU_CONF);

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_set_csi_src_mux);

/*
 * Set the source mux for the IC. Selects either CSI[01] or the VDI.
 */
void ipu_set_ic_src_mux(struct ipu_soc *ipu, int csi_id, bool vdi)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ipu->lock, flags);

	val = ipu_cm_read(ipu, IPU_CONF);
	if (vdi) {
		val |= IPU_CONF_IC_INPUT;
	} else {
		val &= ~IPU_CONF_IC_INPUT;
		if (csi_id == 1)
			val |= IPU_CONF_CSI_SEL;
		else
			val &= ~IPU_CONF_CSI_SEL;
	}
	ipu_cm_write(ipu, val, IPU_CONF);

	spin_unlock_irqrestore(&ipu->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_set_ic_src_mux);


/* Frame Synchronization Unit Channel Linking */

struct fsu_link_reg_info {
	int chno;
	u32 reg;
	u32 mask;
	u32 val;
};

struct fsu_link_info {
	struct fsu_link_reg_info src;
	struct fsu_link_reg_info sink;
};

static const struct fsu_link_info fsu_link_info[] = {
	{
		.src  = { IPUV3_CHANNEL_IC_PRP_ENC_MEM, IPU_FS_PROC_FLOW2,
			  FS_PRP_ENC_DEST_SEL_MASK, FS_PRP_ENC_DEST_SEL_IRT_ENC },
		.sink = { IPUV3_CHANNEL_MEM_ROT_ENC, IPU_FS_PROC_FLOW1,
			  FS_PRPENC_ROT_SRC_SEL_MASK, FS_PRPENC_ROT_SRC_SEL_ENC },
	}, {
		.src =  { IPUV3_CHANNEL_IC_PRP_VF_MEM, IPU_FS_PROC_FLOW2,
			  FS_PRPVF_DEST_SEL_MASK, FS_PRPVF_DEST_SEL_IRT_VF },
		.sink = { IPUV3_CHANNEL_MEM_ROT_VF, IPU_FS_PROC_FLOW1,
			  FS_PRPVF_ROT_SRC_SEL_MASK, FS_PRPVF_ROT_SRC_SEL_VF },
	}, {
		.src =  { IPUV3_CHANNEL_IC_PP_MEM, IPU_FS_PROC_FLOW2,
			  FS_PP_DEST_SEL_MASK, FS_PP_DEST_SEL_IRT_PP },
		.sink = { IPUV3_CHANNEL_MEM_ROT_PP, IPU_FS_PROC_FLOW1,
			  FS_PP_ROT_SRC_SEL_MASK, FS_PP_ROT_SRC_SEL_PP },
	}, {
		.src =  { IPUV3_CHANNEL_CSI_DIRECT, 0 },
		.sink = { IPUV3_CHANNEL_CSI_VDI_PREV, IPU_FS_PROC_FLOW1,
			  FS_VDI_SRC_SEL_MASK, FS_VDI_SRC_SEL_CSI_DIRECT },
	},
};

static const struct fsu_link_info *find_fsu_link_info(int src, int sink)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fsu_link_info); i++) {
		if (src == fsu_link_info[i].src.chno &&
		    sink == fsu_link_info[i].sink.chno)
			return &fsu_link_info[i];
	}

	return NULL;
}

/*
 * Links a source channel to a sink channel in the FSU.
 */
int ipu_fsu_link(struct ipu_soc *ipu, int src_ch, int sink_ch)
{
	const struct fsu_link_info *link;
	u32 src_reg, sink_reg;
	unsigned long flags;

	link = find_fsu_link_info(src_ch, sink_ch);
	if (!link)
		return -EINVAL;

	spin_lock_irqsave(&ipu->lock, flags);

	if (link->src.mask) {
		src_reg = ipu_cm_read(ipu, link->src.reg);
		src_reg &= ~link->src.mask;
		src_reg |= link->src.val;
		ipu_cm_write(ipu, src_reg, link->src.reg);
	}

	if (link->sink.mask) {
		sink_reg = ipu_cm_read(ipu, link->sink.reg);
		sink_reg &= ~link->sink.mask;
		sink_reg |= link->sink.val;
		ipu_cm_write(ipu, sink_reg, link->sink.reg);
	}

	spin_unlock_irqrestore(&ipu->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ipu_fsu_link);

/*
 * Unlinks source and sink channels in the FSU.
 */
int ipu_fsu_unlink(struct ipu_soc *ipu, int src_ch, int sink_ch)
{
	const struct fsu_link_info *link;
	u32 src_reg, sink_reg;
	unsigned long flags;

	link = find_fsu_link_info(src_ch, sink_ch);
	if (!link)
		return -EINVAL;

	spin_lock_irqsave(&ipu->lock, flags);

	if (link->src.mask) {
		src_reg = ipu_cm_read(ipu, link->src.reg);
		src_reg &= ~link->src.mask;
		ipu_cm_write(ipu, src_reg, link->src.reg);
	}

	if (link->sink.mask) {
		sink_reg = ipu_cm_read(ipu, link->sink.reg);
		sink_reg &= ~link->sink.mask;
		ipu_cm_write(ipu, sink_reg, link->sink.reg);
	}

	spin_unlock_irqrestore(&ipu->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ipu_fsu_unlink);

/* Link IDMAC channels in the FSU */
int ipu_idmac_link(struct ipuv3_channel *src, struct ipuv3_channel *sink)
{
	return ipu_fsu_link(src->ipu, src->num, sink->num);
}
EXPORT_SYMBOL_GPL(ipu_idmac_link);

/* Unlink IDMAC channels in the FSU */
int ipu_idmac_unlink(struct ipuv3_channel *src, struct ipuv3_channel *sink)
{
	return ipu_fsu_unlink(src->ipu, src->num, sink->num);
}
EXPORT_SYMBOL_GPL(ipu_idmac_unlink);

struct ipu_devtype {
	const char *name;
	unsigned long cm_ofs;
	unsigned long cpmem_ofs;
	unsigned long srm_ofs;
	unsigned long tpm_ofs;
	unsigned long csi0_ofs;
	unsigned long csi1_ofs;
	unsigned long ic_ofs;
	unsigned long disp0_ofs;
	unsigned long disp1_ofs;
	unsigned long dc_tmpl_ofs;
	unsigned long vdi_ofs;
	enum ipuv3_type type;
};

static struct ipu_devtype ipu_type_imx51 = {
	.name = "IPUv3EX",
	.cm_ofs = 0x1e000000,
	.cpmem_ofs = 0x1f000000,
	.srm_ofs = 0x1f040000,
	.tpm_ofs = 0x1f060000,
	.csi0_ofs = 0x1f030000,
	.csi1_ofs = 0x1f038000,
	.ic_ofs = 0x1e020000,
	.disp0_ofs = 0x1e040000,
	.disp1_ofs = 0x1e048000,
	.dc_tmpl_ofs = 0x1f080000,
	.vdi_ofs = 0x1e068000,
	.type = IPUV3EX,
};

static struct ipu_devtype ipu_type_imx53 = {
	.name = "IPUv3M",
	.cm_ofs = 0x06000000,
	.cpmem_ofs = 0x07000000,
	.srm_ofs = 0x07040000,
	.tpm_ofs = 0x07060000,
	.csi0_ofs = 0x07030000,
	.csi1_ofs = 0x07038000,
	.ic_ofs = 0x06020000,
	.disp0_ofs = 0x06040000,
	.disp1_ofs = 0x06048000,
	.dc_tmpl_ofs = 0x07080000,
	.vdi_ofs = 0x06068000,
	.type = IPUV3M,
};

static struct ipu_devtype ipu_type_imx6q = {
	.name = "IPUv3H",
	.cm_ofs = 0x00200000,
	.cpmem_ofs = 0x00300000,
	.srm_ofs = 0x00340000,
	.tpm_ofs = 0x00360000,
	.csi0_ofs = 0x00230000,
	.csi1_ofs = 0x00238000,
	.ic_ofs = 0x00220000,
	.disp0_ofs = 0x00240000,
	.disp1_ofs = 0x00248000,
	.dc_tmpl_ofs = 0x00380000,
	.vdi_ofs = 0x00268000,
	.type = IPUV3H,
};

static const struct of_device_id imx_ipu_dt_ids[] = {
	{ .compatible = "fsl,imx51-ipu", .data = &ipu_type_imx51, },
	{ .compatible = "fsl,imx53-ipu", .data = &ipu_type_imx53, },
	{ .compatible = "fsl,imx6q-ipu", .data = &ipu_type_imx6q, },
	{ .compatible = "fsl,imx6qp-ipu", .data = &ipu_type_imx6q, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ipu_dt_ids);

static int ipu_submodules_init(struct ipu_soc *ipu,
		struct platform_device *pdev, unsigned long ipu_base,
		struct clk *ipu_clk)
{
	char *unit;
	int ret;
	struct device *dev = &pdev->dev;
	const struct ipu_devtype *devtype = ipu->devtype;

	ret = ipu_cpmem_init(ipu, dev, ipu_base + devtype->cpmem_ofs);
	if (ret) {
		unit = "cpmem";
		goto err_cpmem;
	}

	ret = ipu_csi_init(ipu, dev, 0, ipu_base + devtype->csi0_ofs,
			   IPU_CONF_CSI0_EN, ipu_clk);
	if (ret) {
		unit = "csi0";
		goto err_csi_0;
	}

	ret = ipu_csi_init(ipu, dev, 1, ipu_base + devtype->csi1_ofs,
			   IPU_CONF_CSI1_EN, ipu_clk);
	if (ret) {
		unit = "csi1";
		goto err_csi_1;
	}

	ret = ipu_ic_init(ipu, dev,
			  ipu_base + devtype->ic_ofs,
			  ipu_base + devtype->tpm_ofs);
	if (ret) {
		unit = "ic";
		goto err_ic;
	}

	ret = ipu_vdi_init(ipu, dev, ipu_base + devtype->vdi_ofs,
			   IPU_CONF_VDI_EN | IPU_CONF_ISP_EN |
			   IPU_CONF_IC_INPUT);
	if (ret) {
		unit = "vdi";
		goto err_vdi;
	}

	ret = ipu_image_convert_init(ipu, dev);
	if (ret) {
		unit = "image_convert";
		goto err_image_convert;
	}

	ret = ipu_di_init(ipu, dev, 0, ipu_base + devtype->disp0_ofs,
			  IPU_CONF_DI0_EN, ipu_clk);
	if (ret) {
		unit = "di0";
		goto err_di_0;
	}

	ret = ipu_di_init(ipu, dev, 1, ipu_base + devtype->disp1_ofs,
			IPU_CONF_DI1_EN, ipu_clk);
	if (ret) {
		unit = "di1";
		goto err_di_1;
	}

	ret = ipu_dc_init(ipu, dev, ipu_base + devtype->cm_ofs +
			IPU_CM_DC_REG_OFS, ipu_base + devtype->dc_tmpl_ofs);
	if (ret) {
		unit = "dc_template";
		goto err_dc;
	}

	ret = ipu_dmfc_init(ipu, dev, ipu_base +
			devtype->cm_ofs + IPU_CM_DMFC_REG_OFS, ipu_clk);
	if (ret) {
		unit = "dmfc";
		goto err_dmfc;
	}

	ret = ipu_dp_init(ipu, dev, ipu_base + devtype->srm_ofs);
	if (ret) {
		unit = "dp";
		goto err_dp;
	}

	ret = ipu_smfc_init(ipu, dev, ipu_base +
			devtype->cm_ofs + IPU_CM_SMFC_REG_OFS);
	if (ret) {
		unit = "smfc";
		goto err_smfc;
	}

	return 0;

err_smfc:
	ipu_dp_exit(ipu);
err_dp:
	ipu_dmfc_exit(ipu);
err_dmfc:
	ipu_dc_exit(ipu);
err_dc:
	ipu_di_exit(ipu, 1);
err_di_1:
	ipu_di_exit(ipu, 0);
err_di_0:
	ipu_image_convert_exit(ipu);
err_image_convert:
	ipu_vdi_exit(ipu);
err_vdi:
	ipu_ic_exit(ipu);
err_ic:
	ipu_csi_exit(ipu, 1);
err_csi_1:
	ipu_csi_exit(ipu, 0);
err_csi_0:
	ipu_cpmem_exit(ipu);
err_cpmem:
	dev_err(&pdev->dev, "init %s failed with %d\n", unit, ret);
	return ret;
}

static void ipu_irq_handle(struct ipu_soc *ipu, const int *regs, int num_regs)
{
	unsigned long status;
	int i, bit, irq;

	for (i = 0; i < num_regs; i++) {

		status = ipu_cm_read(ipu, IPU_INT_STAT(regs[i]));
		status &= ipu_cm_read(ipu, IPU_INT_CTRL(regs[i]));

		for_each_set_bit(bit, &status, 32) {
			irq = irq_linear_revmap(ipu->domain,
						regs[i] * 32 + bit);
			if (irq)
				generic_handle_irq(irq);
		}
	}
}

static void ipu_irq_handler(struct irq_desc *desc)
{
	struct ipu_soc *ipu = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	const int int_reg[] = { 0, 1, 2, 3, 10, 11, 12, 13, 14};

	chained_irq_enter(chip, desc);

	ipu_irq_handle(ipu, int_reg, ARRAY_SIZE(int_reg));

	chained_irq_exit(chip, desc);
}

static void ipu_err_irq_handler(struct irq_desc *desc)
{
	struct ipu_soc *ipu = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	const int int_reg[] = { 4, 5, 8, 9};

	chained_irq_enter(chip, desc);

	ipu_irq_handle(ipu, int_reg, ARRAY_SIZE(int_reg));

	chained_irq_exit(chip, desc);
}

int ipu_map_irq(struct ipu_soc *ipu, int irq)
{
	int virq;

	virq = irq_linear_revmap(ipu->domain, irq);
	if (!virq)
		virq = irq_create_mapping(ipu->domain, irq);

	return virq;
}
EXPORT_SYMBOL_GPL(ipu_map_irq);

int ipu_idmac_channel_irq(struct ipu_soc *ipu, struct ipuv3_channel *channel,
		enum ipu_channel_irq irq_type)
{
	return ipu_map_irq(ipu, irq_type + channel->num);
}
EXPORT_SYMBOL_GPL(ipu_idmac_channel_irq);

static void ipu_submodules_exit(struct ipu_soc *ipu)
{
	ipu_smfc_exit(ipu);
	ipu_dp_exit(ipu);
	ipu_dmfc_exit(ipu);
	ipu_dc_exit(ipu);
	ipu_di_exit(ipu, 1);
	ipu_di_exit(ipu, 0);
	ipu_image_convert_exit(ipu);
	ipu_vdi_exit(ipu);
	ipu_ic_exit(ipu);
	ipu_csi_exit(ipu, 1);
	ipu_csi_exit(ipu, 0);
	ipu_cpmem_exit(ipu);
}

static int platform_remove_devices_fn(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static void platform_device_unregister_children(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, platform_remove_devices_fn);
}

struct ipu_platform_reg {
	struct ipu_client_platformdata pdata;
	const char *name;
};

/* These must be in the order of the corresponding device tree port nodes */
static struct ipu_platform_reg client_reg[] = {
	{
		.pdata = {
			.csi = 0,
			.dma[0] = IPUV3_CHANNEL_CSI0,
			.dma[1] = -EINVAL,
		},
		.name = "imx-ipuv3-csi",
	}, {
		.pdata = {
			.csi = 1,
			.dma[0] = IPUV3_CHANNEL_CSI1,
			.dma[1] = -EINVAL,
		},
		.name = "imx-ipuv3-csi",
	}, {
		.pdata = {
			.di = 0,
			.dc = 5,
			.dp = IPU_DP_FLOW_SYNC_BG,
			.dma[0] = IPUV3_CHANNEL_MEM_BG_SYNC,
			.dma[1] = IPUV3_CHANNEL_MEM_FG_SYNC,
		},
		.name = "imx-ipuv3-crtc",
	}, {
		.pdata = {
			.di = 1,
			.dc = 1,
			.dp = -EINVAL,
			.dma[0] = IPUV3_CHANNEL_MEM_DC_SYNC,
			.dma[1] = -EINVAL,
		},
		.name = "imx-ipuv3-crtc",
	},
};

static DEFINE_MUTEX(ipu_client_id_mutex);
static int ipu_client_id;

static int ipu_add_client_devices(struct ipu_soc *ipu, unsigned long ipu_base)
{
	struct device *dev = ipu->dev;
	unsigned i;
	int id, ret;

	mutex_lock(&ipu_client_id_mutex);
	id = ipu_client_id;
	ipu_client_id += ARRAY_SIZE(client_reg);
	mutex_unlock(&ipu_client_id_mutex);

	for (i = 0; i < ARRAY_SIZE(client_reg); i++) {
		struct ipu_platform_reg *reg = &client_reg[i];
		struct platform_device *pdev;
		struct device_node *of_node;

		/* Associate subdevice with the corresponding port node */
		of_node = of_graph_get_port_by_id(dev->of_node, i);
		if (!of_node) {
			dev_info(dev,
				 "no port@%d node in %s, not using %s%d\n",
				 i, dev->of_node->full_name,
				 (i / 2) ? "DI" : "CSI", i % 2);
			continue;
		}

		pdev = platform_device_alloc(reg->name, id++);
		if (!pdev) {
			ret = -ENOMEM;
			goto err_register;
		}

		pdev->dev.parent = dev;

		reg->pdata.of_node = of_node;
		ret = platform_device_add_data(pdev, &reg->pdata,
					       sizeof(reg->pdata));
		if (!ret)
			ret = platform_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			goto err_register;
		}
	}

	return 0;

err_register:
	platform_device_unregister_children(to_platform_device(dev));

	return ret;
}


static int ipu_irq_init(struct ipu_soc *ipu)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	unsigned long unused[IPU_NUM_IRQS / 32] = {
		0x400100d0, 0xffe000fd,
		0x400100d0, 0xffe000fd,
		0x400100d0, 0xffe000fd,
		0x4077ffff, 0xffe7e1fd,
		0x23fffffe, 0x8880fff0,
		0xf98fe7d0, 0xfff81fff,
		0x400100d0, 0xffe000fd,
		0x00000000,
	};
	int ret, i;

	ipu->domain = irq_domain_add_linear(ipu->dev->of_node, IPU_NUM_IRQS,
					    &irq_generic_chip_ops, ipu);
	if (!ipu->domain) {
		dev_err(ipu->dev, "failed to add irq domain\n");
		return -ENODEV;
	}

	ret = irq_alloc_domain_generic_chips(ipu->domain, 32, 1, "IPU",
					     handle_level_irq, 0, 0, 0);
	if (ret < 0) {
		dev_err(ipu->dev, "failed to alloc generic irq chips\n");
		irq_domain_remove(ipu->domain);
		return ret;
	}

	/* Mask and clear all interrupts */
	for (i = 0; i < IPU_NUM_IRQS; i += 32) {
		ipu_cm_write(ipu, 0, IPU_INT_CTRL(i / 32));
		ipu_cm_write(ipu, ~unused[i / 32], IPU_INT_STAT(i / 32));
	}

	for (i = 0; i < IPU_NUM_IRQS; i += 32) {
		gc = irq_get_domain_generic_chip(ipu->domain, i);
		gc->reg_base = ipu->cm_reg;
		gc->unused = unused[i / 32];
		ct = gc->chip_types;
		ct->chip.irq_ack = irq_gc_ack_set_bit;
		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->regs.ack = IPU_INT_STAT(i / 32);
		ct->regs.mask = IPU_INT_CTRL(i / 32);
	}

	irq_set_chained_handler_and_data(ipu->irq_sync, ipu_irq_handler, ipu);
	irq_set_chained_handler_and_data(ipu->irq_err, ipu_err_irq_handler,
					 ipu);

	return 0;
}

static void ipu_irq_exit(struct ipu_soc *ipu)
{
	int i, irq;

	irq_set_chained_handler_and_data(ipu->irq_err, NULL, NULL);
	irq_set_chained_handler_and_data(ipu->irq_sync, NULL, NULL);

	/* TODO: remove irq_domain_generic_chips */

	for (i = 0; i < IPU_NUM_IRQS; i++) {
		irq = irq_linear_revmap(ipu->domain, i);
		if (irq)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(ipu->domain);
}

void ipu_dump(struct ipu_soc *ipu)
{
	int i;

	dev_dbg(ipu->dev, "IPU_CONF = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_CONF));
	dev_dbg(ipu->dev, "IDMAC_CONF = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_CONF));
	dev_dbg(ipu->dev, "IDMAC_CHA_EN1 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_CHA_EN(0)));
	dev_dbg(ipu->dev, "IDMAC_CHA_EN2 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_CHA_EN(32)));
	dev_dbg(ipu->dev, "IDMAC_CHA_PRI1 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_CHA_PRI(0)));
	dev_dbg(ipu->dev, "IDMAC_CHA_PRI2 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_CHA_PRI(32)));
	dev_dbg(ipu->dev, "IDMAC_BAND_EN1 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_BAND_EN(0)));
	dev_dbg(ipu->dev, "IDMAC_BAND_EN2 = \t0x%08X\n",
		ipu_idmac_read(ipu, IDMAC_BAND_EN(32)));
	dev_dbg(ipu->dev, "IPU_CHA_DB_MODE_SEL0 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(0)));
	dev_dbg(ipu->dev, "IPU_CHA_DB_MODE_SEL1 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_CHA_DB_MODE_SEL(32)));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW1 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_FS_PROC_FLOW1));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW2 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_FS_PROC_FLOW2));
	dev_dbg(ipu->dev, "IPU_FS_PROC_FLOW3 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_FS_PROC_FLOW3));
	dev_dbg(ipu->dev, "IPU_FS_DISP_FLOW1 = \t0x%08X\n",
		ipu_cm_read(ipu, IPU_FS_DISP_FLOW1));
	for (i = 0; i < 15; i++)
		dev_dbg(ipu->dev, "IPU_INT_CTRL(%d) = \t%08X\n", i,
			ipu_cm_read(ipu, IPU_INT_CTRL(i)));
}
EXPORT_SYMBOL_GPL(ipu_dump);

static int ipu_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ipu_soc *ipu;
	struct resource *res;
	unsigned long ipu_base;
	int i, ret, irq_sync, irq_err;
	const struct ipu_devtype *devtype;

	devtype = of_device_get_match_data(&pdev->dev);
	if (!devtype)
		return -EINVAL;

	irq_sync = platform_get_irq(pdev, 0);
	irq_err = platform_get_irq(pdev, 1);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev_dbg(&pdev->dev, "irq_sync: %d irq_err: %d\n",
			irq_sync, irq_err);

	if (!res || irq_sync < 0 || irq_err < 0)
		return -ENODEV;

	ipu_base = res->start;

	ipu = devm_kzalloc(&pdev->dev, sizeof(*ipu), GFP_KERNEL);
	if (!ipu)
		return -ENODEV;

	ipu->id = of_alias_get_id(np, "ipu");

	if (of_device_is_compatible(np, "fsl,imx6qp-ipu") &&
	    IS_ENABLED(CONFIG_DRM)) {
		ipu->prg_priv = ipu_prg_lookup_by_phandle(&pdev->dev,
							  "fsl,prg", ipu->id);
		if (!ipu->prg_priv)
			return -EPROBE_DEFER;
	}

	for (i = 0; i < 64; i++)
		ipu->channel[i].ipu = ipu;
	ipu->devtype = devtype;
	ipu->ipu_type = devtype->type;

	spin_lock_init(&ipu->lock);
	mutex_init(&ipu->channel_lock);

	dev_dbg(&pdev->dev, "cm_reg:   0x%08lx\n",
			ipu_base + devtype->cm_ofs);
	dev_dbg(&pdev->dev, "idmac:    0x%08lx\n",
			ipu_base + devtype->cm_ofs + IPU_CM_IDMAC_REG_OFS);
	dev_dbg(&pdev->dev, "cpmem:    0x%08lx\n",
			ipu_base + devtype->cpmem_ofs);
	dev_dbg(&pdev->dev, "csi0:    0x%08lx\n",
			ipu_base + devtype->csi0_ofs);
	dev_dbg(&pdev->dev, "csi1:    0x%08lx\n",
			ipu_base + devtype->csi1_ofs);
	dev_dbg(&pdev->dev, "ic:      0x%08lx\n",
			ipu_base + devtype->ic_ofs);
	dev_dbg(&pdev->dev, "disp0:    0x%08lx\n",
			ipu_base + devtype->disp0_ofs);
	dev_dbg(&pdev->dev, "disp1:    0x%08lx\n",
			ipu_base + devtype->disp1_ofs);
	dev_dbg(&pdev->dev, "srm:      0x%08lx\n",
			ipu_base + devtype->srm_ofs);
	dev_dbg(&pdev->dev, "tpm:      0x%08lx\n",
			ipu_base + devtype->tpm_ofs);
	dev_dbg(&pdev->dev, "dc:       0x%08lx\n",
			ipu_base + devtype->cm_ofs + IPU_CM_DC_REG_OFS);
	dev_dbg(&pdev->dev, "ic:       0x%08lx\n",
			ipu_base + devtype->cm_ofs + IPU_CM_IC_REG_OFS);
	dev_dbg(&pdev->dev, "dmfc:     0x%08lx\n",
			ipu_base + devtype->cm_ofs + IPU_CM_DMFC_REG_OFS);
	dev_dbg(&pdev->dev, "vdi:      0x%08lx\n",
			ipu_base + devtype->vdi_ofs);

	ipu->cm_reg = devm_ioremap(&pdev->dev,
			ipu_base + devtype->cm_ofs, PAGE_SIZE);
	ipu->idmac_reg = devm_ioremap(&pdev->dev,
			ipu_base + devtype->cm_ofs + IPU_CM_IDMAC_REG_OFS,
			PAGE_SIZE);

	if (!ipu->cm_reg || !ipu->idmac_reg)
		return -ENOMEM;

	ipu->clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(ipu->clk)) {
		ret = PTR_ERR(ipu->clk);
		dev_err(&pdev->dev, "clk_get failed with %d", ret);
		return ret;
	}

	platform_set_drvdata(pdev, ipu);

	ret = clk_prepare_enable(ipu->clk);
	if (ret) {
		dev_err(&pdev->dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}

	ipu->dev = &pdev->dev;
	ipu->irq_sync = irq_sync;
	ipu->irq_err = irq_err;

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to reset: %d\n", ret);
		goto out_failed_reset;
	}
	ret = ipu_memory_reset(ipu);
	if (ret)
		goto out_failed_reset;

	ret = ipu_irq_init(ipu);
	if (ret)
		goto out_failed_irq;

	/* Set MCU_T to divide MCU access window into 2 */
	ipu_cm_write(ipu, 0x00400000L | (IPU_MCU_T_DEFAULT << 18),
			IPU_DISP_GEN);

	ret = ipu_submodules_init(ipu, pdev, ipu_base, ipu->clk);
	if (ret)
		goto failed_submodules_init;

	ret = ipu_add_client_devices(ipu, ipu_base);
	if (ret) {
		dev_err(&pdev->dev, "adding client devices failed with %d\n",
				ret);
		goto failed_add_clients;
	}

	dev_info(&pdev->dev, "%s probed\n", devtype->name);

	return 0;

failed_add_clients:
	ipu_submodules_exit(ipu);
failed_submodules_init:
	ipu_irq_exit(ipu);
out_failed_irq:
out_failed_reset:
	clk_disable_unprepare(ipu->clk);
	return ret;
}

static int ipu_remove(struct platform_device *pdev)
{
	struct ipu_soc *ipu = platform_get_drvdata(pdev);

	platform_device_unregister_children(pdev);
	ipu_submodules_exit(ipu);
	ipu_irq_exit(ipu);

	clk_disable_unprepare(ipu->clk);

	return 0;
}

static struct platform_driver imx_ipu_driver = {
	.driver = {
		.name = "imx-ipuv3",
		.of_match_table = imx_ipu_dt_ids,
	},
	.probe = ipu_probe,
	.remove = ipu_remove,
};

static struct platform_driver * const drivers[] = {
#if IS_ENABLED(CONFIG_DRM)
	&ipu_pre_drv,
	&ipu_prg_drv,
#endif
	&imx_ipu_driver,
};

static int __init imx_ipu_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
module_init(imx_ipu_init);

static void __exit imx_ipu_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(imx_ipu_exit);

MODULE_ALIAS("platform:imx-ipuv3");
MODULE_DESCRIPTION("i.MX IPU v3 driver");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL");
