/*
 * linux/drivers/fpga/spi_fpga_init.c - spi fpga init driver
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

/*
 * Note: fpga ice65l08xx is used for spi2uart,spi2gpio,spi2i2c and spi2dpram.
 * this driver is the entry of all modules's drivers,should be run at first.
 * the struct for fpga is build in the driver,and it is important.
 */

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
#include <linux/poll.h>
#include <mach/spi_fpga.h>

#if defined(CONFIG_SPI_FPGA_INIT_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

struct spi_fpga_port *pFpgaPort;

#if SPI_FPGA_TRANS_WORK
#define ID_SPI_FPGA_WRITE 1
#define ID_SPI_FPGA_READ 2
struct spi_fpga_transfer
{
	const u8 *txbuf;
	unsigned n_tx;
	u8 *rxbuf;
	unsigned n_rx;
	int id;
	struct list_head	queue;
};

static void spi_fpga_trans_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, fpga_trans_work);
	unsigned long flags;
	
	while (!list_empty(&port->trans_queue)) 
	{
		struct spi_fpga_transfer	*t = NULL, *tmp;
		list_for_each_entry_safe(t, tmp, &port->trans_queue, queue)
		{

			if (t->id == 0) 
				break;	
			DBG("%s:id=%d,txbuf=0x%x\n",__FUNCTION__,t->id,(int)t->txbuf);
			switch(t->id)
			{
				case ID_SPI_FPGA_WRITE:
					spi_write(port->spi, t->txbuf, t->n_tx);
					break;
				default:
					break;
					
			}
			kfree(t->txbuf);
			kfree(t);
		}
		spin_lock_irqsave(&port->work_lock, flags);
		list_del_init(&port->trans_queue);
		spin_unlock_irqrestore(&port->work_lock, flags);
	}


}

int spi_write_work(struct spi_device *spi, const u8 *buf, size_t len)
{
	struct spi_fpga_port *port = spi_get_drvdata(spi);
	struct spi_fpga_transfer *t;
	unsigned long flags;

	t = kzalloc(sizeof(struct spi_fpga_transfer), GFP_KERNEL);
	if (!t)
	{
		printk("err:%s:ENOMEM\n",__FUNCTION__);
		return -ENOMEM;
	}

	t->txbuf = (char *)kmalloc(32, GFP_KERNEL);
	if(t->txbuf == NULL)
	{
	    printk("%s:t->txbuf kzalloc err!!!\n",__FUNCTION__);
	    return -ENOMEM;
	}

	memcpy(t->txbuf, buf, len);
	t->n_tx = len;
	t->id = ID_SPI_FPGA_WRITE;

	spin_lock_irqsave(&port->work_lock, flags);
	list_add_tail(&t->queue, &port->trans_queue);
	queue_work(port->fpga_trans_workqueue, &port->fpga_trans_work);
	spin_unlock_irqrestore(&port->work_lock, flags);

	return 0;

}

#endif


#if SPI_FPGA_POLL_WAIT

#define SPI_BUFSIZE 1028
static void spi_fpga_complete(void *arg)
{
	struct spi_fpga_port *port = pFpgaPort;
	msleep(5);
	wake_up_interruptible(&port->spi_wait_q);
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
}

int spi_fpga_write(struct spi_device *spi, const u8 *buf, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= buf,
			.len		= len,
		};
	struct spi_message	m;
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	return spi_async(spi, &m);
}

struct poll_table_struct wait;
struct file filp;
int spi_fpga_write_then_read(struct spi_device *spi,
		const u8 *txbuf, unsigned n_tx,
		u8 *rxbuf, unsigned n_rx)
{
	struct spi_fpga_port *port = spi_get_drvdata(spi);
	int			status;
	struct spi_message	message;
	struct spi_transfer	x[2];
	u8			*local_buf;
	printk("%s:line=%d,n_tx+n_rx=%d\n",__FUNCTION__,__LINE__,(n_tx + n_rx));
	/* Use preallocated DMA-safe buffer.  We can't avoid copying here,
	 * (as a pure convenience thing), but we can keep heap costs
	 * out of the hot path ...
	 */
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);

	if ((n_tx + n_rx) > SPI_BUFSIZE)
		return -EINVAL;
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);

	spi_message_init(&message);
	memset(x, 0, sizeof x);
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	if (n_tx) {
		x[0].len = n_tx;
		spi_message_add_tail(&x[0], &message);
	}
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);

	if (n_rx) {
		x[1].len = n_rx;
		spi_message_add_tail(&x[1], &message);
	}
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	/* ... unless someone else is using the pre-allocated buffer */

	local_buf = kmalloc(SPI_BUFSIZE, GFP_KERNEL);
	if (!local_buf)
		return -ENOMEM;

	memcpy(local_buf, txbuf, n_tx);
	x[0].tx_buf = local_buf;
	x[1].rx_buf = local_buf + n_tx;
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	message.complete = spi_fpga_complete;

	/* do the i/o */
	status = spi_async(spi, &message);
