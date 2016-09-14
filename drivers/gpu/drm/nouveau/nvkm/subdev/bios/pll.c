/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/bmp.h>
#include <subdev/bios/pll.h>
#include <subdev/vga.h>


struct pll_mapping {
	u8  type;
	u32 reg;
};

static struct pll_mapping
nv04_pll_mapping[] = {
	{ PLL_CORE  , 0x680500 },
	{ PLL_MEMORY, 0x680504 },
	{ PLL_VPLL0 , 0x680508 },
	{ PLL_VPLL1 , 0x680520 },
	{}
};

static struct pll_mapping
nv40_pll_mapping[] = {
	{ PLL_CORE  , 0x004000 },
	{ PLL_MEMORY, 0x004020 },
	{ PLL_VPLL0 , 0x680508 },
	{ PLL_VPLL1 , 0x680520 },
	{}
};

static struct pll_mapping
nv50_pll_mapping[] = {
	{ PLL_CORE  , 0x004028 },
	{ PLL_SHADER, 0x004020 },
	{ PLL_UNK03 , 0x004000 },
	{ PLL_MEMORY, 0x004008 },
	{ PLL_UNK40 , 0x00e810 },
	{ PLL_UNK41 , 0x00e818 },
	{ PLL_UNK42 , 0x00e824 },
	{ PLL_VPLL0 , 0x614100 },
	{ PLL_VPLL1 , 0x614900 },
	{}
};

static struct pll_mapping
g84_pll_mapping[] = {
	{ PLL_CORE  , 0x004028 },
	{ PLL_SHADER, 0x004020 },
	{ PLL_MEMORY, 0x004008 },
	{ PLL_VDEC  , 0x004030 },
	{ PLL_UNK41 , 0x00e818 },
	{ PLL_VPLL0 , 0x614100 },
	{ PLL_VPLL1 , 0x614900 },
	{}
};

static u16
pll_limits_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_C;
	u16 data = 0x0000;

	if (!bit_entry(bios, 'C', &bit_C)) {
		if (bit_C.version == 1 && bit_C.length >= 10)
			data = nvbios_rd16(bios, bit_C.offset + 8);
		if (data) {
			*ver = nvbios_rd08(bios, data + 0);
			*hdr = nvbios_rd08(bios, data + 1);
			*len = nvbios_rd08(bios, data + 2);
			*cnt = nvbios_rd08(bios, data + 3);
			return data;
		}
	}

	if (bmp_version(bios) >= 0x0524) {
		data = nvbios_rd16(bios, bios->bmp_offset + 142);
		if (data) {
			*ver = nvbios_rd08(bios, data + 0);
			*hdr = 1;
			*cnt = 1;
			*len = 0x18;
			return data;
		}
	}

	*ver = 0x00;
	return data;
}

static struct pll_mapping *
pll_map(struct nvkm_bios *bios)
{
	struct nvkm_device *device = bios->subdev.device;
	switch (device->card_type) {
	case NV_04:
	case NV_10:
	case NV_11:
	case NV_20:
	case NV_30:
		return nv04_pll_mapping;
		break;
	case NV_40:
		return nv40_pll_mapping;
	case NV_50:
		if (device->chipset == 0x50)
			return nv50_pll_mapping;
		else
		if (device->chipset <  0xa3 ||
		    device->chipset == 0xaa ||
		    device->chipset == 0xac)
			return g84_pll_mapping;
	default:
		return NULL;
	}
}

static u16
pll_map_reg(struct nvkm_bios *bios, u32 reg, u32 *type, u8 *ver, u8 *len)
{
	struct pll_mapping *map;
	u8  hdr, cnt;
	u16 data;

	data = pll_limits_table(bios, ver, &hdr, &cnt, len);
	if (data && *ver >= 0x30) {
		data += hdr;
		while (cnt--) {
			if (nvbios_rd32(bios, data + 3) == reg) {
				*type = nvbios_rd08(bios, data + 0);
				return data;
			}
			data += *len;
		}
		return 0x0000;
	}

	map = pll_map(bios);
	while (map && map->reg) {
		if (map->reg == reg && *ver >= 0x20) {
			u16 addr = (data += hdr);
			*type = map->type;
			while (cnt--) {
				if (nvbios_rd32(bios, data) == map->reg)
					return data;
				data += *len;
			}
			return addr;
		} else
		if (map->reg == reg) {
			*type = map->type;
			return data + 1;
		}
		map++;
	}

	return 0x0000;
}

