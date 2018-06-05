/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifndef _CIF_ISP10_H
#define _CIF_ISP10_H

#include <linux/platform_device.h>
#include "cif_isp10_pltfrm.h"
#include "cif_isp10_img_src.h"
#include "cif_isp10_isp.h"
#include <linux/platform_data/rk_isp10_platform.h>
#include <media/v4l2-device.h>
#include <media/v4l2-controls_rockchip.h>
#include <media/videobuf2-v4l2.h>

#include <linux/dma-iommu.h>
#include <drm/rockchip_drm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>

/*****************************************************************************/

#if IS_ENABLED(CONFIG_VIDEOBUF2_DMA_CONTIG)
#define CIF_ISP10_MODE_DMA_CONTIG 1
#endif

#if IS_ENABLED(CONFIG_VIDEOBUF2_DMA_SG)
#define CIF_ISP10_MODE_DMA_SG 1
#endif

#if !defined(CIF_ISP10_MODE_DMA_CONTIG) && \
	!defined(CIF_ISP10_MODE_DMA_SG)
#error One of the videobuf buffer modes(COTING/SG) \
	must be selected in the config
#endif

/* Definitions */

#define CONFIG_CIF_ISP_AUTO_UPD_CFG_BUG

#define CIF_ISP10_NUM_INPUTS    10

/* FORMAT */
#define MAX_NB_FORMATS          30

#define CONTRAST_DEF            0x80
#define BRIGHTNESS_DEF          0x0
#define HUE_DEF                 0x0

/*
 *	MIPI CSI2.0
 */
#define CSI2_DT_YUV420_8b       (0x18)
#define CSI2_DT_YUV420_10b      (0x19)
#define CSI2_DT_YUV422_8b       (0x1E)
#define CSI2_DT_YUV422_10b      (0x1F)
#define CSI2_DT_RGB565          (0x22)
#define CSI2_DT_RGB666          (0x23)
#define CSI2_DT_RGB888          (0x24)
#define CSI2_DT_RAW8            (0x2A)
#define CSI2_DT_RAW10           (0x2B)
#define CSI2_DT_RAW12           (0x2C)

enum cif_isp10_img_src_state {
	CIF_ISP10_IMG_SRC_STATE_OFF       = 0,
	CIF_ISP10_IMG_SRC_STATE_SW_STNDBY = 1,
	CIF_ISP10_IMG_SRC_STATE_STREAMING = 2
};

enum cif_isp10_state {
	/* path not yet opened: */
	CIF_ISP10_STATE_DISABLED  = 0,
	/* path opened but not yet configured: */
	CIF_ISP10_STATE_INACTIVE  = 1,
	/* path opened and configured, ready for streaming: */
	CIF_ISP10_STATE_READY     = 2,
	/* path is streaming: */
	CIF_ISP10_STATE_STREAMING = 3
};

enum cif_isp10_pm_state {
	CIF_ISP10_PM_STATE_OFF,
	CIF_ISP10_PM_STATE_SUSPENDED,
	CIF_ISP10_PM_STATE_SW_STNDBY,
	CIF_ISP10_PM_STATE_STREAMING
};

enum cif_isp10_ispstate {
	CIF_ISP10_STATE_IDLE      = 0,
	CIF_ISP10_STATE_RUNNING   = 1,
	CIF_ISP10_STATE_STOPPING  = 2
};

enum cif_isp10_inp {
	CIF_ISP10_INP_CSI     = 0x10000000,
	CIF_ISP10_INP_CPI     = 0x20000000,

	CIF_ISP10_INP_DMA     = 0x30000000, /* DMA -> ISP */
	CIF_ISP10_INP_DMA_IE  = 0x31000000, /* DMA -> IE */
	CIF_ISP10_INP_DMA_SP  = 0x32000000, /* DMA -> SP */
	CIF_ISP10_INP_DMA_MAX = 0x33000000,

	CIF_ISP10_INP_MAX     = 0x7fffffff
};

#define CIF_ISP10_INP_IS_DMA(inp) \
	(((inp) & 0xf0000000) == CIF_ISP10_INP_DMA)
#define CIF_ISP10_INP_IS_MIPI(inp) \
	(((inp) & 0xf0000000) == CIF_ISP10_INP_CSI)
