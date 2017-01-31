/*
 *
 * (C) COPYRIGHT 2013-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP hardware manipulation routines.
 */

#ifndef __MALIDP_HW_H__
#define __MALIDP_HW_H__

#include <linux/bitops.h>
#include "malidp_regs.h"

struct videomode;
struct clk;

/* Mali DP IP blocks */
enum {
	MALIDP_DE_BLOCK = 0,
	MALIDP_SE_BLOCK,
	MALIDP_DC_BLOCK
};

/* Mali DP layer IDs */
enum {
	DE_VIDEO1 = BIT(0),
	DE_GRAPHICS1 = BIT(1),
	DE_GRAPHICS2 = BIT(2), /* used only in DP500 */
	DE_VIDEO2 = BIT(3),
	DE_SMART = BIT(4),
};

struct malidp_format_id {
	u32 format;		/* DRM fourcc */
	u8 layer;		/* bitmask of layers supporting it */
	u8 id;			/* used internally */
};

#define MALIDP_INVALID_FORMAT_ID	0xff

/*
 * hide the differences between register maps
 * by using a common structure to hold the
 * base register offsets
 */

struct malidp_irq_map {
	u32 irq_mask;		/* mask of IRQs that can be enabled in the block */
	u32 vsync_irq;		/* IRQ bit used for signaling during VSYNC */
};

struct malidp_layer {
	u16 id;			/* layer ID */
	u16 base;		/* address offset for the register bank */
	u16 ptr;		/* address offset for the pointer register */
	u16 stride_offset;	/* Offset to the first stride register. */
};

/* regmap features */
#define MALIDP_REGMAP_HAS_CLEARIRQ	(1 << 0)

struct malidp_hw_regmap {
	/* address offset of the DE register bank */
	/* is always 0x0000 */
	/* address offset of the SE registers bank */
	const u16 se_base;
	/* address offset of the DC registers bank */
	const u16 dc_base;

	/* address offset for the output depth register */
	const u16 out_depth_base;

	/* bitmap with register map features */
	const u8 features;

	/* list of supported layers */
	const u8 n_layers;
	const struct malidp_layer *layers;

	const struct malidp_irq_map de_irq_map;
	const struct malidp_irq_map se_irq_map;
	const struct malidp_irq_map dc_irq_map;

	/* list of supported pixel formats for each layer */
	const struct malidp_format_id *pixel_formats;
	const u8 n_pixel_formats;

	/* pitch alignment requirement in bytes */
	const u8 bus_align_bytes;
};

/* device features */
/* Unlike DP550/650, DP500 has 3 stride registers in its video layer. */
#define MALIDP_DEVICE_LV_HAS_3_STRIDES	BIT(0)

struct malidp_hw_device {
	const struct malidp_hw_regmap map;
	void __iomem *regs;

	/* APB clock */
	struct clk *pclk;
	/* AXI clock */
	struct clk *aclk;
	/* main clock for display core */
	struct clk *mclk;
	/* pixel clock for display core */
	struct clk *pxlclk;

	/*
	 * Validate the driver instance against the hardware bits
	 */
	int (*query_hw)(struct malidp_hw_device *hwdev);

	/*
	 * Set the hardware into config mode, ready to accept mode changes
	 */
	void (*enter_config_mode)(struct malidp_hw_device *hwdev);

	/*
	 * Tell hardware to exit configuration mode
	 */
	void (*leave_config_mode)(struct malidp_hw_device *hwdev);

	/*
	 * Query if hardware is in configuration mode
	 */
	bool (*in_config_mode)(struct malidp_hw_device *hwdev);

	/*
	 * Set configuration valid flag for hardware parameters that can
	 * be changed outside the configuration mode. Hardware will use
	 * the new settings when config valid is set after the end of the
	 * current buffer scanout
	 */
	void (*set_config_valid)(struct malidp_hw_device *hwdev);

	/*
	 * Set a new mode in hardware. Requires the hardware to be in
	 * configuration mode before this function is called.
	 */
	void (*modeset)(struct malidp_hw_device *hwdev, struct videomode *m);

	/*
	 * Calculate the required rotation memory given the active area
	 * and the buffer format.
	 */
	int (*rotmem_required)(struct malidp_hw_device *hwdev, u16 w, u16 h, u32 fmt);

	u8 features;

	u8 min_line_size;
	u16 max_line_size;

	/* size of memory used for rotating layers, up to two banks available */
	u32 rotation_memory[2];
};

/* Supported variants of the hardware */
enum {
	MALIDP_500 = 0,
	MALIDP_550,
	MALIDP_650,
	/* keep the next entry last */
	MALIDP_MAX_DEVICES
};

extern const struct malidp_hw_device malidp_device[MALIDP_MAX_DEVICES];

static inline u32 malidp_hw_read(struct malidp_hw_device *hwdev, u32 reg)
{
	return readl(hwdev->regs + reg);
}

static inline void malidp_hw_write(struct malidp_hw_device *hwdev,
				   u32 value, u32 reg)
{
	writel(value, hwdev->regs + reg);
}

static inline void malidp_hw_setbits(struct malidp_hw_device *hwdev,
				     u32 mask, u32 reg)
{
	u32 data = malidp_hw_read(hwdev, reg);

	data |= mask;
	malidp_hw_write(hwdev, data, reg);
}

static inline void malidp_hw_clearbits(struct malidp_hw_device *hwdev,
				       u32 mask, u32 reg)
{
	u32 data = malidp_hw_read(hwdev, reg);

	data &= ~mask;
	malidp_hw_write(hwdev, data, reg);
}

static inline u32 malidp_get_block_base(struct malidp_hw_device *hwdev,
					u8 block)
{
	switch (block) {
	case MALIDP_SE_BLOCK:
		return hwdev->map.se_base;
	case MALIDP_DC_BLOCK:
		return hwdev->map.dc_base;
	}

	return 0;
}

static inline void malidp_hw_disable_irq(struct malidp_hw_device *hwdev,
					 u8 block, u32 irq)
{
	u32 base = malidp_get_block_base(hwdev, block);

	malidp_hw_clearbits(hwdev, irq, base + MALIDP_REG_MASKIRQ);
}

static inline void malidp_hw_enable_irq(struct malidp_hw_device *hwdev,
					u8 block, u32 irq)
{
	u32 base = malidp_get_block_base(hwdev, block);

	malidp_hw_setbits(hwdev, irq, base + MALIDP_REG_MASKIRQ);
}

int malidp_de_irq_init(struct drm_device *drm, int irq);
void malidp_de_irq_fini(struct drm_device *drm);
int malidp_se_irq_init(struct drm_device *drm, int irq);
void malidp_se_irq_fini(struct drm_device *drm);

u8 malidp_hw_get_format_id(const struct malidp_hw_regmap *map,
			   u8 layer_id, u32 format);

static inline bool malidp_hw_pitch_valid(struct malidp_hw_device *hwdev,
					 unsigned int pitch)
{
	return !(pitch & (hwdev->map.bus_align_bytes - 1));
}

/*
 * background color components are defined as 12bits values,
 * they will be shifted right when stored on hardware that
 * supports only 8bits per channel
 */
#define MALIDP_BGND_COLOR_R		0x000
#define MALIDP_BGND_COLOR_G		0x000
#define MALIDP_BGND_COLOR_B		0x000

#endif  /* __MALIDP_HW_H__ */
