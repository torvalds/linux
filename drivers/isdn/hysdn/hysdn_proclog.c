/* $Id: hysdn_proclog.c,v 1.9.6.3 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards, /proc/net filesystem log functions.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>

#include "hysdn_defs.h"

/* the proc subdir for the interface is defined in the procconf module */
extern struct proc_dir_entry *hysdn_proc_entry;

/*************************************************/
/* structure keeping ascii log for device output */
/*************************************************/
struct log_data {
	struct log_data *next;
	ulong usage_cnt;	/* number of files still to work */
	void *proc_ctrl;	/* pointer to own control procdata structure */
	char log_start[2];	/* log string start (final len aligned by size) */
};

/**********************************************/
/* structure holding proc entrys for one card */
/**********************************************/
struct procdata {
	struct proc_dir_entry *log;	/* log entry */
	char log_name[15];	/* log filename */
	struct log_data *log_head, *log_tail;	/* head and tail for queue */
	int if_used;		/* open count for interface */
	int volatile del_lock;	/* lock for delete operations */
	uchar logtmp[LOG_MAX_LINELEN];
	wait_queue_head_t rd_queue;
};


/**********************************************/
/* log function for cards error log interface */
/**********************************************/
void
hysdn_card_errlog(hysdn_card * card, tErrLogEntry * logp, int maxsize)
{
	char buf[ERRLOG_TEXT_SIZE + 40];

	sprintf(buf, "LOG 0x%08lX 0x%08lX : %s\n", logp->ulErrType, logp->ulErrSubtype, logp->ucText);
	put_log_buffer(card, buf);	/* output the string */
}				/* hysdn_card_errlog */

/***************************************************/
/* Log function using format specifiers for output */
/***************************************************/
void
hysdn_addlog(hysdn_card * card, char *fmt,...)
{
	struct procdata *pd = card->proclog;
	char *cp;
	va_list args;

	if (!pd)
		return;		/* log structure non existent */

	cp = pd->logtmp;
	cp += sprintf(cp, "HYSDN: card %d ", card->myid);

	va_start(args, fmt);
	cp += vsprintf(cp, fmt, args);
	va_end(args);
	*cp++ = '\n';
	*cp = 0;

	if (card->debug_flags & DEB_OUT_SYSLOG)
		printk(KERN_INFO "%s", pd->logtmp);
	else
		put_log_buffer(card, pd->logtmp);

}				/* hysdn_addlog */

/********************************************/
/* put an log buffer into the log queue.    */
/* This buffer will be kept until all files */
/* opened for read got the contents.        */
/* Flushes buffers not longer in use.       */
/********************************************/
void
put_log_buffer(hysdn_card * card, char *cp)
{
	struct log_data *ib;
	struct procdata *pd = card->proclog;
	int i;
	unsigned long flags;

	if (!pd)
		return;
	if (!cp)
		return;
	if (!*cp)
		return;
	if (pd->if_used <= 0)
		return;		/* no open file for read */

	if (!(ib = (struct log_data *) kmalloc(sizeof(struct log_data) + strlen(cp), GFP_ATOMIC)))
		 return;	/* no memory */
	strcpy(ib->log_start, cp);	/* set output string */
	ib->next = NULL;
	ib->proc_ctrl = pd;	/* point to own control structure */
	save_flags(flags);
	cli();
	ib->usage_cnt = pd->if_used;
	if (!pd->log_head)
		pd->log_head = ib;	/* new head */
	else
		pd->log_tail->next = ib;	/* follows existing messages */
	pd->log_tail = ib;	/* new tail */
	i = pd->del_lock++;	/* get lock state */
	restore_flags(flags);

	/* delete old entrys */
	if (!i)
		while (pd->log_head->next) {
			if ((pd->log_head->usage_cnt <= 0) &&
			    (pd->log_head->next->usage_cnt <= 0)) {
				ib = pd->log_head;
				pd->log_head = pd->log_head->next;
				kfree(ib);
			} else
				break;
		}		/* pd->log_head->next */
	pd->del_lock--;		/* release lock level */
	wake_up_interruptible(&(pd->rd_queue));		/* announce new entry */
}				/* put_log_buffer */


