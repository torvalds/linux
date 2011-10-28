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

	perf = ROMPTR(bios, bmp[0x73]);
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

static struct nouveau_pm_memtiming *
nouveau_perf_timing(struct drm_device *dev, struct bit_entry *P,
		    u16 memclk, u8 *entry, u8 recordlen, u8 entries)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nvbios *bios = &dev_priv->vbios;
	u8 ramcfg;
	int i;

	/* perf v2 has a separate "timing map" table, we have to match
	 * the target memory clock to a specific entry, *then* use
	 * ramcfg to select the correct subentry
	 */
	if (P->version == 2) {
		u8 *tmap = ROMPTR(bios, P->data[4]);
		if (!tmap) {
			NV_DEBUG(dev, "no timing map pointer\n");
			return NULL;
		}

		if (tmap[0] != 0x10) {
			NV_WARN(dev, "timing map 0x%02x unknown\n", tmap[0]);
			return NULL;
		}

		entry = tmap + tmap[1];
		recordlen = tmap[2] + (tmap[4] * tmap[3]);
		for (i = 0; i < tmap[5]; i++, entry += recordlen) {
			if (memclk >= ROM16(entry[0]) &&
			    memclk <= ROM16(entry[2]))
				break;
		}

		if (i == tmap[5]) {
			NV_WARN(dev, "no match in timing map table\n");
			return NULL;
		}

		entry += tmap[2];
		recordlen = tmap[3];
		entries   = tmap[4];
	}

	ramcfg = (nv_rd32(dev, NV_PEXTDEV_BOOT_0) & 0x0000003c) >> 2;
	if (bios->ram_restrict_tbl_ptr)
		ramcfg = bios->data[bios->ram_restrict_tbl_ptr + ramcfg];

	if (ramcfg >= entries) {
		NV_WARN(dev, "ramcfg strap out of bounds!\n");
		return NULL;
	}

	entry += ramcfg * recordlen;
	if (entry[1] >= pm->memtimings.nr_timing) {
		if (entry[1] != 0xff)
			NV_WARN(dev, "timingset %d does not exist\n", entry[1]);
		return NULL;
	}

	return &pm->memtimings.timing[entry[1]];
}

