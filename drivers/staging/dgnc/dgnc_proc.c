/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE!
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com.
 *	Thank you.
 *
 *
 * $Id: dgnc_proc.c,v 1.3 2011/06/22 12:16:35 markh Exp $
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <linux/serial_reg.h>
#include <linux/string.h>
#include <asm/uaccess.h>		/* For copy_from_user/copy_to_user */

#include "dgnc_driver.h"
#include "dgnc_proc.h"
#include "dgnc_mgmt.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
#define init_MUTEX(sem)         sema_init(sem, 1)
#define DECLARE_MUTEX(name)     \
        struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)
#endif


/* The /proc/dgnc directory */
static struct proc_dir_entry *ProcDGNC;


/* File operation declarations */
static int	dgnc_gen_proc_open(struct inode *, struct file *);
static int	dgnc_gen_proc_close(struct inode *, struct file *);
static ssize_t	dgnc_gen_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static ssize_t	dgnc_gen_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

static int dgnc_proc_chk_perm(struct inode *, int);

static const struct file_operations dgnc_proc_file_ops =
{
	.owner =	THIS_MODULE,
	.read =		dgnc_gen_proc_read,	/* read		*/
	.write =	dgnc_gen_proc_write,	/* write	*/
	.open =		dgnc_gen_proc_open,	/* open		*/
	.release =	dgnc_gen_proc_close,	/* release	*/
};


static struct inode_operations dgnc_proc_inode_ops =
{
	.permission =	dgnc_proc_chk_perm
};


static void dgnc_register_proc_table(struct dgnc_proc_entry *, struct proc_dir_entry *);
static void dgnc_unregister_proc_table(struct dgnc_proc_entry *, struct proc_dir_entry *);
static void dgnc_remove_proc_entry(struct proc_dir_entry *pde);


/* Stuff in /proc/ */
static int dgnc_read_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_write_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				const char __user *buffer, ssize_t *lenp, loff_t *ppos);

static int dgnc_read_mknod(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);

static struct dgnc_proc_entry dgnc_table[] = {
	{DGNC_INFO,	"info", 0600, NULL, NULL, NULL, &dgnc_read_info, &dgnc_write_info,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_table[0].excl_sem, 1), 0, NULL },
	{DGNC_MKNOD, "mknod", 0600, NULL, NULL, NULL, &dgnc_read_mknod, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_table[1].excl_sem, 1), 0, NULL },
	{0}
};


/* Stuff in /proc/<board>/ */
static int dgnc_read_board_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_board_vpd(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_board_mknod(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_board_ttystats(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_board_ttyintr(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_board_ttyflags(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);

static struct dgnc_proc_entry dgnc_board_table[] = {
	{DGNC_BOARD_INFO, "info", 0600, NULL, NULL, NULL, &dgnc_read_board_info, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[0].excl_sem, 1), 0, NULL },
	{DGNC_BOARD_VPD, "vpd", 0600, NULL, NULL, NULL, &dgnc_read_board_vpd, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[1].excl_sem, 1), 0, NULL },
	{DGNC_BOARD_TTYSTATS, "stats", 0600, NULL, NULL, NULL, &dgnc_read_board_ttystats, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[2].excl_sem, 1), 0, NULL },
	{DGNC_BOARD_TTYINTR, "intr", 0600, NULL, NULL, NULL, &dgnc_read_board_ttyintr, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[3].excl_sem, 1), 0, NULL },
	{DGNC_BOARD_TTYFLAGS, "flags", 0600, NULL, NULL, NULL, &dgnc_read_board_ttyflags, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[4].excl_sem, 1), 0, NULL },
	{DGNC_BOARD_MKNOD, "mknod", 0600, NULL, NULL, NULL, &dgnc_read_board_mknod, NULL,
			NULL, __SEMAPHORE_INITIALIZER(dgnc_board_table[5].excl_sem, 1), 0, NULL },
	{0}
};


/* Stuff in /proc/<board>/<channel> */
static int dgnc_read_channel_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_open_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				void *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_close_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				void *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user  *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_channel_custom_ttyname(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);
static int dgnc_read_channel_custom_prname(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos);

static struct dgnc_proc_entry dgnc_channel_table[] = {
	{DGNC_PORT_INFO, "info", 0600, NULL, NULL, NULL, &dgnc_read_channel_info, NULL,
		NULL, __SEMAPHORE_INITIALIZER(dgnc_channel_table[0].excl_sem, 1), 0, NULL },
	{DGNC_PORT_SNIFF, "sniff", 0600, NULL, &dgnc_open_channel_sniff, &dgnc_close_channel_sniff, &dgnc_read_channel_sniff, NULL,
		NULL, __SEMAPHORE_INITIALIZER(dgnc_channel_table[1].excl_sem, 1), 0, NULL},
        {DGNC_PORT_CUSTOM_TTYNAME, "ttyname", 0600, NULL, NULL, NULL, &dgnc_read_channel_custom_ttyname, NULL,
		NULL, __SEMAPHORE_INITIALIZER(dgnc_channel_table[2].excl_sem, 1), 0, NULL },
        {DGNC_PORT_CUSTOM_PRNAME, "prname", 0600, NULL, NULL, NULL, &dgnc_read_channel_custom_prname, NULL,
		NULL, __SEMAPHORE_INITIALIZER(dgnc_channel_table[3].excl_sem, 1), 0, NULL },
	{0}
};


