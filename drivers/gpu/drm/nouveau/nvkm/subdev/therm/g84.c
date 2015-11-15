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
 * 	    Martin Peres
 */
#include "priv.h"

#include <subdev/fuse.h>

int
g84_temp_get(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;

	if (nvkm_fuse_read(device->fuse, 0x1a8) == 1)
		return nvkm_rd32(device, 0x20400);
	else
		return -ENODEV;
}

void
g84_sensor_setup(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;

	/* enable temperature reading for cards with insane defaults */
	if (nvkm_fuse_read(device->fuse, 0x1a8) == 1) {
		nvkm_mask(device, 0x20008, 0x80008000, 0x80000000);
		nvkm_mask(device, 0x2000c, 0x80000003, 0x00000000);
		mdelay(20); /* wait for the temperature to stabilize */
	}
}

static void
g84_therm_program_alarms(struct nvkm_therm *therm)
{
	struct nvbios_therm_sensor *sensor = &therm->bios_sensor;
	struct nvkm_subdev *subdev = &therm->subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long flags;

	spin_lock_irqsave(&therm->sensor.alarm_program_lock, flags);

	/* enable RISING and FALLING IRQs for shutdown, THRS 0, 1, 2 and 4 */
	nvkm_wr32(device, 0x20000, 0x000003ff);

	/* shutdown: The computer should be shutdown when reached */
	nvkm_wr32(device, 0x20484, sensor->thrs_shutdown.hysteresis);
	nvkm_wr32(device, 0x20480, sensor->thrs_shutdown.temp);

	/* THRS_1 : fan boost*/
	nvkm_wr32(device, 0x204c4, sensor->thrs_fan_boost.temp);

	/* THRS_2 : critical */
	nvkm_wr32(device, 0x204c0, sensor->thrs_critical.temp);

	/* THRS_4 : down clock */
	nvkm_wr32(device, 0x20414, sensor->thrs_down_clock.temp);
	spin_unlock_irqrestore(&therm->sensor.alarm_program_lock, flags);

	nvkm_debug(subdev,
		   "Programmed thresholds [ %d(%d), %d(%d), %d(%d), %d(%d) ]\n",
		   sensor->thrs_fan_boost.temp,
		   sensor->thrs_fan_boost.hysteresis,
		   sensor->thrs_down_clock.temp,
		   sensor->thrs_down_clock.hysteresis,
		   sensor->thrs_critical.temp,
		   sensor->thrs_critical.hysteresis,
		   sensor->thrs_shutdown.temp,
		   sensor->thrs_shutdown.hysteresis);

}

/* must be called with alarm_program_lock taken ! */
static void
g84_therm_threshold_hyst_emulation(struct nvkm_therm *therm,
				   uint32_t thrs_reg, u8 status_bit,
				   const struct nvbios_therm_threshold *thrs,
				   enum nvkm_therm_thrs thrs_name)
{
	struct nvkm_device *device = therm->subdev.device;
	enum nvkm_therm_thrs_direction direction;
	enum nvkm_therm_thrs_state prev_state, new_state;
	int temp, cur;

	prev_state = nvkm_therm_sensor_get_threshold_state(therm, thrs_name);
	temp = nvkm_rd32(device, thrs_reg);

	/* program the next threshold */
	if (temp == thrs->temp) {
		nvkm_wr32(device, thrs_reg, thrs->temp - thrs->hysteresis);
		new_state = NVKM_THERM_THRS_HIGHER;
	} else {
		nvkm_wr32(device, thrs_reg, thrs->temp);
		new_state = NVKM_THERM_THRS_LOWER;
	}

	/* fix the state (in case someone reprogrammed the alarms) */
	cur = therm->func->temp_get(therm);
	if (new_state == NVKM_THERM_THRS_LOWER && cur > thrs->temp)
		new_state = NVKM_THERM_THRS_HIGHER;
	else if (new_state == NVKM_THERM_THRS_HIGHER &&
		cur < thrs->temp - thrs->hysteresis)
		new_state = NVKM_THERM_THRS_LOWER;
	nvkm_therm_sensor_set_threshold_state(therm, thrs_name, new_state);

