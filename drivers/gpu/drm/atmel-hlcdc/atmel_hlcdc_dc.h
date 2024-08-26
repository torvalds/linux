/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */

#ifndef DRM_ATMEL_HLCDC_H
#define DRM_ATMEL_HLCDC_H

#include <linux/regmap.h>

#include <drm/drm_plane.h>

/* LCD controller common registers */
#define ATMEL_HLCDC_LAYER_CHER			0x0
#define ATMEL_HLCDC_LAYER_CHDR			0x4
#define ATMEL_HLCDC_LAYER_CHSR			0x8
#define ATMEL_HLCDC_LAYER_EN			BIT(0)
#define ATMEL_HLCDC_LAYER_UPDATE		BIT(1)
#define ATMEL_HLCDC_LAYER_A2Q			BIT(2)
#define ATMEL_HLCDC_LAYER_RST			BIT(8)

#define ATMEL_HLCDC_LAYER_IER			0xc
#define ATMEL_HLCDC_LAYER_IDR			0x10
#define ATMEL_HLCDC_LAYER_IMR			0x14
#define ATMEL_HLCDC_LAYER_ISR			0x18
#define ATMEL_HLCDC_LAYER_DFETCH		BIT(0)
#define ATMEL_HLCDC_LAYER_LFETCH		BIT(1)
#define ATMEL_HLCDC_LAYER_DMA_IRQ(p)		BIT(2 + (8 * (p)))
#define ATMEL_HLCDC_LAYER_DSCR_IRQ(p)		BIT(3 + (8 * (p)))
#define ATMEL_HLCDC_LAYER_ADD_IRQ(p)		BIT(4 + (8 * (p)))
#define ATMEL_HLCDC_LAYER_DONE_IRQ(p)		BIT(5 + (8 * (p)))
#define ATMEL_HLCDC_LAYER_OVR_IRQ(p)		BIT(6 + (8 * (p)))

#define ATMEL_HLCDC_LAYER_PLANE_HEAD(p)		(((p) * 0x10) + 0x1c)
#define ATMEL_HLCDC_LAYER_PLANE_ADDR(p)		(((p) * 0x10) + 0x20)
#define ATMEL_HLCDC_LAYER_PLANE_CTRL(p)		(((p) * 0x10) + 0x24)
#define ATMEL_HLCDC_LAYER_PLANE_NEXT(p)		(((p) * 0x10) + 0x28)

#define ATMEL_HLCDC_LAYER_DMA_CFG		0
#define ATMEL_HLCDC_LAYER_DMA_SIF		BIT(0)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_MASK		GENMASK(5, 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_SINGLE	(0 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR4	(1 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR8	(2 << 4)
#define ATMEL_HLCDC_LAYER_DMA_BLEN_INCR16	(3 << 4)
#define ATMEL_HLCDC_LAYER_DMA_DLBO		BIT(8)
#define ATMEL_HLCDC_LAYER_DMA_ROTDIS		BIT(12)
#define ATMEL_HLCDC_LAYER_DMA_LOCKDIS		BIT(13)

#define ATMEL_HLCDC_LAYER_FORMAT_CFG		1
#define ATMEL_HLCDC_LAYER_RGB			(0 << 0)
#define ATMEL_HLCDC_LAYER_CLUT			(1 << 0)
#define ATMEL_HLCDC_LAYER_YUV			(2 << 0)
#define ATMEL_HLCDC_RGB_MODE(m)			\
	(ATMEL_HLCDC_LAYER_RGB | (((m) & 0xf) << 4))
#define ATMEL_HLCDC_CLUT_MODE(m)		\
	(ATMEL_HLCDC_LAYER_CLUT | (((m) & 0x3) << 8))
#define ATMEL_HLCDC_YUV_MODE(m)			\
	(ATMEL_HLCDC_LAYER_YUV | (((m) & 0xf) << 12))
#define ATMEL_HLCDC_YUV422ROT			BIT(16)
#define ATMEL_HLCDC_YUV422SWP			BIT(17)
#define ATMEL_HLCDC_DSCALEOPT			BIT(20)

