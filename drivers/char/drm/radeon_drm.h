/* radeon_drm.h -- Public header for the radeon driver -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * Copyright 2002 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#ifndef __RADEON_DRM_H__
#define __RADEON_DRM_H__

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the X server file (radeon_sarea.h)
 */
#ifndef __RADEON_SAREA_DEFINES__
#define __RADEON_SAREA_DEFINES__

/* Old style state flags, required for sarea interface (1.1 and 1.2
 * clears) and 1.2 drm_vertex2 ioctl.
 */
#define RADEON_UPLOAD_CONTEXT		0x00000001
#define RADEON_UPLOAD_VERTFMT		0x00000002
#define RADEON_UPLOAD_LINE		0x00000004
#define RADEON_UPLOAD_BUMPMAP		0x00000008
#define RADEON_UPLOAD_MASKS		0x00000010
#define RADEON_UPLOAD_VIEWPORT		0x00000020
#define RADEON_UPLOAD_SETUP		0x00000040
#define RADEON_UPLOAD_TCL		0x00000080
#define RADEON_UPLOAD_MISC		0x00000100
#define RADEON_UPLOAD_TEX0		0x00000200
#define RADEON_UPLOAD_TEX1		0x00000400
#define RADEON_UPLOAD_TEX2		0x00000800
#define RADEON_UPLOAD_TEX0IMAGES	0x00001000
#define RADEON_UPLOAD_TEX1IMAGES	0x00002000
#define RADEON_UPLOAD_TEX2IMAGES	0x00004000
#define RADEON_UPLOAD_CLIPRECTS		0x00008000	/* handled client-side */
#define RADEON_REQUIRE_QUIESCENCE	0x00010000
#define RADEON_UPLOAD_ZBIAS		0x00020000	/* version 1.2 and newer */
#define RADEON_UPLOAD_ALL		0x003effff
#define RADEON_UPLOAD_CONTEXT_ALL       0x003e01ff

/* New style per-packet identifiers for use in cmd_buffer ioctl with
 * the RADEON_EMIT_PACKET command.  Comments relate new packets to old
 * state bits and the packet size:
 */
