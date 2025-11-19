// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/dpll.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sprintf.h>

#include "core.h"
#include "dpll.h"
#include "prop.h"
#include "regs.h"

#define ZL3073X_DPLL_REF_NONE		ZL3073X_NUM_REFS
#define ZL3073X_DPLL_REF_IS_VALID(_ref)	((_ref) != ZL3073X_DPLL_REF_NONE)

/**
 * struct zl3073x_dpll_pin - DPLL pin
 * @list: this DPLL pin list entry
 * @dpll: DPLL the pin is registered to
 * @dpll_pin: pointer to registered dpll_pin
 * @label: package label
 * @dir: pin direction
 * @id: pin id
 * @prio: pin priority <0, 14>
 * @selectable: pin is selectable in automatic mode
 * @esync_control: embedded sync is controllable
 * @pin_state: last saved pin state
 * @phase_offset: last saved pin phase offset
 * @freq_offset: last saved fractional frequency offset
 */
struct zl3073x_dpll_pin {
	struct list_head	list;
	struct zl3073x_dpll	*dpll;
	struct dpll_pin		*dpll_pin;
	char			label[8];
	enum dpll_pin_direction	dir;
	u8			id;
	u8			prio;
	bool			selectable;
	bool			esync_control;
	enum dpll_pin_state	pin_state;
	s64			phase_offset;
	s64			freq_offset;
};

/*
 * Supported esync ranges for input and for output per output pair type
 */
static const struct dpll_pin_frequency esync_freq_ranges[] = {
	DPLL_PIN_FREQUENCY_RANGE(0, 1),
};

/**
 * zl3073x_dpll_is_input_pin - check if the pin is input one
 * @pin: pin to check
 *
 * Return: true if pin is input, false if pin is output.
 */
static bool
zl3073x_dpll_is_input_pin(struct zl3073x_dpll_pin *pin)
{
	return pin->dir == DPLL_PIN_DIRECTION_INPUT;
}

/**
 * zl3073x_dpll_is_p_pin - check if the pin is P-pin
 * @pin: pin to check
 *
 * Return: true if the pin is P-pin, false if it is N-pin
 */
static bool
zl3073x_dpll_is_p_pin(struct zl3073x_dpll_pin *pin)
{
	return zl3073x_is_p_pin(pin->id);
}

static int
zl3073x_dpll_pin_direction_get(const struct dpll_pin *dpll_pin, void *pin_priv,
			       const struct dpll_device *dpll, void *dpll_priv,
			       enum dpll_pin_direction *direction,
			       struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	*direction = pin->dir;

	return 0;
}

/**
 * zl3073x_dpll_input_ref_frequency_get - get input reference frequency
 * @zldpll: pointer to zl3073x_dpll
 * @ref_id: reference id
 * @frequency: pointer to variable to store frequency
 *
 * Reads frequency of given input reference.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_input_ref_frequency_get(struct zl3073x_dpll *zldpll, u8 ref_id,
				     u32 *frequency)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u16 base, mult, num, denom;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref_id));
	if (rc)
		return rc;

	/* Read registers to compute resulting frequency */
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_FREQ_BASE, &base);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_FREQ_MULT, &mult);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_RATIO_M, &num);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_REF_RATIO_N, &denom);
	if (rc)
		return rc;

	/* Sanity check that HW has not returned zero denominator */
	if (!denom) {
		dev_err(zldev->dev,
			"Zero divisor for ref %u frequency got from device\n",
			ref_id);
		return -EINVAL;
	}

	/* Compute the frequency */
	*frequency = mul_u64_u32_div(base * mult, num, denom);

	return rc;
}

static int
zl3073x_dpll_input_pin_esync_get(const struct dpll_pin *dpll_pin,
				 void *pin_priv,
				 const struct dpll_device *dpll,
				 void *dpll_priv,
				 struct dpll_pin_esync *esync,
				 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u8 ref, ref_sync_ctrl, sync_mode;
	u32 esync_div, ref_freq;
	int rc;

	/* Get reference frequency */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_dpll_input_ref_frequency_get(zldpll, pin->id, &ref_freq);
	if (rc)
		return rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref));
	if (rc)
		return rc;

	/* Get ref sync mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_SYNC_CTRL, &ref_sync_ctrl);
	if (rc)
		return rc;

	/* Get esync divisor */
	rc = zl3073x_read_u32(zldev, ZL_REG_REF_ESYNC_DIV, &esync_div);
	if (rc)
		return rc;

	sync_mode = FIELD_GET(ZL_REF_SYNC_CTRL_MODE, ref_sync_ctrl);

	switch (sync_mode) {
	case ZL_REF_SYNC_CTRL_MODE_50_50_ESYNC_25_75:
		esync->freq = (esync_div == ZL_REF_ESYNC_DIV_1HZ) ? 1 : 0;
		esync->pulse = 25;
		break;
	default:
		esync->freq = 0;
		esync->pulse = 0;
		break;
	}

	/* If the pin supports esync control expose its range but only
	 * if the current reference frequency is > 1 Hz.
	 */
	if (pin->esync_control && ref_freq > 1) {
		esync->range = esync_freq_ranges;
		esync->range_num = ARRAY_SIZE(esync_freq_ranges);
	} else {
		esync->range = NULL;
		esync->range_num = 0;
	}

	return rc;
}

static int
zl3073x_dpll_input_pin_esync_set(const struct dpll_pin *dpll_pin,
				 void *pin_priv,
				 const struct dpll_device *dpll,
				 void *dpll_priv, u64 freq,
				 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u8 ref, ref_sync_ctrl, sync_mode;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration into mailbox */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref));
	if (rc)
		return rc;

	/* Get ref sync mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_SYNC_CTRL, &ref_sync_ctrl);
	if (rc)
		return rc;

	/* Use freq == 0 to disable esync */
	if (!freq)
		sync_mode = ZL_REF_SYNC_CTRL_MODE_REFSYNC_PAIR_OFF;
	else
		sync_mode = ZL_REF_SYNC_CTRL_MODE_50_50_ESYNC_25_75;

	ref_sync_ctrl &= ~ZL_REF_SYNC_CTRL_MODE;
	ref_sync_ctrl |= FIELD_PREP(ZL_REF_SYNC_CTRL_MODE, sync_mode);

	/* Update ref sync control register */
	rc = zl3073x_write_u8(zldev, ZL_REG_REF_SYNC_CTRL, ref_sync_ctrl);
	if (rc)
		return rc;

	if (freq) {
		/* 1 Hz is only supported frequnecy currently */
		rc = zl3073x_write_u32(zldev, ZL_REG_REF_ESYNC_DIV,
				       ZL_REF_ESYNC_DIV_1HZ);
		if (rc)
			return rc;
	}

	/* Commit reference configuration */
	return zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_WR,
			     ZL_REG_REF_MB_MASK, BIT(ref));
}

static int
zl3073x_dpll_input_pin_ffo_get(const struct dpll_pin *dpll_pin, void *pin_priv,
			       const struct dpll_device *dpll, void *dpll_priv,
			       s64 *ffo, struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	*ffo = pin->freq_offset;

	return 0;
}

static int
zl3073x_dpll_input_pin_frequency_get(const struct dpll_pin *dpll_pin,
				     void *pin_priv,
				     const struct dpll_device *dpll,
				     void *dpll_priv, u64 *frequency,
				     struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u32 ref_freq;
	u8 ref;
	int rc;

	/* Read and return ref frequency */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_dpll_input_ref_frequency_get(zldpll, ref, &ref_freq);
	if (!rc)
		*frequency = ref_freq;

	return rc;
}

