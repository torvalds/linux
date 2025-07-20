// SPDX-License-Identifier: GPL-2.0+
/*
 * Parade ps883x usb retimer driver
 *
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <drm/bridge/aux-bridge.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>

#define REG_USB_PORT_CONN_STATUS_0		0x00

#define CONN_STATUS_0_CONNECTION_PRESENT	BIT(0)
#define CONN_STATUS_0_ORIENTATION_REVERSED	BIT(1)
#define CONN_STATUS_0_USB_3_1_CONNECTED		BIT(5)

#define REG_USB_PORT_CONN_STATUS_1		0x01

#define CONN_STATUS_1_DP_CONNECTED		BIT(0)
#define CONN_STATUS_1_DP_SINK_REQUESTED		BIT(1)
#define CONN_STATUS_1_DP_PIN_ASSIGNMENT_C_D	BIT(2)
#define CONN_STATUS_1_DP_HPD_LEVEL		BIT(7)

#define REG_USB_PORT_CONN_STATUS_2		0x02

struct ps883x_retimer {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct regmap *regmap;
	struct typec_switch_dev *sw;
	struct typec_retimer *retimer;
	struct clk *xo_clk;
	struct regulator *vdd_supply;
	struct regulator *vdd33_supply;
	struct regulator *vdd33_cap_supply;
	struct regulator *vddat_supply;
	struct regulator *vddar_supply;
	struct regulator *vddio_supply;

	struct typec_switch *typec_switch;
	struct typec_mux *typec_mux;

	struct mutex lock; /* protect non-concurrent retimer & switch */

	enum typec_orientation orientation;
	unsigned long mode;
	unsigned int svid;
};

