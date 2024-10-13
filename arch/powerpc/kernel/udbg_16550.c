// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * udbg for NS16550 compatible serial ports
 *
 * Copyright (C) 2001-2005 PPC 64 Team, IBM Corp
 */
#include <linux/types.h>
#include <asm/udbg.h>
#include <asm/io.h>
#include <asm/early_ioremap.h>

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);
extern u8 real_205_readb(volatile u8 __iomem  *addr);
extern void real_205_writeb(u8 data, volatile u8 __iomem *addr);

#define UART_RBR	0
#define UART_IER	1
#define UART_FCR	2
#define UART_LCR	3
#define UART_MCR	4
#define UART_LSR	5
#define UART_MSR	6
#define UART_SCR	7
#define UART_THR	UART_RBR
#define UART_IIR	UART_FCR
#define UART_DLL	UART_RBR
#define UART_DLM	UART_IER
#define UART_DLAB	UART_LCR

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

#define LCR_DLAB 0x80

static u8 (*udbg_uart_in)(unsigned int reg);
static void (*udbg_uart_out)(unsigned int reg, u8 data);

static void udbg_uart_flush(void)
{
	if (!udbg_uart_in)
		return;

	/* wait for idle */
	while ((udbg_uart_in(UART_LSR) & LSR_THRE) == 0)
		cpu_relax();
}

static void udbg_uart_putc(char c)
{
	if (!udbg_uart_out)
		return;

	if (c == '\n')
		udbg_uart_putc('\r');
	udbg_uart_flush();
	udbg_uart_out(UART_THR, c);
}

static int udbg_uart_getc_poll(void)
{
	if (!udbg_uart_in)
		return -1;

	if (!(udbg_uart_in(UART_LSR) & LSR_DR))
		return udbg_uart_in(UART_RBR);

	return -1;
}

static int udbg_uart_getc(void)
{
	if (!udbg_uart_in)
		return -1;
	/* wait for char */
	while (!(udbg_uart_in(UART_LSR) & LSR_DR))
		cpu_relax();
	return udbg_uart_in(UART_RBR);
}

static void __init udbg_use_uart(void)
{
	udbg_putc = udbg_uart_putc;
	udbg_flush = udbg_uart_flush;
	udbg_getc = udbg_uart_getc;
	udbg_getc_poll = udbg_uart_getc_poll;
}

void __init udbg_uart_setup(unsigned int speed, unsigned int clock)
{
	unsigned int dll, base_bauds;

	if (!udbg_uart_out)
		return;

	if (clock == 0)
		clock = 1843200;
	if (speed == 0)
		speed = 9600;

	base_bauds = clock / 16;
	dll = base_bauds / speed;

	udbg_uart_out(UART_LCR, 0x00);
	udbg_uart_out(UART_IER, 0xff);
	udbg_uart_out(UART_IER, 0x00);
	udbg_uart_out(UART_LCR, LCR_DLAB);
	udbg_uart_out(UART_DLL, dll & 0xff);
	udbg_uart_out(UART_DLM, dll >> 8);
	/* 8 data, 1 stop, no parity */
	udbg_uart_out(UART_LCR, 0x3);
	/* RTS/DTR */
	udbg_uart_out(UART_MCR, 0x3);
	/* Clear & enable FIFOs */
	udbg_uart_out(UART_FCR, 0x7);
}

unsigned int __init udbg_probe_uart_speed(unsigned int clock)
{
	unsigned int dll, dlm, divisor, prescaler, speed;
	u8 old_lcr;

	old_lcr = udbg_uart_in(UART_LCR);

	/* select divisor latch registers.  */
	udbg_uart_out(UART_LCR, old_lcr | LCR_DLAB);

	/* now, read the divisor */
	dll = udbg_uart_in(UART_DLL);
	dlm = udbg_uart_in(UART_DLM);
	divisor = dlm << 8 | dll;

	/* check prescaling */
	if (udbg_uart_in(UART_MCR) & 0x80)
		prescaler = 4;
	else
		prescaler = 1;

	/* restore the LCR */
	udbg_uart_out(UART_LCR, old_lcr);

	/* calculate speed */
	speed = (clock / prescaler) / (divisor * 16);

	/* sanity check */
	if (speed > (clock / 16))
		speed = 9600;

	return speed;
}

