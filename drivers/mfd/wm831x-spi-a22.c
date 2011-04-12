/*
 * wm831x-spi.c  --  SPI access for Wolfson WM831x PMICs
 *
 * Copyright 2009,2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/otp.h>
#include <linux/mfd/wm831x/regulator.h>
#include <linux/mfd/wm831x/pmu.h>



/* DC-DC*/
#define WM831X_BUCKV_MAX_SELECTOR 0x68
#define WM831X_BUCKP_MAX_SELECTOR 0x66

#define WM831X_DCDC_MODE_FAST    0
#define WM831X_DCDC_MODE_NORMAL  1
#define WM831X_DCDC_MODE_IDLE    2
#define WM831X_DCDC_MODE_STANDBY 3

//#define WM831X_DCDC_MAX_NAME 6

/* Register offsets in control block */
#define WM831X_DCDC_CONTROL_1     0
#define WM831X_DCDC_CONTROL_2     1
#define WM831X_DCDC_ON_CONFIG     2
#define WM831X_DCDC_SLEEP_CONTROL 3
#define WM831X_DCDC_DVS_CONTROL   4

/* LDO*/
#define WM831X_LDO_CONTROL       0
#define WM831X_LDO_ON_CONTROL    1
#define WM831X_LDO_SLEEP_CONTROL 2

#define WM831X_ALIVE_LDO_ON_CONTROL    0
#define WM831X_ALIVE_LDO_SLEEP_CONTROL 1

static int wm831x_spi_read_device(struct wm831x *wm831x, unsigned short reg,
				  int bytes, void *dest)
{
	u16 tx_val;
	u16 *d = dest;
	int r, ret;

	/* Go register at a time */
	for (r = reg; r < reg + (bytes / 2); r++) {
		tx_val = cpu_to_be16(r | 0x8000);

		ret = spi_write_then_read(wm831x->control_data,
					  (u8 *)&tx_val, 2, (u8 *)d, 2);
		if (ret != 0)
			return ret;

		//*d = be16_to_cpu(*d);

		d++;
	}

	return 0;
}

static int wm831x_spi_write_device(struct wm831x *wm831x, unsigned short reg,
				   int bytes, void *src)
{
	struct spi_device *spi = wm831x->control_data;
	u16 *s = src;
	u16 data[2];
	int ret, r;

	/* Go register at a time */
	for (r = reg; r < reg + (bytes / 2); r++) {
		data[0] = cpu_to_be16(r);
		data[1] = *s++;
		//printk("%s:reg=0x%x,data=0x%x\n",__FUNCTION__,be16_to_cpu(data[0]),be16_to_cpu(data[1]));
		ret = spi_write(spi, (char *)&data, sizeof(data));
		if (ret != 0)
			return ret;
	}

	return 0;
}


int wm831x_isinkv_values[WM831X_ISINK_MAX_ISEL + 1] = {
	2,
	2,
	3,
	3,
	4,
	5,
	6,
	7,
	8,
	10,
	11,
	13,
	16,
	19,
	23,
	27,
	32,
	38,
	45,
	54,
	64,
	76,
	91,
	108,
	128,
	152,
	181,
	215,
	256,
	304,
	362,
	431,
	512,
	609,
	724,
	861,
	1024,
	1218,
	1448,
	1722,
	2048,
	2435,
	2896,
	3444,
	4096,
	4871,
	5793,
	6889,
	8192,
	9742,
	11585,
	13777,
	16384,
	19484,
	23170,
	27554,
};
EXPORT_SYMBOL_GPL(wm831x_isinkv_values);

static int wm831x_reg_locked(struct wm831x *wm831x, unsigned short reg)
{
	if (!wm831x->locked)
		return 0;

	switch (reg) {
	case WM831X_WATCHDOG:
	case WM831X_DC4_CONTROL:
	case WM831X_ON_PIN_CONTROL:
	case WM831X_BACKUP_CHARGER_CONTROL:
	case WM831X_CHARGER_CONTROL_1:
	case WM831X_CHARGER_CONTROL_2:
		return 1;

	default:
		return 0;
	}
}

