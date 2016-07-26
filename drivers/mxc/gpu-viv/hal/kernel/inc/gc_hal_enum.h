/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_enum_h_
#define __gc_hal_enum_h_

#include "gc_hal_options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Chip models. */
typedef enum _gceCHIPMODEL
{
    gcv200  = 0x0200,
    gcv300  = 0x0300,
    gcv320  = 0x0320,
    gcv328  = 0x0328,
    gcv350  = 0x0350,
    gcv355  = 0x0355,
    gcv400  = 0x0400,
    gcv410  = 0x0410,
    gcv420  = 0x0420,
    gcv428  = 0x0428,
    gcv450  = 0x0450,
    gcv500  = 0x0500,
    gcv520  = 0x0520,
    gcv530  = 0x0530,
    gcv600  = 0x0600,
    gcv620  = 0x0620,
    gcv700  = 0x0700,
    gcv800  = 0x0800,
    gcv860  = 0x0860,
    gcv880  = 0x0880,
    gcv900  = 0x0900,
    gcv1000 = 0x1000,
    gcv1500 = 0x1500,
    gcv2000 = 0x2000,
    gcv2100 = 0x2100,
    gcv2200 = 0x2200,
    gcv2500 = 0x2500,
    gcv3000 = 0x3000,
    gcv4000 = 0x4000,
    gcv5000 = 0x5000,
    gcv5200 = 0x5200,
    gcv6400 = 0x6400,
    gcv7000 = 0x7000,
    gcv7400 = 0x7400,
}
gceCHIPMODEL;