#define CIF_ISP10_INP_IS_DVP(inp) \
	(((inp) & 0xf0000000) == CIF_ISP10_INP_CPI)
#define CIF_ISP10_INP_NEED_ISP(inp) \
	((inp) <  CIF_ISP10_INP_DMA_IE)
#define CIF_ISP10_INP_DMA_CNT() \
	((CIF_ISP10_INP_DMA_MAX -\
	CIF_ISP10_INP_DMA) >> 24)

enum cif_isp10_pinctrl_state {
	CIF_ISP10_PINCTRL_STATE_SLEEP,
	CIF_ISP10_PINCTRL_STATE_INACTIVE,
	CIF_ISP10_PINCTRL_STATE_DEFAULT,
	CIF_ISP10_PINCTRL_STATE_ACTIVE
};

enum cif_isp10_flash_mode {
	CIF_ISP10_FLASH_MODE_OFF,
	CIF_ISP10_FLASH_MODE_FLASH,
	CIF_ISP10_FLASH_MODE_TORCH,
};

enum cif_isp10_cid {
	CIF_ISP10_CID_FLASH_MODE                  = 0,
	CIF_ISP10_CID_EXPOSURE_TIME               = 1,
	CIF_ISP10_CID_ANALOG_GAIN                 = 2,
	CIF_ISP10_CID_WB_TEMPERATURE              = 3,
	CIF_ISP10_CID_BLACK_LEVEL                 = 4,
	CIF_ISP10_CID_AUTO_GAIN                   = 5,
	CIF_ISP10_CID_AUTO_EXPOSURE               = 6,
	CIF_ISP10_CID_AUTO_WHITE_BALANCE          = 7,
	CIF_ISP10_CID_FOCUS_ABSOLUTE              = 8,
	CIF_ISP10_CID_AUTO_N_PRESET_WHITE_BALANCE = 9,
	CIF_ISP10_CID_SCENE_MODE                  = 10,
	CIF_ISP10_CID_SUPER_IMPOSE                = 11,
	CIF_ISP10_CID_JPEG_QUALITY                = 12,
	CIF_ISP10_CID_IMAGE_EFFECT                = 13,
	CIF_ISP10_CID_HFLIP                       = 14,
	CIF_ISP10_CID_VFLIP                       = 15,
	CIF_ISP10_CID_AUTO_FPS                    = 16,
	CIF_ISP10_CID_VBLANKING                   = 17,
	CIF_ISP10_CID_ISO_SENSITIVITY             = 18,
	CIF_ISP10_CID_MIN_BUFFER_FOR_CAPTURE      = 19,

};

/* correspond to bit field values */
enum cif_isp10_image_effect {
	CIF_ISP10_IE_BW       = 0,
	CIF_ISP10_IE_NEGATIVE = 1,
	CIF_ISP10_IE_SEPIA    = 2,
	CIF_ISP10_IE_C_SEL    = 3,
	CIF_ISP10_IE_EMBOSS   = 4,
	CIF_ISP10_IE_SKETCH   = 5,
	CIF_ISP10_IE_NONE /* not a bit field value */
};

#define CIF_ISP10_PIX_FMT_MASK                  0xf0000000
#define CIF_ISP10_PIX_FMT_MASK_BPP              0x0003f000
#define CIF_ISP10_PIX_FMT_YUV_MASK_CPLANES      0x00000003
#define CIF_ISP10_PIX_FMT_YUV_MASK_UVSWAP       0x00000004
#define CIF_ISP10_PIX_FMT_YUV_MASK_YCSWAP       0x00000008
#define CIF_ISP10_PIX_FMT_YUV_MASK_X            0x00000f00
#define CIF_ISP10_PIX_FMT_YUV_MASK_Y            0x000000f0
#define CIF_ISP10_PIX_FMT_RGB_MASK_PAT          0x000000f0
#define CIF_ISP10_PIX_FMT_BAYER_MASK_PAT        0x000000f0
#define CIF_ISP10_PIX_FMT_GET_BPP(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_MASK_BPP) >> 12)
#define cif_isp10_pix_fmt_set_bpp(pix_fmt, bpp) \
	{ \
		pix_fmt = (((pix_fmt) & ~CIF_ISP10_PIX_FMT_MASK_BPP) | \
			(((bpp) << 12) & CIF_ISP10_PIX_FMT_MASK_BPP)); \
	}

