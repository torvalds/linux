/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include "drmP.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_drm.h"
#include "sid.h"
#include "atom.h"

/* get temperature in millidegrees */
int si_get_temp(struct radeon_device *rdev)
{
	u32 temp;
	int actual_temp = 0;

	temp = (RREG32(CG_MULT_THERMAL_STATUS) & CTF_TEMP_MASK) >>
		CTF_TEMP_SHIFT;

	if (temp & 0x200)
		actual_temp = 255;
	else
		actual_temp = temp & 0x1ff;

	actual_temp = (actual_temp * 1000);

	return actual_temp;
}

/* watermark setup */
static u32 dce6_line_buffer_adjust(struct radeon_device *rdev,
				   struct radeon_crtc *radeon_crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *other_mode)
{
	u32 tmp;
	/*
	 * Line Buffer Setup
	 * There are 3 line buffers, each one shared by 2 display controllers.
	 * DC_LB_MEMORY_SPLIT controls how that line buffer is shared between
	 * the display controllers.  The paritioning is done via one of four
	 * preset allocations specified in bits 21:20:
	 *  0 - half lb
	 *  2 - whole lb, other crtc must be disabled
	 */
	/* this can get tricky if we have two large displays on a paired group
	 * of crtcs.  Ideally for multiple large displays we'd assign them to
	 * non-linked crtcs for maximum line buffer allocation.
	 */
	if (radeon_crtc->base.enabled && mode) {
		if (other_mode)
			tmp = 0; /* 1/2 */
		else
			tmp = 2; /* whole */
	} else
		tmp = 0;

	WREG32(DC_LB_MEMORY_SPLIT + radeon_crtc->crtc_offset,
	       DC_LB_MEMORY_CONFIG(tmp));

