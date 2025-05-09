// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "sti_awg_utils.h"
#include "sti_drv.h"
#include "sti_mixer.h"

/* DVO registers */
#define DVO_AWG_DIGSYNC_CTRL      0x0000
#define DVO_DOF_CFG               0x0004
#define DVO_LUT_PROG_LOW          0x0008
#define DVO_LUT_PROG_MID          0x000C
#define DVO_LUT_PROG_HIGH         0x0010
#define DVO_DIGSYNC_INSTR_I       0x0100

#define DVO_AWG_CTRL_EN           BIT(0)
#define DVO_AWG_FRAME_BASED_SYNC  BIT(2)

#define DVO_DOF_EN_LOWBYTE        BIT(0)
#define DVO_DOF_EN_MIDBYTE        BIT(1)
#define DVO_DOF_EN_HIGHBYTE       BIT(2)
#define DVO_DOF_EN                BIT(6)
#define DVO_DOF_MOD_COUNT_SHIFT   8

#define DVO_LUT_ZERO              0
#define DVO_LUT_Y_G               1
#define DVO_LUT_Y_G_DEL           2
#define DVO_LUT_CB_B              3
#define DVO_LUT_CB_B_DEL          4
#define DVO_LUT_CR_R              5
#define DVO_LUT_CR_R_DEL          6
#define DVO_LUT_HOLD              7

struct dvo_config {
	u32 flags;
	u32 lowbyte;
	u32 midbyte;
	u32 highbyte;
	int (*awg_fwgen_fct)(
			struct awg_code_generation_params *fw_gen_params,
			struct awg_timing *timing);
};

static struct dvo_config rgb_24bit_de_cfg = {
	.flags         = (0L << DVO_DOF_MOD_COUNT_SHIFT),
	.lowbyte       = DVO_LUT_CR_R,
	.midbyte       = DVO_LUT_Y_G,
	.highbyte      = DVO_LUT_CB_B,
	.awg_fwgen_fct = sti_awg_generate_code_data_enable_mode,
};

/*
 * STI digital video output structure
 *
 * @dev: driver device
 * @drm_dev: pointer to drm device
 * @mode: current display mode selected
 * @regs: dvo registers
 * @clk_pix: pixel clock for dvo
 * @clk: clock for dvo
 * @clk_main_parent: dvo parent clock if main path used
 * @clk_aux_parent: dvo parent clock if aux path used
 * @panel_node: panel node reference from device tree
 * @panel: reference to the panel connected to the dvo
 * @enabled: true if dvo is enabled else false
 * @encoder: drm_encoder it is bound
 */
struct sti_dvo {
	struct device dev;
	struct drm_device *drm_dev;
	struct drm_display_mode mode;
	void __iomem *regs;
	struct clk *clk_pix;
	struct clk *clk;
	struct clk *clk_main_parent;
	struct clk *clk_aux_parent;
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct dvo_config *config;
	bool enabled;
	struct drm_encoder *encoder;
	struct drm_bridge bridge;
};

struct sti_dvo_connector {
	struct drm_connector drm_connector;
	struct drm_encoder *encoder;
	struct sti_dvo *dvo;
};

#define to_sti_dvo_connector(x) \
	container_of(x, struct sti_dvo_connector, drm_connector)

#define BLANKING_LEVEL 16
static int dvo_awg_generate_code(struct sti_dvo *dvo, u8 *ram_size, u32 *ram_code)
{
	struct drm_display_mode *mode = &dvo->mode;
	struct dvo_config *config = dvo->config;
	struct awg_code_generation_params fw_gen_params;
	struct awg_timing timing;

	fw_gen_params.ram_code = ram_code;
	fw_gen_params.instruction_offset = 0;

	timing.total_lines = mode->vtotal;
	timing.active_lines = mode->vdisplay;
	timing.blanking_lines = mode->vsync_start - mode->vdisplay;
	timing.trailing_lines = mode->vtotal - mode->vsync_start;
	timing.total_pixels = mode->htotal;
	timing.active_pixels = mode->hdisplay;
	timing.blanking_pixels = mode->hsync_start - mode->hdisplay;
	timing.trailing_pixels = mode->htotal - mode->hsync_start;
	timing.blanking_level = BLANKING_LEVEL;

	if (config->awg_fwgen_fct(&fw_gen_params, &timing)) {
		DRM_ERROR("AWG firmware not properly generated\n");
		return -EINVAL;
	}

	*ram_size = fw_gen_params.instruction_offset;

	return 0;
}

