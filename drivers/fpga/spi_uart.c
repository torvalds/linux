#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <mach/rk2818_iomap.h>

#include "spi_fpga.h"

#if defined(CONFIG_SPI_UART_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define SPI_UART_TEST 0

static struct tty_driver *spi_uart_tty_driver;
/*------------------------以下是spi2uart变量-----------------------*/

#define UART_NR		1	/* Number of UARTs this driver can handle */

#define UART_XMIT_SIZE	PAGE_SIZE
#define WAKEUP_CHARS	1024

#define circ_empty(circ)	((circ)->head == (circ)->tail)
#define circ_clear(circ)	((circ)->head = (circ)->tail = 0)

#define circ_chars_pending(circ) \
		(CIRC_CNT((circ)->head, (circ)->tail, UART_XMIT_SIZE))

#define circ_chars_free(circ) \
		(CIRC_SPACE((circ)->head, (circ)->tail, UART_XMIT_SIZE))




static struct spi_uart *spi_uart_table[UART_NR];
static DEFINE_SPINLOCK(spi_uart_table_lock);


static int spi_uart_add_port(struct spi_uart *uart)
{
	int index, ret = -EBUSY;

	kref_init(&uart->kref);
	mutex_init(&uart->open_lock);
	spin_lock_init(&uart->write_lock);
	spin_lock_init(&uart->irq_lock);
	
	spin_lock(&spi_uart_table_lock);
	for (index = 0; index < UART_NR; index++) 
	{
		if (!spi_uart_table[index]) {
			uart->index = index;
			//printk("index=%d\n\n",index);
			spi_uart_table[index] = uart;
			ret = 0;
			break;
		}
	}
	spin_unlock(&spi_uart_table_lock);

	return ret;
}

static struct spi_uart *spi_uart_port_get(unsigned index)
{
	struct spi_uart *uart;

	if (index >= UART_NR)
		return NULL;

	spin_lock(&spi_uart_table_lock);
	uart = spi_uart_table[index];
	uart->index = index;
	printk("uart->index=%d\n",uart->index);
	if (uart)
		kref_get(&uart->kref);
	spin_unlock(&spi_uart_table_lock);

	return uart;
}

static void spi_uart_port_destroy(struct kref *kref)
{
	struct spi_uart *uart =
		container_of(kref, struct spi_uart, kref);
	kfree(uart);
}

static void spi_uart_port_put(struct spi_uart *uart)
{
	kref_put(&uart->kref, spi_uart_port_destroy);
}

static void spi_uart_port_remove(struct spi_uart *uart)
{
	struct spi_device *spi;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	BUG_ON(spi_uart_table[uart->index] != uart);

	spin_lock(&spi_uart_table_lock);
	spi_uart_table[uart->index] = NULL;
	spin_unlock(&spi_uart_table_lock);

	/*
	 * We're killing a port that potentially still is in use by
	 * the tty layer. Be careful to arrange for the tty layer to
	 * give up on that port ASAP.
	 * Beware: the lock ordering is critical.
	 */
	mutex_lock(&uart->open_lock);
	//mutex_lock(&port->spi_lock);
	spi = port->spi;

	port->spi = NULL;
	//mutex_unlock(&port->spi_lock);
	if (uart->opened)
		tty_hangup(uart->tty);
	mutex_unlock(&uart->open_lock);

	spi_uart_port_put(uart);
}

static unsigned int spi_uart_get_mctrl(struct spi_uart *uart)
{
	unsigned char status;
	unsigned int ret;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	status = spi_in(port, UART_MSR, SEL_UART);//
	ret = 0;
#if 0
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;	
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
#endif
	if (status & UART_MSR_CTS)
		ret = TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
	DBG("Enter::%s,LINE=%d,ret=0x%x************************\n",__FUNCTION__,__LINE__,ret);
	return ret;
}

static void spi_uart_write_mctrl(struct spi_uart *uart, unsigned int mctrl)
{
	unsigned char mcr = 0;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
#if 1
	if (mctrl & TIOCM_RTS)
	{
		mcr |= UART_MCR_RTS;
		mcr |= UART_MCR_AFE;
	}
	//if (mctrl & TIOCM_DTR)
	//	mcr |= UART_MCR_DTR;
	//if (mctrl & TIOCM_OUT1)
	//	mcr |= UART_MCR_OUT1;
	//if (mctrl & TIOCM_OUT2)
	//	mcr |= UART_MCR_OUT2;
	//if (mctrl & TIOCM_LOOP)
	//	mcr |= UART_MCR_LOOP;
	
#endif
	
	DBG("Enter::%s,LINE=%d,mcr=0x%x\n",__FUNCTION__,__LINE__,mcr);
	spi_out(port, UART_MCR, mcr, SEL_UART);
}

