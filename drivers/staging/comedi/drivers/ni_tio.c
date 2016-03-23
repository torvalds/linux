/*
 * Support for NI general purpose counters
 *
 * Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Module: ni_tio
 * Description: National Instruments general purpose counters
 * Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
 *         Herman.Bruyninckx@mech.kuleuven.ac.be,
 *         Wim.Meeussen@mech.kuleuven.ac.be,
 *         Klaas.Gadeyne@mech.kuleuven.ac.be,
 *         Frank Mori Hess <fmhess@users.sourceforge.net>
 * Updated: Thu Nov 16 09:50:32 EST 2006
 * Status: works
 *
 * This module is not used directly by end-users.  Rather, it
 * is used by other drivers (for example ni_660x and ni_pcimio)
 * to provide support for NI's general purpose counters.  It was
 * originally based on the counter code from ni_660x.c and
 * ni_mio_common.c.
 *
 * References:
 * DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
 * DAQ 6601/6602 User Manual (NI 322137B-01)
 * 340934b.pdf  DAQ-STC reference manual
 *
 * TODO: Support use of both banks X and Y
 */

#include <linux/module.h>
#include <linux/slab.h>

#include "ni_tio_internal.h"

/*
 * clock sources for ni e and m series boards,
 * get bits with GI_SRC_SEL()
 */
#define NI_M_TIMEBASE_1_CLK		0x0	/* 20MHz */
#define NI_M_PFI_CLK(x)			(((x) < 10) ? (1 + (x)) : (0xb + (x)))
#define NI_M_RTSI_CLK(x)		(((x) == 7) ? 0x1b : (0xb + (x)))
#define NI_M_TIMEBASE_2_CLK		0x12	/* 100KHz */
#define NI_M_NEXT_TC_CLK		0x13
#define NI_M_NEXT_GATE_CLK		0x14	/* Gi_Src_SubSelect=0 */
#define NI_M_PXI_STAR_TRIGGER_CLK	0x14	/* Gi_Src_SubSelect=1 */
#define NI_M_PXI10_CLK			0x1d
#define NI_M_TIMEBASE_3_CLK		0x1e	/* 80MHz, Gi_Src_SubSelect=0 */
#define NI_M_ANALOG_TRIGGER_OUT_CLK	0x1e	/* Gi_Src_SubSelect=1 */
#define NI_M_LOGIC_LOW_CLK		0x1f
#define NI_M_MAX_PFI_CHAN		15
#define NI_M_MAX_RTSI_CHAN		7

/*
 * clock sources for ni_660x boards,
 * get bits with GI_SRC_SEL()
 */
#define NI_660X_TIMEBASE_1_CLK		0x0	/* 20MHz */
#define NI_660X_SRC_PIN_I_CLK		0x1
#define NI_660X_SRC_PIN_CLK(x)		(0x2 + (x))
#define NI_660X_NEXT_GATE_CLK		0xa
#define NI_660X_RTSI_CLK(x)		(0xb + (x))
#define NI_660X_TIMEBASE_2_CLK		0x12	/* 100KHz */
#define NI_660X_NEXT_TC_CLK		0x13
#define NI_660X_TIMEBASE_3_CLK		0x1e	/* 80MHz */
#define NI_660X_LOGIC_LOW_CLK		0x1f
#define NI_660X_MAX_SRC_PIN		7
#define NI_660X_MAX_RTSI_CHAN		6

/* ni m series gate_select */
#define NI_M_TIMESTAMP_MUX_GATE_SEL	0x0
#define NI_M_PFI_GATE_SEL(x)		(((x) < 10) ? (1 + (x)) : (0xb + (x)))
#define NI_M_RTSI_GATE_SEL(x)		(((x) == 7) ? 0x1b : (0xb + (x)))
#define NI_M_AI_START2_GATE_SEL		0x12
#define NI_M_PXI_STAR_TRIGGER_GATE_SEL	0x13
#define NI_M_NEXT_OUT_GATE_SEL		0x14
#define NI_M_AI_START1_GATE_SEL		0x1c
#define NI_M_NEXT_SRC_GATE_SEL		0x1d
#define NI_M_ANALOG_TRIG_OUT_GATE_SEL	0x1e
#define NI_M_LOGIC_LOW_GATE_SEL		0x1f

/* ni_660x gate select */
#define NI_660X_SRC_PIN_I_GATE_SEL	0x0
#define NI_660X_GATE_PIN_I_GATE_SEL	0x1
#define NI_660X_PIN_GATE_SEL(x)		(0x2 + (x))
#define NI_660X_NEXT_SRC_GATE_SEL	0xa
#define NI_660X_RTSI_GATE_SEL(x)	(0xb + (x))
#define NI_660X_NEXT_OUT_GATE_SEL	0x14
#define NI_660X_LOGIC_LOW_GATE_SEL	0x1f
#define NI_660X_MAX_GATE_PIN		7

/* ni_660x second gate select */
#define NI_660X_SRC_PIN_I_GATE2_SEL	0x0
#define NI_660X_UD_PIN_I_GATE2_SEL	0x1
#define NI_660X_UD_PIN_GATE2_SEL(x)	(0x2 + (x))
#define NI_660X_NEXT_SRC_GATE2_SEL	0xa
#define NI_660X_RTSI_GATE2_SEL(x)	(0xb + (x))
#define NI_660X_NEXT_OUT_GATE2_SEL	0x14
#define NI_660X_SELECTED_GATE2_SEL	0x1e
#define NI_660X_LOGIC_LOW_GATE2_SEL	0x1f
#define NI_660X_MAX_UP_DOWN_PIN		7

static inline unsigned int GI_ALT_SYNC(enum ni_gpct_variant variant)
{
	switch (variant) {
	case ni_gpct_variant_e_series:
	default:
		return 0;
	case ni_gpct_variant_m_series:
		return GI_M_ALT_SYNC;
	case ni_gpct_variant_660x:
		return GI_660X_ALT_SYNC;
	}
}

static inline unsigned int GI_PRESCALE_X2(enum ni_gpct_variant variant)
{
	switch (variant) {
	case ni_gpct_variant_e_series:
	default:
		return 0;
	case ni_gpct_variant_m_series:
		return GI_M_PRESCALE_X2;
	case ni_gpct_variant_660x:
		return GI_660X_PRESCALE_X2;
	}
}