#if 1
	//poll_wait(&filp, &port->spi_wait_q, &wait);

	//if (status == 0)
	memcpy(rxbuf, x[1].rx_buf, n_rx);
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
	kfree(local_buf);
	printk("%s:line=%d\n",__FUNCTION__,__LINE__);
#endif
	return status;
}

#define spi_write spi_fpga_write
#define spi_write_then_read spi_fpga_write_then_read 

#endif

/*------------------------spi读写的基本函数-----------------------*/
unsigned int spi_in(struct spi_fpga_port *port, int reg, int type)
{
	unsigned char index = 0;
	unsigned char tx_buf[2], rx_buf[2], n_rx=2, stat=0;
	unsigned int result=0;
	//printk("index1=%d\n",index);

	switch(type)
	{
#if defined(CONFIG_SPI_FPGA_UART)
		case SEL_UART:
			index = port->uart.index;
			reg = (((reg) | ICE_SEL_UART) | ICE_SEL_READ | ICE_SEL_UART_CH(index));
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = 1;//give fpga 8 clks for reading data
			rx_buf[0] = 0;
			rx_buf[1] = 0;	
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_UART reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xff);
			break;
#endif

#if defined(CONFIG_SPI_FPGA_GPIO)
		case SEL_GPIO:
			reg = (((reg) | ICE_SEL_GPIO) | ICE_SEL_READ );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = 0;//give fpga 8 clks for reading data
			rx_buf[0] = 0;
			rx_buf[1] = 0;	
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_GPIO reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_FPGA_I2C)
		case SEL_I2C:
			reg = (((reg) | ICE_SEL_I2C) | ICE_SEL_READ );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = 0;
			rx_buf[0] = 0;
			rx_buf[1] = 0;				
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf)-1, rx_buf, n_rx);
			result =  rx_buf[1];
			DBG("%s,SEL_I2C reg=0x%x,result=0x%x \n",__FUNCTION__,reg&0xff,result&0xffff);					
			break;
#endif

#if defined(CONFIG_SPI_FPGA_DPRAM)
		case SEL_DPRAM:
			reg = (((reg) | ICE_SEL_DPRAM) & ICE_SEL_DPRAM_READ );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = 0;//give fpga 8 clks for reading data
			rx_buf[0] = 0;
			rx_buf[1] = 0;				
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_GPIO reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xffff);	
			break;
#endif
		case READ_TOP_INT:
			reg = (((reg) | ICE_SEL_UART) | ICE_SEL_READ);
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = 0;
			rx_buf[0] = 0;
			rx_buf[1] = 0;	
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf)-1, rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_INT reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xff);
			break;
		default:
			printk("%s err: Can not support this type!\n",__FUNCTION__);
			break;
	}

	return result;
}

void spi_out(struct spi_fpga_port *port, int reg, int value, int type)
{
	unsigned char index = 0;
	unsigned char tx_buf[3];
	int reg_temp = reg;
	switch(type)
	{
#if defined(CONFIG_SPI_FPGA_UART)
		case SEL_UART:
			index = port->uart.index;
			reg = ((((reg) | ICE_SEL_UART) & ICE_SEL_WRITE) | ICE_SEL_UART_CH(index));
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			if(reg_temp == UART_IER)
			spi_write_work(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			else
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_UART reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_FPGA_GPIO)
		case SEL_GPIO:
			reg = (((reg) | ICE_SEL_GPIO) & ICE_SEL_WRITE );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_GPIO reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_FPGA_I2C)
		case SEL_I2C:
			reg = (((reg) | ICE_SEL_I2C) & ICE_SEL_WRITE);
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_I2C reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);	
			break;
