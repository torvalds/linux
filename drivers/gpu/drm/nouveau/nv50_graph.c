/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_ramht.h"
#include "nouveau_grctx.h"
#include "nouveau_dma.h"
#include "nouveau_vm.h"
#include "nv50_evo.h"

static int  nv50_graph_register(struct drm_device *);
static void nv50_graph_isr(struct drm_device *);

static void
nv50_graph_init_reset(struct drm_device *dev)
{
	uint32_t pmc_e = NV_PMC_ENABLE_PGRAPH | (1 << 21);

	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) & ~pmc_e);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |  pmc_e);
}

static void
nv50_graph_init_intr(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nouveau_irq_register(dev, 12, nv50_graph_isr);
	nv_wr32(dev, NV03_PGRAPH_INTR, 0xffffffff);
	nv_wr32(dev, 0x400138, 0xffffffff);
	nv_wr32(dev, NV40_PGRAPH_INTR_EN, 0xffffffff);
}

static void
nv50_graph_init_regs__nv(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	int i;

	NV_DEBUG(dev, "\n");

	nv_wr32(dev, 0x400804, 0xc0000000);
	nv_wr32(dev, 0x406800, 0xc0000000);
	nv_wr32(dev, 0x400c04, 0xc0000000);
	nv_wr32(dev, 0x401800, 0xc0000000);
	nv_wr32(dev, 0x405018, 0xc0000000);
	nv_wr32(dev, 0x402000, 0xc0000000);

	for (i = 0; i < 16; i++) {
		if (units & 1 << i) {
			if (dev_priv->chipset < 0xa0) {
				nv_wr32(dev, 0x408900 + (i << 12), 0xc0000000);
				nv_wr32(dev, 0x408e08 + (i << 12), 0xc0000000);
				nv_wr32(dev, 0x408314 + (i << 12), 0xc0000000);
			} else {
				nv_wr32(dev, 0x408600 + (i << 11), 0xc0000000);
				nv_wr32(dev, 0x408708 + (i << 11), 0xc0000000);
				nv_wr32(dev, 0x40831c + (i << 11), 0xc0000000);
			}
		}
	}

	nv_wr32(dev, 0x400108, 0xffffffff);

	nv_wr32(dev, 0x400824, 0x00004000);
	nv_wr32(dev, 0x400500, 0x00010001);
}

static void
nv50_graph_init_regs(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV04_PGRAPH_DEBUG_3,
				(1 << 2) /* HW_CONTEXT_SWITCH_ENABLED */);
	nv_wr32(dev, 0x402ca8, 0x800);
}

static int
nv50_graph_init_ctxctl(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_grctx ctx = {};
	uint32_t *cp;
	int i;

	NV_DEBUG(dev, "\n");

	cp = kmalloc(512 * 4, GFP_KERNEL);
	if (!cp) {
		NV_ERROR(dev, "failed to allocate ctxprog\n");
		dev_priv->engine.graph.accel_blocked = true;
		return 0;
	}

	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_PROG;
	ctx.data = cp;
	ctx.ctxprog_max = 512;
	if (!nv50_grctx_init(&ctx)) {
		dev_priv->engine.graph.grctx_size = ctx.ctxvals_pos * 4;

		nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_INDEX, 0);
		for (i = 0; i < ctx.ctxprog_len; i++)
			nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_DATA, cp[i]);
	} else {
		dev_priv->engine.graph.accel_blocked = true;
	}
	kfree(cp);

	nv_wr32(dev, 0x400320, 4);
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_CUR, 0);
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, 0);
	return 0;
}

int
nv50_graph_init(struct drm_device *dev)
{
	int ret;

	NV_DEBUG(dev, "\n");

	nv50_graph_init_reset(dev);
	nv50_graph_init_regs__nv(dev);
	nv50_graph_init_regs(dev);

	ret = nv50_graph_init_ctxctl(dev);
	if (ret)
		return ret;

	ret = nv50_graph_register(dev);
	if (ret)
		return ret;
	nv50_graph_init_intr(dev);
	return 0;
}

void
nv50_graph_takedown(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");
	nv_wr32(dev, 0x40013c, 0x00000000);
	nouveau_irq_unregister(dev, 12);
}

