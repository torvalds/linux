/* 
 *  hotkey.c - ACPI Hotkey Driver ($Revision:$)
 *
 *  Copyright (C) 2004 Luming Yu <luming.yu@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>

#define HOTKEY_ACPI_VERSION "0.1"

#define HOTKEY_PROC "hotkey"
#define HOTKEY_EV_CONFIG    "event_config"
#define HOTKEY_PL_CONFIG    "poll_config"
#define HOTKEY_ACTION   "action"
#define HOTKEY_INFO "info"

#define ACPI_HOTK_NAME          "Generic Hotkey Driver"
#define ACPI_HOTK_CLASS         "Hotkey"
#define ACPI_HOTK_DEVICE_NAME   "Hotkey"
#define ACPI_HOTK_HID           "Unknown?"
#define ACPI_HOTKEY_COMPONENT   0x20000000

#define ACPI_HOTKEY_EVENT   0x1
#define ACPI_HOTKEY_POLLING 0x2
#define ACPI_UNDEFINED_EVENT    0xf

#define MAX_CONFIG_RECORD_LEN   80
#define MAX_NAME_PATH_LEN   80
#define MAX_CALL_PARM       80

#define IS_EVENT(e)       0xff	/* ((e) & 0x40000000)  */
#define IS_POLL(e)      0xff	/* (~((e) & 0x40000000))  */

#define _COMPONENT              ACPI_HOTKEY_COMPONENT
ACPI_MODULE_NAME("acpi_hotkey")

    MODULE_AUTHOR("luming.yu@intel.com");
MODULE_DESCRIPTION(ACPI_HOTK_NAME);
MODULE_LICENSE("GPL");

/*  standardized internal hotkey number/event  */
enum {
	/* Video Extension event */
	HK_EVENT_CYCLE_OUTPUT_DEVICE = 0x80,
	HK_EVENT_OUTPUT_DEVICE_STATUS_CHANGE,
	HK_EVENT_CYCLE_DISPLAY_OUTPUT,
	HK_EVENT_NEXT_DISPLAY_OUTPUT,
	HK_EVENT_PREVIOUS_DISPLAY_OUTPUT,
	HK_EVENT_CYCLE_BRIGHTNESS,
	HK_EVENT_INCREASE_BRIGHTNESS,
	HK_EVENT_DECREASE_BRIGHTNESS,
	HK_EVENT_ZERO_BRIGHTNESS,
	HK_EVENT_DISPLAY_DEVICE_OFF,

	/* Snd Card event */
	HK_EVENT_VOLUME_MUTE,
	HK_EVENT_VOLUME_INCLREASE,
	HK_EVENT_VOLUME_DECREASE,

	/* running state control */
	HK_EVENT_ENTERRING_S3,
	HK_EVENT_ENTERRING_S4,
	HK_EVENT_ENTERRING_S5,
};

/*  procdir we use */
static struct proc_dir_entry *hotkey_proc_dir;
static struct proc_dir_entry *hotkey_config;
static struct proc_dir_entry *hotkey_poll_config;
static struct proc_dir_entry *hotkey_action;
static struct proc_dir_entry *hotkey_info;

/* linkage for all type of hotkey */
struct acpi_hotkey_link {
	struct list_head entries;
	int hotkey_type;	/* event or polling based hotkey  */
	int hotkey_standard_num;	/* standardized hotkey(event) number */
};

/* event based hotkey */
struct acpi_event_hotkey {
	struct acpi_hotkey_link hotkey_link;
	int flag;
	acpi_handle bus_handle;	/* bus to install notify handler */
	int external_hotkey_num;	/* external hotkey/event number */
	acpi_handle action_handle;	/* acpi handle attached aml action method */
	char *action_method;	/* action method */
};

/* 
 * There are two ways to poll status
 * 1. directy call read_xxx method, without any arguments passed in
 * 2. call write_xxx method, with arguments passed in, you need
 * the result is saved in acpi_polling_hotkey.poll_result.
 * anthoer read command through polling interface.
 *
 */

/* polling based hotkey */
struct acpi_polling_hotkey {
	struct acpi_hotkey_link hotkey_link;
	int flag;
	acpi_handle poll_handle;	/* acpi handle attached polling method */
	char *poll_method;	/* poll method */
	acpi_handle action_handle;	/* acpi handle attached action method */
	char *action_method;	/* action method */
	void *poll_result;	/* polling_result */
	struct proc_dir_entry *proc;
};

