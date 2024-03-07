// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_asm330lhhx hwtimestamp library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <asm/unaligned.h>
#include <linux/iio/buffer.h>
#include "st_asm330lhhx.h"

#define ST_ASM330LHHX_TSYNC_OFFSET_NS		(300 * 1000LL)

static void st_asm330lhhx_read_hw_timestamp(struct st_asm330lhhx_hw *hw)
{
	s64 timestamp_hw_global;
	s64 eventLSB, eventMSB;
	__le32 timestamp_hw;
	s64 timestamp_cpu;
	__le32 tmp;
	int err;

	err = st_asm330lhhx_read_locked(hw, ST_ASM330LHHX_REG_TIMESTAMP0_ADDR,
				    (u8 *)&timestamp_hw,
				    sizeof(timestamp_hw));
	if (err < 0)
		return;

	timestamp_cpu = st_asm330lhhx_get_time_ns(hw->iio_devs[0]) -
			ST_ASM330LHHX_TSYNC_OFFSET_NS;

	eventLSB = IIO_EVENT_CODE(IIO_COUNT, 0, 0, 0,
				  STM_IIO_EV_TYPE_TIME_SYNC, 0, 0, 0);
	eventMSB = IIO_EVENT_CODE(IIO_COUNT, 0, 0, 1,
				  STM_IIO_EV_TYPE_TIME_SYNC, 0, 0, 0);

	spin_lock_irq(&hw->hwtimestamp_lock);
	timestamp_hw_global = (hw->hw_timestamp_global & GENMASK_ULL(63, 32)) |
			      (u32)le32_to_cpu(timestamp_hw);
	spin_unlock_irq(&hw->hwtimestamp_lock);

	tmp = cpu_to_le32((u32)timestamp_hw_global);
	memcpy(&((int8_t *)&eventLSB)[0], &tmp, sizeof(tmp));

	tmp = cpu_to_le32((u32)(timestamp_hw_global >> 32));
	memcpy(&((int8_t *)&eventMSB)[0], &tmp, sizeof(tmp));

	if (hw->enable_mask & BIT_ULL(ST_ASM330LHHX_ID_GYRO)) {
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_GYRO], eventLSB,
			       timestamp_cpu);
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_GYRO], eventMSB,
			       timestamp_cpu);
	}
	if (hw->enable_mask & BIT_ULL(ST_ASM330LHHX_ID_ACC)) {
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_ACC], eventLSB,
			       timestamp_cpu);
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_ACC], eventMSB,
			       timestamp_cpu);
	}
	if (hw->enable_mask & BIT_ULL(ST_ASM330LHHX_ID_TEMP)) {
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_TEMP], eventLSB,
			       timestamp_cpu);
		iio_push_event(hw->iio_devs[ST_ASM330LHHX_ID_TEMP], eventMSB,
			       timestamp_cpu);
	}

	if (hw->timesync_c < 6)
		hw->timesync_c++;
	else
		hw->timesync_ktime = ktime_set(0, ST_ASM330LHHX_DEFAULT_KTIME);
}

static void st_asm330lhhx_timesync_fn(struct work_struct *work)
{
	struct st_asm330lhhx_hw *hw = container_of(work, struct st_asm330lhhx_hw,
						  timesync_work);

	st_asm330lhhx_read_hw_timestamp(hw);
}

static enum hrtimer_restart st_asm330lhhx_timer_fn(struct hrtimer *timer)
{
	struct st_asm330lhhx_hw *hw;

	hw = container_of(timer, struct st_asm330lhhx_hw, timesync_timer);
	hrtimer_forward(timer, hrtimer_cb_get_time(timer), hw->timesync_ktime);
	queue_work(hw->timesync_workqueue, &hw->timesync_work);

	return HRTIMER_RESTART;
}

int st_asm330lhhx_hwtimesync_init(struct st_asm330lhhx_hw *hw)
{
	hw->timesync_c = 0;
	hw->timesync_ktime = ktime_set(0, ST_ASM330LHHX_DEFAULT_KTIME);
	hrtimer_init(&hw->timesync_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hw->timesync_timer.function = st_asm330lhhx_timer_fn;

	spin_lock_init(&hw->hwtimestamp_lock);
	hw->hw_timestamp_global = 0;

	hw->timesync_workqueue = create_singlethread_workqueue("st_asm330_workqueue");
	if (!hw->timesync_workqueue) {
		return -ENOMEM;
	}

	INIT_WORK(&hw->timesync_work, st_asm330lhhx_timesync_fn);

	return 0;
}
EXPORT_SYMBOL(st_asm330lhhx_hwtimesync_init);
