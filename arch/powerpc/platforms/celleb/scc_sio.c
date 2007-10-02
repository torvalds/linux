/*
 * setup serial port in SCC
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/console.h>

#include <asm/io.h>
#include <asm/prom.h>

/* sio irq0=0xb00010022 irq0=0xb00010023 irq2=0xb00010024
    mmio=0xfff000-0x1000,0xff2000-0x1000 */
static int txx9_serial_bitmap __initdata = 0;

static struct {
	uint32_t offset;
	uint32_t index;
} txx9_scc_tab[3] __initdata = {
	{ 0x300, 0 },	/* 0xFFF300 */
	{ 0x400, 0 },	/* 0xFFF400 */
	{ 0x800, 1 }	/* 0xFF2800 */
};

static int __init txx9_serial_init(void)
{
	extern int early_serial_txx9_setup(struct uart_port *port);
	struct device_node *node = NULL;
	int i;
	struct uart_port req;
	struct of_irq irq;
	struct resource res;

	while ((node = of_find_compatible_node(node,
				"serial", "toshiba,sio-scc")) != NULL) {
		for (i = 0; i < ARRAY_SIZE(txx9_scc_tab); i++) {
			if (!(txx9_serial_bitmap & (1<<i)))
				continue;

			if (of_irq_map_one(node, i, &irq))
				continue;
			if (of_address_to_resource(node,
				txx9_scc_tab[i].index, &res))
				continue;

			memset(&req, 0, sizeof(req));
			req.line = i;
			req.iotype = UPIO_MEM;
			req.mapbase = res.start + txx9_scc_tab[i].offset;
#ifdef CONFIG_SERIAL_TXX9_CONSOLE
			req.membase = ioremap(req.mapbase, 0x24);
#endif
			req.irq = irq_create_of_mapping(irq.controller,
				irq.specifier, irq.size);
			req.flags |= UPF_IOREMAP | UPF_BUGGY_UART
				/*HAVE_CTS_LINE*/;
			req.uartclk = 83300000;
			early_serial_txx9_setup(&req);
		}
	}

	return 0;
}

static int __init txx9_serial_config(char *ptr)
{
	int	i;

	for (;;) {
		switch(get_option(&ptr, &i)) {
		default:
			return 0;
		case 2:
			txx9_serial_bitmap |= 1 << i;
			break;
		case 1:
			txx9_serial_bitmap |= 1 << i;
			return 0;
		}
	}
}
__setup("txx9_serial=", txx9_serial_config);

console_initcall(txx9_serial_init);
