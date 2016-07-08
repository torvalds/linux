/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Vincent Abriou <vincent.abriou@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "sti_crtc.h"
#include "sti_vtg.h"

/* glue registers */
#define TVO_CSC_MAIN_M0                  0x000
#define TVO_CSC_MAIN_M1                  0x004
#define TVO_CSC_MAIN_M2                  0x008
#define TVO_CSC_MAIN_M3                  0x00c
#define TVO_CSC_MAIN_M4                  0x010
#define TVO_CSC_MAIN_M5                  0x014
#define TVO_CSC_MAIN_M6                  0x018
#define TVO_CSC_MAIN_M7                  0x01c
#define TVO_MAIN_IN_VID_FORMAT           0x030
#define TVO_CSC_AUX_M0                   0x100
#define TVO_CSC_AUX_M1                   0x104
#define TVO_CSC_AUX_M2                   0x108
#define TVO_CSC_AUX_M3                   0x10c
#define TVO_CSC_AUX_M4                   0x110
#define TVO_CSC_AUX_M5                   0x114
#define TVO_CSC_AUX_M6                   0x118
#define TVO_CSC_AUX_M7                   0x11c
#define TVO_AUX_IN_VID_FORMAT            0x130
#define TVO_VIP_HDF                      0x400
#define TVO_HD_SYNC_SEL                  0x418
#define TVO_HD_DAC_CFG_OFF               0x420
#define TVO_VIP_HDMI                     0x500
#define TVO_HDMI_FORCE_COLOR_0           0x504
#define TVO_HDMI_FORCE_COLOR_1           0x508
#define TVO_HDMI_CLIP_VALUE_B_CB         0x50c
#define TVO_HDMI_CLIP_VALUE_Y_G          0x510
#define TVO_HDMI_CLIP_VALUE_R_CR         0x514
#define TVO_HDMI_SYNC_SEL                0x518
#define TVO_HDMI_DFV_OBS                 0x540
#define TVO_VIP_DVO                      0x600
#define TVO_DVO_SYNC_SEL                 0x618
#define TVO_DVO_CONFIG                   0x620

#define TVO_IN_FMT_SIGNED                BIT(0)
#define TVO_SYNC_EXT                     BIT(4)

#define TVO_VIP_REORDER_R_SHIFT          24
#define TVO_VIP_REORDER_G_SHIFT          20
#define TVO_VIP_REORDER_B_SHIFT          16
#define TVO_VIP_REORDER_MASK             0x3
#define TVO_VIP_REORDER_Y_G_SEL          0
#define TVO_VIP_REORDER_CB_B_SEL         1
#define TVO_VIP_REORDER_CR_R_SEL         2

#define TVO_VIP_CLIP_SHIFT               8
#define TVO_VIP_CLIP_MASK                0x7
#define TVO_VIP_CLIP_DISABLED            0
#define TVO_VIP_CLIP_EAV_SAV             1
#define TVO_VIP_CLIP_LIMITED_RANGE_RGB_Y 2
#define TVO_VIP_CLIP_LIMITED_RANGE_CB_CR 3
#define TVO_VIP_CLIP_PROG_RANGE          4

#define TVO_VIP_RND_SHIFT                4
#define TVO_VIP_RND_MASK                 0x3
#define TVO_VIP_RND_8BIT_ROUNDED         0
#define TVO_VIP_RND_10BIT_ROUNDED        1
#define TVO_VIP_RND_12BIT_ROUNDED        2

#define TVO_VIP_SEL_INPUT_MASK           0xf
#define TVO_VIP_SEL_INPUT_MAIN           0x0
#define TVO_VIP_SEL_INPUT_AUX            0x8
#define TVO_VIP_SEL_INPUT_FORCE_COLOR    0xf
#define TVO_VIP_SEL_INPUT_BYPASS_MASK    0x1
#define TVO_VIP_SEL_INPUT_BYPASSED       1

#define TVO_SYNC_MAIN_VTG_SET_REF        0x00
#define TVO_SYNC_AUX_VTG_SET_REF         0x10

#define TVO_SYNC_HD_DCS_SHIFT            8

#define TVO_SYNC_DVO_PAD_HSYNC_SHIFT     8
#define TVO_SYNC_DVO_PAD_VSYNC_SHIFT     16

