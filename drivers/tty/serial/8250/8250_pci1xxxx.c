// SPDX-License-Identifier: GPL-2.0
/*
 *  Probe module for 8250/16550-type MCHP PCI serial ports.
 *
 *  Based on drivers/tty/serial/8250/8250_pci.c,
 *
 *  Copyright (C) 2022 Microchip Technology Inc., All Rights Reserved.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/units.h>
#include <linux/tty.h>

#include <asm/byteorder.h>

#include "8250.h"
#include "8250_pcilib.h"

#define PCI_DEVICE_ID_EFAR_PCI12000		0xa002
#define PCI_DEVICE_ID_EFAR_PCI11010		0xa012
#define PCI_DEVICE_ID_EFAR_PCI11101		0xa022
#define PCI_DEVICE_ID_EFAR_PCI11400		0xa032
#define PCI_DEVICE_ID_EFAR_PCI11414		0xa042

#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_4p	0x0001
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p012	0x0002
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p013	0x0003
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p023	0x0004
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p123	0x0005
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p01	0x0006
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p02	0x0007
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p03	0x0008
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p12	0x0009
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p13	0x000a
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p23	0x000b
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p0	0x000c
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p1	0x000d
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p2	0x000e
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p3	0x000f

#define PCI_SUBDEVICE_ID_EFAR_PCI12000		PCI_DEVICE_ID_EFAR_PCI12000
#define PCI_SUBDEVICE_ID_EFAR_PCI11010		PCI_DEVICE_ID_EFAR_PCI11010
#define PCI_SUBDEVICE_ID_EFAR_PCI11101		PCI_DEVICE_ID_EFAR_PCI11101
#define PCI_SUBDEVICE_ID_EFAR_PCI11400		PCI_DEVICE_ID_EFAR_PCI11400
#define PCI_SUBDEVICE_ID_EFAR_PCI11414		PCI_DEVICE_ID_EFAR_PCI11414

#define UART_ACTV_REG				0x11
#define UART_BLOCK_SET_ACTIVE			BIT(0)

#define UART_PCI_CTRL_REG			0x80
#define UART_PCI_CTRL_SET_MULTIPLE_MSI		BIT(4)
#define UART_PCI_CTRL_D3_CLK_ENABLE		BIT(0)

#define ADCL_CFG_REG				0x40
#define ADCL_CFG_POL_SEL			BIT(2)
#define ADCL_CFG_PIN_SEL			BIT(1)
#define ADCL_CFG_EN				BIT(0)

#define UART_BIT_SAMPLE_CNT			16
#define BAUD_CLOCK_DIV_INT_MSK			GENMASK(31, 8)
#define ADCL_CFG_RTS_DELAY_MASK			GENMASK(11, 8)
#define UART_CLOCK_DEFAULT			(62500 * HZ_PER_KHZ)

#define UART_WAKE_REG				0x8C
#define UART_WAKE_MASK_REG			0x90
#define UART_WAKE_N_PIN				BIT(2)
#define UART_WAKE_NCTS				BIT(1)
#define UART_WAKE_INT				BIT(0)
#define UART_WAKE_SRCS	\
	(UART_WAKE_N_PIN | UART_WAKE_NCTS | UART_WAKE_INT)

#define UART_BAUD_CLK_DIVISOR_REG		0x54

#define UART_RESET_REG				0x94
#define UART_RESET_D3_RESET_DISABLE		BIT(16)

#define MAX_PORTS				4
#define PORT_OFFSET				0x100

static const int logical_to_physical_port_idx[][MAX_PORTS] = {
	{0,  1,  2,  3}, /* PCI12000, PCI11010, PCI11101, PCI11400, PCI11414 */
	{0,  1,  2,  3}, /* PCI4p */
	{0,  1,  2, -1}, /* PCI3p012 */
	{0,  1,  3, -1}, /* PCI3p013 */
	{0,  2,  3, -1}, /* PCI3p023 */
	{1,  2,  3, -1}, /* PCI3p123 */
	{0,  1, -1, -1}, /* PCI2p01 */
	{0,  2, -1, -1}, /* PCI2p02 */
	{0,  3, -1, -1}, /* PCI2p03 */
	{1,  2, -1, -1}, /* PCI2p12 */
	{1,  3, -1, -1}, /* PCI2p13 */
	{2,  3, -1, -1}, /* PCI2p23 */
	{0, -1, -1, -1}, /* PCI1p0 */
	{1, -1, -1, -1}, /* PCI1p1 */
	{2, -1, -1, -1}, /* PCI1p2 */
	{3, -1, -1, -1}, /* PCI1p3 */
};

