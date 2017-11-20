/*
 * Driver for RK-UART controller.
 * Based on drivers/tty/serial/8250.c
 *
 * Copyright (C) 2011 Rochchip.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or

 * (at your option) any later version.
 *
 * Author: hhb@rock-chips.com
 * Date: 2011.06.18
 */

#if defined(CONFIG_SERIAL_ROCKCHIP_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/ratelimit.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/nmi.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/dmaengine.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#endif


/*
*			 Driver Version Note
*
*v0.0 : this driver is 2.6.32 kernel driver;
*v0.1 : this driver is 3.0.8 kernel driver;
*v1.0 : 2012-08-09
*		1.modify dma dirver;
*		2.enable Programmable THRE Interrupt Mode, so we can just judge ((up->iir & 0x0f) == 0x02) when transmit
*		3.reset uart and set it to loopback state to ensure setting baud rate sucessfully 
*v1.1 : 2012-08-23
*		1. dma driver:make "when tx dma is only enable" work functionally  
*v1.2 : 2012-08-28
*		1. dma driver:serial rx use new dma interface  rk29_dma_enqueue_ring 
*v1.3 : 2012-12-14
*		1. When enable Programmable THRE Interrupt Mode, in lsr register, only UART_LSR_TEMT means transmit empty, but
		 UART_LSR_THRE doesn't. So, the macro BOTH_EMPTY should be replaced with UART_LSR_TEMT.
*v1.4 : 2013-04-16
*		1.fix bug dma buffer free error
*v1.5 : 2013-10-17
*		1.in some case, set uart rx as gpio interrupt to wake up arm, when arm suspends 
*v1.6 : 2013-11-29
		migrate to kernel3.10,and fit device tree
*v1.7 : 2014-03-03
		DMA use new interfaces, and use some interfaces with devm_ prefix 
*v1.8 : 2014-03-04
*		1.clear receive time out interrupt request in irq handler	
*/
#define VERSION_AND_TIME  "rk_serial.c v1.8 2014-03-04"

#define PORT_RK		90
#define UART_USR	0x1F	/* UART Status Register */
#define UART_USR_TX_FIFO_EMPTY		0x04 /* Transmit FIFO empty */
#define UART_USR_TX_FIFO_NOT_FULL	0x02 /* Transmit FIFO not full */
#define UART_USR_BUSY (1)
#define UART_IER_PTIME	0x80	/* Programmable THRE Interrupt Mode Enable */
#define UART_LSR_RFE	0x80    /* receive fifo error */
#define UART_SRR		0x22    /* software reset register */
#define UART_SFE	0x26	/* Shadow FIFO Enable */
#define UART_RESET		0x01


//#define BOTH_EMPTY 	(UART_LSR_TEMT | UART_LSR_THRE)
#define UART_NR	5   //uart port number


/* configurate whether the port transmit-receive by DMA in menuconfig*/
#define OPEN_DMA      1
#define CLOSE_DMA     0

#define TX_DMA (1)
#define RX_DMA (2)
#define DMA_SERIAL_BUFFER_SIZE     (UART_XMIT_SIZE*2)
#define CONFIG_CLOCK_CTRL  1
//serial wake up 
#ifdef CONFIG_UART0_WAKEUP_RK29 
#define UART0_USE_WAKEUP CONFIG_UART0_WAKEUP_RK29
#else
#define UART0_USE_WAKEUP 0
#endif
#ifdef CONFIG_UART1_WAKEUP_RK29
#define UART1_USE_WAKEUP CONFIG_UART1_WAKEUP_RK29
#else
#define UART1_USE_WAKEUP 0
#endif
#ifdef CONFIG_UART2_WAKEUP_RK29
#define UART2_USE_WAKEUP CONFIG_UART2_WAKEUP_RK29
#else
#define UART2_USE_WAKEUP 0
#endif
#ifdef CONFIG_UART3_WAKEUP_RK29
#define UART3_USE_WAKEUP CONFIG_UART3_WAKEUP_RK29
#else
#define UART3_USE_WAKEUP 0
#endif

#define USE_TIMER    1           // use timer for dma transport
#define POWER_MANEGEMENT 1
#define RX_TIMEOUT		(3000*3)  //uint ms
#define DMA_TX_TRRIGE_LEVEL 128
#define SERIAL_CIRC_CNT_TO_END(xmit)   CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE)

#define USE_DMA  OPEN_DMA

#define USE_WAKEUP (UART0_USE_WAKEUP | UART1_USE_WAKEUP | UART2_USE_WAKEUP | UART3_USE_WAKEUP)

#if USE_WAKEUP
#include <mach/iomux.h>
#include <linux/wakelock.h>
#endif



static struct uart_driver serial_rk_reg;

/*
 * Debugging.
 */
#ifdef CONFIG_ARCH_RK29
#define DBG_PORT 1   //DBG_PORT which uart is used to print log message
#else
#ifndef CONFIG_RK_DEBUG_UART   //DBG_PORT which uart is used to print log message
#define DBG_PORT 2
#else
#define DBG_PORT CONFIG_RK_DEBUG_UART
#endif
#endif

#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define uart_console(port)	((port)->cons && (port)->cons->index == (port)->line)
#else
#define uart_console(port)	(0)
#endif


extern void printascii(const char *);
static void dbg(const char *fmt, ...)
{
	va_list va;
	char buff[256];

	va_start(va, fmt);
	vsprintf(buff, fmt, va);
	va_end(va);

#if defined(CONFIG_DEBUG_LL) || defined(CONFIG_RK_EARLY_PRINTK)
	printascii(buff);
#endif
}

//enable log output
#define DEBUG 0
static int log_port = -1;
module_param(log_port, int, S_IRUGO|S_IWUSR);

#if DEBUG
#define DEBUG_INTR(fmt...)	if (up->port.line == log_port && !uart_console(&up->port)) dbg(fmt)
#else
#define DEBUG_INTR(fmt...)	do { } while (0)
#endif


#if USE_DMA
/* added by hhb@rock-chips.com for uart dma transfer */

struct rk_uart_dma {
	u32 use_dma;            //1:used
	//enum dma_ch rx_dmach;
	//enum dma_ch tx_dmach;

	//receive and transfer buffer
	char * rx_buffer;    //visual memory
	char * tx_buffer;
	dma_addr_t rx_phy_addr;  //physical memory
	dma_addr_t tx_phy_addr;
	u32 rb_size;		 //buffer size
	u32 tb_size;

	//regard the rx buffer as a circular buffer
	u32 rb_head;
	u32 rb_tail;
	u32 rx_size;

	spinlock_t		tx_lock;
	spinlock_t		rx_lock;

	char tx_dma_inited;   //1:dma tx channel has been init
	char rx_dma_inited;	 //1:dma rx channel has been init
	char tx_dma_used;	 //1:dma tx is working
	char rx_dma_used;    //1:dma rx is working

	/* timer to poll activity on rx dma */
	char use_timer;
	int	 rx_timeout;
	struct timer_list rx_timer;

	struct dma_chan		*dma_chan_rx, *dma_chan_tx;
	struct scatterlist	rx_sgl, tx_sgl;
	unsigned int		rx_bytes, tx_bytes;
};
#endif

#if USE_WAKEUP	
struct uart_wake_up {
	unsigned int enable;
	unsigned int rx_mode;
	unsigned int tx_mode;
	unsigned int rx_pin;
	char rx_pin_name[32];
	unsigned int tx_pin;
	unsigned int rx_irq;
	char rx_irq_name[32];
	struct wake_lock wakelock;
	char wakelock_name[32];
};
#endif

#ifdef CONFIG_OF
struct of_rk_serial {
	unsigned int id;
	unsigned int use_dma;	
	unsigned int uartclk;
};
#endif


