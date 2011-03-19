/*
 * Copyright 2009 Red Hat Inc.
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

/* NVIDIA context programs handle a number of other conditions which are
 * not implemented in our versions.  It's not clear why NVIDIA context
 * programs have this code, nor whether it's strictly necessary for
 * correct operation.  We'll implement additional handling if/when we
 * discover it's necessary.
 *
 * - On context save, NVIDIA set 0x400314 bit 0 to 1 if the "3D state"
 *   flag is set, this gets saved into the context.
 * - On context save, the context program for all cards load nsource
 *   into a flag register and check for ILLEGAL_MTHD.  If it's set,
 *   opcode 0x60000d is called before resuming normal operation.
 * - Some context programs check more conditions than the above.  NV44
 *   checks: ((nsource & 0x0857) || (0x400718 & 0x0100) || (intr & 0x0001))
 *   and calls 0x60000d before resuming normal operation.
 * - At the very beginning of NVIDIA's context programs, flag 9 is checked
 *   and if true 0x800001 is called with count=0, pos=0, the flag is cleared
 *   and then the ctxprog is aborted.  It looks like a complicated NOP,
 *   its purpose is unknown.
 * - In the section of code that loads the per-vs state, NVIDIA check
 *   flag 10.  If it's set, they only transfer the small 0x300 byte block
 *   of state + the state for a single vs as opposed to the state for
 *   all vs units.  It doesn't seem likely that it'll occur in normal
 *   operation, especially seeing as it appears NVIDIA may have screwed
 *   up the ctxprogs for some cards and have an invalid instruction
 *   rather than a cp_lsr(ctx, dwords_for_1_vs_unit) instruction.
 * - There's a number of places where context offset 0 (where we place
 *   the PRAMIN offset of the context) is loaded into either 0x408000,
 *   0x408004 or 0x408008.  Not sure what's up there either.
 * - The ctxprogs for some cards save 0x400a00 again during the cleanup
 *   path for auto-loadctx.
 */

#define CP_FLAG_CLEAR                 0
#define CP_FLAG_SET                   1
#define CP_FLAG_SWAP_DIRECTION        ((0 * 32) + 0)
#define CP_FLAG_SWAP_DIRECTION_LOAD   0
#define CP_FLAG_SWAP_DIRECTION_SAVE   1
#define CP_FLAG_USER_SAVE             ((0 * 32) + 5)
#define CP_FLAG_USER_SAVE_NOT_PENDING 0
#define CP_FLAG_USER_SAVE_PENDING     1
#define CP_FLAG_USER_LOAD             ((0 * 32) + 6)
#define CP_FLAG_USER_LOAD_NOT_PENDING 0
#define CP_FLAG_USER_LOAD_PENDING     1
#define CP_FLAG_STATUS                ((3 * 32) + 0)
#define CP_FLAG_STATUS_IDLE           0
#define CP_FLAG_STATUS_BUSY           1
#define CP_FLAG_AUTO_SAVE             ((3 * 32) + 4)
#define CP_FLAG_AUTO_SAVE_NOT_PENDING 0
#define CP_FLAG_AUTO_SAVE_PENDING     1
#define CP_FLAG_AUTO_LOAD             ((3 * 32) + 5)
#define CP_FLAG_AUTO_LOAD_NOT_PENDING 0
#define CP_FLAG_AUTO_LOAD_PENDING     1
#define CP_FLAG_UNK54                 ((3 * 32) + 6)
#define CP_FLAG_UNK54_CLEAR           0
#define CP_FLAG_UNK54_SET             1
#define CP_FLAG_ALWAYS                ((3 * 32) + 8)
#define CP_FLAG_ALWAYS_FALSE          0
#define CP_FLAG_ALWAYS_TRUE           1
#define CP_FLAG_UNK57                 ((3 * 32) + 9)
#define CP_FLAG_UNK57_CLEAR           0
#define CP_FLAG_UNK57_SET             1

