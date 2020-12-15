// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2020 Intel Corporation */

#include "igc.h"
#include "igc_diag.h"

static struct igc_reg_test reg_test[] = {
	{ IGC_FCAL,	1,	PATTERN_TEST,	0xFFFFFFFF,	0xFFFFFFFF },
	{ IGC_FCAH,	1,	PATTERN_TEST,	0x0000FFFF,	0xFFFFFFFF },
	{ IGC_FCT,	1,	PATTERN_TEST,	0x0000FFFF,	0xFFFFFFFF },
	{ IGC_RDBAH(0), 4,	PATTERN_TEST,	0xFFFFFFFF,	0xFFFFFFFF },
	{ IGC_RDBAL(0),	4,	PATTERN_TEST,	0xFFFFFF80,	0xFFFFFF80 },
	{ IGC_RDLEN(0),	4,	PATTERN_TEST,	0x000FFF80,	0x000FFFFF },
	{ IGC_RDT(0),	4,	PATTERN_TEST,	0x0000FFFF,	0x0000FFFF },
	{ IGC_FCRTH,	1,	PATTERN_TEST,	0x0003FFF0,	0x0003FFF0 },
	{ IGC_FCTTV,	1,	PATTERN_TEST,	0x0000FFFF,	0x0000FFFF },
	{ IGC_TIPG,	1,	PATTERN_TEST,	0x3FFFFFFF,	0x3FFFFFFF },
	{ IGC_TDBAH(0),	4,	PATTERN_TEST,	0xFFFFFFFF,	0xFFFFFFFF },
	{ IGC_TDBAL(0),	4,	PATTERN_TEST,	0xFFFFFF80,	0xFFFFFF80 },
	{ IGC_TDLEN(0),	4,	PATTERN_TEST,	0x000FFF80,	0x000FFFFF },
	{ IGC_TDT(0),	4,	PATTERN_TEST,	0x0000FFFF,	0x0000FFFF },
	{ IGC_RCTL,	1,	SET_READ_TEST,	0xFFFFFFFF,	0x00000000 },
	{ IGC_RCTL,	1,	SET_READ_TEST,	0x04CFB2FE,	0x003FFFFB },
	{ IGC_RCTL,	1,	SET_READ_TEST,	0x04CFB2FE,	0xFFFFFFFF },
	{ IGC_TCTL,	1,	SET_READ_TEST,	0xFFFFFFFF,	0x00000000 },
	{ IGC_RA,	16,	TABLE64_TEST_LO,
						0xFFFFFFFF,	0xFFFFFFFF },
	{ IGC_RA,	16,	TABLE64_TEST_HI,
						0x900FFFFF,	0xFFFFFFFF },
	{ IGC_MTA,	128,	TABLE32_TEST,
						0xFFFFFFFF,	0xFFFFFFFF },
	{ 0, 0, 0, 0}
};

static bool reg_pattern_test(struct igc_adapter *adapter, u64 *data, int reg,
			     u32 mask, u32 write)
{
	struct igc_hw *hw = &adapter->hw;
	u32 pat, val, before;
	static const u32 test_pattern[] = {
		0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF
	};

	for (pat = 0; pat < ARRAY_SIZE(test_pattern); pat++) {
		before = rd32(reg);
		wr32(reg, test_pattern[pat] & write);
		val = rd32(reg);
		if (val != (test_pattern[pat] & write & mask)) {
			netdev_err(adapter->netdev,
				   "pattern test reg %04X failed: got 0x%08X expected 0x%08X",
				   reg, val, test_pattern[pat] & write & mask);
			*data = reg;
			wr32(reg, before);
			return false;
		}
		wr32(reg, before);
	}
	return true;
}

static bool reg_set_and_check(struct igc_adapter *adapter, u64 *data, int reg,
			      u32 mask, u32 write)
{
	struct igc_hw *hw = &adapter->hw;
	u32 val, before;

	before = rd32(reg);
	wr32(reg, write & mask);
	val = rd32(reg);
	if ((write & mask) != (val & mask)) {
		netdev_err(adapter->netdev,
			   "set/check reg %04X test failed: got 0x%08X expected 0x%08X",
			   reg, (val & mask), (write & mask));
		*data = reg;
		wr32(reg, before);
		return false;
	}
	wr32(reg, before);
	return true;
}

bool igc_reg_test(struct igc_adapter *adapter, u64 *data)
{
	struct igc_reg_test *test = reg_test;
	struct igc_hw *hw = &adapter->hw;
	u32 value, before, after;
	u32 i, toggle, b = false;

	/* Because the status register is such a special case,
	 * we handle it separately from the rest of the register
	 * tests.  Some bits are read-only, some toggle, and some
	 * are writeable.
	 */
	toggle = 0x6800D3;
	before = rd32(IGC_STATUS);
	value = before & toggle;
	wr32(IGC_STATUS, toggle);
	after = rd32(IGC_STATUS) & toggle;
	if (value != after) {
		netdev_err(adapter->netdev,
			   "failed STATUS register test got: 0x%08X expected: 0x%08X",
			   after, value);
		*data = 1;
		return false;
	}
	/* restore previous status */
	wr32(IGC_STATUS, before);

	/* Perform the remainder of the register test, looping through
	 * the test table until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			switch (test->test_type) {
			case PATTERN_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 0x40),
						     test->mask,
						     test->write);
				break;
			case SET_READ_TEST:
				b = reg_set_and_check(adapter, data,
						      test->reg + (i * 0x40),
						      test->mask,
						      test->write);
				break;
			case TABLE64_TEST_LO:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 8),
						     test->mask,
						     test->write);
				break;
			case TABLE64_TEST_HI:
				b = reg_pattern_test(adapter, data,
						     test->reg + 4 + (i * 8),
						     test->mask,
						     test->write);
				break;
			case TABLE32_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 4),
						     test->mask,
						     test->write);
				break;
			}
			if (!b)
				return false;
		}
		test++;
	}
	*data = 0;
	return true;
}

bool igc_eeprom_test(struct igc_adapter *adapter, u64 *data)
{
	struct igc_hw *hw = &adapter->hw;

	*data = 0;

	if (hw->nvm.ops.validate(hw) != IGC_SUCCESS) {
		*data = 1;
		return false;
	}

	return true;
}

bool igc_link_test(struct igc_adapter *adapter, u64 *data)
{
	bool link_up;

	*data = 0;

	/* add delay to give enough time for autonegotioation to finish */
	if (adapter->hw.mac.autoneg)
		ssleep(5);

	link_up = igc_has_link(adapter);
	if (!link_up) {
		*data = 1;
		return false;
	}

	return true;
}
