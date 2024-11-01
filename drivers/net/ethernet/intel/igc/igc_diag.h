/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2020 Intel Corporation */

bool igc_reg_test(struct igc_adapter *adapter, u64 *data);
bool igc_eeprom_test(struct igc_adapter *adapter, u64 *data);
bool igc_link_test(struct igc_adapter *adapter, u64 *data);

struct igc_reg_test {
	u16 reg;
	u8 array_len;
	u8 test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x40 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST	1
#define SET_READ_TEST	2
#define TABLE32_TEST	3
#define TABLE64_TEST_LO	4
#define TABLE64_TEST_HI	5