/* Configure AWG, writing instructions
 *
 * @dvo: pointer to DVO structure
 * @awg_ram_code: pointer to AWG instructions table
 * @nb: nb of AWG instructions
 */
static void dvo_awg_configure(struct sti_dvo *dvo, u32 *awg_ram_code, int nb)
{
	int i;

	DRM_DEBUG_DRIVER("\n");

	for (i = 0; i < nb; i++)
		writel(awg_ram_code[i],
		       dvo->regs + DVO_DIGSYNC_INSTR_I + i * 4);
	for (i = nb; i < AWG_MAX_INST; i++)
		writel(0, dvo->regs + DVO_DIGSYNC_INSTR_I + i * 4);

	writel(DVO_AWG_CTRL_EN, dvo->regs + DVO_AWG_DIGSYNC_CTRL);
}

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(dvo->regs + reg))

static void dvo_dbg_awg_microcode(struct seq_file *s, void __iomem *reg)
{
	unsigned int i;

	seq_puts(s, "\n\n");
	seq_puts(s, "  DVO AWG microcode:");
	for (i = 0; i < AWG_MAX_INST; i++) {
		if (i % 8 == 0)
			seq_printf(s, "\n  %04X:", i);
		seq_printf(s, " %04X", readl(reg + i * 4));
	}
}

static int dvo_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_dvo *dvo = (struct sti_dvo *)node->info_ent->data;

	seq_printf(s, "DVO: (vaddr = 0x%p)", dvo->regs);
	DBGFS_DUMP(DVO_AWG_DIGSYNC_CTRL);
	DBGFS_DUMP(DVO_DOF_CFG);
	DBGFS_DUMP(DVO_LUT_PROG_LOW);
	DBGFS_DUMP(DVO_LUT_PROG_MID);
	DBGFS_DUMP(DVO_LUT_PROG_HIGH);
	dvo_dbg_awg_microcode(s, dvo->regs + DVO_DIGSYNC_INSTR_I);
	seq_putc(s, '\n');
	return 0;
}

static struct drm_info_list dvo_debugfs_files[] = {
	{ "dvo", dvo_dbg_show, 0, NULL },
};

static void dvo_debugfs_init(struct sti_dvo *dvo, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dvo_debugfs_files); i++)
		dvo_debugfs_files[i].data = dvo;

	drm_debugfs_create_files(dvo_debugfs_files,
				 ARRAY_SIZE(dvo_debugfs_files),
				 minor->debugfs_root, minor);
}

static void sti_dvo_disable(struct drm_bridge *bridge)
{
	struct sti_dvo *dvo = bridge->driver_private;

	if (!dvo->enabled)
		return;

	DRM_DEBUG_DRIVER("\n");

	if (dvo->config->awg_fwgen_fct)
		writel(0x00000000, dvo->regs + DVO_AWG_DIGSYNC_CTRL);

	writel(0x00000000, dvo->regs + DVO_DOF_CFG);

	drm_panel_disable(dvo->panel);

	/* Disable/unprepare dvo clock */
	clk_disable_unprepare(dvo->clk_pix);
	clk_disable_unprepare(dvo->clk);

	dvo->enabled = false;
}

static void sti_dvo_pre_enable(struct drm_bridge *bridge)
{
	struct sti_dvo *dvo = bridge->driver_private;
	struct dvo_config *config = dvo->config;
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	if (dvo->enabled)
		return;

	/* Make sure DVO is disabled */
	writel(0x00000000, dvo->regs + DVO_DOF_CFG);
	writel(0x00000000, dvo->regs + DVO_AWG_DIGSYNC_CTRL);

	if (config->awg_fwgen_fct) {
		u8 nb_instr;
		u32 awg_ram_code[AWG_MAX_INST];
		/* Configure AWG */
		if (!dvo_awg_generate_code(dvo, &nb_instr, awg_ram_code))
			dvo_awg_configure(dvo, awg_ram_code, nb_instr);
		else
			return;
	}

	/* Prepare/enable clocks */
	if (clk_prepare_enable(dvo->clk_pix))
		DRM_ERROR("Failed to prepare/enable dvo_pix clk\n");
	if (clk_prepare_enable(dvo->clk))
		DRM_ERROR("Failed to prepare/enable dvo clk\n");

	drm_panel_enable(dvo->panel);

	/* Set LUT */
	writel(config->lowbyte,  dvo->regs + DVO_LUT_PROG_LOW);
	writel(config->midbyte,  dvo->regs + DVO_LUT_PROG_MID);
	writel(config->highbyte, dvo->regs + DVO_LUT_PROG_HIGH);

	/* Digital output formatter config */
	val = (config->flags | DVO_DOF_EN);
	writel(val, dvo->regs + DVO_DOF_CFG);

	dvo->enabled = true;
}

