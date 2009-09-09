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
#ifndef __R100D_H__
#define __R100D_H__

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
#define R_000E40_RBBM_STATUS                         0x000E40
#define   S_000E40_CMDFIFO_AVAIL(x)                    (((x) & 0x7F) << 0)
#define   G_000E40_CMDFIFO_AVAIL(x)                    (((x) >> 0) & 0x7F)
#define   C_000E40_CMDFIFO_AVAIL                       0xFFFFFF80
#define   S_000E40_HIRQ_ON_RBB(x)                      (((x) & 0x1) << 8)
#define   G_000E40_HIRQ_ON_RBB(x)                      (((x) >> 8) & 0x1)
#define   C_000E40_HIRQ_ON_RBB                         0xFFFFFEFF
#define   S_000E40_CPRQ_ON_RBB(x)                      (((x) & 0x1) << 9)
#define   G_000E40_CPRQ_ON_RBB(x)                      (((x) >> 9) & 0x1)
#define   C_000E40_CPRQ_ON_RBB                         0xFFFFFDFF
#define   S_000E40_CFRQ_ON_RBB(x)                      (((x) & 0x1) << 10)
#define   G_000E40_CFRQ_ON_RBB(x)                      (((x) >> 10) & 0x1)
#define   C_000E40_CFRQ_ON_RBB                         0xFFFFFBFF
#define   S_000E40_HIRQ_IN_RTBUF(x)                    (((x) & 0x1) << 11)
#define   G_000E40_HIRQ_IN_RTBUF(x)                    (((x) >> 11) & 0x1)
#define   C_000E40_HIRQ_IN_RTBUF                       0xFFFFF7FF
#define   S_000E40_CPRQ_IN_RTBUF(x)                    (((x) & 0x1) << 12)
#define   G_000E40_CPRQ_IN_RTBUF(x)                    (((x) >> 12) & 0x1)
#define   C_000E40_CPRQ_IN_RTBUF                       0xFFFFEFFF
#define   S_000E40_CFRQ_IN_RTBUF(x)                    (((x) & 0x1) << 13)
#define   G_000E40_CFRQ_IN_RTBUF(x)                    (((x) >> 13) & 0x1)
#define   C_000E40_CFRQ_IN_RTBUF                       0xFFFFDFFF
#define   S_000E40_CF_PIPE_BUSY(x)                     (((x) & 0x1) << 14)
#define   G_000E40_CF_PIPE_BUSY(x)                     (((x) >> 14) & 0x1)
#define   C_000E40_CF_PIPE_BUSY                        0xFFFFBFFF
#define   S_000E40_ENG_EV_BUSY(x)                      (((x) & 0x1) << 15)
#define   G_000E40_ENG_EV_BUSY(x)                      (((x) >> 15) & 0x1)
#define   C_000E40_ENG_EV_BUSY                         0xFFFF7FFF
#define   S_000E40_CP_CMDSTRM_BUSY(x)                  (((x) & 0x1) << 16)
#define   G_000E40_CP_CMDSTRM_BUSY(x)                  (((x) >> 16) & 0x1)
#define   C_000E40_CP_CMDSTRM_BUSY                     0xFFFEFFFF
#define   S_000E40_E2_BUSY(x)                          (((x) & 0x1) << 17)
#define   G_000E40_E2_BUSY(x)                          (((x) >> 17) & 0x1)
#define   C_000E40_E2_BUSY                             0xFFFDFFFF
#define   S_000E40_RB2D_BUSY(x)                        (((x) & 0x1) << 18)
#define   G_000E40_RB2D_BUSY(x)                        (((x) >> 18) & 0x1)
#define   C_000E40_RB2D_BUSY                           0xFFFBFFFF
#define   S_000E40_RB3D_BUSY(x)                        (((x) & 0x1) << 19)
#define   G_000E40_RB3D_BUSY(x)                        (((x) >> 19) & 0x1)
#define   C_000E40_RB3D_BUSY                           0xFFF7FFFF
#define   S_000E40_SE_BUSY(x)                          (((x) & 0x1) << 20)
#define   G_000E40_SE_BUSY(x)                          (((x) >> 20) & 0x1)
#define   C_000E40_SE_BUSY                             0xFFEFFFFF
#define   S_000E40_RE_BUSY(x)                          (((x) & 0x1) << 21)
#define   G_000E40_RE_BUSY(x)                          (((x) >> 21) & 0x1)
#define   C_000E40_RE_BUSY                             0xFFDFFFFF
#define   S_000E40_TAM_BUSY(x)                         (((x) & 0x1) << 22)
#define   G_000E40_TAM_BUSY(x)                         (((x) >> 22) & 0x1)
#define   C_000E40_TAM_BUSY                            0xFFBFFFFF
#define   S_000E40_TDM_BUSY(x)                         (((x) & 0x1) << 23)
#define   G_000E40_TDM_BUSY(x)                         (((x) >> 23) & 0x1)
#define   C_000E40_TDM_BUSY                            0xFF7FFFFF
#define   S_000E40_PB_BUSY(x)                          (((x) & 0x1) << 24)
#define   G_000E40_PB_BUSY(x)                          (((x) >> 24) & 0x1)
#define   C_000E40_PB_BUSY                             0xFEFFFFFF
#define   S_000E40_GUI_ACTIVE(x)                       (((x) & 0x1) << 31)
#define   G_000E40_GUI_ACTIVE(x)                       (((x) >> 31) & 0x1)
#define   C_000E40_GUI_ACTIVE                          0x7FFFFFFF

#endif
