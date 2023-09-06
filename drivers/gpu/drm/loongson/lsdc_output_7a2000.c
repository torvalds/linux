// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/delay.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "lsdc_drv.h"
#include "lsdc_output.h"

/*
 * The display controller in LS7A2000 has two display pipes
 * Display pipe 0 is attached with a built-in transparent VGA encoder and
 * a built-in HDMI encoder.
 * Display pipe 1 has only one built-in HDMI encoder connected.
 *       ______________________                          _____________
 *      |             +-----+  |                        |             |
 *      | CRTC0 -+--> | VGA |  ----> VGA Connector ---> | VGA Monitor |<---+
 *      |        |    +-----+  |                        |_____________|    |
 *      |        |             |                         ______________    |
 *      |        |    +------+ |                        |              |   |
 *      |        +--> | HDMI | ----> HDMI Connector --> | HDMI Monitor |<--+
 *      |             +------+ |                        |______________|   |
 *      |            +------+  |                                           |
 *      |            | i2c6 |  <-------------------------------------------+
 *      |            +------+  |
 *      |                      |
 *      |    DC in LS7A2000    |
 *      |                      |
 *      |            +------+  |
 *      |            | i2c7 |  <--------------------------------+
 *      |            +------+  |                                |
 *      |                      |                          ______|_______
 *      |            +------+  |                         |              |
 *      | CRTC1 ---> | HDMI |  ----> HDMI Connector ---> | HDMI Monitor |
 *      |            +------+  |                         |______________|
 *      |______________________|
 */

static int ls7a2000_connector_get_modes(struct drm_connector *connector)
{
	unsigned int num = 0;
	struct edid *edid;

	if (connector->ddc) {
		edid = drm_get_edid(connector, connector->ddc);
		if (edid) {
			drm_connector_update_edid_property(connector, edid);
			num = drm_add_edid_modes(connector, edid);
			kfree(edid);
		}

		return num;
	}

	num = drm_add_modes_noedid(connector, 1920, 1200);

	drm_set_preferred_mode(connector, 1024, 768);

	return num;
}

static struct drm_encoder *
ls7a2000_connector_get_best_encoder(struct drm_connector *connector,
				    struct drm_atomic_state *state)
{
	struct lsdc_output *output = connector_to_lsdc_output(connector);

	return &output->encoder;
}

static const struct drm_connector_helper_funcs ls7a2000_connector_helpers = {
	.atomic_best_encoder = ls7a2000_connector_get_best_encoder,
	.get_modes = ls7a2000_connector_get_modes,
};

/* debugfs */

#define LSDC_HDMI_REG(i, reg) {                               \
	.name = __stringify_1(LSDC_HDMI##i##_##reg##_REG),    \
	.offset = LSDC_HDMI##i##_##reg##_REG,                 \
}

static const struct lsdc_reg32 ls7a2000_hdmi0_encoder_regs[] = {
	LSDC_HDMI_REG(0, ZONE),
	LSDC_HDMI_REG(0, INTF_CTRL),
	LSDC_HDMI_REG(0, PHY_CTRL),
	LSDC_HDMI_REG(0, PHY_PLL),
	LSDC_HDMI_REG(0, AVI_INFO_CRTL),
	LSDC_HDMI_REG(0, PHY_CAL),
	LSDC_HDMI_REG(0, AUDIO_PLL_LO),
	LSDC_HDMI_REG(0, AUDIO_PLL_HI),
	{NULL, 0},  /* MUST be {NULL, 0} terminated */
};

static const struct lsdc_reg32 ls7a2000_hdmi1_encoder_regs[] = {
	LSDC_HDMI_REG(1, ZONE),
	LSDC_HDMI_REG(1, INTF_CTRL),
	LSDC_HDMI_REG(1, PHY_CTRL),
	LSDC_HDMI_REG(1, PHY_PLL),
	LSDC_HDMI_REG(1, AVI_INFO_CRTL),
	LSDC_HDMI_REG(1, PHY_CAL),
	LSDC_HDMI_REG(1, AUDIO_PLL_LO),
	LSDC_HDMI_REG(1, AUDIO_PLL_HI),
	{NULL, 0},  /* MUST be {NULL, 0} terminated */
};