struct uart_rk_port {
	struct uart_port	port;
	struct platform_device	*pdev;
	struct clk		*clk;
	struct clk		*pclk;
	unsigned int		tx_loadsz;	/* transmit fifo load size */
	unsigned char		ier;
	unsigned char		lcr;
	unsigned char		mcr;
	unsigned char		iir;
	unsigned char		fcr;
	/*
	 * Some bits in registers are cleared on a read, so they must
	 * be saved whenever the register is read but the bits will not
	 * be immediately processed.
	 */
#define LSR_SAVE_FLAGS UART_LSR_BRK_ERROR_BITS
	unsigned char		lsr_saved_flags;
#if 0
#define MSR_SAVE_FLAGS UART_MSR_ANY_DELTA
	unsigned char		msr_saved_flags;
#endif

	char			name[16];
	char			fifo[64];
	char 			fifo_size;
	unsigned long		port_activity;
	struct work_struct uart_work;
	struct work_struct uart_work_rx;
	struct workqueue_struct *uart_wq;
	struct rk_uart_dma *dma;
#if USE_WAKEUP
	struct uart_wake_up *wakeup;
#endif
};

#if USE_DMA
static void serial_rk_release_dma_tx(struct uart_rk_port *up);
static int serial_rk_start_dma_tx(struct uart_rk_port *up);
//static void serial_rk_rx_timeout(unsigned long uart);
static void serial_rk_release_dma_rx(struct uart_rk_port *up);
static int serial_rk_start_dma_rx(struct uart_rk_port *up);
static void serial_rk_stop_dma_tx(struct uart_rk_port *up);
static void serial_rk_stop_dma_rx(struct uart_rk_port *up);

#else
static inline int serial_rk_start_tx_dma(struct uart_port *port) { return 0; }
#endif
static int serial_rk_startup(struct uart_port *port);

static inline unsigned int serial_in(struct uart_rk_port *up, int offset)
{
	offset = offset << 2;

	return __raw_readl(up->port.membase + offset);
}

/* Save the LCR value so it can be re-written when a Busy Detect IRQ occurs. */
static inline void dwapb_save_out_value(struct uart_rk_port *up, int offset,
					unsigned char value)
{
	if (offset == UART_LCR)
		up->lcr = value;
}

/* Read the IER to ensure any interrupt is cleared before returning from ISR. */
static inline void dwapb_check_clear_ier(struct uart_rk_port *up, int offset)
{
	if (offset == UART_TX || offset == UART_IER)
		serial_in(up, UART_IER);
}

static inline void serial_out(struct uart_rk_port *up, int offset, unsigned char value)
{
	dwapb_save_out_value(up, offset, value);
	__raw_writel(value, up->port.membase + (offset << 2));
	if (offset != UART_TX)
		dsb(sy);
	dwapb_check_clear_ier(up, offset);
}

/* Uart divisor latch read */
static inline int serial_dl_read(struct uart_rk_port *up)
{
	return serial_in(up, UART_DLL) | serial_in(up, UART_DLM) << 8;
}

/* Uart divisor latch write */
static int serial_dl_write(struct uart_rk_port *up, unsigned int value)
{
	unsigned int tmout = 100;

	while(!(serial_in(up, UART_LCR) & UART_LCR_DLAB)){
		if (--tmout == 0){
			if(up->port.line != DBG_PORT)
				dbg("set serial.%d baudrate fail with DLAB not set\n", up->port.line);
			return -1;
		}
	}

	tmout = 15000;
	while(serial_in(up, UART_USR) & UART_USR_BUSY){
		if (--tmout == 0){
			if(up->port.line != DBG_PORT)
				dbg("set serial.%d baudrate timeout\n", up->port.line);
			return -1;
		}
		udelay(1);
	}

	serial_out(up, UART_DLL, value & 0xff);
	serial_out(up, UART_DLM, value >> 8 & 0xff);

	return 0;

}

static int serial_lcr_write(struct uart_rk_port *up, unsigned char value)
{
	unsigned int tmout = 15000;

	while(serial_in(up, UART_USR) & UART_USR_BUSY){
		if (--tmout == 0){
			if(up->port.line != DBG_PORT)
				dbg("set serial.%d lc r = 0x%02x timeout\n", up->port.line, value);
			return -1;
		}
		udelay(1);
	}

	serial_out(up, UART_LCR, value);

	return 0;
}

static inline void serial_rk_enable_ier_thri(struct uart_rk_port *up)
{
	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}


static inline void serial_rk_disable_ier_thri(struct uart_rk_port *up)
{
	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static int rk29_uart_dump_register(struct uart_rk_port *up){

	unsigned int reg_value = 0;

	reg_value = serial_in(up, UART_IER);
	dbg("UART_IER = 0x%0x\n", reg_value);
	reg_value = serial_in(up, UART_IIR);
	dbg("UART_IIR = 0x%0x\n", reg_value);
    reg_value = serial_in(up, UART_LSR);
    dbg("UART_LSR = 0x%0x\n", reg_value);
    reg_value = serial_in(up, UART_MSR);
    dbg("UART_MSR = 0x%0x\n", reg_value);
    reg_value = serial_in(up, UART_MCR);
    dbg("UART_MCR = 0x%0x\n", reg_value);
    reg_value = serial_in(up, 0x21);
    dbg("UART_RFL = 0x%0x\n", reg_value);
    reg_value = serial_in(up, UART_LCR);
    dbg("UART_LCR = 0x%0x\n", reg_value);
	return 0;

}

/*
 * FIFO support.
 */
static void serial_rk_clear_fifos(struct uart_rk_port *up)
{
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
		       UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);
}

static inline void __stop_tx(struct uart_rk_port *p)
{
	if (p->ier & UART_IER_THRI) {
		p->ier &= ~UART_IER_THRI;
		serial_out(p, UART_IER, p->ier);
	}
}

static void serial_rk_stop_tx(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
#if USE_DMA
	struct rk_uart_dma *uart_dma = up->dma;
	if(uart_dma->use_dma & TX_DMA){
		serial_rk_stop_dma_tx(up);
	}
#endif
	__stop_tx(up);
}


static void serial_rk_start_tx(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

#if USE_DMA
	if(up->dma->use_dma & TX_DMA) {
		if(!up->dma->tx_dma_used)
			serial_rk_enable_ier_thri(up);
	}else {
		serial_rk_enable_ier_thri(up);
	}
#else
	serial_rk_enable_ier_thri(up);
#endif
}


static void serial_rk_stop_rx(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
#if USE_DMA
	struct rk_uart_dma *uart_dma = up->dma;
	if(uart_dma->use_dma & RX_DMA){
		serial_rk_stop_dma_rx(up);
	}
#endif
	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}


static void serial_rk_enable_ms(struct uart_port *port)
{
	/* no MSR capabilities */
#if 0
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	dev_dbg(port->dev, "%s\n", __func__);
	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
#endif
}

#if USE_WAKEUP
static struct uart_wake_up rk29_uart_ports_wakeup[] = {
		{UART0_USE_WAKEUP, UART0_SIN, UART0_SOUT},
		{UART1_USE_WAKEUP, UART1_SIN, UART1_SOUT},
		{UART2_USE_WAKEUP, UART2_SIN, UART2_SOUT},
		{UART3_USE_WAKEUP, UART3_SIN, UART3_SOUT},
};
#endif

#if USE_DMA
/* DMAC PL330 add by hhb@rock-chips.com */

static void serial_rk_stop_dma_tx(struct uart_rk_port *up)
{
	struct rk_uart_dma *uart_dma = up->dma;
	
	if(uart_dma && uart_dma->tx_dma_used) {
		dmaengine_terminate_all(uart_dma->dma_chan_tx);
		uart_dma->tx_dma_used = 0;
	}
}

static void serial_rk_release_dma_tx(struct uart_rk_port *up)
{
	struct rk_uart_dma *uart_dma = up->dma;

	if(uart_dma && uart_dma->tx_dma_inited) {
		serial_rk_stop_dma_tx(up);
		dma_release_channel(uart_dma->dma_chan_tx);
		uart_dma->dma_chan_tx = NULL;
		uart_dma->tx_dma_inited = 0;
	}
}

