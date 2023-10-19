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
#ifndef __RTL8712_WMAC_REGDEF_H__
#define __RTL8712_WMAC_REGDEF_H__

#define NAVCTRL				(RTL8712_WMAC_ + 0x00)
#define BWOPMODE			(RTL8712_WMAC_ + 0x03)
#define BACAMCMD			(RTL8712_WMAC_ + 0x04)
#define BACAMCONTENT			(RTL8712_WMAC_ + 0x08)
#define LBDLY				(RTL8712_WMAC_ + 0x10)
#define FWDLY				(RTL8712_WMAC_ + 0x11)
#define HWPC_RX_CTRL			(RTL8712_WMAC_ + 0x18)
#define MQ				(RTL8712_WMAC_ + 0x20)
#define MA				(RTL8712_WMAC_ + 0x22)
#define MS				(RTL8712_WMAC_ + 0x24)
#define CLM_RESULT			(RTL8712_WMAC_ + 0x27)
#define NHM_RPI_CNT			(RTL8712_WMAC_ + 0x28)
#define RXERR_RPT			(RTL8712_WMAC_ + 0x30)
#define NAV_PROT_LEN			(RTL8712_WMAC_ + 0x34)
#define CFEND_TH			(RTL8712_WMAC_ + 0x36)
#define AMPDU_MIN_SPACE			(RTL8712_WMAC_ + 0x37)
#define	TXOP_STALL_CTRL			(RTL8712_WMAC_ + 0x38)

#endif /*__RTL8712_WMAC_REGDEF_H__*/

