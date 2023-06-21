/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */
#ifndef __AST_DRV_H__
#define __AST_DRV_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/io.h>
#include <linux/types.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mode.h>
#include <drm/drm_framebuffer.h>

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"ast"
#define DRIVER_DESC		"AST"
#define DRIVER_DATE		"20120228"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	0

#define PCI_CHIP_AST2000 0x2000
#define PCI_CHIP_AST2100 0x2010

#define __AST_CHIP(__gen, __index)	((__gen) << 16 | (__index))

enum ast_chip {
	/* 1st gen */
	AST1000 = __AST_CHIP(1, 0), // unused
	AST2000 = __AST_CHIP(1, 1),
	/* 2nd gen */
	AST1100 = __AST_CHIP(2, 0),
	AST2100 = __AST_CHIP(2, 1),
	AST2050 = __AST_CHIP(2, 2), // unused
	/* 3rd gen */
	AST2200 = __AST_CHIP(3, 0),
	AST2150 = __AST_CHIP(3, 1),
	/* 4th gen */
	AST2300 = __AST_CHIP(4, 0),
	AST1300 = __AST_CHIP(4, 1),
	AST1050 = __AST_CHIP(4, 2), // unused
	/* 5th gen */
	AST2400 = __AST_CHIP(5, 0),
	AST1400 = __AST_CHIP(5, 1), // unused
	AST1250 = __AST_CHIP(5, 2), // unused
	/* 6th gen */
	AST2500 = __AST_CHIP(6, 0),
	AST2510 = __AST_CHIP(6, 1), // unused
	AST2520 = __AST_CHIP(6, 2), // unused
	/* 7th gen */
	AST2600 = __AST_CHIP(7, 0),
	AST2620 = __AST_CHIP(7, 1), // unused
};

#define __AST_CHIP_GEN(__chip)	(((unsigned long)(__chip)) >> 16)

enum ast_tx_chip {
	AST_TX_NONE,
	AST_TX_SIL164,
	AST_TX_DP501,
	AST_TX_ASTDP,
};

#define AST_TX_NONE_BIT		BIT(AST_TX_NONE)
#define AST_TX_SIL164_BIT	BIT(AST_TX_SIL164)
#define AST_TX_DP501_BIT	BIT(AST_TX_DP501)
#define AST_TX_ASTDP_BIT	BIT(AST_TX_ASTDP)

#define AST_DRAM_512Mx16 0
#define AST_DRAM_1Gx16   1
#define AST_DRAM_512Mx32 2
#define AST_DRAM_1Gx32   3
#define AST_DRAM_2Gx16   6
#define AST_DRAM_4Gx16   7
#define AST_DRAM_8Gx16   8

/*
 * Hardware cursor
 */

#define AST_MAX_HWC_WIDTH	64
#define AST_MAX_HWC_HEIGHT	64

#define AST_HWC_SIZE		(AST_MAX_HWC_WIDTH * AST_MAX_HWC_HEIGHT * 2)
#define AST_HWC_SIGNATURE_SIZE	32

/* define for signature structure */
#define AST_HWC_SIGNATURE_CHECKSUM	0x00
#define AST_HWC_SIGNATURE_SizeX		0x04
#define AST_HWC_SIGNATURE_SizeY		0x08
#define AST_HWC_SIGNATURE_X		0x0C
#define AST_HWC_SIGNATURE_Y		0x10
#define AST_HWC_SIGNATURE_HOTSPOTX	0x14
#define AST_HWC_SIGNATURE_HOTSPOTY	0x18

/*
 * Planes
 */

struct ast_plane {
	struct drm_plane base;

	void __iomem *vaddr;
	u64 offset;
	unsigned long size;
};

static inline struct ast_plane *to_ast_plane(struct drm_plane *plane)
{
	return container_of(plane, struct ast_plane, base);
}

/*
 * Connector with i2c channel
 */

struct ast_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	struct i2c_algo_bit_data bit;
};

struct ast_vga_connector {
	struct drm_connector base;
	struct ast_i2c_chan *i2c;
};

static inline struct ast_vga_connector *
to_ast_vga_connector(struct drm_connector *connector)
{
	return container_of(connector, struct ast_vga_connector, base);
}

struct ast_sil164_connector {
	struct drm_connector base;
	struct ast_i2c_chan *i2c;
};

static inline struct ast_sil164_connector *
to_ast_sil164_connector(struct drm_connector *connector)
{
	return container_of(connector, struct ast_sil164_connector, base);
}

