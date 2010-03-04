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
 * bfi_ctreg.h catapult host block register definitions
 *
 * !!! Do not edit. Auto generated. !!!
 */

#ifndef __BFI_CTREG_H__
#define __BFI_CTREG_H__


#define HOSTFN0_LPU_MBOX0_0              0x00019200
#define HOSTFN1_LPU_MBOX0_8              0x00019260
#define LPU_HOSTFN0_MBOX0_0              0x00019280
#define LPU_HOSTFN1_MBOX0_8              0x000192e0
#define HOSTFN2_LPU_MBOX0_0              0x00019400
#define HOSTFN3_LPU_MBOX0_8              0x00019460
#define LPU_HOSTFN2_MBOX0_0              0x00019480
#define LPU_HOSTFN3_MBOX0_8              0x000194e0
#define HOSTFN0_INT_STATUS               0x00014000
#define __HOSTFN0_HALT_OCCURRED          0x01000000
#define __HOSTFN0_INT_STATUS_LVL_MK      0x00f00000
#define __HOSTFN0_INT_STATUS_LVL_SH      20
#define __HOSTFN0_INT_STATUS_LVL(_v)     ((_v) << __HOSTFN0_INT_STATUS_LVL_SH)
#define __HOSTFN0_INT_STATUS_P_MK        0x000f0000
#define __HOSTFN0_INT_STATUS_P_SH        16
#define __HOSTFN0_INT_STATUS_P(_v)       ((_v) << __HOSTFN0_INT_STATUS_P_SH)
#define __HOSTFN0_INT_STATUS_F           0x0000ffff
#define HOSTFN0_INT_MSK                  0x00014004
#define HOST_PAGE_NUM_FN0                0x00014008
#define __HOST_PAGE_NUM_FN               0x000001ff
#define HOST_MSIX_ERR_INDEX_FN0          0x0001400c
#define __MSIX_ERR_INDEX_FN              0x000001ff
#define HOSTFN1_INT_STATUS               0x00014100
#define __HOSTFN1_HALT_OCCURRED          0x01000000
#define __HOSTFN1_INT_STATUS_LVL_MK      0x00f00000
#define __HOSTFN1_INT_STATUS_LVL_SH      20
#define __HOSTFN1_INT_STATUS_LVL(_v)     ((_v) << __HOSTFN1_INT_STATUS_LVL_SH)
#define __HOSTFN1_INT_STATUS_P_MK        0x000f0000
#define __HOSTFN1_INT_STATUS_P_SH        16
#define __HOSTFN1_INT_STATUS_P(_v)       ((_v) << __HOSTFN1_INT_STATUS_P_SH)
#define __HOSTFN1_INT_STATUS_F           0x0000ffff
#define HOSTFN1_INT_MSK                  0x00014104
#define HOST_PAGE_NUM_FN1                0x00014108
#define HOST_MSIX_ERR_INDEX_FN1          0x0001410c
#define APP_PLL_425_CTL_REG              0x00014204
#define __P_425_PLL_LOCK                 0x80000000
#define __APP_PLL_425_SRAM_USE_100MHZ    0x00100000
#define __APP_PLL_425_RESET_TIMER_MK     0x000e0000
#define __APP_PLL_425_RESET_TIMER_SH     17
#define __APP_PLL_425_RESET_TIMER(_v)    ((_v) << __APP_PLL_425_RESET_TIMER_SH)
#define __APP_PLL_425_LOGIC_SOFT_RESET   0x00010000
#define __APP_PLL_425_CNTLMT0_1_MK       0x0000c000
#define __APP_PLL_425_CNTLMT0_1_SH       14
#define __APP_PLL_425_CNTLMT0_1(_v)      ((_v) << __APP_PLL_425_CNTLMT0_1_SH)
#define __APP_PLL_425_JITLMT0_1_MK       0x00003000
#define __APP_PLL_425_JITLMT0_1_SH       12
#define __APP_PLL_425_JITLMT0_1(_v)      ((_v) << __APP_PLL_425_JITLMT0_1_SH)
#define __APP_PLL_425_HREF               0x00000800
#define __APP_PLL_425_HDIV               0x00000400
#define __APP_PLL_425_P0_1_MK            0x00000300
#define __APP_PLL_425_P0_1_SH            8
#define __APP_PLL_425_P0_1(_v)           ((_v) << __APP_PLL_425_P0_1_SH)
#define __APP_PLL_425_Z0_2_MK            0x000000e0
#define __APP_PLL_425_Z0_2_SH            5
#define __APP_PLL_425_Z0_2(_v)           ((_v) << __APP_PLL_425_Z0_2_SH)
#define __APP_PLL_425_RSEL200500         0x00000010
#define __APP_PLL_425_ENARST             0x00000008
#define __APP_PLL_425_BYPASS             0x00000004
#define __APP_PLL_425_LRESETN            0x00000002
#define __APP_PLL_425_ENABLE             0x00000001
#define APP_PLL_312_CTL_REG              0x00014208
#define __P_312_PLL_LOCK                 0x80000000
#define __ENABLE_MAC_AHB_1               0x00800000
#define __ENABLE_MAC_AHB_0               0x00400000
#define __ENABLE_MAC_1                   0x00200000
#define __ENABLE_MAC_0                   0x00100000
#define __APP_PLL_312_RESET_TIMER_MK     0x000e0000
#define __APP_PLL_312_RESET_TIMER_SH     17
#define __APP_PLL_312_RESET_TIMER(_v)    ((_v) << __APP_PLL_312_RESET_TIMER_SH)
#define __APP_PLL_312_LOGIC_SOFT_RESET   0x00010000
#define __APP_PLL_312_CNTLMT0_1_MK       0x0000c000
#define __APP_PLL_312_CNTLMT0_1_SH       14
#define __APP_PLL_312_CNTLMT0_1(_v)      ((_v) << __APP_PLL_312_CNTLMT0_1_SH)
#define __APP_PLL_312_JITLMT0_1_MK       0x00003000
#define __APP_PLL_312_JITLMT0_1_SH       12
#define __APP_PLL_312_JITLMT0_1(_v)      ((_v) << __APP_PLL_312_JITLMT0_1_SH)
#define __APP_PLL_312_HREF               0x00000800
#define __APP_PLL_312_HDIV               0x00000400
#define __APP_PLL_312_P0_1_MK            0x00000300
#define __APP_PLL_312_P0_1_SH            8
#define __APP_PLL_312_P0_1(_v)           ((_v) << __APP_PLL_312_P0_1_SH)
#define __APP_PLL_312_Z0_2_MK            0x000000e0
#define __APP_PLL_312_Z0_2_SH            5
#define __APP_PLL_312_Z0_2(_v)           ((_v) << __APP_PLL_312_Z0_2_SH)
#define __APP_PLL_312_RSEL200500         0x00000010
#define __APP_PLL_312_ENARST             0x00000008
#define __APP_PLL_312_BYPASS             0x00000004
#define __APP_PLL_312_LRESETN            0x00000002
#define __APP_PLL_312_ENABLE             0x00000001
#define MBIST_CTL_REG                    0x00014220
#define __EDRAM_BISTR_START              0x00000004
#define __MBIST_RESET                    0x00000002
#define __MBIST_START                    0x00000001
#define MBIST_STAT_REG                   0x00014224
#define __EDRAM_BISTR_STATUS             0x00000008
#define __EDRAM_BISTR_DONE               0x00000004
#define __MEM_BIT_STATUS                 0x00000002
#define __MBIST_DONE                     0x00000001
#define HOST_SEM0_REG                    0x00014230
#define __HOST_SEMAPHORE                 0x00000001
#define HOST_SEM1_REG                    0x00014234
#define HOST_SEM2_REG                    0x00014238
#define HOST_SEM3_REG                    0x0001423c
#define HOST_SEM0_INFO_REG               0x00014240
#define HOST_SEM1_INFO_REG               0x00014244
#define HOST_SEM2_INFO_REG               0x00014248
#define HOST_SEM3_INFO_REG               0x0001424c
#define ETH_MAC_SER_REG                  0x00014288
#define __APP_EMS_CKBUFAMPIN             0x00000020
#define __APP_EMS_REFCLKSEL              0x00000010
#define __APP_EMS_CMLCKSEL               0x00000008
#define __APP_EMS_REFCKBUFEN2            0x00000004
#define __APP_EMS_REFCKBUFEN1            0x00000002
#define __APP_EMS_CHANNEL_SEL            0x00000001
#define HOSTFN2_INT_STATUS               0x00014300
#define __HOSTFN2_HALT_OCCURRED          0x01000000
#define __HOSTFN2_INT_STATUS_LVL_MK      0x00f00000
#define __HOSTFN2_INT_STATUS_LVL_SH      20
#define __HOSTFN2_INT_STATUS_LVL(_v)     ((_v) << __HOSTFN2_INT_STATUS_LVL_SH)
#define __HOSTFN2_INT_STATUS_P_MK        0x000f0000
#define __HOSTFN2_INT_STATUS_P_SH        16
#define __HOSTFN2_INT_STATUS_P(_v)       ((_v) << __HOSTFN2_INT_STATUS_P_SH)
#define __HOSTFN2_INT_STATUS_F           0x0000ffff
#define HOSTFN2_INT_MSK                  0x00014304
#define HOST_PAGE_NUM_FN2                0x00014308
#define HOST_MSIX_ERR_INDEX_FN2          0x0001430c
#define HOSTFN3_INT_STATUS               0x00014400
#define __HALT_OCCURRED                  0x01000000
#define __HOSTFN3_INT_STATUS_LVL_MK      0x00f00000
#define __HOSTFN3_INT_STATUS_LVL_SH      20
#define __HOSTFN3_INT_STATUS_LVL(_v)     ((_v) << __HOSTFN3_INT_STATUS_LVL_SH)
#define __HOSTFN3_INT_STATUS_P_MK        0x000f0000
#define __HOSTFN3_INT_STATUS_P_SH        16
#define __HOSTFN3_INT_STATUS_P(_v)       ((_v) << __HOSTFN3_INT_STATUS_P_SH)
#define __HOSTFN3_INT_STATUS_F           0x0000ffff
#define HOSTFN3_INT_MSK                  0x00014404
#define HOST_PAGE_NUM_FN3                0x00014408
#define HOST_MSIX_ERR_INDEX_FN3          0x0001440c
#define FNC_ID_REG                       0x00014600
#define __FUNCTION_NUMBER                0x00000007
#define FNC_PERS_REG                     0x00014604
#define __F3_FUNCTION_ACTIVE             0x80000000
#define __F3_FUNCTION_MODE               0x40000000
#define __F3_PORT_MAP_MK                 0x30000000
#define __F3_PORT_MAP_SH                 28
#define __F3_PORT_MAP(_v)                ((_v) << __F3_PORT_MAP_SH)
#define __F3_VM_MODE                     0x08000000
#define __F3_INTX_STATUS_MK              0x07000000
#define __F3_INTX_STATUS_SH              24
#define __F3_INTX_STATUS(_v)             ((_v) << __F3_INTX_STATUS_SH)
#define __F2_FUNCTION_ACTIVE             0x00800000
#define __F2_FUNCTION_MODE               0x00400000
#define __F2_PORT_MAP_MK                 0x00300000
#define __F2_PORT_MAP_SH                 20
#define __F2_PORT_MAP(_v)                ((_v) << __F2_PORT_MAP_SH)
#define __F2_VM_MODE                     0x00080000
#define __F2_INTX_STATUS_MK              0x00070000
#define __F2_INTX_STATUS_SH              16
#define __F2_INTX_STATUS(_v)             ((_v) << __F2_INTX_STATUS_SH)
#define __F1_FUNCTION_ACTIVE             0x00008000
#define __F1_FUNCTION_MODE               0x00004000
#define __F1_PORT_MAP_MK                 0x00003000
#define __F1_PORT_MAP_SH                 12
#define __F1_PORT_MAP(_v)                ((_v) << __F1_PORT_MAP_SH)
#define __F1_VM_MODE                     0x00000800
#define __F1_INTX_STATUS_MK              0x00000700
#define __F1_INTX_STATUS_SH              8
#define __F1_INTX_STATUS(_v)             ((_v) << __F1_INTX_STATUS_SH)
#define __F0_FUNCTION_ACTIVE             0x00000080
#define __F0_FUNCTION_MODE               0x00000040
#define __F0_PORT_MAP_MK                 0x00000030
#define __F0_PORT_MAP_SH                 4
#define __F0_PORT_MAP(_v)                ((_v) << __F0_PORT_MAP_SH)
#define __F0_VM_MODE                     0x00000008
#define __F0_INTX_STATUS                 0x00000007
enum {
    __F0_INTX_STATUS_MSIX            = 0x0,
    __F0_INTX_STATUS_INTA            = 0x1,
    __F0_INTX_STATUS_INTB            = 0x2,
    __F0_INTX_STATUS_INTC            = 0x3,
    __F0_INTX_STATUS_INTD            = 0x4,
};
#define OP_MODE                          0x0001460c
#define __APP_ETH_CLK_LOWSPEED           0x00000004
#define __GLOBAL_CORECLK_HALFSPEED       0x00000002
#define __GLOBAL_FCOE_MODE               0x00000001
#define HOST_SEM4_REG                    0x00014610
#define HOST_SEM5_REG                    0x00014614
#define HOST_SEM6_REG                    0x00014618
#define HOST_SEM7_REG                    0x0001461c
#define HOST_SEM4_INFO_REG               0x00014620
#define HOST_SEM5_INFO_REG               0x00014624
#define HOST_SEM6_INFO_REG               0x00014628
#define HOST_SEM7_INFO_REG               0x0001462c
#define HOSTFN0_LPU0_MBOX0_CMD_STAT      0x00019000
#define __HOSTFN0_LPU0_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN0_LPU0_MBOX0_INFO_SH     1
#define __HOSTFN0_LPU0_MBOX0_INFO(_v)    ((_v) << __HOSTFN0_LPU0_MBOX0_INFO_SH)
#define __HOSTFN0_LPU0_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN0_LPU1_MBOX0_CMD_STAT      0x00019004
#define __HOSTFN0_LPU1_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN0_LPU1_MBOX0_INFO_SH     1
#define __HOSTFN0_LPU1_MBOX0_INFO(_v)    ((_v) << __HOSTFN0_LPU1_MBOX0_INFO_SH)
#define __HOSTFN0_LPU1_MBOX0_CMD_STATUS  0x00000001
#define LPU0_HOSTFN0_MBOX0_CMD_STAT      0x00019008
#define __LPU0_HOSTFN0_MBOX0_INFO_MK     0xfffffffe
#define __LPU0_HOSTFN0_MBOX0_INFO_SH     1
#define __LPU0_HOSTFN0_MBOX0_INFO(_v)    ((_v) << __LPU0_HOSTFN0_MBOX0_INFO_SH)
#define __LPU0_HOSTFN0_MBOX0_CMD_STATUS  0x00000001
#define LPU1_HOSTFN0_MBOX0_CMD_STAT      0x0001900c
#define __LPU1_HOSTFN0_MBOX0_INFO_MK     0xfffffffe
#define __LPU1_HOSTFN0_MBOX0_INFO_SH     1
#define __LPU1_HOSTFN0_MBOX0_INFO(_v)    ((_v) << __LPU1_HOSTFN0_MBOX0_INFO_SH)
#define __LPU1_HOSTFN0_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN1_LPU0_MBOX0_CMD_STAT      0x00019010
#define __HOSTFN1_LPU0_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN1_LPU0_MBOX0_INFO_SH     1
#define __HOSTFN1_LPU0_MBOX0_INFO(_v)    ((_v) << __HOSTFN1_LPU0_MBOX0_INFO_SH)
#define __HOSTFN1_LPU0_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN1_LPU1_MBOX0_CMD_STAT      0x00019014
#define __HOSTFN1_LPU1_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN1_LPU1_MBOX0_INFO_SH     1
#define __HOSTFN1_LPU1_MBOX0_INFO(_v)    ((_v) << __HOSTFN1_LPU1_MBOX0_INFO_SH)
#define __HOSTFN1_LPU1_MBOX0_CMD_STATUS  0x00000001
#define LPU0_HOSTFN1_MBOX0_CMD_STAT      0x00019018
#define __LPU0_HOSTFN1_MBOX0_INFO_MK     0xfffffffe
#define __LPU0_HOSTFN1_MBOX0_INFO_SH     1
#define __LPU0_HOSTFN1_MBOX0_INFO(_v)    ((_v) << __LPU0_HOSTFN1_MBOX0_INFO_SH)
#define __LPU0_HOSTFN1_MBOX0_CMD_STATUS  0x00000001
#define LPU1_HOSTFN1_MBOX0_CMD_STAT      0x0001901c
#define __LPU1_HOSTFN1_MBOX0_INFO_MK     0xfffffffe
#define __LPU1_HOSTFN1_MBOX0_INFO_SH     1
#define __LPU1_HOSTFN1_MBOX0_INFO(_v)    ((_v) << __LPU1_HOSTFN1_MBOX0_INFO_SH)
#define __LPU1_HOSTFN1_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN2_LPU0_MBOX0_CMD_STAT      0x00019150
#define __HOSTFN2_LPU0_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN2_LPU0_MBOX0_INFO_SH     1
#define __HOSTFN2_LPU0_MBOX0_INFO(_v)    ((_v) << __HOSTFN2_LPU0_MBOX0_INFO_SH)
#define __HOSTFN2_LPU0_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN2_LPU1_MBOX0_CMD_STAT      0x00019154
#define __HOSTFN2_LPU1_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN2_LPU1_MBOX0_INFO_SH     1
#define __HOSTFN2_LPU1_MBOX0_INFO(_v)    ((_v) << __HOSTFN2_LPU1_MBOX0_INFO_SH)
#define __HOSTFN2_LPU1_MBOX0BOX0_CMD_STATUS 0x00000001
#define LPU0_HOSTFN2_MBOX0_CMD_STAT      0x00019158
#define __LPU0_HOSTFN2_MBOX0_INFO_MK     0xfffffffe
#define __LPU0_HOSTFN2_MBOX0_INFO_SH     1
#define __LPU0_HOSTFN2_MBOX0_INFO(_v)    ((_v) << __LPU0_HOSTFN2_MBOX0_INFO_SH)
#define __LPU0_HOSTFN2_MBOX0_CMD_STATUS  0x00000001
#define LPU1_HOSTFN2_MBOX0_CMD_STAT      0x0001915c
#define __LPU1_HOSTFN2_MBOX0_INFO_MK     0xfffffffe
#define __LPU1_HOSTFN2_MBOX0_INFO_SH     1
#define __LPU1_HOSTFN2_MBOX0_INFO(_v)    ((_v) << __LPU1_HOSTFN2_MBOX0_INFO_SH)
#define __LPU1_HOSTFN2_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN3_LPU0_MBOX0_CMD_STAT      0x00019160
#define __HOSTFN3_LPU0_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN3_LPU0_MBOX0_INFO_SH     1
#define __HOSTFN3_LPU0_MBOX0_INFO(_v)    ((_v) << __HOSTFN3_LPU0_MBOX0_INFO_SH)
#define __HOSTFN3_LPU0_MBOX0_CMD_STATUS  0x00000001
#define HOSTFN3_LPU1_MBOX0_CMD_STAT      0x00019164
#define __HOSTFN3_LPU1_MBOX0_INFO_MK     0xfffffffe
#define __HOSTFN3_LPU1_MBOX0_INFO_SH     1
#define __HOSTFN3_LPU1_MBOX0_INFO(_v)    ((_v) << __HOSTFN3_LPU1_MBOX0_INFO_SH)
#define __HOSTFN3_LPU1_MBOX0_CMD_STATUS  0x00000001
#define LPU0_HOSTFN3_MBOX0_CMD_STAT      0x00019168
#define __LPU0_HOSTFN3_MBOX0_INFO_MK     0xfffffffe
#define __LPU0_HOSTFN3_MBOX0_INFO_SH     1
#define __LPU0_HOSTFN3_MBOX0_INFO(_v)    ((_v) << __LPU0_HOSTFN3_MBOX0_INFO_SH)
#define __LPU0_HOSTFN3_MBOX0_CMD_STATUS  0x00000001
#define LPU1_HOSTFN3_MBOX0_CMD_STAT      0x0001916c
#define __LPU1_HOSTFN3_MBOX0_INFO_MK     0xfffffffe
#define __LPU1_HOSTFN3_MBOX0_INFO_SH     1
#define __LPU1_HOSTFN3_MBOX0_INFO(_v)    ((_v) << __LPU1_HOSTFN3_MBOX0_INFO_SH)
#define __LPU1_HOSTFN3_MBOX0_CMD_STATUS  0x00000001
#define FW_INIT_HALT_P0                  0x000191ac
#define __FW_INIT_HALT_P                 0x00000001
#define FW_INIT_HALT_P1                  0x000191bc
#define CPE_PI_PTR_Q0                    0x00038000
#define __CPE_PI_UNUSED_MK               0xffff0000
#define __CPE_PI_UNUSED_SH               16
#define __CPE_PI_UNUSED(_v)              ((_v) << __CPE_PI_UNUSED_SH)
#define __CPE_PI_PTR                     0x0000ffff
#define CPE_PI_PTR_Q1                    0x00038040
#define CPE_CI_PTR_Q0                    0x00038004
#define __CPE_CI_UNUSED_MK               0xffff0000
#define __CPE_CI_UNUSED_SH               16
#define __CPE_CI_UNUSED(_v)              ((_v) << __CPE_CI_UNUSED_SH)
#define __CPE_CI_PTR                     0x0000ffff
#define CPE_CI_PTR_Q1                    0x00038044
#define CPE_DEPTH_Q0                     0x00038008
#define __CPE_DEPTH_UNUSED_MK            0xf8000000
#define __CPE_DEPTH_UNUSED_SH            27
#define __CPE_DEPTH_UNUSED(_v)           ((_v) << __CPE_DEPTH_UNUSED_SH)
#define __CPE_MSIX_VEC_INDEX_MK          0x07ff0000
#define __CPE_MSIX_VEC_INDEX_SH          16
#define __CPE_MSIX_VEC_INDEX(_v)         ((_v) << __CPE_MSIX_VEC_INDEX_SH)
#define __CPE_DEPTH                      0x0000ffff
#define CPE_DEPTH_Q1                     0x00038048
#define CPE_QCTRL_Q0                     0x0003800c
#define __CPE_CTRL_UNUSED30_MK           0xfc000000
#define __CPE_CTRL_UNUSED30_SH           26
#define __CPE_CTRL_UNUSED30(_v)          ((_v) << __CPE_CTRL_UNUSED30_SH)
#define __CPE_FUNC_INT_CTRL_MK           0x03000000
#define __CPE_FUNC_INT_CTRL_SH           24
#define __CPE_FUNC_INT_CTRL(_v)          ((_v) << __CPE_FUNC_INT_CTRL_SH)
enum {
    __CPE_FUNC_INT_CTRL_DISABLE      = 0x0,
    __CPE_FUNC_INT_CTRL_F2NF         = 0x1,
    __CPE_FUNC_INT_CTRL_3QUART       = 0x2,
    __CPE_FUNC_INT_CTRL_HALF         = 0x3,
};
#define __CPE_CTRL_UNUSED20_MK           0x00f00000
#define __CPE_CTRL_UNUSED20_SH           20
#define __CPE_CTRL_UNUSED20(_v)          ((_v) << __CPE_CTRL_UNUSED20_SH)
#define __CPE_SCI_TH_MK                  0x000f0000
#define __CPE_SCI_TH_SH                  16
#define __CPE_SCI_TH(_v)                 ((_v) << __CPE_SCI_TH_SH)
#define __CPE_CTRL_UNUSED10_MK           0x0000c000
#define __CPE_CTRL_UNUSED10_SH           14
#define __CPE_CTRL_UNUSED10(_v)          ((_v) << __CPE_CTRL_UNUSED10_SH)
#define __CPE_ACK_PENDING                0x00002000
#define __CPE_CTRL_UNUSED40_MK           0x00001c00
#define __CPE_CTRL_UNUSED40_SH           10
#define __CPE_CTRL_UNUSED40(_v)          ((_v) << __CPE_CTRL_UNUSED40_SH)
#define __CPE_PCIEID_MK                  0x00000300
#define __CPE_PCIEID_SH                  8
#define __CPE_PCIEID(_v)                 ((_v) << __CPE_PCIEID_SH)
#define __CPE_CTRL_UNUSED00_MK           0x000000fe
#define __CPE_CTRL_UNUSED00_SH           1
#define __CPE_CTRL_UNUSED00(_v)          ((_v) << __CPE_CTRL_UNUSED00_SH)
#define __CPE_ESIZE                      0x00000001
#define CPE_QCTRL_Q1                     0x0003804c
#define __CPE_CTRL_UNUSED31_MK           0xfc000000
#define __CPE_CTRL_UNUSED31_SH           26
#define __CPE_CTRL_UNUSED31(_v)          ((_v) << __CPE_CTRL_UNUSED31_SH)
#define __CPE_CTRL_UNUSED21_MK           0x00f00000
#define __CPE_CTRL_UNUSED21_SH           20
#define __CPE_CTRL_UNUSED21(_v)          ((_v) << __CPE_CTRL_UNUSED21_SH)
#define __CPE_CTRL_UNUSED11_MK           0x0000c000
#define __CPE_CTRL_UNUSED11_SH           14
#define __CPE_CTRL_UNUSED11(_v)          ((_v) << __CPE_CTRL_UNUSED11_SH)
#define __CPE_CTRL_UNUSED41_MK           0x00001c00
#define __CPE_CTRL_UNUSED41_SH           10
#define __CPE_CTRL_UNUSED41(_v)          ((_v) << __CPE_CTRL_UNUSED41_SH)
#define __CPE_CTRL_UNUSED01_MK           0x000000fe
#define __CPE_CTRL_UNUSED01_SH           1
#define __CPE_CTRL_UNUSED01(_v)          ((_v) << __CPE_CTRL_UNUSED01_SH)
#define RME_PI_PTR_Q0                    0x00038020
#define __LATENCY_TIME_STAMP_MK          0xffff0000
#define __LATENCY_TIME_STAMP_SH          16
#define __LATENCY_TIME_STAMP(_v)         ((_v) << __LATENCY_TIME_STAMP_SH)
#define __RME_PI_PTR                     0x0000ffff
#define RME_PI_PTR_Q1                    0x00038060
#define RME_CI_PTR_Q0                    0x00038024
#define __DELAY_TIME_STAMP_MK            0xffff0000
#define __DELAY_TIME_STAMP_SH            16
#define __DELAY_TIME_STAMP(_v)           ((_v) << __DELAY_TIME_STAMP_SH)
#define __RME_CI_PTR                     0x0000ffff
#define RME_CI_PTR_Q1                    0x00038064
#define RME_DEPTH_Q0                     0x00038028
#define __RME_DEPTH_UNUSED_MK            0xf8000000
#define __RME_DEPTH_UNUSED_SH            27
#define __RME_DEPTH_UNUSED(_v)           ((_v) << __RME_DEPTH_UNUSED_SH)
#define __RME_MSIX_VEC_INDEX_MK          0x07ff0000
#define __RME_MSIX_VEC_INDEX_SH          16
#define __RME_MSIX_VEC_INDEX(_v)         ((_v) << __RME_MSIX_VEC_INDEX_SH)
#define __RME_DEPTH                      0x0000ffff
#define RME_DEPTH_Q1                     0x00038068
#define RME_QCTRL_Q0                     0x0003802c
#define __RME_INT_LATENCY_TIMER_MK       0xff000000
#define __RME_INT_LATENCY_TIMER_SH       24
#define __RME_INT_LATENCY_TIMER(_v)      ((_v) << __RME_INT_LATENCY_TIMER_SH)
#define __RME_INT_DELAY_TIMER_MK         0x00ff0000
#define __RME_INT_DELAY_TIMER_SH         16
#define __RME_INT_DELAY_TIMER(_v)        ((_v) << __RME_INT_DELAY_TIMER_SH)
#define __RME_INT_DELAY_DISABLE          0x00008000
#define __RME_DLY_DELAY_DISABLE          0x00004000
#define __RME_ACK_PENDING                0x00002000
#define __RME_FULL_INTERRUPT_DISABLE     0x00001000
#define __RME_CTRL_UNUSED10_MK           0x00000c00
#define __RME_CTRL_UNUSED10_SH           10
#define __RME_CTRL_UNUSED10(_v)          ((_v) << __RME_CTRL_UNUSED10_SH)
#define __RME_PCIEID_MK                  0x00000300
#define __RME_PCIEID_SH                  8
#define __RME_PCIEID(_v)                 ((_v) << __RME_PCIEID_SH)
#define __RME_CTRL_UNUSED00_MK           0x000000fe
#define __RME_CTRL_UNUSED00_SH           1
#define __RME_CTRL_UNUSED00(_v)          ((_v) << __RME_CTRL_UNUSED00_SH)
#define __RME_ESIZE                      0x00000001
#define RME_QCTRL_Q1                     0x0003806c
#define __RME_CTRL_UNUSED11_MK           0x00000c00
#define __RME_CTRL_UNUSED11_SH           10
#define __RME_CTRL_UNUSED11(_v)          ((_v) << __RME_CTRL_UNUSED11_SH)
#define __RME_CTRL_UNUSED01_MK           0x000000fe
#define __RME_CTRL_UNUSED01_SH           1
#define __RME_CTRL_UNUSED01(_v)          ((_v) << __RME_CTRL_UNUSED01_SH)
#define PSS_CTL_REG                      0x00018800
#define __PSS_I2C_CLK_DIV_MK             0x007f0000
#define __PSS_I2C_CLK_DIV_SH             16
#define __PSS_I2C_CLK_DIV(_v)            ((_v) << __PSS_I2C_CLK_DIV_SH)
#define __PSS_LMEM_INIT_DONE             0x00001000
#define __PSS_LMEM_RESET                 0x00000200
#define __PSS_LMEM_INIT_EN               0x00000100
#define __PSS_LPU1_RESET                 0x00000002
#define __PSS_LPU0_RESET                 0x00000001
#define HQM_QSET0_RXQ_DRBL_P0            0x00038000
#define __RXQ0_ADD_VECTORS_P             0x80000000
#define __RXQ0_STOP_P                    0x40000000
#define __RXQ0_PRD_PTR_P                 0x0000ffff
#define HQM_QSET1_RXQ_DRBL_P0            0x00038080
#define __RXQ1_ADD_VECTORS_P             0x80000000
#define __RXQ1_STOP_P                    0x40000000
#define __RXQ1_PRD_PTR_P                 0x0000ffff
#define HQM_QSET0_RXQ_DRBL_P1            0x0003c000
#define HQM_QSET1_RXQ_DRBL_P1            0x0003c080
#define HQM_QSET0_TXQ_DRBL_P0            0x00038020
#define __TXQ0_ADD_VECTORS_P             0x80000000
#define __TXQ0_STOP_P                    0x40000000
#define __TXQ0_PRD_PTR_P                 0x0000ffff
#define HQM_QSET1_TXQ_DRBL_P0            0x000380a0
#define __TXQ1_ADD_VECTORS_P             0x80000000
#define __TXQ1_STOP_P                    0x40000000
#define __TXQ1_PRD_PTR_P                 0x0000ffff
#define HQM_QSET0_TXQ_DRBL_P1            0x0003c020
#define HQM_QSET1_TXQ_DRBL_P1            0x0003c0a0
#define HQM_QSET0_IB_DRBL_1_P0           0x00038040
#define __IB1_0_ACK_P                    0x80000000
#define __IB1_0_DISABLE_P                0x40000000
#define __IB1_0_NUM_OF_ACKED_EVENTS_P    0x0000ffff
#define HQM_QSET1_IB_DRBL_1_P0           0x000380c0
#define __IB1_1_ACK_P                    0x80000000
#define __IB1_1_DISABLE_P                0x40000000
#define __IB1_1_NUM_OF_ACKED_EVENTS_P    0x0000ffff
#define HQM_QSET0_IB_DRBL_1_P1           0x0003c040
#define HQM_QSET1_IB_DRBL_1_P1           0x0003c0c0
#define HQM_QSET0_IB_DRBL_2_P0           0x00038060
#define __IB2_0_ACK_P                    0x80000000
#define __IB2_0_DISABLE_P                0x40000000
#define __IB2_0_NUM_OF_ACKED_EVENTS_P    0x0000ffff
#define HQM_QSET1_IB_DRBL_2_P0           0x000380e0
#define __IB2_1_ACK_P                    0x80000000
#define __IB2_1_DISABLE_P                0x40000000
#define __IB2_1_NUM_OF_ACKED_EVENTS_P    0x0000ffff
#define HQM_QSET0_IB_DRBL_2_P1           0x0003c060
#define HQM_QSET1_IB_DRBL_2_P1           0x0003c0e0


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


