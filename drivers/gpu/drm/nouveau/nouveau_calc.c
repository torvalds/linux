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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv_fifo_info fifo_data;
	struct nv_sim_state sim_data;
	int MClk = nouveau_hw_get_clock(dev, MPLL);
	int NVClk = nouveau_hw_get_clock(dev, NVPLL);
	uint32_t cfg1 = nvReadFB(dev, NV04_PFB_CFG1);

	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	sim_data.bpp = bpp;
	sim_data.two_heads = nv_two_heads(dev);
	if ((dev->pci_device & 0xffff) == 0x01a0 /*CHIPSET_NFORCE*/ ||
	    (dev->pci_device & 0xffff) == 0x01f0 /*CHIPSET_NFORCE2*/) {
		uint32_t type;

		pci_read_config_dword(pci_get_bus_and_slot(0, 1), 0x7c, &type);

		sim_data.memory_type = (type >> 12) & 1;
		sim_data.memory_width = 64;
		sim_data.mem_latency = 3;
		sim_data.mem_page_miss = 10;
	} else {
		sim_data.memory_type = nvReadFB(dev, NV04_PFB_CFG0) & 0x1;
		sim_data.memory_width = (nvReadEXTDEV(dev, NV_PEXTDEV_BOOT_0) & 0x10) ? 128 : 64;
		sim_data.mem_latency = cfg1 & 0xf;
		sim_data.mem_page_miss = ((cfg1 >> 4) & 0xf) + ((cfg1 >> 31) & 0x1);
	}

	if (dev_priv->card_type == NV_04)
		nv04_calc_arb(&fifo_data, &sim_data);
	else
		nv10_calc_arb(&fifo_data, &sim_data);

	*burst = ilog2(fifo_data.burst >> 4);
	*lwm = fifo_data.lwm >> 3;
}

static void
nv30_update_arb(int *burst, int *lwm)
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type < NV_30)
		nv04_update_arb(dev, vclk, bpp, burst, lwm);
	else if ((dev->pci_device & 0xfff0) == 0x0240 /*CHIPSET_C51*/ ||
		 (dev->pci_device & 0xfff0) == 0x03d0 /*CHIPSET_C512*/) {
		*burst = 128;
		*lwm = 0x0480;
	} else
		nv30_update_arb(burst, lwm);
}

static int
getMNP_single(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
	      struct nouveau_pll_vals *bestpv)
{
	/* Find M, N and P for a single stage PLL
	 *
	 * Note that some bioses (NV3x) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cv = dev_priv->vbios.chip_version;
	int minvco = pll_lim->vco1.minfreq, maxvco = pll_lim->vco1.maxfreq;
	int minM = pll_lim->vco1.min_m, maxM = pll_lim->vco1.max_m;
	int minN = pll_lim->vco1.min_n, maxN = pll_lim->vco1.max_n;
	int minU = pll_lim->vco1.min_inputfreq;
	int maxU = pll_lim->vco1.max_inputfreq;
	int minP = pll_lim->max_p ? pll_lim->min_p : 0;
	int maxP = pll_lim->max_p ? pll_lim->max_p : pll_lim->max_usable_log2p;
	int crystal = pll_lim->refclk;
	int M, N, thisP, P;
	int clkP, calcclk;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	/* this division verified for nv20, nv18, nv28 (Haiku), and nv34 */
	/* possibly correlated with introduction of 27MHz crystal */
	if (dev_priv->card_type < NV_50) {
		if (cv < 0x17 || cv == 0x1a || cv == 0x20) {
			if (clk > 250000)
				maxM = 6;
			if (clk > 340000)
				maxM = 2;
		} else if (cv < 0x40) {
			if (clk > 150000)
				maxM = 6;
			if (clk > 200000)
				maxM = 4;
			if (clk > 340000)
				maxM = 2;
		}
	}

	P = pll_lim->max_p ? maxP : (1 << maxP);
	if ((clk * P) < minvco) {
		minvco = clk * maxP;
		maxvco = minvco * 2;
	}

	if (clk + clk/200 > maxvco)	/* +0.5% */
		maxvco = clk + clk/200;

