 /***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL Licensing Note - According to Mark Vojkovich, author of the Xorg/
 * XFree86 'nv' driver, this source code is provided under MIT-style licensing
 * where the source code is provided "as is" without warranty of any kind.
 * The only usage restriction is for the copyright notices to be retained
 * whenever code is used.
 *
 * Antonino Daplas <adaplas@pol.net> 2005-03-11
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_hw.c,v 1.4 2003/11/03 05:11:25 tsi Exp $ */

#include <linux/pci.h>
#include "nv_type.h"
#include "nv_local.h"
#include "nv_proto.h"

void NVLockUnlock(struct nvidia_par *par, int Lock)
{
	u8 cr11;

	VGA_WR08(par->PCIO, 0x3D4, 0x1F);
	VGA_WR08(par->PCIO, 0x3D5, Lock ? 0x99 : 0x57);

	VGA_WR08(par->PCIO, 0x3D4, 0x11);
	cr11 = VGA_RD08(par->PCIO, 0x3D5);
	if (Lock)
		cr11 |= 0x80;
	else
		cr11 &= ~0x80;
	VGA_WR08(par->PCIO, 0x3D5, cr11);
}

int NVShowHideCursor(struct nvidia_par *par, int ShowHide)
{
	int cur = par->CurrentState->cursor1;

	par->CurrentState->cursor1 = (par->CurrentState->cursor1 & 0xFE) |
	    (ShowHide & 0x01);
	VGA_WR08(par->PCIO, 0x3D4, 0x31);
	VGA_WR08(par->PCIO, 0x3D5, par->CurrentState->cursor1);

	if (par->Architecture == NV_ARCH_40)
		NV_WR32(par->PRAMDAC, 0x0300, NV_RD32(par->PRAMDAC, 0x0300));

	return (cur & 0x01);
}

/****************************************************************************\
*                                                                            *
* The video arbitration routines calculate some "magic" numbers.  Fixes      *
* the snow seen when accessing the framebuffer without it.                   *
* It just works (I hope).                                                    *
*                                                                            *
\****************************************************************************/

typedef struct {
	int graphics_lwm;
	int video_lwm;
	int graphics_burst_size;
	int video_burst_size;
	int valid;
} nv4_fifo_info;

typedef struct {
	int pclk_khz;
	int mclk_khz;
	int nvclk_khz;
	char mem_page_miss;
	char mem_latency;
	int memory_width;
	char enable_video;
	char gr_during_vid;
	char pix_bpp;
	char mem_aligned;
	char enable_mp;
} nv4_sim_state;

typedef struct {
	int graphics_lwm;
	int video_lwm;
	int graphics_burst_size;
	int video_burst_size;
	int valid;
} nv10_fifo_info;

typedef struct {
	int pclk_khz;
	int mclk_khz;
	int nvclk_khz;
	char mem_page_miss;
	char mem_latency;
	int memory_type;
	int memory_width;
	char enable_video;
	char gr_during_vid;
	char pix_bpp;
	char mem_aligned;
	char enable_mp;
} nv10_sim_state;

static void nvGetClocks(struct nvidia_par *par, unsigned int *MClk,
			unsigned int *NVClk)
{
	unsigned int pll, N, M, MB, NB, P;

	if (par->Architecture >= NV_ARCH_40) {
		pll = NV_RD32(par->PMC, 0x4020);
		P = (pll >> 16) & 0x07;
		pll = NV_RD32(par->PMC, 0x4024);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		if (((par->Chipset & 0xfff0) == 0x0290) ||
				((par->Chipset & 0xfff0) == 0x0390)) {
			MB = 1;
			NB = 1;
		} else {
			MB = (pll >> 16) & 0xFF;
			NB = (pll >> 24) & 0xFF;
		}
		*MClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;

		pll = NV_RD32(par->PMC, 0x4000);
		P = (pll >> 16) & 0x03;
		pll = NV_RD32(par->PMC, 0x4004);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		MB = (pll >> 16) & 0xFF;
		NB = (pll >> 24) & 0xFF;

		*NVClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;
	} else if (par->twoStagePLL) {
		pll = NV_RD32(par->PRAMDAC0, 0x0504);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x0F;
		pll = NV_RD32(par->PRAMDAC0, 0x0574);
		if (pll & 0x80000000) {
			MB = pll & 0xFF;
			NB = (pll >> 8) & 0xFF;
		} else {
			MB = 1;
			NB = 1;
		}
		*MClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;

		pll = NV_RD32(par->PRAMDAC0, 0x0500);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x0F;
		pll = NV_RD32(par->PRAMDAC0, 0x0570);
		if (pll & 0x80000000) {
			MB = pll & 0xFF;
			NB = (pll >> 8) & 0xFF;
		} else {
			MB = 1;
			NB = 1;
		}
		*NVClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;
	} else
	    if (((par->Chipset & 0x0ff0) == 0x0300) ||
		((par->Chipset & 0x0ff0) == 0x0330)) {
		pll = NV_RD32(par->PRAMDAC0, 0x0504);
		M = pll & 0x0F;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x07;
		if (pll & 0x00000080) {
			MB = (pll >> 4) & 0x07;
			NB = (pll >> 19) & 0x1f;
		} else {
			MB = 1;
			NB = 1;
		}
		*MClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;

		pll = NV_RD32(par->PRAMDAC0, 0x0500);
		M = pll & 0x0F;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x07;
		if (pll & 0x00000080) {
			MB = (pll >> 4) & 0x07;
			NB = (pll >> 19) & 0x1f;
		} else {
			MB = 1;
			NB = 1;
		}
		*NVClk = ((N * NB * par->CrystalFreqKHz) / (M * MB)) >> P;
	} else {
		pll = NV_RD32(par->PRAMDAC0, 0x0504);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x0F;
		*MClk = (N * par->CrystalFreqKHz / M) >> P;

		pll = NV_RD32(par->PRAMDAC0, 0x0500);
		M = pll & 0xFF;
		N = (pll >> 8) & 0xFF;
		P = (pll >> 16) & 0x0F;
		*NVClk = (N * par->CrystalFreqKHz / M) >> P;
	}
}

