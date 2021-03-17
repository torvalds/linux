// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/* HDformatter registers */
#define HDA_ANA_CFG                     0x0000
#define HDA_ANA_SCALE_CTRL_Y            0x0004
#define HDA_ANA_SCALE_CTRL_CB           0x0008
#define HDA_ANA_SCALE_CTRL_CR           0x000C
#define HDA_ANA_ANC_CTRL                0x0010
#define HDA_ANA_SRC_Y_CFG               0x0014
#define HDA_COEFF_Y_PH1_TAP123          0x0018
#define HDA_COEFF_Y_PH1_TAP456          0x001C
#define HDA_COEFF_Y_PH2_TAP123          0x0020
#define HDA_COEFF_Y_PH2_TAP456          0x0024
#define HDA_COEFF_Y_PH3_TAP123          0x0028
#define HDA_COEFF_Y_PH3_TAP456          0x002C
#define HDA_COEFF_Y_PH4_TAP123          0x0030
#define HDA_COEFF_Y_PH4_TAP456          0x0034
#define HDA_ANA_SRC_C_CFG               0x0040
#define HDA_COEFF_C_PH1_TAP123          0x0044
#define HDA_COEFF_C_PH1_TAP456          0x0048
#define HDA_COEFF_C_PH2_TAP123          0x004C
#define HDA_COEFF_C_PH2_TAP456          0x0050
#define HDA_COEFF_C_PH3_TAP123          0x0054
#define HDA_COEFF_C_PH3_TAP456          0x0058
#define HDA_COEFF_C_PH4_TAP123          0x005C
#define HDA_COEFF_C_PH4_TAP456          0x0060
#define HDA_SYNC_AWGI                   0x0300

/* HDA_ANA_CFG */
#define CFG_AWG_ASYNC_EN                BIT(0)
#define CFG_AWG_ASYNC_HSYNC_MTD         BIT(1)
#define CFG_AWG_ASYNC_VSYNC_MTD         BIT(2)
#define CFG_AWG_SYNC_DEL                BIT(3)
#define CFG_AWG_FLTR_MODE_SHIFT         4
#define CFG_AWG_FLTR_MODE_MASK          (0xF << CFG_AWG_FLTR_MODE_SHIFT)
#define CFG_AWG_FLTR_MODE_SD            (0 << CFG_AWG_FLTR_MODE_SHIFT)
#define CFG_AWG_FLTR_MODE_ED            (1 << CFG_AWG_FLTR_MODE_SHIFT)
#define CFG_AWG_FLTR_MODE_HD            (2 << CFG_AWG_FLTR_MODE_SHIFT)
#define CFG_SYNC_ON_PBPR_MASK           BIT(8)
#define CFG_PREFILTER_EN_MASK           BIT(9)
#define CFG_PBPR_SYNC_OFF_SHIFT         16
#define CFG_PBPR_SYNC_OFF_MASK          (0x7FF << CFG_PBPR_SYNC_OFF_SHIFT)
#define CFG_PBPR_SYNC_OFF_VAL           0x117 /* Voltage dependent. stiH416 */

/* Default scaling values */
#define SCALE_CTRL_Y_DFLT               0x00C50256
#define SCALE_CTRL_CB_DFLT              0x00DB0249
#define SCALE_CTRL_CR_DFLT              0x00DB0249

/* Video DACs control */
#define DAC_CFG_HD_HZUVW_OFF_MASK       BIT(1)

/* Upsampler values for the alternative 2X Filter */
#define SAMPLER_COEF_NB                 8
#define HDA_ANA_SRC_Y_CFG_ALT_2X        0x01130000
static u32 coef_y_alt_2x[] = {
	0x00FE83FB, 0x1F900401, 0x00000000, 0x00000000,
	0x00F408F9, 0x055F7C25, 0x00000000, 0x00000000
};