static inline void spi_uart_update_mctrl(struct spi_uart *uart,
					  unsigned int set, unsigned int clear)
{
	unsigned int old;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	old = uart->mctrl;
	uart->mctrl = (old & ~clear) | set;
	if (old != uart->mctrl)
		spi_uart_write_mctrl(uart, uart->mctrl);
}

#define spi_uart_set_mctrl(uart, x)	spi_uart_update_mctrl(uart, x, 0)
#define spi_uart_clear_mctrl(uart, x)	spi_uart_update_mctrl(uart, 0, x)

static void spi_uart_change_speed(struct spi_uart *uart,
				   struct ktermios *termios,
				   struct ktermios *old)
{
	unsigned char cval, fcr = 0;
	unsigned int baud, quot;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
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
	default:
	case CS8:
		cval = UART_LCR_WLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;

	for (;;) {
		baud = tty_termios_baud_rate(termios);
		if (baud == 0)
			baud = 115200;  /* Special case: B0 rate. */
		if (baud <= uart->uartclk)
			break;
		/*
		 * Oops, the quotient was zero.  Try again with the old
		 * baud rate if possible, otherwise default to 115200.
		 */
		termios->c_cflag &= ~CBAUD;
		if (old) {
			termios->c_cflag |= old->c_cflag & CBAUD;
			old = NULL;
		} else
			termios->c_cflag |= B115200;
	}
	//quot = (2 * uart->uartclk + baud) / (2 * baud);
	quot = (uart->uartclk / baud);
	printk("baud=%d,quot=0x%x\n",baud,quot);
	if (baud < 2400)
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
	else
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_R_TRIG_10;

	uart->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		uart->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		uart->read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	uart->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		uart->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		uart->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			uart->ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		uart->ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	uart->ier &= ~UART_IER_MSI;
	if ((termios->c_cflag & CRTSCTS) || !(termios->c_cflag & CLOCAL))
	{
		//uart->ier |= UART_IER_MSI;
		//uart->mcr = UART_MCR_RTS;//mcr = UART_MCR_RTS while start RTSCTS
	}

	uart->lcr = cval;

	spi_out(port, UART_IER, uart->ier, SEL_UART);
	spi_out(port, UART_LCR, cval | UART_LCR_DLAB, SEL_UART);
	spi_out(port, UART_DLL, quot & 0xff, SEL_UART);
	spi_out(port, UART_DLM, quot >> 8, SEL_UART);
	spi_out(port, UART_LCR, cval, SEL_UART);
	spi_out(port, UART_FCR, fcr, SEL_UART);
	DBG("Enter::%s,LINE=%d,baud=%d,uart->ier=0x%x,cval=0x%x,fcr=0x%x,quot=0x%x\n",
		__FUNCTION__,__LINE__,baud,uart->ier,cval,fcr,quot);
	spi_uart_write_mctrl(uart, uart->mctrl);
}

static void spi_uart_start_tx(struct spi_uart *uart)
{
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
#if 1
	//unsigned long flags;
	if (!(uart->ier & UART_IER_THRI)) {
		//spin_lock_irqsave(&uart->write_lock, flags);
		uart->ier |= UART_IER_THRI;
		spi_out(port, UART_IER, uart->ier, SEL_UART);
		//spin_unlock_irqrestore(&uart->write_lock, flags);	
		printk("t,");
	}	
	
	DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
#endif
}

static void spi_uart_stop_tx(struct spi_uart *uart)
{
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	//unsigned long flags;
	if (uart->ier & UART_IER_THRI) {
		//spin_lock_irqsave(&uart->write_lock, flags);
		uart->ier &= ~UART_IER_THRI;
		spi_out(port, UART_IER, uart->ier, SEL_UART);
		//spin_unlock_irqrestore(&uart->write_lock, flags);	
		//printk("p");
	}
	DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
}