static int ls7a2000_hdmi_encoder_regs_show(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_reg32 *preg;

	preg = (const struct lsdc_reg32 *)node->info_ent->data;

	while (preg->name) {
		u32 offset = preg->offset;

		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   preg->name, offset, lsdc_rreg32(ldev, offset));
		++preg;
	}

	return 0;
}

static const struct drm_info_list ls7a2000_hdmi0_debugfs_files[] = {
	{ "regs", ls7a2000_hdmi_encoder_regs_show, 0, (void *)ls7a2000_hdmi0_encoder_regs },
};

static const struct drm_info_list ls7a2000_hdmi1_debugfs_files[] = {
	{ "regs", ls7a2000_hdmi_encoder_regs_show, 0, (void *)ls7a2000_hdmi1_encoder_regs },
};

static void ls7a2000_hdmi0_late_register(struct drm_connector *connector,
					 struct dentry *root)
{
	struct drm_device *ddev = connector->dev;
	struct drm_minor *minor = ddev->primary;

	drm_debugfs_create_files(ls7a2000_hdmi0_debugfs_files,
				 ARRAY_SIZE(ls7a2000_hdmi0_debugfs_files),
				 root, minor);
}

static void ls7a2000_hdmi1_late_register(struct drm_connector *connector,
					 struct dentry *root)
{
	struct drm_device *ddev = connector->dev;
	struct drm_minor *minor = ddev->primary;

	drm_debugfs_create_files(ls7a2000_hdmi1_debugfs_files,
				 ARRAY_SIZE(ls7a2000_hdmi1_debugfs_files),
				 root, minor);
}

/* monitor present detection */

static enum drm_connector_status
ls7a2000_hdmi0_vga_connector_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *ddev = connector->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_HDMI_HPD_STATUS_REG);

	if (val & HDMI0_HPD_FLAG)
		return connector_status_connected;

	if (connector->ddc) {
		if (drm_probe_ddc(connector->ddc))
			return connector_status_connected;

		return connector_status_disconnected;
	}

	return connector_status_unknown;
}

static enum drm_connector_status
ls7a2000_hdmi1_connector_detect(struct drm_connector *connector, bool force)
{
	struct lsdc_device *ldev = to_lsdc(connector->dev);
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_HDMI_HPD_STATUS_REG);

	if (val & HDMI1_HPD_FLAG)
		return connector_status_connected;

	return connector_status_disconnected;
}

static const struct drm_connector_funcs ls7a2000_hdmi_connector_funcs[2] = {
	{
		.detect = ls7a2000_hdmi0_vga_connector_detect,
		.fill_modes = drm_helper_probe_single_connector_modes,
		.destroy = drm_connector_cleanup,
		.reset = drm_atomic_helper_connector_reset,
		.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
		.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
		.debugfs_init = ls7a2000_hdmi0_late_register,
	},
	{
		.detect = ls7a2000_hdmi1_connector_detect,
		.fill_modes = drm_helper_probe_single_connector_modes,
		.destroy = drm_connector_cleanup,
		.reset = drm_atomic_helper_connector_reset,
		.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
		.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
		.debugfs_init = ls7a2000_hdmi1_late_register,
	},
};

/* Even though some board has only one hdmi on display pipe 1,
 * We still need hook lsdc_encoder_funcs up on display pipe 0,
 * This is because we need its reset() callback get called, to
 * set the LSDC_HDMIx_CTRL_REG using software gpio emulated i2c.
 * Otherwise, the firmware may set LSDC_HDMIx_CTRL_REG blindly.
 */
