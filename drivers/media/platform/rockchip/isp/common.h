/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP_COMMON_H
#define _RKISP_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/media.h>
#include <linux/rk-video-format.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/soc/rockchip/rk_sdmmc.h>

#define RKISP_DEFAULT_WIDTH		800
#define RKISP_DEFAULT_HEIGHT		600

#define RKISP_PLANE_Y			0
#define RKISP_PLANE_CB			1
#define RKISP_PLANE_CR			2

#define RKISP_EMDDATA_FIFO_MAX		4
#define RKISP_DMATX_CHECK              0xA5A5A5A5

#define RKISP_MOTION_DECT_TS_SIZE	16

struct rkisp_device;

/* ISP_V10_1 for only support MP */
enum rkisp_isp_ver {
	ISP_V10 = 0x00,
	ISP_V10_1 = 0x01,
	ISP_V11 = 0x10,
	ISP_V12 = 0x20,
	ISP_V13 = 0x30,
	ISP_V20 = 0x40,
	ISP_V21 = 0x50,
	ISP_V30 = 0x60,
	ISP_V32 = 0x70,
};

enum rkisp_sd_type {
	RKISP_SD_SENSOR,
	RKISP_SD_PHY_CSI,
	RKISP_SD_VCM,
	RKISP_SD_FLASH,
	RKISP_SD_MAX,
};

/* One structure per video node */
struct rkisp_vdev_node {
	struct vb2_queue buf_queue;
	struct video_device vdev;
	struct media_pad pad;
};

enum rkisp_fmt_pix_type {
	FMT_YUV,
	FMT_RGB,
	FMT_BAYER,
	FMT_JPEG,
	FMT_FBCGAIN,
	FMT_EBD,
	FMT_SPD,
	FMT_FBC,
	FMT_MAX
};

enum rkisp_fmt_raw_pat_type {
	RAW_RGGB = 0,
	RAW_GRBG,
	RAW_GBRG,
	RAW_BGGR,
};

struct rkisp_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	void *vaddr[VIDEO_MAX_PLANES];
	u32 buff_addr[VIDEO_MAX_PLANES];
	int dev_id;
};

struct rkisp_dummy_buffer {
	struct list_head queue;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	struct page **pages;
	void *mem_priv;
	void *vaddr;
	u32 size;
	int dma_fd;
	bool is_need_vaddr;
	bool is_need_dbuf;
	bool is_need_dmafd;
};

extern int rkisp_debug;
extern bool rkisp_monitor;
extern bool rkisp_irq_dbg;
extern u64 rkisp_debug_reg;
extern struct platform_driver rkisp_plat_drv;

static inline
struct rkisp_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp_vdev_node, vdev);
}

static inline struct rkisp_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp_vdev_node, buf_queue);
}

static inline struct rkisp_buffer *to_rkisp_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkisp_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

void rkisp_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct);
u32 rkisp_read(struct rkisp_device *dev, u32 reg, bool is_direct);
void rkisp_set_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val, bool is_direct);
void rkisp_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask, bool is_direct);

void rkisp_write_reg_cache(struct rkisp_device *dev, u32 reg, u32 val);
u32 rkisp_read_reg_cache(struct rkisp_device *dev, u32 reg);
void rkisp_set_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val);
void rkisp_clear_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask);

/* for dual isp, config for next isp reg */
void rkisp_next_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct);
u32 rkisp_next_read(struct rkisp_device *dev, u32 reg, bool is_direct);
void rkisp_next_set_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val, bool is_direct);
void rkisp_next_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask, bool is_direct);

void rkisp_next_write_reg_cache(struct rkisp_device *dev, u32 reg, u32 val);
u32 rkisp_next_read_reg_cache(struct rkisp_device *dev, u32 reg);
void rkisp_next_set_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val);
void rkisp_next_clear_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask);

static inline void
rkisp_unite_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct, bool is_unite)
{
	rkisp_write(dev, reg, val, is_direct);
	if (is_unite)
		rkisp_next_write(dev, reg, val, is_direct);
}

static inline void
rkisp_unite_set_bits(struct rkisp_device *dev, u32 reg, u32 mask,
		     u32 val, bool is_direct, bool is_unite)
{
	rkisp_set_bits(dev, reg, mask, val, is_direct);
	if (is_unite)
		rkisp_next_set_bits(dev, reg, mask, val, is_direct);
}

static inline void
rkisp_unite_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask,
		       bool is_direct, bool is_unite)
{
	rkisp_clear_bits(dev, reg, mask, is_direct);
	if (is_unite)
		rkisp_next_clear_bits(dev, reg, mask, is_direct);
}

void rkisp_update_regs(struct rkisp_device *dev, u32 start, u32 end);

int rkisp_alloc_buffer(struct rkisp_device *dev, struct rkisp_dummy_buffer *buf);
void rkisp_free_buffer(struct rkisp_device *dev, struct rkisp_dummy_buffer *buf);
void rkisp_prepare_buffer(struct rkisp_device *dev, struct rkisp_dummy_buffer *buf);
void rkisp_finish_buffer(struct rkisp_device *dev, struct rkisp_dummy_buffer *buf);

int rkisp_attach_hw(struct rkisp_device *isp);
int rkisp_alloc_common_dummy_buf(struct rkisp_device *dev);
void rkisp_free_common_dummy_buf(struct rkisp_device *dev);

void rkisp_set_clk_rate(struct clk *clk, unsigned long rate);
#endif /* _RKISP_COMMON_H */
