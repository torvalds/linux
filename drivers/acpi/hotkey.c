/*
 *  hotkey.c - ACPI Hotkey Driver ($Revision: 0.2 $)
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

#define RESULT_STR_LEN	    80

#define ACTION_METHOD	0
#define POLL_METHOD	1

#define IS_EVENT(e)       	((e) <= 10000 && (e) >0)
#define IS_POLL(e)      	((e) > 10000)
#define IS_OTHERS(e)		((e)<=0 || (e)>=20000)
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

enum conf_entry_enum {
	bus_handle = 0,
	bus_method = 1,
	action_handle = 2,
	method = 3,
	LAST_CONF_ENTRY
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
	union acpi_object *poll_result;	/* polling_result */
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

static void free_hotkey_device(union acpi_hotkey *key);
static void free_hotkey_buffer(union acpi_hotkey *key);
static void free_poll_hotkey_buffer(union acpi_hotkey *key);
static int hotkey_open_config(struct inode *inode, struct file *file);
static int hotkey_poll_open_config(struct inode *inode, struct file *file);
static ssize_t hotkey_write_config(struct file *file,
				   const char __user * buffer,
				   size_t count, loff_t * data);
static int hotkey_info_open_fs(struct inode *inode, struct file *file);
static int hotkey_action_open_fs(struct inode *inode, struct file *file);
static ssize_t hotkey_execute_aml_method(struct file *file,
					 const char __user * buffer,
					 size_t count, loff_t * data);
static int hotkey_config_seq_show(struct seq_file *seq, void *offset);
static int hotkey_poll_config_seq_show(struct seq_file *seq, void *offset);
static int hotkey_polling_open_fs(struct inode *inode, struct file *file);
static union acpi_hotkey *get_hotkey_by_event(struct
					      acpi_hotkey_list
					      *hotkey_list, int event);

/* event based config */
static const struct file_operations hotkey_config_fops = {
	.open = hotkey_open_config,
	.read = seq_read,
	.write = hotkey_write_config,
	.llseek = seq_lseek,
	.release = single_release,
};

/* polling based config */
static const struct file_operations hotkey_poll_config_fops = {
	.open = hotkey_poll_open_config,
	.read = seq_read,
	.write = hotkey_write_config,
	.llseek = seq_lseek,
	.release = single_release,
};

/* hotkey driver info */
static const struct file_operations hotkey_info_fops = {
	.open = hotkey_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* action */
static const struct file_operations hotkey_action_fops = {
	.open = hotkey_action_open_fs,
	.read = seq_read,
	.write = hotkey_execute_aml_method,
	.llseek = seq_lseek,
	.release = single_release,
};

/* polling results */
static const struct file_operations hotkey_polling_fops = {
	.open = hotkey_polling_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

struct acpi_hotkey_list global_hotkey_list;	/* link all ev or pl hotkey  */
struct list_head hotkey_entries;	/* head of the list of hotkey_list */

static int hotkey_info_seq_show(struct seq_file *seq, void *offset)
{

	seq_printf(seq, "Hotkey generic driver ver: %s\n", HOTKEY_ACPI_VERSION);

	return 0;
}

static int hotkey_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, hotkey_info_seq_show, PDE(inode)->data);
}

static char *format_result(union acpi_object *object)
{
	char *buf;

	buf = kzalloc(RESULT_STR_LEN, GFP_KERNEL);
	if (!buf)
		return NULL;
	/* Now, just support integer type */
	if (object->type == ACPI_TYPE_INTEGER)
		sprintf(buf, "%d\n", (u32) object->integer.value);
	return buf;
}

static int hotkey_polling_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_polling_hotkey *poll_hotkey = seq->private;
	char *buf;


	if (poll_hotkey->poll_result) {
		buf = format_result(poll_hotkey->poll_result);
		if (buf)
			seq_printf(seq, "%s", buf);
		kfree(buf);
	}
	return 0;
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
	struct list_head *entries;
	int val = -1;


	list_for_each(entries, list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT
		    && key->event_hotkey.external_hotkey_num == event) {
			val = key->link.hotkey_standard_num;
			break;
		}
	}

	return val;
}

