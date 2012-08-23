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

#ifndef CONFIG_SERIAL_RK_CONSOLE
#if defined(CONFIG_SERIAL_RK29_CONSOLE)
#define CONFIG_SERIAL_RK_CONSOLE
#endif
#endif

#if defined(CONFIG_SERIAL_RK_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
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
*/
#define VERSION_AND_TIME  "rk_serial.c v1.1 2012-08-23"

#define PORT_RK		90
#define UART_USR	0x1F	/* UART Status Register */
#define UART_USR_BUSY (1)
#define UART_IER_PTIME	0x80	/* Programmable THRE Interrupt Mode Enable */
#define UART_LSR_RFE	0x80    /* receive fifo error */
#define UART_SRR		0x22    /* software reset register */
#define UART_RESET		0x01

#define BOTH_EMPTY 	(UART_LSR_TEMT | UART_LSR_THRE)

#define UART_NR	4   //uart port number

/* configurate whether the port transmit-receive by DMA in menuconfig*/
#define OPEN_DMA      1
#define CLOSE_DMA     0

#define TX_DMA (1)
#define RX_DMA (2)

#ifdef CONFIG_UART0_DMA_RK29 
#define UART0_USE_DMA CONFIG_UART0_DMA_RK29
#else
#define UART0_USE_DMA CLOSE_DMA
#endif
#ifdef CONFIG_UART1_DMA_RK29
#define UART1_USE_DMA CONFIG_UART1_DMA_RK29
#else
#define UART1_USE_DMA CLOSE_DMA
#endif
#ifdef CONFIG_UART2_DMA_RK29
#define UART2_USE_DMA CONFIG_UART2_DMA_RK29
#else
#define UART2_USE_DMA CLOSE_DMA
#endif
#ifdef CONFIG_UART3_DMA_RK29
#define UART3_USE_DMA CONFIG_UART3_DMA_RK29
#else
#define UART3_USE_DMA CLOSE_DMA
#endif

#define USE_TIMER    1           // use timer for dma transport
#define POWER_MANEGEMENT 1
#define RX_TIMEOUT		(3000*3)  //uint ms
#define DMA_TX_TRRIGE_LEVEL 128
#define SERIAL_CIRC_CNT_TO_END(xmit)   CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE)


#define USE_DMA (UART0_USE_DMA | UART1_USE_DMA | UART2_USE_DMA | UART3_USE_DMA)
#if USE_DMA
#ifdef CONFIG_ARCH_RK29
#include <mach/dma-pl330.h>
#else
#include <plat/dma-pl330.h>
#endif
#endif



static struct uart_driver serial_rk_reg;

/*
 * Debugging.
 */
#ifdef CONFIG_ARCH_RK29
#define DBG_PORT 1   //DBG_PORT which uart is used to print log message
#else
#define DBG_PORT -1   //DBG_PORT which uart is used to print log message
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

	printascii(buff);
}

//enable log output
#define DEBUG 1
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
	enum dma_ch rx_dmach;
	enum dma_ch tx_dmach;

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

	char			name[12];
	char			fifo[64];
	char 			fifo_size;
	unsigned long		port_activity;
	struct work_struct uart_work;
	struct work_struct uart_work_rx;
	struct workqueue_struct *uart_wq;
#if USE_DMA
	struct rk_uart_dma *dma;
#endif
};

#if USE_DMA
static void serial_rk_release_dma_tx(struct uart_port *port);
static int serial_rk_start_tx_dma(struct uart_port *port);
//static void serial_rk_rx_timeout(unsigned long uart);
static void serial_rk_release_dma_rx(struct uart_port *port);
static int serial_rk_start_rx_dma(struct uart_port *port);
#else
static inline int serial_rk_start_tx_dma(struct uart_port *port) { return 0; }
#endif
static int serial_rk_startup(struct uart_port *port);

