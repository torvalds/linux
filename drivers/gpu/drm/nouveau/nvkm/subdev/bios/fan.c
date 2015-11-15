/*
 * Copyright 2014 Martin Peres
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
 * Authors: Martin Peres
 */
#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/fan.h>

u16
nvbios_fan_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_P;
	u16 fan = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2 && bit_P.length >= 0x5a)
			fan = nvbios_rd16(bios, bit_P.offset + 0x58);

		if (fan) {
			*ver = nvbios_rd08(bios, fan + 0);
			switch (*ver) {
			case 0x10:
				*hdr = nvbios_rd08(bios, fan + 1);
				*len = nvbios_rd08(bios, fan + 2);
				*cnt = nvbios_rd08(bios, fan + 3);
				return fan;
			default:
				break;
			}
		}
	}

	return 0x0000;
}

u16
nvbios_fan_entry(struct nvkm_bios *bios, int idx, u8 *ver, u8 *hdr,
		 u8 *cnt, u8 *len)
{
	u16 data = nvbios_fan_table(bios, ver, hdr, cnt, len);
	if (data && idx < *cnt)
		return data + *hdr + (idx * (*len));
	return 0x0000;
}

u16
nvbios_fan_parse(struct nvkm_bios *bios, struct nvbios_therm_fan *fan)
{
	u8 ver, hdr, cnt, len;

	u16 data = nvbios_fan_entry(bios, 0, &ver, &hdr, &cnt, &len);
	if (data) {
		u8 type = nvbios_rd08(bios, data + 0x00);
		switch (type) {
		case 0:
			fan->type = NVBIOS_THERM_FAN_TOGGLE;
			break;
		case 1:
		case 2:
			/* TODO: Understand the difference between the two! */
			fan->type = NVBIOS_THERM_FAN_PWM;
			break;
		default:
			fan->type = NVBIOS_THERM_FAN_UNK;
		}

		fan->min_duty = nvbios_rd08(bios, data + 0x02);
		fan->max_duty = nvbios_rd08(bios, data + 0x03);

		fan->pwm_freq = nvbios_rd32(bios, data + 0x0b) & 0xffffff;
	}

	return data;
}
