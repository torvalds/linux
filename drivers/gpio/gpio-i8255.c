// SPDX-License-Identifier: GPL-2.0
/*
 * Intel 8255 Programmable Peripheral Interface
 * Copyright (C) 2022 William Breathitt Gray
 */
#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "gpio-i8255.h"

#define I8255_CONTROL_PORTC_LOWER_DIRECTION BIT(0)
#define I8255_CONTROL_PORTB_DIRECTION BIT(1)
#define I8255_CONTROL_PORTC_UPPER_DIRECTION BIT(3)
#define I8255_CONTROL_PORTA_DIRECTION BIT(4)
#define I8255_CONTROL_MODE_SET BIT(7)
#define I8255_PORTA 0
#define I8255_PORTB 1
#define I8255_PORTC 2

static int i8255_get_port(struct i8255 __iomem *const ppi,
			  const unsigned long io_port, const unsigned long mask)
{
	const unsigned long bank = io_port / 3;
	const unsigned long ppi_port = io_port % 3;

	return ioread8(&ppi[bank].port[ppi_port]) & mask;
}

static u8 i8255_direction_mask(const unsigned long offset)
{
	const unsigned long port_offset = offset % 8;
	const unsigned long io_port = offset / 8;
	const unsigned long ppi_port = io_port % 3;

	switch (ppi_port) {
	case I8255_PORTA:
		return I8255_CONTROL_PORTA_DIRECTION;
	case I8255_PORTB:
		return I8255_CONTROL_PORTB_DIRECTION;
	case I8255_PORTC:
		/* Port C can be configured by nibble */
		if (port_offset >= 4)
			return I8255_CONTROL_PORTC_UPPER_DIRECTION;
		return I8255_CONTROL_PORTC_LOWER_DIRECTION;
	default:
		/* Should never reach this path */
		return 0;
	}
}

static void i8255_set_port(struct i8255 __iomem *const ppi,
			   struct i8255_state *const state,
			   const unsigned long io_port,
			   const unsigned long mask, const unsigned long bits)
{
	const unsigned long bank = io_port / 3;
	const unsigned long ppi_port = io_port % 3;
	unsigned long flags;
	unsigned long out_state;

	spin_lock_irqsave(&state[bank].lock, flags);

	out_state = ioread8(&ppi[bank].port[ppi_port]);
	out_state = (out_state & ~mask) | (bits & mask);
	iowrite8(out_state, &ppi[bank].port[ppi_port]);

	spin_unlock_irqrestore(&state[bank].lock, flags);
}

/**
 * i8255_direction_input - configure signal offset as input
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @state:	devices states of the respective PPI banks
 * @offset:	signal offset to configure as input
 *
 * Configures a signal @offset as input for the respective Intel 8255
 * Programmable Peripheral Interface (@ppi) banks. The @state control_state
 * values are updated to reflect the new configuration.
 */
void i8255_direction_input(struct i8255 __iomem *const ppi,
			   struct i8255_state *const state,
			   const unsigned long offset)
{
	const unsigned long io_port = offset / 8;
	const unsigned long bank = io_port / 3;
	unsigned long flags;

	spin_lock_irqsave(&state[bank].lock, flags);

	state[bank].control_state |= I8255_CONTROL_MODE_SET;
	state[bank].control_state |= i8255_direction_mask(offset);

	iowrite8(state[bank].control_state, &ppi[bank].control);

	spin_unlock_irqrestore(&state[bank].lock, flags);
}
EXPORT_SYMBOL_NS_GPL(i8255_direction_input, I8255);

/**
 * i8255_direction_output - configure signal offset as output
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @state:	devices states of the respective PPI banks
 * @offset:	signal offset to configure as output
 * @value:	signal value to output
 *
 * Configures a signal @offset as output for the respective Intel 8255
 * Programmable Peripheral Interface (@ppi) banks and sets the respective signal
 * output to the desired @value. The @state control_state values are updated to
 * reflect the new configuration.
 */
void i8255_direction_output(struct i8255 __iomem *const ppi,
			    struct i8255_state *const state,
			    const unsigned long offset,
			    const unsigned long value)
{
	const unsigned long io_port = offset / 8;
	const unsigned long bank = io_port / 3;
	unsigned long flags;

	spin_lock_irqsave(&state[bank].lock, flags);

	state[bank].control_state |= I8255_CONTROL_MODE_SET;
	state[bank].control_state &= ~i8255_direction_mask(offset);

	iowrite8(state[bank].control_state, &ppi[bank].control);

	spin_unlock_irqrestore(&state[bank].lock, flags);

	i8255_set(ppi, state, offset, value);
}
EXPORT_SYMBOL_NS_GPL(i8255_direction_output, I8255);

/**
 * i8255_get - get signal value at signal offset
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @offset:	offset of signal to get
 *
 * Returns the signal value (0=low, 1=high) for the signal at @offset for the
 * respective Intel 8255 Programmable Peripheral Interface (@ppi) banks.
 */
int i8255_get(struct i8255 __iomem *const ppi, const unsigned long offset)
{
	const unsigned long io_port = offset / 8;
	const unsigned long offset_mask = BIT(offset % 8);

	return !!i8255_get_port(ppi, io_port, offset_mask);
}
EXPORT_SYMBOL_NS_GPL(i8255_get, I8255);

