// SPDX-License-Identifier: GPL-2.0+
/*
 * Support for NI general purpose counters
 *
 * Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>
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

static int ni_tio_clock_period_ps(const struct ni_gpct *counter,
				  unsigned int generic_clock_source,
				  u64 *period_ps)
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
		*period_ps = counter->clock_period_ps;
		return 0;
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
		return -EINVAL;
	}
	*period_ps = clock_period_ps;
	return 0;
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

static int ni_m_series_clock_src_select(const struct ni_gpct *counter,
					unsigned int *clk_src)
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
		return -EINVAL;
	}
	clock_source |= ni_tio_clock_src_modifiers(counter);
	*clk_src = clock_source;
	return 0;
}

static int ni_660x_clock_src_select(const struct ni_gpct *counter,
				    unsigned int *clk_src)
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
		return -EINVAL;
	}
	clock_source |= ni_tio_clock_src_modifiers(counter);
	*clk_src = clock_source;
	return 0;
}

static int ni_tio_generic_clock_src_select(const struct ni_gpct *counter,
					   unsigned int *clk_src)
{
	switch (counter->counter_dev->variant) {
	case ni_gpct_variant_e_series:
	case ni_gpct_variant_m_series:
	default:
		return ni_m_series_clock_src_select(counter, clk_src);
	case ni_gpct_variant_660x:
		return ni_660x_clock_src_select(counter, clk_src);
	}
}

static void ni_tio_set_sync_mode(struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	static const u64 min_normal_sync_period_ps = 25000;
	unsigned int mask = 0;
	unsigned int bits = 0;
	unsigned int reg;
	unsigned int mode;
	unsigned int clk_src = 0;
	u64 ps = 0;
	int ret;
	bool force_alt_sync;

	/* only m series and 660x variants have counting mode registers */
	switch (counter_dev->variant) {
	case ni_gpct_variant_e_series:
	default:
		return;
	case ni_gpct_variant_m_series:
		mask = GI_M_ALT_SYNC;
		break;
	case ni_gpct_variant_660x:
		mask = GI_660X_ALT_SYNC;
		break;
	}

	reg = NITIO_CNT_MODE_REG(cidx);
	mode = ni_tio_get_soft_copy(counter, reg);
	switch (mode & GI_CNT_MODE_MASK) {
	case GI_CNT_MODE_QUADX1:
	case GI_CNT_MODE_QUADX2:
	case GI_CNT_MODE_QUADX4:
	case GI_CNT_MODE_SYNC_SRC:
		force_alt_sync = true;
		break;
	default:
		force_alt_sync = false;
		break;
	}

	ret = ni_tio_generic_clock_src_select(counter, &clk_src);
	if (ret)
		return;
	ret = ni_tio_clock_period_ps(counter, clk_src, &ps);
	if (ret)
		return;
	/*
	 * It's not clear what we should do if clock_period is unknown, so we
	 * are not using the alt sync bit in that case.
	 */
	if (force_alt_sync || (ps && ps < min_normal_sync_period_ps))
		bits = mask;

	ni_tio_set_bits(counter, reg, mask, bits);
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
		ni_tio_set_sync_mode(counter);
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
	unsigned int transient_bits = 0;

	if (arm) {
		unsigned int mask = 0;
		unsigned int bits = 0;

		/* only m series and 660x have counting mode registers */
		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		default:
			break;
		case ni_gpct_variant_m_series:
			mask = GI_M_HW_ARM_SEL_MASK;
			break;
		case ni_gpct_variant_660x:
			mask = GI_660X_HW_ARM_SEL_MASK;
			break;
		}

		switch (start_trigger) {
		case NI_GPCT_ARM_IMMEDIATE:
			transient_bits |= GI_ARM;
			break;
		case NI_GPCT_ARM_PAIRED_IMMEDIATE:
			transient_bits |= GI_ARM | GI_ARM_COPY;
			break;
		default:
			/*
			 * for m series and 660x, pass-through the least
			 * significant bits so we can figure out what select
			 * later
			 */
			if (mask && (start_trigger & NI_GPCT_ARM_UNKNOWN)) {
				bits |= GI_HW_ARM_ENA |
					(GI_HW_ARM_SEL(start_trigger) & mask);
			} else {
				return -EINVAL;
			}
			break;
		}

		if (mask)
			ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx),
					GI_HW_ARM_ENA | mask, bits);
	} else {
		transient_bits |= GI_DISARM;
	}
	ni_tio_set_bits_transient(counter, NITIO_CMD_REG(cidx),
				  0, 0, transient_bits);
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_arm);

