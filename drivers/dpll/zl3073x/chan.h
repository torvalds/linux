/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_CHAN_H
#define _ZL3073X_CHAN_H

#include <linux/bitfield.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include "regs.h"

struct zl3073x_dev;

/**
 * struct zl3073x_chan - DPLL channel state
 * @mode_refsel: mode and reference selection register value
 * @ref_prio: reference priority registers (4 bits per ref, P/N packed)
 * @mon_status: monitor status register value
 * @refsel_status: reference selection status register value
 */
struct zl3073x_chan {
	struct_group(cfg,
		u8	mode_refsel;
		u8	ref_prio[ZL3073X_NUM_REFS / 2];
	);
	struct_group(stat,
		u8	mon_status;
		u8	refsel_status;
	);
};

int zl3073x_chan_state_fetch(struct zl3073x_dev *zldev, u8 index);
const struct zl3073x_chan *zl3073x_chan_state_get(struct zl3073x_dev *zldev,
						 u8 index);
int zl3073x_chan_state_set(struct zl3073x_dev *zldev, u8 index,
			   const struct zl3073x_chan *chan);

int zl3073x_chan_state_update(struct zl3073x_dev *zldev, u8 index);

/**
 * zl3073x_chan_mode_get - get DPLL channel operating mode
 * @chan: pointer to channel state
 *
 * Return: reference selection mode of the given DPLL channel
 */
static inline u8 zl3073x_chan_mode_get(const struct zl3073x_chan *chan)
{
	return FIELD_GET(ZL_DPLL_MODE_REFSEL_MODE, chan->mode_refsel);
}

/**
 * zl3073x_chan_ref_get - get manually selected reference
 * @chan: pointer to channel state
 *
 * Return: reference selected in forced reference lock mode
 */
static inline u8 zl3073x_chan_ref_get(const struct zl3073x_chan *chan)
{
	return FIELD_GET(ZL_DPLL_MODE_REFSEL_REF, chan->mode_refsel);
}

/**
 * zl3073x_chan_mode_set - set DPLL channel operating mode
 * @chan: pointer to channel state
 * @mode: mode to set
 */
static inline void zl3073x_chan_mode_set(struct zl3073x_chan *chan, u8 mode)
{
	FIELD_MODIFY(ZL_DPLL_MODE_REFSEL_MODE, &chan->mode_refsel, mode);
}

/**
 * zl3073x_chan_ref_set - set manually selected reference
 * @chan: pointer to channel state
 * @ref: reference to set
 */
static inline void zl3073x_chan_ref_set(struct zl3073x_chan *chan, u8 ref)
{
	FIELD_MODIFY(ZL_DPLL_MODE_REFSEL_REF, &chan->mode_refsel, ref);
}

/**
 * zl3073x_chan_ref_prio_get - get reference priority
 * @chan: pointer to channel state
 * @ref: input reference index
 *
 * Return: priority of the given reference <0, 15>
 */
static inline u8
zl3073x_chan_ref_prio_get(const struct zl3073x_chan *chan, u8 ref)
{
	u8 val = chan->ref_prio[ref / 2];

	if (!(ref & 1))
		return FIELD_GET(ZL_DPLL_REF_PRIO_REF_P, val);
	else
		return FIELD_GET(ZL_DPLL_REF_PRIO_REF_N, val);
}

/**
 * zl3073x_chan_ref_prio_set - set reference priority
 * @chan: pointer to channel state
 * @ref: input reference index
 * @prio: priority to set
 */
static inline void
zl3073x_chan_ref_prio_set(struct zl3073x_chan *chan, u8 ref, u8 prio)
{
	u8 *val = &chan->ref_prio[ref / 2];

	if (!(ref & 1))
		FIELD_MODIFY(ZL_DPLL_REF_PRIO_REF_P, val, prio);
	else
		FIELD_MODIFY(ZL_DPLL_REF_PRIO_REF_N, val, prio);
}

/**
 * zl3073x_chan_ref_is_selectable - check if reference is selectable
 * @chan: pointer to channel state
 * @ref: input reference index
 *
 * Return: true if the reference priority is not NONE, false otherwise
 */
static inline bool
zl3073x_chan_ref_is_selectable(const struct zl3073x_chan *chan, u8 ref)
{
	return zl3073x_chan_ref_prio_get(chan, ref) != ZL_DPLL_REF_PRIO_NONE;
}

/**
 * zl3073x_chan_lock_state_get - get DPLL channel lock state
 * @chan: pointer to channel state
 *
 * Return: lock state of the given DPLL channel
 */
static inline u8 zl3073x_chan_lock_state_get(const struct zl3073x_chan *chan)
{
	return FIELD_GET(ZL_DPLL_MON_STATUS_STATE, chan->mon_status);
}

/**
 * zl3073x_chan_is_ho_ready - check if holdover is ready
 * @chan: pointer to channel state
 *
 * Return: true if holdover is ready, false otherwise
 */
static inline bool zl3073x_chan_is_ho_ready(const struct zl3073x_chan *chan)
{
	return !!FIELD_GET(ZL_DPLL_MON_STATUS_HO_READY, chan->mon_status);
}

/**
 * zl3073x_chan_refsel_state_get - get reference selection state
 * @chan: pointer to channel state
 *
 * Return: reference selection state of the given DPLL channel
 */
static inline u8 zl3073x_chan_refsel_state_get(const struct zl3073x_chan *chan)
{
	return FIELD_GET(ZL_DPLL_REFSEL_STATUS_STATE, chan->refsel_status);
}

/**
 * zl3073x_chan_refsel_ref_get - get currently selected reference in auto mode
 * @chan: pointer to channel state
 *
 * Return: reference selected by the DPLL in automatic mode
 */
static inline u8 zl3073x_chan_refsel_ref_get(const struct zl3073x_chan *chan)
{
	return FIELD_GET(ZL_DPLL_REFSEL_STATUS_REFSEL, chan->refsel_status);
}

#endif /* _ZL3073X_CHAN_H */
