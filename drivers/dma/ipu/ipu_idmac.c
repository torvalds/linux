/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Copyright (C) 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <mach/ipu.h>

#include "ipu_intern.h"

#define FS_VF_IN_VALID	0x00000002
#define FS_ENC_IN_VALID	0x00000001

static int ipu_disable_channel(struct idmac *idmac, struct idmac_channel *ichan,
			       bool wait_for_stop);

/*
 * There can be only one, we could allocate it dynamically, but then we'd have
 * to add an extra parameter to some functions, and use something as ugly as
 *	struct ipu *ipu = to_ipu(to_idmac(ichan->dma_chan.device));
 * in the ISR
 */
static struct ipu ipu_data;

#define to_ipu(id) container_of(id, struct ipu, idmac)

static u32 __idmac_read_icreg(struct ipu *ipu, unsigned long reg)
{
	return __raw_readl(ipu->reg_ic + reg);
}

#define idmac_read_icreg(ipu, reg) __idmac_read_icreg(ipu, reg - IC_CONF)

static void __idmac_write_icreg(struct ipu *ipu, u32 value, unsigned long reg)
{
	__raw_writel(value, ipu->reg_ic + reg);
}

#define idmac_write_icreg(ipu, v, reg) __idmac_write_icreg(ipu, v, reg - IC_CONF)

static u32 idmac_read_ipureg(struct ipu *ipu, unsigned long reg)
{
	return __raw_readl(ipu->reg_ipu + reg);
}

static void idmac_write_ipureg(struct ipu *ipu, u32 value, unsigned long reg)
{
	__raw_writel(value, ipu->reg_ipu + reg);
}

/*****************************************************************************
 * IPU / IC common functions
 */
static void dump_idmac_reg(struct ipu *ipu)
{
	dev_dbg(ipu->dev, "IDMAC_CONF 0x%x, IC_CONF 0x%x, IDMAC_CHA_EN 0x%x, "
		"IDMAC_CHA_PRI 0x%x, IDMAC_CHA_BUSY 0x%x\n",
		idmac_read_icreg(ipu, IDMAC_CONF),
		idmac_read_icreg(ipu, IC_CONF),
		idmac_read_icreg(ipu, IDMAC_CHA_EN),
		idmac_read_icreg(ipu, IDMAC_CHA_PRI),
		idmac_read_icreg(ipu, IDMAC_CHA_BUSY));
	dev_dbg(ipu->dev, "BUF0_RDY 0x%x, BUF1_RDY 0x%x, CUR_BUF 0x%x, "
		"DB_MODE 0x%x, TASKS_STAT 0x%x\n",
		idmac_read_ipureg(ipu, IPU_CHA_BUF0_RDY),
		idmac_read_ipureg(ipu, IPU_CHA_BUF1_RDY),
		idmac_read_ipureg(ipu, IPU_CHA_CUR_BUF),
		idmac_read_ipureg(ipu, IPU_CHA_DB_MODE_SEL),
		idmac_read_ipureg(ipu, IPU_TASKS_STAT));
}

static uint32_t bytes_per_pixel(enum pixel_fmt fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_GENERIC:	/* generic data */
	case IPU_PIX_FMT_RGB332:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YUV422P:
	default:
		return 1;
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
		return 2;
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
		return 3;
	case IPU_PIX_FMT_GENERIC_32:	/* generic data */
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_ABGR32:
		return 4;
	}
}

/* Enable direct write to memory by the Camera Sensor Interface */
static void ipu_ic_enable_task(struct ipu *ipu, enum ipu_channel channel)
{
	uint32_t ic_conf, mask;

	switch (channel) {
	case IDMAC_IC_0:
		mask = IC_CONF_PRPENC_EN;
		break;
	case IDMAC_IC_7:
		mask = IC_CONF_RWS_EN | IC_CONF_PRPENC_EN;
		break;
	default:
		return;
	}
	ic_conf = idmac_read_icreg(ipu, IC_CONF) | mask;
	idmac_write_icreg(ipu, ic_conf, IC_CONF);
}

/* Called under spin_lock_irqsave(&ipu_data.lock) */
static void ipu_ic_disable_task(struct ipu *ipu, enum ipu_channel channel)
{
	uint32_t ic_conf, mask;

	switch (channel) {
	case IDMAC_IC_0:
		mask = IC_CONF_PRPENC_EN;
		break;
	case IDMAC_IC_7:
		mask = IC_CONF_RWS_EN | IC_CONF_PRPENC_EN;
		break;
	default:
		return;
	}
	ic_conf = idmac_read_icreg(ipu, IC_CONF) & ~mask;
	idmac_write_icreg(ipu, ic_conf, IC_CONF);
}

static uint32_t ipu_channel_status(struct ipu *ipu, enum ipu_channel channel)
{
	uint32_t stat = TASK_STAT_IDLE;
	uint32_t task_stat_reg = idmac_read_ipureg(ipu, IPU_TASKS_STAT);

	switch (channel) {
	case IDMAC_IC_7:
		stat = (task_stat_reg & TSTAT_CSI2MEM_MASK) >>
			TSTAT_CSI2MEM_OFFSET;
		break;
	case IDMAC_IC_0:
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
	default:
		break;
	}
	return stat;
}

struct chan_param_mem_planar {
	/* Word 0 */
	u32	xv:10;
	u32	yv:10;
	u32	xb:12;

	u32	yb:12;
	u32	res1:2;
	u32	nsb:1;
	u32	lnpb:6;
	u32	ubo_l:11;

	u32	ubo_h:15;
	u32	vbo_l:17;

	u32	vbo_h:9;
	u32	res2:3;
	u32	fw:12;
	u32	fh_l:8;

	u32	fh_h:4;
	u32	res3:28;

	/* Word 1 */
	u32	eba0;

	u32	eba1;

	u32	bpp:3;
	u32	sl:14;
	u32	pfs:3;
	u32	bam:3;
	u32	res4:2;
	u32	npb:6;
	u32	res5:1;

	u32	sat:2;
	u32	res6:30;
} __attribute__ ((packed));

struct chan_param_mem_interleaved {
	/* Word 0 */
	u32	xv:10;
	u32	yv:10;
	u32	xb:12;

	u32	yb:12;
	u32	sce:1;
	u32	res1:1;
	u32	nsb:1;
	u32	lnpb:6;
	u32	sx:10;
	u32	sy_l:1;

	u32	sy_h:9;
	u32	ns:10;
	u32	sm:10;
	u32	sdx_l:3;

	u32	sdx_h:2;
	u32	sdy:5;
	u32	sdrx:1;
	u32	sdry:1;
	u32	sdr1:1;
	u32	res2:2;
	u32	fw:12;
	u32	fh_l:8;

	u32	fh_h:4;
	u32	res3:28;

	/* Word 1 */
	u32	eba0;

	u32	eba1;

	u32	bpp:3;
	u32	sl:14;
	u32	pfs:3;
	u32	bam:3;
	u32	res4:2;
	u32	npb:6;
	u32	res5:1;

	u32	sat:2;
	u32	scc:1;
	u32	ofs0:5;
	u32	ofs1:5;
	u32	ofs2:5;
	u32	ofs3:5;
	u32	wid0:3;
	u32	wid1:3;
	u32	wid2:3;

	u32	wid3:3;
	u32	dec_sel:1;
	u32	res6:28;
} __attribute__ ((packed));

union chan_param_mem {
	struct chan_param_mem_planar		pp;
	struct chan_param_mem_interleaved	ip;
};

