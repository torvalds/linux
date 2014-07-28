/*
  comedi/drivers/ni_tio.c
  Support for NI general purpose counters

  Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/

/*
Driver: ni_tio
Description: National Instruments general purpose counters
Devices:
Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
	Herman.Bruyninckx@mech.kuleuven.ac.be,
	Wim.Meeussen@mech.kuleuven.ac.be,
	Klaas.Gadeyne@mech.kuleuven.ac.be,
	Frank Mori Hess <fmhess@users.sourceforge.net>
Updated: Thu Nov 16 09:50:32 EST 2006
Status: works

This module is not used directly by end-users.  Rather, it
is used by other drivers (for example ni_660x and ni_pcimio)
to provide support for NI's general purpose counters.  It was
originally based on the counter code from ni_660x.c and
ni_mio_common.c.

References:
DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
DAQ 6601/6602 User Manual (NI 322137B-01)
340934b.pdf  DAQ-STC reference manual

*/
/*
TODO:
	Support use of both banks X and Y
*/

#include <linux/module.h>
#include <linux/slab.h>

#include "ni_tio_internal.h"

/*
 * clock sources for ni e and m series boards,
 * get bits with Gi_Source_Select_Bits()
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
 * get bits with Gi_Source_Select_Bits()
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

static inline unsigned GI_ALT_SYNC(enum ni_gpct_variant variant)
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

static inline unsigned GI_PRESCALE_X2(enum ni_gpct_variant variant)
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

static inline unsigned GI_PRESCALE_X8(enum ni_gpct_variant variant)
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

static inline unsigned GI_HW_ARM_SEL_MASK(enum ni_gpct_variant variant)
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

static inline unsigned Gi_Source_Select_Bits(unsigned source)
{
	return (source << Gi_Source_Select_Shift) & Gi_Source_Select_Mask;
}

static inline unsigned Gi_Gate_Select_Bits(unsigned gate_select)
{
	return (gate_select << Gi_Gate_Select_Shift) & Gi_Gate_Select_Mask;
}

static int ni_tio_has_gate2_registers(const struct ni_gpct_device *counter_dev)
{
	switch (counter_dev->variant) {
	case ni_gpct_variant_e_series:
	default:
		return 0;
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		return 1;
	}
}

static void ni_tio_reset_count_and_disarm(struct ni_gpct *counter)
{
	unsigned cidx = counter->counter_index;

	write_register(counter, Gi_Reset_Bit(cidx), NITIO_RESET_REG(cidx));
}

static uint64_t ni_tio_clock_period_ps(const struct ni_gpct *counter,
				       unsigned generic_clock_source)
{
	uint64_t clock_period_ps;

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

static unsigned ni_tio_clock_src_modifiers(const struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	const unsigned counting_mode_bits =
		ni_tio_get_soft_copy(counter, NITIO_CNT_MODE_REG(cidx));
	unsigned bits = 0;

	if (ni_tio_get_soft_copy(counter, NITIO_INPUT_SEL_REG(cidx)) &
	    Gi_Source_Polarity_Bit)
		bits |= NI_GPCT_INVERT_CLOCK_SRC_BIT;
	if (counting_mode_bits & GI_PRESCALE_X2(counter_dev->variant))
		bits |= NI_GPCT_PRESCALE_X2_CLOCK_SRC_BITS;
	if (counting_mode_bits & GI_PRESCALE_X8(counter_dev->variant))
		bits |= NI_GPCT_PRESCALE_X8_CLOCK_SRC_BITS;
	return bits;
}

static unsigned ni_m_series_clock_src_select(const struct ni_gpct *counter)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	const unsigned second_gate_reg = NITIO_GATE2_REG(cidx);
	unsigned clock_source = 0;
	unsigned i;
	const unsigned input_select =
		(ni_tio_get_soft_copy(counter, NITIO_INPUT_SEL_REG(cidx)) &
			Gi_Source_Select_Mask) >> Gi_Source_Select_Shift;

	switch (input_select) {
	case NI_M_TIMEBASE_1_CLK:
		clock_source = NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS;
		break;
	case NI_M_TIMEBASE_2_CLK:
		clock_source = NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS;
		break;
	case NI_M_TIMEBASE_3_CLK:
		if (counter_dev->regs[second_gate_reg] &
		    Gi_Source_Subselect_Bit)
			clock_source =
			    NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS;
		else
			clock_source = NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS;
		break;
	case NI_M_LOGIC_LOW_CLK:
		clock_source = NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS;
		break;
	case NI_M_NEXT_GATE_CLK:
		if (counter_dev->regs[second_gate_reg] &
		    Gi_Source_Subselect_Bit)
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
			if (input_select == NI_M_RTSI_CLK(i)) {
				clock_source = NI_GPCT_RTSI_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (input_select == NI_M_PFI_CLK(i)) {
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

static unsigned ni_660x_clock_src_select(const struct ni_gpct *counter)
{
	unsigned clock_source = 0;
	unsigned cidx = counter->counter_index;
	const unsigned input_select =
		(ni_tio_get_soft_copy(counter, NITIO_INPUT_SEL_REG(cidx)) &
			Gi_Source_Select_Mask) >> Gi_Source_Select_Shift;
	unsigned i;

	switch (input_select) {
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
			if (input_select == NI_660X_RTSI_CLK(i)) {
				clock_source = NI_GPCT_RTSI_CLOCK_SRC_BITS(i);
				break;
			}
		}
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_SRC_PIN; ++i) {
			if (input_select == NI_660X_SRC_PIN_CLK(i)) {
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

static unsigned ni_tio_generic_clock_src_select(const struct ni_gpct *counter)
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
	unsigned cidx = counter->counter_index;
	const unsigned counting_mode_reg = NITIO_CNT_MODE_REG(cidx);
	static const uint64_t min_normal_sync_period_ps = 25000;
	unsigned mode;
	uint64_t clock_period_ps;

	if (ni_tio_counting_mode_registers_present(counter_dev) == 0)
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

static int ni_tio_set_counter_mode(struct ni_gpct *counter, unsigned mode)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned mode_reg_mask;
	unsigned mode_reg_values;
	unsigned input_select_bits = 0;
	/* these bits map directly on to the mode register */
	static const unsigned mode_reg_direct_mask =
	    NI_GPCT_GATE_ON_BOTH_EDGES_BIT | NI_GPCT_EDGE_GATE_MODE_MASK |
	    NI_GPCT_STOP_MODE_MASK | NI_GPCT_OUTPUT_MODE_MASK |
	    NI_GPCT_HARDWARE_DISARM_MASK | NI_GPCT_LOADING_ON_TC_BIT |
	    NI_GPCT_LOADING_ON_GATE_BIT | NI_GPCT_LOAD_B_SELECT_BIT;

	mode_reg_mask = mode_reg_direct_mask | Gi_Reload_Source_Switching_Bit;
	mode_reg_values = mode & mode_reg_direct_mask;
	switch (mode & NI_GPCT_RELOAD_SOURCE_MASK) {
	case NI_GPCT_RELOAD_SOURCE_FIXED_BITS:
		break;
	case NI_GPCT_RELOAD_SOURCE_SWITCHING_BITS:
		mode_reg_values |= Gi_Reload_Source_Switching_Bit;
		break;
	case NI_GPCT_RELOAD_SOURCE_GATE_SELECT_BITS:
		input_select_bits |= Gi_Gate_Select_Load_Source_Bit;
		mode_reg_mask |= Gi_Gating_Mode_Mask;
		mode_reg_values |= Gi_Level_Gating_Bits;
		break;
	default:
		break;
	}
	ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
			mode_reg_mask, mode_reg_values);

	if (ni_tio_counting_mode_registers_present(counter_dev)) {
		unsigned bits = 0;

		bits |= GI_CNT_MODE(mode >> NI_GPCT_COUNTING_MODE_SHIFT);
		bits |= GI_INDEX_PHASE((mode >> NI_GPCT_INDEX_PHASE_BITSHIFT));
		if (mode & NI_GPCT_INDEX_ENABLE_BIT)
			bits |= GI_INDEX_MODE;
		ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx),
				GI_CNT_MODE_MASK | GI_INDEX_PHASE_MASK |
				GI_INDEX_MODE, bits);
		ni_tio_set_sync_mode(counter, 0);
	}

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx),
			Gi_Up_Down_Mask,
			(mode >> NI_GPCT_COUNTING_DIRECTION_SHIFT) <<
			Gi_Up_Down_Shift);

	if (mode & NI_GPCT_OR_GATE_BIT)
		input_select_bits |= Gi_Or_Gate_Bit;
	if (mode & NI_GPCT_INVERT_OUTPUT_BIT)
		input_select_bits |= Gi_Output_Polarity_Bit;
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			Gi_Gate_Select_Load_Source_Bit | Gi_Or_Gate_Bit |
			Gi_Output_Polarity_Bit, input_select_bits);

	return 0;
}

