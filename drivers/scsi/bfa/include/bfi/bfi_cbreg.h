/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/*
 * bfi_cbreg.h crossbow host block register definitions
 *
 * !!! Do not edit. Auto generated. !!!
 */

#ifndef __BFI_CBREG_H__
#define __BFI_CBREG_H__


#define HOSTFN0_INT_STATUS               0x00014000
#define __HOSTFN0_INT_STATUS_LVL_MK      0x00f00000
#define __HOSTFN0_INT_STATUS_LVL_SH      20
#define __HOSTFN0_INT_STATUS_LVL(_v)     ((_v) << __HOSTFN0_INT_STATUS_LVL_SH)
#define __HOSTFN0_INT_STATUS_P           0x000fffff
#define HOSTFN0_INT_MSK                  0x00014004
#define HOST_PAGE_NUM_FN0                0x00014008
#define __HOST_PAGE_NUM_FN               0x000001ff
#define HOSTFN1_INT_STATUS               0x00014100
#define __HOSTFN1_INT_STAT_LVL_MK        0x00f00000
#define __HOSTFN1_INT_STAT_LVL_SH        20
#define __HOSTFN1_INT_STAT_LVL(_v)       ((_v) << __HOSTFN1_INT_STAT_LVL_SH)
#define __HOSTFN1_INT_STAT_P             0x000fffff
#define HOSTFN1_INT_MSK                  0x00014104
#define HOST_PAGE_NUM_FN1                0x00014108
#define APP_PLL_400_CTL_REG              0x00014204
#define __P_400_PLL_LOCK                 0x80000000
#define __APP_PLL_400_SRAM_USE_100MHZ    0x00100000
#define __APP_PLL_400_RESET_TIMER_MK     0x000e0000
#define __APP_PLL_400_RESET_TIMER_SH     17
#define __APP_PLL_400_RESET_TIMER(_v)    ((_v) << __APP_PLL_400_RESET_TIMER_SH)
#define __APP_PLL_400_LOGIC_SOFT_RESET   0x00010000
#define __APP_PLL_400_CNTLMT0_1_MK       0x0000c000
#define __APP_PLL_400_CNTLMT0_1_SH       14
#define __APP_PLL_400_CNTLMT0_1(_v)      ((_v) << __APP_PLL_400_CNTLMT0_1_SH)
#define __APP_PLL_400_JITLMT0_1_MK       0x00003000
#define __APP_PLL_400_JITLMT0_1_SH       12
#define __APP_PLL_400_JITLMT0_1(_v)      ((_v) << __APP_PLL_400_JITLMT0_1_SH)
#define __APP_PLL_400_HREF               0x00000800
#define __APP_PLL_400_HDIV               0x00000400
#define __APP_PLL_400_P0_1_MK            0x00000300
#define __APP_PLL_400_P0_1_SH            8
#define __APP_PLL_400_P0_1(_v)           ((_v) << __APP_PLL_400_P0_1_SH)
#define __APP_PLL_400_Z0_2_MK            0x000000e0
#define __APP_PLL_400_Z0_2_SH            5
#define __APP_PLL_400_Z0_2(_v)           ((_v) << __APP_PLL_400_Z0_2_SH)
#define __APP_PLL_400_RSEL200500         0x00000010
#define __APP_PLL_400_ENARST             0x00000008
#define __APP_PLL_400_BYPASS             0x00000004
#define __APP_PLL_400_LRESETN            0x00000002
#define __APP_PLL_400_ENABLE             0x00000001
#define APP_PLL_212_CTL_REG              0x00014208
#define __P_212_PLL_LOCK                 0x80000000
#define __APP_PLL_212_RESET_TIMER_MK     0x000e0000
#define __APP_PLL_212_RESET_TIMER_SH     17
#define __APP_PLL_212_RESET_TIMER(_v)    ((_v) << __APP_PLL_212_RESET_TIMER_SH)
#define __APP_PLL_212_LOGIC_SOFT_RESET   0x00010000
#define __APP_PLL_212_CNTLMT0_1_MK       0x0000c000
#define __APP_PLL_212_CNTLMT0_1_SH       14
#define __APP_PLL_212_CNTLMT0_1(_v)      ((_v) << __APP_PLL_212_CNTLMT0_1_SH)
#define __APP_PLL_212_JITLMT0_1_MK       0x00003000
#define __APP_PLL_212_JITLMT0_1_SH       12
#define __APP_PLL_212_JITLMT0_1(_v)      ((_v) << __APP_PLL_212_JITLMT0_1_SH)
#define __APP_PLL_212_HREF               0x00000800
#define __APP_PLL_212_HDIV               0x00000400
#define __APP_PLL_212_P0_1_MK            0x00000300
#define __APP_PLL_212_P0_1_SH            8
#define __APP_PLL_212_P0_1(_v)           ((_v) << __APP_PLL_212_P0_1_SH)
#define __APP_PLL_212_Z0_2_MK            0x000000e0
#define __APP_PLL_212_Z0_2_SH            5
#define __APP_PLL_212_Z0_2(_v)           ((_v) << __APP_PLL_212_Z0_2_SH)
#define __APP_PLL_212_RSEL200500         0x00000010
#define __APP_PLL_212_ENARST             0x00000008
#define __APP_PLL_212_BYPASS             0x00000004
#define __APP_PLL_212_LRESETN            0x00000002
#define __APP_PLL_212_ENABLE             0x00000001
#define HOST_SEM0_REG                    0x00014230
#define __HOST_SEMAPHORE                 0x00000001
#define HOST_SEM1_REG                    0x00014234
#define HOST_SEM2_REG                    0x00014238
#define HOST_SEM3_REG                    0x0001423c
#define HOST_SEM0_INFO_REG               0x00014240
#define HOST_SEM1_INFO_REG               0x00014244
#define HOST_SEM2_INFO_REG               0x00014248
#define HOST_SEM3_INFO_REG               0x0001424c
#define HOSTFN0_LPU0_CMD_STAT            0x00019000
#define __HOSTFN0_LPU0_MBOX_INFO_MK      0xfffffffe
#define __HOSTFN0_LPU0_MBOX_INFO_SH      1
#define __HOSTFN0_LPU0_MBOX_INFO(_v)     ((_v) << __HOSTFN0_LPU0_MBOX_INFO_SH)
#define __HOSTFN0_LPU0_MBOX_CMD_STATUS   0x00000001
#define LPU0_HOSTFN0_CMD_STAT            0x00019008
#define __LPU0_HOSTFN0_MBOX_INFO_MK      0xfffffffe
#define __LPU0_HOSTFN0_MBOX_INFO_SH      1
#define __LPU0_HOSTFN0_MBOX_INFO(_v)     ((_v) << __LPU0_HOSTFN0_MBOX_INFO_SH)
#define __LPU0_HOSTFN0_MBOX_CMD_STATUS   0x00000001
#define HOSTFN1_LPU1_CMD_STAT            0x00019014
#define __HOSTFN1_LPU1_MBOX_INFO_MK      0xfffffffe
#define __HOSTFN1_LPU1_MBOX_INFO_SH      1
#define __HOSTFN1_LPU1_MBOX_INFO(_v)     ((_v) << __HOSTFN1_LPU1_MBOX_INFO_SH)
#define __HOSTFN1_LPU1_MBOX_CMD_STATUS   0x00000001
#define LPU1_HOSTFN1_CMD_STAT            0x0001901c
#define __LPU1_HOSTFN1_MBOX_INFO_MK      0xfffffffe
#define __LPU1_HOSTFN1_MBOX_INFO_SH      1
#define __LPU1_HOSTFN1_MBOX_INFO(_v)     ((_v) << __LPU1_HOSTFN1_MBOX_INFO_SH)
#define __LPU1_HOSTFN1_MBOX_CMD_STATUS   0x00000001
#define CPE_Q0_DEPTH                     0x00010014
#define CPE_Q0_PI                        0x0001001c
#define CPE_Q0_CI                        0x00010020
#define CPE_Q1_DEPTH                     0x00010034
#define CPE_Q1_PI                        0x0001003c
#define CPE_Q1_CI                        0x00010040
#define CPE_Q2_DEPTH                     0x00010054
#define CPE_Q2_PI                        0x0001005c
#define CPE_Q2_CI                        0x00010060
#define CPE_Q3_DEPTH                     0x00010074
#define CPE_Q3_PI                        0x0001007c
#define CPE_Q3_CI                        0x00010080
#define CPE_Q4_DEPTH                     0x00010094
#define CPE_Q4_PI                        0x0001009c
#define CPE_Q4_CI                        0x000100a0
#define CPE_Q5_DEPTH                     0x000100b4
#define CPE_Q5_PI                        0x000100bc
#define CPE_Q5_CI                        0x000100c0
#define CPE_Q6_DEPTH                     0x000100d4
#define CPE_Q6_PI                        0x000100dc
#define CPE_Q6_CI                        0x000100e0
#define CPE_Q7_DEPTH                     0x000100f4
#define CPE_Q7_PI                        0x000100fc
#define CPE_Q7_CI                        0x00010100
#define RME_Q0_DEPTH                     0x00011014
#define RME_Q0_PI                        0x0001101c
#define RME_Q0_CI                        0x00011020
#define RME_Q1_DEPTH                     0x00011034
#define RME_Q1_PI                        0x0001103c
#define RME_Q1_CI                        0x00011040
#define RME_Q2_DEPTH                     0x00011054
#define RME_Q2_PI                        0x0001105c
#define RME_Q2_CI                        0x00011060
#define RME_Q3_DEPTH                     0x00011074
#define RME_Q3_PI                        0x0001107c
#define RME_Q3_CI                        0x00011080
#define RME_Q4_DEPTH                     0x00011094
#define RME_Q4_PI                        0x0001109c
#define RME_Q4_CI                        0x000110a0
#define RME_Q5_DEPTH                     0x000110b4
#define RME_Q5_PI                        0x000110bc
#define RME_Q5_CI                        0x000110c0
#define RME_Q6_DEPTH                     0x000110d4
#define RME_Q6_PI                        0x000110dc
#define RME_Q6_CI                        0x000110e0
#define RME_Q7_DEPTH                     0x000110f4
#define RME_Q7_PI                        0x000110fc
#define RME_Q7_CI                        0x00011100
#define PSS_CTL_REG                      0x00018800
#define __PSS_I2C_CLK_DIV_MK             0x00030000
#define __PSS_I2C_CLK_DIV_SH             16
#define __PSS_I2C_CLK_DIV(_v)            ((_v) << __PSS_I2C_CLK_DIV_SH)
#define __PSS_LMEM_INIT_DONE             0x00001000
#define __PSS_LMEM_RESET                 0x00000200
#define __PSS_LMEM_INIT_EN               0x00000100
#define __PSS_LPU1_RESET                 0x00000002
#define __PSS_LPU0_RESET                 0x00000001
#define PSS_ERR_STATUS_REG		 0x00018810
#define __PSS_LMEM1_CORR_ERR		 0x00000800
#define __PSS_LMEM0_CORR_ERR             0x00000400
#define __PSS_LMEM1_UNCORR_ERR           0x00000200
#define __PSS_LMEM0_UNCORR_ERR           0x00000100
#define __PSS_BAL_PERR                   0x00000080
#define __PSS_DIP_IF_ERR                 0x00000040
#define __PSS_IOH_IF_ERR                 0x00000020
#define __PSS_TDS_IF_ERR                 0x00000010
#define __PSS_RDS_IF_ERR                 0x00000008
#define __PSS_SGM_IF_ERR                 0x00000004
#define __PSS_LPU1_RAM_ERR               0x00000002
#define __PSS_LPU0_RAM_ERR               0x00000001
#define ERR_SET_REG			 0x00018818
#define __PSS_ERR_STATUS_SET		 0x00000fff

