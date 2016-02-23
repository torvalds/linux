/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "hw.h"

/****************************************************************************\
*                                                                            *
* The video arbitration routines calculate some "magic" numbers.  Fixes      *
* the snow seen when accessing the framebuffer without it.                   *
* It just works (I hope).                                                    *
*                                                                            *
\****************************************************************************/

struct nv_fifo_info {
	int lwm;
	int burst;
};

struct nv_sim_state {
	int pclk_khz;
	int mclk_khz;
	int nvclk_khz;
	int bpp;
	int mem_page_miss;
	int mem_latency;
	int memory_type;
	int memory_width;
	int two_heads;
};

static void
nv04_calc_arb(struct nv_fifo_info *fifo, struct nv_sim_state *arb)
{
	int pagemiss, cas, width, bpp;
	int nvclks, mclks, pclks, crtpagemiss;
	int found, mclk_extra, mclk_loop, cbs, m1, p1;
	int mclk_freq, pclk_freq, nvclk_freq;
	int us_m, us_n, us_p, crtc_drain_rate;
	int cpm_us, us_crt, clwm;

	pclk_freq = arb->pclk_khz;
	mclk_freq = arb->mclk_khz;
	nvclk_freq = arb->nvclk_khz;
	pagemiss = arb->mem_page_miss;
	cas = arb->mem_latency;
	width = arb->memory_width >> 6;
	bpp = arb->bpp;
	cbs = 128;

	pclks = 2;
	nvclks = 10;
	mclks = 13 + cas;
	mclk_extra = 3;
	found = 0;

	while (!found) {
		found = 1;

		mclk_loop = mclks + mclk_extra;
		us_m = mclk_loop * 1000 * 1000 / mclk_freq;
		us_n = nvclks * 1000 * 1000 / nvclk_freq;
		us_p = nvclks * 1000 * 1000 / pclk_freq;

		crtc_drain_rate = pclk_freq * bpp / 8;
		crtpagemiss = 2;
		crtpagemiss += 1;
		cpm_us = crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
		us_crt = cpm_us + us_m + us_n + us_p;
		clwm = us_crt * crtc_drain_rate / (1000 * 1000);
		clwm++;

		m1 = clwm + cbs - 512;
		p1 = m1 * pclk_freq / mclk_freq;
		p1 = p1 * bpp / 8;
		if ((p1 < m1 && m1 > 0) || clwm > 519) {
			found = !mclk_extra;
			mclk_extra--;
		}
		if (clwm < 384)
			clwm = 384;

		fifo->lwm = clwm;
		fifo->burst = cbs;
	}
}

static void
nv10_calc_arb(struct nv_fifo_info *fifo, struct nv_sim_state *arb)
{
	int fill_rate, drain_rate;
	int pclks, nvclks, mclks, xclks;
	int pclk_freq, nvclk_freq, mclk_freq;
	int fill_lat, extra_lat;
	int max_burst_o, max_burst_l;
	int fifo_len, min_lwm, max_lwm;
	const int burst_lat = 80; /* Maximum allowable latency due
				   * to the CRTC FIFO burst. (ns) */

	pclk_freq = arb->pclk_khz;
	nvclk_freq = arb->nvclk_khz;
	mclk_freq = arb->mclk_khz;

	fill_rate = mclk_freq * arb->memory_width / 8; /* kB/s */
	drain_rate = pclk_freq * arb->bpp / 8; /* kB/s */

	fifo_len = arb->two_heads ? 1536 : 1024; /* B */

	/* Fixed FIFO refill latency. */

	pclks = 4;	/* lwm detect. */

	nvclks = 3	/* lwm -> sync. */
		+ 2	/* fbi bus cycles (1 req + 1 busy) */
		+ 1	/* 2 edge sync.  may be very close to edge so
			 * just put one. */
		+ 1	/* fbi_d_rdv_n */
		+ 1	/* Fbi_d_rdata */
		+ 1;	/* crtfifo load */

	mclks = 1	/* 2 edge sync.  may be very close to edge so
			 * just put one. */
		+ 1	/* arb_hp_req */
		+ 5	/* tiling pipeline */
		+ 2	/* latency fifo */
		+ 2	/* memory request to fbio block */
		+ 7;	/* data returned from fbio block */

	/* Need to accumulate 256 bits for read */
	mclks += (arb->memory_type == 0 ? 2 : 1)
		* arb->memory_width / 32;

	fill_lat = mclks * 1000 * 1000 / mclk_freq   /* minimum mclk latency */
		+ nvclks * 1000 * 1000 / nvclk_freq  /* nvclk latency */
		+ pclks * 1000 * 1000 / pclk_freq;   /* pclk latency */

	/* Conditional FIFO refill latency. */

	xclks = 2 * arb->mem_page_miss + mclks /* Extra latency due to
						* the overlay. */
		+ 2 * arb->mem_page_miss       /* Extra pagemiss latency. */
		+ (arb->bpp == 32 ? 8 : 4);    /* Margin of error. */

	extra_lat = xclks * 1000 * 1000 / mclk_freq;

	if (arb->two_heads)
		/* Account for another CRTC. */
		extra_lat += fill_lat + extra_lat + burst_lat;

	/* FIFO burst */

	/* Max burst not leading to overflows. */
	max_burst_o = (1 + fifo_len - extra_lat * drain_rate / (1000 * 1000))
		* (fill_rate / 1000) / ((fill_rate - drain_rate) / 1000);
	fifo->burst = min(max_burst_o, 1024);

	/* Max burst value with an acceptable latency. */
	max_burst_l = burst_lat * fill_rate / (1000 * 1000);
	fifo->burst = min(max_burst_l, fifo->burst);

	fifo->burst = rounddown_pow_of_two(fifo->burst);

	/* FIFO low watermark */

	min_lwm = (fill_lat + extra_lat) * drain_rate / (1000 * 1000) + 1;
	max_lwm = fifo_len - fifo->burst
		+ fill_lat * drain_rate / (1000 * 1000)
		+ fifo->burst * drain_rate / fill_rate;

	fifo->lwm = min_lwm + 10 * (max_lwm - min_lwm) / 100; /* Empirical. */
}

