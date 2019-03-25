// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2017-2018 Broadcom */

#ifndef V3D_REGS_H
#define V3D_REGS_H

#include <linux/bitops.h>

#define V3D_MASK(high, low) ((u32)GENMASK(high, low))
/* Using the GNU statement expression extension */
#define V3D_SET_FIELD(value, field)					\
	({								\
		u32 fieldval = (value) << field##_SHIFT;		\
		WARN_ON((fieldval & ~field##_MASK) != 0);		\
		fieldval & field##_MASK;				\
	 })

#define V3D_GET_FIELD(word, field) (((word) & field##_MASK) >>		\
				    field##_SHIFT)

/* Hub registers for shared hardware between V3D cores. */

#define V3D_HUB_AXICFG                                 0x00000
# define V3D_HUB_AXICFG_MAX_LEN_MASK                   V3D_MASK(3, 0)
# define V3D_HUB_AXICFG_MAX_LEN_SHIFT                  0
#define V3D_HUB_UIFCFG                                 0x00004
#define V3D_HUB_IDENT0                                 0x00008

#define V3D_HUB_IDENT1                                 0x0000c
# define V3D_HUB_IDENT1_WITH_MSO                       BIT(19)
# define V3D_HUB_IDENT1_WITH_TSY                       BIT(18)
# define V3D_HUB_IDENT1_WITH_TFU                       BIT(17)
# define V3D_HUB_IDENT1_WITH_L3C                       BIT(16)
# define V3D_HUB_IDENT1_NHOSTS_MASK                    V3D_MASK(15, 12)
# define V3D_HUB_IDENT1_NHOSTS_SHIFT                   12
# define V3D_HUB_IDENT1_NCORES_MASK                    V3D_MASK(11, 8)
# define V3D_HUB_IDENT1_NCORES_SHIFT                   8
# define V3D_HUB_IDENT1_REV_MASK                       V3D_MASK(7, 4)
# define V3D_HUB_IDENT1_REV_SHIFT                      4
# define V3D_HUB_IDENT1_TVER_MASK                      V3D_MASK(3, 0)
# define V3D_HUB_IDENT1_TVER_SHIFT                     0

#define V3D_HUB_IDENT2                                 0x00010
# define V3D_HUB_IDENT2_WITH_MMU                       BIT(8)
# define V3D_HUB_IDENT2_L3C_NKB_MASK                   V3D_MASK(7, 0)
# define V3D_HUB_IDENT2_L3C_NKB_SHIFT                  0

#define V3D_HUB_IDENT3                                 0x00014
# define V3D_HUB_IDENT3_IPREV_MASK                     V3D_MASK(15, 8)
# define V3D_HUB_IDENT3_IPREV_SHIFT                    8
# define V3D_HUB_IDENT3_IPIDX_MASK                     V3D_MASK(7, 0)
# define V3D_HUB_IDENT3_IPIDX_SHIFT                    0

#define V3D_HUB_INT_STS                                0x00050
#define V3D_HUB_INT_SET                                0x00054
#define V3D_HUB_INT_CLR                                0x00058
#define V3D_HUB_INT_MSK_STS                            0x0005c
#define V3D_HUB_INT_MSK_SET                            0x00060
#define V3D_HUB_INT_MSK_CLR                            0x00064
# define V3D_HUB_INT_MMU_WRV                           BIT(5)
# define V3D_HUB_INT_MMU_PTI                           BIT(4)
# define V3D_HUB_INT_MMU_CAP                           BIT(3)
# define V3D_HUB_INT_MSO                               BIT(2)
# define V3D_HUB_INT_TFUC                              BIT(1)
# define V3D_HUB_INT_TFUF                              BIT(0)

#define V3D_GCA_CACHE_CTRL                             0x0000c
# define V3D_GCA_CACHE_CTRL_FLUSH                      BIT(0)

#define V3D_GCA_SAFE_SHUTDOWN                          0x000b0
# define V3D_GCA_SAFE_SHUTDOWN_EN                      BIT(0)

#define V3D_GCA_SAFE_SHUTDOWN_ACK                      0x000b4
# define V3D_GCA_SAFE_SHUTDOWN_ACK_ACKED               3

# define V3D_TOP_GR_BRIDGE_REVISION                    0x00000
# define V3D_TOP_GR_BRIDGE_MAJOR_MASK                  V3D_MASK(15, 8)
# define V3D_TOP_GR_BRIDGE_MAJOR_SHIFT                 8
# define V3D_TOP_GR_BRIDGE_MINOR_MASK                  V3D_MASK(7, 0)
# define V3D_TOP_GR_BRIDGE_MINOR_SHIFT                 0

/* 7268 reset reg */
# define V3D_TOP_GR_BRIDGE_SW_INIT_0                   0x00008
# define V3D_TOP_GR_BRIDGE_SW_INIT_0_V3D_CLK_108_SW_INIT BIT(0)
/* 7278 reset reg */
# define V3D_TOP_GR_BRIDGE_SW_INIT_1                   0x0000c
# define V3D_TOP_GR_BRIDGE_SW_INIT_1_V3D_CLK_108_SW_INIT BIT(0)

#define V3D_TFU_CS                                     0x00400
/* Stops current job, empties input fifo. */
# define V3D_TFU_CS_TFURST                             BIT(31)
# define V3D_TFU_CS_CVTCT_MASK                         V3D_MASK(23, 16)
# define V3D_TFU_CS_CVTCT_SHIFT                        16
# define V3D_TFU_CS_NFREE_MASK                         V3D_MASK(13, 8)
# define V3D_TFU_CS_NFREE_SHIFT                        8
# define V3D_TFU_CS_BUSY                               BIT(0)

#define V3D_TFU_SU                                     0x00404
/* Interrupt when FINTTHR input slots are free (0 = disabled) */
# define V3D_TFU_SU_FINTTHR_MASK                       V3D_MASK(13, 8)
# define V3D_TFU_SU_FINTTHR_SHIFT                      8
/* Skips resetting the CRC at the start of CRC generation. */
# define V3D_TFU_SU_CRCCHAIN                           BIT(4)
/* skips writes, computes CRC of the image.  miplevels must be 0. */
# define V3D_TFU_SU_CRC                                BIT(3)
# define V3D_TFU_SU_THROTTLE_MASK                      V3D_MASK(1, 0)
# define V3D_TFU_SU_THROTTLE_SHIFT                     0

#define V3D_TFU_ICFG                                   0x00408
/* Interrupt when the conversion is complete. */
# define V3D_TFU_ICFG_IOC                              BIT(0)

/* Input Image Address */
#define V3D_TFU_IIA                                    0x0040c
/* Input Chroma Address */
#define V3D_TFU_ICA                                    0x00410
/* Input Image Stride */
#define V3D_TFU_IIS                                    0x00414
/* Input Image U-Plane Address */
#define V3D_TFU_IUA                                    0x00418
/* Output Image Address */
#define V3D_TFU_IOA                                    0x0041c
/* Image Output Size */
#define V3D_TFU_IOS                                    0x00420
/* TFU YUV Coefficient 0 */
#define V3D_TFU_COEF0                                  0x00424
/* Use these regs instead of the defaults. */
# define V3D_TFU_COEF0_USECOEF                         BIT(31)
/* TFU YUV Coefficient 1 */
#define V3D_TFU_COEF1                                  0x00428
/* TFU YUV Coefficient 2 */
#define V3D_TFU_COEF2                                  0x0042c
/* TFU YUV Coefficient 3 */
#define V3D_TFU_COEF3                                  0x00430

#define V3D_TFU_CRC                                    0x00434

/* Per-MMU registers. */

#define V3D_MMUC_CONTROL                               0x01000
# define V3D_MMUC_CONTROL_CLEAR                        BIT(3)
# define V3D_MMUC_CONTROL_FLUSHING                     BIT(2)
# define V3D_MMUC_CONTROL_FLUSH                        BIT(1)
# define V3D_MMUC_CONTROL_ENABLE                       BIT(0)

#define V3D_MMU_CTL                                    0x01200
# define V3D_MMU_CTL_CAP_EXCEEDED                      BIT(27)
# define V3D_MMU_CTL_CAP_EXCEEDED_ABORT                BIT(26)
# define V3D_MMU_CTL_CAP_EXCEEDED_INT                  BIT(25)
# define V3D_MMU_CTL_CAP_EXCEEDED_EXCEPTION            BIT(24)
# define V3D_MMU_CTL_PT_INVALID                        BIT(20)
# define V3D_MMU_CTL_PT_INVALID_ABORT                  BIT(19)
# define V3D_MMU_CTL_PT_INVALID_INT                    BIT(18)
# define V3D_MMU_CTL_PT_INVALID_EXCEPTION              BIT(17)
# define V3D_MMU_CTL_WRITE_VIOLATION                   BIT(16)
# define V3D_MMU_CTL_WRITE_VIOLATION_ABORT             BIT(11)
# define V3D_MMU_CTL_WRITE_VIOLATION_INT               BIT(10)
# define V3D_MMU_CTL_WRITE_VIOLATION_EXCEPTION         BIT(9)
# define V3D_MMU_CTL_TLB_CLEARING                      BIT(7)
# define V3D_MMU_CTL_TLB_STATS_CLEAR                   BIT(3)
# define V3D_MMU_CTL_TLB_CLEAR                         BIT(2)
# define V3D_MMU_CTL_TLB_STATS_ENABLE                  BIT(1)
# define V3D_MMU_CTL_ENABLE                            BIT(0)

#define V3D_MMU_PT_PA_BASE                             0x01204
#define V3D_MMU_HIT                                    0x01208
#define V3D_MMU_MISSES                                 0x0120c
#define V3D_MMU_STALLS                                 0x01210

#define V3D_MMU_ADDR_CAP                               0x01214
# define V3D_MMU_ADDR_CAP_ENABLE                       BIT(31)
# define V3D_MMU_ADDR_CAP_MPAGE_MASK                   V3D_MASK(11, 0)
# define V3D_MMU_ADDR_CAP_MPAGE_SHIFT                  0

#define V3D_MMU_SHOOT_DOWN                             0x01218
# define V3D_MMU_SHOOT_DOWN_SHOOTING                   BIT(29)
# define V3D_MMU_SHOOT_DOWN_SHOOT                      BIT(28)
# define V3D_MMU_SHOOT_DOWN_PAGE_MASK                  V3D_MASK(27, 0)
# define V3D_MMU_SHOOT_DOWN_PAGE_SHIFT                 0

#define V3D_MMU_BYPASS_START                           0x0121c
#define V3D_MMU_BYPASS_END                             0x01220

/* AXI ID of the access that faulted */
#define V3D_MMU_VIO_ID                                 0x0122c

/* Address for illegal PTEs to return */
#define V3D_MMU_ILLEGAL_ADDR                           0x01230
# define V3D_MMU_ILLEGAL_ADDR_ENABLE                   BIT(31)

/* Address that faulted */
#define V3D_MMU_VIO_ADDR                               0x01234

/* Per-V3D-core registers */

#define V3D_CTL_IDENT0                                 0x00000
# define V3D_IDENT0_VER_MASK                           V3D_MASK(31, 24)
# define V3D_IDENT0_VER_SHIFT                          24

#define V3D_CTL_IDENT1                                 0x00004
/* Multiples of 1kb */
# define V3D_IDENT1_VPM_SIZE_MASK                      V3D_MASK(31, 28)
# define V3D_IDENT1_VPM_SIZE_SHIFT                     28
# define V3D_IDENT1_NSEM_MASK                          V3D_MASK(23, 16)
# define V3D_IDENT1_NSEM_SHIFT                         16
# define V3D_IDENT1_NTMU_MASK                          V3D_MASK(15, 12)
# define V3D_IDENT1_NTMU_SHIFT                         12
# define V3D_IDENT1_QUPS_MASK                          V3D_MASK(11, 8)
# define V3D_IDENT1_QUPS_SHIFT                         8
# define V3D_IDENT1_NSLC_MASK                          V3D_MASK(7, 4)
# define V3D_IDENT1_NSLC_SHIFT                         4
# define V3D_IDENT1_REV_MASK                           V3D_MASK(3, 0)
# define V3D_IDENT1_REV_SHIFT                          0

#define V3D_CTL_IDENT2                                 0x00008
# define V3D_IDENT2_BCG_INT                            BIT(28)

#define V3D_CTL_MISCCFG                                0x00018
# define V3D_CTL_MISCCFG_QRMAXCNT_MASK                 V3D_MASK(3, 1)
# define V3D_CTL_MISCCFG_QRMAXCNT_SHIFT                1
# define V3D_MISCCFG_OVRTMUOUT                         BIT(0)

#define V3D_CTL_L2CACTL                                0x00020
# define V3D_L2CACTL_L2CCLR                            BIT(2)
# define V3D_L2CACTL_L2CDIS                            BIT(1)
# define V3D_L2CACTL_L2CENA                            BIT(0)

#define V3D_CTL_SLCACTL                                0x00024
# define V3D_SLCACTL_TVCCS_MASK                        V3D_MASK(27, 24)
# define V3D_SLCACTL_TVCCS_SHIFT                       24
# define V3D_SLCACTL_TDCCS_MASK                        V3D_MASK(19, 16)
# define V3D_SLCACTL_TDCCS_SHIFT                       16
# define V3D_SLCACTL_UCC_MASK                          V3D_MASK(11, 8)
# define V3D_SLCACTL_UCC_SHIFT                         8
# define V3D_SLCACTL_ICC_MASK                          V3D_MASK(3, 0)
# define V3D_SLCACTL_ICC_SHIFT                         0

#define V3D_CTL_L2TCACTL                               0x00030
# define V3D_L2TCACTL_TMUWCF                           BIT(8)
# define V3D_L2TCACTL_L2T_NO_WM                        BIT(4)
# define V3D_L2TCACTL_FLM_FLUSH                        0
# define V3D_L2TCACTL_FLM_CLEAR                        1
# define V3D_L2TCACTL_FLM_CLEAN                        2
# define V3D_L2TCACTL_FLM_MASK                         V3D_MASK(2, 1)
# define V3D_L2TCACTL_FLM_SHIFT                        1
# define V3D_L2TCACTL_L2TFLS                           BIT(0)
#define V3D_CTL_L2TFLSTA                               0x00034
#define V3D_CTL_L2TFLEND                               0x00038

#define V3D_CTL_INT_STS                                0x00050
#define V3D_CTL_INT_SET                                0x00054
#define V3D_CTL_INT_CLR                                0x00058
#define V3D_CTL_INT_MSK_STS                            0x0005c
#define V3D_CTL_INT_MSK_SET                            0x00060
#define V3D_CTL_INT_MSK_CLR                            0x00064
# define V3D_INT_QPU_MASK                              V3D_MASK(27, 16)
# define V3D_INT_QPU_SHIFT                             16
# define V3D_INT_GMPV                                  BIT(5)
# define V3D_INT_TRFB                                  BIT(4)
# define V3D_INT_SPILLUSE                              BIT(3)
# define V3D_INT_OUTOMEM                               BIT(2)
# define V3D_INT_FLDONE                                BIT(1)
# define V3D_INT_FRDONE                                BIT(0)

#define V3D_CLE_CT0CS                                  0x00100
#define V3D_CLE_CT1CS                                  0x00104
#define V3D_CLE_CTNCS(n) (V3D_CLE_CT0CS + 4 * n)
#define V3D_CLE_CT0EA                                  0x00108
#define V3D_CLE_CT1EA                                  0x0010c
#define V3D_CLE_CTNEA(n) (V3D_CLE_CT0EA + 4 * n)
#define V3D_CLE_CT0CA                                  0x00110
#define V3D_CLE_CT1CA                                  0x00114
#define V3D_CLE_CTNCA(n) (V3D_CLE_CT0CA + 4 * n)
#define V3D_CLE_CT0RA                                  0x00118
#define V3D_CLE_CT1RA                                  0x0011c
#define V3D_CLE_CTNRA(n) (V3D_CLE_CT0RA + 4 * n)
#define V3D_CLE_CT0LC                                  0x00120
#define V3D_CLE_CT1LC                                  0x00124
#define V3D_CLE_CT0PC                                  0x00128
#define V3D_CLE_CT1PC                                  0x0012c
#define V3D_CLE_PCS                                    0x00130
#define V3D_CLE_BFC                                    0x00134
#define V3D_CLE_RFC                                    0x00138
#define V3D_CLE_TFBC                                   0x0013c
#define V3D_CLE_TFIT                                   0x00140
#define V3D_CLE_CT1CFG                                 0x00144
#define V3D_CLE_CT1TILECT                              0x00148
#define V3D_CLE_CT1TSKIP                               0x0014c
#define V3D_CLE_CT1PTCT                                0x00150
#define V3D_CLE_CT0SYNC                                0x00154
#define V3D_CLE_CT1SYNC                                0x00158
#define V3D_CLE_CT0QTS                                 0x0015c
# define V3D_CLE_CT0QTS_ENABLE                         BIT(1)
#define V3D_CLE_CT0QBA                                 0x00160
#define V3D_CLE_CT1QBA                                 0x00164
#define V3D_CLE_CTNQBA(n) (V3D_CLE_CT0QBA + 4 * n)
#define V3D_CLE_CT0QEA                                 0x00168
#define V3D_CLE_CT1QEA                                 0x0016c
#define V3D_CLE_CTNQEA(n) (V3D_CLE_CT0QEA + 4 * n)
#define V3D_CLE_CT0QMA                                 0x00170
#define V3D_CLE_CT0QMS                                 0x00174
#define V3D_CLE_CT1QCFG                                0x00178
/* If set without ETPROC, entirely skip tiles with no primitives. */
# define V3D_CLE_QCFG_ETFILT                           BIT(7)
/* If set with ETFILT, just write the clear color to tiles with no
 * primitives.
 */
# define V3D_CLE_QCFG_ETPROC                           BIT(6)
# define V3D_CLE_QCFG_ETSFLUSH                         BIT(1)
# define V3D_CLE_QCFG_MCDIS                            BIT(0)

#define V3D_PTB_BPCA                                   0x00300
#define V3D_PTB_BPCS                                   0x00304
#define V3D_PTB_BPOA                                   0x00308
#define V3D_PTB_BPOS                                   0x0030c

#define V3D_PTB_BXCF                                   0x00310
# define V3D_PTB_BXCF_RWORDERDISA                      BIT(1)
# define V3D_PTB_BXCF_CLIPDISA                         BIT(0)

#define V3D_V3_PCTR_0_EN                               0x00674
#define V3D_V3_PCTR_0_EN_ENABLE                        BIT(31)
#define V3D_V4_PCTR_0_EN                               0x00650
/* When a bit is set, resets the counter to 0. */
#define V3D_V3_PCTR_0_CLR                              0x00670
#define V3D_V4_PCTR_0_CLR                              0x00654
#define V3D_PCTR_0_OVERFLOW                            0x00658

#define V3D_V3_PCTR_0_PCTRS0                           0x00684
#define V3D_V3_PCTR_0_PCTRS15                          0x00660
#define V3D_V3_PCTR_0_PCTRSX(x)                        (V3D_V3_PCTR_0_PCTRS0 + \
							4 * (x))
/* Each src reg muxes four counters each. */
#define V3D_V4_PCTR_0_SRC_0_3                          0x00660
#define V3D_V4_PCTR_0_SRC_28_31                        0x0067c
# define V3D_PCTR_S0_MASK                              V3D_MASK(6, 0)
# define V3D_PCTR_S0_SHIFT                             0
# define V3D_PCTR_S1_MASK                              V3D_MASK(14, 8)
# define V3D_PCTR_S1_SHIFT                             8
# define V3D_PCTR_S2_MASK                              V3D_MASK(22, 16)
# define V3D_PCTR_S2_SHIFT                             16
# define V3D_PCTR_S3_MASK                              V3D_MASK(30, 24)
# define V3D_PCTR_S3_SHIFT                             24
# define V3D_PCTR_CYCLE_COUNT                          32

/* Output values of the counters. */
#define V3D_PCTR_0_PCTR0                               0x00680
#define V3D_PCTR_0_PCTR31                              0x006fc
#define V3D_PCTR_0_PCTRX(x)                            (V3D_PCTR_0_PCTR0 + \
							4 * (x))
