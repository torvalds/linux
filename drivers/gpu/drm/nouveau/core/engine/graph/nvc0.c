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
#include "fuc/hubnvc0.fuc.h"
#include "fuc/gpcnvc0.fuc.h"

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nvc0_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0x9039, &nouveau_object_ofuncs },
	{ 0x9097, &nouveau_object_ofuncs },
	{ 0x90c0, &nouveau_object_ofuncs },
	{}
};

static struct nouveau_oclass
nvc1_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0x9039, &nouveau_object_ofuncs },
	{ 0x9097, &nouveau_object_ofuncs },
	{ 0x90c0, &nouveau_object_ofuncs },
	{ 0x9197, &nouveau_object_ofuncs },
	{}
};

static struct nouveau_oclass
nvc8_graph_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0x9039, &nouveau_object_ofuncs },
	{ 0x9097, &nouveau_object_ofuncs },
	{ 0x90c0, &nouveau_object_ofuncs },
	{ 0x9197, &nouveau_object_ofuncs },
	{ 0x9297, &nouveau_object_ofuncs },
	{}
};

u64
nvc0_graph_units(struct nouveau_graph *graph)
{
	struct nvc0_graph_priv *priv = (void *)graph;
	u64 cfg;

	cfg  = (u32)priv->gpc_nr;
	cfg |= (u32)priv->tpc_total << 8;
	cfg |= (u64)priv->rop_nr << 32;

	return cfg;
}

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

int
nvc0_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *args, u32 size,
			struct nouveau_object **pobject)
{
	struct nouveau_vm *vm = nouveau_client(parent)->vm;
	struct nvc0_graph_priv *priv = (void *)engine;
	struct nvc0_graph_data *data = priv->mmio_data;
	struct nvc0_graph_mmio *mmio = priv->mmio_list;
	struct nvc0_graph_chan *chan;
	int ret, i;

