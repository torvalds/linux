// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA UART
 *
 * Copyright (C) 2022 Intel Corporation.
 *
 * Authors:
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Matthew Gerlach <matthew.gerlach@linux.intel.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dfl.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/types.h>

#include <linux/serial.h>
#include <linux/serial_8250.h>

#define DFHv1_PARAM_ID_CLK_FRQ    0x2
#define DFHv1_PARAM_ID_FIFO_LEN   0x3

#define DFHv1_PARAM_ID_REG_LAYOUT	0x4
#define DFHv1_PARAM_REG_LAYOUT_WIDTH	GENMASK_ULL(63, 32)
#define DFHv1_PARAM_REG_LAYOUT_SHIFT	GENMASK_ULL(31, 0)

struct dfl_uart {
	int line;
};

static int dfh_get_u64_param_val(struct dfl_device *dfl_dev, int param_id, u64 *pval)
{
	size_t psize;
	u64 *p;

	p = dfh_find_param(dfl_dev, param_id, &psize);
	if (IS_ERR(p))
		return PTR_ERR(p);

	if (psize != sizeof(*pval))
		return -EINVAL;

	*pval = *p;

	return 0;
}

static int dfl_uart_get_params(struct dfl_device *dfl_dev, struct uart_8250_port *uart)
{
	struct device *dev = &dfl_dev->dev;
	u64 fifo_len, clk_freq, reg_layout;
	u32 reg_width;
	int ret;

	ret = dfh_get_u64_param_val(dfl_dev, DFHv1_PARAM_ID_CLK_FRQ, &clk_freq);
	if (ret)
		return dev_err_probe(dev, ret, "missing CLK_FRQ param\n");

	uart->port.uartclk = clk_freq;

	ret = dfh_get_u64_param_val(dfl_dev, DFHv1_PARAM_ID_FIFO_LEN, &fifo_len);
	if (ret)
		return dev_err_probe(dev, ret, "missing FIFO_LEN param\n");

	switch (fifo_len) {
	case 32:
		uart->port.type = PORT_ALTR_16550_F32;
		break;

	case 64:
		uart->port.type = PORT_ALTR_16550_F64;
		break;

	case 128:
		uart->port.type = PORT_ALTR_16550_F128;
		break;

	default:
		return dev_err_probe(dev, -EINVAL, "unsupported FIFO_LEN %llu\n", fifo_len);
	}

	ret = dfh_get_u64_param_val(dfl_dev, DFHv1_PARAM_ID_REG_LAYOUT, &reg_layout);
	if (ret)
		return dev_err_probe(dev, ret, "missing REG_LAYOUT param\n");

	uart->port.regshift = FIELD_GET(DFHv1_PARAM_REG_LAYOUT_SHIFT, reg_layout);
	reg_width = FIELD_GET(DFHv1_PARAM_REG_LAYOUT_WIDTH, reg_layout);
	switch (reg_width) {
	case 4:
		uart->port.iotype = UPIO_MEM32;
		break;

	case 2:
		uart->port.iotype = UPIO_MEM16;
		break;

	default:
		return dev_err_probe(dev, -EINVAL, "unsupported reg-width %u\n", reg_width);

	}

	return 0;
}

static int dfl_uart_probe(struct dfl_device *dfl_dev)
{
	struct device *dev = &dfl_dev->dev;
	struct uart_8250_port uart = { };
	struct dfl_uart *dfluart;
	int ret;

	uart.port.flags = UPF_IOREMAP;
	uart.port.mapbase = dfl_dev->mmio_res.start;
	uart.port.mapsize = resource_size(&dfl_dev->mmio_res);

	ret = dfl_uart_get_params(dfl_dev, &uart);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed uart feature walk\n");

	if (dfl_dev->num_irqs == 1)
		uart.port.irq = dfl_dev->irqs[0];

	dfluart = devm_kzalloc(dev, sizeof(*dfluart), GFP_KERNEL);
	if (!dfluart)
		return -ENOMEM;

	dfluart->line = serial8250_register_8250_port(&uart);
	if (dfluart->line < 0)
		return dev_err_probe(dev, dfluart->line, "unable to register 8250 port.\n");

	dev_set_drvdata(dev, dfluart);

	return 0;
}

static void dfl_uart_remove(struct dfl_device *dfl_dev)
{
	struct dfl_uart *dfluart = dev_get_drvdata(&dfl_dev->dev);

	serial8250_unregister_port(dfluart->line);
}

#define FME_FEATURE_ID_UART 0x24

static const struct dfl_device_id dfl_uart_ids[] = {
	{ FME_ID, FME_FEATURE_ID_UART },
	{ }
};
MODULE_DEVICE_TABLE(dfl, dfl_uart_ids);

static struct dfl_driver dfl_uart_driver = {
	.drv = {
		.name = "dfl-uart",
	},
	.id_table = dfl_uart_ids,
	.probe = dfl_uart_probe,
	.remove = dfl_uart_remove,
};
module_dfl_driver(dfl_uart_driver);

MODULE_DESCRIPTION("DFL Intel UART driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
