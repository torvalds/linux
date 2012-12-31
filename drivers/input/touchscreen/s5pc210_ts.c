/* drivers/input/touschcreen/s5pc210_ts.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com
 *
 * Samsung S5PC210 10.1" touchscreen driver
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
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>

#include <asm/system.h>

#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

#include <mach/irqs.h>
#include <mach/regs-gpio.h>

#include "s5pc210_ts.h"
#include "s5pc210_ts_gpio_i2c.h"
#include "s5pc210_ts_sysfs.h"

static int s5pv310_ts_open(struct input_dev *dev);
static void s5pv310_ts_close(struct input_dev *dev);

static void s5pv310_ts_release_device(struct device *dev);
static void s5pv310_ts_config(unsigned char state);

unsigned int irq_count;
struct s5pv310_ts_t s5pv310_ts;

#define CONFIG_DEBUG_S5PV310_TS_MSG 1
#define CAL_DELAY 1000

static int s5pv310_ts_cal(void)
{
	unsigned char   wdata;

	/*   INT_mode : disable interrupt */
	wdata = 0x00;
	if (s5pv310_ts_write(MODULE_INTMODE, &wdata, 1)) {
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
		printk(KERN_ERR "failed to write disable.\n");
#endif
		return -1;
	}

	/*   touch calibration */
	wdata = 0x03;
	/*   set mode */
	if (s5pv310_ts_write(MODULE_CALIBRATION, &wdata, 1)) {
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
		printk(KERN_ERR "failed to write cal.\n");
#endif
		return -1;
	}

#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "calibration!!!\n");
#endif
	mdelay(CAL_DELAY);

	/*   INT_mode : enable interrupt, low-active, periodically*/
	wdata = 0x09;
	if (s5pv310_ts_write(MODULE_INTMODE, &wdata, 1)) {
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
		printk(KERN_ERR "failed to write enable.\n");
#endif
		return -1;
	}

	return 0;
}

static void s5pv310_ts_process_data(struct touch_process_data_t *ts_data)
{
	/* read address setup */
	s5pv310_ts_write(0x00, NULL, 0x00);

	/* Acc data read */
	write_seqlock(&s5pv310_ts.lock);
	s5pv310_ts_read(&s5pv310_ts.rd[0], 10);

	write_sequnlock(&s5pv310_ts.lock);

	ts_data->finger_cnt = s5pv310_ts.rd[0] & 0x03;

	if ((ts_data->x1 = ((s5pv310_ts.rd[3] << 8) | s5pv310_ts.rd[2]))) {
		ts_data->x1 = (ts_data->x1 * 134) / 100;
#ifdef CONFIG_S5PV310_TS_FLIP
#else
		/* flip X & resize */
		ts_data->x1 = TS_ABS_MAX_X - ts_data->x1;
#endif
	}

	/* resize */
	if ((ts_data->y1 = ((s5pv310_ts.rd[5] << 8) | s5pv310_ts.rd[4]))) {
		ts_data->y1 = (ts_data->y1 * 134) / 100;
#ifdef CONFIG_S5PV310_TS_FLIP
		/* flip Y & resize */
		ts_data->y1 = TS_ABS_MAX_Y - ts_data->y1;
#else
#endif
	}
	if (ts_data->finger_cnt > 1) {
		/* flip X & resize */
		if ((ts_data->x2 = ((s5pv310_ts.rd[7] << 8) | s5pv310_ts.rd[6]))) {
			ts_data->x2 = (ts_data->x2 * 133) / 100;
#ifdef CONFIG_S5PV310_TS_FLIP
#else
			ts_data->x2 = TS_ABS_MAX_X - ts_data->x2;
#endif
		}
		/* resize */
		if ((ts_data->y2 = ((s5pv310_ts.rd[9] << 8) | s5pv310_ts.rd[8]))) {

			ts_data->y2 = (ts_data->y2 * 128) / 100;
#ifdef CONFIG_S5PV310_TS_FLIP
			/* flip Y & resize */
			ts_data->y2 = TS_ABS_MAX_Y - ts_data->y2;
#else
#endif
		}
	}
}

static void s5pv310_ts_get_data(void)
{
	struct touch_process_data_t ts_data;

	memset(&ts_data, 0x00, sizeof(ts_data));

	s5pv310_ts_process_data(&ts_data);
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "finger: %d\n", ts_data.finger_cnt);
	printk(KERN_DEBUG "x1: %d, y1: %d\n", ts_data.x1, ts_data.y1);
	printk(KERN_DEBUG "x2: %d, y2: %d\n", ts_data.x2, ts_data.y2);
