/*
 * Copyright 2012 Nouveau Community
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
#include <subdev/bios/therm.h>

static u32
therm_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *len, u8 *cnt)
{
	struct bit_entry bit_P;
	u32 therm = 0;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 1)
			therm = nvbios_rd32(bios, bit_P.offset + 12);
		else if (bit_P.version == 2)
			therm = nvbios_rd32(bios, bit_P.offset + 16);
		else
			nvkm_error(&bios->subdev,
				   "unknown offset for thermal in BIT P %d\n",
				   bit_P.version);
	}

	/* exit now if we haven't found the thermal table */
	if (!therm)
		return 0;

	*ver = nvbios_rd08(bios, therm + 0);
	*hdr = nvbios_rd08(bios, therm + 1);
	*len = nvbios_rd08(bios, therm + 2);
	*cnt = nvbios_rd08(bios, therm + 3);
	return therm + nvbios_rd08(bios, therm + 1);
}

static u32
nvbios_therm_entry(struct nvkm_bios *bios, int idx, u8 *ver, u8 *len)
{
	u8 hdr, cnt;
	u32 therm = therm_table(bios, ver, &hdr, len, &cnt);
	if (therm && idx < cnt)
		return therm + idx * *len;
	return 0;
}

int
nvbios_therm_sensor_parse(struct nvkm_bios *bios,
			  enum nvbios_therm_domain domain,
			  struct nvbios_therm_sensor *sensor)
{
	s8 thrs_section, sensor_section, offset;
	u8 ver, len, i;
	u32 entry;

	/* we only support the core domain for now */
	if (domain != NVBIOS_THERM_DOMAIN_CORE)
		return -EINVAL;

	/* Read the entries from the table */
	thrs_section = 0;
	sensor_section = -1;
	i = 0;
	while ((entry = nvbios_therm_entry(bios, i++, &ver, &len))) {
		s16 value = nvbios_rd16(bios, entry + 1);

		switch (nvbios_rd08(bios, entry + 0)) {
		case 0x0:
			thrs_section = value;
			if (value > 0)
				return 0; /* we do not try to support ambient */
			break;
		case 0x01:
			sensor_section++;
			if (sensor_section == 0) {
				offset = ((s8) nvbios_rd08(bios, entry + 2)) / 2;
				sensor->offset_constant = offset;
			}
			break;

		case 0x04:
			if (thrs_section == 0) {
				sensor->thrs_critical.temp = (value & 0xff0) >> 4;
				sensor->thrs_critical.hysteresis = value & 0xf;
			}
			break;

		case 0x07:
			if (thrs_section == 0) {
				sensor->thrs_down_clock.temp = (value & 0xff0) >> 4;
				sensor->thrs_down_clock.hysteresis = value & 0xf;
			}
			break;

		case 0x08:
			if (thrs_section == 0) {
				sensor->thrs_fan_boost.temp = (value & 0xff0) >> 4;
				sensor->thrs_fan_boost.hysteresis = value & 0xf;
			}
			break;

		case 0x10:
			if (sensor_section == 0)
				sensor->offset_num = value;
			break;

		case 0x11:
			if (sensor_section == 0)
				sensor->offset_den = value;
			break;

		case 0x12:
			if (sensor_section == 0)
				sensor->slope_mult = value;
			break;

		case 0x13:
			if (sensor_section == 0)
				sensor->slope_div = value;
			break;
		case 0x32:
			if (thrs_section == 0) {
				sensor->thrs_shutdown.temp = (value & 0xff0) >> 4;
				sensor->thrs_shutdown.hysteresis = value & 0xf;
			}
			break;
		}
	}

	return 0;
}

int
nvbios_therm_fan_parse(struct nvkm_bios *bios, struct nvbios_therm_fan *fan)
{
	struct nvbios_therm_trip_point *cur_trip = NULL;
	u8 ver, len, i;
	u32 entry;

	uint8_t duty_lut[] = { 0, 0, 25, 0, 40, 0, 50, 0,
				75, 0, 85, 0, 100, 0, 100, 0 };

	i = 0;
	fan->nr_fan_trip = 0;
	fan->fan_mode = NVBIOS_THERM_FAN_OTHER;
	while ((entry = nvbios_therm_entry(bios, i++, &ver, &len))) {
		s16 value = nvbios_rd16(bios, entry + 1);

		switch (nvbios_rd08(bios, entry + 0)) {
		case 0x22:
			fan->min_duty = value & 0xff;
			fan->max_duty = (value & 0xff00) >> 8;
			break;
		case 0x24:
			fan->nr_fan_trip++;
			if (fan->fan_mode > NVBIOS_THERM_FAN_TRIP)
				fan->fan_mode = NVBIOS_THERM_FAN_TRIP;
			cur_trip = &fan->trip[fan->nr_fan_trip - 1];
			cur_trip->hysteresis = value & 0xf;
			cur_trip->temp = (value & 0xff0) >> 4;
			cur_trip->fan_duty = duty_lut[(value & 0xf000) >> 12];
			break;
		case 0x25:
			cur_trip = &fan->trip[fan->nr_fan_trip - 1];
			cur_trip->fan_duty = value;
			break;
		case 0x26:
			if (!fan->pwm_freq)
				fan->pwm_freq = value;
			break;
		case 0x3b:
			fan->bump_period = value;
			break;
		case 0x3c:
			fan->slow_down_period = value;
			break;
		case 0x46:
			if (fan->fan_mode > NVBIOS_THERM_FAN_LINEAR)
				fan->fan_mode = NVBIOS_THERM_FAN_LINEAR;
			fan->linear_min_temp = nvbios_rd08(bios, entry + 1);
			fan->linear_max_temp = nvbios_rd08(bios, entry + 2);
			break;
		}
	}

	/* starting from fermi, fan management is always linear */
	if (bios->subdev.device->card_type >= NV_C0 &&
		fan->fan_mode == NVBIOS_THERM_FAN_OTHER) {
		fan->fan_mode = NVBIOS_THERM_FAN_LINEAR;
	}

	return 0;
}
