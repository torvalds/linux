// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* Bus-Access-Manager (BAM) Hardware manager. */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/memory.h>

#include "bam.h"
#include "sps_bam.h"

/**
 *  Valid BAM Hardware version.
 *
 */
#define BAM_MIN_VERSION 2
#define BAM_MAX_VERSION 0x2f

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM

/* Maximum number of execution environment */
#define BAM_MAX_EES 8

/**
 *  BAM Hardware registers bitmask.
 *  format: <register>_<field>
 *
 */
/* CTRL */
#define BAM_MESS_ONLY_CANCEL_WB               0x100000
#define CACHE_MISS_ERR_RESP_EN                 0x80000
#define LOCAL_CLK_GATING                       0x60000
#define IBC_DISABLE                            0x10000
#define BAM_CACHED_DESC_STORE                   0x8000
#define BAM_DESC_CACHE_SEL                      0x6000
#define BAM_EN_ACCUM                              0x10
#define BAM_EN                                     0x2
#define BAM_SW_RST                                 0x1

/* REVISION */
#define BAM_INACTIV_TMR_BASE                0xff000000
#define BAM_CMD_DESC_EN                       0x800000
#define BAM_DESC_CACHE_DEPTH                  0x600000
#define BAM_NUM_INACTIV_TMRS                  0x100000
#define BAM_INACTIV_TMRS_EXST                  0x80000
#define BAM_HIGH_FREQUENCY_BAM                 0x40000
#define BAM_HAS_NO_BYPASS                      0x20000
#define BAM_SECURED                            0x10000
#define BAM_USE_VMIDMT                          0x8000
#define BAM_AXI_ACTIVE                          0x4000
#define BAM_CE_BUFFER_SIZE                      0x3000
#define BAM_NUM_EES                              0xf00
#define BAM_REVISION                              0xff

/* SW_REVISION */
#define BAM_MAJOR                           0xf0000000
#define BAM_MINOR                            0xfff0000
#define BAM_STEP                                0xffff

/* NUM_PIPES */
#define BAM_NON_PIPE_GRP                    0xff000000
#define BAM_PERIPH_NON_PIPE_GRP               0xff0000
#define BAM_DATA_ADDR_BUS_WIDTH                 0xC000
#define BAM_NUM_PIPES                             0xff

/* TIMER */
#define BAM_TIMER                               0xffff

/* TIMER_CTRL */
#define TIMER_RST                           0x80000000
#define TIMER_RUN                           0x40000000
#define TIMER_MODE                          0x20000000
#define TIMER_TRSHLD                            0xffff

/* DESC_CNT_TRSHLD */
#define BAM_DESC_CNT_TRSHLD                     0xffff

/* IRQ_SRCS */
#define BAM_IRQ                         0x80000000
#define P_IRQ                           0x7fffffff

/* IRQ_STTS */
#define IRQ_STTS_BAM_TIMER_IRQ                         0x10
#define IRQ_STTS_BAM_EMPTY_IRQ                          0x8
#define IRQ_STTS_BAM_ERROR_IRQ                          0x4
#define IRQ_STTS_BAM_HRESP_ERR_IRQ                      0x2

/* IRQ_CLR */
#define IRQ_CLR_BAM_TIMER_IRQ                          0x10
#define IRQ_CLR_BAM_EMPTY_CLR                           0x8
#define IRQ_CLR_BAM_ERROR_CLR                           0x4
#define IRQ_CLR_BAM_HRESP_ERR_CLR                       0x2

/* IRQ_EN */
#define IRQ_EN_BAM_TIMER_IRQ                           0x10
#define IRQ_EN_BAM_EMPTY_EN                             0x8
#define IRQ_EN_BAM_ERROR_EN                             0x4
#define IRQ_EN_BAM_HRESP_ERR_EN                         0x2

/* AHB_MASTER_ERR_CTRLS */
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HVMID         0x7c0000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_DIRECT_MODE    0x20000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HCID           0x1f000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HPROT            0xf00
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HBURST            0xe0
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HSIZE             0x18
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HWRITE             0x4
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HTRANS             0x3

/* TRUST_REG  */
#define LOCK_EE_CTRL                            0x2000
#define BAM_VMID                                0x1f00
#define BAM_RST_BLOCK                             0x80
#define BAM_EE                                     0x7

/* TEST_BUS_SEL */
#define BAM_SW_EVENTS_ZERO                    0x200000
#define BAM_SW_EVENTS_SEL                     0x180000
#define BAM_DATA_ERASE                         0x40000
#define BAM_DATA_FLUSH                         0x20000
#define BAM_CLK_ALWAYS_ON                      0x10000
#define BAM_TESTBUS_SEL                           0x7f

/* CNFG_BITS */
#define CNFG_BITS_AOS_OVERFLOW_PRVNT		 0x80000000
#define CNFG_BITS_MULTIPLE_EVENTS_DESC_AVAIL_EN  0x40000000
#define CNFG_BITS_MULTIPLE_EVENTS_SIZE_EN        0x20000000
#define CNFG_BITS_BAM_ZLT_W_CD_SUPPORT           0x10000000
#define CNFG_BITS_BAM_CD_ENABLE                   0x8000000
#define CNFG_BITS_BAM_AU_ACCUMED                  0x4000000
#define CNFG_BITS_BAM_PSM_P_HD_DATA               0x2000000
#define CNFG_BITS_BAM_REG_P_EN                    0x1000000
#define CNFG_BITS_BAM_WB_DSC_AVL_P_RST             0x800000
#define CNFG_BITS_BAM_WB_RETR_SVPNT                0x400000
#define CNFG_BITS_BAM_WB_CSW_ACK_IDL               0x200000
#define CNFG_BITS_BAM_WB_BLK_CSW                   0x100000
#define CNFG_BITS_BAM_WB_P_RES                      0x80000
#define CNFG_BITS_BAM_SI_P_RES                      0x40000
#define CNFG_BITS_BAM_AU_P_RES                      0x20000
#define CNFG_BITS_BAM_PSM_P_RES                     0x10000
#define CNFG_BITS_BAM_PSM_CSW_REQ                    0x8000
#define CNFG_BITS_BAM_SB_CLK_REQ                     0x4000
#define CNFG_BITS_BAM_IBC_DISABLE                    0x2000
#define CNFG_BITS_BAM_NO_EXT_P_RST                   0x1000
#define CNFG_BITS_BAM_FULL_PIPE                       0x800
#define CNFG_BITS_BAM_PIPE_CNFG                         0x4

/* PIPE_ATTR_EEn*/
#define BAM_ENABLED                              0x80000000
#define P_ATTR                                   0x7fffffff

/* P_ctrln */
#define P_LOCK_GROUP                          0x1f0000
#define P_WRITE_NWD                              0x800
#define P_PREFETCH_LIMIT                         0x600
#define P_AUTO_EOB_SEL                           0x180
#define P_AUTO_EOB                                0x40
#define P_SYS_MODE                                0x20
#define P_SYS_STRM                                0x10
#define P_DIRECTION                                0x8
#define P_EN                                       0x2

/* P_RSTn */
#define P_RST_P_SW_RST                             0x1

/* P_HALTn */
#define P_HALT_P_PIPE_EMPTY			   0x8
#define P_HALT_P_LAST_DESC_ZLT                     0x4
#define P_HALT_P_PROD_HALTED                       0x2
#define P_HALT_P_HALT                              0x1

/* P_TRUST_REGn */
#define BAM_P_VMID                              0x1f00
#define BAM_P_SUP_GROUP                           0xf8
#define BAM_P_EE                                   0x7

/* P_IRQ_STTSn */
#define P_IRQ_STTS_P_HRESP_ERR_IRQ                0x80
#define P_IRQ_STTS_P_PIPE_RST_ERR_IRQ             0x40
#define P_IRQ_STTS_P_TRNSFR_END_IRQ               0x20
#define P_IRQ_STTS_P_ERR_IRQ                      0x10
#define P_IRQ_STTS_P_OUT_OF_DESC_IRQ               0x8
#define P_IRQ_STTS_P_WAKE_IRQ                      0x4
#define P_IRQ_STTS_P_TIMER_IRQ                     0x2
#define P_IRQ_STTS_P_PRCSD_DESC_IRQ                0x1

/* P_IRQ_CLRn */
#define P_IRQ_CLR_P_HRESP_ERR_CLR                 0x80
#define P_IRQ_CLR_P_PIPE_RST_ERR_CLR              0x40
#define P_IRQ_CLR_P_TRNSFR_END_CLR                0x20
#define P_IRQ_CLR_P_ERR_CLR                       0x10
#define P_IRQ_CLR_P_OUT_OF_DESC_CLR                0x8
#define P_IRQ_CLR_P_WAKE_CLR                       0x4
#define P_IRQ_CLR_P_TIMER_CLR                      0x2
#define P_IRQ_CLR_P_PRCSD_DESC_CLR                 0x1

/* P_IRQ_ENn */
#define P_IRQ_EN_P_HRESP_ERR_EN                   0x80
#define P_IRQ_EN_P_PIPE_RST_ERR_EN                0x40
#define P_IRQ_EN_P_TRNSFR_END_EN                  0x20
#define P_IRQ_EN_P_ERR_EN                         0x10
#define P_IRQ_EN_P_OUT_OF_DESC_EN                  0x8
#define P_IRQ_EN_P_WAKE_EN                         0x4
#define P_IRQ_EN_P_TIMER_EN                        0x2
#define P_IRQ_EN_P_PRCSD_DESC_EN                   0x1

/* P_TIMERn */
#define P_TIMER_P_TIMER                         0xffff

/* P_TIMER_ctrln */
#define P_TIMER_RST                         0x80000000
#define P_TIMER_RUN                         0x40000000
#define P_TIMER_MODE                        0x20000000
#define P_TIMER_TRSHLD                          0xffff

/* P_PRDCR_SDBNDn */
#define P_PRDCR_SDBNDn_BAM_P_SB_UPDATED      0x1000000
#define P_PRDCR_SDBNDn_BAM_P_TOGGLE           0x100000
#define P_PRDCR_SDBNDn_BAM_P_CTRL              0xf0000
#define P_PRDCR_SDBNDn_BAM_P_BYTES_FREE         0xffff

/* P_CNSMR_SDBNDn */
#define P_CNSMR_SDBNDn_BAM_P_SB_UPDATED      0x1000000
#define P_CNSMR_SDBNDn_BAM_P_WAIT_4_ACK       0x800000
#define P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE       0x400000
#define P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE_R     0x200000
#define P_CNSMR_SDBNDn_BAM_P_TOGGLE           0x100000
#define P_CNSMR_SDBNDn_BAM_P_CTRL              0xf0000
#define P_CNSMR_SDBNDn_BAM_P_BYTES_AVAIL        0xffff

/* P_EVNT_regn */
#define P_BYTES_CONSUMED                    0xffff0000
#define P_DESC_FIFO_PEER_OFST                   0xffff

/* P_SW_ofstsn */
#define SW_OFST_IN_DESC                     0xffff0000
#define SW_DESC_OFST                            0xffff

/* P_EVNT_GEN_TRSHLDn */
#define P_EVNT_GEN_TRSHLD_P_TRSHLD              0xffff

/* P_FIFO_sizesn */
#define P_DATA_FIFO_SIZE                    0xffff0000
#define P_DESC_FIFO_SIZE                        0xffff