#define ENCODER_CRTC_MASK                (BIT(0) | BIT(1))

#define TVO_MIN_HD_HEIGHT                720

/* enum listing the supported output data format */
enum sti_tvout_video_out_type {
	STI_TVOUT_VIDEO_OUT_RGB,
	STI_TVOUT_VIDEO_OUT_YUV,
};

struct sti_tvout {
	struct device *dev;
	struct drm_device *drm_dev;
	void __iomem *regs;
	struct reset_control *reset;
	struct drm_encoder *hdmi;
	struct drm_encoder *hda;
	struct drm_encoder *dvo;
};

struct sti_tvout_encoder {
	struct drm_encoder encoder;
	struct sti_tvout *tvout;
};

#define to_sti_tvout_encoder(x) \
	container_of(x, struct sti_tvout_encoder, encoder)

#define to_sti_tvout(x) to_sti_tvout_encoder(x)->tvout

/* preformatter conversion matrix */
static const u32 rgb_to_ycbcr_601[8] = {
	0xF927082E, 0x04C9FEAB, 0x01D30964, 0xFA95FD3D,
	0x0000082E, 0x00002000, 0x00002000, 0x00000000
};

/* 709 RGB to YCbCr */
static const u32 rgb_to_ycbcr_709[8] = {
	0xF891082F, 0x0367FF40, 0x01280B71, 0xF9B1FE20,
	0x0000082F, 0x00002000, 0x00002000, 0x00000000
};

static u32 tvout_read(struct sti_tvout *tvout, int offset)
{
	return readl(tvout->regs + offset);
}

static void tvout_write(struct sti_tvout *tvout, u32 val, int offset)
{
	writel(val, tvout->regs + offset);
}

/**
 * Set the clipping mode of a VIP
 *
 * @tvout: tvout structure
 * @reg: register to set
 * @cr_r:
 * @y_g:
 * @cb_b:
 */
static void tvout_vip_set_color_order(struct sti_tvout *tvout, int reg,
				      u32 cr_r, u32 y_g, u32 cb_b)
{
	u32 val = tvout_read(tvout, reg);

	val &= ~(TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_R_SHIFT);
	val &= ~(TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_G_SHIFT);
	val &= ~(TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_B_SHIFT);
	val |= cr_r << TVO_VIP_REORDER_R_SHIFT;
	val |= y_g << TVO_VIP_REORDER_G_SHIFT;
	val |= cb_b << TVO_VIP_REORDER_B_SHIFT;

	tvout_write(tvout, val, reg);
}

/**
 * Set the clipping mode of a VIP
 *
 * @tvout: tvout structure
 * @reg: register to set
 * @range: clipping range
 */
static void tvout_vip_set_clip_mode(struct sti_tvout *tvout, int reg, u32 range)
{
	u32 val = tvout_read(tvout, reg);

	val &= ~(TVO_VIP_CLIP_MASK << TVO_VIP_CLIP_SHIFT);
	val |= range << TVO_VIP_CLIP_SHIFT;
	tvout_write(tvout, val, reg);
}

/**
 * Set the rounded value of a VIP
 *
 * @tvout: tvout structure
 * @reg: register to set
 * @rnd: rounded val per component
 */
static void tvout_vip_set_rnd(struct sti_tvout *tvout, int reg, u32 rnd)
{
	u32 val = tvout_read(tvout, reg);

	val &= ~(TVO_VIP_RND_MASK << TVO_VIP_RND_SHIFT);
	val |= rnd << TVO_VIP_RND_SHIFT;
	tvout_write(tvout, val, reg);
}

/**
 * Select the VIP input
 *
 * @tvout: tvout structure
 * @reg: register to set
 * @main_path: main or auxiliary path
 * @sel_input_logic_inverted: need to invert the logic
 * @sel_input: selected_input (main/aux + conv)
 */
static void tvout_vip_set_sel_input(struct sti_tvout *tvout,
				    int reg,
				    bool main_path,
				    bool sel_input_logic_inverted,
				    enum sti_tvout_video_out_type video_out)
{
	u32 sel_input;
	u32 val = tvout_read(tvout, reg);

