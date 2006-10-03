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
#include <linux/init.h>
#include <asm/machvec.h>
#include <asm/se7751.h>
#include <asm/io.h>

void heartbeat_7751se(void);
void init_7751se_IRQ(void);

#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
static int kgdb_uart_setup(void);
static struct kgdb_sermap kgdb_uart_sermap = 
{ "ttyS", 0, kgdb_uart_setup, NULL };
#endif
 
/*
 * Initialize the board
 */
static void __init sh7751se_setup(char **cmdline_p)
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


/*
 * The Machine Vector
 */

struct sh_machine_vector mv_7751se __initmv = {
	.mv_name		= "7751 SolutionEngine",
	.mv_setup		= sh7751se_setup,
	.mv_nr_irqs		= 72,

	.mv_inb			= sh7751se_inb,
	.mv_inw			= sh7751se_inw,
	.mv_inl			= sh7751se_inl,
	.mv_outb		= sh7751se_outb,
	.mv_outw		= sh7751se_outw,
	.mv_outl		= sh7751se_outl,

	.mv_inb_p		= sh7751se_inb_p,
	.mv_inw_p		= sh7751se_inw,
	.mv_inl_p		= sh7751se_inl,
	.mv_outb_p		= sh7751se_outb_p,
	.mv_outw_p		= sh7751se_outw,
	.mv_outl_p		= sh7751se_outl,

	.mv_insl		= sh7751se_insl,
	.mv_outsl		= sh7751se_outsl,

	.mv_init_irq		= init_7751se_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_7751se,
#endif
};
ALIAS_MV(7751se)
