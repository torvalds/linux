/*
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/serial.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#define UART_GET_CHAR(uart)     bfin_read16(((uart)->port.membase + OFFSET_RBR))
#define UART_GET_DLL(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLL))
#define UART_GET_DLH(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLH))
#define UART_GET_IER(uart)      bfin_read16(((uart)->port.membase + OFFSET_IER_SET))
#define UART_GET_LCR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LCR))
#define UART_GET_LSR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LSR))
#define UART_GET_GCTL(uart)     bfin_read16(((uart)->port.membase + OFFSET_GCTL))
#define UART_GET_MSR(uart)      bfin_read16(((uart)->port.membase + OFFSET_MSR))
#define UART_GET_MCR(uart)      bfin_read16(((uart)->port.membase + OFFSET_MCR))

#define UART_PUT_CHAR(uart,v)   bfin_write16(((uart)->port.membase + OFFSET_THR),v)
#define UART_PUT_DLL(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_DLL),v)
#define UART_SET_IER(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_IER_SET),v)
#define UART_CLEAR_IER(uart,v)  bfin_write16(((uart)->port.membase + OFFSET_IER_CLEAR),v)
#define UART_PUT_DLH(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_DLH),v)
#define UART_PUT_LSR(uart,v)	bfin_write16(((uart)->port.membase + OFFSET_LSR),v)
#define UART_PUT_LCR(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_LCR),v)
#define UART_CLEAR_LSR(uart)    bfin_write16(((uart)->port.membase + OFFSET_LSR), -1)
#define UART_PUT_GCTL(uart,v)   bfin_write16(((uart)->port.membase + OFFSET_GCTL),v)
#define UART_PUT_MCR(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_MCR),v)
#define UART_CLEAR_SCTS(uart)   bfin_write16(((uart)->port.membase + OFFSET_MSR),SCTS)

#define UART_SET_DLAB(uart)     /* MMRs not muxed on BF54x */
#define UART_CLEAR_DLAB(uart)   /* MMRs not muxed on BF54x */

#define UART_GET_CTS(x) (UART_GET_MSR(x) & CTS)
#define UART_DISABLE_RTS(x) UART_PUT_MCR(x, UART_GET_MCR(x) & ~(ARTS|MRTS))
#define UART_ENABLE_RTS(x) UART_PUT_MCR(x, UART_GET_MCR(x) | MRTS | ARTS)
#define UART_ENABLE_INTS(x, v) UART_SET_IER(x, v)
#define UART_DISABLE_INTS(x) UART_CLEAR_IER(x, 0xF)

#if defined(CONFIG_BFIN_UART0_CTSRTS) || defined(CONFIG_BFIN_UART1_CTSRTS) || \
	defined(CONFIG_BFIN_UART2_CTSRTS) || defined(CONFIG_BFIN_UART3_CTSRTS)
# define CONFIG_SERIAL_BFIN_HARD_CTSRTS
#endif

#define BFIN_UART_TX_FIFO_SIZE	2

/*
 * The pin configuration is different from schematic
 */
struct bfin_serial_port {
        struct uart_port        port;
        unsigned int            old_status;
	int			status_irq;
#ifdef CONFIG_SERIAL_BFIN_DMA
	int			tx_done;
	int			tx_count;
	struct circ_buf		rx_dma_buf;
	struct timer_list       rx_dma_timer;
	int			rx_dma_nrows;
	unsigned int		tx_dma_channel;
	unsigned int		rx_dma_channel;
	struct work_struct	tx_dma_workqueue;
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	int			scts;
	int			cts_pin;
	int			rts_pin;
#endif
};

struct bfin_serial_res {
	unsigned long	uart_base_addr;
	int		uart_irq;
	int		uart_status_irq;
#ifdef CONFIG_SERIAL_BFIN_DMA
	unsigned int	uart_tx_dma_channel;
	unsigned int	uart_rx_dma_channel;
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	int		uart_cts_pin;
	int		uart_rts_pin;
#endif
};

struct bfin_serial_res bfin_serial_resource[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
	0xFFC00400,
	IRQ_UART0_RX,
	IRQ_UART0_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART0_TX,
	CH_UART0_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	0,
	0,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
	0xFFC02000,
	IRQ_UART1_RX,
	IRQ_UART1_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART1_TX,
	CH_UART1_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	GPIO_PE10,
	GPIO_PE9,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART2
	{
	0xFFC02100,
	IRQ_UART2_RX,
	IRQ_UART2_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART2_TX,
	CH_UART2_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	0,
	0,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
	{
	0xFFC03100,
	IRQ_UART3_RX,
	IRQ_UART3_ERROR,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART3_TX,
	CH_UART3_RX,
#endif
#ifdef CONFIG_SERIAL_BFIN_HARD_CTSRTS
	GPIO_PB3,
	GPIO_PB2,
#endif
	},
#endif
};

#define DRIVER_NAME "bfin-uart"