	if (main_path)
		sel_input = TVO_VIP_SEL_INPUT_MAIN;
	else
		sel_input = TVO_VIP_SEL_INPUT_AUX;

	switch (video_out) {
	case STI_TVOUT_VIDEO_OUT_RGB:
		sel_input |= TVO_VIP_SEL_INPUT_BYPASSED;
		break;
	case STI_TVOUT_VIDEO_OUT_YUV:
		sel_input &= ~TVO_VIP_SEL_INPUT_BYPASSED;
		break;
	}

	/* on stih407 chip the sel_input bypass mode logic is inverted */
	if (sel_input_logic_inverted)
		sel_input = sel_input ^ TVO_VIP_SEL_INPUT_BYPASS_MASK;

	val &= ~TVO_VIP_SEL_INPUT_MASK;
	val |= sel_input;
	tvout_write(tvout, val, reg);
}

/**
 * Select the input video signed or unsigned
 *
 * @tvout: tvout structure
 * @reg: register to set
 * @in_vid_signed: used video input format
 */
static void tvout_vip_set_in_vid_fmt(struct sti_tvout *tvout,
		int reg, u32 in_vid_fmt)
{
	u32 val = tvout_read(tvout, reg);

	val &= ~TVO_IN_FMT_SIGNED;
	val |= in_vid_fmt;
	tvout_write(tvout, val, reg);
}

/**
 * Set preformatter matrix
 *
 * @tvout: tvout structure
 * @mode: display mode structure
 */
static void tvout_preformatter_set_matrix(struct sti_tvout *tvout,
					  struct drm_display_mode *mode)
{
	unsigned int i;
	const u32 *pf_matrix;

	if (mode->vdisplay >= TVO_MIN_HD_HEIGHT)
		pf_matrix = rgb_to_ycbcr_709;
	else
		pf_matrix = rgb_to_ycbcr_601;

	for (i = 0; i < 8; i++) {
		tvout_write(tvout, *(pf_matrix + i),
			    TVO_CSC_MAIN_M0 + (i * 4));
		tvout_write(tvout, *(pf_matrix + i),
			    TVO_CSC_AUX_M0 + (i * 4));
	}
}

/**
 * Start VIP block for DVO output
 *
 * @tvout: pointer on tvout structure
 * @main_path: true if main path has to be used in the vip configuration
 *	  else aux path is used.
 */
static void tvout_dvo_start(struct sti_tvout *tvout, bool main_path)
{
	struct device_node *node = tvout->dev->of_node;
	bool sel_input_logic_inverted = false;
	u32 tvo_in_vid_format;
	int val, tmp;

	dev_dbg(tvout->dev, "%s\n", __func__);

	if (main_path) {
		DRM_DEBUG_DRIVER("main vip for DVO\n");
		/* Select the input sync for dvo */
		tmp = TVO_SYNC_MAIN_VTG_SET_REF | VTG_SYNC_ID_DVO;
		val  = tmp << TVO_SYNC_DVO_PAD_VSYNC_SHIFT;
		val |= tmp << TVO_SYNC_DVO_PAD_HSYNC_SHIFT;
		val |= tmp;
		tvout_write(tvout, val, TVO_DVO_SYNC_SEL);
		tvo_in_vid_format = TVO_MAIN_IN_VID_FORMAT;
	} else {
		DRM_DEBUG_DRIVER("aux vip for DVO\n");
		/* Select the input sync for dvo */
		tmp = TVO_SYNC_AUX_VTG_SET_REF | VTG_SYNC_ID_DVO;
		val  = tmp << TVO_SYNC_DVO_PAD_VSYNC_SHIFT;
		val |= tmp << TVO_SYNC_DVO_PAD_HSYNC_SHIFT;
		val |= tmp;
		tvout_write(tvout, val, TVO_DVO_SYNC_SEL);
		tvo_in_vid_format = TVO_AUX_IN_VID_FORMAT;
	}

	/* Set color channel order */
	tvout_vip_set_color_order(tvout, TVO_VIP_DVO,
				  TVO_VIP_REORDER_CR_R_SEL,
				  TVO_VIP_REORDER_Y_G_SEL,
				  TVO_VIP_REORDER_CB_B_SEL);

	/* Set clipping mode */
	tvout_vip_set_clip_mode(tvout, TVO_VIP_DVO, TVO_VIP_CLIP_DISABLED);

	/* Set round mode (rounded to 8-bit per component) */
	tvout_vip_set_rnd(tvout, TVO_VIP_DVO, TVO_VIP_RND_8BIT_ROUNDED);

	if (of_device_is_compatible(node, "st,stih407-tvout")) {
		/* Set input video format */
		tvout_vip_set_in_vid_fmt(tvout, tvo_in_vid_format,
					 TVO_IN_FMT_SIGNED);
		sel_input_logic_inverted = true;
	}

	/* Input selection */
	tvout_vip_set_sel_input(tvout, TVO_VIP_DVO, main_path,
				sel_input_logic_inverted,
				STI_TVOUT_VIDEO_OUT_RGB);
}

