/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DRM_ATMEL_HLCDC_LAYER_H
#define DRM_ATMEL_HLCDC_LAYER_H

#include <linux/mfd/atmel-hlcdc.h>

#include <drm/drm_crtc.h>
#include <drm/drm_flip_work.h>
#include <drm/drmP.h>

#define ATMEL_HLCDC_LAYER_CHER			0x0
#define ATMEL_HLCDC_LAYER_CHDR			0x4
#define ATMEL_HLCDC_LAYER_CHSR			0x8
#define ATMEL_HLCDC_LAYER_DMA_CHAN		BIT(0)
#define ATMEL_HLCDC_LAYER_UPDATE		BIT(1)
#define ATMEL_HLCDC_LAYER_A2Q			BIT(2)
#define ATMEL_HLCDC_LAYER_RST			BIT(8)

#define ATMEL_HLCDC_LAYER_IER			0xc
#define ATMEL_HLCDC_LAYER_IDR			0x10
#define ATMEL_HLCDC_LAYER_IMR			0x14
#define ATMEL_HLCDC_LAYER_ISR			0x18
#define ATMEL_HLCDC_LAYER_DFETCH		BIT(0)
#define ATMEL_HLCDC_LAYER_LFETCH		BIT(1)
#define ATMEL_HLCDC_LAYER_DMA_IRQ		BIT(2)
#define ATMEL_HLCDC_LAYER_DSCR_IRQ		BIT(3)
#define ATMEL_HLCDC_LAYER_ADD_IRQ		BIT(4)
#define ATMEL_HLCDC_LAYER_DONE_IRQ		BIT(5)
#define ATMEL_HLCDC_LAYER_OVR_IRQ		BIT(6)

#define ATMEL_HLCDC_LAYER_PLANE_HEAD(n)		(((n) * 0x10) + 0x1c)
#define ATMEL_HLCDC_LAYER_PLANE_ADDR(n)		(((n) * 0x10) + 0x20)
#define ATMEL_HLCDC_LAYER_PLANE_CTRL(n)		(((n) * 0x10) + 0x24)
#define ATMEL_HLCDC_LAYER_PLANE_NEXT(n)		(((n) * 0x10) + 0x28)
#define ATMEL_HLCDC_LAYER_CFG(p, c)		(((c) * 4) + ((p)->max_planes * 0x10) + 0x1c)

#define ATMEL_HLCDC_LAYER_DMA_CFG_ID		0
#define ATMEL_HLCDC_LAYER_DMA_CFG(p)		ATMEL_HLCDC_LAYER_CFG(p, ATMEL_HLCDC_LAYER_DMA_CFG_ID)
#define ATMEL_HLCDC_LAYER_DMA_SIF		BIT(0)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_MASK		GENMASK(5, 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_SINGLE	(0 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR4	(1 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR8	(2 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR16	(3 << 4)
#define ATMEL_HLCDC_LAYER_DMA_DLBO		BIT(8)
#define ATMEL_HLCDC_LAYER_DMA_ROTDIS		BIT(12)
#define ATMEL_HLCDC_LAYER_DMA_LOCKDIS		BIT(13)

#define ATMEL_HLCDC_LAYER_FORMAT_CFG_ID		1
#define ATMEL_HLCDC_LAYER_FORMAT_CFG(p)		ATMEL_HLCDC_LAYER_CFG(p, ATMEL_HLCDC_LAYER_FORMAT_CFG_ID)
#define ATMEL_HLCDC_LAYER_RGB			(0 << 0)
#define ATMEL_HLCDC_LAYER_CLUT			(1 << 0)
#define ATMEL_HLCDC_LAYER_YUV			(2 << 0)
#define ATMEL_HLCDC_RGB_MODE(m)			(((m) & 0xf) << 4)
#define ATMEL_HLCDC_CLUT_MODE(m)		(((m) & 0x3) << 8)
#define ATMEL_HLCDC_YUV_MODE(m)			(((m) & 0xf) << 12)
#define ATMEL_HLCDC_YUV422ROT			BIT(16)
#define ATMEL_HLCDC_YUV422SWP			BIT(17)
#define ATMEL_HLCDC_DSCALEOPT			BIT(20)

