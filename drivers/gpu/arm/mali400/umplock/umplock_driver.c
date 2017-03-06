/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include "umplock_ioctl.h"
#include <linux/sched.h>

#define MAX_ITEMS 1024
#define MAX_PIDS 128

typedef struct lock_cmd_priv {
	uint32_t msg[128];    /*ioctl args*/
	u32 pid;              /*process id*/
} _lock_cmd_priv;

typedef struct lock_ref {
	int ref_count;
	u32 pid;
	u32 down_count;
} _lock_ref;

typedef struct umplock_item {
	u32 secure_id;
	u32 id_ref_count;
	u32 owner;
	_lock_access_usage usage;
	_lock_ref references[MAX_PIDS];
	struct semaphore item_lock;
} umplock_item;

typedef struct umplock_device_private {
	struct mutex item_list_lock;
	atomic_t sessions;
	umplock_item items[MAX_ITEMS];
	u32 pids[MAX_PIDS];
} umplock_device_private;

struct umplock_device {
	struct cdev cdev;
	struct class *umplock_class;
};

static struct umplock_device umplock_device;
static umplock_device_private device;
static dev_t umplock_dev;
static char umplock_dev_name[] = "umplock";

int umplock_debug_level = 0;
module_param(umplock_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(umplock_debug_level, "set umplock_debug_level to print debug messages");

#define PDEBUG(level, fmt, args...) do { if ((level) <= umplock_debug_level) printk(KERN_DEBUG "umplock: " fmt, ##args); } while (0)
#define PERROR(fmt, args...) do { printk(KERN_ERR "umplock: " fmt, ##args); } while (0)

int umplock_find_item(u32 secure_id)
{
	int i;
	for (i = 0; i < MAX_ITEMS; i++) {
		if (device.items[i].secure_id == secure_id) {
			return i;
		}
	}

	return -1;
}

static int umplock_find_item_by_pid(_lock_cmd_priv *lock_cmd, int *item_slot, int *ref_slot)
{
	_lock_item_s *lock_item;
	int i, j;

	lock_item = (_lock_item_s *)&lock_cmd->msg;

	i = umplock_find_item(lock_item->secure_id);

	if (i < 0) {
		return -1;
	}

	for (j = 0; j < MAX_PIDS; j++) {
		if (device.items[i].references[j].pid == lock_cmd->pid) {
			*item_slot = i;
			*ref_slot = j;
			return 0;
		}
	}
	return -1 ;
}

static int umplock_find_client_valid(u32 pid)
{
	int i;

	if (pid == 0) {
		return -1;
	}

	for (i = 0; i < MAX_PIDS; i++) {
		if (device.pids[i] == pid) {
			return i;
		}
	}

	return -1;
}

static int do_umplock_create_locked(_lock_cmd_priv *lock_cmd)
{
	int i_index, ref_index;
	int ret;
	_lock_item_s *lock_item = (_lock_item_s *)&lock_cmd->msg;

	i_index = ref_index = -1;

	ret = umplock_find_client_valid(lock_cmd->pid);
	if (ret < 0) {
		/*lock request from an invalid client pid, do nothing*/
		return -EINVAL;
	}

	ret = umplock_find_item_by_pid(lock_cmd, &i_index, &ref_index);
	if (ret >= 0) {
	} else if ((i_index = umplock_find_item(lock_item->secure_id)) >= 0) {
		for (ref_index = 0; ref_index < MAX_PIDS; ref_index++) {
			if (device.items[i_index].references[ref_index].pid == 0) {
				break;
			}
		}
		if (ref_index < MAX_PIDS) {
			device.items[i_index].references[ref_index].pid = lock_cmd->pid;
			device.items[i_index].references[ref_index].ref_count = 0;
			device.items[i_index].references[ref_index].down_count = 0;
		} else {
			PERROR("whoops, item ran out of available reference slots\n");
			return -EINVAL;

		}
	} else {
		i_index = umplock_find_item(0);

		if (i_index >= 0) {
			device.items[i_index].secure_id = lock_item->secure_id;
			device.items[i_index].id_ref_count = 0;
			device.items[i_index].usage = lock_item->usage;
			device.items[i_index].references[0].pid = lock_cmd->pid;
			device.items[i_index].references[0].ref_count = 0;
			device.items[i_index].references[0].down_count = 0;
			sema_init(&device.items[i_index].item_lock, 1);
		} else {
			PERROR("whoops, ran out of available slots\n");
			return -EINVAL;
		}
	}

	return 0;
}
/** IOCTLs **/

static int do_umplock_create(_lock_cmd_priv *lock_cmd)
{
	return 0;
}

static int do_umplock_process(_lock_cmd_priv *lock_cmd)
{
	int ret, i_index, ref_index;
	_lock_item_s *lock_item = (_lock_item_s *)&lock_cmd->msg;

	mutex_lock(&device.item_list_lock);

	if (0 == lock_item->secure_id) {
		PERROR("IOCTL_UMPLOCK_PROCESS called with secure_id is 0, pid: %d\n", lock_cmd->pid);
		mutex_unlock(&device.item_list_lock);
		return -EINVAL;
	}

	ret = do_umplock_create_locked(lock_cmd);
	if (ret < 0) {
		mutex_unlock(&device.item_list_lock);
		return -EINVAL;
	}

	ret = umplock_find_item_by_pid(lock_cmd, &i_index, &ref_index);
	if (ret < 0) {
		/*fail to find a item*/
		PERROR("IOCTL_UMPLOCK_PROCESS called with invalid parameter, pid: %d\n", lock_cmd->pid);
		mutex_unlock(&device.item_list_lock);
		return -EINVAL;
	}
	device.items[i_index].references[ref_index].ref_count++;
	device.items[i_index].id_ref_count++;
	PDEBUG(1, "try to lock, pid: %d, secure_id: 0x%x, ref_count: %d\n", lock_cmd->pid, lock_item->secure_id, device.items[i_index].references[ref_index].ref_count);

	if (lock_cmd->pid == device.items[i_index].owner) {
		PDEBUG(1, "already own the lock, pid: %d, secure_id: 0x%x, ref_count: %d\n", lock_cmd->pid, lock_item->secure_id, device.items[i_index].references[ref_index].ref_count);
		mutex_unlock(&device.item_list_lock);
		return 0;
	}

	device.items[i_index].references[ref_index].down_count++;
	mutex_unlock(&device.item_list_lock);
	if (down_interruptible(&device.items[i_index].item_lock)) {
		/*wait up without hold the umplock. restore previous state and return*/
		mutex_lock(&device.item_list_lock);
		device.items[i_index].references[ref_index].ref_count--;
		device.items[i_index].id_ref_count--;
		device.items[i_index].references[ref_index].down_count--;
		if (0 == device.items[i_index].references[ref_index].ref_count) {
			device.items[i_index].references[ref_index].pid = 0;
			if (0 == device.items[i_index].id_ref_count) {
				PDEBUG(1, "release item, pid: %d, secure_id: 0x%x\n", lock_cmd->pid, lock_item->secure_id);
				device.items[i_index].secure_id = 0;
			}
		}

		PERROR("failed lock, pid: %d, secure_id: 0x%x, ref_count: %d\n", lock_cmd->pid, lock_item->secure_id, device.items[i_index].references[ref_index].ref_count);

		mutex_unlock(&device.item_list_lock);
		return -ERESTARTSYS;
	}

	mutex_lock(&device.item_list_lock);
	PDEBUG(1, "got lock, pid: %d, secure_id: 0x%x, ref_count: %d\n", lock_cmd->pid, lock_item->secure_id, device.items[i_index].references[ref_index].ref_count);
	device.items[i_index].owner = lock_cmd->pid;
	mutex_unlock(&device.item_list_lock);

	return 0;
}

static int do_umplock_release(_lock_cmd_priv *lock_cmd)
{
	int ret, i_index, ref_index, call_up;
	_lock_item_s *lock_item = (_lock_item_s *)&lock_cmd->msg;

	mutex_lock(&device.item_list_lock);

	if (0 == lock_item->secure_id) {
		PERROR("IOCTL_UMPLOCK_RELEASE called with secure_id is 0, pid: %d\n", lock_cmd->pid);
		mutex_unlock(&device.item_list_lock);
		return -EINVAL;
	}

	ret = umplock_find_client_valid(lock_cmd->pid);
	if (ret < 0) {
		/*lock request from an invalid client pid, do nothing*/
		mutex_unlock(&device.item_list_lock);
		return -EPERM;
	}

	i_index = ref_index = -1;

	ret = umplock_find_item_by_pid(lock_cmd, &i_index, &ref_index);
	if (ret < 0) {
		/*fail to find item*/
		PERROR("IOCTL_UMPLOCK_RELEASE called with invalid parameter pid: %d, secid: 0x%x\n", lock_cmd->pid, lock_item->secure_id);
		mutex_unlock(&device.item_list_lock);
		return -EINVAL;
	}

	/* if the lock is not owned by this process */
	if (lock_cmd->pid != device.items[i_index].owner) {
		mutex_unlock(&device.item_list_lock);
		return -EPERM;
	}

	/* if the ref_count is 0, that means nothing to unlock, just return */
	if (0 == device.items[i_index].references[ref_index].ref_count) {
		mutex_unlock(&device.item_list_lock);
		return 0;
	}

	device.items[i_index].references[ref_index].ref_count--;
	device.items[i_index].id_ref_count--;
	PDEBUG(1, "unlock, pid: %d, secure_id: 0x%x, ref_count: %d\n", lock_cmd->pid, lock_item->secure_id, device.items[i_index].references[ref_index].ref_count);

	call_up = 0;
	if (device.items[i_index].references[ref_index].down_count > 1) {
		call_up = 1;
		device.items[i_index].references[ref_index].down_count--;
	}
	if (0 == device.items[i_index].references[ref_index].ref_count) {
		device.items[i_index].references[ref_index].pid = 0;
		if (0 == device.items[i_index].id_ref_count) {
			PDEBUG(1, "release item, pid: %d, secure_id: 0x%x\n", lock_cmd->pid, lock_item->secure_id);
			device.items[i_index].secure_id = 0;
		}
		device.items[i_index].owner = 0;
		call_up = 1;
	}
	if (call_up) {
		PDEBUG(1, "call up, pid: %d, secure_id: 0x%x\n", lock_cmd->pid, lock_item->secure_id);
		up(&device.items[i_index].item_lock);
	}
	mutex_unlock(&device.item_list_lock);

	return 0;
}

static int do_umplock_zap(void)
{
	int i;

	PDEBUG(1, "ZAP ALL ENTRIES!\n");

	mutex_lock(&device.item_list_lock);

	for (i = 0; i < MAX_ITEMS; i++) {
		device.items[i].secure_id = 0;
		memset(&device.items[i].references, 0, sizeof(_lock_ref) * MAX_PIDS);
		sema_init(&device.items[i].item_lock, 1);
	}

	for (i = 0; i < MAX_PIDS; i++) {
		device.pids[i] = 0;
	}
	mutex_unlock(&device.item_list_lock);

	return 0;
}

static int do_umplock_dump(void)
{
	int i, j;

	mutex_lock(&device.item_list_lock);
	PERROR("dump all the items begin\n");
	for (i = 0; i < MAX_ITEMS; i++) {
		for (j = 0; j < MAX_PIDS; j++) {
			if (device.items[i].secure_id != 0 && device.items[i].references[j].pid != 0) {
				PERROR("item[%d]->secure_id=0x%x, owner=%d\t reference[%d].ref_count=%d.pid=%d\n",
				       i,
				       device.items[i].secure_id,
				       device.items[i].owner,
				       j,
				       device.items[i].references[j].ref_count,
				       device.items[i].references[j].pid);
			}
		}
	}
	PERROR("dump all the items end\n");
	mutex_unlock(&device.item_list_lock);

	return 0;
}

int do_umplock_client_add(_lock_cmd_priv *lock_cmd)
{
	int i;
	mutex_lock(&device.item_list_lock);
	for (i = 0; i < MAX_PIDS; i++) {
		if (device.pids[i] == lock_cmd->pid) {
			mutex_unlock(&device.item_list_lock);
			return 0;
		}
	}
	for (i = 0; i < MAX_PIDS; i++) {
		if (device.pids[i] == 0) {
			device.pids[i] = lock_cmd->pid;
			break;
		}
	}
	mutex_unlock(&device.item_list_lock);
	if (i == MAX_PIDS) {
		PERROR("Oops, Run out of client slots\n ");
		return -EINVAL;
	}
	return 0;
}

int do_umplock_client_delete(_lock_cmd_priv *lock_cmd)
{
	int p_index = -1, i_index = -1, ref_index = -1;
	int ret;
	_lock_item_s *lock_item;
	lock_item = (_lock_item_s *)&lock_cmd->msg;

	mutex_lock(&device.item_list_lock);
	p_index = umplock_find_client_valid(lock_cmd->pid);
	/*lock item pid is not valid.*/
	if (p_index < 0) {
		mutex_unlock(&device.item_list_lock);
		return 0;
	}

	/*walk through umplock item list and release reference attached to this client*/
	for (i_index = 0; i_index < MAX_ITEMS; i_index++) {
		lock_item->secure_id = device.items[i_index].secure_id;

		/*find the item index and reference slot for the lock_item*/
		ret = umplock_find_item_by_pid(lock_cmd, &i_index, &ref_index);

		if (ret < 0) {
			/*client has no reference on this umplock item, skip*/
			continue;
		}
		while (device.items[i_index].references[ref_index].ref_count) {
			/*release references on this client*/

			PDEBUG(1, "delete client, pid: %d, ref_count: %d\n", lock_cmd->pid, device.items[i_index].references[ref_index].ref_count);

			mutex_unlock(&device.item_list_lock);
			do_umplock_release(lock_cmd);
			mutex_lock(&device.item_list_lock);
		}
	}

	/*remove the pid from umplock valid pid list*/
	device.pids[p_index] = 0;
	mutex_unlock(&device.item_list_lock);

	return 0;
}

static long umplock_driver_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int ret;
	uint32_t size = _IOC_SIZE(cmd);
	_lock_cmd_priv lock_cmd ;

	if (_IOC_TYPE(cmd) != LOCK_IOCTL_GROUP) {
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) >= LOCK_IOCTL_MAX_CMDS) {
		return -ENOTTY;
	}

	switch (cmd) {
	case LOCK_IOCTL_CREATE:
		if (size != sizeof(_lock_item_s)) {
			return -ENOTTY;
		}

		if (copy_from_user(&lock_cmd.msg, (void __user *)arg, size)) {
			return -EFAULT;
		}
		lock_cmd.pid = (u32)current->tgid;
		ret = do_umplock_create(&lock_cmd);
		if (ret) {
			return ret;
		}
		return 0;

	case LOCK_IOCTL_PROCESS:
		if (size != sizeof(_lock_item_s)) {
			return -ENOTTY;
		}

		if (copy_from_user(&lock_cmd.msg, (void __user *)arg, size)) {
			return -EFAULT;
		}
		lock_cmd.pid = (u32)current->tgid;
		return do_umplock_process(&lock_cmd);

	case LOCK_IOCTL_RELEASE:
		if (size != sizeof(_lock_item_s)) {
			return -ENOTTY;
		}

		if (copy_from_user(&lock_cmd.msg, (void __user *)arg, size)) {
			return -EFAULT;
		}
		lock_cmd.pid = (u32)current->tgid;
		ret = do_umplock_release(&lock_cmd);
		if (ret) {
			return ret;
		}
		return 0;

	case LOCK_IOCTL_ZAP:
		do_umplock_zap();
		return 0;

	case LOCK_IOCTL_DUMP:
		do_umplock_dump();
		return 0;
	}

	return -ENOIOCTLCMD;
}

