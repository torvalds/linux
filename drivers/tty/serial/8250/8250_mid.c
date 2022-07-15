// SPDX-License-Identifier: GPL-2.0
/*
 * 8250_mid.c - Driver for UART on Intel Penwell and various other Intel SOCs
 *
 * Copyright (C) 2015 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/rational.h>

#include <linux/dma/hsu.h>
#include <linux/8250_pci.h>

#include "8250.h"

#define PCI_DEVICE_ID_INTEL_PNW_UART1	0x081b
#define PCI_DEVICE_ID_INTEL_PNW_UART2	0x081c
#define PCI_DEVICE_ID_INTEL_PNW_UART3	0x081d
#define PCI_DEVICE_ID_INTEL_TNG_UART	0x1191
#define PCI_DEVICE_ID_INTEL_CDF_UART	0x18d8
#define PCI_DEVICE_ID_INTEL_DNV_UART	0x19d8

/* Intel MID Specific registers */
#define INTEL_MID_UART_FISR		0x08
#define INTEL_MID_UART_PS		0x30
#define INTEL_MID_UART_MUL		0x34
#define INTEL_MID_UART_DIV		0x38

struct mid8250;

struct mid8250_board {
	unsigned int flags;
	unsigned long freq;
	unsigned int base_baud;
	int (*setup)(struct mid8250 *, struct uart_port *p);
	void (*exit)(struct mid8250 *);
};

struct mid8250 {
	int line;
	int dma_index;
	struct pci_dev *dma_dev;
	struct uart_8250_dma dma;
	struct mid8250_board *board;
	struct hsu_dma_chip dma_chip;
};

/*****************************************************************************/

static int pnw_setup(struct mid8250 *mid, struct uart_port *p)
{
	struct pci_dev *pdev = to_pci_dev(p->dev);

	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_PNW_UART1:
		mid->dma_index = 0;
		break;
	case PCI_DEVICE_ID_INTEL_PNW_UART2:
		mid->dma_index = 1;
		break;
	case PCI_DEVICE_ID_INTEL_PNW_UART3:
		mid->dma_index = 2;
		break;
	default:
		return -EINVAL;
	}

	mid->dma_dev = pci_get_slot(pdev->bus,
				    PCI_DEVFN(PCI_SLOT(pdev->devfn), 3));
	return 0;
}

static void pnw_exit(struct mid8250 *mid)
{
	pci_dev_put(mid->dma_dev);
}

static int tng_handle_irq(struct uart_port *p)
{
	struct mid8250 *mid = p->private_data;
	struct uart_8250_port *up = up_to_u8250p(p);
	struct hsu_dma_chip *chip;
	u32 status;
	int ret = 0;
	int err;

	chip = pci_get_drvdata(mid->dma_dev);

	/* Rx DMA */
	err = hsu_dma_get_status(chip, mid->dma_index * 2 + 1, &status);
	if (err > 0) {
		serial8250_rx_dma_flush(up);
		ret |= 1;
	} else if (err == 0)
		ret |= hsu_dma_do_irq(chip, mid->dma_index * 2 + 1, status);

	/* Tx DMA */
	err = hsu_dma_get_status(chip, mid->dma_index * 2, &status);
	if (err > 0)
		ret |= 1;
	else if (err == 0)
		ret |= hsu_dma_do_irq(chip, mid->dma_index * 2, status);

	/* UART */
	ret |= serial8250_handle_irq(p, serial_port_in(p, UART_IIR));
	return IRQ_RETVAL(ret);
}

static int tng_setup(struct mid8250 *mid, struct uart_port *p)
{
	struct pci_dev *pdev = to_pci_dev(p->dev);
	int index = PCI_FUNC(pdev->devfn);

	/*
	 * Device 0000:00:04.0 is not a real HSU port. It provides a global
	 * register set for all HSU ports, although it has the same PCI ID.
	 * Skip it here.
	 */
	if (index-- == 0)
		return -ENODEV;

	mid->dma_index = index;
	mid->dma_dev = pci_get_slot(pdev->bus, PCI_DEVFN(5, 0));

	p->handle_irq = tng_handle_irq;
	return 0;
}

static void tng_exit(struct mid8250 *mid)
{
	pci_dev_put(mid->dma_dev);
}

