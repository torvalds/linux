/*
 * Copyright 2009 Marcin Kościelnicki
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
 */

#define CP_FLAG_CLEAR                 0
#define CP_FLAG_SET                   1
#define CP_FLAG_SWAP_DIRECTION        ((0 * 32) + 0)
#define CP_FLAG_SWAP_DIRECTION_LOAD   0
#define CP_FLAG_SWAP_DIRECTION_SAVE   1
#define CP_FLAG_UNK01                 ((0 * 32) + 1)
#define CP_FLAG_UNK01_CLEAR           0
#define CP_FLAG_UNK01_SET             1
#define CP_FLAG_UNK03                 ((0 * 32) + 3)
#define CP_FLAG_UNK03_CLEAR           0
#define CP_FLAG_UNK03_SET             1
#define CP_FLAG_USER_SAVE             ((0 * 32) + 5)
#define CP_FLAG_USER_SAVE_NOT_PENDING 0
#define CP_FLAG_USER_SAVE_PENDING     1
#define CP_FLAG_USER_LOAD             ((0 * 32) + 6)
#define CP_FLAG_USER_LOAD_NOT_PENDING 0
#define CP_FLAG_USER_LOAD_PENDING     1
#define CP_FLAG_UNK0B                 ((0 * 32) + 0xb)
#define CP_FLAG_UNK0B_CLEAR           0
#define CP_FLAG_UNK0B_SET             1
#define CP_FLAG_XFER_SWITCH           ((0 * 32) + 0xe)
#define CP_FLAG_XFER_SWITCH_DISABLE   0
#define CP_FLAG_XFER_SWITCH_ENABLE    1
#define CP_FLAG_STATE                 ((0 * 32) + 0x1c)
#define CP_FLAG_STATE_STOPPED         0
#define CP_FLAG_STATE_RUNNING         1
#define CP_FLAG_UNK1D                 ((0 * 32) + 0x1d)
#define CP_FLAG_UNK1D_CLEAR           0
#define CP_FLAG_UNK1D_SET             1
#define CP_FLAG_UNK20                 ((1 * 32) + 0)
#define CP_FLAG_UNK20_CLEAR           0
#define CP_FLAG_UNK20_SET             1
#define CP_FLAG_STATUS                ((2 * 32) + 0)
#define CP_FLAG_STATUS_BUSY           0
#define CP_FLAG_STATUS_IDLE           1
#define CP_FLAG_AUTO_SAVE             ((2 * 32) + 4)
#define CP_FLAG_AUTO_SAVE_NOT_PENDING 0
#define CP_FLAG_AUTO_SAVE_PENDING     1
#define CP_FLAG_AUTO_LOAD             ((2 * 32) + 5)
#define CP_FLAG_AUTO_LOAD_NOT_PENDING 0
#define CP_FLAG_AUTO_LOAD_PENDING     1
#define CP_FLAG_NEWCTX                ((2 * 32) + 10)
#define CP_FLAG_NEWCTX_BUSY           0
#define CP_FLAG_NEWCTX_DONE           1
#define CP_FLAG_XFER                  ((2 * 32) + 11)
#define CP_FLAG_XFER_IDLE             0
#define CP_FLAG_XFER_BUSY             1
#define CP_FLAG_ALWAYS                ((2 * 32) + 13)
#define CP_FLAG_ALWAYS_FALSE          0
#define CP_FLAG_ALWAYS_TRUE           1
#define CP_FLAG_INTR                  ((2 * 32) + 15)
#define CP_FLAG_INTR_NOT_PENDING      0
#define CP_FLAG_INTR_PENDING          1

#define CP_CTX                   0x00100000
#define CP_CTX_COUNT             0x000f0000
#define CP_CTX_COUNT_SHIFT               16
#define CP_CTX_REG               0x00003fff
#define CP_LOAD_SR               0x00200000
#define CP_LOAD_SR_VALUE         0x000fffff
#define CP_BRA                   0x00400000
#define CP_BRA_IP                0x0001ff00
#define CP_BRA_IP_SHIFT                   8
#define CP_BRA_IF_CLEAR          0x00000080
#define CP_BRA_FLAG              0x0000007f
#define CP_WAIT                  0x00500000
#define CP_WAIT_SET              0x00000080
#define CP_WAIT_FLAG             0x0000007f
#define CP_SET                   0x00700000
#define CP_SET_1                 0x00000080
#define CP_SET_FLAG              0x0000007f
#define CP_NEWCTX                0x00600004
#define CP_NEXT_TO_SWAP          0x00600005
#define CP_SET_CONTEXT_POINTER   0x00600006
#define CP_SET_XFER_POINTER      0x00600007
#define CP_ENABLE                0x00600009
#define CP_END                   0x0060000c
#define CP_NEXT_TO_CURRENT       0x0060000d
#define CP_DISABLE1              0x0090ffff
#define CP_DISABLE2              0x0091ffff
#define CP_XFER_1      0x008000ff
#define CP_XFER_2      0x008800ff
#define CP_SEEK_1      0x00c000ff
#define CP_SEEK_2      0x00c800ff

#include "ctxnv40.h"
#include "nv50.h"

#include <subdev/fb.h>

#define IS_NVA3F(x) (((x) > 0xa0 && (x) < 0xaa) || (x) == 0xaf)
#define IS_NVAAF(x) ((x) >= 0xaa && (x) <= 0xac)

/*
 * This code deals with PGRAPH contexts on NV50 family cards. Like NV40, it's
 * the GPU itself that does context-switching, but it needs a special
 * microcode to do it. And it's the driver's task to supply this microcode,
 * further known as ctxprog, as well as the initial context values, known
 * as ctxvals.
 *
 * Without ctxprog, you cannot switch contexts. Not even in software, since
 * the majority of context [xfer strands] isn't accessible directly. You're
 * stuck with a single channel, and you also suffer all the problems resulting
 * from missing ctxvals, since you cannot load them.
 *
 * Without ctxvals, you're stuck with PGRAPH's default context. It's enough to
 * run 2d operations, but trying to utilise 3d or CUDA will just lock you up,
 * since you don't have... some sort of needed setup.
 *
 * Nouveau will just disable acceleration if not given ctxprog + ctxvals, since
 * it's too much hassle to handle no-ctxprog as a special case.
 */

/*
 * How ctxprogs work.
 *
 * The ctxprog is written in its own kind of microcode, with very small and
 * crappy set of available commands. You upload it to a small [512 insns]
 * area of memory on PGRAPH, and it'll be run when PFIFO wants PGRAPH to
 * switch channel. or when the driver explicitely requests it. Stuff visible
 * to ctxprog consists of: PGRAPH MMIO registers, PGRAPH context strands,
 * the per-channel context save area in VRAM [known as ctxvals or grctx],
 * 4 flags registers, a scratch register, two grctx pointers, plus many
 * random poorly-understood details.
 *
 * When ctxprog runs, it's supposed to check what operations are asked of it,
 * save old context if requested, optionally reset PGRAPH and switch to the
 * new channel, and load the new context. Context consists of three major
 * parts: subset of MMIO registers and two "xfer areas".
 */

/* TODO:
 *  - document unimplemented bits compared to nvidia
 *  - NVAx: make a TP subroutine, use it.
 *  - use 0x4008fc instead of 0x1540?
 */

enum cp_label {
	cp_check_load = 1,
	cp_setup_auto_load,
	cp_setup_load,
	cp_setup_save,
	cp_swap_state,
	cp_prepare_exit,
	cp_exit,
};

static void nv50_gr_construct_mmio(struct nvkm_grctx *ctx);
static void nv50_gr_construct_xfer1(struct nvkm_grctx *ctx);
static void nv50_gr_construct_xfer2(struct nvkm_grctx *ctx);

/* Main function: construct the ctxprog skeleton, call the other functions. */

static int
nv50_grctx_generate(struct nvkm_grctx *ctx)
{
	cp_set (ctx, STATE, RUNNING);
	cp_set (ctx, XFER_SWITCH, ENABLE);
	/* decide whether we're loading/unloading the context */
	cp_bra (ctx, AUTO_SAVE, PENDING, cp_setup_save);
	cp_bra (ctx, USER_SAVE, PENDING, cp_setup_save);

	cp_name(ctx, cp_check_load);
	cp_bra (ctx, AUTO_LOAD, PENDING, cp_setup_auto_load);
	cp_bra (ctx, USER_LOAD, PENDING, cp_setup_load);
	cp_bra (ctx, ALWAYS, TRUE, cp_prepare_exit);

	/* setup for context load */
	cp_name(ctx, cp_setup_auto_load);
	cp_out (ctx, CP_DISABLE1);
	cp_out (ctx, CP_DISABLE2);
	cp_out (ctx, CP_ENABLE);
	cp_out (ctx, CP_NEXT_TO_SWAP);
	cp_set (ctx, UNK01, SET);
	cp_name(ctx, cp_setup_load);
	cp_out (ctx, CP_NEWCTX);
	cp_wait(ctx, NEWCTX, BUSY);
	cp_set (ctx, UNK1D, CLEAR);
	cp_set (ctx, SWAP_DIRECTION, LOAD);
	cp_bra (ctx, UNK0B, SET, cp_prepare_exit);
	cp_bra (ctx, ALWAYS, TRUE, cp_swap_state);

	/* setup for context save */
	cp_name(ctx, cp_setup_save);
	cp_set (ctx, UNK1D, SET);
	cp_wait(ctx, STATUS, BUSY);
	cp_wait(ctx, INTR, PENDING);
	cp_bra (ctx, STATUS, BUSY, cp_setup_save);
	cp_set (ctx, UNK01, SET);
	cp_set (ctx, SWAP_DIRECTION, SAVE);

	/* general PGRAPH state */
	cp_name(ctx, cp_swap_state);
	cp_set (ctx, UNK03, SET);
	cp_pos (ctx, 0x00004/4);
	cp_ctx (ctx, 0x400828, 1); /* needed. otherwise, flickering happens. */
	cp_pos (ctx, 0x00100/4);
	nv50_gr_construct_mmio(ctx);
	nv50_gr_construct_xfer1(ctx);
	nv50_gr_construct_xfer2(ctx);

	cp_bra (ctx, SWAP_DIRECTION, SAVE, cp_check_load);

	cp_set (ctx, UNK20, SET);
	cp_set (ctx, SWAP_DIRECTION, SAVE); /* no idea why this is needed, but fixes at least one lockup. */
	cp_lsr (ctx, ctx->ctxvals_base);
	cp_out (ctx, CP_SET_XFER_POINTER);
	cp_lsr (ctx, 4);
	cp_out (ctx, CP_SEEK_1);
	cp_out (ctx, CP_XFER_1);
	cp_wait(ctx, XFER, BUSY);

	/* pre-exit state updates */
	cp_name(ctx, cp_prepare_exit);
	cp_set (ctx, UNK01, CLEAR);
	cp_set (ctx, UNK03, CLEAR);
	cp_set (ctx, UNK1D, CLEAR);

	cp_bra (ctx, USER_SAVE, PENDING, cp_exit);
	cp_out (ctx, CP_NEXT_TO_CURRENT);

	cp_name(ctx, cp_exit);
	cp_set (ctx, USER_SAVE, NOT_PENDING);
	cp_set (ctx, USER_LOAD, NOT_PENDING);
	cp_set (ctx, XFER_SWITCH, DISABLE);
	cp_set (ctx, STATE, STOPPED);
	cp_out (ctx, CP_END);
	ctx->ctxvals_pos += 0x400; /* padding... no idea why you need it */

	return 0;
}

void
nv50_grctx_fill(struct nvkm_device *device, struct nvkm_gpuobj *mem)
{
	nv50_grctx_generate(&(struct nvkm_grctx) {
			     .device = device,
			     .mode = NVKM_GRCTX_VALS,
			     .data = mem,
			   });
}

int
nv50_grctx_init(struct nvkm_device *device, u32 *size)
{
	u32 *ctxprog = kmalloc(512 * 4, GFP_KERNEL), i;
	struct nvkm_grctx ctx = {
		.device = device,
		.mode = NVKM_GRCTX_PROG,
		.ucode = ctxprog,
		.ctxprog_max = 512,
	};

	if (!ctxprog)
		return -ENOMEM;
	nv50_grctx_generate(&ctx);

	nvkm_wr32(device, 0x400324, 0);
	for (i = 0; i < ctx.ctxprog_len; i++)
		nvkm_wr32(device, 0x400328, ctxprog[i]);
	*size = ctx.ctxvals_pos * 4;
	kfree(ctxprog);
	return 0;
}

/*
 * Constructs MMIO part of ctxprog and ctxvals. Just a matter of knowing which
 * registers to save/restore and the default values for them.
 */

static void
nv50_gr_construct_mmio_ddata(struct nvkm_grctx *ctx);

