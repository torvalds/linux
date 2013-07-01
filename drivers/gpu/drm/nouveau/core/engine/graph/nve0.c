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

#include "nvc0.h"
#include "fuc/hubnve0.fuc.h"
#include "fuc/gpcnve0.fuc.h"

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nve0_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa040, &nouveau_object_ofuncs },
	{ 0xa097, &nouveau_object_ofuncs },
	{ 0xa0c0, &nouveau_object_ofuncs },
	{}
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static struct nouveau_oclass
nve0_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_context_ctor,
		.dtor = nvc0_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = _nouveau_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
nve0_graph_ctxctl_isr(struct nvc0_graph_priv *priv)
{
	u32 ustat = nv_rd32(priv, 0x409c18);

	if (ustat & 0x00000001)
		nv_error(priv, "CTXCTRL ucode error\n");
	if (ustat & 0x00080000)
		nv_error(priv, "CTXCTRL watchdog timeout\n");
	if (ustat & ~0x00080001)
		nv_error(priv, "CTXCTRL 0x%08x\n", ustat);

	nvc0_graph_ctxctl_debug(priv);
	nv_wr32(priv, 0x409c20, ustat);
}

static const struct nouveau_enum nve0_mp_warp_error[] = {
	{ 0x00, "NO_ERROR" },
	{ 0x01, "STACK_MISMATCH" },
	{ 0x05, "MISALIGNED_PC" },
	{ 0x08, "MISALIGNED_GPR" },
	{ 0x09, "INVALID_OPCODE" },
	{ 0x0d, "GPR_OUT_OF_BOUNDS" },
	{ 0x0e, "MEM_OUT_OF_BOUNDS" },
	{ 0x0f, "UNALIGNED_MEM_ACCESS" },
	{ 0x11, "INVALID_PARAM" },
	{}
};

static const struct nouveau_bitfield nve0_mp_global_error[] = {
	{ 0x00000004, "MULTIPLE_WARP_ERRORS" },
	{ 0x00000008, "OUT_OF_STACK_SPACE" },
	{}
};

static const struct nouveau_enum nve0_gpc_rop_error[] = {
	{ 1, "RT_PITCH_OVERRUN" },
	{ 4, "RT_WIDTH_OVERRUN" },
	{ 5, "RT_HEIGHT_OVERRUN" },
	{ 7, "ZETA_STORAGE_TYPE_MISMATCH" },
	{ 8, "RT_STORAGE_TYPE_MISMATCH" },
	{ 10, "RT_LINEAR_MISMATCH" },
	{}
};

static const struct nouveau_enum nve0_sked_error[] = {
	{ 7, "CONSTANT_BUFFER_SIZE" },
	{ 9, "LOCAL_MEMORY_SIZE_POS" },
	{ 10, "LOCAL_MEMORY_SIZE_NEG" },
	{ 11, "WARP_CSTACK_SIZE" },
	{ 12, "TOTAL_TEMP_SIZE" },
	{ 13, "REGISTER_COUNT" },
	{ 18, "TOTAL_THREADS" },
	{ 20, "PROGRAM_OFFSET" },
	{ 21, "SHARED_MEMORY_SIZE" },
	{ 25, "SHARED_CONFIG_TOO_SMALL" },
	{ 26, "TOTAL_REGISTER_COUNT" },
	{}
};

static void
nve0_graph_mp_trap(struct nvc0_graph_priv *priv, int gpc, int tpc)
{
	u32 werr = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x648));
	u32 gerr = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x650));

	nv_error(priv, "GPC%i/TPC%i/MP trap:", gpc, tpc);
	nouveau_bitfield_print(nve0_mp_global_error, gerr);
	if (werr) {
		pr_cont(" ");
		nouveau_enum_print(nve0_mp_warp_error, werr & 0xffff);
	}
	pr_cont("\n");

	nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x648), 0x00000000);
	nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x650), gerr);
}

static void
nve0_graph_tpc_trap(struct nvc0_graph_priv *priv, int gpc, int tpc)
{
	u32 stat = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x508));

	if (stat & 0x1) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x224));
		nv_error(priv, "GPC%i/TPC%i/TEX trap: %08x\n",
			 gpc, tpc, trap);

		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
		stat &= ~0x1;
	}

	if (stat & 0x2) {
		nve0_graph_mp_trap(priv, gpc, tpc);
		stat &= ~0x2;
	}

	if (stat & 0x4) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x084));
		nv_error(priv, "GPC%i/TPC%i/POLY trap: %08x\n",
			 gpc, tpc, trap);

		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
		stat &= ~0x4;
	}

	if (stat & 0x8) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x48c));
		nv_error(priv, "GPC%i/TPC%i/L1C trap: %08x\n",
			 gpc, tpc, trap);

		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
		stat &= ~0x8;
	}

	if (stat) {
		nv_error(priv, "GPC%i/TPC%i: unknown stat %08x\n",
			 gpc, tpc, stat);
	}
}

