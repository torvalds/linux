/* $Id: divasi.c,v 1.25.6.2 2005/01/31 12:22:20 armin Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * User Mode IDI Interface
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "um_xdi.h"
#include "um_idi.h"

static char *main_revision = "$Revision: 1.25.6.2 $";

static int major;

MODULE_DESCRIPTION("User IDI Interface for Eicon ISDN cards");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("DIVA card driver");
MODULE_LICENSE("GPL");

typedef struct _diva_um_idi_os_context {
	wait_queue_head_t read_wait;
	wait_queue_head_t close_wait;
	struct timer_list diva_timer_id;
	int aborted;
	int adapter_nr;
} diva_um_idi_os_context_t;

static char *DRIVERNAME = "Eicon DIVA - User IDI (http://www.melware.net)";
static char *DRIVERLNAME = "diva_idi";
static char *DEVNAME = "DivasIDI";
char *DRIVERRELEASE_IDI = "2.0";

extern int idifunc_init(void);
extern void idifunc_finit(void);

/*
 *  helper functions
 */
static char *getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";
	return rev;
}

/*
 *  LOCALS
 */
static ssize_t um_idi_read(struct file *file, char __user *buf, size_t count,
			   loff_t *offset);
static ssize_t um_idi_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offset);
static unsigned int um_idi_poll(struct file *file, poll_table *wait);
static int um_idi_open(struct inode *inode, struct file *file);
static int um_idi_release(struct inode *inode, struct file *file);
static int remove_entity(void *entity);
static void diva_um_timer_function(unsigned long data);

/*
 * proc entry
 */
extern struct proc_dir_entry *proc_net_eicon;
static struct proc_dir_entry *um_idi_proc_entry = NULL;

static int um_idi_proc_show(struct seq_file *m, void *v)
{
	char tmprev[32];

	seq_printf(m, "%s\n", DRIVERNAME);
	seq_printf(m, "name     : %s\n", DRIVERLNAME);
	seq_printf(m, "release  : %s\n", DRIVERRELEASE_IDI);
	strcpy(tmprev, main_revision);
	seq_printf(m, "revision : %s\n", getrev(tmprev));
	seq_printf(m, "build    : %s\n", DIVA_BUILD);
	seq_printf(m, "major    : %d\n", major);

	return 0;
}

static int um_idi_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, um_idi_proc_show, NULL);
}

static const struct file_operations um_idi_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= um_idi_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init create_um_idi_proc(void)
{
	um_idi_proc_entry = proc_create(DRIVERLNAME, S_IRUGO, proc_net_eicon,
					&um_idi_proc_fops);
	if (!um_idi_proc_entry)
		return (0);
	return (1);
}

static void remove_um_idi_proc(void)
{
	if (um_idi_proc_entry) {
		remove_proc_entry(DRIVERLNAME, proc_net_eicon);
		um_idi_proc_entry = NULL;
	}
}

static const struct file_operations divas_idi_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = um_idi_read,
	.write   = um_idi_write,
	.poll    = um_idi_poll,
	.open    = um_idi_open,
	.release = um_idi_release
};

static void divas_idi_unregister_chrdev(void)
{
	unregister_chrdev(major, DEVNAME);
}

static int __init divas_idi_register_chrdev(void)
{
	if ((major = register_chrdev(0, DEVNAME, &divas_idi_fops)) < 0)
	{
		printk(KERN_ERR "%s: failed to create /dev entry.\n",
		       DRIVERLNAME);
		return (0);
	}

	return (1);
}

