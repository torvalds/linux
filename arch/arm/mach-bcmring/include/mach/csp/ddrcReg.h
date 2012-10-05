/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    ddrcReg.h
*
*  @brief   Register definitions for BCMRING DDR2 Controller and PHY
*
*/
/****************************************************************************/

#ifndef DDRC_REG_H
#define DDRC_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Include Files ---------------------------------------------------- */

#include <mach/csp/reg.h>
#include <linux/types.h>

#include <mach/csp/mm_io.h>

/* ---- Public Constants and Types --------------------------------------- */

/*********************************************************************/
/* DDR2 Controller (ARM PL341) register definitions */
/*********************************************************************/

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* ARM PL341 DDR2 configuration registers, offset 0x000 */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

	typedef struct {
		uint32_t memcStatus;
		uint32_t memcCmd;
		uint32_t directCmd;
		uint32_t memoryCfg;
		uint32_t refreshPrd;
		uint32_t casLatency;
		uint32_t writeLatency;
		uint32_t tMrd;
		uint32_t tRas;
		uint32_t tRc;
		uint32_t tRcd;
		uint32_t tRfc;
		uint32_t tRp;
		uint32_t tRrd;
		uint32_t tWr;
		uint32_t tWtr;
		uint32_t tXp;
		uint32_t tXsr;
		uint32_t tEsr;
		uint32_t memoryCfg2;
		uint32_t memoryCfg3;
		uint32_t tFaw;
	} ddrcReg_CTLR_MEMC_REG_t;

#define ddrcReg_CTLR_MEMC_REG_OFFSET                    0x0000
#define ddrcReg_CTLR_MEMC_REGP                          ((volatile ddrcReg_CTLR_MEMC_REG_t *)  (MM_IO_BASE_DDRC + ddrcReg_CTLR_MEMC_REG_OFFSET))

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_MEMC_STATUS_BANKS_MASK             (0x3 << 12)
#define ddrcReg_CTLR_MEMC_STATUS_BANKS_4                (0x0 << 12)
#define ddrcReg_CTLR_MEMC_STATUS_BANKS_8                (0x3 << 12)

#define ddrcReg_CTLR_MEMC_STATUS_MONITORS_MASK          (0x3 << 10)
#define ddrcReg_CTLR_MEMC_STATUS_MONITORS_0             (0x0 << 10)
#define ddrcReg_CTLR_MEMC_STATUS_MONITORS_1             (0x1 << 10)
#define ddrcReg_CTLR_MEMC_STATUS_MONITORS_2             (0x2 << 10)
#define ddrcReg_CTLR_MEMC_STATUS_MONITORS_4             (0x3 << 10)

#define ddrcReg_CTLR_MEMC_STATUS_CHIPS_MASK             (0x3 << 7)
#define ddrcReg_CTLR_MEMC_STATUS_CHIPS_1                (0x0 << 7)
#define ddrcReg_CTLR_MEMC_STATUS_CHIPS_2                (0x1 << 7)
#define ddrcReg_CTLR_MEMC_STATUS_CHIPS_3                (0x2 << 7)
#define ddrcReg_CTLR_MEMC_STATUS_CHIPS_4                (0x3 << 7)

#define ddrcReg_CTLR_MEMC_STATUS_TYPE_MASK              (0x7 << 4)
#define ddrcReg_CTLR_MEMC_STATUS_TYPE_DDR2              (0x5 << 4)

#define ddrcReg_CTLR_MEMC_STATUS_WIDTH_MASK             (0x3 << 2)
#define ddrcReg_CTLR_MEMC_STATUS_WIDTH_16               (0x0 << 2)
#define ddrcReg_CTLR_MEMC_STATUS_WIDTH_32               (0x1 << 2)
#define ddrcReg_CTLR_MEMC_STATUS_WIDTH_64               (0x2 << 2)
#define ddrcReg_CTLR_MEMC_STATUS_WIDTH_128              (0x3 << 2)

#define ddrcReg_CTLR_MEMC_STATUS_STATE_MASK             (0x3 << 0)
#define ddrcReg_CTLR_MEMC_STATUS_STATE_CONFIG           (0x0 << 0)
#define ddrcReg_CTLR_MEMC_STATUS_STATE_READY            (0x1 << 0)
#define ddrcReg_CTLR_MEMC_STATUS_STATE_PAUSED           (0x2 << 0)
#define ddrcReg_CTLR_MEMC_STATUS_STATE_LOWPWR           (0x3 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_MEMC_CMD_MASK                      (0x7 << 0)
#define ddrcReg_CTLR_MEMC_CMD_GO                        (0x0 << 0)
#define ddrcReg_CTLR_MEMC_CMD_SLEEP                     (0x1 << 0)
#define ddrcReg_CTLR_MEMC_CMD_WAKEUP                    (0x2 << 0)
#define ddrcReg_CTLR_MEMC_CMD_PAUSE                     (0x3 << 0)
#define ddrcReg_CTLR_MEMC_CMD_CONFIGURE                 (0x4 << 0)
#define ddrcReg_CTLR_MEMC_CMD_ACTIVE_PAUSE              (0x7 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_DIRECT_CMD_CHIP_SHIFT              20
#define ddrcReg_CTLR_DIRECT_CMD_CHIP_MASK               (0x3 << ddrcReg_CTLR_DIRECT_CMD_CHIP_SHIFT)

