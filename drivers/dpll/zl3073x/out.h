/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_OUT_H
#define _ZL3073X_OUT_H

#include <linux/bitfield.h>
#include <linux/stddef.h>
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
 * @mode: output mode
 * @ctrl: output control
 */
struct zl3073x_out {
	struct_group(cfg, /* Config */
		u32	div;
		u32	width;
		u32	esync_n_period;
		u32	esync_n_width;
		s32	phase_comp;
		u8	mode;
	);
	struct_group(inv, /* Invariants */
		u8	ctrl;
	);
};

int zl3073x_out_state_fetch(struct zl3073x_dev *zldev, u8 index);
const struct zl3073x_out *zl3073x_out_state_get(struct zl3073x_dev *zldev,
						u8 index);

int zl3073x_out_state_set(struct zl3073x_dev *zldev, u8 index,
			  const struct zl3073x_out *out);

/**
 * zl3073x_out_clock_type_get - get output clock type
 * @out: pointer to out state
 *
 * Return: clock type of given output (ZL_OUTPUT_MODE_CLOCK_TYPE_*)
 */
static inline u8 zl3073x_out_clock_type_get(const struct zl3073x_out *out)
{
	return FIELD_GET(ZL_OUTPUT_MODE_CLOCK_TYPE, out->mode);
}

/**
 * zl3073x_out_clock_type_set - set output clock type
 * @out: pointer to out state
 * @type: clock type (ZL_OUTPUT_MODE_CLOCK_TYPE_*)
 */
static inline void
zl3073x_out_clock_type_set(struct zl3073x_out *out, u8 type)
{
	FIELD_MODIFY(ZL_OUTPUT_MODE_CLOCK_TYPE, &out->mode, type);
}

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
 * zl3073x_out_is_ndiv - check if the given output is in N-div mode
 * @out: pointer to out state
 *
 * Return: true if output is in N-div mode, false otherwise
 */
static inline bool zl3073x_out_is_ndiv(const struct zl3073x_out *out)
{
	switch (zl3073x_out_signal_format_get(out)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV:
		return true;
	default:
		return false;
	}
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

static inline bool zl3073x_out_esync_is_enabled(const struct zl3073x_out *out)
{
	u8 clock_type = zl3073x_out_clock_type_get(out);

	return clock_type == ZL_OUTPUT_MODE_CLOCK_TYPE_ESYNC;
}

static inline
void zl3073x_out_esync_enable(struct zl3073x_out *out, u32 base_freq)
{
	/* Period is expressed as the number of output divider clock cycles.
	 * So for 1 Hz the period is equal to base frequency.
	 */
	out->esync_n_period = base_freq;

	/* Half of the period in units of 1/2 synth cycle can be represented by
	 * the output_div. To get the supported esync pulse width of 25% of the
	 * period the output_div can just be divided by 2. Note that this
	 * assumes that output_div is even, otherwise some resolution will be
	 * lost.
	 */
	out->esync_n_width = out->div / 2;

	/* Update the clock type */
	zl3073x_out_clock_type_set(out, ZL_OUTPUT_MODE_CLOCK_TYPE_ESYNC);
}

static inline
void zl3073x_out_esync_disable(struct zl3073x_out *out)
{
	/* Just update the clock type. The eSync period and width are
	 * ignored for this clock type.
	 */
	zl3073x_out_clock_type_set(out, ZL_OUTPUT_MODE_CLOCK_TYPE_NORMAL);
}

#endif /* _ZL3073X_OUT_H */
