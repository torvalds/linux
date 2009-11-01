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
#ifndef __R300D_H__
#define __R300D_H__

#define CP_PACKET0			0x00000000
#define		PACKET0_BASE_INDEX_SHIFT	0
#define		PACKET0_BASE_INDEX_MASK		(0x1ffff << 0)
#define		PACKET0_COUNT_SHIFT		16
#define		PACKET0_COUNT_MASK		(0x3fff << 16)
#define CP_PACKET1			0x40000000
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)
#define CP_PACKET3			0xC0000000
#define		PACKET3_IT_OPCODE_SHIFT		8
#define		PACKET3_IT_OPCODE_MASK		(0xff << 8)
#define		PACKET3_COUNT_SHIFT		16
#define		PACKET3_COUNT_MASK		(0x3fff << 16)
/* PACKET3 op code */
#define		PACKET3_NOP			0x10
#define		PACKET3_3D_DRAW_VBUF		0x28
#define		PACKET3_3D_DRAW_IMMD		0x29
#define		PACKET3_3D_DRAW_INDX		0x2A
#define		PACKET3_3D_LOAD_VBPNTR		0x2F
#define		PACKET3_INDX_BUFFER		0x33
#define		PACKET3_3D_DRAW_VBUF_2		0x34
#define		PACKET3_3D_DRAW_IMMD_2		0x35
#define		PACKET3_3D_DRAW_INDX_2		0x36
#define		PACKET3_BITBLT_MULTI		0x9B

#define PACKET0(reg, n)	(CP_PACKET0 |					\
			 REG_SET(PACKET0_BASE_INDEX, (reg) >> 2) |	\
			 REG_SET(PACKET0_COUNT, (n)))
#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))
#define PACKET3(op, n)	(CP_PACKET3 |					\
			 REG_SET(PACKET3_IT_OPCODE, (op)) |		\
			 REG_SET(PACKET3_COUNT, (n)))

#define	PACKET_TYPE0	0
#define	PACKET_TYPE1	1
#define	PACKET_TYPE2	2
#define	PACKET_TYPE3	3

#define CP_PACKET_GET_TYPE(h) (((h) >> 30) & 3)
#define CP_PACKET_GET_COUNT(h) (((h) >> 16) & 0x3FFF)
#define CP_PACKET0_GET_REG(h) (((h) & 0x1FFF) << 2)
#define CP_PACKET0_GET_ONE_REG_WR(h) (((h) >> 15) & 1)
#define CP_PACKET3_GET_OPCODE(h) (((h) >> 8) & 0xFF)

/* Registers */
#define R_000148_MC_FB_LOCATION                      0x000148
#define   S_000148_MC_FB_START(x)                      (((x) & 0xFFFF) << 0)
#define   G_000148_MC_FB_START(x)                      (((x) >> 0) & 0xFFFF)
#define   C_000148_MC_FB_START                         0xFFFF0000
#define   S_000148_MC_FB_TOP(x)                        (((x) & 0xFFFF) << 16)
#define   G_000148_MC_FB_TOP(x)                        (((x) >> 16) & 0xFFFF)
#define   C_000148_MC_FB_TOP                           0x0000FFFF
#define R_00014C_MC_AGP_LOCATION                     0x00014C
#define   S_00014C_MC_AGP_START(x)                     (((x) & 0xFFFF) << 0)
#define   G_00014C_MC_AGP_START(x)                     (((x) >> 0) & 0xFFFF)
#define   C_00014C_MC_AGP_START                        0xFFFF0000
#define   S_00014C_MC_AGP_TOP(x)                       (((x) & 0xFFFF) << 16)
#define   G_00014C_MC_AGP_TOP(x)                       (((x) >> 16) & 0xFFFF)
#define   C_00014C_MC_AGP_TOP                          0x0000FFFF
#define R_00015C_AGP_BASE_2                          0x00015C
#define   S_00015C_AGP_BASE_ADDR_2(x)                  (((x) & 0xF) << 0)
#define   G_00015C_AGP_BASE_ADDR_2(x)                  (((x) >> 0) & 0xF)
#define   C_00015C_AGP_BASE_ADDR_2                     0xFFFFFFF0
#define R_000170_AGP_BASE                            0x000170
#define   S_000170_AGP_BASE_ADDR(x)                    (((x) & 0xFFFFFFFF) << 0)
#define   G_000170_AGP_BASE_ADDR(x)                    (((x) >> 0) & 0xFFFFFFFF)
#define   C_000170_AGP_BASE_ADDR                       0x00000000


#endif
