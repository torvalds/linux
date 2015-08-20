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
#include "nv50.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

static u64
nv50_gr_units(struct nvkm_gr *gr)
{
	return nvkm_rd32(gr->engine.subdev.device, 0x1540);
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static int
nv50_gr_object_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		    int align, struct nvkm_gpuobj **pgpuobj)
{
	int ret = nvkm_gpuobj_new(object->engine->subdev.device, 16,
				  align, false, parent, pgpuobj);
	if (ret == 0) {
		nvkm_kmap(*pgpuobj);
		nvkm_wo32(*pgpuobj, 0x00, object->oclass_name);
		nvkm_wo32(*pgpuobj, 0x04, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x08, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x0c, 0x00000000);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

static const struct nvkm_object_func
nv50_gr_object = {
	.bind = nv50_gr_object_bind,
};

static int
nv50_gr_object_get(struct nvkm_gr *base, int index, struct nvkm_sclass *sclass)
{
	struct nv50_gr *gr = nv50_gr(base);
	int c = 0;

	while (gr->func->sclass[c].oclass) {
		if (c++ == index) {
			*sclass = gr->func->sclass[index];
			return index;
		}
	}

	return c;
}

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv50_gr_chan_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		  int align, struct nvkm_gpuobj **pgpuobj)
{
	struct nv50_gr *gr = nv50_gr_chan(object)->gr;
	int ret = nvkm_gpuobj_new(gr->base.engine.subdev.device, gr->size,
				  align, true, parent, pgpuobj);
	if (ret == 0) {
		nvkm_kmap(*pgpuobj);
		nv50_grctx_fill(gr->base.engine.subdev.device, *pgpuobj);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

static const struct nvkm_object_func
nv50_gr_chan = {
	.bind = nv50_gr_chan_bind,
};

static int
nv50_gr_chan_new(struct nvkm_gr *base, struct nvkm_fifo_chan *fifoch,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nv50_gr *gr = nv50_gr(base);
	struct nv50_gr_chan *chan;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv50_gr_chan, oclass, &chan->object);
	chan->gr = gr;
	*pobject = &chan->object;
	return 0;
}

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static const struct nvkm_bitfield nv50_gr_status[] = {
	{ 0x00000001, "BUSY" }, /* set when any bit is set */
	{ 0x00000002, "DISPATCH" },
	{ 0x00000004, "UNK2" },
	{ 0x00000008, "UNK3" },
	{ 0x00000010, "UNK4" },
	{ 0x00000020, "UNK5" },
	{ 0x00000040, "M2MF" },
	{ 0x00000080, "UNK7" },
	{ 0x00000100, "CTXPROG" },
	{ 0x00000200, "VFETCH" },
	{ 0x00000400, "CCACHE_PREGEOM" },
	{ 0x00000800, "STRMOUT_VATTR_POSTGEOM" },
	{ 0x00001000, "VCLIP" },
	{ 0x00002000, "RATTR_APLANE" },
	{ 0x00004000, "TRAST" },
	{ 0x00008000, "CLIPID" },
	{ 0x00010000, "ZCULL" },
	{ 0x00020000, "ENG2D" },
	{ 0x00040000, "RMASK" },
	{ 0x00080000, "TPC_RAST" },
	{ 0x00100000, "TPC_PROP" },
	{ 0x00200000, "TPC_TEX" },
	{ 0x00400000, "TPC_GEOM" },
	{ 0x00800000, "TPC_MP" },
	{ 0x01000000, "ROP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_0[] = {
	{ 0x01, "VFETCH" },
	{ 0x02, "CCACHE" },
	{ 0x04, "PREGEOM" },
	{ 0x08, "POSTGEOM" },
	{ 0x10, "VATTR" },
	{ 0x20, "STRMOUT" },
	{ 0x40, "VCLIP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_1[] = {
	{ 0x01, "TPC_RAST" },
	{ 0x02, "TPC_PROP" },
	{ 0x04, "TPC_TEX" },
	{ 0x08, "TPC_GEOM" },
	{ 0x10, "TPC_MP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_2[] = {
	{ 0x01, "RATTR" },
	{ 0x02, "APLANE" },
	{ 0x04, "TRAST" },
	{ 0x08, "CLIPID" },
	{ 0x10, "ZCULL" },
	{ 0x20, "ENG2D" },
	{ 0x40, "RMASK" },
	{ 0x80, "ROP" },
	{}
};

static void
nvkm_gr_vstatus_print(struct nv50_gr *gr, int r,
		      const struct nvkm_bitfield *units, u32 status)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	u32 stat = status;
	u8  mask = 0x00;
	char msg[64];
	int i;

	for (i = 0; units[i].name && status; i++) {
		if ((status & 7) == 1)
			mask |= (1 << i);
		status >>= 3;
	}

	nvkm_snprintbf(msg, sizeof(msg), units, mask);
	nvkm_error(subdev, "PGRAPH_VSTATUS%d: %08x [%s]\n", r, stat, msg);
}

static int
g84_gr_tlb_flush(struct nvkm_engine *engine)
{
	struct nv50_gr *gr = (void *)engine;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_timer *tmr = device->timer;
	bool idle, timeout = false;
	unsigned long flags;
	char status[128];
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&gr->lock, flags);
	nvkm_mask(device, 0x400500, 0x00000001, 0x00000000);

	start = nvkm_timer_read(tmr);
	do {
		idle = true;

		for (tmp = nvkm_rd32(device, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nvkm_rd32(device, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nvkm_rd32(device, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle &&
		 !(timeout = nvkm_timer_read(tmr) - start > 2000000000));

	if (timeout) {
		nvkm_error(subdev, "PGRAPH TLB flush idle timeout fail\n");

		tmp = nvkm_rd32(device, 0x400700);
		nvkm_snprintbf(status, sizeof(status), nv50_gr_status, tmp);
		nvkm_error(subdev, "PGRAPH_STATUS %08x [%s]\n", tmp, status);

		nvkm_gr_vstatus_print(gr, 0, nv50_gr_vstatus_0,
				       nvkm_rd32(device, 0x400380));
		nvkm_gr_vstatus_print(gr, 1, nv50_gr_vstatus_1,
				       nvkm_rd32(device, 0x400384));
		nvkm_gr_vstatus_print(gr, 2, nv50_gr_vstatus_2,
				       nvkm_rd32(device, 0x400388));
	}


	nvkm_wr32(device, 0x100c80, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x100c80) & 0x00000001))
			break;
	);
	nvkm_mask(device, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&gr->lock, flags);
	return timeout ? -EBUSY : 0;
}

static const struct nvkm_bitfield nv50_mp_exec_errors[] = {
	{ 0x01, "STACK_UNDERFLOW" },
	{ 0x02, "STACK_MISMATCH" },
	{ 0x04, "QUADON_ACTIVE" },
	{ 0x08, "TIMEOUT" },
	{ 0x10, "INVALID_OPCODE" },
	{ 0x20, "PM_OVERFLOW" },
	{ 0x40, "BREAKPOINT" },
	{}
};

static const struct nvkm_bitfield nv50_mpc_traps[] = {
	{ 0x0000001, "LOCAL_LIMIT_READ" },
	{ 0x0000010, "LOCAL_LIMIT_WRITE" },
	{ 0x0000040, "STACK_LIMIT" },
	{ 0x0000100, "GLOBAL_LIMIT_READ" },
	{ 0x0001000, "GLOBAL_LIMIT_WRITE" },
	{ 0x0010000, "MP0" },
	{ 0x0020000, "MP1" },
	{ 0x0040000, "GLOBAL_LIMIT_RED" },
	{ 0x0400000, "GLOBAL_LIMIT_ATOM" },
	{ 0x4000000, "MP2" },
	{}
};

static const struct nvkm_bitfield nv50_tex_traps[] = {
	{ 0x00000001, "" }, /* any bit set? */
	{ 0x00000002, "FAULT" },
	{ 0x00000004, "STORAGE_TYPE_MISMATCH" },
	{ 0x00000008, "LINEAR_MISMATCH" },
	{ 0x00000020, "WRONG_MEMTYPE" },
	{}
};

static const struct nvkm_bitfield nv50_gr_trap_m2mf[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "IN" },
	{ 0x00000004, "OUT" },
	{}
};

static const struct nvkm_bitfield nv50_gr_trap_vfetch[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static const struct nvkm_bitfield nv50_gr_trap_strmout[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static const struct nvkm_bitfield nv50_gr_trap_ccache[] = {
	{ 0x00000001, "FAULT" },
	{}
};

/* There must be a *lot* of these. Will take some time to gather them up. */
const struct nvkm_enum nv50_data_error_names[] = {
	{ 0x00000003, "INVALID_OPERATION", NULL },
	{ 0x00000004, "INVALID_VALUE", NULL },
	{ 0x00000005, "INVALID_ENUM", NULL },
	{ 0x00000008, "INVALID_OBJECT", NULL },
	{ 0x00000009, "READ_ONLY_OBJECT", NULL },
	{ 0x0000000a, "SUPERVISOR_OBJECT", NULL },
	{ 0x0000000b, "INVALID_ADDRESS_ALIGNMENT", NULL },
	{ 0x0000000c, "INVALID_BITFIELD", NULL },
	{ 0x0000000d, "BEGIN_END_ACTIVE", NULL },
	{ 0x0000000e, "SEMANTIC_COLOR_BACK_OVER_LIMIT", NULL },
	{ 0x0000000f, "VIEWPORT_ID_NEEDS_GP", NULL },
	{ 0x00000010, "RT_DOUBLE_BIND", NULL },
	{ 0x00000011, "RT_TYPES_MISMATCH", NULL },
	{ 0x00000012, "RT_LINEAR_WITH_ZETA", NULL },
	{ 0x00000015, "FP_TOO_FEW_REGS", NULL },
	{ 0x00000016, "ZETA_FORMAT_CSAA_MISMATCH", NULL },
	{ 0x00000017, "RT_LINEAR_WITH_MSAA", NULL },
	{ 0x00000018, "FP_INTERPOLANT_START_OVER_LIMIT", NULL },
	{ 0x00000019, "SEMANTIC_LAYER_OVER_LIMIT", NULL },
	{ 0x0000001a, "RT_INVALID_ALIGNMENT", NULL },
	{ 0x0000001b, "SAMPLER_OVER_LIMIT", NULL },
	{ 0x0000001c, "TEXTURE_OVER_LIMIT", NULL },
	{ 0x0000001e, "GP_TOO_MANY_OUTPUTS", NULL },
	{ 0x0000001f, "RT_BPP128_WITH_MS8", NULL },
	{ 0x00000021, "Z_OUT_OF_BOUNDS", NULL },
	{ 0x00000023, "XY_OUT_OF_BOUNDS", NULL },
	{ 0x00000024, "VP_ZERO_INPUTS", NULL },
	{ 0x00000027, "CP_MORE_PARAMS_THAN_SHARED", NULL },
	{ 0x00000028, "CP_NO_REG_SPACE_STRIPED", NULL },
	{ 0x00000029, "CP_NO_REG_SPACE_PACKED", NULL },
	{ 0x0000002a, "CP_NOT_ENOUGH_WARPS", NULL },
	{ 0x0000002b, "CP_BLOCK_SIZE_MISMATCH", NULL },
	{ 0x0000002c, "CP_NOT_ENOUGH_LOCAL_WARPS", NULL },
	{ 0x0000002d, "CP_NOT_ENOUGH_STACK_WARPS", NULL },
	{ 0x0000002e, "CP_NO_BLOCKDIM_LATCH", NULL },
	{ 0x00000031, "ENG2D_FORMAT_MISMATCH", NULL },
	{ 0x0000003f, "PRIMITIVE_ID_NEEDS_GP", NULL },
	{ 0x00000044, "SEMANTIC_VIEWPORT_OVER_LIMIT", NULL },
	{ 0x00000045, "SEMANTIC_COLOR_FRONT_OVER_LIMIT", NULL },
	{ 0x00000046, "LAYER_ID_NEEDS_GP", NULL },
	{ 0x00000047, "SEMANTIC_CLIP_OVER_LIMIT", NULL },
	{ 0x00000048, "SEMANTIC_PTSZ_OVER_LIMIT", NULL },
	{}
};

static const struct nvkm_bitfield nv50_gr_intr_name[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "COMPUTE_QUERY" },
	{ 0x00000010, "ILLEGAL_MTHD" },
	{ 0x00000020, "ILLEGAL_CLASS" },
	{ 0x00000040, "DOUBLE_NOTIFY" },
	{ 0x00001000, "CONTEXT_SWITCH" },
	{ 0x00010000, "BUFFER_NOTIFY" },
	{ 0x00100000, "DATA_ERROR" },
	{ 0x00200000, "TRAP" },
	{ 0x01000000, "SINGLE_STEP" },
	{}
};

static const struct nvkm_bitfield nv50_gr_trap_prop[] = {
	{ 0x00000004, "SURF_WIDTH_OVERRUN" },
	{ 0x00000008, "SURF_HEIGHT_OVERRUN" },
	{ 0x00000010, "DST2D_FAULT" },
	{ 0x00000020, "ZETA_FAULT" },
	{ 0x00000040, "RT_FAULT" },
	{ 0x00000080, "CUDA_FAULT" },
	{ 0x00000100, "DST2D_STORAGE_TYPE_MISMATCH" },
	{ 0x00000200, "ZETA_STORAGE_TYPE_MISMATCH" },
	{ 0x00000400, "RT_STORAGE_TYPE_MISMATCH" },
	{ 0x00000800, "DST2D_LINEAR_MISMATCH" },
	{ 0x00001000, "RT_LINEAR_MISMATCH" },
	{}
};

static void
nv50_gr_prop_trap(struct nv50_gr *gr, u32 ustatus_addr, u32 ustatus, u32 tp)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 e0c = nvkm_rd32(device, ustatus_addr + 0x04);
	u32 e10 = nvkm_rd32(device, ustatus_addr + 0x08);
	u32 e14 = nvkm_rd32(device, ustatus_addr + 0x0c);
	u32 e18 = nvkm_rd32(device, ustatus_addr + 0x10);
	u32 e1c = nvkm_rd32(device, ustatus_addr + 0x14);
	u32 e20 = nvkm_rd32(device, ustatus_addr + 0x18);
	u32 e24 = nvkm_rd32(device, ustatus_addr + 0x1c);
	char msg[128];

	/* CUDA memory: l[], g[] or stack. */
	if (ustatus & 0x00000080) {
		if (e18 & 0x80000000) {
			/* g[] read fault? */
			nvkm_error(subdev, "TRAP_PROP - TP %d - CUDA_FAULT - Global read fault at address %02x%08x\n",
					 tp, e14, e10 | ((e18 >> 24) & 0x1f));
			e18 &= ~0x1f000000;
		} else if (e18 & 0xc) {
			/* g[] write fault? */
			nvkm_error(subdev, "TRAP_PROP - TP %d - CUDA_FAULT - Global write fault at address %02x%08x\n",
				 tp, e14, e10 | ((e18 >> 7) & 0x1f));
			e18 &= ~0x00000f80;
		} else {
			nvkm_error(subdev, "TRAP_PROP - TP %d - Unknown CUDA fault at address %02x%08x\n",
				 tp, e14, e10);
		}
		ustatus &= ~0x00000080;
	}
	if (ustatus) {
		nvkm_snprintbf(msg, sizeof(msg), nv50_gr_trap_prop, ustatus);
		nvkm_error(subdev, "TRAP_PROP - TP %d - %08x [%s] - "
				   "Address %02x%08x\n",
			   tp, ustatus, msg, e14, e10);
	}
	nvkm_error(subdev, "TRAP_PROP - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
		 tp, e0c, e18, e1c, e20, e24);
}

static void
nv50_gr_mp_trap(struct nv50_gr *gr, int tpid, int display)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 units = nvkm_rd32(device, 0x1540);
	u32 addr, mp10, status, pc, oplow, ophigh;
	char msg[128];
	int i;
	int mps = 0;
	for (i = 0; i < 4; i++) {
		if (!(units & 1 << (i+24)))
			continue;
		if (nv_device(gr)->chipset < 0xa0)
			addr = 0x408200 + (tpid << 12) + (i << 7);
		else
			addr = 0x408100 + (tpid << 11) + (i << 7);
		mp10 = nvkm_rd32(device, addr + 0x10);
		status = nvkm_rd32(device, addr + 0x14);
		if (!status)
			continue;
		if (display) {
			nvkm_rd32(device, addr + 0x20);
			pc = nvkm_rd32(device, addr + 0x24);
			oplow = nvkm_rd32(device, addr + 0x70);
			ophigh = nvkm_rd32(device, addr + 0x74);
			nvkm_snprintbf(msg, sizeof(msg),
				       nv50_mp_exec_errors, status);
			nvkm_error(subdev, "TRAP_MP_EXEC - TP %d MP %d: "
					   "%08x [%s] at %06x warp %d, "
					   "opcode %08x %08x\n",
				   tpid, i, status, msg, pc & 0xffffff,
				   pc >> 24, oplow, ophigh);
		}
		nvkm_wr32(device, addr + 0x10, mp10);
		nvkm_wr32(device, addr + 0x14, 0);
		mps++;
	}
	if (!mps && display)
		nvkm_error(subdev, "TRAP_MP_EXEC - TP %d: "
				"No MPs claiming errors?\n", tpid);
}

static void
nv50_gr_tp_trap(struct nv50_gr *gr, int type, u32 ustatus_old,
		  u32 ustatus_new, int display, const char *name)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 units = nvkm_rd32(device, 0x1540);
	int tps = 0;
	int i, r;
	char msg[128];
	u32 ustatus_addr, ustatus;
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;
		if (nv_device(gr)->chipset < 0xa0)
			ustatus_addr = ustatus_old + (i << 12);
		else
			ustatus_addr = ustatus_new + (i << 11);
		ustatus = nvkm_rd32(device, ustatus_addr) & 0x7fffffff;
		if (!ustatus)
			continue;
		tps++;
		switch (type) {
		case 6: /* texture error... unknown for now */
			if (display) {
				nvkm_error(subdev, "magic set %d:\n", i);
				for (r = ustatus_addr + 4; r <= ustatus_addr + 0x10; r += 4)
					nvkm_error(subdev, "\t%08x: %08x\n", r,
						   nvkm_rd32(device, r));
				if (ustatus) {
					nvkm_snprintbf(msg, sizeof(msg),
						       nv50_tex_traps, ustatus);
					nvkm_error(subdev,
						   "%s - TP%d: %08x [%s]\n",
						   name, i, ustatus, msg);
					ustatus = 0;
				}
			}
			break;
		case 7: /* MP error */
			if (ustatus & 0x04030000) {
				nv50_gr_mp_trap(gr, i, display);
				ustatus &= ~0x04030000;
			}
			if (ustatus && display) {
				nvkm_snprintbf(msg, sizeof(msg),
					       nv50_mpc_traps, ustatus);
				nvkm_error(subdev, "%s - TP%d: %08x [%s]\n",
					   name, i, ustatus, msg);
				ustatus = 0;
			}
			break;
		case 8: /* PROP error */
			if (display)
				nv50_gr_prop_trap(
						gr, ustatus_addr, ustatus, i);
			ustatus = 0;
			break;
		}
		if (ustatus) {
			if (display)
				nvkm_error(subdev, "%s - TP%d: Unhandled ustatus %08x\n", name, i, ustatus);
		}
		nvkm_wr32(device, ustatus_addr, 0xc0000000);
	}

	if (!tps && display)
		nvkm_warn(subdev, "%s - No TPs claiming errors?\n", name);
}