	/* allocate memory for context, and fill with default values */
	ret = nouveau_graph_context_create(parent, engine, oclass, NULL,
					   priv->size, 0x100,
					   NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	/* allocate memory for a "mmio list" buffer that's used by the HUB
	 * fuc to modify some per-context register settings on first load
	 * of the context.
	 */
	ret = nouveau_gpuobj_new(nv_object(chan), NULL, 0x1000, 0x100, 0,
				&chan->mmio);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_map_vm(nv_gpuobj(chan->mmio), vm,
				    NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS,
				    &chan->mmio_vma);
	if (ret)
		return ret;

	/* allocate buffers referenced by mmio list */
	for (i = 0; data->size && i < ARRAY_SIZE(priv->mmio_data); i++) {
		ret = nouveau_gpuobj_new(nv_object(chan), NULL, data->size,
					 data->align, 0, &chan->data[i].mem);
		if (ret)
			return ret;

		ret = nouveau_gpuobj_map_vm(chan->data[i].mem, vm, data->access,
					   &chan->data[i].vma);
		if (ret)
			return ret;

		data++;
	}

	/* finally, fill in the mmio list and point the context at it */
	for (i = 0; mmio->addr && i < ARRAY_SIZE(priv->mmio_list); i++) {
		u32 addr = mmio->addr;
		u32 data = mmio->data;

		if (mmio->shift) {
			u64 info = chan->data[mmio->buffer].vma.offset;
			data |= info >> mmio->shift;
		}

		nv_wo32(chan->mmio, chan->mmio_nr++ * 4, addr);
		nv_wo32(chan->mmio, chan->mmio_nr++ * 4, data);
		mmio++;
	}

	for (i = 0; i < priv->size; i += 4)
		nv_wo32(chan, i, priv->data[i / 4]);

	if (!priv->firmware) {
		nv_wo32(chan, 0x00, chan->mmio_nr / 2);
		nv_wo32(chan, 0x04, chan->mmio_vma.offset >> 8);
	} else {
		nv_wo32(chan, 0xf4, 0);
		nv_wo32(chan, 0xf8, 0);
		nv_wo32(chan, 0x10, chan->mmio_nr / 2);
		nv_wo32(chan, 0x14, lower_32_bits(chan->mmio_vma.offset));
		nv_wo32(chan, 0x18, upper_32_bits(chan->mmio_vma.offset));
		nv_wo32(chan, 0x1c, 1);
		nv_wo32(chan, 0x20, 0);
		nv_wo32(chan, 0x28, 0);
		nv_wo32(chan, 0x2c, 0);
	}

	return 0;
}

void
nvc0_graph_context_dtor(struct nouveau_object *object)
{
	struct nvc0_graph_chan *chan = (void *)object;
	int i;

	for (i = 0; i < ARRAY_SIZE(chan->data); i++) {
		nouveau_gpuobj_unmap(&chan->data[i].vma);
		nouveau_gpuobj_ref(NULL, &chan->data[i].mem);
	}

	nouveau_gpuobj_unmap(&chan->mmio_vma);
	nouveau_gpuobj_ref(NULL, &chan->mmio);

	nouveau_graph_context_destroy(&chan->base);
}

static struct nouveau_oclass
nvc0_graph_cclass = {
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
nvc0_graph_ctxctl_debug_unit(struct nvc0_graph_priv *priv, u32 base)
{
	nv_error(priv, "%06x - done 0x%08x\n", base,
		 nv_rd32(priv, base + 0x400));
	nv_error(priv, "%06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		 nv_rd32(priv, base + 0x800), nv_rd32(priv, base + 0x804),
		 nv_rd32(priv, base + 0x808), nv_rd32(priv, base + 0x80c));
	nv_error(priv, "%06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		 nv_rd32(priv, base + 0x810), nv_rd32(priv, base + 0x814),
		 nv_rd32(priv, base + 0x818), nv_rd32(priv, base + 0x81c));
}

void
nvc0_graph_ctxctl_debug(struct nvc0_graph_priv *priv)
{
	u32 gpcnr = nv_rd32(priv, 0x409604) & 0xffff;
	u32 gpc;

	nvc0_graph_ctxctl_debug_unit(priv, 0x409000);
	for (gpc = 0; gpc < gpcnr; gpc++)
		nvc0_graph_ctxctl_debug_unit(priv, 0x502000 + (gpc * 0x8000));
}

static void
nvc0_graph_ctxctl_isr(struct nvc0_graph_priv *priv)
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

static const struct nouveau_enum nvc0_mp_warp_error[] = {
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

static const struct nouveau_bitfield nvc0_mp_global_error[] = {
	{ 0x00000004, "MULTIPLE_WARP_ERRORS" },
	{ 0x00000008, "OUT_OF_STACK_SPACE" },
	{}
};

static void
nvc0_graph_trap_mp(struct nvc0_graph_priv *priv, int gpc, int tpc)
{
	u32 werr = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x648));
	u32 gerr = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x650));

	nv_error(priv, "GPC%i/TPC%i/MP trap:", gpc, tpc);
	nouveau_bitfield_print(nvc0_mp_global_error, gerr);
	if (werr) {
		pr_cont(" ");
		nouveau_enum_print(nvc0_mp_warp_error, werr & 0xffff);
	}
	pr_cont("\n");

	nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x648), 0x00000000);
	nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x650), gerr);
}

static void
nvc0_graph_trap_tpc(struct nvc0_graph_priv *priv, int gpc, int tpc)
{
	u32 stat = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x0508));

	if (stat & 0x00000001) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x0224));
		nv_error(priv, "GPC%d/TPC%d/TEX: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0224), 0xc0000000);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0508), 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		nvc0_graph_trap_mp(priv, gpc, tpc);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0508), 0x00000002);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x0084));
		nv_error(priv, "GPC%d/TPC%d/POLY: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0084), 0xc0000000);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0508), 0x00000004);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nv_rd32(priv, TPC_UNIT(gpc, tpc, 0x048c));
		nv_error(priv, "GPC%d/TPC%d/L1C: 0x%08x\n", gpc, tpc, trap);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x048c), 0xc0000000);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0508), 0x00000008);
		stat &= ~0x00000008;
	}

	if (stat) {
		nv_error(priv, "GPC%d/TPC%d/0x%08x: unknown\n", gpc, tpc, stat);
		nv_wr32(priv, TPC_UNIT(gpc, tpc, 0x0508), stat);
	}
}

