/*
 *
 *  Copyright (C) 2010 liuyixing <lyx@rock-chips.com>
 *
 *
 * Example platform data:

 static struct plat_sc8800 sc8800_plat_data = {
	 .slav_rts_pin = RK29_PIN4_PD0,
	 .slav_rdy_pin = RK29_PIN4_PD0,
	 .master_rts_pin = RK29_PIN4_PD0,
	 .master_rdy_pin = RK29_PIN4_PD0,
	 //.poll_time = 100,
 };

 static struct spi_board_info spi_board_info[] = {
	 {
	 .modalias	= "sc8800",
	 .platform_data	= &sc8800_plat_data,
	 .max_speed_hz	= 12*1000*1000,
	 .chip_select	= 0,
	 },
 };

 * The initial minor number is 209 in the low-density serial port:
 * mknod /dev/ttySPI0 c 204 209
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/spi/spi.h>
#include <linux/freezer.h>
#include <linux/gpio.h>

#include "sc8800.h"

struct sc8800_port {
	struct uart_port port;
	struct spi_device *spi;

	int cts;	        /* last CTS received for flow ctrl */
	int tx_empty;		/* last TX empty bit */

	spinlock_t conf_lock;	/* shared data */
	int conf;		/* configuration for the SC88000
				 * (bits 0-7, bits 8-11 are irqs) */

	int rx_enabled;	        /* if we should rx chars */

	int irq;		/* irq assigned to the sc8800 */

	int minor;		/* minor number */
	int loopback;		/* 1 if we are in loopback mode */

	/* for handling irqs: need workqueue since we do spi_sync */
	struct workqueue_struct *workqueue;
	struct work_struct work;
	/* set to 1 to make the workhandler exit as soon as possible */
	int  force_end_work;
	/* need to know we are suspending to avoid deadlock on workqueue */
	int suspending;

	/* hook for suspending SC8800 via dedicated pin */
	void (*sc8800_hw_suspend) (int suspend);

	/* poll time (in ms) for ctrl lines */
	int poll_time;
	/* and its timer */
	struct timer_list	timer;

	/*signal define, always gpio*/
	int slav_rts;
	int slav_rdy;
	int master_rts;
	int master_rdy;
};

#define SC8800_MAJOR 204
#define SC8800_MINOR 209
#define MAX_SC8800 1

#define SPI_MAX_PACKET (8192-128)
#define FREAM_SIZE 64
#define SPI_DMA_SIZE PAGE_SIZE

void * g_dma_buffer = NULL;
dma_addr_t g_dma_addr;

#if 1
#define sc8800_dbg(x...) printk(x)
#else
#define sc8800_dbg(x...)
#endif

static struct sc8800_port *sc8800s[MAX_SC8800]; /* the chips */
static DEFINE_MUTEX(sc8800s_lock);		   /* race on probe */

static int sc8800_get_slave_rts_status(struct sc8800_port *s)
{
	return gpio_get_value(s->slav_rts);
}

static int sc8800_get_slave_rdy_status(struct sc8800_port *s)
{
	return gpio_get_value(s->slav_rdy);
}

static void sc8800_set_master_rts_status(struct sc8800_port *s, int value)
{
	gpio_set_value(s->master_rts, value);
}

static void sc8800_set_master_rdy_status(struct sc8800_port *s, int value)
{
	gpio_set_value(s->master_rdy, value);
}