/*
 * dgnc_test_perm does NOT grant the superuser all rights automatically, because
 * some entries are readonly even to root.
 */
static inline int dgnc_test_perm(int mode, int op)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	if (!current->euid)
#else
	if (!current_euid())
#endif
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((mode & op & 0007) == op)
		return 0;
	if (capable(CAP_SYS_ADMIN))
		return 0;
	return -EACCES;
}


/*
 * /proc/sys support
 */
static inline int dgnc_proc_match(int len, const char *name, struct proc_dir_entry *de)
{
	if (!de || !de->low_ino)
		return 0;
	if (de->namelen != len)  
		return 0;
	return !memcmp(name, de->name, len);
}


/*
 *  Scan the entries in table and add them all to /proc at the position
 *  referred to by "root"
 */
static void dgnc_register_proc_table(struct dgnc_proc_entry *table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int len;
	mode_t mode;

	for (; table->magic; table++) {
		/* Can't do anything without a proc name. */
		if (!table->name) {
			DPR_PROC(("dgnc_register_proc_table, no name...\n"));
			continue;
		}

		/* Maybe we can't do anything with it... */
		if (!table->read_handler && !table->write_handler && !table->child) {
			DPR_PROC((KERN_WARNING "DGNC PROC: Can't register %s\n", table->name));
			continue;
		}

		len = strlen(table->name);
		mode = table->mode;
		de = NULL;

		if (!table->child) {
			mode |= S_IFREG;
		} else {
			mode |= S_IFDIR;
			for (de = root->subdir; de; de = de->next) {
				if (dgnc_proc_match(len, table->name, de))
					break;
			}

			/* If the subdir exists already, de is non-NULL */
		}

		if (!de) {
			de = create_proc_entry(table->name, mode, root);
			if (!de)
				continue;
			de->data = (void *) table;
			if (!table->child) {
				de->proc_iops = &dgnc_proc_inode_ops;
				de->proc_fops = &dgnc_proc_file_ops;		
			}
		}

		table->de = de;

		if (de->mode & S_IFDIR)
			dgnc_register_proc_table(table->child, de);

	}
}



/*
 * Unregister a /proc sysctl table and any subdirectories.
 */
static void dgnc_unregister_proc_table(struct dgnc_proc_entry *table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;

	for (; table->magic; table++) {
		if (!(de = table->de))
			continue;

		if (de->mode & S_IFDIR) {
			if (!table->child) {
				DPR_PROC((KERN_ALERT "Help - malformed sysctl tree on free\n"));
				continue;
			}

			/* recurse down into subdirectory... */
			DPR_PROC(("Recursing down a directory...\n"));
			dgnc_unregister_proc_table(table->child, de);

			/* Don't unregister directories which still have entries.. */
			if (de->subdir)
				continue;
		}   

		/* Don't unregister proc entries that are still being used.. */
		if ((atomic_read(&de->count)) != 1) {
			DPR_PROC(("proc entry in use... Not removing...\n"));
			continue;
		}

		dgnc_remove_proc_entry(de);
		table->de = NULL;
	}
}



static int dgnc_gen_proc_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de;
	struct dgnc_proc_entry *entry;
	int (*handler) (struct dgnc_proc_entry *table, int dir, struct file *filp,
		void *buffer, ssize_t *lenp, loff_t *ppos);
	int ret = 0, error = 0;

	de = (struct proc_dir_entry *) PDE(file->f_dentry->d_inode);
	if (!de || !de->data) {
		ret = -ENXIO;
		goto done;
	}

	entry = (struct dgnc_proc_entry *) de->data;
	if (!entry) {
		ret = -ENXIO;
		goto done;
	}

	down(&entry->excl_sem);

	if (entry->excl_cnt) {
		ret = -EBUSY;
	} else {
		entry->excl_cnt++;

		handler = entry->open_handler;
		if (handler) {
			error = (*handler) (entry, OUTBOUND, file, NULL, NULL, NULL);
			if (error) {
				entry->excl_cnt--;
				ret = error;
			}
		}
	}

	up(&entry->excl_sem);

done:

	return ret;
}


static int dgnc_gen_proc_close(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de;
	int (*handler) (struct dgnc_proc_entry *table, int dir, struct file *filp,
		void *buffer, ssize_t *lenp, loff_t *ppos);
	struct dgnc_proc_entry *entry;
	int error = 0;

	de = (struct proc_dir_entry *) PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		goto done;

	entry = (struct dgnc_proc_entry *) de->data;
	if (!entry)
		goto done;

	down(&entry->excl_sem);

	if (entry->excl_cnt)
		entry->excl_cnt = 0;


	handler = entry->close_handler;
	if (handler) {
		error = (*handler) (entry, OUTBOUND, file, NULL, NULL, NULL);
	}

	up(&entry->excl_sem);

done:
	return 0;
}


