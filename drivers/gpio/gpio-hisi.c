// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 HiSilicon Limited. */
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define HISI_GPIO_SWPORT_DR_SET_WX	0x000
#define HISI_GPIO_SWPORT_DR_CLR_WX	0x004
#define HISI_GPIO_SWPORT_DDR_SET_WX	0x010
#define HISI_GPIO_SWPORT_DDR_CLR_WX	0x014
#define HISI_GPIO_SWPORT_DDR_ST_WX	0x018
#define HISI_GPIO_INTEN_SET_WX		0x020
#define HISI_GPIO_INTEN_CLR_WX		0x024
#define HISI_GPIO_INTMASK_SET_WX	0x030
#define HISI_GPIO_INTMASK_CLR_WX	0x034
#define HISI_GPIO_INTTYPE_EDGE_SET_WX	0x040
#define HISI_GPIO_INTTYPE_EDGE_CLR_WX	0x044
#define HISI_GPIO_INT_POLARITY_SET_WX	0x050
#define HISI_GPIO_INT_POLARITY_CLR_WX	0x054
#define HISI_GPIO_DEBOUNCE_SET_WX	0x060
#define HISI_GPIO_DEBOUNCE_CLR_WX	0x064
#define HISI_GPIO_INTSTATUS_WX		0x070
#define HISI_GPIO_PORTA_EOI_WX		0x078
#define HISI_GPIO_EXT_PORT_WX		0x080
#define HISI_GPIO_INTCOMB_MASK_WX	0x0a0
#define HISI_GPIO_INT_DEDGE_SET		0x0b0
#define HISI_GPIO_INT_DEDGE_CLR		0x0b4
#define HISI_GPIO_INT_DEDGE_ST		0x0b8

#define HISI_GPIO_LINE_NUM_MAX	32
#define HISI_GPIO_DRIVER_NAME	"gpio-hisi"

struct hisi_gpio {
	struct gpio_chip	chip;
	struct device		*dev;
	void __iomem		*reg_base;
	unsigned int		line_num;
	struct irq_chip		irq_chip;
	int			irq;
};

static inline u32 hisi_gpio_read_reg(struct gpio_chip *chip,
				     unsigned int off)
{
	struct hisi_gpio *hisi_gpio =
			container_of(chip, struct hisi_gpio, chip);
	void __iomem *reg = hisi_gpio->reg_base + off;

	return readl(reg);
}

static inline void hisi_gpio_write_reg(struct gpio_chip *chip,
				       unsigned int off, u32 val)
{
	struct hisi_gpio *hisi_gpio =
			container_of(chip, struct hisi_gpio, chip);
	void __iomem *reg = hisi_gpio->reg_base + off;

	writel(val, reg);
}

static void hisi_gpio_set_debounce(struct gpio_chip *chip, unsigned int off,
				   u32 debounce)
{
	if (debounce)
		hisi_gpio_write_reg(chip, HISI_GPIO_DEBOUNCE_SET_WX, BIT(off));
	else
		hisi_gpio_write_reg(chip, HISI_GPIO_DEBOUNCE_CLR_WX, BIT(off));
}

static int hisi_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				unsigned long config)
{
	u32 config_para = pinconf_to_config_param(config);
	u32 config_arg;

	switch (config_para) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
		config_arg = pinconf_to_config_argument(config);
		hisi_gpio_set_debounce(chip, offset, config_arg);
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void hisi_gpio_set_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	hisi_gpio_write_reg(chip, HISI_GPIO_PORTA_EOI_WX, BIT(irqd_to_hwirq(d)));
}

static void hisi_gpio_irq_set_mask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	hisi_gpio_write_reg(chip, HISI_GPIO_INTMASK_SET_WX, BIT(irqd_to_hwirq(d)));
}

static void hisi_gpio_irq_clr_mask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	hisi_gpio_write_reg(chip, HISI_GPIO_INTMASK_CLR_WX, BIT(irqd_to_hwirq(d)));
}

