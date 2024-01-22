// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V SBI based earlycon
 *
 * Copyright (C) 2018 Anup Patel <anup@brainfault.org>
 */
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <asm/sbi.h>

static void sbi_putc(struct uart_port *port, unsigned char c)
{
	sbi_console_putchar(c);
}

static void sbi_0_1_console_write(struct console *con,
				  const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	uart_console_write(&dev->port, s, n, sbi_putc);
}

static void sbi_dbcn_console_write(struct console *con,
				   const char *s, unsigned int n)
{
	int ret;

	while (n) {
		ret = sbi_debug_console_write(s, n);
		if (ret < 0)
			break;

		s += ret;
		n -= ret;
	}
}

static int __init early_sbi_setup(struct earlycon_device *device,
				  const char *opt)
{
	if (sbi_debug_console_available)
		device->con->write = sbi_dbcn_console_write;
	else if (IS_ENABLED(CONFIG_RISCV_SBI_V01))
		device->con->write = sbi_0_1_console_write;
	else
		return -ENODEV;

	return 0;
}
EARLYCON_DECLARE(sbi, early_sbi_setup);
