/* $Id: divert_procfs.c,v 1.11.6.2 2001/09/23 22:24:36 kai Exp $
 *
 * Filesystem handling for the diversion supplementary services.
 *
 * Copyright 1998       by Werner Cornelius (werner@isdn4linux.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#else
#include <linux/fs.h>
#endif
#include <linux/sched.h>
#include <linux/isdnif.h>
#include <net/net_namespace.h>
#include <linux/mutex.h>
#include "isdn_divert.h"


/*********************************/
/* Variables for interface queue */
/*********************************/
ulong if_used = 0;		/* number of interface users */
static DEFINE_MUTEX(isdn_divert_mutex);
static struct divert_info *divert_info_head = NULL;	/* head of queue */
static struct divert_info *divert_info_tail = NULL;	/* pointer to last entry */
static DEFINE_SPINLOCK(divert_info_lock);/* lock for queue */
static wait_queue_head_t rd_queue;

/*********************************/
/* put an info buffer into queue */
/*********************************/
void
put_info_buffer(char *cp)
{
	struct divert_info *ib;
	unsigned long flags;

	if (if_used <= 0)
		return;
	if (!cp)
		return;
	if (!*cp)
		return;
	if (!(ib = kmalloc(sizeof(struct divert_info) + strlen(cp), GFP_ATOMIC)))
		 return;	/* no memory */
	strcpy(ib->info_start, cp);	/* set output string */
	ib->next = NULL;
	spin_lock_irqsave( &divert_info_lock, flags );
	ib->usage_cnt = if_used;
	if (!divert_info_head)
		divert_info_head = ib;	/* new head */
	else
		divert_info_tail->next = ib;	/* follows existing messages */
	divert_info_tail = ib;	/* new tail */

	/* delete old entrys */
	while (divert_info_head->next) {
		if ((divert_info_head->usage_cnt <= 0) &&
		    (divert_info_head->next->usage_cnt <= 0)) {
			ib = divert_info_head;
			divert_info_head = divert_info_head->next;
			kfree(ib);
		} else
			break;
	}			/* divert_info_head->next */
	spin_unlock_irqrestore( &divert_info_lock, flags );
	wake_up_interruptible(&(rd_queue));
}				/* put_info_buffer */

#ifdef CONFIG_PROC_FS

/**********************************/
/* deflection device read routine */
/**********************************/
static ssize_t
isdn_divert_read(struct file *file, char __user *buf, size_t count, loff_t * off)
{
	struct divert_info *inf;
	int len;

	if (!*((struct divert_info **) file->private_data)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		interruptible_sleep_on(&(rd_queue));
	}
	if (!(inf = *((struct divert_info **) file->private_data)))
		return (0);

	inf->usage_cnt--;	/* new usage count */
	file->private_data = &inf->next;	/* next structure */
	if ((len = strlen(inf->info_start)) <= count) {
		if (copy_to_user(buf, inf->info_start, len))
			return -EFAULT;
		*off += len;
		return (len);
	}
	return (0);
}				/* isdn_divert_read */

/**********************************/
/* deflection device write routine */
/**********************************/
static ssize_t
isdn_divert_write(struct file *file, const char __user *buf, size_t count, loff_t * off)
{
	return (-ENODEV);
}				/* isdn_divert_write */


/***************************************/
/* select routines for various kernels */
/***************************************/
static unsigned int
isdn_divert_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	poll_wait(file, &(rd_queue), wait);
	/* mask = POLLOUT | POLLWRNORM; */
	if (*((struct divert_info **) file->private_data)) {
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}				/* isdn_divert_poll */

/****************/
/* Open routine */
/****************/
static int
isdn_divert_open(struct inode *ino, struct file *filep)
{
	unsigned long flags;

	spin_lock_irqsave( &divert_info_lock, flags );
 	if_used++;
	if (divert_info_head)
		filep->private_data = &(divert_info_tail->next);
	else
		filep->private_data = &divert_info_head;
	spin_unlock_irqrestore( &divert_info_lock, flags );
	/*  start_divert(); */
	return nonseekable_open(ino, filep);
}				/* isdn_divert_open */

/*******************/
/* close routine   */
/*******************/
static int
isdn_divert_close(struct inode *ino, struct file *filep)
{
	struct divert_info *inf;
	unsigned long flags;

	spin_lock_irqsave( &divert_info_lock, flags );
	if_used--;
	inf = *((struct divert_info **) filep->private_data);
	while (inf) {
		inf->usage_cnt--;
		inf = inf->next;
	}
	if (if_used <= 0)
		while (divert_info_head) {
			inf = divert_info_head;
			divert_info_head = divert_info_head->next;
			kfree(inf);
		}
	spin_unlock_irqrestore( &divert_info_lock, flags );
	return (0);
}				/* isdn_divert_close */

