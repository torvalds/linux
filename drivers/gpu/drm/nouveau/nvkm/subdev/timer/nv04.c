/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "nv04.h"

static u64
nv04_timer_read(struct nvkm_timer *tmr)
{
	struct nvkm_device *device = tmr->subdev.device;
	u32 hi, lo;

	do {
		hi = nvkm_rd32(device, NV04_PTIMER_TIME_1);
		lo = nvkm_rd32(device, NV04_PTIMER_TIME_0);
	} while (hi != nvkm_rd32(device, NV04_PTIMER_TIME_1));

	return ((u64)hi << 32 | lo);
}

static void
nv04_timer_alarm_trigger(struct nvkm_timer *obj)
{
	struct nv04_timer *tmr = container_of(obj, typeof(*tmr), base);
	struct nvkm_device *device = tmr->base.subdev.device;
	struct nvkm_alarm *alarm, *atemp;
	unsigned long flags;
	LIST_HEAD(exec);

	/* move any due alarms off the pending list */
	spin_lock_irqsave(&tmr->lock, flags);
	list_for_each_entry_safe(alarm, atemp, &tmr->alarms, head) {
		if (alarm->timestamp <= tmr->base.read(&tmr->base))
			list_move_tail(&alarm->head, &exec);
	}

	/* reschedule interrupt for next alarm time */
	if (!list_empty(&tmr->alarms)) {
		alarm = list_first_entry(&tmr->alarms, typeof(*alarm), head);
		nvkm_wr32(device, NV04_PTIMER_ALARM_0, alarm->timestamp);
		nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000001);
	} else {
		nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000000);
	}
	spin_unlock_irqrestore(&tmr->lock, flags);

	/* execute any pending alarm handlers */
	list_for_each_entry_safe(alarm, atemp, &exec, head) {
		list_del_init(&alarm->head);
		alarm->func(alarm);
	}
}

static void
nv04_timer_alarm(struct nvkm_timer *obj, u64 time, struct nvkm_alarm *alarm)
{
	struct nv04_timer *tmr = container_of(obj, typeof(*tmr), base);
	struct nvkm_alarm *list;
	unsigned long flags;

	alarm->timestamp = tmr->base.read(&tmr->base) + time;

	/* append new alarm to list, in soonest-alarm-first order */
	spin_lock_irqsave(&tmr->lock, flags);
	if (!time) {
		if (!list_empty(&alarm->head))
			list_del(&alarm->head);
	} else {
		list_for_each_entry(list, &tmr->alarms, head) {
			if (list->timestamp > alarm->timestamp)
				break;
		}
		list_add_tail(&alarm->head, &list->head);
	}
	spin_unlock_irqrestore(&tmr->lock, flags);

	/* process pending alarms */
	nv04_timer_alarm_trigger(&tmr->base);
}

static void
nv04_timer_alarm_cancel(struct nvkm_timer *obj, struct nvkm_alarm *alarm)
{
	struct nv04_timer *tmr = container_of(obj, typeof(*tmr), base);
	unsigned long flags;
	spin_lock_irqsave(&tmr->lock, flags);
	list_del_init(&alarm->head);
	spin_unlock_irqrestore(&tmr->lock, flags);
}

static void
nv04_timer_intr(struct nvkm_subdev *subdev)
{
	struct nv04_timer *tmr = (void *)subdev;
	struct nvkm_device *device = tmr->base.subdev.device;
	u32 stat = nvkm_rd32(device, NV04_PTIMER_INTR_0);

	if (stat & 0x00000001) {
		nv04_timer_alarm_trigger(&tmr->base);
		nvkm_wr32(device, NV04_PTIMER_INTR_0, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_wr32(device, NV04_PTIMER_INTR_0, stat);
	}
}

int
nv04_timer_fini(struct nvkm_object *object, bool suspend)
{
	struct nv04_timer *tmr = (void *)object;
	struct nvkm_device *device = tmr->base.subdev.device;
	if (suspend)
		tmr->suspend_time = nv04_timer_read(&tmr->base);
	nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000000);
	return nvkm_timer_fini(&tmr->base, suspend);
}