static void
acpi_hotkey_notify_handler(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = NULL;
	u32 internal_event;


	if (acpi_bus_get_device(handle, &device))
		return;

	internal_event = hotkey_get_internal_event(event, &global_hotkey_list);
	acpi_bus_generate_event(device, internal_event, 0);

	return;
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
	char proc_name[80];
	mode_t mode;

	mode = S_IFREG | S_IRUGO | S_IWUGO;

	sprintf(proc_name, "%d", device->link.hotkey_standard_num);
	/*
	   strcat(proc_name, device->poll_hotkey.poll_method);
	 */
	proc = create_proc_entry(proc_name, mode, hotkey_proc_dir);

	if (!proc) {
		return -ENODEV;
	} else {
		proc->proc_fops = &hotkey_polling_fops;
		proc->owner = THIS_MODULE;
		proc->data = device;
		proc->uid = 0;
		proc->gid = 0;
		device->poll_hotkey.proc = proc;
	}
	return 0;
}

static int hotkey_add(union acpi_hotkey *device)
{
	int status = 0;
	struct acpi_device *dev = NULL;


	if (device->link.hotkey_type == ACPI_HOTKEY_EVENT) {
		acpi_bus_get_device(device->event_hotkey.bus_handle, &dev);
		status = acpi_install_notify_handler(dev->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_hotkey_notify_handler,
						     dev);
	} else			/* Add polling hotkey */
		create_polling_proc(device);

	global_hotkey_list.count++;

	list_add_tail(&device->link.entries, global_hotkey_list.entries);

	return status;
}

static int hotkey_remove(union acpi_hotkey *device)
{
	struct list_head *entries, *next;


	list_for_each_safe(entries, next, global_hotkey_list.entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_standard_num ==
		    device->link.hotkey_standard_num) {
			list_del(&key->link.entries);
			free_hotkey_device(key);
			global_hotkey_list.count--;
			break;
		}
	}
	kfree(device);
	return 0;
}

static int hotkey_update(union acpi_hotkey *key)
{
	struct list_head *entries;


	list_for_each(entries, global_hotkey_list.entries) {
		union acpi_hotkey *tmp =
		    container_of(entries, union acpi_hotkey, entries);
		if (tmp->link.hotkey_standard_num ==
		    key->link.hotkey_standard_num) {
			if (key->link.hotkey_type == ACPI_HOTKEY_EVENT) {
				free_hotkey_buffer(tmp);
				tmp->event_hotkey.bus_handle =
				    key->event_hotkey.bus_handle;
				tmp->event_hotkey.external_hotkey_num =
				    key->event_hotkey.external_hotkey_num;
				tmp->event_hotkey.action_handle =
				    key->event_hotkey.action_handle;
				tmp->event_hotkey.action_method =
				    key->event_hotkey.action_method;
				kfree(key);
			} else {
				/*
				   char  proc_name[80];

				   sprintf(proc_name, "%d", tmp->link.hotkey_standard_num);
				   strcat(proc_name, tmp->poll_hotkey.poll_method);
				   remove_proc_entry(proc_name,hotkey_proc_dir);
				 */
				free_poll_hotkey_buffer(tmp);
				tmp->poll_hotkey.poll_handle =
				    key->poll_hotkey.poll_handle;
				tmp->poll_hotkey.poll_method =
				    key->poll_hotkey.poll_method;
				tmp->poll_hotkey.action_handle =
				    key->poll_hotkey.action_handle;
				tmp->poll_hotkey.action_method =
				    key->poll_hotkey.action_method;
				tmp->poll_hotkey.poll_result =
				    key->poll_hotkey.poll_result;
				/*
				   create_polling_proc(tmp);
				 */
				kfree(key);
			}
			return 0;
			break;
		}
	}

	return -ENODEV;
}