static void dma_tx_callback(void *data)
{
	struct uart_port *port = data;
	struct uart_rk_port *up = container_of(port, struct uart_rk_port, port);
	struct circ_buf *xmit = &port->state->xmit;
	struct rk_uart_dma *uart_dma = up->dma;
	struct scatterlist *sgl = &uart_dma->tx_sgl;

	dma_unmap_sg(up->port.dev, sgl, 1, DMA_TO_DEVICE);

	xmit->tail = (xmit->tail + uart_dma->tx_bytes) & (UART_XMIT_SIZE - 1);
	port->icount.tx += uart_dma->tx_bytes;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	//spin_lock(&(up->dma->tx_lock));
	uart_dma->tx_dma_used = 0;
	//spin_unlock(&(up->dma->tx_lock));
	serial_rk_enable_ier_thri(up);
	up->port_activity = jiffies;
//	dev_info(up->port.dev, "s:%d\n", size);

}
static int serial_rk_init_dma_tx(struct uart_rk_port *up) {

	struct dma_slave_config slave_config;
	struct uart_port *port = &up->port;
	struct rk_uart_dma *uart_dma = up->dma;
	int ret;

	if(!uart_dma){
		dev_info(up->port.dev, "serial_rk_init_dma_tx fail\n");
		return -1;
	}

	if(uart_dma->tx_dma_inited) {
		return 0;
	}

	uart_dma->dma_chan_tx = dma_request_slave_channel(port->dev, "tx");
	if (!uart_dma->dma_chan_tx) {
		dev_err(port->dev, "cannot get the TX DMA channel!\n");
		ret = -EINVAL;
	}

	slave_config.direction = DMA_MEM_TO_DEV;	
	slave_config.dst_addr = port->mapbase + UART_TX;	
	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;	
	slave_config.dst_maxburst = 16;
	ret = dmaengine_slave_config(uart_dma->dma_chan_tx, &slave_config);	
	if (ret) {
		dev_err(port->dev, "error in TX dma configuration."); 	
		return ret;
	}

	uart_dma->tx_dma_inited = 1;
	dev_info(port->dev, "serial_rk_init_dma_tx sucess\n");
	return 0;
}

static int serial_rk_start_dma_tx(struct uart_rk_port *up)
{
	int count = 0;
	struct uart_port *port = &up->port;
	struct circ_buf *xmit = &port->state->xmit;
	struct rk_uart_dma *uart_dma = up->dma;
	struct scatterlist *sgl = &uart_dma->tx_sgl;
	struct dma_async_tx_descriptor *desc;
	int ret;

	if(!uart_dma->use_dma)
		goto err_out;

	if(-1 == serial_rk_init_dma_tx(up))
		goto err_out;

	if (1 == uart_dma->tx_dma_used)
		return 1;

//	spin_lock(&(uart_dma->tx_lock));
	__stop_tx(up);

	count = SERIAL_CIRC_CNT_TO_END(xmit);
	count -= count%16;
	if(count >= DMA_TX_TRRIGE_LEVEL) {
		uart_dma->tx_bytes = count;
		sg_init_one(sgl, uart_dma->tx_buffer + xmit->tail, count);
		ret = dma_map_sg(port->dev, sgl, 1, DMA_TO_DEVICE);
		
		if (ret == 0) {
			dev_err(port->dev, "DMA mapping error for TX.\n");	
			return -1;
		}	
		desc = dmaengine_prep_slave_sg(uart_dma->dma_chan_tx, sgl, 1,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);	

		if (!desc) {
			dev_err(port->dev, "We cannot prepare for the TX slave dma!\n");		
			return -1;	
		}	
		desc->callback = dma_tx_callback;	
		desc->callback_param = port;	
		dmaengine_submit(desc);
		dma_async_issue_pending(uart_dma->dma_chan_tx);
		uart_dma->tx_dma_used = 1;
	}
//	spin_unlock(&(uart_dma->tx_lock));
	return 1;
err_out:
	dev_info(up->port.dev, "-serial_rk_start_dma_tx-error-\n");
	return -1;
}



/*RX*/
#if 0
static void serial_rk_dma_rxcb(void *buf, int size, enum rk29_dma_buffresult result) {

	//printk(">>%s:%d\n", __func__, result);
}
#endif

static void serial_rk_stop_dma_rx(struct uart_rk_port *up)
{
	struct rk_uart_dma *uart_dma = up->dma;
	
	if(uart_dma && uart_dma->rx_dma_used) {
		del_timer(&uart_dma->rx_timer);
		dmaengine_terminate_all(uart_dma->dma_chan_rx);	
		uart_dma->rb_tail = 0;
		uart_dma->rx_dma_used = 0;
	}
}


static void serial_rk_release_dma_rx(struct uart_rk_port *up)
{
	struct rk_uart_dma *uart_dma = up->dma;
	
	if(uart_dma && uart_dma->rx_dma_inited) {
		serial_rk_stop_dma_rx(up);
		dma_release_channel(uart_dma->dma_chan_rx);
		uart_dma->dma_chan_rx = NULL;	
		uart_dma->rx_dma_inited = 0;
	}
}


static int serial_rk_init_dma_rx(struct uart_rk_port *up) 
{
	int ret;
	struct uart_port *port = &up->port;
	struct dma_slave_config slave_config;
	struct rk_uart_dma *uart_dma = up->dma;

	if(!uart_dma) {
		dev_info(port->dev, "serial_rk_init_dma_rx: port fail\n");
		return -1;
	}

	if(uart_dma->rx_dma_inited) {
		return 0;
	}

	uart_dma->dma_chan_rx = dma_request_slave_channel(port->dev, "rx");
	if (!uart_dma->dma_chan_rx) {
		dev_err(port->dev, "cannot get the DMA channel.\n");
		return -1;
	}

	slave_config.direction = DMA_DEV_TO_MEM;	
	slave_config.src_addr = port->mapbase + UART_RX;	
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;	
	slave_config.src_maxburst = 1;
	ret = dmaengine_slave_config(uart_dma->dma_chan_rx, &slave_config);	
	if (ret) {
		dev_err(port->dev, "error in RX dma configuration.\n");		
		return ret;	
	}

	uart_dma->rx_dma_inited = 1;
	dev_info(port->dev, "serial_rk_init_dma_rx sucess\n");
	return 0;
}

static int serial_rk_start_dma_rx(struct uart_rk_port *up)
{
	struct uart_port *port = &up->port;
	struct rk_uart_dma *uart_dma = up->dma;	
	struct dma_async_tx_descriptor *desc;	
	
	if(!uart_dma->use_dma)
		return 0;

	if(uart_dma->rx_dma_used == 1)
		return 0;

	if(-1 == serial_rk_init_dma_rx(up)){
		dev_info(up->port.dev, "*******serial_rk_init_dma_rx*******error*******\n");
		return -1;
	}
	desc = dmaengine_prep_dma_cyclic(uart_dma->dma_chan_rx, uart_dma->rx_phy_addr, uart_dma->rb_size, uart_dma->rb_size/2,DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);	

	if (!desc) {
		dev_err(port->dev, "We cannot prepare for the RX slave dma!\n");		
		return -EINVAL;	
	}

	//desc->callback = dma_rx_callback;	
	//desc->callback_param = port;	
	dev_dbg(port->dev, "RX: prepare for the DMA.\n");
	dmaengine_submit(desc);	
	dma_async_issue_pending(uart_dma->dma_chan_rx);

	uart_dma->rx_dma_used = 1;
	if(uart_dma->use_timer == 1){
		mod_timer(&uart_dma->rx_timer, jiffies + msecs_to_jiffies(uart_dma->rx_timeout));
	}
	up->port_activity = jiffies;
	return 1;
}