static int ni_660x_clk_src(unsigned int clock_source, unsigned int *bits)
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
		return -EINVAL;
	}
	*bits = GI_SRC_SEL(ni_660x_clock);
	return 0;
}

static int ni_m_clk_src(unsigned int clock_source, unsigned int *bits)
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
		return -EINVAL;
	}
	*bits = GI_SRC_SEL(ni_m_series_clock);
	return 0;
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
	int ret;

	switch (counter_dev->variant) {
	case ni_gpct_variant_660x:
		ret = ni_660x_clk_src(clock_source, &bits);
		break;
	case ni_gpct_variant_e_series:
	case ni_gpct_variant_m_series:
	default:
		ret = ni_m_clk_src(clock_source, &bits);
		break;
	}
	if (ret) {
		struct comedi_device *dev = counter_dev->dev;

		dev_err(dev->class_dev, "invalid clock source 0x%x\n",
			clock_source);
		return ret;
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
	ni_tio_set_sync_mode(counter);
	return 0;
}

static int ni_tio_get_clock_src(struct ni_gpct *counter,
				unsigned int *clock_source,
				unsigned int *period_ns)
{
	u64 temp64 = 0;
	int ret;

	ret = ni_tio_generic_clock_src_select(counter, clock_source);
	if (ret)
		return ret;
	ret = ni_tio_clock_period_ps(counter, *clock_source, &temp64);
	if (ret)
		return ret;
	do_div(temp64, 1000);	/* ps to ns */
	*period_ns = temp64;
	return 0;
}

static inline void ni_tio_set_gate_raw(struct ni_gpct *counter,
				       unsigned int gate_source)
{
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(counter->counter_index),
			GI_GATE_SEL_MASK, GI_GATE_SEL(gate_source));
}

static inline void ni_tio_set_gate2_raw(struct ni_gpct *counter,
					unsigned int gate_source)
{
	ni_tio_set_bits(counter, NITIO_GATE2_REG(counter->counter_index),
			GI_GATE2_SEL_MASK, GI_GATE2_SEL(gate_source));
}

/* Set the mode bits for gate. */
static inline void ni_tio_set_gate_mode(struct ni_gpct *counter,
					unsigned int src)
{
	unsigned int mode_bits = 0;

	if (CR_CHAN(src) & NI_GPCT_DISABLED_GATE_SELECT) {
		/*
		 * Allowing bitwise comparison here to allow non-zero raw
		 * register value to be used for channel when disabling.
		 */
		mode_bits = GI_GATING_DISABLED;
	} else {
		if (src & CR_INVERT)
			mode_bits |= GI_GATE_POL_INVERT;
		if (src & CR_EDGE)
			mode_bits |= GI_RISING_EDGE_GATING;
		else
			mode_bits |= GI_LEVEL_GATING;
	}
	ni_tio_set_bits(counter, NITIO_MODE_REG(counter->counter_index),
			GI_GATE_POL_INVERT | GI_GATING_MODE_MASK,
			mode_bits);
}

/*
 * Set the mode bits for gate2.
 *
 * Previously, the code this function represents did not actually write anything
 * to the register.  Rather, writing to this register was reserved for the code
 * ni ni_tio_set_gate2_raw.
 */
static inline void ni_tio_set_gate2_mode(struct ni_gpct *counter,
					 unsigned int src)
{
	/*
	 * The GI_GATE2_MODE bit was previously set in the code that also sets
	 * the gate2 source.
	 * We'll set mode bits _after_ source bits now, and thus, this function
	 * will effectively enable the second gate after all bits are set.
	 */
	unsigned int mode_bits = GI_GATE2_MODE;

	if (CR_CHAN(src) & NI_GPCT_DISABLED_GATE_SELECT)
		/*
		 * Allowing bitwise comparison here to allow non-zero raw
		 * register value to be used for channel when disabling.
		 */
		mode_bits = GI_GATING_DISABLED;
	if (src & CR_INVERT)
		mode_bits |= GI_GATE2_POL_INVERT;

	ni_tio_set_bits(counter, NITIO_GATE2_REG(counter->counter_index),
			GI_GATE2_POL_INVERT | GI_GATE2_MODE, mode_bits);
}