static void ls7a2000_hdmi0_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	val = PHY_CLOCK_POL | PHY_CLOCK_EN | PHY_DATA_EN;
	lsdc_wreg32(ldev, LSDC_CRTC0_DVO_CONF_REG, val);

	/* using software gpio emulated i2c */
	val = lsdc_rreg32(ldev, LSDC_HDMI0_INTF_CTRL_REG);
	val &= ~HW_I2C_EN;
	lsdc_wreg32(ldev, LSDC_HDMI0_INTF_CTRL_REG, val);

	/* help the hdmi phy to get out of reset state */
	lsdc_wreg32(ldev, LSDC_HDMI0_PHY_CTRL_REG, HDMI_PHY_RESET_N);

	mdelay(20);

	drm_dbg(ddev, "HDMI-0 Reset\n");
}

static void ls7a2000_hdmi1_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	val = PHY_CLOCK_POL | PHY_CLOCK_EN | PHY_DATA_EN;
	lsdc_wreg32(ldev, LSDC_CRTC1_DVO_CONF_REG, val);

	/* using software gpio emulated i2c */
	val = lsdc_rreg32(ldev, LSDC_HDMI1_INTF_CTRL_REG);
	val &= ~HW_I2C_EN;
	lsdc_wreg32(ldev, LSDC_HDMI1_INTF_CTRL_REG, val);

	/*  help the hdmi phy to get out of reset state */
	lsdc_wreg32(ldev, LSDC_HDMI1_PHY_CTRL_REG, HDMI_PHY_RESET_N);

	mdelay(20);

	drm_dbg(ddev, "HDMI-1 Reset\n");
}

static const struct drm_encoder_funcs ls7a2000_encoder_funcs[2] = {
	{
		.reset = ls7a2000_hdmi0_encoder_reset,
		.destroy = drm_encoder_cleanup,
	},
	{
		.reset = ls7a2000_hdmi1_encoder_reset,
		.destroy = drm_encoder_cleanup,
	},
};

static int ls7a2000_hdmi_set_avi_infoframe(struct drm_encoder *encoder,
					   struct drm_display_mode *mode)
{
	struct lsdc_output *output = encoder_to_lsdc_output(encoder);
	struct lsdc_display_pipe *dispipe = output_to_display_pipe(output);
	unsigned int index = dispipe->index;
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct hdmi_avi_infoframe infoframe;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	unsigned char *ptr = &buffer[HDMI_INFOFRAME_HEADER_SIZE];
	unsigned int content0, content1, content2, content3;
	int err;

	err = drm_hdmi_avi_infoframe_from_display_mode(&infoframe,
						       &output->connector,
						       mode);
	if (err < 0) {
		drm_err(ddev, "failed to setup AVI infoframe: %d\n", err);
		return err;
	}

	/* Fixed infoframe configuration not linked to the mode */
	infoframe.colorspace = HDMI_COLORSPACE_RGB;
	infoframe.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
	infoframe.colorimetry = HDMI_COLORIMETRY_NONE;

	err = hdmi_avi_infoframe_pack(&infoframe, buffer, sizeof(buffer));
	if (err < 0) {
		drm_err(ddev, "failed to pack AVI infoframe: %d\n", err);
			return err;
	}

	content0 = *(unsigned int *)ptr;
	content1 = *(ptr + 4);
	content2 = *(unsigned int *)(ptr + 5);
	content3 = *(unsigned int *)(ptr + 9);

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_AVI_CONTENT0, index, content0);
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_AVI_CONTENT1, index, content1);
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_AVI_CONTENT2, index, content2);
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_AVI_CONTENT3, index, content3);

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_AVI_INFO_CRTL_REG, index,
			 AVI_PKT_ENABLE | AVI_PKT_UPDATE);

	drm_dbg(ddev, "Update HDMI-%u avi infoframe\n", index);

	return 0;
}

static void ls7a2000_hdmi_atomic_disable(struct drm_encoder *encoder,
					 struct drm_atomic_state *state)
{
	struct lsdc_output *output = encoder_to_lsdc_output(encoder);
	struct lsdc_display_pipe *dispipe = output_to_display_pipe(output);
	unsigned int index = dispipe->index;
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	u32 val;