static void
nv50_gr_construct_mmio(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i, j;
	int offset, base;
	u32 units = nvkm_rd32(device, 0x1540);

	/* 0800: DISPATCH */
	cp_ctx(ctx, 0x400808, 7);
	gr_def(ctx, 0x400814, 0x00000030);
	cp_ctx(ctx, 0x400834, 0x32);
	if (device->chipset == 0x50) {
		gr_def(ctx, 0x400834, 0xff400040);
		gr_def(ctx, 0x400838, 0xfff00080);
		gr_def(ctx, 0x40083c, 0xfff70090);
		gr_def(ctx, 0x400840, 0xffe806a8);
	}
	gr_def(ctx, 0x400844, 0x00000002);
	if (IS_NVA3F(device->chipset))
		gr_def(ctx, 0x400894, 0x00001000);
	gr_def(ctx, 0x4008e8, 0x00000003);
	gr_def(ctx, 0x4008ec, 0x00001000);
	if (device->chipset == 0x50)
		cp_ctx(ctx, 0x400908, 0xb);
	else if (device->chipset < 0xa0)
		cp_ctx(ctx, 0x400908, 0xc);
	else
		cp_ctx(ctx, 0x400908, 0xe);

	if (device->chipset >= 0xa0)
		cp_ctx(ctx, 0x400b00, 0x1);
	if (IS_NVA3F(device->chipset)) {
		cp_ctx(ctx, 0x400b10, 0x1);
		gr_def(ctx, 0x400b10, 0x0001629d);
		cp_ctx(ctx, 0x400b20, 0x1);
		gr_def(ctx, 0x400b20, 0x0001629d);
	}

	nv50_gr_construct_mmio_ddata(ctx);

	/* 0C00: VFETCH */
	cp_ctx(ctx, 0x400c08, 0x2);
	gr_def(ctx, 0x400c08, 0x0000fe0c);

	/* 1000 */
	if (device->chipset < 0xa0) {
		cp_ctx(ctx, 0x401008, 0x4);
		gr_def(ctx, 0x401014, 0x00001000);
	} else if (!IS_NVA3F(device->chipset)) {
		cp_ctx(ctx, 0x401008, 0x5);
		gr_def(ctx, 0x401018, 0x00001000);
	} else {
		cp_ctx(ctx, 0x401008, 0x5);
		gr_def(ctx, 0x401018, 0x00004000);
	}

	/* 1400 */
	cp_ctx(ctx, 0x401400, 0x8);
	cp_ctx(ctx, 0x401424, 0x3);
	if (device->chipset == 0x50)
		gr_def(ctx, 0x40142c, 0x0001fd87);
	else
		gr_def(ctx, 0x40142c, 0x00000187);
	cp_ctx(ctx, 0x401540, 0x5);
	gr_def(ctx, 0x401550, 0x00001018);

	/* 1800: STREAMOUT */
	cp_ctx(ctx, 0x401814, 0x1);
	gr_def(ctx, 0x401814, 0x000000ff);
	if (device->chipset == 0x50) {
		cp_ctx(ctx, 0x40181c, 0xe);
		gr_def(ctx, 0x401850, 0x00000004);
	} else if (device->chipset < 0xa0) {
		cp_ctx(ctx, 0x40181c, 0xf);
		gr_def(ctx, 0x401854, 0x00000004);
	} else {
		cp_ctx(ctx, 0x40181c, 0x13);
		gr_def(ctx, 0x401864, 0x00000004);
	}

	/* 1C00 */
	cp_ctx(ctx, 0x401c00, 0x1);
	switch (device->chipset) {
	case 0x50:
		gr_def(ctx, 0x401c00, 0x0001005f);
		break;
	case 0x84:
	case 0x86:
	case 0x94:
		gr_def(ctx, 0x401c00, 0x044d00df);
		break;
	case 0x92:
	case 0x96:
	case 0x98:
	case 0xa0:
	case 0xaa:
	case 0xac:
		gr_def(ctx, 0x401c00, 0x042500df);
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
	case 0xaf:
		gr_def(ctx, 0x401c00, 0x142500df);
		break;
	}

	/* 2000 */

	/* 2400 */
	cp_ctx(ctx, 0x402400, 0x1);
	if (device->chipset == 0x50)
		cp_ctx(ctx, 0x402408, 0x1);
	else
		cp_ctx(ctx, 0x402408, 0x2);
	gr_def(ctx, 0x402408, 0x00000600);

	/* 2800: CSCHED */
	cp_ctx(ctx, 0x402800, 0x1);
	if (device->chipset == 0x50)
		gr_def(ctx, 0x402800, 0x00000006);

	/* 2C00: ZCULL */
	cp_ctx(ctx, 0x402c08, 0x6);
	if (device->chipset != 0x50)
		gr_def(ctx, 0x402c14, 0x01000000);
	gr_def(ctx, 0x402c18, 0x000000ff);
	if (device->chipset == 0x50)
		cp_ctx(ctx, 0x402ca0, 0x1);
	else
		cp_ctx(ctx, 0x402ca0, 0x2);
	if (device->chipset < 0xa0)
		gr_def(ctx, 0x402ca0, 0x00000400);
	else if (!IS_NVA3F(device->chipset))
		gr_def(ctx, 0x402ca0, 0x00000800);
	else
		gr_def(ctx, 0x402ca0, 0x00000400);
	cp_ctx(ctx, 0x402cac, 0x4);

	/* 3000: ENG2D */
	cp_ctx(ctx, 0x403004, 0x1);
	gr_def(ctx, 0x403004, 0x00000001);

	/* 3400 */
	if (device->chipset >= 0xa0) {
		cp_ctx(ctx, 0x403404, 0x1);
		gr_def(ctx, 0x403404, 0x00000001);
	}

	/* 5000: CCACHE */
	cp_ctx(ctx, 0x405000, 0x1);
	switch (device->chipset) {
	case 0x50:
		gr_def(ctx, 0x405000, 0x00300080);
		break;
	case 0x84:
	case 0xa0:
	case 0xa3:
	case 0xa5:
	case 0xa8:
	case 0xaa:
	case 0xac:
	case 0xaf:
		gr_def(ctx, 0x405000, 0x000e0080);
		break;
	case 0x86:
	case 0x92:
	case 0x94:
	case 0x96:
	case 0x98:
		gr_def(ctx, 0x405000, 0x00000080);
		break;
	}
	cp_ctx(ctx, 0x405014, 0x1);
	gr_def(ctx, 0x405014, 0x00000004);
	cp_ctx(ctx, 0x40501c, 0x1);
	cp_ctx(ctx, 0x405024, 0x1);
	cp_ctx(ctx, 0x40502c, 0x1);

	/* 6000? */
	if (device->chipset == 0x50)
		cp_ctx(ctx, 0x4063e0, 0x1);

	/* 6800: M2MF */
	if (device->chipset < 0x90) {
		cp_ctx(ctx, 0x406814, 0x2b);
		gr_def(ctx, 0x406818, 0x00000f80);
		gr_def(ctx, 0x406860, 0x007f0080);
		gr_def(ctx, 0x40689c, 0x007f0080);
	} else {
		cp_ctx(ctx, 0x406814, 0x4);
		if (device->chipset == 0x98)
			gr_def(ctx, 0x406818, 0x00000f80);
		else
			gr_def(ctx, 0x406818, 0x00001f80);
		if (IS_NVA3F(device->chipset))
			gr_def(ctx, 0x40681c, 0x00000030);
		cp_ctx(ctx, 0x406830, 0x3);
	}

	/* 7000: per-ROP group state */
	for (i = 0; i < 8; i++) {
		if (units & (1<<(i+16))) {
			cp_ctx(ctx, 0x407000 + (i<<8), 3);
			if (device->chipset == 0x50)
				gr_def(ctx, 0x407000 + (i<<8), 0x1b74f820);
			else if (device->chipset != 0xa5)
				gr_def(ctx, 0x407000 + (i<<8), 0x3b74f821);
			else
				gr_def(ctx, 0x407000 + (i<<8), 0x7b74f821);
			gr_def(ctx, 0x407004 + (i<<8), 0x89058001);

			if (device->chipset == 0x50) {
				cp_ctx(ctx, 0x407010 + (i<<8), 1);
			} else if (device->chipset < 0xa0) {
				cp_ctx(ctx, 0x407010 + (i<<8), 2);
				gr_def(ctx, 0x407010 + (i<<8), 0x00001000);
				gr_def(ctx, 0x407014 + (i<<8), 0x0000001f);
			} else {
				cp_ctx(ctx, 0x407010 + (i<<8), 3);
				gr_def(ctx, 0x407010 + (i<<8), 0x00001000);
				if (device->chipset != 0xa5)
					gr_def(ctx, 0x407014 + (i<<8), 0x000000ff);
				else
					gr_def(ctx, 0x407014 + (i<<8), 0x000001ff);
			}

			cp_ctx(ctx, 0x407080 + (i<<8), 4);
			if (device->chipset != 0xa5)
				gr_def(ctx, 0x407080 + (i<<8), 0x027c10fa);
			else
				gr_def(ctx, 0x407080 + (i<<8), 0x827c10fa);
			if (device->chipset == 0x50)
				gr_def(ctx, 0x407084 + (i<<8), 0x000000c0);
			else
				gr_def(ctx, 0x407084 + (i<<8), 0x400000c0);
			gr_def(ctx, 0x407088 + (i<<8), 0xb7892080);

			if (device->chipset < 0xa0)
				cp_ctx(ctx, 0x407094 + (i<<8), 1);
			else if (!IS_NVA3F(device->chipset))
				cp_ctx(ctx, 0x407094 + (i<<8), 3);
			else {
				cp_ctx(ctx, 0x407094 + (i<<8), 4);
				gr_def(ctx, 0x4070a0 + (i<<8), 1);
			}
		}
	}

	cp_ctx(ctx, 0x407c00, 0x3);
	if (device->chipset < 0x90)
		gr_def(ctx, 0x407c00, 0x00010040);
	else if (device->chipset < 0xa0)
		gr_def(ctx, 0x407c00, 0x00390040);
	else
		gr_def(ctx, 0x407c00, 0x003d0040);
	gr_def(ctx, 0x407c08, 0x00000022);
	if (device->chipset >= 0xa0) {
		cp_ctx(ctx, 0x407c10, 0x3);
		cp_ctx(ctx, 0x407c20, 0x1);
		cp_ctx(ctx, 0x407c2c, 0x1);
	}

	if (device->chipset < 0xa0) {
		cp_ctx(ctx, 0x407d00, 0x9);
	} else {
		cp_ctx(ctx, 0x407d00, 0x15);
	}
	if (device->chipset == 0x98)
		gr_def(ctx, 0x407d08, 0x00380040);
	else {
		if (device->chipset < 0x90)
			gr_def(ctx, 0x407d08, 0x00010040);
		else if (device->chipset < 0xa0)
			gr_def(ctx, 0x407d08, 0x00390040);
		else {
			if (device->fb->ram->type != NVKM_RAM_TYPE_GDDR5)
				gr_def(ctx, 0x407d08, 0x003d0040);
			else
				gr_def(ctx, 0x407d08, 0x003c0040);
		}
		gr_def(ctx, 0x407d0c, 0x00000022);
	}

	/* 8000+: per-TP state */
	for (i = 0; i < 10; i++) {
		if (units & (1<<i)) {
			if (device->chipset < 0xa0)
				base = 0x408000 + (i<<12);
			else
				base = 0x408000 + (i<<11);
			if (device->chipset < 0xa0)
				offset = base + 0xc00;
			else
				offset = base + 0x80;
			cp_ctx(ctx, offset + 0x00, 1);
			gr_def(ctx, offset + 0x00, 0x0000ff0a);
			cp_ctx(ctx, offset + 0x08, 1);

			/* per-MP state */
			for (j = 0; j < (device->chipset < 0xa0 ? 2 : 4); j++) {
				if (!(units & (1 << (j+24)))) continue;
				if (device->chipset < 0xa0)
					offset = base + 0x200 + (j<<7);
				else
					offset = base + 0x100 + (j<<7);
				cp_ctx(ctx, offset, 0x20);
				gr_def(ctx, offset + 0x00, 0x01800000);
				gr_def(ctx, offset + 0x04, 0x00160000);
				gr_def(ctx, offset + 0x08, 0x01800000);
				gr_def(ctx, offset + 0x18, 0x0003ffff);
				switch (device->chipset) {
				case 0x50:
					gr_def(ctx, offset + 0x1c, 0x00080000);
					break;
				case 0x84:
					gr_def(ctx, offset + 0x1c, 0x00880000);
					break;
				case 0x86:
					gr_def(ctx, offset + 0x1c, 0x018c0000);
					break;
				case 0x92:
				case 0x96:
				case 0x98:
					gr_def(ctx, offset + 0x1c, 0x118c0000);
					break;
				case 0x94:
					gr_def(ctx, offset + 0x1c, 0x10880000);
					break;
				case 0xa0:
				case 0xa5:
					gr_def(ctx, offset + 0x1c, 0x310c0000);
					break;
				case 0xa3:
				case 0xa8:
				case 0xaa:
				case 0xac:
				case 0xaf:
					gr_def(ctx, offset + 0x1c, 0x300c0000);
					break;
				}
				gr_def(ctx, offset + 0x40, 0x00010401);
				if (device->chipset == 0x50)
					gr_def(ctx, offset + 0x48, 0x00000040);
				else
					gr_def(ctx, offset + 0x48, 0x00000078);
				gr_def(ctx, offset + 0x50, 0x000000bf);
				gr_def(ctx, offset + 0x58, 0x00001210);
				if (device->chipset == 0x50)
					gr_def(ctx, offset + 0x5c, 0x00000080);
				else
					gr_def(ctx, offset + 0x5c, 0x08000080);
				if (device->chipset >= 0xa0)
					gr_def(ctx, offset + 0x68, 0x0000003e);
			}

			if (device->chipset < 0xa0)
				cp_ctx(ctx, base + 0x300, 0x4);
			else
				cp_ctx(ctx, base + 0x300, 0x5);
			if (device->chipset == 0x50)
				gr_def(ctx, base + 0x304, 0x00007070);
			else if (device->chipset < 0xa0)
				gr_def(ctx, base + 0x304, 0x00027070);
			else if (!IS_NVA3F(device->chipset))
				gr_def(ctx, base + 0x304, 0x01127070);
			else
				gr_def(ctx, base + 0x304, 0x05127070);

			if (device->chipset < 0xa0)
				cp_ctx(ctx, base + 0x318, 1);
			else
				cp_ctx(ctx, base + 0x320, 1);
			if (device->chipset == 0x50)
				gr_def(ctx, base + 0x318, 0x0003ffff);
			else if (device->chipset < 0xa0)
				gr_def(ctx, base + 0x318, 0x03ffffff);
			else
				gr_def(ctx, base + 0x320, 0x07ffffff);

			if (device->chipset < 0xa0)
				cp_ctx(ctx, base + 0x324, 5);
			else
				cp_ctx(ctx, base + 0x328, 4);

			if (device->chipset < 0xa0) {
				cp_ctx(ctx, base + 0x340, 9);
				offset = base + 0x340;
			} else if (!IS_NVA3F(device->chipset)) {
				cp_ctx(ctx, base + 0x33c, 0xb);
				offset = base + 0x344;
			} else {
				cp_ctx(ctx, base + 0x33c, 0xd);
				offset = base + 0x344;
			}
			gr_def(ctx, offset + 0x0, 0x00120407);
			gr_def(ctx, offset + 0x4, 0x05091507);
			if (device->chipset == 0x84)
				gr_def(ctx, offset + 0x8, 0x05100202);
			else
				gr_def(ctx, offset + 0x8, 0x05010202);
			gr_def(ctx, offset + 0xc, 0x00030201);
			if (device->chipset == 0xa3)
				cp_ctx(ctx, base + 0x36c, 1);

			cp_ctx(ctx, base + 0x400, 2);
			gr_def(ctx, base + 0x404, 0x00000040);
			cp_ctx(ctx, base + 0x40c, 2);
			gr_def(ctx, base + 0x40c, 0x0d0c0b0a);
			gr_def(ctx, base + 0x410, 0x00141210);

			if (device->chipset < 0xa0)
				offset = base + 0x800;
			else
				offset = base + 0x500;
			cp_ctx(ctx, offset, 6);
			gr_def(ctx, offset + 0x0, 0x000001f0);
			gr_def(ctx, offset + 0x4, 0x00000001);
			gr_def(ctx, offset + 0x8, 0x00000003);
			if (device->chipset == 0x50 || IS_NVAAF(device->chipset))
				gr_def(ctx, offset + 0xc, 0x00008000);
			gr_def(ctx, offset + 0x14, 0x00039e00);
			cp_ctx(ctx, offset + 0x1c, 2);
			if (device->chipset == 0x50)
				gr_def(ctx, offset + 0x1c, 0x00000040);
			else
				gr_def(ctx, offset + 0x1c, 0x00000100);
			gr_def(ctx, offset + 0x20, 0x00003800);

			if (device->chipset >= 0xa0) {
				cp_ctx(ctx, base + 0x54c, 2);
				if (!IS_NVA3F(device->chipset))
					gr_def(ctx, base + 0x54c, 0x003fe006);
				else
					gr_def(ctx, base + 0x54c, 0x003fe007);
				gr_def(ctx, base + 0x550, 0x003fe000);
			}

			if (device->chipset < 0xa0)
				offset = base + 0xa00;
			else
				offset = base + 0x680;
			cp_ctx(ctx, offset, 1);
			gr_def(ctx, offset, 0x00404040);

			if (device->chipset < 0xa0)
				offset = base + 0xe00;
			else
				offset = base + 0x700;
			cp_ctx(ctx, offset, 2);
			if (device->chipset < 0xa0)
				gr_def(ctx, offset, 0x0077f005);
			else if (device->chipset == 0xa5)
				gr_def(ctx, offset, 0x6cf7f007);
			else if (device->chipset == 0xa8)
				gr_def(ctx, offset, 0x6cfff007);
			else if (device->chipset == 0xac)
				gr_def(ctx, offset, 0x0cfff007);
			else
				gr_def(ctx, offset, 0x0cf7f007);
			if (device->chipset == 0x50)
				gr_def(ctx, offset + 0x4, 0x00007fff);
			else if (device->chipset < 0xa0)
				gr_def(ctx, offset + 0x4, 0x003f7fff);
			else
				gr_def(ctx, offset + 0x4, 0x02bf7fff);
			cp_ctx(ctx, offset + 0x2c, 1);
			if (device->chipset == 0x50) {
				cp_ctx(ctx, offset + 0x50, 9);
				gr_def(ctx, offset + 0x54, 0x000003ff);
				gr_def(ctx, offset + 0x58, 0x00000003);
				gr_def(ctx, offset + 0x5c, 0x00000003);
				gr_def(ctx, offset + 0x60, 0x000001ff);
				gr_def(ctx, offset + 0x64, 0x0000001f);
				gr_def(ctx, offset + 0x68, 0x0000000f);
				gr_def(ctx, offset + 0x6c, 0x0000000f);
			} else if (device->chipset < 0xa0) {
				cp_ctx(ctx, offset + 0x50, 1);
				cp_ctx(ctx, offset + 0x70, 1);
			} else {
				cp_ctx(ctx, offset + 0x50, 1);
				cp_ctx(ctx, offset + 0x60, 5);
			}
		}
	}
}

static void
dd_emit(struct nvkm_grctx *ctx, int num, u32 val) {
	int i;
	if (val && ctx->mode == NVKM_GRCTX_VALS) {
		for (i = 0; i < num; i++)
			nvkm_wo32(ctx->data, 4 * (ctx->ctxvals_pos + i), val);
	}
	ctx->ctxvals_pos += num;
}