#define RADEON_EMIT_PP_MISC                         0	/* context/7 */
#define RADEON_EMIT_PP_CNTL                         1	/* context/3 */
#define RADEON_EMIT_RB3D_COLORPITCH                 2	/* context/1 */
#define RADEON_EMIT_RE_LINE_PATTERN                 3	/* line/2 */
#define RADEON_EMIT_SE_LINE_WIDTH                   4	/* line/1 */
#define RADEON_EMIT_PP_LUM_MATRIX                   5	/* bumpmap/1 */
#define RADEON_EMIT_PP_ROT_MATRIX_0                 6	/* bumpmap/2 */
#define RADEON_EMIT_RB3D_STENCILREFMASK             7	/* masks/3 */
#define RADEON_EMIT_SE_VPORT_XSCALE                 8	/* viewport/6 */
#define RADEON_EMIT_SE_CNTL                         9	/* setup/2 */
#define RADEON_EMIT_SE_CNTL_STATUS                  10	/* setup/1 */
#define RADEON_EMIT_RE_MISC                         11	/* misc/1 */
#define RADEON_EMIT_PP_TXFILTER_0                   12	/* tex0/6 */
#define RADEON_EMIT_PP_BORDER_COLOR_0               13	/* tex0/1 */
#define RADEON_EMIT_PP_TXFILTER_1                   14	/* tex1/6 */
#define RADEON_EMIT_PP_BORDER_COLOR_1               15	/* tex1/1 */
#define RADEON_EMIT_PP_TXFILTER_2                   16	/* tex2/6 */
#define RADEON_EMIT_PP_BORDER_COLOR_2               17	/* tex2/1 */
#define RADEON_EMIT_SE_ZBIAS_FACTOR                 18	/* zbias/2 */
#define RADEON_EMIT_SE_TCL_OUTPUT_VTX_FMT           19	/* tcl/11 */
#define RADEON_EMIT_SE_TCL_MATERIAL_EMMISSIVE_RED   20	/* material/17 */
#define R200_EMIT_PP_TXCBLEND_0                     21	/* tex0/4 */
#define R200_EMIT_PP_TXCBLEND_1                     22	/* tex1/4 */
#define R200_EMIT_PP_TXCBLEND_2                     23	/* tex2/4 */
#define R200_EMIT_PP_TXCBLEND_3                     24	/* tex3/4 */
#define R200_EMIT_PP_TXCBLEND_4                     25	/* tex4/4 */
#define R200_EMIT_PP_TXCBLEND_5                     26	/* tex5/4 */
#define R200_EMIT_PP_TXCBLEND_6                     27	/* /4 */
#define R200_EMIT_PP_TXCBLEND_7                     28	/* /4 */
#define R200_EMIT_TCL_LIGHT_MODEL_CTL_0             29	/* tcl/7 */
#define R200_EMIT_TFACTOR_0                         30	/* tf/7 */
#define R200_EMIT_VTX_FMT_0                         31	/* vtx/5 */
#define R200_EMIT_VAP_CTL                           32	/* vap/1 */
#define R200_EMIT_MATRIX_SELECT_0                   33	/* msl/5 */
#define R200_EMIT_TEX_PROC_CTL_2                    34	/* tcg/5 */
#define R200_EMIT_TCL_UCP_VERT_BLEND_CTL            35	/* tcl/1 */
#define R200_EMIT_PP_TXFILTER_0                     36	/* tex0/6 */
#define R200_EMIT_PP_TXFILTER_1                     37	/* tex1/6 */
#define R200_EMIT_PP_TXFILTER_2                     38	/* tex2/6 */
#define R200_EMIT_PP_TXFILTER_3                     39	/* tex3/6 */
#define R200_EMIT_PP_TXFILTER_4                     40	/* tex4/6 */
#define R200_EMIT_PP_TXFILTER_5                     41	/* tex5/6 */
#define R200_EMIT_PP_TXOFFSET_0                     42	/* tex0/1 */
#define R200_EMIT_PP_TXOFFSET_1                     43	/* tex1/1 */
#define R200_EMIT_PP_TXOFFSET_2                     44	/* tex2/1 */
#define R200_EMIT_PP_TXOFFSET_3                     45	/* tex3/1 */
#define R200_EMIT_PP_TXOFFSET_4                     46	/* tex4/1 */
#define R200_EMIT_PP_TXOFFSET_5                     47	/* tex5/1 */
#define R200_EMIT_VTE_CNTL                          48	/* vte/1 */
#define R200_EMIT_OUTPUT_VTX_COMP_SEL               49	/* vtx/1 */
#define R200_EMIT_PP_TAM_DEBUG3                     50	/* tam/1 */
#define R200_EMIT_PP_CNTL_X                         51	/* cst/1 */
#define R200_EMIT_RB3D_DEPTHXY_OFFSET               52	/* cst/1 */
#define R200_EMIT_RE_AUX_SCISSOR_CNTL               53	/* cst/1 */
#define R200_EMIT_RE_SCISSOR_TL_0                   54	/* cst/2 */
#define R200_EMIT_RE_SCISSOR_TL_1                   55	/* cst/2 */
#define R200_EMIT_RE_SCISSOR_TL_2                   56	/* cst/2 */
#define R200_EMIT_SE_VAP_CNTL_STATUS                57	/* cst/1 */
#define R200_EMIT_SE_VTX_STATE_CNTL                 58	/* cst/1 */
#define R200_EMIT_RE_POINTSIZE                      59	/* cst/1 */
#define R200_EMIT_TCL_INPUT_VTX_VECTOR_ADDR_0       60	/* cst/4 */
#define R200_EMIT_PP_CUBIC_FACES_0                  61
#define R200_EMIT_PP_CUBIC_OFFSETS_0                62
#define R200_EMIT_PP_CUBIC_FACES_1                  63
#define R200_EMIT_PP_CUBIC_OFFSETS_1                64
#define R200_EMIT_PP_CUBIC_FACES_2                  65
#define R200_EMIT_PP_CUBIC_OFFSETS_2                66
#define R200_EMIT_PP_CUBIC_FACES_3                  67
#define R200_EMIT_PP_CUBIC_OFFSETS_3                68
#define R200_EMIT_PP_CUBIC_FACES_4                  69
#define R200_EMIT_PP_CUBIC_OFFSETS_4                70
#define R200_EMIT_PP_CUBIC_FACES_5                  71
#define R200_EMIT_PP_CUBIC_OFFSETS_5                72
#define RADEON_EMIT_PP_TEX_SIZE_0                   73
#define RADEON_EMIT_PP_TEX_SIZE_1                   74
#define RADEON_EMIT_PP_TEX_SIZE_2                   75
#define R200_EMIT_RB3D_BLENDCOLOR                   76
#define R200_EMIT_TCL_POINT_SPRITE_CNTL             77
#define RADEON_EMIT_PP_CUBIC_FACES_0                78
#define RADEON_EMIT_PP_CUBIC_OFFSETS_T0             79
#define RADEON_EMIT_PP_CUBIC_FACES_1                80
#define RADEON_EMIT_PP_CUBIC_OFFSETS_T1             81
#define RADEON_EMIT_PP_CUBIC_FACES_2                82
#define RADEON_EMIT_PP_CUBIC_OFFSETS_T2             83
#define R200_EMIT_PP_TRI_PERF_CNTL                  84
#define R200_EMIT_PP_AFS_0                          85
#define R200_EMIT_PP_AFS_1                          86
#define R200_EMIT_ATF_TFACTOR                       87
#define R200_EMIT_PP_TXCTLALL_0                     88
#define R200_EMIT_PP_TXCTLALL_1                     89
#define R200_EMIT_PP_TXCTLALL_2                     90
#define R200_EMIT_PP_TXCTLALL_3                     91
#define R200_EMIT_PP_TXCTLALL_4                     92
#define R200_EMIT_PP_TXCTLALL_5                     93
#define R200_EMIT_VAP_PVS_CNTL                      94
#define RADEON_MAX_STATE_PACKETS                    95

