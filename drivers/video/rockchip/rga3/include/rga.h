/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RGA_DRIVER_H_
#define _RGA_DRIVER_H_

#include <linux/mutex.h>
#include <linux/scatterlist.h>

/* Use 'r' as magic number */
#define RGA_IOC_MAGIC		'r'
#define RGA_IOW(nr, type)	_IOW(RGA_IOC_MAGIC, nr, type)
#define RGA_IOR(nr, type)	_IOR(RGA_IOC_MAGIC, nr, type)
#define RGA_IOWR(nr, type)	_IOWR(RGA_IOC_MAGIC, nr, type)

#define RGA_IOC_GET_DRVIER_VERSION	RGA_IOR(0x1, struct rga_version_t)
#define RGA_IOC_GET_HW_VERSION		RGA_IOR(0x2, struct rga_hw_versions_t)
#define RGA_IOC_IMPORT_BUFFER		RGA_IOWR(0x3, struct rga_buffer_pool)
#define RGA_IOC_RELEASE_BUFFER		RGA_IOW(0x4, struct rga_buffer_pool)
#define RGA_IOC_REQUEST_CREATE		RGA_IOR(0x5, uint32_t)
#define RGA_IOC_REQUEST_SUBMIT		RGA_IOWR(0x6, struct rga_user_request)
#define RGA_IOC_REQUEST_CONFIG		RGA_IOWR(0x7, struct rga_user_request)
#define RGA_IOC_REQUEST_CANCEL		RGA_IOWR(0x8, uint32_t)

#define RGA_BLIT_SYNC			0x5017
#define RGA_BLIT_ASYNC			0x5018
#define RGA_FLUSH			0x5019
#define RGA_GET_RESULT			0x501a
#define RGA_GET_VERSION			0x501b
#define RGA_CACHE_FLUSH			0x501c

#define RGA2_GET_VERSION		0x601b
#define RGA_IMPORT_DMA			0x601d
#define RGA_RELEASE_DMA			0x601e

#define RGA_TASK_NUM_MAX		50

#define RGA_OUT_OF_RESOURCES		-10
#define RGA_MALLOC_ERROR		-11

#define SCALE_DOWN_LARGE		1
#define SCALE_UP_LARGE			1

#define RGA_BUFFER_POOL_SIZE_MAX 40

#define RGA3_MAJOR_VERSION_MASK	 (0xF0000000)
#define RGA3_MINOR_VERSION_MASK	 (0x0FF00000)
#define RGA3_SVN_VERSION_MASK	 (0x000FFFFF)

#define RGA2_MAJOR_VERSION_MASK	 (0xFF000000)
#define RGA2_MINOR_VERSION_MASK	 (0x00F00000)
#define RGA2_SVN_VERSION_MASK	 (0x000FFFFF)

#define RGA_MODE_ROTATE_0	 (1<<0)
#define RGA_MODE_ROTATE_90	 (1<<1)
#define RGA_MODE_ROTATE_180	 (1<<2)
#define RGA_MODE_ROTATE_270	 (1<<3)
#define RGA_MODE_X_MIRROR	 (1<<4)
#define RGA_MODE_Y_MIRROR	 (1<<5)

#define RGA_MODE_CSC_BT601L	 (1<<0)
#define RGA_MODE_CSC_BT601F	 (1<<1)
#define RGA_MODE_CSC_BT709	 (1<<2)
#define RGA_MODE_CSC_BT2020	 (1<<3)

#define RGA_MODE_ROTATE_MASK (\
		RGA_MODE_ROTATE_0 | \
		RGA_MODE_ROTATE_90 | \
		RGA_MODE_ROTATE_180 | \
		RGA_MODE_ROTATE_270 | \
		RGA_MODE_X_MIRROR | \
		RGA_MODE_Y_MIRROR)

enum rga_memory_type {
	RGA_DMA_BUFFER = 0,
	RGA_VIRTUAL_ADDRESS,
	RGA_PHYSICAL_ADDRESS,
	RGA_DMA_BUFFER_PTR,
};

enum rga_scale_up_mode {
	RGA_SCALE_UP_NONE	= 0x0,
	RGA_SCALE_UP_BIC	= 0x1,
};