int ni_tio_arm(struct ni_gpct *counter, int arm, unsigned start_trigger)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned command_transient_bits = 0;

	if (arm) {
		switch (start_trigger) {
		case NI_GPCT_ARM_IMMEDIATE:
			command_transient_bits |= Gi_Arm_Bit;
			break;
		case NI_GPCT_ARM_PAIRED_IMMEDIATE:
			command_transient_bits |= Gi_Arm_Bit | Gi_Arm_Copy_Bit;
			break;
		default:
			break;
		}
		if (ni_tio_counting_mode_registers_present(counter_dev)) {
			unsigned bits = 0;
			unsigned sel_mask;

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
		command_transient_bits |= Gi_Disarm_Bit;
	}
	ni_tio_set_bits_transient(counter, NITIO_CMD_REG(cidx),
				  0, 0, command_transient_bits);
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_arm);

static unsigned ni_660x_clk_src(unsigned int clock_source)
{
	unsigned clk_src = clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK;
	unsigned ni_660x_clock;
	unsigned i;

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
	return Gi_Source_Select_Bits(ni_660x_clock);
}

static unsigned ni_m_clk_src(unsigned int clock_source)
{
	unsigned clk_src = clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK;
	unsigned ni_m_series_clock;
	unsigned i;

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
	return Gi_Source_Select_Bits(ni_m_series_clock);
};