static void ipu_ch_param_set_plane_offset(union chan_param_mem *params,
					  u32 u_offset, u32 v_offset)
{
	params->pp.ubo_l = u_offset & 0x7ff;
	params->pp.ubo_h = u_offset >> 11;
	params->pp.vbo_l = v_offset & 0x1ffff;
	params->pp.vbo_h = v_offset >> 17;
}

static void ipu_ch_param_set_size(union chan_param_mem *params,
				  uint32_t pixel_fmt, uint16_t width,
				  uint16_t height, uint16_t stride)
{
	u32 u_offset;
	u32 v_offset;

	params->pp.fw		= width - 1;
	params->pp.fh_l		= height - 1;
	params->pp.fh_h		= (height - 1) >> 8;
	params->pp.sl		= stride - 1;

	switch (pixel_fmt) {
	case IPU_PIX_FMT_GENERIC:
		/*Represents 8-bit Generic data */
		params->pp.bpp	= 3;
		params->pp.pfs	= 7;
		params->pp.npb	= 31;
		params->pp.sat	= 2;		/* SAT = use 32-bit access */
		break;
	case IPU_PIX_FMT_GENERIC_32:
		/*Represents 32-bit Generic data */
		params->pp.bpp	= 0;
		params->pp.pfs	= 7;
		params->pp.npb	= 7;
		params->pp.sat	= 2;		/* SAT = use 32-bit access */
		break;
	case IPU_PIX_FMT_RGB565:
		params->ip.bpp	= 2;
		params->ip.pfs	= 4;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		params->ip.ofs0	= 0;		/* Red bit offset */
		params->ip.ofs1	= 5;		/* Green bit offset */
		params->ip.ofs2	= 11;		/* Blue bit offset */
		params->ip.ofs3	= 16;		/* Alpha bit offset */
		params->ip.wid0	= 4;		/* Red bit width - 1 */
		params->ip.wid1	= 5;		/* Green bit width - 1 */
		params->ip.wid2	= 4;		/* Blue bit width - 1 */
		break;
	case IPU_PIX_FMT_BGR24:
		params->ip.bpp	= 1;		/* 24 BPP & RGB PFS */
		params->ip.pfs	= 4;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		params->ip.ofs0	= 0;		/* Red bit offset */
		params->ip.ofs1	= 8;		/* Green bit offset */
		params->ip.ofs2	= 16;		/* Blue bit offset */
		params->ip.ofs3	= 24;		/* Alpha bit offset */
		params->ip.wid0	= 7;		/* Red bit width - 1 */
		params->ip.wid1	= 7;		/* Green bit width - 1 */
		params->ip.wid2	= 7;		/* Blue bit width - 1 */
		break;
	case IPU_PIX_FMT_RGB24:
		params->ip.bpp	= 1;		/* 24 BPP & RGB PFS */
		params->ip.pfs	= 4;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		params->ip.ofs0	= 16;		/* Red bit offset */
		params->ip.ofs1	= 8;		/* Green bit offset */
		params->ip.ofs2	= 0;		/* Blue bit offset */
		params->ip.ofs3	= 24;		/* Alpha bit offset */
		params->ip.wid0	= 7;		/* Red bit width - 1 */
		params->ip.wid1	= 7;		/* Green bit width - 1 */
		params->ip.wid2	= 7;		/* Blue bit width - 1 */
		break;
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_ABGR32:
		params->ip.bpp	= 0;
		params->ip.pfs	= 4;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		params->ip.ofs0	= 8;		/* Red bit offset */
		params->ip.ofs1	= 16;		/* Green bit offset */
		params->ip.ofs2	= 24;		/* Blue bit offset */
		params->ip.ofs3	= 0;		/* Alpha bit offset */
		params->ip.wid0	= 7;		/* Red bit width - 1 */
		params->ip.wid1	= 7;		/* Green bit width - 1 */
		params->ip.wid2	= 7;		/* Blue bit width - 1 */
		params->ip.wid3	= 7;		/* Alpha bit width - 1 */
		break;
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_RGB32:
		params->ip.bpp	= 0;
		params->ip.pfs	= 4;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		params->ip.ofs0	= 24;		/* Red bit offset */
		params->ip.ofs1	= 16;		/* Green bit offset */
		params->ip.ofs2	= 8;		/* Blue bit offset */
		params->ip.ofs3	= 0;		/* Alpha bit offset */
		params->ip.wid0	= 7;		/* Red bit width - 1 */
		params->ip.wid1	= 7;		/* Green bit width - 1 */
		params->ip.wid2	= 7;		/* Blue bit width - 1 */
		params->ip.wid3	= 7;		/* Alpha bit width - 1 */
		break;
	case IPU_PIX_FMT_UYVY:
		params->ip.bpp	= 2;
		params->ip.pfs	= 6;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		break;
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
		params->ip.bpp	= 3;
		params->ip.pfs	= 3;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		u_offset = stride * height;
		v_offset = u_offset + u_offset / 4;
		ipu_ch_param_set_plane_offset(params, u_offset, v_offset);
		break;
	case IPU_PIX_FMT_YVU422P:
		params->ip.bpp	= 3;
		params->ip.pfs	= 2;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		v_offset = stride * height;
		u_offset = v_offset + v_offset / 2;
		ipu_ch_param_set_plane_offset(params, u_offset, v_offset);
		break;
	case IPU_PIX_FMT_YUV422P:
		params->ip.bpp	= 3;
		params->ip.pfs	= 2;
		params->ip.npb	= 7;
		params->ip.sat	= 2;		/* SAT = 32-bit access */
		u_offset = stride * height;
		v_offset = u_offset + u_offset / 2;
		ipu_ch_param_set_plane_offset(params, u_offset, v_offset);
		break;
	default:
		dev_err(ipu_data.dev,
			"mx3 ipu: unimplemented pixel format %d\n", pixel_fmt);
		break;
	}

	params->pp.nsb = 1;
}

static void ipu_ch_param_set_burst_size(union chan_param_mem *params,
					uint16_t burst_pixels)
{
	params->pp.npb = burst_pixels - 1;
}

static void ipu_ch_param_set_buffer(union chan_param_mem *params,
				    dma_addr_t buf0, dma_addr_t buf1)
{
	params->pp.eba0 = buf0;
	params->pp.eba1 = buf1;
}

static void ipu_ch_param_set_rotation(union chan_param_mem *params,
				      enum ipu_rotate_mode rotate)
{
	params->pp.bam = rotate;
}

static void ipu_write_param_mem(uint32_t addr, uint32_t *data,
				uint32_t num_words)
{
	for (; num_words > 0; num_words--) {
		dev_dbg(ipu_data.dev,
			"write param mem - addr = 0x%08X, data = 0x%08X\n",
			addr, *data);
		idmac_write_ipureg(&ipu_data, addr, IPU_IMA_ADDR);
		idmac_write_ipureg(&ipu_data, *data++, IPU_IMA_DATA);
		addr++;
		if ((addr & 0x7) == 5) {
			addr &= ~0x7;	/* set to word 0 */
			addr += 8;	/* increment to next row */
		}
	}
}