	if (radeon_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 2:
			return 8192 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

static u32 dce6_get_number_of_dram_channels(struct radeon_device *rdev)
{
	u32 tmp = RREG32(MC_SHARED_CHMAP);

	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 8;
	case 4:
		return 3;
	case 5:
		return 6;
	case 6:
		return 10;
	case 7:
		return 12;
	case 8:
		return 16;
	}
}

struct dce6_wm_params {
	u32 dram_channels; /* number of dram channels */
	u32 yclk;          /* bandwidth per dram data pin in kHz */
	u32 sclk;          /* engine clock in kHz */
	u32 disp_clk;      /* display clock in kHz */
	u32 src_width;     /* viewport width */
	u32 active_time;   /* active display time in ns */
	u32 blank_time;    /* blank time in ns */
	bool interlaced;    /* mode is interlaced */
	fixed20_12 vsc;    /* vertical scale ratio */
	u32 num_heads;     /* number of active crtcs */
	u32 bytes_per_pixel; /* bytes per pixel display + overlay */
	u32 lb_size;       /* line buffer allocated to pipe */
	u32 vtaps;         /* vertical scaler taps */
};

static u32 dce6_dram_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate raw DRAM Bandwidth */
	fixed20_12 dram_efficiency; /* 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	dram_efficiency.full = dfixed_const(7);
	dram_efficiency.full = dfixed_div(dram_efficiency, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, dram_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	/* Calculate DRAM Bandwidth and the part allocated to display. */
	fixed20_12 disp_dram_allocation; /* 0.3 to 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	disp_dram_allocation.full = dfixed_const(3); /* XXX worse case value 0.3 */
	disp_dram_allocation.full = dfixed_div(disp_dram_allocation, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, disp_dram_allocation);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_data_return_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display Data return Bandwidth */
	fixed20_12 return_efficiency; /* 0.8 */
	fixed20_12 sclk, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(10);
	return_efficiency.full = dfixed_const(8);
	return_efficiency.full = dfixed_div(return_efficiency, a);
	a.full = dfixed_const(32);
	bandwidth.full = dfixed_mul(a, sclk);
	bandwidth.full = dfixed_mul(bandwidth, return_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_get_dmif_bytes_per_request(struct dce6_wm_params *wm)
{
	return 32;
}

static u32 dce6_dmif_request_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, sclk, bandwidth;
	fixed20_12 a, b1, b2;
	u32 min_bandwidth;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm) / 2);
	b1.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm));
	b2.full = dfixed_mul(a, sclk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	min_bandwidth = min(dfixed_trunc(b1), dfixed_trunc(b2));

	a.full = dfixed_const(min_bandwidth);
	bandwidth.full = dfixed_mul(a, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_available_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce6_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce6_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce6_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

static u32 dce6_average_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display mode Average Bandwidth
	 * DisplayMode should contain the source and destination dimensions,
	 * timing, etc.
	 */
	fixed20_12 bpp;
	fixed20_12 line_time;
	fixed20_12 src_width;
	fixed20_12 bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	line_time.full = dfixed_const(wm->active_time + wm->blank_time);
	line_time.full = dfixed_div(line_time, a);
	bpp.full = dfixed_const(wm->bytes_per_pixel);
	src_width.full = dfixed_const(wm->src_width);
	bandwidth.full = dfixed_mul(src_width, bpp);
	bandwidth.full = dfixed_mul(bandwidth, wm->vsc);
	bandwidth.full = dfixed_div(bandwidth, line_time);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_latency_watermark(struct dce6_wm_params *wm)
{
	/* First calcualte the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce6_available_bandwidth(wm);
	u32 worst_chunk_return_time = (512 * 8 * 1000) / available_bandwidth;
	u32 cursor_line_pair_return_time = (128 * 4 * 1000) / available_bandwidth;
	u32 dc_latency = 40000000 / wm->disp_clk; /* dc pipe latency */
	u32 other_heads_data_return_time = ((wm->num_heads + 1) * worst_chunk_return_time) +
		(wm->num_heads * cursor_line_pair_return_time);
	u32 latency = mc_latency + other_heads_data_return_time + dc_latency;
	u32 max_src_lines_per_dst_line, lb_fill_bw, line_fill_time;
	u32 tmp, dmif_size = 12288;
	fixed20_12 a, b, c;

	if (wm->num_heads == 0)
		return 0;

	a.full = dfixed_const(2);
	b.full = dfixed_const(1);
	if ((wm->vsc.full > a.full) ||
	    ((wm->vsc.full > b.full) && (wm->vtaps >= 3)) ||
	    (wm->vtaps >= 5) ||
	    ((wm->vsc.full >= a.full) && wm->interlaced))
		max_src_lines_per_dst_line = 4;
	else
		max_src_lines_per_dst_line = 2;

	a.full = dfixed_const(available_bandwidth);
	b.full = dfixed_const(wm->num_heads);
	a.full = dfixed_div(a, b);

	b.full = dfixed_const(mc_latency + 512);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(b, c);

	c.full = dfixed_const(dmif_size);
	b.full = dfixed_div(c, b);

	tmp = min(dfixed_trunc(a), dfixed_trunc(b));

	b.full = dfixed_const(1000);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(c, b);
	c.full = dfixed_const(wm->bytes_per_pixel);
	b.full = dfixed_mul(b, c);

	lb_fill_bw = min(tmp, dfixed_trunc(b));

	a.full = dfixed_const(max_src_lines_per_dst_line * wm->src_width * wm->bytes_per_pixel);
	b.full = dfixed_const(1000);
	c.full = dfixed_const(lb_fill_bw);
	b.full = dfixed_div(c, b);
	a.full = dfixed_div(a, b);
	line_fill_time = dfixed_trunc(a);

	if (line_fill_time < wm->active_time)
		return latency;
	else
		return latency + (line_fill_time - wm->active_time);

}

static bool dce6_average_bandwidth_vs_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_average_bandwidth_vs_available_bandwidth(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_check_latency_hiding(struct dce6_wm_params *wm)
{
	u32 lb_partitions = wm->lb_size / wm->src_width;
	u32 line_time = wm->active_time + wm->blank_time;
	u32 latency_tolerant_lines;
	u32 latency_hiding;
	fixed20_12 a;

	a.full = dfixed_const(1);
	if (wm->vsc.full > a.full)
		latency_tolerant_lines = 1;
	else {
		if (lb_partitions <= (wm->vtaps + 1))
			latency_tolerant_lines = 1;
		else
			latency_tolerant_lines = 2;
	}

	latency_hiding = (latency_tolerant_lines * line_time + wm->blank_time);

	if (dce6_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

static void dce6_program_watermarks(struct radeon_device *rdev,
					 struct radeon_crtc *radeon_crtc,
					 u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &radeon_crtc->base.mode;
	struct dce6_wm_params wm;
	u32 pixel_period;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 priority_a_mark = 0, priority_b_mark = 0;
	u32 priority_a_cnt = PRIORITY_OFF;
	u32 priority_b_cnt = PRIORITY_OFF;
	u32 tmp, arb_control3;
	fixed20_12 a, b, c;

	if (radeon_crtc->base.enabled && num_heads && mode) {
		pixel_period = 1000000 / (u32)mode->clock;
		line_time = min((u32)mode->crtc_htotal * pixel_period, (u32)65535);
		priority_a_cnt = 0;
		priority_b_cnt = 0;

		wm.yclk = rdev->pm.current_mclk * 10;
		wm.sclk = rdev->pm.current_sclk * 10;
		wm.disp_clk = mode->clock;
		wm.src_width = mode->crtc_hdisplay;
		wm.active_time = mode->crtc_hdisplay * pixel_period;
		wm.blank_time = line_time - wm.active_time;
		wm.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm.interlaced = true;
		wm.vsc = radeon_crtc->vsc;
		wm.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm.vtaps = 2;
		wm.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm.lb_size = lb_size;
		wm.dram_channels = dce6_get_number_of_dram_channels(rdev);
		wm.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce6_latency_watermark(&wm), (u32)65535);
		/* set for low clocks */
		/* wm.yclk = low clk; wm.sclk = low clk */
		latency_watermark_b = min(dce6_latency_watermark(&wm), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce6_average_bandwidth_vs_dram_bandwidth_for_display(&wm) ||
		    !dce6_average_bandwidth_vs_available_bandwidth(&wm) ||
		    !dce6_check_latency_hiding(&wm) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
			priority_a_cnt |= PRIORITY_ALWAYS_ON;
			priority_b_cnt |= PRIORITY_ALWAYS_ON;
		}

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_a);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_a_mark = dfixed_trunc(c);
		priority_a_cnt |= priority_a_mark & PRIORITY_MARK_MASK;

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_b);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_b_mark = dfixed_trunc(c);
		priority_b_cnt |= priority_b_mark & PRIORITY_MARK_MASK;
	}

	/* select wm A */
	arb_control3 = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp = arb_control3;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_a) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* select wm B */
	tmp = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_b) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* restore original selection */
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, arb_control3);

	/* write the priority marks */
	WREG32(PRIORITY_A_CNT + radeon_crtc->crtc_offset, priority_a_cnt);
	WREG32(PRIORITY_B_CNT + radeon_crtc->crtc_offset, priority_b_cnt);

}

void dce6_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	radeon_update_display_priority(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < rdev->num_crtc; i += 2) {
		mode0 = &rdev->mode_info.crtcs[i]->base.mode;
		mode1 = &rdev->mode_info.crtcs[i+1]->base.mode;
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i], mode0, mode1);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i], lb_size, num_heads);
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i+1], mode1, mode0);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i+1], lb_size, num_heads);
	}
}

