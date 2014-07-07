/*
 * Pinctrl GPIO driver for Intel Baytrail
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * Author: Mathias Nyman <mathias.nyman@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/irqdomain.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/pinctrl.h>

/* memory mapped register offsets */
#define BYT_CONF0_REG		0x000
#define BYT_CONF1_REG		0x004
#define BYT_VAL_REG		0x008
#define BYT_DFT_REG		0x00c
#define BYT_INT_STAT_REG	0x800

/* BYT_CONF0_REG register bits */
#define BYT_IODEN		BIT(31)
#define BYT_TRIG_NEG		BIT(26)
#define BYT_TRIG_POS		BIT(25)
#define BYT_TRIG_LVL		BIT(24)
#define BYT_PULL_STR_SHIFT	9
#define BYT_PULL_STR_MASK	(3 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_2K		(0 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_10K	(1 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_20K	(2 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_40K	(3 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_ASSIGN_SHIFT	7
#define BYT_PULL_ASSIGN_MASK	(3 << BYT_PULL_ASSIGN_SHIFT)
#define BYT_PULL_ASSIGN_UP	(1 << BYT_PULL_ASSIGN_SHIFT)
#define BYT_PULL_ASSIGN_DOWN	(2 << BYT_PULL_ASSIGN_SHIFT)
#define BYT_PIN_MUX		0x07

/* BYT_VAL_REG register bits */
#define BYT_INPUT_EN		BIT(2)  /* 0: input enabled (active low)*/
#define BYT_OUTPUT_EN		BIT(1)  /* 0: output enabled (active low)*/
#define BYT_LEVEL		BIT(0)

#define BYT_DIR_MASK		(BIT(1) | BIT(2))
#define BYT_TRIG_MASK		(BIT(26) | BIT(25) | BIT(24))

#define BYT_NGPIO_SCORE		102
#define BYT_NGPIO_NCORE		28
#define BYT_NGPIO_SUS		44

#define BYT_SCORE_ACPI_UID	"1"
#define BYT_NCORE_ACPI_UID	"2"
#define BYT_SUS_ACPI_UID	"3"

/*
 * Baytrail gpio controller consist of three separate sub-controllers called
 * SCORE, NCORE and SUS. The sub-controllers are identified by their acpi UID.
 *
 * GPIO numbering is _not_ ordered meaning that gpio # 0 in ACPI namespace does
 * _not_ correspond to the first gpio register at controller's gpio base.
 * There is no logic or pattern in mapping gpio numbers to registers (pads) so
 * each sub-controller needs to have its own mapping table
 */

/* score_pins[gpio_nr] = pad_nr */

static unsigned const score_pins[BYT_NGPIO_SCORE] = {
	85, 89, 93, 96, 99, 102, 98, 101, 34, 37,
	36, 38, 39, 35, 40, 84, 62, 61, 64, 59,
	54, 56, 60, 55, 63, 57, 51, 50, 53, 47,
	52, 49, 48, 43, 46, 41, 45, 42, 58, 44,
	95, 105, 70, 68, 67, 66, 69, 71, 65, 72,
	86, 90, 88, 92, 103, 77, 79, 83, 78, 81,
	80, 82, 13, 12, 15, 14, 17, 18, 19, 16,
	2, 1, 0, 4, 6, 7, 9, 8, 33, 32,
	31, 30, 29, 27, 25, 28, 26, 23, 21, 20,
	24, 22, 5, 3, 10, 11, 106, 87, 91, 104,
	97, 100,
};

static unsigned const ncore_pins[BYT_NGPIO_NCORE] = {
	19, 18, 17, 20, 21, 22, 24, 25, 23, 16,
	14, 15, 12, 26, 27, 1, 4, 8, 11, 0,
	3, 6, 10, 13, 2, 5, 9, 7,
};

