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
#define		PACKET3_3D_CLEAR_ZMASK		0x32
#define		PACKET3_INDX_BUFFER		0x33
#define		PACKET3_3D_DRAW_VBUF_2		0x34
#define		PACKET3_3D_DRAW_IMMD_2		0x35
#define		PACKET3_3D_DRAW_INDX_2		0x36
#define		PACKET3_3D_CLEAR_HIZ		0x37
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
#define R_0007C0_CP_STAT                             0x0007C0
#define   S_0007C0_MRU_BUSY(x)                         (((x) & 0x1) << 0)
#define   G_0007C0_MRU_BUSY(x)                         (((x) >> 0) & 0x1)
#define   C_0007C0_MRU_BUSY                            0xFFFFFFFE
#define   S_0007C0_MWU_BUSY(x)                         (((x) & 0x1) << 1)
#define   G_0007C0_MWU_BUSY(x)                         (((x) >> 1) & 0x1)
#define   C_0007C0_MWU_BUSY                            0xFFFFFFFD
#define   S_0007C0_RSIU_BUSY(x)                        (((x) & 0x1) << 2)
#define   G_0007C0_RSIU_BUSY(x)                        (((x) >> 2) & 0x1)
#define   C_0007C0_RSIU_BUSY                           0xFFFFFFFB
#define   S_0007C0_RCIU_BUSY(x)                        (((x) & 0x1) << 3)
#define   G_0007C0_RCIU_BUSY(x)                        (((x) >> 3) & 0x1)
#define   C_0007C0_RCIU_BUSY                           0xFFFFFFF7
#define   S_0007C0_CSF_PRIMARY_BUSY(x)                 (((x) & 0x1) << 9)
#define   G_0007C0_CSF_PRIMARY_BUSY(x)                 (((x) >> 9) & 0x1)
#define   C_0007C0_CSF_PRIMARY_BUSY                    0xFFFFFDFF
#define   S_0007C0_CSF_INDIRECT_BUSY(x)                (((x) & 0x1) << 10)
#define   G_0007C0_CSF_INDIRECT_BUSY(x)                (((x) >> 10) & 0x1)
#define   C_0007C0_CSF_INDIRECT_BUSY                   0xFFFFFBFF
#define   S_0007C0_CSQ_PRIMARY_BUSY(x)                 (((x) & 0x1) << 11)
#define   G_0007C0_CSQ_PRIMARY_BUSY(x)                 (((x) >> 11) & 0x1)
#define   C_0007C0_CSQ_PRIMARY_BUSY                    0xFFFFF7FF
#define   S_0007C0_CSQ_INDIRECT_BUSY(x)                (((x) & 0x1) << 12)
#define   G_0007C0_CSQ_INDIRECT_BUSY(x)                (((x) >> 12) & 0x1)
#define   C_0007C0_CSQ_INDIRECT_BUSY                   0xFFFFEFFF
#define   S_0007C0_CSI_BUSY(x)                         (((x) & 0x1) << 13)
#define   G_0007C0_CSI_BUSY(x)                         (((x) >> 13) & 0x1)
#define   C_0007C0_CSI_BUSY                            0xFFFFDFFF
#define   S_0007C0_CSF_INDIRECT2_BUSY(x)               (((x) & 0x1) << 14)
#define   G_0007C0_CSF_INDIRECT2_BUSY(x)               (((x) >> 14) & 0x1)
#define   C_0007C0_CSF_INDIRECT2_BUSY                  0xFFFFBFFF
#define   S_0007C0_CSQ_INDIRECT2_BUSY(x)               (((x) & 0x1) << 15)
#define   G_0007C0_CSQ_INDIRECT2_BUSY(x)               (((x) >> 15) & 0x1)
#define   C_0007C0_CSQ_INDIRECT2_BUSY                  0xFFFF7FFF
#define   S_0007C0_GUIDMA_BUSY(x)                      (((x) & 0x1) << 28)
#define   G_0007C0_GUIDMA_BUSY(x)                      (((x) >> 28) & 0x1)
#define   C_0007C0_GUIDMA_BUSY                         0xEFFFFFFF
#define   S_0007C0_VIDDMA_BUSY(x)                      (((x) & 0x1) << 29)
#define   G_0007C0_VIDDMA_BUSY(x)                      (((x) >> 29) & 0x1)
#define   C_0007C0_VIDDMA_BUSY                         0xDFFFFFFF
#define   S_0007C0_CMDSTRM_BUSY(x)                     (((x) & 0x1) << 30)
#define   G_0007C0_CMDSTRM_BUSY(x)                     (((x) >> 30) & 0x1)
#define   C_0007C0_CMDSTRM_BUSY                        0xBFFFFFFF
#define   S_0007C0_CP_BUSY(x)                          (((x) & 0x1) << 31)
#define   G_0007C0_CP_BUSY(x)                          (((x) >> 31) & 0x1)
#define   C_0007C0_CP_BUSY                             0x7FFFFFFF
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
#define   S_000E40_VAP_BUSY(x)                         (((x) & 0x1) << 20)
#define   G_000E40_VAP_BUSY(x)                         (((x) >> 20) & 0x1)
#define   C_000E40_VAP_BUSY                            0xFFEFFFFF
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
#define   S_000E40_TIM_BUSY(x)                         (((x) & 0x1) << 25)
#define   G_000E40_TIM_BUSY(x)                         (((x) >> 25) & 0x1)
#define   C_000E40_TIM_BUSY                            0xFDFFFFFF
#define   S_000E40_GA_BUSY(x)                          (((x) & 0x1) << 26)
#define   G_000E40_GA_BUSY(x)                          (((x) >> 26) & 0x1)
#define   C_000E40_GA_BUSY                             0xFBFFFFFF
#define   S_000E40_CBA2D_BUSY(x)                       (((x) & 0x1) << 27)
#define   G_000E40_CBA2D_BUSY(x)                       (((x) >> 27) & 0x1)
#define   C_000E40_CBA2D_BUSY                          0xF7FFFFFF
#define   S_000E40_GUI_ACTIVE(x)                       (((x) & 0x1) << 31)
#define   G_000E40_GUI_ACTIVE(x)                       (((x) >> 31) & 0x1)
#define   C_000E40_GUI_ACTIVE                          0x7FFFFFFF
#define R_0000F0_RBBM_SOFT_RESET                     0x0000F0
#define   S_0000F0_SOFT_RESET_CP(x)                    (((x) & 0x1) << 0)
#define   G_0000F0_SOFT_RESET_CP(x)                    (((x) >> 0) & 0x1)
#define   C_0000F0_SOFT_RESET_CP                       0xFFFFFFFE
#define   S_0000F0_SOFT_RESET_HI(x)                    (((x) & 0x1) << 1)
#define   G_0000F0_SOFT_RESET_HI(x)                    (((x) >> 1) & 0x1)
#define   C_0000F0_SOFT_RESET_HI                       0xFFFFFFFD
#define   S_0000F0_SOFT_RESET_VAP(x)                   (((x) & 0x1) << 2)
#define   G_0000F0_SOFT_RESET_VAP(x)                   (((x) >> 2) & 0x1)
#define   C_0000F0_SOFT_RESET_VAP                      0xFFFFFFFB
#define   S_0000F0_SOFT_RESET_RE(x)                    (((x) & 0x1) << 3)
#define   G_0000F0_SOFT_RESET_RE(x)                    (((x) >> 3) & 0x1)
#define   C_0000F0_SOFT_RESET_RE                       0xFFFFFFF7
#define   S_0000F0_SOFT_RESET_PP(x)                    (((x) & 0x1) << 4)
#define   G_0000F0_SOFT_RESET_PP(x)                    (((x) >> 4) & 0x1)
#define   C_0000F0_SOFT_RESET_PP                       0xFFFFFFEF
#define   S_0000F0_SOFT_RESET_E2(x)                    (((x) & 0x1) << 5)
#define   G_0000F0_SOFT_RESET_E2(x)                    (((x) >> 5) & 0x1)
#define   C_0000F0_SOFT_RESET_E2                       0xFFFFFFDF
#define   S_0000F0_SOFT_RESET_RB(x)                    (((x) & 0x1) << 6)
#define   G_0000F0_SOFT_RESET_RB(x)                    (((x) >> 6) & 0x1)
#define   C_0000F0_SOFT_RESET_RB                       0xFFFFFFBF
#define   S_0000F0_SOFT_RESET_HDP(x)                   (((x) & 0x1) << 7)
#define   G_0000F0_SOFT_RESET_HDP(x)                   (((x) >> 7) & 0x1)
#define   C_0000F0_SOFT_RESET_HDP                      0xFFFFFF7F
#define   S_0000F0_SOFT_RESET_MC(x)                    (((x) & 0x1) << 8)
#define   G_0000F0_SOFT_RESET_MC(x)                    (((x) >> 8) & 0x1)
#define   C_0000F0_SOFT_RESET_MC                       0xFFFFFEFF
#define   S_0000F0_SOFT_RESET_AIC(x)                   (((x) & 0x1) << 9)
#define   G_0000F0_SOFT_RESET_AIC(x)                   (((x) >> 9) & 0x1)
#define   C_0000F0_SOFT_RESET_AIC                      0xFFFFFDFF
#define   S_0000F0_SOFT_RESET_VIP(x)                   (((x) & 0x1) << 10)
#define   G_0000F0_SOFT_RESET_VIP(x)                   (((x) >> 10) & 0x1)
#define   C_0000F0_SOFT_RESET_VIP                      0xFFFFFBFF
#define   S_0000F0_SOFT_RESET_DISP(x)                  (((x) & 0x1) << 11)
#define   G_0000F0_SOFT_RESET_DISP(x)                  (((x) >> 11) & 0x1)
#define   C_0000F0_SOFT_RESET_DISP                     0xFFFFF7FF
#define   S_0000F0_SOFT_RESET_CG(x)                    (((x) & 0x1) << 12)
#define   G_0000F0_SOFT_RESET_CG(x)                    (((x) >> 12) & 0x1)
#define   C_0000F0_SOFT_RESET_CG                       0xFFFFEFFF
#define   S_0000F0_SOFT_RESET_GA(x)                    (((x) & 0x1) << 13)
#define   G_0000F0_SOFT_RESET_GA(x)                    (((x) >> 13) & 0x1)
#define   C_0000F0_SOFT_RESET_GA                       0xFFFFDFFF
#define   S_0000F0_SOFT_RESET_IDCT(x)                  (((x) & 0x1) << 14)
#define   G_0000F0_SOFT_RESET_IDCT(x)                  (((x) >> 14) & 0x1)
#define   C_0000F0_SOFT_RESET_IDCT                     0xFFFFBFFF

