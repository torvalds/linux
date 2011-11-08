/*
 * Copyright 2011 Red Hat Inc.
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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_pm.h"

static u32 read_div(struct drm_device *, int, u32, u32);
static u32 read_pll(struct drm_device *, u32);

static u32
read_vco(struct drm_device *dev, u32 dsrc)
{
	u32 ssrc = nv_rd32(dev, dsrc);
	if (!(ssrc & 0x00000100))
		return read_pll(dev, 0x00e800);
	return read_pll(dev, 0x00e820);
}

static u32
read_pll(struct drm_device *dev, u32 pll)
{
	u32 ctrl = nv_rd32(dev, pll + 0);
	u32 coef = nv_rd32(dev, pll + 4);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk, doff;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll & 0xfff000) {
	case 0x00e000:
		sclk = 27000;
		P = 1;
		break;
	case 0x137000:
		doff = (pll - 0x137000) / 0x20;
		sclk = read_div(dev, doff, 0x137120, 0x137140);
		break;
	case 0x132000:
		switch (pll) {
		case 0x132000:
			sclk = read_pll(dev, 0x132020);
			break;
		case 0x132020:
			sclk = read_div(dev, 0, 0x137320, 0x137330);
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct drm_device *dev, int doff, u32 dsrc, u32 dctl)
{
	u32 ssrc = nv_rd32(dev, dsrc + (doff * 4));
	u32 sctl = nv_rd32(dev, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return 27000;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(dev, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(dev, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_mem(struct drm_device *dev)
{
	u32 ssel = nv_rd32(dev, 0x1373f0);
	if (ssel & 0x00000001)
		return read_div(dev, 0, 0x137300, 0x137310);
	return read_pll(dev, 0x132000);
}

static u32
read_clk(struct drm_device *dev, int clk)
{
	u32 sctl = nv_rd32(dev, 0x137250 + (clk * 4));
	u32 ssel = nv_rd32(dev, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << clk)) {
		if (clk < 7)
			sclk = read_pll(dev, 0x137000 + (clk * 0x20));
		else
			sclk = read_pll(dev, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(dev, clk, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;
	return sclk;
}

int
nvc0_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	perflvl->shader = read_clk(dev, 0x00);
	perflvl->core   = perflvl->shader / 2;
	perflvl->memory = read_mem(dev);
	perflvl->rop    = read_clk(dev, 0x01);
	perflvl->hub07  = read_clk(dev, 0x02);
	perflvl->hub06  = read_clk(dev, 0x07);
	perflvl->hub01  = read_clk(dev, 0x08);
	perflvl->copy   = read_clk(dev, 0x09);
	perflvl->daemon = read_clk(dev, 0x0c);
	perflvl->vdec   = read_clk(dev, 0x0e);
	return 0;
}