#define CP_CTX                   0x00100000
#define CP_CTX_COUNT             0x000fc000
#define CP_CTX_COUNT_SHIFT               14
#define CP_CTX_REG               0x00003fff
#define CP_LOAD_SR               0x00200000
#define CP_LOAD_SR_VALUE         0x000fffff
#define CP_BRA                   0x00400000
#define CP_BRA_IP                0x0000ff00
#define CP_BRA_IP_SHIFT                   8
#define CP_BRA_IF_CLEAR          0x00000080
#define CP_BRA_FLAG              0x0000007f
#define CP_WAIT                  0x00500000
#define CP_WAIT_SET              0x00000080
#define CP_WAIT_FLAG             0x0000007f
#define CP_SET                   0x00700000
#define CP_SET_1                 0x00000080
#define CP_SET_FLAG              0x0000007f
#define CP_NEXT_TO_SWAP          0x00600007
#define CP_NEXT_TO_CURRENT       0x00600009
#define CP_SET_CONTEXT_POINTER   0x0060000a
#define CP_END                   0x0060000e
#define CP_LOAD_MAGIC_UNK01      0x00800001 /* unknown */
#define CP_LOAD_MAGIC_NV44TCL    0x00800029 /* per-vs state (0x4497) */
#define CP_LOAD_MAGIC_NV40TCL    0x00800041 /* per-vs state (0x4097) */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_grctx.h"

/* TODO:
 *  - get vs count from 0x1540
 */

static int
nv40_graph_vs_count(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	switch (dev_priv->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		return 8;
	case 0x40:
		return 6;
	case 0x41:
	case 0x42:
		return 5;
	case 0x43:
	case 0x44:
	case 0x46:
	case 0x4a:
		return 3;
	case 0x4c:
	case 0x4e:
	case 0x67:
	default:
		return 1;
	}
}


enum cp_label {
	cp_check_load = 1,
	cp_setup_auto_load,
	cp_setup_load,
	cp_setup_save,
	cp_swap_state,
	cp_swap_state3d_3_is_save,
	cp_prepare_exit,
	cp_exit,
};

