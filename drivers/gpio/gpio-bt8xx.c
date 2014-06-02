/*

    bt8xx GPIO abuser

    Copyright (C) 2008 Michael Buesch <m@bues.ch>

    Please do _only_ contact the people listed _above_ with issues related to this driver.
    All the other people listed below are not related to this driver. Their names
    are only here, because this driver is derived from the bt848 driver.


    Derived from the bt848 driver:

    Copyright (C) 1996,97,98 Ralph  Metzler
			   & Marcus Metzler
    (c) 1999-2002 Gerd Knorr

    some v4l2 code lines are taken from Justin's bttv2 driver which is
    (c) 2000 Justin Schoeman

    V4L1 removal from:
    (c) 2005-2006 Nickolay V. Shmyrev

    Fixes to be fully V4L2 compliant by
    (c) 2006 Mauro Carvalho Chehab

    Cropping and overscan support
    Copyright (C) 2005, 2006 Michael H. Schimek
    Sponsored by OPQ Systems AB

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/slab.h>

/* Steal the hardware definitions from the bttv driver. */
#include "../media/pci/bt8xx/bt848.h"


#define BT8XXGPIO_NR_GPIOS		24 /* We have 24 GPIO pins */


struct bt8xxgpio {
	spinlock_t lock;

	void __iomem *mmio;
	struct pci_dev *pdev;
	struct gpio_chip gpio;

#ifdef CONFIG_PM
	u32 saved_outen;
	u32 saved_data;
#endif
};

#define bgwrite(dat, adr)	writel((dat), bg->mmio+(adr))
#define bgread(adr)		readl(bg->mmio+(adr))


static int modparam_gpiobase = -1/* dynamic */;
module_param_named(gpiobase, modparam_gpiobase, int, 0444);
MODULE_PARM_DESC(gpiobase, "The GPIO number base. -1 means dynamic, which is the default.");


static int bt8xxgpio_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	struct bt8xxgpio *bg = container_of(gpio, struct bt8xxgpio, gpio);
	unsigned long flags;
	u32 outen, data;

	spin_lock_irqsave(&bg->lock, flags);

	data = bgread(BT848_GPIO_DATA);
	data &= ~(1 << nr);
	bgwrite(data, BT848_GPIO_DATA);

	outen = bgread(BT848_GPIO_OUT_EN);
	outen &= ~(1 << nr);
	bgwrite(outen, BT848_GPIO_OUT_EN);

	spin_unlock_irqrestore(&bg->lock, flags);

	return 0;
}

static int bt8xxgpio_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct bt8xxgpio *bg = container_of(gpio, struct bt8xxgpio, gpio);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&bg->lock, flags);
	val = bgread(BT848_GPIO_DATA);
	spin_unlock_irqrestore(&bg->lock, flags);

	return !!(val & (1 << nr));
}

static int bt8xxgpio_gpio_direction_output(struct gpio_chip *gpio,
					unsigned nr, int val)
{
	struct bt8xxgpio *bg = container_of(gpio, struct bt8xxgpio, gpio);
	unsigned long flags;
	u32 outen, data;

	spin_lock_irqsave(&bg->lock, flags);

	outen = bgread(BT848_GPIO_OUT_EN);
	outen |= (1 << nr);
	bgwrite(outen, BT848_GPIO_OUT_EN);

	data = bgread(BT848_GPIO_DATA);
	if (val)
		data |= (1 << nr);
	else
		data &= ~(1 << nr);
	bgwrite(data, BT848_GPIO_DATA);

	spin_unlock_irqrestore(&bg->lock, flags);

	return 0;
}

static void bt8xxgpio_gpio_set(struct gpio_chip *gpio,
			    unsigned nr, int val)
{
	struct bt8xxgpio *bg = container_of(gpio, struct bt8xxgpio, gpio);
	unsigned long flags;
	u32 data;

	spin_lock_irqsave(&bg->lock, flags);

	data = bgread(BT848_GPIO_DATA);
	if (val)
		data |= (1 << nr);
	else
		data &= ~(1 << nr);
	bgwrite(data, BT848_GPIO_DATA);

	spin_unlock_irqrestore(&bg->lock, flags);
}

static void bt8xxgpio_gpio_setup(struct bt8xxgpio *bg)
{
	struct gpio_chip *c = &bg->gpio;

	c->label = dev_name(&bg->pdev->dev);
	c->owner = THIS_MODULE;
	c->direction_input = bt8xxgpio_gpio_direction_input;
	c->get = bt8xxgpio_gpio_get;
	c->direction_output = bt8xxgpio_gpio_direction_output;
	c->set = bt8xxgpio_gpio_set;
	c->dbg_show = NULL;
	c->base = modparam_gpiobase;
	c->ngpio = BT8XXGPIO_NR_GPIOS;
	c->can_sleep = false;
}