void
nv50_graph_fifo_access(struct drm_device *dev, bool enabled)
{
	const uint32_t mask = 0x00010001;

	if (enabled)
		nv_wr32(dev, 0x400500, nv_rd32(dev, 0x400500) | mask);
	else
		nv_wr32(dev, 0x400500, nv_rd32(dev, 0x400500) & ~mask);
}

struct nouveau_channel *
nv50_graph_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t inst;
	int i;

	/* Be sure we're not in the middle of a context switch or bad things
	 * will happen, such as unloading the wrong pgraph context.
	 */
	if (!nv_wait(dev, 0x400300, 0x00000001, 0x00000000))
		NV_ERROR(dev, "Ctxprog is still running\n");

	inst = nv_rd32(dev, NV50_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV50_PGRAPH_CTXCTL_CUR_LOADED))
		return NULL;
	inst = (inst & NV50_PGRAPH_CTXCTL_CUR_INSTANCE) << 12;

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];

		if (chan && chan->ramin && chan->ramin->vinst == inst)
			return chan;
	}

	return NULL;
}

int
nv50_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_grctx ctx = {};
	int hdr, ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, chan, pgraph->grctx_size, 0,
				 NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &chan->ramin_grctx);
	if (ret)
		return ret;

	hdr = (dev_priv->chipset == 0x50) ? 0x200 : 0x20;
	nv_wo32(ramin, hdr + 0x00, 0x00190002);
	nv_wo32(ramin, hdr + 0x04, chan->ramin_grctx->vinst +
				   pgraph->grctx_size - 1);
	nv_wo32(ramin, hdr + 0x08, chan->ramin_grctx->vinst);
	nv_wo32(ramin, hdr + 0x0c, 0);
	nv_wo32(ramin, hdr + 0x10, 0);
	nv_wo32(ramin, hdr + 0x14, 0x00010000);

	ctx.dev = chan->dev;
	ctx.mode = NOUVEAU_GRCTX_VALS;
	ctx.data = chan->ramin_grctx;
	nv50_grctx_init(&ctx);

	nv_wo32(chan->ramin_grctx, 0x00000, chan->ramin->vinst >> 12);

	dev_priv->engine.instmem.flush(dev);
	atomic_inc(&chan->vm->pgraph_refs);
	return 0;
}

void
nv50_graph_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	int i, hdr = (dev_priv->chipset == 0x50) ? 0x200 : 0x20;
	unsigned long flags;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (!chan->ramin)
		return;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	pgraph->fifo_access(dev, false);

	if (pgraph->channel(dev) == chan)
		pgraph->unload_context(dev);

	for (i = hdr; i < hdr + 24; i += 4)
		nv_wo32(chan->ramin, i, 0);
	dev_priv->engine.instmem.flush(dev);

	pgraph->fifo_access(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	nouveau_gpuobj_ref(NULL, &chan->ramin_grctx);

	atomic_dec(&chan->vm->pgraph_refs);
}

static int
nv50_graph_do_load_context(struct drm_device *dev, uint32_t inst)
{
	uint32_t fifo = nv_rd32(dev, 0x400500);

	nv_wr32(dev, 0x400500, fifo & ~1);
	nv_wr32(dev, 0x400784, inst);
	nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) | 0x40);
	nv_wr32(dev, 0x400320, nv_rd32(dev, 0x400320) | 0x11);
	nv_wr32(dev, 0x400040, 0xffffffff);
	(void)nv_rd32(dev, 0x400040);
	nv_wr32(dev, 0x400040, 0x00000000);
	nv_wr32(dev, 0x400304, nv_rd32(dev, 0x400304) | 1);

	if (nouveau_wait_for_idle(dev))
		nv_wr32(dev, 0x40032c, inst | (1<<31));
	nv_wr32(dev, 0x400500, fifo);

	return 0;
}

int
nv50_graph_load_context(struct nouveau_channel *chan)
{
	uint32_t inst = chan->ramin->vinst >> 12;

	NV_DEBUG(chan->dev, "ch%d\n", chan->id);
	return nv50_graph_do_load_context(chan->dev, inst);
}