static int calc_resize_coeffs(uint32_t in_size, uint32_t out_size,
			      uint32_t *resize_coeff,
			      uint32_t *downsize_coeff)
{
	uint32_t temp_size;
	uint32_t temp_downsize;

	*resize_coeff	= 1 << 13;
	*downsize_coeff	= 1 << 13;

	/* Cannot downsize more than 8:1 */
	if (out_size << 3 < in_size)
		return -EINVAL;

	/* compute downsizing coefficient */
	temp_downsize = 0;
	temp_size = in_size;
	while (temp_size >= out_size * 2 && temp_downsize < 2) {
		temp_size >>= 1;
		temp_downsize++;
	}
	*downsize_coeff = temp_downsize;

	/*
	 * compute resizing coefficient using the following formula:
	 * resize_coeff = M*(SI -1)/(SO - 1)
	 * where M = 2^13, SI - input size, SO - output size
	 */
	*resize_coeff = (8192L * (temp_size - 1)) / (out_size - 1);
	if (*resize_coeff >= 16384L) {
		dev_err(ipu_data.dev, "Warning! Overflow on resize coeff.\n");
		*resize_coeff = 0x3FFF;
	}

	dev_dbg(ipu_data.dev, "resizing from %u -> %u pixels, "
		"downsize=%u, resize=%u.%lu (reg=%u)\n", in_size, out_size,
		*downsize_coeff, *resize_coeff >= 8192L ? 1 : 0,
		((*resize_coeff & 0x1FFF) * 10000L) / 8192L, *resize_coeff);

	return 0;
}

static enum ipu_color_space format_to_colorspace(enum pixel_fmt fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_RGB32:
		return IPU_COLORSPACE_RGB;
	default:
		return IPU_COLORSPACE_YCBCR;
	}
}

static int ipu_ic_init_prpenc(struct ipu *ipu,
			      union ipu_channel_param *params, bool src_is_csi)
{
	uint32_t reg, ic_conf;
	uint32_t downsize_coeff, resize_coeff;
	enum ipu_color_space in_fmt, out_fmt;

	/* Setup vertical resizing */
	calc_resize_coeffs(params->video.in_height,
			    params->video.out_height,
			    &resize_coeff, &downsize_coeff);
	reg = (downsize_coeff << 30) | (resize_coeff << 16);

	/* Setup horizontal resizing */
	calc_resize_coeffs(params->video.in_width,
			    params->video.out_width,
			    &resize_coeff, &downsize_coeff);
	reg |= (downsize_coeff << 14) | resize_coeff;

	/* Setup color space conversion */
	in_fmt = format_to_colorspace(params->video.in_pixel_fmt);
	out_fmt = format_to_colorspace(params->video.out_pixel_fmt);

	/*
	 * Colourspace conversion unsupported yet - see _init_csc() in
	 * Freescale sources
	 */
	if (in_fmt != out_fmt) {
		dev_err(ipu->dev, "Colourspace conversion unsupported!\n");
		return -EOPNOTSUPP;
	}

	idmac_write_icreg(ipu, reg, IC_PRP_ENC_RSC);

	ic_conf = idmac_read_icreg(ipu, IC_CONF);

	if (src_is_csi)
		ic_conf &= ~IC_CONF_RWS_EN;
	else
		ic_conf |= IC_CONF_RWS_EN;

	idmac_write_icreg(ipu, ic_conf, IC_CONF);

	return 0;
}

static uint32_t dma_param_addr(uint32_t dma_ch)
{
	/* Channel Parameter Memory */
	return 0x10000 | (dma_ch << 4);
}

static void ipu_channel_set_priority(struct ipu *ipu, enum ipu_channel channel,
				     bool prio)
{
	u32 reg = idmac_read_icreg(ipu, IDMAC_CHA_PRI);

	if (prio)
		reg |= 1UL << channel;
	else
		reg &= ~(1UL << channel);

	idmac_write_icreg(ipu, reg, IDMAC_CHA_PRI);

	dump_idmac_reg(ipu);
}

static uint32_t ipu_channel_conf_mask(enum ipu_channel channel)
{
	uint32_t mask;

	switch (channel) {
	case IDMAC_IC_0:
	case IDMAC_IC_7:
		mask = IPU_CONF_CSI_EN | IPU_CONF_IC_EN;
		break;
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
		mask = IPU_CONF_SDC_EN | IPU_CONF_DI_EN;
		break;
	default:
		mask = 0;
		break;
	}

	return mask;
}

/**
 * ipu_enable_channel() - enable an IPU channel.
 * @idmac:	IPU DMAC context.
 * @ichan:	IDMAC channel.
 * @return:	0 on success or negative error code on failure.
 */
static int ipu_enable_channel(struct idmac *idmac, struct idmac_channel *ichan)
{
	struct ipu *ipu = to_ipu(idmac);
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	uint32_t reg;
	unsigned long flags;

	spin_lock_irqsave(&ipu->lock, flags);

	/* Reset to buffer 0 */
	idmac_write_ipureg(ipu, 1UL << channel, IPU_CHA_CUR_BUF);
	ichan->active_buffer = 0;
	ichan->status = IPU_CHANNEL_ENABLED;

	switch (channel) {
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
	case IDMAC_IC_7:
		ipu_channel_set_priority(ipu, channel, true);
	default:
		break;
	}

	reg = idmac_read_icreg(ipu, IDMAC_CHA_EN);

	idmac_write_icreg(ipu, reg | (1UL << channel), IDMAC_CHA_EN);

	ipu_ic_enable_task(ipu, channel);

	spin_unlock_irqrestore(&ipu->lock, flags);
	return 0;
}

/**
 * ipu_init_channel_buffer() - initialize a buffer for logical IPU channel.
 * @ichan:	IDMAC channel.
 * @pixel_fmt:	pixel format of buffer. Pixel format is a FOURCC ASCII code.
 * @width:	width of buffer in pixels.
 * @height:	height of buffer in pixels.
 * @stride:	stride length of buffer in pixels.
 * @rot_mode:	rotation mode of buffer. A rotation setting other than
 *		IPU_ROTATE_VERT_FLIP should only be used for input buffers of
 *		rotation channels.
 * @phyaddr_0:	buffer 0 physical address.
 * @phyaddr_1:	buffer 1 physical address. Setting this to a value other than
 *		NULL enables double buffering mode.
 * @return:	0 on success or negative error code on failure.
 */
static int ipu_init_channel_buffer(struct idmac_channel *ichan,
				   enum pixel_fmt pixel_fmt,
				   uint16_t width, uint16_t height,
				   uint32_t stride,
				   enum ipu_rotate_mode rot_mode,
				   dma_addr_t phyaddr_0, dma_addr_t phyaddr_1)
{
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	struct idmac *idmac = to_idmac(ichan->dma_chan.device);
	struct ipu *ipu = to_ipu(idmac);
	union chan_param_mem params = {};
	unsigned long flags;
	uint32_t reg;
	uint32_t stride_bytes;

	stride_bytes = stride * bytes_per_pixel(pixel_fmt);

	if (stride_bytes % 4) {
		dev_err(ipu->dev,
			"Stride length must be 32-bit aligned, stride = %d, bytes = %d\n",
			stride, stride_bytes);
		return -EINVAL;
	}

	/* IC channel's stride must be a multiple of 8 pixels */
	if ((channel <= IDMAC_IC_13) && (stride % 8)) {
		dev_err(ipu->dev, "Stride must be 8 pixel multiple\n");
		return -EINVAL;
	}

	/* Build parameter memory data for DMA channel */
	ipu_ch_param_set_size(&params, pixel_fmt, width, height, stride_bytes);
	ipu_ch_param_set_buffer(&params, phyaddr_0, phyaddr_1);
	ipu_ch_param_set_rotation(&params, rot_mode);
	/* Some channels (rotation) have restriction on burst length */
	switch (channel) {
	case IDMAC_IC_7:	/* Hangs with burst 8, 16, other values
				   invalid - Table 44-30 */
/*
		ipu_ch_param_set_burst_size(&params, 8);
 */
		break;
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
		/* In original code only IPU_PIX_FMT_RGB565 was setting burst */
		ipu_ch_param_set_burst_size(&params, 16);
		break;
	case IDMAC_IC_0:
	default:
		break;
	}

	spin_lock_irqsave(&ipu->lock, flags);

	ipu_write_param_mem(dma_param_addr(channel), (uint32_t *)&params, 10);

	reg = idmac_read_ipureg(ipu, IPU_CHA_DB_MODE_SEL);

	if (phyaddr_1)
		reg |= 1UL << channel;
	else
		reg &= ~(1UL << channel);

	idmac_write_ipureg(ipu, reg, IPU_CHA_DB_MODE_SEL);

	ichan->status = IPU_CHANNEL_READY;

	spin_unlock_irqrestore(&ipu->lock, flags);

	return 0;
}

