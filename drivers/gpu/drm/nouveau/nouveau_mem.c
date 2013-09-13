/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 * Copyright 2005 Stephane Marchesin
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Skeggs <bskeggs@redhat.com>
 *    Roy Spliet <r.spliet@student.tudelft.nl>
 */

#include "nouveau_drm.h"
#include "nouveau_pm.h"

#include <subdev/fb.h>

static int
nv40_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	t->reg[0] = (e->tRP << 24 | e->tRAS << 16 | e->tRFC << 8 | e->tRC);

	/* XXX: I don't trust the -1's and +1's... they must come
	 *      from somewhere! */
	t->reg[1] = (e->tWR + 2 + (t->tCWL - 1)) << 24 |
		    1 << 16 |
		    (e->tWTR + 2 + (t->tCWL - 1)) << 8 |
		    (e->tCL + 2 - (t->tCWL - 1));

	t->reg[2] = 0x20200000 |
		    ((t->tCWL - 1) << 24 |
		     e->tRRD << 16 |
		     e->tRCDWR << 8 |
		     e->tRCDRD);

	NV_DEBUG(drm, "Entry %d: 220: %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2]);
	return 0;
}

static int
nv50_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct bit_entry P;
	uint8_t unk18 = 1, unk20 = 0, unk21 = 0, tmp7_3;

	if (bit_table(dev, 'P', &P))
		return -EINVAL;

	switch (min(len, (u8) 22)) {
	case 22:
		unk21 = e->tUNK_21;
	case 21:
		unk20 = e->tUNK_20;
	case 20:
		if (e->tCWL > 0)
			t->tCWL = e->tCWL;
	case 19:
		unk18 = e->tUNK_18;
		break;
	}

	t->reg[0] = (e->tRP << 24 | e->tRAS << 16 | e->tRFC << 8 | e->tRC);

	t->reg[1] = (e->tWR + 2 + (t->tCWL - 1)) << 24 |
				max(unk18, (u8) 1) << 16 |
				(e->tWTR + 2 + (t->tCWL - 1)) << 8;

	t->reg[2] = ((t->tCWL - 1) << 24 |
		    e->tRRD << 16 |
		    e->tRCDWR << 8 |
		    e->tRCDRD);

	t->reg[4] = e->tUNK_13 << 8  | e->tUNK_13;

	t->reg[5] = (e->tRFC << 24 | max(e->tRCDRD, e->tRCDWR) << 16 | e->tRP);

	t->reg[8] = boot->reg[8] & 0xffffff00;

	if (P.version == 1) {
		t->reg[1] |= (e->tCL + 2 - (t->tCWL - 1));

		t->reg[3] = (0x14 + e->tCL) << 24 |
			    0x16 << 16 |
			    (e->tCL - 1) << 8 |
			    (e->tCL - 1);

		t->reg[4] |= boot->reg[4] & 0xffff0000;

		t->reg[6] = (0x33 - t->tCWL) << 16 |
			    t->tCWL << 8 |
			    (0x2e + e->tCL - t->tCWL);

		t->reg[7] = 0x4000202 | (e->tCL - 1) << 16;

		/* XXX: P.version == 1 only has DDR2 and GDDR3? */
		if (pfb->ram->type == NV_MEM_TYPE_DDR2) {
			t->reg[5] |= (e->tCL + 3) << 8;
			t->reg[6] |= (t->tCWL - 2) << 8;
			t->reg[8] |= (e->tCL - 4);
		} else {
			t->reg[5] |= (e->tCL + 2) << 8;
			t->reg[6] |= t->tCWL << 8;
			t->reg[8] |= (e->tCL - 2);
		}
	} else {
		t->reg[1] |= (5 + e->tCL - (t->tCWL));

		/* XXX: 0xb? 0x30? */
		t->reg[3] = (0x30 + e->tCL) << 24 |
			    (boot->reg[3] & 0x00ff0000)|
			    (0xb + e->tCL) << 8 |
			    (e->tCL - 1);

		t->reg[4] |= (unk20 << 24 | unk21 << 16);

		/* XXX: +6? */
		t->reg[5] |= (t->tCWL + 6) << 8;

		t->reg[6] = (0x5a + e->tCL) << 16 |
			    (6 - e->tCL + t->tCWL) << 8 |
			    (0x50 + e->tCL - t->tCWL);

		tmp7_3 = (boot->reg[7] & 0xff000000) >> 24;
		t->reg[7] = (tmp7_3 << 24) |
			    ((tmp7_3 - 6 + e->tCL) << 16) |
			    0x202;
	}

	NV_DEBUG(drm, "Entry %d: 220: %08x %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2], t->reg[3]);
	NV_DEBUG(drm, "         230: %08x %08x %08x %08x\n",
		 t->reg[4], t->reg[5], t->reg[6], t->reg[7]);
	NV_DEBUG(drm, "         240: %08x\n", t->reg[8]);
	return 0;
}

