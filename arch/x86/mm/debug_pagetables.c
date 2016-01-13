#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/pgtable.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	ptdump_walk_pgd_level(m, NULL);
	return 0;
}

static int ptdump_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ptdump_show, NULL);
}

static const struct file_operations ptdump_fops = {
	.owner		= THIS_MODULE,
	.open		= ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *pe;

static int __init pt_dump_debug_init(void)
{
	pe = debugfs_create_file("kernel_page_tables", S_IRUSR, NULL, NULL,
				 &ptdump_fops);
	if (!pe)
		return -ENOMEM;

	return 0;
}

static void __exit pt_dump_debug_exit(void)
{
	debugfs_remove_recursive(pe);
}

module_init(pt_dump_debug_init);
module_exit(pt_dump_debug_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
MODULE_DESCRIPTION("Kernel debugging helper that dumps pagetables");
