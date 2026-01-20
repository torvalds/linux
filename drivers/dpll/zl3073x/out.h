/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_OUT_H
#define _ZL3073X_OUT_H

#include <linux/bitfield.h>
#include <linux/types.h>

#include "regs.h"

struct zl3073x_dev;

/**
 * struct zl3073x_out - output state
 * @div: output divisor
 * @width: output pulse width
 * @esync_n_period: embedded sync or n-pin period (for n-div formats)
 * @esync_n_width: embedded sync or n-pin pulse width
 * @phase_comp: phase compensation
 * @ctrl: output control
 * @mode: output mode
 */
struct zl3073x_out {
	u32	div;
	u32	width;
	u32	esync_n_period;
	u32	esync_n_width;
	s32	phase_comp;
	u8	ctrl;
	u8	mode;
};

int zl3073x_out_state_fetch(struct zl3073x_dev *zldev, u8 index);
const struct zl3073x_out *zl3073x_out_state_get(struct zl3073x_dev *zldev,
						u8 index);

int zl3073x_out_state_set(struct zl3073x_dev *zldev, u8 index,
			  const struct zl3073x_out *out);

/**
 * zl3073x_out_signal_format_get - get output signal format
 * @out: pointer to out state
 *
 * Return: signal format of given output
 */
static inline u8 zl3073x_out_signal_format_get(const struct zl3073x_out *out)
{
	return FIELD_GET(ZL_OUTPUT_MODE_SIGNAL_FORMAT, out->mode);
}

/**
 * zl3073x_out_is_diff - check if the given output is differential
 * @out: pointer to out state
 *
 * Return: true if output is differential, false if output is single-ended
 */
static inline bool zl3073x_out_is_diff(const struct zl3073x_out *out)
{
	switch (zl3073x_out_signal_format_get(out)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_LVDS:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_DIFF:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_LOWVCM:
		return true;
	default:
		break;
	}

	return false;
}

/**
 * zl3073x_out_is_enabled - check if the given output is enabled
 * @out: pointer to out state
 *
 * Return: true if output is enabled, false if output is disabled
 */
static inline bool zl3073x_out_is_enabled(const struct zl3073x_out *out)
{
	return !!FIELD_GET(ZL_OUTPUT_CTRL_EN, out->ctrl);
}

/**
 * zl3073x_out_synth_get - get synth connected to given output
 * @out: pointer to out state
 *
 * Return: index of synth connected to given output.
 */
static inline u8 zl3073x_out_synth_get(const struct zl3073x_out *out)
{
	return FIELD_GET(ZL_OUTPUT_CTRL_SYNTH_SEL, out->ctrl);
}

#endif /* _ZL3073X_OUT_H */