/*
 * These definitions are either in error/missing in spec. Its auto-generated
 * from hard coded values in regparse.pl.
 */
#define __EMPHPOST_AT_4G_MK_FIX          0x0000001c
#define __EMPHPOST_AT_4G_SH_FIX          0x00000002
#define __EMPHPRE_AT_4G_FIX              0x00000003
#define __SFP_TXRATE_EN_FIX              0x00000100
#define __SFP_RXRATE_EN_FIX              0x00000080


/*
 * These register definitions are auto-generated from hard coded values
 * in regparse.pl.
 */
#define HOSTFN0_LPU_MBOX0_0              0x00019200
#define HOSTFN1_LPU_MBOX0_8              0x00019260
#define LPU_HOSTFN0_MBOX0_0              0x00019280
#define LPU_HOSTFN1_MBOX0_8              0x000192e0


/*
 * These register mapping definitions are auto-generated from mapping tables
 * in regparse.pl.
 */
#define BFA_IOC0_HBEAT_REG               HOST_SEM0_INFO_REG
#define BFA_IOC0_STATE_REG               HOST_SEM1_INFO_REG
#define BFA_IOC1_HBEAT_REG               HOST_SEM2_INFO_REG
#define BFA_IOC1_STATE_REG               HOST_SEM3_INFO_REG
#define BFA_FW_USE_COUNT                 HOST_SEM4_INFO_REG

