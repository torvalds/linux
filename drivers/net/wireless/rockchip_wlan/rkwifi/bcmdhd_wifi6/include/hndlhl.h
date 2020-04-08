/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HND SiliconBackplane PMU support.
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
 * $Id: hndpmu.h 546588 2015-04-13 09:24:52Z $
 */

#ifndef _hndlhl_h_
#define _hndlhl_h_

enum {
	LHL_MAC_TIMER = 0,
	LHL_ARM_TIMER = 1
};

typedef struct {
	uint16 offset;
	uint32 mask;
	uint32 val;
} lhl_reg_set_t;

#define LHL_REG_OFF(reg) OFFSETOF(gciregs_t, reg)

extern void si_lhl_timer_config(si_t *sih, osl_t *osh, int timer_type);
extern void si_lhl_timer_enable(si_t *sih);

extern void si_lhl_setup(si_t *sih, osl_t *osh);
extern void si_lhl_enable(si_t *sih, osl_t *osh, bool enable);
extern void si_lhl_ilp_config(si_t *sih, osl_t *osh, uint32 ilp_period);
extern void si_lhl_enable_sdio_wakeup(si_t *sih, osl_t *osh);
extern void si_lhl_disable_sdio_wakeup(si_t *sih);
extern int si_lhl_set_lpoclk(si_t *sih, osl_t *osh, uint32 lpo_force);
extern void si_set_lv_sleep_mode_lhl_config_4369(si_t *sih);

#define HIB_EXT_WAKEUP_CAP(sih)  (BCM4347_CHIP(sih->chip))

#define LHL_IS_PSMODE_0(sih)  (si_lhl_ps_mode(sih) == LHL_PS_MODE_0)
#define LHL_IS_PSMODE_1(sih)  (si_lhl_ps_mode(sih) == LHL_PS_MODE_1)
#endif /* _hndlhl_h_ */
