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
#include "pll.h"

#include <subdev/bios.h>
#include <subdev/bios/pll.h>

static int
getMNP_single(struct nvkm_subdev *subdev, struct nvbios_pll *info, int clk,
	      int *pN, int *pM, int *pP)
{
	/* Find M, N and P for a single stage PLL
	 *
	 * Note that some bioses (NV3x) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	struct nvkm_bios *bios = subdev->device->bios;
	int minvco = info->vco1.min_freq, maxvco = info->vco1.max_freq;
	int minM = info->vco1.min_m, maxM = info->vco1.max_m;
	int minN = info->vco1.min_n, maxN = info->vco1.max_n;
	int minU = info->vco1.min_inputfreq;
	int maxU = info->vco1.max_inputfreq;
	int minP = info->min_p;
	int maxP = info->max_p_usable;
	int crystal = info->refclk;
	int M, N, thisP, P;
	int clkP, calcclk;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	/* this division verified for nv20, nv18, nv28 (Haiku), and nv34 */
	/* possibly correlated with introduction of 27MHz crystal */
	if (bios->version.major < 0x60) {
		int cv = bios->version.chip;
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

	P = 1 << maxP;
	if ((clk * P) < minvco) {
		minvco = clk * maxP;
		maxvco = minvco * 2;
	}

	if (clk + clk/200 > maxvco)	/* +0.5% */
		maxvco = clk + clk/200;

	/* NV34 goes maxlog2P->0, NV20 goes 0->maxlog2P */
	for (thisP = minP; thisP <= maxP; thisP++) {
		P = 1 << thisP;
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
				*pN = N;
				*pM = M;
				*pP = thisP;
				if (delta == 0)	/* except this one */
					return bestclk;
			}
		}
	}

	return bestclk;
}

static int
getMNP_double(struct nvkm_subdev *subdev, struct nvbios_pll *info, int clk,
	      int *pN1, int *pM1, int *pN2, int *pM2, int *pP)
{
	/* Find M, N and P for a two stage PLL
	 *
	 * Note that some bioses (NV30+) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	int chip_version = subdev->device->bios->version.chip;
	int minvco1 = info->vco1.min_freq, maxvco1 = info->vco1.max_freq;
	int minvco2 = info->vco2.min_freq, maxvco2 = info->vco2.max_freq;
	int minU1 = info->vco1.min_inputfreq, minU2 = info->vco2.min_inputfreq;
	int maxU1 = info->vco1.max_inputfreq, maxU2 = info->vco2.max_inputfreq;
	int minM1 = info->vco1.min_m, maxM1 = info->vco1.max_m;
	int minN1 = info->vco1.min_n, maxN1 = info->vco1.max_n;
	int minM2 = info->vco2.min_m, maxM2 = info->vco2.max_m;
	int minN2 = info->vco2.min_n, maxN2 = info->vco2.max_n;
	int maxlog2P = info->max_p_usable;
	int crystal = info->refclk;
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
					*pN1 = N1;
					*pM1 = M1;
					*pN2 = N2;
					*pM2 = M2;
					*pP = log2P;
					if (delta == 0)	/* except this one */
						return bestclk;
				}
			}
		}
	}

	return bestclk;
}

int
nv04_pll_calc(struct nvkm_subdev *subdev, struct nvbios_pll *info, u32 freq,
	      int *N1, int *M1, int *N2, int *M2, int *P)
{
	int ret;

	if (!info->vco2.max_freq || !N2) {
		ret = getMNP_single(subdev, info, freq, N1, M1, P);
		if (N2) {
			*N2 = 1;
			*M2 = 1;
		}
	} else {
		ret = getMNP_double(subdev, info, freq, N1, M1, N2, M2, P);
	}

	if (!ret)
		nvkm_error(subdev, "unable to compute acceptable pll values\n");
	return ret;
}
