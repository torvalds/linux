/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#define gf119_i2c_bus(p) container_of((p), struct gf119_i2c_bus, base)
#include "bus.h"

struct gf119_i2c_bus {
	struct nvkm_i2c_bus base;
	u32 addr;
};

static void
gf119_i2c_bus_drive_scl(struct nvkm_i2c_bus *base, int state)
{
	struct gf119_i2c_bus *bus = gf119_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	nvkm_mask(device, bus->addr, 0x00000001, state ? 0x00000001 : 0);
}

static void
gf119_i2c_bus_drive_sda(struct nvkm_i2c_bus *base, int state)
{
	struct gf119_i2c_bus *bus = gf119_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	nvkm_mask(device, bus->addr, 0x00000002, state ? 0x00000002 : 0);
}

static int
gf119_i2c_bus_sense_scl(struct nvkm_i2c_bus *base)
{
	struct gf119_i2c_bus *bus = gf119_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	return !!(nvkm_rd32(device, bus->addr) & 0x00000010);
}

static int
gf119_i2c_bus_sense_sda(struct nvkm_i2c_bus *base)
{
	struct gf119_i2c_bus *bus = gf119_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	return !!(nvkm_rd32(device, bus->addr) & 0x00000020);
}

static void
gf119_i2c_bus_init(struct nvkm_i2c_bus *base)
{
	struct gf119_i2c_bus *bus = gf119_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	nvkm_wr32(device, bus->addr, 0x00000007);
}

static const struct nvkm_i2c_bus_func
gf119_i2c_bus_func = {
	.init = gf119_i2c_bus_init,
	.drive_scl = gf119_i2c_bus_drive_scl,
	.drive_sda = gf119_i2c_bus_drive_sda,
	.sense_scl = gf119_i2c_bus_sense_scl,
	.sense_sda = gf119_i2c_bus_sense_sda,
	.xfer = nvkm_i2c_bit_xfer,
};

int
gf119_i2c_bus_new(struct nvkm_i2c_pad *pad, int id, u8 drive,
		 struct nvkm_i2c_bus **pbus)
{
	struct gf119_i2c_bus *bus;

	if (!(bus = kzalloc(sizeof(*bus), GFP_KERNEL)))
		return -ENOMEM;
	*pbus = &bus->base;

	nvkm_i2c_bus_ctor(&gf119_i2c_bus_func, pad, id, &bus->base);
	bus->addr = 0x00d014 + (drive * 0x20);
	return 0;
}