static int
zl3073x_dpll_input_pin_frequency_set(const struct dpll_pin *dpll_pin,
				     void *pin_priv,
				     const struct dpll_device *dpll,
				     void *dpll_priv, u64 frequency,
				     struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u16 base, mult;
	u8 ref;
	int rc;

	/* Get base frequency and multiplier for the requested frequency */
	rc = zl3073x_ref_freq_factorize(frequency, &base, &mult);
	if (rc)
		return rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Load reference configuration */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref));

	/* Update base frequency, multiplier, numerator & denominator */
	rc = zl3073x_write_u16(zldev, ZL_REG_REF_FREQ_BASE, base);
	if (rc)
		return rc;
	rc = zl3073x_write_u16(zldev, ZL_REG_REF_FREQ_MULT, mult);
	if (rc)
		return rc;
	rc = zl3073x_write_u16(zldev, ZL_REG_REF_RATIO_M, 1);
	if (rc)
		return rc;
	rc = zl3073x_write_u16(zldev, ZL_REG_REF_RATIO_N, 1);
	if (rc)
		return rc;

	/* Commit reference configuration */
	return zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_WR,
			     ZL_REG_REF_MB_MASK, BIT(ref));
}

/**
 * zl3073x_dpll_selected_ref_get - get currently selected reference
 * @zldpll: pointer to zl3073x_dpll
 * @ref: place to store selected reference
 *
 * Check for currently selected reference the DPLL should be locked to
 * and stores its index to given @ref.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_selected_ref_get(struct zl3073x_dpll *zldpll, u8 *ref)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 state, value;
	int rc;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_AUTO:
		/* For automatic mode read refsel_status register */
		rc = zl3073x_read_u8(zldev,
				     ZL_REG_DPLL_REFSEL_STATUS(zldpll->id),
				     &value);
		if (rc)
			return rc;

		/* Extract reference state */
		state = FIELD_GET(ZL_DPLL_REFSEL_STATUS_STATE, value);

		/* Return the reference only if the DPLL is locked to it */
		if (state == ZL_DPLL_REFSEL_STATUS_STATE_LOCK)
			*ref = FIELD_GET(ZL_DPLL_REFSEL_STATUS_REFSEL, value);
		else
			*ref = ZL3073X_DPLL_REF_NONE;
		break;
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
		/* For manual mode return stored value */
		*ref = zldpll->forced_ref;
		break;
	default:
		/* For other modes like NCO, freerun... there is no input ref */
		*ref = ZL3073X_DPLL_REF_NONE;
		break;
	}

	return 0;
}

/**
 * zl3073x_dpll_selected_ref_set - select reference in manual mode
 * @zldpll: pointer to zl3073x_dpll
 * @ref: input reference to be selected
 *
 * Selects the given reference for the DPLL channel it should be
 * locked to.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_selected_ref_set(struct zl3073x_dpll *zldpll, u8 ref)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 mode, mode_refsel;
	int rc;

	mode = zldpll->refsel_mode;

	switch (mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
		/* Manual mode with ref selected */
		if (ref == ZL3073X_DPLL_REF_NONE) {
			switch (zldpll->lock_status) {
			case DPLL_LOCK_STATUS_LOCKED_HO_ACQ:
			case DPLL_LOCK_STATUS_HOLDOVER:
				/* Switch to forced holdover */
				mode = ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER;
				break;
			default:
				/* Switch to freerun */
				mode = ZL_DPLL_MODE_REFSEL_MODE_FREERUN;
				break;
			}
			/* Keep selected reference */
			ref = zldpll->forced_ref;
		} else if (ref == zldpll->forced_ref) {
			/* No register update - same mode and same ref */
			return 0;
		}
		break;
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER:
		/* Manual mode without no ref */
		if (ref == ZL3073X_DPLL_REF_NONE)
			/* No register update - keep current mode */
			return 0;

		/* Switch to reflock mode and update ref selection */
		mode = ZL_DPLL_MODE_REFSEL_MODE_REFLOCK;
		break;
	default:
		/* For other modes like automatic or NCO ref cannot be selected
		 * manually
		 */
		return -EOPNOTSUPP;
	}

	/* Build mode_refsel value */
	mode_refsel = FIELD_PREP(ZL_DPLL_MODE_REFSEL_MODE, mode) |
		      FIELD_PREP(ZL_DPLL_MODE_REFSEL_REF, ref);

	/* Update dpll_mode_refsel register */
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(zldpll->id),
			      mode_refsel);
	if (rc)
		return rc;

	/* Store new mode and forced reference */
	zldpll->refsel_mode = mode;
	zldpll->forced_ref = ref;

	return rc;
}

/**
 * zl3073x_dpll_connected_ref_get - get currently connected reference
 * @zldpll: pointer to zl3073x_dpll
 * @ref: place to store selected reference
 *
 * Looks for currently connected the DPLL is locked to and stores its index
 * to given @ref.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_connected_ref_get(struct zl3073x_dpll *zldpll, u8 *ref)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	int rc;

	/* Get currently selected input reference */
	rc = zl3073x_dpll_selected_ref_get(zldpll, ref);
	if (rc)
		return rc;

	if (ZL3073X_DPLL_REF_IS_VALID(*ref)) {
		u8 ref_status;

		/* Read the reference monitor status */
		rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(*ref),
				     &ref_status);
		if (rc)
			return rc;

		/* If the monitor indicates an error nothing is connected */
		if (ref_status != ZL_REF_MON_STATUS_OK)
			*ref = ZL3073X_DPLL_REF_NONE;
	}

	return 0;
}

static int
zl3073x_dpll_input_pin_phase_offset_get(const struct dpll_pin *dpll_pin,
					void *pin_priv,
					const struct dpll_device *dpll,
					void *dpll_priv, s64 *phase_offset,
					struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u8 conn_ref, ref, ref_status;
	s64 ref_phase;
	int rc;

	/* Get currently connected reference */
	rc = zl3073x_dpll_connected_ref_get(zldpll, &conn_ref);
	if (rc)
		return rc;

	/* Report phase offset only for currently connected pin if the phase
	 * monitor feature is disabled.
	 */
	ref = zl3073x_input_pin_ref_get(pin->id);
	if (!zldpll->phase_monitor && ref != conn_ref) {
		*phase_offset = 0;

		return 0;
	}

	/* Get this pin monitor status */
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(ref), &ref_status);
	if (rc)
		return rc;

	/* Report phase offset only if the input pin signal is present */
	if (ref_status != ZL_REF_MON_STATUS_OK) {
		*phase_offset = 0;

		return 0;
	}

	ref_phase = pin->phase_offset;

	/* The DPLL being locked to a higher freq than the current ref
	 * the phase offset is modded to the period of the signal
	 * the dpll is locked to.
	 */
	if (ZL3073X_DPLL_REF_IS_VALID(conn_ref) && conn_ref != ref) {
		u32 conn_freq, ref_freq;

		/* Get frequency of connected ref */
		rc = zl3073x_dpll_input_ref_frequency_get(zldpll, conn_ref,
							  &conn_freq);
		if (rc)
			return rc;

		/* Get frequency of given ref */
		rc = zl3073x_dpll_input_ref_frequency_get(zldpll, ref,
							  &ref_freq);
		if (rc)
			return rc;

		if (conn_freq > ref_freq) {
			s64 conn_period, div_factor;

			conn_period = div_s64(PSEC_PER_SEC, conn_freq);
			div_factor = div64_s64(ref_phase, conn_period);
			ref_phase -= conn_period * div_factor;
		}
	}

	*phase_offset = ref_phase * DPLL_PHASE_OFFSET_DIVIDER;

	return rc;
}

static int
zl3073x_dpll_input_pin_phase_adjust_get(const struct dpll_pin *dpll_pin,
					void *pin_priv,
					const struct dpll_device *dpll,
					void *dpll_priv,
					s32 *phase_adjust,
					struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	s64 phase_comp;
	u8 ref;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref));
	if (rc)
		return rc;

	/* Read current phase offset compensation */
	rc = zl3073x_read_u48(zldev, ZL_REG_REF_PHASE_OFFSET_COMP, &phase_comp);
	if (rc)
		return rc;

	/* Perform sign extension for 48bit signed value */
	phase_comp = sign_extend64(phase_comp, 47);

	/* Reverse two's complement negation applied during set and convert
	 * to 32bit signed int
	 */
	*phase_adjust = (s32)-phase_comp;

	return rc;
}

