/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Christian König.
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
 * Authors: Christian König
 */
#include <linux/hdmi.h>
#include <linux/gcd.h>
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

static const struct amdgpu_afmt_acr amdgpu_afmt_predefined_acr[] = {
    /*	     32kHz	  44.1kHz	48kHz    */
    /* Clock      N     CTS      N     CTS      N     CTS */
    {  25175,  4096,  25175, 28224, 125875,  6144,  25175 }, /*  25,20/1.001 MHz */
    {  25200,  4096,  25200,  6272,  28000,  6144,  25200 }, /*  25.20       MHz */
    {  27000,  4096,  27000,  6272,  30000,  6144,  27000 }, /*  27.00       MHz */
    {  27027,  4096,  27027,  6272,  30030,  6144,  27027 }, /*  27.00*1.001 MHz */
    {  54000,  4096,  54000,  6272,  60000,  6144,  54000 }, /*  54.00       MHz */
    {  54054,  4096,  54054,  6272,  60060,  6144,  54054 }, /*  54.00*1.001 MHz */
    {  74176,  4096,  74176,  5733,  75335,  6144,  74176 }, /*  74.25/1.001 MHz */
    {  74250,  4096,  74250,  6272,  82500,  6144,  74250 }, /*  74.25       MHz */
    { 148352,  4096, 148352,  5733, 150670,  6144, 148352 }, /* 148.50/1.001 MHz */
    { 148500,  4096, 148500,  6272, 165000,  6144, 148500 }, /* 148.50       MHz */
};


/*
 * calculate CTS and N values if they are not found in the table
 */
static void amdgpu_afmt_calc_cts(uint32_t clock, int *CTS, int *N, int freq)
{
	int n, cts;
	unsigned long div, mul;

	/* Safe, but overly large values */
	n = 128 * freq;
	cts = clock * 1000;

	/* Smallest valid fraction */
	div = gcd(n, cts);

	n /= div;
	cts /= div;

	/*
	 * The optimal N is 128*freq/1000. Calculate the closest larger
	 * value that doesn't truncate any bits.
	 */
	mul = ((128*freq/1000) + (n-1))/n;

	n *= mul;
	cts *= mul;

	/* Check that we are in spec (not always possible) */
	if (n < (128*freq/1500))
		pr_warn("Calculated ACR N value is too small. You may experience audio problems.\n");
	if (n > (128*freq/300))
		pr_warn("Calculated ACR N value is too large. You may experience audio problems.\n");

	*N = n;
	*CTS = cts;

	DRM_DEBUG("Calculated ACR timing N=%d CTS=%d for frequency %d\n",
		  *N, *CTS, freq);
}

struct amdgpu_afmt_acr amdgpu_afmt_acr(uint32_t clock)
{
	struct amdgpu_afmt_acr res;
	u8 i;

	/* Precalculated values for common clocks */
	for (i = 0; i < ARRAY_SIZE(amdgpu_afmt_predefined_acr); i++) {
		if (amdgpu_afmt_predefined_acr[i].clock == clock)
			return amdgpu_afmt_predefined_acr[i];
	}

	/* And odd clocks get manually calculated */
	amdgpu_afmt_calc_cts(clock, &res.cts_32khz, &res.n_32khz, 32000);
	amdgpu_afmt_calc_cts(clock, &res.cts_44_1khz, &res.n_44_1khz, 44100);
	amdgpu_afmt_calc_cts(clock, &res.cts_48khz, &res.n_48khz, 48000);

	return res;
}