static void free_hotkey_device(union acpi_hotkey *key)
{
	struct acpi_device *dev;


	if (key->link.hotkey_type == ACPI_HOTKEY_EVENT) {
		acpi_bus_get_device(key->event_hotkey.bus_handle, &dev);
		if (dev->handle)
			acpi_remove_notify_handler(dev->handle,
						   ACPI_DEVICE_NOTIFY,
						   acpi_hotkey_notify_handler);
		free_hotkey_buffer(key);
	} else {
		char proc_name[80];

		sprintf(proc_name, "%d", key->link.hotkey_standard_num);
		/*
		   strcat(proc_name, key->poll_hotkey.poll_method);
		 */
		remove_proc_entry(proc_name, hotkey_proc_dir);
		free_poll_hotkey_buffer(key);
	}
	kfree(key);
	return;
}

static void free_hotkey_buffer(union acpi_hotkey *key)
{
	/* key would never be null, action method could be */
	kfree(key->event_hotkey.action_method);
}

static void free_poll_hotkey_buffer(union acpi_hotkey *key)
{
	/* key would never be null, others could be*/
	kfree(key->poll_hotkey.action_method);
	kfree(key->poll_hotkey.poll_method);
	kfree(key->poll_hotkey.poll_result);
}
static int
init_hotkey_device(union acpi_hotkey *key, char **config_entry,
		   int std_num, int external_num)
{
	acpi_handle tmp_handle;
	acpi_status status = AE_OK;

	if (std_num < 0 || IS_POLL(std_num) || !key)
		goto do_fail;

	if (!config_entry[bus_handle] || !config_entry[action_handle]
			|| !config_entry[method])
		goto do_fail;

	key->link.hotkey_type = ACPI_HOTKEY_EVENT;
	key->link.hotkey_standard_num = std_num;
	key->event_hotkey.flag = 0;
	key->event_hotkey.action_method = config_entry[method];

	status = acpi_get_handle(NULL, config_entry[bus_handle],
			   &(key->event_hotkey.bus_handle));
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	key->event_hotkey.external_hotkey_num = external_num;
	status = acpi_get_handle(NULL, config_entry[action_handle],
			    &(key->event_hotkey.action_handle));
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	status = acpi_get_handle(key->event_hotkey.action_handle,
				 config_entry[method], &tmp_handle);
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	return AE_OK;
do_fail_zero:
	key->event_hotkey.action_method = NULL;
do_fail:
	return -ENODEV;
}

static int
init_poll_hotkey_device(union acpi_hotkey *key, char **config_entry,
			int std_num)
{
	acpi_status status = AE_OK;
	acpi_handle tmp_handle;

	if (std_num < 0 || IS_EVENT(std_num) || !key)
		goto do_fail;
	if (!config_entry[bus_handle] ||!config_entry[bus_method] ||
		!config_entry[action_handle] || !config_entry[method])
		goto do_fail;

	key->link.hotkey_type = ACPI_HOTKEY_POLLING;
	key->link.hotkey_standard_num = std_num;
	key->poll_hotkey.flag = 0;
	key->poll_hotkey.poll_method = config_entry[bus_method];
	key->poll_hotkey.action_method = config_entry[method];

	status = acpi_get_handle(NULL, config_entry[bus_handle],
		      &(key->poll_hotkey.poll_handle));
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	status = acpi_get_handle(key->poll_hotkey.poll_handle,
				 config_entry[bus_method], &tmp_handle);
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	status =
	    acpi_get_handle(NULL, config_entry[action_handle],
			    &(key->poll_hotkey.action_handle));
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	status = acpi_get_handle(key->poll_hotkey.action_handle,
				 config_entry[method], &tmp_handle);
	if (ACPI_FAILURE(status))
		goto do_fail_zero;
	key->poll_hotkey.poll_result =
	    kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	if (!key->poll_hotkey.poll_result)
		goto do_fail_zero;
	return AE_OK;

do_fail_zero:
	key->poll_hotkey.poll_method = NULL;
	key->poll_hotkey.action_method = NULL;
do_fail:
	return -ENODEV;
}

static int hotkey_open_config(struct inode *inode, struct file *file)
{
	return (single_open
		     (file, hotkey_config_seq_show, PDE(inode)->data));
}