static void
nv50_gr_construct_mmio_ddata(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int base, num;
	base = ctx->ctxvals_pos;

	/* tesla state */
	dd_emit(ctx, 1, 0);	/* 00000001 UNK0F90 */
	dd_emit(ctx, 1, 0);	/* 00000001 UNK135C */

	/* SRC_TIC state */
	dd_emit(ctx, 1, 0);	/* 00000007 SRC_TILE_MODE_Z */
	dd_emit(ctx, 1, 2);	/* 00000007 SRC_TILE_MODE_Y */
	dd_emit(ctx, 1, 1);	/* 00000001 SRC_LINEAR #1 */
	dd_emit(ctx, 1, 0);	/* 000000ff SRC_ADDRESS_HIGH */
	dd_emit(ctx, 1, 0);	/* 00000001 SRC_SRGB */
	if (device->chipset >= 0x94)
		dd_emit(ctx, 1, 0);	/* 00000003 eng2d UNK0258 */
	dd_emit(ctx, 1, 1);	/* 00000fff SRC_DEPTH */
	dd_emit(ctx, 1, 0x100);	/* 0000ffff SRC_HEIGHT */

	/* turing state */
	dd_emit(ctx, 1, 0);		/* 0000000f TEXTURES_LOG2 */
	dd_emit(ctx, 1, 0);		/* 0000000f SAMPLERS_LOG2 */
	dd_emit(ctx, 1, 0);		/* 000000ff CB_DEF_ADDRESS_HIGH */
	dd_emit(ctx, 1, 0);		/* ffffffff CB_DEF_ADDRESS_LOW */
	dd_emit(ctx, 1, 0);		/* ffffffff SHARED_SIZE */
	dd_emit(ctx, 1, 2);		/* ffffffff REG_MODE */
	dd_emit(ctx, 1, 1);		/* 0000ffff BLOCK_ALLOC_THREADS */
	dd_emit(ctx, 1, 1);		/* 00000001 LANES32 */
	dd_emit(ctx, 1, 0);		/* 000000ff UNK370 */
	dd_emit(ctx, 1, 0);		/* 000000ff USER_PARAM_UNK */
	dd_emit(ctx, 1, 0);		/* 000000ff USER_PARAM_COUNT */
	dd_emit(ctx, 1, 1);		/* 000000ff UNK384 bits 8-15 */
	dd_emit(ctx, 1, 0x3fffff);	/* 003fffff TIC_LIMIT */
	dd_emit(ctx, 1, 0x1fff);	/* 000fffff TSC_LIMIT */
	dd_emit(ctx, 1, 0);		/* 0000ffff CB_ADDR_INDEX */
	dd_emit(ctx, 1, 1);		/* 000007ff BLOCKDIM_X */
	dd_emit(ctx, 1, 1);		/* 000007ff BLOCKDIM_XMY */
	dd_emit(ctx, 1, 0);		/* 00000001 BLOCKDIM_XMY_OVERFLOW */
	dd_emit(ctx, 1, 1);		/* 0003ffff BLOCKDIM_XMYMZ */
	dd_emit(ctx, 1, 1);		/* 000007ff BLOCKDIM_Y */
	dd_emit(ctx, 1, 1);		/* 0000007f BLOCKDIM_Z */
	dd_emit(ctx, 1, 4);		/* 000000ff CP_REG_ALLOC_TEMP */
	dd_emit(ctx, 1, 1);		/* 00000001 BLOCKDIM_DIRTY */
	if (IS_NVA3F(device->chipset))
		dd_emit(ctx, 1, 0);	/* 00000003 UNK03E8 */
	dd_emit(ctx, 1, 1);		/* 0000007f BLOCK_ALLOC_HALFWARPS */
	dd_emit(ctx, 1, 1);		/* 00000007 LOCAL_WARPS_NO_CLAMP */
	dd_emit(ctx, 1, 7);		/* 00000007 LOCAL_WARPS_LOG_ALLOC */
	dd_emit(ctx, 1, 1);		/* 00000007 STACK_WARPS_NO_CLAMP */
	dd_emit(ctx, 1, 7);		/* 00000007 STACK_WARPS_LOG_ALLOC */
	dd_emit(ctx, 1, 1);		/* 00001fff BLOCK_ALLOC_REGSLOTS_PACKED */
	dd_emit(ctx, 1, 1);		/* 00001fff BLOCK_ALLOC_REGSLOTS_STRIDED */
	dd_emit(ctx, 1, 1);		/* 000007ff BLOCK_ALLOC_THREADS */

	/* compat 2d state */
	if (device->chipset == 0x50) {
		dd_emit(ctx, 4, 0);		/* 0000ffff clip X, Y, W, H */

		dd_emit(ctx, 1, 1);		/* ffffffff chroma COLOR_FORMAT */

		dd_emit(ctx, 1, 1);		/* ffffffff pattern COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff pattern SHAPE */
		dd_emit(ctx, 1, 1);		/* ffffffff pattern PATTERN_SELECT */

		dd_emit(ctx, 1, 0xa);		/* ffffffff surf2d SRC_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff surf2d DMA_SRC */
		dd_emit(ctx, 1, 0);		/* 000000ff surf2d SRC_ADDRESS_HIGH */
		dd_emit(ctx, 1, 0);		/* ffffffff surf2d SRC_ADDRESS_LOW */
		dd_emit(ctx, 1, 0x40);		/* 0000ffff surf2d SRC_PITCH */
		dd_emit(ctx, 1, 0);		/* 0000000f surf2d SRC_TILE_MODE_Z */
		dd_emit(ctx, 1, 2);		/* 0000000f surf2d SRC_TILE_MODE_Y */
		dd_emit(ctx, 1, 0x100);		/* ffffffff surf2d SRC_HEIGHT */
		dd_emit(ctx, 1, 1);		/* 00000001 surf2d SRC_LINEAR */
		dd_emit(ctx, 1, 0x100);		/* ffffffff surf2d SRC_WIDTH */

		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_B_X */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_B_Y */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_C_X */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_C_Y */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_D_X */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect CLIP_D_Y */
		dd_emit(ctx, 1, 1);		/* ffffffff gdirect COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff gdirect OPERATION */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect POINT_X */
		dd_emit(ctx, 1, 0);		/* 0000ffff gdirect POINT_Y */

		dd_emit(ctx, 1, 0);		/* 0000ffff blit SRC_Y */
		dd_emit(ctx, 1, 0);		/* ffffffff blit OPERATION */

		dd_emit(ctx, 1, 0);		/* ffffffff ifc OPERATION */

		dd_emit(ctx, 1, 0);		/* ffffffff iifc INDEX_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff iifc LUT_OFFSET */
		dd_emit(ctx, 1, 4);		/* ffffffff iifc COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff iifc OPERATION */
	}

	/* m2mf state */
	dd_emit(ctx, 1, 0);		/* ffffffff m2mf LINE_COUNT */
	dd_emit(ctx, 1, 0);		/* ffffffff m2mf LINE_LENGTH_IN */
	dd_emit(ctx, 2, 0);		/* ffffffff m2mf OFFSET_IN, OFFSET_OUT */
	dd_emit(ctx, 1, 1);		/* ffffffff m2mf TILING_DEPTH_OUT */
	dd_emit(ctx, 1, 0x100);		/* ffffffff m2mf TILING_HEIGHT_OUT */
	dd_emit(ctx, 1, 0);		/* ffffffff m2mf TILING_POSITION_OUT_Z */
	dd_emit(ctx, 1, 1);		/* 00000001 m2mf LINEAR_OUT */
	dd_emit(ctx, 2, 0);		/* 0000ffff m2mf TILING_POSITION_OUT_X, Y */
	dd_emit(ctx, 1, 0x100);		/* ffffffff m2mf TILING_PITCH_OUT */
	dd_emit(ctx, 1, 1);		/* ffffffff m2mf TILING_DEPTH_IN */
	dd_emit(ctx, 1, 0x100);		/* ffffffff m2mf TILING_HEIGHT_IN */
	dd_emit(ctx, 1, 0);		/* ffffffff m2mf TILING_POSITION_IN_Z */
	dd_emit(ctx, 1, 1);		/* 00000001 m2mf LINEAR_IN */
	dd_emit(ctx, 2, 0);		/* 0000ffff m2mf TILING_POSITION_IN_X, Y */
	dd_emit(ctx, 1, 0x100);		/* ffffffff m2mf TILING_PITCH_IN */

	/* more compat 2d state */
	if (device->chipset == 0x50) {
		dd_emit(ctx, 1, 1);		/* ffffffff line COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff line OPERATION */

		dd_emit(ctx, 1, 1);		/* ffffffff triangle COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff triangle OPERATION */

		dd_emit(ctx, 1, 0);		/* 0000000f sifm TILE_MODE_Z */
		dd_emit(ctx, 1, 2);		/* 0000000f sifm TILE_MODE_Y */
		dd_emit(ctx, 1, 0);		/* 000000ff sifm FORMAT_FILTER */
		dd_emit(ctx, 1, 1);		/* 000000ff sifm FORMAT_ORIGIN */
		dd_emit(ctx, 1, 0);		/* 0000ffff sifm SRC_PITCH */
		dd_emit(ctx, 1, 1);		/* 00000001 sifm SRC_LINEAR */
		dd_emit(ctx, 1, 0);		/* 000000ff sifm SRC_OFFSET_HIGH */
		dd_emit(ctx, 1, 0);		/* ffffffff sifm SRC_OFFSET */
		dd_emit(ctx, 1, 0);		/* 0000ffff sifm SRC_HEIGHT */
		dd_emit(ctx, 1, 0);		/* 0000ffff sifm SRC_WIDTH */
		dd_emit(ctx, 1, 3);		/* ffffffff sifm COLOR_FORMAT */
		dd_emit(ctx, 1, 0);		/* ffffffff sifm OPERATION */

		dd_emit(ctx, 1, 0);		/* ffffffff sifc OPERATION */
	}

	/* tesla state */
	dd_emit(ctx, 1, 0);		/* 0000000f GP_TEXTURES_LOG2 */
	dd_emit(ctx, 1, 0);		/* 0000000f GP_SAMPLERS_LOG2 */
	dd_emit(ctx, 1, 0);		/* 000000ff */
	dd_emit(ctx, 1, 0);		/* ffffffff */
	dd_emit(ctx, 1, 4);		/* 000000ff UNK12B0_0 */
	dd_emit(ctx, 1, 0x70);		/* 000000ff UNK12B0_1 */
	dd_emit(ctx, 1, 0x80);		/* 000000ff UNK12B0_3 */
	dd_emit(ctx, 1, 0);		/* 000000ff UNK12B0_2 */
	dd_emit(ctx, 1, 0);		/* 0000000f FP_TEXTURES_LOG2 */
	dd_emit(ctx, 1, 0);		/* 0000000f FP_SAMPLERS_LOG2 */
	if (IS_NVA3F(device->chipset)) {
		dd_emit(ctx, 1, 0);	/* ffffffff */
		dd_emit(ctx, 1, 0);	/* 0000007f MULTISAMPLE_SAMPLES_LOG2 */
	} else {
		dd_emit(ctx, 1, 0);	/* 0000000f MULTISAMPLE_SAMPLES_LOG2 */
	}
	dd_emit(ctx, 1, 0xc);		/* 000000ff SEMANTIC_COLOR.BFC0_ID */
	if (device->chipset != 0x50)
		dd_emit(ctx, 1, 0);	/* 00000001 SEMANTIC_COLOR.CLMP_EN */
	dd_emit(ctx, 1, 8);		/* 000000ff SEMANTIC_COLOR.COLR_NR */
	dd_emit(ctx, 1, 0x14);		/* 000000ff SEMANTIC_COLOR.FFC0_ID */
	if (device->chipset == 0x50) {
		dd_emit(ctx, 1, 0);	/* 000000ff SEMANTIC_LAYER */
		dd_emit(ctx, 1, 0);	/* 00000001 */
	} else {
		dd_emit(ctx, 1, 0);	/* 00000001 SEMANTIC_PTSZ.ENABLE */
		dd_emit(ctx, 1, 0x29);	/* 000000ff SEMANTIC_PTSZ.PTSZ_ID */
		dd_emit(ctx, 1, 0x27);	/* 000000ff SEMANTIC_PRIM */
		dd_emit(ctx, 1, 0x26);	/* 000000ff SEMANTIC_LAYER */
		dd_emit(ctx, 1, 8);	/* 0000000f SMENATIC_CLIP.CLIP_HIGH */
		dd_emit(ctx, 1, 4);	/* 000000ff SEMANTIC_CLIP.CLIP_LO */
		dd_emit(ctx, 1, 0x27);	/* 000000ff UNK0FD4 */
		dd_emit(ctx, 1, 0);	/* 00000001 UNK1900 */
	}
	dd_emit(ctx, 1, 0);		/* 00000007 RT_CONTROL_MAP0 */
	dd_emit(ctx, 1, 1);		/* 00000007 RT_CONTROL_MAP1 */
	dd_emit(ctx, 1, 2);		/* 00000007 RT_CONTROL_MAP2 */
	dd_emit(ctx, 1, 3);		/* 00000007 RT_CONTROL_MAP3 */
	dd_emit(ctx, 1, 4);		/* 00000007 RT_CONTROL_MAP4 */
	dd_emit(ctx, 1, 5);		/* 00000007 RT_CONTROL_MAP5 */
	dd_emit(ctx, 1, 6);		/* 00000007 RT_CONTROL_MAP6 */
	dd_emit(ctx, 1, 7);		/* 00000007 RT_CONTROL_MAP7 */
	dd_emit(ctx, 1, 1);		/* 0000000f RT_CONTROL_COUNT */
	dd_emit(ctx, 8, 0);		/* 00000001 RT_HORIZ_UNK */
	dd_emit(ctx, 8, 0);		/* ffffffff RT_ADDRESS_LOW */
	dd_emit(ctx, 1, 0xcf);		/* 000000ff RT_FORMAT */
	dd_emit(ctx, 7, 0);		/* 000000ff RT_FORMAT */
	if (device->chipset != 0x50)
		dd_emit(ctx, 3, 0);	/* 1, 1, 1 */
	else
		dd_emit(ctx, 2, 0);	/* 1, 1 */
	dd_emit(ctx, 1, 0);		/* ffffffff GP_ENABLE */
	dd_emit(ctx, 1, 0x80);		/* 0000ffff GP_VERTEX_OUTPUT_COUNT*/
	dd_emit(ctx, 1, 4);		/* 000000ff GP_REG_ALLOC_RESULT */
	dd_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	if (IS_NVA3F(device->chipset)) {
		dd_emit(ctx, 1, 3);	/* 00000003 */
		dd_emit(ctx, 1, 0);	/* 00000001 UNK1418. Alone. */
	}
	if (device->chipset != 0x50)
		dd_emit(ctx, 1, 3);	/* 00000003 UNK15AC */
	dd_emit(ctx, 1, 1);		/* ffffffff RASTERIZE_ENABLE */
	dd_emit(ctx, 1, 0);		/* 00000001 FP_CONTROL.EXPORTS_Z */
	if (device->chipset != 0x50)
		dd_emit(ctx, 1, 0);	/* 00000001 FP_CONTROL.MULTIPLE_RESULTS */
	dd_emit(ctx, 1, 0x12);		/* 000000ff FP_INTERPOLANT_CTRL.COUNT */
	dd_emit(ctx, 1, 0x10);		/* 000000ff FP_INTERPOLANT_CTRL.COUNT_NONFLAT */
	dd_emit(ctx, 1, 0xc);		/* 000000ff FP_INTERPOLANT_CTRL.OFFSET */
	dd_emit(ctx, 1, 1);		/* 00000001 FP_INTERPOLANT_CTRL.UMASK.W */
	dd_emit(ctx, 1, 0);		/* 00000001 FP_INTERPOLANT_CTRL.UMASK.X */
	dd_emit(ctx, 1, 0);		/* 00000001 FP_INTERPOLANT_CTRL.UMASK.Y */
	dd_emit(ctx, 1, 0);		/* 00000001 FP_INTERPOLANT_CTRL.UMASK.Z */
	dd_emit(ctx, 1, 4);		/* 000000ff FP_RESULT_COUNT */
	dd_emit(ctx, 1, 2);		/* ffffffff REG_MODE */
	dd_emit(ctx, 1, 4);		/* 000000ff FP_REG_ALLOC_TEMP */
	if (device->chipset >= 0xa0)
		dd_emit(ctx, 1, 0);	/* ffffffff */
	dd_emit(ctx, 1, 0);		/* 00000001 GP_BUILTIN_RESULT_EN.LAYER_IDX */
	dd_emit(ctx, 1, 0);		/* ffffffff STRMOUT_ENABLE */
	dd_emit(ctx, 1, 0x3fffff);	/* 003fffff TIC_LIMIT */
	dd_emit(ctx, 1, 0x1fff);	/* 000fffff TSC_LIMIT */
	dd_emit(ctx, 1, 0);		/* 00000001 VERTEX_TWO_SIDE_ENABLE*/
	if (device->chipset != 0x50)
		dd_emit(ctx, 8, 0);	/* 00000001 */
	if (device->chipset >= 0xa0) {
		dd_emit(ctx, 1, 1);	/* 00000007 VTX_ATTR_DEFINE.COMP */
		dd_emit(ctx, 1, 1);	/* 00000007 VTX_ATTR_DEFINE.SIZE */
		dd_emit(ctx, 1, 2);	/* 00000007 VTX_ATTR_DEFINE.TYPE */
		dd_emit(ctx, 1, 0);	/* 000000ff VTX_ATTR_DEFINE.ATTR */
	}
	dd_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	dd_emit(ctx, 1, 0x14);		/* 0000001f ZETA_FORMAT */
	dd_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	dd_emit(ctx, 1, 0);		/* 0000000f VP_TEXTURES_LOG2 */
	dd_emit(ctx, 1, 0);		/* 0000000f VP_SAMPLERS_LOG2 */
	if (IS_NVA3F(device->chipset))
		dd_emit(ctx, 1, 0);	/* 00000001 */
	dd_emit(ctx, 1, 2);		/* 00000003 POLYGON_MODE_BACK */
	if (device->chipset >= 0xa0)
		dd_emit(ctx, 1, 0);	/* 00000003 VTX_ATTR_DEFINE.SIZE - 1 */
	dd_emit(ctx, 1, 0);		/* 0000ffff CB_ADDR_INDEX */
	if (device->chipset >= 0xa0)
		dd_emit(ctx, 1, 0);	/* 00000003 */
	dd_emit(ctx, 1, 0);		/* 00000001 CULL_FACE_ENABLE */
	dd_emit(ctx, 1, 1);		/* 00000003 CULL_FACE */
	dd_emit(ctx, 1, 0);		/* 00000001 FRONT_FACE */
	dd_emit(ctx, 1, 2);		/* 00000003 POLYGON_MODE_FRONT */
	dd_emit(ctx, 1, 0x1000);	/* 00007fff UNK141C */
	if (device->chipset != 0x50) {
		dd_emit(ctx, 1, 0xe00);		/* 7fff */
		dd_emit(ctx, 1, 0x1000);	/* 7fff */
		dd_emit(ctx, 1, 0x1e00);	/* 7fff */
	}
	dd_emit(ctx, 1, 0);		/* 00000001 BEGIN_END_ACTIVE */
	dd_emit(ctx, 1, 1);		/* 00000001 POLYGON_MODE_??? */
	dd_emit(ctx, 1, 1);		/* 000000ff GP_REG_ALLOC_TEMP / 4 rounded up */
	dd_emit(ctx, 1, 1);		/* 000000ff FP_REG_ALLOC_TEMP... without /4? */
	dd_emit(ctx, 1, 1);		/* 000000ff VP_REG_ALLOC_TEMP / 4 rounded up */
	dd_emit(ctx, 1, 1);		/* 00000001 */
	dd_emit(ctx, 1, 0);		/* 00000001 */
	dd_emit(ctx, 1, 0);		/* 00000001 VTX_ATTR_MASK_UNK0 nonempty */
	dd_emit(ctx, 1, 0);		/* 00000001 VTX_ATTR_MASK_UNK1 nonempty */
	dd_emit(ctx, 1, 0x200);		/* 0003ffff GP_VERTEX_OUTPUT_COUNT*GP_REG_ALLOC_RESULT */
	if (IS_NVA3F(device->chipset))
		dd_emit(ctx, 1, 0x200);
	dd_emit(ctx, 1, 0);		/* 00000001 */
	if (device->chipset < 0xa0) {
		dd_emit(ctx, 1, 1);	/* 00000001 */
		dd_emit(ctx, 1, 0x70);	/* 000000ff */
		dd_emit(ctx, 1, 0x80);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 00000001 */
		dd_emit(ctx, 1, 1);	/* 00000001 */
		dd_emit(ctx, 1, 0x70);	/* 000000ff */
		dd_emit(ctx, 1, 0x80);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 000000ff */
	} else {
		dd_emit(ctx, 1, 1);	/* 00000001 */
		dd_emit(ctx, 1, 0xf0);	/* 000000ff */
		dd_emit(ctx, 1, 0xff);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 00000001 */
		dd_emit(ctx, 1, 1);	/* 00000001 */
		dd_emit(ctx, 1, 0xf0);	/* 000000ff */
		dd_emit(ctx, 1, 0xff);	/* 000000ff */
		dd_emit(ctx, 1, 0);	/* 000000ff */
		dd_emit(ctx, 1, 9);	/* 0000003f UNK114C.COMP,SIZE */
	}

	/* eng2d state */
	dd_emit(ctx, 1, 0);		/* 00000001 eng2d COLOR_KEY_ENABLE */
	dd_emit(ctx, 1, 0);		/* 00000007 eng2d COLOR_KEY_FORMAT */
	dd_emit(ctx, 1, 1);		/* ffffffff eng2d DST_DEPTH */
	dd_emit(ctx, 1, 0xcf);		/* 000000ff eng2d DST_FORMAT */
	dd_emit(ctx, 1, 0);		/* ffffffff eng2d DST_LAYER */
	dd_emit(ctx, 1, 1);		/* 00000001 eng2d DST_LINEAR */
	dd_emit(ctx, 1, 0);		/* 00000007 eng2d PATTERN_COLOR_FORMAT */
	dd_emit(ctx, 1, 0);		/* 00000007 eng2d OPERATION */
	dd_emit(ctx, 1, 0);		/* 00000003 eng2d PATTERN_SELECT */
	dd_emit(ctx, 1, 0xcf);		/* 000000ff eng2d SIFC_FORMAT */
	dd_emit(ctx, 1, 0);		/* 00000001 eng2d SIFC_BITMAP_ENABLE */
	dd_emit(ctx, 1, 2);		/* 00000003 eng2d SIFC_BITMAP_UNK808 */
	dd_emit(ctx, 1, 0);		/* ffffffff eng2d BLIT_DU_DX_FRACT */
	dd_emit(ctx, 1, 1);		/* ffffffff eng2d BLIT_DU_DX_INT */
	dd_emit(ctx, 1, 0);		/* ffffffff eng2d BLIT_DV_DY_FRACT */
	dd_emit(ctx, 1, 1);		/* ffffffff eng2d BLIT_DV_DY_INT */
	dd_emit(ctx, 1, 0);		/* 00000001 eng2d BLIT_CONTROL_FILTER */
	dd_emit(ctx, 1, 0xcf);		/* 000000ff eng2d DRAW_COLOR_FORMAT */
	dd_emit(ctx, 1, 0xcf);		/* 000000ff eng2d SRC_FORMAT */
	dd_emit(ctx, 1, 1);		/* 00000001 eng2d SRC_LINEAR #2 */

	num = ctx->ctxvals_pos - base;
	ctx->ctxvals_pos = base;
	if (IS_NVA3F(device->chipset))
		cp_ctx(ctx, 0x404800, num);
	else
		cp_ctx(ctx, 0x405400, num);
}