#define ddrcReg_CTLR_DIRECT_CMD_TYPE_PRECHARGEALL       (0x0 << 18)
#define ddrcReg_CTLR_DIRECT_CMD_TYPE_AUTOREFRESH        (0x1 << 18)
#define ddrcReg_CTLR_DIRECT_CMD_TYPE_MODEREG            (0x2 << 18)
#define ddrcReg_CTLR_DIRECT_CMD_TYPE_NOP                (0x3 << 18)

#define ddrcReg_CTLR_DIRECT_CMD_BANK_SHIFT              16
#define ddrcReg_CTLR_DIRECT_CMD_BANK_MASK               (0x3 << ddrcReg_CTLR_DIRECT_CMD_BANK_SHIFT)

#define ddrcReg_CTLR_DIRECT_CMD_ADDR_SHIFT              0
#define ddrcReg_CTLR_DIRECT_CMD_ADDR_MASK               (0x1ffff << ddrcReg_CTLR_DIRECT_CMD_ADDR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_MEMORY_CFG_CHIP_CNT_MASK           (0x3 << 21)
#define ddrcReg_CTLR_MEMORY_CFG_CHIP_CNT_1              (0x0 << 21)
#define ddrcReg_CTLR_MEMORY_CFG_CHIP_CNT_2              (0x1 << 21)
#define ddrcReg_CTLR_MEMORY_CFG_CHIP_CNT_3              (0x2 << 21)
#define ddrcReg_CTLR_MEMORY_CFG_CHIP_CNT_4              (0x3 << 21)

#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_MASK           (0x7 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_3_0            (0x0 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_4_1            (0x1 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_5_2            (0x2 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_6_3            (0x3 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_7_4            (0x4 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_8_5            (0x5 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_9_6            (0x6 << 18)
#define ddrcReg_CTLR_MEMORY_CFG_QOS_ARID_10_7           (0x7 << 18)

#define ddrcReg_CTLR_MEMORY_CFG_BURST_LEN_MASK          (0x7 << 15)
#define ddrcReg_CTLR_MEMORY_CFG_BURST_LEN_4             (0x2 << 15)
#define ddrcReg_CTLR_MEMORY_CFG_BURST_LEN_8             (0x3 << 15)	/* @note Not supported in PL341 */

#define ddrcReg_CTLR_MEMORY_CFG_PWRDOWN_ENABLE          (0x1 << 13)

#define ddrcReg_CTLR_MEMORY_CFG_PWRDOWN_CYCLES_SHIFT    7
#define ddrcReg_CTLR_MEMORY_CFG_PWRDOWN_CYCLES_MASK     (0x3f << ddrcReg_CTLR_MEMORY_CFG_PWRDOWN_CYCLES_SHIFT)

#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_MASK       (0x7 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_11         (0x0 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_12         (0x1 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_13         (0x2 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_14         (0x3 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_15         (0x4 << 3)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_ROW_BITS_16         (0x5 << 3)

#define ddrcReg_CTLR_MEMORY_CFG_AXI_COL_BITS_MASK       (0x7 << 0)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_COL_BITS_9          (0x1 << 0)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_COL_BITS_10         (0x2 << 0)
#define ddrcReg_CTLR_MEMORY_CFG_AXI_COL_BITS_11         (0x3 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_REFRESH_PRD_SHIFT                  0
#define ddrcReg_CTLR_REFRESH_PRD_MASK                   (0x7fff << ddrcReg_CTLR_REFRESH_PRD_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_CAS_LATENCY_SHIFT                  1
#define ddrcReg_CTLR_CAS_LATENCY_MASK                   (0x7 << ddrcReg_CTLR_CAS_LATENCY_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_WRITE_LATENCY_SHIFT                0
#define ddrcReg_CTLR_WRITE_LATENCY_MASK                 (0x7 << ddrcReg_CTLR_WRITE_LATENCY_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_MRD_SHIFT                        0
#define ddrcReg_CTLR_T_MRD_MASK                         (0x7f << ddrcReg_CTLR_T_MRD_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RAS_SHIFT                        0
#define ddrcReg_CTLR_T_RAS_MASK                         (0x1f << ddrcReg_CTLR_T_RAS_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RC_SHIFT                         0
#define ddrcReg_CTLR_T_RC_MASK                          (0x1f << ddrcReg_CTLR_T_RC_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RCD_SCHEDULE_DELAY_SHIFT         8
#define ddrcReg_CTLR_T_RCD_SCHEDULE_DELAY_MASK          (0x7 << ddrcReg_CTLR_T_RCD_SCHEDULE_DELAY_SHIFT)

