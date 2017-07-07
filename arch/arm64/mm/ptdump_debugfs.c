#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/ptdump.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	struct ptdump_info *info = m->private;
	ptdump_walk_pgd(m, info);
	return 0;
}

static int ptdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ptdump_show, inode->i_private);
}

static const struct file_operations ptdump_fops = {
	.open		= ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int ptdump_debugfs_register(struct ptdump_info *info, const char *name)
{
	struct dentry *pe;
	pe = debugfs_create_file(name, 0400, NULL, info, &ptdump_fops);
	return pe ? 0 : -ENOMEM;

}