/*
** Driver Load
*/
static int __init divasi_init(void)
{
	char tmprev[50];
	int ret = 0;

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_IDI);
	strcpy(tmprev, main_revision);
	printk("%s  Build: %s\n", getrev(tmprev), DIVA_BUILD);

	if (!divas_idi_register_chrdev()) {
		ret = -EIO;
		goto out;
	}

	if (!create_um_idi_proc()) {
		divas_idi_unregister_chrdev();
		printk(KERN_ERR "%s: failed to create proc entry.\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}

	if (!(idifunc_init())) {
		remove_um_idi_proc();
		divas_idi_unregister_chrdev();
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}
	printk(KERN_INFO "%s: started with major %d\n", DRIVERLNAME, major);

out:
	return (ret);
}


/*
** Driver Unload
*/
static void __exit divasi_exit(void)
{
	idifunc_finit();
	remove_um_idi_proc();
	divas_idi_unregister_chrdev();

	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divasi_init);
module_exit(divasi_exit);


/*
 *  FILE OPERATIONS
 */

static int
divas_um_idi_copy_to_user(void *os_handle, void *dst, const void *src,
			  int length)
{
	memcpy(dst, src, length);
	return (length);
}

static ssize_t
um_idi_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	diva_um_idi_os_context_t *p_os;
	int ret = -EINVAL;
	void *data;

	if (!file->private_data) {
		return (-ENODEV);
	}

	if (!
	    (p_os =
	     (diva_um_idi_os_context_t *) diva_um_id_get_os_context(file->
								    private_data)))
	{
		return (-ENODEV);
	}
	if (p_os->aborted) {
		return (-ENODEV);
	}

	if (!(data = diva_os_malloc(0, count))) {
		return (-ENOMEM);
	}

	ret = diva_um_idi_read(file->private_data,
			       file, data, count,
			       divas_um_idi_copy_to_user);
	switch (ret) {
	case 0:		/* no message available */
		ret = (-EAGAIN);
		break;
	case (-1):		/* adapter was removed */
		ret = (-ENODEV);
		break;
	case (-2):		/* message_length > length of user buffer */
		ret = (-EFAULT);
		break;
	}

	if (ret > 0) {
		if (copy_to_user(buf, data, ret)) {
			ret = (-EFAULT);
		}
	}

	diva_os_free(0, data);
	DBG_TRC(("read: ret %d", ret));
	return (ret);
}


static int
divas_um_idi_copy_from_user(void *os_handle, void *dst, const void *src,
			    int length)
{
	memcpy(dst, src, length);
	return (length);
}

static int um_idi_open_adapter(struct file *file, int adapter_nr)
{
	diva_um_idi_os_context_t *p_os;
	void *e =
		divas_um_idi_create_entity((dword) adapter_nr, (void *) file);

	if (!(file->private_data = e)) {
		return (0);
	}
	p_os = (diva_um_idi_os_context_t *) diva_um_id_get_os_context(e);
	init_waitqueue_head(&p_os->read_wait);
	init_waitqueue_head(&p_os->close_wait);
	init_timer(&p_os->diva_timer_id);
	p_os->diva_timer_id.function = (void *) diva_um_timer_function;
	p_os->diva_timer_id.data = (unsigned long) p_os;
	p_os->aborted = 0;
	p_os->adapter_nr = adapter_nr;
	return (1);
}

static ssize_t
um_idi_write(struct file *file, const char __user *buf, size_t count,
	     loff_t *offset)
{
	diva_um_idi_os_context_t *p_os;
	int ret = -EINVAL;
	void *data;
	int adapter_nr = 0;

	if (!file->private_data) {
		/* the first write() selects the adapter_nr */
		if (count == sizeof(int)) {
			if (copy_from_user
			    ((void *) &adapter_nr, buf,
			     count)) return (-EFAULT);
			if (!(um_idi_open_adapter(file, adapter_nr)))
				return (-ENODEV);
			return (count);
		} else
			return (-ENODEV);
	}

	if (!(p_os =
	      (diva_um_idi_os_context_t *) diva_um_id_get_os_context(file->
								     private_data)))
	{
		return (-ENODEV);
	}
	if (p_os->aborted) {
		return (-ENODEV);
	}

	if (!(data = diva_os_malloc(0, count))) {
		return (-ENOMEM);
	}

	if (copy_from_user(data, buf, count)) {
		ret = -EFAULT;
	} else {
		ret = diva_um_idi_write(file->private_data,
					file, data, count,
					divas_um_idi_copy_from_user);
		switch (ret) {
		case 0:	/* no space available */
			ret = (-EAGAIN);
			break;
		case (-1):	/* adapter was removed */
			ret = (-ENODEV);
			break;
		case (-2):	/* length of user buffer > max message_length */
			ret = (-EFAULT);
			break;
		}
	}
	diva_os_free(0, data);
	DBG_TRC(("write: ret %d", ret));
	return (ret);
}

static unsigned int um_idi_poll(struct file *file, poll_table *wait)
{
	diva_um_idi_os_context_t *p_os;

	if (!file->private_data) {
		return (POLLERR);
	}

	if ((!(p_os =
	       (diva_um_idi_os_context_t *)
	       diva_um_id_get_os_context(file->private_data)))
	    || p_os->aborted) {
		return (POLLERR);
	}

	poll_wait(file, &p_os->read_wait, wait);

	if (p_os->aborted) {
		return (POLLERR);
	}

	switch (diva_user_mode_idi_ind_ready(file->private_data, file)) {
	case (-1):
		return (POLLERR);

	case 0:
		return (0);
	}

	return (POLLIN | POLLRDNORM);
}