/* Commands understood by cmd_buffer ioctl.  More can be added but
 * obviously these can't be removed or changed:
 */
#define RADEON_CMD_PACKET      1	/* emit one of the register packets above */
#define RADEON_CMD_SCALARS     2	/* emit scalar data */
#define RADEON_CMD_VECTORS     3	/* emit vector data */
#define RADEON_CMD_DMA_DISCARD 4	/* discard current dma buf */
#define RADEON_CMD_PACKET3     5	/* emit hw packet */
#define RADEON_CMD_PACKET3_CLIP 6	/* emit hw packet wrapped in cliprects */
#define RADEON_CMD_SCALARS2     7	/* r200 stopgap */
#define RADEON_CMD_WAIT         8	/* emit hw wait commands -- note:
					 *  doesn't make the cpu wait, just
					 *  the graphics hardware */
#define RADEON_CMD_VECLINEAR	9       /* another r200 stopgap */

typedef union {
	int i;
	struct {
		unsigned char cmd_type, pad0, pad1, pad2;
	} header;
	struct {
		unsigned char cmd_type, packet_id, pad0, pad1;
	} packet;
	struct {
		unsigned char cmd_type, offset, stride, count;
	} scalars;
	struct {
		unsigned char cmd_type, offset, stride, count;
	} vectors;
	struct {
		unsigned char cmd_type, addr_lo, addr_hi, count;
	} veclinear;
	struct {
		unsigned char cmd_type, buf_idx, pad0, pad1;
	} dma;
	struct {
		unsigned char cmd_type, flags, pad0, pad1;
	} wait;
} drm_radeon_cmd_header_t;

#define RADEON_WAIT_2D  0x1
#define RADEON_WAIT_3D  0x2

/* Allowed parameters for R300_CMD_PACKET3
 */