/* Chip features. */
typedef enum _gceFEATURE
{
    gcvFEATURE_PIPE_2D = 0,
    gcvFEATURE_PIPE_3D,
    gcvFEATURE_PIPE_VG,
    gcvFEATURE_DC,
    gcvFEATURE_HIGH_DYNAMIC_RANGE,
    gcvFEATURE_MODULE_CG,
    gcvFEATURE_MIN_AREA,
    gcvFEATURE_BUFFER_INTERLEAVING,
    gcvFEATURE_BYTE_WRITE_2D,
    gcvFEATURE_ENDIANNESS_CONFIG,
    gcvFEATURE_DUAL_RETURN_BUS,
    gcvFEATURE_DEBUG_MODE,
    gcvFEATURE_YUY2_RENDER_TARGET,
    gcvFEATURE_FRAGMENT_PROCESSOR,
    gcvFEATURE_2DPE20,
    gcvFEATURE_FAST_CLEAR,
    gcvFEATURE_YUV420_TILER,
    gcvFEATURE_YUY2_AVERAGING,
    gcvFEATURE_FLIP_Y,
    gcvFEATURE_EARLY_Z,
    gcvFEATURE_COMPRESSION,
    gcvFEATURE_MSAA,
    gcvFEATURE_SPECIAL_ANTI_ALIASING,
    gcvFEATURE_SPECIAL_MSAA_LOD,
    gcvFEATURE_422_TEXTURE_COMPRESSION,
    gcvFEATURE_DXT_TEXTURE_COMPRESSION,
    gcvFEATURE_ETC1_TEXTURE_COMPRESSION,
    gcvFEATURE_CORRECT_TEXTURE_CONVERTER,
    gcvFEATURE_TEXTURE_8K,
    gcvFEATURE_SCALER,
    gcvFEATURE_YUV420_SCALER,
    gcvFEATURE_SHADER_HAS_W,
    gcvFEATURE_SHADER_HAS_SIGN,
    gcvFEATURE_SHADER_HAS_FLOOR,
    gcvFEATURE_SHADER_HAS_CEIL,
    gcvFEATURE_SHADER_HAS_SQRT,
    gcvFEATURE_SHADER_HAS_TRIG,
    gcvFEATURE_HZ,
    gcvFEATURE_CORRECT_STENCIL,
    gcvFEATURE_VG20,
    gcvFEATURE_VG_FILTER,
    gcvFEATURE_VG21,
    gcvFEATURE_VG_DOUBLE_BUFFER,
    gcvFEATURE_MC20,
    gcvFEATURE_SUPER_TILED,
    gcvFEATURE_FAST_CLEAR_FLUSH,
    gcvFEATURE_2D_FILTERBLIT_PLUS_ALPHABLEND,
    gcvFEATURE_2D_DITHER,
    gcvFEATURE_2D_A8_TARGET,
    gcvFEATURE_2D_A8_NO_ALPHA,
    gcvFEATURE_2D_FILTERBLIT_FULLROTATION,
    gcvFEATURE_2D_BITBLIT_FULLROTATION,
    gcvFEATURE_WIDE_LINE,
    gcvFEATURE_FC_FLUSH_STALL,
    gcvFEATURE_FULL_DIRECTFB,
    gcvFEATURE_HALF_FLOAT_PIPE,
    gcvFEATURE_LINE_LOOP,
    gcvFEATURE_2D_YUV_BLIT,
    gcvFEATURE_2D_TILING,
    gcvFEATURE_NON_POWER_OF_TWO,
    gcvFEATURE_3D_TEXTURE,
    gcvFEATURE_TEXTURE_ARRAY,
    gcvFEATURE_TILE_FILLER,
    gcvFEATURE_LOGIC_OP,
    gcvFEATURE_COMPOSITION,
    gcvFEATURE_MIXED_STREAMS,
    gcvFEATURE_2D_MULTI_SOURCE_BLT,
    gcvFEATURE_END_EVENT,
    gcvFEATURE_VERTEX_10_10_10_2,
    gcvFEATURE_TEXTURE_10_10_10_2,
    gcvFEATURE_TEXTURE_ANISOTROPIC_FILTERING,
    gcvFEATURE_TEXTURE_FLOAT_HALF_FLOAT,
    gcvFEATURE_2D_ROTATION_STALL_FIX,
    gcvFEATURE_2D_MULTI_SOURCE_BLT_EX,
    gcvFEATURE_BUG_FIXES10,
    gcvFEATURE_2D_MINOR_TILING,
    gcvFEATURE_TEX_COMPRRESSION_SUPERTILED, /* Supertiled compressed textures are supported. */
    gcvFEATURE_FAST_MSAA,
    gcvFEATURE_BUG_FIXED_INDEXED_TRIANGLE_STRIP,
    gcvFEATURE_TEXTURE_TILE_STATUS_READ,
    gcvFEATURE_DEPTH_BIAS_FIX,
    gcvFEATURE_RECT_PRIMITIVE,
    gcvFEATURE_BUG_FIXES11,
    gcvFEATURE_SUPERTILED_TEXTURE,
    gcvFEATURE_2D_NO_COLORBRUSH_INDEX8,
    gcvFEATURE_RS_YUV_TARGET,
    gcvFEATURE_2D_FC_SOURCE,
    gcvFEATURE_2D_CC_NOAA_SOURCE,
    gcvFEATURE_PE_DITHER_FIX,
    gcvFEATURE_2D_YUV_SEPARATE_STRIDE,
    gcvFEATURE_FRUSTUM_CLIP_FIX,
    gcvFEATURE_TEXTURE_SWIZZLE,
    gcvFEATURE_PRIMITIVE_RESTART,
    gcvFEATURE_TEXTURE_LINEAR,
    gcvFEATURE_TEXTURE_YUV_ASSEMBLER,
    gcvFEATURE_LINEAR_RENDER_TARGET,
    gcvFEATURE_SHADER_HAS_ATOMIC,
    gcvFEATURE_SHADER_HAS_INSTRUCTION_CACHE,
    gcvFEATURE_SHADER_ENHANCEMENTS2,
    gcvFEATURE_BUG_FIXES7,
    gcvFEATURE_SHADER_HAS_RTNE,
    gcvFEATURE_SHADER_HAS_EXTRA_INSTRUCTIONS2,
    gcvFEATURE_SHADER_ENHANCEMENTS3,
    gcvFEATURE_DYNAMIC_FREQUENCY_SCALING,
    gcvFEATURE_SINGLE_BUFFER,
    gcvFEATURE_OCCLUSION_QUERY,
    gcvFEATURE_2D_GAMMA,
    gcvFEATURE_2D_COLOR_SPACE_CONVERSION,
    gcvFEATURE_2D_SUPER_TILE_VERSION,
    gcvFEATURE_HALTI0,
    gcvFEATURE_HALTI1,
    gcvFEATURE_HALTI2,
    gcvFEATURE_SUPPORT_GCREGTX,
    gcvFEATURE_2D_MIRROR_EXTENSION,
    gcvFEATURE_TEXTURE_ASTC,
    gcvFEATURE_TEXTURE_ASTC_DECODE_FIX,
    gcvFEATURE_TEXTURE_ASTC_BASE_LOD_FIX,
    gcvFEATURE_2D_SUPER_TILE_V1,
    gcvFEATURE_2D_SUPER_TILE_V2,
    gcvFEATURE_2D_SUPER_TILE_V3,
    gcvFEATURE_2D_MULTI_SOURCE_BLT_EX2,
    gcvFEATURE_NEW_RA,
    gcvFEATURE_BUG_FIXED_IMPLICIT_PRIMITIVE_RESTART,
    gcvFEATURE_PE_MULTI_RT_BLEND_ENABLE_CONTROL,
    gcvFEATURE_SMALL_MSAA, /* An upgraded version of Fast MSAA */
    gcvFEATURE_VERTEX_INST_ID_AS_ATTRIBUTE,
    gcvFEATURE_DUAL_16,
    gcvFEATURE_BRANCH_ON_IMMEDIATE_REG,
    gcvFEATURE_2D_COMPRESSION,
    gcvFEATURE_TPC_COMPRESSION,
    gcvFEATURE_TPCV11_COMPRESSION,
    gcvFEATURE_DEC_COMPRESSION,
    gcvFEATURE_DEC300_COMPRESSION,
    gcvFEATURE_DEC400_COMPRESSION,
    gcvFEATURE_DEC_TPC_COMPRESSION,
    gcvFEATURE_DEC_COMPRESSION_TILE_NV12_8BIT,
    gcvFEATURE_DEC_COMPRESSION_TILE_NV12_10BIT,
    gcvFEATURE_2D_OPF_YUV_OUTPUT,
    gcvFEATURE_2D_FILTERBLIT_A8_ALPHA,
    gcvFEATURE_2D_MULTI_SRC_BLT_TO_UNIFIED_DST_RECT,
    gcvFEATURE_2D_MULTI_SRC_BLT_BILINEAR_FILTER,
    gcvFEATURE_2D_MULTI_SRC_BLT_1_5_ENHANCEMENT,
    gcvFEATURE_V2_COMPRESSION_Z16_FIX,
    gcvFEATURE_VERTEX_INST_ID_AS_INTEGER,
    gcvFEATURE_2D_YUV_MODE,
    gcvFEATURE_2D_CACHE_128B256BPERLINE,
    gcvFEATURE_2D_SEPARATE_CACHE,
    gcvFEATURE_2D_MAJOR_SUPER_TILE,
    gcvFEATURE_2D_V4COMPRESSION,
    gcvFEATURE_2D_VMSAA,
    gcvFEATURE_2D_10BIT_OUTPUT_LINEAR,
    gcvFEATURE_2D_YUV420_OUTPUT_LINEAR,
    gcvFEATURE_ACE,
    gcvFEATURE_COLOR_COMPRESSION,
    gcvFEATURE_32BPP_COMPONENT_TEXTURE_CHANNEL_SWIZZLE,
    gcvFEATURE_64BPP_HW_CLEAR_SUPPORT,
    gcvFEATURE_TX_LERP_PRECISION_FIX,
    gcvFEATURE_COMPRESSION_V2,
    gcvFEATURE_MMU,
    gcvFEATURE_COMPRESSION_V3,
    gcvFEATURE_TX_DECOMPRESSOR,
    gcvFEATURE_MRT_TILE_STATUS_BUFFER,
    gcvFEATURE_COMPRESSION_V1,
    gcvFEATURE_V1_COMPRESSION_Z16_DECOMPRESS_FIX,
    gcvFEATURE_RTT,
    gcvFEATURE_GENERIC_ATTRIB,
    gcvFEATURE_2D_ONE_PASS_FILTER,
    gcvFEATURE_2D_ONE_PASS_FILTER_TAP,
    gcvFEATURE_2D_POST_FLIP,
    gcvFEATURE_2D_PIXEL_ALIGNMENT,
    gcvFEATURE_CORRECT_AUTO_DISABLE_COUNT,
    gcvFEATURE_CORRECT_AUTO_DISABLE_COUNT_WIDTH,
    gcvFEATURE_8K_RT,
    gcvFEATURE_HALTI3,
    gcvFEATURE_EEZ,
    gcvFEATURE_INTEGER_SIGNEXT_FIX,
    gcvFEATURE_PSOUTPUT_MAPPING,
    gcvFEATURE_8K_RT_FIX,
    gcvFEATURE_TX_TILE_STATUS_MAPPING,
    gcvFEATURE_SRGB_RT_SUPPORT,
    gcvFEATURE_TEXTURE_16K,
    gcvFEATURE_PA_FARZCLIPPING_FIX,
    gcvFEATURE_PE_DITHER_COLORMASK_FIX,
    gcvFEATURE_ZSCALE_FIX,
    gcvFEATURE_MULTI_PIXELPIPES,
    gcvFEATURE_PIPE_CL,
    gcvFEATURE_BUG_FIXES18,
    gcvFEATURE_UNIFIED_SAMPLERS,
    gcvFEATURE_CL_PS_WALKER,
    gcvFEATURE_NEW_HZ,
    gcvFEATURE_TX_FRAC_PRECISION_6BIT,
    gcvFEATURE_SH_INSTRUCTION_PREFETCH,
    gcvFEATURE_PROBE,
    gcvFEATURE_SINGLE_PIPE_HALTI1,
    gcvFEATURE_BUG_FIXES8, /* This HW feature is wrong, we can't use this to check integer branch!!!*/
    gcvFEATURE_2D_ALL_QUAD,
    gcvFEATURE_SEPARATE_SRC_DST,
    gcvFEATURE_TX_HOR_ALIGN_SEL,
    gcvFEATURE_HALTI4,
    gcvFEATURE_MRT_FC_FIX,
    gcvFEATURE_TESSELLATION,
    gcvFEATURE_DRAW_INDIRECT,
    gcvFEATURE_COMPUTE_INDIRECT,
    gcvFEATURE_MSAA_TEXTURE,
    gcvFEATURE_STENCIL_TEXTURE,
    gcvFEATURE_S8_ONLY_RENDERING,
    gcvFEATURE_D24S8_SAMPLE_STENCIL,
    gcvFEATURE_ADVANCED_BLEND_MODE_PART0,
    gcvFEATURE_RA_DEPTH_WRITE,
    gcvFEATURE_RS_DS_DOWNSAMPLE_NATIVE_SUPPORT,
    gcvFEATURE_S8_MSAA_COMPRESSION,
    gcvFEATURE_MSAA_FRAGMENT_OPERATION,
    gcvFEATURE_FE_START_VERTEX_SUPPORT,
    gcvFEATURE_DIVISOR_STREAM_ADDR_FIX,
    gcvFEATURE_ZERO_ATTRIB_SUPPORT,
    gcvFEATURE_DANGLING_VERTEX_FIX,
    gcvFEATURE_PE_DISABLE_COLOR_PIPE,
    gcvFEATURE_FE_12bit_stride,
    gcvFEATURE_TX_LOD_GUARDBAND,
    gcvFEATURE_HAS_PRODUCTID,
    gcvFEATURE_INTEGER32_FIX,
    gcvFEATURE_TEXTURE_GATHER,
    gcvFEATURE_IMG_INSTRUCTION,
    gcvFEATURE_HELPER_INVOCATION,
    gcvFEATURE_NO_USER_CSC,
    gcvFEATURE_ANDROID_ONLY,
    gcvFEATURE_V2_MSAA_COHERENCY_FIX,
    gcvFEATURE_BLOCK_SIZE_16x16,
    gcvFEATURE_TX_SUPPORT_DEC,
    gcvFEATURE_RSBLT_MSAA_DECOMPRESSION,
    gcvFEATURE_TILEFILLER_32TILE_ALIGNED,
    gcvFEATURE_GEOMETRY_SHADER,
    gcvFEATURE_HALTI5,
    gcvFEATURE_PIPELINE_32_ATTRIBUTES,
    gcvFEATURE_USC,
    gcvFEATURE_CUBEMAP_ARRAY,
    gcvFEATURE_TX_DESCRIPTOR,
    gcvFEATURE_SEPARATE_RT_CTRL,
    gcvFEATURE_RENDER_ARRAY,
    gcvFEATURE_BLT_ENGINE,
    gcvFEATURE_SMALLDRAW_BATCH,
    gcvFEATURE_TEXTURE_BUFFER,
    gcvFEATURE_GS_SUPPORT_EMIT,
    gcvFEATURE_SAMPLER_BASE_OFFSET,
    gcvFEATURE_IMAGE_OUT_BOUNDARY_FIX,
    gcvFEATURE_TX_BORDER_CLAMP,
    gcvFEATURE_MSAA_SHADING,
    gcvFEATURE_ADVANCED_SH_INST,
    gcvFEATURE_LOD_FIX_FOR_BASELEVEL,
    gcvFEATURE_MULTIDRAW_INDIRECT,
    gcvFEATURE_DRAW_ELEMENTS_BASE_VERTEX,
    gcvFEATURE_NEW_STEERING_AND_ICACHE_FLUSH, /* Steering base on register base. Trigger-style Icache flush state. */
    gcvFEATURE_PE_DITHER_FIX2,
    gcvFEATURE_INDEX_FETCH_FIX,
    gcvFEATURE_TEX_BASELOD,
    gcvFEATURE_TEX_SEAMLESS_CUBE,
    gcvFEATURE_TEX_ETC2,
    gcvFEATURE_TEX_CUBE_BORDER_LOD,
    gcvFEATURE_FE_ALLOW_STALL_PREFETCH_ENG,
    gcvFEATURE_TX_8BPP_TS_FIX,
    gcvFEATURE_HW_TFB,
    gcvFEATURE_COMPRESSION_V4,
    gcvFEATURE_FENCE,
    gcvFEATURE_R8_UNORM,
    gcvFEATURE_TX_DEFAULT_VALUE_FIX,
    gcvFEATURE_TX_8bit_UVFrac,
    gcvFEATURE_TX_MIPFILTER_NONE_FIX,
    gcvFEATURE_MC_STENCIL_CTRL,
    gcvFEATURE_DEPTH_MATH_FIX,
    gcvFEATURE_PE_B2B_PIXEL_FIX,
    gcvFEATURE_TEXTURE_GATHER_OFFSETS,
    gcvFEATURE_TEX_CACHE_FLUSH_FIX,
    gcvFEATURE_WIDELINE_HELPER_FIX,
    gcvFEATURE_LINE_DIAMOND_RULE_FIX,
    gcvFEATURE_MULTIGPU_SYNC_V2,
    gcvFEATURE_DRAW_ID,
    gcvFEATURE_SNAPPAGE_CMD,
    gcvFEATURE_COMMAND_PREFETCH,
    gcvFEATURE_SAMPLEPOS_SWIZZLE_FIX,
    gcvFEATURE_SELECTMAP_SRC0_SWIZZLE_FIX,
    gcvFEATURE_LOADATTR_OOB_FIX,
    gcvFEATURE_RA_DEPTH_WRITE_MSAA1X_FIX,
    gcvFEATURE_MRT_8BIT_DUAL_PIPE_FIX,
    gcvFEATURE_BUG_FIXES1,
    gcvFEATURE_MULTI_SOURCE_BLT,
    gcvFEATURE_ZCOMPRESSION,
    gcvFEATURE_DITHER_AND_FILTER_PLUS_ALPHA_2D,
    gcvFEATURE_ONE_PASS_2D_FILTER,
    gcvFEATURE_TX_FILTER,
    gcvFEATURE_CHIPENABLE_LINK,
    gcvFEATURE_TEXTURE_BIAS_LOD_FIX,
    gcvFEATURE_USE_GL_Z,
    gcvFEATURE_SUPPORT_INTEGER,
    /* PARTLY_SUPPORT_INTEGER_BRANCH:
    **      chips can support all integer types for compare instructions, e.g, CMP, SELECT.
    ** FULLLY_SUPPORT_INTEGER_BRANCH:
    **      chips can support all integer types for JMP instruction.
    ** If PARTLY_SUPPORT_INTEGER_BRANCH is TRUE but FULLLY_SUPPORT_INTEGER_BRANCH is FALSE,
    ** then this chip can only support INT32/UINT32 JMP instruction.
    */
    gcvFEATURE_PARTLY_SUPPORT_INTEGER_BRANCH,
    gcvFEATURE_FULLLY_SUPPORT_INTEGER_BRANCH,
    gcvFEATURE_SUPPORT_INTEGER_ATTRIBUTE,
    gcvFEATURE_SUPPORT_MOVAI,
    gcvFEATURE_NEED_FIX_FOR_CL_X,
    gcvFEATURE_NEED_FIX_FOR_CL_XE,
    gcvFEATURE_HAS_OUTPUT_COUNT_FIX,
    gcvFEATURE_VARYING_PACKING_LIMITATION,
    gcvFEATURE_HIGHP_VARYING_SHIFT,
    gcvFEATURE_BUG_FIXES2,
    gcvFEATURE_64K_L2_CACHE,
    gcvFEATURE_128BTILE,
    gcvFEATURE_ADVANCED_BLEND_OPT,
    gcvFEATURE_SNAPPAGE_CMD_FIX,
    gcvFEATURE_L2_CACHE_FOR_2D_420,
    gcvFEATURE_TILE_STATUS_2BITS,
    gcvFEATURE_EXTRA_SHADER_INSTRUCTIONS0,
    gcvFEATURE_EXTRA_SHADER_INSTRUCTIONS1,
    gcvFEATURE_EXTRA_SHADER_INSTRUCTIONS2,
    gcvFEATURE_MEDIUM_PRECISION,
    gcvFEATURE_FE20_BIT_INDEX,
    gcvFEATURE_BUG_FIXES4,
    gcvFEATURE_BUG_FIXES12,
    gcvFEATURE_VMSAA,
    gcvFEATURE_ROBUST_ATOMIC,
    gcvFEATURE_32F_COLORMASK_FIX,
    gcvFEATURE_NEW_GPIPE,
    gcvFEATURE_RS_NEW_BASEADDR,
    gcvFEATURE_TX_DXT,
    gcvFEATURE_SH_FLAT_INTERPOLATION_DUAL16_FIX,
    gcvFEATURE_EVIS,
    gcvFEATURE_SH_SUPPORT_V4,
    gcvFEATURE_SH_SUPPORT_ALPHA_KILL,
    gcvFEATURE_PE_NO_ALPHA_TEST,
    gcvFEATURE_SH_SNAP2PAGE_MAXPAGES_FIX,
    gcvFEATURE_USC_FULLCACHE_FIX,
    gcvFEATURE_PE_64bit_FENCE_FIX,
    gcvFEATURE_BLT_8bit_256TILE_FC_FIX,
    gcvFEATURE_PE_RGBA16I_FIX,
    gcvFEATURE_BLT_64bpp_MASKED_CLEAR_FIX,
    gcvFEATURE_SH_PSO_MSAA1x_FIX,
    gcvFEATURE_USC_ATOMIC_FIX,
    gcvFEATURE_SH_NO_INDEX_CONST_ON_A0,
    gcvFEATURE_SH_NO_ONECONST_LIMIT,
    gcvFEATURE_EVIS_NO_ABSDIFF,
    gcvFEATURE_EVIS_NO_BITREPLACE,
    gcvFEATURE_EVIS_NO_BOXFILTER,
    gcvFEATURE_EVIS_NO_CORDIAC,
    gcvFEATURE_EVIS_NO_DP32,
    gcvFEATURE_EVIS_NO_FILTER,
    gcvFEATURE_EVIS_NO_IADD,
    gcvFEATURE_EVIS_NO_SELECTADD,
    gcvFEATURE_EVIS_LERP_7OUTPUT,
    gcvFEATURE_EVIS_ACCSQ_8OUTPUT,
    gcvFEATURE_ROBUSTNESS,
    gcvFEATURE_SECURITY,
    gcvFEATURE_TX_YUV_ASSEMBLER_10BIT,
    gcvFEATURE_USC_GOS_ADDR_FIX,
    gcvFEATURE_SUPPORT_MSAA2X,
    gcvFEATURE_TX_DESC_CACHE_CLOCKGATE_FIX,
    gcvFEATURE_TX_INTEGER_COORDINATE,
    gcvFEATURE_PSIO_SAMPLEMASK_IN_R0ZW_FIX,
    gcvFEATURE_MULTI_CORE_BLOCK_SET_CONFIG,
    gcvFEATURE_SH_IMG_LDST_ON_TEMP,
    gcvFEATURE_TX_INTEGER_COORDINATE_V2,
    gcvFEATURE_COMPUTE_ONLY,
    gcvFEATURE_SH_IMG_LDST_CLAMP,
    gcvFEATURE_SH_ICACHE_ALLOC_COUNT_FIX,

    gcvFEATURE_MSAA_OQ_FIX,

    gcvFEATURE_PE_ENHANCEMENTS2,
    gcvFEATURE_PSIO_MSAA_CL_FIX,
    gcvFEATURE_FE_NEED_DUMMYDRAW,
    gcvFEATURE_TX_ASTC_MULTISLICE_FIX,
    gcvFEATURE_PSIO_DUAL16_32bpc_FIX,
    gcvFEATURE_USC_DEFER_FILL_FIX,
    /* Insert features above this comment only. */
    gcvFEATURE_COUNT                /* Not a feature. */
}
gceFEATURE;