static void
nv40_graph_construct_general(struct nouveau_grctx *ctx)
{
	struct drm_nouveau_private *dev_priv = ctx->dev->dev_private;
	int i;

	cp_ctx(ctx, 0x4000a4, 1);
	gr_def(ctx, 0x4000a4, 0x00000008);
	cp_ctx(ctx, 0x400144, 58);
	gr_def(ctx, 0x400144, 0x00000001);
	cp_ctx(ctx, 0x400314, 1);
	gr_def(ctx, 0x400314, 0x00000000);
	cp_ctx(ctx, 0x400400, 10);
	cp_ctx(ctx, 0x400480, 10);
	cp_ctx(ctx, 0x400500, 19);
	gr_def(ctx, 0x400514, 0x00040000);
	gr_def(ctx, 0x400524, 0x55555555);
	gr_def(ctx, 0x400528, 0x55555555);
	gr_def(ctx, 0x40052c, 0x55555555);
	gr_def(ctx, 0x400530, 0x55555555);
	cp_ctx(ctx, 0x400560, 6);
	gr_def(ctx, 0x400568, 0x0000ffff);
	gr_def(ctx, 0x40056c, 0x0000ffff);
	cp_ctx(ctx, 0x40057c, 5);
	cp_ctx(ctx, 0x400710, 3);
	gr_def(ctx, 0x400710, 0x20010001);
	gr_def(ctx, 0x400714, 0x0f73ef00);
	cp_ctx(ctx, 0x400724, 1);
	gr_def(ctx, 0x400724, 0x02008821);
	cp_ctx(ctx, 0x400770, 3);
	if (dev_priv->chipset == 0x40) {
		cp_ctx(ctx, 0x400814, 4);
		cp_ctx(ctx, 0x400828, 5);
		cp_ctx(ctx, 0x400840, 5);
		gr_def(ctx, 0x400850, 0x00000040);
		cp_ctx(ctx, 0x400858, 4);
		gr_def(ctx, 0x400858, 0x00000040);
		gr_def(ctx, 0x40085c, 0x00000040);
		gr_def(ctx, 0x400864, 0x80000000);
		cp_ctx(ctx, 0x40086c, 9);
		gr_def(ctx, 0x40086c, 0x80000000);
		gr_def(ctx, 0x400870, 0x80000000);
		gr_def(ctx, 0x400874, 0x80000000);
		gr_def(ctx, 0x400878, 0x80000000);
		gr_def(ctx, 0x400888, 0x00000040);
		gr_def(ctx, 0x40088c, 0x80000000);
		cp_ctx(ctx, 0x4009c0, 8);
		gr_def(ctx, 0x4009cc, 0x80000000);
		gr_def(ctx, 0x4009dc, 0x80000000);
	} else {
		cp_ctx(ctx, 0x400840, 20);
		if (nv44_graph_class(ctx->dev)) {
			for (i = 0; i < 8; i++)
				gr_def(ctx, 0x400860 + (i * 4), 0x00000001);
		}
		gr_def(ctx, 0x400880, 0x00000040);
		gr_def(ctx, 0x400884, 0x00000040);
		gr_def(ctx, 0x400888, 0x00000040);
		cp_ctx(ctx, 0x400894, 11);
		gr_def(ctx, 0x400894, 0x00000040);
		if (!nv44_graph_class(ctx->dev)) {
			for (i = 0; i < 8; i++)
				gr_def(ctx, 0x4008a0 + (i * 4), 0x80000000);
		}
		cp_ctx(ctx, 0x4008e0, 2);
		cp_ctx(ctx, 0x4008f8, 2);
		if (dev_priv->chipset == 0x4c ||
		    (dev_priv->chipset & 0xf0) == 0x60)
			cp_ctx(ctx, 0x4009f8, 1);
	}
	cp_ctx(ctx, 0x400a00, 73);
	gr_def(ctx, 0x400b0c, 0x0b0b0b0c);
	cp_ctx(ctx, 0x401000, 4);
	cp_ctx(ctx, 0x405004, 1);
	switch (dev_priv->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		cp_ctx(ctx, 0x403448, 1);
		gr_def(ctx, 0x403448, 0x00001010);
		break;
	default:
		cp_ctx(ctx, 0x403440, 1);
		switch (dev_priv->chipset) {
		case 0x40:
			gr_def(ctx, 0x403440, 0x00000010);
			break;
		case 0x44:
		case 0x46:
		case 0x4a:
			gr_def(ctx, 0x403440, 0x00003010);
			break;
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x4c:
		case 0x4e:
		case 0x67:
		default:
			gr_def(ctx, 0x403440, 0x00001010);
			break;
		}
		break;
	}
}

