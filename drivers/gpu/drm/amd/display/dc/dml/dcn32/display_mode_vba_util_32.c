/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "display_mode_vba_util_32.h"
#include "../dml_inline_defs.h"
#include "display_mode_vba_32.h"
#include "../display_mode_lib.h"

#define DCN32_MAX_FMT_420_BUFFER_WIDTH 4096

unsigned int dml32_dscceComputeDelay(
		unsigned int bpc,
		double BPP,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat,
		enum output_encoder_class Output)
{
	// valid bpc         = source bits per component in the set of {8, 10, 12}
	// valid bpp         = increments of 1/16 of a bit
	//                    min = 6/7/8 in N420/N422/444, respectively
	//                    max = such that compression is 1:1
	//valid sliceWidth  = number of pixels per slice line,
	//	must be less than or equal to 5184/numSlices (or 4096/numSlices in 420 mode)
	//valid numSlices   = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
	//valid pixelFormat = pixel/color format in the set of {:N444_RGB, :S422, :N422, :N420}

	// fixed value
	unsigned int rcModelSize = 8192;

	// N422/N420 operate at 2 pixels per clock
	unsigned int pixelsPerClock, lstall, D, initalXmitDelay, w, s, ix, wx, p, l0, a, ax, L,
	Delay, pixels;

	if (pixelFormat == dm_420)
		pixelsPerClock = 2;
	else if (pixelFormat == dm_n422)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
	else
		pixelsPerClock = 1;

	//initial transmit delay as per PPS
	initalXmitDelay = dml_round(rcModelSize / 2.0 / BPP / pixelsPerClock);

	//compute ssm delay
	if (bpc == 8)
		D = 81;
	else if (bpc == 10)
		D = 89;
	else
		D = 113;

	//divide by pixel per cycle to compute slice width as seen by DSC
	w = sliceWidth / pixelsPerClock;

	//422 mode has an additional cycle of delay
	if (pixelFormat == dm_420 || pixelFormat == dm_444 || pixelFormat == dm_n422)
		s = 0;
	else
		s = 1;

	//main calculation for the dscce
	ix = initalXmitDelay + 45;
	wx = (w + 2) / 3;
	p = 3 * wx - w;
	l0 = ix / w;
	a = ix + p * l0;
	ax = (a + 2) / 3 + D + 6 + 1;
	L = (ax + wx - 1) / wx;
	if ((ix % w) == 0 && p != 0)
		lstall = 1;
	else
		lstall = 0;
	Delay = L * wx * (numSlices - 1) + ax + s + lstall + 22;

	//dsc processes 3 pixel containers per cycle and a container can contain 1 or 2 pixels
	pixels = Delay * 3 * pixelsPerClock;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: bpc: %d\n", __func__, bpc);
	dml_print("DML::%s: BPP: %f\n", __func__, BPP);
	dml_print("DML::%s: sliceWidth: %d\n", __func__, sliceWidth);
	dml_print("DML::%s: numSlices: %d\n", __func__, numSlices);
	dml_print("DML::%s: pixelFormat: %d\n", __func__, pixelFormat);
	dml_print("DML::%s: Output: %d\n", __func__, Output);
	dml_print("DML::%s: pixels: %d\n", __func__, pixels);
#endif

	return pixels;
}

unsigned int dml32_dscComputeDelay(enum output_format_class pixelFormat, enum output_encoder_class Output)
{
	unsigned int Delay = 0;

	if (pixelFormat == dm_420) {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 0;
		//   dscc - input deserializer
		Delay = Delay + 3;
		//   dscc gets pixels every other cycle
		Delay = Delay + 2;
		//   dscc - input cdc fifo
		Delay = Delay + 12;
		//   dscc gets pixels every other cycle
		Delay = Delay + 13;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 7;
		//   dscc gets pixels every other cycle
		Delay = Delay + 3;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   sft
		Delay = Delay + 1;
	} else if (pixelFormat == dm_n422 || (pixelFormat != dm_444)) {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 1;
		//   dscc - input deserializer
		Delay = Delay + 5;
		//  dscc - input cdc fifo
		Delay = Delay + 25;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 10;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   sft
		Delay = Delay + 1;
	} else {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 0;
		//   dscc - input deserializer
		Delay = Delay + 3;
		//   dscc - input cdc fifo
		Delay = Delay + 12;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 7;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   sft
		Delay = Delay + 1;
	}

	return Delay;
}


bool IsVertical(enum dm_rotation_angle Scan)
{
	bool is_vert = false;

	if (Scan == dm_rotation_90 || Scan == dm_rotation_90m || Scan == dm_rotation_270 || Scan == dm_rotation_270m)
		is_vert = true;
	else
		is_vert = false;
	return is_vert;
}

void dml32_CalculateSinglePipeDPPCLKAndSCLThroughput(
		double HRatio,
		double HRatioChroma,
		double VRatio,
		double VRatioChroma,
		double MaxDCHUBToPSCLThroughput,
		double MaxPSCLToLBThroughput,
		double PixelClock,
		enum source_format_class SourcePixelFormat,
		unsigned int HTaps,
		unsigned int HTapsChroma,
		unsigned int VTaps,
		unsigned int VTapsChroma,

		/* output */
		double *PSCL_THROUGHPUT,
		double *PSCL_THROUGHPUT_CHROMA,
		double *DPPCLKUsingSingleDPP)
{
	double DPPCLKUsingSingleDPPLuma;
	double DPPCLKUsingSingleDPPChroma;

	if (HRatio > 1) {
		*PSCL_THROUGHPUT = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatio /
				dml_ceil((double) HTaps / 6.0, 1.0));
	} else {
		*PSCL_THROUGHPUT = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
	}

	DPPCLKUsingSingleDPPLuma = PixelClock * dml_max3(VTaps / 6 * dml_min(1, HRatio), HRatio * VRatio /
			*PSCL_THROUGHPUT, 1);

	if ((HTaps > 6 || VTaps > 6) && DPPCLKUsingSingleDPPLuma < 2 * PixelClock)
		DPPCLKUsingSingleDPPLuma = 2 * PixelClock;

	if ((SourcePixelFormat != dm_420_8 && SourcePixelFormat != dm_420_10 && SourcePixelFormat != dm_420_12 &&
			SourcePixelFormat != dm_rgbe_alpha)) {
		*PSCL_THROUGHPUT_CHROMA = 0;
		*DPPCLKUsingSingleDPP = DPPCLKUsingSingleDPPLuma;
	} else {
		if (HRatioChroma > 1) {
			*PSCL_THROUGHPUT_CHROMA = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput *
					HRatioChroma / dml_ceil((double) HTapsChroma / 6.0, 1.0));
		} else {
			*PSCL_THROUGHPUT_CHROMA = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
		}
		DPPCLKUsingSingleDPPChroma = PixelClock * dml_max3(VTapsChroma / 6 * dml_min(1, HRatioChroma),
				HRatioChroma * VRatioChroma / *PSCL_THROUGHPUT_CHROMA, 1);
		if ((HTapsChroma > 6 || VTapsChroma > 6) && DPPCLKUsingSingleDPPChroma < 2 * PixelClock)
			DPPCLKUsingSingleDPPChroma = 2 * PixelClock;
		*DPPCLKUsingSingleDPP = dml_max(DPPCLKUsingSingleDPPLuma, DPPCLKUsingSingleDPPChroma);
	}
}

void dml32_CalculateBytePerPixelAndBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,

		/* Output */
		unsigned int *BytePerPixelY,
		unsigned int *BytePerPixelC,
		double  *BytePerPixelDETY,
		double  *BytePerPixelDETC,
		unsigned int *BlockHeight256BytesY,
		unsigned int *BlockHeight256BytesC,
		unsigned int *BlockWidth256BytesY,
		unsigned int *BlockWidth256BytesC,
		unsigned int *MacroTileHeightY,
		unsigned int *MacroTileHeightC,
		unsigned int *MacroTileWidthY,
		unsigned int *MacroTileWidthC)
{
	if (SourcePixelFormat == dm_444_64) {
		*BytePerPixelDETY = 8;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 8;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_32 || SourcePixelFormat == dm_rgbe) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_16) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 2;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 1;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_rgbe_alpha) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 1;
		*BytePerPixelY = 4;
		*BytePerPixelC = 1;
	} else if (SourcePixelFormat == dm_420_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 2;
		*BytePerPixelY = 1;
		*BytePerPixelC = 2;
	} else if (SourcePixelFormat == dm_420_12) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 4;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else {
		*BytePerPixelDETY = 4.0 / 3;
		*BytePerPixelDETC = 8.0 / 3;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: SourcePixelFormat = %d\n", __func__, SourcePixelFormat);
	dml_print("DML::%s: BytePerPixelDETY = %f\n", __func__, *BytePerPixelDETY);
	dml_print("DML::%s: BytePerPixelDETC = %f\n", __func__, *BytePerPixelDETC);
	dml_print("DML::%s: BytePerPixelY    = %d\n", __func__, *BytePerPixelY);
	dml_print("DML::%s: BytePerPixelC    = %d\n", __func__, *BytePerPixelC);
#endif
	if ((SourcePixelFormat == dm_444_64 || SourcePixelFormat == dm_444_32
			|| SourcePixelFormat == dm_444_16
			|| SourcePixelFormat == dm_444_8
			|| SourcePixelFormat == dm_mono_16
			|| SourcePixelFormat == dm_mono_8
			|| SourcePixelFormat == dm_rgbe)) {
		if (SurfaceTiling == dm_sw_linear)
			*BlockHeight256BytesY = 1;
		else if (SourcePixelFormat == dm_444_64)
			*BlockHeight256BytesY = 4;
		else if (SourcePixelFormat == dm_444_8)
			*BlockHeight256BytesY = 16;
		else
			*BlockHeight256BytesY = 8;

		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else {
		if (SurfaceTiling == dm_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dm_rgbe_alpha) {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 16;
		} else if (SourcePixelFormat == dm_420_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256U / *BytePerPixelC / *BlockHeight256BytesC;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: BlockWidth256BytesY  = %d\n", __func__, *BlockWidth256BytesY);
	dml_print("DML::%s: BlockHeight256BytesY = %d\n", __func__, *BlockHeight256BytesY);
	dml_print("DML::%s: BlockWidth256BytesC  = %d\n", __func__, *BlockWidth256BytesC);
	dml_print("DML::%s: BlockHeight256BytesC = %d\n", __func__, *BlockHeight256BytesC);
#endif

	if (SurfaceTiling == dm_sw_linear) {
		*MacroTileHeightY = *BlockHeight256BytesY;
		*MacroTileWidthY = 256 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0)
			*MacroTileWidthC = 0;
		else
			*MacroTileWidthC = 256 / *BytePerPixelC / *MacroTileHeightC;
	} else if (SurfaceTiling == dm_sw_64kb_d || SurfaceTiling == dm_sw_64kb_d_t ||
			SurfaceTiling == dm_sw_64kb_d_x || SurfaceTiling == dm_sw_64kb_r_x) {
		*MacroTileHeightY = 16 * *BlockHeight256BytesY;
		*MacroTileWidthY = 65536 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = 16 * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0)
			*MacroTileWidthC = 0;
		else
			*MacroTileWidthC = 65536 / *BytePerPixelC / *MacroTileHeightC;
	} else {
		*MacroTileHeightY = 32 * *BlockHeight256BytesY;
		*MacroTileWidthY = 65536 * 4 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = 32 * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0)
			*MacroTileWidthC = 0;
		else
			*MacroTileWidthC = 65536 * 4 / *BytePerPixelC / *MacroTileHeightC;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MacroTileWidthY  = %d\n", __func__, *MacroTileWidthY);
	dml_print("DML::%s: MacroTileHeightY = %d\n", __func__, *MacroTileHeightY);
	dml_print("DML::%s: MacroTileWidthC  = %d\n", __func__, *MacroTileWidthC);
	dml_print("DML::%s: MacroTileHeightC = %d\n", __func__, *MacroTileHeightC);
#endif
} // CalculateBytePerPixelAndBlockSizes

void dml32_CalculateSwathAndDETConfiguration(
		unsigned int DETSizeOverride[],
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
		unsigned int ConfigReturnBufferSizeInKByte,
		unsigned int MaxTotalDETInKByte,
		unsigned int MinCompressedBufferSizeInKByte,
		double ForceSingleDPP,
		unsigned int NumberOfActiveSurfaces,
		unsigned int nomDETInKByte,
		enum unbounded_requesting_policy UseUnboundedRequestingFinal,
		bool DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment,
		unsigned int PixelChunkSizeKBytes,
		unsigned int ROBSizeKBytes,
		unsigned int CompressedBufferSegmentSizeInkByteFinal,
		enum output_encoder_class Output[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double MaximumSwathWidthLuma[],
		double MaximumSwathWidthChroma[],
		enum dm_rotation_angle SourceRotation[],
		bool ViewportStationary[],
		enum source_format_class SourcePixelFormat[],
		enum dm_swizzle_mode SurfaceTiling[],
		unsigned int ViewportWidth[],
		unsigned int ViewportHeight[],
		unsigned int ViewportXStart[],
		unsigned int ViewportYStart[],
		unsigned int ViewportXStartC[],
		unsigned int ViewportYStartC[],
		unsigned int SurfaceWidthY[],
		unsigned int SurfaceWidthC[],
		unsigned int SurfaceHeightY[],
		unsigned int SurfaceHeightC[],
		unsigned int Read256BytesBlockHeightY[],
		unsigned int Read256BytesBlockHeightC[],
		unsigned int Read256BytesBlockWidthY[],
		unsigned int Read256BytesBlockWidthC[],
		enum odm_combine_mode ODMMode[],
		unsigned int BlendingAndTiming[],
		unsigned int BytePerPixY[],
		unsigned int BytePerPixC[],
		double BytePerPixDETY[],
		double BytePerPixDETC[],
		unsigned int HActive[],
		double HRatio[],
		double HRatioChroma[],
		unsigned int DPPPerSurface[],

		/* Output */
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[],
		double SwathWidth[],
		double SwathWidthChroma[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		unsigned int DETBufferSizeInKByte[],
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		bool *UnboundedRequestEnabled,
		unsigned int *CompressedBufferSizeInkByte,
		unsigned int *CompBufReservedSpaceKBytes,
		bool *CompBufReservedSpaceNeedAdjustment,
		bool ViewportSizeSupportPerSurface[],
		bool *ViewportSizeSupport)
{
	unsigned int MaximumSwathHeightY[DC__NUM_DPP__MAX];
	unsigned int MaximumSwathHeightC[DC__NUM_DPP__MAX];
	unsigned int RoundedUpMaxSwathSizeBytesY[DC__NUM_DPP__MAX];
	unsigned int RoundedUpMaxSwathSizeBytesC[DC__NUM_DPP__MAX];
	unsigned int RoundedUpSwathSizeBytesY;
	unsigned int RoundedUpSwathSizeBytesC;
	double SwathWidthdoubleDPP[DC__NUM_DPP__MAX];
	double SwathWidthdoubleDPPChroma[DC__NUM_DPP__MAX];
	unsigned int k;
	unsigned int TotalActiveDPP = 0;
	bool NoChromaSurfaces = true;
	unsigned int DETBufferSizeInKByteForSwathCalculation;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %d\n", __func__, ForceSingleDPP);
	dml_print("DML::%s: ROBSizeKBytes = %d\n", __func__, ROBSizeKBytes);
	dml_print("DML::%s: PixelChunkSizeKBytes = %d\n", __func__, PixelChunkSizeKBytes);
#endif
	dml32_CalculateSwathWidth(ForceSingleDPP,
			NumberOfActiveSurfaces,
			SourcePixelFormat,
			SourceRotation,
			ViewportStationary,
			ViewportWidth,
			ViewportHeight,
			ViewportXStart,
			ViewportYStart,
			ViewportXStartC,
			ViewportYStartC,
			SurfaceWidthY,
			SurfaceWidthC,
			SurfaceHeightY,
			SurfaceHeightC,
			ODMMode,
			BytePerPixY,
			BytePerPixC,
			Read256BytesBlockHeightY,
			Read256BytesBlockHeightC,
			Read256BytesBlockWidthY,
			Read256BytesBlockWidthC,
			BlendingAndTiming,
			HActive,
			HRatio,
			DPPPerSurface,

			/* Output */
			SwathWidthdoubleDPP,
			SwathWidthdoubleDPPChroma,
			SwathWidth,
			SwathWidthChroma,
			MaximumSwathHeightY,
			MaximumSwathHeightC,
			swath_width_luma_ub,
			swath_width_chroma_ub);

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		RoundedUpMaxSwathSizeBytesY[k] = swath_width_luma_ub[k] * BytePerPixDETY[k] * MaximumSwathHeightY[k];
		RoundedUpMaxSwathSizeBytesC[k] = swath_width_chroma_ub[k] * BytePerPixDETC[k] * MaximumSwathHeightC[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d DPPPerSurface = %d\n", __func__, k, DPPPerSurface[k]);
		dml_print("DML::%s: k=%0d swath_width_luma_ub = %d\n", __func__, k, swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%0d BytePerPixDETY = %f\n", __func__, k, BytePerPixDETY[k]);
		dml_print("DML::%s: k=%0d MaximumSwathHeightY = %d\n", __func__, k, MaximumSwathHeightY[k]);
		dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesY = %d\n", __func__, k,
				RoundedUpMaxSwathSizeBytesY[k]);
		dml_print("DML::%s: k=%0d swath_width_chroma_ub = %d\n", __func__, k, swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%0d BytePerPixDETC = %f\n", __func__, k, BytePerPixDETC[k]);
		dml_print("DML::%s: k=%0d MaximumSwathHeightC = %d\n", __func__, k, MaximumSwathHeightC[k]);
		dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesC = %d\n", __func__, k,
				RoundedUpMaxSwathSizeBytesC[k]);
#endif

		if (SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesY[k] = dml_ceil((unsigned int) RoundedUpMaxSwathSizeBytesY[k], 256);
			RoundedUpMaxSwathSizeBytesC[k] = dml_ceil((unsigned int) RoundedUpMaxSwathSizeBytesC[k], 256);
		}
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		TotalActiveDPP = TotalActiveDPP + (ForceSingleDPP ? 1 : DPPPerSurface[k]);
		if (SourcePixelFormat[k] == dm_420_8 || SourcePixelFormat[k] == dm_420_10 ||
				SourcePixelFormat[k] == dm_420_12 || SourcePixelFormat[k] == dm_rgbe_alpha) {
			NoChromaSurfaces = false;
		}
	}

	// By default, just set the reserved space to 2 pixel chunks size
	*CompBufReservedSpaceKBytes = PixelChunkSizeKBytes * 2;

	// if unbounded req is enabled, program reserved space such that the ROB will not hold more than 8 swaths worth of data
	// - assume worst-case compression rate of 4. [ROB size - 8 * swath_size / max_compression ratio]
	// - assume for "narrow" vp case in which the ROB can fit 8 swaths, the DET should be big enough to do full size req
	*CompBufReservedSpaceNeedAdjustment = ((int) ROBSizeKBytes - (int) *CompBufReservedSpaceKBytes) > (int) (RoundedUpMaxSwathSizeBytesY[0]/512);

	if (*CompBufReservedSpaceNeedAdjustment == 1) {
		*CompBufReservedSpaceKBytes = ROBSizeKBytes - RoundedUpMaxSwathSizeBytesY[0]/512;
	}

	#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: CompBufReservedSpaceKBytes          = %d\n",  __func__, *CompBufReservedSpaceKBytes);
		dml_print("DML::%s: CompBufReservedSpaceNeedAdjustment  = %d\n",  __func__, *CompBufReservedSpaceNeedAdjustment);
	#endif

	*UnboundedRequestEnabled = dml32_UnboundedRequest(UseUnboundedRequestingFinal, TotalActiveDPP, NoChromaSurfaces, Output[0], SurfaceTiling[0], *CompBufReservedSpaceNeedAdjustment, DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment);

	dml32_CalculateDETBufferSize(DETSizeOverride,
			UseMALLForPStateChange,
			ForceSingleDPP,
			NumberOfActiveSurfaces,
			*UnboundedRequestEnabled,
			nomDETInKByte,
			MaxTotalDETInKByte,
			ConfigReturnBufferSizeInKByte,
			MinCompressedBufferSizeInKByte,
			CompressedBufferSegmentSizeInkByteFinal,
			SourcePixelFormat,
			ReadBandwidthLuma,
			ReadBandwidthChroma,
			RoundedUpMaxSwathSizeBytesY,
			RoundedUpMaxSwathSizeBytesC,
			DPPPerSurface,

			/* Output */
			DETBufferSizeInKByte,    // per hubp pipe
			CompressedBufferSizeInkByte);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: TotalActiveDPP = %d\n", __func__, TotalActiveDPP);
	dml_print("DML::%s: nomDETInKByte = %d\n", __func__, nomDETInKByte);
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %d\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: UseUnboundedRequestingFinal = %d\n", __func__, UseUnboundedRequestingFinal);
	dml_print("DML::%s: UnboundedRequestEnabled = %d\n", __func__, *UnboundedRequestEnabled);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %d\n", __func__, *CompressedBufferSizeInkByte);
#endif

	*ViewportSizeSupport = true;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {

		DETBufferSizeInKByteForSwathCalculation = (UseMALLForPStateChange[k] ==
				dm_use_mall_pstate_change_phantom_pipe ? 1024 : DETBufferSizeInKByte[k]);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d DETBufferSizeInKByteForSwathCalculation = %d\n", __func__, k,
				DETBufferSizeInKByteForSwathCalculation);
#endif

		if (RoundedUpMaxSwathSizeBytesY[k] + RoundedUpMaxSwathSizeBytesC[k] <=
				DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k];
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY[k];
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC[k];
		} else if (RoundedUpMaxSwathSizeBytesY[k] >= 1.5 * RoundedUpMaxSwathSizeBytesC[k] &&
				RoundedUpMaxSwathSizeBytesY[k] / 2 + RoundedUpMaxSwathSizeBytesC[k] <=
				DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY[k] / 2;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC[k];
		} else if (RoundedUpMaxSwathSizeBytesY[k] < 1.5 * RoundedUpMaxSwathSizeBytesC[k] &&
				RoundedUpMaxSwathSizeBytesY[k] + RoundedUpMaxSwathSizeBytesC[k] / 2 <=
				DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k];
			SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY[k];
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC[k] / 2;
		} else {
			SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY[k] / 2;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC[k] / 2;
		}

		if ((RoundedUpMaxSwathSizeBytesY[k] / 2 + RoundedUpMaxSwathSizeBytesC[k] / 2 >
				DETBufferSizeInKByteForSwathCalculation * 1024 / 2)
				|| SwathWidth[k] > MaximumSwathWidthLuma[k] || (SwathHeightC[k] > 0 &&
						SwathWidthChroma[k] > MaximumSwathWidthChroma[k])) {
			*ViewportSizeSupport = false;
			ViewportSizeSupportPerSurface[k] = false;
		} else {
			ViewportSizeSupportPerSurface[k] = true;
		}

		if (SwathHeightC[k] == 0) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d All DET for plane0\n", __func__, k);
#endif
			DETBufferSizeY[k] = DETBufferSizeInKByte[k] * 1024;
			DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY <= 1.5 * RoundedUpSwathSizeBytesC) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d Half DET for plane0, half for plane1\n", __func__, k);
#endif
			DETBufferSizeY[k] = DETBufferSizeInKByte[k] * 1024 / 2;
			DETBufferSizeC[k] = DETBufferSizeInKByte[k] * 1024 / 2;
		} else {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d 2/3 DET for plane0, 1/3 for plane1\n", __func__, k);
#endif
			DETBufferSizeY[k] = dml_floor(DETBufferSizeInKByte[k] * 1024 * 2 / 3, 1024);
			DETBufferSizeC[k] = DETBufferSizeInKByte[k] * 1024 - DETBufferSizeY[k];
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d SwathHeightY = %d\n", __func__, k, SwathHeightY[k]);
		dml_print("DML::%s: k=%0d SwathHeightC = %d\n", __func__, k, SwathHeightC[k]);
		dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesY = %d\n", __func__,
				k, RoundedUpMaxSwathSizeBytesY[k]);
		dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesC = %d\n", __func__,
				k, RoundedUpMaxSwathSizeBytesC[k]);
		dml_print("DML::%s: k=%0d RoundedUpSwathSizeBytesY = %d\n", __func__, k, RoundedUpSwathSizeBytesY);
		dml_print("DML::%s: k=%0d RoundedUpSwathSizeBytesC = %d\n", __func__, k, RoundedUpSwathSizeBytesC);
		dml_print("DML::%s: k=%0d DETBufferSizeInKByte = %d\n", __func__, k, DETBufferSizeInKByte[k]);
		dml_print("DML::%s: k=%0d DETBufferSizeY = %d\n", __func__, k, DETBufferSizeY[k]);
		dml_print("DML::%s: k=%0d DETBufferSizeC = %d\n", __func__, k, DETBufferSizeC[k]);
		dml_print("DML::%s: k=%0d ViewportSizeSupportPerSurface = %d\n", __func__, k,
				ViewportSizeSupportPerSurface[k]);
#endif

	}
} // CalculateSwathAndDETConfiguration

void dml32_CalculateSwathWidth(
		bool				ForceSingleDPP,
		unsigned int			NumberOfActiveSurfaces,
		enum source_format_class	SourcePixelFormat[],
		enum dm_rotation_angle		SourceRotation[],
		bool				ViewportStationary[],
		unsigned int			ViewportWidth[],
		unsigned int			ViewportHeight[],
		unsigned int			ViewportXStart[],
		unsigned int			ViewportYStart[],
		unsigned int			ViewportXStartC[],
		unsigned int			ViewportYStartC[],
		unsigned int			SurfaceWidthY[],
		unsigned int			SurfaceWidthC[],
		unsigned int			SurfaceHeightY[],
		unsigned int			SurfaceHeightC[],
		enum odm_combine_mode		ODMMode[],
		unsigned int			BytePerPixY[],
		unsigned int			BytePerPixC[],
		unsigned int			Read256BytesBlockHeightY[],
		unsigned int			Read256BytesBlockHeightC[],
		unsigned int			Read256BytesBlockWidthY[],
		unsigned int			Read256BytesBlockWidthC[],
		unsigned int			BlendingAndTiming[],
		unsigned int			HActive[],
		double				HRatio[],
		unsigned int			DPPPerSurface[],

		/* Output */
		double				SwathWidthdoubleDPPY[],
		double				SwathWidthdoubleDPPC[],
		double				SwathWidthY[], // per-pipe
		double				SwathWidthC[], // per-pipe
		unsigned int			MaximumSwathHeightY[],
		unsigned int			MaximumSwathHeightC[],
		unsigned int			swath_width_luma_ub[], // per-pipe
		unsigned int			swath_width_chroma_ub[]) // per-pipe
{
	unsigned int k, j;
	enum odm_combine_mode MainSurfaceODMMode;

	unsigned int surface_width_ub_l;
	unsigned int surface_height_ub_l;
	unsigned int surface_width_ub_c = 0;
	unsigned int surface_height_ub_c = 0;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %d\n", __func__, ForceSingleDPP);
	dml_print("DML::%s: NumberOfActiveSurfaces = %d\n", __func__, NumberOfActiveSurfaces);
#endif

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!IsVertical(SourceRotation[k]))
			SwathWidthdoubleDPPY[k] = ViewportWidth[k];
		else
			SwathWidthdoubleDPPY[k] = ViewportHeight[k];

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d ViewportWidth=%d\n", __func__, k, ViewportWidth[k]);
		dml_print("DML::%s: k=%d ViewportHeight=%d\n", __func__, k, ViewportHeight[k]);
#endif

		MainSurfaceODMMode = ODMMode[k];
		for (j = 0; j < NumberOfActiveSurfaces; ++j) {
			if (BlendingAndTiming[k] == j)
				MainSurfaceODMMode = ODMMode[j];
		}

		if (ForceSingleDPP) {
			SwathWidthY[k] = SwathWidthdoubleDPPY[k];
		} else {
			if (MainSurfaceODMMode == dm_odm_combine_mode_4to1) {
				SwathWidthY[k] = dml_min(SwathWidthdoubleDPPY[k],
						dml_round(HActive[k] / 4.0 * HRatio[k]));
			} else if (MainSurfaceODMMode == dm_odm_combine_mode_2to1) {
				SwathWidthY[k] = dml_min(SwathWidthdoubleDPPY[k],
						dml_round(HActive[k] / 2.0 * HRatio[k]));
			} else if (DPPPerSurface[k] == 2) {
				SwathWidthY[k] = SwathWidthdoubleDPPY[k] / 2;
			} else {
				SwathWidthY[k] = SwathWidthdoubleDPPY[k];
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d HActive=%d\n", __func__, k, HActive[k]);
		dml_print("DML::%s: k=%d HRatio=%f\n", __func__, k, HRatio[k]);
		dml_print("DML::%s: k=%d MainSurfaceODMMode=%d\n", __func__, k, MainSurfaceODMMode);
		dml_print("DML::%s: k=%d SwathWidthdoubleDPPY=%d\n", __func__, k, SwathWidthdoubleDPPY[k]);
		dml_print("DML::%s: k=%d SwathWidthY=%d\n", __func__, k, SwathWidthY[k]);
#endif

		if (SourcePixelFormat[k] == dm_420_8 || SourcePixelFormat[k] == dm_420_10 ||
				SourcePixelFormat[k] == dm_420_12) {
			SwathWidthC[k] = SwathWidthY[k] / 2;
			SwathWidthdoubleDPPC[k] = SwathWidthdoubleDPPY[k] / 2;
		} else {
			SwathWidthC[k] = SwathWidthY[k];
			SwathWidthdoubleDPPC[k] = SwathWidthdoubleDPPY[k];
		}

		if (ForceSingleDPP == true) {
			SwathWidthY[k] = SwathWidthdoubleDPPY[k];
			SwathWidthC[k] = SwathWidthdoubleDPPC[k];
		}

		surface_width_ub_l  = dml_ceil(SurfaceWidthY[k], Read256BytesBlockWidthY[k]);
		surface_height_ub_l = dml_ceil(SurfaceHeightY[k], Read256BytesBlockHeightY[k]);

		if (!IsVertical(SourceRotation[k])) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = dml_min(surface_width_ub_l,
						dml_floor(ViewportXStart[k] +
								SwathWidthY[k] +
								Read256BytesBlockWidthY[k] - 1,
								Read256BytesBlockWidthY[k]) -
								dml_floor(ViewportXStart[k],
								Read256BytesBlockWidthY[k]));
			} else {
				swath_width_luma_ub[k] = dml_min(surface_width_ub_l,
						dml_ceil(SwathWidthY[k] - 1,
								Read256BytesBlockWidthY[k]) +
								Read256BytesBlockWidthY[k]);
			}
			if (BytePerPixC[k] > 0) {
				surface_width_ub_c  = dml_ceil(SurfaceWidthC[k], Read256BytesBlockWidthC[k]);
				if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = dml_min(surface_width_ub_c,
							dml_floor(ViewportXStartC[k] + SwathWidthC[k] +
									Read256BytesBlockWidthC[k] - 1,
									Read256BytesBlockWidthC[k]) -
									dml_floor(ViewportXStartC[k],
									Read256BytesBlockWidthC[k]));
				} else {
					swath_width_chroma_ub[k] = dml_min(surface_width_ub_c,
							dml_ceil(SwathWidthC[k] - 1,
								Read256BytesBlockWidthC[k]) +
								Read256BytesBlockWidthC[k]);
				}
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];

			if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = dml_min(surface_height_ub_l, dml_floor(ViewportYStart[k] +
						SwathWidthY[k] + Read256BytesBlockHeightY[k] - 1,
						Read256BytesBlockHeightY[k]) -
						dml_floor(ViewportYStart[k], Read256BytesBlockHeightY[k]));
			} else {
				swath_width_luma_ub[k] = dml_min(surface_height_ub_l, dml_ceil(SwathWidthY[k] - 1,
						Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]);
			}
			if (BytePerPixC[k] > 0) {
				surface_height_ub_c = dml_ceil(SurfaceHeightC[k], Read256BytesBlockHeightC[k]);
				if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = dml_min(surface_height_ub_c,
							dml_floor(ViewportYStartC[k] + SwathWidthC[k] +
									Read256BytesBlockHeightC[k] - 1,
									Read256BytesBlockHeightC[k]) -
									dml_floor(ViewportYStartC[k],
											Read256BytesBlockHeightC[k]));
				} else {
					swath_width_chroma_ub[k] = dml_min(surface_height_ub_c,
							dml_ceil(SwathWidthC[k] - 1, Read256BytesBlockHeightC[k]) +
							Read256BytesBlockHeightC[k]);
				}
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d surface_width_ub_l=%0d\n", __func__, k, surface_width_ub_l);
		dml_print("DML::%s: k=%d surface_height_ub_l=%0d\n", __func__, k, surface_height_ub_l);
		dml_print("DML::%s: k=%d surface_width_ub_c=%0d\n", __func__, k, surface_width_ub_c);
		dml_print("DML::%s: k=%d surface_height_ub_c=%0d\n", __func__, k, surface_height_ub_c);
		dml_print("DML::%s: k=%d Read256BytesBlockWidthY=%0d\n", __func__, k, Read256BytesBlockWidthY[k]);
		dml_print("DML::%s: k=%d Read256BytesBlockHeightY=%0d\n", __func__, k, Read256BytesBlockHeightY[k]);
		dml_print("DML::%s: k=%d Read256BytesBlockWidthC=%0d\n", __func__, k, Read256BytesBlockWidthC[k]);
		dml_print("DML::%s: k=%d Read256BytesBlockHeightC=%0d\n", __func__, k, Read256BytesBlockHeightC[k]);
		dml_print("DML::%s: k=%d ViewportStationary=%0d\n", __func__, k, ViewportStationary[k]);
		dml_print("DML::%s: k=%d DPPPerSurface=%0d\n", __func__, k, DPPPerSurface[k]);
		dml_print("DML::%s: k=%d swath_width_luma_ub=%0d\n", __func__, k, swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%d swath_width_chroma_ub=%0d\n", __func__, k, swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%d MaximumSwathHeightY=%0d\n", __func__, k, MaximumSwathHeightY[k]);
		dml_print("DML::%s: k=%d MaximumSwathHeightC=%0d\n", __func__, k, MaximumSwathHeightC[k]);
#endif

	}
} // CalculateSwathWidth

bool dml32_UnboundedRequest(enum unbounded_requesting_policy UseUnboundedRequestingFinal,
			unsigned int TotalNumberOfActiveDPP,
			bool NoChroma,
			enum output_encoder_class Output,
			enum dm_swizzle_mode SurfaceTiling,
			bool CompBufReservedSpaceNeedAdjustment,
			bool DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment)
{
	bool ret_val = false;

	ret_val = (UseUnboundedRequestingFinal != dm_unbounded_requesting_disable &&
			TotalNumberOfActiveDPP == 1 && NoChroma);
	if (UseUnboundedRequestingFinal == dm_unbounded_requesting_edp_only && Output != dm_edp)
		ret_val = false;

	if (SurfaceTiling == dm_sw_linear)
		ret_val = false;

	if (CompBufReservedSpaceNeedAdjustment == 1 && DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment)
		ret_val = false;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: CompBufReservedSpaceNeedAdjustment  = %d\n",  __func__, CompBufReservedSpaceNeedAdjustment);
	dml_print("DML::%s: DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment  = %d\n",  __func__, DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment);
	dml_print("DML::%s: ret_val = %d\n",  __func__, ret_val);
#endif

	return (ret_val);
}