static int hisi_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(irqd_to_hwirq(d));

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		hisi_gpio_write_reg(chip, HISI_GPIO_INT_DEDGE_SET, mask);
		break;
	case IRQ_TYPE_EDGE_RISING:
		hisi_gpio_write_reg(chip, HISI_GPIO_INTTYPE_EDGE_SET_WX, mask);
		hisi_gpio_write_reg(chip, HISI_GPIO_INT_POLARITY_SET_WX, mask);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		hisi_gpio_write_reg(chip, HISI_GPIO_INTTYPE_EDGE_SET_WX, mask);
		hisi_gpio_write_reg(chip, HISI_GPIO_INT_POLARITY_CLR_WX, mask);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		hisi_gpio_write_reg(chip, HISI_GPIO_INTTYPE_EDGE_CLR_WX, mask);
		hisi_gpio_write_reg(chip, HISI_GPIO_INT_POLARITY_SET_WX, mask);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		hisi_gpio_write_reg(chip, HISI_GPIO_INTTYPE_EDGE_CLR_WX, mask);
		hisi_gpio_write_reg(chip, HISI_GPIO_INT_POLARITY_CLR_WX, mask);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * The dual-edge interrupt and other interrupt's registers do not
	 * take effect at the same time. The registers of the two-edge
	 * interrupts have higher priorities, the configuration of
	 * the dual-edge interrupts must be disabled before the configuration
	 * of other kind of interrupts.
	 */
	if (type != IRQ_TYPE_EDGE_BOTH) {
		unsigned int both = hisi_gpio_read_reg(chip, HISI_GPIO_INT_DEDGE_ST);

		if (both & mask)
			hisi_gpio_write_reg(chip, HISI_GPIO_INT_DEDGE_CLR, mask);
	}

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static void hisi_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	hisi_gpio_irq_clr_mask(d);
	hisi_gpio_write_reg(chip, HISI_GPIO_INTEN_SET_WX, BIT(irqd_to_hwirq(d)));
}

static void hisi_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	hisi_gpio_irq_set_mask(d);
	hisi_gpio_write_reg(chip, HISI_GPIO_INTEN_CLR_WX, BIT(irqd_to_hwirq(d)));
}

static void hisi_gpio_irq_handler(struct irq_desc *desc)
{
	struct hisi_gpio *hisi_gpio = irq_desc_get_handler_data(desc);
	unsigned long irq_msk = hisi_gpio_read_reg(&hisi_gpio->chip,
						   HISI_GPIO_INTSTATUS_WX);
	struct irq_chip *irq_c = irq_desc_get_chip(desc);
	int hwirq;

	chained_irq_enter(irq_c, desc);
	for_each_set_bit(hwirq, &irq_msk, HISI_GPIO_LINE_NUM_MAX)
		generic_handle_irq(irq_find_mapping(hisi_gpio->chip.irq.domain,
						    hwirq));
	chained_irq_exit(irq_c, desc);
}

static void hisi_gpio_init_irq(struct hisi_gpio *hisi_gpio)
{
	struct gpio_chip *chip = &hisi_gpio->chip;
	struct gpio_irq_chip *girq_chip = &chip->irq;

	/* Set hooks for irq_chip */
	hisi_gpio->irq_chip.irq_ack = hisi_gpio_set_ack;
	hisi_gpio->irq_chip.irq_mask = hisi_gpio_irq_set_mask;
	hisi_gpio->irq_chip.irq_unmask = hisi_gpio_irq_clr_mask;
	hisi_gpio->irq_chip.irq_set_type = hisi_gpio_irq_set_type;
	hisi_gpio->irq_chip.irq_enable = hisi_gpio_irq_enable;
	hisi_gpio->irq_chip.irq_disable = hisi_gpio_irq_disable;

	girq_chip->chip = &hisi_gpio->irq_chip;
	girq_chip->default_type = IRQ_TYPE_NONE;
	girq_chip->num_parents = 1;
	girq_chip->parents = &hisi_gpio->irq;
	girq_chip->parent_handler = hisi_gpio_irq_handler;
	girq_chip->parent_handler_data = hisi_gpio;

	/* Clear Mask of GPIO controller combine IRQ */
	hisi_gpio_write_reg(chip, HISI_GPIO_INTCOMB_MASK_WX, 1);
}