static int
nvc0_mem_timing_calc(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (e->tCWL > 0)
		t->tCWL = e->tCWL;

	t->reg[0] = (e->tRP << 24 | (e->tRAS & 0x7f) << 17 |
		     e->tRFC << 8 | e->tRC);

	t->reg[1] = (boot->reg[1] & 0xff000000) |
		    (e->tRCDWR & 0x0f) << 20 |
		    (e->tRCDRD & 0x0f) << 14 |
		    (t->tCWL << 7) |
		    (e->tCL & 0x0f);

	t->reg[2] = (boot->reg[2] & 0xff0000ff) |
		    e->tWR << 16 | e->tWTR << 8;

	t->reg[3] = (e->tUNK_20 & 0x1f) << 9 |
		    (e->tUNK_21 & 0xf) << 5 |
		    (e->tUNK_13 & 0x1f);

	t->reg[4] = (boot->reg[4] & 0xfff00fff) |
		    (e->tRRD&0x1f) << 15;

	NV_DEBUG(drm, "Entry %d: 290: %08x %08x %08x %08x\n", t->id,
		 t->reg[0], t->reg[1], t->reg[2], t->reg[3]);
	NV_DEBUG(drm, "         2a0: %08x\n", t->reg[4]);
	return 0;
}

/**
 * MR generation methods
 */