static void serial_rk_update_rb_addr(struct uart_rk_port *up)
{
	struct rk_uart_dma *uart_dma = up->dma;
	struct dma_tx_state state;	
	//spin_lock(&(up->dma->rx_lock));
	uart_dma->rx_size = 0;
	if(uart_dma->rx_dma_used == 1) {
		dmaengine_tx_status(uart_dma->dma_chan_rx, (dma_cookie_t)0, &state);
		uart_dma->rb_head = (state.residue - uart_dma->rx_phy_addr);
		uart_dma->rx_size = CIRC_CNT(uart_dma->rb_head, uart_dma->rb_tail, uart_dma->rb_size);
	}
	//spin_unlock(&(up->dma->rx_lock));
}

static void serial_rk_report_dma_rx(unsigned long uart)
{
	int count, flip = 0;
	struct uart_rk_port *up = (struct uart_rk_port *)uart;
	struct uart_port *port = &up->port;
	struct rk_uart_dma *uart_dma = up->dma;

	if(!uart_dma->rx_dma_used || !port->state->port.tty)
		return;

	serial_rk_update_rb_addr(up);
	//if (uart_dma->rx_size > 0)
	//	printk("rx_size:%d ADDR:%x\n", uart_dma->rx_size, uart_dma->rb_head);
	while(1) {
		count = CIRC_CNT_TO_END(uart_dma->rb_head, uart_dma->rb_tail, uart_dma->rb_size);
		if(count <= 0)
			break;
		port->icount.rx += count;
		flip = tty_insert_flip_string(&port->state->port, uart_dma->rx_buffer
			+ uart_dma->rb_tail, count);
		tty_flip_buffer_push(&port->state->port);
		uart_dma->rb_tail = (uart_dma->rb_tail + count) & (uart_dma->rb_size - 1);
		up->port_activity = jiffies;
	}

	if(uart_dma->use_timer == 1)
		mod_timer(&uart_dma->rx_timer, jiffies + msecs_to_jiffies(uart_dma->rx_timeout));
}
#endif /* USE_DMA */