/**
 * wm831x_reg_unlock: Unlock user keyed registers
 *
 * The WM831x has a user key preventing writes to particularly
 * critical registers.  This function locks those registers,
 * allowing writes to them.
 */
void wm831x_reg_lock(struct wm831x *wm831x)
{
	int ret;

	ret = wm831x_reg_write(wm831x, WM831X_SECURITY_KEY, 0);
	if (ret == 0) {
		dev_vdbg(wm831x->dev, "Registers locked\n");

		mutex_lock(&wm831x->io_lock);
		WARN_ON(wm831x->locked);
		wm831x->locked = 1;
		mutex_unlock(&wm831x->io_lock);
	} else {
		dev_err(wm831x->dev, "Failed to lock registers: %d\n", ret);
	}

}
EXPORT_SYMBOL_GPL(wm831x_reg_lock);

/**
 * wm831x_reg_unlock: Unlock user keyed registers
 *
 * The WM831x has a user key preventing writes to particularly
 * critical registers.  This function locks those registers,
 * preventing spurious writes.
 */
int wm831x_reg_unlock(struct wm831x *wm831x)
{
	int ret;

	/* 0x9716 is the value required to unlock the registers */
	ret = wm831x_reg_write(wm831x, WM831X_SECURITY_KEY, 0x9716);
	if (ret == 0) {
		dev_vdbg(wm831x->dev, "Registers unlocked\n");

		mutex_lock(&wm831x->io_lock);
		WARN_ON(!wm831x->locked);
		wm831x->locked = 0;
		mutex_unlock(&wm831x->io_lock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_reg_unlock);

static int wm831x_read(struct wm831x *wm831x, unsigned short reg,
		       int bytes, void *dest)
{
	int ret, i;
	u16 *buf = dest;

	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

	ret = wm831x->read_dev(wm831x, reg, bytes, dest);
	if (ret < 0)
		return ret;

	for (i = 0; i < bytes / 2; i++) {
		buf[i] = be16_to_cpu(buf[i]);

		dev_vdbg(wm831x->dev, "Read %04x from R%d(0x%x)\n",
			 buf[i], reg + i, reg + i);
	}

	return 0;
}

/**
 * wm831x_reg_read: Read a single WM831x register.
 *
 * @wm831x: Device to read from.
 * @reg: Register to read.
 */
int wm831x_reg_read(struct wm831x *wm831x, unsigned short reg)
{
	unsigned short val;
	int ret;

	mutex_lock(&wm831x->io_lock);

	ret = wm831x_read(wm831x, reg, 2, &val);

	mutex_unlock(&wm831x->io_lock);

	if (ret < 0)
		return ret;
	else
		return val;
}
EXPORT_SYMBOL_GPL(wm831x_reg_read);

/**
 * wm831x_bulk_read: Read multiple WM831x registers
 *
 * @wm831x: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.
 */
int wm831x_bulk_read(struct wm831x *wm831x, unsigned short reg,
		     int count, u16 *buf)
{
	int ret;

	mutex_lock(&wm831x->io_lock);

	ret = wm831x_read(wm831x, reg, count * 2, buf);

	mutex_unlock(&wm831x->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_bulk_read);

static int wm831x_write(struct wm831x *wm831x, unsigned short reg,
			int bytes, void *src)
{
	u16 *buf = src;
	int i;

	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

	for (i = 0; i < bytes / 2; i++) {
		if (wm831x_reg_locked(wm831x, reg))
			return -EPERM;

		dev_vdbg(wm831x->dev, "Write %04x to R%d(0x%x)\n",
			 buf[i], reg + i, reg + i);

		buf[i] = cpu_to_be16(buf[i]);
	}

	return wm831x->write_dev(wm831x, reg, bytes, src);
}

/**
 * wm831x_reg_write: Write a single WM831x register.
 *
 * @wm831x: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 */
int wm831x_reg_write(struct wm831x *wm831x, unsigned short reg,
		     unsigned short val)
{
	int ret;

	mutex_lock(&wm831x->io_lock);

	ret = wm831x_write(wm831x, reg, 2, &val);

	mutex_unlock(&wm831x->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_reg_write);


static int wm831x_init(struct wm831x *wm831x)
{

/*wm831x_pre_init*/
	wm831x_reg_write(wm831x, WM831X_POWER_STATE, 0x8804);	//900ma
	
/*wm831x_irq_init:irq=252,180 mask irq*/
	wm831x_reg_write(wm831x, 0x4019, 0xffff);	
	wm831x_reg_write(wm831x, 0x401a, 0xffff);	
	wm831x_reg_write(wm831x, 0x401b, 0xffff);	
	wm831x_reg_write(wm831x, 0x401c, 0xffff);	
	wm831x_reg_write(wm831x, 0x401d, 0xffff);	
	wm831x_reg_write(wm831x, 0x4018, 0xffff);//wm831x_reg_write(wm831x, 0x4018, 0x0);	
	wm831x_reg_write(wm831x, 0x4019, 0xffff);	

/*regulator: DCDC1: 600 <--> 1800 mV */
	//wm831x_reg_write(wm831x, 0x401c, 0xfffe);
	//wm831x_reg_write(wm831x, 0x401c, 0xfefe);

/*regulator: DCDC2: 600 <--> 1800 mV*/ 
	//wm831x_reg_write(wm831x, 0x401c, 0xfefc);
	//wm831x_reg_write(wm831x, 0x401c, 0xfcfc);

/* regulator: DCDC3: 850 <--> 3400 mV */
	//wm831x_reg_write(wm831x, 0x401c, 0xfcf8);

/*regulator: DCDC4: 0 <--> 30000 mV */
	//wm831x_reg_write(wm831x, 0x401c, 0xfcf0);

/*wm831x_isink_enable*/
	wm831x_reg_write(wm831x, 0x404e, 0x8500);
	wm831x_reg_write(wm831x, 0x404e, 0xc500);

/*wm831x_isink_probe:line=203,irq=220*/
	//wm831x_reg_write(wm831x, 0x401a, 0xffbf);
	//wm831x_reg_write(wm831x, 0x401a, 0xff3f);


/*regulator: LDO1: 900 <--> 3300 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xfffe);

/*regulator: LDO2: 900 <--> 3300 mV*/ 
	//wm831x_reg_write(wm831x, 0x401b, 0xfffc);

/*regulator: LDO3: 900 <--> 3300 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xfff8);

/*regulator: LDO4: 900 <--> 3300 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xfff0);

/* regulator: LDO5: 900 <--> 3300 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xffe0);

/*regulator: LDO6: 900 <--> 3300 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xffc0);

/*regulator: LDO7: 1000 <--> 3500 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xff80);

/*regulator: LDO8: 1000 <--> 3500 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xff00);

/*regulator: LDO9: 1000 <--> 3500 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xfe00);

/*regulator: LDO10: 1000 <--> 3500 mV */
	//wm831x_reg_write(wm831x, 0x401b, 0xfc00);

/*regulator: LDO11: 1200 <--> 1550 mV */
	wm831x_reg_write(wm831x, 0x4008, 0x9716);
	wm831x_reg_write(wm831x, 0x4006, 0x8463);

/*wm831x_post_init set dcdc3=3000000mV end*/
	wm831x_reg_write(wm831x, 0x4051, 0xfa49);
	wm831x_reg_write(wm831x, 0x4062, 0x2156);
	

/*wm831x_post_init set ldo10=3000000mV end*/
	wm831x_reg_write(wm831x, 0x4084, 0x201a);

/*wm831x_post_init set dcdc2=1300000mV end8*/
	wm831x_reg_write(wm831x, 0x405d, 0x4140);

/* wm831x_post_init set dcdc1=1800000mV end*/
	wm831x_reg_write(wm831x, 0x4058, 0x6168);

/*wm831x_post_init set ldo1=1800000mV end*/
	wm831x_reg_write(wm831x, 0x4069, 0x6010);

/*wm831x_post_init set ldo4=2500000mV end*/
	wm831x_reg_write(wm831x, 0x4072, 0x8017);

/*wm831x_post_init set ldo7=3300000mV end*/
	wm831x_reg_write(wm831x, 0x407b, 0xa01d);

/*wm831x_post_init set dcdc4=-22mV end*/
	wm831x_reg_write(wm831x, 0x4050, 0xf);

/*wm831x_post_init set ldo2=3000000mV end*/
	wm831x_reg_write(wm831x, 0x406c, 0x1c);
	wm831x_reg_write(wm831x, 0x4051, 0x24b);

/*wm831x_post_init set ldo3=1800000mV end*/
	wm831x_reg_write(wm831x, 0x406f, 0x10);
	wm831x_reg_write(wm831x, 0x4051, 0x24f);

/*wm831x_post_init set ldo5=3000000mV end*/
	wm831x_reg_write(wm831x, 0x4075, 0x1c);
	wm831x_reg_write(wm831x, 0x4051, 0x25f);

/*wm831x_post_init set ldo6=2800000mV end*/
	wm831x_reg_write(wm831x, 0x4078, 0x1a);
	wm831x_reg_write(wm831x, 0x4051, 0x27f);

/*wm831x_post_init set ldo8=1200000mV end*/
	wm831x_reg_write(wm831x, 0x407e, 0x4);
	wm831x_reg_write(wm831x, 0x4051, 0x2ff);

/*wm831x_post_init set ldo9=3000000mV end*/
	wm831x_reg_write(wm831x, 0x4081, 0x1a);
	wm831x_reg_write(wm831x, 0x4051, 0x3ff);

	wm831x_reg_write(wm831x, 0x4008, 0x0);

	wm831x_reg_write(wm831x, 0x4008, 0x9716);
	wm831x_reg_write(wm831x, 0x4064, 0x104);
	wm831x_reg_write(wm831x, 0x4008, 0x0);
	wm831x_reg_write(wm831x, 0x4050, 0x7);

/* backlight brightness=255*/
	wm831x_reg_write(wm831x, 0x404e, 0xc500);
	wm831x_reg_write(wm831x, 0x4050, 0xf);
	wm831x_reg_write(wm831x, 0x404e, 0xc535);
	wm831x_reg_write(wm831x, 0x404e, 0xc535);


/*wm831x-rtc wm831x-rtc: rtc core: registered wm831x as rtc0*/
	//wm831x_reg_write(wm831x, 0x4019, 0xeef7);
	//wm831x_reg_write(wm831x, 0x4019, 0xeef3);
	
/*wm831x_power_probe:wm831x_power initialized*/
	wm831x_reg_write(wm831x, 0x4008, 0x9716);
	wm831x_reg_write(wm831x, 0x404b, 0x8812);

	wm831x_reg_write(wm831x, 0x4008, 0x0);
	wm831x_reg_write(wm831x, 0x4008, 0x9716);
	wm831x_reg_write(wm831x, 0x4048, 0x9c21);

	wm831x_reg_write(wm831x, 0x4048, 0x9c21);

	wm831x_reg_write(wm831x, 0x4049, 0x44ff);
	wm831x_reg_write(wm831x, 0x4001, 0x57);
	wm831x_reg_write(wm831x, 0x4008, 0x0);	
	//wm831x_reg_write(wm831x, 0x4019, 0x6ef3);
	//wm831x_reg_write(wm831x, 0x4019, 0x2ef3);	
	
/*device-mapper: uevent: version 1.0.3*/
	wm831x_reg_write(wm831x, 0x402e, 0x8000);
	wm831x_reg_write(wm831x, 0x4014, 0x8);
	wm831x_reg_write(wm831x, 0x402f, 0x400);
	wm831x_reg_write(wm831x, 0x402e, 0xc000);

/*gpu: power on... done!*/
	wm831x_reg_write(wm831x, 0x402e, 0x0);
	wm831x_reg_write(wm831x, 0x4011, 0x2100);
	wm831x_reg_write(wm831x, 0x402e, 0x8000);
	wm831x_reg_write(wm831x, 0x402f, 0x200);
	wm831x_reg_write(wm831x, 0x402e, 0xc000);


/*wm831x_isink_is_enabled:line=85*/
/*wm831x-rtc wm831x-rtc: setting system clock to 1970-01-02 04:18:35 UTC (101915)*/
	wm831x_reg_write(wm831x, 0x402e, 0x0);
	wm831x_reg_write(wm831x, 0x4011, 0x100);

	wm831x_reg_write(wm831x, 0x402e, 0x8000);
	wm831x_reg_write(wm831x, 0x402f, 0x100);
	wm831x_reg_write(wm831x, 0x402e, 0xc000);
	wm831x_reg_write(wm831x, 0x4011, 0x100);
	wm831x_reg_write(wm831x, 0x402e, 0x0);

	printk("%s\n",__FUNCTION__);
	
}

extern void rk29_send_power_key(int state);
static int gNumInt = 0, gNumTimer = 0;
static struct timer_list 	irq_timer;
static struct wm831x *gwm831x;

void wm831x_power_off(void)
{
	wm831x_reg_write(gwm831x, WM831X_POWER_STATE, 0);//power off
}

static void wm831x_irq_timer(unsigned long data)
{
	struct wm831x *wm831x = (struct wm831x *)data;
	int pin = irq_to_gpio(wm831x->irq);

	if(gNumInt >0)
	{
		if(gpio_get_value(pin) > 0)	
		gNumTimer++;
		else
		gNumTimer = 0;

		if(gNumTimer >20)
		{
			rk29_send_power_key(0);
			gNumTimer = 0;
			gNumInt = 0;
		}
	}
		
	irq_timer.expires  = jiffies + msecs_to_jiffies(20);
	add_timer(&irq_timer);

}

static void wm831x_irq_worker(struct work_struct *work)
{
	struct wm831x *wm831x = container_of(work, struct wm831x, irq_work);	
	wm831x_reg_write(wm831x, WM831X_INTERRUPT_STATUS_1, 0xffff);//clear all intterupt
	gNumInt++;
	rk29_send_power_key(1);
	enable_irq(wm831x->irq);	
	wake_unlock(&wm831x->irq_wake);
	//printk("%s,irq=%d\n",__FUNCTION__,wm831x->irq);
}

static irqreturn_t wm831x_irq_thread(int irq, void *data)
{
	struct wm831x *wm831x = data;

	disable_irq_nosync(irq);
	wake_lock(&wm831x->irq_wake);
	queue_work(wm831x->irq_wq, &wm831x->irq_work);

	return IRQ_HANDLED;
}

static int __devinit wm831x_spi_probe(struct spi_device *spi)
{
	struct wm831x *wm831x;
	enum wm831x_parent type;
	int ret,gpio,irq;
	
	/* Currently SPI support for ID tables is unmerged, we're faking it */
	if (strcmp(spi->modalias, "wm8310") == 0)
		type = WM8310;
	else if (strcmp(spi->modalias, "wm8311") == 0)
		type = WM8311;
	else if (strcmp(spi->modalias, "wm8312") == 0)
		type = WM8312;
	else if (strcmp(spi->modalias, "wm8320") == 0)
		type = WM8320;
	else if (strcmp(spi->modalias, "wm8321") == 0)
		type = WM8321;
	else if (strcmp(spi->modalias, "wm8325") == 0)
		type = WM8325;
	else {
		dev_err(&spi->dev, "Unknown device type\n");
		return -EINVAL;
	}

	wm831x = kzalloc(sizeof(struct wm831x), GFP_KERNEL);
	if (wm831x == NULL)
		return -ENOMEM;

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	gpio = spi->irq;
	ret = gpio_request(gpio, "wm831x");
	if (ret) {
		printk( "failed to request rk gpio irq for wm831x \n");
		return ret;
	}
	gpio_pull_updown(gpio, GPIOPullUp);
	if (ret) {
	    printk("failed to pull up gpio irq for wm831x \n");
		return ret;
	}	
	irq = gpio_to_irq(gpio);

	dev_set_drvdata(&spi->dev, wm831x);
	wm831x->dev = &spi->dev;
	wm831x->control_data = spi;
	wm831x->read_dev = wm831x_spi_read_device;
	wm831x->write_dev = wm831x_spi_write_device;
	gwm831x = wm831x;
	mutex_init(&wm831x->io_lock);
	
	wm831x_init(wm831x);
	
	wm831x->irq_wq = create_singlethread_workqueue("wm831x-irq");
	if (!wm831x->irq_wq) {
		dev_err(wm831x->dev, "Failed to allocate IRQ worker\n");
		return -ESRCH;
	}
	
	INIT_WORK(&wm831x->irq_work, wm831x_irq_worker);
	wake_lock_init(&wm831x->irq_wake, WAKE_LOCK_SUSPEND, "wm831x_irq_wake");

	ret = request_threaded_irq(irq, wm831x_irq_thread, NULL, 
				 IRQF_TRIGGER_LOW,
				   "wm831x", wm831x);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to request IRQ %d: %d\n",
			wm831x->irq, ret);
		return ret;
	}
	wm831x->irq = irq;
	enable_irq_wake(irq); // so wm831x irq can wake up system
	/* only support on intterupt */
	wm831x_reg_write(wm831x, WM831X_SYSTEM_INTERRUPTS_MASK, 0xefff);
	wm831x_reg_write(wm831x, WM831X_INTERRUPT_STATUS_1_MASK, 0xefff);

	setup_timer(&irq_timer, wm831x_irq_timer, (unsigned long)wm831x);
	irq_timer.expires  = jiffies+2000;
	add_timer(&irq_timer);

	return 0;
	//return wm831x_device_init(wm831x, type, irq);
}

static int __devexit wm831x_spi_remove(struct spi_device *spi)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);

	//wm831x_device_exit(wm831x);

	return 0;
}

