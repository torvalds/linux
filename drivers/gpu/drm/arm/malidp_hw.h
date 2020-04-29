/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * (C) COPYRIGHT 2013-2016 ARM Limited. All rights reserved.
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
	SE_MEMWRITE = BIT(5),
};

enum rotation_features {
	ROTATE_NONE,		/* does not support rotation at all */
	ROTATE_ANY,		/* supports rotation on any buffers */
	ROTATE_COMPRESSED,	/* supports rotation only on compressed buffers */
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
	u32 err_mask;		/* mask of bits that represent errors */
};

struct malidp_layer {
	u16 id;			/* layer ID */
	u16 base;		/* address offset for the register bank */
	u16 ptr;		/* address offset for the pointer register */
	u16 stride_offset;	/* offset to the first stride register. */
	s16 yuv2rgb_offset;	/* offset to the YUV->RGB matrix entries */
	u16 mmu_ctrl_offset;    /* offset to the MMU control register */
	enum rotation_features rot;	/* type of rotation supported */
	/* address offset for the AFBC decoder registers */
	u16 afbc_decoder_offset;
};

enum malidp_scaling_coeff_set {
	MALIDP_UPSCALING_COEFFS = 1,
	MALIDP_DOWNSCALING_1_5_COEFFS = 2,
	MALIDP_DOWNSCALING_2_COEFFS = 3,
	MALIDP_DOWNSCALING_2_75_COEFFS = 4,
	MALIDP_DOWNSCALING_4_COEFFS = 5,
};

struct malidp_se_config {
	u8 scale_enable : 1;
	u8 enhancer_enable : 1;
	u8 hcoeff : 3;
	u8 vcoeff : 3;
	u8 plane_src_id;
	u16 input_w, input_h;
	u16 output_w, output_h;
	u32 h_init_phase, h_delta_phase;
	u32 v_init_phase, v_delta_phase;
};

/* regmap features */
#define MALIDP_REGMAP_HAS_CLEARIRQ				BIT(0)
#define MALIDP_DEVICE_AFBC_SUPPORT_SPLIT			BIT(1)
#define MALIDP_DEVICE_AFBC_YUV_420_10_SUPPORT_SPLIT		BIT(2)
#define MALIDP_DEVICE_AFBC_YUYV_USE_422_P2			BIT(3)

struct malidp_hw_regmap {
	/* address offset of the DE register bank */
	/* is always 0x0000 */
	/* address offset of the DE coefficients registers */
	const u16 coeffs_base;
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

struct malidp_hw_device;

/*
 * Static structure containing hardware specific data and pointers to
 * functions that behave differently between various versions of the IP.
 */
struct malidp_hw {
	const struct malidp_hw_regmap map;

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
	 * Set/clear configuration valid flag for hardware parameters that can
	 * be changed outside the configuration mode to the given value.
	 * Hardware will use the new settings when config valid is set,
	 * after the end of the current buffer scanout, and will ignore
	 * any new values for those parameters if config valid flag is cleared
	 */
	void (*set_config_valid)(struct malidp_hw_device *hwdev, u8 value);

	/*
	 * Set a new mode in hardware. Requires the hardware to be in
	 * configuration mode before this function is called.
	 */
	void (*modeset)(struct malidp_hw_device *hwdev, struct videomode *m);

	/*
	 * Calculate the required rotation memory given the active area
	 * and the buffer format.
	 */
	int (*rotmem_required)(struct malidp_hw_device *hwdev, u16 w, u16 h,
			       u32 fmt, bool has_modifier);

	int (*se_set_scaling_coeffs)(struct malidp_hw_device *hwdev,
				     struct malidp_se_config *se_config,
				     struct malidp_se_config *old_config);

	long (*se_calc_mclk)(struct malidp_hw_device *hwdev,
			     struct malidp_se_config *se_config,
			     struct videomode *vm);
	/*
	 * Enable writing to memory the content of the next frame
	 * @param hwdev - malidp_hw_device structure containing the HW description
	 * @param addrs - array of addresses for each plane
	 * @param pitches - array of pitches for each plane
	 * @param num_planes - number of planes to be written
	 * @param w - width of the output frame
	 * @param h - height of the output frame
	 * @param fmt_id - internal format ID of output buffer
	 */
	int (*enable_memwrite)(struct malidp_hw_device *hwdev, dma_addr_t *addrs,
			       s32 *pitches, int num_planes, u16 w, u16 h, u32 fmt_id,
			       const s16 *rgb2yuv_coeffs);

	/*
	 * Disable the writing to memory of the next frame's content.
	 */
	void (*disable_memwrite)(struct malidp_hw_device *hwdev);

	u8 features;
};

/* Supported variants of the hardware */
enum {
	MALIDP_500 = 0,
	MALIDP_550,
	MALIDP_650,
	/* keep the next entry last */
	MALIDP_MAX_DEVICES
};

extern const struct malidp_hw malidp_device[MALIDP_MAX_DEVICES];

/*
 * Structure used by the driver during runtime operation.
 */
struct malidp_hw_device {
	struct malidp_hw *hw;
	void __iomem *regs;

	/* APB clock */
	struct clk *pclk;
	/* AXI clock */
	struct clk *aclk;
	/* main clock for display core */
	struct clk *mclk;
	/* pixel clock for display core */
	struct clk *pxlclk;

	u8 min_line_size;
	u16 max_line_size;
	u32 output_color_depth;

	/* track the device PM state */
	bool pm_suspended;

	/* track the SE memory writeback state */
	u8 mw_state;

