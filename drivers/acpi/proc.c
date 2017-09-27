#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/bcd.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>

#include "sleep.h"
#include "internal.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT

/*
 * this file provides support for:
 * /proc/acpi/wakeup
 */

ACPI_MODULE_NAME("sleep")

static int
acpi_system_wakeup_device_seq_show(struct seq_file *seq, void *offset)
{
	struct list_head *node, *next;

	seq_printf(seq, "Device\tS-state\t  Status   Sysfs node\n");

	mutex_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
		struct acpi_device_physical_node *entry;

		if (!dev->wakeup.flags.valid)
			continue;

		seq_printf(seq, "%s\t  S%d\t",
			   dev->pnp.bus_id,
			   (u32) dev->wakeup.sleep_state);

		mutex_lock(&dev->physical_node_lock);

		if (!dev->physical_node_count) {
			seq_printf(seq, "%c%-8s\n",
				dev->wakeup.flags.valid ? '*' : ' ',
				device_may_wakeup(&dev->dev) ?
					"enabled" : "disabled");
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
					(device_may_wakeup(&dev->dev) ||
					device_may_wakeup(ldev)) ?
					"enabled" : "disabled",
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
	struct list_head *node, *next;
	char strbuf[5];
	char str[5] = "";

	if (count > 4)
		count = 4;

	if (copy_from_user(strbuf, buffer, count))
		return -EFAULT;
	strbuf[count] = '\0';
	sscanf(strbuf, "%s", str);

	mutex_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, wakeup_list);
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
			   PDE_DATA(inode));
}

static const struct file_operations acpi_system_wakeup_device_fops = {
	.owner = THIS_MODULE,
	.open = acpi_system_wakeup_device_open_fs,
	.read = seq_read,
	.write = acpi_system_write_wakeup_device,
	.llseek = seq_lseek,
	.release = single_release,
};

void __init acpi_sleep_proc_init(void)
{
	/* 'wakeup device' [R/W] */
	proc_create("wakeup", S_IFREG | S_IRUGO | S_IWUSR,
		    acpi_root_dir, &acpi_system_wakeup_device_fops);
}
