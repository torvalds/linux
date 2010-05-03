/*
 * AMD CS5535/CS5536 GPIO driver
 * Copyright (C) 2006  Advanced Micro Devices, Inc.
 * Copyright (C) 2007-2009  Andres Salomon <dilinger@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/cs5535.h>

#define DRV_NAME "cs5535-gpio"
#define GPIO_BAR 1

/*
 * Some GPIO pins
 *  31-29,23 : reserved (always mask out)
 *  28       : Power Button
 *  26       : PME#
 *  22-16    : LPC
 *  14,15    : SMBus
 *  9,8      : UART1
 *  7        : PCI INTB
 *  3,4      : UART2/DDC
 *  2        : IDE_IRQ0
 *  1        : AC_BEEP
 *  0        : PCI INTA
 *
 * If a mask was not specified, allow all except
 * reserved and Power Button
 */
#define GPIO_DEFAULT_MASK 0x0F7FFFFF

static ulong mask = GPIO_DEFAULT_MASK;
module_param_named(mask, mask, ulong, 0444);
MODULE_PARM_DESC(mask, "GPIO channel mask.");

static struct cs5535_gpio_chip {
	struct gpio_chip chip;
	resource_size_t base;

	struct pci_dev *pdev;
	spinlock_t lock;
} cs5535_gpio_chip;

/*
 * The CS5535/CS5536 GPIOs support a number of extra features not defined
 * by the gpio_chip API, so these are exported.  For a full list of the
 * registers, see include/linux/cs5535.h.
 */

static void __cs5535_gpio_set(struct cs5535_gpio_chip *chip, unsigned offset,
		unsigned int reg)
{
	if (offset < 16)
		/* low bank register */
		outl(1 << offset, chip->base + reg);
	else
		/* high bank register */
		outl(1 << (offset - 16), chip->base + 0x80 + reg);
}

void cs5535_gpio_set(unsigned offset, unsigned int reg)
{
	struct cs5535_gpio_chip *chip = &cs5535_gpio_chip;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	__cs5535_gpio_set(chip, offset, reg);
	spin_unlock_irqrestore(&chip->lock, flags);
}
EXPORT_SYMBOL_GPL(cs5535_gpio_set);

static void __cs5535_gpio_clear(struct cs5535_gpio_chip *chip, unsigned offset,
		unsigned int reg)
{
	if (offset < 16)
		/* low bank register */
		outl(1 << (offset + 16), chip->base + reg);
	else
		/* high bank register */
		outl(1 << offset, chip->base + 0x80 + reg);
}

void cs5535_gpio_clear(unsigned offset, unsigned int reg)
{
	struct cs5535_gpio_chip *chip = &cs5535_gpio_chip;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	__cs5535_gpio_clear(chip, offset, reg);
	spin_unlock_irqrestore(&chip->lock, flags);
}
EXPORT_SYMBOL_GPL(cs5535_gpio_clear);

int cs5535_gpio_isset(unsigned offset, unsigned int reg)
{
	struct cs5535_gpio_chip *chip = &cs5535_gpio_chip;
	unsigned long flags;
	long val;

	spin_lock_irqsave(&chip->lock, flags);
	if (offset < 16)
		/* low bank register */
		val = inl(chip->base + reg);
	else {
		/* high bank register */
		val = inl(chip->base + 0x80 + reg);
		offset -= 16;
	}
	spin_unlock_irqrestore(&chip->lock, flags);

	return (val & (1 << offset)) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(cs5535_gpio_isset);

/*
 * Generic gpio_chip API support.
 */

static int chip_gpio_request(struct gpio_chip *c, unsigned offset)
{
	struct cs5535_gpio_chip *chip = (struct cs5535_gpio_chip *) c;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	/* check if this pin is available */
	if ((mask & (1 << offset)) == 0) {
		dev_info(&chip->pdev->dev,
			"pin %u is not available (check mask)\n", offset);
		spin_unlock_irqrestore(&chip->lock, flags);
		return -EINVAL;
	}

	/* disable output aux 1 & 2 on this pin */
	__cs5535_gpio_clear(chip, offset, GPIO_OUTPUT_AUX1);
	__cs5535_gpio_clear(chip, offset, GPIO_OUTPUT_AUX2);

	/* disable input aux 1 on this pin */
	__cs5535_gpio_clear(chip, offset, GPIO_INPUT_AUX1);

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int chip_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return cs5535_gpio_isset(offset, GPIO_READ_BACK);
}

static void chip_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	if (val)
		cs5535_gpio_set(offset, GPIO_OUTPUT_VAL);
	else
		cs5535_gpio_clear(offset, GPIO_OUTPUT_VAL);
}