/*********/
/* IOCTL */
/*********/
static int isdn_divert_ioctl_unlocked(struct file *file, uint cmd, ulong arg)
{
	divert_ioctl dioctl;
	int i;
	unsigned long flags;
	divert_rule *rulep;
	char *cp;

	if (copy_from_user(&dioctl, (void __user *) arg, sizeof(dioctl)))
		return -EFAULT;

	switch (cmd) {
		case IIOCGETVER:
			dioctl.drv_version = DIVERT_IIOC_VERSION;	/* set version */
			break;

		case IIOCGETDRV:
			if ((dioctl.getid.drvid = divert_if.name_to_drv(dioctl.getid.drvnam)) < 0)
				return (-EINVAL);
			break;

		case IIOCGETNAM:
			cp = divert_if.drv_to_name(dioctl.getid.drvid);
			if (!cp)
				return (-EINVAL);
			if (!*cp)
				return (-EINVAL);
			strcpy(dioctl.getid.drvnam, cp);
			break;

		case IIOCGETRULE:
			if (!(rulep = getruleptr(dioctl.getsetrule.ruleidx)))
				return (-EINVAL);
			dioctl.getsetrule.rule = *rulep;	/* copy data */
			break;

		case IIOCMODRULE:
			if (!(rulep = getruleptr(dioctl.getsetrule.ruleidx)))
				return (-EINVAL);
            spin_lock_irqsave(&divert_lock, flags);
			*rulep = dioctl.getsetrule.rule;	/* copy data */
			spin_unlock_irqrestore(&divert_lock, flags);
			return (0);	/* no copy required */
			break;

		case IIOCINSRULE:
			return (insertrule(dioctl.getsetrule.ruleidx, &dioctl.getsetrule.rule));
			break;

		case IIOCDELRULE:
			return (deleterule(dioctl.getsetrule.ruleidx));
			break;

		case IIOCDODFACT:
			return (deflect_extern_action(dioctl.fwd_ctrl.subcmd,
						  dioctl.fwd_ctrl.callid,
						 dioctl.fwd_ctrl.to_nr));

		case IIOCDOCFACT:
		case IIOCDOCFDIS:
		case IIOCDOCFINT:
			if (!divert_if.drv_to_name(dioctl.cf_ctrl.drvid))
				return (-EINVAL);	/* invalid driver */
			if (strnlen(dioctl.cf_ctrl.msn, sizeof(dioctl.cf_ctrl.msn)) ==
					sizeof(dioctl.cf_ctrl.msn))
				return -EINVAL;
			if (strnlen(dioctl.cf_ctrl.fwd_nr, sizeof(dioctl.cf_ctrl.fwd_nr)) ==
					sizeof(dioctl.cf_ctrl.fwd_nr))
				return -EINVAL;
			if ((i = cf_command(dioctl.cf_ctrl.drvid,
					    (cmd == IIOCDOCFACT) ? 1 : (cmd == IIOCDOCFDIS) ? 0 : 2,
					    dioctl.cf_ctrl.cfproc,
					    dioctl.cf_ctrl.msn,
					    dioctl.cf_ctrl.service,
					    dioctl.cf_ctrl.fwd_nr,
					    &dioctl.cf_ctrl.procid)))
				return (i);
			break;

		default:
			return (-EINVAL);
	}			/* switch cmd */
	return copy_to_user((void __user *)arg, &dioctl, sizeof(dioctl)) ? -EFAULT : 0;
}				/* isdn_divert_ioctl */

static long isdn_divert_ioctl(struct file *file, uint cmd, ulong arg)
{
	long ret;

	mutex_lock(&isdn_divert_mutex);
	ret = isdn_divert_ioctl_unlocked(file, cmd, arg);
	mutex_unlock(&isdn_divert_mutex);

	return ret;
}

static const struct file_operations isdn_fops =
{
	.owner          = THIS_MODULE,
	.llseek         = no_llseek,
	.read           = isdn_divert_read,
	.write          = isdn_divert_write,
	.poll           = isdn_divert_poll,
	.unlocked_ioctl = isdn_divert_ioctl,
	.open           = isdn_divert_open,
	.release        = isdn_divert_close,                                      
};

/****************************/
/* isdn subdir in /proc/net */
/****************************/
static struct proc_dir_entry *isdn_proc_entry = NULL;
static struct proc_dir_entry *isdn_divert_entry = NULL;
#endif	/* CONFIG_PROC_FS */

/***************************************************************************/
/* divert_dev_init must be called before the proc filesystem may be used   */
/***************************************************************************/
int
divert_dev_init(void)
{

	init_waitqueue_head(&rd_queue);

#ifdef CONFIG_PROC_FS
	isdn_proc_entry = proc_mkdir("isdn", init_net.proc_net);
	if (!isdn_proc_entry)
		return (-1);
	isdn_divert_entry = proc_create("divert", S_IFREG | S_IRUGO,
					isdn_proc_entry, &isdn_fops);
	if (!isdn_divert_entry) {
		remove_proc_entry("isdn", init_net.proc_net);
		return (-1);
	}
#endif	/* CONFIG_PROC_FS */

	return (0);
}				/* divert_dev_init */

/***************************************************************************/
/* divert_dev_deinit must be called before leaving isdn when included as   */
/* a module.                                                               */
/***************************************************************************/
int
divert_dev_deinit(void)
{

#ifdef CONFIG_PROC_FS
	remove_proc_entry("divert", isdn_proc_entry);
	remove_proc_entry("isdn", init_net.proc_net);
#endif	/* CONFIG_PROC_FS */

	return (0);
}				/* divert_dev_deinit */