static inline unsigned int GI_PRESCALE_X8(enum ni_gpct_variant variant)
{
	switch (variant) {
	case ni_gpct_variant_e_series:
	default:
		return 0;
	case ni_gpct_variant_m_series:
		return GI_M_PRESCALE_X8;
	case ni_gpct_variant_660x:
		return GI_660X_PRESCALE_X8;
	}
}

static inline unsigned int GI_HW_ARM_SEL_MASK(enum ni_gpct_variant variant)
{
	switch (variant) {
	case ni_gpct_variant_e_series:
	default:
		return 0;
	case ni_gpct_variant_m_series:
		return GI_M_HW_ARM_SEL_MASK;
	case ni_gpct_variant_660x:
		return GI_660X_HW_ARM_SEL_MASK;
	}
}

static bool ni_tio_has_gate2_registers(const struct ni_gpct_device *counter_dev)
{
	switch (counter_dev->variant) {
	case ni_gpct_variant_e_series:
	default:
		return false;
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		return true;
	}
}

/**
 * ni_tio_write() - Write a TIO register using the driver provided callback.
 * @counter: struct ni_gpct counter.
 * @value: the value to write
 * @reg: the register to write.
 */
void ni_tio_write(struct ni_gpct *counter, unsigned int value,
		  enum ni_gpct_register reg)
{
	if (reg < NITIO_NUM_REGS)
		counter->counter_dev->write(counter, value, reg);
}
EXPORT_SYMBOL_GPL(ni_tio_write);

/**
 * ni_tio_read() - Read a TIO register using the driver provided callback.
 * @counter: struct ni_gpct counter.
 * @reg: the register to read.
 */
unsigned int ni_tio_read(struct ni_gpct *counter, enum ni_gpct_register reg)
{
	if (reg < NITIO_NUM_REGS)
		return counter->counter_dev->read(counter, reg);
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_read);

static void ni_tio_reset_count_and_disarm(struct ni_gpct *counter)
{
	unsigned int cidx = counter->counter_index;

	ni_tio_write(counter, GI_RESET(cidx), NITIO_RESET_REG(cidx));
}

static u64 ni_tio_clock_period_ps(const struct ni_gpct *counter,
				  unsigned int generic_clock_source)
{
	u64 clock_period_ps;

	switch (generic_clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK) {
	case NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS:
		clock_period_ps = 50000;
		break;
	case NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS:
		clock_period_ps = 10000000;
		break;
	case NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS:
		clock_period_ps = 12500;
		break;
	case NI_GPCT_PXI10_CLOCK_SRC_BITS:
		clock_period_ps = 100000;
		break;
	default:
		/*
		 * clock period is specified by user with prescaling
		 * already taken into account.
		 */
		return counter->clock_period_ps;
	}

	switch (generic_clock_source & NI_GPCT_PRESCALE_MODE_CLOCK_SRC_MASK) {
	case NI_GPCT_NO_PRESCALE_CLOCK_SRC_BITS:
		break;
	case NI_GPCT_PRESCALE_X2_CLOCK_SRC_BITS:
		clock_period_ps *= 2;
		break;
	case NI_GPCT_PRESCALE_X8_CLOCK_SRC_BITS:
		clock_period_ps *= 8;
		break;
	default:
		BUG();
		break;
	}
	return clock_period_ps;
}

static void ni_tio_set_bits_transient(struct ni_gpct *counter,
				      enum ni_gpct_register reg,
				      unsigned int mask, unsigned int value,
				      unsigned int transient)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned long flags;

	if (reg < NITIO_NUM_REGS) {
		spin_lock_irqsave(&counter_dev->regs_lock, flags);
		counter_dev->regs[reg] &= ~mask;
		counter_dev->regs[reg] |= (value & mask);
		ni_tio_write(counter, counter_dev->regs[reg] | transient, reg);
		mmiowb();
		spin_unlock_irqrestore(&counter_dev->regs_lock, flags);
	}
}

/**
 * ni_tio_set_bits() - Safely write a counter register.
 * @counter: struct ni_gpct counter.
 * @reg: the register to write.
 * @mask: the bits to change.
 * @value: the new bits value.
 *
 * Used to write to, and update the software copy, a register whose bits may
 * be twiddled in interrupt context, or whose software copy may be read in
 * interrupt context.
 */
void ni_tio_set_bits(struct ni_gpct *counter, enum ni_gpct_register reg,
		     unsigned int mask, unsigned int value)
{
	ni_tio_set_bits_transient(counter, reg, mask, value, 0x0);
}
EXPORT_SYMBOL_GPL(ni_tio_set_bits);

/**
 * ni_tio_get_soft_copy() - Safely read the software copy of a counter register.
 * @counter: struct ni_gpct counter.
 * @reg: the register to read.
 *
 * Used to get the software copy of a register whose bits might be modified
 * in interrupt context, or whose software copy might need to be read in
 * interrupt context.
 */
unsigned int ni_tio_get_soft_copy(const struct ni_gpct *counter,
				  enum ni_gpct_register reg)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int value = 0;
	unsigned long flags;

	if (reg < NITIO_NUM_REGS) {
		spin_lock_irqsave(&counter_dev->regs_lock, flags);
		value = counter_dev->regs[reg];
		spin_unlock_irqrestore(&counter_dev->regs_lock, flags);
	}
	return value;
}
EXPORT_SYMBOL_GPL(ni_tio_get_soft_copy);

static unsigned int ni_tio_clock_src_modifiers(const struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int counting_mode_bits =
		ni_tio_get_soft_copy(counter, NITIO_CNT_MODE_REG(cidx));
	unsigned int bits = 0;

	if (ni_tio_get_soft_copy(counter, NITIO_INPUT_SEL_REG(cidx)) &
	    GI_SRC_POL_INVERT)
		bits |= NI_GPCT_INVERT_CLOCK_SRC_BIT;
	if (counting_mode_bits & GI_PRESCALE_X2(counter_dev->variant))
		bits |= NI_GPCT_PRESCALE_X2_CLOCK_SRC_BITS;
	if (counting_mode_bits & GI_PRESCALE_X8(counter_dev->variant))
		bits |= NI_GPCT_PRESCALE_X8_CLOCK_SRC_BITS;
	return bits;
}

