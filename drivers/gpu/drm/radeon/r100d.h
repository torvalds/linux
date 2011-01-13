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
#define R_0000F0_RBBM_SOFT_RESET                     0x0000F0
#define   S_0000F0_SOFT_RESET_CP(x)                    (((x) & 0x1) << 0)
#define   G_0000F0_SOFT_RESET_CP(x)                    (((x) >> 0) & 0x1)
#define   C_0000F0_SOFT_RESET_CP                       0xFFFFFFFE
#define   S_0000F0_SOFT_RESET_HI(x)                    (((x) & 0x1) << 1)
#define   G_0000F0_SOFT_RESET_HI(x)                    (((x) >> 1) & 0x1)
#define   C_0000F0_SOFT_RESET_HI                       0xFFFFFFFD
#define   S_0000F0_SOFT_RESET_SE(x)                    (((x) & 0x1) << 2)
#define   G_0000F0_SOFT_RESET_SE(x)                    (((x) >> 2) & 0x1)
#define   C_0000F0_SOFT_RESET_SE                       0xFFFFFFFB
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
#define R_000030_BUS_CNTL                            0x000030
#define   S_000030_BUS_DBL_RESYNC(x)                   (((x) & 0x1) << 0)
#define   G_000030_BUS_DBL_RESYNC(x)                   (((x) >> 0) & 0x1)
#define   C_000030_BUS_DBL_RESYNC                      0xFFFFFFFE
#define   S_000030_BUS_MSTR_RESET(x)                   (((x) & 0x1) << 1)
#define   G_000030_BUS_MSTR_RESET(x)                   (((x) >> 1) & 0x1)
#define   C_000030_BUS_MSTR_RESET                      0xFFFFFFFD
#define   S_000030_BUS_FLUSH_BUF(x)                    (((x) & 0x1) << 2)
#define   G_000030_BUS_FLUSH_BUF(x)                    (((x) >> 2) & 0x1)
#define   C_000030_BUS_FLUSH_BUF                       0xFFFFFFFB
#define   S_000030_BUS_STOP_REQ_DIS(x)                 (((x) & 0x1) << 3)
#define   G_000030_BUS_STOP_REQ_DIS(x)                 (((x) >> 3) & 0x1)
#define   C_000030_BUS_STOP_REQ_DIS                    0xFFFFFFF7
#define   S_000030_BUS_PM4_READ_COMBINE_EN(x)          (((x) & 0x1) << 4)
#define   G_000030_BUS_PM4_READ_COMBINE_EN(x)          (((x) >> 4) & 0x1)
#define   C_000030_BUS_PM4_READ_COMBINE_EN             0xFFFFFFEF
#define   S_000030_BUS_WRT_COMBINE_EN(x)               (((x) & 0x1) << 5)
#define   G_000030_BUS_WRT_COMBINE_EN(x)               (((x) >> 5) & 0x1)
#define   C_000030_BUS_WRT_COMBINE_EN                  0xFFFFFFDF
#define   S_000030_BUS_MASTER_DIS(x)                   (((x) & 0x1) << 6)
#define   G_000030_BUS_MASTER_DIS(x)                   (((x) >> 6) & 0x1)
#define   C_000030_BUS_MASTER_DIS                      0xFFFFFFBF
#define   S_000030_BIOS_ROM_WRT_EN(x)                  (((x) & 0x1) << 7)
#define   G_000030_BIOS_ROM_WRT_EN(x)                  (((x) >> 7) & 0x1)
#define   C_000030_BIOS_ROM_WRT_EN                     0xFFFFFF7F
#define   S_000030_BM_DAC_CRIPPLE(x)                   (((x) & 0x1) << 8)
#define   G_000030_BM_DAC_CRIPPLE(x)                   (((x) >> 8) & 0x1)
#define   C_000030_BM_DAC_CRIPPLE                      0xFFFFFEFF
#define   S_000030_BUS_NON_PM4_READ_COMBINE_EN(x)      (((x) & 0x1) << 9)
#define   G_000030_BUS_NON_PM4_READ_COMBINE_EN(x)      (((x) >> 9) & 0x1)
#define   C_000030_BUS_NON_PM4_READ_COMBINE_EN         0xFFFFFDFF
#define   S_000030_BUS_XFERD_DISCARD_EN(x)             (((x) & 0x1) << 10)
#define   G_000030_BUS_XFERD_DISCARD_EN(x)             (((x) >> 10) & 0x1)
#define   C_000030_BUS_XFERD_DISCARD_EN                0xFFFFFBFF
#define   S_000030_BUS_SGL_READ_DISABLE(x)             (((x) & 0x1) << 11)
#define   G_000030_BUS_SGL_READ_DISABLE(x)             (((x) >> 11) & 0x1)
#define   C_000030_BUS_SGL_READ_DISABLE                0xFFFFF7FF
#define   S_000030_BIOS_DIS_ROM(x)                     (((x) & 0x1) << 12)
#define   G_000030_BIOS_DIS_ROM(x)                     (((x) >> 12) & 0x1)
#define   C_000030_BIOS_DIS_ROM                        0xFFFFEFFF
#define   S_000030_BUS_PCI_READ_RETRY_EN(x)            (((x) & 0x1) << 13)
#define   G_000030_BUS_PCI_READ_RETRY_EN(x)            (((x) >> 13) & 0x1)
#define   C_000030_BUS_PCI_READ_RETRY_EN               0xFFFFDFFF
#define   S_000030_BUS_AGP_AD_STEPPING_EN(x)           (((x) & 0x1) << 14)
#define   G_000030_BUS_AGP_AD_STEPPING_EN(x)           (((x) >> 14) & 0x1)
#define   C_000030_BUS_AGP_AD_STEPPING_EN              0xFFFFBFFF
#define   S_000030_BUS_PCI_WRT_RETRY_EN(x)             (((x) & 0x1) << 15)
#define   G_000030_BUS_PCI_WRT_RETRY_EN(x)             (((x) >> 15) & 0x1)
#define   C_000030_BUS_PCI_WRT_RETRY_EN                0xFFFF7FFF
#define   S_000030_BUS_RETRY_WS(x)                     (((x) & 0xF) << 16)
#define   G_000030_BUS_RETRY_WS(x)                     (((x) >> 16) & 0xF)
#define   C_000030_BUS_RETRY_WS                        0xFFF0FFFF
#define   S_000030_BUS_MSTR_RD_MULT(x)                 (((x) & 0x1) << 20)
#define   G_000030_BUS_MSTR_RD_MULT(x)                 (((x) >> 20) & 0x1)
#define   C_000030_BUS_MSTR_RD_MULT                    0xFFEFFFFF
#define   S_000030_BUS_MSTR_RD_LINE(x)                 (((x) & 0x1) << 21)
#define   G_000030_BUS_MSTR_RD_LINE(x)                 (((x) >> 21) & 0x1)
#define   C_000030_BUS_MSTR_RD_LINE                    0xFFDFFFFF
#define   S_000030_BUS_SUSPEND(x)                      (((x) & 0x1) << 22)
#define   G_000030_BUS_SUSPEND(x)                      (((x) >> 22) & 0x1)
#define   C_000030_BUS_SUSPEND                         0xFFBFFFFF
#define   S_000030_LAT_16X(x)                          (((x) & 0x1) << 23)
#define   G_000030_LAT_16X(x)                          (((x) >> 23) & 0x1)
#define   C_000030_LAT_16X                             0xFF7FFFFF
#define   S_000030_BUS_RD_DISCARD_EN(x)                (((x) & 0x1) << 24)
#define   G_000030_BUS_RD_DISCARD_EN(x)                (((x) >> 24) & 0x1)
#define   C_000030_BUS_RD_DISCARD_EN                   0xFEFFFFFF
#define   S_000030_ENFRCWRDY(x)                        (((x) & 0x1) << 25)
#define   G_000030_ENFRCWRDY(x)                        (((x) >> 25) & 0x1)
#define   C_000030_ENFRCWRDY                           0xFDFFFFFF
#define   S_000030_BUS_MSTR_WS(x)                      (((x) & 0x1) << 26)
#define   G_000030_BUS_MSTR_WS(x)                      (((x) >> 26) & 0x1)
#define   C_000030_BUS_MSTR_WS                         0xFBFFFFFF
#define   S_000030_BUS_PARKING_DIS(x)                  (((x) & 0x1) << 27)
#define   G_000030_BUS_PARKING_DIS(x)                  (((x) >> 27) & 0x1)
#define   C_000030_BUS_PARKING_DIS                     0xF7FFFFFF
#define   S_000030_BUS_MSTR_DISCONNECT_EN(x)           (((x) & 0x1) << 28)
#define   G_000030_BUS_MSTR_DISCONNECT_EN(x)           (((x) >> 28) & 0x1)
#define   C_000030_BUS_MSTR_DISCONNECT_EN              0xEFFFFFFF
#define   S_000030_SERR_EN(x)                          (((x) & 0x1) << 29)
#define   G_000030_SERR_EN(x)                          (((x) >> 29) & 0x1)
#define   C_000030_SERR_EN                             0xDFFFFFFF
#define   S_000030_BUS_READ_BURST(x)                   (((x) & 0x1) << 30)
#define   G_000030_BUS_READ_BURST(x)                   (((x) >> 30) & 0x1)
#define   C_000030_BUS_READ_BURST                      0xBFFFFFFF
#define   S_000030_BUS_RDY_READ_DLY(x)                 (((x) & 0x1) << 31)
#define   G_000030_BUS_RDY_READ_DLY(x)                 (((x) >> 31) & 0x1)
#define   C_000030_BUS_RDY_READ_DLY                    0x7FFFFFFF
#define R_000040_GEN_INT_CNTL                        0x000040
#define   S_000040_CRTC_VBLANK(x)                      (((x) & 0x1) << 0)
#define   G_000040_CRTC_VBLANK(x)                      (((x) >> 0) & 0x1)
#define   C_000040_CRTC_VBLANK                         0xFFFFFFFE
#define   S_000040_CRTC_VLINE(x)                       (((x) & 0x1) << 1)
#define   G_000040_CRTC_VLINE(x)                       (((x) >> 1) & 0x1)
#define   C_000040_CRTC_VLINE                          0xFFFFFFFD
#define   S_000040_CRTC_VSYNC(x)                       (((x) & 0x1) << 2)
#define   G_000040_CRTC_VSYNC(x)                       (((x) >> 2) & 0x1)
#define   C_000040_CRTC_VSYNC                          0xFFFFFFFB
#define   S_000040_SNAPSHOT(x)                         (((x) & 0x1) << 3)
#define   G_000040_SNAPSHOT(x)                         (((x) >> 3) & 0x1)
#define   C_000040_SNAPSHOT                            0xFFFFFFF7
#define   S_000040_FP_DETECT(x)                        (((x) & 0x1) << 4)
#define   G_000040_FP_DETECT(x)                        (((x) >> 4) & 0x1)
#define   C_000040_FP_DETECT                           0xFFFFFFEF
#define   S_000040_CRTC2_VLINE(x)                      (((x) & 0x1) << 5)
#define   G_000040_CRTC2_VLINE(x)                      (((x) >> 5) & 0x1)
#define   C_000040_CRTC2_VLINE                         0xFFFFFFDF
#define   S_000040_DMA_VIPH0_INT_EN(x)                 (((x) & 0x1) << 12)
#define   G_000040_DMA_VIPH0_INT_EN(x)                 (((x) >> 12) & 0x1)
#define   C_000040_DMA_VIPH0_INT_EN                    0xFFFFEFFF
#define   S_000040_CRTC2_VSYNC(x)                      (((x) & 0x1) << 6)
#define   G_000040_CRTC2_VSYNC(x)                      (((x) >> 6) & 0x1)
#define   C_000040_CRTC2_VSYNC                         0xFFFFFFBF
#define   S_000040_SNAPSHOT2(x)                        (((x) & 0x1) << 7)
#define   G_000040_SNAPSHOT2(x)                        (((x) >> 7) & 0x1)
#define   C_000040_SNAPSHOT2                           0xFFFFFF7F
#define   S_000040_CRTC2_VBLANK(x)                     (((x) & 0x1) << 9)
#define   G_000040_CRTC2_VBLANK(x)                     (((x) >> 9) & 0x1)
#define   C_000040_CRTC2_VBLANK                        0xFFFFFDFF
#define   S_000040_FP2_DETECT(x)                       (((x) & 0x1) << 10)
#define   G_000040_FP2_DETECT(x)                       (((x) >> 10) & 0x1)
#define   C_000040_FP2_DETECT                          0xFFFFFBFF
#define   S_000040_VSYNC_DIFF_OVER_LIMIT(x)            (((x) & 0x1) << 11)
#define   G_000040_VSYNC_DIFF_OVER_LIMIT(x)            (((x) >> 11) & 0x1)
#define   C_000040_VSYNC_DIFF_OVER_LIMIT               0xFFFFF7FF
#define   S_000040_DMA_VIPH1_INT_EN(x)                 (((x) & 0x1) << 13)
#define   G_000040_DMA_VIPH1_INT_EN(x)                 (((x) >> 13) & 0x1)
#define   C_000040_DMA_VIPH1_INT_EN                    0xFFFFDFFF
#define   S_000040_DMA_VIPH2_INT_EN(x)                 (((x) & 0x1) << 14)
#define   G_000040_DMA_VIPH2_INT_EN(x)                 (((x) >> 14) & 0x1)
#define   C_000040_DMA_VIPH2_INT_EN                    0xFFFFBFFF
#define   S_000040_DMA_VIPH3_INT_EN(x)                 (((x) & 0x1) << 15)
#define   G_000040_DMA_VIPH3_INT_EN(x)                 (((x) >> 15) & 0x1)
#define   C_000040_DMA_VIPH3_INT_EN                    0xFFFF7FFF
#define   S_000040_I2C_INT_EN(x)                       (((x) & 0x1) << 17)
#define   G_000040_I2C_INT_EN(x)                       (((x) >> 17) & 0x1)
#define   C_000040_I2C_INT_EN                          0xFFFDFFFF
#define   S_000040_GUI_IDLE(x)                         (((x) & 0x1) << 19)
#define   G_000040_GUI_IDLE(x)                         (((x) >> 19) & 0x1)
#define   C_000040_GUI_IDLE                            0xFFF7FFFF
#define   S_000040_VIPH_INT_EN(x)                      (((x) & 0x1) << 24)
#define   G_000040_VIPH_INT_EN(x)                      (((x) >> 24) & 0x1)
#define   C_000040_VIPH_INT_EN                         0xFEFFFFFF
#define   S_000040_SW_INT_EN(x)                        (((x) & 0x1) << 25)
#define   G_000040_SW_INT_EN(x)                        (((x) >> 25) & 0x1)
#define   C_000040_SW_INT_EN                           0xFDFFFFFF
#define   S_000040_GEYSERVILLE(x)                      (((x) & 0x1) << 27)
#define   G_000040_GEYSERVILLE(x)                      (((x) >> 27) & 0x1)
#define   C_000040_GEYSERVILLE                         0xF7FFFFFF
#define   S_000040_HDCP_AUTHORIZED_INT(x)              (((x) & 0x1) << 28)
#define   G_000040_HDCP_AUTHORIZED_INT(x)              (((x) >> 28) & 0x1)
#define   C_000040_HDCP_AUTHORIZED_INT                 0xEFFFFFFF
#define   S_000040_DVI_I2C_INT(x)                      (((x) & 0x1) << 29)
#define   G_000040_DVI_I2C_INT(x)                      (((x) >> 29) & 0x1)
#define   C_000040_DVI_I2C_INT                         0xDFFFFFFF
#define   S_000040_GUIDMA(x)                           (((x) & 0x1) << 30)
#define   G_000040_GUIDMA(x)                           (((x) >> 30) & 0x1)
#define   C_000040_GUIDMA                              0xBFFFFFFF
#define   S_000040_VIDDMA(x)                           (((x) & 0x1) << 31)
#define   G_000040_VIDDMA(x)                           (((x) >> 31) & 0x1)
#define   C_000040_VIDDMA                              0x7FFFFFFF
#define R_000044_GEN_INT_STATUS                      0x000044
#define   S_000044_CRTC_VBLANK_STAT(x)                 (((x) & 0x1) << 0)
#define   G_000044_CRTC_VBLANK_STAT(x)                 (((x) >> 0) & 0x1)
#define   C_000044_CRTC_VBLANK_STAT                    0xFFFFFFFE
#define   S_000044_CRTC_VBLANK_STAT_AK(x)              (((x) & 0x1) << 0)
#define   G_000044_CRTC_VBLANK_STAT_AK(x)              (((x) >> 0) & 0x1)
#define   C_000044_CRTC_VBLANK_STAT_AK                 0xFFFFFFFE
#define   S_000044_CRTC_VLINE_STAT(x)                  (((x) & 0x1) << 1)
#define   G_000044_CRTC_VLINE_STAT(x)                  (((x) >> 1) & 0x1)
#define   C_000044_CRTC_VLINE_STAT                     0xFFFFFFFD
#define   S_000044_CRTC_VLINE_STAT_AK(x)               (((x) & 0x1) << 1)
#define   G_000044_CRTC_VLINE_STAT_AK(x)               (((x) >> 1) & 0x1)
#define   C_000044_CRTC_VLINE_STAT_AK                  0xFFFFFFFD
#define   S_000044_CRTC_VSYNC_STAT(x)                  (((x) & 0x1) << 2)
#define   G_000044_CRTC_VSYNC_STAT(x)                  (((x) >> 2) & 0x1)
#define   C_000044_CRTC_VSYNC_STAT                     0xFFFFFFFB
#define   S_000044_CRTC_VSYNC_STAT_AK(x)               (((x) & 0x1) << 2)
#define   G_000044_CRTC_VSYNC_STAT_AK(x)               (((x) >> 2) & 0x1)
#define   C_000044_CRTC_VSYNC_STAT_AK                  0xFFFFFFFB
#define   S_000044_SNAPSHOT_STAT(x)                    (((x) & 0x1) << 3)
#define   G_000044_SNAPSHOT_STAT(x)                    (((x) >> 3) & 0x1)
#define   C_000044_SNAPSHOT_STAT                       0xFFFFFFF7
#define   S_000044_SNAPSHOT_STAT_AK(x)                 (((x) & 0x1) << 3)
#define   G_000044_SNAPSHOT_STAT_AK(x)                 (((x) >> 3) & 0x1)
#define   C_000044_SNAPSHOT_STAT_AK                    0xFFFFFFF7
#define   S_000044_FP_DETECT_STAT(x)                   (((x) & 0x1) << 4)
#define   G_000044_FP_DETECT_STAT(x)                   (((x) >> 4) & 0x1)
#define   C_000044_FP_DETECT_STAT                      0xFFFFFFEF
#define   S_000044_FP_DETECT_STAT_AK(x)                (((x) & 0x1) << 4)
#define   G_000044_FP_DETECT_STAT_AK(x)                (((x) >> 4) & 0x1)
#define   C_000044_FP_DETECT_STAT_AK                   0xFFFFFFEF
#define   S_000044_CRTC2_VLINE_STAT(x)                 (((x) & 0x1) << 5)
#define   G_000044_CRTC2_VLINE_STAT(x)                 (((x) >> 5) & 0x1)
#define   C_000044_CRTC2_VLINE_STAT                    0xFFFFFFDF
#define   S_000044_CRTC2_VLINE_STAT_AK(x)              (((x) & 0x1) << 5)
#define   G_000044_CRTC2_VLINE_STAT_AK(x)              (((x) >> 5) & 0x1)
#define   C_000044_CRTC2_VLINE_STAT_AK                 0xFFFFFFDF
#define   S_000044_CRTC2_VSYNC_STAT(x)                 (((x) & 0x1) << 6)
#define   G_000044_CRTC2_VSYNC_STAT(x)                 (((x) >> 6) & 0x1)
#define   C_000044_CRTC2_VSYNC_STAT                    0xFFFFFFBF
#define   S_000044_CRTC2_VSYNC_STAT_AK(x)              (((x) & 0x1) << 6)
#define   G_000044_CRTC2_VSYNC_STAT_AK(x)              (((x) >> 6) & 0x1)
#define   C_000044_CRTC2_VSYNC_STAT_AK                 0xFFFFFFBF
#define   S_000044_SNAPSHOT2_STAT(x)                   (((x) & 0x1) << 7)
#define   G_000044_SNAPSHOT2_STAT(x)                   (((x) >> 7) & 0x1)
#define   C_000044_SNAPSHOT2_STAT                      0xFFFFFF7F
#define   S_000044_SNAPSHOT2_STAT_AK(x)                (((x) & 0x1) << 7)
#define   G_000044_SNAPSHOT2_STAT_AK(x)                (((x) >> 7) & 0x1)
#define   C_000044_SNAPSHOT2_STAT_AK                   0xFFFFFF7F
#define   S_000044_CAP0_INT_ACTIVE(x)                  (((x) & 0x1) << 8)
#define   G_000044_CAP0_INT_ACTIVE(x)                  (((x) >> 8) & 0x1)
#define   C_000044_CAP0_INT_ACTIVE                     0xFFFFFEFF
#define   S_000044_CRTC2_VBLANK_STAT(x)                (((x) & 0x1) << 9)
#define   G_000044_CRTC2_VBLANK_STAT(x)                (((x) >> 9) & 0x1)
#define   C_000044_CRTC2_VBLANK_STAT                   0xFFFFFDFF
#define   S_000044_CRTC2_VBLANK_STAT_AK(x)             (((x) & 0x1) << 9)
#define   G_000044_CRTC2_VBLANK_STAT_AK(x)             (((x) >> 9) & 0x1)
#define   C_000044_CRTC2_VBLANK_STAT_AK                0xFFFFFDFF
#define   S_000044_FP2_DETECT_STAT(x)                  (((x) & 0x1) << 10)
#define   G_000044_FP2_DETECT_STAT(x)                  (((x) >> 10) & 0x1)
#define   C_000044_FP2_DETECT_STAT                     0xFFFFFBFF
#define   S_000044_FP2_DETECT_STAT_AK(x)               (((x) & 0x1) << 10)
#define   G_000044_FP2_DETECT_STAT_AK(x)               (((x) >> 10) & 0x1)
#define   C_000044_FP2_DETECT_STAT_AK                  0xFFFFFBFF
#define   S_000044_VSYNC_DIFF_OVER_LIMIT_STAT(x)       (((x) & 0x1) << 11)
#define   G_000044_VSYNC_DIFF_OVER_LIMIT_STAT(x)       (((x) >> 11) & 0x1)
#define   C_000044_VSYNC_DIFF_OVER_LIMIT_STAT          0xFFFFF7FF
#define   S_000044_VSYNC_DIFF_OVER_LIMIT_STAT_AK(x)    (((x) & 0x1) << 11)
#define   G_000044_VSYNC_DIFF_OVER_LIMIT_STAT_AK(x)    (((x) >> 11) & 0x1)
#define   C_000044_VSYNC_DIFF_OVER_LIMIT_STAT_AK       0xFFFFF7FF
#define   S_000044_DMA_VIPH0_INT(x)                    (((x) & 0x1) << 12)
#define   G_000044_DMA_VIPH0_INT(x)                    (((x) >> 12) & 0x1)
#define   C_000044_DMA_VIPH0_INT                       0xFFFFEFFF
#define   S_000044_DMA_VIPH0_INT_AK(x)                 (((x) & 0x1) << 12)
#define   G_000044_DMA_VIPH0_INT_AK(x)                 (((x) >> 12) & 0x1)
#define   C_000044_DMA_VIPH0_INT_AK                    0xFFFFEFFF
#define   S_000044_DMA_VIPH1_INT(x)                    (((x) & 0x1) << 13)
#define   G_000044_DMA_VIPH1_INT(x)                    (((x) >> 13) & 0x1)
#define   C_000044_DMA_VIPH1_INT                       0xFFFFDFFF
#define   S_000044_DMA_VIPH1_INT_AK(x)                 (((x) & 0x1) << 13)
#define   G_000044_DMA_VIPH1_INT_AK(x)                 (((x) >> 13) & 0x1)
#define   C_000044_DMA_VIPH1_INT_AK                    0xFFFFDFFF
#define   S_000044_DMA_VIPH2_INT(x)                    (((x) & 0x1) << 14)
#define   G_000044_DMA_VIPH2_INT(x)                    (((x) >> 14) & 0x1)
#define   C_000044_DMA_VIPH2_INT                       0xFFFFBFFF
#define   S_000044_DMA_VIPH2_INT_AK(x)                 (((x) & 0x1) << 14)
#define   G_000044_DMA_VIPH2_INT_AK(x)                 (((x) >> 14) & 0x1)
#define   C_000044_DMA_VIPH2_INT_AK                    0xFFFFBFFF
#define   S_000044_DMA_VIPH3_INT(x)                    (((x) & 0x1) << 15)
#define   G_000044_DMA_VIPH3_INT(x)                    (((x) >> 15) & 0x1)
#define   C_000044_DMA_VIPH3_INT                       0xFFFF7FFF
#define   S_000044_DMA_VIPH3_INT_AK(x)                 (((x) & 0x1) << 15)
#define   G_000044_DMA_VIPH3_INT_AK(x)                 (((x) >> 15) & 0x1)
#define   C_000044_DMA_VIPH3_INT_AK                    0xFFFF7FFF
#define   S_000044_I2C_INT(x)                          (((x) & 0x1) << 17)
#define   G_000044_I2C_INT(x)                          (((x) >> 17) & 0x1)
#define   C_000044_I2C_INT                             0xFFFDFFFF
#define   S_000044_I2C_INT_AK(x)                       (((x) & 0x1) << 17)
#define   G_000044_I2C_INT_AK(x)                       (((x) >> 17) & 0x1)
#define   C_000044_I2C_INT_AK                          0xFFFDFFFF
#define   S_000044_GUI_IDLE_STAT(x)                    (((x) & 0x1) << 19)
#define   G_000044_GUI_IDLE_STAT(x)                    (((x) >> 19) & 0x1)
#define   C_000044_GUI_IDLE_STAT                       0xFFF7FFFF
#define   S_000044_GUI_IDLE_STAT_AK(x)                 (((x) & 0x1) << 19)
#define   G_000044_GUI_IDLE_STAT_AK(x)                 (((x) >> 19) & 0x1)
#define   C_000044_GUI_IDLE_STAT_AK                    0xFFF7FFFF
#define   S_000044_VIPH_INT(x)                         (((x) & 0x1) << 24)
#define   G_000044_VIPH_INT(x)                         (((x) >> 24) & 0x1)
#define   C_000044_VIPH_INT                            0xFEFFFFFF
#define   S_000044_SW_INT(x)                           (((x) & 0x1) << 25)
#define   G_000044_SW_INT(x)                           (((x) >> 25) & 0x1)
#define   C_000044_SW_INT                              0xFDFFFFFF
#define   S_000044_SW_INT_AK(x)                        (((x) & 0x1) << 25)
#define   G_000044_SW_INT_AK(x)                        (((x) >> 25) & 0x1)
#define   C_000044_SW_INT_AK                           0xFDFFFFFF
#define   S_000044_SW_INT_SET(x)                       (((x) & 0x1) << 26)
#define   G_000044_SW_INT_SET(x)                       (((x) >> 26) & 0x1)
#define   C_000044_SW_INT_SET                          0xFBFFFFFF
#define   S_000044_GEYSERVILLE_STAT(x)                 (((x) & 0x1) << 27)
#define   G_000044_GEYSERVILLE_STAT(x)                 (((x) >> 27) & 0x1)
#define   C_000044_GEYSERVILLE_STAT                    0xF7FFFFFF
#define   S_000044_GEYSERVILLE_STAT_AK(x)              (((x) & 0x1) << 27)
#define   G_000044_GEYSERVILLE_STAT_AK(x)              (((x) >> 27) & 0x1)
#define   C_000044_GEYSERVILLE_STAT_AK                 0xF7FFFFFF
#define   S_000044_HDCP_AUTHORIZED_INT_STAT(x)         (((x) & 0x1) << 28)
#define   G_000044_HDCP_AUTHORIZED_INT_STAT(x)         (((x) >> 28) & 0x1)
#define   C_000044_HDCP_AUTHORIZED_INT_STAT            0xEFFFFFFF
#define   S_000044_HDCP_AUTHORIZED_INT_AK(x)           (((x) & 0x1) << 28)
#define   G_000044_HDCP_AUTHORIZED_INT_AK(x)           (((x) >> 28) & 0x1)
#define   C_000044_HDCP_AUTHORIZED_INT_AK              0xEFFFFFFF
#define   S_000044_DVI_I2C_INT_STAT(x)                 (((x) & 0x1) << 29)
#define   G_000044_DVI_I2C_INT_STAT(x)                 (((x) >> 29) & 0x1)
#define   C_000044_DVI_I2C_INT_STAT                    0xDFFFFFFF
#define   S_000044_DVI_I2C_INT_AK(x)                   (((x) & 0x1) << 29)
#define   G_000044_DVI_I2C_INT_AK(x)                   (((x) >> 29) & 0x1)
#define   C_000044_DVI_I2C_INT_AK                      0xDFFFFFFF
#define   S_000044_GUIDMA_STAT(x)                      (((x) & 0x1) << 30)
#define   G_000044_GUIDMA_STAT(x)                      (((x) >> 30) & 0x1)
#define   C_000044_GUIDMA_STAT                         0xBFFFFFFF
#define   S_000044_GUIDMA_AK(x)                        (((x) & 0x1) << 30)
#define   G_000044_GUIDMA_AK(x)                        (((x) >> 30) & 0x1)
#define   C_000044_GUIDMA_AK                           0xBFFFFFFF
#define   S_000044_VIDDMA_STAT(x)                      (((x) & 0x1) << 31)
#define   G_000044_VIDDMA_STAT(x)                      (((x) >> 31) & 0x1)
#define   C_000044_VIDDMA_STAT                         0x7FFFFFFF
#define   S_000044_VIDDMA_AK(x)                        (((x) & 0x1) << 31)
#define   G_000044_VIDDMA_AK(x)                        (((x) >> 31) & 0x1)
#define   C_000044_VIDDMA_AK                           0x7FFFFFFF
#define R_000050_CRTC_GEN_CNTL                       0x000050
#define   S_000050_CRTC_DBL_SCAN_EN(x)                 (((x) & 0x1) << 0)
#define   G_000050_CRTC_DBL_SCAN_EN(x)                 (((x) >> 0) & 0x1)
#define   C_000050_CRTC_DBL_SCAN_EN                    0xFFFFFFFE
#define   S_000050_CRTC_INTERLACE_EN(x)                (((x) & 0x1) << 1)
#define   G_000050_CRTC_INTERLACE_EN(x)                (((x) >> 1) & 0x1)
#define   C_000050_CRTC_INTERLACE_EN                   0xFFFFFFFD
#define   S_000050_CRTC_C_SYNC_EN(x)                   (((x) & 0x1) << 4)
#define   G_000050_CRTC_C_SYNC_EN(x)                   (((x) >> 4) & 0x1)
#define   C_000050_CRTC_C_SYNC_EN                      0xFFFFFFEF
#define   S_000050_CRTC_PIX_WIDTH(x)                   (((x) & 0xF) << 8)
#define   G_000050_CRTC_PIX_WIDTH(x)                   (((x) >> 8) & 0xF)
#define   C_000050_CRTC_PIX_WIDTH                      0xFFFFF0FF
#define   S_000050_CRTC_ICON_EN(x)                     (((x) & 0x1) << 15)
#define   G_000050_CRTC_ICON_EN(x)                     (((x) >> 15) & 0x1)
#define   C_000050_CRTC_ICON_EN                        0xFFFF7FFF
#define   S_000050_CRTC_CUR_EN(x)                      (((x) & 0x1) << 16)
#define   G_000050_CRTC_CUR_EN(x)                      (((x) >> 16) & 0x1)
#define   C_000050_CRTC_CUR_EN                         0xFFFEFFFF
#define   S_000050_CRTC_VSTAT_MODE(x)                  (((x) & 0x3) << 17)
#define   G_000050_CRTC_VSTAT_MODE(x)                  (((x) >> 17) & 0x3)
#define   C_000050_CRTC_VSTAT_MODE                     0xFFF9FFFF
#define   S_000050_CRTC_CUR_MODE(x)                    (((x) & 0x7) << 20)
#define   G_000050_CRTC_CUR_MODE(x)                    (((x) >> 20) & 0x7)
#define   C_000050_CRTC_CUR_MODE                       0xFF8FFFFF
#define   S_000050_CRTC_EXT_DISP_EN(x)                 (((x) & 0x1) << 24)
#define   G_000050_CRTC_EXT_DISP_EN(x)                 (((x) >> 24) & 0x1)
#define   C_000050_CRTC_EXT_DISP_EN                    0xFEFFFFFF
#define   S_000050_CRTC_EN(x)                          (((x) & 0x1) << 25)
#define   G_000050_CRTC_EN(x)                          (((x) >> 25) & 0x1)
#define   C_000050_CRTC_EN                             0xFDFFFFFF
#define   S_000050_CRTC_DISP_REQ_EN_B(x)               (((x) & 0x1) << 26)
#define   G_000050_CRTC_DISP_REQ_EN_B(x)               (((x) >> 26) & 0x1)
#define   C_000050_CRTC_DISP_REQ_EN_B                  0xFBFFFFFF
#define R_000054_CRTC_EXT_CNTL                       0x000054
#define   S_000054_CRTC_VGA_XOVERSCAN(x)               (((x) & 0x1) << 0)
#define   G_000054_CRTC_VGA_XOVERSCAN(x)               (((x) >> 0) & 0x1)
#define   C_000054_CRTC_VGA_XOVERSCAN                  0xFFFFFFFE
#define   S_000054_VGA_BLINK_RATE(x)                   (((x) & 0x3) << 1)
#define   G_000054_VGA_BLINK_RATE(x)                   (((x) >> 1) & 0x3)
#define   C_000054_VGA_BLINK_RATE                      0xFFFFFFF9
#define   S_000054_VGA_ATI_LINEAR(x)                   (((x) & 0x1) << 3)
#define   G_000054_VGA_ATI_LINEAR(x)                   (((x) >> 3) & 0x1)
#define   C_000054_VGA_ATI_LINEAR                      0xFFFFFFF7
#define   S_000054_VGA_128KAP_PAGING(x)                (((x) & 0x1) << 4)
#define   G_000054_VGA_128KAP_PAGING(x)                (((x) >> 4) & 0x1)
#define   C_000054_VGA_128KAP_PAGING                   0xFFFFFFEF
#define   S_000054_VGA_TEXT_132(x)                     (((x) & 0x1) << 5)
#define   G_000054_VGA_TEXT_132(x)                     (((x) >> 5) & 0x1)
#define   C_000054_VGA_TEXT_132                        0xFFFFFFDF
#define   S_000054_VGA_XCRT_CNT_EN(x)                  (((x) & 0x1) << 6)
#define   G_000054_VGA_XCRT_CNT_EN(x)                  (((x) >> 6) & 0x1)
#define   C_000054_VGA_XCRT_CNT_EN                     0xFFFFFFBF
#define   S_000054_CRTC_HSYNC_DIS(x)                   (((x) & 0x1) << 8)
#define   G_000054_CRTC_HSYNC_DIS(x)                   (((x) >> 8) & 0x1)
#define   C_000054_CRTC_HSYNC_DIS                      0xFFFFFEFF
#define   S_000054_CRTC_VSYNC_DIS(x)                   (((x) & 0x1) << 9)
#define   G_000054_CRTC_VSYNC_DIS(x)                   (((x) >> 9) & 0x1)
#define   C_000054_CRTC_VSYNC_DIS                      0xFFFFFDFF
#define   S_000054_CRTC_DISPLAY_DIS(x)                 (((x) & 0x1) << 10)
#define   G_000054_CRTC_DISPLAY_DIS(x)                 (((x) >> 10) & 0x1)
#define   C_000054_CRTC_DISPLAY_DIS                    0xFFFFFBFF
#define   S_000054_CRTC_SYNC_TRISTATE(x)               (((x) & 0x1) << 11)
#define   G_000054_CRTC_SYNC_TRISTATE(x)               (((x) >> 11) & 0x1)
#define   C_000054_CRTC_SYNC_TRISTATE                  0xFFFFF7FF
#define   S_000054_CRTC_HSYNC_TRISTATE(x)              (((x) & 0x1) << 12)
#define   G_000054_CRTC_HSYNC_TRISTATE(x)              (((x) >> 12) & 0x1)
#define   C_000054_CRTC_HSYNC_TRISTATE                 0xFFFFEFFF
#define   S_000054_CRTC_VSYNC_TRISTATE(x)              (((x) & 0x1) << 13)
#define   G_000054_CRTC_VSYNC_TRISTATE(x)              (((x) >> 13) & 0x1)
#define   C_000054_CRTC_VSYNC_TRISTATE                 0xFFFFDFFF
#define   S_000054_CRT_ON(x)                           (((x) & 0x1) << 15)
#define   G_000054_CRT_ON(x)                           (((x) >> 15) & 0x1)
#define   C_000054_CRT_ON                              0xFFFF7FFF
#define   S_000054_VGA_CUR_B_TEST(x)                   (((x) & 0x1) << 17)
#define   G_000054_VGA_CUR_B_TEST(x)                   (((x) >> 17) & 0x1)
#define   C_000054_VGA_CUR_B_TEST                      0xFFFDFFFF
#define   S_000054_VGA_PACK_DIS(x)                     (((x) & 0x1) << 18)
#define   G_000054_VGA_PACK_DIS(x)                     (((x) >> 18) & 0x1)
#define   C_000054_VGA_PACK_DIS                        0xFFFBFFFF
#define   S_000054_VGA_MEM_PS_EN(x)                    (((x) & 0x1) << 19)
#define   G_000054_VGA_MEM_PS_EN(x)                    (((x) >> 19) & 0x1)
#define   C_000054_VGA_MEM_PS_EN                       0xFFF7FFFF
#define   S_000054_VCRTC_IDX_MASTER(x)                 (((x) & 0x7F) << 24)
#define   G_000054_VCRTC_IDX_MASTER(x)                 (((x) >> 24) & 0x7F)
#define   C_000054_VCRTC_IDX_MASTER                    0x80FFFFFF
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
#define R_000170_AGP_BASE                            0x000170
#define   S_000170_AGP_BASE_ADDR(x)                    (((x) & 0xFFFFFFFF) << 0)
#define   G_000170_AGP_BASE_ADDR(x)                    (((x) >> 0) & 0xFFFFFFFF)
#define   C_000170_AGP_BASE_ADDR                       0x00000000
#define R_00023C_DISPLAY_BASE_ADDR                   0x00023C
#define   S_00023C_DISPLAY_BASE_ADDR(x)                (((x) & 0xFFFFFFFF) << 0)
#define   G_00023C_DISPLAY_BASE_ADDR(x)                (((x) >> 0) & 0xFFFFFFFF)
#define   C_00023C_DISPLAY_BASE_ADDR                   0x00000000
#define R_000260_CUR_OFFSET                          0x000260
#define   S_000260_CUR_OFFSET(x)                       (((x) & 0x7FFFFFF) << 0)
#define   G_000260_CUR_OFFSET(x)                       (((x) >> 0) & 0x7FFFFFF)
#define   C_000260_CUR_OFFSET                          0xF8000000
#define   S_000260_CUR_LOCK(x)                         (((x) & 0x1) << 31)
#define   G_000260_CUR_LOCK(x)                         (((x) >> 31) & 0x1)
#define   C_000260_CUR_LOCK                            0x7FFFFFFF
#define R_00033C_CRTC2_DISPLAY_BASE_ADDR             0x00033C
#define   S_00033C_CRTC2_DISPLAY_BASE_ADDR(x)          (((x) & 0xFFFFFFFF) << 0)
#define   G_00033C_CRTC2_DISPLAY_BASE_ADDR(x)          (((x) >> 0) & 0xFFFFFFFF)
#define   C_00033C_CRTC2_DISPLAY_BASE_ADDR             0x00000000
#define R_000360_CUR2_OFFSET                         0x000360
#define   S_000360_CUR2_OFFSET(x)                      (((x) & 0x7FFFFFF) << 0)
#define   G_000360_CUR2_OFFSET(x)                      (((x) >> 0) & 0x7FFFFFF)
#define   C_000360_CUR2_OFFSET                         0xF8000000
#define   S_000360_CUR2_LOCK(x)                        (((x) & 0x1) << 31)
#define   G_000360_CUR2_LOCK(x)                        (((x) >> 31) & 0x1)
#define   C_000360_CUR2_LOCK                           0x7FFFFFFF
#define R_0003C2_GENMO_WT                            0x0003C2
#define   S_0003C2_GENMO_MONO_ADDRESS_B(x)             (((x) & 0x1) << 0)
#define   G_0003C2_GENMO_MONO_ADDRESS_B(x)             (((x) >> 0) & 0x1)
#define   C_0003C2_GENMO_MONO_ADDRESS_B                0xFE
#define   S_0003C2_VGA_RAM_EN(x)                       (((x) & 0x1) << 1)
#define   G_0003C2_VGA_RAM_EN(x)                       (((x) >> 1) & 0x1)
#define   C_0003C2_VGA_RAM_EN                          0xFD
#define   S_0003C2_VGA_CKSEL(x)                        (((x) & 0x3) << 2)
#define   G_0003C2_VGA_CKSEL(x)                        (((x) >> 2) & 0x3)
#define   C_0003C2_VGA_CKSEL                           0xF3
#define   S_0003C2_ODD_EVEN_MD_PGSEL(x)                (((x) & 0x1) << 5)
#define   G_0003C2_ODD_EVEN_MD_PGSEL(x)                (((x) >> 5) & 0x1)
#define   C_0003C2_ODD_EVEN_MD_PGSEL                   0xDF
#define   S_0003C2_VGA_HSYNC_POL(x)                    (((x) & 0x1) << 6)
#define   G_0003C2_VGA_HSYNC_POL(x)                    (((x) >> 6) & 0x1)
#define   C_0003C2_VGA_HSYNC_POL                       0xBF
#define   S_0003C2_VGA_VSYNC_POL(x)                    (((x) & 0x1) << 7)
#define   G_0003C2_VGA_VSYNC_POL(x)                    (((x) >> 7) & 0x1)
#define   C_0003C2_VGA_VSYNC_POL                       0x7F
#define R_0003F8_CRTC2_GEN_CNTL                      0x0003F8
#define   S_0003F8_CRTC2_DBL_SCAN_EN(x)                (((x) & 0x1) << 0)
#define   G_0003F8_CRTC2_DBL_SCAN_EN(x)                (((x) >> 0) & 0x1)
#define   C_0003F8_CRTC2_DBL_SCAN_EN                   0xFFFFFFFE
#define   S_0003F8_CRTC2_INTERLACE_EN(x)               (((x) & 0x1) << 1)
#define   G_0003F8_CRTC2_INTERLACE_EN(x)               (((x) >> 1) & 0x1)
#define   C_0003F8_CRTC2_INTERLACE_EN                  0xFFFFFFFD
#define   S_0003F8_CRTC2_SYNC_TRISTATE(x)              (((x) & 0x1) << 4)
#define   G_0003F8_CRTC2_SYNC_TRISTATE(x)              (((x) >> 4) & 0x1)
#define   C_0003F8_CRTC2_SYNC_TRISTATE                 0xFFFFFFEF
#define   S_0003F8_CRTC2_HSYNC_TRISTATE(x)             (((x) & 0x1) << 5)
#define   G_0003F8_CRTC2_HSYNC_TRISTATE(x)             (((x) >> 5) & 0x1)
#define   C_0003F8_CRTC2_HSYNC_TRISTATE                0xFFFFFFDF
#define   S_0003F8_CRTC2_VSYNC_TRISTATE(x)             (((x) & 0x1) << 6)
#define   G_0003F8_CRTC2_VSYNC_TRISTATE(x)             (((x) >> 6) & 0x1)
#define   C_0003F8_CRTC2_VSYNC_TRISTATE                0xFFFFFFBF
#define   S_0003F8_CRT2_ON(x)                          (((x) & 0x1) << 7)
#define   G_0003F8_CRT2_ON(x)                          (((x) >> 7) & 0x1)
#define   C_0003F8_CRT2_ON                             0xFFFFFF7F
#define   S_0003F8_CRTC2_PIX_WIDTH(x)                  (((x) & 0xF) << 8)
#define   G_0003F8_CRTC2_PIX_WIDTH(x)                  (((x) >> 8) & 0xF)
#define   C_0003F8_CRTC2_PIX_WIDTH                     0xFFFFF0FF
#define   S_0003F8_CRTC2_ICON_EN(x)                    (((x) & 0x1) << 15)
#define   G_0003F8_CRTC2_ICON_EN(x)                    (((x) >> 15) & 0x1)
#define   C_0003F8_CRTC2_ICON_EN                       0xFFFF7FFF
#define   S_0003F8_CRTC2_CUR_EN(x)                     (((x) & 0x1) << 16)
#define   G_0003F8_CRTC2_CUR_EN(x)                     (((x) >> 16) & 0x1)
#define   C_0003F8_CRTC2_CUR_EN                        0xFFFEFFFF
#define   S_0003F8_CRTC2_CUR_MODE(x)                   (((x) & 0x7) << 20)
#define   G_0003F8_CRTC2_CUR_MODE(x)                   (((x) >> 20) & 0x7)
#define   C_0003F8_CRTC2_CUR_MODE                      0xFF8FFFFF
#define   S_0003F8_CRTC2_DISPLAY_DIS(x)                (((x) & 0x1) << 23)
#define   G_0003F8_CRTC2_DISPLAY_DIS(x)                (((x) >> 23) & 0x1)
#define   C_0003F8_CRTC2_DISPLAY_DIS                   0xFF7FFFFF
#define   S_0003F8_CRTC2_EN(x)                         (((x) & 0x1) << 25)
#define   G_0003F8_CRTC2_EN(x)                         (((x) >> 25) & 0x1)
#define   C_0003F8_CRTC2_EN                            0xFDFFFFFF
#define   S_0003F8_CRTC2_DISP_REQ_EN_B(x)              (((x) & 0x1) << 26)
#define   G_0003F8_CRTC2_DISP_REQ_EN_B(x)              (((x) >> 26) & 0x1)
#define   C_0003F8_CRTC2_DISP_REQ_EN_B                 0xFBFFFFFF
#define   S_0003F8_CRTC2_C_SYNC_EN(x)                  (((x) & 0x1) << 27)
#define   G_0003F8_CRTC2_C_SYNC_EN(x)                  (((x) >> 27) & 0x1)
#define   C_0003F8_CRTC2_C_SYNC_EN                     0xF7FFFFFF
#define   S_0003F8_CRTC2_HSYNC_DIS(x)                  (((x) & 0x1) << 28)
#define   G_0003F8_CRTC2_HSYNC_DIS(x)                  (((x) >> 28) & 0x1)
#define   C_0003F8_CRTC2_HSYNC_DIS                     0xEFFFFFFF
#define   S_0003F8_CRTC2_VSYNC_DIS(x)                  (((x) & 0x1) << 29)
#define   G_0003F8_CRTC2_VSYNC_DIS(x)                  (((x) >> 29) & 0x1)
#define   C_0003F8_CRTC2_VSYNC_DIS                     0xDFFFFFFF
#define R_000420_OV0_SCALE_CNTL                      0x000420
#define   S_000420_OV0_NO_READ_BEHIND_SCAN(x)          (((x) & 0x1) << 1)
#define   G_000420_OV0_NO_READ_BEHIND_SCAN(x)          (((x) >> 1) & 0x1)
#define   C_000420_OV0_NO_READ_BEHIND_SCAN             0xFFFFFFFD
#define   S_000420_OV0_HORZ_PICK_NEAREST(x)            (((x) & 0x1) << 2)
#define   G_000420_OV0_HORZ_PICK_NEAREST(x)            (((x) >> 2) & 0x1)
#define   C_000420_OV0_HORZ_PICK_NEAREST               0xFFFFFFFB
#define   S_000420_OV0_VERT_PICK_NEAREST(x)            (((x) & 0x1) << 3)
#define   G_000420_OV0_VERT_PICK_NEAREST(x)            (((x) >> 3) & 0x1)
#define   C_000420_OV0_VERT_PICK_NEAREST               0xFFFFFFF7
#define   S_000420_OV0_SIGNED_UV(x)                    (((x) & 0x1) << 4)
#define   G_000420_OV0_SIGNED_UV(x)                    (((x) >> 4) & 0x1)
#define   C_000420_OV0_SIGNED_UV                       0xFFFFFFEF
#define   S_000420_OV0_GAMMA_SEL(x)                    (((x) & 0x7) << 5)
#define   G_000420_OV0_GAMMA_SEL(x)                    (((x) >> 5) & 0x7)
#define   C_000420_OV0_GAMMA_SEL                       0xFFFFFF1F
#define   S_000420_OV0_SURFACE_FORMAT(x)               (((x) & 0xF) << 8)
#define   G_000420_OV0_SURFACE_FORMAT(x)               (((x) >> 8) & 0xF)
#define   C_000420_OV0_SURFACE_FORMAT                  0xFFFFF0FF
#define   S_000420_OV0_ADAPTIVE_DEINT(x)               (((x) & 0x1) << 12)
#define   G_000420_OV0_ADAPTIVE_DEINT(x)               (((x) >> 12) & 0x1)
#define   C_000420_OV0_ADAPTIVE_DEINT                  0xFFFFEFFF
#define   S_000420_OV0_CRTC_SEL(x)                     (((x) & 0x1) << 14)
#define   G_000420_OV0_CRTC_SEL(x)                     (((x) >> 14) & 0x1)
#define   C_000420_OV0_CRTC_SEL                        0xFFFFBFFF
#define   S_000420_OV0_BURST_PER_PLANE(x)              (((x) & 0x7F) << 16)
#define   G_000420_OV0_BURST_PER_PLANE(x)              (((x) >> 16) & 0x7F)
#define   C_000420_OV0_BURST_PER_PLANE                 0xFF80FFFF
#define   S_000420_OV0_DOUBLE_BUFFER_REGS(x)           (((x) & 0x1) << 24)
#define   G_000420_OV0_DOUBLE_BUFFER_REGS(x)           (((x) >> 24) & 0x1)
#define   C_000420_OV0_DOUBLE_BUFFER_REGS              0xFEFFFFFF
#define   S_000420_OV0_BANDWIDTH(x)                    (((x) & 0x1) << 26)
#define   G_000420_OV0_BANDWIDTH(x)                    (((x) >> 26) & 0x1)
#define   C_000420_OV0_BANDWIDTH                       0xFBFFFFFF
#define   S_000420_OV0_LIN_TRANS_BYPASS(x)             (((x) & 0x1) << 28)
#define   G_000420_OV0_LIN_TRANS_BYPASS(x)             (((x) >> 28) & 0x1)
#define   C_000420_OV0_LIN_TRANS_BYPASS                0xEFFFFFFF
#define   S_000420_OV0_INT_EMU(x)                      (((x) & 0x1) << 29)
#define   G_000420_OV0_INT_EMU(x)                      (((x) >> 29) & 0x1)
#define   C_000420_OV0_INT_EMU                         0xDFFFFFFF
#define   S_000420_OV0_OVERLAY_EN(x)                   (((x) & 0x1) << 30)
#define   G_000420_OV0_OVERLAY_EN(x)                   (((x) >> 30) & 0x1)
#define   C_000420_OV0_OVERLAY_EN                      0xBFFFFFFF
#define   S_000420_OV0_SOFT_RESET(x)                   (((x) & 0x1) << 31)
#define   G_000420_OV0_SOFT_RESET(x)                   (((x) >> 31) & 0x1)
#define   C_000420_OV0_SOFT_RESET                      0x7FFFFFFF
#define R_00070C_CP_RB_RPTR_ADDR                     0x00070C
#define   S_00070C_RB_RPTR_SWAP(x)                     (((x) & 0x3) << 0)
#define   G_00070C_RB_RPTR_SWAP(x)                     (((x) >> 0) & 0x3)
#define   C_00070C_RB_RPTR_SWAP                        0xFFFFFFFC
#define   S_00070C_RB_RPTR_ADDR(x)                     (((x) & 0x3FFFFFFF) << 2)
#define   G_00070C_RB_RPTR_ADDR(x)                     (((x) >> 2) & 0x3FFFFFFF)
#define   C_00070C_RB_RPTR_ADDR                        0x00000003
#define R_000740_CP_CSQ_CNTL                         0x000740
#define   S_000740_CSQ_CNT_PRIMARY(x)                  (((x) & 0xFF) << 0)
#define   G_000740_CSQ_CNT_PRIMARY(x)                  (((x) >> 0) & 0xFF)
#define   C_000740_CSQ_CNT_PRIMARY                     0xFFFFFF00
#define   S_000740_CSQ_CNT_INDIRECT(x)                 (((x) & 0xFF) << 8)
#define   G_000740_CSQ_CNT_INDIRECT(x)                 (((x) >> 8) & 0xFF)
#define   C_000740_CSQ_CNT_INDIRECT                    0xFFFF00FF
#define   S_000740_CSQ_MODE(x)                         (((x) & 0xF) << 28)
#define   G_000740_CSQ_MODE(x)                         (((x) >> 28) & 0xF)
#define   C_000740_CSQ_MODE                            0x0FFFFFFF
#define R_000770_SCRATCH_UMSK                        0x000770
#define   S_000770_SCRATCH_UMSK(x)                     (((x) & 0x3F) << 0)
#define   G_000770_SCRATCH_UMSK(x)                     (((x) >> 0) & 0x3F)
#define   C_000770_SCRATCH_UMSK                        0xFFFFFFC0
#define   S_000770_SCRATCH_SWAP(x)                     (((x) & 0x3) << 16)
#define   G_000770_SCRATCH_SWAP(x)                     (((x) >> 16) & 0x3)
#define   C_000770_SCRATCH_SWAP                        0xFFFCFFFF
#define R_000774_SCRATCH_ADDR                        0x000774
#define   S_000774_SCRATCH_ADDR(x)                     (((x) & 0x7FFFFFF) << 5)
#define   G_000774_SCRATCH_ADDR(x)                     (((x) >> 5) & 0x7FFFFFF)
#define   C_000774_SCRATCH_ADDR                        0x0000001F
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