void dml32_CalculateDETBufferSize(
		unsigned int DETSizeOverride[],
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
		bool ForceSingleDPP,
		unsigned int NumberOfActiveSurfaces,
		bool UnboundedRequestEnabled,
		unsigned int nomDETInKByte,
		unsigned int MaxTotalDETInKByte,
		unsigned int ConfigReturnBufferSizeInKByte,
		unsigned int MinCompressedBufferSizeInKByte,
		unsigned int CompressedBufferSegmentSizeInkByteFinal,
		enum source_format_class SourcePixelFormat[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		unsigned int RoundedUpMaxSwathSizeBytesY[],
		unsigned int RoundedUpMaxSwathSizeBytesC[],
		unsigned int DPPPerSurface[],
		/* Output */
		unsigned int DETBufferSizeInKByte[],
		unsigned int *CompressedBufferSizeInkByte)
{
	unsigned int DETBufferSizePoolInKByte;
	unsigned int NextDETBufferPieceInKByte;
	bool DETPieceAssignedToThisSurfaceAlready[DC__NUM_DPP__MAX];
	bool NextPotentialSurfaceToAssignDETPieceFound;
	unsigned int NextSurfaceToAssignDETPiece;
	double TotalBandwidth;
	double BandwidthOfSurfacesNotAssignedDETPiece;
	unsigned int max_minDET;
	unsigned int minDET;
	unsigned int minDET_pipe;
	unsigned int j, k;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %d\n", __func__, ForceSingleDPP);
	dml_print("DML::%s: nomDETInKByte = %d\n", __func__, nomDETInKByte);
	dml_print("DML::%s: NumberOfActiveSurfaces = %d\n", __func__, NumberOfActiveSurfaces);
	dml_print("DML::%s: UnboundedRequestEnabled = %d\n", __func__, UnboundedRequestEnabled);
	dml_print("DML::%s: MaxTotalDETInKByte = %d\n", __func__, MaxTotalDETInKByte);
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %d\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: MinCompressedBufferSizeInKByte = %d\n", __func__, MinCompressedBufferSizeInKByte);
	dml_print("DML::%s: CompressedBufferSegmentSizeInkByteFinal = %d\n", __func__,
			CompressedBufferSegmentSizeInkByteFinal);
#endif

	// Note: Will use default det size if that fits 2 swaths
	if (UnboundedRequestEnabled) {
		if (DETSizeOverride[0] > 0) {
			DETBufferSizeInKByte[0] = DETSizeOverride[0];
		} else {
			DETBufferSizeInKByte[0] = dml_max(nomDETInKByte, dml_ceil(2.0 *
					((double) RoundedUpMaxSwathSizeBytesY[0] +
							(double) RoundedUpMaxSwathSizeBytesC[0]) / 1024.0, 64.0));
		}
		*CompressedBufferSizeInkByte = ConfigReturnBufferSizeInKByte - DETBufferSizeInKByte[0];
	} else {
		DETBufferSizePoolInKByte = MaxTotalDETInKByte;
		for (k = 0; k < NumberOfActiveSurfaces; ++k) {
			DETBufferSizeInKByte[k] = nomDETInKByte;
			if (SourcePixelFormat[k] == dm_420_8 || SourcePixelFormat[k] == dm_420_10 ||
					SourcePixelFormat[k] == dm_420_12) {
				max_minDET = nomDETInKByte - 64;
			} else {
				max_minDET = nomDETInKByte;
			}
			minDET = 128;
			minDET_pipe = 0;

			// add DET resource until can hold 2 full swaths
			while (minDET <= max_minDET && minDET_pipe == 0) {
				if (2.0 * ((double) RoundedUpMaxSwathSizeBytesY[k] +
						(double) RoundedUpMaxSwathSizeBytesC[k]) / 1024.0 <= minDET)
					minDET_pipe = minDET;
				minDET = minDET + 64;
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d minDET        = %d\n", __func__, k, minDET);
			dml_print("DML::%s: k=%0d max_minDET    = %d\n", __func__, k, max_minDET);
			dml_print("DML::%s: k=%0d minDET_pipe   = %d\n", __func__, k, minDET_pipe);
			dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesY = %d\n", __func__, k,
					RoundedUpMaxSwathSizeBytesY[k]);
			dml_print("DML::%s: k=%0d RoundedUpMaxSwathSizeBytesC = %d\n", __func__, k,
					RoundedUpMaxSwathSizeBytesC[k]);
#endif

			if (minDET_pipe == 0) {
				minDET_pipe = dml_max(128, dml_ceil(((double)RoundedUpMaxSwathSizeBytesY[k] +
						(double)RoundedUpMaxSwathSizeBytesC[k]) / 1024.0, 64));
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%0d minDET_pipe = %d (assume each plane take half DET)\n",
						__func__, k, minDET_pipe);
#endif
			}

			if (UseMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe) {
				DETBufferSizeInKByte[k] = 0;
			} else if (DETSizeOverride[k] > 0) {
				DETBufferSizeInKByte[k] = DETSizeOverride[k];
				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte -
						(ForceSingleDPP ? 1 : DPPPerSurface[k]) * DETSizeOverride[k];
			} else if ((ForceSingleDPP ? 1 : DPPPerSurface[k]) * minDET_pipe <= DETBufferSizePoolInKByte) {
				DETBufferSizeInKByte[k] = minDET_pipe;
				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte -
						(ForceSingleDPP ? 1 : DPPPerSurface[k]) * minDET_pipe;
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%d DPPPerSurface = %d\n", __func__, k, DPPPerSurface[k]);
			dml_print("DML::%s: k=%d DETSizeOverride = %d\n", __func__, k, DETSizeOverride[k]);
			dml_print("DML::%s: k=%d DETBufferSizeInKByte = %d\n", __func__, k, DETBufferSizeInKByte[k]);
			dml_print("DML::%s: DETBufferSizePoolInKByte = %d\n", __func__, DETBufferSizePoolInKByte);
#endif
		}

		TotalBandwidth = 0;
		for (k = 0; k < NumberOfActiveSurfaces; ++k) {
			if (UseMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe)
				TotalBandwidth = TotalBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: --- Before bandwidth adjustment ---\n", __func__);
		for (uint k = 0; k < NumberOfActiveSurfaces; ++k)
			dml_print("DML::%s: k=%d DETBufferSizeInKByte   = %d\n", __func__, k, DETBufferSizeInKByte[k]);
		dml_print("DML::%s: --- DET allocation with bandwidth ---\n", __func__);
		dml_print("DML::%s: TotalBandwidth = %f\n", __func__, TotalBandwidth);
#endif
		BandwidthOfSurfacesNotAssignedDETPiece = TotalBandwidth;
		for (k = 0; k < NumberOfActiveSurfaces; ++k) {

			if (UseMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe) {
				DETPieceAssignedToThisSurfaceAlready[k] = true;
			} else if (DETSizeOverride[k] > 0 || (((double) (ForceSingleDPP ? 1 : DPPPerSurface[k]) *
					(double) DETBufferSizeInKByte[k] / (double) MaxTotalDETInKByte) >=
					((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / TotalBandwidth))) {
				DETPieceAssignedToThisSurfaceAlready[k] = true;
				BandwidthOfSurfacesNotAssignedDETPiece = BandwidthOfSurfacesNotAssignedDETPiece -
						ReadBandwidthLuma[k] - ReadBandwidthChroma[k];
			} else {
				DETPieceAssignedToThisSurfaceAlready[k] = false;
			}
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%d DETPieceAssignedToThisSurfaceAlready = %d\n", __func__, k,
					DETPieceAssignedToThisSurfaceAlready[k]);
			dml_print("DML::%s: k=%d BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, k,
					BandwidthOfSurfacesNotAssignedDETPiece);
#endif
		}

		for (j = 0; j < NumberOfActiveSurfaces; ++j) {
			NextPotentialSurfaceToAssignDETPieceFound = false;
			NextSurfaceToAssignDETPiece = 0;

			for (k = 0; k < NumberOfActiveSurfaces; ++k) {
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%d k=%d, ReadBandwidthLuma[k] = %f\n", __func__, j, k,
						ReadBandwidthLuma[k]);
				dml_print("DML::%s: j=%d k=%d, ReadBandwidthChroma[k] = %f\n", __func__, j, k,
						ReadBandwidthChroma[k]);
				dml_print("DML::%s: j=%d k=%d, ReadBandwidthLuma[Next] = %f\n", __func__, j, k,
						ReadBandwidthLuma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%d k=%d, ReadBandwidthChroma[Next] = %f\n", __func__, j, k,
						ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%d k=%d, NextSurfaceToAssignDETPiece = %d\n", __func__, j, k,
						NextSurfaceToAssignDETPiece);
#endif
				if (!DETPieceAssignedToThisSurfaceAlready[k] &&
						(!NextPotentialSurfaceToAssignDETPieceFound ||
						ReadBandwidthLuma[k] + ReadBandwidthChroma[k] <
						ReadBandwidthLuma[NextSurfaceToAssignDETPiece] +
						ReadBandwidthChroma[NextSurfaceToAssignDETPiece])) {
					NextSurfaceToAssignDETPiece = k;
					NextPotentialSurfaceToAssignDETPieceFound = true;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%d k=%d, DETPieceAssignedToThisSurfaceAlready = %d\n",
						__func__, j, k, DETPieceAssignedToThisSurfaceAlready[k]);
				dml_print("DML::%s: j=%d k=%d, NextPotentialSurfaceToAssignDETPieceFound = %d\n",
						__func__, j, k, NextPotentialSurfaceToAssignDETPieceFound);
#endif
			}

			if (NextPotentialSurfaceToAssignDETPieceFound) {
				// Note: To show the banker's rounding behavior in VBA and also the fact
				// that the DET buffer size varies due to precision issue
				//
				//double tmp1 =  ((double) DETBufferSizePoolInKByte *
				// (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] +
				// ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) /
				// BandwidthOfSurfacesNotAssignedDETPiece /
				// ((ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0));
				//double tmp2 =  dml_round((double) DETBufferSizePoolInKByte *
				// (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] +
				// ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) /
				 //BandwidthOfSurfacesNotAssignedDETPiece /
				// ((ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0));
				//
				//dml_print("DML::%s: j=%d, tmp1 = %f\n", __func__, j, tmp1);
				//dml_print("DML::%s: j=%d, tmp2 = %f\n", __func__, j, tmp2);

				NextDETBufferPieceInKByte = dml_min(
					dml_round((double) DETBufferSizePoolInKByte *
						(ReadBandwidthLuma[NextSurfaceToAssignDETPiece] +
						ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) /
						BandwidthOfSurfacesNotAssignedDETPiece /
						((ForceSingleDPP ? 1 :
								DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0)) *
						(ForceSingleDPP ? 1 :
								DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0,
						dml_floor((double) DETBufferSizePoolInKByte,
						(ForceSingleDPP ? 1 :
								DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0));

				// Above calculation can assign the entire DET buffer allocation to a single pipe.
				// We should limit the per-pipe DET size to the nominal / max per pipe.
				if (NextDETBufferPieceInKByte > nomDETInKByte * (ForceSingleDPP ? 1 : DPPPerSurface[k])) {
					if (DETBufferSizeInKByte[NextSurfaceToAssignDETPiece] <
							nomDETInKByte * (ForceSingleDPP ? 1 : DPPPerSurface[k])) {
						NextDETBufferPieceInKByte = nomDETInKByte * (ForceSingleDPP ? 1 : DPPPerSurface[k]) -
								DETBufferSizeInKByte[NextSurfaceToAssignDETPiece];
					} else {
						// Case where DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]
						// already has the max per-pipe value
						NextDETBufferPieceInKByte = 0;
					}
				}

#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%0d, DETBufferSizePoolInKByte = %d\n", __func__, j,
					DETBufferSizePoolInKByte);
				dml_print("DML::%s: j=%0d, NextSurfaceToAssignDETPiece = %d\n", __func__, j,
					NextSurfaceToAssignDETPiece);
				dml_print("DML::%s: j=%0d, ReadBandwidthLuma[%0d] = %f\n", __func__, j,
					NextSurfaceToAssignDETPiece, ReadBandwidthLuma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%0d, ReadBandwidthChroma[%0d] = %f\n", __func__, j,
					NextSurfaceToAssignDETPiece, ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%0d, BandwidthOfSurfacesNotAssignedDETPiece = %f\n",
					__func__, j, BandwidthOfSurfacesNotAssignedDETPiece);
				dml_print("DML::%s: j=%0d, NextDETBufferPieceInKByte = %d\n", __func__, j,
					NextDETBufferPieceInKByte);
				dml_print("DML::%s: j=%0d, DETBufferSizeInKByte[%0d] increases from %0d ",
					__func__, j, NextSurfaceToAssignDETPiece,
					DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]);
#endif

				DETBufferSizeInKByte[NextSurfaceToAssignDETPiece] =
						DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]
						+ NextDETBufferPieceInKByte
						/ (ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]);
#ifdef __DML_VBA_DEBUG__
				dml_print("to %0d\n", DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]);
#endif

				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte - NextDETBufferPieceInKByte;
				DETPieceAssignedToThisSurfaceAlready[NextSurfaceToAssignDETPiece] = true;
				BandwidthOfSurfacesNotAssignedDETPiece = BandwidthOfSurfacesNotAssignedDETPiece -
						(ReadBandwidthLuma[NextSurfaceToAssignDETPiece] +
								ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
			}
		}
		*CompressedBufferSizeInkByte = MinCompressedBufferSizeInKByte;
	}
	*CompressedBufferSizeInkByte = *CompressedBufferSizeInkByte * CompressedBufferSegmentSizeInkByteFinal / 64;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: --- After bandwidth adjustment ---\n", __func__);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %d\n", __func__, *CompressedBufferSizeInkByte);
	for (uint k = 0; k < NumberOfActiveSurfaces; ++k) {
		dml_print("DML::%s: k=%d DETBufferSizeInKByte = %d (TotalReadBandWidth=%f)\n",
				__func__, k, DETBufferSizeInKByte[k], ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);
	}
#endif
} // CalculateDETBufferSize

void dml32_CalculateODMMode(
		unsigned int MaximumPixelsPerLinePerDSCUnit,
		unsigned int HActive,
		enum output_format_class OutFormat,
		enum output_encoder_class Output,
		enum odm_combine_policy ODMUse,
		double StateDispclk,
		double MaxDispclk,
		bool DSCEnable,
		unsigned int TotalNumberOfActiveDPP,
		unsigned int MaxNumDPP,
		double PixelClock,
		double DISPCLKDPPCLKDSCCLKDownSpreading,
		double DISPCLKRampingMargin,
		double DISPCLKDPPCLKVCOSpeed,
		unsigned int NumberOfDSCSlices,

		/* Output */
		bool *TotalAvailablePipesSupport,
		unsigned int *NumberOfDPP,
		enum odm_combine_mode *ODMMode,
		double *RequiredDISPCLKPerSurface)
{

	double SurfaceRequiredDISPCLKWithoutODMCombine;
	double SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
	double SurfaceRequiredDISPCLKWithODMCombineFourToOne;

	SurfaceRequiredDISPCLKWithoutODMCombine = dml32_CalculateRequiredDispclk(dm_odm_combine_mode_disabled,
			PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed,
			MaxDispclk);
	SurfaceRequiredDISPCLKWithODMCombineTwoToOne = dml32_CalculateRequiredDispclk(dm_odm_combine_mode_2to1,
			PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed,
			MaxDispclk);
	SurfaceRequiredDISPCLKWithODMCombineFourToOne = dml32_CalculateRequiredDispclk(dm_odm_combine_mode_4to1,
			PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed,
			MaxDispclk);
	*TotalAvailablePipesSupport = true;
	*ODMMode = dm_odm_combine_mode_disabled; // initialize as disable

	if (ODMUse == dm_odm_combine_policy_none)
		*ODMMode = dm_odm_combine_mode_disabled;

	*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithoutODMCombine;
	*NumberOfDPP = 0;

	// FIXME check ODMUse == "" condition does it mean bypass or Gabriel means something like don't care??
	// (ODMUse == "" || ODMUse == "CombineAsNeeded")

	if (!(Output == dm_hdmi || Output == dm_dp || Output == dm_edp) && (ODMUse == dm_odm_combine_policy_4to1 ||
			((SurfaceRequiredDISPCLKWithODMCombineTwoToOne > StateDispclk ||
					(DSCEnable && (HActive > 2 * MaximumPixelsPerLinePerDSCUnit))
					|| NumberOfDSCSlices > 8)))) {
		if (TotalNumberOfActiveDPP + 4 <= MaxNumDPP) {
			*ODMMode = dm_odm_combine_mode_4to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineFourToOne;
			*NumberOfDPP = 4;
		} else {
			*TotalAvailablePipesSupport = false;
		}
	} else if (Output != dm_hdmi && (ODMUse == dm_odm_combine_policy_2to1 ||
			(((SurfaceRequiredDISPCLKWithoutODMCombine > StateDispclk &&
					SurfaceRequiredDISPCLKWithODMCombineTwoToOne <= StateDispclk) ||
					(DSCEnable && (HActive > MaximumPixelsPerLinePerDSCUnit))
					|| (NumberOfDSCSlices <= 8 && NumberOfDSCSlices > 4))))) {
		if (TotalNumberOfActiveDPP + 2 <= MaxNumDPP) {
			*ODMMode = dm_odm_combine_mode_2to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
			*NumberOfDPP = 2;
		} else {
			*TotalAvailablePipesSupport = false;
		}
	} else {
		if (TotalNumberOfActiveDPP + 1 <= MaxNumDPP)
			*NumberOfDPP = 1;
		else
			*TotalAvailablePipesSupport = false;
	}
	if (OutFormat == dm_420 && HActive > DCN32_MAX_FMT_420_BUFFER_WIDTH &&
			ODMUse != dm_odm_combine_policy_4to1) {
		if (HActive > DCN32_MAX_FMT_420_BUFFER_WIDTH * 4) {
			*ODMMode = dm_odm_combine_mode_disabled;
			*NumberOfDPP = 0;
			*TotalAvailablePipesSupport = false;
		} else if (HActive > DCN32_MAX_FMT_420_BUFFER_WIDTH * 2 ||
				*ODMMode == dm_odm_combine_mode_4to1) {
			*ODMMode = dm_odm_combine_mode_4to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineFourToOne;
			*NumberOfDPP = 4;
		} else {
			*ODMMode = dm_odm_combine_mode_2to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
			*NumberOfDPP = 2;
		}
	}
	if (Output == dm_hdmi && OutFormat == dm_420 &&
			HActive > DCN32_MAX_FMT_420_BUFFER_WIDTH) {
		*ODMMode = dm_odm_combine_mode_disabled;
		*NumberOfDPP = 0;
		*TotalAvailablePipesSupport = false;
	}
}

double dml32_CalculateRequiredDispclk(
		enum odm_combine_mode ODMMode,
		double PixelClock,
		double DISPCLKDPPCLKDSCCLKDownSpreading,
		double DISPCLKRampingMargin,
		double DISPCLKDPPCLKVCOSpeed,
		double MaxDispclk)
{
	double RequiredDispclk = 0.;
	double PixelClockAfterODM;
	double DISPCLKWithRampingRoundedToDFSGranularity;
	double DISPCLKWithoutRampingRoundedToDFSGranularity;
	double MaxDispclkRoundedDownToDFSGranularity;

	if (ODMMode == dm_odm_combine_mode_4to1)
		PixelClockAfterODM = PixelClock / 4;
	else if (ODMMode == dm_odm_combine_mode_2to1)
		PixelClockAfterODM = PixelClock / 2;
	else
		PixelClockAfterODM = PixelClock;


	DISPCLKWithRampingRoundedToDFSGranularity = dml32_RoundToDFSGranularity(
			PixelClockAfterODM * (1 + DISPCLKDPPCLKDSCCLKDownSpreading / 100)
					* (1 + DISPCLKRampingMargin / 100), 1, DISPCLKDPPCLKVCOSpeed);

	DISPCLKWithoutRampingRoundedToDFSGranularity = dml32_RoundToDFSGranularity(
			PixelClockAfterODM * (1 + DISPCLKDPPCLKDSCCLKDownSpreading / 100), 1, DISPCLKDPPCLKVCOSpeed);

	MaxDispclkRoundedDownToDFSGranularity = dml32_RoundToDFSGranularity(MaxDispclk, 0, DISPCLKDPPCLKVCOSpeed);

	if (DISPCLKWithoutRampingRoundedToDFSGranularity > MaxDispclkRoundedDownToDFSGranularity)
		RequiredDispclk = DISPCLKWithoutRampingRoundedToDFSGranularity;
	else if (DISPCLKWithRampingRoundedToDFSGranularity > MaxDispclkRoundedDownToDFSGranularity)
		RequiredDispclk = MaxDispclkRoundedDownToDFSGranularity;
	else
		RequiredDispclk = DISPCLKWithRampingRoundedToDFSGranularity;

	return RequiredDispclk;
}

double dml32_RoundToDFSGranularity(double Clock, bool round_up, double VCOSpeed)
{
	if (Clock <= 0.0)
		return 0.0;

	if (round_up)
		return VCOSpeed * 4.0 / dml_floor(VCOSpeed * 4.0 / Clock, 1.0);
	else
		return VCOSpeed * 4.0 / dml_ceil(VCOSpeed * 4.0 / Clock, 1.0);
}

void dml32_CalculateOutputLink(
		double PHYCLKPerState,
		double PHYCLKD18PerState,
		double PHYCLKD32PerState,
		double Downspreading,
		bool IsMainSurfaceUsingTheIndicatedTiming,
		enum output_encoder_class Output,
		enum output_format_class OutputFormat,
		unsigned int HTotal,
		unsigned int HActive,
		double PixelClockBackEnd,
		double ForcedOutputLinkBPP,
		unsigned int DSCInputBitPerComponent,
		unsigned int NumberOfDSCSlices,
		double AudioSampleRate,
		unsigned int AudioSampleLayout,
		enum odm_combine_mode ODMModeNoDSC,
		enum odm_combine_mode ODMModeDSC,
		bool DSCEnable,
		unsigned int OutputLinkDPLanes,
		enum dm_output_link_dp_rate OutputLinkDPRate,

		/* Output */
		bool *RequiresDSC,
		double *RequiresFEC,
		double  *OutBpp,
		enum dm_output_type *OutputType,
		enum dm_output_rate *OutputRate,
		unsigned int *RequiredSlots)
{
	bool LinkDSCEnable;
	unsigned int dummy;
	*RequiresDSC = false;
	*RequiresFEC = false;
	*OutBpp = 0;
	*OutputType = dm_output_type_unknown;
	*OutputRate = dm_output_rate_unknown;

	if (IsMainSurfaceUsingTheIndicatedTiming) {
		if (Output == dm_hdmi) {
			*RequiresDSC = false;
			*RequiresFEC = false;
			*OutBpp = dml32_TruncToValidBPP(dml_min(600, PHYCLKPerState) * 10, 3, HTotal, HActive,
					PixelClockBackEnd, ForcedOutputLinkBPP, false, Output, OutputFormat,
					DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
					ODMModeNoDSC, ODMModeDSC, &dummy);
			//OutputTypeAndRate = "HDMI";
			*OutputType = dm_output_type_hdmi;

		} else if (Output == dm_dp || Output == dm_dp2p0 || Output == dm_edp) {
			if (DSCEnable == true) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				if (Output == dm_dp || Output == dm_dp2p0)
					*RequiresFEC = true;
				else
					*RequiresFEC = false;
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				if (Output == dm_dp2p0)
					*RequiresFEC = true;
				else
					*RequiresFEC = false;
			}
			if (Output == dm_dp2p0) {
				*OutBpp = 0;
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_uhbr10) &&
						PHYCLKD32PerState >= 10000.0 / 32) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 10000,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat,
							DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate,
							AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKD32PerState < 13500.0 / 32 && DSCEnable == true &&
							ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 10000,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR10";
					*OutputType = dm_output_type_dp2p0;
					*OutputRate = dm_output_rate_dp_rate_uhbr10;
				}
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_uhbr13p5) &&
						*OutBpp == 0 && PHYCLKD32PerState >= 13500.0 / 32) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 13500,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat,
							DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate,
							AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKD32PerState < 20000 / 32 && DSCEnable == true &&
							ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 13500,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR13p5";
					*OutputType = dm_output_type_dp2p0;
					*OutputRate = dm_output_rate_dp_rate_uhbr13p5;
				}
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_uhbr20) &&
						*OutBpp == 0 && PHYCLKD32PerState >= 20000 / 32) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 20000,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat,
							DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate,
							AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && DSCEnable == true && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 20000,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR20";
					*OutputType = dm_output_type_dp2p0;
					*OutputRate = dm_output_rate_dp_rate_uhbr20;
				}
			} else {
				*OutBpp = 0;
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_hbr) &&
						PHYCLKPerState >= 270) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 2700,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat,
							DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate,
							AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKPerState < 540 && DSCEnable == true &&
							ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dm_dp)
							*RequiresFEC = true;
						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 2700,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR";
					*OutputType = (Output == dm_dp) ? dm_output_type_dp : dm_output_type_edp;
					*OutputRate = dm_output_rate_dp_rate_hbr;
				}
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_hbr2) &&
						*OutBpp == 0 && PHYCLKPerState >= 540) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 5400,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat,
							DSCInputBitPerComponent, NumberOfDSCSlices, AudioSampleRate,
							AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKPerState < 810 && DSCEnable == true &&
							ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dm_dp)
							*RequiresFEC = true;

						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 5400,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR2";
					*OutputType = (Output == dm_dp) ? dm_output_type_dp : dm_output_type_edp;
					*OutputRate = dm_output_rate_dp_rate_hbr2;
				}
				if ((OutputLinkDPRate == dm_dp_rate_na || OutputLinkDPRate == dm_dp_rate_hbr3) && *OutBpp == 0 && PHYCLKPerState >= 810) {
					*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 8100,
							OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
							ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices,
							AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC,
							RequiredSlots);

					if (*OutBpp == 0 && DSCEnable == true && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dm_dp)
							*RequiresFEC = true;

						*OutBpp = dml32_TruncToValidBPP((1 - Downspreading / 100) * 8100,
								OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd,
								ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent,
								NumberOfDSCSlices, AudioSampleRate, AudioSampleLayout,
								ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR3";
					*OutputType = (Output == dm_dp) ? dm_output_type_dp : dm_output_type_edp;
					*OutputRate = dm_output_rate_dp_rate_hbr3;
				}
			}
		}
	}
}

void dml32_CalculateDPPCLK(
		unsigned int NumberOfActiveSurfaces,
		double DISPCLKDPPCLKDSCCLKDownSpreading,
		double DISPCLKDPPCLKVCOSpeed,
		double DPPCLKUsingSingleDPP[],
		unsigned int DPPPerSurface[],

		/* output */
		double *GlobalDPPCLK,
		double Dppclk[])
{
	unsigned int k;
	*GlobalDPPCLK = 0;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		Dppclk[k] = DPPCLKUsingSingleDPP[k] / DPPPerSurface[k] * (1 + DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		*GlobalDPPCLK = dml_max(*GlobalDPPCLK, Dppclk[k]);
	}
	*GlobalDPPCLK = dml32_RoundToDFSGranularity(*GlobalDPPCLK, 1, DISPCLKDPPCLKVCOSpeed);
	for (k = 0; k < NumberOfActiveSurfaces; ++k)
		Dppclk[k] = *GlobalDPPCLK / 255 * dml_ceil(Dppclk[k] * 255.0 / *GlobalDPPCLK, 1.0);
}

double dml32_TruncToValidBPP(
		double LinkBitRate,
		unsigned int Lanes,
		unsigned int HTotal,
		unsigned int HActive,
		double PixelClock,
		double DesiredBPP,
		bool DSCEnable,
		enum output_encoder_class Output,
		enum output_format_class Format,
		unsigned int DSCInputBitPerComponent,
		unsigned int DSCSlices,
		unsigned int AudioRate,
		unsigned int AudioLayout,
		enum odm_combine_mode ODMModeNoDSC,
		enum odm_combine_mode ODMModeDSC,
		/* Output */
		unsigned int *RequiredSlots)
{
	double    MaxLinkBPP;
	unsigned int   MinDSCBPP;
	double    MaxDSCBPP;
	unsigned int   NonDSCBPP0;
	unsigned int   NonDSCBPP1;
	unsigned int   NonDSCBPP2;
	unsigned int   NonDSCBPP3 = BPP_INVALID;

	if (Format == dm_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 1.5 * DSCInputBitPerComponent - 1.0 / 16;
	} else if (Format == dm_444) {
		NonDSCBPP3 = 18;
		NonDSCBPP0 = 24;
		NonDSCBPP1 = 30;
		NonDSCBPP2 = 36;
		MinDSCBPP = 8;
		MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16;
	} else {
		if (Output == dm_hdmi) {
			NonDSCBPP0 = 24;
			NonDSCBPP1 = 24;
			NonDSCBPP2 = 24;
		} else {
			NonDSCBPP0 = 16;
			NonDSCBPP1 = 20;
			NonDSCBPP2 = 24;
		}
		if (Format == dm_n422) {
			MinDSCBPP = 7;
			MaxDSCBPP = 2 * DSCInputBitPerComponent - 1.0 / 16.0;
		} else {
			MinDSCBPP = 8;
			MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16.0;
		}
	}
	if (Output == dm_dp2p0) {
		MaxLinkBPP = LinkBitRate * Lanes / PixelClock * 128 / 132 * 383 / 384 * 65536 / 65540;
	} else if (DSCEnable && Output == dm_dp) {
		MaxLinkBPP = LinkBitRate / 10 * 8 * Lanes / PixelClock * (1 - 2.4 / 100);
	} else {
		MaxLinkBPP = LinkBitRate / 10 * 8 * Lanes / PixelClock;
	}

	if (DSCEnable) {
		if (ODMModeDSC == dm_odm_combine_mode_4to1)
			MaxLinkBPP = dml_min(MaxLinkBPP, 16);
		else if (ODMModeDSC == dm_odm_combine_mode_2to1)
			MaxLinkBPP = dml_min(MaxLinkBPP, 32);
		else if (ODMModeDSC == dm_odm_split_mode_1to2)
			MaxLinkBPP = 2 * MaxLinkBPP;
	} else {
		if (ODMModeNoDSC == dm_odm_combine_mode_4to1)
			MaxLinkBPP = dml_min(MaxLinkBPP, 16);
		else if (ODMModeNoDSC == dm_odm_combine_mode_2to1)
			MaxLinkBPP = dml_min(MaxLinkBPP, 32);
		else if (ODMModeNoDSC == dm_odm_split_mode_1to2)
			MaxLinkBPP = 2 * MaxLinkBPP;
	}

	*RequiredSlots = dml_ceil(DesiredBPP / MaxLinkBPP * 64, 1);

	if (DesiredBPP == 0) {
		if (DSCEnable) {
			if (MaxLinkBPP < MinDSCBPP)
				return BPP_INVALID;
			else if (MaxLinkBPP >= MaxDSCBPP)
				return MaxDSCBPP;
			else
				return dml_floor(16.0 * MaxLinkBPP, 1.0) / 16.0;
		} else {
			if (MaxLinkBPP >= NonDSCBPP2)
				return NonDSCBPP2;
			else if (MaxLinkBPP >= NonDSCBPP1)
				return NonDSCBPP1;
			else if (MaxLinkBPP >= NonDSCBPP0)
				return 16.0;
			else if ((Output == dm_dp2p0 || Output == dm_dp) && NonDSCBPP3 != BPP_INVALID &&  MaxLinkBPP >= NonDSCBPP3)
				return NonDSCBPP3; // Special case to allow 6bpc RGB for DP connections.
			else
				return BPP_INVALID;
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 ||
				DesiredBPP <= NonDSCBPP0)) ||
				(DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP)))
			return BPP_INVALID;
		else
			return DesiredBPP;
	}
} // TruncToValidBPP

double dml32_RequiredDTBCLK(
		bool              DSCEnable,
		double               PixelClock,
		enum output_format_class  OutputFormat,
		double               OutputBpp,
		unsigned int              DSCSlices,
		unsigned int                 HTotal,
		unsigned int                 HActive,
		unsigned int              AudioRate,
		unsigned int              AudioLayout)
{
	double PixelWordRate;
	double HCActive;
	double HCBlank;
	double AverageTribyteRate;
	double HActiveTribyteRate;

	if (DSCEnable != true)
		return dml_max(PixelClock / 4.0 * OutputBpp / 24.0, 25.0);

	PixelWordRate = PixelClock /  (OutputFormat == dm_444 ? 1 : 2);
	HCActive = dml_ceil(DSCSlices * dml_ceil(OutputBpp *
			dml_ceil(HActive / DSCSlices, 1) / 8.0, 1) / 3.0, 1);
	HCBlank = 64 + 32 *
			dml_ceil(AudioRate *  (AudioLayout == 1 ? 1 : 0.25) * HTotal / (PixelClock * 1000), 1);
	AverageTribyteRate = PixelWordRate * (HCActive + HCBlank) / HTotal;
	HActiveTribyteRate = PixelWordRate * HCActive / HActive;
	return dml_max4(PixelWordRate / 4.0, AverageTribyteRate / 4.0, HActiveTribyteRate / 4.0, 25.0) * 1.002;
}