#define HDA_ANA_SRC_C_CFG_ALT_2X        0x01750004
static u32 coef_c_alt_2x[] = {
	0x001305F7, 0x05274BD0, 0x00000000, 0x00000000,
	0x0004907C, 0x09C80B9D, 0x00000000, 0x00000000
};

/* Upsampler values for the 4X Filter */
#define HDA_ANA_SRC_Y_CFG_4X            0x01ED0005
#define HDA_ANA_SRC_C_CFG_4X            0x01ED0004
static u32 coef_yc_4x[] = {
	0x00FC827F, 0x008FE20B, 0x00F684FC, 0x050F7C24,
	0x00F4857C, 0x0A1F402E, 0x00FA027F, 0x0E076E1D
};

/* AWG instructions for some video modes */
#define AWG_MAX_INST                    64

/* 720p@50 */
static u32 AWGi_720p_50[] = {
	0x00000971, 0x00000C26, 0x0000013B, 0x00000CDA,
	0x00000104, 0x00000E7E, 0x00000E7F, 0x0000013B,
	0x00000D8E, 0x00000104, 0x00001804, 0x00000971,
	0x00000C26, 0x0000003B, 0x00000FB4, 0x00000FB5,
	0x00000104, 0x00001AE8
};

#define NN_720p_50 ARRAY_SIZE(AWGi_720p_50)

/* 720p@60 */
static u32 AWGi_720p_60[] = {
	0x00000971, 0x00000C26, 0x0000013B, 0x00000CDA,
	0x00000104, 0x00000E7E, 0x00000E7F, 0x0000013B,
	0x00000C44, 0x00000104, 0x00001804, 0x00000971,
	0x00000C26, 0x0000003B, 0x00000F0F, 0x00000F10,
	0x00000104, 0x00001AE8
};

#define NN_720p_60 ARRAY_SIZE(AWGi_720p_60)

/* 1080p@30 */
static u32 AWGi_1080p_30[] = {
	0x00000971, 0x00000C2A, 0x0000013B, 0x00000C56,
	0x00000104, 0x00000FDC, 0x00000FDD, 0x0000013B,
	0x00000C2A, 0x00000104, 0x00001804, 0x00000971,
	0x00000C2A, 0x0000003B, 0x00000EBE, 0x00000EBF,
	0x00000EBF, 0x00000104, 0x00001A2F, 0x00001C4B,
	0x00001C52
};

#define NN_1080p_30 ARRAY_SIZE(AWGi_1080p_30)

/* 1080p@25 */
static u32 AWGi_1080p_25[] = {
	0x00000971, 0x00000C2A, 0x0000013B, 0x00000C56,
	0x00000104, 0x00000FDC, 0x00000FDD, 0x0000013B,
	0x00000DE2, 0x00000104, 0x00001804, 0x00000971,
	0x00000C2A, 0x0000003B, 0x00000F51, 0x00000F51,
	0x00000F52, 0x00000104, 0x00001A2F, 0x00001C4B,
	0x00001C52
};

#define NN_1080p_25 ARRAY_SIZE(AWGi_1080p_25)

/* 1080p@24 */
static u32 AWGi_1080p_24[] = {
	0x00000971, 0x00000C2A, 0x0000013B, 0x00000C56,
	0x00000104, 0x00000FDC, 0x00000FDD, 0x0000013B,
	0x00000E50, 0x00000104, 0x00001804, 0x00000971,
	0x00000C2A, 0x0000003B, 0x00000F76, 0x00000F76,
	0x00000F76, 0x00000104, 0x00001A2F, 0x00001C4B,
	0x00001C52
};

#define NN_1080p_24 ARRAY_SIZE(AWGi_1080p_24)

/* 720x480p@60 */
static u32 AWGi_720x480p_60[] = {
	0x00000904, 0x00000F18, 0x0000013B, 0x00001805,
	0x00000904, 0x00000C3D, 0x0000003B, 0x00001A06
};