#define P_RETR_CNTXT_RETR_DESC_OFST            0xffff0000
#define P_RETR_CNTXT_RETR_OFST_IN_DESC             0xffff
#define P_SI_CNTXT_SI_DESC_OFST                    0xffff
#define P_DF_CNTXT_WB_ACCUMULATED              0xffff0000
#define P_DF_CNTXT_DF_DESC_OFST                    0xffff
#define P_AU_PSM_CNTXT_1_AU_PSM_ACCUMED        0xffff0000
#define P_AU_PSM_CNTXT_1_AU_ACKED                  0xffff
#define P_PSM_CNTXT_2_PSM_DESC_VALID           0x80000000
#define P_PSM_CNTXT_2_PSM_DESC_IRQ             0x40000000
#define P_PSM_CNTXT_2_PSM_DESC_IRQ_DONE        0x20000000
#define P_PSM_CNTXT_2_PSM_GENERAL_BITS         0x1e000000
#define P_PSM_CNTXT_2_PSM_CONS_STATE            0x1c00000
#define P_PSM_CNTXT_2_PSM_PROD_SYS_STATE         0x380000
#define P_PSM_CNTXT_2_PSM_PROD_B2B_STATE          0x70000
#define P_PSM_CNTXT_2_PSM_DESC_SIZE                0xffff
#define P_PSM_CNTXT_4_PSM_DESC_OFST            0xffff0000
#define P_PSM_CNTXT_4_PSM_SAVED_ACCUMED_SIZE       0xffff
#define P_PSM_CNTXT_5_PSM_BLOCK_BYTE_CNT       0xffff0000
#define P_PSM_CNTXT_5_PSM_OFST_IN_DESC             0xffff

#else

/* Maximum number of execution environment */
#define BAM_MAX_EES 4

/**
 *  BAM Hardware registers bitmask.
 *  format: <register>_<field>
 *
 */
/* CTRL */
#define IBC_DISABLE                            0x10000
#define BAM_CACHED_DESC_STORE                   0x8000
#define BAM_DESC_CACHE_SEL                      0x6000
/* BAM_PERIPH_IRQ_SIC_SEL is an obsolete field; This bit is reserved now */
#define BAM_PERIPH_IRQ_SIC_SEL                  0x1000
#define BAM_EN_ACCUM                              0x10
#define BAM_EN                                     0x2
#define BAM_SW_RST                                 0x1

/* REVISION */
#define BAM_INACTIV_TMR_BASE                0xff000000
#define BAM_INACTIV_TMRS_EXST                  0x80000
#define BAM_HIGH_FREQUENCY_BAM                 0x40000
#define BAM_HAS_NO_BYPASS                      0x20000
#define BAM_SECURED                            0x10000
#define BAM_NUM_EES                              0xf00
#define BAM_REVISION                              0xff

/* NUM_PIPES */
#define BAM_NON_PIPE_GRP                    0xff000000
#define BAM_PERIPH_NON_PIPE_GRP               0xff0000
#define BAM_DATA_ADDR_BUS_WIDTH                 0xC000
#define BAM_NUM_PIPES                             0xff

/* DESC_CNT_TRSHLD */
#define BAM_DESC_CNT_TRSHLD                     0xffff

/* IRQ_SRCS */
#define BAM_IRQ                         0x80000000
#define P_IRQ                           0x7fffffff

#define IRQ_STTS_BAM_EMPTY_IRQ                          0x8
#define IRQ_STTS_BAM_ERROR_IRQ                          0x4
#define IRQ_STTS_BAM_HRESP_ERR_IRQ                      0x2
#define IRQ_CLR_BAM_EMPTY_CLR                           0x8
#define IRQ_CLR_BAM_ERROR_CLR                           0x4
#define IRQ_CLR_BAM_HRESP_ERR_CLR                       0x2
#define IRQ_EN_BAM_EMPTY_EN                             0x8
#define IRQ_EN_BAM_ERROR_EN                             0x4
#define IRQ_EN_BAM_HRESP_ERR_EN                         0x2
#define IRQ_SIC_SEL_BAM_IRQ_SIC_SEL              0x80000000
#define IRQ_SIC_SEL_P_IRQ_SIC_SEL                0x7fffffff
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HVMID         0x7c0000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_DIRECT_MODE    0x20000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HCID           0x1f000
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HPROT            0xf00
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HBURST            0xe0
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HSIZE             0x18
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HWRITE             0x4
#define AHB_MASTER_ERR_CTRLS_BAM_ERR_HTRANS             0x3
#define CNFG_BITS_BAM_AU_ACCUMED                  0x4000000
#define CNFG_BITS_BAM_PSM_P_HD_DATA               0x2000000
#define CNFG_BITS_BAM_REG_P_EN                    0x1000000
#define CNFG_BITS_BAM_WB_DSC_AVL_P_RST             0x800000
#define CNFG_BITS_BAM_WB_RETR_SVPNT                0x400000
#define CNFG_BITS_BAM_WB_CSW_ACK_IDL               0x200000
#define CNFG_BITS_BAM_WB_BLK_CSW                   0x100000
#define CNFG_BITS_BAM_WB_P_RES                      0x80000
#define CNFG_BITS_BAM_SI_P_RES                      0x40000
#define CNFG_BITS_BAM_AU_P_RES                      0x20000
#define CNFG_BITS_BAM_PSM_P_RES                     0x10000
#define CNFG_BITS_BAM_PSM_CSW_REQ                    0x8000
#define CNFG_BITS_BAM_SB_CLK_REQ                     0x4000
#define CNFG_BITS_BAM_IBC_DISABLE                    0x2000
#define CNFG_BITS_BAM_NO_EXT_P_RST                   0x1000
#define CNFG_BITS_BAM_FULL_PIPE                       0x800
#define CNFG_BITS_BAM_PIPE_CNFG                         0x4

/* TEST_BUS_SEL */
#define BAM_DATA_ERASE                         0x40000
#define BAM_DATA_FLUSH                         0x20000
#define BAM_CLK_ALWAYS_ON                      0x10000
#define BAM_TESTBUS_SEL                           0x7f

/* TRUST_REG  */
#define BAM_VMID                                0x1f00
#define BAM_RST_BLOCK                             0x80
#define BAM_EE                                     0x3

/* P_TRUST_REGn */
#define BAM_P_VMID                              0x1f00
#define BAM_P_EE                                   0x3

/* P_PRDCR_SDBNDn */
#define P_PRDCR_SDBNDn_BAM_P_SB_UPDATED      0x1000000
#define P_PRDCR_SDBNDn_BAM_P_TOGGLE           0x100000
#define P_PRDCR_SDBNDn_BAM_P_CTRL              0xf0000
#define P_PRDCR_SDBNDn_BAM_P_BYTES_FREE         0xffff
/* P_CNSMR_SDBNDn */
#define P_CNSMR_SDBNDn_BAM_P_SB_UPDATED      0x1000000
#define P_CNSMR_SDBNDn_BAM_P_WAIT_4_ACK       0x800000
#define P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE       0x400000
#define P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE_R     0x200000
#define P_CNSMR_SDBNDn_BAM_P_TOGGLE           0x100000
#define P_CNSMR_SDBNDn_BAM_P_CTRL              0xf0000
#define P_CNSMR_SDBNDn_BAM_P_BYTES_AVAIL        0xffff

/* P_ctrln */
#define P_PREFETCH_LIMIT                         0x600
#define P_AUTO_EOB_SEL                           0x180
#define P_AUTO_EOB                                0x40
#define P_SYS_MODE                             0x20
#define P_SYS_STRM                             0x10
#define P_DIRECTION                             0x8
#define P_EN                                    0x2

#define P_RST_P_SW_RST                                 0x1

#define P_HALT_P_PROD_HALTED                           0x2
#define P_HALT_P_HALT                                  0x1

#define P_IRQ_STTS_P_TRNSFR_END_IRQ                   0x20
#define P_IRQ_STTS_P_ERR_IRQ                          0x10
#define P_IRQ_STTS_P_OUT_OF_DESC_IRQ                   0x8
#define P_IRQ_STTS_P_WAKE_IRQ                          0x4
#define P_IRQ_STTS_P_TIMER_IRQ                         0x2
#define P_IRQ_STTS_P_PRCSD_DESC_IRQ                    0x1

#define P_IRQ_CLR_P_TRNSFR_END_CLR                    0x20
#define P_IRQ_CLR_P_ERR_CLR                           0x10
#define P_IRQ_CLR_P_OUT_OF_DESC_CLR                    0x8
#define P_IRQ_CLR_P_WAKE_CLR                           0x4
#define P_IRQ_CLR_P_TIMER_CLR                          0x2
#define P_IRQ_CLR_P_PRCSD_DESC_CLR                     0x1

#define P_IRQ_EN_P_TRNSFR_END_EN                      0x20
#define P_IRQ_EN_P_ERR_EN                             0x10
#define P_IRQ_EN_P_OUT_OF_DESC_EN                      0x8
#define P_IRQ_EN_P_WAKE_EN                             0x4
#define P_IRQ_EN_P_TIMER_EN                            0x2
#define P_IRQ_EN_P_PRCSD_DESC_EN                       0x1

#define P_TIMER_P_TIMER                             0xffff

/* P_TIMER_ctrln */
#define P_TIMER_RST                0x80000000
#define P_TIMER_RUN                0x40000000
#define P_TIMER_MODE               0x20000000
#define P_TIMER_TRSHLD                 0xffff

/* P_EVNT_regn */
#define P_BYTES_CONSUMED             0xffff0000
#define P_DESC_FIFO_PEER_OFST            0xffff

/* P_SW_ofstsn */
#define SW_OFST_IN_DESC              0xffff0000
#define SW_DESC_OFST                     0xffff

#define P_EVNT_GEN_TRSHLD_P_TRSHLD                  0xffff

/* P_FIFO_sizesn */
#define P_DATA_FIFO_SIZE           0xffff0000
#define P_DESC_FIFO_SIZE               0xffff

#define P_RETR_CNTXT_RETR_DESC_OFST            0xffff0000
#define P_RETR_CNTXT_RETR_OFST_IN_DESC             0xffff
#define P_SI_CNTXT_SI_DESC_OFST                    0xffff
#define P_AU_PSM_CNTXT_1_AU_PSM_ACCUMED        0xffff0000
#define P_AU_PSM_CNTXT_1_AU_ACKED                  0xffff
#define P_PSM_CNTXT_2_PSM_DESC_VALID           0x80000000
#define P_PSM_CNTXT_2_PSM_DESC_IRQ             0x40000000
#define P_PSM_CNTXT_2_PSM_DESC_IRQ_DONE        0x20000000
#define P_PSM_CNTXT_2_PSM_GENERAL_BITS         0x1e000000
#define P_PSM_CNTXT_2_PSM_CONS_STATE            0x1c00000
#define P_PSM_CNTXT_2_PSM_PROD_SYS_STATE         0x380000
#define P_PSM_CNTXT_2_PSM_PROD_B2B_STATE          0x70000
#define P_PSM_CNTXT_2_PSM_DESC_SIZE                0xffff
#define P_PSM_CNTXT_4_PSM_DESC_OFST            0xffff0000
#define P_PSM_CNTXT_4_PSM_SAVED_ACCUMED_SIZE       0xffff
#define P_PSM_CNTXT_5_PSM_BLOCK_BYTE_CNT       0xffff0000
#define P_PSM_CNTXT_5_PSM_OFST_IN_DESC             0xffff
#endif

#define BAM_ERROR   (-1)
#define BAM_INVALID_OFFSET		0xFFFFFFFF

