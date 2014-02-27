/*
 * HND SiliconBackplane PMU support.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: hndpmu.h 385540 2013-02-15 23:14:50Z $
 */

#ifndef _hndpmu_h_
#define _hndpmu_h_


extern void si_pmu_otp_power(si_t *sih, osl_t *osh, bool on);
extern void si_sdiod_drive_strength_init(si_t *sih, osl_t *osh, uint32 drivestrength);

extern void si_pmu_minresmask_htavail_set(si_t *sih, osl_t *osh, bool set_clear);

#endif /* _hndpmu_h_ */