static void spi_uart_stop_rx(struct spi_uart *uart)
{
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	uart->ier &= ~UART_IER_RLSI;
	uart->read_status_mask &= ~UART_LSR_DR;
	spi_out(port, UART_IER, uart->ier, SEL_UART);
}

static void spi_uart_receive_chars(struct spi_uart *uart, unsigned int *status)
{
	struct tty_struct *tty = uart->tty;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	unsigned int ch, flag;
	int max_count = 1024;	
	//printk("rx:");
	while (--max_count >0 )
	{
		ch = spi_in(port, UART_RX, SEL_UART);//
		//printk("0x%x,",ch&0xff);
		flag = TTY_NORMAL;
		uart->icount.rx++;
		//--max_count;
#if 1
		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
				        UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				uart->icount.brk++;
			} else if (*status & UART_LSR_PE)
				uart->icount.parity++;
			else if (*status & UART_LSR_FE)
				uart->icount.frame++;
			if (*status & UART_LSR_OE)
				uart->icount.overrun++;

			/*
			 * Mask off conditions which should be ignored.
			 */
			*status &= uart->read_status_mask;
			if (*status & UART_LSR_BI) {
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}
#endif
		if ((*status & uart->ignore_status_mask & ~UART_LSR_OE) == 0)
			tty_insert_flip_char(tty, ch, flag);

		/*
		 * Overrun is special.  Since it's reported immediately,
		 * it doesn't affect the current character.
		 */
		if (*status & ~uart->ignore_status_mask & UART_LSR_OE)
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		*status = spi_in(port, UART_LSR, SEL_UART);
		if(!(*status & UART_LSR_DR))
			break;
	} 
	//printk("\n");
	DBG("Enter::%s,LINE=%d,rx_count=%d********\n",__FUNCTION__,__LINE__,(1024-max_count));
	printk("r%d\n",1024-max_count);
	tty_flip_buffer_push(tty);
	
}

static void spi_uart_transmit_chars(struct spi_uart *uart)
{
	struct circ_buf *xmit = &uart->xmit;
	int count;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	if (uart->x_char) {
		spi_out(port, UART_TX, uart->x_char, SEL_UART);
		uart->icount.tx++;
		uart->x_char = 0;
		printk("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
		return;
	}
	if (circ_empty(xmit) || uart->tty->stopped || uart->tty->hw_stopped) {
		spi_uart_stop_tx(uart);
		DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
		//printk("circ_empty()\n");
		return;
	}
	//printk("tx:");
	count = 32;//
	while(count > 0)
	{
		spi_out(port, UART_TX, xmit->buf[xmit->tail], SEL_UART);
		//printk("0x%x,",xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		uart->icount.tx++;
		--count;
		if (circ_empty(xmit))
		break;	
	}

	//printk("\n");
	DBG("Enter::%s,LINE=%d,tx_count=%d\n",__FUNCTION__,__LINE__,(32-count));
	if (circ_chars_pending(xmit) < WAKEUP_CHARS)
	{	
		tty_wakeup(uart->tty);
		printk("k,");
	}
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	if (circ_empty(xmit))
	{
		DBG("circ_empty(xmit)\n");
		spi_uart_stop_tx(uart);
		printk("e,");
	}
	
	printk("t%d\n",32-count);

	DBG("uart->tty->hw_stopped = %d\n",uart->tty->hw_stopped);
}

#if 0
static void spi_uart_check_modem_status(struct spi_uart *uart)
{
	int status;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	status = spi_in(port, UART_MSR, SEL_UART);//
	DBG("Enter::%s,LINE=%d,status=0x%x*******\n",__FUNCTION__,__LINE__,status);
	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		uart->icount.rng++;
	if (status & UART_MSR_DDSR)
		uart->icount.dsr++;
	if (status & UART_MSR_DDCD)
		uart->icount.dcd++;
	if (status & UART_MSR_DCTS) {
		uart->icount.cts++;
		if (uart->tty->termios->c_cflag & CRTSCTS) {
			int cts = (status & UART_MSR_CTS);
			if (uart->tty->hw_stopped) {
				if (cts) {
					uart->tty->hw_stopped = 0;
					spi_uart_start_tx(uart);
					DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
					tty_wakeup(uart->tty);
				}
			} else {
				if (!cts) {
					uart->tty->hw_stopped = 1;
					DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
					spi_uart_stop_tx(uart);
				}
			}
		}
	}
	DBG("Enter::%s,LINE=%d,status=0x%x*******\n",__FUNCTION__,__LINE__,status);
}
#endif


#if SPI_UART_TEST
#define UART_TEST_LEN 16	//8bit
unsigned char buf_test_uart[UART_TEST_LEN];
unsigned int ice65l08_init_para[]=
{
	0x030083,					
	0x010000,
	0x000034,					// (0100XYH) 设置分频系数：XY =  MCLK / (4*波特率)；
	0x030003,					// 设置字节有效长度： 8 bits；
	0x01000f,					// TX RX 中断
	0x020080,					// 设置触发等级   接受FIFO为 16bytes 产生中断；
};

void spi_uart_work_handler(struct work_struct *work)
{
	int i;
	int ret,count;
	int offset,value;
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, uart.spi_uart_work);
	printk("*************test spi_uart now***************\n");

	for(i=0;i<UART_TEST_LEN;i++)
	buf_test_uart[i] = '0'+i;	

	for(i =0; i<sizeof(ice65l08_init_para)/sizeof(ice65l08_init_para[0]); i++)
	{
		offset = (ice65l08_init_para[i] >> 16) & 0xff;
		value = ice65l08_init_para[i] & 0xffff;
		spi_out(port, offset, value, SEL_UART);
	}	
	
	count = UART_TEST_LEN;
	while(count > 0)
	{
		spi_out(port, UART_TX, buf_test_uart[UART_TEST_LEN-count], SEL_UART);
		--count;
	}
}