enum bam_regs {
	CTRL,
	REVISION,
	SW_REVISION,
	NUM_PIPES,
	TIMER,
	TIMER_CTRL,
	DESC_CNT_TRSHLD,
	IRQ_SRCS,
	IRQ_SRCS_MSK,
	IRQ_SRCS_UNMASKED,
	IRQ_STTS,
	IRQ_CLR,
	IRQ_EN,
	IRQ_SIC_SEL,
	AHB_MASTER_ERR_CTRLS,
	AHB_MASTER_ERR_ADDR,
	AHB_MASTER_ERR_ADDR_MSB,
	AHB_MASTER_ERR_DATA,
	IRQ_DEST,
	PERIPH_IRQ_DEST,
	TRUST_REG,
	TEST_BUS_SEL,
	TEST_BUS_REG,
	CNFG_BITS,
	IRQ_SRCS_EE,
	IRQ_SRCS_MSK_EE,
	IRQ_SRCS_UNMASKED_EE,
	PIPE_ATTR_EE,
	P_CTRL,
	P_RST,
	P_HALT,
	P_IRQ_STTS,
	P_IRQ_CLR,
	P_IRQ_EN,
	P_TIMER,
	P_TIMER_CTRL,
	P_PRDCR_SDBND,
	P_CNSMR_SDBND,
	P_EVNT_DEST_ADDR,
	P_EVNT_DEST_ADDR_MSB,
	P_EVNT_REG,
	P_SW_OFSTS,
	P_DATA_FIFO_ADDR,
	P_DATA_FIFO_ADDR_MSB,
	P_DESC_FIFO_ADDR,
	P_DESC_FIFO_ADDR_MSB,
	P_EVNT_GEN_TRSHLD,
	P_FIFO_SIZES,
	P_IRQ_DEST_ADDR,
	P_RETR_CNTXT,
	P_SI_CNTXT,
	P_DF_CNTXT,
	P_AU_PSM_CNTXT_1,
	P_PSM_CNTXT_2,
	P_PSM_CNTXT_3,
	P_PSM_CNTXT_3_MSB,
	P_PSM_CNTXT_4,
	P_PSM_CNTXT_5,
	P_TRUST_REG,
	BAM_MAX_REGS,
};

static u32 bam_regmap[][BAM_MAX_REGS] = {
	{ /* LEGACY BAM*/
			[CTRL] = 0xf80,
			[REVISION] = 0xf84,
			[NUM_PIPES] = 0xfbc,
			[DESC_CNT_TRSHLD] = 0xf88,
			[IRQ_SRCS] = 0xf8c,
			[IRQ_SRCS_MSK] = 0xf90,
			[IRQ_SRCS_UNMASKED] = 0xfb0,
			[IRQ_STTS] = 0xf94,
			[IRQ_CLR] = 0xf98,
			[IRQ_EN] = 0xf9c,
			[IRQ_SIC_SEL] = 0xfa0,
			[AHB_MASTER_ERR_CTRLS] = 0xfa4,
			[AHB_MASTER_ERR_ADDR] = 0xfa8,
			[AHB_MASTER_ERR_DATA] = 0xfac,
			[IRQ_DEST] = 0xfb4,
			[PERIPH_IRQ_DEST] = 0xfb8,
			[TRUST_REG] = 0xff0,
			[TEST_BUS_SEL] = 0xff4,
			[TEST_BUS_REG] = 0xff8,
			[CNFG_BITS] = 0xffc,
			[IRQ_SRCS_EE] = 0x1800,
			[IRQ_SRCS_MSK_EE] = 0x1804,
			[IRQ_SRCS_UNMASKED_EE] = 0x1808,
			[P_CTRL] = 0x0,
			[P_RST] = 0x4,
			[P_HALT] = 0x8,
			[P_IRQ_STTS] = 0x10,
			[P_IRQ_CLR] = 0x14,
			[P_IRQ_EN] = 0x18,
			[P_TIMER] = 0x1c,
			[P_TIMER_CTRL] = 0x20,
			[P_PRDCR_SDBND] = 0x24,
			[P_CNSMR_SDBND] = 0x28,
			[P_EVNT_DEST_ADDR] = 0x102c,
			[P_EVNT_REG] = 0x1018,
			[P_SW_OFSTS] = 0x1000,
			[P_DATA_FIFO_ADDR] = 0x1024,
			[P_DESC_FIFO_ADDR] = 0x101c,
			[P_EVNT_GEN_TRSHLD] = 0x1028,
			[P_FIFO_SIZES] = 0x1020,
			[P_IRQ_DEST_ADDR] = 0x103c,
			[P_RETR_CNTXT] = 0x1034,
			[P_SI_CNTXT] = 0x1038,
			[P_AU_PSM_CNTXT_1] = 0x1004,
			[P_PSM_CNTXT_2] = 0x1008,
			[P_PSM_CNTXT_3] = 0x100c,
			[P_PSM_CNTXT_4] = 0x1010,
			[P_PSM_CNTXT_5] = 0x1014,
			[P_TRUST_REG] = 0x30,
	},
	{ /* NDP BAM */
			[CTRL] = 0x0,
			[REVISION] = 0x4,
			[SW_REVISION] = 0x80,
			[NUM_PIPES] = 0x3c,
			[TIMER] = 0x40,
			[TIMER_CTRL] = 0x44,
			[DESC_CNT_TRSHLD] = 0x8,
			[IRQ_SRCS] = 0xc,
			[IRQ_SRCS_MSK] = 0x10,
			[IRQ_SRCS_UNMASKED] = 0x30,
			[IRQ_STTS] = 0x14,
			[IRQ_CLR] = 0x18,
			[IRQ_EN] = 0x1c,
			[AHB_MASTER_ERR_CTRLS] = 0x24,
			[AHB_MASTER_ERR_ADDR] = 0x28,
			[AHB_MASTER_ERR_ADDR_MSB] = 0x104,
			[AHB_MASTER_ERR_DATA] = 0x2c,
			[TRUST_REG] = 0x70,
			[TEST_BUS_SEL] = 0x74,
			[TEST_BUS_REG] = 0x78,
			[CNFG_BITS] = 0x7c,
			[IRQ_SRCS_EE] = 0x800,
			[IRQ_SRCS_MSK_EE] = 0x804,
			[IRQ_SRCS_UNMASKED_EE] = 0x808,
			[PIPE_ATTR_EE] = 0x80c,
			[P_CTRL] = 0x1000,
			[P_RST] = 0x1004,
			[P_HALT] = 0x1008,
			[P_IRQ_STTS] = 0x1010,
			[P_IRQ_CLR] = 0x1014,
			[P_IRQ_EN] = 0x1018,
			[P_TIMER] = 0x101c,
			[P_TIMER_CTRL] = 0x1020,
			[P_PRDCR_SDBND] = 0x1024,
			[P_CNSMR_SDBND] = 0x1028,
			[P_EVNT_DEST_ADDR] = 0x182c,
			[P_EVNT_DEST_ADDR_MSB] = 0x1934,
			[P_EVNT_REG] = 0x1818,
			[P_SW_OFSTS] = 0x1800,
			[P_DATA_FIFO_ADDR] = 0x1824,
			[P_DATA_FIFO_ADDR_MSB] = 0x1924,
			[P_DESC_FIFO_ADDR] = 0x181c,
			[P_DESC_FIFO_ADDR_MSB] = 0x1914,
			[P_EVNT_GEN_TRSHLD] = 0x1828,
			[P_FIFO_SIZES] = 0x1820,
			[P_RETR_CNTXT] = 0x1834,
			[P_SI_CNTXT] = 0x1838,
			[P_DF_CNTXT] = 0x1830,
			[P_AU_PSM_CNTXT_1] = 0x1804,
			[P_PSM_CNTXT_2] = 0x1808,
			[P_PSM_CNTXT_3] = 0x180c,
			[P_PSM_CNTXT_3_MSB] = 0x1904,
			[P_PSM_CNTXT_4] = 0x1810,
			[P_PSM_CNTXT_5] = 0x1814,
			[P_TRUST_REG] = 0x1030,
	},
	{ /* 4K OFFSETs*/
			[CTRL] = 0x0,
			[REVISION] = 0x1000,
			[SW_REVISION] = 0x1004,
			[NUM_PIPES] = 0x1008,
			[TIMER] = 0x40,
			[TIMER_CTRL] = 0x44,
			[DESC_CNT_TRSHLD] = 0x8,
			[IRQ_SRCS] = 0x3010,
			[IRQ_SRCS_MSK] = 0x3014,
			[IRQ_SRCS_UNMASKED] = 0x3018,
			[IRQ_STTS] = 0x14,
			[IRQ_CLR] = 0x18,
			[IRQ_EN] = 0x1c,
			[AHB_MASTER_ERR_CTRLS] = 0x1024,
			[AHB_MASTER_ERR_ADDR] = 0x1028,
			[AHB_MASTER_ERR_ADDR_MSB] = 0x1104,
			[AHB_MASTER_ERR_DATA] = 0x102c,
			[TRUST_REG] = 0x2000,
			[TEST_BUS_SEL] = 0x1010,
			[TEST_BUS_REG] = 0x1014,
			[CNFG_BITS] = 0x7c,
			[IRQ_SRCS_EE] = 0x3000,
			[IRQ_SRCS_MSK_EE] = 0x3004,
			[IRQ_SRCS_UNMASKED_EE] = 0x3008,
			[PIPE_ATTR_EE] = 0x300c,
			[P_CTRL] = 0x13000,
			[P_RST] = 0x13004,
			[P_HALT] = 0x13008,
			[P_IRQ_STTS] = 0x13010,
			[P_IRQ_CLR] = 0x13014,
			[P_IRQ_EN] = 0x13018,
			[P_TIMER] = 0x1301c,
			[P_TIMER_CTRL] = 0x13020,
			[P_PRDCR_SDBND] = 0x13024,
			[P_CNSMR_SDBND] = 0x13028,
			[P_EVNT_DEST_ADDR] = 0x1382c,
			[P_EVNT_DEST_ADDR_MSB] = 0x13934,
			[P_EVNT_REG] = 0x13818,
			[P_SW_OFSTS] = 0x13800,
			[P_DATA_FIFO_ADDR] = 0x13824,
			[P_DATA_FIFO_ADDR_MSB] = 0x13924,
			[P_DESC_FIFO_ADDR] = 0x1381c,
			[P_DESC_FIFO_ADDR_MSB] = 0x13914,
			[P_EVNT_GEN_TRSHLD] = 0x13828,
			[P_FIFO_SIZES] = 0x13820,
			[P_RETR_CNTXT] = 0x13834,
			[P_SI_CNTXT] = 0x13838,
			[P_DF_CNTXT] = 0x13830,
			[P_AU_PSM_CNTXT_1] = 0x13804,
			[P_PSM_CNTXT_2] = 0x13808,
			[P_PSM_CNTXT_3] = 0x1380c,
			[P_PSM_CNTXT_3_MSB] = 0x13904,
			[P_PSM_CNTXT_4] = 0x13810,
			[P_PSM_CNTXT_5] = 0x13814,
			[P_TRUST_REG] = 0x2020,
	},
};

/* AHB buffer error control */
enum bam_nonsecure_reset {
	BAM_NONSECURE_RESET_ENABLE  = 0,
	BAM_NONSECURE_RESET_DISABLE = 1,
};

static inline u32 bam_get_register_offset(void *base, enum bam_regs reg,
								u32 param)
{
	int index = BAM_ERROR;
	u32 offset = 0;
	u32 *ptr_reg = bam_regmap[bam_type];
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}

	if (reg >= CTRL && reg < IRQ_SRCS_EE)
		index = 0;
	if (reg >= IRQ_SRCS_EE && reg < P_CTRL)
		index = (bam_type == SPS_BAM_NDP_4K) ? 0x1000 : 0x80;
	if (reg >= P_CTRL && reg < P_TRUST_REG) {
		if (bam_type == SPS_BAM_LEGACY) {
			if (reg >= P_EVNT_DEST_ADDR)
				index = 0x40;
			else
				index = 0x80;
		} else
			index = 0x1000;
	} else if (reg == P_TRUST_REG) {
		if (bam_type == SPS_BAM_LEGACY)
			index = 0x80;
		else
			index = (bam_type == SPS_BAM_NDP_4K) ? 0x4 : 0x1000;
	}
	if (index < 0) {
		SPS_ERR(dev, "Failed to find register offset for %d\n", reg);
		return BAM_INVALID_OFFSET;
	}

	offset = *(ptr_reg + reg) + (index * param);
	return offset;
}