/* dummy draw type.*/
typedef enum _gceDUMMY_DRAW_TYPE
{
    gcvDUMMY_DRAW_INVALID = 0,
    gcvDUMMY_DRAW_GC400,
    gcvDUMMY_DRAW_V60,
}
gceDUMMY_DRAW_TYPE;

/* Chip SWWA. */
typedef enum _gceSWWA
{
    gcvSWWA_601 = 0,
    gcvSWWA_706,
    gcvSWWA_1163,
    gcvSWWA_1165,
    /* Insert SWWA above this comment only. */
    gcvSWWA_COUNT                   /* Not a SWWA. */
}
gceSWWA;


/* Option Set*/
typedef enum _gceOPTION
{
    /* HW setting. */
    gcvOPTION_PREFER_ZCONVERT_BYPASS = 0,
    gcvOPTION_PREFER_TILED_DISPLAY_BUFFER = 1,
    gcvOPTION_PREFER_GUARDBAND = 2,
    gcvOPTION_PREFER_TPG_TRIVIALMODEL = 3,
    gcvOPTION_PREFER_RA_DEPTH_WRITE = 4,
    gcvOPTION_PREFER_USC_RECONFIG = 5,

    /* SW options */
    gcvOPTION_HW_NULL = 50,
    gcvOPTION_PRINT_OPTION = 51,
    gcvOPTION_KERNEL_FENCE = 52,
    gcvOPTION_ASYNC_BLT = 53,
    gcvOPTION_FBO_PREFER_MEM = 54,
    gcvOPTION_GPU_TEX_UPLOAD = 55,

    /* Insert option above this comment only */
    gcvOPTION_COUNT                     /* Not a OPTION*/
}
gceOPTION;

typedef enum _gceFRAMEINFO
{
    /* Total frame count in one run */
    gcvFRAMEINFO_FRAME_NUM       = 0,
    /* Total draw count in current frame, including draw/compute */
    gcvFRAMEINFO_DRAW_NUM        = 1,
    /* Total compute count in current frame, subset of drawNum */
    gcvFRAMEINFO_COMPUTE_NUM     = 2,
    /* Total dual16 draw/compute count in current frame, subset of drawNum */
    gcvFRAMEINFO_DUAL16_NUM      = 3,
    /* Current programID is being set. only valid for ES20 driver right now */
    gcvFRAMEINFO_PROGRAM_ID     = 4,

    gcvFRAMEINFO_COUNT,
}
gceFRAMEINFO;

typedef enum _gceFRAMEINFO_OP
{
    gcvFRAMEINFO_OP_INC       = 0,
    gcvFRAMEINFO_OP_DEC       = 1,
    gcvFRAMEINFO_OP_ZERO      = 2,
    gcvFRAMEINFO_OP_GET       = 3,
    gcvFRAMEINFO_OP_SET       = 4,
    gcvFRAMEINFO_OP_COUNT,
}
gceFRAMEINFO_OP;


/* Chip Power Status. */
typedef enum _gceCHIPPOWERSTATE
{
    gcvPOWER_ON = 0,
    gcvPOWER_OFF,
    gcvPOWER_IDLE,
    gcvPOWER_SUSPEND,
    gcvPOWER_SUSPEND_ATPOWERON,
    gcvPOWER_OFF_ATPOWERON,
    gcvPOWER_IDLE_BROADCAST,
    gcvPOWER_SUSPEND_BROADCAST,
    gcvPOWER_OFF_BROADCAST,
    gcvPOWER_OFF_RECOVERY,
    gcvPOWER_OFF_TIMEOUT,
    gcvPOWER_ON_AUTO
}
gceCHIPPOWERSTATE;

/* CPU cache operations */
typedef enum _gceCACHEOPERATION
{
    gcvCACHE_CLEAN      = 0x01,     /* Flush CPU cache to mem */
    gcvCACHE_INVALIDATE = 0x02,     /* Invalidte CPU cache */
    gcvCACHE_FLUSH      = gcvCACHE_CLEAN  | gcvCACHE_INVALIDATE,    /* Both flush & invalidate */
    gcvCACHE_MEMORY_BARRIER = 0x04
}
gceCACHEOPERATION;

/* Surface types. */
typedef enum _gceSURF_TYPE
{
    gcvSURF_TYPE_UNKNOWN = 0,
    gcvSURF_INDEX,
    gcvSURF_VERTEX,
    gcvSURF_TEXTURE,
    gcvSURF_RENDER_TARGET,
    gcvSURF_DEPTH,
    gcvSURF_BITMAP,
    gcvSURF_TILE_STATUS,
    gcvSURF_IMAGE,
    gcvSURF_MASK,
    gcvSURF_SCISSOR,
    gcvSURF_HIERARCHICAL_DEPTH,
    gcvSURF_ICACHE,
    gcvSURF_TXDESC,
    gcvSURF_FENCE,
    gcvSURF_TFBHEADER,
    gcvSURF_NUM_TYPES, /* Make sure this is the last one! */

    /* Combinations. */
    gcvSURF_NO_TILE_STATUS = 0x100,
    gcvSURF_NO_VIDMEM      = 0x200, /* Used to allocate surfaces with no underlying vidmem node.
                                       In Android, vidmem node is allocated by another process. */
    gcvSURF_CACHEABLE      = 0x400, /* Used to allocate a cacheable surface */

    gcvSURF_TILE_RLV_FENCE = 0x800, /* create texture fence as tile */

    gcvSURF_TILE_STATUS_DIRTY  = 0x1000, /* Init tile status to all dirty */

    gcvSURF_LINEAR             = 0x2000,

    gcvSURF_CREATE_AS_TEXTURE  = 0x4000,  /* create it as a texture */

    gcvSURF_PROTECTED_CONTENT  = 0x8000,  /* create it as content protected */

    gcvSURF_CREATE_AS_DISPLAYBUFFER = 0x10000, /*create it as a display buffer surface */

    gcvSURF_CONTIGUOUS         = 0x20000,      /*create it as contiguous */

    /* Create it as no compression, valid on when it has tile status. */
    gcvSURF_NO_COMPRESSION     = 0x40000,

    gcvSURF_DEC                = 0x80000,  /* Surface is DEC compressed */

    gcvSURF_NO_HZ              = 0x100000,

    gcvSURF_TEXTURE_LINEAR               = gcvSURF_TEXTURE
                                         | gcvSURF_LINEAR,

    gcvSURF_RENDER_TARGET_LINEAR         = gcvSURF_RENDER_TARGET
                                         | gcvSURF_LINEAR,

    gcvSURF_RENDER_TARGET_NO_TILE_STATUS = gcvSURF_RENDER_TARGET
                                         | gcvSURF_NO_TILE_STATUS,

    gcvSURF_RENDER_TARGET_NO_COMPRESSION = gcvSURF_RENDER_TARGET
                                         | gcvSURF_NO_COMPRESSION,

    gcvSURF_RENDER_TARGET_TS_DIRTY = gcvSURF_RENDER_TARGET
                                         | gcvSURF_TILE_STATUS_DIRTY,

    gcvSURF_DEPTH_NO_TILE_STATUS         = gcvSURF_DEPTH
                                         | gcvSURF_NO_TILE_STATUS,

    gcvSURF_DEPTH_TS_DIRTY               = gcvSURF_DEPTH
                                         | gcvSURF_TILE_STATUS_DIRTY,

    /* Supported surface types with no vidmem node. */
    gcvSURF_BITMAP_NO_VIDMEM             = gcvSURF_BITMAP
                                         | gcvSURF_NO_VIDMEM,

    gcvSURF_TEXTURE_NO_VIDMEM            = gcvSURF_TEXTURE
                                         | gcvSURF_NO_VIDMEM,

    /* Cacheable surface types with no vidmem node. */
    gcvSURF_CACHEABLE_BITMAP_NO_VIDMEM   = gcvSURF_BITMAP_NO_VIDMEM
                                         | gcvSURF_CACHEABLE,

    gcvSURF_CACHEABLE_BITMAP             = gcvSURF_BITMAP
                                         | gcvSURF_CACHEABLE,
}
gceSURF_TYPE;

typedef enum _gceSURF_USAGE
{
    gcvSURF_USAGE_UNKNOWN,
    gcvSURF_USAGE_RESOLVE_AFTER_CPU,
    gcvSURF_USAGE_RESOLVE_AFTER_3D
}
gceSURF_USAGE;