static int
nv04_timer_init(struct nvkm_object *object)
{
	struct nv04_timer *tmr = (void *)object;
	struct nvkm_subdev *subdev = &tmr->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 m = 1, f, n, d, lo, hi;
	int ret;

	ret = nvkm_timer_init(&tmr->base);
	if (ret)
		return ret;

	/* aim for 31.25MHz, which gives us nanosecond timestamps */
	d = 1000000 / 32;

	/* determine base clock for timer source */
#if 0 /*XXX*/
	if (device->chipset < 0x40) {
		n = nvkm_hw_get_clock(device, PLL_CORE);
	} else
#endif
	if (device->chipset <= 0x40) {
		/*XXX: figure this out */
		f = -1;
		n = 0;
	} else {
		f = device->crystal;
		n = f;
		while (n < (d * 2)) {
			n += (n / m);
			m++;
		}

		nvkm_wr32(device, 0x009220, m - 1);
	}

	if (!n) {
		nvkm_warn(subdev, "unknown input clock freq\n");
		if (!nvkm_rd32(device, NV04_PTIMER_NUMERATOR) ||
		    !nvkm_rd32(device, NV04_PTIMER_DENOMINATOR)) {
			nvkm_wr32(device, NV04_PTIMER_NUMERATOR, 1);
			nvkm_wr32(device, NV04_PTIMER_DENOMINATOR, 1);
		}
		return 0;
	}

	/* reduce ratio to acceptable values */
	while (((n % 5) == 0) && ((d % 5) == 0)) {
		n /= 5;
		d /= 5;
	}

	while (((n % 2) == 0) && ((d % 2) == 0)) {
		n /= 2;
		d /= 2;
	}

	while (n > 0xffff || d > 0xffff) {
		n >>= 1;
		d >>= 1;
	}

	/* restore the time before suspend */
	lo = tmr->suspend_time;
	hi = (tmr->suspend_time >> 32);

	nvkm_debug(subdev, "input frequency : %dHz\n", f);
	nvkm_debug(subdev, "input multiplier: %d\n", m);
	nvkm_debug(subdev, "numerator       : %08x\n", n);
	nvkm_debug(subdev, "denominator     : %08x\n", d);
	nvkm_debug(subdev, "timer frequency : %dHz\n", (f * m) * d / n);
	nvkm_debug(subdev, "time low        : %08x\n", lo);
	nvkm_debug(subdev, "time high       : %08x\n", hi);

	nvkm_wr32(device, NV04_PTIMER_NUMERATOR, n);
	nvkm_wr32(device, NV04_PTIMER_DENOMINATOR, d);
	nvkm_wr32(device, NV04_PTIMER_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000000);
	nvkm_wr32(device, NV04_PTIMER_TIME_1, hi);
	nvkm_wr32(device, NV04_PTIMER_TIME_0, lo);
	return 0;
}

void
nv04_timer_dtor(struct nvkm_object *object)
{
	struct nv04_timer *tmr = (void *)object;
	return nvkm_timer_destroy(&tmr->base);
}

int
nv04_timer_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nv04_timer *tmr;
	int ret;

	ret = nvkm_timer_create(parent, engine, oclass, &tmr);
	*pobject = nv_object(tmr);
	if (ret)
		return ret;

	tmr->base.subdev.intr = nv04_timer_intr;
	tmr->base.read = nv04_timer_read;
	tmr->base.alarm = nv04_timer_alarm;
	tmr->base.alarm_cancel = nv04_timer_alarm_cancel;
	tmr->suspend_time = 0;

	INIT_LIST_HEAD(&tmr->alarms);
	spin_lock_init(&tmr->lock);
	return 0;
}

struct nvkm_oclass
nv04_timer_oclass = {
	.handle = NV_SUBDEV(TIMER, 0x04),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_timer_ctor,
		.dtor = nv04_timer_dtor,
		.init = nv04_timer_init,
		.fini = nv04_timer_fini,
	}
};