#endif

	if (ts_data.finger_cnt == 0 && ts_data.x1 == 0 && ts_data.y1 == 0) {
		if (irq_count > 10) {
			s5pv310_ts_cal();
			irq_count = 0;
		}
		irq_count++;
	}

	if (ts_data.finger_cnt > 0 && ts_data.finger_cnt < 3) {
		s5pv310_ts.x = ts_data.x1;
		s5pv310_ts.y = ts_data.y1;
		/* press */
		input_report_abs(s5pv310_ts.driver,
				ABS_MT_TOUCH_MAJOR, 200);
		input_report_abs(s5pv310_ts.driver,
				ABS_MT_WIDTH_MAJOR, 10);
		input_report_abs(s5pv310_ts.driver,
				ABS_MT_POSITION_X, s5pv310_ts.x);
		input_report_abs(s5pv310_ts.driver,
				ABS_MT_POSITION_Y, s5pv310_ts.y);
		input_mt_sync(s5pv310_ts.driver);

		if (ts_data.finger_cnt == 2) {
			s5pv310_ts.x = ts_data.x2;
			s5pv310_ts.y = ts_data.y2;
			 /* press */
			input_report_abs(s5pv310_ts.driver,
					ABS_MT_TOUCH_MAJOR, 200);
			input_report_abs(s5pv310_ts.driver,
					ABS_MT_WIDTH_MAJOR, 10);
			input_report_abs(s5pv310_ts.driver,
					ABS_MT_POSITION_X, s5pv310_ts.x);
			input_report_abs(s5pv310_ts.driver,
					ABS_MT_POSITION_Y, s5pv310_ts.y);
			input_mt_sync(s5pv310_ts.driver);
		}
		input_sync(s5pv310_ts.driver);
		irq_count = 0;
	} else {
		 /* up */
		input_mt_sync(s5pv310_ts.driver);
		input_sync(s5pv310_ts.driver);
	}
}

irqreturn_t s5pv310_ts_irq(int irq, void *dev_id)
{
	unsigned long flags;

	local_irq_save(flags);
	local_irq_disable();
	s5pv310_ts_get_data();
	local_irq_restore(flags);
	return	IRQ_HANDLED;
}

static int s5pv310_ts_open(struct input_dev *dev)
{
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "%s\n", __func__);
#endif

	return	0;
}

static void s5pv310_ts_close(struct input_dev *dev)
{
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "%s\n", __func__);
#endif
}

static void s5pv310_ts_release_device(struct device *dev)
{
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "%s\n", __func__);
#endif
}

static void s5pv310_ts_config(unsigned char state)
{
	unsigned char	wdata;

	/* s5pc210_ts_reset(); */
	s5pv310_ts_port_init();
	mdelay(500);

	/* Touchscreen Active mode */
	wdata = 0x00;
	s5pv310_ts_write(MODULE_POWERMODE, &wdata, 1);
	mdelay(100);

	if (state == TOUCH_STATE_BOOT) {
		/* INT_mode : disable interrupt */
		wdata = 0x00;
		s5pv310_ts_write(MODULE_INTMODE, &wdata, 1);

		if ((soc_is_exynos4212() || soc_is_exynos4412()) &&
					samsung_board_rev_is_0_1()) {
			s5p_register_gpio_interrupt(TS_ATTB);
			s3c_gpio_cfgpin(TS_ATTB, S3C_GPIO_SFN(0xf));
		}

		if (!request_irq(S5PV310_TS_IRQ, s5pv310_ts_irq,
			IRQF_DISABLED, "s5pc210-Touch IRQ",
					(void *)&s5pv310_ts))
			printk(KERN_DEBUG "MT TOUCH request_irq = %d\r\n",
					S5PV310_TS_IRQ);
		else
			printk(KERN_ERR "MT TOUCH request_irq = %d error!! \r\n",
					S5PV310_TS_IRQ);

		if (gpio_is_valid(TS_ATTB)) {
			if (gpio_request(TS_ATTB, "TS_ATTB"))
				printk(KERN_ERR "failed to request GPH1 for TS_ATTB..\n");
		}

		s3c_gpio_cfgpin(TS_ATTB, (0xf << 20));
		s3c_gpio_setpull(TS_ATTB, S3C_GPIO_PULL_NONE);

		irq_set_irq_type(S5PV310_TS_IRQ, IRQ_TYPE_EDGE_RISING);

		/* seqlock init */
		seqlock_init(&s5pv310_ts.lock);

		s5pv310_ts.seq = 0;

	} else {
		/* INT_mode : disable interrupt, low-active, finger moving */
		wdata = 0x01;
		if (s5pv310_ts_write(MODULE_INTMODE, &wdata, 1)) {
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
			printk(KERN_ERR "failed to write disable.\n");
#endif
		}

		mdelay(CAL_DELAY);
		/* INT_mode : enable interrupt, low-active, finger moving */
		wdata = 0x09;
		if (s5pv310_ts_write(MODULE_INTMODE, &wdata, 1)) {
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
			printk(KERN_ERR "failed to write enable.\n");
#endif
		}
		mdelay(100);
	}
}

