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
#include <subdev/vga.h>

u8
nvkm_rdport(struct nvkm_device *device, int head, u16 port)
{
	if (device->card_type >= NV_50)
		return nvkm_rd08(device, 0x601000 + port);

	if (port == 0x03c0 || port == 0x03c1 ||	/* AR */
	    port == 0x03c2 || port == 0x03da ||	/* INP0 */
	    port == 0x03d4 || port == 0x03d5)	/* CR */
		return nvkm_rd08(device, 0x601000 + (head * 0x2000) + port);

	if (port == 0x03c2 || port == 0x03cc ||	/* MISC */
	    port == 0x03c4 || port == 0x03c5 ||	/* SR */
	    port == 0x03ce || port == 0x03cf) {	/* GR */
		if (device->card_type < NV_40)
			head = 0; /* CR44 selects head */
		return nvkm_rd08(device, 0x0c0000 + (head * 0x2000) + port);
	}

	return 0x00;
}

void
nvkm_wrport(struct nvkm_device *device, int head, u16 port, u8 data)
{
	if (device->card_type >= NV_50)
		nvkm_wr08(device, 0x601000 + port, data);
	else
	if (port == 0x03c0 || port == 0x03c1 ||	/* AR */
	    port == 0x03c2 || port == 0x03da ||	/* INP0 */
	    port == 0x03d4 || port == 0x03d5)	/* CR */
		nvkm_wr08(device, 0x601000 + (head * 0x2000) + port, data);
	else
	if (port == 0x03c2 || port == 0x03cc ||	/* MISC */
	    port == 0x03c4 || port == 0x03c5 ||	/* SR */
	    port == 0x03ce || port == 0x03cf) {	/* GR */
		if (device->card_type < NV_40)
			head = 0; /* CR44 selects head */
		nvkm_wr08(device, 0x0c0000 + (head * 0x2000) + port, data);
	}
}

u8
nvkm_rdvgas(struct nvkm_device *device, int head, u8 index)
{
	nvkm_wrport(device, head, 0x03c4, index);
	return nvkm_rdport(device, head, 0x03c5);
}

void
nvkm_wrvgas(struct nvkm_device *device, int head, u8 index, u8 value)
{
	nvkm_wrport(device, head, 0x03c4, index);
	nvkm_wrport(device, head, 0x03c5, value);
}

u8
nvkm_rdvgag(struct nvkm_device *device, int head, u8 index)
{
	nvkm_wrport(device, head, 0x03ce, index);
	return nvkm_rdport(device, head, 0x03cf);
}

void
nvkm_wrvgag(struct nvkm_device *device, int head, u8 index, u8 value)
{
	nvkm_wrport(device, head, 0x03ce, index);
	nvkm_wrport(device, head, 0x03cf, value);
}

u8
nvkm_rdvgac(struct nvkm_device *device, int head, u8 index)
{
	nvkm_wrport(device, head, 0x03d4, index);
	return nvkm_rdport(device, head, 0x03d5);
}

void
nvkm_wrvgac(struct nvkm_device *device, int head, u8 index, u8 value)
{
	nvkm_wrport(device, head, 0x03d4, index);
	nvkm_wrport(device, head, 0x03d5, value);
}

u8
nvkm_rdvgai(struct nvkm_device *device, int head, u16 port, u8 index)
{
	if (port == 0x03c4) return nvkm_rdvgas(device, head, index);
	if (port == 0x03ce) return nvkm_rdvgag(device, head, index);
	if (port == 0x03d4) return nvkm_rdvgac(device, head, index);
	return 0x00;
}

void
nvkm_wrvgai(struct nvkm_device *device, int head, u16 port, u8 index, u8 value)
{
	if      (port == 0x03c4) nvkm_wrvgas(device, head, index, value);
	else if (port == 0x03ce) nvkm_wrvgag(device, head, index, value);
	else if (port == 0x03d4) nvkm_wrvgac(device, head, index, value);
}

bool
nvkm_lockvgac(struct nvkm_device *device, bool lock)
{
	bool locked = !nvkm_rdvgac(device, 0, 0x1f);
	u8 data = lock ? 0x99 : 0x57;
	if (device->card_type < NV_50)
		nvkm_wrvgac(device, 0, 0x1f, data);
	else
		nvkm_wrvgac(device, 0, 0x3f, data);
	if (device->chipset == 0x11) {
		if (!(nvkm_rd32(device, 0x001084) & 0x10000000))
			nvkm_wrvgac(device, 1, 0x1f, data);
	}
	return locked;
}

/* CR44 takes values 0 (head A), 3 (head B) and 4 (heads tied)
 * it affects only the 8 bit vga io regs, which we access using mmio at
 * 0xc{0,2}3c*, 0x60{1,3}3*, and 0x68{1,3}3d*
 * in general, the set value of cr44 does not matter: reg access works as
 * expected and values can be set for the appropriate head by using a 0x2000
 * offset as required
 * however:
 * a) pre nv40, the head B range of PRMVIO regs at 0xc23c* was not exposed and
 *    cr44 must be set to 0 or 3 for accessing values on the correct head
 *    through the common 0xc03c* addresses
 * b) in tied mode (4) head B is programmed to the values set on head A, and
 *    access using the head B addresses can have strange results, ergo we leave
 *    tied mode in init once we know to what cr44 should be restored on exit
 *
 * the owner parameter is slightly abused:
 * 0 and 1 are treated as head values and so the set value is (owner * 3)
 * other values are treated as literal values to set
 */
u8
nvkm_rdvgaowner(struct nvkm_device *device)
{
	if (device->card_type < NV_50) {
		if (device->chipset == 0x11) {
			u32 tied = nvkm_rd32(device, 0x001084) & 0x10000000;
			if (tied == 0) {
				u8 slA = nvkm_rdvgac(device, 0, 0x28) & 0x80;
				u8 tvA = nvkm_rdvgac(device, 0, 0x33) & 0x01;
				u8 slB = nvkm_rdvgac(device, 1, 0x28) & 0x80;
				u8 tvB = nvkm_rdvgac(device, 1, 0x33) & 0x01;
				if (slA && !tvA) return 0x00;
				if (slB && !tvB) return 0x03;
				if (slA) return 0x00;
				if (slB) return 0x03;
				return 0x00;
			}
			return 0x04;
		}

		return nvkm_rdvgac(device, 0, 0x44);
	}

	return 0x00;
}

void
nvkm_wrvgaowner(struct nvkm_device *device, u8 select)
{
	if (device->card_type < NV_50) {
		u8 owner = (select == 1) ? 3 : select;
		if (device->chipset == 0x11) {
			/* workaround hw lockup bug */
			nvkm_rdvgac(device, 0, 0x1f);
			nvkm_rdvgac(device, 1, 0x1f);
		}

		nvkm_wrvgac(device, 0, 0x44, owner);

		if (device->chipset == 0x11) {
			nvkm_wrvgac(device, 0, 0x2e, owner);
			nvkm_wrvgac(device, 0, 0x2e, owner);
		}
	}
}
