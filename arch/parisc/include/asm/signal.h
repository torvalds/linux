/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_SIGNAL_H
#define _ASM_PARISC_SIGNAL_H

#include <uapi/asm/signal.h>

# ifndef __ASSEMBLER__

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

#include <asm/sigcontext.h>

#endif /* !__ASSEMBLER__ */
#endif /* _ASM_PARISC_SIGNAL_H */
