/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-2016 Pratyush Anand <panand@redhat.com>
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/probes.h>

#define MAX_UINSN_BYTES		AARCH64_INSN_SIZE

#define UPROBE_SWBP_INSN	cpu_to_le32(BRK64_OPCODE_UPROBES)
#define UPROBE_SWBP_INSN_SIZE	AARCH64_INSN_SIZE
#define UPROBE_XOL_SLOT_BYTES	MAX_UINSN_BYTES

typedef u32 uprobe_opcode_t;

struct arch_uprobe_task {
};

struct arch_uprobe {
	union {
		u8 insn[MAX_UINSN_BYTES];
		u8 ixol[MAX_UINSN_BYTES];
	};
	struct arch_probe_insn api;
	bool simulate;
};

#endif