static void
nv40_graph_construct_state3d(struct nouveau_grctx *ctx)
{
	struct drm_nouveau_private *dev_priv = ctx->dev->dev_private;
	int i;

	if (dev_priv->chipset == 0x40) {
		cp_ctx(ctx, 0x401880, 51);
		gr_def(ctx, 0x401940, 0x00000100);
	} else
	if (dev_priv->chipset == 0x46 || dev_priv->chipset == 0x47 ||
	    dev_priv->chipset == 0x49 || dev_priv->chipset == 0x4b) {
		cp_ctx(ctx, 0x401880, 32);
		for (i = 0; i < 16; i++)
			gr_def(ctx, 0x401880 + (i * 4), 0x00000111);
		if (dev_priv->chipset == 0x46)
			cp_ctx(ctx, 0x401900, 16);
		cp_ctx(ctx, 0x401940, 3);
	}
	cp_ctx(ctx, 0x40194c, 18);
	gr_def(ctx, 0x401954, 0x00000111);
	gr_def(ctx, 0x401958, 0x00080060);
	gr_def(ctx, 0x401974, 0x00000080);
	gr_def(ctx, 0x401978, 0xffff0000);
	gr_def(ctx, 0x40197c, 0x00000001);
	gr_def(ctx, 0x401990, 0x46400000);
	if (dev_priv->chipset == 0x40) {
		cp_ctx(ctx, 0x4019a0, 2);
		cp_ctx(ctx, 0x4019ac, 5);
	} else {
		cp_ctx(ctx, 0x4019a0, 1);
		cp_ctx(ctx, 0x4019b4, 3);
	}
	gr_def(ctx, 0x4019bc, 0xffff0000);
	switch (dev_priv->chipset) {
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
		cp_ctx(ctx, 0x4019c0, 18);
		for (i = 0; i < 16; i++)
			gr_def(ctx, 0x4019c0 + (i * 4), 0x88888888);
		break;
	}
	cp_ctx(ctx, 0x401a08, 8);
	gr_def(ctx, 0x401a10, 0x0fff0000);
	gr_def(ctx, 0x401a14, 0x0fff0000);
	gr_def(ctx, 0x401a1c, 0x00011100);
	cp_ctx(ctx, 0x401a2c, 4);
	cp_ctx(ctx, 0x401a44, 26);
	for (i = 0; i < 16; i++)
		gr_def(ctx, 0x401a44 + (i * 4), 0x07ff0000);
	gr_def(ctx, 0x401a8c, 0x4b7fffff);
	if (dev_priv->chipset == 0x40) {
		cp_ctx(ctx, 0x401ab8, 3);
	} else {
		cp_ctx(ctx, 0x401ab8, 1);
		cp_ctx(ctx, 0x401ac0, 1);
	}
	cp_ctx(ctx, 0x401ad0, 8);
	gr_def(ctx, 0x401ad0, 0x30201000);
	gr_def(ctx, 0x401ad4, 0x70605040);
	gr_def(ctx, 0x401ad8, 0xb8a89888);
	gr_def(ctx, 0x401adc, 0xf8e8d8c8);
	cp_ctx(ctx, 0x401b10, dev_priv->chipset == 0x40 ? 2 : 1);
	gr_def(ctx, 0x401b10, 0x40100000);
	cp_ctx(ctx, 0x401b18, dev_priv->chipset == 0x40 ? 6 : 5);
	gr_def(ctx, 0x401b28, dev_priv->chipset == 0x40 ?
			      0x00000004 : 0x00000000);
	cp_ctx(ctx, 0x401b30, 25);
	gr_def(ctx, 0x401b34, 0x0000ffff);
	gr_def(ctx, 0x401b68, 0x435185d6);
	gr_def(ctx, 0x401b6c, 0x2155b699);
	gr_def(ctx, 0x401b70, 0xfedcba98);
	gr_def(ctx, 0x401b74, 0x00000098);
	gr_def(ctx, 0x401b84, 0xffffffff);
	gr_def(ctx, 0x401b88, 0x00ff7000);
	gr_def(ctx, 0x401b8c, 0x0000ffff);
	if (dev_priv->chipset != 0x44 && dev_priv->chipset != 0x4a &&
	    dev_priv->chipset != 0x4e)
		cp_ctx(ctx, 0x401b94, 1);
	cp_ctx(ctx, 0x401b98, 8);
	gr_def(ctx, 0x401b9c, 0x00ff0000);
	cp_ctx(ctx, 0x401bc0, 9);
	gr_def(ctx, 0x401be0, 0x00ffff00);
	cp_ctx(ctx, 0x401c00, 192);
	for (i = 0; i < 16; i++) { /* fragment texture units */
		gr_def(ctx, 0x401c40 + (i * 4), 0x00018488);
		gr_def(ctx, 0x401c80 + (i * 4), 0x00028202);
		gr_def(ctx, 0x401d00 + (i * 4), 0x0000aae4);
		gr_def(ctx, 0x401d40 + (i * 4), 0x01012000);
		gr_def(ctx, 0x401d80 + (i * 4), 0x00080008);
		gr_def(ctx, 0x401e00 + (i * 4), 0x00100008);
	}
	for (i = 0; i < 4; i++) { /* vertex texture units */
		gr_def(ctx, 0x401e90 + (i * 4), 0x0001bc80);
		gr_def(ctx, 0x401ea0 + (i * 4), 0x00000202);
		gr_def(ctx, 0x401ec0 + (i * 4), 0x00000008);
		gr_def(ctx, 0x401ee0 + (i * 4), 0x00080008);
	}
	cp_ctx(ctx, 0x400f5c, 3);
	gr_def(ctx, 0x400f5c, 0x00000002);
	cp_ctx(ctx, 0x400f84, 1);
}

