/*
 * drivers/serial/rk29_serial.c - driver for rk29 serial device and console
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#if defined(CONFIG_SERIAL_RK29_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include "rk2818_serial.h"

#define DBG_PORT    0
#if 0
#define DBG     printk
#else
#define DBG(...)
#endif
/*
 * We wrap our port structure around the generic uart_port.
 */
struct rk29_port {
	struct uart_port	uart;
	char			name[16];
	struct clk		*clk;
	unsigned int		imr;
};

#define UART_TO_RK29(uart_port)	((struct rk29_port *) uart_port)
#define RK29_SERIAL_MAJOR	 TTY_MAJOR
#define RK29_SERIAL_MINOR	 64      


static inline void rk29_uart_write(struct uart_port *port, unsigned int val,
			     unsigned int off)
{
	__raw_writel(val, port->membase + off);
}

static inline unsigned int rk29_uart_read(struct uart_port *port, unsigned int off)
{
	return __raw_readl(port->membase + off);
}

static int rk29_set_baud_rate(struct uart_port *port, unsigned int baud)
{
	unsigned int uartTemp;
	
	rk29_uart_write(port,rk29_uart_read(port,UART_LCR) | LCR_DLA_EN,UART_LCR);
    uartTemp = port->uartclk / (16 * baud);
    rk29_uart_write(port,uartTemp & 0xff,UART_DLL);
    rk29_uart_write(port,(uartTemp>>8) & 0xff,UART_DLH);
    rk29_uart_write(port,rk29_uart_read(port,UART_LCR) & (~LCR_DLA_EN),UART_LCR);
	return baud;
}

/*
 * 判断发送缓冲区是否为空
 *先以FIFO打开做，后面可以做成FIFO关或FIFO开。
 */
static u_int rk29_serial_tx_empty(struct uart_port *port)
{
    while(!(rk29_uart_read(port,UART_USR)&UART_TRANSMIT_FIFO_EMPTY))
        cpu_relax();
	if(rk29_uart_read(port,UART_USR)&UART_TRANSMIT_FIFO_EMPTY)
	{
        return (1);///1：空
	}else{
        return (0);///0:非空
	}
}

/*
 * Power / Clock management.
 */
static void rk29_serial_pm(struct uart_port *port, unsigned int state,
			    unsigned int oldstate)
{
	struct rk29_port *rk29_port = UART_TO_RK29(port);

	switch (state) {
	case 0:
		/*
		 * Enable the peripheral clock for this serial port.
		 * This is called on uart_open() or a resume event.
		 */
		clk_enable(rk29_port->clk);
		break;
	case 3:
		/*
		 * Disable the peripheral clock for this serial port.
		 * This is called on uart_close() or a suspend event.
		 */
		clk_disable(rk29_port->clk);
		break;
	default:
		printk(KERN_ERR "rk29_serial: unknown pm %d\n", state);
	}
}

/*
 * Return string describing the specified port
 */
static const char *rk29_serial_type(struct uart_port *port)
{
	return (port->type == PORT_RK29) ? "RK29_SERIAL" : NULL;
}

static void rk29_serial_enable_ms(struct uart_port *port)
{
  #ifdef DEBUG_LHH
  printk("Enter::%s\n",__FUNCTION__);
  #endif
}

/* no modem control lines */
static unsigned int rk29_serial_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;
	
	status = rk29_uart_read(port,UART_MSR);
	if (status & UART_MSR_URCTS)
	{			
		result = TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
		printk("UART_GET_MSR:0x%x\n",result);
	}else{			
		result = TIOCM_CAR | TIOCM_DSR;
		printk("UART_GET_MSR:0x%x\n",result);
	}
	return result;
}

static void rk29_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{        
	#ifdef DEBUG_LHH
	printk("Enter::%s\n",__FUNCTION__);
	#endif 
}

/*
 * Stop transmitting.
 */
static void rk29_serial_stop_tx(struct uart_port *port)
{
	#ifdef DEBUG_LHH
	printk("Enter::%s\n",__FUNCTION__);
	#endif
}

/*
 * Start transmitting.
 */
