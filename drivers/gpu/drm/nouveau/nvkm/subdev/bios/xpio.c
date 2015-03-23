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
#include <subdev/bios.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/xpio.h>

static u16
dcb_xpiod_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u16 data = dcb_gpio_table(bios, ver, hdr, cnt, len);
	if (data && *ver >= 0x40 && *hdr >= 0x06) {
		u16 xpio = nv_ro16(bios, data + 0x04);
		if (xpio) {
			*ver = nv_ro08(bios, data + 0x00);
			*hdr = nv_ro08(bios, data + 0x01);
			*cnt = nv_ro08(bios, data + 0x02);
			*len = nv_ro08(bios, data + 0x03);
			return xpio;
		}
	}
	return 0x0000;
}

u16
dcb_xpio_table(struct nvkm_bios *bios, u8 idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u16 data = dcb_xpiod_table(bios, ver, hdr, cnt, len);
	if (data && idx < *cnt) {
		u16 xpio = nv_ro16(bios, data + *hdr + (idx * *len));
		if (xpio) {
			*ver = nv_ro08(bios, data + 0x00);
			*hdr = nv_ro08(bios, data + 0x01);
			*cnt = nv_ro08(bios, data + 0x02);
			*len = nv_ro08(bios, data + 0x03);
			return xpio;
		}
	}
	return 0x0000;
}

u16
dcb_xpio_parse(struct nvkm_bios *bios, u8 idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_xpio *info)
{
	u16 data = dcb_xpio_table(bios, idx, ver, hdr, cnt, len);
	if (data && *len >= 6) {
		info->type = nv_ro08(bios, data + 0x04);
		info->addr = nv_ro08(bios, data + 0x05);
		info->flags = nv_ro08(bios, data + 0x06);
	}
	return 0x0000;
}