static void
receive_chars(struct uart_rk_port *up, unsigned int *status)
{
	struct tty_struct *tty = up->port.state->port.tty;
	unsigned char ch, lsr = *status;
	int max_count = 256;
	char flag;

	do {
		if (likely(lsr & UART_LSR_DR)){
			ch = serial_in(up, UART_RX);
		}
		else
			/*
			 * Intel 82571 has a Serial Over Lan device that will
			 * set UART_LSR_BI without setting UART_LSR_DR when
			 * it receives a break. To avoid reading from the
			 * receive buffer without UART_LSR_DR bit set, we
			 * just force the read character to be 0
			 */
			ch = 0;

		flag = TTY_NORMAL;
		up->port.icount.rx++;

		lsr |= up->lsr_saved_flags;
		up->lsr_saved_flags = 0;

		if (unlikely(lsr & UART_LSR_BRK_ERROR_BITS)) {
			/*
			 * For statistics only
			 */
			if (lsr & UART_LSR_BI) {
				lsr &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (lsr & UART_LSR_PE)
				up->port.icount.parity++;
			else if (lsr & UART_LSR_FE)
				up->port.icount.frame++;
			if (lsr & UART_LSR_OE)
				up->port.icount.overrun++;


			/*
			 * Mask off conditions which should be ignored.
			 */
			lsr &= up->port.read_status_mask;

			if (lsr & UART_LSR_BI) {
				DEBUG_INTR("handling break....");
				flag = TTY_BREAK;
			} else if (lsr & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (lsr & UART_LSR_FE)
				flag = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;

		uart_insert_char(&up->port, lsr, UART_LSR_OE, ch, flag);

ignore_char:
		lsr = serial_in(up, UART_LSR);
	} while ((lsr & (UART_LSR_DR | UART_LSR_BI)) && (max_count-- > 0));
	spin_unlock(&up->port.lock);
	tty_flip_buffer_push(tty->port);
	spin_lock(&up->port.lock);
	*status = lsr;
}

static void transmit_chars(struct uart_rk_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (up->port.x_char) {
		serial_out(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_tx_stopped(&up->port)) {
		__stop_tx(up);
		return;
	}
	if (uart_circ_empty(xmit)) {
		__stop_tx(up);
		return;
	}
#if USE_DMA
	//hhb
	if(up->dma->use_dma & TX_DMA){
		if(SERIAL_CIRC_CNT_TO_END(xmit) >= DMA_TX_TRRIGE_LEVEL){
			serial_rk_start_dma_tx(up);
			return;
		}
	}
#endif
	count = up->port.fifosize - serial_in(up , 0x20);
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	DEBUG_INTR("THRE...");
#if USE_DMA
	up->port_activity = jiffies;
#endif
	if (uart_circ_empty(xmit))
		__stop_tx(up);
}

static unsigned int check_modem_status(struct uart_rk_port *up)
{
	unsigned int status = serial_in(up, UART_MSR);

#if 0
	status |= up->msr_saved_flags;
	up->msr_saved_flags = 0;
	if (status & UART_MSR_ANY_DELTA && up->ier & UART_IER_MSI &&
	    up->port.state != NULL) {
		if (status & UART_MSR_TERI)
			up->port.icount.rng++;
		if (status & UART_MSR_DDSR)
			up->port.icount.dsr++;
		if (status & UART_MSR_DDCD)
			uart_handle_dcd_change(&up->port, status & UART_MSR_DCD);
		if (status & UART_MSR_DCTS)
			uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

		wake_up_interruptible(&up->port.state->port.delta_msr_wait);
	}
#endif

	return status;
}


/*
 * This handles the interrupt from one port.
 */
static void serial_rk_handle_port(struct uart_rk_port *up)
{
	unsigned int status;
	unsigned long flags;
	spin_lock_irqsave(&up->port.lock, flags);

	/* reading UART_LSR can automatically clears PE FE OE bits, except receive fifo error bit*/
	status = serial_in(up, UART_LSR);

	DEBUG_INTR("status = %x...\n", status);
#if USE_DMA
	/* DMA mode enable */
	if(up->dma->use_dma) {

		if (status & UART_LSR_RFE) {
			if(up->port.line != DBG_PORT){
				dev_info(up->port.dev, "error:lsr=0x%x\n", status);
				status = serial_in(up, UART_LSR);
				DEBUG_INTR("error:lsr=0x%x\n", status);
			}
		}

		if (status & 0x02) {
			if(up->port.line != DBG_PORT){
				dev_info(up->port.dev, "error:lsr=0x%x\n", status);
				status = serial_in(up, UART_LSR);
				DEBUG_INTR("error:lsr=0x%x\n", status);
			}
		}

		if(!(up->dma->use_dma & RX_DMA)) {
			if (status & (UART_LSR_DR | UART_LSR_BI)) {
				receive_chars(up, &status);
			} else if ((up->iir & 0x0f) == 0x0c) {
            	serial_in(up, UART_RX);
        	}
		}

		if ((up->iir & 0x0f) == 0x02) {
			transmit_chars(up);
		}
	} else 
#endif
	{   //dma mode disable

		/*
		 * when uart receive a serial of data which doesn't have stop bit and so on, that causes frame error,and
		 * set UART_LSR_RFE to one,what is worse,we couldn't read the data in the receive fifo. So if
		 * wo don't clear this bit and reset the receive fifo, the received data available interrupt would
		 * occur continuously.  added by hhb@rock-chips.com 2011-08-05
		 */

		if (status & UART_LSR_RFE) {
			if(up->port.line != DBG_PORT){
				dev_info(up->port.dev, "error:lsr=0x%x\n", status);
				status = serial_in(up, UART_LSR);
				DEBUG_INTR("error:lsr=0x%x\n", status);
				rk29_uart_dump_register(up);
			}
		}

		if (status & (UART_LSR_DR | UART_LSR_BI)) {
			receive_chars(up, &status);
		} else if ((up->iir & 0x0f) == 0x0c) {
            serial_in(up, UART_RX);
        }
		check_modem_status(up);
		//hhb@rock-chips.com when FIFO and THRE mode both are enabled,and FIFO TX empty trigger is set to larger than 1,
		//,we need to add ((up->iir & 0x0f) == 0x02) to transmit_chars,because when entering interrupt,the FIFO and THR
		//might not be 1.
		if ((up->iir & 0x0f) == 0x02) {
			transmit_chars(up);
		}
	}
	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * This is the serial driver's interrupt routine.
 */

static irqreturn_t serial_rk_interrupt(int irq, void *dev_id)
{
	struct uart_rk_port *up = dev_id;
	int handled = 0;
	unsigned int iir;

	iir = serial_in(up, UART_IIR);

	DEBUG_INTR("%s(%d) iir = 0x%02x\n", __func__, irq, iir);
	up->iir = iir;

	if (!(iir & UART_IIR_NO_INT)) {
		serial_rk_handle_port(up);
		handled = 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {

		/* The DesignWare APB UART has an Busy Detect (0x07)
		 * interrupt meaning an LCR write attempt occured while the
		 * UART was busy. The interrupt must be cleared by reading
		 * the UART status register (USR) and the LCR re-written. */

		if(!(serial_in(up, UART_USR) & UART_USR_BUSY)){
			serial_out(up, UART_LCR, up->lcr);
		}
		handled = 1;
		dbg("the serial.%d is busy\n", up->port.line);
	}
	DEBUG_INTR("end(%d).\n", handled);

	return IRQ_RETVAL(handled);
}

static unsigned int serial_rk_tx_empty(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned long flags;
	unsigned int lsr;

	dev_dbg(port->dev, "%s\n", __func__);
	spin_lock_irqsave(&up->port.lock, flags);
	lsr = serial_in(up, UART_LSR);
	up->lsr_saved_flags |= lsr & LSR_SAVE_FLAGS;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return (lsr & UART_LSR_TEMT) == UART_LSR_TEMT ? TIOCSER_TEMT : 0;
}

static unsigned int serial_rk_get_mctrl(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned int status;
	unsigned int ret;

	status = check_modem_status(up);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	dev_dbg(port->dev, "%s 0x%08x\n", __func__, ret);
	return ret;
}

static void serial_rk_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned char mcr = 0;

	dev_dbg(port->dev, "+%s\n", __func__);
	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	mcr |= up->mcr;

	serial_out(up, UART_MCR, mcr);
	dev_dbg(port->dev, "-serial.%d %s mcr: 0x%02x\n", port->line, __func__, mcr);
}

static void serial_rk_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned long flags;

	dev_dbg(port->dev, "+%s\n", __func__);
	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_lcr_write(up, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
	dev_dbg(port->dev, "-%s lcr: 0x%02x\n", __func__, up->lcr);
}

#if defined(CONFIG_SERIAL_ROCKCHIP_CONSOLE) || defined(CONFIG_CONSOLE_POLL)
/*
 *	Wait for transmitter & holding register to empty
 */
static void wait_for_xmitr(struct uart_rk_port *up, int bits)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	for (;;) {
		status = serial_in(up, UART_LSR);

		up->lsr_saved_flags |= status & LSR_SAVE_FLAGS;

		if ((status & bits) == bits)
			break;
		if (--tmout == 0)
			break;
		udelay(1);
	}
}
#endif

#ifdef CONFIG_CONSOLE_POLL
/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

static int serial_rk_get_poll_char(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned char lsr = serial_in(up, UART_LSR);

	while (!(lsr & UART_LSR_DR))
		lsr = serial_in(up, UART_LSR);

	return serial_in(up, UART_RX);
}

static void serial_rk_put_poll_char(struct uart_port *port,
			 unsigned char c)
{
	unsigned int ier;
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	/*
	 *	First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	wait_for_xmitr(up, UART_LSR_TEMT);
	/*
	 *	Send the character out.
	 *	If a LF, also do CR...
	 */
	serial_out(up, UART_TX, c);
	if (c == 10) {
		wait_for_xmitr(up, UART_LSR_TEMT);
		serial_out(up, UART_TX, 13);
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up, UART_LSR_TEMT);
	serial_out(up, UART_IER, ier);
}

#endif /* CONFIG_CONSOLE_POLL */

static int serial_rk_startup(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned long flags;
	int retval, fifosize = 0;
	

	dev_dbg(port->dev, "%s\n", __func__);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(up->port.irq, serial_rk_interrupt, up->port.irqflags,
				up->name, up);
	if (retval)
		return retval;

	up->mcr = 0;
#ifdef CONFIG_CLOCK_CTRL
	clk_prepare_enable(up->clk);
	clk_prepare_enable(up->pclk); // enable the config uart clock
#endif
	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	serial_rk_clear_fifos(up);

	//read uart fifo size  hhb@rock-chips.com
	fifosize = __raw_readl(up->port.membase + 0xf4);
	up->port.fifosize = ((fifosize >> 16) & 0xff) << 4;
	if(up->port.fifosize <= 0)
		up->port.fifosize = 32;
	//printk("fifo size:%d :%08x\n", up->port.fifosize, fifosize);

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);
	(void) serial_in(up, UART_USR);

	/*
	 * Now, initialize the UART
	 */
	serial_lcr_write(up, UART_LCR_WLEN8 | UART_LCR_EPAR);

	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Most PC uarts need OUT2 raised to enable interrupts.
	 */
//	up->port.mctrl |= TIOCM_OUT2;

	serial_rk_set_mctrl(&up->port, up->port.mctrl);

	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Clear the interrupt registers again for luck, and clear the
	 * saved flags to avoid getting false values from polling
	 * routines or the previous session.
	 */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);
	(void) serial_in(up, UART_USR);
	up->lsr_saved_flags = 0;
#if 0
	up->msr_saved_flags = 0;
#endif
#if USE_DMA
	if (up->dma->use_dma & TX_DMA) {
		if(up->port.state->xmit.buf != up->dma->tx_buffer){
			free_page((unsigned long)up->port.state->xmit.buf);
			up->port.state->xmit.buf = up->dma->tx_buffer;
		}
	} else 
#endif
	{
		up->ier = 0;
		serial_out(up, UART_IER, up->ier);
	}

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */

	return 0;
}


static void serial_rk_shutdown(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned long flags;

	dev_dbg(port->dev, "%s\n", __func__);
	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	serial_out(up, UART_IER, 0);

	spin_lock_irqsave(&up->port.lock, flags);
//	up->port.mctrl &= ~TIOCM_OUT2;
	serial_rk_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_lcr_write(up, serial_in(up, UART_LCR) & ~UART_LCR_SBC);
	serial_rk_clear_fifos(up);

	/*
	 * Read data port to reset things, and then free the irq
	 */
	(void) serial_in(up, UART_RX);
#if USE_DMA
	if (up->dma->use_dma & TX_DMA)
		up->port.state->xmit.buf = NULL;
#endif
	free_irq(up->port.irq, up);
#ifdef CONFIG_CLOCK_CTRL
	clk_disable_unprepare(up->clk);
	clk_disable_unprepare(up->pclk); 
#endif
}

static void
serial_rk_set_termios(struct uart_port *port, struct ktermios *termios,
		      struct ktermios *old)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);
	unsigned char cval = 0, fcr = 0, mcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	dev_dbg(port->dev, "+%s\n", __func__);

#if USE_DMA
	//stop dma tx, which might make the uart be busy while some registers are set
	if(up->dma->tx_dma_used) {
		serial_rk_stop_dma_tx(up);
	}
#endif

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		cval = UART_LCR_WLEN5;
		break;
	case CS6:
		cval = UART_LCR_WLEN6;
		break;
	case CS7:
		cval = UART_LCR_WLEN7;
		break;
	case CS8:
	default:
		cval = UART_LCR_WLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB){
		cval |= UART_LCR_STOP;
	}
	if (termios->c_cflag & PARENB){
		cval |= UART_LCR_PARITY;
	}
	if (!(termios->c_cflag & PARODD)){
		cval |= UART_LCR_EPAR;
	}