#define ATMEL_HLCDC_XRGB4444_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(0))
#define ATMEL_HLCDC_ARGB4444_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(1))
#define ATMEL_HLCDC_RGBA4444_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(2))
#define ATMEL_HLCDC_RGB565_MODE			(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(3))
#define ATMEL_HLCDC_ARGB1555_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(4))
#define ATMEL_HLCDC_XRGB8888_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(9))
#define ATMEL_HLCDC_RGB888_MODE			(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(10))
#define ATMEL_HLCDC_ARGB8888_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(12))
#define ATMEL_HLCDC_RGBA8888_MODE		(ATMEL_HLCDC_LAYER_RGB | ATMEL_HLCDC_RGB_MODE(13))

#define ATMEL_HLCDC_AYUV_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(0))
#define ATMEL_HLCDC_YUYV_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(1))
#define ATMEL_HLCDC_UYVY_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(2))
#define ATMEL_HLCDC_YVYU_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(3))
#define ATMEL_HLCDC_VYUY_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(4))
#define ATMEL_HLCDC_NV61_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(5))
#define ATMEL_HLCDC_YUV422_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(6))
#define ATMEL_HLCDC_NV21_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(7))
#define ATMEL_HLCDC_YUV420_MODE			(ATMEL_HLCDC_LAYER_YUV | ATMEL_HLCDC_YUV_MODE(8))

#define ATMEL_HLCDC_LAYER_POS_CFG(p)		ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.pos)
#define ATMEL_HLCDC_LAYER_SIZE_CFG(p)		ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.size)
#define ATMEL_HLCDC_LAYER_MEMSIZE_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.memsize)
#define ATMEL_HLCDC_LAYER_XSTRIDE_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.xstride)
#define ATMEL_HLCDC_LAYER_PSTRIDE_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.pstride)
#define ATMEL_HLCDC_LAYER_DFLTCOLOR_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.default_color)
#define ATMEL_HLCDC_LAYER_CRKEY_CFG(p)		ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.chroma_key)
#define ATMEL_HLCDC_LAYER_CRKEY_MASK_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.chroma_key_mask)

#define ATMEL_HLCDC_LAYER_GENERAL_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.general_config)
#define ATMEL_HLCDC_LAYER_CRKEY			BIT(0)
#define ATMEL_HLCDC_LAYER_INV			BIT(1)
#define ATMEL_HLCDC_LAYER_ITER2BL		BIT(2)
#define ATMEL_HLCDC_LAYER_ITER			BIT(3)
#define ATMEL_HLCDC_LAYER_REVALPHA		BIT(4)
#define ATMEL_HLCDC_LAYER_GAEN			BIT(5)
#define ATMEL_HLCDC_LAYER_LAEN			BIT(6)
#define ATMEL_HLCDC_LAYER_OVR			BIT(7)
#define ATMEL_HLCDC_LAYER_DMA			BIT(8)
#define ATMEL_HLCDC_LAYER_REP			BIT(9)
#define ATMEL_HLCDC_LAYER_DSTKEY		BIT(10)
#define ATMEL_HLCDC_LAYER_DISCEN		BIT(11)
#define ATMEL_HLCDC_LAYER_GA_SHIFT		16
#define ATMEL_HLCDC_LAYER_GA_MASK		GENMASK(23, ATMEL_HLCDC_LAYER_GA_SHIFT)
#define ATMEL_HLCDC_LAYER_GA(x)			((x) << ATMEL_HLCDC_LAYER_GA_SHIFT)

#define ATMEL_HLCDC_LAYER_CSC_CFG(p, o)		ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.csc + o)

#define ATMEL_HLCDC_LAYER_DISC_POS_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.disc_pos)

#define ATMEL_HLCDC_LAYER_DISC_SIZE_CFG(p)	ATMEL_HLCDC_LAYER_CFG(p, (p)->desc->layout.disc_size)

#define ATMEL_HLCDC_MAX_PLANES			3

#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_RESERVED	BIT(0)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_LOADED	BIT(1)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_DONE	BIT(2)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_OVERRUN	BIT(3)

