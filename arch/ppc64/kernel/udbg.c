/*
 * NS16550 Serial Port (uart) debugging stuff.
 *
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#define WANT_PPCDBG_TAB /* Only defined here */
#include <linux/config.h>
#include <linux/types.h>
#include <asm/ppcdebug.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pmac_feature.h>

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);

struct NS16550 {
	/* this struct must be packed */
	unsigned char rbr;  /* 0 */
	unsigned char ier;  /* 1 */
	unsigned char fcr;  /* 2 */
	unsigned char lcr;  /* 3 */
	unsigned char mcr;  /* 4 */
	unsigned char lsr;  /* 5 */
	unsigned char msr;  /* 6 */
	unsigned char scr;  /* 7 */
};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier
#define dlab lcr

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

static volatile struct NS16550 __iomem *udbg_comport;

void udbg_init_uart(void __iomem *comport, unsigned int speed)
{
	u16 dll = speed ? (115200 / speed) : 12;

	if (comport) {
		udbg_comport = (struct NS16550 __iomem *)comport;
		out_8(&udbg_comport->lcr, 0x00);
		out_8(&udbg_comport->ier, 0xff);
		out_8(&udbg_comport->ier, 0x00);
		out_8(&udbg_comport->lcr, 0x80);	/* Access baud rate */
		out_8(&udbg_comport->dll, dll & 0xff);	/* 1 = 115200,  2 = 57600,
							   3 = 38400, 12 = 9600 baud */
		out_8(&udbg_comport->dlm, dll >> 8);	/* dll >> 8 which should be zero
							   for fast rates; */
		out_8(&udbg_comport->lcr, 0x03);	/* 8 data, 1 stop, no parity */
		out_8(&udbg_comport->mcr, 0x03);	/* RTS/DTR */
		out_8(&udbg_comport->fcr ,0x07);	/* Clear & enable FIFOs */
	}
}

#ifdef CONFIG_PPC_PMAC

#define	SCC_TXRDY	4
#define SCC_RXRDY	1

static volatile u8 __iomem *sccc;
static volatile u8 __iomem *sccd;

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 0,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0xea,		/* tx 8 bits, assert DTR & RTS */
    4,  0x46,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void udbg_init_scc(struct device_node *np)
{
	u32 *reg;
	unsigned long addr;
	int i, x;

	if (np == NULL)
		np = of_find_node_by_name(NULL, "escc");
	if (np == NULL || np->parent == NULL)
		return;

	udbg_printf("found SCC...\n");
	/* Get address within mac-io ASIC */ 
	reg = (u32 *)get_property(np, "reg", NULL);
	if (reg == NULL)
		return;
	addr = reg[0];
	udbg_printf("local addr: %lx\n", addr);
	/* Get address of mac-io PCI itself */
	reg = (u32 *)get_property(np->parent, "assigned-addresses", NULL);
	if (reg == NULL)
		return;
	addr += reg[2];
	udbg_printf("final addr: %lx\n", addr);

	/* Setup for 57600 8N1 */
	addr += 0x20;
	sccc = (volatile u8 * __iomem) ioremap(addr & PAGE_MASK, PAGE_SIZE) ;
	sccc += addr & ~PAGE_MASK;
	sccd = sccc + 0x10;

	udbg_printf("ioremap result sccc: %p\n", sccc);
	mb();

	for (i = 20000; i != 0; --i)
		x = in_8(sccc);
	out_8(sccc, 0x09);		/* reset A or B side */
	out_8(sccc, 0xc0);
	for (i = 0; i < sizeof(scc_inittab); ++i)
		out_8(sccc, scc_inittab[i]);

	ppc_md.udbg_putc = udbg_putc;
	ppc_md.udbg_getc = udbg_getc;
	ppc_md.udbg_getc_poll = udbg_getc_poll;

	udbg_puts("Hello World !\n");
}

#endif /* CONFIG_PPC_PMAC */

#if CONFIG_PPC_PMAC
static void udbg_real_putc(unsigned char c)
{
	while ((real_readb(sccc) & SCC_TXRDY) == 0)
		;
	real_writeb(c, sccd);
	if (c == '\n')
		udbg_real_putc('\r');
}