#define R_00000D_SCLK_CNTL                           0x00000D
#define   S_00000D_SCLK_SRC_SEL(x)                     (((x) & 0x7) << 0)
#define   G_00000D_SCLK_SRC_SEL(x)                     (((x) >> 0) & 0x7)
#define   C_00000D_SCLK_SRC_SEL                        0xFFFFFFF8
#define   S_00000D_CP_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 3)
#define   G_00000D_CP_MAX_DYN_STOP_LAT(x)              (((x) >> 3) & 0x1)
#define   C_00000D_CP_MAX_DYN_STOP_LAT                 0xFFFFFFF7
#define   S_00000D_HDP_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 4)
#define   G_00000D_HDP_MAX_DYN_STOP_LAT(x)             (((x) >> 4) & 0x1)
#define   C_00000D_HDP_MAX_DYN_STOP_LAT                0xFFFFFFEF
#define   S_00000D_TV_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 5)
#define   G_00000D_TV_MAX_DYN_STOP_LAT(x)              (((x) >> 5) & 0x1)
#define   C_00000D_TV_MAX_DYN_STOP_LAT                 0xFFFFFFDF
#define   S_00000D_E2_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 6)
#define   G_00000D_E2_MAX_DYN_STOP_LAT(x)              (((x) >> 6) & 0x1)
#define   C_00000D_E2_MAX_DYN_STOP_LAT                 0xFFFFFFBF
#define   S_00000D_SE_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 7)
#define   G_00000D_SE_MAX_DYN_STOP_LAT(x)              (((x) >> 7) & 0x1)
#define   C_00000D_SE_MAX_DYN_STOP_LAT                 0xFFFFFF7F
#define   S_00000D_IDCT_MAX_DYN_STOP_LAT(x)            (((x) & 0x1) << 8)
#define   G_00000D_IDCT_MAX_DYN_STOP_LAT(x)            (((x) >> 8) & 0x1)
#define   C_00000D_IDCT_MAX_DYN_STOP_LAT               0xFFFFFEFF
#define   S_00000D_VIP_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 9)
#define   G_00000D_VIP_MAX_DYN_STOP_LAT(x)             (((x) >> 9) & 0x1)
#define   C_00000D_VIP_MAX_DYN_STOP_LAT                0xFFFFFDFF
#define   S_00000D_RE_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 10)
#define   G_00000D_RE_MAX_DYN_STOP_LAT(x)              (((x) >> 10) & 0x1)
#define   C_00000D_RE_MAX_DYN_STOP_LAT                 0xFFFFFBFF
#define   S_00000D_PB_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 11)
#define   G_00000D_PB_MAX_DYN_STOP_LAT(x)              (((x) >> 11) & 0x1)
#define   C_00000D_PB_MAX_DYN_STOP_LAT                 0xFFFFF7FF
#define   S_00000D_TAM_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 12)
#define   G_00000D_TAM_MAX_DYN_STOP_LAT(x)             (((x) >> 12) & 0x1)
#define   C_00000D_TAM_MAX_DYN_STOP_LAT                0xFFFFEFFF
#define   S_00000D_TDM_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 13)
#define   G_00000D_TDM_MAX_DYN_STOP_LAT(x)             (((x) >> 13) & 0x1)
#define   C_00000D_TDM_MAX_DYN_STOP_LAT                0xFFFFDFFF
#define   S_00000D_RB_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 14)
#define   G_00000D_RB_MAX_DYN_STOP_LAT(x)              (((x) >> 14) & 0x1)
#define   C_00000D_RB_MAX_DYN_STOP_LAT                 0xFFFFBFFF
#define   S_00000D_FORCE_DISP2(x)                      (((x) & 0x1) << 15)
#define   G_00000D_FORCE_DISP2(x)                      (((x) >> 15) & 0x1)
#define   C_00000D_FORCE_DISP2                         0xFFFF7FFF
#define   S_00000D_FORCE_CP(x)                         (((x) & 0x1) << 16)
#define   G_00000D_FORCE_CP(x)                         (((x) >> 16) & 0x1)
#define   C_00000D_FORCE_CP                            0xFFFEFFFF
#define   S_00000D_FORCE_HDP(x)                        (((x) & 0x1) << 17)
#define   G_00000D_FORCE_HDP(x)                        (((x) >> 17) & 0x1)
#define   C_00000D_FORCE_HDP                           0xFFFDFFFF
#define   S_00000D_FORCE_DISP1(x)                      (((x) & 0x1) << 18)
#define   G_00000D_FORCE_DISP1(x)                      (((x) >> 18) & 0x1)
#define   C_00000D_FORCE_DISP1                         0xFFFBFFFF
#define   S_00000D_FORCE_TOP(x)                        (((x) & 0x1) << 19)
#define   G_00000D_FORCE_TOP(x)                        (((x) >> 19) & 0x1)
#define   C_00000D_FORCE_TOP                           0xFFF7FFFF
#define   S_00000D_FORCE_E2(x)                         (((x) & 0x1) << 20)
#define   G_00000D_FORCE_E2(x)                         (((x) >> 20) & 0x1)
#define   C_00000D_FORCE_E2                            0xFFEFFFFF
#define   S_00000D_FORCE_SE(x)                         (((x) & 0x1) << 21)
#define   G_00000D_FORCE_SE(x)                         (((x) >> 21) & 0x1)
#define   C_00000D_FORCE_SE                            0xFFDFFFFF
#define   S_00000D_FORCE_IDCT(x)                       (((x) & 0x1) << 22)
#define   G_00000D_FORCE_IDCT(x)                       (((x) >> 22) & 0x1)
#define   C_00000D_FORCE_IDCT                          0xFFBFFFFF
#define   S_00000D_FORCE_VIP(x)                        (((x) & 0x1) << 23)
#define   G_00000D_FORCE_VIP(x)                        (((x) >> 23) & 0x1)
#define   C_00000D_FORCE_VIP                           0xFF7FFFFF
#define   S_00000D_FORCE_RE(x)                         (((x) & 0x1) << 24)
#define   G_00000D_FORCE_RE(x)                         (((x) >> 24) & 0x1)
#define   C_00000D_FORCE_RE                            0xFEFFFFFF
#define   S_00000D_FORCE_PB(x)                         (((x) & 0x1) << 25)
#define   G_00000D_FORCE_PB(x)                         (((x) >> 25) & 0x1)
#define   C_00000D_FORCE_PB                            0xFDFFFFFF
#define   S_00000D_FORCE_TAM(x)                        (((x) & 0x1) << 26)
#define   G_00000D_FORCE_TAM(x)                        (((x) >> 26) & 0x1)
#define   C_00000D_FORCE_TAM                           0xFBFFFFFF
#define   S_00000D_FORCE_TDM(x)                        (((x) & 0x1) << 27)
#define   G_00000D_FORCE_TDM(x)                        (((x) >> 27) & 0x1)
#define   C_00000D_FORCE_TDM                           0xF7FFFFFF
#define   S_00000D_FORCE_RB(x)                         (((x) & 0x1) << 28)
#define   G_00000D_FORCE_RB(x)                         (((x) >> 28) & 0x1)
#define   C_00000D_FORCE_RB                            0xEFFFFFFF
#define   S_00000D_FORCE_TV_SCLK(x)                    (((x) & 0x1) << 29)
#define   G_00000D_FORCE_TV_SCLK(x)                    (((x) >> 29) & 0x1)
#define   C_00000D_FORCE_TV_SCLK                       0xDFFFFFFF
#define   S_00000D_FORCE_SUBPIC(x)                     (((x) & 0x1) << 30)
#define   G_00000D_FORCE_SUBPIC(x)                     (((x) >> 30) & 0x1)
#define   C_00000D_FORCE_SUBPIC                        0xBFFFFFFF
#define   S_00000D_FORCE_OV0(x)                        (((x) & 0x1) << 31)
#define   G_00000D_FORCE_OV0(x)                        (((x) >> 31) & 0x1)
#define   C_00000D_FORCE_OV0                           0x7FFFFFFF

#endif
