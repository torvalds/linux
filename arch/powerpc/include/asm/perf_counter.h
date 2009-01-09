/*
 * Performance counter support - PowerPC-specific definitions.
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>

#define MAX_HWCOUNTERS		8
#define MAX_EVENT_ALTERNATIVES	8

/*
 * This struct provides the constants and functions needed to
 * describe the PMU on a particular POWER-family CPU.
 */
struct power_pmu {
	int	n_counter;
	int	max_alternatives;
	u64	add_fields;
	u64	test_adder;
	int	(*compute_mmcr)(unsigned int events[], int n_ev,
				unsigned int hwc[], u64 mmcr[]);
	int	(*get_constraint)(unsigned int event, u64 *mskp, u64 *valp);
	int	(*get_alternatives)(unsigned int event, unsigned int alt[]);
	void	(*disable_pmc)(unsigned int pmc, u64 mmcr[]);
	int	n_generic;
	int	*generic_events;
};

extern struct power_pmu *ppmu;

/*
 * The power_pmu.get_constraint function returns a 64-bit value and
 * a 64-bit mask that express the constraints between this event and
 * other events.
 *
 * The value and mask are divided up into (non-overlapping) bitfields
 * of three different types:
 *
 * Select field: this expresses the constraint that some set of bits
 * in MMCR* needs to be set to a specific value for this event.  For a
 * select field, the mask contains 1s in every bit of the field, and
 * the value contains a unique value for each possible setting of the
 * MMCR* bits.  The constraint checking code will ensure that two events
 * that set the same field in their masks have the same value in their
 * value dwords.
 *
 * Add field: this expresses the constraint that there can be at most
 * N events in a particular class.  A field of k bits can be used for
 * N <= 2^(k-1) - 1.  The mask has the most significant bit of the field
 * set (and the other bits 0), and the value has only the least significant
 * bit of the field set.  In addition, the 'add_fields' and 'test_adder'
 * in the struct power_pmu for this processor come into play.  The
 * add_fields value contains 1 in the LSB of the field, and the
 * test_adder contains 2^(k-1) - 1 - N in the field.
 *
 * NAND field: this expresses the constraint that you may not have events
 * in all of a set of classes.  (For example, on PPC970, you can't select
 * events from the FPU, ISU and IDU simultaneously, although any two are
 * possible.)  For N classes, the field is N+1 bits wide, and each class
 * is assigned one bit from the least-significant N bits.  The mask has
 * only the most-significant bit set, and the value has only the bit
 * for the event's class set.  The test_adder has the least significant
 * bit set in the field.
 *
 * If an event is not subject to the constraint expressed by a particular
 * field, then it will have 0 in both the mask and value for that field.
 */