struct pci1xxxx_8250 {
	unsigned int nr;
	void __iomem *membase;
	int line[] __counted_by(nr);
};

static int pci1xxxx_get_num_ports(struct pci_dev *dev)
{
	switch (dev->subsystem_device) {
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p0:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p1:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p2:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p3:
	case PCI_SUBDEVICE_ID_EFAR_PCI12000:
	case PCI_SUBDEVICE_ID_EFAR_PCI11010:
	case PCI_SUBDEVICE_ID_EFAR_PCI11101:
	case PCI_SUBDEVICE_ID_EFAR_PCI11400:
	default:
		return 1;
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p01:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p02:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p03:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p12:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p13:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p23:
		return 2;
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p012:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p123:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p013:
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p023:
		return 3;
	case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_4p:
	case PCI_SUBDEVICE_ID_EFAR_PCI11414:
		return 4;
	}
}

static unsigned int pci1xxxx_get_divisor(struct uart_port *port,
					 unsigned int baud, unsigned int *frac)
{
	unsigned int quot;

	/*
	 * Calculate baud rate sampling period in nanoseconds.
	 * Fractional part x denotes x/255 parts of a nanosecond.
	 */
	quot = NSEC_PER_SEC / (baud * UART_BIT_SAMPLE_CNT);
	*frac = (NSEC_PER_SEC - quot * baud * UART_BIT_SAMPLE_CNT) *
		  255 / UART_BIT_SAMPLE_CNT / baud;

	return quot;
}

static void pci1xxxx_set_divisor(struct uart_port *port, unsigned int baud,
				 unsigned int quot, unsigned int frac)
{
	writel(FIELD_PREP(BAUD_CLOCK_DIV_INT_MSK, quot) | frac,
	       port->membase + UART_BAUD_CLK_DIVISOR_REG);
}

static int pci1xxxx_rs485_config(struct uart_port *port,
				 struct ktermios *termios,
				 struct serial_rs485 *rs485)
{
	u32 delay_in_baud_periods;
	u32 baud_period_in_ns;
	u32 mode_cfg = 0;
	u32 clock_div;

	/*
	 * pci1xxxx's uart hardware supports only RTS delay after
	 * Tx and in units of bit times to a maximum of 15
	 */
	if (rs485->flags & SER_RS485_ENABLED) {
		mode_cfg = ADCL_CFG_EN | ADCL_CFG_PIN_SEL;

		if (!(rs485->flags & SER_RS485_RTS_ON_SEND))
			mode_cfg |= ADCL_CFG_POL_SEL;

		if (rs485->delay_rts_after_send) {
			clock_div = readl(port->membase + UART_BAUD_CLK_DIVISOR_REG);
			baud_period_in_ns =
				FIELD_GET(BAUD_CLOCK_DIV_INT_MSK, clock_div) *
				UART_BIT_SAMPLE_CNT;
			delay_in_baud_periods =
				rs485->delay_rts_after_send * NSEC_PER_MSEC /
				baud_period_in_ns;
			delay_in_baud_periods =
				min_t(u32, delay_in_baud_periods,
				      FIELD_MAX(ADCL_CFG_RTS_DELAY_MASK));
			mode_cfg |= FIELD_PREP(ADCL_CFG_RTS_DELAY_MASK,
					   delay_in_baud_periods);
			rs485->delay_rts_after_send =
				baud_period_in_ns * delay_in_baud_periods /
				NSEC_PER_MSEC;
		}
	}
	writel(mode_cfg, port->membase + ADCL_CFG_REG);
	return 0;
}

static const struct serial_rs485 pci1xxxx_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
		 SER_RS485_RTS_AFTER_SEND,
	.delay_rts_after_send = 1,
	/* Delay RTS before send is not supported */
};

static bool pci1xxxx_port_suspend(int line)
{
	struct uart_8250_port *up = serial8250_get_port(line);
	struct uart_port *port = &up->port;
	struct tty_port *tport = &port->state->port;
	unsigned long flags;
	bool ret = false;
	u8 wakeup_mask;

	mutex_lock(&tport->mutex);
	if (port->suspended == 0 && port->dev) {
		wakeup_mask = readb(up->port.membase + UART_WAKE_MASK_REG);

		uart_port_lock_irqsave(port, &flags);
		port->mctrl &= ~TIOCM_OUT2;
		port->ops->set_mctrl(port, port->mctrl);
		uart_port_unlock_irqrestore(port, flags);

		ret = (wakeup_mask & UART_WAKE_SRCS) != UART_WAKE_SRCS;
	}

	writeb(UART_WAKE_SRCS, port->membase + UART_WAKE_REG);
	mutex_unlock(&tport->mutex);

	return ret;
}