/*
 * Device
 */

struct ast_device {
	struct drm_device base;

	struct mutex ioregs_lock; /* Protects access to I/O registers in ioregs */
	void __iomem *regs;
	void __iomem *ioregs;
	void __iomem *dp501_fw_buf;

	enum ast_chip chip;
	uint32_t dram_bus_width;
	uint32_t dram_type;
	uint32_t mclk;

	void __iomem	*vram;
	unsigned long	vram_base;
	unsigned long	vram_size;
	unsigned long	vram_fb_available;

	struct ast_plane primary_plane;
	struct ast_plane cursor_plane;
	struct drm_crtc crtc;
	struct {
		struct {
			struct drm_encoder encoder;
			struct ast_vga_connector vga_connector;
		} vga;
		struct {
			struct drm_encoder encoder;
			struct ast_sil164_connector sil164_connector;
		} sil164;
		struct {
			struct drm_encoder encoder;
			struct drm_connector connector;
		} dp501;
		struct {
			struct drm_encoder encoder;
			struct drm_connector connector;
		} astdp;
	} output;

	bool support_wide_screen;
	enum {
		ast_use_p2a,
		ast_use_dt,
		ast_use_defaults
	} config_mode;

	unsigned long tx_chip_types;		/* bitfield of enum ast_chip_type */
	u8 *dp501_fw_addr;
	const struct firmware *dp501_fw;	/* dp501 fw */
};

static inline struct ast_device *to_ast_device(struct drm_device *dev)
{
	return container_of(dev, struct ast_device, base);
}

struct ast_device *ast_device_create(const struct drm_driver *drv,
				     struct pci_dev *pdev,
				     unsigned long flags);

static inline unsigned long __ast_gen(struct ast_device *ast)
{
	return __AST_CHIP_GEN(ast->chip);
}
#define AST_GEN(__ast)	__ast_gen(__ast)

static inline bool __ast_gen_is_eq(struct ast_device *ast, unsigned long gen)
{
	return __ast_gen(ast) == gen;
}
#define IS_AST_GEN1(__ast)	__ast_gen_is_eq(__ast, 1)
#define IS_AST_GEN2(__ast)	__ast_gen_is_eq(__ast, 2)
#define IS_AST_GEN3(__ast)	__ast_gen_is_eq(__ast, 3)
#define IS_AST_GEN4(__ast)	__ast_gen_is_eq(__ast, 4)
#define IS_AST_GEN5(__ast)	__ast_gen_is_eq(__ast, 5)
#define IS_AST_GEN6(__ast)	__ast_gen_is_eq(__ast, 6)
#define IS_AST_GEN7(__ast)	__ast_gen_is_eq(__ast, 7)

#define AST_IO_AR_PORT_WRITE		(0x40)
#define AST_IO_MISC_PORT_WRITE		(0x42)
#define AST_IO_VGA_ENABLE_PORT		(0x43)
#define AST_IO_SEQ_PORT			(0x44)
#define AST_IO_DAC_INDEX_READ		(0x47)
#define AST_IO_DAC_INDEX_WRITE		(0x48)
#define AST_IO_DAC_DATA		        (0x49)
#define AST_IO_GR_PORT			(0x4E)
#define AST_IO_CRTC_PORT		(0x54)
#define AST_IO_INPUT_STATUS1_READ	(0x5A)
#define AST_IO_MISC_PORT_READ		(0x4C)

#define AST_IO_MM_OFFSET		(0x380)

#define AST_IO_VGAIR1_VREFRESH		BIT(3)

#define AST_IO_VGACRCB_HWC_ENABLED     BIT(1)
#define AST_IO_VGACRCB_HWC_16BPP       BIT(0) /* set: ARGB4444, cleared: 2bpp palette */

static inline u32 ast_read32(struct ast_device *ast, u32 reg)
{
	return ioread32(ast->regs + reg);
}

static inline void ast_write32(struct ast_device *ast, u32 reg, u32 val)
{
	iowrite32(val, ast->regs + reg);
}

static inline u8 ast_io_read8(struct ast_device *ast, u32 reg)
{
	return ioread8(ast->ioregs + reg);
}

static inline void ast_io_write8(struct ast_device *ast, u32 reg, u8 val)
{
	iowrite8(val, ast->ioregs + reg);
}

static inline u8 ast_get_index_reg(struct ast_device *ast, u32 base, u8 index)
{
	ast_io_write8(ast, base, index);
	++base;
	return ast_io_read8(ast, base);
}