static unsigned int ni_m_series_clock_src_select(const struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int second_gate_reg = NITIO_GATE2_REG(cidx);
	unsigned int clock_source = 0;
	unsigned int src;
	unsigned int i;

	src = GI_BITS_TO_SRC(ni_tio_get_soft_copy(counter,
						  NITIO_INPUT_SEL_REG(cidx)));

	switch (src) {
	case NI_M_TIMEBASE_1_CLK:
		clock_source = NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS;
		break;
	case NI_M_TIMEBASE_2_CLK:
		clock_source = NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS;
		break;
	case NI_M_TIMEBASE_3_CLK:
		if (counter_dev->regs[second_gate_reg] & GI_SRC_SUBSEL)
			clock_source =
			    NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS;
		else
			clock_source = NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS;
		break;
	case NI_M_LOGIC_LOW_CLK:
		clock_source = NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS;
		break;
	case NI_M_NEXT_GATE_CLK:
		if (counter_dev->regs[second_gate_reg] & GI_SRC_SUBSEL)
			clock_source = NI_GPCT_PXI_STAR_TRIGGER_CLOCK_SRC_BITS;
		else
			clock_source = NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS;
		break;
	case NI_M_PXI10_CLK:
		clock_source = NI_GPCT_PXI10_CLOCK_SRC_BITS;
		break;
	case NI_M_NEXT_TC_CLK:
		clock_source = NI_GPCT_NEXT_TC_CLOCK_SRC_BITS;
		break;
	default:
		for (i = 0; i <= NI_M_MAX_RTSI_CHAN; ++i) {
			if (src == NI_M_RTSI_CLK(i)) {
				clock_source = NI_GPCT_RTSI_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (src == NI_M_PFI_CLK(i)) {
				clock_source = NI_GPCT_PFI_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_M_MAX_PFI_CHAN)
			break;
		BUG();
		break;
	}
	clock_source |= ni_tio_clock_src_modifiers(counter);
	return clock_source;
}

static unsigned int ni_660x_clock_src_select(const struct ni_gpct *counter)
{
	unsigned int clock_source = 0;
	unsigned int cidx = counter->counter_index;
	unsigned int src;
	unsigned int i;

	src = GI_BITS_TO_SRC(ni_tio_get_soft_copy(counter,
						  NITIO_INPUT_SEL_REG(cidx)));

	switch (src) {
	case NI_660X_TIMEBASE_1_CLK:
		clock_source = NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS;
		break;
	case NI_660X_TIMEBASE_2_CLK:
		clock_source = NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS;
		break;
	case NI_660X_TIMEBASE_3_CLK:
		clock_source = NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS;
		break;
	case NI_660X_LOGIC_LOW_CLK:
		clock_source = NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS;
		break;
	case NI_660X_SRC_PIN_I_CLK:
		clock_source = NI_GPCT_SOURCE_PIN_i_CLOCK_SRC_BITS;
		break;
	case NI_660X_NEXT_GATE_CLK:
		clock_source = NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS;
		break;
	case NI_660X_NEXT_TC_CLK:
		clock_source = NI_GPCT_NEXT_TC_CLOCK_SRC_BITS;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (src == NI_660X_RTSI_CLK(i)) {
				clock_source = NI_GPCT_RTSI_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_SRC_PIN; ++i) {
			if (src == NI_660X_SRC_PIN_CLK(i)) {
				clock_source =
				    NI_GPCT_SOURCE_PIN_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_SRC_PIN)
			break;
		BUG();
		break;
	}
	clock_source |= ni_tio_clock_src_modifiers(counter);
	return clock_source;
}

static unsigned int
ni_tio_generic_clock_src_select(const struct ni_gpct *counter)
{
	switch (counter->counter_dev->variant) {
	case ni_gpct_variant_e_series:
	case ni_gpct_variant_m_series:
	default:
		return ni_m_series_clock_src_select(counter);
	case ni_gpct_variant_660x:
		return ni_660x_clock_src_select(counter);
	}
}

static void ni_tio_set_sync_mode(struct ni_gpct *counter, int force_alt_sync)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int counting_mode_reg = NITIO_CNT_MODE_REG(cidx);
	static const u64 min_normal_sync_period_ps = 25000;
	unsigned int mode;
	u64 clock_period_ps;

	if (!ni_tio_counting_mode_registers_present(counter_dev))
		return;

	mode = ni_tio_get_soft_copy(counter, counting_mode_reg);
	switch (mode & GI_CNT_MODE_MASK) {
	case GI_CNT_MODE_QUADX1:
	case GI_CNT_MODE_QUADX2:
	case GI_CNT_MODE_QUADX4:
	case GI_CNT_MODE_SYNC_SRC:
		force_alt_sync = 1;
		break;
	default:
		break;
	}

	clock_period_ps = ni_tio_clock_period_ps(counter,
				ni_tio_generic_clock_src_select(counter));

	/*
	 * It's not clear what we should do if clock_period is unknown, so we
	 * are not using the alt sync bit in that case, but allow the caller
	 * to decide by using the force_alt_sync parameter.
	 */
	if (force_alt_sync ||
	    (clock_period_ps && clock_period_ps < min_normal_sync_period_ps)) {
		ni_tio_set_bits(counter, counting_mode_reg,
				GI_ALT_SYNC(counter_dev->variant),
				GI_ALT_SYNC(counter_dev->variant));
	} else {
		ni_tio_set_bits(counter, counting_mode_reg,
				GI_ALT_SYNC(counter_dev->variant),
				0x0);
	}
}

static int ni_tio_set_counter_mode(struct ni_gpct *counter, unsigned int mode)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int mode_reg_mask;
	unsigned int mode_reg_values;
	unsigned int input_select_bits = 0;
	/* these bits map directly on to the mode register */
	static const unsigned int mode_reg_direct_mask =
	    NI_GPCT_GATE_ON_BOTH_EDGES_BIT | NI_GPCT_EDGE_GATE_MODE_MASK |
	    NI_GPCT_STOP_MODE_MASK | NI_GPCT_OUTPUT_MODE_MASK |
	    NI_GPCT_HARDWARE_DISARM_MASK | NI_GPCT_LOADING_ON_TC_BIT |
	    NI_GPCT_LOADING_ON_GATE_BIT | NI_GPCT_LOAD_B_SELECT_BIT;

	mode_reg_mask = mode_reg_direct_mask | GI_RELOAD_SRC_SWITCHING;
	mode_reg_values = mode & mode_reg_direct_mask;
	switch (mode & NI_GPCT_RELOAD_SOURCE_MASK) {
	case NI_GPCT_RELOAD_SOURCE_FIXED_BITS:
		break;
	case NI_GPCT_RELOAD_SOURCE_SWITCHING_BITS:
		mode_reg_values |= GI_RELOAD_SRC_SWITCHING;
		break;
	case NI_GPCT_RELOAD_SOURCE_GATE_SELECT_BITS:
		input_select_bits |= GI_GATE_SEL_LOAD_SRC;
		mode_reg_mask |= GI_GATING_MODE_MASK;
		mode_reg_values |= GI_LEVEL_GATING;
		break;
	default:
		break;
	}
	ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
			mode_reg_mask, mode_reg_values);

	if (ni_tio_counting_mode_registers_present(counter_dev)) {
		unsigned int bits = 0;

		bits |= GI_CNT_MODE(mode >> NI_GPCT_COUNTING_MODE_SHIFT);
		bits |= GI_INDEX_PHASE((mode >> NI_GPCT_INDEX_PHASE_BITSHIFT));
		if (mode & NI_GPCT_INDEX_ENABLE_BIT)
			bits |= GI_INDEX_MODE;
		ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx),
				GI_CNT_MODE_MASK | GI_INDEX_PHASE_MASK |
				GI_INDEX_MODE, bits);
		ni_tio_set_sync_mode(counter, 0);
	}

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx), GI_CNT_DIR_MASK,
			GI_CNT_DIR(mode >> NI_GPCT_COUNTING_DIRECTION_SHIFT));

	if (mode & NI_GPCT_OR_GATE_BIT)
		input_select_bits |= GI_OR_GATE;
	if (mode & NI_GPCT_INVERT_OUTPUT_BIT)
		input_select_bits |= GI_OUTPUT_POL_INVERT;
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			GI_GATE_SEL_LOAD_SRC | GI_OR_GATE |
			GI_OUTPUT_POL_INVERT, input_select_bits);

	return 0;
}

