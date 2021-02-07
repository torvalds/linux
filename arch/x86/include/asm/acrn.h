/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ACRN_H
#define _ASM_X86_ACRN_H

/*
 * This CPUID returns feature bitmaps in EAX.
 * Guest VM uses this to detect the appropriate feature bit.
 */
#define	ACRN_CPUID_FEATURES		0x40000001
/* Bit 0 indicates whether guest VM is privileged */
#define	ACRN_FEATURE_PRIVILEGED_VM	BIT(0)

void acrn_setup_intr_handler(void (*handler)(void));
void acrn_remove_intr_handler(void);

static inline u32 acrn_cpuid_base(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return hypervisor_cpuid_base("ACRNACRNACRN", 0);

	return 0;
}

#endif /* _ASM_X86_ACRN_H */