/**
 * Atmel HLCDC Layer registers layout structure
 *
 * Each HLCDC layer has its own register organization and a given register
 * can be placed differently on 2 different layers depending on its
 * capabilities.
 * This structure stores common registers layout for a given layer and is
 * used by HLCDC layer code to choose the appropriate register to write to
 * or to read from.
 *
 * For all fields, a value of zero means "unsupported".
 *
 * See Atmel's datasheet for a detailled description of these registers.
 *
 * @xstride: xstride registers
 * @pstride: pstride registers
 * @pos: position register
 * @size: displayed size register
 * @memsize: memory size register
 * @default_color: default color register
 * @chroma_key: chroma key register
 * @chroma_key_mask: chroma key mask register
 * @general_config: general layer config register
 * @disc_pos: discard area position register
 * @disc_size: discard area size register
 * @csc: color space conversion register
 */
struct atmel_hlcdc_layer_cfg_layout {
	int xstride[ATMEL_HLCDC_MAX_PLANES];
	int pstride[ATMEL_HLCDC_MAX_PLANES];
	int pos;
	int size;
	int memsize;
	int default_color;
	int chroma_key;
	int chroma_key_mask;
	int general_config;
	int disc_pos;
	int disc_size;
	int csc;
};

/**
 * Atmel HLCDC framebuffer flip structure
 *
 * This structure is allocated when someone asked for a layer update (most
 * likely a DRM plane update, either primary, overlay or cursor plane) and
 * released when the layer do not need to reference the framebuffer object
 * anymore (i.e. the layer was disabled or updated).
 *
 * @dscrs: DMA descriptors
 * @fb: the referenced framebuffer object
 * @ngems: number of GEM objects referenced by the fb element
 * @status: fb flip operation status
 */
struct atmel_hlcdc_layer_fb_flip {
	struct atmel_hlcdc_dma_channel_dscr *dscrs[ATMEL_HLCDC_MAX_PLANES];
	struct drm_flip_task *task;
	struct drm_framebuffer *fb;
	int ngems;
	u32 status;
};

/**
 * Atmel HLCDC DMA descriptor structure
 *
 * This structure is used by the HLCDC DMA engine to schedule a DMA transfer.
 *
 * The structure fields must remain in this specific order, because they're
 * used by the HLCDC DMA engine, which expect them in this order.
 * HLCDC DMA descriptors must be aligned on 64 bits.
 *
 * @addr: buffer DMA address
 * @ctrl: DMA transfer options
 * @next: next DMA descriptor to fetch
 * @gem_flip: the attached gem_flip operation
 */
struct atmel_hlcdc_dma_channel_dscr {
	dma_addr_t addr;
	u32 ctrl;
	dma_addr_t next;
	u32 status;
} __aligned(sizeof(u64));

/**
 * Atmel HLCDC layer types
 */
enum atmel_hlcdc_layer_type {
	ATMEL_HLCDC_BASE_LAYER,
	ATMEL_HLCDC_OVERLAY_LAYER,
	ATMEL_HLCDC_CURSOR_LAYER,
	ATMEL_HLCDC_PP_LAYER,
};

/**
 * Atmel HLCDC Supported formats structure
 *
 * This structure list all the formats supported by a given layer.
 *
 * @nformats: number of supported formats
 * @formats: supported formats
 */
struct atmel_hlcdc_formats {
	int nformats;
	uint32_t *formats;
};

/**
 * Atmel HLCDC Layer description structure
 *
 * This structure describe the capabilities provided by a given layer.
 *
 * @name: layer name
 * @type: layer type
 * @id: layer id
 * @regs_offset: offset of the layer registers from the HLCDC registers base
 * @nconfigs: number of config registers provided by this layer
 * @formats: supported formats
 * @layout: config registers layout
 * @max_width: maximum width supported by this layer (0 means unlimited)
 * @max_height: maximum height supported by this layer (0 means unlimited)
 */
struct atmel_hlcdc_layer_desc {
	const char *name;
	enum atmel_hlcdc_layer_type type;
	int id;
	int regs_offset;
	int nconfigs;
	struct atmel_hlcdc_formats *formats;
	struct atmel_hlcdc_layer_cfg_layout layout;
	int max_width;
	int max_height;
};

/**
 * Atmel HLCDC Layer Update Slot structure
 *
 * This structure stores layer update requests to be applied on next frame.
 * This is the base structure behind the atomic layer update infrastructure.
 *
 * Atomic layer update provides a way to update all layer's parameters
 * simultaneously. This is needed to avoid incompatible sequential updates
 * like this one:
 * 1) update layer format from RGB888 (1 plane/buffer) to YUV422
 *    (2 planes/buffers)
 * 2) the format update is applied but the DMA channel for the second
 *    plane/buffer is not enabled
 * 3) enable the DMA channel for the second plane
 *
 * @fb_flip: fb_flip object
 * @updated_configs: bitmask used to record modified configs
 * @configs: new config values
 */
