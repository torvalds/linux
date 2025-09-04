// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>

#include "sti_compositor.h"
#include "sti_drv.h"
#include "sti_hqvdp_lut.h"
#include "sti_plane.h"
#include "sti_vtg.h"

/* Firmware name */
#define HQVDP_FMW_NAME          "hqvdp-stih407.bin"

/* Regs address */
#define HQVDP_DMEM              0x00000000               /* 0x00000000 */
#define HQVDP_PMEM              0x00040000               /* 0x00040000 */
#define HQVDP_RD_PLUG           0x000E0000               /* 0x000E0000 */
#define HQVDP_RD_PLUG_CONTROL   (HQVDP_RD_PLUG + 0x1000) /* 0x000E1000 */
#define HQVDP_RD_PLUG_PAGE_SIZE (HQVDP_RD_PLUG + 0x1004) /* 0x000E1004 */
#define HQVDP_RD_PLUG_MIN_OPC   (HQVDP_RD_PLUG + 0x1008) /* 0x000E1008 */
#define HQVDP_RD_PLUG_MAX_OPC   (HQVDP_RD_PLUG + 0x100C) /* 0x000E100C */
#define HQVDP_RD_PLUG_MAX_CHK   (HQVDP_RD_PLUG + 0x1010) /* 0x000E1010 */
#define HQVDP_RD_PLUG_MAX_MSG   (HQVDP_RD_PLUG + 0x1014) /* 0x000E1014 */
#define HQVDP_RD_PLUG_MIN_SPACE (HQVDP_RD_PLUG + 0x1018) /* 0x000E1018 */
#define HQVDP_WR_PLUG           0x000E2000               /* 0x000E2000 */
#define HQVDP_WR_PLUG_CONTROL   (HQVDP_WR_PLUG + 0x1000) /* 0x000E3000 */
#define HQVDP_WR_PLUG_PAGE_SIZE (HQVDP_WR_PLUG + 0x1004) /* 0x000E3004 */
#define HQVDP_WR_PLUG_MIN_OPC   (HQVDP_WR_PLUG + 0x1008) /* 0x000E3008 */
#define HQVDP_WR_PLUG_MAX_OPC   (HQVDP_WR_PLUG + 0x100C) /* 0x000E300C */
#define HQVDP_WR_PLUG_MAX_CHK   (HQVDP_WR_PLUG + 0x1010) /* 0x000E3010 */
#define HQVDP_WR_PLUG_MAX_MSG   (HQVDP_WR_PLUG + 0x1014) /* 0x000E3014 */
#define HQVDP_WR_PLUG_MIN_SPACE (HQVDP_WR_PLUG + 0x1018) /* 0x000E3018 */
#define HQVDP_MBX               0x000E4000               /* 0x000E4000 */
#define HQVDP_MBX_IRQ_TO_XP70   (HQVDP_MBX + 0x0000)     /* 0x000E4000 */
#define HQVDP_MBX_INFO_HOST     (HQVDP_MBX + 0x0004)     /* 0x000E4004 */
#define HQVDP_MBX_IRQ_TO_HOST   (HQVDP_MBX + 0x0008)     /* 0x000E4008 */
#define HQVDP_MBX_INFO_XP70     (HQVDP_MBX + 0x000C)     /* 0x000E400C */
#define HQVDP_MBX_SW_RESET_CTRL (HQVDP_MBX + 0x0010)     /* 0x000E4010 */
#define HQVDP_MBX_STARTUP_CTRL1 (HQVDP_MBX + 0x0014)     /* 0x000E4014 */
#define HQVDP_MBX_STARTUP_CTRL2 (HQVDP_MBX + 0x0018)     /* 0x000E4018 */
#define HQVDP_MBX_GP_STATUS     (HQVDP_MBX + 0x001C)     /* 0x000E401C */
#define HQVDP_MBX_NEXT_CMD      (HQVDP_MBX + 0x0020)     /* 0x000E4020 */
#define HQVDP_MBX_CURRENT_CMD   (HQVDP_MBX + 0x0024)     /* 0x000E4024 */
#define HQVDP_MBX_SOFT_VSYNC    (HQVDP_MBX + 0x0028)     /* 0x000E4028 */

/* Plugs config */
#define PLUG_CONTROL_ENABLE     0x00000001
#define PLUG_PAGE_SIZE_256      0x00000002
#define PLUG_MIN_OPC_8          0x00000003
#define PLUG_MAX_OPC_64         0x00000006
#define PLUG_MAX_CHK_2X         0x00000001
#define PLUG_MAX_MSG_1X         0x00000000
#define PLUG_MIN_SPACE_1        0x00000000

/* SW reset CTRL */
#define SW_RESET_CTRL_FULL      BIT(0)
#define SW_RESET_CTRL_CORE      BIT(1)

/* Startup ctrl 1 */
#define STARTUP_CTRL1_RST_DONE  BIT(0)
#define STARTUP_CTRL1_AUTH_IDLE BIT(2)

/* Startup ctrl 2 */
#define STARTUP_CTRL2_FETCH_EN  BIT(1)

/* Info xP70 */
#define INFO_XP70_FW_READY      BIT(15)
#define INFO_XP70_FW_PROCESSING BIT(14)
#define INFO_XP70_FW_INITQUEUES BIT(13)

/* SOFT_VSYNC */
#define SOFT_VSYNC_HW           0x00000000
#define SOFT_VSYNC_SW_CMD       0x00000001
#define SOFT_VSYNC_SW_CTRL_IRQ  0x00000003

/* Reset & boot poll config */
#define POLL_MAX_ATTEMPT        50
#define POLL_DELAY_MS           20

#define SCALE_FACTOR            8192
#define SCALE_MAX_FOR_LEG_LUT_F 4096
#define SCALE_MAX_FOR_LEG_LUT_E 4915
#define SCALE_MAX_FOR_LEG_LUT_D 6654
#define SCALE_MAX_FOR_LEG_LUT_C 8192

enum sti_hvsrc_orient {
	HVSRC_HORI,
	HVSRC_VERT
};

/* Command structures */
struct sti_hqvdp_top {
	u32 config;
	u32 mem_format;
	u32 current_luma;
	u32 current_enh_luma;
	u32 current_right_luma;
	u32 current_enh_right_luma;
	u32 current_chroma;
	u32 current_enh_chroma;
	u32 current_right_chroma;
	u32 current_enh_right_chroma;
	u32 output_luma;
	u32 output_chroma;
	u32 luma_src_pitch;
	u32 luma_enh_src_pitch;
	u32 luma_right_src_pitch;
	u32 luma_enh_right_src_pitch;
	u32 chroma_src_pitch;
	u32 chroma_enh_src_pitch;
	u32 chroma_right_src_pitch;
	u32 chroma_enh_right_src_pitch;
	u32 luma_processed_pitch;
	u32 chroma_processed_pitch;
	u32 input_frame_size;
	u32 input_viewport_ori;
	u32 input_viewport_ori_right;
	u32 input_viewport_size;
	u32 left_view_border_width;
	u32 right_view_border_width;
	u32 left_view_3d_offset_width;
	u32 right_view_3d_offset_width;
	u32 side_stripe_color;
	u32 crc_reset_ctrl;
};

