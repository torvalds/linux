/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_VM_FAULT_H
#define _ASM_HEXAGON_VM_FAULT_H

extern void execute_protection_fault(struct pt_regs *);
extern void write_protection_fault(struct pt_regs *);
extern void read_protection_fault(struct pt_regs *);

#endif