#define NN_720x480p_60 ARRAY_SIZE(AWGi_720x480p_60)

/* Video mode category */
enum sti_hda_vid_cat {
	VID_SD,
	VID_ED,
	VID_HD_74M,
	VID_HD_148M
};

struct sti_hda_video_config {
	struct drm_display_mode mode;
	u32 *awg_instr;
	int nb_instr;
	enum sti_hda_vid_cat vid_cat;
};

/* HD analog supported modes
 * Interlaced modes may be added when supported by the whole display chain
 */
static const struct sti_hda_video_config hda_supported_modes[] = {
	/* 1080p30 74.250Mhz */
	{{DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_1080p_30, NN_1080p_30, VID_HD_74M},
	/* 1080p30 74.176Mhz */
	{{DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74176, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_1080p_30, NN_1080p_30, VID_HD_74M},
	/* 1080p24 74.250Mhz */
	{{DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_1080p_24, NN_1080p_24, VID_HD_74M},
	/* 1080p24 74.176Mhz */
	{{DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74176, 1920, 2558,
		   2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_1080p_24, NN_1080p_24, VID_HD_74M},
	/* 1080p25 74.250Mhz */
	{{DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_1080p_25, NN_1080p_25, VID_HD_74M},
	/* 720p60 74.250Mhz */
	{{DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_720p_60, NN_720p_60, VID_HD_74M},
	/* 720p60 74.176Mhz */
	{{DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74176, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_720p_60, NN_720p_60, VID_HD_74M},
	/* 720p50 74.250Mhz */
	{{DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)},
	 AWGi_720p_50, NN_720p_50, VID_HD_74M},
	/* 720x480p60 27.027Mhz */
	{{DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27027, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)},
	 AWGi_720x480p_60, NN_720x480p_60, VID_ED},
	/* 720x480p60 27.000Mhz */
	{{DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)},
	 AWGi_720x480p_60, NN_720x480p_60, VID_ED}
};

/*
 * STI hd analog structure
 *
 * @dev: driver device
 * @drm_dev: pointer to drm device
 * @mode: current display mode selected
 * @regs: HD analog register
 * @video_dacs_ctrl: video DACS control register
 * @enabled: true if HD analog is enabled else false
 */
struct sti_hda {
	struct device dev;
	struct drm_device *drm_dev;
	struct drm_display_mode mode;
	void __iomem *regs;
	void __iomem *video_dacs_ctrl;
	struct clk *clk_pix;
	struct clk *clk_hddac;
	bool enabled;
};

struct sti_hda_connector {
	struct drm_connector drm_connector;
	struct drm_encoder *encoder;
	struct sti_hda *hda;
};

#define to_sti_hda_connector(x) \
	container_of(x, struct sti_hda_connector, drm_connector)

static u32 hda_read(struct sti_hda *hda, int offset)
{
	return readl(hda->regs + offset);
}

static void hda_write(struct sti_hda *hda, u32 val, int offset)
{
	writel(val, hda->regs + offset);
}

/**
 * Search for a video mode in the supported modes table
 *
 * @mode: mode being searched
 * @idx: index of the found mode
 *
 * Return true if mode is found
 */
static bool hda_get_mode_idx(struct drm_display_mode mode, int *idx)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hda_supported_modes); i++)
		if (drm_mode_equal(&hda_supported_modes[i].mode, &mode)) {
			*idx = i;
			return true;
		}
	return false;
}

/**
 * Enable the HD DACS
 *
 * @hda: pointer to HD analog structure
 * @enable: true if HD DACS need to be enabled, else false
 */
static void hda_enable_hd_dacs(struct sti_hda *hda, bool enable)
{
	if (hda->video_dacs_ctrl) {
		u32 val;

		val = readl(hda->video_dacs_ctrl);
		if (enable)
			val &= ~DAC_CFG_HD_HZUVW_OFF_MASK;
		else
			val |= DAC_CFG_HD_HZUVW_OFF_MASK;

		writel(val, hda->video_dacs_ctrl);
	}
}

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(hda->regs + reg))