#define R300_CMD_PACKET3_CLEAR		0
#define R300_CMD_PACKET3_RAW		1

/* Commands understood by cmd_buffer ioctl for R300.
 * The interface has not been stabilized, so some of these may be removed
 * and eventually reordered before stabilization.
 */
#define R300_CMD_PACKET0		1
#define R300_CMD_VPU			2	/* emit vertex program upload */
#define R300_CMD_PACKET3		3	/* emit a packet3 */
#define R300_CMD_END3D			4	/* emit sequence ending 3d rendering */
#define R300_CMD_CP_DELAY		5
#define R300_CMD_DMA_DISCARD		6
#define R300_CMD_WAIT			7
#	define R300_WAIT_2D  		0x1
#	define R300_WAIT_3D  		0x2
#	define R300_WAIT_2D_CLEAN  	0x3
#	define R300_WAIT_3D_CLEAN  	0x4
#define R300_CMD_SCRATCH		8

typedef union {
	unsigned int u;
	struct {
		unsigned char cmd_type, pad0, pad1, pad2;
	} header;
	struct {
		unsigned char cmd_type, count, reglo, reghi;
	} packet0;
	struct {
		unsigned char cmd_type, count, adrlo, adrhi;
	} vpu;
	struct {
		unsigned char cmd_type, packet, pad0, pad1;
	} packet3;
	struct {
		unsigned char cmd_type, packet;
		unsigned short count;	/* amount of packet2 to emit */
	} delay;
	struct {
		unsigned char cmd_type, buf_idx, pad0, pad1;
	} dma;
	struct {
		unsigned char cmd_type, flags, pad0, pad1;
	} wait;
	struct {
		unsigned char cmd_type, reg, n_bufs, flags;
	} scratch;
} drm_r300_cmd_header_t;

#define RADEON_FRONT			0x1
#define RADEON_BACK			0x2
#define RADEON_DEPTH			0x4
#define RADEON_STENCIL			0x8
#define RADEON_CLEAR_FASTZ		0x80000000
#define RADEON_USE_HIERZ		0x40000000
#define RADEON_USE_COMP_ZBUF		0x20000000

/* Primitive types
 */
#define RADEON_POINTS			0x1
#define RADEON_LINES			0x2
#define RADEON_LINE_STRIP		0x3
#define RADEON_TRIANGLES		0x4
#define RADEON_TRIANGLE_FAN		0x5
#define RADEON_TRIANGLE_STRIP		0x6

/* Vertex/indirect buffer size
 */
#define RADEON_BUFFER_SIZE		65536

/* Byte offsets for indirect buffer data
 */
#define RADEON_INDEX_PRIM_OFFSET	20

#define RADEON_SCRATCH_REG_OFFSET	32

#define RADEON_NR_SAREA_CLIPRECTS	12

/* There are 2 heaps (local/GART).  Each region within a heap is a
 * minimum of 64k, and there are at most 64 of them per heap.
 */
#define RADEON_LOCAL_TEX_HEAP		0
#define RADEON_GART_TEX_HEAP		1
#define RADEON_NR_TEX_HEAPS		2
#define RADEON_NR_TEX_REGIONS		64
#define RADEON_LOG_TEX_GRANULARITY	16

#define RADEON_MAX_TEXTURE_LEVELS	12
#define RADEON_MAX_TEXTURE_UNITS	3

#define RADEON_MAX_SURFACES		8

/* Blits have strict offset rules.  All blit offset must be aligned on
 * a 1K-byte boundary.
 */
#define RADEON_OFFSET_SHIFT             10
#define RADEON_OFFSET_ALIGN             (1 << RADEON_OFFSET_SHIFT)
#define RADEON_OFFSET_MASK              (RADEON_OFFSET_ALIGN - 1)

#endif				/* __RADEON_SAREA_DEFINES__ */

typedef struct {
	unsigned int red;
	unsigned int green;
	unsigned int blue;
	unsigned int alpha;
} radeon_color_regs_t;