static void
nve0_graph_gpc_trap(struct nvc0_graph_priv *priv)
{
	const u32 mask = nv_rd32(priv, 0x400118);
	int gpc;

	for (gpc = 0; gpc < 4; ++gpc) {
		u32 stat;
		int tpc;

		if (!(mask & (1 << gpc)))
			continue;
		stat = nv_rd32(priv, GPC_UNIT(gpc, 0x2c90));

		if (stat & 0x0001) {
			u32 trap[4];
			int i;

			trap[0] = nv_rd32(priv, GPC_UNIT(gpc, 0x0420));
			trap[1] = nv_rd32(priv, GPC_UNIT(gpc, 0x0434));
			trap[2] = nv_rd32(priv, GPC_UNIT(gpc, 0x0438));
			trap[3] = nv_rd32(priv, GPC_UNIT(gpc, 0x043c));

			nv_error(priv, "GPC%i/PROP trap:", gpc);
			for (i = 0; i <= 29; ++i) {
				if (!(trap[0] & (1 << i)))
					continue;
				pr_cont(" ");
				nouveau_enum_print(nve0_gpc_rop_error, i);
			}
			pr_cont("\n");

			nv_error(priv, "x = %u, y = %u, "
				 "format = %x, storage type = %x\n",
				 trap[1] & 0xffff,
				 trap[1] >> 16,
				 (trap[2] >> 8) & 0x3f,
				 trap[3] & 0xff);

			nv_wr32(priv, GPC_UNIT(gpc, 0x0420), 0xc0000000);
			stat &= ~0x0001;
		}

		if (stat & 0x0002) {
			u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x0900));
			nv_error(priv, "GPC%i/ZCULL trap: %08x\n", gpc,
				 trap);
			nv_wr32(priv, GPC_UNIT(gpc, 0x0900), 0xc0000000);
			stat &= ~0x0002;
		}

		if (stat & 0x0004) {
			u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x1028));
			nv_error(priv, "GPC%i/CCACHE trap: %08x\n", gpc,
				 trap);
			nv_wr32(priv, GPC_UNIT(gpc, 0x1028), 0xc0000000);
			stat &= ~0x0004;
		}

		if (stat & 0x0008) {
			u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x0824));
			nv_error(priv, "GPC%i/ESETUP trap %08x\n", gpc,
				 trap);
			nv_wr32(priv, GPC_UNIT(gpc, 0x0824), 0xc0000000);
			stat &= ~0x0008;
		}

		for (tpc = 0; tpc < 8; ++tpc) {
			if (stat & (1 << (16 + tpc)))
				nve0_graph_tpc_trap(priv, gpc, tpc);
		}
		stat &= ~0xff0000;

		if (stat) {
			nv_error(priv, "GPC%i: unknown stat %08x\n",
				 gpc, stat);
		}
	}
}


static void
nve0_graph_trap_isr(struct nvc0_graph_priv *priv, int chid, u64 inst,
		struct nouveau_object *engctx)
{
	u32 trap = nv_rd32(priv, 0x400108);
	int i;
	int rop;

	if (trap & 0x00000001) {
		u32 stat = nv_rd32(priv, 0x404000);
		nv_error(priv, "DISPATCH ch %d [0x%010llx %s] 0x%08x\n",
			 chid, inst, nouveau_client_name(engctx), stat);
		nv_wr32(priv, 0x404000, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000001);
		trap &= ~0x00000001;
	}

	if (trap & 0x00000010) {
		u32 stat = nv_rd32(priv, 0x405840);
		nv_error(priv, "SHADER ch %d [0x%010llx %s] 0x%08x\n",
			 chid, inst, nouveau_client_name(engctx), stat);
		nv_wr32(priv, 0x405840, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000010);
		trap &= ~0x00000010;
	}