/**
 *
 * Read register with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 *
 * @return u32
 */
static inline u32 bam_read_reg(void *base, enum bam_regs reg, u32 param)
{
	u32 val, offset = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}
	offset = bam_get_register_offset(base, reg, param);
	if (offset == BAM_INVALID_OFFSET) {
		SPS_ERR(dev, "Failed to get the register offset for %d\n", reg);
		return offset;
	}
	val = ioread32(dev->base + offset);
	SPS_DBG(dev, "sps:bam 0x%pK(va) offset 0x%x reg 0x%x r_val 0x%x\n",
			dev->base, offset, reg, val);
	return val;
}

/**
 * Read register masked field with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 * @mask - register bitmask.
 *
 * @return u32
 */
static inline u32 bam_read_reg_field(void *base, enum bam_regs reg, u32 param,
								const u32 mask)
{
	u32 val, shift, offset = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);
	unsigned long lmask = mask;

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}
	shift = find_first_bit(&lmask, 32);
	offset = bam_get_register_offset(base, reg, param);
	if (offset == BAM_INVALID_OFFSET) {
		SPS_ERR(dev, "Failed to get the register offset for %d\n", reg);
		return offset;
	}
	val = ioread32(dev->base + offset);
	val &= mask;		/* clear other bits */
	val >>= shift;
	SPS_DBG(dev, "sps:bam 0x%pK(va) read reg 0x%x mask 0x%x r_val 0x%x\n",
			dev->base, offset, mask, val);
	return val;
}

/**
 *
 * Write register with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 * @val - value to write.
 *
 */
static inline void bam_write_reg(void *base, enum bam_regs reg,
						u32 param, u32 val)
{
	u32 offset = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	offset = bam_get_register_offset(base, reg, param);
	if (offset == BAM_INVALID_OFFSET) {
		SPS_ERR(dev, "Failed to get the register offset for %d\n", reg);
		return;
	}
	iowrite32(val, dev->base + offset);
	SPS_DBG(dev, "sps:bam 0x%pK(va) write reg 0x%x w_val 0x%x\n",
			dev->base, offset, val);
}

/**
 * Write register masked field with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 * @mask - register bitmask.
 * @val - value to write.
 *
 */
static inline void bam_write_reg_field(void *base, enum bam_regs reg,
					u32 param, const u32 mask, u32 val)
{
	u32 tmp, shift, offset = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);
	unsigned long lmask = mask;

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	shift = find_first_bit(&lmask, 32);
	offset = bam_get_register_offset(base, reg, param);
	if (offset == BAM_INVALID_OFFSET) {
		SPS_ERR(dev, "Failed to get the register offset for %d\n", reg);
		return;
	}
	tmp = ioread32(dev->base + offset);

	tmp &= ~mask;		/* clear written bits */
	val = tmp | (val << shift);
	iowrite32(val, dev->base + offset);
	SPS_DBG(dev, "sps:bam 0x%pK(va) write reg 0x%x w_val 0x%x\n",
			dev->base, offset, val);
}

/**
 * Initialize a BAM device
 *
 */
int bam_init(void *base, u32 ee,
		u16 summing_threshold,
		u32 irq_mask, u32 *version,
		u32 *num_pipes, u32 options)
{
	u32 cfg_bits;
	u32 ver = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}

	SPS_DBG3(dev, "sps: bam=%pa 0x%pK(va).ee=%d\n",
			BAM_ID(dev), dev->base, ee);

	ver = bam_read_reg_field(base, REVISION, 0, BAM_REVISION);

	if ((ver < BAM_MIN_VERSION) || (ver > BAM_MAX_VERSION)) {
		SPS_ERR(dev, "sps:bam 0x%pK(va) Invalid BAM REVISION 0x%x\n",
				dev->base, ver);
		return -ENODEV;
	}

	SPS_DBG(dev, "sps:REVISION of BAM 0x%pK is 0x%x\n",
				dev->base, ver);

	if (summing_threshold == 0) {
		summing_threshold = 4;
		SPS_ERR(dev,
			"sps:bam 0x%pK(va) summing_threshold is zero,use default 4\n",
			dev->base);
	}

	if (options & SPS_BAM_NO_EXT_P_RST)
		cfg_bits = 0xffffffff & ~(3 << 11);
	else
		cfg_bits = 0xffffffff & ~(1 << 11);

	bam_write_reg_field(base, CTRL, 0, BAM_SW_RST, 1);
	/* No delay needed */
	bam_write_reg_field(base, CTRL, 0, BAM_SW_RST, 0);

	bam_write_reg_field(base, CTRL, 0, BAM_EN, 1);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	bam_write_reg_field(base, CTRL, 0, CACHE_MISS_ERR_RESP_EN, 0);

	if (options & SPS_BAM_NO_LOCAL_CLK_GATING)
		bam_write_reg_field(base, CTRL, 0, LOCAL_CLK_GATING, 0);
	else
		bam_write_reg_field(base, CTRL, 0, LOCAL_CLK_GATING, 1);

	if (enhd_pipe) {
		if (options & SPS_BAM_CANCEL_WB)
			bam_write_reg_field(base, CTRL, 0,
					BAM_MESS_ONLY_CANCEL_WB, 1);
		else
			bam_write_reg_field(base, CTRL, 0,
					BAM_MESS_ONLY_CANCEL_WB, 0);
	}
#endif
	bam_write_reg(base, DESC_CNT_TRSHLD, 0, summing_threshold);

	bam_write_reg(base, CNFG_BITS, 0, cfg_bits);

	/*
	 *  Enable Global BAM Interrupt - for error reasons ,
	 *  filter with mask.
	 *  Note: Pipes interrupts are disabled until BAM_P_IRQ_enn is set
	 */
	bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, BAM_IRQ, 1);

	bam_write_reg(base, IRQ_EN, 0, irq_mask);

	*num_pipes = bam_read_reg_field(base, NUM_PIPES, 0, BAM_NUM_PIPES);

	*version = ver;

	return 0;
}

/**
 * Set BAM global interrupt
 */
void bam_set_global_irq(void *base, u32 ee, u32 irq_mask, bool en)
{
	if (en)
		bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, BAM_IRQ, 1);
	else
		bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, BAM_IRQ, 0);
}

/**
 * Set BAM global execution environment
 *
 * @base - BAM virtual base address
 *
 * @ee - BAM execution environment index
 *
 * @vmid - virtual master identifier
 *
 * @reset - enable/disable BAM global software reset
 */
static void bam_set_ee(void *base, u32 ee, u32 vmid,
			enum bam_nonsecure_reset reset)
{
	bam_write_reg_field(base, TRUST_REG, 0, BAM_EE, ee);
	bam_write_reg_field(base, TRUST_REG, 0, BAM_VMID, vmid);
	bam_write_reg_field(base, TRUST_REG, 0, BAM_RST_BLOCK, reset);
}

/**
 * Set the pipe execution environment
 *
 * @base - BAM virtual base address
 *
 * @pipe - pipe index
 *
 * @ee - BAM execution environment index
 *
 * @vmid - virtual master identifier
 */
static void bam_pipe_set_ee(void *base, u32 pipe, u32 ee, u32 vmid)
{
	bam_write_reg_field(base, P_TRUST_REG, pipe, BAM_P_EE, ee);
	bam_write_reg_field(base, P_TRUST_REG, pipe, BAM_P_VMID, vmid);
}

/**
 * Initialize BAM device security execution environment
 */
int bam_security_init(void *base, u32 ee, u32 vmid, u32 pipe_mask)
{
	u32 version;
	u32 num_pipes;
	u32 mask;
	u32 pipe;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}

	SPS_DBG3(dev, "sps: bam=%pa 0x%pK(va)\n", BAM_ID(dev), dev->base);

	/*
	 * Discover the hardware version number and the number of pipes
	 * supported by this BAM
	 */
	version = bam_read_reg_field(base, REVISION, 0, BAM_REVISION);
	num_pipes = bam_read_reg_field(base, NUM_PIPES, 0, BAM_NUM_PIPES);
	if (version < 3 || version > 0x1F) {
		SPS_ERR(dev,
			"sps:bam 0x%pK(va) security is not supported for this BAM version 0x%x\n",
			dev->base, version);
		return -ENODEV;
	}

	if (num_pipes > BAM_MAX_PIPES) {
		SPS_ERR(dev,
		"sps:bam 0x%pK(va) the number of pipes is more than the maximum number allowed\n",
			dev->base);
		return -ENODEV;
	}

	for (pipe = 0, mask = 1; pipe < num_pipes; pipe++, mask <<= 1)
		if ((mask & pipe_mask) != 0)
			bam_pipe_set_ee(base, pipe, ee, vmid);

	/* If MSbit is set, assign top-level interrupt to this EE */
	mask = 1UL << 31;
	if ((mask & pipe_mask) != 0)
		bam_set_ee(base, ee, vmid, BAM_NONSECURE_RESET_ENABLE);

	return 0;
}

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
static inline u32 bam_get_pipe_attr(void *base, u32 ee, bool global)
{
	u32 val;

	if (global)
		val = bam_read_reg_field(base, PIPE_ATTR_EE, ee, BAM_ENABLED);
	else
		val = bam_read_reg_field(base, PIPE_ATTR_EE, ee, P_ATTR);

	return val;
}
#else
static inline u32 bam_get_pipe_attr(void *base, u32 ee, bool global)
{
	return 0;
}
#endif

/**
 * Verify that a BAM device is enabled and gathers the hardware
 * configuration.
 *
 */
int bam_check(void *base, u32 *version, u32 ee, u32 *num_pipes)
{
	u32 ver = 0;
	u32 enabled = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}

	SPS_DBG3(dev, "sps: bam=%pa 0x%pK(va)\n", BAM_ID(dev), dev->base);

	if (!enhd_pipe)
		enabled = bam_read_reg_field(base, CTRL, 0, BAM_EN);
	else
		enabled = bam_get_pipe_attr(base, ee, true);

	if (!enabled) {
		SPS_ERR(dev, "sps: bam 0x%pK(va) is not enabled\n", dev->base);
		return -ENODEV;
	}

	ver = bam_read_reg(base, REVISION, 0) & BAM_REVISION;

	/*
	 *  Discover the hardware version number and the number of pipes
	 *  supported by this BAM
	 */
	*num_pipes = bam_read_reg_field(base, NUM_PIPES, 0, BAM_NUM_PIPES);
	*version = ver;

	/* Check BAM version */
	if ((ver < BAM_MIN_VERSION) || (ver > BAM_MAX_VERSION)) {
		SPS_ERR(dev, "sps: bam 0x%pK(va) Invalid BAM version 0x%x\n",
				dev->base, ver);
		return -ENODEV;
	}

	return 0;
}

/**
 * Disable a BAM device
 *
 */
void bam_exit(void *base, u32 ee)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG3(dev, "sps: bam=%pa 0x%pK(va).ee=%d\n", BAM_ID(dev),
			dev->base, ee);

	bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, BAM_IRQ, 0);

	bam_write_reg(base, IRQ_EN, 0, 0);

	/* Disable the BAM */
	bam_write_reg_field(base, CTRL, 0, BAM_EN, 0);
}

/**
 * Output BAM register content
 * including the TEST_BUS register content under
 * different TEST_BUS_SEL values.
 */
