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

u16
dcb_gpio_table(struct nouveau_bios *bios)
{
	u8  ver, hdr, cnt, len;
	u16 dcb = dcb_table(bios, &ver, &hdr, &cnt, &len);
	if (dcb) {
		if (ver >= 0x30 && hdr >= 0x0c)
			return nv_ro16(bios, dcb + 0x0a);
		if (ver >= 0x22 && nv_ro08(bios, dcb - 1) >= 0x13)
			return nv_ro16(bios, dcb - 0x0f);
	}
	return 0x0000;
}

u16
dcb_gpio_entry(struct nouveau_bios *bios, int idx, int ent, u8 *ver)
{
	u16 gpio = dcb_gpio_table(bios);
	if (gpio) {
		*ver = nv_ro08(bios, gpio);
		if (*ver < 0x30 && ent < nv_ro08(bios, gpio + 2))
			return gpio + 3 + (ent * nv_ro08(bios, gpio + 1));
		else if (ent < nv_ro08(bios, gpio + 2))
			return gpio + nv_ro08(bios, gpio + 1) +
			       (ent * nv_ro08(bios, gpio + 3));
	}
	return 0x0000;
}

int
dcb_gpio_parse(struct nouveau_bios *bios, int idx, u8 func, u8 line,
	       struct dcb_gpio_func *gpio)
{
	u8  ver, hdr, cnt, len;
	u16 entry;
	int i = -1;

	while ((entry = dcb_gpio_entry(bios, idx, ++i, &ver))) {
		if (ver < 0x40) {
			u16 data = nv_ro16(bios, entry);
			*gpio = (struct dcb_gpio_func) {
				.line = (data & 0x001f) >> 0,
				.func = (data & 0x07e0) >> 5,
				.log[0] = (data & 0x1800) >> 11,
				.log[1] = (data & 0x6000) >> 13,
				.param = !!(data & 0x8000),
			};
		} else
		if (ver < 0x41) {
			u32 data = nv_ro32(bios, entry);
			*gpio = (struct dcb_gpio_func) {
				.line = (data & 0x0000001f) >> 0,
				.func = (data & 0x0000ff00) >> 8,
				.log[0] = (data & 0x18000000) >> 27,
				.log[1] = (data & 0x60000000) >> 29,
				.param = !!(data & 0x80000000),
			};
		} else {
			u32 data = nv_ro32(bios, entry + 0);
			u8 data1 = nv_ro32(bios, entry + 4);
			*gpio = (struct dcb_gpio_func) {
				.line = (data & 0x0000003f) >> 0,
				.func = (data & 0x0000ff00) >> 8,
				.log[0] = (data1 & 0x30) >> 4,
				.log[1] = (data1 & 0xc0) >> 6,
				.param = !!(data & 0x80000000),
			};
		}

		if ((line == 0xff || line == gpio->line) &&
		    (func == 0xff || func == gpio->func))
			return 0;
	}

	/* DCB 2.2, fixed TVDAC GPIO data */
	if ((entry = dcb_table(bios, &ver, &hdr, &cnt, &len)) && ver >= 0x22) {
		if (func == DCB_GPIO_TVDAC0) {
			u8 conf = nv_ro08(bios, entry - 5);
			u8 addr = nv_ro08(bios, entry - 4);
			if (conf & 0x01) {
				*gpio = (struct dcb_gpio_func) {
					.func = DCB_GPIO_TVDAC0,
					.line = addr >> 4,
					.log[0] = !!(conf & 0x02),
					.log[1] =  !(conf & 0x02),
				};
				return 0;
			}
		}
	}

	return -EINVAL;
}