static unsigned const sus_pins[BYT_NGPIO_SUS] = {
	29, 33, 30, 31, 32, 34, 36, 35, 38, 37,
	18, 7, 11, 20, 17, 1, 8, 10, 19, 12,
	0, 2, 23, 39, 28, 27, 22, 21, 24, 25,
	26, 51, 56, 54, 49, 55, 48, 57, 50, 58,
	52, 53, 59, 40,
};

static struct pinctrl_gpio_range byt_ranges[] = {
	{
		.name = BYT_SCORE_ACPI_UID, /* match with acpi _UID in probe */
		.npins = BYT_NGPIO_SCORE,
		.pins = score_pins,
	},
	{
		.name = BYT_NCORE_ACPI_UID,
		.npins = BYT_NGPIO_NCORE,
		.pins = ncore_pins,
	},
	{
		.name = BYT_SUS_ACPI_UID,
		.npins = BYT_NGPIO_SUS,
		.pins = sus_pins,
	},
	{
	},
};

struct byt_gpio {
	struct gpio_chip		chip;
	struct irq_domain		*domain;
	struct platform_device		*pdev;
	spinlock_t			lock;
	void __iomem			*reg_base;
	struct pinctrl_gpio_range	*range;
};

#define to_byt_gpio(c)	container_of(c, struct byt_gpio, chip)

static void __iomem *byt_gpio_reg(struct gpio_chip *chip, unsigned offset,
				 int reg)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	u32 reg_offset;

	if (reg == BYT_INT_STAT_REG)
		reg_offset = (offset / 32) * 4;
	else
		reg_offset = vg->range->pins[offset] * 16;

	return vg->reg_base + reg_offset + reg;
}

static bool is_special_pin(struct byt_gpio *vg, unsigned offset)
{
	/* SCORE pin 92-93 */
	if (!strcmp(vg->range->name, BYT_SCORE_ACPI_UID) &&
		offset >= 92 && offset <= 93)
		return true;

	/* SUS pin 11-21 */
	if (!strcmp(vg->range->name, BYT_SUS_ACPI_UID) &&
		offset >= 11 && offset <= 21)
		return true;

	return false;
}

static int byt_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	void __iomem *reg = byt_gpio_reg(chip, offset, BYT_CONF0_REG);
	u32 value;
	bool special;

	/*
	 * In most cases, func pin mux 000 means GPIO function.
	 * But, some pins may have func pin mux 001 represents
	 * GPIO function. Only allow user to export pin with
	 * func pin mux preset as GPIO function by BIOS/FW.
	 */
	value = readl(reg) & BYT_PIN_MUX;
	special = is_special_pin(vg, offset);
	if ((special && value != 1) || (!special && value)) {
		dev_err(&vg->pdev->dev,
			"pin %u cannot be used as GPIO.\n", offset);
		return -EINVAL;
	}

	pm_runtime_get(&vg->pdev->dev);

	return 0;
}

static void byt_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	void __iomem *reg = byt_gpio_reg(&vg->chip, offset, BYT_CONF0_REG);
	u32 value;

	/* clear interrupt triggering */
	value = readl(reg);
	value &= ~(BYT_TRIG_POS | BYT_TRIG_NEG | BYT_TRIG_LVL);
	writel(value, reg);

	pm_runtime_put(&vg->pdev->dev);
}

static int byt_irq_type(struct irq_data *d, unsigned type)
{
	struct byt_gpio *vg = irq_data_get_irq_chip_data(d);
	u32 offset = irqd_to_hwirq(d);
	u32 value;
	unsigned long flags;
	void __iomem *reg = byt_gpio_reg(&vg->chip, offset, BYT_CONF0_REG);

	if (offset >= vg->chip.ngpio)
		return -EINVAL;

	spin_lock_irqsave(&vg->lock, flags);
	value = readl(reg);

	/* For level trigges the BYT_TRIG_POS and BYT_TRIG_NEG bits
	 * are used to indicate high and low level triggering
	 */
	value &= ~(BYT_TRIG_POS | BYT_TRIG_NEG | BYT_TRIG_LVL);

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		value |= BYT_TRIG_LVL;
	case IRQ_TYPE_EDGE_RISING:
		value |= BYT_TRIG_POS;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		value |= BYT_TRIG_LVL;
	case IRQ_TYPE_EDGE_FALLING:
		value |= BYT_TRIG_NEG;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		value |= (BYT_TRIG_NEG | BYT_TRIG_POS);
		break;
	}
	writel(value, reg);

	spin_unlock_irqrestore(&vg->lock, flags);

	return 0;
}

