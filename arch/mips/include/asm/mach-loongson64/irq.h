/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_LOONGSON64_IRQ_H_
#define __ASM_MACH_LOONGSON64_IRQ_H_

/* cpu core interrupt numbers */
#define NR_IRQS_LEGACY		16
#define NR_MIPS_CPU_IRQS	8
#define NR_IRQS			(NR_IRQS_LEGACY + NR_MIPS_CPU_IRQS + 256)

#define MIPS_CPU_IRQ_BASE 	NR_IRQS_LEGACY

#include <asm/mach-generic/irq.h>

#endif /* __ASM_MACH_LOONGSON64_IRQ_H_ */