static void rk29_serial_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

    if(DBG_PORT == port->line) {
        DBG("TX:");
    }
    
	while(!(uart_circ_empty(xmit)))
	{
		while (!(rk29_uart_read(port,UART_USR) & UART_TRANSMIT_FIFO_NOT_FULL)){
            rk29_uart_write(port,UART_IER_RECV_DATA_AVAIL_INT_ENABLE|UART_IER_SEND_EMPTY_INT_ENABLE,UART_IER);
            return;
        }
        rk29_uart_write(port,xmit->buf[xmit->tail],UART_THR);
        
        if(DBG_PORT == port->line) {
            DBG("0x%x, ", xmit->buf[xmit->tail]);
        }
        
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
	if((uart_circ_empty(xmit)))
		rk29_uart_write(port,UART_IER_RECV_DATA_AVAIL_INT_ENABLE,UART_IER);
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);	
    
    if(DBG_PORT == port->line) {
        DBG("\n");
    }
}

/*
 * Stop receiving - port is in process of being closed.
 */
static void rk29_serial_stop_rx(struct uart_port *port)
{
    #ifdef DEBUG_LHH
    printk("Enter::%s\n",__FUNCTION__);
    #endif
}

/*
 * Control the transmission of a break signal
 */
static void rk29_serial_break_ctl(struct uart_port *port, int break_state)
{
    unsigned int temp;
    temp = rk29_uart_read(port,UART_LCR);
    if (break_state != 0)       
        temp = temp & (~BREAK_CONTROL_BIT);/* start break */
	else
        temp = temp | BREAK_CONTROL_BIT; /* stop break */
    rk29_uart_write(port,temp,UART_LCR);	
}


/*
 * Characters received (called from interrupt handler)
 */
static void rk29_rx_chars(struct uart_port *port)
{
	unsigned int ch, flag;
    
    if(DBG_PORT == port->line) {
        DBG("RX:");
    }
    
	while((rk29_uart_read(port,UART_USR) & UART_RECEIVE_FIFO_NOT_EMPTY) == UART_RECEIVE_FIFO_NOT_EMPTY)
	{
		u32 lsr = rk29_uart_read(port, UART_LSR);
	    ch = rk29_uart_read(port,UART_RBR);
	    flag = TTY_NORMAL;
		port->icount.rx++;
		if (lsr & UART_BREAK_INT_BIT) {
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
		}
		if (uart_handle_sysrq_char(port, ch))
		{
			continue;
		} 
		uart_insert_char(port, 0, 0, ch, flag);
        
        if(DBG_PORT == port->line) {
            DBG("0x%x, ", ch);
        }
	}
	tty_flip_buffer_push(port->state->port.tty);

    if(DBG_PORT == port->line) {
        DBG("\n");
    }
}

/*
 * Interrupt handler
 */
static irqreturn_t rk29_uart_interrupt(int irq, void *dev_id)
{	
	struct uart_port *port = dev_id;
	unsigned int status, pending;
	
	status = rk29_uart_read(port,UART_IIR); 
	pending = status & 0x0f;
    if((pending == UART_IIR_RECV_AVAILABLE) || (pending == UART_IIR_CHAR_TIMEOUT))
		rk29_rx_chars(port);
	if(pending == UART_IIR_THR_EMPTY)
		rk29_serial_start_tx(port);		
	return IRQ_HANDLED;	
}

/*
 * Disable the port
 */
static void rk29_serial_shutdown(struct uart_port *port)
{
   struct rk29_port *rk29_port = UART_TO_RK29(port);
   rk29_uart_write(port,0x00,UART_IER);
   clk_disable(rk29_port->clk);
   free_irq(port->irq, port);
}
/*
 * Perform initialization and enable port for reception
 */