/*
 * These register mapping definitions are auto-generated from mapping tables
 * in regparse.pl.
 */
#define BFA_IOC0_HBEAT_REG               HOST_SEM0_INFO_REG
#define BFA_IOC0_STATE_REG               HOST_SEM1_INFO_REG
#define BFA_IOC1_HBEAT_REG               HOST_SEM2_INFO_REG
#define BFA_IOC1_STATE_REG               HOST_SEM3_INFO_REG
#define BFA_FW_USE_COUNT                 HOST_SEM4_INFO_REG

#define CPE_DEPTH_Q(__n) \
	(CPE_DEPTH_Q0 + (__n) * (CPE_DEPTH_Q1 - CPE_DEPTH_Q0))
#define CPE_QCTRL_Q(__n) \
	(CPE_QCTRL_Q0 + (__n) * (CPE_QCTRL_Q1 - CPE_QCTRL_Q0))
#define CPE_PI_PTR_Q(__n) \
	(CPE_PI_PTR_Q0 + (__n) * (CPE_PI_PTR_Q1 - CPE_PI_PTR_Q0))
#define CPE_CI_PTR_Q(__n) \
	(CPE_CI_PTR_Q0 + (__n) * (CPE_CI_PTR_Q1 - CPE_CI_PTR_Q0))