/**
 * Start VIP block for HDMI output
 *
 * @tvout: pointer on tvout structure
 * @main_path: true if main path has to be used in the vip configuration
 *	  else aux path is used.
 */
static void tvout_hdmi_start(struct sti_tvout *tvout, bool main_path)
{
	struct device_node *node = tvout->dev->of_node;
	bool sel_input_logic_inverted = false;
	u32 tvo_in_vid_format;

	dev_dbg(tvout->dev, "%s\n", __func__);

	if (main_path) {
		DRM_DEBUG_DRIVER("main vip for hdmi\n");
		/* select the input sync for hdmi */
		tvout_write(tvout,
			    TVO_SYNC_MAIN_VTG_SET_REF | VTG_SYNC_ID_HDMI,
			    TVO_HDMI_SYNC_SEL);
		tvo_in_vid_format = TVO_MAIN_IN_VID_FORMAT;
	} else {
		DRM_DEBUG_DRIVER("aux vip for hdmi\n");
		/* select the input sync for hdmi */
		tvout_write(tvout,
			    TVO_SYNC_AUX_VTG_SET_REF | VTG_SYNC_ID_HDMI,
			    TVO_HDMI_SYNC_SEL);
		tvo_in_vid_format = TVO_AUX_IN_VID_FORMAT;
	}

	/* set color channel order */
	tvout_vip_set_color_order(tvout, TVO_VIP_HDMI,
				  TVO_VIP_REORDER_CR_R_SEL,
				  TVO_VIP_REORDER_Y_G_SEL,
				  TVO_VIP_REORDER_CB_B_SEL);

	/* set clipping mode */
	tvout_vip_set_clip_mode(tvout, TVO_VIP_HDMI, TVO_VIP_CLIP_DISABLED);

	/* set round mode (rounded to 8-bit per component) */
	tvout_vip_set_rnd(tvout, TVO_VIP_HDMI, TVO_VIP_RND_8BIT_ROUNDED);

	if (of_device_is_compatible(node, "st,stih407-tvout")) {
		/* set input video format */
		tvout_vip_set_in_vid_fmt(tvout, tvo_in_vid_format,
					TVO_IN_FMT_SIGNED);
		sel_input_logic_inverted = true;
	}

	/* input selection */
	tvout_vip_set_sel_input(tvout, TVO_VIP_HDMI, main_path,
			sel_input_logic_inverted, STI_TVOUT_VIDEO_OUT_RGB);
}

/**
 * Start HDF VIP and HD DAC
 *
 * @tvout: pointer on tvout structure
 * @main_path: true if main path has to be used in the vip configuration
 *	  else aux path is used.
 */
