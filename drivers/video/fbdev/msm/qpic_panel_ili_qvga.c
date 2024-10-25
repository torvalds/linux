// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2015, 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>

#include "mdss.h"
#include "mdss_qpic.h"
#include "mdss_qpic_panel.h"

static int mdss_qpic_pinctrl_set_state(struct qpic_panel_io_desc *qpic_panel_io,
		bool active);
static int panel_io_init(struct qpic_panel_io_desc *panel_io)
{
	int rc;

	if (panel_io->vdd_vreg) {
		rc = regulator_set_voltage(panel_io->vdd_vreg,
			1800000, 1800000);
		if (rc) {
			pr_err("vdd_vreg->set_voltage failed, rc=%d\n", rc);
			return -EINVAL;
		}
	}
	if (panel_io->avdd_vreg) {
		rc = regulator_set_voltage(panel_io->avdd_vreg,
			2704000, 2704000);
		if (rc) {
			pr_err("vdd_vreg->set_voltage failed, rc=%d\n", rc);
			return -EINVAL;
		}
	}
	return 0;
}

static void panel_io_off(struct qpic_panel_io_desc *qpic_panel_io)
{
	if (mdss_qpic_pinctrl_set_state(qpic_panel_io, false))
		pr_warn("%s panel on: pinctrl not enabled\n", __func__);

	if (qpic_panel_io->ad8_gpio)
		gpio_free(qpic_panel_io->ad8_gpio);
	if (qpic_panel_io->cs_gpio)
		gpio_free(qpic_panel_io->cs_gpio);
	if (qpic_panel_io->rst_gpio)
		gpio_free(qpic_panel_io->rst_gpio);
	if (qpic_panel_io->te_gpio)
		gpio_free(qpic_panel_io->te_gpio);
	if (qpic_panel_io->bl_gpio)
		gpio_free(qpic_panel_io->bl_gpio);
	if (qpic_panel_io->vdd_vreg)
		regulator_disable(qpic_panel_io->vdd_vreg);
	if (qpic_panel_io->avdd_vreg)
		regulator_disable(qpic_panel_io->avdd_vreg);
}

void ili9341_off(struct qpic_panel_io_desc *qpic_panel_io)
{
	qpic_send_pkt(OP_SET_DISPLAY_OFF, NULL, 0);
	/* wait for 20 ms after display off */
	msleep(20);
	panel_io_off(qpic_panel_io);
}

static int panel_io_on(struct qpic_panel_io_desc *qpic_panel_io)
{
	int rc;

	if (qpic_panel_io->vdd_vreg) {
		rc = regulator_enable(qpic_panel_io->vdd_vreg);
		if (rc) {
			pr_err("enable vdd failed, rc=%d\n", rc);
			return -ENODEV;
		}
	}

	if (qpic_panel_io->avdd_vreg) {
		rc = regulator_enable(qpic_panel_io->avdd_vreg);
		if (rc) {
			pr_err("enable avdd failed, rc=%d\n", rc);
			goto power_on_error;
		}
	}

	/* GPIO settings using pinctrl */
	if (mdss_qpic_pinctrl_set_state(qpic_panel_io, true)) {
		pr_warn("%s panel on: pinctrl not enabled\n", __func__);

		if ((qpic_panel_io->rst_gpio) &&
			(gpio_request(qpic_panel_io->rst_gpio, "disp_rst_n"))) {
			pr_err("%s request reset gpio failed\n", __func__);
			goto power_on_error;
		}

		if ((qpic_panel_io->cs_gpio) &&
			(gpio_request(qpic_panel_io->cs_gpio, "disp_cs_n"))) {
			pr_err("%s request cs gpio failed\n", __func__);
			goto power_on_error;
		}

		if ((qpic_panel_io->ad8_gpio) &&
			(gpio_request(qpic_panel_io->ad8_gpio, "disp_ad8_n"))) {
			pr_err("%s request ad8 gpio failed\n", __func__);
			goto power_on_error;
		}

		if ((qpic_panel_io->te_gpio) &&
			(gpio_request(qpic_panel_io->te_gpio, "disp_te_n"))) {
			pr_err("%s request te gpio failed\n", __func__);
			goto power_on_error;
		}

		if ((qpic_panel_io->bl_gpio) &&
			(gpio_request(qpic_panel_io->bl_gpio, "disp_bl_n"))) {
			pr_err("%s request bl gpio failed\n", __func__);
			goto power_on_error;
		}
	}

	/* wait for 20 ms after enable gpio as suggested by hw */
	msleep(20);
	return 0;
power_on_error:
	panel_io_off(qpic_panel_io);
	return -EINVAL;
}

int ili9341_on(struct qpic_panel_io_desc *qpic_panel_io)
{
	u8 param[4];
	int ret;

	if (!qpic_panel_io->init) {
		panel_io_init(qpic_panel_io);
		qpic_panel_io->init = true;
	}
	ret = panel_io_on(qpic_panel_io);
	if (ret)
		return ret;
	qpic_send_pkt(OP_SOFT_RESET, NULL, 0);
	/* wait for 120 ms after reset as panel spec suggests */
	msleep(120);
	qpic_send_pkt(OP_SET_DISPLAY_OFF, NULL, 0);
	/* wait for 20 ms after disply off */
	msleep(20);

	/* set memory access control */
	param[0] = 0x48;
	qpic_send_pkt(OP_SET_ADDRESS_MODE, param, 1);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	param[0] = 0x66;
	qpic_send_pkt(OP_SET_PIXEL_FORMAT, param, 1);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* set interface */
	param[0] = 1;
	param[1] = 0;
	param[2] = 0;
	qpic_send_pkt(OP_ILI9341_INTERFACE_CONTROL, param, 3);
	/* wait for 20 ms after command sent */
	msleep(20);

	/* exit sleep mode */
	qpic_send_pkt(OP_EXIT_SLEEP_MODE, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* normal mode */
	qpic_send_pkt(OP_ENTER_NORMAL_MODE, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* display on */
	qpic_send_pkt(OP_SET_DISPLAY_ON, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	param[0] = 0;
	qpic_send_pkt(OP_ILI9341_TEARING_EFFECT_LINE_ON, param, 1);

	/* test */
	param[0] = qpic_read_data(OP_GET_PIXEL_FORMAT, 1);
	pr_debug("pxl format =%x\n", param[0]);

	return 0;
}

static int mdss_qpic_pinctrl_set_state(struct qpic_panel_io_desc *qpic_panel_io,
		bool active)
{
	struct pinctrl_state *pin_state;
	int rc = -EFAULT;

	if (IS_ERR_OR_NULL(qpic_panel_io->pin_res.pinctrl))
		return PTR_ERR(qpic_panel_io->pin_res.pinctrl);

	if (active)
		gpio_direction_output(qpic_panel_io->bl_gpio, 1);
	else
		gpio_direction_output(qpic_panel_io->bl_gpio, 0);

	pin_state = active ? qpic_panel_io->pin_res.gpio_state_active
		: qpic_panel_io->pin_res.gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(qpic_panel_io->pin_res.pinctrl,
				pin_state);
		if (rc)
			pr_err("%s: can not set %s pins\n", __func__,
					active ? MDSS_PINCTRL_STATE_DEFAULT
					: MDSS_PINCTRL_STATE_SLEEP);
	} else {
		pr_err("%s: invalid '%s' pinstate\n", __func__,
				active ? MDSS_PINCTRL_STATE_DEFAULT
				: MDSS_PINCTRL_STATE_SLEEP);
	}
	return rc;
}
