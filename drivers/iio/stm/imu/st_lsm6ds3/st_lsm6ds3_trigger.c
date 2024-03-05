// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lsm6ds3 trigger driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2014-2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/iio/events.h>
#include <linux/version.h>

#include "st_lsm6ds3.h"

#define ST_LSM6DS3_DIS_BIT				0x00
#define ST_LSM6DS3_SRC_FUNC_ADDR			0x53
#define ST_LSM6DS3_FIFO_DATA_AVL_ADDR			0x3b
#define ST_LSM6DS3_ACCEL_DATA_AVL_ADDR			0x1e

#define ST_LSM6DS3_ACCEL_DATA_AVL			0x01
#define ST_LSM6DS3_GYRO_DATA_AVL			0x02
#define ST_LSM6DS3_SRC_STEP_DETECTOR_DATA_AVL		0x10
#define ST_LSM6DS3_SRC_TILT_DATA_AVL			0x20
#define ST_LSM6DS3_SRC_STEP_COUNTER_DATA_AVL		0x80
#define ST_LSM6DS3_SRC_STEP_COUNTER_DATA_OVR		0x08
#define ST_LSM6DS3_FIFO_DATA_AVL			0x80
#define ST_LSM6DS3_FIFO_DATA_OVR			0x40


static irqreturn_t lsm6ds3_irq_management(int irq, void *private)
{
	int err;
	bool push;
	bool force_read_accel = false;
	struct lsm6ds3_data *cdata = private;
	u8 src_accel_gyro = 0, src_dig_func = 0;

	cdata->timestamp =
					iio_get_time_ns(cdata->indio_dev[ST_MASK_ID_ACCEL]);

	if ((cdata->sensors_enabled & ~cdata->sensors_use_fifo) &
			(BIT(ST_MASK_ID_ACCEL) | BIT(ST_MASK_ID_GYRO) |
						BIT(ST_MASK_ID_EXT0))) {
		err = cdata->tf->read(cdata, ST_LSM6DS3_ACCEL_DATA_AVL_ADDR,
						1, &src_accel_gyro, true);
		if (err < 0)
			goto read_fifo_status;

		if (src_accel_gyro & ST_LSM6DS3_ACCEL_DATA_AVL) {
#ifdef CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT
			if ((cdata->sensors_enabled & ~cdata->sensors_use_fifo)
						& BIT(ST_MASK_ID_EXT0)) {
				cdata->nofifo_decimation[ST_MASK_ID_EXT0].num_samples++;
				force_read_accel = true;

				if ((cdata->nofifo_decimation[ST_MASK_ID_EXT0].num_samples %
						cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator) == 0) {
					push = true;
					cdata->nofifo_decimation[ST_MASK_ID_EXT0].num_samples = 0;
				} else {
					push = false;
				}

				lsm6ds3_read_output_data(cdata, ST_MASK_ID_EXT0, push);
			}
#endif /* CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT */

			if ((cdata->sensors_enabled & ~cdata->sensors_use_fifo) &
							BIT(ST_MASK_ID_ACCEL)) {
				cdata->nofifo_decimation[ST_MASK_ID_ACCEL].num_samples++;

				if ((cdata->nofifo_decimation[ST_MASK_ID_ACCEL].num_samples %
						cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator) == 0) {
					push = true;
					cdata->nofifo_decimation[ST_MASK_ID_ACCEL].num_samples = 0;
				} else {
					push = false;
				}

				lsm6ds3_read_output_data(cdata, ST_MASK_ID_ACCEL, push);
			} else {
				if (force_read_accel)
					lsm6ds3_read_output_data(cdata, ST_MASK_ID_ACCEL, false);
			}

		}

		if (src_accel_gyro & ST_LSM6DS3_GYRO_DATA_AVL) {
			if ((cdata->sensors_enabled & ~cdata->sensors_use_fifo) & BIT(ST_MASK_ID_GYRO))
				lsm6ds3_read_output_data(cdata, ST_MASK_ID_GYRO, true);
		}
	}

read_fifo_status:
	if (cdata->sensors_use_fifo)
		st_lsm6ds3_read_fifo(cdata, false);

	err = cdata->tf->read(cdata, ST_LSM6DS3_SRC_FUNC_ADDR,
						1, &src_dig_func, true);
	if (err < 0)
		goto exit_irq;

	if (src_dig_func & ST_LSM6DS3_SRC_STEP_DETECTOR_DATA_AVL) {
		if (cdata->sensors_enabled & BIT(ST_MASK_ID_STEP_DETECTOR)) {
			st_lsm6ds3_push_data_with_timestamp(cdata,
				ST_MASK_ID_STEP_DETECTOR, NULL, cdata->timestamp);
		}

		if ((cdata->sign_motion_event_ready) &&
				(cdata->sensors_enabled &
					BIT(ST_MASK_ID_SIGN_MOTION))) {
			iio_push_event(cdata->indio_dev[
				ST_MASK_ID_SIGN_MOTION],
				IIO_UNMOD_EVENT_CODE(STM_IIO_SIGN_MOTION,
				0, IIO_EV_TYPE_THRESH, IIO_EV_DIR_EITHER),
				cdata->timestamp);

			cdata->sign_motion_event_ready = false;
		}
	}

	if (src_dig_func & ST_LSM6DS3_SRC_STEP_COUNTER_DATA_OVR)
		cdata->num_steps += (1 << 16);

	if (src_dig_func & ST_LSM6DS3_SRC_STEP_COUNTER_DATA_AVL)
		iio_trigger_poll_chained(cdata->trig[ST_MASK_ID_STEP_COUNTER]);

	if ((src_dig_func & ST_LSM6DS3_SRC_TILT_DATA_AVL) &&
				(cdata->sensors_enabled & BIT(ST_MASK_ID_TILT))) {
		st_lsm6ds3_push_data_with_timestamp(cdata,
				ST_MASK_ID_TILT, NULL, cdata->timestamp);
	}

exit_irq:
	return IRQ_HANDLED;
}