/**
 * ipu_select_buffer() - mark a channel's buffer as ready.
 * @channel:	channel ID.
 * @buffer_n:	buffer number to mark ready.
 */
static void ipu_select_buffer(enum ipu_channel channel, int buffer_n)
{
	/* No locking - this is a write-one-to-set register, cleared by IPU */
	if (buffer_n == 0)
		/* Mark buffer 0 as ready. */
		idmac_write_ipureg(&ipu_data, 1UL << channel, IPU_CHA_BUF0_RDY);
	else
		/* Mark buffer 1 as ready. */
		idmac_write_ipureg(&ipu_data, 1UL << channel, IPU_CHA_BUF1_RDY);
}

/**
 * ipu_update_channel_buffer() - update physical address of a channel buffer.
 * @ichan:	IDMAC channel.
 * @buffer_n:	buffer number to update.
 *		0 or 1 are the only valid values.
 * @phyaddr:	buffer physical address.
 */
/* Called under spin_lock(_irqsave)(&ichan->lock) */
static void ipu_update_channel_buffer(struct idmac_channel *ichan,
				      int buffer_n, dma_addr_t phyaddr)
{
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	uint32_t reg;
	unsigned long flags;

	spin_lock_irqsave(&ipu_data.lock, flags);

	if (buffer_n == 0) {
		reg = idmac_read_ipureg(&ipu_data, IPU_CHA_BUF0_RDY);
		if (reg & (1UL << channel)) {
			ipu_ic_disable_task(&ipu_data, channel);
			ichan->status = IPU_CHANNEL_READY;
		}

		/* 44.3.3.1.9 - Row Number 1 (WORD1, offset 0) */
		idmac_write_ipureg(&ipu_data, dma_param_addr(channel) +
				   0x0008UL, IPU_IMA_ADDR);
		idmac_write_ipureg(&ipu_data, phyaddr, IPU_IMA_DATA);
	} else {
		reg = idmac_read_ipureg(&ipu_data, IPU_CHA_BUF1_RDY);
		if (reg & (1UL << channel)) {
			ipu_ic_disable_task(&ipu_data, channel);
			ichan->status = IPU_CHANNEL_READY;
		}

		/* Check if double-buffering is already enabled */
		reg = idmac_read_ipureg(&ipu_data, IPU_CHA_DB_MODE_SEL);

		if (!(reg & (1UL << channel)))
			idmac_write_ipureg(&ipu_data, reg | (1UL << channel),
					   IPU_CHA_DB_MODE_SEL);

		/* 44.3.3.1.9 - Row Number 1 (WORD1, offset 1) */
		idmac_write_ipureg(&ipu_data, dma_param_addr(channel) +
				   0x0009UL, IPU_IMA_ADDR);
		idmac_write_ipureg(&ipu_data, phyaddr, IPU_IMA_DATA);
	}

	spin_unlock_irqrestore(&ipu_data.lock, flags);
}

/* Called under spin_lock_irqsave(&ichan->lock) */
static int ipu_submit_buffer(struct idmac_channel *ichan,
	struct idmac_tx_desc *desc, struct scatterlist *sg, int buf_idx)
{
	unsigned int chan_id = ichan->dma_chan.chan_id;
	struct device *dev = &ichan->dma_chan.dev->device;

	if (async_tx_test_ack(&desc->txd))
		return -EINTR;

	/*
	 * On first invocation this shouldn't be necessary, the call to
	 * ipu_init_channel_buffer() above will set addresses for us, so we
	 * could make it conditional on status >= IPU_CHANNEL_ENABLED, but
	 * doing it again shouldn't hurt either.
	 */
	ipu_update_channel_buffer(ichan, buf_idx, sg_dma_address(sg));

	ipu_select_buffer(chan_id, buf_idx);
	dev_dbg(dev, "Updated sg %p on channel 0x%x buffer %d\n",
		sg, chan_id, buf_idx);

	return 0;
}

/* Called under spin_lock_irqsave(&ichan->lock) */
static int ipu_submit_channel_buffers(struct idmac_channel *ichan,
				      struct idmac_tx_desc *desc)
{
	struct scatterlist *sg;
	int i, ret = 0;

	for (i = 0, sg = desc->sg; i < 2 && sg; i++) {
		if (!ichan->sg[i]) {
			ichan->sg[i] = sg;

			ret = ipu_submit_buffer(ichan, desc, sg, i);
			if (ret < 0)
				return ret;

			sg = sg_next(sg);
		}
	}

	return ret;
}

static dma_cookie_t idmac_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct idmac_tx_desc *desc = to_tx_desc(tx);
	struct idmac_channel *ichan = to_idmac_chan(tx->chan);
	struct idmac *idmac = to_idmac(tx->chan->device);
	struct ipu *ipu = to_ipu(idmac);
	struct device *dev = &ichan->dma_chan.dev->device;
	dma_cookie_t cookie;
	unsigned long flags;
	int ret;

	/* Sanity check */
	if (!list_empty(&desc->list)) {
		/* The descriptor doesn't belong to client */
		dev_err(dev, "Descriptor %p not prepared!\n", tx);
		return -EBUSY;
	}

	mutex_lock(&ichan->chan_mutex);

	async_tx_clear_ack(tx);

	if (ichan->status < IPU_CHANNEL_READY) {
		struct idmac_video_param *video = &ichan->params.video;
		/*
		 * Initial buffer assignment - the first two sg-entries from
		 * the descriptor will end up in the IDMAC buffers
		 */
		dma_addr_t dma_1 = sg_is_last(desc->sg) ? 0 :
			sg_dma_address(&desc->sg[1]);

		WARN_ON(ichan->sg[0] || ichan->sg[1]);

		cookie = ipu_init_channel_buffer(ichan,
						 video->out_pixel_fmt,
						 video->out_width,
						 video->out_height,
						 video->out_stride,
						 IPU_ROTATE_NONE,
						 sg_dma_address(&desc->sg[0]),
						 dma_1);
		if (cookie < 0)
			goto out;
	}

	dev_dbg(dev, "Submitting sg %p\n", &desc->sg[0]);

	cookie = ichan->dma_chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	/* from dmaengine.h: "last cookie value returned to client" */
	ichan->dma_chan.cookie = cookie;
	tx->cookie = cookie;

	/* ipu->lock can be taken under ichan->lock, but not v.v. */
	spin_lock_irqsave(&ichan->lock, flags);

	list_add_tail(&desc->list, &ichan->queue);
	/* submit_buffers() atomically verifies and fills empty sg slots */
	ret = ipu_submit_channel_buffers(ichan, desc);

	spin_unlock_irqrestore(&ichan->lock, flags);

	if (ret < 0) {
		cookie = ret;
		goto dequeue;
	}

	if (ichan->status < IPU_CHANNEL_ENABLED) {
		ret = ipu_enable_channel(idmac, ichan);
		if (ret < 0) {
			cookie = ret;
			goto dequeue;
		}
	}

	dump_idmac_reg(ipu);