#define ddrcReg_CTLR_T_RCD_SHIFT                        0
#define ddrcReg_CTLR_T_RCD_MASK                         (0x7 << ddrcReg_CTLR_T_RCD_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RFC_SCHEDULE_DELAY_SHIFT         8
#define ddrcReg_CTLR_T_RFC_SCHEDULE_DELAY_MASK          (0x7f << ddrcReg_CTLR_T_RFC_SCHEDULE_DELAY_SHIFT)

#define ddrcReg_CTLR_T_RFC_SHIFT                        0
#define ddrcReg_CTLR_T_RFC_MASK                         (0x7f << ddrcReg_CTLR_T_RFC_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RP_SCHEDULE_DELAY_SHIFT          8
#define ddrcReg_CTLR_T_RP_SCHEDULE_DELAY_MASK           (0x7 << ddrcReg_CTLR_T_RP_SCHEDULE_DELAY_SHIFT)

#define ddrcReg_CTLR_T_RP_SHIFT                         0
#define ddrcReg_CTLR_T_RP_MASK                          (0xf << ddrcReg_CTLR_T_RP_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_RRD_SHIFT                        0
#define ddrcReg_CTLR_T_RRD_MASK                         (0xf << ddrcReg_CTLR_T_RRD_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_WR_SHIFT                         0
#define ddrcReg_CTLR_T_WR_MASK                          (0x7 << ddrcReg_CTLR_T_WR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_WTR_SHIFT                        0
#define ddrcReg_CTLR_T_WTR_MASK                         (0x7 << ddrcReg_CTLR_T_WTR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_XP_SHIFT                         0
#define ddrcReg_CTLR_T_XP_MASK                          (0xff << ddrcReg_CTLR_T_XP_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_XSR_SHIFT                        0
#define ddrcReg_CTLR_T_XSR_MASK                         (0xff << ddrcReg_CTLR_T_XSR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_ESR_SHIFT                        0
#define ddrcReg_CTLR_T_ESR_MASK                         (0xff << ddrcReg_CTLR_T_ESR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_MEMORY_CFG2_WIDTH_MASK             (0x3 << 6)
#define ddrcReg_CTLR_MEMORY_CFG2_WIDTH_16BITS           (0 << 6)
#define ddrcReg_CTLR_MEMORY_CFG2_WIDTH_32BITS           (1 << 6)
#define ddrcReg_CTLR_MEMORY_CFG2_WIDTH_64BITS           (2 << 6)

#define ddrcReg_CTLR_MEMORY_CFG2_AXI_BANK_BITS_MASK     (0x3 << 4)
#define ddrcReg_CTLR_MEMORY_CFG2_AXI_BANK_BITS_2        (0 << 4)
#define ddrcReg_CTLR_MEMORY_CFG2_AXI_BANK_BITS_3        (3 << 4)

#define ddrcReg_CTLR_MEMORY_CFG2_CKE_INIT_STATE_LOW     (0 << 3)
#define ddrcReg_CTLR_MEMORY_CFG2_CKE_INIT_STATE_HIGH    (1 << 3)

#define ddrcReg_CTLR_MEMORY_CFG2_DQM_INIT_STATE_LOW     (0 << 2)
#define ddrcReg_CTLR_MEMORY_CFG2_DQM_INIT_STATE_HIGH    (1 << 2)

#define ddrcReg_CTLR_MEMORY_CFG2_CLK_MASK               (0x3 << 0)
#define ddrcReg_CTLR_MEMORY_CFG2_CLK_ASYNC              (0 << 0)
#define ddrcReg_CTLR_MEMORY_CFG2_CLK_SYNC_A_LE_M        (1 << 0)
#define ddrcReg_CTLR_MEMORY_CFG2_CLK_SYNC_A_GT_M        (3 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_MEMORY_CFG3_REFRESH_TO_SHIFT       0
#define ddrcReg_CTLR_MEMORY_CFG3_REFRESH_TO_MASK        (0x7 << ddrcReg_CTLR_MEMORY_CFG3_REFRESH_TO_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_T_FAW_SCHEDULE_DELAY_SHIFT         8
#define ddrcReg_CTLR_T_FAW_SCHEDULE_DELAY_MASK          (0x1f << ddrcReg_CTLR_T_FAW_SCHEDULE_DELAY_SHIFT)

#define ddrcReg_CTLR_T_FAW_PERIOD_SHIFT                 0
#define ddrcReg_CTLR_T_FAW_PERIOD_MASK                  (0x1f << ddrcReg_CTLR_T_FAW_PERIOD_SHIFT)

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* ARM PL341 AXI ID QOS configuration registers, offset 0x100 */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

#define ddrcReg_CTLR_QOS_CNT                            16
#define ddrcReg_CTLR_QOS_MAX                            (ddrcReg_CTLR_QOS_CNT - 1)

	typedef struct {
		uint32_t cfg[ddrcReg_CTLR_QOS_CNT];
	} ddrcReg_CTLR_QOS_REG_t;

#define ddrcReg_CTLR_QOS_REG_OFFSET                     0x100
#define ddrcReg_CTLR_QOS_REGP                           ((volatile ddrcReg_CTLR_QOS_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_CTLR_QOS_REG_OFFSET))

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_QOS_CFG_MAX_SHIFT                  2
#define ddrcReg_CTLR_QOS_CFG_MAX_MASK                   (0xff << ddrcReg_CTLR_QOS_CFG_MAX_SHIFT)