unsigned int dml32_DSCDelayRequirement(bool DSCEnabled,
		enum odm_combine_mode ODMMode,
		unsigned int DSCInputBitPerComponent,
		double OutputBpp,
		unsigned int HActive,
		unsigned int HTotal,
		unsigned int NumberOfDSCSlices,
		enum output_format_class  OutputFormat,
		enum output_encoder_class Output,
		double PixelClock,
		double PixelClockBackEnd,
		double dsc_delay_factor_wa)
{
	unsigned int DSCDelayRequirement_val;

	if (DSCEnabled == true && OutputBpp != 0) {
		if (ODMMode == dm_odm_combine_mode_4to1) {
			DSCDelayRequirement_val = 4 * (dml32_dscceComputeDelay(DSCInputBitPerComponent, OutputBpp,
					dml_ceil(HActive / NumberOfDSCSlices, 1), NumberOfDSCSlices / 4,
					OutputFormat, Output) + dml32_dscComputeDelay(OutputFormat, Output));
		} else if (ODMMode == dm_odm_combine_mode_2to1) {
			DSCDelayRequirement_val = 2 * (dml32_dscceComputeDelay(DSCInputBitPerComponent, OutputBpp,
					dml_ceil(HActive / NumberOfDSCSlices, 1), NumberOfDSCSlices / 2,
					OutputFormat, Output) + dml32_dscComputeDelay(OutputFormat, Output));
		} else {
			DSCDelayRequirement_val = dml32_dscceComputeDelay(DSCInputBitPerComponent, OutputBpp,
					dml_ceil(HActive / NumberOfDSCSlices, 1), NumberOfDSCSlices,
					OutputFormat, Output) + dml32_dscComputeDelay(OutputFormat, Output);
		}

		DSCDelayRequirement_val = DSCDelayRequirement_val + (HTotal - HActive) *
				dml_ceil((double)DSCDelayRequirement_val / HActive, 1);

		DSCDelayRequirement_val = DSCDelayRequirement_val * PixelClock / PixelClockBackEnd;

	} else {
		DSCDelayRequirement_val = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSCEnabled              = %d\n", __func__, DSCEnabled);
	dml_print("DML::%s: OutputBpp               = %f\n", __func__, OutputBpp);
	dml_print("DML::%s: HActive                 = %d\n", __func__, HActive);
	dml_print("DML::%s: OutputFormat            = %d\n", __func__, OutputFormat);
	dml_print("DML::%s: DSCInputBitPerComponent = %d\n", __func__, DSCInputBitPerComponent);
	dml_print("DML::%s: NumberOfDSCSlices       = %d\n", __func__, NumberOfDSCSlices);
	dml_print("DML::%s: DSCDelayRequirement_val = %d\n", __func__, DSCDelayRequirement_val);
#endif

	return dml_ceil(DSCDelayRequirement_val * dsc_delay_factor_wa, 1);
}

void dml32_CalculateSurfaceSizeInMall(
		unsigned int NumberOfActiveSurfaces,
		unsigned int MALLAllocatedForDCN,
		enum dm_use_mall_for_static_screen_mode UseMALLForStaticScreen[],
		enum dm_use_mall_for_pstate_change_mode UsesMALLForPStateChange[],
		bool DCCEnable[],
		bool ViewportStationary[],
		unsigned int ViewportXStartY[],
		unsigned int ViewportYStartY[],
		unsigned int ViewportXStartC[],
		unsigned int ViewportYStartC[],
		unsigned int ViewportWidthY[],
		unsigned int ViewportHeightY[],
		unsigned int BytesPerPixelY[],
		unsigned int ViewportWidthC[],
		unsigned int ViewportHeightC[],
		unsigned int BytesPerPixelC[],
		unsigned int SurfaceWidthY[],
		unsigned int SurfaceWidthC[],
		unsigned int SurfaceHeightY[],
		unsigned int SurfaceHeightC[],
		unsigned int Read256BytesBlockWidthY[],
		unsigned int Read256BytesBlockWidthC[],
		unsigned int Read256BytesBlockHeightY[],
		unsigned int Read256BytesBlockHeightC[],
		unsigned int ReadBlockWidthY[],
		unsigned int ReadBlockWidthC[],
		unsigned int ReadBlockHeightY[],
		unsigned int ReadBlockHeightC[],
		unsigned int DCCMetaPitchY[],
		unsigned int DCCMetaPitchC[],

		/* Output */
		unsigned int    SurfaceSizeInMALL[],
		bool *ExceededMALLSize)
{
	unsigned int k;
	unsigned int TotalSurfaceSizeInMALLForSS = 0;
	unsigned int TotalSurfaceSizeInMALLForSubVP = 0;
	unsigned int MALLAllocatedForDCNInBytes = MALLAllocatedForDCN * 1024 * 1024;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (ViewportStationary[k]) {
			SurfaceSizeInMALL[k] = dml_min(dml_ceil(SurfaceWidthY[k], ReadBlockWidthY[k]),
					dml_floor(ViewportXStartY[k] + ViewportWidthY[k] + ReadBlockWidthY[k] - 1,
						ReadBlockWidthY[k]) - dml_floor(ViewportXStartY[k],
						ReadBlockWidthY[k])) * dml_min(dml_ceil(SurfaceHeightY[k],
						ReadBlockHeightY[k]), dml_floor(ViewportYStartY[k] +
						ViewportHeightY[k] + ReadBlockHeightY[k] - 1, ReadBlockHeightY[k]) -
						dml_floor(ViewportYStartY[k], ReadBlockHeightY[k])) * BytesPerPixelY[k];

			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
						dml_min(dml_ceil(SurfaceWidthC[k], ReadBlockWidthC[k]),
							dml_floor(ViewportXStartC[k] + ViewportWidthC[k] +
							ReadBlockWidthC[k] - 1, ReadBlockWidthC[k]) -
							dml_floor(ViewportXStartC[k], ReadBlockWidthC[k])) *
							dml_min(dml_ceil(SurfaceHeightC[k], ReadBlockHeightC[k]),
							dml_floor(ViewportYStartC[k] + ViewportHeightC[k] +
							ReadBlockHeightC[k] - 1, ReadBlockHeightC[k]) -
							dml_floor(ViewportYStartC[k], ReadBlockHeightC[k])) *
							BytesPerPixelC[k];
			}
			if (DCCEnable[k] == true) {
				SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
						(dml_min(dml_ceil(DCCMetaPitchY[k], 8 * Read256BytesBlockWidthY[k]),
							dml_floor(ViewportXStartY[k] + ViewportWidthY[k] + 8 *
							Read256BytesBlockWidthY[k] - 1, 8 * Read256BytesBlockWidthY[k])
							- dml_floor(ViewportXStartY[k], 8 * Read256BytesBlockWidthY[k]))
							* dml_min(dml_ceil(SurfaceHeightY[k], 8 *
							Read256BytesBlockHeightY[k]), dml_floor(ViewportYStartY[k] +
							ViewportHeightY[k] + 8 * Read256BytesBlockHeightY[k] - 1, 8 *
							Read256BytesBlockHeightY[k]) - dml_floor(ViewportYStartY[k], 8 *
							Read256BytesBlockHeightY[k])) * BytesPerPixelY[k] / 256) + (64 * 1024);
				if (Read256BytesBlockWidthC[k] > 0) {
					SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
							dml_min(dml_ceil(DCCMetaPitchC[k], 8 *
								Read256BytesBlockWidthC[k]),
								dml_floor(ViewportXStartC[k] + ViewportWidthC[k] + 8
								* Read256BytesBlockWidthC[k] - 1, 8 *
								Read256BytesBlockWidthC[k]) -
								dml_floor(ViewportXStartC[k], 8 *
								Read256BytesBlockWidthC[k])) *
								dml_min(dml_ceil(SurfaceHeightC[k], 8 *
								Read256BytesBlockHeightC[k]),
								dml_floor(ViewportYStartC[k] + ViewportHeightC[k] +
								8 * Read256BytesBlockHeightC[k] - 1, 8 *
								Read256BytesBlockHeightC[k]) -
								dml_floor(ViewportYStartC[k], 8 *
								Read256BytesBlockHeightC[k])) *
								BytesPerPixelC[k] / 256;
				}
			}
		} else {
			SurfaceSizeInMALL[k] = dml_ceil(dml_min(SurfaceWidthY[k], ViewportWidthY[k] +
					ReadBlockWidthY[k] - 1), ReadBlockWidthY[k]) *
					dml_ceil(dml_min(SurfaceHeightY[k], ViewportHeightY[k] +
							ReadBlockHeightY[k] - 1), ReadBlockHeightY[k]) *
							BytesPerPixelY[k];
			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
						dml_ceil(dml_min(SurfaceWidthC[k], ViewportWidthC[k] +
								ReadBlockWidthC[k] - 1), ReadBlockWidthC[k]) *
						dml_ceil(dml_min(SurfaceHeightC[k], ViewportHeightC[k] +
								ReadBlockHeightC[k] - 1), ReadBlockHeightC[k]) *
								BytesPerPixelC[k];
			}
			if (DCCEnable[k] == true) {
				SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
						(dml_ceil(dml_min(DCCMetaPitchY[k], ViewportWidthY[k] + 8 *
								Read256BytesBlockWidthY[k] - 1), 8 *
								Read256BytesBlockWidthY[k]) *
						dml_ceil(dml_min(SurfaceHeightY[k], ViewportHeightY[k] + 8 *
								Read256BytesBlockHeightY[k] - 1), 8 *
								Read256BytesBlockHeightY[k]) * BytesPerPixelY[k] / 256) + (64 * 1024);

				if (Read256BytesBlockWidthC[k] > 0) {
					SurfaceSizeInMALL[k] = SurfaceSizeInMALL[k] +
							dml_ceil(dml_min(DCCMetaPitchC[k], ViewportWidthC[k] + 8 *
									Read256BytesBlockWidthC[k] - 1), 8 *
									Read256BytesBlockWidthC[k]) *
							dml_ceil(dml_min(SurfaceHeightC[k], ViewportHeightC[k] + 8 *
									Read256BytesBlockHeightC[k] - 1), 8 *
									Read256BytesBlockHeightC[k]) *
									BytesPerPixelC[k] / 256;
				}
			}
		}
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		/* SS and Subvp counted separate as they are never used at the same time */
		if (UsesMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe)
			TotalSurfaceSizeInMALLForSubVP = TotalSurfaceSizeInMALLForSubVP + SurfaceSizeInMALL[k];
		else if (UseMALLForStaticScreen[k] == dm_use_mall_static_screen_enable)
			TotalSurfaceSizeInMALLForSS = TotalSurfaceSizeInMALLForSS + SurfaceSizeInMALL[k];
	}
	*ExceededMALLSize =  (TotalSurfaceSizeInMALLForSS > MALLAllocatedForDCNInBytes) ||
							(TotalSurfaceSizeInMALLForSubVP > MALLAllocatedForDCNInBytes);
} // CalculateSurfaceSizeInMall

void dml32_CalculateVMRowAndSwath(
		unsigned int NumberOfActiveSurfaces,
		DmlPipe myPipe[],
		unsigned int SurfaceSizeInMALL[],
		unsigned int PTEBufferSizeInRequestsLuma,
		unsigned int PTEBufferSizeInRequestsChroma,
		unsigned int DCCMetaBufferSizeBytes,
		enum dm_use_mall_for_static_screen_mode UseMALLForStaticScreen[],
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
		unsigned int MALLAllocatedForDCN,
		double SwathWidthY[],
		double SwathWidthC[],
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		unsigned int GPUVMMaxPageTableLevels,
		unsigned int GPUVMMinPageSizeKBytes[],
		unsigned int HostVMMinPageSize,

		/* Output */
		bool PTEBufferSizeNotExceeded[],
		bool DCCMetaBufferSizeNotExceeded[],
		unsigned int dpte_row_width_luma_ub[],
		unsigned int dpte_row_width_chroma_ub[],
		unsigned int dpte_row_height_luma[],
		unsigned int dpte_row_height_chroma[],
		unsigned int dpte_row_height_linear_luma[],     // VBA_DELTA
		unsigned int dpte_row_height_linear_chroma[],   // VBA_DELTA
		unsigned int meta_req_width[],
		unsigned int meta_req_width_chroma[],
		unsigned int meta_req_height[],
		unsigned int meta_req_height_chroma[],
		unsigned int meta_row_width[],
		unsigned int meta_row_width_chroma[],
		unsigned int meta_row_height[],
		unsigned int meta_row_height_chroma[],
		unsigned int vm_group_bytes[],
		unsigned int dpte_group_bytes[],
		unsigned int PixelPTEReqWidthY[],
		unsigned int PixelPTEReqHeightY[],
		unsigned int PTERequestSizeY[],
		unsigned int PixelPTEReqWidthC[],
		unsigned int PixelPTEReqHeightC[],
		unsigned int PTERequestSizeC[],
		unsigned int dpde0_bytes_per_frame_ub_l[],
		unsigned int meta_pte_bytes_per_frame_ub_l[],
		unsigned int dpde0_bytes_per_frame_ub_c[],
		unsigned int meta_pte_bytes_per_frame_ub_c[],
		double PrefetchSourceLinesY[],
		double PrefetchSourceLinesC[],
		double VInitPreFillY[],
		double VInitPreFillC[],
		unsigned int MaxNumSwathY[],
		unsigned int MaxNumSwathC[],
		double meta_row_bw[],
		double dpte_row_bw[],
		double PixelPTEBytesPerRow[],
		double PDEAndMetaPTEBytesFrame[],
		double MetaRowByte[],
		bool use_one_row_for_frame[],
		bool use_one_row_for_frame_flip[],
		bool UsesMALLForStaticScreen[],
		bool PTE_BUFFER_MODE[],
		unsigned int BIGK_FRAGMENT_SIZE[])
{
	unsigned int k;
	unsigned int PTEBufferSizeInRequestsForLuma[DC__NUM_DPP__MAX];
	unsigned int PTEBufferSizeInRequestsForChroma[DC__NUM_DPP__MAX];
	unsigned int PDEAndMetaPTEBytesFrameY;
	unsigned int PDEAndMetaPTEBytesFrameC;
	unsigned int MetaRowByteY[DC__NUM_DPP__MAX] = {0};
	unsigned int MetaRowByteC[DC__NUM_DPP__MAX] = {0};
	unsigned int PixelPTEBytesPerRowY[DC__NUM_DPP__MAX];
	unsigned int PixelPTEBytesPerRowC[DC__NUM_DPP__MAX];
	unsigned int PixelPTEBytesPerRowY_one_row_per_frame[DC__NUM_DPP__MAX];
	unsigned int PixelPTEBytesPerRowC_one_row_per_frame[DC__NUM_DPP__MAX];
	unsigned int dpte_row_width_luma_ub_one_row_per_frame[DC__NUM_DPP__MAX];
	unsigned int dpte_row_height_luma_one_row_per_frame[DC__NUM_DPP__MAX];
	unsigned int dpte_row_width_chroma_ub_one_row_per_frame[DC__NUM_DPP__MAX];
	unsigned int dpte_row_height_chroma_one_row_per_frame[DC__NUM_DPP__MAX];
	bool one_row_per_frame_fits_in_buffer[DC__NUM_DPP__MAX];

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (HostVMEnable == true) {
			vm_group_bytes[k] = 512;
			dpte_group_bytes[k] = 512;
		} else if (GPUVMEnable == true) {
			vm_group_bytes[k] = 2048;
			if (GPUVMMinPageSizeKBytes[k] >= 64 && IsVertical(myPipe[k].SourceRotation))
				dpte_group_bytes[k] = 512;
			else
				dpte_group_bytes[k] = 2048;
		} else {
			vm_group_bytes[k] = 0;
			dpte_group_bytes[k] = 0;
		}

		if (myPipe[k].SourcePixelFormat == dm_420_8 || myPipe[k].SourcePixelFormat == dm_420_10 ||
				myPipe[k].SourcePixelFormat == dm_420_12 ||
				myPipe[k].SourcePixelFormat == dm_rgbe_alpha) {
			if ((myPipe[k].SourcePixelFormat == dm_420_10 || myPipe[k].SourcePixelFormat == dm_420_12) &&
					!IsVertical(myPipe[k].SourceRotation)) {
				PTEBufferSizeInRequestsForLuma[k] =
						(PTEBufferSizeInRequestsLuma + PTEBufferSizeInRequestsChroma) / 2;
				PTEBufferSizeInRequestsForChroma[k] = PTEBufferSizeInRequestsForLuma[k];
			} else {
				PTEBufferSizeInRequestsForLuma[k] = PTEBufferSizeInRequestsLuma;
				PTEBufferSizeInRequestsForChroma[k] = PTEBufferSizeInRequestsChroma;
			}

			PDEAndMetaPTEBytesFrameC = dml32_CalculateVMAndRowBytes(
					myPipe[k].ViewportStationary,
					myPipe[k].DCCEnable,
					myPipe[k].DPPPerSurface,
					myPipe[k].BlockHeight256BytesC,
					myPipe[k].BlockWidth256BytesC,
					myPipe[k].SourcePixelFormat,
					myPipe[k].SurfaceTiling,
					myPipe[k].BytePerPixelC,
					myPipe[k].SourceRotation,
					SwathWidthC[k],
					myPipe[k].ViewportHeightChroma,
					myPipe[k].ViewportXStartC,
					myPipe[k].ViewportYStartC,
					GPUVMEnable,
					HostVMEnable,
					HostVMMaxNonCachedPageTableLevels,
					GPUVMMaxPageTableLevels,
					GPUVMMinPageSizeKBytes[k],
					HostVMMinPageSize,
					PTEBufferSizeInRequestsForChroma[k],
					myPipe[k].PitchC,
					myPipe[k].DCCMetaPitchC,
					myPipe[k].BlockWidthC,
					myPipe[k].BlockHeightC,

					/* Output */
					&MetaRowByteC[k],
					&PixelPTEBytesPerRowC[k],
					&dpte_row_width_chroma_ub[k],
					&dpte_row_height_chroma[k],
					&dpte_row_height_linear_chroma[k],
					&PixelPTEBytesPerRowC_one_row_per_frame[k],
					&dpte_row_width_chroma_ub_one_row_per_frame[k],
					&dpte_row_height_chroma_one_row_per_frame[k],
					&meta_req_width_chroma[k],
					&meta_req_height_chroma[k],
					&meta_row_width_chroma[k],
					&meta_row_height_chroma[k],
					&PixelPTEReqWidthC[k],
					&PixelPTEReqHeightC[k],
					&PTERequestSizeC[k],
					&dpde0_bytes_per_frame_ub_c[k],
					&meta_pte_bytes_per_frame_ub_c[k]);

			PrefetchSourceLinesC[k] = dml32_CalculatePrefetchSourceLines(
					myPipe[k].VRatioChroma,
					myPipe[k].VTapsChroma,
					myPipe[k].InterlaceEnable,
					myPipe[k].ProgressiveToInterlaceUnitInOPP,
					myPipe[k].SwathHeightC,
					myPipe[k].SourceRotation,
					myPipe[k].ViewportStationary,
					SwathWidthC[k],
					myPipe[k].ViewportHeightChroma,
					myPipe[k].ViewportXStartC,
					myPipe[k].ViewportYStartC,

					/* Output */
					&VInitPreFillC[k],
					&MaxNumSwathC[k]);
		} else {
			PTEBufferSizeInRequestsForLuma[k] = PTEBufferSizeInRequestsLuma + PTEBufferSizeInRequestsChroma;
			PTEBufferSizeInRequestsForChroma[k] = 0;
			PixelPTEBytesPerRowC[k] = 0;
			PDEAndMetaPTEBytesFrameC = 0;
			MetaRowByteC[k] = 0;
			MaxNumSwathC[k] = 0;
			PrefetchSourceLinesC[k] = 0;
			dpte_row_height_chroma_one_row_per_frame[k] = 0;
			dpte_row_width_chroma_ub_one_row_per_frame[k] = 0;
			PixelPTEBytesPerRowC_one_row_per_frame[k] = 0;
		}

		PDEAndMetaPTEBytesFrameY = dml32_CalculateVMAndRowBytes(
				myPipe[k].ViewportStationary,
				myPipe[k].DCCEnable,
				myPipe[k].DPPPerSurface,
				myPipe[k].BlockHeight256BytesY,
				myPipe[k].BlockWidth256BytesY,
				myPipe[k].SourcePixelFormat,
				myPipe[k].SurfaceTiling,
				myPipe[k].BytePerPixelY,
				myPipe[k].SourceRotation,
				SwathWidthY[k],
				myPipe[k].ViewportHeight,
				myPipe[k].ViewportXStart,
				myPipe[k].ViewportYStart,
				GPUVMEnable,
				HostVMEnable,
				HostVMMaxNonCachedPageTableLevels,
				GPUVMMaxPageTableLevels,
				GPUVMMinPageSizeKBytes[k],
				HostVMMinPageSize,
				PTEBufferSizeInRequestsForLuma[k],
				myPipe[k].PitchY,
				myPipe[k].DCCMetaPitchY,
				myPipe[k].BlockWidthY,
				myPipe[k].BlockHeightY,

				/* Output */
				&MetaRowByteY[k],
				&PixelPTEBytesPerRowY[k],
				&dpte_row_width_luma_ub[k],
				&dpte_row_height_luma[k],
				&dpte_row_height_linear_luma[k],
				&PixelPTEBytesPerRowY_one_row_per_frame[k],
				&dpte_row_width_luma_ub_one_row_per_frame[k],
				&dpte_row_height_luma_one_row_per_frame[k],
				&meta_req_width[k],
				&meta_req_height[k],
				&meta_row_width[k],
				&meta_row_height[k],
				&PixelPTEReqWidthY[k],
				&PixelPTEReqHeightY[k],
				&PTERequestSizeY[k],
				&dpde0_bytes_per_frame_ub_l[k],
				&meta_pte_bytes_per_frame_ub_l[k]);

		PrefetchSourceLinesY[k] = dml32_CalculatePrefetchSourceLines(
				myPipe[k].VRatio,
				myPipe[k].VTaps,
				myPipe[k].InterlaceEnable,
				myPipe[k].ProgressiveToInterlaceUnitInOPP,
				myPipe[k].SwathHeightY,
				myPipe[k].SourceRotation,
				myPipe[k].ViewportStationary,
				SwathWidthY[k],
				myPipe[k].ViewportHeight,
				myPipe[k].ViewportXStart,
				myPipe[k].ViewportYStart,

				/* Output */
				&VInitPreFillY[k],
				&MaxNumSwathY[k]);

		PDEAndMetaPTEBytesFrame[k] = PDEAndMetaPTEBytesFrameY + PDEAndMetaPTEBytesFrameC;
		MetaRowByte[k] = MetaRowByteY[k] + MetaRowByteC[k];

		if (PixelPTEBytesPerRowY[k] <= 64 * PTEBufferSizeInRequestsForLuma[k] &&
				PixelPTEBytesPerRowC[k] <= 64 * PTEBufferSizeInRequestsForChroma[k]) {
			PTEBufferSizeNotExceeded[k] = true;
		} else {
			PTEBufferSizeNotExceeded[k] = false;
		}

		one_row_per_frame_fits_in_buffer[k] = (PixelPTEBytesPerRowY_one_row_per_frame[k] <= 64 * 2 *
			PTEBufferSizeInRequestsForLuma[k] &&
			PixelPTEBytesPerRowC_one_row_per_frame[k] <= 64 * 2 * PTEBufferSizeInRequestsForChroma[k]);
	}

	dml32_CalculateMALLUseForStaticScreen(
			NumberOfActiveSurfaces,
			MALLAllocatedForDCN,
			UseMALLForStaticScreen,   // mode
			SurfaceSizeInMALL,
			one_row_per_frame_fits_in_buffer,
			/* Output */
			UsesMALLForStaticScreen); // boolen

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		PTE_BUFFER_MODE[k] = myPipe[k].FORCE_ONE_ROW_FOR_FRAME || UsesMALLForStaticScreen[k] ||
				(UseMALLForPStateChange[k] == dm_use_mall_pstate_change_sub_viewport) ||
				(UseMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe) ||
				(GPUVMMinPageSizeKBytes[k] > 64);
		BIGK_FRAGMENT_SIZE[k] = dml_log2(GPUVMMinPageSizeKBytes[k] * 1024) - 12;
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, SurfaceSizeInMALL = %d\n",  __func__, k, SurfaceSizeInMALL[k]);
		dml_print("DML::%s: k=%d, UsesMALLForStaticScreen = %d\n",  __func__, k, UsesMALLForStaticScreen[k]);
#endif
		use_one_row_for_frame[k] = myPipe[k].FORCE_ONE_ROW_FOR_FRAME || UsesMALLForStaticScreen[k] ||
				(UseMALLForPStateChange[k] == dm_use_mall_pstate_change_sub_viewport) ||
				(UseMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe) ||
				(GPUVMMinPageSizeKBytes[k] > 64 && IsVertical(myPipe[k].SourceRotation));

		use_one_row_for_frame_flip[k] = use_one_row_for_frame[k] &&
				!(UseMALLForPStateChange[k] == dm_use_mall_pstate_change_full_frame);

		if (use_one_row_for_frame[k]) {
			dpte_row_height_luma[k] = dpte_row_height_luma_one_row_per_frame[k];
			dpte_row_width_luma_ub[k] = dpte_row_width_luma_ub_one_row_per_frame[k];
			PixelPTEBytesPerRowY[k] = PixelPTEBytesPerRowY_one_row_per_frame[k];
			dpte_row_height_chroma[k] = dpte_row_height_chroma_one_row_per_frame[k];
			dpte_row_width_chroma_ub[k] = dpte_row_width_chroma_ub_one_row_per_frame[k];
			PixelPTEBytesPerRowC[k] = PixelPTEBytesPerRowC_one_row_per_frame[k];
			PTEBufferSizeNotExceeded[k] = one_row_per_frame_fits_in_buffer[k];
		}

		if (MetaRowByte[k] <= DCCMetaBufferSizeBytes)
			DCCMetaBufferSizeNotExceeded[k] = true;
		else
			DCCMetaBufferSizeNotExceeded[k] = false;

		PixelPTEBytesPerRow[k] = PixelPTEBytesPerRowY[k] + PixelPTEBytesPerRowC[k];
		if (use_one_row_for_frame[k])
			PixelPTEBytesPerRow[k] = PixelPTEBytesPerRow[k] / 2;

		dml32_CalculateRowBandwidth(
				GPUVMEnable,
				myPipe[k].SourcePixelFormat,
				myPipe[k].VRatio,
				myPipe[k].VRatioChroma,
				myPipe[k].DCCEnable,
				myPipe[k].HTotal / myPipe[k].PixelClock,
				MetaRowByteY[k], MetaRowByteC[k],
				meta_row_height[k],
				meta_row_height_chroma[k],
				PixelPTEBytesPerRowY[k],
				PixelPTEBytesPerRowC[k],
				dpte_row_height_luma[k],
				dpte_row_height_chroma[k],

				/* Output */
				&meta_row_bw[k],
				&dpte_row_bw[k]);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, use_one_row_for_frame        = %d\n",  __func__, k, use_one_row_for_frame[k]);
		dml_print("DML::%s: k=%d, use_one_row_for_frame_flip   = %d\n",
				__func__, k, use_one_row_for_frame_flip[k]);
		dml_print("DML::%s: k=%d, UseMALLForPStateChange       = %d\n",
				__func__, k, UseMALLForPStateChange[k]);
		dml_print("DML::%s: k=%d, dpte_row_height_luma         = %d\n",  __func__, k, dpte_row_height_luma[k]);
		dml_print("DML::%s: k=%d, dpte_row_width_luma_ub       = %d\n",
				__func__, k, dpte_row_width_luma_ub[k]);
		dml_print("DML::%s: k=%d, PixelPTEBytesPerRowY         = %d\n",  __func__, k, PixelPTEBytesPerRowY[k]);
		dml_print("DML::%s: k=%d, dpte_row_height_chroma       = %d\n",
				__func__, k, dpte_row_height_chroma[k]);
		dml_print("DML::%s: k=%d, dpte_row_width_chroma_ub     = %d\n",
				__func__, k, dpte_row_width_chroma_ub[k]);
		dml_print("DML::%s: k=%d, PixelPTEBytesPerRowC         = %d\n",  __func__, k, PixelPTEBytesPerRowC[k]);
		dml_print("DML::%s: k=%d, PixelPTEBytesPerRow          = %d\n",  __func__, k, PixelPTEBytesPerRow[k]);
		dml_print("DML::%s: k=%d, PTEBufferSizeNotExceeded     = %d\n",
				__func__, k, PTEBufferSizeNotExceeded[k]);
		dml_print("DML::%s: k=%d, PTE_BUFFER_MODE              = %d\n", __func__, k, PTE_BUFFER_MODE[k]);
		dml_print("DML::%s: k=%d, BIGK_FRAGMENT_SIZE           = %d\n", __func__, k, BIGK_FRAGMENT_SIZE[k]);
#endif
	}
} // CalculateVMRowAndSwath

unsigned int dml32_CalculateVMAndRowBytes(
		bool ViewportStationary,
		bool DCCEnable,
		unsigned int NumberOfDPPs,
		unsigned int BlockHeight256Bytes,
		unsigned int BlockWidth256Bytes,
		enum source_format_class SourcePixelFormat,
		unsigned int SurfaceTiling,
		unsigned int BytePerPixel,
		enum dm_rotation_angle SourceRotation,
		double SwathWidth,
		unsigned int ViewportHeight,
		unsigned int    ViewportXStart,
		unsigned int    ViewportYStart,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		unsigned int GPUVMMaxPageTableLevels,
		unsigned int GPUVMMinPageSizeKBytes,
		unsigned int HostVMMinPageSize,
		unsigned int PTEBufferSizeInRequests,
		unsigned int Pitch,
		unsigned int DCCMetaPitch,
		unsigned int MacroTileWidth,
		unsigned int MacroTileHeight,

		/* Output */
		unsigned int *MetaRowByte,
		unsigned int *PixelPTEBytesPerRow,
		unsigned int    *dpte_row_width_ub,
		unsigned int *dpte_row_height,
		unsigned int *dpte_row_height_linear,
		unsigned int    *PixelPTEBytesPerRow_one_row_per_frame,
		unsigned int    *dpte_row_width_ub_one_row_per_frame,
		unsigned int    *dpte_row_height_one_row_per_frame,
		unsigned int *MetaRequestWidth,
		unsigned int *MetaRequestHeight,
		unsigned int *meta_row_width,
		unsigned int *meta_row_height,
		unsigned int *PixelPTEReqWidth,
		unsigned int *PixelPTEReqHeight,
		unsigned int *PTERequestSize,
		unsigned int    *DPDE0BytesFrame,
		unsigned int    *MetaPTEBytesFrame)
{
	unsigned int MPDEBytesFrame;
	unsigned int DCCMetaSurfaceBytes;
	unsigned int ExtraDPDEBytesFrame;
	unsigned int PDEAndMetaPTEBytesFrame;
	unsigned int HostVMDynamicLevels = 0;
	unsigned int    MacroTileSizeBytes;
	unsigned int    vp_height_meta_ub;
	unsigned int    vp_height_dpte_ub;
	unsigned int PixelPTEReqWidth_linear = 0; // VBA_DELTA. VBA doesn't calculate this

	if (GPUVMEnable == true && HostVMEnable == true) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 2);
	}

	*MetaRequestHeight = 8 * BlockHeight256Bytes;
	*MetaRequestWidth = 8 * BlockWidth256Bytes;
	if (SurfaceTiling == dm_sw_linear) {
		*meta_row_height = 32;
		*meta_row_width = dml_floor(ViewportXStart + SwathWidth + *MetaRequestWidth - 1, *MetaRequestWidth)
				- dml_floor(ViewportXStart, *MetaRequestWidth);
	} else if (!IsVertical(SourceRotation)) {
		*meta_row_height = *MetaRequestHeight;
		if (ViewportStationary && NumberOfDPPs == 1) {
			*meta_row_width = dml_floor(ViewportXStart + SwathWidth + *MetaRequestWidth - 1,
					*MetaRequestWidth) - dml_floor(ViewportXStart, *MetaRequestWidth);
		} else {
			*meta_row_width = dml_ceil(SwathWidth - 1, *MetaRequestWidth) + *MetaRequestWidth;
		}
		*MetaRowByte = *meta_row_width * *MetaRequestHeight * BytePerPixel / 256.0;
	} else {
		*meta_row_height = *MetaRequestWidth;
		if (ViewportStationary && NumberOfDPPs == 1) {
			*meta_row_width = dml_floor(ViewportYStart + ViewportHeight + *MetaRequestHeight - 1,
					*MetaRequestHeight) - dml_floor(ViewportYStart, *MetaRequestHeight);
		} else {
			*meta_row_width = dml_ceil(SwathWidth - 1, *MetaRequestHeight) + *MetaRequestHeight;
		}
		*MetaRowByte = *meta_row_width * *MetaRequestWidth * BytePerPixel / 256.0;
	}

	if (ViewportStationary && (NumberOfDPPs == 1 || !IsVertical(SourceRotation))) {
		vp_height_meta_ub = dml_floor(ViewportYStart + ViewportHeight + 64 * BlockHeight256Bytes - 1,
				64 * BlockHeight256Bytes) - dml_floor(ViewportYStart, 64 * BlockHeight256Bytes);
	} else if (!IsVertical(SourceRotation)) {
		vp_height_meta_ub = dml_ceil(ViewportHeight - 1, 64 * BlockHeight256Bytes) + 64 * BlockHeight256Bytes;
	} else {
		vp_height_meta_ub = dml_ceil(SwathWidth - 1, 64 * BlockHeight256Bytes) + 64 * BlockHeight256Bytes;
	}

	DCCMetaSurfaceBytes = DCCMetaPitch * vp_height_meta_ub * BytePerPixel / 256.0;

	if (GPUVMEnable == true) {
		*MetaPTEBytesFrame = (dml_ceil((double) (DCCMetaSurfaceBytes - 4.0 * 1024.0) /
				(8 * 4.0 * 1024), 1) + 1) * 64;
		MPDEBytesFrame = 128 * (GPUVMMaxPageTableLevels - 1);
	} else {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
	}

	if (DCCEnable != true) {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
		*MetaRowByte = 0;
	}

	MacroTileSizeBytes = MacroTileWidth * BytePerPixel * MacroTileHeight;

	if (GPUVMEnable == true && GPUVMMaxPageTableLevels > 1) {
		if (ViewportStationary && (NumberOfDPPs == 1 || !IsVertical(SourceRotation))) {
			vp_height_dpte_ub = dml_floor(ViewportYStart + ViewportHeight +
					MacroTileHeight - 1, MacroTileHeight) -
					dml_floor(ViewportYStart, MacroTileHeight);
		} else if (!IsVertical(SourceRotation)) {
			vp_height_dpte_ub = dml_ceil(ViewportHeight - 1, MacroTileHeight) + MacroTileHeight;
		} else {
			vp_height_dpte_ub = dml_ceil(SwathWidth - 1, MacroTileHeight) + MacroTileHeight;
		}
		*DPDE0BytesFrame = 64 * (dml_ceil((Pitch * vp_height_dpte_ub * BytePerPixel - MacroTileSizeBytes) /
				(8 * 2097152), 1) + 1);
		ExtraDPDEBytesFrame = 128 * (GPUVMMaxPageTableLevels - 2);
	} else {
		*DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
		vp_height_dpte_ub = 0;
	}

	PDEAndMetaPTEBytesFrame = *MetaPTEBytesFrame + MPDEBytesFrame + *DPDE0BytesFrame + ExtraDPDEBytesFrame;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DCCEnable = %d\n", __func__, DCCEnable);
	dml_print("DML::%s: GPUVMEnable = %d\n", __func__, GPUVMEnable);
	dml_print("DML::%s: SwModeLinear = %d\n", __func__, SurfaceTiling == dm_sw_linear);
	dml_print("DML::%s: BytePerPixel = %d\n", __func__, BytePerPixel);
	dml_print("DML::%s: GPUVMMaxPageTableLevels = %d\n", __func__, GPUVMMaxPageTableLevels);
	dml_print("DML::%s: BlockHeight256Bytes = %d\n", __func__, BlockHeight256Bytes);
	dml_print("DML::%s: BlockWidth256Bytes = %d\n", __func__, BlockWidth256Bytes);
	dml_print("DML::%s: MacroTileHeight = %d\n", __func__, MacroTileHeight);
	dml_print("DML::%s: MacroTileWidth = %d\n", __func__, MacroTileWidth);
	dml_print("DML::%s: MetaPTEBytesFrame = %d\n", __func__, *MetaPTEBytesFrame);
	dml_print("DML::%s: MPDEBytesFrame = %d\n", __func__, MPDEBytesFrame);
	dml_print("DML::%s: DPDE0BytesFrame = %d\n", __func__, *DPDE0BytesFrame);
	dml_print("DML::%s: ExtraDPDEBytesFrame= %d\n", __func__, ExtraDPDEBytesFrame);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: ViewportHeight = %d\n", __func__, ViewportHeight);
	dml_print("DML::%s: SwathWidth = %d\n", __func__, SwathWidth);
	dml_print("DML::%s: vp_height_dpte_ub = %d\n", __func__, vp_height_dpte_ub);
#endif

	if (HostVMEnable == true)
		PDEAndMetaPTEBytesFrame = PDEAndMetaPTEBytesFrame * (1 + 8 * HostVMDynamicLevels);

	if (SurfaceTiling == dm_sw_linear) {
		*PixelPTEReqHeight = 1;
		*PixelPTEReqWidth = GPUVMMinPageSizeKBytes * 1024 * 8 / BytePerPixel;
		PixelPTEReqWidth_linear = GPUVMMinPageSizeKBytes * 1024 * 8 / BytePerPixel;
		*PTERequestSize = 64;
	} else if (GPUVMMinPageSizeKBytes == 4) {
		*PixelPTEReqHeight = 16 * BlockHeight256Bytes;
		*PixelPTEReqWidth = 16 * BlockWidth256Bytes;
		*PTERequestSize = 128;
	} else {
		*PixelPTEReqHeight = MacroTileHeight;
		*PixelPTEReqWidth = 8 *  1024 * GPUVMMinPageSizeKBytes / (MacroTileHeight * BytePerPixel);
		*PTERequestSize = 64;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMMinPageSizeKBytes = %d\n", __func__, GPUVMMinPageSizeKBytes);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d (after HostVM factor)\n", __func__, PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: PixelPTEReqHeight = %d\n", __func__, *PixelPTEReqHeight);
	dml_print("DML::%s: PixelPTEReqWidth = %d\n", __func__, *PixelPTEReqWidth);
	dml_print("DML::%s: PixelPTEReqWidth_linear = %d\n", __func__, PixelPTEReqWidth_linear);
	dml_print("DML::%s: PTERequestSize = %d\n", __func__, *PTERequestSize);
	dml_print("DML::%s: Pitch = %d\n", __func__, Pitch);
#endif

	*dpte_row_height_one_row_per_frame = vp_height_dpte_ub;
	*dpte_row_width_ub_one_row_per_frame = (dml_ceil(((double)Pitch * (double)*dpte_row_height_one_row_per_frame /
			(double) *PixelPTEReqHeight - 1) / (double) *PixelPTEReqWidth, 1) + 1) *
					(double) *PixelPTEReqWidth;
	*PixelPTEBytesPerRow_one_row_per_frame = *dpte_row_width_ub_one_row_per_frame / *PixelPTEReqWidth *
			*PTERequestSize;

	if (SurfaceTiling == dm_sw_linear) {
		*dpte_row_height = dml_min(128, 1 << (unsigned int) dml_floor(dml_log2(PTEBufferSizeInRequests *
				*PixelPTEReqWidth / Pitch), 1));
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: dpte_row_height = %d (1)\n", __func__,
				PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch);
		dml_print("DML::%s: dpte_row_height = %f (2)\n", __func__,
				dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch));
		dml_print("DML::%s: dpte_row_height = %f (3)\n", __func__,
				dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1));
		dml_print("DML::%s: dpte_row_height = %d (4)\n", __func__,
				1 << (unsigned int) dml_floor(dml_log2(PTEBufferSizeInRequests *
						*PixelPTEReqWidth / Pitch), 1));
		dml_print("DML::%s: dpte_row_height = %d\n", __func__, *dpte_row_height);
