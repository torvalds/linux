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
		struct zl3073x_ref *p_ref = ref - 1; /* P-pin counterpart*/

		/* Copy the shared items from the P-pin */
		ref->config = p_ref->config;
		ref->esync_n_div = p_ref->esync_n_div;
		ref->freq_base = p_ref->freq_base;
		ref->freq_mult = p_ref->freq_mult;
		ref->freq_ratio_m = p_ref->freq_ratio_m;
		ref->freq_ratio_n = p_ref->freq_ratio_n;
		ref->phase_comp = p_ref->phase_comp;
		ref->sync_ctrl = p_ref->sync_ctrl;

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

	/* Read frequency related registers */
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_FREQ_BASE, &ref->freq_base);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_FREQ_MULT, &ref->freq_mult);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_RATIO_M, &ref->freq_ratio_m);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_RATIO_N, &ref->freq_ratio_n);
	if (rc)
		return rc;

	/* Read eSync and N-div rated registers */
	rc = zl3073x_read_u32(zldev, ZL_REG_REF_ESYNC_DIV, &ref->esync_n_div);
	if (rc)
		return rc;
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_SYNC_CTRL, &ref->sync_ctrl);
	if (rc)
		return rc;

	/* Read phase compensation register */
	rc = zl3073x_read_u48(zldev, ZL_REG_REF_PHASE_OFFSET_COMP,
			      &ref->phase_comp);
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

int zl3073x_ref_state_set(struct zl3073x_dev *zldev, u8 index,
			  const struct zl3073x_ref *ref)
{
	struct zl3073x_ref *dref = &zldev->ref[index];
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Update mailbox with changed values */
	if (dref->freq_base != ref->freq_base)
		rc = zl3073x_write_u16(zldev, ZL_REG_REF_FREQ_BASE,
				       ref->freq_base);
	if (!rc && dref->freq_mult != ref->freq_mult)
		rc = zl3073x_write_u16(zldev, ZL_REG_REF_FREQ_MULT,
				       ref->freq_mult);
	if (!rc && dref->freq_ratio_m != ref->freq_ratio_m)
		rc = zl3073x_write_u16(zldev, ZL_REG_REF_RATIO_M,
				       ref->freq_ratio_m);
	if (!rc && dref->freq_ratio_n != ref->freq_ratio_n)
		rc = zl3073x_write_u16(zldev, ZL_REG_REF_RATIO_N,
				       ref->freq_ratio_n);
	if (!rc && dref->esync_n_div != ref->esync_n_div)
		rc = zl3073x_write_u32(zldev, ZL_REG_REF_ESYNC_DIV,
				       ref->esync_n_div);
	if (!rc && dref->sync_ctrl != ref->sync_ctrl)
		rc = zl3073x_write_u8(zldev, ZL_REG_REF_SYNC_CTRL,
				      ref->sync_ctrl);
	if (!rc && dref->phase_comp != ref->phase_comp)
		rc = zl3073x_write_u48(zldev, ZL_REG_REF_PHASE_OFFSET_COMP,
				       ref->phase_comp);
	if (rc)
		return rc;

	/* Commit reference configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_WR,
			   ZL_REG_REF_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* After successful commit store new state */
	dref->freq_base = ref->freq_base;
	dref->freq_mult = ref->freq_mult;
	dref->freq_ratio_m = ref->freq_ratio_m;
	dref->freq_ratio_n = ref->freq_ratio_n;
	dref->esync_n_div = ref->esync_n_div;
	dref->sync_ctrl = ref->sync_ctrl;
	dref->phase_comp = ref->phase_comp;

	return 0;
}