enum rga_scale_down_mode {
	RGA_SCALE_DOWN_NONE	= 0x0,
	RGA_SCALE_DOWN_AVG	= 0x1,
};

/* RGA process mode enum */
enum {
	BITBLT_MODE			= 0x0,
	COLOR_PALETTE_MODE		= 0x1,
	COLOR_FILL_MODE			= 0x2,
	/* used by rga2 */
	UPDATE_PALETTE_TABLE_MODE	= 0x6,
	UPDATE_PATTEN_BUF_MODE		= 0x7,
}; /*render mode*/

/* RGA rd_mode */
enum {
	RGA_RASTER_MODE			 = 0x1 << 0,
	RGA_FBC_MODE			 = 0x1 << 1,
	RGA_TILE_MODE			 = 0x1 << 2,
};

enum {
	RGA_10BIT_COMPACT		= 0x0,
	RGA_10BIT_INCOMPACT		= 0x1,
};

enum {
	RGA_CONTEXT_NONE		= 0x0,
	RGA_CONTEXT_SRC_FIX_ENABLE	= 0x1 << 0,
	RGA_CONTEXT_SRC_CACHE_INFO	= 0x1 << 1,
	RGA_CONTEXT_SRC_MASK		= RGA_CONTEXT_SRC_FIX_ENABLE |
					  RGA_CONTEXT_SRC_CACHE_INFO,
	RGA_CONTEXT_PAT_FIX_ENABLE	= 0x1 << 2,
	RGA_CONTEXT_PAT_CACHE_INFO	= 0x1 << 3,
	RGA_CONTEXT_PAT_MASK		= RGA_CONTEXT_PAT_FIX_ENABLE |
					  RGA_CONTEXT_PAT_CACHE_INFO,
	RGA_CONTEXT_DST_FIX_ENABLE	= 0x1 << 4,
	RGA_CONTEXT_DST_CACHE_INFO	= 0x1 << 5,
	RGA_CONTEXT_DST_MASK		= RGA_CONTEXT_DST_FIX_ENABLE |
					  RGA_CONTEXT_DST_CACHE_INFO,
};

/* RGA feature */
enum {
	RGA_COLOR_FILL			= 0x1 << 0,
	RGA_COLOR_PALETTE		= 0x1 << 1,
	RGA_COLOR_KEY			= 0x1 << 2,
	RGA_ROP_CALCULATE		= 0x1 << 3,
	RGA_NN_QUANTIZE			= 0x1 << 4,
	RGA_OSD_BLEND			= 0x1 << 5,
	RGA_DITHER			= 0x1 << 6,
	RGA_MOSAIC			= 0x1 << 7,
	RGA_YIN_YOUT			= 0x1 << 8,
	RGA_YUV_HDS			= 0x1 << 9,
	RGA_YUV_VDS			= 0x1 << 10,
	RGA_OSD				= 0x1 << 11,
	RGA_PRE_INTR			= 0x1 << 12,
};

enum rga_surf_format {
	RGA_FORMAT_RGBA_8888		= 0x0,
	RGA_FORMAT_RGBX_8888		= 0x1,
	RGA_FORMAT_RGB_888		= 0x2,
	RGA_FORMAT_BGRA_8888		= 0x3,
	RGA_FORMAT_RGB_565		= 0x4,
	RGA_FORMAT_RGBA_5551		= 0x5,
	RGA_FORMAT_RGBA_4444		= 0x6,
	RGA_FORMAT_BGR_888		= 0x7,

	RGA_FORMAT_YCbCr_422_SP		= 0x8,
	RGA_FORMAT_YCbCr_422_P		= 0x9,
	RGA_FORMAT_YCbCr_420_SP		= 0xa,
	RGA_FORMAT_YCbCr_420_P		= 0xb,

	RGA_FORMAT_YCrCb_422_SP		= 0xc,
	RGA_FORMAT_YCrCb_422_P		= 0xd,
	RGA_FORMAT_YCrCb_420_SP		= 0xe,
	RGA_FORMAT_YCrCb_420_P		= 0xf,

	RGA_FORMAT_BPP1			= 0x10,
	RGA_FORMAT_BPP2			= 0x11,
	RGA_FORMAT_BPP4			= 0x12,
	RGA_FORMAT_BPP8			= 0x13,