static void
nv40_graph_construct_state3d_2(struct nouveau_grctx *ctx)
{
	struct drm_nouveau_private *dev_priv = ctx->dev->dev_private;
	int i;

	cp_ctx(ctx, 0x402000, 1);
	cp_ctx(ctx, 0x402404, dev_priv->chipset == 0x40 ? 1 : 2);
	switch (dev_priv->chipset) {
	case 0x40:
		gr_def(ctx, 0x402404, 0x00000001);
		break;
	case 0x4c:
	case 0x4e:
	case 0x67:
		gr_def(ctx, 0x402404, 0x00000020);
		break;
	case 0x46:
	case 0x49:
	case 0x4b:
		gr_def(ctx, 0x402404, 0x00000421);
		break;
	default:
		gr_def(ctx, 0x402404, 0x00000021);
	}
	if (dev_priv->chipset != 0x40)
		gr_def(ctx, 0x402408, 0x030c30c3);
	switch (dev_priv->chipset) {
	case 0x44:
	case 0x46:
	case 0x4a:
	case 0x4c:
	case 0x4e:
	case 0x67:
		cp_ctx(ctx, 0x402440, 1);
		gr_def(ctx, 0x402440, 0x00011001);
		break;
	default:
		break;
	}
	cp_ctx(ctx, 0x402480, dev_priv->chipset == 0x40 ? 8 : 9);
	gr_def(ctx, 0x402488, 0x3e020200);
	gr_def(ctx, 0x40248c, 0x00ffffff);
	switch (dev_priv->chipset) {
	case 0x40:
		gr_def(ctx, 0x402490, 0x60103f00);
		break;
	case 0x47:
		gr_def(ctx, 0x402490, 0x40103f00);
		break;
	case 0x41:
	case 0x42:
	case 0x49:
	case 0x4b:
		gr_def(ctx, 0x402490, 0x20103f00);
		break;
	default:
		gr_def(ctx, 0x402490, 0x0c103f00);
		break;
	}
	gr_def(ctx, 0x40249c, dev_priv->chipset <= 0x43 ?
			      0x00020000 : 0x00040000);
	cp_ctx(ctx, 0x402500, 31);
	gr_def(ctx, 0x402530, 0x00008100);
	if (dev_priv->chipset == 0x40)
		cp_ctx(ctx, 0x40257c, 6);
	cp_ctx(ctx, 0x402594, 16);
	cp_ctx(ctx, 0x402800, 17);
	gr_def(ctx, 0x402800, 0x00000001);
	switch (dev_priv->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		cp_ctx(ctx, 0x402864, 1);
		gr_def(ctx, 0x402864, 0x00001001);
		cp_ctx(ctx, 0x402870, 3);
		gr_def(ctx, 0x402878, 0x00000003);
		if (dev_priv->chipset != 0x47) { /* belong at end!! */
			cp_ctx(ctx, 0x402900, 1);
			cp_ctx(ctx, 0x402940, 1);
			cp_ctx(ctx, 0x402980, 1);
			cp_ctx(ctx, 0x4029c0, 1);
			cp_ctx(ctx, 0x402a00, 1);
			cp_ctx(ctx, 0x402a40, 1);
			cp_ctx(ctx, 0x402a80, 1);
			cp_ctx(ctx, 0x402ac0, 1);
		}
		break;
	case 0x40:
		cp_ctx(ctx, 0x402844, 1);
		gr_def(ctx, 0x402844, 0x00000001);
		cp_ctx(ctx, 0x402850, 1);
		break;
	default:
		cp_ctx(ctx, 0x402844, 1);
		gr_def(ctx, 0x402844, 0x00001001);
		cp_ctx(ctx, 0x402850, 2);
		gr_def(ctx, 0x402854, 0x00000003);
		break;
	}

	cp_ctx(ctx, 0x402c00, 4);
	gr_def(ctx, 0x402c00, dev_priv->chipset == 0x40 ?
			      0x80800001 : 0x00888001);
	switch (dev_priv->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		cp_ctx(ctx, 0x402c20, 40);
		for (i = 0; i < 32; i++)
			gr_def(ctx, 0x402c40 + (i * 4), 0xffffffff);
		cp_ctx(ctx, 0x4030b8, 13);
		gr_def(ctx, 0x4030dc, 0x00000005);
		gr_def(ctx, 0x4030e8, 0x0000ffff);
		break;
	default:
		cp_ctx(ctx, 0x402c10, 4);
		if (dev_priv->chipset == 0x40)
			cp_ctx(ctx, 0x402c20, 36);
		else
		if (dev_priv->chipset <= 0x42)
			cp_ctx(ctx, 0x402c20, 24);
		else
		if (dev_priv->chipset <= 0x4a)
			cp_ctx(ctx, 0x402c20, 16);
		else
			cp_ctx(ctx, 0x402c20, 8);
		cp_ctx(ctx, 0x402cb0, dev_priv->chipset == 0x40 ? 12 : 13);
		gr_def(ctx, 0x402cd4, 0x00000005);
		if (dev_priv->chipset != 0x40)
			gr_def(ctx, 0x402ce0, 0x0000ffff);
		break;
	}

	cp_ctx(ctx, 0x403400, dev_priv->chipset == 0x40 ? 4 : 3);
	cp_ctx(ctx, 0x403410, dev_priv->chipset == 0x40 ? 4 : 3);
	cp_ctx(ctx, 0x403420, nv40_graph_vs_count(ctx->dev));
	for (i = 0; i < nv40_graph_vs_count(ctx->dev); i++)
		gr_def(ctx, 0x403420 + (i * 4), 0x00005555);

	if (dev_priv->chipset != 0x40) {
		cp_ctx(ctx, 0x403600, 1);
		gr_def(ctx, 0x403600, 0x00000001);
	}
	cp_ctx(ctx, 0x403800, 1);

	cp_ctx(ctx, 0x403c18, 1);
	gr_def(ctx, 0x403c18, 0x00000001);
	switch (dev_priv->chipset) {
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
		cp_ctx(ctx, 0x405018, 1);
		gr_def(ctx, 0x405018, 0x08e00001);
		cp_ctx(ctx, 0x405c24, 1);
		gr_def(ctx, 0x405c24, 0x000e3000);
		break;
	}
	if (dev_priv->chipset != 0x4e)
		cp_ctx(ctx, 0x405800, 11);
	cp_ctx(ctx, 0x407000, 1);
}

