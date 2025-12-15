/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 MIPS.
 */

#ifndef _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H
#define _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H

#include <linux/types.h>

#define RISCV_ISA_VENDOR_EXT_XMIPSEXECTL	0

#ifndef __ASSEMBLER__
struct riscv_isa_vendor_ext_data_list;
extern struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_mips;
#endif

/* Extension specific instructions */

/*
 * All of the xmipsexectl extension instructions are
 * ‘hint’ encodings of the SLLI instruction,
 * with rd = 0, rs1 = 0 and imm = 1 for IHB, imm = 3 for EHB,
 * and imm = 5 for PAUSE.
 * MIPS.PAUSE is an alternative opcode which is implemented to have the
 * same behavior as PAUSE on some MIPS RISCV cores.
 * MIPS.EHB clears all execution hazards before allowing
 * any subsequent instructions to execute.
 * MIPS.IHB clears all instruction hazards before
 * allowing any subsequent instructions to fetch.
 */

#define MIPS_PAUSE	ASM_INSN_I("0x00501013\n\t")
#define MIPS_EHB	ASM_INSN_I("0x00301013\n\t")
#define MIPS_IHB	ASM_INSN_I("0x00101013\n\t")

#endif // _ASM_RISCV_VENDOR_EXTENSIONS_MIPS_H