	if (trap & 0x00000100) {
		u32 stat = nv_rd32(priv, 0x407020);
		nv_error(priv, "SKED ch %d [0x%010llx %s]:",
			 chid, inst, nouveau_client_name(engctx));

		for (i = 0; i <= 29; ++i) {
			if (!(stat & (1 << i)))
				continue;
			pr_cont(" ");
			nouveau_enum_print(nve0_sked_error, i);
		}
		pr_cont("\n");

		if (stat & 0x3fffffff)
			nv_wr32(priv, 0x407020, 0x40000000);
		nv_wr32(priv, 0x400108, 0x00000100);
		trap &= ~0x00000100;
	}

	if (trap & 0x01000000) {
		nv_error(priv, "GPC ch %d [0x%010llx %s]:\n",
			 chid, inst, nouveau_client_name(engctx));
		nve0_graph_gpc_trap(priv);
		trap &= ~0x01000000;
	}

	if (trap & 0x02000000) {
		for (rop = 0; rop < priv->rop_nr; rop++) {
			u32 statz = nv_rd32(priv, ROP_UNIT(rop, 0x070));
			u32 statc = nv_rd32(priv, ROP_UNIT(rop, 0x144));
			nv_error(priv,
				 "ROP%d ch %d [0x%010llx %s] 0x%08x 0x%08x\n",
				 rop, chid, inst, nouveau_client_name(engctx),
				 statz, statc);
			nv_wr32(priv, ROP_UNIT(rop, 0x070), 0xc0000000);
			nv_wr32(priv, ROP_UNIT(rop, 0x144), 0xc0000000);
		}
		nv_wr32(priv, 0x400108, 0x02000000);
		trap &= ~0x02000000;
	}

	if (trap) {
		nv_error(priv, "TRAP ch %d [0x%010llx %s] 0x%08x\n",
			 chid, inst, nouveau_client_name(engctx), trap);
		nv_wr32(priv, 0x400108, trap);
	}
}

static void
nve0_graph_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nouveau_handle *handle;
	struct nvc0_graph_priv *priv = (void *)subdev;
	u64 inst = nv_rd32(priv, 0x409b00) & 0x0fffffff;
	u32 stat = nv_rd32(priv, 0x400100);
	u32 addr = nv_rd32(priv, 0x400704);
	u32 mthd = (addr & 0x00003ffc);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 data = nv_rd32(priv, 0x400708);
	u32 code = nv_rd32(priv, 0x400110);
	u32 class = nv_rd32(priv, 0x404200 + (subc * 4));
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000010) {
		handle = nouveau_handle_get_class(engctx, class);
		if (!handle || nv_call(handle->object, mthd, data)) {
			nv_error(priv,
				 "ILLEGAL_MTHD ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
				 chid, inst, nouveau_client_name(engctx), subc,
				 class, mthd, data);
		}
		nouveau_handle_put(handle);
		nv_wr32(priv, 0x400100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000020) {
		nv_error(priv,
			 "ILLEGAL_CLASS ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, inst, nouveau_client_name(engctx), subc, class,
			 mthd, data);
		nv_wr32(priv, 0x400100, 0x00000020);
		stat &= ~0x00000020;
	}

	if (stat & 0x00100000) {
		nv_error(priv, "DATA_ERROR [");
		nouveau_enum_print(nv50_data_error_names, code);
		pr_cont("] ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			chid, inst, nouveau_client_name(engctx), subc, class,
			mthd, data);
		nv_wr32(priv, 0x400100, 0x00100000);
		stat &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		nve0_graph_trap_isr(priv, chid, inst, engctx);
		nv_wr32(priv, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		nve0_graph_ctxctl_isr(priv);
		nv_wr32(priv, 0x400100, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		nv_error(priv, "unknown stat 0x%08x\n", stat);
		nv_wr32(priv, 0x400100, stat);
	}

	nv_wr32(priv, 0x400500, 0x00010001);
	nouveau_engctx_put(engctx);
}

static int
nve0_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nvc0_graph_priv *priv;
	int ret, i;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x18001000;
	nv_subdev(priv)->intr = nve0_graph_intr;
	nv_engine(priv)->cclass = &nve0_graph_cclass;
	nv_engine(priv)->sclass = nve0_graph_sclass;

	priv->base.units = nvc0_graph_units;

	if (nouveau_boolopt(device->cfgopt, "NvGrUseFW", false)) {
		nv_info(priv, "using external firmware\n");
		if (nvc0_graph_ctor_fw(priv, "fuc409c", &priv->fuc409c) ||
		    nvc0_graph_ctor_fw(priv, "fuc409d", &priv->fuc409d) ||
		    nvc0_graph_ctor_fw(priv, "fuc41ac", &priv->fuc41ac) ||
		    nvc0_graph_ctor_fw(priv, "fuc41ad", &priv->fuc41ad))
			return -EINVAL;
		priv->firmware = true;
	}

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 256, 0,
				&priv->unk4188b4);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 256, 0,
				&priv->unk4188b8);
	if (ret)
		return ret;

	for (i = 0; i < 0x1000; i += 4) {
		nv_wo32(priv->unk4188b4, i, 0x00000010);
		nv_wo32(priv->unk4188b8, i, 0x00000010);
	}

	priv->gpc_nr =  nv_rd32(priv, 0x409604) & 0x0000001f;
	priv->rop_nr = (nv_rd32(priv, 0x409604) & 0x001f0000) >> 16;
	for (i = 0; i < priv->gpc_nr; i++) {
		priv->tpc_nr[i] = nv_rd32(priv, GPC_UNIT(i, 0x2608));
		priv->tpc_total += priv->tpc_nr[i];
	}

	switch (nv_device(priv)->chipset) {
	case 0xe4:
		if (priv->tpc_total == 8)
			priv->magic_not_rop_nr = 3;
		else
		if (priv->tpc_total == 7)
			priv->magic_not_rop_nr = 1;
		break;
	case 0xe7:
	case 0xe6:
		priv->magic_not_rop_nr = 1;
		break;
	case 0xf0:
	default:
		break;
	}

	return 0;
}

