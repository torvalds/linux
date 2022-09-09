/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_CPU_LOONGSON64
/*
 * Loongson-3's SFB (Store-Fill-Buffer) may buffer writes indefinitely when a
 * tight read loop is executed, because reads take priority over writes & the
 * hardware (incorrectly) doesn't ensure that writes will eventually occur.
 *
 * Since spin loops of any kind should have a cpu_relax() in them, force an SFB
 * flush from cpu_relax() such that any pending writes will become visible as
 * expected.
 */
#define cpu_relax()	smp_mb()
#else
#define cpu_relax()	barrier()
#endif

#endif /* __ASSEMBLY__ */

#endif /* __ASM_VDSO_PROCESSOR_H */
