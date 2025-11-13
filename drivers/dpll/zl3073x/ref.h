/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_REF_H
#define _ZL3073X_REF_H

#include <linux/bitfield.h>
#include <linux/types.h>

#include "regs.h"

struct zl3073x_dev;

/**
 * struct zl3073x_ref - input reference state
 * @ffo: current fractional frequency offset
 * @config: reference config
 * @mon_status: reference monitor status
 */
struct zl3073x_ref {
	s64	ffo;
	u8	config;
	u8	mon_status;
};

int zl3073x_ref_state_fetch(struct zl3073x_dev *zldev, u8 index);

const struct zl3073x_ref *zl3073x_ref_state_get(struct zl3073x_dev *zldev,
						u8 index);

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