/* hotkey object union */
union acpi_hotkey {
	struct list_head entries;
	struct acpi_hotkey_link link;
	struct acpi_event_hotkey event_hotkey;
	struct acpi_polling_hotkey poll_hotkey;
};

/* hotkey object list */
struct acpi_hotkey_list {
	struct list_head *entries;
	int count;
};

static int auto_hotkey_add(struct acpi_device *device);
static int auto_hotkey_remove(struct acpi_device *device, int type);

static struct acpi_driver hotkey_driver = {
	.name = ACPI_HOTK_NAME,
	.class = ACPI_HOTK_CLASS,
	.ids = ACPI_HOTK_HID,
	.ops = {
		.add = auto_hotkey_add,
		.remove = auto_hotkey_remove,
		},
};

static int hotkey_open_config(struct inode *inode, struct file *file);
static ssize_t hotkey_write_config(struct file *file,
				   const char __user * buffer,
				   size_t count, loff_t * data);
static ssize_t hotkey_write_poll_config(struct file *file,
					const char __user * buffer,
					size_t count, loff_t * data);
static int hotkey_info_open_fs(struct inode *inode, struct file *file);
static int hotkey_action_open_fs(struct inode *inode, struct file *file);
static ssize_t hotkey_execute_aml_method(struct file *file,
					 const char __user * buffer,
					 size_t count, loff_t * data);
static int hotkey_config_seq_show(struct seq_file *seq, void *offset);
static int hotkey_polling_open_fs(struct inode *inode, struct file *file);

/* event based config */
static struct file_operations hotkey_config_fops = {
	.open = hotkey_open_config,
	.read = seq_read,
	.write = hotkey_write_config,
	.llseek = seq_lseek,
	.release = single_release,
};

/* polling based config */
static struct file_operations hotkey_poll_config_fops = {
	.open = hotkey_open_config,
	.read = seq_read,
	.write = hotkey_write_poll_config,
	.llseek = seq_lseek,
	.release = single_release,
};

/* hotkey driver info */
static struct file_operations hotkey_info_fops = {
	.open = hotkey_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* action */
static struct file_operations hotkey_action_fops = {
	.open = hotkey_action_open_fs,
	.read = seq_read,
	.write = hotkey_execute_aml_method,
	.llseek = seq_lseek,
	.release = single_release,
};

/* polling results */
static struct file_operations hotkey_polling_fops = {
	.open = hotkey_polling_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

struct acpi_hotkey_list global_hotkey_list;	/* link all ev or pl hotkey  */
struct list_head hotkey_entries;	/* head of the list of hotkey_list */

static int hotkey_info_seq_show(struct seq_file *seq, void *offset)
{
	ACPI_FUNCTION_TRACE("hotkey_info_seq_show");

	seq_printf(seq, "Hotkey generic driver ver: %s", HOTKEY_ACPI_VERSION);

	return_VALUE(0);
}

static int hotkey_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, hotkey_info_seq_show, PDE(inode)->data);
}

static char *format_result(union acpi_object *object)
{
	char *buf = (char *)kmalloc(sizeof(union acpi_object), GFP_KERNEL);

	memset(buf, 0, sizeof(union acpi_object));

	/* Now, just support integer type */
	if (object->type == ACPI_TYPE_INTEGER)
		sprintf(buf, "%d", (u32) object->integer.value);

	return buf;
}

static int hotkey_polling_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_polling_hotkey *poll_hotkey =
	    (struct acpi_polling_hotkey *)seq->private;

	ACPI_FUNCTION_TRACE("hotkey_polling_seq_show");

	if (poll_hotkey->poll_result)
		seq_printf(seq, "%s", format_result(poll_hotkey->poll_result));

	return_VALUE(0);
}

static int hotkey_polling_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, hotkey_polling_seq_show, PDE(inode)->data);
}

static int hotkey_action_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, hotkey_info_seq_show, PDE(inode)->data);
}

/* Mapping external hotkey number to standardized hotkey event num */
static int hotkey_get_internal_event(int event, struct acpi_hotkey_list *list)
{
	struct list_head *entries, *next;
	int val = 0;

	ACPI_FUNCTION_TRACE("hotkey_get_internal_event");

	list_for_each_safe(entries, next, list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT
		    && key->event_hotkey.external_hotkey_num == event)
			val = key->link.hotkey_standard_num;
		else
			val = -1;
	}

	return_VALUE(val);
}

