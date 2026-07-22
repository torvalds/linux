// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "dml2_debug.h"
#include "dml2_core_dcn5_calcs_dsc_latency.h"
#include "lib_float_math.h"

/* -------------------------------------------------------------------------
 * Shared DSC sub-functions (static) — identical for legacy and updated paths
 * ------------------------------------------------------------------------- */

// function to compute the dscc_pcl (i.e., pixel compression layer) latency in terms of groups (output is actually in terms of dispclk)
static int dscc_pcl_compute_delay(enum dml2_output_format_class pixel_format, int num_slices)
{
	int dispclk_per_dscclk;
	int delay;

	//1 slice configuration uses a single slice processor (/3 clock)
	if (num_slices == 1) {
	  dispclk_per_dscclk = 3;
	} else { //greater than 1 slice configuration uses two slice processor (/6 clock)
	  dispclk_per_dscclk = 6;
	}

	// N422 and N420 process 2x pixels per clock
	if (pixel_format == dml2_420 || pixel_format == dml2_n422) {
		dispclk_per_dscclk *= 2;
	}

	//fixed delay though dscc_pcl
	delay = 7;

	delay *= dispclk_per_dscclk;
	return delay;
}

// function to compute dscc_bcl layer delay in terms of groups + pipeline delay and pixels (444 format pixels)
// returns the delay in groups of pixels, pipeline delay in cycles, and individual pixels
// valid bpc		 = source bits per component in the set of {8, 10, 12}
// valid bpp		 = increments of 1/16 of a bit
//					min = 6/7/8 in N420/N422/444, respectively
//					max = such that compression is 1:1
// valid slice_width  = number of pixels per slice line, must be less than or equal to `DSC_MAX_WIDTH/num_slices (or 4096/num_slices in 420 mode)
// valid num_slices   = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
// valid pixel_format = pixel/color format in the set of {dml2_444, dml2_s422, dml2_n422, dml2_420}
// note: implementation takes 4 cycles to process a group in N422 mode or 3 cycles in all other pixel formats
static void dscc_bcl_compute_delay(latency_t *p, int bpc, float bpp, int slice_width, int num_slices, enum dml2_output_format_class pixel_format, int initial_xmit_delay_offset, int group_delay_after_initial_xmit_delay_override_en, int group_delay_after_initial_xmit_delay)
{
	//fixed value
	int rc_model_size = 8192;

	//latency calculation variables
	int pixels_per_clock;
	int padding_pixels;
	int ssm_group_priming_delay;
	int ssm_pipeline_delay;
	int obsm_pipeline_delay;
	int slice_padded_pixels;
	int ixd_plus_padding;
	int ixd_plus_padding_groups;
	int cycles_per_group;
	int groups_per_bcl_cycle;
	int syntax_elements_per_group;
	int group_delay;
	int pipeline_delay;
	int pixel_delay;
	int additional_group_delay;
	int lines_to_reach_ixd_adjusted;
	int groups_to_reach_ixd_adjusted;
	int slice_width_groups;
	int initial_xmit_delay;
	int lines_to_reach_ixd;
	int slice_width_modified;

	//N422/N420 operate at 2 pixels per clock
	if (pixel_format == dml2_n422 || pixel_format == dml2_420) {
		pixels_per_clock = 2;
	} else { //all other modes operate at 1 pixel per clock
		pixels_per_clock = 1;
	}

	//initial transmit delay as per PPS. If debugging, ensure register's
	//initial_xmit delay matches the final initial_xmit_delay below
	initial_xmit_delay = (int)math_round((double)(rc_model_size / 2.0 / bpp / pixels_per_clock));

	//slice width as seen by dscc_bcl in pixels or pixels pairs (depending on number of pixels per pixel container based on pixel format)
	slice_width_modified = (pixel_format == dml2_n422 || pixel_format == dml2_420) ? slice_width/2 : slice_width;

	//initial_xmit_delay is increased by one under specific conditions as per pps spreadsheet
	padding_pixels = ((slice_width_modified % 3) != 0) ? (3 - (slice_width_modified % 3)) * (initial_xmit_delay / slice_width_modified) : 0;
	if ((3.0 * pixels_per_clock * bpp) >= (((initial_xmit_delay + 2) / 3) * (3 + (pixel_format == dml2_n422)))) {
		if ((initial_xmit_delay + padding_pixels) % 3 == 1)
			initial_xmit_delay++;
	}

	//increase initial xmit delay by the offset value
	initial_xmit_delay += initial_xmit_delay_offset;

	//sub-stream multiplexer balance fifo priming delay in groups as per dsc standard
	switch (bpc) {
	case  8:
		ssm_group_priming_delay =  83;
		break;
	case 10:
		ssm_group_priming_delay =  91;
		break;
	case 12:
		ssm_group_priming_delay = 115;
		break;
	case 14:
		ssm_group_priming_delay = 123;
		break;
	case 16:
		ssm_group_priming_delay = 128;
		break;
	default:
		DML_LOG_VERBOSE("ERROR: BPC is not a valid value. bpc = %d", bpc);
		ssm_group_priming_delay = 83; // Default to 8bpc value
		break;
	}

	//slice width in groups is rounded up to the nearest group as DSC adds padded pixels such that there are an integer number of groups per slice
	slice_width_groups = (slice_width_modified + 2) / 3;

	//determine number of padded pixels in the last group of a slice line, computed as
	slice_padded_pixels = 3 * slice_width_groups - slice_width_modified;

	//determine integer number of complete slice lines required to reach initial transmit delay without ssm delay considered
	lines_to_reach_ixd = initial_xmit_delay / slice_width_modified;

	//increase initial transmit delay by the number of padded pixels added to a slice line multiplied by the integer number of complete lines to reach initial transmit delay
	//this step is necessary as each padded pixel added takes up a clock cycle but is not included in the initialXmitDelay and, therefore, adds to the overall delay
	ixd_plus_padding = initial_xmit_delay + slice_padded_pixels * lines_to_reach_ixd;

	//convert the the padded initial transmit delay from pixels to groups by rounding up to the nearest group as DSC processes in groups of pixels
	ixd_plus_padding_groups = (ixd_plus_padding + 2) / 3;

	//number of groups required for a slice to reach initial transmit delay is the sum of the padded initial transmit delay plus the ssm group priming delay
	groups_to_reach_ixd_adjusted = ixd_plus_padding_groups + ssm_group_priming_delay;

	//number of lines required to reach padded initial transmit delay in groups in slices to the left of the last horizontal slice
	//needs to be rounded up as a complete slice lines are buffered prior to initial transmit delay being reached in the last horizontal slice
	lines_to_reach_ixd_adjusted = (groups_to_reach_ixd_adjusted + slice_width_groups - 1) / slice_width_groups; //round up lines to reach ixd to next

	//determine if there are non-zero number of pixels reached in the group where initial transmit delay is reached
	//an additional group time (i.e., 3 pixel times) is required before the first output if there are no additional pixels beyond initial transmit delay
	additional_group_delay = ((initial_xmit_delay - lines_to_reach_ixd * slice_width_modified) % 3) == 0 ? 1 : 0;

	//number of pipeline delay cycles in the ssm block (can be determined empirically or analytically by inspecting the ssm block), 1 cycle for mg_dm_ra flopping, 1 cycle for ssefc flopping, 2 cycles for memory read pipeline
	ssm_pipeline_delay = 4;

	//number of pipe delay cycles in the obsm block (can be determined empirically or analytically by inspecting the obsm block), 1 cycle for flopping data out
	obsm_pipeline_delay = 1;

	//a group of pixels is worth 6 pixels in N422/N420 mode or 3 pixels in all other modes
	cycles_per_group = (pixel_format == dml2_n422 || pixel_format == dml2_420) ? 6 : 3;

	//number of groups processed per bcl cycle where a bcl cycle consists of processing the set of syntax elements at the input (1 syntax element in 1 slice config or 2 syntax elements in a 2 or more slice config)
	groups_per_bcl_cycle = (num_slices > 1) ? 2 : 1;

	//number of syntax elements per group is 4 in N422 or 3 in all other pixel formats
	syntax_elements_per_group = (pixel_format == dml2_n422) ? 4 : 3;

	//delay of the bit stream contruction layer in pixels is the sum of:
	//1. number of pixel containers in a slice line multiplied by the number of lines required to reach initial transmit delay multiplied by number of slices to the left of the last horizontal slice
	group_delay  = (lines_to_reach_ixd_adjusted * slice_width_groups * (num_slices - 1));

	//2. number of pixel containers required to reach initial transmit delay (specifically, in the last horizontal slice), value of groups_to_reach_ixd_adjusted is multiplied by groups_per_bcl_cycle (processing rate is 1/2 in 2 or more slice config)
	group_delay += (lines_to_reach_ixd_adjusted - 1) * slice_width_groups;
	group_delay += groups_per_bcl_cycle * (groups_to_reach_ixd_adjusted - ((lines_to_reach_ixd_adjusted - 1) * slice_width_groups));

	//3. additional group of delay if initial transmit delay is reached exactly in a group
	group_delay += additional_group_delay;

	//4. additional group delay if slice width is not evenly divisible by 2, applicable when initial xmit delay is met on the ssm output 1
	if (num_slices >= 2) {
	  if ((num_slices % 2) == 0 || lines_to_reach_ixd_adjusted % 2 == 0) {
		group_delay += (slice_width_groups % 2) != 0;
	  }
	}

	//5. additional 1 group if slice count is 3 or more, slice count is odd, and initial transmit delay is reached on ssm output 0
	if (group_delay_after_initial_xmit_delay_override_en == 0) {
	  if (num_slices >= 3) {
		if ((num_slices % 2) == 1) {
		  if (lines_to_reach_ixd_adjusted % 2 == 1) {
			group_delay += 1;
		  }
		}
	  }
	} else { //use programmed delay
	  group_delay += group_delay_after_initial_xmit_delay;

	  //reduce group delay by 1 if initial xmit delay is reached on the first slice stream
	  if (num_slices >= 3) {
		if ((num_slices % 2) == 1) {
		  if (lines_to_reach_ixd_adjusted % 2 == 1) {
			group_delay -= -1;
		  }
		}
	  }
	}

	//6. ssm and obsm pipeline delay (i.e., clock cycles of delay)
	pipeline_delay = ssm_pipeline_delay + obsm_pipeline_delay;

	//pixel delay is group_delay (converted to pixels) + pipeline, however, first group (or first pair) of groups in 1 slice config (or 2 or more slice config) is a special case since it is processed as soon as it arrives (i.e., in syntax_elements_per_group cycles * groups_per_bcl_cycle)
	pixel_delay = ((group_delay - groups_per_bcl_cycle) * cycles_per_group) + (syntax_elements_per_group * groups_per_bcl_cycle) + pipeline_delay;

	//map delay values to return data structure
	p->groups   = group_delay;
	p->pipeline = pipeline_delay;
	p->pixels   = pixel_delay;

	// Extra variables for functional coverage
	p->additional_group_delay		  = additional_group_delay;
	p->lines_to_reach_ixd			  = lines_to_reach_ixd_adjusted;
	p->groups_to_reach_ixd			 = groups_to_reach_ixd_adjusted;
	p->slice_width_groups			  = slice_width_groups;
	p->initial_xmit_delay			  = initial_xmit_delay;
	p->number_of_lines_to_reach_ixd	= lines_to_reach_ixd;
	p->slice_width_modified			= slice_width_modified;

	return;
}

