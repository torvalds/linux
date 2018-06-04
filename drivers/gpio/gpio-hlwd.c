// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2008-2009 The GameCube Linux Team
// Copyright (C) 2008,2009 Albert Herranz
// Copyright (C) 2017-2018 Jonathan Neuschäfer
//
// Nintendo Wii (Hollywood) GPIO driver

#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

/*
 * Register names and offsets courtesy of WiiBrew:
 * https://wiibrew.org/wiki/Hardware/Hollywood_GPIOs
 *
 * Note that for most registers, there are two versions:
 * - HW_GPIOB_* Is always accessible by the Broadway PowerPC core, but does
 *   always give access to all GPIO lines
 * - HW_GPIO_* Is only accessible by the Broadway PowerPC code if the memory
 *   firewall (AHBPROT) in the Hollywood chipset has been configured to allow
 *   such access.
 *
 * The ownership of each GPIO line can be configured in the HW_GPIO_OWNER
 * register: A one bit configures the line for access via the HW_GPIOB_*
 * registers, a zero bit indicates access via HW_GPIO_*. This driver uses
 * HW_GPIOB_*.
 */
#define HW_GPIOB_OUT		0x00
#define HW_GPIOB_DIR		0x04
#define HW_GPIOB_IN		0x08
#define HW_GPIOB_INTLVL		0x0c
#define HW_GPIOB_INTFLAG	0x10
#define HW_GPIOB_INTMASK	0x14
#define HW_GPIOB_INMIR		0x18
#define HW_GPIO_ENABLE		0x1c
#define HW_GPIO_OUT		0x20
#define HW_GPIO_DIR		0x24
#define HW_GPIO_IN		0x28
#define HW_GPIO_INTLVL		0x2c
#define HW_GPIO_INTFLAG		0x30
#define HW_GPIO_INTMASK		0x34
#define HW_GPIO_INMIR		0x38
#define HW_GPIO_OWNER		0x3c

struct hlwd_gpio {
	struct gpio_chip gpioc;
	void __iomem *regs;
};

static int hlwd_gpio_probe(struct platform_device *pdev)
{
	struct hlwd_gpio *hlwd;
	struct resource *regs_resource;
	u32 ngpios;
	int res;

	hlwd = devm_kzalloc(&pdev->dev, sizeof(*hlwd), GFP_KERNEL);
	if (!hlwd)
		return -ENOMEM;

	regs_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hlwd->regs = devm_ioremap_resource(&pdev->dev, regs_resource);
	if (IS_ERR(hlwd->regs))
		return PTR_ERR(hlwd->regs);

	/*
	 * Claim all GPIOs using the OWNER register. This will not work on
	 * systems where the AHBPROT memory firewall hasn't been configured to
	 * permit PPC access to HW_GPIO_*.
	 *
	 * Note that this has to happen before bgpio_init reads the
	 * HW_GPIOB_OUT and HW_GPIOB_DIR, because otherwise it reads the wrong
	 * values.
	 */
	iowrite32be(0xffffffff, hlwd->regs + HW_GPIO_OWNER);

	res = bgpio_init(&hlwd->gpioc, &pdev->dev, 4,
			hlwd->regs + HW_GPIOB_IN, hlwd->regs + HW_GPIOB_OUT,
			NULL, hlwd->regs + HW_GPIOB_DIR, NULL,
			BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (res < 0) {
		dev_warn(&pdev->dev, "bgpio_init failed: %d\n", res);
		return res;
	}

	res = of_property_read_u32(pdev->dev.of_node, "ngpios", &ngpios);
	if (res)
		ngpios = 32;
	hlwd->gpioc.ngpio = ngpios;

	return devm_gpiochip_add_data(&pdev->dev, &hlwd->gpioc, hlwd);
}

static const struct of_device_id hlwd_gpio_match[] = {
	{ .compatible = "nintendo,hollywood-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, hlwd_gpio_match);

static struct platform_driver hlwd_gpio_driver = {
	.driver	= {
		.name		= "gpio-hlwd",
		.of_match_table	= hlwd_gpio_match,
	},
	.probe	= hlwd_gpio_probe,
};
module_platform_driver(hlwd_gpio_driver);

MODULE_AUTHOR("Jonathan Neuschäfer <j.neuschaefer@gmx.net>");
MODULE_DESCRIPTION("Nintendo Wii GPIO driver");
MODULE_LICENSE("GPL");