static int sc8800_send_head_data(struct sc8800_port *s, u32 data_len)
{
	char head[64] = {0};
	struct spi_message message;
	int status;
	struct spi_transfer tran;

	head[0] = 0x7f;
	head[1] = 0x7e;
	head[2] = 0x55;
	head[3] = 0xaa;
	head[4] = data_len & 0xff;
	head[5] = (data_len>>8) & 0xff;
	head[6] = (data_len>>16) & 0xff;
	head[7] = (data_len>>24) & 0xff;
	
	tran.tx_buf = (void *)(head);
	tran.len = FREAM_SIZE;
	
	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	status = spi_sync(s->spi, &message);
	if (status) {
		dev_warn(&s->spi->dev, "error while calling spi_sync\n");
		return -EIO;
	}
	
	return 0;
}
static int sc8800_recv_and_parse_head_data(struct sc8800_port *s, u32 *len)
{
	struct spi_message message;
	int status;
	struct spi_transfer tran;
	char buf[64] = {0};
	u32 data_len = 0;
		
	tran.rx_buf = (void *)buf;
	tran.len = FREAM_SIZE;
	
	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	status = spi_sync(s->spi, &message);
	if (status) {
		dev_warn(&s->spi->dev, "error while calling spi_sync\n");
		return -EIO;
	}

	if ((buf[0]!=0x7f) || (buf[1]!=0x7e) || (buf[2]!=0x55) || (buf[3]!=0xaa)) {
		dev_warn(&s->spi->dev, "line %d, error head data", __LINE__);
		return -EIO;
	}
	
	data_len = buf[5] + (buf[6]<<8) + (buf[7]<<16) + (buf[8]<<24);

	*len = data_len;
	
	sc8800_dbg("line %d, %d data need to read\n", __LINE__, *len);
	
	return 0;
}

static int sc8800_send_data(struct sc8800_port *s, char *buf, int len)
{
#if 1
	int status;
	struct spi_message message;
	struct spi_transfer tran;
	
	tran.tx_buf = (void *)buf;
	tran.len = len;
	
	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	status = spi_sync(s->spi, &message);
	if (status) {
		dev_warn(&s->spi->dev, "error while calling spi_sync\n");
		return -EIO;
	}
#else
	int status;
	struct spi_message message;
	struct spi_transfer tran;

	spi_message_init(&message);
	
	if (!g_dma_buffer) {
		g_dma_buffer = dma_alloc_coherent(NULL, SPI_DMA_SIZE, &g_dma_addr, GFP_KERNEL | GFP_DMA);
		if (!g_dma_buffer) {
			dev_err(&s->spi->dev, "alloc dma memory fail\n");
		}
	}

	if (!g_dma_buffer) {	//nomal
		sc8800_dbg("send data nomal");
		tran.tx_buf = (void *)buf;
		tran.len = len;
	}
	else {	//dma	
		//message.is_dma_mapped = 1;
		//memcpy(g_dma_buffer, buf, );
	}

	spi_message_add_tail(&tran, &message);
	status = spi_sync(s->spi, &message);
	if (status) {
		dev_warn(&s->spi->dev, "error while calling spi_sync\n");
		return -EIO;
	}
#endif

	return 0;
}

static int sc8800_recv_data(struct sc8800_port *s, char *buf, int len)
{
	struct spi_message message;
	int status;
	struct spi_transfer tran;
	
	tran.rx_buf = (void *)buf;
	tran.len = len;
	
	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	status = spi_sync(s->spi, &message);
	if (status) {
		dev_warn(&s->spi->dev, "error while calling spi_sync\n");
		return -EIO;
	}
	
	return 0;
}

static void sc8800_work(struct work_struct *w);

static void sc8800_dowork(struct sc8800_port *s)
{
	if (!s->force_end_work && !work_pending(&s->work) &&
	    !freezing(current) && !s->suspending)
		queue_work(s->workqueue, &s->work);
}

static void sc8800_timeout(unsigned long data)
{
	struct sc8800_port *s = (struct sc8800_port *)data;

	if (s->port.state) {
		sc8800_dowork(s);
		mod_timer(&s->timer, jiffies + s->poll_time);
	}
}