#define CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(pix_fmt) \
	((pix_fmt) & CIF_ISP10_PIX_FMT_YUV_MASK_CPLANES)
#define CIF_ISP10_PIX_FMT_YUV_IS_YC_SWAPPED(pix_fmt) \
	((pix_fmt) & CIF_ISP10_PIX_FMT_YUV_MASK_YCSWAP)
#define CIF_ISP10_PIX_FMT_YUV_IS_UV_SWAPPED(pix_fmt) \
		((pix_fmt) & CIF_ISP10_PIX_FMT_YUV_MASK_UVSWAP)
#define CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_YUV_MASK_X) >> 8)
#define CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_YUV_MASK_Y) >> 4)
#define cif_isp10_pix_fmt_set_y_subs(pix_fmt, y_subs) \
	{ \
		pix_fmt = (((pix_fmt) & ~CIF_ISP10_PIX_FMT_YUV_MASK_Y) | \
			((y_subs << 4) & CIF_ISP10_PIX_FMT_YUV_MASK_Y)); \
	}
#define cif_isp10_pix_fmt_set_x_subs(pix_fmt, x_subs) \
	{ \
		pix_fmt = (((pix_fmt) & ~CIF_ISP10_PIX_FMT_YUV_MASK_X) | \
			(((x_subs) << 8) & CIF_ISP10_PIX_FMT_YUV_MASK_X)); \
	}
#define cif_isp10_pix_fmt_set_yc_swapped(pix_fmt, yc_swapped) \
	{ \
		pix_fmt = (((pix_fmt) & ~CIF_ISP10_PIX_FMT_YUV_MASK_YCSWAP) | \
			(((yc_swapped) << 3) & \
			CIF_ISP10_PIX_FMT_YUV_MASK_YCSWAP)); \
	}

#define CIF_ISP10_PIX_FMT_BAYER_PAT_IS_BGGR(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_BAYER_MASK_PAT) == 0x0)
#define CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GBRG(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_BAYER_MASK_PAT) == 0x10)
#define CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GRBG(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_BAYER_MASK_PAT) == 0x20)
#define CIF_ISP10_PIX_FMT_BAYER_PAT_IS_RGGB(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_BAYER_MASK_PAT) == 0x30)

#define CIF_ISP10_PIX_FMT_IS_YUV(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_MASK) == 0x10000000)
#define CIF_ISP10_PIX_FMT_IS_RGB(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_MASK) == 0x20000000)
#define CIF_ISP10_PIX_FMT_IS_RAW_BAYER(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_MASK) == 0x30000000)
#define CIF_ISP10_PIX_FMT_IS_JPEG(pix_fmt) \
	(((pix_fmt) & CIF_ISP10_PIX_FMT_MASK) == 0x40000000)

#define CIF_ISP10_PIX_FMT_IS_INTERLEAVED(pix_fmt) \
	(!CIF_ISP10_PIX_FMT_IS_YUV(pix_fmt) ||\
	!CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(pix_fmt))

enum cif_isp10_pix_fmt {
	/* YUV */
	CIF_YUV400			= 0x10008000,
	CIF_YVU400			= 0x10008004,
	CIF_Y10				= 0x1000a000,

	CIF_YUV420I			= 0x1000c220,
	CIF_YUV420SP			= 0x1000c221,	/* NV12 */
	CIF_YUV420P			= 0x1000c222,
	CIF_YVU420I			= 0x1000c224,
	CIF_YVU420SP			= 0x1000c225,	/* NV21 */
	CIF_YVU420P			= 0x1000c226,	/* YV12 */

	CIF_YUV422I			= 0x10010240,
	CIF_YUV422SP			= 0x10010241,
	CIF_YUV422P			= 0x10010242,
	CIF_YVU422I			= 0x10010244,
	CIF_YVU422SP			= 0x10010245,
	CIF_YVU422P			= 0x10010246,

	CIF_YUV444I			= 0x10018440,
	CIF_YUV444SP			= 0x10018441,
	CIF_YUV444P			= 0x10018442,
	CIF_YVU444I			= 0x10018444,
	CIF_YVU444SP			= 0x10018445,
	CIF_YVU444P			= 0x10018446,