/******************************/
/* file operations and tables */
/******************************/

/****************************************/
/* write log file -> set log level bits */
/****************************************/
static ssize_t
hysdn_log_write(struct file *file, const char __user *buf, size_t count, loff_t * off)
{
	ulong u = 0;
	int found = 0;
	uchar *cp, valbuf[128];
	long base = 10;
	hysdn_card *card = (hysdn_card *) file->private_data;

	if (count > (sizeof(valbuf) - 1))
		count = sizeof(valbuf) - 1;	/* limit length */
	if (copy_from_user(valbuf, buf, count))
		return (-EFAULT);	/* copy failed */

	valbuf[count] = 0;	/* terminating 0 */
	cp = valbuf;
	if ((count > 2) && (valbuf[0] == '0') && (valbuf[1] == 'x')) {
		cp += 2;	/* pointer after hex modifier */
		base = 16;
	}
	/* scan the input for debug flags */
	while (*cp) {
		if ((*cp >= '0') && (*cp <= '9')) {
			found = 1;
			u *= base;	/* adjust to next digit */
			u += *cp++ - '0';
			continue;
		}
		if (base != 16)
			break;	/* end of number */

		if ((*cp >= 'a') && (*cp <= 'f')) {
			found = 1;
			u *= base;	/* adjust to next digit */
			u += *cp++ - 'a' + 10;
			continue;
		}
		break;		/* terminated */
	}

	if (found) {
		card->debug_flags = u;	/* remember debug flags */
		hysdn_addlog(card, "debug set to 0x%lx", card->debug_flags);
	}
	return (count);
}				/* hysdn_log_write */

/******************/
/* read log file */
/******************/
static ssize_t
hysdn_log_read(struct file *file, char __user *buf, size_t count, loff_t * off)
{
	struct log_data *inf;
	int len;
	struct proc_dir_entry *pde = PDE(file->f_dentry->d_inode);
	struct procdata *pd = NULL;
	hysdn_card *card;

	if (!*((struct log_data **) file->private_data)) {
		if (file->f_flags & O_NONBLOCK)
			return (-EAGAIN);

		/* sorry, but we need to search the card */
		card = card_root;
		while (card) {
			pd = card->proclog;
			if (pd->log == pde)
				break;
			card = card->next;	/* search next entry */
		}
		if (card)
			interruptible_sleep_on(&(pd->rd_queue));
		else
			return (-EAGAIN);

	}
	if (!(inf = *((struct log_data **) file->private_data)))
		return (0);

	inf->usage_cnt--;	/* new usage count */
	file->private_data = &inf->next;	/* next structure */
	if ((len = strlen(inf->log_start)) <= count) {
		if (copy_to_user(buf, inf->log_start, len))
			return -EFAULT;
		*off += len;
		return (len);
	}
	return (0);
}				/* hysdn_log_read */

/******************/
/* open log file */
/******************/
static int
hysdn_log_open(struct inode *ino, struct file *filep)
{
	hysdn_card *card;
	struct procdata *pd = NULL;
	ulong flags;

	lock_kernel();
	card = card_root;
	while (card) {
		pd = card->proclog;
		if (pd->log == PDE(ino))
			break;
		card = card->next;	/* search next entry */
	}
	if (!card) {
		unlock_kernel();
		return (-ENODEV);	/* device is unknown/invalid */
	}
	filep->private_data = card;	/* remember our own card */

	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> write log level only */
	} else if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ) {

		/* read access -> log/debug read */
		save_flags(flags);
		cli();
		pd->if_used++;
		if (pd->log_head)
			filep->private_data = &pd->log_tail->next;
		else
			filep->private_data = &pd->log_head;
		restore_flags(flags);
	} else {		/* simultaneous read/write access forbidden ! */
		unlock_kernel();
		return (-EPERM);	/* no permission this time */
	}
	unlock_kernel();
	return nonseekable_open(ino, filep);
}				/* hysdn_log_open */

