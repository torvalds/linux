/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_MASKS_H
#define _ASM_POWERPC_PGTABLE_MASKS_H

#ifndef _PAGE_NA
#define _PAGE_NA	0
#define _PAGE_NAX	_PAGE_EXEC
#define _PAGE_RO	_PAGE_READ
#define _PAGE_ROX	(_PAGE_READ | _PAGE_EXEC)
#define _PAGE_RW	(_PAGE_READ | _PAGE_WRITE)
#define _PAGE_RWX	(_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)
#endif

/* Permission flags for kernel mappings */
#ifndef _PAGE_KERNEL_RO
#define _PAGE_KERNEL_RO		_PAGE_RO
#define _PAGE_KERNEL_ROX	_PAGE_ROX
#define _PAGE_KERNEL_RW		(_PAGE_RW | _PAGE_DIRTY)
#define _PAGE_KERNEL_RWX	(_PAGE_RWX | _PAGE_DIRTY)
#endif

/* Permission masks used to generate the __P and __S table */
#define PAGE_NONE	__pgprot(_PAGE_BASE | _PAGE_NA)
#define PAGE_EXECONLY_X	__pgprot(_PAGE_BASE | _PAGE_NAX)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_RWX)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_RO)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_ROX)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_RO)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_ROX)

#endif /* _ASM_POWERPC_PGTABLE_MASKS_H */