static void
nvc0_graph_trap_gpc(struct nvc0_graph_priv *priv, int gpc)
{
	u32 stat = nv_rd32(priv, GPC_UNIT(gpc, 0x2c90));
	int tpc;

	if (stat & 0x00000001) {
		u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x0420));
		nv_error(priv, "GPC%d/PROP: 0x%08x\n", gpc, trap);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x0900));
		nv_error(priv, "GPC%d/ZCULL: 0x%08x\n", gpc, trap);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0x00000002);
		stat &= ~0x00000002;
	}

	if (stat & 0x00000004) {
		u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x1028));
		nv_error(priv, "GPC%d/CCACHE: 0x%08x\n", gpc, trap);
		nv_wr32(priv, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0x00000004);
		stat &= ~0x00000004;
	}

	if (stat & 0x00000008) {
		u32 trap = nv_rd32(priv, GPC_UNIT(gpc, 0x0824));
		nv_error(priv, "GPC%d/ESETUP: 0x%08x\n", gpc, trap);
		nv_wr32(priv, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), 0x00000008);
		stat &= ~0x00000009;
	}

	for (tpc = 0; tpc < priv->tpc_nr[gpc]; tpc++) {
		u32 mask = 0x00010000 << tpc;
		if (stat & mask) {
			nvc0_graph_trap_tpc(priv, gpc, tpc);
			nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), mask);
			stat &= ~mask;
		}
	}

	if (stat) {
		nv_error(priv, "GPC%d/0x%08x: unknown\n", gpc, stat);
		nv_wr32(priv, GPC_UNIT(gpc, 0x2c90), stat);
	}
}

static void
nvc0_graph_trap_intr(struct nvc0_graph_priv *priv)
{
	u32 trap = nv_rd32(priv, 0x400108);
	int rop, gpc;

	if (trap & 0x00000001) {
		u32 stat = nv_rd32(priv, 0x404000);
		nv_error(priv, "DISPATCH 0x%08x\n", stat);
		nv_wr32(priv, 0x404000, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000001);
		trap &= ~0x00000001;
	}

	if (trap & 0x00000002) {
		u32 stat = nv_rd32(priv, 0x404600);
		nv_error(priv, "M2MF 0x%08x\n", stat);
		nv_wr32(priv, 0x404600, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000002);
		trap &= ~0x00000002;
	}

	if (trap & 0x00000008) {
		u32 stat = nv_rd32(priv, 0x408030);
		nv_error(priv, "CCACHE 0x%08x\n", stat);
		nv_wr32(priv, 0x408030, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000008);
		trap &= ~0x00000008;
	}

	if (trap & 0x00000010) {
		u32 stat = nv_rd32(priv, 0x405840);
		nv_error(priv, "SHADER 0x%08x\n", stat);
		nv_wr32(priv, 0x405840, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000010);
		trap &= ~0x00000010;
	}

	if (trap & 0x00000040) {
		u32 stat = nv_rd32(priv, 0x40601c);
		nv_error(priv, "UNK6 0x%08x\n", stat);
		nv_wr32(priv, 0x40601c, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000040);
		trap &= ~0x00000040;
	}

	if (trap & 0x00000080) {
		u32 stat = nv_rd32(priv, 0x404490);
		nv_error(priv, "MACRO 0x%08x\n", stat);
		nv_wr32(priv, 0x404490, 0xc0000000);
		nv_wr32(priv, 0x400108, 0x00000080);
		trap &= ~0x00000080;
	}

	if (trap & 0x01000000) {
		u32 stat = nv_rd32(priv, 0x400118);
		for (gpc = 0; stat && gpc < priv->gpc_nr; gpc++) {
			u32 mask = 0x00000001 << gpc;
			if (stat & mask) {
				nvc0_graph_trap_gpc(priv, gpc);
				nv_wr32(priv, 0x400118, mask);
				stat &= ~mask;
			}
		}
		nv_wr32(priv, 0x400108, 0x01000000);
		trap &= ~0x01000000;
	}

	if (trap & 0x02000000) {
		for (rop = 0; rop < priv->rop_nr; rop++) {
			u32 statz = nv_rd32(priv, ROP_UNIT(rop, 0x070));
			u32 statc = nv_rd32(priv, ROP_UNIT(rop, 0x144));
			nv_error(priv, "ROP%d 0x%08x 0x%08x\n",
				 rop, statz, statc);
			nv_wr32(priv, ROP_UNIT(rop, 0x070), 0xc0000000);
			nv_wr32(priv, ROP_UNIT(rop, 0x144), 0xc0000000);
		}
		nv_wr32(priv, 0x400108, 0x02000000);
		trap &= ~0x02000000;
	}

	if (trap) {
		nv_error(priv, "TRAP UNHANDLED 0x%08x\n", trap);
		nv_wr32(priv, 0x400108, trap);
	}
}