int ni_tio_arm(struct ni_gpct *counter, bool arm, unsigned int start_trigger)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int command_transient_bits = 0;

	if (arm) {
		switch (start_trigger) {
		case NI_GPCT_ARM_IMMEDIATE:
			command_transient_bits |= GI_ARM;
			break;
		case NI_GPCT_ARM_PAIRED_IMMEDIATE:
			command_transient_bits |= GI_ARM | GI_ARM_COPY;
			break;
		default:
			break;
		}
		if (ni_tio_counting_mode_registers_present(counter_dev)) {
			unsigned int bits = 0;
			unsigned int sel_mask;

			sel_mask = GI_HW_ARM_SEL_MASK(counter_dev->variant);

			switch (start_trigger) {
			case NI_GPCT_ARM_IMMEDIATE:
			case NI_GPCT_ARM_PAIRED_IMMEDIATE:
				break;
			default:
				if (start_trigger & NI_GPCT_ARM_UNKNOWN) {
					/*
					 * pass-through the least significant
					 * bits so we can figure out what
					 * select later
					 */
					bits |= GI_HW_ARM_ENA |
						(GI_HW_ARM_SEL(start_trigger) &
						 sel_mask);
				} else {
					return -EINVAL;
				}
				break;
			}
			ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx),
					GI_HW_ARM_ENA | sel_mask, bits);
		}
	} else {
		command_transient_bits |= GI_DISARM;
	}
	ni_tio_set_bits_transient(counter, NITIO_CMD_REG(cidx),
				  0, 0, command_transient_bits);
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_arm);

static unsigned int ni_660x_clk_src(unsigned int clock_source)
{
	unsigned int clk_src = clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK;
	unsigned int ni_660x_clock;
	unsigned int i;

	switch (clk_src) {
	case NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_TIMEBASE_1_CLK;
		break;
	case NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_TIMEBASE_2_CLK;
		break;
	case NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_TIMEBASE_3_CLK;
		break;
	case NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_LOGIC_LOW_CLK;
		break;
	case NI_GPCT_SOURCE_PIN_i_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_SRC_PIN_I_CLK;
		break;
	case NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_NEXT_GATE_CLK;
		break;
	case NI_GPCT_NEXT_TC_CLOCK_SRC_BITS:
		ni_660x_clock = NI_660X_NEXT_TC_CLK;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (clk_src == NI_GPCT_RTSI_CLOCK_SRC_BITS(i)) {
				ni_660x_clock = NI_660X_RTSI_CLK(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_SRC_PIN; ++i) {
			if (clk_src == NI_GPCT_SOURCE_PIN_CLOCK_SRC_BITS(i)) {
				ni_660x_clock = NI_660X_SRC_PIN_CLK(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_SRC_PIN)
			break;
		ni_660x_clock = 0;
		BUG();
		break;
	}
	return GI_SRC_SEL(ni_660x_clock);
}

static unsigned int ni_m_clk_src(unsigned int clock_source)
{
	unsigned int clk_src = clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK;
	unsigned int ni_m_series_clock;
	unsigned int i;

	switch (clk_src) {
	case NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_TIMEBASE_1_CLK;
		break;
	case NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_TIMEBASE_2_CLK;
		break;
	case NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_TIMEBASE_3_CLK;
		break;
	case NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_LOGIC_LOW_CLK;
		break;
	case NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_NEXT_GATE_CLK;
		break;
	case NI_GPCT_NEXT_TC_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_NEXT_TC_CLK;
		break;
	case NI_GPCT_PXI10_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_PXI10_CLK;
		break;
	case NI_GPCT_PXI_STAR_TRIGGER_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_PXI_STAR_TRIGGER_CLK;
		break;
	case NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS:
		ni_m_series_clock = NI_M_ANALOG_TRIGGER_OUT_CLK;
		break;
	default:
		for (i = 0; i <= NI_M_MAX_RTSI_CHAN; ++i) {
			if (clk_src == NI_GPCT_RTSI_CLOCK_SRC_BITS(i)) {
				ni_m_series_clock = NI_M_RTSI_CLK(i);
				break;
			}
		}
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (clk_src == NI_GPCT_PFI_CLOCK_SRC_BITS(i)) {
				ni_m_series_clock = NI_M_PFI_CLK(i);
				break;
			}
		}
		if (i <= NI_M_MAX_PFI_CHAN)
			break;
		pr_err("invalid clock source 0x%lx\n",
		       (unsigned long)clock_source);
		BUG();
		ni_m_series_clock = 0;
		break;
	}
	return GI_SRC_SEL(ni_m_series_clock);
};

static void ni_tio_set_source_subselect(struct ni_gpct *counter,
					unsigned int clock_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int second_gate_reg = NITIO_GATE2_REG(cidx);

	if (counter_dev->variant != ni_gpct_variant_m_series)
		return;
	switch (clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK) {
		/* Gi_Source_Subselect is zero */
	case NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS:
	case NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS:
		counter_dev->regs[second_gate_reg] &= ~GI_SRC_SUBSEL;
		break;
		/* Gi_Source_Subselect is one */
	case NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS:
	case NI_GPCT_PXI_STAR_TRIGGER_CLOCK_SRC_BITS:
		counter_dev->regs[second_gate_reg] |= GI_SRC_SUBSEL;
		break;
		/* Gi_Source_Subselect doesn't matter */
	default:
		return;
	}
	ni_tio_write(counter, counter_dev->regs[second_gate_reg],
		     second_gate_reg);
}

static int ni_tio_set_clock_src(struct ni_gpct *counter,
				unsigned int clock_source,
				unsigned int period_ns)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int bits = 0;

	/* FIXME: validate clock source */
	switch (counter_dev->variant) {
	case ni_gpct_variant_660x:
		bits |= ni_660x_clk_src(clock_source);
		break;
	case ni_gpct_variant_e_series:
	case ni_gpct_variant_m_series:
	default:
		bits |= ni_m_clk_src(clock_source);
		break;
	}
	if (clock_source & NI_GPCT_INVERT_CLOCK_SRC_BIT)
		bits |= GI_SRC_POL_INVERT;
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			GI_SRC_SEL_MASK | GI_SRC_POL_INVERT, bits);
	ni_tio_set_source_subselect(counter, clock_source);

	if (ni_tio_counting_mode_registers_present(counter_dev)) {
		bits = 0;
		switch (clock_source & NI_GPCT_PRESCALE_MODE_CLOCK_SRC_MASK) {
		case NI_GPCT_NO_PRESCALE_CLOCK_SRC_BITS:
			break;
		case NI_GPCT_PRESCALE_X2_CLOCK_SRC_BITS:
			bits |= GI_PRESCALE_X2(counter_dev->variant);
			break;
		case NI_GPCT_PRESCALE_X8_CLOCK_SRC_BITS:
			bits |= GI_PRESCALE_X8(counter_dev->variant);
			break;
		default:
			return -EINVAL;
		}
		ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx),
				GI_PRESCALE_X2(counter_dev->variant) |
				GI_PRESCALE_X8(counter_dev->variant), bits);
	}
	counter->clock_period_ps = period_ns * 1000;
	ni_tio_set_sync_mode(counter, 0);
	return 0;
}

