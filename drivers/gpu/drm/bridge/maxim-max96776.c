// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim max96776 GMSL2 Deserializer with eDP Output
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_panel.h>

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/max96776.h>

#define AUX_ADDR_7_0(x)				(((x) >> 0) & 0xff)
#define AUX_ADDR_15_8(x)			(((x) >> 8) & 0xff)
#define AUX_ADDR_19_16(x)			(((x) >> 16) & 0x0f)
#define DPCD_MAX_LANE_COUNT(x)			((x) & 0x1f)

enum link_lane_count {
	USE_ONE_LINK = 1,
	USE_TWO_LINK = 2,
	USE_FOUR_LINK = 4
};

enum link_rate {
	BW_1_62,
	BW_2_7,
	BW_5_4,
};

struct max96776_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_panel *panel;
	struct drm_display_mode mode;

	struct device *dev;
	struct max96776 *parent;
	struct regmap *regmap;
	struct drm_dp_aux aux;
	u8 link_rate;
	u8 lane_count;
	int max_link_rate;
	enum link_lane_count max_lane_count;
};

static const struct reg_sequence max96776_clk_ref[3][14] = {
	/* 1.62Gbps */
	{
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0xcc },
		{ 0xe7b5, 0x44 },
		{ 0xe7b6, 0x81 },
		{ 0xe7b7, 0x30 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x10 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	},

	/* 2.7Gbps */
	{
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	},

	/* 5.4Gbps */
	{
		{ 0xe7b2, 0x30 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x14 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x2e },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x01 },
		{ 0xe7be, 0x32 },
		{ 0xe7bf, 0x00 },
	},

};

#define to_max96776_bridge(x)	container_of(x, struct max96776_bridge, x)

static void
max96776_dp_aux_dpcd_addr_sel(struct max96776_bridge *des, unsigned int addr)
{
	u32 reg;

	reg = AUX_ADDR_7_0(addr);
	regmap_write(des->regmap, 0xe778, FIELD_PREP(USER_DATA1_B0, reg));
	reg = AUX_ADDR_15_8(addr);
	regmap_write(des->regmap, 0xe779, FIELD_PREP(USER_DATA1_B1, reg));

	/*
	 * Most significant four bits of DPCD register address when performing
	 * a twenty bit AUX read or write command.
	 */
	reg = AUX_ADDR_19_16(addr);
	regmap_write(des->regmap, 0xe77c, FIELD_PREP(USER_DATA3_B0, reg));
}

static ssize_t max96776_dp_aux_transfer(struct drm_dp_aux *aux,
					struct drm_dp_aux_msg *msg)
{
	struct max96776_bridge *des = to_max96776_bridge(aux);
	int num_transferred = 0;
	u8 *buffer = msg->buffer;
	u32 reg;
	int i;

	/*
	 * as Spec if Burst data transfer is supported,
	 * The burst data size must be limited to a maximum
	 * of 16 bytes.
	 */
	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	/*
	 * Write AUX channel
	 *
	 * this command writes a DPCD register on the eDP/DP sink device. The register
	 * address is specified by the user in address 0xe778 and 0xe779. The data (a byte)
	 * to be written is specified in 0xe77a. The AUX channel must be configured prior to
	 * using the command(this occurs at power-up). The example below writes DPCD sink
	 * register 0x0100 with data 0x0a, To issue command, write the following registers:
	 *
	 * 1. LSBs of write address: 0xe778 0x00
	 * 2. MSBs of write address: 0xe779 0x01
	 * 3. LSBs of data to write: 0xe77a 0x0a
	 * 4. command select: 0xe776 0x20
	 * 5. Execute command: 0xe777 0x80
	 */
	if (!(msg->request & DP_AUX_I2C_READ)) {
		for (i = 0; i < msg->size; i++) {
			max96776_dp_aux_dpcd_addr_sel(des, msg->address + i);
			reg = buffer[i];
			regmap_write(des->regmap, 0xe77a,
				     FIELD_PREP(USER_DATA2_B0, reg));
			regmap_update_bits(des->regmap, 0xe776, AUX_WRITE,
					   FIELD_PREP(AUX_WRITE, 1));
			regmap_update_bits(des->regmap, 0xe777, RUN_COMMAND,
					   FIELD_PREP(RUN_COMMAND, 1));
			mdelay(10);
			num_transferred++;
		}
	}