static void
nvc0_graph_intr(struct nouveau_subdev *subdev)
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
				 chid, inst << 12, nouveau_client_name(engctx),
				 subc, class, mthd, data);
		}
		nouveau_handle_put(handle);
		nv_wr32(priv, 0x400100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000020) {
		nv_error(priv,
			 "ILLEGAL_CLASS ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, inst << 12, nouveau_client_name(engctx), subc,
			 class, mthd, data);
		nv_wr32(priv, 0x400100, 0x00000020);
		stat &= ~0x00000020;
	}

	if (stat & 0x00100000) {
		nv_error(priv, "DATA_ERROR [");
		nouveau_enum_print(nv50_data_error_names, code);
		pr_cont("] ch %d [0x%010llx %s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			chid, inst << 12, nouveau_client_name(engctx), subc,
			class, mthd, data);
		nv_wr32(priv, 0x400100, 0x00100000);
		stat &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		nv_error(priv, "TRAP ch %d [0x%010llx %s]\n", chid, inst << 12,
			 nouveau_client_name(engctx));
		nvc0_graph_trap_intr(priv);
		nv_wr32(priv, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		nvc0_graph_ctxctl_isr(priv);
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

int
nvc0_graph_ctor_fw(struct nvc0_graph_priv *priv, const char *fwname,
		   struct nvc0_graph_fuc *fuc)
{
	struct nouveau_device *device = nv_device(priv);
	const struct firmware *fw;
	char f[32];
	int ret;

	snprintf(f, sizeof(f), "nouveau/nv%02x_%s", device->chipset, fwname);
	ret = request_firmware(&fw, f, &device->pdev->dev);
	if (ret) {
		snprintf(f, sizeof(f), "nouveau/%s", fwname);
		ret = request_firmware(&fw, f, &device->pdev->dev);
		if (ret) {
			nv_error(priv, "failed to load %s\n", fwname);
			return ret;
		}
	}

	fuc->size = fw->size;
	fuc->data = kmemdup(fw->data, fuc->size, GFP_KERNEL);
	release_firmware(fw);
	return (fuc->data != NULL) ? 0 : -ENOMEM;
}

static int
nvc0_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nvc0_graph_priv *priv;
	bool enable = device->chipset != 0xd7;
	int ret, i;

	ret = nouveau_graph_create(parent, engine, oclass, enable, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x18001000;
	nv_subdev(priv)->intr = nvc0_graph_intr;
	nv_engine(priv)->cclass = &nvc0_graph_cclass;

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

	switch (nvc0_graph_class(priv)) {
	case 0x9097:
		nv_engine(priv)->sclass = nvc0_graph_sclass;
		break;
	case 0x9197:
		nv_engine(priv)->sclass = nvc1_graph_sclass;
		break;
	case 0x9297:
		nv_engine(priv)->sclass = nvc8_graph_sclass;
		break;
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

	priv->rop_nr = (nv_rd32(priv, 0x409604) & 0x001f0000) >> 16;
	priv->gpc_nr =  nv_rd32(priv, 0x409604) & 0x0000001f;
	for (i = 0; i < priv->gpc_nr; i++) {
		priv->tpc_nr[i]  = nv_rd32(priv, GPC_UNIT(i, 0x2608));
		priv->tpc_total += priv->tpc_nr[i];
	}

	/*XXX: these need figuring out... though it might not even matter */
	switch (nv_device(priv)->chipset) {
	case 0xc0:
		if (priv->tpc_total == 11) { /* 465, 3/4/4/0, 4 */
			priv->magic_not_rop_nr = 0x07;
		} else
		if (priv->tpc_total == 14) { /* 470, 3/3/4/4, 5 */
			priv->magic_not_rop_nr = 0x05;
		} else
		if (priv->tpc_total == 15) { /* 480, 3/4/4/4, 6 */
			priv->magic_not_rop_nr = 0x06;
		}
		break;
	case 0xc3: /* 450, 4/0/0/0, 2 */
		priv->magic_not_rop_nr = 0x03;
		break;
	case 0xc4: /* 460, 3/4/0/0, 4 */
		priv->magic_not_rop_nr = 0x01;
		break;
	case 0xc1: /* 2/0/0/0, 1 */
		priv->magic_not_rop_nr = 0x01;
		break;
	case 0xc8: /* 4/4/3/4, 5 */
		priv->magic_not_rop_nr = 0x06;
		break;
	case 0xce: /* 4/4/0/0, 4 */
		priv->magic_not_rop_nr = 0x03;
		break;
	case 0xcf: /* 4/0/0/0, 3 */
		priv->magic_not_rop_nr = 0x03;
		break;
	case 0xd9: /* 1/0/0/0, 1 */
		priv->magic_not_rop_nr = 0x01;
		break;
	}

	return 0;
}

static void
nvc0_graph_dtor_fw(struct nvc0_graph_fuc *fuc)
{
	kfree(fuc->data);
	fuc->data = NULL;
}

void
nvc0_graph_dtor(struct nouveau_object *object)
{
	struct nvc0_graph_priv *priv = (void *)object;

	kfree(priv->data);

	nvc0_graph_dtor_fw(&priv->fuc409c);
	nvc0_graph_dtor_fw(&priv->fuc409d);
	nvc0_graph_dtor_fw(&priv->fuc41ac);
	nvc0_graph_dtor_fw(&priv->fuc41ad);

	nouveau_gpuobj_ref(NULL, &priv->unk4188b8);
	nouveau_gpuobj_ref(NULL, &priv->unk4188b4);

	nouveau_graph_destroy(&priv->base);
}

static void
nvc0_graph_init_obj418880(struct nvc0_graph_priv *priv)
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
nvc0_graph_init_regs(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x400080, 0x003083c2);
	nv_wr32(priv, 0x400088, 0x00006fe7);
	nv_wr32(priv, 0x40008c, 0x00000000);
	nv_wr32(priv, 0x400090, 0x00000030);
	nv_wr32(priv, 0x40013c, 0x013901f7);
	nv_wr32(priv, 0x400140, 0x00000100);
	nv_wr32(priv, 0x400144, 0x00000000);
	nv_wr32(priv, 0x400148, 0x00000110);
	nv_wr32(priv, 0x400138, 0x00000000);
	nv_wr32(priv, 0x400130, 0x00000000);
	nv_wr32(priv, 0x400134, 0x00000000);
	nv_wr32(priv, 0x400124, 0x00000002);
}

static void
nvc0_graph_init_unk40xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40415c, 0x00000000);
	nv_wr32(priv, 0x404170, 0x00000000);
}

