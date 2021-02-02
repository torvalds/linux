// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rk628.h>

enum {
	RK628_IRQ_HDMITX_HPD_HIGH,
	RK628_IRQ_HDMITX_HPD_LOW,
	RK628_IRQ_HDMITX,
	RK628_IRQ_GVI,
	RK628_IRQ_DSI1,
	RK628_IRQ_DSI0,
	RK628_IRQ_CSI,
	RK628_IRQ_HDMIRX,

	RK628_IRQ_GPIO0,
	RK628_IRQ_GPIO1,
	RK628_IRQ_GPIO2,
	RK628_IRQ_GPIO3,
	RK628_IRQ_EFUSE,
};

static struct resource rk628_gpio_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_GPIO0),
	DEFINE_RES_IRQ(RK628_IRQ_GPIO1),
	DEFINE_RES_IRQ(RK628_IRQ_GPIO2),
	DEFINE_RES_IRQ(RK628_IRQ_GPIO3),
};

static struct resource rk628_dsi0_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_DSI0),
};

static struct resource rk628_dsi1_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_DSI1),
};

static struct resource rk628_csi_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_CSI),
	DEFINE_RES_IRQ(RK628_IRQ_HDMIRX),
};

static struct resource rk628_gvi_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_GVI),
};

static struct resource rk628_hdmi_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_HDMITX),
	DEFINE_RES_IRQ(RK628_IRQ_HDMITX_HPD_HIGH),
	DEFINE_RES_IRQ(RK628_IRQ_HDMITX_HPD_LOW),
};

static struct resource rk628_hdmirx_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_HDMIRX),
};

static struct resource rk628_efuse_resources[] = {
	DEFINE_RES_IRQ(RK628_IRQ_EFUSE),
};

static const struct mfd_cell rk628_devs[] = {
	{
		.name = "rk628-cru",
		.of_compatible = "rockchip,rk628-cru",
	}, {
		.name = "rk628-pinctrl",
		.of_compatible = "rockchip,rk628-pinctrl",
		.resources = rk628_gpio_resources,
		.num_resources = ARRAY_SIZE(rk628_gpio_resources),
	}, {
		.name = "rk628-combrxphy",
		.of_compatible = "rockchip,rk628-combrxphy",
	}, {
		.name = "rk628-combtxphy",
		.of_compatible = "rockchip,rk628-combtxphy",
	}, {
		.name = "rk628-csi",
		.of_compatible = "rockchip,rk628-csi",
		.resources = rk628_csi_resources,
		.num_resources = ARRAY_SIZE(rk628_csi_resources),
	}, {
		.name = "rk628-hdmirx",
		.of_compatible = "rockchip,rk628-hdmirx",
		.resources = rk628_hdmirx_resources,
		.num_resources = ARRAY_SIZE(rk628_hdmirx_resources),
	}, {
		.name = "rk628-dsi1",
		.of_compatible = "rockchip,rk628-dsi1",
		.resources = rk628_dsi1_resources,
		.num_resources = ARRAY_SIZE(rk628_dsi1_resources),
	}, {
		.name = "rk628-dsi0",
		.of_compatible = "rockchip,rk628-dsi0",
		.resources = rk628_dsi0_resources,
		.num_resources = ARRAY_SIZE(rk628_dsi0_resources),
	}, {
		.name = "rk628-rgb-tx",
		.of_compatible = "rockchip,rk628-rgb-tx",
	}, {
		.name = "rk628-yuv-rx",
		.of_compatible = "rockchip,rk628-yuv-rx",
	}, {
		.name = "rk628-yuv-tx",
		.of_compatible = "rockchip,rk628-yuv-tx",
	}, {
		.name = "rk628-bt1120-rx",
		.of_compatible = "rockchip,rk628-bt1120-rx",
	}, {
		.name = "rk628-bt1120-tx",
		.of_compatible = "rockchip,rk628-bt1120-tx",
	}, {
		.name = "rk628-lvds",
		.of_compatible = "rockchip,rk628-lvds",
	}, {
		.name = "rk628-gvi",
		.of_compatible = "rockchip,rk628-gvi",
		.resources = rk628_gvi_resources,
		.num_resources = ARRAY_SIZE(rk628_gvi_resources),
	}, {
		.name = "rk628-hdmi",
		.of_compatible = "rockchip,rk628-hdmi",
		.resources = rk628_hdmi_resources,
		.num_resources = ARRAY_SIZE(rk628_hdmi_resources),
	}, {
		.name = "rk628-efuse",
		.of_compatible = "rockchip,rk628-efuse",
		.resources = rk628_efuse_resources,
		.num_resources = ARRAY_SIZE(rk628_efuse_resources),
	}, {
		.name = "rk628-post-process",
		.of_compatible = "rockchip,rk628-post-process",
	},
};