static void
acpi_hotkey_notify_handler(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = NULL;
	u32 internal_event;

	ACPI_FUNCTION_TRACE("acpi_hotkey_notify_handler");

	if (acpi_bus_get_device(handle, &device))
		return_VOID;

	internal_event = hotkey_get_internal_event(event, &global_hotkey_list);
	acpi_bus_generate_event(device, event, 0);

	return_VOID;
}

/* Need to invent automatically hotkey add method */
static int auto_hotkey_add(struct acpi_device *device)
{
	/* Implement me */
	return 0;
}

/* Need to invent automatically hotkey remove method */
static int auto_hotkey_remove(struct acpi_device *device, int type)
{
	/* Implement me */
	return 0;
}

/* Create a proc file for each polling method */
static int create_polling_proc(union acpi_hotkey *device)
{
	struct proc_dir_entry *proc;
	mode_t mode;

	ACPI_FUNCTION_TRACE("create_polling_proc");
	mode = S_IFREG | S_IRUGO | S_IWUGO;

	proc = create_proc_entry(device->poll_hotkey.action_method,
				 mode, hotkey_proc_dir);

	if (!proc) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  device->poll_hotkey.poll_method));
		return_VALUE(-ENODEV);
	} else {
		proc->proc_fops = &hotkey_polling_fops;
		proc->owner = THIS_MODULE;
		proc->data = device;
		proc->uid = 0;
		proc->gid = 0;
		device->poll_hotkey.proc = proc;
	}
	return_VALUE(0);
}

static int is_valid_acpi_path(const char *pathname)
{
	acpi_handle handle;
	acpi_status status;
	ACPI_FUNCTION_TRACE("is_valid_acpi_path");

	status = acpi_get_handle(NULL, (char *)pathname, &handle);
	return_VALUE(!ACPI_FAILURE(status));
}

static int is_valid_hotkey(union acpi_hotkey *device)
{
	ACPI_FUNCTION_TRACE("is_valid_hotkey");
	/* Implement valid check */
	return_VALUE(1);
}

static int hotkey_add(union acpi_hotkey *device)
{
	int status = 0;
	struct acpi_device *dev = NULL;

	ACPI_FUNCTION_TRACE("hotkey_add");

	if (device->link.hotkey_type == ACPI_HOTKEY_EVENT) {
		status =
		    acpi_bus_get_device(device->event_hotkey.bus_handle, &dev);
		if (status)
			return_VALUE(status);

		status = acpi_install_notify_handler(dev->handle,
						     ACPI_SYSTEM_NOTIFY,
						     acpi_hotkey_notify_handler,
						     device);
	} else			/* Add polling hotkey */
		create_polling_proc(device);

	global_hotkey_list.count++;

	list_add_tail(&device->link.entries, global_hotkey_list.entries);

	return_VALUE(status);
}

static int hotkey_remove(union acpi_hotkey *device)
{
	struct list_head *entries, *next;

	ACPI_FUNCTION_TRACE("hotkey_remove");

	list_for_each_safe(entries, next, global_hotkey_list.entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_standard_num ==
		    device->link.hotkey_standard_num) {
			list_del(&key->link.entries);
			remove_proc_entry(key->poll_hotkey.action_method,
					  hotkey_proc_dir);
			global_hotkey_list.count--;
			break;
		}
	}
	return_VALUE(0);
}

static void hotkey_update(union acpi_hotkey *key)
{
	struct list_head *entries, *next;

	ACPI_FUNCTION_TRACE("hotkey_update");

	list_for_each_safe(entries, next, global_hotkey_list.entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_standard_num ==
		    key->link.hotkey_standard_num) {
			key->event_hotkey.bus_handle =
			    key->event_hotkey.bus_handle;
			key->event_hotkey.external_hotkey_num =
			    key->event_hotkey.external_hotkey_num;
			key->event_hotkey.action_handle =
			    key->event_hotkey.action_handle;
			key->event_hotkey.action_method =
			    key->event_hotkey.action_method;
			break;
		}
	}

	return_VOID;
}