#endif
		*dpte_row_width_ub = dml_ceil(((double) Pitch * (double) *dpte_row_height - 1),
				(double) *PixelPTEReqWidth) + *PixelPTEReqWidth;
		*PixelPTEBytesPerRow = *dpte_row_width_ub / (double)*PixelPTEReqWidth * (double)*PTERequestSize;

		// VBA_DELTA, VBA doesn't have programming value for pte row height linear.
		*dpte_row_height_linear = 1 << (unsigned int) dml_floor(dml_log2(PTEBufferSizeInRequests *
				PixelPTEReqWidth_linear / Pitch), 1);
		if (*dpte_row_height_linear > 128)
			*dpte_row_height_linear = 128;

	} else if (!IsVertical(SourceRotation)) {
		*dpte_row_height = *PixelPTEReqHeight;

		if (GPUVMMinPageSizeKBytes > 64) {
			*dpte_row_width_ub = (dml_ceil((Pitch * *dpte_row_height / *PixelPTEReqHeight - 1) /
					*PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth;
		} else if (ViewportStationary && (NumberOfDPPs == 1)) {
			*dpte_row_width_ub = dml_floor(ViewportXStart + SwathWidth +
					*PixelPTEReqWidth - 1, *PixelPTEReqWidth) -
					dml_floor(ViewportXStart, *PixelPTEReqWidth);
		} else {
			*dpte_row_width_ub = (dml_ceil((SwathWidth - 1) / *PixelPTEReqWidth, 1) + 1) *
					*PixelPTEReqWidth;
		}

		*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqWidth * *PTERequestSize;
	} else {
		*dpte_row_height = dml_min(*PixelPTEReqWidth, MacroTileWidth);

		if (ViewportStationary && (NumberOfDPPs == 1)) {
			*dpte_row_width_ub = dml_floor(ViewportYStart + ViewportHeight + *PixelPTEReqHeight - 1,
					*PixelPTEReqHeight) - dml_floor(ViewportYStart, *PixelPTEReqHeight);
		} else {
			*dpte_row_width_ub = (dml_ceil((SwathWidth - 1) / *PixelPTEReqHeight, 1) + 1)
					* *PixelPTEReqHeight;
		}

		*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqHeight * *PTERequestSize;
	}

	if (GPUVMEnable != true)
		*PixelPTEBytesPerRow = 0;
	if (HostVMEnable == true)
		*PixelPTEBytesPerRow = *PixelPTEBytesPerRow * (1 + 8 * HostVMDynamicLevels);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMMinPageSizeKBytes = %d\n", __func__, GPUVMMinPageSizeKBytes);
	dml_print("DML::%s: dpte_row_height = %d\n", __func__, *dpte_row_height);
	dml_print("DML::%s: dpte_row_height_linear = %d\n", __func__, *dpte_row_height_linear);
	dml_print("DML::%s: dpte_row_width_ub = %d\n", __func__, *dpte_row_width_ub);
	dml_print("DML::%s: PixelPTEBytesPerRow = %d\n", __func__, *PixelPTEBytesPerRow);
	dml_print("DML::%s: PTEBufferSizeInRequests = %d\n", __func__, PTEBufferSizeInRequests);
	dml_print("DML::%s: dpte_row_height_one_row_per_frame = %d\n", __func__, *dpte_row_height_one_row_per_frame);
	dml_print("DML::%s: dpte_row_width_ub_one_row_per_frame = %d\n",
			__func__, *dpte_row_width_ub_one_row_per_frame);
	dml_print("DML::%s: PixelPTEBytesPerRow_one_row_per_frame = %d\n",
			__func__, *PixelPTEBytesPerRow_one_row_per_frame);
	dml_print("DML: vm_bytes = meta_pte_bytes_per_frame (per_pipe) = MetaPTEBytesFrame = : %i\n",
			*MetaPTEBytesFrame);
#endif

	return PDEAndMetaPTEBytesFrame;
} // CalculateVMAndRowBytes

double dml32_CalculatePrefetchSourceLines(
		double VRatio,
		unsigned int VTaps,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		unsigned int SwathHeight,
		enum dm_rotation_angle SourceRotation,
		bool ViewportStationary,
		double SwathWidth,
		unsigned int ViewportHeight,
		unsigned int ViewportXStart,
		unsigned int ViewportYStart,

		/* Output */
		double *VInitPreFill,
		unsigned int *MaxNumSwath)
{

	unsigned int vp_start_rot;
	unsigned int sw0_tmp;
	unsigned int MaxPartialSwath;
	double numLines;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VRatio = %f\n", __func__, VRatio);
	dml_print("DML::%s: VTaps = %d\n", __func__, VTaps);
	dml_print("DML::%s: ViewportXStart = %d\n", __func__, ViewportXStart);
	dml_print("DML::%s: ViewportYStart = %d\n", __func__, ViewportYStart);
	dml_print("DML::%s: ViewportStationary = %d\n", __func__, ViewportStationary);
	dml_print("DML::%s: SwathHeight = %d\n", __func__, SwathHeight);
#endif
	if (ProgressiveToInterlaceUnitInOPP)
		*VInitPreFill = dml_floor((VRatio + (double) VTaps + 1) / 2.0, 1);
	else
		*VInitPreFill = dml_floor((VRatio + (double) VTaps + 1 + Interlace * 0.5 * VRatio) / 2.0, 1);

	if (ViewportStationary) {
		if (SourceRotation == dm_rotation_180 || SourceRotation == dm_rotation_180m) {
			vp_start_rot = SwathHeight -
					(((unsigned int) (ViewportYStart + ViewportHeight - 1) % SwathHeight) + 1);
		} else if (SourceRotation == dm_rotation_270 || SourceRotation == dm_rotation_90m) {
			vp_start_rot = ViewportXStart;
		} else if (SourceRotation == dm_rotation_90 || SourceRotation == dm_rotation_270m) {
			vp_start_rot = SwathHeight -
					(((unsigned int)(ViewportYStart + SwathWidth - 1) % SwathHeight) + 1);
		} else {
			vp_start_rot = ViewportYStart;
		}
		sw0_tmp = SwathHeight - (vp_start_rot % SwathHeight);
		if (sw0_tmp < *VInitPreFill)
			*MaxNumSwath = dml_ceil((*VInitPreFill - sw0_tmp) / SwathHeight, 1) + 1;
		else
			*MaxNumSwath = 1;
		MaxPartialSwath = dml_max(1, (unsigned int) (vp_start_rot + *VInitPreFill - 1) % SwathHeight);
	} else {
		*MaxNumSwath = dml_ceil((*VInitPreFill - 1.0) / SwathHeight, 1) + 1;
		if (*VInitPreFill > 1)
			MaxPartialSwath = dml_max(1, (unsigned int) (*VInitPreFill - 2) % SwathHeight);
		else
			MaxPartialSwath = dml_max(1, (unsigned int) (*VInitPreFill + SwathHeight - 2) % SwathHeight);
	}
	numLines = *MaxNumSwath * SwathHeight + MaxPartialSwath;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: vp_start_rot = %d\n", __func__, vp_start_rot);
	dml_print("DML::%s: VInitPreFill = %d\n", __func__, *VInitPreFill);
	dml_print("DML::%s: MaxPartialSwath = %d\n", __func__, MaxPartialSwath);
	dml_print("DML::%s: MaxNumSwath = %d\n", __func__, *MaxNumSwath);
	dml_print("DML::%s: Prefetch source lines = %3.2f\n", __func__, numLines);
#endif
	return numLines;

} // CalculatePrefetchSourceLines

void dml32_CalculateMALLUseForStaticScreen(
		unsigned int NumberOfActiveSurfaces,
		unsigned int MALLAllocatedForDCNFinal,
		enum dm_use_mall_for_static_screen_mode *UseMALLForStaticScreen,
		unsigned int SurfaceSizeInMALL[],
		bool one_row_per_frame_fits_in_buffer[],

		/* output */
		bool UsesMALLForStaticScreen[])
{
	unsigned int k;
	unsigned int SurfaceToAddToMALL;
	bool CanAddAnotherSurfaceToMALL;
	unsigned int TotalSurfaceSizeInMALL;

	TotalSurfaceSizeInMALL = 0;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		UsesMALLForStaticScreen[k] = (UseMALLForStaticScreen[k] == dm_use_mall_static_screen_enable);
		if (UsesMALLForStaticScreen[k])
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, UsesMALLForStaticScreen = %d\n",  __func__, k, UsesMALLForStaticScreen[k]);
		dml_print("DML::%s: k=%d, TotalSurfaceSizeInMALL = %d\n",  __func__, k, TotalSurfaceSizeInMALL);
#endif
	}

	SurfaceToAddToMALL = 0;
	CanAddAnotherSurfaceToMALL = true;
	while (CanAddAnotherSurfaceToMALL) {
		CanAddAnotherSurfaceToMALL = false;
		for (k = 0; k < NumberOfActiveSurfaces; ++k) {
			if (TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k] <= MALLAllocatedForDCNFinal * 1024 * 1024 &&
					!UsesMALLForStaticScreen[k] &&
					UseMALLForStaticScreen[k] != dm_use_mall_static_screen_disable &&
					one_row_per_frame_fits_in_buffer[k] &&
					(!CanAddAnotherSurfaceToMALL ||
					SurfaceSizeInMALL[k] < SurfaceSizeInMALL[SurfaceToAddToMALL])) {
				CanAddAnotherSurfaceToMALL = true;
				SurfaceToAddToMALL = k;
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%d, UseMALLForStaticScreen = %d (dis, en, optimize)\n",
						__func__, k, UseMALLForStaticScreen[k]);
#endif
			}
		}
		if (CanAddAnotherSurfaceToMALL) {
			UsesMALLForStaticScreen[SurfaceToAddToMALL] = true;
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[SurfaceToAddToMALL];

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: SurfaceToAddToMALL       = %d\n",  __func__, SurfaceToAddToMALL);
			dml_print("DML::%s: TotalSurfaceSizeInMALL   = %d\n",  __func__, TotalSurfaceSizeInMALL);
#endif

		}
	}
}