static const struct regmap_irq rk628_irqs[] = {
	REGMAP_IRQ_REG(RK628_IRQ_HDMITX_HPD_HIGH, 0, BIT(0)),
	REGMAP_IRQ_REG(RK628_IRQ_HDMITX_HPD_LOW, 0, BIT(1)),
	REGMAP_IRQ_REG(RK628_IRQ_HDMITX, 0, BIT(2)),
	REGMAP_IRQ_REG(RK628_IRQ_GVI, 0, BIT(3)),
	REGMAP_IRQ_REG(RK628_IRQ_DSI1, 0, BIT(4)),
	REGMAP_IRQ_REG(RK628_IRQ_DSI0, 0, BIT(5)),
	REGMAP_IRQ_REG(RK628_IRQ_CSI, 0, BIT(6)),
	REGMAP_IRQ_REG(RK628_IRQ_HDMIRX, 0, BIT(8)),

	REGMAP_IRQ_REG(RK628_IRQ_GPIO0, 4, BIT(0)),
	REGMAP_IRQ_REG(RK628_IRQ_GPIO1, 4, BIT(1)),
	REGMAP_IRQ_REG(RK628_IRQ_GPIO2, 4, BIT(2)),
	REGMAP_IRQ_REG(RK628_IRQ_GPIO3, 4, BIT(3)),
	REGMAP_IRQ_REG(RK628_IRQ_EFUSE, 4, BIT(4)),
};

static struct rk628_irq_chip_data rk628_irq_chip_data = {
	.name = "rk628",
	.irqs = rk628_irqs,
	.num_irqs = ARRAY_SIZE(rk628_irqs),
	.num_regs = 2,
	.irq_reg_stride = (GRF_INTR1_STATUS - GRF_INTR0_STATUS) / 4,
	.reg_stride = 4,
	.status_base = GRF_INTR0_STATUS,
	.mask_base = GRF_INTR0_EN,
	.ack_base = GRF_INTR0_CLR_EN,
};