// function to compute input delay (delay from DSC pixel input to DSCCIF output)
// returns the delay
// valid pixel_format = pixel/color format in the set of {dml2_444, dml2_s422, dml2_n422, dml2_420}
static int dsc_compute_input_pixel_delay(enum dml2_output_format_class pixel_format, int num_slices, int dispclk_dynamic_gating_en)
{
	// initialize latency value
	int delay = 0;

	// sfr
	delay += 2;

	// dscc - vblank clock control for dispclk
	if (dispclk_dynamic_gating_en == 1) {
		//in N420, pixel containers arrive every other cycle so the downstream ends up getting 2 pixel containers back to back for which the logic can absorb (i.e., hide the stall)
		if (pixel_format != dml2_420) {
		  delay += 1; //1 cycle delay from first set of pixels until the clock turns on
		}
	}

	// dsccif, delay values for N422/S422 only, no delay for N444/N420
	if (pixel_format == dml2_n422) {
		// extra delay required to collect a pair of pixels
		delay += 1;
	} else if (pixel_format == dml2_s422) {
		// extra delay required to collect pixels for interpolation
		delay += 4;
	}

	//dsccif, base delay in derasterization block
	delay += 1;

	//2 slices or more configuration, derasterization enabled
	if (num_slices >= 2) {
	  delay += 4; // ram 1 cycle write, ram 2 cycle read, 1 cycle prefetch buffer
	};

	// return result
	return delay;
}