static int um_idi_open(struct inode *inode, struct file *file)
{
	return (0);
}


static int um_idi_release(struct inode *inode, struct file *file)
{
	diva_um_idi_os_context_t *p_os;
	unsigned int adapter_nr;
	int ret = 0;

	if (!(file->private_data)) {
		ret = -ENODEV;
		goto out;
	}

	if (!(p_os =
	      (diva_um_idi_os_context_t *) diva_um_id_get_os_context(file->private_data))) {
		ret = -ENODEV;
		goto out;
	}

	adapter_nr = p_os->adapter_nr;

	if ((ret = remove_entity(file->private_data))) {
		goto out;
	}

	if (divas_um_idi_delete_entity
	    ((int) adapter_nr, file->private_data)) {
		ret = -ENODEV;
		goto out;
	}

out:
	return (ret);
}

int diva_os_get_context_size(void)
{
	return (sizeof(diva_um_idi_os_context_t));
}

void diva_os_wakeup_read(void *os_context)
{
	diva_um_idi_os_context_t *p_os =
		(diva_um_idi_os_context_t *) os_context;
	wake_up_interruptible(&p_os->read_wait);
}

void diva_os_wakeup_close(void *os_context)
{
	diva_um_idi_os_context_t *p_os =
		(diva_um_idi_os_context_t *) os_context;
	wake_up_interruptible(&p_os->close_wait);
}

static
void diva_um_timer_function(unsigned long data)
{
	diva_um_idi_os_context_t *p_os = (diva_um_idi_os_context_t *) data;

	p_os->aborted = 1;
	wake_up_interruptible(&p_os->read_wait);
	wake_up_interruptible(&p_os->close_wait);
	DBG_ERR(("entity removal watchdog"))
		}

/*
**  If application exits without entity removal this function will remove
**  entity and block until removal is complete
*/
static int remove_entity(void *entity)
{
	struct task_struct *curtask = current;
	diva_um_idi_os_context_t *p_os;

	diva_um_idi_stop_wdog(entity);

	if (!entity) {
		DBG_FTL(("Zero entity on remove"))
			return (0);
	}

	if (!(p_os =
	      (diva_um_idi_os_context_t *)
	      diva_um_id_get_os_context(entity))) {
		DBG_FTL(("Zero entity os context on remove"))
			return (0);
	}

	if (!divas_um_idi_entity_assigned(entity) || p_os->aborted) {
		/*
		  Entity is not assigned, also can be removed
		*/
		return (0);
	}

	DBG_TRC(("E(%08x) check remove", entity))

		/*
		  If adapter not answers on remove request inside of
		  10 Sec, then adapter is dead
		*/
		diva_um_idi_start_wdog(entity);

	{
		DECLARE_WAITQUEUE(wait, curtask);

		add_wait_queue(&p_os->close_wait, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!divas_um_idi_entity_start_remove(entity)
			    || p_os->aborted) {
				break;
			}
			schedule();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&p_os->close_wait, &wait);
	}

	DBG_TRC(("E(%08x) start remove", entity))
	{
		DECLARE_WAITQUEUE(wait, curtask);

		add_wait_queue(&p_os->close_wait, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!divas_um_idi_entity_assigned(entity)
			    || p_os->aborted) {
				break;
			}
			schedule();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&p_os->close_wait, &wait);
	}

	DBG_TRC(("E(%08x) remove complete, aborted:%d", entity,
		 p_os->aborted))

		diva_um_idi_stop_wdog(entity);

	p_os->aborted = 0;

	return (0);
}

/*
 * timer watchdog
 */
void diva_um_idi_start_wdog(void *entity)
{
	diva_um_idi_os_context_t *p_os;

	if (entity &&
	    ((p_os =
	      (diva_um_idi_os_context_t *)
	      diva_um_id_get_os_context(entity)))) {
		mod_timer(&p_os->diva_timer_id, jiffies + 10 * HZ);
	}
}

void diva_um_idi_stop_wdog(void *entity)
{
	diva_um_idi_os_context_t *p_os;

	if (entity &&
	    ((p_os =
	      (diva_um_idi_os_context_t *)
	      diva_um_id_get_os_context(entity)))) {
		del_timer(&p_os->diva_timer_id);
	}
}
