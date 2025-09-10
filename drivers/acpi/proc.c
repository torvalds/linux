// SPDX-License-Identifier: GPL-2.0
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string_choices.h>
#include <linux/suspend.h>
#include <linux/bcd.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>

#include "sleep.h"
#include "internal.h"

/*
 * this file provides support for:
 * /proc/acpi/wakeup
 */

static int
acpi_system_wakeup_device_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_device *dev, *tmp;

	seq_printf(seq, "Device\tS-state\t  Status   Sysfs node\n");

	mutex_lock(&acpi_device_lock);
	list_for_each_entry_safe(dev, tmp, &acpi_wakeup_device_list,
				 wakeup_list) {
		struct acpi_device_physical_node *entry;

		if (!dev->wakeup.flags.valid)
			continue;

		seq_printf(seq, "%s\t  S%llu\t",
			   dev->pnp.bus_id,
			   dev->wakeup.sleep_state);

		mutex_lock(&dev->physical_node_lock);

		if (!dev->physical_node_count) {
			seq_printf(seq, "%c%-8s\n",
				dev->wakeup.flags.valid ? '*' : ' ',
				str_enabled_disabled(device_may_wakeup(&dev->dev)));
		} else {
			struct device *ldev;
			list_for_each_entry(entry, &dev->physical_node_list,
					node) {
				ldev = get_device(entry->dev);
				if (!ldev)
					continue;

				if (&entry->node !=
						dev->physical_node_list.next)
					seq_printf(seq, "\t\t");

				seq_printf(seq, "%c%-8s  %s:%s\n",
					dev->wakeup.flags.valid ? '*' : ' ',
					str_enabled_disabled(device_may_wakeup(ldev) ||
							     device_may_wakeup(&dev->dev)),
					ldev->bus ? ldev->bus->name :
					"no-bus", dev_name(ldev));
				put_device(ldev);
			}
		}

		mutex_unlock(&dev->physical_node_lock);
	}
	mutex_unlock(&acpi_device_lock);
	return 0;
}

static void physical_device_enable_wakeup(struct acpi_device *adev)
{
	struct acpi_device_physical_node *entry;

	mutex_lock(&adev->physical_node_lock);

	list_for_each_entry(entry,
		&adev->physical_node_list, node)
		if (entry->dev && device_can_wakeup(entry->dev)) {
			bool enable = !device_may_wakeup(entry->dev);
			device_set_wakeup_enable(entry->dev, enable);
		}

	mutex_unlock(&adev->physical_node_lock);
}

static ssize_t
acpi_system_write_wakeup_device(struct file *file,
				const char __user * buffer,
				size_t count, loff_t * ppos)
{
	struct acpi_device *dev, *tmp;
	char strbuf[5];
	char str[5] = "";

	if (count > 4)
		count = 4;

	if (copy_from_user(strbuf, buffer, count))
		return -EFAULT;
	strbuf[count] = '\0';
	sscanf(strbuf, "%s", str);

	mutex_lock(&acpi_device_lock);
	list_for_each_entry_safe(dev, tmp, &acpi_wakeup_device_list,
				 wakeup_list) {
		if (!dev->wakeup.flags.valid)
			continue;

		if (!strncmp(dev->pnp.bus_id, str, 4)) {
			if (device_can_wakeup(&dev->dev)) {
				bool enable = !device_may_wakeup(&dev->dev);
				device_set_wakeup_enable(&dev->dev, enable);
			} else {
				physical_device_enable_wakeup(dev);
			}
			break;
		}
	}
	mutex_unlock(&acpi_device_lock);
	return count;
}

static int
acpi_system_wakeup_device_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_wakeup_device_seq_show,
			   pde_data(inode));
}

static const struct proc_ops acpi_system_wakeup_device_proc_ops = {
	.proc_open	= acpi_system_wakeup_device_open_fs,
	.proc_read	= seq_read,
	.proc_write	= acpi_system_write_wakeup_device,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

void __init acpi_sleep_proc_init(void)
{
	/* 'wakeup device' [R/W] */
	proc_create("wakeup", 0644, acpi_root_dir, &acpi_system_wakeup_device_proc_ops);
}
