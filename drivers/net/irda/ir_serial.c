/*
 *
 *  Copyright (C) 2011 liuyixing <lyx@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 *note: serial driver for IrDA(SIR and FIR) device
 *
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/freezer.h>
#include <mach/board.h>
#include <linux/irq.h>
#include <mach/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "bu92725guw.h"
#include "ir_serial.h"


#define MAX_FRAME_NUM 20
struct rev_frame_length {
	unsigned long frame_length[MAX_FRAME_NUM];
	int iRead;
	int iWrite;
	int iCount;
};

#define frame_read_empty(f)		  ((f)->iCount == 0)
#define frame_write_full(f)		  ((f)->iCount == MAX_FRAME_NUM)
#define frame_length_buf_clear(f) ((f)->iCount = (f)->iWrite = (f)->iRead = 0)

struct bu92747_port {
	struct device		*dev;
	struct irda_info *pdata;
	struct uart_port port;

	/*for FIR fream read*/
	struct rev_frame_length rev_frames;
	//unsigned long last_frame_length;
	unsigned long cur_frame_length; 
	//wait_queue_head_t data_ready_wq;
	//atomic_t data_ready;
	spinlock_t data_lock; 

	int tx_empty;		/* last TX empty bit */

	spinlock_t conf_lock;	/* shared data */
	int baud;		/* current baud rate */

	int rx_enabled;	        /* if we should rx chars */

	int irq_pin;
	int irq;		/* irq assigned to the bu92747 */

	int minor;		/* minor number */

	struct workqueue_struct *workqueue;
	struct work_struct work;
	/* set to 1 to make the workhandler exit as soon as possible */
	int  force_end_work;
	
	int open_flag;
	/* need to know we are suspending to avoid deadlock on workqueue */
	int suspending;

};

#define MAX_BU92747 1
#define BU92747_MAJOR 204
#define BU92747_MINOR 209

static struct bu92747_port *bu92747s[MAX_BU92747]; /* the chips */
static DEFINE_MUTEX(bu92747s_lock);		   /* race on probe */
#define IS_FIR(s)		((s)->baud >= 4000000)
static int max_rate = 4000000; 
static u8 g_receive_buf[BU92725GUW_FIFO_SIZE];

#if 0
#define IRDA_DBG_FUNC(x...) printk(x)
#else
#define IRDA_DBG_FUNC(x...)
#endif

#if 0
#define IRDA_DBG_RECV(x...) printk(x)
#else
#define IRDA_DBG_RECV(x...)
#endif

#if 0
#define IRDA_DBG_SENT(x...) printk(x)
#else
#define IRDA_DBG_SENT(x...)
#endif

/* race on startup&shutdown, mutex lock with CIR driver */
static DEFINE_MUTEX(irda_cir_lock);
int bu92747_try_lock(void)
{
	if (mutex_trylock(&irda_cir_lock))
		return 1;	//ready
	else
		return 0;	//busy
}

void bu92747_unlock(void)
{
	return mutex_unlock(&irda_cir_lock);
}

static int add_frame_length(struct rev_frame_length *f, unsigned long length)
{
	if (frame_write_full(f))
		return -1;

	f->frame_length[f->iWrite] = length;
	f->iCount++;
	f->iWrite = (f->iWrite+1) % MAX_FRAME_NUM;	
	
	return 0;
}

static int get_frame_length(struct rev_frame_length *f, unsigned long *length)
{
	if (frame_read_empty(f))
		return -1;

	*length = f->frame_length[f->iRead];
	f->iCount--;
	f->iRead = (f->iRead+1) % MAX_FRAME_NUM;
	
	return 0;
}

static int bu92747_irda_do_rx(struct bu92747_port *s)
{
	//int i;
	//unsigned int ch, flag;
	int len;
	struct tty_struct *tty = s->port.state->port.tty;
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	if (s->rx_enabled == 0) {
		BU92725GUW_clr_fifo();
		BU92725GUW_reset();
		return 0;
	}
	
	len = BU92725GUW_get_data(g_receive_buf);
	#if 0
	flag = TTY_NORMAL;
	//printk("receive data:\n");
	for (i=0;i<len;i++) {
		ch = g_receive_buf[i];
		uart_insert_char(&s->port, 0, 0, ch, flag);
		s->port.icount.rx++;
		//printk("%d ", ch);
	}
	//printk("\n");
	#else
	if (len > 0) {
		IRDA_DBG_RECV("line %d, enter %s, receive %d data........\n", __LINE__, __func__, len);
		tty_insert_flip_string(tty, g_receive_buf, len);
		s->port.icount.rx += len;
	}
	#endif
	return len;
 }