static void sc8800_work(struct work_struct *w)
{
	struct sc8800_port *s = container_of(w, struct sc8800_port, work);
	struct circ_buf *xmit = &s->port.state->xmit;
	u32 len,i;
	char *buf = NULL;
	unsigned char ch;
	
	dev_dbg(&s->spi->dev, "%s\n", __func__);

	if (sc8800_get_slave_rts_status(s) == GPIO_HIGH) {	//do wirte, master--->slave
		if (sc8800_get_slave_rdy_status(s) == GPIO_HIGH) {	/*1.check slave rdy, must be high*/
			if (!(uart_circ_empty(xmit))) {
				len = uart_circ_chars_pending(xmit);
				len = (len > SPI_MAX_PACKET) ? SPI_MAX_PACKET : len;
				sc8800_dbg("send data length = %d\n", len);
				
				sc8800_set_master_rts_status(s, GPIO_LOW);	/*2.set master rts low*/
				sc8800_send_head_data(s,len);	/*3.send 64byte head data*/
				while (sc8800_get_slave_rdy_status(s)) {	/*4.check slav rdy, wait for low */
					msleep(1);
				}
				
				/*5.long data transmit*/
				sc8800_send_data(s, xmit->buf+xmit->tail, len);
				
				while(sc8800_get_slave_rdy_status(s)==GPIO_LOW) {	/*6.wait for slave rdy high*/
					msleep(1);
				}

				sc8800_set_master_rts_status(s, GPIO_HIGH);	/*end transmit, set master rts high*/
				xmit->tail = (xmit->tail + len) & (UART_XMIT_SIZE - 1);
				s->port.icount.tx += len;
			}
		}
		else {	//slave not ready, do it next time
			queue_work(s->workqueue, &s->work);
		}			
	}
	else {	//do read, slave--->master
		sc8800_set_master_rdy_status(s, GPIO_LOW);
		sc8800_recv_and_parse_head_data(s, &len);

		buf = (char *)kzalloc(len, GFP_KERNEL);
		if (!buf) {
			dev_err(&s->spi->dev, "line %d, err while malloc mem\n", __LINE__);
			sc8800_set_master_rdy_status(s, GPIO_HIGH);
			return ;
		}
		memset(buf, 0, len);
		sc8800_recv_data(s, buf, len);
		
		while (sc8800_get_slave_rts_status(s) == GPIO_LOW) {
			msleep(1);
		}
		sc8800_set_master_rdy_status(s, GPIO_HIGH);

		for (i=0; i<len; i++) {
			ch = buf[i];		
			uart_insert_char(&s->port, 0, 0, ch, TTY_NORMAL);
		}
		tty_flip_buffer_push(s->port.state->port.tty);
	}
}

static irqreturn_t sc8800_irq(int irqno, void *dev_id)
{
	struct sc8800_port *s = dev_id;

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	sc8800_dowork(s);
	return IRQ_HANDLED;
}

static void sc8800_enable_ms(struct uart_port *port)
{
	struct sc8800_port *s = container_of(port,
					      struct sc8800_port,
					      port);

	if (s->poll_time > 0)
		mod_timer(&s->timer, jiffies);
	dev_dbg(&s->spi->dev, "%s\n", __func__);
}

static void sc8800_start_tx(struct uart_port *port)
{
	struct sc8800_port *s = container_of(port,
					      struct sc8800_port,
					      port);

	dev_dbg(&s->spi->dev, "%s\n", __func__);
	
	sc8800_dowork(s);
}

static void sc8800_stop_rx(struct uart_port *port)
{
	struct sc8800_port *s = container_of(port,
					      struct sc8800_port,
					      port);

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	s->rx_enabled = 0;

	sc8800_dowork(s);
}

static void sc8800_shutdown(struct uart_port *port)
{
	struct sc8800_port *s = container_of(port,
					      struct sc8800_port,
					      port);

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	if (s->suspending)
		return;

	s->force_end_work = 1;

	if (s->poll_time > 0)
		del_timer_sync(&s->timer);

	if (s->workqueue) {
		flush_workqueue(s->workqueue);
		destroy_workqueue(s->workqueue);
		s->workqueue = NULL;
	}
	if (s->irq)
		free_irq(s->irq, s);

	gpio_free(s->master_rdy);
	gpio_free(s->master_rts);
	gpio_free(s->slav_rdy);
	gpio_free(s->slav_rts);

	/* set shutdown mode to save power */
	if (s->sc8800_hw_suspend)
		s->sc8800_hw_suspend(1);
}

