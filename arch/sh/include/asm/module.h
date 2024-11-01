/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SH_MODULE_H
#define _ASM_SH_MODULE_H

#include <asm-generic/module.h>

#ifdef CONFIG_DWARF_UNWINDER
struct mod_arch_specific {
	struct list_head fde_list;
	struct list_head cie_list;
};
#endif

#endif /* _ASM_SH_MODULE_H */
