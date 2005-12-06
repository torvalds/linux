/**************************************************************************

Copyright (C) 2004-2005 Nicolai Haehnle et al.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifndef _R300_REG_H
#define _R300_REG_H

#define R300_MC_INIT_MISC_LAT_TIMER	0x180
#	define R300_MC_MISC__MC_CPR_INIT_LAT_SHIFT	0
#	define R300_MC_MISC__MC_VF_INIT_LAT_SHIFT	4
#	define R300_MC_MISC__MC_DISP0R_INIT_LAT_SHIFT	8
#	define R300_MC_MISC__MC_DISP1R_INIT_LAT_SHIFT	12
#	define R300_MC_MISC__MC_FIXED_INIT_LAT_SHIFT	16
#	define R300_MC_MISC__MC_E2R_INIT_LAT_SHIFT	20
#	define R300_MC_MISC__MC_SAME_PAGE_PRIO_SHIFT	24
#	define R300_MC_MISC__MC_GLOBW_INIT_LAT_SHIFT	28

#define R300_MC_INIT_GFX_LAT_TIMER	0x154
#	define R300_MC_MISC__MC_G3D0R_INIT_LAT_SHIFT	0
#	define R300_MC_MISC__MC_G3D1R_INIT_LAT_SHIFT	4
#	define R300_MC_MISC__MC_G3D2R_INIT_LAT_SHIFT	8
#	define R300_MC_MISC__MC_G3D3R_INIT_LAT_SHIFT	12
#	define R300_MC_MISC__MC_TX0R_INIT_LAT_SHIFT	16
#	define R300_MC_MISC__MC_TX1R_INIT_LAT_SHIFT	20
#	define R300_MC_MISC__MC_GLOBR_INIT_LAT_SHIFT	24
#	define R300_MC_MISC__MC_GLOBW_FULL_LAT_SHIFT	28

/*
This file contains registers and constants for the R300. They have been
found mostly by examining command buffers captured using glxtest, as well
as by extrapolating some known registers and constants from the R200.

I am fairly certain that they are correct unless stated otherwise in comments.
*/

#define R300_SE_VPORT_XSCALE                0x1D98
#define R300_SE_VPORT_XOFFSET               0x1D9C
#define R300_SE_VPORT_YSCALE                0x1DA0
#define R300_SE_VPORT_YOFFSET               0x1DA4
#define R300_SE_VPORT_ZSCALE                0x1DA8
#define R300_SE_VPORT_ZOFFSET               0x1DAC

/* This register is written directly and also starts data section in many 3d CP_PACKET3's */
#define R300_VAP_VF_CNTL	0x2084

#	define	R300_VAP_VF_CNTL__PRIM_TYPE__SHIFT                       0
#	define  R300_VAP_VF_CNTL__PRIM_NONE				 (0<<0)
#	define  R300_VAP_VF_CNTL__PRIM_POINTS				 (1<<0)
#	define  R300_VAP_VF_CNTL__PRIM_LINES				 (2<<0)
#	define  R300_VAP_VF_CNTL__PRIM_LINE_STRIP			 (3<<0)
#	define  R300_VAP_VF_CNTL__PRIM_TRIANGLES			 (4<<0)
#	define  R300_VAP_VF_CNTL__PRIM_TRIANGLE_FAN			 (5<<0)
#	define  R300_VAP_VF_CNTL__PRIM_TRIANGLE_STRIP			 (6<<0)
#	define  R300_VAP_VF_CNTL__PRIM_LINE_LOOP			 (12<<0)
#	define  R300_VAP_VF_CNTL__PRIM_QUADS			 	 (13<<0)
#	define  R300_VAP_VF_CNTL__PRIM_QUAD_STRIP			 (14<<0)
#	define  R300_VAP_VF_CNTL__PRIM_POLYGON			 	 (15<<0)

#	define	R300_VAP_VF_CNTL__PRIM_WALK__SHIFT                       4
	/* State based - direct writes to registers trigger vertex generation */
#	define	R300_VAP_VF_CNTL__PRIM_WALK_STATE_BASED                      (0<<4)
#	define	R300_VAP_VF_CNTL__PRIM_WALK_INDICES                          (1<<4)
#	define	R300_VAP_VF_CNTL__PRIM_WALK_VERTEX_LIST                      (2<<4)
#	define	R300_VAP_VF_CNTL__PRIM_WALK_VERTEX_EMBEDDED                  (3<<4)

		/* I don't think I saw these three used.. */
#	define	R300_VAP_VF_CNTL__COLOR_ORDER__SHIFT                     6
#	define	R300_VAP_VF_CNTL__TCL_OUTPUT_CTL_ENA__SHIFT              9
#	define	R300_VAP_VF_CNTL__PROG_STREAM_ENA__SHIFT                 10

		/* index size - when not set the indices are assumed to be 16 bit */
#	define	R300_VAP_VF_CNTL__INDEX_SIZE_32bit                      (1<<11)
		/* number of vertices */
#	define	R300_VAP_VF_CNTL__NUM_VERTICES__SHIFT                    16

/* BEGIN: Wild guesses */
#define R300_VAP_OUTPUT_VTX_FMT_0           0x2090
#       define R300_VAP_OUTPUT_VTX_FMT_0__POS_PRESENT     (1<<0)
#       define R300_VAP_OUTPUT_VTX_FMT_0__COLOR_PRESENT   (1<<1)
#       define R300_VAP_OUTPUT_VTX_FMT_0__COLOR_1_PRESENT (1<<2)	/* GUESS */
#       define R300_VAP_OUTPUT_VTX_FMT_0__COLOR_2_PRESENT (1<<3)	/* GUESS */
#       define R300_VAP_OUTPUT_VTX_FMT_0__COLOR_3_PRESENT (1<<4)	/* GUESS */
#       define R300_VAP_OUTPUT_VTX_FMT_0__PT_SIZE_PRESENT (1<<16)	/* GUESS */

#define R300_VAP_OUTPUT_VTX_FMT_1           0x2094
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_0_COMP_CNT_SHIFT 0
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_1_COMP_CNT_SHIFT 3
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_2_COMP_CNT_SHIFT 6
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_3_COMP_CNT_SHIFT 9
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_4_COMP_CNT_SHIFT 12
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_5_COMP_CNT_SHIFT 15
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_6_COMP_CNT_SHIFT 18
#       define R300_VAP_OUTPUT_VTX_FMT_1__TEX_7_COMP_CNT_SHIFT 21
/* END */

#define R300_SE_VTE_CNTL                  0x20b0
#	define     R300_VPORT_X_SCALE_ENA                0x00000001
#	define     R300_VPORT_X_OFFSET_ENA               0x00000002
#	define     R300_VPORT_Y_SCALE_ENA                0x00000004
#	define     R300_VPORT_Y_OFFSET_ENA               0x00000008
#	define     R300_VPORT_Z_SCALE_ENA                0x00000010
#	define     R300_VPORT_Z_OFFSET_ENA               0x00000020
#	define     R300_VTX_XY_FMT                       0x00000100
#	define     R300_VTX_Z_FMT                        0x00000200
#	define     R300_VTX_W0_FMT                       0x00000400
#	define     R300_VTX_W0_NORMALIZE                 0x00000800
#	define     R300_VTX_ST_DENORMALIZED              0x00001000

/* BEGIN: Vertex data assembly - lots of uncertainties */
/* gap */
/* Where do we get our vertex data?
//
// Vertex data either comes either from immediate mode registers or from
// vertex arrays.
// There appears to be no mixed mode (though we can force the pitch of
// vertex arrays to 0, effectively reusing the same element over and over
// again).
//
// Immediate mode is controlled by the INPUT_CNTL registers. I am not sure
// if these registers influence vertex array processing.
//
// Vertex arrays are controlled via the 3D_LOAD_VBPNTR packet3.
//
// In both cases, vertex attributes are then passed through INPUT_ROUTE.

// Beginning with INPUT_ROUTE_0_0 is a list of WORDs that route vertex data
// into the vertex processor's input registers.
// The first word routes the first input, the second word the second, etc.
// The corresponding input is routed into the register with the given index.
// The list is ended by a word with INPUT_ROUTE_END set.
//
// Always set COMPONENTS_4 in immediate mode. */

#define R300_VAP_INPUT_ROUTE_0_0            0x2150
#       define R300_INPUT_ROUTE_COMPONENTS_1     (0 << 0)
#       define R300_INPUT_ROUTE_COMPONENTS_2     (1 << 0)
#       define R300_INPUT_ROUTE_COMPONENTS_3     (2 << 0)
#       define R300_INPUT_ROUTE_COMPONENTS_4     (3 << 0)
#       define R300_INPUT_ROUTE_COMPONENTS_RGBA  (4 << 0)	/* GUESS */
#       define R300_VAP_INPUT_ROUTE_IDX_SHIFT    8
#       define R300_VAP_INPUT_ROUTE_IDX_MASK     (31 << 8)	/* GUESS */
#       define R300_VAP_INPUT_ROUTE_END          (1 << 13)
#       define R300_INPUT_ROUTE_IMMEDIATE_MODE   (0 << 14)	/* GUESS */
#       define R300_INPUT_ROUTE_FLOAT            (1 << 14)	/* GUESS */
#       define R300_INPUT_ROUTE_UNSIGNED_BYTE    (2 << 14)	/* GUESS */
#       define R300_INPUT_ROUTE_FLOAT_COLOR      (3 << 14)	/* GUESS */
#define R300_VAP_INPUT_ROUTE_0_1            0x2154
#define R300_VAP_INPUT_ROUTE_0_2            0x2158
#define R300_VAP_INPUT_ROUTE_0_3            0x215C
#define R300_VAP_INPUT_ROUTE_0_4            0x2160
#define R300_VAP_INPUT_ROUTE_0_5            0x2164
#define R300_VAP_INPUT_ROUTE_0_6            0x2168
#define R300_VAP_INPUT_ROUTE_0_7            0x216C

