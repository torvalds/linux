/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include "atom.h"
#include "rs690d.h"

int rs690_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(R_000090_MC_SYSTEM_STATUS);
		if (G_000090_MC_SYSTEM_IDLE(tmp))
			return 0;
		udelay(1);
	}
	return -1;
}

static void rs690_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	if (rs690_mc_wait_for_idle(rdev)) {
		pr_warn("Failed to wait MC idle while programming pipes. Bad things might happen.\n");
	}
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_v2;
};

void rs690_pm_info(struct radeon_device *rdev)
{
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *info;
	uint16_t data_offset;
	uint8_t frev, crev;
	fixed20_12 tmp;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		info = (union igp_info *)(rdev->mode_info.atom_context->bios + data_offset);

		/* Get various system informations from bios */
		switch (crev) {
		case 1:
			tmp.full = dfixed_const(100);
			rdev->pm.igp_sideport_mclk.full = dfixed_const(le32_to_cpu(info->info.ulBootUpMemoryClock));
			rdev->pm.igp_sideport_mclk.full = dfixed_div(rdev->pm.igp_sideport_mclk, tmp);
			if (le16_to_cpu(info->info.usK8MemoryClock))
				rdev->pm.igp_system_mclk.full = dfixed_const(le16_to_cpu(info->info.usK8MemoryClock));
			else if (rdev->clock.default_mclk) {
				rdev->pm.igp_system_mclk.full = dfixed_const(rdev->clock.default_mclk);
				rdev->pm.igp_system_mclk.full = dfixed_div(rdev->pm.igp_system_mclk, tmp);
			} else
				rdev->pm.igp_system_mclk.full = dfixed_const(400);
			rdev->pm.igp_ht_link_clk.full = dfixed_const(le16_to_cpu(info->info.usFSBClock));
			rdev->pm.igp_ht_link_width.full = dfixed_const(info->info.ucHTLinkWidth);
			break;
		case 2:
			tmp.full = dfixed_const(100);
			rdev->pm.igp_sideport_mclk.full = dfixed_const(le32_to_cpu(info->info_v2.ulBootUpSidePortClock));
			rdev->pm.igp_sideport_mclk.full = dfixed_div(rdev->pm.igp_sideport_mclk, tmp);
			if (le32_to_cpu(info->info_v2.ulBootUpUMAClock))
				rdev->pm.igp_system_mclk.full = dfixed_const(le32_to_cpu(info->info_v2.ulBootUpUMAClock));
			else if (rdev->clock.default_mclk)
				rdev->pm.igp_system_mclk.full = dfixed_const(rdev->clock.default_mclk);
			else
				rdev->pm.igp_system_mclk.full = dfixed_const(66700);
			rdev->pm.igp_system_mclk.full = dfixed_div(rdev->pm.igp_system_mclk, tmp);
			rdev->pm.igp_ht_link_clk.full = dfixed_const(le32_to_cpu(info->info_v2.ulHTLinkFreq));
			rdev->pm.igp_ht_link_clk.full = dfixed_div(rdev->pm.igp_ht_link_clk, tmp);
			rdev->pm.igp_ht_link_width.full = dfixed_const(le16_to_cpu(info->info_v2.usMinHTLinkWidth));
			break;
		default:
			/* We assume the slower possible clock ie worst case */
			rdev->pm.igp_sideport_mclk.full = dfixed_const(200);
			rdev->pm.igp_system_mclk.full = dfixed_const(200);
			rdev->pm.igp_ht_link_clk.full = dfixed_const(1000);
			rdev->pm.igp_ht_link_width.full = dfixed_const(8);
			DRM_ERROR("No integrated system info for your GPU, using safe default\n");
			break;
		}
	} else {
		/* We assume the slower possible clock ie worst case */
		rdev->pm.igp_sideport_mclk.full = dfixed_const(200);
		rdev->pm.igp_system_mclk.full = dfixed_const(200);
		rdev->pm.igp_ht_link_clk.full = dfixed_const(1000);
		rdev->pm.igp_ht_link_width.full = dfixed_const(8);
		DRM_ERROR("No integrated system info for your GPU, using safe default\n");
	}
	/* Compute various bandwidth */
	/* k8_bandwidth = (memory_clk / 2) * 2 * 8 * 0.5 = memory_clk * 4  */
	tmp.full = dfixed_const(4);
	rdev->pm.k8_bandwidth.full = dfixed_mul(rdev->pm.igp_system_mclk, tmp);
	/* ht_bandwidth = ht_clk * 2 * ht_width / 8 * 0.8
	 *              = ht_clk * ht_width / 5
	 */
	tmp.full = dfixed_const(5);
	rdev->pm.ht_bandwidth.full = dfixed_mul(rdev->pm.igp_ht_link_clk,
						rdev->pm.igp_ht_link_width);
	rdev->pm.ht_bandwidth.full = dfixed_div(rdev->pm.ht_bandwidth, tmp);
	if (tmp.full < rdev->pm.max_bandwidth.full) {
		/* HT link is a limiting factor */
		rdev->pm.max_bandwidth.full = tmp.full;
	}
	/* sideport_bandwidth = (sideport_clk / 2) * 2 * 2 * 0.7
	 *                    = (sideport_clk * 14) / 10
	 */
	tmp.full = dfixed_const(14);
	rdev->pm.sideport_bandwidth.full = dfixed_mul(rdev->pm.igp_sideport_mclk, tmp);
	tmp.full = dfixed_const(10);
	rdev->pm.sideport_bandwidth.full = dfixed_div(rdev->pm.sideport_bandwidth, tmp);
}

