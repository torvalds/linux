/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip related low power flags
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: lpflags.h 592839 2015-10-14 14:19:09Z $
 */
#ifndef _lpflags_h_
#define _lpflags_h_

/* Chip related low power flags (lpflags) */
#define LPFLAGS_SI_GLOBAL_DISABLE		(1 << 0)
#define LPFLAGS_SI_MEM_STDBY_DISABLE		(1 << 1)
#define LPFLAGS_SI_SFLASH_DISABLE		(1 << 2)
#define LPFLAGS_SI_BTLDO3P3_DISABLE		(1 << 3)
#define LPFLAGS_SI_GCI_FORCE_REGCLK_DISABLE	(1 << 4)
#define LPFLAGS_SI_FORCE_PWM_WHEN_RADIO_ON	(1 << 5)
#define LPFLAGS_SI_DS0_SLEEP_PDA_DISABLE	(1 << 6)
#define LPFLAGS_SI_DS1_SLEEP_PDA_DISABLE	(1 << 7)
#define LPFLAGS_PHY_GLOBAL_DISABLE		(1 << 16)
#define LPFLAGS_PHY_LP_DISABLE			(1 << 17)
#define LPFLAGS_PSM_PHY_CTL			(1 << 18)

#endif /* _lpflags_h_ */