dequeue:
	if (cookie < 0) {
		spin_lock_irqsave(&ichan->lock, flags);
		list_del_init(&desc->list);
		spin_unlock_irqrestore(&ichan->lock, flags);
		tx->cookie = cookie;
		ichan->dma_chan.cookie = cookie;
	}

out:
	mutex_unlock(&ichan->chan_mutex);

	return cookie;
}

/* Called with ichan->chan_mutex held */
static int idmac_desc_alloc(struct idmac_channel *ichan, int n)
{
	struct idmac_tx_desc *desc = vmalloc(n * sizeof(struct idmac_tx_desc));
	struct idmac *idmac = to_idmac(ichan->dma_chan.device);

	if (!desc)
		return -ENOMEM;

	/* No interrupts, just disable the tasklet for a moment */
	tasklet_disable(&to_ipu(idmac)->tasklet);

	ichan->n_tx_desc = n;
	ichan->desc = desc;
	INIT_LIST_HEAD(&ichan->queue);
	INIT_LIST_HEAD(&ichan->free_list);

	while (n--) {
		struct dma_async_tx_descriptor *txd = &desc->txd;

		memset(txd, 0, sizeof(*txd));
		dma_async_tx_descriptor_init(txd, &ichan->dma_chan);
		txd->tx_submit		= idmac_tx_submit;

		list_add(&desc->list, &ichan->free_list);

		desc++;
	}

	tasklet_enable(&to_ipu(idmac)->tasklet);

	return 0;
}

/**
 * ipu_init_channel() - initialize an IPU channel.
 * @idmac:	IPU DMAC context.
 * @ichan:	pointer to the channel object.
 * @return      0 on success or negative error code on failure.
 */
static int ipu_init_channel(struct idmac *idmac, struct idmac_channel *ichan)
{
	union ipu_channel_param *params = &ichan->params;
	uint32_t ipu_conf;
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	unsigned long flags;
	uint32_t reg;
	struct ipu *ipu = to_ipu(idmac);
	int ret = 0, n_desc = 0;

	dev_dbg(ipu->dev, "init channel = %d\n", channel);

	if (channel != IDMAC_SDC_0 && channel != IDMAC_SDC_1 &&
	    channel != IDMAC_IC_7)
		return -EINVAL;

	spin_lock_irqsave(&ipu->lock, flags);

	switch (channel) {
	case IDMAC_IC_7:
		n_desc = 16;
		reg = idmac_read_icreg(ipu, IC_CONF);
		idmac_write_icreg(ipu, reg & ~IC_CONF_CSI_MEM_WR_EN, IC_CONF);
		break;
	case IDMAC_IC_0:
		n_desc = 16;
		reg = idmac_read_ipureg(ipu, IPU_FS_PROC_FLOW);
		idmac_write_ipureg(ipu, reg & ~FS_ENC_IN_VALID, IPU_FS_PROC_FLOW);
		ret = ipu_ic_init_prpenc(ipu, params, true);
		break;
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
		n_desc = 4;
	default:
		break;
	}

	ipu->channel_init_mask |= 1L << channel;

	/* Enable IPU sub module */
	ipu_conf = idmac_read_ipureg(ipu, IPU_CONF) |
		ipu_channel_conf_mask(channel);
	idmac_write_ipureg(ipu, ipu_conf, IPU_CONF);

	spin_unlock_irqrestore(&ipu->lock, flags);

	if (n_desc && !ichan->desc)
		ret = idmac_desc_alloc(ichan, n_desc);

	dump_idmac_reg(ipu);

	return ret;
}

/**
 * ipu_uninit_channel() - uninitialize an IPU channel.
 * @idmac:	IPU DMAC context.
 * @ichan:	pointer to the channel object.
 */
static void ipu_uninit_channel(struct idmac *idmac, struct idmac_channel *ichan)
{
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	unsigned long flags;
	uint32_t reg;
	unsigned long chan_mask = 1UL << channel;
	uint32_t ipu_conf;
	struct ipu *ipu = to_ipu(idmac);

	spin_lock_irqsave(&ipu->lock, flags);

	if (!(ipu->channel_init_mask & chan_mask)) {
		dev_err(ipu->dev, "Channel already uninitialized %d\n",
			channel);
		spin_unlock_irqrestore(&ipu->lock, flags);
		return;
	}

	/* Reset the double buffer */
	reg = idmac_read_ipureg(ipu, IPU_CHA_DB_MODE_SEL);
	idmac_write_ipureg(ipu, reg & ~chan_mask, IPU_CHA_DB_MODE_SEL);

	ichan->sec_chan_en = false;

	switch (channel) {
	case IDMAC_IC_7:
		reg = idmac_read_icreg(ipu, IC_CONF);
		idmac_write_icreg(ipu, reg & ~(IC_CONF_RWS_EN | IC_CONF_PRPENC_EN),
			     IC_CONF);
		break;
	case IDMAC_IC_0:
		reg = idmac_read_icreg(ipu, IC_CONF);
		idmac_write_icreg(ipu, reg & ~(IC_CONF_PRPENC_EN | IC_CONF_PRPENC_CSC1),
				  IC_CONF);
		break;
	case IDMAC_SDC_0:
	case IDMAC_SDC_1:
	default:
		break;
	}

	ipu->channel_init_mask &= ~(1L << channel);

	ipu_conf = idmac_read_ipureg(ipu, IPU_CONF) &
		~ipu_channel_conf_mask(channel);
	idmac_write_ipureg(ipu, ipu_conf, IPU_CONF);

	spin_unlock_irqrestore(&ipu->lock, flags);

	ichan->n_tx_desc = 0;
	vfree(ichan->desc);
	ichan->desc = NULL;
}

/**
 * ipu_disable_channel() - disable an IPU channel.
 * @idmac:		IPU DMAC context.
 * @ichan:		channel object pointer.
 * @wait_for_stop:	flag to set whether to wait for channel end of frame or
 *			return immediately.
 * @return:		0 on success or negative error code on failure.
 */
static int ipu_disable_channel(struct idmac *idmac, struct idmac_channel *ichan,
			       bool wait_for_stop)
{
	enum ipu_channel channel = ichan->dma_chan.chan_id;
	struct ipu *ipu = to_ipu(idmac);
	uint32_t reg;
	unsigned long flags;
	unsigned long chan_mask = 1UL << channel;
	unsigned int timeout;

	if (wait_for_stop && channel != IDMAC_SDC_1 && channel != IDMAC_SDC_0) {
		timeout = 40;
		/* This waiting always fails. Related to spurious irq problem */
		while ((idmac_read_icreg(ipu, IDMAC_CHA_BUSY) & chan_mask) ||
		       (ipu_channel_status(ipu, channel) == TASK_STAT_ACTIVE)) {
			timeout--;
			msleep(10);

			if (!timeout) {
				dev_dbg(ipu->dev,
					"Warning: timeout waiting for channel %u to "
					"stop: buf0_rdy = 0x%08X, buf1_rdy = 0x%08X, "
					"busy = 0x%08X, tstat = 0x%08X\n", channel,
					idmac_read_ipureg(ipu, IPU_CHA_BUF0_RDY),
					idmac_read_ipureg(ipu, IPU_CHA_BUF1_RDY),
					idmac_read_icreg(ipu, IDMAC_CHA_BUSY),
					idmac_read_ipureg(ipu, IPU_TASKS_STAT));
				break;
			}
		}
		dev_dbg(ipu->dev, "timeout = %d * 10ms\n", 40 - timeout);
	}
	/* SDC BG and FG must be disabled before DMA is disabled */
	if (wait_for_stop && (channel == IDMAC_SDC_0 ||
			      channel == IDMAC_SDC_1)) {
		for (timeout = 5;
		     timeout && !ipu_irq_status(ichan->eof_irq); timeout--)
			msleep(5);
	}

	spin_lock_irqsave(&ipu->lock, flags);

	/* Disable IC task */
	ipu_ic_disable_task(ipu, channel);

	/* Disable DMA channel(s) */
	reg = idmac_read_icreg(ipu, IDMAC_CHA_EN);
	idmac_write_icreg(ipu, reg & ~chan_mask, IDMAC_CHA_EN);

	spin_unlock_irqrestore(&ipu->lock, flags);

	return 0;
}

