/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "display_mode_support.h"
#include "display_mode_lib.h"

int dml_ms_check(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		int num_pipes)
{
	struct _vcs_dpi_ip_params_st *ip;
	struct _vcs_dpi_soc_bounding_box_st *soc;
	struct _vcs_dpi_mode_evaluation_st *me;
	struct dml_ms_internal_vars *v;
	int num_planes, i, j, ij, k, ijk;

	ip = &(mode_lib->ip);
	soc = &(mode_lib->soc);
	me = &(mode_lib->me);
	v = &(mode_lib->vars);
	num_planes = dml_wm_e2e_to_wm(mode_lib, e2e, num_pipes, v->planes);

	//instantiating variables to zero
	v->MacroTileBlockWidthC = 0;
	v->SwathWidthGranularityC = 0;

	v->DCFCLKPerState[5] = 0;
	v->DCFCLKPerState[4] = 0;
	v->DCFCLKPerState[3] = 0;
	v->DCFCLKPerState[2] = 0;
	v->DCFCLKPerState[1] = 0;
	v->DCFCLKPerState[0] = 0;

	if (soc->vmin.dcfclk_mhz > 0) {
		v->DCFCLKPerState[5] = soc->vmin.dcfclk_mhz;
		v->DCFCLKPerState[4] = soc->vmin.dcfclk_mhz;
		v->DCFCLKPerState[3] = soc->vmin.dcfclk_mhz;
		v->DCFCLKPerState[2] = soc->vmin.dcfclk_mhz;
		v->DCFCLKPerState[1] = soc->vmin.dcfclk_mhz;
		v->DCFCLKPerState[0] = soc->vmin.dcfclk_mhz;
	}

	if (soc->vmid.dcfclk_mhz > 0) {
		v->DCFCLKPerState[5] = soc->vmid.dcfclk_mhz;
		v->DCFCLKPerState[4] = soc->vmid.dcfclk_mhz;
		v->DCFCLKPerState[3] = soc->vmid.dcfclk_mhz;
		v->DCFCLKPerState[2] = soc->vmid.dcfclk_mhz;
		v->DCFCLKPerState[1] = soc->vmid.dcfclk_mhz;
	}

	if (soc->vnom.dcfclk_mhz > 0) {
		v->DCFCLKPerState[5] = soc->vnom.dcfclk_mhz;
		v->DCFCLKPerState[4] = soc->vnom.dcfclk_mhz;
		v->DCFCLKPerState[3] = soc->vnom.dcfclk_mhz;
		v->DCFCLKPerState[2] = soc->vnom.dcfclk_mhz;
	}

	if (soc->vmax.dcfclk_mhz > 0) {
		v->DCFCLKPerState[5] = soc->vmax.dcfclk_mhz;
		v->DCFCLKPerState[4] = soc->vmax.dcfclk_mhz;
		v->DCFCLKPerState[3] = soc->vmax.dcfclk_mhz;
	}

	v->FabricAndDRAMBandwidthPerState[5] = 0;
	v->FabricAndDRAMBandwidthPerState[4] = 0;
	v->FabricAndDRAMBandwidthPerState[3] = 0;
	v->FabricAndDRAMBandwidthPerState[2] = 0;
	v->FabricAndDRAMBandwidthPerState[1] = 0;
	v->FabricAndDRAMBandwidthPerState[0] = 0;

	if (soc->vmin.dram_bw_per_chan_gbps > 0) {
		v->FabricAndDRAMBandwidthPerState[5] = soc->vmin.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[4] = soc->vmin.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[3] = soc->vmin.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[2] = soc->vmin.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[1] = soc->vmin.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[0] = soc->vmin.dram_bw_per_chan_gbps;
	}

	if (soc->vmid.dram_bw_per_chan_gbps > 0) {
		v->FabricAndDRAMBandwidthPerState[5] = soc->vmid.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[4] = soc->vmid.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[3] = soc->vmid.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[2] = soc->vmid.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[1] = soc->vmid.dram_bw_per_chan_gbps;
	}

	if (soc->vnom.dram_bw_per_chan_gbps > 0) {
		v->FabricAndDRAMBandwidthPerState[5] = soc->vnom.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[4] = soc->vnom.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[3] = soc->vnom.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[2] = soc->vnom.dram_bw_per_chan_gbps;
	}

	if (soc->vmax.dram_bw_per_chan_gbps > 0) {
		v->FabricAndDRAMBandwidthPerState[5] = soc->vmax.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[4] = soc->vmax.dram_bw_per_chan_gbps;
		v->FabricAndDRAMBandwidthPerState[3] = soc->vmax.dram_bw_per_chan_gbps;
	}

	v->PHYCLKPerState[5] = 0;
	v->PHYCLKPerState[4] = 0;
	v->PHYCLKPerState[3] = 0;
	v->PHYCLKPerState[2] = 0;
	v->PHYCLKPerState[1] = 0;
	v->PHYCLKPerState[0] = 0;

	if (soc->vmin.phyclk_mhz > 0) {
		v->PHYCLKPerState[5] = soc->vmin.phyclk_mhz;
		v->PHYCLKPerState[4] = soc->vmin.phyclk_mhz;
		v->PHYCLKPerState[3] = soc->vmin.phyclk_mhz;
		v->PHYCLKPerState[2] = soc->vmin.phyclk_mhz;
		v->PHYCLKPerState[1] = soc->vmin.phyclk_mhz;
		v->PHYCLKPerState[0] = soc->vmin.phyclk_mhz;
	}

	if (soc->vmid.phyclk_mhz > 0) {
		v->PHYCLKPerState[5] = soc->vmid.phyclk_mhz;
		v->PHYCLKPerState[4] = soc->vmid.phyclk_mhz;
		v->PHYCLKPerState[3] = soc->vmid.phyclk_mhz;
		v->PHYCLKPerState[2] = soc->vmid.phyclk_mhz;
		v->PHYCLKPerState[1] = soc->vmid.phyclk_mhz;
	}

	if (soc->vnom.phyclk_mhz > 0) {
		v->PHYCLKPerState[5] = soc->vnom.phyclk_mhz;
		v->PHYCLKPerState[4] = soc->vnom.phyclk_mhz;
		v->PHYCLKPerState[3] = soc->vnom.phyclk_mhz;
		v->PHYCLKPerState[2] = soc->vnom.phyclk_mhz;
	}

	if (soc->vmax.phyclk_mhz > 0) {
		v->PHYCLKPerState[5] = soc->vmax.phyclk_mhz;
		v->PHYCLKPerState[4] = soc->vmax.phyclk_mhz;
		v->PHYCLKPerState[3] = soc->vmax.phyclk_mhz;
	}

	v->MaxDispclk[5] = 0;
	v->MaxDispclk[4] = 0;
	v->MaxDispclk[3] = 0;
	v->MaxDispclk[2] = 0;
	v->MaxDispclk[1] = 0;
	v->MaxDispclk[0] = 0;

	if (soc->vmin.dispclk_mhz > 0) {
		v->MaxDispclk[5] = soc->vmin.dispclk_mhz;
		v->MaxDispclk[4] = soc->vmin.dispclk_mhz;
		v->MaxDispclk[3] = soc->vmin.dispclk_mhz;
		v->MaxDispclk[2] = soc->vmin.dispclk_mhz;
		v->MaxDispclk[1] = soc->vmin.dispclk_mhz;
		v->MaxDispclk[0] = soc->vmin.dispclk_mhz;
	}

	if (soc->vmid.dispclk_mhz > 0) {
		v->MaxDispclk[5] = soc->vmid.dispclk_mhz;
		v->MaxDispclk[4] = soc->vmid.dispclk_mhz;
		v->MaxDispclk[3] = soc->vmid.dispclk_mhz;
		v->MaxDispclk[2] = soc->vmid.dispclk_mhz;
		v->MaxDispclk[1] = soc->vmid.dispclk_mhz;
	}

	if (soc->vnom.dispclk_mhz > 0) {
		v->MaxDispclk[5] = soc->vnom.dispclk_mhz;
		v->MaxDispclk[4] = soc->vnom.dispclk_mhz;
		v->MaxDispclk[3] = soc->vnom.dispclk_mhz;
		v->MaxDispclk[2] = soc->vnom.dispclk_mhz;
	}

	if (soc->vmax.dispclk_mhz > 0) {
		v->MaxDispclk[5] = soc->vmax.dispclk_mhz;
		v->MaxDispclk[4] = soc->vmax.dispclk_mhz;
		v->MaxDispclk[3] = soc->vmax.dispclk_mhz;
	}

	v->MaxDppclk[5] = 0;
	v->MaxDppclk[4] = 0;
	v->MaxDppclk[3] = 0;
	v->MaxDppclk[2] = 0;
	v->MaxDppclk[1] = 0;
	v->MaxDppclk[0] = 0;

	if (soc->vmin.dppclk_mhz > 0) {
		v->MaxDppclk[5] = soc->vmin.dppclk_mhz;
		v->MaxDppclk[4] = soc->vmin.dppclk_mhz;
		v->MaxDppclk[3] = soc->vmin.dppclk_mhz;
		v->MaxDppclk[2] = soc->vmin.dppclk_mhz;
		v->MaxDppclk[1] = soc->vmin.dppclk_mhz;
		v->MaxDppclk[0] = soc->vmin.dppclk_mhz;
	}

	if (soc->vmid.dppclk_mhz > 0) {
		v->MaxDppclk[5] = soc->vmid.dppclk_mhz;
		v->MaxDppclk[4] = soc->vmid.dppclk_mhz;
		v->MaxDppclk[3] = soc->vmid.dppclk_mhz;
		v->MaxDppclk[2] = soc->vmid.dppclk_mhz;
		v->MaxDppclk[1] = soc->vmid.dppclk_mhz;
	}

	if (soc->vnom.dppclk_mhz > 0) {
		v->MaxDppclk[5] = soc->vnom.dppclk_mhz;
		v->MaxDppclk[4] = soc->vnom.dppclk_mhz;
		v->MaxDppclk[3] = soc->vnom.dppclk_mhz;
		v->MaxDppclk[2] = soc->vnom.dppclk_mhz;
	}

	if (soc->vmax.dppclk_mhz > 0) {
		v->MaxDppclk[5] = soc->vmax.dppclk_mhz;
		v->MaxDppclk[4] = soc->vmax.dppclk_mhz;
		v->MaxDppclk[3] = soc->vmax.dppclk_mhz;
	}

	if (me->voltage_override == dm_vmax) {
		v->VoltageOverrideLevel = NumberOfStates - 1;
	} else if (me->voltage_override == dm_vnom) {
		v->VoltageOverrideLevel = NumberOfStates - 2;
	} else if (me->voltage_override == dm_vmid) {
		v->VoltageOverrideLevel = NumberOfStates - 3;
	} else {
		v->VoltageOverrideLevel = 0;
	}

	// Scale Ratio Support Check

	v->ScaleRatioSupport = 1;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
				e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
		struct _vcs_dpi_scaler_taps_st scale_taps =
				e2e[v->planes[k].e2e_index].pipe.scale_taps;
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;

		if (scale_ratio_depth.hscl_ratio > ip->max_hscl_ratio
				|| scale_ratio_depth.vscl_ratio > ip->max_vscl_ratio
				|| scale_ratio_depth.hscl_ratio > scale_taps.htaps
				|| scale_ratio_depth.vscl_ratio > scale_taps.vtaps
				|| (src.source_format != dm_444_64 && src.source_format != dm_444_32
						&& src.source_format != dm_444_16
						&& ((scale_ratio_depth.hscl_ratio / 2
								> scale_taps.htaps_c)
								|| (scale_ratio_depth.vscl_ratio / 2
										> scale_taps.vtaps_c))))

				{
			v->ScaleRatioSupport = 0;
		}
	}

	// Source Format, Pixel Format and Scan Support Check

	v->SourceFormatPixelAndScanSupport = 1;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;

		if ((src.sw_mode == dm_sw_linear && src.source_scan != dm_horz)
				|| ((src.sw_mode == dm_sw_4kb_d || src.sw_mode == dm_sw_4kb_d_x
						|| src.sw_mode == dm_sw_64kb_d
						|| src.sw_mode == dm_sw_64kb_d_t
						|| src.sw_mode == dm_sw_64kb_d_x
						|| src.sw_mode == dm_sw_var_d
						|| src.sw_mode == dm_sw_var_d_x)
						&& (src.source_format != dm_444_64))) {
			v->SourceFormatPixelAndScanSupport = 0;
		}
	}

	// Bandwidth Support Check

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;

		if (src.source_scan == dm_horz) {
			v->SwathWidthYSingleDPP[k] = src.viewport_width;
		} else {
			v->SwathWidthYSingleDPP[k] = src.viewport_height;
		}

		if (src.source_format == dm_444_64) {
			v->BytePerPixelInDETY[k] = 8;
			v->BytePerPixelInDETC[k] = 0;
		} else if (src.source_format == dm_444_32) {
			v->BytePerPixelInDETY[k] = 4;
			v->BytePerPixelInDETC[k] = 0;
		} else if (src.source_format == dm_444_16) {
			v->BytePerPixelInDETY[k] = 2;
			v->BytePerPixelInDETC[k] = 0;
		} else if (src.source_format == dm_420_8) {
			v->BytePerPixelInDETY[k] = 1;
			v->BytePerPixelInDETC[k] = 2;
		} else {
			v->BytePerPixelInDETY[k] = 4.00 / 3.00;
			v->BytePerPixelInDETC[k] = 8.00 / 3.00;
		}
	}

	v->TotalReadBandwidthConsumedGBytePerSecond = 0;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;
		struct _vcs_dpi_display_pipe_dest_params_st dest =
				e2e[v->planes[k].e2e_index].pipe.dest;
		struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
				e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;

		v->ReadBandwidth[k] =
				v->SwathWidthYSingleDPP[k]
						* (dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
								* scale_ratio_depth.vscl_ratio
								+ (dml_ceil_ex(
										v->BytePerPixelInDETC[k],
										2) / 2)
										* (scale_ratio_depth.vscl_ratio
												/ 2))
						/ (dest.htotal / dest.pixel_rate_mhz);

		if (src.dcc == 1) {
			v->ReadBandwidth[k] = v->ReadBandwidth[k] * (1 + 1 / 256);
		}

		if (ip->pte_enable == 1 && src.source_scan != dm_horz
				&& (src.sw_mode == dm_sw_4kb_s || src.sw_mode == dm_sw_4kb_s_x
						|| src.sw_mode == dm_sw_4kb_d
						|| src.sw_mode == dm_sw_4kb_d_x)) {
			v->ReadBandwidth[k] = v->ReadBandwidth[k] * (1 + 1 / 64);
		} else if (ip->pte_enable == 1 && src.source_scan == dm_horz
				&& (src.source_format == dm_444_64 || src.source_format == dm_444_32)
				&& (src.sw_mode == dm_sw_64kb_s || src.sw_mode == dm_sw_64kb_s_t
						|| src.sw_mode == dm_sw_64kb_s_x
						|| src.sw_mode == dm_sw_64kb_d
						|| src.sw_mode == dm_sw_64kb_d_t
						|| src.sw_mode == dm_sw_64kb_d_x)) {
			v->ReadBandwidth[k] = v->ReadBandwidth[k] * (1 + 1 / 256);
		} else if (ip->pte_enable == 1) {
			v->ReadBandwidth[k] = v->ReadBandwidth[k] * (1 + 1 / 512);
		}

		v->TotalReadBandwidthConsumedGBytePerSecond =
				v->TotalReadBandwidthConsumedGBytePerSecond
						+ v->ReadBandwidth[k] / 1000;
	}

	v->TotalWriteBandwidthConsumedGBytePerSecond = 0;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_dest_params_st dest =
				e2e[v->planes[k].e2e_index].pipe.dest;
		struct _vcs_dpi_display_output_params_st dout = e2e[v->planes[k].e2e_index].dout;

		if (dout.output_type == dm_wb && dout.output_format == dm_444) {
			v->WriteBandwidth[k] = dest.recout_width
					/ (dest.htotal / dest.pixel_rate_mhz) * 4;
		} else if (dout.output_type == dm_wb) {
			v->WriteBandwidth[k] = dest.recout_width
					/ (dest.htotal / dest.pixel_rate_mhz) * 1.5;
		} else {
			v->WriteBandwidth[k] = 0;
		}

		v->TotalWriteBandwidthConsumedGBytePerSecond =
				v->TotalWriteBandwidthConsumedGBytePerSecond
						+ v->WriteBandwidth[k] / 1000;
	}

	v->TotalBandwidthConsumedGBytePerSecond = v->TotalReadBandwidthConsumedGBytePerSecond
			+ v->TotalWriteBandwidthConsumedGBytePerSecond;

	v->DCCEnabledInAnyPlane = 0;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;

		if (src.dcc == 1) {
			v->DCCEnabledInAnyPlane = 1;
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		v->ReturnBWToDCNPerState = dml_min(
				soc->return_bus_width_bytes * v->DCFCLKPerState[i],
				v->FabricAndDRAMBandwidthPerState[i] * 1000
						* soc->ideal_dram_bw_after_urgent_percent / 100);

		v->ReturnBWPerState[i] = v->ReturnBWToDCNPerState;

		if (v->DCCEnabledInAnyPlane == 1
				&& v->ReturnBWToDCNPerState
						> (v->DCFCLKPerState[i]
								* soc->return_bus_width_bytes / 4)) {
			v->ReturnBWPerState[i] =
					dml_min(
							v->ReturnBWPerState[i],
							v->ReturnBWToDCNPerState * 4
									* (1
											- soc->urgent_latency_us
													/ ((ip->rob_buffer_size_kbytes
															- ip->pixel_chunk_size_kbytes)
															* 1024
															/ (v->ReturnBWToDCNPerState
																	- v->DCFCLKPerState[i]
																			* soc->return_bus_width_bytes
																			/ 4)
															+ soc->urgent_latency_us)));
		}

		v->CriticalPoint = 2 * soc->return_bus_width_bytes * v->DCFCLKPerState[i]
				* soc->urgent_latency_us
				/ (v->ReturnBWToDCNPerState * soc->urgent_latency_us
						+ (ip->rob_buffer_size_kbytes
								- ip->pixel_chunk_size_kbytes)
								* 1024);

		if (v->DCCEnabledInAnyPlane == 1 && v->CriticalPoint > 1 && v->CriticalPoint < 4) {
			v->ReturnBWPerState[i] =
					dml_min(
							v->ReturnBWPerState[i],
							4 * v->ReturnBWToDCNPerState
									* (ip->rob_buffer_size_kbytes
											- ip->pixel_chunk_size_kbytes)
									* 1024
									* soc->return_bus_width_bytes
									* v->DCFCLKPerState[i]
									* soc->urgent_latency_us
									/ dml_pow(
											(v->ReturnBWToDCNPerState
													* soc->urgent_latency_us
													+ (ip->rob_buffer_size_kbytes
															- ip->pixel_chunk_size_kbytes)
															* 1024),
											2));
		}

		v->ReturnBWToDCNPerState = dml_min(
				soc->return_bus_width_bytes * v->DCFCLKPerState[i],
				v->FabricAndDRAMBandwidthPerState[i] * 1000);

		if (v->DCCEnabledInAnyPlane == 1
				&& v->ReturnBWToDCNPerState
						> (v->DCFCLKPerState[i]
								* soc->return_bus_width_bytes / 4)) {
			v->ReturnBWPerState[i] =
					dml_min(
							v->ReturnBWPerState[i],
							v->ReturnBWToDCNPerState * 4
									* (1
											- soc->urgent_latency_us
													/ ((ip->rob_buffer_size_kbytes
															- ip->pixel_chunk_size_kbytes)
															* 1024
															/ (v->ReturnBWToDCNPerState
																	- v->DCFCLKPerState[i]
																			* soc->return_bus_width_bytes
																			/ 4)
															+ soc->urgent_latency_us)));
		}

		v->CriticalPoint = 2 * soc->return_bus_width_bytes * v->DCFCLKPerState[i]
				* soc->urgent_latency_us
				/ (v->ReturnBWToDCNPerState * soc->urgent_latency_us
						+ (ip->rob_buffer_size_kbytes
								- ip->pixel_chunk_size_kbytes)
								* 1024);

		if (v->DCCEnabledInAnyPlane == 1 && v->CriticalPoint > 1 && v->CriticalPoint < 4) {
			v->ReturnBWPerState[i] =
					dml_min(
							v->ReturnBWPerState[i],
							4 * v->ReturnBWToDCNPerState
									* (ip->rob_buffer_size_kbytes
											- ip->pixel_chunk_size_kbytes)
									* 1024
									* soc->return_bus_width_bytes
									* v->DCFCLKPerState[i]
									* soc->urgent_latency_us
									/ dml_pow(
											(v->ReturnBWToDCNPerState
													* soc->urgent_latency_us
													+ (ip->rob_buffer_size_kbytes
															- ip->pixel_chunk_size_kbytes)
															* 1024),
											2));
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		if ((v->TotalReadBandwidthConsumedGBytePerSecond * 1000 <= v->ReturnBWPerState[i])
				&& (v->TotalBandwidthConsumedGBytePerSecond * 1000
						<= v->FabricAndDRAMBandwidthPerState[i] * 1000
								* soc->ideal_dram_bw_after_urgent_percent
								/ 100)) {
			v->BandwidthSupport[i] = 1;
		} else {
			v->BandwidthSupport[i] = 0;
		}
	}

	// Writeback Latency support check

	v->WritebackLatencySupport = 1;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_dest_params_st dest =
				e2e[v->planes[k].e2e_index].pipe.dest;
		struct _vcs_dpi_display_output_params_st dout = e2e[v->planes[k].e2e_index].dout;

		if (dout.output_type == dm_wb && dout.output_format == dm_444
				&& (dest.recout_width / (dest.htotal / dest.pixel_rate_mhz) * 4)
						> ((ip->writeback_luma_buffer_size_kbytes
								+ ip->writeback_chroma_buffer_size_kbytes)
								* 1024 / soc->writeback_latency_us)) {
			v->WritebackLatencySupport = 0;
		} else if (dout.output_type == dm_wb
				&& (dest.recout_width / (dest.htotal / dest.pixel_rate_mhz))
						> (dml_min(
								ip->writeback_luma_buffer_size_kbytes,
								2
										* ip->writeback_chroma_buffer_size_kbytes)
								* 1024 / soc->writeback_latency_us)) {
			v->WritebackLatencySupport = 0;
		}
	}

	// Re-ordering Buffer Support Check

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		v->UrgentRoundTripAndOutOfOrderLatencyPerState[i] =
				(soc->round_trip_ping_latency_dcfclk_cycles + 32)
						/ v->DCFCLKPerState[i]
						+ soc->urgent_out_of_order_return_per_channel_bytes
								* soc->num_chans
								/ v->ReturnBWPerState[i];

		if ((ip->rob_buffer_size_kbytes - ip->pixel_chunk_size_kbytes) * 1024
				/ v->ReturnBWPerState[i]
				> v->UrgentRoundTripAndOutOfOrderLatencyPerState[i]) {
			v->ROBSupport[i] = 1;
		} else {
			v->ROBSupport[i] = 0;
		}
	}

	// Display IO Support Check

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_dest_params_st dest =
				e2e[v->planes[k].e2e_index].pipe.dest;
		struct _vcs_dpi_display_output_params_st dout = e2e[v->planes[k].e2e_index].dout;

		if (dout.output_format == dm_420) {
			v->RequiredOutputBW = dest.pixel_rate_mhz * 3 / 2;
		} else {
			v->RequiredOutputBW = dest.pixel_rate_mhz * 3;
		}

		if (dout.output_type == dm_hdmi) {
			v->RequiredPHYCLK[k] = v->RequiredOutputBW / 3;
		} else if (dout.output_type == dm_dp) {
			v->RequiredPHYCLK[k] = v->RequiredOutputBW / 4;
		} else {
			v->RequiredPHYCLK[k] = 0;
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		v->DIOSupport[i] = 1;

		for (k = 0; k < num_planes; k++) {
			struct _vcs_dpi_display_output_params_st dout =
					e2e[v->planes[k].e2e_index].dout;

			if ((v->RequiredPHYCLK[k] > v->PHYCLKPerState[i])
					|| (dout.output_type == dm_hdmi
							&& v->RequiredPHYCLK[k] > 600)) {
				v->DIOSupport[i] = 0;
			}
		}
	}

	// Total Available Writeback Support Check

	v->TotalNumberOfActiveWriteback = 0;

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_output_params_st dout = e2e[v->planes[k].e2e_index].dout;

		if (dout.output_type == dm_wb) {
			v->TotalNumberOfActiveWriteback = v->TotalNumberOfActiveWriteback + 1;
		}
	}

	if (v->TotalNumberOfActiveWriteback <= ip->max_num_wb) {
		v->TotalAvailableWritebackSupport = 1;
	} else {
		v->TotalAvailableWritebackSupport = 0;
	}

	// Maximum DISPCLK/DPPCLK Support check

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_dest_params_st dest =
				e2e[v->planes[k].e2e_index].pipe.dest;
		struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
				e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
		struct _vcs_dpi_scaler_taps_st scale_taps =
				e2e[v->planes[k].e2e_index].pipe.scale_taps;

		if (scale_ratio_depth.hscl_ratio > 1) {
			v->PSCL_FACTOR[k] = dml_min(
					ip->max_dchub_pscl_bw_pix_per_clk,
					ip->max_pscl_lb_bw_pix_per_clk
							* scale_ratio_depth.hscl_ratio
							/ dml_ceil_ex(scale_taps.htaps / 6, 1));
		} else {
			v->PSCL_FACTOR[k] = dml_min(
					ip->max_dchub_pscl_bw_pix_per_clk,
					ip->max_pscl_lb_bw_pix_per_clk);
		}

		if (v->BytePerPixelInDETC[k] == 0) {
			v->PSCL_FACTOR_CHROMA[k] = 0;
			v->MinDPPCLKUsingSingleDPP[k] =
					dest.pixel_rate_mhz
							* dml_max(
									scale_taps.vtaps / 6
											* dml_min(
													1,
													scale_ratio_depth.hscl_ratio),
									dml_max(
											scale_ratio_depth.hscl_ratio
													* scale_ratio_depth.vscl_ratio
													/ v->PSCL_FACTOR[k],
											1));

		} else {
			if (scale_ratio_depth.hscl_ratio / 2 > 1) {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(
						ip->max_dchub_pscl_bw_pix_per_clk,
						ip->max_pscl_lb_bw_pix_per_clk
								* scale_ratio_depth.hscl_ratio / 2
								/ dml_ceil_ex(
										scale_taps.htaps_c
												/ 6,
										1));
			} else {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(
						ip->max_dchub_pscl_bw_pix_per_clk,
						ip->max_pscl_lb_bw_pix_per_clk);
			}
			v->MinDPPCLKUsingSingleDPP[k] =
					dest.pixel_rate_mhz
							* dml_max(
									dml_max(
											scale_taps.vtaps
													/ 6
													* dml_min(
															1,
															scale_ratio_depth.hscl_ratio),
											scale_ratio_depth.hscl_ratio
													* scale_ratio_depth.vscl_ratio
													/ v->PSCL_FACTOR[k]),
									dml_max(
											dml_max(
													scale_taps.vtaps_c
															/ 6
															* dml_min(
																	1,
																	scale_ratio_depth.hscl_ratio
																			/ 2),
													scale_ratio_depth.hscl_ratio
															* scale_ratio_depth.vscl_ratio
															/ 4
															/ v->PSCL_FACTOR_CHROMA[k]),
											1));

		}
	}

	for (k = 0; k < num_planes; k++) {
		struct _vcs_dpi_display_pipe_source_params_st src =
				e2e[v->planes[k].e2e_index].pipe.src;
		struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
				e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
		struct _vcs_dpi_scaler_taps_st scale_taps =
				e2e[v->planes[k].e2e_index].pipe.scale_taps;

		if (src.source_format == dm_444_64 || src.source_format == dm_444_32
				|| src.source_format == dm_444_16) {
			if (src.sw_mode == dm_sw_linear) {
				v->Read256BlockHeightY[k] = 1;
			} else if (src.source_format == dm_444_64) {
				v->Read256BlockHeightY[k] = 4;
			} else {
				v->Read256BlockHeightY[k] = 8;
			}

			v->Read256BlockWidthY[k] = 256 / dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
					/ v->Read256BlockHeightY[k];
			v->Read256BlockHeightC[k] = 0;
			v->Read256BlockWidthC[k] = 0;
		} else {
			if (src.sw_mode == dm_sw_linear) {
				v->Read256BlockHeightY[k] = 1;
				v->Read256BlockHeightC[k] = 1;
			} else if (src.source_format == dm_420_8) {
				v->Read256BlockHeightY[k] = 16;
				v->Read256BlockHeightC[k] = 8;
			} else {
				v->Read256BlockHeightY[k] = 8;
				v->Read256BlockHeightC[k] = 8;
			}

			v->Read256BlockWidthY[k] = 256 / dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
					/ v->Read256BlockHeightY[k];
			v->Read256BlockWidthC[k] = 256 / dml_ceil_ex(v->BytePerPixelInDETC[k], 2)
					/ v->Read256BlockHeightC[k];
		}

		if (src.source_scan == dm_horz) {
			v->MaxSwathHeightY[k] = v->Read256BlockHeightY[k];
			v->MaxSwathHeightC[k] = v->Read256BlockHeightC[k];
		} else {
			v->MaxSwathHeightY[k] = v->Read256BlockWidthY[k];
			v->MaxSwathHeightC[k] = v->Read256BlockWidthC[k];
		}

		if (src.source_format == dm_444_64 || src.source_format == dm_444_32
				|| src.source_format == dm_444_16) {
			if (src.sw_mode == dm_sw_linear
					|| (src.source_format == dm_444_64
							&& (src.sw_mode == dm_sw_4kb_s
									|| src.sw_mode
											== dm_sw_4kb_s_x
									|| src.sw_mode
											== dm_sw_64kb_s
									|| src.sw_mode
											== dm_sw_64kb_s_t
									|| src.sw_mode
											== dm_sw_64kb_s_x
									|| src.sw_mode
											== dm_sw_var_s
									|| src.sw_mode
											== dm_sw_var_s_x)
							&& src.source_scan == dm_horz)) {
				v->MinSwathHeightY[k] = v->MaxSwathHeightY[k];
			} else {
				v->MinSwathHeightY[k] = v->MaxSwathHeightY[k] / 2;
			}
			v->MinSwathHeightC[k] = v->MaxSwathHeightC[k];
		} else {
			if (src.sw_mode == dm_sw_linear) {
				v->MinSwathHeightY[k] = v->MaxSwathHeightY[k];
				v->MinSwathHeightC[k] = v->MaxSwathHeightC[k];
			} else if (src.source_format == dm_420_8 && src.source_scan == dm_horz) {
				v->MinSwathHeightY[k] = v->MaxSwathHeightY[k] / 2;
				if (ip->bug_forcing_LC_req_same_size_fixed == 1) {
					v->MinSwathHeightC[k] = v->MaxSwathHeightC[k];
				} else {
					v->MinSwathHeightC[k] = v->MaxSwathHeightC[k] / 2;
				}
			} else if (src.source_format == dm_420_10 && src.source_scan == dm_horz) {
				v->MinSwathHeightC[k] = v->MaxSwathHeightC[k] / 2;
				if (ip->bug_forcing_LC_req_same_size_fixed == 1) {
					v->MinSwathHeightY[k] = v->MaxSwathHeightY[k];
				} else {
					v->MinSwathHeightY[k] = v->MaxSwathHeightY[k] / 2;
				}
			} else {
				v->MinSwathHeightY[k] = v->MaxSwathHeightY[k];
				v->MinSwathHeightC[k] = v->MaxSwathHeightC[k];
			}
		}

		if (src.sw_mode == dm_sw_linear) {
			v->MaximumSwathWidth = 8192;
		} else {
			v->MaximumSwathWidth = 5120;
		}

		v->NumberOfDPPRequiredForDETSize =
				dml_ceil_ex(
						v->SwathWidthYSingleDPP[k]
								/ dml_min(
										v->MaximumSwathWidth,
										ip->det_buffer_size_kbytes
												* 1024
												/ 2
												/ (v->BytePerPixelInDETY[k]
														* v->MinSwathHeightY[k]
														+ v->BytePerPixelInDETC[k]
																/ 2
																* v->MinSwathHeightC[k])),
						1);

		if (v->BytePerPixelInDETC[k] == 0) {
			v->NumberOfDPPRequiredForLBSize =
					dml_ceil_ex(
							(scale_taps.vtaps
									+ dml_max(
											dml_ceil_ex(
													scale_ratio_depth.vscl_ratio,
													1)
													- 2,
											0))
									* v->SwathWidthYSingleDPP[k]
									/ dml_max(
											scale_ratio_depth.hscl_ratio,
											1)
									* scale_ratio_depth.lb_depth
									/ ip->line_buffer_size_bits,
							1);
		} else {
			v->NumberOfDPPRequiredForLBSize =
					dml_max(
							dml_ceil_ex(
									(scale_taps.vtaps
											+ dml_max(
													dml_ceil_ex(
															scale_ratio_depth.vscl_ratio,
															1)
															- 2,
													0))
											* v->SwathWidthYSingleDPP[k]
											/ dml_max(
													scale_ratio_depth.hscl_ratio,
													1)
											* scale_ratio_depth.lb_depth
											/ ip->line_buffer_size_bits,
									1),
							dml_ceil_ex(
									(scale_taps.vtaps_c
											+ dml_max(
													dml_ceil_ex(
															scale_ratio_depth.vscl_ratio
																	/ 2,
															1)
															- 2,
													0))
											* v->SwathWidthYSingleDPP[k]
											/ 2
											/ dml_max(
													scale_ratio_depth.hscl_ratio
															/ 2,
													1)
											* scale_ratio_depth.lb_depth
											/ ip->line_buffer_size_bits,
									1));
		}

		v->NumberOfDPPRequiredForDETAndLBSize[k] = dml_max(
				v->NumberOfDPPRequiredForDETSize,
				v->NumberOfDPPRequiredForLBSize);

	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i] = 0;
			v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] = 0;
			v->DISPCLK_DPPCLK_Support[j * NumberOfStatesPlusTwo + i] = 1;

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				v->MinDispclkUsingSingleDPP = dml_max(
						dest.pixel_rate_mhz,
						v->MinDPPCLKUsingSingleDPP[k] * (j + 1))
						* (1 + soc->downspread_percent / 100);
				v->MinDispclkUsingDualDPP = dml_max(
						dest.pixel_rate_mhz,
						v->MinDPPCLKUsingSingleDPP[k] / 2 * (j + 1))
						* (1 + soc->downspread_percent / 100);

				if (i < NumberOfStates) {
					v->MinDispclkUsingSingleDPP =
							v->MinDispclkUsingSingleDPP
									* (1
											+ ip->dispclk_ramp_margin_percent
													/ 100);
					v->MinDispclkUsingDualDPP =
							v->MinDispclkUsingDualDPP
									* (1
											+ ip->dispclk_ramp_margin_percent
													/ 100);
				}

				if (v->MinDispclkUsingSingleDPP
						<= dml_min(
								v->MaxDispclk[i],
								(j + 1) * v->MaxDppclk[i])
						&& v->NumberOfDPPRequiredForDETAndLBSize[k] <= 1) {
					v->NoOfDPP[ijk] = 1;
					v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] = dml_max(
							v->RequiredDISPCLK[j * NumberOfStatesPlusTwo
									+ i],
							v->MinDispclkUsingSingleDPP);
				} else if (v->MinDispclkUsingDualDPP
						<= dml_min(
								v->MaxDispclk[i],
								(j + 1) * v->MaxDppclk[i])) {
					v->NoOfDPP[ijk] = 2;
					v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] = dml_max(
							v->RequiredDISPCLK[j * NumberOfStatesPlusTwo
									+ i],
							v->MinDispclkUsingDualDPP);
				} else {
					v->NoOfDPP[ijk] = 2;
					v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] = dml_max(
							v->RequiredDISPCLK[j * NumberOfStatesPlusTwo
									+ i],
							v->MinDispclkUsingDualDPP);
					v->DISPCLK_DPPCLK_Support[j * NumberOfStatesPlusTwo + i] =
							0;
				}

				v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i] =
						v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo
								+ i] + v->NoOfDPP[ijk];
			}

			if (v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i]
					> ip->max_num_dpp) {
				v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i] = 0;
				v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] = 0;
				v->DISPCLK_DPPCLK_Support[j * NumberOfStatesPlusTwo + i] = 1;

				for (k = 0; k < num_planes; k++) {
					struct _vcs_dpi_display_pipe_dest_params_st dest =
							e2e[v->planes[k].e2e_index].pipe.dest;
					ijk = k * 2 * NumberOfStatesPlusTwo
							+ j * NumberOfStatesPlusTwo + i;

					v->MinDispclkUsingSingleDPP = dml_max(
							dest.pixel_rate_mhz,
							v->MinDPPCLKUsingSingleDPP[k] * (j + 1))
							* (1 + soc->downspread_percent / 100);
					v->MinDispclkUsingDualDPP = dml_max(
							dest.pixel_rate_mhz,
							v->MinDPPCLKUsingSingleDPP[k] / 2 * (j + 1))
							* (1 + soc->downspread_percent / 100);

					if (i < NumberOfStates) {
						v->MinDispclkUsingSingleDPP =
								v->MinDispclkUsingSingleDPP
										* (1
												+ ip->dispclk_ramp_margin_percent
														/ 100);
						v->MinDispclkUsingDualDPP =
								v->MinDispclkUsingDualDPP
										* (1
												+ ip->dispclk_ramp_margin_percent
														/ 100);
					}

					if (v->NumberOfDPPRequiredForDETAndLBSize[k] <= 1) {
						v->NoOfDPP[ijk] = 1;
						v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] =
								dml_max(
										v->RequiredDISPCLK[j
												* NumberOfStatesPlusTwo
												+ i],
										v->MinDispclkUsingSingleDPP);
						if (v->MinDispclkUsingSingleDPP
								> dml_min(
										v->MaxDispclk[i],
										(j + 1)
												* v->MaxDppclk[i])) {
							v->DISPCLK_DPPCLK_Support[j
									* NumberOfStatesPlusTwo + i] =
									0;
						}
					} else {
						v->NoOfDPP[ijk] = 2;
						v->RequiredDISPCLK[j * NumberOfStatesPlusTwo + i] =
								dml_max(
										v->RequiredDISPCLK[j
												* NumberOfStatesPlusTwo
												+ i],
										v->MinDispclkUsingDualDPP);
						if (v->MinDispclkUsingDualDPP
								> dml_min(
										v->MaxDispclk[i],
										(j + 1)
												* v->MaxDppclk[i])) {
							v->DISPCLK_DPPCLK_Support[j
									* NumberOfStatesPlusTwo + i] =
									0;
						}
					}
					v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i] =
							v->TotalNumberOfActiveDPP[j
									* NumberOfStatesPlusTwo + i]
									+ v->NoOfDPP[ijk];
				}
			}
		}
	}

	// Viewport Size Check

	v->ViewportSizeSupport = 1;

	for (k = 0; k < num_planes; k++) {
		if (v->NumberOfDPPRequiredForDETAndLBSize[k] > 2) {
			v->ViewportSizeSupport = 0;
		}
	}

	// Total Available Pipes Support Check

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			if (v->TotalNumberOfActiveDPP[j * NumberOfStatesPlusTwo + i]
					<= ip->max_num_dpp) {
				v->TotalAvailablePipesSupport[j * NumberOfStatesPlusTwo + i] = 1;
			} else {
				v->TotalAvailablePipesSupport[j * NumberOfStatesPlusTwo + i] = 0;
			}
		}
	}

	// Urgent Latency Support Check

	for (j = 0; j < 2; j++) {
		for (i = 0; i < NumberOfStatesPlusTwo; i++) {
			ij = j * NumberOfStatesPlusTwo + i;
			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
						e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
				struct _vcs_dpi_scaler_taps_st scale_taps =
						e2e[v->planes[k].e2e_index].pipe.scale_taps;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				v->SwathWidthYPerState[ijk] = v->SwathWidthYSingleDPP[k]
						/ v->NoOfDPP[ijk];

				v->SwathWidthGranularityY = 256
						/ dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
						/ v->MaxSwathHeightY[k];
				v->RoundedUpMaxSwathSizeBytesY = (dml_ceil_ex(
						v->SwathWidthYPerState[ijk] - 1,
						v->SwathWidthGranularityY)
						+ v->SwathWidthGranularityY)
						* v->BytePerPixelInDETY[k] * v->MaxSwathHeightY[k];
				if (src.source_format == dm_420_10) {
					v->RoundedUpMaxSwathSizeBytesY = dml_ceil_ex(
							v->RoundedUpMaxSwathSizeBytesY,
							256) + 256;
				}
				if (v->MaxSwathHeightC[k] > 0) {
					v->SwathWidthGranularityC = 256
							/ dml_ceil_ex(v->BytePerPixelInDETC[k], 2)
							/ v->MaxSwathHeightC[k];
				}
				v->RoundedUpMaxSwathSizeBytesC = (dml_ceil_ex(
						v->SwathWidthYPerState[ijk] / 2 - 1,
						v->SwathWidthGranularityC)
						+ v->SwathWidthGranularityC)
						* v->BytePerPixelInDETC[k] * v->MaxSwathHeightC[k];
				if (src.source_format == dm_420_10) {
					v->RoundedUpMaxSwathSizeBytesC = dml_ceil_ex(
							v->RoundedUpMaxSwathSizeBytesC,
							256) + 256;
				}

				if (v->RoundedUpMaxSwathSizeBytesY + v->RoundedUpMaxSwathSizeBytesC
						<= ip->det_buffer_size_kbytes * 1024 / 2) {
					v->SwathHeightYPerState[ijk] = v->MaxSwathHeightY[k];
					v->SwathHeightCPerState[ijk] = v->MaxSwathHeightC[k];
				} else {
					v->SwathHeightYPerState[ijk] = v->MinSwathHeightY[k];
					v->SwathHeightCPerState[ijk] = v->MinSwathHeightC[k];
				}

				if (v->BytePerPixelInDETC[k] == 0) {
					v->LinesInDETLuma = ip->det_buffer_size_kbytes * 1024
							/ v->BytePerPixelInDETY[k]
							/ v->SwathWidthYPerState[ijk];

					v->LinesInDETChroma = 0;
				} else if (v->SwathHeightYPerState[ijk]
						<= v->SwathHeightCPerState[ijk]) {
					v->LinesInDETLuma = ip->det_buffer_size_kbytes * 1024 / 2
							/ v->BytePerPixelInDETY[k]
							/ v->SwathWidthYPerState[ijk];
					v->LinesInDETChroma = ip->det_buffer_size_kbytes * 1024 / 2
							/ v->BytePerPixelInDETC[k]
							/ (v->SwathWidthYPerState[ijk] / 2);
				} else {
					v->LinesInDETLuma = ip->det_buffer_size_kbytes * 1024 * 2
							/ 3 / v->BytePerPixelInDETY[k]
							/ v->SwathWidthYPerState[ijk];
					v->LinesInDETChroma = ip->det_buffer_size_kbytes * 1024 / 3
							/ v->BytePerPixelInDETY[k]
							/ (v->SwathWidthYPerState[ijk] / 2);
				}

				v->EffectiveLBLatencyHidingSourceLinesLuma =
						dml_min(
								ip->max_line_buffer_lines,
								dml_floor_ex(
										ip->line_buffer_size_bits
												/ scale_ratio_depth.lb_depth
												/ (v->SwathWidthYPerState[ijk]
														/ dml_max(
																scale_ratio_depth.hscl_ratio,
																1)),
										1))
								- (scale_taps.vtaps - 1);

				v->EffectiveLBLatencyHidingSourceLinesChroma =
						dml_min(
								ip->max_line_buffer_lines,
								dml_floor_ex(
										ip->line_buffer_size_bits
												/ scale_ratio_depth.lb_depth
												/ (v->SwathWidthYPerState[ijk]
														/ 2
														/ dml_max(
																scale_ratio_depth.hscl_ratio
																		/ 2,
																1)),
										1))
								- (scale_taps.vtaps_c - 1);

				v->EffectiveDETLBLinesLuma =
						dml_floor_ex(
								v->LinesInDETLuma
										+ dml_min(
												v->LinesInDETLuma
														* v->RequiredDISPCLK[ij]
														* v->BytePerPixelInDETY[k]
														* v->PSCL_FACTOR[k]
														/ v->ReturnBWPerState[i],
												v->EffectiveLBLatencyHidingSourceLinesLuma),
								v->SwathHeightYPerState[ijk]);

				v->EffectiveDETLBLinesChroma =
						dml_floor_ex(
								v->LinesInDETChroma
										+ dml_min(
												v->LinesInDETChroma
														* v->RequiredDISPCLK[ij]
														* v->BytePerPixelInDETC[k]
														* v->PSCL_FACTOR_CHROMA[k]
														/ v->ReturnBWPerState[i],
												v->EffectiveLBLatencyHidingSourceLinesChroma),
								v->SwathHeightCPerState[ijk]);

				if (v->BytePerPixelInDETC[k] == 0) {
					v->UrgentLatencySupportUsPerState[ijk] =
							v->EffectiveDETLBLinesLuma
									* (dest.htotal
											/ dest.pixel_rate_mhz)
									/ scale_ratio_depth.vscl_ratio
									- v->EffectiveDETLBLinesLuma
											* v->SwathWidthYPerState[ijk]
											* dml_ceil_ex(
													v->BytePerPixelInDETY[k],
													1)
											/ (v->ReturnBWPerState[i]
													/ v->NoOfDPP[ijk]);
				} else {
					v->UrgentLatencySupportUsPerState[ijk] =
							dml_min(
									v->EffectiveDETLBLinesLuma
											* (dest.htotal
													/ dest.pixel_rate_mhz)
											/ scale_ratio_depth.vscl_ratio
											- v->EffectiveDETLBLinesLuma
													* v->SwathWidthYPerState[ijk]
													* dml_ceil_ex(
															v->BytePerPixelInDETY[k],
															1)
													/ (v->ReturnBWPerState[i]
															/ v->NoOfDPP[ijk]),
									v->EffectiveDETLBLinesChroma
											* (dest.htotal
													/ dest.pixel_rate_mhz)
											/ (scale_ratio_depth.vscl_ratio
													/ 2)
											- v->EffectiveDETLBLinesChroma
													* v->SwathWidthYPerState[ijk]
													/ 2
													* dml_ceil_ex(
															v->BytePerPixelInDETC[k],
															2)
													/ (v->ReturnBWPerState[i]
															/ v->NoOfDPP[ijk]));
				}

			}
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			ij = j * NumberOfStatesPlusTwo + i;

			v->UrgentLatencySupport[ij] = 1;
			for (k = 0; k < num_planes; k++) {
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (v->UrgentLatencySupportUsPerState[ijk]
						< soc->urgent_latency_us / 1) {
					v->UrgentLatencySupport[ij] = 0;
				}
			}
		}
	}

	// Prefetch Check

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			ij = j * NumberOfStatesPlusTwo + i;

			v->TotalNumberOfDCCActiveDPP[ij] = 0;
			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (src.dcc == 1) {
					v->TotalNumberOfDCCActiveDPP[ij] =
							v->TotalNumberOfDCCActiveDPP[ij]
									+ v->NoOfDPP[ijk];
				}
			}
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			ij = j * NumberOfStatesPlusTwo + i;

			v->ProjectedDCFCLKDeepSleep = 8;

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
						e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				v->ProjectedDCFCLKDeepSleep = dml_max(
						v->ProjectedDCFCLKDeepSleep,
						dest.pixel_rate_mhz / 16);
				if (v->BytePerPixelInDETC[k] == 0) {
					if (scale_ratio_depth.vscl_ratio <= 1) {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETY[k],
														1)
												/ 64
												* scale_ratio_depth.hscl_ratio
												* dest.pixel_rate_mhz
												/ v->NoOfDPP[ijk]);
					} else {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETY[k],
														1)
												/ 64
												* v->PSCL_FACTOR[k]
												* v->RequiredDISPCLK[ij]
												/ (1
														+ j));
					}

				} else {
					if (scale_ratio_depth.vscl_ratio <= 1) {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETY[k],
														1)
												/ 32
												* scale_ratio_depth.hscl_ratio
												* dest.pixel_rate_mhz
												/ v->NoOfDPP[ijk]);
					} else {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETY[k],
														1)
												/ 32
												* v->PSCL_FACTOR[k]
												* v->RequiredDISPCLK[ij]
												/ (1
														+ j));
					}
					if ((scale_ratio_depth.vscl_ratio / 2) <= 1) {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETC[k],
														2)
												/ 32
												* scale_ratio_depth.hscl_ratio
												/ 2
												* dest.pixel_rate_mhz
												/ v->NoOfDPP[ijk]);
					} else {
						v->ProjectedDCFCLKDeepSleep =
								dml_max(
										v->ProjectedDCFCLKDeepSleep,
										1.1
												* dml_ceil_ex(
														v->BytePerPixelInDETC[k],
														2)
												/ 32
												* v->PSCL_FACTOR_CHROMA[k]
												* v->RequiredDISPCLK[ij]
												/ (1
														+ j));
					}

				}
			}

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				struct _vcs_dpi_scaler_ratio_depth_st scale_ratio_depth =
						e2e[v->planes[k].e2e_index].pipe.scale_ratio_depth;
				struct _vcs_dpi_scaler_taps_st scale_taps =
						e2e[v->planes[k].e2e_index].pipe.scale_taps;
				struct _vcs_dpi_display_output_params_st dout =
						e2e[v->planes[k].e2e_index].dout;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (src.dcc == 1) {
					v->MetaReqHeightY = 8 * v->Read256BlockHeightY[k];
					v->MetaReqWidthY = 64 * 256
							/ dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
							/ v->MetaReqHeightY;
					v->MetaSurfaceWidthY = dml_ceil_ex(
							src.viewport_width / v->NoOfDPP[ijk] - 1,
							v->MetaReqWidthY) + v->MetaReqWidthY;
					v->MetaSurfaceHeightY = dml_ceil_ex(
							src.viewport_height - 1,
							v->MetaReqHeightY) + v->MetaReqHeightY;
					if (ip->pte_enable == 1) {
						v->MetaPteBytesPerFrameY =
								(dml_ceil_ex(
										(v->MetaSurfaceWidthY
												* v->MetaSurfaceHeightY
												* dml_ceil_ex(
														v->BytePerPixelInDETY[k],
														1)
												/ 256.0
												- 4096)
												/ 8
												/ 4096,
										1) + 1) * 64;
					} else {
						v->MetaPteBytesPerFrameY = 0;
					}
					if (src.source_scan == dm_horz) {
						v->MetaRowBytesY =
								v->MetaSurfaceWidthY
										* v->MetaReqHeightY
										* dml_ceil_ex(
												v->BytePerPixelInDETY[k],
												1)
										/ 256;
					} else {
						v->MetaRowBytesY =
								v->MetaSurfaceHeightY
										* v->MetaReqWidthY
										* dml_ceil_ex(
												v->BytePerPixelInDETY[k],
												1)
										/ 256;
					}
				} else {
					v->MetaPteBytesPerFrameY = 0;
					v->MetaRowBytesY = 0;
				}

				if (ip->pte_enable == 1) {
					if (src.sw_mode == dm_sw_linear) {
						v->MacroTileBlockSizeBytesY = 256;
						v->MacroTileBlockHeightY = 1;
					} else if (src.sw_mode == dm_sw_4kb_s
							|| src.sw_mode == dm_sw_4kb_s_x
							|| src.sw_mode == dm_sw_4kb_d
							|| src.sw_mode == dm_sw_4kb_d_x) {
						v->MacroTileBlockSizeBytesY = 4096;
						v->MacroTileBlockHeightY = 4
								* v->Read256BlockHeightY[k];
					} else if (src.sw_mode == dm_sw_64kb_s
							|| src.sw_mode == dm_sw_64kb_s_t
							|| src.sw_mode == dm_sw_64kb_s_x
							|| src.sw_mode == dm_sw_64kb_d
							|| src.sw_mode == dm_sw_64kb_d_t
							|| src.sw_mode == dm_sw_64kb_d_x) {
						v->MacroTileBlockSizeBytesY = 64 * 1024;
						v->MacroTileBlockHeightY = 16
								* v->Read256BlockHeightY[k];
					} else {
						v->MacroTileBlockSizeBytesY = 256 * 1024;
						v->MacroTileBlockHeightY = 32
								* v->Read256BlockHeightY[k];
					}
					if (v->MacroTileBlockSizeBytesY <= 65536) {
						v->DataPTEReqHeightY = v->MacroTileBlockHeightY;
					} else {
						v->DataPTEReqHeightY = 16
								* v->Read256BlockHeightY[k];
					}
					v->DataPTEReqWidthY = 4096
							/ dml_ceil_ex(v->BytePerPixelInDETY[k], 1)
							/ v->DataPTEReqHeightY * 8;
					if (src.sw_mode == dm_sw_linear) {
						v->DPTEBytesPerRowY =
								64
										* (dml_ceil_ex(
												(src.viewport_width
														/ v->NoOfDPP[ijk]
														* dml_min(
																128,
																dml_pow(
																		2,
																		dml_floor_ex(
																				dml_log(
																						ip->dpte_buffer_size_in_pte_reqs
																								* v->DataPTEReqWidthY
																								/ (src.viewport_width
																										/ v->NoOfDPP[ijk]),
																						2),
																				1)))
														- 1)
														/ v->DataPTEReqWidthY,
												1)
												+ 1);
					} else if (src.source_scan == dm_horz) {
						v->DPTEBytesPerRowY =
								64
										* (dml_ceil_ex(
												(src.viewport_width
														/ v->NoOfDPP[ijk]
														- 1)
														/ v->DataPTEReqWidthY,
												1)
												+ 1);
					} else {
						v->DPTEBytesPerRowY =
								64
										* (dml_ceil_ex(
												(src.viewport_height
														- 1)
														/ v->DataPTEReqHeightY,
												1)
												+ 1);
					}
				} else {
					v->DPTEBytesPerRowY = 0;
				}

				if (src.source_format != dm_444_64 && src.source_format != dm_444_32
						&& src.source_format != dm_444_16) {
					if (src.dcc == 1) {
						v->MetaReqHeightC = 8 * v->Read256BlockHeightC[k];
						v->MetaReqWidthC =
								64 * 256
										/ dml_ceil_ex(
												v->BytePerPixelInDETC[k],
												2)
										/ v->MetaReqHeightC;
						v->MetaSurfaceWidthC = dml_ceil_ex(
								src.viewport_width / v->NoOfDPP[ijk]
										/ 2 - 1,
								v->MetaReqWidthC)
								+ v->MetaReqWidthC;
						v->MetaSurfaceHeightC = dml_ceil_ex(
								src.viewport_height / 2 - 1,
								v->MetaReqHeightC)
								+ v->MetaReqHeightC;
						if (ip->pte_enable == 1) {
							v->MetaPteBytesPerFrameC =
									(dml_ceil_ex(
											(v->MetaSurfaceWidthC
													* v->MetaSurfaceHeightC
													* dml_ceil_ex(
															v->BytePerPixelInDETC[k],
															2)
													/ 256.0
													- 4096)
													/ 8
													/ 4096,
											1) + 1)
											* 64;
						} else {
							v->MetaPteBytesPerFrameC = 0;
						}
						if (src.source_scan == dm_horz) {
							v->MetaRowBytesC =
									v->MetaSurfaceWidthC
											* v->MetaReqHeightC
											* dml_ceil_ex(
													v->BytePerPixelInDETC[k],
													2)
											/ 256;
						} else {
							v->MetaRowBytesC =
									v->MetaSurfaceHeightC
											* v->MetaReqWidthC
											* dml_ceil_ex(
													v->BytePerPixelInDETC[k],
													2)
											/ 256;
						}
					} else {
						v->MetaPteBytesPerFrameC = 0;
						v->MetaRowBytesC = 0;
					}

					if (ip->pte_enable == 1) {
						if (src.sw_mode == dm_sw_linear) {
							v->MacroTileBlockSizeBytesC = 256;
							v->MacroTileBlockHeightC = 1;
						} else if (src.sw_mode == dm_sw_4kb_s
								|| src.sw_mode == dm_sw_4kb_s_x
								|| src.sw_mode == dm_sw_4kb_d
								|| src.sw_mode == dm_sw_4kb_d_x) {
							v->MacroTileBlockSizeBytesC = 4096;
							v->MacroTileBlockHeightC = 4
									* v->Read256BlockHeightC[k];
						} else if (src.sw_mode == dm_sw_64kb_s
								|| src.sw_mode == dm_sw_64kb_s_t
								|| src.sw_mode == dm_sw_64kb_s_x
								|| src.sw_mode == dm_sw_64kb_d
								|| src.sw_mode == dm_sw_64kb_d_t
								|| src.sw_mode == dm_sw_64kb_d_x) {
							v->MacroTileBlockSizeBytesC = 64 * 1024;
							v->MacroTileBlockHeightC = 16
									* v->Read256BlockHeightC[k];
						} else {
							v->MacroTileBlockSizeBytesC = 256 * 1024;
							v->MacroTileBlockHeightC = 32
									* v->Read256BlockHeightC[k];
						}
						v->MacroTileBlockWidthC =
								v->MacroTileBlockSizeBytesC
										/ dml_ceil_ex(
												v->BytePerPixelInDETC[k],
												2)
										/ v->MacroTileBlockHeightC;
						if (v->MacroTileBlockSizeBytesC <= 65536) {
							v->DataPTEReqHeightC =
									v->MacroTileBlockHeightC;
						} else {
							v->DataPTEReqHeightC = 16
									* v->Read256BlockHeightC[k];
						}
						v->DataPTEReqWidthC =
								4096
										/ dml_ceil_ex(
												v->BytePerPixelInDETC[k],
												2)
										/ v->DataPTEReqHeightC
										* 8;
						if (src.sw_mode == dm_sw_linear) {
							v->DPTEBytesPerRowC =
									64
											* (dml_ceil_ex(
													(src.viewport_width
															/ v->NoOfDPP[ijk]
															/ 2
															* dml_min(
																	128,
																	dml_pow(
																			2,
																			dml_floor_ex(
																					dml_log(
																							ip->dpte_buffer_size_in_pte_reqs
																									* v->DataPTEReqWidthC
																									/ (src.viewport_width
																											/ v->NoOfDPP[ijk]
																											/ 2),
																							2),
																					1)))
															- 1)
															/ v->DataPTEReqWidthC,
													1)
													+ 1);
						} else if (src.source_scan == dm_horz) {
							v->DPTEBytesPerRowC =
									64
											* (dml_ceil_ex(
													(src.viewport_width
															/ v->NoOfDPP[ijk]
															/ 2
															- 1)
															/ v->DataPTEReqWidthC,
													1)
													+ 1);
						} else {
							v->DPTEBytesPerRowC =
									64
											* (dml_ceil_ex(
													(src.viewport_height
															/ 2
															- 1)
															/ v->DataPTEReqHeightC,
													1)
													+ 1);
						}
					} else {
						v->DPTEBytesPerRowC = 0;
					}
				} else {
					v->DPTEBytesPerRowC = 0;
					v->MetaPteBytesPerFrameC = 0;
					v->MetaRowBytesC = 0;
				}

				v->DPTEBytesPerRow[k] = v->DPTEBytesPerRowY + v->DPTEBytesPerRowC;
				v->MetaPTEBytesPerFrame[k] = v->MetaPteBytesPerFrameY
						+ v->MetaPteBytesPerFrameC;
				v->MetaRowBytes[k] = v->MetaRowBytesY + v->MetaRowBytesC;

				v->VInitY = (scale_ratio_depth.vscl_ratio + scale_taps.vtaps + 1
						+ dest.interlaced * 0.5
								* scale_ratio_depth.vscl_ratio)
						/ 2.0;
				v->PrefillY[k] = dml_floor_ex(v->VInitY, 1);
				v->MaxNumSwY[k] = dml_ceil_ex(
						(v->PrefillY[k] - 1.0)
								/ v->SwathHeightYPerState[ijk],
						1) + 1.0;

				if (v->PrefillY[k] > 1) {
					v->MaxPartialSwY = ((int) (v->PrefillY[k] - 2))
							% ((int) v->SwathHeightYPerState[ijk]);
				} else {
					v->MaxPartialSwY = ((int) (v->PrefillY[k]
							+ v->SwathHeightYPerState[ijk] - 2))
							% ((int) v->SwathHeightYPerState[ijk]);
				}
				v->MaxPartialSwY = dml_max(1, v->MaxPartialSwY);

				v->PrefetchLinesY[k] = v->MaxNumSwY[k]
						* v->SwathHeightYPerState[ijk] + v->MaxPartialSwY;

				if (src.source_format != dm_444_64 && src.source_format != dm_444_32
						&& src.source_format != dm_444_16) {
					v->VInitC =
							(scale_ratio_depth.vscl_ratio / 2
									+ scale_taps.vtaps + 1
									+ dest.interlaced * 0.5
											* scale_ratio_depth.vscl_ratio
											/ 2) / 2.0;
					v->PrefillC[k] = dml_floor_ex(v->VInitC, 1);
					v->MaxNumSwC[k] =
							dml_ceil_ex(
									(v->PrefillC[k] - 1.0)
											/ v->SwathHeightCPerState[ijk],
									1) + 1.0;
					if (v->PrefillC[k] > 1) {
						v->MaxPartialSwC =
								((int) (v->PrefillC[k] - 2))
										% ((int) v->SwathHeightCPerState[ijk]);
					} else {
						v->MaxPartialSwC =
								((int) (v->PrefillC[k]
										+ v->SwathHeightCPerState[ijk]
										- 2))
										% ((int) v->SwathHeightCPerState[ijk]);
					}
					v->MaxPartialSwC = dml_max(1, v->MaxPartialSwC);

					v->PrefetchLinesC[k] = v->MaxNumSwC[k]
							* v->SwathHeightCPerState[ijk]
							+ v->MaxPartialSwC;
				} else {
					v->PrefetchLinesC[k] = 0;
				}

				v->dst_x_after_scaler = 90 * dest.pixel_rate_mhz
						/ (v->RequiredDISPCLK[ij] / (j + 1))
						+ 42 * dest.pixel_rate_mhz / v->RequiredDISPCLK[ij];
				if (v->NoOfDPP[ijk] > 1) {
					v->dst_x_after_scaler = v->dst_x_after_scaler
							+ dest.recout_width / 2.0;
				}

				if (dout.output_format == dm_420) {
					v->dst_y_after_scaler = 1;
				} else {
					v->dst_y_after_scaler = 0;
				}

				v->TimeCalc = 24 / v->ProjectedDCFCLKDeepSleep;

				v->VUpdateOffset = dml_ceil_ex(dest.htotal / 4, 1);
				v->TotalRepeaterDelay = ip->max_inter_dcn_tile_repeaters
						* (2 / (v->RequiredDISPCLK[ij] / (j + 1))
								+ 3 / v->RequiredDISPCLK[ij]);
				v->VUpdateWidth = (14 / v->ProjectedDCFCLKDeepSleep
						+ 12 / (v->RequiredDISPCLK[ij] / (j + 1))
						+ v->TotalRepeaterDelay) * dest.pixel_rate_mhz;
				v->VReadyOffset =
						dml_max(
								150
										/ (v->RequiredDISPCLK[ij]
												/ (j
														+ 1)),
								v->TotalRepeaterDelay
										+ 20
												/ v->ProjectedDCFCLKDeepSleep
										+ 10
												/ (v->RequiredDISPCLK[ij]
														/ (j
																+ 1)))
								* dest.pixel_rate_mhz;

				v->TimeSetup =
						(v->VUpdateOffset + v->VUpdateWidth
								+ v->VReadyOffset)
								/ dest.pixel_rate_mhz;

				v->ExtraLatency =
						v->UrgentRoundTripAndOutOfOrderLatencyPerState[i]
								+ (v->TotalNumberOfActiveDPP[ij]
										* ip->pixel_chunk_size_kbytes
										+ v->TotalNumberOfDCCActiveDPP[ij]
												* ip->meta_chunk_size_kbytes)
										* 1024
										/ v->ReturnBWPerState[i];

				if (ip->pte_enable == 1) {
					v->ExtraLatency = v->ExtraLatency
							+ v->TotalNumberOfActiveDPP[ij]
									* ip->pte_chunk_size_kbytes
									* 1024
									/ v->ReturnBWPerState[i];
				}

				if (ip->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one
						== 1) {
					v->MaximumVStartup = dest.vtotal - dest.vactive - 1;
				} else {
					v->MaximumVStartup = dest.vsync_plus_back_porch - 1;
				}

				v->LineTimesForPrefetch[k] =
						v->MaximumVStartup
								- soc->urgent_latency_us
										/ (dest.htotal
												/ dest.pixel_rate_mhz)
								- (v->TimeCalc + v->TimeSetup)
										/ (dest.htotal
												/ dest.pixel_rate_mhz)
								- (v->dst_y_after_scaler
										+ v->dst_x_after_scaler
												/ dest.htotal);

				v->LineTimesForPrefetch[k] = dml_floor_ex(
						4.0 * (v->LineTimesForPrefetch[k] + 0.125),
						1) / 4;

				v->PrefetchBW[k] =
						(v->MetaPTEBytesPerFrame[k] + 2 * v->MetaRowBytes[k]
								+ 2 * v->DPTEBytesPerRow[k]
								+ v->PrefetchLinesY[k]
										* v->SwathWidthYPerState[ijk]
										* dml_ceil_ex(
												v->BytePerPixelInDETY[k],
												1)
								+ v->PrefetchLinesC[k]
										* v->SwathWidthYPerState[ijk]
										/ 2
										* dml_ceil_ex(
												v->BytePerPixelInDETC[k],
												2))
								/ (v->LineTimesForPrefetch[k]
										* dest.htotal
										/ dest.pixel_rate_mhz);
			}

			v->BWAvailableForImmediateFlip = v->ReturnBWPerState[i];

			for (k = 0; k < num_planes; k++) {
				v->BWAvailableForImmediateFlip = v->BWAvailableForImmediateFlip
						- dml_max(v->ReadBandwidth[k], v->PrefetchBW[k]);
			}

			v->TotalImmediateFlipBytes = 0;

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;

				if (src.source_format != dm_420_8
						&& src.source_format != dm_420_10) {
					v->TotalImmediateFlipBytes = v->TotalImmediateFlipBytes
							+ v->MetaPTEBytesPerFrame[k]
							+ v->MetaRowBytes[k]
							+ v->DPTEBytesPerRow[k];
				}
			}

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (ip->pte_enable == 1 && src.dcc == 1) {
					v->TimeForMetaPTEWithImmediateFlip =
							dml_max(
									v->MetaPTEBytesPerFrame[k]
											/ v->PrefetchBW[k],
									dml_max(
											v->MetaPTEBytesPerFrame[k]
													* v->TotalImmediateFlipBytes
													/ (v->BWAvailableForImmediateFlip
															* (v->MetaPTEBytesPerFrame[k]
																	+ v->MetaRowBytes[k]
																	+ v->DPTEBytesPerRow[k])),
											dml_max(
													v->ExtraLatency,
													dml_max(
															soc->urgent_latency_us,
															dest.htotal
																	/ dest.pixel_rate_mhz
																	/ 4))));

					v->TimeForMetaPTEWithoutImmediateFlip =
							dml_max(
									v->MetaPTEBytesPerFrame[k]
											/ v->PrefetchBW[k],
									dml_max(
											v->ExtraLatency,
											dest.htotal
													/ dest.pixel_rate_mhz
													/ 4));
				} else {
					v->TimeForMetaPTEWithImmediateFlip = dest.htotal
							/ dest.pixel_rate_mhz / 4;
					v->TimeForMetaPTEWithoutImmediateFlip = dest.htotal
							/ dest.pixel_rate_mhz / 4;
				}

				if (ip->pte_enable == 1 || src.dcc == 1) {
					v->TimeForMetaAndDPTERowWithImmediateFlip =
							dml_max(
									(v->MetaRowBytes[k]
											+ v->DPTEBytesPerRow[k])
											/ v->PrefetchBW[k],
									dml_max(
											(v->MetaRowBytes[k]
													+ v->DPTEBytesPerRow[k])
													* v->TotalImmediateFlipBytes
													/ (v->BWAvailableForImmediateFlip
															* (v->MetaPTEBytesPerFrame[k]
																	+ v->MetaRowBytes[k]
																	+ v->DPTEBytesPerRow[k])),
											dml_max(
													dest.htotal
															/ dest.pixel_rate_mhz
															- v->TimeForMetaPTEWithImmediateFlip,
													dml_max(
															v->ExtraLatency,
															2
																	* soc->urgent_latency_us))));

					v->TimeForMetaAndDPTERowWithoutImmediateFlip =
							dml_max(
									(v->MetaRowBytes[k]
											+ v->DPTEBytesPerRow[k])
											/ v->PrefetchBW[k],
									dml_max(
											dest.htotal
													/ dest.pixel_rate_mhz
													- v->TimeForMetaPTEWithoutImmediateFlip,
											v->ExtraLatency));
				} else {
					v->TimeForMetaAndDPTERowWithImmediateFlip =
							dml_max(
									dest.htotal
											/ dest.pixel_rate_mhz
											- v->TimeForMetaPTEWithImmediateFlip,
									v->ExtraLatency
											- v->TimeForMetaPTEWithImmediateFlip);
					v->TimeForMetaAndDPTERowWithoutImmediateFlip =
							dml_max(
									dest.htotal
											/ dest.pixel_rate_mhz
											- v->TimeForMetaPTEWithoutImmediateFlip,
									v->ExtraLatency
											- v->TimeForMetaPTEWithoutImmediateFlip);
				}

				v->LinesForMetaPTEWithImmediateFlip[k] =
						dml_floor_ex(
								4.0
										* (v->TimeForMetaPTEWithImmediateFlip
												/ (dest.htotal
														/ dest.pixel_rate_mhz)
												+ 0.125),
								1) / 4.0;

				v->LinesForMetaPTEWithoutImmediateFlip[k] =
						dml_floor_ex(
								4.0
										* (v->TimeForMetaPTEWithoutImmediateFlip
												/ (dest.htotal
														/ dest.pixel_rate_mhz)
												+ 0.125),
								1) / 4.0;

				v->LinesForMetaAndDPTERowWithImmediateFlip[k] =
						dml_floor_ex(
								4.0
										* (v->TimeForMetaAndDPTERowWithImmediateFlip
												/ (dest.htotal
														/ dest.pixel_rate_mhz)
												+ 0.125),
								1) / 4.0;

				v->LinesForMetaAndDPTERowWithoutImmediateFlip[k] =
						dml_floor_ex(
								4.0
										* (v->TimeForMetaAndDPTERowWithoutImmediateFlip
												/ (dest.htotal
														/ dest.pixel_rate_mhz)
												+ 0.125),
								1) / 4.0;

				v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip =
						v->LineTimesForPrefetch[k]
								- v->LinesForMetaPTEWithImmediateFlip[k]
								- v->LinesForMetaAndDPTERowWithImmediateFlip[k];

				v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip =
						v->LineTimesForPrefetch[k]
								- v->LinesForMetaPTEWithoutImmediateFlip[k]
								- v->LinesForMetaAndDPTERowWithoutImmediateFlip[k];

				if (v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip > 0) {
					v->VRatioPreYWithImmediateFlip[ijk] =
							v->PrefetchLinesY[k]
									/ v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip;
					if (v->SwathHeightYPerState[ijk] > 4) {
						if (v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
								- (v->PrefillY[k] - 3.0) / 2.0
								> 0) {
							v->VRatioPreYWithImmediateFlip[ijk] =
									dml_max(
											v->VRatioPreYWithImmediateFlip[ijk],
											(v->MaxNumSwY[k]
													* v->SwathHeightYPerState[ijk])
													/ (v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
															- (v->PrefillY[k]
																	- 3.0)
																	/ 2.0));
						} else {
							v->VRatioPreYWithImmediateFlip[ijk] =
									999999;
						}
					}
					v->VRatioPreCWithImmediateFlip[ijk] =
							v->PrefetchLinesC[k]
									/ v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip;
					if (v->SwathHeightCPerState[ijk] > 4) {
						if (v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
								- (v->PrefillC[k] - 3.0) / 2.0
								> 0) {
							v->VRatioPreCWithImmediateFlip[ijk] =
									dml_max(
											v->VRatioPreCWithImmediateFlip[ijk],
											(v->MaxNumSwC[k]
													* v->SwathHeightCPerState[ijk])
													/ (v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
															- (v->PrefillC[k]
																	- 3.0)
																	/ 2.0));
						} else {
							v->VRatioPreCWithImmediateFlip[ijk] =
									999999;
						}
					}

					v->RequiredPrefetchPixelDataBWWithImmediateFlip[ijk] =
							v->NoOfDPP[ijk]
									* (v->PrefetchLinesY[k]
											/ v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
											* dml_ceil_ex(
													v->BytePerPixelInDETY[k],
													1)
											+ v->PrefetchLinesC[k]
													/ v->LineTimesToRequestPrefetchPixelDataWithImmediateFlip
													* dml_ceil_ex(
															v->BytePerPixelInDETC[k],
															2)
													/ 2)
									* v->SwathWidthYPerState[ijk]
									/ (dest.htotal
											/ dest.pixel_rate_mhz);
				} else {
					v->VRatioPreYWithImmediateFlip[ijk] = 999999;
					v->VRatioPreCWithImmediateFlip[ijk] = 999999;
					v->RequiredPrefetchPixelDataBWWithImmediateFlip[ijk] =
							999999;
				}

				if (v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
						> 0) {
					v->VRatioPreYWithoutImmediateFlip[ijk] =
							v->PrefetchLinesY[k]
									/ v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip;
					if (v->SwathHeightYPerState[ijk] > 4) {
						if (v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
								- (v->PrefillY[k] - 3.0) / 2.0
								> 0) {
							v->VRatioPreYWithoutImmediateFlip[ijk] =
									dml_max(
											v->VRatioPreYWithoutImmediateFlip[ijk],
											(v->MaxNumSwY[k]
													* v->SwathHeightYPerState[ijk])
													/ (v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
															- (v->PrefillY[k]
																	- 3.0)
																	/ 2.0));
						} else {
							v->VRatioPreYWithoutImmediateFlip[ijk] =
									999999;
						}
					}
					v->VRatioPreCWithoutImmediateFlip[ijk] =
							v->PrefetchLinesC[k]
									/ v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip;
					if (v->SwathHeightCPerState[ijk] > 4) {
						if (v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
								- (v->PrefillC[k] - 3.0) / 2.0
								> 0) {
							v->VRatioPreCWithoutImmediateFlip[ijk] =
									dml_max(
											v->VRatioPreCWithoutImmediateFlip[ijk],
											(v->MaxNumSwC[k]
													* v->SwathHeightCPerState[ijk])
													/ (v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
															- (v->PrefillC[k]
																	- 3.0)
																	/ 2.0));
						} else {
							v->VRatioPreCWithoutImmediateFlip[ijk] =
									999999;
						}
					}

					v->RequiredPrefetchPixelDataBWWithoutImmediateFlip[ijk] =
							v->NoOfDPP[ijk]
									* (v->PrefetchLinesY[k]
											/ v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
											* dml_ceil_ex(
													v->BytePerPixelInDETY[k],
													1)
											+ v->PrefetchLinesC[k]
													/ v->LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip
													* dml_ceil_ex(
															v->BytePerPixelInDETC[k],
															2)
													/ 2)
									* v->SwathWidthYPerState[ijk]
									/ (dest.htotal
											/ dest.pixel_rate_mhz);
				} else {
					v->VRatioPreYWithoutImmediateFlip[ijk] = 999999;
					v->VRatioPreCWithoutImmediateFlip[ijk] = 999999;
					v->RequiredPrefetchPixelDataBWWithoutImmediateFlip[ijk] =
							999999;
				}
			}

			v->MaximumReadBandwidthWithPrefetchWithImmediateFlip = 0;

			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				struct _vcs_dpi_display_pipe_dest_params_st dest =
						e2e[v->planes[k].e2e_index].pipe.dest;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (src.source_format != dm_420_8
						&& src.source_format != dm_420_10) {
					v->MaximumReadBandwidthWithPrefetchWithImmediateFlip =
							v->MaximumReadBandwidthWithPrefetchWithImmediateFlip
									+ dml_max(
											v->ReadBandwidth[k],
											v->RequiredPrefetchPixelDataBWWithImmediateFlip[ijk])
									+ dml_max(
											v->MetaPTEBytesPerFrame[k]
													/ (v->LinesForMetaPTEWithImmediateFlip[k]
															* dest.htotal
															/ dest.pixel_rate_mhz),
											(v->MetaRowBytes[k]
													+ v->DPTEBytesPerRow[k])
													/ (v->LinesForMetaAndDPTERowWithImmediateFlip[k]
															* dest.htotal
															/ dest.pixel_rate_mhz));
				} else {
					v->MaximumReadBandwidthWithPrefetchWithImmediateFlip =
							v->MaximumReadBandwidthWithPrefetchWithImmediateFlip
									+ dml_max(
											v->ReadBandwidth[k],
											v->RequiredPrefetchPixelDataBWWithoutImmediateFlip[ijk]);
				}
			}

			v->MaximumReadBandwidthWithPrefetchWithoutImmediateFlip = 0;

			for (k = 0; k < num_planes; k++) {
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				v->MaximumReadBandwidthWithPrefetchWithoutImmediateFlip =
						v->MaximumReadBandwidthWithPrefetchWithoutImmediateFlip
								+ dml_max(
										v->ReadBandwidth[k],
										v->RequiredPrefetchPixelDataBWWithoutImmediateFlip[ijk]);
			}

			v->PrefetchSupportedWithImmediateFlip[ij] = 1;
			if (v->MaximumReadBandwidthWithPrefetchWithImmediateFlip
					> v->ReturnBWPerState[i]) {
				v->PrefetchSupportedWithImmediateFlip[ij] = 0;
			}
			for (k = 0; k < num_planes; k++) {
				if (v->LineTimesForPrefetch[k] < 2
						|| v->LinesForMetaPTEWithImmediateFlip[k] >= 8
						|| v->LinesForMetaAndDPTERowWithImmediateFlip[k]
								>= 16) {
					v->PrefetchSupportedWithImmediateFlip[ij] = 0;
				}
			}

			v->PrefetchSupportedWithoutImmediateFlip[ij] = 1;
			if (v->MaximumReadBandwidthWithPrefetchWithoutImmediateFlip
					> v->ReturnBWPerState[i]) {
				v->PrefetchSupportedWithoutImmediateFlip[ij] = 0;
			}
			for (k = 0; k < num_planes; k++) {
				if (v->LineTimesForPrefetch[k] < 2
						|| v->LinesForMetaPTEWithoutImmediateFlip[k] >= 8
						|| v->LinesForMetaAndDPTERowWithoutImmediateFlip[k]
								>= 16) {
					v->PrefetchSupportedWithoutImmediateFlip[ij] = 0;
				}
			}
		}
	}

	for (i = 0; i < NumberOfStatesPlusTwo; i++) {
		for (j = 0; j < 2; j++) {
			ij = j * NumberOfStatesPlusTwo + i;

			v->VRatioInPrefetchSupportedWithImmediateFlip[ij] = 1;
			for (k = 0; k < num_planes; k++) {
				struct _vcs_dpi_display_pipe_source_params_st src =
						e2e[v->planes[k].e2e_index].pipe.src;
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (((src.source_format != dm_420_8
						&& src.source_format != dm_420_10)
						&& (v->VRatioPreYWithImmediateFlip[ijk] > 4
								|| v->VRatioPreCWithImmediateFlip[ijk]
										> 4))
						|| ((src.source_format == dm_420_8
								|| src.source_format == dm_420_10)
								&& (v->VRatioPreYWithoutImmediateFlip[ijk]
										> 4
										|| v->VRatioPreCWithoutImmediateFlip[ijk]
												> 4))) {
					v->VRatioInPrefetchSupportedWithImmediateFlip[ij] = 0;
				}
			}
			v->VRatioInPrefetchSupportedWithoutImmediateFlip[ij] = 1;
			for (k = 0; k < num_planes; k++) {
				ijk = k * 2 * NumberOfStatesPlusTwo + j * NumberOfStatesPlusTwo + i;

				if (v->VRatioPreYWithoutImmediateFlip[ijk] > 4
						|| v->VRatioPreCWithoutImmediateFlip[ijk] > 4) {
					v->VRatioInPrefetchSupportedWithoutImmediateFlip[ij] = 0;
				}
			}
		}
	}

	// Mode Support, Voltage State and SOC Configuration

	for (i = (NumberOfStatesPlusTwo - 1); i >= 0; i--) // use int type here
			{
		for (j = 0; j < 2; j++) {
			ij = j * NumberOfStatesPlusTwo + i;

			if (v->ScaleRatioSupport == 1 && v->SourceFormatPixelAndScanSupport == 1
					&& v->ViewportSizeSupport == 1
					&& v->BandwidthSupport[i] == 1 && v->DIOSupport[i] == 1
					&& v->UrgentLatencySupport[ij] == 1 && v->ROBSupport[i] == 1
					&& v->DISPCLK_DPPCLK_Support[ij] == 1
					&& v->TotalAvailablePipesSupport[ij] == 1
					&& v->TotalAvailableWritebackSupport == 1
					&& v->WritebackLatencySupport == 1) {
				if (v->PrefetchSupportedWithImmediateFlip[ij] == 1
						&& v->VRatioInPrefetchSupportedWithImmediateFlip[ij]
								== 1) {
					v->ModeSupportWithImmediateFlip[ij] = 1;
				} else {
					v->ModeSupportWithImmediateFlip[ij] = 0;
				}
				if (v->PrefetchSupportedWithoutImmediateFlip[ij] == 1
						&& v->VRatioInPrefetchSupportedWithoutImmediateFlip[ij]
								== 1) {
					v->ModeSupportWithoutImmediateFlip[ij] = 1;
				} else {
					v->ModeSupportWithoutImmediateFlip[ij] = 0;
				}
			} else {
				v->ModeSupportWithImmediateFlip[ij] = 0;
				v->ModeSupportWithoutImmediateFlip[ij] = 0;
			}
		}
	}

	for (i = (NumberOfStatesPlusTwo - 1); i >= 0; i--) // use int type here
			{
		if ((i == (NumberOfStatesPlusTwo - 1)
				|| v->ModeSupportWithImmediateFlip[1 * NumberOfStatesPlusTwo + i]
						== 1
				|| v->ModeSupportWithImmediateFlip[0 * NumberOfStatesPlusTwo + i]
						== 1) && i >= v->VoltageOverrideLevel) {
			v->VoltageLevelWithImmediateFlip = i;
		}
	}

	for (i = (NumberOfStatesPlusTwo - 1); i >= 0; i--) // use int type here
			{
		if ((i == (NumberOfStatesPlusTwo - 1)
				|| v->ModeSupportWithoutImmediateFlip[1 * NumberOfStatesPlusTwo + i]
						== 1
				|| v->ModeSupportWithoutImmediateFlip[0 * NumberOfStatesPlusTwo + i]
						== 1) && i >= v->VoltageOverrideLevel) {
			v->VoltageLevelWithoutImmediateFlip = i;
		}
	}

	if (v->VoltageLevelWithImmediateFlip == (NumberOfStatesPlusTwo - 1)) {
		v->ImmediateFlipSupported = 0;
		v->VoltageLevel = v->VoltageLevelWithoutImmediateFlip;
	} else {
		v->ImmediateFlipSupported = 1;
		v->VoltageLevel = v->VoltageLevelWithImmediateFlip;
	}

	v->DCFCLK = v->DCFCLKPerState[(int) v->VoltageLevel];
	v->FabricAndDRAMBandwidth = v->FabricAndDRAMBandwidthPerState[(int) v->VoltageLevel];

	for (j = 0; j < 2; j++) {
		v->RequiredDISPCLKPerRatio[j] = v->RequiredDISPCLK[j * NumberOfStatesPlusTwo
				+ (int) v->VoltageLevel];
		for (k = 0; k < num_planes; k++) {
			v->DPPPerPlanePerRatio[k * 2 + j] = v->NoOfDPP[k * 2 * NumberOfStatesPlusTwo
					+ j * NumberOfStatesPlusTwo + (int) v->VoltageLevel];
		}
		v->DISPCLK_DPPCLK_SupportPerRatio[j] = v->DISPCLK_DPPCLK_Support[j
				* NumberOfStatesPlusTwo + (int) v->VoltageLevel];
	}

	ASSERT(v->ImmediateFlipSupported || v->MacroTileBlockWidthC || v->DCFCLK || v->FabricAndDRAMBandwidth);

	return (v->VoltageLevel);
}