/* Configs for interlaced : no IT, no pass thru, 3 fields */
#define TOP_CONFIG_INTER_BTM            0x00000000
#define TOP_CONFIG_INTER_TOP            0x00000002

/* Config for progressive : no IT, no pass thru, 3 fields */
#define TOP_CONFIG_PROGRESSIVE          0x00000001

/* Default MemFormat: in=420_raster_dual out=444_raster;opaque Mem2Tv mode */
#define TOP_MEM_FORMAT_DFLT             0x00018060

/* Min/Max size */
#define MAX_WIDTH                       0x1FFF
#define MAX_HEIGHT                      0x0FFF
#define MIN_WIDTH                       0x0030
#define MIN_HEIGHT                      0x0010

struct sti_hqvdp_vc1re {
	u32 ctrl_prv_csdi;
	u32 ctrl_cur_csdi;
	u32 ctrl_nxt_csdi;
	u32 ctrl_cur_fmd;
	u32 ctrl_nxt_fmd;
};

struct sti_hqvdp_fmd {
	u32 config;
	u32 viewport_ori;
	u32 viewport_size;
	u32 next_next_luma;
	u32 next_next_right_luma;
	u32 next_next_next_luma;
	u32 next_next_next_right_luma;
	u32 threshold_scd;
	u32 threshold_rfd;
	u32 threshold_move;
	u32 threshold_cfd;
};

struct sti_hqvdp_csdi {
	u32 config;
	u32 config2;
	u32 dcdi_config;
	u32 prev_luma;
	u32 prev_enh_luma;
	u32 prev_right_luma;
	u32 prev_enh_right_luma;
	u32 next_luma;
	u32 next_enh_luma;
	u32 next_right_luma;
	u32 next_enh_right_luma;
	u32 prev_chroma;
	u32 prev_enh_chroma;
	u32 prev_right_chroma;
	u32 prev_enh_right_chroma;
	u32 next_chroma;
	u32 next_enh_chroma;
	u32 next_right_chroma;
	u32 next_enh_right_chroma;
	u32 prev_motion;
	u32 prev_right_motion;
	u32 cur_motion;
	u32 cur_right_motion;
	u32 next_motion;
	u32 next_right_motion;
};

/* Config for progressive: by pass */
#define CSDI_CONFIG_PROG                0x00000000
/* Config for directional deinterlacing without motion */
#define CSDI_CONFIG_INTER_DIR           0x00000016
/* Additional configs for fader, blender, motion,... deinterlace algorithms */
#define CSDI_CONFIG2_DFLT               0x000001B3
#define CSDI_DCDI_CONFIG_DFLT           0x00203803

struct sti_hqvdp_hvsrc {
	u32 hor_panoramic_ctrl;
	u32 output_picture_size;
	u32 init_horizontal;
	u32 init_vertical;
	u32 param_ctrl;
	u32 yh_coef[NB_COEF];
	u32 ch_coef[NB_COEF];
	u32 yv_coef[NB_COEF];
	u32 cv_coef[NB_COEF];
	u32 hori_shift;
	u32 vert_shift;
};

/* Default ParamCtrl: all controls enabled */
#define HVSRC_PARAM_CTRL_DFLT           0xFFFFFFFF

struct sti_hqvdp_iqi {
	u32 config;
	u32 demo_wind_size;
	u32 pk_config;
	u32 coeff0_coeff1;
	u32 coeff2_coeff3;
	u32 coeff4;
	u32 pk_lut;
	u32 pk_gain;
	u32 pk_coring_level;
	u32 cti_config;
	u32 le_config;
	u32 le_lut[64];
	u32 con_bri;
	u32 sat_gain;
	u32 pxf_conf;
	u32 default_color;
};

/* Default Config : IQI bypassed */
#define IQI_CONFIG_DFLT                 0x00000001
/* Default Contrast & Brightness gain = 256 */
#define IQI_CON_BRI_DFLT                0x00000100
/* Default Saturation gain = 256 */
#define IQI_SAT_GAIN_DFLT               0x00000100
/* Default PxfConf : P2I bypassed */
#define IQI_PXF_CONF_DFLT               0x00000001

struct sti_hqvdp_top_status {
	u32 processing_time;
	u32 input_y_crc;
	u32 input_uv_crc;
};

struct sti_hqvdp_fmd_status {
	u32 fmd_repeat_move_status;
	u32 fmd_scene_count_status;
	u32 cfd_sum;
	u32 field_sum;
	u32 next_y_fmd_crc;
	u32 next_next_y_fmd_crc;
	u32 next_next_next_y_fmd_crc;
};

struct sti_hqvdp_csdi_status {
	u32 prev_y_csdi_crc;
	u32 cur_y_csdi_crc;
	u32 next_y_csdi_crc;
	u32 prev_uv_csdi_crc;
	u32 cur_uv_csdi_crc;
	u32 next_uv_csdi_crc;
	u32 y_csdi_crc;
	u32 uv_csdi_crc;
	u32 uv_cup_crc;
	u32 mot_csdi_crc;
	u32 mot_cur_csdi_crc;
	u32 mot_prev_csdi_crc;
};

struct sti_hqvdp_hvsrc_status {
	u32 y_hvsrc_crc;
	u32 u_hvsrc_crc;
	u32 v_hvsrc_crc;
};

struct sti_hqvdp_iqi_status {
	u32 pxf_it_status;
	u32 y_iqi_crc;
	u32 u_iqi_crc;
	u32 v_iqi_crc;
};

/* Main commands. We use 2 commands one being processed by the firmware, one
 * ready to be fetched upon next Vsync*/
#define NB_VDP_CMD	2

struct sti_hqvdp_cmd {
	struct sti_hqvdp_top top;
	struct sti_hqvdp_vc1re vc1re;
	struct sti_hqvdp_fmd fmd;
	struct sti_hqvdp_csdi csdi;
	struct sti_hqvdp_hvsrc hvsrc;
	struct sti_hqvdp_iqi iqi;
	struct sti_hqvdp_top_status top_status;
	struct sti_hqvdp_fmd_status fmd_status;
	struct sti_hqvdp_csdi_status csdi_status;
	struct sti_hqvdp_hvsrc_status hvsrc_status;
	struct sti_hqvdp_iqi_status iqi_status;
};

/*
 * STI HQVDP structure
 *
 * @dev:               driver device
 * @drm_dev:           the drm device
 * @regs:              registers
 * @plane:             plane structure for hqvdp it self
 * @clk:               IP clock
 * @clk_pix_main:      pix main clock
 * @reset:             reset control
 * @vtg_nb:            notifier to handle VTG Vsync
 * @btm_field_pending: is there any bottom field (interlaced frame) to display
 * @hqvdp_cmd:         buffer of commands
 * @hqvdp_cmd_paddr:   physical address of hqvdp_cmd
 * @vtg:               vtg for main data path
 * @xp70_initialized:  true if xp70 is already initialized
 * @vtg_registered:    true if registered to VTG
 */
