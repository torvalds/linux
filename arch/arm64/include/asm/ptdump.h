/*
 * Copyright (C) 2014 ARM Ltd.
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
#ifndef __ASM_PTDUMP_H
#define __ASM_PTDUMP_H

#ifdef CONFIG_ARM64_PTDUMP_CORE

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
	unsigned long			max_addr;
};

void ptdump_walk_pgd(struct seq_file *s, struct ptdump_info *info);
#ifdef CONFIG_ARM64_PTDUMP_DEBUGFS
int ptdump_debugfs_register(struct ptdump_info *info, const char *name);
#else
static inline int ptdump_debugfs_register(struct ptdump_info *info,
					const char *name)
{
	return 0;
}
#endif
#endif /* CONFIG_ARM64_PTDUMP_CORE */
#endif /* __ASM_PTDUMP_H */