	CIF_UYV400			= 0x10008008,

	CIF_UYV420I			= 0x1000c228,
	CIF_UYV420SP			= 0x1000c229,
	CIF_UYV420P			= 0x1000c22a,
	CIF_VYU420I			= 0x1000c22c,
	CIF_VYU420SP			= 0x1000c22d,
	CIF_VYU420P			= 0x1000c22e,

	CIF_UYV422I			= 0x10010248,
	CIF_UYV422SP			= 0x10010249,
	CIF_UYV422P			= 0x1001024a,
	CIF_VYU422I			= 0x1001024c,
	CIF_VYU422SP			= 0x1001024d,
	CIF_VYU422P			= 0x1001024e,

	CIF_UYV444I			= 0x10018448,
	CIF_UYV444SP			= 0x10018449,
	CIF_UYV444P			= 0x1001844a,
	CIF_VYU444I			= 0x1001844c,
	CIF_VYU444SP			= 0x1001844d,
	CIF_VYU444P			= 0x1001844e,

	/* RGB */
	CIF_RGB565			= 0x20010000,
	CIF_RGB666			= 0x20012000,
	CIF_RGB888			= 0x20020000,

	/* RAW Bayer */
	CIF_BAYER_SBGGR8		= 0x30008000,
	CIF_BAYER_SGBRG8		= 0x30008010,
	CIF_BAYER_SGRBG8		= 0x30008020,
	CIF_BAYER_SRGGB8		= 0x30008030,

	CIF_BAYER_SBGGR10		= 0x3000a000,
	CIF_BAYER_SGBRG10		= 0x3000a010,
	CIF_BAYER_SGRBG10		= 0x3000a020,
	CIF_BAYER_SRGGB10		= 0x3000a030,

	CIF_BAYER_SBGGR12		= 0x3000c000,
	CIF_BAYER_SGBRG12		= 0x3000c010,
	CIF_BAYER_SGRBG12		= 0x3000c020,
	CIF_BAYER_SRGGB12		= 0x3000c030,

	/* JPEG */
	CIF_JPEG			= 0x40008000,

	/* Data */
	CIF_DATA			= 0x70000000,

	CIF_UNKNOWN_FORMAT		= 0x80000000
};

enum cif_isp10_stream_id {
	CIF_ISP10_STREAM_SP	= 0x1,
	CIF_ISP10_STREAM_MP	= 0x2,
	CIF_ISP10_STREAM_DMA	= 0x4,
	CIF_ISP10_STREAM_ISP	= 0x8
};

#define CIF_ISP10_ALL_STREAMS \
	(CIF_ISP10_STREAM_SP | \
	CIF_ISP10_STREAM_MP | \
	CIF_ISP10_STREAM_DMA)

enum cif_isp10_buff_fmt {
	/* values correspond to bitfield values */
	CIF_ISP10_BUFF_FMT_PLANAR 	= 0,
	CIF_ISP10_BUFF_FMT_SEMIPLANAR 	= 1,
	CIF_ISP10_BUFF_FMT_INTERLEAVED 	= 2,

	CIF_ISP10_BUFF_FMT_RAW8 	= 0,
	CIF_ISP10_BUFF_FMT_RAW12 	= 2
};

enum cif_isp10_jpeg_header {
	CIF_ISP10_JPEG_HEADER_JFIF,
	CIF_ISP10_JPEG_HEADER_NONE
};

struct cif_isp10_csi_config {
	u32 vc;
	u32 nb_lanes;
	u32 bit_rate;
	/* really used csi */
	u32 used_csi; /* xuhf@rock-chips.com: v1.0.4 */
};

struct cif_isp10_paraport_config {
	u32 cif_vsync;
	u32 cif_hsync;
	u32 cif_pclk;
	/* really used csi */
	u32 used_csi; /* xuhf@rock-chips.com: v1.0.4 */
};

struct cif_isp10_frm_intrvl {
	u32 numerator;
	u32 denominator;
};

