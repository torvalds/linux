/*
 * Copyright (C) 2008 Maarten Maathuis.
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
#include "drm_mode.h"

#define NOUVEAU_DMA_DEBUG (nouveau_reg_debug & NOUVEAU_REG_DEBUG_EVO)
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_crtc.h"
#include "nv50_display.h"

static void
nv50_cursor_show(struct nouveau_crtc *nv_crtc, bool update)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = nv50_display(dev)->master;
	int ret;

	NV_DEBUG_KMS(dev, "\n");

	if (update && nv_crtc->cursor.visible)
		return;

	ret = RING_SPACE(evo, (dev_priv->chipset != 0x50 ? 5 : 3) + update * 2);
	if (ret) {
		NV_ERROR(dev, "no space while unhiding cursor\n");
		return;
	}

	if (dev_priv->chipset != 0x50) {
		BEGIN_NV04(evo, 0, NV84_EVO_CRTC(nv_crtc->index, CURSOR_DMA), 1);
		OUT_RING(evo, NvEvoVRAM);
	}
	BEGIN_NV04(evo, 0, NV50_EVO_CRTC(nv_crtc->index, CURSOR_CTRL), 2);
	OUT_RING(evo, NV50_EVO_CRTC_CURSOR_CTRL_SHOW);
	OUT_RING(evo, nv_crtc->cursor.offset >> 8);

	if (update) {
		BEGIN_NV04(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING(evo, 0);
		FIRE_RING(evo);
		nv_crtc->cursor.visible = true;
	}
}

static void
nv50_cursor_hide(struct nouveau_crtc *nv_crtc, bool update)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = nv50_display(dev)->master;
	int ret;

	NV_DEBUG_KMS(dev, "\n");

	if (update && !nv_crtc->cursor.visible)
		return;

	ret = RING_SPACE(evo, (dev_priv->chipset != 0x50 ? 5 : 3) + update * 2);
	if (ret) {
		NV_ERROR(dev, "no space while hiding cursor\n");
		return;
	}
	BEGIN_NV04(evo, 0, NV50_EVO_CRTC(nv_crtc->index, CURSOR_CTRL), 2);
	OUT_RING(evo, NV50_EVO_CRTC_CURSOR_CTRL_HIDE);
	OUT_RING(evo, 0);
	if (dev_priv->chipset != 0x50) {
		BEGIN_NV04(evo, 0, NV84_EVO_CRTC(nv_crtc->index, CURSOR_DMA), 1);
		OUT_RING(evo, NV84_EVO_CRTC_CURSOR_DMA_HANDLE_NONE);
	}

	if (update) {
		BEGIN_NV04(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING(evo, 0);
		FIRE_RING(evo);
		nv_crtc->cursor.visible = false;
	}
}

static void
nv50_cursor_set_pos(struct nouveau_crtc *nv_crtc, int x, int y)
{
	struct drm_device *dev = nv_crtc->base.dev;

	nv_crtc->cursor_saved_x = x; nv_crtc->cursor_saved_y = y;
	nv_wr32(dev, NV50_PDISPLAY_CURSOR_USER_POS(nv_crtc->index),
		((y & 0xFFFF) << 16) | (x & 0xFFFF));
	/* Needed to make the cursor move. */
	nv_wr32(dev, NV50_PDISPLAY_CURSOR_USER_POS_CTRL(nv_crtc->index), 0);
}

static void
nv50_cursor_set_offset(struct nouveau_crtc *nv_crtc, uint32_t offset)
{
	NV_DEBUG_KMS(nv_crtc->base.dev, "\n");
	if (offset == nv_crtc->cursor.offset)
		return;

	nv_crtc->cursor.offset = offset;
	if (nv_crtc->cursor.visible) {
		nv_crtc->cursor.visible = false;
		nv_crtc->cursor.show(nv_crtc, true);
	}
}

int
nv50_cursor_init(struct nouveau_crtc *nv_crtc)
{
	nv_crtc->cursor.set_offset = nv50_cursor_set_offset;
	nv_crtc->cursor.set_pos = nv50_cursor_set_pos;
	nv_crtc->cursor.hide = nv50_cursor_hide;
	nv_crtc->cursor.show = nv50_cursor_show;
	return 0;
}