typedef enum _gceSURF_COLOR_SPACE
{
    gcvSURF_COLOR_SPACE_UNKNOWN,
    gcvSURF_COLOR_SPACE_LINEAR,
    gcvSURF_COLOR_SPACE_NONLINEAR,
}
gceSURF_COLOR_SPACE;

typedef enum _gceSURF_COLOR_TYPE
{
    gcvSURF_COLOR_UNKNOWN = 0,
    gcvSURF_COLOR_LINEAR        = 0x01,
    gcvSURF_COLOR_ALPHA_PRE     = 0x02,
}
gceSURF_COLOR_TYPE;

/* Rotation. */
typedef enum _gceSURF_ROTATION
{
    gcvSURF_0_DEGREE = 0,
    gcvSURF_90_DEGREE,
    gcvSURF_180_DEGREE,
    gcvSURF_270_DEGREE,
    gcvSURF_FLIP_X,
    gcvSURF_FLIP_Y,

    gcvSURF_POST_FLIP_X = 0x40000000,
    gcvSURF_POST_FLIP_Y = 0x80000000,
}
gceSURF_ROTATION;

/* Surface flag */
typedef enum _gceSURF_FLAG
{
    /* None flag */
    gcvSURF_FLAG_NONE                = 0x0,
    /* content is preserved after swap */
    gcvSURF_FLAG_CONTENT_PRESERVED   = 0x1,
    /* content is updated after swap*/
    gcvSURF_FLAG_CONTENT_UPDATED     = 0x2,
    /* content is y inverted */
    gcvSURF_FLAG_CONTENT_YINVERTED   = 0x4,
    /* surface has multiple nodes */
    gcvSURF_FLAG_MULTI_NODE          = 0x8,
}
gceSURF_FLAG;

typedef enum _gceMIPMAP_IMAGE_FORMAT
{
    gcvUNKNOWN_MIPMAP_IMAGE_FORMAT  = -2
}
gceMIPMAP_IMAGE_FORMAT;

/* Surface formats.
** Name rules is from MSB->LSB.
*/
typedef enum _gceSURF_FORMAT
{
    /* Unknown format. */
    gcvSURF_UNKNOWN             = 0,

    /* Palettized formats. */
    gcvSURF_INDEX1              = 100,
    gcvSURF_INDEX4,
    gcvSURF_INDEX8,
#if gcdVG_ONLY
    gcvSURF_INDEX2,
#endif

    /* RGB formats. */
    gcvSURF_A2R2G2B2            = 200,
    gcvSURF_R3G3B2,
    gcvSURF_A8R3G3B2,
    gcvSURF_X4R4G4B4,
    gcvSURF_A4R4G4B4,
    gcvSURF_R4G4B4A4,
    gcvSURF_X1R5G5B5,
    gcvSURF_A1R5G5B5,
    gcvSURF_R5G5B5A1,
    gcvSURF_R5G6B5,
    gcvSURF_R8G8B8,
    gcvSURF_X8R8G8B8,
    gcvSURF_A8R8G8B8,
    gcvSURF_R8G8B8A8,
    gcvSURF_G8R8G8B8,
    gcvSURF_R8G8B8G8,
    gcvSURF_X2R10G10B10,
    gcvSURF_A2R10G10B10,
    gcvSURF_R10G10B10A2,
    gcvSURF_X12R12G12B12,
    gcvSURF_A12R12G12B12,
    gcvSURF_X16R16G16B16,
    gcvSURF_A16R16G16B16,
    gcvSURF_A32R32G32B32,
    gcvSURF_R8G8B8X8,
    gcvSURF_R5G5B5X1,
    gcvSURF_R4G4B4X4,
    gcvSURF_X16R16G16B16_2_A8R8G8B8,
    gcvSURF_A16R16G16B16_2_A8R8G8B8,
    gcvSURF_A32R32G32B32_2_G32R32F,
    gcvSURF_A32R32G32B32_4_A8R8G8B8,
    /* BGR formats. */
    gcvSURF_A4B4G4R4            = 300,
    gcvSURF_A1B5G5R5,
    gcvSURF_B5G6R5,
    gcvSURF_B8G8R8,
    gcvSURF_B16G16R16,
    gcvSURF_X8B8G8R8,
    gcvSURF_A8B8G8R8,
    gcvSURF_A2B10G10R10,
    gcvSURF_X16B16G16R16,
    gcvSURF_A16B16G16R16,
    gcvSURF_B32G32R32,
    gcvSURF_X32B32G32R32,
    gcvSURF_A32B32G32R32,
    gcvSURF_B4G4R4A4,
    gcvSURF_B5G5R5A1,
    gcvSURF_B8G8R8X8,
    gcvSURF_B8G8R8A8,
    gcvSURF_B10G10R10A2,
    gcvSURF_X4B4G4R4,
    gcvSURF_X1B5G5R5,
    gcvSURF_B4G4R4X4,
    gcvSURF_B5G5R5X1,
    gcvSURF_X2B10G10R10,
    gcvSURF_B8G8R8_SNORM,
    gcvSURF_X8B8G8R8_SNORM,
    gcvSURF_A8B8G8R8_SNORM,
    gcvSURF_A8B12G12R12_2_A8R8G8B8,

    /* Compressed formats. */
    gcvSURF_DXT1                = 400,
    gcvSURF_DXT2,
    gcvSURF_DXT3,
    gcvSURF_DXT4,
    gcvSURF_DXT5,
    gcvSURF_CXV8U8,
    gcvSURF_ETC1,
    gcvSURF_R11_EAC,
    gcvSURF_SIGNED_R11_EAC,
    gcvSURF_RG11_EAC,
    gcvSURF_SIGNED_RG11_EAC,
    gcvSURF_RGB8_ETC2,
    gcvSURF_SRGB8_ETC2,
    gcvSURF_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    gcvSURF_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    gcvSURF_RGBA8_ETC2_EAC,
    gcvSURF_SRGB8_ALPHA8_ETC2_EAC,

    /* YUV formats. */
    gcvSURF_YUY2                = 500,
    gcvSURF_UYVY,
    gcvSURF_YV12,
    gcvSURF_I420,
    gcvSURF_NV12,
    gcvSURF_NV21,
    gcvSURF_NV16,
    gcvSURF_NV61,
    gcvSURF_YVYU,
    gcvSURF_VYUY,
    gcvSURF_AYUV,
    gcvSURF_YUV420_10_ST,
    gcvSURF_YUV420_TILE_ST,
    gcvSURF_YUV420_TILE_10_ST,
    gcvSURF_NV12_10BIT,
    gcvSURF_NV21_10BIT,
    gcvSURF_NV16_10BIT,
    gcvSURF_NV61_10BIT,
    gcvSURF_P010,
#if gcdVG_ONLY
    gcvSURF_AYUY2,
    gcvSURF_ANV12,
    gcvSURF_ANV16,
#endif

    /* Depth formats. */
    gcvSURF_D16                 = 600,
    gcvSURF_D24S8,
    gcvSURF_D32,
    gcvSURF_D24X8,
    gcvSURF_D32F,
    gcvSURF_S8D32F,
    gcvSURF_S8D32F_1_G32R32F,
    gcvSURF_S8D32F_2_A8R8G8B8,
    gcvSURF_D24S8_1_A8R8G8B8,
    gcvSURF_S8,
    gcvSURF_X24S8,
    gcvSURF_X24S8_1_A8R8G8B8,

    /* Alpha formats. */
    gcvSURF_A4                  = 700,
    gcvSURF_A8,
    gcvSURF_A12,
    gcvSURF_A16,
    gcvSURF_A32,
    gcvSURF_A1,

    /* Luminance formats. */
    gcvSURF_L4                  = 800,
    gcvSURF_L8,
    gcvSURF_L12,
    gcvSURF_L16,
    gcvSURF_L32,
    gcvSURF_L1,

    /* Alpha/Luminance formats. */
    gcvSURF_A4L4                = 900,
    gcvSURF_A2L6,
    gcvSURF_A8L8,
    gcvSURF_A4L12,
    gcvSURF_A12L12,
    gcvSURF_A16L16,

    /* Bump formats. */
    gcvSURF_L6V5U5              = 1000,
    gcvSURF_V8U8,
    gcvSURF_X8L8V8U8,
    gcvSURF_Q8W8V8U8,
    gcvSURF_A2W10V10U10,
    gcvSURF_V16U16,
    gcvSURF_Q16W16V16U16,

    /* R/RG/RA formats. */
    gcvSURF_R8                  = 1100,
    gcvSURF_X8R8,
    gcvSURF_G8R8,
    gcvSURF_X8G8R8,
    gcvSURF_A8R8,
    gcvSURF_R16,
    gcvSURF_X16R16,
    gcvSURF_G16R16,
    gcvSURF_X16G16R16,
    gcvSURF_A16R16,
    gcvSURF_R32,
    gcvSURF_X32R32,
    gcvSURF_G32R32,
    gcvSURF_X32G32R32,
    gcvSURF_A32R32,
    gcvSURF_RG16,
    gcvSURF_R8_SNORM,
    gcvSURF_G8R8_SNORM,

    gcvSURF_R8_1_X8R8G8B8,
    gcvSURF_G8R8_1_X8R8G8B8,

    /* Floating point formats. */
    gcvSURF_R16F                = 1200,
    gcvSURF_X16R16F,
    gcvSURF_G16R16F,
    gcvSURF_X16G16R16F,
    gcvSURF_B16G16R16F,
    gcvSURF_X16B16G16R16F,
    gcvSURF_A16B16G16R16F,
    gcvSURF_R32F,
    gcvSURF_X32R32F,
    gcvSURF_G32R32F,
    gcvSURF_X32G32R32F,
    gcvSURF_B32G32R32F,
    gcvSURF_X32B32G32R32F,
    gcvSURF_A32B32G32R32F,
    gcvSURF_A16F,
    gcvSURF_L16F,
    gcvSURF_A16L16F,
    gcvSURF_A16R16F,
    gcvSURF_A32F,
    gcvSURF_L32F,
    gcvSURF_A32L32F,
    gcvSURF_A32R32F,
    gcvSURF_E5B9G9R9,
    gcvSURF_B10G11R11F,

    gcvSURF_X16B16G16R16F_2_A8R8G8B8,
    gcvSURF_A16B16G16R16F_2_A8R8G8B8,
    gcvSURF_A16B16G16R16F_2_G16R16F,
    gcvSURF_G32R32F_2_A8R8G8B8,
    gcvSURF_X32B32G32R32F_2_G32R32F,
    gcvSURF_A32B32G32R32F_2_G32R32F,
    gcvSURF_X32B32G32R32F_4_A8R8G8B8,
    gcvSURF_A32B32G32R32F_4_A8R8G8B8,

    gcvSURF_R16F_1_A4R4G4B4,
    gcvSURF_G16R16F_1_A8R8G8B8,
    gcvSURF_B16G16R16F_2_A8R8G8B8,

    gcvSURF_R32F_1_A8R8G8B8,
    gcvSURF_B32G32R32F_3_A8R8G8B8,
    gcvSURF_B10G11R11F_1_A8R8G8B8,

    gcvSURF_A32F_1_R32F,
    gcvSURF_L32F_1_R32F,
    gcvSURF_A32L32F_1_G32R32F,



    /* sRGB format. */
    gcvSURF_SBGR8               = 1400,
    gcvSURF_A8_SBGR8,
    gcvSURF_X8_SBGR8,
    gcvSURF_A8_SRGB8,
    gcvSURF_X8_SRGB8,

    /* Integer formats. */
    gcvSURF_R8I                 = 1500,
    gcvSURF_R8UI,
    gcvSURF_R16I,
    gcvSURF_R16UI,
    gcvSURF_R32I,
    gcvSURF_R32UI,
    gcvSURF_X8R8I,
    gcvSURF_G8R8I,
    gcvSURF_X8R8UI,
    gcvSURF_G8R8UI,
    gcvSURF_X16R16I,
    gcvSURF_G16R16I,
    gcvSURF_X16R16UI,
    gcvSURF_G16R16UI,
    gcvSURF_X32R32I,
    gcvSURF_G32R32I,
    gcvSURF_X32R32UI,
    gcvSURF_G32R32UI,
    gcvSURF_X8G8R8I,
    gcvSURF_B8G8R8I,
    gcvSURF_X8G8R8UI,
    gcvSURF_B8G8R8UI,
    gcvSURF_X16G16R16I,
    gcvSURF_B16G16R16I,
    gcvSURF_X16G16R16UI,
    gcvSURF_B16G16R16UI,
    gcvSURF_X32G32R32I,
    gcvSURF_B32G32R32I,
    gcvSURF_X32G32R32UI,
    gcvSURF_B32G32R32UI,
    gcvSURF_X8B8G8R8I,
    gcvSURF_A8B8G8R8I,
    gcvSURF_X8B8G8R8UI,
    gcvSURF_A8B8G8R8UI,
    gcvSURF_X16B16G16R16I,
    gcvSURF_A16B16G16R16I,
    gcvSURF_X16B16G16R16UI,
    gcvSURF_A16B16G16R16UI,
    gcvSURF_X32B32G32R32I,
    gcvSURF_A32B32G32R32I,
    gcvSURF_X32B32G32R32UI,
    gcvSURF_A32B32G32R32UI,
    gcvSURF_A2B10G10R10UI,
    gcvSURF_G32R32I_2_A8R8G8B8,
    gcvSURF_G32R32I_1_G32R32F,
    gcvSURF_G32R32UI_2_A8R8G8B8,
    gcvSURF_G32R32UI_1_G32R32F,
    gcvSURF_X16B16G16R16I_2_A8R8G8B8,
    gcvSURF_X16B16G16R16I_1_G32R32F,
    gcvSURF_A16B16G16R16I_2_A8R8G8B8,
    gcvSURF_A16B16G16R16I_1_G32R32F,
    gcvSURF_X16B16G16R16UI_2_A8R8G8B8,
    gcvSURF_X16B16G16R16UI_1_G32R32F,
    gcvSURF_A16B16G16R16UI_2_A8R8G8B8,
    gcvSURF_A16B16G16R16UI_1_G32R32F,
    gcvSURF_X32B32G32R32I_2_G32R32I,
    gcvSURF_A32B32G32R32I_2_G32R32I,
    gcvSURF_A32B32G32R32I_2_G32R32F,
    gcvSURF_X32B32G32R32I_3_A8R8G8B8,
    gcvSURF_A32B32G32R32I_4_A8R8G8B8,
    gcvSURF_X32B32G32R32UI_2_G32R32UI,
    gcvSURF_A32B32G32R32UI_2_G32R32UI,
    gcvSURF_A32B32G32R32UI_2_G32R32F,
    gcvSURF_X32B32G32R32UI_3_A8R8G8B8,
    gcvSURF_A32B32G32R32UI_4_A8R8G8B8,
    gcvSURF_A2B10G10R10UI_1_A8R8G8B8,
    gcvSURF_A8B8G8R8I_1_A8R8G8B8,
    gcvSURF_A8B8G8R8UI_1_A8R8G8B8,
    gcvSURF_R8I_1_A4R4G4B4,
    gcvSURF_R8UI_1_A4R4G4B4,
    gcvSURF_R16I_1_A4R4G4B4,
    gcvSURF_R16UI_1_A4R4G4B4,
    gcvSURF_R32I_1_A8R8G8B8,
    gcvSURF_R32UI_1_A8R8G8B8,
    gcvSURF_X8R8I_1_A4R4G4B4,
    gcvSURF_X8R8UI_1_A4R4G4B4,
    gcvSURF_G8R8I_1_A4R4G4B4,
    gcvSURF_G8R8UI_1_A4R4G4B4,
    gcvSURF_X16R16I_1_A4R4G4B4,
    gcvSURF_X16R16UI_1_A4R4G4B4,
    gcvSURF_G16R16I_1_A8R8G8B8,
    gcvSURF_G16R16UI_1_A8R8G8B8,
    gcvSURF_X32R32I_1_A8R8G8B8,
    gcvSURF_X32R32UI_1_A8R8G8B8,
    gcvSURF_X8G8R8I_1_A4R4G4B4,
    gcvSURF_X8G8R8UI_1_A4R4G4B4,
    gcvSURF_B8G8R8I_1_A8R8G8B8,
    gcvSURF_B8G8R8UI_1_A8R8G8B8,
    gcvSURF_B16G16R16I_2_A8R8G8B8,
    gcvSURF_B16G16R16I_1_G32R32F,
    gcvSURF_B16G16R16UI_2_A8R8G8B8,
    gcvSURF_B16G16R16UI_1_G32R32F,
    gcvSURF_B32G32R32I_3_A8R8G8B8,
    gcvSURF_B32G32R32UI_3_A8R8G8B8,
    gcvSURF_A16B16G16R16_2_A8R8G8B8,
    gcvSURF_R8G8B8_1_A8R8G8B8,
    gcvSURF_G16R16_1_A8R8G8B8,
    gcvSURF_A2B10G10R10_1_A8R8G8B8,
    gcvSURF_A2R10G10B10_1_A8R8G8B8,
    gcvSURF_A2W10V10U10_1_A8R8G8B8,

    /* ASTC formats. */
    gcvSURF_ASTC4x4             = 1600,
    gcvSURF_ASTC5x4,
    gcvSURF_ASTC5x5,
    gcvSURF_ASTC6x5,
    gcvSURF_ASTC6x6,
    gcvSURF_ASTC8x5,
    gcvSURF_ASTC8x6,
    gcvSURF_ASTC8x8,
    gcvSURF_ASTC10x5,
    gcvSURF_ASTC10x6,
    gcvSURF_ASTC10x8,
    gcvSURF_ASTC10x10,
    gcvSURF_ASTC12x10,
    gcvSURF_ASTC12x12,
    gcvSURF_ASTC4x4_SRGB,
    gcvSURF_ASTC5x4_SRGB,
    gcvSURF_ASTC5x5_SRGB,
    gcvSURF_ASTC6x5_SRGB,
    gcvSURF_ASTC6x6_SRGB,
    gcvSURF_ASTC8x5_SRGB,
    gcvSURF_ASTC8x6_SRGB,
    gcvSURF_ASTC8x8_SRGB,
    gcvSURF_ASTC10x5_SRGB,
    gcvSURF_ASTC10x6_SRGB,
    gcvSURF_ASTC10x8_SRGB,
    gcvSURF_ASTC10x10_SRGB,
    gcvSURF_ASTC12x10_SRGB,
    gcvSURF_ASTC12x12_SRGB,

    /* Recompile format*/
    gcvSURF_L16_1_A4R4G4B4 = 1700,
    gcvSURF_V16U16_1_A8R8G8B8,
    gcvSURF_Q8W8V8U8_1_A8R8G8B8,
    gcvSURF_X8L8V8U8_1_A8R8G8B8,
    gcvSURF_R3G3B2_1_A8R8G8B8,
    gcvSURF_A8R3G3B2_1_A8R8G8B8,
    gcvSURF_W11V11U10_1_A8R8G8B8,
    gcvSURF_Q16W16V16U16_2_A8R8G8B8,
    gcvSURF_W11V11U10,
    gcvSURF_V8U8_1_A4R4G4B4,
    gcvSURF_A8B8G8R8_1_A8R8G8B8,
    gcvSURF_A32R32G32B32_1_A8R8G8B8,
    gcvSURF_X16B16G16R16F_1_A8R8G8B8,
    gcvSURF_A16B16G16R16F_1_A8R8G8B8,
    gcvSURF_G32R32F_1_A8R8G8B8,
    gcvSURF_X32B32G32R32F_1_A8R8G8B8,
    gcvSURF_A32B32G32R32F_1_A8R8G8B8,
    gcvSURF_G32R32I_1_A8R8G8B8,
    gcvSURF_G32R32UI_1_A8R8G8B8,
    gcvSURF_A32B32G32R32I_1_A8R8G8B8,
    gcvSURF_A32B32G32R32UI_1_A8R8G8B8,
    gcvSURF_Q16W16V16U16_1_A8R8G8B8,
    gcvSURF_A16B16G16R16_1_A8R8G8B8,
    gcvSURF_FORMAT_COUNT
}
gceSURF_FORMAT;