	RGA_FORMAT_Y4			= 0x14,
	RGA_FORMAT_YCbCr_400		= 0x15,

	RGA_FORMAT_BGRX_8888		= 0x16,

	RGA_FORMAT_YVYU_422		= 0x18,
	RGA_FORMAT_YVYU_420		= 0x19,
	RGA_FORMAT_VYUY_422		= 0x1a,
	RGA_FORMAT_VYUY_420		= 0x1b,
	RGA_FORMAT_YUYV_422		= 0x1c,
	RGA_FORMAT_YUYV_420		= 0x1d,
	RGA_FORMAT_UYVY_422		= 0x1e,
	RGA_FORMAT_UYVY_420		= 0x1f,

	RGA_FORMAT_YCbCr_420_SP_10B	= 0x20,
	RGA_FORMAT_YCrCb_420_SP_10B	= 0x21,
	RGA_FORMAT_YCbCr_422_SP_10B	= 0x22,
	RGA_FORMAT_YCrCb_422_SP_10B	= 0x23,

	RGA_FORMAT_BGR_565		= 0x24,
	RGA_FORMAT_BGRA_5551		= 0x25,
	RGA_FORMAT_BGRA_4444		= 0x26,

	RGA_FORMAT_ARGB_8888		= 0x28,
	RGA_FORMAT_XRGB_8888		= 0x29,
	RGA_FORMAT_ARGB_5551		= 0x2a,
	RGA_FORMAT_ARGB_4444		= 0x2b,
	RGA_FORMAT_ABGR_8888		= 0x2c,
	RGA_FORMAT_XBGR_8888		= 0x2d,
	RGA_FORMAT_ABGR_5551		= 0x2e,
	RGA_FORMAT_ABGR_4444		= 0x2f,

	RGA_FORMAT_RGBA_2BPP		= 0x30,

	RGA_FORMAT_UNKNOWN		= 0x100,
};

enum rga_alpha_mode {
	RGA_ALPHA_STRAIGHT		= 0,
	RGA_ALPHA_INVERSE		= 1,
};

enum rga_global_blend_mode {
	RGA_ALPHA_GLOBAL		= 0,
	RGA_ALPHA_PER_PIXEL		= 1,
	RGA_ALPHA_PER_PIXEL_GLOBAL	= 2,
};

enum rga_alpha_cal_mode {
	RGA_ALPHA_SATURATION		= 0,
	RGA_ALPHA_NO_SATURATION		= 1,
};

enum rga_factor_mode {
	RGA_ALPHA_ZERO			= 0,
	RGA_ALPHA_ONE			= 1,
	/*
	 *   When used as a factor for the SRC channel, it indicates
	 * the use of the DST channel's alpha value, and vice versa.
	 */
	RGA_ALPHA_OPPOSITE		= 2,
	RGA_ALPHA_OPPOSITE_INVERSE	= 3,
	RGA_ALPHA_OWN			= 4,
};

enum rga_color_mode {
	RGA_ALPHA_PRE_MULTIPLIED	= 0,
	RGA_ALPHA_NO_PRE_MULTIPLIED	= 1,
};

enum rga_alpha_blend_mode {
	RGA_ALPHA_NONE			= 0,
	RGA_ALPHA_BLEND_SRC,
	RGA_ALPHA_BLEND_DST,
	RGA_ALPHA_BLEND_SRC_OVER,
	RGA_ALPHA_BLEND_DST_OVER,
	RGA_ALPHA_BLEND_SRC_IN,
	RGA_ALPHA_BLEND_DST_IN,
	RGA_ALPHA_BLEND_SRC_OUT,
	RGA_ALPHA_BLEND_DST_OUT,
	RGA_ALPHA_BLEND_SRC_ATOP,
	RGA_ALPHA_BLEND_DST_ATOP,
	RGA_ALPHA_BLEND_XOR,
	RGA_ALPHA_BLEND_CLEAR,
};

#define RGA_SCHED_PRIORITY_DEFAULT 0
#define RGA_SCHED_PRIORITY_MAX 6

#define RGA_VERSION_SIZE	16
#define RGA_HW_SIZE		5

struct rga_version_t {
	uint32_t major;
	uint32_t minor;
	uint32_t revision;
	uint8_t str[RGA_VERSION_SIZE];
};