static int
zl3073x_dpll_input_pin_phase_adjust_set(const struct dpll_pin *dpll_pin,
					void *pin_priv,
					const struct dpll_device *dpll,
					void *dpll_priv,
					s32 phase_adjust,
					struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	s64 phase_comp;
	u8 ref;
	int rc;

	/* The value in the register is stored as two's complement negation
	 * of requested value.
	 */
	phase_comp = -phase_adjust;

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(ref));
	if (rc)
		return rc;

	/* Write the requested value into the compensation register */
	rc = zl3073x_write_u48(zldev, ZL_REG_REF_PHASE_OFFSET_COMP, phase_comp);
	if (rc)
		return rc;

	/* Commit reference configuration */
	return zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_WR,
			     ZL_REG_REF_MB_MASK, BIT(ref));
}

/**
 * zl3073x_dpll_ref_prio_get - get priority for given input pin
 * @pin: pointer to pin
 * @prio: place to store priority
 *
 * Reads current priority for the given input pin and stores the value
 * to @prio.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_ref_prio_get(struct zl3073x_dpll_pin *pin, u8 *prio)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, ref_prio;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(zldpll->id));
	if (rc)
		return rc;

	/* Read reference priority - one value for P&N pins (4 bits/pin) */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REF_PRIO(ref / 2),
			     &ref_prio);
	if (rc)
		return rc;

	/* Select nibble according pin type */
	if (zl3073x_dpll_is_p_pin(pin))
		*prio = FIELD_GET(ZL_DPLL_REF_PRIO_REF_P, ref_prio);
	else
		*prio = FIELD_GET(ZL_DPLL_REF_PRIO_REF_N, ref_prio);

	return rc;
}

/**
 * zl3073x_dpll_ref_prio_set - set priority for given input pin
 * @pin: pointer to pin
 * @prio: place to store priority
 *
 * Sets priority for the given input pin.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_ref_prio_set(struct zl3073x_dpll_pin *pin, u8 prio)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, ref_prio;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(zldpll->id));
	if (rc)
		return rc;

	/* Read reference priority - one value shared between P&N pins */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REF_PRIO(ref / 2), &ref_prio);
	if (rc)
		return rc;

	/* Update nibble according pin type */
	if (zl3073x_dpll_is_p_pin(pin)) {
		ref_prio &= ~ZL_DPLL_REF_PRIO_REF_P;
		ref_prio |= FIELD_PREP(ZL_DPLL_REF_PRIO_REF_P, prio);
	} else {
		ref_prio &= ~ZL_DPLL_REF_PRIO_REF_N;
		ref_prio |= FIELD_PREP(ZL_DPLL_REF_PRIO_REF_N, prio);
	}

	/* Update reference priority */
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_REF_PRIO(ref / 2), ref_prio);
	if (rc)
		return rc;

	/* Commit configuration */
	return zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_WR,
			     ZL_REG_DPLL_MB_MASK, BIT(zldpll->id));
}

/**
 * zl3073x_dpll_ref_state_get - get status for given input pin
 * @pin: pointer to pin
 * @state: place to store status
 *
 * Checks current status for the given input pin and stores the value
 * to @state.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_ref_state_get(struct zl3073x_dpll_pin *pin,
			   enum dpll_pin_state *state)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, ref_conn, status;
	int rc;

	ref = zl3073x_input_pin_ref_get(pin->id);

	/* Get currently connected reference */
	rc = zl3073x_dpll_connected_ref_get(zldpll, &ref_conn);
	if (rc)
		return rc;

	if (ref == ref_conn) {
		*state = DPLL_PIN_STATE_CONNECTED;
		return 0;
	}

	/* If the DPLL is running in automatic mode and the reference is
	 * selectable and its monitor does not report any error then report
	 * pin as selectable.
	 */
	if (zldpll->refsel_mode == ZL_DPLL_MODE_REFSEL_MODE_AUTO &&
	    pin->selectable) {
		/* Read reference monitor status */
		rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(ref),
				     &status);
		if (rc)
			return rc;

		/* If the monitor indicates errors report the reference
		 * as disconnected
		 */
		if (status == ZL_REF_MON_STATUS_OK) {
			*state = DPLL_PIN_STATE_SELECTABLE;
			return 0;
		}
	}

	/* Otherwise report the pin as disconnected */
	*state = DPLL_PIN_STATE_DISCONNECTED;

	return 0;
}

static int
zl3073x_dpll_input_pin_state_on_dpll_get(const struct dpll_pin *dpll_pin,
					 void *pin_priv,
					 const struct dpll_device *dpll,
					 void *dpll_priv,
					 enum dpll_pin_state *state,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	return zl3073x_dpll_ref_state_get(pin, state);
}

static int
zl3073x_dpll_input_pin_state_on_dpll_set(const struct dpll_pin *dpll_pin,
					 void *pin_priv,
					 const struct dpll_device *dpll,
					 void *dpll_priv,
					 enum dpll_pin_state state,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u8 new_ref;
	int rc;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER:
		if (state == DPLL_PIN_STATE_CONNECTED) {
			/* Choose the pin as new selected reference */
			new_ref = zl3073x_input_pin_ref_get(pin->id);
		} else if (state == DPLL_PIN_STATE_DISCONNECTED) {
			/* No reference */
			new_ref = ZL3073X_DPLL_REF_NONE;
		} else {
			NL_SET_ERR_MSG_MOD(extack,
					   "Invalid pin state for manual mode");
			return -EINVAL;
		}

		rc = zl3073x_dpll_selected_ref_set(zldpll, new_ref);
		break;

	case ZL_DPLL_MODE_REFSEL_MODE_AUTO:
		if (state == DPLL_PIN_STATE_SELECTABLE) {
			if (pin->selectable)
				return 0; /* Pin is already selectable */

			/* Restore pin priority in HW */
			rc = zl3073x_dpll_ref_prio_set(pin, pin->prio);
			if (rc)
				return rc;

			/* Mark pin as selectable */
			pin->selectable = true;
		} else if (state == DPLL_PIN_STATE_DISCONNECTED) {
			if (!pin->selectable)
				return 0; /* Pin is already disconnected */

			/* Set pin priority to none in HW */
			rc = zl3073x_dpll_ref_prio_set(pin,
						       ZL_DPLL_REF_PRIO_NONE);
			if (rc)
				return rc;

			/* Mark pin as non-selectable */
			pin->selectable = false;
		} else {
			NL_SET_ERR_MSG(extack,
				       "Invalid pin state for automatic mode");
			return -EINVAL;
		}
		break;

	default:
		/* In other modes we cannot change input reference */
		NL_SET_ERR_MSG(extack,
			       "Pin state cannot be changed in current mode");
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

static int
zl3073x_dpll_input_pin_prio_get(const struct dpll_pin *dpll_pin, void *pin_priv,
				const struct dpll_device *dpll, void *dpll_priv,
				u32 *prio, struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	*prio = pin->prio;

	return 0;
}

static int
zl3073x_dpll_input_pin_prio_set(const struct dpll_pin *dpll_pin, void *pin_priv,
				const struct dpll_device *dpll, void *dpll_priv,
				u32 prio, struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;
	int rc;

	if (prio > ZL_DPLL_REF_PRIO_MAX)
		return -EINVAL;

	/* If the pin is selectable then update HW registers */
	if (pin->selectable) {
		rc = zl3073x_dpll_ref_prio_set(pin, prio);
		if (rc)
			return rc;
	}

	/* Save priority */
	pin->prio = prio;

	return 0;
}

static int
zl3073x_dpll_output_pin_esync_get(const struct dpll_pin *dpll_pin,
				  void *pin_priv,
				  const struct dpll_device *dpll,
				  void *dpll_priv,
				  struct dpll_pin_esync *esync,
				  struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	struct device *dev = zldev->dev;
	u32 esync_period, esync_width;
	u8 clock_type, synth;
	u8 out, output_mode;
	u32 output_div;
	u32 synth_freq;
	int rc;

	out = zl3073x_output_pin_out_get(pin->id);

	/* If N-division is enabled, esync is not supported. The register used
	 * for N-division is also used for the esync divider so both cannot
	 * be used.
	 */
	switch (zl3073x_out_signal_format_get(zldev, out)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV:
		return -EOPNOTSUPP;
	default:
		break;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	/* Read output mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_MODE, &output_mode);
	if (rc)
		return rc;

	/* Read output divisor */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_DIV, &output_div);
	if (rc)
		return rc;

	/* Check output divisor for zero */
	if (!output_div) {
		dev_err(dev, "Zero divisor for OUTPUT%u got from device\n",
			out);
		return -EINVAL;
	}

	/* Get synth attached to output pin */
	synth = zl3073x_out_synth_get(zldev, out);

	/* Get synth frequency */
	synth_freq = zl3073x_synth_freq_get(zldev, synth);

	clock_type = FIELD_GET(ZL_OUTPUT_MODE_CLOCK_TYPE, output_mode);
	if (clock_type != ZL_OUTPUT_MODE_CLOCK_TYPE_ESYNC) {
		/* No need to read esync data if it is not enabled */
		esync->freq = 0;
		esync->pulse = 0;

		goto finish;
	}

	/* Read esync period */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD, &esync_period);
	if (rc)
		return rc;

	/* Check esync divisor for zero */
	if (!esync_period) {
		dev_err(dev, "Zero esync divisor for OUTPUT%u got from device\n",
			out);
		return -EINVAL;
	}

	/* Get esync pulse width in units of half synth cycles */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_WIDTH, &esync_width);
	if (rc)
		return rc;

	/* Compute esync frequency */
	esync->freq = synth_freq / output_div / esync_period;

	/* By comparing the esync_pulse_width to the half of the pulse width
	 * the esync pulse percentage can be determined.
	 * Note that half pulse width is in units of half synth cycles, which
	 * is why it reduces down to be output_div.
	 */
	esync->pulse = (50 * esync_width) / output_div;