static void ni_tio_get_clock_src(struct ni_gpct *counter,
				 unsigned int *clock_source,
				 unsigned int *period_ns)
{
	u64 temp64;

	*clock_source = ni_tio_generic_clock_src_select(counter);
	temp64 = ni_tio_clock_period_ps(counter, *clock_source);
	do_div(temp64, 1000);	/* ps to ns */
	*period_ns = temp64;
}

static int ni_660x_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
	unsigned int cidx = counter->counter_index;
	unsigned int gate_sel;
	unsigned int i;

	switch (chan) {
	case NI_GPCT_NEXT_SOURCE_GATE_SELECT:
		gate_sel = NI_660X_NEXT_SRC_GATE_SEL;
		break;
	case NI_GPCT_NEXT_OUT_GATE_SELECT:
	case NI_GPCT_LOGIC_LOW_GATE_SELECT:
	case NI_GPCT_SOURCE_PIN_i_GATE_SELECT:
	case NI_GPCT_GATE_PIN_i_GATE_SELECT:
		gate_sel = chan & 0x1f;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (chan == NI_GPCT_RTSI_GATE_SELECT(i)) {
				gate_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_GATE_PIN; ++i) {
			if (chan == NI_GPCT_GATE_PIN_GATE_SELECT(i)) {
				gate_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_660X_MAX_GATE_PIN)
			break;
		return -EINVAL;
	}
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			GI_GATE_SEL_MASK, GI_GATE_SEL(gate_sel));
	return 0;
}

static int ni_m_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
	unsigned int cidx = counter->counter_index;
	unsigned int gate_sel;
	unsigned int i;

	switch (chan) {
	case NI_GPCT_TIMESTAMP_MUX_GATE_SELECT:
	case NI_GPCT_AI_START2_GATE_SELECT:
	case NI_GPCT_PXI_STAR_TRIGGER_GATE_SELECT:
	case NI_GPCT_NEXT_OUT_GATE_SELECT:
	case NI_GPCT_AI_START1_GATE_SELECT:
	case NI_GPCT_NEXT_SOURCE_GATE_SELECT:
	case NI_GPCT_ANALOG_TRIGGER_OUT_GATE_SELECT:
	case NI_GPCT_LOGIC_LOW_GATE_SELECT:
		gate_sel = chan & 0x1f;
		break;
	default:
		for (i = 0; i <= NI_M_MAX_RTSI_CHAN; ++i) {
			if (chan == NI_GPCT_RTSI_GATE_SELECT(i)) {
				gate_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (chan == NI_GPCT_PFI_GATE_SELECT(i)) {
				gate_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_M_MAX_PFI_CHAN)
			break;
		return -EINVAL;
	}
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			GI_GATE_SEL_MASK, GI_GATE_SEL(gate_sel));
	return 0;
}

static int ni_660x_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(gate_source);
	unsigned int gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned int gate2_sel;
	unsigned int i;

	switch (chan) {
	case NI_GPCT_SOURCE_PIN_i_GATE_SELECT:
	case NI_GPCT_UP_DOWN_PIN_i_GATE_SELECT:
	case NI_GPCT_SELECTED_GATE_GATE_SELECT:
	case NI_GPCT_NEXT_OUT_GATE_SELECT:
	case NI_GPCT_LOGIC_LOW_GATE_SELECT:
		gate2_sel = chan & 0x1f;
		break;
	case NI_GPCT_NEXT_SOURCE_GATE_SELECT:
		gate2_sel = NI_660X_NEXT_SRC_GATE2_SEL;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (chan == NI_GPCT_RTSI_GATE_SELECT(i)) {
				gate2_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_UP_DOWN_PIN; ++i) {
			if (chan == NI_GPCT_UP_DOWN_PIN_GATE_SELECT(i)) {
				gate2_sel = chan & 0x1f;
				break;
			}
		}
		if (i <= NI_660X_MAX_UP_DOWN_PIN)
			break;
		return -EINVAL;
	}
	counter_dev->regs[gate2_reg] |= GI_GATE2_MODE;
	counter_dev->regs[gate2_reg] &= ~GI_GATE2_SEL_MASK;
	counter_dev->regs[gate2_reg] |= GI_GATE2_SEL(gate2_sel);
	ni_tio_write(counter, counter_dev->regs[gate2_reg], gate2_reg);
	return 0;
}

