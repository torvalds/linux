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
#include "priv.h"
#include "regsnv04.h"

void
nv04_timer_time(struct nvkm_timer *tmr, u64 time)
{
	struct nvkm_subdev *subdev = &tmr->subdev;
	struct nvkm_device *device = subdev->device;
	u32 hi = upper_32_bits(time);
	u32 lo = lower_32_bits(time);

	nvkm_debug(subdev, "time low        : %08x\n", lo);
	nvkm_debug(subdev, "time high       : %08x\n", hi);

	nvkm_wr32(device, NV04_PTIMER_TIME_1, hi);
	nvkm_wr32(device, NV04_PTIMER_TIME_0, lo);
}

u64
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

void
nv04_timer_alarm_fini(struct nvkm_timer *tmr)
{
	struct nvkm_device *device = tmr->subdev.device;
	nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000000);
}

void
nv04_timer_alarm_init(struct nvkm_timer *tmr, u32 time)
{
	struct nvkm_device *device = tmr->subdev.device;
	nvkm_wr32(device, NV04_PTIMER_ALARM_0, time);
	nvkm_wr32(device, NV04_PTIMER_INTR_EN_0, 0x00000001);
}

void
nv04_timer_intr(struct nvkm_timer *tmr)
{
	struct nvkm_subdev *subdev = &tmr->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, NV04_PTIMER_INTR_0);

	if (stat & 0x00000001) {
		nvkm_timer_alarm_trigger(tmr);
		nvkm_wr32(device, NV04_PTIMER_INTR_0, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_wr32(device, NV04_PTIMER_INTR_0, stat);
	}
}

static void
nv04_timer_init(struct nvkm_timer *tmr)
{
	struct nvkm_subdev *subdev = &tmr->subdev;
	struct nvkm_device *device = subdev->device;
	u32 f = 0; /*XXX: nvclk */
	u32 n, d;

	/* aim for 31.25MHz, which gives us nanosecond timestamps */
	d = 1000000 / 32;
	n = f;

	if (!f) {
		n = nvkm_rd32(device, NV04_PTIMER_NUMERATOR);
		d = nvkm_rd32(device, NV04_PTIMER_DENOMINATOR);
		if (!n || !d) {
			n = 1;
			d = 1;
		}
		nvkm_warn(subdev, "unknown input clock freq\n");
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

	nvkm_debug(subdev, "input frequency : %dHz\n", f);
	nvkm_debug(subdev, "numerator       : %08x\n", n);
	nvkm_debug(subdev, "denominator     : %08x\n", d);
	nvkm_debug(subdev, "timer frequency : %dHz\n", f * d / n);

	nvkm_wr32(device, NV04_PTIMER_NUMERATOR, n);
	nvkm_wr32(device, NV04_PTIMER_DENOMINATOR, d);
}

static const struct nvkm_timer_func
nv04_timer = {
	.init = nv04_timer_init,
	.intr = nv04_timer_intr,
	.read = nv04_timer_read,
	.time = nv04_timer_time,
	.alarm_init = nv04_timer_alarm_init,
	.alarm_fini = nv04_timer_alarm_fini,
};

int
nv04_timer_new(struct nvkm_device *device, int index, struct nvkm_timer **ptmr)
{
	return nvkm_timer_new_(&nv04_timer, device, index, ptmr);
}