/*******************************************************************************/
/* close a cardlog file. If the file has been opened for exclusive write it is */
/* assumed as pof data input and the pof loader is noticed about.              */
/* Otherwise file is handled as log output. In this case the interface usage   */
/* count is decremented and all buffers are noticed of closing. If this file   */
/* was the last one to be closed, all buffers are freed.                       */
/*******************************************************************************/
static int
hysdn_log_close(struct inode *ino, struct file *filep)
{
	struct log_data *inf;
	struct procdata *pd;
	hysdn_card *card;
	int retval = 0;
	unsigned long flags;


	lock_kernel();
	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> write debug level written */
		retval = 0;	/* success */
	} else {
		/* read access -> log/debug read, mark one further file as closed */

		pd = NULL;
		save_flags(flags);
		cli();
		inf = *((struct log_data **) filep->private_data);	/* get first log entry */
		if (inf)
			pd = (struct procdata *) inf->proc_ctrl;	/* still entries there */
		else {
			/* no info available -> search card */
			card = card_root;
			while (card) {
				pd = card->proclog;
				if (pd->log == PDE(ino))
					break;
				card = card->next;	/* search next entry */
			}
			if (card)
				pd = card->proclog;	/* pointer to procfs log */
		}
		if (pd)
			pd->if_used--;	/* decrement interface usage count by one */

		while (inf) {
			inf->usage_cnt--;	/* decrement usage count for buffers */
			inf = inf->next;
		}
		restore_flags(flags);

		if (pd)
			if (pd->if_used <= 0)	/* delete buffers if last file closed */
				while (pd->log_head) {
					inf = pd->log_head;
					pd->log_head = pd->log_head->next;
					kfree(inf);
				}
	}			/* read access */
	unlock_kernel();

	return (retval);
}				/* hysdn_log_close */

/*************************************************/
/* select/poll routine to be able using select() */
/*************************************************/
static unsigned int
hysdn_log_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;
	struct proc_dir_entry *pde = PDE(file->f_dentry->d_inode);
	hysdn_card *card;
	struct procdata *pd = NULL;

	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE)
		return (mask);	/* no polling for write supported */

	/* we need to search the card */
	card = card_root;
	while (card) {
		pd = card->proclog;
		if (pd->log == pde)
			break;
		card = card->next;	/* search next entry */
	}
	if (!card)
		return (mask);	/* card not found */

	poll_wait(file, &(pd->rd_queue), wait);

	if (*((struct log_data **) file->private_data))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}				/* hysdn_log_poll */

/**************************************************/
/* table for log filesystem functions defined above. */
/**************************************************/
static struct file_operations log_fops =
{
	.llseek         = no_llseek,
	.read           = hysdn_log_read,
	.write          = hysdn_log_write,
	.poll           = hysdn_log_poll,
	.open           = hysdn_log_open,
	.release        = hysdn_log_close,                                        
};


/***********************************************************************************/
/* hysdn_proclog_init is called when the module is loaded after creating the cards */
/* conf files.                                                                     */
/***********************************************************************************/
int
hysdn_proclog_init(hysdn_card * card)
{
	struct procdata *pd;

	/* create a cardlog proc entry */

	if ((pd = (struct procdata *) kmalloc(sizeof(struct procdata), GFP_KERNEL)) != NULL) {
		memset(pd, 0, sizeof(struct procdata));
		sprintf(pd->log_name, "%s%d", PROC_LOG_BASENAME, card->myid);
		if ((pd->log = create_proc_entry(pd->log_name, S_IFREG | S_IRUGO | S_IWUSR, hysdn_proc_entry)) != NULL) {
		        pd->log->proc_fops = &log_fops; 
		        pd->log->owner = THIS_MODULE;
		}

		init_waitqueue_head(&(pd->rd_queue));

		card->proclog = (void *) pd;	/* remember procfs structure */
	}
	return (0);
}				/* hysdn_proclog_init */

/************************************************************************************/
/* hysdn_proclog_release is called when the module is unloaded and before the cards */
/* conf file is released                                                            */
/* The module counter is assumed to be 0 !                                          */
/************************************************************************************/
void
hysdn_proclog_release(hysdn_card * card)
{
	struct procdata *pd;

	if ((pd = (struct procdata *) card->proclog) != NULL) {
		if (pd->log)
			remove_proc_entry(pd->log_name, hysdn_proc_entry);
		kfree(pd);	/* release memory */
		card->proclog = NULL;
	}
}				/* hysdn_proclog_release */