static int hotkey_poll_open_config(struct inode *inode, struct file *file)
{
	return (single_open
		     (file, hotkey_poll_config_seq_show, PDE(inode)->data));
}

static int hotkey_config_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey_list *hotkey_list = &global_hotkey_list;
	struct list_head *entries;
	char bus_name[ACPI_PATHNAME_MAX] = { 0 };
	char action_name[ACPI_PATHNAME_MAX] = { 0 };
	struct acpi_buffer bus = { ACPI_PATHNAME_MAX, bus_name };
	struct acpi_buffer act = { ACPI_PATHNAME_MAX, action_name };


	list_for_each(entries, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_EVENT) {
			acpi_get_name(key->event_hotkey.bus_handle,
				      ACPI_NAME_TYPE_MAX, &bus);
			acpi_get_name(key->event_hotkey.action_handle,
				      ACPI_NAME_TYPE_MAX, &act);
			seq_printf(seq, "%s:%s:%s:%d:%d\n", bus_name,
				   action_name,
				   key->event_hotkey.action_method,
				   key->link.hotkey_standard_num,
				   key->event_hotkey.external_hotkey_num);
		}
	}
	seq_puts(seq, "\n");
	return 0;
}

static int hotkey_poll_config_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey_list *hotkey_list = &global_hotkey_list;
	struct list_head *entries;
	char bus_name[ACPI_PATHNAME_MAX] = { 0 };
	char action_name[ACPI_PATHNAME_MAX] = { 0 };
	struct acpi_buffer bus = { ACPI_PATHNAME_MAX, bus_name };
	struct acpi_buffer act = { ACPI_PATHNAME_MAX, action_name };


	list_for_each(entries, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_type == ACPI_HOTKEY_POLLING) {
			acpi_get_name(key->poll_hotkey.poll_handle,
				      ACPI_NAME_TYPE_MAX, &bus);
			acpi_get_name(key->poll_hotkey.action_handle,
				      ACPI_NAME_TYPE_MAX, &act);
			seq_printf(seq, "%s:%s:%s:%s:%d\n", bus_name,
				   key->poll_hotkey.poll_method,
				   action_name,
				   key->poll_hotkey.action_method,
				   key->link.hotkey_standard_num);
		}
	}
	seq_puts(seq, "\n");
	return 0;
}

static int
get_parms(char *config_record, int *cmd, char **config_entry,
	       int *internal_event_num, int *external_event_num)
{
/* the format of *config_record =
 * "1:\d+:*" : "cmd:internal_event_num"
 * "\d+:\w+:\w+:\w+:\w+:\d+:\d+" :
 * "cmd:bus_handle:bus_method:action_handle:method:internal_event_num:external_event_num"
 */
	char *tmp, *tmp1, count;
	int i;

	sscanf(config_record, "%d", cmd);
	if (*cmd == 1) {
		if (sscanf(config_record, "%d:%d", cmd, internal_event_num) !=
		    2)
			goto do_fail;
		else
			return (6);
	}
	tmp = strchr(config_record, ':');
	if (!tmp)
		goto do_fail;
	tmp++;
	for (i = 0; i < LAST_CONF_ENTRY; i++) {
		tmp1 = strchr(tmp, ':');
		if (!tmp1) {
			goto do_fail;
		}
		count = tmp1 - tmp;
		config_entry[i] = kzalloc(count + 1, GFP_KERNEL);
		if (!config_entry[i])
			goto handle_failure;
		strncpy(config_entry[i], tmp, count);
		tmp = tmp1 + 1;
	}
	if (sscanf(tmp, "%d:%d", internal_event_num, external_event_num) <= 0)
		goto handle_failure;
	if (!IS_OTHERS(*internal_event_num)) {
		return 6;
	}
handle_failure:
	while (i-- > 0)
		kfree(config_entry[i]);
do_fail:
	return -1;
}