typedef struct {
	/* Context state */
	unsigned int pp_misc;	/* 0x1c14 */
	unsigned int pp_fog_color;
	unsigned int re_solid_color;
	unsigned int rb3d_blendcntl;
	unsigned int rb3d_depthoffset;
	unsigned int rb3d_depthpitch;
	unsigned int rb3d_zstencilcntl;

	unsigned int pp_cntl;	/* 0x1c38 */
	unsigned int rb3d_cntl;
	unsigned int rb3d_coloroffset;
	unsigned int re_width_height;
	unsigned int rb3d_colorpitch;
	unsigned int se_cntl;

	/* Vertex format state */
	unsigned int se_coord_fmt;	/* 0x1c50 */

	/* Line state */
	unsigned int re_line_pattern;	/* 0x1cd0 */
	unsigned int re_line_state;

	unsigned int se_line_width;	/* 0x1db8 */

	/* Bumpmap state */
	unsigned int pp_lum_matrix;	/* 0x1d00 */

	unsigned int pp_rot_matrix_0;	/* 0x1d58 */
	unsigned int pp_rot_matrix_1;

	/* Mask state */
	unsigned int rb3d_stencilrefmask;	/* 0x1d7c */
	unsigned int rb3d_ropcntl;
	unsigned int rb3d_planemask;

	/* Viewport state */
	unsigned int se_vport_xscale;	/* 0x1d98 */
	unsigned int se_vport_xoffset;
	unsigned int se_vport_yscale;
	unsigned int se_vport_yoffset;
	unsigned int se_vport_zscale;
	unsigned int se_vport_zoffset;

	/* Setup state */
	unsigned int se_cntl_status;	/* 0x2140 */

	/* Misc state */
	unsigned int re_top_left;	/* 0x26c0 */
	unsigned int re_misc;
} drm_radeon_context_regs_t;

typedef struct {
	/* Zbias state */
	unsigned int se_zbias_factor;	/* 0x1dac */
	unsigned int se_zbias_constant;
} drm_radeon_context2_regs_t;

/* Setup registers for each texture unit
 */
typedef struct {
	unsigned int pp_txfilter;
	unsigned int pp_txformat;
	unsigned int pp_txoffset;
	unsigned int pp_txcblend;
	unsigned int pp_txablend;
	unsigned int pp_tfactor;
	unsigned int pp_border_color;
} drm_radeon_texture_regs_t;

typedef struct {
	unsigned int start;
	unsigned int finish;
	unsigned int prim:8;
	unsigned int stateidx:8;
	unsigned int numverts:16;	/* overloaded as offset/64 for elt prims */
	unsigned int vc_format;	/* vertex format */
} drm_radeon_prim_t;

typedef struct {
	drm_radeon_context_regs_t context;
	drm_radeon_texture_regs_t tex[RADEON_MAX_TEXTURE_UNITS];
	drm_radeon_context2_regs_t context2;
	unsigned int dirty;
} drm_radeon_state_t;

typedef struct {
	/* The channel for communication of state information to the
	 * kernel on firing a vertex buffer with either of the
	 * obsoleted vertex/index ioctls.
	 */
	drm_radeon_context_regs_t context_state;
	drm_radeon_texture_regs_t tex_state[RADEON_MAX_TEXTURE_UNITS];
	unsigned int dirty;
	unsigned int vertsize;
	unsigned int vc_format;

	/* The current cliprects, or a subset thereof.
	 */
	drm_clip_rect_t boxes[RADEON_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Counters for client-side throttling of rendering clients.
	 */
	unsigned int last_frame;
	unsigned int last_dispatch;
	unsigned int last_clear;

	drm_tex_region_t tex_list[RADEON_NR_TEX_HEAPS][RADEON_NR_TEX_REGIONS +
						       1];
	unsigned int tex_age[RADEON_NR_TEX_HEAPS];
	int ctx_owner;
	int pfState;		/* number of 3d windows (0,1,2ormore) */
	int pfCurrentPage;	/* which buffer is being displayed? */
	int crtc2_base;		/* CRTC2 frame offset */
	int tiling_enabled;	/* set by drm, read by 2d + 3d clients */
} drm_radeon_sarea_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmRadeon.h)
 *
 * KW: actually it's illegal to change any of this (backwards compatibility).
 */