static void rs690_mc_init(struct radeon_device *rdev)
{
	u64 base;
	uint32_t h_addr, l_addr;
	unsigned long long k8_addr;

	rs400_gart_adjust_size(rdev);
	rdev->mc.vram_is_ddr = true;
	rdev->mc.vram_width = 128;
	rdev->mc.real_vram_size = RREG32(RADEON_CONFIG_MEMSIZE);
	rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	base = RREG32_MC(R_000100_MCCFG_FB_LOCATION);
	base = G_000100_MC_FB_START(base) << 16;
	rdev->mc.igp_sideport_enabled = radeon_atombios_sideport_present(rdev);
	/* Some boards seem to be configured for 128MB of sideport memory,
	 * but really only have 64MB.  Just skip the sideport and use
	 * UMA memory.
	 */
	if (rdev->mc.igp_sideport_enabled &&
	    (rdev->mc.real_vram_size == (384 * 1024 * 1024))) {
		base += 128 * 1024 * 1024;
		rdev->mc.real_vram_size -= 128 * 1024 * 1024;
		rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	}

	/* Use K8 direct mapping for fast fb access. */ 
	rdev->fastfb_working = false;
	h_addr = G_00005F_K8_ADDR_EXT(RREG32_MC(R_00005F_MC_MISC_UMA_CNTL));
	l_addr = RREG32_MC(R_00001E_K8_FB_LOCATION);
	k8_addr = ((unsigned long long)h_addr) << 32 | l_addr;
#if defined(CONFIG_X86_32) && !defined(CONFIG_X86_PAE)
	if (k8_addr + rdev->mc.visible_vram_size < 0x100000000ULL)	
#endif
	{
		/* FastFB shall be used with UMA memory. Here it is simply disabled when sideport 
		 * memory is present.
		 */
		if (rdev->mc.igp_sideport_enabled == false && radeon_fastfb == 1) {
			DRM_INFO("Direct mapping: aper base at 0x%llx, replaced by direct mapping base 0x%llx.\n", 
					(unsigned long long)rdev->mc.aper_base, k8_addr);
			rdev->mc.aper_base = (resource_size_t)k8_addr;
			rdev->fastfb_working = true;
		}
	}  

	rs690_pm_info(rdev);
	radeon_vram_location(rdev, &rdev->mc, base);
	rdev->mc.gtt_base_align = rdev->mc.gtt_size - 1;
	radeon_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);
}

void rs690_line_buffer_adjust(struct radeon_device *rdev,
			      struct drm_display_mode *mode1,
			      struct drm_display_mode *mode2)
{
	u32 tmp;