	/* Disable the hdmi phy */
	val = lsdc_pipe_rreg32(ldev, LSDC_HDMI0_PHY_CTRL_REG, index);
	val &= ~HDMI_PHY_EN;
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_CTRL_REG, index, val);

	/* Disable the hdmi interface */
	val = lsdc_pipe_rreg32(ldev, LSDC_HDMI0_INTF_CTRL_REG, index);
	val &= ~HDMI_INTERFACE_EN;
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_INTF_CTRL_REG, index, val);

	drm_dbg(ddev, "HDMI-%u disabled\n", index);
}

static void ls7a2000_hdmi_atomic_enable(struct drm_encoder *encoder,
					struct drm_atomic_state *state)
{
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct lsdc_output *output = encoder_to_lsdc_output(encoder);
	struct lsdc_display_pipe *dispipe = output_to_display_pipe(output);
	unsigned int index = dispipe->index;
	u32 val;

	/* datasheet say it should larger than 48 */
	val = 64 << HDMI_H_ZONE_IDLE_SHIFT | 64 << HDMI_V_ZONE_IDLE_SHIFT;

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_ZONE_REG, index, val);

	val = HDMI_PHY_TERM_STATUS |
	      HDMI_PHY_TERM_DET_EN |
	      HDMI_PHY_TERM_H_EN |
	      HDMI_PHY_TERM_L_EN |
	      HDMI_PHY_RESET_N |
	      HDMI_PHY_EN;

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_CTRL_REG, index, val);

	udelay(2);

	val = HDMI_CTL_PERIOD_MODE |
	      HDMI_AUDIO_EN |
	      HDMI_PACKET_EN |
	      HDMI_INTERFACE_EN |
	      (8 << HDMI_VIDEO_PREAMBLE_SHIFT);

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_INTF_CTRL_REG, index, val);

	drm_dbg(ddev, "HDMI-%u enabled\n", index);
}

/*
 *  Fout = M * Fin
 *
 *  M = (4 * LF) / (IDF * ODF)
 *
 *  IDF: Input Division Factor
 *  ODF: Output Division Factor
 *   LF: Loop Factor
 *    M: Required Mult
 *
 *  +--------------------------------------------------------+
 *  |     Fin (kHZ)     | M  | IDF | LF | ODF |   Fout(Mhz)  |
 *  |-------------------+----+-----+----+-----+--------------|
 *  |  170000 ~ 340000  | 10 | 16  | 40 |  1  | 1700 ~ 3400  |
 *  |   85000 ~ 170000  | 10 |  8  | 40 |  2  |  850 ~ 1700  |
 *  |   42500 ~  85000  | 10 |  4  | 40 |  4  |  425 ~ 850   |
 *  |   21250 ~  42500  | 10 |  2  | 40 |  8  | 212.5 ~ 425  |
 *  |   20000 ~  21250  | 10 |  1  | 40 | 16  |  200 ~ 212.5 |
 *  +--------------------------------------------------------+
 */
