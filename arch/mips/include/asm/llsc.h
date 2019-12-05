/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Macros for 32/64-bit neutral inline assembler
 */

#ifndef __ASM_LLSC_H
#define __ASM_LLSC_H

#include <asm/isa-rev.h>

#if _MIPS_SZLONG == 32
#define __LL		"ll	"
#define __SC		"sc	"
#define __INS		"ins	"
#define __EXT		"ext	"
#elif _MIPS_SZLONG == 64
#define __LL		"lld	"
#define __SC		"scd	"
#define __INS		"dins	"
#define __EXT		"dext	"
#endif

/*
 * Using a branch-likely instruction to check the result of an sc instruction
 * works around a bug present in R10000 CPUs prior to revision 3.0 that could
 * cause ll-sc sequences to execute non-atomically.
 */
#if R10000_LLSC_WAR
# define __SC_BEQZ "beqzl	"
#elif MIPS_ISA_REV >= 6
# define __SC_BEQZ "beqzc	"
#else
# define __SC_BEQZ "beqz	"
#endif

#endif /* __ASM_LLSC_H  */
