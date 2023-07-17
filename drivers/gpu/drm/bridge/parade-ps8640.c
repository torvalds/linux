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
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
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
	struct device_link *link;
	bool pre_enabled;
	bool need_post_hpd_delay;
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

static int _ps8640_wait_hpd_asserted(struct ps8640 *ps_bridge, unsigned long wait_us)
{
	struct regmap *map = ps_bridge->regmap[PAGE2_TOP_CNTL];
	int status;
	int ret;

	/*
	 * Apparently something about the firmware in the chip signals that
	 * HPD goes high by reporting GPIO9 as high (even though HPD isn't
	 * actually connected to GPIO9).
	 */
	ret = regmap_read_poll_timeout(map, PAGE2_GPIO_H, status,
				       status & PS_GPIO9, 20000, wait_us);

	/*
	 * The first time we see HPD go high after a reset we delay an extra
	 * 50 ms. The best guess is that the MCU is doing "stuff" during this
	 * time (maybe talking to the panel) and we don't want to interrupt it.
	 *
	 * No locking is done around "need_post_hpd_delay". If we're here we
	 * know we're holding a PM Runtime reference and the only other place
	 * that touches this is PM Runtime resume.
	 */
	if (!ret && ps_bridge->need_post_hpd_delay) {
		ps_bridge->need_post_hpd_delay = false;
		msleep(50);
	}

	return ret;
}

static int ps8640_wait_hpd_asserted(struct drm_dp_aux *aux, unsigned long wait_us)
{
	struct ps8640 *ps_bridge = aux_to_ps8640(aux);
	struct device *dev = &ps_bridge->page[PAGE0_DP_CNTL]->dev;
	int ret;

	/*
	 * Note that this function is called by code that has already powered
	 * the panel. We have to power ourselves up but we don't need to worry
	 * about powering the panel.
	 */
	pm_runtime_get_sync(dev);
	ret = _ps8640_wait_hpd_asserted(ps_bridge, wait_us);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static ssize_t ps8640_aux_transfer_msg(struct drm_dp_aux *aux,
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
	case SWAUX_STATUS_DEFER:
	case SWAUX_STATUS_I2C_DEFER:
		if (is_native_aux)
			msg->reply |= DP_AUX_NATIVE_REPLY_DEFER;
		else
			msg->reply |= DP_AUX_I2C_REPLY_DEFER;
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

static ssize_t ps8640_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct ps8640 *ps_bridge = aux_to_ps8640(aux);
	struct device *dev = &ps_bridge->page[PAGE0_DP_CNTL]->dev;
	int ret;

	pm_runtime_get_sync(dev);
	ret = ps8640_aux_transfer_msg(aux, msg);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static void ps8640_bridge_vdo_control(struct ps8640 *ps_bridge,
				      const enum ps8640_vdo_control ctrl)
{
	struct regmap *map = ps_bridge->regmap[PAGE3_DSI_CNTL1];
	struct device *dev = &ps_bridge->page[PAGE3_DSI_CNTL1]->dev;
	u8 vdo_ctrl_buf[] = { VDO_CTL_ADD, ctrl };
	int ret;

	ret = regmap_bulk_write(map, PAGE3_SET_ADD,
				vdo_ctrl_buf, sizeof(vdo_ctrl_buf));

	if (ret < 0)
		dev_err(dev, "failed to %sable VDO: %d\n",
			ctrl == ENABLE ? "en" : "dis", ret);
}

static int __maybe_unused ps8640_resume(struct device *dev)
{
	struct ps8640 *ps_bridge = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ps_bridge->supplies),
				    ps_bridge->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators %d\n", ret);
		return ret;
	}

	gpiod_set_value(ps_bridge->gpio_powerdown, 0);
	gpiod_set_value(ps_bridge->gpio_reset, 1);
	usleep_range(2000, 2500);
	gpiod_set_value(ps_bridge->gpio_reset, 0);
	/* Double reset for T4 and T5 */
	msleep(50);
	gpiod_set_value(ps_bridge->gpio_reset, 1);
	msleep(50);
	gpiod_set_value(ps_bridge->gpio_reset, 0);

	/* We just reset things, so we need a delay after the first HPD */
	ps_bridge->need_post_hpd_delay = true;

	/*
	 * Mystery 200 ms delay for the "MCU to be ready". It's unclear if
	 * this is truly necessary since the MCU will already signal that
	 * things are "good to go" by signaling HPD on "gpio 9". See
	 * _ps8640_wait_hpd_asserted(). For now we'll keep this mystery delay
	 * just in case.
	 */
	msleep(200);

	return 0;
}

