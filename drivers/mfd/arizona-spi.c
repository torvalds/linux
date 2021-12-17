// SPDX-License-Identifier: GPL-2.0-only
/*
 * arizona-spi.c  --  Arizona SPI bus interface
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <uapi/linux/input-event-codes.h>

#include <linux/mfd/arizona/core.h>

#include "arizona.h"

#ifdef CONFIG_ACPI
static const struct acpi_gpio_params reset_gpios = { 1, 0, false };
static const struct acpi_gpio_params ldoena_gpios = { 2, 0, false };

static const struct acpi_gpio_mapping arizona_acpi_gpios[] = {
	{ "reset-gpios", &reset_gpios, 1, },
	{ "wlf,ldoena-gpios", &ldoena_gpios, 1 },
	{ }
};

/*
 * The ACPI resources for the device only describe external GPIO-s. They do
 * not provide mappings for the GPIO-s coming from the Arizona codec itself.
 */
static const struct gpiod_lookup arizona_soc_gpios[] = {
	{ "arizona", 2, "wlf,spkvdd-ena", 0, GPIO_ACTIVE_HIGH },
	{ "arizona", 4, "wlf,micd-pol", 0, GPIO_ACTIVE_LOW },
};

/*
 * The AOSP 3.5 mm Headset: Accessory Specification gives the following values:
 * Function A Play/Pause:           0 ohm
 * Function D Voice assistant:    135 ohm
 * Function B Volume Up           240 ohm
 * Function C Volume Down         470 ohm
 * Minimum Mic DC resistance     1000 ohm
 * Minimum Ear speaker impedance   16 ohm
 * Note the first max value below must be less then the min. speaker impedance,
 * to allow CTIA/OMTP detection to work. The other max values are the closest
 * value from extcon-arizona.c:arizona_micd_levels halfway 2 button resistances.
 */
static const struct arizona_micd_range arizona_micd_aosp_ranges[] = {
	{ .max =  11, .key = KEY_PLAYPAUSE },
	{ .max = 186, .key = KEY_VOICECOMMAND },
	{ .max = 348, .key = KEY_VOLUMEUP },
	{ .max = 752, .key = KEY_VOLUMEDOWN },
};

static void arizona_spi_acpi_remove_lookup(void *lookup)
{
	gpiod_remove_lookup_table(lookup);
}