static int
nv50_gr_trap_handler(struct nv50_gr *gr, u32 display,
		     int chid, u64 inst, const char *name)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 status = nvkm_rd32(device, 0x400108);
	u32 ustatus;
	char msg[128];

	if (!status && display) {
		nvkm_error(subdev, "TRAP: no units reporting traps?\n");
		return 1;
	}

	/* DISPATCH: Relays commands to other units and handles NOTIFY,
	 * COND, QUERY. If you get a trap from it, the command is still stuck
	 * in DISPATCH and you need to do something about it. */
	if (status & 0x001) {
		ustatus = nvkm_rd32(device, 0x400804) & 0x7fffffff;
		if (!ustatus && display) {
			nvkm_error(subdev, "TRAP_DISPATCH - no ustatus?\n");
		}

		nvkm_wr32(device, 0x400500, 0x00000000);

		/* Known to be triggered by screwed up NOTIFY and COND... */
		if (ustatus & 0x00000001) {
			u32 addr = nvkm_rd32(device, 0x400808);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 datal = nvkm_rd32(device, 0x40080c);
			u32 datah = nvkm_rd32(device, 0x400810);
			u32 class = nvkm_rd32(device, 0x400814);
			u32 r848 = nvkm_rd32(device, 0x400848);

			nvkm_error(subdev, "TRAP DISPATCH_FAULT\n");
			if (display && (addr & 0x80000000)) {
				nvkm_error(subdev,
					   "ch %d [%010llx %s] subc %d "
					   "class %04x mthd %04x data %08x%08x "
					   "400808 %08x 400848 %08x\n",
					   chid, inst, name, subc, class, mthd,
					   datah, datal, addr, r848);
			} else
			if (display) {
				nvkm_error(subdev, "no stuck command?\n");
			}

			nvkm_wr32(device, 0x400808, 0);
			nvkm_wr32(device, 0x4008e8, nvkm_rd32(device, 0x4008e8) & 3);
			nvkm_wr32(device, 0x400848, 0);
			ustatus &= ~0x00000001;
		}

		if (ustatus & 0x00000002) {
			u32 addr = nvkm_rd32(device, 0x40084c);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 data = nvkm_rd32(device, 0x40085c);
			u32 class = nvkm_rd32(device, 0x400814);

			nvkm_error(subdev, "TRAP DISPATCH_QUERY\n");
			if (display && (addr & 0x80000000)) {
				nvkm_error(subdev,
					   "ch %d [%010llx %s] subc %d "
					   "class %04x mthd %04x data %08x "
					   "40084c %08x\n", chid, inst, name,
					   subc, class, mthd, data, addr);
			} else
			if (display) {
				nvkm_error(subdev, "no stuck command?\n");
			}

			nvkm_wr32(device, 0x40084c, 0);
			ustatus &= ~0x00000002;
		}

		if (ustatus && display) {
			nvkm_error(subdev, "TRAP_DISPATCH "
					   "(unknown %08x)\n", ustatus);
		}

		nvkm_wr32(device, 0x400804, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x001);
		status &= ~0x001;
		if (!status)
			return 0;
	}

	/* M2MF: Memory to memory copy engine. */
	if (status & 0x002) {
		u32 ustatus = nvkm_rd32(device, 0x406800) & 0x7fffffff;
		if (display) {
			nvkm_snprintbf(msg, sizeof(msg),
				       nv50_gr_trap_m2mf, ustatus);
			nvkm_error(subdev, "TRAP_M2MF %08x [%s]\n",
				   ustatus, msg);
			nvkm_error(subdev, "TRAP_M2MF %08x %08x %08x %08x\n",
				   nvkm_rd32(device, 0x406804),
				   nvkm_rd32(device, 0x406808),
				   nvkm_rd32(device, 0x40680c),
				   nvkm_rd32(device, 0x406810));
		}

		/* No sane way found yet -- just reset the bugger. */
		nvkm_wr32(device, 0x400040, 2);
		nvkm_wr32(device, 0x400040, 0);
		nvkm_wr32(device, 0x406800, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x002);
		status &= ~0x002;
	}

	/* VFETCH: Fetches data from vertex buffers. */
	if (status & 0x004) {
		u32 ustatus = nvkm_rd32(device, 0x400c04) & 0x7fffffff;
		if (display) {
			nvkm_snprintbf(msg, sizeof(msg),
				       nv50_gr_trap_vfetch, ustatus);
			nvkm_error(subdev, "TRAP_VFETCH %08x [%s]\n",
				   ustatus, msg);
			nvkm_error(subdev, "TRAP_VFETCH %08x %08x %08x %08x\n",
				   nvkm_rd32(device, 0x400c00),
				   nvkm_rd32(device, 0x400c08),
				   nvkm_rd32(device, 0x400c0c),
				   nvkm_rd32(device, 0x400c10));
		}

		nvkm_wr32(device, 0x400c04, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x004);
		status &= ~0x004;
	}

	/* STRMOUT: DirectX streamout / OpenGL transform feedback. */
	if (status & 0x008) {
		ustatus = nvkm_rd32(device, 0x401800) & 0x7fffffff;
		if (display) {
			nvkm_snprintbf(msg, sizeof(msg),
				       nv50_gr_trap_strmout, ustatus);
			nvkm_error(subdev, "TRAP_STRMOUT %08x [%s]\n",
				   ustatus, msg);
			nvkm_error(subdev, "TRAP_STRMOUT %08x %08x %08x %08x\n",
				   nvkm_rd32(device, 0x401804),
				   nvkm_rd32(device, 0x401808),
				   nvkm_rd32(device, 0x40180c),
				   nvkm_rd32(device, 0x401810));
		}

		/* No sane way found yet -- just reset the bugger. */
		nvkm_wr32(device, 0x400040, 0x80);
		nvkm_wr32(device, 0x400040, 0);
		nvkm_wr32(device, 0x401800, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x008);
		status &= ~0x008;
	}

	/* CCACHE: Handles code and c[] caches and fills them. */
	if (status & 0x010) {
		ustatus = nvkm_rd32(device, 0x405018) & 0x7fffffff;
		if (display) {
			nvkm_snprintbf(msg, sizeof(msg),
				       nv50_gr_trap_ccache, ustatus);
			nvkm_error(subdev, "TRAP_CCACHE %08x [%s]\n",
				   ustatus, msg);
			nvkm_error(subdev, "TRAP_CCACHE %08x %08x %08x %08x "
					   "%08x %08x %08x\n",
				   nvkm_rd32(device, 0x405000),
				   nvkm_rd32(device, 0x405004),
				   nvkm_rd32(device, 0x405008),
				   nvkm_rd32(device, 0x40500c),
				   nvkm_rd32(device, 0x405010),
				   nvkm_rd32(device, 0x405014),
				   nvkm_rd32(device, 0x40501c));
		}

		nvkm_wr32(device, 0x405018, 0xc0000000);
		nvkm_wr32(device, 0x400108, 0x010);
		status &= ~0x010;
	}

	/* Unknown, not seen yet... 0x402000 is the only trap status reg
	 * remaining, so try to handle it anyway. Perhaps related to that
	 * unknown DMA slot on tesla? */
	if (status & 0x20) {
		ustatus = nvkm_rd32(device, 0x402000) & 0x7fffffff;
		if (display)
			nvkm_error(subdev, "TRAP_UNKC04 %08x\n", ustatus);
		nvkm_wr32(device, 0x402000, 0xc0000000);
		/* no status modifiction on purpose */
	}

	/* TEXTURE: CUDA texturing units */
	if (status & 0x040) {
		nv50_gr_tp_trap(gr, 6, 0x408900, 0x408600, display,
				    "TRAP_TEXTURE");
		nvkm_wr32(device, 0x400108, 0x040);
		status &= ~0x040;
	}

	/* MP: CUDA execution engines. */
	if (status & 0x080) {
		nv50_gr_tp_trap(gr, 7, 0x408314, 0x40831c, display,
				    "TRAP_MP");
		nvkm_wr32(device, 0x400108, 0x080);
		status &= ~0x080;
	}

	/* PROP:  Handles TP-initiated uncached memory accesses:
	 * l[], g[], stack, 2d surfaces, render targets. */
	if (status & 0x100) {
		nv50_gr_tp_trap(gr, 8, 0x408e08, 0x408708, display,
				    "TRAP_PROP");
		nvkm_wr32(device, 0x400108, 0x100);
		status &= ~0x100;
	}

	if (status) {
		if (display)
			nvkm_error(subdev, "TRAP: unknown %08x\n", status);
		nvkm_wr32(device, 0x400108, status);
	}

	return 1;
}

