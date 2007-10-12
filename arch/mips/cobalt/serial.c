/*
 *  Registration of Cobalt UART platform device.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

#include <cobalt.h>
#include <irq.h>

static struct resource cobalt_uart_resource[] __initdata = {
	{
		.start	= 0x1c800000,
		.end	= 0x1c800007,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= SERIAL_IRQ,
		.end	= SERIAL_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct plat_serial8250_port cobalt_serial8250_port[] = {
	{
		.irq		= SERIAL_IRQ,
		.uartclk	= 18432000,
		.iotype		= UPIO_MEM,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.mapbase	= 0x1c800000,
	},
	{},
};

static __init int cobalt_uart_add(void)
{
	struct platform_device *pdev;
	int retval;

	/*
	 * Cobalt Qube1 has no UART.
	 */
	if (cobalt_board_id == COBALT_BRD_ID_QUBE1)
		return 0;

	pdev = platform_device_alloc("serial8250", -1);
	if (!pdev)
		return -ENOMEM;

	pdev->id = PLAT8250_DEV_PLATFORM;
	pdev->dev.platform_data = cobalt_serial8250_port;

	retval = platform_device_add_resources(pdev, cobalt_uart_resource, ARRAY_SIZE(cobalt_uart_resource));
	if (retval)
		goto err_free_device;

	retval = platform_device_add(pdev);
	if (retval)
		goto err_free_device;

	return 0;

err_free_device:
	platform_device_put(pdev);

	return retval;
}
device_initcall(cobalt_uart_add);