#ifdef CMSPAR
	if (termios->c_cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif


	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / 0xffff,
				  port->uartclk / 16);

	quot = uart_get_divisor(port, baud);
	//dev_info(up->port.dev, "uartclk:%d\n", port->uartclk/16);
	//dev_info(up->port.dev, "baud:%d\n", baud);
	//dev_info(up->port.dev, "quot:%d\n", quot);

	if (baud < 2400){
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
	}
	else{
		fcr = UART_FCR_ENABLE_FIFO;
#if USE_DMA
		//added by hhb@rock-chips.com
		if(up->dma->use_dma & TX_DMA){
			fcr |= UART_FCR_T_TRIG_01;
		} else
#endif
		{
			fcr |= UART_FCR_T_TRIG_01;
		}

#if USE_DMA
		//added by hhb@rock-chips.com
		if(up->dma->use_dma & RX_DMA){	
			fcr |= UART_FCR_R_TRIG_00;
		} else
#endif
		{
			if (termios->c_cflag & CRTSCTS)
				fcr |= UART_FCR_R_TRIG_11;
			else
				fcr |= UART_FCR_R_TRIG_00;
		}
	}

	/*
	 * MCR-based auto flow control.  When AFE is enabled, RTS will be
	 * deasserted when the receive FIFO contains more characters than
	 * the trigger, or the MCR RTS bit is cleared.  In the case where
	 * the remote UART is not using CTS auto flow control, we must
	 * have sufficient FIFO entries for the latency of the remote
	 * UART to respond.  IOW, at least 32 bytes of FIFO.
	 */
	up->mcr &= ~UART_MCR_AFE;
	if (termios->c_cflag & CRTSCTS){
		up->mcr |= UART_MCR_AFE;
	}

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
#if 0
	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;
#endif

	//to avoid uart busy when set baud rate  hhb@rock-chips.com
	serial_out(up, UART_SRR, UART_RESET);
	mcr = serial_in(up, UART_MCR);
	serial_out(up, UART_MCR, mcr | 0x10);  //loopback mode
	
	up->lcr = cval;				/* Save LCR */
	/* set DLAB */
	if(serial_lcr_write(up, cval | UART_LCR_DLAB)) {
		if(up->port.line != DBG_PORT)
			dbg("serial.%d set DLAB fail\n", up->port.line);
		serial_out(up, UART_SRR, UART_RESET);
		goto fail;
	}

	/* set uart baud rate */
	if(serial_dl_write(up, quot)) {
		if(up->port.line != DBG_PORT)
			dbg("serial.%d set dll fail\n", up->port.line);
		serial_out(up, UART_SRR, UART_RESET);
		goto fail;
	}

	/* reset DLAB */
	if(serial_lcr_write(up, cval)) {
		if(up->port.line != DBG_PORT)
			dbg("serial.%d reset DLAB fail\n", up->port.line);
		serial_out(up, UART_SRR, UART_RESET);
		goto fail;
	}
	else {
		serial_rk_set_mctrl(&up->port, up->port.mctrl);
		up->fcr = fcr;
		serial_out(up, UART_FCR, up->fcr);		/* set fcr */
		up->ier = 0;
		//start serial receive data
#if USE_DMA
		if (up->dma->use_dma) {
			up->ier |= UART_IER_RLSI;
			up->ier |= UART_IER_PTIME;   //Programmable THRE Interrupt Mode Enable
			if (up->dma->use_dma & RX_DMA)
				serial_rk_start_dma_rx(up);
			else
				up->ier |= UART_IER_RDI;
		} else
#endif
		{
			//  not use dma receive
			up->ier |= UART_IER_RDI;
			up->ier |= UART_IER_RLSI;
			if(up->port.line != DBG_PORT)
				up->ier |= UART_IER_PTIME;   //Programmable THRE Interrupt Mode Enable

		}
		serial_out(up, UART_IER, up->ier);
	}
	
	spin_unlock_irqrestore(&up->port.lock, flags);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
	dev_dbg(port->dev, "-%s baud %d\n", __func__, baud);
	return;

fail:
	spin_unlock_irqrestore(&up->port.lock, flags);

}

#if 0
static void
serial_rk_set_ldisc(struct uart_port *port, int new)
{
	if (new == N_PPS) {
		port->flags |= UPF_HARDPPS_CD;
		serial_rk_enable_ms(port);
	} else
		port->flags &= ~UPF_HARDPPS_CD;
}
#endif

static void
serial_rk_pm(struct uart_port *port, unsigned int state,
	      unsigned int oldstate)
{
#ifdef CONFIG_CLOCK_CTRL
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	dev_dbg(port->dev, "%s: %s\n", __func__, state ? "disable" : "enable");
	if (state) {
	clk_disable_unprepare(up->clk);
	clk_disable_unprepare(up->pclk); 
	} else {
	clk_prepare_enable(up->clk);
	clk_prepare_enable(up->pclk); 
	}
#endif
}

static void serial_rk_release_port(struct uart_port *port)
{
	dev_dbg(port->dev, "%s\n", __func__);
}

static int serial_rk_request_port(struct uart_port *port)
{
	dev_dbg(port->dev, "%s\n", __func__);
	return 0;
}

static void serial_rk_config_port(struct uart_port *port, int flags)
{
	dev_dbg(port->dev, "%s\n", __func__);
	port->type = PORT_RK;
}

static int
serial_rk_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	dev_dbg(port->dev, "%s\n", __func__);
	return -EINVAL;
}

static const char *
serial_rk_type(struct uart_port *port)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	dev_dbg(port->dev, "%s: %s\n", __func__, up->name);
	return up->name;
}

static struct uart_ops serial_rk_pops = {
	.tx_empty	= serial_rk_tx_empty,
	.set_mctrl	= serial_rk_set_mctrl,
	.get_mctrl	= serial_rk_get_mctrl,
	.stop_tx	= serial_rk_stop_tx,
	.start_tx	= serial_rk_start_tx,
	.stop_rx	= serial_rk_stop_rx,
	.enable_ms	= serial_rk_enable_ms,
	.break_ctl	= serial_rk_break_ctl,
	.startup	= serial_rk_startup,
	.shutdown	= serial_rk_shutdown,
	.set_termios	= serial_rk_set_termios,
#if 0
	.set_ldisc	= serial_rk_set_ldisc,
#endif
	.pm		= serial_rk_pm,
	.type		= serial_rk_type,
	.release_port	= serial_rk_release_port,
	.request_port	= serial_rk_request_port,
	.config_port	= serial_rk_config_port,
	.verify_port	= serial_rk_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = serial_rk_get_poll_char,
	.poll_put_char = serial_rk_put_poll_char,
#endif
};

#ifdef CONFIG_SERIAL_ROCKCHIP_CONSOLE

static struct uart_rk_port *serial_rk_console_ports[UART_NR];

static void serial_rk_console_putchar(struct uart_port *port, int ch)
{
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	wait_for_xmitr(up, UART_LSR_THRE);
	serial_out(up, UART_TX, ch);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial_rk_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_rk_port *up = serial_rk_console_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	touch_nmi_watchdog();

	local_irq_save(flags);
	if (up->port.sysrq) {
		/* serial_rk_handle_port() already took the lock */
		locked = 0;
	} else if (oops_in_progress) {
		locked = spin_trylock(&up->port.lock);
	} else
		spin_lock(&up->port.lock);

	/*
	 *	First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);

	serial_out(up, UART_IER, 0);

	uart_console_write(&up->port, s, count, serial_rk_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up, UART_LSR_TEMT);
	serial_out(up, UART_IER, ier);

#if 0
	/*
	 *	The receive handling will happen properly because the
	 *	receive ready bit will still be set; it is not cleared
	 *	on read.  However, modem control will not, we must
	 *	call it if we have saved something in the saved flags
	 *	while processing with interrupts off.
	 */
	if (up->msr_saved_flags)
		check_modem_status(up);
#endif

	if (locked)
		spin_unlock(&up->port.lock);
	local_irq_restore(flags);
}

