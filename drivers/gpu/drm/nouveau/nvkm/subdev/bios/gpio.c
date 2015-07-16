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
#include <subdev/bios/dcb.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/xpio.h>

u16
dcb_gpio_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u16 data = 0x0000;
	u16 dcb = dcb_table(bios, ver, hdr, cnt, len);
	if (dcb) {
		if (*ver >= 0x30 && *hdr >= 0x0c)
			data = nv_ro16(bios, dcb + 0x0a);
		else
		if (*ver >= 0x22 && nv_ro08(bios, dcb - 1) >= 0x13)
			data = nv_ro16(bios, dcb - 0x0f);

		if (data) {
			*ver = nv_ro08(bios, data + 0x00);
			if (*ver < 0x30) {
				*hdr = 3;
				*cnt = nv_ro08(bios, data + 0x02);
				*len = nv_ro08(bios, data + 0x01);
			} else
			if (*ver <= 0x41) {
				*hdr = nv_ro08(bios, data + 0x01);
				*cnt = nv_ro08(bios, data + 0x02);
				*len = nv_ro08(bios, data + 0x03);
			} else {
				data = 0x0000;
			}
		}
	}
	return data;
}

u16
dcb_gpio_entry(struct nvkm_bios *bios, int idx, int ent, u8 *ver, u8 *len)
{
	u8  hdr, cnt, xver; /* use gpio version for xpio entry parsing */
	u16 gpio;

	if (!idx--)
		gpio = dcb_gpio_table(bios, ver, &hdr, &cnt, len);
	else
		gpio = dcb_xpio_table(bios, idx, &xver, &hdr, &cnt, len);

	if (gpio && ent < cnt)
		return gpio + hdr + (ent * *len);

	return 0x0000;
}

u16
dcb_gpio_parse(struct nvkm_bios *bios, int idx, int ent, u8 *ver, u8 *len,
	       struct dcb_gpio_func *gpio)
{
	u16 data = dcb_gpio_entry(bios, idx, ent, ver, len);
	if (data) {
		if (*ver < 0x40) {
			u16 info = nv_ro16(bios, data);
			*gpio = (struct dcb_gpio_func) {
				.line = (info & 0x001f) >> 0,
				.func = (info & 0x07e0) >> 5,
				.log[0] = (info & 0x1800) >> 11,
				.log[1] = (info & 0x6000) >> 13,
				.param = !!(info & 0x8000),
			};
		} else
		if (*ver < 0x41) {
			u32 info = nv_ro32(bios, data);
			*gpio = (struct dcb_gpio_func) {
				.line = (info & 0x0000001f) >> 0,
				.func = (info & 0x0000ff00) >> 8,
				.log[0] = (info & 0x18000000) >> 27,
				.log[1] = (info & 0x60000000) >> 29,
				.param = !!(info & 0x80000000),
			};
		} else {
			u32 info = nv_ro32(bios, data + 0);
			u8 info1 = nv_ro32(bios, data + 4);
			*gpio = (struct dcb_gpio_func) {
				.line = (info & 0x0000003f) >> 0,
				.func = (info & 0x0000ff00) >> 8,
				.log[0] = (info1 & 0x30) >> 4,
				.log[1] = (info1 & 0xc0) >> 6,
				.param = !!(info & 0x80000000),
			};
		}
	}

	return data;
}

u16
dcb_gpio_match(struct nvkm_bios *bios, int idx, u8 func, u8 line,
	       u8 *ver, u8 *len, struct dcb_gpio_func *gpio)
{
	u8  hdr, cnt, i = 0;
	u16 data;

	while ((data = dcb_gpio_parse(bios, idx, i++, ver, len, gpio))) {
		if ((line == 0xff || line == gpio->line) &&
		    (func == 0xff || func == gpio->func))
			return data;
	}

	/* DCB 2.2, fixed TVDAC GPIO data */
	if ((data = dcb_table(bios, ver, &hdr, &cnt, len))) {
		if (*ver >= 0x22 && *ver < 0x30 && func == DCB_GPIO_TVDAC0) {
			u8 conf = nv_ro08(bios, data - 5);
			u8 addr = nv_ro08(bios, data - 4);
			if (conf & 0x01) {
				*gpio = (struct dcb_gpio_func) {
					.func = DCB_GPIO_TVDAC0,
					.line = addr >> 4,
					.log[0] = !!(conf & 0x02),
					.log[1] =  !(conf & 0x02),
				};
				*ver = 0x00;
				return data;
			}
		}
	}

	return 0x0000;
}