static int rk29_serial_startup(struct uart_port *port)
{
	struct rk29_port *rk29_port = UART_TO_RK29(port);
	struct tty_struct *tty = port->state->port.tty;	
	int retval;	

    if(2 == port->line) {
        rk29_mux_api_set(GPIO2B1_UART2SOUT_NAME, GPIO2L_UART2_SOUT);
        rk29_mux_api_set(GPIO2B0_UART2SIN_NAME, GPIO2L_UART2_SIN);
        rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME, GPIO2L_UART2_RTS_N);
        rk29_mux_api_set(GPIO2A6_UART2CTSN_NAME, GPIO2L_UART2_CTS_N);
    }
    else if(0 == port->line) {
        rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_UART0_SOUT);
        rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_UART0_SIN);
        rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
        rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_UART0_CTS_N);
    }
    
	retval = request_irq(port->irq,rk29_uart_interrupt,IRQF_SHARED,
		     tty ? tty->name : "rk29_serial",port);
	if(retval)
	{
		printk("\nrk29_serial_startup err \n");	
		rk29_serial_shutdown(port);
		return 	retval;
	}	
	clk_enable(rk29_port->clk);
	rk29_uart_write(port,0xf1,UART_FCR);
	rk29_uart_write(port,0x01,UART_SFE);///enable fifo
    rk29_uart_write(port,UART_IER_RECV_DATA_AVAIL_INT_ENABLE,UART_IER);  //enable uart recevice IRQ
	return 0;
}

/*
 * Change the port parameters
 */
static void rk29_serial_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
    unsigned long flags;
    unsigned int mode, baud;
	unsigned int umcon,fcr;
    /* Get current mode register */
    mode = rk29_uart_read(port,UART_LCR) & (BREAK_CONTROL_BIT | EVEN_PARITY_SELECT | PARITY_ENABLED
                       | ONE_HALF_OR_TWO_BIT | UART_DATABIT_MASK);  
    
    baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);
    /* byte size */
    switch (termios->c_cflag & CSIZE) {
    case CS5:
        mode |= LCR_WLS_5;
        break;
    case CS6:
        mode |= LCR_WLS_6;
        break;
    case CS7:
        mode |= LCR_WLS_7;
        break;
    default:
        mode |= LCR_WLS_8;
        break;
    }
    
    /* stop bits */
    if (termios->c_cflag & CSTOPB)
        mode |= ONE_STOP_BIT;
    
    /* parity */
    if (termios->c_cflag & PARENB) 
    {
        mode |= PARITY_ENABLED;
        if (termios->c_cflag & PARODD)
            mode |= ODD_PARITY;
        else
            mode |= EVEN_PARITY;
    }
    spin_lock_irqsave(&port->lock, flags);
	if(termios->c_cflag & CRTSCTS)                               
	{        
			/*开启uart0硬件流控*/
		printk("start CRTSCTS control and baudrate is %d\n",baud);
		umcon=rk29_uart_read(port,UART_MCR);
		printk("UART_GET_MCR umcon=0x%x\n",umcon);
		umcon |= UART_MCR_AFCEN;
		umcon |= UART_MCR_URRTS;
		umcon &=~UART_SIR_ENABLE;
		rk29_uart_write(port,umcon,UART_MCR);
		printk("UART_GET_MCR umcon=0x%x\n",umcon);
		fcr=rk29_uart_read(port,UART_FCR);
		printk("UART_GET_MCR fcr=0x%x\n",fcr);
		fcr |= UART_FCR_FIFO_ENABLE;
		rk29_uart_write(port,fcr,UART_FCR);		
		printk("UART_GET_MCR fcr=0x%x\n",fcr);
	}
    mode = mode | LCR_DLA_EN;
    while(rk29_uart_read(port,UART_USR)&UART_USR_BUSY)
    	cpu_relax(); 
    rk29_uart_write(port,mode,UART_LCR);
    baud = rk29_set_baud_rate(port, baud);
    uart_update_timeout(port, termios->c_cflag, baud);
    spin_unlock_irqrestore(&port->lock, flags);
}


static void rk29_serial_release_port(struct uart_port *port)
{
    struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *resource;
	resource_size_t size;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		return;
	size = resource->end - resource->start + 1;

	release_mem_region(port->mapbase, size);
	iounmap(port->membase);
	port->membase = NULL;
}