static void ni_tio_set_source_subselect(struct ni_gpct *counter,
					unsigned int clock_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	const unsigned second_gate_reg = NITIO_GATE2_REG(cidx);

	if (counter_dev->variant != ni_gpct_variant_m_series)
		return;
	switch (clock_source & NI_GPCT_CLOCK_SRC_SELECT_MASK) {
		/* Gi_Source_Subselect is zero */
	case NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS:
	case NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS:
		counter_dev->regs[second_gate_reg] &= ~Gi_Source_Subselect_Bit;
		break;
		/* Gi_Source_Subselect is one */
	case NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS:
	case NI_GPCT_PXI_STAR_TRIGGER_CLOCK_SRC_BITS:
		counter_dev->regs[second_gate_reg] |= Gi_Source_Subselect_Bit;
		break;
		/* Gi_Source_Subselect doesn't matter */
	default:
		return;
	}
	write_register(counter, counter_dev->regs[second_gate_reg],
		       second_gate_reg);
}

static int ni_tio_set_clock_src(struct ni_gpct *counter,
				unsigned int clock_source,
				unsigned int period_ns)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned bits = 0;

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
		bits |= Gi_Source_Polarity_Bit;
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx),
			Gi_Source_Select_Mask | Gi_Source_Polarity_Bit, bits);
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
	uint64_t temp64;

	*clock_source = ni_tio_generic_clock_src_select(counter);
	temp64 = ni_tio_clock_period_ps(counter, *clock_source);
	do_div(temp64, 1000);	/* ps to ns */
	*period_ns = temp64;
}

static int ni_660x_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
	unsigned cidx = counter->counter_index;
	unsigned gate_sel;
	unsigned i;

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
			Gi_Gate_Select_Mask, Gi_Gate_Select_Bits(gate_sel));
	return 0;
}

static int ni_m_set_gate(struct ni_gpct *counter, unsigned int gate_source)
{
	unsigned int chan = CR_CHAN(gate_source);
	unsigned cidx = counter->counter_index;
	unsigned gate_sel;
	unsigned i;

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
			Gi_Gate_Select_Mask, Gi_Gate_Select_Bits(gate_sel));
	return 0;
}