#define ATMEL_HLCDC_C1_MODE			ATMEL_HLCDC_CLUT_MODE(0)
#define ATMEL_HLCDC_C2_MODE			ATMEL_HLCDC_CLUT_MODE(1)
#define ATMEL_HLCDC_C4_MODE			ATMEL_HLCDC_CLUT_MODE(2)
#define ATMEL_HLCDC_C8_MODE			ATMEL_HLCDC_CLUT_MODE(3)

#define ATMEL_HLCDC_XRGB4444_MODE		ATMEL_HLCDC_RGB_MODE(0)
#define ATMEL_HLCDC_ARGB4444_MODE		ATMEL_HLCDC_RGB_MODE(1)
#define ATMEL_HLCDC_RGBA4444_MODE		ATMEL_HLCDC_RGB_MODE(2)
#define ATMEL_HLCDC_RGB565_MODE			ATMEL_HLCDC_RGB_MODE(3)
#define ATMEL_HLCDC_ARGB1555_MODE		ATMEL_HLCDC_RGB_MODE(4)
#define ATMEL_HLCDC_XRGB8888_MODE		ATMEL_HLCDC_RGB_MODE(9)
#define ATMEL_HLCDC_RGB888_MODE			ATMEL_HLCDC_RGB_MODE(10)
#define ATMEL_HLCDC_ARGB8888_MODE		ATMEL_HLCDC_RGB_MODE(12)
#define ATMEL_HLCDC_RGBA8888_MODE		ATMEL_HLCDC_RGB_MODE(13)

#define ATMEL_HLCDC_AYUV_MODE			ATMEL_HLCDC_YUV_MODE(0)
#define ATMEL_HLCDC_YUYV_MODE			ATMEL_HLCDC_YUV_MODE(1)
#define ATMEL_HLCDC_UYVY_MODE			ATMEL_HLCDC_YUV_MODE(2)
#define ATMEL_HLCDC_YVYU_MODE			ATMEL_HLCDC_YUV_MODE(3)
#define ATMEL_HLCDC_VYUY_MODE			ATMEL_HLCDC_YUV_MODE(4)
#define ATMEL_HLCDC_NV61_MODE			ATMEL_HLCDC_YUV_MODE(5)
#define ATMEL_HLCDC_YUV422_MODE			ATMEL_HLCDC_YUV_MODE(6)
#define ATMEL_HLCDC_NV21_MODE			ATMEL_HLCDC_YUV_MODE(7)
#define ATMEL_HLCDC_YUV420_MODE			ATMEL_HLCDC_YUV_MODE(8)

#define ATMEL_HLCDC_LAYER_POS(x, y)		((x) | ((y) << 16))
#define ATMEL_HLCDC_LAYER_SIZE(w, h)		(((w) - 1) | (((h) - 1) << 16))

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
#define ATMEL_HLCDC_LAYER_GA_MASK		\
	GENMASK(23, ATMEL_HLCDC_LAYER_GA_SHIFT)
#define ATMEL_HLCDC_LAYER_GA(x)			\
	((x) << ATMEL_HLCDC_LAYER_GA_SHIFT)

#define ATMEL_HLCDC_LAYER_DISC_POS(x, y)	((x) | ((y) << 16))
#define ATMEL_HLCDC_LAYER_DISC_SIZE(w, h)	(((w) - 1) | (((h) - 1) << 16))

#define ATMEL_HLCDC_LAYER_SCALER_FACTORS(x, y)	((x) | ((y) << 16))
#define ATMEL_HLCDC_LAYER_SCALER_ENABLE		BIT(31)

#define ATMEL_HLCDC_LAYER_MAX_PLANES		3

#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_RESERVED	BIT(0)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_LOADED	BIT(1)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_DONE	BIT(2)
#define ATMEL_HLCDC_DMA_CHANNEL_DSCR_OVERRUN	BIT(3)

#define ATMEL_HLCDC_CLUT_SIZE			256

#define ATMEL_HLCDC_MAX_LAYERS			6

/* XLCDC controller specific registers */
#define ATMEL_XLCDC_LAYER_ENR			0x10
#define ATMEL_XLCDC_LAYER_EN			BIT(0)