#define RME_DEPTH_Q(__n) \
	(RME_DEPTH_Q0 + (__n) * (RME_DEPTH_Q1 - RME_DEPTH_Q0))
#define RME_QCTRL_Q(__n) \
	(RME_QCTRL_Q0 + (__n) * (RME_QCTRL_Q1 - RME_QCTRL_Q0))
#define RME_PI_PTR_Q(__n) \
	(RME_PI_PTR_Q0 + (__n) * (RME_PI_PTR_Q1 - RME_PI_PTR_Q0))
#define RME_CI_PTR_Q(__n) \
	(RME_CI_PTR_Q0 + (__n) * (RME_CI_PTR_Q1 - RME_CI_PTR_Q0))
#define HQM_QSET_RXQ_DRBL_P0(__n) \
	(HQM_QSET0_RXQ_DRBL_P0 + (__n) * (HQM_QSET1_RXQ_DRBL_P0 - \
	HQM_QSET0_RXQ_DRBL_P0))
#define HQM_QSET_TXQ_DRBL_P0(__n) \
	(HQM_QSET0_TXQ_DRBL_P0 + (__n) * (HQM_QSET1_TXQ_DRBL_P0 - \
	HQM_QSET0_TXQ_DRBL_P0))
#define HQM_QSET_IB_DRBL_1_P0(__n) \
	(HQM_QSET0_IB_DRBL_1_P0 + (__n) * (HQM_QSET1_IB_DRBL_1_P0 - \
	HQM_QSET0_IB_DRBL_1_P0))
