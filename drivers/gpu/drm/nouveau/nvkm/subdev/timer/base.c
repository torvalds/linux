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
#include <subdev/timer.h>

bool
nvkm_timer_wait_eq(void *obj, u64 nsec, u32 addr, u32 mask, u32 data)
{
	struct nvkm_timer *ptimer = nvkm_timer(obj);
	u64 time0;

	time0 = ptimer->read(ptimer);
	do {
		if (nv_iclass(obj, NV_SUBDEV_CLASS)) {
			if ((nv_rd32(obj, addr) & mask) == data)
				return true;
		} else {
			if ((nv_ro32(obj, addr) & mask) == data)
				return true;
		}
	} while (ptimer->read(ptimer) - time0 < nsec);

	return false;
}

bool
nvkm_timer_wait_ne(void *obj, u64 nsec, u32 addr, u32 mask, u32 data)
{
	struct nvkm_timer *ptimer = nvkm_timer(obj);
	u64 time0;

	time0 = ptimer->read(ptimer);
	do {
		if (nv_iclass(obj, NV_SUBDEV_CLASS)) {
			if ((nv_rd32(obj, addr) & mask) != data)
				return true;
		} else {
			if ((nv_ro32(obj, addr) & mask) != data)
				return true;
		}
	} while (ptimer->read(ptimer) - time0 < nsec);

	return false;
}

bool
nvkm_timer_wait_cb(void *obj, u64 nsec, bool (*func)(void *), void *data)
{
	struct nvkm_timer *ptimer = nvkm_timer(obj);
	u64 time0;

	time0 = ptimer->read(ptimer);
	do {
		if (func(data) == true)
			return true;
	} while (ptimer->read(ptimer) - time0 < nsec);

	return false;
}

void
nvkm_timer_alarm(void *obj, u32 nsec, struct nvkm_alarm *alarm)
{
	struct nvkm_timer *ptimer = nvkm_timer(obj);
	ptimer->alarm(ptimer, nsec, alarm);
}

void
nvkm_timer_alarm_cancel(void *obj, struct nvkm_alarm *alarm)
{
	struct nvkm_timer *ptimer = nvkm_timer(obj);
	ptimer->alarm_cancel(ptimer, alarm);
}