static void tvout_hda_start(struct sti_tvout *tvout, bool main_path)
{
	struct device_node *node = tvout->dev->of_node;
	bool sel_input_logic_inverted = false;
	u32 tvo_in_vid_format;
	int val;

	dev_dbg(tvout->dev, "%s\n", __func__);

	if (main_path) {
		DRM_DEBUG_DRIVER("main vip for HDF\n");
		/* Select the input sync for HD analog and HD DCS */
		val  = TVO_SYNC_MAIN_VTG_SET_REF | VTG_SYNC_ID_HDDCS;
		val  = val << TVO_SYNC_HD_DCS_SHIFT;
		val |= TVO_SYNC_MAIN_VTG_SET_REF | VTG_SYNC_ID_HDF;
		tvout_write(tvout, val, TVO_HD_SYNC_SEL);
		tvo_in_vid_format = TVO_MAIN_IN_VID_FORMAT;
	} else {
		DRM_DEBUG_DRIVER("aux vip for HDF\n");
		/* Select the input sync for HD analog and HD DCS */
		val  = TVO_SYNC_AUX_VTG_SET_REF | VTG_SYNC_ID_HDDCS;
		val  = val << TVO_SYNC_HD_DCS_SHIFT;
		val |= TVO_SYNC_AUX_VTG_SET_REF | VTG_SYNC_ID_HDF;
		tvout_write(tvout, val, TVO_HD_SYNC_SEL);
		tvo_in_vid_format = TVO_AUX_IN_VID_FORMAT;
	}

	/* set color channel order */
	tvout_vip_set_color_order(tvout, TVO_VIP_HDF,
				  TVO_VIP_REORDER_CR_R_SEL,
				  TVO_VIP_REORDER_Y_G_SEL,
				  TVO_VIP_REORDER_CB_B_SEL);

	/* set clipping mode */
	tvout_vip_set_clip_mode(tvout, TVO_VIP_HDF, TVO_VIP_CLIP_DISABLED);

	/* set round mode (rounded to 10-bit per component) */
	tvout_vip_set_rnd(tvout, TVO_VIP_HDF, TVO_VIP_RND_10BIT_ROUNDED);

	if (of_device_is_compatible(node, "st,stih407-tvout")) {
		/* set input video format */
		tvout_vip_set_in_vid_fmt(tvout,
			tvo_in_vid_format, TVO_IN_FMT_SIGNED);
		sel_input_logic_inverted = true;
	}

	/* Input selection */
	tvout_vip_set_sel_input(tvout, TVO_VIP_HDF, main_path,
				sel_input_logic_inverted,
				STI_TVOUT_VIDEO_OUT_YUV);

	/* power up HD DAC */
	tvout_write(tvout, 0, TVO_HD_DAC_CFG_OFF);
}

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(tvout->regs + reg))

