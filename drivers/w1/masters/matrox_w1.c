/*
 *	matrox_w1.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>
#include <linux/atomic.h>
#include <asm/io.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>

#include "../w1.h"
#include "../w1_int.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Driver for transport(Dallas 1-wire protocol) over VGA DDC(matrox gpio).");

static struct pci_device_id matrox_w1_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MATROX, PCI_DEVICE_ID_MATROX_G400) },
	{ },
};
MODULE_DEVICE_TABLE(pci, matrox_w1_tbl);

static int matrox_w1_probe(struct pci_dev *, const struct pci_device_id *);
static void matrox_w1_remove(struct pci_dev *);

static struct pci_driver matrox_w1_pci_driver = {
	.name = "matrox_w1",
	.id_table = matrox_w1_tbl,
	.probe = matrox_w1_probe,
	.remove = matrox_w1_remove,
};

/*
 * Matrox G400 DDC registers.
 */

#define MATROX_G400_DDC_CLK		(1<<4)
#define MATROX_G400_DDC_DATA		(1<<1)

#define MATROX_BASE			0x3C00
#define MATROX_STATUS			0x1e14

#define MATROX_PORT_INDEX_OFFSET	0x00
#define MATROX_PORT_DATA_OFFSET		0x0A

#define MATROX_GET_CONTROL		0x2A
#define MATROX_GET_DATA			0x2B
#define MATROX_CURSOR_CTL		0x06

struct matrox_device
{
	void __iomem *base_addr;
	void __iomem *port_index;
	void __iomem *port_data;
	u8 data_mask;

	unsigned long phys_addr;
	void __iomem *virt_addr;
	unsigned long found;

	struct w1_bus_master *bus_master;
};

static u8 matrox_w1_read_ddc_bit(void *);
static void matrox_w1_write_ddc_bit(void *, u8);

/*
 * These functions read and write DDC Data bit.
 *
 * Using tristate pins, since i can't find any open-drain pin in whole motherboard.
 * Unfortunately we can't connect to Intel's 82801xx IO controller
 * since we don't know motherboard schema, which has pretty unused(may be not) GPIO.
 *
 * I've heard that PIIX also has open drain pin.
 *
 * Port mapping.
 */
static __inline__ u8 matrox_w1_read_reg(struct matrox_device *dev, u8 reg)
{
	u8 ret;

	writeb(reg, dev->port_index);
	ret = readb(dev->port_data);
	barrier();

	return ret;
}

static __inline__ void matrox_w1_write_reg(struct matrox_device *dev, u8 reg, u8 val)
{
	writeb(reg, dev->port_index);
	writeb(val, dev->port_data);
	wmb();
}

static void matrox_w1_write_ddc_bit(void *data, u8 bit)
{
	u8 ret;
	struct matrox_device *dev = data;

	if (bit)
		bit = 0;
	else
		bit = dev->data_mask;

	ret = matrox_w1_read_reg(dev, MATROX_GET_CONTROL);
	matrox_w1_write_reg(dev, MATROX_GET_CONTROL, ((ret & ~dev->data_mask) | bit));
	matrox_w1_write_reg(dev, MATROX_GET_DATA, 0x00);
}

static u8 matrox_w1_read_ddc_bit(void *data)
{
	u8 ret;
	struct matrox_device *dev = data;

	ret = matrox_w1_read_reg(dev, MATROX_GET_DATA);

	return ret;
}

static void matrox_w1_hw_init(struct matrox_device *dev)
{
	matrox_w1_write_reg(dev, MATROX_GET_DATA, 0xFF);
	matrox_w1_write_reg(dev, MATROX_GET_CONTROL, 0x00);
}

static int matrox_w1_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct matrox_device *dev;
	int err;

	if (pdev->vendor != PCI_VENDOR_ID_MATROX || pdev->device != PCI_DEVICE_ID_MATROX_G400)
		return -ENODEV;

	dev = kzalloc(sizeof(struct matrox_device) +
		       sizeof(struct w1_bus_master), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev,
			"%s: Failed to create new matrox_device object.\n",
			__func__);
		return -ENOMEM;
	}


	dev->bus_master = (struct w1_bus_master *)(dev + 1);

	/*
	 * True for G400, for some other we need resource 0, see drivers/video/matrox/matroxfb_base.c
	 */

	dev->phys_addr = pci_resource_start(pdev, 1);

	dev->virt_addr = ioremap_nocache(dev->phys_addr, 16384);
	if (!dev->virt_addr) {
		dev_err(&pdev->dev, "%s: failed to ioremap(0x%lx, %d).\n",
			__func__, dev->phys_addr, 16384);
		err = -EIO;
		goto err_out_free_device;
	}

	dev->base_addr = dev->virt_addr + MATROX_BASE;
	dev->port_index = dev->base_addr + MATROX_PORT_INDEX_OFFSET;
	dev->port_data = dev->base_addr + MATROX_PORT_DATA_OFFSET;
	dev->data_mask = (MATROX_G400_DDC_DATA);

	matrox_w1_hw_init(dev);

	dev->bus_master->data = dev;
	dev->bus_master->read_bit = &matrox_w1_read_ddc_bit;
	dev->bus_master->write_bit = &matrox_w1_write_ddc_bit;

	err = w1_add_master_device(dev->bus_master);
	if (err)
		goto err_out_free_device;

	pci_set_drvdata(pdev, dev);

	dev->found = 1;

	dev_info(&pdev->dev, "Matrox G400 GPIO transport layer for 1-wire.\n");

	return 0;

err_out_free_device:
	if (dev->virt_addr)
		iounmap(dev->virt_addr);
	kfree(dev);

	return err;
}

static void matrox_w1_remove(struct pci_dev *pdev)
{
	struct matrox_device *dev = pci_get_drvdata(pdev);

	if (dev->found) {
		w1_remove_master_device(dev->bus_master);
		iounmap(dev->virt_addr);
	}
	kfree(dev);
}
module_pci_driver(matrox_w1_pci_driver);