int
nv50_graph_unload_context(struct drm_device *dev)
{
	uint32_t inst;

	inst  = nv_rd32(dev, NV50_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV50_PGRAPH_CTXCTL_CUR_LOADED))
		return 0;
	inst &= NV50_PGRAPH_CTXCTL_CUR_INSTANCE;

	nouveau_wait_for_idle(dev);
	nv_wr32(dev, 0x400784, inst);
	nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) | 0x20);
	nv_wr32(dev, 0x400304, nv_rd32(dev, 0x400304) | 0x01);
	nouveau_wait_for_idle(dev);

	nv_wr32(dev, NV50_PGRAPH_CTXCTL_CUR, inst);
	return 0;
}

static void
nv50_graph_context_switch(struct drm_device *dev)
{
	uint32_t inst;

	nv50_graph_unload_context(dev);

	inst  = nv_rd32(dev, NV50_PGRAPH_CTXCTL_NEXT);
	inst &= NV50_PGRAPH_CTXCTL_NEXT_INSTANCE;
	nv50_graph_do_load_context(dev, inst);

	nv_wr32(dev, NV40_PGRAPH_INTR_EN, nv_rd32(dev,
		NV40_PGRAPH_INTR_EN) | NV_PGRAPH_INTR_CONTEXT_SWITCH);
}

