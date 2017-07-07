/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Marcin Nowakowski <marcin.nowakowski@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __PROBES_COMMON_H
#define __PROBES_COMMON_H

#include <asm/inst.h>

int __insn_is_compact_branch(union mips_instruction insn);

static inline int __insn_has_delay_slot(const union mips_instruction insn)
{
	switch (insn.i_format.opcode) {
	/*
	 * jr and jalr are in r_format format.
	 */
	case spec_op:
		switch (insn.r_format.func) {
		case jalr_op:
		case jr_op:
			return 1;
		}
		break;

	/*
	 * This group contains:
	 * bltz_op, bgez_op, bltzl_op, bgezl_op,
	 * bltzal_op, bgezal_op, bltzall_op, bgezall_op.
	 */
	case bcond_op:
		switch (insn.i_format.rt) {
		case bltz_op:
		case bltzl_op:
		case bgez_op:
		case bgezl_op:
		case bltzal_op:
		case bltzall_op:
		case bgezal_op:
		case bgezall_op:
		case bposge32_op:
			return 1;
		}
		break;

	/*
	 * These are unconditional and in j_format.
	 */
	case jal_op:
	case j_op:
	case beq_op:
	case beql_op:
	case bne_op:
	case bnel_op:
	case blez_op: /* not really i_format */
	case blezl_op:
	case bgtz_op:
	case bgtzl_op:
		return 1;

	/*
	 * And now the FPA/cp1 branch instructions.
	 */
	case cop1_op:
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	case lwc2_op: /* This is bbit0 on Octeon */
	case ldc2_op: /* This is bbit032 on Octeon */
	case swc2_op: /* This is bbit1 on Octeon */
	case sdc2_op: /* This is bbit132 on Octeon */
#endif
		return 1;
	}

	return 0;
}

#endif  /* __PROBES_COMMON_H */