	/*
	 * Read AUX channel
	 *
	 * this command read DPCD register on the eDP/DP sink device. The register
	 * address is specified by the user in address 0xe778 and 0xe779. Once the
	 * command has executed, the return data (a byte) is stored in 0xe77a. The
	 * AUX channel must be configured prior to using the command(this occurs
	 * at power-up). The example, to read DPCD sink register 0x100 (main link
	 * bandwidth setting), write the following registers:
	 *
	 * 1. LSBs of write address: 0xe778 0x00
	 * 2. MSBs of write address: 0xe779 0x01
	 * 3. command select: 0xe776 0x10
	 * 4. Execute command: 0xe777 0x80
	 * 5. LSBs of return value read: 0xe77a
	 */
	if (msg->request & DP_AUX_I2C_READ) {
		for (i = 0; i < msg->size; i++) {
			max96776_dp_aux_dpcd_addr_sel(des, msg->address + i);
			regmap_update_bits(des->regmap, 0xe776, AUX_READ,
					   FIELD_PREP(AUX_READ, 1));
			regmap_update_bits(des->regmap, 0xe777, RUN_COMMAND,
					   FIELD_PREP(RUN_COMMAND, 1));
			mdelay(10);
			regmap_read(des->regmap, 0xe77a, &reg);
			buffer[i] = (u8)reg;

			num_transferred++;
		}
	}

	msg->reply = DP_AUX_I2C_REPLY_ACK;
	return (num_transferred == msg->size) ? num_transferred : -EBUSY;
}

static int max96776_bridge_get_modes(struct drm_bridge *bridge,
				      struct drm_connector *connector)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);

	if (des->next_bridge)
		return drm_bridge_get_modes(des->next_bridge, connector);

	if (des->panel)
		return drm_panel_get_modes(des->panel, connector);

	return drm_add_modes_noedid(connector, 1920, 1080);
}