#ifdef CONFIG_RK_CONSOLE_THREAD
#include <linux/kfifo.h>
#include <linux/kthread.h>
static struct task_struct *console_task;
#define FIFO_SIZE SZ_512K
static DEFINE_KFIFO(fifo, unsigned char, FIFO_SIZE);
static bool console_thread_stop;

static void console_putc(struct uart_rk_port *up, unsigned int c)
{
	while (!(serial_in(up, UART_USR) & UART_USR_TX_FIFO_NOT_FULL))
		cpu_relax();
	serial_out(up, UART_TX, c);
}

static void console_flush(struct uart_rk_port *up)
{
	while (!(serial_in(up, UART_USR) & UART_USR_TX_FIFO_EMPTY))
		cpu_relax();
}

static int console_thread(void *data)
{
	struct uart_rk_port *up = data;
	unsigned char c;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop())
			break;
		set_current_state(TASK_RUNNING);
		while (!console_thread_stop && serial_in(up, UART_SFE) && kfifo_get(&fifo, &c)) {
			console_putc(up, c);
		}
		if (!console_thread_stop)
			console_flush(up);
	}

	return 0;
}

static void console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_rk_port *up = serial_rk_console_ports[co->index];
	unsigned int fifo_count = FIFO_SIZE;
	unsigned char c, r = '\r';

	if (console_thread_stop ||
	    oops_in_progress ||
	    system_state == SYSTEM_HALT ||
	    system_state == SYSTEM_POWER_OFF ||
	    system_state == SYSTEM_RESTART) {
		if (!console_thread_stop) {
			console_thread_stop = true;
			smp_wmb();
			console_flush(up);
			while (fifo_count-- && kfifo_get(&fifo, &c))
				console_putc(up, c);
		}
		while (count--) {
			if (*s == '\n') {
				console_putc(up, r);
			}
			console_putc(up, *s++);
		}
		console_flush(up);
	} else {
		while (count--) {
			if (*s == '\n') {
				kfifo_put(&fifo, &r);
			}
			kfifo_put(&fifo, s++);
		}
		wake_up_process(console_task);
	}
}
#endif

static int __init serial_rk_console_setup(struct console *co, char *options)
{
	struct uart_rk_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (unlikely(co->index >= UART_NR || co->index < 0))
		return -ENODEV;

	if (serial_rk_console_ports[co->index] == NULL)
		return -ENODEV;
	up = serial_rk_console_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

#ifdef CONFIG_RK_CONSOLE_THREAD
	if (!console_task) {
		console_task = kthread_run(console_thread, up, "kconsole");
		if (!IS_ERR(console_task))
			co->write = console_write;
	}
#endif
	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct console serial_rk_console = {
	.name		= "ttyS",
	.write		= serial_rk_console_write,
	.device		= uart_console_device,
	.setup		= serial_rk_console_setup,
	.flags		= CON_PRINTBUFFER | CON_ANYTIME,
	.index		= -1,
	.data		= &serial_rk_reg,
};

static void serial_rk_add_console_port(struct uart_rk_port *up)
{
	serial_rk_console_ports[up->pdev->id] = up;
}

#define SERIAL_CONSOLE	&serial_rk_console
#else
#define SERIAL_CONSOLE	NULL

static inline void serial_rk_add_console_port(struct uart_rk_port *up)
{}

#endif

static struct uart_driver serial_rk_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "rk_serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.cons			= SERIAL_CONSOLE,
	.nr			= UART_NR,
};
#if USE_WAKEUP
static irqreturn_t serial_rk_wakeup_handler(int irq, void *dev) {
	struct uart_rk_port *up = dev;
	struct uart_wake_up *wakeup = up->wakeup;
	if(wakeup->enable == 1) {
		iomux_set(wakeup->rx_mode);
		wake_lock_timeout(&wakeup->wakelock, 3 * HZ);
	}    
	return 0;
}

static int serial_rk_setup_wakeup_irq(struct uart_rk_port *up)
{
	int ret = 0;
	struct uart_wake_up *wakeup = up->wakeup;

	if(wakeup->enable == 1) {
		memset(wakeup->wakelock_name, 0, 32);
		sprintf(wakeup->wakelock_name, "serial.%d_wakelock", up->port.line);
		wake_lock_init(&wakeup->wakelock, WAKE_LOCK_SUSPEND, wakeup->wakelock_name);
		memset(wakeup->rx_pin_name, 0, 32);		
		sprintf(wakeup->rx_pin_name, "UART%d_SIN", up->port.line);
		wakeup->rx_pin = iomux_mode_to_gpio(wakeup->rx_mode);
		ret = gpio_request(wakeup->rx_pin, wakeup->rx_pin_name);
		if (ret) {
			printk("request %s fail ! \n", wakeup->rx_pin_name);
		    return ret;
		}
		gpio_direction_input(wakeup->rx_pin);
		wakeup->rx_irq = gpio_to_irq(wakeup->rx_pin);
		memset(wakeup->rx_irq_name, 0, 32);
		sprintf(wakeup->rx_irq_name, "serial.%d_wake_up_irq", up->port.line);
		ret = request_irq(wakeup->rx_irq, serial_rk_wakeup_handler, IRQF_TRIGGER_FALLING, wakeup->rx_irq_name, up);
		if(ret < 0) {
			printk("%s request fail\n", wakeup->rx_irq_name);
		    return ret;
	 	}
		disable_irq_nosync(wakeup->rx_irq);
		enable_irq_wake(wakeup->rx_irq);
		iomux_set(wakeup->rx_mode);
	}
	return ret;
}

static int serial_rk_enable_wakeup_irq(struct uart_rk_port *up) {
	struct uart_wake_up *wakeup = up->wakeup;
	if(wakeup->enable == 1) {
		iomux_set(wakeup->rx_mode & 0xfff0);
		enable_irq(wakeup->rx_irq);
	}
    return 0;
}

static int serial_rk_disable_wakeup_irq(struct uart_rk_port *up) {
	struct uart_wake_up *wakeup = up->wakeup;
	if(wakeup->enable == 1) {
		disable_irq_nosync(wakeup->rx_irq);
		iomux_set(wakeup->rx_mode);
	}
    return 0;
}

static int serial_rk_remove_wakeup_irq(struct uart_rk_port *up) {
	struct uart_wake_up *wakeup = up->wakeup;
	if(wakeup->enable == 1) {
		//disable_irq_nosync(wakeup->rx_irq);
		free_irq(wakeup->rx_irq, NULL);
		gpio_free(wakeup->rx_pin);
		wake_lock_destroy(&wakeup->wakelock);
	}
    return 0;
}
#endif

#ifdef CONFIG_OF
static int of_rk_serial_parse_dt(struct device_node *np, struct of_rk_serial *rks) 
{
	unsigned int val = 0;
	const char *s = NULL;
	int ret, i = 0;
	rks->id = of_alias_get_id(np, "serial");
	if(!of_property_read_u32(np, "clock-frequency", &val))
		rks->uartclk = val;

#if USE_DMA
	rks->use_dma = 0;
	for(i = 0; i < 2; i++) {
		ret = of_property_read_string_index(np, "dma-names", i, &s);
		if(!ret) {
			if(!strcmp(s, "tx"))
				rks->use_dma |= TX_DMA;
			 else if (!strcmp(s, "rx"))
				rks->use_dma |= RX_DMA;
		}
	}
#endif
	return 0;
}
#endif