static ssize_t dgnc_gen_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *de;
	struct dgnc_proc_entry *entry;
	int (*handler) (struct dgnc_proc_entry *table, int dir, struct file *filp,
		char __user *buffer, ssize_t *lenp, loff_t *ppos2);
	ssize_t res;
	ssize_t error;

	de = (struct proc_dir_entry*) PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		return -ENXIO; 

	entry = (struct dgnc_proc_entry *) de->data;
	if (!entry)
		return -ENXIO;

	/* Test for read permission */
	if (dgnc_test_perm(entry->mode, 4))
		return -EPERM;

	res = count;

	handler = entry->read_handler;
	if (!handler)
		return -ENXIO;

	error = (*handler) (entry, OUTBOUND, file, buf, &res, ppos);
	if (error)
		return error;

	return res;
}


static ssize_t	dgnc_gen_proc_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct proc_dir_entry *de;
	struct dgnc_proc_entry *entry;
	int (*handler) (struct dgnc_proc_entry *table, int dir, struct file *filp,
		const char __user *buffer, ssize_t *lenp, loff_t *ppos2);
	ssize_t res;   
	ssize_t error;

	de = (struct proc_dir_entry *) PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		return -ENXIO;

	entry = (struct dgnc_proc_entry *) de->data;
	if (!entry)
		return -ENXIO;

	/* Test for write permission */
	if (dgnc_test_perm(entry->mode, 2))
		return -EPERM;

	res = count;

	handler = entry->write_handler;
	if (!handler)
		return -ENXIO;

	error = (*handler) (entry, INBOUND, file, buf, &res, ppos);
	if (error)
		return error; 

	return res;
}


static int dgnc_proc_chk_perm(struct inode *inode, int op)
{               
	return dgnc_test_perm(inode->i_mode, op);
}


/*               
 *  Return what is (hopefully) useful information about the
 *  driver. 
 */
static int dgnc_read_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	static int done = 0;
	static char buf[4096];
	char *p = buf;

	DPR_PROC(("dgnc_proc_info\n"));

	if (done) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	p += sprintf(p, "Driver:\t\t%s\n", DG_NAME);
	p += sprintf(p, "\n");
	p += sprintf(p, "Debug:\t\t0x%x\n", dgnc_debug);
	p += sprintf(p, "Sysfs Support:\t0x1\n");
	p += sprintf(p, "Rawreadok:\t0x%x\n", dgnc_rawreadok);
	p += sprintf(p, "Max Boards:\t%d\n", MAXBOARDS);
	p += sprintf(p, "Total Boards:\t%d\n", dgnc_NumBoards);
	p += sprintf(p, "Poll rate:\t%dms\n", dgnc_poll_tick);
	p += sprintf(p, "Poll counter:\t%ld\n", dgnc_poll_counter);
	p += sprintf(p, "State:\t\t%s\n", dgnc_driver_state_text[dgnc_driver_state]);

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  When writing to the "info" entry point, I actually allow one
 *  to modify certain variables.  This may be a sleazy overload
 *  of this /proc entry, but I don't want:
 *
 *     a. to clutter /proc more than I have to
 *     b. to overload the "config" entry, which would be somewhat
 *        more natural
 *     c. necessarily advertise the fact this ability exists
 *
 *  The continued support of this feature has not yet been
 *  guaranteed.
 *
 *  Writing operates on a "state machine" principle.
 *
 *  State 0: waiting for a symbol to start.  Waiting for anything
 *           which isn't " ' = or whitespace.
 *  State 1: reading a symbol.  If the character is a space, move
 *           to state 2.  If =, move to state 3.  If " or ', move
 *           to state 0.
 *  State 2: Waiting for =... suck whitespace.  If anything other
 *           than whitespace, drop to state 0.
 *  State 3: Got =.  Suck whitespace waiting for value to start.
 *           If " or ', go to state 4 (and remember which quote it
 *           was).  Otherwise, go to state 5.
 *  State 4: Reading value, within quotes.  Everything is added to
 *           value up until the matching quote.  When you hit the
 *           matching quote, try to set the variable, then state 0.
 *  State 5: Reading value, outside quotes.  Everything not " ' =
 *           or whitespace goes in value.  Hitting one of the
 *           terminators tosses us back to state 0 after trying to
 *           set the variable.
 */
typedef enum {
	INFO_NONE, INFO_INT, INFO_CHAR, INFO_SHORT,
	INFO_LONG, INFO_PTR, INFO_STRING, INFO_END
} info_proc_var_val;

static struct {
	char              *name;
	info_proc_var_val  type;
	int                rw;       /* 0=readonly */
	void              *val_ptr;
} dgnc_info_vars[] = {
	{ "rawreadok",   INFO_INT,    1, (void *) &dgnc_rawreadok },
        { "pollrate",    INFO_INT,    1, (void *) &dgnc_poll_tick },
	{ NULL, INFO_NONE, 0, NULL },
	{ "debug",   INFO_LONG,   1, (void *) &dgnc_debug },
	{ NULL, INFO_END, 0, NULL }
};