static void
nv40_graph_construct_state3d_3(struct nouveau_grctx *ctx)
{
	int len = nv44_graph_class(ctx->dev) ? 0x0084 : 0x0684;

	cp_out (ctx, 0x300000);
	cp_lsr (ctx, len - 4);
	cp_bra (ctx, SWAP_DIRECTION, SAVE, cp_swap_state3d_3_is_save);
	cp_lsr (ctx, len);
	cp_name(ctx, cp_swap_state3d_3_is_save);
	cp_out (ctx, 0x800001);

	ctx->ctxvals_pos += len;
}

static void
nv40_graph_construct_shader(struct nouveau_grctx *ctx)
{
	struct drm_device *dev = ctx->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *obj = ctx->data;
	int vs, vs_nr, vs_len, vs_nr_b0, vs_nr_b1, b0_offset, b1_offset;
	int offset, i;

	vs_nr    = nv40_graph_vs_count(ctx->dev);
	vs_nr_b0 = 363;
	vs_nr_b1 = dev_priv->chipset == 0x40 ? 128 : 64;
	if (dev_priv->chipset == 0x40) {
		b0_offset = 0x2200/4; /* 33a0 */
		b1_offset = 0x55a0/4; /* 1500 */
		vs_len = 0x6aa0/4;
	} else
	if (dev_priv->chipset == 0x41 || dev_priv->chipset == 0x42) {
		b0_offset = 0x2200/4; /* 2200 */
		b1_offset = 0x4400/4; /* 0b00 */
		vs_len = 0x4f00/4;
	} else {
		b0_offset = 0x1d40/4; /* 2200 */
		b1_offset = 0x3f40/4; /* 0b00 : 0a40 */
		vs_len = nv44_graph_class(dev) ? 0x4980/4 : 0x4a40/4;
	}

	cp_lsr(ctx, vs_len * vs_nr + 0x300/4);
	cp_out(ctx, nv44_graph_class(dev) ? 0x800029 : 0x800041);

	offset = ctx->ctxvals_pos;
	ctx->ctxvals_pos += (0x0300/4 + (vs_nr * vs_len));

	if (ctx->mode != NOUVEAU_GRCTX_VALS)
		return;

	offset += 0x0280/4;
	for (i = 0; i < 16; i++, offset += 2)
		nv_wo32(obj, offset * 4, 0x3f800000);

	for (vs = 0; vs < vs_nr; vs++, offset += vs_len) {
		for (i = 0; i < vs_nr_b0 * 6; i += 6)
			nv_wo32(obj, (offset + b0_offset + i) * 4, 0x00000001);
		for (i = 0; i < vs_nr_b1 * 4; i += 4)
			nv_wo32(obj, (offset + b1_offset + i) * 4, 0x3f800000);
	}
}