static void hda_dbg_cfg(struct seq_file *s, int val)
{
	seq_puts(s, "\tAWG ");
	seq_puts(s, val & CFG_AWG_ASYNC_EN ? "enabled" : "disabled");
}

static void hda_dbg_awg_microcode(struct seq_file *s, void __iomem *reg)
{
	unsigned int i;

	seq_puts(s, "\n\n  HDA AWG microcode:");
	for (i = 0; i < AWG_MAX_INST; i++) {
		if (i % 8 == 0)
			seq_printf(s, "\n  %04X:", i);
		seq_printf(s, " %04X", readl(reg + i * 4));
	}
}

static void hda_dbg_video_dacs_ctrl(struct seq_file *s, void __iomem *reg)
{
	u32 val = readl(reg);

	seq_printf(s, "\n\n  %-25s 0x%08X", "VIDEO_DACS_CONTROL", val);
	seq_puts(s, "\tHD DACs ");
	seq_puts(s, val & DAC_CFG_HD_HZUVW_OFF_MASK ? "disabled" : "enabled");
}

static int hda_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_hda *hda = (struct sti_hda *)node->info_ent->data;

	seq_printf(s, "HD Analog: (vaddr = 0x%p)", hda->regs);
	DBGFS_DUMP(HDA_ANA_CFG);
	hda_dbg_cfg(s, readl(hda->regs + HDA_ANA_CFG));
	DBGFS_DUMP(HDA_ANA_SCALE_CTRL_Y);
	DBGFS_DUMP(HDA_ANA_SCALE_CTRL_CB);
	DBGFS_DUMP(HDA_ANA_SCALE_CTRL_CR);
	DBGFS_DUMP(HDA_ANA_ANC_CTRL);
	DBGFS_DUMP(HDA_ANA_SRC_Y_CFG);
	DBGFS_DUMP(HDA_ANA_SRC_C_CFG);
	hda_dbg_awg_microcode(s, hda->regs + HDA_SYNC_AWGI);
	if (hda->video_dacs_ctrl)
		hda_dbg_video_dacs_ctrl(s, hda->video_dacs_ctrl);
	seq_putc(s, '\n');
	return 0;
}

static struct drm_info_list hda_debugfs_files[] = {
	{ "hda", hda_dbg_show, 0, NULL },
};

static void hda_debugfs_init(struct sti_hda *hda, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hda_debugfs_files); i++)
		hda_debugfs_files[i].data = hda;

	drm_debugfs_create_files(hda_debugfs_files,
				 ARRAY_SIZE(hda_debugfs_files),
				 minor->debugfs_root, minor);
}

/**
 * Configure AWG, writing instructions
 *
 * @hda: pointer to HD analog structure
 * @awg_instr: pointer to AWG instructions table
 * @nb: nb of AWG instructions
 */
static void sti_hda_configure_awg(struct sti_hda *hda, u32 *awg_instr, int nb)
{
	unsigned int i;

	DRM_DEBUG_DRIVER("\n");

	for (i = 0; i < nb; i++)
		hda_write(hda, awg_instr[i], HDA_SYNC_AWGI + i * 4);
	for (i = nb; i < AWG_MAX_INST; i++)
		hda_write(hda, 0, HDA_SYNC_AWGI + i * 4);
}

static void sti_hda_disable(struct drm_bridge *bridge)
{
	struct sti_hda *hda = bridge->driver_private;
	u32 val;

	if (!hda->enabled)
		return;

	DRM_DEBUG_DRIVER("\n");

	/* Disable HD DAC and AWG */
	val = hda_read(hda, HDA_ANA_CFG);
	val &= ~CFG_AWG_ASYNC_EN;
	hda_write(hda, val, HDA_ANA_CFG);
	hda_write(hda, 0, HDA_ANA_ANC_CTRL);

	hda_enable_hd_dacs(hda, false);

	/* Disable/unprepare hda clock */
	clk_disable_unprepare(hda->clk_hddac);
	clk_disable_unprepare(hda->clk_pix);

	hda->enabled = false;
}