static struct scatterlist *idmac_sg_next(struct idmac_channel *ichan,
	struct idmac_tx_desc **desc, struct scatterlist *sg)
{
	struct scatterlist *sgnew = sg ? sg_next(sg) : NULL;

	if (sgnew)
		/* next sg-element in this list */
		return sgnew;

	if ((*desc)->list.next == &ichan->queue)
		/* No more descriptors on the queue */
		return NULL;

	/* Fetch next descriptor */
	*desc = list_entry((*desc)->list.next, struct idmac_tx_desc, list);
	return (*desc)->sg;
}

/*
 * We have several possibilities here:
 * current BUF		next BUF
 *
 * not last sg		next not last sg
 * not last sg		next last sg
 * last sg		first sg from next descriptor
 * last sg		NULL
 *
 * Besides, the descriptor queue might be empty or not. We process all these
 * cases carefully.
 */
static irqreturn_t idmac_interrupt(int irq, void *dev_id)
{
	struct idmac_channel *ichan = dev_id;
	struct device *dev = &ichan->dma_chan.dev->device;
	unsigned int chan_id = ichan->dma_chan.chan_id;
	struct scatterlist **sg, *sgnext, *sgnew = NULL;
	/* Next transfer descriptor */
	struct idmac_tx_desc *desc, *descnew;
	dma_async_tx_callback callback;
	void *callback_param;
	bool done = false;
	u32 ready0, ready1, curbuf, err;
	unsigned long flags;

	/* IDMAC has cleared the respective BUFx_RDY bit, we manage the buffer */

	dev_dbg(dev, "IDMAC irq %d, buf %d\n", irq, ichan->active_buffer);

	spin_lock_irqsave(&ipu_data.lock, flags);

	ready0	= idmac_read_ipureg(&ipu_data, IPU_CHA_BUF0_RDY);
	ready1	= idmac_read_ipureg(&ipu_data, IPU_CHA_BUF1_RDY);
	curbuf	= idmac_read_ipureg(&ipu_data, IPU_CHA_CUR_BUF);
	err	= idmac_read_ipureg(&ipu_data, IPU_INT_STAT_4);

	if (err & (1 << chan_id)) {
		idmac_write_ipureg(&ipu_data, 1 << chan_id, IPU_INT_STAT_4);
		spin_unlock_irqrestore(&ipu_data.lock, flags);
		/*
		 * Doing this
		 * ichan->sg[0] = ichan->sg[1] = NULL;
		 * you can force channel re-enable on the next tx_submit(), but
		 * this is dirty - think about descriptors with multiple
		 * sg elements.
		 */
		dev_warn(dev, "NFB4EOF on channel %d, ready %x, %x, cur %x\n",
			 chan_id, ready0, ready1, curbuf);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&ipu_data.lock, flags);

	/* Other interrupts do not interfere with this channel */
	spin_lock(&ichan->lock);
	if (unlikely((ichan->active_buffer && (ready1 >> chan_id) & 1) ||
		     (!ichan->active_buffer && (ready0 >> chan_id) & 1)
		     )) {
		spin_unlock(&ichan->lock);
		dev_dbg(dev,
			"IRQ with active buffer still ready on channel %x, "
			"active %d, ready %x, %x!\n", chan_id,
			ichan->active_buffer, ready0, ready1);
		return IRQ_NONE;
	}

	if (unlikely(list_empty(&ichan->queue))) {
		ichan->sg[ichan->active_buffer] = NULL;
		spin_unlock(&ichan->lock);
		dev_err(dev,
			"IRQ without queued buffers on channel %x, active %d, "
			"ready %x, %x!\n", chan_id,
			ichan->active_buffer, ready0, ready1);
		return IRQ_NONE;
	}

	/*
	 * active_buffer is a software flag, it shows which buffer we are
	 * currently expecting back from the hardware, IDMAC should be
	 * processing the other buffer already
	 */
	sg = &ichan->sg[ichan->active_buffer];
	sgnext = ichan->sg[!ichan->active_buffer];

	if (!*sg) {
		spin_unlock(&ichan->lock);
		return IRQ_HANDLED;
	}

	desc = list_entry(ichan->queue.next, struct idmac_tx_desc, list);
	descnew = desc;

	dev_dbg(dev, "IDMAC irq %d, dma 0x%08x, next dma 0x%08x, current %d, curbuf 0x%08x\n",
		irq, sg_dma_address(*sg), sgnext ? sg_dma_address(sgnext) : 0, ichan->active_buffer, curbuf);

	/* Find the descriptor of sgnext */
	sgnew = idmac_sg_next(ichan, &descnew, *sg);
	if (sgnext != sgnew)
		dev_err(dev, "Submitted buffer %p, next buffer %p\n", sgnext, sgnew);

	/*
	 * if sgnext == NULL sg must be the last element in a scatterlist and
	 * queue must be empty
	 */
	if (unlikely(!sgnext)) {
		if (!WARN_ON(sg_next(*sg)))
			dev_dbg(dev, "Underrun on channel %x\n", chan_id);
		ichan->sg[!ichan->active_buffer] = sgnew;

		if (unlikely(sgnew)) {
			ipu_submit_buffer(ichan, descnew, sgnew, !ichan->active_buffer);
		} else {
			spin_lock_irqsave(&ipu_data.lock, flags);
			ipu_ic_disable_task(&ipu_data, chan_id);
			spin_unlock_irqrestore(&ipu_data.lock, flags);
			ichan->status = IPU_CHANNEL_READY;
			/* Continue to check for complete descriptor */
		}
	}

	/* Calculate and submit the next sg element */
	sgnew = idmac_sg_next(ichan, &descnew, sgnew);

	if (unlikely(!sg_next(*sg)) || !sgnext) {
		/*
		 * Last element in scatterlist done, remove from the queue,
		 * _init for debugging
		 */
		list_del_init(&desc->list);
		done = true;
	}

	*sg = sgnew;

	if (likely(sgnew) &&
	    ipu_submit_buffer(ichan, descnew, sgnew, ichan->active_buffer) < 0) {
		callback = descnew->txd.callback;
		callback_param = descnew->txd.callback_param;
		spin_unlock(&ichan->lock);
		if (callback)
			callback(callback_param);
		spin_lock(&ichan->lock);
	}

	/* Flip the active buffer - even if update above failed */
	ichan->active_buffer = !ichan->active_buffer;
	if (done)
		ichan->completed = desc->txd.cookie;

	callback = desc->txd.callback;
	callback_param = desc->txd.callback_param;

	spin_unlock(&ichan->lock);

	if (done && (desc->txd.flags & DMA_PREP_INTERRUPT) && callback)
		callback(callback_param);

	return IRQ_HANDLED;
}