#define CPE_Q_DEPTH(__n) \
	(CPE_Q0_DEPTH + (__n) * (CPE_Q1_DEPTH - CPE_Q0_DEPTH))
#define CPE_Q_PI(__n) \
	(CPE_Q0_PI + (__n) * (CPE_Q1_PI - CPE_Q0_PI))
#define CPE_Q_CI(__n) \
	(CPE_Q0_CI + (__n) * (CPE_Q1_CI - CPE_Q0_CI))
#define RME_Q_DEPTH(__n) \
	(RME_Q0_DEPTH + (__n) * (RME_Q1_DEPTH - RME_Q0_DEPTH))
#define RME_Q_PI(__n) \
	(RME_Q0_PI + (__n) * (RME_Q1_PI - RME_Q0_PI))
#define RME_Q_CI(__n) \
	(RME_Q0_CI + (__n) * (RME_Q1_CI - RME_Q0_CI))

#define CPE_Q_NUM(__fn, __q)  (((__fn) << 2) + (__q))
#define RME_Q_NUM(__fn, __q)  (((__fn) << 2) + (__q))
#define CPE_Q_MASK(__q)  ((__q) & 0x3)
#define RME_Q_MASK(__q)  ((__q) & 0x3)


/*
 * PCI MSI-X vector defines
 */