struct rga_hw_versions_t {
	struct rga_version_t version[RGA_HW_SIZE];
	uint32_t size;
};

struct rga_memory_parm {
	uint32_t width;
	uint32_t height;
	uint32_t format;

	uint32_t size;
};

struct rga_external_buffer {
	uint64_t memory;
	uint32_t type;

	uint32_t handle;
	struct rga_memory_parm memory_parm;

	uint8_t reserve[252];
};

struct rga_buffer_pool {
	uint64_t buffers_ptr;
	uint32_t size;
};

struct rga_mmu_info_t {
	unsigned long src0_base_addr;
	unsigned long src1_base_addr;
	unsigned long dst_base_addr;
	unsigned long els_base_addr;

	/* [0] mmu enable [1] flush [2] prefetch_en [3] prefetch dir */
	u8 src0_mmu_flag;
	u8 src1_mmu_flag;
	u8 dst_mmu_flag;
	u8 els_mmu_flag;
};

struct rga_color_fill_t {
	int16_t gr_x_a;
	int16_t gr_y_a;
	int16_t gr_x_b;
	int16_t gr_y_b;
	int16_t gr_x_g;
	int16_t gr_y_g;
	int16_t gr_x_r;
	int16_t gr_y_r;
};

/***************************************/
/* porting from rga.h for msg convert */
/***************************************/

struct rga_fading_t {
	uint8_t b;
	uint8_t g;
	uint8_t r;
	uint8_t res;
};

struct rga_mmu_t {
	uint8_t mmu_en;
	uint64_t base_addr;
	/*
	 * [0] mmu enable [1] src_flush [2] dst_flush
	 * [3] CMD_flush [4~5] page size
	 */
	uint32_t mmu_flag;
};

struct rga_rect_t {
	uint16_t xmin;
	/* width - 1 */
	uint16_t xmax;
	uint16_t ymin;
	/* height - 1 */
	uint16_t ymax;
};

struct rga_point_t {
	uint16_t x;
	uint16_t y;
};

struct rga_line_draw_t {
	/* LineDraw_start_point	*/
	struct rga_point_t start_point;
	/* LineDraw_end_point */
	struct rga_point_t end_point;
	/* LineDraw_color */
	uint32_t color;
	/* (enum) LineDrawing mode sel */
	uint32_t flag;
	/* range 1~16 */
	uint32_t line_width;
};

/* color space convert coefficient. */
struct rga_csc_coe {
	int16_t r_v;
	int16_t g_y;
	int16_t b_u;
	int32_t off;
};

struct rga_full_csc {
	uint8_t flag;
	struct rga_csc_coe coe_y;
	struct rga_csc_coe coe_u;
	struct rga_csc_coe coe_v;
};

struct rga_mosaic_info {
	uint8_t enable;
	uint8_t mode;
};

/* MAX(min, (max - channel_value)) */
struct rga_osd_invert_factor {
	uint8_t alpha_max;
	uint8_t alpha_min;
	uint8_t yg_max;
	uint8_t yg_min;
	uint8_t crb_max;
	uint8_t crb_min;
};

struct rga_color {
	union {
		struct {
			uint8_t red;
			uint8_t green;
			uint8_t blue;
			uint8_t alpha;
		};
		uint32_t value;
	};
};

struct rga_osd_bpp2 {
	uint8_t  ac_swap;		// ac swap flag
					// 0: CA
					// 1: AC
	uint8_t  endian_swap;		// rgba2bpp endian swap
					// 0: Big endian
					// 1: Little endian
	struct rga_color color0;
	struct rga_color color1;
};

struct rga_osd_mode_ctrl {
	uint8_t mode;			// OSD cal mode:
					//   0b'1: statistics mode
					//   1b'1: auto inversion overlap mode
	uint8_t direction_mode;		// horizontal or vertical
					//   0: horizontal
					//   1: vertical
	uint8_t width_mode;		// using @fix_width or LUT width
					//   0: fix width
					//   1: LUT width
	uint16_t block_fix_width;	// OSD block fixed width
					//   real width = (fix_width + 1) * 2
	uint8_t block_num;		// OSD block num
	uint16_t flags_index;		// auto invert flags index