static void ipu_gc_tasklet(unsigned long arg)
{
	struct ipu *ipu = (struct ipu *)arg;
	int i;

	for (i = 0; i < IPU_CHANNELS_NUM; i++) {
		struct idmac_channel *ichan = ipu->channel + i;
		struct idmac_tx_desc *desc;
		unsigned long flags;
		struct scatterlist *sg;
		int j, k;

		for (j = 0; j < ichan->n_tx_desc; j++) {
			desc = ichan->desc + j;
			spin_lock_irqsave(&ichan->lock, flags);
			if (async_tx_test_ack(&desc->txd)) {
				list_move(&desc->list, &ichan->free_list);
				for_each_sg(desc->sg, sg, desc->sg_len, k) {
					if (ichan->sg[0] == sg)
						ichan->sg[0] = NULL;
					else if (ichan->sg[1] == sg)
						ichan->sg[1] = NULL;
				}
				async_tx_clear_ack(&desc->txd);
			}
			spin_unlock_irqrestore(&ichan->lock, flags);
		}
	}
}

/* Allocate and initialise a transfer descriptor. */
static struct dma_async_tx_descriptor *idmac_prep_slave_sg(struct dma_chan *chan,
		struct scatterlist *sgl, unsigned int sg_len,
		enum dma_data_direction direction, unsigned long tx_flags)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	struct idmac_tx_desc *desc = NULL;
	struct dma_async_tx_descriptor *txd = NULL;
	unsigned long flags;

	/* We only can handle these three channels so far */
	if (chan->chan_id != IDMAC_SDC_0 && chan->chan_id != IDMAC_SDC_1 &&
	    chan->chan_id != IDMAC_IC_7)
		return NULL;

	if (direction != DMA_FROM_DEVICE && direction != DMA_TO_DEVICE) {
		dev_err(chan->device->dev, "Invalid DMA direction %d!\n", direction);
		return NULL;
	}

	mutex_lock(&ichan->chan_mutex);

	spin_lock_irqsave(&ichan->lock, flags);
	if (!list_empty(&ichan->free_list)) {
		desc = list_entry(ichan->free_list.next,
				  struct idmac_tx_desc, list);

		list_del_init(&desc->list);

		desc->sg_len	= sg_len;
		desc->sg	= sgl;
		txd		= &desc->txd;
		txd->flags	= tx_flags;
	}
	spin_unlock_irqrestore(&ichan->lock, flags);

	mutex_unlock(&ichan->chan_mutex);

	tasklet_schedule(&to_ipu(to_idmac(chan->device))->tasklet);

	return txd;
}

/* Re-select the current buffer and re-activate the channel */
static void idmac_issue_pending(struct dma_chan *chan)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	struct idmac *idmac = to_idmac(chan->device);
	struct ipu *ipu = to_ipu(idmac);
	unsigned long flags;

	/* This is not always needed, but doesn't hurt either */
	spin_lock_irqsave(&ipu->lock, flags);
	ipu_select_buffer(chan->chan_id, ichan->active_buffer);
	spin_unlock_irqrestore(&ipu->lock, flags);

	/*
	 * Might need to perform some parts of initialisation from
	 * ipu_enable_channel(), but not all, we do not want to reset to buffer
	 * 0, don't need to set priority again either, but re-enabling the task
	 * and the channel might be a good idea.
	 */
}

static int __idmac_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			   unsigned long arg)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	struct idmac *idmac = to_idmac(chan->device);
	unsigned long flags;
	int i;

	/* Only supports DMA_TERMINATE_ALL */
	if (cmd != DMA_TERMINATE_ALL)
		return -ENXIO;

	ipu_disable_channel(idmac, ichan,
			    ichan->status >= IPU_CHANNEL_ENABLED);

	tasklet_disable(&to_ipu(idmac)->tasklet);

	/* ichan->queue is modified in ISR, have to spinlock */
	spin_lock_irqsave(&ichan->lock, flags);
	list_splice_init(&ichan->queue, &ichan->free_list);

	if (ichan->desc)
		for (i = 0; i < ichan->n_tx_desc; i++) {
			struct idmac_tx_desc *desc = ichan->desc + i;
			if (list_empty(&desc->list))
				/* Descriptor was prepared, but not submitted */
				list_add(&desc->list, &ichan->free_list);

			async_tx_clear_ack(&desc->txd);
		}

	ichan->sg[0] = NULL;
	ichan->sg[1] = NULL;
	spin_unlock_irqrestore(&ichan->lock, flags);

	tasklet_enable(&to_ipu(idmac)->tasklet);

	ichan->status = IPU_CHANNEL_INITIALIZED;

	return 0;
}

static int idmac_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			 unsigned long arg)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	int ret;

	mutex_lock(&ichan->chan_mutex);

	ret = __idmac_control(chan, cmd, arg);

	mutex_unlock(&ichan->chan_mutex);

	return ret;
}

#ifdef DEBUG
static irqreturn_t ic_sof_irq(int irq, void *dev_id)
{
	struct idmac_channel *ichan = dev_id;
	printk(KERN_DEBUG "Got SOF IRQ %d on Channel %d\n",
	       irq, ichan->dma_chan.chan_id);
	disable_irq_nosync(irq);
	return IRQ_HANDLED;
}

static irqreturn_t ic_eof_irq(int irq, void *dev_id)
{
	struct idmac_channel *ichan = dev_id;
	printk(KERN_DEBUG "Got EOF IRQ %d on Channel %d\n",
	       irq, ichan->dma_chan.chan_id);
	disable_irq_nosync(irq);
	return IRQ_HANDLED;
}

static int ic_sof = -EINVAL, ic_eof = -EINVAL;
#endif

static int idmac_alloc_chan_resources(struct dma_chan *chan)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	struct idmac *idmac = to_idmac(chan->device);
	int ret;

	/* dmaengine.c now guarantees to only offer free channels */
	BUG_ON(chan->client_count > 1);
	WARN_ON(ichan->status != IPU_CHANNEL_FREE);

	chan->cookie		= 1;
	ichan->completed	= -ENXIO;

	ret = ipu_irq_map(chan->chan_id);
	if (ret < 0)
		goto eimap;

	ichan->eof_irq = ret;

	/*
	 * Important to first disable the channel, because maybe someone
	 * used it before us, e.g., the bootloader
	 */
	ipu_disable_channel(idmac, ichan, true);

	ret = ipu_init_channel(idmac, ichan);
	if (ret < 0)
		goto eichan;

	ret = request_irq(ichan->eof_irq, idmac_interrupt, 0,
			  ichan->eof_name, ichan);
	if (ret < 0)
		goto erirq;

#ifdef DEBUG
	if (chan->chan_id == IDMAC_IC_7) {
		ic_sof = ipu_irq_map(69);
		if (ic_sof > 0)
			request_irq(ic_sof, ic_sof_irq, 0, "IC SOF", ichan);
		ic_eof = ipu_irq_map(70);
		if (ic_eof > 0)
			request_irq(ic_eof, ic_eof_irq, 0, "IC EOF", ichan);
	}
#endif

	ichan->status = IPU_CHANNEL_INITIALIZED;

	dev_dbg(&chan->dev->device, "Found channel 0x%x, irq %d\n",
		chan->chan_id, ichan->eof_irq);

	return ret;

erirq:
	ipu_uninit_channel(idmac, ichan);
eichan:
	ipu_irq_unmap(chan->chan_id);
eimap:
	return ret;
}

static void idmac_free_chan_resources(struct dma_chan *chan)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);
	struct idmac *idmac = to_idmac(chan->device);

	mutex_lock(&ichan->chan_mutex);

	__idmac_control(chan, DMA_TERMINATE_ALL, 0);

	if (ichan->status > IPU_CHANNEL_FREE) {
#ifdef DEBUG
		if (chan->chan_id == IDMAC_IC_7) {
			if (ic_sof > 0) {
				free_irq(ic_sof, ichan);
				ipu_irq_unmap(69);
				ic_sof = -EINVAL;
			}
			if (ic_eof > 0) {
				free_irq(ic_eof, ichan);
				ipu_irq_unmap(70);
				ic_eof = -EINVAL;
			}
		}
#endif
		free_irq(ichan->eof_irq, ichan);
		ipu_irq_unmap(chan->chan_id);
	}

	ichan->status = IPU_CHANNEL_FREE;

	ipu_uninit_channel(idmac, ichan);

	mutex_unlock(&ichan->chan_mutex);

	tasklet_schedule(&to_ipu(idmac)->tasklet);
}

