/*
 * Copyright 2013 Red Hat Inc.
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
#include <subdev/bios/bit.h>
#include <subdev/bios/cstep.h>

u32
nvbios_cstepTe(struct nvkm_bios *bios,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *xnr, u8 *xsz)
{
	struct bit_entry bit_P;
	u32 cstep = 0;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2 && bit_P.length >= 0x38)
			cstep = nvbios_rd32(bios, bit_P.offset + 0x34);

		if (cstep) {
			*ver = nvbios_rd08(bios, cstep + 0);
			switch (*ver) {
			case 0x10:
				*hdr = nvbios_rd08(bios, cstep + 1);
				*cnt = nvbios_rd08(bios, cstep + 3);
				*len = nvbios_rd08(bios, cstep + 2);
				*xnr = nvbios_rd08(bios, cstep + 5);
				*xsz = nvbios_rd08(bios, cstep + 4);
				return cstep;
			default:
				break;
			}
		}
	}

	return 0;
}

u32
nvbios_cstepEe(struct nvkm_bios *bios, int idx, u8 *ver, u8 *hdr)
{
	u8  cnt, len, xnr, xsz;
	u32 data = nvbios_cstepTe(bios, ver, hdr, &cnt, &len, &xnr, &xsz);
	if (data && idx < cnt) {
		data = data + *hdr + (idx * len);
		*hdr = len;
		return data;
	}
	return 0;
}

u32
nvbios_cstepEp(struct nvkm_bios *bios, int idx, u8 *ver, u8 *hdr,
	       struct nvbios_cstepE *info)
{
	u32 data = nvbios_cstepEe(bios, idx, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->pstate = (nvbios_rd16(bios, data + 0x00) & 0x01e0) >> 5;
		info->index   = nvbios_rd08(bios, data + 0x03);
	}
	return data;
}

u32
nvbios_cstepEm(struct nvkm_bios *bios, u8 pstate, u8 *ver, u8 *hdr,
	       struct nvbios_cstepE *info)
{
	u32 data, idx = 0;
	while ((data = nvbios_cstepEp(bios, idx++, ver, hdr, info))) {
		if (info->pstate == pstate)
			break;
	}
	return data;
}

u32
nvbios_cstepXe(struct nvkm_bios *bios, int idx, u8 *ver, u8 *hdr)
{
	u8  cnt, len, xnr, xsz;
	u32 data = nvbios_cstepTe(bios, ver, hdr, &cnt, &len, &xnr, &xsz);
	if (data && idx < xnr) {
		data = data + *hdr + (cnt * len) + (idx * xsz);
		*hdr = xsz;
		return data;
	}
	return 0;
}

u32
nvbios_cstepXp(struct nvkm_bios *bios, int idx, u8 *ver, u8 *hdr,
	       struct nvbios_cstepX *info)
{
	u32 data = nvbios_cstepXe(bios, idx, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->freq    = nvbios_rd16(bios, data + 0x00) * 1000;
		info->unkn[0] = nvbios_rd08(bios, data + 0x02);
		info->unkn[1] = nvbios_rd08(bios, data + 0x03);
		info->voltage = nvbios_rd08(bios, data + 0x04);
	}
	return data;
}