static void nv4CalcArbitration(nv4_fifo_info * fifo, nv4_sim_state * arb)
{
	int data, pagemiss, cas, width, video_enable, bpp;
	int nvclks, mclks, pclks, vpagemiss, crtpagemiss, vbs;
	int found, mclk_extra, mclk_loop, cbs, m1, p1;
	int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
	int us_m, us_n, us_p, video_drain_rate, crtc_drain_rate;
	int vpm_us, us_video, vlwm, video_fill_us, cpm_us, us_crt, clwm;

	fifo->valid = 1;
	pclk_freq = arb->pclk_khz;
	mclk_freq = arb->mclk_khz;
	nvclk_freq = arb->nvclk_khz;
	pagemiss = arb->mem_page_miss;
	cas = arb->mem_latency;
	width = arb->memory_width >> 6;
	video_enable = arb->enable_video;
	bpp = arb->pix_bpp;
	mp_enable = arb->enable_mp;
	clwm = 0;
	vlwm = 0;
	cbs = 128;
	pclks = 2;
	nvclks = 2;
	nvclks += 2;
	nvclks += 1;
	mclks = 5;
	mclks += 3;
	mclks += 1;
	mclks += cas;
	mclks += 1;
	mclks += 1;
	mclks += 1;
	mclks += 1;
	mclk_extra = 3;
	nvclks += 2;
	nvclks += 1;
	nvclks += 1;
	nvclks += 1;
	if (mp_enable)
		mclks += 4;
	nvclks += 0;
	pclks += 0;
	found = 0;
	vbs = 0;
	while (found != 1) {
		fifo->valid = 1;
		found = 1;
		mclk_loop = mclks + mclk_extra;
		us_m = mclk_loop * 1000 * 1000 / mclk_freq;
		us_n = nvclks * 1000 * 1000 / nvclk_freq;
		us_p = nvclks * 1000 * 1000 / pclk_freq;
		if (video_enable) {
			video_drain_rate = pclk_freq * 2;
			crtc_drain_rate = pclk_freq * bpp / 8;
			vpagemiss = 2;
			vpagemiss += 1;
			crtpagemiss = 2;
			vpm_us =
			    (vpagemiss * pagemiss) * 1000 * 1000 / mclk_freq;
			if (nvclk_freq * 2 > mclk_freq * width)
				video_fill_us =
				    cbs * 1000 * 1000 / 16 / nvclk_freq;
			else
				video_fill_us =
				    cbs * 1000 * 1000 / (8 * width) /
				    mclk_freq;
			us_video = vpm_us + us_m + us_n + us_p + video_fill_us;
			vlwm = us_video * video_drain_rate / (1000 * 1000);
			vlwm++;
			vbs = 128;
			if (vlwm > 128)
				vbs = 64;
			if (vlwm > (256 - 64))
				vbs = 32;
			if (nvclk_freq * 2 > mclk_freq * width)
				video_fill_us =
				    vbs * 1000 * 1000 / 16 / nvclk_freq;
			else
				video_fill_us =
				    vbs * 1000 * 1000 / (8 * width) /
				    mclk_freq;
			cpm_us =
			    crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt =
			    us_video + video_fill_us + cpm_us + us_m + us_n +
			    us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;
		} else {
			crtc_drain_rate = pclk_freq * bpp / 8;
			crtpagemiss = 2;
			crtpagemiss += 1;
			cpm_us =
			    crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = cpm_us + us_m + us_n + us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;
		}
		m1 = clwm + cbs - 512;
		p1 = m1 * pclk_freq / mclk_freq;
		p1 = p1 * bpp / 8;
		if ((p1 < m1) && (m1 > 0)) {
			fifo->valid = 0;
			found = 0;
			if (mclk_extra == 0)
				found = 1;
			mclk_extra--;
		} else if (video_enable) {
			if ((clwm > 511) || (vlwm > 255)) {
				fifo->valid = 0;
				found = 0;
				if (mclk_extra == 0)
					found = 1;
				mclk_extra--;
			}
		} else {
			if (clwm > 519) {
				fifo->valid = 0;
				found = 0;
				if (mclk_extra == 0)
					found = 1;
				mclk_extra--;
			}
		}
		if (clwm < 384)
			clwm = 384;
		if (vlwm < 128)
			vlwm = 128;
		data = (int)(clwm);
		fifo->graphics_lwm = data;
		fifo->graphics_burst_size = 128;
		data = (int)((vlwm + 15));
		fifo->video_lwm = data;
		fifo->video_burst_size = vbs;
	}
}

static void nv4UpdateArbitrationSettings(unsigned VClk,
					 unsigned pixelDepth,
					 unsigned *burst,
					 unsigned *lwm, struct nvidia_par *par)
{
	nv4_fifo_info fifo_data;
	nv4_sim_state sim_data;
	unsigned int MClk, NVClk, cfg1;

	nvGetClocks(par, &MClk, &NVClk);

	cfg1 = NV_RD32(par->PFB, 0x00000204);
	sim_data.pix_bpp = (char)pixelDepth;
	sim_data.enable_video = 0;
	sim_data.enable_mp = 0;
	sim_data.memory_width = (NV_RD32(par->PEXTDEV, 0x0000) & 0x10) ?
	    128 : 64;
	sim_data.mem_latency = (char)cfg1 & 0x0F;
	sim_data.mem_aligned = 1;
	sim_data.mem_page_miss =
	    (char)(((cfg1 >> 4) & 0x0F) + ((cfg1 >> 31) & 0x01));
	sim_data.gr_during_vid = 0;
	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	nv4CalcArbitration(&fifo_data, &sim_data);
	if (fifo_data.valid) {
		int b = fifo_data.graphics_burst_size >> 4;
		*burst = 0;
		while (b >>= 1)
			(*burst)++;
		*lwm = fifo_data.graphics_lwm >> 3;
	}
}

static void nv10CalcArbitration(nv10_fifo_info * fifo, nv10_sim_state * arb)
{
	int data, pagemiss, width, video_enable, bpp;
	int nvclks, mclks, pclks, vpagemiss, crtpagemiss;
	int nvclk_fill;
	int found, mclk_extra, mclk_loop, cbs, m1;
	int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
	int us_m, us_m_min, us_n, us_p, crtc_drain_rate;
	int vus_m;
	int vpm_us, us_video, cpm_us, us_crt, clwm;
	int clwm_rnd_down;
	int m2us, us_pipe_min, p1clk, p2;
	int min_mclk_extra;
	int us_min_mclk_extra;

	fifo->valid = 1;
	pclk_freq = arb->pclk_khz;	/* freq in KHz */
	mclk_freq = arb->mclk_khz;
	nvclk_freq = arb->nvclk_khz;
	pagemiss = arb->mem_page_miss;
	width = arb->memory_width / 64;
	video_enable = arb->enable_video;
	bpp = arb->pix_bpp;
	mp_enable = arb->enable_mp;
	clwm = 0;

	cbs = 512;

	pclks = 4;	/* lwm detect. */

	nvclks = 3;	/* lwm -> sync. */
	nvclks += 2;	/* fbi bus cycles (1 req + 1 busy) */
	/* 2 edge sync.  may be very close to edge so just put one. */
	mclks = 1;
	mclks += 1;	/* arb_hp_req */
	mclks += 5;	/* ap_hp_req   tiling pipeline */

	mclks += 2;	/* tc_req     latency fifo */
	mclks += 2;	/* fb_cas_n_  memory request to fbio block */
	mclks += 7;	/* sm_d_rdv   data returned from fbio block */

	/* fb.rd.d.Put_gc   need to accumulate 256 bits for read */
	if (arb->memory_type == 0)
		if (arb->memory_width == 64)	/* 64 bit bus */
			mclks += 4;
		else
			mclks += 2;
	else if (arb->memory_width == 64)	/* 64 bit bus */
		mclks += 2;
	else
		mclks += 1;

	if ((!video_enable) && (arb->memory_width == 128)) {
		mclk_extra = (bpp == 32) ? 31 : 42;	/* Margin of error */
		min_mclk_extra = 17;
	} else {
		mclk_extra = (bpp == 32) ? 8 : 4;	/* Margin of error */
		/* mclk_extra = 4; *//* Margin of error */
		min_mclk_extra = 18;
	}

	/* 2 edge sync.  may be very close to edge so just put one. */
	nvclks += 1;
	nvclks += 1;		/* fbi_d_rdv_n */
	nvclks += 1;		/* Fbi_d_rdata */
	nvclks += 1;		/* crtfifo load */

	if (mp_enable)
		mclks += 4;	/* Mp can get in with a burst of 8. */
	/* Extra clocks determined by heuristics */

	nvclks += 0;
	pclks += 0;
	found = 0;
	while (found != 1) {
		fifo->valid = 1;
		found = 1;
		mclk_loop = mclks + mclk_extra;
		/* Mclk latency in us */
		us_m = mclk_loop * 1000 * 1000 / mclk_freq;
		/* Minimum Mclk latency in us */
		us_m_min = mclks * 1000 * 1000 / mclk_freq;
		us_min_mclk_extra = min_mclk_extra * 1000 * 1000 / mclk_freq;
		/* nvclk latency in us */
		us_n = nvclks * 1000 * 1000 / nvclk_freq;
		/* nvclk latency in us */
		us_p = pclks * 1000 * 1000 / pclk_freq;
		us_pipe_min = us_m_min + us_n + us_p;

		/* Mclk latency in us */
		vus_m = mclk_loop * 1000 * 1000 / mclk_freq;

		if (video_enable) {
			crtc_drain_rate = pclk_freq * bpp / 8;	/* MB/s */

			vpagemiss = 1;	/* self generating page miss */
			vpagemiss += 1;	/* One higher priority before */

			crtpagemiss = 2;	/* self generating page miss */
			if (mp_enable)
				crtpagemiss += 1;	/* if MA0 conflict */

			vpm_us =
			    (vpagemiss * pagemiss) * 1000 * 1000 / mclk_freq;

			/* Video has separate read return path */
			us_video = vpm_us + vus_m;

			cpm_us =
			    crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			/* Wait for video */
			us_crt = us_video
			    + cpm_us	/* CRT Page miss */
			    + us_m + us_n + us_p	/* other latency */
			    ;

			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			/* fixed point <= float_point - 1.  Fixes that */
			clwm++;
		} else {
		    /* bpp * pclk/8 */
			crtc_drain_rate = pclk_freq * bpp / 8;

			crtpagemiss = 1;	/* self generating page miss */
			crtpagemiss += 1;	/* MA0 page miss */
			if (mp_enable)
				crtpagemiss += 1;	/* if MA0 conflict */
			cpm_us =
			    crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = cpm_us + us_m + us_n + us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			/* fixed point <= float_point - 1.  Fixes that */
			clwm++;

			/* Finally, a heuristic check when width == 64 bits */
			if (width == 1) {
				nvclk_fill = nvclk_freq * 8;
				if (crtc_drain_rate * 100 >= nvclk_fill * 102)
					/*Large number to fail */
					clwm = 0xfff;

				else if (crtc_drain_rate * 100 >=
					 nvclk_fill * 98) {
					clwm = 1024;
					cbs = 512;
				}
			}
		}

		/*
		   Overfill check:
		 */

		clwm_rnd_down = ((int)clwm / 8) * 8;
		if (clwm_rnd_down < clwm)
			clwm += 8;

		m1 = clwm + cbs - 1024;	/* Amount of overfill */
		m2us = us_pipe_min + us_min_mclk_extra;

		/* pclk cycles to drain */
		p1clk = m2us * pclk_freq / (1000 * 1000);
		p2 = p1clk * bpp / 8;	/* bytes drained. */

		if ((p2 < m1) && (m1 > 0)) {
			fifo->valid = 0;
			found = 0;
			if (min_mclk_extra == 0) {
				if (cbs <= 32) {
					/* Can't adjust anymore! */
					found = 1;
				} else {
					/* reduce the burst size */
					cbs = cbs / 2;
				}
			} else {
				min_mclk_extra--;
			}
		} else {
			if (clwm > 1023) {	/* Have some margin */
				fifo->valid = 0;
				found = 0;
				if (min_mclk_extra == 0)
					/* Can't adjust anymore! */
					found = 1;
				else
					min_mclk_extra--;
			}
		}

		if (clwm < (1024 - cbs + 8))
			clwm = 1024 - cbs + 8;
		data = (int)(clwm);
		/*  printf("CRT LWM: %f bytes, prog: 0x%x, bs: 256\n",
		    clwm, data ); */
		fifo->graphics_lwm = data;
		fifo->graphics_burst_size = cbs;

		fifo->video_lwm = 1024;
		fifo->video_burst_size = 512;
	}
}

