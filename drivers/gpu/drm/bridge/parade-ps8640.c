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
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_bridge.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#define PAGE0_AUXCH_CFG3	0x76
#define  AUXCH_CFG3_RESET	0xff
#define PAGE0_SWAUX_ADDR_7_0	0x7d
#define PAGE0_SWAUX_ADDR_15_8	0x7e
#define PAGE0_SWAUX_ADDR_23_16	0x7f
#define  SWAUX_ADDR_MASK	GENMASK(19, 0)
#define PAGE0_SWAUX_LENGTH	0x80
#define  SWAUX_LENGTH_MASK	GENMASK(3, 0)
#define  SWAUX_NO_PAYLOAD	BIT(7)
#define PAGE0_SWAUX_WDATA	0x81
#define PAGE0_SWAUX_RDATA	0x82
#define PAGE0_SWAUX_CTRL	0x83
#define  SWAUX_SEND		BIT(0)
#define PAGE0_SWAUX_STATUS	0x84
#define  SWAUX_M_MASK		GENMASK(4, 0)
#define  SWAUX_STATUS_MASK	GENMASK(7, 5)
#define  SWAUX_STATUS_NACK	(0x1 << 5)
#define  SWAUX_STATUS_DEFER	(0x2 << 5)
#define  SWAUX_STATUS_ACKM	(0x3 << 5)
#define  SWAUX_STATUS_INVALID	(0x4 << 5)
#define  SWAUX_STATUS_I2C_NACK	(0x5 << 5)
#define  SWAUX_STATUS_I2C_DEFER	(0x6 << 5)
#define  SWAUX_STATUS_TIMEOUT	(0x7 << 5)

#define PAGE2_GPIO_H		0xa7
#define  PS_GPIO9		BIT(1)
#define PAGE2_I2C_BYPASS	0xea
#define  I2C_BYPASS_EN		0xd0
#define PAGE2_MCS_EN		0xf3
#define  MCS_EN			BIT(0)

#define PAGE3_SET_ADD		0xfe
#define  VDO_CTL_ADD		0x13
#define  VDO_DIS		0x18
#define  VDO_EN			0x1c

#define NUM_MIPI_LANES		4

#define COMMON_PS8640_REGMAP_CONFIG \
	.reg_bits = 8, \
	.val_bits = 8, \
	.cache_type = REGCACHE_NONE

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
	struct drm_dp_aux aux;
	struct mipi_dsi_device *dsi;
	struct i2c_client *page[MAX_DEVS];
	struct regmap	*regmap[MAX_DEVS];
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_powerdown;
	bool powered;
};

static const struct regmap_config ps8640_regmap_config[] = {
	[PAGE0_DP_CNTL] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xbf,
	},
	[PAGE1_VDO_BDG] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
	[PAGE2_TOP_CNTL] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
	[PAGE3_DSI_CNTL1] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
	[PAGE4_MIPI_PHY] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
	[PAGE5_VPLL] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0x7f,
	},
	[PAGE6_DSI_CNTL2] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
	[PAGE7_SPI_CNTL] = {
		COMMON_PS8640_REGMAP_CONFIG,
		.max_register = 0xff,
	},
};

static inline struct ps8640 *bridge_to_ps8640(struct drm_bridge *e)
{
	return container_of(e, struct ps8640, bridge);
}

static inline struct ps8640 *aux_to_ps8640(struct drm_dp_aux *aux)
{
	return container_of(aux, struct ps8640, aux);
}

