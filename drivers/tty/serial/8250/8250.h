/*
 *  Driver for 8250/16550-type serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/dmaengine.h>

struct uart_8250_dma {
	int (*tx_dma)(struct uart_8250_port *p);
	int (*rx_dma)(struct uart_8250_port *p, unsigned int iir);

	/* Filter function */
	dma_filter_fn		fn;
	/* Parameter to the filter function */
	void			*rx_param;
	void			*tx_param;

	struct dma_slave_config	rxconf;
	struct dma_slave_config	txconf;

	struct dma_chan		*rxchan;
	struct dma_chan		*txchan;

	dma_addr_t		rx_addr;
	dma_addr_t		tx_addr;

	dma_cookie_t		rx_cookie;
	dma_cookie_t		tx_cookie;

	void			*rx_buf;

	size_t			rx_size;
	size_t			tx_size;

	unsigned char		tx_running:1;
	unsigned char		tx_err: 1;
	unsigned char		rx_running:1;
};

struct old_serial_port {
	unsigned int uart;
	unsigned int baud_base;
	unsigned int port;
	unsigned int irq;
	upf_t        flags;
	unsigned char hub6;
	unsigned char io_type;
	unsigned char __iomem *iomem_base;
	unsigned short iomem_reg_shift;
	unsigned long irqflags;
};

struct serial8250_config {
	const char	*name;
	unsigned short	fifo_size;
	unsigned short	tx_loadsz;
	unsigned char	fcr;
	unsigned char	rxtrig_bytes[UART_FCR_R_TRIG_MAX_STATE];
	unsigned int	flags;
};

#define UART_CAP_FIFO	(1 << 8)	/* UART has FIFO */
#define UART_CAP_EFR	(1 << 9)	/* UART has EFR */
#define UART_CAP_SLEEP	(1 << 10)	/* UART has IER sleep */
#define UART_CAP_AFE	(1 << 11)	/* MCR-based hw flow control */
#define UART_CAP_UUE	(1 << 12)	/* UART needs IER bit 6 set (Xscale) */
#define UART_CAP_RTOIE	(1 << 13)	/* UART needs IER bit 4 set (Xscale, Tegra) */
#define UART_CAP_HFIFO	(1 << 14)	/* UART has a "hidden" FIFO */
#define UART_CAP_RPM	(1 << 15)	/* Runtime PM is active while idle */

#define UART_BUG_QUOT	(1 << 0)	/* UART has buggy quot LSB */
#define UART_BUG_TXEN	(1 << 1)	/* UART has buggy TX IIR status */
#define UART_BUG_NOMSR	(1 << 2)	/* UART has buggy MSR status bits (Au1x00) */
#define UART_BUG_THRE	(1 << 3)	/* UART has buggy THRE reassertion */
#define UART_BUG_PARITY	(1 << 4)	/* UART mishandles parity if FIFO enabled */

#define HIGH_BITS_OFFSET ((sizeof(long)-sizeof(int))*8)

#ifdef CONFIG_SERIAL_8250_SHARE_IRQ
#define SERIAL8250_SHARE_IRQS 1
#else
#define SERIAL8250_SHARE_IRQS 0
#endif

static inline int serial_in(struct uart_8250_port *up, int offset)
{
	return up->port.serial_in(&up->port, offset);
}

static inline void serial_out(struct uart_8250_port *up, int offset, int value)
{
	up->port.serial_out(&up->port, offset, value);
}

void serial8250_clear_and_reinit_fifos(struct uart_8250_port *p);

static inline int serial_dl_read(struct uart_8250_port *up)
{
	return up->dl_read(up);
}

static inline void serial_dl_write(struct uart_8250_port *up, int value)
{
	up->dl_write(up, value);
}

struct uart_8250_port *serial8250_get_port(int line);
void serial8250_rpm_get(struct uart_8250_port *p);
void serial8250_rpm_put(struct uart_8250_port *p);

#if defined(__alpha__) && !defined(CONFIG_PCI)
/*
 * Digital did something really horribly wrong with the OUT1 and OUT2
 * lines on at least some ALPHA's.  The failure mode is that if either
 * is cleared, the machine locks up with endless interrupts.
 */
#define ALPHA_KLUDGE_MCR  (UART_MCR_OUT2 | UART_MCR_OUT1)
#else
#define ALPHA_KLUDGE_MCR 0
#endif

#ifdef CONFIG_SERIAL_8250_PNP
int serial8250_pnp_init(void);
void serial8250_pnp_exit(void);
#else
static inline int serial8250_pnp_init(void) { return 0; }
static inline void serial8250_pnp_exit(void) { }
#endif

#ifdef CONFIG_ARCH_OMAP1
static inline int is_omap1_8250(struct uart_8250_port *pt)
{
	int res;

	switch (pt->port.mapbase) {
	case OMAP1_UART1_BASE:
	case OMAP1_UART2_BASE:
	case OMAP1_UART3_BASE:
		res = 1;
		break;
	default:
		res = 0;
		break;
	}

	return res;
}

static inline int is_omap1510_8250(struct uart_8250_port *pt)
{
	if (!cpu_is_omap1510())
		return 0;

	return is_omap1_8250(pt);
}
#else
static inline int is_omap1_8250(struct uart_8250_port *pt)
{
	return 0;
}
static inline int is_omap1510_8250(struct uart_8250_port *pt)
{
	return 0;
}
#endif

#ifdef CONFIG_SERIAL_8250_DMA
extern int serial8250_tx_dma(struct uart_8250_port *);
extern int serial8250_rx_dma(struct uart_8250_port *, unsigned int iir);
extern int serial8250_request_dma(struct uart_8250_port *);
extern void serial8250_release_dma(struct uart_8250_port *);
#else
static inline int serial8250_tx_dma(struct uart_8250_port *p)
{
	return -1;
}
static inline int serial8250_rx_dma(struct uart_8250_port *p, unsigned int iir)
{
	return -1;
}
static inline int serial8250_request_dma(struct uart_8250_port *p)
{
	return -1;
}
static inline void serial8250_release_dma(struct uart_8250_port *p) { }
#endif

static inline int ns16550a_goto_highspeed(struct uart_8250_port *up)
{
	unsigned char status;

	status = serial_in(up, 0x04); /* EXCR2 */
#define PRESL(x) ((x) & 0x30)
	if (PRESL(status) == 0x10) {
		/* already in high speed mode */
		return 0;
	} else {
		status &= ~0xB0; /* Disable LOCK, mask out PRESL[01] */
		status |= 0x10;  /* 1.625 divisor for baud_base --> 921600 */
		serial_out(up, 0x04, status);
	}
	return 1;
}
