/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#ifndef __gc_hal_profiler_h_
#define __gc_hal_profiler_h_

#ifdef __cplusplus
extern "C" {
#endif

#define GLVERTEX_OBJECT 10
#define GLVERTEX_OBJECT_BYTES 11

#define GLINDEX_OBJECT 20
#define GLINDEX_OBJECT_BYTES 21

#define GLTEXTURE_OBJECT 30
#define GLTEXTURE_OBJECT_BYTES 31

#if VIVANTE_PROFILER
#define gcmPROFILE_GC(Hal, Enum, Value)	gcoPROFILER_Count(Hal, Enum, Value)
#else
#define gcmPROFILE_GC(Hal, Enum, Value) do { } while (gcvFALSE)
#endif

/* HW profile information. */
typedef struct _gcsPROFILER_COUNTERS
{
    /* HW static counters. */
    gctUINT32       gpuClock;
    gctUINT32       axiClock;
    gctUINT32       shaderClock;

    /* HW vairable counters. */
    gctUINT32       gpuClockStart;
    gctUINT32       gpuClockEnd;

    /* HW vairable counters. */
    gctUINT32       gpuCyclesCounter;
    gctUINT32       gpuTotalRead64BytesPerFrame;
    gctUINT32       gpuTotalWrite64BytesPerFrame;

    /* PE */
    gctUINT32       pe_pixel_count_killed_by_color_pipe;
    gctUINT32       pe_pixel_count_killed_by_depth_pipe;
    gctUINT32       pe_pixel_count_drawn_by_color_pipe;
    gctUINT32       pe_pixel_count_drawn_by_depth_pipe;

    /* SH */
    gctUINT32       ps_inst_counter;
    gctUINT32       rendered_pixel_counter;
    gctUINT32       vs_inst_counter;
    gctUINT32       rendered_vertice_counter;
    gctUINT32       vtx_branch_inst_counter;
    gctUINT32       vtx_texld_inst_counter;
    gctUINT32       pxl_branch_inst_counter;
    gctUINT32       pxl_texld_inst_counter;

    /* PA */
    gctUINT32       pa_input_vtx_counter;
    gctUINT32       pa_input_prim_counter;
    gctUINT32       pa_output_prim_counter;
    gctUINT32       pa_depth_clipped_counter;
    gctUINT32       pa_trivial_rejected_counter;
    gctUINT32       pa_culled_counter;

    /* SE */
    gctUINT32       se_culled_triangle_count;
    gctUINT32       se_culled_lines_count;

    /* RA */
    gctUINT32       ra_valid_pixel_count;
    gctUINT32       ra_total_quad_count;
    gctUINT32       ra_valid_quad_count_after_early_z;
    gctUINT32       ra_total_primitive_count;
    gctUINT32       ra_pipe_cache_miss_counter;
    gctUINT32       ra_prefetch_cache_miss_counter;
	gctUINT32       ra_eez_culled_counter;

    /* TX */
    gctUINT32       tx_total_bilinear_requests;
    gctUINT32       tx_total_trilinear_requests;
    gctUINT32       tx_total_discarded_texture_requests;
    gctUINT32       tx_total_texture_requests;
    gctUINT32       tx_mem_read_count;
    gctUINT32       tx_mem_read_in_8B_count;
    gctUINT32       tx_cache_miss_count;
    gctUINT32       tx_cache_hit_texel_count;
    gctUINT32       tx_cache_miss_texel_count;

    /* MC */
    gctUINT32       mc_total_read_req_8B_from_pipeline;
    gctUINT32       mc_total_read_req_8B_from_IP;
    gctUINT32       mc_total_write_req_8B_from_pipeline;

    /* HI */
    gctUINT32       hi_axi_cycles_read_request_stalled;
    gctUINT32       hi_axi_cycles_write_request_stalled;
    gctUINT32       hi_axi_cycles_write_data_stalled;
}
gcsPROFILER_COUNTERS;

/* HAL profile information. */
typedef struct _gcsPROFILER
{
    gctFILE         		file;

    /* Aggregate Information */

    /* Clock Info */
    gctUINT64       		frameStart;
    gctUINT64       		frameEnd;

    /* Current frame information */
    gctUINT32       		frameNumber;
    gctUINT64       		frameStartTimeusec;
    gctUINT64       		frameEndTimeusec;
    gctUINT64       		frameStartCPUTimeusec;
    gctUINT64       		frameEndCPUTimeusec;

#if PROFILE_HAL_COUNTERS
    gctUINT32       		vertexBufferTotalBytesAlloc;
    gctUINT32       		vertexBufferNewBytesAlloc;
    int             		vertexBufferTotalObjectsAlloc;
    int             		vertexBufferNewObjectsAlloc;

    gctUINT32       		indexBufferTotalBytesAlloc;
    gctUINT32       		indexBufferNewBytesAlloc;
    int             		indexBufferTotalObjectsAlloc;
    int             		indexBufferNewObjectsAlloc;

    gctUINT32       		textureBufferTotalBytesAlloc;
    gctUINT32       		textureBufferNewBytesAlloc;
    int             		textureBufferTotalObjectsAlloc;
    int             		textureBufferNewObjectsAlloc;

    gctUINT32       		numCommits;
    gctUINT32       		drawPointCount;
    gctUINT32       		drawLineCount;
    gctUINT32       		drawTriangleCount;
    gctUINT32       		drawVertexCount;
    gctUINT32       		redundantStateChangeCalls;
#endif
}
gcsPROFILER;

/* Memory profile information. */
struct _gcsMemProfile
{
    /* Memory Usage */
    gctUINT32       videoMemUsed;
    gctUINT32       systemMemUsed;
    gctUINT32       commitBufferSize;
    gctUINT32       contextBufferCopyBytes;
};

/* Shader profile information. */
struct _gcsSHADER_PROFILER
{
    gctUINT32       shaderLength;
    gctUINT32       shaderALUCycles;
    gctUINT32       shaderTexLoadCycles;
    gctUINT32       shaderTempRegCount;
    gctUINT32       shaderSamplerRegCount;
    gctUINT32       shaderInputRegCount;
    gctUINT32       shaderOutputRegCount;
};

/* Initialize the gcsProfiler. */
gceSTATUS
gcoPROFILER_Initialize(
    IN gcoHAL Hal,
    IN gctFILE File
    );

/* Destroy the gcProfiler. */
gceSTATUS
gcoPROFILER_Destroy(
    IN gcoHAL Hal
    );

/* Call to signal end of frame. */
gceSTATUS
gcoPROFILER_EndFrame(
    IN gcoHAL Hal
    );

gceSTATUS
gcoPROFILER_Count(
	IN gcoHAL Hal,
	IN gctUINT32 Enum,
	IN gctINT Value
	);

/* Profile input vertex shader. */
gceSTATUS
gcoPROFILER_ShaderVS(
    IN gcoHAL Hal,
    IN gctPOINTER Vs
    );

/* Profile input fragment shader. */
gceSTATUS
gcoPROFILER_ShaderFS(
    IN gcoHAL Hal,
    IN gctPOINTER Fs
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_profiler_h_ */

