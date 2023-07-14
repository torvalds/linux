/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Vector Instructions
 *
 * This wrapper header file allows to use the vector instruction macros in
 * both assembler files as well as in inline assemblies in C files.
 */

#ifndef __ASM_S390_VX_INSN_H
#define __ASM_S390_VX_INSN_H

#include <asm/vx-insn-asm.h>

#ifndef __ASSEMBLY__

asm(".include \"asm/vx-insn-asm.h\"\n");

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_S390_VX_INSN_H */