static void pci1xxxx_port_resume(int line)
{
	struct uart_8250_port *up = serial8250_get_port(line);
	struct uart_port *port = &up->port;
	struct tty_port *tport = &port->state->port;
	unsigned long flags;

	mutex_lock(&tport->mutex);
	writeb(UART_BLOCK_SET_ACTIVE, port->membase + UART_ACTV_REG);
	writeb(UART_WAKE_SRCS, port->membase + UART_WAKE_REG);

	if (port->suspended == 0) {
		uart_port_lock_irqsave(port, &flags);
		port->mctrl |= TIOCM_OUT2;
		port->ops->set_mctrl(port, port->mctrl);
		uart_port_unlock_irqrestore(port, flags);
	}
	mutex_unlock(&tport->mutex);
}

static int pci1xxxx_suspend(struct device *dev)
{
	struct pci1xxxx_8250 *priv = dev_get_drvdata(dev);
	struct pci_dev *pcidev = to_pci_dev(dev);
	bool wakeup = false;
	unsigned int data;
	void __iomem *p;
	int i;

	for (i = 0; i < priv->nr; i++) {
		if (priv->line[i] >= 0) {
			serial8250_suspend_port(priv->line[i]);
			wakeup |= pci1xxxx_port_suspend(priv->line[i]);
		}
	}

	p = pci_ioremap_bar(pcidev, 0);
	if (!p) {
		dev_err(dev, "remapping of bar 0 memory failed");
		return -ENOMEM;
	}

	data = readl(p + UART_RESET_REG);
	writel(data | UART_RESET_D3_RESET_DISABLE, p + UART_RESET_REG);

	if (wakeup)
		writeb(UART_PCI_CTRL_D3_CLK_ENABLE, p + UART_PCI_CTRL_REG);

	iounmap(p);
	device_set_wakeup_enable(dev, true);
	pci_wake_from_d3(pcidev, true);

	return 0;
}

static int pci1xxxx_resume(struct device *dev)
{
	struct pci1xxxx_8250 *priv = dev_get_drvdata(dev);
	struct pci_dev *pcidev = to_pci_dev(dev);
	unsigned int data;
	void __iomem *p;
	int i;

	p = pci_ioremap_bar(pcidev, 0);
	if (!p) {
		dev_err(dev, "remapping of bar 0 memory failed");
		return -ENOMEM;
	}

	data = readl(p + UART_RESET_REG);
	writel(data & ~UART_RESET_D3_RESET_DISABLE, p + UART_RESET_REG);
	iounmap(p);

	for (i = 0; i < priv->nr; i++) {
		if (priv->line[i] >= 0) {
			pci1xxxx_port_resume(priv->line[i]);
			serial8250_resume_port(priv->line[i]);
		}
	}

	return 0;
}

static int pci1xxxx_setup(struct pci_dev *pdev,
			  struct uart_8250_port *port, int port_idx)
{
	int ret;

	port->port.flags |= UPF_FIXED_TYPE | UPF_SKIP_TEST;
	port->port.type = PORT_MCHP16550A;
	port->port.set_termios = serial8250_do_set_termios;
	port->port.get_divisor = pci1xxxx_get_divisor;
	port->port.set_divisor = pci1xxxx_set_divisor;
	port->port.rs485_config = pci1xxxx_rs485_config;
	port->port.rs485_supported = pci1xxxx_rs485_supported;

	ret = serial8250_pci_setup_port(pdev, port, 0, PORT_OFFSET * port_idx, 0);
	if (ret < 0)
		return ret;

	writeb(UART_BLOCK_SET_ACTIVE, port->port.membase + UART_ACTV_REG);
	writeb(UART_WAKE_SRCS, port->port.membase + UART_WAKE_REG);
	writeb(UART_WAKE_N_PIN, port->port.membase + UART_WAKE_MASK_REG);

	return 0;
}

static unsigned int pci1xxxx_get_max_port(int subsys_dev)
{
	unsigned int i = MAX_PORTS;

	if (subsys_dev < ARRAY_SIZE(logical_to_physical_port_idx))
		while (i--) {
			if (logical_to_physical_port_idx[subsys_dev][i] != -1)
				return logical_to_physical_port_idx[subsys_dev][i] + 1;
		}

	if (subsys_dev == PCI_SUBDEVICE_ID_EFAR_PCI11414)
		return 4;

	return 1;
}

