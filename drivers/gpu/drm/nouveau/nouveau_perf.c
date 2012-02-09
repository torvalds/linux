/*
 * Copyright 2010 Red Hat Inc.
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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"

static u8 *
nouveau_perf_table(struct drm_device *dev, u8 *ver)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct bit_entry P;

	if (!bit_table(dev, 'P', &P) && P.version && P.version <= 2) {
		u8 *perf = ROMPTR(dev, P.data[0]);
		if (perf) {
			*ver = perf[0];
			return perf;
		}
	}

	if (bios->type == NVBIOS_BMP) {
		if (bios->data[bios->offset + 6] >= 0x25) {
			u8 *perf = ROMPTR(dev, bios->data[bios->offset + 0x94]);
			if (perf) {
				*ver = perf[1];
				return perf;
			}
		}
	}

	return NULL;
}

static u8 *
nouveau_perf_entry(struct drm_device *dev, int idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8 *perf = nouveau_perf_table(dev, ver);
	if (perf) {
		if (*ver >= 0x12 && *ver < 0x20 && idx < perf[2]) {
			*hdr = perf[3];
			*cnt = 0;
			*len = 0;
			return perf + perf[0] + idx * perf[3];
		} else
		if (*ver >= 0x20 && *ver < 0x40 && idx < perf[2]) {
			*hdr = perf[3];
			*cnt = perf[4];
			*len = perf[5];
			return perf + perf[1] + idx * (*hdr + (*cnt * *len));
		} else
		if (*ver >= 0x40 && *ver < 0x41 && idx < perf[5]) {
			*hdr = perf[2];
			*cnt = perf[4];
			*len = perf[3];
			return perf + perf[1] + idx * (*hdr + (*cnt * *len));
		}
	}
	return NULL;
}

static u8 *
nouveau_perf_rammap(struct drm_device *dev, u32 freq,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct bit_entry P;
	u8 *perf, i = 0;

	if (!bit_table(dev, 'P', &P) && P.version == 2) {
		u8 *rammap = ROMPTR(dev, P.data[4]);
		if (rammap) {
			u8 *ramcfg = rammap + rammap[1];

			*ver = rammap[0];
			*hdr = rammap[2];
			*cnt = rammap[4];
			*len = rammap[3];

			freq /= 1000;
			for (i = 0; i < rammap[5]; i++) {
				if (freq >= ROM16(ramcfg[0]) &&
				    freq <= ROM16(ramcfg[2]))
					return ramcfg;

				ramcfg += *hdr + (*cnt * *len);
			}
		}

		return NULL;
	}

	if (dev_priv->chipset == 0x49 ||
	    dev_priv->chipset == 0x4b)
		freq /= 2;

	while ((perf = nouveau_perf_entry(dev, i++, ver, hdr, cnt, len))) {
		if (*ver >= 0x20 && *ver < 0x25) {
			if (perf[0] != 0xff && freq <= ROM16(perf[11]) * 1000)
				break;
		} else
		if (*ver >= 0x25 && *ver < 0x40) {
			if (perf[0] != 0xff && freq <= ROM16(perf[12]) * 1000)
				break;
		}
	}

	if (perf) {
		u8 *ramcfg = perf + *hdr;
		*ver = 0x00;
		*hdr = 0;
		return ramcfg;
	}

	return NULL;
}

u8 *
nouveau_perf_ramcfg(struct drm_device *dev, u32 freq, u8 *ver, u8 *len)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	u8 strap, hdr, cnt;
	u8 *rammap;

	strap = (nv_rd32(dev, 0x101000) & 0x0000003c) >> 2;
	if (bios->ram_restrict_tbl_ptr)
		strap = bios->data[bios->ram_restrict_tbl_ptr + strap];

	rammap = nouveau_perf_rammap(dev, freq, ver, &hdr, &cnt, len);
	if (rammap && strap < cnt)
		return rammap + hdr + (strap * *len);

	return NULL;
}

u8 *
nouveau_perf_timing(struct drm_device *dev, u32 freq, u8 *ver, u8 *len)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct bit_entry P;
	u8 *perf, *timing = NULL;
	u8 i = 0, hdr, cnt;

	if (bios->type == NVBIOS_BMP) {
		while ((perf = nouveau_perf_entry(dev, i++, ver, &hdr, &cnt,
						  len)) && *ver == 0x15) {
			if (freq <= ROM32(perf[5]) * 20) {
				*ver = 0x00;
				*len = 14;
				return perf + 41;
			}
		}
		return NULL;
	}

	if (!bit_table(dev, 'P', &P)) {
		if (P.version == 1)
			timing = ROMPTR(dev, P.data[4]);
		else
		if (P.version == 2)
			timing = ROMPTR(dev, P.data[8]);
	}

	if (timing && timing[0] == 0x10) {
		u8 *ramcfg = nouveau_perf_ramcfg(dev, freq, ver, len);
		if (ramcfg && ramcfg[1] < timing[2]) {
			*ver = timing[0];
			*len = timing[3];
			return timing + timing[1] + (ramcfg[1] * timing[3]);
		}
	}

	return NULL;
}

static void
legacy_perf_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	char *perf, *entry, *bmp = &bios->data[bios->offset];
	int headerlen, use_straps;

	if (bmp[5] < 0x5 || bmp[6] < 0x14) {
		NV_DEBUG(dev, "BMP version too old for perf\n");
		return;
	}

	perf = ROMPTR(dev, bmp[0x73]);
	if (!perf) {
		NV_DEBUG(dev, "No memclock table pointer found.\n");
		return;
	}

	switch (perf[0]) {
	case 0x12:
	case 0x14:
	case 0x18:
		use_straps = 0;
		headerlen = 1;
		break;
	case 0x01:
		use_straps = perf[1] & 1;
		headerlen = (use_straps ? 8 : 2);
		break;
	default:
		NV_WARN(dev, "Unknown memclock table version %x.\n", perf[0]);
		return;
	}

	entry = perf + headerlen;
	if (use_straps)
		entry += (nv_rd32(dev, NV_PEXTDEV_BOOT_0) & 0x3c) >> 1;

	sprintf(pm->perflvl[0].name, "performance_level_0");
	pm->perflvl[0].memory = ROM16(entry[0]) * 20;
	pm->nr_perflvl = 1;
}

static void
nouveau_perf_voltage(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct bit_entry P;
	u8 *vmap;
	int id;

	id = perflvl->volt_min;
	perflvl->volt_min = 0;

	/* boards using voltage table version <0x40 store the voltage
	 * level directly in the perflvl entry as a multiple of 10mV
	 */
	if (dev_priv->engine.pm.voltage.version < 0x40) {
		perflvl->volt_min = id * 10000;
		perflvl->volt_max = perflvl->volt_min;
		return;
	}

	/* on newer ones, the perflvl stores an index into yet another
	 * vbios table containing a min/max voltage value for the perflvl
	 */
	if (bit_table(dev, 'P', &P) || P.version != 2 || P.length < 34) {
		NV_DEBUG(dev, "where's our volt map table ptr? %d %d\n",
			 P.version, P.length);
		return;
	}

	vmap = ROMPTR(dev, P.data[32]);
	if (!vmap) {
		NV_DEBUG(dev, "volt map table pointer invalid\n");
		return;
	}

	if (id < vmap[3]) {
		vmap += vmap[1] + (vmap[2] * id);
		perflvl->volt_min = ROM32(vmap[0]);
		perflvl->volt_max = ROM32(vmap[4]);
	}
}

