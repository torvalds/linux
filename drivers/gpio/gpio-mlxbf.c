// SPDX-License-Identifier: GPL-2.0

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/resource.h>
#include <linux/types.h>

/* Number of pins on BlueField */
#define MLXBF_GPIO_NR 54

/* Pad Electrical Controls. */
#define MLXBF_GPIO_PAD_CONTROL_FIRST_WORD 0x0700
#define MLXBF_GPIO_PAD_CONTROL_1_FIRST_WORD 0x0708
#define MLXBF_GPIO_PAD_CONTROL_2_FIRST_WORD 0x0710
#define MLXBF_GPIO_PAD_CONTROL_3_FIRST_WORD 0x0718

#define MLXBF_GPIO_PIN_DIR_I 0x1040
#define MLXBF_GPIO_PIN_DIR_O 0x1048
#define MLXBF_GPIO_PIN_STATE 0x1000
#define MLXBF_GPIO_SCRATCHPAD 0x20

#ifdef CONFIG_PM
struct mlxbf_gpio_context_save_regs {
	u64 scratchpad;
	u64 pad_control[MLXBF_GPIO_NR];
	u64 pin_dir_i;
	u64 pin_dir_o;
};
#endif

/* Device state structure. */
struct mlxbf_gpio_state {
	struct gpio_chip gc;

	/* Memory Address */
	void __iomem *base;

#ifdef CONFIG_PM
	struct mlxbf_gpio_context_save_regs csave_regs;
#endif
};

static int mlxbf_gpio_probe(struct platform_device *pdev)
{
	struct mlxbf_gpio_state *gs;
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	int ret;

	gs = devm_kzalloc(&pdev->dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	gs->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gs->base))
		return PTR_ERR(gs->base);

	gc = &gs->gc;
	ret = bgpio_init(gc, dev, 8,
			 gs->base + MLXBF_GPIO_PIN_STATE,
			 NULL,
			 NULL,
			 gs->base + MLXBF_GPIO_PIN_DIR_O,
			 gs->base + MLXBF_GPIO_PIN_DIR_I,
			 0);
	if (ret)
		return -ENODEV;

	gc->owner = THIS_MODULE;
	gc->ngpio = MLXBF_GPIO_NR;

	ret = devm_gpiochip_add_data(dev, &gs->gc, gs);
	if (ret) {
		dev_err(&pdev->dev, "Failed adding memory mapped gpiochip\n");
		return ret;
	}

	platform_set_drvdata(pdev, gs);
	dev_info(&pdev->dev, "registered Mellanox BlueField GPIO");
	return 0;
}

#ifdef CONFIG_PM
static int mlxbf_gpio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mlxbf_gpio_state *gs = platform_get_drvdata(pdev);

	gs->csave_regs.scratchpad = readq(gs->base + MLXBF_GPIO_SCRATCHPAD);
	gs->csave_regs.pad_control[0] =
		readq(gs->base + MLXBF_GPIO_PAD_CONTROL_FIRST_WORD);
	gs->csave_regs.pad_control[1] =
		readq(gs->base + MLXBF_GPIO_PAD_CONTROL_1_FIRST_WORD);
	gs->csave_regs.pad_control[2] =
		readq(gs->base + MLXBF_GPIO_PAD_CONTROL_2_FIRST_WORD);
	gs->csave_regs.pad_control[3] =
		readq(gs->base + MLXBF_GPIO_PAD_CONTROL_3_FIRST_WORD);
	gs->csave_regs.pin_dir_i = readq(gs->base + MLXBF_GPIO_PIN_DIR_I);
	gs->csave_regs.pin_dir_o = readq(gs->base + MLXBF_GPIO_PIN_DIR_O);

	return 0;
}

static int mlxbf_gpio_resume(struct platform_device *pdev)
{
	struct mlxbf_gpio_state *gs = platform_get_drvdata(pdev);

	writeq(gs->csave_regs.scratchpad, gs->base + MLXBF_GPIO_SCRATCHPAD);
	writeq(gs->csave_regs.pad_control[0],
	       gs->base + MLXBF_GPIO_PAD_CONTROL_FIRST_WORD);
	writeq(gs->csave_regs.pad_control[1],
	       gs->base + MLXBF_GPIO_PAD_CONTROL_1_FIRST_WORD);
	writeq(gs->csave_regs.pad_control[2],
	       gs->base + MLXBF_GPIO_PAD_CONTROL_2_FIRST_WORD);
	writeq(gs->csave_regs.pad_control[3],
	       gs->base + MLXBF_GPIO_PAD_CONTROL_3_FIRST_WORD);
	writeq(gs->csave_regs.pin_dir_i, gs->base + MLXBF_GPIO_PIN_DIR_I);
	writeq(gs->csave_regs.pin_dir_o, gs->base + MLXBF_GPIO_PIN_DIR_O);

	return 0;
}
#endif

static const struct acpi_device_id __maybe_unused mlxbf_gpio_acpi_match[] = {
	{ "MLNXBF02", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, mlxbf_gpio_acpi_match);

static struct platform_driver mlxbf_gpio_driver = {
	.driver = {
		.name = "mlxbf_gpio",
		.acpi_match_table = ACPI_PTR(mlxbf_gpio_acpi_match),
	},
	.probe    = mlxbf_gpio_probe,
#ifdef CONFIG_PM
	.suspend  = mlxbf_gpio_suspend,
	.resume   = mlxbf_gpio_resume,
#endif
};

module_platform_driver(mlxbf_gpio_driver);

MODULE_DESCRIPTION("Mellanox BlueField GPIO Driver");
MODULE_AUTHOR("Mellanox Technologies");
MODULE_LICENSE("GPL");