static void
nv50_gr_intr(struct nvkm_subdev *subdev)
{
	struct nv50_gr *gr = (void *)subdev;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo_chan *chan;
	u32 stat = nvkm_rd32(device, 0x400100);
	u32 inst = nvkm_rd32(device, 0x40032c) & 0x0fffffff;
	u32 addr = nvkm_rd32(device, 0x400704);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nvkm_rd32(device, 0x400708);
	u32 class = nvkm_rd32(device, 0x400814);
	u32 show = stat, show_bitfield = stat;
	const struct nvkm_enum *en;
	unsigned long flags;
	const char *name = "unknown";
	char msg[128];
	int chid = -1;

	chan = nvkm_fifo_chan_inst(device->fifo, (u64)inst << 12, &flags);
	if (chan)  {
		name = chan->object.client->name;
		chid = chan->chid;
	}

	if (show & 0x00100000) {
		u32 ecode = nvkm_rd32(device, 0x400110);
		en = nvkm_enum_find(nv50_data_error_names, ecode);
		nvkm_error(subdev, "DATA_ERROR %08x [%s]\n",
			   ecode, en ? en->name : "");
		show_bitfield &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		if (!nv50_gr_trap_handler(gr, show, chid, (u64)inst << 12, name))
			show &= ~0x00200000;
		show_bitfield &= ~0x00200000;
	}

	nvkm_wr32(device, 0x400100, stat);
	nvkm_wr32(device, 0x400500, 0x00010001);

	if (show) {
		show &= show_bitfield;
		nvkm_snprintbf(msg, sizeof(msg), nv50_gr_intr_name, show);
		nvkm_error(subdev, "%08x [%s] ch %d [%010llx %s] subc %d "
				   "class %04x mthd %04x data %08x\n",
			   stat, msg, chid, (u64)inst << 12, name,
			   subc, class, mthd, data);
	}

	if (nvkm_rd32(device, 0x400824) & (1 << 31))
		nvkm_wr32(device, 0x400824, nvkm_rd32(device, 0x400824) & ~(1 << 31));

	nvkm_fifo_chan_put(device->fifo, flags, &chan);
}