void bam_output_register_content(void *base, u32 ee)
{
	u32 num_pipes;
	u32 i;
	u32 pipe_attr = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}

	print_bam_test_bus_reg(base, 0);

	print_bam_selected_reg(base, BAM_MAX_EES);

	num_pipes = bam_read_reg_field(base, NUM_PIPES, 0,
					BAM_NUM_PIPES);
	SPS_INFO(dev, "sps:bam %pa 0x%pK(va) has %d pipes\n",
			BAM_ID(dev), dev->base, num_pipes);

	pipe_attr = enhd_pipe ?
		bam_get_pipe_attr(base, ee, false) : 0x0;

	if (!enhd_pipe || !pipe_attr)
		for (i = 0; i < num_pipes; i++)
			print_bam_pipe_selected_reg(base, i);
	else {
		for (i = 0; i < num_pipes; i++) {
			if (pipe_attr & (1UL << i))
				print_bam_pipe_selected_reg(base, i);
		}
	}
}

/**
 * Get BAM IRQ source and clear global IRQ status
 */
u32 bam_check_irq_source(void *base, u32 ee, u32 mask,
				enum sps_callback_case *cb_case)
{
	u32 source = 0, clr = 0;
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}
	source = bam_read_reg(base, IRQ_SRCS_EE, ee);
	clr = source & (1UL << 31);

	if (clr) {
		u32 status = 0;

		status = bam_read_reg(base, IRQ_STTS, 0);

		if (status & IRQ_STTS_BAM_ERROR_IRQ) {
			SPS_ERR(dev,
				"sps:bam %pa 0x%pK(va);bam irq status=0x%x\nsps: BAM_ERROR_IRQ\n",
				BAM_ID(dev), dev->base, status);
			bam_output_register_content(base, ee);
			*cb_case = SPS_CALLBACK_BAM_ERROR_IRQ;
		} else if (status & IRQ_STTS_BAM_HRESP_ERR_IRQ) {
			SPS_ERR(dev,
				"sps:bam %pa 0x%pK(va);bam irq status=0x%x\nsps: BAM_HRESP_ERR_IRQ\n",
				BAM_ID(dev), dev->base, status);
			bam_output_register_content(base, ee);
			*cb_case = SPS_CALLBACK_BAM_HRESP_ERR_IRQ;
#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
		} else if (status & IRQ_STTS_BAM_TIMER_IRQ) {
			SPS_DBG1(dev,
				"sps:bam 0x%pK(va);receive BAM_TIMER_IRQ\n",
					dev->base);
			*cb_case = SPS_CALLBACK_BAM_TIMER_IRQ;
#endif
		} else
			SPS_INFO(dev,
				"sps:bam %pa 0x%pK(va);bam irq status=0x%x\n",
				BAM_ID(dev), dev->base, status);

		bam_write_reg(base, IRQ_CLR, 0, status);
	}

	source &= (mask|(1UL << 31));
	return source;
}

/*
 * Reset a BAM pipe
 */
void bam_pipe_reset(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev, "sps: bam=%pa 0x%pK(va).pipe=%d\n", BAM_ID(dev),
			dev->base, pipe);

	bam_write_reg(base, P_RST, pipe, 1);
	wmb(); /* ensure pipe is reset */
	bam_write_reg(base, P_RST, pipe, 0);
	wmb(); /* ensure pipe reset is de-asserted*/
}

/*
 * Disable a BAM pipe
 */
void bam_disable_pipe(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev, "sps: bam=0x%pK(va).pipe=%d\n", base, pipe);
	bam_write_reg_field(base, P_CTRL, pipe, P_EN, 0);
	wmb(); /* ensure pipe is disabled */
}

/*
 * Check if the last desc is ZLT
 */
bool bam_pipe_check_zlt(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return false;
	}

	if (bam_read_reg_field(base, P_HALT, pipe, P_HALT_P_LAST_DESC_ZLT)) {
		SPS_DBG(dev,
			"sps: bam=0x%pK(va).pipe=%d: the last desc is ZLT\n",
			base, pipe);
		return true;
	}

	SPS_DBG(dev,
		"sps: bam=0x%pK(va).pipe=%d: the last desc is not ZLT\n",
		base, pipe);
	return false;
}

/*
 * Check if desc FIFO is empty
 */
bool bam_pipe_check_pipe_empty(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return false;
	}

	if (bam_read_reg_field(base, P_HALT, pipe, P_HALT_P_PIPE_EMPTY)) {
		SPS_DBG(dev,
			"sps: bam=0x%pK(va).pipe=%d: desc FIFO is empty\n",
			 base, pipe);
		return true;
	}

	SPS_DBG(dev,
		"sps: bam=0x%pK(va).pipe=%d: desc FIFO is not empty\n",
		base, pipe);
	return false;
}

/**
 * Initialize a BAM pipe
 */
int bam_pipe_init(void *base, u32 pipe,	struct bam_pipe_parameters *param,
					u32 ee)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return SPS_ERROR;
	}
	SPS_DBG2(dev, "sps: bam=%pa 0x%pK(va).pipe=%d\n",
			BAM_ID(dev), dev->base, pipe);

	/* Reset the BAM pipe */
	bam_write_reg(base, P_RST, pipe, 1);
	/* No delay needed */
	bam_write_reg(base, P_RST, pipe, 0);

	/* Enable the Pipe Interrupt at the BAM level */
	bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, (1 << pipe), 1);

	bam_write_reg(base, P_IRQ_EN, pipe, param->pipe_irq_mask);

	bam_write_reg_field(base, P_CTRL, pipe, P_DIRECTION, param->dir);
	bam_write_reg_field(base, P_CTRL, pipe, P_SYS_MODE, param->mode);

	bam_write_reg(base, P_EVNT_GEN_TRSHLD, pipe, param->event_threshold);

	bam_write_reg(base, P_DESC_FIFO_ADDR, pipe,
			SPS_GET_LOWER_ADDR(param->desc_base));
	bam_write_reg_field(base, P_FIFO_SIZES, pipe, P_DESC_FIFO_SIZE,
			    param->desc_size);

	bam_write_reg_field(base, P_CTRL, pipe, P_SYS_STRM,
			    param->stream_mode);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (SPS_LPAE && SPS_GET_UPPER_ADDR(param->desc_base))
		bam_write_reg(base, P_DESC_FIFO_ADDR_MSB, pipe,
				SPS_GET_UPPER_ADDR(param->desc_base));

	bam_write_reg_field(base, P_CTRL, pipe, P_LOCK_GROUP,
				param->lock_group);

	SPS_DBG(dev, "sps:bam=0x%pK(va).pipe=%d.lock_group=%d\n",
			dev->base, pipe, param->lock_group);
#endif

	if (param->mode == BAM_PIPE_MODE_BAM2BAM) {
		u32 peer_dest_addr = param->peer_phys_addr +
				      bam_get_register_offset(base, P_EVNT_REG,
						      param->peer_pipe);

		bam_write_reg(base, P_DATA_FIFO_ADDR, pipe,
			      SPS_GET_LOWER_ADDR(param->data_base));
		bam_write_reg_field(base, P_FIFO_SIZES, pipe,
				    P_DATA_FIFO_SIZE, param->data_size);

		if (!(param->dummy_peer)) {
			bam_write_reg(base, P_EVNT_DEST_ADDR, pipe,
						peer_dest_addr);
		} else {
			bam_write_reg(base, P_EVNT_DEST_ADDR, pipe,
						param->peer_phys_addr);
		}
		SPS_DBG2(dev,
			"sps:bam=0x%pK(va).pipe=%d.peer_bam=0x%x.peer_pipe=%d\n",
			dev->base, pipe,
			(u32) param->peer_phys_addr,
			param->peer_pipe);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
		if (SPS_LPAE && SPS_GET_UPPER_ADDR(param->data_base)) {
			bam_write_reg(base, P_EVNT_DEST_ADDR_MSB, pipe, 0x0);
			bam_write_reg(base, P_DATA_FIFO_ADDR_MSB, pipe,
				      SPS_GET_UPPER_ADDR(param->data_base));
		}

		bam_write_reg_field(base, P_CTRL, pipe, P_WRITE_NWD,
					param->write_nwd);

		SPS_DBG(dev, "sps:%s WRITE_NWD bit for this bam2bam pipe\n",
			param->write_nwd ? "Set" : "Do not set");
#endif
	}

	/* Pipe Enable - at last */
	bam_write_reg_field(base, P_CTRL, pipe, P_EN, 1);

	return 0;
}

/**
 * Reset the BAM pipe
 *
 */
void bam_pipe_exit(void *base, u32 pipe, u32 ee)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev, "sps: bam=%pa 0x%pK(va).pipe=%d\n",
			BAM_ID(dev), dev->base, pipe);

	bam_write_reg(base, P_IRQ_EN, pipe, 0);

	/* Disable the Pipe Interrupt at the BAM level */
	bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, (1 << pipe), 0);

	/* Pipe Disable */
	bam_write_reg_field(base, P_CTRL, pipe, P_EN, 0);
}

/**
 * Enable a BAM pipe
 *
 */
void bam_pipe_enable(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev, "sps: bam=%pa 0x%pK(va).pipe=%d\n",
			BAM_ID(dev), dev->base, pipe);

	if (bam_read_reg_field(base, P_CTRL, pipe, P_EN))
		SPS_DBG2(dev, "sps:bam=0x%pK(va).pipe=%d is already enabled\n",
				dev->base, pipe);
	else
		bam_write_reg_field(base, P_CTRL, pipe, P_EN, 1);
}

/**
 * Diasble a BAM pipe
 *
 */
void bam_pipe_disable(void *base, u32 pipe)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev, "sps: bam=%pa 0x%pK(va).pipe=%d\n",
			BAM_ID(dev), dev->base, pipe);

	bam_write_reg_field(base, P_CTRL, pipe, P_EN, 0);
}

/**
 * Check if a BAM pipe is enabled.
 *
 */
int bam_pipe_is_enabled(void *base, u32 pipe)
{
	return bam_read_reg_field(base, P_CTRL, pipe, P_EN);
}

/**
 * Configure interrupt for a BAM pipe
 *
 */
void bam_pipe_set_irq(void *base, u32 pipe, enum bam_enable irq_en,
		      u32 src_mask, u32 ee)
{
	struct sps_bam *dev = to_sps_bam_dev(base);

	if ((dev == NULL) || (&dev->base != base)) {
		SPS_ERR(sps, "Failed to get dev for base addr 0x%pK\n", base);
		return;
	}
	SPS_DBG2(dev,
		"sps: bam=%pa 0x%pK(va).pipe=%d; irq_en:%d; src_mask:0x%x; ee:%d\n",
			BAM_ID(dev), dev->base, pipe,
			irq_en, src_mask, ee);
	if (src_mask & BAM_PIPE_IRQ_RST_ERROR) {
		if (enhd_pipe)
			bam_write_reg_field(base, IRQ_EN, 0,
					IRQ_EN_BAM_ERROR_EN, 0);
		else {
			src_mask &= ~BAM_PIPE_IRQ_RST_ERROR;
			SPS_DBG2(dev,
				"SPS_O_RST_ERROR is not supported, pipe %d\n",
				pipe);
		}
	}
	if (src_mask & BAM_PIPE_IRQ_HRESP_ERROR) {
		if (enhd_pipe)
			bam_write_reg_field(base, IRQ_EN, 0,
					IRQ_EN_BAM_HRESP_ERR_EN, 0);
		else {
			src_mask &= ~BAM_PIPE_IRQ_HRESP_ERROR;
			SPS_DBG2(dev,
				"SPS_O_HRESP_ERROR is not supported, pipe %d\n",
				pipe);
		}
	}

	bam_write_reg(base, P_IRQ_EN, pipe, src_mask);
	bam_write_reg_field(base, IRQ_SRCS_MSK_EE, ee, (1 << pipe), irq_en);
}