static int
nv50_graph_nvsw_dma_vblsem(struct nouveau_channel *chan,
			   u32 class, u32 mthd, u32 data)
{
	struct nouveau_gpuobj *gpuobj;

	gpuobj = nouveau_ramht_find(chan, data);
	if (!gpuobj)
		return -ENOENT;

	if (nouveau_notifier_offset(gpuobj, NULL))
		return -EINVAL;

	chan->nvsw.vblsem = gpuobj;
	chan->nvsw.vblsem_offset = ~0;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_offset(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	if (nouveau_notifier_offset(chan->nvsw.vblsem, &data))
		return -ERANGE;

	chan->nvsw.vblsem_offset = data >> 2;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_release_val(struct nouveau_channel *chan,
				   u32 class, u32 mthd, u32 data)
{
	chan->nvsw.vblsem_rval = data;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_release(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (!chan->nvsw.vblsem || chan->nvsw.vblsem_offset == ~0 || data > 1)
		return -EINVAL;

	drm_vblank_get(dev, data);

	chan->nvsw.vblsem_head = data;
	list_add(&chan->nvsw.vbl_wait, &dev_priv->vbl_waiting);

	return 0;
}

static int
nv50_graph_nvsw_mthd_page_flip(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	struct nouveau_page_flip_state s;

	if (!nouveau_finish_page_flip(chan, &s)) {
		/* XXX - Do something here */
	}

	return 0;
}

static int
nv50_graph_register(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->engine.graph.registered)
		return 0;

	NVOBJ_CLASS(dev, 0x506e, SW); /* nvsw */
	NVOBJ_MTHD (dev, 0x506e, 0x018c, nv50_graph_nvsw_dma_vblsem);
	NVOBJ_MTHD (dev, 0x506e, 0x0400, nv50_graph_nvsw_vblsem_offset);
	NVOBJ_MTHD (dev, 0x506e, 0x0404, nv50_graph_nvsw_vblsem_release_val);
	NVOBJ_MTHD (dev, 0x506e, 0x0408, nv50_graph_nvsw_vblsem_release);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, nv50_graph_nvsw_mthd_page_flip);

	NVOBJ_CLASS(dev, 0x0030, GR); /* null */
	NVOBJ_CLASS(dev, 0x5039, GR); /* m2mf */
	NVOBJ_CLASS(dev, 0x502d, GR); /* 2d */

	/* tesla */
	if (dev_priv->chipset == 0x50)
		NVOBJ_CLASS(dev, 0x5097, GR); /* tesla (nv50) */
	else
	if (dev_priv->chipset < 0xa0)
		NVOBJ_CLASS(dev, 0x8297, GR); /* tesla (nv8x/nv9x) */
	else {
		switch (dev_priv->chipset) {
		case 0xa0:
		case 0xaa:
		case 0xac:
			NVOBJ_CLASS(dev, 0x8397, GR);
			break;
		case 0xa3:
		case 0xa5:
		case 0xa8:
			NVOBJ_CLASS(dev, 0x8597, GR);
			break;
		case 0xaf:
			NVOBJ_CLASS(dev, 0x8697, GR);
			break;
		}
	}

	/* compute */
	NVOBJ_CLASS(dev, 0x50c0, GR);
	if (dev_priv->chipset  > 0xa0 &&
	    dev_priv->chipset != 0xaa &&
	    dev_priv->chipset != 0xac)
		NVOBJ_CLASS(dev, 0x85c0, GR);

	dev_priv->engine.graph.registered = true;
	return 0;
}

void
nv50_graph_tlb_flush(struct drm_device *dev)
{
	nv50_vm_flush_engine(dev, 0);
}

void
nv86_graph_tlb_flush(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	bool idle, timeout = false;
	unsigned long flags;
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x400500, 0x00000001, 0x00000000);

	start = ptimer->read(dev);
	do {
		idle = true;

		for (tmp = nv_rd32(dev, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(dev, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(dev, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle && !(timeout = ptimer->read(dev) - start > 2000000000));

	if (timeout) {
		NV_ERROR(dev, "PGRAPH TLB flush idle timeout fail: "
			      "0x%08x 0x%08x 0x%08x 0x%08x\n",
			 nv_rd32(dev, 0x400700), nv_rd32(dev, 0x400380),
			 nv_rd32(dev, 0x400384), nv_rd32(dev, 0x400388));
	}

	nv50_vm_flush_engine(dev, 0);

	nv_mask(dev, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}

static struct nouveau_enum nv50_mp_exec_error_names[] =
{
	{ 3, "STACK_UNDERFLOW" },
	{ 4, "QUADON_ACTIVE" },
	{ 8, "TIMEOUT" },
	{ 0x10, "INVALID_OPCODE" },
	{ 0x40, "BREAKPOINT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_m2mf[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "IN" },
	{ 0x00000004, "OUT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_vfetch[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_strmout[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_ccache[] = {
	{ 0x00000001, "FAULT" },
	{}
};

/* There must be a *lot* of these. Will take some time to gather them up. */
struct nouveau_enum nv50_data_error_names[] = {
	{ 0x00000003, "INVALID_QUERY_OR_TEXTURE" },
	{ 0x00000004, "INVALID_VALUE" },
	{ 0x00000005, "INVALID_ENUM" },
	{ 0x00000008, "INVALID_OBJECT" },
	{ 0x00000009, "READ_ONLY_OBJECT" },
	{ 0x0000000a, "SUPERVISOR_OBJECT" },
	{ 0x0000000b, "INVALID_ADDRESS_ALIGNMENT" },
	{ 0x0000000c, "INVALID_BITFIELD" },
	{ 0x0000000d, "BEGIN_END_ACTIVE" },
	{ 0x0000000e, "SEMANTIC_COLOR_BACK_OVER_LIMIT" },
	{ 0x0000000f, "VIEWPORT_ID_NEEDS_GP" },
	{ 0x00000010, "RT_DOUBLE_BIND" },
	{ 0x00000011, "RT_TYPES_MISMATCH" },
	{ 0x00000012, "RT_LINEAR_WITH_ZETA" },
	{ 0x00000015, "FP_TOO_FEW_REGS" },
	{ 0x00000016, "ZETA_FORMAT_CSAA_MISMATCH" },
	{ 0x00000017, "RT_LINEAR_WITH_MSAA" },
	{ 0x00000018, "FP_INTERPOLANT_START_OVER_LIMIT" },
	{ 0x00000019, "SEMANTIC_LAYER_OVER_LIMIT" },
	{ 0x0000001a, "RT_INVALID_ALIGNMENT" },
	{ 0x0000001b, "SAMPLER_OVER_LIMIT" },
	{ 0x0000001c, "TEXTURE_OVER_LIMIT" },
	{ 0x0000001e, "GP_TOO_MANY_OUTPUTS" },
	{ 0x0000001f, "RT_BPP128_WITH_MS8" },
	{ 0x00000021, "Z_OUT_OF_BOUNDS" },
	{ 0x00000023, "XY_OUT_OF_BOUNDS" },
	{ 0x00000027, "CP_MORE_PARAMS_THAN_SHARED" },
	{ 0x00000028, "CP_NO_REG_SPACE_STRIPED" },
	{ 0x00000029, "CP_NO_REG_SPACE_PACKED" },
	{ 0x0000002a, "CP_NOT_ENOUGH_WARPS" },
	{ 0x0000002b, "CP_BLOCK_SIZE_MISMATCH" },
	{ 0x0000002c, "CP_NOT_ENOUGH_LOCAL_WARPS" },
	{ 0x0000002d, "CP_NOT_ENOUGH_STACK_WARPS" },
	{ 0x0000002e, "CP_NO_BLOCKDIM_LATCH" },
	{ 0x00000031, "ENG2D_FORMAT_MISMATCH" },
	{ 0x0000003f, "PRIMITIVE_ID_NEEDS_GP" },
	{ 0x00000044, "SEMANTIC_VIEWPORT_OVER_LIMIT" },
	{ 0x00000045, "SEMANTIC_COLOR_FRONT_OVER_LIMIT" },
	{ 0x00000046, "LAYER_ID_NEEDS_GP" },
	{ 0x00000047, "SEMANTIC_CLIP_OVER_LIMIT" },
	{ 0x00000048, "SEMANTIC_PTSZ_OVER_LIMIT" },
	{}
};

static struct nouveau_bitfield nv50_graph_intr[] = {
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

static void
nv50_pgraph_mp_trap(struct drm_device *dev, int tpid, int display)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	uint32_t addr, mp10, status, pc, oplow, ophigh;
	int i;
	int mps = 0;
	for (i = 0; i < 4; i++) {
		if (!(units & 1 << (i+24)))
			continue;
		if (dev_priv->chipset < 0xa0)
			addr = 0x408200 + (tpid << 12) + (i << 7);
		else
			addr = 0x408100 + (tpid << 11) + (i << 7);
		mp10 = nv_rd32(dev, addr + 0x10);
		status = nv_rd32(dev, addr + 0x14);
		if (!status)
			continue;
		if (display) {
			nv_rd32(dev, addr + 0x20);
			pc = nv_rd32(dev, addr + 0x24);
			oplow = nv_rd32(dev, addr + 0x70);
			ophigh= nv_rd32(dev, addr + 0x74);
			NV_INFO(dev, "PGRAPH_TRAP_MP_EXEC - "
					"TP %d MP %d: ", tpid, i);
			nouveau_enum_print(nv50_mp_exec_error_names, status);
			printk(" at %06x warp %d, opcode %08x %08x\n",
					pc&0xffffff, pc >> 24,
					oplow, ophigh);
		}
		nv_wr32(dev, addr + 0x10, mp10);
		nv_wr32(dev, addr + 0x14, 0);
		mps++;
	}
	if (!mps && display)
		NV_INFO(dev, "PGRAPH_TRAP_MP_EXEC - TP %d: "
				"No MPs claiming errors?\n", tpid);
}

static void
nv50_pgraph_tp_trap(struct drm_device *dev, int type, uint32_t ustatus_old,
		uint32_t ustatus_new, int display, const char *name)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int tps = 0;
	uint32_t units = nv_rd32(dev, 0x1540);
	int i, r;
	uint32_t ustatus_addr, ustatus;
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;
		if (dev_priv->chipset < 0xa0)
			ustatus_addr = ustatus_old + (i << 12);
		else
			ustatus_addr = ustatus_new + (i << 11);
		ustatus = nv_rd32(dev, ustatus_addr) & 0x7fffffff;
		if (!ustatus)
			continue;
		tps++;
		switch (type) {
		case 6: /* texture error... unknown for now */
			nv50_fb_vm_trap(dev, display, name);
			if (display) {
				NV_ERROR(dev, "magic set %d:\n", i);
				for (r = ustatus_addr + 4; r <= ustatus_addr + 0x10; r += 4)
					NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
						nv_rd32(dev, r));
			}
			break;
		case 7: /* MP error */
			if (ustatus & 0x00010000) {
				nv50_pgraph_mp_trap(dev, i, display);
				ustatus &= ~0x00010000;
			}
			break;
		case 8: /* TPDMA error */
			{
			uint32_t e0c = nv_rd32(dev, ustatus_addr + 4);
			uint32_t e10 = nv_rd32(dev, ustatus_addr + 8);
			uint32_t e14 = nv_rd32(dev, ustatus_addr + 0xc);
			uint32_t e18 = nv_rd32(dev, ustatus_addr + 0x10);
			uint32_t e1c = nv_rd32(dev, ustatus_addr + 0x14);
			uint32_t e20 = nv_rd32(dev, ustatus_addr + 0x18);
			uint32_t e24 = nv_rd32(dev, ustatus_addr + 0x1c);
			nv50_fb_vm_trap(dev, display, name);
			/* 2d engine destination */
			if (ustatus & 0x00000010) {
				if (display) {
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_2D - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_2D - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000010;
			}
			/* Render target */
			if (ustatus & 0x00000040) {
				if (display) {
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_RT - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_RT - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000040;
			}
			/* CUDA memory: l[], g[] or stack. */
			if (ustatus & 0x00000080) {
				if (display) {
					if (e18 & 0x80000000) {
						/* g[] read fault? */
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Global read fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 24) & 0x1f));
						e18 &= ~0x1f000000;
					} else if (e18 & 0xc) {
						/* g[] write fault? */
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Global write fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 7) & 0x1f));
						e18 &= ~0x00000f80;
					} else {
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Unknown CUDA fault at address %02x%08x\n",
								i, e14, e10);
					}
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000080;
			}
			}
			break;
		}
		if (ustatus) {
			if (display)
				NV_INFO(dev, "%s - TP%d: Unhandled ustatus 0x%08x\n", name, i, ustatus);
		}
		nv_wr32(dev, ustatus_addr, 0xc0000000);
	}

	if (!tps && display)
		NV_INFO(dev, "%s - No TPs claiming errors?\n", name);
}