static int ni_m_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(gate_source);
	unsigned int gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned int gate2_sel;

	/*
	 * FIXME: We don't know what the m-series second gate codes are,
	 * so we'll just pass the bits through for now.
	 */
	switch (chan) {
	default:
		gate2_sel = chan & 0x1f;
		break;
	}
	counter_dev->regs[gate2_reg] |= GI_GATE2_MODE;
	counter_dev->regs[gate2_reg] &= ~GI_GATE2_SEL_MASK;
	counter_dev->regs[gate2_reg] |= GI_GATE2_SEL(gate2_sel);
	ni_tio_write(counter, counter_dev->regs[gate2_reg], gate2_reg);
	return 0;
}

int ni_tio_set_gate_src(struct ni_gpct *counter,
			unsigned int gate, unsigned int src)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(src);
	unsigned int gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned int mode = 0;

	switch (gate) {
	case 0:
		if (chan == NI_GPCT_DISABLED_GATE_SELECT) {
			ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
					GI_GATING_MODE_MASK,
					GI_GATING_DISABLED);
			return 0;
		}
		if (src & CR_INVERT)
			mode |= GI_GATE_POL_INVERT;
		if (src & CR_EDGE)
			mode |= GI_RISING_EDGE_GATING;
		else
			mode |= GI_LEVEL_GATING;
		ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
				GI_GATE_POL_INVERT | GI_GATING_MODE_MASK,
				mode);
		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			return ni_m_set_gate(counter, src);
		case ni_gpct_variant_660x:
			return ni_660x_set_gate(counter, src);
		}
		break;
	case 1:
		if (!ni_tio_has_gate2_registers(counter_dev))
			return -EINVAL;

		if (chan == NI_GPCT_DISABLED_GATE_SELECT) {
			counter_dev->regs[gate2_reg] &= ~GI_GATE2_MODE;
			ni_tio_write(counter, counter_dev->regs[gate2_reg],
				     gate2_reg);
			return 0;
		}
		if (src & CR_INVERT)
			counter_dev->regs[gate2_reg] |= GI_GATE2_POL_INVERT;
		else
			counter_dev->regs[gate2_reg] &= ~GI_GATE2_POL_INVERT;
		switch (counter_dev->variant) {
		case ni_gpct_variant_m_series:
			return ni_m_set_gate2(counter, src);
		case ni_gpct_variant_660x:
			return ni_660x_set_gate2(counter, src);
		default:
			BUG();
			break;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_set_gate_src);

static int ni_tio_set_other_src(struct ni_gpct *counter, unsigned int index,
				unsigned int source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int abz_reg, shift, mask;

	if (counter_dev->variant != ni_gpct_variant_m_series)
		return -EINVAL;

	abz_reg = NITIO_ABZ_REG(cidx);
	switch (index) {
	case NI_GPCT_SOURCE_ENCODER_A:
		shift = 10;
		break;
	case NI_GPCT_SOURCE_ENCODER_B:
		shift = 5;
		break;
	case NI_GPCT_SOURCE_ENCODER_Z:
		shift = 0;
		break;
	default:
		return -EINVAL;
	}
	mask = 0x1f << shift;
	if (source > 0x1f)
		source = 0x1f;	/* Disable gate */

	counter_dev->regs[abz_reg] &= ~mask;
	counter_dev->regs[abz_reg] |= (source << shift) & mask;
	ni_tio_write(counter, counter_dev->regs[abz_reg], abz_reg);
	return 0;
}

static unsigned int ni_660x_gate_to_generic_gate(unsigned int gate)
{
	unsigned int i;

	switch (gate) {
	case NI_660X_SRC_PIN_I_GATE_SEL:
		return NI_GPCT_SOURCE_PIN_i_GATE_SELECT;
	case NI_660X_GATE_PIN_I_GATE_SEL:
		return NI_GPCT_GATE_PIN_i_GATE_SELECT;
	case NI_660X_NEXT_SRC_GATE_SEL:
		return NI_GPCT_NEXT_SOURCE_GATE_SELECT;
	case NI_660X_NEXT_OUT_GATE_SEL:
		return NI_GPCT_NEXT_OUT_GATE_SELECT;
	case NI_660X_LOGIC_LOW_GATE_SEL:
		return NI_GPCT_LOGIC_LOW_GATE_SELECT;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_660X_RTSI_GATE_SEL(i))
				return NI_GPCT_RTSI_GATE_SELECT(i);
		}
		for (i = 0; i <= NI_660X_MAX_GATE_PIN; ++i) {
			if (gate == NI_660X_PIN_GATE_SEL(i))
				return NI_GPCT_GATE_PIN_GATE_SELECT(i);
		}
		BUG();
		break;
	}
	return 0;
};

static unsigned int ni_m_gate_to_generic_gate(unsigned int gate)
{
	unsigned int i;

	switch (gate) {
	case NI_M_TIMESTAMP_MUX_GATE_SEL:
		return NI_GPCT_TIMESTAMP_MUX_GATE_SELECT;
	case NI_M_AI_START2_GATE_SEL:
		return NI_GPCT_AI_START2_GATE_SELECT;
	case NI_M_PXI_STAR_TRIGGER_GATE_SEL:
		return NI_GPCT_PXI_STAR_TRIGGER_GATE_SELECT;
	case NI_M_NEXT_OUT_GATE_SEL:
		return NI_GPCT_NEXT_OUT_GATE_SELECT;
	case NI_M_AI_START1_GATE_SEL:
		return NI_GPCT_AI_START1_GATE_SELECT;
	case NI_M_NEXT_SRC_GATE_SEL:
		return NI_GPCT_NEXT_SOURCE_GATE_SELECT;
	case NI_M_ANALOG_TRIG_OUT_GATE_SEL:
		return NI_GPCT_ANALOG_TRIGGER_OUT_GATE_SELECT;
	case NI_M_LOGIC_LOW_GATE_SEL:
		return NI_GPCT_LOGIC_LOW_GATE_SELECT;
	default:
		for (i = 0; i <= NI_M_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_M_RTSI_GATE_SEL(i))
				return NI_GPCT_RTSI_GATE_SELECT(i);
		}
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (gate == NI_M_PFI_GATE_SEL(i))
				return NI_GPCT_PFI_GATE_SELECT(i);
		}
		BUG();
		break;
	}
	return 0;
};