static void
nve0_graph_init_obj418880(struct nvc0_graph_priv *priv)
{
	int i;

	nv_wr32(priv, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x08a4), 0x00000000);
	for (i = 0; i < 4; i++)
		nv_wr32(priv, GPC_BCAST(0x0888) + (i * 4), 0x00000000);
	nv_wr32(priv, GPC_BCAST(0x08b4), priv->unk4188b4->addr >> 8);
	nv_wr32(priv, GPC_BCAST(0x08b8), priv->unk4188b8->addr >> 8);
}

static void
nve0_graph_init_regs(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x400080, 0x003083c2);
	nv_wr32(priv, 0x400088, 0x0001ffe7);
	nv_wr32(priv, 0x40008c, 0x00000000);
	nv_wr32(priv, 0x400090, 0x00000030);
	nv_wr32(priv, 0x40013c, 0x003901f7);
	nv_wr32(priv, 0x400140, 0x00000100);
	nv_wr32(priv, 0x400144, 0x00000000);
	nv_wr32(priv, 0x400148, 0x00000110);
	nv_wr32(priv, 0x400138, 0x00000000);
	nv_wr32(priv, 0x400130, 0x00000000);
	nv_wr32(priv, 0x400134, 0x00000000);
	nv_wr32(priv, 0x400124, 0x00000002);
}

static void
nve0_graph_init_unk40xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40415c, 0x00000000);
	nv_wr32(priv, 0x404170, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x4041b4, 0x00000000);
		break;
	default:
		break;
	}
}

static void
nve0_graph_init_unk44xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x404488, 0x00000000);
	nv_wr32(priv, 0x40448c, 0x00000000);
}

static void
nve0_graph_init_unk78xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x407808, 0x00000000);
}

static void
nve0_graph_init_unk60xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x406024, 0x00000000);
}

static void
nve0_graph_init_unk64xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x4064f0, 0x00000000);
	nv_wr32(priv, 0x4064f4, 0x00000000);
	nv_wr32(priv, 0x4064f8, 0x00000000);
}

static void
nve0_graph_init_unk58xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x405844, 0x00ffffff);
	nv_wr32(priv, 0x405850, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x405900, 0x0000ff00);
		break;
	default:
		nv_wr32(priv, 0x405900, 0x0000ff34);
		break;
	}
	nv_wr32(priv, 0x405908, 0x00000000);
	nv_wr32(priv, 0x405928, 0x00000000);
	nv_wr32(priv, 0x40592c, 0x00000000);
}

static void
nve0_graph_init_unk80xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40803c, 0x00000000);
}

static void
nve0_graph_init_unk70xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x407010, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x407040, 0x80440424);
		nv_wr32(priv, 0x407048, 0x0000000a);
		break;
	default:
		break;
	}
}

static void
nve0_graph_init_unk5bxx(struct nvc0_graph_priv *priv)
{
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x505b44, 0x00000000);
		break;
	default:
		break;
	}
	nv_wr32(priv, 0x405b50, 0x00000000);
}

