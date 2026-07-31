// SPDX-License-Identifier: GPL-2.0
/*
 * Moxa PCIe multiport serial device driver
 *
 * Copyright (C) 2025 Moxa Inc. (support@moxa.com)
 * Author: Crescent Hsieh <crescentcy.hsieh@moxa.com>
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <linux/8250_pci.h>
#include <linux/serial_8250.h>

#include "8250.h"

#define PCI_DEVICE_ID_MOXA_CP102E	0x1024
#define PCI_DEVICE_ID_MOXA_CP102EL	0x1025
#define PCI_DEVICE_ID_MOXA_CP102N	0x1027
#define PCI_DEVICE_ID_MOXA_CP104EL_A	0x1045
#define PCI_DEVICE_ID_MOXA_CP104N	0x1046
#define PCI_DEVICE_ID_MOXA_CP112N	0x1121
#define PCI_DEVICE_ID_MOXA_CP114EL	0x1144
#define PCI_DEVICE_ID_MOXA_CP114N	0x1145
#define PCI_DEVICE_ID_MOXA_CP116E_A_A	0x1160
#define PCI_DEVICE_ID_MOXA_CP116E_A_B	0x1161
#define PCI_DEVICE_ID_MOXA_CP118EL_A	0x1182
#define PCI_DEVICE_ID_MOXA_CP118E_A_I	0x1183
#define PCI_DEVICE_ID_MOXA_CP132EL	0x1322
#define PCI_DEVICE_ID_MOXA_CP132N	0x1323
#define PCI_DEVICE_ID_MOXA_CP134EL_A	0x1342
#define PCI_DEVICE_ID_MOXA_CP134N	0x1343
#define PCI_DEVICE_ID_MOXA_CP138E_A	0x1381
#define PCI_DEVICE_ID_MOXA_CP168EL_A	0x1683

/* Bits in PCI device ID encoding board capabilities */
#define MOXA_DEV_ID_IFACE_MASK	GENMASK(11, 8)	/* Supported serial interface */
#define MOXA_DEV_ID_NPORTS_MASK	GENMASK(7, 4)	/* Number of UART ports */

/* UART */
#define MOXA_PUART_BASE_BAUD		921600
#define MOXA_PUART_OFFSET		0x200

#define MOXA_GPIO_DIRECTION	0x09
#define MOXA_GPIO_OUTPUT	0x0A

#define MOXA_GPIO_PIN2	BIT(2)

#define MOXA_UIR_OFFSET		0x04
#define MOXA_UIR_RS232		0x00
#define MOXA_UIR_RS422		0x01
#define MOXA_UIR_RS485_4W	0x0B
#define MOXA_UIR_RS485_2W	0x0F

#define MOXA_EVEN_RS_MASK	GENMASK(3, 0)
#define MOXA_ODD_RS_MASK	GENMASK(7, 4)

struct mxpcie8250 {
	unsigned int supp_rs;
	unsigned int num_ports;
	void __iomem *bar1_base; /* UART registers (MMIO) */
	void __iomem *bar2_base; /* UIR / GPIO / CPLD (IO) */
	int line[] __counted_by(num_ports);
};

enum {
	MOXA_SUPP_RS232 = BIT(0),
	MOXA_SUPP_RS422 = BIT(1),
	MOXA_SUPP_RS485 = BIT(2),
};

static bool mxpcie8250_is_mini_pcie(unsigned short device)
{
	if (device == PCI_DEVICE_ID_MOXA_CP102N ||
	    device == PCI_DEVICE_ID_MOXA_CP104N ||
	    device == PCI_DEVICE_ID_MOXA_CP112N ||
	    device == PCI_DEVICE_ID_MOXA_CP114N ||
	    device == PCI_DEVICE_ID_MOXA_CP132N ||
	    device == PCI_DEVICE_ID_MOXA_CP134N)
		return true;

	return false;
}

static unsigned int mxpcie8250_get_supp_rs(unsigned short device)
{
	switch (device & MOXA_DEV_ID_IFACE_MASK) {
	case 0x0000:
	case 0x0600:
		return MOXA_SUPP_RS232;
	case 0x0100:
		return MOXA_SUPP_RS232 | MOXA_SUPP_RS422 | MOXA_SUPP_RS485;
	case 0x0300:
		return MOXA_SUPP_RS422 | MOXA_SUPP_RS485;
	default:
		return 0;
	}
}

static unsigned short mxpcie8250_get_nports(unsigned short device)
{
	switch (device) {
	case PCI_DEVICE_ID_MOXA_CP116E_A_A:
	case PCI_DEVICE_ID_MOXA_CP116E_A_B:
		return 8;
	default:
		return FIELD_GET(MOXA_DEV_ID_NPORTS_MASK, device);
	}
}

static void mxpcie8250_set_interface(struct mxpcie8250 *priv,
				     unsigned int port_idx,
				     u8 mode)
{
	void __iomem *uir_addr = priv->bar2_base + MOXA_UIR_OFFSET + port_idx / 2;
	u8 cval;

	cval = ioread8(uir_addr);

	if (port_idx % 2)
		FIELD_MODIFY(MOXA_ODD_RS_MASK, &cval, mode);
	else
		FIELD_MODIFY(MOXA_EVEN_RS_MASK, &cval, mode);

	iowrite8(cval, uir_addr);
}

