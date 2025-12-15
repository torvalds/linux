/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_REF_H
#define _ZL3073X_REF_H

#include <linux/bitfield.h>
#include <linux/math64.h>
#include <linux/types.h>

#include "regs.h"

struct zl3073x_dev;

/**
 * struct zl3073x_ref - input reference state
 * @ffo: current fractional frequency offset
 * @phase_comp: phase compensation
 * @esync_n_div: divisor for embedded sync or n-divided signal formats
 * @freq_base: frequency base
 * @freq_mult: frequnecy multiplier
 * @freq_ratio_m: FEC mode multiplier
 * @freq_ratio_n: FEC mode divisor
 * @config: reference config
 * @sync_ctrl: reference sync control
 * @mon_status: reference monitor status
 */
struct zl3073x_ref {
	s64	ffo;
	u64	phase_comp;
	u32	esync_n_div;
	u16	freq_base;
	u16	freq_mult;
	u16	freq_ratio_m;
	u16	freq_ratio_n;
	u8	config;
	u8	sync_ctrl;
	u8	mon_status;
};

int zl3073x_ref_state_fetch(struct zl3073x_dev *zldev, u8 index);

const struct zl3073x_ref *zl3073x_ref_state_get(struct zl3073x_dev *zldev,
						u8 index);

int zl3073x_ref_state_set(struct zl3073x_dev *zldev, u8 index,
			  const struct zl3073x_ref *ref);

int zl3073x_ref_freq_factorize(u32 freq, u16 *base, u16 *mult);

/**
 * zl3073x_ref_ffo_get - get current fractional frequency offset
 * @ref: pointer to ref state
 *
 * Return: the latest measured fractional frequency offset
 */
static inline s64
zl3073x_ref_ffo_get(const struct zl3073x_ref *ref)
{
	return ref->ffo;
}

/**
 * zl3073x_ref_freq_get - get given input reference frequency
 * @ref: pointer to ref state
 *
 * Return: frequency of the given input reference
 */
static inline u32
zl3073x_ref_freq_get(const struct zl3073x_ref *ref)
{
	return mul_u64_u32_div(ref->freq_base * ref->freq_mult,
			       ref->freq_ratio_m, ref->freq_ratio_n);
}

/**
 * zl3073x_ref_freq_set - set given input reference frequency
 * @ref: pointer to ref state
 * @freq: frequency to be set
 *
 * Return: 0 on success, <0 when frequency cannot be factorized
 */
static inline int
zl3073x_ref_freq_set(struct zl3073x_ref *ref, u32 freq)
{
	u16 base, mult;
	int rc;

	rc = zl3073x_ref_freq_factorize(freq, &base, &mult);
	if (rc)
		return rc;

	ref->freq_base = base;
	ref->freq_mult = mult;

	return 0;
}

/**
 * zl3073x_ref_is_diff - check if the given input reference is differential
 * @ref: pointer to ref state
 *
 * Return: true if reference is differential, false if reference is single-ended
 */
static inline bool
zl3073x_ref_is_diff(const struct zl3073x_ref *ref)
{
	return !!FIELD_GET(ZL_REF_CONFIG_DIFF_EN, ref->config);
}

/**
 * zl3073x_ref_is_enabled - check if the given input reference is enabled
 * @ref: pointer to ref state
 *
 * Return: true if input refernce is enabled, false otherwise
 */
static inline bool
zl3073x_ref_is_enabled(const struct zl3073x_ref *ref)
{
	return !!FIELD_GET(ZL_REF_CONFIG_ENABLE, ref->config);
}

/**
 * zl3073x_ref_is_status_ok - check the given input reference status
 * @ref: pointer to ref state
 *
 * Return: true if the status is ok, false otherwise
 */
static inline bool
zl3073x_ref_is_status_ok(const struct zl3073x_ref *ref)
{
	return ref->mon_status == ZL_REF_MON_STATUS_OK;
}

#endif /* _ZL3073X_REF_H */