static void dgnc_set_info_var(char *name, char *val)
{
	int i;
	unsigned long newval;
	unsigned char charval;
	unsigned short shortval;
	unsigned int intval;

	for (i = 0; dgnc_info_vars[i].type != INFO_END; i++) {
		if (dgnc_info_vars[i].name)
			if (!strcmp(name, dgnc_info_vars[i].name))
				break;
	}

	if (dgnc_info_vars[i].type == INFO_END)
		return;
	if (dgnc_info_vars[i].rw == 0)
		return;
	if (dgnc_info_vars[i].val_ptr == NULL)
		return;

	newval = simple_strtoul(val, NULL, 0 ); 

	switch (dgnc_info_vars[i].type) {
	case INFO_CHAR:
		charval = newval & 0xff;
		APR(("Modifying %s (%lx) <= 0x%02x  (%d)\n",
		           name, (long)(dgnc_info_vars[i].val_ptr ),
		           charval, charval));
		*(uchar *)(dgnc_info_vars[i].val_ptr) = charval;
		break;
	case INFO_SHORT:
		shortval = newval & 0xffff;
		APR(("Modifying %s (%lx) <= 0x%04x  (%d)\n",
		           name, (long)(dgnc_info_vars[i].val_ptr),
		           shortval, shortval));
		*(ushort *)(dgnc_info_vars[i].val_ptr) = shortval;
		break;
	case INFO_INT:
		intval = newval & 0xffffffff;
		APR(("Modifying %s (%lx) <= 0x%08x  (%d)\n",
		           name, (long)(dgnc_info_vars[i].val_ptr),
		           intval, intval));
		*(uint *)(dgnc_info_vars[i].val_ptr) = intval;
		break;
	case INFO_LONG:
		APR(("Modifying %s (%lx) <= 0x%lx  (%ld)\n",
		           name, (long)(dgnc_info_vars[i].val_ptr),
		           newval, newval));
		*(ulong *)(dgnc_info_vars[i].val_ptr) = newval;
		break;
	case INFO_PTR:
	case INFO_STRING:
	case INFO_END:
	case INFO_NONE:
	default:
		break;
	}
}

static int dgnc_write_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				const char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	static int state = 0;
	#define MAXSYM 255
	static int sympos, valpos;
	static char sym[MAXSYM + 1];
	static char val[MAXSYM + 1];
	static int quotchar = 0;

	int i;

	long len;
	#define INBUFLEN 256
	char inbuf[INBUFLEN];

	if (*ppos == 0) {
		state = 0;
		sympos = 0; sym[0] = 0;
		valpos = 0; val[0] = 0;
		quotchar = 0;
	}

	if ((!*lenp) || (dir != INBOUND)) {
		*lenp = 0;
		return 0;
	}

	len = *lenp;

	if (len > INBUFLEN - 1)
		len = INBUFLEN - 1;

	if (copy_from_user(inbuf, buffer, len))
		return -EFAULT;

	inbuf[len] = 0;

	for (i = 0; i < len; i++) {
		unsigned char c = inbuf[i];

		switch (state) {
		case 0:
			quotchar = sympos = valpos = sym[0] = val[0] = 0;
			if (!isspace(c) && (c != '\"') &&
			    (c != '\'') && (c != '=')) {
				sym[sympos++] = c;
				state = 1;
				break;
			}
			break;
		case 1:
			if (isspace(c)) {
				sym[sympos] = 0;
				state = 2;
				break;
			}
			if (c == '=') {
				sym[sympos] = 0;
				state = 3;
				break;
			}
			if ((c == '\"' ) || ( c == '\'' )) {
				state = 0;
				break;
			}
			if (sympos < MAXSYM) sym[sympos++] = c;
			break;
		case 2:
			if (isspace(c)) break;
			if (c == '=') {
				state = 3;
				break;
			}
			if ((c != '\"') && (c != '\'')) {
				quotchar = sympos = valpos = sym[0] = val[0] = 0;
				sym[sympos++] = c;
				state = 1;
				break;
			}
			state = 0;
			break;
		case 3:
			if (isspace(c)) break;
			if (c == '=') {
				state = 0;
				break;
			}
			if ((c == '\"') || (c == '\'')) {
				state = 4;
				quotchar = c;
				break;
			}
			val[valpos++] = c;
			state = 5;
			break;
		case 4:
			if (c == quotchar) {
				val[valpos] = 0;
				dgnc_set_info_var(sym, val);
				state = 0;
				break;
			}
			if (valpos < MAXSYM) val[valpos++] = c;
			break;
		case 5:
			if (isspace(c) || (c == '\"') ||
			    (c == '\'') || (c == '=')) {
				val[valpos] = 0;
				dgnc_set_info_var(sym, val);
				state = 0;
				break;
			}
			if (valpos < MAXSYM) val[valpos++] = c;
			break;
		default:
			break;
		}
	}

	*lenp = len;
	*ppos += len;
		
	return len;
}


/*
 *  Return mknod information for the driver's devices.
 */                                             