	/* find the direction */
	if (prev_state < new_state)
		direction = NVKM_THERM_THRS_RISING;
	else if (prev_state > new_state)
		direction = NVKM_THERM_THRS_FALLING;
	else
		return;

	/* advertise a change in direction */
	nvkm_therm_sensor_event(therm, thrs_name, direction);
}

static void
g84_therm_intr(struct nvkm_therm *therm)
{
	struct nvkm_subdev *subdev = &therm->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvbios_therm_sensor *sensor = &therm->bios_sensor;
	unsigned long flags;
	uint32_t intr;

	spin_lock_irqsave(&therm->sensor.alarm_program_lock, flags);

	intr = nvkm_rd32(device, 0x20100) & 0x3ff;

	/* THRS_4: downclock */
	if (intr & 0x002) {
		g84_therm_threshold_hyst_emulation(therm, 0x20414, 24,
						   &sensor->thrs_down_clock,
						   NVKM_THERM_THRS_DOWNCLOCK);
		intr &= ~0x002;
	}

	/* shutdown */
	if (intr & 0x004) {
		g84_therm_threshold_hyst_emulation(therm, 0x20480, 20,
						   &sensor->thrs_shutdown,
						   NVKM_THERM_THRS_SHUTDOWN);
		intr &= ~0x004;
	}

	/* THRS_1 : fan boost */
	if (intr & 0x008) {
		g84_therm_threshold_hyst_emulation(therm, 0x204c4, 21,
						   &sensor->thrs_fan_boost,
						   NVKM_THERM_THRS_FANBOOST);
		intr &= ~0x008;
	}

	/* THRS_2 : critical */
	if (intr & 0x010) {
		g84_therm_threshold_hyst_emulation(therm, 0x204c0, 22,
						   &sensor->thrs_critical,
						   NVKM_THERM_THRS_CRITICAL);
		intr &= ~0x010;
	}

	if (intr)
		nvkm_error(subdev, "intr %08x\n", intr);

	/* ACK everything */
	nvkm_wr32(device, 0x20100, 0xffffffff);
	nvkm_wr32(device, 0x1100, 0x10000); /* PBUS */

	spin_unlock_irqrestore(&therm->sensor.alarm_program_lock, flags);
}

void
g84_therm_fini(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;

	/* Disable PTherm IRQs */
	nvkm_wr32(device, 0x20000, 0x00000000);

	/* ACK all PTherm IRQs */
	nvkm_wr32(device, 0x20100, 0xffffffff);
	nvkm_wr32(device, 0x1100, 0x10000); /* PBUS */
}

static void
g84_therm_init(struct nvkm_therm *therm)
{
	g84_sensor_setup(therm);
}

static const struct nvkm_therm_func
g84_therm = {
	.init = g84_therm_init,
	.fini = g84_therm_fini,
	.intr = g84_therm_intr,
	.pwm_ctrl = nv50_fan_pwm_ctrl,
	.pwm_get = nv50_fan_pwm_get,
	.pwm_set = nv50_fan_pwm_set,
	.pwm_clock = nv50_fan_pwm_clock,
	.temp_get = g84_temp_get,
	.program_alarms = g84_therm_program_alarms,
};

int
g84_therm_new(struct nvkm_device *device, int index, struct nvkm_therm **ptherm)
{
	struct nvkm_therm *therm;
	int ret;

	ret = nvkm_therm_new_(&g84_therm, device, index, &therm);
	*ptherm = therm;
	if (ret)
		return ret;

	/* init the thresholds */
	nvkm_therm_sensor_set_threshold_state(therm, NVKM_THERM_THRS_SHUTDOWN,
						     NVKM_THERM_THRS_LOWER);
	nvkm_therm_sensor_set_threshold_state(therm, NVKM_THERM_THRS_FANBOOST,
						     NVKM_THERM_THRS_LOWER);
	nvkm_therm_sensor_set_threshold_state(therm, NVKM_THERM_THRS_CRITICAL,
						     NVKM_THERM_THRS_LOWER);
	nvkm_therm_sensor_set_threshold_state(therm, NVKM_THERM_THRS_DOWNCLOCK,
						     NVKM_THERM_THRS_LOWER);
	return 0;
}