/* gap */
/* Notes:
//  - always set up to produce at least two attributes:
//    if vertex program uses only position, fglrx will set normal, too
//  - INPUT_CNTL_0_COLOR and INPUT_CNTL_COLOR bits are always equal */
#define R300_VAP_INPUT_CNTL_0               0x2180
#       define R300_INPUT_CNTL_0_COLOR           0x00000001
#define R300_VAP_INPUT_CNTL_1               0x2184
#       define R300_INPUT_CNTL_POS               0x00000001
#       define R300_INPUT_CNTL_NORMAL            0x00000002
#       define R300_INPUT_CNTL_COLOR             0x00000004
#       define R300_INPUT_CNTL_TC0               0x00000400
#       define R300_INPUT_CNTL_TC1               0x00000800
#       define R300_INPUT_CNTL_TC2               0x00001000	/* GUESS */
#       define R300_INPUT_CNTL_TC3               0x00002000	/* GUESS */
#       define R300_INPUT_CNTL_TC4               0x00004000	/* GUESS */
#       define R300_INPUT_CNTL_TC5               0x00008000	/* GUESS */
#       define R300_INPUT_CNTL_TC6               0x00010000	/* GUESS */
#       define R300_INPUT_CNTL_TC7               0x00020000	/* GUESS */

/* gap */
/* Words parallel to INPUT_ROUTE_0; All words that are active in INPUT_ROUTE_0
// are set to a swizzling bit pattern, other words are 0.
//
// In immediate mode, the pattern is always set to xyzw. In vertex array
// mode, the swizzling pattern is e.g. used to set zw components in texture
// coordinates with only tweo components. */
#define R300_VAP_INPUT_ROUTE_1_0            0x21E0
#       define R300_INPUT_ROUTE_SELECT_X    0
#       define R300_INPUT_ROUTE_SELECT_Y    1
#       define R300_INPUT_ROUTE_SELECT_Z    2
#       define R300_INPUT_ROUTE_SELECT_W    3
#       define R300_INPUT_ROUTE_SELECT_ZERO 4
#       define R300_INPUT_ROUTE_SELECT_ONE  5
#       define R300_INPUT_ROUTE_SELECT_MASK 7
#       define R300_INPUT_ROUTE_X_SHIFT          0
#       define R300_INPUT_ROUTE_Y_SHIFT          3
#       define R300_INPUT_ROUTE_Z_SHIFT          6
#       define R300_INPUT_ROUTE_W_SHIFT          9
#       define R300_INPUT_ROUTE_ENABLE           (15 << 12)
#define R300_VAP_INPUT_ROUTE_1_1            0x21E4
#define R300_VAP_INPUT_ROUTE_1_2            0x21E8
#define R300_VAP_INPUT_ROUTE_1_3            0x21EC
#define R300_VAP_INPUT_ROUTE_1_4            0x21F0
#define R300_VAP_INPUT_ROUTE_1_5            0x21F4
#define R300_VAP_INPUT_ROUTE_1_6            0x21F8
#define R300_VAP_INPUT_ROUTE_1_7            0x21FC

/* END */

/* gap */
/* BEGIN: Upload vertex program and data
// The programmable vertex shader unit has a memory bank of unknown size
// that can be written to in 16 byte units by writing the address into
// UPLOAD_ADDRESS, followed by data in UPLOAD_DATA (multiples of 4 DWORDs).
//
// Pointers into the memory bank are always in multiples of 16 bytes.
//
// The memory bank is divided into areas with fixed meaning.
//
// Starting at address UPLOAD_PROGRAM: Vertex program instructions.
// Native limits reported by drivers from ATI suggest size 256 (i.e. 4KB),
// whereas the difference between known addresses suggests size 512.
//
// Starting at address UPLOAD_PARAMETERS: Vertex program parameters.
// Native reported limits and the VPI layout suggest size 256, whereas
// difference between known addresses suggests size 512.
//
// At address UPLOAD_POINTSIZE is a vector (0, 0, ps, 0), where ps is the
// floating point pointsize. The exact purpose of this state is uncertain,
// as there is also the R300_RE_POINTSIZE register.
//
// Multiple vertex programs and parameter sets can be loaded at once,
// which could explain the size discrepancy. */
#define R300_VAP_PVS_UPLOAD_ADDRESS         0x2200
#       define R300_PVS_UPLOAD_PROGRAM           0x00000000
#       define R300_PVS_UPLOAD_PARAMETERS        0x00000200
#       define R300_PVS_UPLOAD_POINTSIZE         0x00000406
/* gap */
#define R300_VAP_PVS_UPLOAD_DATA            0x2208
/* END */

/* gap */
/* I do not know the purpose of this register. However, I do know that
// it is set to 221C_CLEAR for clear operations and to 221C_NORMAL
// for normal rendering. */
#define R300_VAP_UNKNOWN_221C               0x221C
#       define R300_221C_NORMAL                  0x00000000
#       define R300_221C_CLEAR                   0x0001C000

/* gap */
/* Sometimes, END_OF_PKT and 0x2284=0 are the only commands sent between
// rendering commands and overwriting vertex program parameters.
// Therefore, I suspect writing zero to 0x2284 synchronizes the engine and
// avoids bugs caused by still running shaders reading bad data from memory. */
#define R300_VAP_PVS_WAITIDLE               0x2284	/* GUESS */

/* Absolutely no clue what this register is about. */
#define R300_VAP_UNKNOWN_2288               0x2288
#       define R300_2288_R300                    0x00750000	/* -- nh */
#       define R300_2288_RV350                   0x0000FFFF	/* -- Vladimir */

/* gap */
/* Addresses are relative to the vertex program instruction area of the
// memory bank. PROGRAM_END points to the last instruction of the active
// program
//
// The meaning of the two UNKNOWN fields is obviously not known. However,
// experiments so far have shown that both *must* point to an instruction
// inside the vertex program, otherwise the GPU locks up.
// fglrx usually sets CNTL_3_UNKNOWN to the end of the program and
// CNTL_1_UNKNOWN points to instruction where last write to position takes place.
// Most likely this is used to ignore rest of the program in cases where group of verts arent visible.
// For some reason this "section" is sometimes accepted other instruction that have
// no relationship with position calculations.
*/
#define R300_VAP_PVS_CNTL_1                 0x22D0
#       define R300_PVS_CNTL_1_PROGRAM_START_SHIFT   0
#       define R300_PVS_CNTL_1_POS_END_SHIFT         10
#       define R300_PVS_CNTL_1_PROGRAM_END_SHIFT     20
/* Addresses are relative the the vertex program parameters area. */
#define R300_VAP_PVS_CNTL_2                 0x22D4
#       define R300_PVS_CNTL_2_PARAM_OFFSET_SHIFT 0
#       define R300_PVS_CNTL_2_PARAM_COUNT_SHIFT  16
#define R300_VAP_PVS_CNTL_3	           0x22D8
#       define R300_PVS_CNTL_3_PROGRAM_UNKNOWN_SHIFT 10
#       define R300_PVS_CNTL_3_PROGRAM_UNKNOWN2_SHIFT 0

/* The entire range from 0x2300 to 0x2AC inclusive seems to be used for
// immediate vertices */
#define R300_VAP_VTX_COLOR_R                0x2464
#define R300_VAP_VTX_COLOR_G                0x2468
#define R300_VAP_VTX_COLOR_B                0x246C
#define R300_VAP_VTX_POS_0_X_1              0x2490	/* used for glVertex2*() */
#define R300_VAP_VTX_POS_0_Y_1              0x2494
#define R300_VAP_VTX_COLOR_PKD              0x249C	/* RGBA */
#define R300_VAP_VTX_POS_0_X_2              0x24A0	/* used for glVertex3*() */
#define R300_VAP_VTX_POS_0_Y_2              0x24A4
#define R300_VAP_VTX_POS_0_Z_2              0x24A8
#define R300_VAP_VTX_END_OF_PKT             0x24AC	/* write 0 to indicate end of packet? */

/* gap */

/* These are values from r300_reg/r300_reg.h - they are known to be correct
   and are here so we can use one register file instead of several
   - Vladimir */
#define R300_GB_VAP_RASTER_VTX_FMT_0	0x4000
#	define R300_GB_VAP_RASTER_VTX_FMT_0__POS_PRESENT	(1<<0)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__COLOR_0_PRESENT	(1<<1)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__COLOR_1_PRESENT	(1<<2)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__COLOR_2_PRESENT	(1<<3)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__COLOR_3_PRESENT	(1<<4)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__COLOR_SPACE	(0xf<<5)
#	define R300_GB_VAP_RASTER_VTX_FMT_0__PT_SIZE_PRESENT	(0x1<<16)

#define R300_GB_VAP_RASTER_VTX_FMT_1	0x4004
	/* each of the following is 3 bits wide, specifies number
	   of components */
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_0_COMP_CNT_SHIFT	0
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_1_COMP_CNT_SHIFT	3
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_2_COMP_CNT_SHIFT	6
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_3_COMP_CNT_SHIFT	9
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_4_COMP_CNT_SHIFT	12
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_5_COMP_CNT_SHIFT	15
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_6_COMP_CNT_SHIFT	18
#	define R300_GB_VAP_RASTER_VTX_FMT_1__TEX_7_COMP_CNT_SHIFT	21

/* UNK30 seems to enables point to quad transformation on textures
   (or something closely related to that).
   This bit is rather fatal at the time being due to lackings at pixel shader side */
#define R300_GB_ENABLE	0x4008
#	define R300_GB_POINT_STUFF_ENABLE	(1<<0)
#	define R300_GB_LINE_STUFF_ENABLE	(1<<1)
#	define R300_GB_TRIANGLE_STUFF_ENABLE	(1<<2)
#	define R300_GB_STENCIL_AUTO_ENABLE	(1<<4)
#	define R300_GB_UNK30			(1<<30)
	/* each of the following is 2 bits wide */
#define R300_GB_TEX_REPLICATE	0
#define R300_GB_TEX_ST		1
#define R300_GB_TEX_STR		2
#	define R300_GB_TEX0_SOURCE_SHIFT	16
#	define R300_GB_TEX1_SOURCE_SHIFT	18
#	define R300_GB_TEX2_SOURCE_SHIFT	20
#	define R300_GB_TEX3_SOURCE_SHIFT	22
#	define R300_GB_TEX4_SOURCE_SHIFT	24
#	define R300_GB_TEX5_SOURCE_SHIFT	26
#	define R300_GB_TEX6_SOURCE_SHIFT	28
#	define R300_GB_TEX7_SOURCE_SHIFT	30

/* MSPOS - positions for multisample antialiasing (?) */
#define R300_GB_MSPOS0	0x4010
	/* shifts - each of the fields is 4 bits */