finish:
	/* Set supported esync ranges if the pin supports esync control and
	 * if the output frequency is > 1 Hz.
	 */
	if (pin->esync_control && (synth_freq / output_div) > 1) {
		esync->range = esync_freq_ranges;
		esync->range_num = ARRAY_SIZE(esync_freq_ranges);
	} else {
		esync->range = NULL;
		esync->range_num = 0;
	}

	return 0;
}

static int
zl3073x_dpll_output_pin_esync_set(const struct dpll_pin *dpll_pin,
				  void *pin_priv,
				  const struct dpll_device *dpll,
				  void *dpll_priv, u64 freq,
				  struct netlink_ext_ack *extack)
{
	u32 esync_period, esync_width, output_div;
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u8 clock_type, out, output_mode, synth;
	u32 synth_freq;
	int rc;

	out = zl3073x_output_pin_out_get(pin->id);

	/* If N-division is enabled, esync is not supported. The register used
	 * for N-division is also used for the esync divider so both cannot
	 * be used.
	 */
	switch (zl3073x_out_signal_format_get(zldev, out)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV:
		return -EOPNOTSUPP;
	default:
		break;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	/* Read output mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_MODE, &output_mode);
	if (rc)
		return rc;

	/* Select clock type */
	if (freq)
		clock_type = ZL_OUTPUT_MODE_CLOCK_TYPE_ESYNC;
	else
		clock_type = ZL_OUTPUT_MODE_CLOCK_TYPE_NORMAL;

	/* Update clock type in output mode */
	output_mode &= ~ZL_OUTPUT_MODE_CLOCK_TYPE;
	output_mode |= FIELD_PREP(ZL_OUTPUT_MODE_CLOCK_TYPE, clock_type);
	rc = zl3073x_write_u8(zldev, ZL_REG_OUTPUT_MODE, output_mode);
	if (rc)
		return rc;

	/* If esync is being disabled just write mailbox and finish */
	if (!freq)
		goto write_mailbox;

	/* Get synth attached to output pin */
	synth = zl3073x_out_synth_get(zldev, out);

	/* Get synth frequency */
	synth_freq = zl3073x_synth_freq_get(zldev, synth);

	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_DIV, &output_div);
	if (rc)
		return rc;

	/* Check output divisor for zero */
	if (!output_div) {
		dev_err(zldev->dev,
			"Zero divisor for OUTPUT%u got from device\n", out);
		return -EINVAL;
	}

	/* Compute and update esync period */
	esync_period = synth_freq / (u32)freq / output_div;
	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD, esync_period);
	if (rc)
		return rc;

	/* Half of the period in units of 1/2 synth cycle can be represented by
	 * the output_div. To get the supported esync pulse width of 25% of the
	 * period the output_div can just be divided by 2. Note that this
	 * assumes that output_div is even, otherwise some resolution will be
	 * lost.
	 */
	esync_width = output_div / 2;
	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_WIDTH, esync_width);
	if (rc)
		return rc;

write_mailbox:
	/* Commit output configuration */
	return zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_WR,
			     ZL_REG_OUTPUT_MB_MASK, BIT(out));
}

static int
zl3073x_dpll_output_pin_frequency_get(const struct dpll_pin *dpll_pin,
				      void *pin_priv,
				      const struct dpll_device *dpll,
				      void *dpll_priv, u64 *frequency,
				      struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	struct device *dev = zldev->dev;
	u8 out, signal_format, synth;
	u32 output_div, synth_freq;
	int rc;

	out = zl3073x_output_pin_out_get(pin->id);
	synth = zl3073x_out_synth_get(zldev, out);
	synth_freq = zl3073x_synth_freq_get(zldev, synth);

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_DIV, &output_div);
	if (rc)
		return rc;

	/* Check output divisor for zero */
	if (!output_div) {
		dev_err(dev, "Zero divisor for output %u got from device\n",
			out);
		return -EINVAL;
	}

	/* Read used signal format for the given output */
	signal_format = zl3073x_out_signal_format_get(zldev, out);

	switch (signal_format) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV:
		/* In case of divided format we have to distiguish between
		 * given output pin type.
		 */
		if (zl3073x_dpll_is_p_pin(pin)) {
			/* For P-pin the resulting frequency is computed as
			 * simple division of synth frequency and output
			 * divisor.
			 */
			*frequency = synth_freq / output_div;
		} else {
			/* For N-pin we have to divide additionally by
			 * divisor stored in esync_period output mailbox
			 * register that is used as N-pin divisor for these
			 * modes.
			 */
			u32 ndiv;

			rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD,
					      &ndiv);
			if (rc)
				return rc;

			/* Check N-pin divisor for zero */
			if (!ndiv) {
				dev_err(dev,
					"Zero N-pin divisor for output %u got from device\n",
					out);
				return -EINVAL;
			}

			/* Compute final divisor for N-pin */
			*frequency = synth_freq / output_div / ndiv;
		}
		break;
	default:
		/* In other modes the resulting frequency is computed as
		 * division of synth frequency and output divisor.
		 */
		*frequency = synth_freq / output_div;
		break;
	}

	return rc;
}