/*  count is length for one input record */
static ssize_t hotkey_write_config(struct file *file,
				   const char __user * buffer,
				   size_t count, loff_t * data)
{
	char *config_record = NULL;
	char *config_entry[LAST_CONF_ENTRY];
	int cmd, internal_event_num, external_event_num;
	int ret = 0;
	union acpi_hotkey *key = kzalloc(sizeof(union acpi_hotkey), GFP_KERNEL);

	if (!key)
		return -ENOMEM;

	config_record = kzalloc(count + 1, GFP_KERNEL);
	if (!config_record) {
		kfree(key);
		return -ENOMEM;
	}

	if (copy_from_user(config_record, buffer, count)) {
		kfree(config_record);
		kfree(key);
		printk(KERN_ERR PREFIX "Invalid data\n");
		return -EINVAL;
	}
	ret = get_parms(config_record, &cmd, config_entry,
		       &internal_event_num, &external_event_num);
	kfree(config_record);
	if (ret != 6) {
		printk(KERN_ERR PREFIX "Invalid data format ret=%d\n", ret);
		return -EINVAL;
	}

	if (cmd == 1) {
		union acpi_hotkey *tmp = NULL;
		tmp = get_hotkey_by_event(&global_hotkey_list,
					  internal_event_num);
		if (!tmp)
			printk(KERN_ERR PREFIX "Invalid key\n");
		else
			memcpy(key, tmp, sizeof(union acpi_hotkey));
		goto cont_cmd;
	}
	if (IS_EVENT(internal_event_num)) {
		if (init_hotkey_device(key, config_entry,
			internal_event_num, external_event_num))
			goto init_hotkey_fail;
	} else {
		if (init_poll_hotkey_device(key, config_entry,
			       internal_event_num))
			goto init_poll_hotkey_fail;
	}
cont_cmd:
	switch (cmd) {
	case 0:
		if (get_hotkey_by_event(&global_hotkey_list,
				key->link.hotkey_standard_num))
			goto fail_out;
		else
			hotkey_add(key);
		break;
	case 1:
		hotkey_remove(key);
		break;
	case 2:
		/* key is kfree()ed if matched*/
		if (hotkey_update(key))
			goto fail_out;
		break;
	default:
		goto fail_out;
		break;
	}
	return count;

init_poll_hotkey_fail:		/* failed init_poll_hotkey_device */
	kfree(config_entry[bus_method]);
	config_entry[bus_method] = NULL;
init_hotkey_fail:		/* failed init_hotkey_device */
	kfree(config_entry[method]);
fail_out:
	kfree(config_entry[bus_handle]);
	kfree(config_entry[action_handle]);
	/* No double free since elements =NULL for error cases */
	if (IS_EVENT(internal_event_num)) {
		if (config_entry[bus_method])
			kfree(config_entry[bus_method]);
		free_hotkey_buffer(key);	/* frees [method] */
	} else
		free_poll_hotkey_buffer(key);  /* frees [bus_method]+[method] */
	kfree(key);
	printk(KERN_ERR PREFIX "invalid key\n");
	return -EINVAL;
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

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);

	return (status == AE_OK);
}

static int read_acpi_int(acpi_handle handle, const char *method,
			 union acpi_object *val)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, (char *)method, NULL, &output);
	if (val) {
		val->integer.value = out_obj.integer.value;
		val->type = out_obj.type;
	} else
		printk(KERN_ERR PREFIX "null val pointer\n");
	return ((status == AE_OK)
		     && (out_obj.type == ACPI_TYPE_INTEGER));
}