struct cif_isp10_frm_fmt {
	u32 width;
	u32 height;
	u32 stride;
	u32 std_id;
	enum cif_isp10_pix_fmt pix_fmt;
	enum cif_isp10_pix_fmt_quantization quantization;
	struct v4l2_rect defrect;
};

struct cif_isp10_strm_fmt {
	struct cif_isp10_frm_fmt frm_fmt;
	struct cif_isp10_frm_intrvl frm_intrvl;
};

struct cif_isp10_strm_fmt_desc {
	bool discrete_frmsize;
	struct {
		u32 width;
		u32 height;
	} min_frmsize;
	struct {
		u32 width;
		u32 height;
	} max_frmsize;
	enum cif_isp10_pix_fmt pix_fmt;
	bool discrete_intrvl;
	struct cif_isp10_frm_intrvl min_intrvl;
	struct cif_isp10_frm_intrvl max_intrvl;
	struct v4l2_rect defrect;
	u32 std_id;
};

struct cif_isp10_rsz_config {
	struct cif_isp10_frm_fmt *input;
	struct cif_isp10_frm_fmt output;
	bool ycflt_adjust;
	bool ism_adjust;
};

struct cif_isp10_dcrop_config {
	unsigned int h_offs;
	unsigned int v_offs;
	unsigned int h_size;
	unsigned int v_size;
};

struct cif_isp10_sp_config {
	struct cif_isp10_rsz_config rsz_config;
	struct cif_isp10_dcrop_config dcrop;
};

struct cif_isp10_mp_config {
	struct cif_isp10_rsz_config rsz_config;
	struct cif_isp10_dcrop_config dcrop;
};

struct cif_isp10_mi_path_config {
	struct cif_isp10_frm_fmt *input;
	struct cif_isp10_frm_fmt output;
	u32 llength;
	u32 curr_buff_addr;
	u32 next_buff_addr;
	u32 cb_offs;
	u32 cr_offs;
	u32 y_size;
	u32 cb_size;
	u32 cr_size;
	bool busy;

	/* FOR BT655: 0 = ODD, 1 = EVEN */
	bool field_flag;
	/* for interlace offset */
	u32 vir_len_offset;
};

struct cif_isp10_zoom_buffer_info {
	u32 width;
	u32 height;
	unsigned long  buff_addr;
	u32 flags;
};

struct cif_isp10_mi_config {
	bool raw_enable;
	u32 async_updt;
	struct cif_isp10_mi_path_config mp;
	struct cif_isp10_mi_path_config sp;
	struct cif_isp10_mi_path_config dma;
};

struct cif_isp10_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	unsigned long int size;
};

struct cif_isp10_metadata_s {
	unsigned int cnt;
	unsigned int vmas;
	spinlock_t spinlock;
	unsigned char *d;
};

struct cif_isp10_stream {
	enum cif_isp10_stream_id id;
	enum cif_isp10_state state;
	enum cif_isp10_state saved_state;
	struct list_head buf_queue;
	struct cif_isp10_buffer *curr_buf;
	struct cif_isp10_buffer *next_buf;
	bool updt_cfg;
	bool stall;
	bool stop;
	CIF_ISP10_PLTFRM_EVENT done;
	struct cif_isp10_metadata_s metadata;
};

struct cif_isp10_jpeg_config {
	bool enable;
	bool busy;
	u32 ratio;
	struct cif_isp10_frm_fmt *input;
	enum cif_isp10_jpeg_header header;
};

struct cif_isp10_ie_config {
	enum cif_isp10_image_effect effect;
};

/* IS */
struct cif_isp10_ism_params {
	unsigned int ctrl;
	unsigned int recenter;
	unsigned int h_offs;
	unsigned int v_offs;
	unsigned int h_size;
	unsigned int v_size;
	unsigned int max_dx;
	unsigned int max_dy;
	unsigned int displace;
};

struct cif_isp10_ism_config {
	bool ism_en;
	struct cif_isp10_ism_params ism_params;
	bool ism_update_needed;
};

struct cif_isp10_isp_config {
	bool si_enable;
	struct cif_isp10_ie_config ie_config;
	struct cif_isp10_ism_config ism_config;
	struct cif_isp10_frm_fmt *input;
	struct cif_isp10_frm_fmt output;
};