	/* Guess line buffer size to be 8192 pixels */
	u32 lb_size = 8192;

	/*
	 * Line Buffer Setup
	 * There is a single line buffer shared by both display controllers.
	 * R_006520_DC_LB_MEMORY_SPLIT controls how that line buffer is shared between
	 * the display controllers.  The paritioning can either be done
	 * manually or via one of four preset allocations specified in bits 1:0:
	 *  0 - line buffer is divided in half and shared between crtc
	 *  1 - D1 gets 3/4 of the line buffer, D2 gets 1/4
	 *  2 - D1 gets the whole buffer
	 *  3 - D1 gets 1/4 of the line buffer, D2 gets 3/4
	 * Setting bit 2 of R_006520_DC_LB_MEMORY_SPLIT controls switches to manual
	 * allocation mode. In manual allocation mode, D1 always starts at 0,
	 * D1 end/2 is specified in bits 14:4; D2 allocation follows D1.
	 */
	tmp = RREG32(R_006520_DC_LB_MEMORY_SPLIT) & C_006520_DC_LB_MEMORY_SPLIT;
	tmp &= ~C_006520_DC_LB_MEMORY_SPLIT_MODE;
	/* auto */
	if (mode1 && mode2) {
		if (mode1->hdisplay > mode2->hdisplay) {
			if (mode1->hdisplay > 2560)
				tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1_3Q_D2_1Q;
			else
				tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF;
		} else if (mode2->hdisplay > mode1->hdisplay) {
			if (mode2->hdisplay > 2560)
				tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1_1Q_D2_3Q;
			else
				tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF;
		} else
			tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF;
	} else if (mode1) {
		tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1_ONLY;
	} else if (mode2) {
		tmp |= V_006520_DC_LB_MEMORY_SPLIT_D1_1Q_D2_3Q;
	}
	WREG32(R_006520_DC_LB_MEMORY_SPLIT, tmp);

	/* Save number of lines the linebuffer leads before the scanout */
	if (mode1)
		rdev->mode_info.crtcs[0]->lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode1->crtc_hdisplay);

	if (mode2)
		rdev->mode_info.crtcs[1]->lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode2->crtc_hdisplay);
}

struct rs690_watermark {
	u32        lb_request_fifo_depth;
	fixed20_12 num_line_pair;
	fixed20_12 estimated_width;
	fixed20_12 worst_case_latency;
	fixed20_12 consumption_rate;
	fixed20_12 active_time;
	fixed20_12 dbpp;
	fixed20_12 priority_mark_max;
	fixed20_12 priority_mark;
	fixed20_12 sclk;
};