void dml32_CalculateRowBandwidth(
		bool GPUVMEnable,
		enum source_format_class SourcePixelFormat,
		double VRatio,
		double VRatioChroma,
		bool DCCEnable,
		double LineTime,
		unsigned int MetaRowByteLuma,
		unsigned int MetaRowByteChroma,
		unsigned int meta_row_height_luma,
		unsigned int meta_row_height_chroma,
		unsigned int PixelPTEBytesPerRowLuma,
		unsigned int PixelPTEBytesPerRowChroma,
		unsigned int dpte_row_height_luma,
		unsigned int dpte_row_height_chroma,
		/* Output */
		double *meta_row_bw,
		double *dpte_row_bw)
{
	if (DCCEnable != true) {
		*meta_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12 ||
			SourcePixelFormat == dm_rgbe_alpha) {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime) + VRatioChroma *
				MetaRowByteChroma / (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12 ||
			SourcePixelFormat == dm_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime) +
				VRatioChroma * PixelPTEBytesPerRowChroma / (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

double dml32_CalculateUrgentLatency(
		double UrgentLatencyPixelDataOnly,
		double UrgentLatencyPixelMixedWithVMData,
		double UrgentLatencyVMDataOnly,
		bool   DoUrgentLatencyAdjustment,
		double UrgentLatencyAdjustmentFabricClockComponent,
		double UrgentLatencyAdjustmentFabricClockReference,
		double FabricClock)
{
	double   ret;

	ret = dml_max3(UrgentLatencyPixelDataOnly, UrgentLatencyPixelMixedWithVMData, UrgentLatencyVMDataOnly);
	if (DoUrgentLatencyAdjustment == true) {
		ret = ret + UrgentLatencyAdjustmentFabricClockComponent *
				(UrgentLatencyAdjustmentFabricClockReference / FabricClock - 1);
	}
	return ret;
}

void dml32_CalculateUrgentBurstFactor(
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange,
		unsigned int    swath_width_luma_ub,
		unsigned int    swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double  LineTime,
		double  UrgentLatency,
		double  CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int CursorBPP,
		double  VRatio,
		double  VRatioC,
		double  BytePerPixelInDETY,
		double  BytePerPixelInDETC,
		unsigned int    DETBufferSizeY,
		unsigned int    DETBufferSizeC,
		/* Output */
		double *UrgentBurstFactorCursor,
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorChroma,
		bool   *NotEnoughUrgentLatencyHiding)
{
	double       LinesInDETLuma;
	double       LinesInDETChroma;
	unsigned int LinesInCursorBuffer;
	double       CursorBufferSizeInTime;
	double       DETBufferSizeInTimeLuma;
	double       DETBufferSizeInTimeChroma;

	*NotEnoughUrgentLatencyHiding = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = 1 << (unsigned int) dml_floor(dml_log2(CursorBufferSize * 1024.0 /
				(CursorWidth * CursorBPP / 8.0)), 1.0);
		if (VRatio > 0) {
			CursorBufferSizeInTime = LinesInCursorBuffer * LineTime / VRatio;
			if (CursorBufferSizeInTime - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHiding = 1;
				*UrgentBurstFactorCursor = 0;
			} else {
				*UrgentBurstFactorCursor = CursorBufferSizeInTime /
						(CursorBufferSizeInTime - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorCursor = 1;
		}
	}

	LinesInDETLuma = (UseMALLForPStateChange == dm_use_mall_pstate_change_phantom_pipe ? 1024*1024 :
			DETBufferSizeY) / BytePerPixelInDETY / swath_width_luma_ub;

	if (VRatio > 0) {
		DETBufferSizeInTimeLuma = dml_floor(LinesInDETLuma, SwathHeightY) * LineTime / VRatio;
		if (DETBufferSizeInTimeLuma - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorLuma = 0;
		} else {
			*UrgentBurstFactorLuma = DETBufferSizeInTimeLuma / (DETBufferSizeInTimeLuma - UrgentLatency);
		}
	} else {
		*UrgentBurstFactorLuma = 1;
	}

	if (BytePerPixelInDETC > 0) {
		LinesInDETChroma = (UseMALLForPStateChange == dm_use_mall_pstate_change_phantom_pipe ?
					1024 * 1024 : DETBufferSizeC) / BytePerPixelInDETC
					/ swath_width_chroma_ub;

		if (VRatio > 0) {
			DETBufferSizeInTimeChroma = dml_floor(LinesInDETChroma, SwathHeightC) * LineTime / VRatio;
			if (DETBufferSizeInTimeChroma - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHiding = 1;
				*UrgentBurstFactorChroma = 0;
			} else {
				*UrgentBurstFactorChroma = DETBufferSizeInTimeChroma
						/ (DETBufferSizeInTimeChroma - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorChroma = 1;
		}
	}
} // CalculateUrgentBurstFactor

void dml32_CalculateDCFCLKDeepSleep(
		unsigned int NumberOfActiveSurfaces,
		unsigned int BytePerPixelY[],
		unsigned int BytePerPixelC[],
		double VRatio[],
		double VRatioChroma[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerSurface[],
		double HRatio[],
		double HRatioChroma[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double Dppclk[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		unsigned int ReturnBusWidth,

		/* Output */
		double *DCFClkDeepSleep)
{
	unsigned int k;
	double   DisplayPipeLineDeliveryTimeLuma;
	double   DisplayPipeLineDeliveryTimeChroma;
	double   DCFClkDeepSleepPerSurface[DC__NUM_DPP__MAX];
	double ReadBandwidth = 0.0;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {

		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerSurface[k] / HRatio[k]
					/ PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}
		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] *
						DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] / PSCL_THROUGHPUT_CHROMA[k]
						/ Dppclk[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			DCFClkDeepSleepPerSurface[k] = dml_max(__DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] *
					BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma,
					__DML_MIN_DCFCLK_FACTOR__ * SwathWidthC[k] * BytePerPixelC[k] /
					32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			DCFClkDeepSleepPerSurface[k] = __DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] /
					64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		DCFClkDeepSleepPerSurface[k] = dml_max(DCFClkDeepSleepPerSurface[k], PixelClock[k] / 16);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, PixelClock = %f\n", __func__, k, PixelClock[k]);
		dml_print("DML::%s: k=%d, DCFClkDeepSleepPerSurface = %f\n", __func__, k, DCFClkDeepSleepPerSurface[k]);
#endif
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k)
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];

	*DCFClkDeepSleep = dml_max(8.0, __DML_MIN_DCFCLK_FACTOR__ * ReadBandwidth / (double) ReturnBusWidth);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: __DML_MIN_DCFCLK_FACTOR__ = %f\n", __func__, __DML_MIN_DCFCLK_FACTOR__);
	dml_print("DML::%s: ReadBandwidth = %f\n", __func__, ReadBandwidth);
	dml_print("DML::%s: ReturnBusWidth = %d\n", __func__, ReturnBusWidth);
	dml_print("DML::%s: DCFClkDeepSleep = %f\n", __func__, *DCFClkDeepSleep);
#endif

	for (k = 0; k < NumberOfActiveSurfaces; ++k)
		*DCFClkDeepSleep = dml_max(*DCFClkDeepSleep, DCFClkDeepSleepPerSurface[k]);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DCFClkDeepSleep = %f (final)\n", __func__, *DCFClkDeepSleep);
#endif
} // CalculateDCFCLKDeepSleep

double dml32_CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackVTaps,
		unsigned int         WritebackDestinationWidth,
		unsigned int         WritebackDestinationHeight,
		unsigned int         WritebackSourceHeight,
		unsigned int HTotal)
{
	double CalculateWriteBackDelay;
	double Line_length;
	double Output_lines_last_notclamped;
	double WritebackVInit;

	WritebackVInit = (WritebackVRatio + WritebackVTaps + 1) / 2;
	Line_length = dml_max((double) WritebackDestinationWidth,
			dml_ceil((double)WritebackDestinationWidth / 6.0, 1.0) * WritebackVTaps);
	Output_lines_last_notclamped = WritebackDestinationHeight - 1 -
			dml_ceil(((double)WritebackSourceHeight -
					(double) WritebackVInit) / (double)WritebackVRatio, 1.0);
	if (Output_lines_last_notclamped < 0) {
		CalculateWriteBackDelay = 0;
	} else {
		CalculateWriteBackDelay = Output_lines_last_notclamped * Line_length +
				(HTotal - WritebackDestinationWidth) + 80;
	}
	return CalculateWriteBackDelay;
}

void dml32_UseMinimumDCFCLK(
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
		bool DRRDisplay[],
		bool SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int MaxPrefetchMode,
		double DRAMClockChangeLatencyFinal,
		double FCLKChangeLatency,
		double SREnterPlusExitTime,
		unsigned int ReturnBusWidth,
		unsigned int RoundTripPingLatencyCycles,
		unsigned int ReorderingBytes,
		unsigned int PixelChunkSizeInKByte,
		unsigned int MetaChunkSize,
		bool GPUVMEnable,
		unsigned int GPUVMMaxPageTableLevels,
		bool HostVMEnable,
		unsigned int NumberOfActiveSurfaces,
		double HostVMMinPageSize,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		bool DynamicMetadataVMEnabled,
		bool ImmediateFlipRequirement,
		bool ProgressiveToInterlaceUnitInOPP,
		double MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation,
		double PercentOfIdealSDPPortBWReceivedAfterUrgLatency,
		unsigned int VTotal[],
		unsigned int VActive[],
		unsigned int DynamicMetadataTransmittedBytes[],
		unsigned int DynamicMetadataLinesBeforeActiveRequired[],
		bool Interlace[],
		double RequiredDPPCLKPerSurface[][2][DC__NUM_DPP__MAX],
		double RequiredDISPCLK[][2],
		double UrgLatency[],
		unsigned int NoOfDPP[][2][DC__NUM_DPP__MAX],
		double ProjectedDCFClkDeepSleep[][2],
		double MaximumVStartup[][2][DC__NUM_DPP__MAX],
		unsigned int TotalNumberOfActiveDPP[][2],
		unsigned int TotalNumberOfDCCActiveDPP[][2],
		unsigned int dpte_group_bytes[],
		double PrefetchLinesY[][2][DC__NUM_DPP__MAX],
		double PrefetchLinesC[][2][DC__NUM_DPP__MAX],
		unsigned int swath_width_luma_ub_all_states[][2][DC__NUM_DPP__MAX],
		unsigned int swath_width_chroma_ub_all_states[][2][DC__NUM_DPP__MAX],
		unsigned int BytePerPixelY[],
		unsigned int BytePerPixelC[],
		unsigned int HTotal[],
		double PixelClock[],
		double PDEAndMetaPTEBytesPerFrame[][2][DC__NUM_DPP__MAX],
		double DPTEBytesPerRow[][2][DC__NUM_DPP__MAX],
		double MetaRowBytes[][2][DC__NUM_DPP__MAX],
		bool DynamicMetadataEnable[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double DCFCLKPerState[],
		/* Output */
		double DCFCLKState[][2])
{
	unsigned int i, j, k;
	unsigned int     dummy1;
	double dummy2, dummy3;
	double   NormalEfficiency;
	double   TotalMaxPrefetchFlipDPTERowBandwidth[DC__VOLTAGE_STATES][2];

	NormalEfficiency = PercentOfIdealSDPPortBWReceivedAfterUrgLatency / 100.0;
	for  (i = 0; i < DC__VOLTAGE_STATES; ++i) {
		for  (j = 0; j <= 1; ++j) {
			double PixelDCFCLKCyclesRequiredInPrefetch[DC__NUM_DPP__MAX];
			double PrefetchPixelLinesTime[DC__NUM_DPP__MAX];
			double DCFCLKRequiredForPeakBandwidthPerSurface[DC__NUM_DPP__MAX];
			double DynamicMetadataVMExtraLatency[DC__NUM_DPP__MAX];
			double MinimumTWait = 0.0;
			double DPTEBandwidth;
			double DCFCLKRequiredForAverageBandwidth;
			unsigned int ExtraLatencyBytes;
			double ExtraLatencyCycles;
			double DCFCLKRequiredForPeakBandwidth;
			unsigned int NoOfDPPState[DC__NUM_DPP__MAX];
			double MinimumTvmPlus2Tr0;

			TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = 0;
			for (k = 0; k < NumberOfActiveSurfaces; ++k) {
				TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = TotalMaxPrefetchFlipDPTERowBandwidth[i][j]
						+ NoOfDPP[i][j][k] * DPTEBytesPerRow[i][j][k]
								/ (15.75 * HTotal[k] / PixelClock[k]);
			}

			for (k = 0; k <= NumberOfActiveSurfaces - 1; ++k)
				NoOfDPPState[k] = NoOfDPP[i][j][k];

			DPTEBandwidth = TotalMaxPrefetchFlipDPTERowBandwidth[i][j];
			DCFCLKRequiredForAverageBandwidth = dml_max(ProjectedDCFClkDeepSleep[i][j], DPTEBandwidth / NormalEfficiency / ReturnBusWidth);

			ExtraLatencyBytes = dml32_CalculateExtraLatencyBytes(ReorderingBytes,
					TotalNumberOfActiveDPP[i][j], PixelChunkSizeInKByte,
					TotalNumberOfDCCActiveDPP[i][j], MetaChunkSize, GPUVMEnable, HostVMEnable,
					NumberOfActiveSurfaces, NoOfDPPState, dpte_group_bytes, 1, HostVMMinPageSize,
					HostVMMaxNonCachedPageTableLevels);
			ExtraLatencyCycles = RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__
					+ ExtraLatencyBytes / NormalEfficiency / ReturnBusWidth;
			for (k = 0; k < NumberOfActiveSurfaces; ++k) {
				double DCFCLKCyclesRequiredInPrefetch;
				double PrefetchTime;

				PixelDCFCLKCyclesRequiredInPrefetch[k] = (PrefetchLinesY[i][j][k]
						* swath_width_luma_ub_all_states[i][j][k] * BytePerPixelY[k]
						+ PrefetchLinesC[i][j][k] * swath_width_chroma_ub_all_states[i][j][k]
								* BytePerPixelC[k]) / NormalEfficiency
						/ ReturnBusWidth;
				DCFCLKCyclesRequiredInPrefetch = 2 * ExtraLatencyCycles / NoOfDPPState[k]
						+ PDEAndMetaPTEBytesPerFrame[i][j][k] / NormalEfficiency
								/ NormalEfficiency / ReturnBusWidth
								* (GPUVMMaxPageTableLevels > 2 ? 1 : 0)
						+ 2 * DPTEBytesPerRow[i][j][k] / NormalEfficiency / NormalEfficiency
								/ ReturnBusWidth
						+ 2 * MetaRowBytes[i][j][k] / NormalEfficiency / ReturnBusWidth
						+ PixelDCFCLKCyclesRequiredInPrefetch[k];
				PrefetchPixelLinesTime[k] = dml_max(PrefetchLinesY[i][j][k], PrefetchLinesC[i][j][k])
						* HTotal[k] / PixelClock[k];
				DynamicMetadataVMExtraLatency[k] = (GPUVMEnable == true &&
						DynamicMetadataEnable[k] == true && DynamicMetadataVMEnabled == true) ?
						UrgLatency[i] * GPUVMMaxPageTableLevels *
						(HostVMEnable == true ? HostVMMaxNonCachedPageTableLevels + 1 : 1) : 0;

				MinimumTWait = dml32_CalculateTWait(MaxPrefetchMode,
						UseMALLForPStateChange[k],
						SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
						DRRDisplay[k],
						DRAMClockChangeLatencyFinal,
						FCLKChangeLatency,
						UrgLatency[i],
						SREnterPlusExitTime);

				PrefetchTime = (MaximumVStartup[i][j][k] - 1) * HTotal[k] / PixelClock[k] -
						MinimumTWait - UrgLatency[i] *
						((GPUVMMaxPageTableLevels <= 2 ? GPUVMMaxPageTableLevels :
						GPUVMMaxPageTableLevels - 2) *  (HostVMEnable == true ?
						HostVMMaxNonCachedPageTableLevels + 1 : 1) - 1) -
						DynamicMetadataVMExtraLatency[k];

				if (PrefetchTime > 0) {
					double ExpectedVRatioPrefetch;

					ExpectedVRatioPrefetch = PrefetchPixelLinesTime[k] / (PrefetchTime *
							PixelDCFCLKCyclesRequiredInPrefetch[k] /
							DCFCLKCyclesRequiredInPrefetch);
					DCFCLKRequiredForPeakBandwidthPerSurface[k] = NoOfDPPState[k] *
							PixelDCFCLKCyclesRequiredInPrefetch[k] /
							PrefetchPixelLinesTime[k] *
							dml_max(1.0, ExpectedVRatioPrefetch) *
							dml_max(1.0, ExpectedVRatioPrefetch / 4);
					if (HostVMEnable == true || ImmediateFlipRequirement == true) {
						DCFCLKRequiredForPeakBandwidthPerSurface[k] =
								DCFCLKRequiredForPeakBandwidthPerSurface[k] +
								NoOfDPPState[k] * DPTEBandwidth / NormalEfficiency /
								NormalEfficiency / ReturnBusWidth;
					}
				} else {
					DCFCLKRequiredForPeakBandwidthPerSurface[k] = DCFCLKPerState[i];
				}
				if (DynamicMetadataEnable[k] == true) {
					double TSetupPipe;
					double TdmbfPipe;
					double TdmsksPipe;
					double TdmecPipe;
					double AllowedTimeForUrgentExtraLatency;

					dml32_CalculateVUpdateAndDynamicMetadataParameters(
							MaxInterDCNTileRepeaters,
							RequiredDPPCLKPerSurface[i][j][k],
							RequiredDISPCLK[i][j],
							ProjectedDCFClkDeepSleep[i][j],
							PixelClock[k],
							HTotal[k],
							VTotal[k] - VActive[k],
							DynamicMetadataTransmittedBytes[k],
							DynamicMetadataLinesBeforeActiveRequired[k],
							Interlace[k],
							ProgressiveToInterlaceUnitInOPP,

							/* output */
							&TSetupPipe,
							&TdmbfPipe,
							&TdmecPipe,
							&TdmsksPipe,
							&dummy1,
							&dummy2,
							&dummy3);
					AllowedTimeForUrgentExtraLatency = MaximumVStartup[i][j][k] * HTotal[k] /
							PixelClock[k] - MinimumTWait - TSetupPipe - TdmbfPipe -
							TdmecPipe - TdmsksPipe - DynamicMetadataVMExtraLatency[k];
					if (AllowedTimeForUrgentExtraLatency > 0)
						DCFCLKRequiredForPeakBandwidthPerSurface[k] =
								dml_max(DCFCLKRequiredForPeakBandwidthPerSurface[k],
								ExtraLatencyCycles / AllowedTimeForUrgentExtraLatency);
					else
						DCFCLKRequiredForPeakBandwidthPerSurface[k] = DCFCLKPerState[i];
				}
			}
			DCFCLKRequiredForPeakBandwidth = 0;
			for (k = 0; k <= NumberOfActiveSurfaces - 1; ++k) {
				DCFCLKRequiredForPeakBandwidth = DCFCLKRequiredForPeakBandwidth +
						DCFCLKRequiredForPeakBandwidthPerSurface[k];
			}
			MinimumTvmPlus2Tr0 = UrgLatency[i] * (GPUVMEnable == true ?
					(HostVMEnable == true ? (GPUVMMaxPageTableLevels + 2) *
					(HostVMMaxNonCachedPageTableLevels + 1) - 1 : GPUVMMaxPageTableLevels + 1) : 0);
			for (k = 0; k < NumberOfActiveSurfaces; ++k) {
				double MaximumTvmPlus2Tr0PlusTsw;

				MaximumTvmPlus2Tr0PlusTsw = (MaximumVStartup[i][j][k] - 2) * HTotal[k] /
						PixelClock[k] - MinimumTWait - DynamicMetadataVMExtraLatency[k];
				if (MaximumTvmPlus2Tr0PlusTsw <= MinimumTvmPlus2Tr0 + PrefetchPixelLinesTime[k] / 4) {
					DCFCLKRequiredForPeakBandwidth = DCFCLKPerState[i];
				} else {
					DCFCLKRequiredForPeakBandwidth = dml_max3(DCFCLKRequiredForPeakBandwidth,
							2 * ExtraLatencyCycles / (MaximumTvmPlus2Tr0PlusTsw -
								MinimumTvmPlus2Tr0 -
								PrefetchPixelLinesTime[k] / 4),
							(2 * ExtraLatencyCycles +
								PixelDCFCLKCyclesRequiredInPrefetch[k]) /
								(MaximumTvmPlus2Tr0PlusTsw - MinimumTvmPlus2Tr0));
				}
			}
			DCFCLKState[i][j] = dml_min(DCFCLKPerState[i], 1.05 *
					dml_max(DCFCLKRequiredForAverageBandwidth, DCFCLKRequiredForPeakBandwidth));
		}
	}
}

unsigned int dml32_CalculateExtraLatencyBytes(unsigned int ReorderingBytes,
		unsigned int TotalNumberOfActiveDPP,
		unsigned int PixelChunkSizeInKByte,
		unsigned int TotalNumberOfDCCActiveDPP,
		unsigned int MetaChunkSize,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int NumberOfActiveSurfaces,
		unsigned int NumberOfDPP[],
		unsigned int dpte_group_bytes[],
		double HostVMInefficiencyFactor,
		double HostVMMinPageSize,
		unsigned int HostVMMaxNonCachedPageTableLevels)
{
	unsigned int k;
	double   ret;
	unsigned int  HostVMDynamicLevels;

	if (GPUVMEnable == true && HostVMEnable == true) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 2);
	} else {
		HostVMDynamicLevels = 0;
	}

	ret = ReorderingBytes + (TotalNumberOfActiveDPP * PixelChunkSizeInKByte +
			TotalNumberOfDCCActiveDPP * MetaChunkSize) * 1024.0;

	if (GPUVMEnable == true) {
		for (k = 0; k < NumberOfActiveSurfaces; ++k) {
			ret = ret + NumberOfDPP[k] * dpte_group_bytes[k] *
					(1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
		}
	}
	return ret;
}

void dml32_CalculateVUpdateAndDynamicMetadataParameters(
		unsigned int MaxInterDCNTileRepeaters,
		double Dppclk,
		double Dispclk,
		double DCFClkDeepSleep,
		double PixelClock,
		unsigned int HTotal,
		unsigned int VBlank,
		unsigned int DynamicMetadataTransmittedBytes,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,

		/* output */
		double *TSetup,
		double *Tdmbf,
		double *Tdmec,
		double *Tdmsks,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
{
	double TotalRepeaterDelayTime;

	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / Dppclk + 3 / Dispclk);
	*VUpdateWidthPix  =
			dml_ceil((14.0 / DCFClkDeepSleep + 12.0 / Dppclk + TotalRepeaterDelayTime) * PixelClock, 1.0);
	*VReadyOffsetPix  = dml_ceil(dml_max(150.0 / Dppclk,
			TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / Dppclk) * PixelClock, 1.0);
	*VUpdateOffsetPix = dml_ceil(HTotal / 4.0, 1.0);
	*TSetup = (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;
	*Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / Dispclk;
	*Tdmec = HTotal / PixelClock;

	if (DynamicMetadataLinesBeforeActiveRequired == 0)
		*Tdmsks = VBlank * HTotal / PixelClock / 2.0;
	else
		*Tdmsks = DynamicMetadataLinesBeforeActiveRequired * HTotal / PixelClock;

	if (InterlaceEnable == 1 && ProgressiveToInterlaceUnitInOPP == false)
		*Tdmsks = *Tdmsks / 2;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VUpdateWidthPix = %d\n", __func__, *VUpdateWidthPix);
	dml_print("DML::%s: VReadyOffsetPix = %d\n", __func__, *VReadyOffsetPix);
	dml_print("DML::%s: VUpdateOffsetPix = %d\n", __func__, *VUpdateOffsetPix);

	dml_print("DML::%s: DynamicMetadataLinesBeforeActiveRequired = %d\n",
			__func__, DynamicMetadataLinesBeforeActiveRequired);
	dml_print("DML::%s: VBlank = %d\n", __func__, VBlank);
	dml_print("DML::%s: HTotal = %d\n", __func__, HTotal);
	dml_print("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	dml_print("DML::%s: Tdmsks = %f\n", __func__, *Tdmsks);
#endif
}

double dml32_CalculateTWait(
		unsigned int PrefetchMode,
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange,
		bool SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
		bool DRRDisplay,
		double DRAMClockChangeLatency,
		double FCLKChangeLatency,
		double UrgentLatency,
		double SREnterPlusExitTime)
{
	double TWait = 0.0;

	if (PrefetchMode == 0 &&
			!(UseMALLForPStateChange == dm_use_mall_pstate_change_full_frame) &&
			!(UseMALLForPStateChange == dm_use_mall_pstate_change_sub_viewport) &&
			!(UseMALLForPStateChange == dm_use_mall_pstate_change_phantom_pipe) &&
			!(SynchronizeDRRDisplaysForUCLKPStateChangeFinal && DRRDisplay)) {
		TWait = dml_max3(DRAMClockChangeLatency + UrgentLatency, SREnterPlusExitTime, UrgentLatency);
	} else if (PrefetchMode <= 1 && !(UseMALLForPStateChange == dm_use_mall_pstate_change_phantom_pipe)) {
		TWait = dml_max3(FCLKChangeLatency + UrgentLatency, SREnterPlusExitTime, UrgentLatency);
	} else if (PrefetchMode <= 2 && !(UseMALLForPStateChange == dm_use_mall_pstate_change_phantom_pipe)) {
		TWait = dml_max(SREnterPlusExitTime, UrgentLatency);
	} else {
		TWait = UrgentLatency;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: PrefetchMode = %d\n", __func__, PrefetchMode);
	dml_print("DML::%s: TWait = %f\n", __func__, TWait);
#endif
	return TWait;
} // CalculateTWait

// Function: get_return_bw_mbps
// Megabyte per second
double dml32_get_return_bw_mbps(const soc_bounding_box_st *soc,
		const int VoltageLevel,
		const bool HostVMEnable,
		const double DCFCLK,
		const double FabricClock,
		const double DRAMSpeed)
{
	double ReturnBW = 0.;
	double IdealSDPPortBandwidth    = soc->return_bus_width_bytes /*mode_lib->vba.ReturnBusWidth*/ * DCFCLK;
	double IdealFabricBandwidth     = FabricClock * soc->fabric_datapath_to_dcn_data_return_bytes;
	double IdealDRAMBandwidth       = DRAMSpeed * soc->num_chans * soc->dram_channel_width_bytes;
	double PixelDataOnlyReturnBW    = dml_min3(IdealSDPPortBandwidth * soc->pct_ideal_sdp_bw_after_urgent / 100,
			IdealFabricBandwidth * soc->pct_ideal_fabric_bw_after_urgent / 100,
			IdealDRAMBandwidth * (VoltageLevel < 2 ? soc->pct_ideal_dram_bw_after_urgent_strobe  :
					soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_only) / 100);
	double PixelMixedWithVMDataReturnBW = dml_min3(IdealSDPPortBandwidth * soc->pct_ideal_sdp_bw_after_urgent / 100,
			IdealFabricBandwidth * soc->pct_ideal_fabric_bw_after_urgent / 100,
			IdealDRAMBandwidth * (VoltageLevel < 2 ? soc->pct_ideal_dram_bw_after_urgent_strobe :
					soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_only) / 100);

	if (HostVMEnable != true)
		ReturnBW = PixelDataOnlyReturnBW;
	else
		ReturnBW = PixelMixedWithVMDataReturnBW;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VoltageLevel = %d\n", __func__, VoltageLevel);
	dml_print("DML::%s: HostVMEnable = %d\n", __func__, HostVMEnable);
	dml_print("DML::%s: DCFCLK       = %f\n", __func__, DCFCLK);
	dml_print("DML::%s: FabricClock  = %f\n", __func__, FabricClock);
	dml_print("DML::%s: DRAMSpeed    = %f\n", __func__, DRAMSpeed);
	dml_print("DML::%s: IdealSDPPortBandwidth        = %f\n", __func__, IdealSDPPortBandwidth);
	dml_print("DML::%s: IdealFabricBandwidth         = %f\n", __func__, IdealFabricBandwidth);
	dml_print("DML::%s: IdealDRAMBandwidth           = %f\n", __func__, IdealDRAMBandwidth);
	dml_print("DML::%s: PixelDataOnlyReturnBW        = %f\n", __func__, PixelDataOnlyReturnBW);
	dml_print("DML::%s: PixelMixedWithVMDataReturnBW = %f\n", __func__, PixelMixedWithVMDataReturnBW);
	dml_print("DML::%s: ReturnBW                     = %f MBps\n", __func__, ReturnBW);
#endif
	return ReturnBW;
}

// Function: get_return_bw_mbps_vm_only
// Megabyte per second
double dml32_get_return_bw_mbps_vm_only(const soc_bounding_box_st *soc,
		const int VoltageLevel,
		const double DCFCLK,
		const double FabricClock,
		const double DRAMSpeed)
{
	double VMDataOnlyReturnBW = dml_min3(
			soc->return_bus_width_bytes * DCFCLK * soc->pct_ideal_sdp_bw_after_urgent / 100.0,
			FabricClock * soc->fabric_datapath_to_dcn_data_return_bytes
					* soc->pct_ideal_sdp_bw_after_urgent / 100.0,
			DRAMSpeed * soc->num_chans * soc->dram_channel_width_bytes
					* (VoltageLevel < 2 ?
							soc->pct_ideal_dram_bw_after_urgent_strobe :
							soc->pct_ideal_dram_sdp_bw_after_urgent_vm_only) / 100.0);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VoltageLevel = %d\n", __func__, VoltageLevel);
	dml_print("DML::%s: DCFCLK       = %f\n", __func__, DCFCLK);
	dml_print("DML::%s: FabricClock  = %f\n", __func__, FabricClock);
	dml_print("DML::%s: DRAMSpeed    = %f\n", __func__, DRAMSpeed);
	dml_print("DML::%s: VMDataOnlyReturnBW = %f\n", __func__, VMDataOnlyReturnBW);
#endif
	return VMDataOnlyReturnBW;
}

double dml32_CalculateExtraLatency(
		unsigned int RoundTripPingLatencyCycles,
		unsigned int ReorderingBytes,
		double DCFCLK,
		unsigned int TotalNumberOfActiveDPP,
		unsigned int PixelChunkSizeInKByte,
		unsigned int TotalNumberOfDCCActiveDPP,
		unsigned int MetaChunkSize,
		double ReturnBW,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int NumberOfActiveSurfaces,
		unsigned int NumberOfDPP[],
		unsigned int dpte_group_bytes[],
		double HostVMInefficiencyFactor,
		double HostVMMinPageSize,
		unsigned int HostVMMaxNonCachedPageTableLevels)
{
	double ExtraLatencyBytes;
	double ExtraLatency;

	ExtraLatencyBytes = dml32_CalculateExtraLatencyBytes(
			ReorderingBytes,
			TotalNumberOfActiveDPP,
			PixelChunkSizeInKByte,
			TotalNumberOfDCCActiveDPP,
			MetaChunkSize,
			GPUVMEnable,
			HostVMEnable,
			NumberOfActiveSurfaces,
			NumberOfDPP,
			dpte_group_bytes,
			HostVMInefficiencyFactor,
			HostVMMinPageSize,
			HostVMMaxNonCachedPageTableLevels);

	ExtraLatency = (RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__) / DCFCLK + ExtraLatencyBytes / ReturnBW;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: RoundTripPingLatencyCycles=%d\n", __func__, RoundTripPingLatencyCycles);
	dml_print("DML::%s: DCFCLK=%f\n", __func__, DCFCLK);
	dml_print("DML::%s: ExtraLatencyBytes=%f\n", __func__, ExtraLatencyBytes);
	dml_print("DML::%s: ReturnBW=%f\n", __func__, ReturnBW);
	dml_print("DML::%s: ExtraLatency=%f\n", __func__, ExtraLatency);
#endif

	return ExtraLatency;
} // CalculateExtraLatency

bool dml32_CalculatePrefetchSchedule(
		struct vba_vars_st *v,
		unsigned int k,
		double HostVMInefficiencyFactor,
		DmlPipe *myPipe,
		unsigned int DSCDelay,
		unsigned int DPP_RECOUT_WIDTH,
		unsigned int VStartup,
		unsigned int MaxVStartup,
		double UrgentLatency,
		double UrgentExtraLatency,
		double TCalc,
		unsigned int PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		double PrefetchSourceLinesY,
		unsigned int SwathWidthY,
		unsigned int VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		unsigned int SwathWidthC,
		unsigned int VInitPreFillC,
		unsigned int MaxNumSwathC,
		unsigned int swath_width_luma_ub,
		unsigned int swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
		double TPreReq,
		bool ExtendPrefetchIfPossible,
		/* Output */
		double   *DSTXAfterScaler,
		double   *DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBWLuma,
		double *RequiredPrefetchPixDataBWChroma,
		bool   *NotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		double *prefetch_vmrow_bw,
		double *Tdmdl_vm,
		double *Tdmdl,
		double *TSetup,
		unsigned int   *VUpdateOffsetPix,
		double   *VUpdateWidthPix,
		double   *VReadyOffsetPix)
{
	double DPPCLKDelaySubtotalPlusCNVCFormater = v->DPPCLKDelaySubtotal + v->DPPCLKDelayCNVCFormater;
	bool MyError = false;
	unsigned int DPPCycles, DISPCLKCycles;
	double DSTTotalPixelsAfterScaler;
	double LineTime;
	double dst_y_prefetch_equ;
	double prefetch_bw_oto;
	double Tvm_oto;
	double Tr0_oto;
	double Tvm_oto_lines;
	double Tr0_oto_lines;
	double dst_y_prefetch_oto;
	double TimeForFetchingMetaPTE = 0;
	double TimeForFetchingRowInVBlank = 0;
	double LinesToRequestPrefetchPixelData = 0;
	double LinesForPrefetchBandwidth = 0;
	unsigned int HostVMDynamicLevelsTrips;
	double  trip_to_mem;
	double  Tvm_trips;
	double  Tr0_trips;
	double  Tvm_trips_rounded;
	double  Tr0_trips_rounded;
	double  Lsw_oto;
	double  Tpre_rounded;
	double  prefetch_bw_equ;
	double  Tvm_equ;
	double  Tr0_equ;
	double  Tdmbf;
	double  Tdmec;
	double  Tdmsks;
	double  prefetch_sw_bytes;
	double  bytes_pp;
	double  dep_bytes;
	unsigned int max_vratio_pre = v->MaxVRatioPre;
	double  min_Lsw;
	double  Tsw_est1 = 0;
	double  Tsw_est3 = 0;

	if (v->GPUVMEnable == true && v->HostVMEnable == true)
		HostVMDynamicLevelsTrips = v->HostVMMaxNonCachedPageTableLevels;
	else
		HostVMDynamicLevelsTrips = 0;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: v->GPUVMEnable = %d\n", __func__, v->GPUVMEnable);
	dml_print("DML::%s: v->GPUVMMaxPageTableLevels = %d\n", __func__, v->GPUVMMaxPageTableLevels);
	dml_print("DML::%s: DCCEnable = %d\n", __func__, myPipe->DCCEnable);
	dml_print("DML::%s: v->HostVMEnable=%d HostVMInefficiencyFactor=%f\n",
			__func__, v->HostVMEnable, HostVMInefficiencyFactor);
#endif
	dml32_CalculateVUpdateAndDynamicMetadataParameters(
			v->MaxInterDCNTileRepeaters,
			myPipe->Dppclk,
			myPipe->Dispclk,
			myPipe->DCFClkDeepSleep,
			myPipe->PixelClock,
			myPipe->HTotal,
			myPipe->VBlank,
			v->DynamicMetadataTransmittedBytes[k],
			v->DynamicMetadataLinesBeforeActiveRequired[k],
			myPipe->InterlaceEnable,
			myPipe->ProgressiveToInterlaceUnitInOPP,
			TSetup,

			/* output */
			&Tdmbf,
			&Tdmec,
			&Tdmsks,
			VUpdateOffsetPix,
			VUpdateWidthPix,
			VReadyOffsetPix);

	LineTime = myPipe->HTotal / myPipe->PixelClock;
	trip_to_mem = UrgentLatency;
	Tvm_trips = UrgentExtraLatency + trip_to_mem * (v->GPUVMMaxPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1);

	if (v->DynamicMetadataVMEnabled == true)
		*Tdmdl = TWait + Tvm_trips + trip_to_mem;
	else
		*Tdmdl = TWait + UrgentExtraLatency;

#ifdef __DML_VBA_ALLOW_DELTA__
	if (v->DynamicMetadataEnable[k] == false)
		*Tdmdl = 0.0;
#endif

	if (v->DynamicMetadataEnable[k] == true) {
		if (VStartup * LineTime < *TSetup + *Tdmdl + Tdmbf + Tdmec + Tdmsks) {
			*NotEnoughTimeForDynamicMetadata = true;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: Not Enough Time for Dynamic Meta!\n", __func__);
			dml_print("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n",
					__func__, Tdmbf);
			dml_print("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, Tdmec);
			dml_print("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n",
					__func__, Tdmsks);
			dml_print("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd\n",
					__func__, *Tdmdl);
#endif
		} else {
			*NotEnoughTimeForDynamicMetadata = false;
		}
	} else {
		*NotEnoughTimeForDynamicMetadata = false;
	}

	*Tdmdl_vm =  (v->DynamicMetadataEnable[k] == true && v->DynamicMetadataVMEnabled == true &&
			v->GPUVMEnable == true ? TWait + Tvm_trips : 0);

	if (myPipe->ScalerEnabled)
		DPPCycles = DPPCLKDelaySubtotalPlusCNVCFormater + v->DPPCLKDelaySCL;
	else
		DPPCycles = DPPCLKDelaySubtotalPlusCNVCFormater + v->DPPCLKDelaySCLLBOnly;

	DPPCycles = DPPCycles + myPipe->NumberOfCursors * v->DPPCLKDelayCNVCCursor;

	DISPCLKCycles = v->DISPCLKDelaySubtotal;

	if (myPipe->Dppclk == 0.0 || myPipe->Dispclk == 0.0)
		return true;

	*DSTXAfterScaler = DPPCycles * myPipe->PixelClock / myPipe->Dppclk + DISPCLKCycles *
			myPipe->PixelClock / myPipe->Dispclk + DSCDelay;

	*DSTXAfterScaler = *DSTXAfterScaler + (myPipe->ODMMode != dm_odm_combine_mode_disabled ? 18 : 0)
			+ (myPipe->DPPPerSurface - 1) * DPP_RECOUT_WIDTH
			+ ((myPipe->ODMMode == dm_odm_split_mode_1to2 || myPipe->ODMMode == dm_odm_mode_mso_1to2) ?
					myPipe->HActive / 2 : 0)
			+ ((myPipe->ODMMode == dm_odm_mode_mso_1to4) ? myPipe->HActive * 3 / 4 : 0);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DPPCycles: %d\n", __func__, DPPCycles);
	dml_print("DML::%s: PixelClock: %f\n", __func__, myPipe->PixelClock);
	dml_print("DML::%s: Dppclk: %f\n", __func__, myPipe->Dppclk);
	dml_print("DML::%s: DISPCLKCycles: %d\n", __func__, DISPCLKCycles);
	dml_print("DML::%s: DISPCLK: %f\n", __func__,  myPipe->Dispclk);
	dml_print("DML::%s: DSCDelay: %d\n", __func__,  DSCDelay);
	dml_print("DML::%s: ODMMode: %d\n", __func__,  myPipe->ODMMode);
	dml_print("DML::%s: DPP_RECOUT_WIDTH: %d\n", __func__, DPP_RECOUT_WIDTH);
	dml_print("DML::%s: DSTXAfterScaler: %d\n", __func__,  *DSTXAfterScaler);
#endif

	if (v->OutputFormat[k] == dm_420 || (myPipe->InterlaceEnable && myPipe->ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = *DSTYAfterScaler * myPipe->HTotal + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / myPipe->HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * myPipe->HTotal));
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSTXAfterScaler: %d (final)\n", __func__,  *DSTXAfterScaler);
	dml_print("DML::%s: DSTYAfterScaler: %d (final)\n", __func__, *DSTYAfterScaler);
#endif

	MyError = false;

	Tr0_trips = trip_to_mem * (HostVMDynamicLevelsTrips + 1);

	if (v->GPUVMEnable == true) {
		Tvm_trips_rounded = dml_ceil(4.0 * Tvm_trips / LineTime, 1.0) / 4.0 * LineTime;
		Tr0_trips_rounded = dml_ceil(4.0 * Tr0_trips / LineTime, 1.0) / 4.0 * LineTime;
		if (v->GPUVMMaxPageTableLevels >= 3) {
			*Tno_bw = UrgentExtraLatency + trip_to_mem *
					(double) ((v->GPUVMMaxPageTableLevels - 2) * (HostVMDynamicLevelsTrips + 1) - 1);
		} else if (v->GPUVMMaxPageTableLevels == 1 && myPipe->DCCEnable != true) {
			Tr0_trips_rounded = dml_ceil(4.0 * UrgentExtraLatency / LineTime, 1.0) /
					4.0 * LineTime; // VBA_ERROR
			*Tno_bw = UrgentExtraLatency;
		} else {
			*Tno_bw = 0;
		}
	} else if (myPipe->DCCEnable == true) {
		Tvm_trips_rounded = LineTime / 4.0;
		Tr0_trips_rounded = dml_ceil(4.0 * Tr0_trips / LineTime, 1.0) / 4.0 * LineTime;
		*Tno_bw = 0;
	} else {
		Tvm_trips_rounded = LineTime / 4.0;
		Tr0_trips_rounded = LineTime / 2.0;
		*Tno_bw = 0;
	}
	Tvm_trips_rounded = dml_max(Tvm_trips_rounded, LineTime / 4.0);
	Tr0_trips_rounded = dml_max(Tr0_trips_rounded, LineTime / 4.0);

	if (myPipe->SourcePixelFormat == dm_420_8 || myPipe->SourcePixelFormat == dm_420_10
			|| myPipe->SourcePixelFormat == dm_420_12) {
		bytes_pp = myPipe->BytePerPixelY + myPipe->BytePerPixelC / 4;
	} else {
		bytes_pp = myPipe->BytePerPixelY + myPipe->BytePerPixelC;
	}

	prefetch_sw_bytes = PrefetchSourceLinesY * swath_width_luma_ub * myPipe->BytePerPixelY
			+ PrefetchSourceLinesC * swath_width_chroma_ub * myPipe->BytePerPixelC;
	prefetch_bw_oto = dml_max(bytes_pp * myPipe->PixelClock / myPipe->DPPPerSurface,
			prefetch_sw_bytes / (dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime));

	min_Lsw = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) / max_vratio_pre;
	min_Lsw = dml_max(min_Lsw, 1.0);
	Lsw_oto = dml_ceil(4.0 * dml_max(prefetch_sw_bytes / prefetch_bw_oto / LineTime, min_Lsw), 1.0) / 4.0;

	if (v->GPUVMEnable == true) {
		Tvm_oto = dml_max3(
				Tvm_trips,
				*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_oto,
				LineTime / 4.0);
	} else
		Tvm_oto = LineTime / 4.0;

	if ((v->GPUVMEnable == true || myPipe->DCCEnable == true)) {
		Tr0_oto = dml_max4(
				Tr0_trips,
				(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_oto,
				(LineTime - Tvm_oto)/2.0,
				LineTime / 4.0);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Tr0_oto max0 = %f\n", __func__,
				(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_oto);
		dml_print("DML::%s: Tr0_oto max1 = %f\n", __func__, Tr0_trips);
		dml_print("DML::%s: Tr0_oto max2 = %f\n", __func__, LineTime - Tvm_oto);
		dml_print("DML::%s: Tr0_oto max3 = %f\n", __func__, LineTime / 4);
#endif
	} else
		Tr0_oto = (LineTime - Tvm_oto) / 2.0;

	Tvm_oto_lines = dml_ceil(4.0 * Tvm_oto / LineTime, 1) / 4.0;
	Tr0_oto_lines = dml_ceil(4.0 * Tr0_oto / LineTime, 1) / 4.0;
	dst_y_prefetch_oto = Tvm_oto_lines + 2 * Tr0_oto_lines + Lsw_oto;

	dst_y_prefetch_equ = VStartup - (*TSetup + dml_max(TWait + TCalc, *Tdmdl)) / LineTime -
			(*DSTYAfterScaler + (double) *DSTXAfterScaler / (double) myPipe->HTotal);

	dst_y_prefetch_equ = dml_min(dst_y_prefetch_equ, __DML_VBA_MAX_DST_Y_PRE__);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: HTotal = %d\n", __func__, myPipe->HTotal);
	dml_print("DML::%s: min_Lsw = %f\n", __func__, min_Lsw);
	dml_print("DML::%s: *Tno_bw = %f\n", __func__, *Tno_bw);
	dml_print("DML::%s: UrgentExtraLatency = %f\n", __func__, UrgentExtraLatency);
	dml_print("DML::%s: trip_to_mem = %f\n", __func__, trip_to_mem);
	dml_print("DML::%s: BytePerPixelY = %d\n", __func__, myPipe->BytePerPixelY);
	dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, PrefetchSourceLinesY);
	dml_print("DML::%s: swath_width_luma_ub = %d\n", __func__, swath_width_luma_ub);
	dml_print("DML::%s: BytePerPixelC = %d\n", __func__, myPipe->BytePerPixelC);
	dml_print("DML::%s: PrefetchSourceLinesC = %f\n", __func__, PrefetchSourceLinesC);
	dml_print("DML::%s: swath_width_chroma_ub = %d\n", __func__, swath_width_chroma_ub);
	dml_print("DML::%s: prefetch_sw_bytes = %f\n", __func__, prefetch_sw_bytes);
	dml_print("DML::%s: bytes_pp = %f\n", __func__, bytes_pp);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: MetaRowByte = %d\n", __func__, MetaRowByte);
	dml_print("DML::%s: PixelPTEBytesPerRow = %d\n", __func__, PixelPTEBytesPerRow);
	dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	dml_print("DML::%s: Tvm_trips = %f\n", __func__, Tvm_trips);
	dml_print("DML::%s: Tr0_trips = %f\n", __func__, Tr0_trips);
	dml_print("DML::%s: prefetch_bw_oto = %f\n", __func__, prefetch_bw_oto);
	dml_print("DML::%s: Tr0_oto = %f\n", __func__, Tr0_oto);
	dml_print("DML::%s: Tvm_oto = %f\n", __func__, Tvm_oto);
	dml_print("DML::%s: Tvm_oto_lines = %f\n", __func__, Tvm_oto_lines);
	dml_print("DML::%s: Tr0_oto_lines = %f\n", __func__, Tr0_oto_lines);
	dml_print("DML::%s: Lsw_oto = %f\n", __func__, Lsw_oto);
	dml_print("DML::%s: dst_y_prefetch_oto = %f\n", __func__, dst_y_prefetch_oto);
	dml_print("DML::%s: dst_y_prefetch_equ = %f\n", __func__, dst_y_prefetch_equ);
#endif

	dst_y_prefetch_equ = dml_floor(4.0 * (dst_y_prefetch_equ + 0.125), 1) / 4.0;
	Tpre_rounded = dst_y_prefetch_equ * LineTime;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: dst_y_prefetch_equ: %f (after round)\n", __func__, dst_y_prefetch_equ);
	dml_print("DML::%s: LineTime: %f\n", __func__, LineTime);
	dml_print("DML::%s: VStartup: %d\n", __func__, VStartup);
	dml_print("DML::%s: Tvstartup: %fus - time between vstartup and first pixel of active\n",
			__func__, VStartup * LineTime);
	dml_print("DML::%s: TSetup: %fus - time from vstartup to vready\n", __func__, *TSetup);
	dml_print("DML::%s: TCalc: %fus - time for calculations in dchub starting at vready\n", __func__, TCalc);
	dml_print("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, Tdmbf);
	dml_print("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, Tdmec);
	dml_print("DML::%s: Tdmdl_vm: %fus - time for vm stages of dmd\n", __func__, *Tdmdl_vm);
	dml_print("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd\n", __func__, *Tdmdl);
	dml_print("DML::%s: DSTYAfterScaler: %d lines - number of lines of pipeline and buffer delay after scaler\n",
			__func__, *DSTYAfterScaler);
#endif
	dep_bytes = dml_max(PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor,
			MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor);

	if (prefetch_sw_bytes < dep_bytes)
		prefetch_sw_bytes = 2 * dep_bytes;

	*PrefetchBandwidth = 0;
	*DestinationLinesToRequestVMInVBlank = 0;
	*DestinationLinesToRequestRowInVBlank = 0;
	*VRatioPrefetchY = 0;
	*VRatioPrefetchC = 0;
	*RequiredPrefetchPixDataBWLuma = 0;
	if (dst_y_prefetch_equ > 1 &&
			(Tpre_rounded >= TPreReq || dst_y_prefetch_equ == __DML_VBA_MAX_DST_Y_PRE__)) {
		double PrefetchBandwidth1;
		double PrefetchBandwidth2;
		double PrefetchBandwidth3;
		double PrefetchBandwidth4;

		if (Tpre_rounded - *Tno_bw > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte
					+ 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
					+ prefetch_sw_bytes) / (Tpre_rounded - *Tno_bw);
			Tsw_est1 = prefetch_sw_bytes / PrefetchBandwidth1;
		} else
			PrefetchBandwidth1 = 0;

		if (VStartup == MaxVStartup && (Tsw_est1 / LineTime < min_Lsw)
				&& Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - *Tno_bw > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte
					+ 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor)
					/ (Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - *Tno_bw);
		}

		if (Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded > 0)
			PrefetchBandwidth2 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + prefetch_sw_bytes) /
			(Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded);
		else
			PrefetchBandwidth2 = 0;

		if (Tpre_rounded - Tvm_trips_rounded > 0) {
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
					+ prefetch_sw_bytes) / (Tpre_rounded - Tvm_trips_rounded);
			Tsw_est3 = prefetch_sw_bytes / PrefetchBandwidth3;
		} else
			PrefetchBandwidth3 = 0;


		if (VStartup == MaxVStartup &&
				(Tsw_est3 / LineTime < min_Lsw) && Tpre_rounded - min_Lsw * LineTime - 0.75 *
				LineTime - Tvm_trips_rounded > 0) {
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor)
					/ (Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - Tvm_trips_rounded);
		}

		if (Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded > 0) {
			PrefetchBandwidth4 = prefetch_sw_bytes /
					(Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded);
		} else {
			PrefetchBandwidth4 = 0;
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Tpre_rounded: %f\n", __func__, Tpre_rounded);
		dml_print("DML::%s: Tno_bw: %f\n", __func__, *Tno_bw);
		dml_print("DML::%s: Tvm_trips_rounded: %f\n", __func__, Tvm_trips_rounded);
		dml_print("DML::%s: Tsw_est1: %f\n", __func__, Tsw_est1);
		dml_print("DML::%s: Tsw_est3: %f\n", __func__, Tsw_est3);
		dml_print("DML::%s: PrefetchBandwidth1: %f\n", __func__, PrefetchBandwidth1);
		dml_print("DML::%s: PrefetchBandwidth2: %f\n", __func__, PrefetchBandwidth2);
		dml_print("DML::%s: PrefetchBandwidth3: %f\n", __func__, PrefetchBandwidth3);
		dml_print("DML::%s: PrefetchBandwidth4: %f\n", __func__, PrefetchBandwidth4);
#endif
		{
			bool Case1OK;
			bool Case2OK;
			bool Case3OK;

			if (PrefetchBandwidth1 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth1
						>= Tvm_trips_rounded
						&& (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor)
								/ PrefetchBandwidth1 >= Tr0_trips_rounded) {
					Case1OK = true;
				} else {
					Case1OK = false;
				}
			} else {
				Case1OK = false;
			}

			if (PrefetchBandwidth2 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth2
						>= Tvm_trips_rounded
						&& (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor)
						/ PrefetchBandwidth2 < Tr0_trips_rounded) {
					Case2OK = true;
				} else {
					Case2OK = false;
				}
			} else {
				Case2OK = false;
			}

			if (PrefetchBandwidth3 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth3 <
						Tvm_trips_rounded && (MetaRowByte + PixelPTEBytesPerRow *
								HostVMInefficiencyFactor) / PrefetchBandwidth3 >=
								Tr0_trips_rounded) {
					Case3OK = true;
				} else {
					Case3OK = false;
				}
			} else {
				Case3OK = false;
			}

			if (Case1OK)
				prefetch_bw_equ = PrefetchBandwidth1;
			else if (Case2OK)
				prefetch_bw_equ = PrefetchBandwidth2;
			else if (Case3OK)
				prefetch_bw_equ = PrefetchBandwidth3;
			else
				prefetch_bw_equ = PrefetchBandwidth4;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: Case1OK: %d\n", __func__, Case1OK);
			dml_print("DML::%s: Case2OK: %d\n", __func__, Case2OK);
			dml_print("DML::%s: Case3OK: %d\n", __func__, Case3OK);
			dml_print("DML::%s: prefetch_bw_equ: %f\n", __func__, prefetch_bw_equ);
#endif

			if (prefetch_bw_equ > 0) {
				if (v->GPUVMEnable == true) {
					Tvm_equ = dml_max3(*Tno_bw + PDEAndMetaPTEBytesFrame *
							HostVMInefficiencyFactor / prefetch_bw_equ,
							Tvm_trips, LineTime / 4);
				} else {
					Tvm_equ = LineTime / 4;
				}

				if ((v->GPUVMEnable == true || myPipe->DCCEnable == true)) {
					Tr0_equ = dml_max4((MetaRowByte + PixelPTEBytesPerRow *
							HostVMInefficiencyFactor) / prefetch_bw_equ, Tr0_trips,
							(LineTime - Tvm_equ) / 2, LineTime / 4);
				} else {
					Tr0_equ = (LineTime - Tvm_equ) / 2;
				}
			} else {
				Tvm_equ = 0;
				Tr0_equ = 0;
#ifdef __DML_VBA_DEBUG__
				dml_print("DML: prefetch_bw_equ equals 0! %s:%d\n", __FILE__, __LINE__);
#endif
			}
		}

		if (dst_y_prefetch_oto < dst_y_prefetch_equ) {
			if (dst_y_prefetch_oto * LineTime < TPreReq) {
				*DestinationLinesForPrefetch = dst_y_prefetch_equ;
			} else {
				*DestinationLinesForPrefetch = dst_y_prefetch_oto;
			}
			TimeForFetchingMetaPTE = Tvm_oto;
			TimeForFetchingRowInVBlank = Tr0_oto;
			*PrefetchBandwidth = prefetch_bw_oto;
			/* Clamp to oto for bandwidth calculation */
			LinesForPrefetchBandwidth = dst_y_prefetch_oto;
		} else {
			/* For mode programming we want to extend the prefetch as much as possible
			 * (up to oto, or as long as we can for equ) if we're not already applying
			 * the 60us prefetch requirement. This is to avoid intermittent underflow
			 * issues during prefetch.
			 *
			 * The prefetch extension is applied under the following scenarios:
			 * 1. We're in prefetch mode > 0 (i.e. we don't support MCLK switch in blank)
			 * 2. We're using subvp or drr methods of p-state switch, in which case we
			 *    we don't care if prefetch takes up more of the blanking time
			 *
			 * Mode programming typically chooses the smallest prefetch time possible
			 * (i.e. highest bandwidth during prefetch) presumably to create margin between
			 * p-states / c-states that happen in vblank and prefetch. Therefore we only
			 * apply this prefetch extension when p-state in vblank is not required (UCLK
			 * p-states take up the most vblank time).
			 */
			if (ExtendPrefetchIfPossible && TPreReq == 0 && VStartup < MaxVStartup) {
				MyError = true;
			} else {
				*DestinationLinesForPrefetch = dst_y_prefetch_equ;
				TimeForFetchingMetaPTE = Tvm_equ;
				TimeForFetchingRowInVBlank = Tr0_equ;
				*PrefetchBandwidth = prefetch_bw_equ;
				/* Clamp to equ for bandwidth calculation */
				LinesForPrefetchBandwidth = dst_y_prefetch_equ;
			}
		}

		*DestinationLinesToRequestVMInVBlank = dml_ceil(4.0 * TimeForFetchingMetaPTE / LineTime, 1.0) / 4.0;

		*DestinationLinesToRequestRowInVBlank =
				dml_ceil(4.0 * TimeForFetchingRowInVBlank / LineTime, 1.0) / 4.0;

		LinesToRequestPrefetchPixelData = LinesForPrefetchBandwidth -
				*DestinationLinesToRequestVMInVBlank - 2 * *DestinationLinesToRequestRowInVBlank;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: DestinationLinesForPrefetch = %f\n", __func__, *DestinationLinesForPrefetch);
		dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n",
				__func__, *DestinationLinesToRequestVMInVBlank);
		dml_print("DML::%s: TimeForFetchingRowInVBlank = %f\n", __func__, TimeForFetchingRowInVBlank);
		dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
		dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n",
				__func__, *DestinationLinesToRequestRowInVBlank);
		dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, PrefetchSourceLinesY);
		dml_print("DML::%s: LinesToRequestPrefetchPixelData = %f\n", __func__, LinesToRequestPrefetchPixelData);
#endif

		if (LinesToRequestPrefetchPixelData >= 1 && prefetch_bw_equ > 0) {
			*VRatioPrefetchY = (double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData;
			*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *VRatioPrefetchY);
			dml_print("DML::%s: SwathHeightY = %d\n", __func__, SwathHeightY);
			dml_print("DML::%s: VInitPreFillY = %d\n", __func__, VInitPreFillY);
#endif
			if ((SwathHeightY > 4) && (VInitPreFillY > 3)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillY - 3.0) / 2.0) {
					*VRatioPrefetchY =
							dml_max((double) PrefetchSourceLinesY /
									LinesToRequestPrefetchPixelData,
									(double) MaxNumSwathY * SwathHeightY /
									(LinesToRequestPrefetchPixelData -
									(VInitPreFillY - 3.0) / 2.0));
					*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
				} else {
					MyError = true;
					*VRatioPrefetchY = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *VRatioPrefetchY);
				dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, PrefetchSourceLinesY);
				dml_print("DML::%s: MaxNumSwathY = %d\n", __func__, MaxNumSwathY);
#endif
			}

			*VRatioPrefetchC = (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData;
			*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *VRatioPrefetchC);
			dml_print("DML::%s: SwathHeightC = %d\n", __func__, SwathHeightC);
			dml_print("DML::%s: VInitPreFillC = %d\n", __func__, VInitPreFillC);
#endif
			if ((SwathHeightC > 4)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillC - 3.0) / 2.0) {
					*VRatioPrefetchC =
						dml_max(*VRatioPrefetchC,
							(double) MaxNumSwathC * SwathHeightC /
							(LinesToRequestPrefetchPixelData -
							(VInitPreFillC - 3.0) / 2.0));
					*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);
				} else {
					MyError = true;
					*VRatioPrefetchC = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *VRatioPrefetchC);
				dml_print("DML::%s: PrefetchSourceLinesC = %f\n", __func__, PrefetchSourceLinesC);
				dml_print("DML::%s: MaxNumSwathC = %d\n", __func__, MaxNumSwathC);
#endif
			}

			*RequiredPrefetchPixDataBWLuma = (double) PrefetchSourceLinesY
					/ LinesToRequestPrefetchPixelData * myPipe->BytePerPixelY * swath_width_luma_ub
					/ LineTime;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: BytePerPixelY = %d\n", __func__, myPipe->BytePerPixelY);
			dml_print("DML::%s: swath_width_luma_ub = %d\n", __func__, swath_width_luma_ub);
			dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
			dml_print("DML::%s: RequiredPrefetchPixDataBWLuma = %f\n",
					__func__, *RequiredPrefetchPixDataBWLuma);
#endif
			*RequiredPrefetchPixDataBWChroma = (double) PrefetchSourceLinesC /
					LinesToRequestPrefetchPixelData
					* myPipe->BytePerPixelC
					* swath_width_chroma_ub / LineTime;
		} else {
			MyError = true;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML:%s: MyErr set. LinesToRequestPrefetchPixelData: %f, should be > 0\n",
					__func__, LinesToRequestPrefetchPixelData);
