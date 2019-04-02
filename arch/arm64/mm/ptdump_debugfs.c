// SPDX-License-Identifier: GPL-2.0
#include <linux/defs.h>
#include <linux/seq_file.h>

#include <asm/ptdump.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	struct ptdump_info *info = m->private;
	ptdump_walk_pgd(m, info);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptdump);

void ptdump_defs_register(struct ptdump_info *info, const char *name)
{
	defs_create_file(name, 0400, NULL, info, &ptdump_fops);
}