static void tvout_dbg_vip(struct seq_file *s, int val)
{
	int r, g, b, tmp, mask;
	char *const reorder[] = {"Y_G", "Cb_B", "Cr_R"};
	char *const clipping[] = {"No", "EAV/SAV", "Limited range RGB/Y",
				  "Limited range Cb/Cr", "decided by register"};
	char *const round[] = {"8-bit", "10-bit", "12-bit"};
	char *const input_sel[] = {"Main (color matrix enabled)",
				   "Main (color matrix by-passed)",
				   "", "", "", "", "", "",
				   "Aux (color matrix enabled)",
				   "Aux (color matrix by-passed)",
				   "", "", "", "", "", "Force value"};

	seq_puts(s, "\t");
	mask = TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_R_SHIFT;
	r = (val & mask) >> TVO_VIP_REORDER_R_SHIFT;
	mask = TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_G_SHIFT;
	g = (val & mask) >> TVO_VIP_REORDER_G_SHIFT;
	mask = TVO_VIP_REORDER_MASK << TVO_VIP_REORDER_B_SHIFT;
	b = (val & mask) >> TVO_VIP_REORDER_B_SHIFT;
	seq_printf(s, "%-24s %s->%s %s->%s %s->%s\n", "Reorder:",
		   reorder[r], reorder[TVO_VIP_REORDER_CR_R_SEL],
		   reorder[g], reorder[TVO_VIP_REORDER_Y_G_SEL],
		   reorder[b], reorder[TVO_VIP_REORDER_CB_B_SEL]);
	seq_puts(s, "\t\t\t\t\t");
	mask = TVO_VIP_CLIP_MASK << TVO_VIP_CLIP_SHIFT;
	tmp = (val & mask) >> TVO_VIP_CLIP_SHIFT;
	seq_printf(s, "%-24s %s\n", "Clipping:", clipping[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	mask = TVO_VIP_RND_MASK << TVO_VIP_RND_SHIFT;
	tmp = (val & mask) >> TVO_VIP_RND_SHIFT;
	seq_printf(s, "%-24s input data rounded to %s per component\n",
		   "Round:", round[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & TVO_VIP_SEL_INPUT_MASK);
	seq_printf(s, "%-24s %s", "Input selection:", input_sel[tmp]);
}

static void tvout_dbg_hd_dac_cfg(struct seq_file *s, int val)
{
	seq_printf(s, "\t%-24s %s", "HD DAC:",
		   val & 1 ? "disabled" : "enabled");
}

static int tvout_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_tvout *tvout = (struct sti_tvout *)node->info_ent->data;
	struct drm_device *dev = node->minor->dev;
	struct drm_crtc *crtc;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(s, "TVOUT: (vaddr = 0x%p)", tvout->regs);

	seq_puts(s, "\n\n  HDMI encoder: ");
	crtc = tvout->hdmi->crtc;
	if (crtc) {
		seq_printf(s, "connected to %s path",
			   sti_crtc_is_main(crtc) ? "main" : "aux");
		DBGFS_DUMP(TVO_HDMI_SYNC_SEL);
		DBGFS_DUMP(TVO_VIP_HDMI);
		tvout_dbg_vip(s, readl(tvout->regs + TVO_VIP_HDMI));
	} else {
		seq_puts(s, "disabled");
	}

	seq_puts(s, "\n\n  DVO encoder: ");
	crtc = tvout->dvo->crtc;
	if (crtc) {
		seq_printf(s, "connected to %s path",
			   sti_crtc_is_main(crtc) ? "main" : "aux");
		DBGFS_DUMP(TVO_DVO_SYNC_SEL);
		DBGFS_DUMP(TVO_DVO_CONFIG);
		DBGFS_DUMP(TVO_VIP_DVO);
		tvout_dbg_vip(s, readl(tvout->regs + TVO_VIP_DVO));
	} else {
		seq_puts(s, "disabled");
	}

	seq_puts(s, "\n\n  HDA encoder: ");
	crtc = tvout->hda->crtc;
	if (crtc) {
		seq_printf(s, "connected to %s path",
			   sti_crtc_is_main(crtc) ? "main" : "aux");
		DBGFS_DUMP(TVO_HD_SYNC_SEL);
		DBGFS_DUMP(TVO_HD_DAC_CFG_OFF);
		tvout_dbg_hd_dac_cfg(s,
				     readl(tvout->regs + TVO_HD_DAC_CFG_OFF));
		DBGFS_DUMP(TVO_VIP_HDF);
		tvout_dbg_vip(s, readl(tvout->regs + TVO_VIP_HDF));
	} else {
		seq_puts(s, "disabled");
	}

	seq_puts(s, "\n\n  main path configuration");
	DBGFS_DUMP(TVO_CSC_MAIN_M0);
	DBGFS_DUMP(TVO_CSC_MAIN_M1);
	DBGFS_DUMP(TVO_CSC_MAIN_M2);
	DBGFS_DUMP(TVO_CSC_MAIN_M3);
	DBGFS_DUMP(TVO_CSC_MAIN_M4);
	DBGFS_DUMP(TVO_CSC_MAIN_M5);
	DBGFS_DUMP(TVO_CSC_MAIN_M6);
	DBGFS_DUMP(TVO_CSC_MAIN_M7);
	DBGFS_DUMP(TVO_MAIN_IN_VID_FORMAT);

	seq_puts(s, "\n\n  auxiliary path configuration");
	DBGFS_DUMP(TVO_CSC_AUX_M0);
	DBGFS_DUMP(TVO_CSC_AUX_M2);
	DBGFS_DUMP(TVO_CSC_AUX_M3);
	DBGFS_DUMP(TVO_CSC_AUX_M4);
	DBGFS_DUMP(TVO_CSC_AUX_M5);
	DBGFS_DUMP(TVO_CSC_AUX_M6);
	DBGFS_DUMP(TVO_CSC_AUX_M7);
	DBGFS_DUMP(TVO_AUX_IN_VID_FORMAT);
	seq_puts(s, "\n");

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static struct drm_info_list tvout_debugfs_files[] = {
	{ "tvout", tvout_dbg_show, 0, NULL },
};

static void tvout_debugfs_exit(struct sti_tvout *tvout, struct drm_minor *minor)
{
	drm_debugfs_remove_files(tvout_debugfs_files,
				 ARRAY_SIZE(tvout_debugfs_files),
				 minor);
}

static int tvout_debugfs_init(struct sti_tvout *tvout, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tvout_debugfs_files); i++)
		tvout_debugfs_files[i].data = tvout;

	return drm_debugfs_create_files(tvout_debugfs_files,
					ARRAY_SIZE(tvout_debugfs_files),
					minor->debugfs_root, minor);
}

static void sti_tvout_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static void sti_tvout_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
}