/* -------------------------------------------------------------------------
 * Output pixel delay — legacy and updated formulas differ here
 * ------------------------------------------------------------------------- */

// function to compute output pixel delay (delay from DSCCIF output to DSC output) — legacy DCN5 formula
// returns the delay and uncertainty
// valid pixel_format = pixel/color format in the set of {dml2_444, dml2_s422, dml2_n422, dml2_420}
static delay_uncertainty_t legacy_dsc_compute_output_pixel_delay(enum dml2_output_format_class pixel_format, int num_slices, int dscclk_dynamic_gating_en)
{
	// initialize latency value
	int dispclk_per_dscclk;
	int delay				= 0;
	int uncertainty		  = 0;
	delay_uncertainty_t delay_uncertainty;

	//1 slice configuration uses a single slice processor (/3 clock)
	if (num_slices == 1) {
	  dispclk_per_dscclk = 3;
	} else { //greater than 1 slice configuration uses two slice processor (/6 clock)
	  dispclk_per_dscclk = 6;
	}

	// N422 and N420 process 2x pixels per clock
	if (pixel_format == dml2_420 || pixel_format == dml2_n422) {
		dispclk_per_dscclk  *= 2;
	}

	// dscc - input deserializer
	//1 slice configuration has a single slice stream, 3 cycles (multiplied by pixels per container) to accumulate 1 group
	if (num_slices == 1) {
	  delay += 3;

	  //N422/N420 single slice configuration is a special case; pixel containers arrive every other cycle and data is output on the cycle after every 3rd data transfer
	  //this causes 2 extra cycles of delay (i.e., not 3)
	  if (pixel_format == dml2_420 || pixel_format == dml2_n422) {
		delay += 2;
	  }
	} else { //greater than 1 slice configration has two slice streams, 6 cycles to accumulate 2 groups
	//no need to multiply by pixels per container as first half of first slice line is output faster by dscc_if as
	//the second slice stream has no started yet so we do not need to wait for those pixels to appear at the input
	  delay += 6;
	}

	// dscc - input cdc fifo begin
	delay	   += 1;					// flop data/update address
	uncertainty += 2 * dispclk_per_dscclk; //(2 cycles of transport + metastability delay) * x dscclks per dispclk
	uncertainty += 1 * dispclk_per_dscclk; // 1st stage of sync cell flopping has 1 cycle of uncertainty due to clock skew * dscclks per dispclk
	delay	   += 2 * dispclk_per_dscclk; // 2nd and 3rd stage of sync cell flopping have 2 cycles * x dscclks per dispclk
	delay	   += 1 * dispclk_per_dscclk; // 1 flop data out * x dscclks per dispclk

	// dscc - vblank clock control for dscclk
	if (dscclk_dynamic_gating_en == 1) {
		delay += 1 * dispclk_per_dscclk; //1 cycle delay from first set of pixels until the clock turns on
	}

	// dscc_top - engine logic excluded, see other formula

	// dscc - syntax element cdc fifo begin
	delay	   += 1 * dispclk_per_dscclk; // flop data/update address * 6 dscclks per dispclk
	uncertainty += 2;					// 2 cycles of transport + metastability delay
	uncertainty += 1;					// 1st stage of sync cell flopping has 1 cycle of uncertainty due to clock skew
	delay	   += 2;					// 2nd and 3rd stage of sync cell flopping have 2 cycles * x dscclks per dispclk
	delay	   += 1;					// 1 flop data out

	// dscc_bcl - bitstream construction layer logic exclued, see other formula

	// sft
	delay += 1;

	// return delay and uncertainty
	delay_uncertainty.delay	   = delay;
	delay_uncertainty.uncertainty = uncertainty;
	return delay_uncertainty;
}