#define R_00000D_SCLK_CNTL                           0x00000D
#define   S_00000D_SCLK_SRC_SEL(x)                     (((x) & 0x7) << 0)
#define   G_00000D_SCLK_SRC_SEL(x)                     (((x) >> 0) & 0x7)
#define   C_00000D_SCLK_SRC_SEL                        0xFFFFFFF8
#define   S_00000D_TCLK_SRC_SEL(x)                     (((x) & 0x7) << 8)
#define   G_00000D_TCLK_SRC_SEL(x)                     (((x) >> 8) & 0x7)
#define   C_00000D_TCLK_SRC_SEL                        0xFFFFF8FF
#define   S_00000D_FORCE_CP(x)                         (((x) & 0x1) << 16)
#define   G_00000D_FORCE_CP(x)                         (((x) >> 16) & 0x1)
#define   C_00000D_FORCE_CP                            0xFFFEFFFF
#define   S_00000D_FORCE_HDP(x)                        (((x) & 0x1) << 17)
#define   G_00000D_FORCE_HDP(x)                        (((x) >> 17) & 0x1)
#define   C_00000D_FORCE_HDP                           0xFFFDFFFF
#define   S_00000D_FORCE_DISP(x)                       (((x) & 0x1) << 18)
#define   G_00000D_FORCE_DISP(x)                       (((x) >> 18) & 0x1)
#define   C_00000D_FORCE_DISP                          0xFFFBFFFF
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