/**
 * Configure a BAM pipe for satellite MTI use
 *
 */
void bam_pipe_satellite_mti(void *base, u32 pipe, u32 irq_gen_addr, u32 ee)
{
	bam_write_reg(base, P_IRQ_EN, pipe, 0);
#ifndef CONFIG_SPS_SUPPORT_NDP_BAM
	bam_write_reg(base, P_IRQ_DEST_ADDR, pipe, irq_gen_addr);
	bam_write_reg_field(base, IRQ_SIC_SEL, 0, (1 << pipe), 1);
#endif
	bam_write_reg_field(base, IRQ_SRCS_MSK, 0, (1 << pipe), 1);
}

/**
 * Configure MTI for a BAM pipe
 *
 */
void bam_pipe_set_mti(void *base, u32 pipe, enum bam_enable irq_en,
		      u32 src_mask, u32 irq_gen_addr)
{
	/*
	 * MTI use is only supported on BAMs when global config is controlled
	 * by a remote processor.
	 * Consequently, the global configuration register to enable SIC (MTI)
	 * support cannot be accessed.
	 * The remote processor must be relied upon to enable the SIC and the
	 * interrupt. Since the remote processor enable both SIC and interrupt,
	 * the interrupt enable mask must be set to zero for polling mode.
	 */
#ifndef CONFIG_SPS_SUPPORT_NDP_BAM
	bam_write_reg(base, P_IRQ_DEST_ADDR, pipe, irq_gen_addr);
#endif
	if (!irq_en)
		src_mask = 0;

	bam_write_reg(base, P_IRQ_EN, pipe, src_mask);
}

/**
 * Get and Clear BAM pipe IRQ status
 *
 */
u32 bam_pipe_get_and_clear_irq_status(void *base, u32 pipe)
{
	u32 status = 0;

	status = bam_read_reg(base, P_IRQ_STTS, pipe);
	bam_write_reg(base, P_IRQ_CLR, pipe, status);

	return status;
}

/**
 * Set write offset for a BAM pipe
 *
 */
void bam_pipe_set_desc_write_offset(void *base, u32 pipe, u32 next_write)
{
	/*
	 * It is not necessary to perform a read-modify-write masking to write
	 * the P_DESC_FIFO_PEER_OFST value, since the other field in the
	 * register (P_BYTES_CONSUMED) is read-only.
	 */
	bam_write_reg_field(base, P_EVNT_REG, pipe, P_DESC_FIFO_PEER_OFST,
			    next_write);
}

/**
 * Get write offset for a BAM pipe
 *
 */
u32 bam_pipe_get_desc_write_offset(void *base, u32 pipe)
{
	return bam_read_reg_field(base, P_EVNT_REG, pipe,
				  P_DESC_FIFO_PEER_OFST);
}

/**
 * Get read offset for a BAM pipe
 *
 */
u32 bam_pipe_get_desc_read_offset(void *base, u32 pipe)
{
	return bam_read_reg_field(base, P_SW_OFSTS, pipe, SW_DESC_OFST);
}

/* halt and un-halt a pipe */
void bam_pipe_halt(void *base, u32 pipe, bool halt)
{
	if (halt)
		bam_write_reg_field(base, P_HALT, pipe, P_HALT_P_HALT, 1);
	else
		bam_write_reg_field(base, P_HALT, pipe, P_HALT_P_HALT, 0);
}

/* output the content of BAM-level registers */
void print_bam_reg(void *virt_addr)
{
	int i, n, index = 0;
	u32 *bam = (u32 *) virt_addr;
	u32 ctrl;
	u32 ver;
	u32 pipes;
	u32 offset = 0;

	if (bam == NULL)
		return;

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (bam_type == SPS_BAM_NDP_4K) {
		ctrl = bam[0x0 / 4];
		ver = bam[0x1000 / 4];
		pipes = bam[0x1008 / 4];
	} else {
		ctrl = bam[0x0 / 4];
		ver = bam[0x4 / 4];
		pipes = bam[0x3c / 4];
	}
#else
	ctrl = bam[0xf80 / 4];
	ver = bam[0xf84 / 4];
	pipes = bam[0xfbc / 4];
#endif

	SPS_DUMP("%s",
		"\nsps:<bam-begin> --- Content of BAM-level registers---\n");

	SPS_DUMP("BAM_CTRL: 0x%x\n", ctrl);
	SPS_DUMP("BAM_REVISION: 0x%x\n", ver);
	SPS_DUMP("NUM_PIPES: 0x%x\n", pipes);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (bam_type == SPS_BAM_NDP_4K)
		offset = 0x301c;
	else
		offset = 0x80;
	for (i = 0x0; i < offset; i += 0x10)

#else
	for (i = 0xf80; i < 0x1000; i += 0x10)
#endif
		SPS_DUMP("bam addr 0x%x: 0x%x,0x%x,0x%x,0x%x\n", i,
			bam[i / 4], bam[(i / 4) + 1],
			bam[(i / 4) + 2], bam[(i / 4) + 3]);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (bam_type == SPS_BAM_NDP_4K) {
		offset = 0x3000;
		index = 0x1000;
	} else {
		offset = 0x800;
		index = 0x80;
	}
	for (i = offset, n = 0; n++ < 8; i += index)
#else
	for (i = 0x1800, n = 0; n++ < 4; i += 0x80)
#endif
		SPS_DUMP("bam addr 0x%x: 0x%x,0x%x,0x%x,0x%x\n", i,
			bam[i / 4], bam[(i / 4) + 1],
			bam[(i / 4) + 2], bam[(i / 4) + 3]);

	SPS_DUMP("%s",
		"\nsps:<bam-begin> --- Content of BAM-level registers ---\n");
}

/* output the content of BAM pipe registers */
void print_bam_pipe_reg(void *virt_addr, u32 pipe_index)
{
	int i;
	u32 *bam = (u32 *) virt_addr;
	u32 pipe = pipe_index;
	u32 offset = 0;

	if (bam == NULL)
		return;

	SPS_DUMP("\nsps:<pipe-begin> --- Content of Pipe %d registers ---\n",
			pipe);

	SPS_DUMP("%s", "-- Pipe Management Registers --\n");

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (bam_type == SPS_BAM_NDP_4K)
		offset = 0x13000;
	else
		offset = 0x1000;
	for (i = offset + 0x1000 * pipe; i < offset + 0x1000 * pipe + 0x80;
	    i += 0x10)
#else
	for (i = 0x0000 + 0x80 * pipe; i < 0x0000 + 0x80 * (pipe + 1);
	    i += 0x10)
#endif
		SPS_DUMP("bam addr 0x%x: 0x%x,0x%x,0x%x,0x%x\n", i,
			bam[i / 4], bam[(i / 4) + 1],
			bam[(i / 4) + 2], bam[(i / 4) + 3]);

	SPS_DUMP("%s",
		"-- Pipe Configuration and Internal State Registers --\n");

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	if (bam_type == SPS_BAM_NDP_4K)
		offset = 0x13800;
	else
		offset = 0x1800;
	for (i = offset + 0x1000 * pipe; i < offset + 0x1000 * pipe + 0x40;
		i += 0x10)
#else
	for (i = 0x1000 + 0x40 * pipe; i < 0x1000 + 0x40 * (pipe + 1);
	    i += 0x10)
#endif
		SPS_DUMP("bam addr 0x%x: 0x%x,0x%x,0x%x,0x%x\n", i,
			bam[i / 4], bam[(i / 4) + 1],
			bam[(i / 4) + 2], bam[(i / 4) + 3]);

	SPS_DUMP("\nsps:<pipe-end> --- Content of Pipe %d registers ---\n",
			pipe);
}

/* output the content of selected BAM-level registers */
void print_bam_selected_reg(void *virt_addr, u32 ee)
{
	void *base = virt_addr;

	u32 bam_ctrl;
	u32 bam_revision;
	u32 bam_rev_num;
	u32 bam_rev_ee_num;

	u32 bam_num_pipes;
	u32 bam_pipe_num;
	u32 bam_data_addr_bus_width;

	u32 bam_desc_cnt_trshld;
	u32 bam_desc_cnt_trd_val;

	u32 bam_irq_en;
	u32 bam_irq_stts;

	u32 bam_irq_src_ee = 0;
	u32 bam_irq_msk_ee = 0;
	u32 bam_irq_unmsk_ee = 0;
	u32 bam_pipe_attr_ee = 0;

	u32 bam_ahb_err_ctrl;
	u32 bam_ahb_err_addr;
	u32 bam_ahb_err_data;
	u32 bam_cnfg_bits;

	u32 bam_sw_rev = 0;
	u32 bam_timer = 0;
	u32 bam_timer_ctrl = 0;
	u32 bam_ahb_err_addr_msb = 0;

	if (base == NULL)
		return;

	bam_ctrl = bam_read_reg(base, CTRL, 0);
	bam_revision = bam_read_reg(base, REVISION, 0);
	bam_rev_num = bam_read_reg_field(base, REVISION, 0, BAM_REVISION);
	bam_rev_ee_num = bam_read_reg_field(base, REVISION, 0, BAM_NUM_EES);

	bam_num_pipes = bam_read_reg(base, NUM_PIPES, 0);
	bam_pipe_num = bam_read_reg_field(base, NUM_PIPES, 0, BAM_NUM_PIPES);
	bam_data_addr_bus_width = bam_read_reg_field(base, NUM_PIPES, 0,
					BAM_DATA_ADDR_BUS_WIDTH);

	bam_desc_cnt_trshld = bam_read_reg(base, DESC_CNT_TRSHLD, 0);
	bam_desc_cnt_trd_val = bam_read_reg_field(base, DESC_CNT_TRSHLD, 0,
					BAM_DESC_CNT_TRSHLD);

	bam_irq_en = bam_read_reg(base, IRQ_EN, 0);
	bam_irq_stts = bam_read_reg(base, IRQ_STTS, 0);

	if (ee < BAM_MAX_EES) {
		bam_irq_src_ee = bam_read_reg(base, IRQ_SRCS_EE, ee);
		bam_irq_msk_ee = bam_read_reg(base, IRQ_SRCS_MSK_EE, ee);
		bam_irq_unmsk_ee = bam_read_reg(base, IRQ_SRCS_UNMASKED_EE, ee);
	}

	bam_ahb_err_ctrl = bam_read_reg(base, AHB_MASTER_ERR_CTRLS, 0);
	bam_ahb_err_addr = bam_read_reg(base, AHB_MASTER_ERR_ADDR, 0);
	bam_ahb_err_data = bam_read_reg(base, AHB_MASTER_ERR_DATA, 0);
	bam_cnfg_bits = bam_read_reg(base, CNFG_BITS, 0);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	bam_sw_rev = bam_read_reg(base, SW_REVISION, 0);
	bam_ahb_err_addr_msb = SPS_LPAE ?
		bam_read_reg(base, AHB_MASTER_ERR_ADDR_MSB, 0) : 0;
	if (ee < BAM_MAX_EES)
		bam_pipe_attr_ee = enhd_pipe ?
			bam_read_reg(base, PIPE_ATTR_EE, ee) : 0x0;
#endif


	SPS_DUMP("%s", "\nsps:<bam-begin> --- BAM-level registers ---\n\n");

	SPS_DUMP("BAM_CTRL: 0x%x\n", bam_ctrl);
	SPS_DUMP("BAM_REVISION: 0x%x\n", bam_revision);
	SPS_DUMP("    REVISION: 0x%x\n", bam_rev_num);
	SPS_DUMP("    NUM_EES: %d\n", bam_rev_ee_num);
	SPS_DUMP("BAM_SW_REVISION: 0x%x\n", bam_sw_rev);
	SPS_DUMP("BAM_NUM_PIPES: %d\n", bam_num_pipes);
	SPS_DUMP("BAM_DATA_ADDR_BUS_WIDTH: %d\n",
			((bam_data_addr_bus_width == 0x0) ? 32 : 36));
	SPS_DUMP("    NUM_PIPES: %d\n", bam_pipe_num);
	SPS_DUMP("BAM_DESC_CNT_TRSHLD: 0x%x\n", bam_desc_cnt_trshld);
	SPS_DUMP("    DESC_CNT_TRSHLD: 0x%x (%d)\n", bam_desc_cnt_trd_val,
			bam_desc_cnt_trd_val);

	SPS_DUMP("BAM_IRQ_EN: 0x%x\n", bam_irq_en);
	SPS_DUMP("BAM_IRQ_STTS: 0x%x\n", bam_irq_stts);

	if (ee < BAM_MAX_EES) {
		SPS_DUMP("BAM_IRQ_SRCS_EE(%d): 0x%x\n", ee, bam_irq_src_ee);
		SPS_DUMP("BAM_IRQ_SRCS_MSK_EE(%d): 0x%x\n", ee, bam_irq_msk_ee);
		SPS_DUMP("BAM_IRQ_SRCS_UNMASKED_EE(%d): 0x%x\n", ee,
				bam_irq_unmsk_ee);
		SPS_DUMP("BAM_PIPE_ATTR_EE(%d): 0x%x\n", ee, bam_pipe_attr_ee);
	}

	SPS_DUMP("BAM_AHB_MASTER_ERR_CTRLS: 0x%x\n", bam_ahb_err_ctrl);
	SPS_DUMP("BAM_AHB_MASTER_ERR_ADDR: 0x%x\n", bam_ahb_err_addr);
	SPS_DUMP("BAM_AHB_MASTER_ERR_ADDR_MSB: 0x%x\n", bam_ahb_err_addr_msb);
	SPS_DUMP("BAM_AHB_MASTER_ERR_DATA: 0x%x\n", bam_ahb_err_data);

	SPS_DUMP("BAM_CNFG_BITS: 0x%x\n", bam_cnfg_bits);
	SPS_DUMP("BAM_TIMER: 0x%x\n", bam_timer);
	SPS_DUMP("BAM_TIMER_CTRL: 0x%x\n", bam_timer_ctrl);

	SPS_DUMP("%s", "\nsps:<bam-end> --- BAM-level registers ---\n\n");
}

