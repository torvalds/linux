/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_SIGNAL_H
#define __SPARC_SIGNAL_H

#ifndef __ASSEMBLER__
#include <linux/personality.h>
#include <linux/types.h>
#endif
#include <uapi/asm/signal.h>

#ifndef __ASSEMBLER__

#define __ARCH_HAS_KA_RESTORER
#define __ARCH_HAS_SA_RESTORER

#endif /* !(__ASSEMBLER__) */
#endif /* !(__SPARC_SIGNAL_H) */
