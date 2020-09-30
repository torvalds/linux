/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HND SiliconBackplane PMU support.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * $Id: hndpmu.h 657872 2016-09-02 22:17:34Z $
 */

#ifndef _hndpmu_h_
#define _hndpmu_h_

#include <typedefs.h>
#include <osl_decl.h>
#include <siutils.h>
#include <sbchipc.h>


extern void si_pmu_otp_power(si_t *sih, osl_t *osh, bool on, uint32* min_res_mask);
extern void si_sdiod_drive_strength_init(si_t *sih, osl_t *osh, uint32 drivestrength);

extern void si_pmu_minresmask_htavail_set(si_t *sih, osl_t *osh, bool set_clear);
extern void si_pmu_slow_clk_reinit(si_t *sih, osl_t *osh);
extern void si_pmu_avbtimer_enable(si_t *sih, osl_t *osh, bool set_flag);
extern uint32 si_pmu_dump_pmucap_binary(si_t *sih, uchar *p);
extern uint32 si_pmu_dump_buf_size_pmucap(si_t *sih);
extern int si_pmu_wait_for_steady_state(si_t *sih, osl_t *osh, pmuregs_t *pmu);
#if defined(BCMULP)
int si_pmu_ulp_register(si_t *sih);
extern void si_pmu_ulp_ilp_config(si_t *sih, osl_t *osh, uint32 ilp_period);
#endif /* BCMULP */
extern uint32 si_pmu_get_pmutimer(si_t *sih);
extern void si_pmu_set_min_res_mask(si_t *sih, osl_t *osh, uint min_res_mask);
extern bool si_pmu_cap_fast_lpo(si_t *sih);
extern int si_pmu_fast_lpo_disable(si_t *sih);
#endif /* _hndpmu_h_ */