static ssize_t ps8640_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct ps8640 *ps_bridge = aux_to_ps8640(aux);
	struct regmap *map = ps_bridge->regmap[PAGE0_DP_CNTL];
	struct device *dev = &ps_bridge->page[PAGE0_DP_CNTL]->dev;
	unsigned int len = msg->size;
	unsigned int data;
	unsigned int base;
	int ret;
	u8 request = msg->request &
		     ~(DP_AUX_I2C_MOT | DP_AUX_I2C_WRITE_STATUS_UPDATE);
	u8 *buf = msg->buffer;
	u8 addr_len[PAGE0_SWAUX_LENGTH + 1 - PAGE0_SWAUX_ADDR_7_0];
	u8 i;
	bool is_native_aux = false;

	if (len > DP_AUX_MAX_PAYLOAD_BYTES)
		return -EINVAL;

	if (msg->address & ~SWAUX_ADDR_MASK)
		return -EINVAL;

	switch (request) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_NATIVE_READ:
		is_native_aux = true;
		fallthrough;
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_READ:
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_write(map, PAGE0_AUXCH_CFG3, AUXCH_CFG3_RESET);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to write PAGE0_AUXCH_CFG3: %d\n",
			      ret);
		return ret;
	}

	/* Assume it's good */
	msg->reply = 0;

	base = PAGE0_SWAUX_ADDR_7_0;
	addr_len[PAGE0_SWAUX_ADDR_7_0 - base] = msg->address;
	addr_len[PAGE0_SWAUX_ADDR_15_8 - base] = msg->address >> 8;
	addr_len[PAGE0_SWAUX_ADDR_23_16 - base] = (msg->address >> 16) |
						  (msg->request << 4);
	addr_len[PAGE0_SWAUX_LENGTH - base] = (len == 0) ? SWAUX_NO_PAYLOAD :
					      ((len - 1) & SWAUX_LENGTH_MASK);

	regmap_bulk_write(map, PAGE0_SWAUX_ADDR_7_0, addr_len,
			  ARRAY_SIZE(addr_len));

	if (len && (request == DP_AUX_NATIVE_WRITE ||
		    request == DP_AUX_I2C_WRITE)) {
		/* Write to the internal FIFO buffer */
		for (i = 0; i < len; i++) {
			ret = regmap_write(map, PAGE0_SWAUX_WDATA, buf[i]);
			if (ret) {
				DRM_DEV_ERROR(dev,
					      "failed to write WDATA: %d\n",
					      ret);
				return ret;
			}
		}
	}

	regmap_write(map, PAGE0_SWAUX_CTRL, SWAUX_SEND);

	/* Zero delay loop because i2c transactions are slow already */
	regmap_read_poll_timeout(map, PAGE0_SWAUX_CTRL, data,
				 !(data & SWAUX_SEND), 0, 50 * 1000);

	regmap_read(map, PAGE0_SWAUX_STATUS, &data);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to read PAGE0_SWAUX_STATUS: %d\n",
			      ret);
		return ret;
	}

	switch (data & SWAUX_STATUS_MASK) {
	/* Ignore the DEFER cases as they are already handled in hardware */
	case SWAUX_STATUS_NACK:
	case SWAUX_STATUS_I2C_NACK:
		/*
		 * The programming guide is not clear about whether a I2C NACK
		 * would trigger SWAUX_STATUS_NACK or SWAUX_STATUS_I2C_NACK. So
		 * we handle both cases together.
		 */
		if (is_native_aux)
			msg->reply |= DP_AUX_NATIVE_REPLY_NACK;
		else
			msg->reply |= DP_AUX_I2C_REPLY_NACK;

		fallthrough;
	case SWAUX_STATUS_ACKM:
		len = data & SWAUX_M_MASK;
		break;
	case SWAUX_STATUS_INVALID:
		return -EOPNOTSUPP;
	case SWAUX_STATUS_TIMEOUT:
		return -ETIMEDOUT;
	}

	if (len && (request == DP_AUX_NATIVE_READ ||
		    request == DP_AUX_I2C_READ)) {
		/* Read from the internal FIFO buffer */
		for (i = 0; i < len; i++) {
			ret = regmap_read(map, PAGE0_SWAUX_RDATA, &data);
			if (ret) {
				DRM_DEV_ERROR(dev,
					      "failed to read RDATA: %d\n",
					      ret);
				return ret;
			}

			buf[i] = data;
		}
	}

	return len;
}