static int byt_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *reg = byt_gpio_reg(chip, offset, BYT_VAL_REG);
	return readl(reg) & BYT_LEVEL;
}

static void byt_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	void __iomem *reg = byt_gpio_reg(chip, offset, BYT_VAL_REG);
	unsigned long flags;
	u32 old_val;

	spin_lock_irqsave(&vg->lock, flags);

	old_val = readl(reg);

	if (value)
		writel(old_val | BYT_LEVEL, reg);
	else
		writel(old_val & ~BYT_LEVEL, reg);

	spin_unlock_irqrestore(&vg->lock, flags);
}

static int byt_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	void __iomem *reg = byt_gpio_reg(chip, offset, BYT_VAL_REG);
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&vg->lock, flags);

	value = readl(reg) | BYT_DIR_MASK;
	value &= ~BYT_INPUT_EN;		/* active low */
	writel(value, reg);

	spin_unlock_irqrestore(&vg->lock, flags);

	return 0;
}

static int byt_gpio_direction_output(struct gpio_chip *chip,
				     unsigned gpio, int value)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	void __iomem *reg = byt_gpio_reg(chip, gpio, BYT_VAL_REG);
	unsigned long flags;
	u32 reg_val;

	spin_lock_irqsave(&vg->lock, flags);

	reg_val = readl(reg) | BYT_DIR_MASK;
	reg_val &= ~BYT_OUTPUT_EN;

	if (value)
		writel(reg_val | BYT_LEVEL, reg);
	else
		writel(reg_val & ~BYT_LEVEL, reg);

	spin_unlock_irqrestore(&vg->lock, flags);

	return 0;
}

static void byt_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	int i;
	unsigned long flags;
	u32 conf0, val, offs;

	spin_lock_irqsave(&vg->lock, flags);

	for (i = 0; i < vg->chip.ngpio; i++) {
		const char *pull_str = NULL;
		const char *pull = NULL;
		const char *label;
		offs = vg->range->pins[i] * 16;
		conf0 = readl(vg->reg_base + offs + BYT_CONF0_REG);
		val = readl(vg->reg_base + offs + BYT_VAL_REG);

		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "Unrequested";

		switch (conf0 & BYT_PULL_ASSIGN_MASK) {
		case BYT_PULL_ASSIGN_UP:
			pull = "up";
			break;
		case BYT_PULL_ASSIGN_DOWN:
			pull = "down";
			break;
		}

		switch (conf0 & BYT_PULL_STR_MASK) {
		case BYT_PULL_STR_2K:
			pull_str = "2k";
			break;
		case BYT_PULL_STR_10K:
			pull_str = "10k";
			break;
		case BYT_PULL_STR_20K:
			pull_str = "20k";
			break;
		case BYT_PULL_STR_40K:
			pull_str = "40k";
			break;
		}

		seq_printf(s,
			   " gpio-%-3d (%-20.20s) %s %s %s pad-%-3d offset:0x%03x mux:%d %s%s%s",
			   i,
			   label,
			   val & BYT_INPUT_EN ? "  " : "in",
			   val & BYT_OUTPUT_EN ? "   " : "out",
			   val & BYT_LEVEL ? "hi" : "lo",
			   vg->range->pins[i], offs,
			   conf0 & 0x7,
			   conf0 & BYT_TRIG_NEG ? " fall" : "     ",
			   conf0 & BYT_TRIG_POS ? " rise" : "     ",
			   conf0 & BYT_TRIG_LVL ? " level" : "      ");

		if (pull && pull_str)
			seq_printf(s, " %-4s %-3s", pull, pull_str);
		else
			seq_puts(s, "          ");

		if (conf0 & BYT_IODEN)
			seq_puts(s, " open-drain");

		seq_puts(s, "\n");
	}
	spin_unlock_irqrestore(&vg->lock, flags);
}