#endif
			
#if defined(CONFIG_SPI_FPGA_DPRAM)
		case SEL_DPRAM:
			reg = (((reg) | ICE_SEL_DPRAM) | ICE_SEL_DPRAM_WRITE );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_DPRAM reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);				
			break;
#endif

		default:
			printk("%s err: Can not support this type!\n",__FUNCTION__);
			break;
	}

}

#if SPI_FPGA_TEST_DEBUG
int spi_test_wrong_handle(void)
{
	gpio_direction_output(SPI_FPGA_TEST_DEBUG_PIN,0);
	udelay(2);
	gpio_direction_output(SPI_FPGA_TEST_DEBUG_PIN,1);
	printk("%s:give one trailing edge!\n",__FUNCTION__);
	return 0;
}

static int spi_test_request_gpio(int set)
{
	int ret;
	rk2818_mux_api_set(GPIOE0_VIPDATA0_SEL_NAME,0);
	ret = gpio_request(SPI_FPGA_TEST_DEBUG_PIN, NULL);
	if (ret) {
		printk("%s:failed to request SPI_FPGA_TEST_DEBUG_PIN pin\n",__FUNCTION__);
		return ret;
	}	
	gpio_direction_output(SPI_FPGA_TEST_DEBUG_PIN,set);

	return 0;
}

#endif

static void spi_fpga_irq_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, fpga_irq_work);
	struct spi_device 	*spi = port->spi;
	int ret,uart_ch=0;

	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	
	ret = spi_in(port, ICE_SEL_READ_INT_TYPE, READ_TOP_INT) & 0xff;
	if((ret | ICE_INT_TYPE_UART0) == ICE_INT_TYPE_UART0)
	{
#if defined(CONFIG_SPI_FPGA_UART)
		DBG("%s:ICE_INT_TYPE_UART0 ret=0x%x\n",__FUNCTION__,ret);
		port->uart.index = uart_ch;
		spi_uart_handle_irq(spi);
#endif
	}
	else if((ret | ICE_INT_TYPE_GPIO) == ICE_INT_TYPE_GPIO)
	{
#if defined(CONFIG_SPI_FPGA_GPIO)
		DBG("%s:ICE_INT_TYPE_GPIO ret=0x%x\n",__FUNCTION__,ret);
		spi_gpio_handle_irq(spi);
#endif
	}
	else if((ret | ICE_INT_TYPE_I2C2) == ICE_INT_TYPE_I2C2)
	{
#if defined(CONFIG_SPI_FPGA_I2C)
		DBG("%s:ICE_INT_TYPE_I2C2 ret=0x%x\n",__FUNCTION__,ret);
		spi_i2c_handle_irq(port,I2C_CH2);
#endif
	}
	else if((ret | ICE_INT_TYPE_I2C3) == ICE_INT_TYPE_I2C3)
	{
#if defined(CONFIG_SPI_FPGA_I2C)
		DBG("%s:ICE_INT_TYPE_I2C3 ret=0x%x\n",__FUNCTION__,ret);
		spi_i2c_handle_irq(port,I2C_CH3);
#endif
	}
	else if((ret | ICE_INT_TYPE_DPRAM) == ICE_INT_TYPE_DPRAM)
	{
#if defined(CONFIG_SPI_FPGA_DPRAM)
		DBG("%s:ICE_INT_TYPE_DPRAM ret=0x%x\n",__FUNCTION__,ret);
		spi_dpram_handle_ack(spi);
#endif
	}
	else if((ret | ICE_INT_TYPE_SLEEP) == ICE_INT_TYPE_SLEEP)
	{
		DBG("%s:ICE_INT_TYPE_SLEEP ret=0x%x\n",__FUNCTION__,ret);
		printk("FPGA wake up system now ...\n");
	}
	else
	{
		printk("warning:ret=0x%x\n",ret);
	}

	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
}


static irqreturn_t spi_fpga_irq(int irq, void *dev_id)
{
	struct spi_fpga_port *port = dev_id;
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	/*
	 * Can't do anything in interrupt context because we need to
	 * block (spi_sync() is blocking) so fire of the interrupt
	 * handling workqueue.
	 * Remember that we access ICE65LXX registers through SPI bus
	 * via spi_sync() call.
	 */
	 
	//schedule_work(&port->fpga_irq_work);
	queue_work(port->fpga_irq_workqueue, &port->fpga_irq_work);

	return IRQ_HANDLED;
}