static int __maybe_unused ps8640_suspend(struct device *dev)
{
	struct ps8640 *ps_bridge = dev_get_drvdata(dev);
	int ret;

	gpiod_set_value(ps_bridge->gpio_reset, 1);
	gpiod_set_value(ps_bridge->gpio_powerdown, 1);
	ret = regulator_bulk_disable(ARRAY_SIZE(ps_bridge->supplies),
				     ps_bridge->supplies);
	if (ret < 0)
		dev_err(dev, "cannot disable regulators %d\n", ret);

	return ret;
}

static const struct dev_pm_ops ps8640_pm_ops = {
	SET_RUNTIME_PM_OPS(ps8640_suspend, ps8640_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static void ps8640_atomic_pre_enable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);
	struct regmap *map = ps_bridge->regmap[PAGE2_TOP_CNTL];
	struct device *dev = &ps_bridge->page[PAGE0_DP_CNTL]->dev;
	int ret;

	pm_runtime_get_sync(dev);
	ret = _ps8640_wait_hpd_asserted(ps_bridge, 200 * 1000);
	if (ret < 0)
		dev_warn(dev, "HPD didn't go high: %d\n", ret);

	/*
	 * The Manufacturer Command Set (MCS) is a device dependent interface
	 * intended for factory programming of the display module default
	 * parameters. Once the display module is configured, the MCS shall be
	 * disabled by the manufacturer. Once disabled, all MCS commands are
	 * ignored by the display interface.
	 */

	ret = regmap_update_bits(map, PAGE2_MCS_EN, MCS_EN, 0);
	if (ret < 0)
		dev_warn(dev, "failed write PAGE2_MCS_EN: %d\n", ret);

	/* Switch access edp panel's edid through i2c */
	ret = regmap_write(map, PAGE2_I2C_BYPASS, I2C_BYPASS_EN);
	if (ret < 0)
		dev_warn(dev, "failed write PAGE2_MCS_EN: %d\n", ret);

	ps8640_bridge_vdo_control(ps_bridge, ENABLE);

	ps_bridge->pre_enabled = true;
}

static void ps8640_atomic_post_disable(struct drm_bridge *bridge,
				       struct drm_bridge_state *old_bridge_state)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);

	ps_bridge->pre_enabled = false;

	ps8640_bridge_vdo_control(ps_bridge, DISABLE);
	pm_runtime_put_sync_suspend(&ps_bridge->page[PAGE0_DP_CNTL]->dev);
}

static int ps8640_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);
	struct device *dev = &ps_bridge->page[0]->dev;
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	ps_bridge->aux.drm_dev = bridge->dev;
	ret = drm_dp_aux_register(&ps_bridge->aux);
	if (ret) {
		dev_err(dev, "failed to register DP AUX channel: %d\n", ret);
		return ret;
	}

	ps_bridge->link = device_link_add(bridge->dev->dev, dev, DL_FLAG_STATELESS);
	if (!ps_bridge->link) {
		dev_err(dev, "failed to create device link");
		ret = -EINVAL;
		goto err_devlink;
	}

	/* Attach the panel-bridge to the dsi bridge */
	ret = drm_bridge_attach(bridge->encoder, ps_bridge->panel_bridge,
				&ps_bridge->bridge, flags);
	if (ret)
		goto err_bridge_attach;

	return 0;

err_bridge_attach:
	device_link_del(ps_bridge->link);
err_devlink:
	drm_dp_aux_unregister(&ps_bridge->aux);

	return ret;
}

static void ps8640_bridge_detach(struct drm_bridge *bridge)
{
	struct ps8640 *ps_bridge = bridge_to_ps8640(bridge);

	drm_dp_aux_unregister(&ps_bridge->aux);
	if (ps_bridge->link)
		device_link_del(ps_bridge->link);
}

static void ps8640_runtime_disable(void *data)
{
	pm_runtime_dont_use_autosuspend(data);
	pm_runtime_disable(data);
}