static int byt_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct byt_gpio *vg = to_byt_gpio(chip);
	return irq_create_mapping(vg->domain, offset);
}

static void byt_gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct byt_gpio *vg = irq_data_get_irq_handler_data(data);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	u32 base, pin, mask;
	void __iomem *reg;
	u32 pending;
	unsigned virq;
	int looplimit = 0;

	/* check from GPIO controller which pin triggered the interrupt */
	for (base = 0; base < vg->chip.ngpio; base += 32) {

		reg = byt_gpio_reg(&vg->chip, base, BYT_INT_STAT_REG);

		while ((pending = readl(reg))) {
			pin = __ffs(pending);
			mask = BIT(pin);
			/* Clear before handling so we can't lose an edge */
			writel(mask, reg);

			virq = irq_find_mapping(vg->domain, base + pin);
			generic_handle_irq(virq);

			/* In case bios or user sets triggering incorretly a pin
			 * might remain in "interrupt triggered" state.
			 */
			if (looplimit++ > 32) {
				dev_err(&vg->pdev->dev,
					"Gpio %d interrupt flood, disabling\n",
					base + pin);

				reg = byt_gpio_reg(&vg->chip, base + pin,
						   BYT_CONF0_REG);
				mask = readl(reg);
				mask &= ~(BYT_TRIG_NEG | BYT_TRIG_POS |
					  BYT_TRIG_LVL);
				writel(mask, reg);
				mask = readl(reg); /* flush */
				break;
			}
		}
	}
	chip->irq_eoi(data);
}

static void byt_irq_unmask(struct irq_data *d)
{
}

static void byt_irq_mask(struct irq_data *d)
{
}