static void sti_hda_pre_enable(struct drm_bridge *bridge)
{
	struct sti_hda *hda = bridge->driver_private;
	u32 val, i, mode_idx;
	u32 src_filter_y, src_filter_c;
	u32 *coef_y, *coef_c;
	u32 filter_mode;

	DRM_DEBUG_DRIVER("\n");

	if (hda->enabled)
		return;

	/* Prepare/enable clocks */
	if (clk_prepare_enable(hda->clk_pix))
		DRM_ERROR("Failed to prepare/enable hda_pix clk\n");
	if (clk_prepare_enable(hda->clk_hddac))
		DRM_ERROR("Failed to prepare/enable hda_hddac clk\n");

	if (!hda_get_mode_idx(hda->mode, &mode_idx)) {
		DRM_ERROR("Undefined mode\n");
		return;
	}

	switch (hda_supported_modes[mode_idx].vid_cat) {
	case VID_HD_148M:
		DRM_ERROR("Beyond HD analog capabilities\n");
		return;
	case VID_HD_74M:
		/* HD use alternate 2x filter */
		filter_mode = CFG_AWG_FLTR_MODE_HD;
		src_filter_y = HDA_ANA_SRC_Y_CFG_ALT_2X;
		src_filter_c = HDA_ANA_SRC_C_CFG_ALT_2X;
		coef_y = coef_y_alt_2x;
		coef_c = coef_c_alt_2x;
		break;
	case VID_ED:
		/* ED uses 4x filter */
		filter_mode = CFG_AWG_FLTR_MODE_ED;
		src_filter_y = HDA_ANA_SRC_Y_CFG_4X;
		src_filter_c = HDA_ANA_SRC_C_CFG_4X;
		coef_y = coef_yc_4x;
		coef_c = coef_yc_4x;
		break;
	case VID_SD:
		DRM_ERROR("Not supported\n");
		return;
	default:
		DRM_ERROR("Undefined resolution\n");
		return;
	}
	DRM_DEBUG_DRIVER("Using HDA mode #%d\n", mode_idx);

	/* Enable HD Video DACs */
	hda_enable_hd_dacs(hda, true);

	/* Configure scaler */
	hda_write(hda, SCALE_CTRL_Y_DFLT, HDA_ANA_SCALE_CTRL_Y);
	hda_write(hda, SCALE_CTRL_CB_DFLT, HDA_ANA_SCALE_CTRL_CB);
	hda_write(hda, SCALE_CTRL_CR_DFLT, HDA_ANA_SCALE_CTRL_CR);

	/* Configure sampler */
	hda_write(hda , src_filter_y, HDA_ANA_SRC_Y_CFG);
	hda_write(hda, src_filter_c,  HDA_ANA_SRC_C_CFG);
	for (i = 0; i < SAMPLER_COEF_NB; i++) {
		hda_write(hda, coef_y[i], HDA_COEFF_Y_PH1_TAP123 + i * 4);
		hda_write(hda, coef_c[i], HDA_COEFF_C_PH1_TAP123 + i * 4);
	}

	/* Configure main HDFormatter */
	val = 0;
	val |= (hda->mode.flags & DRM_MODE_FLAG_INTERLACE) ?
	    0 : CFG_AWG_ASYNC_VSYNC_MTD;
	val |= (CFG_PBPR_SYNC_OFF_VAL << CFG_PBPR_SYNC_OFF_SHIFT);
	val |= filter_mode;
	hda_write(hda, val, HDA_ANA_CFG);

	/* Configure AWG */
	sti_hda_configure_awg(hda, hda_supported_modes[mode_idx].awg_instr,
			      hda_supported_modes[mode_idx].nb_instr);

	/* Enable AWG */
	val = hda_read(hda, HDA_ANA_CFG);
	val |= CFG_AWG_ASYNC_EN;
	hda_write(hda, val, HDA_ANA_CFG);

	hda->enabled = true;
}