/* output the content of selected BAM pipe registers */
void print_bam_pipe_selected_reg(void *virt_addr, u32 pipe_index)
{
	void *base = virt_addr;
	u32 pipe = pipe_index;

	u32 p_ctrl;
	u32 p_sys_mode;
	u32 p_direction;
	u32 p_lock_group = 0;

	u32 p_irq_en;
	u32 p_irq_stts;
	u32 p_irq_stts_eot;
	u32 p_irq_stts_int;

	u32 p_prd_sdbd;
	u32 p_bytes_free;
	u32 p_prd_ctrl;
	u32 p_prd_toggle;
	u32 p_prd_sb_updated;

	u32 p_con_sdbd;
	u32 p_bytes_avail;
	u32 p_con_ctrl;
	u32 p_con_toggle;
	u32 p_con_ack_toggle;
	u32 p_con_ack_toggle_r;
	u32 p_con_wait_4_ack;
	u32 p_con_sb_updated;

	u32 p_sw_offset;
	u32 p_read_pointer;
	u32 p_evnt_reg;
	u32 p_write_pointer;

	u32 p_evnt_dest;
	u32 p_evnt_dest_msb = 0;
	u32 p_desc_fifo_addr;
	u32 p_desc_fifo_addr_msb = 0;
	u32 p_desc_fifo_size;
	u32 p_data_fifo_addr;
	u32 p_data_fifo_addr_msb = 0;
	u32 p_data_fifo_size;
	u32 p_fifo_sizes;

	u32 p_evnt_trd;
	u32 p_evnt_trd_val;

	u32 p_retr_ct;
	u32 p_retr_offset;
	u32 p_si_ct;
	u32 p_si_offset;
	u32 p_df_ct = 0;
	u32 p_df_offset = 0;
	u32 p_au_ct1;
	u32 p_psm_ct2;
	u32 p_psm_ct3;
	u32 p_psm_ct3_msb = 0;
	u32 p_psm_ct4;
	u32 p_psm_ct5;

	u32 p_timer = 0;
	u32 p_timer_ctrl = 0;

	if (base == NULL)
		return;

	p_ctrl = bam_read_reg(base, P_CTRL, pipe);
	p_sys_mode = bam_read_reg_field(base, P_CTRL, pipe, P_SYS_MODE);
	p_direction = bam_read_reg_field(base, P_CTRL, pipe, P_DIRECTION);

	p_irq_en = bam_read_reg(base, P_IRQ_EN, pipe);
	p_irq_stts = bam_read_reg(base, P_IRQ_STTS, pipe);
	p_irq_stts_eot = bam_read_reg_field(base, P_IRQ_STTS, pipe,
					P_IRQ_STTS_P_TRNSFR_END_IRQ);
	p_irq_stts_int = bam_read_reg_field(base, P_IRQ_STTS, pipe,
					P_IRQ_STTS_P_PRCSD_DESC_IRQ);

	p_prd_sdbd = bam_read_reg(base, P_PRDCR_SDBND, pipe);
	p_bytes_free = bam_read_reg_field(base, P_PRDCR_SDBND, pipe,
					P_PRDCR_SDBNDn_BAM_P_BYTES_FREE);
	p_prd_ctrl = bam_read_reg_field(base, P_PRDCR_SDBND, pipe,
					P_PRDCR_SDBNDn_BAM_P_CTRL);
	p_prd_toggle = bam_read_reg_field(base, P_PRDCR_SDBND, pipe,
					P_PRDCR_SDBNDn_BAM_P_TOGGLE);
	p_prd_sb_updated = bam_read_reg_field(base, P_PRDCR_SDBND, pipe,
					P_PRDCR_SDBNDn_BAM_P_SB_UPDATED);
	p_con_sdbd = bam_read_reg(base, P_CNSMR_SDBND, pipe);
	p_bytes_avail = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_BYTES_AVAIL);
	p_con_ctrl = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_CTRL);
	p_con_toggle = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_TOGGLE);
	p_con_ack_toggle = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE);
	p_con_ack_toggle_r = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_ACK_TOGGLE_R);
	p_con_wait_4_ack = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_WAIT_4_ACK);
	p_con_sb_updated = bam_read_reg_field(base, P_CNSMR_SDBND, pipe,
					P_CNSMR_SDBNDn_BAM_P_SB_UPDATED);

	p_sw_offset = bam_read_reg(base, P_SW_OFSTS, pipe);
	p_read_pointer = bam_read_reg_field(base, P_SW_OFSTS, pipe,
						SW_DESC_OFST);
	p_evnt_reg = bam_read_reg(base, P_EVNT_REG, pipe);
	p_write_pointer = bam_read_reg_field(base, P_EVNT_REG, pipe,
						P_DESC_FIFO_PEER_OFST);

	p_evnt_dest = bam_read_reg(base, P_EVNT_DEST_ADDR, pipe);
	p_desc_fifo_addr = bam_read_reg(base, P_DESC_FIFO_ADDR, pipe);
	p_desc_fifo_size = bam_read_reg_field(base, P_FIFO_SIZES, pipe,
						P_DESC_FIFO_SIZE);
	p_data_fifo_addr = bam_read_reg(base, P_DATA_FIFO_ADDR, pipe);
	p_data_fifo_size = bam_read_reg_field(base, P_FIFO_SIZES, pipe,
						P_DATA_FIFO_SIZE);
	p_fifo_sizes = bam_read_reg(base, P_FIFO_SIZES, pipe);

	p_evnt_trd = bam_read_reg(base, P_EVNT_GEN_TRSHLD, pipe);
	p_evnt_trd_val = bam_read_reg_field(base, P_EVNT_GEN_TRSHLD, pipe,
					P_EVNT_GEN_TRSHLD_P_TRSHLD);

	p_retr_ct = bam_read_reg(base, P_RETR_CNTXT, pipe);
	p_retr_offset = bam_read_reg_field(base, P_RETR_CNTXT, pipe,
					P_RETR_CNTXT_RETR_DESC_OFST);
	p_si_ct = bam_read_reg(base, P_SI_CNTXT, pipe);
	p_si_offset = bam_read_reg_field(base, P_SI_CNTXT, pipe,
					P_SI_CNTXT_SI_DESC_OFST);
	p_au_ct1 = bam_read_reg(base, P_AU_PSM_CNTXT_1, pipe);
	p_psm_ct2 = bam_read_reg(base, P_PSM_CNTXT_2, pipe);
	p_psm_ct3 = bam_read_reg(base, P_PSM_CNTXT_3, pipe);
	p_psm_ct4 = bam_read_reg(base, P_PSM_CNTXT_4, pipe);
	p_psm_ct5 = bam_read_reg(base, P_PSM_CNTXT_5, pipe);

#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
	p_evnt_dest_msb = SPS_LPAE ?
		bam_read_reg(base, P_EVNT_DEST_ADDR_MSB, pipe) : 0;

	p_desc_fifo_addr_msb = SPS_LPAE ?
		bam_read_reg(base, P_DESC_FIFO_ADDR_MSB, pipe) : 0;
	p_data_fifo_addr_msb = SPS_LPAE ?
		bam_read_reg(base, P_DATA_FIFO_ADDR_MSB, pipe) : 0;

	p_psm_ct3_msb = SPS_LPAE ? bam_read_reg(base, P_PSM_CNTXT_3, pipe) : 0;
	p_lock_group = bam_read_reg_field(base, P_CTRL, pipe, P_LOCK_GROUP);
	p_df_ct = bam_read_reg(base, P_DF_CNTXT, pipe);
	p_df_offset = bam_read_reg_field(base, P_DF_CNTXT, pipe,
					P_DF_CNTXT_DF_DESC_OFST);
