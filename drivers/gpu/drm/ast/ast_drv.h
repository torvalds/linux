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

#include <linux/io.h>
#include <linux/types.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mode.h>
#include <drm/drm_framebuffer.h>

#include "ast_reg.h"

struct ast_vbios_enhtable;

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"ast"
#define DRIVER_DESC		"AST"

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
	AST1400 = __AST_CHIP(5, 1),
	AST1250 = __AST_CHIP(5, 2), // unused
	/* 6th gen */
	AST2500 = __AST_CHIP(6, 0),
	AST2510 = __AST_CHIP(6, 1),
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

enum ast_config_mode {
	ast_use_p2a,
	ast_use_dt,
	ast_use_defaults
};

enum ast_dram_layout {
	AST_DRAM_512Mx16 = 0,
	AST_DRAM_1Gx16 = 1,
	AST_DRAM_512Mx32 = 2,
	AST_DRAM_1Gx32 = 3,
	AST_DRAM_2Gx16 = 6,
	AST_DRAM_4Gx16 = 7,
	AST_DRAM_8Gx16 = 8,
};

/*
 * Hardware cursor
 */

#define AST_MAX_HWC_WIDTH	64
#define AST_MAX_HWC_HEIGHT	64
#define AST_HWC_PITCH		(AST_MAX_HWC_WIDTH * SZ_2)
#define AST_HWC_SIZE		(AST_MAX_HWC_HEIGHT * AST_HWC_PITCH)

/*
 * Planes
 */

struct ast_plane {
	struct drm_plane base;

	u64 offset;
	unsigned long size;
};

static inline struct ast_plane *to_ast_plane(struct drm_plane *plane)
{
	return container_of(plane, struct ast_plane, base);
}

struct ast_cursor_plane {
	struct ast_plane base;

	u8 argb4444[AST_HWC_SIZE];
};

static inline struct ast_cursor_plane *to_ast_cursor_plane(struct drm_plane *plane)
{
	return container_of(to_ast_plane(plane), struct ast_cursor_plane, base);
}

/*
 * Connector
 */

struct ast_connector {
	struct drm_connector base;

	enum drm_connector_status physical_status;
};

static inline struct ast_connector *
to_ast_connector(struct drm_connector *connector)
{
	return container_of(connector, struct ast_connector, base);
}

/*
 * Device
 */

struct ast_device {
	struct drm_device base;

	void __iomem *regs;
	void __iomem *ioregs;
	void __iomem *dp501_fw_buf;

	enum ast_config_mode config_mode;
	enum ast_chip chip;

	void __iomem	*vram;
	unsigned long	vram_base;
	unsigned long	vram_size;

	struct mutex modeset_lock; /* Protects access to modeset I/O registers in ioregs */

	enum ast_tx_chip tx_chip;

	struct ast_plane primary_plane;
	struct ast_cursor_plane cursor_plane;
	struct drm_crtc crtc;
	union {
		struct {
			struct drm_encoder encoder;
			struct ast_connector connector;
		} vga;
		struct {
			struct drm_encoder encoder;
			struct ast_connector connector;
		} sil164;
		struct {
			struct drm_encoder encoder;
			struct ast_connector connector;
		} dp501;
		struct {
			struct drm_encoder encoder;
			struct ast_connector connector;
		} astdp;
	} output;

	bool support_wsxga_p; /* 1680x1050 */
	bool support_fullhd; /* 1920x1080 */
	bool support_wuxga; /* 1920x1200 */

	u8 *dp501_fw_addr;
	const struct firmware *dp501_fw;	/* dp501 fw */
};

static inline struct ast_device *to_ast_device(struct drm_device *dev)
{
	return container_of(dev, struct ast_device, base);
}

struct drm_device *ast_device_create(struct pci_dev *pdev,
				     const struct drm_driver *drv,
				     enum ast_chip chip,
				     enum ast_config_mode config_mode,
				     void __iomem *regs,
				     void __iomem *ioregs,
				     bool need_post);

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

static inline u8 __ast_read8(const void __iomem *addr, u32 reg)
{
	return ioread8(addr + reg);
}

static inline u32 __ast_read32(const void __iomem *addr, u32 reg)
{
	return ioread32(addr + reg);
}

static inline void __ast_write8(void __iomem *addr, u32 reg, u8 val)
{
	iowrite8(val, addr + reg);
}

static inline void __ast_write32(void __iomem *addr, u32 reg, u32 val)
{
	iowrite32(val, addr + reg);
}