static int
zl3073x_dpll_output_pin_frequency_set(const struct dpll_pin *dpll_pin,
				      void *pin_priv,
				      const struct dpll_device *dpll,
				      void *dpll_priv, u64 frequency,
				      struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	struct device *dev = zldev->dev;
	u32 output_n_freq, output_p_freq;
	u8 out, signal_format, synth;
	u32 cur_div, new_div, ndiv;
	u32 synth_freq;
	int rc;

	out = zl3073x_output_pin_out_get(pin->id);
	synth = zl3073x_out_synth_get(zldev, out);
	synth_freq = zl3073x_synth_freq_get(zldev, synth);
	new_div = synth_freq / (u32)frequency;

	/* Get used signal format for the given output */
	signal_format = zl3073x_out_signal_format_get(zldev, out);

	guard(mutex)(&zldev->multiop_lock);

	/* Load output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	/* Check signal format */
	if (signal_format != ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV &&
	    signal_format != ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV) {
		/* For non N-divided signal formats the frequency is computed
		 * as division of synth frequency and output divisor.
		 */
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_DIV, new_div);
		if (rc)
			return rc;

		/* For 50/50 duty cycle the divisor is equal to width */
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_WIDTH, new_div);
		if (rc)
			return rc;

		/* Commit output configuration */
		return zl3073x_mb_op(zldev,
				     ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_WR,
				     ZL_REG_OUTPUT_MB_MASK, BIT(out));
	}

	/* For N-divided signal format get current divisor */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_DIV, &cur_div);
	if (rc)
		return rc;

	/* Check output divisor for zero */
	if (!cur_div) {
		dev_err(dev, "Zero divisor for output %u got from device\n",
			out);
		return -EINVAL;
	}

	/* Get N-pin divisor (shares the same register with esync */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD, &ndiv);
	if (rc)
		return rc;

	/* Check N-pin divisor for zero */
	if (!ndiv) {
		dev_err(dev,
			"Zero N-pin divisor for output %u got from device\n",
			out);
		return -EINVAL;
	}

	/* Compute current output frequency for P-pin */
	output_p_freq = synth_freq / cur_div;

	/* Compute current N-pin frequency */
	output_n_freq = output_p_freq / ndiv;

	if (zl3073x_dpll_is_p_pin(pin)) {
		/* We are going to change output frequency for P-pin but
		 * if the requested frequency is less than current N-pin
		 * frequency then indicate a failure as we are not able
		 * to compute N-pin divisor to keep its frequency unchanged.
		 */
		if (frequency <= output_n_freq)
			return -EINVAL;

		/* Update the output divisor */
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_DIV, new_div);
		if (rc)
			return rc;

		/* For 50/50 duty cycle the divisor is equal to width */
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_WIDTH, new_div);
		if (rc)
			return rc;

		/* Compute new divisor for N-pin */
		ndiv = (u32)frequency / output_n_freq;
	} else {
		/* We are going to change frequency of N-pin but if
		 * the requested freq is greater or equal than freq of P-pin
		 * in the output pair we cannot compute divisor for the N-pin.
		 * In this case indicate a failure.
		 */
		if (output_p_freq <= frequency)
			return -EINVAL;

		/* Compute new divisor for N-pin */
		ndiv = output_p_freq / (u32)frequency;
	}

	/* Update divisor for the N-pin */
	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD, ndiv);
	if (rc)
		return rc;

	/* For 50/50 duty cycle the divisor is equal to width */
	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_WIDTH, ndiv);
	if (rc)
		return rc;

	/* Commit output configuration */
	return zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_WR,
			     ZL_REG_OUTPUT_MB_MASK, BIT(out));
}

static int
zl3073x_dpll_output_pin_phase_adjust_get(const struct dpll_pin *dpll_pin,
					 void *pin_priv,
					 const struct dpll_device *dpll,
					 void *dpll_priv,
					 s32 *phase_adjust,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	u32 synth_freq;
	s32 phase_comp;
	u8 out, synth;
	int rc;

	out = zl3073x_output_pin_out_get(pin->id);
	synth = zl3073x_out_synth_get(zldev, out);
	synth_freq = zl3073x_synth_freq_get(zldev, synth);

	/* Check synth freq for zero */
	if (!synth_freq) {
		dev_err(zldev->dev, "Got zero synth frequency for output %u\n",
			out);
		return -EINVAL;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	/* Read current output phase compensation */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_PHASE_COMP, &phase_comp);
	if (rc)
		return rc;

	/* Value in register is expressed in half synth clock cycles */
	phase_comp *= (int)div_u64(PSEC_PER_SEC, 2 * synth_freq);

	/* Reverse two's complement negation applied during 'set' */
	*phase_adjust = -phase_comp;

	return rc;
}

static int
zl3073x_dpll_output_pin_phase_adjust_set(const struct dpll_pin *dpll_pin,
					 void *pin_priv,
					 const struct dpll_device *dpll,
					 void *dpll_priv,
					 s32 phase_adjust,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	struct zl3073x_dpll_pin *pin = pin_priv;
	int half_synth_cycle;
	u32 synth_freq;
	u8 out, synth;
	int rc;

	/* Get attached synth */
	out = zl3073x_output_pin_out_get(pin->id);
	synth = zl3073x_out_synth_get(zldev, out);

	/* Get synth's frequency */
	synth_freq = zl3073x_synth_freq_get(zldev, synth);

	/* Value in register is expressed in half synth clock cycles so
	 * the given phase adjustment a multiple of half synth clock.
	 */
	half_synth_cycle = (int)div_u64(PSEC_PER_SEC, 2 * synth_freq);

	if ((phase_adjust % half_synth_cycle) != 0) {
		NL_SET_ERR_MSG_FMT(extack,
				   "Phase adjustment value has to be multiple of %d",
				   half_synth_cycle);
		return -EINVAL;
	}
	phase_adjust /= half_synth_cycle;

	/* The value in the register is stored as two's complement negation
	 * of requested value.
	 */
	phase_adjust = -phase_adjust;

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(out));
	if (rc)
		return rc;

	/* Write the requested value into the compensation register */
	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_PHASE_COMP, phase_adjust);
	if (rc)
		return rc;

	/* Update output configuration from mailbox */
	return zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_WR,
			     ZL_REG_OUTPUT_MB_MASK, BIT(out));
}

static int
zl3073x_dpll_output_pin_state_on_dpll_get(const struct dpll_pin *dpll_pin,
					  void *pin_priv,
					  const struct dpll_device *dpll,
					  void *dpll_priv,
					  enum dpll_pin_state *state,
					  struct netlink_ext_ack *extack)
{
	/* If the output pin is registered then it is always connected */
	*state = DPLL_PIN_STATE_CONNECTED;

	return 0;
}

static int
zl3073x_dpll_lock_status_get(const struct dpll_device *dpll, void *dpll_priv,
			     enum dpll_lock_status *status,
			     enum dpll_lock_status_error *status_error,
			     struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 mon_status, state;
	int rc;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_NCO:
		/* In FREERUN and NCO modes the DPLL is always unlocked */
		*status = DPLL_LOCK_STATUS_UNLOCKED;

		return 0;
	default:
		break;
	}

	/* Read DPLL monitor status */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MON_STATUS(zldpll->id),
			     &mon_status);
	if (rc)
		return rc;
	state = FIELD_GET(ZL_DPLL_MON_STATUS_STATE, mon_status);

	switch (state) {
	case ZL_DPLL_MON_STATUS_STATE_LOCK:
		if (FIELD_GET(ZL_DPLL_MON_STATUS_HO_READY, mon_status))
			*status = DPLL_LOCK_STATUS_LOCKED_HO_ACQ;
		else
			*status = DPLL_LOCK_STATUS_LOCKED;
		break;
	case ZL_DPLL_MON_STATUS_STATE_HOLDOVER:
	case ZL_DPLL_MON_STATUS_STATE_ACQUIRING:
		*status = DPLL_LOCK_STATUS_HOLDOVER;
		break;
	default:
		dev_warn(zldev->dev, "Unknown DPLL monitor status: 0x%02x\n",
			 mon_status);
		*status = DPLL_LOCK_STATUS_UNLOCKED;
		break;
	}

	return 0;
}

