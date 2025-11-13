// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/dev_printk.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include "core.h"
#include "ref.h"

/**
 * zl3073x_ref_freq_factorize - factorize given frequency
 * @freq: input frequency
 * @base: base frequency
 * @mult: multiplier
 *
 * Checks if the given frequency can be factorized using one of the
 * supported base frequencies. If so the base frequency and multiplier
 * are stored into appropriate parameters if they are not NULL.
 *
 * Return: 0 on success, -EINVAL if the frequency cannot be factorized
 */
int
zl3073x_ref_freq_factorize(u32 freq, u16 *base, u16 *mult)
{
	static const u16 base_freqs[] = {
		1, 2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 64, 80, 100, 125,
		128, 160, 200, 250, 256, 320, 400, 500, 625, 640, 800, 1000,
		1250, 1280, 1600, 2000, 2500, 3125, 3200, 4000, 5000, 6250,
		6400, 8000, 10000, 12500, 15625, 16000, 20000, 25000, 31250,
		32000, 40000, 50000, 62500,
	};
	u32 div;
	int i;

	for (i = 0; i < ARRAY_SIZE(base_freqs); i++) {
		div = freq / base_freqs[i];

		if (div <= U16_MAX && (freq % base_freqs[i]) == 0) {
			if (base)
				*base = base_freqs[i];
			if (mult)
				*mult = div;

			return 0;
		}
	}

	return -EINVAL;
}

/**
 * zl3073x_ref_state_fetch - fetch input reference state from hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: input reference index to fetch state for
 *
 * Function fetches state for the given input reference from hardware and
 * stores it for later use.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_ref_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_ref *ref = &zldev->ref[index];
	int rc;

	/* For differential type inputs the N-pin reference shares
	 * part of the configuration with the P-pin counterpart.
	 */
	if (zl3073x_is_n_pin(index) && zl3073x_ref_is_diff(ref - 1)) {
		struct zl3073x_ref *p_ref = &zldev->ref[index - 1];

		/* Copy the shared items from the P-pin */
		ref->config = p_ref->config;

		return 0; /* Finish - no non-shared items for now */
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read ref_config register */
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_CONFIG, &ref->config);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "REF%u is %s and configured as %s\n", index,
		str_enabled_disabled(zl3073x_ref_is_enabled(ref)),
		zl3073x_ref_is_diff(ref) ? "differential" : "single-ended");

	return rc;
}

/**
 * zl3073x_ref_state_get - get current input reference state
 * @zldev: pointer to zl3073x_dev structure
 * @index: input reference index to get state for
 *
 * Return: pointer to given input reference state
 */
const struct zl3073x_ref *
zl3073x_ref_state_get(struct zl3073x_dev *zldev, u8 index)
{
	return &zldev->ref[index];
}