enum {
    BFA_MSIX_CPE_Q0 = 0,
    BFA_MSIX_CPE_Q1 = 1,
    BFA_MSIX_CPE_Q2 = 2,
    BFA_MSIX_CPE_Q3 = 3,
    BFA_MSIX_CPE_Q4 = 4,
    BFA_MSIX_CPE_Q5 = 5,
    BFA_MSIX_CPE_Q6 = 6,
    BFA_MSIX_CPE_Q7 = 7,
    BFA_MSIX_RME_Q0 = 8,
    BFA_MSIX_RME_Q1 = 9,
    BFA_MSIX_RME_Q2 = 10,
    BFA_MSIX_RME_Q3 = 11,
    BFA_MSIX_RME_Q4 = 12,
    BFA_MSIX_RME_Q5 = 13,
    BFA_MSIX_RME_Q6 = 14,
    BFA_MSIX_RME_Q7 = 15,
    BFA_MSIX_ERR_EMC = 16,
    BFA_MSIX_ERR_LPU0 = 17,
    BFA_MSIX_ERR_LPU1 = 18,
    BFA_MSIX_ERR_PSS = 19,
    BFA_MSIX_MBOX_LPU0 = 20,
    BFA_MSIX_MBOX_LPU1 = 21,
    BFA_MSIX_CB_MAX = 22,
};

/*
 * And corresponding host interrupt status bit field defines
 */
#define __HFN_INT_CPE_Q0                   0x00000001U
#define __HFN_INT_CPE_Q1                   0x00000002U
#define __HFN_INT_CPE_Q2                   0x00000004U
#define __HFN_INT_CPE_Q3                   0x00000008U
#define __HFN_INT_CPE_Q4                   0x00000010U
#define __HFN_INT_CPE_Q5                   0x00000020U
#define __HFN_INT_CPE_Q6                   0x00000040U
#define __HFN_INT_CPE_Q7                   0x00000080U
#define __HFN_INT_RME_Q0                   0x00000100U
#define __HFN_INT_RME_Q1                   0x00000200U
#define __HFN_INT_RME_Q2                   0x00000400U
#define __HFN_INT_RME_Q3                   0x00000800U
#define __HFN_INT_RME_Q4                   0x00001000U
#define __HFN_INT_RME_Q5                   0x00002000U
#define __HFN_INT_RME_Q6                   0x00004000U
#define __HFN_INT_RME_Q7                   0x00008000U
#define __HFN_INT_ERR_EMC                  0x00010000U
#define __HFN_INT_ERR_LPU0                 0x00020000U
#define __HFN_INT_ERR_LPU1                 0x00040000U
#define __HFN_INT_ERR_PSS                  0x00080000U
#define __HFN_INT_MBOX_LPU0                0x00100000U
#define __HFN_INT_MBOX_LPU1                0x00200000U
#define __HFN_INT_MBOX1_LPU0               0x00400000U
#define __HFN_INT_MBOX1_LPU1               0x00800000U
#define __HFN_INT_CPE_MASK                 0x000000ffU
#define __HFN_INT_RME_MASK                 0x0000ff00U


/*
 * crossbow memory map.
 */
#define PSS_SMEM_PAGE_START	0x8000
#define PSS_SMEM_PGNUM(_pg0, _ma)	((_pg0) + ((_ma) >> 15))
#define PSS_SMEM_PGOFF(_ma)	((_ma) & 0x7fff)

/*
 * End of crossbow memory map
 */


#endif /* __BFI_CBREG_H__ */

