/* $Id: hysdn_procconf.c,v 1.8.6.4 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards, /proc/net filesystem dir and conf functions.
 *
 * written by Werner Cornelius (werner@titro.de) for Hypercope GmbH
 *
 * Copyright 1999  by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/cred.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <net/net_namespace.h>

#include "hysdn_defs.h"

static DEFINE_MUTEX(hysdn_conf_mutex);

#define INFO_OUT_LEN 80		/* length of info line including lf */

/********************************************************/
/* defines and data structure for conf write operations */
/********************************************************/
#define CONF_STATE_DETECT 0	/* waiting for detect */
#define CONF_STATE_CONF   1	/* writing config data */
#define CONF_STATE_POF    2	/* writing pof data */
#define CONF_LINE_LEN   255	/* 255 chars max */

struct conf_writedata {
	hysdn_card *card;	/* card the device is connected to */
	int buf_size;		/* actual number of bytes in the buffer */
	int needed_size;	/* needed size when reading pof */
	int state;		/* actual interface states from above constants */
	unsigned char conf_line[CONF_LINE_LEN];	/* buffered conf line */
	unsigned short channel;		/* active channel number */
	unsigned char *pof_buffer;	/* buffer when writing pof */
};

/***********************************************************************/
/* process_line parses one config line and transfers it to the card if */
/* necessary.                                                          */
/* if the return value is negative an error occurred.                   */
/***********************************************************************/
static int
process_line(struct conf_writedata *cnf)
{
	unsigned char *cp = cnf->conf_line;
	int i;

	if (cnf->card->debug_flags & LOG_CNF_LINE)
		hysdn_addlog(cnf->card, "conf line: %s", cp);

	if (*cp == '-') {	/* option */
		cp++;		/* point to option char */

		if (*cp++ != 'c')
			return (0);	/* option unknown or used */
		i = 0;		/* start value for channel */
		while ((*cp <= '9') && (*cp >= '0'))
			i = i * 10 + *cp++ - '0';	/* get decimal number */
		if (i > 65535) {
			if (cnf->card->debug_flags & LOG_CNF_MISC)
				hysdn_addlog(cnf->card, "conf channel invalid  %d", i);
			return (-ERR_INV_CHAN);		/* invalid channel */
		}
		cnf->channel = i & 0xFFFF;	/* set new channel number */
		return (0);	/* success */
	}			/* option */
	if (*cp == '*') {	/* line to send */
		if (cnf->card->debug_flags & LOG_CNF_DATA)
			hysdn_addlog(cnf->card, "conf chan=%d %s", cnf->channel, cp);
		return (hysdn_tx_cfgline(cnf->card, cnf->conf_line + 1,
					 cnf->channel));	/* send the line without * */
	}			/* line to send */
	return (0);
}				/* process_line */

/***********************************/
/* conf file operations and tables */
/***********************************/