static int umplock_driver_open(struct inode *inode, struct file *filp)
{
	_lock_cmd_priv lock_cmd;

	atomic_inc(&device.sessions);
	PDEBUG(1, "OPEN SESSION (%i references)\n", atomic_read(&device.sessions));

	lock_cmd.pid = (u32)current->tgid;
	do_umplock_client_add(&lock_cmd);

	return 0;
}

static int umplock_driver_release(struct inode *inode, struct file *filp)
{
	int sessions = 0;
	_lock_cmd_priv lock_cmd;

	lock_cmd.pid = (u32)current->tgid;
	do_umplock_client_delete(&lock_cmd);

	mutex_lock(&device.item_list_lock);
	atomic_dec(&device.sessions);
	sessions = atomic_read(&device.sessions);
	PDEBUG(1, "CLOSE SESSION (%i references)\n", sessions);
	mutex_unlock(&device.item_list_lock);
	if (sessions == 0) {
		do_umplock_zap();
	}

	return 0;
}

static struct file_operations umplock_fops = {
	.owner   = THIS_MODULE,
	.open    = umplock_driver_open,
	.release = umplock_driver_release,
	.unlocked_ioctl = umplock_driver_ioctl,
};

int umplock_device_initialize(void)
{
	int err;

	err = alloc_chrdev_region(&umplock_dev, 0, 1, umplock_dev_name);

	if (0 == err) {
		memset(&umplock_device, 0, sizeof(umplock_device));
		cdev_init(&umplock_device.cdev, &umplock_fops);
		umplock_device.cdev.owner = THIS_MODULE;
		umplock_device.cdev.ops = &umplock_fops;

		err = cdev_add(&umplock_device.cdev, umplock_dev, 1);
		if (0 == err) {
			umplock_device.umplock_class = class_create(THIS_MODULE, umplock_dev_name);
			if (IS_ERR(umplock_device.umplock_class)) {
				err = PTR_ERR(umplock_device.umplock_class);
			} else {
				struct device *mdev;
				mdev = device_create(umplock_device.umplock_class, NULL, umplock_dev, NULL, umplock_dev_name);
				if (!IS_ERR(mdev)) {
					return 0; /* all ok */
				}

				err = PTR_ERR(mdev);
				class_destroy(umplock_device.umplock_class);
			}
			cdev_del(&umplock_device.cdev);
		}

		unregister_chrdev_region(umplock_dev, 1);
	} else {
		PERROR("alloc chardev region failed\n");
	}

	return err;
}