static int pci1xxxx_logical_to_physical_port_translate(int subsys_dev, int port)
{
	if (subsys_dev < ARRAY_SIZE(logical_to_physical_port_idx))
		return logical_to_physical_port_idx[subsys_dev][port];

	return logical_to_physical_port_idx[0][port];
}

static int pci1xxxx_serial_probe(struct pci_dev *pdev,
				 const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct pci1xxxx_8250 *priv;
	struct uart_8250_port uart;
	unsigned int max_vec_reqd;
	unsigned int nr_ports, i;
	int num_vectors;
	int subsys_dev;
	int port_idx;
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	nr_ports = pci1xxxx_get_num_ports(pdev);

	priv = devm_kzalloc(dev, struct_size(priv, line, nr_ports), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->membase = pci_ioremap_bar(pdev, 0);
	if (!priv->membase)
		return -ENOMEM;

	pci_set_master(pdev);

	priv->nr = nr_ports;

	subsys_dev = pdev->subsystem_device;
	max_vec_reqd = pci1xxxx_get_max_port(subsys_dev);

	num_vectors = pci_alloc_irq_vectors(pdev, 1, max_vec_reqd, PCI_IRQ_ALL_TYPES);
	if (num_vectors < 0) {
		pci_iounmap(pdev, priv->membase);
		return num_vectors;
	}

	memset(&uart, 0, sizeof(uart));
	uart.port.flags = UPF_SHARE_IRQ | UPF_FIXED_PORT;
	uart.port.uartclk = UART_CLOCK_DEFAULT;
	uart.port.dev = dev;

	if (num_vectors == max_vec_reqd)
		writeb(UART_PCI_CTRL_SET_MULTIPLE_MSI, priv->membase + UART_PCI_CTRL_REG);

	for (i = 0; i < nr_ports; i++) {
		priv->line[i] = -ENODEV;

		port_idx = pci1xxxx_logical_to_physical_port_translate(subsys_dev, i);

		if (num_vectors == max_vec_reqd)
			uart.port.irq = pci_irq_vector(pdev, port_idx);
		else
			uart.port.irq = pci_irq_vector(pdev, 0);

		rc = pci1xxxx_setup(pdev, &uart, port_idx);
		if (rc) {
			dev_warn(dev, "Failed to setup port %u\n", i);
			continue;
		}

		priv->line[i] = serial8250_register_8250_port(&uart);
		if (priv->line[i] < 0) {
			dev_warn(dev,
				"Couldn't register serial port %lx, irq %d, type %d, error %d\n",
				uart.port.iobase, uart.port.irq, uart.port.iotype,
				priv->line[i]);
		}
	}

	pci_set_drvdata(pdev, priv);

	return 0;
}

static void pci1xxxx_serial_remove(struct pci_dev *dev)
{
	struct pci1xxxx_8250 *priv = pci_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < priv->nr; i++) {
		if (priv->line[i] >= 0)
			serial8250_unregister_port(priv->line[i]);
	}

	pci_free_irq_vectors(dev);
	pci_iounmap(dev, priv->membase);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pci1xxxx_pm_ops, pci1xxxx_suspend, pci1xxxx_resume);

static const struct pci_device_id pci1xxxx_pci_tbl[] = {
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_PCI11010) },
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_PCI11101) },
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_PCI11400) },
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_PCI11414) },
	{ PCI_VDEVICE(EFAR, PCI_DEVICE_ID_EFAR_PCI12000) },
	{}
};
MODULE_DEVICE_TABLE(pci, pci1xxxx_pci_tbl);

static struct pci_driver pci1xxxx_pci_driver = {
	.name = "pci1xxxx serial",
	.probe = pci1xxxx_serial_probe,
	.remove = pci1xxxx_serial_remove,
	.driver = {
		.pm     = pm_sleep_ptr(&pci1xxxx_pm_ops),
	},
	.id_table = pci1xxxx_pci_tbl,
};
module_pci_driver(pci1xxxx_pci_driver);

static_assert((ARRAY_SIZE(logical_to_physical_port_idx) == PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p3 + 1));

MODULE_IMPORT_NS(SERIAL_8250_PCI);
MODULE_DESCRIPTION("Microchip Technology Inc. PCIe to UART module");
MODULE_AUTHOR("Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>");
MODULE_AUTHOR("Tharun Kumar P <tharunkumar.pasumarthi@microchip.com>");
MODULE_LICENSE("GPL");