static void mxpcie8250_init_board(struct pci_dev *pdev, struct mxpcie8250 *priv)
{
	void __iomem *bar2_base = priv->bar2_base;
	unsigned short device = pdev->device;
	u8 cval;

	/* Initial terminator */
	if (device == PCI_DEVICE_ID_MOXA_CP114EL ||
	    device == PCI_DEVICE_ID_MOXA_CP118EL_A) {
		iowrite8(0xff, bar2_base + MOXA_GPIO_DIRECTION);
		iowrite8(0x00, bar2_base + MOXA_GPIO_OUTPUT);
	}
	/*
	 * Enable hardware buffer to prevent break signal output when system boots up.
	 * This hardware buffer is only supported on Mini PCIe series.
	 */
	if (mxpcie8250_is_mini_pcie(device)) {
		/* Set GPIO direction */
		cval = ioread8(bar2_base + MOXA_GPIO_DIRECTION);
		cval |= MOXA_GPIO_PIN2;
		iowrite8(cval, bar2_base + MOXA_GPIO_DIRECTION);
		/* Enable low GPIO */
		cval = ioread8(bar2_base + MOXA_GPIO_OUTPUT);
		cval &= ~MOXA_GPIO_PIN2;
		iowrite8(cval, bar2_base + MOXA_GPIO_OUTPUT);
	}
}

static void mxpcie8250_setup_port(struct pci_dev *pdev,
				  struct mxpcie8250 *priv,
				  struct uart_8250_port *up,
				  int idx)
{
	unsigned short device = pdev->device;
	int offset = idx * MOXA_PUART_OFFSET;
	u8 init_mode = MOXA_UIR_RS232;

	if (!(priv->supp_rs & MOXA_SUPP_RS232))
		init_mode = MOXA_UIR_RS422;

	mxpcie8250_set_interface(priv, idx, init_mode);

	if (idx == 3 &&
	    (device == PCI_DEVICE_ID_MOXA_CP104EL_A ||
	     device == PCI_DEVICE_ID_MOXA_CP114EL   ||
	     device == PCI_DEVICE_ID_MOXA_CP134EL_A))
		offset = 7 * MOXA_PUART_OFFSET;

	up->port.mapbase = pci_resource_start(pdev, FL_BASE1) + offset;
	up->port.membase = pcim_iomap_table(pdev)[FL_BASE1] + offset;
}

static int mxpcie8250_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct uart_8250_port up = {};
	struct mxpcie8250 *priv;
	unsigned short device = pdev->device;
	unsigned int num_ports;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	num_ports = mxpcie8250_get_nports(device);

	priv = devm_kzalloc(dev, struct_size(priv, line, num_ports), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num_ports = num_ports;
	priv->supp_rs = mxpcie8250_get_supp_rs(device);

	priv->bar1_base = pcim_iomap(pdev, FL_BASE1, 0);
	if (!priv->bar1_base)
		return -ENOMEM;

	priv->bar2_base = pcim_iomap(pdev, FL_BASE2, 0);
	if (!priv->bar2_base)
		return -ENOMEM;

	mxpcie8250_init_board(pdev, priv);

	up.port.dev = dev;
	up.port.irq = pdev->irq;
	up.port.uartclk = MOXA_PUART_BASE_BAUD * 16;
	up.port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
	up.port.type = PORT_MUEX50;

	up.port.iotype = UPIO_MEM;
	up.port.iobase = 0;
	up.port.regshift = 0;

	for (unsigned int i = 0; i < num_ports; i++) {
		mxpcie8250_setup_port(pdev, priv, &up, i);

		dev_dbg(dev, "Setup PCI port: port %lx, irq %d, type %d\n",
			up.port.iobase, up.port.irq, up.port.iotype);

		priv->line[i] = serial8250_register_8250_port(&up);
		if (priv->line[i] < 0) {
			dev_err(dev,
				"Couldn't register serial port %lx, irq %d, type %d, error %d\n",
				up.port.iobase, up.port.irq,
				up.port.iotype, priv->line[i]);
			break;
		}
	}
	pci_set_drvdata(pdev, priv);

	return 0;
}

static void mxpcie8250_remove(struct pci_dev *pdev)
{
	struct mxpcie8250 *priv = pci_get_drvdata(pdev);

	for (unsigned int i = 0; i < priv->num_ports; i++)
		serial8250_unregister_port(priv->line[i]);
}

static const struct pci_device_id mxpcie8250_pci_ids[] = {
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102E) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102EL) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP102N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP104EL_A) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP104N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP112N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP114EL) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP114N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP116E_A_A) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP116E_A_B) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP118EL_A) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP118E_A_I) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP132EL) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP132N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP134EL_A) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP134N) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP138E_A) },
	{ PCI_VDEVICE(MOXA, PCI_DEVICE_ID_MOXA_CP168EL_A) },
	{ }
};
MODULE_DEVICE_TABLE(pci, mxpcie8250_pci_ids);

static struct pci_driver mxpcie8250_pci_driver = {
	.name		= "8250_mxpcie",
	.id_table	= mxpcie8250_pci_ids,
	.probe		= mxpcie8250_probe,
	.remove		= mxpcie8250_remove,
};
module_pci_driver(mxpcie8250_pci_driver);

MODULE_AUTHOR("Moxa Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Moxa PCIe Multiport Serial Device Driver");