void umplock_device_terminate(void)
{
	device_destroy(umplock_device.umplock_class, umplock_dev);
	class_destroy(umplock_device.umplock_class);

	cdev_del(&umplock_device.cdev);
	unregister_chrdev_region(umplock_dev, 1);
}

static int __init umplock_initialize_module(void)
{
	PDEBUG(1, "Inserting UMP lock device driver. Compiled: %s, time: %s\n", __DATE__, __TIME__);

	mutex_init(&device.item_list_lock);
	if (umplock_device_initialize() != 0) {
		PERROR("UMP lock device driver init failed\n");
		return -ENOTTY;
	}
	memset(&device.items, 0, sizeof(umplock_item) * MAX_ITEMS);
	memset(&device.pids, 0, sizeof(u32) * MAX_PIDS);
	atomic_set(&device.sessions, 0);

	PDEBUG(1, "UMP lock device driver loaded\n");

	return 0;
}

static void __exit umplock_cleanup_module(void)
{
	PDEBUG(1, "unloading UMP lock module\n");

	memset(&device.items, 0, sizeof(umplock_item) * MAX_ITEMS);
	memset(&device.pids, 0, sizeof(u32) * MAX_PIDS);
	umplock_device_terminate();
	mutex_destroy(&device.item_list_lock);

	PDEBUG(1, "UMP lock module unloaded\n");
}

module_init(umplock_initialize_module);
module_exit(umplock_cleanup_module);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_DESCRIPTION("ARM UMP locker");