	/* invertion config */
	uint8_t color_mode;		// selete color
					//   0: src1 color
					//   1: config data color
	uint8_t invert_flags_mode;	// invert flag selete
					//   0: use RAM flag
					//   1: usr last result
	uint8_t default_color_sel;	// default color mode
					//   0: default is bright
					//   1: default is dark
	uint8_t invert_enable;		// invert channel enable
					//   1 << 0: alpha enable
					//   1 << 1: Y/G disable
					//   1 << 3: C/RB disable
	uint8_t invert_mode;		// invert cal mode
					//   0: normal(max-data)
					//   1: swap
	uint8_t invert_thresh;		// if luma > thresh, osd_flag to be 1
	uint8_t unfix_index;		// OSD width config index
};

struct rga_osd_info {
	uint8_t  enable;

	struct rga_osd_mode_ctrl mode_ctrl;
	struct rga_osd_invert_factor cal_factor;
	struct rga_osd_bpp2 bpp2_info;

	union {
		struct {
			uint32_t last_flags0;
			uint32_t last_flags1;
		};
		uint64_t last_flags;
	};

	union {
		struct {
			uint32_t cur_flags0;
			uint32_t cur_flags1;
		};
		uint64_t cur_flags;
	};
};

struct rga_pre_intr_info {
	uint8_t enable;

	uint8_t read_intr_en;
	uint8_t write_intr_en;
	uint8_t read_hold_en;
	uint32_t read_threshold;
	uint32_t write_start;
	uint32_t write_step;
};

struct rga_win_info_t {
	/* yrgb	mem addr */
	unsigned long yrgb_addr;
	/* cb/cr mem addr */
	unsigned long uv_addr;
	/* cr mem addr */
	unsigned long v_addr;
	/* definition by RK_FORMAT */
	unsigned int format;

	unsigned short src_act_w;
	unsigned short src_act_h;

	unsigned short dst_act_w;
	unsigned short dst_act_h;

	unsigned short x_offset;
	unsigned short y_offset;

	unsigned short vir_w;
	unsigned short vir_h;

	unsigned short y2r_mode;
	unsigned short r2y_mode;

	unsigned short rotate_mode;
	/* RASTER or FBCD or TILE */
	unsigned short rd_mode;

	unsigned short is_10b_compact;
	unsigned short is_10b_endian;

	unsigned short enable;
};

struct rga_img_info_t {
	/* yrgb	mem addr */
	uint64_t yrgb_addr;
	/* cb/cr mem addr */
	uint64_t uv_addr;
	/* cr mem addr */
	uint64_t v_addr;
	/* definition by RK_FORMAT */
	uint32_t format;

	uint16_t act_w;
	uint16_t act_h;
	uint16_t x_offset;
	uint16_t y_offset;

	uint16_t vir_w;
	uint16_t vir_h;

	uint16_t endian_mode;
	/* useless */
	uint16_t alpha_swap;

	/* used by RGA3 */
	uint16_t rotate_mode;
	uint16_t rd_mode;

	uint16_t compact_mode;
	uint16_t is_10b_endian;

	uint16_t enable;
};

struct rga_req {
	/* (enum) process mode sel */
	uint8_t render_mode;

	struct rga_img_info_t src;
	struct rga_img_info_t dst;
	struct rga_img_info_t pat;

	/* rop4 mask addr */
	uint64_t rop_mask_addr;
	/* LUT addr */
	uint64_t LUT_addr;

	/* dst clip window default value is dst_vir */
	/* value from [0, w-1] / [0, h-1]*/
	struct rga_rect_t clip;

	/* dst angle default value 0 16.16 scan from table */
	int32_t sina;
	/* dst angle default value 0 16.16 scan from table */
	int32_t cosa;

	/* alpha rop process flag		 */
	/* ([0] = 1 alpha_rop_enable)	 */
	/* ([1] = 1 rop enable)			 */
	/* ([2] = 1 fading_enable)		 */
	/* ([3] = 1 PD_enable)			 */
	/* ([4] = 1 alpha cal_mode_sel)	 */
	/* ([5] = 1 dither_enable)		 */
	/* ([6] = 1 gradient fill mode sel) */
	/* ([7] = 1 AA_enable)			 */
	uint16_t alpha_rop_flag;