struct sti_hqvdp {
	struct device *dev;
	struct drm_device *drm_dev;
	void __iomem *regs;
	struct sti_plane plane;
	struct clk *clk;
	struct clk *clk_pix_main;
	struct reset_control *reset;
	struct notifier_block vtg_nb;
	bool btm_field_pending;
	void *hqvdp_cmd;
	u32 hqvdp_cmd_paddr;
	struct sti_vtg *vtg;
	bool xp70_initialized;
	bool vtg_registered;
};

#define to_sti_hqvdp(x) container_of(x, struct sti_hqvdp, plane)

static const uint32_t hqvdp_supported_formats[] = {
	DRM_FORMAT_NV12,
};

/**
 * sti_hqvdp_get_free_cmd
 * @hqvdp: hqvdp structure
 *
 * Look for a hqvdp_cmd that is not being used (or about to be used) by the FW.
 *
 * RETURNS:
 * the offset of the command to be used.
 * -1 in error cases
 */
static int sti_hqvdp_get_free_cmd(struct sti_hqvdp *hqvdp)
{
	u32 curr_cmd, next_cmd;
	u32 cmd = hqvdp->hqvdp_cmd_paddr;
	int i;

	curr_cmd = readl(hqvdp->regs + HQVDP_MBX_CURRENT_CMD);
	next_cmd = readl(hqvdp->regs + HQVDP_MBX_NEXT_CMD);

	for (i = 0; i < NB_VDP_CMD; i++) {
		if ((cmd != curr_cmd) && (cmd != next_cmd))
			return i * sizeof(struct sti_hqvdp_cmd);
		cmd += sizeof(struct sti_hqvdp_cmd);
	}

	return -1;
}

/**
 * sti_hqvdp_get_curr_cmd
 * @hqvdp: hqvdp structure
 *
 * Look for the hqvdp_cmd that is being used by the FW.
 *
 * RETURNS:
 *  the offset of the command to be used.
 * -1 in error cases
 */
static int sti_hqvdp_get_curr_cmd(struct sti_hqvdp *hqvdp)
{
	u32 curr_cmd;
	u32 cmd = hqvdp->hqvdp_cmd_paddr;
	unsigned int i;

	curr_cmd = readl(hqvdp->regs + HQVDP_MBX_CURRENT_CMD);

	for (i = 0; i < NB_VDP_CMD; i++) {
		if (cmd == curr_cmd)
			return i * sizeof(struct sti_hqvdp_cmd);

		cmd += sizeof(struct sti_hqvdp_cmd);
	}

	return -1;
}

/**
 * sti_hqvdp_get_next_cmd
 * @hqvdp: hqvdp structure
 *
 * Look for the next hqvdp_cmd that will be used by the FW.
 *
 * RETURNS:
 *  the offset of the next command that will be used.
 * -1 in error cases
 */
static int sti_hqvdp_get_next_cmd(struct sti_hqvdp *hqvdp)
{
	int next_cmd;
	dma_addr_t cmd = hqvdp->hqvdp_cmd_paddr;
	unsigned int i;

	next_cmd = readl(hqvdp->regs + HQVDP_MBX_NEXT_CMD);

	for (i = 0; i < NB_VDP_CMD; i++) {
		if (cmd == next_cmd)
			return i * sizeof(struct sti_hqvdp_cmd);

		cmd += sizeof(struct sti_hqvdp_cmd);
	}

	return -1;
}

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(hqvdp->regs + reg))

static const char *hqvdp_dbg_get_lut(u32 *coef)
{
	if (!memcmp(coef, coef_lut_a_legacy, 16))
		return "LUT A";
	if (!memcmp(coef, coef_lut_b, 16))
		return "LUT B";
	if (!memcmp(coef, coef_lut_c_y_legacy, 16))
		return "LUT C Y";
	if (!memcmp(coef, coef_lut_c_c_legacy, 16))
		return "LUT C C";
	if (!memcmp(coef, coef_lut_d_y_legacy, 16))
		return "LUT D Y";
	if (!memcmp(coef, coef_lut_d_c_legacy, 16))
		return "LUT D C";
	if (!memcmp(coef, coef_lut_e_y_legacy, 16))
		return "LUT E Y";
	if (!memcmp(coef, coef_lut_e_c_legacy, 16))
		return "LUT E C";
	if (!memcmp(coef, coef_lut_f_y_legacy, 16))
		return "LUT F Y";
	if (!memcmp(coef, coef_lut_f_c_legacy, 16))
		return "LUT F C";
	return "<UNKNOWN>";
}

static void hqvdp_dbg_dump_cmd(struct seq_file *s, struct sti_hqvdp_cmd *c)
{
	int src_w, src_h, dst_w, dst_h;

	seq_puts(s, "\n\tTOP:");
	seq_printf(s, "\n\t %-20s 0x%08X", "Config", c->top.config);
	switch (c->top.config) {
	case TOP_CONFIG_PROGRESSIVE:
		seq_puts(s, "\tProgressive");
		break;
	case TOP_CONFIG_INTER_TOP:
		seq_puts(s, "\tInterlaced, top field");
		break;
	case TOP_CONFIG_INTER_BTM:
		seq_puts(s, "\tInterlaced, bottom field");
		break;
	default:
		seq_puts(s, "\t<UNKNOWN>");
		break;
	}

	seq_printf(s, "\n\t %-20s 0x%08X", "MemFormat", c->top.mem_format);
	seq_printf(s, "\n\t %-20s 0x%08X", "CurrentY", c->top.current_luma);
	seq_printf(s, "\n\t %-20s 0x%08X", "CurrentC", c->top.current_chroma);
	seq_printf(s, "\n\t %-20s 0x%08X", "YSrcPitch", c->top.luma_src_pitch);
	seq_printf(s, "\n\t %-20s 0x%08X", "CSrcPitch",
		   c->top.chroma_src_pitch);
	seq_printf(s, "\n\t %-20s 0x%08X", "InputFrameSize",
		   c->top.input_frame_size);
	seq_printf(s, "\t%dx%d",
		   c->top.input_frame_size & 0x0000FFFF,
		   c->top.input_frame_size >> 16);
	seq_printf(s, "\n\t %-20s 0x%08X", "InputViewportSize",
		   c->top.input_viewport_size);
	src_w = c->top.input_viewport_size & 0x0000FFFF;
	src_h = c->top.input_viewport_size >> 16;
	seq_printf(s, "\t%dx%d", src_w, src_h);

	seq_puts(s, "\n\tHVSRC:");
	seq_printf(s, "\n\t %-20s 0x%08X", "OutputPictureSize",
		   c->hvsrc.output_picture_size);
	dst_w = c->hvsrc.output_picture_size & 0x0000FFFF;
	dst_h = c->hvsrc.output_picture_size >> 16;
	seq_printf(s, "\t%dx%d", dst_w, dst_h);
	seq_printf(s, "\n\t %-20s 0x%08X", "ParamCtrl", c->hvsrc.param_ctrl);

	seq_printf(s, "\n\t %-20s %s", "yh_coef",
		   hqvdp_dbg_get_lut(c->hvsrc.yh_coef));
	seq_printf(s, "\n\t %-20s %s", "ch_coef",
		   hqvdp_dbg_get_lut(c->hvsrc.ch_coef));
	seq_printf(s, "\n\t %-20s %s", "yv_coef",
		   hqvdp_dbg_get_lut(c->hvsrc.yv_coef));
	seq_printf(s, "\n\t %-20s %s", "cv_coef",
		   hqvdp_dbg_get_lut(c->hvsrc.cv_coef));

	seq_printf(s, "\n\t %-20s", "ScaleH");
	if (dst_w > src_w)
		seq_printf(s, " %d/1", dst_w / src_w);
	else
		seq_printf(s, " 1/%d", src_w / dst_w);

	seq_printf(s, "\n\t %-20s", "tScaleV");
	if (dst_h > src_h)
		seq_printf(s, " %d/1", dst_h / src_h);
	else
		seq_printf(s, " 1/%d", src_h / dst_h);

	seq_puts(s, "\n\tCSDI:");
	seq_printf(s, "\n\t %-20s 0x%08X\t", "Config", c->csdi.config);
	switch (c->csdi.config) {
	case CSDI_CONFIG_PROG:
		seq_puts(s, "Bypass");
		break;
	case CSDI_CONFIG_INTER_DIR:
		seq_puts(s, "Deinterlace, directional");
		break;
	default:
		seq_puts(s, "<UNKNOWN>");
		break;
	}

	seq_printf(s, "\n\t %-20s 0x%08X", "Config2", c->csdi.config2);
	seq_printf(s, "\n\t %-20s 0x%08X", "DcdiConfig", c->csdi.dcdi_config);
}