// function to compute output pixel delay (delay from DSCCIF output to DSC output) — updated formula (DCN5.1 and newer)
// returns the delay and uncertainty
// valid pixel_format = pixel/color format in the set of {N444, S422, N422, N420}
static delay_uncertainty_t dsc_compute_output_pixel_delay(enum dml2_output_format_class pixel_format, int num_slices, int dscclk_dynamic_gating_en)
{
	// initialize latency value
	int dispclk_per_dscclk;
	int delay				= 0;
	int uncertainty		  = 0;
	delay_uncertainty_t delay_uncertainty;

	//1 slice configuration uses a single slice processor (/3 clock)
	if (num_slices == 1) {
	  dispclk_per_dscclk = 3;
	} else { //greater than 1 slice configuration uses two slice processor (/6 clock)
	  dispclk_per_dscclk = 6;
	}

	// N422 and N420 process 2x pixels per clock
	if (pixel_format == dml2_420 || pixel_format == dml2_n422) {
		dispclk_per_dscclk  *= 2;
	}

	// dscc - input deserializer
	//1 slice configuration has a single slice stream, 3 cycles (multiplied by pixels per container) to accumulate 1 group
	if (num_slices == 1) {
	  delay += 3;

	  //N422/N420 single slice configuration is a special case; pixel containers arrive every other cycle and data is output on the cycle after every 3rd data transfer
	  //this causes 2 extra cycles of delay (i.e., not 3)
	  if (pixel_format == dml2_420 || pixel_format == dml2_n422) {
		delay += 2;
	  }
	} else { //greater than 1 slice configration has two slice streams, 6 cycles to accumulate 2 groups
	//no need to multiply by pixels per container as first half of first slice line is output faster by dscc_if as
	//the second slice stream has no started yet so we do not need to wait for those pixels to appear at the input
	  //it takes 12 cycles for 6 pairs to arrive
	  if (pixel_format == dml2_n422) {
		delay += 12;
	  } else {
		delay += 6;
	  }
	}

	// dscc - input cdc fifo begin
	delay	   += 1;					// flop data/update address
	uncertainty += 1 * dispclk_per_dscclk; // 1st stage of sync cell flopping has 1 cycle of uncertainty due to clock skew * dscclks per dispclk
	uncertainty += 1 * dispclk_per_dscclk; //(1 cycle metastability delay, src synchronous flop) * x dscclks per dispclk
	delay	   += 2 * dispclk_per_dscclk; // 2nd and 3rd stage of sync cell flopping have 2 cycles * x dscclks per dispclk

	// dscc - vblank clock control for dscclk
	if (dscclk_dynamic_gating_en == 1) {
		uncertainty += 1 * dispclk_per_dscclk; //(1 cycle metastability delay, src synchronous flop) * x dscclks per dispclk
		uncertainty += 1 * dispclk_per_dscclk; // 1st stage of sync cell flopping has 1 cycle of uncertainty due to clock skew * dscclks per dispclk

		//counting this real synchronizer delay as uncertainty because it happens in parallel with dsccif/pixel serializer logic
		uncertainty += 2 * dispclk_per_dscclk; // 2nd and 3rd stage of sync cell flopping have 2 cycles * x dscclks per dispclk
		uncertainty += 1 * dispclk_per_dscclk; //1 cycle delay from first set of pixels until the clock turns on
	}

	// dscc_top - engine logic excluded, see other formula

	// dscc - syntax element cdc fifo begin
	delay	   += 1 * dispclk_per_dscclk; // flop data/update address * 6 dscclks per dispclk
	uncertainty += 1;					  // 1st stage of sync cell flopping has 1 cycle of uncertainty due to clock skew
	uncertainty += 1;					  //1 cycle metastability delay, src synchronous flop
	delay	   += 2;					  // 2nd and 3rd stage of sync cell flopping have 2 cycles
	delay	   += 1;					  // 1 flop data out

	// dscc_bcl - bitstream construction layer logic exclued, see other formula

	// sft
	delay += 1;

	// return delay and uncertainty
	delay_uncertainty.delay	   = delay;
	delay_uncertainty.uncertainty = uncertainty;
	return delay_uncertainty;
}