void
nouveau_perf_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nvbios *bios = &dev_priv->vbios;
	u8 *perf, ver, hdr, cnt, len;
	int ret, vid, i = -1;

	if (bios->type == NVBIOS_BMP && bios->data[bios->offset + 6] < 0x25) {
		legacy_perf_init(dev);
		return;
	}

	perf = nouveau_perf_table(dev, &ver);
	if (ver >= 0x20 && ver < 0x40)
		pm->fan.pwm_divisor = ROM16(perf[6]);

	while ((perf = nouveau_perf_entry(dev, ++i, &ver, &hdr, &cnt, &len))) {
		struct nouveau_pm_level *perflvl = &pm->perflvl[pm->nr_perflvl];

		if (perf[0] == 0xff)
			continue;

		switch (ver) {
		case 0x12:
		case 0x13:
		case 0x15:
			perflvl->fanspeed = perf[55];
			if (hdr > 56)
				perflvl->volt_min = perf[56];
			perflvl->core = ROM32(perf[1]) * 10;
			perflvl->memory = ROM32(perf[5]) * 20;
			break;
		case 0x21:
		case 0x23:
		case 0x24:
			perflvl->fanspeed = perf[4];
			perflvl->volt_min = perf[5];
			perflvl->shader = ROM16(perf[6]) * 1000;
			perflvl->core = perflvl->shader;
			perflvl->core += (signed char)perf[8] * 1000;
			if (dev_priv->chipset == 0x49 ||
			    dev_priv->chipset == 0x4b)
				perflvl->memory = ROM16(perf[11]) * 1000;
			else
				perflvl->memory = ROM16(perf[11]) * 2000;
			break;
		case 0x25:
			perflvl->fanspeed = perf[4];
			perflvl->volt_min = perf[5];
			perflvl->core = ROM16(perf[6]) * 1000;
			perflvl->shader = ROM16(perf[10]) * 1000;
			perflvl->memory = ROM16(perf[12]) * 1000;
			break;
		case 0x30:
			perflvl->memscript = ROM16(perf[2]);
		case 0x35:
			perflvl->fanspeed = perf[6];
			perflvl->volt_min = perf[7];
			perflvl->core = ROM16(perf[8]) * 1000;
			perflvl->shader = ROM16(perf[10]) * 1000;
			perflvl->memory = ROM16(perf[12]) * 1000;
			perflvl->vdec = ROM16(perf[16]) * 1000;
			perflvl->dom6 = ROM16(perf[20]) * 1000;
			break;
		case 0x40:
#define subent(n) ((ROM16(perf[hdr + (n) * len]) & 0xfff) * 1000)
			perflvl->fanspeed = 0; /*XXX*/
			perflvl->volt_min = perf[2];
			if (dev_priv->card_type == NV_50) {
				perflvl->core   = subent(0);
				perflvl->shader = subent(1);
				perflvl->memory = subent(2);
				perflvl->vdec   = subent(3);
				perflvl->unka0  = subent(4);
			} else {
				perflvl->hub06  = subent(0);
				perflvl->hub01  = subent(1);
				perflvl->copy   = subent(2);
				perflvl->shader = subent(3);
				perflvl->rop    = subent(4);
				perflvl->memory = subent(5);
				perflvl->vdec   = subent(6);
				perflvl->daemon = subent(10);
				perflvl->hub07  = subent(11);
				perflvl->core   = perflvl->shader / 2;
			}
			break;
		}

		/* make sure vid is valid */
		nouveau_perf_voltage(dev, perflvl);
		if (pm->voltage.supported && perflvl->volt_min) {
			vid = nouveau_volt_vid_lookup(dev, perflvl->volt_min);
			if (vid < 0) {
				NV_DEBUG(dev, "perflvl %d, bad vid\n", i);
				continue;
			}
		}

		/* get the corresponding memory timings */
		ret = nouveau_mem_timing_calc(dev, perflvl->memory,
					          &perflvl->timing);
		if (ret) {
			NV_DEBUG(dev, "perflvl %d, bad timing: %d\n", i, ret);
			continue;
		}

		snprintf(perflvl->name, sizeof(perflvl->name),
			 "performance_level_%d", i);
		perflvl->id = i;

		snprintf(perflvl->profile.name, sizeof(perflvl->profile.name),
			 "%d", perflvl->id);
		perflvl->profile.func = &nouveau_pm_static_profile_func;
		list_add_tail(&perflvl->profile.head, &pm->profiles);


		pm->nr_perflvl++;
	}
}

void
nouveau_perf_fini(struct drm_device *dev)
{
}