#	define R300_GB_MSPOS0__MS_X0_SHIFT	0
#	define R300_GB_MSPOS0__MS_Y0_SHIFT	4
#	define R300_GB_MSPOS0__MS_X1_SHIFT	8
#	define R300_GB_MSPOS0__MS_Y1_SHIFT	12
#	define R300_GB_MSPOS0__MS_X2_SHIFT	16
#	define R300_GB_MSPOS0__MS_Y2_SHIFT	20
#	define R300_GB_MSPOS0__MSBD0_Y		24
#	define R300_GB_MSPOS0__MSBD0_X		28

#define R300_GB_MSPOS1	0x4014
#	define R300_GB_MSPOS1__MS_X3_SHIFT	0
#	define R300_GB_MSPOS1__MS_Y3_SHIFT	4
#	define R300_GB_MSPOS1__MS_X4_SHIFT	8
#	define R300_GB_MSPOS1__MS_Y4_SHIFT	12
#	define R300_GB_MSPOS1__MS_X5_SHIFT	16
#	define R300_GB_MSPOS1__MS_Y5_SHIFT	20
#	define R300_GB_MSPOS1__MSBD1		24

#define R300_GB_TILE_CONFIG	0x4018
#	define R300_GB_TILE_ENABLE	(1<<0)
#	define R300_GB_TILE_PIPE_COUNT_RV300	0
#	define R300_GB_TILE_PIPE_COUNT_R300	(3<<1)
#	define R300_GB_TILE_PIPE_COUNT_R420	(7<<1)
#	define R300_GB_TILE_SIZE_8		0
#	define R300_GB_TILE_SIZE_16		(1<<4)
#	define R300_GB_TILE_SIZE_32		(2<<4)
#	define R300_GB_SUPER_SIZE_1		(0<<6)
#	define R300_GB_SUPER_SIZE_2		(1<<6)
#	define R300_GB_SUPER_SIZE_4		(2<<6)
#	define R300_GB_SUPER_SIZE_8		(3<<6)
#	define R300_GB_SUPER_SIZE_16		(4<<6)
#	define R300_GB_SUPER_SIZE_32		(5<<6)
#	define R300_GB_SUPER_SIZE_64		(6<<6)
#	define R300_GB_SUPER_SIZE_128		(7<<6)
#	define R300_GB_SUPER_X_SHIFT		9	/* 3 bits wide */
#	define R300_GB_SUPER_Y_SHIFT		12	/* 3 bits wide */
#	define R300_GB_SUPER_TILE_A		0
#	define R300_GB_SUPER_TILE_B		(1<<15)
#	define R300_GB_SUBPIXEL_1_12		0
#	define R300_GB_SUBPIXEL_1_16		(1<<16)

#define R300_GB_FIFO_SIZE	0x4024
	/* each of the following is 2 bits wide */
#define R300_GB_FIFO_SIZE_32	0
#define R300_GB_FIFO_SIZE_64	1
#define R300_GB_FIFO_SIZE_128	2
#define R300_GB_FIFO_SIZE_256	3
#	define R300_SC_IFIFO_SIZE_SHIFT	0
#	define R300_SC_TZFIFO_SIZE_SHIFT	2
#	define R300_SC_BFIFO_SIZE_SHIFT	4

#	define R300_US_OFIFO_SIZE_SHIFT	12
#	define R300_US_WFIFO_SIZE_SHIFT	14
	/* the following use the same constants as above, but meaning is
	   is times 2 (i.e. instead of 32 words it means 64 */
#	define R300_RS_TFIFO_SIZE_SHIFT	6
#	define R300_RS_CFIFO_SIZE_SHIFT	8
#	define R300_US_RAM_SIZE_SHIFT		10
	/* watermarks, 3 bits wide */
#	define R300_RS_HIGHWATER_COL_SHIFT	16
#	define R300_RS_HIGHWATER_TEX_SHIFT	19
#	define R300_OFIFO_HIGHWATER_SHIFT	22	/* two bits only */
#	define R300_CUBE_FIFO_HIGHWATER_COL_SHIFT	24

#define R300_GB_SELECT	0x401C
#	define R300_GB_FOG_SELECT_C0A		0
#	define R300_GB_FOG_SELECT_C1A		1
#	define R300_GB_FOG_SELECT_C2A		2
#	define R300_GB_FOG_SELECT_C3A		3
#	define R300_GB_FOG_SELECT_1_1_W	4
#	define R300_GB_FOG_SELECT_Z		5
#	define R300_GB_DEPTH_SELECT_Z		0
#	define R300_GB_DEPTH_SELECT_1_1_W	(1<<3)
#	define R300_GB_W_SELECT_1_W		0
#	define R300_GB_W_SELECT_1		(1<<4)

#define R300_GB_AA_CONFIG		0x4020
#	define R300_AA_ENABLE			0x01
#	define R300_AA_SUBSAMPLES_2		0
#	define R300_AA_SUBSAMPLES_3		(1<<1)
#	define R300_AA_SUBSAMPLES_4		(2<<1)
#	define R300_AA_SUBSAMPLES_6		(3<<1)

/* END */

/* gap */
/* The upper enable bits are guessed, based on fglrx reported limits. */
#define R300_TX_ENABLE                      0x4104
#       define R300_TX_ENABLE_0                  (1 << 0)
#       define R300_TX_ENABLE_1                  (1 << 1)
#       define R300_TX_ENABLE_2                  (1 << 2)
#       define R300_TX_ENABLE_3                  (1 << 3)
#       define R300_TX_ENABLE_4                  (1 << 4)
#       define R300_TX_ENABLE_5                  (1 << 5)
#       define R300_TX_ENABLE_6                  (1 << 6)
#       define R300_TX_ENABLE_7                  (1 << 7)
#       define R300_TX_ENABLE_8                  (1 << 8)
#       define R300_TX_ENABLE_9                  (1 << 9)
#       define R300_TX_ENABLE_10                 (1 << 10)
#       define R300_TX_ENABLE_11                 (1 << 11)
#       define R300_TX_ENABLE_12                 (1 << 12)
#       define R300_TX_ENABLE_13                 (1 << 13)
#       define R300_TX_ENABLE_14                 (1 << 14)
#       define R300_TX_ENABLE_15                 (1 << 15)

/* The pointsize is given in multiples of 6. The pointsize can be
// enormous: Clear() renders a single point that fills the entire
// framebuffer. */
#define R300_RE_POINTSIZE                   0x421C
#       define R300_POINTSIZE_Y_SHIFT            0
#       define R300_POINTSIZE_Y_MASK             (0xFFFF << 0)	/* GUESS */
#       define R300_POINTSIZE_X_SHIFT            16
#       define R300_POINTSIZE_X_MASK             (0xFFFF << 16)	/* GUESS */
#       define R300_POINTSIZE_MAX             (R300_POINTSIZE_Y_MASK / 6)

/* The line width is given in multiples of 6.
   In default mode lines are classified as vertical lines.
   HO: horizontal
   VE: vertical or horizontal
   HO & VE: no classification
*/
#define R300_RE_LINE_CNT                      0x4234
#       define R300_LINESIZE_SHIFT            0
#       define R300_LINESIZE_MASK             (0xFFFF << 0)	/* GUESS */
#       define R300_LINESIZE_MAX             (R300_LINESIZE_MASK / 6)
#       define R300_LINE_CNT_HO               (1 << 16)
#       define R300_LINE_CNT_VE               (1 << 17)

/* Some sort of scale or clamp value for texcoordless textures. */
#define R300_RE_UNK4238                       0x4238

#define R300_RE_SHADE_MODEL                   0x4278
#	define R300_RE_SHADE_MODEL_SMOOTH     0x3aaaa
#	define R300_RE_SHADE_MODEL_FLAT       0x39595

/* Dangerous */
#define R300_RE_POLYGON_MODE                  0x4288
#	define R300_PM_ENABLED                (1 << 0)
#	define R300_PM_FRONT_POINT            (0 << 0)
#	define R300_PM_BACK_POINT             (0 << 0)
#	define R300_PM_FRONT_LINE             (1 << 4)
#	define R300_PM_FRONT_FILL             (1 << 5)
#	define R300_PM_BACK_LINE              (1 << 7)
#	define R300_PM_BACK_FILL              (1 << 8)

/* Not sure why there are duplicate of factor and constant values.
   My best guess so far is that there are seperate zbiases for test and write.
   Ordering might be wrong.
   Some of the tests indicate that fgl has a fallback implementation of zbias
   via pixel shaders. */
#define R300_RE_ZBIAS_T_FACTOR                0x42A4
#define R300_RE_ZBIAS_T_CONSTANT              0x42A8
#define R300_RE_ZBIAS_W_FACTOR                0x42AC
#define R300_RE_ZBIAS_W_CONSTANT              0x42B0

/* This register needs to be set to (1<<1) for RV350 to correctly
   perform depth test (see --vb-triangles in r300_demo)
   Don't know about other chips. - Vladimir
   This is set to 3 when GL_POLYGON_OFFSET_FILL is on.
   My guess is that there are two bits for each zbias primitive (FILL, LINE, POINT).
   One to enable depth test and one for depth write.
   Yet this doesnt explain why depth writes work ...
    */
#define R300_RE_OCCLUSION_CNTL		    0x42B4
#	define R300_OCCLUSION_ON		(1<<1)

#define R300_RE_CULL_CNTL                   0x42B8
#       define R300_CULL_FRONT                   (1 << 0)
#       define R300_CULL_BACK                    (1 << 1)
#       define R300_FRONT_FACE_CCW               (0 << 2)
#       define R300_FRONT_FACE_CW                (1 << 2)

/* BEGIN: Rasterization / Interpolators - many guesses
// 0_UNKNOWN_18 has always been set except for clear operations.
// TC_CNT is the number of incoming texture coordinate sets (i.e. it depends
// on the vertex program, *not* the fragment program) */
#define R300_RS_CNTL_0                      0x4300
#       define R300_RS_CNTL_TC_CNT_SHIFT         2
#       define R300_RS_CNTL_TC_CNT_MASK          (7 << 2)
#		define R300_RS_CNTL_CI_CNT_SHIFT         7	/* number of color interpolators used */
#       define R300_RS_CNTL_0_UNKNOWN_18         (1 << 18)
/* Guess: RS_CNTL_1 holds the index of the highest used RS_ROUTE_n register. */
#define R300_RS_CNTL_1                      0x4304