static int serial_rk_probe(struct platform_device *pdev)
{
	struct uart_rk_port	*up;
	struct resource		*mem;
	int irq;
	int ret = -ENOSPC;
	struct of_rk_serial rks;

	up = devm_kzalloc(&pdev->dev, sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	up->port.membase = devm_request_and_ioremap(&pdev->dev, mem);
	if (!up->port.membase)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

#ifdef CONFIG_OF
	of_rk_serial_parse_dt(pdev->dev.of_node, &rks);
	pdev->id = rks.id;
#endif

	sprintf(up->name, "rk29_serial.%d", pdev->id);
	up->pdev = pdev;
#ifdef CONFIG_CLOCK_CTRL
	up->pclk = devm_clk_get(&pdev->dev, "pclk_uart");
	up->clk = devm_clk_get(&pdev->dev, "sclk_uart");
	if (unlikely(IS_ERR(up->clk)) || unlikely(IS_ERR(up->pclk))) {
		dev_err(&pdev->dev, "get clock fail\n");
		return -EINVAL;
	}
#endif
	up->tx_loadsz = 30;
#if USE_DMA
	up->dma = devm_kzalloc(&pdev->dev, sizeof(struct rk_uart_dma), GFP_KERNEL);
	if (!up->dma) {
		dev_err(&pdev->dev, "unable to allocate mem\n");
		return -ENOMEM;
	}
	up->dma->use_dma = rks.use_dma;
#endif
#if USE_WAKEUP
	up->wakeup = &rk29_uart_ports_wakeup[pdev->id];
#endif
	up->port.dev = &pdev->dev;
	up->port.type = PORT_RK;
	up->port.irq = irq;
	up->port.iotype = UPIO_MEM;
	
	up->port.regshift = 2;
	//fifo size default is 32, but it will be updated later when start_up
	up->port.fifosize = 32;
	up->port.ops = &serial_rk_pops;
	up->port.line = pdev->id;
	up->port.iobase = mem->start;
	up->port.mapbase = mem->start;
	up->port.irqflags = IRQF_DISABLED;
#if defined(CONFIG_CLOCK_CTRL)
	up->port.uartclk = clk_get_rate(up->clk);
#elif defined(CONFIG_OF)
	up->port.uartclk = rks.uartclk;
#else
	up->port.uartclk = 24000000;
#endif

#if USE_DMA
	/* set dma config */
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if(up->dma->use_dma & RX_DMA) {
		//timer
		up->dma->use_timer = USE_TIMER;
		up->dma->rx_timer.function = serial_rk_report_dma_rx;
		up->dma->rx_timer.data = (unsigned long)up;
		up->dma->rx_timeout = 10;
		up->dma->rx_timer.expires = jiffies + msecs_to_jiffies(up->dma->rx_timeout);
		init_timer(&up->dma->rx_timer);

		//rx buffer
		up->dma->rb_size = DMA_SERIAL_BUFFER_SIZE;
		up->dma->rx_buffer = dmam_alloc_coherent(up->port.dev, up->dma->rb_size,
				&up->dma->rx_phy_addr, DMA_MEMORY_MAP);
		up->dma->rb_tail = 0;
		up->dma->rx_dma_inited = 0;
		up->dma->rx_dma_used = 0;

		if(!up->dma->rx_buffer){
			dev_info(up->port.dev, "dmam_alloc_coherent dma_rx_buffer fail\n");
		}
		else {
			dev_info(up->port.dev, "dma_rx_buffer %p\n", up->dma->rx_buffer);
			dev_info(up->port.dev, "dma_rx_phy 0x%08x\n", (unsigned)up->dma->rx_phy_addr);
		}

		// work queue
		//INIT_WORK(&up->uart_work, serial_rk_report_revdata_workfunc);
		//INIT_WORK(&up->uart_work_rx, serial_rk_start_dma_rx);
		//up->uart_wq = create_singlethread_workqueue("uart_workqueue");
		spin_lock_init(&(up->dma->rx_lock));
		serial_rk_init_dma_rx(up);
	}

	if(up->dma->use_dma & TX_DMA){
		//tx buffer
		up->dma->tb_size = UART_XMIT_SIZE;
		up->dma->tx_buffer = dmam_alloc_coherent(up->port.dev, up->dma->tb_size,
				&up->dma->tx_phy_addr, DMA_MEMORY_MAP);
		if(!up->dma->tx_buffer){
			dev_info(up->port.dev, "dmam_alloc_coherent dma_tx_buffer fail\n");
		}
		else{
			dev_info(up->port.dev, "dma_tx_buffer %p\n", up->dma->tx_buffer);
			dev_info(up->port.dev, "dma_tx_phy 0x%08x\n", (unsigned) up->dma->tx_phy_addr);
		}
		spin_lock_init(&(up->dma->tx_lock));
		serial_rk_init_dma_tx(up);
	}

	
#endif
	serial_rk_add_console_port(up);
	ret = uart_add_one_port(&serial_rk_reg, &up->port);
	if (ret != 0)
		return ret;
	platform_set_drvdata(pdev, up);
	dev_info(&pdev->dev, "membase %p\n", up->port.membase);
#if USE_WAKEUP
	serial_rk_setup_wakeup_irq(up); 
#endif
	return 0;
}

static int serial_rk_remove(struct platform_device *pdev)
{
	struct uart_rk_port *up = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (up) {
#if USE_DMA
		serial_rk_release_dma_tx(up);
		serial_rk_release_dma_rx(up);
#endif
#if USE_WAKEUP
    	serial_rk_remove_wakeup_irq(up);
#endif
		//destroy_workqueue(up->uart_wq);
		uart_remove_one_port(&serial_rk_reg, &up->port);
		up->port.membase = NULL;
	}

	return 0;
}

static int serial_rk_suspend(struct platform_device *dev, pm_message_t state)
{
	struct uart_rk_port *up = platform_get_drvdata(dev);

	if (up && up->port.line != DBG_PORT && POWER_MANEGEMENT){
		uart_suspend_port(&serial_rk_reg, &up->port);
	}
	if(up->port.line == DBG_PORT && POWER_MANEGEMENT){
		serial_rk_pm(&up->port, 1, 0);
	}
#if USE_WAKEUP
    serial_rk_enable_wakeup_irq(up);
#endif
	return 0;
}

static int serial_rk_resume(struct platform_device *dev)
{
	struct uart_rk_port *up = platform_get_drvdata(dev);
#if USE_WAKEUP
    serial_rk_disable_wakeup_irq(up);
#endif
	if (up && up->port.line != DBG_PORT && POWER_MANEGEMENT){
		uart_resume_port(&serial_rk_reg, &up->port);
	}
	if(up->port.line == DBG_PORT && POWER_MANEGEMENT){
		serial_rk_pm(&up->port, 0, 1);
	}
	return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id of_rk_serial_match[] = {
	{ .compatible = "rockchip,serial" },
	{ /* Sentinel */ }
};
#endif
static struct platform_driver serial_rk_driver = {
	.probe		= serial_rk_probe,
	.remove		= serial_rk_remove,
	.suspend	= serial_rk_suspend,
	.resume		= serial_rk_resume,
	.driver		= {
		.name	= "serial",
#ifdef CONFIG_OF
		.of_match_table	= of_rk_serial_match,
#endif
		.owner	= THIS_MODULE,
	},
};

static int __init serial_rk_init(void)
{
	int ret;
	//hhb@rock-chips.com
	printk("%s\n", VERSION_AND_TIME);
	ret = uart_register_driver(&serial_rk_reg);
	if (ret)
		return ret;
	ret = platform_driver_register(&serial_rk_driver);
	if (ret != 0)
		uart_unregister_driver(&serial_rk_reg);
	return ret;
}

static void __exit serial_rk_exit(void)
{
	platform_driver_unregister(&serial_rk_driver);
	uart_unregister_driver(&serial_rk_reg);
}

module_init(serial_rk_init);
module_exit(serial_rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK UART driver");
