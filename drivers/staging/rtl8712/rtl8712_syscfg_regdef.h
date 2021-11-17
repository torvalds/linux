/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL8712_SYSCFG_REGDEF_H__
#define __RTL8712_SYSCFG_REGDEF_H__


#define SYS_ISO_CTRL		(RTL8712_SYSCFG_ + 0x0000)
#define SYS_FUNC_EN		(RTL8712_SYSCFG_ + 0x0002)
#define PMC_FSM			(RTL8712_SYSCFG_ + 0x0004)
#define SYS_CLKR		(RTL8712_SYSCFG_ + 0x0008)
#define EE_9346CR		(RTL8712_SYSCFG_ + 0x000A)
#define EE_VPD			(RTL8712_SYSCFG_ + 0x000C)
#define AFE_MISC		(RTL8712_SYSCFG_ + 0x0010)
#define SPS0_CTRL		(RTL8712_SYSCFG_ + 0x0011)
#define SPS1_CTRL		(RTL8712_SYSCFG_ + 0x0018)
#define RF_CTRL			(RTL8712_SYSCFG_ + 0x001F)
#define LDOA15_CTRL		(RTL8712_SYSCFG_ + 0x0020)
#define LDOV12D_CTRL		(RTL8712_SYSCFG_ + 0x0021)
#define LDOHCI12_CTRL		(RTL8712_SYSCFG_ + 0x0022)
#define LDO_USB_CTRL		(RTL8712_SYSCFG_ + 0x0023)
#define LPLDO_CTRL		(RTL8712_SYSCFG_ + 0x0024)
#define AFE_XTAL_CTRL		(RTL8712_SYSCFG_ + 0x0026)
#define AFE_PLL_CTRL		(RTL8712_SYSCFG_ + 0x0028)
#define EFUSE_CTRL		(RTL8712_SYSCFG_ + 0x0030)
#define EFUSE_TEST		(RTL8712_SYSCFG_ + 0x0034)
#define PWR_DATA		(RTL8712_SYSCFG_ + 0x0038)
#define DPS_TIMER		(RTL8712_SYSCFG_ + 0x003C)
#define RCLK_MON		(RTL8712_SYSCFG_ + 0x003E)
#define EFUSE_CLK_CTRL		(RTL8712_SYSCFG_ + 0x02F8)


#endif /*__RTL8712_SYSCFG_REGDEF_H__*/

