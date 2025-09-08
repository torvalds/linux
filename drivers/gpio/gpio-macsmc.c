// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SMC GPIO driver
 * Copyright The Asahi Linux Contributors
 *
 * This driver implements basic SMC PMU GPIO support that can read inputs
 * and write outputs. Mode changes and IRQ config are not yet implemented.
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/core.h>
#include <linux/mfd/macsmc.h>

#define MAX_GPIO 64

/*
 * Commands 0-6 are, presumably, the intended API.
 * Command 0xff lets you get/set the pin configuration in detail directly,
 * but the bit meanings seem not to be stable between devices/PMU hardware
 * versions.
 *
 * We're going to try to make do with the low commands for now.
 * We don't implement pin mode changes at this time.
 */

#define CMD_ACTION	(0 << 24)
#define CMD_OUTPUT	(1 << 24)
#define CMD_INPUT	(2 << 24)
#define CMD_PINMODE	(3 << 24)
#define CMD_IRQ_ENABLE	(4 << 24)
#define CMD_IRQ_ACK	(5 << 24)
#define CMD_IRQ_MODE	(6 << 24)
#define CMD_CONFIG	(0xff << 24)

#define MODE_INPUT	0
#define MODE_OUTPUT	1
#define MODE_VALUE_0	0
#define MODE_VALUE_1	2

#define IRQ_MODE_HIGH		0
#define IRQ_MODE_LOW		1
#define IRQ_MODE_RISING		2
#define IRQ_MODE_FALLING	3
#define IRQ_MODE_BOTH		4

#define CONFIG_MASK	GENMASK(23, 16)
#define CONFIG_VAL	GENMASK(7, 0)

#define CONFIG_OUTMODE	GENMASK(7, 6)
#define CONFIG_IRQMODE	GENMASK(5, 3)
#define CONFIG_PULLDOWN	BIT(2)
#define CONFIG_PULLUP	BIT(1)
#define CONFIG_OUTVAL	BIT(0)

/*
 * Output modes seem to differ depending on the PMU in use... ?
 * j274 / M1 (Sera PMU):
 *   0 = input
 *   1 = output
 *   2 = open drain
 *   3 = disable
 * j314 / M1Pro (Maverick PMU):
 *   0 = input
 *   1 = open drain
 *   2 = output
 *   3 = ?
 */

struct macsmc_gpio {
	struct device *dev;
	struct apple_smc *smc;
	struct gpio_chip gc;

	int first_index;
};

static int macsmc_gpio_nr(smc_key key)
{
	int low = hex_to_bin(key & 0xff);
	int high = hex_to_bin((key >> 8) & 0xff);

	if (low < 0 || high < 0)
		return -1;

	return low | (high << 4);
}

static int macsmc_gpio_key(unsigned int offset)
{
	return _SMC_KEY("gP\0\0") | hex_asc_hi(offset) << 8 | hex_asc_lo(offset);
}

static int macsmc_gpio_find_first_gpio_index(struct macsmc_gpio *smcgp)
{
	struct apple_smc *smc = smcgp->smc;
	smc_key key = macsmc_gpio_key(0);
	smc_key first_key, last_key;
	int start, count, ret;

	/* Return early if the key is out of bounds */
	ret = apple_smc_get_key_by_index(smc, 0, &first_key);
	if (ret)
		return ret;
	if (key <= first_key)
		return -ENODEV;

	ret = apple_smc_get_key_by_index(smc, smc->key_count - 1, &last_key);
	if (ret)
		return ret;
	if (key > last_key)
		return -ENODEV;

	/* Binary search to find index of first SMC key bigger or equal to key */
	start = 0;
	count = smc->key_count;
	while (count > 1) {
		smc_key pkey;
		int pivot = start + ((count - 1) >> 1);

		ret = apple_smc_get_key_by_index(smc, pivot, &pkey);
		if (ret < 0)
			return ret;

		if (pkey == key)
			return pivot;

		pivot++;

		if (pkey < key) {
			count -= pivot - start;
			start = pivot;
		} else {
			count = pivot - start;
		}
	}

	return start;
}