static int hqvdp_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_hqvdp *hqvdp = (struct sti_hqvdp *)node->info_ent->data;
	int cmd, cmd_offset, infoxp70;
	void *virt;

	seq_printf(s, "%s: (vaddr = 0x%p)",
		   sti_plane_to_str(&hqvdp->plane), hqvdp->regs);

	DBGFS_DUMP(HQVDP_MBX_IRQ_TO_XP70);
	DBGFS_DUMP(HQVDP_MBX_INFO_HOST);
	DBGFS_DUMP(HQVDP_MBX_IRQ_TO_HOST);
	DBGFS_DUMP(HQVDP_MBX_INFO_XP70);
	infoxp70 = readl(hqvdp->regs + HQVDP_MBX_INFO_XP70);
	seq_puts(s, "\tFirmware state: ");
	if (infoxp70 & INFO_XP70_FW_READY)
		seq_puts(s, "idle and ready");
	else if (infoxp70 & INFO_XP70_FW_PROCESSING)
		seq_puts(s, "processing a picture");
	else if (infoxp70 & INFO_XP70_FW_INITQUEUES)
		seq_puts(s, "programming queues");
	else
		seq_puts(s, "NOT READY");

	DBGFS_DUMP(HQVDP_MBX_SW_RESET_CTRL);
	DBGFS_DUMP(HQVDP_MBX_STARTUP_CTRL1);
	if (readl(hqvdp->regs + HQVDP_MBX_STARTUP_CTRL1)
					& STARTUP_CTRL1_RST_DONE)
		seq_puts(s, "\tReset is done");
	else
		seq_puts(s, "\tReset is NOT done");
	DBGFS_DUMP(HQVDP_MBX_STARTUP_CTRL2);
	if (readl(hqvdp->regs + HQVDP_MBX_STARTUP_CTRL2)
					& STARTUP_CTRL2_FETCH_EN)
		seq_puts(s, "\tFetch is enabled");
	else
		seq_puts(s, "\tFetch is NOT enabled");
	DBGFS_DUMP(HQVDP_MBX_GP_STATUS);
	DBGFS_DUMP(HQVDP_MBX_NEXT_CMD);
	DBGFS_DUMP(HQVDP_MBX_CURRENT_CMD);
	DBGFS_DUMP(HQVDP_MBX_SOFT_VSYNC);
	if (!(readl(hqvdp->regs + HQVDP_MBX_SOFT_VSYNC) & 3))
		seq_puts(s, "\tHW Vsync");
	else
		seq_puts(s, "\tSW Vsync ?!?!");

	/* Last command */
	cmd = readl(hqvdp->regs + HQVDP_MBX_CURRENT_CMD);
	cmd_offset = sti_hqvdp_get_curr_cmd(hqvdp);
	if (cmd_offset == -1) {
		seq_puts(s, "\n\n  Last command: unknown");
	} else {
		virt = hqvdp->hqvdp_cmd + cmd_offset;
		seq_printf(s, "\n\n  Last command: address @ 0x%x (0x%p)",
			   cmd, virt);
		hqvdp_dbg_dump_cmd(s, (struct sti_hqvdp_cmd *)virt);
	}

	/* Next command */
	cmd = readl(hqvdp->regs + HQVDP_MBX_NEXT_CMD);
	cmd_offset = sti_hqvdp_get_next_cmd(hqvdp);
	if (cmd_offset == -1) {
		seq_puts(s, "\n\n  Next command: unknown");
	} else {
		virt = hqvdp->hqvdp_cmd + cmd_offset;
		seq_printf(s, "\n\n  Next command address: @ 0x%x (0x%p)",
			   cmd, virt);
		hqvdp_dbg_dump_cmd(s, (struct sti_hqvdp_cmd *)virt);
	}

	seq_putc(s, '\n');
	return 0;
}

static struct drm_info_list hqvdp_debugfs_files[] = {
	{ "hqvdp", hqvdp_dbg_show, 0, NULL },
};

static void hqvdp_debugfs_init(struct sti_hqvdp *hqvdp, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hqvdp_debugfs_files); i++)
		hqvdp_debugfs_files[i].data = hqvdp;

	drm_debugfs_create_files(hqvdp_debugfs_files,
				 ARRAY_SIZE(hqvdp_debugfs_files),
				 minor->debugfs_root, minor);
}

/**
 * sti_hqvdp_update_hvsrc
 * @orient: horizontal or vertical
 * @scale:  scaling/zoom factor
 * @hvsrc:  the structure containing the LUT coef
 *
 * Update the Y and C Lut coef, as well as the shift param
 *
 * RETURNS:
 * None.
 */
static void sti_hqvdp_update_hvsrc(enum sti_hvsrc_orient orient, int scale,
		struct sti_hqvdp_hvsrc *hvsrc)
{
	const int *coef_c, *coef_y;
	int shift_c, shift_y;

