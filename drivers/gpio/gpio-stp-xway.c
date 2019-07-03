// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/mutex.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <lantiq_soc.h>

/*
 * The Serial To Parallel (STP) is found on MIPS based Lantiq socs. It is a
 * peripheral controller used to drive external shift register cascades. At most
 * 3 groups of 8 bits can be driven. The hardware is able to allow the DSL modem
 * to drive the 2 LSBs of the cascade automatically.
 */

/* control register 0 */
#define XWAY_STP_CON0		0x00
/* control register 1 */
#define XWAY_STP_CON1		0x04
/* data register 0 */
#define XWAY_STP_CPU0		0x08
/* data register 1 */
#define XWAY_STP_CPU1		0x0C
/* access register */
#define XWAY_STP_AR		0x10

/* software or hardware update select bit */
#define XWAY_STP_CON_SWU	BIT(31)

/* automatic update rates */
#define XWAY_STP_2HZ		0
#define XWAY_STP_4HZ		BIT(23)
#define XWAY_STP_8HZ		BIT(24)
#define XWAY_STP_10HZ		(BIT(24) | BIT(23))
#define XWAY_STP_SPEED_MASK	(0xf << 23)

/* clock source for automatic update */
#define XWAY_STP_UPD_FPI	BIT(31)
#define XWAY_STP_UPD_MASK	(BIT(31) | BIT(30))

/* let the adsl core drive the 2 LSBs */
#define XWAY_STP_ADSL_SHIFT	24
#define XWAY_STP_ADSL_MASK	0x3

/* 2 groups of 3 bits can be driven by the phys */
#define XWAY_STP_PHY_MASK	0x7
#define XWAY_STP_PHY1_SHIFT	27
#define XWAY_STP_PHY2_SHIFT	15

/* STP has 3 groups of 8 bits */
#define XWAY_STP_GROUP0		BIT(0)
#define XWAY_STP_GROUP1		BIT(1)
#define XWAY_STP_GROUP2		BIT(2)
#define XWAY_STP_GROUP_MASK	(0x7)

/* Edge configuration bits */
#define XWAY_STP_FALLING	BIT(26)
#define XWAY_STP_EDGE_MASK	BIT(26)

#define xway_stp_r32(m, reg)		__raw_readl(m + reg)
#define xway_stp_w32(m, val, reg)	__raw_writel(val, m + reg)
#define xway_stp_w32_mask(m, clear, set, reg) \
		ltq_w32((ltq_r32(m + reg) & ~(clear)) | (set), \
		m + reg)

struct xway_stp {
	struct gpio_chip gc;
	void __iomem *virt;
	u32 edge;	/* rising or falling edge triggered shift register */
	u32 shadow;	/* shadow the shift registers state */
	u8 groups;	/* we can drive 1-3 groups of 8bit each */
	u8 dsl;		/* the 2 LSBs can be driven by the dsl core */
	u8 phy1;	/* 3 bits can be driven by phy1 */
	u8 phy2;	/* 3 bits can be driven by phy2 */
	u8 reserved;	/* mask out the hw driven bits in gpio_request */
};

/**
 * xway_stp_get() - gpio_chip->get - get gpios.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * Gets the shadow value.
 */
static int xway_stp_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct xway_stp *chip = gpiochip_get_data(gc);

	return (xway_stp_r32(chip->virt, XWAY_STP_CPU0) & BIT(gpio));
}

/**
 * xway_stp_set() - gpio_chip->set - set gpios.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * Set the shadow value and call ltq_ebu_apply.
 */
static void xway_stp_set(struct gpio_chip *gc, unsigned gpio, int val)
{
	struct xway_stp *chip = gpiochip_get_data(gc);

	if (val)
		chip->shadow |= BIT(gpio);
	else
		chip->shadow &= ~BIT(gpio);
	xway_stp_w32(chip->virt, chip->shadow, XWAY_STP_CPU0);
	xway_stp_w32_mask(chip->virt, 0, XWAY_STP_CON_SWU, XWAY_STP_CON0);
}

/**
 * xway_stp_dir_out() - gpio_chip->dir_out - set gpio direction.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * Same as xway_stp_set, always returns 0.
 */
static int xway_stp_dir_out(struct gpio_chip *gc, unsigned gpio, int val)
{
	xway_stp_set(gc, gpio, val);

	return 0;
}

/**
 * xway_stp_request() - gpio_chip->request
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * We mask out the HW driven pins
 */
static int xway_stp_request(struct gpio_chip *gc, unsigned gpio)
{
	struct xway_stp *chip = gpiochip_get_data(gc);

	if ((gpio < 8) && (chip->reserved & BIT(gpio))) {
		dev_err(gc->parent, "GPIO %d is driven by hardware\n", gpio);
		return -ENODEV;
	}

	return 0;
}

/**
 * xway_stp_hw_init() - Configure the STP unit and enable the clock gate
 * @virt: pointer to the remapped register range
 */
