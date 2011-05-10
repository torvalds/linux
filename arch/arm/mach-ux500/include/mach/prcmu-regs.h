/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 *
 * License Terms: GNU General Public License v2
 *
 * PRCM Unit registers
 */

#ifndef __MACH_PRCMU_REGS_H
#define __MACH_PRCMU_REGS_H

#include <mach/hardware.h>

#define _PRCMU_BASE		IO_ADDRESS(U8500_PRCMU_BASE)

#define PRCM_ARM_PLLDIVPS	(_PRCMU_BASE + 0x118)
#define PRCM_ARM_CHGCLKREQ	(_PRCMU_BASE + 0x114)
#define PRCM_PLLARM_ENABLE	(_PRCMU_BASE + 0x98)
#define PRCM_ARMCLKFIX_MGT	(_PRCMU_BASE + 0x0)
#define PRCM_A9_RESETN_CLR	(_PRCMU_BASE + 0x1f4)
#define PRCM_A9_RESETN_SET	(_PRCMU_BASE + 0x1f0)
#define PRCM_ARM_LS_CLAMP	(_PRCMU_BASE + 0x30c)
#define PRCM_SRAM_A9		(_PRCMU_BASE + 0x308)

/* ARM WFI Standby signal register */
#define PRCM_ARM_WFI_STANDBY    (_PRCMU_BASE + 0x130)
#define PRCMU_IOCR              (_PRCMU_BASE + 0x310)

/* CPU mailbox registers */
#define PRCM_MBOX_CPU_VAL	(_PRCMU_BASE + 0x0fc)
#define PRCM_MBOX_CPU_SET	(_PRCMU_BASE + 0x100)
#define PRCM_MBOX_CPU_CLR	(_PRCMU_BASE + 0x104)

/* Dual A9 core interrupt management unit registers */
#define PRCM_A9_MASK_REQ	(_PRCMU_BASE + 0x328)
#define PRCM_A9_MASK_ACK	(_PRCMU_BASE + 0x32c)
#define PRCM_ARMITMSK31TO0	(_PRCMU_BASE + 0x11c)
#define PRCM_ARMITMSK63TO32	(_PRCMU_BASE + 0x120)
#define PRCM_ARMITMSK95TO64	(_PRCMU_BASE + 0x124)
#define PRCM_ARMITMSK127TO96	(_PRCMU_BASE + 0x128)
#define PRCM_POWER_STATE_VAL	(_PRCMU_BASE + 0x25C)
#define PRCM_ARMITVAL31TO0	(_PRCMU_BASE + 0x260)
#define PRCM_ARMITVAL63TO32	(_PRCMU_BASE + 0x264)
#define PRCM_ARMITVAL95TO64	(_PRCMU_BASE + 0x268)
#define PRCM_ARMITVAL127TO96	(_PRCMU_BASE + 0x26C)

#define PRCM_HOSTACCESS_REQ	(_PRCMU_BASE + 0x334)
#define ARM_WAKEUP_MODEM	0x1

#define PRCM_ARM_IT1_CLEAR	(_PRCMU_BASE + 0x48C)
#define PRCM_ARM_IT1_VAL	(_PRCMU_BASE + 0x494)
#define PRCM_HOLD_EVT		(_PRCMU_BASE + 0x174)

#define PRCM_ITSTATUS0		(_PRCMU_BASE + 0x148)
#define PRCM_ITSTATUS1		(_PRCMU_BASE + 0x150)
#define PRCM_ITSTATUS2		(_PRCMU_BASE + 0x158)
#define PRCM_ITSTATUS3		(_PRCMU_BASE + 0x160)
#define PRCM_ITSTATUS4		(_PRCMU_BASE + 0x168)
#define PRCM_ITSTATUS5		(_PRCMU_BASE + 0x484)
#define PRCM_ITCLEAR5		(_PRCMU_BASE + 0x488)
#define PRCM_ARMIT_MASKXP70_IT	(_PRCMU_BASE + 0x1018)

/* System reset register */
#define PRCM_APE_SOFTRST	(_PRCMU_BASE + 0x228)

/* Level shifter and clamp control registers */
#define PRCM_MMIP_LS_CLAMP_SET     (_PRCMU_BASE + 0x420)
#define PRCM_MMIP_LS_CLAMP_CLR     (_PRCMU_BASE + 0x424)

/* PRCMU clock/PLL/reset registers */
#define PRCM_PLLDSI_FREQ           (_PRCMU_BASE + 0x500)
#define PRCM_PLLDSI_ENABLE         (_PRCMU_BASE + 0x504)
#define PRCM_LCDCLK_MGT            (_PRCMU_BASE + 0x044)
#define PRCM_MCDECLK_MGT           (_PRCMU_BASE + 0x064)
#define PRCM_HDMICLK_MGT           (_PRCMU_BASE + 0x058)
#define PRCM_TVCLK_MGT             (_PRCMU_BASE + 0x07c)
#define PRCM_DSI_PLLOUT_SEL        (_PRCMU_BASE + 0x530)
#define PRCM_DSITVCLK_DIV          (_PRCMU_BASE + 0x52C)
#define PRCM_APE_RESETN_SET        (_PRCMU_BASE + 0x1E4)
#define PRCM_APE_RESETN_CLR        (_PRCMU_BASE + 0x1E8)

/* ePOD and memory power signal control registers */
#define PRCM_EPOD_C_SET            (_PRCMU_BASE + 0x410)
#define PRCM_SRAM_LS_SLEEP         (_PRCMU_BASE + 0x304)

/* Debug power control unit registers */
#define PRCM_POWER_STATE_SET       (_PRCMU_BASE + 0x254)

/* Miscellaneous unit registers */
#define PRCM_DSI_SW_RESET          (_PRCMU_BASE + 0x324)

#endif /* __MACH_PRCMU_REGS_H */
