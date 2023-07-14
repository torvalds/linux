/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_MSI_H
#define _ASM_S390_MSI_H
#include <asm-generic/msi.h>

/*
 * Work around S390 not using irq_domain at all so we can't set
 * IRQ_DOMAIN_FLAG_ISOLATED_MSI. See for an explanation how it works:
 *
 * https://lore.kernel.org/r/31af8174-35e9-ebeb-b9ef-74c90d4bfd93@linux.ibm.com/
 *
 * Note this is less isolated than the ARM/x86 versions as userspace can trigger
 * MSI belonging to kernel devices within the same gisa.
 */
#define arch_is_isolated_msi() true

#endif