static int xway_stp_hw_init(struct xway_stp *chip)
{
	/* sane defaults */
	xway_stp_w32(chip->virt, 0, XWAY_STP_AR);
	xway_stp_w32(chip->virt, 0, XWAY_STP_CPU0);
	xway_stp_w32(chip->virt, 0, XWAY_STP_CPU1);
	xway_stp_w32(chip->virt, XWAY_STP_CON_SWU, XWAY_STP_CON0);
	xway_stp_w32(chip->virt, 0, XWAY_STP_CON1);

	/* apply edge trigger settings for the shift register */
	xway_stp_w32_mask(chip->virt, XWAY_STP_EDGE_MASK,
				chip->edge, XWAY_STP_CON0);

	/* apply led group settings */
	xway_stp_w32_mask(chip->virt, XWAY_STP_GROUP_MASK,
				chip->groups, XWAY_STP_CON1);

	/* tell the hardware which pins are controlled by the dsl modem */
	xway_stp_w32_mask(chip->virt,
			XWAY_STP_ADSL_MASK << XWAY_STP_ADSL_SHIFT,
			chip->dsl << XWAY_STP_ADSL_SHIFT,
			XWAY_STP_CON0);

	/* tell the hardware which pins are controlled by the phys */
	xway_stp_w32_mask(chip->virt,
			XWAY_STP_PHY_MASK << XWAY_STP_PHY1_SHIFT,
			chip->phy1 << XWAY_STP_PHY1_SHIFT,
			XWAY_STP_CON0);
	xway_stp_w32_mask(chip->virt,
			XWAY_STP_PHY_MASK << XWAY_STP_PHY2_SHIFT,
			chip->phy2 << XWAY_STP_PHY2_SHIFT,
			XWAY_STP_CON1);

	/* mask out the hw driven bits in gpio_request */
	chip->reserved = (chip->phy2 << 5) | (chip->phy1 << 2) | chip->dsl;

	/*
	 * if we have pins that are driven by hw, we need to tell the stp what
	 * clock to use as a timer.
	 */
	if (chip->reserved)
		xway_stp_w32_mask(chip->virt, XWAY_STP_UPD_MASK,
			XWAY_STP_UPD_FPI, XWAY_STP_CON1);

	return 0;
}

static int xway_stp_probe(struct platform_device *pdev)
{
	u32 shadow, groups, dsl, phy;
	struct xway_stp *chip;
	struct clk *clk;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->virt = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->virt))
		return PTR_ERR(chip->virt);

	chip->gc.parent = &pdev->dev;
	chip->gc.label = "stp-xway";
	chip->gc.direction_output = xway_stp_dir_out;
	chip->gc.get = xway_stp_get;
	chip->gc.set = xway_stp_set;
	chip->gc.request = xway_stp_request;
	chip->gc.base = -1;
	chip->gc.owner = THIS_MODULE;

	/* store the shadow value if one was passed by the devicetree */
	if (!of_property_read_u32(pdev->dev.of_node, "lantiq,shadow", &shadow))
		chip->shadow = shadow;

	/* find out which gpio groups should be enabled */
	if (!of_property_read_u32(pdev->dev.of_node, "lantiq,groups", &groups))
		chip->groups = groups & XWAY_STP_GROUP_MASK;
	else
		chip->groups = XWAY_STP_GROUP0;
	chip->gc.ngpio = fls(chip->groups) * 8;

	/* find out which gpios are controlled by the dsl core */
	if (!of_property_read_u32(pdev->dev.of_node, "lantiq,dsl", &dsl))
		chip->dsl = dsl & XWAY_STP_ADSL_MASK;

	/* find out which gpios are controlled by the phys */
	if (of_machine_is_compatible("lantiq,ar9") ||
			of_machine_is_compatible("lantiq,gr9") ||
			of_machine_is_compatible("lantiq,vr9")) {
		if (!of_property_read_u32(pdev->dev.of_node, "lantiq,phy1", &phy))
			chip->phy1 = phy & XWAY_STP_PHY_MASK;
		if (!of_property_read_u32(pdev->dev.of_node, "lantiq,phy2", &phy))
			chip->phy2 = phy & XWAY_STP_PHY_MASK;
	}

	/* check which edge trigger we should use, default to a falling edge */
	if (!of_find_property(pdev->dev.of_node, "lantiq,rising", NULL))
		chip->edge = XWAY_STP_FALLING;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return PTR_ERR(clk);
	}
	clk_enable(clk);

	ret = xway_stp_hw_init(chip);
	if (!ret)
		ret = devm_gpiochip_add_data(&pdev->dev, &chip->gc, chip);

	if (!ret)
		dev_info(&pdev->dev, "Init done\n");

	return ret;
}

static const struct of_device_id xway_stp_match[] = {
	{ .compatible = "lantiq,gpio-stp-xway" },
	{},
};
MODULE_DEVICE_TABLE(of, xway_stp_match);

static struct platform_driver xway_stp_driver = {
	.probe = xway_stp_probe,
	.driver = {
		.name = "gpio-stp-xway",
		.of_match_table = xway_stp_match,
	},
};

static int __init xway_stp_init(void)
{
	return platform_driver_register(&xway_stp_driver);
}

subsys_initcall(xway_stp_init);