static int ni_660x_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(gate_source);
	unsigned gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned gate2_sel;
	unsigned i;

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
	counter_dev->regs[gate2_reg] |= Gi_Second_Gate_Mode_Bit;
	counter_dev->regs[gate2_reg] &= ~Gi_Second_Gate_Select_Mask;
	counter_dev->regs[gate2_reg] |= Gi_Second_Gate_Select_Bits(gate2_sel);
	write_register(counter, counter_dev->regs[gate2_reg], gate2_reg);
	return 0;
}

static int ni_m_set_gate2(struct ni_gpct *counter, unsigned int gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(gate_source);
	unsigned gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned gate2_sel;

	/*
	 * FIXME: We don't know what the m-series second gate codes are,
	 * so we'll just pass the bits through for now.
	 */
	switch (chan) {
	default:
		gate2_sel = chan & 0x1f;
		break;
	}
	counter_dev->regs[gate2_reg] |= Gi_Second_Gate_Mode_Bit;
	counter_dev->regs[gate2_reg] &= ~Gi_Second_Gate_Select_Mask;
	counter_dev->regs[gate2_reg] |= Gi_Second_Gate_Select_Bits(gate2_sel);
	write_register(counter, counter_dev->regs[gate2_reg], gate2_reg);
	return 0;
}

int ni_tio_set_gate_src(struct ni_gpct *counter, unsigned gate_index,
			unsigned int gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned int chan = CR_CHAN(gate_source);
	unsigned gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned mode = 0;

	switch (gate_index) {
	case 0:
		if (chan == NI_GPCT_DISABLED_GATE_SELECT) {
			ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
					Gi_Gating_Mode_Mask,
					Gi_Gating_Disabled_Bits);
			return 0;
		}
		if (gate_source & CR_INVERT)
			mode |= Gi_Gate_Polarity_Bit;
		if (gate_source & CR_EDGE)
			mode |= Gi_Rising_Edge_Gating_Bits;
		else
			mode |= Gi_Level_Gating_Bits;
		ni_tio_set_bits(counter, NITIO_MODE_REG(cidx),
				Gi_Gate_Polarity_Bit | Gi_Gating_Mode_Mask,
				mode);
		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			return ni_m_set_gate(counter, gate_source);
		case ni_gpct_variant_660x:
			return ni_660x_set_gate(counter, gate_source);
		}
		break;
	case 1:
		if (!ni_tio_has_gate2_registers(counter_dev))
			return -EINVAL;

		if (chan == NI_GPCT_DISABLED_GATE_SELECT) {
			counter_dev->regs[gate2_reg] &=
						~Gi_Second_Gate_Mode_Bit;
			write_register(counter, counter_dev->regs[gate2_reg],
				       gate2_reg);
			return 0;
		}
		if (gate_source & CR_INVERT) {
			counter_dev->regs[gate2_reg] |=
						Gi_Second_Gate_Polarity_Bit;
		} else {
			counter_dev->regs[gate2_reg] &=
						~Gi_Second_Gate_Polarity_Bit;
		}
		switch (counter_dev->variant) {
		case ni_gpct_variant_m_series:
			return ni_m_set_gate2(counter, gate_source);
		case ni_gpct_variant_660x:
			return ni_660x_set_gate2(counter, gate_source);
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

static int ni_tio_set_other_src(struct ni_gpct *counter, unsigned index,
				unsigned int source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
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
	write_register(counter, counter_dev->regs[abz_reg], abz_reg);
	return 0;
}

static unsigned ni_660x_gate_to_generic_gate(unsigned gate)
{
	unsigned i;

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
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_GATE_PIN; ++i) {
			if (gate == NI_660X_PIN_GATE_SEL(i))
				return NI_GPCT_GATE_PIN_GATE_SELECT(i);
		}
		if (i <= NI_660X_MAX_GATE_PIN)
			break;
		BUG();
		break;
	}
	return 0;
};

static unsigned ni_m_gate_to_generic_gate(unsigned gate)
{
	unsigned i;

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
		if (i <= NI_M_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_M_MAX_PFI_CHAN; ++i) {
			if (gate == NI_M_PFI_GATE_SEL(i))
				return NI_GPCT_PFI_GATE_SELECT(i);
		}
		if (i <= NI_M_MAX_PFI_CHAN)
			break;
		BUG();
		break;
	}
	return 0;
};