struct cif_isp10_config {
	CIF_ISP10_PLTFRM_MEM_IO_ADDR base_addr;
	enum cif_isp10_flash_mode flash_mode;
	enum cif_isp10_inp input_sel;
	struct cif_isp10_jpeg_config jpeg_config;
	struct cif_isp10_mi_config mi_config;
	struct cif_isp10_sp_config sp_config;
	struct cif_isp10_mp_config mp_config;
	struct cif_isp10_strm_fmt img_src_output;
	struct cif_isp10_isp_config isp_config;
	struct pltfrm_cam_itf cam_itf;
	bool out_of_buffer_stall;
};

struct cif_isp10_mi_state {
	unsigned long flags;
	unsigned int isp_ctrl;
	unsigned int y_base_ad;
	unsigned int y_size;
	unsigned int cb_base_ad;
	unsigned int cb_size;
	unsigned int cr_base_ad;
	unsigned int cr_size;
};

struct cif_isp10_img_src_exp {
	struct list_head list;
	struct cif_isp10_img_src_ext_ctrl exp;
};

struct cif_isp10_img_src_data {
	unsigned int v_frame_id;
	struct isp_supplemental_sensor_mode_data data;
};

struct cif_isp10_img_src_exps {
	spinlock_t lock;	/* protect list */
	struct list_head list;

	struct mutex mutex;	/* protect frm_exp */
	struct cif_isp10_img_src_data data[2];
	unsigned char exp_valid_frms[2];
};

enum cif_isp10_isp_vs_cmd {
	CIF_ISP10_VS_EXIT = 0,
	CIF_ISP10_VS_EXP = 1
};

struct cif_isp10_isp_vs_work {
	struct work_struct work;
	struct cif_isp10_device *dev;
	enum cif_isp10_isp_vs_cmd cmd;
	void *param;
};

struct cif_isp10_fmt {
	char *name;
	u32 fourcc;
	int flags;
	int depth;
	unsigned char rotation;
	unsigned char overlay;
};

#ifdef CIF_ISP10_MODE_DMA_SG
struct cif_isp10_iommu {
	int client_fd;
	int map_fd;
	unsigned long linear_addr;
	unsigned long len;
};

struct cif_isp10_dma_buf {
	struct dma_buf *dma_buffer;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int fd;
};
#endif

struct cif_isp10_device {
	unsigned int dev_id;
	CIF_ISP10_PLTFRM_DEVICE dev;
	struct v4l2_device v4l2_dev;
	enum cif_isp10_pm_state pm_state;
	enum cif_isp10_img_src_state img_src_state;
	enum cif_isp10_ispstate isp_state;

	spinlock_t vbq_lock;	/* spinlock for videobuf queues */
	spinlock_t vbreq_lock;	/* spinlock for videobuf requeues */
	spinlock_t iowrite32_verify_lock;
	spinlock_t isp_state_lock;

	wait_queue_head_t isp_stop_wait;	/* wait while isp stop */
	unsigned int isp_stop_flags;

	struct cif_isp10_img_src *img_src;
	struct cif_isp10_img_src *img_src_array[CIF_ISP10_NUM_INPUTS];
	unsigned int img_src_cnt;
	struct vb2_alloc_ctx *alloc_ctx;

#ifdef CIF_ISP10_MODE_DMA_SG
	struct iommu_domain *domain;
	struct cif_isp10_dma_buf dma_buffer[VB2_MAX_FRAME];
	int dma_buf_cnt;
#endif
	struct cif_isp10_img_src_exps img_src_exps;

	struct cif_isp10_config config;
	struct cif_isp10_isp_dev isp_dev;
	struct cif_isp10_stream sp_stream;
	struct cif_isp10_stream mp_stream;
	struct cif_isp10_stream dma_stream;

	struct workqueue_struct *vs_wq;
	void (*sof_event)(struct cif_isp10_device *dev, __u32 frame_sequence);
	/*
	 * requeue_bufs() is used to clean and rebuild the local buffer
	 * lists xx_stream.buf_queue. This is used e.g. in the CAPTURE use
	 * case where we start MP and SP separately and needs to shortly
	 * stop and start SP when start MP
	 */
	void (*requeue_bufs)(struct cif_isp10_device *dev,
				enum cif_isp10_stream_id stream_id);
	bool   b_isp_frame_in;
	bool   b_mi_frame_end;
	int   otf_zsl_mode;
	struct flash_timeinfo_s flash_t;