#define V3D_GMP_STATUS                                 0x00800
# define V3D_GMP_STATUS_GMPRST                         BIT(31)
# define V3D_GMP_STATUS_WR_COUNT_MASK                  V3D_MASK(30, 24)
# define V3D_GMP_STATUS_WR_COUNT_SHIFT                 24
# define V3D_GMP_STATUS_RD_COUNT_MASK                  V3D_MASK(22, 16)
# define V3D_GMP_STATUS_RD_COUNT_SHIFT                 16
# define V3D_GMP_STATUS_WR_ACTIVE                      BIT(5)
# define V3D_GMP_STATUS_RD_ACTIVE                      BIT(4)
# define V3D_GMP_STATUS_CFG_BUSY                       BIT(3)
# define V3D_GMP_STATUS_CNTOVF                         BIT(2)
# define V3D_GMP_STATUS_INVPROT                        BIT(1)
# define V3D_GMP_STATUS_VIO                            BIT(0)

#define V3D_GMP_CFG                                    0x00804
# define V3D_GMP_CFG_LBURSTEN                          BIT(3)
# define V3D_GMP_CFG_PGCRSEN                           BIT()
# define V3D_GMP_CFG_STOP_REQ                          BIT(1)
# define V3D_GMP_CFG_PROT_ENABLE                       BIT(0)

#define V3D_GMP_VIO_ADDR                               0x00808
#define V3D_GMP_VIO_TYPE                               0x0080c
#define V3D_GMP_TABLE_ADDR                             0x00810
#define V3D_GMP_CLEAR_LOAD                             0x00814
#define V3D_GMP_PRESERVE_LOAD                          0x00818
#define V3D_GMP_VALID_LINES                            0x00820

#endif /* V3D_REGS_H */