static int
nouveau_mem_ddr2_mr(struct drm_device *dev, u32 freq,
		    struct nouveau_pm_tbl_entry *e, u8 len,
		    struct nouveau_pm_memtiming *boot,
		    struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	t->drive_strength = 0;
	if (len < 15) {
		t->odt = boot->odt;
	} else {
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_DDR2_MAX) {
		NV_WARN(drm, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_DDR2_MAX) {
		NV_WARN(drm, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(drm, "(%u) Invalid odt value, assuming disabled: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0x100f) |
		   (e->tCL) << 4 |
		   (e->tWR - 1) << 9;
	t->mr[1] = (boot->mr[1] & 0x101fbb) |
		   (t->odt & 0x1) << 2 |
		   (t->odt & 0x2) << 5;

	NV_DEBUG(drm, "(%u) MR: %08x", t->id, t->mr[0]);
	return 0;
}

static const uint8_t nv_mem_wr_lut_ddr3[NV_MEM_WR_DDR3_MAX] = {
	0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 0, 0};

static int
nouveau_mem_ddr3_mr(struct drm_device *dev, u32 freq,
		    struct nouveau_pm_tbl_entry *e, u8 len,
		    struct nouveau_pm_memtiming *boot,
		    struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	u8 cl = e->tCL - 4;

	t->drive_strength = 0;
	if (len < 15) {
		t->odt = boot->odt;
	} else {
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_DDR3_MAX || e->tCL < 4) {
		NV_WARN(drm, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_DDR3_MAX || e->tWR < 4) {
		NV_WARN(drm, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (e->tCWL < 5) {
		NV_WARN(drm, "(%u) Invalid tCWL: %u", t->id, e->tCWL);
		return -ERANGE;
	}

	t->mr[0] = (boot->mr[0] & 0x180b) |
		   /* CAS */
		   (cl & 0x7) << 4 |
		   (cl & 0x8) >> 1 |
		   (nv_mem_wr_lut_ddr3[e->tWR]) << 9;
	t->mr[1] = (boot->mr[1] & 0x101dbb) |
		   (t->odt & 0x1) << 2 |
		   (t->odt & 0x2) << 5 |
		   (t->odt & 0x4) << 7;
	t->mr[2] = (boot->mr[2] & 0x20ffb7) | (e->tCWL - 5) << 3;

	NV_DEBUG(drm, "(%u) MR: %08x %08x", t->id, t->mr[0], t->mr[2]);
	return 0;
}

static const uint8_t nv_mem_cl_lut_gddr3[NV_MEM_CL_GDDR3_MAX] = {
	0, 0, 0, 0, 4, 5, 6, 7, 0, 1, 2, 3, 8, 9, 10, 11};
static const uint8_t nv_mem_wr_lut_gddr3[NV_MEM_WR_GDDR3_MAX] = {
	0, 0, 0, 0, 0, 2, 3, 8, 9, 10, 11, 0, 0, 1, 1, 0, 3};

static int
nouveau_mem_gddr3_mr(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (len < 15) {
		t->drive_strength = boot->drive_strength;
		t->odt = boot->odt;
	} else {
		t->drive_strength = (e->RAM_FT1 & 0x30) >> 4;
		t->odt = e->RAM_FT1 & 0x07;
	}

	if (e->tCL >= NV_MEM_CL_GDDR3_MAX) {
		NV_WARN(drm, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_GDDR3_MAX) {
		NV_WARN(drm, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(drm, "(%u) Invalid odt value, assuming autocal: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0xe0b) |
		   /* CAS */
		   ((nv_mem_cl_lut_gddr3[e->tCL] & 0x7) << 4) |
		   ((nv_mem_cl_lut_gddr3[e->tCL] & 0x8) >> 2);
	t->mr[1] = (boot->mr[1] & 0x100f40) | t->drive_strength |
		   (t->odt << 2) |
		   (nv_mem_wr_lut_gddr3[e->tWR] & 0xf) << 4;
	t->mr[2] = boot->mr[2];

	NV_DEBUG(drm, "(%u) MR: %08x %08x %08x", t->id,
		      t->mr[0], t->mr[1], t->mr[2]);
	return 0;
}

static int
nouveau_mem_gddr5_mr(struct drm_device *dev, u32 freq,
		     struct nouveau_pm_tbl_entry *e, u8 len,
		     struct nouveau_pm_memtiming *boot,
		     struct nouveau_pm_memtiming *t)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (len < 15) {
		t->drive_strength = boot->drive_strength;
		t->odt = boot->odt;
	} else {
		t->drive_strength = (e->RAM_FT1 & 0x30) >> 4;
		t->odt = e->RAM_FT1 & 0x03;
	}

	if (e->tCL >= NV_MEM_CL_GDDR5_MAX) {
		NV_WARN(drm, "(%u) Invalid tCL: %u", t->id, e->tCL);
		return -ERANGE;
	}

	if (e->tWR >= NV_MEM_WR_GDDR5_MAX) {
		NV_WARN(drm, "(%u) Invalid tWR: %u", t->id, e->tWR);
		return -ERANGE;
	}

	if (t->odt > 3) {
		NV_WARN(drm, "(%u) Invalid odt value, assuming autocal: %x",
			t->id, t->odt);
		t->odt = 0;
	}

	t->mr[0] = (boot->mr[0] & 0x007) |
		   ((e->tCL - 5) << 3) |
		   ((e->tWR - 4) << 8);
	t->mr[1] = (boot->mr[1] & 0x1007f0) |
		   t->drive_strength |
		   (t->odt << 2);

	NV_DEBUG(drm, "(%u) MR: %08x %08x", t->id, t->mr[0], t->mr[1]);
	return 0;
}

int
nouveau_mem_timing_calc(struct drm_device *dev, u32 freq,
			struct nouveau_pm_memtiming *t)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_memtiming *boot = &pm->boot.timing;
	struct nouveau_pm_tbl_entry *e;
	u8 ver, len, *ptr, *ramcfg;
	int ret;

	ptr = nouveau_perf_timing(dev, freq, &ver, &len);
	if (!ptr || ptr[0] == 0x00) {
		*t = *boot;
		return 0;
	}
	e = (struct nouveau_pm_tbl_entry *)ptr;

	t->tCWL = boot->tCWL;

	switch (device->card_type) {
	case NV_40:
		ret = nv40_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	case NV_50:
		ret = nv50_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	case NV_C0:
	case NV_D0:
		ret = nvc0_mem_timing_calc(dev, freq, e, len, boot, t);
		break;
	default:
		ret = -ENODEV;
		break;
	}

	switch (pfb->ram->type * !ret) {
	case NV_MEM_TYPE_GDDR3:
		ret = nouveau_mem_gddr3_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_GDDR5:
		ret = nouveau_mem_gddr5_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_DDR2:
		ret = nouveau_mem_ddr2_mr(dev, freq, e, len, boot, t);
		break;
	case NV_MEM_TYPE_DDR3:
		ret = nouveau_mem_ddr3_mr(dev, freq, e, len, boot, t);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	ramcfg = nouveau_perf_ramcfg(dev, freq, &ver, &len);
	if (ramcfg) {
		int dll_off;

		if (ver == 0x00)
			dll_off = !!(ramcfg[3] & 0x04);
		else
			dll_off = !!(ramcfg[2] & 0x40);

		switch (pfb->ram->type) {
		case NV_MEM_TYPE_GDDR3:
			t->mr[1] &= ~0x00000040;
			t->mr[1] |=  0x00000040 * dll_off;
			break;
		default:
			t->mr[1] &= ~0x00000001;
			t->mr[1] |=  0x00000001 * dll_off;
			break;
		}
	}

	return ret;
}

void
nouveau_mem_timing_read(struct drm_device *dev, struct nouveau_pm_memtiming *t)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_fb *pfb = nouveau_fb(device);
	u32 timing_base, timing_regs, mr_base;
	int i;

	if (device->card_type >= 0xC0) {
		timing_base = 0x10f290;
		mr_base = 0x10f300;
	} else {
		timing_base = 0x100220;
		mr_base = 0x1002c0;
	}

	t->id = -1;

	switch (device->card_type) {
	case NV_50:
		timing_regs = 9;
		break;
	case NV_C0:
	case NV_D0:
		timing_regs = 5;
		break;
	case NV_30:
	case NV_40:
		timing_regs = 3;
		break;
	default:
		timing_regs = 0;
		return;
	}
	for(i = 0; i < timing_regs; i++)
		t->reg[i] = nv_rd32(device, timing_base + (0x04 * i));

	t->tCWL = 0;
	if (device->card_type < NV_C0) {
		t->tCWL = ((nv_rd32(device, 0x100228) & 0x0f000000) >> 24) + 1;
	} else if (device->card_type <= NV_D0) {
		t->tCWL = ((nv_rd32(device, 0x10f294) & 0x00000f80) >> 7);
	}

	t->mr[0] = nv_rd32(device, mr_base);
	t->mr[1] = nv_rd32(device, mr_base + 0x04);
	t->mr[2] = nv_rd32(device, mr_base + 0x20);
	t->mr[3] = nv_rd32(device, mr_base + 0x24);

	t->odt = 0;
	t->drive_strength = 0;

	switch (pfb->ram->type) {
	case NV_MEM_TYPE_DDR3:
		t->odt |= (t->mr[1] & 0x200) >> 7;
	case NV_MEM_TYPE_DDR2:
		t->odt |= (t->mr[1] & 0x04) >> 2 |
			  (t->mr[1] & 0x40) >> 5;
		break;
	case NV_MEM_TYPE_GDDR3:
	case NV_MEM_TYPE_GDDR5:
		t->drive_strength = t->mr[1] & 0x03;
		t->odt = (t->mr[1] & 0x0c) >> 2;
		break;
	default:
		break;
	}
}

int
nouveau_mem_exec(struct nouveau_mem_exec_func *exec,
		 struct nouveau_pm_level *perflvl)
{
	struct nouveau_drm *drm = nouveau_drm(exec->dev);
	struct nouveau_device *device = nouveau_dev(exec->dev);
	struct nouveau_fb *pfb = nouveau_fb(device);
	struct nouveau_pm_memtiming *info = &perflvl->timing;
	u32 tMRD = 1000, tCKSRE = 0, tCKSRX = 0, tXS = 0, tDLLK = 0;
	u32 mr[3] = { info->mr[0], info->mr[1], info->mr[2] };
	u32 mr1_dlloff;

	switch (pfb->ram->type) {
	case NV_MEM_TYPE_DDR2:
		tDLLK = 2000;
		mr1_dlloff = 0x00000001;
		break;
	case NV_MEM_TYPE_DDR3:
		tDLLK = 12000;
		tCKSRE = 2000;
		tXS = 1000;
		mr1_dlloff = 0x00000001;
		break;
	case NV_MEM_TYPE_GDDR3:
		tDLLK = 40000;
		mr1_dlloff = 0x00000040;
		break;
	default:
		NV_ERROR(drm, "cannot reclock unsupported memtype\n");
		return -ENODEV;
	}

	/* fetch current MRs */
	switch (pfb->ram->type) {
	case NV_MEM_TYPE_GDDR3:
	case NV_MEM_TYPE_DDR3:
		mr[2] = exec->mrg(exec, 2);
	default:
		mr[1] = exec->mrg(exec, 1);
		mr[0] = exec->mrg(exec, 0);
		break;
	}

	/* DLL 'on' -> DLL 'off' mode, disable before entering self-refresh  */
	if (!(mr[1] & mr1_dlloff) && (info->mr[1] & mr1_dlloff)) {
		exec->precharge(exec);
		exec->mrs (exec, 1, mr[1] | mr1_dlloff);
		exec->wait(exec, tMRD);
	}

	/* enter self-refresh mode */
	exec->precharge(exec);
	exec->refresh(exec);
	exec->refresh(exec);
	exec->refresh_auto(exec, false);
	exec->refresh_self(exec, true);
	exec->wait(exec, tCKSRE);

	/* modify input clock frequency */
	exec->clock_set(exec);

	/* exit self-refresh mode */
	exec->wait(exec, tCKSRX);
	exec->precharge(exec);
	exec->refresh_self(exec, false);
	exec->refresh_auto(exec, true);
	exec->wait(exec, tXS);
	exec->wait(exec, tXS);

	/* update MRs */
	if (mr[2] != info->mr[2]) {
		exec->mrs (exec, 2, info->mr[2]);
		exec->wait(exec, tMRD);
	}

	if (mr[1] != info->mr[1]) {
		/* need to keep DLL off until later, at least on GDDR3 */
		exec->mrs (exec, 1, info->mr[1] | (mr[1] & mr1_dlloff));
		exec->wait(exec, tMRD);
	}

	if (mr[0] != info->mr[0]) {
		exec->mrs (exec, 0, info->mr[0]);
		exec->wait(exec, tMRD);
	}

	/* update PFB timing registers */
	exec->timing_set(exec);

	/* DLL (enable + ) reset */
	if (!(info->mr[1] & mr1_dlloff)) {
		if (mr[1] & mr1_dlloff) {
			exec->mrs (exec, 1, info->mr[1]);
			exec->wait(exec, tMRD);
		}
		exec->mrs (exec, 0, info->mr[0] | 0x00000100);
		exec->wait(exec, tMRD);
		exec->mrs (exec, 0, info->mr[0] | 0x00000000);
		exec->wait(exec, tMRD);
		exec->wait(exec, tDLLK);
		if (pfb->ram->type == NV_MEM_TYPE_GDDR3)
			exec->precharge(exec);
	}

	return 0;
}