static int dnv_handle_irq(struct uart_port *p)
{
	struct mid8250 *mid = p->private_data;
	struct uart_8250_port *up = up_to_u8250p(p);
	unsigned int fisr = serial_port_in(p, INTEL_MID_UART_FISR);
	u32 status;
	int ret = 0;
	int err;

	if (fisr & BIT(2)) {
		err = hsu_dma_get_status(&mid->dma_chip, 1, &status);
		if (err > 0) {
			serial8250_rx_dma_flush(up);
			ret |= 1;
		} else if (err == 0)
			ret |= hsu_dma_do_irq(&mid->dma_chip, 1, status);
	}
	if (fisr & BIT(1)) {
		err = hsu_dma_get_status(&mid->dma_chip, 0, &status);
		if (err > 0)
			ret |= 1;
		else if (err == 0)
			ret |= hsu_dma_do_irq(&mid->dma_chip, 0, status);
	}
	if (fisr & BIT(0))
		ret |= serial8250_handle_irq(p, serial_port_in(p, UART_IIR));
	return IRQ_RETVAL(ret);
}

#define DNV_DMA_CHAN_OFFSET 0x80

static int dnv_setup(struct mid8250 *mid, struct uart_port *p)
{
	struct hsu_dma_chip *chip = &mid->dma_chip;
	struct pci_dev *pdev = to_pci_dev(p->dev);
	unsigned int bar = FL_GET_BASE(mid->board->flags);
	int ret;

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	p->irq = pci_irq_vector(pdev, 0);

	chip->dev = &pdev->dev;
	chip->irq = pci_irq_vector(pdev, 0);
	chip->regs = p->membase;
	chip->length = pci_resource_len(pdev, bar);
	chip->offset = DNV_DMA_CHAN_OFFSET;

	/* Falling back to PIO mode if DMA probing fails */
	ret = hsu_dma_probe(chip);
	if (ret)
		return 0;

	mid->dma_dev = pdev;

	p->handle_irq = dnv_handle_irq;
	return 0;
}

static void dnv_exit(struct mid8250 *mid)
{
	if (!mid->dma_dev)
		return;
	hsu_dma_remove(&mid->dma_chip);
}

/*****************************************************************************/

static void mid8250_set_termios(struct uart_port *p,
				struct ktermios *termios,
				struct ktermios *old)
{
	unsigned int baud = tty_termios_baud_rate(termios);
	struct mid8250 *mid = p->private_data;
	unsigned short ps = 16;
	unsigned long fuart = baud * ps;
	unsigned long w = BIT(24) - 1;
	unsigned long mul, div;

	/* Gracefully handle the B0 case: fall back to B9600 */
	fuart = fuart ? fuart : 9600 * 16;

	if (mid->board->freq < fuart) {
		/* Find prescaler value that satisfies Fuart < Fref */
		if (mid->board->freq > baud)
			ps = mid->board->freq / baud;	/* baud rate too high */
		else
			ps = 1;				/* PLL case */
		fuart = baud * ps;
	} else {
		/* Get Fuart closer to Fref */
		fuart *= rounddown_pow_of_two(mid->board->freq / fuart);
	}

	rational_best_approximation(fuart, mid->board->freq, w, w, &mul, &div);
	p->uartclk = fuart * 16 / ps;		/* core uses ps = 16 always */

	writel(ps, p->membase + INTEL_MID_UART_PS);		/* set PS */
	writel(mul, p->membase + INTEL_MID_UART_MUL);		/* set MUL */
	writel(div, p->membase + INTEL_MID_UART_DIV);

	serial8250_do_set_termios(p, termios, old);
}

static bool mid8250_dma_filter(struct dma_chan *chan, void *param)
{
	struct hsu_dma_slave *s = param;

	if (s->dma_dev != chan->device->dev || s->chan_id != chan->chan_id)
		return false;

	chan->private = s;
	return true;
}