static void ls7a2000_hdmi_phy_pll_config(struct lsdc_device *ldev,
					 int fin,
					 unsigned int index)
{
	struct drm_device *ddev = &ldev->base;
	int count = 0;
	u32 val;

	/* Firstly, disable phy pll */
	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_PLL_REG, index, 0x0);

	/*
	 * Most of time, loongson HDMI require M = 10
	 * for example, 10 = (4 * 40) / (8 * 2)
	 * here, write "1" to the ODF will get "2"
	 */

	if (fin >= 170000)
		val = (16 << HDMI_PLL_IDF_SHIFT) |
		      (40 << HDMI_PLL_LF_SHIFT) |
		      (0 << HDMI_PLL_ODF_SHIFT);
	else if (fin >= 85000)
		val = (8 << HDMI_PLL_IDF_SHIFT) |
		      (40 << HDMI_PLL_LF_SHIFT) |
		      (1 << HDMI_PLL_ODF_SHIFT);
	else if (fin >= 42500)
		val = (4 << HDMI_PLL_IDF_SHIFT) |
		      (40 << HDMI_PLL_LF_SHIFT) |
		      (2 << HDMI_PLL_ODF_SHIFT);
	else if  (fin >= 21250)
		val = (2 << HDMI_PLL_IDF_SHIFT) |
		      (40 << HDMI_PLL_LF_SHIFT) |
		      (3 << HDMI_PLL_ODF_SHIFT);
	else
		val = (1 << HDMI_PLL_IDF_SHIFT) |
		      (40 << HDMI_PLL_LF_SHIFT) |
		      (4 << HDMI_PLL_ODF_SHIFT);

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_PLL_REG, index, val);

	val |= HDMI_PLL_ENABLE;

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_PLL_REG, index, val);

	udelay(2);

	drm_dbg(ddev, "Fin of HDMI-%u: %d kHz\n", index, fin);

	/* Wait hdmi phy pll lock */
	do {
		val = lsdc_pipe_rreg32(ldev, LSDC_HDMI0_PHY_PLL_REG, index);

		if (val & HDMI_PLL_LOCKED) {
			drm_dbg(ddev, "Setting HDMI-%u PLL take %d cycles\n",
				index, count);
			break;
		}
		++count;
	} while (count < 1000);

	lsdc_pipe_wreg32(ldev, LSDC_HDMI0_PHY_CAL_REG, index, 0x0f000ff0);

	if (count >= 1000)
		drm_err(ddev, "Setting HDMI-%u PLL failed\n", index);
}

static void ls7a2000_hdmi_atomic_mode_set(struct drm_encoder *encoder,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct lsdc_output *output = encoder_to_lsdc_output(encoder);
	struct lsdc_display_pipe *dispipe = output_to_display_pipe(output);
	unsigned int index = dispipe->index;
	struct drm_device *ddev = encoder->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_display_mode *mode = &crtc_state->mode;

	ls7a2000_hdmi_phy_pll_config(ldev, mode->clock, index);

	ls7a2000_hdmi_set_avi_infoframe(encoder, mode);

	drm_dbg(ddev, "%s modeset finished\n", encoder->name);
}

static const struct drm_encoder_helper_funcs ls7a2000_encoder_helper_funcs = {
	.atomic_disable = ls7a2000_hdmi_atomic_disable,
	.atomic_enable = ls7a2000_hdmi_atomic_enable,
	.atomic_mode_set = ls7a2000_hdmi_atomic_mode_set,
};

/*
 * For LS7A2000:
 *
 * 1) Most of board export one vga + hdmi output interface.
 * 2) Yet, Some boards export double hdmi output interface.
 * 3) Still have boards export three output(2 hdmi + 1 vga).
 *
 * So let's hook hdmi helper funcs to all display pipe, don't miss.
 * writing hdmi register do no harms.
 */
int ls7a2000_output_init(struct drm_device *ddev,
			 struct lsdc_display_pipe *dispipe,
			 struct i2c_adapter *ddc,
			 unsigned int pipe)
{
	struct lsdc_output *output = &dispipe->output;
	struct drm_encoder *encoder = &output->encoder;
	struct drm_connector *connector = &output->connector;
	int ret;

	ret = drm_encoder_init(ddev, encoder, &ls7a2000_encoder_funcs[pipe],
			       DRM_MODE_ENCODER_TMDS, "encoder-%u", pipe);
	if (ret)
		return ret;

	encoder->possible_crtcs = BIT(pipe);

	drm_encoder_helper_add(encoder, &ls7a2000_encoder_helper_funcs);

	ret = drm_connector_init_with_ddc(ddev, connector,
					  &ls7a2000_hdmi_connector_funcs[pipe],
					  DRM_MODE_CONNECTOR_HDMIA, ddc);
	if (ret)
		return ret;

	drm_info(ddev, "display pipe-%u has HDMI %s\n", pipe, pipe ? "" : "and/or VGA");

	drm_connector_helper_add(connector, &ls7a2000_connector_helpers);

	drm_connector_attach_encoder(connector, encoder);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	return 0;
}
