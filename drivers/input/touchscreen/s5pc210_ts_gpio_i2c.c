/* drivers/input/touschcreen/s5pc210_ts_gpio_i2c.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com
 *
 * Samsung S5PC210 10.1" touchscreen gpio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the term of the GNU General Public License as published by
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright 2010 Hardkernel Co.,Ltd. <odroid@hardkernel.com>
 * Copyright 2010 Samsung Electronics <samsung.com>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/fs.h>

#include <mach/irqs.h>
#include <asm/system.h>

#include <linux/delay.h>
#include <mach/regs-gpio.h>

#include "s5pc210_ts_gpio_i2c.h"
#include "s5pc210_ts.h"


/* Touch I2C Address Define */
#define	TOUCH_WR_ADDR 0xB8
#define	TOUCH_RD_ADDR 0xB9

/* Touch I2C Port define */
#ifdef CONFIG_MACH_SMDK4X12

#define GPD0CON (S5P_VA_GPIO + 0xA0)
#define GPD0DAT (S5P_VA_GPIO + 0xA4)

#define	SDA_CON_PORT (*(unsigned long *)GPD0CON)
#define	SDA_DAT_PORT (*(unsigned long *)GPD0DAT)
#define	SDA_PIN 2

#define	CLK_CON_PORT (*(unsigned long *)GPD0CON)
#define	CLK_DAT_PORT (*(unsigned long *)GPD0DAT)
#define	CLK_PIN 3

#elif defined (CONFIG_MACH_SMDKV310)

#define GPB1CON (S5P_VA_GPIO + 0x40)
#define GPB1DAT (S5P_VA_GPIO + 0x44)

#define	SDA_CON_PORT (*(unsigned long *)GPB1CON)
#define	SDA_DAT_PORT (*(unsigned long *)GPB1DAT)
#define	SDA_PIN 6

#define	CLK_CON_PORT (*(unsigned long *)GPB1CON)
#define	CLK_DAT_PORT (*(unsigned long *)GPB1DAT)
#define	CLK_PIN 7

#else
#error Unsupported board!
#endif

#define	DELAY_TIME 5
#define	PORT_CHANGE_DELAY_TIME 5
#define	CON_PORT_MASK 0xF
#define	CON_PORT_OFFSET 0x4

#define	GPIO_CON_INPUT 0x0
#define	GPIO_CON_OUTPUT 0x1

#define	HIGH 1
#define	LOW 0

static void gpio_i2c_sda_port_control(unsigned char inout);
static void gpio_i2c_clk_port_control(unsigned char inout);
static unsigned char gpio_i2c_get_sda(void);
static void gpio_i2c_set_sda(unsigned char hi_lo);
static void gpio_i2c_set_clk(unsigned char hi_lo);
static void gpio_i2c_start(void);
static void gpio_i2c_stop(void);
static void gpio_i2c_send_ack(void);
static void gpio_i2c_send_noack(void);
static unsigned char gpio_i2c_chk_ack(void);
static void gpio_i2c_byte_write(unsigned char wdata);
static void gpio_i2c_byte_read(unsigned char *rdata);

static void gpio_i2c_sda_port_control(unsigned char inout)
{
	SDA_CON_PORT &= (unsigned long)(~(CON_PORT_MASK <<
					(SDA_PIN * CON_PORT_OFFSET)));
	SDA_CON_PORT |= (unsigned long)((inout <<
					(SDA_PIN * CON_PORT_OFFSET)));
}

static void gpio_i2c_clk_port_control(unsigned char inout)
{
	CLK_CON_PORT &=  (unsigned long)(~(CON_PORT_MASK <<
					(CLK_PIN * CON_PORT_OFFSET)));
	CLK_CON_PORT |=  (unsigned long)((inout <<
					(CLK_PIN * CON_PORT_OFFSET)));
}

static unsigned char gpio_i2c_get_sda(void)
{
	return	SDA_DAT_PORT & (HIGH << SDA_PIN) ? 1 : 0;
}

static void gpio_i2c_set_sda(unsigned char hi_lo)
{
	if (hi_lo) {
		gpio_i2c_sda_port_control(GPIO_CON_INPUT);
		udelay(PORT_CHANGE_DELAY_TIME);
	} else {
		SDA_DAT_PORT &= ~(HIGH << SDA_PIN);
		gpio_i2c_sda_port_control(GPIO_CON_OUTPUT);
		udelay(PORT_CHANGE_DELAY_TIME);
	}
}

static void gpio_i2c_set_clk(unsigned char hi_lo)
{
	if (hi_lo) {
		gpio_i2c_clk_port_control(GPIO_CON_INPUT);
		udelay(PORT_CHANGE_DELAY_TIME);
	} else {
		CLK_DAT_PORT &= ~(HIGH << CLK_PIN);
		gpio_i2c_clk_port_control(GPIO_CON_OUTPUT);
		udelay(PORT_CHANGE_DELAY_TIME);
	}
}

static void gpio_i2c_start(void)
{
	/* Setup SDA, CLK output High */
	gpio_i2c_set_sda(HIGH);
	gpio_i2c_set_clk(HIGH);

	udelay(DELAY_TIME);

	/* SDA low before CLK low */
	gpio_i2c_set_sda(LOW);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(LOW);
	udelay(DELAY_TIME);
}