/****************************************************/
/* write conf file -> boot or send cfg line to card */
/****************************************************/
static ssize_t
hysdn_conf_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
	struct conf_writedata *cnf;
	int i;
	unsigned char ch, *cp;

	if (!count)
		return (0);	/* nothing to handle */

	if (!(cnf = file->private_data))
		return (-EFAULT);	/* should never happen */

	if (cnf->state == CONF_STATE_DETECT) {	/* auto detect cnf or pof data */
		if (copy_from_user(&ch, buf, 1))	/* get first char for detect */
			return (-EFAULT);

		if (ch == 0x1A) {
			/* we detected a pof file */
			if ((cnf->needed_size = pof_write_open(cnf->card, &cnf->pof_buffer)) <= 0)
				return (cnf->needed_size);	/* an error occurred -> exit */
			cnf->buf_size = 0;	/* buffer is empty */
			cnf->state = CONF_STATE_POF;	/* new state */
		} else {
			/* conf data has been detected */
			cnf->buf_size = 0;	/* buffer is empty */
			cnf->state = CONF_STATE_CONF;	/* requested conf data write */
			if (cnf->card->state != CARD_STATE_RUN)
				return (-ERR_NOT_BOOTED);
			cnf->conf_line[CONF_LINE_LEN - 1] = 0;	/* limit string length */
			cnf->channel = 4098;	/* default channel for output */
		}
	}			/* state was auto detect */
	if (cnf->state == CONF_STATE_POF) {	/* pof write active */
		i = cnf->needed_size - cnf->buf_size;	/* bytes still missing for write */
		if (i <= 0)
			return (-EINVAL);	/* size error handling pof */

		if (i < count)
			count = i;	/* limit requested number of bytes */
		if (copy_from_user(cnf->pof_buffer + cnf->buf_size, buf, count))
			return (-EFAULT);	/* error while copying */
		cnf->buf_size += count;

		if (cnf->needed_size == cnf->buf_size) {
			cnf->needed_size = pof_write_buffer(cnf->card, cnf->buf_size);	/* write data */
			if (cnf->needed_size <= 0) {
				cnf->card->state = CARD_STATE_BOOTERR;	/* show boot error */
				return (cnf->needed_size);	/* an error occurred */
			}
			cnf->buf_size = 0;	/* buffer is empty again */
		}
	}
	/* pof write active */
	else {			/* conf write active */

		if (cnf->card->state != CARD_STATE_RUN) {
			if (cnf->card->debug_flags & LOG_CNF_MISC)
				hysdn_addlog(cnf->card, "cnf write denied -> not booted");
			return (-ERR_NOT_BOOTED);
		}
		i = (CONF_LINE_LEN - 1) - cnf->buf_size;	/* bytes available in buffer */
		if (i > 0) {
			/* copy remaining bytes into buffer */

			if (count > i)
				count = i;	/* limit transfer */
			if (copy_from_user(cnf->conf_line + cnf->buf_size, buf, count))
				return (-EFAULT);	/* error while copying */

			i = count;	/* number of chars in buffer */
			cp = cnf->conf_line + cnf->buf_size;
			while (i) {
				/* search for end of line */
				if ((*cp < ' ') && (*cp != 9))
					break;	/* end of line found */
				cp++;
				i--;
			}	/* search for end of line */

			if (i) {
				/* delimiter found */
				*cp++ = 0;	/* string termination */
				count -= (i - 1);	/* subtract remaining bytes from count */
				while ((i) && (*cp < ' ') && (*cp != 9)) {
					i--;	/* discard next char */
					count++;	/* mark as read */
					cp++;	/* next char */
				}
				cnf->buf_size = 0;	/* buffer is empty after transfer */
				if ((i = process_line(cnf)) < 0)	/* handle the line */
					count = i;	/* return the error */
			}
			/* delimiter found */
			else {
				cnf->buf_size += count;		/* add chars to string */
				if (cnf->buf_size >= CONF_LINE_LEN - 1) {
					if (cnf->card->debug_flags & LOG_CNF_MISC)
						hysdn_addlog(cnf->card, "cnf line too long %d chars pos %d", cnf->buf_size, count);
					return (-ERR_CONF_LONG);
				}
			}	/* not delimited */

		}
		/* copy remaining bytes into buffer */
		else {
			if (cnf->card->debug_flags & LOG_CNF_MISC)
				hysdn_addlog(cnf->card, "cnf line too long");
			return (-ERR_CONF_LONG);
		}
	}			/* conf write active */

	return (count);
}				/* hysdn_conf_write */

/*******************************************/
/* read conf file -> output card info data */
/*******************************************/
static ssize_t
hysdn_conf_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
	char *cp;

	if (!(file->f_mode & FMODE_READ))
		return -EPERM;	/* no permission to read */

	if (!(cp = file->private_data))
		return -EFAULT;	/* should never happen */

	return simple_read_from_buffer(buf, count, off, cp, strlen(cp));
}				/* hysdn_conf_read */

/******************/
/* open conf file */
/******************/
static int
hysdn_conf_open(struct inode *ino, struct file *filep)
{
	hysdn_card *card;
	struct conf_writedata *cnf;
	char *cp, *tmp;

	/* now search the addressed card */
	mutex_lock(&hysdn_conf_mutex);
	card = PDE_DATA(ino);
	if (card->debug_flags & (LOG_PROC_OPEN | LOG_PROC_ALL))
		hysdn_addlog(card, "config open for uid=%d gid=%d mode=0x%x",
			     filep->f_cred->fsuid, filep->f_cred->fsgid,
			     filep->f_mode);

	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> write boot file or conf line */

		if (!(cnf = kmalloc(sizeof(struct conf_writedata), GFP_KERNEL))) {
			mutex_unlock(&hysdn_conf_mutex);
			return (-EFAULT);
		}
		cnf->card = card;
		cnf->buf_size = 0;	/* nothing buffered */
		cnf->state = CONF_STATE_DETECT;		/* start auto detect */
		filep->private_data = cnf;

	} else if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ) {
		/* read access -> output card info data */

		if (!(tmp = kmalloc(INFO_OUT_LEN * 2 + 2, GFP_KERNEL))) {
			mutex_unlock(&hysdn_conf_mutex);
			return (-EFAULT);	/* out of memory */
		}
		filep->private_data = tmp;	/* start of string */

		/* first output a headline */
		sprintf(tmp, "id bus slot type irq iobase dp-mem     b-chans fax-chans state device");
		cp = tmp;	/* start of string */
		while (*cp)
			cp++;
		while (((cp - tmp) % (INFO_OUT_LEN + 1)) != INFO_OUT_LEN)
			*cp++ = ' ';
		*cp++ = '\n';

		/* and now the data */
		sprintf(cp, "%d  %3d %4d %4d %3d 0x%04x 0x%08lx %7d %9d %3d   %s",
			card->myid,
			card->bus,
			PCI_SLOT(card->devfn),
			card->brdtype,
			card->irq,
			card->iobase,
			card->membase,
			card->bchans,
			card->faxchans,
			card->state,
			hysdn_net_getname(card));
		while (*cp)
			cp++;
		while (((cp - tmp) % (INFO_OUT_LEN + 1)) != INFO_OUT_LEN)
			*cp++ = ' ';
		*cp++ = '\n';
		*cp = 0;	/* end of string */
	} else {		/* simultaneous read/write access forbidden ! */
		mutex_unlock(&hysdn_conf_mutex);
		return (-EPERM);	/* no permission this time */
	}
	mutex_unlock(&hysdn_conf_mutex);
	return nonseekable_open(ino, filep);
}				/* hysdn_conf_open */

