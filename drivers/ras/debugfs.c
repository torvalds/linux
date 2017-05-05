#include <linux/debugfs.h>

struct dentry *ras_debugfs_dir;

static atomic_t trace_count = ATOMIC_INIT(0);

int ras_userspace_consumers(void)
{
	return atomic_read(&trace_count);
}
EXPORT_SYMBOL_GPL(ras_userspace_consumers);

static int trace_show(struct seq_file *m, void *v)
{
	return atomic_read(&trace_count);
}

static int trace_open(struct inode *inode, struct file *file)
{
	atomic_inc(&trace_count);
	return single_open(file, trace_show, NULL);
}

static int trace_release(struct inode *inode, struct file *file)
{
	atomic_dec(&trace_count);
	return single_release(inode, file);
}

static const struct file_operations trace_fops = {
	.open    = trace_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = trace_release,
};

int __init ras_add_daemon_trace(void)
{
	struct dentry *fentry;

	if (!ras_debugfs_dir)
		return -ENOENT;

	fentry = debugfs_create_file("daemon_active", S_IRUSR, ras_debugfs_dir,
				     NULL, &trace_fops);
	if (!fentry)
		return -ENODEV;

	return 0;

}

void __init ras_debugfs_init(void)
{
	ras_debugfs_dir = debugfs_create_dir("ras", NULL);
}
