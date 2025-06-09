// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "adf_admin.h"
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_timer.h"

#define ADF_DEFAULT_TIMER_PERIOD_MS 200

/* This periodic update is used to trigger HB, RL & TL fw events */
static void work_handler(struct work_struct *work)
{
	struct adf_accel_dev *accel_dev;
	struct adf_timer *timer_ctx;
	u32 time_periods;

	timer_ctx = container_of(to_delayed_work(work), struct adf_timer, work_ctx);
	accel_dev = timer_ctx->accel_dev;

	adf_misc_wq_queue_delayed_work(&timer_ctx->work_ctx,
				       msecs_to_jiffies(ADF_DEFAULT_TIMER_PERIOD_MS));

	time_periods = div_u64(ktime_ms_delta(ktime_get_real(), timer_ctx->initial_ktime),
			       ADF_DEFAULT_TIMER_PERIOD_MS);

	if (adf_send_admin_tim_sync(accel_dev, time_periods))
		dev_err(&GET_DEV(accel_dev), "Failed to synchronize qat timer\n");
}

int adf_timer_start(struct adf_accel_dev *accel_dev)
{
	struct adf_timer *timer_ctx;

	timer_ctx = kzalloc(sizeof(*timer_ctx), GFP_KERNEL);
	if (!timer_ctx)
		return -ENOMEM;

	timer_ctx->accel_dev = accel_dev;
	accel_dev->timer = timer_ctx;
	timer_ctx->initial_ktime = ktime_get_real();

	INIT_DELAYED_WORK(&timer_ctx->work_ctx, work_handler);
	adf_misc_wq_queue_delayed_work(&timer_ctx->work_ctx,
				       msecs_to_jiffies(ADF_DEFAULT_TIMER_PERIOD_MS));

	return 0;
}
EXPORT_SYMBOL_GPL(adf_timer_start);

void adf_timer_stop(struct adf_accel_dev *accel_dev)
{
	struct adf_timer *timer_ctx = accel_dev->timer;

	if (!timer_ctx)
		return;

	cancel_delayed_work_sync(&timer_ctx->work_ctx);

	kfree(timer_ctx);
	accel_dev->timer = NULL;
}
EXPORT_SYMBOL_GPL(adf_timer_stop);