static void gpio_i2c_stop(void)
{
	/* Setup SDA, CLK output low */
	gpio_i2c_set_sda(LOW);
	gpio_i2c_set_clk(LOW);

	udelay(DELAY_TIME);

	/* SDA high after CLK high */
	gpio_i2c_set_clk(HIGH);
	udelay(DELAY_TIME);
	gpio_i2c_set_sda(HIGH);
	udelay(DELAY_TIME);
}

static void gpio_i2c_send_ack(void)
{
	/* SDA Low */
	gpio_i2c_set_sda(LOW);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(HIGH);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(LOW);
	udelay(DELAY_TIME);
}

static void gpio_i2c_send_noack(void)
{
	/* SDA High */
	gpio_i2c_set_sda(HIGH);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(HIGH);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(LOW);
	udelay(DELAY_TIME);
}

static unsigned char gpio_i2c_chk_ack(void)
{
	unsigned char count = 0, ret = 0;

	gpio_i2c_set_sda(LOW);
	udelay(DELAY_TIME);
	gpio_i2c_set_clk(HIGH);
	udelay(DELAY_TIME);

	gpio_i2c_sda_port_control(GPIO_CON_INPUT);
	udelay(PORT_CHANGE_DELAY_TIME);

	while (gpio_i2c_get_sda()) {
		if (count++ > 100) {
			ret = 1;
			break;
		} else
			udelay(DELAY_TIME);
	}

	gpio_i2c_set_clk(LOW);
	udelay(DELAY_TIME);

#if defined(DEBUG_GPIO_I2C)
	if (ret)
		printk(KERN_DEBUG "%s %d: no ack\n", __func__, ret);
	else
		printk(KERN_DEBUG "%s %d: ack\n" , __func__, ret);
#endif

	return	ret;
}

static void gpio_i2c_byte_write(unsigned char wdata)
{
	unsigned char cnt, mask;

	for (cnt = 0, mask = 0x80; cnt < 8; cnt++, mask >>= 1) {
		if (wdata & mask)
			gpio_i2c_set_sda(HIGH);
		else
			gpio_i2c_set_sda(LOW);

		gpio_i2c_set_clk(HIGH);
		udelay(DELAY_TIME);
		gpio_i2c_set_clk(LOW);
		udelay(DELAY_TIME);
	}
}

static void gpio_i2c_byte_read(unsigned char *rdata)
{
	unsigned char cnt, mask;

	gpio_i2c_sda_port_control(GPIO_CON_INPUT);
	udelay(PORT_CHANGE_DELAY_TIME);

	for (cnt = 0, mask = 0x80, *rdata = 0; cnt < 8; cnt++, mask >>= 1) {
		gpio_i2c_set_clk(HIGH);
		udelay(DELAY_TIME);

		if (gpio_i2c_get_sda())
			*rdata |= mask;

		gpio_i2c_set_clk(LOW);
		udelay(DELAY_TIME);
	}
}

int s5pv310_ts_write(unsigned char addr, unsigned char *wdata,
			unsigned char wsize)
{
	unsigned char cnt, ack;

	/* start */
	gpio_i2c_start();

	/* i2c address */
	gpio_i2c_byte_write(TOUCH_WR_ADDR);

	ack = gpio_i2c_chk_ack();
	if (ack) {
#if defined(DEBUG_GPIO_I2C)
		printk(KERN_DEBUG "%s [write addr] : no ack\n", __func__);
#endif
		goto write_stop;
	}

	/* register */
	gpio_i2c_byte_write(addr);

	ack = gpio_i2c_chk_ack();
	if (ack)	{
#if defined(DEBUG_GPIO_I2C)
		printk(KERN_DEBUG "%s [write reg] : no ack\n", __func__);
#endif
	}

	if (wsize) {
		for (cnt = 0; cnt < wsize; cnt++) {
			gpio_i2c_byte_write(wdata[cnt]);
			ack = gpio_i2c_chk_ack();
			if (ack) {
#if defined(DEBUG_GPIO_I2C)
				printk(KERN_DEBUG "%s [write reg]:no ack\n", __func__);
#endif
				goto write_stop;
			}
		}
	}

write_stop:
#if defined(CONFIG_TOUCHSCREEN_EXYNOS4)
	if (wsize)
		gpio_i2c_stop();
#else
	gpio_i2c_stop();
#endif

#if defined(DEBUG_GPIO_I2C)
	printk(KERN_DEBUG "%s : %d\n", __func__, ack);
#endif
	return	ack;
}

int s5pv310_ts_read(unsigned char *rdata, unsigned char rsize)
{
	unsigned char ack, cnt;

	/* start */
	gpio_i2c_start();

	/* i2c address */
	gpio_i2c_byte_write(TOUCH_RD_ADDR);

	ack = gpio_i2c_chk_ack();
	if (ack) {
#if defined(DEBUG_GPIO_I2C)
		printk(KERN_DEBUG "%s [write addr] : no ack\n", __func__);
#endif
		goto read_stop;
	}

	for (cnt = 0; cnt < rsize; cnt++) {
		gpio_i2c_byte_read(&rdata[cnt]);

		if (cnt == rsize - 1)
			gpio_i2c_send_noack();
		else
			gpio_i2c_send_ack();
	}

read_stop:
	gpio_i2c_stop();
#if defined(DEBUG_GPIO_I2C)
	printk(KERN_DEBUG "%s : %d\n", __func__, ack);
#endif
	return	ack;
}

void s5pv310_ts_port_init(void)
{
	gpio_i2c_set_sda(HIGH);
	gpio_i2c_set_clk(HIGH);
}