static int bu92747_irda_do_tx(struct bu92747_port *s)
{
	//int i;
	struct circ_buf *xmit = &s->port.state->xmit;
	int len = uart_circ_chars_pending(xmit);
	int len1, len2;
	IRDA_DBG_SENT("line %d, enter %s, sending %d data\n", __LINE__, __FUNCTION__, len);
	
	if (IS_FIR(s)) {
		irda_hw_tx_enable_irq(BU92725GUW_FIR);
	}
	else {		
		irda_hw_tx_enable_irq(BU92725GUW_SIR);
	}
	
	if (len>0) {		
		s->tx_empty = 0;
	}
	
	/* [Modify] AIC 2011/09/27
	 * BU92725GUW_send_data(xmit->buf+xmit->tail, len, NULL, 0);
	 */
	if ( (xmit->tail + len) > UART_XMIT_SIZE ) {
		len1 = UART_XMIT_SIZE - xmit->tail;
		len2 = len - len1;
		BU92725GUW_send_data(xmit->buf+xmit->tail, len1, xmit->buf, len2);
	} else {
		BU92725GUW_send_data(xmit->buf+xmit->tail, len, NULL, 0);
	}
	/* [Modify-end] AIC 2011/09/27 */
	s->port.icount.tx += len;
	xmit->tail = (xmit->tail + len) & (UART_XMIT_SIZE - 1);

	
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&s->port);

	return len;
}

static void bu92747_irda_dowork(struct bu92747_port *s)
{
	if (!s->force_end_work && !work_pending(&s->work) &&
		!freezing(current) && !s->suspending)
		queue_work(s->workqueue, &s->work);
}

static void bu92747_irda_work(struct work_struct *w)
{
	struct bu92747_port *s = container_of(w, struct bu92747_port, work);
	struct circ_buf *xmit = &s->port.state->xmit;

	IRDA_DBG_SENT("line %d, enter %s \n", __LINE__, __FUNCTION__);

	if (!s->force_end_work && !freezing(current)) {
		if (!uart_circ_empty(xmit) && !uart_tx_stopped(&s->port)) {
			if (s->tx_empty)
				bu92747_irda_do_tx(s);
			else 
				bu92747_irda_dowork(s);
		}
	}
}