static void spi_testuart_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->uart.uart_timer.expires  = jiffies + msecs_to_jiffies(1000);
	add_timer(&port->uart.uart_timer);
	//schedule_work(&port->gpio.spi_gpio_work);
	queue_work(port->uart.spi_uart_workqueue, &port->uart.spi_uart_work);
}

#endif




/*
 * This handles the interrupt from one port.
 */
void spi_uart_handle_irq(struct spi_device *spi)
{
	struct spi_fpga_port *port = spi_get_drvdata(spi);
	struct spi_uart *uart = &port->uart;
	unsigned int uart_iir, lsr;

	if (unlikely(uart->in_spi_uart_irq == current))
		return;
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);

	/*
	 * In a few places spi_uart_handle_irq() is called directly instead of
	 * waiting for the actual interrupt to be raised and the SPI IRQ
	 * thread scheduled in order to reduce latency.  However, some
	 * interaction with the tty core may end up calling us back
	 * (serial echo, flow control, etc.) through those same places
	 * causing undesirable effects.  Let's stop the recursion here.
	 */
	 
	uart_iir = spi_in(port, UART_IIR, SEL_UART);//	
	if (uart_iir & UART_IIR_NO_INT)
		return;
	
	DBG("iir=0x%x\n",uart_iir);
		
	uart->in_spi_uart_irq = current;
	lsr = spi_in(port, UART_LSR, SEL_UART);//
	DBG("lsr=0x%x\n",lsr);

	if (lsr & UART_LSR_DR)
	//if (((uart_iir & UART_IIR_RDI) | (uart_iir & UART_IIR_RLSI)) &&  (lsr & UART_LSR_DR))
	{
		DBG("Enter::%s,LINE=%d,lsr & UART_LSR_DR************\n",__FUNCTION__,__LINE__);
		spi_uart_receive_chars(uart, &lsr);	
	}

	//spi_uart_check_modem_status(uart);
	
	
	if (lsr & UART_LSR_THRE)
	//if ((uart_iir & UART_IIR_THRI)&&(lsr & UART_LSR_THRE))
	{
		DBG("Enter::%s,LINE=%d,ICE_STATUS_TXF == 0************\n",__FUNCTION__,__LINE__);
		spi_uart_transmit_chars(uart);
	}

	uart->in_spi_uart_irq = NULL;

	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	
}

