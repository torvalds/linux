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


#ifndef _BRCM_PMU_H_
#define _BRCM_PMU_H_

#include "types.h"

extern u16 si_pmu_fast_pwrup_delay(struct si_pub *sih);
extern void si_pmu_sprom_enable(struct si_pub *sih, bool enable);
extern u32 si_pmu_chipcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val);
extern u32 si_pmu_regcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val);
extern u32 si_pmu_alp_clock(struct si_pub *sih);
extern void si_pmu_pllupd(struct si_pub *sih);
extern void si_pmu_spuravoid_pllupdate(struct si_pub *sih, u8 spuravoid);
extern u32 si_pmu_pllcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val);
extern void si_pmu_init(struct si_pub *sih);
extern void si_pmu_res_init(struct si_pub *sih);
extern u32 si_pmu_measure_alpclk(struct si_pub *sih);

#endif /* _BRCM_PMU_H_ */