#define ddrcReg_CTLR_QOS_CFG_MIN_SHIFT                  1
#define ddrcReg_CTLR_QOS_CFG_MIN_MASK                   (1 << ddrcReg_CTLR_QOS_CFG_MIN_SHIFT)

#define ddrcReg_CTLR_QOS_CFG_ENABLE                     (1 << 0)

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* ARM PL341 Memory chip configuration registers, offset 0x200 */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

#define ddrcReg_CTLR_CHIP_CNT                           4
#define ddrcReg_CTLR_CHIP_MAX                           (ddrcReg_CTLR_CHIP_CNT - 1)

	typedef struct {
		uint32_t cfg[ddrcReg_CTLR_CHIP_CNT];
	} ddrcReg_CTLR_CHIP_REG_t;

#define ddrcReg_CTLR_CHIP_REG_OFFSET                    0x200
#define ddrcReg_CTLR_CHIP_REGP                          ((volatile ddrcReg_CTLR_CHIP_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_CTLR_CHIP_REG_OFFSET))

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_CHIP_CFG_MEM_ORG_MASK              (1 << 16)
#define ddrcReg_CTLR_CHIP_CFG_MEM_ORG_ROW_BANK_COL      (0 << 16)
#define ddrcReg_CTLR_CHIP_CFG_MEM_ORG_BANK_ROW_COL      (1 << 16)

#define ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MATCH_SHIFT      8
#define ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MATCH_MASK       (0xff << ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MATCH_SHIFT)

#define ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MASK_SHIFT       0
#define ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MASK_MASK        (0xff << ddrcReg_CTLR_CHIP_CFG_AXI_ADDR_MASK_SHIFT)

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* ARM PL341 User configuration registers, offset 0x300 */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

#define ddrcReg_CTLR_USER_OUTPUT_CNT                    2

	typedef struct {
		uint32_t input;
		uint32_t output[ddrcReg_CTLR_USER_OUTPUT_CNT];
		uint32_t feature;
	} ddrcReg_CTLR_USER_REG_t;

#define ddrcReg_CTLR_USER_REG_OFFSET                    0x300
#define ddrcReg_CTLR_USER_REGP                          ((volatile ddrcReg_CTLR_USER_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_CTLR_USER_REG_OFFSET))

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_USER_INPUT_STATUS_SHIFT            0
#define ddrcReg_CTLR_USER_INPUT_STATUS_MASK             (0xff << ddrcReg_CTLR_USER_INPUT_STATUS_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_USER_OUTPUT_CFG_SHIFT              0
#define ddrcReg_CTLR_USER_OUTPUT_CFG_MASK               (0xff << ddrcReg_CTLR_USER_OUTPUT_CFG_SHIFT)

#define ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_SHIFT      1
#define ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_MASK       (1 << ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_BP134      (0 << ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_PL301      (1 << ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_REGISTERED ddrcReg_CTLR_USER_OUTPUT_0_CFG_SYNC_BRIDGE_PL301

/* ----------------------------------------------------- */

#define ddrcReg_CTLR_FEATURE_WRITE_BLOCK_DISABLE        (1 << 2)
#define ddrcReg_CTLR_FEATURE_EARLY_BURST_RSP_DISABLE    (1 << 0)

/*********************************************************************/
/* Broadcom DDR23 PHY register definitions */
/*********************************************************************/

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* Broadcom DDR23 PHY Address and Control register definitions */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

	typedef struct {
		uint32_t revision;
		uint32_t pmCtl;
		 REG32_RSVD(0x0008, 0x0010);
		uint32_t pllStatus;
		uint32_t pllCfg;
		uint32_t pllPreDiv;
		uint32_t pllDiv;
		uint32_t pllCtl1;
		uint32_t pllCtl2;
		uint32_t ssCtl;
		uint32_t ssCfg;
		uint32_t vdlStatic;
		uint32_t vdlDynamic;
		uint32_t padIdle;
		uint32_t pvtComp;
		uint32_t padDrive;
		uint32_t clkRgltrCtl;
	} ddrcReg_PHY_ADDR_CTL_REG_t;

#define ddrcReg_PHY_ADDR_CTL_REG_OFFSET                 0x0400
#define ddrcReg_PHY_ADDR_CTL_REGP                       ((volatile ddrcReg_PHY_ADDR_CTL_REG_t __iomem*) (MM_IO_BASE_DDRC + ddrcReg_PHY_ADDR_CTL_REG_OFFSET))

/* @todo These SS definitions are duplicates of ones below */