typedef enum _gceSURF_YUV_COLOR_SPACE
{
    gcvSURF_ITU_REC601,
    gcvSURF_ITU_REC709,
    gcvSURF_ITU_REC2020,
}
gceSURF_YUV_COLOR_SPACE;

typedef enum _gceSURF_YUV_SAMPLE_RANGE
{
    gcvSURF_YUV_FULL_RANGE,
    gcvSURF_YUV_NARROW_RANGE,
}
gceSURF_YUV_SAMPLE_RANGE;

typedef enum _gceSURF_YUV_CHROMA_SITING
{
    gcvSURF_YUV_CHROMA_SITING_0,
    gcvSURF_YUV_CHROMA_SITING_0_5,
}
gceSURF_YUV_CHROMA_SITING;

typedef enum _gceSURF_INFO_TYPE
{
    gcvSURF_INFO_UNKNOWN   = 0,
    gcvSURF_INFO_LAYERSIZE = 1,
    gcvSURF_INFO_SLICESIZE = 2,
}
gceSURF_INFO_TYPE;

/* Format modifiers. */
typedef enum _gceSURF_FORMAT_MODE
{
    gcvSURF_FORMAT_OCL = 0x80000000
}
gceSURF_FORMAT_MODE;

/* Pixel swizzle modes. */
typedef enum _gceSURF_SWIZZLE
{
    gcvSURF_NOSWIZZLE = 0,
    gcvSURF_ARGB,
    gcvSURF_ABGR,
    gcvSURF_RGBA,
    gcvSURF_BGRA
}
gceSURF_SWIZZLE;

/* Transparency modes. */
typedef enum _gceSURF_TRANSPARENCY
{
    /* Valid only for PE 1.0 */
    gcvSURF_OPAQUE = 0,
    gcvSURF_SOURCE_MATCH,
    gcvSURF_SOURCE_MASK,
    gcvSURF_PATTERN_MASK,
}
gceSURF_TRANSPARENCY;

/* Surface Alignment. */
typedef enum _gceSURF_ALIGNMENT
{
    gcvSURF_FOUR = 0,
    gcvSURF_SIXTEEN,
    gcvSURF_SUPER_TILED,
    gcvSURF_SPLIT_TILED,
    gcvSURF_SPLIT_SUPER_TILED
}
gceSURF_ALIGNMENT;

