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
#include <engine/fifo.h>

#include <nvif/class.h>

u64
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
		nvkm_wo32(*pgpuobj, 0x00, object->oclass);
		nvkm_wo32(*pgpuobj, 0x04, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x08, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x0c, 0x00000000);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

const struct nvkm_object_func
nv50_gr_object = {
	.bind = nv50_gr_object_bind,
};

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

int
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
		if (device->chipset < 0xa0)
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
		if (device->chipset < 0xa0)
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

void
nv50_gr_intr(struct nvkm_gr *base)
{
	struct nv50_gr *gr = nv50_gr(base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_chan *chan;
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

	chan = nvkm_chan_get_inst(&gr->base.engine, (u64)inst << 12, &flags);
	if (chan)  {
		name = chan->name;
		chid = chan->id;
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

	nvkm_chan_put(&chan, flags);
}

int
nv50_gr_init(struct nvkm_gr *base)
{
	struct nv50_gr *gr = nv50_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int ret, units, i;

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

		if (device->chipset < 0xa0) {
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
	ret = nv50_grctx_init(device, &gr->size);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x400824, 0x00000000);
	nvkm_wr32(device, 0x400828, 0x00000000);
	nvkm_wr32(device, 0x40082c, 0x00000000);
	nvkm_wr32(device, 0x400830, 0x00000000);
	nvkm_wr32(device, 0x40032c, 0x00000000);
	nvkm_wr32(device, 0x400330, 0x00000000);

	/* some unknown zcull magic */
	switch (device->chipset & 0xf0) {
	case 0x50:
	case 0x80:
	case 0x90:
		nvkm_wr32(device, 0x402ca8, 0x00000800);
		break;
	case 0xa0:
	default:
		if (device->chipset == 0xa0 ||
		    device->chipset == 0xaa ||
		    device->chipset == 0xac) {
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

int
nv50_gr_new_(const struct nvkm_gr_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	struct nv50_gr *gr;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	spin_lock_init(&gr->lock);
	*pgr = &gr->base;

	return nvkm_gr_ctor(func, device, type, inst, true, &gr->base);
}

static const struct nvkm_gr_func
nv50_gr = {
	.init = nv50_gr_init,
	.intr = nv50_gr_intr,
	.chan_new = nv50_gr_chan_new,
	.units = nv50_gr_units,
	.sclass = {
		{ -1, -1, NV_NULL_CLASS, &nv50_gr_object },
		{ -1, -1, NV50_TWOD, &nv50_gr_object },
		{ -1, -1, NV50_MEMORY_TO_MEMORY_FORMAT, &nv50_gr_object },
		{ -1, -1, NV50_TESLA, &nv50_gr_object },
		{ -1, -1, NV50_COMPUTE, &nv50_gr_object },
		{}
	}
};

int
nv50_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return nv50_gr_new_(&nv50_gr, device, type, inst, pgr);
}