/* Radeon specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_RADEON_CP_INIT    0x00
#define DRM_RADEON_CP_START   0x01
#define DRM_RADEON_CP_STOP    0x02
#define DRM_RADEON_CP_RESET   0x03
#define DRM_RADEON_CP_IDLE    0x04
#define DRM_RADEON_RESET      0x05
#define DRM_RADEON_FULLSCREEN 0x06
#define DRM_RADEON_SWAP       0x07
#define DRM_RADEON_CLEAR      0x08
#define DRM_RADEON_VERTEX     0x09
#define DRM_RADEON_INDICES    0x0A
#define DRM_RADEON_NOT_USED
#define DRM_RADEON_STIPPLE    0x0C
#define DRM_RADEON_INDIRECT   0x0D
#define DRM_RADEON_TEXTURE    0x0E
#define DRM_RADEON_VERTEX2    0x0F
#define DRM_RADEON_CMDBUF     0x10
#define DRM_RADEON_GETPARAM   0x11
#define DRM_RADEON_FLIP       0x12
#define DRM_RADEON_ALLOC      0x13
#define DRM_RADEON_FREE       0x14
#define DRM_RADEON_INIT_HEAP  0x15
#define DRM_RADEON_IRQ_EMIT   0x16
#define DRM_RADEON_IRQ_WAIT   0x17
#define DRM_RADEON_CP_RESUME  0x18
#define DRM_RADEON_SETPARAM   0x19
#define DRM_RADEON_SURF_ALLOC 0x1a
#define DRM_RADEON_SURF_FREE  0x1b

#define DRM_IOCTL_RADEON_CP_INIT    DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_CP_INIT, drm_radeon_init_t)
#define DRM_IOCTL_RADEON_CP_START   DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_CP_START)
#define DRM_IOCTL_RADEON_CP_STOP    DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_CP_STOP, drm_radeon_cp_stop_t)
#define DRM_IOCTL_RADEON_CP_RESET   DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_CP_RESET)
#define DRM_IOCTL_RADEON_CP_IDLE    DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_CP_IDLE)
#define DRM_IOCTL_RADEON_RESET      DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_RESET)
#define DRM_IOCTL_RADEON_FULLSCREEN DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_FULLSCREEN, drm_radeon_fullscreen_t)
#define DRM_IOCTL_RADEON_SWAP       DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_SWAP)
#define DRM_IOCTL_RADEON_CLEAR      DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_CLEAR, drm_radeon_clear_t)
#define DRM_IOCTL_RADEON_VERTEX     DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_VERTEX, drm_radeon_vertex_t)
#define DRM_IOCTL_RADEON_INDICES    DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_INDICES, drm_radeon_indices_t)
#define DRM_IOCTL_RADEON_STIPPLE    DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_STIPPLE, drm_radeon_stipple_t)
#define DRM_IOCTL_RADEON_INDIRECT   DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_INDIRECT, drm_radeon_indirect_t)
#define DRM_IOCTL_RADEON_TEXTURE    DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_TEXTURE, drm_radeon_texture_t)
#define DRM_IOCTL_RADEON_VERTEX2    DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_VERTEX2, drm_radeon_vertex2_t)
#define DRM_IOCTL_RADEON_CMDBUF     DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_CMDBUF, drm_radeon_cmd_buffer_t)
#define DRM_IOCTL_RADEON_GETPARAM   DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_GETPARAM, drm_radeon_getparam_t)
#define DRM_IOCTL_RADEON_FLIP       DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_FLIP)
#define DRM_IOCTL_RADEON_ALLOC      DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_ALLOC, drm_radeon_mem_alloc_t)
#define DRM_IOCTL_RADEON_FREE       DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_FREE, drm_radeon_mem_free_t)
#define DRM_IOCTL_RADEON_INIT_HEAP  DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_INIT_HEAP, drm_radeon_mem_init_heap_t)
#define DRM_IOCTL_RADEON_IRQ_EMIT   DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_IRQ_EMIT, drm_radeon_irq_emit_t)
#define DRM_IOCTL_RADEON_IRQ_WAIT   DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_IRQ_WAIT, drm_radeon_irq_wait_t)
#define DRM_IOCTL_RADEON_CP_RESUME  DRM_IO(  DRM_COMMAND_BASE + DRM_RADEON_CP_RESUME)
#define DRM_IOCTL_RADEON_SETPARAM   DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_SETPARAM, drm_radeon_setparam_t)
#define DRM_IOCTL_RADEON_SURF_ALLOC DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_SURF_ALLOC, drm_radeon_surface_alloc_t)
#define DRM_IOCTL_RADEON_SURF_FREE  DRM_IOW( DRM_COMMAND_BASE + DRM_RADEON_SURF_FREE, drm_radeon_surface_free_t)

typedef struct drm_radeon_init {
	enum {
		RADEON_INIT_CP = 0x01,
		RADEON_CLEANUP_CP = 0x02,
		RADEON_INIT_R200_CP = 0x03,
		RADEON_INIT_R300_CP = 0x04
	} func;
	unsigned long sarea_priv_offset;
	int is_pci;
	int cp_mode;
	int gart_size;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

	unsigned long fb_offset;
	unsigned long mmio_offset;
	unsigned long ring_offset;
	unsigned long ring_rptr_offset;
	unsigned long buffers_offset;
	unsigned long gart_textures_offset;
} drm_radeon_init_t;

typedef struct drm_radeon_cp_stop {
	int flush;
	int idle;
} drm_radeon_cp_stop_t;

typedef struct drm_radeon_fullscreen {
	enum {
		RADEON_INIT_FULLSCREEN = 0x01,
		RADEON_CLEANUP_FULLSCREEN = 0x02
	} func;
} drm_radeon_fullscreen_t;

#define CLEAR_X1	0
#define CLEAR_Y1	1
#define CLEAR_X2	2
#define CLEAR_Y2	3
#define CLEAR_DEPTH	4

typedef union drm_radeon_clear_rect {
	float f[5];
	unsigned int ui[5];
} drm_radeon_clear_rect_t;

typedef struct drm_radeon_clear {
	unsigned int flags;
	unsigned int clear_color;
	unsigned int clear_depth;
	unsigned int color_mask;
	unsigned int depth_mask;	/* misnamed field:  should be stencil */
	drm_radeon_clear_rect_t __user *depth_boxes;
} drm_radeon_clear_t;