static const struct nv50_gr_func
nv50_gr = {
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x5097, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{}
	}
};

static const struct nv50_gr_func
g84_gr = {
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{ -1, -1, 0x8297, &nv50_gr_object },
		{}
	}
};

static const struct nv50_gr_func
gt200_gr = {
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{ -1, -1, 0x8397, &nv50_gr_object },
		{}
	}
};

static const struct nv50_gr_func
gt215_gr = {
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{ -1, -1, 0x8597, &nv50_gr_object },
		{ -1, -1, 0x85c0, &nv50_gr_object },
		{}
	}
};

static const struct nv50_gr_func
mcp89_gr = {
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{ -1, -1, 0x85c0, &nv50_gr_object },
		{ -1, -1, 0x8697, &nv50_gr_object },
		{}
	}
};

static const struct nvkm_gr_func
nv50_gr_ = {
	.chan_new = nv50_gr_chan_new,
	.object_get = nv50_gr_object_get,
};

static int
nv50_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nv50_gr *gr;
	int ret;

	ret = nvkm_gr_create(parent, engine, oclass, true, &gr);
	*pobject = nv_object(gr);
	if (ret)
		return ret;

	nv_subdev(gr)->unit = 0x00201000;
	nv_subdev(gr)->intr = nv50_gr_intr;

	gr->base.func = &nv50_gr_;
	gr->base.units = nv50_gr_units;

	switch (nv_device(gr)->chipset) {
	case 0x50:
		gr->func = &nv50_gr;
		break;
	case 0x84:
	case 0x86:
	case 0x92:
	case 0x94:
	case 0x96:
	case 0x98:
		gr->func = &g84_gr;
		break;
	case 0xa0:
	case 0xaa:
	case 0xac:
		gr->func = &gt200_gr;
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		gr->func = &gt215_gr;
		break;
	case 0xaf:
		gr->func = &mcp89_gr;
		break;
	}

	/* unfortunate hw bug workaround... */
	if (nv_device(gr)->chipset != 0x50 &&
	    nv_device(gr)->chipset != 0xac)
		nv_engine(gr)->tlb_flush = g84_gr_tlb_flush;

	spin_lock_init(&gr->lock);
	return 0;
}