static int mid8250_dma_setup(struct mid8250 *mid, struct uart_8250_port *port)
{
	struct uart_8250_dma *dma = &mid->dma;
	struct device *dev = port->port.dev;
	struct hsu_dma_slave *rx_param;
	struct hsu_dma_slave *tx_param;

	if (!mid->dma_dev)
		return 0;

	rx_param = devm_kzalloc(dev, sizeof(*rx_param), GFP_KERNEL);
	if (!rx_param)
		return -ENOMEM;

	tx_param = devm_kzalloc(dev, sizeof(*tx_param), GFP_KERNEL);
	if (!tx_param)
		return -ENOMEM;

	rx_param->chan_id = mid->dma_index * 2 + 1;
	tx_param->chan_id = mid->dma_index * 2;

	dma->rxconf.src_maxburst = 64;
	dma->txconf.dst_maxburst = 64;

	rx_param->dma_dev = &mid->dma_dev->dev;
	tx_param->dma_dev = &mid->dma_dev->dev;

	dma->fn = mid8250_dma_filter;
	dma->rx_param = rx_param;
	dma->tx_param = tx_param;

	port->dma = dma;
	return 0;
}

static int mid8250_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct uart_8250_port uart;
	struct mid8250 *mid;
	unsigned int bar;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	mid = devm_kzalloc(&pdev->dev, sizeof(*mid), GFP_KERNEL);
	if (!mid)
		return -ENOMEM;

	mid->board = (struct mid8250_board *)id->driver_data;
	bar = FL_GET_BASE(mid->board->flags);

	memset(&uart, 0, sizeof(struct uart_8250_port));

	uart.port.dev = &pdev->dev;
	uart.port.irq = pdev->irq;
	uart.port.private_data = mid;
	uart.port.type = PORT_16750;
	uart.port.iotype = UPIO_MEM;
	uart.port.uartclk = mid->board->base_baud * 16;
	uart.port.flags = UPF_SHARE_IRQ | UPF_FIXED_PORT | UPF_FIXED_TYPE;
	uart.port.set_termios = mid8250_set_termios;

	uart.port.mapbase = pci_resource_start(pdev, bar);
	uart.port.membase = pcim_iomap(pdev, bar, 0);
	if (!uart.port.membase)
		return -ENOMEM;

	if (mid->board->setup) {
		ret = mid->board->setup(mid, &uart.port);
		if (ret)
			return ret;
	}

	ret = mid8250_dma_setup(mid, &uart);
	if (ret)
		goto err;

	ret = serial8250_register_8250_port(&uart);
	if (ret < 0)
		goto err;

	mid->line = ret;

	pci_set_drvdata(pdev, mid);
	return 0;

err:
	mid->board->exit(mid);
	return ret;
}

static void mid8250_remove(struct pci_dev *pdev)
{
	struct mid8250 *mid = pci_get_drvdata(pdev);

	serial8250_unregister_port(mid->line);

	mid->board->exit(mid);
}

static const struct mid8250_board pnw_board = {
	.flags = FL_BASE0,
	.freq = 50000000,
	.base_baud = 115200,
	.setup = pnw_setup,
	.exit = pnw_exit,
};

static const struct mid8250_board tng_board = {
	.flags = FL_BASE0,
	.freq = 38400000,
	.base_baud = 1843200,
	.setup = tng_setup,
	.exit = tng_exit,
};

static const struct mid8250_board dnv_board = {
	.flags = FL_BASE1,
	.freq = 133333333,
	.base_baud = 115200,
	.setup = dnv_setup,
	.exit = dnv_exit,
};

#define MID_DEVICE(id, board) { PCI_VDEVICE(INTEL, id), (kernel_ulong_t)&board }

static const struct pci_device_id pci_ids[] = {
	MID_DEVICE(PCI_DEVICE_ID_INTEL_PNW_UART1, pnw_board),
	MID_DEVICE(PCI_DEVICE_ID_INTEL_PNW_UART2, pnw_board),
	MID_DEVICE(PCI_DEVICE_ID_INTEL_PNW_UART3, pnw_board),
	MID_DEVICE(PCI_DEVICE_ID_INTEL_TNG_UART, tng_board),
	MID_DEVICE(PCI_DEVICE_ID_INTEL_CDF_UART, dnv_board),
	MID_DEVICE(PCI_DEVICE_ID_INTEL_DNV_UART, dnv_board),
	{ },
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver mid8250_pci_driver = {
	.name           = "8250_mid",
	.id_table       = pci_ids,
	.probe          = mid8250_probe,
	.remove         = mid8250_remove,
};

module_pci_driver(mid8250_pci_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel MID UART driver");