static irqreturn_t bu92747_irda_irq(int irqno, void *dev_id)
{
	struct bu92747_port *s = dev_id;
	u32 irq_src = 0;
	unsigned long len;
	struct rev_frame_length *f = &(s->rev_frames);

	irq_src = irda_hw_get_irqsrc();
	IRDA_DBG_RECV("[%s][%d], 0x%x\n",__FUNCTION__,__LINE__, irq_src);

	/* error */
	if (irq_src & (REG_INT_CRC | REG_INT_OE | REG_INT_FE
		| REG_INT_AC | REG_INT_DECE | REG_INT_RDOE | REG_INT_DEX)) {
		printk("[%s][%d]: do err, REG_EIR = 0x%x\n", __FUNCTION__, __LINE__, irq_src);
		BU92725GUW_clr_fifo();
		BU92725GUW_reset();
		if ((BU92725GUW_SEND==irda_hw_get_mode())
			|| (BU92725GUW_MULTI_SEND==irda_hw_get_mode())) {
			s->tx_empty = 1;
		}
	}
	
	if (irq_src & (REG_INT_DRX | FRM_EVT_RX_EOFRX | FRM_EVT_RX_RDE)) {
		//fixing CA001 (IrSimple mode sending) failing issue
		/* modified to process a frame ending processing first, when RDE_EI and EOF_EI are happen at the same time.
		 * Before the modification, disconnect packet was processed as the previous packet,
		 * not as a disconnect packet. The packets were combined.
		 */
		if ((irq_src & REG_INT_EOF) && (s->port.state->port.tty != NULL)) {
			tty_flip_buffer_push(s->port.state->port.tty);
			if (IS_FIR(s)) {
				spin_lock(&s->data_lock);
				if (add_frame_length(f, s->cur_frame_length) == 0) {
					s->cur_frame_length = 0;
				}
				else {
					printk("func %s,line %d: FIR frame length buf full......\n", __FUNCTION__, __LINE__);				
				}
				spin_unlock(&s->data_lock);
			}
	    	}
		//~ 

		len = bu92747_irda_do_rx(s);
		if (!IS_FIR(s))
			tty_flip_buffer_push(s->port.state->port.tty);
		else {
			spin_lock(&s->data_lock);
			s->cur_frame_length += len;
			spin_unlock(&s->data_lock);
		}
	}
	
	if ((irq_src & REG_INT_EOF) && (s->port.state->port.tty != NULL)) {
		spin_lock(&s->data_lock);	// [Modify] AIC 2011/09/30 
		tty_flip_buffer_push(s->port.state->port.tty);
		if (IS_FIR(s)) {
			/* [Modify] AIC 2011/09/30
			 * spin_lock(&s->data_lock);
			 */
			if (add_frame_length(f, s->cur_frame_length) == 0) {
				s->cur_frame_length = 0;
			}
			else {
				printk("func %s,line %d: FIR frame length buf full......\n", __FUNCTION__, __LINE__);				
			}
			/* [Modify] AIC 2011/09/30 
			 * spin_unlock(&s->data_lock);
			 */
		}
		spin_unlock(&s->data_lock);	// [Modify] AIC 2011/09/30
	}
	
	/* [Modify] AIC 2011/09/27
	 *
	 * if (irq_src & (FRM_EVT_TX_TXE | FRM_EVT_TX_WRE)) {
	 *	s->tx_empty = 1;
	 *	irda_hw_set_moderx();
	 * }
	 */
	/* [Modify] AIC 2011/09/29
	 *    
	 * if (irq_src & (FRM_EVT_TX_TXE | FRM_EVT_TX_WRE)) {
	 *	s->tx_empty = 1;
	 *	if ( irq_src & FRM_EVT_TX_TXE ) {
	 *		irda_hw_set_moderx();
	 *	}
	 */
	if ( (irq_src & (FRM_EVT_TX_TXE | FRM_EVT_TX_WRE)) &&
				(BU92725GUW_get_length_in_fifo_buffer() == 0) ) {
		s->tx_empty = 1;

		if ( irq_src & FRM_EVT_TX_TXE ) {
			irda_hw_set_moderx();
		}
	}
	/* [Modify-end] AIC 2011/09/29 */
#if 0
	/* error */
	if (irq_src & REG_INT_TO) {
		printk("[%s][%d]: do timeout err\n", __FUNCTION__, __LINE__);
		BU92725GUW_clr_fifo();
		BU92725GUW_reset();
		if ((BU92725GUW_SEND==irda_hw_get_mode())
			|| (BU92725GUW_MULTI_SEND==irda_hw_get_mode())) {
			s->tx_empty = 1;
		}
	}
#endif	
	return IRQ_HANDLED;
}


static void bu92747_irda_stop_tx(struct uart_port *port)
{
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
}

static void bu92747_irda_start_tx(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	//wait for start cmd
	if (IS_FIR(s))
		return	;

	bu92747_irda_dowork(s);
}

static void bu92747_irda_stop_rx(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	s->rx_enabled = 0;
}

static unsigned int bu92747_irda_tx_empty(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	/* may not be truly up-to-date */
	return s->tx_empty;
}

static const char *bu92747_irda_type(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	return s->port.type == PORT_IRDA ? "BU92747" : NULL;
}

static void bu92747_irda_release_port(struct uart_port *port)
{
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
}

static void bu92747_irda_config_port(struct uart_port *port, int flags)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	if (flags & UART_CONFIG_TYPE)
		s->port.type = PORT_IRDA;
}

static int bu92747_irda_verify_port(struct uart_port *port,
				   struct serial_struct *ser)
{
	int ret = -EINVAL;

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	if (ser->type == PORT_UNKNOWN || ser->type == PORT_IRDA)
		ret = 0;
	
	return ret;
}