static unsigned ni_660x_gate2_to_generic_gate(unsigned gate)
{
	unsigned i;

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
		if (i <= NI_660X_MAX_RTSI_CHAN)
			break;
		for (i = 0; i <= NI_660X_MAX_UP_DOWN_PIN; ++i) {
			if (gate == NI_660X_UD_PIN_GATE2_SEL(i))
				return NI_GPCT_UP_DOWN_PIN_GATE_SELECT(i);
		}
		if (i <= NI_660X_MAX_UP_DOWN_PIN)
			break;
		BUG();
		break;
	}
	return 0;
};

static unsigned ni_m_gate2_to_generic_gate(unsigned gate)
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

static int ni_tio_get_gate_src(struct ni_gpct *counter, unsigned gate_index,
			       unsigned int *gate_source)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned mode = ni_tio_get_soft_copy(counter, NITIO_MODE_REG(cidx));
	unsigned gate2_reg = NITIO_GATE2_REG(cidx);
	unsigned gate_sel;

	switch (gate_index) {
	case 0:
		if ((mode & Gi_Gating_Mode_Mask) == Gi_Gating_Disabled_Bits) {
			*gate_source = NI_GPCT_DISABLED_GATE_SELECT;
			return 0;
		}

		gate_sel = ni_tio_get_soft_copy(counter,
						NITIO_INPUT_SEL_REG(cidx));
		gate_sel &= Gi_Gate_Select_Mask;
		gate_sel >>= Gi_Gate_Select_Shift;

		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			*gate_source = ni_m_gate_to_generic_gate(gate_sel);
			break;
		case ni_gpct_variant_660x:
			*gate_source = ni_660x_gate_to_generic_gate(gate_sel);
			break;
		}
		if (mode & Gi_Gate_Polarity_Bit)
			*gate_source |= CR_INVERT;
		if ((mode & Gi_Gating_Mode_Mask) != Gi_Level_Gating_Bits)
			*gate_source |= CR_EDGE;
		break;
	case 1:
		if ((mode & Gi_Gating_Mode_Mask) == Gi_Gating_Disabled_Bits ||
		    !(counter_dev->regs[gate2_reg] & Gi_Second_Gate_Mode_Bit)) {
			*gate_source = NI_GPCT_DISABLED_GATE_SELECT;
			return 0;
		}

		gate_sel = counter_dev->regs[gate2_reg];
		gate_sel &= Gi_Second_Gate_Select_Mask;
		gate_sel >>= Gi_Second_Gate_Select_Shift;

		switch (counter_dev->variant) {
		case ni_gpct_variant_e_series:
		case ni_gpct_variant_m_series:
		default:
			*gate_source = ni_m_gate2_to_generic_gate(gate_sel);
			break;
		case ni_gpct_variant_660x:
			*gate_source = ni_660x_gate2_to_generic_gate(gate_sel);
			break;
		}
		if (counter_dev->regs[gate2_reg] & Gi_Second_Gate_Polarity_Bit)
			*gate_source |= CR_INVERT;
		/* second gate can't have edge/level mode set independently */
		if ((mode & Gi_Gating_Mode_Mask) != Gi_Level_Gating_Bits)
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
	unsigned cidx = counter->counter_index;
	unsigned status;

	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		return ni_tio_set_counter_mode(counter, data[1]);
	case INSN_CONFIG_ARM:
		return ni_tio_arm(counter, 1, data[1]);
	case INSN_CONFIG_DISARM:
		ni_tio_arm(counter, 0, 0);
		return 0;
	case INSN_CONFIG_GET_COUNTER_STATUS:
		data[1] = 0;
		status = read_register(counter, NITIO_SHARED_STATUS_REG(cidx));
		if (status & Gi_Armed_Bit(cidx)) {
			data[1] |= COMEDI_COUNTER_ARMED;
			if (status & Gi_Counting_Bit(cidx))
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
	unsigned cidx = counter->counter_index;
	unsigned int val;

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx), Gi_Save_Trace_Bit, 0);
	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx),
			Gi_Save_Trace_Bit, Gi_Save_Trace_Bit);

	/*
	 * The count doesn't get latched until the next clock edge, so it is
	 * possible the count may change (once) while we are reading. Since
	 * the read of the SW_Save_Reg isn't atomic (apparently even when it's
	 * a 32 bit register according to 660x docs), we need to read twice
	 * and make sure the reading hasn't changed. If it has, a third read
	 * will be correct since the count value will definitely have latched
	 * by then.
	 */
	val = read_register(counter, NITIO_SW_SAVE_REG(cidx));
	if (val != read_register(counter, NITIO_SW_SAVE_REG(cidx)))
		val = read_register(counter, NITIO_SW_SAVE_REG(cidx));

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
	unsigned cidx = counter->counter_index;
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