static int dgnc_read_mknod(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int i = 0;

	DPR_PROC(("dgnc_proc_info\n"));

	if (done) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	DPR_PROC(("dgnc_proc_mknod\n"));

	p += sprintf(p, "#\tCreate the management devices.\n");

	for (i = 0; i < MAXMGMTDEVICES; i++) {
		char tmp[100];
		sprintf(tmp, "/dev/dg/dgnc/mgmt%d", i);
		p += sprintf(p, "%s\t%d\t%d\t%d\n",
			tmp, dgnc_Major, i, 1);
	}

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*               
 *  Return what is (hopefully) useful information about the specific board.
 */
static int dgnc_read_board_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	char *name;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	name = brd->name;

	p += sprintf(p, "Board Name = %s\n", name);
	if (brd->serial_num[0] == '\0')
		p += sprintf(p, "Serial number = <UNKNOWN>\n");
	else
		p += sprintf(p, "Serial number = %s\n", brd->serial_num);

	p += sprintf(p, "Board Type = %d\n", brd->type);
	p += sprintf(p, "Number of Ports = %d\n", brd->nasync);

	/*
	 * report some things about the PCI bus that are important
	 * to some applications
	 */
        p += sprintf(p, "Vendor ID = 0x%x\n", brd->vendor);
        p += sprintf(p, "Device ID = 0x%x\n", brd->device);
        p += sprintf(p, "Subvendor ID = 0x%x\n", brd->subvendor);
        p += sprintf(p, "Subdevice ID = 0x%x\n", brd->subdevice);
        p += sprintf(p, "Bus = %d\n", brd->pci_bus);
        p += sprintf(p, "Slot = %d\n", brd->pci_slot);

	/*
	 * report the physical addresses assigned to us when we got
	 * registered
	 */	
        p += sprintf(p, "Memory Base Address = 0x%lx\n", brd->membase);
        p += sprintf(p, "Remapped Memory Base Address = 0x%p\n", brd->re_map_membase);

        p += sprintf(p, "Current state of board = %s\n", dgnc_state_text[brd->state]);
        p += sprintf(p, "Interrupt #: %d. Times interrupted: %ld\n",
		brd->irq, brd->intr_count);

        p += sprintf(p, "TX interrupts: %ld  RX interrupts: %ld\n",
		brd->intr_tx, brd->intr_rx);
        p += sprintf(p, "Modem interrupts: %ld\n", brd->intr_modem);

        p += sprintf(p, "Majors allocated to board = TTY: %d PR: %d\n",
		brd->SerialDriver.major, brd->PrintDriver.major);


	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}