/*
 * xfer areas. These are a pain.
 *
 * There are 2 xfer areas: the first one is big and contains all sorts of
 * stuff, the second is small and contains some per-TP context.
 *
 * Each area is split into 8 "strands". The areas, when saved to grctx,
 * are made of 8-word blocks. Each block contains a single word from
 * each strand. The strands are independent of each other, their
 * addresses are unrelated to each other, and data in them is closely
 * packed together. The strand layout varies a bit between cards: here
 * and there, a single word is thrown out in the middle and the whole
 * strand is offset by a bit from corresponding one on another chipset.
 * For this reason, addresses of stuff in strands are almost useless.
 * Knowing sequence of stuff and size of gaps between them is much more
 * useful, and that's how we build the strands in our generator.
 *
 * NVA0 takes this mess to a whole new level by cutting the old strands
 * into a few dozen pieces [known as genes], rearranging them randomly,
 * and putting them back together to make new strands. Hopefully these
 * genes correspond more or less directly to the same PGRAPH subunits
 * as in 400040 register.
 *
 * The most common value in default context is 0, and when the genes
 * are separated by 0's, gene bounduaries are quite speculative...
 * some of them can be clearly deduced, others can be guessed, and yet
 * others won't be resolved without figuring out the real meaning of
 * given ctxval. For the same reason, ending point of each strand
 * is unknown. Except for strand 0, which is the longest strand and
 * its end corresponds to end of the whole xfer.
 *
 * An unsolved mystery is the seek instruction: it takes an argument
 * in bits 8-18, and that argument is clearly the place in strands to
 * seek to... but the offsets don't seem to correspond to offsets as
 * seen in grctx. Perhaps there's another, real, not randomly-changing
 * addressing in strands, and the xfer insn just happens to skip over
 * the unused bits? NV10-NV30 PIPE comes to mind...
 *
 * As far as I know, there's no way to access the xfer areas directly
 * without the help of ctxprog.
 */

static void
xf_emit(struct nvkm_grctx *ctx, int num, u32 val) {
	int i;
	if (val && ctx->mode == NVKM_GRCTX_VALS) {
		for (i = 0; i < num; i++)
			nvkm_wo32(ctx->data, 4 * (ctx->ctxvals_pos + (i << 3)), val);
	}
	ctx->ctxvals_pos += num << 3;
}

/* Gene declarations... */

static void nv50_gr_construct_gene_dispatch(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_m2mf(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_ccache(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_unk10xx(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_unk14xx(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_zcull(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_clipid(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_unk24xx(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_vfetch(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_eng2d(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_csched(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_unk1cxx(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_strmout(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_unk34xx(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_ropm1(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_ropm2(struct nvkm_grctx *ctx);
static void nv50_gr_construct_gene_ropc(struct nvkm_grctx *ctx);
static void nv50_gr_construct_xfer_tp(struct nvkm_grctx *ctx);

static void
nv50_gr_construct_xfer1(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i;
	int offset;
	int size = 0;
	u32 units = nvkm_rd32(device, 0x1540);

	offset = (ctx->ctxvals_pos+0x3f)&~0x3f;
	ctx->ctxvals_base = offset;

	if (device->chipset < 0xa0) {
		/* Strand 0 */
		ctx->ctxvals_pos = offset;
		nv50_gr_construct_gene_dispatch(ctx);
		nv50_gr_construct_gene_m2mf(ctx);
		nv50_gr_construct_gene_unk24xx(ctx);
		nv50_gr_construct_gene_clipid(ctx);
		nv50_gr_construct_gene_zcull(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 1 */
		ctx->ctxvals_pos = offset + 0x1;
		nv50_gr_construct_gene_vfetch(ctx);
		nv50_gr_construct_gene_eng2d(ctx);
		nv50_gr_construct_gene_csched(ctx);
		nv50_gr_construct_gene_ropm1(ctx);
		nv50_gr_construct_gene_ropm2(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 2 */
		ctx->ctxvals_pos = offset + 0x2;
		nv50_gr_construct_gene_ccache(ctx);
		nv50_gr_construct_gene_unk1cxx(ctx);
		nv50_gr_construct_gene_strmout(ctx);
		nv50_gr_construct_gene_unk14xx(ctx);
		nv50_gr_construct_gene_unk10xx(ctx);
		nv50_gr_construct_gene_unk34xx(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 3: per-ROP group state */
		ctx->ctxvals_pos = offset + 3;
		for (i = 0; i < 6; i++)
			if (units & (1 << (i + 16)))
				nv50_gr_construct_gene_ropc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strands 4-7: per-TP state */
		for (i = 0; i < 4; i++) {
			ctx->ctxvals_pos = offset + 4 + i;
			if (units & (1 << (2 * i)))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << (2 * i + 1)))
				nv50_gr_construct_xfer_tp(ctx);
			if ((ctx->ctxvals_pos-offset)/8 > size)
				size = (ctx->ctxvals_pos-offset)/8;
		}
	} else {
		/* Strand 0 */
		ctx->ctxvals_pos = offset;
		nv50_gr_construct_gene_dispatch(ctx);
		nv50_gr_construct_gene_m2mf(ctx);
		nv50_gr_construct_gene_unk34xx(ctx);
		nv50_gr_construct_gene_csched(ctx);
		nv50_gr_construct_gene_unk1cxx(ctx);
		nv50_gr_construct_gene_strmout(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 1 */
		ctx->ctxvals_pos = offset + 1;
		nv50_gr_construct_gene_unk10xx(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 2 */
		ctx->ctxvals_pos = offset + 2;
		if (device->chipset == 0xa0)
			nv50_gr_construct_gene_unk14xx(ctx);
		nv50_gr_construct_gene_unk24xx(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 3 */
		ctx->ctxvals_pos = offset + 3;
		nv50_gr_construct_gene_vfetch(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 4 */
		ctx->ctxvals_pos = offset + 4;
		nv50_gr_construct_gene_ccache(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 5 */
		ctx->ctxvals_pos = offset + 5;
		nv50_gr_construct_gene_ropm2(ctx);
		nv50_gr_construct_gene_ropm1(ctx);
		/* per-ROP context */
		for (i = 0; i < 8; i++)
			if (units & (1<<(i+16)))
				nv50_gr_construct_gene_ropc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 6 */
		ctx->ctxvals_pos = offset + 6;
		nv50_gr_construct_gene_zcull(ctx);
		nv50_gr_construct_gene_clipid(ctx);
		nv50_gr_construct_gene_eng2d(ctx);
		if (units & (1 << 0))
			nv50_gr_construct_xfer_tp(ctx);
		if (units & (1 << 1))
			nv50_gr_construct_xfer_tp(ctx);
		if (units & (1 << 2))
			nv50_gr_construct_xfer_tp(ctx);
		if (units & (1 << 3))
			nv50_gr_construct_xfer_tp(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 7 */
		ctx->ctxvals_pos = offset + 7;
		if (device->chipset == 0xa0) {
			if (units & (1 << 4))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << 5))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << 6))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << 7))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << 8))
				nv50_gr_construct_xfer_tp(ctx);
			if (units & (1 << 9))
				nv50_gr_construct_xfer_tp(ctx);
		} else {
			nv50_gr_construct_gene_unk14xx(ctx);
		}
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;
	}

	ctx->ctxvals_pos = offset + size * 8;
	ctx->ctxvals_pos = (ctx->ctxvals_pos+0x3f)&~0x3f;
	cp_lsr (ctx, offset);
	cp_out (ctx, CP_SET_XFER_POINTER);
	cp_lsr (ctx, size);
	cp_out (ctx, CP_SEEK_1);
	cp_out (ctx, CP_XFER_1);
	cp_wait(ctx, XFER, BUSY);
}

/*
 * non-trivial demagiced parts of ctx init go here
 */

static void
nv50_gr_construct_gene_dispatch(struct nvkm_grctx *ctx)
{
	/* start of strand 0 */
	struct nvkm_device *device = ctx->device;
	/* SEEK */
	if (device->chipset == 0x50)
		xf_emit(ctx, 5, 0);
	else if (!IS_NVA3F(device->chipset))
		xf_emit(ctx, 6, 0);
	else
		xf_emit(ctx, 4, 0);
	/* SEEK */
	/* the PGRAPH's internal FIFO */
	if (device->chipset == 0x50)
		xf_emit(ctx, 8*3, 0);
	else
		xf_emit(ctx, 0x100*3, 0);
	/* and another bonus slot?!? */
	xf_emit(ctx, 3, 0);
	/* and YET ANOTHER bonus slot? */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 3, 0);
	/* SEEK */
	/* CTX_SWITCH: caches of gr objects bound to subchannels. 8 values, last used index */
	xf_emit(ctx, 9, 0);
	/* SEEK */
	xf_emit(ctx, 9, 0);
	/* SEEK */
	xf_emit(ctx, 9, 0);
	/* SEEK */
	xf_emit(ctx, 9, 0);
	/* SEEK */
	if (device->chipset < 0x90)
		xf_emit(ctx, 4, 0);
	/* SEEK */
	xf_emit(ctx, 2, 0);
	/* SEEK */
	xf_emit(ctx, 6*2, 0);
	xf_emit(ctx, 2, 0);
	/* SEEK */
	xf_emit(ctx, 2, 0);
	/* SEEK */
	xf_emit(ctx, 6*2, 0);
	xf_emit(ctx, 2, 0);
	/* SEEK */
	if (device->chipset == 0x50)
		xf_emit(ctx, 0x1c, 0);
	else if (device->chipset < 0xa0)
		xf_emit(ctx, 0x1e, 0);
	else
		xf_emit(ctx, 0x22, 0);
	/* SEEK */
	xf_emit(ctx, 0x15, 0);
}

static void
nv50_gr_construct_gene_m2mf(struct nvkm_grctx *ctx)
{
	/* Strand 0, right after dispatch */
	struct nvkm_device *device = ctx->device;
	int smallm2mf = 0;
	if (device->chipset < 0x92 || device->chipset == 0x98)
		smallm2mf = 1;
	/* SEEK */
	xf_emit (ctx, 1, 0);		/* DMA_NOTIFY instance >> 4 */
	xf_emit (ctx, 1, 0);		/* DMA_BUFFER_IN instance >> 4 */
	xf_emit (ctx, 1, 0);		/* DMA_BUFFER_OUT instance >> 4 */
	xf_emit (ctx, 1, 0);		/* OFFSET_IN */
	xf_emit (ctx, 1, 0);		/* OFFSET_OUT */
	xf_emit (ctx, 1, 0);		/* PITCH_IN */
	xf_emit (ctx, 1, 0);		/* PITCH_OUT */
	xf_emit (ctx, 1, 0);		/* LINE_LENGTH */
	xf_emit (ctx, 1, 0);		/* LINE_COUNT */
	xf_emit (ctx, 1, 0x21);		/* FORMAT: bits 0-4 INPUT_INC, bits 5-9 OUTPUT_INC */
	xf_emit (ctx, 1, 1);		/* LINEAR_IN */
	xf_emit (ctx, 1, 0x2);		/* TILING_MODE_IN: bits 0-2 y tiling, bits 3-5 z tiling */
	xf_emit (ctx, 1, 0x100);	/* TILING_PITCH_IN */
	xf_emit (ctx, 1, 0x100);	/* TILING_HEIGHT_IN */
	xf_emit (ctx, 1, 1);		/* TILING_DEPTH_IN */
	xf_emit (ctx, 1, 0);		/* TILING_POSITION_IN_Z */
	xf_emit (ctx, 1, 0);		/* TILING_POSITION_IN */
	xf_emit (ctx, 1, 1);		/* LINEAR_OUT */
	xf_emit (ctx, 1, 0x2);		/* TILING_MODE_OUT: bits 0-2 y tiling, bits 3-5 z tiling */
	xf_emit (ctx, 1, 0x100);	/* TILING_PITCH_OUT */
	xf_emit (ctx, 1, 0x100);	/* TILING_HEIGHT_OUT */
	xf_emit (ctx, 1, 1);		/* TILING_DEPTH_OUT */
	xf_emit (ctx, 1, 0);		/* TILING_POSITION_OUT_Z */
	xf_emit (ctx, 1, 0);		/* TILING_POSITION_OUT */
	xf_emit (ctx, 1, 0);		/* OFFSET_IN_HIGH */
	xf_emit (ctx, 1, 0);		/* OFFSET_OUT_HIGH */
	/* SEEK */
	if (smallm2mf)
		xf_emit(ctx, 0x40, 0);	/* 20 * ffffffff, 3ffff */
	else
		xf_emit(ctx, 0x100, 0);	/* 80 * ffffffff, 3ffff */
	xf_emit(ctx, 4, 0);		/* 1f/7f, 0, 1f/7f, 0 [1f for smallm2mf, 7f otherwise] */
	/* SEEK */
	if (smallm2mf)
		xf_emit(ctx, 0x400, 0);	/* ffffffff */
	else
		xf_emit(ctx, 0x800, 0);	/* ffffffff */
	xf_emit(ctx, 4, 0);		/* ff/1ff, 0, 0, 0 [ff for smallm2mf, 1ff otherwise] */
	/* SEEK */
	xf_emit(ctx, 0x40, 0);		/* 20 * bits ffffffff, 3ffff */
	xf_emit(ctx, 0x6, 0);		/* 1f, 0, 1f, 0, 1f, 0 */
}

static void
nv50_gr_construct_gene_ccache(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 2, 0);		/* RO */
	xf_emit(ctx, 0x800, 0);		/* ffffffff */
	switch (device->chipset) {
	case 0x50:
	case 0x92:
	case 0xa0:
		xf_emit(ctx, 0x2b, 0);
		break;
	case 0x84:
		xf_emit(ctx, 0x29, 0);
		break;
	case 0x94:
	case 0x96:
	case 0xa3:
		xf_emit(ctx, 0x27, 0);
		break;
	case 0x86:
	case 0x98:
	case 0xa5:
	case 0xa8:
	case 0xaa:
	case 0xac:
	case 0xaf:
		xf_emit(ctx, 0x25, 0);
		break;
	}
	/* CB bindings, 0x80 of them. first word is address >> 8, second is
	 * size >> 4 | valid << 24 */
	xf_emit(ctx, 0x100, 0);		/* ffffffff CB_DEF */
	xf_emit(ctx, 1, 0);		/* 0000007f CB_ADDR_BUFFER */
	xf_emit(ctx, 1, 0);		/* 0 */
	xf_emit(ctx, 0x30, 0);		/* ff SET_PROGRAM_CB */
	xf_emit(ctx, 1, 0);		/* 3f last SET_PROGRAM_CB */
	xf_emit(ctx, 4, 0);		/* RO */
	xf_emit(ctx, 0x100, 0);		/* ffffffff */
	xf_emit(ctx, 8, 0);		/* 1f, 0, 0, ... */
	xf_emit(ctx, 8, 0);		/* ffffffff */
	xf_emit(ctx, 4, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 3 */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_CODE_CB */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_TIC */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_TSC */
	xf_emit(ctx, 1, 0);		/* 00000001 LINKED_TSC */
	xf_emit(ctx, 1, 0);		/* 000000ff TIC_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff TIC_ADDRESS_LOW */
	xf_emit(ctx, 1, 0x3fffff);	/* 003fffff TIC_LIMIT */
	xf_emit(ctx, 1, 0);		/* 000000ff TSC_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff TSC_ADDRESS_LOW */
	xf_emit(ctx, 1, 0x1fff);	/* 000fffff TSC_LIMIT */
	xf_emit(ctx, 1, 0);		/* 000000ff VP_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff VP_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 00ffffff VP_START_ID */
	xf_emit(ctx, 1, 0);		/* 000000ff CB_DEF_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff CB_DEF_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0);		/* 000000ff GP_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff GP_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 00ffffff GP_START_ID */
	xf_emit(ctx, 1, 0);		/* 000000ff FP_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff FP_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 00ffffff FP_START_ID */
}

static void
nv50_gr_construct_gene_unk10xx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i;
	/* end of area 2 on pre-NVA0, area 1 on NVAx */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x80);		/* 0000ffff GP_VERTEX_OUTPUT_COUNT */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 0x80c14);	/* 01ffffff SEMANTIC_COLOR */
	xf_emit(ctx, 1, 0);		/* 00000001 VERTEX_TWO_SIDE_ENABLE */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0x3ff);
	else
		xf_emit(ctx, 1, 0x7ff);	/* 000007ff */
	xf_emit(ctx, 1, 0);		/* 111/113 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	for (i = 0; i < 8; i++) {
		switch (device->chipset) {
		case 0x50:
		case 0x86:
		case 0x98:
		case 0xaa:
		case 0xac:
			xf_emit(ctx, 0xa0, 0);	/* ffffffff */
			break;
		case 0x84:
		case 0x92:
		case 0x94:
		case 0x96:
			xf_emit(ctx, 0x120, 0);
			break;
		case 0xa5:
		case 0xa8:
			xf_emit(ctx, 0x100, 0);	/* ffffffff */
			break;
		case 0xa0:
		case 0xa3:
		case 0xaf:
			xf_emit(ctx, 0x400, 0);	/* ffffffff */
			break;
		}
		xf_emit(ctx, 4, 0);	/* 3f, 0, 0, 0 */
		xf_emit(ctx, 4, 0);	/* ffffffff */
	}
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x80);		/* 0000ffff GP_VERTEX_OUTPUT_COUNT */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_REG_ALLOC_TEMP */
	xf_emit(ctx, 1, 1);		/* 00000001 RASTERIZE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0x27);		/* 000000ff UNK0FD4 */
	xf_emit(ctx, 1, 0);		/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0x26);		/* 000000ff SEMANTIC_LAYER */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
}

static void
nv50_gr_construct_gene_unk34xx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* end of area 2 on pre-NVA0, area 1 on NVAx */
	xf_emit(ctx, 1, 0);		/* 00000001 VIEWPORT_CLIP_RECTS_EN */
	xf_emit(ctx, 1, 0);		/* 00000003 VIEWPORT_CLIP_MODE */
	xf_emit(ctx, 0x10, 0x04000000);	/* 07ffffff VIEWPORT_CLIP_HORIZ*8, VIEWPORT_CLIP_VERT*8 */
	xf_emit(ctx, 1, 0);		/* 00000001 POLYGON_STIPPLE_ENABLE */
	xf_emit(ctx, 0x20, 0);		/* ffffffff POLYGON_STIPPLE */
	xf_emit(ctx, 2, 0);		/* 00007fff WINDOW_OFFSET_XY */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0x04e3bfdf);	/* ffffffff UNK0D64 */
	xf_emit(ctx, 1, 0x04e3bfdf);	/* ffffffff UNK0DF4 */
	xf_emit(ctx, 1, 0);		/* 00000003 WINDOW_ORIGIN */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0x1fe21);	/* 0001ffff tesla UNK0FAC */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 0x0fac6881);
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 1);
		xf_emit(ctx, 3, 0);
	}
}