#endif
			*VRatioPrefetchY = 0;
			*VRatioPrefetchC = 0;
			*RequiredPrefetchPixDataBWLuma = 0;
			*RequiredPrefetchPixDataBWChroma = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML: Tpre: %fus - sum of time to request meta pte, 2 x data pte + meta data, swaths\n",
			(double)LinesToRequestPrefetchPixelData * LineTime +
			2.0*TimeForFetchingRowInVBlank + TimeForFetchingMetaPTE);
		dml_print("DML:  Tvm: %fus - time to fetch page tables for meta surface\n", TimeForFetchingMetaPTE);
		dml_print("DML: To: %fus - time for propagation from scaler to optc\n",
			(*DSTYAfterScaler + ((double) (*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime);
		dml_print("DML: Tvstartup - TSetup - Tcalc - Twait - Tpre - To > 0\n");
		dml_print("DML: Tslack(pre): %fus - time left over in schedule\n", VStartup * LineTime -
			TimeForFetchingMetaPTE - 2*TimeForFetchingRowInVBlank - (*DSTYAfterScaler +
			((double) (*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime - TWait - TCalc - *TSetup);
		dml_print("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %d\n",
				PixelPTEBytesPerRow);
#endif
	} else {
		MyError = true;
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: MyErr set, dst_y_prefetch_equ = %f (should be > 1)\n",
				__func__, dst_y_prefetch_equ);
#endif
	}

	{
		double prefetch_vm_bw;
		double prefetch_row_bw;

		if (PDEAndMetaPTEBytesFrame == 0) {
			prefetch_vm_bw = 0;
		} else if (*DestinationLinesToRequestVMInVBlank > 0) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
			dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
			dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n",
					__func__, *DestinationLinesToRequestVMInVBlank);
			dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
#endif
			prefetch_vm_bw = PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor /
					(*DestinationLinesToRequestVMInVBlank * LineTime);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: prefetch_vm_bw = %f\n", __func__, prefetch_vm_bw);
#endif
		} else {
			prefetch_vm_bw = 0;
			MyError = true;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: MyErr set. DestinationLinesToRequestVMInVBlank=%f (should be > 0)\n",
					__func__, *DestinationLinesToRequestVMInVBlank);
#endif
		}

		if (MetaRowByte + PixelPTEBytesPerRow == 0) {
			prefetch_row_bw = 0;
		} else if (*DestinationLinesToRequestRowInVBlank > 0) {
			prefetch_row_bw = (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) /
					(*DestinationLinesToRequestRowInVBlank * LineTime);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: MetaRowByte = %d\n", __func__, MetaRowByte);
			dml_print("DML::%s: PixelPTEBytesPerRow = %d\n", __func__, PixelPTEBytesPerRow);
			dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n",
					__func__, *DestinationLinesToRequestRowInVBlank);
			dml_print("DML::%s: prefetch_row_bw = %f\n", __func__, prefetch_row_bw);
#endif
		} else {
			prefetch_row_bw = 0;
			MyError = true;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: MyErr set. DestinationLinesToRequestRowInVBlank=%f (should be > 0)\n",
					__func__, *DestinationLinesToRequestRowInVBlank);
#endif
		}

		*prefetch_vmrow_bw = dml_max(prefetch_vm_bw, prefetch_row_bw);
	}

	if (MyError) {
		*PrefetchBandwidth = 0;
		*DestinationLinesToRequestVMInVBlank = 0;
		*DestinationLinesToRequestRowInVBlank = 0;
		*DestinationLinesForPrefetch = 0;
		*VRatioPrefetchY = 0;
		*VRatioPrefetchC = 0;
		*RequiredPrefetchPixDataBWLuma = 0;
		*RequiredPrefetchPixDataBWChroma = 0;
	}

	return MyError;
} // CalculatePrefetchSchedule

void dml32_CalculateFlipSchedule(
		double HostVMInefficiencyFactor,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int GPUVMMaxPageTableLevels,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		bool GPUVMEnable,
		double HostVMMinPageSize,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
		double VRatioChroma,
		double Tno_bw,
		bool DCCEnable,
		unsigned int dpte_row_height,
		unsigned int meta_row_height,
		unsigned int dpte_row_height_chroma,
		unsigned int meta_row_height_chroma,
		bool    use_one_row_for_frame_flip,

		/* Output */
		double *DestinationLinesToRequestVMInImmediateFlip,
		double *DestinationLinesToRequestRowInImmediateFlip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe)
{
	double min_row_time = 0.0;
	unsigned int HostVMDynamicLevelsTrips;
	double TimeForFetchingMetaPTEImmediateFlip;
	double TimeForFetchingRowInVBlankImmediateFlip;
	double ImmediateFlipBW = 1.0;

	if (GPUVMEnable == true && HostVMEnable == true)
		HostVMDynamicLevelsTrips = HostVMMaxNonCachedPageTableLevels;
	else
		HostVMDynamicLevelsTrips = 0;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: TotImmediateFlipBytes = %d\n", __func__, TotImmediateFlipBytes);
	dml_print("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
#endif

	if (TotImmediateFlipBytes > 0) {
		if (use_one_row_for_frame_flip) {
			ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + 2 * DPTEBytesPerRow) *
					BandwidthAvailableForImmediateFlip / TotImmediateFlipBytes;
		} else {
			ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + DPTEBytesPerRow) *
					BandwidthAvailableForImmediateFlip / TotImmediateFlipBytes;
		}
		if (GPUVMEnable == true) {
			TimeForFetchingMetaPTEImmediateFlip = dml_max3(Tno_bw + PDEAndMetaPTEBytesPerFrame *
					HostVMInefficiencyFactor / ImmediateFlipBW,
					UrgentExtraLatency + UrgentLatency *
					(GPUVMMaxPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1),
					LineTime / 4.0);
		} else {
			TimeForFetchingMetaPTEImmediateFlip = 0;
		}
		if ((GPUVMEnable == true || DCCEnable == true)) {
			TimeForFetchingRowInVBlankImmediateFlip = dml_max3(
					(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / ImmediateFlipBW,
					UrgentLatency * (HostVMDynamicLevelsTrips + 1), LineTime / 4.0);
		} else {
			TimeForFetchingRowInVBlankImmediateFlip = 0;
		}

		*DestinationLinesToRequestVMInImmediateFlip =
				dml_ceil(4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime), 1.0) / 4.0;
		*DestinationLinesToRequestRowInImmediateFlip =
				dml_ceil(4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime), 1.0) / 4.0;

		if (GPUVMEnable == true) {
			*final_flip_bw = dml_max(PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor /
					(*DestinationLinesToRequestVMInImmediateFlip * LineTime),
					(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) /
					(*DestinationLinesToRequestRowInImmediateFlip * LineTime));
		} else if ((GPUVMEnable == true || DCCEnable == true)) {
			*final_flip_bw = (MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) /
					(*DestinationLinesToRequestRowInImmediateFlip * LineTime);
		} else {
			*final_flip_bw = 0;
		}
	} else {
		TimeForFetchingMetaPTEImmediateFlip = 0;
		TimeForFetchingRowInVBlankImmediateFlip = 0;
		*DestinationLinesToRequestVMInImmediateFlip = 0;
		*DestinationLinesToRequestRowInImmediateFlip = 0;
		*final_flip_bw = 0;
	}

	if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_rgbe_alpha) {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dml_min(dpte_row_height *
					LineTime / VRatio, dpte_row_height_chroma * LineTime / VRatioChroma);
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = dml_min(meta_row_height *
					LineTime / VRatio, meta_row_height_chroma * LineTime / VRatioChroma);
		} else {
			min_row_time = dml_min4(dpte_row_height * LineTime / VRatio, meta_row_height *
					LineTime / VRatio, dpte_row_height_chroma * LineTime /
					VRatioChroma, meta_row_height_chroma * LineTime / VRatioChroma);
		}
	} else {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dpte_row_height * LineTime / VRatio;
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = meta_row_height * LineTime / VRatio;
		} else {
			min_row_time =
				dml_min(dpte_row_height * LineTime / VRatio, meta_row_height * LineTime / VRatio);
		}
	}

	if (*DestinationLinesToRequestVMInImmediateFlip >= 32 || *DestinationLinesToRequestRowInImmediateFlip >= 16
			|| TimeForFetchingMetaPTEImmediateFlip + 2 * TimeForFetchingRowInVBlankImmediateFlip
					> min_row_time) {
		*ImmediateFlipSupportedForPipe = false;
	} else {
		*ImmediateFlipSupportedForPipe = true;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMEnable = %d\n", __func__, GPUVMEnable);
	dml_print("DML::%s: DCCEnable = %d\n", __func__, DCCEnable);
	dml_print("DML::%s: DestinationLinesToRequestVMInImmediateFlip = %f\n",
			__func__, *DestinationLinesToRequestVMInImmediateFlip);
	dml_print("DML::%s: DestinationLinesToRequestRowInImmediateFlip = %f\n",
			__func__, *DestinationLinesToRequestRowInImmediateFlip);
	dml_print("DML::%s: TimeForFetchingMetaPTEImmediateFlip = %f\n", __func__, TimeForFetchingMetaPTEImmediateFlip);
	dml_print("DML::%s: TimeForFetchingRowInVBlankImmediateFlip = %f\n",
			__func__, TimeForFetchingRowInVBlankImmediateFlip);
	dml_print("DML::%s: min_row_time = %f\n", __func__, min_row_time);
	dml_print("DML::%s: ImmediateFlipSupportedForPipe = %d\n", __func__, *ImmediateFlipSupportedForPipe);
#endif
} // CalculateFlipSchedule

void dml32_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(
		struct vba_vars_st *v,
		unsigned int PrefetchMode,
		double DCFCLK,
		double ReturnBW,
		SOCParametersList mmSOCParameters,
		double SOCCLK,
		double DCFClkDeepSleep,
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerSurface[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		double DSTXAfterScaler[],
		double DSTYAfterScaler[],
		bool UnboundedRequestEnabled,
		unsigned int CompressedBufferSizeInkByte,

		/* Output */
		enum clock_change_support *DRAMClockChangeSupport,
		double MaxActiveDRAMClockChangeLatencySupported[],
		unsigned int SubViewportLinesNeededInMALL[],
		enum dm_fclock_change_support *FCLKChangeSupport,
		double *MinActiveFCLKChangeLatencySupported,
		bool *USRRetrainingSupport,
		double ActiveDRAMClockChangeLatencyMargin[])
{
	unsigned int i, j, k;
	unsigned int SurfaceWithMinActiveFCLKChangeMargin = 0;
	unsigned int DRAMClockChangeSupportNumber = 0;
	unsigned int LastSurfaceWithoutMargin = 0;
	unsigned int DRAMClockChangeMethod = 0;
	bool FoundFirstSurfaceWithMinActiveFCLKChangeMargin = false;
	double MinActiveFCLKChangeMargin = 0.;
	double SecondMinActiveFCLKChangeMarginOneDisplayInVBLank = 0.;
	double ActiveClockChangeLatencyHidingY;
	double ActiveClockChangeLatencyHidingC;
	double ActiveClockChangeLatencyHiding;
	double EffectiveDETBufferSizeY;
	double     ActiveFCLKChangeLatencyMargin[DC__NUM_DPP__MAX];
	double     USRRetrainingLatencyMargin[DC__NUM_DPP__MAX];
	double TotalPixelBW = 0.0;
	bool    SynchronizedSurfaces[DC__NUM_DPP__MAX][DC__NUM_DPP__MAX];
	double     EffectiveLBLatencyHidingY;
	double     EffectiveLBLatencyHidingC;
	double     LinesInDETY[DC__NUM_DPP__MAX];
	double     LinesInDETC[DC__NUM_DPP__MAX];
	unsigned int    LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX];
	unsigned int    LinesInDETCRoundedDownToSwath[DC__NUM_DPP__MAX];
	double     FullDETBufferingTimeY;
	double     FullDETBufferingTimeC;
	double     WritebackDRAMClockChangeLatencyMargin;
	double     WritebackFCLKChangeLatencyMargin;
	double     WritebackLatencyHiding;
	bool    SameTimingForFCLKChange;

	unsigned int    TotalActiveWriteback = 0;
	unsigned int LBLatencyHidingSourceLinesY[DC__NUM_DPP__MAX];
	unsigned int LBLatencyHidingSourceLinesC[DC__NUM_DPP__MAX];

	v->Watermark.UrgentWatermark = mmSOCParameters.UrgentLatency + mmSOCParameters.ExtraLatency;
	v->Watermark.USRRetrainingWatermark = mmSOCParameters.UrgentLatency + mmSOCParameters.ExtraLatency
			+ mmSOCParameters.USRRetrainingLatency + mmSOCParameters.SMNLatency;
	v->Watermark.DRAMClockChangeWatermark = mmSOCParameters.DRAMClockChangeLatency + v->Watermark.UrgentWatermark;
	v->Watermark.FCLKChangeWatermark = mmSOCParameters.FCLKChangeLatency + v->Watermark.UrgentWatermark;
	v->Watermark.StutterExitWatermark = mmSOCParameters.SRExitTime + mmSOCParameters.ExtraLatency
			+ 10 / DCFClkDeepSleep;
	v->Watermark.StutterEnterPlusExitWatermark = mmSOCParameters.SREnterPlusExitTime + mmSOCParameters.ExtraLatency
			+ 10 / DCFClkDeepSleep;
	v->Watermark.Z8StutterExitWatermark = mmSOCParameters.SRExitZ8Time + mmSOCParameters.ExtraLatency
			+ 10 / DCFClkDeepSleep;
	v->Watermark.Z8StutterEnterPlusExitWatermark = mmSOCParameters.SREnterPlusExitZ8Time
			+ mmSOCParameters.ExtraLatency + 10 / DCFClkDeepSleep;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: UrgentLatency = %f\n", __func__, mmSOCParameters.UrgentLatency);
	dml_print("DML::%s: ExtraLatency = %f\n", __func__, mmSOCParameters.ExtraLatency);
	dml_print("DML::%s: DRAMClockChangeLatency = %f\n", __func__, mmSOCParameters.DRAMClockChangeLatency);
	dml_print("DML::%s: UrgentWatermark = %f\n", __func__, v->Watermark.UrgentWatermark);
	dml_print("DML::%s: USRRetrainingWatermark = %f\n", __func__, v->Watermark.USRRetrainingWatermark);
	dml_print("DML::%s: DRAMClockChangeWatermark = %f\n", __func__, v->Watermark.DRAMClockChangeWatermark);
	dml_print("DML::%s: FCLKChangeWatermark = %f\n", __func__, v->Watermark.FCLKChangeWatermark);
	dml_print("DML::%s: StutterExitWatermark = %f\n", __func__, v->Watermark.StutterExitWatermark);
	dml_print("DML::%s: StutterEnterPlusExitWatermark = %f\n", __func__, v->Watermark.StutterEnterPlusExitWatermark);
	dml_print("DML::%s: Z8StutterExitWatermark = %f\n", __func__, v->Watermark.Z8StutterExitWatermark);
	dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n",
			__func__, v->Watermark.Z8StutterEnterPlusExitWatermark);
#endif


	TotalActiveWriteback = 0;
	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if (v->WritebackEnable[k] == true)
			TotalActiveWriteback = TotalActiveWriteback + 1;
	}

	if (TotalActiveWriteback <= 1) {
		v->Watermark.WritebackUrgentWatermark = mmSOCParameters.WritebackLatency;
	} else {
		v->Watermark.WritebackUrgentWatermark = mmSOCParameters.WritebackLatency
				+ v->WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}
	if (v->USRRetrainingRequiredFinal)
		v->Watermark.WritebackDRAMClockChangeWatermark = v->Watermark.WritebackDRAMClockChangeWatermark
				+ mmSOCParameters.USRRetrainingLatency;

	if (TotalActiveWriteback <= 1) {
		v->Watermark.WritebackDRAMClockChangeWatermark = mmSOCParameters.DRAMClockChangeLatency
				+ mmSOCParameters.WritebackLatency;
		v->Watermark.WritebackFCLKChangeWatermark = mmSOCParameters.FCLKChangeLatency
				+ mmSOCParameters.WritebackLatency;
	} else {
		v->Watermark.WritebackDRAMClockChangeWatermark = mmSOCParameters.DRAMClockChangeLatency
				+ mmSOCParameters.WritebackLatency + v->WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
		v->Watermark.WritebackFCLKChangeWatermark = mmSOCParameters.FCLKChangeLatency
				+ mmSOCParameters.WritebackLatency + v->WritebackChunkSize * 1024 / 32 / SOCCLK;
	}

	if (v->USRRetrainingRequiredFinal)
		v->Watermark.WritebackDRAMClockChangeWatermark = v->Watermark.WritebackDRAMClockChangeWatermark
				+ mmSOCParameters.USRRetrainingLatency;

	if (v->USRRetrainingRequiredFinal)
		v->Watermark.WritebackFCLKChangeWatermark = v->Watermark.WritebackFCLKChangeWatermark
				+ mmSOCParameters.USRRetrainingLatency;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: WritebackDRAMClockChangeWatermark = %f\n",
			__func__, v->Watermark.WritebackDRAMClockChangeWatermark);
	dml_print("DML::%s: WritebackFCLKChangeWatermark = %f\n", __func__, v->Watermark.WritebackFCLKChangeWatermark);
	dml_print("DML::%s: WritebackUrgentWatermark = %f\n", __func__, v->Watermark.WritebackUrgentWatermark);
	dml_print("DML::%s: v->USRRetrainingRequiredFinal = %d\n", __func__, v->USRRetrainingRequiredFinal);
	dml_print("DML::%s: USRRetrainingLatency = %f\n", __func__, mmSOCParameters.USRRetrainingLatency);
#endif

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		TotalPixelBW = TotalPixelBW + DPPPerSurface[k] * (SwathWidthY[k] * BytePerPixelDETY[k] * v->VRatio[k] +
				SwathWidthC[k] * BytePerPixelDETC[k] * v->VRatioChroma[k]) / (v->HTotal[k] / v->PixelClock[k]);
	}

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {

		LBLatencyHidingSourceLinesY[k] = dml_min((double) v->MaxLineBufferLines, dml_floor(v->LineBufferSizeFinal / v->LBBitPerPixel[k] / (SwathWidthY[k] / dml_max(v->HRatio[k], 1.0)), 1)) - (v->vtaps[k] - 1);
		LBLatencyHidingSourceLinesC[k] = dml_min((double) v->MaxLineBufferLines, dml_floor(v->LineBufferSizeFinal / v->LBBitPerPixel[k] / (SwathWidthC[k] / dml_max(v->HRatioChroma[k], 1.0)), 1)) - (v->VTAPsChroma[k] - 1);


#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, v->MaxLineBufferLines = %d\n", __func__, k, v->MaxLineBufferLines);
		dml_print("DML::%s: k=%d, v->LineBufferSizeFinal     = %d\n", __func__, k, v->LineBufferSizeFinal);
		dml_print("DML::%s: k=%d, v->LBBitPerPixel      = %d\n", __func__, k, v->LBBitPerPixel[k]);
		dml_print("DML::%s: k=%d, v->HRatio             = %f\n", __func__, k, v->HRatio[k]);
		dml_print("DML::%s: k=%d, v->vtaps              = %d\n", __func__, k, v->vtaps[k]);
#endif

		EffectiveLBLatencyHidingY = LBLatencyHidingSourceLinesY[k] / v->VRatio[k] * (v->HTotal[k] / v->PixelClock[k]);
		EffectiveLBLatencyHidingC = LBLatencyHidingSourceLinesC[k] / v->VRatioChroma[k] * (v->HTotal[k] / v->PixelClock[k]);
		EffectiveDETBufferSizeY = DETBufferSizeY[k];

		if (UnboundedRequestEnabled) {
			EffectiveDETBufferSizeY = EffectiveDETBufferSizeY
					+ CompressedBufferSizeInkByte * 1024
							* (SwathWidthY[k] * BytePerPixelDETY[k] * v->VRatio[k])
							/ (v->HTotal[k] / v->PixelClock[k]) / TotalPixelBW;
		}

		LinesInDETY[k] = (double) EffectiveDETBufferSizeY / BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath[k] = dml_floor(LinesInDETY[k], SwathHeightY[k]);
		FullDETBufferingTimeY = LinesInDETYRoundedDownToSwath[k] * (v->HTotal[k] / v->PixelClock[k]) / v->VRatio[k];

		ActiveClockChangeLatencyHidingY = EffectiveLBLatencyHidingY + FullDETBufferingTimeY
				- (DSTXAfterScaler[k] / v->HTotal[k] + DSTYAfterScaler[k]) * v->HTotal[k] / v->PixelClock[k];

		if (v->NumberOfActiveSurfaces > 1) {
			ActiveClockChangeLatencyHidingY = ActiveClockChangeLatencyHidingY
					- (1.0 - 1.0 / v->NumberOfActiveSurfaces) * SwathHeightY[k] * v->HTotal[k]
							/ v->PixelClock[k] / v->VRatio[k];
		}

		if (BytePerPixelDETC[k] > 0) {
			LinesInDETC[k] = DETBufferSizeC[k] / BytePerPixelDETC[k] / SwathWidthC[k];
			LinesInDETCRoundedDownToSwath[k] = dml_floor(LinesInDETC[k], SwathHeightC[k]);
			FullDETBufferingTimeC = LinesInDETCRoundedDownToSwath[k] * (v->HTotal[k] / v->PixelClock[k])
					/ v->VRatioChroma[k];
			ActiveClockChangeLatencyHidingC = EffectiveLBLatencyHidingC + FullDETBufferingTimeC
					- (DSTXAfterScaler[k] / v->HTotal[k] + DSTYAfterScaler[k]) * v->HTotal[k]
							/ v->PixelClock[k];
			if (v->NumberOfActiveSurfaces > 1) {
				ActiveClockChangeLatencyHidingC = ActiveClockChangeLatencyHidingC
						- (1 - 1 / v->NumberOfActiveSurfaces) * SwathHeightC[k] * v->HTotal[k]
								/ v->PixelClock[k] / v->VRatioChroma[k];
			}
			ActiveClockChangeLatencyHiding = dml_min(ActiveClockChangeLatencyHidingY,
					ActiveClockChangeLatencyHidingC);
		} else {
			ActiveClockChangeLatencyHiding = ActiveClockChangeLatencyHidingY;
		}

		ActiveDRAMClockChangeLatencyMargin[k] = ActiveClockChangeLatencyHiding - v->Watermark.UrgentWatermark
				- v->Watermark.DRAMClockChangeWatermark;
		ActiveFCLKChangeLatencyMargin[k] = ActiveClockChangeLatencyHiding - v->Watermark.UrgentWatermark
				- v->Watermark.FCLKChangeWatermark;
		USRRetrainingLatencyMargin[k] = ActiveClockChangeLatencyHiding - v->Watermark.USRRetrainingWatermark;

		if (v->WritebackEnable[k]) {
			WritebackLatencyHiding = v->WritebackInterfaceBufferSize * 1024
					/ (v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k]
							/ (v->WritebackSourceHeight[k] * v->HTotal[k] / v->PixelClock[k]) * 4);
			if (v->WritebackPixelFormat[k] == dm_444_64)
				WritebackLatencyHiding = WritebackLatencyHiding / 2;

			WritebackDRAMClockChangeLatencyMargin = WritebackLatencyHiding
					- v->Watermark.WritebackDRAMClockChangeWatermark;

			WritebackFCLKChangeLatencyMargin = WritebackLatencyHiding
					- v->Watermark.WritebackFCLKChangeWatermark;

			ActiveDRAMClockChangeLatencyMargin[k] = dml_min(ActiveDRAMClockChangeLatencyMargin[k],
					WritebackFCLKChangeLatencyMargin);
			ActiveFCLKChangeLatencyMargin[k] = dml_min(ActiveFCLKChangeLatencyMargin[k],
					WritebackDRAMClockChangeLatencyMargin);
		}
		MaxActiveDRAMClockChangeLatencySupported[k] =
				(v->UsesMALLForPStateChange[k] == dm_use_mall_pstate_change_phantom_pipe) ?
						0 :
						(ActiveDRAMClockChangeLatencyMargin[k]
								+ mmSOCParameters.DRAMClockChangeLatency);
	}

	for (i = 0; i < v->NumberOfActiveSurfaces; ++i) {
		for (j = 0; j < v->NumberOfActiveSurfaces; ++j) {
			if (i == j ||
					(v->BlendingAndTiming[i] == i && v->BlendingAndTiming[j] == i) ||
					(v->BlendingAndTiming[j] == j && v->BlendingAndTiming[i] == j) ||
					(v->BlendingAndTiming[i] == v->BlendingAndTiming[j] && v->BlendingAndTiming[i] != i) ||
					(v->SynchronizeTimingsFinal && v->PixelClock[i] == v->PixelClock[j] &&
					v->HTotal[i] == v->HTotal[j] && v->VTotal[i] == v->VTotal[j] &&
					v->VActive[i] == v->VActive[j]) || (v->SynchronizeDRRDisplaysForUCLKPStateChangeFinal &&
					(v->DRRDisplay[i] || v->DRRDisplay[j]))) {
				SynchronizedSurfaces[i][j] = true;
			} else {
				SynchronizedSurfaces[i][j] = false;
			}
		}
	}

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if ((v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) &&
				(!FoundFirstSurfaceWithMinActiveFCLKChangeMargin ||
				ActiveFCLKChangeLatencyMargin[k] < MinActiveFCLKChangeMargin)) {
			FoundFirstSurfaceWithMinActiveFCLKChangeMargin = true;
			MinActiveFCLKChangeMargin = ActiveFCLKChangeLatencyMargin[k];
			SurfaceWithMinActiveFCLKChangeMargin = k;
		}
	}

	*MinActiveFCLKChangeLatencySupported = MinActiveFCLKChangeMargin + mmSOCParameters.FCLKChangeLatency;

	SameTimingForFCLKChange = true;
	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if (!SynchronizedSurfaces[k][SurfaceWithMinActiveFCLKChangeMargin]) {
			if ((v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) &&
					(SameTimingForFCLKChange ||
					ActiveFCLKChangeLatencyMargin[k] <
					SecondMinActiveFCLKChangeMarginOneDisplayInVBLank)) {
				SecondMinActiveFCLKChangeMarginOneDisplayInVBLank = ActiveFCLKChangeLatencyMargin[k];
			}
			SameTimingForFCLKChange = false;
		}
	}

	if (MinActiveFCLKChangeMargin > 0) {
		*FCLKChangeSupport = dm_fclock_change_vactive;
	} else if ((SameTimingForFCLKChange || SecondMinActiveFCLKChangeMarginOneDisplayInVBLank > 0) &&
			(PrefetchMode <= 1)) {
		*FCLKChangeSupport = dm_fclock_change_vblank;
	} else {
		*FCLKChangeSupport = dm_fclock_change_unsupported;
	}

	*USRRetrainingSupport = true;
	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if ((v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) &&
				(USRRetrainingLatencyMargin[k] < 0)) {
			*USRRetrainingSupport = false;
		}
	}

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if (v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_full_frame &&
				v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_sub_viewport &&
				v->UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe &&
				ActiveDRAMClockChangeLatencyMargin[k] < 0) {
			if (PrefetchMode > 0) {
				DRAMClockChangeSupportNumber = 2;
			} else if (DRAMClockChangeSupportNumber == 0) {
				DRAMClockChangeSupportNumber = 1;
				LastSurfaceWithoutMargin = k;
			} else if (DRAMClockChangeSupportNumber == 1 &&
					!SynchronizedSurfaces[LastSurfaceWithoutMargin][k]) {
				DRAMClockChangeSupportNumber = 2;
			}
		}
	}

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		if (v->UsesMALLForPStateChange[k] == dm_use_mall_pstate_change_full_frame)
			DRAMClockChangeMethod = 1;
		else if (v->UsesMALLForPStateChange[k] == dm_use_mall_pstate_change_sub_viewport)
			DRAMClockChangeMethod = 2;
	}

	if (DRAMClockChangeMethod == 0) {
		if (DRAMClockChangeSupportNumber == 0)
			*DRAMClockChangeSupport = dm_dram_clock_change_vactive;
		else if (DRAMClockChangeSupportNumber == 1)
			*DRAMClockChangeSupport = dm_dram_clock_change_vblank;
		else
			*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	} else if (DRAMClockChangeMethod == 1) {
		if (DRAMClockChangeSupportNumber == 0)
			*DRAMClockChangeSupport = dm_dram_clock_change_vactive_w_mall_full_frame;
		else if (DRAMClockChangeSupportNumber == 1)
			*DRAMClockChangeSupport = dm_dram_clock_change_vblank_w_mall_full_frame;
		else
			*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	} else {
		if (DRAMClockChangeSupportNumber == 0)
			*DRAMClockChangeSupport = dm_dram_clock_change_vactive_w_mall_sub_vp;
		else if (DRAMClockChangeSupportNumber == 1)
			*DRAMClockChangeSupport = dm_dram_clock_change_vblank_w_mall_sub_vp;
		else
			*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	}

	for (k = 0; k < v->NumberOfActiveSurfaces; ++k) {
		unsigned int dst_y_pstate;
		unsigned int src_y_pstate_l;
		unsigned int src_y_pstate_c;
		unsigned int src_y_ahead_l, src_y_ahead_c, sub_vp_lines_l, sub_vp_lines_c;

		dst_y_pstate = dml_ceil((mmSOCParameters.DRAMClockChangeLatency + mmSOCParameters.UrgentLatency) / (v->HTotal[k] / v->PixelClock[k]), 1);
		src_y_pstate_l = dml_ceil(dst_y_pstate * v->VRatio[k], SwathHeightY[k]);
		src_y_ahead_l = dml_floor(DETBufferSizeY[k] / BytePerPixelDETY[k] / SwathWidthY[k], SwathHeightY[k]) + LBLatencyHidingSourceLinesY[k];
		sub_vp_lines_l = src_y_pstate_l + src_y_ahead_l + v->meta_row_height[k];

#ifdef __DML_VBA_DEBUG__
dml_print("DML::%s: k=%d, DETBufferSizeY               = %d\n", __func__, k, DETBufferSizeY[k]);
dml_print("DML::%s: k=%d, BytePerPixelDETY             = %f\n", __func__, k, BytePerPixelDETY[k]);
dml_print("DML::%s: k=%d, SwathWidthY                  = %d\n", __func__, k, SwathWidthY[k]);
dml_print("DML::%s: k=%d, SwathHeightY                 = %d\n", __func__, k, SwathHeightY[k]);
dml_print("DML::%s: k=%d, LBLatencyHidingSourceLinesY  = %d\n", __func__, k, LBLatencyHidingSourceLinesY[k]);
dml_print("DML::%s: k=%d, dst_y_pstate      = %d\n", __func__, k, dst_y_pstate);
dml_print("DML::%s: k=%d, src_y_pstate_l    = %d\n", __func__, k, src_y_pstate_l);
dml_print("DML::%s: k=%d, src_y_ahead_l     = %d\n", __func__, k, src_y_ahead_l);
dml_print("DML::%s: k=%d, v->meta_row_height   = %d\n", __func__, k, v->meta_row_height[k]);
dml_print("DML::%s: k=%d, sub_vp_lines_l    = %d\n", __func__, k, sub_vp_lines_l);
#endif
		SubViewportLinesNeededInMALL[k] = sub_vp_lines_l;

		if (BytePerPixelDETC[k] > 0) {
			src_y_pstate_c = dml_ceil(dst_y_pstate * v->VRatioChroma[k], SwathHeightC[k]);
			src_y_ahead_c = dml_floor(DETBufferSizeC[k] / BytePerPixelDETC[k] / SwathWidthC[k], SwathHeightC[k]) + LBLatencyHidingSourceLinesC[k];
			sub_vp_lines_c = src_y_pstate_c + src_y_ahead_c + v->meta_row_height_chroma[k];
			SubViewportLinesNeededInMALL[k] = dml_max(sub_vp_lines_l, sub_vp_lines_c);

#ifdef __DML_VBA_DEBUG__
dml_print("DML::%s: k=%d, src_y_pstate_c            = %d\n", __func__, k, src_y_pstate_c);
dml_print("DML::%s: k=%d, src_y_ahead_c             = %d\n", __func__, k, src_y_ahead_c);
dml_print("DML::%s: k=%d, v->meta_row_height_chroma    = %d\n", __func__, k, v->meta_row_height_chroma[k]);
dml_print("DML::%s: k=%d, sub_vp_lines_c            = %d\n", __func__, k, sub_vp_lines_c);
#endif
		}
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DRAMClockChangeSupport = %d\n", __func__, *DRAMClockChangeSupport);
	dml_print("DML::%s: FCLKChangeSupport = %d\n", __func__, *FCLKChangeSupport);
	dml_print("DML::%s: MinActiveFCLKChangeLatencySupported = %f\n",
			__func__, *MinActiveFCLKChangeLatencySupported);
	dml_print("DML::%s: USRRetrainingSupport = %d\n", __func__, *USRRetrainingSupport);
#endif
} // CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport

double dml32_CalculateWriteBackDISPCLK(
		enum source_format_class WritebackPixelFormat,
		double PixelClock,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackHTaps,
		unsigned int WritebackVTaps,
		unsigned int   WritebackSourceWidth,
		unsigned int   WritebackDestinationWidth,
		unsigned int HTotal,
		unsigned int WritebackLineBufferSize,
		double DISPCLKDPPCLKVCOSpeed)
{
	double DISPCLK_H, DISPCLK_V, DISPCLK_HB;

	DISPCLK_H = PixelClock * dml_ceil(WritebackHTaps / 8.0, 1) / WritebackHRatio;
	DISPCLK_V = PixelClock * (WritebackVTaps * dml_ceil(WritebackDestinationWidth / 6.0, 1) + 8.0) / HTotal;
	DISPCLK_HB = PixelClock * WritebackVTaps * (WritebackDestinationWidth *
			WritebackVTaps - WritebackLineBufferSize / 57.0) / 6.0 / WritebackSourceWidth;
	return dml32_RoundToDFSGranularity(dml_max3(DISPCLK_H, DISPCLK_V, DISPCLK_HB), 1, DISPCLKDPPCLKVCOSpeed);
}

void dml32_CalculateMinAndMaxPrefetchMode(
		enum dm_prefetch_modes   AllowForPStateChangeOrStutterInVBlankFinal,
		unsigned int             *MinPrefetchMode,
		unsigned int             *MaxPrefetchMode)
{
	if (AllowForPStateChangeOrStutterInVBlankFinal == dm_prefetch_support_none) {
		*MinPrefetchMode = 3;
		*MaxPrefetchMode = 3;
	} else if (AllowForPStateChangeOrStutterInVBlankFinal == dm_prefetch_support_stutter) {
		*MinPrefetchMode = 2;
		*MaxPrefetchMode = 2;
	} else if (AllowForPStateChangeOrStutterInVBlankFinal == dm_prefetch_support_fclk_and_stutter) {
		*MinPrefetchMode = 1;
		*MaxPrefetchMode = 1;
	} else if (AllowForPStateChangeOrStutterInVBlankFinal == dm_prefetch_support_uclk_fclk_and_stutter) {
		*MinPrefetchMode = 0;
		*MaxPrefetchMode = 0;
	} else {
		*MinPrefetchMode = 0;
		*MaxPrefetchMode = 3;
	}
} // CalculateMinAndMaxPrefetchMode

