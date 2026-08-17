/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#include <asm/barrier.h>

#define cpu_relax()	bcr_serialize()

#endif /* __ASM_VDSO_PROCESSOR_H */