static int dgnc_read_board_vpd(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	int i = 0, j = 0;
	char *p = buf;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	p += sprintf(p, "\n      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F          ASCII\n");

	for (i = 0; i < 0x40 * 2; i++) {
		j = i;
		if (!(i % 16)) {
			if (j > 0) {
				p += sprintf(p, "    ");
				for (j = i - 16; j < i; j++) {
					if (0x20 <= brd->vpd[j] && brd->vpd[j] <= 0x7e)
						p += sprintf(p, "%c", brd->vpd[j]);
					else
						p += sprintf(p, ".");
				}
				p += sprintf(p, "\n");
			}
			p += sprintf(p, "%04X ", i);
		}
		p += sprintf(p, "%02X ", brd->vpd[i]);
	}
	if (!(i % 16)) {
		p += sprintf(p, "    ");
		for (j = i - 16; j < i; j++) {
			if (0x20 <= brd->vpd[j] && brd->vpd[j] <= 0x7e)
				p += sprintf(p, "%c", brd->vpd[j]);
			else
				p += sprintf(p, ".");
		}
		p += sprintf(p, "\n");
	}

	p += sprintf(p, "\n");

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return what is (hopefully) useful stats about the specific board's ttys
 */
static int dgnc_read_board_ttystats(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int i = 0;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	/* Prepare the Header Labels */
	p += sprintf(p, "%2s %10s %23s %10s %9s\n",
		"Ch", "Chars Rx", "  Rx Par--Brk--Frm--Ovr", 
		"Chars Tx", "XON XOFF");

        for (i = 0; i < brd->nasync; i++) {

		struct channel_t *ch = brd->channels[i];

		p += sprintf(p, "%2d ", i);
		p += sprintf(p, "%10ld ", ch->ch_rxcount);
		p += sprintf(p, "    %4ld %4ld %4ld %4ld ", ch->ch_err_parity,
			ch->ch_err_break, ch->ch_err_frame, ch->ch_err_overrun);
		p += sprintf(p, "%10ld ", ch->ch_txcount);
		p += sprintf(p, "%4ld %4ld ", ch->ch_xon_sends, ch->ch_xoff_sends);

		p += sprintf(p, "\n");
	}

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return what is (hopefully) useful stats about the specific board's tty intrs
 */
static int dgnc_read_board_ttyintr(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int i = 0;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	/* Prepare the Header Labels */
	p += sprintf(p, "%2s     %14s  %14s  %14s\n",
		"Ch", "TX interrupts", "RX interrupts", "Modem interrupts");

        for (i = 0; i < brd->nasync; i++) {

		struct channel_t *ch = brd->channels[i];

		p += sprintf(p, "%2d ", i);

	        p += sprintf(p, "    %14ld  %14ld    %14ld",
			ch->ch_intr_tx, ch->ch_intr_rx, ch->ch_intr_modem);

		p += sprintf(p, "\n");
	}

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return what is (hopefully) useful flags about the specific board's ttys
 */
static int dgnc_read_board_ttyflags(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int i = 0;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	/* Prepare the Header Labels */
	p += sprintf(p, "%2s %5s %5s %5s %5s %5s %10s  Line Status Flags\n",
		"Ch", "CFlag", "IFlag", "OFlag", "LFlag", "DFlag", "Baud");

        for (i = 0; i < brd->nasync; i++) {

		struct channel_t *ch = brd->channels[i];

		p += sprintf(p, "%2d ", i);
		p += sprintf(p, "%5x ", ch->ch_c_cflag);
		p += sprintf(p, "%5x ", ch->ch_c_iflag);
		p += sprintf(p, "%5x ", ch->ch_c_oflag);
		p += sprintf(p, "%5x ", ch->ch_c_lflag);
		p += sprintf(p, "%5x ", ch->ch_digi.digi_flags);
		p += sprintf(p, "%10d ", ch->ch_old_baud);

		if (!ch->ch_open_count) {
			p += sprintf(p, " -- -- -- -- -- -- --") ;
		} else {
			p += sprintf(p, " op %s %s %s %s %s %s",
				(ch->ch_mostat & UART_MCR_RTS) ? "rs" : "--",
				(ch->ch_mistat & UART_MSR_CTS) ? "cs" : "--",
				(ch->ch_mostat & UART_MCR_DTR) ? "tr" : "--",
				(ch->ch_mistat & UART_MSR_DSR) ? "mr" : "--",
				(ch->ch_mistat & UART_MSR_DCD) ? "cd" : "--",
				(ch->ch_mistat & UART_MSR_RI)  ? "ri" : "--");
		}

		p += sprintf(p, "\n");
	}
	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return mknod information for the board's devices.
 */                                             
static int dgnc_read_board_mknod(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct board_t	*brd;
	static int done = 0;
	static char buf[4096];
	char str[MAXTTYNAMELEN];
	char *p = buf;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	brd = (struct board_t *) table->data;

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	/*
	 * For each board, output the device information in
	 * a handy table format...
	 */
	p += sprintf(p,	"# Create the TTY and PR devices\n");

	/* TTY devices */
	sprintf(str, "ttyn%d%%p", brd->boardnum + 1);
	p += sprintf(p, "%s\t\t\t%d\t%d\t%d\n", str,
		brd->dgnc_Serial_Major, 0, brd->maxports);

	/* PR devices */
	sprintf(str, "prn%d%%p", brd->boardnum + 1);
	p += sprintf(p, "%s\t\t\t%d\t%d\t%d\n", str,
		brd->dgnc_TransparentPrint_Major, 128, brd->maxports);

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return what is (hopefully) useful information about the specific channel.
 */
static int dgnc_read_channel_info(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	static int done = 0;
	static char buf[4096];
	char *p = buf;

	DPR_PROC(("dgnc_proc_info\n"));

	ch = (struct channel_t *) table->data;

	if (done || !ch || (ch->magic != DGNC_CHANNEL_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	p += sprintf(p, "Port number:\t\t%d\n", ch->ch_portnum);
	p += sprintf(p, "\n");

	/* Prepare the Header Labels */
	p += sprintf(p, "%10s %23s %10s %9s\n",
		"Chars Rx", "  Rx Par--Brk--Frm--Ovr",
		"Chars Tx", "XON XOFF");
	p += sprintf(p, "%10ld ", ch->ch_rxcount);
	p += sprintf(p, "    %4ld %4ld %4ld %4ld ", ch->ch_err_parity,
		ch->ch_err_break, ch->ch_err_frame, ch->ch_err_overrun);
	p += sprintf(p, "%10ld ", ch->ch_txcount);  
	p += sprintf(p, "%4ld %4ld ", ch->ch_xon_sends, ch->ch_xoff_sends);
	p += sprintf(p, "\n\n");

	/* Prepare the Header Labels */
	p += sprintf(p, "%5s %5s %5s %5s %5s %10s  Line Status Flags\n",
		"CFlag", "IFlag", "OFlag", "LFlag", "DFlag", "Baud");

	p += sprintf(p, "%5x ", ch->ch_c_cflag);
	p += sprintf(p, "%5x ", ch->ch_c_iflag);
	p += sprintf(p, "%5x ", ch->ch_c_oflag);
	p += sprintf(p, "%5x ", ch->ch_c_lflag);
	p += sprintf(p, "%5x ", ch->ch_digi.digi_flags);
	p += sprintf(p, "%10d ", ch->ch_old_baud);
	if (!ch->ch_open_count) {
		p += sprintf(p, " -- -- -- -- -- -- --") ;
	} else {
		p += sprintf(p, " op %s %s %s %s %s %s",
			(ch->ch_mostat & UART_MCR_RTS) ? "rs" : "--",
			(ch->ch_mistat & UART_MSR_CTS) ? "cs" : "--",
			(ch->ch_mostat & UART_MCR_DTR) ? "tr" : "--",
			(ch->ch_mistat & UART_MSR_DSR) ? "mr" : "--",
			(ch->ch_mistat & UART_MSR_DCD) ? "cd" : "--",
			(ch->ch_mistat & UART_MSR_RI)  ? "ri" : "--");
	}
	p += sprintf(p, "\n\n");

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf; 
	done = 1;
	return 0;
}


/*
 *  Return mknod information for the board's devices.
 */
static int dgnc_read_channel_custom_ttyname(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	struct board_t  *brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int     cn;
	int     bn;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	ch = (struct channel_t *) table->data;

	if (done || !ch || (ch->magic != DGNC_CHANNEL_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	brd = ch->ch_bd;        

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	bn = brd->boardnum;
	cn = ch->ch_portnum;

	p += sprintf(p, "ttyn%d%c\n", bn + 1, 'a' + cn);

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf;
	done = 1;
	return 0;
}




/*
 *  Return mknod information for the board's devices.
 */
static int dgnc_read_channel_custom_prname(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	struct board_t  *brd;
	static int done = 0;
	static char buf[4096];
	char *p = buf;
	int     cn;
	int     bn;

	DPR_PROC(("dgnc_proc_brd_info\n"));

	ch = (struct channel_t *) table->data;

	if (done || !ch || (ch->magic != DGNC_CHANNEL_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	brd = ch->ch_bd;        

	if (done || !brd || (brd->magic != DGNC_BOARD_MAGIC)) {
		done = 0;
		*lenp = 0;
		return 0;
	}

	bn = brd->boardnum;
	cn = ch->ch_portnum;

	p += sprintf(p, "prn%d%c\n", bn + 1, 'a' + cn);

	if (copy_to_user(buffer, buf, (p - (char *) buf)))
		return -EFAULT;

	*lenp = p - (char *) buf;
	*ppos += p - (char *) buf;
	done = 1;
	return 0;
}


static int dgnc_open_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				void *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	ulong  lock_flags;

	ch = (struct channel_t *) table->data;

	if (!ch || (ch->magic != DGNC_CHANNEL_MAGIC))
		return 0;

	ch->ch_sniff_buf = dgnc_driver_kzmalloc(SNIFF_MAX, GFP_KERNEL);

	DGNC_LOCK(ch->ch_lock, lock_flags);
	ch->ch_sniff_flags |= SNIFF_OPEN;
	DGNC_UNLOCK(ch->ch_lock, lock_flags);

	return 0;
}

static int dgnc_close_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				void *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	ulong  lock_flags;

	ch = (struct channel_t *) table->data;

	if (!ch || (ch->magic != DGNC_CHANNEL_MAGIC))
		return 0;

	DGNC_LOCK(ch->ch_lock, lock_flags);
	ch->ch_sniff_flags &= ~(SNIFF_OPEN);
	kfree(ch->ch_sniff_buf);
	DGNC_UNLOCK(ch->ch_lock, lock_flags);

	return 0;
}


/*
 *    Copy data from the monitoring buffer to the user, freeing space
 *    in the monitoring buffer for more messages
 *
 */
static int dgnc_read_channel_sniff(struct dgnc_proc_entry *table, int dir, struct file *filp,
				char __user *buffer, ssize_t *lenp, loff_t *ppos)
{
	struct channel_t *ch;
	int n;
	int r;
	int offset = 0;
	int res = 0;
	ssize_t rtn = 0;
	ulong  lock_flags;

	ch = (struct channel_t *) table->data;

	if (!ch || (ch->magic != DGNC_CHANNEL_MAGIC)) {
		rtn = -ENXIO;
		goto done;
	}

	/*
	 *  Wait for some data to appear in the buffer.
	 */
	DGNC_LOCK(ch->ch_lock, lock_flags);

	for (;;) {
		n = (ch->ch_sniff_in - ch->ch_sniff_out) & SNIFF_MASK;

		if (n != 0)
			break;

		ch->ch_sniff_flags |= SNIFF_WAIT_DATA;

		DGNC_UNLOCK(ch->ch_lock, lock_flags);

		/*
		 * Go to sleep waiting until the condition becomes true.
		 */
		rtn = wait_event_interruptible(ch->ch_sniff_wait,
			((ch->ch_sniff_flags & SNIFF_WAIT_DATA) == 0));

		if (rtn)
			goto done;

		DGNC_LOCK(ch->ch_lock, lock_flags);
	}

	/*
	 *  Read whatever is there.
	 */

	if (n > *lenp)
		n = *lenp;

	res = n;

	r = SNIFF_MAX - ch->ch_sniff_out;

	if (r <= n) {
		DGNC_UNLOCK(ch->ch_lock, lock_flags);
		rtn = copy_to_user(buffer, ch->ch_sniff_buf + ch->ch_sniff_out, r);
		if (rtn) {
			rtn = -EFAULT;
			goto done;
		}

		DGNC_LOCK(ch->ch_lock, lock_flags);

		ch->ch_sniff_out = 0;
		n -= r;
		offset = r;
	}

	DGNC_UNLOCK(ch->ch_lock, lock_flags);
	rtn = copy_to_user(buffer + offset, ch->ch_sniff_buf + ch->ch_sniff_out, n);
	if (rtn) {
		rtn = -EFAULT;
		goto done;
	}
	DGNC_LOCK(ch->ch_lock, lock_flags);

	ch->ch_sniff_out += n;
	*ppos += res;
	rtn = res;
//	rtn = 0;

	/*
	 *  Wakeup any thread waiting for buffer space.
	 */

	if (ch->ch_sniff_flags & SNIFF_WAIT_SPACE) {
		ch->ch_sniff_flags &= ~SNIFF_WAIT_SPACE;
		wake_up_interruptible(&ch->ch_sniff_wait);
	}

	DGNC_UNLOCK(ch->ch_lock, lock_flags);

done:
	return rtn;
}


/*
 * Register the basic /proc/dgnc files that appear whenever
 * the driver is loaded.
 */
void dgnc_proc_register_basic_prescan(void)
{
	/*
	 *      Register /proc/dgnc
	 */
	ProcDGNC = proc_create("dgnc", (0700 | S_IFDIR), NULL, &dgnc_proc_file_ops);
	dgnc_register_proc_table(dgnc_table, ProcDGNC);
}


/*
 * Register the basic /proc/dgnc files that appear whenever
 * the driver is loaded.
 */
void dgnc_proc_register_basic_postscan(int board_num)
{
	int i, j;
	char board[10];
	sprintf(board, "%d", board_num);

	/* Set proc board entry pointer */
	dgnc_Board[board_num]->proc_entry_pointer = create_proc_entry(board, (0700 | S_IFDIR), ProcDGNC);

	/* Create a new copy of the board_table... */
	dgnc_Board[board_num]->dgnc_board_table = dgnc_driver_kzmalloc(sizeof(dgnc_board_table), 
		GFP_KERNEL);

	/* Now copy the default table into that memory */
	memcpy(dgnc_Board[board_num]->dgnc_board_table, dgnc_board_table, sizeof(dgnc_board_table));

	/* Initialize semaphores in each table slot */
	for (i = 0; i < 999; i++) {
		if (!dgnc_Board[board_num]->dgnc_board_table[i].magic) {
			break;
		}

		init_MUTEX(&(dgnc_Board[board_num]->dgnc_board_table[i].excl_sem));
		dgnc_Board[board_num]->dgnc_board_table[i].data = dgnc_Board[board_num];

	}

	/* Register board table into proc */
	dgnc_register_proc_table(dgnc_Board[board_num]->dgnc_board_table, 
		dgnc_Board[board_num]->proc_entry_pointer);

	/*
	 * Add new entries for each port.
	 */
	for (i = 0; i < dgnc_Board[board_num]->nasync; i++) {

		char channel[10];
		sprintf(channel, "%d", i);

		/* Set proc channel entry pointer */
		dgnc_Board[board_num]->channels[i]->proc_entry_pointer =
			create_proc_entry(channel, (0700 | S_IFDIR), 
			dgnc_Board[board_num]->proc_entry_pointer);

		/* Create a new copy of the channel_table... */
		dgnc_Board[board_num]->channels[i]->dgnc_channel_table =
			dgnc_driver_kzmalloc(sizeof(dgnc_channel_table), GFP_KERNEL);

		/* Now copy the default table into that memory */
		memcpy(dgnc_Board[board_num]->channels[i]->dgnc_channel_table,
			dgnc_channel_table, sizeof(dgnc_channel_table));

		/* Initialize semaphores in each table slot */
		for (j = 0; j < 999; j++) {
			if (!dgnc_Board[board_num]->channels[i]->dgnc_channel_table[j].magic) {
				break;
			}

			init_MUTEX(&(dgnc_Board[board_num]->channels[i]->dgnc_channel_table[j].excl_sem));
			dgnc_Board[board_num]->channels[i]->dgnc_channel_table[j].data = 
				dgnc_Board[board_num]->channels[i];
		}

		/* Register channel table into proc */
		dgnc_register_proc_table(dgnc_Board[board_num]->channels[i]->dgnc_channel_table, 
			dgnc_Board[board_num]->channels[i]->proc_entry_pointer);
	}
}


static void dgnc_remove_proc_entry(struct proc_dir_entry *pde)
{ 
	if (!pde) {
		DPR_PROC(("dgnc_remove_proc_entry... NULL entry... not removing...\n"));
		return;
	}

	remove_proc_entry(pde->name, pde->parent);
}


void dgnc_proc_unregister_all(void)
{
	int i = 0, j = 0;

	/* Walk each board, blowing away their proc entries... */
	for (i = 0; i < dgnc_NumBoards; i++) {

		/* Walk each channel, blowing away their proc entries... */
		for (j = 0; j < dgnc_Board[i]->nasync; j++) {

			dgnc_unregister_proc_table(dgnc_Board[i]->channels[j]->dgnc_channel_table, 
				dgnc_Board[i]->channels[j]->proc_entry_pointer);
			dgnc_remove_proc_entry(dgnc_Board[i]->channels[j]->proc_entry_pointer);
			kfree(dgnc_Board[i]->channels[j]->dgnc_channel_table);
		}

		dgnc_unregister_proc_table(dgnc_Board[i]->dgnc_board_table, 
			dgnc_Board[i]->proc_entry_pointer);
		dgnc_remove_proc_entry(dgnc_Board[i]->proc_entry_pointer);
		kfree(dgnc_Board[i]->dgnc_board_table);
	}

	/* Blow away the top proc entry */
	dgnc_unregister_proc_table(dgnc_table, ProcDGNC);
	dgnc_remove_proc_entry(ProcDGNC);
}
