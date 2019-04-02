/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2014 ARM Ltd. */
#ifndef __ASM_PTDUMP_H
#define __ASM_PTDUMP_H

#ifdef CONFIG_ARM_PTDUMP_CORE

#include <linux/mm_types.h>
#include <linux/seq_file.h>

struct addr_marker {
	unsigned long start_address;
	char *name;
};

struct ptdump_info {
	struct mm_struct		*mm;
	const struct addr_marker	*markers;
	unsigned long			base_addr;
};

void ptdump_walk_pgd(struct seq_file *s, struct ptdump_info *info);
#ifdef CONFIG_ARM_PTDUMP_DEFS
int ptdump_defs_register(struct ptdump_info *info, const char *name);
#else
static inline int ptdump_defs_register(struct ptdump_info *info,
					const char *name)
{
	return 0;
}
#endif /* CONFIG_ARM_PTDUMP_DEFS */

void ptdump_check_wx(void);

#endif /* CONFIG_ARM_PTDUMP_CORE */

#ifdef CONFIG_DE_WX
#define de_checkwx() ptdump_check_wx()
#else
#define de_checkwx() do { } while (0)
#endif

#endif /* __ASM_PTDUMP_H */