/* gap */
/* Only used for texture coordinates.
// Use the source field to route texture coordinate input from the vertex program
// to the desired interpolator. Note that the source field is relative to the
// outputs the vertex program *actually* writes. If a vertex program only writes
// texcoord[1], this will be source index 0.
// Set INTERP_USED on all interpolators that produce data used by the
// fragment program. INTERP_USED looks like a swizzling mask, but
// I haven't seen it used that way.
//
// Note: The _UNKNOWN constants are always set in their respective register.
// I don't know if this is necessary. */
#define R300_RS_INTERP_0                    0x4310
#define R300_RS_INTERP_1                    0x4314
#       define R300_RS_INTERP_1_UNKNOWN          0x40
#define R300_RS_INTERP_2                    0x4318
#       define R300_RS_INTERP_2_UNKNOWN          0x80
#define R300_RS_INTERP_3                    0x431C
#       define R300_RS_INTERP_3_UNKNOWN          0xC0
#define R300_RS_INTERP_4                    0x4320
#define R300_RS_INTERP_5                    0x4324
#define R300_RS_INTERP_6                    0x4328
#define R300_RS_INTERP_7                    0x432C
#       define R300_RS_INTERP_SRC_SHIFT          2
#       define R300_RS_INTERP_SRC_MASK           (7 << 2)
#       define R300_RS_INTERP_USED               0x00D10000

/* These DWORDs control how vertex data is routed into fragment program
// registers, after interpolators. */
#define R300_RS_ROUTE_0                     0x4330
#define R300_RS_ROUTE_1                     0x4334
#define R300_RS_ROUTE_2                     0x4338
#define R300_RS_ROUTE_3                     0x433C	/* GUESS */
#define R300_RS_ROUTE_4                     0x4340	/* GUESS */
#define R300_RS_ROUTE_5                     0x4344	/* GUESS */
#define R300_RS_ROUTE_6                     0x4348	/* GUESS */
#define R300_RS_ROUTE_7                     0x434C	/* GUESS */
#       define R300_RS_ROUTE_SOURCE_INTERP_0     0
#       define R300_RS_ROUTE_SOURCE_INTERP_1     1
#       define R300_RS_ROUTE_SOURCE_INTERP_2     2
#       define R300_RS_ROUTE_SOURCE_INTERP_3     3
#       define R300_RS_ROUTE_SOURCE_INTERP_4     4
#       define R300_RS_ROUTE_SOURCE_INTERP_5     5	/* GUESS */
#       define R300_RS_ROUTE_SOURCE_INTERP_6     6	/* GUESS */
#       define R300_RS_ROUTE_SOURCE_INTERP_7     7	/* GUESS */
#       define R300_RS_ROUTE_ENABLE              (1 << 3)	/* GUESS */
#       define R300_RS_ROUTE_DEST_SHIFT          6
#       define R300_RS_ROUTE_DEST_MASK           (31 << 6)	/* GUESS */

/* Special handling for color: When the fragment program uses color,
// the ROUTE_0_COLOR bit is set and ROUTE_0_COLOR_DEST contains the
// color register index. */
#       define R300_RS_ROUTE_0_COLOR             (1 << 14)
#       define R300_RS_ROUTE_0_COLOR_DEST_SHIFT  17
#       define R300_RS_ROUTE_0_COLOR_DEST_MASK   (31 << 17)	/* GUESS */
/* As above, but for secondary color */
#		define R300_RS_ROUTE_1_COLOR1            (1 << 14)
#		define R300_RS_ROUTE_1_COLOR1_DEST_SHIFT 17
#		define R300_RS_ROUTE_1_COLOR1_DEST_MASK  (31 << 17)
#		define R300_RS_ROUTE_1_UNKNOWN11         (1 << 11)
/* END */

/* BEGIN: Scissors and cliprects
// There are four clipping rectangles. Their corner coordinates are inclusive.
// Every pixel is assigned a number from 0 and 15 by setting bits 0-3 depending
// on whether the pixel is inside cliprects 0-3, respectively. For example,
// if a pixel is inside cliprects 0 and 1, but outside 2 and 3, it is assigned
// the number 3 (binary 0011).
// Iff the bit corresponding to the pixel's number in RE_CLIPRECT_CNTL is set,
// the pixel is rasterized.
//
// In addition to this, there is a scissors rectangle. Only pixels inside the
// scissors rectangle are drawn. (coordinates are inclusive)
//
// For some reason, the top-left corner of the framebuffer is at (1440, 1440)
// for the purpose of clipping and scissors. */
#define R300_RE_CLIPRECT_TL_0               0x43B0
#define R300_RE_CLIPRECT_BR_0               0x43B4
#define R300_RE_CLIPRECT_TL_1               0x43B8
#define R300_RE_CLIPRECT_BR_1               0x43BC
#define R300_RE_CLIPRECT_TL_2               0x43C0
#define R300_RE_CLIPRECT_BR_2               0x43C4
#define R300_RE_CLIPRECT_TL_3               0x43C8
#define R300_RE_CLIPRECT_BR_3               0x43CC
#       define R300_CLIPRECT_OFFSET              1440
#       define R300_CLIPRECT_MASK                0x1FFF
#       define R300_CLIPRECT_X_SHIFT             0
#       define R300_CLIPRECT_X_MASK              (0x1FFF << 0)
#       define R300_CLIPRECT_Y_SHIFT             13
#       define R300_CLIPRECT_Y_MASK              (0x1FFF << 13)
#define R300_RE_CLIPRECT_CNTL               0x43D0
#       define R300_CLIP_OUT                     (1 << 0)
#       define R300_CLIP_0                       (1 << 1)
#       define R300_CLIP_1                       (1 << 2)
#       define R300_CLIP_10                      (1 << 3)
#       define R300_CLIP_2                       (1 << 4)
#       define R300_CLIP_20                      (1 << 5)
#       define R300_CLIP_21                      (1 << 6)
#       define R300_CLIP_210                     (1 << 7)
#       define R300_CLIP_3                       (1 << 8)
#       define R300_CLIP_30                      (1 << 9)
#       define R300_CLIP_31                      (1 << 10)
#       define R300_CLIP_310                     (1 << 11)
#       define R300_CLIP_32                      (1 << 12)
#       define R300_CLIP_320                     (1 << 13)
#       define R300_CLIP_321                     (1 << 14)
#       define R300_CLIP_3210                    (1 << 15)

/* gap */
#define R300_RE_SCISSORS_TL                 0x43E0
#define R300_RE_SCISSORS_BR                 0x43E4
#       define R300_SCISSORS_OFFSET              1440
#       define R300_SCISSORS_X_SHIFT             0
#       define R300_SCISSORS_X_MASK              (0x1FFF << 0)
#       define R300_SCISSORS_Y_SHIFT             13
#       define R300_SCISSORS_Y_MASK              (0x1FFF << 13)
/* END */

/* BEGIN: Texture specification
// The texture specification dwords are grouped by meaning and not by texture unit.
// This means that e.g. the offset for texture image unit N is found in register
// TX_OFFSET_0 + (4*N) */
#define R300_TX_FILTER_0                    0x4400
#       define R300_TX_REPEAT                    0
#       define R300_TX_MIRRORED                  1
#       define R300_TX_CLAMP                     4
#       define R300_TX_CLAMP_TO_EDGE             2
#       define R300_TX_CLAMP_TO_BORDER           6
#       define R300_TX_WRAP_S_SHIFT              0
#       define R300_TX_WRAP_S_MASK               (7 << 0)
#       define R300_TX_WRAP_T_SHIFT              3
#       define R300_TX_WRAP_T_MASK               (7 << 3)
#       define R300_TX_WRAP_Q_SHIFT              6
#       define R300_TX_WRAP_Q_MASK               (7 << 6)
#       define R300_TX_MAG_FILTER_NEAREST        (1 << 9)
#       define R300_TX_MAG_FILTER_LINEAR         (2 << 9)
#       define R300_TX_MAG_FILTER_MASK           (3 << 9)
#       define R300_TX_MIN_FILTER_NEAREST        (1 << 11)
#       define R300_TX_MIN_FILTER_LINEAR         (2 << 11)
#	define R300_TX_MIN_FILTER_NEAREST_MIP_NEAREST       (5  <<  11)
#	define R300_TX_MIN_FILTER_NEAREST_MIP_LINEAR        (9  <<  11)
#	define R300_TX_MIN_FILTER_LINEAR_MIP_NEAREST        (6  <<  11)
#	define R300_TX_MIN_FILTER_LINEAR_MIP_LINEAR         (10 <<  11)

/* NOTE: NEAREST doesnt seem to exist.
   Im not seting MAG_FILTER_MASK and (3 << 11) on for all
   anisotropy modes because that would void selected mag filter */
#	define R300_TX_MIN_FILTER_ANISO_NEAREST             ((0 << 13) /*|R300_TX_MAG_FILTER_MASK|(3<<11)*/)
#	define R300_TX_MIN_FILTER_ANISO_LINEAR              ((0 << 13) /*|R300_TX_MAG_FILTER_MASK|(3<<11)*/)
#	define R300_TX_MIN_FILTER_ANISO_NEAREST_MIP_NEAREST ((1 << 13) /*|R300_TX_MAG_FILTER_MASK|(3<<11)*/)
#	define R300_TX_MIN_FILTER_ANISO_NEAREST_MIP_LINEAR  ((2 << 13) /*|R300_TX_MAG_FILTER_MASK|(3<<11)*/)
#       define R300_TX_MIN_FILTER_MASK           ( (15 << 11) | (3 << 13) )
#	define R300_TX_MAX_ANISO_1_TO_1  (0 << 21)
#	define R300_TX_MAX_ANISO_2_TO_1  (2 << 21)
#	define R300_TX_MAX_ANISO_4_TO_1  (4 << 21)
#	define R300_TX_MAX_ANISO_8_TO_1  (6 << 21)
#	define R300_TX_MAX_ANISO_16_TO_1 (8 << 21)
#	define R300_TX_MAX_ANISO_MASK    (14 << 21)

#define R300_TX_UNK1_0                      0x4440
#	define R300_LOD_BIAS_MASK	    0x1fff

#define R300_TX_SIZE_0                      0x4480
#       define R300_TX_WIDTHMASK_SHIFT           0
#       define R300_TX_WIDTHMASK_MASK            (2047 << 0)
#       define R300_TX_HEIGHTMASK_SHIFT          11
#       define R300_TX_HEIGHTMASK_MASK           (2047 << 11)
#       define R300_TX_UNK23                     (1 << 23)
#       define R300_TX_SIZE_SHIFT                26	/* largest of width, height */
#       define R300_TX_SIZE_MASK                 (15 << 26)
#define R300_TX_FORMAT_0                    0x44C0
	/* The interpretation of the format word by Wladimir van der Laan */
	/* The X, Y, Z and W refer to the layout of the components.
	   They are given meanings as R, G, B and Alpha by the swizzle
	   specification */
