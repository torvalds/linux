// SPDX-License-Identifier: GPL-2.0-only
/*
 * custom_method.c - debugfs interface for customizing ACPI control method
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/acpi.h>
#include <linux/security.h>

#include "internal.h"

MODULE_LICENSE("GPL");

static struct dentry *cm_dentry;

/* /sys/kernel/debug/acpi/custom_method */

static ssize_t cm_write(struct file *file, const char __user * user_buf,
			size_t count, loff_t *ppos)
{
	static char *buf;
	static u32 max_size;
	static u32 uncopied_bytes;

	struct acpi_table_header table;
	acpi_status status;
	int ret;

	ret = security_locked_down(LOCKDOWN_ACPI_TABLES);
	if (ret)
		return ret;

	if (!(*ppos)) {
		/* parse the table header to get the table length */
		if (count <= sizeof(struct acpi_table_header))
			return -EINVAL;
		if (copy_from_user(&table, user_buf,
				   sizeof(struct acpi_table_header)))
			return -EFAULT;
		uncopied_bytes = max_size = table.length;
		buf = kzalloc(max_size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	if (buf == NULL)
		return -EINVAL;

	if ((*ppos > max_size) ||
	    (*ppos + count > max_size) ||
	    (*ppos + count < count) ||
	    (count > uncopied_bytes)) {
		kfree(buf);
		return -EINVAL;
	}

	if (copy_from_user(buf + (*ppos), user_buf, count)) {
		kfree(buf);
		buf = NULL;
		return -EFAULT;
	}

	uncopied_bytes -= count;
	*ppos += count;

	if (!uncopied_bytes) {
		status = acpi_install_method(buf);
		kfree(buf);
		buf = NULL;
		if (ACPI_FAILURE(status))
			return -EINVAL;
		add_taint(TAINT_OVERRIDDEN_ACPI_TABLE, LOCKDEP_NOW_UNRELIABLE);
	}

	kfree(buf);
	return count;
}

static const struct file_operations cm_fops = {
	.write = cm_write,
	.llseek = default_llseek,
};

static int __init acpi_custom_method_init(void)
{
	cm_dentry = debugfs_create_file("custom_method", S_IWUSR,
					acpi_debugfs_dir, NULL, &cm_fops);
	return 0;
}

static void __exit acpi_custom_method_exit(void)
{
	debugfs_remove(cm_dentry);
}

module_init(acpi_custom_method_init);
module_exit(acpi_custom_method_exit);