static void
nvc0_graph_init_unk44xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x404488, 0x00000000);
	nv_wr32(priv, 0x40448c, 0x00000000);
}

static void
nvc0_graph_init_unk78xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x407808, 0x00000000);
}

static void
nvc0_graph_init_unk60xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x406024, 0x00000000);
}

static void
nvc0_graph_init_unk64xx(struct nvc0_graph_priv *priv)
{
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x4064f0, 0x00000000);
		nv_wr32(priv, 0x4064f4, 0x00000000);
		nv_wr32(priv, 0x4064f8, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
}

static void
nvc0_graph_init_unk58xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x405844, 0x00ffffff);
	nv_wr32(priv, 0x405850, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x405900, 0x00002834);
		break;
	case 0xc0:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x405908, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x405928, 0x00000000);
		nv_wr32(priv, 0x40592c, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
}

static void
nvc0_graph_init_unk80xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40803c, 0x00000000);
}

static void
nvc0_graph_init_gpc(struct nvc0_graph_priv *priv)
{
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418408, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x4184a0, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x4184a4, 0x00000000);
		nv_wr32(priv, 0x4184a8, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x418604, 0x00000000);
	nv_wr32(priv, 0x418680, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
	case 0xc1:
		nv_wr32(priv, 0x418714, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc8:
	default:
		nv_wr32(priv, 0x418714, 0x80000000);
		break;
	}
	nv_wr32(priv, 0x418384, 0x00000000);
	nv_wr32(priv, 0x418814, 0x00000000);
	nv_wr32(priv, 0x418818, 0x00000000);
	nv_wr32(priv, 0x41881c, 0x00000000);
	nv_wr32(priv, 0x418b04, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
	case 0xc1:
	case 0xc8:
		nv_wr32(priv, 0x4188c8, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	default:
		nv_wr32(priv, 0x4188c8, 0x80000000);
		break;
	}
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
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418c64, 0x00000000);
		nv_wr32(priv, 0x418c68, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x418c88, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418cb4, 0x00000000);
		nv_wr32(priv, 0x418cb8, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x418d00, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418d28, 0x00000000);
		nv_wr32(priv, 0x418d2c, 0x00000000);
		nv_wr32(priv, 0x418f00, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x418f08, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418f20, 0x00000000);
		nv_wr32(priv, 0x418f24, 0x00000000);
		/*fall-through*/
	case 0xc1:
		nv_wr32(priv, 0x418e00, 0x00000003);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc8:
	default:
		nv_wr32(priv, 0x418e00, 0x00000050);
		break;
	}
	nv_wr32(priv, 0x418e08, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x418e1c, 0x00000000);
		nv_wr32(priv, 0x418e20, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x41900c, 0x00000000);
	nv_wr32(priv, 0x419018, 0x00000000);
}

