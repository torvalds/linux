/*
 * Based on arch/arm/include/asm/page.h
 *
 * Copyright (C) 1995-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PAGE_H
#define __ASM_PAGE_H

/* PAGE_SHIFT determines the page size */
#ifdef CONFIG_ARM64_64K_PAGES
#define PAGE_SHIFT		16
#else
#define PAGE_SHIFT		12
#endif
#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

/* We do define AT_SYSINFO_EHDR but don't use the gate mechanism */
#define __HAVE_ARCH_GATE_AREA		1

/*
 * The idmap and swapper page tables need some space reserved in the kernel
 * image. Both require pgd, pud (4 levels only) and pmd tables to (section)
 * map the kernel. The swapper also maps the FDT (see __create_page_tables for
 * more information).
 */
#if CONFIG_ARM64_PGTABLE_LEVELS == 4
#define SWAPPER_DIR_SIZE	(3 * PAGE_SIZE)
#define IDMAP_DIR_SIZE		(3 * PAGE_SIZE)
#else
#define SWAPPER_DIR_SIZE	(2 * PAGE_SIZE)
#define IDMAP_DIR_SIZE		(2 * PAGE_SIZE)
#endif

#ifndef __ASSEMBLY__

#if CONFIG_ARM64_PGTABLE_LEVELS == 2
#include <asm/pgtable-2level-types.h>
#elif CONFIG_ARM64_PGTABLE_LEVELS == 3
#include <asm/pgtable-3level-types.h>
#else
#include <asm/pgtable-4level-types.h>
#endif

extern void __cpu_clear_user_page(void *p, unsigned long user);
extern void __cpu_copy_user_page(void *to, const void *from,
				 unsigned long user);
extern void copy_page(void *to, const void *from);
extern void clear_page(void *to);

#define clear_user_page(addr,vaddr,pg)  __cpu_clear_user_page(addr, vaddr)
#define copy_user_page(to,from,vaddr,pg) __cpu_copy_user_page(to, from, vaddr)

typedef struct page *pgtable_t;

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
extern int pfn_valid(unsigned long);
#endif

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/getorder.h>

#endif