static unsigned int ni_660x_gate2_to_generic_gate(unsigned int gate)
{
	unsigned int i;

	switch (gate) {
	case NI_660X_SRC_PIN_I_GATE2_SEL:
		return NI_GPCT_SOURCE_PIN_i_GATE_SELECT;
	case NI_660X_UD_PIN_I_GATE2_SEL:
		return NI_GPCT_UP_DOWN_PIN_i_GATE_SELECT;
	case NI_660X_NEXT_SRC_GATE2_SEL:
		return NI_GPCT_NEXT_SOURCE_GATE_SELECT;
	case NI_660X_NEXT_OUT_GATE2_SEL:
		return NI_GPCT_NEXT_OUT_GATE_SELECT;
	case NI_660X_SELECTED_GATE2_SEL:
		return NI_GPCT_SELECTED_GATE_GATE_SELECT;
	case NI_660X_LOGIC_LOW_GATE2_SEL:
		return NI_GPCT_LOGIC_LOW_GATE_SELECT;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_660X_RTSI_GATE2_SEL(i))
				return NI_GPCT_RTSI_GATE_SELECT(i);
		}
		for (i = 0; i <= NI_660X_MAX_UP_DOWN_PIN; ++i) {
			if (gate == NI_660X_UD_PIN_GATE2_SEL(i))
				return NI_GPCT_UP_DOWN_PIN_GATE_SELECT(i);
		}
		BUG();
		break;
	}
	return 0;
};

static unsigned int ni_m_gate2_to_generic_gate(unsigned int gate)
{
	/*
	 * FIXME: the second gate sources for the m series are undocumented,
	 * so we just return the raw bits for now.
	 */
	switch (gate) {
	default:
		return gate;
	}
	return 0;
};

static int ni_tio_get_gate_src(struct ni_gpct *counter, unsigned int gate_index,
			       unsigned int *gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int mode;
	unsigned int reg;
	unsigned int gate;

	mode = ni_tio_get_soft_copy(counter, NITIO_MODE_REG(cidx));
	if (((mode & GI_GATING_MODE_MASK) == GI_GATING_DISABLED) ||
	    (gate_index == 1 &&
	     !(counter_dev->regs[NITIO_GATE2_REG(cidx)] & GI_GATE2_MODE))) {
		*gate_source = NI_GPCT_DISABLED_GATE_SELECT;
		return 0;
	}

	switch (gate_index) {
	case 0:
		reg = NITIO_INPUT_SEL_REG(cidx);
		gate = GI_BITS_TO_GATE(ni_tio_get_soft_copy(counter, reg));

		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			*gate_source = ni_m_gate_to_generic_gate(gate);
			break;
		case ni_gpct_variant_660x:
			*gate_source = ni_660x_gate_to_generic_gate(gate);
			break;
		}
		if (mode & GI_GATE_POL_INVERT)
			*gate_source |= CR_INVERT;
		if ((mode & GI_GATING_MODE_MASK) != GI_LEVEL_GATING)
			*gate_source |= CR_EDGE;
		break;
	case 1:
		reg = NITIO_GATE2_REG(cidx);
		gate = GI_BITS_TO_GATE2(counter_dev->regs[reg]);

		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			*gate_source = ni_m_gate2_to_generic_gate(gate);
			break;
		case ni_gpct_variant_660x:
			*gate_source = ni_660x_gate2_to_generic_gate(gate);
			break;
		}
		if (counter_dev->regs[reg] & GI_GATE2_POL_INVERT)
			*gate_source |= CR_INVERT;
		/* second gate can't have edge/level mode set independently */
		if ((mode & GI_GATING_MODE_MASK) != GI_LEVEL_GATING)
			*gate_source |= CR_EDGE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int ni_tio_insn_config(struct comedi_device *dev,
		       struct comedi_subdevice *s,
		       struct comedi_insn *insn,
		       unsigned int *data)
{
	struct ni_gpct *counter = s->private;
	unsigned int cidx = counter->counter_index;
	unsigned int status;

	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		return ni_tio_set_counter_mode(counter, data[1]);
	case INSN_CONFIG_ARM:
		return ni_tio_arm(counter, true, data[1]);
	case INSN_CONFIG_DISARM:
		ni_tio_arm(counter, false, 0);
		return 0;
	case INSN_CONFIG_GET_COUNTER_STATUS:
		data[1] = 0;
		status = ni_tio_read(counter, NITIO_SHARED_STATUS_REG(cidx));
		if (status & GI_ARMED(cidx)) {
			data[1] |= COMEDI_COUNTER_ARMED;
			if (status & GI_COUNTING(cidx))
				data[1] |= COMEDI_COUNTER_COUNTING;
		}
		data[2] = COMEDI_COUNTER_ARMED | COMEDI_COUNTER_COUNTING;
		return 0;
	case INSN_CONFIG_SET_CLOCK_SRC:
		return ni_tio_set_clock_src(counter, data[1], data[2]);
	case INSN_CONFIG_GET_CLOCK_SRC:
		ni_tio_get_clock_src(counter, &data[1], &data[2]);
		return 0;
	case INSN_CONFIG_SET_GATE_SRC:
		return ni_tio_set_gate_src(counter, data[1], data[2]);
	case INSN_CONFIG_GET_GATE_SRC:
		return ni_tio_get_gate_src(counter, data[1], &data[2]);
	case INSN_CONFIG_SET_OTHER_SRC:
		return ni_tio_set_other_src(counter, data[1], data[2]);
	case INSN_CONFIG_RESET:
		ni_tio_reset_count_and_disarm(counter);
		return 0;
	default:
		break;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ni_tio_insn_config);

static unsigned int ni_tio_read_sw_save_reg(struct comedi_device *dev,
					    struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	unsigned int cidx = counter->counter_index;
	unsigned int val;

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx), GI_SAVE_TRACE, 0);
	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx),
			GI_SAVE_TRACE, GI_SAVE_TRACE);

	/*
	 * The count doesn't get latched until the next clock edge, so it is
	 * possible the count may change (once) while we are reading. Since
	 * the read of the SW_Save_Reg isn't atomic (apparently even when it's
	 * a 32 bit register according to 660x docs), we need to read twice
	 * and make sure the reading hasn't changed. If it has, a third read
	 * will be correct since the count value will definitely have latched
	 * by then.
	 */
	val = ni_tio_read(counter, NITIO_SW_SAVE_REG(cidx));
	if (val != ni_tio_read(counter, NITIO_SW_SAVE_REG(cidx)))
		val = ni_tio_read(counter, NITIO_SW_SAVE_REG(cidx));

	return val;
}