static void
nouveau_perf_voltage(struct drm_device *dev, struct bit_entry *P,
		     struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
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
	if (P->version != 2 || P->length < 34) {
		NV_DEBUG(dev, "where's our volt map table ptr? %d %d\n",
			 P->version, P->length);
		return;
	}

	vmap = ROMPTR(bios, P->data[32]);
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
	struct bit_entry P;
	struct nouveau_pm_memtimings *memtimings = &pm->memtimings;
	struct nouveau_pm_tbl_header mt_hdr;
	u8 version, headerlen, recordlen, entries;
	u8 *perf, *entry;
	int vid, i;

	if (bios->type == NVBIOS_BIT) {
		if (bit_table(dev, 'P', &P))
			return;

		if (P.version != 1 && P.version != 2) {
			NV_WARN(dev, "unknown perf for BIT P %d\n", P.version);
			return;
		}

		perf = ROMPTR(bios, P.data[0]);
		version   = perf[0];
		headerlen = perf[1];
		if (version < 0x40) {
			recordlen = perf[3] + (perf[4] * perf[5]);
			entries   = perf[2];
		} else {
			recordlen = perf[2] + (perf[3] * perf[4]);
			entries   = perf[5];
		}
	} else {
		if (bios->data[bios->offset + 6] < 0x25) {
			legacy_perf_init(dev);
			return;
		}

		perf = ROMPTR(bios, bios->data[bios->offset + 0x94]);
		if (!perf) {
			NV_DEBUG(dev, "perf table pointer invalid\n");
			return;
		}

		version   = perf[1];
		headerlen = perf[0];
		recordlen = perf[3];
		entries   = perf[2];
	}

	if (entries > NOUVEAU_PM_MAX_LEVEL) {
		NV_DEBUG(dev, "perf table has too many entries - buggy vbios?\n");
		entries = NOUVEAU_PM_MAX_LEVEL;
	}

	entry = perf + headerlen;

	/* For version 0x15, initialize memtiming table */
	if(version == 0x15) {
		memtimings->timing =
				kcalloc(entries, sizeof(*memtimings->timing), GFP_KERNEL);
		if(!memtimings) {
			NV_WARN(dev,"Could not allocate memtiming table\n");
			return;
		}

		mt_hdr.entry_cnt = entries;
		mt_hdr.entry_len = 14;
		mt_hdr.version = version;
		mt_hdr.header_len = 4;
	}

	for (i = 0; i < entries; i++) {
		struct nouveau_pm_level *perflvl = &pm->perflvl[pm->nr_perflvl];

		perflvl->timing = NULL;

		if (entry[0] == 0xff) {
			entry += recordlen;
			continue;
		}

		switch (version) {
		case 0x12:
		case 0x13:
		case 0x15:
			perflvl->fanspeed = entry[55];
			if (recordlen > 56)
				perflvl->volt_min = entry[56];
			perflvl->core = ROM32(entry[1]) * 10;
			perflvl->memory = ROM32(entry[5]) * 20;
			break;
		case 0x21:
		case 0x23:
		case 0x24:
			perflvl->fanspeed = entry[4];
			perflvl->volt_min = entry[5];
			perflvl->shader = ROM16(entry[6]) * 1000;
			perflvl->core = perflvl->shader;
			perflvl->core += (signed char)entry[8] * 1000;
			if (dev_priv->chipset == 0x49 ||
			    dev_priv->chipset == 0x4b)
				perflvl->memory = ROM16(entry[11]) * 1000;
			else
				perflvl->memory = ROM16(entry[11]) * 2000;

			break;
		case 0x25:
			perflvl->fanspeed = entry[4];
			perflvl->volt_min = entry[5];
			perflvl->core = ROM16(entry[6]) * 1000;
			perflvl->shader = ROM16(entry[10]) * 1000;
			perflvl->memory = ROM16(entry[12]) * 1000;
			break;
		case 0x30:
			perflvl->memscript = ROM16(entry[2]);
		case 0x35:
			perflvl->fanspeed = entry[6];
			perflvl->volt_min = entry[7];
			perflvl->core = ROM16(entry[8]) * 1000;
			perflvl->shader = ROM16(entry[10]) * 1000;
			perflvl->memory = ROM16(entry[12]) * 1000;
			/*XXX: confirm on 0x35 */
			perflvl->unk05 = ROM16(entry[16]) * 1000;
			break;
		case 0x40:
#define subent(n) (ROM16(entry[perf[2] + ((n) * perf[3])]) & 0xfff) * 1000
			perflvl->fanspeed = 0; /*XXX*/
			perflvl->volt_min = entry[2];
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
		nouveau_perf_voltage(dev, &P, perflvl);
		if (pm->voltage.supported && perflvl->volt_min) {
			vid = nouveau_volt_vid_lookup(dev, perflvl->volt_min);
			if (vid < 0) {
				NV_DEBUG(dev, "drop perflvl %d, bad vid\n", i);
				entry += recordlen;
				continue;
			}
		}

		/* get the corresponding memory timings */
		if (version == 0x15) {
			memtimings->timing[i].id = i;
			nv30_mem_timing_entry(dev,&mt_hdr,(struct nouveau_pm_tbl_entry*) &entry[41],0,&memtimings->timing[i]);
			perflvl->timing = &memtimings->timing[i];
		} else if (version > 0x15) {
			/* last 3 args are for < 0x40, ignored for >= 0x40 */
			perflvl->timing =
				nouveau_perf_timing(dev, &P,
						    perflvl->memory / 1000,
						    entry + perf[3],
						    perf[5], perf[4]);
		}

		snprintf(perflvl->name, sizeof(perflvl->name),
			 "performance_level_%d", i);
		perflvl->id = i;
		pm->nr_perflvl++;

		entry += recordlen;
	}
}

void
nouveau_perf_fini(struct drm_device *dev)
{
}