/* Surface Addressing. */
typedef enum _gceSURF_ADDRESSING
{
    gcvSURF_NO_STRIDE_TILED = 0,
    gcvSURF_NO_STRIDE_LINEAR,
    gcvSURF_STRIDE_TILED,
    gcvSURF_STRIDE_LINEAR
}
gceSURF_ADDRESSING;

/* Transparency modes. */
typedef enum _gce2D_TRANSPARENCY
{
    /* Valid only for PE 2.0 */
    gcv2D_OPAQUE = 0,
    gcv2D_KEYED,
    gcv2D_MASKED
}
gce2D_TRANSPARENCY;

/* Mono packing modes. */
typedef enum _gceSURF_MONOPACK
{
    gcvSURF_PACKED8 = 0,
    gcvSURF_PACKED16,
    gcvSURF_PACKED32,
    gcvSURF_UNPACKED,
}
gceSURF_MONOPACK;

/* Blending modes. */
typedef enum _gceSURF_BLEND_MODE
{
    /* Porter-Duff blending modes.                   */
    /*                         Fsrc      Fdst        */
    gcvBLEND_CLEAR = 0,     /* 0         0           */
    gcvBLEND_SRC,           /* 1         0           */
    gcvBLEND_DST,           /* 0         1           */
    gcvBLEND_SRC_OVER_DST,  /* 1         1 - Asrc    */
    gcvBLEND_DST_OVER_SRC,  /* 1 - Adst  1           */
    gcvBLEND_SRC_IN_DST,    /* Adst      0           */
    gcvBLEND_DST_IN_SRC,    /* 0         Asrc        */
    gcvBLEND_SRC_OUT_DST,   /* 1 - Adst  0           */
    gcvBLEND_DST_OUT_SRC,   /* 0         1 - Asrc    */
    gcvBLEND_SRC_ATOP_DST,  /* Adst      1 - Asrc    */
    gcvBLEND_DST_ATOP_SRC,  /* 1 - Adst  Asrc        */
    gcvBLEND_SRC_XOR_DST,   /* 1 - Adst  1 - Asrc    */

    /* Special blending modes.                       */
    gcvBLEND_SET,           /* DST = 1               */
    gcvBLEND_SUB            /* DST = DST * (1 - SRC) */
}
gceSURF_BLEND_MODE;

/* Per-pixel alpha modes. */
typedef enum _gceSURF_PIXEL_ALPHA_MODE
{
    gcvSURF_PIXEL_ALPHA_STRAIGHT = 0,
    gcvSURF_PIXEL_ALPHA_INVERSED
}
gceSURF_PIXEL_ALPHA_MODE;

/* Global alpha modes. */
typedef enum _gceSURF_GLOBAL_ALPHA_MODE
{
    gcvSURF_GLOBAL_ALPHA_OFF = 0,
    gcvSURF_GLOBAL_ALPHA_ON,
    gcvSURF_GLOBAL_ALPHA_SCALE
}
gceSURF_GLOBAL_ALPHA_MODE;

/* Color component modes for alpha blending. */
typedef enum _gceSURF_PIXEL_COLOR_MODE
{
    gcvSURF_COLOR_STRAIGHT = 0,
    gcvSURF_COLOR_MULTIPLY
}
gceSURF_PIXEL_COLOR_MODE;

/* Color component modes for alpha blending. */
typedef enum _gce2D_PIXEL_COLOR_MULTIPLY_MODE
{
    gcv2D_COLOR_MULTIPLY_DISABLE = 0,
    gcv2D_COLOR_MULTIPLY_ENABLE
}
gce2D_PIXEL_COLOR_MULTIPLY_MODE;

/* Color component modes for alpha blending. */
typedef enum _gce2D_GLOBAL_COLOR_MULTIPLY_MODE
{
    gcv2D_GLOBAL_COLOR_MULTIPLY_DISABLE = 0,
    gcv2D_GLOBAL_COLOR_MULTIPLY_ALPHA,
    gcv2D_GLOBAL_COLOR_MULTIPLY_COLOR
}
gce2D_GLOBAL_COLOR_MULTIPLY_MODE;

/* Alpha blending factor modes. */
typedef enum _gceSURF_BLEND_FACTOR_MODE
{
    gcvSURF_BLEND_ZERO = 0,
    gcvSURF_BLEND_ONE,
    gcvSURF_BLEND_STRAIGHT,
    gcvSURF_BLEND_INVERSED,
    gcvSURF_BLEND_COLOR,
    gcvSURF_BLEND_COLOR_INVERSED,
    gcvSURF_BLEND_SRC_ALPHA_SATURATED,
    gcvSURF_BLEND_STRAIGHT_NO_CROSS,
    gcvSURF_BLEND_INVERSED_NO_CROSS,
    gcvSURF_BLEND_COLOR_NO_CROSS,
    gcvSURF_BLEND_COLOR_INVERSED_NO_CROSS,
    gcvSURF_BLEND_SRC_ALPHA_SATURATED_CROSS
}
gceSURF_BLEND_FACTOR_MODE;

/* Alpha blending porter duff rules. */
typedef enum _gce2D_PORTER_DUFF_RULE
{
    gcvPD_CLEAR = 0,
    gcvPD_SRC,
    gcvPD_SRC_OVER,
    gcvPD_DST_OVER,
    gcvPD_SRC_IN,
    gcvPD_DST_IN,
    gcvPD_SRC_OUT,
    gcvPD_DST_OUT,
    gcvPD_SRC_ATOP,
    gcvPD_DST_ATOP,
    gcvPD_ADD,
    gcvPD_XOR,
    gcvPD_DST
}
gce2D_PORTER_DUFF_RULE;

/* Alpha blending factor modes. */
typedef enum _gce2D_YUV_COLOR_MODE
{
    gcv2D_YUV_601= 0,
    gcv2D_YUV_709,
    gcv2D_YUV_USER_DEFINED,
    gcv2D_YUV_USER_DEFINED_CLAMP,

    /* Default setting is for src. gcv2D_YUV_DST
        can be ORed to set dst.
    */
    gcv2D_YUV_DST = 0x80000000,
}
gce2D_YUV_COLOR_MODE;

/* Nature rotation rules. */
typedef enum _gce2D_NATURE_ROTATION
{
    gcvNR_0_DEGREE = 0,
    gcvNR_LEFT_90_DEGREE,
    gcvNR_RIGHT_90_DEGREE,
    gcvNR_180_DEGREE,
    gcvNR_FLIP_X,
    gcvNR_FLIP_Y,
    gcvNR_TOTAL_RULE,
}
gce2D_NATURE_ROTATION;

typedef enum _gce2D_COMMAND
{
    gcv2D_CLEAR = 0,
    gcv2D_LINE,
    gcv2D_BLT,
    gcv2D_STRETCH,
    gcv2D_HOR_FILTER,
    gcv2D_VER_FILTER,
    gcv2D_MULTI_SOURCE_BLT,
    gcv2D_FILTER_BLT,
}
gce2D_COMMAND;

typedef enum _gce2D_TILE_STATUS_CONFIG
{
    gcv2D_TSC_DISABLE       = 0,
    gcv2D_TSC_ENABLE        = 0x00000001,
    gcv2D_TSC_COMPRESSED    = 0x00000002,
    gcv2D_TSC_DOWN_SAMPLER  = 0x00000004,
    gcv2D_TSC_2D_COMPRESSED = 0x00000008,

    gcv2D_TSC_DEC_COMPRESSED = 0x00000020,
    gcv2D_TSC_DEC_TPC        = 0x00000040,
    gcv2D_TSC_DEC_TPC_COMPRESSED = 0x00000080,

    gcv2D_TSC_V4_COMPRESSED      = 0x00000100,
    gcv2D_TSC_V4_COMPRESSED_256B = 0x00000200 | gcv2D_TSC_V4_COMPRESSED,

    gcv2D_TSC_DEC_TPC_TILED  = gcv2D_TSC_DEC_COMPRESSED | gcv2D_TSC_DEC_TPC,
    gcv2D_TSC_DEC_TPC_TILED_COMPRESSED = gcv2D_TSC_DEC_TPC_TILED | gcv2D_TSC_DEC_TPC_COMPRESSED,

    gcv2D_TSC_TPC_COMPRESSED     = 0x00001000,
    gcv2D_TSC_TPC_COMPRESSED_V10 = gcv2D_TSC_TPC_COMPRESSED | 0x00000400,
    gcv2D_TSC_TPC_COMPRESSED_V11 = gcv2D_TSC_TPC_COMPRESSED | 0x00000800,
}
gce2D_TILE_STATUS_CONFIG;

typedef enum _gce2D_QUERY
{
    gcv2D_QUERY_RGB_ADDRESS_MIN_ALIGN       = 0,
    gcv2D_QUERY_RGB_STRIDE_MIN_ALIGN,
    gcv2D_QUERY_YUV_ADDRESS_MIN_ALIGN,
    gcv2D_QUERY_YUV_STRIDE_MIN_ALIGN,
}
gce2D_QUERY;

typedef enum _gce2D_SUPER_TILE_VERSION
{
    gcv2D_SUPER_TILE_VERSION_V1       = 1,
    gcv2D_SUPER_TILE_VERSION_V2       = 2,
    gcv2D_SUPER_TILE_VERSION_V3       = 3,
}
gce2D_SUPER_TILE_VERSION;

typedef enum _gce2D_STATE
{
    gcv2D_STATE_SPECIAL_FILTER_MIRROR_MODE       = 1,
    gcv2D_STATE_SUPER_TILE_VERSION,
    gcv2D_STATE_EN_GAMMA,
    gcv2D_STATE_DE_GAMMA,
    gcv2D_STATE_MULTI_SRC_BLIT_UNIFIED_DST_RECT,
    gcv2D_STATE_MULTI_SRC_BLIT_BILINEAR_FILTER,
    gcv2D_STATE_PROFILE_ENABLE,
    gcv2D_STATE_XRGB_ENABLE,

    gcv2D_STATE_ARRAY_EN_GAMMA                   = 0x10001,
    gcv2D_STATE_ARRAY_DE_GAMMA,
    gcv2D_STATE_ARRAY_CSC_YUV_TO_RGB,
    gcv2D_STATE_ARRAY_CSC_RGB_TO_YUV,

    gcv2D_STATE_DEC_TPC_NV12_10BIT              = 0x20001,
    gcv2D_STATE_ARRAY_YUV_SRC_TILE_STATUS_ADDR,
    gcv2D_STATE_ARRAY_YUV_DST_TILE_STATUS_ADDR,
}
gce2D_STATE;

typedef enum _gce2D_STATE_PROFILE
{
    gcv2D_STATE_PROFILE_NONE    = 0x0,
    gcv2D_STATE_PROFILE_COMMAND = 0x1,
    gcv2D_STATE_PROFILE_SURFACE = 0x2,
    gcv2D_STATE_PROFILE_ALL     = 0xFFFF,
}
gce2D_STATE_PROFILE;

/* Texture object types */
typedef enum _gceTEXTURE_TYPE
{
    gcvTEXTURE_UNKNOWN = 0,
    gcvTEXTURE_1D,
    gcvTEXTURE_2D,
    gcvTEXTURE_3D,
    gcvTEXTURE_CUBEMAP,
    gcvTEXTURE_1D_ARRAY,
    gcvTEXTURE_2D_ARRAY,
    gcvTEXTURE_2D_MS,
    gcvTEXTURE_2D_MS_ARRAY,
    gcvTEXTURE_CUBEMAP_ARRAY,
    gcvTEXTURE_EXTERNAL
}
gceTEXTURE_TYPE;