/* PLL regs */
#define SCLK_CNTL                                      0xd
#define   FORCE_HDP                                    (1 << 17)
#define CLK_PWRMGT_CNTL                                0x14
#define   GLOBAL_PMAN_EN                               (1 << 10)
#define   DISP_PM                                      (1 << 20)
#define PLL_PWRMGT_CNTL                                0x15
#define   MPLL_TURNOFF                                 (1 << 0)
#define   SPLL_TURNOFF                                 (1 << 1)
#define   PPLL_TURNOFF                                 (1 << 2)
#define   P2PLL_TURNOFF                                (1 << 3)
#define   TVPLL_TURNOFF                                (1 << 4)
#define   MOBILE_SU                                    (1 << 16)
#define   SU_SCLK_USE_BCLK                             (1 << 17)
#define SCLK_CNTL2                                     0x1e
#define   REDUCED_SPEED_SCLK_MODE                      (1 << 16)
#define   REDUCED_SPEED_SCLK_SEL(x)                    ((x) << 17)
#define MCLK_MISC                                      0x1f
#define   EN_MCLK_TRISTATE_IN_SUSPEND                  (1 << 18)
#define SCLK_MORE_CNTL                                 0x35
#define   REDUCED_SPEED_SCLK_EN                        (1 << 16)
#define   IO_CG_VOLTAGE_DROP                           (1 << 17)
#define   VOLTAGE_DELAY_SEL(x)                         ((x) << 20)
#define   VOLTAGE_DROP_SYNC                            (1 << 19)

/* mmreg */
#define DISP_PWR_MAN                                   0xd08
#define   DISP_D3_GRPH_RST                             (1 << 18)
#define   DISP_D3_SUBPIC_RST                           (1 << 19)
#define   DISP_D3_OV0_RST                              (1 << 20)
#define   DISP_D1D2_GRPH_RST                           (1 << 21)
#define   DISP_D1D2_SUBPIC_RST                         (1 << 22)
#define   DISP_D1D2_OV0_RST                            (1 << 23)
#define   DISP_DVO_ENABLE_RST                          (1 << 24)
#define   TV_ENABLE_RST                                (1 << 25)
#define   AUTO_PWRUP_EN                                (1 << 26)

#endif