static void bu92747_irda_shutdown(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);
	struct rev_frame_length *f = &(s->rev_frames);

	printk("line %d, enter %s \n", __LINE__, __FUNCTION__);

	if (s->suspending)
		return;

	s->open_flag = 0;
	s->force_end_work = 1;

	if (s->workqueue) {
		flush_workqueue(s->workqueue);
		destroy_workqueue(s->workqueue);
		s->workqueue = NULL;
	}

	spin_lock(&s->data_lock);
	frame_length_buf_clear(f);
	s->cur_frame_length = 0;
	spin_unlock(&s->data_lock);
		
	if (s->irq)
		free_irq(s->irq, s);
	
	irda_hw_shutdown();
	if (s->pdata->irda_pwr_ctl)
		s->pdata->irda_pwr_ctl(0);	

	bu92747_unlock();
}

static int bu92747_irda_startup(struct uart_port *port)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);
	char b[32];
	struct rev_frame_length *f = &(s->rev_frames);

	printk("line %d, enter %s \n", __LINE__, __FUNCTION__);

	s->rx_enabled = 1;
	
	if (s->suspending)
		return 0;

	if (!bu92747_try_lock()) {
		printk("func %s, cannot get bu92747 lock, bu92747 in using\n", __func__);
		return -EBUSY;
	}
	
	s->baud = 9600;
	
	spin_lock(&s->data_lock);
	frame_length_buf_clear(f);
	s->cur_frame_length = 0;
	spin_unlock(&s->data_lock);

	s->tx_empty = 1;
	s->force_end_work = 0;

	sprintf(b, "bu92747_irda-%d", s->minor);
	s->workqueue = create_rt_workqueue(b);
	if (!s->workqueue) {
		dev_warn(s->dev, "cannot create workqueue\n");
		bu92747_unlock();
		return -EBUSY;
	}
	INIT_WORK(&s->work, bu92747_irda_work);

	if (request_irq(s->irq, bu92747_irda_irq,
			IRQF_TRIGGER_LOW, "bu92747_irda", s) < 0) {
		dev_warn(s->dev, "cannot allocate irq %d\n", s->irq);
		s->irq = 0;
		destroy_workqueue(s->workqueue);
		s->workqueue = NULL;
		bu92747_unlock();
		return -EBUSY;
	}

	disable_irq(s->irq);

	if (s->pdata->irda_pwr_ctl)
		s->pdata->irda_pwr_ctl(1);

	irda_hw_startup();
	irda_hw_set_moderx();

	enable_irq(s->irq);

	s->open_flag = 1;

	return 0;
}

static int bu92747_irda_request_port(struct uart_port *port)
{
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
	return 0;
}

static void bu92747_irda_break_ctl(struct uart_port *port, int break_state)
{
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
}

static unsigned int bu92747_irda_get_mctrl(struct uart_port *port)
{
	return  TIOCM_DSR | TIOCM_CAR;
}

static void bu92747_irda_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
}

static void
bu92747_irda_set_termios(struct uart_port *port, struct ktermios *termios,
			struct ktermios *old)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);
	int baud = 0;
	unsigned cflag;
	struct tty_struct *tty = s->port.state->port.tty;

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
	if (!tty)
		return;

	cflag = termios->c_cflag;
	baud = uart_get_baud_rate(port, termios, old, 0, max_rate);

	switch (baud) {
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 4000000:
		if (s->baud!=baud) {
			IRDA_DBG_RECV("func %s:irda set baudrate %d........\n", __FUNCTION__, baud);
			irda_hw_set_speed(baud);
			s->baud = baud;
			s->tx_empty = 1;
		}
		break;

	default:
		break;
	}
	
	uart_update_timeout(port, termios->c_cflag, baud);

}

static int bu92747_get_frame_length(struct bu92747_port *s)
{
	struct rev_frame_length *f = &(s->rev_frames);
	unsigned long len = 0;

	spin_lock(&s->data_lock);
	if (get_frame_length(f, &len) != 0) {
		IRDA_DBG_RECV("func %s, line %d: FIR data not ready......\n", __FUNCTION__, __LINE__);
		len = 0;
	}
	spin_unlock(&s->data_lock);
	
	return len;
}