static void rs690_crtc_bandwidth_compute(struct radeon_device *rdev,
					 struct radeon_crtc *crtc,
					 struct rs690_watermark *wm,
					 bool low)
{
	struct drm_display_mode *mode = &crtc->base.mode;
	fixed20_12 a, b, c;
	fixed20_12 pclk, request_fifo_depth, tolerable_latency, estimated_width;
	fixed20_12 consumption_time, line_time, chunk_time, read_delay_latency;
	fixed20_12 sclk, core_bandwidth, max_bandwidth;
	u32 selected_sclk;

	if (!crtc->base.enabled) {
		/* FIXME: wouldn't it better to set priority mark to maximum */
		wm->lb_request_fifo_depth = 4;
		return;
	}

	if (((rdev->family == CHIP_RS780) || (rdev->family == CHIP_RS880)) &&
	    (rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled)
		selected_sclk = radeon_dpm_get_sclk(rdev, low);
	else
		selected_sclk = rdev->pm.current_sclk;

	/* sclk in Mhz */
	a.full = dfixed_const(100);
	sclk.full = dfixed_const(selected_sclk);
	sclk.full = dfixed_div(sclk, a);

	/* core_bandwidth = sclk(Mhz) * 16 */
	a.full = dfixed_const(16);
	core_bandwidth.full = dfixed_div(rdev->pm.sclk, a);

	if (crtc->vsc.full > dfixed_const(2))
		wm->num_line_pair.full = dfixed_const(2);
	else
		wm->num_line_pair.full = dfixed_const(1);

	b.full = dfixed_const(mode->crtc_hdisplay);
	c.full = dfixed_const(256);
	a.full = dfixed_div(b, c);
	request_fifo_depth.full = dfixed_mul(a, wm->num_line_pair);
	request_fifo_depth.full = dfixed_ceil(request_fifo_depth);
	if (a.full < dfixed_const(4)) {
		wm->lb_request_fifo_depth = 4;
	} else {
		wm->lb_request_fifo_depth = dfixed_trunc(request_fifo_depth);
	}

	/* Determine consumption rate
	 *  pclk = pixel clock period(ns) = 1000 / (mode.clock / 1000)
	 *  vtaps = number of vertical taps,
	 *  vsc = vertical scaling ratio, defined as source/destination
	 *  hsc = horizontal scaling ration, defined as source/destination
	 */
	a.full = dfixed_const(mode->clock);
	b.full = dfixed_const(1000);
	a.full = dfixed_div(a, b);
	pclk.full = dfixed_div(b, a);
	if (crtc->rmx_type != RMX_OFF) {
		b.full = dfixed_const(2);
		if (crtc->vsc.full > b.full)
			b.full = crtc->vsc.full;
		b.full = dfixed_mul(b, crtc->hsc);
		c.full = dfixed_const(2);
		b.full = dfixed_div(b, c);
		consumption_time.full = dfixed_div(pclk, b);
	} else {
		consumption_time.full = pclk.full;
	}
	a.full = dfixed_const(1);
	wm->consumption_rate.full = dfixed_div(a, consumption_time);


	/* Determine line time
	 *  LineTime = total time for one line of displayhtotal
	 *  LineTime = total number of horizontal pixels
	 *  pclk = pixel clock period(ns)
	 */
	a.full = dfixed_const(crtc->base.mode.crtc_htotal);
	line_time.full = dfixed_mul(a, pclk);

	/* Determine active time
	 *  ActiveTime = time of active region of display within one line,
	 *  hactive = total number of horizontal active pixels
	 *  htotal = total number of horizontal pixels
	 */
	a.full = dfixed_const(crtc->base.mode.crtc_htotal);
	b.full = dfixed_const(crtc->base.mode.crtc_hdisplay);
	wm->active_time.full = dfixed_mul(line_time, b);
	wm->active_time.full = dfixed_div(wm->active_time, a);

	/* Maximun bandwidth is the minimun bandwidth of all component */
	max_bandwidth = core_bandwidth;
	if (rdev->mc.igp_sideport_enabled) {
		if (max_bandwidth.full > rdev->pm.sideport_bandwidth.full &&
			rdev->pm.sideport_bandwidth.full)
			max_bandwidth = rdev->pm.sideport_bandwidth;
		read_delay_latency.full = dfixed_const(370 * 800);
		a.full = dfixed_const(1000);
		b.full = dfixed_div(rdev->pm.igp_sideport_mclk, a);
		read_delay_latency.full = dfixed_div(read_delay_latency, b);
		read_delay_latency.full = dfixed_mul(read_delay_latency, a);
	} else {
		if (max_bandwidth.full > rdev->pm.k8_bandwidth.full &&
			rdev->pm.k8_bandwidth.full)
			max_bandwidth = rdev->pm.k8_bandwidth;
		if (max_bandwidth.full > rdev->pm.ht_bandwidth.full &&
			rdev->pm.ht_bandwidth.full)
			max_bandwidth = rdev->pm.ht_bandwidth;
		read_delay_latency.full = dfixed_const(5000);
	}

	/* sclk = system clocks(ns) = 1000 / max_bandwidth / 16 */
	a.full = dfixed_const(16);
	sclk.full = dfixed_mul(max_bandwidth, a);
	a.full = dfixed_const(1000);
	sclk.full = dfixed_div(a, sclk);
	/* Determine chunk time
	 * ChunkTime = the time it takes the DCP to send one chunk of data
	 * to the LB which consists of pipeline delay and inter chunk gap
	 * sclk = system clock(ns)
	 */
	a.full = dfixed_const(256 * 13);
	chunk_time.full = dfixed_mul(sclk, a);
	a.full = dfixed_const(10);
	chunk_time.full = dfixed_div(chunk_time, a);

	/* Determine the worst case latency
	 * NumLinePair = Number of line pairs to request(1=2 lines, 2=4 lines)
	 * WorstCaseLatency = worst case time from urgent to when the MC starts
	 *                    to return data
	 * READ_DELAY_IDLE_MAX = constant of 1us
	 * ChunkTime = time it takes the DCP to send one chunk of data to the LB
	 *             which consists of pipeline delay and inter chunk gap
	 */
	if (dfixed_trunc(wm->num_line_pair) > 1) {
		a.full = dfixed_const(3);
		wm->worst_case_latency.full = dfixed_mul(a, chunk_time);
		wm->worst_case_latency.full += read_delay_latency.full;
	} else {
		a.full = dfixed_const(2);
		wm->worst_case_latency.full = dfixed_mul(a, chunk_time);
		wm->worst_case_latency.full += read_delay_latency.full;
	}

	/* Determine the tolerable latency
	 * TolerableLatency = Any given request has only 1 line time
	 *                    for the data to be returned
	 * LBRequestFifoDepth = Number of chunk requests the LB can
	 *                      put into the request FIFO for a display
	 *  LineTime = total time for one line of display
	 *  ChunkTime = the time it takes the DCP to send one chunk
	 *              of data to the LB which consists of
	 *  pipeline delay and inter chunk gap
	 */
	if ((2+wm->lb_request_fifo_depth) >= dfixed_trunc(request_fifo_depth)) {
		tolerable_latency.full = line_time.full;
	} else {
		tolerable_latency.full = dfixed_const(wm->lb_request_fifo_depth - 2);
		tolerable_latency.full = request_fifo_depth.full - tolerable_latency.full;
		tolerable_latency.full = dfixed_mul(tolerable_latency, chunk_time);
		tolerable_latency.full = line_time.full - tolerable_latency.full;
	}
	/* We assume worst case 32bits (4 bytes) */
	wm->dbpp.full = dfixed_const(4 * 8);

	/* Determine the maximum priority mark
	 *  width = viewport width in pixels
	 */
	a.full = dfixed_const(16);
	wm->priority_mark_max.full = dfixed_const(crtc->base.mode.crtc_hdisplay);
	wm->priority_mark_max.full = dfixed_div(wm->priority_mark_max, a);
	wm->priority_mark_max.full = dfixed_ceil(wm->priority_mark_max);

	/* Determine estimated width */
	estimated_width.full = tolerable_latency.full - wm->worst_case_latency.full;
	estimated_width.full = dfixed_div(estimated_width, consumption_time);
	if (dfixed_trunc(estimated_width) > crtc->base.mode.crtc_hdisplay) {
		wm->priority_mark.full = dfixed_const(10);
	} else {
		a.full = dfixed_const(16);
		wm->priority_mark.full = dfixed_div(estimated_width, a);
		wm->priority_mark.full = dfixed_ceil(wm->priority_mark);
		wm->priority_mark.full = wm->priority_mark_max.full - wm->priority_mark.full;
	}
}

static void rs690_compute_mode_priority(struct radeon_device *rdev,
					struct rs690_watermark *wm0,
					struct rs690_watermark *wm1,
					struct drm_display_mode *mode0,
					struct drm_display_mode *mode1,
					u32 *d1mode_priority_a_cnt,
					u32 *d2mode_priority_a_cnt)
{
	fixed20_12 priority_mark02, priority_mark12, fill_rate;
	fixed20_12 a, b;

	*d1mode_priority_a_cnt = S_006548_D1MODE_PRIORITY_A_OFF(1);
	*d2mode_priority_a_cnt = S_006548_D1MODE_PRIORITY_A_OFF(1);

	if (mode0 && mode1) {
		if (dfixed_trunc(wm0->dbpp) > 64)
			a.full = dfixed_mul(wm0->dbpp, wm0->num_line_pair);
		else
			a.full = wm0->num_line_pair.full;
		if (dfixed_trunc(wm1->dbpp) > 64)
			b.full = dfixed_mul(wm1->dbpp, wm1->num_line_pair);
		else
			b.full = wm1->num_line_pair.full;
		a.full += b.full;
		fill_rate.full = dfixed_div(wm0->sclk, a);
		if (wm0->consumption_rate.full > fill_rate.full) {
			b.full = wm0->consumption_rate.full - fill_rate.full;
			b.full = dfixed_mul(b, wm0->active_time);
			a.full = dfixed_mul(wm0->worst_case_latency,
						wm0->consumption_rate);
			a.full = a.full + b.full;
			b.full = dfixed_const(16 * 1000);
			priority_mark02.full = dfixed_div(a, b);
		} else {
			a.full = dfixed_mul(wm0->worst_case_latency,
						wm0->consumption_rate);
			b.full = dfixed_const(16 * 1000);
			priority_mark02.full = dfixed_div(a, b);
		}
		if (wm1->consumption_rate.full > fill_rate.full) {
			b.full = wm1->consumption_rate.full - fill_rate.full;
			b.full = dfixed_mul(b, wm1->active_time);
			a.full = dfixed_mul(wm1->worst_case_latency,
						wm1->consumption_rate);
			a.full = a.full + b.full;
			b.full = dfixed_const(16 * 1000);
			priority_mark12.full = dfixed_div(a, b);
		} else {
			a.full = dfixed_mul(wm1->worst_case_latency,
						wm1->consumption_rate);
			b.full = dfixed_const(16 * 1000);
			priority_mark12.full = dfixed_div(a, b);
		}
		if (wm0->priority_mark.full > priority_mark02.full)
			priority_mark02.full = wm0->priority_mark.full;
		if (wm0->priority_mark_max.full > priority_mark02.full)
			priority_mark02.full = wm0->priority_mark_max.full;
		if (wm1->priority_mark.full > priority_mark12.full)
			priority_mark12.full = wm1->priority_mark.full;
		if (wm1->priority_mark_max.full > priority_mark12.full)
			priority_mark12.full = wm1->priority_mark_max.full;
		*d1mode_priority_a_cnt = dfixed_trunc(priority_mark02);
		*d2mode_priority_a_cnt = dfixed_trunc(priority_mark12);
		if (rdev->disp_priority == 2) {
			*d1mode_priority_a_cnt |= S_006548_D1MODE_PRIORITY_A_ALWAYS_ON(1);
			*d2mode_priority_a_cnt |= S_006D48_D2MODE_PRIORITY_A_ALWAYS_ON(1);
		}
	} else if (mode0) {
		if (dfixed_trunc(wm0->dbpp) > 64)
			a.full = dfixed_mul(wm0->dbpp, wm0->num_line_pair);
		else
			a.full = wm0->num_line_pair.full;
		fill_rate.full = dfixed_div(wm0->sclk, a);
		if (wm0->consumption_rate.full > fill_rate.full) {
			b.full = wm0->consumption_rate.full - fill_rate.full;
			b.full = dfixed_mul(b, wm0->active_time);
			a.full = dfixed_mul(wm0->worst_case_latency,
						wm0->consumption_rate);
			a.full = a.full + b.full;
			b.full = dfixed_const(16 * 1000);
			priority_mark02.full = dfixed_div(a, b);
		} else {
			a.full = dfixed_mul(wm0->worst_case_latency,
						wm0->consumption_rate);
			b.full = dfixed_const(16 * 1000);
			priority_mark02.full = dfixed_div(a, b);
		}
		if (wm0->priority_mark.full > priority_mark02.full)
			priority_mark02.full = wm0->priority_mark.full;
		if (wm0->priority_mark_max.full > priority_mark02.full)
			priority_mark02.full = wm0->priority_mark_max.full;
		*d1mode_priority_a_cnt = dfixed_trunc(priority_mark02);
		if (rdev->disp_priority == 2)
			*d1mode_priority_a_cnt |= S_006548_D1MODE_PRIORITY_A_ALWAYS_ON(1);
	} else if (mode1) {
		if (dfixed_trunc(wm1->dbpp) > 64)
			a.full = dfixed_mul(wm1->dbpp, wm1->num_line_pair);
		else
			a.full = wm1->num_line_pair.full;
		fill_rate.full = dfixed_div(wm1->sclk, a);
		if (wm1->consumption_rate.full > fill_rate.full) {
			b.full = wm1->consumption_rate.full - fill_rate.full;
			b.full = dfixed_mul(b, wm1->active_time);
			a.full = dfixed_mul(wm1->worst_case_latency,
						wm1->consumption_rate);
			a.full = a.full + b.full;
			b.full = dfixed_const(16 * 1000);
			priority_mark12.full = dfixed_div(a, b);
		} else {
			a.full = dfixed_mul(wm1->worst_case_latency,
						wm1->consumption_rate);
			b.full = dfixed_const(16 * 1000);
			priority_mark12.full = dfixed_div(a, b);
		}
		if (wm1->priority_mark.full > priority_mark12.full)
			priority_mark12.full = wm1->priority_mark.full;
		if (wm1->priority_mark_max.full > priority_mark12.full)
			priority_mark12.full = wm1->priority_mark_max.full;
		*d2mode_priority_a_cnt = dfixed_trunc(priority_mark12);
		if (rdev->disp_priority == 2)
			*d2mode_priority_a_cnt |= S_006D48_D2MODE_PRIORITY_A_ALWAYS_ON(1);
	}
}

void rs690_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	struct rs690_watermark wm0_high, wm0_low;
	struct rs690_watermark wm1_high, wm1_low;
	u32 tmp;
	u32 d1mode_priority_a_cnt, d1mode_priority_b_cnt;
	u32 d2mode_priority_a_cnt, d2mode_priority_b_cnt;

	if (!rdev->mode_info.mode_config_initialized)
		return;

	radeon_update_display_priority(rdev);

	if (rdev->mode_info.crtcs[0]->base.enabled)
		mode0 = &rdev->mode_info.crtcs[0]->base.mode;
	if (rdev->mode_info.crtcs[1]->base.enabled)
		mode1 = &rdev->mode_info.crtcs[1]->base.mode;
	/*
	 * Set display0/1 priority up in the memory controller for
	 * modes if the user specifies HIGH for displaypriority
	 * option.
	 */
	if ((rdev->disp_priority == 2) &&
	    ((rdev->family == CHIP_RS690) || (rdev->family == CHIP_RS740))) {
		tmp = RREG32_MC(R_000104_MC_INIT_MISC_LAT_TIMER);
		tmp &= C_000104_MC_DISP0R_INIT_LAT;
		tmp &= C_000104_MC_DISP1R_INIT_LAT;
		if (mode0)
			tmp |= S_000104_MC_DISP0R_INIT_LAT(1);
		if (mode1)
			tmp |= S_000104_MC_DISP1R_INIT_LAT(1);
		WREG32_MC(R_000104_MC_INIT_MISC_LAT_TIMER, tmp);
	}
	rs690_line_buffer_adjust(rdev, mode0, mode1);

	if ((rdev->family == CHIP_RS690) || (rdev->family == CHIP_RS740))
		WREG32(R_006C9C_DCP_CONTROL, 0);
	if ((rdev->family == CHIP_RS780) || (rdev->family == CHIP_RS880))
		WREG32(R_006C9C_DCP_CONTROL, 2);

	rs690_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[0], &wm0_high, false);
	rs690_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[1], &wm1_high, false);

	rs690_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[0], &wm0_low, true);
	rs690_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[1], &wm1_low, true);

	tmp = (wm0_high.lb_request_fifo_depth - 1);
	tmp |= (wm1_high.lb_request_fifo_depth - 1) << 16;
	WREG32(R_006D58_LB_MAX_REQ_OUTSTANDING, tmp);

	rs690_compute_mode_priority(rdev,
				    &wm0_high, &wm1_high,
				    mode0, mode1,
				    &d1mode_priority_a_cnt, &d2mode_priority_a_cnt);
	rs690_compute_mode_priority(rdev,
				    &wm0_low, &wm1_low,
				    mode0, mode1,
				    &d1mode_priority_b_cnt, &d2mode_priority_b_cnt);

	WREG32(R_006548_D1MODE_PRIORITY_A_CNT, d1mode_priority_a_cnt);
	WREG32(R_00654C_D1MODE_PRIORITY_B_CNT, d1mode_priority_b_cnt);
	WREG32(R_006D48_D2MODE_PRIORITY_A_CNT, d2mode_priority_a_cnt);
	WREG32(R_006D4C_D2MODE_PRIORITY_B_CNT, d2mode_priority_b_cnt);
}