void dml32_CalculatePixelDeliveryTimes(
		unsigned int             NumberOfActiveSurfaces,
		double              VRatio[],
		double              VRatioChroma[],
		double              VRatioPrefetchY[],
		double              VRatioPrefetchC[],
		unsigned int             swath_width_luma_ub[],
		unsigned int             swath_width_chroma_ub[],
		unsigned int             DPPPerSurface[],
		double              HRatio[],
		double              HRatioChroma[],
		double              PixelClock[],
		double              PSCL_THROUGHPUT[],
		double              PSCL_THROUGHPUT_CHROMA[],
		double              Dppclk[],
		unsigned int             BytePerPixelC[],
		enum dm_rotation_angle   SourceRotation[],
		unsigned int             NumberOfCursors[],
		unsigned int             CursorWidth[][DC__NUM_CURSOR__MAX],
		unsigned int             CursorBPP[][DC__NUM_CURSOR__MAX],
		unsigned int             BlockWidth256BytesY[],
		unsigned int             BlockHeight256BytesY[],
		unsigned int             BlockWidth256BytesC[],
		unsigned int             BlockHeight256BytesC[],

		/* Output */
		double              DisplayPipeLineDeliveryTimeLuma[],
		double              DisplayPipeLineDeliveryTimeChroma[],
		double              DisplayPipeLineDeliveryTimeLumaPrefetch[],
		double              DisplayPipeLineDeliveryTimeChromaPrefetch[],
		double              DisplayPipeRequestDeliveryTimeLuma[],
		double              DisplayPipeRequestDeliveryTimeChroma[],
		double              DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		double              DisplayPipeRequestDeliveryTimeChromaPrefetch[],
		double              CursorRequestDeliveryTime[],
		double              CursorRequestDeliveryTimePrefetch[])
{
	double   req_per_swath_ub;
	unsigned int k;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : HRatio = %f\n", __func__, k, HRatio[k]);
		dml_print("DML::%s: k=%d : VRatio = %f\n", __func__, k, VRatio[k]);
		dml_print("DML::%s: k=%d : HRatioChroma = %f\n", __func__, k, HRatioChroma[k]);
		dml_print("DML::%s: k=%d : VRatioChroma = %f\n", __func__, k, VRatioChroma[k]);
		dml_print("DML::%s: k=%d : swath_width_luma_ub = %d\n", __func__, k, swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%d : swath_width_chroma_ub = %d\n", __func__, k, swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%d : PSCL_THROUGHPUT = %f\n", __func__, k, PSCL_THROUGHPUT[k]);
		dml_print("DML::%s: k=%d : PSCL_THROUGHPUT_CHROMA = %f\n", __func__, k, PSCL_THROUGHPUT_CHROMA[k]);
		dml_print("DML::%s: k=%d : DPPPerSurface = %d\n", __func__, k, DPPPerSurface[k]);
		dml_print("DML::%s: k=%d : PixelClock = %f\n", __func__, k, PixelClock[k]);
		dml_print("DML::%s: k=%d : Dppclk = %f\n", __func__, k, Dppclk[k]);
#endif

		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] =
					swath_width_luma_ub[k] * DPPPerSurface[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] =
					swath_width_chroma_ub[k] * DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] =
					swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] =
					swath_width_luma_ub[k] * DPPPerSurface[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] =
					swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] *
						DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] =
						swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeLuma = %f\n",
				__func__, k, DisplayPipeLineDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeLumaPrefetch = %f\n",
				__func__, k, DisplayPipeLineDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeChroma = %f\n",
				__func__, k, DisplayPipeLineDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeChromaPrefetch = %f\n",
				__func__, k, DisplayPipeLineDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!IsVertical(SourceRotation[k]))
			req_per_swath_ub = swath_width_luma_ub[k] / BlockWidth256BytesY[k];
		else
			req_per_swath_ub = swath_width_luma_ub[k] / BlockHeight256BytesY[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : req_per_swath_ub = %f (Luma)\n", __func__, k, req_per_swath_ub);
#endif

		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k] / req_per_swath_ub;
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] =
				DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub;
		if (BytePerPixelC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (!IsVertical(SourceRotation[k]))
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockWidth256BytesC[k];
			else
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockHeight256BytesC[k];
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%d : req_per_swath_ub = %f (Chroma)\n", __func__, k, req_per_swath_ub);
#endif
			DisplayPipeRequestDeliveryTimeChroma[k] =
					DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] =
					DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeLuma = %f\n",
				__func__, k, DisplayPipeRequestDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeLumaPrefetch = %f\n",
				__func__, k, DisplayPipeRequestDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeChroma = %f\n",
				__func__, k, DisplayPipeRequestDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeChromaPrefetch = %f\n",
				__func__, k, DisplayPipeRequestDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		unsigned int cursor_req_per_width;

		cursor_req_per_width = dml_ceil((double) CursorWidth[k][0] * (double) CursorBPP[k][0] /
				256.0 / 8.0, 1.0);
		if (NumberOfCursors[k] > 0) {
			if (VRatio[k] <= 1) {
				CursorRequestDeliveryTime[k] = (double) CursorWidth[k][0] /
						HRatio[k] / PixelClock[k] / cursor_req_per_width;
			} else {
				CursorRequestDeliveryTime[k] = (double) CursorWidth[k][0] /
						PSCL_THROUGHPUT[k] / Dppclk[k] / cursor_req_per_width;
			}
			if (VRatioPrefetchY[k] <= 1) {
				CursorRequestDeliveryTimePrefetch[k] = (double) CursorWidth[k][0] /
						HRatio[k] / PixelClock[k] / cursor_req_per_width;
			} else {
				CursorRequestDeliveryTimePrefetch[k] = (double) CursorWidth[k][0] /
						PSCL_THROUGHPUT[k] / Dppclk[k] / cursor_req_per_width;
			}
		} else {
			CursorRequestDeliveryTime[k] = 0;
			CursorRequestDeliveryTimePrefetch[k] = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : NumberOfCursors = %d\n",
				__func__, k, NumberOfCursors[k]);
		dml_print("DML::%s: k=%d : CursorRequestDeliveryTime = %f\n",
				__func__, k, CursorRequestDeliveryTime[k]);
		dml_print("DML::%s: k=%d : CursorRequestDeliveryTimePrefetch = %f\n",
				__func__, k, CursorRequestDeliveryTimePrefetch[k]);
#endif
	}
} // CalculatePixelDeliveryTimes

void dml32_CalculateMetaAndPTETimes(
		bool use_one_row_for_frame[],
		unsigned int NumberOfActiveSurfaces,
		bool GPUVMEnable,
		unsigned int MetaChunkSize,
		unsigned int MinMetaChunkSizeBytes,
		unsigned int    HTotal[],
		double  VRatio[],
		double  VRatioChroma[],
		double  DestinationLinesToRequestRowInVBlank[],
		double  DestinationLinesToRequestRowInImmediateFlip[],
		bool DCCEnable[],
		double  PixelClock[],
		unsigned int BytePerPixelY[],
		unsigned int BytePerPixelC[],
		enum dm_rotation_angle SourceRotation[],
		unsigned int dpte_row_height[],
		unsigned int dpte_row_height_chroma[],
		unsigned int meta_row_width[],
		unsigned int meta_row_width_chroma[],
		unsigned int meta_row_height[],
		unsigned int meta_row_height_chroma[],
		unsigned int meta_req_width[],
		unsigned int meta_req_width_chroma[],
		unsigned int meta_req_height[],
		unsigned int meta_req_height_chroma[],
		unsigned int dpte_group_bytes[],
		unsigned int    PTERequestSizeY[],
		unsigned int    PTERequestSizeC[],
		unsigned int    PixelPTEReqWidthY[],
		unsigned int    PixelPTEReqHeightY[],
		unsigned int    PixelPTEReqWidthC[],
		unsigned int    PixelPTEReqHeightC[],
		unsigned int    dpte_row_width_luma_ub[],
		unsigned int    dpte_row_width_chroma_ub[],

		/* Output */
		double DST_Y_PER_PTE_ROW_NOM_L[],
		double DST_Y_PER_PTE_ROW_NOM_C[],
		double DST_Y_PER_META_ROW_NOM_L[],
		double DST_Y_PER_META_ROW_NOM_C[],
		double TimePerMetaChunkNominal[],
		double TimePerChromaMetaChunkNominal[],
		double TimePerMetaChunkVBlank[],
		double TimePerChromaMetaChunkVBlank[],
		double TimePerMetaChunkFlip[],
		double TimePerChromaMetaChunkFlip[],
		double time_per_pte_group_nom_luma[],
		double time_per_pte_group_vblank_luma[],
		double time_per_pte_group_flip_luma[],
		double time_per_pte_group_nom_chroma[],
		double time_per_pte_group_vblank_chroma[],
		double time_per_pte_group_flip_chroma[])
{
	unsigned int   meta_chunk_width;
	unsigned int   min_meta_chunk_width;
	unsigned int   meta_chunk_per_row_int;
	unsigned int   meta_row_remainder;
	unsigned int   meta_chunk_threshold;
	unsigned int   meta_chunks_per_row_ub;
	unsigned int   meta_chunk_width_chroma;
	unsigned int   min_meta_chunk_width_chroma;
	unsigned int   meta_chunk_per_row_int_chroma;
	unsigned int   meta_row_remainder_chroma;
	unsigned int   meta_chunk_threshold_chroma;
	unsigned int   meta_chunks_per_row_ub_chroma;
	unsigned int   dpte_group_width_luma;
	unsigned int   dpte_groups_per_row_luma_ub;
	unsigned int   dpte_group_width_chroma;
	unsigned int   dpte_groups_per_row_chroma_ub;
	unsigned int k;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		DST_Y_PER_PTE_ROW_NOM_L[k] = dpte_row_height[k] / VRatio[k];
		if (BytePerPixelC[k] == 0)
			DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		else
			DST_Y_PER_PTE_ROW_NOM_C[k] = dpte_row_height_chroma[k] / VRatioChroma[k];
		DST_Y_PER_META_ROW_NOM_L[k] = meta_row_height[k] / VRatio[k];
		if (BytePerPixelC[k] == 0)
			DST_Y_PER_META_ROW_NOM_C[k] = 0;
		else
			DST_Y_PER_META_ROW_NOM_C[k] = meta_row_height_chroma[k] / VRatioChroma[k];
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (DCCEnable[k] == true) {
			meta_chunk_width = MetaChunkSize * 1024 * 256 / BytePerPixelY[k] / meta_row_height[k];
			min_meta_chunk_width = MinMetaChunkSizeBytes * 256 / BytePerPixelY[k] / meta_row_height[k];
			meta_chunk_per_row_int = meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = meta_row_width[k] % meta_chunk_width;
			if (!IsVertical(SourceRotation[k]))
				meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_width[k];
			else
				meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_height[k];

			if (meta_row_remainder <= meta_chunk_threshold)
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
			else
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;

			TimePerMetaChunkNominal[k] = meta_row_height[k] / VRatio[k] *
					HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkVBlank[k] = DestinationLinesToRequestRowInVBlank[k] *
					HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkFlip[k] = DestinationLinesToRequestRowInImmediateFlip[k] *
					HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			if (BytePerPixelC[k] == 0) {
				TimePerChromaMetaChunkNominal[k] = 0;
				TimePerChromaMetaChunkVBlank[k] = 0;
				TimePerChromaMetaChunkFlip[k] = 0;
			} else {
				meta_chunk_width_chroma = MetaChunkSize * 1024 * 256 / BytePerPixelC[k] /
						meta_row_height_chroma[k];
				min_meta_chunk_width_chroma = MinMetaChunkSizeBytes * 256 / BytePerPixelC[k] /
						meta_row_height_chroma[k];
				meta_chunk_per_row_int_chroma = (double) meta_row_width_chroma[k] /
						meta_chunk_width_chroma;
				meta_row_remainder_chroma = meta_row_width_chroma[k] % meta_chunk_width_chroma;
				if (!IsVertical(SourceRotation[k])) {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma -
							meta_req_width_chroma[k];
				} else {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma -
							meta_req_height_chroma[k];
				}
				if (meta_row_remainder_chroma <= meta_chunk_threshold_chroma)
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 1;
				else
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 2;

				TimePerChromaMetaChunkNominal[k] = meta_row_height_chroma[k] / VRatioChroma[k] *
						HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
				TimePerChromaMetaChunkVBlank[k] = DestinationLinesToRequestRowInVBlank[k] *
						HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
				TimePerChromaMetaChunkFlip[k] = DestinationLinesToRequestRowInImmediateFlip[k] *
						HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
			}
		} else {
			TimePerMetaChunkNominal[k] = 0;
			TimePerMetaChunkVBlank[k] = 0;
			TimePerMetaChunkFlip[k] = 0;
			TimePerChromaMetaChunkNominal[k] = 0;
			TimePerChromaMetaChunkVBlank[k] = 0;
			TimePerChromaMetaChunkFlip[k] = 0;
		}
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (GPUVMEnable == true) {
			if (!IsVertical(SourceRotation[k])) {
				dpte_group_width_luma = (double) dpte_group_bytes[k] /
						(double) PTERequestSizeY[k] * PixelPTEReqWidthY[k];
			} else {
				dpte_group_width_luma = (double) dpte_group_bytes[k] /
						(double) PTERequestSizeY[k] * PixelPTEReqHeightY[k];
			}

			if (use_one_row_for_frame[k]) {
				dpte_groups_per_row_luma_ub = dml_ceil((double) dpte_row_width_luma_ub[k] /
						(double) dpte_group_width_luma / 2.0, 1.0);
			} else {
				dpte_groups_per_row_luma_ub = dml_ceil((double) dpte_row_width_luma_ub[k] /
						(double) dpte_group_width_luma, 1.0);
			}
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d, use_one_row_for_frame        = %d\n",
					__func__, k, use_one_row_for_frame[k]);
			dml_print("DML::%s: k=%0d, dpte_group_bytes             = %d\n",
					__func__, k, dpte_group_bytes[k]);
			dml_print("DML::%s: k=%0d, PTERequestSizeY              = %d\n",
					__func__, k, PTERequestSizeY[k]);
			dml_print("DML::%s: k=%0d, PixelPTEReqWidthY            = %d\n",
					__func__, k, PixelPTEReqWidthY[k]);
			dml_print("DML::%s: k=%0d, PixelPTEReqHeightY           = %d\n",
					__func__, k, PixelPTEReqHeightY[k]);
			dml_print("DML::%s: k=%0d, dpte_row_width_luma_ub       = %d\n",
					__func__, k, dpte_row_width_luma_ub[k]);
			dml_print("DML::%s: k=%0d, dpte_group_width_luma        = %d\n",
					__func__, k, dpte_group_width_luma);
			dml_print("DML::%s: k=%0d, dpte_groups_per_row_luma_ub  = %d\n",
					__func__, k, dpte_groups_per_row_luma_ub);
#endif

			time_per_pte_group_nom_luma[k] = DST_Y_PER_PTE_ROW_NOM_L[k] *
					HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_vblank_luma[k] = DestinationLinesToRequestRowInVBlank[k] *
					HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_flip_luma[k] = DestinationLinesToRequestRowInImmediateFlip[k] *
					HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			if (BytePerPixelC[k] == 0) {
				time_per_pte_group_nom_chroma[k] = 0;
				time_per_pte_group_vblank_chroma[k] = 0;
				time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (!IsVertical(SourceRotation[k])) {
					dpte_group_width_chroma = (double) dpte_group_bytes[k] /
							(double) PTERequestSizeC[k] * PixelPTEReqWidthC[k];
				} else {
					dpte_group_width_chroma = (double) dpte_group_bytes[k] /
							(double) PTERequestSizeC[k] * PixelPTEReqHeightC[k];
				}

				if (use_one_row_for_frame[k]) {
					dpte_groups_per_row_chroma_ub = dml_ceil((double) dpte_row_width_chroma_ub[k] /
							(double) dpte_group_width_chroma / 2.0, 1.0);
				} else {
					dpte_groups_per_row_chroma_ub = dml_ceil((double) dpte_row_width_chroma_ub[k] /
							(double) dpte_group_width_chroma, 1.0);
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%0d, dpte_row_width_chroma_ub        = %d\n",
						__func__, k, dpte_row_width_chroma_ub[k]);
				dml_print("DML::%s: k=%0d, dpte_group_width_chroma        = %d\n",
						__func__, k, dpte_group_width_chroma);
				dml_print("DML::%s: k=%0d, dpte_groups_per_row_chroma_ub  = %d\n",
						__func__, k, dpte_groups_per_row_chroma_ub);
#endif
				time_per_pte_group_nom_chroma[k] = DST_Y_PER_PTE_ROW_NOM_C[k] *
						HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
				time_per_pte_group_vblank_chroma[k] = DestinationLinesToRequestRowInVBlank[k] *
						HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
				time_per_pte_group_flip_chroma[k] = DestinationLinesToRequestRowInImmediateFlip[k] *
						HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
			}
		} else {
			time_per_pte_group_nom_luma[k] = 0;
			time_per_pte_group_vblank_luma[k] = 0;
			time_per_pte_group_flip_luma[k] = 0;
			time_per_pte_group_nom_chroma[k] = 0;
			time_per_pte_group_vblank_chroma[k] = 0;
			time_per_pte_group_flip_chroma[k] = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d, DestinationLinesToRequestRowInVBlank         = %f\n",
				__func__, k, DestinationLinesToRequestRowInVBlank[k]);
		dml_print("DML::%s: k=%0d, DestinationLinesToRequestRowInImmediateFlip  = %f\n",
				__func__, k, DestinationLinesToRequestRowInImmediateFlip[k]);
		dml_print("DML::%s: k=%0d, DST_Y_PER_PTE_ROW_NOM_L                      = %f\n",
				__func__, k, DST_Y_PER_PTE_ROW_NOM_L[k]);
		dml_print("DML::%s: k=%0d, DST_Y_PER_PTE_ROW_NOM_C                      = %f\n",
				__func__, k, DST_Y_PER_PTE_ROW_NOM_C[k]);
		dml_print("DML::%s: k=%0d, DST_Y_PER_META_ROW_NOM_L                     = %f\n",
				__func__, k, DST_Y_PER_META_ROW_NOM_L[k]);
		dml_print("DML::%s: k=%0d, DST_Y_PER_META_ROW_NOM_C                     = %f\n",
				__func__, k, DST_Y_PER_META_ROW_NOM_C[k]);
		dml_print("DML::%s: k=%0d, TimePerMetaChunkNominal          = %f\n",
				__func__, k, TimePerMetaChunkNominal[k]);
		dml_print("DML::%s: k=%0d, TimePerMetaChunkVBlank           = %f\n",
				__func__, k, TimePerMetaChunkVBlank[k]);
		dml_print("DML::%s: k=%0d, TimePerMetaChunkFlip             = %f\n",
				__func__, k, TimePerMetaChunkFlip[k]);
		dml_print("DML::%s: k=%0d, TimePerChromaMetaChunkNominal    = %f\n",
				__func__, k, TimePerChromaMetaChunkNominal[k]);
		dml_print("DML::%s: k=%0d, TimePerChromaMetaChunkVBlank     = %f\n",
				__func__, k, TimePerChromaMetaChunkVBlank[k]);
		dml_print("DML::%s: k=%0d, TimePerChromaMetaChunkFlip       = %f\n",
				__func__, k, TimePerChromaMetaChunkFlip[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_nom_luma      = %f\n",
				__func__, k, time_per_pte_group_nom_luma[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_vblank_luma   = %f\n",
				__func__, k, time_per_pte_group_vblank_luma[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_flip_luma     = %f\n",
				__func__, k, time_per_pte_group_flip_luma[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_nom_chroma    = %f\n",
				__func__, k, time_per_pte_group_nom_chroma[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_vblank_chroma = %f\n",
				__func__, k, time_per_pte_group_vblank_chroma[k]);
		dml_print("DML::%s: k=%0d, time_per_pte_group_flip_chroma   = %f\n",
				__func__, k, time_per_pte_group_flip_chroma[k]);
#endif
	}
} // CalculateMetaAndPTETimes

void dml32_CalculateVMGroupAndRequestTimes(
		unsigned int     NumberOfActiveSurfaces,
		bool     GPUVMEnable,
		unsigned int     GPUVMMaxPageTableLevels,
		unsigned int     HTotal[],
		unsigned int     BytePerPixelC[],
		double      DestinationLinesToRequestVMInVBlank[],
		double      DestinationLinesToRequestVMInImmediateFlip[],
		bool     DCCEnable[],
		double      PixelClock[],
		unsigned int        dpte_row_width_luma_ub[],
		unsigned int        dpte_row_width_chroma_ub[],
		unsigned int     vm_group_bytes[],
		unsigned int     dpde0_bytes_per_frame_ub_l[],
		unsigned int     dpde0_bytes_per_frame_ub_c[],
		unsigned int        meta_pte_bytes_per_frame_ub_l[],
		unsigned int        meta_pte_bytes_per_frame_ub_c[],

		/* Output */
		double      TimePerVMGroupVBlank[],
		double      TimePerVMGroupFlip[],
		double      TimePerVMRequestVBlank[],
		double      TimePerVMRequestFlip[])
{
	unsigned int k;
	unsigned int   num_group_per_lower_vm_stage;
	unsigned int   num_req_per_lower_vm_stage;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: NumberOfActiveSurfaces = %d\n", __func__, NumberOfActiveSurfaces);
	dml_print("DML::%s: GPUVMEnable = %d\n", __func__, GPUVMEnable);
#endif
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d, DCCEnable = %d\n", __func__, k, DCCEnable[k]);
		dml_print("DML::%s: k=%0d, vm_group_bytes = %d\n", __func__, k, vm_group_bytes[k]);
		dml_print("DML::%s: k=%0d, dpde0_bytes_per_frame_ub_l = %d\n",
				__func__, k, dpde0_bytes_per_frame_ub_l[k]);
		dml_print("DML::%s: k=%0d, dpde0_bytes_per_frame_ub_c = %d\n",
				__func__, k, dpde0_bytes_per_frame_ub_c[k]);
		dml_print("DML::%s: k=%0d, meta_pte_bytes_per_frame_ub_l = %d\n",
				__func__, k, meta_pte_bytes_per_frame_ub_l[k]);
		dml_print("DML::%s: k=%0d, meta_pte_bytes_per_frame_ub_c = %d\n",
				__func__, k, meta_pte_bytes_per_frame_ub_c[k]);
#endif

		if (GPUVMEnable == true && (DCCEnable[k] == true || GPUVMMaxPageTableLevels > 1)) {
			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage = dml_ceil(
							(double) (dpde0_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1.0) +
							dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) /
							(double) (vm_group_bytes[k]), 1.0);
				} else {
					num_group_per_lower_vm_stage = dml_ceil(
							(double) (dpde0_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1.0);
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = dml_ceil(
							(double) (meta_pte_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1.0) +
							dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) /
							(double) (vm_group_bytes[k]), 1.0);
					} else {
						num_group_per_lower_vm_stage = dml_ceil(
								(double) (meta_pte_bytes_per_frame_ub_l[k]) /
								(double) (vm_group_bytes[k]), 1.0);
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = 2 + dml_ceil(
							(double) (dpde0_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1) +
							dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) /
							(double) (vm_group_bytes[k]), 1) +
							dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1) +
							dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) /
							(double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage = 1 + dml_ceil(
							(double) (dpde0_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1) + dml_ceil(
							(double) (meta_pte_bytes_per_frame_ub_l[k]) /
							(double) (vm_group_bytes[k]), 1);
					}
				}
			}

			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64 +
							dpde0_bytes_per_frame_ub_c[k] / 64;
				} else {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64;
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64 +
								meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] /
								64 + dpde0_bytes_per_frame_ub_c[k] / 64 +
								meta_pte_bytes_per_frame_ub_l[k] / 64 +
								meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] /
								64 + meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				}
			}

			TimePerVMGroupVBlank[k] = DestinationLinesToRequestVMInVBlank[k] *
					HTotal[k] / PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMGroupFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] *
					HTotal[k] / PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMRequestVBlank[k] = DestinationLinesToRequestVMInVBlank[k] *
					HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;
			TimePerVMRequestFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] *
					HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;

			if (GPUVMMaxPageTableLevels > 2) {
				TimePerVMGroupVBlank[k]    = TimePerVMGroupVBlank[k] / 2;
				TimePerVMGroupFlip[k]      = TimePerVMGroupFlip[k] / 2;
				TimePerVMRequestVBlank[k]  = TimePerVMRequestVBlank[k] / 2;
				TimePerVMRequestFlip[k]    = TimePerVMRequestFlip[k] / 2;
			}

		} else {
			TimePerVMGroupVBlank[k] = 0;
			TimePerVMGroupFlip[k] = 0;
			TimePerVMRequestVBlank[k] = 0;
			TimePerVMRequestFlip[k] = 0;
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d, TimePerVMGroupVBlank = %f\n", __func__, k, TimePerVMGroupVBlank[k]);
		dml_print("DML::%s: k=%0d, TimePerVMGroupFlip = %f\n", __func__, k, TimePerVMGroupFlip[k]);
		dml_print("DML::%s: k=%0d, TimePerVMRequestVBlank = %f\n", __func__, k, TimePerVMRequestVBlank[k]);
		dml_print("DML::%s: k=%0d, TimePerVMRequestFlip = %f\n", __func__, k, TimePerVMRequestFlip[k]);
#endif
	}
} // CalculateVMGroupAndRequestTimes

void dml32_CalculateDCCConfiguration(
		bool             DCCEnabled,
		bool             DCCProgrammingAssumesScanDirectionUnknown,
		enum source_format_class SourcePixelFormat,
		unsigned int             SurfaceWidthLuma,
		unsigned int             SurfaceWidthChroma,
		unsigned int             SurfaceHeightLuma,
		unsigned int             SurfaceHeightChroma,
		unsigned int                nomDETInKByte,
		unsigned int             RequestHeight256ByteLuma,
		unsigned int             RequestHeight256ByteChroma,
		enum dm_swizzle_mode     TilingFormat,
		unsigned int             BytePerPixelY,
		unsigned int             BytePerPixelC,
		double              BytePerPixelDETY,
		double              BytePerPixelDETC,
		enum dm_rotation_angle   SourceRotation,
		/* Output */
		unsigned int        *MaxUncompressedBlockLuma,
		unsigned int        *MaxUncompressedBlockChroma,
		unsigned int        *MaxCompressedBlockLuma,
		unsigned int        *MaxCompressedBlockChroma,
		unsigned int        *IndependentBlockLuma,
		unsigned int        *IndependentBlockChroma)
{
	typedef enum {
		REQ_256Bytes,
		REQ_128BytesNonContiguous,
		REQ_128BytesContiguous,
		REQ_NA
	} RequestType;

	RequestType   RequestLuma;
	RequestType   RequestChroma;

	unsigned int   segment_order_horz_contiguous_luma;
	unsigned int   segment_order_horz_contiguous_chroma;
	unsigned int   segment_order_vert_contiguous_luma;
	unsigned int   segment_order_vert_contiguous_chroma;
	unsigned int req128_horz_wc_l;
	unsigned int req128_horz_wc_c;
	unsigned int req128_vert_wc_l;
	unsigned int req128_vert_wc_c;
	unsigned int MAS_vp_horz_limit;
	unsigned int MAS_vp_vert_limit;
	unsigned int max_vp_horz_width;
	unsigned int max_vp_vert_height;
	unsigned int eff_surf_width_l;
	unsigned int eff_surf_width_c;
	unsigned int eff_surf_height_l;
	unsigned int eff_surf_height_c;
	unsigned int full_swath_bytes_horz_wc_l;
	unsigned int full_swath_bytes_horz_wc_c;
	unsigned int full_swath_bytes_vert_wc_l;
	unsigned int full_swath_bytes_vert_wc_c;
	unsigned int DETBufferSizeForDCC = nomDETInKByte * 1024;

	unsigned int   yuv420;
	unsigned int   horz_div_l;
	unsigned int   horz_div_c;
	unsigned int   vert_div_l;
	unsigned int   vert_div_c;

	unsigned int     swath_buf_size;
	double   detile_buf_vp_horz_limit;
	double   detile_buf_vp_vert_limit;

	yuv420 = ((SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 ||
			SourcePixelFormat == dm_420_12) ? 1 : 0);
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size / ((double) RequestHeight256ByteLuma *
				BytePerPixelY / (1 + horz_div_l));
		detile_buf_vp_vert_limit = (double) swath_buf_size / (256.0 / RequestHeight256ByteLuma /
				(1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size / ((double) RequestHeight256ByteLuma *
				BytePerPixelY / (1 + horz_div_l) + (double) RequestHeight256ByteChroma *
				BytePerPixelC / (1 + horz_div_c) / (1 + yuv420));
		detile_buf_vp_vert_limit = (double) swath_buf_size / (256.0 / RequestHeight256ByteLuma /
				(1 + vert_div_l) + 256.0 / RequestHeight256ByteChroma /
				(1 + vert_div_c) / (1 + yuv420));
	}

	if (SourcePixelFormat == dm_420_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = dml_floor(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = dml_floor(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = SourcePixelFormat == dm_rgbe_alpha ? 3840 : 6144;
	MAS_vp_vert_limit = SourcePixelFormat == dm_rgbe_alpha ? 3840 : (BytePerPixelY == 8 ? 3072 : 6144);
	max_vp_horz_width = dml_min((double) MAS_vp_horz_limit, detile_buf_vp_horz_limit);
	max_vp_vert_height = dml_min((double) MAS_vp_vert_limit, detile_buf_vp_vert_limit);
	eff_surf_width_l =  (SurfaceWidthLuma > max_vp_horz_width ? max_vp_horz_width : SurfaceWidthLuma);
	eff_surf_width_c =  eff_surf_width_l / (1 + yuv420);
	eff_surf_height_l =  (SurfaceHeightLuma > max_vp_vert_height ? max_vp_vert_height : SurfaceHeightLuma);
	eff_surf_height_c =  eff_surf_height_l / (1 + yuv420);

	full_swath_bytes_horz_wc_l = eff_surf_width_l * RequestHeight256ByteLuma * BytePerPixelY;
	full_swath_bytes_vert_wc_l = eff_surf_height_l * 256 / RequestHeight256ByteLuma;
	if (BytePerPixelC > 0) {
		full_swath_bytes_horz_wc_c = eff_surf_width_c * RequestHeight256ByteChroma * BytePerPixelC;
		full_swath_bytes_vert_wc_c = eff_surf_height_c * 256 / RequestHeight256ByteChroma;
	} else {
		full_swath_bytes_horz_wc_c = 0;
		full_swath_bytes_vert_wc_c = 0;
	}

	if (SourcePixelFormat == dm_420_10) {
		full_swath_bytes_horz_wc_l = dml_ceil((double) full_swath_bytes_horz_wc_l * 2.0 / 3.0, 256.0);
		full_swath_bytes_horz_wc_c = dml_ceil((double) full_swath_bytes_horz_wc_c * 2.0 / 3.0, 256.0);
		full_swath_bytes_vert_wc_l = dml_ceil((double) full_swath_bytes_vert_wc_l * 2.0 / 3.0, 256.0);
		full_swath_bytes_vert_wc_c = dml_ceil((double) full_swath_bytes_vert_wc_c * 2.0 / 3.0, 256.0);
	}

	if (2 * full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 0;
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c && 2 * full_swath_bytes_horz_wc_l +
			full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c && full_swath_bytes_horz_wc_l + 2 *
			full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c && 2 *
			full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c &&
			full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 0;
	} else {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 1;
	}

	if (BytePerPixelY == 2) {
		segment_order_horz_contiguous_luma = 0;
		segment_order_vert_contiguous_luma = 1;
	} else {
		segment_order_horz_contiguous_luma = 1;
		segment_order_vert_contiguous_luma = 0;
	}

	if (BytePerPixelC == 2) {
		segment_order_horz_contiguous_chroma = 0;
		segment_order_vert_contiguous_chroma = 1;
	} else {
		segment_order_horz_contiguous_chroma = 1;
		segment_order_vert_contiguous_chroma = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DCCEnabled = %d\n", __func__, DCCEnabled);
	dml_print("DML::%s: nomDETInKByte = %d\n", __func__, nomDETInKByte);
	dml_print("DML::%s: DETBufferSizeForDCC = %d\n", __func__, DETBufferSizeForDCC);
	dml_print("DML::%s: req128_horz_wc_l = %d\n", __func__, req128_horz_wc_l);
	dml_print("DML::%s: req128_horz_wc_c = %d\n", __func__, req128_horz_wc_c);
	dml_print("DML::%s: full_swath_bytes_horz_wc_l = %d\n", __func__, full_swath_bytes_horz_wc_l);
	dml_print("DML::%s: full_swath_bytes_vert_wc_c = %d\n", __func__, full_swath_bytes_vert_wc_c);
	dml_print("DML::%s: segment_order_horz_contiguous_luma = %d\n", __func__, segment_order_horz_contiguous_luma);
	dml_print("DML::%s: segment_order_horz_contiguous_chroma = %d\n",
			__func__, segment_order_horz_contiguous_chroma);
#endif

	if (DCCProgrammingAssumesScanDirectionUnknown == true) {
		if (req128_horz_wc_l == 0 && req128_vert_wc_l == 0)
			RequestLuma = REQ_256Bytes;
		else if ((req128_horz_wc_l == 1 && segment_order_horz_contiguous_luma == 0) ||
				(req128_vert_wc_l == 1 && segment_order_vert_contiguous_luma == 0))
			RequestLuma = REQ_128BytesNonContiguous;
		else
			RequestLuma = REQ_128BytesContiguous;

		if (req128_horz_wc_c == 0 && req128_vert_wc_c == 0)
			RequestChroma = REQ_256Bytes;
		else if ((req128_horz_wc_c == 1 && segment_order_horz_contiguous_chroma == 0) ||
				(req128_vert_wc_c == 1 && segment_order_vert_contiguous_chroma == 0))
			RequestChroma = REQ_128BytesNonContiguous;
		else
			RequestChroma = REQ_128BytesContiguous;

	} else if (!IsVertical(SourceRotation)) {
		if (req128_horz_wc_l == 0)
			RequestLuma = REQ_256Bytes;
		else if (segment_order_horz_contiguous_luma == 0)
			RequestLuma = REQ_128BytesNonContiguous;
		else
			RequestLuma = REQ_128BytesContiguous;

		if (req128_horz_wc_c == 0)
			RequestChroma = REQ_256Bytes;
		else if (segment_order_horz_contiguous_chroma == 0)
			RequestChroma = REQ_128BytesNonContiguous;
		else
			RequestChroma = REQ_128BytesContiguous;

	} else {
		if (req128_vert_wc_l == 0)
			RequestLuma = REQ_256Bytes;
		else if (segment_order_vert_contiguous_luma == 0)
			RequestLuma = REQ_128BytesNonContiguous;
		else
			RequestLuma = REQ_128BytesContiguous;

		if (req128_vert_wc_c == 0)
			RequestChroma = REQ_256Bytes;
		else if (segment_order_vert_contiguous_chroma == 0)
			RequestChroma = REQ_128BytesNonContiguous;
		else
			RequestChroma = REQ_128BytesContiguous;
	}

	if (RequestLuma == REQ_256Bytes) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 256;
		*IndependentBlockLuma = 0;
	} else if (RequestLuma == REQ_128BytesContiguous) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 128;
		*IndependentBlockLuma = 128;
	} else {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 64;
		*IndependentBlockLuma = 64;
	}

	if (RequestChroma == REQ_256Bytes) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 256;
		*IndependentBlockChroma = 0;
	} else if (RequestChroma == REQ_128BytesContiguous) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 128;
		*IndependentBlockChroma = 128;
	} else {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 64;
		*IndependentBlockChroma = 64;
	}

	if (DCCEnabled != true || BytePerPixelC == 0) {
		*MaxUncompressedBlockChroma = 0;
		*MaxCompressedBlockChroma = 0;
		*IndependentBlockChroma = 0;
	}

	if (DCCEnabled != true) {
		*MaxUncompressedBlockLuma = 0;
		*MaxCompressedBlockLuma = 0;
		*IndependentBlockLuma = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MaxUncompressedBlockLuma = %d\n", __func__, *MaxUncompressedBlockLuma);
	dml_print("DML::%s: MaxCompressedBlockLuma = %d\n", __func__, *MaxCompressedBlockLuma);
	dml_print("DML::%s: IndependentBlockLuma = %d\n", __func__, *IndependentBlockLuma);
	dml_print("DML::%s: MaxUncompressedBlockChroma = %d\n", __func__, *MaxUncompressedBlockChroma);
	dml_print("DML::%s: MaxCompressedBlockChroma = %d\n", __func__, *MaxCompressedBlockChroma);
	dml_print("DML::%s: IndependentBlockChroma = %d\n", __func__, *IndependentBlockChroma);
#endif

} // CalculateDCCConfiguration

void dml32_CalculateStutterEfficiency(
		unsigned int      CompressedBufferSizeInkByte,
		enum dm_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
		bool   UnboundedRequestEnabled,
		unsigned int      MetaFIFOSizeInKEntries,
		unsigned int      ZeroSizeBufferEntries,
		unsigned int      PixelChunkSizeInKByte,
		unsigned int   NumberOfActiveSurfaces,
		unsigned int      ROBBufferSizeInKByte,
		double    TotalDataReadBandwidth,
		double    DCFCLK,
		double    ReturnBW,
		unsigned int      CompbufReservedSpace64B,
		unsigned int      CompbufReservedSpaceZs,
		double    SRExitTime,
		double    SRExitZ8Time,
		bool   SynchronizeTimingsFinal,
		unsigned int   BlendingAndTiming[],
		double    StutterEnterPlusExitWatermark,
		double    Z8StutterEnterPlusExitWatermark,
		bool   ProgressiveToInterlaceUnitInOPP,
		bool   Interlace[],
		double    MinTTUVBlank[],
		unsigned int   DPPPerSurface[],
		unsigned int      DETBufferSizeY[],
		unsigned int   BytePerPixelY[],
		double    BytePerPixelDETY[],
		double      SwathWidthY[],
		unsigned int   SwathHeightY[],
		unsigned int   SwathHeightC[],
		double    NetDCCRateLuma[],
		double    NetDCCRateChroma[],
		double    DCCFractionOfZeroSizeRequestsLuma[],
		double    DCCFractionOfZeroSizeRequestsChroma[],
		unsigned int      HTotal[],
		unsigned int      VTotal[],
		double    PixelClock[],
		double    VRatio[],
		enum dm_rotation_angle SourceRotation[],
		unsigned int   BlockHeight256BytesY[],
		unsigned int   BlockWidth256BytesY[],
		unsigned int   BlockHeight256BytesC[],
		unsigned int   BlockWidth256BytesC[],
		unsigned int   DCCYMaxUncompressedBlock[],
		unsigned int   DCCCMaxUncompressedBlock[],
		unsigned int      VActive[],
		bool   DCCEnable[],
		bool   WritebackEnable[],
		double    ReadBandwidthSurfaceLuma[],
		double    ReadBandwidthSurfaceChroma[],
		double    meta_row_bw[],
		double    dpte_row_bw[],

		/* Output */
		double   *StutterEfficiencyNotIncludingVBlank,
		double   *StutterEfficiency,
		unsigned int     *NumberOfStutterBurstsPerFrame,
		double   *Z8StutterEfficiencyNotIncludingVBlank,
		double   *Z8StutterEfficiency,
		unsigned int     *Z8NumberOfStutterBurstsPerFrame,
		double   *StutterPeriod,
		bool  *DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE)
{

	bool FoundCriticalSurface = false;
	unsigned int SwathSizeCriticalSurface = 0;
	unsigned int LastChunkOfSwathSize;
	unsigned int MissingPartOfLastSwathOfDETSize;
	double LastZ8StutterPeriod = 0.0;
	double LastStutterPeriod = 0.0;
	unsigned int TotalNumberOfActiveOTG = 0;
	double doublePixelClock = 0;
	unsigned int doubleHTotal = 0;
	unsigned int doubleVTotal = 0;
	bool SameTiming = true;
	double DETBufferingTimeY;
	double SwathWidthYCriticalSurface = 0.0;
	double SwathHeightYCriticalSurface = 0.0;
	double VActiveTimeCriticalSurface = 0.0;
	double FrameTimeCriticalSurface = 0.0;
	unsigned int BytePerPixelYCriticalSurface = 0;
	double LinesToFinishSwathTransferStutterCriticalSurface = 0.0;
	unsigned int DETBufferSizeYCriticalSurface = 0;
	double MinTTUVBlankCriticalSurface = 0.0;
	unsigned int BlockWidth256BytesYCriticalSurface = 0;
	bool doublePlaneCriticalSurface = 0;
	bool doublePipeCriticalSurface = 0;
	double TotalCompressedReadBandwidth;
	double TotalRowReadBandwidth;
	double AverageDCCCompressionRate;
	double EffectiveCompressedBufferSize;
	double PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer;
	double StutterBurstTime;
	unsigned int TotalActiveWriteback;
	double LinesInDETY;
	double LinesInDETYRoundedDownToSwath;
	double MaximumEffectiveCompressionLuma;
	double MaximumEffectiveCompressionChroma;
	double TotalZeroSizeRequestReadBandwidth;
	double TotalZeroSizeCompressedReadBandwidth;
	double AverageDCCZeroSizeFraction;
	double AverageZeroSizeCompressionRate;
	unsigned int k;

	TotalZeroSizeRequestReadBandwidth = 0;
	TotalZeroSizeCompressedReadBandwidth = 0;
	TotalRowReadBandwidth = 0;
	TotalCompressedReadBandwidth = 0;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) {
			if (DCCEnable[k] == true) {
				if ((IsVertical(SourceRotation[k]) && BlockWidth256BytesY[k] > SwathHeightY[k])
						|| (!IsVertical(SourceRotation[k])
								&& BlockHeight256BytesY[k] > SwathHeightY[k])
						|| DCCYMaxUncompressedBlock[k] < 256) {
					MaximumEffectiveCompressionLuma = 2;
				} else {
					MaximumEffectiveCompressionLuma = 4;
				}
				TotalCompressedReadBandwidth = TotalCompressedReadBandwidth
						+ ReadBandwidthSurfaceLuma[k]
								/ dml_min(NetDCCRateLuma[k],
										MaximumEffectiveCompressionLuma);
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%0d, ReadBandwidthSurfaceLuma = %f\n",
						__func__, k, ReadBandwidthSurfaceLuma[k]);
				dml_print("DML::%s: k=%0d, NetDCCRateLuma = %f\n",
						__func__, k, NetDCCRateLuma[k]);
				dml_print("DML::%s: k=%0d, MaximumEffectiveCompressionLuma = %f\n",
						__func__, k, MaximumEffectiveCompressionLuma);
#endif
				TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth
						+ ReadBandwidthSurfaceLuma[k] * DCCFractionOfZeroSizeRequestsLuma[k];
				TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth
						+ ReadBandwidthSurfaceLuma[k] * DCCFractionOfZeroSizeRequestsLuma[k]
								/ MaximumEffectiveCompressionLuma;

				if (ReadBandwidthSurfaceChroma[k] > 0) {
					if ((IsVertical(SourceRotation[k]) && BlockWidth256BytesC[k] > SwathHeightC[k])
							|| (!IsVertical(SourceRotation[k])
									&& BlockHeight256BytesC[k] > SwathHeightC[k])
							|| DCCCMaxUncompressedBlock[k] < 256) {
						MaximumEffectiveCompressionChroma = 2;
					} else {
						MaximumEffectiveCompressionChroma = 4;
					}
					TotalCompressedReadBandwidth =
							TotalCompressedReadBandwidth
							+ ReadBandwidthSurfaceChroma[k]
							/ dml_min(NetDCCRateChroma[k],
							MaximumEffectiveCompressionChroma);
#ifdef __DML_VBA_DEBUG__
					dml_print("DML::%s: k=%0d, ReadBandwidthSurfaceChroma = %f\n",
							__func__, k, ReadBandwidthSurfaceChroma[k]);
					dml_print("DML::%s: k=%0d, NetDCCRateChroma = %f\n",
							__func__, k, NetDCCRateChroma[k]);
					dml_print("DML::%s: k=%0d, MaximumEffectiveCompressionChroma = %f\n",
							__func__, k, MaximumEffectiveCompressionChroma);
#endif
					TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth
							+ ReadBandwidthSurfaceChroma[k]
									* DCCFractionOfZeroSizeRequestsChroma[k];
					TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth
							+ ReadBandwidthSurfaceChroma[k]
									* DCCFractionOfZeroSizeRequestsChroma[k]
									/ MaximumEffectiveCompressionChroma;
				}
			} else {
				TotalCompressedReadBandwidth = TotalCompressedReadBandwidth
						+ ReadBandwidthSurfaceLuma[k] + ReadBandwidthSurfaceChroma[k];
			}
			TotalRowReadBandwidth = TotalRowReadBandwidth
					+ DPPPerSurface[k] * (meta_row_bw[k] + dpte_row_bw[k]);
		}
	}

	AverageDCCCompressionRate = TotalDataReadBandwidth / TotalCompressedReadBandwidth;
	AverageDCCZeroSizeFraction = TotalZeroSizeRequestReadBandwidth / TotalDataReadBandwidth;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: UnboundedRequestEnabled = %d\n", __func__, UnboundedRequestEnabled);
	dml_print("DML::%s: TotalCompressedReadBandwidth = %f\n", __func__, TotalCompressedReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeRequestReadBandwidth = %f\n", __func__, TotalZeroSizeRequestReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeCompressedReadBandwidth = %f\n",
			__func__, TotalZeroSizeCompressedReadBandwidth);
	dml_print("DML::%s: MaximumEffectiveCompressionLuma = %f\n", __func__, MaximumEffectiveCompressionLuma);
	dml_print("DML::%s: MaximumEffectiveCompressionChroma = %f\n", __func__, MaximumEffectiveCompressionChroma);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: AverageDCCZeroSizeFraction = %f\n", __func__, AverageDCCZeroSizeFraction);
	dml_print("DML::%s: CompbufReservedSpace64B = %d\n", __func__, CompbufReservedSpace64B);
	dml_print("DML::%s: CompbufReservedSpaceZs = %d\n", __func__, CompbufReservedSpaceZs);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %d\n", __func__, CompressedBufferSizeInkByte);