static void
nv04_update_arb(struct drm_device *dev, int VClk, int bpp,
		int *burst, int *lwm)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_object *device = &nouveau_drm(dev)->device.object;
	struct nv_fifo_info fifo_data;
	struct nv_sim_state sim_data;
	int MClk = nouveau_hw_get_clock(dev, PLL_MEMORY);
	int NVClk = nouveau_hw_get_clock(dev, PLL_CORE);
	uint32_t cfg1 = nvif_rd32(device, NV04_PFB_CFG1);

	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	sim_data.bpp = bpp;
	sim_data.two_heads = nv_two_heads(dev);
	if ((dev->pdev->device & 0xffff) == 0x01a0 /*CHIPSET_NFORCE*/ ||
	    (dev->pdev->device & 0xffff) == 0x01f0 /*CHIPSET_NFORCE2*/) {
		uint32_t type;

		pci_read_config_dword(pci_get_bus_and_slot(0, 1), 0x7c, &type);

		sim_data.memory_type = (type >> 12) & 1;
		sim_data.memory_width = 64;
		sim_data.mem_latency = 3;
		sim_data.mem_page_miss = 10;
	} else {
		sim_data.memory_type = nvif_rd32(device, NV04_PFB_CFG0) & 0x1;
		sim_data.memory_width = (nvif_rd32(device, NV_PEXTDEV_BOOT_0) & 0x10) ? 128 : 64;
		sim_data.mem_latency = cfg1 & 0xf;
		sim_data.mem_page_miss = ((cfg1 >> 4) & 0xf) + ((cfg1 >> 31) & 0x1);
	}

	if (drm->device.info.family == NV_DEVICE_INFO_V0_TNT)
		nv04_calc_arb(&fifo_data, &sim_data);
	else
		nv10_calc_arb(&fifo_data, &sim_data);

	*burst = ilog2(fifo_data.burst >> 4);
	*lwm = fifo_data.lwm >> 3;
}

static void
nv20_update_arb(int *burst, int *lwm)
{
	unsigned int fifo_size, burst_size, graphics_lwm;

	fifo_size = 2048;
	burst_size = 512;
	graphics_lwm = fifo_size - burst_size;

	*burst = ilog2(burst_size >> 5);
	*lwm = graphics_lwm >> 3;
}

void
nouveau_calc_arb(struct drm_device *dev, int vclk, int bpp, int *burst, int *lwm)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (drm->device.info.family < NV_DEVICE_INFO_V0_KELVIN)
		nv04_update_arb(dev, vclk, bpp, burst, lwm);
	else if ((dev->pdev->device & 0xfff0) == 0x0240 /*CHIPSET_C51*/ ||
		 (dev->pdev->device & 0xfff0) == 0x03d0 /*CHIPSET_C512*/) {
		*burst = 128;
		*lwm = 0x0480;
	} else
		nv20_update_arb(burst, lwm);
}