static void nv10UpdateArbitrationSettings(unsigned VClk,
					  unsigned pixelDepth,
					  unsigned *burst,
					  unsigned *lwm,
					  struct nvidia_par *par)
{
	nv10_fifo_info fifo_data;
	nv10_sim_state sim_data;
	unsigned int MClk, NVClk, cfg1;

	nvGetClocks(par, &MClk, &NVClk);

	cfg1 = NV_RD32(par->PFB, 0x0204);
	sim_data.pix_bpp = (char)pixelDepth;
	sim_data.enable_video = 1;
	sim_data.enable_mp = 0;
	sim_data.memory_type = (NV_RD32(par->PFB, 0x0200) & 0x01) ? 1 : 0;
	sim_data.memory_width = (NV_RD32(par->PEXTDEV, 0x0000) & 0x10) ?
	    128 : 64;
	sim_data.mem_latency = (char)cfg1 & 0x0F;
	sim_data.mem_aligned = 1;
	sim_data.mem_page_miss =
	    (char)(((cfg1 >> 4) & 0x0F) + ((cfg1 >> 31) & 0x01));
	sim_data.gr_during_vid = 0;
	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	nv10CalcArbitration(&fifo_data, &sim_data);
	if (fifo_data.valid) {
		int b = fifo_data.graphics_burst_size >> 4;
		*burst = 0;
		while (b >>= 1)
			(*burst)++;
		*lwm = fifo_data.graphics_lwm >> 3;
	}
}

static void nv30UpdateArbitrationSettings (
    struct nvidia_par *par,
    unsigned int      *burst,
    unsigned int      *lwm
)
{
    unsigned int MClk, NVClk;
    unsigned int fifo_size, burst_size, graphics_lwm;

    fifo_size = 2048;
    burst_size = 512;
    graphics_lwm = fifo_size - burst_size;

    nvGetClocks(par, &MClk, &NVClk);

    *burst = 0;
    burst_size >>= 5;
    while(burst_size >>= 1) (*burst)++;
    *lwm = graphics_lwm >> 3;
}

static void nForceUpdateArbitrationSettings(unsigned VClk,
					    unsigned pixelDepth,
					    unsigned *burst,
					    unsigned *lwm,
					    struct nvidia_par *par)
{
	nv10_fifo_info fifo_data;
	nv10_sim_state sim_data;
	unsigned int M, N, P, pll, MClk, NVClk, memctrl;
	struct pci_dev *dev;

	if ((par->Chipset & 0x0FF0) == 0x01A0) {
		unsigned int uMClkPostDiv;
		dev = pci_get_bus_and_slot(0, 3);
		pci_read_config_dword(dev, 0x6C, &uMClkPostDiv);
		uMClkPostDiv = (uMClkPostDiv >> 8) & 0xf;

		if (!uMClkPostDiv)
			uMClkPostDiv = 4;
		MClk = 400000 / uMClkPostDiv;
	} else {
		dev = pci_get_bus_and_slot(0, 5);
		pci_read_config_dword(dev, 0x4c, &MClk);
		MClk /= 1000;
	}
	pci_dev_put(dev);
	pll = NV_RD32(par->PRAMDAC0, 0x0500);
	M = (pll >> 0) & 0xFF;
	N = (pll >> 8) & 0xFF;
	P = (pll >> 16) & 0x0F;
	NVClk = (N * par->CrystalFreqKHz / M) >> P;
	sim_data.pix_bpp = (char)pixelDepth;
	sim_data.enable_video = 0;
	sim_data.enable_mp = 0;
	dev = pci_get_bus_and_slot(0, 1);
	pci_read_config_dword(dev, 0x7C, &sim_data.memory_type);
	pci_dev_put(dev);
	sim_data.memory_type = (sim_data.memory_type >> 12) & 1;
	sim_data.memory_width = 64;

	dev = pci_get_bus_and_slot(0, 3);
	pci_read_config_dword(dev, 0, &memctrl);
	pci_dev_put(dev);
	memctrl >>= 16;

	if ((memctrl == 0x1A9) || (memctrl == 0x1AB) || (memctrl == 0x1ED)) {
		int dimm[3];

		dev = pci_get_bus_and_slot(0, 2);
		pci_read_config_dword(dev, 0x40, &dimm[0]);
		dimm[0] = (dimm[0] >> 8) & 0x4f;
		pci_read_config_dword(dev, 0x44, &dimm[1]);
		dimm[1] = (dimm[1] >> 8) & 0x4f;
		pci_read_config_dword(dev, 0x48, &dimm[2]);
		dimm[2] = (dimm[2] >> 8) & 0x4f;

		if ((dimm[0] + dimm[1]) != dimm[2]) {
			printk("nvidiafb: your nForce DIMMs are not arranged "
			       "in optimal banks!\n");
		}
		pci_dev_put(dev);
	}

	sim_data.mem_latency = 3;
	sim_data.mem_aligned = 1;
	sim_data.mem_page_miss = 10;
	sim_data.gr_during_vid = 0;
	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	nv10CalcArbitration(&fifo_data, &sim_data);
	if (fifo_data.valid) {
		int b = fifo_data.graphics_burst_size >> 4;
		*burst = 0;
		while (b >>= 1)
			(*burst)++;
		*lwm = fifo_data.graphics_lwm >> 3;
	}
}

/****************************************************************************\
*                                                                            *
*                          RIVA Mode State Routines                          *
*                                                                            *
\****************************************************************************/

/*
 * Calculate the Video Clock parameters for the PLL.
 */
