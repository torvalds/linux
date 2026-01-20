/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_SYNTH_H
#define _ZL3073X_SYNTH_H

#include <linux/bitfield.h>
#include <linux/math64.h>
#include <linux/types.h>

#include "regs.h"

struct zl3073x_dev;

/**
 * struct zl3073x_synth - synthesizer state
 * @freq_mult: frequency multiplier
 * @freq_base: frequency base
 * @freq_m: frequency numerator
 * @freq_n: frequency denominator
 * @ctrl: synth control
 */
struct zl3073x_synth {
	u32	freq_mult;
	u16	freq_base;
	u16	freq_m;
	u16	freq_n;
	u8	ctrl;
};

int zl3073x_synth_state_fetch(struct zl3073x_dev *zldev, u8 synth_id);

const struct zl3073x_synth *zl3073x_synth_state_get(struct zl3073x_dev *zldev,
						    u8 synth_id);

int zl3073x_synth_state_set(struct zl3073x_dev *zldev, u8 synth_id,
			    const struct zl3073x_synth *synth);

/**
 * zl3073x_synth_dpll_get - get DPLL ID the synth is driven by
 * @synth: pointer to synth state
 *
 * Return: ID of DPLL the given synthetizer is driven by
 */
static inline u8 zl3073x_synth_dpll_get(const struct zl3073x_synth *synth)
{
	return FIELD_GET(ZL_SYNTH_CTRL_DPLL_SEL, synth->ctrl);
}

/**
 * zl3073x_synth_freq_get - get synth current freq
 * @synth: pointer to synth state
 *
 * Return: frequency of given synthetizer
 */
static inline u32 zl3073x_synth_freq_get(const struct zl3073x_synth *synth)
{
	return mul_u64_u32_div(synth->freq_base * synth->freq_m,
			       synth->freq_mult, synth->freq_n);
}

/**
 * zl3073x_synth_is_enabled - check if the given synth is enabled
 * @synth: pointer to synth state
 *
 * Return: true if synth is enabled, false otherwise
 */
static inline bool zl3073x_synth_is_enabled(const struct zl3073x_synth *synth)
{
	return FIELD_GET(ZL_SYNTH_CTRL_EN, synth->ctrl);
}

#endif /* _ZL3073X_SYNTH_H */