	/* Get the appropriate coef tables */
	if (scale < SCALE_MAX_FOR_LEG_LUT_F) {
		coef_y = coef_lut_f_y_legacy;
		coef_c = coef_lut_f_c_legacy;
		shift_y = SHIFT_LUT_F_Y_LEGACY;
		shift_c = SHIFT_LUT_F_C_LEGACY;
	} else if (scale < SCALE_MAX_FOR_LEG_LUT_E) {
		coef_y = coef_lut_e_y_legacy;
		coef_c = coef_lut_e_c_legacy;
		shift_y = SHIFT_LUT_E_Y_LEGACY;
		shift_c = SHIFT_LUT_E_C_LEGACY;
	} else if (scale < SCALE_MAX_FOR_LEG_LUT_D) {
		coef_y = coef_lut_d_y_legacy;
		coef_c = coef_lut_d_c_legacy;
		shift_y = SHIFT_LUT_D_Y_LEGACY;
		shift_c = SHIFT_LUT_D_C_LEGACY;
	} else if (scale < SCALE_MAX_FOR_LEG_LUT_C) {
		coef_y = coef_lut_c_y_legacy;
		coef_c = coef_lut_c_c_legacy;
		shift_y = SHIFT_LUT_C_Y_LEGACY;
		shift_c = SHIFT_LUT_C_C_LEGACY;
	} else if (scale == SCALE_MAX_FOR_LEG_LUT_C) {
		coef_y = coef_c = coef_lut_b;
		shift_y = shift_c = SHIFT_LUT_B;
	} else {
		coef_y = coef_c = coef_lut_a_legacy;
		shift_y = shift_c = SHIFT_LUT_A_LEGACY;
	}

	if (orient == HVSRC_HORI) {
		hvsrc->hori_shift = (shift_c << 16) | shift_y;
		memcpy(hvsrc->yh_coef, coef_y, sizeof(hvsrc->yh_coef));
		memcpy(hvsrc->ch_coef, coef_c, sizeof(hvsrc->ch_coef));
	} else {
		hvsrc->vert_shift = (shift_c << 16) | shift_y;
		memcpy(hvsrc->yv_coef, coef_y, sizeof(hvsrc->yv_coef));
		memcpy(hvsrc->cv_coef, coef_c, sizeof(hvsrc->cv_coef));
	}
}

/**
 * sti_hqvdp_check_hw_scaling
 * @hqvdp: hqvdp pointer
 * @mode: display mode with timing constraints
 * @src_w: source width
 * @src_h: source height
 * @dst_w: destination width
 * @dst_h: destination height
 *
 * Check if the HW is able to perform the scaling request
 * The firmware scaling limitation is "CEIL(1/Zy) <= FLOOR(LFW)" where:
 *   Zy = OutputHeight / InputHeight
 *   LFW = (Tx * IPClock) / (MaxNbCycles * Cp)
 *     Tx : Total video mode horizontal resolution
 *     IPClock : HQVDP IP clock (Mhz)
 *     MaxNbCycles: max(InputWidth, OutputWidth)
 *     Cp: Video mode pixel clock (Mhz)
 *
 * RETURNS:
 * True if the HW can scale.
 */
static bool sti_hqvdp_check_hw_scaling(struct sti_hqvdp *hqvdp,
				       struct drm_display_mode *mode,
				       int src_w, int src_h,
				       int dst_w, int dst_h)
{
	unsigned long lfw;
	unsigned int inv_zy;

	lfw = mode->htotal * (clk_get_rate(hqvdp->clk) / 1000000);
	lfw /= max(src_w, dst_w) * mode->clock / 1000;

	inv_zy = DIV_ROUND_UP(src_h, dst_h);

	return inv_zy <= lfw;
}

/**
 * sti_hqvdp_disable
 * @hqvdp: hqvdp pointer
 *
 * Disables the HQVDP plane
 */