static int spi_fpga_set_sysclk(int set)
{
	rk2818_mux_api_set(GPIOH7_HSADCCLK_SEL_NAME,IOMUXB_GPIO1_D7);	
	gpio_direction_output(SPI_FPGA_STANDBY_PIN,set);

	return 0;
}

static int spi_fpga_set_status(struct spi_fpga_port *port, int stat)
{
	if(stat == ICE_STATUS_SLEEP)
	{
		spi_out(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO0_INT_WAKE), ICE_STATUS_SLEEP, SEL_GPIO);
	}
	else
	{
		spi_out(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO0_INT_WAKE), ICE_STATUS_WAKE, SEL_GPIO);
	}

	return 0;
}

static int spi_fpga_rst(void)
{
	int ret;
	ret = gpio_request(SPI_FPGA_RST_PIN, NULL);
	if (ret) {
		printk("%s:failed to request fpga rst pin\n",__FUNCTION__);
		return ret;
	}
	rk2818_mux_api_set(GPIOH6_IQ_SEL_NAME,0);	
	gpio_direction_output(SPI_FPGA_RST_PIN,GPIO_HIGH);
	gpio_direction_output(SPI_FPGA_RST_PIN,GPIO_LOW);
	mdelay(1);
	gpio_direction_output(SPI_FPGA_RST_PIN,GPIO_HIGH);

	gpio_direction_input(SPI_FPGA_RST_PIN);

	return 0;
}