static union acpi_hotkey *get_hotkey_by_event(struct
					      acpi_hotkey_list
					      *hotkey_list, int event)
{
	struct list_head *entries;

	list_for_each(entries, hotkey_list->entries) {
		union acpi_hotkey *key =
		    container_of(entries, union acpi_hotkey, entries);
		if (key->link.hotkey_standard_num == event) {
			return (key);
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
	char *arg;
	int event, method_type, type, value;
	union acpi_hotkey *key;


	arg = kzalloc(count + 1, GFP_KERNEL);
	if (!arg)
		return -ENOMEM;

	if (copy_from_user(arg, buffer, count)) {
		kfree(arg);
		printk(KERN_ERR PREFIX "Invalid argument 2\n");
		return -EINVAL;
	}

	if (sscanf(arg, "%d:%d:%d:%d", &event, &method_type, &type, &value) !=
	    4) {
		kfree(arg);
		printk(KERN_ERR PREFIX "Invalid argument 3\n");
		return -EINVAL;
	}
	kfree(arg);
	if (type == ACPI_TYPE_INTEGER) {
		key = get_hotkey_by_event(hotkey_list, event);
		if (!key)
			goto do_fail;
		if (IS_EVENT(event))
			write_acpi_int(key->event_hotkey.action_handle,
				       key->event_hotkey.action_method, value,
				       NULL);
		else if (IS_POLL(event)) {
			if (method_type == POLL_METHOD)
				read_acpi_int(key->poll_hotkey.poll_handle,
					      key->poll_hotkey.poll_method,
					      key->poll_hotkey.poll_result);
			else if (method_type == ACTION_METHOD)
				write_acpi_int(key->poll_hotkey.action_handle,
					       key->poll_hotkey.action_method,
					       value, NULL);
			else
				goto do_fail;

		}
	} else {
		printk(KERN_WARNING "Not supported\n");
		return -EINVAL;
	}
	return count;
      do_fail:
	return -EINVAL;

}

static int __init hotkey_init(void)
{
	int result;
	mode_t mode = S_IFREG | S_IRUGO | S_IWUGO;


	if (acpi_disabled)
		return -ENODEV;

	if (acpi_specific_hotkey_enabled) {
		printk("Using specific hotkey driver\n");
		return -ENODEV;
	}

	hotkey_proc_dir = proc_mkdir(HOTKEY_PROC, acpi_root_dir);
	if (!hotkey_proc_dir) {
		return (-ENODEV);
	}
	hotkey_proc_dir->owner = THIS_MODULE;

	hotkey_config =
	    create_proc_entry(HOTKEY_EV_CONFIG, mode, hotkey_proc_dir);
	if (!hotkey_config) {
		goto do_fail1;
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
		goto do_fail2;
	} else {
		hotkey_poll_config->proc_fops = &hotkey_poll_config_fops;
		hotkey_poll_config->data = &global_hotkey_list;
		hotkey_poll_config->owner = THIS_MODULE;
		hotkey_poll_config->uid = 0;
		hotkey_poll_config->gid = 0;
	}

	hotkey_action = create_proc_entry(HOTKEY_ACTION, mode, hotkey_proc_dir);
	if (!hotkey_action) {
		goto do_fail3;
	} else {
		hotkey_action->proc_fops = &hotkey_action_fops;
		hotkey_action->owner = THIS_MODULE;
		hotkey_action->uid = 0;
		hotkey_action->gid = 0;
	}

	hotkey_info = create_proc_entry(HOTKEY_INFO, mode, hotkey_proc_dir);
	if (!hotkey_info) {
		goto do_fail4;
	} else {
		hotkey_info->proc_fops = &hotkey_info_fops;
		hotkey_info->owner = THIS_MODULE;
		hotkey_info->uid = 0;
		hotkey_info->gid = 0;
	}

	result = acpi_bus_register_driver(&hotkey_driver);
	if (result < 0)
		goto do_fail5;
	global_hotkey_list.count = 0;
	global_hotkey_list.entries = &hotkey_entries;

	INIT_LIST_HEAD(&hotkey_entries);

	return (0);

      do_fail5:
	remove_proc_entry(HOTKEY_INFO, hotkey_proc_dir);
      do_fail4:
	remove_proc_entry(HOTKEY_ACTION, hotkey_proc_dir);
      do_fail3:
	remove_proc_entry(HOTKEY_PL_CONFIG, hotkey_proc_dir);
      do_fail2:
	remove_proc_entry(HOTKEY_EV_CONFIG, hotkey_proc_dir);
      do_fail1:
	remove_proc_entry(HOTKEY_PROC, acpi_root_dir);
	return (-ENODEV);
}

static void __exit hotkey_exit(void)
{
	struct list_head *entries, *next;


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
