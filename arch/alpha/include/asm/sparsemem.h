/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_SPARSEMEM_H
#define _ASM_ALPHA_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM

#define SECTION_SIZE_BITS	27

/*
 * According to "Alpha Architecture Reference Manual" physical
 * addresses are at most 48 bits.
 * https://download.majix.org/dec/alpha_arch_ref.pdf
 */
#define MAX_PHYSMEM_BITS	48

#endif /* CONFIG_SPARSEMEM */

#endif /* _ASM_ALPHA_SPARSEMEM_H */