static inline u8 __ast_read8_i(void __iomem *addr, u32 reg, u8 index)
{
	__ast_write8(addr, reg, index);
	return __ast_read8(addr, reg + 1);
}

static inline u8 __ast_read8_i_masked(void __iomem *addr, u32 reg, u8 index, u8 read_mask)
{
	u8 val = __ast_read8_i(addr, reg, index);

	return val & read_mask;
}

static inline void __ast_write8_i(void __iomem *addr, u32 reg, u8 index, u8 val)
{
	__ast_write8(addr, reg, index);
	__ast_write8(addr, reg + 1, val);
}

static inline void __ast_write8_i_masked(void __iomem *addr, u32 reg, u8 index, u8 preserve_mask,
					 u8 val)
{
	u8 tmp = __ast_read8_i_masked(addr, reg, index, preserve_mask);

	val &= ~preserve_mask;
	__ast_write8_i(addr, reg, index, tmp | val);
}

static inline u32 ast_read32(struct ast_device *ast, u32 reg)
{
	return __ast_read32(ast->regs, reg);
}

static inline void ast_write32(struct ast_device *ast, u32 reg, u32 val)
{
	__ast_write32(ast->regs, reg, val);
}

static inline u8 ast_io_read8(struct ast_device *ast, u32 reg)
{
	return __ast_read8(ast->ioregs, reg);
}

static inline void ast_io_write8(struct ast_device *ast, u32 reg, u8 val)
{
	__ast_write8(ast->ioregs, reg, val);
}

static inline u8 ast_get_index_reg(struct ast_device *ast, u32 base, u8 index)
{
	return __ast_read8_i(ast->ioregs, base, index);
}

static inline u8 ast_get_index_reg_mask(struct ast_device *ast, u32 base, u8 index,
					u8 preserve_mask)
{
	return __ast_read8_i_masked(ast->ioregs, base, index, preserve_mask);
}

static inline void ast_set_index_reg(struct ast_device *ast, u32 base, u8 index, u8 val)
{
	__ast_write8_i(ast->ioregs, base, index, val);
}

static inline void ast_set_index_reg_mask(struct ast_device *ast, u32 base, u8 index,
					  u8 preserve_mask, u8 val)
{
	__ast_write8_i_masked(ast->ioregs, base, index, preserve_mask, val);
}

struct ast_vbios_stdtable {
	u8 misc;
	u8 seq[4];
	u8 crtc[25];
	u8 ar[20];
	u8 gr[9];
};

struct ast_vbios_dclk_info {
	u8 param1;
	u8 param2;
	u8 param3;
};

struct ast_crtc_state {
	struct drm_crtc_state base;

	/* Last known format of primary plane */
	const struct drm_format_info *format;

	const struct ast_vbios_stdtable *std_table;
	const struct ast_vbios_enhtable *vmode;
};

#define to_ast_crtc_state(state) container_of(state, struct ast_crtc_state, base)

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

/* ast_2000.c */
int ast_2000_post(struct ast_device *ast);

/* ast_2100.c */
int ast_2100_post(struct ast_device *ast);

/* ast_2300.c */
int ast_2300_post(struct ast_device *ast);

/* ast_2500.c */
void ast_2500_patch_ahb(void __iomem *regs);
int ast_2500_post(struct ast_device *ast);

/* ast_2600.c */
int ast_2600_post(struct ast_device *ast);

/* ast post */
int ast_post_gpu(struct ast_device *ast);
u32 ast_mindwm(struct ast_device *ast, u32 r);
void ast_moutdwm(struct ast_device *ast, u32 r, u32 v);

int ast_vga_output_init(struct ast_device *ast);
int ast_sil164_output_init(struct ast_device *ast);

/* ast_cursor.c */
long ast_cursor_vram_offset(struct ast_device *ast);
int ast_cursor_plane_init(struct ast_device *ast);

/* ast dp501 */
bool ast_backup_fw(struct ast_device *ast, u8 *addr, u32 size);
void ast_init_3rdtx(struct ast_device *ast);
int ast_dp501_output_init(struct ast_device *ast);

/* aspeed DP */
int ast_dp_launch(struct ast_device *ast);
int ast_astdp_output_init(struct ast_device *ast);

/* ast_mode.c */
int ast_mode_config_init(struct ast_device *ast);
int ast_plane_init(struct drm_device *dev, struct ast_plane *ast_plane,
		   u64 offset, unsigned long size,
		   uint32_t possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, unsigned int format_count,
		   const uint64_t *format_modifiers,
		   enum drm_plane_type type);
void __iomem *ast_plane_vaddr(struct ast_plane *ast);

#endif