static inline u8 ast_get_index_reg_mask(struct ast_device *ast, u32 base, u8 index,
					u8 preserve_mask)
{
	u8 val = ast_get_index_reg(ast, base, index);

	return val & preserve_mask;
}

static inline void ast_set_index_reg(struct ast_device *ast, u32 base, u8 index, u8 val)
{
	ast_io_write8(ast, base, index);
	++base;
	ast_io_write8(ast, base, val);
}

static inline void ast_set_index_reg_mask(struct ast_device *ast, u32 base, u8 index,
					  u8 preserve_mask, u8 val)
{
	u8 tmp = ast_get_index_reg_mask(ast, base, index, preserve_mask);

	tmp |= val;
	ast_set_index_reg(ast, base, index, tmp);
}

#define AST_VIDMEM_SIZE_8M    0x00800000
#define AST_VIDMEM_SIZE_16M   0x01000000
#define AST_VIDMEM_SIZE_32M   0x02000000
#define AST_VIDMEM_SIZE_64M   0x04000000
#define AST_VIDMEM_SIZE_128M  0x08000000

#define AST_VIDMEM_DEFAULT_SIZE AST_VIDMEM_SIZE_8M

struct ast_vbios_stdtable {
	u8 misc;
	u8 seq[4];
	u8 crtc[25];
	u8 ar[20];
	u8 gr[9];
};

struct ast_vbios_enhtable {
	u32 ht;
	u32 hde;
	u32 hfp;
	u32 hsync;
	u32 vt;
	u32 vde;
	u32 vfp;
	u32 vsync;
	u32 dclk_index;
	u32 flags;
	u32 refresh_rate;
	u32 refresh_rate_index;
	u32 mode_id;
};

struct ast_vbios_dclk_info {
	u8 param1;
	u8 param2;
	u8 param3;
};

struct ast_vbios_mode_info {
	const struct ast_vbios_stdtable *std_table;
	const struct ast_vbios_enhtable *enh_table;
};

struct ast_crtc_state {
	struct drm_crtc_state base;

	/* Last known format of primary plane */
	const struct drm_format_info *format;

	struct ast_vbios_mode_info vbios_mode_info;
};

#define to_ast_crtc_state(state) container_of(state, struct ast_crtc_state, base)

int ast_mode_config_init(struct ast_device *ast);

#define AST_MM_ALIGN_SHIFT 4
#define AST_MM_ALIGN_MASK ((1 << AST_MM_ALIGN_SHIFT) - 1)

#define AST_DP501_FW_VERSION_MASK	GENMASK(7, 4)
#define AST_DP501_FW_VERSION_1		BIT(4)
#define AST_DP501_PNP_CONNECTED		BIT(1)

#define AST_DP501_DEFAULT_DCLK	65

#define AST_DP501_GBL_VERSION	0xf000
#define AST_DP501_PNPMONITOR	0xf010
#define AST_DP501_LINKRATE	0xf014
#define AST_DP501_EDID_DATA	0xf020

/*
 * Display Transmitter Type:
 */
#define TX_TYPE_MASK				GENMASK(3, 1)
#define NO_TX						(0 << 1)
#define ITE66121_VBIOS_TX			(1 << 1)
#define SI164_VBIOS_TX				(2 << 1)
#define CH7003_VBIOS_TX			(3 << 1)
#define DP501_VBIOS_TX				(4 << 1)
#define ANX9807_VBIOS_TX			(5 << 1)
#define TX_FW_EMBEDDED_FW_TX		(6 << 1)
#define ASTDP_DPMCU_TX				(7 << 1)

#define AST_VRAM_INIT_STATUS_MASK	GENMASK(7, 6)
//#define AST_VRAM_INIT_BY_BMC		BIT(7)
//#define AST_VRAM_INIT_READY		BIT(6)

/* Define for Soc scratched reg used on ASTDP */
#define AST_DP_PHY_SLEEP			BIT(4)
#define AST_DP_VIDEO_ENABLE		BIT(0)

#define AST_DP_POWER_ON			true
#define AST_DP_POWER_OFF			false

/*
 * CRD1[b5]: DP MCU FW is executing
 * CRDC[b0]: DP link success
 * CRDF[b0]: DP HPD
 * CRE5[b0]: Host reading EDID process is done
 */