#define ATMEL_XLCDC_LAYER_IER			0x0
#define ATMEL_XLCDC_LAYER_IDR			0x4
#define ATMEL_XLCDC_LAYER_ISR			0xc
#define ATMEL_XLCDC_LAYER_OVR_IRQ(p)		BIT(2 + (8 * (p)))

#define ATMEL_XLCDC_LAYER_PLANE_ADDR(p)		(((p) * 0x4) + 0x18)

#define ATMEL_XLCDC_LAYER_DMA_CFG		0

#define ATMEL_XLCDC_LAYER_DMA			BIT(0)
#define ATMEL_XLCDC_LAYER_REP			BIT(1)
#define ATMEL_XLCDC_LAYER_DISCEN		BIT(4)

#define ATMEL_XLCDC_LAYER_SFACTC_A0_MULT_AS	(4 << 6)
#define ATMEL_XLCDC_LAYER_SFACTA_ONE		BIT(9)
#define ATMEL_XLCDC_LAYER_DFACTC_M_A0_MULT_AS	(6 << 11)
#define ATMEL_XLCDC_LAYER_DFACTA_ONE		BIT(14)

#define ATMEL_XLCDC_LAYER_A0_SHIFT		16
#define ATMEL_XLCDC_LAYER_A0(x)			\
	((x) << ATMEL_XLCDC_LAYER_A0_SHIFT)

#define ATMEL_XLCDC_LAYER_VSCALER_LUMA_ENABLE		BIT(0)
#define ATMEL_XLCDC_LAYER_VSCALER_CHROMA_ENABLE		BIT(1)
#define ATMEL_XLCDC_LAYER_HSCALER_LUMA_ENABLE		BIT(4)
#define ATMEL_XLCDC_LAYER_HSCALER_CHROMA_ENABLE		BIT(5)

#define ATMEL_XLCDC_LAYER_VXSYCFG_ONE		BIT(0)
#define ATMEL_XLCDC_LAYER_VXSYTAP2_ENABLE	BIT(4)
#define ATMEL_XLCDC_LAYER_VXSCCFG_ONE		BIT(16)
#define ATMEL_XLCDC_LAYER_VXSCTAP2_ENABLE	BIT(20)

#define ATMEL_XLCDC_LAYER_HXSYCFG_ONE		BIT(0)
#define ATMEL_XLCDC_LAYER_HXSYTAP2_ENABLE	BIT(4)
#define ATMEL_XLCDC_LAYER_HXSCCFG_ONE		BIT(16)
#define ATMEL_XLCDC_LAYER_HXSCTAP2_ENABLE	BIT(20)

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
 * @sacler_config: scaler factors register
 * @phicoeffs: X/Y PHI coefficient registers
 * @disc_pos: discard area position register
 * @disc_size: discard area size register
 * @csc: color space conversion register
 * @vxs_config: vertical scalar filter taps control register
 * @hxs_config: horizontal scalar filter taps control register
 */
