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
#define nv04_i2c_bus(p) container_of((p), struct nv04_i2c_bus, base)
#include "bus.h"

#include <subdev/vga.h>

struct nv04_i2c_bus {
	struct nvkm_i2c_bus base;
	u8 drive;
	u8 sense;
};

static void
nv04_i2c_bus_drive_scl(struct nvkm_i2c_bus *base, int state)
{
	struct nv04_i2c_bus *bus = nv04_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	u8 val = nvkm_rdvgac(device, 0, bus->drive);
	if (state) val |= 0x20;
	else	   val &= 0xdf;
	nvkm_wrvgac(device, 0, bus->drive, val | 0x01);
}

static void
nv04_i2c_bus_drive_sda(struct nvkm_i2c_bus *base, int state)
{
	struct nv04_i2c_bus *bus = nv04_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	u8 val = nvkm_rdvgac(device, 0, bus->drive);
	if (state) val |= 0x10;
	else	   val &= 0xef;
	nvkm_wrvgac(device, 0, bus->drive, val | 0x01);
}

static int
nv04_i2c_bus_sense_scl(struct nvkm_i2c_bus *base)
{
	struct nv04_i2c_bus *bus = nv04_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	return !!(nvkm_rdvgac(device, 0, bus->sense) & 0x04);
}

static int
nv04_i2c_bus_sense_sda(struct nvkm_i2c_bus *base)
{
	struct nv04_i2c_bus *bus = nv04_i2c_bus(base);
	struct nvkm_device *device = bus->base.pad->i2c->subdev.device;
	return !!(nvkm_rdvgac(device, 0, bus->sense) & 0x08);
}

static const struct nvkm_i2c_bus_func
nv04_i2c_bus_func = {
	.drive_scl = nv04_i2c_bus_drive_scl,
	.drive_sda = nv04_i2c_bus_drive_sda,
	.sense_scl = nv04_i2c_bus_sense_scl,
	.sense_sda = nv04_i2c_bus_sense_sda,
	.xfer = nvkm_i2c_bit_xfer,
};

int
nv04_i2c_bus_new(struct nvkm_i2c_pad *pad, int id, u8 drive, u8 sense,
		 struct nvkm_i2c_bus **pbus)
{
	struct nv04_i2c_bus *bus;

	if (!(bus = kzalloc(sizeof(*bus), GFP_KERNEL)))
		return -ENOMEM;
	*pbus = &bus->base;

	nvkm_i2c_bus_ctor(&nv04_i2c_bus_func, pad, id, &bus->base);
	bus->drive = drive;
	bus->sense = sense;
	return 0;
}
