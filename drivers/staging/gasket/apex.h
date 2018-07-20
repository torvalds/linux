/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Apex kernel-userspace interface definition(s).
 *
 * Copyright (C) 2018 Google, Inc.
 */
#ifndef __APEX_H__
#define __APEX_H__

#include <linux/ioctl.h>
#include <linux/bitops.h>

#include "gasket.h"

/* Structural definitions/macros. */
/* The number of PCI BARs. */
#define APEX_NUM_BARS 3

/* Size of a memory page in bytes, and the related number of bits to shift. */
#define APEX_PAGE_SHIFT 12
#define APEX_PAGE_SIZE BIT(APEX_PAGE_SHIFT)

#define APEX_EXTENDED_SHIFT 63 /* Extended address bit position. */

/*
 * Addresses are 2^3=8 bytes each. Page in second level page table holds
 * APEX_PAGE_SIZE/8 addresses.
 */
#define APEX_ADDR_SHIFT 3
#define APEX_LEVEL_SHIFT (APEX_PAGE_SHIFT - APEX_ADDR_SHIFT)
#define APEX_LEVEL_SIZE BIT(APEX_LEVEL_SHIFT)

#define APEX_PAGE_TABLE_MAX 65536
#define APEX_SIMPLE_PAGE_MAX APEX_PAGE_TABLE_MAX
#define APEX_EXTENDED_PAGE_MAX (APEX_PAGE_TABLE_MAX << APEX_LEVEL_SHIFT)

/* Check reset 120 times */
#define APEX_RESET_RETRY 120
/* Wait 100 ms between checks. Total 12 sec wait maximum. */
#define APEX_RESET_DELAY 100

#define APEX_CHIP_INIT_DONE 2
#define APEX_RESET_ACCEPTED 0

enum apex_reset_types {
	APEX_CHIP_REINIT_RESET = 3,
};

/* Interrupt defines */
/* Gasket device interrupts enums must be dense (i.e., no empty slots). */
enum apex_interrupt {
	APEX_INTERRUPT_INSTR_QUEUE = 0,
	APEX_INTERRUPT_INPUT_ACTV_QUEUE = 1,
	APEX_INTERRUPT_PARAM_QUEUE = 2,
	APEX_INTERRUPT_OUTPUT_ACTV_QUEUE = 3,
	APEX_INTERRUPT_SC_HOST_0 = 4,
	APEX_INTERRUPT_SC_HOST_1 = 5,
	APEX_INTERRUPT_SC_HOST_2 = 6,
	APEX_INTERRUPT_SC_HOST_3 = 7,
	APEX_INTERRUPT_TOP_LEVEL_0 = 8,
	APEX_INTERRUPT_TOP_LEVEL_1 = 9,
	APEX_INTERRUPT_TOP_LEVEL_2 = 10,
	APEX_INTERRUPT_TOP_LEVEL_3 = 11,
	APEX_INTERRUPT_FATAL_ERR = 12,
	APEX_INTERRUPT_COUNT = 13,
};

/*
 * Clock Gating ioctl.
 */
struct apex_gate_clock_ioctl {
	/* Enter or leave clock gated state. */
	u64 enable;

	/* If set, enter clock gating state, regardless of custom block's
	 * internal idle state
	 */
	u64 force_idle;
};

/* Base number for all Apex-common IOCTLs */
#define APEX_IOCTL_BASE 0x7F

/* Enable/Disable clock gating. */
#define APEX_IOCTL_GATE_CLOCK                                                  \
	_IOW(APEX_IOCTL_BASE, 0, struct apex_gate_clock_ioctl)

#endif /* __APEX_H__ */
