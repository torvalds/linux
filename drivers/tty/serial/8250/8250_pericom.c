// SPDX-License-Identifier: GPL-2.0
/* Driver for Pericom UART */

#include <linux/bits.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/pci.h>

#include "8250.h"

#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_2SDB	0x1051
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_2S	0x1053
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM422_4	0x105a
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM485_4	0x105b
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SDB	0x105c
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_4S	0x105e
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM422_8	0x106a
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM485_8	0x106b
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_2DB	0x1091
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM232_2	0x1093
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_4	0x1098
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_4DB	0x1099
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM232_4	0x109b
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_8	0x10a9
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_2SMDB	0x10d1
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_2SM	0x10d3
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SM	0x10d9
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SMDB	0x10da
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_4SM	0x10dc
#define PCI_DEVICE_ID_ACCESSIO_PCIE_COM_8SM	0x10e9
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_1	0x1108
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM422_2	0x1110
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_2	0x1111
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM422_4	0x1118
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_4	0x1119
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_2S	0x1152
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_4S	0x115a
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM232_2	0x1190
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM232_2	0x1191
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM232_4	0x1198
#define PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM232_4	0x1199
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_2SM	0x11d0
#define PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_4SM	0x11d8

struct pericom8250 {
	void __iomem *virt;
	unsigned int nr;
	int line[];
};

static void pericom_do_set_divisor(struct uart_port *port, unsigned int baud,
				   unsigned int quot, unsigned int quot_frac)
{
	int scr;

	for (scr = 16; scr > 4; scr--) {
		unsigned int maxrate = port->uartclk / scr;
		unsigned int divisor = max(maxrate / baud, 1U);
		int delta = maxrate / divisor - baud;

		if (baud > maxrate + baud / 50)
			continue;

		if (delta > baud / 50)
			divisor++;

		if (divisor > 0xffff)
			continue;

		/* Update delta due to possible divisor change */
		delta = maxrate / divisor - baud;
		if (abs(delta) < baud / 50) {
			struct uart_8250_port *up = up_to_u8250p(port);
			int lcr = serial_port_in(port, UART_LCR);

			serial_port_out(port, UART_LCR, lcr | UART_LCR_DLAB);
			serial_dl_write(up, divisor);
			serial_port_out(port, 2, 16 - scr);
			serial_port_out(port, UART_LCR, lcr);
			return;
		}
	}
}

static int pericom8250_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	unsigned int nr, i, bar = 0, maxnr;
	struct pericom8250 *pericom;
	struct uart_8250_port uart;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	maxnr = pci_resource_len(pdev, bar) >> 3;

	if (pdev->vendor == PCI_VENDOR_ID_PERICOM)
		nr = pdev->device & 0x0f;
	else if (pdev->vendor == PCI_VENDOR_ID_ACCESSIO)
		nr = BIT(((pdev->device & 0x38) >> 3) - 1);
	else
		nr = 1;

	pericom = devm_kzalloc(&pdev->dev, struct_size(pericom, line, nr), GFP_KERNEL);
	if (!pericom)
		return -ENOMEM;

	pericom->virt = pcim_iomap(pdev, bar, 0);
	if (!pericom->virt)
		return -ENOMEM;

	memset(&uart, 0, sizeof(uart));

	uart.port.dev = &pdev->dev;
	uart.port.irq = pdev->irq;
	uart.port.private_data = pericom;
	uart.port.iotype = UPIO_PORT;
	uart.port.uartclk = 921600 * 16;
	uart.port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;
	uart.port.set_divisor = pericom_do_set_divisor;
	for (i = 0; i < nr && i < maxnr; i++) {
		unsigned int offset = (i == 3 && nr == 4) ? 0x38 : i * 0x8;

		uart.port.iobase = pci_resource_start(pdev, bar) + offset;

		dev_dbg(&pdev->dev, "Setup PCI port: port %lx, irq %d, type %d\n",
			uart.port.iobase, uart.port.irq, uart.port.iotype);

		pericom->line[i] = serial8250_register_8250_port(&uart);
		if (pericom->line[i] < 0) {
			dev_err(&pdev->dev,
				"Couldn't register serial port %lx, irq %d, type %d, error %d\n",
				uart.port.iobase, uart.port.irq,
				uart.port.iotype, pericom->line[i]);
			break;
		}
	}
	pericom->nr = i;

	pci_set_drvdata(pdev, pericom);
	return 0;
}

static void pericom8250_remove(struct pci_dev *pdev)
{
	struct pericom8250 *pericom = pci_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < pericom->nr; i++)
		serial8250_unregister_port(pericom->line[i]);
}

static const struct pci_device_id pericom8250_pci_ids[] = {
	/*
	 * Pericom PI7C9X795[1248] Uno/Dual/Quad/Octal UART
	 * (Only 7954 has an offset jump for port 4)
	 */
	{ PCI_VDEVICE(PERICOM, PCI_DEVICE_ID_PERICOM_PI7C9X7951) },
	{ PCI_VDEVICE(PERICOM, PCI_DEVICE_ID_PERICOM_PI7C9X7952) },
	{ PCI_VDEVICE(PERICOM, PCI_DEVICE_ID_PERICOM_PI7C9X7954) },
	{ PCI_VDEVICE(PERICOM, PCI_DEVICE_ID_PERICOM_PI7C9X7958) },

	/*
	 * ACCES I/O Products quad
	 * (Only 7954 has an offset jump for port 4)
	 */
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_2SDB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_2S) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM422_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM485_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SDB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_4S) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM422_8) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM485_8) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_2DB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM232_2) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_4DB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM232_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM232_8) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_2SMDB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_2SM) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SM) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_4SMDB) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_COM_4SM) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_COM_8SM) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_1) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM422_2) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_2) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM422_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM485_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_2S) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_4S) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM232_2) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM232_2) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM232_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_MPCIE_ICM232_4) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_2SM) },
	{ PCI_VDEVICE(ACCESSIO, PCI_DEVICE_ID_ACCESSIO_PCIE_ICM_4SM) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pericom8250_pci_ids);

static struct pci_driver pericom8250_pci_driver = {
	.name           = "8250_pericom",
	.id_table       = pericom8250_pci_ids,
	.probe          = pericom8250_probe,
	.remove         = pericom8250_remove,
};
module_pci_driver(pericom8250_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Pericom UART driver");
