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

#include <core/os.h>
#include <core/class.h>
#include <core/client.h>
#include <core/handle.h>
#include <core/engctx.h>
#include <core/enum.h>

#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/timer.h>

#include <engine/fifo.h>
#include <engine/graph.h>

#include "nv50.h"

struct nv50_graph_priv {
	struct nouveau_graph base;
	spinlock_t lock;
	u32 size;
};

struct nv50_graph_chan {
	struct nouveau_graph_chan base;
};

static u64
nv50_graph_units(struct nouveau_graph *graph)
{
	struct nv50_graph_priv *priv = (void *)graph;

	return nv_rd32(priv, 0x1540);
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static int
nv50_graph_object_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
				    16, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	return 0;
}

static struct nouveau_ofuncs
nv50_graph_ofuncs = {
	.ctor = nv50_graph_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_oclass
nv50_graph_sclass[] = {
	{ 0x0030, &nv50_graph_ofuncs },
	{ 0x502d, &nv50_graph_ofuncs },
	{ 0x5039, &nv50_graph_ofuncs },
	{ 0x5097, &nv50_graph_ofuncs },
	{ 0x50c0, &nv50_graph_ofuncs },
	{}
};

static struct nouveau_oclass
nv84_graph_sclass[] = {
	{ 0x0030, &nv50_graph_ofuncs },
	{ 0x502d, &nv50_graph_ofuncs },
	{ 0x5039, &nv50_graph_ofuncs },
	{ 0x50c0, &nv50_graph_ofuncs },
	{ 0x8297, &nv50_graph_ofuncs },
	{}
};

static struct nouveau_oclass
nva0_graph_sclass[] = {
	{ 0x0030, &nv50_graph_ofuncs },
	{ 0x502d, &nv50_graph_ofuncs },
	{ 0x5039, &nv50_graph_ofuncs },
	{ 0x50c0, &nv50_graph_ofuncs },
	{ 0x8397, &nv50_graph_ofuncs },
	{}
};

static struct nouveau_oclass
nva3_graph_sclass[] = {
	{ 0x0030, &nv50_graph_ofuncs },
	{ 0x502d, &nv50_graph_ofuncs },
	{ 0x5039, &nv50_graph_ofuncs },
	{ 0x50c0, &nv50_graph_ofuncs },
	{ 0x8597, &nv50_graph_ofuncs },
	{ 0x85c0, &nv50_graph_ofuncs },
	{}
};

static struct nouveau_oclass
nvaf_graph_sclass[] = {
	{ 0x0030, &nv50_graph_ofuncs },
	{ 0x502d, &nv50_graph_ofuncs },
	{ 0x5039, &nv50_graph_ofuncs },
	{ 0x50c0, &nv50_graph_ofuncs },
	{ 0x85c0, &nv50_graph_ofuncs },
	{ 0x8697, &nv50_graph_ofuncs },
	{}
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv50_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv50_graph_priv *priv = (void *)engine;
	struct nv50_graph_chan *chan;
	int ret;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL,
					   priv->size, 0,
					   NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv50_grctx_fill(nv_device(priv), nv_gpuobj(chan));
	return 0;
}

static struct nouveau_oclass
nv50_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_graph_context_ctor,
		.dtor = _nouveau_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static const struct nouveau_bitfield nv50_pgraph_status[] = {
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
	{ 0x00000400, "CCACHE_UNK4" },
	{ 0x00000800, "STRMOUT_GSCHED_UNK5" },
	{ 0x00001000, "UNK14XX" },
	{ 0x00002000, "UNK24XX_CSCHED" },
	{ 0x00004000, "UNK1CXX" },
	{ 0x00008000, "CLIPID" },
	{ 0x00010000, "ZCULL" },
	{ 0x00020000, "ENG2D" },
	{ 0x00040000, "UNK34XX" },
	{ 0x00080000, "TPRAST" },
	{ 0x00100000, "TPROP" },
	{ 0x00200000, "TEX" },
	{ 0x00400000, "TPVP" },
	{ 0x00800000, "MP" },
	{ 0x01000000, "ROP" },
	{}
};

static const char *const nv50_pgraph_vstatus_0[] = {
	"VFETCH", "CCACHE", "UNK4", "UNK5", "GSCHED", "STRMOUT", "UNK14XX", NULL
};

static const char *const nv50_pgraph_vstatus_1[] = {
	"TPRAST", "TPROP", "TEXTURE", "TPVP", "MP", NULL
};

static const char *const nv50_pgraph_vstatus_2[] = {
	"UNK24XX", "CSCHED", "UNK1CXX", "CLIPID", "ZCULL", "ENG2D", "UNK34XX",
	"ROP", NULL
};

static void nouveau_pgraph_vstatus_print(struct nv50_graph_priv *priv, int r,
		const char *const units[], u32 status)
{
	int i;

	nv_error(priv, "PGRAPH_VSTATUS%d: 0x%08x", r, status);

	for (i = 0; units[i] && status; i++) {
		if ((status & 7) == 1)
			pr_cont(" %s", units[i]);
		status >>= 3;
	}
	if (status)
		pr_cont(" (invalid: 0x%x)", status);
	pr_cont("\n");
}

static int
nv84_graph_tlb_flush(struct nouveau_engine *engine)
{
	struct nouveau_timer *ptimer = nouveau_timer(engine);
	struct nv50_graph_priv *priv = (void *)engine;
	bool idle, timeout = false;
	unsigned long flags;
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&priv->lock, flags);
	nv_mask(priv, 0x400500, 0x00000001, 0x00000000);

	start = ptimer->read(ptimer);
	do {
		idle = true;

		for (tmp = nv_rd32(priv, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(priv, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(priv, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle &&
		 !(timeout = ptimer->read(ptimer) - start > 2000000000));

	if (timeout) {
		nv_error(priv, "PGRAPH TLB flush idle timeout fail\n");

		tmp = nv_rd32(priv, 0x400700);
		nv_error(priv, "PGRAPH_STATUS  : 0x%08x", tmp);
		nouveau_bitfield_print(nv50_pgraph_status, tmp);
		pr_cont("\n");

		nouveau_pgraph_vstatus_print(priv, 0, nv50_pgraph_vstatus_0,
				nv_rd32(priv, 0x400380));
		nouveau_pgraph_vstatus_print(priv, 1, nv50_pgraph_vstatus_1,
				nv_rd32(priv, 0x400384));
		nouveau_pgraph_vstatus_print(priv, 2, nv50_pgraph_vstatus_2,
				nv_rd32(priv, 0x400388));
	}


	nv_wr32(priv, 0x100c80, 0x00000001);
	if (!nv_wait(priv, 0x100c80, 0x00000001, 0x00000000))
		nv_error(priv, "vm flush timeout\n");
	nv_mask(priv, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&priv->lock, flags);
	return timeout ? -EBUSY : 0;
}

static const struct nouveau_bitfield nv50_mp_exec_errors[] = {
	{ 0x01, "STACK_UNDERFLOW" },
	{ 0x02, "STACK_MISMATCH" },
	{ 0x04, "QUADON_ACTIVE" },
	{ 0x08, "TIMEOUT" },
	{ 0x10, "INVALID_OPCODE" },
	{ 0x20, "PM_OVERFLOW" },
	{ 0x40, "BREAKPOINT" },
	{}
};

static const struct nouveau_bitfield nv50_mpc_traps[] = {
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

static const struct nouveau_bitfield nv50_graph_trap_m2mf[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "IN" },
	{ 0x00000004, "OUT" },
	{}
};

static const struct nouveau_bitfield nv50_graph_trap_vfetch[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static const struct nouveau_bitfield nv50_graph_trap_strmout[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static const struct nouveau_bitfield nv50_graph_trap_ccache[] = {
	{ 0x00000001, "FAULT" },
	{}
};

/* There must be a *lot* of these. Will take some time to gather them up. */
const struct nouveau_enum nv50_data_error_names[] = {
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

static const struct nouveau_bitfield nv50_graph_intr_name[] = {
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

static const struct nouveau_bitfield nv50_graph_trap_prop[] = {
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
nv50_priv_prop_trap(struct nv50_graph_priv *priv,
		    u32 ustatus_addr, u32 ustatus, u32 tp)
{
	u32 e0c = nv_rd32(priv, ustatus_addr + 0x04);
	u32 e10 = nv_rd32(priv, ustatus_addr + 0x08);
	u32 e14 = nv_rd32(priv, ustatus_addr + 0x0c);
	u32 e18 = nv_rd32(priv, ustatus_addr + 0x10);
	u32 e1c = nv_rd32(priv, ustatus_addr + 0x14);
	u32 e20 = nv_rd32(priv, ustatus_addr + 0x18);
	u32 e24 = nv_rd32(priv, ustatus_addr + 0x1c);

	/* CUDA memory: l[], g[] or stack. */
	if (ustatus & 0x00000080) {
		if (e18 & 0x80000000) {
			/* g[] read fault? */
			nv_error(priv, "TRAP_PROP - TP %d - CUDA_FAULT - Global read fault at address %02x%08x\n",
					 tp, e14, e10 | ((e18 >> 24) & 0x1f));
			e18 &= ~0x1f000000;
		} else if (e18 & 0xc) {
			/* g[] write fault? */
			nv_error(priv, "TRAP_PROP - TP %d - CUDA_FAULT - Global write fault at address %02x%08x\n",
				 tp, e14, e10 | ((e18 >> 7) & 0x1f));
			e18 &= ~0x00000f80;
		} else {
			nv_error(priv, "TRAP_PROP - TP %d - Unknown CUDA fault at address %02x%08x\n",
				 tp, e14, e10);
		}
		ustatus &= ~0x00000080;
	}
	if (ustatus) {
		nv_error(priv, "TRAP_PROP - TP %d -", tp);
		nouveau_bitfield_print(nv50_graph_trap_prop, ustatus);
		pr_cont(" - Address %02x%08x\n", e14, e10);
	}
	nv_error(priv, "TRAP_PROP - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
		 tp, e0c, e18, e1c, e20, e24);
}

static void
nv50_priv_mp_trap(struct nv50_graph_priv *priv, int tpid, int display)
{
	u32 units = nv_rd32(priv, 0x1540);
	u32 addr, mp10, status, pc, oplow, ophigh;
	int i;
	int mps = 0;
	for (i = 0; i < 4; i++) {
		if (!(units & 1 << (i+24)))
			continue;
		if (nv_device(priv)->chipset < 0xa0)
			addr = 0x408200 + (tpid << 12) + (i << 7);
		else
			addr = 0x408100 + (tpid << 11) + (i << 7);
		mp10 = nv_rd32(priv, addr + 0x10);
		status = nv_rd32(priv, addr + 0x14);
		if (!status)
			continue;
		if (display) {
			nv_rd32(priv, addr + 0x20);
			pc = nv_rd32(priv, addr + 0x24);
			oplow = nv_rd32(priv, addr + 0x70);
			ophigh = nv_rd32(priv, addr + 0x74);
			nv_error(priv, "TRAP_MP_EXEC - "
					"TP %d MP %d:", tpid, i);
			nouveau_bitfield_print(nv50_mp_exec_errors, status);
			pr_cont(" at %06x warp %d, opcode %08x %08x\n",
					pc&0xffffff, pc >> 24,
					oplow, ophigh);
		}
		nv_wr32(priv, addr + 0x10, mp10);
		nv_wr32(priv, addr + 0x14, 0);
		mps++;
	}
	if (!mps && display)
		nv_error(priv, "TRAP_MP_EXEC - TP %d: "
				"No MPs claiming errors?\n", tpid);
}

static void
nv50_priv_tp_trap(struct nv50_graph_priv *priv, int type, u32 ustatus_old,
		u32 ustatus_new, int display, const char *name)
{
	int tps = 0;
	u32 units = nv_rd32(priv, 0x1540);
	int i, r;
	u32 ustatus_addr, ustatus;
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;
		if (nv_device(priv)->chipset < 0xa0)
			ustatus_addr = ustatus_old + (i << 12);
		else
			ustatus_addr = ustatus_new + (i << 11);
		ustatus = nv_rd32(priv, ustatus_addr) & 0x7fffffff;
		if (!ustatus)
			continue;
		tps++;
		switch (type) {
		case 6: /* texture error... unknown for now */
			if (display) {
				nv_error(priv, "magic set %d:\n", i);
				for (r = ustatus_addr + 4; r <= ustatus_addr + 0x10; r += 4)
					nv_error(priv, "\t0x%08x: 0x%08x\n", r,
						nv_rd32(priv, r));
			}
			break;
		case 7: /* MP error */
			if (ustatus & 0x04030000) {
				nv50_priv_mp_trap(priv, i, display);
				ustatus &= ~0x04030000;
			}
			if (ustatus && display) {
				nv_error("%s - TP%d:", name, i);
				nouveau_bitfield_print(nv50_mpc_traps, ustatus);
				pr_cont("\n");
				ustatus = 0;
			}
			break;
		case 8: /* PROP error */
			if (display)
				nv50_priv_prop_trap(
						priv, ustatus_addr, ustatus, i);
			ustatus = 0;
			break;
		}
		if (ustatus) {
			if (display)
				nv_error(priv, "%s - TP%d: Unhandled ustatus 0x%08x\n", name, i, ustatus);
		}
		nv_wr32(priv, ustatus_addr, 0xc0000000);
	}

	if (!tps && display)
		nv_warn(priv, "%s - No TPs claiming errors?\n", name);
}

static int
nv50_graph_trap_handler(struct nv50_graph_priv *priv, u32 display,
			int chid, u64 inst, struct nouveau_object *engctx)
{
	u32 status = nv_rd32(priv, 0x400108);
	u32 ustatus;

	if (!status && display) {
		nv_error(priv, "TRAP: no units reporting traps?\n");
		return 1;
	}

	/* DISPATCH: Relays commands to other units and handles NOTIFY,
	 * COND, QUERY. If you get a trap from it, the command is still stuck
	 * in DISPATCH and you need to do something about it. */
	if (status & 0x001) {
		ustatus = nv_rd32(priv, 0x400804) & 0x7fffffff;
		if (!ustatus && display) {
			nv_error(priv, "TRAP_DISPATCH - no ustatus?\n");
		}

		nv_wr32(priv, 0x400500, 0x00000000);

		/* Known to be triggered by screwed up NOTIFY and COND... */
		if (ustatus & 0x00000001) {
			u32 addr = nv_rd32(priv, 0x400808);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 datal = nv_rd32(priv, 0x40080c);
			u32 datah = nv_rd32(priv, 0x400810);
			u32 class = nv_rd32(priv, 0x400814);
			u32 r848 = nv_rd32(priv, 0x400848);

			nv_error(priv, "TRAP DISPATCH_FAULT\n");
			if (display && (addr & 0x80000000)) {
				nv_error(priv,
					 "ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x%08x 400808 0x%08x 400848 0x%08x\n",
					 chid, inst,
					 nouveau_client_name(engctx), subc,
					 class, mthd, datah, datal, addr, r848);
			} else
			if (display) {
				nv_error(priv, "no stuck command?\n");
			}

			nv_wr32(priv, 0x400808, 0);
			nv_wr32(priv, 0x4008e8, nv_rd32(priv, 0x4008e8) & 3);
			nv_wr32(priv, 0x400848, 0);
			ustatus &= ~0x00000001;
		}

		if (ustatus & 0x00000002) {
			u32 addr = nv_rd32(priv, 0x40084c);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 data = nv_rd32(priv, 0x40085c);
			u32 class = nv_rd32(priv, 0x400814);

			nv_error(priv, "TRAP DISPATCH_QUERY\n");
			if (display && (addr & 0x80000000)) {
				nv_error(priv,
					 "ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x 40084c 0x%08x\n",
					 chid, inst,
					 nouveau_client_name(engctx), subc,
					 class, mthd, data, addr);
			} else
			if (display) {
				nv_error(priv, "no stuck command?\n");
			}

			nv_wr32(priv, 0x40084c, 0);
			ustatus &= ~0x00000002;
		}

		if (ustatus && display) {
			nv_error(priv, "TRAP_DISPATCH (unknown "
				      "0x%08x)\n", ustatus);
		}

		nv_wr32(priv, 0x400804, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x001);
		status &= ~0x001;
		if (!status)
			return 0;
	}

	/* M2MF: Memory to memory copy engine. */
	if (status & 0x002) {
		u32 ustatus = nv_rd32(priv, 0x406800) & 0x7fffffff;
		if (display) {
			nv_error(priv, "TRAP_M2MF");
			nouveau_bitfield_print(nv50_graph_trap_m2mf, ustatus);
			pr_cont("\n");
			nv_error(priv, "TRAP_M2MF %08x %08x %08x %08x\n",
				nv_rd32(priv, 0x406804), nv_rd32(priv, 0x406808),
				nv_rd32(priv, 0x40680c), nv_rd32(priv, 0x406810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(priv, 0x400040, 2);
		nv_wr32(priv, 0x400040, 0);
		nv_wr32(priv, 0x406800, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x002);
		status &= ~0x002;
	}

	/* VFETCH: Fetches data from vertex buffers. */
	if (status & 0x004) {
		u32 ustatus = nv_rd32(priv, 0x400c04) & 0x7fffffff;
		if (display) {
			nv_error(priv, "TRAP_VFETCH");
			nouveau_bitfield_print(nv50_graph_trap_vfetch, ustatus);
			pr_cont("\n");
			nv_error(priv, "TRAP_VFETCH %08x %08x %08x %08x\n",
				nv_rd32(priv, 0x400c00), nv_rd32(priv, 0x400c08),
				nv_rd32(priv, 0x400c0c), nv_rd32(priv, 0x400c10));
		}

		nv_wr32(priv, 0x400c04, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x004);
		status &= ~0x004;
	}

	/* STRMOUT: DirectX streamout / OpenGL transform feedback. */
	if (status & 0x008) {
		ustatus = nv_rd32(priv, 0x401800) & 0x7fffffff;
		if (display) {
			nv_error(priv, "TRAP_STRMOUT");
			nouveau_bitfield_print(nv50_graph_trap_strmout, ustatus);
			pr_cont("\n");
			nv_error(priv, "TRAP_STRMOUT %08x %08x %08x %08x\n",
				nv_rd32(priv, 0x401804), nv_rd32(priv, 0x401808),
				nv_rd32(priv, 0x40180c), nv_rd32(priv, 0x401810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(priv, 0x400040, 0x80);
		nv_wr32(priv, 0x400040, 0);
		nv_wr32(priv, 0x401800, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x008);
		status &= ~0x008;
	}

	/* CCACHE: Handles code and c[] caches and fills them. */
	if (status & 0x010) {
		ustatus = nv_rd32(priv, 0x405018) & 0x7fffffff;
		if (display) {
			nv_error(priv, "TRAP_CCACHE");
			nouveau_bitfield_print(nv50_graph_trap_ccache, ustatus);
			pr_cont("\n");
			nv_error(priv, "TRAP_CCACHE %08x %08x %08x %08x"
				     " %08x %08x %08x\n",
				nv_rd32(priv, 0x405000), nv_rd32(priv, 0x405004),
				nv_rd32(priv, 0x405008), nv_rd32(priv, 0x40500c),
				nv_rd32(priv, 0x405010), nv_rd32(priv, 0x405014),
				nv_rd32(priv, 0x40501c));

		}

		nv_wr32(priv, 0x405018, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x010);
		status &= ~0x010;
	}

	/* Unknown, not seen yet... 0x402000 is the only trap status reg
	 * remaining, so try to handle it anyway. Perhaps related to that
	 * unknown DMA slot on tesla? */
	if (status & 0x20) {
		ustatus = nv_rd32(priv, 0x402000) & 0x7fffffff;
		if (display)
			nv_error(priv, "TRAP_UNKC04 0x%08x\n", ustatus);
		nv_wr32(priv, 0x402000, 0xc0000000);
		/* no status modifiction on purpose */
	}

	/* TEXTURE: CUDA texturing units */
	if (status & 0x040) {
		nv50_priv_tp_trap(priv, 6, 0x408900, 0x408600, display,
				    "TRAP_TEXTURE");
		nv_wr32(priv, 0x400108, 0x040);
		status &= ~0x040;
	}

	/* MP: CUDA execution engines. */
	if (status & 0x080) {
		nv50_priv_tp_trap(priv, 7, 0x408314, 0x40831c, display,
				    "TRAP_MP");
		nv_wr32(priv, 0x400108, 0x080);
		status &= ~0x080;
	}

	/* PROP:  Handles TP-initiated uncached memory accesses:
	 * l[], g[], stack, 2d surfaces, render targets. */
	if (status & 0x100) {
		nv50_priv_tp_trap(priv, 8, 0x408e08, 0x408708, display,
				    "TRAP_PROP");
		nv_wr32(priv, 0x400108, 0x100);
		status &= ~0x100;
	}

	if (status) {
		if (display)
			nv_error(priv, "TRAP: unknown 0x%08x\n", status);
		nv_wr32(priv, 0x400108, status);
	}

	return 1;
}

static void
nv50_graph_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nouveau_handle *handle = NULL;
	struct nv50_graph_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, 0x400100);
	u32 inst = nv_rd32(priv, 0x40032c) & 0x0fffffff;
	u32 addr = nv_rd32(priv, 0x400704);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nv_rd32(priv, 0x400708);
	u32 class = nv_rd32(priv, 0x400814);
	u32 show = stat, show_bitfield = stat;
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000010) {
		handle = nouveau_handle_get_class(engctx, class);
		if (handle && !nv_call(handle->object, mthd, data))
			show &= ~0x00000010;
		nouveau_handle_put(handle);
	}

	if (show & 0x00100000) {
		u32 ecode = nv_rd32(priv, 0x400110);
		nv_error(priv, "DATA_ERROR ");
		nouveau_enum_print(nv50_data_error_names, ecode);
		pr_cont("\n");
		show_bitfield &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		if (!nv50_graph_trap_handler(priv, show, chid, (u64)inst << 12,
				engctx))
			show &= ~0x00200000;
		show_bitfield &= ~0x00200000;
	}

	nv_wr32(priv, 0x400100, stat);
	nv_wr32(priv, 0x400500, 0x00010001);

	if (show) {
		show &= show_bitfield;
		if (show) {
			nv_error(priv, "%s", "");
			nouveau_bitfield_print(nv50_graph_intr_name, show);
			pr_cont("\n");
		}
		nv_error(priv,
			 "ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, (u64)inst << 12, nouveau_client_name(engctx),
			 subc, class, mthd, data);
	}

	if (nv_rd32(priv, 0x400824) & (1 << 31))
		nv_wr32(priv, 0x400824, nv_rd32(priv, 0x400824) & ~(1 << 31));

	nouveau_engctx_put(engctx);
}

static int
nv50_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00201000;
	nv_subdev(priv)->intr = nv50_graph_intr;
	nv_engine(priv)->cclass = &nv50_graph_cclass;

	priv->base.units = nv50_graph_units;

	switch (nv_device(priv)->chipset) {
	case 0x50:
		nv_engine(priv)->sclass = nv50_graph_sclass;
		break;
	case 0x84:
	case 0x86:
	case 0x92:
	case 0x94:
	case 0x96:
	case 0x98:
		nv_engine(priv)->sclass = nv84_graph_sclass;
		break;
	case 0xa0:
	case 0xaa:
	case 0xac:
		nv_engine(priv)->sclass = nva0_graph_sclass;
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		nv_engine(priv)->sclass = nva3_graph_sclass;
		break;
	case 0xaf:
		nv_engine(priv)->sclass = nvaf_graph_sclass;
		break;

	};

	/* unfortunate hw bug workaround... */
	if (nv_device(priv)->chipset != 0x50 &&
	    nv_device(priv)->chipset != 0xac)
		nv_engine(priv)->tlb_flush = nv84_graph_tlb_flush;

	spin_lock_init(&priv->lock);
	return 0;
}

static int
nv50_graph_init(struct nouveau_object *object)
{
	struct nv50_graph_priv *priv = (void *)object;
	int ret, units, i;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	/* NV_PGRAPH_DEBUG_3_HW_CTX_SWITCH_ENABLED */
	nv_wr32(priv, 0x40008c, 0x00000004);

	/* reset/enable traps and interrupts */
	nv_wr32(priv, 0x400804, 0xc0000000);
	nv_wr32(priv, 0x406800, 0xc0000000);
	nv_wr32(priv, 0x400c04, 0xc0000000);
	nv_wr32(priv, 0x401800, 0xc0000000);
	nv_wr32(priv, 0x405018, 0xc0000000);
	nv_wr32(priv, 0x402000, 0xc0000000);

	units = nv_rd32(priv, 0x001540);
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;

		if (nv_device(priv)->chipset < 0xa0) {
			nv_wr32(priv, 0x408900 + (i << 12), 0xc0000000);
			nv_wr32(priv, 0x408e08 + (i << 12), 0xc0000000);
			nv_wr32(priv, 0x408314 + (i << 12), 0xc0000000);
		} else {
			nv_wr32(priv, 0x408600 + (i << 11), 0xc0000000);
			nv_wr32(priv, 0x408708 + (i << 11), 0xc0000000);
			nv_wr32(priv, 0x40831c + (i << 11), 0xc0000000);
		}
	}

	nv_wr32(priv, 0x400108, 0xffffffff);
	nv_wr32(priv, 0x400138, 0xffffffff);
	nv_wr32(priv, 0x400100, 0xffffffff);
	nv_wr32(priv, 0x40013c, 0xffffffff);
	nv_wr32(priv, 0x400500, 0x00010001);

	/* upload context program, initialise ctxctl defaults */
	ret = nv50_grctx_init(nv_device(priv), &priv->size);
	if (ret)
		return ret;

	nv_wr32(priv, 0x400824, 0x00000000);
	nv_wr32(priv, 0x400828, 0x00000000);
	nv_wr32(priv, 0x40082c, 0x00000000);
	nv_wr32(priv, 0x400830, 0x00000000);
	nv_wr32(priv, 0x40032c, 0x00000000);
	nv_wr32(priv, 0x400330, 0x00000000);

	/* some unknown zcull magic */
	switch (nv_device(priv)->chipset & 0xf0) {
	case 0x50:
	case 0x80:
	case 0x90:
		nv_wr32(priv, 0x402ca8, 0x00000800);
		break;
	case 0xa0:
	default:
		nv_wr32(priv, 0x402cc0, 0x00000000);
		if (nv_device(priv)->chipset == 0xa0 ||
		    nv_device(priv)->chipset == 0xaa ||
		    nv_device(priv)->chipset == 0xac) {
			nv_wr32(priv, 0x402ca8, 0x00000802);
		} else {
			nv_wr32(priv, 0x402cc0, 0x00000000);
			nv_wr32(priv, 0x402ca8, 0x00000002);
		}

		break;
	}

	/* zero out zcull regions */
	for (i = 0; i < 8; i++) {
		nv_wr32(priv, 0x402c20 + (i * 8), 0x00000000);
		nv_wr32(priv, 0x402c24 + (i * 8), 0x00000000);
		nv_wr32(priv, 0x402c28 + (i * 8), 0x00000000);
		nv_wr32(priv, 0x402c2c + (i * 8), 0x00000000);
	}
	return 0;
}

struct nouveau_oclass
nv50_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_graph_ctor,
		.dtor = _nouveau_graph_dtor,
		.init = nv50_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