static void sti_hda_set_mode(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode)
{
	struct sti_hda *hda = bridge->driver_private;
	u32 mode_idx;
	int hddac_rate;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	memcpy(&hda->mode, mode, sizeof(struct drm_display_mode));

	if (!hda_get_mode_idx(hda->mode, &mode_idx)) {
		DRM_ERROR("Undefined mode\n");
		return;
	}

	switch (hda_supported_modes[mode_idx].vid_cat) {
	case VID_HD_74M:
		/* HD use alternate 2x filter */
		hddac_rate = mode->clock * 1000 * 2;
		break;
	case VID_ED:
		/* ED uses 4x filter */
		hddac_rate = mode->clock * 1000 * 4;
		break;
	default:
		DRM_ERROR("Undefined mode\n");
		return;
	}

	/* HD DAC = 148.5Mhz or 108 Mhz */
	ret = clk_set_rate(hda->clk_hddac, hddac_rate);
	if (ret < 0)
		DRM_ERROR("Cannot set rate (%dHz) for hda_hddac clk\n",
			  hddac_rate);

	/* HDformatter clock = compositor clock */
	ret = clk_set_rate(hda->clk_pix, mode->clock * 1000);
	if (ret < 0)
		DRM_ERROR("Cannot set rate (%dHz) for hda_pix clk\n",
			  mode->clock * 1000);
}

static void sti_hda_bridge_nope(struct drm_bridge *bridge)
{
	/* do nothing */
}

static const struct drm_bridge_funcs sti_hda_bridge_funcs = {
	.pre_enable = sti_hda_pre_enable,
	.enable = sti_hda_bridge_nope,
	.disable = sti_hda_disable,
	.post_disable = sti_hda_bridge_nope,
	.mode_set = sti_hda_set_mode,
};

static int sti_hda_connector_get_modes(struct drm_connector *connector)
{
	unsigned int i;
	int count = 0;
	struct sti_hda_connector *hda_connector
		= to_sti_hda_connector(connector);
	struct sti_hda *hda = hda_connector->hda;

	DRM_DEBUG_DRIVER("\n");

	for (i = 0; i < ARRAY_SIZE(hda_supported_modes); i++) {
		struct drm_display_mode *mode =
			drm_mode_duplicate(hda->drm_dev,
					&hda_supported_modes[i].mode);
		if (!mode)
			continue;

		/* the first mode is the preferred mode */
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

#define CLK_TOLERANCE_HZ 50

static int sti_hda_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	int target = mode->clock * 1000;
	int target_min = target - CLK_TOLERANCE_HZ;
	int target_max = target + CLK_TOLERANCE_HZ;
	int result;
	int idx;
	struct sti_hda_connector *hda_connector
		= to_sti_hda_connector(connector);
	struct sti_hda *hda = hda_connector->hda;

	if (!hda_get_mode_idx(*mode, &idx)) {
		return MODE_BAD;
	} else {
		result = clk_round_rate(hda->clk_pix, target);

		DRM_DEBUG_DRIVER("target rate = %d => available rate = %d\n",
				 target, result);

		if ((result < target_min) || (result > target_max)) {
			DRM_DEBUG_DRIVER("hda pixclk=%d not supported\n",
					 target);
			return MODE_BAD;
		}
	}

	return MODE_OK;
}

static const
struct drm_connector_helper_funcs sti_hda_connector_helper_funcs = {
	.get_modes = sti_hda_connector_get_modes,
	.mode_valid = sti_hda_connector_mode_valid,
};

static int sti_hda_late_register(struct drm_connector *connector)
{
	struct sti_hda_connector *hda_connector
		= to_sti_hda_connector(connector);
	struct sti_hda *hda = hda_connector->hda;

	hda_debugfs_init(hda, hda->drm_dev->primary);

	return 0;
}

static const struct drm_connector_funcs sti_hda_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = sti_hda_late_register,
};

