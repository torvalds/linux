// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021-2022 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/hte.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

/*
 * This sample HTE test driver demonstrates HTE API usage by enabling
 * hardware timestamp on gpio_in and specified LIC IRQ lines.
 *
 * Note: gpio_out and gpio_in need to be shorted externally in order for this
 * test driver to work for the GPIO monitoring. The test driver has been
 * tested on Jetson AGX Xavier platform by shorting pin 32 and 16 on 40 pin
 * header.
 *
 * Device tree snippet to activate this driver:
 *	tegra_hte_test {
 *		compatible = "nvidia,tegra194-hte-test";
 *		in-gpio = <&gpio_aon TEGRA194_AON_GPIO(BB, 1)>;
 *		out-gpio = <&gpio_aon TEGRA194_AON_GPIO(BB, 0)>;
 *		timestamps = <&tegra_hte_aon TEGRA194_AON_GPIO(BB, 1)>,
 *			     <&tegra_hte_lic 0x19>;
 *		timestamp-names = "hte-gpio", "hte-i2c-irq";
 *		status = "okay";
 *	};
 *
 * How to run test driver:
 * - Load test driver.
 * - For the GPIO, at regular interval gpio_out pin toggles triggering
 *   HTE for rising edge on gpio_in pin.
 *
 * - For the LIC IRQ line, it uses 0x19 interrupt which is i2c controller 1.
 * - Run i2cdetect -y 1 1>/dev/null, this command will generate i2c bus
 *   transactions which creates timestamp data.
 * - It prints below message for both the lines.
 *   HW timestamp(<line id>:<ts seq number>): <timestamp>, edge: <edge>.
 * - Unloading the driver disables and deallocate the HTE.
 */

static struct tegra_hte_test {
	int gpio_in_irq;
	struct device *pdev;
	struct gpio_desc *gpio_in;
	struct gpio_desc *gpio_out;
	struct hte_ts_desc *desc;
	struct timer_list timer;
	struct kobject *kobj;
} hte;

static enum hte_return process_hw_ts(struct hte_ts_data *ts, void *p)
{
	char *edge;
	struct hte_ts_desc *desc = p;

	if (!ts || !p)
		return HTE_CB_HANDLED;

	if (ts->raw_level < 0)
		edge = "Unknown";

	pr_info("HW timestamp(%u: %llu): %llu, edge: %s\n",
		desc->attr.line_id, ts->seq, ts->tsc,
		(ts->raw_level >= 0) ? ((ts->raw_level == 0) ?
					"falling" : "rising") : edge);

	return HTE_CB_HANDLED;
}

static void gpio_timer_cb(struct timer_list *t)
{
	(void)t;

	gpiod_set_value(hte.gpio_out, !gpiod_get_value(hte.gpio_out));
	mod_timer(&hte.timer, jiffies + msecs_to_jiffies(8000));
}

static irqreturn_t tegra_hte_test_gpio_isr(int irq, void *data)
{
	(void)irq;
	(void)data;

	return IRQ_HANDLED;
}

static const struct of_device_id tegra_hte_test_of_match[] = {
	{ .compatible = "nvidia,tegra194-hte-test"},
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_hte_test_of_match);

static int tegra_hte_test_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, cnt;

	dev_set_drvdata(&pdev->dev, &hte);
	hte.pdev = &pdev->dev;

	hte.gpio_out = gpiod_get(&pdev->dev, "out", 0);
	if (IS_ERR(hte.gpio_out)) {
		dev_err(&pdev->dev, "failed to get gpio out\n");
		ret = -EINVAL;
		goto out;
	}

	hte.gpio_in = gpiod_get(&pdev->dev, "in", 0);
	if (IS_ERR(hte.gpio_in)) {
		dev_err(&pdev->dev, "failed to get gpio in\n");
		ret = -EINVAL;
		goto free_gpio_out;
	}

	ret = gpiod_direction_output(hte.gpio_out, 0);
	if (ret) {
		dev_err(&pdev->dev, "failed to set output\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	ret = gpiod_direction_input(hte.gpio_in);
	if (ret) {
		dev_err(&pdev->dev, "failed to set input\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	ret = gpiod_to_irq(hte.gpio_in);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to map GPIO to IRQ: %d\n", ret);
		ret = -ENXIO;
		goto free_gpio_in;
	}

	hte.gpio_in_irq = ret;
	ret = request_irq(ret, tegra_hte_test_gpio_isr,
			  IRQF_TRIGGER_RISING,
			  "tegra_hte_gpio_test_isr", &hte);
	if (ret) {
		dev_err(&pdev->dev, "failed to acquire IRQ\n");
		ret = -ENXIO;
		goto free_irq;
	}

	cnt = of_hte_req_count(hte.pdev);
	if (cnt < 0) {
		ret = cnt;
		goto free_irq;
	}

	dev_info(&pdev->dev, "Total requested lines:%d\n", cnt);

	hte.desc = devm_kzalloc(hte.pdev, sizeof(*hte.desc) * cnt, GFP_KERNEL);
	if (!hte.desc) {
		ret = -ENOMEM;
		goto free_irq;
	}

	for (i = 0; i < cnt; i++) {
		if (i == 0)
			/*
			 * GPIO hte init, line_id and name will be parsed from
			 * the device tree node. The edge_flag is implicitly
			 * set by request_irq call. Only line_data is needed to be
			 * set.
			 */
			hte_init_line_attr(&hte.desc[i], 0, 0, NULL,
					   hte.gpio_in);
		else
			/*
			 * same comment as above except that IRQ does not need
			 * line data.
			 */
			hte_init_line_attr(&hte.desc[i], 0, 0, NULL, NULL);

		ret = hte_ts_get(hte.pdev, &hte.desc[i], i);
		if (ret)
			goto ts_put;

		ret = devm_hte_request_ts_ns(hte.pdev, &hte.desc[i],
					     process_hw_ts, NULL,
					     &hte.desc[i]);
		if (ret) /* no need to ts_put, request API takes care */
			goto free_irq;
	}

	timer_setup(&hte.timer, gpio_timer_cb, 0);
	mod_timer(&hte.timer, jiffies + msecs_to_jiffies(5000));

	return 0;

ts_put:
	cnt = i;
	for (i = 0; i < cnt; i++)
		hte_ts_put(&hte.desc[i]);
free_irq:
	free_irq(hte.gpio_in_irq, &hte);
free_gpio_in:
	gpiod_put(hte.gpio_in);
free_gpio_out:
	gpiod_put(hte.gpio_out);
out:

	return ret;
}

static void tegra_hte_test_remove(struct platform_device *pdev)
{
	(void)pdev;

	free_irq(hte.gpio_in_irq, &hte);
	gpiod_put(hte.gpio_in);
	gpiod_put(hte.gpio_out);
	del_timer_sync(&hte.timer);
}

static struct platform_driver tegra_hte_test_driver = {
	.probe = tegra_hte_test_probe,
	.remove_new = tegra_hte_test_remove,
	.driver = {
		.name = "tegra_hte_test",
		.of_match_table = tegra_hte_test_of_match,
	},
};
module_platform_driver(tegra_hte_test_driver);

MODULE_AUTHOR("Dipen Patel <dipenp@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra HTE (Hardware Timestamping Engine) test driver");
MODULE_LICENSE("GPL");