uint32_t rs690_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	unsigned long flags;
	uint32_t r;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_000078_MC_INDEX, S_000078_MC_IND_ADDR(reg));
	r = RREG32(R_00007C_MC_DATA);
	WREG32(R_000078_MC_INDEX, ~C_000078_MC_IND_ADDR);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
	return r;
}

void rs690_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_000078_MC_INDEX, S_000078_MC_IND_ADDR(reg) |
		S_000078_MC_IND_WR_EN(1));
	WREG32(R_00007C_MC_DATA, v);
	WREG32(R_000078_MC_INDEX, 0x7F);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
}

static void rs690_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;

	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (rs690_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");
	/* Program MC, should be a 32bits limited address space */
	WREG32_MC(R_000100_MCCFG_FB_LOCATION,
			S_000100_MC_FB_START(rdev->mc.vram_start >> 16) |
			S_000100_MC_FB_TOP(rdev->mc.vram_end >> 16));
	WREG32(R_000134_HDP_FB_LOCATION,
		S_000134_HDP_FB_START(rdev->mc.vram_start >> 16));

	rv515_mc_resume(rdev, &save);
}

static int rs690_startup(struct radeon_device *rdev)
{
	int r;

	rs690_mc_program(rdev);
	/* Resume clock */
	rv515_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	rs690_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r = rs400_gart_enable(rdev);
	if (r)
		return r;

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	rs600_irq_set(rdev);
	rdev->config.r300.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP (%d).\n", r);
		return r;
	}

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_audio_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing audio\n");
		return r;
	}

	return 0;
}