	struct pltfrm_soc_cfg soc_cfg;
	void *nodes;

};

struct cif_isp10_fmt *get_cif_isp10_output_format(int index);
int get_cif_isp10_output_format_size(void);

struct v4l2_fmtdesc *get_cif_isp10_output_format_desc(int index);
int get_cif_isp10_output_format_desc_size(void);

/* Clean code starts from here */

static inline
struct cif_isp10_stream *to_stream_by_id(struct cif_isp10_device *dev,
					 enum cif_isp10_stream_id id)
{
	if (WARN_ON(id != CIF_ISP10_STREAM_MP &&
		id != CIF_ISP10_STREAM_SP &&
		id != CIF_ISP10_STREAM_DMA &&
		id != CIF_ISP10_STREAM_ISP))
		return &dev->sp_stream;

	switch (id) {
	case CIF_ISP10_STREAM_MP:
		return &dev->mp_stream;
	case CIF_ISP10_STREAM_SP:
		return &dev->sp_stream;
	case CIF_ISP10_STREAM_DMA:
		return &dev->dma_stream;
	case CIF_ISP10_STREAM_ISP:
		return NULL;
	}
	return NULL;
}

struct cif_isp10_device *cif_isp10_create(
	CIF_ISP10_PLTFRM_DEVICE pdev,
	void (*sof_event)(struct cif_isp10_device *dev, __u32 frame_sequence),
	void (*requeue_bufs)(struct cif_isp10_device *dev,
				enum cif_isp10_stream_id stream_id),
	struct pltfrm_soc_cfg *soc_cfg);

void cif_isp10_destroy(
	struct cif_isp10_device *dev);

int cif_isp10_init(
	struct cif_isp10_device *dev,
	u32 stream_ids);

int cif_isp10_release(
	struct cif_isp10_device *dev,
	int stream_ids);

int cif_isp10_streamon(
	struct cif_isp10_device *dev,
	u32 stream_ids);

int cif_isp10_streamoff(
	struct cif_isp10_device *dev,
	u32 stream_ids);

int cif_isp10_g_input(
	struct cif_isp10_device *dev,
	enum cif_isp10_inp *inp);

int cif_isp10_s_input(
	struct cif_isp10_device *dev,
	enum cif_isp10_inp inp);

int cif_isp10_s_fmt(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	struct cif_isp10_strm_fmt *strm_fmt,
	u32 stride);

int cif_isp10_resume(
	struct cif_isp10_device *dev);

int cif_isp10_suspend(
	struct cif_isp10_device *dev);

int cif_isp10_qbuf(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream,
	struct cif_isp10_buffer *buf);

int cif_isp10_reqbufs(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id strm,
	struct v4l2_requestbuffers *req);
int cif_isp10_mmap(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	struct vm_area_struct *vma);

int cif_isp10_get_target_frm_size(
	struct cif_isp10_device *dev,
	u32 *target_width,
	u32 *target_height);

int cif_isp10_calc_isp_cropping(
	struct cif_isp10_device *dev,
	u32 *width,
	u32 *height,
	u32 *h_offs,
	u32 *v_offs);

const char *cif_isp10_g_input_name(
	struct cif_isp10_device *dev,
	enum cif_isp10_inp inp);

int cif_isp10_calc_min_out_buff_size(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	u32 *size,
	bool payload);

int cif_isp10_s_ctrl(
	struct cif_isp10_device *dev,
	const enum cif_isp10_cid id,
	int val);

int cif_isp10_s_vb_metadata(
	struct cif_isp10_device *dev,
	struct cif_isp10_isp_readout_work *readout_work);

int cif_isp10_s_exp(
	struct cif_isp10_device *dev,
	struct cif_isp10_img_src_ext_ctrl *exp_ctrl);

int cif_isp10_s_vcm(
	struct cif_isp10_device *dev,
	unsigned int id,
	int val);

void cif_isp10_sensor_mode_data_sync(
	struct cif_isp10_device *dev,
	unsigned int frame_id,
	struct isp_supplemental_sensor_mode_data *data);
#endif
