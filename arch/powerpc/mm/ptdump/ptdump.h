/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/seq_file.h>

struct flag_info {
	u64		mask;
	u64		val;
	const char	*set;
	const char	*clear;
	bool		is_val;
	int		shift;
};

struct ptdump_pg_level {
	const struct flag_info *flag;
	size_t num;
	u64 mask;
};

extern struct ptdump_pg_level pg_level[5];

void pt_dump_size(struct seq_file *m, unsigned long delta);
