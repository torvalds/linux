/*
 * arch/arm/mach-tegra/board-stingray.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MACH_TEGRA_BOARD_STINGRAY_H
#define _MACH_TEGRA_BOARD_STINGRAY_H

void stingray_pinmux_init(void);
int stingray_panel_init(void);
int stingray_keypad_init(void);
int stingray_wlan_init(void);
int stingray_sensors_init(void);
int stingray_touch_init(void);
int stingray_power_init(void);
unsigned int stingray_revision(void);
void stingray_gps_init(void);

/* as defined in the bootloader*/
#define HWREV(x)    (((x)>>16) & 0xFFFF)
#define INSTANCE(x) ((x) & 0xFFFF)
#define _HWREV(x) ((x)<<16)
#define STINGRAY_REVISION_UNKNOWN    _HWREV(0x0000)
#define SSTINGRAY_REVISION_DEF       _HWREV(0xFF00)
#define STINGRAY_REVISION_S1         _HWREV(0x1100)
#define STINGRAY_REVISION_S2         _HWREV(0x1200)
#define STINGRAY_REVISION_S3         _HWREV(0x1300)
#define STINGRAY_REVISION_M1         _HWREV(0x2100)
#define STINGRAY_REVISION_M2         _HWREV(0x2200)
#define STINGRAY_REVISION_M3         _HWREV(0x2300)
#define STINGRAY_REVISION_P0         _HWREV(0x8000)
#define STINGRAY_REVISION_P1         _HWREV(0x8100)
#define STINGRAY_REVISION_P2         _HWREV(0x8200)
#define STINGRAY_REVISION_P3         _HWREV(0x8300)
#define STINGRAY_REVISION_P4         _HWREV(0x8400)
#define STINGRAY_REVISION_P5         _HWREV(0x8500)
#define STINGRAY_REVISION_P6         _HWREV(0x8600)

/*
 * These #defines are used for the bits in powerup_reason.
 */
#define PU_REASON_USB_CABLE             0x00000010 /* Bit 4  */
#define PU_REASON_FACTORY_CABLE         0x00000020 /* Bit 5  */
#define PU_REASON_PWR_KEY_PRESS         0x00000080 /* Bit 7  */
#define PU_REASON_CHARGER               0x00000100 /* Bit 8  */
#define PU_REASON_POWER_CUT             0x00000200 /* bit 9  */
#define PU_REASON_SW_AP_RESET           0x00004000 /* Bit 14 */
#define PU_REASON_WDOG_AP_RESET         0x00008000 /* Bit 15 */
#define PU_REASON_AP_KERNEL_PANIC       0x00020000 /* Bit 17 */


#endif
