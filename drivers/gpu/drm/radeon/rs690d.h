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
#ifndef __RS690D_H__
#define __RS690D_H__

/* Registers */
#define R_000078_MC_INDEX                            0x000078
#define   S_000078_MC_IND_ADDR(x)                      (((x) & 0x1FF) << 0)
#define   G_000078_MC_IND_ADDR(x)                      (((x) >> 0) & 0x1FF)
#define   C_000078_MC_IND_ADDR                         0xFFFFFE00
#define   S_000078_MC_IND_WR_EN(x)                     (((x) & 0x1) << 9)
#define   G_000078_MC_IND_WR_EN(x)                     (((x) >> 9) & 0x1)
#define   C_000078_MC_IND_WR_EN                        0xFFFFFDFF
#define R_00007C_MC_DATA                             0x00007C
#define   S_00007C_MC_DATA(x)                          (((x) & 0xFFFFFFFF) << 0)
#define   G_00007C_MC_DATA(x)                          (((x) >> 0) & 0xFFFFFFFF)
#define   C_00007C_MC_DATA                             0x00000000
#define R_0000F8_CONFIG_MEMSIZE                      0x0000F8
#define   S_0000F8_CONFIG_MEMSIZE(x)                   (((x) & 0xFFFFFFFF) << 0)
#define   G_0000F8_CONFIG_MEMSIZE(x)                   (((x) >> 0) & 0xFFFFFFFF)
#define   C_0000F8_CONFIG_MEMSIZE                      0x00000000
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
#define R_006520_DC_LB_MEMORY_SPLIT                  0x006520
#define   S_006520_DC_LB_MEMORY_SPLIT(x)               (((x) & 0x3) << 0)
#define   G_006520_DC_LB_MEMORY_SPLIT(x)               (((x) >> 0) & 0x3)
#define   C_006520_DC_LB_MEMORY_SPLIT                  0xFFFFFFFC
#define   S_006520_DC_LB_MEMORY_SPLIT_MODE(x)          (((x) & 0x1) << 2)
#define   G_006520_DC_LB_MEMORY_SPLIT_MODE(x)          (((x) >> 2) & 0x1)
#define   C_006520_DC_LB_MEMORY_SPLIT_MODE             0xFFFFFFFB
#define   V_006520_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF    0
#define   V_006520_DC_LB_MEMORY_SPLIT_D1_3Q_D2_1Q      1
#define   V_006520_DC_LB_MEMORY_SPLIT_D1_ONLY          2
#define   V_006520_DC_LB_MEMORY_SPLIT_D1_1Q_D2_3Q      3
#define   S_006520_DC_LB_DISP1_END_ADR(x)              (((x) & 0x7FF) << 4)
#define   G_006520_DC_LB_DISP1_END_ADR(x)              (((x) >> 4) & 0x7FF)
#define   C_006520_DC_LB_DISP1_END_ADR                 0xFFFF800F
#define R_006548_D1MODE_PRIORITY_A_CNT               0x006548
#define   S_006548_D1MODE_PRIORITY_MARK_A(x)           (((x) & 0x7FFF) << 0)
#define   G_006548_D1MODE_PRIORITY_MARK_A(x)           (((x) >> 0) & 0x7FFF)
#define   C_006548_D1MODE_PRIORITY_MARK_A              0xFFFF8000
#define   S_006548_D1MODE_PRIORITY_A_OFF(x)            (((x) & 0x1) << 16)
#define   G_006548_D1MODE_PRIORITY_A_OFF(x)            (((x) >> 16) & 0x1)
#define   C_006548_D1MODE_PRIORITY_A_OFF               0xFFFEFFFF
#define   S_006548_D1MODE_PRIORITY_A_ALWAYS_ON(x)      (((x) & 0x1) << 20)
#define   G_006548_D1MODE_PRIORITY_A_ALWAYS_ON(x)      (((x) >> 20) & 0x1)
#define   C_006548_D1MODE_PRIORITY_A_ALWAYS_ON         0xFFEFFFFF
#define   S_006548_D1MODE_PRIORITY_A_FORCE_MASK(x)     (((x) & 0x1) << 24)
#define   G_006548_D1MODE_PRIORITY_A_FORCE_MASK(x)     (((x) >> 24) & 0x1)
#define   C_006548_D1MODE_PRIORITY_A_FORCE_MASK        0xFEFFFFFF
#define R_00654C_D1MODE_PRIORITY_B_CNT               0x00654C
#define   S_00654C_D1MODE_PRIORITY_MARK_B(x)           (((x) & 0x7FFF) << 0)
#define   G_00654C_D1MODE_PRIORITY_MARK_B(x)           (((x) >> 0) & 0x7FFF)
#define   C_00654C_D1MODE_PRIORITY_MARK_B              0xFFFF8000
#define   S_00654C_D1MODE_PRIORITY_B_OFF(x)            (((x) & 0x1) << 16)
#define   G_00654C_D1MODE_PRIORITY_B_OFF(x)            (((x) >> 16) & 0x1)
#define   C_00654C_D1MODE_PRIORITY_B_OFF               0xFFFEFFFF
#define   S_00654C_D1MODE_PRIORITY_B_ALWAYS_ON(x)      (((x) & 0x1) << 20)
#define   G_00654C_D1MODE_PRIORITY_B_ALWAYS_ON(x)      (((x) >> 20) & 0x1)
#define   C_00654C_D1MODE_PRIORITY_B_ALWAYS_ON         0xFFEFFFFF
#define   S_00654C_D1MODE_PRIORITY_B_FORCE_MASK(x)     (((x) & 0x1) << 24)
#define   G_00654C_D1MODE_PRIORITY_B_FORCE_MASK(x)     (((x) >> 24) & 0x1)
#define   C_00654C_D1MODE_PRIORITY_B_FORCE_MASK        0xFEFFFFFF
#define R_006C9C_DCP_CONTROL                         0x006C9C
#define R_006D48_D2MODE_PRIORITY_A_CNT               0x006D48
#define   S_006D48_D2MODE_PRIORITY_MARK_A(x)           (((x) & 0x7FFF) << 0)
#define   G_006D48_D2MODE_PRIORITY_MARK_A(x)           (((x) >> 0) & 0x7FFF)
#define   C_006D48_D2MODE_PRIORITY_MARK_A              0xFFFF8000
#define   S_006D48_D2MODE_PRIORITY_A_OFF(x)            (((x) & 0x1) << 16)
#define   G_006D48_D2MODE_PRIORITY_A_OFF(x)            (((x) >> 16) & 0x1)
#define   C_006D48_D2MODE_PRIORITY_A_OFF               0xFFFEFFFF
#define   S_006D48_D2MODE_PRIORITY_A_ALWAYS_ON(x)      (((x) & 0x1) << 20)
#define   G_006D48_D2MODE_PRIORITY_A_ALWAYS_ON(x)      (((x) >> 20) & 0x1)
#define   C_006D48_D2MODE_PRIORITY_A_ALWAYS_ON         0xFFEFFFFF
#define   S_006D48_D2MODE_PRIORITY_A_FORCE_MASK(x)     (((x) & 0x1) << 24)
#define   G_006D48_D2MODE_PRIORITY_A_FORCE_MASK(x)     (((x) >> 24) & 0x1)
#define   C_006D48_D2MODE_PRIORITY_A_FORCE_MASK        0xFEFFFFFF
#define R_006D4C_D2MODE_PRIORITY_B_CNT               0x006D4C
#define   S_006D4C_D2MODE_PRIORITY_MARK_B(x)           (((x) & 0x7FFF) << 0)
#define   G_006D4C_D2MODE_PRIORITY_MARK_B(x)           (((x) >> 0) & 0x7FFF)
#define   C_006D4C_D2MODE_PRIORITY_MARK_B              0xFFFF8000
#define   S_006D4C_D2MODE_PRIORITY_B_OFF(x)            (((x) & 0x1) << 16)
#define   G_006D4C_D2MODE_PRIORITY_B_OFF(x)            (((x) >> 16) & 0x1)
#define   C_006D4C_D2MODE_PRIORITY_B_OFF               0xFFFEFFFF
#define   S_006D4C_D2MODE_PRIORITY_B_ALWAYS_ON(x)      (((x) & 0x1) << 20)
#define   G_006D4C_D2MODE_PRIORITY_B_ALWAYS_ON(x)      (((x) >> 20) & 0x1)
#define   C_006D4C_D2MODE_PRIORITY_B_ALWAYS_ON         0xFFEFFFFF
#define   S_006D4C_D2MODE_PRIORITY_B_FORCE_MASK(x)     (((x) & 0x1) << 24)
#define   G_006D4C_D2MODE_PRIORITY_B_FORCE_MASK(x)     (((x) >> 24) & 0x1)
#define   C_006D4C_D2MODE_PRIORITY_B_FORCE_MASK        0xFEFFFFFF
#define R_006D58_LB_MAX_REQ_OUTSTANDING              0x006D58
#define   S_006D58_LB_D1_MAX_REQ_OUTSTANDING(x)        (((x) & 0xF) << 0)
#define   G_006D58_LB_D1_MAX_REQ_OUTSTANDING(x)        (((x) >> 0) & 0xF)
#define   C_006D58_LB_D1_MAX_REQ_OUTSTANDING           0xFFFFFFF0
#define   S_006D58_LB_D2_MAX_REQ_OUTSTANDING(x)        (((x) & 0xF) << 16)
#define   G_006D58_LB_D2_MAX_REQ_OUTSTANDING(x)        (((x) >> 16) & 0xF)
#define   C_006D58_LB_D2_MAX_REQ_OUTSTANDING           0xFFF0FFFF


