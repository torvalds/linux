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
#include <subdev/bios/bit.h>
#include <subdev/bios/mxm.h>

u16
mxm_table(struct nouveau_bios *bios, u8 *ver, u8 *hdr)
{
	struct bit_entry x;

	if (bit_entry(bios, 'x', &x)) {
		nv_debug(bios, "BIT 'x' table not present\n");
		return 0x0000;
	}

	*ver = x.version;
	*hdr = x.length;
	if (*ver != 1 || *hdr < 3) {
		nv_warn(bios, "BIT 'x' table %d/%d unknown\n", *ver, *hdr);
		return 0x0000;
	}

	return x.offset;
}

/* These map MXM v2.x digital connection values to the appropriate SOR/link,
 * hopefully they're correct for all boards within the same chipset...
 *
 * MXM v3.x VBIOS are nicer and provide pointers to these tables.
 */
static u8 nv84_sor_map[16] = {
	0x00, 0x12, 0x22, 0x11, 0x32, 0x31, 0x11, 0x31,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv92_sor_map[16] = {
	0x00, 0x12, 0x22, 0x11, 0x32, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv94_sor_map[16] = {
	0x00, 0x14, 0x24, 0x11, 0x34, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00
};

static u8 nv98_sor_map[16] = {
	0x00, 0x14, 0x12, 0x11, 0x00, 0x31, 0x11, 0x31,
	0x11, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

u8
mxm_sor_map(struct nouveau_bios *bios, u8 conn)
{
	u8  ver, hdr;
	u16 mxm = mxm_table(bios, &ver, &hdr);
	if (mxm && hdr >= 6) {
		u16 map = nv_ro16(bios, mxm + 4);
		if (map) {
			ver = nv_ro08(bios, map);
			if (ver == 0x10) {
				if (conn < nv_ro08(bios, map + 3)) {
					map += nv_ro08(bios, map + 1);
					map += conn;
					return nv_ro08(bios, map);
				}

				return 0x00;
			}

			nv_warn(bios, "unknown sor map v%02x\n", ver);
		}
	}

	if (bios->version.chip == 0x84 || bios->version.chip == 0x86)
		return nv84_sor_map[conn];
	if (bios->version.chip == 0x92)
		return nv92_sor_map[conn];
	if (bios->version.chip == 0x94 || bios->version.chip == 0x96)
		return nv94_sor_map[conn];
	if (bios->version.chip == 0x98)
		return nv98_sor_map[conn];

	nv_warn(bios, "missing sor map\n");
	return 0x00;
}

u8
mxm_ddc_map(struct nouveau_bios *bios, u8 port)
{
	u8  ver, hdr;
	u16 mxm = mxm_table(bios, &ver, &hdr);
	if (mxm && hdr >= 8) {
		u16 map = nv_ro16(bios, mxm + 6);
		if (map) {
			ver = nv_ro08(bios, map);
			if (ver == 0x10) {
				if (port < nv_ro08(bios, map + 3)) {
					map += nv_ro08(bios, map + 1);
					map += port;
					return nv_ro08(bios, map);
				}

				return 0x00;
			}

			nv_warn(bios, "unknown ddc map v%02x\n", ver);
		}
	}

	/* v2.x: directly write port as dcb i2cidx */
	return (port << 4) | port;
}