#define HQM_QSET_IB_DRBL_2_P0(__n) \
	(HQM_QSET0_IB_DRBL_2_P0 + (__n) * (HQM_QSET1_IB_DRBL_2_P0 - \
	HQM_QSET0_IB_DRBL_2_P0))
#define HQM_QSET_RXQ_DRBL_P1(__n) \
	(HQM_QSET0_RXQ_DRBL_P1 + (__n) * (HQM_QSET1_RXQ_DRBL_P1 - \
	HQM_QSET0_RXQ_DRBL_P1))
#define HQM_QSET_TXQ_DRBL_P1(__n) \
	(HQM_QSET0_TXQ_DRBL_P1 + (__n) * (HQM_QSET1_TXQ_DRBL_P1 - \
	HQM_QSET0_TXQ_DRBL_P1))
#define HQM_QSET_IB_DRBL_1_P1(__n) \
	(HQM_QSET0_IB_DRBL_1_P1 + (__n) * (HQM_QSET1_IB_DRBL_1_P1 - \
	HQM_QSET0_IB_DRBL_1_P1))
#define HQM_QSET_IB_DRBL_2_P1(__n) \
	(HQM_QSET0_IB_DRBL_2_P1 + (__n) * (HQM_QSET1_IB_DRBL_2_P1 - \
	HQM_QSET0_IB_DRBL_2_P1))

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
    BFA_MSIX_RME_Q0 = 4,
    BFA_MSIX_RME_Q1 = 5,
    BFA_MSIX_RME_Q2 = 6,
    BFA_MSIX_RME_Q3 = 7,
    BFA_MSIX_LPU_ERR = 8,
    BFA_MSIX_CT_MAX = 9,
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
#define __HFN_INT_LL_HALT		   0x01000000U
#define __HFN_INT_CPE_MASK                 0x000000ffU
#define __HFN_INT_RME_MASK                 0x0000ff00U


/*
 * catapult memory map.
 */
#define LL_PGN_HQM0                      0x0096
#define LL_PGN_HQM1                      0x0097
#define PSS_SMEM_PAGE_START	0x8000
#define PSS_SMEM_PGNUM(_pg0, _ma)	((_pg0) + ((_ma) >> 15))
#define PSS_SMEM_PGOFF(_ma)	((_ma) & 0x7fff)

/*
 * End of catapult memory map
 */


#endif /* __BFI_CTREG_H__ */