static int spi_uart_startup(struct spi_uart *uart)
{
	unsigned long page;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	/*
	 * Set the TTY IO error marker - we will only clear this
	 * once we have successfully opened the port.
	 */
	set_bit(TTY_IO_ERROR, &uart->tty->flags);

	/* Initialise and allocate the transmit buffer. */
	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	uart->xmit.buf = (unsigned char *)page;
	circ_clear(&uart->xmit);

	//mutex_lock(&port->spi_lock);

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in spi_change_speed())
	 */
	spi_out(port, UART_FCR, UART_FCR_ENABLE_FIFO, SEL_UART);
	spi_out(port, UART_FCR, UART_FCR_ENABLE_FIFO |
			UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, SEL_UART);
	spi_out(port, UART_FCR, 0, SEL_UART);

	/*
	 * Clear the interrupt registers.
	 */
	(void) spi_in(port, UART_LSR, SEL_UART);//
	(void) spi_in(port, UART_RX, SEL_UART);//
	(void) spi_in(port, UART_IIR, SEL_UART);//
	(void) spi_in(port, UART_MSR, SEL_UART);//

	/*
	 * Now, initialize the UART
	 */
	spi_out(port, UART_LCR, UART_LCR_WLEN8, SEL_UART);

	uart->ier = UART_IER_RLSI | UART_IER_RDI;
	//uart->ier = UART_IER_RLSI | UART_IER_RDI | UART_IER_RTOIE | UART_IER_UUE;
	uart->mctrl = TIOCM_OUT2;

	spi_uart_change_speed(uart, uart->tty->termios, NULL);

	if (uart->tty->termios->c_cflag & CBAUD)
		spi_uart_set_mctrl(uart, TIOCM_RTS | TIOCM_DTR);

	if (uart->tty->termios->c_cflag & CRTSCTS)
		if (!(spi_uart_get_mctrl(uart) & TIOCM_CTS))
			uart->tty->hw_stopped = 1;

	clear_bit(TTY_IO_ERROR, &uart->tty->flags);
	DBG("Enter::%s,LINE=%d,uart->ier=0x%x\n",__FUNCTION__,__LINE__,uart->ier);
	/* Kick the IRQ handler once while we're still holding the host lock */
	//spi_uart_handle_irq(port->spi);
	//mutex_unlock(&port->spi_lock);
	return 0;

//err1:
	//free_page((unsigned long)uart->xmit.buf);
	//return ret;
}

static void spi_uart_shutdown(struct spi_uart *uart)
{
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	//mutex_lock(&port->spi_lock);
	spi_uart_stop_rx(uart);

	/* TODO: wait here for TX FIFO to drain */

	/* Turn off DTR and RTS early. */
	if (uart->tty->termios->c_cflag & HUPCL)
		spi_uart_clear_mctrl(uart, TIOCM_DTR | TIOCM_RTS);

	 /* Disable interrupts from this port */

	uart->ier = 0;
	spi_out(port, UART_IER, 0, SEL_UART);

	spi_uart_clear_mctrl(uart, TIOCM_OUT2);

	/* Disable break condition and FIFOs. */
	uart->lcr &= ~UART_LCR_SBC;
	spi_out(port, UART_LCR, uart->lcr, SEL_UART);
	spi_out(port, UART_FCR, UART_FCR_ENABLE_FIFO |
				 UART_FCR_CLEAR_RCVR |
				 UART_FCR_CLEAR_XMIT, SEL_UART);
	spi_out(port, UART_FCR, 0, SEL_UART);

	//mutex_unlock(&port->spi_lock);

//skip:
	/* Free the transmit buffer page. */
	free_page((unsigned long)uart->xmit.buf);
}

static int spi_uart_open (struct tty_struct *tty, struct file * filp)
{
	struct spi_uart *uart;
	int ret;
	
	uart = spi_uart_port_get(tty->index);
	if (!uart)
	{
		DBG("Enter::%s,LINE=%d,!port\n",__FUNCTION__,__LINE__);
		return -ENODEV;
	}
	DBG("Enter::%s,LINE=%d,tty->index=%d\n",__FUNCTION__,__LINE__,tty->index);
	mutex_lock(&uart->open_lock);

	/*
	 * Make sure not to mess up with a dead port
	 * which has not been closed yet.
	 */
	if (tty->driver_data && tty->driver_data != uart) {
		mutex_unlock(&uart->open_lock);
		spi_uart_port_put(uart);
		DBG("Enter::%s,LINE=%d,!= uart\n",__FUNCTION__,__LINE__);
		return -EBUSY;
	}

	if (!uart->opened) {
		tty->driver_data = uart;
		uart->tty = tty;
		ret = spi_uart_startup(uart);
		if (ret) {
			tty->driver_data = NULL;
			uart->tty = NULL;
			mutex_unlock(&uart->open_lock);
			spi_uart_port_put(uart);
			DBG("Enter::%s,LINE=%d,ret=%d\n",__FUNCTION__,__LINE__,ret);
			return ret;
		}
	}
	uart->opened++;
	DBG("Enter::%s,uart->opened++=%d\n",__FUNCTION__,uart->opened);
	mutex_unlock(&uart->open_lock);
	return 0;
}

