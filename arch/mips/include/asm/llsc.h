/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Macros for 32/64-bit neutral inline assembler
 */

#ifndef __ASM_LLSC_H
#define __ASM_LLSC_H

#if _MIPS_SZLONG == 32
#define SZLONG_LOG 5
#define SZLONG_MASK 31UL
#define __LL		"ll	"
#define __SC		"sc	"
#define __INS		"ins	"
#define __EXT		"ext	"
#elif _MIPS_SZLONG == 64
#define SZLONG_LOG 6
#define SZLONG_MASK 63UL
#define __LL		"lld	"
#define __SC		"scd	"
#define __INS		"dins	"
#define __EXT		"dext	"
#endif

#endif /* __ASM_LLSC_H  */