static void
nve0_graph_init_gpc(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x418408, 0x00000000);
	nv_wr32(priv, 0x4184a0, 0x00000000);
	nv_wr32(priv, 0x4184a4, 0x00000000);
	nv_wr32(priv, 0x4184a8, 0x00000000);
	nv_wr32(priv, 0x418604, 0x00000000);
	nv_wr32(priv, 0x418680, 0x00000000);
	nv_wr32(priv, 0x418714, 0x00000000);
	nv_wr32(priv, 0x418384, 0x00000000);
	nv_wr32(priv, 0x418814, 0x00000000);
	nv_wr32(priv, 0x418818, 0x00000000);
	nv_wr32(priv, 0x41881c, 0x00000000);
	nv_wr32(priv, 0x418b04, 0x00000000);
	nv_wr32(priv, 0x4188c8, 0x00000000);
	nv_wr32(priv, 0x4188cc, 0x00000000);
	nv_wr32(priv, 0x4188d0, 0x00010000);
	nv_wr32(priv, 0x4188d4, 0x00000001);
	nv_wr32(priv, 0x418910, 0x00010001);
	nv_wr32(priv, 0x418914, 0x00000301);
	nv_wr32(priv, 0x418918, 0x00800000);
	nv_wr32(priv, 0x418980, 0x77777770);
	nv_wr32(priv, 0x418984, 0x77777777);
	nv_wr32(priv, 0x418988, 0x77777777);
	nv_wr32(priv, 0x41898c, 0x77777777);
	nv_wr32(priv, 0x418c04, 0x00000000);
	nv_wr32(priv, 0x418c64, 0x00000000);
	nv_wr32(priv, 0x418c68, 0x00000000);
	nv_wr32(priv, 0x418c88, 0x00000000);
	nv_wr32(priv, 0x418cb4, 0x00000000);
	nv_wr32(priv, 0x418cb8, 0x00000000);
	nv_wr32(priv, 0x418d00, 0x00000000);
	nv_wr32(priv, 0x418d28, 0x00000000);
	nv_wr32(priv, 0x418d2c, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x418f00, 0x00000400);
		break;
	default:
		nv_wr32(priv, 0x418f00, 0x00000000);
		break;
	}
	nv_wr32(priv, 0x418f08, 0x00000000);
	nv_wr32(priv, 0x418f20, 0x00000000);
	nv_wr32(priv, 0x418f24, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x418e00, 0x00000000);
		break;
	default:
		nv_wr32(priv, 0x418e00, 0x00000060);
		break;
	}
	nv_wr32(priv, 0x418e08, 0x00000000);
	nv_wr32(priv, 0x418e1c, 0x00000000);
	nv_wr32(priv, 0x418e20, 0x00000000);
	nv_wr32(priv, 0x41900c, 0x00000000);
	nv_wr32(priv, 0x419018, 0x00000000);
}

