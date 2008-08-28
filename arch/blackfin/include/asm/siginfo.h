#ifndef _BFIN_SIGINFO_H
#define _BFIN_SIGINFO_H

#include <linux/types.h>
#include <asm-generic/siginfo.h>

#define UID16_SIGINFO_COMPAT_NEEDED

#define si_uid16	_sifields._kill._uid

#define ILL_ILLPARAOP	(__SI_FAULT|2)	/* illegal opcode combine ********** */
#define ILL_ILLEXCPT	(__SI_FAULT|4)	/* unrecoverable exception ********** */
#define ILL_CPLB_VI	(__SI_FAULT|9)	/* D/I CPLB protect violation ******** */
#define ILL_CPLB_MISS	(__SI_FAULT|10)	/* D/I CPLB miss ******** */
#define ILL_CPLB_MULHIT	(__SI_FAULT|11)	/* D/I CPLB multiple hit ******** */

/*
 * SIGBUS si_codes
 */
#define BUS_OPFETCH	(__SI_FAULT|4)	/* error from instruction fetch ******** */

/*
 * SIGTRAP si_codes
 */
#define TRAP_STEP	(__SI_FAULT|1)	/* single-step breakpoint************* */
#define TRAP_TRACEFLOW	(__SI_FAULT|2)	/* trace buffer overflow ************* */
#define TRAP_WATCHPT	(__SI_FAULT|3)	/* watchpoint match      ************* */
#define TRAP_ILLTRAP	(__SI_FAULT|4)	/* illegal trap          ************* */

/*
 * SIGSEGV si_codes
 */
#define SEGV_STACKFLOW	(__SI_FAULT|3)	/* stack overflow */

#endif