#if gcdENABLE_3D
/* Texture functions. */
typedef enum _gceTEXTURE_FUNCTION
{
    gcvTEXTURE_DUMMY = 0,
    gcvTEXTURE_REPLACE = 0,
    gcvTEXTURE_MODULATE,
    gcvTEXTURE_ADD,
    gcvTEXTURE_ADD_SIGNED,
    gcvTEXTURE_INTERPOLATE,
    gcvTEXTURE_SUBTRACT,
    gcvTEXTURE_DOT3
}
gceTEXTURE_FUNCTION;

/* Texture sources. */
typedef enum _gceTEXTURE_SOURCE
{
    gcvCOLOR_FROM_TEXTURE = 0,
    gcvCOLOR_FROM_CONSTANT_COLOR,
    gcvCOLOR_FROM_PRIMARY_COLOR,
    gcvCOLOR_FROM_PREVIOUS_COLOR
}
gceTEXTURE_SOURCE;

/* Texture source channels. */
typedef enum _gceTEXTURE_CHANNEL
{
    gcvFROM_COLOR = 0,
    gcvFROM_ONE_MINUS_COLOR,
    gcvFROM_ALPHA,
    gcvFROM_ONE_MINUS_ALPHA
}
gceTEXTURE_CHANNEL;
#endif /* gcdENABLE_3D */

/* Filter types. */
typedef enum _gceFILTER_TYPE
{
    gcvFILTER_SYNC = 0,
    gcvFILTER_BLUR,
    gcvFILTER_USER
}
gceFILTER_TYPE;

/* Filter pass types. */
typedef enum _gceFILTER_PASS_TYPE
{
    gcvFILTER_HOR_PASS = 0,
    gcvFILTER_VER_PASS
}
gceFILTER_PASS_TYPE;

/* Endian hints. */
typedef enum _gceENDIAN_HINT
{
    gcvENDIAN_NO_SWAP = 0,
    gcvENDIAN_SWAP_WORD,
    gcvENDIAN_SWAP_DWORD
}
gceENDIAN_HINT;

/* Tiling modes. */
typedef enum _gceTILING
{
    gcvINVALIDTILED = 0x0,        /* Invalid tiling */
    /* Tiling basic modes enum'ed in power of 2. */
    gcvLINEAR      = 0x1,         /* No    tiling. */
    gcvTILED       = 0x2,         /* 4x4   tiling. */
    gcvSUPERTILED  = 0x4,         /* 64x64 tiling. */
    gcvMINORTILED  = 0x8,         /* 2x2   tiling. */

    /* Tiling special layouts. */
    gcvTILING_SPLIT_BUFFER = 0x10,
    gcvTILING_X_MAJOR      = 0x20,
    gcvTILING_Y_MAJOR      = 0x40,
    gcvTILING_SWAP         = 0x80,

    /* Tiling combination layouts. */
    gcvMULTI_TILED      = gcvTILED
                        | gcvTILING_SPLIT_BUFFER,

    gcvMULTI_SUPERTILED = gcvSUPERTILED
                        | gcvTILING_SPLIT_BUFFER,

    gcvYMAJOR_SUPERTILED = gcvSUPERTILED
                        | gcvTILING_Y_MAJOR,

    gcvTILED_8X4           = 0x0100,
    gcvTILED_4X8           = 0x0100 | gcvTILING_SWAP,
    gcvTILED_8X8           = 0x0200,
    gcvTILED_16X4          = 0x0400,
    gcvTILED_32X4          = 0x0800,
    gcvTILED_64X4          = 0x1000,

    gcvTILED_8X8_XMAJOR    = gcvTILED_8X8 | gcvTILING_X_MAJOR,
    gcvTILED_8X8_YMAJOR    = gcvTILED_8X8 | gcvTILING_Y_MAJOR,
}
gceTILING;

typedef enum _gceCACHE_MODE
{
    gcvCACHE_NONE,
    gcvCACHE_128,
    gcvCACHE_256,
}
gceCACHE_MODE;

#define DEFAULT_CACHE_MODE    gcvCACHE_256

/* 2D pattern type. */
typedef enum _gce2D_PATTERN
{
    gcv2D_PATTERN_SOLID = 0,
    gcv2D_PATTERN_MONO,
    gcv2D_PATTERN_COLOR,
    gcv2D_PATTERN_INVALID
}
gce2D_PATTERN;

/* 2D source type. */
typedef enum _gce2D_SOURCE
{
    gcv2D_SOURCE_MASKED = 0,
    gcv2D_SOURCE_MONO,
    gcv2D_SOURCE_COLOR,
    gcv2D_SOURCE_INVALID
}
gce2D_SOURCE;

/* Pipes. */
typedef enum _gcePIPE_SELECT
{
    gcvPIPE_INVALID = ~0,
    gcvPIPE_3D      =  0,
    gcvPIPE_2D
}
gcePIPE_SELECT;

/* Hardware type. */
typedef enum _gceHARDWARE_TYPE
{
    gcvHARDWARE_INVALID,
    gcvHARDWARE_3D,
    gcvHARDWARE_2D,
    gcvHARDWARE_VG,
    gcvHARDWARE_3D2D,
    gcvHARDWARE_NUM_TYPES,
}
gceHARDWARE_TYPE;

#define gcdCHIP_COUNT               gcvCORE_COUNT

typedef enum _gceMMU_MODE
{
    gcvMMU_MODE_1K,
    gcvMMU_MODE_4K,
} gceMMU_MODE;

/* User signal command codes. */
typedef enum _gceUSER_SIGNAL_COMMAND_CODES
{
    gcvUSER_SIGNAL_CREATE,
    gcvUSER_SIGNAL_DESTROY,
    gcvUSER_SIGNAL_SIGNAL,
    gcvUSER_SIGNAL_WAIT,
    gcvUSER_SIGNAL_MAP,
    gcvUSER_SIGNAL_UNMAP,
}
gceUSER_SIGNAL_COMMAND_CODES;

/* Sync point command codes. */
typedef enum _gceSYNC_POINT_COMMAND_CODES
{
    gcvSYNC_POINT_CREATE,
    gcvSYNC_POINT_DESTROY,
    gcvSYNC_POINT_SIGNAL,
}
gceSYNC_POINT_COMMAND_CODES;

/* Shared buffer command codes. */
typedef enum _gceSHBUF_COMMAND_CODES
{
    gcvSHBUF_CREATE,
    gcvSHBUF_DESTROY,
    gcvSHBUF_MAP,
    gcvSHBUF_WRITE,
    gcvSHBUF_READ,
}
gceSHBUF_COMMAND_CODES;

/* Event locations. */
typedef enum _gceKERNEL_WHERE
{
    gcvKERNEL_COMMAND,
    gcvKERNEL_VERTEX,
    gcvKERNEL_TRIANGLE,
    gcvKERNEL_TEXTURE,
    gcvKERNEL_PIXEL,
    gcvKERNEL_BLT,
}
gceKERNEL_WHERE;

#if gcdENABLE_VG
/* Hardware blocks. */
typedef enum _gceBLOCK
{
    gcvBLOCK_COMMAND,
    gcvBLOCK_TESSELLATOR,
    gcvBLOCK_TESSELLATOR2,
    gcvBLOCK_TESSELLATOR3,
    gcvBLOCK_RASTER,
    gcvBLOCK_VG,
    gcvBLOCK_VG2,
    gcvBLOCK_VG3,
    gcvBLOCK_PIXEL,

    /* Number of defined blocks. */
    gcvBLOCK_COUNT
}
gceBLOCK;
#endif

/* gcdDUMP message type. */
typedef enum _gceDEBUG_MESSAGE_TYPE
{
    gcvMESSAGE_TEXT,
    gcvMESSAGE_DUMP
}
gceDEBUG_MESSAGE_TYPE;

/* Shading format. */
typedef enum _gceSHADING
{
    gcvSHADING_SMOOTH,
    gcvSHADING_FLAT_D3D,
    gcvSHADING_FLAT_OPENGL,
}
gceSHADING;

/* Culling modes. */
typedef enum _gceCULL
{
    gcvCULL_NONE,
    gcvCULL_CCW,
    gcvCULL_CW,
}
gceCULL;

/* Fill modes. */
typedef enum _gceFILL
{
    gcvFILL_POINT,
    gcvFILL_WIRE_FRAME,
    gcvFILL_SOLID,
}
gceFILL;

/* Compare modes. */
typedef enum _gceCOMPARE
{
    gcvCOMPARE_INVALID = 0,
    gcvCOMPARE_NEVER,
    gcvCOMPARE_NOT_EQUAL,
    gcvCOMPARE_LESS,
    gcvCOMPARE_LESS_OR_EQUAL,
    gcvCOMPARE_EQUAL,
    gcvCOMPARE_GREATER,
    gcvCOMPARE_GREATER_OR_EQUAL,
    gcvCOMPARE_ALWAYS,
}
gceCOMPARE;

/* Stencil modes. */
typedef enum _gceSTENCIL_MODE
{
    gcvSTENCIL_NONE,
    gcvSTENCIL_SINGLE_SIDED,
    gcvSTENCIL_DOUBLE_SIDED,
}
gceSTENCIL_MODE;

/* Stencil operations. */
typedef enum _gceSTENCIL_OPERATION
{
    gcvSTENCIL_KEEP,
    gcvSTENCIL_REPLACE,
    gcvSTENCIL_ZERO,
    gcvSTENCIL_INVERT,
    gcvSTENCIL_INCREMENT,
    gcvSTENCIL_DECREMENT,
    gcvSTENCIL_INCREMENT_SATURATE,
    gcvSTENCIL_DECREMENT_SATURATE,
    gcvSTENCIL_OPERATION_INVALID = -1
}
gceSTENCIL_OPERATION;

/* Stencil selection. */
typedef enum _gceSTENCIL_WHERE
{
    gcvSTENCIL_FRONT,
    gcvSTENCIL_BACK,
}
gceSTENCIL_WHERE;

/* Texture addressing selection. */
typedef enum _gceTEXTURE_WHICH
{
    gcvTEXTURE_S,
    gcvTEXTURE_T,
    gcvTEXTURE_R,
}
gceTEXTURE_WHICH;

/* Texture addressing modes. */
typedef enum _gceTEXTURE_ADDRESSING
{
    gcvTEXTURE_INVALID    = 0,
    gcvTEXTURE_CLAMP,
    gcvTEXTURE_WRAP,
    gcvTEXTURE_MIRROR,
    gcvTEXTURE_BORDER,
    gcvTEXTURE_MIRROR_ONCE,
}
gceTEXTURE_ADDRESSING;

/* Texture filters. */
typedef enum _gceTEXTURE_FILTER
{
    gcvTEXTURE_NONE,
    gcvTEXTURE_POINT,
    gcvTEXTURE_LINEAR,
    gcvTEXTURE_ANISOTROPIC,
}
gceTEXTURE_FILTER;

typedef enum _gceTEXTURE_COMPONENT
{
    gcvTEXTURE_COMPONENT_R,
    gcvTEXTURE_COMPONENT_G,
    gcvTEXTURE_COMPONENT_B,
    gcvTEXTURE_COMPONENT_A,

    gcvTEXTURE_COMPONENT_NUM,
} gceTEXTURE_COMPONENT;