static void
nve0_graph_init_tpc(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x419d0c, 0x00000000);
	nv_wr32(priv, 0x419d10, 0x00000014);
	nv_wr32(priv, 0x419ab0, 0x00000000);
	nv_wr32(priv, 0x419ac8, 0x00000000);
	nv_wr32(priv, 0x419ab8, 0x000000e7);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x419aec, 0x00000000);
		break;
	default:
		break;
	}
	nv_wr32(priv, 0x419abc, 0x00000000);
	nv_wr32(priv, 0x419ac0, 0x00000000);
	nv_wr32(priv, 0x419ab4, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x419aa8, 0x00000000);
		nv_wr32(priv, 0x419aac, 0x00000000);
		break;
	default:
		break;
	}
	nv_wr32(priv, 0x41980c, 0x00000010);
	nv_wr32(priv, 0x419844, 0x00000000);
	nv_wr32(priv, 0x419850, 0x00000004);
	nv_wr32(priv, 0x419854, 0x00000000);
	nv_wr32(priv, 0x419858, 0x00000000);
	nv_wr32(priv, 0x419c98, 0x00000000);
	nv_wr32(priv, 0x419ca8, 0x00000000);
	nv_wr32(priv, 0x419cb0, 0x01000000);
	nv_wr32(priv, 0x419cb4, 0x00000000);
	nv_wr32(priv, 0x419cb8, 0x00b08bea);
	nv_wr32(priv, 0x419c84, 0x00010384);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x419cbc, 0x281b3646);
		break;
	default:
		nv_wr32(priv, 0x419cbc, 0x28137646);
		break;
	}
	nv_wr32(priv, 0x419cc0, 0x00000000);
	nv_wr32(priv, 0x419cc4, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x419c80, 0x00020230);
		nv_wr32(priv, 0x419ccc, 0x00000000);
		nv_wr32(priv, 0x419cd0, 0x00000000);
		nv_wr32(priv, 0x419c0c, 0x00000000);
		nv_wr32(priv, 0x419e00, 0x00000080);
		break;
	default:
		nv_wr32(priv, 0x419c80, 0x00020232);
		nv_wr32(priv, 0x419c0c, 0x00000000);
		nv_wr32(priv, 0x419e00, 0x00000000);
		break;
	}
	nv_wr32(priv, 0x419ea0, 0x00000000);
	nv_wr32(priv, 0x419ee4, 0x00000000);
	nv_wr32(priv, 0x419ea4, 0x00000100);
	nv_wr32(priv, 0x419ea8, 0x00000000);
	nv_wr32(priv, 0x419eb4, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		break;
	default:
		nv_wr32(priv, 0x419eb8, 0x00000000);
		break;
	}
	nv_wr32(priv, 0x419ebc, 0x00000000);
	nv_wr32(priv, 0x419ec0, 0x00000000);
	nv_wr32(priv, 0x419edc, 0x00000000);
	nv_wr32(priv, 0x419f00, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xf0:
		nv_wr32(priv, 0x419ed0, 0x00003234);
		nv_wr32(priv, 0x419f74, 0x00015555);
		nv_wr32(priv, 0x419f80, 0x00000000);
		nv_wr32(priv, 0x419f84, 0x00000000);
		nv_wr32(priv, 0x419f88, 0x00000000);
		nv_wr32(priv, 0x419f8c, 0x00000000);
		break;
	default:
		nv_wr32(priv, 0x419f74, 0x00000555);
		break;
	}
}

static void
nve0_graph_init_tpcunk(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x41be04, 0x00000000);
	nv_wr32(priv, 0x41be08, 0x00000004);
	nv_wr32(priv, 0x41be0c, 0x00000000);
	nv_wr32(priv, 0x41be10, 0x003b8bc7);
	nv_wr32(priv, 0x41be14, 0x00000000);
	nv_wr32(priv, 0x41be18, 0x00000000);
	nv_wr32(priv, 0x41bfd4, 0x00800000);
	nv_wr32(priv, 0x41bfdc, 0x00000000);
	nv_wr32(priv, 0x41bff8, 0x00000000);
	nv_wr32(priv, 0x41bffc, 0x00000000);
	nv_wr32(priv, 0x41becc, 0x00000000);
	nv_wr32(priv, 0x41bee8, 0x00000000);
	nv_wr32(priv, 0x41beec, 0x00000000);
}

static void
nve0_graph_init_unk88xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40880c, 0x00000000);
	nv_wr32(priv, 0x408850, 0x00000004);
	nv_wr32(priv, 0x408910, 0x00000000);
	nv_wr32(priv, 0x408914, 0x00000000);
	nv_wr32(priv, 0x408918, 0x00000000);
	nv_wr32(priv, 0x40891c, 0x00000000);
	nv_wr32(priv, 0x408920, 0x00000000);
	nv_wr32(priv, 0x408924, 0x00000000);
	nv_wr32(priv, 0x408928, 0x00000000);
	nv_wr32(priv, 0x40892c, 0x00000000);
	nv_wr32(priv, 0x408930, 0x00000000);
	nv_wr32(priv, 0x408950, 0x00000000);
	nv_wr32(priv, 0x408954, 0x0000ffff);
	nv_wr32(priv, 0x408958, 0x00000034);
	nv_wr32(priv, 0x408984, 0x00000000);
	nv_wr32(priv, 0x408988, 0x08040201);
	nv_wr32(priv, 0x40898c, 0x80402010);
}

static void
nve0_graph_init_units(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x409ffc, 0x00000000);
	nv_wr32(priv, 0x409c14, 0x00003e3e);
	switch (nv_device(priv)->chipset) {
	case 0xe4:
	case 0xe7:
	case 0xe6:
		nv_wr32(priv, 0x409c24, 0x000f0001);
		break;
	case 0xf0:
		nv_wr32(priv, 0x409c24, 0x000f0000);
		break;
	}

	nv_wr32(priv, 0x404000, 0xc0000000);
	nv_wr32(priv, 0x404600, 0xc0000000);
	nv_wr32(priv, 0x408030, 0xc0000000);
	nv_wr32(priv, 0x404490, 0xc0000000);
	nv_wr32(priv, 0x406018, 0xc0000000);
	nv_wr32(priv, 0x407020, 0x40000000);
	nv_wr32(priv, 0x405840, 0xc0000000);
	nv_wr32(priv, 0x405844, 0x00ffffff);

	nv_mask(priv, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(priv, 0x419eb4, 0x00001000, 0x00001000);

}