static void spi_uart_close(struct tty_struct *tty, struct file * filp)
{
	struct spi_uart *uart = tty->driver_data;
	printk("Enter::%s,LINE=%d,tty->hw_stopped=%d\n",__FUNCTION__,__LINE__,tty->hw_stopped);
	if (!uart)
		return;

	mutex_lock(&uart->open_lock);
	BUG_ON(!uart->opened);

	/*
	 * This is messy.  The tty layer calls us even when open()
	 * returned an error.  Ignore this close request if tty->count
	 * is larger than uart->count.
	 */
	if (tty->count > uart->opened) {
		mutex_unlock(&uart->open_lock);
		return;
	}

	if (--uart->opened == 0) {
		DBG("Enter::%s,opened=%d\n",__FUNCTION__,uart->opened);
		tty->closing = 1;
		spi_uart_shutdown(uart);
		tty_ldisc_flush(tty);
		uart->tty = NULL;
		tty->driver_data = NULL;
		tty->closing = 0;
	}
	spi_uart_port_put(uart);
	mutex_unlock(&uart->open_lock);

}

static int spi_uart_write(struct tty_struct * tty, const unsigned char *buf,
			   int count)
{
	struct spi_uart *uart = tty->driver_data;
	struct circ_buf *circ = &uart->xmit;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	int c, ret = 0;

	if (!port->spi)
	{
		printk("spi error!!!!\n");
		return -ENODEV;
	}

	
	DBG("spi_uart_write 1 circ->head=%d,circ->tail=%d\n",circ->head,circ->tail);

	spin_lock(&uart->write_lock);
	while (1) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(circ->buf + circ->head, buf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}
	spin_unlock(&uart->write_lock);
	
#if 1
	if ( !(uart->ier & UART_IER_THRI)) {
		//mutex_lock(&port->spi_lock);
			DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
			/*Note:ICE65L08 output a 'Transmitter holding register interrupt' after 1us*/
			//printk("s,");
			spi_uart_start_tx(uart);
			spi_uart_handle_irq(port->spi);
		//mutex_unlock(&port->spi_lock);	
	}	
#endif	

	//printk("w%d\n",ret);
	return ret;
}

static int spi_uart_write_room(struct tty_struct *tty)
{
	struct spi_uart *uart = tty->driver_data;
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	return uart ? circ_chars_free(&uart->xmit) : 0;
}

static int spi_uart_chars_in_buffer(struct tty_struct *tty)
{
	struct spi_uart *uart = tty->driver_data;
	printk("Enter::%s,LINE=%d,circ=%ld****\n",__FUNCTION__,__LINE__,circ_chars_pending(&uart->xmit));	
	return uart ? circ_chars_pending(&uart->xmit) : 0;
}