static const struct acpi_device_id hisi_gpio_acpi_match[] = {
	{"HISI0184", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, hisi_gpio_acpi_match);

static void hisi_gpio_get_pdata(struct device *dev,
				struct hisi_gpio *hisi_gpio)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fwnode_handle *fwnode;
	int idx = 0;

	device_for_each_child_node(dev, fwnode)  {
		/* Cycle for once, no need for an array to save line_num */
		if (fwnode_property_read_u32(fwnode, "ngpios",
					     &hisi_gpio->line_num)) {
			dev_err(dev,
				"failed to get number of lines for port%d and use default value instead\n",
				idx);
			hisi_gpio->line_num = HISI_GPIO_LINE_NUM_MAX;
		}

		if (WARN_ON(hisi_gpio->line_num > HISI_GPIO_LINE_NUM_MAX))
			hisi_gpio->line_num = HISI_GPIO_LINE_NUM_MAX;

		hisi_gpio->irq = platform_get_irq(pdev, idx);

		dev_info(dev,
			 "get hisi_gpio[%d] with %d lines\n", idx,
			 hisi_gpio->line_num);

		idx++;
	}
}

static int hisi_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_gpio *hisi_gpio;
	int port_num;
	int ret;

	/*
	 * One GPIO controller own one port currently,
	 * if we get more from ACPI table, return error.
	 */
	port_num = device_get_child_node_count(dev);
	if (WARN_ON(port_num != 1))
		return -ENODEV;

	hisi_gpio = devm_kzalloc(dev, sizeof(*hisi_gpio), GFP_KERNEL);
	if (!hisi_gpio)
		return -ENOMEM;

	hisi_gpio->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hisi_gpio->reg_base))
		return PTR_ERR(hisi_gpio->reg_base);

	hisi_gpio_get_pdata(dev, hisi_gpio);

	hisi_gpio->dev = dev;

	ret = bgpio_init(&hisi_gpio->chip, hisi_gpio->dev, 0x4,
			 hisi_gpio->reg_base + HISI_GPIO_EXT_PORT_WX,
			 hisi_gpio->reg_base + HISI_GPIO_SWPORT_DR_SET_WX,
			 hisi_gpio->reg_base + HISI_GPIO_SWPORT_DR_CLR_WX,
			 hisi_gpio->reg_base + HISI_GPIO_SWPORT_DDR_SET_WX,
			 hisi_gpio->reg_base + HISI_GPIO_SWPORT_DDR_CLR_WX,
			 BGPIOF_NO_SET_ON_INPUT);
	if (ret) {
		dev_err(dev, "failed to init, ret = %d\n", ret);
		return ret;
	}

	hisi_gpio->chip.set_config = hisi_gpio_set_config;
	hisi_gpio->chip.ngpio = hisi_gpio->line_num;
	hisi_gpio->chip.bgpio_dir_unreadable = 1;
	hisi_gpio->chip.base = -1;

	if (hisi_gpio->irq > 0)
		hisi_gpio_init_irq(hisi_gpio);

	ret = devm_gpiochip_add_data(dev, &hisi_gpio->chip, hisi_gpio);
	if (ret) {
		dev_err(dev, "failed to register gpiochip, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver hisi_gpio_driver = {
	.driver		= {
		.name	= HISI_GPIO_DRIVER_NAME,
		.acpi_match_table = hisi_gpio_acpi_match,
	},
	.probe		= hisi_gpio_probe,
};

module_platform_driver(hisi_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luo Jiaxing <luojiaxing@huawei.com>");
MODULE_DESCRIPTION("HiSilicon GPIO controller driver");
MODULE_ALIAS("platform:" HISI_GPIO_DRIVER_NAME);