	/* size of memory used for rotating layers, up to two banks available */
	u32 rotation_memory[2];

	/* priority level of RQOS register used for driven the ARQOS signal */
	u32 arqos_value;
};

static inline u32 malidp_hw_read(struct malidp_hw_device *hwdev, u32 reg)
{
	WARN_ON(hwdev->pm_suspended);
	return readl(hwdev->regs + reg);
}

static inline void malidp_hw_write(struct malidp_hw_device *hwdev,
				   u32 value, u32 reg)
{
	WARN_ON(hwdev->pm_suspended);
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
		return hwdev->hw->map.se_base;
	case MALIDP_DC_BLOCK:
		return hwdev->hw->map.dc_base;
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
void malidp_se_irq_hw_init(struct malidp_hw_device *hwdev);
void malidp_de_irq_hw_init(struct malidp_hw_device *hwdev);
void malidp_de_irq_fini(struct malidp_hw_device *hwdev);
int malidp_se_irq_init(struct drm_device *drm, int irq);
void malidp_se_irq_fini(struct malidp_hw_device *hwdev);

u8 malidp_hw_get_format_id(const struct malidp_hw_regmap *map,
			   u8 layer_id, u32 format, bool has_modifier);

int malidp_format_get_bpp(u32 fmt);

static inline u8 malidp_hw_get_pitch_align(struct malidp_hw_device *hwdev, bool rotated)
{
	/*
	 * only hardware that cannot do 8 bytes bus alignments have further
	 * constraints on rotated planes
	 */
	if (hwdev->hw->map.bus_align_bytes == 8)
		return 8;
	else
		return hwdev->hw->map.bus_align_bytes << (rotated ? 2 : 0);
}

/* U16.16 */
#define FP_1_00000	0x00010000	/* 1.0 */
#define FP_0_66667	0x0000AAAA	/* 0.6667 = 1/1.5 */
#define FP_0_50000	0x00008000	/* 0.5 = 1/2 */
#define FP_0_36363	0x00005D17	/* 0.36363 = 1/2.75 */
#define FP_0_25000	0x00004000	/* 0.25 = 1/4 */

static inline enum malidp_scaling_coeff_set
malidp_se_select_coeffs(u32 upscale_factor)
{
	return (upscale_factor >= FP_1_00000) ? MALIDP_UPSCALING_COEFFS :
	       (upscale_factor >= FP_0_66667) ? MALIDP_DOWNSCALING_1_5_COEFFS :
	       (upscale_factor >= FP_0_50000) ? MALIDP_DOWNSCALING_2_COEFFS :
	       (upscale_factor >= FP_0_36363) ? MALIDP_DOWNSCALING_2_75_COEFFS :
	       MALIDP_DOWNSCALING_4_COEFFS;
}

#undef FP_0_25000
#undef FP_0_36363
#undef FP_0_50000
#undef FP_0_66667
#undef FP_1_00000

static inline void malidp_se_set_enh_coeffs(struct malidp_hw_device *hwdev)
{
	static const s32 enhancer_coeffs[] = {
		-8, -8, -8, -8, 128, -8, -8, -8, -8
	};
	u32 val = MALIDP_SE_SET_ENH_LIMIT_LOW(MALIDP_SE_ENH_LOW_LEVEL) |
		  MALIDP_SE_SET_ENH_LIMIT_HIGH(MALIDP_SE_ENH_HIGH_LEVEL);
	u32 image_enh = hwdev->hw->map.se_base +
			((hwdev->hw->map.features & MALIDP_REGMAP_HAS_CLEARIRQ) ?
			 0x10 : 0xC) + MALIDP_SE_IMAGE_ENH;
	u32 enh_coeffs = image_enh + MALIDP_SE_ENH_COEFF0;
	int i;

	malidp_hw_write(hwdev, val, image_enh);
	for (i = 0; i < ARRAY_SIZE(enhancer_coeffs); ++i)
		malidp_hw_write(hwdev, enhancer_coeffs[i], enh_coeffs + i * 4);
}

/*
 * background color components are defined as 12bits values,
 * they will be shifted right when stored on hardware that
 * supports only 8bits per channel
 */
#define MALIDP_BGND_COLOR_R		0x000
#define MALIDP_BGND_COLOR_G		0x000
#define MALIDP_BGND_COLOR_B		0x000

#define MALIDP_COLORADJ_NUM_COEFFS	12
#define MALIDP_COEFFTAB_NUM_COEFFS	64

#define MALIDP_GAMMA_LUT_SIZE		4096

#define AFBC_SIZE_MASK		AFBC_FORMAT_MOD_BLOCK_SIZE_MASK
#define AFBC_SIZE_16X16		AFBC_FORMAT_MOD_BLOCK_SIZE_16x16
#define AFBC_YTR		AFBC_FORMAT_MOD_YTR
#define AFBC_SPARSE		AFBC_FORMAT_MOD_SPARSE
#define AFBC_CBR		AFBC_FORMAT_MOD_CBR
#define AFBC_SPLIT		AFBC_FORMAT_MOD_SPLIT
#define AFBC_TILED		AFBC_FORMAT_MOD_TILED
#define AFBC_SC			AFBC_FORMAT_MOD_SC

#define AFBC_MOD_VALID_BITS	(AFBC_SIZE_MASK | AFBC_YTR | AFBC_SPLIT | \
				 AFBC_SPARSE | AFBC_CBR | AFBC_TILED | AFBC_SC)

extern const u64 malidp_format_modifiers[];

#endif  /* __MALIDP_HW_H__ */
