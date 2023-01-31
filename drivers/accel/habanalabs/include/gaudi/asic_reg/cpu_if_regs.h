/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_CPU_IF_REGS_H_
#define ASIC_REG_CPU_IF_REGS_H_

/*
 *****************************************
 *   CPU_IF (Prototype: CPU_IF)
 *****************************************
 */

#define mmCPU_IF_ARUSER_OVR                                          0x442104

#define mmCPU_IF_ARUSER_OVR_EN                                       0x442108

#define mmCPU_IF_AWUSER_OVR                                          0x44210C

#define mmCPU_IF_AWUSER_OVR_EN                                       0x442110

#define mmCPU_IF_AXCACHE_OVR                                         0x442114

#define mmCPU_IF_LOCK_OVR                                            0x442118

#define mmCPU_IF_PROT_OVR                                            0x44211C

#define mmCPU_IF_MAX_OUTSTANDING                                     0x442120

#define mmCPU_IF_EARLY_BRESP_EN                                      0x442124

#define mmCPU_IF_FORCE_RSP_OK                                        0x442128

#define mmCPU_IF_CPU_MSB_ADDR                                        0x44212C

#define mmCPU_IF_AXI_SPLIT_INTR                                      0x442130

#define mmCPU_IF_TOTAL_WR_CNT                                        0x442140

#define mmCPU_IF_INFLIGHT_WR_CNT                                     0x442144

#define mmCPU_IF_TOTAL_RD_CNT                                        0x442150

#define mmCPU_IF_INFLIGHT_RD_CNT                                     0x442154

#define mmCPU_IF_PF_PQ_PI                                            0x442200

#define mmCPU_IF_PQ_BASE_ADDR_LOW                                    0x442204

#define mmCPU_IF_PQ_BASE_ADDR_HIGH                                   0x442208

#define mmCPU_IF_PQ_LENGTH                                           0x44220C

#define mmCPU_IF_CQ_BASE_ADDR_LOW                                    0x442210

#define mmCPU_IF_CQ_BASE_ADDR_HIGH                                   0x442214

#define mmCPU_IF_CQ_LENGTH                                           0x442218

#define mmCPU_IF_EQ_BASE_ADDR_LOW                                    0x442220

#define mmCPU_IF_EQ_BASE_ADDR_HIGH                                   0x442224

#define mmCPU_IF_EQ_LENGTH                                           0x442228

#define mmCPU_IF_EQ_RD_OFFS                                          0x44222C

#define mmCPU_IF_QUEUE_INIT                                          0x442230

#define mmCPU_IF_TPC_SERR_INTR_STS                                   0x442300

#define mmCPU_IF_TPC_SERR_INTR_CLR                                   0x442304

#define mmCPU_IF_TPC_SERR_INTR_MASK                                  0x442308

#define mmCPU_IF_TPC_DERR_INTR_STS                                   0x442310

#define mmCPU_IF_TPC_DERR_INTR_CLR                                   0x442314

#define mmCPU_IF_TPC_DERR_INTR_MASK                                  0x442318

#define mmCPU_IF_DMA_SERR_INTR_STS                                   0x442320

#define mmCPU_IF_DMA_SERR_INTR_CLR                                   0x442324

#define mmCPU_IF_DMA_SERR_INTR_MASK                                  0x442328

#define mmCPU_IF_DMA_DERR_INTR_STS                                   0x442330

#define mmCPU_IF_DMA_DERR_INTR_CLR                                   0x442334

#define mmCPU_IF_DMA_DERR_INTR_MASK                                  0x442338

#define mmCPU_IF_SRAM_SERR_INTR_STS                                  0x442340

#define mmCPU_IF_SRAM_SERR_INTR_CLR                                  0x442344

#define mmCPU_IF_SRAM_SERR_INTR_MASK                                 0x442348

#define mmCPU_IF_SRAM_DERR_INTR_STS                                  0x442350

#define mmCPU_IF_SRAM_DERR_INTR_CLR                                  0x442354

#define mmCPU_IF_SRAM_DERR_INTR_MASK                                 0x442358

#define mmCPU_IF_NIC_SERR_INTR_STS                                   0x442360

#define mmCPU_IF_NIC_SERR_INTR_CLR                                   0x442364

#define mmCPU_IF_NIC_SERR_INTR_MASK                                  0x442368

#define mmCPU_IF_NIC_DERR_INTR_STS                                   0x442370

#define mmCPU_IF_NIC_DERR_INTR_CLR                                   0x442374

#define mmCPU_IF_NIC_DERR_INTR_MASK                                  0x442378

#define mmCPU_IF_DMA_IF_SERR_INTR_STS                                0x442380

#define mmCPU_IF_DMA_IF_SERR_INTR_CLR                                0x442384

#define mmCPU_IF_DMA_IF_SERR_INTR_MASK                               0x442388

#define mmCPU_IF_DMA_IF_DERR_INTR_STS                                0x442390

#define mmCPU_IF_DMA_IF_DERR_INTR_CLR                                0x442394

#define mmCPU_IF_DMA_IF_DERR_INTR_MASK                               0x442398

#define mmCPU_IF_HBM_SERR_INTR_STS                                   0x4423A0

#define mmCPU_IF_HBM_SERR_INTR_CLR                                   0x4423A4

#define mmCPU_IF_HBM_SERR_INTR_MASK                                  0x4423A8

#define mmCPU_IF_HBM_DERR_INTR_STS                                   0x4423B0

#define mmCPU_IF_HBM_DERR_INTR_CLR                                   0x4423B4

#define mmCPU_IF_HBM_DERR_INTR_MASK                                  0x4423B8

#define mmCPU_IF_PLL_SEI_INTR_STS                                    0x442400

#define mmCPU_IF_PLL_SEI_INTR_CLR                                    0x442404

#define mmCPU_IF_PLL_SEI_INTR_MASK                                   0x442408

#define mmCPU_IF_NIC_SEI_INTR_STS                                    0x442410

#define mmCPU_IF_NIC_SEI_INTR_CLR                                    0x442414

#define mmCPU_IF_NIC_SEI_INTR_MASK                                   0x442418

#define mmCPU_IF_DMA_SEI_INTR_STS                                    0x442420

#define mmCPU_IF_DMA_SEI_INTR_CLR                                    0x442424

#define mmCPU_IF_DMA_SEI_INTR_MASK                                   0x442428

#define mmCPU_IF_DMA_IF_SEI_INTR_STS                                 0x442430

#define mmCPU_IF_DMA_IF_SEI_INTR_CLR                                 0x442434

#define mmCPU_IF_DMA_IF_SEI_INTR_MASK                                0x442438

#endif /* ASIC_REG_CPU_IF_REGS_H_ */