static const struct regmap_config rk628_grf_regmap_config = {
	.name = "grf",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = GRF_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static inline const struct regmap_irq *
irq_to_regmap_irq(struct rk628_irq_chip_data *d, int irq)
{
	return &d->irqs[irq];
}

static void rk628_irq_lock(struct irq_data *data)
{
	struct rk628_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	mutex_lock(&d->lock);
}

static void rk628_irq_sync_unlock(struct irq_data *data)
{
	struct rk628_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	int i;
	u32 reg, mask, val;

	for (i = 0; i < d->num_regs; i++) {
		reg = d->mask_base + (i * d->reg_stride * d->irq_reg_stride);
		mask = d->mask_buf_def[i];
		val = mask << 16 | (~d->mask_buf[i] & mask);
		regmap_write(d->map, reg, val);
	}

	mutex_unlock(&d->lock);
}

static void rk628_irq_enable(struct irq_data *data)
{
	struct rk628_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	d->mask_buf[irq_data->reg_offset / d->reg_stride] &= ~irq_data->mask;
}

static void rk628_irq_disable(struct irq_data *data)
{
	struct rk628_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	d->mask_buf[irq_data->reg_offset / d->reg_stride] |= irq_data->mask;
}

static const struct irq_chip rk628_irq_chip = {
	.irq_bus_lock = rk628_irq_lock,
	.irq_bus_sync_unlock = rk628_irq_sync_unlock,
	.irq_disable = rk628_irq_disable,
	.irq_enable = rk628_irq_enable,
};

static irqreturn_t rk628_irq_thread(int irq, void *data)
{
	struct rk628_irq_chip_data *d = data;
	int i;
	bool handled = false;
	u32 reg;

	for (i = 0; i < d->num_regs; i++) {
		reg = d->status_base + (i * d->reg_stride * d->irq_reg_stride);
		regmap_read(d->map, reg, &d->status_buf[i]);
	}

	for (i = 0; i < d->num_irqs; i++) {
		if (d->status_buf[d->irqs[i].reg_offset / d->reg_stride] & d->irqs[i].mask) {
			handle_nested_irq(irq_find_mapping(d->domain, i));
			handled = true;
		}
	}

	for (i = 0; i < d->num_regs; i++) {
		if (d->status_buf[i]) {
			reg = d->ack_base + (i * d->reg_stride * d->irq_reg_stride);
			regmap_write(d->map, reg,
				     d->status_buf[i] << 16 | d->status_buf[i]);
		}
	}

	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int rk628_irq_map(struct irq_domain *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct rk628_irq_chip_data *d = h->host_data;

	irq_set_chip_data(virq, d);
	irq_set_chip(virq, &d->irq_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_parent(virq, d->irq);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops rk628_irq_domain_ops = {
	.map = rk628_irq_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static int rk628_irq_init(struct rk628 *rk628, int irq)
{
	struct device *dev = rk628->dev;
	struct rk628_irq_chip_data *d = rk628->irq_data;
	struct regmap *map = rk628->grf;
	u32 reg, mask, val;
	int i;
	int ret;

	if (d->num_regs <= 0)
		return -EINVAL;

	d->status_buf = devm_kcalloc(dev, d->num_regs, sizeof(unsigned int),
				     GFP_KERNEL);
	if (!d->status_buf)
		return -ENOMEM;

	d->mask_buf = devm_kcalloc(dev, d->num_regs, sizeof(unsigned int),
				   GFP_KERNEL);
	if (!d->mask_buf)
		return -ENOMEM;

	d->mask_buf_def = devm_kcalloc(dev, d->num_regs, sizeof(unsigned int),
				       GFP_KERNEL);
	if (!d->mask_buf_def)
		return -ENOMEM;

	d->irq_chip = rk628_irq_chip;
	d->irq_chip.name = d->name;
	d->irq = irq;
	d->map = map;

	mutex_init(&d->lock);

	for (i = 0; i < d->num_irqs; i++)
		d->mask_buf_def[d->irqs[i].reg_offset / d->reg_stride] |= d->irqs[i].mask;

	/* Mask all the interrupts by default */
	for (i = 0; i < d->num_regs; i++) {
		d->mask_buf[i] = d->mask_buf_def[i];
		reg = d->mask_base + (i * d->reg_stride * d->irq_reg_stride);
		mask = d->mask_buf[i];
		val = mask << 16 | (~d->mask_buf[i] & mask);
		regmap_write(d->map, reg, val);
	}

	d->domain = irq_domain_add_linear(dev->of_node, d->num_irqs,
					  &rk628_irq_domain_ops, d);
	if (!d->domain) {
		dev_err(dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL, rk628_irq_thread,
					IRQF_ONESHOT, d->name, d);
	if (ret != 0) {
		irq_domain_remove(d->domain);
		dev_err(dev, "Failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}

	return 0;
}

static void rk628_irq_exit(struct rk628 *rk628)
{
	struct rk628_irq_chip_data *d = rk628->irq_data;
	unsigned int virq;
	int hwirq;

	/* Dispose all virtual irq from irq domain before removing it */
	for (hwirq = 0; hwirq < d->num_irqs; hwirq++) {
		/* Ignore hwirq if holes in the IRQ list */
		if (!d->irqs[hwirq].mask)
			continue;

		/*
		 * Find the virtual irq of hwirq on chip and if it is
		 * there then dispose it
		 */
		virq = irq_find_mapping(d->domain, hwirq);
		if (virq)
			irq_dispose_mapping(virq);
	}

	irq_domain_remove(d->domain);
}

static int
rk628_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct rk628 *rk628;
	int ret;

	rk628 = devm_kzalloc(dev, sizeof(*rk628), GFP_KERNEL);
	if (!rk628)
		return -ENOMEM;

	rk628->dev = dev;
	rk628->client = client;
	rk628->irq_data = &rk628_irq_chip_data;
	i2c_set_clientdata(client, rk628);

	rk628->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(rk628->enable_gpio)) {
		ret = PTR_ERR(rk628->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	rk628->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(rk628->reset_gpio)) {
		ret = PTR_ERR(rk628->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	gpiod_set_value(rk628->enable_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(rk628->reset_gpio, 0);
	usleep_range(10000, 11000);

	rk628->grf = devm_regmap_init_i2c(client, &rk628_grf_regmap_config);
	if (IS_ERR(rk628->grf)) {
		ret = PTR_ERR(rk628->grf);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* selete int io function */
	ret = regmap_write(rk628->grf, GRF_GPIO3AB_SEL_CON, 0x30002000);
	if (ret) {
		dev_err(dev, "failed to access register: %d\n", ret);
		return ret;
	}

	ret = rk628_irq_init(rk628, client->irq);
	if (ret) {
		dev_err(dev, "failed to add IRQ chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(dev, PLATFORM_DEVID_NONE,
			      rk628_devs, ARRAY_SIZE(rk628_devs),
			      NULL, 0, rk628->irq_data->domain);
	if (ret) {
		rk628_irq_exit(rk628);
		dev_err(dev, "Failed to add MFD children: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk628_i2c_remove(struct i2c_client *client)
{
	struct rk628 *rk628 = i2c_get_clientdata(client);

	mfd_remove_devices(rk628->dev);
	rk628_irq_exit(rk628);

	return 0;
}

static const struct of_device_id rk628_of_match[] = {
	{ .compatible = "rockchip,rk628", },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_of_match);

static const struct i2c_device_id rk628_i2c_id[] = {
	{ "rk628", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rk628_i2c_id);

static struct i2c_driver rk628_i2c_driver = {
	.driver = {
		.name = "rk628",
		.of_match_table = of_match_ptr(rk628_of_match),
	},
	.probe = rk628_i2c_probe,
	.remove = rk628_i2c_remove,
	.id_table = rk628_i2c_id,
};
module_i2c_driver(rk628_i2c_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 MFD driver");
MODULE_LICENSE("GPL v2");