static int bt8xxgpio_probe(struct pci_dev *dev,
			const struct pci_device_id *pci_id)
{
	struct bt8xxgpio *bg;
	int err;

	bg = devm_kzalloc(&dev->dev, sizeof(struct bt8xxgpio), GFP_KERNEL);
	if (!bg)
		return -ENOMEM;

	bg->pdev = dev;
	spin_lock_init(&bg->lock);

	err = pci_enable_device(dev);
	if (err) {
		printk(KERN_ERR "bt8xxgpio: Can't enable device.\n");
		return err;
	}
	if (!devm_request_mem_region(&dev->dev, pci_resource_start(dev, 0),
				pci_resource_len(dev, 0),
				"bt8xxgpio")) {
		printk(KERN_WARNING "bt8xxgpio: Can't request iomem (0x%llx).\n",
		       (unsigned long long)pci_resource_start(dev, 0));
		err = -EBUSY;
		goto err_disable;
	}
	pci_set_master(dev);
	pci_set_drvdata(dev, bg);

	bg->mmio = devm_ioremap(&dev->dev, pci_resource_start(dev, 0), 0x1000);
	if (!bg->mmio) {
		printk(KERN_ERR "bt8xxgpio: ioremap() failed\n");
		err = -EIO;
		goto err_disable;
	}

	/* Disable interrupts */
	bgwrite(0, BT848_INT_MASK);

	/* gpio init */
	bgwrite(0, BT848_GPIO_DMA_CTL);
	bgwrite(0, BT848_GPIO_REG_INP);
	bgwrite(0, BT848_GPIO_OUT_EN);

	bt8xxgpio_gpio_setup(bg);
	err = gpiochip_add(&bg->gpio);
	if (err) {
		printk(KERN_ERR "bt8xxgpio: Failed to register GPIOs\n");
		goto err_disable;
	}

	return 0;

err_disable:
	pci_disable_device(dev);

	return err;
}

static void bt8xxgpio_remove(struct pci_dev *pdev)
{
	struct bt8xxgpio *bg = pci_get_drvdata(pdev);

	gpiochip_remove(&bg->gpio);

	bgwrite(0, BT848_INT_MASK);
	bgwrite(~0x0, BT848_INT_STAT);
	bgwrite(0x0, BT848_GPIO_OUT_EN);

	iounmap(bg->mmio);
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
static int bt8xxgpio_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct bt8xxgpio *bg = pci_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&bg->lock, flags);

	bg->saved_outen = bgread(BT848_GPIO_OUT_EN);
	bg->saved_data = bgread(BT848_GPIO_DATA);

	bgwrite(0, BT848_INT_MASK);
	bgwrite(~0x0, BT848_INT_STAT);
	bgwrite(0x0, BT848_GPIO_OUT_EN);

	spin_unlock_irqrestore(&bg->lock, flags);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int bt8xxgpio_resume(struct pci_dev *pdev)
{
	struct bt8xxgpio *bg = pci_get_drvdata(pdev);
	unsigned long flags;
	int err;

	pci_set_power_state(pdev, PCI_D0);
	err = pci_enable_device(pdev);
	if (err)
		return err;
	pci_restore_state(pdev);

	spin_lock_irqsave(&bg->lock, flags);

	bgwrite(0, BT848_INT_MASK);
	bgwrite(0, BT848_GPIO_DMA_CTL);
	bgwrite(0, BT848_GPIO_REG_INP);
	bgwrite(bg->saved_outen, BT848_GPIO_OUT_EN);
	bgwrite(bg->saved_data & bg->saved_outen,
		BT848_GPIO_DATA);

	spin_unlock_irqrestore(&bg->lock, flags);

	return 0;
}
#else
#define bt8xxgpio_suspend NULL
#define bt8xxgpio_resume NULL
#endif /* CONFIG_PM */

static const struct pci_device_id bt8xxgpio_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT848) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT849) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT878) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT879) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, bt8xxgpio_pci_tbl);

static struct pci_driver bt8xxgpio_pci_driver = {
	.name		= "bt8xxgpio",
	.id_table	= bt8xxgpio_pci_tbl,
	.probe		= bt8xxgpio_probe,
	.remove		= bt8xxgpio_remove,
	.suspend	= bt8xxgpio_suspend,
	.resume		= bt8xxgpio_resume,
};

module_pci_driver(bt8xxgpio_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Buesch");
MODULE_DESCRIPTION("Abuse a BT8xx framegrabber card as generic GPIO card");