static void free_hotkey_device(union acpi_hotkey *key)
{
	struct acpi_device *dev;
	int status;

	ACPI_FUNCTION_TRACE("free_hotkey_device");

	if (key->link.hotkey_type == ACPI_HOTKEY_EVENT) {
		status =
		    acpi_bus_get_device(key->event_hotkey.bus_handle, &dev);
		if (dev->handle)
			acpi_remove_notify_handler(dev->handle,
						   ACPI_SYSTEM_NOTIFY,
						   acpi_hotkey_notify_handler);
	} else
		remove_proc_entry(key->poll_hotkey.action_method,
				  hotkey_proc_dir);
	kfree(key);
	return_VOID;
}

static int
init_hotkey_device(union acpi_hotkey *key, char *bus_str, char *action_str,
		   char *method, int std_num, int external_num)
{
	ACPI_FUNCTION_TRACE("init_hotkey_device");

	key->link.hotkey_type = ACPI_HOTKEY_EVENT;
	key->link.hotkey_standard_num = std_num;
	key->event_hotkey.flag = 0;
	if (is_valid_acpi_path(bus_str))
		acpi_get_handle((acpi_handle) 0,
				bus_str, &(key->event_hotkey.bus_handle));
	else
		return_VALUE(-ENODEV);
	key->event_hotkey.external_hotkey_num = external_num;
	if (is_valid_acpi_path(action_str))
		acpi_get_handle((acpi_handle) 0,
				action_str, &(key->event_hotkey.action_handle));
	key->event_hotkey.action_method = kmalloc(sizeof(method), GFP_KERNEL);
	strcpy(key->event_hotkey.action_method, method);

	return_VALUE(!is_valid_hotkey(key));
}

static int
init_poll_hotkey_device(union acpi_hotkey *key,
			char *poll_str,
			char *poll_method,
			char *action_str, char *action_method, int std_num)
{
	ACPI_FUNCTION_TRACE("init_poll_hotkey_device");

	key->link.hotkey_type = ACPI_HOTKEY_POLLING;
	key->link.hotkey_standard_num = std_num;
	key->poll_hotkey.flag = 0;
	if (is_valid_acpi_path(poll_str))
		acpi_get_handle((acpi_handle) 0,
				poll_str, &(key->poll_hotkey.poll_handle));
	else
		return_VALUE(-ENODEV);
	key->poll_hotkey.poll_method = poll_method;
	if (is_valid_acpi_path(action_str))
		acpi_get_handle((acpi_handle) 0,
				action_str, &(key->poll_hotkey.action_handle));
	key->poll_hotkey.action_method =
	    kmalloc(sizeof(action_method), GFP_KERNEL);
	strcpy(key->poll_hotkey.action_method, action_method);
	key->poll_hotkey.poll_result =
	    (union acpi_object *)kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	return_VALUE(is_valid_hotkey(key));
}

static int check_hotkey_valid(union acpi_hotkey *key,
			      struct acpi_hotkey_list *list)
{
	ACPI_FUNCTION_TRACE("check_hotkey_valid");
	return_VALUE(0);
}

static int hotkey_open_config(struct inode *inode, struct file *file)
{
	ACPI_FUNCTION_TRACE("hotkey_open_config");
	return_VALUE(single_open
		     (file, hotkey_config_seq_show, PDE(inode)->data));
}

static int hotkey_config_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey_list *hotkey_list = &global_hotkey_list;
	struct list_head *entries, *next;
	char bus_name[ACPI_PATHNAME_MAX] = { 0 };
	char action_name[ACPI_PATHNAME_MAX] = { 0 };
	struct acpi_buffer bus = { ACPI_PATHNAME_MAX, bus_name };
	struct acpi_buffer act = { ACPI_PATHNAME_MAX, action_name };

	ACPI_FUNCTION_TRACE(("hotkey_config_seq_show"));

	if (!hotkey_list)
		goto end;

	list_for_each_safe(entries, next, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT) {
			acpi_get_name(key->event_hotkey.bus_handle,
				      ACPI_NAME_TYPE_MAX, &bus);
			acpi_get_name(key->event_hotkey.action_handle,
				      ACPI_NAME_TYPE_MAX, &act);
			seq_printf(seq, "%s:%s:%s:%d:%d", bus_name,
				   action_name,
				   key->event_hotkey.action_method,
				   key->link.hotkey_standard_num,
				   key->event_hotkey.external_hotkey_num);
		} /* ACPI_HOTKEY_POLLING */
		else {
			acpi_get_name(key->poll_hotkey.poll_handle,
				      ACPI_NAME_TYPE_MAX, &bus);
			acpi_get_name(key->poll_hotkey.action_handle,
				      ACPI_NAME_TYPE_MAX, &act);
			seq_printf(seq, "%s:%s:%s:%s:%d", bus_name,
				   key->poll_hotkey.poll_method,
				   action_name,
				   key->poll_hotkey.action_method,
				   key->link.hotkey_standard_num);
		}
	}
	seq_puts(seq, "\n");
      end:
	return_VALUE(0);
}