static int rk29_serial_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *resource;
	resource_size_t size;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		return -ENXIO;
	size = resource->end - resource->start + 1;

	if (unlikely(!request_mem_region(port->mapbase, size, "rk29_serial")))
		return -EBUSY;

	port->membase = ioremap(port->mapbase, size);
	if (!port->membase) {
		release_mem_region(port->mapbase, size);
		return -EBUSY;
	}

	return 0;
}		      

/*
 * Configure/autoconfigure the port.
 */
static void rk29_serial_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_RK29;
		rk29_serial_request_port(port);
	}
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int rk29_serial_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_RK29)
		ret = -EINVAL;
	if (port->irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != SERIAL_IO_MEM)
		ret = -EINVAL;
	if (port->uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if ((void *)port->mapbase != ser->iomem_base)
		ret = -EINVAL;
	if (port->iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

#ifdef CONFIG_CONSOLE_POLL
/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

static int rk29_serial_poll_get_char(struct uart_port *port)
{
	while (!((rk29_uart_read(port, UART_USR) & UART_RECEIVE_FIFO_NOT_EMPTY) == UART_RECEIVE_FIFO_NOT_EMPTY))
		barrier();
	return rk29_uart_read(port, UART_RBR);
}

static void rk29_serial_poll_put_char(struct uart_port *port, unsigned char c)
{
	while (!(rk29_uart_read(port, UART_USR) & UART_TRANSMIT_FIFO_NOT_FULL))
		barrier();
	rk29_uart_write(port, c, UART_THR);
}
#endif /* CONFIG_CONSOLE_POLL */

static struct uart_ops rk29_uart_pops = {
	.tx_empty = rk29_serial_tx_empty,
	.set_mctrl = rk29_serial_set_mctrl,
	.get_mctrl = rk29_serial_get_mctrl,
	.stop_tx = rk29_serial_stop_tx,
	.start_tx = rk29_serial_start_tx,
	.stop_rx = rk29_serial_stop_rx,
	.enable_ms = rk29_serial_enable_ms,
	.break_ctl = rk29_serial_break_ctl,
	.startup = rk29_serial_startup,
	.shutdown = rk29_serial_shutdown,
	.set_termios = rk29_serial_set_termios,
	.type = rk29_serial_type,
	.release_port = rk29_serial_release_port,
	.request_port = rk29_serial_request_port,
	.config_port = rk29_serial_config_port,
	.verify_port = rk29_serial_verify_port,
	.pm = rk29_serial_pm,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = rk29_serial_poll_get_char,
	.poll_put_char = rk29_serial_poll_put_char,
#endif
};


static struct rk29_port rk29_uart_ports[] = {
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &rk29_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 32,
			.line = 0,
		},
	},
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &rk29_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 32,
			.line = 1,
		},
	},
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &rk29_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 32,
			.line = 2,
		},
	},
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &rk29_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 32,
			.line = 3,
		},
	},
};

#define UART_NR	ARRAY_SIZE(rk29_uart_ports)

static inline struct uart_port *get_port_from_line(unsigned int line)
{
	return &rk29_uart_ports[line].uart;
}

#ifdef CONFIG_SERIAL_RK29_CONSOLE
static void rk29_console_putchar(struct uart_port *port, int ch)
{
    while (!(rk29_uart_read(port,UART_USR) & UART_TRANSMIT_FIFO_NOT_FULL))
		cpu_relax();
	rk29_uart_write(port,ch,UART_THR);	
}

/*
 * Interrupts are disabled on entering
 */
static void rk29_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port;
	struct rk29_port *rk29_port;

	BUG_ON(co->index < 0 || co->index >= UART_NR);

	port = get_port_from_line(co->index);
	rk29_port = UART_TO_RK29(port);

	spin_lock(&port->lock);
	uart_console_write(port, s, count, rk29_console_putchar);
	spin_unlock(&port->lock);
}