static const struct drm_bridge_funcs ps8640_bridge_funcs = {
	.attach = ps8640_bridge_attach,
	.detach = ps8640_bridge_detach,
	.atomic_post_disable = ps8640_atomic_post_disable,
	.atomic_pre_enable = ps8640_atomic_pre_enable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int ps8640_bridge_get_dsi_resources(struct device *dev, struct ps8640 *ps_bridge)
{
	struct device_node *in_ep, *dsi_node;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	const struct mipi_dsi_device_info info = { .type = "ps8640",
						   .channel = 0,
						   .node = NULL,
						 };

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
		return -EPROBE_DEFER;

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		return PTR_ERR(dsi);
	}

	ps_bridge->dsi = dsi;

	dsi->host = host;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = NUM_MIPI_LANES;

	return 0;
}

static int ps8640_bridge_link_panel(struct drm_dp_aux *aux)
{
	struct ps8640 *ps_bridge = aux_to_ps8640(aux);
	struct device *dev = aux->dev;
	struct device_node *np = dev->of_node;
	int ret;

	/*
	 * NOTE about returning -EPROBE_DEFER from this function: if we
	 * return an error (most relevant to -EPROBE_DEFER) it will only
	 * be passed out to ps8640_probe() if it called this directly (AKA the
	 * panel isn't under the "aux-bus" node). That should be fine because
	 * if the panel is under "aux-bus" it's guaranteed to have probed by
	 * the time this function has been called.
	 */

	/* port@1 is ps8640 output port */
	ps_bridge->panel_bridge = devm_drm_of_get_bridge(dev, np, 1, 0);
	if (IS_ERR(ps_bridge->panel_bridge))
		return PTR_ERR(ps_bridge->panel_bridge);

	ret = devm_drm_bridge_add(dev, &ps_bridge->bridge);
	if (ret)
		return ret;

	return devm_mipi_dsi_attach(dev, ps_bridge->dsi);
}

static int ps8640_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ps8640 *ps_bridge;
	int ret;
	u32 i;

	ps_bridge = devm_kzalloc(dev, sizeof(*ps_bridge), GFP_KERNEL);
	if (!ps_bridge)
		return -ENOMEM;

	ps_bridge->supplies[0].supply = "vdd12";
	ps_bridge->supplies[1].supply = "vdd33";
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
	ps_bridge->bridge.type = DRM_MODE_CONNECTOR_eDP;

	/*
	 * Get MIPI DSI resources early. These can return -EPROBE_DEFER so
	 * we want to get them out of the way sooner.
	 */
	ret = ps8640_bridge_get_dsi_resources(&client->dev, ps_bridge);
	if (ret)
		return ret;

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
	ps_bridge->aux.wait_hpd_asserted = ps8640_wait_hpd_asserted;
	drm_dp_aux_init(&ps_bridge->aux);

	pm_runtime_enable(dev);
	/*
	 * Powering on ps8640 takes ~300ms. To avoid wasting time on power
	 * cycling ps8640 too often, set autosuspend_delay to 2000ms to ensure
	 * the bridge wouldn't suspend in between each _aux_transfer_msg() call
	 * during EDID read (~20ms in my experiment) and in between the last
	 * _aux_transfer_msg() call during EDID read and the _pre_enable() call
	 * (~100ms in my experiment).
	 */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	pm_suspend_ignore_children(dev, true);
	ret = devm_add_action_or_reset(dev, ps8640_runtime_disable, dev);
	if (ret)
		return ret;

	ret = devm_of_dp_aux_populate_bus(&ps_bridge->aux, ps8640_bridge_link_panel);

	/*
	 * If devm_of_dp_aux_populate_bus() returns -ENODEV then it's up to
	 * usa to call ps8640_bridge_link_panel() directly. NOTE: in this case
	 * the function is allowed to -EPROBE_DEFER.
	 */
	if (ret == -ENODEV)
		return ps8640_bridge_link_panel(&ps_bridge->aux);

	return ret;
}

static const struct of_device_id ps8640_match[] = {
	{ .compatible = "parade,ps8640" },
	{ }
};
MODULE_DEVICE_TABLE(of, ps8640_match);

static struct i2c_driver ps8640_driver = {
	.probe = ps8640_probe,
	.driver = {
		.name = "ps8640",
		.of_match_table = ps8640_match,
		.pm = &ps8640_pm_ops,
	},
};
module_i2c_driver(ps8640_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_AUTHOR("CK Hu <ck.hu@mediatek.com>");
MODULE_AUTHOR("Enric Balletbo i Serra <enric.balletbo@collabora.com>");
MODULE_DESCRIPTION("PARADE ps8640 DSI-eDP converter driver");
MODULE_LICENSE("GPL v2");
