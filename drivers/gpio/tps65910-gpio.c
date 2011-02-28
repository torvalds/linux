/*
 * tps65910_gpio.c -- access to GPIOs on TPS65910x chips
 *
 * Copyright (C) 2010 Mistral solutions Pvt Ltd <www.mistralsolutions.com>
 *
 * Based on twl4030-gpio.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/i2c/tps65910.h>

static int gpio_tps65910_remove(struct platform_device *pdev);

/*
 * The GPIO "subchip" supports 1 GPIOs which can be configured as
 * inputs or outputs, with pullups or pulldowns on each pin.  Each
 * GPIO can trigger interrupts on either or both edges.
 */


/* Data structures */
static struct gpio_chip tps65910_gpiochip;
static DEFINE_MUTEX(gpio_lock);
static unsigned int gpio_usage_count;
static struct work_struct  gpio_work;
static struct mutex work_lock;
/*
 * To configure TPS65910 GPIO registers
 */
static inline int gpio_tps65910_write(u8 address, u8 data)
{
	return tps65910_i2c_write_u8(TPS65910_I2C_ID0, data, address);
}


/*
 * To read a TPS65910 GPIO module register
 */
static inline int gpio_tps65910_read(u8 address)
{
	u8 data;
	int ret = 0;

	ret = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &data, address);
	return (ret < 0) ? ret : data;
}

static int tps65910_request(struct gpio_chip *chip, unsigned offset)
{
	int status = 0;

	mutex_lock(&gpio_lock);

	/* initialize TPS65910 GPIO */
	/* By default the GPIO_CKSYNC signal is GPIO */
	if (!gpio_usage_count)
		gpio_usage_count++;

	mutex_unlock(&gpio_lock);
	return status;
}

static void tps65910_free(struct gpio_chip *chip, unsigned offset)
{
	mutex_lock(&gpio_lock);

	/* on last use, switch off GPIO module */
	if (!gpio_usage_count)
		gpio_usage_count--;

	mutex_unlock(&gpio_lock);
}

static int tps65910_direction_in(struct gpio_chip *chip, unsigned offset)
{
	/* Configure TPS65910 GPIO as input */
	u8 val;

	mutex_lock(&gpio_lock);

	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	val &= ~(TPS65910_GPIO_CFG_OUTPUT);

	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	mutex_unlock(&gpio_lock);

	return 0;
}

static int tps65910_get(struct gpio_chip *chip, unsigned offset)
{
	int status = 0;

	mutex_lock(&gpio_lock);

	status = gpio_tps65910_read(TPS65910_REG_GPIO0);

	mutex_unlock(&gpio_lock);
	if (status & 0x01)
		return 1;
	else
		return 0;
}
static
int tps65910_direction_out(struct gpio_chip *chip, unsigned offset, int value)
{
	/* Configure TPS65910 GPIO as input */
	u8 val;
	u32 ret;
	mutex_lock(&gpio_lock);
	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	val |= TPS65910_GPIO_CFG_OUTPUT;

	ret = gpio_tps65910_write(TPS65910_REG_GPIO0, val);
	mutex_unlock(&gpio_lock);

	if (ret != 0)
		return -EIO;
	return 0;
}

static void tps65910_set(struct gpio_chip *chip, unsigned offset, int value)
{
	int val = 0;
	u32 ret;

	mutex_lock(&gpio_lock);
	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	if (value == 1)
		val |= 0x01;
	else
		val &= 0xFE;

	ret = gpio_tps65910_write(TPS65910_REG_GPIO0, val);

	mutex_unlock(&gpio_lock);
}



static void tps65910_gpio_set_debounce(u8 debounce)
{
	u8 val;

	mutex_lock(&gpio_lock);

	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	if (debounce == TPS65910_DEBOUNCE_91_5_MS)
		val = (0<<4);
	else if (debounce == TPS65910_DEBOUNCE_150_MS)
		val = (1<<4);
	else
		printk(KERN_ERR "Invalid argument to %s\n", __func__);

	gpio_tps65910_write(TPS65910_REG_GPIO0, val);

	mutex_unlock(&gpio_lock);
}
EXPORT_SYMBOL(tps65910_gpio_set_debounce);


static void tps65910_gpio_pullup_enable(void)
{
	u8 val;
	u32 ret;

	mutex_lock(&gpio_lock);

	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	val = (1<<3);

	ret = gpio_tps65910_write(TPS65910_REG_GPIO0, val);

	mutex_unlock(&gpio_lock);

	if (ret != 0)
		printk(KERN_ERR "Error writing to TPS65910_REG_GPIO0 in %s \n",
				__func__);
}
EXPORT_SYMBOL(tps65910_gpio_pullup_enable);

