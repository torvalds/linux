/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

/**
 *  @addtogroup  DRIVERS
 *  @brief       Hardware drivers.
 *
 *  @{
 *      @file    inv_mpu_trigger.c
 *      @brief   A sysfs device driver for Invensense devices
 *      @details This file is part of inv mpu iio driver code
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#ifdef INV_KERNEL_3_10
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#else
#include "iio.h"
#include "sysfs.h"
#include "trigger.h"
#endif

#include "inv_mpu_iio.h"

/**
 * inv_mpu_data_rdy_trigger_set_state() set data ready interrupt state
 **/
static int inv_mpu_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
/*
	--yd 	struct iio_dev *indio_dev = trig->private_data;

	--yd 	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
*/
	return 0;
}

static const struct iio_trigger_ops inv_mpu_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &inv_mpu_data_rdy_trigger_set_state,
};

int inv_mpu_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
#ifdef INV_KERNEL_3_10
	st->trig = iio_trigger_alloc("%s-dev%d",
#else
	st->trig = iio_allocate_trigger("%s-dev%d",
#endif
					indio_dev->name,
					indio_dev->id);
	if (st->trig == NULL)
		return -ENOMEM;
	st->trig->dev.parent = st->dev;
#ifdef INV_KERNEL_3_10
	iio_trigger_set_drvdata(st->trig, indio_dev);
#else
	st->trig->private_data = indio_dev;
#endif
	st->trig->ops = &inv_mpu_trigger_ops;
	ret = iio_trigger_register(st->trig);

	if (ret) {
#ifdef INV_KERNEL_3_10
		iio_trigger_free(st->trig);
#else
		iio_free_trigger(st->trig);
#endif
		return -EPERM;
	}
	indio_dev->trig = st->trig;

	return 0;
}

void inv_mpu_remove_trigger(struct iio_dev *indio_dev)
{
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
#ifdef INV_KERNEL_3_10
	iio_trigger_free(st->trig);
#else
	iio_free_trigger(st->trig);
#endif
}
/**
 *  @}
 */