struct atmel_hlcdc_layer_update_slot {
	struct atmel_hlcdc_layer_fb_flip *fb_flip;
	unsigned long *updated_configs;
	u32 *configs;
};

/**
 * Atmel HLCDC Layer Update structure
 *
 * This structure provides a way to queue layer update requests.
 *
 * At a given time there is at most:
 *  - one pending update request, which means the update request has been
 *    committed (or validated) and is waiting for the DMA channel(s) to be
 *    available
 *  - one request being prepared, which means someone started a layer update
 *    but has not committed it yet. There cannot be more than one started
 *    request, because the update lock is taken when starting a layer update
 *    and release when committing or rolling back the request.
 *
 * @slots: update slots. One is used for pending request and the other one
 *	   for started update request
 * @pending: the pending slot index or -1 if no request is pending
 * @next: the started update slot index or -1 no update has been started
 */
struct atmel_hlcdc_layer_update {
	struct atmel_hlcdc_layer_update_slot slots[2];
	int pending;
	int next;
};

enum atmel_hlcdc_layer_dma_channel_status {
	ATMEL_HLCDC_LAYER_DISABLED,
	ATMEL_HLCDC_LAYER_ENABLED,
	ATMEL_HLCDC_LAYER_DISABLING,
};

/**
 * Atmel HLCDC Layer DMA channel structure
 *
 * This structure stores information on the DMA channel associated to a
 * given layer.
 *
 * @status: DMA channel status
 * @cur: current framebuffer
 * @queue: next framebuffer
 * @dscrs: allocated DMA descriptors
 */
struct atmel_hlcdc_layer_dma_channel {
	enum atmel_hlcdc_layer_dma_channel_status status;
	struct atmel_hlcdc_layer_fb_flip *cur;
	struct atmel_hlcdc_layer_fb_flip *queue;
	struct atmel_hlcdc_dma_channel_dscr *dscrs;
};

/**
 * Atmel HLCDC Layer structure
 *
 * This structure stores information on the layer instance.
 *
 * @desc: layer description
 * @max_planes: maximum planes/buffers that can be associated with this layer.
 *	       This depends on the supported formats.
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @dma: dma channel
 * @gc: fb flip garbage collector
 * @update: update handler
 * @lock: layer lock
 */
struct atmel_hlcdc_layer {
	const struct atmel_hlcdc_layer_desc *desc;
	int max_planes;
	struct atmel_hlcdc *hlcdc;
	struct workqueue_struct *wq;
	struct drm_flip_work gc;
	struct atmel_hlcdc_layer_dma_channel dma;
	struct atmel_hlcdc_layer_update update;
	spinlock_t lock;
};

void atmel_hlcdc_layer_irq(struct atmel_hlcdc_layer *layer);

int atmel_hlcdc_layer_init(struct drm_device *dev,
			   struct atmel_hlcdc_layer *layer,
			   const struct atmel_hlcdc_layer_desc *desc);

void atmel_hlcdc_layer_cleanup(struct drm_device *dev,
			       struct atmel_hlcdc_layer *layer);

void atmel_hlcdc_layer_disable(struct atmel_hlcdc_layer *layer);

int atmel_hlcdc_layer_update_start(struct atmel_hlcdc_layer *layer);

void atmel_hlcdc_layer_update_cfg(struct atmel_hlcdc_layer *layer, int cfg,
				  u32 mask, u32 val);

void atmel_hlcdc_layer_update_set_fb(struct atmel_hlcdc_layer *layer,
				     struct drm_framebuffer *fb,
				     unsigned int *offsets);

void atmel_hlcdc_layer_update_set_finished(struct atmel_hlcdc_layer *layer,
					   void (*finished)(void *data),
					   void *finished_data);

void atmel_hlcdc_layer_update_rollback(struct atmel_hlcdc_layer *layer);

void atmel_hlcdc_layer_update_commit(struct atmel_hlcdc_layer *layer);

#endif /* DRM_ATMEL_HLCDC_LAYER_H */