static int macsmc_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct macsmc_gpio *smcgp = gpiochip_get_data(gc);
	smc_key key = macsmc_gpio_key(offset);
	u32 val;
	int ret;

	/* First try reading the explicit pin mode register */
	ret = apple_smc_rw_u32(smcgp->smc, key, CMD_PINMODE, &val);
	if (!ret)
		return (val & MODE_OUTPUT) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;

	/*
	 * Less common IRQ configs cause CMD_PINMODE to fail, and so does open drain mode.
	 * Fall back to reading IRQ mode, which will only succeed for inputs.
	 */
	ret = apple_smc_rw_u32(smcgp->smc, key, CMD_IRQ_MODE, &val);
	return ret ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int macsmc_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct macsmc_gpio *smcgp = gpiochip_get_data(gc);
	smc_key key = macsmc_gpio_key(offset);
	u32 cmd, val;
	int ret;

	ret = macsmc_gpio_get_direction(gc, offset);
	if (ret < 0)
		return ret;

	if (ret == GPIO_LINE_DIRECTION_OUT)
		cmd = CMD_OUTPUT;
	else
		cmd = CMD_INPUT;

	ret = apple_smc_rw_u32(smcgp->smc, key, cmd, &val);
	if (ret < 0)
		return ret;

	return val ? 1 : 0;
}

static int macsmc_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct macsmc_gpio *smcgp = gpiochip_get_data(gc);
	smc_key key = macsmc_gpio_key(offset);
	int ret;

	value |= CMD_OUTPUT;
	ret = apple_smc_write_u32(smcgp->smc, key, CMD_OUTPUT | value);
	if (ret < 0)
		dev_err(smcgp->dev, "GPIO set failed %p4ch = 0x%x\n",
			&key, value);

	return ret;
}

static int macsmc_gpio_init_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask, unsigned int ngpios)
{
	struct macsmc_gpio *smcgp = gpiochip_get_data(gc);
	int count;
	int i;

	count = min(smcgp->smc->key_count, MAX_GPIO);

	bitmap_zero(valid_mask, ngpios);

	for (i = 0; i < count; i++) {
		int ret, gpio_nr;
		smc_key key;

		ret = apple_smc_get_key_by_index(smcgp->smc, smcgp->first_index + i, &key);
		if (ret < 0)
			return ret;

		if (key > SMC_KEY(gPff))
			break;

		gpio_nr = macsmc_gpio_nr(key);
		if (gpio_nr < 0 || gpio_nr > MAX_GPIO) {
			dev_err(smcgp->dev, "Bad GPIO key %p4ch\n", &key);
			continue;
		}

		set_bit(gpio_nr, valid_mask);
	}

	return 0;
}

static int macsmc_gpio_probe(struct platform_device *pdev)
{
	struct macsmc_gpio *smcgp;
	struct apple_smc *smc = dev_get_drvdata(pdev->dev.parent);
	smc_key key;
	int ret;

	smcgp = devm_kzalloc(&pdev->dev, sizeof(*smcgp), GFP_KERNEL);
	if (!smcgp)
		return -ENOMEM;

	smcgp->dev = &pdev->dev;
	smcgp->smc = smc;

	smcgp->first_index = macsmc_gpio_find_first_gpio_index(smcgp);
	if (smcgp->first_index < 0)
		return smcgp->first_index;

	ret = apple_smc_get_key_by_index(smc, smcgp->first_index, &key);
	if (ret < 0)
		return ret;

	if (key > macsmc_gpio_key(MAX_GPIO - 1))
		return -ENODEV;

	dev_info(smcgp->dev, "First GPIO key: %p4ch\n", &key);

	smcgp->gc.label = "macsmc-pmu-gpio";
	smcgp->gc.owner = THIS_MODULE;
	smcgp->gc.get = macsmc_gpio_get;
	smcgp->gc.set = macsmc_gpio_set;
	smcgp->gc.get_direction = macsmc_gpio_get_direction;
	smcgp->gc.init_valid_mask = macsmc_gpio_init_valid_mask;
	smcgp->gc.can_sleep = true;
	smcgp->gc.ngpio = MAX_GPIO;
	smcgp->gc.base = -1;
	smcgp->gc.parent = &pdev->dev;

	return devm_gpiochip_add_data(&pdev->dev, &smcgp->gc, smcgp);
}

static const struct of_device_id macsmc_gpio_of_table[] = {
	{ .compatible = "apple,smc-gpio", },
	{}
};
MODULE_DEVICE_TABLE(of, macsmc_gpio_of_table);

static struct platform_driver macsmc_gpio_driver = {
	.driver = {
		.name = "macsmc-gpio",
		.of_match_table = macsmc_gpio_of_table,
	},
	.probe = macsmc_gpio_probe,
};
module_platform_driver(macsmc_gpio_driver);

MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Apple SMC GPIO driver");
