/*
 * debugfs.c - ACPI debugfs interface to userspace.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("debugfs");


/* /sys/modules/acpi/parameters/aml_debug_output */

module_param_named(aml_debug_output, acpi_gbl_enable_aml_debug_object,
		   bool, 0644);
MODULE_PARM_DESC(aml_debug_output,
		 "To enable/disable the ACPI Debug Object output.");

/* /sys/kernel/debug/acpi/custom_method */

static ssize_t cm_write(struct file *file, const char __user * user_buf,
			size_t count, loff_t *ppos)
{
	static char *buf;
	static u32 max_size;
	static u32 uncopied_bytes;

	struct acpi_table_header table;
	acpi_status status;

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
	    (count > uncopied_bytes))
		return -EINVAL;

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
		add_taint(TAINT_OVERRIDDEN_ACPI_TABLE);
	}

	return count;
}

static const struct file_operations cm_fops = {
	.write = cm_write,
	.llseek = default_llseek,
};

int __init acpi_debugfs_init(void)
{
	struct dentry *acpi_dir, *cm_dentry;

	acpi_dir = debugfs_create_dir("acpi", NULL);
	if (!acpi_dir)
		goto err;

	cm_dentry = debugfs_create_file("custom_method", S_IWUSR,
					acpi_dir, NULL, &cm_fops);
	if (!cm_dentry)
		goto err;

	return 0;

err:
	if (acpi_dir)
		debugfs_remove(acpi_dir);
	return -EINVAL;
}
