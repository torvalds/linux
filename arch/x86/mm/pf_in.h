/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Fault Injection Test harness (FI)
 *  Copyright (C) Intel Crop.
 */

#ifndef __PF_H_
#define __PF_H_

enum reason_type {
	NOT_ME,	/* page fault is not in regions */
	NOTHING,	/* access others point in regions */
	REG_READ,	/* read from addr to reg */
	REG_WRITE,	/* write from reg to addr */
	IMM_WRITE,	/* write from imm to addr */
	OTHERS	/* Other instructions can not intercept */
};

enum reason_type get_ins_type(unsigned long ins_addr);
unsigned int get_ins_mem_width(unsigned long ins_addr);
unsigned long get_ins_reg_val(unsigned long ins_addr, struct pt_regs *regs);
unsigned long get_ins_imm_val(unsigned long ins_addr);

#endif /* __PF_H_ */
