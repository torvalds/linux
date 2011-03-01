/*
 * Blackfin Infra-red Driver
 *
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 *
 */

#include <linux/serial.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>

#include <asm/irq.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/portmux.h>
#include <mach/bfin_serial_5xx.h>
#undef DRIVER_NAME

#ifdef CONFIG_SIR_BFIN_DMA
struct dma_rx_buf {
	char *buf;
	int head;
	int tail;
};
#endif

struct bfin_sir_port {
	unsigned char __iomem   *membase;
	unsigned int            irq;
	unsigned int            lsr;
	unsigned long           clk;
	struct net_device       *dev;
#ifdef CONFIG_SIR_BFIN_DMA
	int                     tx_done;
	struct dma_rx_buf       rx_dma_buf;
	struct timer_list       rx_dma_timer;
	int                     rx_dma_nrows;
#endif
	unsigned int            tx_dma_channel;
	unsigned int            rx_dma_channel;
};

struct bfin_sir_port_res {
	unsigned long   base_addr;
	int             irq;
	unsigned int    rx_dma_channel;
	unsigned int    tx_dma_channel;
};

struct bfin_sir_self {
	struct bfin_sir_port    *sir_port;
	spinlock_t              lock;
	unsigned int            open;
	int                     speed;
	int                     newspeed;

	struct sk_buff          *txskb;
	struct sk_buff          *rxskb;
	struct net_device_stats stats;
	struct device           *dev;
	struct irlap_cb         *irlap;
	struct qos_info         qos;

	iobuff_t                tx_buff;
	iobuff_t                rx_buff;

	struct work_struct      work;
	int                     mtt;
};

#define DRIVER_NAME "bfin_sir"

#define SIR_UART_GET_CHAR(port)    bfin_read16((port)->membase + OFFSET_RBR)
#define SIR_UART_GET_DLL(port)     bfin_read16((port)->membase + OFFSET_DLL)
#define SIR_UART_GET_DLH(port)     bfin_read16((port)->membase + OFFSET_DLH)
#define SIR_UART_GET_LCR(port)     bfin_read16((port)->membase + OFFSET_LCR)
#define SIR_UART_GET_GCTL(port)    bfin_read16((port)->membase + OFFSET_GCTL)

#define SIR_UART_PUT_CHAR(port, v) bfin_write16(((port)->membase + OFFSET_THR), v)
#define SIR_UART_PUT_DLL(port, v)  bfin_write16(((port)->membase + OFFSET_DLL), v)
#define SIR_UART_PUT_DLH(port, v)  bfin_write16(((port)->membase + OFFSET_DLH), v)
#define SIR_UART_PUT_LCR(port, v)  bfin_write16(((port)->membase + OFFSET_LCR), v)
#define SIR_UART_PUT_GCTL(port, v) bfin_write16(((port)->membase + OFFSET_GCTL), v)

#ifdef CONFIG_BF54x
#define SIR_UART_GET_LSR(port)     bfin_read16((port)->membase + OFFSET_LSR)
#define SIR_UART_GET_IER(port)     bfin_read16((port)->membase + OFFSET_IER_SET)
#define SIR_UART_SET_IER(port, v)  bfin_write16(((port)->membase + OFFSET_IER_SET), v)
#define SIR_UART_CLEAR_IER(port, v) bfin_write16(((port)->membase + OFFSET_IER_CLEAR), v)
#define SIR_UART_PUT_LSR(port, v)  bfin_write16(((port)->membase + OFFSET_LSR), v)
#define SIR_UART_CLEAR_LSR(port)   bfin_write16(((port)->membase + OFFSET_LSR), -1)

#define SIR_UART_SET_DLAB(port)
#define SIR_UART_CLEAR_DLAB(port)

#define SIR_UART_ENABLE_INTS(port, v) SIR_UART_SET_IER(port, v)
#define SIR_UART_DISABLE_INTS(port)   SIR_UART_CLEAR_IER(port, 0xF)
#define SIR_UART_STOP_TX(port)     do { SIR_UART_PUT_LSR(port, TFI); SIR_UART_CLEAR_IER(port, ETBEI); } while (0)
#define SIR_UART_ENABLE_TX(port)   do { SIR_UART_SET_IER(port, ETBEI); } while (0)
#define SIR_UART_STOP_RX(port)     do { SIR_UART_CLEAR_IER(port, ERBFI); } while (0)
#define SIR_UART_ENABLE_RX(port)   do { SIR_UART_SET_IER(port, ERBFI); } while (0)
#else

#define SIR_UART_GET_IIR(port)     bfin_read16((port)->membase + OFFSET_IIR)
#define SIR_UART_GET_IER(port)     bfin_read16((port)->membase + OFFSET_IER)
#define SIR_UART_PUT_IER(port, v)  bfin_write16(((port)->membase + OFFSET_IER), v)

#define SIR_UART_SET_DLAB(port)    do { SIR_UART_PUT_LCR(port, SIR_UART_GET_LCR(port) | DLAB); } while (0)
#define SIR_UART_CLEAR_DLAB(port)  do { SIR_UART_PUT_LCR(port, SIR_UART_GET_LCR(port) & ~DLAB); } while (0)

#define SIR_UART_ENABLE_INTS(port, v) SIR_UART_PUT_IER(port, v)
#define SIR_UART_DISABLE_INTS(port)   SIR_UART_PUT_IER(port, 0)
#define SIR_UART_STOP_TX(port)     do { SIR_UART_PUT_IER(port, SIR_UART_GET_IER(port) & ~ETBEI); } while (0)
#define SIR_UART_ENABLE_TX(port)   do { SIR_UART_PUT_IER(port, SIR_UART_GET_IER(port) | ETBEI); } while (0)
#define SIR_UART_STOP_RX(port)     do { SIR_UART_PUT_IER(port, SIR_UART_GET_IER(port) & ~ERBFI); } while (0)
#define SIR_UART_ENABLE_RX(port)   do { SIR_UART_PUT_IER(port, SIR_UART_GET_IER(port) | ERBFI); } while (0)

static inline unsigned int SIR_UART_GET_LSR(struct bfin_sir_port *port)
{
	unsigned int lsr = bfin_read16(port->membase + OFFSET_LSR);
	port->lsr |= (lsr & (BI|FE|PE|OE));
	return lsr | port->lsr;
}

static inline void SIR_UART_CLEAR_LSR(struct bfin_sir_port *port)
{
	port->lsr = 0;
	bfin_read16(port->membase + OFFSET_LSR);
}
#endif

static const unsigned short per[][4] = {
	/* rx pin      tx pin     NULL  uart_number */
	{P_UART0_RX, P_UART0_TX,    0,    0},
	{P_UART1_RX, P_UART1_TX,    0,    1},
	{P_UART2_RX, P_UART2_TX,    0,    2},
	{P_UART3_RX, P_UART3_TX,    0,    3},
};