int rs690_resume(struct radeon_device *rdev)
{
	int r;

	/* Make sur GART are not working */
	rs400_gart_disable(rdev);
	/* Resume clock before doing reset */
	rv515_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Resume clock after posting */
	rv515_clock_startup(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);

	rdev->accel_working = true;
	r = rs690_startup(rdev);
	if (r) {
		rdev->accel_working = false;
	}
	return r;
}

int rs690_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	radeon_audio_fini(rdev);
	r100_cp_disable(rdev);
	radeon_wb_disable(rdev);
	rs600_irq_disable(rdev);
	rs400_gart_disable(rdev);
	return 0;
}

void rs690_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	radeon_audio_fini(rdev);
	r100_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_gem_fini(rdev);
	rs400_gart_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int rs690_init(struct radeon_device *rdev)
{
	int r;

	/* Disable VGA */
	rv515_vga_render_disable(rdev);
	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* restore some register to sane defaults */
	r100_restore_sanity(rdev);
	/* TODO: disable VGA need to use VGA request */
	/* BIOS*/
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		r = radeon_atombios_init(rdev);
		if (r)
			return r;
	} else {
		dev_err(rdev->dev, "Expecting atombios for RV515 GPU\n");
		return -EINVAL;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (radeon_boot_test_post_card(rdev) == false)
		return -EINVAL;

	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* initialize memory controller */
	rs690_mc_init(rdev);
	rv515_debugfs(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;
	r = rs400_gart_init(rdev);
	if (r)
		return r;
	rs600_set_safe_registers(rdev);

	/* Initialize power management */
	radeon_pm_init(rdev);

	rdev->accel_working = true;
	r = rs690_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		rs400_gart_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