#define ddrcReg_PHY_ADDR_SS_CTRL_ENABLE                 0x00000001
#define ddrcReg_PHY_ADDR_SS_CFG_CYCLE_PER_TICK_MASK     0xFFFF0000
#define ddrcReg_PHY_ADDR_SS_CFG_CYCLE_PER_TICK_SHIFT    16
#define ddrcReg_PHY_ADDR_SS_CFG_MIN_CYCLE_PER_TICK      10	/* Higher the value, lower the SS modulation frequency */
#define ddrcReg_PHY_ADDR_SS_CFG_NDIV_AMPLITUDE_MASK     0x0000FFFF
#define ddrcReg_PHY_ADDR_SS_CFG_NDIV_AMPLITUDE_SHIFT    0

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_REVISION_MAJOR_SHIFT       8
#define ddrcReg_PHY_ADDR_CTL_REVISION_MAJOR_MASK        (0xff << ddrcReg_PHY_ADDR_CTL_REVISION_MAJOR_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_REVISION_MINOR_SHIFT       0
#define ddrcReg_PHY_ADDR_CTL_REVISION_MINOR_MASK        (0xff << ddrcReg_PHY_ADDR_CTL_REVISION_MINOR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_CLK_PM_CTL_DDR_CLK_DISABLE (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_STATUS_LOCKED          (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_DIV2_CLK_RESET     (1 << 31)