static void sti_tvout_encoder_destroy(struct drm_encoder *encoder)
{
	struct sti_tvout_encoder *sti_encoder = to_sti_tvout_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(sti_encoder);
}

static const struct drm_encoder_funcs sti_tvout_encoder_funcs = {
	.destroy = sti_tvout_encoder_destroy,
};

static void sti_dvo_encoder_enable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	tvout_preformatter_set_matrix(tvout, &encoder->crtc->mode);

	tvout_dvo_start(tvout, sti_crtc_is_main(encoder->crtc));
}

static void sti_dvo_encoder_disable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	/* Reset VIP register */
	tvout_write(tvout, 0x0, TVO_VIP_DVO);
}

static const struct drm_encoder_helper_funcs sti_dvo_encoder_helper_funcs = {
	.dpms = sti_tvout_encoder_dpms,
	.mode_set = sti_tvout_encoder_mode_set,
	.enable = sti_dvo_encoder_enable,
	.disable = sti_dvo_encoder_disable,
};

static struct drm_encoder *
sti_tvout_create_dvo_encoder(struct drm_device *dev,
			     struct sti_tvout *tvout)
{
	struct sti_tvout_encoder *encoder;
	struct drm_encoder *drm_encoder;

	encoder = devm_kzalloc(tvout->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return NULL;

	encoder->tvout = tvout;

	drm_encoder = (struct drm_encoder *)encoder;

	drm_encoder->possible_crtcs = ENCODER_CRTC_MASK;
	drm_encoder->possible_clones = 1 << 0;

	drm_encoder_init(dev, drm_encoder,
			 &sti_tvout_encoder_funcs, DRM_MODE_ENCODER_LVDS,
			 NULL);

	drm_encoder_helper_add(drm_encoder, &sti_dvo_encoder_helper_funcs);

	return drm_encoder;
}

static void sti_hda_encoder_enable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	tvout_preformatter_set_matrix(tvout, &encoder->crtc->mode);

	tvout_hda_start(tvout, sti_crtc_is_main(encoder->crtc));
}

static void sti_hda_encoder_disable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	/* reset VIP register */
	tvout_write(tvout, 0x0, TVO_VIP_HDF);

	/* power down HD DAC */
	tvout_write(tvout, 1, TVO_HD_DAC_CFG_OFF);
}

static const struct drm_encoder_helper_funcs sti_hda_encoder_helper_funcs = {
	.dpms = sti_tvout_encoder_dpms,
	.mode_set = sti_tvout_encoder_mode_set,
	.commit = sti_hda_encoder_enable,
	.disable = sti_hda_encoder_disable,
};

static struct drm_encoder *sti_tvout_create_hda_encoder(struct drm_device *dev,
		struct sti_tvout *tvout)
{
	struct sti_tvout_encoder *encoder;
	struct drm_encoder *drm_encoder;

	encoder = devm_kzalloc(tvout->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return NULL;

	encoder->tvout = tvout;

	drm_encoder = (struct drm_encoder *) encoder;

	drm_encoder->possible_crtcs = ENCODER_CRTC_MASK;
	drm_encoder->possible_clones = 1 << 0;

	drm_encoder_init(dev, drm_encoder,
			&sti_tvout_encoder_funcs, DRM_MODE_ENCODER_DAC, NULL);

	drm_encoder_helper_add(drm_encoder, &sti_hda_encoder_helper_funcs);

	return drm_encoder;
}

static void sti_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	tvout_preformatter_set_matrix(tvout, &encoder->crtc->mode);

	tvout_hdmi_start(tvout, sti_crtc_is_main(encoder->crtc));
}