static void max96776_edp_timing_config(struct max96776_bridge *des)
{
	struct drm_display_mode *mode = &des->mode;
	u32 hfp, hsa, hbp, hact;
	u32 vact, vsa, vfp, vbp;
	u64 hwords, mvid, link_rate;
	bool hsync_pol, vsync_pol;

	vact = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hact = mode->hdisplay;
	hsa = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;

	regmap_write(des->regmap, 0xe794, FIELD_PREP(HRES_B0, hact));
	regmap_write(des->regmap, 0xe795, FIELD_PREP(HRES_B1, hact >> 8));
	regmap_write(des->regmap, 0xe796, FIELD_PREP(HFP_B0, hfp));
	regmap_write(des->regmap, 0xe797, FIELD_PREP(HFP_B1, hfp >> 8));
	regmap_write(des->regmap, 0xe798, FIELD_PREP(HSW_B0, hsa));
	regmap_write(des->regmap, 0xe799, FIELD_PREP(HSW_B1, hsa >> 8));
	regmap_write(des->regmap, 0xe79a, FIELD_PREP(HBP_B0, hbp));
	regmap_write(des->regmap, 0xe79b, FIELD_PREP(HBP_B1, hbp >> 8));
	regmap_write(des->regmap, 0xe79c, FIELD_PREP(VRES_B0, vact));
	regmap_write(des->regmap, 0xe79d, FIELD_PREP(VRES_B1, vact >> 8));
	regmap_write(des->regmap, 0xe79e, FIELD_PREP(VFP_B0, vfp));
	regmap_write(des->regmap, 0xe79f, FIELD_PREP(VFP_B1, vfp >> 8));
	regmap_write(des->regmap, 0xe7a0, FIELD_PREP(VSW_B0, vsa));
	regmap_write(des->regmap, 0xe7a1, FIELD_PREP(VSW_B1, vsa >> 8));
	regmap_write(des->regmap, 0xe7a2, FIELD_PREP(VBP_B0, vbp));
	regmap_write(des->regmap, 0xe7a3, FIELD_PREP(VBP_B1, vbp >> 8));

	hsync_pol = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);
	vsync_pol = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	regmap_update_bits(des->regmap, 0xe7ac, HSYNC_POL | VSYNC_POL,
			   FIELD_PREP(HSYNC_POL, hsync_pol) |
			   FIELD_PREP(VSYNC_POL, vsync_pol));

	/* NVID should always be set to 0x8000 */
	regmap_write(des->regmap, 0xe7a8, FIELD_PREP(NVID_B0, 0));
	regmap_write(des->regmap, 0xe7a9, FIELD_PREP(NVID_B1, 0x80));

	/* HWORDS = ((HRES x bits/pixel)/16) - LANE_COUNT */
	hwords = DIV_ROUND_CLOSEST_ULL(hact * 24, 16) - des->lane_count;
	regmap_write(des->regmap, 0xe7a4, FIELD_PREP(HWORDS_B0, hwords));
	regmap_write(des->regmap, 0xe7a5, FIELD_PREP(HWORDS_B1, hwords >> 8));

	/* MVID = (PCLK x NVID) x 10 / Link Rate */
	link_rate = drm_dp_bw_code_to_link_rate(des->link_rate);
	mvid = DIV_ROUND_CLOSEST_ULL((u64)mode->clock * 32768, link_rate);
	regmap_write(des->regmap, 0xe7a6, FIELD_PREP(HWORDS_B0, mvid));
	regmap_write(des->regmap, 0xe7a7, FIELD_PREP(HWORDS_B1, mvid >> 8));

	regmap_write(des->regmap, 0xe7aa, FIELD_PREP(TUC_VALUE_B0, 0x40));
	regmap_write(des->regmap, 0xe7ab, FIELD_PREP(TUC_VALUE_B1, 0));
}

static void max96776_get_edp_sink_max_lane_count(struct max96776_bridge *des)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum number of Main Link lanes
	 * 0x01 = 1 lane, 0x02 = 2 lanes, 0x04 = 4 lanes
	 */
	drm_dp_dpcd_readb(&des->aux, DP_MAX_LANE_COUNT, &data);
	des->lane_count = DPCD_MAX_LANE_COUNT(data);
}

static void max96776_get_edp_sink_max_bw(struct max96776_bridge *des)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 * For DP rev.1.2, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps, 0x14 = 5.4Gbps
	 * For DP rev.1.4, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps, 0x14 = 5.4Gbps 0x1e = 8.1 Gbps
	 */
	drm_dp_dpcd_readb(&des->aux, DP_MAX_LINK_RATE, &data);
	des->link_rate = data;
}

static void max96776_edp_link_config(struct max96776_bridge *des)
{

	max96776_get_edp_sink_max_bw(des);
	max96776_get_edp_sink_max_lane_count(des);

	if ((des->link_rate != DP_LINK_BW_1_62) &&
	    (des->link_rate != DP_LINK_BW_2_7) &&
	    (des->link_rate != DP_LINK_BW_5_4) &&
	    (des->link_rate != DP_LINK_BW_8_1)) {
		dev_err(des->dev, "Rx Max Link Rate is abnormal :%x !\n",
			des->link_rate);
		des->link_rate = DP_LINK_BW_1_62;
	}

	if (des->lane_count == 0) {
		dev_err(des->dev, "Rx Max Lane count is abnormal :%x !\n",
			des->lane_count);
		des->lane_count = (u8)USE_ONE_LINK;
	}

	/* Setup TX lane count & rate */
	if (des->lane_count > (u8)des->max_lane_count)
		des->lane_count = (u8)des->max_lane_count;
	if (des->link_rate > des->max_link_rate)
		des->link_rate = des->max_link_rate;

	regmap_write(des->regmap, 0xe790, FIELD_PREP(LINK_RATE, des->link_rate));
	regmap_write(des->regmap, 0xe792, FIELD_PREP(LANE_COUNT, des->lane_count));
	dev_info(des->dev, "final bandwidth: 0x%02x, lane count: 0x%02x\n",
		 des->link_rate, des->lane_count);
}