static int
zl3073x_dpll_mode_get(const struct dpll_device *dpll, void *dpll_priv,
		      enum dpll_mode *mode, struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER:
	case ZL_DPLL_MODE_REFSEL_MODE_NCO:
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
		/* Use MANUAL for device FREERUN, HOLDOVER, NCO and
		 * REFLOCK modes
		 */
		*mode = DPLL_MODE_MANUAL;
		break;
	case ZL_DPLL_MODE_REFSEL_MODE_AUTO:
		/* Use AUTO for device AUTO mode */
		*mode = DPLL_MODE_AUTOMATIC;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
zl3073x_dpll_phase_offset_avg_factor_get(const struct dpll_device *dpll,
					 void *dpll_priv, u32 *factor,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;

	*factor = zl3073x_dev_phase_avg_factor_get(zldpll->dev);

	return 0;
}

static void
zl3073x_dpll_change_work(struct work_struct *work)
{
	struct zl3073x_dpll *zldpll;

	zldpll = container_of(work, struct zl3073x_dpll, change_work);
	dpll_device_change_ntf(zldpll->dpll_dev);
}

static int
zl3073x_dpll_phase_offset_avg_factor_set(const struct dpll_device *dpll,
					 void *dpll_priv, u32 factor,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *item, *zldpll = dpll_priv;
	int rc;

	if (factor > 15) {
		NL_SET_ERR_MSG_FMT(extack,
				   "Phase offset average factor has to be from range <0,15>");
		return -EINVAL;
	}

	rc = zl3073x_dev_phase_avg_factor_set(zldpll->dev, factor);
	if (rc) {
		NL_SET_ERR_MSG_FMT(extack,
				   "Failed to set phase offset averaging factor");
		return rc;
	}

	/* The averaging factor is common for all DPLL channels so after change
	 * we have to send a notification for other DPLL devices.
	 */
	list_for_each_entry(item, &zldpll->dev->dplls, list) {
		if (item != zldpll)
			schedule_work(&item->change_work);
	}

	return 0;
}

static int
zl3073x_dpll_phase_offset_monitor_get(const struct dpll_device *dpll,
				      void *dpll_priv,
				      enum dpll_feature_state *state,
				      struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;

	if (zldpll->phase_monitor)
		*state = DPLL_FEATURE_STATE_ENABLE;
	else
		*state = DPLL_FEATURE_STATE_DISABLE;

	return 0;
}

static int
zl3073x_dpll_phase_offset_monitor_set(const struct dpll_device *dpll,
				      void *dpll_priv,
				      enum dpll_feature_state state,
				      struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;

	zldpll->phase_monitor = (state == DPLL_FEATURE_STATE_ENABLE);

	return 0;
}

static const struct dpll_pin_ops zl3073x_dpll_input_pin_ops = {
	.direction_get = zl3073x_dpll_pin_direction_get,
	.esync_get = zl3073x_dpll_input_pin_esync_get,
	.esync_set = zl3073x_dpll_input_pin_esync_set,
	.ffo_get = zl3073x_dpll_input_pin_ffo_get,
	.frequency_get = zl3073x_dpll_input_pin_frequency_get,
	.frequency_set = zl3073x_dpll_input_pin_frequency_set,
	.phase_offset_get = zl3073x_dpll_input_pin_phase_offset_get,
	.phase_adjust_get = zl3073x_dpll_input_pin_phase_adjust_get,
	.phase_adjust_set = zl3073x_dpll_input_pin_phase_adjust_set,
	.prio_get = zl3073x_dpll_input_pin_prio_get,
	.prio_set = zl3073x_dpll_input_pin_prio_set,
	.state_on_dpll_get = zl3073x_dpll_input_pin_state_on_dpll_get,
	.state_on_dpll_set = zl3073x_dpll_input_pin_state_on_dpll_set,
};

static const struct dpll_pin_ops zl3073x_dpll_output_pin_ops = {
	.direction_get = zl3073x_dpll_pin_direction_get,
	.esync_get = zl3073x_dpll_output_pin_esync_get,
	.esync_set = zl3073x_dpll_output_pin_esync_set,
	.frequency_get = zl3073x_dpll_output_pin_frequency_get,
	.frequency_set = zl3073x_dpll_output_pin_frequency_set,
	.phase_adjust_get = zl3073x_dpll_output_pin_phase_adjust_get,
	.phase_adjust_set = zl3073x_dpll_output_pin_phase_adjust_set,
	.state_on_dpll_get = zl3073x_dpll_output_pin_state_on_dpll_get,
};

static const struct dpll_device_ops zl3073x_dpll_device_ops = {
	.lock_status_get = zl3073x_dpll_lock_status_get,
	.mode_get = zl3073x_dpll_mode_get,
	.phase_offset_avg_factor_get = zl3073x_dpll_phase_offset_avg_factor_get,
	.phase_offset_avg_factor_set = zl3073x_dpll_phase_offset_avg_factor_set,
	.phase_offset_monitor_get = zl3073x_dpll_phase_offset_monitor_get,
	.phase_offset_monitor_set = zl3073x_dpll_phase_offset_monitor_set,
};

/**
 * zl3073x_dpll_pin_alloc - allocate DPLL pin
 * @zldpll: pointer to zl3073x_dpll
 * @dir: pin direction
 * @id: pin id
 *
 * Allocates and initializes zl3073x_dpll_pin structure for given
 * pin id and direction.
 *
 * Return: pointer to allocated structure on success, error pointer on error
 */
static struct zl3073x_dpll_pin *
zl3073x_dpll_pin_alloc(struct zl3073x_dpll *zldpll, enum dpll_pin_direction dir,
		       u8 id)
{
	struct zl3073x_dpll_pin *pin;

	pin = kzalloc(sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return ERR_PTR(-ENOMEM);

	pin->dpll = zldpll;
	pin->dir = dir;
	pin->id = id;

	return pin;
}

/**
 * zl3073x_dpll_pin_free - deallocate DPLL pin
 * @pin: pin to free
 *
 * Deallocates DPLL pin previously allocated by @zl3073x_dpll_pin_alloc.
 */
static void
zl3073x_dpll_pin_free(struct zl3073x_dpll_pin *pin)
{
	WARN(pin->dpll_pin, "DPLL pin is still registered\n");

	kfree(pin);
}

/**
 * zl3073x_dpll_pin_register - register DPLL pin
 * @pin: pointer to DPLL pin
 * @index: absolute pin index for registration
 *
 * Registers given DPLL pin into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_pin_register(struct zl3073x_dpll_pin *pin, u32 index)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_pin_props *props;
	const struct dpll_pin_ops *ops;
	int rc;

	/* Get pin properties */
	props = zl3073x_pin_props_get(zldpll->dev, pin->dir, pin->id);
	if (IS_ERR(props))
		return PTR_ERR(props);

	/* Save package label & esync capability */
	strscpy(pin->label, props->package_label);
	pin->esync_control = props->esync_control;

	if (zl3073x_dpll_is_input_pin(pin)) {
		rc = zl3073x_dpll_ref_prio_get(pin, &pin->prio);
		if (rc)
			goto err_prio_get;

		if (pin->prio == ZL_DPLL_REF_PRIO_NONE) {
			/* Clamp prio to max value & mark pin non-selectable */
			pin->prio = ZL_DPLL_REF_PRIO_MAX;
			pin->selectable = false;
		} else {
			/* Mark pin as selectable */
			pin->selectable = true;
		}
	}

	/* Create or get existing DPLL pin */
	pin->dpll_pin = dpll_pin_get(zldpll->dev->clock_id, index, THIS_MODULE,
				     &props->dpll_props);
	if (IS_ERR(pin->dpll_pin)) {
		rc = PTR_ERR(pin->dpll_pin);
		goto err_pin_get;
	}

	if (zl3073x_dpll_is_input_pin(pin))
		ops = &zl3073x_dpll_input_pin_ops;
	else
		ops = &zl3073x_dpll_output_pin_ops;

	/* Register the pin */
	rc = dpll_pin_register(zldpll->dpll_dev, pin->dpll_pin, ops, pin);
	if (rc)
		goto err_register;

	/* Free pin properties */
	zl3073x_pin_props_put(props);

	return 0;

err_register:
	dpll_pin_put(pin->dpll_pin);
err_prio_get:
	pin->dpll_pin = NULL;
err_pin_get:
	zl3073x_pin_props_put(props);

	return rc;
}

/**
 * zl3073x_dpll_pin_unregister - unregister DPLL pin
 * @pin: pointer to DPLL pin
 *
 * Unregisters pin previously registered by @zl3073x_dpll_pin_register.
 */
static void
zl3073x_dpll_pin_unregister(struct zl3073x_dpll_pin *pin)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	const struct dpll_pin_ops *ops;

	WARN(!pin->dpll_pin, "DPLL pin is not registered\n");

	if (zl3073x_dpll_is_input_pin(pin))
		ops = &zl3073x_dpll_input_pin_ops;
	else
		ops = &zl3073x_dpll_output_pin_ops;

	/* Unregister the pin */
	dpll_pin_unregister(zldpll->dpll_dev, pin->dpll_pin, ops, pin);

	dpll_pin_put(pin->dpll_pin);
	pin->dpll_pin = NULL;
}