static void
nv50_gr_construct_gene_unk14xx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* middle of area 2 on pre-NVA0, beginning of area 2 on NVA0, area 7 on >NVA0 */
	if (device->chipset != 0x50) {
		xf_emit(ctx, 5, 0);		/* ffffffff */
		xf_emit(ctx, 1, 0x80c14);	/* 01ffffff SEMANTIC_COLOR */
		xf_emit(ctx, 1, 0);		/* 00000001 */
		xf_emit(ctx, 1, 0);		/* 000003ff */
		xf_emit(ctx, 1, 0x804);		/* 00000fff SEMANTIC_CLIP */
		xf_emit(ctx, 1, 0);		/* 00000001 */
		xf_emit(ctx, 2, 4);		/* 7f, ff */
		xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	}
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 4);			/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);			/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);			/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x10);			/* 7f/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);			/* 000000ff VP_CLIP_DISTANCE_ENABLE */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0);		/* 3ff */
	xf_emit(ctx, 1, 0);			/* 000000ff tesla UNK1940 */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK0D7C */
	xf_emit(ctx, 1, 0x804);			/* 00000fff SEMANTIC_CLIP */
	xf_emit(ctx, 1, 1);			/* 00000001 VIEWPORT_TRANSFORM_EN */
	xf_emit(ctx, 1, 0x1a);			/* 0000001f POLYGON_MODE */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0x7f);		/* 000000ff tesla UNK0FFC */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 1);			/* 00000001 SHADE_MODEL */
	xf_emit(ctx, 1, 0x80c14);		/* 01ffffff SEMANTIC_COLOR */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0x8100c12);		/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 4);			/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);			/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);			/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x10);			/* 7f/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK0D7C */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK0F8C */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 1);			/* 00000001 VIEWPORT_TRANSFORM_EN */
	xf_emit(ctx, 1, 0x8100c12);		/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 4, 0);			/* ffffffff NOPERSPECTIVE_BITMAP */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0);			/* 0000000f */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0x3ff);		/* 000003ff tesla UNK0D68 */
	else
		xf_emit(ctx, 1, 0x7ff);		/* 000007ff tesla UNK0D68 */
	xf_emit(ctx, 1, 0x80c14);		/* 01ffffff SEMANTIC_COLOR */
	xf_emit(ctx, 1, 0);			/* 00000001 VERTEX_TWO_SIDE_ENABLE */
	xf_emit(ctx, 0x30, 0);			/* ffffffff VIEWPORT_SCALE: X0, Y0, Z0, X1, Y1, ... */
	xf_emit(ctx, 3, 0);			/* f, 0, 0 */
	xf_emit(ctx, 3, 0);			/* ffffffff last VIEWPORT_SCALE? */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 1);			/* 00000001 VIEWPORT_TRANSFORM_EN */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1924 */
	xf_emit(ctx, 1, 0x10);			/* 000000ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);			/* 00000001 */
	xf_emit(ctx, 0x30, 0);			/* ffffffff VIEWPORT_TRANSLATE */
	xf_emit(ctx, 3, 0);			/* f, 0, 0 */
	xf_emit(ctx, 3, 0);			/* ffffffff */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 2, 0x88);			/* 000001ff tesla UNK19D8 */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1924 */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 4);			/* 0000000f CULL_MODE */
	xf_emit(ctx, 2, 0);			/* 07ffffff SCREEN_SCISSOR */
	xf_emit(ctx, 2, 0);			/* 00007fff WINDOW_OFFSET_XY */
	xf_emit(ctx, 1, 0);			/* 00000003 WINDOW_ORIGIN */
	xf_emit(ctx, 0x10, 0);			/* 00000001 SCISSOR_ENABLE */
	xf_emit(ctx, 1, 0);			/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0x26);			/* 000000ff SEMANTIC_LAYER */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0);			/* 0000000f */
	xf_emit(ctx, 1, 0x3f800000);		/* ffffffff LINE_WIDTH */
	xf_emit(ctx, 1, 0);			/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0);			/* 00000001 LINE_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);			/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 0);		/* 00000001 */
	xf_emit(ctx, 1, 0x1a);			/* 0000001f POLYGON_MODE */
	xf_emit(ctx, 1, 0x10);			/* 000000ff VIEW_VOLUME_CLIP_CTRL */
	if (device->chipset != 0x50) {
		xf_emit(ctx, 1, 0);		/* ffffffff */
		xf_emit(ctx, 1, 0);		/* 00000001 */
		xf_emit(ctx, 1, 0);		/* 000003ff */
	}
	xf_emit(ctx, 0x20, 0);			/* 10xbits ffffffff, 3fffff. SCISSOR_* */
	xf_emit(ctx, 1, 0);			/* f */
	xf_emit(ctx, 1, 0);			/* 0? */
	xf_emit(ctx, 1, 0);			/* ffffffff */
	xf_emit(ctx, 1, 0);			/* 003fffff */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0x52);			/* 000001ff SEMANTIC_PTSZ */
	xf_emit(ctx, 1, 0);			/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0x26);			/* 000000ff SEMANTIC_LAYER */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 4);			/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);			/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);			/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x1a);			/* 0000001f POLYGON_MODE */
	xf_emit(ctx, 1, 0);			/* 00000001 LINE_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);			/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0x00ffff00);		/* 00ffffff LINE_STIPPLE_PATTERN */
	xf_emit(ctx, 1, 0);			/* 0000000f */
}

static void
nv50_gr_construct_gene_zcull(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* end of strand 0 on pre-NVA0, beginning of strand 6 on NVAx */
	/* SEEK */
	xf_emit(ctx, 1, 0x3f);		/* 0000003f UNK1590 */
	xf_emit(ctx, 1, 0);		/* 00000001 ALPHA_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_BACK_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_REF */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_BACK_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 2);		/* 00000003 tesla UNK143C */
	xf_emit(ctx, 2, 0x04000000);	/* 07ffffff tesla UNK0D6C */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0);		/* 00000001 CLIPID_ENABLE */
	xf_emit(ctx, 2, 0);		/* ffffffff DEPTH_BOUNDS */
	xf_emit(ctx, 1, 0);		/* 00000001 */
	xf_emit(ctx, 1, 0);		/* 00000007 DEPTH_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 4);		/* 0000000f CULL_MODE */
	xf_emit(ctx, 1, 0);		/* 0000ffff */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK0FB0 */
	xf_emit(ctx, 1, 0);		/* 00000001 POLYGON_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0);		/* 000000ff CLEAR_STENCIL */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_FRONT_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_REF */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_FRONT_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff CLEAR_DEPTH */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0);	/* 00000003 tesla UNK1108 */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0x1001);	/* 00001fff ZETA_ARRAY_MODE */
	/* SEEK */
	xf_emit(ctx, 4, 0xffff);	/* 0000ffff MSAA_MASK */
	xf_emit(ctx, 0x10, 0);		/* 00000001 SCISSOR_ENABLE */
	xf_emit(ctx, 0x10, 0);		/* ffffffff DEPTH_RANGE_NEAR */
	xf_emit(ctx, 0x10, 0x3f800000);	/* ffffffff DEPTH_RANGE_FAR */
	xf_emit(ctx, 1, 0x10);		/* 7f/ff/3ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);		/* 00000001 VIEWPORT_CLIP_RECTS_EN */
	xf_emit(ctx, 1, 3);		/* 00000003 FP_CTRL_UNK196C */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK1968 */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0);	/* 0fffffff tesla UNK1104 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK151C */
}

static void
nv50_gr_construct_gene_clipid(struct nvkm_grctx *ctx)
{
	/* middle of strand 0 on pre-NVA0 [after 24xx], middle of area 6 on NVAx */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000007 UNK0FB4 */
	/* SEEK */
	xf_emit(ctx, 4, 0);		/* 07ffffff CLIPID_REGION_HORIZ */
	xf_emit(ctx, 4, 0);		/* 07ffffff CLIPID_REGION_VERT */
	xf_emit(ctx, 2, 0);		/* 07ffffff SCREEN_SCISSOR */
	xf_emit(ctx, 2, 0x04000000);	/* 07ffffff UNK1508 */
	xf_emit(ctx, 1, 0);		/* 00000001 CLIPID_ENABLE */
	xf_emit(ctx, 1, 0x80);		/* 00003fff CLIPID_WIDTH */
	xf_emit(ctx, 1, 0);		/* 000000ff CLIPID_ID */
	xf_emit(ctx, 1, 0);		/* 000000ff CLIPID_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff CLIPID_ADDRESS_LOW */
	xf_emit(ctx, 1, 0x80);		/* 00003fff CLIPID_HEIGHT */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_CLIPID */
}

static void
nv50_gr_construct_gene_unk24xx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i;
	/* middle of strand 0 on pre-NVA0 [after m2mf], end of strand 2 on NVAx */
	/* SEEK */
	xf_emit(ctx, 0x33, 0);
	/* SEEK */
	xf_emit(ctx, 2, 0);
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	/* SEEK */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 4, 0);	/* RO */
		xf_emit(ctx, 0xe10, 0); /* 190 * 9: 8*ffffffff, 7ff */
		xf_emit(ctx, 1, 0);	/* 1ff */
		xf_emit(ctx, 8, 0);	/* 0? */
		xf_emit(ctx, 9, 0);	/* ffffffff, 7ff */

		xf_emit(ctx, 4, 0);	/* RO */
		xf_emit(ctx, 0xe10, 0); /* 190 * 9: 8*ffffffff, 7ff */
		xf_emit(ctx, 1, 0);	/* 1ff */
		xf_emit(ctx, 8, 0);	/* 0? */
		xf_emit(ctx, 9, 0);	/* ffffffff, 7ff */
	} else {
		xf_emit(ctx, 0xc, 0);	/* RO */
		/* SEEK */
		xf_emit(ctx, 0xe10, 0); /* 190 * 9: 8*ffffffff, 7ff */
		xf_emit(ctx, 1, 0);	/* 1ff */
		xf_emit(ctx, 8, 0);	/* 0? */

		/* SEEK */
		xf_emit(ctx, 0xc, 0);	/* RO */
		/* SEEK */
		xf_emit(ctx, 0xe10, 0); /* 190 * 9: 8*ffffffff, 7ff */
		xf_emit(ctx, 1, 0);	/* 1ff */
		xf_emit(ctx, 8, 0);	/* 0? */
	}
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 3);	/* 00000003 tesla UNK1100 */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 0);		/* 0000000f VP_GP_BUILTIN_ATTR_EN */
	xf_emit(ctx, 1, 0x80c14);	/* 01ffffff SEMANTIC_COLOR */
	xf_emit(ctx, 1, 1);		/* 00000001 */
	/* SEEK */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 2, 4);	/* 000000ff */
	xf_emit(ctx, 1, 0x80c14);	/* 01ffffff SEMANTIC_COLOR */
	xf_emit(ctx, 1, 0);		/* 00000001 VERTEX_TWO_SIDE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 POINT_SPRITE_ENABLE */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 0x27);		/* 000000ff SEMANTIC_PRIM_ID */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f */
	xf_emit(ctx, 1, 1);		/* 00000001 */
	for (i = 0; i < 10; i++) {
		/* SEEK */
		xf_emit(ctx, 0x40, 0);		/* ffffffff */
		xf_emit(ctx, 0x10, 0);		/* 3, 0, 0.... */
		xf_emit(ctx, 0x10, 0);		/* ffffffff */
	}
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000001 POINT_SPRITE_CTRL */
	xf_emit(ctx, 1, 1);		/* 00000001 */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 4, 0);		/* ffffffff NOPERSPECTIVE_BITMAP */
	xf_emit(ctx, 0x10, 0);		/* 00ffffff POINT_COORD_REPLACE_MAP */
	xf_emit(ctx, 1, 0);		/* 00000003 WINDOW_ORIGIN */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0);	/* 000003ff */
}

static void
nv50_gr_construct_gene_vfetch(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int acnt = 0x10, rep, i;
	/* beginning of strand 1 on pre-NVA0, strand 3 on NVAx */
	if (IS_NVA3F(device->chipset))
		acnt = 0x20;
	/* SEEK */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 1, 0);	/* ffffffff tesla UNK13A4 */
		xf_emit(ctx, 1, 1);	/* 00000fff tesla UNK1318 */
	}
	xf_emit(ctx, 1, 0);		/* ffffffff VERTEX_BUFFER_FIRST */
	xf_emit(ctx, 1, 0);		/* 00000001 PRIMITIVE_RESTART_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK0DE8 */
	xf_emit(ctx, 1, 0);		/* ffffffff PRIMITIVE_RESTART_INDEX */
	xf_emit(ctx, 1, 0xf);		/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, (acnt/8)-1, 0);	/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, acnt/8, 0);	/* ffffffff VTX_ATR_MASK_UNK0DD0 */
	xf_emit(ctx, 1, 0);		/* 0000000f VP_GP_BUILTIN_ATTR_EN */
	xf_emit(ctx, 1, 0x20);		/* 0000ffff tesla UNK129C */
	xf_emit(ctx, 1, 0);		/* 000000ff turing UNK370??? */
	xf_emit(ctx, 1, 0);		/* 0000ffff turing USER_PARAM_COUNT */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	/* SEEK */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 0xb, 0);	/* RO */
	else if (device->chipset >= 0xa0)
		xf_emit(ctx, 0x9, 0);	/* RO */
	else
		xf_emit(ctx, 0x8, 0);	/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 00000001 EDGE_FLAG */
	xf_emit(ctx, 1, 0);		/* 00000001 PROVOKING_VERTEX_LAST */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0x1a);		/* 0000001f POLYGON_MODE */
	/* SEEK */
	xf_emit(ctx, 0xc, 0);		/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 7f/ff */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* 0000000f VP_GP_BUILTIN_ATTR_EN */
	xf_emit(ctx, 1, 4);		/* 000001ff UNK1A28 */
	xf_emit(ctx, 1, 8);		/* 000001ff UNK0DF0 */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0x3ff);	/* 3ff tesla UNK0D68 */
	else
		xf_emit(ctx, 1, 0x7ff);	/* 7ff tesla UNK0D68 */
	if (device->chipset == 0xa8)
		xf_emit(ctx, 1, 0x1e00);	/* 7fff */
	/* SEEK */
	xf_emit(ctx, 0xc, 0);		/* RO or close */
	/* SEEK */
	xf_emit(ctx, 1, 0xf);		/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, (acnt/8)-1, 0);	/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, 1, 0);		/* 0000000f VP_GP_BUILTIN_ATTR_EN */
	if (device->chipset > 0x50 && device->chipset < 0xa0)
		xf_emit(ctx, 2, 0);	/* ffffffff */
	else
		xf_emit(ctx, 1, 0);	/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK0FD8 */
	/* SEEK */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 0x10, 0);	/* 0? */
		xf_emit(ctx, 2, 0);	/* weird... */
		xf_emit(ctx, 2, 0);	/* RO */
	} else {
		xf_emit(ctx, 8, 0);	/* 0? */
		xf_emit(ctx, 1, 0);	/* weird... */
		xf_emit(ctx, 2, 0);	/* RO */
	}
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* ffffffff VB_ELEMENT_BASE */
	xf_emit(ctx, 1, 0);		/* ffffffff UNK1438 */
	xf_emit(ctx, acnt, 0);		/* 1 tesla UNK1000 */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 0);	/* ffffffff tesla UNK1118? */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* ffffffff VERTEX_ARRAY_UNK90C */
	xf_emit(ctx, 1, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* ffffffff VERTEX_ARRAY_UNK90C */
	xf_emit(ctx, 1, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* RO */
	xf_emit(ctx, 2, 0);		/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK111C? */
	xf_emit(ctx, 1, 0);		/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 000000ff UNK15F4_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff UNK15F4_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 000000ff UNK0F84_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff UNK0F84_ADDRESS_LOW */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* 00003fff VERTEX_ARRAY_ATTRIB_OFFSET */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* 00000fff VERTEX_ARRAY_STRIDE */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* ffffffff VERTEX_ARRAY_LOW */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* 000000ff VERTEX_ARRAY_HIGH */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* ffffffff VERTEX_LIMIT_LOW */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	xf_emit(ctx, acnt, 0);		/* 000000ff VERTEX_LIMIT_HIGH */
	xf_emit(ctx, 3, 0);		/* f/1f */
	/* SEEK */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, acnt, 0);		/* f */
		xf_emit(ctx, 3, 0);		/* f/1f */
	}
	/* SEEK */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 2, 0);	/* RO */
	else
		xf_emit(ctx, 5, 0);	/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* ffff DMA_VTXBUF */
	/* SEEK */
	if (device->chipset < 0xa0) {
		xf_emit(ctx, 0x41, 0);	/* RO */
		/* SEEK */
		xf_emit(ctx, 0x11, 0);	/* RO */
	} else if (!IS_NVA3F(device->chipset))
		xf_emit(ctx, 0x50, 0);	/* RO */
	else
		xf_emit(ctx, 0x58, 0);	/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0xf);		/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, (acnt/8)-1, 0);	/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, 1, 1);		/* 1 UNK0DEC */
	/* SEEK */
	xf_emit(ctx, acnt*4, 0);	/* ffffffff VTX_ATTR */
	xf_emit(ctx, 4, 0);		/* f/1f, 0, 0, 0 */
	/* SEEK */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 0x1d, 0);	/* RO */
	else
		xf_emit(ctx, 0x16, 0);	/* RO */
	/* SEEK */
	xf_emit(ctx, 1, 0xf);		/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, (acnt/8)-1, 0);	/* ffffffff VP_ATTR_EN */
	/* SEEK */
	if (device->chipset < 0xa0)
		xf_emit(ctx, 8, 0);	/* RO */
	else if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 0xc, 0);	/* RO */
	else
		xf_emit(ctx, 7, 0);	/* RO */
	/* SEEK */
	xf_emit(ctx, 0xa, 0);		/* RO */
	if (device->chipset == 0xa0)
		rep = 0xc;
	else
		rep = 4;
	for (i = 0; i < rep; i++) {
		/* SEEK */
		if (IS_NVA3F(device->chipset))
			xf_emit(ctx, 0x20, 0);	/* ffffffff */
		xf_emit(ctx, 0x200, 0);	/* ffffffff */
		xf_emit(ctx, 4, 0);	/* 7f/ff, 0, 0, 0 */
		xf_emit(ctx, 4, 0);	/* ffffffff */
	}
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 113/111 */
	xf_emit(ctx, 1, 0xf);		/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, (acnt/8)-1, 0);	/* ffffffff VP_ATTR_EN */
	xf_emit(ctx, acnt/8, 0);	/* ffffffff VTX_ATTR_MASK_UNK0DD0 */
	xf_emit(ctx, 1, 0);		/* 0000000f VP_GP_BUILTIN_ATTR_EN */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	/* SEEK */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 7, 0);	/* weird... */
	else
		xf_emit(ctx, 5, 0);	/* weird... */
}

