// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/ptdump.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	struct ptdump_info *info = m->private;
	ptdump_walk_pgd(m, info);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptdump);

int ptdump_debugfs_register(struct ptdump_info *info, const char *name)
{
	struct dentry *pe;
	pe = debugfs_create_file(name, 0400, NULL, info, &ptdump_fops);
	return pe ? 0 : -ENOMEM;

}
