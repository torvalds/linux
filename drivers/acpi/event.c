/*
 * event.c - exporting ACPI events via procfs
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 */

#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("event")

/* Global vars for handling event proc entry */
static DEFINE_SPINLOCK(acpi_system_event_lock);
int event_is_open = 0;
extern struct list_head acpi_bus_event_list;
extern wait_queue_head_t acpi_bus_event_queue;

static int acpi_system_open_event(struct inode *inode, struct file *file)
{
	spin_lock_irq(&acpi_system_event_lock);

	if (event_is_open)
		goto out_busy;

	event_is_open = 1;

	spin_unlock_irq(&acpi_system_event_lock);
	return 0;

      out_busy:
	spin_unlock_irq(&acpi_system_event_lock);
	return -EBUSY;
}

static ssize_t
acpi_system_read_event(struct file *file, char __user * buffer, size_t count,
		       loff_t * ppos)
{
	int result = 0;
	struct acpi_bus_event event;
	static char str[ACPI_MAX_STRING];
	static int chars_remaining = 0;
	static char *ptr;

	ACPI_FUNCTION_TRACE("acpi_system_read_event");

	if (!chars_remaining) {
		memset(&event, 0, sizeof(struct acpi_bus_event));

		if ((file->f_flags & O_NONBLOCK)
		    && (list_empty(&acpi_bus_event_list)))
			return_VALUE(-EAGAIN);

		result = acpi_bus_receive_event(&event);
		if (result)
			return_VALUE(result);

		chars_remaining = sprintf(str, "%s %s %08x %08x\n",
					  event.device_class ? event.
					  device_class : "<unknown>",
					  event.bus_id ? event.
					  bus_id : "<unknown>", event.type,
					  event.data);
		ptr = str;
	}

	if (chars_remaining < count) {
		count = chars_remaining;
	}

	if (copy_to_user(buffer, ptr, count))
		return_VALUE(-EFAULT);

	*ppos += count;
	chars_remaining -= count;
	ptr += count;

	return_VALUE(count);
}

static int acpi_system_close_event(struct inode *inode, struct file *file)
{
	spin_lock_irq(&acpi_system_event_lock);
	event_is_open = 0;
	spin_unlock_irq(&acpi_system_event_lock);
	return 0;
}

static unsigned int acpi_system_poll_event(struct file *file, poll_table * wait)
{
	poll_wait(file, &acpi_bus_event_queue, wait);
	if (!list_empty(&acpi_bus_event_list))
		return POLLIN | POLLRDNORM;
	return 0;
}

static struct file_operations acpi_system_event_ops = {
	.open = acpi_system_open_event,
	.read = acpi_system_read_event,
	.release = acpi_system_close_event,
	.poll = acpi_system_poll_event,
};

static int __init acpi_event_init(void)
{
	struct proc_dir_entry *entry;
	int error = 0;

	ACPI_FUNCTION_TRACE("acpi_event_init");

	if (acpi_disabled)
		return_VALUE(0);

	/* 'event' [R] */
	entry = create_proc_entry("event", S_IRUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_event_ops;
	else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' proc fs entry\n",
				  "event"));
		error = -EFAULT;
	}
	return_VALUE(error);
}

subsys_initcall(acpi_event_init);