static void
nv50_gr_construct_gene_eng2d(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* middle of strand 1 on pre-NVA0 [after vfetch], middle of strand 6 on NVAx */
	/* SEEK */
	xf_emit(ctx, 2, 0);		/* 0001ffff CLIP_X, CLIP_Y */
	xf_emit(ctx, 2, 0);		/* 0000ffff CLIP_W, CLIP_H */
	xf_emit(ctx, 1, 0);		/* 00000001 CLIP_ENABLE */
	if (device->chipset < 0xa0) {
		/* this is useless on everything but the original NV50,
		 * guess they forgot to nuke it. Or just didn't bother. */
		xf_emit(ctx, 2, 0);	/* 0000ffff IFC_CLIP_X, Y */
		xf_emit(ctx, 2, 1);	/* 0000ffff IFC_CLIP_W, H */
		xf_emit(ctx, 1, 0);	/* 00000001 IFC_CLIP_ENABLE */
	}
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	xf_emit(ctx, 1, 0x100);		/* 0001ffff DST_WIDTH */
	xf_emit(ctx, 1, 0x100);		/* 0001ffff DST_HEIGHT */
	xf_emit(ctx, 1, 0x11);		/* 3f[NV50]/7f[NV84+] DST_FORMAT */
	xf_emit(ctx, 1, 0);		/* 0001ffff DRAW_POINT_X */
	xf_emit(ctx, 1, 8);		/* 0000000f DRAW_UNK58C */
	xf_emit(ctx, 1, 0);		/* 000fffff SIFC_DST_X_FRACT */
	xf_emit(ctx, 1, 0);		/* 0001ffff SIFC_DST_X_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff SIFC_DST_Y_FRACT */
	xf_emit(ctx, 1, 0);		/* 0001ffff SIFC_DST_Y_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff SIFC_DX_DU_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff SIFC_DX_DU_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff SIFC_DY_DV_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff SIFC_DY_DV_INT */
	xf_emit(ctx, 1, 1);		/* 0000ffff SIFC_WIDTH */
	xf_emit(ctx, 1, 1);		/* 0000ffff SIFC_HEIGHT */
	xf_emit(ctx, 1, 0xcf);		/* 000000ff SIFC_FORMAT */
	xf_emit(ctx, 1, 2);		/* 00000003 SIFC_BITMAP_UNK808 */
	xf_emit(ctx, 1, 0);		/* 00000003 SIFC_BITMAP_LINE_PACK_MODE */
	xf_emit(ctx, 1, 0);		/* 00000001 SIFC_BITMAP_LSB_FIRST */
	xf_emit(ctx, 1, 0);		/* 00000001 SIFC_BITMAP_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000ffff BLIT_DST_X */
	xf_emit(ctx, 1, 0);		/* 0000ffff BLIT_DST_Y */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_DU_DX_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff BLIT_DU_DX_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_DV_DY_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff BLIT_DV_DY_INT */
	xf_emit(ctx, 1, 1);		/* 0000ffff BLIT_DST_W */
	xf_emit(ctx, 1, 1);		/* 0000ffff BLIT_DST_H */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_SRC_X_FRACT */
	xf_emit(ctx, 1, 0);		/* 0001ffff BLIT_SRC_X_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_SRC_Y_FRACT */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK888 */
	xf_emit(ctx, 1, 4);		/* 0000003f UNK884 */
	xf_emit(ctx, 1, 0);		/* 00000007 UNK880 */
	xf_emit(ctx, 1, 1);		/* 0000001f tesla UNK0FB8 */
	xf_emit(ctx, 1, 0x15);		/* 000000ff tesla UNK128C */
	xf_emit(ctx, 2, 0);		/* 00000007, ffff0ff3 */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK260 */
	xf_emit(ctx, 1, 0x4444480);	/* 1fffffff UNK870 */
	/* SEEK */
	xf_emit(ctx, 0x10, 0);
	/* SEEK */
	xf_emit(ctx, 0x27, 0);
}

static void
nv50_gr_construct_gene_csched(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* middle of strand 1 on pre-NVA0 [after eng2d], middle of strand 0 on NVAx */
	/* SEEK */
	xf_emit(ctx, 2, 0);		/* 00007fff WINDOW_OFFSET_XY... what is it doing here??? */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1924 */
	xf_emit(ctx, 1, 0);		/* 00000003 WINDOW_ORIGIN */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 0);		/* 000003ff */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* ffffffff turing UNK364 */
	xf_emit(ctx, 1, 0);		/* 0000000f turing UNK36C */
	xf_emit(ctx, 1, 0);		/* 0000ffff USER_PARAM_COUNT */
	xf_emit(ctx, 1, 0x100);		/* 00ffffff turing UNK384 */
	xf_emit(ctx, 1, 0);		/* 0000000f turing UNK2A0 */
	xf_emit(ctx, 1, 0);		/* 0000ffff GRIDID */
	xf_emit(ctx, 1, 0x10001);	/* ffffffff GRIDDIM_XY */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0x10001);	/* ffffffff BLOCKDIM_XY */
	xf_emit(ctx, 1, 1);		/* 0000ffff BLOCKDIM_Z */
	xf_emit(ctx, 1, 0x10001);	/* 00ffffff BLOCK_ALLOC */
	xf_emit(ctx, 1, 1);		/* 00000001 LANES32 */
	xf_emit(ctx, 1, 4);		/* 000000ff FP_REG_ALLOC_TEMP */
	xf_emit(ctx, 1, 2);		/* 00000003 REG_MODE */
	/* SEEK */
	xf_emit(ctx, 0x40, 0);		/* ffffffff USER_PARAM */
	switch (device->chipset) {
	case 0x50:
	case 0x92:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0x80, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 0x10*2, 0);	/* ffffffff, 1f */
		break;
	case 0x84:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0x60, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 0xc*2, 0);	/* ffffffff, 1f */
		break;
	case 0x94:
	case 0x96:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0x40, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 8*2, 0);	/* ffffffff, 1f */
		break;
	case 0x86:
	case 0x98:
		xf_emit(ctx, 4, 0);	/* f, 0, 0, 0 */
		xf_emit(ctx, 0x10, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 2*2, 0);	/* ffffffff, 1f */
		break;
	case 0xa0:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0xf0, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 0x1e*2, 0);	/* ffffffff, 1f */
		break;
	case 0xa3:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0x60, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 0xc*2, 0);	/* ffffffff, 1f */
		break;
	case 0xa5:
	case 0xaf:
		xf_emit(ctx, 8, 0);	/* 7, 0, 0, 0, ... */
		xf_emit(ctx, 0x30, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 6*2, 0);	/* ffffffff, 1f */
		break;
	case 0xaa:
		xf_emit(ctx, 0x12, 0);
		break;
	case 0xa8:
	case 0xac:
		xf_emit(ctx, 4, 0);	/* f, 0, 0, 0 */
		xf_emit(ctx, 0x10, 0);	/* fff */
		xf_emit(ctx, 2, 0);	/* ff, fff */
		xf_emit(ctx, 2*2, 0);	/* ffffffff, 1f */
		break;
	}
	xf_emit(ctx, 1, 0);		/* 0000000f */
	xf_emit(ctx, 1, 0);		/* 00000000 */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 0000001f */
	xf_emit(ctx, 4, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 00000003 turing UNK35C */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 4, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 00000003 turing UNK35C */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 000000ff */
}

static void
nv50_gr_construct_gene_unk1cxx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 2, 0);		/* 00007fff WINDOW_OFFSET_XY */
	xf_emit(ctx, 1, 0x3f800000);	/* ffffffff LINE_WIDTH */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1658 */
	xf_emit(ctx, 1, 0);		/* 00000001 POLYGON_SMOOTH_ENABLE */
	xf_emit(ctx, 3, 0);		/* 00000001 POLYGON_OFFSET_*_ENABLE */
	xf_emit(ctx, 1, 4);		/* 0000000f CULL_MODE */
	xf_emit(ctx, 1, 0x1a);		/* 0000001f POLYGON_MODE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 0);		/* 00000001 POINT_SPRITE_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK165C */
	xf_emit(ctx, 0x10, 0);		/* 00000001 SCISSOR_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0x00ffff00);	/* 00ffffff LINE_STIPPLE_PATTERN */
	xf_emit(ctx, 1, 0);		/* ffffffff POLYGON_OFFSET_UNITS */
	xf_emit(ctx, 1, 0);		/* ffffffff POLYGON_OFFSET_FACTOR */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK1668 */
	xf_emit(ctx, 2, 0);		/* 07ffffff SCREEN_SCISSOR */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0x11);		/* 0000007f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 0000007f RT_FORMAT */
	xf_emit(ctx, 8, 0);		/* 00000001 RT_HORIZ_LINEAR */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000001 ALPHA_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 ALPHA_TEST_FUNC */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 3);	/* 00000003 UNK16B4 */
	else if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 1);	/* 00000001 UNK16B4 */
	xf_emit(ctx, 1, 0);		/* 00000003 MULTISAMPLE_CTRL */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK0F90 */
	xf_emit(ctx, 1, 2);		/* 00000003 tesla UNK143C */
	xf_emit(ctx, 2, 0x04000000);	/* 07ffffff tesla UNK0D6C */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 5);		/* 0000000f UNK1408 */
	xf_emit(ctx, 1, 0x52);		/* 000001ff SEMANTIC_PTSZ */
	xf_emit(ctx, 1, 0);		/* ffffffff POINT_SIZE */
	xf_emit(ctx, 1, 0);		/* 00000001 */
	xf_emit(ctx, 1, 0);		/* 00000007 tesla UNK0FB4 */
	if (device->chipset != 0x50) {
		xf_emit(ctx, 1, 0);	/* 3ff */
		xf_emit(ctx, 1, 1);	/* 00000001 tesla UNK1110 */
	}
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 0);	/* 00000003 tesla UNK1928 */
	xf_emit(ctx, 0x10, 0);		/* ffffffff DEPTH_RANGE_NEAR */
	xf_emit(ctx, 0x10, 0x3f800000);	/* ffffffff DEPTH_RANGE_FAR */
	xf_emit(ctx, 1, 0x10);		/* 000000ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 0x20, 0);		/* 07ffffff VIEWPORT_HORIZ, then VIEWPORT_VERT. (W&0x3fff)<<13 | (X&0x1fff). */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK187C */
	xf_emit(ctx, 1, 0);		/* 00000003 WINDOW_ORIGIN */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_MASK */
	xf_emit(ctx, 1, 0x8100c12);	/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 5);		/* 0000000f tesla UNK1220 */
	xf_emit(ctx, 1, 0);		/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 000000ff tesla UNK1A20 */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 VERTEX_TWO_SIDE_ENABLE */
	xf_emit(ctx, 4, 0xffff);	/* 0000ffff MSAA_MASK */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 3);	/* 00000003 tesla UNK1100 */
	if (device->chipset < 0xa0)
		xf_emit(ctx, 0x1c, 0);	/* RO */
	else if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 0x9, 0);
	xf_emit(ctx, 1, 0);		/* 00000001 UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0x00ffff00);	/* 00ffffff LINE_STIPPLE_PATTERN */
	xf_emit(ctx, 1, 0x1a);		/* 0000001f POLYGON_MODE */
	xf_emit(ctx, 1, 0);		/* 00000003 WINDOW_ORIGIN */
	if (device->chipset != 0x50) {
		xf_emit(ctx, 1, 3);	/* 00000003 tesla UNK1100 */
		xf_emit(ctx, 1, 0);	/* 3ff */
	}
	/* XXX: the following block could belong either to unk1cxx, or
	 * to STRMOUT. Rather hard to tell. */
	if (device->chipset < 0xa0)
		xf_emit(ctx, 0x25, 0);
	else
		xf_emit(ctx, 0x3b, 0);
}

static void
nv50_gr_construct_gene_strmout(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 1, 0x102);		/* 0000ffff STRMOUT_BUFFER_CTRL */
	xf_emit(ctx, 1, 0);		/* ffffffff STRMOUT_PRIMITIVE_COUNT */
	xf_emit(ctx, 4, 4);		/* 000000ff STRMOUT_NUM_ATTRIBS */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 4, 0);	/* ffffffff UNK1A8C */
		xf_emit(ctx, 4, 0);	/* ffffffff UNK1780 */
	}
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 4);		/* 0000007f VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0x3ff);	/* 000003ff tesla UNK0D68 */
	else
		xf_emit(ctx, 1, 0x7ff);	/* 000007ff tesla UNK0D68 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	/* SEEK */
	xf_emit(ctx, 1, 0x102);		/* 0000ffff STRMOUT_BUFFER_CTRL */
	xf_emit(ctx, 1, 0);		/* ffffffff STRMOUT_PRIMITIVE_COUNT */
	xf_emit(ctx, 4, 0);		/* 000000ff STRMOUT_ADDRESS_HIGH */
	xf_emit(ctx, 4, 0);		/* ffffffff STRMOUT_ADDRESS_LOW */
	xf_emit(ctx, 4, 4);		/* 000000ff STRMOUT_NUM_ATTRIBS */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 4, 0);	/* ffffffff UNK1A8C */
		xf_emit(ctx, 4, 0);	/* ffffffff UNK1780 */
	}
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_STRMOUT */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_QUERY */
	xf_emit(ctx, 1, 0);		/* 000000ff QUERY_ADDRESS_HIGH */
	xf_emit(ctx, 2, 0);		/* ffffffff QUERY_ADDRESS_LOW QUERY_COUNTER */
	xf_emit(ctx, 2, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	/* SEEK */
	xf_emit(ctx, 0x20, 0);		/* ffffffff STRMOUT_MAP */
	xf_emit(ctx, 1, 0);		/* 0000000f */
	xf_emit(ctx, 1, 0);		/* 00000000? */
	xf_emit(ctx, 2, 0);		/* ffffffff */
}