static void sti_dvo_set_mode(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode)
{
	struct sti_dvo *dvo = bridge->driver_private;
	struct sti_mixer *mixer = to_sti_mixer(dvo->encoder->crtc);
	int rate = mode->clock * 1000;
	struct clk *clkp;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	drm_mode_copy(&dvo->mode, mode);

	/* According to the path used (main or aux), the dvo clocks should
	 * have a different parent clock. */
	if (mixer->id == STI_MIXER_MAIN)
		clkp = dvo->clk_main_parent;
	else
		clkp = dvo->clk_aux_parent;

	if (clkp) {
		clk_set_parent(dvo->clk_pix, clkp);
		clk_set_parent(dvo->clk, clkp);
	}

	/* DVO clocks = compositor clock */
	ret = clk_set_rate(dvo->clk_pix, rate);
	if (ret < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for dvo_pix clk\n", rate);
		return;
	}

	ret = clk_set_rate(dvo->clk, rate);
	if (ret < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for dvo clk\n", rate);
		return;
	}

	/* For now, we only support 24bit data enable (DE) synchro format */
	dvo->config = &rgb_24bit_de_cfg;
}

static void sti_dvo_bridge_nope(struct drm_bridge *bridge)
{
	/* do nothing */
}

static const struct drm_bridge_funcs sti_dvo_bridge_funcs = {
	.pre_enable = sti_dvo_pre_enable,
	.enable = sti_dvo_bridge_nope,
	.disable = sti_dvo_disable,
	.post_disable = sti_dvo_bridge_nope,
	.mode_set = sti_dvo_set_mode,
};

static int sti_dvo_connector_get_modes(struct drm_connector *connector)
{
	struct sti_dvo_connector *dvo_connector
		= to_sti_dvo_connector(connector);
	struct sti_dvo *dvo = dvo_connector->dvo;

	if (dvo->panel)
		return drm_panel_get_modes(dvo->panel, connector);

	return 0;
}

#define CLK_TOLERANCE_HZ 50

static enum drm_mode_status
sti_dvo_connector_mode_valid(struct drm_connector *connector,
			     const struct drm_display_mode *mode)
{
	int target = mode->clock * 1000;
	int target_min = target - CLK_TOLERANCE_HZ;
	int target_max = target + CLK_TOLERANCE_HZ;
	int result;
	struct sti_dvo_connector *dvo_connector
		= to_sti_dvo_connector(connector);
	struct sti_dvo *dvo = dvo_connector->dvo;

	result = clk_round_rate(dvo->clk_pix, target);

	DRM_DEBUG_DRIVER("target rate = %d => available rate = %d\n",
			 target, result);

	if ((result < target_min) || (result > target_max)) {
		DRM_DEBUG_DRIVER("dvo pixclk=%d not supported\n", target);
		return MODE_BAD;
	}

	return MODE_OK;
}

static const
struct drm_connector_helper_funcs sti_dvo_connector_helper_funcs = {
	.get_modes = sti_dvo_connector_get_modes,
	.mode_valid = sti_dvo_connector_mode_valid,
};

static enum drm_connector_status
sti_dvo_connector_detect(struct drm_connector *connector, bool force)
{
	struct sti_dvo_connector *dvo_connector
		= to_sti_dvo_connector(connector);
	struct sti_dvo *dvo = dvo_connector->dvo;

	DRM_DEBUG_DRIVER("\n");

	if (!dvo->panel) {
		dvo->panel = of_drm_find_panel(dvo->panel_node);
		if (IS_ERR(dvo->panel))
			dvo->panel = NULL;
	}

	if (dvo->panel)
		return connector_status_connected;

	return connector_status_disconnected;
}

static int sti_dvo_late_register(struct drm_connector *connector)
{
	struct sti_dvo_connector *dvo_connector
		= to_sti_dvo_connector(connector);
	struct sti_dvo *dvo = dvo_connector->dvo;

	dvo_debugfs_init(dvo, dvo->drm_dev->primary);

	return 0;
}