static int
get_parms(char *config_record,
	  int *cmd,
	  char *bus_handle,
	  char *bus_method,
	  char *action_handle,
	  char *method, int *internal_event_num, int *external_event_num)
{
	char *tmp, *tmp1;
	ACPI_FUNCTION_TRACE(("get_parms"));

	sscanf(config_record, "%d", cmd);

	tmp = strchr(config_record, ':');
	tmp++;
	tmp1 = strchr(tmp, ':');
	strncpy(bus_handle, tmp, tmp1 - tmp);
	bus_handle[tmp1 - tmp] = 0;

	tmp = tmp1;
	tmp++;
	tmp1 = strchr(tmp, ':');
	strncpy(bus_method, tmp, tmp1 - tmp);
	bus_method[tmp1 - tmp] = 0;

	tmp = tmp1;
	tmp++;
	tmp1 = strchr(tmp, ':');
	strncpy(action_handle, tmp, tmp1 - tmp);
	action_handle[tmp1 - tmp] = 0;

	tmp = tmp1;
	tmp++;
	tmp1 = strchr(tmp, ':');
	strncpy(method, tmp, tmp1 - tmp);
	method[tmp1 - tmp] = 0;

	sscanf(tmp1 + 1, "%d:%d", internal_event_num, external_event_num);
	return_VALUE(6);
}

/*  count is length for one input record */
static ssize_t hotkey_write_config(struct file *file,
				   const char __user * buffer,
				   size_t count, loff_t * data)
{
	struct acpi_hotkey_list *hotkey_list = &global_hotkey_list;
	char config_record[MAX_CONFIG_RECORD_LEN];
	char bus_handle[MAX_NAME_PATH_LEN];
	char bus_method[MAX_NAME_PATH_LEN];
	char action_handle[MAX_NAME_PATH_LEN];
	char method[20];
	int cmd, internal_event_num, external_event_num;
	int ret = 0;
	union acpi_hotkey *key = NULL;

	ACPI_FUNCTION_TRACE(("hotkey_write_config"));

	if (!hotkey_list || count > MAX_CONFIG_RECORD_LEN) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid arguments\n"));
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(config_record, buffer, count)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid data \n"));
		return_VALUE(-EINVAL);
	}
	config_record[count] = '\0';

	ret = get_parms(config_record,
			&cmd,
			bus_handle,
			bus_method,
			action_handle,
			method, &internal_event_num, &external_event_num);
	if (ret != 6) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Invalid data format ret=%d\n", ret));
		return_VALUE(-EINVAL);
	}

	key = kmalloc(sizeof(union acpi_hotkey), GFP_KERNEL);
	ret = init_hotkey_device(key, bus_handle, action_handle, method,
				 internal_event_num, external_event_num);

	if (ret || check_hotkey_valid(key, hotkey_list)) {
		kfree(key);
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid hotkey \n"));
		return_VALUE(-EINVAL);
	}
	switch (cmd) {
	case 0:
		hotkey_add(key);
		break;
	case 1:
		hotkey_remove(key);
		free_hotkey_device(key);
		break;
	case 2:
		hotkey_update(key);
		break;
	default:
		break;
	}
	return_VALUE(count);
}