static void
nvc0_graph_init_tpc(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x419d08, 0x00000000);
	nv_wr32(priv, 0x419d0c, 0x00000000);
	nv_wr32(priv, 0x419d10, 0x00000014);
	nv_wr32(priv, 0x419ab0, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419ac8, 0x00000000);
		break;
	case 0xc0:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x419ab8, 0x000000e7);
	nv_wr32(priv, 0x419abc, 0x00000000);
	nv_wr32(priv, 0x419ac0, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419ab4, 0x00000000);
		nv_wr32(priv, 0x41980c, 0x00000010);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		nv_wr32(priv, 0x41980c, 0x00000000);
		break;
	}
	nv_wr32(priv, 0x419810, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
	case 0xc1:
		nv_wr32(priv, 0x419814, 0x00000004);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc8:
	default:
		nv_wr32(priv, 0x419814, 0x00000000);
		break;
	}
	nv_wr32(priv, 0x419844, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x41984c, 0x0000a918);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		nv_wr32(priv, 0x41984c, 0x00005bc5);
		break;
	}
	nv_wr32(priv, 0x419850, 0x00000000);
	nv_wr32(priv, 0x419854, 0x00000000);
	nv_wr32(priv, 0x419858, 0x00000000);
	nv_wr32(priv, 0x41985c, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419880, 0x00000002);
		break;
	case 0xc0:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x419c98, 0x00000000);
	nv_wr32(priv, 0x419ca8, 0x80000000);
	nv_wr32(priv, 0x419cb4, 0x00000000);
	nv_wr32(priv, 0x419cb8, 0x00008bf4);
	nv_wr32(priv, 0x419cbc, 0x28137606);
	nv_wr32(priv, 0x419cc0, 0x00000000);
	nv_wr32(priv, 0x419cc4, 0x00000000);
	nv_wr32(priv, 0x419bd4, 0x00800000);
	nv_wr32(priv, 0x419bdc, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419bf8, 0x00000000);
		nv_wr32(priv, 0x419bfc, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x419d2c, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419d48, 0x00000000);
		nv_wr32(priv, 0x419d4c, 0x00000000);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		break;
	}
	nv_wr32(priv, 0x419c0c, 0x00000000);
	nv_wr32(priv, 0x419e00, 0x00000000);
	nv_wr32(priv, 0x419ea0, 0x00000000);
	nv_wr32(priv, 0x419ea4, 0x00000100);
	switch (nv_device(priv)->chipset) {
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419ea8, 0x02001100);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	default:
		nv_wr32(priv, 0x419ea8, 0x00001100);
		break;
	}

	switch (nv_device(priv)->chipset) {
	case 0xc8:
		nv_wr32(priv, 0x419eac, 0x11100f02);
		break;
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xd9:
	case 0xd7:
	default:
		nv_wr32(priv, 0x419eac, 0x11100702);
		break;
	}
	nv_wr32(priv, 0x419eb0, 0x00000003);
	nv_wr32(priv, 0x419eb4, 0x00000000);
	nv_wr32(priv, 0x419eb8, 0x00000000);
	nv_wr32(priv, 0x419ebc, 0x00000000);
	nv_wr32(priv, 0x419ec0, 0x00000000);
	switch (nv_device(priv)->chipset) {
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xd9:
	case 0xd7:
		nv_wr32(priv, 0x419ec8, 0x0e063818);
		nv_wr32(priv, 0x419ecc, 0x0e060e06);
		nv_wr32(priv, 0x419ed0, 0x00003818);
		break;
	case 0xc0:
	case 0xc8:
	default:
		nv_wr32(priv, 0x419ec8, 0x06060618);
		nv_wr32(priv, 0x419ed0, 0x0eff0e38);
		break;
	}
	nv_wr32(priv, 0x419ed4, 0x011104f1);
	nv_wr32(priv, 0x419edc, 0x00000000);
	nv_wr32(priv, 0x419f00, 0x00000000);
	nv_wr32(priv, 0x419f2c, 0x00000000);
}