static struct drm_encoder *sti_hda_find_encoder(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_DAC)
			return encoder;
	}

	return NULL;
}

static int sti_hda_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_hda *hda = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct sti_hda_connector *connector;
	struct drm_connector *drm_connector;
	struct drm_bridge *bridge;
	int err;

	/* Set the drm device handle */
	hda->drm_dev = drm_dev;

	encoder = sti_hda_find_encoder(drm_dev);
	if (!encoder)
		return -ENOMEM;

	connector = devm_kzalloc(dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	connector->hda = hda;

		bridge = devm_kzalloc(dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	bridge->driver_private = hda;
	bridge->funcs = &sti_hda_bridge_funcs;
	drm_bridge_attach(encoder, bridge, NULL, 0);

	connector->encoder = encoder;

	drm_connector = (struct drm_connector *)connector;

	drm_connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_init(drm_dev, drm_connector,
			&sti_hda_connector_funcs, DRM_MODE_CONNECTOR_Component);
	drm_connector_helper_add(drm_connector,
			&sti_hda_connector_helper_funcs);

	err = drm_connector_attach_encoder(drm_connector, encoder);
	if (err) {
		DRM_ERROR("Failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	/* force to disable hd dacs at startup */
	hda_enable_hd_dacs(hda, false);

	return 0;

err_sysfs:
	return -EINVAL;
}

static void sti_hda_unbind(struct device *dev,
		struct device *master, void *data)
{
}

static const struct component_ops sti_hda_ops = {
	.bind = sti_hda_bind,
	.unbind = sti_hda_unbind,
};

static int sti_hda_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sti_hda *hda;
	struct resource *res;

	DRM_INFO("%s\n", __func__);

	hda = devm_kzalloc(dev, sizeof(*hda), GFP_KERNEL);
	if (!hda)
		return -ENOMEM;

	hda->dev = pdev->dev;

	/* Get resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hda-reg");
	if (!res) {
		DRM_ERROR("Invalid hda resource\n");
		return -ENOMEM;
	}
	hda->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!hda->regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
			"video-dacs-ctrl");
	if (res) {
		hda->video_dacs_ctrl = devm_ioremap(dev, res->start,
				resource_size(res));
		if (!hda->video_dacs_ctrl)
			return -ENOMEM;
	} else {
		/* If no existing video-dacs-ctrl resource continue the probe */
		DRM_DEBUG_DRIVER("No video-dacs-ctrl resource\n");
		hda->video_dacs_ctrl = NULL;
	}

	/* Get clock resources */
	hda->clk_pix = devm_clk_get(dev, "pix");
	if (IS_ERR(hda->clk_pix)) {
		DRM_ERROR("Cannot get hda_pix clock\n");
		return PTR_ERR(hda->clk_pix);
	}

	hda->clk_hddac = devm_clk_get(dev, "hddac");
	if (IS_ERR(hda->clk_hddac)) {
		DRM_ERROR("Cannot get hda_hddac clock\n");
		return PTR_ERR(hda->clk_hddac);
	}

	platform_set_drvdata(pdev, hda);

	return component_add(&pdev->dev, &sti_hda_ops);
}

static int sti_hda_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sti_hda_ops);
	return 0;
}

static const struct of_device_id hda_of_match[] = {
	{ .compatible = "st,stih416-hda", },
	{ .compatible = "st,stih407-hda", },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, hda_of_match);

struct platform_driver sti_hda_driver = {
	.driver = {
		.name = "sti-hda",
		.owner = THIS_MODULE,
		.of_match_table = hda_of_match,
	},
	.probe = sti_hda_probe,
	.remove = sti_hda_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