/*  count is length for one input record */
static ssize_t hotkey_write_poll_config(struct file *file,
					const char __user * buffer,
					size_t count, loff_t * data)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_hotkey_list *hotkey_list =
	    (struct acpi_hotkey_list *)m->private;

	char config_record[MAX_CONFIG_RECORD_LEN];
	char polling_handle[MAX_NAME_PATH_LEN];
	char action_handle[MAX_NAME_PATH_LEN];
	char poll_method[20], action_method[20];
	int ret, internal_event_num, cmd, external_event_num;
	union acpi_hotkey *key = NULL;

	ACPI_FUNCTION_TRACE("hotkey_write_poll_config");

	if (!hotkey_list || count > MAX_CONFIG_RECORD_LEN) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid arguments\n"));
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(config_record, buffer, count)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid data \n"));
		return_VALUE(-EINVAL);
	}
	config_record[count] = '\0';

	ret = get_parms(config_record,
			&cmd,
			polling_handle,
			poll_method,
			action_handle,
			action_method,
			&internal_event_num, &external_event_num);

	if (ret != 6) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid data format\n"));
		return_VALUE(-EINVAL);
	}

	key = kmalloc(sizeof(union acpi_hotkey), GFP_KERNEL);
	ret = init_poll_hotkey_device(key, polling_handle, poll_method,
				      action_handle, action_method,
				      internal_event_num);
	if (ret || check_hotkey_valid(key, hotkey_list)) {
		kfree(key);
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid hotkey \n"));
		return_VALUE(-EINVAL);
	}
	switch (cmd) {
	case 0:
		hotkey_add(key);
		break;
	case 1:
		hotkey_remove(key);
		break;
	case 2:
		hotkey_update(key);
		break;
	default:
		break;
	}
	return_VALUE(count);
}

/*  
 * This function evaluates an ACPI method, given an int as parameter, the
 * method is searched within the scope of the handle, can be NULL. The output
 * of the method is written is output, which can also be NULL
 *
 * returns 1 if write is successful, 0 else.
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val,
			  struct acpi_buffer *output)
{
	struct acpi_object_list params;	/* list of input parameters (an int here) */
	union acpi_object in_obj;	/* the only param we use */
	acpi_status status;

	ACPI_FUNCTION_TRACE("write_acpi_int");
	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);

	return_VALUE(status == AE_OK);
}

static int read_acpi_int(acpi_handle handle, const char *method, int *val)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	ACPI_FUNCTION_TRACE("read_acpi_int");
	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, (char *)method, NULL, &output);
	*val = out_obj.integer.value;
	return_VALUE((status == AE_OK)
		     && (out_obj.type == ACPI_TYPE_INTEGER));
}

static acpi_handle
get_handle_from_hotkeylist(struct acpi_hotkey_list *hotkey_list, int event_num)
{
	struct list_head *entries, *next;

	list_for_each_safe(entries, next, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT
		    && key->link.hotkey_standard_num == event_num) {
			return (key->event_hotkey.action_handle);
		}
	}
	return (NULL);
}

static
char *get_method_from_hotkeylist(struct acpi_hotkey_list *hotkey_list,
				 int event_num)
{
	struct list_head *entries, *next;

	list_for_each_safe(entries, next, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);

		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT &&
		    key->link.hotkey_standard_num == event_num)
			return (key->event_hotkey.action_method);
	}
	return (NULL);
}

static struct acpi_polling_hotkey *get_hotkey_by_event(struct
						       acpi_hotkey_list
						       *hotkey_list, int event)
{
	struct list_head *entries, *next;

	list_for_each_safe(entries, next, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_POLLING
		    && key->link.hotkey_standard_num == event) {
			return (&key->poll_hotkey);
		}
	}
	return (NULL);
}

/*  
 * user call AML method interface:
 * Call convention:
 * echo "event_num: arg type : value"
 * example: echo "1:1:30" > /proc/acpi/action
 * Just support 1 integer arg passing to AML method
 */

static ssize_t hotkey_execute_aml_method(struct file *file,
					 const char __user * buffer,
					 size_t count, loff_t * data)
{
	struct acpi_hotkey_list *hotkey_list = &global_hotkey_list;
	char arg[MAX_CALL_PARM];
	int event, type, value;

	char *method;
	acpi_handle handle;

	ACPI_FUNCTION_TRACE("hotkey_execte_aml_method");

	if (!hotkey_list || count > MAX_CALL_PARM) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid argument 1"));
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(arg, buffer, count)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid argument 2"));
		return_VALUE(-EINVAL);
	}

	arg[count] = '\0';

	if (sscanf(arg, "%d:%d:%d", &event, &type, &value) != 3) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid argument 3"));
		return_VALUE(-EINVAL);
	}

	if (type == ACPI_TYPE_INTEGER) {
		handle = get_handle_from_hotkeylist(hotkey_list, event);
		method = (char *)get_method_from_hotkeylist(hotkey_list, event);
		if (IS_EVENT(event))
			write_acpi_int(handle, method, value, NULL);
		else if (IS_POLL(event)) {
			struct acpi_polling_hotkey *key;
			key = (struct acpi_polling_hotkey *)
			    get_hotkey_by_event(hotkey_list, event);
			read_acpi_int(handle, method, key->poll_result);
		}
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Not supported"));
		return_VALUE(-EINVAL);
	}

	return_VALUE(count);
}