static int sc8800_startup(struct uart_port *port)
{
	struct sc8800_port *s = container_of(port,
					      struct sc8800_port,
					      port);
	int ret;
	char b[12];

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	s->rx_enabled = 1;

	if (s->suspending)
		return 0;
	
	s->force_end_work = 0;
	
	sprintf(b, "sc8800-%d", s->minor);
	s->workqueue = create_freezeable_workqueue(b);
	if (!s->workqueue) {
		dev_warn(&s->spi->dev, "cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&s->work, sc8800_work);

	ret = gpio_request(s->slav_rts, "slav rts");
	if (ret) {
		dev_err(&s->spi->dev, "line %d: gpio request err\n", __LINE__);
		ret = -EBUSY;
		goto gpio_err1;
	}
	ret = gpio_request(s->slav_rdy, "slav rdy");
	if (ret) {
		dev_err(&s->spi->dev, "line %d: gpio request err\n", __LINE__);
		ret = -EBUSY;
		goto gpio_err2;
	}
	ret = gpio_request(s->master_rts, "master rts");
	if (ret) {
		dev_err(&s->spi->dev, "line %d: gpio request err\n", __LINE__);
		ret = -EBUSY;
		goto gpio_err3;
	}
	ret = gpio_request(s->master_rdy, "master rdy");
	if (ret) {
		dev_err(&s->spi->dev, "line %d: gpio request err\n", __LINE__);
		ret = -EBUSY;
		goto gpio_err4;
	}

	gpio_direction_input(s->slav_rts);
	gpio_pull_updown(s->slav_rts, GPIOPullUp);
	gpio_direction_input(s->slav_rdy);
	gpio_pull_updown(s->slav_rdy, GPIOPullUp);
	gpio_direction_output(s->master_rts, GPIO_HIGH);
	gpio_direction_output(s->master_rdy, GPIO_HIGH);
	
	if (request_irq(s->irq, sc8800_irq,
			IRQF_TRIGGER_FALLING, "sc8800", s) < 0) {
		dev_warn(&s->spi->dev, "cannot allocate irq %d\n", s->irq);
		s->irq = 0;
		goto irq_err;
	}

	if (s->sc8800_hw_suspend)
		s->sc8800_hw_suspend(0);
	
	sc8800_dowork(s);

	sc8800_enable_ms(&s->port);

	return 0;
	
irq_err:
	gpio_free(s->master_rdy);
gpio_err4:
	gpio_free(s->master_rts);
gpio_err3:
	gpio_free(s->slav_rdy);
gpio_err2:
	gpio_free(s->slav_rts);
gpio_err1:
	destroy_workqueue(s->workqueue);
	s->workqueue = NULL;	
	return ret;

}

static struct uart_ops sc8800_ops = {
	.start_tx	= sc8800_start_tx,
	.stop_rx	= sc8800_stop_rx,
	.startup	= sc8800_startup,
	.shutdown	= sc8800_shutdown,
};

static struct uart_driver sc8800_uart_driver = {
	.owner          = THIS_MODULE,
	.driver_name    = "ttySPI",
	.dev_name       = "ttySPI",
	.major          = SC8800_MAJOR,
	.minor          = SC8800_MINOR,
	.nr             = MAX_SC8800,
};
static int uart_driver_registered;

static int __devinit sc8800_probe(struct spi_device *spi)
{
	int i, retval;
	struct plat_sc8800 *pdata;
	
	mutex_lock(&sc8800s_lock);

	if (!uart_driver_registered) {
		uart_driver_registered = 1;
		retval = uart_register_driver(&sc8800_uart_driver);
		if (retval) {
			printk(KERN_ERR "Couldn't register sc8800 uart driver\n");
			mutex_unlock(&sc8800s_lock);
			return retval;
		}
	}

	for (i = 0; i < MAX_SC8800; i++)
		if (!sc8800s[i])
			break;
	if (i == MAX_SC8800) {
		dev_warn(&spi->dev, "too many SC8800 chips\n");
		mutex_unlock(&sc8800s_lock);
		return -ENOMEM;
	}

	sc8800s[i] = kzalloc(sizeof(struct sc8800_port), GFP_KERNEL);
	if (!sc8800s[i]) {
		dev_warn(&spi->dev,
			 "kmalloc for sc8800 structure %d failed!\n", i);
		mutex_unlock(&sc8800s_lock);
		return -ENOMEM;
	}
	sc8800s[i]->spi = spi;
	spin_lock_init(&sc8800s[i]->conf_lock);
	dev_set_drvdata(&spi->dev, sc8800s[i]);
	pdata = spi->dev.platform_data;
	sc8800s[i]->irq = gpio_to_irq(pdata->slav_rts_pin);
	sc8800s[i]->slav_rts = pdata->slav_rts_pin;
	sc8800s[i]->slav_rdy = pdata->slav_rdy_pin;
	sc8800s[i]->master_rts = pdata->master_rts_pin;
	sc8800s[i]->master_rdy = pdata->master_rdy_pin;
	//sc8800s[i]->sc8800_hw_suspend = pdata->sc8800_hw_suspend;
	sc8800s[i]->minor = i;
	init_timer(&sc8800s[i]->timer);
	sc8800s[i]->timer.function = sc8800_timeout;
	sc8800s[i]->timer.data = (unsigned long) sc8800s[i];

	dev_dbg(&spi->dev, "%s: adding port %d\n", __func__, i);
	sc8800s[i]->port.irq = sc8800s[i]->irq;
	sc8800s[i]->port.uartclk = 24000000;
	sc8800s[i]->port.fifosize = 64;
	sc8800s[i]->port.ops = &sc8800_ops;
	sc8800s[i]->port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF;
	sc8800s[i]->port.line = i;
	sc8800s[i]->port.dev = &spi->dev;
	retval = uart_add_one_port(&sc8800_uart_driver, &sc8800s[i]->port);
	if (retval < 0)
		dev_warn(&spi->dev,
			 "uart_add_one_port failed for line %d with error %d\n",
			 i, retval);

	/* set shutdown mode to save power. Will be woken-up on open */
	if (sc8800s[i]->sc8800_hw_suspend)
		sc8800s[i]->sc8800_hw_suspend(1);

	mutex_unlock(&sc8800s_lock);
	return 0;
}

static int __devexit sc8800_remove(struct spi_device *spi)
{
	struct sc8800_port *s = dev_get_drvdata(&spi->dev);
	int i;

	mutex_lock(&sc8800s_lock);

	/* find out the index for the chip we are removing */
	for (i = 0; i < MAX_SC8800; i++)
		if (sc8800s[i] == s)
			break;

	dev_dbg(&spi->dev, "%s: removing port %d\n", __func__, i);
	uart_remove_one_port(&sc8800_uart_driver, &sc8800s[i]->port);
	kfree(sc8800s[i]);
	sc8800s[i] = NULL;

	/* check if this is the last chip we have */
	for (i = 0; i < MAX_SC8800; i++)
		if (sc8800s[i]) {
			mutex_unlock(&sc8800s_lock);
			return 0;
		}
	pr_debug("removing sc8800 driver\n");
	uart_unregister_driver(&sc8800_uart_driver);

	mutex_unlock(&sc8800s_lock);
	return 0;
}

#ifdef CONFIG_PM

static int sc8800_suspend(struct spi_device *spi, pm_message_t state)
{
	struct sc8800_port *s = dev_get_drvdata(&spi->dev);

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	disable_irq(s->irq);

	s->suspending = 1;
	uart_suspend_port(&sc8800_uart_driver, &s->port);

	if (s->sc8800_hw_suspend)
		s->sc8800_hw_suspend(1);

	return 0;
}

static int sc8800_resume(struct spi_device *spi)
{
	struct sc8800_port *s = dev_get_drvdata(&spi->dev);

	dev_dbg(&s->spi->dev, "%s\n", __func__);

	if (s->sc8800_hw_suspend)
		s->sc8800_hw_suspend(0);
	uart_resume_port(&sc8800_uart_driver, &s->port);
	s->suspending = 0;

	enable_irq(s->irq);

	if (s->workqueue)
		sc8800_dowork(s);

	return 0;
}

#else
#define sc8800_suspend NULL
#define sc8800_resume  NULL
#endif

static struct spi_driver sc8800_driver = {
	.driver = {
		.name		= "sc8800",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= sc8800_probe,
	.remove		= __devexit_p(sc8800_remove),
	.suspend	= sc8800_suspend,
	.resume		= sc8800_resume,
};

static int __init sc8800_init(void)
{
	return spi_register_driver(&sc8800_driver);
}
module_init(sc8800_init);

static void __exit sc8800_exit(void)
{
	spi_unregister_driver(&sc8800_driver);
}
module_exit(sc8800_exit);

MODULE_DESCRIPTION("SC8800 driver");
MODULE_AUTHOR("liuyixing <lyx@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:SC8800");
