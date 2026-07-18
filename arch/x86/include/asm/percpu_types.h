/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PERCPU_TYPES_H
#define _ASM_X86_PERCPU_TYPES_H

#if defined(CONFIG_SMP) && defined(CONFIG_CC_HAS_NAMED_AS)
#define __percpu_seg_override	CONCATENATE(__seg_, __percpu_seg)
#else /* !CONFIG_CC_HAS_NAMED_AS: */
#define __percpu_seg_override
#endif

#if defined(CONFIG_USE_X86_SEG_SUPPORT) && defined(USE_TYPEOF_UNQUAL)
#define __percpu_qual		__percpu_seg_override
#endif

#include <asm-generic/percpu_types.h>

#endif
