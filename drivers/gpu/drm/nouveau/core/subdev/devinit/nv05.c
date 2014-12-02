/*
 * Copyright (C) 2010 Francisco Jerez.
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

#include <subdev/bios.h>
#include <subdev/bios/bmp.h>
#include <subdev/vga.h>

#include "fbmem.h"
#include "nv04.h"

static void
nv05_devinit_meminit(struct nouveau_devinit *devinit)
{
	static const u8 default_config_tab[][2] = {
		{ 0x24, 0x00 },
		{ 0x28, 0x00 },
		{ 0x24, 0x01 },
		{ 0x1f, 0x00 },
		{ 0x0f, 0x00 },
		{ 0x17, 0x00 },
		{ 0x06, 0x00 },
		{ 0x00, 0x00 }
	};
	struct nv04_devinit_priv *priv = (void *)devinit;
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct io_mapping *fb;
	u32 patt = 0xdeadbeef;
	u16 data;
	u8 strap, ramcfg[2];
	int i, v;

	/* Map the framebuffer aperture */
	fb = fbmem_init(nv_device(priv));
	if (!fb) {
		nv_error(priv, "failed to map fb\n");
		return;
	}

	strap = (nv_rd32(priv, 0x101000) & 0x0000003c) >> 2;
	if ((data = bmp_mem_init_table(bios))) {
		ramcfg[0] = nv_ro08(bios, data + 2 * strap + 0);
		ramcfg[1] = nv_ro08(bios, data + 2 * strap + 1);
	} else {
		ramcfg[0] = default_config_tab[strap][0];
		ramcfg[1] = default_config_tab[strap][1];
	}

	/* Sequencer off */
	nv_wrvgas(priv, 0, 1, nv_rdvgas(priv, 0, 1) | 0x20);

	if (nv_rd32(priv, NV04_PFB_BOOT_0) & NV04_PFB_BOOT_0_UMA_ENABLE)
		goto out;

	nv_mask(priv, NV04_PFB_DEBUG_0, NV04_PFB_DEBUG_0_REFRESH_OFF, 0);

	/* If present load the hardcoded scrambling table */
	if (data) {
		for (i = 0, data += 0x10; i < 8; i++, data += 4) {
			u32 scramble = nv_ro32(bios, data);
			nv_wr32(priv, NV04_PFB_SCRAMBLE(i), scramble);
		}
	}

	/* Set memory type/width/length defaults depending on the straps */
	nv_mask(priv, NV04_PFB_BOOT_0, 0x3f, ramcfg[0]);

	if (ramcfg[1] & 0x80)
		nv_mask(priv, NV04_PFB_CFG0, 0, NV04_PFB_CFG0_SCRAMBLE);

	nv_mask(priv, NV04_PFB_CFG1, 0x700001, (ramcfg[1] & 1) << 20);
	nv_mask(priv, NV04_PFB_CFG1, 0, 1);

	/* Probe memory bus width */
	for (i = 0; i < 4; i++)
		fbmem_poke(fb, 4 * i, patt);

	if (fbmem_peek(fb, 0xc) != patt)
		nv_mask(priv, NV04_PFB_BOOT_0,
			  NV04_PFB_BOOT_0_RAM_WIDTH_128, 0);

	/* Probe memory length */
	v = nv_rd32(priv, NV04_PFB_BOOT_0) & NV04_PFB_BOOT_0_RAM_AMOUNT;

	if (v == NV04_PFB_BOOT_0_RAM_AMOUNT_32MB &&
	    (!fbmem_readback(fb, 0x1000000, ++patt) ||
	     !fbmem_readback(fb, 0, ++patt)))
		nv_mask(priv, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_16MB);

	if (v == NV04_PFB_BOOT_0_RAM_AMOUNT_16MB &&
	    !fbmem_readback(fb, 0x800000, ++patt))
		nv_mask(priv, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);

	if (!fbmem_readback(fb, 0x400000, ++patt))
		nv_mask(priv, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);

out:
	/* Sequencer on */
	nv_wrvgas(priv, 0, 1, nv_rdvgas(priv, 0, 1) & ~0x20);
	fbmem_fini(fb);
}

struct nouveau_oclass *
nv05_devinit_oclass = &(struct nouveau_devinit_impl) {
	.base.handle = NV_SUBDEV(DEVINIT, 0x05),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_devinit_ctor,
		.dtor = nv04_devinit_dtor,
		.init = nv04_devinit_init,
		.fini = nv04_devinit_fini,
	},
	.meminit = nv05_devinit_meminit,
	.pll_set = nv04_devinit_pll_set,
	.post = nvbios_init,
}.base;