	/* 0 nearst / 1 bilnear / 2 bicubic */
	uint8_t scale_mode;

	/* color key max */
	uint32_t color_key_max;
	/* color key min */
	uint32_t color_key_min;

	/* foreground color */
	uint32_t fg_color;
	/* background color */
	uint32_t bg_color;

	/* color fill use gradient */
	struct rga_color_fill_t gr_color;

	struct rga_line_draw_t line_draw_info;

	struct rga_fading_t fading;

	/* porter duff alpha mode sel */
	uint8_t PD_mode;

	/* global alpha value */
	uint8_t alpha_global_value;

	/* rop2/3/4 code scan from rop code table*/
	uint16_t rop_code;

	/* [2] 0 blur 1 sharp / [1:0] filter_type*/
	uint8_t bsfilter_flag;

	/* (enum) color palette 0/1bpp, 1/2bpp 2/4bpp 3/8bpp*/
	uint8_t palette_mode;

	/* (enum) BT.601 MPEG / BT.601 JPEG / BT.709 */
	uint8_t yuv2rgb_mode;

	/* 0/big endian 1/little endian*/
	uint8_t endian_mode;

	/* (enum) rotate mode */
	/* 0x0,	 no rotate */
	/* 0x1,	 rotate	 */
	/* 0x2,	 x_mirror */
	/* 0x3,	 y_mirror */
	uint8_t rotate_mode;

	/* 0 solid color / 1 pattern color */
	uint8_t color_fill_mode;

	/* mmu information */
	struct rga_mmu_t mmu_info;

	/* ([0~1] alpha mode)			*/
	/* ([2~3] rop mode)			*/
	/* ([4] zero mode en)		 */
	/* ([5] dst alpha mode)	 */
	/* ([6] alpha output mode sel) 0 src / 1 dst*/
	uint8_t alpha_rop_mode;

	uint8_t src_trans_mode;

	uint8_t dither_mode;

	/* full color space convert */
	struct rga_full_csc full_csc;

	int32_t in_fence_fd;
	uint8_t core;
	uint8_t priority;
	int32_t out_fence_fd;

	uint8_t handle_flag;

	/* RGA2 1106 add */
	struct rga_mosaic_info mosaic_info;

	uint8_t uvhds_mode;
	uint8_t uvvds_mode;

	struct rga_osd_info osd_info;

	struct rga_pre_intr_info pre_intr_info;

	uint8_t reservr[59];
};

struct rga_alpha_config {
	bool enable;
	bool fg_pre_multiplied;
	bool bg_pre_multiplied;
	bool fg_pixel_alpha_en;
	bool bg_pixel_alpha_en;
	bool fg_global_alpha_en;
	bool bg_global_alpha_en;
	uint16_t fg_global_alpha_value;
	uint16_t bg_global_alpha_value;
	enum rga_alpha_blend_mode mode;
};

struct rga2_req {
	/* (enum) process mode sel */
	u8 render_mode;

	/* active window */
	struct rga_img_info_t src;
	struct rga_img_info_t src1;
	struct rga_img_info_t dst;
	struct rga_img_info_t pat;

	/* rop4 mask addr */
	unsigned long rop_mask_addr;
	/* LUT addr */
	unsigned long LUT_addr;

	u32 rop_mask_stride;

	/* 0: SRC + DST => DST	 */
	/* 1: SRC + SRC1 => DST	 */
	u8 bitblt_mode;

	/* [1:0] */
	/* 0 degree 0x0				 */
	/* 90 degree 0x1				 */
	/* 180 degree 0x2				 */
	/* 270 degree 0x3				 */
	/* [5:4]						 */
	/* none				0x0		 */
	/* x_mirror			0x1		 */
	/* y_mirror			0x2		 */
	/* x_mirror + y_mirror 0x3		 */
	u8 rotate_mode;

	/* alpha rop process flag		 */
	/* ([0] = 1 alpha_rop_enable)	 */
	/* ([1] = 1 rop enable)			 */
	/* ([2] = 1 fading_enable)		 */
	/* ([3] = 1 alpha cal_mode_sel)	 */
	/* ([4] = 1 src_dither_up_enable) */
	/* ([5] = 1 dst_dither_up_enable) */
	/* ([6] = 1 dither_down_enable)	 */
	/* ([7] = 1 gradient fill mode sel) */
	u16 alpha_rop_flag;