#endif
	if (AverageDCCZeroSizeFraction == 1) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth
				/ TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = (double) MetaFIFOSizeInKEntries * 1024 * 64
				* AverageZeroSizeCompressionRate
				+ ((double) ZeroSizeBufferEntries - CompbufReservedSpaceZs) * 64
						* AverageZeroSizeCompressionRate;
	} else if (AverageDCCZeroSizeFraction > 0) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth
				/ TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = dml_min(
				(double) CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
				(double) MetaFIFOSizeInKEntries * 1024 * 64
					/ (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate
					+ 1 / AverageDCCCompressionRate))
					+ dml_min(((double) ROBBufferSizeInKByte * 1024 - CompbufReservedSpace64B * 64)
					* AverageDCCCompressionRate,
					((double) ZeroSizeBufferEntries - CompbufReservedSpaceZs) * 64
					/ (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: min 1 = %f\n", __func__,
				CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 2 = %f\n", __func__, MetaFIFOSizeInKEntries * 1024 * 64 /
				(AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate + 1 /
						AverageDCCCompressionRate));
		dml_print("DML::%s: min 3 = %f\n", __func__, (ROBBufferSizeInKByte * 1024 -
				CompbufReservedSpace64B * 64) * AverageDCCCompressionRate);
		dml_print("DML::%s: min 4 = %f\n", __func__, (ZeroSizeBufferEntries - CompbufReservedSpaceZs) * 64 /
				(AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));
#endif
	} else {
		EffectiveCompressedBufferSize = dml_min(
				(double) CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
				(double) MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate)
				+ ((double) ROBBufferSizeInKByte * 1024 - CompbufReservedSpace64B * 64)
						* AverageDCCCompressionRate;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: min 1 = %f\n", __func__,
				CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 2 = %f\n", __func__,
				MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate);
#endif
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MetaFIFOSizeInKEntries = %d\n", __func__, MetaFIFOSizeInKEntries);
	dml_print("DML::%s: AverageZeroSizeCompressionRate = %f\n", __func__, AverageZeroSizeCompressionRate);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
#endif

	*StutterPeriod = 0;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) {
			LinesInDETY = ((double) DETBufferSizeY[k]
					+ (UnboundedRequestEnabled == true ? EffectiveCompressedBufferSize : 0)
							* ReadBandwidthSurfaceLuma[k] / TotalDataReadBandwidth)
					/ BytePerPixelDETY[k] / SwathWidthY[k];
			LinesInDETYRoundedDownToSwath = dml_floor(LinesInDETY, SwathHeightY[k]);
			DETBufferingTimeY = LinesInDETYRoundedDownToSwath * ((double) HTotal[k] / PixelClock[k])
					/ VRatio[k];
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d, DETBufferSizeY = %d\n", __func__, k, DETBufferSizeY[k]);
			dml_print("DML::%s: k=%0d, BytePerPixelDETY = %f\n", __func__, k, BytePerPixelDETY[k]);
			dml_print("DML::%s: k=%0d, SwathWidthY = %d\n", __func__, k, SwathWidthY[k]);
			dml_print("DML::%s: k=%0d, ReadBandwidthSurfaceLuma = %f\n",
					__func__, k, ReadBandwidthSurfaceLuma[k]);
			dml_print("DML::%s: k=%0d, TotalDataReadBandwidth = %f\n", __func__, k, TotalDataReadBandwidth);
			dml_print("DML::%s: k=%0d, LinesInDETY = %f\n", __func__, k, LinesInDETY);
			dml_print("DML::%s: k=%0d, LinesInDETYRoundedDownToSwath = %f\n",
					__func__, k, LinesInDETYRoundedDownToSwath);
			dml_print("DML::%s: k=%0d, HTotal = %d\n", __func__, k, HTotal[k]);
			dml_print("DML::%s: k=%0d, PixelClock = %f\n", __func__, k, PixelClock[k]);
			dml_print("DML::%s: k=%0d, VRatio = %f\n", __func__, k, VRatio[k]);
			dml_print("DML::%s: k=%0d, DETBufferingTimeY = %f\n", __func__, k, DETBufferingTimeY);
			dml_print("DML::%s: k=%0d, PixelClock = %f\n", __func__, k, PixelClock[k]);
#endif

			if (!FoundCriticalSurface || DETBufferingTimeY < *StutterPeriod) {
				bool isInterlaceTiming = Interlace[k] && !ProgressiveToInterlaceUnitInOPP;

				FoundCriticalSurface = true;
				*StutterPeriod = DETBufferingTimeY;
				FrameTimeCriticalSurface = (
						isInterlaceTiming ?
								dml_floor((double) VTotal[k] / 2.0, 1.0) : VTotal[k])
						* (double) HTotal[k] / PixelClock[k];
				VActiveTimeCriticalSurface = (
						isInterlaceTiming ?
								dml_floor((double) VActive[k] / 2.0, 1.0) : VActive[k])
						* (double) HTotal[k] / PixelClock[k];
				BytePerPixelYCriticalSurface = BytePerPixelY[k];
				SwathWidthYCriticalSurface = SwathWidthY[k];
				SwathHeightYCriticalSurface = SwathHeightY[k];
				BlockWidth256BytesYCriticalSurface = BlockWidth256BytesY[k];
				LinesToFinishSwathTransferStutterCriticalSurface = SwathHeightY[k]
						- (LinesInDETY - LinesInDETYRoundedDownToSwath);
				DETBufferSizeYCriticalSurface = DETBufferSizeY[k];
				MinTTUVBlankCriticalSurface = MinTTUVBlank[k];
				doublePlaneCriticalSurface = (ReadBandwidthSurfaceChroma[k] == 0);
				doublePipeCriticalSurface = (DPPPerSurface[k] == 1);

#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%0d, FoundCriticalSurface                = %d\n",
						__func__, k, FoundCriticalSurface);
				dml_print("DML::%s: k=%0d, StutterPeriod                       = %f\n",
						__func__, k, *StutterPeriod);
				dml_print("DML::%s: k=%0d, MinTTUVBlankCriticalSurface         = %f\n",
						__func__, k, MinTTUVBlankCriticalSurface);
				dml_print("DML::%s: k=%0d, FrameTimeCriticalSurface            = %f\n",
						__func__, k, FrameTimeCriticalSurface);
				dml_print("DML::%s: k=%0d, VActiveTimeCriticalSurface          = %f\n",
						__func__, k, VActiveTimeCriticalSurface);
				dml_print("DML::%s: k=%0d, BytePerPixelYCriticalSurface        = %d\n",
						__func__, k, BytePerPixelYCriticalSurface);
				dml_print("DML::%s: k=%0d, SwathWidthYCriticalSurface          = %f\n",
						__func__, k, SwathWidthYCriticalSurface);
				dml_print("DML::%s: k=%0d, SwathHeightYCriticalSurface         = %f\n",
						__func__, k, SwathHeightYCriticalSurface);
				dml_print("DML::%s: k=%0d, BlockWidth256BytesYCriticalSurface  = %d\n",
						__func__, k, BlockWidth256BytesYCriticalSurface);
				dml_print("DML::%s: k=%0d, doublePlaneCriticalSurface          = %d\n",
						__func__, k, doublePlaneCriticalSurface);
				dml_print("DML::%s: k=%0d, doublePipeCriticalSurface           = %d\n",
						__func__, k, doublePipeCriticalSurface);
				dml_print("DML::%s: k=%0d, LinesToFinishSwathTransferStutterCriticalSurface = %f\n",
						__func__, k, LinesToFinishSwathTransferStutterCriticalSurface);
#endif
			}
		}
	}

	PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = dml_min(*StutterPeriod * TotalDataReadBandwidth,
			EffectiveCompressedBufferSize);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ROBBufferSizeInKByte = %d\n", __func__, ROBBufferSizeInKByte);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n",
			__func__, *StutterPeriod * TotalDataReadBandwidth);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
	dml_print("DML::%s: PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = %f\n", __func__,
			PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer);
	dml_print("DML::%s: ReturnBW = %f\n", __func__, ReturnBW);
	dml_print("DML::%s: TotalDataReadBandwidth = %f\n", __func__, TotalDataReadBandwidth);
	dml_print("DML::%s: TotalRowReadBandwidth = %f\n", __func__, TotalRowReadBandwidth);
	dml_print("DML::%s: DCFCLK = %f\n", __func__, DCFCLK);
#endif

	StutterBurstTime = PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / AverageDCCCompressionRate
			/ ReturnBW
			+ (*StutterPeriod * TotalDataReadBandwidth
					- PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (DCFCLK * 64)
			+ *StutterPeriod * TotalRowReadBandwidth / ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Part 1 = %f\n", __func__, PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer /
			AverageDCCCompressionRate / ReturnBW);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n",
			__func__, (*StutterPeriod * TotalDataReadBandwidth));
	dml_print("DML::%s: Part 2 = %f\n", __func__, (*StutterPeriod * TotalDataReadBandwidth -
			PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (DCFCLK * 64));
	dml_print("DML::%s: Part 3 = %f\n", __func__, *StutterPeriod * TotalRowReadBandwidth / ReturnBW);
	dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
#endif
	StutterBurstTime = dml_max(StutterBurstTime,
			LinesToFinishSwathTransferStutterCriticalSurface * BytePerPixelYCriticalSurface
					* SwathWidthYCriticalSurface / ReturnBW);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Time to finish residue swath=%f\n",
			__func__,
			LinesToFinishSwathTransferStutterCriticalSurface *
			BytePerPixelYCriticalSurface * SwathWidthYCriticalSurface / ReturnBW);
#endif

	TotalActiveWriteback = 0;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (WritebackEnable[k])
			TotalActiveWriteback = TotalActiveWriteback + 1;
	}

	if (TotalActiveWriteback == 0) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: SRExitTime = %f\n", __func__, SRExitTime);
		dml_print("DML::%s: SRExitZ8Time = %f\n", __func__, SRExitZ8Time);
		dml_print("DML::%s: StutterBurstTime = %f (final)\n", __func__, StutterBurstTime);
		dml_print("DML::%s: StutterPeriod = %f\n", __func__, *StutterPeriod);
#endif
		*StutterEfficiencyNotIncludingVBlank = dml_max(0.,
				1 - (SRExitTime + StutterBurstTime) / *StutterPeriod) * 100;
		*Z8StutterEfficiencyNotIncludingVBlank = dml_max(0.,
				1 - (SRExitZ8Time + StutterBurstTime) / *StutterPeriod) * 100;
		*NumberOfStutterBurstsPerFrame = (
				*StutterEfficiencyNotIncludingVBlank > 0 ?
						dml_ceil(VActiveTimeCriticalSurface / *StutterPeriod, 1) : 0);
		*Z8NumberOfStutterBurstsPerFrame = (
				*Z8StutterEfficiencyNotIncludingVBlank > 0 ?
						dml_ceil(VActiveTimeCriticalSurface / *StutterPeriod, 1) : 0);
	} else {
		*StutterEfficiencyNotIncludingVBlank = 0.;
		*Z8StutterEfficiencyNotIncludingVBlank = 0.;
		*NumberOfStutterBurstsPerFrame = 0;
		*Z8NumberOfStutterBurstsPerFrame = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VActiveTimeCriticalSurface = %f\n", __func__, VActiveTimeCriticalSurface);
	dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n",
			__func__, *StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: Z8StutterEfficiencyNotIncludingVBlank = %f\n",
			__func__, *Z8StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: NumberOfStutterBurstsPerFrame = %d\n", __func__, *NumberOfStutterBurstsPerFrame);
	dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %d\n", __func__, *Z8NumberOfStutterBurstsPerFrame);
#endif

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) {
			if (BlendingAndTiming[k] == k) {
				if (TotalNumberOfActiveOTG == 0) {
					doublePixelClock = PixelClock[k];
					doubleHTotal = HTotal[k];
					doubleVTotal = VTotal[k];
				} else if (doublePixelClock != PixelClock[k] || doubleHTotal != HTotal[k]
						|| doubleVTotal != VTotal[k]) {
					SameTiming = false;
				}
				TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
			}
		}
	}

	if (*StutterEfficiencyNotIncludingVBlank > 0) {
		LastStutterPeriod = VActiveTimeCriticalSurface - (*NumberOfStutterBurstsPerFrame - 1) * *StutterPeriod;

		if ((SynchronizeTimingsFinal || TotalNumberOfActiveOTG == 1) && SameTiming
				&& LastStutterPeriod + MinTTUVBlankCriticalSurface > StutterEnterPlusExitWatermark) {
			*StutterEfficiency = (1 - (*NumberOfStutterBurstsPerFrame * SRExitTime
						+ StutterBurstTime * VActiveTimeCriticalSurface
						/ *StutterPeriod) / FrameTimeCriticalSurface) * 100;
		} else {
			*StutterEfficiency = *StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*StutterEfficiency = 0;
	}

	if (*Z8StutterEfficiencyNotIncludingVBlank > 0) {
		LastZ8StutterPeriod = VActiveTimeCriticalSurface
				- (*NumberOfStutterBurstsPerFrame - 1) * *StutterPeriod;
		if ((SynchronizeTimingsFinal || TotalNumberOfActiveOTG == 1) && SameTiming && LastZ8StutterPeriod +
				MinTTUVBlankCriticalSurface > Z8StutterEnterPlusExitWatermark) {
			*Z8StutterEfficiency = (1 - (*NumberOfStutterBurstsPerFrame * SRExitZ8Time + StutterBurstTime
				* VActiveTimeCriticalSurface / *StutterPeriod) / FrameTimeCriticalSurface) * 100;
		} else {
			*Z8StutterEfficiency = *Z8StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*Z8StutterEfficiency = 0.;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: LastZ8StutterPeriod = %f\n", __func__, LastZ8StutterPeriod);
	dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, Z8StutterEnterPlusExitWatermark);
	dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
	dml_print("DML::%s: StutterPeriod = %f\n", __func__, *StutterPeriod);
	dml_print("DML::%s: StutterEfficiency = %f\n", __func__, *StutterEfficiency);
	dml_print("DML::%s: Z8StutterEfficiency = %f\n", __func__, *Z8StutterEfficiency);
	dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n",
			__func__, *StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %d\n", __func__, *Z8NumberOfStutterBurstsPerFrame);
#endif

	SwathSizeCriticalSurface = BytePerPixelYCriticalSurface * SwathHeightYCriticalSurface
			* dml_ceil(SwathWidthYCriticalSurface, BlockWidth256BytesYCriticalSurface);
	LastChunkOfSwathSize = SwathSizeCriticalSurface % (PixelChunkSizeInKByte * 1024);
	MissingPartOfLastSwathOfDETSize = dml_ceil(DETBufferSizeYCriticalSurface, SwathSizeCriticalSurface)
			- DETBufferSizeYCriticalSurface;

	*DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = !(!UnboundedRequestEnabled && (NumberOfActiveSurfaces == 1)
			&& doublePlaneCriticalSurface && doublePipeCriticalSurface && (LastChunkOfSwathSize > 0)
			&& (LastChunkOfSwathSize <= 4096) && (MissingPartOfLastSwathOfDETSize > 0)
			&& (MissingPartOfLastSwathOfDETSize <= LastChunkOfSwathSize));

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: SwathSizeCriticalSurface = %d\n", __func__, SwathSizeCriticalSurface);
	dml_print("DML::%s: LastChunkOfSwathSize = %d\n", __func__, LastChunkOfSwathSize);
	dml_print("DML::%s: MissingPartOfLastSwathOfDETSize = %d\n", __func__, MissingPartOfLastSwathOfDETSize);
	dml_print("DML::%s: DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = %d\n", __func__, *DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
#endif
} // CalculateStutterEfficiency

void dml32_CalculateMaxDETAndMinCompressedBufferSize(
		unsigned int    ConfigReturnBufferSizeInKByte,
		unsigned int    ROBBufferSizeInKByte,
		unsigned int MaxNumDPP,
		bool nomDETInKByteOverrideEnable, // VBA_DELTA, allow DV to override default DET size
		unsigned int nomDETInKByteOverrideValue,  // VBA_DELTA

		/* Output */
		unsigned int *MaxTotalDETInKByte,
		unsigned int *nomDETInKByte,
		unsigned int *MinCompressedBufferSizeInKByte)
{
	bool     det_buff_size_override_en  = nomDETInKByteOverrideEnable;
	unsigned int        det_buff_size_override_val = nomDETInKByteOverrideValue;

	*MaxTotalDETInKByte = dml_ceil(((double)ConfigReturnBufferSizeInKByte +
			(double) ROBBufferSizeInKByte) * 4.0 / 5.0, 64);
	*nomDETInKByte = dml_floor((double) *MaxTotalDETInKByte / (double) MaxNumDPP, 64);
	*MinCompressedBufferSizeInKByte = ConfigReturnBufferSizeInKByte - *MaxTotalDETInKByte;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %0d\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: ROBBufferSizeInKByte = %0d\n", __func__, ROBBufferSizeInKByte);
	dml_print("DML::%s: MaxNumDPP = %0d\n", __func__, MaxNumDPP);
	dml_print("DML::%s: MaxTotalDETInKByte = %0d\n", __func__, *MaxTotalDETInKByte);
	dml_print("DML::%s: nomDETInKByte = %0d\n", __func__, *nomDETInKByte);
	dml_print("DML::%s: MinCompressedBufferSizeInKByte = %0d\n", __func__, *MinCompressedBufferSizeInKByte);
#endif

	if (det_buff_size_override_en) {
		*nomDETInKByte = det_buff_size_override_val;
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: nomDETInKByte = %0d (override)\n", __func__, *nomDETInKByte);
#endif
	}
} // CalculateMaxDETAndMinCompressedBufferSize

bool dml32_CalculateVActiveBandwithSupport(unsigned int NumberOfActiveSurfaces,
		double ReturnBW,
		bool NotUrgentLatencyHiding[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double cursor_bw[],
		double meta_row_bandwidth[],
		double dpte_row_bandwidth[],
		unsigned int NumberOfDPP[],
		double UrgentBurstFactorLuma[],
		double UrgentBurstFactorChroma[],
		double UrgentBurstFactorCursor[])
{
	unsigned int k;
	bool NotEnoughUrgentLatencyHiding = false;
	bool CalculateVActiveBandwithSupport_val = false;
	double VActiveBandwith = 0;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (NotUrgentLatencyHiding[k]) {
			NotEnoughUrgentLatencyHiding = true;
		}
	}

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		VActiveBandwith = VActiveBandwith + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k] + NumberOfDPP[k] * meta_row_bandwidth[k] + NumberOfDPP[k] * dpte_row_bandwidth[k];
	}

	CalculateVActiveBandwithSupport_val = (VActiveBandwith <= ReturnBW) && !NotEnoughUrgentLatencyHiding;

#ifdef __DML_VBA_DEBUG__
dml_print("DML::%s: NotEnoughUrgentLatencyHiding        = %d\n", __func__, NotEnoughUrgentLatencyHiding);
dml_print("DML::%s: VActiveBandwith                     = %f\n", __func__, VActiveBandwith);
dml_print("DML::%s: ReturnBW                            = %f\n", __func__, ReturnBW);
dml_print("DML::%s: CalculateVActiveBandwithSupport_val = %d\n", __func__, CalculateVActiveBandwithSupport_val);
#endif
	return CalculateVActiveBandwithSupport_val;
}

void dml32_CalculatePrefetchBandwithSupport(unsigned int NumberOfActiveSurfaces,
		double ReturnBW,
		bool NotUrgentLatencyHiding[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double PrefetchBandwidthLuma[],
		double PrefetchBandwidthChroma[],
		double cursor_bw[],
		double meta_row_bandwidth[],
		double dpte_row_bandwidth[],
		double cursor_bw_pre[],
		double prefetch_vmrow_bw[],
		unsigned int NumberOfDPP[],
		double UrgentBurstFactorLuma[],
		double UrgentBurstFactorChroma[],
		double UrgentBurstFactorCursor[],
		double UrgentBurstFactorLumaPre[],
		double UrgentBurstFactorChromaPre[],
		double UrgentBurstFactorCursorPre[],
		double PrefetchBW[],
		double VRatio[],
		double MaxVRatioPre,

		/* output */
		double  *MaxPrefetchBandwidth,
		double  *FractionOfUrgentBandwidth,
		bool *PrefetchBandwidthSupport)
{
	unsigned int k;
	double ActiveBandwidthPerSurface;
	bool NotEnoughUrgentLatencyHiding = false;
	double TotalActiveBandwidth = 0;
	double TotalPrefetchBandwidth = 0;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (NotUrgentLatencyHiding[k]) {
			NotEnoughUrgentLatencyHiding = true;
		}
	}

	*MaxPrefetchBandwidth = 0;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		ActiveBandwidthPerSurface = ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k] + NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k]);

		TotalActiveBandwidth += ActiveBandwidthPerSurface;

		TotalPrefetchBandwidth = TotalPrefetchBandwidth + PrefetchBW[k] * VRatio[k];

		*MaxPrefetchBandwidth = *MaxPrefetchBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
				ActiveBandwidthPerSurface,
				NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
	}

	if (MaxVRatioPre == __DML_MAX_VRATIO_PRE__)
		*PrefetchBandwidthSupport = (*MaxPrefetchBandwidth <= ReturnBW) && (TotalPrefetchBandwidth <= TotalActiveBandwidth * __DML_MAX_BW_RATIO_PRE__) && !NotEnoughUrgentLatencyHiding;
	else
		*PrefetchBandwidthSupport = (*MaxPrefetchBandwidth <= ReturnBW) && !NotEnoughUrgentLatencyHiding;

	*FractionOfUrgentBandwidth = *MaxPrefetchBandwidth / ReturnBW;
}

double dml32_CalculateBandwidthAvailableForImmediateFlip(unsigned int NumberOfActiveSurfaces,
		double ReturnBW,
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double PrefetchBandwidthLuma[],
		double PrefetchBandwidthChroma[],
		double cursor_bw[],
		double cursor_bw_pre[],
		unsigned int NumberOfDPP[],
		double UrgentBurstFactorLuma[],
		double UrgentBurstFactorChroma[],
		double UrgentBurstFactorCursor[],
		double UrgentBurstFactorLumaPre[],
		double UrgentBurstFactorChromaPre[],
		double UrgentBurstFactorCursorPre[])
{
	unsigned int k;
	double CalculateBandwidthAvailableForImmediateFlip_val = ReturnBW;

	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		CalculateBandwidthAvailableForImmediateFlip_val = CalculateBandwidthAvailableForImmediateFlip_val - dml_max(ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
				NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
	}

	return CalculateBandwidthAvailableForImmediateFlip_val;
}

void dml32_CalculateImmediateFlipBandwithSupport(unsigned int NumberOfActiveSurfaces,
		double ReturnBW,
		enum immediate_flip_requirement ImmediateFlipRequirement[],
		double final_flip_bw[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		double PrefetchBandwidthLuma[],
		double PrefetchBandwidthChroma[],
		double cursor_bw[],
		double meta_row_bandwidth[],
		double dpte_row_bandwidth[],
		double cursor_bw_pre[],
		double prefetch_vmrow_bw[],
		unsigned int NumberOfDPP[],
		double UrgentBurstFactorLuma[],
		double UrgentBurstFactorChroma[],
		double UrgentBurstFactorCursor[],
		double UrgentBurstFactorLumaPre[],
		double UrgentBurstFactorChromaPre[],
		double UrgentBurstFactorCursorPre[],

		/* output */
		double  *TotalBandwidth,
		double  *FractionOfUrgentBandwidth,
		bool *ImmediateFlipBandwidthSupport)
{
	unsigned int k;
	*TotalBandwidth = 0;
	for (k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (ImmediateFlipRequirement[k] != dm_immediate_flip_not_required) {
			*TotalBandwidth = *TotalBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
					NumberOfDPP[k] * final_flip_bw[k] + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
					NumberOfDPP[k] * (final_flip_bw[k] + PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
		} else {
			*TotalBandwidth = *TotalBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
					NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k]) + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
					NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
		}
	}
	*ImmediateFlipBandwidthSupport = (*TotalBandwidth <= ReturnBW);
	*FractionOfUrgentBandwidth = *TotalBandwidth / ReturnBW;
}

bool dml32_CalculateDETSwathFillLatencyHiding(unsigned int NumberOfActiveSurfaces,
		double ReturnBW,
		double UrgentLatency,
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		unsigned int SwathWidthY[],
		unsigned int SwathWidthC[],
		double  BytePerPixelInDETY[],
		double  BytePerPixelInDETC[],
		unsigned int    DETBufferSizeY[],
		unsigned int    DETBufferSizeC[],
		unsigned int	NumOfDPP[],
		unsigned int	HTotal[],
		double	PixelClock[],
		double	VRatioY[],
		double	VRatioC[],
		enum dm_use_mall_for_pstate_change_mode UsesMALLForPStateChange[],
		enum unbounded_requesting_policy UseUnboundedRequesting)
{
	int k;
	double SwathSizeAllSurfaces = 0;
	double SwathSizeAllSurfacesInFetchTimeUs;
	double DETSwathLatencyHidingUs;
	double DETSwathLatencyHidingYUs;
	double DETSwathLatencyHidingCUs;
	double SwathSizePerSurfaceY[DC__NUM_DPP__MAX];
	double SwathSizePerSurfaceC[DC__NUM_DPP__MAX];
	bool NotEnoughDETSwathFillLatencyHiding = false;

	if (UseUnboundedRequesting == dm_unbounded_requesting)
		return false;

	/* calculate sum of single swath size for all pipes in bytes */
	for (k = 0; k < NumberOfActiveSurfaces; k++) {
		SwathSizePerSurfaceY[k] = SwathHeightY[k] * SwathWidthY[k] * BytePerPixelInDETY[k] * NumOfDPP[k];

		if (SwathHeightC[k] != 0)
			SwathSizePerSurfaceC[k] = SwathHeightC[k] * SwathWidthC[k] * BytePerPixelInDETC[k] * NumOfDPP[k];
		else
			SwathSizePerSurfaceC[k] = 0;

		SwathSizeAllSurfaces += SwathSizePerSurfaceY[k] + SwathSizePerSurfaceC[k];
	}

	SwathSizeAllSurfacesInFetchTimeUs = SwathSizeAllSurfaces / ReturnBW + UrgentLatency;

	/* ensure all DET - 1 swath can hide a fetch for all surfaces */
	for (k = 0; k < NumberOfActiveSurfaces; k++) {
		double LineTime = HTotal[k] / PixelClock[k];

		/* only care if surface is not phantom */
		if (UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_phantom_pipe) {
			DETSwathLatencyHidingYUs = (dml_floor(DETBufferSizeY[k] / BytePerPixelInDETY[k] / SwathWidthY[k], 1.0) - SwathHeightY[k]) / VRatioY[k] * LineTime;

			if (SwathHeightC[k] != 0) {
				DETSwathLatencyHidingCUs = (dml_floor(DETBufferSizeC[k] / BytePerPixelInDETC[k] / SwathWidthC[k], 1.0) - SwathHeightC[k]) / VRatioC[k] * LineTime;

				DETSwathLatencyHidingUs = dml_min(DETSwathLatencyHidingYUs, DETSwathLatencyHidingCUs);
			} else {
				DETSwathLatencyHidingUs = DETSwathLatencyHidingYUs;
			}

			/* DET must be able to hide time to fetch 1 swath for each surface */
			if (DETSwathLatencyHidingUs < SwathSizeAllSurfacesInFetchTimeUs) {
				NotEnoughDETSwathFillLatencyHiding = true;
				break;
			}
		}
	}

	return NotEnoughDETSwathFillLatencyHiding;
}