static int bu92747_irda_ioctl(struct uart_port *port, unsigned int cmd, unsigned long arg)
{
	struct bu92747_port *s = container_of(port,
						  struct bu92747_port,
						  port);
	void __user *argp = (void __user *)arg;
	unsigned long len = 0;
	int ret = 0;
	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);

	switch (cmd) {
	case TTYIR_GETLENGTH:
		len = bu92747_get_frame_length(s);
		if (len >= 0) {
			if (copy_to_user(argp, &len, sizeof(len)))
				ret = -EFAULT;
		}
		else
			ret = -EFAULT;
		break;
		
	case TTYIR_STARTSEND:		
		bu92747_irda_dowork(s);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	
	return ret;
}

static struct uart_ops bu92747_irda_ops = {
	.tx_empty	= bu92747_irda_tx_empty,
	.set_mctrl	= bu92747_irda_set_mctrl,
	.get_mctrl	= bu92747_irda_get_mctrl,
	.stop_tx        = bu92747_irda_stop_tx,
	.start_tx	= bu92747_irda_start_tx,
	.stop_rx	= bu92747_irda_stop_rx,
	//.enable_ms      = bu92747_irda_enable_ms,
	.break_ctl      = bu92747_irda_break_ctl,
	.startup	= bu92747_irda_startup,
	.shutdown	= bu92747_irda_shutdown,
	.set_termios	= bu92747_irda_set_termios,
	.type		= bu92747_irda_type,
	.release_port   = bu92747_irda_release_port,
	.request_port   = bu92747_irda_request_port,
	.config_port	= bu92747_irda_config_port,
	.verify_port	= bu92747_irda_verify_port,
	.ioctl			= bu92747_irda_ioctl,
};

static struct uart_driver bu92747_irda_uart_driver = {
	.owner          = THIS_MODULE,
	.driver_name    = "ttyIr",
	.dev_name       = "ttyIr",
	.major          = BU92747_MAJOR,
	.minor          = BU92747_MINOR,
	.nr             = MAX_BU92747,
};

static int uart_driver_registered;
static int __devinit bu92747_irda_probe(struct platform_device *pdev)
{
	int i, retval;
    struct irda_info *platdata = pdev->dev.platform_data;

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
	if (!platdata) {
		dev_warn(&pdev->dev, "no platform data info\n");
		return -1;
	}

	mutex_lock(&bu92747s_lock);

	if (!uart_driver_registered) {
		uart_driver_registered = 1;
		retval = uart_register_driver(&bu92747_irda_uart_driver);
		if (retval) {
			printk(KERN_ERR "Couldn't register bu92747 uart driver\n");
			mutex_unlock(&bu92747s_lock);
			return retval;
		}
	}

	for (i = 0; i < MAX_BU92747; i++)
		if (!bu92747s[i])
			break;
	if (i == MAX_BU92747) {
		dev_warn(&pdev->dev, "too many bu92747 chips\n");
		mutex_unlock(&bu92747s_lock);
		return -ENOMEM;
	}

	bu92747s[i] = kzalloc(sizeof(struct bu92747_port), GFP_KERNEL);
	if (!bu92747s[i]) {
		dev_warn(&pdev->dev,
			 "kmalloc for bu92747 structure %d failed!\n", i);
		mutex_unlock(&bu92747s_lock);
		return -ENOMEM;
	}
	bu92747s[i]->dev = &pdev->dev;
	bu92747s[i]->irq_pin = platdata->intr_pin;
	bu92747s[i]->irq = gpio_to_irq(platdata->intr_pin);
	if (platdata->iomux_init)
		platdata->iomux_init();
	bu92747s[i]->pdata = platdata;
	spin_lock_init(&bu92747s[i]->conf_lock);
	dev_set_drvdata(&pdev->dev, bu92747s[i]);
	bu92747s[i]->minor = i;
	dev_dbg(&pdev->dev, "%s: adding port %d\n", __func__, i);
	bu92747s[i]->port.irq = bu92747s[i]->irq;
	bu92747s[i]->port.fifosize = BU92725GUW_FIFO_SIZE;
	bu92747s[i]->port.ops = &bu92747_irda_ops;
	bu92747s[i]->port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF;
	bu92747s[i]->port.line = i;
	bu92747s[i]->port.type = PORT_IRDA;
	bu92747s[i]->port.dev = &pdev->dev;
	retval = uart_add_one_port(&bu92747_irda_uart_driver, &bu92747s[i]->port);
	if (retval < 0)
		dev_warn(&pdev->dev,
			 "uart_add_one_port failed for line %d with error %d\n",
			 i, retval);
	bu92747s[i]->open_flag = 0;
	bu92747s[i]->suspending = 0;
	/* set shutdown mode to save power. Will be woken-up on open */	
	if (bu92747s[i]->pdata->irda_pwr_ctl)
		bu92747s[i]->pdata->irda_pwr_ctl(0);
	
	spin_lock_init(&(bu92747s[i]->data_lock));

	mutex_unlock(&bu92747s_lock);
	
	return 0;
}