int ni_tio_insn_read(struct comedi_device *dev,
		     struct comedi_subdevice *s,
		     struct comedi_insn *insn,
		     unsigned int *data)
{
	struct ni_gpct *counter = s->private;
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int channel = CR_CHAN(insn->chanspec);
	unsigned int cidx = counter->counter_index;
	int i;

	for (i = 0; i < insn->n; i++) {
		switch (channel) {
		case 0:
			data[i] = ni_tio_read_sw_save_reg(dev, s);
			break;
		case 1:
			data[i] = counter_dev->regs[NITIO_LOADA_REG(cidx)];
			break;
		case 2:
			data[i] = counter_dev->regs[NITIO_LOADB_REG(cidx)];
			break;
		}
	}
	return insn->n;
}
EXPORT_SYMBOL_GPL(ni_tio_insn_read);

static unsigned int ni_tio_next_load_register(struct ni_gpct *counter)
{
	unsigned int cidx = counter->counter_index;
	unsigned int bits = ni_tio_read(counter, NITIO_SHARED_STATUS_REG(cidx));

	return (bits & GI_NEXT_LOAD_SRC(cidx))
			? NITIO_LOADB_REG(cidx)
			: NITIO_LOADA_REG(cidx);
}

int ni_tio_insn_write(struct comedi_device *dev,
		      struct comedi_subdevice *s,
		      struct comedi_insn *insn,
		      unsigned int *data)
{
	struct ni_gpct *counter = s->private;
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int channel = CR_CHAN(insn->chanspec);
	unsigned int cidx = counter->counter_index;
	unsigned int load_reg;

	if (insn->n < 1)
		return 0;
	switch (channel) {
	case 0:
		/*
		 * Unsafe if counter is armed.
		 * Should probably check status and return -EBUSY if armed.
		 */

		/*
		 * Don't disturb load source select, just use whichever
		 * load register is already selected.
		 */
		load_reg = ni_tio_next_load_register(counter);
		ni_tio_write(counter, data[0], load_reg);
		ni_tio_set_bits_transient(counter, NITIO_CMD_REG(cidx),
					  0, 0, GI_LOAD);
		/* restore load reg */
		ni_tio_write(counter, counter_dev->regs[load_reg], load_reg);
		break;
	case 1:
		counter_dev->regs[NITIO_LOADA_REG(cidx)] = data[0];
		ni_tio_write(counter, data[0], NITIO_LOADA_REG(cidx));
		break;
	case 2:
		counter_dev->regs[NITIO_LOADB_REG(cidx)] = data[0];
		ni_tio_write(counter, data[0], NITIO_LOADB_REG(cidx));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_insn_write);

void ni_tio_init_counter(struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;

	ni_tio_reset_count_and_disarm(counter);

	/* initialize counter registers */
	counter_dev->regs[NITIO_AUTO_INC_REG(cidx)] = 0x0;
	ni_tio_write(counter, 0x0, NITIO_AUTO_INC_REG(cidx));

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx),
			~0, GI_SYNC_GATE);

	ni_tio_set_bits(counter, NITIO_MODE_REG(cidx), ~0, 0);

	counter_dev->regs[NITIO_LOADA_REG(cidx)] = 0x0;
	ni_tio_write(counter, 0x0, NITIO_LOADA_REG(cidx));

	counter_dev->regs[NITIO_LOADB_REG(cidx)] = 0x0;
	ni_tio_write(counter, 0x0, NITIO_LOADB_REG(cidx));

	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx), ~0, 0);

	if (ni_tio_counting_mode_registers_present(counter_dev))
		ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx), ~0, 0);

	if (ni_tio_has_gate2_registers(counter_dev)) {
		counter_dev->regs[NITIO_GATE2_REG(cidx)] = 0x0;
		ni_tio_write(counter, 0x0, NITIO_GATE2_REG(cidx));
	}

	ni_tio_set_bits(counter, NITIO_DMA_CFG_REG(cidx), ~0, 0x0);

	ni_tio_set_bits(counter, NITIO_INT_ENA_REG(cidx), ~0, 0x0);
}
EXPORT_SYMBOL_GPL(ni_tio_init_counter);

struct ni_gpct_device *
ni_gpct_device_construct(struct comedi_device *dev,
			 void (*write)(struct ni_gpct *counter,
				       unsigned int value,
				       enum ni_gpct_register reg),
			 unsigned int (*read)(struct ni_gpct *counter,
					      enum ni_gpct_register reg),
			 enum ni_gpct_variant variant,
			 unsigned int num_counters)
{
	struct ni_gpct_device *counter_dev;
	struct ni_gpct *counter;
	unsigned int i;

	if (num_counters == 0)
		return NULL;

	counter_dev = kzalloc(sizeof(*counter_dev), GFP_KERNEL);
	if (!counter_dev)
		return NULL;

	counter_dev->dev = dev;
	counter_dev->write = write;
	counter_dev->read = read;
	counter_dev->variant = variant;

	spin_lock_init(&counter_dev->regs_lock);

	counter_dev->counters = kcalloc(num_counters, sizeof(*counter),
					GFP_KERNEL);
	if (!counter_dev->counters) {
		kfree(counter_dev);
		return NULL;
	}

	for (i = 0; i < num_counters; ++i) {
		counter = &counter_dev->counters[i];
		counter->counter_dev = counter_dev;
		spin_lock_init(&counter->lock);
	}
	counter_dev->num_counters = num_counters;

	return counter_dev;
}
EXPORT_SYMBOL_GPL(ni_gpct_device_construct);

void ni_gpct_device_destroy(struct ni_gpct_device *counter_dev)
{
	if (!counter_dev)
		return;
	kfree(counter_dev->counters);
	kfree(counter_dev);
}
EXPORT_SYMBOL_GPL(ni_gpct_device_destroy);

static int __init ni_tio_init_module(void)
{
	return 0;
}
module_init(ni_tio_init_module);

static void __exit ni_tio_cleanup_module(void)
{
}
module_exit(ni_tio_cleanup_module);

MODULE_AUTHOR("Comedi <comedi@comedi.org>");
MODULE_DESCRIPTION("Comedi support for NI general-purpose counters");
MODULE_LICENSE("GPL");