static void sti_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct sti_tvout *tvout = to_sti_tvout(encoder);

	/* reset VIP register */
	tvout_write(tvout, 0x0, TVO_VIP_HDMI);
}

static const struct drm_encoder_helper_funcs sti_hdmi_encoder_helper_funcs = {
	.dpms = sti_tvout_encoder_dpms,
	.mode_set = sti_tvout_encoder_mode_set,
	.commit = sti_hdmi_encoder_enable,
	.disable = sti_hdmi_encoder_disable,
};

static struct drm_encoder *sti_tvout_create_hdmi_encoder(struct drm_device *dev,
		struct sti_tvout *tvout)
{
	struct sti_tvout_encoder *encoder;
	struct drm_encoder *drm_encoder;

	encoder = devm_kzalloc(tvout->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return NULL;

	encoder->tvout = tvout;

	drm_encoder = (struct drm_encoder *) encoder;

	drm_encoder->possible_crtcs = ENCODER_CRTC_MASK;
	drm_encoder->possible_clones = 1 << 1;

	drm_encoder_init(dev, drm_encoder,
			&sti_tvout_encoder_funcs, DRM_MODE_ENCODER_TMDS, NULL);

	drm_encoder_helper_add(drm_encoder, &sti_hdmi_encoder_helper_funcs);

	return drm_encoder;
}

static void sti_tvout_create_encoders(struct drm_device *dev,
		struct sti_tvout *tvout)
{
	tvout->hdmi = sti_tvout_create_hdmi_encoder(dev, tvout);
	tvout->hda = sti_tvout_create_hda_encoder(dev, tvout);
	tvout->dvo = sti_tvout_create_dvo_encoder(dev, tvout);
}

static void sti_tvout_destroy_encoders(struct sti_tvout *tvout)
{
	if (tvout->hdmi)
		drm_encoder_cleanup(tvout->hdmi);
	tvout->hdmi = NULL;

	if (tvout->hda)
		drm_encoder_cleanup(tvout->hda);
	tvout->hda = NULL;

	if (tvout->dvo)
		drm_encoder_cleanup(tvout->dvo);
	tvout->dvo = NULL;
}

static int sti_tvout_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_tvout *tvout = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	tvout->drm_dev = drm_dev;

	sti_tvout_create_encoders(drm_dev, tvout);

	if (tvout_debugfs_init(tvout, drm_dev->primary))
		DRM_ERROR("TVOUT debugfs setup failed\n");

	return 0;
}

static void sti_tvout_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct sti_tvout *tvout = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	sti_tvout_destroy_encoders(tvout);

	tvout_debugfs_exit(tvout, drm_dev->primary);
}

static const struct component_ops sti_tvout_ops = {
	.bind	= sti_tvout_bind,
	.unbind	= sti_tvout_unbind,
};

static int sti_tvout_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct sti_tvout *tvout;
	struct resource *res;

	DRM_INFO("%s\n", __func__);

	if (!node)
		return -ENODEV;

	tvout = devm_kzalloc(dev, sizeof(*tvout), GFP_KERNEL);
	if (!tvout)
		return -ENOMEM;

	tvout->dev = dev;

	/* get Memory ressources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tvout-reg");
	if (!res) {
		DRM_ERROR("Invalid glue resource\n");
		return -ENOMEM;
	}
	tvout->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!tvout->regs)
		return -ENOMEM;

	/* get reset resources */
	tvout->reset = devm_reset_control_get(dev, "tvout");
	/* take tvout out of reset */
	if (!IS_ERR(tvout->reset))
		reset_control_deassert(tvout->reset);

	platform_set_drvdata(pdev, tvout);

	return component_add(dev, &sti_tvout_ops);
}

static int sti_tvout_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sti_tvout_ops);
	return 0;
}

static const struct of_device_id tvout_of_match[] = {
	{ .compatible = "st,stih416-tvout", },
	{ .compatible = "st,stih407-tvout", },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, tvout_of_match);

struct platform_driver sti_tvout_driver = {
	.driver = {
		.name = "sti-tvout",
		.owner = THIS_MODULE,
		.of_match_table = tvout_of_match,
	},
	.probe = sti_tvout_probe,
	.remove = sti_tvout_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