static void
nv50_gr_construct_gene_ropm1(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 1, 0x4e3bfdf);	/* ffffffff UNK0D64 */
	xf_emit(ctx, 1, 0x4e3bfdf);	/* ffffffff UNK0DF4 */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0);		/* 000003ff */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 0x11);	/* 000000ff tesla UNK1968 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
}

static void
nv50_gr_construct_gene_ropm2(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_QUERY */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 2, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 000000ff QUERY_ADDRESS_HIGH */
	xf_emit(ctx, 2, 0);		/* ffffffff QUERY_ADDRESS_LOW, COUNTER */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 7 */
	/* SEEK */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_QUERY */
	xf_emit(ctx, 1, 0);		/* 000000ff QUERY_ADDRESS_HIGH */
	xf_emit(ctx, 2, 0);		/* ffffffff QUERY_ADDRESS_LOW, COUNTER */
	xf_emit(ctx, 1, 0x4e3bfdf);	/* ffffffff UNK0D64 */
	xf_emit(ctx, 1, 0x4e3bfdf);	/* ffffffff UNK0DF4 */
	xf_emit(ctx, 1, 0);		/* 00000001 eng2d UNK260 */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 0x11);	/* 000000ff tesla UNK1968 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
}

static void
nv50_gr_construct_gene_ropc(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int magic2;
	if (device->chipset == 0x50) {
		magic2 = 0x00003e60;
	} else if (!IS_NVA3F(device->chipset)) {
		magic2 = 0x001ffe67;
	} else {
		magic2 = 0x00087e67;
	}
	xf_emit(ctx, 1, 0);		/* f/7 MUTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_BACK_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_BACK_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 2);		/* 00000003 tesla UNK143C */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, magic2);	/* 001fffff tesla UNK0F78 */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000007 DEPTH_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_FRONT_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_FRONT_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	if (device->chipset >= 0xa0 && !IS_NVAAF(device->chipset))
		xf_emit(ctx, 1, 0x15);	/* 000000ff */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 0x10);		/* 3ff/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);		/* ffffffff CLEAR_DEPTH */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	if (device->chipset == 0x86 || device->chipset == 0x92 || device->chipset == 0x98 || device->chipset >= 0xa0) {
		xf_emit(ctx, 3, 0);	/* ff, ffffffff, ffffffff */
		xf_emit(ctx, 1, 4);	/* 7 */
		xf_emit(ctx, 1, 0x400);	/* fffffff */
		xf_emit(ctx, 1, 0x300);	/* ffff */
		xf_emit(ctx, 1, 0x1001);	/* 1fff */
		if (device->chipset != 0xa0) {
			if (IS_NVA3F(device->chipset))
				xf_emit(ctx, 1, 0);	/* 0000000f UNK15C8 */
			else
				xf_emit(ctx, 1, 0x15);	/* ff */
		}
	}
	xf_emit(ctx, 1, 0);		/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_BACK_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 2);		/* 00000003 tesla UNK143C */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000007 DEPTH_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_FRONT_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 0x10);		/* 7f/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1900 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_BACK_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_REF */
	xf_emit(ctx, 2, 0);		/* ffffffff DEPTH_BOUNDS */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000007 DEPTH_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK0FB0 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_FRONT_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_REF */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 0x10);		/* 7f/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 0x10, 0);		/* ffffffff DEPTH_RANGE_NEAR */
	xf_emit(ctx, 0x10, 0x3f800000);	/* ffffffff DEPTH_RANGE_FAR */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 0);		/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_BACK_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_FUNC_REF */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_BACK_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 2, 0);		/* ffffffff DEPTH_BOUNDS */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000007 DEPTH_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 000000ff CLEAR_STENCIL */
	xf_emit(ctx, 1, 0);		/* 00000007 STENCIL_FRONT_FUNC_FUNC */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_FUNC_REF */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_FRONT_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 1, 0x10);		/* 7f/ff VIEW_VOLUME_CLIP_CTRL */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 0x3f);		/* 0000003f UNK1590 */
	xf_emit(ctx, 1, 0);		/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 2, 0);		/* ffff0ff3, ffff */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK0FB0 */
	xf_emit(ctx, 1, 0);		/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff CLEAR_DEPTH */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK19CC */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 2, 0);
		xf_emit(ctx, 1, 0x1001);
		xf_emit(ctx, 0xb, 0);
	} else {
		xf_emit(ctx, 1, 0);	/* 00000007 */
		xf_emit(ctx, 1, 0);	/* 00000001 tesla UNK1534 */
		xf_emit(ctx, 1, 0);	/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
		xf_emit(ctx, 8, 0);	/* 00000001 BLEND_ENABLE */
		xf_emit(ctx, 1, 0);	/* ffff0ff3 */
	}
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f */
	xf_emit(ctx, 1, 0);		/* 00000001 LOGIC_OP_ENABLE */
	if (device->chipset != 0x50) {
		xf_emit(ctx, 1, 0);	/* 0000000f LOGIC_OP */
		xf_emit(ctx, 1, 0);	/* 000000ff */
	}
	xf_emit(ctx, 1, 0);		/* 00000007 OPERATION */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 0);		/* 00000003 UNK0F90 */
	xf_emit(ctx, 2, 1);		/* 00000007 BLEND_EQUATION_RGB, ALPHA */
	xf_emit(ctx, 1, 1);		/* 00000001 UNK133C */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_RGB */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_RGB */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_ALPHA */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_ALPHA */
	xf_emit(ctx, 1, 0);		/* 00000001 */
	xf_emit(ctx, 1, magic2);	/* 001fffff tesla UNK0F78 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 0);	/* 00000001 tesla UNK12E4 */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_RGB */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_ALPHA */
		xf_emit(ctx, 8, 1);	/* 00000001 IBLEND_UNK00 */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_FUNC_SRC_RGB */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_FUNC_DST_RGB */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_FUNC_SRC_ALPHA */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_FUNC_DST_ALPHA */
		xf_emit(ctx, 1, 0);	/* 00000001 tesla UNK1140 */
		xf_emit(ctx, 2, 0);	/* 00000001 */
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
		xf_emit(ctx, 1, 0);	/* 0000000f */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* ffffffff */
		xf_emit(ctx, 2, 0);	/* 00000001 */
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
		xf_emit(ctx, 1, 0);	/* 00000001 */
		xf_emit(ctx, 1, 0);	/* 000003ff */
	} else if (device->chipset >= 0xa0) {
		xf_emit(ctx, 2, 0);	/* 00000001 */
		xf_emit(ctx, 1, 0);	/* 00000007 */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* ffffffff */
		xf_emit(ctx, 2, 0);	/* 00000001 */
	} else {
		xf_emit(ctx, 1, 0);	/* 00000007 MULTISAMPLE_SAMPLES_LOG2 */
		xf_emit(ctx, 1, 0);	/* 00000003 tesla UNK1430 */
		xf_emit(ctx, 1, 0);	/* ffffffff tesla UNK1A3C */
	}
	xf_emit(ctx, 4, 0);		/* ffffffff CLEAR_COLOR */
	xf_emit(ctx, 4, 0);		/* ffffffff BLEND_COLOR A R G B */
	xf_emit(ctx, 1, 0);		/* 00000fff eng2d UNK2B0 */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 2, 0);	/* 00000001 */
	xf_emit(ctx, 1, 0);		/* 000003ff */
	xf_emit(ctx, 8, 0);		/* 00000001 BLEND_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 UNK133C */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_RGB */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_RGB */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_RGB */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_ALPHA */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_ALPHA */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_ALPHA */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK19C0 */
	xf_emit(ctx, 1, 0);		/* 00000001 LOGIC_OP_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f LOGIC_OP */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 0);	/* 00000001 UNK12E4? NVA3+ only? */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 8, 1);	/* 00000001 IBLEND_UNK00 */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_RGB */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_FUNC_SRC_RGB */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_FUNC_DST_RGB */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_ALPHA */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_FUNC_SRC_ALPHA */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_FUNC_DST_ALPHA */
		xf_emit(ctx, 1, 0);	/* 00000001 tesla UNK15C4 */
		xf_emit(ctx, 1, 0);	/* 00000001 */
		xf_emit(ctx, 1, 0);	/* 00000001 tesla UNK1140 */
	}
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	xf_emit(ctx, 1, 0);		/* 00000007 PATTERN_COLOR_FORMAT */
	xf_emit(ctx, 2, 0);		/* ffffffff PATTERN_MONO_COLOR */
	xf_emit(ctx, 1, 0);		/* 00000001 PATTERN_MONO_FORMAT */
	xf_emit(ctx, 2, 0);		/* ffffffff PATTERN_MONO_BITMAP */
	xf_emit(ctx, 1, 0);		/* 00000003 PATTERN_SELECT */
	xf_emit(ctx, 1, 0);		/* 000000ff ROP */
	xf_emit(ctx, 1, 0);		/* ffffffff BETA1 */
	xf_emit(ctx, 1, 0);		/* ffffffff BETA4 */
	xf_emit(ctx, 1, 0);		/* 00000007 OPERATION */
	xf_emit(ctx, 0x50, 0);		/* 10x ffffff, ffffff, ffffff, ffffff, 3 PATTERN */
}

static void
nv50_gr_construct_xfer_unk84xx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int magic3;
	switch (device->chipset) {
	case 0x50:
		magic3 = 0x1000;
		break;
	case 0x86:
	case 0x98:
	case 0xa8:
	case 0xaa:
	case 0xac:
	case 0xaf:
		magic3 = 0x1e00;
		break;
	default:
		magic3 = 0;
	}
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 7f/ff[NVA0+] VP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 111/113[NVA0+] */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 0x1f, 0);	/* ffffffff */
	else if (device->chipset >= 0xa0)
		xf_emit(ctx, 0x0f, 0);	/* ffffffff */
	else
		xf_emit(ctx, 0x10, 0);	/* fffffff VP_RESULT_MAP_1 up */
	xf_emit(ctx, 2, 0);		/* f/1f[NVA3], fffffff/ffffffff[NVA0+] */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_RESULT_MAP_SIZE */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 0x03020100);	/* ffffffff */
	else
		xf_emit(ctx, 1, 0x00608080);	/* fffffff VP_RESULT_MAP_0 */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 2, 0);		/* 111/113, 7f/ff */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0x80);		/* 0000ffff GP_VERTEX_OUTPUT_COUNT */
	if (magic3)
		xf_emit(ctx, 1, magic3);	/* 00007fff tesla UNK141C */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 111/113 */
	xf_emit(ctx, 0x1f, 0);		/* ffffffff GP_RESULT_MAP_1 up */
	xf_emit(ctx, 1, 0);		/* 0000001f */
	xf_emit(ctx, 1, 0);		/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 0x80);		/* 0000ffff GP_VERTEX_OUTPUT_COUNT */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0x03020100);	/* ffffffff GP_RESULT_MAP_0 */
	xf_emit(ctx, 1, 3);		/* 00000003 GP_OUTPUT_PRIMITIVE_TYPE */
	if (magic3)
		xf_emit(ctx, 1, magic3);	/* 7fff tesla UNK141C */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 0);		/* 00000001 PROVOKING_VERTEX_LAST */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 111/113 */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 3);		/* 00000003 GP_OUTPUT_PRIMITIVE_TYPE */
	xf_emit(ctx, 1, 0);		/* 00000001 PROVOKING_VERTEX_LAST */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK13A0 */
	xf_emit(ctx, 1, 4);		/* 7f/ff VP_REG_ALLOC_RESULT */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
	xf_emit(ctx, 1, 0);		/* 111/113 */
	if (device->chipset == 0x94 || device->chipset == 0x96)
		xf_emit(ctx, 0x1020, 0);	/* 4 x (0x400 x 0xffffffff, ff, 0, 0, 0, 4 x ffffffff) */
	else if (device->chipset < 0xa0)
		xf_emit(ctx, 0xa20, 0);	/* 4 x (0x280 x 0xffffffff, ff, 0, 0, 0, 4 x ffffffff) */
	else if (!IS_NVA3F(device->chipset))
		xf_emit(ctx, 0x210, 0);	/* ffffffff */
	else
		xf_emit(ctx, 0x410, 0);	/* ffffffff */
	xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
	xf_emit(ctx, 1, 4);		/* 000000ff GP_RESULT_MAP_SIZE */
	xf_emit(ctx, 1, 3);		/* 00000003 GP_OUTPUT_PRIMITIVE_TYPE */
	xf_emit(ctx, 1, 0);		/* 00000001 PROVOKING_VERTEX_LAST */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
}

static void
nv50_gr_construct_xfer_tprop(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int magic1, magic2;
	if (device->chipset == 0x50) {
		magic1 = 0x3ff;
		magic2 = 0x00003e60;
	} else if (!IS_NVA3F(device->chipset)) {
		magic1 = 0x7ff;
		magic2 = 0x001ffe67;
	} else {
		magic1 = 0x7ff;
		magic2 = 0x00087e67;
	}
	xf_emit(ctx, 1, 0);		/* 00000007 ALPHA_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* ffffffff ALPHA_TEST_REF */
	xf_emit(ctx, 1, 0);		/* 00000001 ALPHA_TEST_ENABLE */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000000f UNK16A0 */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_BACK_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_BACK_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 4, 0);		/* ffffffff BLEND_COLOR */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK19C0 */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK0FDC */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 LOGIC_OP_ENABLE */
	xf_emit(ctx, 1, 0);		/* ff[NV50]/3ff[NV84+] */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 4, 0xffff);	/* 0000ffff MSAA_MASK */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 3, 0);		/* 00000007 STENCIL_FRONT_OP_FAIL, ZFAIL, ZPASS */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_BACK_ENABLE */
	xf_emit(ctx, 2, 0);		/* 00007fff WINDOW_OFFSET_XY */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK19CC */
	xf_emit(ctx, 1, 0);		/* 7 */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff COLOR_KEY */
	xf_emit(ctx, 1, 0);		/* 00000001 COLOR_KEY_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 COLOR_KEY_FORMAT */
	xf_emit(ctx, 2, 0);		/* ffffffff SIFC_BITMAP_COLOR */
	xf_emit(ctx, 1, 1);		/* 00000001 SIFC_BITMAP_WRITE_BIT0_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 ALPHA_TEST_FUNC */
	xf_emit(ctx, 1, 0);		/* 00000001 ALPHA_TEST_ENABLE */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 3);	/* 00000003 tesla UNK16B4 */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* 00000003 tesla UNK1298 */
	} else if (device->chipset >= 0xa0) {
		xf_emit(ctx, 1, 1);	/* 00000001 tesla UNK16B4 */
		xf_emit(ctx, 1, 0);	/* 00000003 */
	} else {
		xf_emit(ctx, 1, 0);	/* 00000003 MULTISAMPLE_CTRL */
	}
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 8, 0);		/* 00000001 BLEND_ENABLE */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_ALPHA */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_ALPHA */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_ALPHA */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_RGB */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_RGB */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_RGB */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 0);	/* 00000001 UNK12E4 */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_RGB */
		xf_emit(ctx, 8, 1);	/* 00000007 IBLEND_EQUATION_ALPHA */
		xf_emit(ctx, 8, 1);	/* 00000001 IBLEND_UNK00 */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_SRC_RGB */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_DST_RGB */
		xf_emit(ctx, 8, 2);	/* 0000001f IBLEND_SRC_ALPHA */
		xf_emit(ctx, 8, 1);	/* 0000001f IBLEND_DST_ALPHA */
		xf_emit(ctx, 1, 0);	/* 00000001 UNK1140 */
	}
	xf_emit(ctx, 1, 1);		/* 00000001 UNK133C */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000001 LOGIC_OP_ENABLE */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000003 UNK0F90 */
	xf_emit(ctx, 1, 0);		/* 00000001 FRAMEBUFFER_SRGB */
	xf_emit(ctx, 1, 0);		/* 7 */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	xf_emit(ctx, 1, 0);		/* 00000007 OPERATION */
	xf_emit(ctx, 1, 0xcf);		/* 000000ff SIFC_FORMAT */
	xf_emit(ctx, 1, 0xcf);		/* 000000ff DRAW_COLOR_FORMAT */
	xf_emit(ctx, 1, 0xcf);		/* 000000ff SRC_FORMAT */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	xf_emit(ctx, 1, 0);		/* 7/f[NVA3] MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 8, 0);		/* 00000001 BLEND_ENABLE */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_ALPHA */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_ALPHA */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_ALPHA */
	xf_emit(ctx, 1, 1);		/* 0000001f BLEND_FUNC_DST_RGB */
	xf_emit(ctx, 1, 1);		/* 00000007 BLEND_EQUATION_RGB */
	xf_emit(ctx, 1, 2);		/* 0000001f BLEND_FUNC_SRC_RGB */
	xf_emit(ctx, 1, 1);		/* 00000001 UNK133C */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 8, 1);		/* 00000001 UNK19E0 */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, magic2);	/* 001fffff tesla UNK0F78 */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0);	/* ff */
	else
		xf_emit(ctx, 3, 0);	/* 1, 7, 3ff */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000003 UNK0F90 */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_DU_DX_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff BLIT_DU_DX_INT */
	xf_emit(ctx, 1, 0);		/* 000fffff BLIT_DV_DY_FRACT */
	xf_emit(ctx, 1, 1);		/* 0001ffff BLIT_DV_DY_INT */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, magic1);	/* 3ff/7ff tesla UNK0D68 */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	xf_emit(ctx, 8, 0);		/* 0000ffff DMA_COLOR */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_GLOBAL */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_LOCAL */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_STACK */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_DST */
	xf_emit(ctx, 1, 0);		/* 7 */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 8, 0);		/* 000000ff RT_ADDRESS_HIGH */
	xf_emit(ctx, 8, 0);		/* ffffffff RT_LAYER_STRIDE */
	xf_emit(ctx, 8, 0);		/* ffffffff RT_ADDRESS_LOW */
	xf_emit(ctx, 8, 8);		/* 0000007f RT_TILE_MODE */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 8, 0x400);		/* 0fffffff RT_HORIZ */
	xf_emit(ctx, 8, 0x300);		/* 0000ffff RT_VERT */
	xf_emit(ctx, 1, 1);		/* 00001fff RT_ARRAY_MODE */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, 0x20);		/* 00000fff DST_TILE_MODE */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 0x100);		/* 0001ffff DST_HEIGHT */
	xf_emit(ctx, 1, 0);		/* 000007ff DST_LAYER */
	xf_emit(ctx, 1, 1);		/* 00000001 DST_LINEAR */
	xf_emit(ctx, 1, 0);		/* ffffffff DST_ADDRESS_LOW */
	xf_emit(ctx, 1, 0);		/* 000000ff DST_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0x40);		/* 0007ffff DST_PITCH */
	xf_emit(ctx, 1, 0x100);		/* 0001ffff DST_WIDTH */
	xf_emit(ctx, 1, 0);		/* 0000ffff */
	xf_emit(ctx, 1, 3);		/* 00000003 tesla UNK15AC */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 0);		/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0);		/* 00000003 UNK0F90 */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	xf_emit(ctx, 1, magic2);	/* 001fffff tesla UNK0F78 */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 2);		/* 00000003 tesla UNK143C */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_ZETA */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 2, 0);		/* ffff, ff/3ff */
	xf_emit(ctx, 1, 0);		/* 0001ffff GP_BUILTIN_RESULT_EN */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 000000ff STENCIL_FRONT_MASK */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0);		/* ffffffff ZETA_LAYER_STRIDE */
	xf_emit(ctx, 1, 0);		/* 000000ff ZETA_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);		/* ffffffff ZETA_ADDRESS_LOW */
	xf_emit(ctx, 1, 4);		/* 00000007 ZETA_TILE_MODE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	xf_emit(ctx, 1, 0x400);		/* 0fffffff ZETA_HORIZ */
	xf_emit(ctx, 1, 0x300);		/* 0000ffff ZETA_VERT */
	xf_emit(ctx, 1, 0x1001);	/* 00001fff ZETA_ARRAY_MODE */
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 0);	/* 00000001 */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 7, 0);		/* 3f/7f RT_FORMAT */
	xf_emit(ctx, 1, 0x0fac6881);	/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0xf);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 7, 0);		/* 0000000f COLOR_MASK */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 8, 0);		/* 00000001 BLEND_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000003 UNK0F90 */
	xf_emit(ctx, 1, 0);		/* 00000001 FRAMEBUFFER_SRGB */
	xf_emit(ctx, 1, 0);		/* 7 */
	xf_emit(ctx, 1, 0);		/* 00000001 LOGIC_OP_ENABLE */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 0);	/* 00000001 UNK1140 */
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	}
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK1534 */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	if (device->chipset >= 0xa0)
		xf_emit(ctx, 1, 0x0fac6881);	/* fffffff */
	xf_emit(ctx, 1, magic2);	/* 001fffff tesla UNK0F78 */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_BOUNDS_EN */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE_ENABLE */
	xf_emit(ctx, 1, 0x11);		/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK0FB0 */
	xf_emit(ctx, 1, 0);		/* ff/3ff */
	xf_emit(ctx, 1, 4);		/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);		/* 00000001 STENCIL_FRONT_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK15B4 */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK19CC */
	xf_emit(ctx, 1, 0);		/* 00000007 */
	xf_emit(ctx, 1, 0);		/* 00000001 SAMPLECNT_ENABLE */
	xf_emit(ctx, 1, 0);		/* 0000000f ZETA_FORMAT */
	xf_emit(ctx, 1, 1);		/* 00000001 ZETA_ENABLE */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
		xf_emit(ctx, 1, 0);	/* 0000000f tesla UNK15C8 */
	}
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A3C */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 3, 0);		/* 7/f, 1, ffff0ff3 */
		xf_emit(ctx, 1, 0xfac6881);	/* fffffff */
		xf_emit(ctx, 4, 0);		/* 1, 1, 1, 3ff */
		xf_emit(ctx, 1, 4);		/* 7 */
		xf_emit(ctx, 1, 0);		/* 1 */
		xf_emit(ctx, 2, 1);		/* 1 */
		xf_emit(ctx, 2, 0);		/* 7, f */
		xf_emit(ctx, 1, 1);		/* 1 */
		xf_emit(ctx, 1, 0);		/* 7/f */
		if (IS_NVA3F(device->chipset))
			xf_emit(ctx, 0x9, 0);	/* 1 */
		else
			xf_emit(ctx, 0x8, 0);	/* 1 */
		xf_emit(ctx, 1, 0);		/* ffff0ff3 */
		xf_emit(ctx, 8, 1);		/* 1 */
		xf_emit(ctx, 1, 0x11);		/* 7f */
		xf_emit(ctx, 7, 0);		/* 7f */
		xf_emit(ctx, 1, 0xfac6881);	/* fffffff */
		xf_emit(ctx, 1, 0xf);		/* f */
		xf_emit(ctx, 7, 0);		/* f */
		xf_emit(ctx, 1, 0x11);		/* 7f */
		xf_emit(ctx, 1, 1);		/* 1 */
		xf_emit(ctx, 5, 0);		/* 1, 7, 3ff, 3, 7 */
		if (IS_NVA3F(device->chipset)) {
			xf_emit(ctx, 1, 0);	/* 00000001 UNK1140 */
			xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
		}
	}
}

