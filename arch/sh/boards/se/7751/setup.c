/* 
 * linux/arch/sh/kernel/setup_7751se.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 * Modified for 7751 Solution Engine by
 * Ian da Silva and Jeremy Siegel, 2001.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <linux/hdreg.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/se7751/se7751.h>

#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
#endif

/*
 * Configure the Super I/O chip
 */
#if 0
/* Leftover code from regular Solution Engine, for reference. */
/* The SH7751 Solution Engine has a different SuperIO. */
static void __init smsc_config(int index, int data)
{
	outb_p(index, INDEX_PORT);
	outb_p(data, DATA_PORT);
}

static void __init init_smsc(void)
{
	outb_p(CONFIG_ENTER, CONFIG_PORT);
	outb_p(CONFIG_ENTER, CONFIG_PORT);

	/* FDC */
	smsc_config(CURRENT_LDN_INDEX, LDN_FDC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 6); /* IRQ6 */

	/* IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_IDE1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 14); /* IRQ14 */

	/* AUXIO (GPIO): to use IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_AUXIO);
	smsc_config(GPIO46_INDEX, 0x00); /* nIOROP */
	smsc_config(GPIO47_INDEX, 0x00); /* nIOWOP */

	/* COM1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_COM1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IO_BASE_HI_INDEX, 0x03);
	smsc_config(IO_BASE_LO_INDEX, 0xf8);
	smsc_config(IRQ_SELECT_INDEX, 4); /* IRQ4 */

	/* COM2 */
	smsc_config(CURRENT_LDN_INDEX, LDN_COM2);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IO_BASE_HI_INDEX, 0x02);
	smsc_config(IO_BASE_LO_INDEX, 0xf8);
	smsc_config(IRQ_SELECT_INDEX, 3); /* IRQ3 */

	/* RTC */
	smsc_config(CURRENT_LDN_INDEX, LDN_RTC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 8); /* IRQ8 */

	/* XXX: PARPORT, KBD, and MOUSE will come here... */
	outb_p(CONFIG_EXIT, CONFIG_PORT);
}
#endif

const char *get_system_type(void)
{
	return "7751 SolutionEngine";
}

#ifdef CONFIG_SH_KGDB
static int kgdb_uart_setup(void);
static struct kgdb_sermap kgdb_uart_sermap = 
{ "ttyS", 0, kgdb_uart_setup, NULL };
#endif
 
/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	/* Call init_smsc() replacement to set up SuperIO. */
	/* XXX: RTC setting comes here */
#ifdef CONFIG_SH_KGDB
	kgdb_register_sermap(&kgdb_uart_sermap);
#endif
}

/*********************************************************************
 * Currently a hack (e.g. does not interact well w/serial.c, lots of *
 * hardcoded stuff) but may be useful if SCI/F needs debugging.      *
 * Mostly copied from x86 code (see files asm-i386/kgdb_local.h and  *
 * arch/i386/lib/kgdb_serial.c).                                     *
 *********************************************************************/

#ifdef CONFIG_SH_KGDB
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>

#define COM1_PORT 0x3f8  /* Base I/O address */
#define COM1_IRQ  4      /* IRQ not used yet */
#define COM2_PORT 0x2f8  /* Base I/O address */
#define COM2_IRQ  3      /* IRQ not used yet */

#define SB_CLOCK 1843200 /* Serial baud clock */
#define SB_BASE (SB_CLOCK/16)
#define SB_MCR UART_MCR_OUT2 | UART_MCR_DTR | UART_MCR_RTS

struct uart_port {
	int base;
};
#define UART_NPORTS 2
struct uart_port uart_ports[] = {
	{ COM1_PORT },
	{ COM2_PORT },
};
struct uart_port *kgdb_uart_port;

#define UART_IN(reg)	inb_p(kgdb_uart_port->base + reg)
#define UART_OUT(reg,v)	outb_p((v), kgdb_uart_port->base + reg)

/* Basic read/write functions for the UART */
#define UART_LSR_RXCERR    (UART_LSR_BI | UART_LSR_FE | UART_LSR_PE)
static int kgdb_uart_getchar(void)
{
	int lsr;
	int c = -1;

	while (c == -1) {
		lsr = UART_IN(UART_LSR);
		if (lsr & UART_LSR_DR) 
			c = UART_IN(UART_RX);
		if ((lsr & UART_LSR_RXCERR))
			c = -1;
	}
	return c;
}

static void kgdb_uart_putchar(int c)
{
	while ((UART_IN(UART_LSR) & UART_LSR_THRE) == 0)
		;
	UART_OUT(UART_TX, c);
}

/*
 * Initialize UART to configured/requested values.
 * (But we don't interrupts yet, or interact w/serial.c)
 */
static int kgdb_uart_setup(void)
{
	int port;
	int lcr = 0;
	int bdiv = 0;

	if (kgdb_portnum >= UART_NPORTS) {
		KGDB_PRINTK("uart port %d invalid.\n", kgdb_portnum);
		return -1;
	}

	kgdb_uart_port = &uart_ports[kgdb_portnum];

	/* Init sequence from gdb_hook_interrupt */
	UART_IN(UART_RX);
	UART_OUT(UART_IER, 0);

	UART_IN(UART_RX);	/* Serial driver comments say */
	UART_IN(UART_IIR);	/* this clears interrupt regs */
	UART_IN(UART_MSR);

	/* Figure basic LCR values */
	switch (kgdb_bits) {
	case '7':
		lcr |= UART_LCR_WLEN7;
		break;
	default: case '8': 
		lcr |= UART_LCR_WLEN8;
		break;
	}
	switch (kgdb_parity) {
	case 'O':
		lcr |= UART_LCR_PARITY;
		break;
	case 'E':
		lcr |= (UART_LCR_PARITY | UART_LCR_EPAR);
		break;
	default: break;
	}

	/* Figure the baud rate divisor */
	bdiv = (SB_BASE/kgdb_baud);
	
	/* Set the baud rate and LCR values */
	UART_OUT(UART_LCR, (lcr | UART_LCR_DLAB));
	UART_OUT(UART_DLL, (bdiv & 0xff));
	UART_OUT(UART_DLM, ((bdiv >> 8) & 0xff));
	UART_OUT(UART_LCR, lcr);

	/* Set the MCR */
	UART_OUT(UART_MCR, SB_MCR);

	/* Turn off FIFOs for now */
	UART_OUT(UART_FCR, 0);

	/* Setup complete: initialize function pointers */
	kgdb_getchar = kgdb_uart_getchar;
	kgdb_putchar = kgdb_uart_putchar;

	return 0;
}
#endif /* CONFIG_SH_KGDB */