void
nv40_grctx_init(struct nouveau_grctx *ctx)
{
	/* decide whether we're loading/unloading the context */
	cp_bra (ctx, AUTO_SAVE, PENDING, cp_setup_save);
	cp_bra (ctx, USER_SAVE, PENDING, cp_setup_save);

	cp_name(ctx, cp_check_load);
	cp_bra (ctx, AUTO_LOAD, PENDING, cp_setup_auto_load);
	cp_bra (ctx, USER_LOAD, PENDING, cp_setup_load);
	cp_bra (ctx, ALWAYS, TRUE, cp_exit);

	/* setup for context load */
	cp_name(ctx, cp_setup_auto_load);
	cp_wait(ctx, STATUS, IDLE);
	cp_out (ctx, CP_NEXT_TO_SWAP);
	cp_name(ctx, cp_setup_load);
	cp_wait(ctx, STATUS, IDLE);
	cp_set (ctx, SWAP_DIRECTION, LOAD);
	cp_out (ctx, 0x00910880); /* ?? */
	cp_out (ctx, 0x00901ffe); /* ?? */
	cp_out (ctx, 0x01940000); /* ?? */
	cp_lsr (ctx, 0x20);
	cp_out (ctx, 0x0060000b); /* ?? */
	cp_wait(ctx, UNK57, CLEAR);
	cp_out (ctx, 0x0060000c); /* ?? */
	cp_bra (ctx, ALWAYS, TRUE, cp_swap_state);

	/* setup for context save */
	cp_name(ctx, cp_setup_save);
	cp_set (ctx, SWAP_DIRECTION, SAVE);

	/* general PGRAPH state */
	cp_name(ctx, cp_swap_state);
	cp_pos (ctx, 0x00020/4);
	nv40_graph_construct_general(ctx);
	cp_wait(ctx, STATUS, IDLE);

	/* 3D state, block 1 */
	cp_bra (ctx, UNK54, CLEAR, cp_prepare_exit);
	nv40_graph_construct_state3d(ctx);
	cp_wait(ctx, STATUS, IDLE);

	/* 3D state, block 2 */
	nv40_graph_construct_state3d_2(ctx);

	/* Some other block of "random" state */
	nv40_graph_construct_state3d_3(ctx);

	/* Per-vertex shader state */
	cp_pos (ctx, ctx->ctxvals_pos);
	nv40_graph_construct_shader(ctx);

	/* pre-exit state updates */
	cp_name(ctx, cp_prepare_exit);
	cp_bra (ctx, SWAP_DIRECTION, SAVE, cp_check_load);
	cp_bra (ctx, USER_SAVE, PENDING, cp_exit);
	cp_out (ctx, CP_NEXT_TO_CURRENT);

	cp_name(ctx, cp_exit);
	cp_set (ctx, USER_SAVE, NOT_PENDING);
	cp_set (ctx, USER_LOAD, NOT_PENDING);
	cp_out (ctx, CP_END);
}