/* -------------------------------------------------------------------------
 * Public functions
 * ------------------------------------------------------------------------- */

// dcn5_dsc_compute_delay_legacy - DSC delay using the original DCN5 formula
// valid bpc          = source bits per component in the set of {8, 10, 12}
// valid bpp          = increments of 1/16 of a bit
//                     min = 6/7/8 in N420/N422/444, respectively
//                     max = such that compression is 1:1
// valid slice_width  = number of pixels per slice line, must be less than or equal to `DSC_MAX_WIDTH/num_slices (or 4096/num_slices in 420 mode)
// valid num_slices   = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
// valid pixel_format = pixel/color format in the set of {N444, S422, N422, N420}
void dcn5_dsc_compute_delay_legacy(delay_uncertainty_t *p, int bpc, float bpp, int slice_width, int num_slices, enum dml2_output_format_class pixel_format, int dscclk_dynamic_gating_en, int dispclk_dynamic_gating_en, int initial_xmit_delay_offset, int group_delay_after_initial_xmit_delay_override_en, int group_delay_after_initial_xmit_delay)
{
	// initialize
	int total_delay		  = 0;
	int total_uncertainty	= 0;

	delay_uncertainty_t delay_uncertainty;
	latency_t dscc_bcl_latency;

	// compute input pixel delay
	total_delay += dsc_compute_input_pixel_delay(pixel_format, num_slices, dispclk_dynamic_gating_en);

	// dscc_pcl delay
	total_delay += dscc_pcl_compute_delay(pixel_format, num_slices);

	// dscc_bcl delay
	dscc_bcl_compute_delay(&dscc_bcl_latency, bpc, bpp, slice_width, num_slices, pixel_format, initial_xmit_delay_offset, group_delay_after_initial_xmit_delay_override_en, group_delay_after_initial_xmit_delay);
	total_delay += dscc_bcl_latency.pixels;

	// compute output delay
	delay_uncertainty = legacy_dsc_compute_output_pixel_delay(pixel_format, num_slices, dscclk_dynamic_gating_en);

	delay_uncertainty.delay += total_delay;
	delay_uncertainty.uncertainty += total_uncertainty;

	p->delay = delay_uncertainty.delay;
	p->uncertainty = delay_uncertainty.uncertainty;
	return;
}