static u16
pll_map_type(struct nvkm_bios *bios, u8 type, u32 *reg, u8 *ver, u8 *len)
{
	struct pll_mapping *map;
	u8  hdr, cnt;
	u16 data;

	data = pll_limits_table(bios, ver, &hdr, &cnt, len);
	if (data && *ver >= 0x30) {
		data += hdr;
		while (cnt--) {
			if (nvbios_rd08(bios, data + 0) == type) {
				*reg = nvbios_rd32(bios, data + 3);
				return data;
			}
			data += *len;
		}
		return 0x0000;
	}

	map = pll_map(bios);
	while (map && map->reg) {
		if (map->type == type && *ver >= 0x20) {
			u16 addr = (data += hdr);
			*reg = map->reg;
			while (cnt--) {
				if (nvbios_rd32(bios, data) == map->reg)
					return data;
				data += *len;
			}
			return addr;
		} else
		if (map->type == type) {
			*reg = map->reg;
			return data + 1;
		}
		map++;
	}

	return 0x0000;
}

int
nvbios_pll_parse(struct nvkm_bios *bios, u32 type, struct nvbios_pll *info)
{
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvkm_device *device = subdev->device;
	u8  ver, len;
	u32 reg = type;
	u16 data;

	if (type > PLL_MAX) {
		reg  = type;
		data = pll_map_reg(bios, reg, &type, &ver, &len);
	} else {
		data = pll_map_type(bios, type, &reg, &ver, &len);
	}

	if (ver && !data)
		return -ENOENT;

	memset(info, 0, sizeof(*info));
	info->type = type;
	info->reg = reg;

	switch (ver) {
	case 0x00:
		break;
	case 0x10:
	case 0x11:
		info->vco1.min_freq = nvbios_rd32(bios, data + 0);
		info->vco1.max_freq = nvbios_rd32(bios, data + 4);
		info->vco2.min_freq = nvbios_rd32(bios, data + 8);
		info->vco2.max_freq = nvbios_rd32(bios, data + 12);
		info->vco1.min_inputfreq = nvbios_rd32(bios, data + 16);
		info->vco2.min_inputfreq = nvbios_rd32(bios, data + 20);
		info->vco1.max_inputfreq = INT_MAX;
		info->vco2.max_inputfreq = INT_MAX;

		info->max_p = 0x7;
		info->max_p_usable = 0x6;

		/* these values taken from nv30/31/36 */
		switch (bios->version.chip) {
		case 0x36:
			info->vco1.min_n = 0x5;
			break;
		default:
			info->vco1.min_n = 0x1;
			break;
		}
		info->vco1.max_n = 0xff;
		info->vco1.min_m = 0x1;
		info->vco1.max_m = 0xd;

		/*
		 * On nv30, 31, 36 (i.e. all cards with two stage PLLs with this
		 * table version (apart from nv35)), N2 is compared to
		 * maxN2 (0x46) and 10 * maxM2 (0x4), so set maxN2 to 0x28 and
		 * save a comparison
		 */
		info->vco2.min_n = 0x4;
		switch (bios->version.chip) {
		case 0x30:
		case 0x35:
			info->vco2.max_n = 0x1f;
			break;
		default:
			info->vco2.max_n = 0x28;
			break;
		}
		info->vco2.min_m = 0x1;
		info->vco2.max_m = 0x4;
		break;
	case 0x20:
	case 0x21:
		info->vco1.min_freq = nvbios_rd16(bios, data + 4) * 1000;
		info->vco1.max_freq = nvbios_rd16(bios, data + 6) * 1000;
		info->vco2.min_freq = nvbios_rd16(bios, data + 8) * 1000;
		info->vco2.max_freq = nvbios_rd16(bios, data + 10) * 1000;
		info->vco1.min_inputfreq = nvbios_rd16(bios, data + 12) * 1000;
		info->vco2.min_inputfreq = nvbios_rd16(bios, data + 14) * 1000;
		info->vco1.max_inputfreq = nvbios_rd16(bios, data + 16) * 1000;
		info->vco2.max_inputfreq = nvbios_rd16(bios, data + 18) * 1000;
		info->vco1.min_n = nvbios_rd08(bios, data + 20);
		info->vco1.max_n = nvbios_rd08(bios, data + 21);
		info->vco1.min_m = nvbios_rd08(bios, data + 22);
		info->vco1.max_m = nvbios_rd08(bios, data + 23);
		info->vco2.min_n = nvbios_rd08(bios, data + 24);
		info->vco2.max_n = nvbios_rd08(bios, data + 25);
		info->vco2.min_m = nvbios_rd08(bios, data + 26);
		info->vco2.max_m = nvbios_rd08(bios, data + 27);

		info->max_p = nvbios_rd08(bios, data + 29);
		info->max_p_usable = info->max_p;
		if (bios->version.chip < 0x60)
			info->max_p_usable = 0x6;
		info->bias_p = nvbios_rd08(bios, data + 30);

		if (len > 0x22)
			info->refclk = nvbios_rd32(bios, data + 31);
		break;
	case 0x30:
		data = nvbios_rd16(bios, data + 1);

		info->vco1.min_freq = nvbios_rd16(bios, data + 0) * 1000;
		info->vco1.max_freq = nvbios_rd16(bios, data + 2) * 1000;
		info->vco2.min_freq = nvbios_rd16(bios, data + 4) * 1000;
		info->vco2.max_freq = nvbios_rd16(bios, data + 6) * 1000;
		info->vco1.min_inputfreq = nvbios_rd16(bios, data + 8) * 1000;
		info->vco2.min_inputfreq = nvbios_rd16(bios, data + 10) * 1000;
		info->vco1.max_inputfreq = nvbios_rd16(bios, data + 12) * 1000;
		info->vco2.max_inputfreq = nvbios_rd16(bios, data + 14) * 1000;
		info->vco1.min_n = nvbios_rd08(bios, data + 16);
		info->vco1.max_n = nvbios_rd08(bios, data + 17);
		info->vco1.min_m = nvbios_rd08(bios, data + 18);
		info->vco1.max_m = nvbios_rd08(bios, data + 19);
		info->vco2.min_n = nvbios_rd08(bios, data + 20);
		info->vco2.max_n = nvbios_rd08(bios, data + 21);
		info->vco2.min_m = nvbios_rd08(bios, data + 22);
		info->vco2.max_m = nvbios_rd08(bios, data + 23);
		info->max_p_usable = info->max_p = nvbios_rd08(bios, data + 25);
		info->bias_p = nvbios_rd08(bios, data + 27);
		info->refclk = nvbios_rd32(bios, data + 28);
		break;
	case 0x40:
		info->refclk = nvbios_rd16(bios, data + 9) * 1000;
		data = nvbios_rd16(bios, data + 1);

		info->vco1.min_freq = nvbios_rd16(bios, data + 0) * 1000;
		info->vco1.max_freq = nvbios_rd16(bios, data + 2) * 1000;
		info->vco1.min_inputfreq = nvbios_rd16(bios, data + 4) * 1000;
		info->vco1.max_inputfreq = nvbios_rd16(bios, data + 6) * 1000;
		info->vco1.min_m = nvbios_rd08(bios, data + 8);
		info->vco1.max_m = nvbios_rd08(bios, data + 9);
		info->vco1.min_n = nvbios_rd08(bios, data + 10);
		info->vco1.max_n = nvbios_rd08(bios, data + 11);
		info->min_p = nvbios_rd08(bios, data + 12);
		info->max_p = nvbios_rd08(bios, data + 13);
		break;
	default:
		nvkm_error(subdev, "unknown pll limits version 0x%02x\n", ver);
		return -EINVAL;
	}

	if (!info->refclk) {
		info->refclk = device->crystal;
		if (bios->version.chip == 0x51) {
			u32 sel_clk = nvkm_rd32(device, 0x680524);
			if ((info->reg == 0x680508 && sel_clk & 0x20) ||
			    (info->reg == 0x680520 && sel_clk & 0x80)) {
				if (nvkm_rdvgac(device, 0, 0x27) < 0xa3)
					info->refclk = 200000;
				else
					info->refclk = 25000;
			}
		}
	}

	/*
	 * By now any valid limit table ought to have set a max frequency for
	 * vco1, so if it's zero it's either a pre limit table bios, or one
	 * with an empty limit table (seen on nv18)
	 */
	if (!info->vco1.max_freq) {
		info->vco1.max_freq = nvbios_rd32(bios, bios->bmp_offset + 67);
		info->vco1.min_freq = nvbios_rd32(bios, bios->bmp_offset + 71);
		if (bmp_version(bios) < 0x0506) {
			info->vco1.max_freq = 256000;
			info->vco1.min_freq = 128000;
		}

		info->vco1.min_inputfreq = 0;
		info->vco1.max_inputfreq = INT_MAX;
		info->vco1.min_n = 0x1;
		info->vco1.max_n = 0xff;
		info->vco1.min_m = 0x1;

		if (device->crystal == 13500) {
			/* nv05 does this, nv11 doesn't, nv10 unknown */
			if (bios->version.chip < 0x11)
				info->vco1.min_m = 0x7;
			info->vco1.max_m = 0xd;
		} else {
			if (bios->version.chip < 0x11)
				info->vco1.min_m = 0x8;
			info->vco1.max_m = 0xe;
		}

		if (bios->version.chip <  0x17 ||
		    bios->version.chip == 0x1a ||
		    bios->version.chip == 0x20)
			info->max_p = 4;
		else
			info->max_p = 5;
		info->max_p_usable = info->max_p;
	}

	return 0;
}
