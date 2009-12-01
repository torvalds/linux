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
#ifndef __RS600D_H__
#define __RS600D_H__

/* Registers */
#define R_000040_GEN_INT_CNTL                        0x000040
#define   S_000040_DISPLAY_INT_STATUS(x)               (((x) & 0x1) << 0)
#define   G_000040_DISPLAY_INT_STATUS(x)               (((x) >> 0) & 0x1)
#define   C_000040_DISPLAY_INT_STATUS                  0xFFFFFFFE
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
#define   S_000044_DISPLAY_INT_STAT(x)                 (((x) & 0x1) << 0)
#define   G_000044_DISPLAY_INT_STAT(x)                 (((x) >> 0) & 0x1)
#define   C_000044_DISPLAY_INT_STAT                    0xFFFFFFFE
#define   S_000044_VGA_INT_STAT(x)                     (((x) & 0x1) << 1)
#define   G_000044_VGA_INT_STAT(x)                     (((x) >> 1) & 0x1)
#define   C_000044_VGA_INT_STAT                        0xFFFFFFFD
#define   S_000044_CAP0_INT_ACTIVE(x)                  (((x) & 0x1) << 8)
#define   G_000044_CAP0_INT_ACTIVE(x)                  (((x) >> 8) & 0x1)
#define   C_000044_CAP0_INT_ACTIVE                     0xFFFFFEFF
#define   S_000044_DMA_VIPH0_INT(x)                    (((x) & 0x1) << 12)
#define   G_000044_DMA_VIPH0_INT(x)                    (((x) >> 12) & 0x1)
#define   C_000044_DMA_VIPH0_INT                       0xFFFFEFFF
#define   S_000044_DMA_VIPH1_INT(x)                    (((x) & 0x1) << 13)
#define   G_000044_DMA_VIPH1_INT(x)                    (((x) >> 13) & 0x1)
#define   C_000044_DMA_VIPH1_INT                       0xFFFFDFFF
#define   S_000044_DMA_VIPH2_INT(x)                    (((x) & 0x1) << 14)
#define   G_000044_DMA_VIPH2_INT(x)                    (((x) >> 14) & 0x1)
#define   C_000044_DMA_VIPH2_INT                       0xFFFFBFFF
#define   S_000044_DMA_VIPH3_INT(x)                    (((x) & 0x1) << 15)
#define   G_000044_DMA_VIPH3_INT(x)                    (((x) >> 15) & 0x1)
#define   C_000044_DMA_VIPH3_INT                       0xFFFF7FFF
#define   S_000044_MC_PROBE_FAULT_STAT(x)              (((x) & 0x1) << 16)
#define   G_000044_MC_PROBE_FAULT_STAT(x)              (((x) >> 16) & 0x1)
#define   C_000044_MC_PROBE_FAULT_STAT                 0xFFFEFFFF
#define   S_000044_I2C_INT(x)                          (((x) & 0x1) << 17)
#define   G_000044_I2C_INT(x)                          (((x) >> 17) & 0x1)
#define   C_000044_I2C_INT                             0xFFFDFFFF
#define   S_000044_SCRATCH_INT_STAT(x)                 (((x) & 0x1) << 18)
#define   G_000044_SCRATCH_INT_STAT(x)                 (((x) >> 18) & 0x1)
#define   C_000044_SCRATCH_INT_STAT                    0xFFFBFFFF
#define   S_000044_GUI_IDLE_STAT(x)                    (((x) & 0x1) << 19)
#define   G_000044_GUI_IDLE_STAT(x)                    (((x) >> 19) & 0x1)
#define   C_000044_GUI_IDLE_STAT                       0xFFF7FFFF
#define   S_000044_ATI_OVERDRIVE_INT_STAT(x)           (((x) & 0x1) << 20)
#define   G_000044_ATI_OVERDRIVE_INT_STAT(x)           (((x) >> 20) & 0x1)
#define   C_000044_ATI_OVERDRIVE_INT_STAT              0xFFEFFFFF
#define   S_000044_MC_PROTECTION_FAULT_STAT(x)         (((x) & 0x1) << 21)
#define   G_000044_MC_PROTECTION_FAULT_STAT(x)         (((x) >> 21) & 0x1)
#define   C_000044_MC_PROTECTION_FAULT_STAT            0xFFDFFFFF
#define   S_000044_RBBM_READ_INT_STAT(x)               (((x) & 0x1) << 22)
#define   G_000044_RBBM_READ_INT_STAT(x)               (((x) >> 22) & 0x1)
#define   C_000044_RBBM_READ_INT_STAT                  0xFFBFFFFF
#define   S_000044_CB_CONTEXT_SWITCH_STAT(x)           (((x) & 0x1) << 23)
#define   G_000044_CB_CONTEXT_SWITCH_STAT(x)           (((x) >> 23) & 0x1)
#define   C_000044_CB_CONTEXT_SWITCH_STAT              0xFF7FFFFF
#define   S_000044_VIPH_INT(x)                         (((x) & 0x1) << 24)
#define   G_000044_VIPH_INT(x)                         (((x) >> 24) & 0x1)
#define   C_000044_VIPH_INT                            0xFEFFFFFF
#define   S_000044_SW_INT(x)                           (((x) & 0x1) << 25)
#define   G_000044_SW_INT(x)                           (((x) >> 25) & 0x1)
#define   C_000044_SW_INT                              0xFDFFFFFF
#define   S_000044_SW_INT_SET(x)                       (((x) & 0x1) << 26)
#define   G_000044_SW_INT_SET(x)                       (((x) >> 26) & 0x1)
#define   C_000044_SW_INT_SET                          0xFBFFFFFF
#define   S_000044_IDCT_INT_STAT(x)                    (((x) & 0x1) << 27)
#define   G_000044_IDCT_INT_STAT(x)                    (((x) >> 27) & 0x1)
#define   C_000044_IDCT_INT_STAT                       0xF7FFFFFF
#define   S_000044_GUIDMA_STAT(x)                      (((x) & 0x1) << 30)
#define   G_000044_GUIDMA_STAT(x)                      (((x) >> 30) & 0x1)
#define   C_000044_GUIDMA_STAT                         0xBFFFFFFF
#define   S_000044_VIDDMA_STAT(x)                      (((x) & 0x1) << 31)
#define   G_000044_VIDDMA_STAT(x)                      (((x) >> 31) & 0x1)
#define   C_000044_VIDDMA_STAT                         0x7FFFFFFF
#define R_00004C_BUS_CNTL                            0x00004C
#define   S_00004C_BUS_MASTER_DIS(x)                   (((x) & 0x1) << 14)
#define   G_00004C_BUS_MASTER_DIS(x)                   (((x) >> 14) & 0x1)
#define   C_00004C_BUS_MASTER_DIS                      0xFFFFBFFF
#define   S_00004C_BUS_MSI_REARM(x)                    (((x) & 0x1) << 20)
#define   G_00004C_BUS_MSI_REARM(x)                    (((x) >> 20) & 0x1)
#define   C_00004C_BUS_MSI_REARM                       0xFFEFFFFF
#define R_000070_MC_IND_INDEX                        0x000070
#define   S_000070_MC_IND_ADDR(x)                      (((x) & 0xFFFF) << 0)
#define   G_000070_MC_IND_ADDR(x)                      (((x) >> 0) & 0xFFFF)
#define   C_000070_MC_IND_ADDR                         0xFFFF0000
#define   S_000070_MC_IND_SEQ_RBS_0(x)                 (((x) & 0x1) << 16)
#define   G_000070_MC_IND_SEQ_RBS_0(x)                 (((x) >> 16) & 0x1)
#define   C_000070_MC_IND_SEQ_RBS_0                    0xFFFEFFFF
#define   S_000070_MC_IND_SEQ_RBS_1(x)                 (((x) & 0x1) << 17)
#define   G_000070_MC_IND_SEQ_RBS_1(x)                 (((x) >> 17) & 0x1)
#define   C_000070_MC_IND_SEQ_RBS_1                    0xFFFDFFFF
#define   S_000070_MC_IND_SEQ_RBS_2(x)                 (((x) & 0x1) << 18)
#define   G_000070_MC_IND_SEQ_RBS_2(x)                 (((x) >> 18) & 0x1)
#define   C_000070_MC_IND_SEQ_RBS_2                    0xFFFBFFFF
#define   S_000070_MC_IND_SEQ_RBS_3(x)                 (((x) & 0x1) << 19)
#define   G_000070_MC_IND_SEQ_RBS_3(x)                 (((x) >> 19) & 0x1)
#define   C_000070_MC_IND_SEQ_RBS_3                    0xFFF7FFFF
#define   S_000070_MC_IND_AIC_RBS(x)                   (((x) & 0x1) << 20)
#define   G_000070_MC_IND_AIC_RBS(x)                   (((x) >> 20) & 0x1)
#define   C_000070_MC_IND_AIC_RBS                      0xFFEFFFFF
#define   S_000070_MC_IND_CITF_ARB0(x)                 (((x) & 0x1) << 21)
#define   G_000070_MC_IND_CITF_ARB0(x)                 (((x) >> 21) & 0x1)
#define   C_000070_MC_IND_CITF_ARB0                    0xFFDFFFFF
#define   S_000070_MC_IND_CITF_ARB1(x)                 (((x) & 0x1) << 22)
#define   G_000070_MC_IND_CITF_ARB1(x)                 (((x) >> 22) & 0x1)
#define   C_000070_MC_IND_CITF_ARB1                    0xFFBFFFFF
#define   S_000070_MC_IND_WR_EN(x)                     (((x) & 0x1) << 23)
#define   G_000070_MC_IND_WR_EN(x)                     (((x) >> 23) & 0x1)
#define   C_000070_MC_IND_WR_EN                        0xFF7FFFFF
#define   S_000070_MC_IND_RD_INV(x)                    (((x) & 0x1) << 24)
#define   G_000070_MC_IND_RD_INV(x)                    (((x) >> 24) & 0x1)
#define   C_000070_MC_IND_RD_INV                       0xFEFFFFFF
#define R_000074_MC_IND_DATA                         0x000074
#define   S_000074_MC_IND_DATA(x)                      (((x) & 0xFFFFFFFF) << 0)
#define   G_000074_MC_IND_DATA(x)                      (((x) >> 0) & 0xFFFFFFFF)
#define   C_000074_MC_IND_DATA                         0x00000000
#define R_000134_HDP_FB_LOCATION                     0x000134
#define   S_000134_HDP_FB_START(x)                     (((x) & 0xFFFF) << 0)
#define   G_000134_HDP_FB_START(x)                     (((x) >> 0) & 0xFFFF)
#define   C_000134_HDP_FB_START                        0xFFFF0000
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
#define R_0060A4_D1CRTC_STATUS_FRAME_COUNT           0x0060A4
#define   S_0060A4_D1CRTC_FRAME_COUNT(x)               (((x) & 0xFFFFFF) << 0)
#define   G_0060A4_D1CRTC_FRAME_COUNT(x)               (((x) >> 0) & 0xFFFFFF)
#define   C_0060A4_D1CRTC_FRAME_COUNT                  0xFF000000
#define R_006534_D1MODE_VBLANK_STATUS                0x006534
#define   S_006534_D1MODE_VBLANK_OCCURRED(x)           (((x) & 0x1) << 0)
#define   G_006534_D1MODE_VBLANK_OCCURRED(x)           (((x) >> 0) & 0x1)
#define   C_006534_D1MODE_VBLANK_OCCURRED              0xFFFFFFFE
#define   S_006534_D1MODE_VBLANK_ACK(x)                (((x) & 0x1) << 4)
#define   G_006534_D1MODE_VBLANK_ACK(x)                (((x) >> 4) & 0x1)
#define   C_006534_D1MODE_VBLANK_ACK                   0xFFFFFFEF
#define   S_006534_D1MODE_VBLANK_STAT(x)               (((x) & 0x1) << 12)
#define   G_006534_D1MODE_VBLANK_STAT(x)               (((x) >> 12) & 0x1)
#define   C_006534_D1MODE_VBLANK_STAT                  0xFFFFEFFF
#define   S_006534_D1MODE_VBLANK_INTERRUPT(x)          (((x) & 0x1) << 16)
#define   G_006534_D1MODE_VBLANK_INTERRUPT(x)          (((x) >> 16) & 0x1)
#define   C_006534_D1MODE_VBLANK_INTERRUPT             0xFFFEFFFF
#define R_006540_DxMODE_INT_MASK                     0x006540
#define   S_006540_D1MODE_VBLANK_INT_MASK(x)           (((x) & 0x1) << 0)
#define   G_006540_D1MODE_VBLANK_INT_MASK(x)           (((x) >> 0) & 0x1)
#define   C_006540_D1MODE_VBLANK_INT_MASK              0xFFFFFFFE
#define   S_006540_D1MODE_VLINE_INT_MASK(x)            (((x) & 0x1) << 4)
#define   G_006540_D1MODE_VLINE_INT_MASK(x)            (((x) >> 4) & 0x1)
#define   C_006540_D1MODE_VLINE_INT_MASK               0xFFFFFFEF
#define   S_006540_D2MODE_VBLANK_INT_MASK(x)           (((x) & 0x1) << 8)
#define   G_006540_D2MODE_VBLANK_INT_MASK(x)           (((x) >> 8) & 0x1)
#define   C_006540_D2MODE_VBLANK_INT_MASK              0xFFFFFEFF
#define   S_006540_D2MODE_VLINE_INT_MASK(x)            (((x) & 0x1) << 12)
#define   G_006540_D2MODE_VLINE_INT_MASK(x)            (((x) >> 12) & 0x1)
#define   C_006540_D2MODE_VLINE_INT_MASK               0xFFFFEFFF
#define   S_006540_D1MODE_VBLANK_CP_SEL(x)             (((x) & 0x1) << 30)
#define   G_006540_D1MODE_VBLANK_CP_SEL(x)             (((x) >> 30) & 0x1)
#define   C_006540_D1MODE_VBLANK_CP_SEL                0xBFFFFFFF
#define   S_006540_D2MODE_VBLANK_CP_SEL(x)             (((x) & 0x1) << 31)
#define   G_006540_D2MODE_VBLANK_CP_SEL(x)             (((x) >> 31) & 0x1)
#define   C_006540_D2MODE_VBLANK_CP_SEL                0x7FFFFFFF
#define R_0068A4_D2CRTC_STATUS_FRAME_COUNT           0x0068A4
#define   S_0068A4_D2CRTC_FRAME_COUNT(x)               (((x) & 0xFFFFFF) << 0)
#define   G_0068A4_D2CRTC_FRAME_COUNT(x)               (((x) >> 0) & 0xFFFFFF)
#define   C_0068A4_D2CRTC_FRAME_COUNT                  0xFF000000
#define R_006D34_D2MODE_VBLANK_STATUS                0x006D34
#define   S_006D34_D2MODE_VBLANK_OCCURRED(x)           (((x) & 0x1) << 0)
#define   G_006D34_D2MODE_VBLANK_OCCURRED(x)           (((x) >> 0) & 0x1)
#define   C_006D34_D2MODE_VBLANK_OCCURRED              0xFFFFFFFE
#define   S_006D34_D2MODE_VBLANK_ACK(x)                (((x) & 0x1) << 4)
#define   G_006D34_D2MODE_VBLANK_ACK(x)                (((x) >> 4) & 0x1)
#define   C_006D34_D2MODE_VBLANK_ACK                   0xFFFFFFEF
#define   S_006D34_D2MODE_VBLANK_STAT(x)               (((x) & 0x1) << 12)
#define   G_006D34_D2MODE_VBLANK_STAT(x)               (((x) >> 12) & 0x1)
#define   C_006D34_D2MODE_VBLANK_STAT                  0xFFFFEFFF
#define   S_006D34_D2MODE_VBLANK_INTERRUPT(x)          (((x) & 0x1) << 16)
#define   G_006D34_D2MODE_VBLANK_INTERRUPT(x)          (((x) >> 16) & 0x1)
#define   C_006D34_D2MODE_VBLANK_INTERRUPT             0xFFFEFFFF
#define R_007EDC_DISP_INTERRUPT_STATUS               0x007EDC
#define   S_007EDC_LB_D1_VBLANK_INTERRUPT(x)           (((x) & 0x1) << 4)
#define   G_007EDC_LB_D1_VBLANK_INTERRUPT(x)           (((x) >> 4) & 0x1)
#define   C_007EDC_LB_D1_VBLANK_INTERRUPT              0xFFFFFFEF
#define   S_007EDC_LB_D2_VBLANK_INTERRUPT(x)           (((x) & 0x1) << 5)
#define   G_007EDC_LB_D2_VBLANK_INTERRUPT(x)           (((x) >> 5) & 0x1)
#define   C_007EDC_LB_D2_VBLANK_INTERRUPT              0xFFFFFFDF