// dcn5_dsc_compute_delay - DSC delay using the updated formula (DCN5.1 and newer)
// valid bpc          = source bits per component in the set of {8, 10, 12}
// valid bpp          = increments of 1/16 of a bit
//                     min = 6/7/8 in N420/N422/444, respectively
//                     max = such that compression is 1:1
// valid slice_width  = number of pixels per slice line, must be less than or equal to `DSC_MAX_WIDTH/num_slices (or 4096/num_slices in 420 mode)
// valid num_slices   = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
// valid pixel_format = pixel/color format in the set of {N444, S422, N422, N420}
void dcn5_dsc_compute_delay(delay_uncertainty_t *p, int bpc, float bpp, int slice_width, int num_slices, enum dml2_output_format_class pixel_format, int dscclk_dynamic_gating_en, int dispclk_dynamic_gating_en, int initial_xmit_delay_offset, int group_delay_after_initial_xmit_delay_override_en, int group_delay_after_initial_xmit_delay)
{
	// initialize
	int total_delay		  = 0;
	int total_uncertainty	= 0;

	delay_uncertainty_t delay_uncertainty;
	latency_t dscc_bcl_latency;

	// compute input pixel delay
	total_delay += dsc_compute_input_pixel_delay(pixel_format, num_slices, dispclk_dynamic_gating_en);

	// dscc_pcl delay
	total_delay += dscc_pcl_compute_delay(pixel_format, num_slices);

	// dscc_bcl delay
	dscc_bcl_compute_delay(&dscc_bcl_latency, bpc, bpp, slice_width, num_slices, pixel_format, initial_xmit_delay_offset, group_delay_after_initial_xmit_delay_override_en, group_delay_after_initial_xmit_delay);
	total_delay += dscc_bcl_latency.pixels;

	// compute output delay
	delay_uncertainty = dsc_compute_output_pixel_delay(pixel_format, num_slices, dscclk_dynamic_gating_en);

	delay_uncertainty.delay += total_delay;
	delay_uncertainty.uncertainty += total_uncertainty;

	p->delay = delay_uncertainty.delay;
	p->uncertainty = delay_uncertainty.uncertainty;
	return;
}
