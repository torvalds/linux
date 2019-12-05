/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CRASH_H
#define _ASM_X86_CRASH_H

struct kimage;

int crash_load_segments(struct kimage *image);
int crash_setup_memmap_entries(struct kimage *image,
		struct boot_params *params);
void crash_smp_send_stop(void);

#ifdef CONFIG_KEXEC_CORE
void __init crash_reserve_low_1M(void);
#else
static inline void __init crash_reserve_low_1M(void) { }
#endif

#endif /* _ASM_X86_CRASH_H */
