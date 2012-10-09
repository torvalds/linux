/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2007 Cavium Networks
 */
#include <linux/console.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/tty.h>
#include <linux/irq.h>

#include <asm/time.h>

#include <asm/octeon/octeon.h>

#define DEBUG_UART 1

unsigned int octeon_serial_in(struct uart_port *up, int offset)
{
	int rv = cvmx_read_csr((uint64_t)(up->membase + (offset << 3)));
	if (offset == UART_IIR && (rv & 0xf) == 7) {
		/* Busy interrupt, read the USR (39) and try again. */
		cvmx_read_csr((uint64_t)(up->membase + (39 << 3)));
		rv = cvmx_read_csr((uint64_t)(up->membase + (offset << 3)));
	}
	return rv;
}

void octeon_serial_out(struct uart_port *up, int offset, int value)
{
	/*
	 * If bits 6 or 7 of the OCTEON UART's LCR are set, it quits
	 * working.
	 */
	if (offset == UART_LCR)
		value &= 0x9f;
	cvmx_write_csr((uint64_t)(up->membase + (offset << 3)), (u8)value);
}

static int __devinit octeon_serial_probe(struct platform_device *pdev)
{
	int irq, res;
	struct resource *res_mem;
	struct uart_port port;

	/* All adaptors have an irq.  */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	memset(&port, 0, sizeof(port));

	port.flags = ASYNC_SKIP_TEST | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
	port.type = PORT_OCTEON;
	port.iotype = UPIO_MEM;
	port.regshift = 3;
	port.dev = &pdev->dev;

	if (octeon_is_simulation())
		/* Make simulator output fast*/
		port.uartclk = 115200 * 16;
	else
		port.uartclk = octeon_get_io_clock_rate();

	port.serial_in = octeon_serial_in;
	port.serial_out = octeon_serial_out;
	port.irq = irq;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem == NULL) {
		dev_err(&pdev->dev, "found no memory resource\n");
		return -ENXIO;
	}
	port.mapbase = res_mem->start;
	port.membase = ioremap(res_mem->start, resource_size(res_mem));

	res = serial8250_register_port(&port);

	return res >= 0 ? 0 : res;
}

static struct of_device_id octeon_serial_match[] = {
	{
		.compatible = "cavium,octeon-3860-uart",
	},
	{},
};
MODULE_DEVICE_TABLE(of, octeon_serial_match);

static struct platform_driver octeon_serial_driver = {
	.probe		= octeon_serial_probe,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "octeon_serial",
		.of_match_table = octeon_serial_match,
	},
};

static int __init octeon_serial_init(void)
{
	return platform_driver_register(&octeon_serial_driver);
}
late_initcall(octeon_serial_init);
