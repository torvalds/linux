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

#include "spi_fpga.h"

#if defined(CONFIG_SPI_FPGA_INIT_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif
struct spi_fpga_port *pFpgaPort;

/*------------------------spi读写的基本函数-----------------------*/
unsigned int spi_in(struct spi_fpga_port *port, int reg, int type)
{
	unsigned char index = 0;
	unsigned char tx_buf[1], rx_buf[2], n_rx=2, stat=0;
	unsigned int result=0;
	//printk("index1=%d\n",index);

	switch(type)
	{
#if defined(CONFIG_SPI_UART)
		case SEL_UART:
			index = port->uart.index;
			reg = (((reg) | ICE_SEL_UART) | ICE_SEL_READ | ICE_SEL_UART_CH(index));
			tx_buf[0] = reg & 0xff;
			rx_buf[0] = 0;
			rx_buf[1] = 0;	
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = rx_buf[1];
			DBG("%s,SEL_UART reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xff);
			break;
#endif

#if defined(CONFIG_SPI_GPIO)
		case SEL_GPIO:
			reg = (((reg) | ICE_SEL_GPIO) | ICE_SEL_READ );
			tx_buf[0] = reg & 0xff;
			rx_buf[0] = 0;
			rx_buf[1] = 0;	
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_GPIO reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_I2C)
		case SEL_I2C:
			reg = (((reg) | ICE_SEL_I2C) & ICE_SEL_READ );
			tx_buf[0] = reg & 0xff;
			rx_buf[0] = 0;
			rx_buf[1] = 0;				
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_I2C reg=0x%x,result=0x%x [0x%x] [0x%x]\n",__FUNCTION__,reg&0xff,result&0xffff,rx_buf[0],rx_buf[1]);					
			break;
#endif

#if defined(CONFIG_SPI_DPRAM)
		case SEL_DPRAM:
			reg = (((reg) | ICE_SEL_DPRAM) & ICE_SEL_DPRAM_READ );
			tx_buf[0] = reg & 0xff;
			rx_buf[0] = 0;
			rx_buf[1] = 0;				
			stat = spi_write_then_read(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf), rx_buf, n_rx);
			result = (rx_buf[0] << 8) | rx_buf[1];
			DBG("%s,SEL_GPIO reg=0x%x,result=0x%x\n",__FUNCTION__,reg&0xff,result&0xffff);	
			break;
#endif
		default:
			printk("Can not support this type!\n");
			break;
	}

	return result;
}

void spi_out(struct spi_fpga_port *port, int reg, int value, int type)
{
	unsigned char index = 0;
	unsigned char tx_buf[3];
	//printk("index2=%d,",index);
	switch(type)
	{
#if defined(CONFIG_SPI_UART)
		case SEL_UART:
			index = port->uart.index;
			reg = ((((reg) | ICE_SEL_UART) & ICE_SEL_WRITE) | ICE_SEL_UART_CH(index));
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_UART reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_GPIO)
		case SEL_GPIO:
			reg = (((reg) | ICE_SEL_GPIO) & ICE_SEL_WRITE );
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_GPIO reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);
			break;
#endif

#if defined(CONFIG_SPI_I2C)

		case SEL_I2C:
			reg = (((reg) | ICE_SEL_I2C) & ICE_SEL_WRITE);
			tx_buf[0] = reg & 0xff;
			tx_buf[1] = (value>>8) & 0xff;
			tx_buf[2] = value & 0xff;
			spi_write(port->spi, (const u8 *)&tx_buf, sizeof(tx_buf));
			DBG("%s,SEL_I2C reg=0x%x,value=0x%x\n",__FUNCTION__,reg&0xff,value&0xffff);	
			break;
#endif
			
#if defined(CONFIG_SPI_DPRAM)
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
			printk("Can not support this type!\n");
			break;
	}

}