static int byt_irq_reqres(struct irq_data *d)
{
	struct byt_gpio *vg = irq_data_get_irq_chip_data(d);

	if (gpio_lock_as_irq(&vg->chip, irqd_to_hwirq(d))) {
		dev_err(vg->chip.dev,
			"unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
		return -EINVAL;
	}
	return 0;
}

static void byt_irq_relres(struct irq_data *d)
{
	struct byt_gpio *vg = irq_data_get_irq_chip_data(d);

	gpio_unlock_as_irq(&vg->chip, irqd_to_hwirq(d));
}

static struct irq_chip byt_irqchip = {
	.name = "BYT-GPIO",
	.irq_mask = byt_irq_mask,
	.irq_unmask = byt_irq_unmask,
	.irq_set_type = byt_irq_type,
	.irq_request_resources = byt_irq_reqres,
	.irq_release_resources = byt_irq_relres,
};

static void byt_gpio_irq_init_hw(struct byt_gpio *vg)
{
	void __iomem *reg;
	u32 base, value;

	/* clear interrupt status trigger registers */
	for (base = 0; base < vg->chip.ngpio; base += 32) {
		reg = byt_gpio_reg(&vg->chip, base, BYT_INT_STAT_REG);
		writel(0xffffffff, reg);
		/* make sure trigger bits are cleared, if not then a pin
		   might be misconfigured in bios */
		value = readl(reg);
		if (value)
			dev_err(&vg->pdev->dev,
				"GPIO interrupt error, pins misconfigured\n");
	}
}

static int byt_gpio_irq_map(struct irq_domain *d, unsigned int virq,
			    irq_hw_number_t hw)
{
	struct byt_gpio *vg = d->host_data;

	irq_set_chip_and_handler_name(virq, &byt_irqchip, handle_simple_irq,
				      "demux");
	irq_set_chip_data(virq, vg);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops byt_gpio_irq_ops = {
	.map = byt_gpio_irq_map,
};

static int byt_gpio_probe(struct platform_device *pdev)
{
	struct byt_gpio *vg;
	struct gpio_chip *gc;
	struct resource *mem_rc, *irq_rc;
	struct device *dev = &pdev->dev;
	struct acpi_device *acpi_dev;
	struct pinctrl_gpio_range *range;
	acpi_handle handle = ACPI_HANDLE(dev);
	unsigned hwirq;
	int ret;

	if (acpi_bus_get_device(handle, &acpi_dev))
		return -ENODEV;

	vg = devm_kzalloc(dev, sizeof(struct byt_gpio), GFP_KERNEL);
	if (!vg) {
		dev_err(&pdev->dev, "can't allocate byt_gpio chip data\n");
		return -ENOMEM;
	}

	for (range = byt_ranges; range->name; range++) {
		if (!strcmp(acpi_dev->pnp.unique_id, range->name)) {
			vg->chip.ngpio = range->npins;
			vg->range = range;
			break;
		}
	}

	if (!vg->chip.ngpio || !vg->range)
		return -ENODEV;

	vg->pdev = pdev;
	platform_set_drvdata(pdev, vg);

	mem_rc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vg->reg_base = devm_ioremap_resource(dev, mem_rc);
	if (IS_ERR(vg->reg_base))
		return PTR_ERR(vg->reg_base);

	spin_lock_init(&vg->lock);

	gc = &vg->chip;
	gc->label = dev_name(&pdev->dev);
	gc->owner = THIS_MODULE;
	gc->request = byt_gpio_request;
	gc->free = byt_gpio_free;
	gc->direction_input = byt_gpio_direction_input;
	gc->direction_output = byt_gpio_direction_output;
	gc->get = byt_gpio_get;
	gc->set = byt_gpio_set;
	gc->dbg_show = byt_gpio_dbg_show;
	gc->base = -1;
	gc->can_sleep = false;
	gc->dev = dev;

	/* set up interrupts  */
	irq_rc = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_rc && irq_rc->start) {
		hwirq = irq_rc->start;
		gc->to_irq = byt_gpio_to_irq;

		vg->domain = irq_domain_add_linear(NULL, gc->ngpio,
						   &byt_gpio_irq_ops, vg);
		if (!vg->domain)
			return -ENXIO;

		byt_gpio_irq_init_hw(vg);

		irq_set_handler_data(hwirq, vg);
		irq_set_chained_handler(hwirq, byt_gpio_irq_handler);
	}

	ret = gpiochip_add(gc);
	if (ret) {
		dev_err(&pdev->dev, "failed adding byt-gpio chip\n");
		return ret;
	}

	pm_runtime_enable(dev);

	return 0;
}

static int byt_gpio_runtime_suspend(struct device *dev)
{
	return 0;
}

static int byt_gpio_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops byt_gpio_pm_ops = {
	.runtime_suspend = byt_gpio_runtime_suspend,
	.runtime_resume = byt_gpio_runtime_resume,
};

static const struct acpi_device_id byt_gpio_acpi_match[] = {
	{ "INT33B2", 0 },
	{ "INT33FC", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, byt_gpio_acpi_match);

static int byt_gpio_remove(struct platform_device *pdev)
{
	struct byt_gpio *vg = platform_get_drvdata(pdev);
	int err;

	pm_runtime_disable(&pdev->dev);
	err = gpiochip_remove(&vg->chip);
	if (err)
		dev_warn(&pdev->dev, "failed to remove gpio_chip.\n");

	return 0;
}

static struct platform_driver byt_gpio_driver = {
	.probe          = byt_gpio_probe,
	.remove         = byt_gpio_remove,
	.driver         = {
		.name   = "byt_gpio",
		.owner  = THIS_MODULE,
		.pm	= &byt_gpio_pm_ops,
		.acpi_match_table = ACPI_PTR(byt_gpio_acpi_match),
	},
};

static int __init byt_gpio_init(void)
{
	return platform_driver_register(&byt_gpio_driver);
}

subsys_initcall(byt_gpio_init);