static enum dma_status idmac_tx_status(struct dma_chan *chan,
		       dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct idmac_channel *ichan = to_idmac_chan(chan);

	dma_set_tx_state(txstate, ichan->completed, chan->cookie, 0);
	if (cookie != chan->cookie)
		return DMA_ERROR;
	return DMA_SUCCESS;
}

static int __init ipu_idmac_init(struct ipu *ipu)
{
	struct idmac *idmac = &ipu->idmac;
	struct dma_device *dma = &idmac->dma;
	int i;

	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	/* Compulsory common fields */
	dma->dev				= ipu->dev;
	dma->device_alloc_chan_resources	= idmac_alloc_chan_resources;
	dma->device_free_chan_resources		= idmac_free_chan_resources;
	dma->device_tx_status			= idmac_tx_status;
	dma->device_issue_pending		= idmac_issue_pending;

	/* Compulsory for DMA_SLAVE fields */
	dma->device_prep_slave_sg		= idmac_prep_slave_sg;
	dma->device_control			= idmac_control;

	INIT_LIST_HEAD(&dma->channels);
	for (i = 0; i < IPU_CHANNELS_NUM; i++) {
		struct idmac_channel *ichan = ipu->channel + i;
		struct dma_chan *dma_chan = &ichan->dma_chan;

		spin_lock_init(&ichan->lock);
		mutex_init(&ichan->chan_mutex);

		ichan->status		= IPU_CHANNEL_FREE;
		ichan->sec_chan_en	= false;
		ichan->completed	= -ENXIO;
		snprintf(ichan->eof_name, sizeof(ichan->eof_name), "IDMAC EOF %d", i);

		dma_chan->device	= &idmac->dma;
		dma_chan->cookie	= 1;
		dma_chan->chan_id	= i;
		list_add_tail(&dma_chan->device_node, &dma->channels);
	}

	idmac_write_icreg(ipu, 0x00000070, IDMAC_CONF);

	return dma_async_device_register(&idmac->dma);
}

static void __exit ipu_idmac_exit(struct ipu *ipu)
{
	int i;
	struct idmac *idmac = &ipu->idmac;

	for (i = 0; i < IPU_CHANNELS_NUM; i++) {
		struct idmac_channel *ichan = ipu->channel + i;

		idmac_control(&ichan->dma_chan, DMA_TERMINATE_ALL, 0);
		idmac_prep_slave_sg(&ichan->dma_chan, NULL, 0, DMA_NONE, 0);
	}

	dma_async_device_unregister(&idmac->dma);
}

/*****************************************************************************
 * IPU common probe / remove
 */

static int __init ipu_probe(struct platform_device *pdev)
{
	struct ipu_platform_data *pdata = pdev->dev.platform_data;
	struct resource *mem_ipu, *mem_ic;
	int ret;

	spin_lock_init(&ipu_data.lock);

	mem_ipu	= platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mem_ic	= platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!pdata || !mem_ipu || !mem_ic)
		return -EINVAL;

	ipu_data.dev = &pdev->dev;

	platform_set_drvdata(pdev, &ipu_data);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_noirq;

	ipu_data.irq_fn = ret;
	ret = platform_get_irq(pdev, 1);
	if (ret < 0)
		goto err_noirq;

	ipu_data.irq_err = ret;
	ipu_data.irq_base = pdata->irq_base;

	dev_dbg(&pdev->dev, "fn irq %u, err irq %u, irq-base %u\n",
		ipu_data.irq_fn, ipu_data.irq_err, ipu_data.irq_base);

	/* Remap IPU common registers */
	ipu_data.reg_ipu = ioremap(mem_ipu->start, resource_size(mem_ipu));
	if (!ipu_data.reg_ipu) {
		ret = -ENOMEM;
		goto err_ioremap_ipu;
	}

	/* Remap Image Converter and Image DMA Controller registers */
	ipu_data.reg_ic = ioremap(mem_ic->start, resource_size(mem_ic));
	if (!ipu_data.reg_ic) {
		ret = -ENOMEM;
		goto err_ioremap_ic;
	}

	/* Get IPU clock */
	ipu_data.ipu_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(ipu_data.ipu_clk)) {
		ret = PTR_ERR(ipu_data.ipu_clk);
		goto err_clk_get;
	}

	/* Make sure IPU HSP clock is running */
	clk_enable(ipu_data.ipu_clk);

	/* Disable all interrupts */
	idmac_write_ipureg(&ipu_data, 0, IPU_INT_CTRL_1);
	idmac_write_ipureg(&ipu_data, 0, IPU_INT_CTRL_2);
	idmac_write_ipureg(&ipu_data, 0, IPU_INT_CTRL_3);
	idmac_write_ipureg(&ipu_data, 0, IPU_INT_CTRL_4);
	idmac_write_ipureg(&ipu_data, 0, IPU_INT_CTRL_5);

	dev_dbg(&pdev->dev, "%s @ 0x%08lx, fn irq %u, err irq %u\n", pdev->name,
		(unsigned long)mem_ipu->start, ipu_data.irq_fn, ipu_data.irq_err);

	ret = ipu_irq_attach_irq(&ipu_data, pdev);
	if (ret < 0)
		goto err_attach_irq;

	/* Initialize DMA engine */
	ret = ipu_idmac_init(&ipu_data);
	if (ret < 0)
		goto err_idmac_init;

	tasklet_init(&ipu_data.tasklet, ipu_gc_tasklet, (unsigned long)&ipu_data);

	ipu_data.dev = &pdev->dev;

	dev_dbg(ipu_data.dev, "IPU initialized\n");

	return 0;

err_idmac_init:
err_attach_irq:
	ipu_irq_detach_irq(&ipu_data, pdev);
	clk_disable(ipu_data.ipu_clk);
	clk_put(ipu_data.ipu_clk);
err_clk_get:
	iounmap(ipu_data.reg_ic);
err_ioremap_ic:
	iounmap(ipu_data.reg_ipu);
err_ioremap_ipu:
err_noirq:
	dev_err(&pdev->dev, "Failed to probe IPU: %d\n", ret);
	return ret;
}

static int __exit ipu_remove(struct platform_device *pdev)
{
	struct ipu *ipu = platform_get_drvdata(pdev);

	ipu_idmac_exit(ipu);
	ipu_irq_detach_irq(ipu, pdev);
	clk_disable(ipu->ipu_clk);
	clk_put(ipu->ipu_clk);
	iounmap(ipu->reg_ic);
	iounmap(ipu->reg_ipu);
	tasklet_kill(&ipu->tasklet);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

/*
 * We need two MEM resources - with IPU-common and Image Converter registers,
 * including PF_CONF and IDMAC_* registers, and two IRQs - function and error
 */
static struct platform_driver ipu_platform_driver = {
	.driver = {
		.name	= "ipu-core",
		.owner	= THIS_MODULE,
	},
	.remove		= __exit_p(ipu_remove),
};

static int __init ipu_init(void)
{
	return platform_driver_probe(&ipu_platform_driver, ipu_probe);
}
subsys_initcall(ipu_init);

MODULE_DESCRIPTION("IPU core driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Guennadi Liakhovetski <lg@denx.de>");
MODULE_ALIAS("platform:ipu-core");