	/* NV34 goes maxlog2P->0, NV20 goes 0->maxlog2P */
	for (thisP = minP; thisP <= maxP; thisP++) {
		P = pll_lim->max_p ? thisP : (1 << thisP);
		clkP = clk * P;

		if (clkP < minvco)
			continue;
		if (clkP > maxvco)
			return bestclk;

		for (M = minM; M <= maxM; M++) {
			if (crystal/M < minU)
				return bestclk;
			if (crystal/M > maxU)
				continue;

			/* add crystal/2 to round better */
			N = (clkP * M + crystal/2) / crystal;

			if (N < minN)
				continue;
			if (N > maxN)
				break;

			/* more rounding additions */
			calcclk = ((N * crystal + P/2) / P + M/2) / M;
			delta = abs(calcclk - clk);
			/* we do an exhaustive search rather than terminating
			 * on an optimality condition...
			 */
			if (delta < bestdelta) {
				bestdelta = delta;
				bestclk = calcclk;
				bestpv->N1 = N;
				bestpv->M1 = M;
				bestpv->log2P = thisP;
				if (delta == 0)	/* except this one */
					return bestclk;
			}
		}
	}

	return bestclk;
}

static int
getMNP_double(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
	      struct nouveau_pll_vals *bestpv)
{
	/* Find M, N and P for a two stage PLL
	 *
	 * Note that some bioses (NV30+) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chip_version = dev_priv->vbios.chip_version;
	int minvco1 = pll_lim->vco1.minfreq, maxvco1 = pll_lim->vco1.maxfreq;
	int minvco2 = pll_lim->vco2.minfreq, maxvco2 = pll_lim->vco2.maxfreq;
	int minU1 = pll_lim->vco1.min_inputfreq, minU2 = pll_lim->vco2.min_inputfreq;
	int maxU1 = pll_lim->vco1.max_inputfreq, maxU2 = pll_lim->vco2.max_inputfreq;
	int minM1 = pll_lim->vco1.min_m, maxM1 = pll_lim->vco1.max_m;
	int minN1 = pll_lim->vco1.min_n, maxN1 = pll_lim->vco1.max_n;
	int minM2 = pll_lim->vco2.min_m, maxM2 = pll_lim->vco2.max_m;
	int minN2 = pll_lim->vco2.min_n, maxN2 = pll_lim->vco2.max_n;
	int maxlog2P = pll_lim->max_usable_log2p;
	int crystal = pll_lim->refclk;
	bool fixedgain2 = (minM2 == maxM2 && minN2 == maxN2);
	int M1, N1, M2, N2, log2P;
	int clkP, calcclk1, calcclk2, calcclkout;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	int vco2 = (maxvco2 - maxvco2/200) / 2;
	for (log2P = 0; clk && log2P < maxlog2P && clk <= (vco2 >> log2P); log2P++)
		;
	clkP = clk << log2P;

	if (maxvco2 < clk + clk/200)	/* +0.5% */
		maxvco2 = clk + clk/200;

	for (M1 = minM1; M1 <= maxM1; M1++) {
		if (crystal/M1 < minU1)
			return bestclk;
		if (crystal/M1 > maxU1)
			continue;

		for (N1 = minN1; N1 <= maxN1; N1++) {
			calcclk1 = crystal * N1 / M1;
			if (calcclk1 < minvco1)
				continue;
			if (calcclk1 > maxvco1)
				break;

			for (M2 = minM2; M2 <= maxM2; M2++) {
				if (calcclk1/M2 < minU2)
					break;
				if (calcclk1/M2 > maxU2)
					continue;

				/* add calcclk1/2 to round better */
				N2 = (clkP * M2 + calcclk1/2) / calcclk1;
				if (N2 < minN2)
					continue;
				if (N2 > maxN2)
					break;

				if (!fixedgain2) {
					if (chip_version < 0x60)
						if (N2/M2 < 4 || N2/M2 > 10)
							continue;

					calcclk2 = calcclk1 * N2 / M2;
					if (calcclk2 < minvco2)
						break;
					if (calcclk2 > maxvco2)
						continue;
				} else
					calcclk2 = calcclk1;

				calcclkout = calcclk2 >> log2P;
				delta = abs(calcclkout - clk);
				/* we do an exhaustive search rather than terminating
				 * on an optimality condition...
				 */
				if (delta < bestdelta) {
					bestdelta = delta;
					bestclk = calcclkout;
					bestpv->N1 = N1;
					bestpv->M1 = M1;
					bestpv->N2 = N2;
					bestpv->M2 = M2;
					bestpv->log2P = log2P;
					if (delta == 0)	/* except this one */
						return bestclk;
				}
			}
		}
	}

	return bestclk;
}

int
nouveau_calc_pll_mnp(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
		     struct nouveau_pll_vals *pv)
{
	int outclk;

	if (!pll_lim->vco2.maxfreq)
		outclk = getMNP_single(dev, pll_lim, clk, pv);
	else
		outclk = getMNP_double(dev, pll_lim, clk, pv);

	if (!outclk)
		NV_ERROR(dev, "Could not find a compatible set of PLL values\n");

	return outclk;
}
