/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _UAPI_BFIN_SIGINFO_H
#define _UAPI_BFIN_SIGINFO_H

#include <linux/types.h>
#include <asm-generic/siginfo.h>

#define UID16_SIGINFO_COMPAT_NEEDED

#define si_uid16	_sifields._kill._uid

#define ILL_ILLPARAOP	2	/* illegal opcode combine ********** */
#define ILL_ILLEXCPT	4	/* unrecoverable exception ********** */
#define ILL_CPLB_VI	9	/* D/I CPLB protect violation ******** */
#define ILL_CPLB_MISS	10	/* D/I CPLB miss ******** */
#define ILL_CPLB_MULHIT	11	/* D/I CPLB multiple hit ******** */
#undef NSIGILL
#define NSIGILL         11

/*
 * SIGBUS si_codes
 */
#define BUS_OPFETCH	4	/* error from instruction fetch ******** */
#undef NSIGBUS
#define NSIGBUS		4

/*
 * SIGTRAP si_codes
 */
#define TRAP_STEP	1	/* single-step breakpoint************* */
#define TRAP_TRACEFLOW	2	/* trace buffer overflow ************* */
#define TRAP_WATCHPT	3	/* watchpoint match      ************* */
#define TRAP_ILLTRAP	4	/* illegal trap          ************* */
#undef NSIGTRAP
#define NSIGTRAP	4

/*
 * SIGSEGV si_codes
 */
#define SEGV_STACKFLOW	3	/* stack overflow */
#undef NSIGSEGV
#define NSIGSEGV	3

#endif /* _UAPI_BFIN_SIGINFO_H */