#endif

	SPS_DUMP("\nsps:<pipe-begin> --- Registers of Pipe %d ---\n\n", pipe);

	SPS_DUMP("BAM_P_CTRL: 0x%x\n", p_ctrl);
	SPS_DUMP("    SYS_MODE: %d\n", p_sys_mode);
	if (p_direction)
		SPS_DUMP("    DIRECTION:%d->Producer\n", p_direction);
	else
		SPS_DUMP("    DIRECTION:%d->Consumer\n", p_direction);
	SPS_DUMP("    LOCK_GROUP: 0x%x (%d)\n", p_lock_group, p_lock_group);

	SPS_DUMP("BAM_P_IRQ_EN: 0x%x\n", p_irq_en);
	SPS_DUMP("BAM_P_IRQ_STTS: 0x%x\n", p_irq_stts);
	SPS_DUMP("    TRNSFR_END_IRQ(EOT): 0x%x\n", p_irq_stts_eot);
	SPS_DUMP("    PRCSD_DESC_IRQ(INT): 0x%x\n", p_irq_stts_int);

	SPS_DUMP("BAM_P_PRDCR_SDBND: 0x%x\n", p_prd_sdbd);
	SPS_DUMP("    BYTES_FREE: 0x%x (%d)\n", p_bytes_free, p_bytes_free);
	SPS_DUMP("    CTRL: 0x%x\n", p_prd_ctrl);
	SPS_DUMP("    TOGGLE: %d\n", p_prd_toggle);
	SPS_DUMP("    SB_UPDATED: %d\n", p_prd_sb_updated);
	SPS_DUMP("BAM_P_CNSMR_SDBND: 0x%x\n", p_con_sdbd);
	SPS_DUMP("    WAIT_4_ACK: %d\n", p_con_wait_4_ack);
	SPS_DUMP("    BYTES_AVAIL: 0x%x (%d)\n", p_bytes_avail, p_bytes_avail);
	SPS_DUMP("    CTRL: 0x%x\n", p_con_ctrl);
	SPS_DUMP("    TOGGLE: %d\n", p_con_toggle);
	SPS_DUMP("    ACK_TOGGLE: %d\n", p_con_ack_toggle);
	SPS_DUMP("    ACK_TOGGLE_R: %d\n", p_con_ack_toggle_r);
	SPS_DUMP("    SB_UPDATED: %d\n", p_con_sb_updated);

	SPS_DUMP("BAM_P_SW_DESC_OFST: 0x%x\n", p_sw_offset);
	SPS_DUMP("    SW_DESC_OFST: 0x%x\n", p_read_pointer);
	SPS_DUMP("BAM_P_EVNT_REG: 0x%x\n", p_evnt_reg);
	SPS_DUMP("    DESC_FIFO_PEER_OFST: 0x%x\n", p_write_pointer);

	SPS_DUMP("BAM_P_RETR_CNTXT: 0x%x\n", p_retr_ct);
	SPS_DUMP("    RETR_OFFSET: 0x%x\n", p_retr_offset);
	SPS_DUMP("BAM_P_SI_CNTXT: 0x%x\n", p_si_ct);
	SPS_DUMP("    SI_OFFSET: 0x%x\n", p_si_offset);
	SPS_DUMP("BAM_P_DF_CNTXT: 0x%x\n", p_df_ct);
	SPS_DUMP("    DF_OFFSET: 0x%x\n", p_df_offset);

	SPS_DUMP("BAM_P_DESC_FIFO_ADDR: 0x%x\n", p_desc_fifo_addr);
	SPS_DUMP("BAM_P_DESC_FIFO_ADDR_MSB: 0x%x\n", p_desc_fifo_addr_msb);
	SPS_DUMP("BAM_P_DATA_FIFO_ADDR: 0x%x\n", p_data_fifo_addr);
	SPS_DUMP("BAM_P_DATA_FIFO_ADDR_MSB: 0x%x\n", p_data_fifo_addr_msb);
	SPS_DUMP("BAM_P_FIFO_SIZES: 0x%x\n", p_fifo_sizes);
	SPS_DUMP("    DESC_FIFO_SIZE: 0x%x (%d)\n", p_desc_fifo_size,
							p_desc_fifo_size);
	SPS_DUMP("    DATA_FIFO_SIZE: 0x%x (%d)\n", p_data_fifo_size,
							p_data_fifo_size);

	SPS_DUMP("BAM_P_EVNT_DEST_ADDR: 0x%x\n", p_evnt_dest);
	SPS_DUMP("BAM_P_EVNT_DEST_ADDR_MSB: 0x%x\n", p_evnt_dest_msb);
	SPS_DUMP("BAM_P_EVNT_GEN_TRSHLD: 0x%x\n", p_evnt_trd);
	SPS_DUMP("    EVNT_GEN_TRSHLD: 0x%x (%d)\n", p_evnt_trd_val,
							p_evnt_trd_val);

	SPS_DUMP("BAM_P_AU_PSM_CNTXT_1: 0x%x\n", p_au_ct1);
	SPS_DUMP("BAM_P_PSM_CNTXT_2: 0x%x\n", p_psm_ct2);
	SPS_DUMP("BAM_P_PSM_CNTXT_3: 0x%x\n", p_psm_ct3);
	SPS_DUMP("BAM_P_PSM_CNTXT_3_MSB: 0x%x\n", p_psm_ct3_msb);
	SPS_DUMP("BAM_P_PSM_CNTXT_4: 0x%x\n", p_psm_ct4);
	SPS_DUMP("BAM_P_PSM_CNTXT_5: 0x%x\n", p_psm_ct5);
	SPS_DUMP("BAM_P_TIMER: 0x%x\n", p_timer);
	SPS_DUMP("BAM_P_TIMER_CTRL: 0x%x\n", p_timer_ctrl);

	SPS_DUMP("\nsps:<pipe-end> --- Registers of Pipe %d ---\n\n", pipe);
}

/* output descriptor FIFO of a pipe */
void print_bam_pipe_desc_fifo(void *virt_addr, u32 pipe_index, u32 option)
{
	void *base = virt_addr;
	u32 pipe = pipe_index;
	u32 desc_fifo_addr;
	u32 desc_fifo_size;
	u32 *desc_fifo;
	int i;
	char desc_info[MAX_MSG_LEN];

	if (base == NULL)
		return;

	desc_fifo_addr = bam_read_reg(base, P_DESC_FIFO_ADDR, pipe);
	desc_fifo_size = bam_read_reg_field(base, P_FIFO_SIZES, pipe,
						P_DESC_FIFO_SIZE);

	if (desc_fifo_addr == 0) {
		SPS_ERR(sps, "sps: desc FIFO address of Pipe %d is NULL\n",
				pipe);
		return;
	} else if (desc_fifo_size == 0) {
		SPS_ERR(sps, "sps: desc FIFO size of Pipe %d is 0\n", pipe);
		return;
	}

	SPS_DUMP("\nsps:<desc-begin> --- descriptor FIFO of Pipe %d -----\n\n",
			pipe);

	SPS_DUMP("BAM_P_DESC_FIFO_ADDR: 0x%x\n"
		"BAM_P_DESC_FIFO_SIZE: 0x%x (%d)\n\n",
		desc_fifo_addr, desc_fifo_size, desc_fifo_size);

	desc_fifo = (u32 *) phys_to_virt(desc_fifo_addr);

	if (option == 100) {
		SPS_DUMP("%s",
			"----- start of data blocks -----\n");
		for (i = 0; i < desc_fifo_size; i += 8) {
			u32 *data_block_vir;
			u32 data_block_phy = desc_fifo[i / 4];

			if (data_block_phy) {
				data_block_vir =
					(u32 *) phys_to_virt(data_block_phy);

				SPS_DUMP("desc addr:0x%x; data addr:0x%x:\n",
					desc_fifo_addr + i, data_block_phy);
				SPS_DUMP("0x%x, 0x%x, 0x%x, 0x%x\n",
					data_block_vir[0], data_block_vir[1],
					data_block_vir[2], data_block_vir[3]);
				SPS_DUMP("0x%x, 0x%x, 0x%x, 0x%x\n",
					data_block_vir[4], data_block_vir[5],
					data_block_vir[6], data_block_vir[7]);
				SPS_DUMP("0x%x, 0x%x, 0x%x, 0x%x\n",
					data_block_vir[8], data_block_vir[9],
					data_block_vir[10], data_block_vir[11]);
				SPS_DUMP("0x%x, 0x%x, 0x%x, 0x%x\n\n",
					data_block_vir[12], data_block_vir[13],
					data_block_vir[14], data_block_vir[15]);
			}
		}
		SPS_DUMP("%s",
			"----- end of data blocks -----\n");
	} else if (option) {
		u32 size = option * 128;
		u32 current_desc = bam_pipe_get_desc_read_offset(base,
								pipe_index);
		u32 begin = 0;
		u32 end = desc_fifo_size;

		if (current_desc > size / 2)
			begin = current_desc - size / 2;

		if (desc_fifo_size > current_desc + size / 2)
			end = current_desc + size / 2;

		SPS_DUMP("%s",
			"------------ begin of partial FIFO ------------\n\n");

		SPS_DUMP("%s",
			"desc addr; desc content; desc flags\n");
		for (i = begin; i < end; i += 0x8) {
			u32 offset;
			u32 flags = desc_fifo[(i / 4) + 1] >> 16;

			memset(desc_info, 0, sizeof(desc_info));
			offset = scnprintf(desc_info, 40, "0x%x: 0x%x, 0x%x: ",
				desc_fifo_addr + i,
				desc_fifo[i / 4], desc_fifo[(i / 4) + 1]);

			if (flags & SPS_IOVEC_FLAG_INT)
				offset += scnprintf(desc_info + offset, 5,
							"INT ");
			if (flags & SPS_IOVEC_FLAG_EOT)
				offset += scnprintf(desc_info + offset, 5,
							"EOT ");
			if (flags & SPS_IOVEC_FLAG_EOB)
				offset += scnprintf(desc_info + offset, 5,
							"EOB ");
			if (flags & SPS_IOVEC_FLAG_NWD)
				offset += scnprintf(desc_info + offset, 5,
							"NWD ");
			if (flags & SPS_IOVEC_FLAG_CMD)
				offset += scnprintf(desc_info + offset, 5,
							"CMD ");
			if (flags & SPS_IOVEC_FLAG_LOCK)
				offset += scnprintf(desc_info + offset, 5,
							"LCK ");
			if (flags & SPS_IOVEC_FLAG_UNLOCK)
				offset += scnprintf(desc_info + offset, 5,
							"UNL ");
			if (flags & SPS_IOVEC_FLAG_IMME)
				offset += scnprintf(desc_info + offset, 5,
							"IMM ");

			SPS_DUMP("%s\n", desc_info);
		}

		SPS_DUMP("%s",
			"\n------------  end of partial FIFO  ------------\n");
	} else {
		SPS_DUMP("%s",
			"---------------- begin of FIFO ----------------\n\n");

		for (i = 0; i < desc_fifo_size; i += 0x10)
			SPS_DUMP("addr 0x%x: 0x%x, 0x%x, 0x%x, 0x%x\n",
				desc_fifo_addr + i,
				desc_fifo[i / 4], desc_fifo[(i / 4) + 1],
				desc_fifo[(i / 4) + 2], desc_fifo[(i / 4) + 3]);

		SPS_DUMP("%s",
			"\n----------------  end of FIFO  ----------------\n");
	}

	SPS_DUMP("\nsps:<desc-end> --- descriptor FIFO of Pipe %d -----\n\n",
			pipe);
}

/* output BAM_TEST_BUS_REG with specified TEST_BUS_SEL */
void print_bam_test_bus_reg(void *base, u32 tb_sel)
{
	u32 i;
	u32 test_bus_selection[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x20, 0x21, 0x22, 0x23,
			0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
	u32 size = ARRAY_SIZE(test_bus_selection);

	if (base == NULL)
		return;

	if (tb_sel) {
		SPS_DUMP("\nsps:Specified TEST_BUS_SEL value: 0x%x\n", tb_sel);
		bam_write_reg_field(base, TEST_BUS_SEL, 0, BAM_TESTBUS_SEL,
					tb_sel);
		SPS_DUMP("sps:BAM_TEST_BUS_REG:0x%x for TEST_BUS_SEL:0x%x\n\n",
			bam_read_reg(base, TEST_BUS_REG, 0),
			bam_read_reg_field(base, TEST_BUS_SEL, 0,
						BAM_TESTBUS_SEL));
	}

	SPS_DUMP("%s", "\nsps:<testbus-begin> --- BAM TEST_BUS dump -----\n\n");

	/* output other selections */
	for (i = 0; i < size; i++) {
		bam_write_reg_field(base, TEST_BUS_SEL, 0, BAM_TESTBUS_SEL,
					test_bus_selection[i]);

		SPS_DUMP("sps:TEST_BUS_REG:0x%x\t  TEST_BUS_SEL:0x%x\n",
			bam_read_reg(base, TEST_BUS_REG, 0),
			bam_read_reg_field(base, TEST_BUS_SEL, 0,
					BAM_TESTBUS_SEL));
	}

	SPS_DUMP("%s", "\nsps:<testbus-end> --- BAM TEST_BUS dump -----\n\n");
}