#	define R300_TX_FORMAT_X8		    0x0
#	define R300_TX_FORMAT_X16		    0x1
#	define R300_TX_FORMAT_Y4X4		    0x2
#	define R300_TX_FORMAT_Y8X8		    0x3
#	define R300_TX_FORMAT_Y16X16		    0x4
#	define R300_TX_FORMAT_Z3Y3X2		    0x5
#	define R300_TX_FORMAT_Z5Y6X5		    0x6
#	define R300_TX_FORMAT_Z6Y5X5		    0x7
#	define R300_TX_FORMAT_Z11Y11X10		    0x8
#	define R300_TX_FORMAT_Z10Y11X11		    0x9
#	define R300_TX_FORMAT_W4Z4Y4X4		    0xA
#	define R300_TX_FORMAT_W1Z5Y5X5		    0xB
#	define R300_TX_FORMAT_W8Z8Y8X8		    0xC
#	define R300_TX_FORMAT_W2Z10Y10X10	    0xD
#	define R300_TX_FORMAT_W16Z16Y16X16	    0xE
#	define R300_TX_FORMAT_DXT1	    	    0xF
#	define R300_TX_FORMAT_DXT3	    	    0x10
#	define R300_TX_FORMAT_DXT5	    	    0x11
#	define R300_TX_FORMAT_D3DMFT_CxV8U8	    0x12	/* no swizzle */
#	define R300_TX_FORMAT_A8R8G8B8	    	    0x13	/* no swizzle */
#	define R300_TX_FORMAT_B8G8_B8G8	    	    0x14	/* no swizzle */
#	define R300_TX_FORMAT_G8R8_G8B8	    	    0x15	/* no swizzle */
						  /* 0x16 - some 16 bit green format.. ?? */
#	define R300_TX_FORMAT_UNK25		   (1 << 25)	/* no swizzle */

	/* gap */
	/* Floating point formats */
	/* Note - hardware supports both 16 and 32 bit floating point */
#	define R300_TX_FORMAT_FL_I16	    	    0x18
#	define R300_TX_FORMAT_FL_I16A16	    	    0x19
#	define R300_TX_FORMAT_FL_R16G16B16A16	    0x1A
#	define R300_TX_FORMAT_FL_I32	    	    0x1B
#	define R300_TX_FORMAT_FL_I32A32	    	    0x1C
#	define R300_TX_FORMAT_FL_R32G32B32A32	    0x1D
	/* alpha modes, convenience mostly */
	/* if you have alpha, pick constant appropriate to the
	   number of channels (1 for I8, 2 for I8A8, 4 for R8G8B8A8, etc */
# 	define R300_TX_FORMAT_ALPHA_1CH		    0x000
# 	define R300_TX_FORMAT_ALPHA_2CH		    0x200
# 	define R300_TX_FORMAT_ALPHA_4CH		    0x600
# 	define R300_TX_FORMAT_ALPHA_NONE	    0xA00
	/* Swizzling */
	/* constants */
#	define R300_TX_FORMAT_X		0
#	define R300_TX_FORMAT_Y		1
#	define R300_TX_FORMAT_Z		2
#	define R300_TX_FORMAT_W		3
#	define R300_TX_FORMAT_ZERO	4
#	define R300_TX_FORMAT_ONE	5
#	define R300_TX_FORMAT_CUT_Z	6	/* 2.0*Z, everything above 1.0 is set to 0.0 */
#	define R300_TX_FORMAT_CUT_W	7	/* 2.0*W, everything above 1.0 is set to 0.0 */

#	define R300_TX_FORMAT_B_SHIFT	18
#	define R300_TX_FORMAT_G_SHIFT	15
#	define R300_TX_FORMAT_R_SHIFT	12
#	define R300_TX_FORMAT_A_SHIFT	9
	/* Convenience macro to take care of layout and swizzling */