static int chip_direction_input(struct gpio_chip *c, unsigned offset)
{
	struct cs5535_gpio_chip *chip = (struct cs5535_gpio_chip *) c;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	__cs5535_gpio_set(chip, offset, GPIO_INPUT_ENABLE);
	__cs5535_gpio_clear(chip, offset, GPIO_OUTPUT_ENABLE);
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int chip_direction_output(struct gpio_chip *c, unsigned offset, int val)
{
	struct cs5535_gpio_chip *chip = (struct cs5535_gpio_chip *) c;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	__cs5535_gpio_set(chip, offset, GPIO_INPUT_ENABLE);
	__cs5535_gpio_set(chip, offset, GPIO_OUTPUT_ENABLE);
	if (val)
		__cs5535_gpio_set(chip, offset, GPIO_OUTPUT_VAL);
	else
		__cs5535_gpio_clear(chip, offset, GPIO_OUTPUT_VAL);

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static char *cs5535_gpio_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3",
	"GPIO4", "GPIO5", "GPIO6", "GPIO7",
	"GPIO8", "GPIO9", "GPIO10", "GPIO11",
	"GPIO12", "GPIO13", "GPIO14", "GPIO15",
	"GPIO16", "GPIO17", "GPIO18", "GPIO19",
	"GPIO20", "GPIO21", "GPIO22", NULL,
	"GPIO24", "GPIO25", "GPIO26", "GPIO27",
	"GPIO28", NULL, NULL, NULL,
};

static struct cs5535_gpio_chip cs5535_gpio_chip = {
	.chip = {
		.owner = THIS_MODULE,
		.label = DRV_NAME,

		.base = 0,
		.ngpio = 32,
		.names = cs5535_gpio_names,
		.request = chip_gpio_request,

		.get = chip_gpio_get,
		.set = chip_gpio_set,

		.direction_input = chip_direction_input,
		.direction_output = chip_direction_output,
	},
};

static int __init cs5535_gpio_probe(struct pci_dev *pdev,
		const struct pci_device_id *pci_id)
{
	int err;
	ulong mask_orig = mask;

	/* There are two ways to get the GPIO base address; one is by
	 * fetching it from MSR_LBAR_GPIO, the other is by reading the
	 * PCI BAR info.  The latter method is easier (especially across
	 * different architectures), so we'll stick with that for now.  If
	 * it turns out to be unreliable in the face of crappy BIOSes, we
	 * can always go back to using MSRs.. */

	err = pci_enable_device_io(pdev);
	if (err) {
		dev_err(&pdev->dev, "can't enable device IO\n");
		goto done;
	}

	err = pci_request_region(pdev, GPIO_BAR, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "can't alloc PCI BAR #%d\n", GPIO_BAR);
		goto done;
	}

	/* set up the driver-specific struct */
	cs5535_gpio_chip.base = pci_resource_start(pdev, GPIO_BAR);
	cs5535_gpio_chip.pdev = pdev;
	spin_lock_init(&cs5535_gpio_chip.lock);

	dev_info(&pdev->dev, "allocated PCI BAR #%d: base 0x%llx\n", GPIO_BAR,
			(unsigned long long) cs5535_gpio_chip.base);

	/* mask out reserved pins */
	mask &= 0x1F7FFFFF;

	/* do not allow pin 28, Power Button, as there's special handling
	 * in the PMC needed. (note 12, p. 48) */
	mask &= ~(1 << 28);

	if (mask_orig != mask)
		dev_info(&pdev->dev, "mask changed from 0x%08lX to 0x%08lX\n",
				mask_orig, mask);

	/* finally, register with the generic GPIO API */
	err = gpiochip_add(&cs5535_gpio_chip.chip);
	if (err)
		goto release_region;

	dev_info(&pdev->dev, DRV_NAME ": GPIO support successfully loaded.\n");
	return 0;

release_region:
	pci_release_region(pdev, GPIO_BAR);
done:
	return err;
}

static void __exit cs5535_gpio_remove(struct pci_dev *pdev)
{
	int err;

	err = gpiochip_remove(&cs5535_gpio_chip.chip);
	if (err) {
		/* uhh? */
		dev_err(&pdev->dev, "unable to remove gpio_chip?\n");
	}
	pci_release_region(pdev, GPIO_BAR);
}

static struct pci_device_id cs5535_gpio_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_CS5535_ISA) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, cs5535_gpio_pci_tbl);

/*
 * We can't use the standard PCI driver registration stuff here, since
 * that allows only one driver to bind to each PCI device (and we want
 * multiple drivers to be able to bind to the device).  Instead, manually
 * scan for the PCI device, request a single region, and keep track of the
 * devices that we're using.
 */

static int __init cs5535_gpio_scan_pci(void)
{
	struct pci_dev *pdev;
	int err = -ENODEV;
	int i;

	for (i = 0; i < ARRAY_SIZE(cs5535_gpio_pci_tbl); i++) {
		pdev = pci_get_device(cs5535_gpio_pci_tbl[i].vendor,
				cs5535_gpio_pci_tbl[i].device, NULL);
		if (pdev) {
			err = cs5535_gpio_probe(pdev, &cs5535_gpio_pci_tbl[i]);
			if (err)
				pci_dev_put(pdev);

			/* we only support a single CS5535/6 southbridge */
			break;
		}
	}

	return err;
}

static void __exit cs5535_gpio_free_pci(void)
{
	cs5535_gpio_remove(cs5535_gpio_chip.pdev);
	pci_dev_put(cs5535_gpio_chip.pdev);
}

static int __init cs5535_gpio_init(void)
{
	return cs5535_gpio_scan_pci();
}

static void __exit cs5535_gpio_exit(void)
{
	cs5535_gpio_free_pci();
}

module_init(cs5535_gpio_init);
module_exit(cs5535_gpio_exit);

MODULE_AUTHOR("Andres Salomon <dilinger@collabora.co.uk>");
MODULE_DESCRIPTION("AMD CS5535/CS5536 GPIO driver");
MODULE_LICENSE("GPL");
