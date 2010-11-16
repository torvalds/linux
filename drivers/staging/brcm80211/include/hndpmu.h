/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _hndpmu_h_
#define _hndpmu_h_

#define SET_LDO_VOLTAGE_LDO1	1
#define SET_LDO_VOLTAGE_LDO2	2
#define SET_LDO_VOLTAGE_LDO3	3
#define SET_LDO_VOLTAGE_PAREF	4
#define SET_LDO_VOLTAGE_CLDO_PWM	5
#define SET_LDO_VOLTAGE_CLDO_BURST	6
#define SET_LDO_VOLTAGE_CBUCK_PWM	7
#define SET_LDO_VOLTAGE_CBUCK_BURST	8
#define SET_LDO_VOLTAGE_LNLDO1	9
#define SET_LDO_VOLTAGE_LNLDO2_SEL	10

extern void si_pmu_init(si_t *sih, struct osl_info *osh);
extern void si_pmu_chip_init(si_t *sih, struct osl_info *osh);
extern void si_pmu_pll_init(si_t *sih, struct osl_info *osh, u32 xtalfreq);
extern void si_pmu_res_init(si_t *sih, struct osl_info *osh);
extern void si_pmu_swreg_init(si_t *sih, struct osl_info *osh);

extern u32 si_pmu_force_ilp(si_t *sih, struct osl_info *osh, bool force);

extern u32 si_pmu_si_clock(si_t *sih, struct osl_info *osh);
extern u32 si_pmu_cpu_clock(si_t *sih, struct osl_info *osh);
extern u32 si_pmu_mem_clock(si_t *sih, struct osl_info *osh);
extern u32 si_pmu_alp_clock(si_t *sih, struct osl_info *osh);
extern u32 si_pmu_ilp_clock(si_t *sih, struct osl_info *osh);

extern void si_pmu_set_switcher_voltage(si_t *sih, struct osl_info *osh,
					u8 bb_voltage, u8 rf_voltage);
extern void si_pmu_set_ldo_voltage(si_t *sih, struct osl_info *osh, u8 ldo,
				   u8 voltage);
extern u16 si_pmu_fast_pwrup_delay(si_t *sih, struct osl_info *osh);
extern void si_pmu_rcal(si_t *sih, struct osl_info *osh);
extern void si_pmu_pllupd(si_t *sih);
extern void si_pmu_spuravoid(si_t *sih, struct osl_info *osh, u8 spuravoid);

extern bool si_pmu_is_otp_powered(si_t *sih, struct osl_info *osh);
extern u32 si_pmu_measure_alpclk(si_t *sih, struct osl_info *osh);

extern u32 si_pmu_chipcontrol(si_t *sih, uint reg, u32 mask, u32 val);
extern u32 si_pmu_regcontrol(si_t *sih, uint reg, u32 mask, u32 val);
extern u32 si_pmu_pllcontrol(si_t *sih, uint reg, u32 mask, u32 val);
extern void si_pmu_pllupd(si_t *sih);
extern void si_pmu_sprom_enable(si_t *sih, struct osl_info *osh, bool enable);

extern void si_pmu_radio_enable(si_t *sih, bool enable);
extern u32 si_pmu_waitforclk_on_backplane(si_t *sih, struct osl_info *osh,
					     u32 clk, u32 delay);

extern void si_pmu_otp_power(si_t *sih, struct osl_info *osh, bool on);
extern void si_sdiod_drive_strength_init(si_t *sih, struct osl_info *osh,
					 u32 drivestrength);

#endif				/* _hndpmu_h_ */