typedef struct drm_radeon_vertex {
	int prim;
	int idx;		/* Index of vertex buffer */
	int count;		/* Number of vertices in buffer */
	int discard;		/* Client finished with buffer? */
} drm_radeon_vertex_t;

typedef struct drm_radeon_indices {
	int prim;
	int idx;
	int start;
	int end;
	int discard;		/* Client finished with buffer? */
} drm_radeon_indices_t;

/* v1.2 - obsoletes drm_radeon_vertex and drm_radeon_indices
 *      - allows multiple primitives and state changes in a single ioctl
 *      - supports driver change to emit native primitives
 */
typedef struct drm_radeon_vertex2 {
	int idx;		/* Index of vertex buffer */
	int discard;		/* Client finished with buffer? */
	int nr_states;
	drm_radeon_state_t __user *state;
	int nr_prims;
	drm_radeon_prim_t __user *prim;
} drm_radeon_vertex2_t;

/* v1.3 - obsoletes drm_radeon_vertex2
 *      - allows arbitarily large cliprect list
 *      - allows updating of tcl packet, vector and scalar state
 *      - allows memory-efficient description of state updates
 *      - allows state to be emitted without a primitive
 *           (for clears, ctx switches)
 *      - allows more than one dma buffer to be referenced per ioctl
 *      - supports tcl driver
 *      - may be extended in future versions with new cmd types, packets
 */