static void CalcVClock(int clockIn,
		       int *clockOut, u32 * pllOut, struct nvidia_par *par)
{
	unsigned lowM, highM;
	unsigned DeltaNew, DeltaOld;
	unsigned VClk, Freq;
	unsigned M, N, P;

	DeltaOld = 0xFFFFFFFF;

	VClk = (unsigned)clockIn;

	if (par->CrystalFreqKHz == 13500) {
		lowM = 7;
		highM = 13;
	} else {
		lowM = 8;
		highM = 14;
	}

	for (P = 0; P <= 4; P++) {
		Freq = VClk << P;
		if ((Freq >= 128000) && (Freq <= 350000)) {
			for (M = lowM; M <= highM; M++) {
				N = ((VClk << P) * M) / par->CrystalFreqKHz;
				if (N <= 255) {
					Freq =
					    ((par->CrystalFreqKHz * N) /
					     M) >> P;
					if (Freq > VClk)
						DeltaNew = Freq - VClk;
					else
						DeltaNew = VClk - Freq;
					if (DeltaNew < DeltaOld) {
						*pllOut =
						    (P << 16) | (N << 8) | M;
						*clockOut = Freq;
						DeltaOld = DeltaNew;
					}
				}
			}
		}
	}
}

static void CalcVClock2Stage(int clockIn,
			     int *clockOut,
			     u32 * pllOut,
			     u32 * pllBOut, struct nvidia_par *par)
{
	unsigned DeltaNew, DeltaOld;
	unsigned VClk, Freq;
	unsigned M, N, P;

	DeltaOld = 0xFFFFFFFF;

	*pllBOut = 0x80000401;	/* fixed at x4 for now */

	VClk = (unsigned)clockIn;

	for (P = 0; P <= 6; P++) {
		Freq = VClk << P;
		if ((Freq >= 400000) && (Freq <= 1000000)) {
			for (M = 1; M <= 13; M++) {
				N = ((VClk << P) * M) /
				    (par->CrystalFreqKHz << 2);
				if ((N >= 5) && (N <= 255)) {
					Freq =
					    (((par->CrystalFreqKHz << 2) * N) /
					     M) >> P;
					if (Freq > VClk)
						DeltaNew = Freq - VClk;
					else
						DeltaNew = VClk - Freq;
					if (DeltaNew < DeltaOld) {
						*pllOut =
						    (P << 16) | (N << 8) | M;
						*clockOut = Freq;
						DeltaOld = DeltaNew;
					}
				}
			}
		}
	}
}

/*
 * Calculate extended mode parameters (SVGA) and save in a
 * mode state structure.
 */
void NVCalcStateExt(struct nvidia_par *par,
		    RIVA_HW_STATE * state,
		    int bpp,
		    int width,
		    int hDisplaySize, int height, int dotClock, int flags)
{
	int pixelDepth, VClk = 0;
	/*
	 * Save mode parameters.
	 */
	state->bpp = bpp;	/* this is not bitsPerPixel, it's 8,15,16,32 */
	state->width = width;
	state->height = height;
	/*
	 * Extended RIVA registers.
	 */
	pixelDepth = (bpp + 1) / 8;
	if (par->twoStagePLL)
		CalcVClock2Stage(dotClock, &VClk, &state->pll, &state->pllB,
				 par);
	else
		CalcVClock(dotClock, &VClk, &state->pll, par);

	switch (par->Architecture) {
	case NV_ARCH_04:
		nv4UpdateArbitrationSettings(VClk,
					     pixelDepth * 8,
					     &(state->arbitration0),
					     &(state->arbitration1), par);
		state->cursor0 = 0x00;
		state->cursor1 = 0xbC;
		if (flags & FB_VMODE_DOUBLE)
			state->cursor1 |= 2;
		state->cursor2 = 0x00000000;
		state->pllsel = 0x10000700;
		state->config = 0x00001114;
		state->general = bpp == 16 ? 0x00101100 : 0x00100100;
		state->repaint1 = hDisplaySize < 1280 ? 0x04 : 0x00;
		break;
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
	default:
		if ((par->Chipset & 0xfff0) == 0x0240) {
			state->arbitration0 = 256;
			state->arbitration1 = 0x0480;
		} else if (((par->Chipset & 0xffff) == 0x01A0) ||
		    ((par->Chipset & 0xffff) == 0x01f0)) {
			nForceUpdateArbitrationSettings(VClk,
							pixelDepth * 8,
							&(state->arbitration0),
							&(state->arbitration1),
							par);
		} else if (par->Architecture < NV_ARCH_30) {
			nv10UpdateArbitrationSettings(VClk,
						      pixelDepth * 8,
						      &(state->arbitration0),
						      &(state->arbitration1),
						      par);
		} else {
			nv30UpdateArbitrationSettings(par,
						      &(state->arbitration0),
						      &(state->arbitration1));
		}

		state->cursor0 = 0x80 | (par->CursorStart >> 17);
		state->cursor1 = (par->CursorStart >> 11) << 2;
		state->cursor2 = par->CursorStart >> 24;
		if (flags & FB_VMODE_DOUBLE)
			state->cursor1 |= 2;
		state->pllsel = 0x10000700;
		state->config = NV_RD32(par->PFB, 0x00000200);
		state->general = bpp == 16 ? 0x00101100 : 0x00100100;
		state->repaint1 = hDisplaySize < 1280 ? 0x04 : 0x00;
		break;
	}

	if (bpp != 8)		/* DirectColor */
		state->general |= 0x00000030;

	state->repaint0 = (((width / 8) * pixelDepth) & 0x700) >> 3;
	state->pixel = (pixelDepth > 2) ? 3 : pixelDepth;
}