/* MC registers */
#define R_000000_MC_STATUS                           0x000000
#define   S_000000_MC_IDLE(x)                          (((x) & 0x1) << 0)
#define   G_000000_MC_IDLE(x)                          (((x) >> 0) & 0x1)
#define   C_000000_MC_IDLE                             0xFFFFFFFE
#define R_000004_MC_FB_LOCATION                      0x000004
#define   S_000004_MC_FB_START(x)                      (((x) & 0xFFFF) << 0)
#define   G_000004_MC_FB_START(x)                      (((x) >> 0) & 0xFFFF)
#define   C_000004_MC_FB_START                         0xFFFF0000
#define   S_000004_MC_FB_TOP(x)                        (((x) & 0xFFFF) << 16)
#define   G_000004_MC_FB_TOP(x)                        (((x) >> 16) & 0xFFFF)
#define   C_000004_MC_FB_TOP                           0x0000FFFF
#define R_000005_MC_AGP_LOCATION                     0x000005
#define   S_000005_MC_AGP_START(x)                     (((x) & 0xFFFF) << 0)
#define   G_000005_MC_AGP_START(x)                     (((x) >> 0) & 0xFFFF)
#define   C_000005_MC_AGP_START                        0xFFFF0000
#define   S_000005_MC_AGP_TOP(x)                       (((x) & 0xFFFF) << 16)
#define   G_000005_MC_AGP_TOP(x)                       (((x) >> 16) & 0xFFFF)
#define   C_000005_MC_AGP_TOP                          0x0000FFFF
#define R_000006_AGP_BASE                            0x000006
#define   S_000006_AGP_BASE_ADDR(x)                    (((x) & 0xFFFFFFFF) << 0)
#define   G_000006_AGP_BASE_ADDR(x)                    (((x) >> 0) & 0xFFFFFFFF)
#define   C_000006_AGP_BASE_ADDR                       0x00000000
#define R_000007_AGP_BASE_2                          0x000007
#define   S_000007_AGP_BASE_ADDR_2(x)                  (((x) & 0xF) << 0)
#define   G_000007_AGP_BASE_ADDR_2(x)                  (((x) >> 0) & 0xF)
#define   C_000007_AGP_BASE_ADDR_2                     0xFFFFFFF0
#define R_000009_MC_CNTL1                            0x000009
#define   S_000009_ENABLE_PAGE_TABLES(x)               (((x) & 0x1) << 26)
#define   G_000009_ENABLE_PAGE_TABLES(x)               (((x) >> 26) & 0x1)
#define   C_000009_ENABLE_PAGE_TABLES                  0xFBFFFFFF
/* FIXME don't know the various field size need feedback from AMD */
#define R_000100_MC_PT0_CNTL                         0x000100
#define   S_000100_ENABLE_PT(x)                        (((x) & 0x1) << 0)
#define   G_000100_ENABLE_PT(x)                        (((x) >> 0) & 0x1)
#define   C_000100_ENABLE_PT                           0xFFFFFFFE
#define   S_000100_EFFECTIVE_L2_CACHE_SIZE(x)          (((x) & 0x7) << 15)
#define   G_000100_EFFECTIVE_L2_CACHE_SIZE(x)          (((x) >> 15) & 0x7)
#define   C_000100_EFFECTIVE_L2_CACHE_SIZE             0xFFFC7FFF
#define   S_000100_EFFECTIVE_L2_QUEUE_SIZE(x)          (((x) & 0x7) << 21)
#define   G_000100_EFFECTIVE_L2_QUEUE_SIZE(x)          (((x) >> 21) & 0x7)
#define   C_000100_EFFECTIVE_L2_QUEUE_SIZE             0xFF1FFFFF
#define   S_000100_INVALIDATE_ALL_L1_TLBS(x)           (((x) & 0x1) << 28)
#define   G_000100_INVALIDATE_ALL_L1_TLBS(x)           (((x) >> 28) & 0x1)
#define   C_000100_INVALIDATE_ALL_L1_TLBS              0xEFFFFFFF
#define   S_000100_INVALIDATE_L2_CACHE(x)              (((x) & 0x1) << 29)
#define   G_000100_INVALIDATE_L2_CACHE(x)              (((x) >> 29) & 0x1)
#define   C_000100_INVALIDATE_L2_CACHE                 0xDFFFFFFF
#define R_000102_MC_PT0_CONTEXT0_CNTL                0x000102
#define   S_000102_ENABLE_PAGE_TABLE(x)                (((x) & 0x1) << 0)
#define   G_000102_ENABLE_PAGE_TABLE(x)                (((x) >> 0) & 0x1)
#define   C_000102_ENABLE_PAGE_TABLE                   0xFFFFFFFE
#define   S_000102_PAGE_TABLE_DEPTH(x)                 (((x) & 0x3) << 1)
#define   G_000102_PAGE_TABLE_DEPTH(x)                 (((x) >> 1) & 0x3)
#define   C_000102_PAGE_TABLE_DEPTH                    0xFFFFFFF9
#define   V_000102_PAGE_TABLE_FLAT                     0
/* R600 documentation suggest that this should be a number of pages */
#define R_000112_MC_PT0_SYSTEM_APERTURE_LOW_ADDR     0x000112
#define R_000114_MC_PT0_SYSTEM_APERTURE_HIGH_ADDR    0x000114
#define R_00011C_MC_PT0_CONTEXT0_DEFAULT_READ_ADDR   0x00011C
#define R_00012C_MC_PT0_CONTEXT0_FLAT_BASE_ADDR      0x00012C
#define R_00013C_MC_PT0_CONTEXT0_FLAT_START_ADDR     0x00013C
#define R_00014C_MC_PT0_CONTEXT0_FLAT_END_ADDR       0x00014C
#define R_00016C_MC_PT0_CLIENT0_CNTL                 0x00016C
#define   S_00016C_ENABLE_TRANSLATION_MODE_OVERRIDE(x) (((x) & 0x1) << 0)
#define   G_00016C_ENABLE_TRANSLATION_MODE_OVERRIDE(x) (((x) >> 0) & 0x1)
#define   C_00016C_ENABLE_TRANSLATION_MODE_OVERRIDE    0xFFFFFFFE
#define   S_00016C_TRANSLATION_MODE_OVERRIDE(x)        (((x) & 0x1) << 1)
#define   G_00016C_TRANSLATION_MODE_OVERRIDE(x)        (((x) >> 1) & 0x1)
#define   C_00016C_TRANSLATION_MODE_OVERRIDE           0xFFFFFFFD
#define   S_00016C_SYSTEM_ACCESS_MODE_MASK(x)          (((x) & 0x3) << 8)
#define   G_00016C_SYSTEM_ACCESS_MODE_MASK(x)          (((x) >> 8) & 0x3)
#define   C_00016C_SYSTEM_ACCESS_MODE_MASK             0xFFFFFCFF
#define   V_00016C_SYSTEM_ACCESS_MODE_PA_ONLY          0
#define   V_00016C_SYSTEM_ACCESS_MODE_USE_SYS_MAP      1
#define   V_00016C_SYSTEM_ACCESS_MODE_IN_SYS           2
#define   V_00016C_SYSTEM_ACCESS_MODE_NOT_IN_SYS       3
#define   S_00016C_SYSTEM_APERTURE_UNMAPPED_ACCESS(x)  (((x) & 0x1) << 10)
#define   G_00016C_SYSTEM_APERTURE_UNMAPPED_ACCESS(x)  (((x) >> 10) & 0x1)
#define   C_00016C_SYSTEM_APERTURE_UNMAPPED_ACCESS     0xFFFFFBFF
#define   V_00016C_SYSTEM_APERTURE_UNMAPPED_PASSTHROUGH  0
#define   V_00016C_SYSTEM_APERTURE_UNMAPPED_DEFAULT_PAGE 1
#define   S_00016C_EFFECTIVE_L1_CACHE_SIZE(x)          (((x) & 0x7) << 11)
#define   G_00016C_EFFECTIVE_L1_CACHE_SIZE(x)          (((x) >> 11) & 0x7)
#define   C_00016C_EFFECTIVE_L1_CACHE_SIZE             0xFFFFC7FF
#define   S_00016C_ENABLE_FRAGMENT_PROCESSING(x)       (((x) & 0x1) << 14)
#define   G_00016C_ENABLE_FRAGMENT_PROCESSING(x)       (((x) >> 14) & 0x1)
#define   C_00016C_ENABLE_FRAGMENT_PROCESSING          0xFFFFBFFF
#define   S_00016C_EFFECTIVE_L1_QUEUE_SIZE(x)          (((x) & 0x7) << 15)
#define   G_00016C_EFFECTIVE_L1_QUEUE_SIZE(x)          (((x) >> 15) & 0x7)
#define   C_00016C_EFFECTIVE_L1_QUEUE_SIZE             0xFFFC7FFF
#define   S_00016C_INVALIDATE_L1_TLB(x)                (((x) & 0x1) << 20)
#define   G_00016C_INVALIDATE_L1_TLB(x)                (((x) >> 20) & 0x1)
#define   C_00016C_INVALIDATE_L1_TLB                   0xFFEFFFFF

#endif