static int ps883x_configure(struct ps883x_retimer *retimer, int cfg0,
			    int cfg1, int cfg2)
{
	struct device *dev = &retimer->client->dev;
	int ret;

	ret = regmap_write(retimer->regmap, REG_USB_PORT_CONN_STATUS_0, cfg0);
	if (ret) {
		dev_err(dev, "failed to write conn_status_0: %d\n", ret);
		return ret;
	}

	ret = regmap_write(retimer->regmap, REG_USB_PORT_CONN_STATUS_1, cfg1);
	if (ret) {
		dev_err(dev, "failed to write conn_status_1: %d\n", ret);
		return ret;
	}

	ret = regmap_write(retimer->regmap, REG_USB_PORT_CONN_STATUS_2, cfg2);
	if (ret) {
		dev_err(dev, "failed to write conn_status_2: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ps883x_set(struct ps883x_retimer *retimer)
{
	int cfg0 = CONN_STATUS_0_CONNECTION_PRESENT;
	int cfg1 = 0x00;
	int cfg2 = 0x00;

	if (retimer->orientation == TYPEC_ORIENTATION_NONE ||
	    retimer->mode == TYPEC_STATE_SAFE) {
		return ps883x_configure(retimer, cfg0, cfg1, cfg2);
	}

	if (retimer->mode != TYPEC_STATE_USB && retimer->svid != USB_TYPEC_DP_SID)
		return -EINVAL;

	if (retimer->orientation == TYPEC_ORIENTATION_REVERSE)
		cfg0 |= CONN_STATUS_0_ORIENTATION_REVERSED;

	switch (retimer->mode) {
	case TYPEC_STATE_USB:
		cfg0 |= CONN_STATUS_0_USB_3_1_CONNECTED;
		break;

	case TYPEC_DP_STATE_C:
		cfg1 = CONN_STATUS_1_DP_CONNECTED |
		       CONN_STATUS_1_DP_SINK_REQUESTED |
		       CONN_STATUS_1_DP_PIN_ASSIGNMENT_C_D |
		       CONN_STATUS_1_DP_HPD_LEVEL;
		break;

	case TYPEC_DP_STATE_D:
		cfg0 |= CONN_STATUS_0_USB_3_1_CONNECTED;
		cfg1 = CONN_STATUS_1_DP_CONNECTED |
		       CONN_STATUS_1_DP_SINK_REQUESTED |
		       CONN_STATUS_1_DP_PIN_ASSIGNMENT_C_D |
		       CONN_STATUS_1_DP_HPD_LEVEL;
		break;

	case TYPEC_DP_STATE_E:
		cfg1 = CONN_STATUS_1_DP_CONNECTED |
		       CONN_STATUS_1_DP_HPD_LEVEL;
		break;

	default:
		return -EOPNOTSUPP;
	}

	return ps883x_configure(retimer, cfg0, cfg1, cfg2);
}

static int ps883x_sw_set(struct typec_switch_dev *sw,
			 enum typec_orientation orientation)
{
	struct ps883x_retimer *retimer = typec_switch_get_drvdata(sw);
	int ret = 0;

	ret = typec_switch_set(retimer->typec_switch, orientation);
	if (ret)
		return ret;

	mutex_lock(&retimer->lock);

	if (retimer->orientation != orientation) {
		retimer->orientation = orientation;

		ret = ps883x_set(retimer);
	}

	mutex_unlock(&retimer->lock);

	return ret;
}

static int ps883x_retimer_set(struct typec_retimer *rtmr,
			      struct typec_retimer_state *state)
{
	struct ps883x_retimer *retimer = typec_retimer_get_drvdata(rtmr);
	struct typec_mux_state mux_state;
	int ret = 0;

	mutex_lock(&retimer->lock);

	if (state->mode != retimer->mode) {
		retimer->mode = state->mode;

		if (state->alt)
			retimer->svid = state->alt->svid;
		else
			retimer->svid = 0;

		ret = ps883x_set(retimer);
	}

	mutex_unlock(&retimer->lock);

	if (ret)
		return ret;

	mux_state.alt = state->alt;
	mux_state.data = state->data;
	mux_state.mode = state->mode;

	return typec_mux_set(retimer->typec_mux, &mux_state);
}

static int ps883x_enable_vregs(struct ps883x_retimer *retimer)
{
	struct device *dev = &retimer->client->dev;
	int ret;

	ret = regulator_enable(retimer->vdd33_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD 3.3V regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(retimer->vdd33_cap_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD 3.3V CAP regulator: %d\n", ret);
		goto err_vdd33_disable;
	}

	usleep_range(4000, 10000);

	ret = regulator_enable(retimer->vdd_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD regulator: %d\n", ret);
		goto err_vdd33_cap_disable;
	}

	ret = regulator_enable(retimer->vddar_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD AR regulator: %d\n", ret);
		goto err_vdd_disable;
	}

	ret = regulator_enable(retimer->vddat_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD AT regulator: %d\n", ret);
		goto err_vddar_disable;
	}

	ret = regulator_enable(retimer->vddio_supply);
	if (ret) {
		dev_err(dev, "cannot enable VDD IO regulator: %d\n", ret);
		goto err_vddat_disable;
	}

	return 0;

err_vddat_disable:
	regulator_disable(retimer->vddat_supply);
err_vddar_disable:
	regulator_disable(retimer->vddar_supply);
err_vdd_disable:
	regulator_disable(retimer->vdd_supply);
err_vdd33_cap_disable:
	regulator_disable(retimer->vdd33_cap_supply);
err_vdd33_disable:
	regulator_disable(retimer->vdd33_supply);

	return ret;
}

static void ps883x_disable_vregs(struct ps883x_retimer *retimer)
{
	regulator_disable(retimer->vddio_supply);
	regulator_disable(retimer->vddat_supply);
	regulator_disable(retimer->vddar_supply);
	regulator_disable(retimer->vdd_supply);
	regulator_disable(retimer->vdd33_cap_supply);
	regulator_disable(retimer->vdd33_supply);
}

static int ps883x_get_vregs(struct ps883x_retimer *retimer)
{
	struct device *dev = &retimer->client->dev;

	retimer->vdd_supply = devm_regulator_get(dev, "vdd");
	if (IS_ERR(retimer->vdd_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vdd_supply),
				     "failed to get VDD\n");

	retimer->vdd33_supply = devm_regulator_get(dev, "vdd33");
	if (IS_ERR(retimer->vdd33_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vdd33_supply),
				     "failed to get VDD 3.3V\n");

	retimer->vdd33_cap_supply = devm_regulator_get(dev, "vdd33-cap");
	if (IS_ERR(retimer->vdd33_cap_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vdd33_cap_supply),
				     "failed to get VDD CAP 3.3V\n");

	retimer->vddat_supply = devm_regulator_get(dev, "vddat");
	if (IS_ERR(retimer->vddat_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vddat_supply),
				     "failed to get VDD AT\n");

	retimer->vddar_supply = devm_regulator_get(dev, "vddar");
	if (IS_ERR(retimer->vddar_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vddar_supply),
				     "failed to get VDD AR\n");

	retimer->vddio_supply = devm_regulator_get(dev, "vddio");
	if (IS_ERR(retimer->vddio_supply))
		return dev_err_probe(dev, PTR_ERR(retimer->vddio_supply),
				     "failed to get VDD IO\n");

	return 0;
}

static const struct regmap_config ps883x_retimer_regmap = {
	.max_register = 0x1f,
	.reg_bits = 8,
	.val_bits = 8,
};

static int ps883x_retimer_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct typec_switch_desc sw_desc = { };
	struct typec_retimer_desc rtmr_desc = { };
	struct ps883x_retimer *retimer;
	unsigned int val;
	int ret;

	retimer = devm_kzalloc(dev, sizeof(*retimer), GFP_KERNEL);
	if (!retimer)
		return -ENOMEM;

	retimer->client = client;

	mutex_init(&retimer->lock);

	retimer->regmap = devm_regmap_init_i2c(client, &ps883x_retimer_regmap);
	if (IS_ERR(retimer->regmap))
		return dev_err_probe(dev, PTR_ERR(retimer->regmap),
				     "failed to allocate register map\n");

	ret = ps883x_get_vregs(retimer);
	if (ret)
		return ret;

	retimer->xo_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(retimer->xo_clk))
		return dev_err_probe(dev, PTR_ERR(retimer->xo_clk),
				     "failed to get xo clock\n");

	retimer->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(retimer->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(retimer->reset_gpio),
				     "failed to get reset gpio\n");

	retimer->typec_switch = typec_switch_get(dev);
	if (IS_ERR(retimer->typec_switch))
		return dev_err_probe(dev, PTR_ERR(retimer->typec_switch),
				     "failed to acquire orientation-switch\n");

	retimer->typec_mux = typec_mux_get(dev);
	if (IS_ERR(retimer->typec_mux)) {
		ret = dev_err_probe(dev, PTR_ERR(retimer->typec_mux),
				    "failed to acquire mode-mux\n");
		goto err_switch_put;
	}

	ret = drm_aux_bridge_register(dev);
	if (ret)
		goto err_mux_put;

	ret = ps883x_enable_vregs(retimer);
	if (ret)
		goto err_mux_put;

	ret = clk_prepare_enable(retimer->xo_clk);
	if (ret) {
		dev_err(dev, "failed to enable XO: %d\n", ret);
		goto err_vregs_disable;
	}

	/* skip resetting if already configured */
	if (regmap_test_bits(retimer->regmap, REG_USB_PORT_CONN_STATUS_0,
			     CONN_STATUS_0_CONNECTION_PRESENT) == 1) {
		gpiod_direction_output(retimer->reset_gpio, 0);
	} else {
		gpiod_direction_output(retimer->reset_gpio, 1);

		/* VDD IO supply enable to reset release delay */
		usleep_range(4000, 14000);

		gpiod_set_value(retimer->reset_gpio, 0);

		/* firmware initialization delay */
		msleep(60);

		/* make sure device is accessible */
		ret = regmap_read(retimer->regmap, REG_USB_PORT_CONN_STATUS_0,
				  &val);
		if (ret) {
			dev_err(dev, "failed to read conn_status_0: %d\n", ret);
			if (ret == -ENXIO)
				ret = -EIO;
			goto err_clk_disable;
		}
	}

	sw_desc.drvdata = retimer;
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = ps883x_sw_set;

	retimer->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(retimer->sw)) {
		ret = PTR_ERR(retimer->sw);
		dev_err(dev, "failed to register typec switch: %d\n", ret);
		goto err_clk_disable;
	}

	rtmr_desc.drvdata = retimer;
	rtmr_desc.fwnode = dev_fwnode(dev);
	rtmr_desc.set = ps883x_retimer_set;

	retimer->retimer = typec_retimer_register(dev, &rtmr_desc);
	if (IS_ERR(retimer->retimer)) {
		ret = PTR_ERR(retimer->retimer);
		dev_err(dev, "failed to register typec retimer: %d\n", ret);
		goto err_switch_unregister;
	}

	return 0;

err_switch_unregister:
	typec_switch_unregister(retimer->sw);
err_clk_disable:
	clk_disable_unprepare(retimer->xo_clk);
err_vregs_disable:
	gpiod_set_value(retimer->reset_gpio, 1);
	ps883x_disable_vregs(retimer);
err_mux_put:
	typec_mux_put(retimer->typec_mux);
err_switch_put:
	typec_switch_put(retimer->typec_switch);

	return ret;
}

static void ps883x_retimer_remove(struct i2c_client *client)
{
	struct ps883x_retimer *retimer = i2c_get_clientdata(client);

	typec_retimer_unregister(retimer->retimer);
	typec_switch_unregister(retimer->sw);

	gpiod_set_value(retimer->reset_gpio, 1);

	clk_disable_unprepare(retimer->xo_clk);

	ps883x_disable_vregs(retimer);

	typec_mux_put(retimer->typec_mux);
	typec_switch_put(retimer->typec_switch);
}

static const struct of_device_id ps883x_retimer_of_table[] = {
	{ .compatible = "parade,ps8830" },
	{ }
};
MODULE_DEVICE_TABLE(of, ps883x_retimer_of_table);

static struct i2c_driver ps883x_retimer_driver = {
	.driver = {
		.name = "ps883x_retimer",
		.of_match_table = ps883x_retimer_of_table,
	},
	.probe		= ps883x_retimer_probe,
	.remove		= ps883x_retimer_remove,
};

module_i2c_driver(ps883x_retimer_driver);

MODULE_DESCRIPTION("Parade ps883x Type-C Retimer driver");
MODULE_LICENSE("GPL");