static int
nv50_pgraph_trap_handler(struct drm_device *dev, u32 display, u64 inst, u32 chid)
{
	u32 status = nv_rd32(dev, 0x400108);
	u32 ustatus;

	if (!status && display) {
		NV_INFO(dev, "PGRAPH - TRAP: no units reporting traps?\n");
		return 1;
	}

	/* DISPATCH: Relays commands to other units and handles NOTIFY,
	 * COND, QUERY. If you get a trap from it, the command is still stuck
	 * in DISPATCH and you need to do something about it. */
	if (status & 0x001) {
		ustatus = nv_rd32(dev, 0x400804) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_DISPATCH - no ustatus?\n");
		}

		nv_wr32(dev, 0x400500, 0x00000000);

		/* Known to be triggered by screwed up NOTIFY and COND... */
		if (ustatus & 0x00000001) {
			u32 addr = nv_rd32(dev, 0x400808);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 datal = nv_rd32(dev, 0x40080c);
			u32 datah = nv_rd32(dev, 0x400810);
			u32 class = nv_rd32(dev, 0x400814);
			u32 r848 = nv_rd32(dev, 0x400848);

			NV_INFO(dev, "PGRAPH - TRAP DISPATCH_FAULT\n");
			if (display && (addr & 0x80000000)) {
				NV_INFO(dev, "PGRAPH - ch %d (0x%010llx) "
					     "subc %d class 0x%04x mthd 0x%04x "
					     "data 0x%08x%08x "
					     "400808 0x%08x 400848 0x%08x\n",
					chid, inst, subc, class, mthd, datah,
					datal, addr, r848);
			} else
			if (display) {
				NV_INFO(dev, "PGRAPH - no stuck command?\n");
			}

			nv_wr32(dev, 0x400808, 0);
			nv_wr32(dev, 0x4008e8, nv_rd32(dev, 0x4008e8) & 3);
			nv_wr32(dev, 0x400848, 0);
			ustatus &= ~0x00000001;
		}

		if (ustatus & 0x00000002) {
			u32 addr = nv_rd32(dev, 0x40084c);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 data = nv_rd32(dev, 0x40085c);
			u32 class = nv_rd32(dev, 0x400814);

			NV_INFO(dev, "PGRAPH - TRAP DISPATCH_QUERY\n");
			if (display && (addr & 0x80000000)) {
				NV_INFO(dev, "PGRAPH - ch %d (0x%010llx) "
					     "subc %d class 0x%04x mthd 0x%04x "
					     "data 0x%08x 40084c 0x%08x\n",
					chid, inst, subc, class, mthd,
					data, addr);
			} else
			if (display) {
				NV_INFO(dev, "PGRAPH - no stuck command?\n");
			}

			nv_wr32(dev, 0x40084c, 0);
			ustatus &= ~0x00000002;
		}

		if (ustatus && display) {
			NV_INFO(dev, "PGRAPH - TRAP_DISPATCH (unknown "
				      "0x%08x)\n", ustatus);
		}

		nv_wr32(dev, 0x400804, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x001);
		status &= ~0x001;
		if (!status)
			return 0;
	}

	/* M2MF: Memory to memory copy engine. */
	if (status & 0x002) {
		u32 ustatus = nv_rd32(dev, 0x406800) & 0x7fffffff;
		if (display) {
			NV_INFO(dev, "PGRAPH - TRAP_M2MF");
			nouveau_bitfield_print(nv50_graph_trap_m2mf, ustatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - TRAP_M2MF %08x %08x %08x %08x\n",
				nv_rd32(dev, 0x406804), nv_rd32(dev, 0x406808),
				nv_rd32(dev, 0x40680c), nv_rd32(dev, 0x406810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 2);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x406800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x002);
		status &= ~0x002;
	}

	/* VFETCH: Fetches data from vertex buffers. */
	if (status & 0x004) {
		u32 ustatus = nv_rd32(dev, 0x400c04) & 0x7fffffff;
		if (display) {
			NV_INFO(dev, "PGRAPH - TRAP_VFETCH");
			nouveau_bitfield_print(nv50_graph_trap_vfetch, ustatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - TRAP_VFETCH %08x %08x %08x %08x\n",
				nv_rd32(dev, 0x400c00), nv_rd32(dev, 0x400c08),
				nv_rd32(dev, 0x400c0c), nv_rd32(dev, 0x400c10));
		}

		nv_wr32(dev, 0x400c04, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x004);
		status &= ~0x004;
	}

	/* STRMOUT: DirectX streamout / OpenGL transform feedback. */
	if (status & 0x008) {
		ustatus = nv_rd32(dev, 0x401800) & 0x7fffffff;
		if (display) {
			NV_INFO(dev, "PGRAPH - TRAP_STRMOUT");
			nouveau_bitfield_print(nv50_graph_trap_strmout, ustatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - TRAP_STRMOUT %08x %08x %08x %08x\n",
				nv_rd32(dev, 0x401804), nv_rd32(dev, 0x401808),
				nv_rd32(dev, 0x40180c), nv_rd32(dev, 0x401810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 0x80);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x401800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x008);
		status &= ~0x008;
	}

	/* CCACHE: Handles code and c[] caches and fills them. */
	if (status & 0x010) {
		ustatus = nv_rd32(dev, 0x405018) & 0x7fffffff;
		if (display) {
			NV_INFO(dev, "PGRAPH - TRAP_CCACHE");
			nouveau_bitfield_print(nv50_graph_trap_ccache, ustatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - TRAP_CCACHE %08x %08x %08x %08x"
				     " %08x %08x %08x\n",
				nv_rd32(dev, 0x405800), nv_rd32(dev, 0x405804),
				nv_rd32(dev, 0x405808), nv_rd32(dev, 0x40580c),
				nv_rd32(dev, 0x405810), nv_rd32(dev, 0x405814),
				nv_rd32(dev, 0x40581c));

		}

		nv_wr32(dev, 0x405018, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x010);
		status &= ~0x010;
	}

	/* Unknown, not seen yet... 0x402000 is the only trap status reg
	 * remaining, so try to handle it anyway. Perhaps related to that
	 * unknown DMA slot on tesla? */
	if (status & 0x20) {
		ustatus = nv_rd32(dev, 0x402000) & 0x7fffffff;
		if (display)
			NV_INFO(dev, "PGRAPH - TRAP_UNKC04 0x%08x\n", ustatus);
		nv_wr32(dev, 0x402000, 0xc0000000);
		/* no status modifiction on purpose */
	}

	/* TEXTURE: CUDA texturing units */
	if (status & 0x040) {
		nv50_pgraph_tp_trap(dev, 6, 0x408900, 0x408600, display,
				    "PGRAPH - TRAP_TEXTURE");
		nv_wr32(dev, 0x400108, 0x040);
		status &= ~0x040;
	}

	/* MP: CUDA execution engines. */
	if (status & 0x080) {
		nv50_pgraph_tp_trap(dev, 7, 0x408314, 0x40831c, display,
				    "PGRAPH - TRAP_MP");
		nv_wr32(dev, 0x400108, 0x080);
		status &= ~0x080;
	}

	/* TPDMA:  Handles TP-initiated uncached memory accesses:
	 * l[], g[], stack, 2d surfaces, render targets. */
	if (status & 0x100) {
		nv50_pgraph_tp_trap(dev, 8, 0x408e08, 0x408708, display,
				    "PGRAPH - TRAP_TPDMA");
		nv_wr32(dev, 0x400108, 0x100);
		status &= ~0x100;
	}

	if (status) {
		if (display)
			NV_INFO(dev, "PGRAPH - TRAP: unknown 0x%08x\n", status);
		nv_wr32(dev, 0x400108, status);
	}

	return 1;
}

static int
nv50_graph_isr_chid(struct drm_device *dev, u64 inst)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		chan = dev_priv->channels.ptr[i];
		if (!chan || !chan->ramin)
			continue;

		if (inst == chan->ramin->vinst)
			break;
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return i;
}