static int ni_660x_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
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
	ni_tio_set_gate_raw(counter, gate_sel);
	return 0;
}

static int ni_m_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
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
	ni_tio_set_gate_raw(counter, gate_sel);
	return 0;
}

static int ni_660x_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
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
	ni_tio_set_gate2_raw(counter, gate2_sel);
	return 0;
}

static int ni_m_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	/*
	 * FIXME: We don't know what the m-series second gate codes are,
	 * so we'll just pass the bits through for now.
	 */
	ni_tio_set_gate2_raw(counter, gate_source);
	return 0;
}

int ni_tio_set_gate_src_raw(struct ni_gpct *counter,
			    unsigned int gate, unsigned int src)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;

	switch (gate) {
	case 0:
		/* 1.  start by disabling gate */
		ni_tio_set_gate_mode(counter, NI_GPCT_DISABLED_GATE_SELECT);
		/* 2.  set the requested gate source */
		ni_tio_set_gate_raw(counter, src);
		/* 3.  reenable & set mode to starts things back up */
		ni_tio_set_gate_mode(counter, src);
		break;
	case 1:
		if (!ni_tio_has_gate2_registers(counter_dev))
			return -EINVAL;

		/* 1.  start by disabling gate */
		ni_tio_set_gate2_mode(counter, NI_GPCT_DISABLED_GATE_SELECT);
		/* 2.  set the requested gate source */
		ni_tio_set_gate2_raw(counter, src);
		/* 3.  reenable & set mode to starts things back up */
		ni_tio_set_gate2_mode(counter, src);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_set_gate_src_raw);