typedef struct drm_radeon_cmd_buffer {
	int bufsz;
	char __user *buf;
	int nbox;
	drm_clip_rect_t __user *boxes;
} drm_radeon_cmd_buffer_t;

typedef struct drm_radeon_tex_image {
	unsigned int x, y;	/* Blit coordinates */
	unsigned int width, height;
	const void __user *data;
} drm_radeon_tex_image_t;

typedef struct drm_radeon_texture {
	unsigned int offset;
	int pitch;
	int format;
	int width;		/* Texture image coordinates */
	int height;
	drm_radeon_tex_image_t __user *image;
} drm_radeon_texture_t;

typedef struct drm_radeon_stipple {
	unsigned int __user *mask;
} drm_radeon_stipple_t;

typedef struct drm_radeon_indirect {
	int idx;
	int start;
	int end;
	int discard;
} drm_radeon_indirect_t;

/* enum for card type parameters */
#define RADEON_CARD_PCI 0
#define RADEON_CARD_AGP 1
#define RADEON_CARD_PCIE 2

/* 1.3: An ioctl to get parameters that aren't available to the 3d
 * client any other way.
 */
#define RADEON_PARAM_GART_BUFFER_OFFSET    1	/* card offset of 1st GART buffer */
#define RADEON_PARAM_LAST_FRAME            2
#define RADEON_PARAM_LAST_DISPATCH         3
#define RADEON_PARAM_LAST_CLEAR            4
/* Added with DRM version 1.6. */
#define RADEON_PARAM_IRQ_NR                5
#define RADEON_PARAM_GART_BASE             6	/* card offset of GART base */
/* Added with DRM version 1.8. */
#define RADEON_PARAM_REGISTER_HANDLE       7	/* for drmMap() */
#define RADEON_PARAM_STATUS_HANDLE         8
#define RADEON_PARAM_SAREA_HANDLE          9
#define RADEON_PARAM_GART_TEX_HANDLE       10
#define RADEON_PARAM_SCRATCH_OFFSET        11
#define RADEON_PARAM_CARD_TYPE             12

typedef struct drm_radeon_getparam {
	int param;
	void __user *value;
} drm_radeon_getparam_t;

/* 1.6: Set up a memory manager for regions of shared memory:
 */
#define RADEON_MEM_REGION_GART 1
#define RADEON_MEM_REGION_FB   2

typedef struct drm_radeon_mem_alloc {
	int region;
	int alignment;
	int size;
	int __user *region_offset;	/* offset from start of fb or GART */
} drm_radeon_mem_alloc_t;

typedef struct drm_radeon_mem_free {
	int region;
	int region_offset;
} drm_radeon_mem_free_t;

typedef struct drm_radeon_mem_init_heap {
	int region;
	int size;
	int start;
} drm_radeon_mem_init_heap_t;

/* 1.6: Userspace can request & wait on irq's:
 */
typedef struct drm_radeon_irq_emit {
	int __user *irq_seq;
} drm_radeon_irq_emit_t;

typedef struct drm_radeon_irq_wait {
	int irq_seq;
} drm_radeon_irq_wait_t;

/* 1.10: Clients tell the DRM where they think the framebuffer is located in
 * the card's address space, via a new generic ioctl to set parameters
 */

typedef struct drm_radeon_setparam {
	unsigned int param;
	int64_t value;
} drm_radeon_setparam_t;

#define RADEON_SETPARAM_FB_LOCATION    1	/* determined framebuffer location */
#define RADEON_SETPARAM_SWITCH_TILING  2	/* enable/disable color tiling */
#define RADEON_SETPARAM_PCIGART_LOCATION 3	/* PCI Gart Location */
#define RADEON_SETPARAM_NEW_MEMMAP 4		/* Use new memory map */

/* 1.14: Clients can allocate/free a surface
 */
typedef struct drm_radeon_surface_alloc {
	unsigned int address;
	unsigned int size;
	unsigned int flags;
} drm_radeon_surface_alloc_t;

typedef struct drm_radeon_surface_free {
	unsigned int address;
} drm_radeon_surface_free_t;

#endif