static int __devinit s5pv310_ts_probe(struct platform_device *pdev)
{
	int rc;

	irq_count = 0;
	/* struct init */
	memset(&s5pv310_ts, 0x00, sizeof(s5pv310_ts));

	/* create sys_fs */
	rc = s5pv310_ts_sysfs_create(pdev);
	if (rc) {
		printk(KERN_ERR "%s : sysfs_create fail.\n", __func__);
		return rc;
	}

	s5pv310_ts.driver = input_allocate_device();

	if (!(s5pv310_ts.driver)) {
		printk(KERN_ERR "%s : cdev_alloc() no memory.\n", __func__);
		s5pv310_ts_sysfs_remove(pdev);
		return -ENOMEM;
	}

	s5pv310_ts.driver->name = S5PV310_TS_DEVICE_NAME;
	s5pv310_ts.driver->phys = "s5pv310_ts/input1";
	s5pv310_ts.driver->open = s5pv310_ts_open;
	s5pv310_ts.driver->close = s5pv310_ts_close;

	s5pv310_ts.driver->id.bustype = BUS_HOST;
	s5pv310_ts.driver->id.vendor = 0x16B4;
	s5pv310_ts.driver->id.product = 0x0702;
	s5pv310_ts.driver->id.version = 0x0001;

	set_bit(EV_ABS, s5pv310_ts.driver->evbit);

	/* multi touch */
	input_set_abs_params(s5pv310_ts.driver, ABS_MT_POSITION_X,
				TS_ABS_MIN_X, TS_ABS_MAX_X, 0, 0);
	input_set_abs_params(s5pv310_ts.driver, ABS_MT_POSITION_Y,
				TS_ABS_MIN_Y, TS_ABS_MAX_Y, 0, 0);
	input_set_abs_params(s5pv310_ts.driver, ABS_MT_TOUCH_MAJOR,
				0, 255, 2, 0);
	input_set_abs_params(s5pv310_ts.driver, ABS_MT_WIDTH_MAJOR,
				0, 15, 2, 0);

	if (input_register_device(s5pv310_ts.driver)) {
		printk(KERN_ERR "S5PC210 TS input register device fail.\n");
		s5pv310_ts_sysfs_remove(pdev);
		input_free_device(s5pv310_ts.driver);
		return	-ENODEV;
	}

	s5pv310_ts_config(TOUCH_STATE_BOOT);
	s5pv310_ts_cal();

	printk(KERN_DEBUG "SMDKC210(MT) Touch driver initialized.\n");

	return	0;
}

static int __devexit s5pv310_ts_remove(struct platform_device *pdev)
{
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "%s\n", __func__);
#endif

	free_irq(S5PV310_TS_IRQ, (void *)&s5pv310_ts);

	s5pv310_ts_sysfs_remove(pdev);

	input_unregister_device(s5pv310_ts.driver);

	return  0;
}

#ifdef CONFIG_PM
static int s5pv310_ts_resume(struct platform_device *dev)
{
	s5pv310_ts_config(TOUCH_STATE_RESUME);

	/* interrupt enable */
	enable_irq(S5PV310_TS_IRQ);

	return	0;
}

static int s5pv310_ts_suspend(struct platform_device *dev, pm_message_t state)
{
	unsigned char wdata;

	wdata = 0x00;
	s5pv310_ts_write(MODULE_POWERMODE, &wdata, 1);
	mdelay(CAL_DELAY);

	/* INT_mode : disable interrupt */
	wdata = 0x00;
	s5pv310_ts_write(MODULE_INTMODE, &wdata, 1);
	mdelay(CAL_DELAY);

	/* Touchscreen enter freeze mode : */
	wdata = 0x01;
	s5pv310_ts_write(MODULE_POWERMODE, &wdata, 1);
	mdelay(100);

	/* interrupt disable */
	disable_irq(S5PV310_TS_IRQ);

	return	0;
}
#else
static int s5pv310_ts_resume(struct platform_device *dev)
{
	return 0;
}
static int s5pv310_ts_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}
#endif

static struct platform_driver s5pv310_ts_platform_device_driver = {
	.probe		= s5pv310_ts_probe,
	.remove		= s5pv310_ts_remove,
	.suspend	= s5pv310_ts_suspend,
	.resume		= s5pv310_ts_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= S5PV310_TS_DEVICE_NAME,
	},
};

static struct platform_device s5pv310_ts_platform_device = {
	.name		= S5PV310_TS_DEVICE_NAME,
	.id		= -1,
	.num_resources	= 0,
	.dev 		= {
		.release= s5pv310_ts_release_device,
	},
};

static int __init s5pv310_ts_init(void)
{
	int ret = platform_driver_register(&s5pv310_ts_platform_device_driver);

	if (!ret) {
		ret = platform_device_register(&s5pv310_ts_platform_device);

#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "platform_driver_register %d\n", ret);
#endif

		if (ret)
			platform_driver_unregister(
				&s5pv310_ts_platform_device_driver);
	}
	return ret;
}

static void __exit s5pv310_ts_exit(void)
{
#ifdef CONFIG_DEBUG_S5PV310_TS_MSG
	printk(KERN_DEBUG "%s\n", __func__);
#endif
	platform_device_unregister(&s5pv310_ts_platform_device);
	platform_driver_unregister(&s5pv310_ts_platform_device_driver);
}
module_init(s5pv310_ts_init);
module_exit(s5pv310_ts_exit);

MODULE_DESCRIPTION("Samsung 10.1\" Touchscreen driver");
MODULE_AUTHOR("Dongsu Ha <dsfine.ha@samsung.com>");
MODULE_AUTHOR("HardKernel");
MODULE_LICENSE("GPL");