static void spi_fpga_irq_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, fpga_irq_work);
	struct spi_device 	*spi = port->spi;
	int ret,uart_ch,gpio_ch;

	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	
	ret = spi_in(port, ICE_SEL_READ_INT_TYPE, SEL_UART);
	if((ret | ICE_INT_TYPE_UART0) == ICE_INT_TYPE_UART0)
	{
#if defined(CONFIG_SPI_UART)
		uart_ch = 0;
		printk("Enter::%s,LINE=%d,uart_ch=%d,uart.index=%d\n",__FUNCTION__,__LINE__,uart_ch,port->uart.index);
		port->uart.index = uart_ch;
		spi_uart_handle_irq(spi);
#endif
	}
	else if((ret | ICE_INT_TYPE_GPIO) == ICE_INT_TYPE_GPIO)
	{
		gpio_ch = 0;
		printk("Enter::%s,LINE=%d,gpio_ch=%d\n",__FUNCTION__,__LINE__,gpio_ch);
#if defined(CONFIG_SPI_GPIO)
		spi_gpio_handle_irq(spi);
#endif
	}
	else if((ret | ICE_INT_TYPE_I2C2) == ICE_INT_TYPE_I2C2)
	{
#if defined(CONFIG_SPI_I2C)
		spi_i2c_handle_irq(port,0);
#endif
	}
	else if((ret | ICE_INT_TYPE_I2C3) == ICE_INT_TYPE_I2C3)
	{
#if defined(CONFIG_SPI_I2C)
		spi_i2c_handle_irq(port,1);
#endif
	}
	else if((ret | ICE_INT_TYPE_DPRAM) == ICE_INT_TYPE_DPRAM)
	{
#if defined(CONFIG_SPI_DPRAM)
		spi_dpram_handle_irq(spi);
#endif
	}
	else
	{
		printk("%s:NO such INT TYPE\n",__FUNCTION__);
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


static int spi_open_sysclk(int set)
{
	int ret;
	ret = gpio_request(SPI_FPGA_STANDBY_PIN, NULL);
	if (ret) {
		printk("%s:failed to request standby pin\n",__FUNCTION__);
		return ret;
	}
	rk2818_mux_api_set(GPIOH7_HSADCCLK_SEL_NAME,IOMUXB_GPIO1_D7);	
	gpio_direction_output(SPI_FPGA_STANDBY_PIN,set);

	return 0;
}


static int __devinit spi_fpga_probe(struct spi_device * spi)
{
	struct spi_fpga_port *port;
	int ret;
	char b[12];
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

	spi_open_sysclk(GPIO_HIGH);

	sprintf(b, "fpga_irq_workqueue");
	port->fpga_irq_workqueue = create_freezeable_workqueue(b);
	if (!port->fpga_irq_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->fpga_irq_work, spi_fpga_irq_work_handler);
	
#if defined(CONFIG_SPI_UART)
	ret = spi_uart_register(port);
	if(ret)
	{
		spi_uart_unregister(port);
		printk("%s:ret=%d,fail to spi_uart_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
#if defined(CONFIG_SPI_GPIO)
	ret = spi_gpio_register(port);
	if(ret)
	{
		spi_gpio_unregister(port);
		printk("%s:ret=%d,fail to spi_gpio_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
#if 0 //defined(CONFIG_SPI_I2C)

	printk("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	ret = spi_i2c_register(port);
	printk("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	if(ret)
	{
		spi_i2c_unregister(port);
		printk("%s:ret=%d,fail to spi_i2c_register\n",__FUNCTION__,ret);
		return ret;
	}
#endif
#if defined(CONFIG_SPI_DPRAM)
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

	gpio_pull_updown(SPI_FPGA_INT_PIN,GPIOPullUp);
	ret = request_irq(gpio_to_irq(SPI_FPGA_INT_PIN),spi_fpga_irq,IRQF_TRIGGER_RISING,NULL,port);
	if(ret)
	{
		printk("unable to request spi_uart irq\n");
		goto err2;
	}	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	pFpgaPort = port;
	
#if defined(CONFIG_SPI_GPIO)
	spi_gpio_init();
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

static int spi_fpga_suspend(struct spi_device *spi, pm_message_t state)
{
	//struct spi_fpga_port *port = dev_get_drvdata(&spi->dev);

	return 0;
}

static int spi_fpga_resume(struct spi_device *spi)
{
	//struct spi_fpga_port *port = dev_get_drvdata(&spi->dev);

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

module_init(spi_fpga_init);
module_exit(spi_fpga_exit);

MODULE_DESCRIPTION("Driver for spi2uart,spi2gpio,spi2i2c.");
MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_LICENSE("GPL");
