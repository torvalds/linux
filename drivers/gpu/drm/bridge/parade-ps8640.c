// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#define PAGE2_GPIO_H		0xa7
#define PS_GPIO9		BIT(1)
#define PAGE2_I2C_BYPASS	0xea
#define I2C_BYPASS_EN		0xd0
#define PAGE2_MCS_EN		0xf3
#define MCS_EN			BIT(0)
#define PAGE3_SET_ADD		0xfe
#define VDO_CTL_ADD		0x13
#define VDO_DIS			0x18
#define VDO_EN			0x1c
#define DP_NUM_LANES		4

/*
 * PS8640 uses multiple addresses:
 * page[0]: for DP control
 * page[1]: for VIDEO Bridge
 * page[2]: for control top
 * page[3]: for DSI Link Control1
 * page[4]: for MIPI Phy
 * page[5]: for VPLL
 * page[6]: for DSI Link Control2
 * page[7]: for SPI ROM mapping
 */
enum page_addr_offset {
	PAGE0_DP_CNTL = 0,
	PAGE1_VDO_BDG,
	PAGE2_TOP_CNTL,
	PAGE3_DSI_CNTL1,
	PAGE4_MIPI_PHY,
	PAGE5_VPLL,
	PAGE6_DSI_CNTL2,
	PAGE7_SPI_CNTL,
	MAX_DEVS
};

enum ps8640_vdo_control {
	DISABLE = VDO_DIS,
	ENABLE = VDO_EN,
};

struct ps8640 {
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct mipi_dsi_device *dsi;
	struct i2c_client *page[MAX_DEVS];
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_powerdown;
	bool powered;
};

static inline struct ps8640 *bridge_to_ps8640(struct drm_bridge *e)
{
	return container_of(e, struct ps8640, bridge);
}

static int ps8640_bridge_vdo_control(struct ps8640 *ps_bridge,
				     const enum ps8640_vdo_control ctrl)
{
	struct i2c_client *client = ps_bridge->page[PAGE3_DSI_CNTL1];
	u8 vdo_ctrl_buf[] = { VDO_CTL_ADD, ctrl };
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, PAGE3_SET_ADD,
					     sizeof(vdo_ctrl_buf),
					     vdo_ctrl_buf);
	if (ret < 0) {
		DRM_ERROR("failed to %sable VDO: %d\n",
			  ctrl == ENABLE ? "en" : "dis", ret);
		return ret;
	}

	return 0;
}

static void ps8640_bridge_poweron(struct ps8640 *ps_bridge)
{
	struct i2c_client *client = ps_bridge->page[PAGE2_TOP_CNTL];
	unsigned long timeout;
	int ret, status;

	if (ps_bridge->powered)
		return;

	ret = regulator_bulk_enable(ARRAY_SIZE(ps_bridge->supplies),
				    ps_bridge->supplies);
	if (ret < 0) {
		DRM_ERROR("cannot enable regulators %d\n", ret);
		return;
	}

	gpiod_set_value(ps_bridge->gpio_powerdown, 0);
	gpiod_set_value(ps_bridge->gpio_reset, 1);
	usleep_range(2000, 2500);
	gpiod_set_value(ps_bridge->gpio_reset, 0);

	/*
	 * Wait for the ps8640 embedded MCU to be ready
	 * First wait 200ms and then check the MCU ready flag every 20ms
	 */
	msleep(200);

	timeout = jiffies + msecs_to_jiffies(200) + 1;

	while (time_is_after_jiffies(timeout)) {
		status = i2c_smbus_read_byte_data(client, PAGE2_GPIO_H);
		if (status < 0) {
			DRM_ERROR("failed read PAGE2_GPIO_H: %d\n", status);
			goto err_regulators_disable;
		}
		if ((status & PS_GPIO9) == PS_GPIO9)
			break;

		msleep(20);
	}

	msleep(50);

	/*
	 * The Manufacturer Command Set (MCS) is a device dependent interface
	 * intended for factory programming of the display module default
	 * parameters. Once the display module is configured, the MCS shall be
	 * disabled by the manufacturer. Once disabled, all MCS commands are
	 * ignored by the display interface.
	 */
	status = i2c_smbus_read_byte_data(client, PAGE2_MCS_EN);
	if (status < 0) {
		DRM_ERROR("failed read PAGE2_MCS_EN: %d\n", status);
		goto err_regulators_disable;
	}

	ret = i2c_smbus_write_byte_data(client, PAGE2_MCS_EN,
					status & ~MCS_EN);
	if (ret < 0) {
		DRM_ERROR("failed write PAGE2_MCS_EN: %d\n", ret);
		goto err_regulators_disable;
	}

	/* Switch access edp panel's edid through i2c */
	ret = i2c_smbus_write_byte_data(client, PAGE2_I2C_BYPASS,
					I2C_BYPASS_EN);
	if (ret < 0) {
		DRM_ERROR("failed write PAGE2_I2C_BYPASS: %d\n", ret);
		goto err_regulators_disable;
	}

	ps_bridge->powered = true;

	return;

err_regulators_disable:
	regulator_bulk_disable(ARRAY_SIZE(ps_bridge->supplies),
			       ps_bridge->supplies);
}