static void max96776_edp_pll_config(struct max96776_bridge *des)
{
	/* provides control for eDP PLL */
	switch (des->link_rate) {
	case DP_LINK_BW_5_4:
		regmap_multi_reg_write(des->regmap, max96776_clk_ref[BW_5_4],
				       ARRAY_SIZE(max96776_clk_ref[BW_5_4]));
		break;
	case DP_LINK_BW_2_7:
		regmap_multi_reg_write(des->regmap, max96776_clk_ref[BW_2_7],
				       ARRAY_SIZE(max96776_clk_ref[BW_2_7]));
		break;
	case DP_LINK_BW_1_62:
	default:
		regmap_multi_reg_write(des->regmap, max96776_clk_ref[BW_1_62],
				       ARRAY_SIZE(max96776_clk_ref[BW_1_62]));
		break;
	}
}

static void max96776_edp_full_training(struct max96776_bridge *des)
{
	u8 status[2];
	u32 sts;
	int ret;

	regmap_update_bits(des->regmap, 0xe776, RUN_LINK_TRAINING,
			   FIELD_PREP(RUN_LINK_TRAINING, 0x1));
	regmap_update_bits(des->regmap, 0xe777, RUN_COMMAND,
			   FIELD_PREP(RUN_COMMAND, 0x1));
	ret = regmap_read_poll_timeout(des->regmap, 0x07f0, sts,
				       FIELD_PREP(TRAINING_SUCCESSFUL, sts),
				       MSEC_PER_SEC, 200 * MSEC_PER_SEC);
	if (ret < 0)
		dev_err(des->dev, "Link Training not successful\n");

	drm_dp_dpcd_read(&des->aux, DP_LANE0_1_STATUS, status, 2);
	dev_info(des->dev, "SINK LANE0_1_STATUS:0x%02x LANE2_3_STATUS:0x%02x\n",
		 status[0], status[1]);
}

static void
max96776_bridge_atomic_pre_enable(struct drm_bridge *bridge,
				   struct drm_bridge_state *old_bridge_state)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);
	u8 dpcd;

	/* disable HDCP 2.2 on eDP Deserializer */
	regmap_update_bits(des->regmap, 0x1700, CMD_RESET,
			   FIELD_PREP(CMD_RESET, 0x01));

	/*
	 * This bit must be set to allow waiting for the
	 * CMU to lock. It also should be set when using
	 * SSC. Otherwise, a fixed wait time of 20μS is
	 * used.
	 */
	regmap_update_bits(des->regmap, 0xe7b0, SS_ENABLE,
			   FIELD_PREP(SS_ENABLE, 0x01));

	/*
	 * Determines whether spread spectrum clocking (SSC)
	 * is used with the DP sink device.
	 */
	drm_dp_dpcd_readb(&des->aux, DP_MAX_DOWNSPREAD, &dpcd);
	if (!!(dpcd & DP_MAX_DOWNSPREAD_0_5))
		regmap_update_bits(des->regmap, 0xe7b1, SSC_ENABLE,
				   FIELD_PREP(SSC_ENABLE, 0x01));

	max96776_edp_link_config(des);
	max96776_edp_pll_config(des);
	max96776_edp_timing_config(des);

	if (des->panel)
		drm_panel_prepare(des->panel);
}