/* Texture swizzle modes. */
typedef enum _gceTEXTURE_SWIZZLE
{
    gcvTEXTURE_SWIZZLE_R = 0,
    gcvTEXTURE_SWIZZLE_G,
    gcvTEXTURE_SWIZZLE_B,
    gcvTEXTURE_SWIZZLE_A,
    gcvTEXTURE_SWIZZLE_0,
    gcvTEXTURE_SWIZZLE_1,

    gcvTEXTURE_SWIZZLE_INVALID,
} gceTEXTURE_SWIZZLE;

typedef enum _gceTEXTURE_SRGBDECODE
{
    gcvTEXTURE_SRGB_INVALID = 0,
    gcvTEXTURE_DECODE,
    gcvTEXTURE_SKIP_DECODE,
}gceTEXTURE_SRGBDECODE;

typedef enum _gceTEXTURE_COMPARE_MODE
{
    gcvTEXTURE_COMPARE_MODE_INVALID  = 0,
    gcvTEXTURE_COMPARE_MODE_NONE,
    gcvTEXTURE_COMPARE_MODE_REF,
} gceTEXTURE_COMPARE_MODE;

typedef enum _gceTEXTURE_DS_MODE
{
    gcvTEXTURE_DS_MODE_INVALID = 0,
    gcvTEXTURE_DS_MODE_DEPTH   = 1,
    gcvTEXTURE_DS_MODE_STENCIL = 2,
}gceTEXTURE_DS_MODE;


/* Pixel output swizzle modes. */
typedef enum _gcePIXEL_SWIZZLE
{
    gcvPIXEL_SWIZZLE_R = gcvTEXTURE_SWIZZLE_R,
    gcvPIXEL_SWIZZLE_G = gcvTEXTURE_SWIZZLE_G,
    gcvPIXEL_SWIZZLE_B = gcvTEXTURE_SWIZZLE_B,
    gcvPIXEL_SWIZZLE_A = gcvTEXTURE_SWIZZLE_A,

    gcvPIXEL_SWIZZLE_INVALID,
} gcePIXEL_SWIZZLE;

/* Primitive types. */
typedef enum _gcePRIMITIVE
{
    gcvPRIMITIVE_POINT_LIST,
    gcvPRIMITIVE_LINE_LIST,
    gcvPRIMITIVE_LINE_STRIP,
    gcvPRIMITIVE_LINE_LOOP,
    gcvPRIMITIVE_TRIANGLE_LIST,
    gcvPRIMITIVE_TRIANGLE_STRIP,
    gcvPRIMITIVE_TRIANGLE_FAN,
    gcvPRIMITIVE_RECTANGLE,
    gcvPRIMITIVE_LINES_ADJACENCY,
    gcvPRIMITIVE_LINE_STRIP_ADJACENCY,
    gcvPRIMITIVE_TRIANGLES_ADJACENCY,
    gcvPRIMITIVE_TRIANGLE_STRIP_ADJACENCY,
    gcvPRIMITIVE_PATCH_LIST,
}
gcePRIMITIVE;

/* Index types. */
typedef enum _gceINDEX_TYPE
{
    gcvINDEX_8,
    gcvINDEX_16,
    gcvINDEX_32,
}
gceINDEX_TYPE;

/* Multi GPU rendering modes. */
typedef enum _gceMULTI_GPU_RENDERING_MODE
{
    gcvMULTI_GPU_RENDERING_MODE_OFF,
    gcvMULTI_GPU_RENDERING_MODE_SPLIT_WIDTH,
    gcvMULTI_GPU_RENDERING_MODE_SPLIT_HEIGHT,
    gcvMULTI_GPU_RENDERING_MODE_INTERLEAVED_64x64,
    gcvMULTI_GPU_RENDERING_MODE_INTERLEAVED_128x64,
    gcvMULTI_GPU_RENDERING_MODE_INTERLEAVED_128x128,
    gcvMULTI_GPU_RENDERING_MODE_INVALID
}
gceMULTI_GPU_RENDERING_MODE;

typedef enum _gceCORE_3D_MASK
{
    gcvCORE_3D_0_MASK   = (1 << 0),
    gcvCORE_3D_1_MASK   = (1 << 1),

    gcvCORE_3D_ALL_MASK = (0xFFFF)
}
gceCORE_3D_MASK;

typedef enum _gceCORE_3D_ID
{
    gcvCORE_3D_0_ID       = 0,
    gcvCORE_3D_1_ID       = 1,

    gcvCORE_3D_ID_INVALID = ~0UL
}
gceCORE_3D_ID;

typedef enum _gceMULTI_GPU_MODE
{
    gcvMULTI_GPU_MODE_COMBINED    = 0,
    gcvMULTI_GPU_MODE_INDEPENDENT = 1
}
gceMULTI_GPU_MODE;

typedef enum _gceMACHINECODE
{
    gcvMACHINECODE_ANTUTU0 = 0x0,

    gcvMACHINECODE_GLB27_RELEASE_0,

    gcvMACHINECODE_GLB25_RELEASE_0,
    gcvMACHINECODE_GLB25_RELEASE_1,

    /* keep it as the last enum */
    gcvMACHINECODE_COUNT
}
gceMACHINECODE;

typedef enum _gceUNIFORMCVT
{
    gcvUNIFORMCVT_NONE = 0,
    gcvUNIFORMCVT_TO_BOOL,
    gcvUNIFORMCVT_TO_FLOAT,
} gceUNIFORMCVT;

typedef enum _gceHAL_ARG_VERSION
{
    gcvHAL_ARG_VERSION_V1 = 0x0,
    gcvHAL_ARG_VERSION_V2,
}
gceHAL_ARG_VERSION;


typedef enum _gceCMDBUF_TYPE
{
    /* Contiguous command buffer. */
    gcvCMDBUF_CONTIGUOUS,
    /* Virtual command buffer. */
    gcvCMDBUF_VIRTUAL,
    /* Command buffer allocated from reserved memory. */
    gcvCMDBUF_RESERVED,
}
gceCMDBUF_SOURCE;

typedef enum _gceCHIP_FLAG
{
    gcvCHIP_FLAG_MSAA_COHERENCEY_ECO_FIX = 1 << 0,
    gcvCHIP_FLAG_GC2000_R2               = 1 << 1,
}
gceCHIP_FLAG;

typedef enum
{
    gcvENGINE_RENDER   = 0,
    gcvENGINE_BLT      = 1,
    gcvENGINE_COUNT    = 2,
}
gceENGINE;

/* CORE enum. */
typedef enum _gceCORE
{
    gcvCORE_MAJOR,
    gcvCORE_3D1,
    gcvCORE_3D2,
    gcvCORE_3D3,
    gcvCORE_2D,
    gcvCORE_VG,
#if gcdDEC_ENABLE_AHB
    gcvCORE_DEC,
#endif
    gcvCORE_COUNT
}
gceCORE;


typedef enum _gceADDRESS_AREA
{
    gcvADDRESS_AREA_NORMAL,
    gcvADDRESS_AREA_SECURE,

    gcvADDRESS_AREA_COUNT
}
gceADDRESS_AREA;

typedef enum _gceSECURE_MODE
{
    /* For cores without gcvFEATURE_SECURITY. */
    gcvSECURE_NONE,

    /* Use registers added in gcvFEATURE_SECURITY in normal driver,
    ** In this mode, GPU always works under non secure mode and
    ** should not touch secure buffer. It is used to test basic function.
    */
    gcvSECURE_IN_NORMAL,

    /* Make use of gcvFEATURE_SECURITY in trust application. */
    gcvSECURE_IN_TA
}
gceSECURE_MODE;

/*
* Bit of a requirement is 1 means requirement is a must, 0 means requirement can
* be ignored.
*/
#define gcvALLOC_FLAG_CONTIGUOUS_BIT        0
#define gcvALLOC_FLAG_CACHEABLE_BIT         1
#define gcvALLOC_FLAG_SECURITY_BIT          2
#define gcvALLOC_FLAG_NON_CONTIGUOUS_BIT    3
#define gcvALLOC_FLAG_MEMLIMIT_BIT          4
#define gcvALLOC_FLAG_DMABUF_BIT            5
#define gcvALLOC_FLAG_USERMEMORY_BIT        6
#define gcvALLOC_FLAG_EXTERNAL_MEMORY_BIT   7

/* No special needs. */
#define gcvALLOC_FLAG_NONE              (0)
/* Physical contiguous. */
#define gcvALLOC_FLAG_CONTIGUOUS        (1 << gcvALLOC_FLAG_CONTIGUOUS_BIT)
/* Can be remapped as cacheable. */
#define gcvALLOC_FLAG_CACHEABLE         (1 << gcvALLOC_FLAG_CACHEABLE_BIT)
/* Secure buffer. */
#define gcvALLOC_FLAG_SECURITY          (1 << gcvALLOC_FLAG_SECURITY_BIT)
/* Physical non contiguous. */
#define gcvALLOC_FLAG_NON_CONTIGUOUS    (1 << gcvALLOC_FLAG_NON_CONTIGUOUS_BIT)
#define gcvALLOC_FLAG_MEMLIMIT          (1 << gcvALLOC_FLAG_MEMLIMIT_BIT)

/* Import DMABUF. */
#define gcvALLOC_FLAG_DMABUF            (1 << gcvALLOC_FLAG_DMABUF_BIT)
/* Import USERMEMORY. */
#define gcvALLOC_FLAG_USERMEMORY        (1 << gcvALLOC_FLAG_USERMEMORY_BIT)
/* Import an External Buffer. */
#define gcvALLOC_FLAG_EXTERNAL_MEMORY   (1 << gcvALLOC_FLAG_EXTERNAL_MEMORY_BIT)

/* GL_VIV internal usage */
#ifndef GL_MAP_BUFFER_OBJ_VIV
#define GL_MAP_BUFFER_OBJ_VIV       0x10000
#endif

/* Command buffer usage. */
#define gcvCOMMAND_2D   (1 << 0)
#define gcvCOMMAND_3D   (1 << 1)

/* Default chip ID means chip ID same as core index. */
#define gcvCHIP_ID_DEFAULT             (~0U)

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gckCONTEXT          * gckCONTEXT;
typedef struct _gcoCMDBUF           * gcoCMDBUF;

typedef struct _gcsSTATE_DELTA      * gcsSTATE_DELTA_PTR;
typedef struct _gcsQUEUE            * gcsQUEUE_PTR;
typedef struct _gcoQUEUE            * gcoQUEUE;
typedef struct _gcsHAL_INTERFACE    * gcsHAL_INTERFACE_PTR;
typedef struct _gcs2D_PROFILE       * gcs2D_PROFILE_PTR;


#if gcdENABLE_VG
typedef struct _gcoVGHARDWARE *            gcoVGHARDWARE;
typedef struct _gcoVGBUFFER *           gcoVGBUFFER;
typedef struct _gckVGHARDWARE *         gckVGHARDWARE;
typedef struct _gcsVGCONTEXT *            gcsVGCONTEXT_PTR;
typedef struct _gcsVGCONTEXT_MAP *        gcsVGCONTEXT_MAP_PTR;
typedef struct _gcsVGCMDQUEUE *            gcsVGCMDQUEUE_PTR;
typedef struct _gcsTASK_MASTER_TABLE *    gcsTASK_MASTER_TABLE_PTR;
typedef struct _gckVGKERNEL *            gckVGKERNEL;
typedef void *                            gctTHREAD;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_enum_h_ */