void NVLoadStateExt(struct nvidia_par *par, RIVA_HW_STATE * state)
{
	int i;

	NV_WR32(par->PMC, 0x0140, 0x00000000);
	NV_WR32(par->PMC, 0x0200, 0xFFFF00FF);
	NV_WR32(par->PMC, 0x0200, 0xFFFFFFFF);

	NV_WR32(par->PTIMER, 0x0200 * 4, 0x00000008);
	NV_WR32(par->PTIMER, 0x0210 * 4, 0x00000003);
	NV_WR32(par->PTIMER, 0x0140 * 4, 0x00000000);
	NV_WR32(par->PTIMER, 0x0100 * 4, 0xFFFFFFFF);

	if (par->Architecture == NV_ARCH_04) {
		NV_WR32(par->PFB, 0x0200, state->config);
	} else if ((par->Architecture < NV_ARCH_40) ||
		   (par->Chipset & 0xfff0) == 0x0040) {
		for (i = 0; i < 8; i++) {
			NV_WR32(par->PFB, 0x0240 + (i * 0x10), 0);
			NV_WR32(par->PFB, 0x0244 + (i * 0x10),
				par->FbMapSize - 1);
		}
	} else {
		int regions = 12;

		if (((par->Chipset & 0xfff0) == 0x0090) ||
		    ((par->Chipset & 0xfff0) == 0x01D0) ||
		    ((par->Chipset & 0xfff0) == 0x0290))
			regions = 15;
		for(i = 0; i < regions; i++) {
			NV_WR32(par->PFB, 0x0600 + (i * 0x10), 0);
			NV_WR32(par->PFB, 0x0604 + (i * 0x10),
				par->FbMapSize - 1);
		}
	}

	if (par->Architecture >= NV_ARCH_40) {
		NV_WR32(par->PRAMIN, 0x0000 * 4, 0x80000010);
		NV_WR32(par->PRAMIN, 0x0001 * 4, 0x00101202);
		NV_WR32(par->PRAMIN, 0x0002 * 4, 0x80000011);
		NV_WR32(par->PRAMIN, 0x0003 * 4, 0x00101204);
		NV_WR32(par->PRAMIN, 0x0004 * 4, 0x80000012);
		NV_WR32(par->PRAMIN, 0x0005 * 4, 0x00101206);
		NV_WR32(par->PRAMIN, 0x0006 * 4, 0x80000013);
		NV_WR32(par->PRAMIN, 0x0007 * 4, 0x00101208);
		NV_WR32(par->PRAMIN, 0x0008 * 4, 0x80000014);
		NV_WR32(par->PRAMIN, 0x0009 * 4, 0x0010120A);
		NV_WR32(par->PRAMIN, 0x000A * 4, 0x80000015);
		NV_WR32(par->PRAMIN, 0x000B * 4, 0x0010120C);
		NV_WR32(par->PRAMIN, 0x000C * 4, 0x80000016);
		NV_WR32(par->PRAMIN, 0x000D * 4, 0x0010120E);
		NV_WR32(par->PRAMIN, 0x000E * 4, 0x80000017);
		NV_WR32(par->PRAMIN, 0x000F * 4, 0x00101210);
		NV_WR32(par->PRAMIN, 0x0800 * 4, 0x00003000);
		NV_WR32(par->PRAMIN, 0x0801 * 4, par->FbMapSize - 1);
		NV_WR32(par->PRAMIN, 0x0802 * 4, 0x00000002);
		NV_WR32(par->PRAMIN, 0x0808 * 4, 0x02080062);
		NV_WR32(par->PRAMIN, 0x0809 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080A * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x080B * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x080C * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080D * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0810 * 4, 0x02080043);
		NV_WR32(par->PRAMIN, 0x0811 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0812 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0813 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0814 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0815 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0818 * 4, 0x02080044);
		NV_WR32(par->PRAMIN, 0x0819 * 4, 0x02000000);
		NV_WR32(par->PRAMIN, 0x081A * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081B * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081C * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081D * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0820 * 4, 0x02080019);
		NV_WR32(par->PRAMIN, 0x0821 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0822 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0823 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0824 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0825 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0828 * 4, 0x020A005C);
		NV_WR32(par->PRAMIN, 0x0829 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x082A * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x082B * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x082C * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x082D * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0830 * 4, 0x0208009F);
		NV_WR32(par->PRAMIN, 0x0831 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0832 * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x0833 * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x0834 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0835 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0838 * 4, 0x0208004A);
		NV_WR32(par->PRAMIN, 0x0839 * 4, 0x02000000);
		NV_WR32(par->PRAMIN, 0x083A * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x083B * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x083C * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x083D * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0840 * 4, 0x02080077);
		NV_WR32(par->PRAMIN, 0x0841 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0842 * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x0843 * 4, 0x00001200);
		NV_WR32(par->PRAMIN, 0x0844 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0845 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x084C * 4, 0x00003002);
		NV_WR32(par->PRAMIN, 0x084D * 4, 0x00007FFF);
		NV_WR32(par->PRAMIN, 0x084E * 4,
			par->FbUsableSize | 0x00000002);

#ifdef __BIG_ENDIAN
		NV_WR32(par->PRAMIN, 0x080A * 4,
			NV_RD32(par->PRAMIN, 0x080A * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x0812 * 4,
			NV_RD32(par->PRAMIN, 0x0812 * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x081A * 4,
			NV_RD32(par->PRAMIN, 0x081A * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x0822 * 4,
			NV_RD32(par->PRAMIN, 0x0822 * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x082A * 4,
			NV_RD32(par->PRAMIN, 0x082A * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x0832 * 4,
			NV_RD32(par->PRAMIN, 0x0832 * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x083A * 4,
			NV_RD32(par->PRAMIN, 0x083A * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x0842 * 4,
			NV_RD32(par->PRAMIN, 0x0842 * 4) | 0x01000000);
		NV_WR32(par->PRAMIN, 0x0819 * 4, 0x01000000);
		NV_WR32(par->PRAMIN, 0x0839 * 4, 0x01000000);
#endif
	} else {
		NV_WR32(par->PRAMIN, 0x0000 * 4, 0x80000010);
		NV_WR32(par->PRAMIN, 0x0001 * 4, 0x80011201);
		NV_WR32(par->PRAMIN, 0x0002 * 4, 0x80000011);
		NV_WR32(par->PRAMIN, 0x0003 * 4, 0x80011202);
		NV_WR32(par->PRAMIN, 0x0004 * 4, 0x80000012);
		NV_WR32(par->PRAMIN, 0x0005 * 4, 0x80011203);
		NV_WR32(par->PRAMIN, 0x0006 * 4, 0x80000013);
		NV_WR32(par->PRAMIN, 0x0007 * 4, 0x80011204);
		NV_WR32(par->PRAMIN, 0x0008 * 4, 0x80000014);
		NV_WR32(par->PRAMIN, 0x0009 * 4, 0x80011205);
		NV_WR32(par->PRAMIN, 0x000A * 4, 0x80000015);
		NV_WR32(par->PRAMIN, 0x000B * 4, 0x80011206);
		NV_WR32(par->PRAMIN, 0x000C * 4, 0x80000016);
		NV_WR32(par->PRAMIN, 0x000D * 4, 0x80011207);
		NV_WR32(par->PRAMIN, 0x000E * 4, 0x80000017);
		NV_WR32(par->PRAMIN, 0x000F * 4, 0x80011208);
		NV_WR32(par->PRAMIN, 0x0800 * 4, 0x00003000);
		NV_WR32(par->PRAMIN, 0x0801 * 4, par->FbMapSize - 1);
		NV_WR32(par->PRAMIN, 0x0802 * 4, 0x00000002);
		NV_WR32(par->PRAMIN, 0x0803 * 4, 0x00000002);
		if (par->Architecture >= NV_ARCH_10)
			NV_WR32(par->PRAMIN, 0x0804 * 4, 0x01008062);
		else
			NV_WR32(par->PRAMIN, 0x0804 * 4, 0x01008042);
		NV_WR32(par->PRAMIN, 0x0805 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0806 * 4, 0x12001200);
		NV_WR32(par->PRAMIN, 0x0807 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0808 * 4, 0x01008043);
		NV_WR32(par->PRAMIN, 0x0809 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080A * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080B * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080C * 4, 0x01008044);
		NV_WR32(par->PRAMIN, 0x080D * 4, 0x00000002);
		NV_WR32(par->PRAMIN, 0x080E * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x080F * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0810 * 4, 0x01008019);
		NV_WR32(par->PRAMIN, 0x0811 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0812 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0813 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0814 * 4, 0x0100A05C);
		NV_WR32(par->PRAMIN, 0x0815 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0816 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0817 * 4, 0x00000000);
		if (par->WaitVSyncPossible)
			NV_WR32(par->PRAMIN, 0x0818 * 4, 0x0100809F);
		else
			NV_WR32(par->PRAMIN, 0x0818 * 4, 0x0100805F);
		NV_WR32(par->PRAMIN, 0x0819 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081A * 4, 0x12001200);
		NV_WR32(par->PRAMIN, 0x081B * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081C * 4, 0x0100804A);
		NV_WR32(par->PRAMIN, 0x081D * 4, 0x00000002);
		NV_WR32(par->PRAMIN, 0x081E * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x081F * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0820 * 4, 0x01018077);
		NV_WR32(par->PRAMIN, 0x0821 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0822 * 4, 0x12001200);
		NV_WR32(par->PRAMIN, 0x0823 * 4, 0x00000000);
		NV_WR32(par->PRAMIN, 0x0824 * 4, 0x00003002);
		NV_WR32(par->PRAMIN, 0x0825 * 4, 0x00007FFF);
		NV_WR32(par->PRAMIN, 0x0826 * 4,
			par->FbUsableSize | 0x00000002);
		NV_WR32(par->PRAMIN, 0x0827 * 4, 0x00000002);
#ifdef __BIG_ENDIAN
		NV_WR32(par->PRAMIN, 0x0804 * 4,
			NV_RD32(par->PRAMIN, 0x0804 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x0808 * 4,
			NV_RD32(par->PRAMIN, 0x0808 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x080C * 4,
			NV_RD32(par->PRAMIN, 0x080C * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x0810 * 4,
			NV_RD32(par->PRAMIN, 0x0810 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x0814 * 4,
			NV_RD32(par->PRAMIN, 0x0814 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x0818 * 4,
			NV_RD32(par->PRAMIN, 0x0818 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x081C * 4,
			NV_RD32(par->PRAMIN, 0x081C * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x0820 * 4,
			NV_RD32(par->PRAMIN, 0x0820 * 4) | 0x00080000);
		NV_WR32(par->PRAMIN, 0x080D * 4, 0x00000001);
		NV_WR32(par->PRAMIN, 0x081D * 4, 0x00000001);
#endif
	}
	if (par->Architecture < NV_ARCH_10) {
		if ((par->Chipset & 0x0fff) == 0x0020) {
			NV_WR32(par->PRAMIN, 0x0824 * 4,
				NV_RD32(par->PRAMIN, 0x0824 * 4) | 0x00020000);
			NV_WR32(par->PRAMIN, 0x0826 * 4,
				NV_RD32(par->PRAMIN,
					0x0826 * 4) + par->FbAddress);
		}
		NV_WR32(par->PGRAPH, 0x0080, 0x000001FF);
		NV_WR32(par->PGRAPH, 0x0080, 0x1230C000);
		NV_WR32(par->PGRAPH, 0x0084, 0x72111101);
		NV_WR32(par->PGRAPH, 0x0088, 0x11D5F071);
		NV_WR32(par->PGRAPH, 0x008C, 0x0004FF31);
		NV_WR32(par->PGRAPH, 0x008C, 0x4004FF31);
		NV_WR32(par->PGRAPH, 0x0140, 0x00000000);
		NV_WR32(par->PGRAPH, 0x0100, 0xFFFFFFFF);
		NV_WR32(par->PGRAPH, 0x0170, 0x10010100);
		NV_WR32(par->PGRAPH, 0x0710, 0xFFFFFFFF);
		NV_WR32(par->PGRAPH, 0x0720, 0x00000001);
		NV_WR32(par->PGRAPH, 0x0810, 0x00000000);
		NV_WR32(par->PGRAPH, 0x0608, 0xFFFFFFFF);
	} else {
		NV_WR32(par->PGRAPH, 0x0080, 0xFFFFFFFF);
		NV_WR32(par->PGRAPH, 0x0080, 0x00000000);

		NV_WR32(par->PGRAPH, 0x0140, 0x00000000);
		NV_WR32(par->PGRAPH, 0x0100, 0xFFFFFFFF);
		NV_WR32(par->PGRAPH, 0x0144, 0x10010100);
		NV_WR32(par->PGRAPH, 0x0714, 0xFFFFFFFF);
		NV_WR32(par->PGRAPH, 0x0720, 0x00000001);
		NV_WR32(par->PGRAPH, 0x0710,
			NV_RD32(par->PGRAPH, 0x0710) & 0x0007ff00);
		NV_WR32(par->PGRAPH, 0x0710,
			NV_RD32(par->PGRAPH, 0x0710) | 0x00020100);

		if (par->Architecture == NV_ARCH_10) {
			NV_WR32(par->PGRAPH, 0x0084, 0x00118700);
			NV_WR32(par->PGRAPH, 0x0088, 0x24E00810);
			NV_WR32(par->PGRAPH, 0x008C, 0x55DE0030);

			for (i = 0; i < 32; i++)
				NV_WR32(&par->PGRAPH[(0x0B00 / 4) + i], 0,
					NV_RD32(&par->PFB[(0x0240 / 4) + i],
						0));

			NV_WR32(par->PGRAPH, 0x640, 0);
			NV_WR32(par->PGRAPH, 0x644, 0);
			NV_WR32(par->PGRAPH, 0x684, par->FbMapSize - 1);
			NV_WR32(par->PGRAPH, 0x688, par->FbMapSize - 1);

			NV_WR32(par->PGRAPH, 0x0810, 0x00000000);
			NV_WR32(par->PGRAPH, 0x0608, 0xFFFFFFFF);
		} else {
			if (par->Architecture >= NV_ARCH_40) {
				u32 tmp;

				NV_WR32(par->PGRAPH, 0x0084, 0x401287c0);
				NV_WR32(par->PGRAPH, 0x008C, 0x60de8051);
				NV_WR32(par->PGRAPH, 0x0090, 0x00008000);
				NV_WR32(par->PGRAPH, 0x0610, 0x00be3c5f);

				tmp = NV_RD32(par->REGS, 0x1540) & 0xff;
				for(i = 0; tmp && !(tmp & 1); tmp >>= 1, i++);
				NV_WR32(par->PGRAPH, 0x5000, i);

				if ((par->Chipset & 0xfff0) == 0x0040) {
					NV_WR32(par->PGRAPH, 0x09b0,
						0x83280fff);
					NV_WR32(par->PGRAPH, 0x09b4,
						0x000000a0);
				} else {
					NV_WR32(par->PGRAPH, 0x0820,
						0x83280eff);
					NV_WR32(par->PGRAPH, 0x0824,
						0x000000a0);
				}

				switch (par->Chipset & 0xfff0) {
				case 0x0040:
				case 0x0210:
					NV_WR32(par->PGRAPH, 0x09b8,
						0x0078e366);
					NV_WR32(par->PGRAPH, 0x09bc,
						0x0000014c);
					NV_WR32(par->PFB, 0x033C,
						NV_RD32(par->PFB, 0x33C) &
						0xffff7fff);
					break;
				case 0x00C0:
				case 0x0120:
					NV_WR32(par->PGRAPH, 0x0828,
						0x007596ff);
					NV_WR32(par->PGRAPH, 0x082C,
						0x00000108);
					break;
				case 0x0160:
				case 0x01D0:
				case 0x0240:
					NV_WR32(par->PMC, 0x1700,
						NV_RD32(par->PFB, 0x020C));
					NV_WR32(par->PMC, 0x1704, 0);
					NV_WR32(par->PMC, 0x1708, 0);
					NV_WR32(par->PMC, 0x170C,
						NV_RD32(par->PFB, 0x020C));
					NV_WR32(par->PGRAPH, 0x0860, 0);
					NV_WR32(par->PGRAPH, 0x0864, 0);
					NV_WR32(par->PRAMDAC, 0x0608,
						NV_RD32(par->PRAMDAC,
							0x0608) | 0x00100000);
					break;
				case 0x0140:
					NV_WR32(par->PGRAPH, 0x0828,
						0x0072cb77);
					NV_WR32(par->PGRAPH, 0x082C,
						0x00000108);
					break;
				case 0x0220:
				case 0x0230:
					NV_WR32(par->PGRAPH, 0x0860, 0);
					NV_WR32(par->PGRAPH, 0x0864, 0);
					NV_WR32(par->PRAMDAC, 0x0608,
						NV_RD32(par->PRAMDAC, 0x0608) |
						0x00100000);
					break;
				case 0x0090:
				case 0x0290:
					NV_WR32(par->PRAMDAC, 0x0608,
						NV_RD32(par->PRAMDAC, 0x0608) |
						0x00100000);
					NV_WR32(par->PGRAPH, 0x0828,
						0x07830610);
					NV_WR32(par->PGRAPH, 0x082C,
						0x0000016A);
					break;
				default:
					break;
				};

				NV_WR32(par->PGRAPH, 0x0b38, 0x2ffff800);
				NV_WR32(par->PGRAPH, 0x0b3c, 0x00006000);
				NV_WR32(par->PGRAPH, 0x032C, 0x01000000);
				NV_WR32(par->PGRAPH, 0x0220, 0x00001200);
			} else if (par->Architecture == NV_ARCH_30) {
				NV_WR32(par->PGRAPH, 0x0084, 0x40108700);
				NV_WR32(par->PGRAPH, 0x0890, 0x00140000);
				NV_WR32(par->PGRAPH, 0x008C, 0xf00e0431);
				NV_WR32(par->PGRAPH, 0x0090, 0x00008000);
				NV_WR32(par->PGRAPH, 0x0610, 0xf04b1f36);
				NV_WR32(par->PGRAPH, 0x0B80, 0x1002d888);
				NV_WR32(par->PGRAPH, 0x0B88, 0x62ff007f);
			} else {
				NV_WR32(par->PGRAPH, 0x0084, 0x00118700);
				NV_WR32(par->PGRAPH, 0x008C, 0xF20E0431);
				NV_WR32(par->PGRAPH, 0x0090, 0x00000000);
				NV_WR32(par->PGRAPH, 0x009C, 0x00000040);

				if ((par->Chipset & 0x0ff0) >= 0x0250) {
					NV_WR32(par->PGRAPH, 0x0890,
						0x00080000);
					NV_WR32(par->PGRAPH, 0x0610,
						0x304B1FB6);
					NV_WR32(par->PGRAPH, 0x0B80,
						0x18B82880);
					NV_WR32(par->PGRAPH, 0x0B84,
						0x44000000);
					NV_WR32(par->PGRAPH, 0x0098,
						0x40000080);
					NV_WR32(par->PGRAPH, 0x0B88,
						0x000000ff);
				} else {
					NV_WR32(par->PGRAPH, 0x0880,
						0x00080000);
					NV_WR32(par->PGRAPH, 0x0094,
						0x00000005);
					NV_WR32(par->PGRAPH, 0x0B80,
						0x45CAA208);
					NV_WR32(par->PGRAPH, 0x0B84,
						0x24000000);
					NV_WR32(par->PGRAPH, 0x0098,
						0x00000040);
					NV_WR32(par->PGRAPH, 0x0750,
						0x00E00038);
					NV_WR32(par->PGRAPH, 0x0754,
						0x00000030);
					NV_WR32(par->PGRAPH, 0x0750,
						0x00E10038);
					NV_WR32(par->PGRAPH, 0x0754,
						0x00000030);
				}
			}

			if ((par->Architecture < NV_ARCH_40) ||
			    ((par->Chipset & 0xfff0) == 0x0040)) {
				for (i = 0; i < 32; i++) {
					NV_WR32(par->PGRAPH, 0x0900 + i*4,
						NV_RD32(par->PFB, 0x0240 +i*4));
					NV_WR32(par->PGRAPH, 0x6900 + i*4,
						NV_RD32(par->PFB, 0x0240 +i*4));
				}
			} else {
				if (((par->Chipset & 0xfff0) == 0x0090) ||
				    ((par->Chipset & 0xfff0) == 0x01D0) ||
				    ((par->Chipset & 0xfff0) == 0x0290)) {
					for (i = 0; i < 60; i++) {
						NV_WR32(par->PGRAPH,
							0x0D00 + i*4,
							NV_RD32(par->PFB,
								0x0600 + i*4));
						NV_WR32(par->PGRAPH,
							0x6900 + i*4,
							NV_RD32(par->PFB,
								0x0600 + i*4));
					}
				} else {
					for (i = 0; i < 48; i++) {
						NV_WR32(par->PGRAPH,
							0x0900 + i*4,
							NV_RD32(par->PFB,
								0x0600 + i*4));
						if(((par->Chipset & 0xfff0)
						    != 0x0160) &&
						   ((par->Chipset & 0xfff0)
						    != 0x0220) &&
						   ((par->Chipset & 0xfff0)
						    != 0x240))
							NV_WR32(par->PGRAPH,
								0x6900 + i*4,
								NV_RD32(par->PFB,
									0x0600 + i*4));
					}
				}
			}

			if (par->Architecture >= NV_ARCH_40) {
				if ((par->Chipset & 0xfff0) == 0x0040) {
					NV_WR32(par->PGRAPH, 0x09A4,
						NV_RD32(par->PFB, 0x0200));
					NV_WR32(par->PGRAPH, 0x09A8,
						NV_RD32(par->PFB, 0x0204));
					NV_WR32(par->PGRAPH, 0x69A4,
						NV_RD32(par->PFB, 0x0200));
					NV_WR32(par->PGRAPH, 0x69A8,
						NV_RD32(par->PFB, 0x0204));

					NV_WR32(par->PGRAPH, 0x0820, 0);
					NV_WR32(par->PGRAPH, 0x0824, 0);
					NV_WR32(par->PGRAPH, 0x0864,
						par->FbMapSize - 1);
					NV_WR32(par->PGRAPH, 0x0868,
						par->FbMapSize - 1);
				} else {
					if ((par->Chipset & 0xfff0) == 0x0090 ||
					    (par->Chipset & 0xfff0) == 0x01D0 ||
					    (par->Chipset & 0xfff0) == 0x0290) {
						NV_WR32(par->PGRAPH, 0x0DF0,
							NV_RD32(par->PFB, 0x0200));
						NV_WR32(par->PGRAPH, 0x0DF4,
							NV_RD32(par->PFB, 0x0204));
					} else {
						NV_WR32(par->PGRAPH, 0x09F0,
							NV_RD32(par->PFB, 0x0200));
						NV_WR32(par->PGRAPH, 0x09F4,
							NV_RD32(par->PFB, 0x0204));
					}
					NV_WR32(par->PGRAPH, 0x69F0,
						NV_RD32(par->PFB, 0x0200));
					NV_WR32(par->PGRAPH, 0x69F4,
						NV_RD32(par->PFB, 0x0204));

					NV_WR32(par->PGRAPH, 0x0840, 0);
					NV_WR32(par->PGRAPH, 0x0844, 0);
					NV_WR32(par->PGRAPH, 0x08a0,
						par->FbMapSize - 1);
					NV_WR32(par->PGRAPH, 0x08a4,
						par->FbMapSize - 1);
				}
			} else {
				NV_WR32(par->PGRAPH, 0x09A4,
					NV_RD32(par->PFB, 0x0200));
				NV_WR32(par->PGRAPH, 0x09A8,
					NV_RD32(par->PFB, 0x0204));
				NV_WR32(par->PGRAPH, 0x0750, 0x00EA0000);
				NV_WR32(par->PGRAPH, 0x0754,
					NV_RD32(par->PFB, 0x0200));
				NV_WR32(par->PGRAPH, 0x0750, 0x00EA0004);
				NV_WR32(par->PGRAPH, 0x0754,
					NV_RD32(par->PFB, 0x0204));

				NV_WR32(par->PGRAPH, 0x0820, 0);
				NV_WR32(par->PGRAPH, 0x0824, 0);
				NV_WR32(par->PGRAPH, 0x0864,
					par->FbMapSize - 1);
				NV_WR32(par->PGRAPH, 0x0868,
					par->FbMapSize - 1);
			}
			NV_WR32(par->PGRAPH, 0x0B20, 0x00000000);
			NV_WR32(par->PGRAPH, 0x0B04, 0xFFFFFFFF);
		}
	}
	NV_WR32(par->PGRAPH, 0x053C, 0);
	NV_WR32(par->PGRAPH, 0x0540, 0);
	NV_WR32(par->PGRAPH, 0x0544, 0x00007FFF);
	NV_WR32(par->PGRAPH, 0x0548, 0x00007FFF);

	NV_WR32(par->PFIFO, 0x0140 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0141 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x0480 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0494 * 4, 0x00000000);
	if (par->Architecture >= NV_ARCH_40)
		NV_WR32(par->PFIFO, 0x0481 * 4, 0x00010000);
	else
		NV_WR32(par->PFIFO, 0x0481 * 4, 0x00000100);
	NV_WR32(par->PFIFO, 0x0490 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0491 * 4, 0x00000000);
	if (par->Architecture >= NV_ARCH_40)
		NV_WR32(par->PFIFO, 0x048B * 4, 0x00001213);
	else
		NV_WR32(par->PFIFO, 0x048B * 4, 0x00001209);
	NV_WR32(par->PFIFO, 0x0400 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0414 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0084 * 4, 0x03000100);
	NV_WR32(par->PFIFO, 0x0085 * 4, 0x00000110);
	NV_WR32(par->PFIFO, 0x0086 * 4, 0x00000112);
	NV_WR32(par->PFIFO, 0x0143 * 4, 0x0000FFFF);
	NV_WR32(par->PFIFO, 0x0496 * 4, 0x0000FFFF);
	NV_WR32(par->PFIFO, 0x0050 * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x0040 * 4, 0xFFFFFFFF);
	NV_WR32(par->PFIFO, 0x0415 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x048C * 4, 0x00000000);
	NV_WR32(par->PFIFO, 0x04A0 * 4, 0x00000000);
#ifdef __BIG_ENDIAN
	NV_WR32(par->PFIFO, 0x0489 * 4, 0x800F0078);
#else
	NV_WR32(par->PFIFO, 0x0489 * 4, 0x000F0078);
#endif
	NV_WR32(par->PFIFO, 0x0488 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x0480 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x0494 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x0495 * 4, 0x00000001);
	NV_WR32(par->PFIFO, 0x0140 * 4, 0x00000001);
	if (par->Architecture >= NV_ARCH_10) {
		if (par->twoHeads) {
			NV_WR32(par->PCRTC0, 0x0860, state->head);
			NV_WR32(par->PCRTC0, 0x2860, state->head2);
		}
		NV_WR32(par->PRAMDAC, 0x0404, NV_RD32(par->PRAMDAC, 0x0404) |
			(1 << 25));

		NV_WR32(par->PMC, 0x8704, 1);
		NV_WR32(par->PMC, 0x8140, 0);
		NV_WR32(par->PMC, 0x8920, 0);
		NV_WR32(par->PMC, 0x8924, 0);
		NV_WR32(par->PMC, 0x8908, par->FbMapSize - 1);
		NV_WR32(par->PMC, 0x890C, par->FbMapSize - 1);
		NV_WR32(par->PMC, 0x1588, 0);

		NV_WR32(par->PCRTC, 0x0810, state->cursorConfig);
		NV_WR32(par->PCRTC, 0x0830, state->displayV - 3);
		NV_WR32(par->PCRTC, 0x0834, state->displayV - 1);

		if (par->FlatPanel) {
			if ((par->Chipset & 0x0ff0) == 0x0110) {
				NV_WR32(par->PRAMDAC, 0x0528, state->dither);
			} else if (par->twoHeads) {
				NV_WR32(par->PRAMDAC, 0x083C, state->dither);
			}

			VGA_WR08(par->PCIO, 0x03D4, 0x53);
			VGA_WR08(par->PCIO, 0x03D5, state->timingH);
			VGA_WR08(par->PCIO, 0x03D4, 0x54);
			VGA_WR08(par->PCIO, 0x03D5, state->timingV);
			VGA_WR08(par->PCIO, 0x03D4, 0x21);
			VGA_WR08(par->PCIO, 0x03D5, 0xfa);
		}

		VGA_WR08(par->PCIO, 0x03D4, 0x41);
		VGA_WR08(par->PCIO, 0x03D5, state->extra);
	}

	VGA_WR08(par->PCIO, 0x03D4, 0x19);
	VGA_WR08(par->PCIO, 0x03D5, state->repaint0);
	VGA_WR08(par->PCIO, 0x03D4, 0x1A);
	VGA_WR08(par->PCIO, 0x03D5, state->repaint1);
	VGA_WR08(par->PCIO, 0x03D4, 0x25);
	VGA_WR08(par->PCIO, 0x03D5, state->screen);
	VGA_WR08(par->PCIO, 0x03D4, 0x28);
	VGA_WR08(par->PCIO, 0x03D5, state->pixel);
	VGA_WR08(par->PCIO, 0x03D4, 0x2D);
	VGA_WR08(par->PCIO, 0x03D5, state->horiz);
	VGA_WR08(par->PCIO, 0x03D4, 0x1C);
	VGA_WR08(par->PCIO, 0x03D5, state->fifo);
	VGA_WR08(par->PCIO, 0x03D4, 0x1B);
	VGA_WR08(par->PCIO, 0x03D5, state->arbitration0);
	VGA_WR08(par->PCIO, 0x03D4, 0x20);
	VGA_WR08(par->PCIO, 0x03D5, state->arbitration1);

	if(par->Architecture >= NV_ARCH_30) {
		VGA_WR08(par->PCIO, 0x03D4, 0x47);
		VGA_WR08(par->PCIO, 0x03D5, state->arbitration1 >> 8);
	}

	VGA_WR08(par->PCIO, 0x03D4, 0x30);
	VGA_WR08(par->PCIO, 0x03D5, state->cursor0);
	VGA_WR08(par->PCIO, 0x03D4, 0x31);
	VGA_WR08(par->PCIO, 0x03D5, state->cursor1);
	VGA_WR08(par->PCIO, 0x03D4, 0x2F);
	VGA_WR08(par->PCIO, 0x03D5, state->cursor2);
	VGA_WR08(par->PCIO, 0x03D4, 0x39);
	VGA_WR08(par->PCIO, 0x03D5, state->interlace);

	if (!par->FlatPanel) {
		NV_WR32(par->PRAMDAC0, 0x050C, state->pllsel);
		NV_WR32(par->PRAMDAC0, 0x0508, state->vpll);
		if (par->twoHeads)
			NV_WR32(par->PRAMDAC0, 0x0520, state->vpll2);
		if (par->twoStagePLL) {
			NV_WR32(par->PRAMDAC0, 0x0578, state->vpllB);
			NV_WR32(par->PRAMDAC0, 0x057C, state->vpll2B);
		}
	} else {
		NV_WR32(par->PRAMDAC, 0x0848, state->scale);
		NV_WR32(par->PRAMDAC, 0x0828, state->crtcSync +
			par->PanelTweak);
	}

	NV_WR32(par->PRAMDAC, 0x0600, state->general);

	NV_WR32(par->PCRTC, 0x0140, 0);
	NV_WR32(par->PCRTC, 0x0100, 1);

	par->CurrentState = state;
}

void NVUnloadStateExt(struct nvidia_par *par, RIVA_HW_STATE * state) {
	VGA_WR08(par->PCIO, 0x03D4, 0x19);
	state->repaint0 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x1A);
	state->repaint1 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x25);
	state->screen = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x28);
	state->pixel = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x2D);
	state->horiz = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x1C);
	state->fifo         = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x1B);
	state->arbitration0 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x20);
	state->arbitration1 = VGA_RD08(par->PCIO, 0x03D5);

	if(par->Architecture >= NV_ARCH_30) {
		VGA_WR08(par->PCIO, 0x03D4, 0x47);
		state->arbitration1 |= (VGA_RD08(par->PCIO, 0x03D5) & 1) << 8;
	}

	VGA_WR08(par->PCIO, 0x03D4, 0x30);
	state->cursor0 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x31);
	state->cursor1 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x2F);
	state->cursor2 = VGA_RD08(par->PCIO, 0x03D5);
	VGA_WR08(par->PCIO, 0x03D4, 0x39);
	state->interlace = VGA_RD08(par->PCIO, 0x03D5);
	state->vpll = NV_RD32(par->PRAMDAC0, 0x0508);
	if (par->twoHeads)
		state->vpll2 = NV_RD32(par->PRAMDAC0, 0x0520);
	if (par->twoStagePLL) {
		state->vpllB = NV_RD32(par->PRAMDAC0, 0x0578);
		state->vpll2B = NV_RD32(par->PRAMDAC0, 0x057C);
	}
	state->pllsel = NV_RD32(par->PRAMDAC0, 0x050C);
	state->general = NV_RD32(par->PRAMDAC, 0x0600);
	state->scale = NV_RD32(par->PRAMDAC, 0x0848);
	state->config = NV_RD32(par->PFB, 0x0200);

	if (par->Architecture >= NV_ARCH_10) {
		if (par->twoHeads) {
			state->head = NV_RD32(par->PCRTC0, 0x0860);
			state->head2 = NV_RD32(par->PCRTC0, 0x2860);
			VGA_WR08(par->PCIO, 0x03D4, 0x44);
			state->crtcOwner = VGA_RD08(par->PCIO, 0x03D5);
		}
		VGA_WR08(par->PCIO, 0x03D4, 0x41);
		state->extra = VGA_RD08(par->PCIO, 0x03D5);
		state->cursorConfig = NV_RD32(par->PCRTC, 0x0810);

		if ((par->Chipset & 0x0ff0) == 0x0110) {
			state->dither = NV_RD32(par->PRAMDAC, 0x0528);
		} else if (par->twoHeads) {
			state->dither = NV_RD32(par->PRAMDAC, 0x083C);
		}

		if (par->FlatPanel) {
			VGA_WR08(par->PCIO, 0x03D4, 0x53);
			state->timingH = VGA_RD08(par->PCIO, 0x03D5);
			VGA_WR08(par->PCIO, 0x03D4, 0x54);
			state->timingV = VGA_RD08(par->PCIO, 0x03D5);
		}
	}
}

void NVSetStartAddress(struct nvidia_par *par, u32 start)
{
	NV_WR32(par->PCRTC, 0x800, start);
}