static int __init rk29_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud, flow, bits, parity;
	
	if (unlikely(co->index >= UART_NR || co->index < 0))
		return -ENXIO;

	port = get_port_from_line(co->index);

	if (unlikely(!port->membase))
		return -ENXIO;

	port->cons = co;

	//rk29_init_clock(port);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	bits = 8;
	parity = 'n';
	flow = 'n';	
	rk29_uart_write(port,rk29_uart_read(port,UART_LCR) | LCR_WLS_8 | PARITY_DISABLED | ONE_STOP_BIT,UART_LCR);	/* 8N1 */
	if (baud < 300 || baud > 115200)
		baud = 115200;
	rk29_set_baud_rate(port, baud);

	printk(KERN_INFO "rk29_serial: console setup on port %d\n", port->line);

	/* clear rx fifo, else will blocked on set_termios (always busy) */
	while ((rk29_uart_read(port, UART_USR) & UART_RECEIVE_FIFO_NOT_EMPTY) == UART_RECEIVE_FIFO_NOT_EMPTY)
		rk29_uart_read(port, UART_RBR);

	return uart_set_options(port, co, baud, parity, bits, flow);	
}

static struct uart_driver rk29_uart_driver;

static struct console rk29_console = {
	.name = "ttyS",
	.write = rk29_console_write,
	.device = uart_console_device,
	.setup = rk29_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = 1,  
	.data = &rk29_uart_driver,
};

#define RK29_CONSOLE	(&rk29_console)

#else
#define RK29_CONSOLE	NULL
#endif

static struct uart_driver rk29_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "rk29_serial",
	.dev_name = "ttyS",
	.nr = UART_NR,
	.cons = RK29_CONSOLE,
	.major		= RK29_SERIAL_MAJOR,	
	.minor		= RK29_SERIAL_MINOR,
};

static int __devinit rk29_serial_probe(struct platform_device *pdev)
{
	struct rk29_port *rk29_port;
	struct resource *resource;
	struct uart_port *port;
    //struct rk29_serial_platform_data *pdata = pdev->dev.platform_data;
		
	if (unlikely(pdev->id < 0 || pdev->id >= UART_NR))
		return -ENXIO;

	printk(KERN_INFO "rk29_serial: detected port %d\n", pdev->id);

	//if (pdata && pdata->io_init)
		//pdata->io_init();
    
	port = get_port_from_line(pdev->id);
	port->dev = &pdev->dev;
	rk29_port = UART_TO_RK29(port);

	rk29_port->clk = clk_get(&pdev->dev, "uart");
	if (unlikely(IS_ERR(rk29_port->clk)))
		return PTR_ERR(rk29_port->clk);
	port->uartclk = 24000000; ///clk_get_rate(rk29_port->clk);

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		return -ENXIO;
	port->mapbase = resource->start;

	port->irq = platform_get_irq(pdev, 0);
	if (unlikely(port->irq < 0))
		return -ENXIO;

	platform_set_drvdata(pdev, port);

	return uart_add_one_port(&rk29_uart_driver, port);	
}

static int __devexit rk29_serial_remove(struct platform_device *pdev)
{
	struct rk29_port *rk29_port = platform_get_drvdata(pdev);

	clk_put(rk29_port->clk);

	return 0;
}

static struct platform_driver rk29_platform_driver = {
	.remove = rk29_serial_remove,
	.driver = {
		.name = "rk29_serial",
		.owner = THIS_MODULE,
	},
};

static int __init rk29_serial_init(void)
{
	int ret;
	ret = uart_register_driver(&rk29_uart_driver);
	if (unlikely(ret))
		return ret;

	ret = platform_driver_probe(&rk29_platform_driver, rk29_serial_probe);
	if (unlikely(ret))
		uart_unregister_driver(&rk29_uart_driver);

	printk(KERN_INFO "rk29_serial: driver initialized\n");

	return ret;
}

static void __exit rk29_serial_exit(void)
{
	#ifdef CONFIG_SERIAL_RK29_CONSOLE
	unregister_console(&rk29_console);
	#endif
	platform_driver_unregister(&rk29_platform_driver);
	uart_unregister_driver(&rk29_uart_driver);
}

/*
 * While this can be a module, if builtin it's most likely the console
 * So let's leave module_exit but move module_init to an earlier place
 */
arch_initcall(rk29_serial_init);
module_exit(rk29_serial_exit);

MODULE_AUTHOR("lhh lhh@rock-chips.com");
MODULE_DESCRIPTION("Rockchip rk29 Serial port driver");
MODULE_LICENSE("GPL");