struct atmel_hlcdc_layer_cfg_layout {
	int xstride[ATMEL_HLCDC_LAYER_MAX_PLANES];
	int pstride[ATMEL_HLCDC_LAYER_MAX_PLANES];
	int pos;
	int size;
	int memsize;
	int default_color;
	int chroma_key;
	int chroma_key_mask;
	int general_config;
	int scaler_config;
	struct {
		int x;
		int y;
	} phicoeffs;
	int disc_pos;
	int disc_size;
	int csc;
	int vxs_config;
	int hxs_config;
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
 * @self: descriptor DMA address
 */
struct atmel_hlcdc_dma_channel_dscr {
	dma_addr_t addr;
	u32 ctrl;
	dma_addr_t next;
	dma_addr_t self;
} __aligned(sizeof(u64));

/**
 * Atmel HLCDC layer types
 */
enum atmel_hlcdc_layer_type {
	ATMEL_HLCDC_NO_LAYER,
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
	u32 *formats;
};

/**
 * Atmel HLCDC Layer description structure
 *
 * This structure describes the capabilities provided by a given layer.
 *
 * @name: layer name
 * @type: layer type
 * @id: layer id
 * @regs_offset: offset of the layer registers from the HLCDC registers base
 * @cfgs_offset: CFGX registers offset from the layer registers base
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
	int cfgs_offset;
	int clut_offset;
	struct atmel_hlcdc_formats *formats;
	struct atmel_hlcdc_layer_cfg_layout layout;
	int max_width;
	int max_height;
};

/**
 * Atmel HLCDC Layer.
 *
 * A layer can be a DRM plane of a post processing layer used to render
 * HLCDC composition into memory.
 *
 * @desc: layer description
 * @regmap: pointer to the HLCDC regmap
 */
struct atmel_hlcdc_layer {
	const struct atmel_hlcdc_layer_desc *desc;
	struct regmap *regmap;
};

/**
 * Atmel HLCDC Plane.
 *
 * @base: base DRM plane structure
 * @layer: HLCDC layer structure
 * @properties: pointer to the property definitions structure
 */
struct atmel_hlcdc_plane {
	struct drm_plane base;
	struct atmel_hlcdc_layer layer;
};

static inline struct atmel_hlcdc_plane *
drm_plane_to_atmel_hlcdc_plane(struct drm_plane *p)
{
	return container_of(p, struct atmel_hlcdc_plane, base);
}

static inline struct atmel_hlcdc_plane *
atmel_hlcdc_layer_to_plane(struct atmel_hlcdc_layer *layer)
{
	return container_of(layer, struct atmel_hlcdc_plane, layer);
}

/**
 * struct atmel_hlcdc_dc - Atmel HLCDC Display Controller.
 * @desc: HLCDC Display Controller description
 * @dscrpool: DMA coherent pool used to allocate DMA descriptors
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @crtc: CRTC provided by the display controller
 * @layers: active HLCDC layers
 * @suspend: used to store the HLCDC state when entering suspend
 * @suspend.imr: used to read/write LCDC Interrupt Mask Register
 * @suspend.state: Atomic commit structure
 */
struct atmel_hlcdc_dc {
	const struct atmel_hlcdc_dc_desc *desc;
	struct dma_pool *dscrpool;
	struct atmel_hlcdc *hlcdc;
	struct drm_crtc *crtc;
	struct atmel_hlcdc_layer *layers[ATMEL_HLCDC_MAX_LAYERS];
	struct {
		u32 imr;
		struct drm_atomic_state *state;
	} suspend;
};

struct atmel_hlcdc_plane_state;

/**
 * struct atmel_lcdc_dc_ops - describes atmel_lcdc ops group
 * to differentiate HLCDC and XLCDC IP code support
 * @plane_setup_scaler: update the vertical and horizontal scaling factors
 * @update_lcdc_buffers: update the each LCDC layers DMA registers
 * @lcdc_atomic_disable: disable LCDC interrupts and layers
 * @lcdc_update_general_settings: update each LCDC layers general
 * configuration register
 * @lcdc_atomic_update: enable the LCDC layers and interrupts
 * @lcdc_csc_init: update the color space conversion co-efficient of
 * High-end overlay register
 * @lcdc_irq_dbg: to raise alert incase of interrupt overrun in any LCDC layer
 */
struct atmel_lcdc_dc_ops {
	void (*plane_setup_scaler)(struct atmel_hlcdc_plane *plane,
				   struct atmel_hlcdc_plane_state *state);
	void (*lcdc_update_buffers)(struct atmel_hlcdc_plane *plane,
				    struct atmel_hlcdc_plane_state *state,
				    u32 sr, int i);
	void (*lcdc_atomic_disable)(struct atmel_hlcdc_plane *plane);
	void (*lcdc_update_general_settings)(struct atmel_hlcdc_plane *plane,
					     struct atmel_hlcdc_plane_state *state);
	void (*lcdc_atomic_update)(struct atmel_hlcdc_plane *plane,
				   struct atmel_hlcdc_dc *dc);
	void (*lcdc_csc_init)(struct atmel_hlcdc_plane *plane,
			      const struct atmel_hlcdc_layer_desc *desc);
	void (*lcdc_irq_dbg)(struct atmel_hlcdc_plane *plane,
			     const struct atmel_hlcdc_layer_desc *desc);
};

extern const struct atmel_lcdc_dc_ops atmel_hlcdc_ops;
extern const struct atmel_lcdc_dc_ops atmel_xlcdc_ops;

/**
 * Atmel HLCDC Display Controller description structure.
 *
 * This structure describes the HLCDC IP capabilities and depends on the
 * HLCDC IP version (or Atmel SoC family).
 *
 * @min_width: minimum width supported by the Display Controller
 * @min_height: minimum height supported by the Display Controller
 * @max_width: maximum width supported by the Display Controller
 * @max_height: maximum height supported by the Display Controller
 * @max_spw: maximum vertical/horizontal pulse width
 * @max_vpw: maximum vertical back/front porch width
 * @max_hpw: maximum horizontal back/front porch width
 * @conflicting_output_formats: true if RGBXXX output formats conflict with
 *				each other.
 * @fixed_clksrc: true if clock source is fixed
 * @is_xlcdc: true if XLCDC IP is supported
 * @layers: a layer description table describing available layers
 * @nlayers: layer description table size
 * @ops: atmel lcdc dc ops
 */
struct atmel_hlcdc_dc_desc {
	int min_width;
	int min_height;
	int max_width;
	int max_height;
	int max_spw;
	int max_vpw;
	int max_hpw;
	bool conflicting_output_formats;
	bool fixed_clksrc;
	bool is_xlcdc;
	const struct atmel_hlcdc_layer_desc *layers;
	int nlayers;
	const struct atmel_lcdc_dc_ops *ops;
};

extern struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_formats;
extern struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_and_yuv_formats;

static inline void atmel_hlcdc_layer_write_reg(struct atmel_hlcdc_layer *layer,
					       unsigned int reg, u32 val)
{
	regmap_write(layer->regmap, layer->desc->regs_offset + reg, val);
}

static inline u32 atmel_hlcdc_layer_read_reg(struct atmel_hlcdc_layer *layer,
					     unsigned int reg)
{
	u32 val;

	regmap_read(layer->regmap, layer->desc->regs_offset + reg, &val);

	return val;
}

static inline void atmel_hlcdc_layer_write_cfg(struct atmel_hlcdc_layer *layer,
					       unsigned int cfgid, u32 val)
{
	atmel_hlcdc_layer_write_reg(layer,
				    layer->desc->cfgs_offset +
				    (cfgid * sizeof(u32)), val);
}

static inline u32 atmel_hlcdc_layer_read_cfg(struct atmel_hlcdc_layer *layer,
					     unsigned int cfgid)
{
	return atmel_hlcdc_layer_read_reg(layer,
					  layer->desc->cfgs_offset +
					  (cfgid * sizeof(u32)));
}

static inline void atmel_hlcdc_layer_write_clut(struct atmel_hlcdc_layer *layer,
						unsigned int c, u32 val)
{
	regmap_write(layer->regmap,
		     layer->desc->clut_offset + c * sizeof(u32),
		     val);
}

static inline void atmel_hlcdc_layer_init(struct atmel_hlcdc_layer *layer,
				const struct atmel_hlcdc_layer_desc *desc,
				struct regmap *regmap)
{
	layer->desc = desc;
	layer->regmap = regmap;
}

enum drm_mode_status
atmel_hlcdc_dc_mode_valid(struct atmel_hlcdc_dc *dc,
			  const struct drm_display_mode *mode);

int atmel_hlcdc_create_planes(struct drm_device *dev);
void atmel_hlcdc_plane_irq(struct atmel_hlcdc_plane *plane);

int atmel_hlcdc_plane_prepare_disc_area(struct drm_crtc_state *c_state);
int atmel_hlcdc_plane_prepare_ahb_routing(struct drm_crtc_state *c_state);

void atmel_hlcdc_crtc_irq(struct drm_crtc *c);

int atmel_hlcdc_crtc_create(struct drm_device *dev);

int atmel_hlcdc_create_outputs(struct drm_device *dev);
int atmel_hlcdc_encoder_get_bus_fmt(struct drm_encoder *encoder);

#endif /* DRM_ATMEL_HLCDC_H */
