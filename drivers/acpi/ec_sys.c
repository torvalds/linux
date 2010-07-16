#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/debugfs.h>
#include "internal.h"

MODULE_AUTHOR("Thomas Renninger <trenn@suse.de>");
MODULE_DESCRIPTION("ACPI EC sysfs access driver");
MODULE_LICENSE("GPL");

struct sysdev_class acpi_ec_sysdev_class = {
	.name = "ec",
};

static struct dentry *acpi_ec_debugfs_dir;

int acpi_ec_add_debugfs(struct acpi_ec *ec, unsigned int ec_device_count)
{
	struct dentry *dev_dir;
	char name[64];
	if (ec_device_count == 0) {
		acpi_ec_debugfs_dir = debugfs_create_dir("ec", NULL);
		if (!acpi_ec_debugfs_dir)
			return -ENOMEM;
	}

	sprintf(name, "ec%u", ec_device_count);
	dev_dir = debugfs_create_dir(name, acpi_ec_debugfs_dir);
	if (!dev_dir) {
		if (ec_device_count == 0)
			debugfs_remove_recursive(acpi_ec_debugfs_dir);
		/* TBD: Proper cleanup for multiple ECs */
		return -ENOMEM;
	}

	debugfs_create_x32("gpe", 0444, dev_dir, (u32 *)&first_ec->gpe);
	debugfs_create_bool("use_global_lock", 0444, dev_dir,
			    (u32 *)&first_ec->global_lock);
	return 0;
}

static int __init acpi_ec_sys_init(void)
{
	int err = 0;
	if (first_ec)
		err = acpi_ec_add_debugfs(first_ec, 0);
	else
		err = -ENODEV;
	return err;
}

static void __exit acpi_ec_sys_exit(void)
{
	debugfs_remove_recursive(acpi_ec_debugfs_dir);
}

module_init(acpi_ec_sys_init);
module_exit(acpi_ec_sys_exit);