/**
 * zl3073x_dpll_pins_unregister - unregister all registered DPLL pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Enumerates all DPLL pins registered to given DPLL device and
 * unregisters them.
 */
static void
zl3073x_dpll_pins_unregister(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dpll_pin *pin, *next;

	list_for_each_entry_safe(pin, next, &zldpll->pins, list) {
		zl3073x_dpll_pin_unregister(pin);
		list_del(&pin->list);
		zl3073x_dpll_pin_free(pin);
	}
}

/**
 * zl3073x_dpll_pin_is_registrable - check if the pin is registrable
 * @zldpll: pointer to zl3073x_dpll structure
 * @dir: pin direction
 * @index: pin index
 *
 * Checks if the given pin can be registered to given DPLL. For both
 * directions the pin can be registered if it is enabled. In case of
 * differential signal type only P-pin is reported as registrable.
 * And additionally for the output pin, the pin can be registered only
 * if it is connected to synthesizer that is driven by given DPLL.
 *
 * Return: true if the pin is registrable, false if not
 */
static bool
zl3073x_dpll_pin_is_registrable(struct zl3073x_dpll *zldpll,
				enum dpll_pin_direction dir, u8 index)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	bool is_diff, is_enabled;
	const char *name;

	if (dir == DPLL_PIN_DIRECTION_INPUT) {
		u8 ref = zl3073x_input_pin_ref_get(index);

		name = "REF";

		/* Skip the pin if the DPLL is running in NCO mode */
		if (zldpll->refsel_mode == ZL_DPLL_MODE_REFSEL_MODE_NCO)
			return false;

		is_diff = zl3073x_ref_is_diff(zldev, ref);
		is_enabled = zl3073x_ref_is_enabled(zldev, ref);
	} else {
		/* Output P&N pair shares single HW output */
		u8 out = zl3073x_output_pin_out_get(index);

		name = "OUT";

		/* Skip the pin if it is connected to different DPLL channel */
		if (zl3073x_out_dpll_get(zldev, out) != zldpll->id) {
			dev_dbg(zldev->dev,
				"%s%u is driven by different DPLL\n", name,
				out);

			return false;
		}

		is_diff = zl3073x_out_is_diff(zldev, out);
		is_enabled = zl3073x_output_pin_is_enabled(zldev, index);
	}

	/* Skip N-pin if the corresponding input/output is differential */
	if (is_diff && zl3073x_is_n_pin(index)) {
		dev_dbg(zldev->dev, "%s%u is differential, skipping N-pin\n",
			name, index / 2);

		return false;
	}

	/* Skip the pin if it is disabled */
	if (!is_enabled) {
		dev_dbg(zldev->dev, "%s%u%c is disabled\n", name, index / 2,
			zl3073x_is_p_pin(index) ? 'P' : 'N');

		return false;
	}

	return true;
}

/**
 * zl3073x_dpll_pins_register - register all registerable DPLL pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Enumerates all possible input/output pins and registers all of them
 * that are registrable.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_pins_register(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dpll_pin *pin;
	enum dpll_pin_direction dir;
	u8 id, index;
	int rc;

	/* Process input pins */
	for (index = 0; index < ZL3073X_NUM_PINS; index++) {
		/* First input pins and then output pins */
		if (index < ZL3073X_NUM_INPUT_PINS) {
			id = index;
			dir = DPLL_PIN_DIRECTION_INPUT;
		} else {
			id = index - ZL3073X_NUM_INPUT_PINS;
			dir = DPLL_PIN_DIRECTION_OUTPUT;
		}

		/* Check if the pin registrable to this DPLL */
		if (!zl3073x_dpll_pin_is_registrable(zldpll, dir, id))
			continue;

		pin = zl3073x_dpll_pin_alloc(zldpll, dir, id);
		if (IS_ERR(pin)) {
			rc = PTR_ERR(pin);
			goto error;
		}

		rc = zl3073x_dpll_pin_register(pin, index);
		if (rc)
			goto error;

		list_add(&pin->list, &zldpll->pins);
	}

	return 0;

error:
	zl3073x_dpll_pins_unregister(zldpll);

	return rc;
}

/**
 * zl3073x_dpll_device_register - register DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Registers given DPLL device into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_device_register(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 dpll_mode_refsel;
	int rc;

	/* Read DPLL mode and forcibly selected reference */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(zldpll->id),
			     &dpll_mode_refsel);
	if (rc)
		return rc;

	/* Extract mode and selected input reference */
	zldpll->refsel_mode = FIELD_GET(ZL_DPLL_MODE_REFSEL_MODE,
					dpll_mode_refsel);
	zldpll->forced_ref = FIELD_GET(ZL_DPLL_MODE_REFSEL_REF,
				       dpll_mode_refsel);

	zldpll->dpll_dev = dpll_device_get(zldev->clock_id, zldpll->id,
					   THIS_MODULE);
	if (IS_ERR(zldpll->dpll_dev)) {
		rc = PTR_ERR(zldpll->dpll_dev);
		zldpll->dpll_dev = NULL;

		return rc;
	}

	rc = dpll_device_register(zldpll->dpll_dev,
				  zl3073x_prop_dpll_type_get(zldev, zldpll->id),
				  &zl3073x_dpll_device_ops, zldpll);
	if (rc) {
		dpll_device_put(zldpll->dpll_dev);
		zldpll->dpll_dev = NULL;
	}

	return rc;
}

/**
 * zl3073x_dpll_device_unregister - unregister DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Unregisters given DPLL device from DPLL sub-system previously registered
 * by @zl3073x_dpll_device_register.
 */
static void
zl3073x_dpll_device_unregister(struct zl3073x_dpll *zldpll)
{
	WARN(!zldpll->dpll_dev, "DPLL device is not registered\n");

	cancel_work_sync(&zldpll->change_work);

	dpll_device_unregister(zldpll->dpll_dev, &zl3073x_dpll_device_ops,
			       zldpll);
	dpll_device_put(zldpll->dpll_dev);
	zldpll->dpll_dev = NULL;
}

/**
 * zl3073x_dpll_pin_phase_offset_check - check for pin phase offset change
 * @pin: pin to check
 *
 * Check for the change of DPLL to connected pin phase offset change.
 *
 * Return: true on phase offset change, false otherwise
 */
