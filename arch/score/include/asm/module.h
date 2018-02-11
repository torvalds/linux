/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCORE_MODULE_H
#define _ASM_SCORE_MODULE_H

#include <linux/list.h>
#include <asm/extable.h>
#include <asm-generic/module.h>

struct mod_arch_specific {
	/* Data Bus Error exception tables */
	struct list_head dbe_list;
	const struct exception_table_entry *dbe_start;
	const struct exception_table_entry *dbe_end;
};

typedef uint8_t Elf64_Byte;		/* Type for a 8-bit quantity. */

/* Given an address, look for it in the exception tables. */
#ifdef CONFIG_MODULES
const struct exception_table_entry *search_module_dbetables(unsigned long addr);
#else
static inline const struct exception_table_entry
*search_module_dbetables(unsigned long addr)
{
	return NULL;
}
#endif

#define MODULE_PROC_FAMILY "SCORE7"
#define MODULE_KERNEL_TYPE "32BIT "
#define MODULE_KERNEL_SMTC ""

#define MODULE_ARCH_VERMAGIC \
	MODULE_PROC_FAMILY MODULE_KERNEL_TYPE MODULE_KERNEL_SMTC

#endif /* _ASM_SCORE_MODULE_H */