static inline unsigned int serial_in(struct uart_rk_port *up, int offset)
{
	offset = offset << 2;
	return __raw_readb(up->port.membase + offset);
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
	__raw_writeb(value, up->port.membase + (offset << 2));
	if (offset != UART_TX)
		dsb();
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
		serial_rk_release_dma_tx(port);
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
		serial_rk_release_dma_rx(port);
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


#if USE_DMA
/*
 * Start transmitting by dma.
 */
#define DMA_SERIAL_BUFFER_SIZE     UART_XMIT_SIZE

/* added by hhb@rock-chips.com  for uart dma transfer*/
static struct rk_uart_dma rk29_uart_ports_dma[] = {
		{UART0_USE_DMA, DMACH_UART0_RX, DMACH_UART0_TX},
		{UART1_USE_DMA, DMACH_UART1_RX, DMACH_UART1_TX},
		{UART2_USE_DMA, DMACH_UART2_RX, DMACH_UART2_TX},
		{UART3_USE_DMA, DMACH_UART3_RX, DMACH_UART3_TX},
};


/* DMAC PL330 add by hhb@rock-chips.com */
static struct rk29_dma_client rk29_uart_dma_client = {
	.name = "rk-uart-dma",
};

/*TX*/

static void serial_rk_release_dma_tx(struct uart_port *port)
{
	struct uart_rk_port *up =
			container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;
	if(!port){
		return;
	}
	if(uart_dma && uart_dma->tx_dma_inited) {
		rk29_dma_free(uart_dma->tx_dmach, &rk29_uart_dma_client);
		uart_dma->tx_dma_inited = 0;
		uart_dma->tx_dma_used = 0;
	}
}

/*this function will be called every time after rk29_dma_enqueue() be invoked*/
static void serial_rk_dma_txcb(void *buf, int size, enum rk29_dma_buffresult result) {
	struct uart_port *port = buf;
	struct uart_rk_port *up = container_of(port, struct uart_rk_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	if(result != RK29_RES_OK){
		printk(">>>>%s:%d result:%d\n", __func__, __LINE__, result);
		up->dma->tx_dma_used = 0;
		return;
	}

	//spin_lock(&(up->dma->rx_lock));
	xmit->tail = (xmit->tail + size) & (UART_XMIT_SIZE - 1);
	port->icount.tx += size;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	//spin_lock(&(up->dma->tx_lock));
	up->dma->tx_dma_used = 0;
	//spin_unlock(&(up->dma->tx_lock));
	serial_rk_enable_ier_thri(up);
	up->port_activity = jiffies;
//	dev_info(up->port.dev, "s:%d\n", size);
}

static int serial_rk_init_dma_tx(struct uart_port *port) {

	struct uart_rk_port *up =
				container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;

	if(!port || !uart_dma){
		dev_info(up->port.dev, "serial_rk_init_dma_tx fail\n");
		return -1;
	}

	if(uart_dma->tx_dma_inited) {
		return 0;
	}

	if (rk29_dma_request(uart_dma->tx_dmach, &rk29_uart_dma_client, NULL) == -EBUSY) {
		dev_info(up->port.dev, "rk29_dma_request tx fail\n");
		return -1;
	}

	if (rk29_dma_set_buffdone_fn(uart_dma->tx_dmach, serial_rk_dma_txcb)) {
		dev_info(up->port.dev, "rk29_dma_set_buffdone_fn tx fail\n");
		return -1;
	}
	
	if (rk29_dma_devconfig(uart_dma->tx_dmach, RK29_DMASRC_MEM, (unsigned long)(port->iobase + UART_TX))) {
		dev_info(up->port.dev, "rk29_dma_devconfig tx fail\n");
		return -1;
	}
	
	if (rk29_dma_config(uart_dma->tx_dmach, 1, 16)) {
		dev_info(up->port.dev, "rk29_dma_config tx fail\n");
		return -1;
	}

	uart_dma->tx_dma_inited = 1;
	dev_info(up->port.dev, "serial_rk_init_dma_tx sucess\n");
	return 0;

}

static int serial_rk_start_tx_dma(struct uart_port *port)
{
	int count = 0;
	struct circ_buf *xmit = &port->state->xmit;
	struct uart_rk_port *up = container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;

	if(!uart_dma->use_dma)
		goto err_out;

	if(-1 == serial_rk_init_dma_tx(port))
		goto err_out;

	if (1 == uart_dma->tx_dma_used)
		return 1;

//	spin_lock(&(uart_dma->tx_lock));
	__stop_tx(up);

	count = SERIAL_CIRC_CNT_TO_END(xmit);
	count -= count%16;
	if(count >= DMA_TX_TRRIGE_LEVEL) {
		if (rk29_dma_enqueue(uart_dma->tx_dmach, port, uart_dma->tx_phy_addr + xmit->tail , count)) {
			goto err_out;
		}
		rk29_dma_ctrl(uart_dma->tx_dmach, RK29_DMAOP_START);
		up->dma->tx_dma_used = 1;
	}
//	spin_unlock(&(uart_dma->tx_lock));
	return 1;
err_out:
	dev_info(up->port.dev, "-serial_rk_start_tx_dma-error-\n");
	return -1;
}



/*RX*/
static void serial_rk_dma_rxcb(void *buf, int size, enum rk29_dma_buffresult result) {

	//printk(">>%s:%d\n", __func__, result);
}

static void serial_rk_release_dma_rx(struct uart_port *port)
{
	struct uart_rk_port *up =
				container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;
	
	if(!port){
		return;
	}
	
	if(uart_dma && uart_dma->rx_dma_inited) {
		del_timer(&uart_dma->rx_timer);
		rk29_dma_free(uart_dma->rx_dmach, &rk29_uart_dma_client);
		uart_dma->rb_tail = 0;
		uart_dma->rx_dma_inited = 0;
		uart_dma->rx_dma_used = 0;
	}
}


static int serial_rk_init_dma_rx(struct uart_port *port) {

	struct uart_rk_port *up =
				container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;

	if(!port || !uart_dma){
		dev_info(up->port.dev, "serial_rk_init_dma_rx: port fail\n");
		return -1;
	}

	if(uart_dma->rx_dma_inited) {
		return 0;
	}

	if (rk29_dma_request(uart_dma->rx_dmach, &rk29_uart_dma_client, NULL) == -EBUSY) {
		dev_info(up->port.dev, "rk29_dma_request fail rx \n");
		return -1;
	}

	if (rk29_dma_set_buffdone_fn(uart_dma->rx_dmach, serial_rk_dma_rxcb)) {
		dev_info(up->port.dev, "rk29_dma_set_buffdone_fn rx fail\n");
		return -1;
	}

	if (rk29_dma_devconfig(uart_dma->rx_dmach, RK29_DMASRC_HW, (unsigned long)(port->iobase + UART_RX))) {
		dev_info(up->port.dev, "rk29_dma_devconfig rx fail\n");
		return -1;
	}

	if (rk29_dma_config(uart_dma->rx_dmach, 1, 1)) {
		dev_info(up->port.dev, "rk29_dma_config rx fail 1 1 \n");
		return -1;
	}

	rk29_dma_setflags(uart_dma->rx_dmach, RK29_DMAF_CIRCULAR);

	uart_dma->rx_dma_inited = 1;
	dev_info(up->port.dev, "serial_rk_init_dma_rx sucess\n");
	return 0;

}

static int serial_rk_start_rx_dma(struct uart_port *port)
{
	struct uart_rk_port *up =
				container_of(port, struct uart_rk_port, port);
	struct rk_uart_dma *uart_dma = up->dma;
	if(!uart_dma->use_dma)
		return 0;

	if(uart_dma->rx_dma_used == 1)
		return 0;

	if(-1 == serial_rk_init_dma_rx(port)){
		dev_info(up->port.dev, "*******serial_rk_init_dma_rx*******error*******\n");
		return -1;
	}

	if (rk29_dma_enqueue(uart_dma->rx_dmach, (void *)up, uart_dma->rx_phy_addr,
			uart_dma->rb_size/2)) {
		dev_info(up->port.dev, "*******rk29_dma_enqueue fail*****\n");
		return -1;
	}

	if (rk29_dma_enqueue(uart_dma->rx_dmach, (void *)up,
			uart_dma->rx_phy_addr+uart_dma->rb_size/2, uart_dma->rb_size/2)) {
		dev_info(up->port.dev, "*******rk29_dma_enqueue fail*****\n");
		return -1;
	}

	rk29_dma_ctrl(uart_dma->rx_dmach, RK29_DMAOP_START);
	uart_dma->rx_dma_used = 1;
	if(uart_dma->use_timer == 1){
		mod_timer(&uart_dma->rx_timer, jiffies + msecs_to_jiffies(uart_dma->rx_timeout));
	}
	up->port_activity = jiffies;
	return 1;
}

static void serial_rk_update_rb_addr(struct uart_rk_port *up){
	dma_addr_t current_pos = 0;
	dma_addr_t rx_current_pos = 0;
	struct rk_uart_dma *uart_dma = up->dma;
	//spin_lock(&(up->dma->rx_lock));
	uart_dma->rx_size = 0;
	if(uart_dma->rx_dma_used == 1){
		rk29_dma_getposition(uart_dma->rx_dmach, &current_pos, &rx_current_pos);
		uart_dma->rb_head = (rx_current_pos - uart_dma->rx_phy_addr);
		uart_dma->rx_size = CIRC_CNT(uart_dma->rb_head, uart_dma->rb_tail, uart_dma->rb_size);
	}
	//spin_unlock(&(up->dma->rx_lock));
}

static void serial_rk_report_dma_rx(unsigned long uart)
{
	int count, flip = 0;
	struct uart_rk_port *up = (struct uart_rk_port *)uart;
	struct rk_uart_dma *uart_dma = up->dma;

	if(!uart_dma->rx_dma_used || !up->port.state->port.tty)
		return;

	serial_rk_update_rb_addr(up);

	while(1) {
		count = CIRC_CNT_TO_END(uart_dma->rb_head, uart_dma->rb_tail, uart_dma->rb_size);
		if(count <= 0)
			break;
		up->port.icount.rx += count;
		flip = tty_insert_flip_string(up->port.state->port.tty, uart_dma->rx_buffer
				+ uart_dma->rb_tail, count);
		tty_flip_buffer_push(up->port.state->port.tty);
		uart_dma->rb_tail = (uart_dma->rb_tail + count) & (uart_dma->rb_size - 1);
		up->port_activity = jiffies;
	}

	//if (uart_dma->rx_size > 0)
	//	printk("rx_size:%d ADDR:%x\n", uart_dma->rx_size, uart_dma->rb_head);

	if(uart_dma->use_timer == 1){
		mod_timer(&uart_dma->rx_timer, jiffies + msecs_to_jiffies(uart_dma->rx_timeout));
	}

}
#if 0
static void serial_rk_rx_timeout(unsigned long uart)
{
	struct uart_rk_port *up = (struct uart_rk_port *)uart;

	serial_rk_report_dma_rx(up);
	//queue_work(up->uart_wq, &up->uart_work);
}

static void serial_rk_report_revdata_workfunc(struct work_struct *work)
{
	struct uart_rk_port *up =
				container_of(work, struct uart_rk_port, uart_work);

	serial_rk_report_dma_rx((unsigned long)up);

	//spin_lock(&(up->dma->rx_lock));

	if(up->port.state->port.tty && up->dma->use_timer != 1 && up->fifo_size > 0){

		tty_insert_flip_string(up->port.state->port.tty, up->fifo, up->fifo_size);
		tty_flip_buffer_push(up->port.state->port.tty);
		up->port.icount.rx += up->fifo_size;
		up->ier |= UART_IER_RDI;
		serial_out(up, UART_IER, up->ier);
	}

	//spin_unlock(&(up->dma->rx_lock));

}


static void serial_rk_start_dma_rx(struct work_struct *work)
{
	struct uart_rk_port *up =
					container_of(work, struct uart_rk_port, uart_work_rx);

	//mod_timer(&up->dma->rx_timer, jiffies + msecs_to_jiffies(up->dma->rx_timeout));
	//rk29_dma_ctrl(up->dma->rx_dmach, RK29_DMAOP_START);
	//serial_rk_start_rx_dma(&up->port);

}
#endif

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
	tty_flip_buffer_push(tty);
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
			serial_rk_start_tx_dma(&up->port);
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

	return (lsr & BOTH_EMPTY) == BOTH_EMPTY ? TIOCSER_TEMT : 0;
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

#if defined(CONFIG_SERIAL_RK_CONSOLE) || defined(CONFIG_CONSOLE_POLL)
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

	wait_for_xmitr(up, BOTH_EMPTY);
	/*
	 *	Send the character out.
	 *	If a LF, also do CR...
	 */
	serial_out(up, UART_TX, c);
	if (c == 10) {
		wait_for_xmitr(up, BOTH_EMPTY);
		serial_out(up, UART_TX, 13);
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up, BOTH_EMPTY);
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

	clk_enable(up->pclk);
	clk_enable(up->clk);  // enable the config uart clock

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
	//if (up->dma->use_dma & TX_DMA)
	//	up->port.state->xmit.buf = NULL;
#endif
	free_irq(up->port.irq, up);
	clk_disable(up->clk);
	clk_disable(up->pclk);
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
				serial_rk_start_rx_dma(&up->port);
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
	struct uart_rk_port *up =
		container_of(port, struct uart_rk_port, port);

	dev_dbg(port->dev, "%s: %s\n", __func__, state ? "disable" : "enable");
	if (state) {
		clk_disable(up->clk);
		clk_disable(up->pclk);
	} else {
		clk_enable(up->pclk);
		clk_enable(up->clk);
	}
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

#ifdef CONFIG_SERIAL_RK_CONSOLE

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
	wait_for_xmitr(up, BOTH_EMPTY);
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
	.driver_name		= "rk29_serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.cons			= SERIAL_CONSOLE,
	.nr			= UART_NR,
};

static int __devinit serial_rk_probe(struct platform_device *pdev)
{
	struct uart_rk_port	*up;
	struct resource		*mem;
	int irq;
	int ret = -ENOSPC;
	
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	if (!request_mem_region(mem->start, (mem->end - mem->start) + 1,
				pdev->dev.driver->name)) {
		dev_err(&pdev->dev, "memory region already claimed\n");
		return -EBUSY;
	}

	up = kzalloc(sizeof(*up), GFP_KERNEL);
	if (up == NULL) {
		ret = -ENOMEM;
		goto do_release_region;
	}

	sprintf(up->name, "rk29_serial.%d", pdev->id);
	up->pdev = pdev;
	up->pclk = clk_get(&pdev->dev, "pclk_uart");
	up->clk = clk_get(&pdev->dev, "uart");
	if (unlikely(IS_ERR(up->clk))) {
		ret = PTR_ERR(up->clk);
		goto do_free;
	}
	up->tx_loadsz = 30;
#if USE_DMA
	up->dma = &rk29_uart_ports_dma[pdev->id];
#endif
	up->port.dev = &pdev->dev;
	up->port.type = PORT_RK;
	up->port.irq = irq;
	up->port.iotype = UPIO_DWAPB;

	up->port.regshift = 2;
	//fifo size default is 32, but it will be updated later when start_up
	up->port.fifosize = 32;
	up->port.ops = &serial_rk_pops;
	up->port.line = pdev->id;
	up->port.iobase = mem->start;
	up->port.membase = ioremap_nocache(mem->start, mem->end - mem->start + 1);
	if (!up->port.membase) {
		ret = -ENOMEM;
		goto do_put_clk;
	}
	up->port.mapbase = mem->start;
	up->port.irqflags = IRQF_DISABLED;
	up->port.uartclk = clk_get_rate(up->clk);

#if USE_DMA
	/* set dma config */
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if(up->dma->use_dma & RX_DMA) {
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		//timer
		up->dma->use_timer = USE_TIMER;
		up->dma->rx_timer.function = serial_rk_report_dma_rx;
		up->dma->rx_timer.data = (unsigned long)up;
		up->dma->rx_timeout = 10;
		up->dma->rx_timer.expires = jiffies + msecs_to_jiffies(up->dma->rx_timeout);
		init_timer(&up->dma->rx_timer);

		//rx buffer
		up->dma->rb_size =  UART_XMIT_SIZE*8;
		up->dma->rx_buffer = dmam_alloc_coherent(up->port.dev, up->dma->rb_size,
				&up->dma->rx_phy_addr, DMA_MEMORY_MAP);
		up->dma->rb_tail = 0;

		if(!up->dma->rx_buffer){
			dev_info(up->port.dev, "dmam_alloc_coherent dma_rx_buffer fail\n");
		}
		else {
			dev_info(up->port.dev, "dma_rx_buffer 0x%08x, phy:0x%08x\n", (unsigned) up->dma->rx_buffer, (unsigned)up->dma->rx_phy_addr);
			//dev_info(up->port.dev, "up 0x%08x\n", (unsigned)up->dma);
		}

		// work queue
		//INIT_WORK(&up->uart_work, serial_rk_report_revdata_workfunc);
		//INIT_WORK(&up->uart_work_rx, serial_rk_start_dma_rx);
		//up->uart_wq = create_singlethread_workqueue("uart_workqueue");
		up->dma->rx_dma_used = 0;
		spin_lock_init(&(up->dma->rx_lock));
		serial_rk_init_dma_rx(&up->port);
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
			dev_info(up->port.dev, "dma_tx_buffer 0x%08x\n", (unsigned) up->dma->tx_buffer);
			dev_info(up->port.dev, "dma_tx_phy 0x%08x\n", (unsigned) up->dma->tx_phy_addr);
		}
		spin_lock_init(&(up->dma->tx_lock));
		serial_rk_init_dma_tx(&up->port);
	}

#endif
	serial_rk_add_console_port(up);
	ret = uart_add_one_port(&serial_rk_reg, &up->port);
	if (ret != 0)
		goto do_iounmap;

	platform_set_drvdata(pdev, up);
	dev_info(&pdev->dev, "membase 0x%08x\n", (unsigned) up->port.membase);

	return 0;

do_iounmap:
	iounmap(up->port.membase);
	up->port.membase = NULL;
do_put_clk:
	clk_put(up->clk);
	clk_put(up->pclk);
do_free:
	kfree(up);
do_release_region:
	release_mem_region(mem->start, (mem->end - mem->start) + 1);
	return ret;
}