static int
nv50_gr_init(struct nvkm_object *object)
{
	struct nv50_gr *gr = (void *)object;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int ret, units, i;

	ret = nvkm_gr_init(&gr->base);
	if (ret)
		return ret;

	/* NV_PGRAPH_DEBUG_3_HW_CTX_SWITCH_ENABLED */
	nvkm_wr32(device, 0x40008c, 0x00000004);

	/* reset/enable traps and interrupts */
	nvkm_wr32(device, 0x400804, 0xc0000000);
	nvkm_wr32(device, 0x406800, 0xc0000000);
	nvkm_wr32(device, 0x400c04, 0xc0000000);
	nvkm_wr32(device, 0x401800, 0xc0000000);
	nvkm_wr32(device, 0x405018, 0xc0000000);
	nvkm_wr32(device, 0x402000, 0xc0000000);

	units = nvkm_rd32(device, 0x001540);
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;

		if (nv_device(gr)->chipset < 0xa0) {
			nvkm_wr32(device, 0x408900 + (i << 12), 0xc0000000);
			nvkm_wr32(device, 0x408e08 + (i << 12), 0xc0000000);
			nvkm_wr32(device, 0x408314 + (i << 12), 0xc0000000);
		} else {
			nvkm_wr32(device, 0x408600 + (i << 11), 0xc0000000);
			nvkm_wr32(device, 0x408708 + (i << 11), 0xc0000000);
			nvkm_wr32(device, 0x40831c + (i << 11), 0xc0000000);
		}
	}

	nvkm_wr32(device, 0x400108, 0xffffffff);
	nvkm_wr32(device, 0x400138, 0xffffffff);
	nvkm_wr32(device, 0x400100, 0xffffffff);
	nvkm_wr32(device, 0x40013c, 0xffffffff);
	nvkm_wr32(device, 0x400500, 0x00010001);

	/* upload context program, initialise ctxctl defaults */
	ret = nv50_grctx_init(nv_device(gr), &gr->size);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x400824, 0x00000000);
	nvkm_wr32(device, 0x400828, 0x00000000);
	nvkm_wr32(device, 0x40082c, 0x00000000);
	nvkm_wr32(device, 0x400830, 0x00000000);
	nvkm_wr32(device, 0x40032c, 0x00000000);
	nvkm_wr32(device, 0x400330, 0x00000000);

	/* some unknown zcull magic */
	switch (nv_device(gr)->chipset & 0xf0) {
	case 0x50:
	case 0x80:
	case 0x90:
		nvkm_wr32(device, 0x402ca8, 0x00000800);
		break;
	case 0xa0:
	default:
		if (nv_device(gr)->chipset == 0xa0 ||
		    nv_device(gr)->chipset == 0xaa ||
		    nv_device(gr)->chipset == 0xac) {
			nvkm_wr32(device, 0x402ca8, 0x00000802);
		} else {
			nvkm_wr32(device, 0x402cc0, 0x00000000);
			nvkm_wr32(device, 0x402ca8, 0x00000002);
		}

		break;
	}

	/* zero out zcull regions */
	for (i = 0; i < 8; i++) {
		nvkm_wr32(device, 0x402c20 + (i * 0x10), 0x00000000);
		nvkm_wr32(device, 0x402c24 + (i * 0x10), 0x00000000);
		nvkm_wr32(device, 0x402c28 + (i * 0x10), 0x00000000);
		nvkm_wr32(device, 0x402c2c + (i * 0x10), 0x00000000);
	}
	return 0;
}

struct nvkm_oclass
nv50_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_gr_ctor,
		.dtor = _nvkm_gr_dtor,
		.init = nv50_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