	struct rga_alpha_config alpha_config;

	/* 0 1 2 3 */
	u8 scale_bicu_mode;

	u32 color_key_max;
	u32 color_key_min;

	/* foreground color */
	u32 fg_color;
	/* background color */
	u32 bg_color;

	u8 color_fill_mode;
	/* color fill use gradient */
	struct rga_color_fill_t gr_color;

	/* Fading value */
	u8 fading_alpha_value;
	u8 fading_r_value;
	u8 fading_g_value;
	u8 fading_b_value;

	/* src global alpha value */
	u8 src_a_global_val;
	/* dst global alpha value */
	u8 dst_a_global_val;

	/* rop mode select 0 : rop2 1 : rop3 2 : rop4 */
	u8 rop_mode;
	/* rop2/3/4 code */
	u16 rop_code;

	/* (enum) color palette 0/1bpp, 1/2bpp 2/4bpp 3/8bpp*/
	u8 palette_mode;

	/* (enum) BT.601 MPEG / BT.601 JPEG / BT.709 */
	u8 yuv2rgb_mode;

	u8 full_csc_en;

	/* 0/little endian 1/big endian */
	u8 endian_mode;

	u8 CMD_fin_int_enable;

	/* mmu information */
	struct rga_mmu_info_t mmu_info;

	u8 alpha_zero_key;
	u8 src_trans_mode;

	/* useless */
	u8 alpha_swp;
	u8 dither_mode;

	u8 rgb2yuv_mode;

	/* RGA2 1106 add */
	struct rga_mosaic_info mosaic_info;

	uint8_t yin_yout_en;

	uint8_t uvhds_mode;
	uint8_t uvvds_mode;

	struct rga_osd_info osd_info;
};

struct rga3_req {
	/* (enum) process mode sel */
	u8 render_mode;

	struct rga_win_info_t win0;
	struct rga_win_info_t wr;
	struct rga_win_info_t win1;

	/* rop4 mask addr */
	unsigned long rop_mask_addr;
	unsigned long LUT_addr;

	u32 rop_mask_stride;

	u8 bitblt_mode;
	u8 rotate_mode;

	u16 alpha_rop_flag;

	struct rga_alpha_config alpha_config;

	/* for abb mode presever alpha. */
	bool abb_alpha_pass;

	u8 scale_bicu_mode;

	u32 color_key_max;
	u32 color_key_min;

	u32 fg_color;
	u32 bg_color;

	u8 color_fill_mode;
	struct rga_color_fill_t gr_color;

	u8 fading_alpha_value;
	u8 fading_r_value;
	u8 fading_g_value;
	u8 fading_b_value;

	/* win0 global alpha value		*/
	u8 win0_a_global_val;
	/* win1 global alpha value		*/
	u8 win1_a_global_val;

	u8 rop_mode;
	u16 rop_code;

	u8 palette_mode;

	u8 yuv2rgb_mode;

	u8 endian_mode;

	u8 CMD_fin_int_enable;

	struct rga_mmu_info_t mmu_info;

	u8 alpha_zero_key;
	u8 src_trans_mode;

	u8 alpha_swp;
	u8 dither_mode;

	u8 rgb2yuv_mode;
};

struct rga_video_frame_info {
	uint32_t x_offset;
	uint32_t y_offset;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t vir_w;
	uint32_t vir_h;
	uint32_t rd_mode;
};

struct rga_mpi_job_t {
	struct dma_buf *dma_buf_src0;
	struct dma_buf *dma_buf_src1;
	struct dma_buf *dma_buf_dst;

	struct rga_video_frame_info *src;
	struct rga_video_frame_info *pat;
	struct rga_video_frame_info *dst;
	struct rga_video_frame_info *output;

	int ctx_id;
};

struct rga_user_request {
	uint64_t task_ptr;
	uint32_t task_num;
	uint32_t id;
	uint32_t sync_mode;
	uint32_t release_fence_fd;

	uint32_t mpi_config_flags;

	uint32_t acquire_fence_fd;

	uint8_t reservr[120];
};

int rga_mpi_commit(struct rga_mpi_job_t *mpi_job);

#endif /*_RGA_DRIVER_H_*/
