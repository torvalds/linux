// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 * For more information on Raspberry Pi's PoE hat see:
 * https://www.raspberrypi.org/products/poe-hat/
 *
 * Limitations:
 *  - No disable bit, so a disabled PWM is simulated by duty_cycle 0
 *  - Only normal polarity
 *  - Fixed 12.5 kHz period
 *
 * The current period is completed when HW is reconfigured.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include <soc/bcm2835/raspberrypi-firmware.h>
#include <dt-bindings/pwm/raspberrypi,firmware-poe-pwm.h>

#define RPI_PWM_MAX_DUTY		255
#define RPI_PWM_PERIOD_NS		80000 /* 12.5 kHz */

#define RPI_PWM_CUR_DUTY_REG		0x0

struct raspberrypi_pwm {
	struct rpi_firmware *firmware;
	struct pwm_chip chip;
	unsigned int duty_cycle;
};

struct raspberrypi_pwm_prop {
	__le32 reg;
	__le32 val;
	__le32 ret;
} __packed;

static inline
struct raspberrypi_pwm *raspberrypi_pwm_from_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct raspberrypi_pwm, chip);
}

static int raspberrypi_pwm_set_property(struct rpi_firmware *firmware,
					u32 reg, u32 val)
{
	struct raspberrypi_pwm_prop msg = {
		.reg = cpu_to_le32(reg),
		.val = cpu_to_le32(val),
	};
	int ret;

	ret = rpi_firmware_property(firmware, RPI_FIRMWARE_SET_POE_HAT_VAL,
				    &msg, sizeof(msg));
	if (ret)
		return ret;
	if (msg.ret)
		return -EIO;

	return 0;
}

static int raspberrypi_pwm_get_property(struct rpi_firmware *firmware,
					u32 reg, u32 *val)
{
	struct raspberrypi_pwm_prop msg = {
		.reg = cpu_to_le32(reg),
	};
	int ret;

	ret = rpi_firmware_property(firmware, RPI_FIRMWARE_GET_POE_HAT_VAL,
				    &msg, sizeof(msg));
	if (ret)
		return ret;
	if (msg.ret)
		return -EIO;

	*val = le32_to_cpu(msg.val);

	return 0;
}

static int raspberrypi_pwm_get_state(struct pwm_chip *chip,
				     struct pwm_device *pwm,
				     struct pwm_state *state)
{
	struct raspberrypi_pwm *rpipwm = raspberrypi_pwm_from_chip(chip);

	state->period = RPI_PWM_PERIOD_NS;
	state->duty_cycle = DIV_ROUND_UP(rpipwm->duty_cycle * RPI_PWM_PERIOD_NS,
					 RPI_PWM_MAX_DUTY);
	state->enabled = !!(rpipwm->duty_cycle);
	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static int raspberrypi_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
				 const struct pwm_state *state)
{
	struct raspberrypi_pwm *rpipwm = raspberrypi_pwm_from_chip(chip);
	unsigned int duty_cycle;
	int ret;

	if (state->period < RPI_PWM_PERIOD_NS ||
	    state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled)
		duty_cycle = 0;
	else if (state->duty_cycle < RPI_PWM_PERIOD_NS)
		duty_cycle = DIV_ROUND_DOWN_ULL(state->duty_cycle * RPI_PWM_MAX_DUTY,
						RPI_PWM_PERIOD_NS);
	else
		duty_cycle = RPI_PWM_MAX_DUTY;

	if (duty_cycle == rpipwm->duty_cycle)
		return 0;

	ret = raspberrypi_pwm_set_property(rpipwm->firmware, RPI_PWM_CUR_DUTY_REG,
					   duty_cycle);
	if (ret) {
		dev_err(chip->dev, "Failed to set duty cycle: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	rpipwm->duty_cycle = duty_cycle;

	return 0;
}

static const struct pwm_ops raspberrypi_pwm_ops = {
	.get_state = raspberrypi_pwm_get_state,
	.apply = raspberrypi_pwm_apply,
	.owner = THIS_MODULE,
};

static int raspberrypi_pwm_probe(struct platform_device *pdev)
{
	struct device_node *firmware_node;
	struct device *dev = &pdev->dev;
	struct rpi_firmware *firmware;
	struct raspberrypi_pwm *rpipwm;
	int ret;

	firmware_node = of_get_parent(dev->of_node);
	if (!firmware_node) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	firmware = devm_rpi_firmware_get(&pdev->dev, firmware_node);
	of_node_put(firmware_node);
	if (!firmware)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "Failed to get firmware handle\n");

	rpipwm = devm_kzalloc(&pdev->dev, sizeof(*rpipwm), GFP_KERNEL);
	if (!rpipwm)
		return -ENOMEM;

	rpipwm->firmware = firmware;
	rpipwm->chip.dev = dev;
	rpipwm->chip.ops = &raspberrypi_pwm_ops;
	rpipwm->chip.npwm = RASPBERRYPI_FIRMWARE_PWM_NUM;

	ret = raspberrypi_pwm_get_property(rpipwm->firmware, RPI_PWM_CUR_DUTY_REG,
					   &rpipwm->duty_cycle);
	if (ret) {
		dev_err(dev, "Failed to get duty cycle: %pe\n", ERR_PTR(ret));
		return ret;
	}

	return devm_pwmchip_add(dev, &rpipwm->chip);
}

static const struct of_device_id raspberrypi_pwm_of_match[] = {
	{ .compatible = "raspberrypi,firmware-poe-pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, raspberrypi_pwm_of_match);

static struct platform_driver raspberrypi_pwm_driver = {
	.driver = {
		.name = "raspberrypi-poe-pwm",
		.of_match_table = raspberrypi_pwm_of_match,
	},
	.probe = raspberrypi_pwm_probe,
};
module_platform_driver(raspberrypi_pwm_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_DESCRIPTION("Raspberry Pi Firmware Based PWM Bus Driver");
MODULE_LICENSE("GPL v2");
