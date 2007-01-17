/*
 * 16550 serial console support.
 *
 * Original copied from <file:arch/ppc/boot/common/ns16550.c>
 * (which had no copyright)
 * Modifications: 2006 (c) MontaVista Software, Inc.
 *
 * Modified by: Mark A. Greer <mgreer@mvista.com>
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"

#define UART_DLL	0	/* Out: Divisor Latch Low */
#define UART_DLM	1	/* Out: Divisor Latch High */
#define UART_FCR	2	/* Out: FIFO Control Register */
#define UART_LCR	3	/* Out: Line Control Register */
#define UART_MCR	4	/* Out: Modem Control Register */
#define UART_LSR	5	/* In:  Line Status Register */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_DR	0x01	/* Receiver data ready */
#define UART_MSR	6	/* In:  Modem Status Register */
#define UART_SCR	7	/* I/O: Scratch Register */

static unsigned char *reg_base;
static u32 reg_shift;

static int ns16550_open(void)
{
	out_8(reg_base + (UART_FCR << reg_shift), 0x06);
	return 0;
}

static void ns16550_putc(unsigned char c)
{
	while ((in_8(reg_base + (UART_LSR << reg_shift)) & UART_LSR_THRE) == 0);
	out_8(reg_base, c);
}

static unsigned char ns16550_getc(void)
{
	while ((in_8(reg_base + (UART_LSR << reg_shift)) & UART_LSR_DR) == 0);
	return in_8(reg_base);
}

static u8 ns16550_tstc(void)
{
	return ((in_8(reg_base + (UART_LSR << reg_shift)) & UART_LSR_DR) != 0);
}

int ns16550_console_init(void *devp, struct serial_console_data *scdp)
{
	int n;

	n = getprop(devp, "virtual-reg", &reg_base, sizeof(reg_base));
	if (n != sizeof(reg_base))
		return -1;

	n = getprop(devp, "reg-shift", &reg_shift, sizeof(reg_shift));
	if (n != sizeof(reg_shift))
		reg_shift = 0;

	scdp->open = ns16550_open;
	scdp->putc = ns16550_putc;
	scdp->getc = ns16550_getc;
	scdp->tstc = ns16550_tstc;
	scdp->close = NULL;

	return 0;
}