/***************************/
/* close a config file.    */
/***************************/
static int
hysdn_conf_close(struct inode *ino, struct file *filep)
{
	hysdn_card *card;
	struct conf_writedata *cnf;
	int retval = 0;

	mutex_lock(&hysdn_conf_mutex);
	card = PDE_DATA(ino);
	if (card->debug_flags & (LOG_PROC_OPEN | LOG_PROC_ALL))
		hysdn_addlog(card, "config close for uid=%d gid=%d mode=0x%x",
			     filep->f_cred->fsuid, filep->f_cred->fsgid,
			     filep->f_mode);

	if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_WRITE) {
		/* write only access -> write boot file or conf line */
		if (filep->private_data) {
			cnf = filep->private_data;

			if (cnf->state == CONF_STATE_POF)
				retval = pof_write_close(cnf->card);	/* close the pof write */
			kfree(filep->private_data);	/* free allocated memory for buffer */

		}		/* handle write private data */
	} else if ((filep->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ) {
		/* read access -> output card info data */

		kfree(filep->private_data);	/* release memory */
	}
	mutex_unlock(&hysdn_conf_mutex);
	return (retval);
}				/* hysdn_conf_close */

/******************************************************/
/* table for conf filesystem functions defined above. */
/******************************************************/
static const struct file_operations conf_fops =
{
	.owner		= THIS_MODULE,
	.llseek         = no_llseek,
	.read           = hysdn_conf_read,
	.write          = hysdn_conf_write,
	.open           = hysdn_conf_open,
	.release        = hysdn_conf_close,
};

/*****************************/
/* hysdn subdir in /proc/net */
/*****************************/
struct proc_dir_entry *hysdn_proc_entry = NULL;

/*******************************************************************************/
/* hysdn_procconf_init is called when the module is loaded and after the cards */
/* have been detected. The needed proc dir and card config files are created.  */
/* The log init is called at last.                                             */
/*******************************************************************************/
int
hysdn_procconf_init(void)
{
	hysdn_card *card;
	unsigned char conf_name[20];

	hysdn_proc_entry = proc_mkdir(PROC_SUBDIR_NAME, init_net.proc_net);
	if (!hysdn_proc_entry) {
		printk(KERN_ERR "HYSDN: unable to create hysdn subdir\n");
		return (-1);
	}
	card = card_root;	/* point to first card */
	while (card) {

		sprintf(conf_name, "%s%d", PROC_CONF_BASENAME, card->myid);
		if ((card->procconf = (void *) proc_create_data(conf_name,
							   S_IFREG | S_IRUGO | S_IWUSR,
							   hysdn_proc_entry,
							   &conf_fops,
							   card)) != NULL) {
			hysdn_proclog_init(card);	/* init the log file entry */
		}
		card = card->next;	/* next entry */
	}

	printk(KERN_NOTICE "HYSDN: procfs initialised\n");
	return (0);
}				/* hysdn_procconf_init */

/*************************************************************************************/
/* hysdn_procconf_release is called when the module is unloaded and before the cards */
/* resources are released. The module counter is assumed to be 0 !                   */
/*************************************************************************************/
void
hysdn_procconf_release(void)
{
	hysdn_card *card;
	unsigned char conf_name[20];

	card = card_root;	/* start with first card */
	while (card) {

		sprintf(conf_name, "%s%d", PROC_CONF_BASENAME, card->myid);
		if (card->procconf)
			remove_proc_entry(conf_name, hysdn_proc_entry);

		hysdn_proclog_release(card);	/* init the log file entry */

		card = card->next;	/* point to next card */
	}

	remove_proc_entry(PROC_SUBDIR_NAME, init_net.proc_net);
}