int st_lsm6ds3_allocate_triggers(struct lsm6ds3_data *cdata,
				const struct iio_trigger_ops *trigger_ops)
{
	int err, i, n;

	for (i = 0; i < ST_INDIO_DEV_NUM; i++) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,13,0)
		cdata->trig[i] = iio_trigger_alloc(cdata->dev,
						"%s-trigger",
						cdata->indio_dev[i]->name);
#else /* LINUX_VERSION_CODE */
		cdata->trig[i] = iio_trigger_alloc("%s-trigger",
						cdata->indio_dev[i]->name);
#endif /* LINUX_VERSION_CODE */
		if (!cdata->trig[i]) {
			dev_err(cdata->dev,
					"failed to allocate iio trigger.\n");
			err = -ENOMEM;
			goto deallocate_trigger;
		}
		iio_trigger_set_drvdata(cdata->trig[i], cdata->indio_dev[i]);
		cdata->trig[i]->ops = trigger_ops;
		cdata->trig[i]->dev.parent = cdata->dev;
	}

	err = request_threaded_irq(cdata->irq, NULL, lsm6ds3_irq_management,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					cdata->name, cdata);
	if (err)
		goto deallocate_trigger;

	for (n = 0; n < ST_INDIO_DEV_NUM; n++) {
		err = iio_trigger_register(cdata->trig[n]);
		if (err < 0) {
			dev_err(cdata->dev,
					"failed to register iio trigger.\n");
			goto free_irq;
		}
		cdata->indio_dev[n]->trig = cdata->trig[n];
	}

	return 0;

free_irq:
	free_irq(cdata->irq, cdata);
	for (n--; n >= 0; n--)
		iio_trigger_unregister(cdata->trig[n]);
deallocate_trigger:
	for (i--; i >= 0; i--)
		iio_trigger_free(cdata->trig[i]);

	return err;
}
EXPORT_SYMBOL(st_lsm6ds3_allocate_triggers);

void st_lsm6ds3_deallocate_triggers(struct lsm6ds3_data *cdata)
{
	int i;

	free_irq(cdata->irq, cdata);

	for (i = 0; i < ST_INDIO_DEV_NUM; i++)
		iio_trigger_unregister(cdata->trig[i]);
}
EXPORT_SYMBOL(st_lsm6ds3_deallocate_triggers);