#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_TEST_SEL_SHIFT     17
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_TEST_SEL_MASK      (0x1f << ddrcReg_PHY_ADDR_CTL_PLL_CFG_TEST_SEL_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_TEST_ENABLE        (1 << 16)

#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_BGAP_ADJ_SHIFT     12
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_BGAP_ADJ_MASK      (0xf << ddrcReg_PHY_ADDR_CTL_PLL_CFG_BGAP_ADJ_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_VCO_RNG            (1 << 7)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_CH1_PWRDWN         (1 << 6)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_BYPASS_ENABLE      (1 << 5)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_CLKOUT_ENABLE      (1 << 4)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_D_RESET            (1 << 3)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_A_RESET            (1 << 2)
#define ddrcReg_PHY_ADDR_CTL_PLL_CFG_PWRDWN             (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_DITHER_MFB     (1 << 26)
#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_PWRDWN         (1 << 25)

#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_MODE_SHIFT     20
#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_MODE_MASK      (0x7 << ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_MODE_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_INT_SHIFT      8
#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_INT_MASK       (0x1ff << ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_INT_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P2_SHIFT       4
#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P2_MASK        (0xf << ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P2_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P1_SHIFT       0
#define ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P1_MASK        (0xf << ddrcReg_PHY_ADDR_CTL_PLL_PRE_DIV_P1_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_DIV_M1_SHIFT           24
#define ddrcReg_PHY_ADDR_CTL_PLL_DIV_M1_MASK            (0xff << ddrcReg_PHY_ADDR_CTL_PLL_DIV_M1_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_DIV_FRAC_SHIFT         0
#define ddrcReg_PHY_ADDR_CTL_PLL_DIV_FRAC_MASK          (0xffffff << ddrcReg_PHY_ADDR_CTL_PLL_DIV_FRAC_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_TESTA_SHIFT       30
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_TESTA_MASK        (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_TESTA_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XS_SHIFT     27
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XS_MASK      (0x7 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XS_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XF_SHIFT     24
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XF_MASK      (0x7 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_KVCO_XF_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_LPF_BW_SHIFT      22
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_LPF_BW_MASK       (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_LPF_BW_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_LF_ORDER          (0x1 << 21)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CN_SHIFT          19
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CN_MASK           (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CN_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RN_SHIFT          17
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RN_MASK           (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RN_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CP_SHIFT          15
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CP_MASK           (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CP_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CZ_SHIFT          13
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CZ_MASK           (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_CZ_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RZ_SHIFT          10
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RZ_MASK           (0x7 << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_RZ_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICPX_SHIFT        5
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICPX_MASK         (0x1f << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICPX_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICP_OFF_SHIFT     0
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICP_OFF_MASK      (0x1f << ddrcReg_PHY_ADDR_CTL_PLL_CTL1_ICP_OFF_SHIFT)

/* ----------------------------------------------------- */
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_PTAP_ADJ_SHIFT    4
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_PTAP_ADJ_MASK     (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL2_PTAP_ADJ_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_CTAP_ADJ_SHIFT    2
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_CTAP_ADJ_MASK     (0x3 << ddrcReg_PHY_ADDR_CTL_PLL_CTL2_CTAP_ADJ_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_LOWCUR_ENABLE     (0x1 << 1)
#define ddrcReg_PHY_ADDR_CTL_PLL_CTL2_BIASIN_ENABLE     (0x1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_SS_EN_ENABLE           (0x1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_CYC_PER_TICK_SHIFT  16
#define ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_CYC_PER_TICK_MASK   (0xffff << ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_CYC_PER_TICK_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_NDIV_AMP_SHIFT      0
#define ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_NDIV_AMP_MASK       (0xffff << ddrcReg_PHY_ADDR_CTL_PLL_SS_CFG_NDIV_AMP_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_FORCE           (1 << 20)
#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_ENABLE          (1 << 16)

#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_FALL_SHIFT      12
#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_FALL_MASK       (0x3 << ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_FALL_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_RISE_SHIFT      8
#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_RISE_MASK       (0x3 << ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_RISE_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_STEP_SHIFT      0
#define ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_STEP_MASK       (0x3f << ddrcReg_PHY_ADDR_CTL_VDL_STATIC_OVR_STEP_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_ENABLE         (1 << 16)

#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_FALL_SHIFT     12
#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_FALL_MASK      (0x3 << ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_FALL_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_RISE_SHIFT     8
#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_RISE_MASK      (0x3 << ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_RISE_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_STEP_SHIFT     0
#define ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_STEP_MASK      (0x3f << ddrcReg_PHY_ADDR_CTL_VDL_DYNAMIC_OVR_STEP_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_ENABLE            (1u << 31)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_RXENB_DISABLE     (1 << 8)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CTL_IDDQ_DISABLE  (1 << 6)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CTL_REB_DISABLE   (1 << 5)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CTL_OEB_DISABLE   (1 << 4)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CKE_IDDQ_DISABLE  (1 << 2)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CKE_REB_DISABLE   (1 << 1)
#define ddrcReg_PHY_ADDR_CTL_PAD_IDLE_CKE_OEB_DISABLE   (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_PD_DONE           (1 << 30)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ND_DONE           (1 << 29)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_SAMPLE_DONE       (1 << 28)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_SAMPLE_AUTO_ENABLE    (1 << 27)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_SAMPLE_ENABLE     (1 << 26)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_OVR_ENABLE   (1 << 25)
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_OVR_ENABLE     (1 << 24)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_PD_SHIFT          20
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_PD_MASK           (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_PD_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ND_SHIFT          16
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ND_MASK           (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_ND_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_PD_SHIFT     12
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_PD_MASK      (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_PD_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_ND_SHIFT     8
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_ND_MASK      (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_ADDR_ND_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_PD_SHIFT       4
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_PD_MASK        (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_PD_SHIFT)

#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_ND_SHIFT       0
#define ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_ND_MASK        (0xf << ddrcReg_PHY_ADDR_CTL_PVT_COMP_DQ_ND_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_PAD_DRIVE_RT60B            (1 << 4)
#define ddrcReg_PHY_ADDR_CTL_PAD_DRIVE_SEL_SSTL18       (1 << 3)
#define ddrcReg_PHY_ADDR_CTL_PAD_DRIVE_SELTXDRV_CI      (1 << 2)
#define ddrcReg_PHY_ADDR_CTL_PAD_DRIVE_SELRXDRV         (1 << 1)
#define ddrcReg_PHY_ADDR_CTL_PAD_DRIVE_SLEW             (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_ADDR_CTL_CLK_RGLTR_CTL_PWR_HALF     (1 << 1)
#define ddrcReg_PHY_ADDR_CTL_CLK_RGLTR_CTL_PWR_OFF      (1 << 0)

/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* Broadcom DDR23 PHY Byte Lane register definitions */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_CNT                       2
#define ddrcReg_PHY_BYTE_LANE_MAX                       (ddrcReg_CTLR_BYTE_LANE_CNT - 1)

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_CNT               8

	typedef struct {
		uint32_t revision;
		uint32_t vdlCalibrate;
		uint32_t vdlStatus;
		 REG32_RSVD(0x000c, 0x0010);
		uint32_t vdlOverride[ddrcReg_PHY_BYTE_LANE_VDL_OVR_CNT];
		uint32_t readCtl;
		uint32_t readStatus;
		uint32_t readClear;
		uint32_t padIdleCtl;
		uint32_t padDriveCtl;
		uint32_t padClkCtl;
		uint32_t writeCtl;
		uint32_t clkRegCtl;
	} ddrcReg_PHY_BYTE_LANE_REG_t;

/* There are 2 instances of the byte Lane registers, one for each byte lane. */
#define ddrcReg_PHY_BYTE_LANE_1_REG_OFFSET              0x0500
#define ddrcReg_PHY_BYTE_LANE_2_REG_OFFSET              0x0600

#define ddrcReg_PHY_BYTE_LANE_1_REGP                    ((volatile ddrcReg_PHY_BYTE_LANE_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_PHY_BYTE_LANE_1_REG_OFFSET))
#define ddrcReg_PHY_BYTE_LANE_2_REGP                    ((volatile ddrcReg_PHY_BYTE_LANE_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_PHY_BYTE_LANE_2_REG_OFFSET))

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_REVISION_MAJOR_SHIFT      8
#define ddrcReg_PHY_BYTE_LANE_REVISION_MAJOR_MASK       (0xff << ddrcReg_PHY_BYTE_LANE_REVISION_MAJOR_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_REVISION_MINOR_SHIFT      0
#define ddrcReg_PHY_BYTE_LANE_REVISION_MINOR_MASK       (0xff << ddrcReg_PHY_BYTE_LANE_REVISION_MINOR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_CLK_2CYCLE      (1 << 4)
#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_CLK_1CYCLE      (0 << 4)

#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_TEST            (1 << 3)
#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_ALWAYS          (1 << 2)
#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_ONCE            (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_VDL_CALIB_FAST            (1 << 0)

/* ----------------------------------------------------- */

/* The byte lane VDL status calibTotal[9:0] is comprised of [9:4] step value, [3:2] fine fall */
/* and [1:0] fine rise. Note that calibTotal[9:0] is located at bit 4 in the VDL status */
/* register. The fine rise and fall are no longer used, so add some definitions for just */
/* the step setting to simplify things. */

#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_STEP_SHIFT     8
#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_STEP_MASK      (0x3f << ddrcReg_PHY_BYTE_LANE_VDL_STATUS_STEP_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_TOTAL_SHIFT    4
#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_TOTAL_MASK     (0x3ff << ddrcReg_PHY_BYTE_LANE_VDL_STATUS_TOTAL_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_LOCK           (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_VDL_STATUS_IDLE           (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_ENABLE            (1 << 16)

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_FALL_SHIFT        12
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_FALL_MASK         (0x3 << ddrcReg_PHY_BYTE_LANE_VDL_OVR_FALL_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_RISE_SHIFT        8
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_RISE_MASK         (0x3 << ddrcReg_PHY_BYTE_LANE_VDL_OVR_RISE_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_STEP_SHIFT        0
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_STEP_MASK         (0x3f << ddrcReg_PHY_BYTE_LANE_VDL_OVR_STEP_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_STATIC_READ_DQS_P     0
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_STATIC_READ_DQS_N     1
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_STATIC_READ_EN        2
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_STATIC_WRITE_DQ_DQM   3
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_DYNAMIC_READ_DQS_P    4
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_DYNAMIC_READ_DQS_N    5
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_DYNAMIC_READ_EN       6
#define ddrcReg_PHY_BYTE_LANE_VDL_OVR_IDX_DYNAMIC_WRITE_DQ_DQM  7

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_READ_CTL_DELAY_SHIFT      8
#define ddrcReg_PHY_BYTE_LANE_READ_CTL_DELAY_MASK       (0x3 << ddrcReg_PHY_BYTE_LANE_READ_CTL_DELAY_SHIFT)

#define ddrcReg_PHY_BYTE_LANE_READ_CTL_DQ_ODT_ENABLE    (1 << 3)
#define ddrcReg_PHY_BYTE_LANE_READ_CTL_DQ_ODT_ADJUST    (1 << 2)
#define ddrcReg_PHY_BYTE_LANE_READ_CTL_RD_ODT_ENABLE    (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_READ_CTL_RD_ODT_ADJUST    (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_READ_STATUS_ERROR_SHIFT   0
#define ddrcReg_PHY_BYTE_LANE_READ_STATUS_ERROR_MASK    (0xf << ddrcReg_PHY_BYTE_LANE_READ_STATUS_ERROR_SHIFT)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_READ_CLEAR_STATUS         (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_ENABLE                   (1u << 31)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DM_RXENB_DISABLE         (1 << 19)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DM_IDDQ_DISABLE          (1 << 18)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DM_REB_DISABLE           (1 << 17)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DM_OEB_DISABLE           (1 << 16)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQ_RXENB_DISABLE         (1 << 15)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQ_IDDQ_DISABLE          (1 << 14)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQ_REB_DISABLE           (1 << 13)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQ_OEB_DISABLE           (1 << 12)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_READ_ENB_RXENB_DISABLE   (1 << 11)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_READ_ENB_IDDQ_DISABLE    (1 << 10)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_READ_ENB_REB_DISABLE     (1 << 9)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_READ_ENB_OEB_DISABLE     (1 << 8)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQS_RXENB_DISABLE        (1 << 7)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQS_IDDQ_DISABLE         (1 << 6)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQS_REB_DISABLE          (1 << 5)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_DQS_OEB_DISABLE          (1 << 4)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_CLK_RXENB_DISABLE        (1 << 3)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_CLK_IDDQ_DISABLE         (1 << 2)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_CLK_REB_DISABLE          (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_PAD_IDLE_CTL_CLK_OEB_DISABLE          (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_RT60B_DDR_READ_ENB      (1 << 5)
#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_RT60B                   (1 << 4)
#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_SEL_SSTL18              (1 << 3)
#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_SELTXDRV_CI             (1 << 2)
#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_SELRXDRV                (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_PAD_DRIVE_CTL_SLEW                    (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_PAD_CLK_CTL_DISABLE                   (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_WRITE_CTL_PREAMBLE_DDR3               (1 << 0)

/* ----------------------------------------------------- */

#define ddrcReg_PHY_BYTE_LANE_CLK_REG_CTL_PWR_HALF                  (1 << 1)
#define ddrcReg_PHY_BYTE_LANE_CLK_REG_CTL_PWR_OFF                   (1 << 0)

/*********************************************************************/
/* ARM PL341 DDRC to Broadcom DDR23 PHY glue register definitions */
/*********************************************************************/

	typedef struct {
		uint32_t cfg;
		uint32_t actMonCnt;
		uint32_t ctl;
		uint32_t lbistCtl;
		uint32_t lbistSeed;
		uint32_t lbistStatus;
		uint32_t tieOff;
		uint32_t actMonClear;
		uint32_t status;
		uint32_t user;
	} ddrcReg_CTLR_PHY_GLUE_REG_t;

#define ddrcReg_CTLR_PHY_GLUE_OFFSET                            0x0700
#define ddrcReg_CTLR_PHY_GLUE_REGP                              ((volatile ddrcReg_CTLR_PHY_GLUE_REG_t *) (MM_IO_BASE_DDRC + ddrcReg_CTLR_PHY_GLUE_OFFSET))

/* ----------------------------------------------------- */

/* DDR2 / AXI block phase alignment interrupt control */
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT                     18
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_MASK                      (0x3 << ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_OFF                       (0 << ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_ON_TIGHT                  (1 << ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_ON_MEDIUM                 (2 << ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_INT_ON_LOOSE                  (3 << ddrcReg_CTLR_PHY_GLUE_CFG_INT_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_SHIFT              17
#define ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_MASK               (1 << ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_DIFFERENTIAL       (0 << ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_CMOS               (1 << ddrcReg_CTLR_PHY_GLUE_CFG_PLL_REFCLK_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHIFT            16
#define ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_MASK             (1 << ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_DEEP             (0 << ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHALLOW          (1 << ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_HW_FIXED_ALIGNMENT_DISABLED   ddrcReg_CTLR_PHY_GLUE_CFG_DIV2CLK_TREE_SHALLOW

#define ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_SHIFT             15
#define ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_MASK              (1 << ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_BP134             (0 << ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_PL301             (1 << ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_REGISTERED        ddrcReg_CTLR_PHY_GLUE_CFG_SYNC_BRIDGE_PL301

/* Software control of PHY VDL updates from control register settings. Bit 13 enables the use of Bit 14. */
/* If software control is not enabled, then updates occur when a refresh command is issued by the hardware */
/* controller. If 2 chips selects are being used, then software control must be enabled. */
#define ddrcReg_CTLR_PHY_GLUE_CFG_PHY_VDL_UPDATE_SW_CTL_LOAD    (1 << 14)
#define ddrcReg_CTLR_PHY_GLUE_CFG_PHY_VDL_UPDATE_SW_CTL_ENABLE  (1 << 13)

/* Use these to bypass a pipeline stage. By default the ADDR is off but the BYTE LANE in / out are on. */
#define ddrcReg_CTLR_PHY_GLUE_CFG_PHY_ADDR_CTL_IN_BYPASS_PIPELINE_STAGE (1 << 12)
#define ddrcReg_CTLR_PHY_GLUE_CFG_PHY_BYTE_LANE_IN_BYPASS_PIPELINE_STAGE (1 << 11)
#define ddrcReg_CTLR_PHY_GLUE_CFG_PHY_BYTE_LANE_OUT_BYPASS_PIPELINE_STAGE (1 << 10)

/* Chip select count */
#define ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_SHIFT                  9
#define ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_MASK                   (1 << ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_1                      (0 << ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_2                      (1 << ddrcReg_CTLR_PHY_GLUE_CFG_CS_CNT_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_CLK_SHIFT                     8
#define ddrcReg_CTLR_PHY_GLUE_CFG_CLK_ASYNC                     (0 << ddrcReg_CTLR_PHY_GLUE_CFG_CLK_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_CLK_SYNC                      (1 << ddrcReg_CTLR_PHY_GLUE_CFG_CLK_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_CKE_INIT_SHIFT                7
#define ddrcReg_CTLR_PHY_GLUE_CFG_CKE_INIT_LOW                  (0 << ddrcReg_CTLR_PHY_GLUE_CFG_CKE_INIT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_CKE_INIT_HIGH                 (1 << ddrcReg_CTLR_PHY_GLUE_CFG_CKE_INIT_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_DQM_INIT_SHIFT                6
#define ddrcReg_CTLR_PHY_GLUE_CFG_DQM_INIT_LOW                  (0 << ddrcReg_CTLR_PHY_GLUE_CFG_DQM_INIT_SHIFT)
#define ddrcReg_CTLR_PHY_GLUE_CFG_DQM_INIT_HIGH                 (1 << ddrcReg_CTLR_PHY_GLUE_CFG_DQM_INIT_SHIFT)

#define ddrcReg_CTLR_PHY_GLUE_CFG_CAS_LATENCY_SHIFT             0
#define ddrcReg_CTLR_PHY_GLUE_CFG_CAS_LATENCY_MASK              (0x7 << ddrcReg_CTLR_PHY_GLUE_CFG_CAS_LATENCY_SHIFT)

/* ----------------------------------------------------- */
#define ddrcReg_CTLR_PHY_GLUE_STATUS_PHASE_SHIFT                0
#define ddrcReg_CTLR_PHY_GLUE_STATUS_PHASE_MASK                 (0x7f << ddrcReg_CTLR_PHY_GLUE_STATUS_PHASE_SHIFT)

/* ---- Public Function Prototypes --------------------------------------- */

#ifdef __cplusplus
}				/* end extern "C" */
#endif
#endif				/* DDRC_REG_H */
