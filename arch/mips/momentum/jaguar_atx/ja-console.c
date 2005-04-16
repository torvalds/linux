/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2002, 2004 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <asm/serial.h>

/* SUPERIO uart register map */
struct ja_uartregs {
	union {
		volatile u8	pad0[3];
		volatile u8	rbr;	/* read only, DLAB == 0 */
		volatile u8	pad1[3];
		volatile u8	thr;	/* write only, DLAB == 0 */
		volatile u8	pad2[3];
		volatile u8	dll;	/* DLAB == 1 */
	} u1;
	union {
		volatile u8	pad0[3];
		volatile u8	ier;	/* DLAB == 0 */
		volatile u8	pad1[3];
		volatile u8	dlm;	/* DLAB == 1 */
	} u2;
	union {
		volatile u8	pad0[3];
		volatile u8	iir;	/* read only */
		volatile u8	pad1[3];
		volatile u8	fcr;	/* write only */
	} u3;
	volatile u8	pad0[3];
	volatile u8	iu_lcr;
	volatile u8	pad1[3];
	volatile u8	iu_mcr;
	volatile u8	pad2[3];
	volatile u8	iu_lsr;
	volatile u8	pad3[3];
	volatile u8	iu_msr;
	volatile u8	pad4[3];
	volatile u8	iu_scr;
} ja_uregs_t;

#define iu_rbr u1.rbr
#define iu_thr u1.thr
#define iu_dll u1.dll
#define iu_ier u2.ier
#define iu_dlm u2.dlm
#define iu_iir u3.iir
#define iu_fcr u3.fcr

extern unsigned long uart_base;

static inline struct ja_uartregs *console_uart(void)
{
	return (struct ja_uartregs *) (uart_base + 0x23UL);
}

void prom_putchar(char c)
{
	struct ja_uartregs *uart = console_uart();

	while ((uart->iu_lsr & 0x20) == 0);
	uart->iu_thr = c;
}

char __init prom_getchar(void)
{
	return 0;
}

static void inline ja_console_probe(void)
{
	struct uart_port up;

	/*
	 * Register to interrupt zero because we share the interrupt with
	 * the serial driver which we don't properly support yet.
	 */
	memset(&up, 0, sizeof(up));
	up.membase	= (unsigned char *) uart_base + 0x23UL;
	up.irq		= JAGUAR_ATX_SERIAL1_IRQ;
	up.uartclk	= JAGUAR_ATX_UART_CLK;
	up.regshift	= 2;
	up.iotype	= UPIO_MEM;
	up.flags	= ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	up.line		= 0;

	if (early_serial_setup(&up))
		printk(KERN_ERR "Early serial init of port 0 failed\n");
}

__init void ja_setup_console(void)
{
	ja_console_probe();
}