static bool
zl3073x_dpll_pin_phase_offset_check(struct zl3073x_dpll_pin *pin)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	unsigned int reg;
	s64 phase_offset;
	u8 ref;
	int rc;

	ref = zl3073x_input_pin_ref_get(pin->id);

	/* Select register to read phase offset value depending on pin and
	 * phase monitor state:
	 * 1) For connected pin use dpll_phase_err_data register
	 * 2) For other pins use appropriate ref_phase register if the phase
	 *    monitor feature is enabled and reference monitor does not
	 *    report signal errors for given input pin
	 */
	if (pin->pin_state == DPLL_PIN_STATE_CONNECTED) {
		reg = ZL_REG_DPLL_PHASE_ERR_DATA(zldpll->id);
	} else if (zldpll->phase_monitor) {
		u8 status;

		/* Get reference monitor status */
		rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(ref),
				     &status);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to read %s refmon status: %pe\n",
				pin->label, ERR_PTR(rc));

			return false;
		}

		if (status != ZL_REF_MON_STATUS_OK)
			return false;

		reg = ZL_REG_REF_PHASE(ref);
	} else {
		/* The pin is not connected or phase monitor disabled */
		return false;
	}

	/* Read measured phase offset value */
	rc = zl3073x_read_u48(zldev, reg, &phase_offset);
	if (rc) {
		dev_err(zldev->dev, "Failed to read ref phase offset: %pe\n",
			ERR_PTR(rc));

		return false;
	}

	/* Convert to ps */
	phase_offset = div_s64(sign_extend64(phase_offset, 47), 100);

	/* Compare with previous value */
	if (phase_offset != pin->phase_offset) {
		dev_dbg(zldev->dev, "%s phase offset changed: %lld -> %lld\n",
			pin->label, pin->phase_offset, phase_offset);
		pin->phase_offset = phase_offset;

		return true;
	}

	return false;
}

/**
 * zl3073x_dpll_pin_ffo_check - check for pin fractional frequency offset change
 * @pin: pin to check
 *
 * Check for the given pin's fractional frequency change.
 *
 * Return: true on fractional frequency offset change, false otherwise
 */
static bool
zl3073x_dpll_pin_ffo_check(struct zl3073x_dpll_pin *pin)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, status;
	s64 ffo;
	int rc;

	/* Get reference monitor status */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(ref), &status);
	if (rc) {
		dev_err(zldev->dev, "Failed to read %s refmon status: %pe\n",
			pin->label, ERR_PTR(rc));

		return false;
	}

	/* Do not report ffo changes if the reference monitor report errors */
	if (status != ZL_REF_MON_STATUS_OK)
		return false;

	/* Get the latest measured ref's ffo */
	ffo = zl3073x_ref_ffo_get(zldev, ref);

	/* Compare with previous value */
	if (pin->freq_offset != ffo) {
		dev_dbg(zldev->dev, "%s freq offset changed: %lld -> %lld\n",
			pin->label, pin->freq_offset, ffo);
		pin->freq_offset = ffo;

		return true;
	}

	return false;
}

/**
 * zl3073x_dpll_changes_check - check for changes and send notifications
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Checks for changes on given DPLL device and its registered DPLL pins
 * and sends notifications about them.
 *
 * This function is periodically called from @zl3073x_dev_periodic_work.
 */
void
zl3073x_dpll_changes_check(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	enum dpll_lock_status lock_status;
	struct device *dev = zldev->dev;
	struct zl3073x_dpll_pin *pin;
	int rc;

	zldpll->check_count++;

	/* Get current lock status for the DPLL */
	rc = zl3073x_dpll_lock_status_get(zldpll->dpll_dev, zldpll,
					  &lock_status, NULL, NULL);
	if (rc) {
		dev_err(dev, "Failed to get DPLL%u lock status: %pe\n",
			zldpll->id, ERR_PTR(rc));
		return;
	}

	/* If lock status was changed then notify DPLL core */
	if (zldpll->lock_status != lock_status) {
		zldpll->lock_status = lock_status;
		dpll_device_change_ntf(zldpll->dpll_dev);
	}

	/* Input pin monitoring does make sense only in automatic
	 * or forced reference modes.
	 */
	if (zldpll->refsel_mode != ZL_DPLL_MODE_REFSEL_MODE_AUTO &&
	    zldpll->refsel_mode != ZL_DPLL_MODE_REFSEL_MODE_REFLOCK)
		return;

	/* Update phase offset latch registers for this DPLL if the phase
	 * offset monitor feature is enabled.
	 */
	if (zldpll->phase_monitor) {
		rc = zl3073x_ref_phase_offsets_update(zldev, zldpll->id);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to update phase offsets: %pe\n",
				ERR_PTR(rc));
			return;
		}
	}

	list_for_each_entry(pin, &zldpll->pins, list) {
		enum dpll_pin_state state;
		bool pin_changed = false;

		/* Output pins change checks are not necessary because output
		 * states are constant.
		 */
		if (!zl3073x_dpll_is_input_pin(pin))
			continue;

		rc = zl3073x_dpll_ref_state_get(pin, &state);
		if (rc) {
			dev_err(dev,
				"Failed to get %s on DPLL%u state: %pe\n",
				pin->label, zldpll->id, ERR_PTR(rc));
			return;
		}

		if (state != pin->pin_state) {
			dev_dbg(dev, "%s state changed: %u->%u\n", pin->label,
				pin->pin_state, state);
			pin->pin_state = state;
			pin_changed = true;
		}

		/* Check for phase offset and ffo change once per second */
		if (zldpll->check_count % 2 == 0) {
			if (zl3073x_dpll_pin_phase_offset_check(pin))
				pin_changed = true;

			if (zl3073x_dpll_pin_ffo_check(pin))
				pin_changed = true;
		}

		if (pin_changed)
			dpll_pin_change_ntf(pin->dpll_pin);
	}
}

/**
 * zl3073x_dpll_init_fine_phase_adjust - do initial fine phase adjustments
 * @zldev: pointer to zl3073x device
 *
 * Performs initial fine phase adjustments needed per datasheet.
 *
 * Return: 0 on success, <0 on error
 */
int
zl3073x_dpll_init_fine_phase_adjust(struct zl3073x_dev *zldev)
{
	int rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_MASK, 0x1f);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_INTVL, 0x01);
	if (rc)
		return rc;

	rc = zl3073x_write_u16(zldev, ZL_REG_SYNTH_PHASE_SHIFT_DATA, 0xffff);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_CTRL, 0x01);
	if (rc)
		return rc;

	return rc;
}

/**
 * zl3073x_dpll_alloc - allocate DPLL device
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel number
 *
 * Allocates DPLL device structure for given DPLL channel.
 *
 * Return: pointer to DPLL device on success, error pointer on error
 */
struct zl3073x_dpll *
zl3073x_dpll_alloc(struct zl3073x_dev *zldev, u8 ch)
{
	struct zl3073x_dpll *zldpll;

	zldpll = kzalloc(sizeof(*zldpll), GFP_KERNEL);
	if (!zldpll)
		return ERR_PTR(-ENOMEM);

	zldpll->dev = zldev;
	zldpll->id = ch;
	INIT_LIST_HEAD(&zldpll->pins);
	INIT_WORK(&zldpll->change_work, zl3073x_dpll_change_work);

	return zldpll;
}

/**
 * zl3073x_dpll_free - free DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Deallocates given DPLL device previously allocated by @zl3073x_dpll_alloc.
 */
void
zl3073x_dpll_free(struct zl3073x_dpll *zldpll)
{
	WARN(zldpll->dpll_dev, "DPLL device is still registered\n");

	kfree(zldpll);
}

/**
 * zl3073x_dpll_register - register DPLL device and all its pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Registers given DPLL device and all its pins into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
int
zl3073x_dpll_register(struct zl3073x_dpll *zldpll)
{
	int rc;

	rc = zl3073x_dpll_device_register(zldpll);
	if (rc)
		return rc;

	rc = zl3073x_dpll_pins_register(zldpll);
	if (rc) {
		zl3073x_dpll_device_unregister(zldpll);
		return rc;
	}

	return 0;
}

/**
 * zl3073x_dpll_unregister - unregister DPLL device and its pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Unregisters given DPLL device and all its pins from DPLL sub-system
 * previously registered by @zl3073x_dpll_register.
 */
void
zl3073x_dpll_unregister(struct zl3073x_dpll *zldpll)
{
	/* Unregister all pins and dpll */
	zl3073x_dpll_pins_unregister(zldpll);
	zl3073x_dpll_device_unregister(zldpll);
}
