// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008-2010 Thomas Chou <thomas@wytron.com.tw>
 */

#include <linux/io.h>

#if (defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE) && defined(JTAG_UART_BASE))\
	|| (defined(CONFIG_SERIAL_ALTERA_UART_CONSOLE) && defined(UART0_BASE))
static void *my_ioremap(unsigned long physaddr)
{
	return (void *)(physaddr | CONFIG_NIOS2_IO_REGION_BASE);
}
#endif

#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE) && defined(JTAG_UART_BASE)

#define ALTERA_JTAGUART_SIZE				8
#define ALTERA_JTAGUART_DATA_REG			0
#define ALTERA_JTAGUART_CONTROL_REG			4
#define ALTERA_JTAGUART_CONTROL_AC_MSK			(0x00000400)
#define ALTERA_JTAGUART_CONTROL_WSPACE_MSK		(0xFFFF0000)
static void *uartbase;

#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE_BYPASS)
static void jtag_putc(int ch)
{
	if (readl(uartbase + ALTERA_JTAGUART_CONTROL_REG) &
		ALTERA_JTAGUART_CONTROL_WSPACE_MSK)
		writeb(ch, uartbase + ALTERA_JTAGUART_DATA_REG);
}
#else
static void jtag_putc(int ch)
{
	while ((readl(uartbase + ALTERA_JTAGUART_CONTROL_REG) &
		ALTERA_JTAGUART_CONTROL_WSPACE_MSK) == 0)
		;
	writeb(ch, uartbase + ALTERA_JTAGUART_DATA_REG);
}
#endif

static int putchar(int ch)
{
	jtag_putc(ch);
	return ch;
}

static void console_init(void)
{
	uartbase = my_ioremap((unsigned long) JTAG_UART_BASE);
	writel(ALTERA_JTAGUART_CONTROL_AC_MSK,
		uartbase + ALTERA_JTAGUART_CONTROL_REG);
}

#elif defined(CONFIG_SERIAL_ALTERA_UART_CONSOLE) && defined(UART0_BASE)

#define ALTERA_UART_SIZE		32
#define ALTERA_UART_TXDATA_REG		4
#define ALTERA_UART_STATUS_REG		8
#define ALTERA_UART_DIVISOR_REG		16
#define ALTERA_UART_STATUS_TRDY_MSK	(0x40)
static unsigned uartbase;

static void uart_putc(int ch)
{
	int i;

	for (i = 0; (i < 0x10000); i++) {
		if (readw(uartbase + ALTERA_UART_STATUS_REG) &
			ALTERA_UART_STATUS_TRDY_MSK)
			break;
	}
	writeb(ch, uartbase + ALTERA_UART_TXDATA_REG);
}

static int putchar(int ch)
{
	uart_putc(ch);
	if (ch == '\n')
		uart_putc('\r');
	return ch;
}

static void console_init(void)
{
	unsigned int baud, baudclk;

	uartbase = (unsigned long) my_ioremap((unsigned long) UART0_BASE);
	baud = CONFIG_SERIAL_ALTERA_UART_BAUDRATE;
	baudclk = UART0_FREQ / baud;
	writew(baudclk, uartbase + ALTERA_UART_DIVISOR_REG);
}

#else

static int putchar(int ch)
{
	return ch;
}

static void console_init(void)
{
}

#endif

static int puts(const char *s)
{
	while (*s)
		putchar(*s++);
	return 0;
}