static const struct drm_connector_funcs sti_dvo_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = sti_dvo_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = sti_dvo_late_register,
};

static struct drm_encoder *sti_dvo_find_encoder(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_LVDS)
			return encoder;
	}

	return NULL;
}

static int sti_dvo_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_dvo *dvo = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct sti_dvo_connector *connector;
	struct drm_connector *drm_connector;
	int err;

	/* Set the drm device handle */
	dvo->drm_dev = drm_dev;

	encoder = sti_dvo_find_encoder(drm_dev);
	if (!encoder)
		return -ENOMEM;

	connector = devm_kzalloc(dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	connector->dvo = dvo;

	dvo->bridge.driver_private = dvo;
	dvo->bridge.of_node = dvo->dev.of_node;
	drm_bridge_add(&dvo->bridge);

	err = drm_bridge_attach(encoder, &dvo->bridge, NULL, 0);
	if (err)
		return err;

	connector->encoder = encoder;
	dvo->encoder = encoder;

	drm_connector = (struct drm_connector *)connector;

	drm_connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_init(drm_dev, drm_connector,
			   &sti_dvo_connector_funcs, DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(drm_connector,
				 &sti_dvo_connector_helper_funcs);

	err = drm_connector_attach_encoder(drm_connector, encoder);
	if (err) {
		DRM_ERROR("Failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	drm_bridge_remove(&dvo->bridge);
	return -EINVAL;
}

static void sti_dvo_unbind(struct device *dev,
			   struct device *master, void *data)
{
	struct sti_dvo *dvo = dev_get_drvdata(dev);

	drm_bridge_remove(&dvo->bridge);
}

static const struct component_ops sti_dvo_ops = {
	.bind = sti_dvo_bind,
	.unbind = sti_dvo_unbind,
};

static int sti_dvo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sti_dvo *dvo;
	struct device_node *np = dev->of_node;

	DRM_INFO("%s\n", __func__);

	dvo = devm_drm_bridge_alloc(dev, struct sti_dvo, bridge, &sti_dvo_bridge_funcs);
	if (IS_ERR(dvo)) {
		DRM_ERROR("Failed to allocate DVO\n");
		return PTR_ERR(dvo);
	}

	dvo->dev = pdev->dev;
	dvo->regs = devm_platform_ioremap_resource_byname(pdev, "dvo-reg");
	if (IS_ERR(dvo->regs))
		return PTR_ERR(dvo->regs);

	dvo->clk_pix = devm_clk_get(dev, "dvo_pix");
	if (IS_ERR(dvo->clk_pix)) {
		DRM_ERROR("Cannot get dvo_pix clock\n");
		return PTR_ERR(dvo->clk_pix);
	}

	dvo->clk = devm_clk_get(dev, "dvo");
	if (IS_ERR(dvo->clk)) {
		DRM_ERROR("Cannot get dvo clock\n");
		return PTR_ERR(dvo->clk);
	}

	dvo->clk_main_parent = devm_clk_get(dev, "main_parent");
	if (IS_ERR(dvo->clk_main_parent)) {
		DRM_DEBUG_DRIVER("Cannot get main_parent clock\n");
		dvo->clk_main_parent = NULL;
	}

	dvo->clk_aux_parent = devm_clk_get(dev, "aux_parent");
	if (IS_ERR(dvo->clk_aux_parent)) {
		DRM_DEBUG_DRIVER("Cannot get aux_parent clock\n");
		dvo->clk_aux_parent = NULL;
	}

	dvo->panel_node = of_parse_phandle(np, "sti,panel", 0);
	if (!dvo->panel_node)
		DRM_ERROR("No panel associated to the dvo output\n");
	of_node_put(dvo->panel_node);

	platform_set_drvdata(pdev, dvo);

	return component_add(&pdev->dev, &sti_dvo_ops);
}

static void sti_dvo_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sti_dvo_ops);
}

static const struct of_device_id dvo_of_match[] = {
	{ .compatible = "st,stih407-dvo", },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, dvo_of_match);

struct platform_driver sti_dvo_driver = {
	.driver = {
		.name = "sti-dvo",
		.of_match_table = dvo_of_match,
	},
	.probe = sti_dvo_probe,
	.remove = sti_dvo_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