static void sti_hqvdp_disable(struct sti_hqvdp *hqvdp)
{
	int i;

	DRM_DEBUG_DRIVER("%s\n", sti_plane_to_str(&hqvdp->plane));

	/* Unregister VTG Vsync callback */
	if (sti_vtg_unregister_client(hqvdp->vtg, &hqvdp->vtg_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");

	/* Set next cmd to NULL */
	writel(0, hqvdp->regs + HQVDP_MBX_NEXT_CMD);

	for (i = 0; i < POLL_MAX_ATTEMPT; i++) {
		if (readl(hqvdp->regs + HQVDP_MBX_INFO_XP70)
				& INFO_XP70_FW_READY)
			break;
		msleep(POLL_DELAY_MS);
	}

	/* VTG can stop now */
	clk_disable_unprepare(hqvdp->clk_pix_main);

	if (i == POLL_MAX_ATTEMPT)
		DRM_ERROR("XP70 could not revert to idle\n");

	hqvdp->plane.status = STI_PLANE_DISABLED;
	hqvdp->vtg_registered = false;
}

/**
 * sti_hqvdp_vtg_cb
 * @nb: notifier block
 * @evt: event message
 * @data: private data
 *
 * Handle VTG Vsync event, display pending bottom field
 *
 * RETURNS:
 * 0 on success.
 */
static int sti_hqvdp_vtg_cb(struct notifier_block *nb, unsigned long evt, void *data)
{
	struct sti_hqvdp *hqvdp = container_of(nb, struct sti_hqvdp, vtg_nb);
	int btm_cmd_offset, top_cmd_offest;
	struct sti_hqvdp_cmd *btm_cmd, *top_cmd;

	if ((evt != VTG_TOP_FIELD_EVENT) && (evt != VTG_BOTTOM_FIELD_EVENT)) {
		DRM_DEBUG_DRIVER("Unknown event\n");
		return 0;
	}

	if (hqvdp->plane.status == STI_PLANE_FLUSHING) {
		/* disable need to be synchronize on vsync event */
		DRM_DEBUG_DRIVER("Vsync event received => disable %s\n",
				 sti_plane_to_str(&hqvdp->plane));

		sti_hqvdp_disable(hqvdp);
	}

	if (hqvdp->btm_field_pending) {
		/* Create the btm field command from the current one */
		btm_cmd_offset = sti_hqvdp_get_free_cmd(hqvdp);
		top_cmd_offest = sti_hqvdp_get_curr_cmd(hqvdp);
		if ((btm_cmd_offset == -1) || (top_cmd_offest == -1)) {
			DRM_DEBUG_DRIVER("Warning: no cmd, will skip field\n");
			return -EBUSY;
		}

		btm_cmd = hqvdp->hqvdp_cmd + btm_cmd_offset;
		top_cmd = hqvdp->hqvdp_cmd + top_cmd_offest;

		memcpy(btm_cmd, top_cmd, sizeof(*btm_cmd));

		btm_cmd->top.config = TOP_CONFIG_INTER_BTM;
		btm_cmd->top.current_luma +=
				btm_cmd->top.luma_src_pitch / 2;
		btm_cmd->top.current_chroma +=
				btm_cmd->top.chroma_src_pitch / 2;

		/* Post the command to mailbox */
		writel(hqvdp->hqvdp_cmd_paddr + btm_cmd_offset,
				hqvdp->regs + HQVDP_MBX_NEXT_CMD);

		hqvdp->btm_field_pending = false;

		dev_dbg(hqvdp->dev, "%s Posted command:0x%x\n",
				__func__, hqvdp->hqvdp_cmd_paddr);

		sti_plane_update_fps(&hqvdp->plane, false, true);
	}

	return 0;
}

static void sti_hqvdp_init(struct sti_hqvdp *hqvdp)
{
	int size;
	dma_addr_t dma_addr;

	hqvdp->vtg_nb.notifier_call = sti_hqvdp_vtg_cb;

	/* Allocate memory for the VDP commands */
	size = NB_VDP_CMD * sizeof(struct sti_hqvdp_cmd);
	hqvdp->hqvdp_cmd = dma_alloc_wc(hqvdp->dev, size,
					&dma_addr,
					GFP_KERNEL | GFP_DMA);
	if (!hqvdp->hqvdp_cmd) {
		DRM_ERROR("Failed to allocate memory for VDP cmd\n");
		return;
	}

	hqvdp->hqvdp_cmd_paddr = (u32)dma_addr;
	memset(hqvdp->hqvdp_cmd, 0, size);
}

static void sti_hqvdp_init_plugs(struct sti_hqvdp *hqvdp)
{
	/* Configure Plugs (same for RD & WR) */
	writel(PLUG_PAGE_SIZE_256, hqvdp->regs + HQVDP_RD_PLUG_PAGE_SIZE);
	writel(PLUG_MIN_OPC_8, hqvdp->regs + HQVDP_RD_PLUG_MIN_OPC);
	writel(PLUG_MAX_OPC_64, hqvdp->regs + HQVDP_RD_PLUG_MAX_OPC);
	writel(PLUG_MAX_CHK_2X, hqvdp->regs + HQVDP_RD_PLUG_MAX_CHK);
	writel(PLUG_MAX_MSG_1X, hqvdp->regs + HQVDP_RD_PLUG_MAX_MSG);
	writel(PLUG_MIN_SPACE_1, hqvdp->regs + HQVDP_RD_PLUG_MIN_SPACE);
	writel(PLUG_CONTROL_ENABLE, hqvdp->regs + HQVDP_RD_PLUG_CONTROL);

	writel(PLUG_PAGE_SIZE_256, hqvdp->regs + HQVDP_WR_PLUG_PAGE_SIZE);
	writel(PLUG_MIN_OPC_8, hqvdp->regs + HQVDP_WR_PLUG_MIN_OPC);
	writel(PLUG_MAX_OPC_64, hqvdp->regs + HQVDP_WR_PLUG_MAX_OPC);
	writel(PLUG_MAX_CHK_2X, hqvdp->regs + HQVDP_WR_PLUG_MAX_CHK);
	writel(PLUG_MAX_MSG_1X, hqvdp->regs + HQVDP_WR_PLUG_MAX_MSG);
	writel(PLUG_MIN_SPACE_1, hqvdp->regs + HQVDP_WR_PLUG_MIN_SPACE);
	writel(PLUG_CONTROL_ENABLE, hqvdp->regs + HQVDP_WR_PLUG_CONTROL);
}

/**
 * sti_hqvdp_start_xp70
 * @hqvdp: hqvdp pointer
 *
 * Run the xP70 initialization sequence
 */
static void sti_hqvdp_start_xp70(struct sti_hqvdp *hqvdp)
{
	const struct firmware *firmware;
	u32 *fw_rd_plug, *fw_wr_plug, *fw_pmem, *fw_dmem;
	u8 *data;
	int i;
	struct fw_header {
		int rd_size;
		int wr_size;
		int pmem_size;
		int dmem_size;
	} *header;

	DRM_DEBUG_DRIVER("\n");

	if (hqvdp->xp70_initialized) {
		DRM_DEBUG_DRIVER("HQVDP XP70 already initialized\n");
		return;
	}

	/* Request firmware */
	if (request_firmware(&firmware, HQVDP_FMW_NAME, hqvdp->dev)) {
		DRM_ERROR("Can't get HQVDP firmware\n");
		return;
	}

	/* Check firmware parts */
	if (!firmware) {
		DRM_ERROR("Firmware not available\n");
		return;
	}

	header = (struct fw_header *)firmware->data;
	if (firmware->size < sizeof(*header)) {
		DRM_ERROR("Invalid firmware size (%zu)\n", firmware->size);
		goto out;
	}
	if ((sizeof(*header) + header->rd_size + header->wr_size +
		header->pmem_size + header->dmem_size) != firmware->size) {
		DRM_ERROR("Invalid fmw structure (%zu+%d+%d+%d+%d != %zu)\n",
			  sizeof(*header), header->rd_size, header->wr_size,
			  header->pmem_size, header->dmem_size,
			  firmware->size);
		goto out;
	}

	data = (u8 *)firmware->data;
	data += sizeof(*header);
	fw_rd_plug = (void *)data;
	data += header->rd_size;
	fw_wr_plug = (void *)data;
	data += header->wr_size;
	fw_pmem = (void *)data;
	data += header->pmem_size;
	fw_dmem = (void *)data;

	/* Enable clock */
	if (clk_prepare_enable(hqvdp->clk))
		DRM_ERROR("Failed to prepare/enable HQVDP clk\n");

	/* Reset */
	writel(SW_RESET_CTRL_FULL, hqvdp->regs + HQVDP_MBX_SW_RESET_CTRL);

	for (i = 0; i < POLL_MAX_ATTEMPT; i++) {
		if (readl(hqvdp->regs + HQVDP_MBX_STARTUP_CTRL1)
				& STARTUP_CTRL1_RST_DONE)
			break;
		msleep(POLL_DELAY_MS);
	}
	if (i == POLL_MAX_ATTEMPT) {
		DRM_ERROR("Could not reset\n");
		clk_disable_unprepare(hqvdp->clk);
		goto out;
	}

	/* Init Read & Write plugs */
	for (i = 0; i < header->rd_size / 4; i++)
		writel(fw_rd_plug[i], hqvdp->regs + HQVDP_RD_PLUG + i * 4);
	for (i = 0; i < header->wr_size / 4; i++)
		writel(fw_wr_plug[i], hqvdp->regs + HQVDP_WR_PLUG + i * 4);

	sti_hqvdp_init_plugs(hqvdp);

	/* Authorize Idle Mode */
	writel(STARTUP_CTRL1_AUTH_IDLE, hqvdp->regs + HQVDP_MBX_STARTUP_CTRL1);

	/* Prevent VTG interruption during the boot */
	writel(SOFT_VSYNC_SW_CTRL_IRQ, hqvdp->regs + HQVDP_MBX_SOFT_VSYNC);
	writel(0, hqvdp->regs + HQVDP_MBX_NEXT_CMD);

	/* Download PMEM & DMEM */
	for (i = 0; i < header->pmem_size / 4; i++)
		writel(fw_pmem[i], hqvdp->regs + HQVDP_PMEM + i * 4);
	for (i = 0; i < header->dmem_size / 4; i++)
		writel(fw_dmem[i], hqvdp->regs + HQVDP_DMEM + i * 4);

	/* Enable fetch */
	writel(STARTUP_CTRL2_FETCH_EN, hqvdp->regs + HQVDP_MBX_STARTUP_CTRL2);

	/* Wait end of boot */
	for (i = 0; i < POLL_MAX_ATTEMPT; i++) {
		if (readl(hqvdp->regs + HQVDP_MBX_INFO_XP70)
				& INFO_XP70_FW_READY)
			break;
		msleep(POLL_DELAY_MS);
	}
	if (i == POLL_MAX_ATTEMPT) {
		DRM_ERROR("Could not boot\n");
		clk_disable_unprepare(hqvdp->clk);
		goto out;
	}

	/* Launch Vsync */
	writel(SOFT_VSYNC_HW, hqvdp->regs + HQVDP_MBX_SOFT_VSYNC);

	DRM_INFO("HQVDP XP70 initialized\n");

	hqvdp->xp70_initialized = true;

out:
	release_firmware(firmware);
}

static int sti_hqvdp_atomic_check(struct drm_plane *drm_plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_hqvdp *hqvdp = to_sti_hqvdp(plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;

	/* no need for further checks if the plane is being disabled */
	if (!crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	mode = &crtc_state->mode;
	dst_x = new_plane_state->crtc_x;
	dst_y = new_plane_state->crtc_y;
	dst_w = clamp_val(new_plane_state->crtc_w, 0, mode->hdisplay - dst_x);
	dst_h = clamp_val(new_plane_state->crtc_h, 0, mode->vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = new_plane_state->src_x >> 16;
	src_y = new_plane_state->src_y >> 16;
	src_w = new_plane_state->src_w >> 16;
	src_h = new_plane_state->src_h >> 16;

	if (mode->clock && !sti_hqvdp_check_hw_scaling(hqvdp, mode,
						       src_w, src_h,
						       dst_w, dst_h)) {
		DRM_ERROR("Scaling beyond HW capabilities\n");
		return -EINVAL;
	}

	if (!drm_fb_dma_get_gem_obj(fb, 0)) {
		DRM_ERROR("Can't get DMA GEM object for fb\n");
		return -EINVAL;
	}

	/*
	 * Input / output size
	 * Align to upper even value
	 */
	dst_w = ALIGN(dst_w, 2);
	dst_h = ALIGN(dst_h, 2);

	if ((src_w > MAX_WIDTH) || (src_w < MIN_WIDTH) ||
	    (src_h > MAX_HEIGHT) || (src_h < MIN_HEIGHT) ||
	    (dst_w > MAX_WIDTH) || (dst_w < MIN_WIDTH) ||
	    (dst_h > MAX_HEIGHT) || (dst_h < MIN_HEIGHT)) {
		DRM_ERROR("Invalid in/out size %dx%d -> %dx%d\n",
			  src_w, src_h,
			  dst_w, dst_h);
		return -EINVAL;
	}

	if (!hqvdp->xp70_initialized)
		/* Start HQVDP XP70 coprocessor */
		sti_hqvdp_start_xp70(hqvdp);

	if (!hqvdp->vtg_registered) {
		/* Prevent VTG shutdown */
		if (clk_prepare_enable(hqvdp->clk_pix_main)) {
			DRM_ERROR("Failed to prepare/enable pix main clk\n");
			return -EINVAL;
		}

		/* Register VTG Vsync callback to handle bottom fields */
		if (sti_vtg_register_client(hqvdp->vtg,
					    &hqvdp->vtg_nb,
					    crtc)) {
			DRM_ERROR("Cannot register VTG notifier\n");
			clk_disable_unprepare(hqvdp->clk_pix_main);
			return -EINVAL;
		}
		hqvdp->vtg_registered = true;
	}

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(to_sti_mixer(crtc)),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
		      sti_plane_to_str(plane),
		      dst_w, dst_h, dst_x, dst_y,
		      src_w, src_h, src_x, src_y);

	return 0;
}

static void sti_hqvdp_atomic_update(struct drm_plane *drm_plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *oldstate = drm_atomic_get_old_plane_state(state,
									  drm_plane);
	struct drm_plane_state *newstate = drm_atomic_get_new_plane_state(state,
									  drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_hqvdp *hqvdp = to_sti_hqvdp(plane);
	struct drm_crtc *crtc = newstate->crtc;
	struct drm_framebuffer *fb = newstate->fb;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	struct drm_gem_dma_object *dma_obj;
	struct sti_hqvdp_cmd *cmd;
	int scale_h, scale_v;
	int cmd_offset;

	if (!crtc || !fb)
		return;

	if ((oldstate->fb == newstate->fb) &&
	    (oldstate->crtc_x == newstate->crtc_x) &&
	    (oldstate->crtc_y == newstate->crtc_y) &&
	    (oldstate->crtc_w == newstate->crtc_w) &&
	    (oldstate->crtc_h == newstate->crtc_h) &&
	    (oldstate->src_x == newstate->src_x) &&
	    (oldstate->src_y == newstate->src_y) &&
	    (oldstate->src_w == newstate->src_w) &&
	    (oldstate->src_h == newstate->src_h)) {
		/* No change since last update, do not post cmd */
		DRM_DEBUG_DRIVER("No change, not posting cmd\n");
		plane->status = STI_PLANE_UPDATED;
		return;
	}

	mode = &crtc->mode;
	dst_x = newstate->crtc_x;
	dst_y = newstate->crtc_y;
	dst_w = clamp_val(newstate->crtc_w, 0, mode->hdisplay - dst_x);
	dst_h = clamp_val(newstate->crtc_h, 0, mode->vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = newstate->src_x >> 16;
	src_y = newstate->src_y >> 16;
	src_w = newstate->src_w >> 16;
	src_h = newstate->src_h >> 16;

	cmd_offset = sti_hqvdp_get_free_cmd(hqvdp);
	if (cmd_offset == -1) {
		DRM_DEBUG_DRIVER("Warning: no cmd, will skip frame\n");
		return;
	}
	cmd = hqvdp->hqvdp_cmd + cmd_offset;

	/* Static parameters, defaulting to progressive mode */
	cmd->top.config = TOP_CONFIG_PROGRESSIVE;
	cmd->top.mem_format = TOP_MEM_FORMAT_DFLT;
	cmd->hvsrc.param_ctrl = HVSRC_PARAM_CTRL_DFLT;
	cmd->csdi.config = CSDI_CONFIG_PROG;

	/* VC1RE, FMD bypassed : keep everything set to 0
	 * IQI/P2I bypassed */
	cmd->iqi.config = IQI_CONFIG_DFLT;
	cmd->iqi.con_bri = IQI_CON_BRI_DFLT;
	cmd->iqi.sat_gain = IQI_SAT_GAIN_DFLT;
	cmd->iqi.pxf_conf = IQI_PXF_CONF_DFLT;

	dma_obj = drm_fb_dma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&fb->format->format,
			 (unsigned long) dma_obj->dma_addr);

	/* Buffer planes address */
	cmd->top.current_luma = (u32) dma_obj->dma_addr + fb->offsets[0];
	cmd->top.current_chroma = (u32) dma_obj->dma_addr + fb->offsets[1];

	/* Pitches */
	cmd->top.luma_processed_pitch = fb->pitches[0];
	cmd->top.luma_src_pitch = fb->pitches[0];
	cmd->top.chroma_processed_pitch = fb->pitches[1];
	cmd->top.chroma_src_pitch = fb->pitches[1];

	/* Input / output size
	 * Align to upper even value */
	dst_w = ALIGN(dst_w, 2);
	dst_h = ALIGN(dst_h, 2);

	cmd->top.input_viewport_size = src_h << 16 | src_w;
	cmd->top.input_frame_size = src_h << 16 | src_w;
	cmd->hvsrc.output_picture_size = dst_h << 16 | dst_w;
	cmd->top.input_viewport_ori = src_y << 16 | src_x;

	/* Handle interlaced */
	if (fb->flags & DRM_MODE_FB_INTERLACED) {
		/* Top field to display */
		cmd->top.config = TOP_CONFIG_INTER_TOP;

		/* Update pitches and vert size */
		cmd->top.input_frame_size = (src_h / 2) << 16 | src_w;
		cmd->top.luma_processed_pitch *= 2;
		cmd->top.luma_src_pitch *= 2;
		cmd->top.chroma_processed_pitch *= 2;
		cmd->top.chroma_src_pitch *= 2;

		/* Enable directional deinterlacing processing */
		cmd->csdi.config = CSDI_CONFIG_INTER_DIR;
		cmd->csdi.config2 = CSDI_CONFIG2_DFLT;
		cmd->csdi.dcdi_config = CSDI_DCDI_CONFIG_DFLT;
	}

	/* Update hvsrc lut coef */
	scale_h = SCALE_FACTOR * dst_w / src_w;
	sti_hqvdp_update_hvsrc(HVSRC_HORI, scale_h, &cmd->hvsrc);

	scale_v = SCALE_FACTOR * dst_h / src_h;
	sti_hqvdp_update_hvsrc(HVSRC_VERT, scale_v, &cmd->hvsrc);

	writel(hqvdp->hqvdp_cmd_paddr + cmd_offset,
	       hqvdp->regs + HQVDP_MBX_NEXT_CMD);

	/* Interlaced : get ready to display the bottom field at next Vsync */
	if (fb->flags & DRM_MODE_FB_INTERLACED)
		hqvdp->btm_field_pending = true;

	dev_dbg(hqvdp->dev, "%s Posted command:0x%x\n",
		__func__, hqvdp->hqvdp_cmd_paddr + cmd_offset);

	sti_plane_update_fps(plane, true, true);

	plane->status = STI_PLANE_UPDATED;
}

static void sti_hqvdp_atomic_disable(struct drm_plane *drm_plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *oldstate = drm_atomic_get_old_plane_state(state,
									  drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);

	if (!oldstate->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n",
				 drm_plane->base.id);
		return;
	}

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			 oldstate->crtc->base.id,
			 sti_mixer_to_str(to_sti_mixer(oldstate->crtc)),
			 drm_plane->base.id, sti_plane_to_str(plane));

	plane->status = STI_PLANE_DISABLING;
}

static const struct drm_plane_helper_funcs sti_hqvdp_helpers_funcs = {
	.atomic_check = sti_hqvdp_atomic_check,
	.atomic_update = sti_hqvdp_atomic_update,
	.atomic_disable = sti_hqvdp_atomic_disable,
};

static int sti_hqvdp_late_register(struct drm_plane *drm_plane)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_hqvdp *hqvdp = to_sti_hqvdp(plane);

	hqvdp_debugfs_init(hqvdp, drm_plane->dev->primary);

	return 0;
}

static const struct drm_plane_funcs sti_hqvdp_plane_helpers_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.late_register = sti_hqvdp_late_register,
};

static struct drm_plane *sti_hqvdp_create(struct drm_device *drm_dev,
					  struct device *dev, int desc)
{
	struct sti_hqvdp *hqvdp = dev_get_drvdata(dev);
	int res;

	hqvdp->plane.desc = desc;
	hqvdp->plane.status = STI_PLANE_DISABLED;

	sti_hqvdp_init(hqvdp);

	res = drm_universal_plane_init(drm_dev, &hqvdp->plane.drm_plane, 1,
				       &sti_hqvdp_plane_helpers_funcs,
				       hqvdp_supported_formats,
				       ARRAY_SIZE(hqvdp_supported_formats),
				       NULL, DRM_PLANE_TYPE_OVERLAY, NULL);
	if (res) {
		DRM_ERROR("Failed to initialize universal plane\n");
		return NULL;
	}

	drm_plane_helper_add(&hqvdp->plane.drm_plane, &sti_hqvdp_helpers_funcs);

	sti_plane_init_property(&hqvdp->plane, DRM_PLANE_TYPE_OVERLAY);

	return &hqvdp->plane.drm_plane;
}

static int sti_hqvdp_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_hqvdp *hqvdp = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_plane *plane;

	DRM_DEBUG_DRIVER("\n");

	hqvdp->drm_dev = drm_dev;

	/* Create HQVDP plane once xp70 is initialized */
	plane = sti_hqvdp_create(drm_dev, hqvdp->dev, STI_HQVDP_0);
	if (!plane)
		DRM_ERROR("Can't create HQVDP plane\n");

	return 0;
}