static unsigned ni_tio_next_load_register(struct ni_gpct *counter)
{
	unsigned cidx = counter->counter_index;
	const unsigned bits =
		read_register(counter, NITIO_SHARED_STATUS_REG(cidx));

	return (bits & Gi_Next_Load_Source_Bit(cidx))
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
	const unsigned channel = CR_CHAN(insn->chanspec);
	unsigned cidx = counter->counter_index;
	unsigned load_reg;

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
		write_register(counter, data[0], load_reg);
		ni_tio_set_bits_transient(counter, NITIO_CMD_REG(cidx),
					  0, 0, Gi_Load_Bit);
		/* restore load reg */
		write_register(counter, counter_dev->regs[load_reg], load_reg);
		break;
	case 1:
		counter_dev->regs[NITIO_LOADA_REG(cidx)] = data[0];
		write_register(counter, data[0], NITIO_LOADA_REG(cidx));
		break;
	case 2:
		counter_dev->regs[NITIO_LOADB_REG(cidx)] = data[0];
		write_register(counter, data[0], NITIO_LOADB_REG(cidx));
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
	unsigned cidx = counter->counter_index;

	ni_tio_reset_count_and_disarm(counter);

	/* initialize counter registers */
	counter_dev->regs[NITIO_AUTO_INC_REG(cidx)] = 0x0;
	write_register(counter, 0x0, NITIO_AUTO_INC_REG(cidx));

	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx),
			~0, Gi_Synchronize_Gate_Bit);

	ni_tio_set_bits(counter, NITIO_MODE_REG(cidx), ~0, 0);

	counter_dev->regs[NITIO_LOADA_REG(cidx)] = 0x0;
	write_register(counter, 0x0, NITIO_LOADA_REG(cidx));

	counter_dev->regs[NITIO_LOADB_REG(cidx)] = 0x0;
	write_register(counter, 0x0, NITIO_LOADB_REG(cidx));

	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx), ~0, 0);

	if (ni_tio_counting_mode_registers_present(counter_dev))
		ni_tio_set_bits(counter, NITIO_CNT_MODE_REG(cidx), ~0, 0);

	if (ni_tio_has_gate2_registers(counter_dev)) {
		counter_dev->regs[NITIO_GATE2_REG(cidx)] = 0x0;
		write_register(counter, 0x0, NITIO_GATE2_REG(cidx));
	}

	ni_tio_set_bits(counter, NITIO_DMA_CFG_REG(cidx), ~0, 0x0);

	ni_tio_set_bits(counter, NITIO_INT_ENA_REG(cidx), ~0, 0x0);
}
EXPORT_SYMBOL_GPL(ni_tio_init_counter);

struct ni_gpct_device *
ni_gpct_device_construct(struct comedi_device *dev,
			 void (*write_register)(struct ni_gpct *counter,
						unsigned bits,
						enum ni_gpct_register reg),
			 unsigned (*read_register)(struct ni_gpct *counter,
						   enum ni_gpct_register reg),
			 enum ni_gpct_variant variant,
			 unsigned num_counters)
{
	struct ni_gpct_device *counter_dev;
	struct ni_gpct *counter;
	unsigned i;

	if (num_counters == 0)
		return NULL;

	counter_dev = kzalloc(sizeof(*counter_dev), GFP_KERNEL);
	if (!counter_dev)
		return NULL;

	counter_dev->dev = dev;
	counter_dev->write_register = write_register;
	counter_dev->read_register = read_register;
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
	if (!counter_dev->counters)
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