static union {
	unsigned char __iomem *mmio_base;
	unsigned long pio_base;
} udbg_uart;

static unsigned int udbg_uart_stride = 1;

static u8 udbg_uart_in_pio(unsigned int reg)
{
	return inb(udbg_uart.pio_base + (reg * udbg_uart_stride));
}

static void udbg_uart_out_pio(unsigned int reg, u8 data)
{
	outb(data, udbg_uart.pio_base + (reg * udbg_uart_stride));
}

void __init udbg_uart_init_pio(unsigned long port, unsigned int stride)
{
	if (!port)
		return;
	udbg_uart.pio_base = port;
	udbg_uart_stride = stride;
	udbg_uart_in = udbg_uart_in_pio;
	udbg_uart_out = udbg_uart_out_pio;
	udbg_use_uart();
}

static u8 udbg_uart_in_mmio(unsigned int reg)
{
	return in_8(udbg_uart.mmio_base + (reg * udbg_uart_stride));
}

static void udbg_uart_out_mmio(unsigned int reg, u8 data)
{
	out_8(udbg_uart.mmio_base + (reg * udbg_uart_stride), data);
}


void __init udbg_uart_init_mmio(void __iomem *addr, unsigned int stride)
{
	if (!addr)
		return;
	udbg_uart.mmio_base = addr;
	udbg_uart_stride = stride;
	udbg_uart_in = udbg_uart_in_mmio;
	udbg_uart_out = udbg_uart_out_mmio;
	udbg_use_uart();
}

#ifdef CONFIG_PPC_PASEMI

#define UDBG_UART_PAS_ADDR	((void __iomem *)0xfcff03f8UL)

static u8 udbg_uart_in_pas(unsigned int reg)
{
	return real_205_readb(UDBG_UART_PAS_ADDR + reg);
}

static void udbg_uart_out_pas(unsigned int reg, u8 val)
{
	real_205_writeb(val, UDBG_UART_PAS_ADDR + reg);
}

void __init udbg_init_pas_realmode(void)
{
	udbg_uart_in = udbg_uart_in_pas;
	udbg_uart_out = udbg_uart_out_pas;
	udbg_use_uart();
}

#endif /* CONFIG_PPC_PASEMI */

#ifdef CONFIG_PPC_EARLY_DEBUG_44x

#include <platforms/44x/44x.h>

static u8 udbg_uart_in_44x_as1(unsigned int reg)
{
	return as1_readb((void __iomem *)PPC44x_EARLY_DEBUG_VIRTADDR + reg);
}

static void udbg_uart_out_44x_as1(unsigned int reg, u8 val)
{
	as1_writeb(val, (void __iomem *)PPC44x_EARLY_DEBUG_VIRTADDR + reg);
}

void __init udbg_init_44x_as1(void)
{
	udbg_uart_in = udbg_uart_in_44x_as1;
	udbg_uart_out = udbg_uart_out_44x_as1;
	udbg_use_uart();
}

#endif /* CONFIG_PPC_EARLY_DEBUG_44x */

#ifdef CONFIG_PPC_EARLY_DEBUG_16550

static void __iomem *udbg_uart_early_addr;

void __init udbg_init_debug_16550(void)
{
	udbg_uart_early_addr = early_ioremap(CONFIG_PPC_EARLY_DEBUG_16550_PHYSADDR, 0x1000);
	udbg_uart_init_mmio(udbg_uart_early_addr, CONFIG_PPC_EARLY_DEBUG_16550_STRIDE);
}

static int __init udbg_init_debug_16550_ioremap(void)
{
	void __iomem *addr;

	if (!udbg_uart_early_addr)
		return 0;

	addr = ioremap(CONFIG_PPC_EARLY_DEBUG_16550_PHYSADDR, 0x1000);
	if (WARN_ON(!addr))
		return -ENOMEM;

	udbg_uart_init_mmio(addr, CONFIG_PPC_EARLY_DEBUG_16550_STRIDE);
	early_iounmap(udbg_uart_early_addr, 0x1000);
	udbg_uart_early_addr = NULL;

	return 0;
}

early_initcall(udbg_init_debug_16550_ioremap);

#endif /* CONFIG_PPC_EARLY_DEBUG_16550 */