static int __devinit spi_fpga_probe(struct spi_device * spi)
{
	struct spi_fpga_port *port;
	int ret;
	char b[24];
	int num;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	/*
	 * bits_per_word cannot be configured in platform data
	*/
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;
	
	port = kzalloc(sizeof(struct spi_fpga_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;
	DBG("port=0x%x\n",(int)port);
	
	mutex_init(&port->spi_lock);
	spin_lock_init(&port->work_lock);
	ret = gpio_request(SPI_FPGA_STANDBY_PIN, NULL);
	if (ret) {
		printk("%s:failed to request standby pin\n",__FUNCTION__);
		return ret;
	}
	spi_fpga_set_sysclk(GPIO_HIGH);
	
#if SPI_FPGA_TRANS_WORK
	init_waitqueue_head(&port->wait_wq);
	init_waitqueue_head(&port->wait_rq);
	port->write_en = TRUE;
	port->read_en = TRUE;
	sprintf(b, "fpga_trans_workqueue");
	port->fpga_trans_workqueue = create_rt_workqueue(b);
	if (!port->fpga_trans_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->fpga_trans_work, spi_fpga_trans_work_handler);
	INIT_LIST_HEAD(&port->trans_queue);
#endif

	spi_fpga_rst();		//reset fpga
	
	sprintf(b, "fpga_irq_workqueue");
	port->fpga_irq_workqueue = create_rt_workqueue(b);
	if (!port->fpga_irq_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->fpga_irq_work, spi_fpga_irq_work_handler);
	
#if defined(CONFIG_SPI_FPGA_UART)
	ret = spi_uart_register(port);
	if(ret)
	{
		spi_uart_unregister(port);
		printk("%s:ret=%d,fail to spi_uart_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
#if defined(CONFIG_SPI_FPGA_GPIO)
	ret = spi_gpio_register(port);
	if(ret)
	{
		spi_gpio_unregister(port);
		printk("%s:ret=%d,fail to spi_gpio_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
#if defined(CONFIG_SPI_FPGA_I2C)

	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	spin_lock_init(&port->i2c.i2c_lock);
	for (num= 2;num<4;num++)
	{
		ret = spi_i2c_register(port,num);		
		if(ret)
		{
			spi_i2c_unregister(port);
			printk("%s:ret=%d,fail to spi_i2c_register\n",__FUNCTION__,ret);
			return ret;
		}
		DBG("spi_i2c spi_i2c.%d: i2c-%d: spi_i2c I2C adapter\n",num,num);
	}
#endif

#if defined(CONFIG_SPI_FPGA_DPRAM)
	ret = spi_dpram_register(port);
	if(ret)
	{
		spi_dpram_unregister(port);
		printk("%s:ret=%d,fail to spi_dpram_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
	port->spi = spi;
	spi_set_drvdata(spi, port);
	
	ret = gpio_request(SPI_FPGA_INT_PIN, NULL);
	if (ret) {
		printk("%s:failed to request fpga intterupt gpio\n",__FUNCTION__);
		goto err1;
	}

	rk2818_mux_api_set(CXGPIO_HSADC_SEL_NAME,IOMUXB_GPIO2_14_23);
	gpio_pull_updown(SPI_FPGA_INT_PIN,GPIOPullUp);
	ret = request_irq(gpio_to_irq(SPI_FPGA_INT_PIN),spi_fpga_irq,IRQF_TRIGGER_FALLING,NULL,port);
	if(ret)
	{
		printk("unable to request spi_uart irq\n");
		goto err2;
	}	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	pFpgaPort = port;

	
#if defined(CONFIG_SPI_FPGA_GPIO)
	spi_fpga_set_status(port, ICE_STATUS_WAKE);
	spi_gpio_init();
#endif

#if	SPI_FPGA_TEST_DEBUG
	spi_test_request_gpio(GPIO_HIGH);
#endif

	return 0;

err2:
	free_irq(gpio_to_irq(SPI_FPGA_INT_PIN),NULL);
err1:	
	gpio_free(SPI_FPGA_INT_PIN);

	return ret;
	

}

static int __devexit spi_fpga_remove(struct spi_device *spi)
{
	//struct spi_fpga_port *port = dev_get_drvdata(&spi->dev);

	
	return 0;
}

#ifdef CONFIG_PM

static int spi_fpga_wait_suspend(struct spi_fpga_port *port)
{
	int i,n_tx,n_rx;
	for(i=0;i<1000;i++)
	{
		n_tx = spi_in(port, UART_LSR, READ_TOP_INT);	//CONFIG_SPI_FPGA_UART = 1
		n_rx = spi_in(port, UART_LSR, SEL_UART);
		if((((n_tx >> 8) & 0x3f) == 0) && (((n_rx >> 8) & 0x3f) == 0))	//no data in tx_buf and rx_buf
		{
			printk("%s,i=%d\n",__FUNCTION__,i);
			return 0;	
		}
	}
	
	return -1;
}

static int spi_fpga_suspend(struct spi_device *spi, pm_message_t state)
{

	struct spi_fpga_port *port = dev_get_drvdata(&spi->dev);
	int ret;
	ret = spi_fpga_wait_suspend(port);
	if(!ret)
	{
		spi_fpga_set_status(port, ICE_STATUS_SLEEP);	//CONFIG_SPI_FPGA_GPIO = 1
		udelay(1);
		spi_fpga_set_sysclk(GPIO_LOW);
	}
	else
	{
		printk("fail to suspend fpga because it is sending or recieve data!\n");
		return -1;
	}

	printk("%s\n",__FUNCTION__);

	return 0;
}

static int spi_fpga_resume(struct spi_device *spi)
{

	struct spi_fpga_port *port = dev_get_drvdata(&spi->dev);
	spi_fpga_set_sysclk(GPIO_HIGH);
	udelay(1);
	spi_fpga_set_status(port, ICE_STATUS_WAKE);

	printk("%s\n",__FUNCTION__);

	return 0;
}

#else
#define spi_fpga_suspend NULL
#define spi_fpga_resume  NULL
#endif

static struct spi_driver spi_fpga_driver = {
	.driver = {
		.name		= "spi_fpga",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= spi_fpga_probe,
	.remove		= __devexit_p(spi_fpga_remove),
	.suspend	= spi_fpga_suspend,
	.resume		= spi_fpga_resume,
};

static int __init spi_fpga_init(void)
{
	return spi_register_driver(&spi_fpga_driver);
}

static void __exit spi_fpga_exit(void)
{
	spi_unregister_driver(&spi_fpga_driver);
}

subsys_initcall(spi_fpga_init);
module_exit(spi_fpga_exit);

MODULE_DESCRIPTION("Driver for spi2uart,spi2gpio,spi2i2c.");
MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_LICENSE("GPL");