static void
nvc0_graph_init_unk88xx(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x40880c, 0x00000000);
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
	nv_wr32(priv, 0x408984, 0x00000000);
	nv_wr32(priv, 0x408988, 0x08040201);
	nv_wr32(priv, 0x40898c, 0x80402010);
}

static void
nvc0_graph_init_gpc_0(struct nvc0_graph_priv *priv)
{
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tpc_total);
	u32 data[TPC_MAX / 8];
	u8  tpcnr[GPC_MAX];
	int i, gpc, tpc;

	nv_wr32(priv, TPC_UNIT(0, 0, 0x5c), 1); /* affects TFB offset queries */

	/*
	 *      TP      ROP UNKVAL(magic_not_rop_nr)
	 * 450: 4/0/0/0 2        3
	 * 460: 3/4/0/0 4        1
	 * 465: 3/4/4/0 4        7
	 * 470: 3/3/4/4 5        5
	 * 480: 3/4/4/4 6        6
	 */

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

	nv_wr32(priv, GPC_BCAST(0x1bd4), magicgpc918);
	nv_wr32(priv, GPC_BCAST(0x08ac), nv_rd32(priv, 0x100800));
}

static void
nvc0_graph_init_units(struct nvc0_graph_priv *priv)
{
	nv_wr32(priv, 0x409c24, 0x000f0000);
	nv_wr32(priv, 0x404000, 0xc0000000); /* DISPATCH */
	nv_wr32(priv, 0x404600, 0xc0000000); /* M2MF */
	nv_wr32(priv, 0x408030, 0xc0000000);
	nv_wr32(priv, 0x40601c, 0xc0000000);
	nv_wr32(priv, 0x404490, 0xc0000000); /* MACRO */
	nv_wr32(priv, 0x406018, 0xc0000000);
	nv_wr32(priv, 0x405840, 0xc0000000);
	nv_wr32(priv, 0x405844, 0x00ffffff);
	nv_mask(priv, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(priv, 0x419eb4, 0x00001000, 0x00001000);
}

static void
nvc0_graph_init_gpc_1(struct nvc0_graph_priv *priv)
{
	int gpc, tpc;

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
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
nvc0_graph_init_rop(struct nvc0_graph_priv *priv)
{
	int rop;

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(priv, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(priv, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(priv, ROP_UNIT(rop, 0x208), 0xffffffff);
	}
}

void
nvc0_graph_init_fw(struct nvc0_graph_priv *priv, u32 fuc_base,
		   struct nvc0_graph_fuc *code, struct nvc0_graph_fuc *data)
{
	int i;

	nv_wr32(priv, fuc_base + 0x01c0, 0x01000000);
	for (i = 0; i < data->size / 4; i++)
		nv_wr32(priv, fuc_base + 0x01c4, data->data[i]);

	nv_wr32(priv, fuc_base + 0x0180, 0x01000000);
	for (i = 0; i < code->size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, fuc_base + 0x0188, i >> 6);
		nv_wr32(priv, fuc_base + 0x0184, code->data[i]);
	}
}

static int
nvc0_graph_init_ctxctl(struct nvc0_graph_priv *priv)
{
	u32 r000260;
	int i;

	if (priv->firmware) {
		/* load fuc microcode */
		r000260 = nv_mask(priv, 0x000260, 0x00000001, 0x00000000);
		nvc0_graph_init_fw(priv, 0x409000, &priv->fuc409c,
						   &priv->fuc409d);
		nvc0_graph_init_fw(priv, 0x41a000, &priv->fuc41ac,
						   &priv->fuc41ad);
		nv_wr32(priv, 0x000260, r000260);

		/* start both of them running */
		nv_wr32(priv, 0x409840, 0xffffffff);
		nv_wr32(priv, 0x41a10c, 0x00000000);
		nv_wr32(priv, 0x40910c, 0x00000000);
		nv_wr32(priv, 0x41a100, 0x00000002);
		nv_wr32(priv, 0x409100, 0x00000002);
		if (!nv_wait(priv, 0x409800, 0x00000001, 0x00000001))
			nv_warn(priv, "0x409800 wait failed\n");

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

		if (priv->data == NULL) {
			int ret = nvc0_grctx_generate(priv);
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
	for (i = 0; i < sizeof(nvc0_grhub_data) / 4; i++)
		nv_wr32(priv, 0x4091c4, nvc0_grhub_data[i]);

	nv_wr32(priv, 0x409180, 0x01000000);
	for (i = 0; i < sizeof(nvc0_grhub_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x409188, i >> 6);
		nv_wr32(priv, 0x409184, nvc0_grhub_code[i]);
	}

	/* load GPC microcode */
	nv_wr32(priv, 0x41a1c0, 0x01000000);
	for (i = 0; i < sizeof(nvc0_grgpc_data) / 4; i++)
		nv_wr32(priv, 0x41a1c4, nvc0_grgpc_data[i]);

	nv_wr32(priv, 0x41a180, 0x01000000);
	for (i = 0; i < sizeof(nvc0_grgpc_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x41a188, i >> 6);
		nv_wr32(priv, 0x41a184, nvc0_grgpc_code[i]);
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
		int ret = nvc0_grctx_generate(priv);
		if (ret) {
			nv_error(priv, "failed to construct context\n");
			return ret;
		}
	}

	return 0;
}

static int
nvc0_graph_init(struct nouveau_object *object)
{
	struct nvc0_graph_priv *priv = (void *)object;
	int ret;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nvc0_graph_init_obj418880(priv);
	nvc0_graph_init_regs(priv);

	switch (nv_device(priv)->chipset) {
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xc1:
	case 0xc8:
	case 0xd9:
	case 0xd7:
		nvc0_graph_init_unk40xx(priv);
		nvc0_graph_init_unk44xx(priv);
		nvc0_graph_init_unk78xx(priv);
		nvc0_graph_init_unk60xx(priv);
		nvc0_graph_init_unk64xx(priv);
		nvc0_graph_init_unk58xx(priv);
		nvc0_graph_init_unk80xx(priv);
		nvc0_graph_init_gpc(priv);
		nvc0_graph_init_tpc(priv);
		nvc0_graph_init_unk88xx(priv);
		break;
	default:
		break;
	}

	nvc0_graph_init_gpc_0(priv);
	/*nvc0_graph_init_unitplemented_c242(priv);*/

	nv_wr32(priv, 0x400500, 0x00010001);
	nv_wr32(priv, 0x400100, 0xffffffff);
	nv_wr32(priv, 0x40013c, 0xffffffff);

	nvc0_graph_init_units(priv);
	nvc0_graph_init_gpc_1(priv);
	nvc0_graph_init_rop(priv);

	nv_wr32(priv, 0x400108, 0xffffffff);
	nv_wr32(priv, 0x400138, 0xffffffff);
	nv_wr32(priv, 0x400118, 0xffffffff);
	nv_wr32(priv, 0x400130, 0xffffffff);
	nv_wr32(priv, 0x40011c, 0xffffffff);
	nv_wr32(priv, 0x400134, 0xffffffff);
	nv_wr32(priv, 0x400054, 0x34ce3464);

	ret = nvc0_graph_init_ctxctl(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass
nvc0_graph_oclass = {
	.handle = NV_ENGINE(GR, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nvc0_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