static void sti_hqvdp_unbind(struct device *dev,
		struct device *master, void *data)
{
	/* do nothing */
}

static const struct component_ops sti_hqvdp_ops = {
	.bind = sti_hqvdp_bind,
	.unbind = sti_hqvdp_unbind,
};

static int sti_hqvdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *vtg_np;
	struct sti_hqvdp *hqvdp;

	DRM_DEBUG_DRIVER("\n");

	hqvdp = devm_kzalloc(dev, sizeof(*hqvdp), GFP_KERNEL);
	if (!hqvdp) {
		DRM_ERROR("Failed to allocate HQVDP context\n");
		return -ENOMEM;
	}

	hqvdp->dev = dev;
	hqvdp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hqvdp->regs)) {
		DRM_ERROR("Register mapping failed\n");
		return PTR_ERR(hqvdp->regs);
	}

	/* Get clock resources */
	hqvdp->clk = devm_clk_get(dev, "hqvdp");
	hqvdp->clk_pix_main = devm_clk_get(dev, "pix_main");
	if (IS_ERR(hqvdp->clk) || IS_ERR(hqvdp->clk_pix_main)) {
		DRM_ERROR("Cannot get clocks\n");
		return -ENXIO;
	}

	/* Get reset resources */
	hqvdp->reset = devm_reset_control_get(dev, "hqvdp");
	if (!IS_ERR(hqvdp->reset))
		reset_control_deassert(hqvdp->reset);

	vtg_np = of_parse_phandle(pdev->dev.of_node, "st,vtg", 0);
	if (vtg_np)
		hqvdp->vtg = of_vtg_find(vtg_np);
	of_node_put(vtg_np);

	platform_set_drvdata(pdev, hqvdp);

	return component_add(&pdev->dev, &sti_hqvdp_ops);
}

static void sti_hqvdp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sti_hqvdp_ops);
}

static const struct of_device_id hqvdp_of_match[] = {
	{ .compatible = "st,stih407-hqvdp", },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, hqvdp_of_match);

struct platform_driver sti_hqvdp_driver = {
	.driver = {
		.name = "sti-hqvdp",
		.of_match_table = hqvdp_of_match,
	},
	.probe = sti_hqvdp_probe,
	.remove = sti_hqvdp_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
