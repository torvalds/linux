// SPDX-License-Identifier: GPL-2.0
/*
 * Intel INT0002 "Virtual GPIO" driver
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Loosely based on android x86 kernel code which is:
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * Author: Dyut Kumar Sil <dyut.k.sil@intel.com>
 *
 * Some peripherals on Bay Trail and Cherry Trail platforms signal a Power
 * Management Event (PME) to the Power Management Controller (PMC) to wakeup
 * the system. When this happens software needs to clear the PME bus 0 status
 * bit in the GPE0a_STS register to avoid an IRQ storm on IRQ 9.
 *
 * This is modelled in ACPI through the INT0002 ACPI device, which is
 * called a "Virtual GPIO controller" in ACPI because it defines the event
 * handler to call when the PME triggers through _AEI and _L02 / _E02
 * methods as would be done for a real GPIO interrupt in ACPI. Note this
 * is a hack to define an AML event handler for the PME while using existing
 * ACPI mechanisms, this is not a real GPIO at all.
 *
 * This driver will bind to the INT0002 device, and register as a GPIO
 * controller, letting gpiolib-acpi.c call the _L02 handler as it would
 * for a real GPIO controller.
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define DRV_NAME			"INT0002 Virtual GPIO"

/* For some reason the virtual GPIO pin tied to the GPE is numbered pin 2 */
#define GPE0A_PME_B0_VIRT_GPIO_PIN	2

#define GPE0A_PME_B0_STS_BIT		BIT(13)
#define GPE0A_PME_B0_EN_BIT		BIT(13)
#define GPE0A_STS_PORT			0x420
#define GPE0A_EN_PORT			0x428

/*
 * As this is not a real GPIO at all, but just a hack to model an event in
 * ACPI the get / set functions are dummy functions.
 */

static int int0002_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return 0;
}

static void int0002_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
}

static int int0002_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int value)
{
	return 0;
}

static void int0002_irq_ack(struct irq_data *data)
{
	outl(GPE0A_PME_B0_STS_BIT, GPE0A_STS_PORT);
}

static void int0002_irq_unmask(struct irq_data *data)
{
	u32 gpe_en_reg;

	gpe_en_reg = inl(GPE0A_EN_PORT);
	gpe_en_reg |= GPE0A_PME_B0_EN_BIT;
	outl(gpe_en_reg, GPE0A_EN_PORT);
}

static void int0002_irq_mask(struct irq_data *data)
{
	u32 gpe_en_reg;

	gpe_en_reg = inl(GPE0A_EN_PORT);
	gpe_en_reg &= ~GPE0A_PME_B0_EN_BIT;
	outl(gpe_en_reg, GPE0A_EN_PORT);
}

static int int0002_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct platform_device *pdev = to_platform_device(chip->parent);
	int irq = platform_get_irq(pdev, 0);

	/* Propagate to parent irq */
	if (on)
		enable_irq_wake(irq);
	else
		disable_irq_wake(irq);

	return 0;
}

static irqreturn_t int0002_irq(int irq, void *data)
{
	struct gpio_chip *chip = data;
	u32 gpe_sts_reg;

	gpe_sts_reg = inl(GPE0A_STS_PORT);
	if (!(gpe_sts_reg & GPE0A_PME_B0_STS_BIT))
		return IRQ_NONE;

	generic_handle_irq(irq_find_mapping(chip->irq.domain,
					    GPE0A_PME_B0_VIRT_GPIO_PIN));

	pm_system_wakeup();

	return IRQ_HANDLED;
}

static struct irq_chip int0002_byt_irqchip = {
	.name			= DRV_NAME,
	.irq_ack		= int0002_irq_ack,
	.irq_mask		= int0002_irq_mask,
	.irq_unmask		= int0002_irq_unmask,
	.irq_set_wake		= int0002_irq_set_wake,
};

static struct irq_chip int0002_cht_irqchip = {
	.name			= DRV_NAME,
	.irq_ack		= int0002_irq_ack,
	.irq_mask		= int0002_irq_mask,
	.irq_unmask		= int0002_irq_unmask,
	/*
	 * No set_wake, on CHT the IRQ is typically shared with the ACPI SCI
	 * and we don't want to mess with the ACPI SCI irq settings.
	 */
};

static const struct x86_cpu_id int0002_cpu_ids[] = {
	INTEL_CPU_FAM6(ATOM_SILVERMONT, int0002_byt_irqchip),	/* Valleyview, Bay Trail  */
	INTEL_CPU_FAM6(ATOM_AIRMONT, int0002_cht_irqchip),	/* Braswell, Cherry Trail */
	{}
};

static int int0002_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct x86_cpu_id *cpu_id;
	struct irq_chip *irq_chip;
	struct gpio_chip *chip;
	int irq, ret;

	/* Menlow has a different INT0002 device? <sigh> */
	cpu_id = x86_match_cpu(int0002_cpu_ids);
	if (!cpu_id)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Error getting IRQ: %d\n", irq);
		return irq;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->label = DRV_NAME;
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->get = int0002_gpio_get;
	chip->set = int0002_gpio_set;
	chip->direction_input = int0002_gpio_get;
	chip->direction_output = int0002_gpio_direction_output;
	chip->base = -1;
	chip->ngpio = GPE0A_PME_B0_VIRT_GPIO_PIN + 1;
	chip->irq.need_valid_mask = true;

	ret = devm_gpiochip_add_data(&pdev->dev, chip, NULL);
	if (ret) {
		dev_err(dev, "Error adding gpio chip: %d\n", ret);
		return ret;
	}

	bitmap_clear(chip->irq.valid_mask, 0, GPE0A_PME_B0_VIRT_GPIO_PIN);

	/*
	 * We manually request the irq here instead of passing a flow-handler
	 * to gpiochip_set_chained_irqchip, because the irq is shared.
	 */
	ret = devm_request_irq(dev, irq, int0002_irq,
			       IRQF_SHARED, "INT0002", chip);
	if (ret) {
		dev_err(dev, "Error requesting IRQ %d: %d\n", irq, ret);
		return ret;
	}

	irq_chip = (struct irq_chip *)cpu_id->driver_data;

	ret = gpiochip_irqchip_add(chip, irq_chip, 0, handle_edge_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "Error adding irqchip: %d\n", ret);
		return ret;
	}

	gpiochip_set_chained_irqchip(chip, irq_chip, irq, NULL);

	return 0;
}

static const struct acpi_device_id int0002_acpi_ids[] = {
	{ "INT0002", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, int0002_acpi_ids);

static struct platform_driver int0002_driver = {
	.driver = {
		.name			= DRV_NAME,
		.acpi_match_table	= int0002_acpi_ids,
	},
	.probe	= int0002_probe,
};

module_platform_driver(int0002_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Intel INT0002 Virtual GPIO driver");
MODULE_LICENSE("GPL v2");