static int __devexit serial_rk_remove(struct platform_device *pdev)
{
	struct uart_rk_port *up = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (up) {
		struct resource	*mem;
		destroy_workqueue(up->uart_wq);
		uart_remove_one_port(&serial_rk_reg, &up->port);
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		iounmap(up->port.membase);
		up->port.membase = NULL;
		clk_put(up->clk);
		clk_put(up->pclk);
		kfree(up);
		release_mem_region(mem->start, (mem->end - mem->start) + 1);
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

	return 0;
}

static int serial_rk_resume(struct platform_device *dev)
{
	struct uart_rk_port *up = platform_get_drvdata(dev);

	if (up && up->port.line != DBG_PORT && POWER_MANEGEMENT){
		uart_resume_port(&serial_rk_reg, &up->port);
	}
	if(up->port.line == DBG_PORT && POWER_MANEGEMENT){
		serial_rk_pm(&up->port, 0, 1);
	}
	return 0;
}

static struct platform_driver serial_rk_driver = {
	.probe		= serial_rk_probe,
	.remove		= __devexit_p(serial_rk_remove),
	.suspend	= serial_rk_suspend,
	.resume		= serial_rk_resume,
	.driver		= {
#if defined(CONFIG_ARCH_RK29)
		.name	= "rk29_serial",
#elif defined(CONFIG_SERIAL_RK2818)
		.name	= "rk2818_serial",
#else
		.name	= "rk_serial",
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