static int wm831x_spi_suspend(struct spi_device *spi, pm_message_t m)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);
	return 0;
	//return wm831x_device_suspend(wm831x);
}

static int wm831x_spi_resume(struct spi_device *spi)
{
	return 0;
}

static struct spi_driver wm8310_spi_driver = {
	.driver = {
		.name	= "wm8310",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
	.resume	= wm831x_spi_resume,
};

static struct spi_driver wm8311_spi_driver = {
	.driver = {
		.name	= "wm8311",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8312_spi_driver = {
	.driver = {
		.name	= "wm8312",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8320_spi_driver = {
	.driver = {
		.name	= "wm8320",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8321_spi_driver = {
	.driver = {
		.name	= "wm8321",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8325_spi_driver = {
	.driver = {
		.name	= "wm8325",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static int __init wm831x_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&wm8310_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8310 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8311_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8311 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8312_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8312 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8320_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8320 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8321_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8321 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8325_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8325 SPI driver: %d\n", ret);

	return 0;
}
subsys_initcall(wm831x_spi_init);

static void __exit wm831x_spi_exit(void)
{
	spi_unregister_driver(&wm8325_spi_driver);
	spi_unregister_driver(&wm8321_spi_driver);
	spi_unregister_driver(&wm8320_spi_driver);
	spi_unregister_driver(&wm8312_spi_driver);
	spi_unregister_driver(&wm8311_spi_driver);
	spi_unregister_driver(&wm8310_spi_driver);
}
module_exit(wm831x_spi_exit);

MODULE_DESCRIPTION("SPI support for WM831x/2x AudioPlus PMICs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown");