static void spi_uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct spi_uart *uart = tty->driver_data;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	printk("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	uart->x_char = ch;
	if (ch && !(uart->ier & UART_IER_THRI)) {
		//mutex_lock(&port->spi_lock);
		spi_uart_start_tx(uart);
		printk("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
		spi_uart_handle_irq(port->spi);
		//mutex_unlock(&port->spi_lock);		
	}
}

static void spi_uart_throttle(struct tty_struct *tty)
{
	struct spi_uart *uart = tty->driver_data;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	printk("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	if (!I_IXOFF(tty) && !(tty->termios->c_cflag & CRTSCTS))
		return;
	printk("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	//mutex_lock(&port->spi_lock);
	if (I_IXOFF(tty)) {
		uart->x_char = STOP_CHAR(tty);
		spi_uart_start_tx(uart);
		DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
	}

	if (tty->termios->c_cflag & CRTSCTS)
		spi_uart_clear_mctrl(uart, TIOCM_RTS);

	spi_uart_handle_irq(port->spi);
	//mutex_unlock(&port->spi_lock);
}

static void spi_uart_unthrottle(struct tty_struct *tty)
{
	struct spi_uart *uart = tty->driver_data;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart);
	printk("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	if (!I_IXOFF(tty) && !(tty->termios->c_cflag & CRTSCTS))
		return;
	//mutex_lock(&port->spi_lock);
	if (I_IXOFF(tty)) {
		if (uart->x_char) {
			uart->x_char = 0;
		} else {
			uart->x_char = START_CHAR(tty);
			spi_uart_start_tx(uart);
			DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
		}
	}

	if (tty->termios->c_cflag & CRTSCTS)
		spi_uart_set_mctrl(uart, TIOCM_RTS);

	spi_uart_handle_irq(port->spi);
	//mutex_unlock(&port->spi_lock);
}

static void spi_uart_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct spi_uart *uart = tty->driver_data;
	unsigned int cflag = tty->termios->c_cflag;
	unsigned int mask = TIOCM_DTR;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	
#define RELEVANT_IFLAG(iflag)   ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	if ((cflag ^ old_termios->c_cflag) == 0 &&
	    RELEVANT_IFLAG(tty->termios->c_iflag ^ old_termios->c_iflag) == 0)
		return;

	//mutex_lock(&port->spi_lock);
	spi_uart_change_speed(uart, tty->termios, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD)){
		DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
		spi_uart_clear_mctrl(uart, TIOCM_RTS | TIOCM_DTR);
		//spi_uart_clear_mctrl(uart, TIOCM_RTS);
		}
	
	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
		if (!(cflag & CRTSCTS) || !test_bit(TTY_THROTTLED, &tty->flags))
			mask |= TIOCM_RTS;
		spi_uart_set_mctrl(uart, mask);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !(cflag & CRTSCTS)) {
		DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
		tty->hw_stopped = 0;
		spi_uart_start_tx(uart);
		DBG("Enter::%s,UART_IER=0x%x\n",__FUNCTION__,uart->ier);
	}

	/* Handle turning on CRTSCTS */
	if (!(old_termios->c_cflag & CRTSCTS) && (cflag & CRTSCTS)) {
		DBG("Enter::%s,LINE=%d,status=0x%x,Handle turning on CRTSCTS*****************\n",__FUNCTION__,__LINE__,spi_uart_get_mctrl(uart));
		//spi_uart_set_mctrl(uart, TIOCM_RTS);
		if (!(spi_uart_get_mctrl(uart) & TIOCM_CTS)) {
			DBG("Enter::%s,LINE=%d,tty->hw_stopped = 1********\n",__FUNCTION__,__LINE__);
			tty->hw_stopped = 1;
			spi_uart_stop_tx(uart);
		}
	}
	//mutex_unlock(&port->spi_lock);

}

static int spi_uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct spi_uart *uart = tty->driver_data;
	struct spi_fpga_port *port = container_of(uart, struct spi_fpga_port, uart); 
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	//mutex_lock(&port->spi_lock);
	if (break_state == -1)
		uart->lcr |= UART_LCR_SBC;
	else
		uart->lcr &= ~UART_LCR_SBC;
	spi_out(port, UART_LCR, uart->lcr, SEL_UART);
	//mutex_unlock(&port->spi_lock);
	return 0;
}

static int spi_uart_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct spi_uart *uart = tty->driver_data;
	int result;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	//mutex_lock(&port->spi_lock);
	result = uart->mctrl | spi_uart_get_mctrl(uart);
	//mutex_unlock(&port->spi_lock);
	return result;
}

static int spi_uart_tiocmset(struct tty_struct *tty, struct file *file,
			      unsigned int set, unsigned int clear)
{
	struct spi_uart *uart = tty->driver_data;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	//mutex_lock(&port->spi_lock);
	spi_uart_update_mctrl(uart, set, clear);
	//mutex_unlock(&port->spi_lock);

	return 0;
}

