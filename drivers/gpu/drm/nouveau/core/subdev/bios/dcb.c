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

#include "core/device.h"

#include "subdev/bios.h"
#include "subdev/bios/dcb.h"

u16
dcb_table(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct nouveau_device *device = nv_device(bios);
	u16 dcb = 0x0000;

	if (device->card_type > NV_04)
		dcb = nv_ro16(bios, 0x36);
	if (!dcb) {
		nv_warn(bios, "DCB table not found\n");
		return dcb;
	}

	*ver = nv_ro08(bios, dcb);

	if (*ver >= 0x41) {
		nv_warn(bios, "DCB version 0x%02x unknown\n", *ver);
		return 0x0000;
	} else
	if (*ver >= 0x30) {
		if (nv_ro32(bios, dcb + 6) == 0x4edcbdcb) {
			*hdr = nv_ro08(bios, dcb + 1);
			*cnt = nv_ro08(bios, dcb + 2);
			*len = nv_ro08(bios, dcb + 3);
			return dcb;
		}
	} else
	if (*ver >= 0x20) {
		if (nv_ro32(bios, dcb + 4) == 0x4edcbdcb) {
			u16 i2c = nv_ro16(bios, dcb + 2);
			*hdr = 8;
			*cnt = (i2c - dcb) / 8;
			*len = 8;
			return dcb;
		}
	} else
	if (*ver >= 0x15) {
		if (!nv_memcmp(bios, dcb - 7, "DEV_REC", 7)) {
			u16 i2c = nv_ro16(bios, dcb + 2);
			*hdr = 4;
			*cnt = (i2c - dcb) / 10;
			*len = 10;
			return dcb;
		}
	} else {
		/*
		 * v1.4 (some NV15/16, NV11+) seems the same as v1.5, but
		 * always has the same single (crt) entry, even when tv-out
		 * present, so the conclusion is this version cannot really
		 * be used.
		 *
		 * v1.2 tables (some NV6/10, and NV15+) normally have the
		 * same 5 entries, which are not specific to the card and so
		 * no use.
		 *
		 * v1.2 does have an I2C table that read_dcb_i2c_table can
		 * handle, but cards exist (nv11 in #14821) with a bad i2c
		 * table pointer, so use the indices parsed in
		 * parse_bmp_structure.
		 *
		 * v1.1 (NV5+, maybe some NV4) is entirely unhelpful
		 */
		nv_warn(bios, "DCB contains no useful data\n");
		return 0x0000;
	}

	nv_warn(bios, "DCB header validation failed\n");
	return 0x0000;
}

u16
dcb_outp(struct nouveau_bios *bios, u8 idx, u8 *ver, u8 *len)
{
	u8  hdr, cnt;
	u16 dcb = dcb_table(bios, ver, &hdr, &cnt, len);
	if (dcb && idx < cnt)
		return dcb + hdr + (idx * *len);
	return 0x0000;
}

int
dcb_outp_foreach(struct nouveau_bios *bios, void *data,
		 int (*exec)(struct nouveau_bios *, void *, int, u16))
{
	int ret, idx = -1;
	u8  ver, len;
	u16 outp;

	while ((outp = dcb_outp(bios, ++idx, &ver, &len))) {
		if (nv_ro32(bios, outp) == 0x00000000)
			break; /* seen on an NV11 with DCB v1.5 */
		if (nv_ro32(bios, outp) == 0xffffffff)
			break; /* seen on an NV17 with DCB v2.0 */

		if (nv_ro08(bios, outp) == DCB_OUTPUT_UNUSED)
			continue;
		if (nv_ro08(bios, outp) == DCB_OUTPUT_EOL)
			break;

		ret = exec(bios, data, idx, outp);
		if (ret)
			return ret;
	}

	return 0;
}