void udbg_init_pmac_realmode(void)
{
	sccc = (volatile u8 __iomem *)0x80013020ul;
	sccd = (volatile u8 __iomem *)0x80013030ul;

	ppc_md.udbg_putc = udbg_real_putc;
	ppc_md.udbg_getc = NULL;
	ppc_md.udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC_PMAC */

#ifdef CONFIG_PPC_MAPLE
void udbg_maple_real_putc(unsigned char c)
{
	if (udbg_comport) {
		while ((real_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		real_writeb(c, &udbg_comport->thr); eieio();
		if (c == '\n') {
			/* Also put a CR.  This is for convenience. */
			while ((real_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
				/* wait for idle */;
			real_writeb('\r', &udbg_comport->thr); eieio();
		}
	}
}

void udbg_init_maple_realmode(void)
{
	udbg_comport = (volatile struct NS16550 __iomem *)0xf40003f8;

	ppc_md.udbg_putc = udbg_maple_real_putc;
	ppc_md.udbg_getc = NULL;
	ppc_md.udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC_MAPLE */

void udbg_putc(unsigned char c)
{
	if (udbg_comport) {
		while ((in_8(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		out_8(&udbg_comport->thr, c);
		if (c == '\n') {
			/* Also put a CR.  This is for convenience. */
			while ((in_8(&udbg_comport->lsr) & LSR_THRE) == 0)
				/* wait for idle */; 
			out_8(&udbg_comport->thr, '\r');
		}
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		while ((in_8(sccc) & SCC_TXRDY) == 0)
			;
		out_8(sccd,  c);		
		if (c == '\n')
			udbg_putc('\r');
	}
#endif /* CONFIG_PPC_PMAC */
}

int udbg_getc_poll(void)
{
	if (udbg_comport) {
		if ((in_8(&udbg_comport->lsr) & LSR_DR) != 0)
			return in_8(&udbg_comport->rbr);
		else
			return -1;
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		if ((in_8(sccc) & SCC_RXRDY) != 0)
			return in_8(sccd);
		else
			return -1;
	}
#endif /* CONFIG_PPC_PMAC */
	return -1;
}

unsigned char udbg_getc(void)
{
	if (udbg_comport) {
		while ((in_8(&udbg_comport->lsr) & LSR_DR) == 0)
			/* wait for char */;
		return in_8(&udbg_comport->rbr);
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		while ((in_8(sccc) & SCC_RXRDY) == 0)
			;
		return in_8(sccd);
	}
#endif /* CONFIG_PPC_PMAC */
	return 0;
}

void udbg_puts(const char *s)
{
	if (ppc_md.udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				ppc_md.udbg_putc(c);
		}
	}
#if 0
	else {
		printk("%s", s);
	}
#endif
}

int udbg_write(const char *s, int n)
{
	int remain = n;
	char c;

	if (!ppc_md.udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			ppc_md.udbg_putc(c);
		}
	}

	return n - remain;
}

int udbg_read(char *buf, int buflen)
{
	char c, *p = buf;
	int i;

	if (!ppc_md.udbg_getc)
		return 0;

	for (i = 0; i < buflen; ++i) {
		do {
			c = ppc_md.udbg_getc();
		} while (c == 0x11 || c == 0x13);
		if (c == 0)
			break;
		*p++ = c;
	}

	return i;
}

void udbg_console_write(struct console *con, const char *s, unsigned int n)
{
	udbg_write(s, n);
}

#define UDBG_BUFSIZE 256
void udbg_printf(const char *fmt, ...)
{
	unsigned char buf[UDBG_BUFSIZE];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, UDBG_BUFSIZE, fmt, args);
	udbg_puts(buf);
	va_end(args);
}

/* Special print used by PPCDBG() macro */
void udbg_ppcdbg(unsigned long debug_flags, const char *fmt, ...)
{
	unsigned long active_debugs = debug_flags & ppc64_debug_switch;

	if (active_debugs) {
		va_list ap;
		unsigned char buf[UDBG_BUFSIZE];
		unsigned long i, len = 0;

		for (i=0; i < PPCDBG_NUM_FLAGS; i++) {
			if (((1U << i) & active_debugs) && 
			    trace_names[i]) {
				len += strlen(trace_names[i]); 
				udbg_puts(trace_names[i]);
				break;
			}
		}

		snprintf(buf, UDBG_BUFSIZE, " [%s]: ", current->comm);
		len += strlen(buf); 
		udbg_puts(buf);

		while (len < 18) {
			udbg_puts(" ");
			len++;
		}

		va_start(ap, fmt);
		vsnprintf(buf, UDBG_BUFSIZE, fmt, ap);
		udbg_puts(buf);
		va_end(ap);
	}
}

unsigned long udbg_ifdebug(unsigned long flags)
{
	return (flags & ppc64_debug_switch);
}
