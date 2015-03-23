/*
 * Copyright (C) 2012 Rabin Vincent <rabin at rab.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARM_KERNEL_UPROBES_H
#define __ARM_KERNEL_UPROBES_H

enum probes_insn uprobe_decode_ldmstm(probes_opcode_t insn,
				      struct arch_probes_insn *asi,
				      const struct decode_header *d);

enum probes_insn decode_ldr(probes_opcode_t insn,
			    struct arch_probes_insn *asi,
			    const struct decode_header *d);

enum probes_insn
decode_rd12rn16rm0rs8_rwflags(probes_opcode_t insn,
			      struct arch_probes_insn *asi,
			      const struct decode_header *d);

enum probes_insn
decode_wb_pc(probes_opcode_t insn, struct arch_probes_insn *asi,
	     const struct decode_header *d, bool alu);

enum probes_insn
decode_pc_ro(probes_opcode_t insn, struct arch_probes_insn *asi,
	     const struct decode_header *d);

extern const union decode_action uprobes_probes_actions[];

#endif