static void
nve0_graph_init_gpc_0(struct nvc0_graph_priv *priv)
{
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tpc_total);
	u32 data[TPC_MAX / 8];
	u8  tpcnr[GPC_MAX];
	int i, gpc, tpc;

	nv_wr32(priv, GPC_UNIT(0, 0x3018), 0x00000001);

	memset(data, 0x00, sizeof(data));
	memcpy(tpcnr, priv->tpc_nr, sizeof(priv->tpc_nr));
	for (i = 0, gpc = -1; i < priv->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % priv->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = priv->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nv_wr32(priv, GPC_BCAST(0x0980), data[0]);
	nv_wr32(priv, GPC_BCAST(0x0984), data[1]);
	nv_wr32(priv, GPC_BCAST(0x0988), data[2]);
	nv_wr32(priv, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(priv, GPC_UNIT(gpc, 0x0914), priv->magic_not_rop_nr << 8 |
						  priv->tpc_nr[gpc]);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0910), 0x00040000 | priv->tpc_total);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nv_wr32(priv, GPC_BCAST(0x3fd4), magicgpc918);
	nv_wr32(priv, GPC_BCAST(0x08ac), nv_rd32(priv, 0x100800));
}

static void
nve0_graph_init_gpc_1(struct nvc0_graph_priv *priv)
{
	int gpc, tpc;

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(priv, GPC_UNIT(gpc, 0x3038), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < priv->tpc_nr[gpc]; tpc++) {
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x644), 0x001ffffe);
			nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x64c), 0x0000000f);
		}
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}
}

static void
nve0_graph_init_rop(struct nvc0_graph_priv *priv)
{
	int rop;

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(priv, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(priv, ROP_UNIT(rop, 0x208), 0xffffffff);
	}
}