static void
max96776_bridge_atomic_enable(struct drm_bridge *bridge,
			       struct drm_bridge_state *old_bridge_state)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);

	max96776_edp_full_training(des);

	if (des->panel)
		drm_panel_enable(des->panel);
}

static void
max96776_bridge_atomic_disable(struct drm_bridge *bridge,
				struct drm_bridge_state *old_bridge_state)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);

	if (des->panel)
		drm_panel_disable(des->panel);
}

static void
max96776_bridge_atomic_post_disable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);

	if (des->panel)
		drm_panel_unprepare(des->panel);
}


static int max96776_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &des->panel,
					  &des->next_bridge);
	if (ret < 0 && ret != -ENODEV)
		return ret;

	if (des->next_bridge)
		return drm_bridge_attach(bridge->encoder, des->next_bridge,
					 bridge, 0);

	return 0;
}

static void max96776_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adj_mode)
{
	struct max96776_bridge *des  = to_max96776_bridge(bridge);

	drm_mode_copy(&des->mode, adj_mode);
}

static enum drm_connector_status
max96776_bridge_detect(struct drm_bridge *bridge)
{
	struct max96776_bridge *des = to_max96776_bridge(bridge);
	u32 hpd;

	if (regmap_read(des->regmap, 0x6230, &hpd))
		return connector_status_disconnected;

	if (!FIELD_PREP(HPD_PRESENT, hpd))
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_bridge_funcs max96776_bridge_funcs = {
	.attach = max96776_bridge_attach,
	.detect = max96776_bridge_detect,
	.get_modes = max96776_bridge_get_modes,
	.atomic_pre_enable = max96776_bridge_atomic_pre_enable,
	.atomic_post_disable = max96776_bridge_atomic_post_disable,
	.atomic_enable = max96776_bridge_atomic_enable,
	.atomic_disable = max96776_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.mode_set = max96776_bridge_mode_set,
};

static int max96776_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96776_bridge *des;
	int ret;

	des = devm_kzalloc(dev, sizeof(*des), GFP_KERNEL);
	if (!des)
		return -ENOMEM;

	des->dev = dev;
	des->parent = dev_get_drvdata(dev->parent);
	platform_set_drvdata(pdev, des);

	des->regmap = dev_get_regmap(dev->parent, NULL);
	if (!des->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	des->max_link_rate = DP_LINK_BW_5_4;
	des->max_lane_count = USE_FOUR_LINK;

	des->aux.name = "DP-AUX";
	des->aux.transfer = max96776_dp_aux_transfer;
	des->aux.dev = des->dev;

	ret = drm_dp_aux_register(&des->aux);
	if (ret) {
		dev_err(dev, "failed to register dp aux\n");
		return ret;
	}

	des->bridge.funcs = &max96776_bridge_funcs;
	des->bridge.of_node = dev->of_node;
	des->bridge.ops = DRM_BRIDGE_OP_MODES | DRM_BRIDGE_OP_DETECT;
	des->bridge.type = DRM_MODE_CONNECTOR_eDP;

	drm_bridge_add(&des->bridge);

	return 0;
}

static int max96776_bridge_remove(struct platform_device *pdev)
{
	struct max96776_bridge *des = platform_get_drvdata(pdev);

	drm_bridge_remove(&des->bridge);

	return 0;
}

static const struct of_device_id max96776_bridge_of_match[] = {
	{ .compatible = "maxim,max96776-bridge" },
	{}
};
MODULE_DEVICE_TABLE(of, max96776_bridge_of_match);

static struct platform_driver max96776_bridge_driver = {
	.driver = {
		.name = "max96776-bridge",
		.of_match_table = max96776_bridge_of_match,
	},
	.probe = max96776_bridge_probe,
	.remove = max96776_bridge_remove,
};

module_platform_driver(max96776_bridge_driver);

MODULE_AUTHOR("Guochun Huang <hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("Maxim max96776 GMSL2 Deserializer with eDP Output");
MODULE_LICENSE("GPL");
