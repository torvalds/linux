// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Emma Mobile 8250 driver
 *
 *  Copyright (C) 2012 Magnus Damm
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include "8250.h"

#define UART_DLL_EM 9
#define UART_DLM_EM 10
#define UART_HCR0_EM 11

/*
 * A high value for UART_FCR_EM avoids overlapping with existing UART_*
 * register defines. UART_FCR_EM_HW is the real HW register offset.
 */
#define UART_FCR_EM 0x10003
#define UART_FCR_EM_HW 3

#define UART_HCR0_EM_SW_RESET	BIT(7) /* SW Reset */

struct serial8250_em_priv {
	int line;
};

static void serial8250_em_serial_out_helper(struct uart_port *p, int offset,
					    int value)
{
	switch (offset) {
	case UART_TX: /* TX @ 0x00 */
		writeb(value, p->membase);
		break;
	case UART_LCR: /* LCR @ 0x10 (+1) */
	case UART_MCR: /* MCR @ 0x14 (+1) */
	case UART_SCR: /* SCR @ 0x20 (+1) */
		writel(value, p->membase + ((offset + 1) << 2));
		break;
	case UART_FCR_EM:
		writel(value, p->membase + (UART_FCR_EM_HW << 2));
		break;
	case UART_IER: /* IER @ 0x04 */
		value &= 0x0f; /* only 4 valid bits - not Xscale */
		fallthrough;
	case UART_DLL_EM: /* DLL @ 0x24 (+9) */
	case UART_DLM_EM: /* DLM @ 0x28 (+9) */
	case UART_HCR0_EM: /* HCR0 @ 0x2c */
		writel(value, p->membase + (offset << 2));
		break;
	}
}

static unsigned int serial8250_em_serial_in(struct uart_port *p, int offset)
{
	switch (offset) {
	case UART_RX: /* RX @ 0x00 */
		return readb(p->membase);
	case UART_LCR: /* LCR @ 0x10 (+1) */
	case UART_MCR: /* MCR @ 0x14 (+1) */
	case UART_LSR: /* LSR @ 0x18 (+1) */
	case UART_MSR: /* MSR @ 0x1c (+1) */
	case UART_SCR: /* SCR @ 0x20 (+1) */
		return readl(p->membase + ((offset + 1) << 2));
	case UART_FCR_EM:
		return readl(p->membase + (UART_FCR_EM_HW << 2));
	case UART_IER: /* IER @ 0x04 */
	case UART_IIR: /* IIR @ 0x08 */
	case UART_DLL_EM: /* DLL @ 0x24 (+9) */
	case UART_DLM_EM: /* DLM @ 0x28 (+9) */
	case UART_HCR0_EM: /* HCR0 @ 0x2c */
		return readl(p->membase + (offset << 2));
	}
	return 0;
}

static void serial8250_em_reg_update(struct uart_port *p, int off, int value)
{
	unsigned int ier, fcr, lcr, mcr, hcr0;

	ier = serial8250_em_serial_in(p, UART_IER);
	fcr = serial8250_em_serial_in(p, UART_FCR_EM);
	lcr = serial8250_em_serial_in(p, UART_LCR);
	mcr = serial8250_em_serial_in(p, UART_MCR);
	hcr0 = serial8250_em_serial_in(p, UART_HCR0_EM);

	serial8250_em_serial_out_helper(p, UART_FCR_EM, fcr |
							UART_FCR_CLEAR_RCVR |
							UART_FCR_CLEAR_XMIT);
	serial8250_em_serial_out_helper(p, UART_HCR0_EM, hcr0 |
							 UART_HCR0_EM_SW_RESET);
	serial8250_em_serial_out_helper(p, UART_HCR0_EM, hcr0 &
							 ~UART_HCR0_EM_SW_RESET);

	switch (off) {
	case UART_FCR_EM:
		fcr = value;
		break;
	case UART_LCR:
		lcr = value;
		break;
	case UART_MCR:
		mcr = value;
		break;
	}

	serial8250_em_serial_out_helper(p, UART_IER, ier);
	serial8250_em_serial_out_helper(p, UART_FCR_EM, fcr);
	serial8250_em_serial_out_helper(p, UART_MCR, mcr);
	serial8250_em_serial_out_helper(p, UART_LCR, lcr);
	serial8250_em_serial_out_helper(p, UART_HCR0_EM, hcr0);
}

static void serial8250_em_serial_out(struct uart_port *p, int offset, int value)
{
	switch (offset) {
	case UART_TX:
	case UART_SCR:
	case UART_IER:
	case UART_DLL_EM:
	case UART_DLM_EM:
		serial8250_em_serial_out_helper(p, offset, value);
		break;
	case UART_FCR:
		serial8250_em_reg_update(p, UART_FCR_EM, value);
		break;
	case UART_LCR:
	case UART_MCR:
		serial8250_em_reg_update(p, offset, value);
		break;
	}
}

static u32 serial8250_em_serial_dl_read(struct uart_8250_port *up)
{
	return serial_in(up, UART_DLL_EM) | serial_in(up, UART_DLM_EM) << 8;
}

static void serial8250_em_serial_dl_write(struct uart_8250_port *up, u32 value)
{
	serial_out(up, UART_DLL_EM, value & 0xff);
	serial_out(up, UART_DLM_EM, value >> 8 & 0xff);
}

static int serial8250_em_probe(struct platform_device *pdev)
{
	struct serial8250_em_priv *priv;
	struct device *dev = &pdev->dev;
	struct uart_8250_port up;
	struct resource *regs;
	struct clk *sclk;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return dev_err_probe(dev, -EINVAL, "missing registers\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sclk = devm_clk_get_enabled(dev, "sclk");
	if (IS_ERR(sclk))
		return dev_err_probe(dev, PTR_ERR(sclk), "unable to get clock\n");

	memset(&up, 0, sizeof(up));
	up.port.mapbase = regs->start;
	up.port.irq = irq;
	up.port.type = PORT_16750;
	up.port.flags = UPF_FIXED_PORT | UPF_IOREMAP | UPF_FIXED_TYPE;
	up.port.dev = dev;
	up.port.private_data = priv;

	up.port.uartclk = clk_get_rate(sclk);

	up.port.iotype = UPIO_MEM32;
	up.port.serial_in = serial8250_em_serial_in;
	up.port.serial_out = serial8250_em_serial_out;
	up.dl_read = serial8250_em_serial_dl_read;
	up.dl_write = serial8250_em_serial_dl_write;

	ret = serial8250_register_8250_port(&up);
	if (ret < 0)
		return dev_err_probe(dev, ret, "unable to register 8250 port\n");

	priv->line = ret;
	platform_set_drvdata(pdev, priv);
	return 0;
}

static void serial8250_em_remove(struct platform_device *pdev)
{
	struct serial8250_em_priv *priv = platform_get_drvdata(pdev);

	serial8250_unregister_port(priv->line);
}

static const struct of_device_id serial8250_em_dt_ids[] = {
	{ .compatible = "renesas,em-uart", },
	{},
};
MODULE_DEVICE_TABLE(of, serial8250_em_dt_ids);

static struct platform_driver serial8250_em_platform_driver = {
	.driver = {
		.name		= "serial8250-em",
		.of_match_table = serial8250_em_dt_ids,
	},
	.probe			= serial8250_em_probe,
	.remove_new		= serial8250_em_remove,
};

module_platform_driver(serial8250_em_platform_driver);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Renesas Emma Mobile 8250 Driver");
MODULE_LICENSE("GPL v2");