static int
nve0_graph_init_ctxctl(struct nvc0_graph_priv *priv)
{
	u32 r000260;
	int i;

	if (priv->firmware) {
		/* load fuc microcode */
		r000260 = nv_mask(priv, 0x000260, 0x00000001, 0x00000000);
		nvc0_graph_init_fw(priv, 0x409000, &priv->fuc409c, &priv->fuc409d);
		nvc0_graph_init_fw(priv, 0x41a000, &priv->fuc41ac, &priv->fuc41ad);
		nv_wr32(priv, 0x000260, r000260);

		/* start both of them running */
		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x41a10c, 0x00000000);
		nv_wr32(priv, 0x40910c, 0x00000000);
		nv_wr32(priv, 0x41a100, 0x00000002);
		nv_wr32(priv, 0x409100, 0x00000002);
		if (!nv_wait(priv, 0x409800, 0x00000001, 0x00000001))
			nv_error(priv, "0x409800 wait failed\n");

		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x409500, 0x7fffffff);
		nv_wr32(priv, 0x409504, 0x00000021);

		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x409500, 0x00000000);
		nv_wr32(priv, 0x409504, 0x00000010);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x10 timeout\n");
			return -EBUSY;
		}
		priv->size = nv_rd32(priv, 0x409800);

		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x409500, 0x00000000);
		nv_wr32(priv, 0x409504, 0x00000016);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x16 timeout\n");
			return -EBUSY;
		}

		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x409500, 0x00000000);
		nv_wr32(priv, 0x409504, 0x00000025);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x25 timeout\n");
			return -EBUSY;
		}

		nv_wr32(priv, 0x409800, 0x00000000);
		nv_wr32(priv, 0x409500, 0x00000001);
		nv_wr32(priv, 0x409504, 0x00000030);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x30 timeout\n");
			return -EBUSY;
		}

		nv_wr32(priv, 0x409810, 0xb00095c8);
		nv_wr32(priv, 0x409800, 0x00000000);
		nv_wr32(priv, 0x409500, 0x00000001);
		nv_wr32(priv, 0x409504, 0x00000031);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x31 timeout\n");
			return -EBUSY;
		}

		nv_wr32(priv, 0x409810, 0x00080420);
		nv_wr32(priv, 0x409800, 0x00000000);
		nv_wr32(priv, 0x409500, 0x00000001);
		nv_wr32(priv, 0x409504, 0x00000032);
		if (!nv_wait_ne(priv, 0x409800, 0xffffffff, 0x00000000)) {
			nv_error(priv, "fuc09 req 0x32 timeout\n");
			return -EBUSY;
		}

		nv_wr32(priv, 0x409614, 0x00000070);
		nv_wr32(priv, 0x409614, 0x00000770);
		nv_wr32(priv, 0x40802c, 0x00000001);

		if (priv->data == NULL) {
			int ret = nve0_grctx_generate(priv);
			if (ret) {
				nv_error(priv, "failed to construct context\n");
				return ret;
			}
		}

		return 0;
	}

	/* load HUB microcode */
	r000260 = nv_mask(priv, 0x000260, 0x00000001, 0x00000000);
	nv_wr32(priv, 0x4091c0, 0x01000000);
	for (i = 0; i < sizeof(nve0_grhub_data) / 4; i++)
		nv_wr32(priv, 0x4091c4, nve0_grhub_data[i]);

	nv_wr32(priv, 0x409180, 0x01000000);
	for (i = 0; i < sizeof(nve0_grhub_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x409188, i >> 6);
		nv_wr32(priv, 0x409184, nve0_grhub_code[i]);
	}

	/* load GPC microcode */
	nv_wr32(priv, 0x41a1c0, 0x01000000);
	for (i = 0; i < sizeof(nve0_grgpc_data) / 4; i++)
		nv_wr32(priv, 0x41a1c4, nve0_grgpc_data[i]);

	nv_wr32(priv, 0x41a180, 0x01000000);
	for (i = 0; i < sizeof(nve0_grgpc_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x41a188, i >> 6);
		nv_wr32(priv, 0x41a184, nve0_grgpc_code[i]);
	}
	nv_wr32(priv, 0x000260, r000260);

	/* start HUB ucode running, it'll init the GPCs */
	nv_wr32(priv, 0x409800, nv_device(priv)->chipset);
	nv_wr32(priv, 0x40910c, 0x00000000);
	nv_wr32(priv, 0x409100, 0x00000002);
	if (!nv_wait(priv, 0x409800, 0x80000000, 0x80000000)) {
		nv_error(priv, "HUB_INIT timed out\n");
		nvc0_graph_ctxctl_debug(priv);
		return -EBUSY;
	}

	priv->size = nv_rd32(priv, 0x409804);
	if (priv->data == NULL) {
		int ret = nve0_grctx_generate(priv);
		if (ret) {
			nv_error(priv, "failed to construct context\n");
			return ret;
		}
	}

	return 0;
}

static int
nve0_graph_init(struct nouveau_object *object)
{
	struct nvc0_graph_priv *priv = (void *)object;
	int ret;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nve0_graph_init_obj418880(priv);
	nve0_graph_init_regs(priv);
	nve0_graph_init_unk40xx(priv);
	nve0_graph_init_unk44xx(priv);
	nve0_graph_init_unk78xx(priv);
	nve0_graph_init_unk60xx(priv);
	nve0_graph_init_unk64xx(priv);
	nve0_graph_init_unk58xx(priv);
	nve0_graph_init_unk80xx(priv);
	nve0_graph_init_unk70xx(priv);
	nve0_graph_init_unk5bxx(priv);
	nve0_graph_init_gpc(priv);
	nve0_graph_init_tpc(priv);
	nve0_graph_init_tpcunk(priv);
	nve0_graph_init_unk88xx(priv);
	nve0_graph_init_gpc_0(priv);

	nv_wr32(priv, 0x400500, 0x00010001);
	nv_wr32(priv, 0x400100, 0xffffffff);
	nv_wr32(priv, 0x40013c, 0xffffffff);

	nve0_graph_init_units(priv);
	nve0_graph_init_gpc_1(priv);
	nve0_graph_init_rop(priv);

	nv_wr32(priv, 0x400108, 0xffffffff);
	nv_wr32(priv, 0x400138, 0xffffffff);
	nv_wr32(priv, 0x400118, 0xffffffff);
	nv_wr32(priv, 0x400130, 0xffffffff);
	nv_wr32(priv, 0x40011c, 0xffffffff);
	nv_wr32(priv, 0x400134, 0xffffffff);
	nv_wr32(priv, 0x400054, 0x34ce3464);

	ret = nve0_graph_init_ctxctl(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass
nve0_graph_oclass = {
	.handle = NV_ENGINE(GR, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nve0_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