/**
 * i8255_get_direction - get the I/O direction for a signal offset
 * @state:	devices states of the respective PPI banks
 * @offset:	offset of signal to get direction
 *
 * Returns the signal direction (0=output, 1=input) for the signal at @offset.
 */
int i8255_get_direction(const struct i8255_state *const state,
			const unsigned long offset)
{
	const unsigned long io_port = offset / 8;
	const unsigned long bank = io_port / 3;

	return !!(state[bank].control_state & i8255_direction_mask(offset));
}
EXPORT_SYMBOL_NS_GPL(i8255_get_direction, I8255);

/**
 * i8255_get_multiple - get multiple signal values at multiple signal offsets
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @mask:	mask of signals to get
 * @bits:	bitmap to store signal values
 * @ngpio:	number of GPIO signals of the respective PPI banks
 *
 * Stores in @bits the values (0=low, 1=high) for the signals defined by @mask
 * for the respective Intel 8255 Programmable Peripheral Interface (@ppi) banks.
 */
void i8255_get_multiple(struct i8255 __iomem *const ppi,
			const unsigned long *const mask,
			unsigned long *const bits, const unsigned long ngpio)
{
	unsigned long offset;
	unsigned long port_mask;
	unsigned long io_port;
	unsigned long port_state;

	bitmap_zero(bits, ngpio);

	for_each_set_clump8(offset, port_mask, mask, ngpio) {
		io_port = offset / 8;
		port_state = i8255_get_port(ppi, io_port, port_mask);

		bitmap_set_value8(bits, port_state, offset);
	}
}
EXPORT_SYMBOL_NS_GPL(i8255_get_multiple, I8255);

/**
 * i8255_mode0_output - configure all PPI ports to MODE 0 output mode
 * @ppi:	Intel 8255 Programmable Peripheral Interface bank
 *
 * Configures all Intel 8255 Programmable Peripheral Interface (@ppi) ports to
 * MODE 0 (Basic Input/Output) output mode.
 */
void i8255_mode0_output(struct i8255 __iomem *const ppi)
{
	iowrite8(I8255_CONTROL_MODE_SET, &ppi->control);
}
EXPORT_SYMBOL_NS_GPL(i8255_mode0_output, I8255);

/**
 * i8255_set - set signal value at signal offset
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @state:	devices states of the respective PPI banks
 * @offset:	offset of signal to set
 * @value:	value of signal to set
 *
 * Assigns output @value for the signal at @offset for the respective Intel 8255
 * Programmable Peripheral Interface (@ppi) banks.
 */
void i8255_set(struct i8255 __iomem *const ppi, struct i8255_state *const state,
	       const unsigned long offset, const unsigned long value)
{
	const unsigned long io_port = offset / 8;
	const unsigned long port_offset = offset % 8;
	const unsigned long mask = BIT(port_offset);
	const unsigned long bits = value << port_offset;

	i8255_set_port(ppi, state, io_port, mask, bits);
}
EXPORT_SYMBOL_NS_GPL(i8255_set, I8255);

/**
 * i8255_set_multiple - set signal values at multiple signal offsets
 * @ppi:	Intel 8255 Programmable Peripheral Interface banks
 * @state:	devices states of the respective PPI banks
 * @mask:	mask of signals to set
 * @bits:	bitmap of signal output values
 * @ngpio:	number of GPIO signals of the respective PPI banks
 *
 * Assigns output values defined by @bits for the signals defined by @mask for
 * the respective Intel 8255 Programmable Peripheral Interface (@ppi) banks.
 */
void i8255_set_multiple(struct i8255 __iomem *const ppi,
			struct i8255_state *const state,
			const unsigned long *const mask,
			const unsigned long *const bits,
			const unsigned long ngpio)
{
	unsigned long offset;
	unsigned long port_mask;
	unsigned long io_port;
	unsigned long value;

	for_each_set_clump8(offset, port_mask, mask, ngpio) {
		io_port = offset / 8;
		value = bitmap_get_value8(bits, offset);
		i8255_set_port(ppi, state, io_port, port_mask, value);
	}
}
EXPORT_SYMBOL_NS_GPL(i8255_set_multiple, I8255);

/**
 * i8255_state_init - initialize i8255_state structure
 * @state:	devices states of the respective PPI banks
 * @nbanks:	number of Intel 8255 Programmable Peripheral Interface banks
 *
 * Initializes the @state of each Intel 8255 Programmable Peripheral Interface
 * bank for use in i8255 library functions.
 */
void i8255_state_init(struct i8255_state *const state,
		      const unsigned long nbanks)
{
	unsigned long bank;

	for (bank = 0; bank < nbanks; bank++)
		spin_lock_init(&state[bank].lock);
}
EXPORT_SYMBOL_NS_GPL(i8255_state_init, I8255);

MODULE_AUTHOR("William Breathitt Gray");
MODULE_DESCRIPTION("Intel 8255 Programmable Peripheral Interface");
MODULE_LICENSE("GPL");