#if 0
static int spi_uart_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	int i, len = 0;
	off_t begin = 0;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	len += sprintf(page, "serinfo:1.0 driver%s%s revision:%s\n",
		       "", "", "");
	for (i = 0; i < UART_NR && len < PAGE_SIZE - 96; i++) {
		struct spi_uart *uart = spi_uart_port_get(i);
		if (uart) {
			len += sprintf(page+len, "%d: uart:SPI", i);
			if(capable(CAP_SYS_ADMIN)) {
				len += sprintf(page + len, " tx:%d rx:%d",
					       uart->icount.tx, uart->icount.rx);
				if (uart->icount.frame)
					len += sprintf(page + len, " fe:%d",
						       uart->icount.frame);
				if (uart->icount.parity)
					len += sprintf(page + len, " pe:%d",
						       uart->icount.parity);
				if (uart->icount.brk)
					len += sprintf(page + len, " brk:%d",
						       uart->icount.brk);
				if (uart->icount.overrun)
					len += sprintf(page + len, " oe:%d",
						       uart->icount.overrun);
				if (uart->icount.cts)
					len += sprintf(page + len, " cts:%d",
						       uart->icount.cts);
				if (uart->icount.dsr)
					len += sprintf(page + len, " dsr:%d",
						       uart->icount.dsr);
				if (uart->icount.rng)
					len += sprintf(page + len, " rng:%d",
						       uart->icount.rng);
				if (uart->icount.dcd)
					len += sprintf(page + len, " dcd:%d",
						       uart->icount.dcd);
			}
			strcat(page, "\n");
			len++;
			spi_uart_port_put(uart);
		}

		if (len + begin > off + count)
			goto done;
		if (len + begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;

done:
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return (count < begin + len - off) ? count : (begin + len - off);
}
#endif

static const struct tty_operations spi_uart_ops = {
	.open			= spi_uart_open,
	.close			= spi_uart_close,
	.write			= spi_uart_write,
	.write_room		= spi_uart_write_room,
	.chars_in_buffer	= spi_uart_chars_in_buffer,
	.send_xchar		= spi_uart_send_xchar,
	.throttle		= spi_uart_throttle,
	.unthrottle		= spi_uart_unthrottle,
	.set_termios		= spi_uart_set_termios,
	.break_ctl		= spi_uart_break_ctl,
	.tiocmget		= spi_uart_tiocmget,
	.tiocmset		= spi_uart_tiocmset,
	//.read_proc		= spi_uart_read_proc,
};


static struct tty_driver *spi_uart_tty_driver;

int spi_uart_register(struct spi_fpga_port *port)
{
	int i,ret;
	struct tty_driver *tty_drv;
	spi_uart_tty_driver = tty_drv = alloc_tty_driver(UART_NR);
	if (!tty_drv)
		return -ENOMEM;
	tty_drv->owner = THIS_MODULE;
	tty_drv->driver_name = "spi_uart";
	tty_drv->name =   "ttySPI";
	tty_drv->major = 0;  /* dynamically allocated */
	tty_drv->minor_start = 0;
	//tty_drv->num = UART_NR;
	tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	tty_drv->subtype = SERIAL_TYPE_NORMAL;
	tty_drv->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_drv->init_termios = tty_std_termios;
	tty_drv->init_termios.c_cflag = B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_drv->init_termios.c_ispeed = 115200;
	tty_drv->init_termios.c_ospeed = 115200;
	tty_set_operations(tty_drv, &spi_uart_ops);

	ret = tty_register_driver(tty_drv);
	if (ret)
	goto err1;

	//port->uart.uartclk = 64*115200;	//MCLK/4
	port->uart.uartclk = 60*100000;	//MCLK/4

	for(i=0; i<UART_NR; i++)
	{
		ret = spi_uart_add_port(&port->uart);
		if (ret) {
			goto err2;
		} else {
			struct device *dev;
			dev = tty_register_device(spi_uart_tty_driver, port->uart.index, &port->spi->dev);
			if (IS_ERR(dev)) {
				spi_uart_port_remove(&port->uart);
				ret = PTR_ERR(dev);
				goto err2;
			}
		}
	}

#if SPI_UART_TEST
	char b[20];
	sprintf(b, "spi_uart_workqueue");
	port->uart.spi_uart_workqueue = create_freezeable_workqueue(b);
	if (!port->uart.spi_uart_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->uart.spi_uart_work, spi_uart_work_handler);

	setup_timer(&port->uart.uart_timer, spi_testuart_timer, (unsigned long)port);
	port->uart.uart_timer.expires  = jiffies+2000;
	add_timer(&port->uart.uart_timer);

#endif


	return 0;

err2:
	tty_unregister_driver(tty_drv);
err1:
	put_tty_driver(tty_drv);
	
	return ret;
	
	
}
int spi_uart_unregister(struct spi_fpga_port *port)
{

	return 0;
}

MODULE_DESCRIPTION("Driver for spi2uart.");
MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_LICENSE("GPL");