#	define R300_EASY_TX_FORMAT(B, G, R, A, FMT)	(\
	  ((R300_TX_FORMAT_##B)<<R300_TX_FORMAT_B_SHIFT) \
	| ((R300_TX_FORMAT_##G)<<R300_TX_FORMAT_G_SHIFT) \
	| ((R300_TX_FORMAT_##R)<<R300_TX_FORMAT_R_SHIFT) \
	| ((R300_TX_FORMAT_##A)<<R300_TX_FORMAT_A_SHIFT) \
	| (R300_TX_FORMAT_##FMT) \
	  )
	/* These can be ORed with result of R300_EASY_TX_FORMAT() */
	/* We don't really know what they do. Take values from a constant color ? */
#	define R300_TX_FORMAT_CONST_X		(1<<5)
#	define R300_TX_FORMAT_CONST_Y		(2<<5)
#	define R300_TX_FORMAT_CONST_Z		(4<<5)
#	define R300_TX_FORMAT_CONST_W		(8<<5)

#	define R300_TX_FORMAT_YUV_MODE		0x00800000

#define R300_TX_OFFSET_0                    0x4540
/* BEGIN: Guess from R200 */
#       define R300_TXO_ENDIAN_NO_SWAP           (0 << 0)
#       define R300_TXO_ENDIAN_BYTE_SWAP         (1 << 0)
#       define R300_TXO_ENDIAN_WORD_SWAP         (2 << 0)
#       define R300_TXO_ENDIAN_HALFDW_SWAP       (3 << 0)
#       define R300_TXO_OFFSET_MASK              0xffffffe0
#       define R300_TXO_OFFSET_SHIFT             5
/* END */
#define R300_TX_UNK4_0                      0x4580
#define R300_TX_BORDER_COLOR_0              0x45C0	//ff00ff00 == { 0, 1.0, 0, 1.0 }

/* END */

/* BEGIN: Fragment program instruction set
// Fragment programs are written directly into register space.
// There are separate instruction streams for texture instructions and ALU
// instructions.
// In order to synchronize these streams, the program is divided into up
// to 4 nodes. Each node begins with a number of TEX operations, followed
// by a number of ALU operations.
// The first node can have zero TEX ops, all subsequent nodes must have at least
// one TEX ops.
// All nodes must have at least one ALU op.
//
// The index of the last node is stored in PFS_CNTL_0: A value of 0 means
// 1 node, a value of 3 means 4 nodes.
// The total amount of instructions is defined in PFS_CNTL_2. The offsets are
// offsets into the respective instruction streams, while *_END points to the
// last instruction relative to this offset. */
#define R300_PFS_CNTL_0                     0x4600
#       define R300_PFS_CNTL_LAST_NODES_SHIFT    0
#       define R300_PFS_CNTL_LAST_NODES_MASK     (3 << 0)
#       define R300_PFS_CNTL_FIRST_NODE_HAS_TEX  (1 << 3)
#define R300_PFS_CNTL_1                     0x4604
/* There is an unshifted value here which has so far always been equal to the
// index of the highest used temporary register. */
#define R300_PFS_CNTL_2                     0x4608
#       define R300_PFS_CNTL_ALU_OFFSET_SHIFT    0
#       define R300_PFS_CNTL_ALU_OFFSET_MASK     (63 << 0)
#       define R300_PFS_CNTL_ALU_END_SHIFT       6
#       define R300_PFS_CNTL_ALU_END_MASK        (63 << 0)
#       define R300_PFS_CNTL_TEX_OFFSET_SHIFT    12
#       define R300_PFS_CNTL_TEX_OFFSET_MASK     (31 << 12)	/* GUESS */
#       define R300_PFS_CNTL_TEX_END_SHIFT       18
#       define R300_PFS_CNTL_TEX_END_MASK        (31 << 18)	/* GUESS */

/* gap */
/* Nodes are stored backwards. The last active node is always stored in
// PFS_NODE_3.
// Example: In a 2-node program, NODE_0 and NODE_1 are set to 0. The
// first node is stored in NODE_2, the second node is stored in NODE_3.
//
// Offsets are relative to the master offset from PFS_CNTL_2.
// LAST_NODE is set for the last node, and only for the last node. */
#define R300_PFS_NODE_0                     0x4610
#define R300_PFS_NODE_1                     0x4614
#define R300_PFS_NODE_2                     0x4618
#define R300_PFS_NODE_3                     0x461C
#       define R300_PFS_NODE_ALU_OFFSET_SHIFT    0
#       define R300_PFS_NODE_ALU_OFFSET_MASK     (63 << 0)
#       define R300_PFS_NODE_ALU_END_SHIFT       6
#       define R300_PFS_NODE_ALU_END_MASK        (63 << 6)
#       define R300_PFS_NODE_TEX_OFFSET_SHIFT    12
#       define R300_PFS_NODE_TEX_OFFSET_MASK     (31 << 12)
#       define R300_PFS_NODE_TEX_END_SHIFT       17
#       define R300_PFS_NODE_TEX_END_MASK        (31 << 17)
#       define R300_PFS_NODE_LAST_NODE           (1 << 22)

/* TEX
// As far as I can tell, texture instructions cannot write into output
// registers directly. A subsequent ALU instruction is always necessary,
// even if it's just MAD o0, r0, 1, 0 */
#define R300_PFS_TEXI_0                     0x4620
#       define R300_FPITX_SRC_SHIFT              0
#       define R300_FPITX_SRC_MASK               (31 << 0)
#       define R300_FPITX_SRC_CONST              (1 << 5)	/* GUESS */
#       define R300_FPITX_DST_SHIFT              6
#       define R300_FPITX_DST_MASK               (31 << 6)
#       define R300_FPITX_IMAGE_SHIFT            11
#       define R300_FPITX_IMAGE_MASK             (15 << 11)	/* GUESS based on layout and native limits */
/* Unsure if these are opcodes, or some kind of bitfield, but this is how
 * they were set when I checked
 */
#		define R300_FPITX_OPCODE_SHIFT			15
#			define R300_FPITX_OP_TEX			1
#			define R300_FPITX_OP_TXP			3
#			define R300_FPITX_OP_TXB			4

/* ALU
// The ALU instructions register blocks are enumerated according to the order
// in which fglrx. I assume there is space for 64 instructions, since
// each block has space for a maximum of 64 DWORDs, and this matches reported
// native limits.
//
// The basic functional block seems to be one MAD for each color and alpha,
// and an adder that adds all components after the MUL.
//  - ADD, MUL, MAD etc.: use MAD with appropriate neutral operands
//  - DP4: Use OUTC_DP4, OUTA_DP4
//  - DP3: Use OUTC_DP3, OUTA_DP4, appropriate alpha operands
//  - DPH: Use OUTC_DP4, OUTA_DP4, appropriate alpha operands
//  - CMP: If ARG2 < 0, return ARG1, else return ARG0
//  - FLR: use FRC+MAD
//  - XPD: use MAD+MAD
//  - SGE, SLT: use MAD+CMP
//  - RSQ: use ABS modifier for argument
//  - Use OUTC_REPL_ALPHA to write results of an alpha-only operation (e.g. RCP)
//    into color register
//  - apparently, there's no quick DST operation
//  - fglrx set FPI2_UNKNOWN_31 on a "MAD fragment.color, tmp0, tmp1, tmp2"
//  - fglrx set FPI2_UNKNOWN_31 on a "MAX r2, r1, c0"
//  - fglrx once set FPI0_UNKNOWN_31 on a "FRC r1, r1"
//
// Operand selection
// First stage selects three sources from the available registers and
// constant parameters. This is defined in INSTR1 (color) and INSTR3 (alpha).
// fglrx sorts the three source fields: Registers before constants,
// lower indices before higher indices; I do not know whether this is necessary.
// fglrx fills unused sources with "read constant 0"
// According to specs, you cannot select more than two different constants.
//
// Second stage selects the operands from the sources. This is defined in
// INSTR0 (color) and INSTR2 (alpha). You can also select the special constants
// zero and one.
// Swizzling and negation happens in this stage, as well.
//
// Important: Color and alpha seem to be mostly separate, i.e. their sources
// selection appears to be fully independent (the register storage is probably
// physically split into a color and an alpha section).
// However (because of the apparent physical split), there is some interaction
// WRT swizzling. If, for example, you want to load an R component into an
// Alpha operand, this R component is taken from a *color* source, not from
// an alpha source. The corresponding register doesn't even have to appear in
// the alpha sources list. (I hope this alll makes sense to you)
//
// Destination selection
// The destination register index is in FPI1 (color) and FPI3 (alpha) together
// with enable bits.
// There are separate enable bits for writing into temporary registers
// (DSTC_REG_* /DSTA_REG) and and program output registers (DSTC_OUTPUT_* /DSTA_OUTPUT).
// You can write to both at once, or not write at all (the same index
// must be used for both).
//
// Note: There is a special form for LRP
//  - Argument order is the same as in ARB_fragment_program.
//  - Operation is MAD
//  - ARG1 is set to ARGC_SRC1C_LRP/ARGC_SRC1A_LRP
//  - Set FPI0/FPI2_SPECIAL_LRP
// Arbitrary LRP (including support for swizzling) requires vanilla MAD+MAD */
#define R300_PFS_INSTR1_0                   0x46C0
#       define R300_FPI1_SRC0C_SHIFT             0
#       define R300_FPI1_SRC0C_MASK              (31 << 0)
#       define R300_FPI1_SRC0C_CONST             (1 << 5)
#       define R300_FPI1_SRC1C_SHIFT             6
#       define R300_FPI1_SRC1C_MASK              (31 << 6)
#       define R300_FPI1_SRC1C_CONST             (1 << 11)
#       define R300_FPI1_SRC2C_SHIFT             12
#       define R300_FPI1_SRC2C_MASK              (31 << 12)
#       define R300_FPI1_SRC2C_CONST             (1 << 17)
#       define R300_FPI1_DSTC_SHIFT              18
#       define R300_FPI1_DSTC_MASK               (31 << 18)
#       define R300_FPI1_DSTC_REG_X              (1 << 23)
#       define R300_FPI1_DSTC_REG_Y              (1 << 24)
#       define R300_FPI1_DSTC_REG_Z              (1 << 25)
#       define R300_FPI1_DSTC_OUTPUT_X           (1 << 26)
#       define R300_FPI1_DSTC_OUTPUT_Y           (1 << 27)
#       define R300_FPI1_DSTC_OUTPUT_Z           (1 << 28)

#define R300_PFS_INSTR3_0                   0x47C0
#       define R300_FPI3_SRC0A_SHIFT             0
#       define R300_FPI3_SRC0A_MASK              (31 << 0)
#       define R300_FPI3_SRC0A_CONST             (1 << 5)
#       define R300_FPI3_SRC1A_SHIFT             6
#       define R300_FPI3_SRC1A_MASK              (31 << 6)
#       define R300_FPI3_SRC1A_CONST             (1 << 11)
#       define R300_FPI3_SRC2A_SHIFT             12
#       define R300_FPI3_SRC2A_MASK              (31 << 12)
#       define R300_FPI3_SRC2A_CONST             (1 << 17)
#       define R300_FPI3_DSTA_SHIFT              18
#       define R300_FPI3_DSTA_MASK               (31 << 18)
#       define R300_FPI3_DSTA_REG                (1 << 23)
#       define R300_FPI3_DSTA_OUTPUT             (1 << 24)

#define R300_PFS_INSTR0_0                   0x48C0
#       define R300_FPI0_ARGC_SRC0C_XYZ          0
#       define R300_FPI0_ARGC_SRC0C_XXX          1
#       define R300_FPI0_ARGC_SRC0C_YYY          2
#       define R300_FPI0_ARGC_SRC0C_ZZZ          3
#       define R300_FPI0_ARGC_SRC1C_XYZ          4
#       define R300_FPI0_ARGC_SRC1C_XXX          5
#       define R300_FPI0_ARGC_SRC1C_YYY          6
#       define R300_FPI0_ARGC_SRC1C_ZZZ          7
#       define R300_FPI0_ARGC_SRC2C_XYZ          8
#       define R300_FPI0_ARGC_SRC2C_XXX          9
#       define R300_FPI0_ARGC_SRC2C_YYY          10
#       define R300_FPI0_ARGC_SRC2C_ZZZ          11
#       define R300_FPI0_ARGC_SRC0A              12
#       define R300_FPI0_ARGC_SRC1A              13
#       define R300_FPI0_ARGC_SRC2A              14
#       define R300_FPI0_ARGC_SRC1C_LRP          15
#       define R300_FPI0_ARGC_ZERO               20
#       define R300_FPI0_ARGC_ONE                21
#       define R300_FPI0_ARGC_HALF               22	/* GUESS */
#       define R300_FPI0_ARGC_SRC0C_YZX          23
#       define R300_FPI0_ARGC_SRC1C_YZX          24
#       define R300_FPI0_ARGC_SRC2C_YZX          25
#       define R300_FPI0_ARGC_SRC0C_ZXY          26
#       define R300_FPI0_ARGC_SRC1C_ZXY          27
#       define R300_FPI0_ARGC_SRC2C_ZXY          28
#       define R300_FPI0_ARGC_SRC0CA_WZY         29
#       define R300_FPI0_ARGC_SRC1CA_WZY         30
#       define R300_FPI0_ARGC_SRC2CA_WZY         31

#       define R300_FPI0_ARG0C_SHIFT             0
#       define R300_FPI0_ARG0C_MASK              (31 << 0)
#       define R300_FPI0_ARG0C_NEG               (1 << 5)
#       define R300_FPI0_ARG0C_ABS               (1 << 6)
#       define R300_FPI0_ARG1C_SHIFT             7
#       define R300_FPI0_ARG1C_MASK              (31 << 7)
#       define R300_FPI0_ARG1C_NEG               (1 << 12)
#       define R300_FPI0_ARG1C_ABS               (1 << 13)
#       define R300_FPI0_ARG2C_SHIFT             14
#       define R300_FPI0_ARG2C_MASK              (31 << 14)
#       define R300_FPI0_ARG2C_NEG               (1 << 19)
#       define R300_FPI0_ARG2C_ABS               (1 << 20)
#       define R300_FPI0_SPECIAL_LRP             (1 << 21)
#       define R300_FPI0_OUTC_MAD                (0 << 23)
#       define R300_FPI0_OUTC_DP3                (1 << 23)
#       define R300_FPI0_OUTC_DP4                (2 << 23)
#       define R300_FPI0_OUTC_MIN                (4 << 23)
#       define R300_FPI0_OUTC_MAX                (5 << 23)
#       define R300_FPI0_OUTC_CMP                (8 << 23)
#       define R300_FPI0_OUTC_FRC                (9 << 23)
#       define R300_FPI0_OUTC_REPL_ALPHA         (10 << 23)
#       define R300_FPI0_OUTC_SAT                (1 << 30)
#       define R300_FPI0_UNKNOWN_31              (1 << 31)

#define R300_PFS_INSTR2_0                   0x49C0
#       define R300_FPI2_ARGA_SRC0C_X            0
#       define R300_FPI2_ARGA_SRC0C_Y            1
#       define R300_FPI2_ARGA_SRC0C_Z            2
#       define R300_FPI2_ARGA_SRC1C_X            3
#       define R300_FPI2_ARGA_SRC1C_Y            4
#       define R300_FPI2_ARGA_SRC1C_Z            5
#       define R300_FPI2_ARGA_SRC2C_X            6
#       define R300_FPI2_ARGA_SRC2C_Y            7
#       define R300_FPI2_ARGA_SRC2C_Z            8
#       define R300_FPI2_ARGA_SRC0A              9
#       define R300_FPI2_ARGA_SRC1A              10
#       define R300_FPI2_ARGA_SRC2A              11
#       define R300_FPI2_ARGA_SRC1A_LRP          15
#       define R300_FPI2_ARGA_ZERO               16
#       define R300_FPI2_ARGA_ONE                17
#       define R300_FPI2_ARGA_HALF               18	/* GUESS */

#       define R300_FPI2_ARG0A_SHIFT             0
#       define R300_FPI2_ARG0A_MASK              (31 << 0)
#       define R300_FPI2_ARG0A_NEG               (1 << 5)
#		define R300_FPI2_ARG0A_ABS				 (1 << 6)	/* GUESS */
#       define R300_FPI2_ARG1A_SHIFT             7
#       define R300_FPI2_ARG1A_MASK              (31 << 7)
#       define R300_FPI2_ARG1A_NEG               (1 << 12)
#		define R300_FPI2_ARG1A_ABS				 (1 << 13)	/* GUESS */
#       define R300_FPI2_ARG2A_SHIFT             14
#       define R300_FPI2_ARG2A_MASK              (31 << 14)
#       define R300_FPI2_ARG2A_NEG               (1 << 19)
#		define R300_FPI2_ARG2A_ABS				 (1 << 20)	/* GUESS */
#       define R300_FPI2_SPECIAL_LRP             (1 << 21)
#       define R300_FPI2_OUTA_MAD                (0 << 23)
#       define R300_FPI2_OUTA_DP4                (1 << 23)
#       define R300_FPI2_OUTA_MIN                (2 << 23)
#       define R300_FPI2_OUTA_MAX                (3 << 23)
#       define R300_FPI2_OUTA_CMP                (6 << 23)
#       define R300_FPI2_OUTA_FRC                (7 << 23)
#       define R300_FPI2_OUTA_EX2                (8 << 23)
#       define R300_FPI2_OUTA_LG2                (9 << 23)
#       define R300_FPI2_OUTA_RCP                (10 << 23)
#       define R300_FPI2_OUTA_RSQ                (11 << 23)
#       define R300_FPI2_OUTA_SAT                (1 << 30)
#       define R300_FPI2_UNKNOWN_31              (1 << 31)
/* END */

/* gap */
#define R300_PP_ALPHA_TEST                  0x4BD4
#       define R300_REF_ALPHA_MASK               0x000000ff
#       define R300_ALPHA_TEST_FAIL              (0 << 8)
#       define R300_ALPHA_TEST_LESS              (1 << 8)
#       define R300_ALPHA_TEST_LEQUAL            (3 << 8)
#       define R300_ALPHA_TEST_EQUAL             (2 << 8)
#       define R300_ALPHA_TEST_GEQUAL            (6 << 8)
#       define R300_ALPHA_TEST_GREATER           (4 << 8)
#       define R300_ALPHA_TEST_NEQUAL            (5 << 8)
#       define R300_ALPHA_TEST_PASS              (7 << 8)
#       define R300_ALPHA_TEST_OP_MASK           (7 << 8)
#       define R300_ALPHA_TEST_ENABLE            (1 << 11)

/* gap */
/* Fragment program parameters in 7.16 floating point */
#define R300_PFS_PARAM_0_X                  0x4C00
#define R300_PFS_PARAM_0_Y                  0x4C04
#define R300_PFS_PARAM_0_Z                  0x4C08
#define R300_PFS_PARAM_0_W                  0x4C0C
/* GUESS: PARAM_31 is last, based on native limits reported by fglrx */
#define R300_PFS_PARAM_31_X                 0x4DF0
#define R300_PFS_PARAM_31_Y                 0x4DF4
#define R300_PFS_PARAM_31_Z                 0x4DF8
#define R300_PFS_PARAM_31_W                 0x4DFC

/* Notes:
// - AFAIK fglrx always sets BLEND_UNKNOWN when blending is used in the application
// - AFAIK fglrx always sets BLEND_NO_SEPARATE when CBLEND and ABLEND are set to the same
//   function (both registers are always set up completely in any case)
// - Most blend flags are simply copied from R200 and not tested yet */
#define R300_RB3D_CBLEND                    0x4E04
#define R300_RB3D_ABLEND                    0x4E08
 /* the following only appear in CBLEND */
#       define R300_BLEND_ENABLE                     (1 << 0)
#       define R300_BLEND_UNKNOWN                    (3 << 1)
#       define R300_BLEND_NO_SEPARATE                (1 << 3)
 /* the following are shared between CBLEND and ABLEND */
#       define R300_FCN_MASK                         (3  << 12)
#       define R300_COMB_FCN_ADD_CLAMP               (0  << 12)
#       define R300_COMB_FCN_ADD_NOCLAMP             (1  << 12)
#       define R300_COMB_FCN_SUB_CLAMP               (2  << 12)
#       define R300_COMB_FCN_SUB_NOCLAMP             (3  << 12)
#       define R300_SRC_BLEND_GL_ZERO                (32 << 16)
#       define R300_SRC_BLEND_GL_ONE                 (33 << 16)
#       define R300_SRC_BLEND_GL_SRC_COLOR           (34 << 16)
#       define R300_SRC_BLEND_GL_ONE_MINUS_SRC_COLOR (35 << 16)
#       define R300_SRC_BLEND_GL_DST_COLOR           (36 << 16)
#       define R300_SRC_BLEND_GL_ONE_MINUS_DST_COLOR (37 << 16)
#       define R300_SRC_BLEND_GL_SRC_ALPHA           (38 << 16)
#       define R300_SRC_BLEND_GL_ONE_MINUS_SRC_ALPHA (39 << 16)
#       define R300_SRC_BLEND_GL_DST_ALPHA           (40 << 16)
#       define R300_SRC_BLEND_GL_ONE_MINUS_DST_ALPHA (41 << 16)
#       define R300_SRC_BLEND_GL_SRC_ALPHA_SATURATE  (42 << 16)
#       define R300_SRC_BLEND_MASK                   (63 << 16)
#       define R300_DST_BLEND_GL_ZERO                (32 << 24)
#       define R300_DST_BLEND_GL_ONE                 (33 << 24)
#       define R300_DST_BLEND_GL_SRC_COLOR           (34 << 24)
#       define R300_DST_BLEND_GL_ONE_MINUS_SRC_COLOR (35 << 24)
#       define R300_DST_BLEND_GL_DST_COLOR           (36 << 24)
#       define R300_DST_BLEND_GL_ONE_MINUS_DST_COLOR (37 << 24)
#       define R300_DST_BLEND_GL_SRC_ALPHA           (38 << 24)
#       define R300_DST_BLEND_GL_ONE_MINUS_SRC_ALPHA (39 << 24)
#       define R300_DST_BLEND_GL_DST_ALPHA           (40 << 24)
#       define R300_DST_BLEND_GL_ONE_MINUS_DST_ALPHA (41 << 24)
#       define R300_DST_BLEND_MASK                   (63 << 24)
#define R300_RB3D_COLORMASK                 0x4E0C
#       define R300_COLORMASK0_B                 (1<<0)
#       define R300_COLORMASK0_G                 (1<<1)
#       define R300_COLORMASK0_R                 (1<<2)
#       define R300_COLORMASK0_A                 (1<<3)

/* gap */
#define R300_RB3D_COLOROFFSET0              0x4E28
#       define R300_COLOROFFSET_MASK             0xFFFFFFF0	/* GUESS */
#define R300_RB3D_COLOROFFSET1              0x4E2C	/* GUESS */
#define R300_RB3D_COLOROFFSET2              0x4E30	/* GUESS */
#define R300_RB3D_COLOROFFSET3              0x4E34	/* GUESS */
/* gap */
/* Bit 16: Larger tiles
// Bit 17: 4x2 tiles
// Bit 18: Extremely weird tile like, but some pixels duplicated? */
#define R300_RB3D_COLORPITCH0               0x4E38
#       define R300_COLORPITCH_MASK              0x00001FF8	/* GUESS */
#       define R300_COLOR_TILE_ENABLE            (1 << 16)	/* GUESS */
#       define R300_COLOR_MICROTILE_ENABLE       (1 << 17)	/* GUESS */
#       define R300_COLOR_ENDIAN_NO_SWAP         (0 << 18)	/* GUESS */
#       define R300_COLOR_ENDIAN_WORD_SWAP       (1 << 18)	/* GUESS */
#       define R300_COLOR_ENDIAN_DWORD_SWAP      (2 << 18)	/* GUESS */
#       define R300_COLOR_FORMAT_RGB565          (2 << 22)
#       define R300_COLOR_FORMAT_ARGB8888        (3 << 22)
#define R300_RB3D_COLORPITCH1               0x4E3C	/* GUESS */
#define R300_RB3D_COLORPITCH2               0x4E40	/* GUESS */
#define R300_RB3D_COLORPITCH3               0x4E44	/* GUESS */

/* gap */
/* Guess by Vladimir.
// Set to 0A before 3D operations, set to 02 afterwards. */
#define R300_RB3D_DSTCACHE_CTLSTAT          0x4E4C
#       define R300_RB3D_DSTCACHE_02             0x00000002
#       define R300_RB3D_DSTCACHE_0A             0x0000000A

/* gap */
/* There seems to be no "write only" setting, so use Z-test = ALWAYS for this. */
/* Bit (1<<8) is the "test" bit. so plain write is 6  - vd */
#define R300_RB3D_ZSTENCIL_CNTL_0                   0x4F00
#       define R300_RB3D_Z_DISABLED_1            0x00000010	/* GUESS */
#       define R300_RB3D_Z_DISABLED_2            0x00000014	/* GUESS */
#       define R300_RB3D_Z_TEST                  0x00000012
#       define R300_RB3D_Z_TEST_AND_WRITE        0x00000016
#       define R300_RB3D_Z_WRITE_ONLY        	 0x00000006

#       define R300_RB3D_Z_TEST                  0x00000012
#       define R300_RB3D_Z_TEST_AND_WRITE        0x00000016
#       define R300_RB3D_Z_WRITE_ONLY        	 0x00000006
#	define R300_RB3D_STENCIL_ENABLE		 0x00000001

#define R300_RB3D_ZSTENCIL_CNTL_1                   0x4F04
		/* functions */
#	define R300_ZS_NEVER			0
#	define R300_ZS_LESS			1
#	define R300_ZS_LEQUAL			2
#	define R300_ZS_EQUAL			3
#	define R300_ZS_GEQUAL			4
#	define R300_ZS_GREATER			5
#	define R300_ZS_NOTEQUAL			6
#	define R300_ZS_ALWAYS			7
#       define R300_ZS_MASK                     7
		/* operations */
#	define R300_ZS_KEEP			0
#	define R300_ZS_ZERO			1
#	define R300_ZS_REPLACE			2
#	define R300_ZS_INCR			3
#	define R300_ZS_DECR			4
#	define R300_ZS_INVERT			5
#	define R300_ZS_INCR_WRAP		6
#	define R300_ZS_DECR_WRAP		7

       /* front and back refer to operations done for front
          and back faces, i.e. separate stencil function support */
#	define R300_RB3D_ZS1_DEPTH_FUNC_SHIFT		0
#	define R300_RB3D_ZS1_FRONT_FUNC_SHIFT		3
#	define R300_RB3D_ZS1_FRONT_FAIL_OP_SHIFT	6
#	define R300_RB3D_ZS1_FRONT_ZPASS_OP_SHIFT	9
#	define R300_RB3D_ZS1_FRONT_ZFAIL_OP_SHIFT      12
#	define R300_RB3D_ZS1_BACK_FUNC_SHIFT           15
#	define R300_RB3D_ZS1_BACK_FAIL_OP_SHIFT        18
#	define R300_RB3D_ZS1_BACK_ZPASS_OP_SHIFT       21
#	define R300_RB3D_ZS1_BACK_ZFAIL_OP_SHIFT       24

#define R300_RB3D_ZSTENCIL_CNTL_2                   0x4F08
#	define R300_RB3D_ZS2_STENCIL_REF_SHIFT		0
#	define R300_RB3D_ZS2_STENCIL_MASK		0xFF
#	define R300_RB3D_ZS2_STENCIL_MASK_SHIFT	        8
#	define R300_RB3D_ZS2_STENCIL_WRITE_MASK_SHIFT	16

/* gap */

#define R300_RB3D_ZSTENCIL_FORMAT                   0x4F10
#	define R300_DEPTH_FORMAT_16BIT_INT_Z     (0 << 0)
#	define R300_DEPTH_FORMAT_24BIT_INT_Z     (2 << 0)

/* gap */
#define R300_RB3D_DEPTHOFFSET               0x4F20
#define R300_RB3D_DEPTHPITCH                0x4F24
#       define R300_DEPTHPITCH_MASK              0x00001FF8	/* GUESS */
#       define R300_DEPTH_TILE_ENABLE            (1 << 16)	/* GUESS */
#       define R300_DEPTH_MICROTILE_ENABLE       (1 << 17)	/* GUESS */
#       define R300_DEPTH_ENDIAN_NO_SWAP         (0 << 18)	/* GUESS */
#       define R300_DEPTH_ENDIAN_WORD_SWAP       (1 << 18)	/* GUESS */
#       define R300_DEPTH_ENDIAN_DWORD_SWAP      (2 << 18)	/* GUESS */

/* BEGIN: Vertex program instruction set
// Every instruction is four dwords long:
//  DWORD 0: output and opcode
//  DWORD 1: first argument
//  DWORD 2: second argument
//  DWORD 3: third argument
//
// Notes:
//  - ABS r, a is implemented as MAX r, a, -a
//  - MOV is implemented as ADD to zero
//  - XPD is implemented as MUL + MAD
//  - FLR is implemented as FRC + ADD
//  - apparently, fglrx tries to schedule instructions so that there is at least
//    one instruction between the write to a temporary and the first read
//    from said temporary; however, violations of this scheduling are allowed
//  - register indices seem to be unrelated with OpenGL aliasing to conventional state
//  - only one attribute and one parameter can be loaded at a time; however, the
//    same attribute/parameter can be used for more than one argument
//  - the second software argument for POW is the third hardware argument (no idea why)
//  - MAD with only temporaries as input seems to use VPI_OUT_SELECT_MAD_2
//
// There is some magic surrounding LIT:
//  The single argument is replicated across all three inputs, but swizzled:
//   First argument: xyzy
//   Second argument: xyzx
//   Third argument: xyzw
//  Whenever the result is used later in the fragment program, fglrx forces x and w
//  to be 1.0 in the input selection; I don't know whether this is strictly necessary */
#define R300_VPI_OUT_OP_DOT                     (1 << 0)
#define R300_VPI_OUT_OP_MUL                     (2 << 0)
#define R300_VPI_OUT_OP_ADD                     (3 << 0)
#define R300_VPI_OUT_OP_MAD                     (4 << 0)
#define R300_VPI_OUT_OP_DST                     (5 << 0)
#define R300_VPI_OUT_OP_FRC                     (6 << 0)
#define R300_VPI_OUT_OP_MAX                     (7 << 0)
#define R300_VPI_OUT_OP_MIN                     (8 << 0)
#define R300_VPI_OUT_OP_SGE                     (9 << 0)
#define R300_VPI_OUT_OP_SLT                     (10 << 0)
#define R300_VPI_OUT_OP_UNK12                   (12 << 0)	/* Used in GL_POINT_DISTANCE_ATTENUATION_ARB, vector(scalar, vector) */
#define R300_VPI_OUT_OP_EXP                     (65 << 0)
#define R300_VPI_OUT_OP_LOG                     (66 << 0)
#define R300_VPI_OUT_OP_UNK67                   (67 << 0)	/* Used in fog computations, scalar(scalar) */
#define R300_VPI_OUT_OP_LIT                     (68 << 0)
#define R300_VPI_OUT_OP_POW                     (69 << 0)
#define R300_VPI_OUT_OP_RCP                     (70 << 0)
#define R300_VPI_OUT_OP_RSQ                     (72 << 0)
#define R300_VPI_OUT_OP_UNK73                   (73 << 0)	/* Used in GL_POINT_DISTANCE_ATTENUATION_ARB, scalar(scalar) */
#define R300_VPI_OUT_OP_EX2                     (75 << 0)
#define R300_VPI_OUT_OP_LG2                     (76 << 0)
#define R300_VPI_OUT_OP_MAD_2                   (128 << 0)
#define R300_VPI_OUT_OP_UNK129                  (129 << 0)	/* all temps, vector(scalar, vector, vector) */

#define R300_VPI_OUT_REG_CLASS_TEMPORARY        (0 << 8)
#define R300_VPI_OUT_REG_CLASS_RESULT           (2 << 8)
#define R300_VPI_OUT_REG_CLASS_MASK             (31 << 8)

#define R300_VPI_OUT_REG_INDEX_SHIFT            13
#define R300_VPI_OUT_REG_INDEX_MASK             (31 << 13)	/* GUESS based on fglrx native limits */

#define R300_VPI_OUT_WRITE_X                    (1 << 20)
#define R300_VPI_OUT_WRITE_Y                    (1 << 21)
#define R300_VPI_OUT_WRITE_Z                    (1 << 22)
#define R300_VPI_OUT_WRITE_W                    (1 << 23)

#define R300_VPI_IN_REG_CLASS_TEMPORARY         (0 << 0)
#define R300_VPI_IN_REG_CLASS_ATTRIBUTE         (1 << 0)
#define R300_VPI_IN_REG_CLASS_PARAMETER         (2 << 0)
#define R300_VPI_IN_REG_CLASS_NONE              (9 << 0)
#define R300_VPI_IN_REG_CLASS_MASK              (31 << 0)	/* GUESS */

#define R300_VPI_IN_REG_INDEX_SHIFT             5
#define R300_VPI_IN_REG_INDEX_MASK              (255 << 5)	/* GUESS based on fglrx native limits */

/* The R300 can select components from the input register arbitrarily.
// Use the following constants, shifted by the component shift you
// want to select */
#define R300_VPI_IN_SELECT_X    0
#define R300_VPI_IN_SELECT_Y    1
#define R300_VPI_IN_SELECT_Z    2
#define R300_VPI_IN_SELECT_W    3
#define R300_VPI_IN_SELECT_ZERO 4
#define R300_VPI_IN_SELECT_ONE  5
#define R300_VPI_IN_SELECT_MASK 7

#define R300_VPI_IN_X_SHIFT                     13
#define R300_VPI_IN_Y_SHIFT                     16
#define R300_VPI_IN_Z_SHIFT                     19
#define R300_VPI_IN_W_SHIFT                     22

#define R300_VPI_IN_NEG_X                       (1 << 25)
#define R300_VPI_IN_NEG_Y                       (1 << 26)
#define R300_VPI_IN_NEG_Z                       (1 << 27)
#define R300_VPI_IN_NEG_W                       (1 << 28)
/* END */

//BEGIN: Packet 3 commands

// A primitive emission dword.
#define R300_PRIM_TYPE_NONE                     (0 << 0)
#define R300_PRIM_TYPE_POINT                    (1 << 0)
#define R300_PRIM_TYPE_LINE                     (2 << 0)
#define R300_PRIM_TYPE_LINE_STRIP               (3 << 0)
#define R300_PRIM_TYPE_TRI_LIST                 (4 << 0)
#define R300_PRIM_TYPE_TRI_FAN                  (5 << 0)
#define R300_PRIM_TYPE_TRI_STRIP                (6 << 0)
#define R300_PRIM_TYPE_TRI_TYPE2                (7 << 0)
#define R300_PRIM_TYPE_RECT_LIST                (8 << 0)
#define R300_PRIM_TYPE_3VRT_POINT_LIST          (9 << 0)
#define R300_PRIM_TYPE_3VRT_LINE_LIST           (10 << 0)
#define R300_PRIM_TYPE_POINT_SPRITES            (11 << 0)	// GUESS (based on r200)
#define R300_PRIM_TYPE_LINE_LOOP                (12 << 0)
#define R300_PRIM_TYPE_QUADS                    (13 << 0)
#define R300_PRIM_TYPE_QUAD_STRIP               (14 << 0)
#define R300_PRIM_TYPE_POLYGON                  (15 << 0)
#define R300_PRIM_TYPE_MASK                     0xF
#define R300_PRIM_WALK_IND                      (1 << 4)
#define R300_PRIM_WALK_LIST                     (2 << 4)
#define R300_PRIM_WALK_RING                     (3 << 4)
#define R300_PRIM_WALK_MASK                     (3 << 4)
#define R300_PRIM_COLOR_ORDER_BGRA              (0 << 6)	// GUESS (based on r200)
#define R300_PRIM_COLOR_ORDER_RGBA              (1 << 6)	// GUESS
#define R300_PRIM_NUM_VERTICES_SHIFT            16

// Draw a primitive from vertex data in arrays loaded via 3D_LOAD_VBPNTR.
// Two parameter dwords:
// 0. The first parameter appears to be always 0
// 1. The second parameter is a standard primitive emission dword.
#define R300_PACKET3_3D_DRAW_VBUF           0x00002800

// Specify the full set of vertex arrays as (address, stride).
// The first parameter is the number of vertex arrays specified.
// The rest of the command is a variable length list of blocks, where
// each block is three dwords long and specifies two arrays.
// The first dword of a block is split into two words, the lower significant
// word refers to the first array, the more significant word to the second
// array in the block.
// The low byte of each word contains the size of an array entry in dwords,
// the high byte contains the stride of the array.
// The second dword of a block contains the pointer to the first array,
// the third dword of a block contains the pointer to the second array.
// Note that if the total number of arrays is odd, the third dword of
// the last block is omitted.
#define R300_PACKET3_3D_LOAD_VBPNTR         0x00002F00

#define R300_PACKET3_INDX_BUFFER            0x00003300
#    define R300_EB_UNK1_SHIFT                      24
#    define R300_EB_UNK1                    (0x80<<24)
#    define R300_EB_UNK2                        0x0810
#define R300_PACKET3_3D_DRAW_INDX_2         0x00003600

//END

#endif				/* _R300_REG_H */
