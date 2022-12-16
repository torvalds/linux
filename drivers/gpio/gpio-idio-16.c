// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO library for the ACCES IDIO-16 family
 * Copyright (C) 2022 William Breathitt Gray
 */
#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "gpio-idio-16.h"

#define DEFAULT_SYMBOL_NAMESPACE GPIO_IDIO_16

/**
 * idio_16_get - get signal value at signal offset
 * @reg:	ACCES IDIO-16 device registers
 * @state:	ACCES IDIO-16 device state
 * @offset:	offset of signal to get
 *
 * Returns the signal value (0=low, 1=high) for the signal at @offset.
 */
int idio_16_get(struct idio_16 __iomem *const reg,
		struct idio_16_state *const state, const unsigned long offset)
{
	const unsigned long mask = BIT(offset);

	if (offset < IDIO_16_NOUT)
		return test_bit(offset, state->out_state);

	if (offset < 24)
		return !!(ioread8(&reg->in0_7) & (mask >> IDIO_16_NOUT));

	if (offset < 32)
		return !!(ioread8(&reg->in8_15) & (mask >> 24));

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(idio_16_get);

/**
 * idio_16_get_multiple - get multiple signal values at multiple signal offsets
 * @reg:	ACCES IDIO-16 device registers
 * @state:	ACCES IDIO-16 device state
 * @mask:	mask of signals to get
 * @bits:	bitmap to store signal values
 *
 * Stores in @bits the values (0=low, 1=high) for the signals defined by @mask.
 */
void idio_16_get_multiple(struct idio_16 __iomem *const reg,
			  struct idio_16_state *const state,
			  const unsigned long *const mask,
			  unsigned long *const bits)
{
	unsigned long flags;
	const unsigned long out_mask = GENMASK(IDIO_16_NOUT - 1, 0);

	spin_lock_irqsave(&state->lock, flags);

	bitmap_replace(bits, bits, state->out_state, &out_mask, IDIO_16_NOUT);
	if (*mask & GENMASK(23, 16))
		bitmap_set_value8(bits, ioread8(&reg->in0_7), 16);
	if (*mask & GENMASK(31, 24))
		bitmap_set_value8(bits, ioread8(&reg->in8_15), 24);

	spin_unlock_irqrestore(&state->lock, flags);
}
EXPORT_SYMBOL_GPL(idio_16_get_multiple);

/**
 * idio_16_set - set signal value at signal offset
 * @reg:	ACCES IDIO-16 device registers
 * @state:	ACCES IDIO-16 device state
 * @offset:	offset of signal to set
 * @value:	value of signal to set
 *
 * Assigns output @value for the signal at @offset.
 */
void idio_16_set(struct idio_16 __iomem *const reg,
		 struct idio_16_state *const state, const unsigned long offset,
		 const unsigned long value)
{
	unsigned long flags;

	if (offset >= IDIO_16_NOUT)
		return;

	spin_lock_irqsave(&state->lock, flags);

	__assign_bit(offset, state->out_state, value);
	if (offset < 8)
		iowrite8(bitmap_get_value8(state->out_state, 0), &reg->out0_7);
	else
		iowrite8(bitmap_get_value8(state->out_state, 8), &reg->out8_15);

	spin_unlock_irqrestore(&state->lock, flags);
}
EXPORT_SYMBOL_GPL(idio_16_set);

/**
 * idio_16_set_multiple - set signal values at multiple signal offsets
 * @reg:	ACCES IDIO-16 device registers
 * @state:	ACCES IDIO-16 device state
 * @mask:	mask of signals to set
 * @bits:	bitmap of signal output values
 *
 * Assigns output values defined by @bits for the signals defined by @mask.
 */
void idio_16_set_multiple(struct idio_16 __iomem *const reg,
			  struct idio_16_state *const state,
			  const unsigned long *const mask,
			  const unsigned long *const bits)
{
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	bitmap_replace(state->out_state, state->out_state, bits, mask,
		       IDIO_16_NOUT);
	if (*mask & GENMASK(7, 0))
		iowrite8(bitmap_get_value8(state->out_state, 0), &reg->out0_7);
	if (*mask & GENMASK(15, 8))
		iowrite8(bitmap_get_value8(state->out_state, 8), &reg->out8_15);

	spin_unlock_irqrestore(&state->lock, flags);
}
EXPORT_SYMBOL_GPL(idio_16_set_multiple);

/**
 * idio_16_state_init - initialize idio_16_state structure
 * @state:	ACCES IDIO-16 device state
 *
 * Initializes the ACCES IDIO-16 device @state for use in idio-16 library
 * functions.
 */
void idio_16_state_init(struct idio_16_state *const state)
{
	spin_lock_init(&state->lock);
}
EXPORT_SYMBOL_GPL(idio_16_state_init);

MODULE_AUTHOR("William Breathitt Gray");
MODULE_DESCRIPTION("ACCES IDIO-16 GPIO Library");
MODULE_LICENSE("GPL");