#define ASTDP_MCU_FW_EXECUTING			BIT(5)
#define ASTDP_LINK_SUCCESS				BIT(0)
#define ASTDP_HPD						BIT(0)
#define ASTDP_HOST_EDID_READ_DONE		BIT(0)
#define ASTDP_HOST_EDID_READ_DONE_MASK	GENMASK(0, 0)

/*
 * CRB8[b1]: Enable VSYNC off
 * CRB8[b0]: Enable HSYNC off
 */
#define AST_DPMS_VSYNC_OFF				BIT(1)
#define AST_DPMS_HSYNC_OFF				BIT(0)

/*
 * CRDF[b4]: Mirror of AST_DP_VIDEO_ENABLE
 * Precondition:	A. ~AST_DP_PHY_SLEEP  &&
 *			B. DP_HPD &&
 *			C. DP_LINK_SUCCESS
 */
#define ASTDP_MIRROR_VIDEO_ENABLE		BIT(4)

#define ASTDP_EDID_READ_POINTER_MASK	GENMASK(7, 0)
#define ASTDP_EDID_VALID_FLAG_MASK		GENMASK(0, 0)
#define ASTDP_EDID_READ_DATA_MASK		GENMASK(7, 0)

/*
 * ASTDP setmode registers:
 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
 * CRE1[7:0]: MISC1 (default: 0x00)
 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
 */
#define ASTDP_MISC0_24bpp			BIT(5)
#define ASTDP_MISC1				0
#define ASTDP_AND_CLEAR_MASK		0x00

/*
 * ASTDP resoultion table:
 * EX:	ASTDP_A_B_C:
 *		A: Resolution
 *		B: Refresh Rate
 *		C: Misc information, such as CVT, Reduce Blanked
 */
#define ASTDP_640x480_60		0x00
#define ASTDP_640x480_72		0x01
#define ASTDP_640x480_75		0x02
#define ASTDP_640x480_85		0x03
#define ASTDP_800x600_56		0x04
#define ASTDP_800x600_60		0x05
#define ASTDP_800x600_72		0x06
#define ASTDP_800x600_75		0x07
#define ASTDP_800x600_85		0x08
#define ASTDP_1024x768_60		0x09
#define ASTDP_1024x768_70		0x0A
#define ASTDP_1024x768_75		0x0B
#define ASTDP_1024x768_85		0x0C
#define ASTDP_1280x1024_60		0x0D
#define ASTDP_1280x1024_75		0x0E
#define ASTDP_1280x1024_85		0x0F
#define ASTDP_1600x1200_60		0x10
#define ASTDP_320x240_60		0x11
#define ASTDP_400x300_60		0x12
#define ASTDP_512x384_60		0x13
#define ASTDP_1920x1200_60		0x14
#define ASTDP_1920x1080_60		0x15
#define ASTDP_1280x800_60		0x16
#define ASTDP_1280x800_60_RB	0x17
#define ASTDP_1440x900_60		0x18
#define ASTDP_1440x900_60_RB	0x19
#define ASTDP_1680x1050_60		0x1A
#define ASTDP_1680x1050_60_RB	0x1B
#define ASTDP_1600x900_60		0x1C
#define ASTDP_1600x900_60_RB	0x1D
#define ASTDP_1366x768_60		0x1E
#define ASTDP_1152x864_75		0x1F

int ast_mm_init(struct ast_device *ast);

/* ast post */
void ast_post_gpu(struct drm_device *dev);
u32 ast_mindwm(struct ast_device *ast, u32 r);
void ast_moutdwm(struct ast_device *ast, u32 r, u32 v);
void ast_patch_ahb_2500(struct ast_device *ast);
/* ast dp501 */
void ast_set_dp501_video_output(struct drm_device *dev, u8 mode);
bool ast_backup_fw(struct drm_device *dev, u8 *addr, u32 size);
bool ast_dp501_read_edid(struct drm_device *dev, u8 *ediddata);
u8 ast_get_dp501_max_clk(struct drm_device *dev);
void ast_init_3rdtx(struct drm_device *dev);

/* ast_i2c.c */
struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev);

/* aspeed DP */
int ast_astdp_read_edid(struct drm_device *dev, u8 *ediddata);
void ast_dp_launch(struct drm_device *dev);
void ast_dp_power_on_off(struct drm_device *dev, bool no);
void ast_dp_set_on_off(struct drm_device *dev, bool no);
void ast_dp_set_mode(struct drm_crtc *crtc, struct ast_vbios_mode_info *vbios_mode);

#endif