static void ps8640_bridge_poweroff(struct ps8640 *ps_bridge)
{
	int ret;

	if (!ps_bridge->powered)
		return;

	gpiod_set_value(ps_bridge->gpio_reset, 1);
	gpiod_set_value(ps_bridge->gpio_powerdown, 1);
	ret = regulator_bulk_disable(ARRAY_SIZE(ps_bridge->supplies),
				     ps_bridge->supplies);
	if (ret < 0)
		DRM_ERROR("cannot disable regulators %d\n", ret);

	ps_bridge->powered = false;
}

static void ps8640_pre_enable(struct drm_bridge *bridge)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);
	int ret;

	ps8640_bridge_poweron(ps_bridge);

	ret = ps8640_bridge_vdo_control(ps_bridge, ENABLE);
	if (ret < 0)
		ps8640_bridge_poweroff(ps_bridge);
}

static void ps8640_post_disable(struct drm_bridge *bridge)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);

	ps8640_bridge_vdo_control(ps_bridge, DISABLE);
	ps8640_bridge_poweroff(ps_bridge);
}

static int ps8640_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);
	struct device *dev = &ps_bridge->page[0]->dev;
	struct device_node *in_ep, *dsi_node;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;
	const struct mipi_dsi_device_info info = { .type = "ps8640",
						   .channel = 0,
						   .node = NULL,
						 };

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	/* port@0 is ps8640 dsi input port */
	in_ep = of_graph_get_endpoint_by_regs(dev->of_node, 0, -1);
	if (!in_ep)
		return -ENODEV;

	dsi_node = of_graph_get_remote_port_parent(in_ep);
	of_node_put(in_ep);
	if (!dsi_node)
		return -ENODEV;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	of_node_put(dsi_node);
	if (!host)
		return -ENODEV;

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		return ret;
	}

	ps_bridge->dsi = dsi;

	dsi->host = host;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = DP_NUM_LANES;
	ret = mipi_dsi_attach(dsi);
	if (ret)
		goto err_dsi_attach;

	/* Attach the panel-bridge to the dsi bridge */
	return drm_bridge_attach(bridge->encoder, ps_bridge->panel_bridge,
				 &ps_bridge->bridge, flags);

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
	return ret;
}

static struct edid *ps8640_bridge_get_edid(struct drm_bridge *bridge,
					   struct drm_connector *connector)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);
	bool poweroff = !ps_bridge->powered;
	struct edid *edid;

	/*
	 * When we end calling get_edid() triggered by an ioctl, i.e
	 *
	 *   drm_mode_getconnector (ioctl)
	 *     -> drm_helper_probe_single_connector_modes
	 *        -> drm_bridge_connector_get_modes
	 *           -> ps8640_bridge_get_edid
	 *
	 * We need to make sure that what we need is enabled before reading
	 * EDID, for this chip, we need to do a full poweron, otherwise it will
	 * fail.
	 */
	drm_bridge_chain_pre_enable(bridge);

	edid = drm_get_edid(connector,
			    ps_bridge->page[PAGE0_DP_CNTL]->adapter);

	/*
	 * If we call the get_edid() function without having enabled the chip
	 * before, return the chip to its original power state.
	 */
	if (poweroff)
		drm_bridge_chain_post_disable(bridge);

	return edid;
}

