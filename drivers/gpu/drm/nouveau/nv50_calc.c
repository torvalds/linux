/*
 * Copyright 2010 Red Hat Inc.
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
#include "nouveau_hw.h"

int
nv50_calc_pll(struct drm_device *dev, struct pll_lims *pll, int clk,
	      int *N1, int *M1, int *N2, int *M2, int *P)
{
	struct nouveau_pll_vals pll_vals;
	int ret;

	ret = nouveau_calc_pll_mnp(dev, pll, clk, &pll_vals);
	if (ret <= 0)
		return ret;

	*N1 = pll_vals.N1;
	*M1 = pll_vals.M1;
	*N2 = pll_vals.N2;
	*M2 = pll_vals.M2;
	*P = pll_vals.log2P;
	return ret;
}

int
nva3_calc_pll(struct drm_device *dev, struct pll_lims *pll, int clk,
	      int *pN, int *pfN, int *pM, int *P)
{
	u32 best_err = ~0, err;
	int M, lM, hM, N, fN;

	*P = pll->vco1.maxfreq / clk;
	if (*P > pll->max_p)
		*P = pll->max_p;
	if (*P < pll->min_p)
		*P = pll->min_p;

	lM = (pll->refclk + pll->vco1.max_inputfreq) / pll->vco1.max_inputfreq;
	lM = max(lM, (int)pll->vco1.min_m);
	hM = (pll->refclk + pll->vco1.min_inputfreq) / pll->vco1.min_inputfreq;
	hM = min(hM, (int)pll->vco1.max_m);

	for (M = lM; M <= hM; M++) {
		u32 tmp = clk * *P * M;
		N  = tmp / pll->refclk;
		fN = tmp % pll->refclk;
		if (!pfN && fN >= pll->refclk / 2)
			N++;

		if (N < pll->vco1.min_n)
			continue;
		if (N > pll->vco1.max_n)
			break;

		err = abs(clk - (pll->refclk * N / M / *P));
		if (err < best_err) {
			best_err = err;
			*pN = N;
			*pM = M;
		}

		if (pfN) {
			*pfN = (((fN << 13) / pll->refclk) - 4096) & 0xffff;
			return clk;
		}
	}

	if (unlikely(best_err == ~0)) {
		NV_ERROR(dev, "unable to find matching pll values\n");
		return -EINVAL;
	}

	return pll->refclk * *pN / *pM / *P;
}
