/*****************************************************************************
* Copyright 2009 Broadcom Corporation.  All rights reserved.
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

#ifndef CAP_H
#define CAP_H

/* ---- Include Files ---------------------------------------------------- */
/* ---- Public Constants and Types --------------------------------------- */
typedef enum {
	CAP_NOT_PRESENT = 0,
	CAP_PRESENT
} CAP_RC_T;

typedef enum {
	CAP_VPM,
	CAP_ETH_PHY,
	CAP_ETH_GMII,
	CAP_ETH_SGMII,
	CAP_USB,
	CAP_TSC,
	CAP_EHSS,
	CAP_SDIO,
	CAP_UARTB,
	CAP_KEYPAD,
	CAP_CLCD,
	CAP_GE,
	CAP_LEDM,
	CAP_BBL,
	CAP_VDEC,
	CAP_PIF,
	CAP_APM,
	CAP_SPU,
	CAP_PKA,
	CAP_RNG,
} CAP_CAPABILITY_T;

typedef enum {
	CAP_LCD_WVGA = 0,
	CAP_LCD_VGA = 0x1,
	CAP_LCD_WQVGA = 0x2,
	CAP_LCD_QVGA = 0x3
} CAP_LCD_RES_T;

/* ---- Public Variable Externs ------------------------------------------ */
/* ---- Public Function Prototypes --------------------------------------- */

static inline CAP_RC_T cap_isPresent(CAP_CAPABILITY_T capability, int index);
static inline uint32_t cap_getMaxArmSpeedHz(void);
static inline uint32_t cap_getMaxVpmSpeedHz(void);
static inline CAP_LCD_RES_T cap_getMaxLcdRes(void);

#endif