static int __init hotkey_init(void)
{
	int result;
	mode_t mode = S_IFREG | S_IRUGO | S_IWUGO;

	ACPI_FUNCTION_TRACE("hotkey_init");

	if (acpi_disabled)
		return -ENODEV;

	if (acpi_specific_hotkey_enabled) {
		printk("Using specific hotkey driver\n");
		return -ENODEV;
	}

	hotkey_proc_dir = proc_mkdir(HOTKEY_PROC, acpi_root_dir);
	if (!hotkey_proc_dir) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  HOTKEY_PROC));
		return (-ENODEV);
	}
	hotkey_proc_dir->owner = THIS_MODULE;

	hotkey_config =
	    create_proc_entry(HOTKEY_EV_CONFIG, mode, hotkey_proc_dir);
	if (!hotkey_config) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  HOTKEY_EV_CONFIG));
		return (-ENODEV);
	} else {
		hotkey_config->proc_fops = &hotkey_config_fops;
		hotkey_config->data = &global_hotkey_list;
		hotkey_config->owner = THIS_MODULE;
		hotkey_config->uid = 0;
		hotkey_config->gid = 0;
	}

	hotkey_poll_config =
	    create_proc_entry(HOTKEY_PL_CONFIG, mode, hotkey_proc_dir);
	if (!hotkey_poll_config) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  HOTKEY_EV_CONFIG));
		return (-ENODEV);
	} else {
		hotkey_poll_config->proc_fops = &hotkey_poll_config_fops;
		hotkey_poll_config->data = &global_hotkey_list;
		hotkey_poll_config->owner = THIS_MODULE;
		hotkey_poll_config->uid = 0;
		hotkey_poll_config->gid = 0;
	}

	hotkey_action = create_proc_entry(HOTKEY_ACTION, mode, hotkey_proc_dir);
	if (!hotkey_action) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  HOTKEY_ACTION));
		return (-ENODEV);
	} else {
		hotkey_action->proc_fops = &hotkey_action_fops;
		hotkey_action->owner = THIS_MODULE;
		hotkey_action->uid = 0;
		hotkey_action->gid = 0;
	}

	hotkey_info = create_proc_entry(HOTKEY_INFO, mode, hotkey_proc_dir);
	if (!hotkey_info) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Hotkey: Unable to create %s entry\n",
				  HOTKEY_INFO));
		return (-ENODEV);
	} else {
		hotkey_info->proc_fops = &hotkey_info_fops;
		hotkey_info->owner = THIS_MODULE;
		hotkey_info->uid = 0;
		hotkey_info->gid = 0;
	}

	result = acpi_bus_register_driver(&hotkey_driver);
	if (result < 0) {
		remove_proc_entry(HOTKEY_PROC, acpi_root_dir);
		return (-ENODEV);
	}
	global_hotkey_list.count = 0;
	global_hotkey_list.entries = &hotkey_entries;

	INIT_LIST_HEAD(&hotkey_entries);

	return (0);
}

static void __exit hotkey_exit(void)
{
	struct list_head *entries, *next;

	ACPI_FUNCTION_TRACE("hotkey_remove");

	list_for_each_safe(entries, next, global_hotkey_list.entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);

		acpi_os_wait_events_complete(NULL);
		list_del(&key->link.entries);
		global_hotkey_list.count--;
		free_hotkey_device(key);
	}
	acpi_bus_unregister_driver(&hotkey_driver);
	remove_proc_entry(HOTKEY_EV_CONFIG, hotkey_proc_dir);
	remove_proc_entry(HOTKEY_PL_CONFIG, hotkey_proc_dir);
	remove_proc_entry(HOTKEY_ACTION, hotkey_proc_dir);
	remove_proc_entry(HOTKEY_INFO, hotkey_proc_dir);
	remove_proc_entry(HOTKEY_PROC, acpi_root_dir);
	return;
}

module_init(hotkey_init);
module_exit(hotkey_exit);
