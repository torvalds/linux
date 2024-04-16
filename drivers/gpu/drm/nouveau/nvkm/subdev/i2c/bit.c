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
 * all copies or substantial busions of the Software.
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
#include "bus.h"

#ifdef CONFIG_NOUVEAU_I2C_INTERNAL
#define T_TIMEOUT  2200000
#define T_RISEFALL 1000
#define T_HOLD     5000

static inline void
nvkm_i2c_drive_scl(struct nvkm_i2c_bus *bus, int state)
{
	bus->func->drive_scl(bus, state);
}

static inline void
nvkm_i2c_drive_sda(struct nvkm_i2c_bus *bus, int state)
{
	bus->func->drive_sda(bus, state);
}

static inline int
nvkm_i2c_sense_scl(struct nvkm_i2c_bus *bus)
{
	return bus->func->sense_scl(bus);
}

static inline int
nvkm_i2c_sense_sda(struct nvkm_i2c_bus *bus)
{
	return bus->func->sense_sda(bus);
}

static void
nvkm_i2c_delay(struct nvkm_i2c_bus *bus, u32 nsec)
{
	udelay((nsec + 500) / 1000);
}

static bool
nvkm_i2c_raise_scl(struct nvkm_i2c_bus *bus)
{
	u32 timeout = T_TIMEOUT / T_RISEFALL;

	nvkm_i2c_drive_scl(bus, 1);
	do {
		nvkm_i2c_delay(bus, T_RISEFALL);
	} while (!nvkm_i2c_sense_scl(bus) && --timeout);

	return timeout != 0;
}

static int
i2c_start(struct nvkm_i2c_bus *bus)
{
	int ret = 0;

	if (!nvkm_i2c_sense_scl(bus) ||
	    !nvkm_i2c_sense_sda(bus)) {
		nvkm_i2c_drive_scl(bus, 0);
		nvkm_i2c_drive_sda(bus, 1);
		if (!nvkm_i2c_raise_scl(bus))
			ret = -EBUSY;
	}

	nvkm_i2c_drive_sda(bus, 0);
	nvkm_i2c_delay(bus, T_HOLD);
	nvkm_i2c_drive_scl(bus, 0);
	nvkm_i2c_delay(bus, T_HOLD);
	return ret;
}

static void
i2c_stop(struct nvkm_i2c_bus *bus)
{
	nvkm_i2c_drive_scl(bus, 0);
	nvkm_i2c_drive_sda(bus, 0);
	nvkm_i2c_delay(bus, T_RISEFALL);

	nvkm_i2c_drive_scl(bus, 1);
	nvkm_i2c_delay(bus, T_HOLD);
	nvkm_i2c_drive_sda(bus, 1);
	nvkm_i2c_delay(bus, T_HOLD);
}

static int
i2c_bitw(struct nvkm_i2c_bus *bus, int sda)
{
	nvkm_i2c_drive_sda(bus, sda);
	nvkm_i2c_delay(bus, T_RISEFALL);

	if (!nvkm_i2c_raise_scl(bus))
		return -ETIMEDOUT;
	nvkm_i2c_delay(bus, T_HOLD);

	nvkm_i2c_drive_scl(bus, 0);
	nvkm_i2c_delay(bus, T_HOLD);
	return 0;
}

static int
i2c_bitr(struct nvkm_i2c_bus *bus)
{
	int sda;

	nvkm_i2c_drive_sda(bus, 1);
	nvkm_i2c_delay(bus, T_RISEFALL);

	if (!nvkm_i2c_raise_scl(bus))
		return -ETIMEDOUT;
	nvkm_i2c_delay(bus, T_HOLD);

	sda = nvkm_i2c_sense_sda(bus);

	nvkm_i2c_drive_scl(bus, 0);
	nvkm_i2c_delay(bus, T_HOLD);
	return sda;
}

static int
nvkm_i2c_get_byte(struct nvkm_i2c_bus *bus, u8 *byte, bool last)
{
	int i, bit;

	*byte = 0;
	for (i = 7; i >= 0; i--) {
		bit = i2c_bitr(bus);
		if (bit < 0)
			return bit;
		*byte |= bit << i;
	}

	return i2c_bitw(bus, last ? 1 : 0);
}

static int
nvkm_i2c_put_byte(struct nvkm_i2c_bus *bus, u8 byte)
{
	int i, ret;
	for (i = 7; i >= 0; i--) {
		ret = i2c_bitw(bus, !!(byte & (1 << i)));
		if (ret < 0)
			return ret;
	}

	ret = i2c_bitr(bus);
	if (ret == 1) /* nack */
		ret = -EIO;
	return ret;
}

static int
i2c_addr(struct nvkm_i2c_bus *bus, struct i2c_msg *msg)
{
	u32 addr = msg->addr << 1;
	if (msg->flags & I2C_M_RD)
		addr |= 1;
	return nvkm_i2c_put_byte(bus, addr);
}

int
nvkm_i2c_bit_xfer(struct nvkm_i2c_bus *bus, struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msg = msgs;
	int ret = 0, mcnt = num;

	while (!ret && mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		ret = i2c_start(bus);
		if (ret == 0)
			ret = i2c_addr(bus, msg);

		if (msg->flags & I2C_M_RD) {
			while (!ret && remaining--)
				ret = nvkm_i2c_get_byte(bus, ptr++, !remaining);
		} else {
			while (!ret && remaining--)
				ret = nvkm_i2c_put_byte(bus, *ptr++);
		}

		msg++;
	}

	i2c_stop(bus);
	return (ret < 0) ? ret : num;
}
#else
int
nvkm_i2c_bit_xfer(struct nvkm_i2c_bus *bus, struct i2c_msg *msgs, int num)
{
	return -ENODEV;
}
#endif