static void tps65910_gpio_pullup_disable(void)
{
	u8 val;
	u32 ret;

	mutex_lock(&gpio_lock);

	val = gpio_tps65910_read(TPS65910_REG_GPIO0);

	val = (0<<3);

	ret = gpio_tps65910_write(TPS65910_REG_GPIO0, val);

	mutex_unlock(&gpio_lock);
}
EXPORT_SYMBOL(tps65910_gpio_pullup_disable);

static void tps65910_gpio_work(struct work_struct *work)
{

	/* Read the status register and take action  */
	u8 status2;
	int err;
	mutex_lock(&work_lock);
	err =  tps65910_i2c_read_u8(TPS65910_I2C_ID0, &status2,
			TPS65910_REG_INT_STS);
	if (!err) {
		switch (status2) {
		case TPS65910_GPIO_F_IT:
			printk(KERN_NOTICE "Received TPS65910 GPIO falling \
				edge interrupt \n");
			/* Clear interrupt */
			tps65910_i2c_write_u8(TPS65910_I2C_ID0, status2,
					TPS65910_REG_INT_STS);
			/* Add code accroding to board requirment */
			break;
		case TPS65910_GPIO_R_IT:
			printk(KERN_NOTICE "Received TPS65910 GPIO Raising \
				 edge interrupt \n");
			/* Clear interrupt */
			tps65910_i2c_write_u8(TPS65910_I2C_ID0, status2,
					TPS65910_REG_INT_STS);
			/* Add code accroding to board requirment */
			break;
		}
	} else {
		printk(KERN_ERR"Could not read TPS65910_REG_INT_STS\n");
	}

	mutex_unlock(&work_lock);

}



static irqreturn_t tps65910_gpio_isr(int irq, void *_tps65910)
{
	/* Disable IRQ, schedule work, enable IRQ and  acknowledge */
	disable_irq(irq);
	(void) schedule_work(&gpio_work);
	enable_irq(irq);
	return IRQ_HANDLED;
}


static struct gpio_chip tps65910_gpiochip = {
	.label			= "tps65910",
	.owner			= THIS_MODULE,
	.request		= tps65910_request,
	.free			= tps65910_free,
	.direction_input	= tps65910_direction_in,
	.get			= tps65910_get,
	.direction_output	= tps65910_direction_out,
	.set			= tps65910_set,
};


static int __devinit gpio_tps65910_probe(struct platform_device *pdev)
{
	int ret = -1;
	int status = 0;

	struct tps65910_gpio *pdata = pdev->dev.platform_data;

	if (pdata->gpio_mode == TPS65910_GPIO_AS_IRQ) {

		if (pdata->irq_num) {
			status = request_irq(pdata->irq_num, tps65910_gpio_isr,
					IRQF_SHARED, "tps65910_gpio", pdev);
			if (status < 0) {
				pr_err("tps65910: could not claim irq%d: %d\n",
					pdata->irq_num, status);
			}

		}

		INIT_WORK(&gpio_work, tps65910_gpio_work);
		mutex_init(&work_lock);

		tps65910_gpiochip.ngpio = TPS65910_GPIO_MAX;
		tps65910_gpiochip.dev = &pdev->dev;

		ret = gpiochip_add(&tps65910_gpiochip);

		if (ret < 0) {
			dev_err(&pdev->dev, "could not register gpiochip \
				 %d\n", ret);
			tps65910_gpiochip.ngpio = 0;
			gpio_tps65910_remove(pdev);
			return -ENODEV;
		}
		if (pdata->gpio_setup)
			pdata->gpio_setup(pdata);
	}
	return ret;
}

static int gpio_tps65910_remove(struct platform_device *pdev)
{
	struct tps65910_gpio *pdata = pdev->dev.platform_data;
	int status;

	if (pdata->gpio_taredown)
		pdata->gpio_taredown(pdata);
	if (pdata->gpio_mode == TPS65910_GPIO_AS_IRQ)
		free_irq(pdata->irq_num, NULL);

	status = gpiochip_remove(&tps65910_gpiochip);
	if (status < 0)
		return status;
	return 0;
}

static struct platform_driver gpio_tps65910_driver = {
	.driver.name	= "tps65910_gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= gpio_tps65910_probe,
	.remove		= gpio_tps65910_remove,
};

static int __init gpio_tps65910_init(void)
{
	return platform_driver_register(&gpio_tps65910_driver);
}
subsys_initcall(gpio_tps65910_init);

static void __exit gpio_tps65910_exit(void)
{
	platform_driver_unregister(&gpio_tps65910_driver);
}
module_exit(gpio_tps65910_exit);

MODULE_AUTHOR("Mistral Solutions Pvt Ltd.");
MODULE_DESCRIPTION("GPIO interface for TPS65910");
MODULE_LICENSE("GPL");