static void
nv50_graph_isr(struct drm_device *dev)
{
	u32 stat;

	while ((stat = nv_rd32(dev, 0x400100))) {
		u64 inst = (u64)(nv_rd32(dev, 0x40032c) & 0x0fffffff) << 12;
		u32 chid = nv50_graph_isr_chid(dev, inst);
		u32 addr = nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR);
		u32 subc = (addr & 0x00070000) >> 16;
		u32 mthd = (addr & 0x00001ffc);
		u32 data = nv_rd32(dev, NV04_PGRAPH_TRAPPED_DATA);
		u32 class = nv_rd32(dev, 0x400814);
		u32 show = stat;

		if (stat & 0x00000010) {
			if (!nouveau_gpuobj_mthd_call2(dev, chid, class,
						       mthd, data))
				show &= ~0x00000010;
		}

		if (stat & 0x00001000) {
			nv_wr32(dev, 0x400500, 0x00000000);
			nv_wr32(dev, 0x400100, 0x00001000);
			nv_mask(dev, 0x40013c, 0x00001000, 0x00000000);
			nv50_graph_context_switch(dev);
			stat &= ~0x00001000;
			show &= ~0x00001000;
		}

		show = (show && nouveau_ratelimit()) ? show : 0;

		if (show & 0x00100000) {
			u32 ecode = nv_rd32(dev, 0x400110);
			NV_INFO(dev, "PGRAPH - DATA_ERROR ");
			nouveau_enum_print(nv50_data_error_names, ecode);
			printk("\n");
		}

		if (stat & 0x00200000) {
			if (!nv50_pgraph_trap_handler(dev, show, inst, chid))
				show &= ~0x00200000;
		}

		nv_wr32(dev, 0x400100, stat);
		nv_wr32(dev, 0x400500, 0x00010001);

		if (show) {
			NV_INFO(dev, "PGRAPH -");
			nouveau_bitfield_print(nv50_graph_intr, show);
			printk("\n");
			NV_INFO(dev, "PGRAPH - ch %d (0x%010llx) subc %d "
				     "class 0x%04x mthd 0x%04x data 0x%08x\n",
				chid, inst, subc, class, mthd, data);
		}
	}

	if (nv_rd32(dev, 0x400824) & (1 << 31))
		nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) & ~(1 << 31));
}