#define R_000090_MC_SYSTEM_STATUS                    0x000090
#define   S_000090_MC_SYSTEM_IDLE(x)                   (((x) & 0x1) << 0)
#define   G_000090_MC_SYSTEM_IDLE(x)                   (((x) >> 0) & 0x1)
#define   C_000090_MC_SYSTEM_IDLE                      0xFFFFFFFE
#define   S_000090_MC_SEQUENCER_IDLE(x)                (((x) & 0x1) << 1)
#define   G_000090_MC_SEQUENCER_IDLE(x)                (((x) >> 1) & 0x1)
#define   C_000090_MC_SEQUENCER_IDLE                   0xFFFFFFFD
#define   S_000090_MC_ARBITER_IDLE(x)                  (((x) & 0x1) << 2)
#define   G_000090_MC_ARBITER_IDLE(x)                  (((x) >> 2) & 0x1)
#define   C_000090_MC_ARBITER_IDLE                     0xFFFFFFFB
#define   S_000090_MC_SELECT_PM(x)                     (((x) & 0x1) << 3)
#define   G_000090_MC_SELECT_PM(x)                     (((x) >> 3) & 0x1)
#define   C_000090_MC_SELECT_PM                        0xFFFFFFF7
#define   S_000090_RESERVED4(x)                        (((x) & 0xF) << 4)
#define   G_000090_RESERVED4(x)                        (((x) >> 4) & 0xF)
#define   C_000090_RESERVED4                           0xFFFFFF0F
#define   S_000090_RESERVED8(x)                        (((x) & 0xF) << 8)
#define   G_000090_RESERVED8(x)                        (((x) >> 8) & 0xF)
#define   C_000090_RESERVED8                           0xFFFFF0FF
#define   S_000090_RESERVED12(x)                       (((x) & 0xF) << 12)
#define   G_000090_RESERVED12(x)                       (((x) >> 12) & 0xF)
#define   C_000090_RESERVED12                          0xFFFF0FFF
#define   S_000090_MCA_INIT_EXECUTED(x)                (((x) & 0x1) << 16)
#define   G_000090_MCA_INIT_EXECUTED(x)                (((x) >> 16) & 0x1)
#define   C_000090_MCA_INIT_EXECUTED                   0xFFFEFFFF
#define   S_000090_MCA_IDLE(x)                         (((x) & 0x1) << 17)
#define   G_000090_MCA_IDLE(x)                         (((x) >> 17) & 0x1)
#define   C_000090_MCA_IDLE                            0xFFFDFFFF
#define   S_000090_MCA_SEQ_IDLE(x)                     (((x) & 0x1) << 18)
#define   G_000090_MCA_SEQ_IDLE(x)                     (((x) >> 18) & 0x1)
#define   C_000090_MCA_SEQ_IDLE                        0xFFFBFFFF
#define   S_000090_MCA_ARB_IDLE(x)                     (((x) & 0x1) << 19)
#define   G_000090_MCA_ARB_IDLE(x)                     (((x) >> 19) & 0x1)
#define   C_000090_MCA_ARB_IDLE                        0xFFF7FFFF
#define   S_000090_RESERVED20(x)                       (((x) & 0xFFF) << 20)
#define   G_000090_RESERVED20(x)                       (((x) >> 20) & 0xFFF)
#define   C_000090_RESERVED20                          0x000FFFFF
#define R_000100_MCCFG_FB_LOCATION                   0x000100
#define   S_000100_MC_FB_START(x)                      (((x) & 0xFFFF) << 0)
#define   G_000100_MC_FB_START(x)                      (((x) >> 0) & 0xFFFF)
#define   C_000100_MC_FB_START                         0xFFFF0000
#define   S_000100_MC_FB_TOP(x)                        (((x) & 0xFFFF) << 16)
#define   G_000100_MC_FB_TOP(x)                        (((x) >> 16) & 0xFFFF)
#define   C_000100_MC_FB_TOP                           0x0000FFFF
#define R_000104_MC_INIT_MISC_LAT_TIMER              0x000104
#define   S_000104_MC_CPR_INIT_LAT(x)                  (((x) & 0xF) << 0)
#define   G_000104_MC_CPR_INIT_LAT(x)                  (((x) >> 0) & 0xF)
#define   C_000104_MC_CPR_INIT_LAT                     0xFFFFFFF0
#define   S_000104_MC_VF_INIT_LAT(x)                   (((x) & 0xF) << 4)
#define   G_000104_MC_VF_INIT_LAT(x)                   (((x) >> 4) & 0xF)
#define   C_000104_MC_VF_INIT_LAT                      0xFFFFFF0F
#define   S_000104_MC_DISP0R_INIT_LAT(x)               (((x) & 0xF) << 8)
#define   G_000104_MC_DISP0R_INIT_LAT(x)               (((x) >> 8) & 0xF)
#define   C_000104_MC_DISP0R_INIT_LAT                  0xFFFFF0FF
#define   S_000104_MC_DISP1R_INIT_LAT(x)               (((x) & 0xF) << 12)
#define   G_000104_MC_DISP1R_INIT_LAT(x)               (((x) >> 12) & 0xF)
#define   C_000104_MC_DISP1R_INIT_LAT                  0xFFFF0FFF
#define   S_000104_MC_FIXED_INIT_LAT(x)                (((x) & 0xF) << 16)
#define   G_000104_MC_FIXED_INIT_LAT(x)                (((x) >> 16) & 0xF)
#define   C_000104_MC_FIXED_INIT_LAT                   0xFFF0FFFF
#define   S_000104_MC_E2R_INIT_LAT(x)                  (((x) & 0xF) << 20)
#define   G_000104_MC_E2R_INIT_LAT(x)                  (((x) >> 20) & 0xF)
#define   C_000104_MC_E2R_INIT_LAT                     0xFF0FFFFF
#define   S_000104_SAME_PAGE_PRIO(x)                   (((x) & 0xF) << 24)
#define   G_000104_SAME_PAGE_PRIO(x)                   (((x) >> 24) & 0xF)
#define   C_000104_SAME_PAGE_PRIO                      0xF0FFFFFF
#define   S_000104_MC_GLOBW_INIT_LAT(x)                (((x) & 0xF) << 28)
#define   G_000104_MC_GLOBW_INIT_LAT(x)                (((x) >> 28) & 0xF)
#define   C_000104_MC_GLOBW_INIT_LAT                   0x0FFFFFFF

#endif