static const struct drm_bridge_funcs ps8640_bridge_funcs = {
	.attach = ps8640_bridge_attach,
	.get_edid = ps8640_bridge_get_edid,
	.post_disable = ps8640_post_disable,
	.pre_enable = ps8640_pre_enable,
};

static int ps8640_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct ps8640 *ps_bridge;
	struct drm_panel *panel;
	int ret;
	u32 i;

	ps_bridge = devm_kzalloc(dev, sizeof(*ps_bridge), GFP_KERNEL);
	if (!ps_bridge)
		return -ENOMEM;

	/* port@1 is ps8640 output port */
	ret = drm_of_find_panel_or_bridge(np, 1, 0, &panel, NULL);
	if (ret < 0)
		return ret;
	if (!panel)
		return -ENODEV;

	ps_bridge->panel_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(ps_bridge->panel_bridge))
		return PTR_ERR(ps_bridge->panel_bridge);

	ps_bridge->supplies[0].supply = "vdd33";
	ps_bridge->supplies[1].supply = "vdd12";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ps_bridge->supplies),
				      ps_bridge->supplies);
	if (ret)
		return ret;

	ps_bridge->gpio_powerdown = devm_gpiod_get(&client->dev, "powerdown",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(ps_bridge->gpio_powerdown))
		return PTR_ERR(ps_bridge->gpio_powerdown);

	/*
	 * Assert the reset to avoid the bridge being initialized prematurely
	 */
	ps_bridge->gpio_reset = devm_gpiod_get(&client->dev, "reset",
					       GPIOD_OUT_HIGH);
	if (IS_ERR(ps_bridge->gpio_reset))
		return PTR_ERR(ps_bridge->gpio_reset);

	ps_bridge->bridge.funcs = &ps8640_bridge_funcs;
	ps_bridge->bridge.of_node = dev->of_node;
	ps_bridge->bridge.ops = DRM_BRIDGE_OP_EDID;
	ps_bridge->bridge.type = DRM_MODE_CONNECTOR_eDP;

	ps_bridge->page[PAGE0_DP_CNTL] = client;

	for (i = 1; i < ARRAY_SIZE(ps_bridge->page); i++) {
		ps_bridge->page[i] = devm_i2c_new_dummy_device(&client->dev,
							     client->adapter,
							     client->addr + i);
		if (IS_ERR(ps_bridge->page[i])) {
			dev_err(dev, "failed i2c dummy device, address %02x\n",
				client->addr + i);
			return PTR_ERR(ps_bridge->page[i]);
		}
	}

	i2c_set_clientdata(client, ps_bridge);

	drm_bridge_add(&ps_bridge->bridge);

	return 0;
}

static int ps8640_remove(struct i2c_client *client)
{
	struct ps8640 *ps_bridge = i2c_get_clientdata(client);

	drm_bridge_remove(&ps_bridge->bridge);

	return 0;
}

static const struct of_device_id ps8640_match[] = {
	{ .compatible = "parade,ps8640" },
	{ }
};
MODULE_DEVICE_TABLE(of, ps8640_match);

static struct i2c_driver ps8640_driver = {
	.probe_new = ps8640_probe,
	.remove = ps8640_remove,
	.driver = {
		.name = "ps8640",
		.of_match_table = ps8640_match,
	},
};
module_i2c_driver(ps8640_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_AUTHOR("CK Hu <ck.hu@mediatek.com>");
MODULE_AUTHOR("Enric Balletbo i Serra <enric.balletbo@collabora.com>");
MODULE_DESCRIPTION("PARADE ps8640 DSI-eDP converter driver");
MODULE_LICENSE("GPL v2");