static void
nv50_gr_construct_xfer_tex(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 2, 0);		/* 1 LINKED_TSC. yes, 2. */
	if (device->chipset != 0x50)
		xf_emit(ctx, 1, 0);	/* 3 */
	xf_emit(ctx, 1, 1);		/* 1ffff BLIT_DU_DX_INT */
	xf_emit(ctx, 1, 0);		/* fffff BLIT_DU_DX_FRACT */
	xf_emit(ctx, 1, 1);		/* 1ffff BLIT_DV_DY_INT */
	xf_emit(ctx, 1, 0);		/* fffff BLIT_DV_DY_FRACT */
	if (device->chipset == 0x50)
		xf_emit(ctx, 1, 0);	/* 3 BLIT_CONTROL */
	else
		xf_emit(ctx, 2, 0);	/* 3ff, 1 */
	xf_emit(ctx, 1, 0x2a712488);	/* ffffffff SRC_TIC_0 */
	xf_emit(ctx, 1, 0);		/* ffffffff SRC_TIC_1 */
	xf_emit(ctx, 1, 0x4085c000);	/* ffffffff SRC_TIC_2 */
	xf_emit(ctx, 1, 0x40);		/* ffffffff SRC_TIC_3 */
	xf_emit(ctx, 1, 0x100);		/* ffffffff SRC_TIC_4 */
	xf_emit(ctx, 1, 0x10100);	/* ffffffff SRC_TIC_5 */
	xf_emit(ctx, 1, 0x02800000);	/* ffffffff SRC_TIC_6 */
	xf_emit(ctx, 1, 0);		/* ffffffff SRC_TIC_7 */
	if (device->chipset == 0x50) {
		xf_emit(ctx, 1, 0);	/* 00000001 turing UNK358 */
		xf_emit(ctx, 1, 0);	/* ffffffff tesla UNK1A34? */
		xf_emit(ctx, 1, 0);	/* 00000003 turing UNK37C tesla UNK1690 */
		xf_emit(ctx, 1, 0);	/* 00000003 BLIT_CONTROL */
		xf_emit(ctx, 1, 0);	/* 00000001 turing UNK32C tesla UNK0F94 */
	} else if (!IS_NVAAF(device->chipset)) {
		xf_emit(ctx, 1, 0);	/* ffffffff tesla UNK1A34? */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* 000003ff */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* 000003ff */
		xf_emit(ctx, 1, 0);	/* 00000003 tesla UNK1664 / turing UNK03E8 */
		xf_emit(ctx, 1, 0);	/* 00000003 */
		xf_emit(ctx, 1, 0);	/* 000003ff */
	} else {
		xf_emit(ctx, 0x6, 0);
	}
	xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A34 */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_TEXTURE */
	xf_emit(ctx, 1, 0);		/* 0000ffff DMA_SRC */
}

static void
nv50_gr_construct_xfer_unk8cxx(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	xf_emit(ctx, 1, 0);		/* 00000001 UNK1534 */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 2, 0);		/* 7, ffff0ff3 */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE */
	xf_emit(ctx, 1, 0x04e3bfdf);	/* ffffffff UNK0D64 */
	xf_emit(ctx, 1, 0x04e3bfdf);	/* ffffffff UNK0DF4 */
	xf_emit(ctx, 1, 1);		/* 00000001 UNK15B4 */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0x00ffff00);	/* 00ffffff LINE_STIPPLE_PATTERN */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK0F98 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);	/* 0000001f tesla UNK169C */
	xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK1668 */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_STIPPLE_ENABLE */
	xf_emit(ctx, 1, 0x00ffff00);	/* 00ffffff LINE_STIPPLE_PATTERN */
	xf_emit(ctx, 1, 0);		/* 00000001 POLYGON_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 UNK1534 */
	xf_emit(ctx, 1, 0);		/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 1, 0);		/* 00000001 tesla UNK1658 */
	xf_emit(ctx, 1, 0);		/* 00000001 LINE_SMOOTH_ENABLE */
	xf_emit(ctx, 1, 0);		/* ffff0ff3 */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_TEST_ENABLE */
	xf_emit(ctx, 1, 0);		/* 00000001 DEPTH_WRITE */
	xf_emit(ctx, 1, 1);		/* 00000001 UNK15B4 */
	xf_emit(ctx, 1, 0);		/* 00000001 POINT_SPRITE_ENABLE */
	xf_emit(ctx, 1, 1);		/* 00000001 tesla UNK165C */
	xf_emit(ctx, 1, 0x30201000);	/* ffffffff tesla UNK1670 */
	xf_emit(ctx, 1, 0x70605040);	/* ffffffff tesla UNK1670 */
	xf_emit(ctx, 1, 0xb8a89888);	/* ffffffff tesla UNK1670 */
	xf_emit(ctx, 1, 0xf8e8d8c8);	/* ffffffff tesla UNK1670 */
	xf_emit(ctx, 1, 0);		/* 00000001 VERTEX_TWO_SIDE_ENABLE */
	xf_emit(ctx, 1, 0x1a);		/* 0000001f POLYGON_MODE */
}

static void
nv50_gr_construct_xfer_tp(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	if (device->chipset < 0xa0) {
		nv50_gr_construct_xfer_unk84xx(ctx);
		nv50_gr_construct_xfer_tprop(ctx);
		nv50_gr_construct_xfer_tex(ctx);
		nv50_gr_construct_xfer_unk8cxx(ctx);
	} else {
		nv50_gr_construct_xfer_tex(ctx);
		nv50_gr_construct_xfer_tprop(ctx);
		nv50_gr_construct_xfer_unk8cxx(ctx);
		nv50_gr_construct_xfer_unk84xx(ctx);
	}
}

static void
nv50_gr_construct_xfer_mpc(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i, mpcnt = 2;
	switch (device->chipset) {
		case 0x98:
		case 0xaa:
			mpcnt = 1;
			break;
		case 0x50:
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa8:
		case 0xac:
			mpcnt = 2;
			break;
		case 0xa0:
		case 0xa3:
		case 0xa5:
		case 0xaf:
			mpcnt = 3;
			break;
	}
	for (i = 0; i < mpcnt; i++) {
		xf_emit(ctx, 1, 0);		/* ff */
		xf_emit(ctx, 1, 0x80);		/* ffffffff tesla UNK1404 */
		xf_emit(ctx, 1, 0x80007004);	/* ffffffff tesla UNK12B0 */
		xf_emit(ctx, 1, 0x04000400);	/* ffffffff */
		if (device->chipset >= 0xa0)
			xf_emit(ctx, 1, 0xc0);	/* 00007fff tesla UNK152C */
		xf_emit(ctx, 1, 0x1000);	/* 0000ffff tesla UNK0D60 */
		xf_emit(ctx, 1, 0);		/* ff/3ff */
		xf_emit(ctx, 1, 0);		/* ffffffff tesla UNK1A30 */
		if (device->chipset == 0x86 || device->chipset == 0x98 || device->chipset == 0xa8 || IS_NVAAF(device->chipset)) {
			xf_emit(ctx, 1, 0xe00);		/* 7fff */
			xf_emit(ctx, 1, 0x1e00);	/* 7fff */
		}
		xf_emit(ctx, 1, 1);		/* 000000ff VP_REG_ALLOC_TEMP */
		xf_emit(ctx, 1, 0);		/* 00000001 LINKED_TSC */
		xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
		if (device->chipset == 0x50)
			xf_emit(ctx, 2, 0x1000);	/* 7fff tesla UNK141C */
		xf_emit(ctx, 1, 1);		/* 000000ff GP_REG_ALLOC_TEMP */
		xf_emit(ctx, 1, 0);		/* 00000001 GP_ENABLE */
		xf_emit(ctx, 1, 4);		/* 000000ff FP_REG_ALLOC_TEMP */
		xf_emit(ctx, 1, 2);		/* 00000003 REG_MODE */
		if (IS_NVAAF(device->chipset))
			xf_emit(ctx, 0xb, 0);	/* RO */
		else if (device->chipset >= 0xa0)
			xf_emit(ctx, 0xc, 0);	/* RO */
		else
			xf_emit(ctx, 0xa, 0);	/* RO */
	}
	xf_emit(ctx, 1, 0x08100c12);		/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 0);			/* ff/3ff */
	if (device->chipset >= 0xa0) {
		xf_emit(ctx, 1, 0x1fe21);	/* 0003ffff tesla UNK0FAC */
	}
	xf_emit(ctx, 3, 0);			/* 7fff, 0, 0 */
	xf_emit(ctx, 1, 0);			/* 00000001 tesla UNK1534 */
	xf_emit(ctx, 1, 0);			/* 7/f MULTISAMPLE_SAMPLES_LOG2 */
	xf_emit(ctx, 4, 0xffff);		/* 0000ffff MSAA_MASK */
	xf_emit(ctx, 1, 1);			/* 00000001 LANES32 */
	xf_emit(ctx, 1, 0x10001);		/* 00ffffff BLOCK_ALLOC */
	xf_emit(ctx, 1, 0x10001);		/* ffffffff BLOCKDIM_XY */
	xf_emit(ctx, 1, 1);			/* 0000ffff BLOCKDIM_Z */
	xf_emit(ctx, 1, 0);			/* ffffffff SHARED_SIZE */
	xf_emit(ctx, 1, 0x1fe21);		/* 1ffff/3ffff[NVA0+] tesla UNk0FAC */
	xf_emit(ctx, 1, 0);			/* ffffffff tesla UNK1A34 */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 1);		/* 0000001f tesla UNK169C */
	xf_emit(ctx, 1, 0);			/* ff/3ff */
	xf_emit(ctx, 1, 0);			/* 1 LINKED_TSC */
	xf_emit(ctx, 1, 0);			/* ff FP_ADDRESS_HIGH */
	xf_emit(ctx, 1, 0);			/* ffffffff FP_ADDRESS_LOW */
	xf_emit(ctx, 1, 0x08100c12);		/* 1fffffff FP_INTERPOLANT_CTRL */
	xf_emit(ctx, 1, 4);			/* 00000007 FP_CONTROL */
	xf_emit(ctx, 1, 0);			/* 000000ff FRAG_COLOR_CLAMP_EN */
	xf_emit(ctx, 1, 2);			/* 00000003 REG_MODE */
	xf_emit(ctx, 1, 0x11);			/* 0000007f RT_FORMAT */
	xf_emit(ctx, 7, 0);			/* 0000007f RT_FORMAT */
	xf_emit(ctx, 1, 0);			/* 00000007 */
	xf_emit(ctx, 1, 0xfac6881);		/* 0fffffff RT_CONTROL */
	xf_emit(ctx, 1, 0);			/* 00000003 MULTISAMPLE_CTRL */
	if (IS_NVA3F(device->chipset))
		xf_emit(ctx, 1, 3);		/* 00000003 tesla UNK16B4 */
	xf_emit(ctx, 1, 0);			/* 00000001 ALPHA_TEST_ENABLE */
	xf_emit(ctx, 1, 0);			/* 00000007 ALPHA_TEST_FUNC */
	xf_emit(ctx, 1, 0);			/* 00000001 FRAMEBUFFER_SRGB */
	xf_emit(ctx, 1, 4);			/* ffffffff tesla UNK1400 */
	xf_emit(ctx, 8, 0);			/* 00000001 BLEND_ENABLE */
	xf_emit(ctx, 1, 0);			/* 00000001 LOGIC_OP_ENABLE */
	xf_emit(ctx, 1, 2);			/* 0000001f BLEND_FUNC_SRC_RGB */
	xf_emit(ctx, 1, 1);			/* 0000001f BLEND_FUNC_DST_RGB */
	xf_emit(ctx, 1, 1);			/* 00000007 BLEND_EQUATION_RGB */
	xf_emit(ctx, 1, 2);			/* 0000001f BLEND_FUNC_SRC_ALPHA */
	xf_emit(ctx, 1, 1);			/* 0000001f BLEND_FUNC_DST_ALPHA */
	xf_emit(ctx, 1, 1);			/* 00000007 BLEND_EQUATION_ALPHA */
	xf_emit(ctx, 1, 1);			/* 00000001 UNK133C */
	if (IS_NVA3F(device->chipset)) {
		xf_emit(ctx, 1, 0);		/* 00000001 UNK12E4 */
		xf_emit(ctx, 8, 2);		/* 0000001f IBLEND_FUNC_SRC_RGB */
		xf_emit(ctx, 8, 1);		/* 0000001f IBLEND_FUNC_DST_RGB */
		xf_emit(ctx, 8, 1);		/* 00000007 IBLEND_EQUATION_RGB */
		xf_emit(ctx, 8, 2);		/* 0000001f IBLEND_FUNC_SRC_ALPHA */
		xf_emit(ctx, 8, 1);		/* 0000001f IBLEND_FUNC_DST_ALPHA */
		xf_emit(ctx, 8, 1);		/* 00000007 IBLEND_EQUATION_ALPHA */
		xf_emit(ctx, 8, 1);		/* 00000001 IBLEND_UNK00 */
		xf_emit(ctx, 1, 0);		/* 00000003 tesla UNK1928 */
		xf_emit(ctx, 1, 0);		/* 00000001 UNK1140 */
	}
	xf_emit(ctx, 1, 0);			/* 00000003 tesla UNK0F90 */
	xf_emit(ctx, 1, 4);			/* 000000ff FP_RESULT_COUNT */
	/* XXX: demagic this part some day */
	if (device->chipset == 0x50)
		xf_emit(ctx, 0x3a0, 0);
	else if (device->chipset < 0x94)
		xf_emit(ctx, 0x3a2, 0);
	else if (device->chipset == 0x98 || device->chipset == 0xaa)
		xf_emit(ctx, 0x39f, 0);
	else
		xf_emit(ctx, 0x3a3, 0);
	xf_emit(ctx, 1, 0x11);			/* 3f/7f DST_FORMAT */
	xf_emit(ctx, 1, 0);			/* 7 OPERATION */
	xf_emit(ctx, 1, 1);			/* 1 DST_LINEAR */
	xf_emit(ctx, 0x2d, 0);
}

static void
nv50_gr_construct_xfer2(struct nvkm_grctx *ctx)
{
	struct nvkm_device *device = ctx->device;
	int i;
	u32 offset;
	u32 units = nvkm_rd32(device, 0x1540);
	int size = 0;

	offset = (ctx->ctxvals_pos+0x3f)&~0x3f;

	if (device->chipset < 0xa0) {
		for (i = 0; i < 8; i++) {
			ctx->ctxvals_pos = offset + i;
			/* that little bugger belongs to csched. No idea
			 * what it's doing here. */
			if (i == 0)
				xf_emit(ctx, 1, 0x08100c12); /* FP_INTERPOLANT_CTRL */
			if (units & (1 << i))
				nv50_gr_construct_xfer_mpc(ctx);
			if ((ctx->ctxvals_pos-offset)/8 > size)
				size = (ctx->ctxvals_pos-offset)/8;
		}
	} else {
		/* Strand 0: TPs 0, 1 */
		ctx->ctxvals_pos = offset;
		/* that little bugger belongs to csched. No idea
		 * what it's doing here. */
		xf_emit(ctx, 1, 0x08100c12); /* FP_INTERPOLANT_CTRL */
		if (units & (1 << 0))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 1))
			nv50_gr_construct_xfer_mpc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 1: TPs 2, 3 */
		ctx->ctxvals_pos = offset + 1;
		if (units & (1 << 2))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 3))
			nv50_gr_construct_xfer_mpc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 2: TPs 4, 5, 6 */
		ctx->ctxvals_pos = offset + 2;
		if (units & (1 << 4))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 5))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 6))
			nv50_gr_construct_xfer_mpc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;

		/* Strand 3: TPs 7, 8, 9 */
		ctx->ctxvals_pos = offset + 3;
		if (units & (1 << 7))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 8))
			nv50_gr_construct_xfer_mpc(ctx);
		if (units & (1 << 9))
			nv50_gr_construct_xfer_mpc(ctx);
		if ((ctx->ctxvals_pos-offset)/8 > size)
			size = (ctx->ctxvals_pos-offset)/8;
	}
	ctx->ctxvals_pos = offset + size * 8;
	ctx->ctxvals_pos = (ctx->ctxvals_pos+0x3f)&~0x3f;
	cp_lsr (ctx, offset);
	cp_out (ctx, CP_SET_XFER_POINTER);
	cp_lsr (ctx, size);
	cp_out (ctx, CP_SEEK_2);
	cp_out (ctx, CP_XFER_2);
	cp_wait(ctx, XFER, BUSY);
}
