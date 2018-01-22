// SPDX-License-Identifier: GPL-2.0
/*
 * 8250_moxa.c - MOXA Smartio/Industio MUE multiport serial driver.
 *
 * Author: Mathieu OTHACEHE <m.othacehe@gmail.com>
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "8250.h"

#define	PCI_DEVICE_ID_MOXA_CP102E	0x1024
#define	PCI_DEVICE_ID_MOXA_CP102EL	0x1025
#define	PCI_DEVICE_ID_MOXA_CP104EL_A	0x1045
#define	PCI_DEVICE_ID_MOXA_CP114EL	0x1144
#define	PCI_DEVICE_ID_MOXA_CP116E_A_A	0x1160
#define	PCI_DEVICE_ID_MOXA_CP116E_A_B	0x1161
#define	PCI_DEVICE_ID_MOXA_CP118EL_A	0x1182
#define	PCI_DEVICE_ID_MOXA_CP118E_A_I	0x1183
#define	PCI_DEVICE_ID_MOXA_CP132EL	0x1322
#define	PCI_DEVICE_ID_MOXA_CP134EL_A	0x1342
#define	PCI_DEVICE_ID_MOXA_CP138E_A	0x1381
#define	PCI_DEVICE_ID_MOXA_CP168EL_A	0x1683

#define MOXA_BASE_BAUD 921600
#define MOXA_UART_OFFSET 0x200
#define MOXA_BASE_BAR 1

struct moxa8250_board {
	unsigned int num_ports;
	int line[0];
};

enum {
	moxa8250_2p = 0,
	moxa8250_4p,
	moxa8250_8p
};

static struct moxa8250_board moxa8250_boards[] = {
	[moxa8250_2p] = { .num_ports = 2},
	[moxa8250_4p] = { .num_ports = 4},
	[moxa8250_8p] = { .num_ports = 8},
};

static int moxa8250_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct uart_8250_port uart;
	struct moxa8250_board *brd;
	void __iomem *ioaddr;
	resource_size_t baseaddr;
	unsigned int i, nr_ports;
	unsigned int offset;
	int ret;

	brd = &moxa8250_boards[id->driver_data];
	nr_ports = brd->num_ports;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	brd = devm_kzalloc(&pdev->dev, sizeof(struct moxa8250_board) +
			   sizeof(unsigned int) * nr_ports, GFP_KERNEL);
	if (!brd)
		return -ENOMEM;
	brd->num_ports = nr_ports;

	memset(&uart, 0, sizeof(struct uart_8250_port));

	uart.port.dev = &pdev->dev;
	uart.port.irq = pdev->irq;
	uart.port.uartclk = MOXA_BASE_BAUD * 16;
	uart.port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;

	baseaddr = pci_resource_start(pdev, MOXA_BASE_BAR);
	ioaddr = pcim_iomap(pdev, MOXA_BASE_BAR, 0);
	if (!ioaddr)
		return -ENOMEM;

	for (i = 0; i < nr_ports; i++) {

		/*
		 * MOXA Smartio MUE boards with 4 ports have
		 * a different offset for port #3
		 */
		if (nr_ports == 4 && i == 3)
			offset = 7 * MOXA_UART_OFFSET;
		else
			offset = i * MOXA_UART_OFFSET;

		uart.port.iotype = UPIO_MEM;
		uart.port.iobase = 0;
		uart.port.mapbase = baseaddr + offset;
		uart.port.membase = ioaddr + offset;
		uart.port.regshift = 0;

		dev_dbg(&pdev->dev, "Setup PCI port: port %lx, irq %d, type %d\n",
			uart.port.iobase, uart.port.irq, uart.port.iotype);

		brd->line[i] = serial8250_register_8250_port(&uart);
		if (brd->line[i] < 0) {
			dev_err(&pdev->dev,
				"Couldn't register serial port %lx, irq %d, type %d, error %d\n",
				uart.port.iobase, uart.port.irq,
				uart.port.iotype, brd->line[i]);
			break;
		}
	}

	pci_set_drvdata(pdev, brd);
	return 0;
}

static void moxa8250_remove(struct pci_dev *pdev)
{
	struct moxa8250_board *brd = pci_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < brd->num_ports; i++)
		serial8250_unregister_port(brd->line[i]);
}

#define MOXA_DEVICE(id, data) { PCI_VDEVICE(MOXA, id), (kernel_ulong_t)data }

static const struct pci_device_id pci_ids[] = {
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP102E, moxa8250_2p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP102EL, moxa8250_2p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP104EL_A, moxa8250_4p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP114EL, moxa8250_4p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP116E_A_A, moxa8250_8p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP116E_A_B, moxa8250_8p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP118EL_A, moxa8250_8p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP118E_A_I, moxa8250_8p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP132EL, moxa8250_2p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP134EL_A, moxa8250_4p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP138E_A, moxa8250_8p),
	MOXA_DEVICE(PCI_DEVICE_ID_MOXA_CP168EL_A, moxa8250_8p),
	{0}
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver moxa8250_pci_driver = {
	.name           = "8250_moxa",
	.id_table       = pci_ids,
	.probe          = moxa8250_probe,
	.remove         = moxa8250_remove,
};

module_pci_driver(moxa8250_pci_driver);

MODULE_AUTHOR("Mathieu OTHACEHE");
MODULE_DESCRIPTION("MOXA SmartIO MUE driver");
MODULE_LICENSE("GPL v2");