static int ps8640_bridge_vdo_control(struct ps8640 *ps_bridge,
				     const enum ps8640_vdo_control ctrl)
{
	struct regmap *map = ps_bridge->regmap[PAGE3_DSI_CNTL1];
	u8 vdo_ctrl_buf[] = { VDO_CTL_ADD, ctrl };
	int ret;

	ret = regmap_bulk_write(map, PAGE3_SET_ADD,
				vdo_ctrl_buf, sizeof(vdo_ctrl_buf));

	if (ret < 0) {
		DRM_ERROR("failed to %sable VDO: %d\n",
			  ctrl == ENABLE ? "en" : "dis", ret);
		return ret;
	}

	return 0;
}

static void ps8640_bridge_poweron(struct ps8640 *ps_bridge)
{
	struct regmap *map = ps_bridge->regmap[PAGE2_TOP_CNTL];
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

	ret = regmap_read_poll_timeout(map, PAGE2_GPIO_H, status,
				       status & PS_GPIO9, 20 * 1000, 200 * 1000);

	if (ret < 0) {
		DRM_ERROR("failed read PAGE2_GPIO_H: %d\n", ret);
		goto err_regulators_disable;
	}

	msleep(50);

	/*
	 * The Manufacturer Command Set (MCS) is a device dependent interface
	 * intended for factory programming of the display module default
	 * parameters. Once the display module is configured, the MCS shall be
	 * disabled by the manufacturer. Once disabled, all MCS commands are
	 * ignored by the display interface.
	 */

	ret = regmap_update_bits(map, PAGE2_MCS_EN, MCS_EN, 0);
	if (ret < 0) {
		DRM_ERROR("failed write PAGE2_MCS_EN: %d\n", ret);
		goto err_regulators_disable;
	}

	/* Switch access edp panel's edid through i2c */
	ret = regmap_write(map, PAGE2_I2C_BYPASS, I2C_BYPASS_EN);
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
	dsi->lanes = NUM_MIPI_LANES;
	ret = mipi_dsi_attach(dsi);
	if (ret) {
		dev_err(dev, "failed to attach dsi device: %d\n", ret);
		goto err_dsi_attach;
	}

	ret = drm_dp_aux_register(&ps_bridge->aux);
	if (ret) {
		dev_err(dev, "failed to register DP AUX channel: %d\n", ret);
		goto err_aux_register;
	}

	/* Attach the panel-bridge to the dsi bridge */
	return drm_bridge_attach(bridge->encoder, ps_bridge->panel_bridge,
				 &ps_bridge->bridge, flags);

err_aux_register:
	mipi_dsi_detach(dsi);
err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
	return ret;
}

static void ps8640_bridge_detach(struct drm_bridge *bridge)
{
	drm_dp_aux_unregister(&bridge_to_ps8640(bridge)->aux);
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
	.detach = ps8640_bridge_detach,
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

	ps_bridge->regmap[PAGE0_DP_CNTL] = devm_regmap_init_i2c(client, ps8640_regmap_config);
	if (IS_ERR(ps_bridge->regmap[PAGE0_DP_CNTL]))
		return PTR_ERR(ps_bridge->regmap[PAGE0_DP_CNTL]);

	for (i = 1; i < ARRAY_SIZE(ps_bridge->page); i++) {
		ps_bridge->page[i] = devm_i2c_new_dummy_device(&client->dev,
							     client->adapter,
							     client->addr + i);
		if (IS_ERR(ps_bridge->page[i]))
			return PTR_ERR(ps_bridge->page[i]);

		ps_bridge->regmap[i] = devm_regmap_init_i2c(ps_bridge->page[i],
							    ps8640_regmap_config + i);
		if (IS_ERR(ps_bridge->regmap[i]))
			return PTR_ERR(ps_bridge->regmap[i]);
	}

	i2c_set_clientdata(client, ps_bridge);

	ps_bridge->aux.name = "parade-ps8640-aux";
	ps_bridge->aux.dev = dev;
	ps_bridge->aux.transfer = ps8640_aux_transfer;
	drm_dp_aux_init(&ps_bridge->aux);

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
