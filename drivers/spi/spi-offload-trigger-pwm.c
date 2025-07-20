// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 *
 * Generic PWM trigger for SPI offload.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/offload/types.h>
#include <linux/time.h>
#include <linux/types.h>

struct spi_offload_trigger_pwm_state {
	struct device *dev;
	struct pwm_device *pwm;
};

static bool spi_offload_trigger_pwm_match(struct spi_offload_trigger *trigger,
					  enum spi_offload_trigger_type type,
					  u64 *args, u32 nargs)
{
	if (nargs)
		return false;

	return type == SPI_OFFLOAD_TRIGGER_PERIODIC;
}

static int spi_offload_trigger_pwm_validate(struct spi_offload_trigger *trigger,
					    struct spi_offload_trigger_config *config)
{
	struct spi_offload_trigger_pwm_state *st = spi_offload_trigger_get_priv(trigger);
	struct spi_offload_trigger_periodic *periodic = &config->periodic;
	struct pwm_waveform wf = { };
	int ret;

	if (config->type != SPI_OFFLOAD_TRIGGER_PERIODIC)
		return -EINVAL;

	if (!periodic->frequency_hz)
		return -EINVAL;

	wf.period_length_ns = DIV_ROUND_UP_ULL(NSEC_PER_SEC, periodic->frequency_hz);
	/* REVISIT: 50% duty-cycle for now - may add config parameter later */
	wf.duty_length_ns = wf.period_length_ns / 2;

	ret = pwm_round_waveform_might_sleep(st->pwm, &wf);
	if (ret < 0)
		return ret;

	periodic->frequency_hz = DIV_ROUND_UP_ULL(NSEC_PER_SEC, wf.period_length_ns);

	return 0;
}

static int spi_offload_trigger_pwm_enable(struct spi_offload_trigger *trigger,
					  struct spi_offload_trigger_config *config)
{
	struct spi_offload_trigger_pwm_state *st = spi_offload_trigger_get_priv(trigger);
	struct spi_offload_trigger_periodic *periodic = &config->periodic;
	struct pwm_waveform wf = { };

	if (config->type != SPI_OFFLOAD_TRIGGER_PERIODIC)
		return -EINVAL;

	if (!periodic->frequency_hz)
		return -EINVAL;

	wf.period_length_ns = DIV_ROUND_UP_ULL(NSEC_PER_SEC, periodic->frequency_hz);
	/* REVISIT: 50% duty-cycle for now - may add config parameter later */
	wf.duty_length_ns = wf.period_length_ns / 2;

	return pwm_set_waveform_might_sleep(st->pwm, &wf, false);
}

static void spi_offload_trigger_pwm_disable(struct spi_offload_trigger *trigger)
{
	struct spi_offload_trigger_pwm_state *st = spi_offload_trigger_get_priv(trigger);
	struct pwm_waveform wf;
	int ret;

	ret = pwm_get_waveform_might_sleep(st->pwm, &wf);
	if (ret < 0) {
		dev_err(st->dev, "failed to get waveform: %d\n", ret);
		return;
	}

	wf.duty_length_ns = 0;

	ret = pwm_set_waveform_might_sleep(st->pwm, &wf, false);
	if (ret < 0)
		dev_err(st->dev, "failed to disable PWM: %d\n", ret);
}

static const struct spi_offload_trigger_ops spi_offload_trigger_pwm_ops = {
	.match = spi_offload_trigger_pwm_match,
	.validate = spi_offload_trigger_pwm_validate,
	.enable = spi_offload_trigger_pwm_enable,
	.disable = spi_offload_trigger_pwm_disable,
};

static void spi_offload_trigger_pwm_release(void *data)
{
	pwm_disable(data);
}

static int spi_offload_trigger_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_offload_trigger_info info = {
		.fwnode = dev_fwnode(dev),
		.ops = &spi_offload_trigger_pwm_ops,
	};
	struct spi_offload_trigger_pwm_state *st;
	struct pwm_state state;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	info.priv = st;
	st->dev = dev;

	st->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(st->pwm))
		return dev_err_probe(dev, PTR_ERR(st->pwm), "failed to get PWM\n");

	/* init with duty_cycle = 0, output enabled to ensure trigger off */
	pwm_init_state(st->pwm, &state);
	state.enabled = true;

	ret = pwm_apply_might_sleep(st->pwm, &state);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to apply PWM state\n");

	ret = devm_add_action_or_reset(dev, spi_offload_trigger_pwm_release, st->pwm);
	if (ret)
		return ret;

	return devm_spi_offload_trigger_register(dev, &info);
}

static const struct of_device_id spi_offload_trigger_pwm_of_match_table[] = {
	{ .compatible = "pwm-trigger" },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_offload_trigger_pwm_of_match_table);

static struct platform_driver spi_offload_trigger_pwm_driver = {
	.driver = {
		.name = "pwm-trigger",
		.of_match_table = spi_offload_trigger_pwm_of_match_table,
	},
	.probe = spi_offload_trigger_pwm_probe,
};
module_platform_driver(spi_offload_trigger_pwm_driver);

MODULE_AUTHOR("David Lechner <dlechner@baylibre.com>");
MODULE_DESCRIPTION("Generic PWM trigger");
MODULE_LICENSE("GPL");