int ni_tio_set_gate_src(struct ni_gpct *counter,
			unsigned int gate, unsigned int src)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	/*
	 * mask off disable flag.  This high bit still passes CR_CHAN.
	 * Doing this allows one to both set the gate as disabled, but also
	 * change the route value of the gate.
	 */
	int chan = CR_CHAN(src) & (~NI_GPCT_DISABLED_GATE_SELECT);
	int ret;

	switch (gate) {
	case 0:
		/* 1.  start by disabling gate */
		ni_tio_set_gate_mode(counter, NI_GPCT_DISABLED_GATE_SELECT);
		/* 2.  set the requested gate source */
		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
			ret = ni_m_set_gate(counter, chan);
			break;
		case ni_gpct_variant_660x:
			ret = ni_660x_set_gate(counter, chan);
			break;
		default:
			return -EINVAL;
		}
		if (ret)
			return ret;
		/* 3.  reenable & set mode to starts things back up */
		ni_tio_set_gate_mode(counter, src);
		break;
	case 1:
		if (!ni_tio_has_gate2_registers(counter_dev))
			return -EINVAL;

		/* 1.  start by disabling gate */
		ni_tio_set_gate2_mode(counter, NI_GPCT_DISABLED_GATE_SELECT);
		/* 2.  set the requested gate source */
		switch (counter_dev->variant) {
		case ni_gpct_variant_m_series:
			ret = ni_m_set_gate2(counter, chan);
			break;
		case ni_gpct_variant_660x:
			ret = ni_660x_set_gate2(counter, chan);
			break;
		default:
			return -EINVAL;
		}
		if (ret)
			return ret;
		/* 3.  reenable & set mode to starts things back up */
		ni_tio_set_gate2_mode(counter, src);
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

	/* allow for new device-global names */
	if (index == NI_GPCT_SOURCE_ENCODER_A ||
	    (index >= NI_CtrA(0) && index <= NI_CtrA(-1))) {
		shift = 10;
	} else if (index == NI_GPCT_SOURCE_ENCODER_B ||
	    (index >= NI_CtrB(0) && index <= NI_CtrB(-1))) {
		shift = 5;
	} else if (index == NI_GPCT_SOURCE_ENCODER_Z ||
	    (index >= NI_CtrZ(0) && index <= NI_CtrZ(-1))) {
		shift = 0;
	} else {
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

static int ni_tio_get_other_src(struct ni_gpct *counter, unsigned int index,
				unsigned int *source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned int cidx = counter->counter_index;
	unsigned int abz_reg, shift, mask;

	if (counter_dev->variant != ni_gpct_variant_m_series)
		/* A,B,Z only valid for m-series */
		return -EINVAL;

	abz_reg = NITIO_ABZ_REG(cidx);

	/* allow for new device-global names */
	if (index == NI_GPCT_SOURCE_ENCODER_A ||
	    (index >= NI_CtrA(0) && index <= NI_CtrA(-1))) {
		shift = 10;
	} else if (index == NI_GPCT_SOURCE_ENCODER_B ||
	    (index >= NI_CtrB(0) && index <= NI_CtrB(-1))) {
		shift = 5;
	} else if (index == NI_GPCT_SOURCE_ENCODER_Z ||
	    (index >= NI_CtrZ(0) && index <= NI_CtrZ(-1))) {
		shift = 0;
	} else {
		return -EINVAL;
	}

	mask = 0x1f;

	*source = (ni_tio_get_soft_copy(counter, abz_reg) >> shift) & mask;
	return 0;
}

static int ni_660x_gate_to_generic_gate(unsigned int gate, unsigned int *src)
{
	unsigned int source;
	unsigned int i;

	switch (gate) {
	case NI_660X_SRC_PIN_I_GATE_SEL:
		source = NI_GPCT_SOURCE_PIN_i_GATE_SELECT;
		break;
	case NI_660X_GATE_PIN_I_GATE_SEL:
		source = NI_GPCT_GATE_PIN_i_GATE_SELECT;
		break;
	case NI_660X_NEXT_SRC_GATE_SEL:
		source = NI_GPCT_NEXT_SOURCE_GATE_SELECT;
		break;
	case NI_660X_NEXT_OUT_GATE_SEL:
		source = NI_GPCT_NEXT_OUT_GATE_SELECT;
		break;
	case NI_660X_LOGIC_LOW_GATE_SEL:
		source = NI_GPCT_LOGIC_LOW_GATE_SELECT;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_660X_RTSI_GATE_SEL(i)) {
				source = NI_GPCT_RTSI_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_GATE_PIN; ++i) {
			if (gate == NI_660X_PIN_GATE_SEL(i)) {
				source = NI_GPCT_GATE_PIN_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_GATE_PIN)
			break;
		return -EINVAL;
	}
	*src = source;
	return 0;
}

static int ni_m_gate_to_generic_gate(unsigned int gate, unsigned int *src)
{
	unsigned int source;
	unsigned int i;

	switch (gate) {
	case NI_M_TIMESTAMP_MUX_GATE_SEL:
		source = NI_GPCT_TIMESTAMP_MUX_GATE_SELECT;
		break;
	case NI_M_AI_START2_GATE_SEL:
		source = NI_GPCT_AI_START2_GATE_SELECT;
		break;
	case NI_M_PXI_STAR_TRIGGER_GATE_SEL:
		source = NI_GPCT_PXI_STAR_TRIGGER_GATE_SELECT;
		break;
	case NI_M_NEXT_OUT_GATE_SEL:
		source = NI_GPCT_NEXT_OUT_GATE_SELECT;
		break;
	case NI_M_AI_START1_GATE_SEL:
		source = NI_GPCT_AI_START1_GATE_SELECT;
		break;
	case NI_M_NEXT_SRC_GATE_SEL:
		source = NI_GPCT_NEXT_SOURCE_GATE_SELECT;
		break;
	case NI_M_ANALOG_TRIG_OUT_GATE_SEL:
		source = NI_GPCT_ANALOG_TRIGGER_OUT_GATE_SELECT;
		break;
	case NI_M_LOGIC_LOW_GATE_SEL:
		source = NI_GPCT_LOGIC_LOW_GATE_SELECT;
		break;
	default:
		for (i = 0; i <= NI_M_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_M_RTSI_GATE_SEL(i)) {
				source = NI_GPCT_RTSI_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (gate == NI_M_PFI_GATE_SEL(i)) {
				source = NI_GPCT_PFI_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_M_MAX_PFI_CHAN)
			break;
		return -EINVAL;
	}
	*src = source;
	return 0;
}

static int ni_660x_gate2_to_generic_gate(unsigned int gate, unsigned int *src)
{
	unsigned int source;
	unsigned int i;

	switch (gate) {
	case NI_660X_SRC_PIN_I_GATE2_SEL:
		source = NI_GPCT_SOURCE_PIN_i_GATE_SELECT;
		break;
	case NI_660X_UD_PIN_I_GATE2_SEL:
		source = NI_GPCT_UP_DOWN_PIN_i_GATE_SELECT;
		break;
	case NI_660X_NEXT_SRC_GATE2_SEL:
		source = NI_GPCT_NEXT_SOURCE_GATE_SELECT;
		break;
	case NI_660X_NEXT_OUT_GATE2_SEL:
		source = NI_GPCT_NEXT_OUT_GATE_SELECT;
		break;
	case NI_660X_SELECTED_GATE2_SEL:
		source = NI_GPCT_SELECTED_GATE_GATE_SELECT;
		break;
	case NI_660X_LOGIC_LOW_GATE2_SEL:
		source = NI_GPCT_LOGIC_LOW_GATE_SELECT;
		break;
	default:
		for (i = 0; i <= NI_660X_MAX_RTSI_CHAN; ++i) {
			if (gate == NI_660X_RTSI_GATE2_SEL(i)) {
				source = NI_GPCT_RTSI_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_UP_DOWN_PIN; ++i) {
			if (gate == NI_660X_UD_PIN_GATE2_SEL(i)) {
				source = NI_GPCT_UP_DOWN_PIN_GATE_SELECT(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_UP_DOWN_PIN)
			break;
		return -EINVAL;
	}
	*src = source;
	return 0;
}

static int ni_m_gate2_to_generic_gate(unsigned int gate, unsigned int *src)
{
	/*
	 * FIXME: the second gate sources for the m series are undocumented,
	 * so we just return the raw bits for now.
	 */
	*src = gate;
	return 0;
}

static inline unsigned int ni_tio_get_gate_mode(struct ni_gpct *counter)
{
	unsigned int mode = ni_tio_get_soft_copy(
		counter, NITIO_MODE_REG(counter->counter_index));
	unsigned int ret = 0;

	if ((mode & GI_GATING_MODE_MASK) == GI_GATING_DISABLED)
		ret |= NI_GPCT_DISABLED_GATE_SELECT;
	if (mode & GI_GATE_POL_INVERT)
		ret |= CR_INVERT;
	if ((mode & GI_GATING_MODE_MASK) != GI_LEVEL_GATING)
		ret |= CR_EDGE;

	return ret;
}

static inline unsigned int ni_tio_get_gate2_mode(struct ni_gpct *counter)
{
	unsigned int mode = ni_tio_get_soft_copy(
		counter, NITIO_GATE2_REG(counter->counter_index));
	unsigned int ret = 0;

	if (!(mode & GI_GATE2_MODE))
		ret |= NI_GPCT_DISABLED_GATE_SELECT;
	if (mode & GI_GATE2_POL_INVERT)
		ret |= CR_INVERT;

	return ret;
}

static inline unsigned int ni_tio_get_gate_val(struct ni_gpct *counter)
{
	return GI_BITS_TO_GATE(ni_tio_get_soft_copy(counter,
		NITIO_INPUT_SEL_REG(counter->counter_index)));
}

static inline unsigned int ni_tio_get_gate2_val(struct ni_gpct *counter)
{
	return GI_BITS_TO_GATE2(ni_tio_get_soft_copy(counter,
		NITIO_GATE2_REG(counter->counter_index)));
}

static int ni_tio_get_gate_src(struct ni_gpct *counter, unsigned int gate_index,
			       unsigned int *gate_source)
{
	unsigned int gate;
	int ret;

	switch (gate_index) {
	case 0:
		gate = ni_tio_get_gate_val(counter);
		switch (counter->counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			ret = ni_m_gate_to_generic_gate(gate, gate_source);
			break;
		case ni_gpct_variant_660x:
			ret = ni_660x_gate_to_generic_gate(gate, gate_source);
			break;
		}
		if (ret)
			return ret;
		*gate_source |= ni_tio_get_gate_mode(counter);
		break;
	case 1:
		gate = ni_tio_get_gate2_val(counter);
		switch (counter->counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			ret = ni_m_gate2_to_generic_gate(gate, gate_source);
			break;
		case ni_gpct_variant_660x:
			ret = ni_660x_gate2_to_generic_gate(gate, gate_source);
			break;
		}
		if (ret)
			return ret;
		*gate_source |= ni_tio_get_gate2_mode(counter);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ni_tio_get_gate_src_raw(struct ni_gpct *counter,
				   unsigned int gate_index,
				   unsigned int *gate_source)
{
	switch (gate_index) {
	case 0:
		*gate_source = ni_tio_get_gate_mode(counter)
			     | ni_tio_get_gate_val(counter);
		break;
	case 1:
		*gate_source = ni_tio_get_gate2_mode(counter)
			     | ni_tio_get_gate2_val(counter);
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
	int ret = 0;

	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		ret = ni_tio_set_counter_mode(counter, data[1]);
		break;
	case INSN_CONFIG_ARM:
		ret = ni_tio_arm(counter, true, data[1]);
		break;
	case INSN_CONFIG_DISARM:
		ret = ni_tio_arm(counter, false, 0);
		break;
	case INSN_CONFIG_GET_COUNTER_STATUS:
		data[1] = 0;
		status = ni_tio_read(counter, NITIO_SHARED_STATUS_REG(cidx));
		if (status & GI_ARMED(cidx)) {
			data[1] |= COMEDI_COUNTER_ARMED;
			if (status & GI_COUNTING(cidx))
				data[1] |= COMEDI_COUNTER_COUNTING;
		}
		data[2] = COMEDI_COUNTER_ARMED | COMEDI_COUNTER_COUNTING;
		break;
	case INSN_CONFIG_SET_CLOCK_SRC:
		ret = ni_tio_set_clock_src(counter, data[1], data[2]);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		ret = ni_tio_get_clock_src(counter, &data[1], &data[2]);
		break;
	case INSN_CONFIG_SET_GATE_SRC:
		ret = ni_tio_set_gate_src(counter, data[1], data[2]);
		break;
	case INSN_CONFIG_GET_GATE_SRC:
		ret = ni_tio_get_gate_src(counter, data[1], &data[2]);
		break;
	case INSN_CONFIG_SET_OTHER_SRC:
		ret = ni_tio_set_other_src(counter, data[1], data[2]);
		break;
	case INSN_CONFIG_RESET:
		ni_tio_reset_count_and_disarm(counter);
		break;
	default:
		return -EINVAL;
	}
	return ret ? ret : insn->n;
}
EXPORT_SYMBOL_GPL(ni_tio_insn_config);

/**
 * Retrieves the register value of the current source of the output selector for
 * the given destination.
 *
 * If the terminal for the destination is not already configured as an output,
 * this function returns -EINVAL as error.
 *
 * Return: the register value of the destination output selector;
 *         -EINVAL if terminal is not configured for output.
 */
int ni_tio_get_routing(struct ni_gpct_device *counter_dev, unsigned int dest)
{
	/* we need to know the actual counter below... */
	int ctr_index = (dest - NI_COUNTER_NAMES_BASE) % NI_MAX_COUNTERS;
	struct ni_gpct *counter = &counter_dev->counters[ctr_index];
	int ret = 1;
	unsigned int reg;

	if (dest >= NI_CtrA(0) && dest <= NI_CtrZ(-1)) {
		ret = ni_tio_get_other_src(counter, dest, &reg);
	} else if (dest >= NI_CtrGate(0) && dest <= NI_CtrGate(-1)) {
		ret = ni_tio_get_gate_src_raw(counter, 0, &reg);
	} else if (dest >= NI_CtrAux(0) && dest <= NI_CtrAux(-1)) {
		ret = ni_tio_get_gate_src_raw(counter, 1, &reg);
	/*
	 * This case is not possible through this interface.  A user must use
	 * INSN_CONFIG_SET_CLOCK_SRC instead.
	 * } else if (dest >= NI_CtrSource(0) && dest <= NI_CtrSource(-1)) {
	 *	ret = ni_tio_set_clock_src(counter, &reg, &period_ns);
	 */
	}

	if (ret)
		return -EINVAL;

	return reg;
}
EXPORT_SYMBOL_GPL(ni_tio_get_routing);

/**
 * Sets the register value of the selector MUX for the given destination.
 * @counter_dev:Pointer to general counter device.
 * @destination:Device-global identifier of route destination.
 * @register_value:
 *		The first several bits of this value should store the desired
 *		value to write to the register.  All other bits are for
 *		transmitting information that modify the mode of the particular
 *		destination/gate.  These mode bits might include a bitwise or of
 *		CR_INVERT and CR_EDGE.  Note that the calling function should
 *		have already validated the correctness of this value.
 */
int ni_tio_set_routing(struct ni_gpct_device *counter_dev, unsigned int dest,
		       unsigned int reg)
{
	/* we need to know the actual counter below... */
	int ctr_index = (dest - NI_COUNTER_NAMES_BASE) % NI_MAX_COUNTERS;
	struct ni_gpct *counter = &counter_dev->counters[ctr_index];
	int ret;

	if (dest >= NI_CtrA(0) && dest <= NI_CtrZ(-1)) {
		ret = ni_tio_set_other_src(counter, dest, reg);
	} else if (dest >= NI_CtrGate(0) && dest <= NI_CtrGate(-1)) {
		ret = ni_tio_set_gate_src_raw(counter, 0, reg);
	} else if (dest >= NI_CtrAux(0) && dest <= NI_CtrAux(-1)) {
		ret = ni_tio_set_gate_src_raw(counter, 1, reg);
	/*
	 * This case is not possible through this interface.  A user must use
	 * INSN_CONFIG_SET_CLOCK_SRC instead.
	 * } else if (dest >= NI_CtrSource(0) && dest <= NI_CtrSource(-1)) {
	 *	ret = ni_tio_set_clock_src(counter, reg, period_ns);
	 */
	} else {
		return -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ni_tio_set_routing);

/**
 * Sets the given destination MUX to its default value or disable it.
 *
 * Return: 0 if successful; -EINVAL if terminal is unknown.
 */
int ni_tio_unset_routing(struct ni_gpct_device *counter_dev, unsigned int dest)
{
	if (dest >= NI_GATES_NAMES_BASE && dest <= NI_GATES_NAMES_MAX)
		/* Disable gate (via mode bits) and set to default 0-value */
		return ni_tio_set_routing(counter_dev, dest,
					  NI_GPCT_DISABLED_GATE_SELECT);
	/*
	 * This case is not possible through this interface.  A user must use
	 * INSN_CONFIG_SET_CLOCK_SRC instead.
	 * if (dest >= NI_CtrSource(0) && dest <= NI_CtrSource(-1))
	 *	return ni_tio_set_clock_src(counter, reg, period_ns);
	 */

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ni_tio_unset_routing);

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
			 unsigned int num_counters,
			 unsigned int counters_per_chip,
			 const struct ni_route_tables *routing_tables)
{
	struct ni_gpct_device *counter_dev;
	struct ni_gpct *counter;
	unsigned int i;

	if (num_counters == 0 || counters_per_chip == 0)
		return NULL;

	counter_dev = kzalloc(sizeof(*counter_dev), GFP_KERNEL);
	if (!counter_dev)
		return NULL;

	counter_dev->dev = dev;
	counter_dev->write = write;
	counter_dev->read = read;
	counter_dev->variant = variant;
	counter_dev->routing_tables = routing_tables;

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
		counter->chip_index = i / counters_per_chip;
		counter->counter_index = i % counters_per_chip;
		spin_lock_init(&counter->lock);
	}
	counter_dev->num_counters = num_counters;
	counter_dev->counters_per_chip = counters_per_chip;

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