static int __devexit bu92747_irda_remove(struct platform_device *pdev)
{
	struct bu92747_port *s = dev_get_drvdata(&pdev->dev);
	int i;

	IRDA_DBG_FUNC("line %d, enter %s \n", __LINE__, __FUNCTION__);
	mutex_lock(&bu92747s_lock);

	/* find out the index for the chip we are removing */
	for (i = 0; i < MAX_BU92747; i++)
		if (bu92747s[i] == s)
			break;

	dev_dbg(&pdev->dev, "%s: removing port %d\n", __func__, i);
	uart_remove_one_port(&bu92747_irda_uart_driver, &bu92747s[i]->port);
	kfree(bu92747s[i]);
	bu92747s[i] = NULL;

	/* check if this is the last chip we have */
	for (i = 0; i < MAX_BU92747; i++)
		if (bu92747s[i]) {
			mutex_unlock(&bu92747s_lock);
			return 0;
		}
	pr_debug("removing bu92747 driver\n");
	uart_unregister_driver(&bu92747_irda_uart_driver);

	mutex_unlock(&bu92747s_lock);
	return 0;
}


#ifdef CONFIG_PM
static int bu92747_irda_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bu92747_port *s = dev_get_drvdata(&pdev->dev);

	if (s->open_flag) {
		printk("line %d, enter %s \n", __LINE__, __FUNCTION__);
		disable_irq(s->irq);
		cancel_work_sync(&s->work);
		s->suspending = 1;
		uart_suspend_port(&bu92747_irda_uart_driver, &s->port);

		irda_hw_shutdown();
		if (s->pdata->irda_pwr_ctl)
			s->pdata->irda_pwr_ctl(0);
	}
	
	return 0;
}

static int bu92747_irda_resume(struct platform_device *pdev)
{
	struct bu92747_port *s = dev_get_drvdata(&pdev->dev);
	
	if (s->open_flag) {
		printk("line %d, enter %s \n", __LINE__, __FUNCTION__);
		if (s->pdata->irda_pwr_ctl)
			s->pdata->irda_pwr_ctl(1);
		
		irda_hw_startup();
		irda_hw_set_speed(s->baud);
		irda_hw_set_moderx();

		uart_resume_port(&bu92747_irda_uart_driver, &s->port);
		s->suspending = 0;

		if (!s->tx_empty)
			s->tx_empty = 1;
		enable_irq(s->irq);
		if (s->workqueue && !IS_FIR(s))
			bu92747_irda_dowork(s);
	}
	
	return 0;
}
#else
#define bu92747_irda_suspend NULL
#define bu92747_irda_resume  NULL
#endif

static struct platform_driver bu92747_irda_driver = {
	.driver = {
		.name = "bu92747_irda",
        .owner	= THIS_MODULE,
	},
	.probe = bu92747_irda_probe,
	.remove = bu92747_irda_remove,
	.suspend = bu92747_irda_suspend,
	.resume = bu92747_irda_resume,
};

static int __init bu92747_irda_init(void)
{
	if (platform_driver_register(&bu92747_irda_driver) != 0) {
    	printk("Could not register irda driver\n");
    	return -EINVAL;
	}
	return 0;
}

static void __exit bu92747_irda_exit(void)
{
	platform_driver_unregister(&bu92747_irda_driver);
}

module_init(bu92747_irda_init);
module_exit(bu92747_irda_exit);
MODULE_DESCRIPTION("BU92747 irda driver");
MODULE_AUTHOR("liuyixing <lyx@rock-chips.com>");
MODULE_LICENSE("GPL");

