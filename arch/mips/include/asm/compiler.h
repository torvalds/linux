/*
 * Copyright (C) 2004, 2007  Maciej W. Rozycki
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_COMPILER_H
#define _ASM_COMPILER_H

/*
 * With GCC 4.5 onwards we can use __builtin_unreachable to indicate to the
 * compiler that a particular code path will never be hit. This allows it to be
 * optimised out of the generated binary.
 *
 * Unfortunately at least GCC 4.6.3 through 7.3.0 inclusive suffer from a bug
 * that can lead to instructions from beyond an unreachable statement being
 * incorrectly reordered into earlier delay slots if the unreachable statement
 * is the only content of a case in a switch statement. This can lead to
 * seemingly random behaviour, such as invalid memory accesses from incorrectly
 * reordered loads or stores. See this potential GCC fix for details:
 *
 *   https://gcc.gnu.org/ml/gcc-patches/2015-09/msg00360.html
 *
 * It is unclear whether GCC 8 onwards suffer from the same issue - nothing
 * relevant is mentioned in GCC 8 release notes and nothing obviously relevant
 * stands out in GCC commit logs, but these newer GCC versions generate very
 * different code for the testcase which doesn't exhibit the bug.
 *
 * GCC also handles stack allocation suboptimally when calling noreturn
 * functions or calling __builtin_unreachable():
 *
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82365
 *
 * We work around both of these issues by placing a volatile asm statement,
 * which GCC is prevented from reordering past, prior to __builtin_unreachable
 * calls.
 *
 * The .insn statement is required to ensure that any branches to the
 * statement, which sadly must be kept due to the asm statement, are known to
 * be branches to code and satisfy linker requirements for microMIPS kernels.
 */
#undef barrier_before_unreachable
#define barrier_before_unreachable() asm volatile(".insn")

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#define GCC_IMM_ASM() "n"
#define GCC_REG_ACCUM "$0"
#else
#define GCC_IMM_ASM() "rn"
#define GCC_REG_ACCUM "accum"
#endif

#ifdef CONFIG_CPU_MIPSR6
/* All MIPS R6 toolchains support the ZC constrain */
#define GCC_OFF_SMALL_ASM() "ZC"
#else
#ifndef CONFIG_CPU_MICROMIPS
#define GCC_OFF_SMALL_ASM() "R"
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9)
#define GCC_OFF_SMALL_ASM() "ZC"
#else
#error "microMIPS compilation unsupported with GCC older than 4.9"
#endif /* CONFIG_CPU_MICROMIPS */
#endif /* CONFIG_CPU_MIPSR6 */

#ifdef CONFIG_CPU_MIPSR6
#define MIPS_ISA_LEVEL "mips64r6"
#define MIPS_ISA_ARCH_LEVEL MIPS_ISA_LEVEL
#define MIPS_ISA_LEVEL_RAW mips64r6
#define MIPS_ISA_ARCH_LEVEL_RAW MIPS_ISA_LEVEL_RAW
#else
/* MIPS64 is a superset of MIPS32 */
#define MIPS_ISA_LEVEL "mips64r2"
#define MIPS_ISA_ARCH_LEVEL "arch=r4000"
#define MIPS_ISA_LEVEL_RAW mips64r2
#define MIPS_ISA_ARCH_LEVEL_RAW MIPS_ISA_LEVEL_RAW
#endif /* CONFIG_CPU_MIPSR6 */

#endif /* _ASM_COMPILER_H */