static int arizona_spi_acpi_probe(struct arizona *arizona)
{
	struct gpiod_lookup_table *lookup;
	acpi_status status;
	int ret;

	/* Add mappings for the 2 ACPI declared GPIOs used for reset and ldo-ena */
	devm_acpi_dev_add_driver_gpios(arizona->dev, arizona_acpi_gpios);

	/* Add lookups for the SoCs own GPIOs used for micdet-polarity and spkVDD-enable */
	lookup = devm_kzalloc(arizona->dev,
			      struct_size(lookup, table, ARRAY_SIZE(arizona_soc_gpios) + 1),
			      GFP_KERNEL);
	if (!lookup)
		return -ENOMEM;

	lookup->dev_id = dev_name(arizona->dev);
	memcpy(lookup->table, arizona_soc_gpios, sizeof(arizona_soc_gpios));

	gpiod_add_lookup_table(lookup);
	ret = devm_add_action_or_reset(arizona->dev, arizona_spi_acpi_remove_lookup, lookup);
	if (ret)
		return ret;

	/* Enable 32KHz clock from SoC to codec for jack-detect */
	status = acpi_evaluate_object(ACPI_HANDLE(arizona->dev), "CLKE", NULL, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(arizona->dev, "Failed to enable 32KHz clk ACPI error %d\n", status);

	/*
	 * Some DSDTs wrongly declare the IRQ trigger-type as IRQF_TRIGGER_FALLING
	 * The IRQ line will stay low when a new IRQ event happens between reading
	 * the IRQ status flags and acknowledging them. When the IRQ line stays
	 * low like this the IRQ will never trigger again when its type is set
	 * to IRQF_TRIGGER_FALLING. Correct the IRQ trigger-type to fix this.
	 *
	 * Note theoretically it is possible that some boards are not capable
	 * of handling active low level interrupts. In that case setting the
	 * flag to IRQF_TRIGGER_FALLING would not be a bug (and we would need
	 * to work around this) but so far all known usages of IRQF_TRIGGER_FALLING
	 * are a bug in the board's DSDT.
	 */
	arizona->pdata.irq_flags = IRQF_TRIGGER_LOW;

	/* Wait 200 ms after jack insertion */
	arizona->pdata.micd_detect_debounce = 200;

	/* Use standard AOSP values for headset-button mappings */
	arizona->pdata.micd_ranges = arizona_micd_aosp_ranges;
	arizona->pdata.num_micd_ranges = ARRAY_SIZE(arizona_micd_aosp_ranges);

	return 0;
}

static const struct acpi_device_id arizona_acpi_match[] = {
	{
		.id = "WM510204",
		.driver_data = WM5102,
	},
	{
		.id = "WM510205",
		.driver_data = WM5102,
	},
	{ }
};
MODULE_DEVICE_TABLE(acpi, arizona_acpi_match);
#else
static int arizona_spi_acpi_probe(struct arizona *arizona)
{
	return -ENODEV;
}
#endif

static int arizona_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	const void *match_data;
	struct arizona *arizona;
	const struct regmap_config *regmap_config = NULL;
	unsigned long type = 0;
	int ret;

	match_data = device_get_match_data(&spi->dev);
	if (match_data)
		type = (unsigned long)match_data;
	else if (id)
		type = id->driver_data;

	switch (type) {
	case WM5102:
		if (IS_ENABLED(CONFIG_MFD_WM5102))
			regmap_config = &wm5102_spi_regmap;
		break;
	case WM5110:
	case WM8280:
		if (IS_ENABLED(CONFIG_MFD_WM5110))
			regmap_config = &wm5110_spi_regmap;
		break;
	case WM1831:
	case CS47L24:
		if (IS_ENABLED(CONFIG_MFD_CS47L24))
			regmap_config = &cs47l24_spi_regmap;
		break;
	default:
		dev_err(&spi->dev, "Unknown device type %ld\n", type);
		return -EINVAL;
	}

	if (!regmap_config) {
		dev_err(&spi->dev,
			"No kernel support for device type %ld\n", type);
		return -EINVAL;
	}

	arizona = devm_kzalloc(&spi->dev, sizeof(*arizona), GFP_KERNEL);
	if (arizona == NULL)
		return -ENOMEM;

	arizona->regmap = devm_regmap_init_spi(spi, regmap_config);
	if (IS_ERR(arizona->regmap)) {
		ret = PTR_ERR(arizona->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	arizona->type = type;
	arizona->dev = &spi->dev;
	arizona->irq = spi->irq;

	if (has_acpi_companion(&spi->dev)) {
		ret = arizona_spi_acpi_probe(arizona);
		if (ret)
			return ret;
	}

	return arizona_dev_init(arizona);
}

static int arizona_spi_remove(struct spi_device *spi)
{
	struct arizona *arizona = spi_get_drvdata(spi);

	arizona_dev_exit(arizona);

	return 0;
}

static const struct spi_device_id arizona_spi_ids[] = {
	{ "wm5102", WM5102 },
	{ "wm5110", WM5110 },
	{ "wm8280", WM8280 },
	{ "wm1831", WM1831 },
	{ "cs47l24", CS47L24 },
	{ },
};
MODULE_DEVICE_TABLE(spi, arizona_spi_ids);

#ifdef CONFIG_OF
const struct of_device_id arizona_spi_of_match[] = {
	{ .compatible = "wlf,wm5102", .data = (void *)WM5102 },
	{ .compatible = "wlf,wm5110", .data = (void *)WM5110 },
	{ .compatible = "wlf,wm8280", .data = (void *)WM8280 },
	{ .compatible = "wlf,wm1831", .data = (void *)WM1831 },
	{ .compatible = "cirrus,cs47l24", .data = (void *)CS47L24 },
	{},
};
#endif

static struct spi_driver arizona_spi_driver = {
	.driver = {
		.name	= "arizona",
		.pm	= &arizona_pm_ops,
		.of_match_table	= of_match_ptr(arizona_spi_of_match),
		.acpi_match_table = ACPI_PTR(arizona_acpi_match),
	},
	.probe		= arizona_spi_probe,
	.remove		= arizona_spi_remove,
	.id_table	= arizona_spi_ids,
};

module_spi_driver(arizona_spi_driver);

MODULE_SOFTDEP("pre: arizona_ldo1");
MODULE_DESCRIPTION("Arizona SPI bus interface");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
