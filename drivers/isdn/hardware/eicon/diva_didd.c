/* $Id: diva_didd.c,v 1.13.6.4 2005/02/11 19:40:25 armin Exp $
 *
 * DIDD Interface module for Eicon active cards.
 * 
 * Functions are in dadapter.c 
 * 
 * Copyright 2002-2003 by Armin Schindler (mac@melware.de) 
 * Copyright 2002-2003 Cytronics & Melware (info@melware.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/net_namespace.h>

#include "platform.h"
#include "di_defs.h"
#include "dadapter.h"
#include "divasync.h"
#include "did_vers.h"

static char *main_revision = "$Revision: 1.13.6.4 $";

static char *DRIVERNAME =
    "Eicon DIVA - DIDD table (http://www.melware.net)";
static char *DRIVERLNAME = "divadidd";
char *DRIVERRELEASE_DIDD = "2.0";

MODULE_DESCRIPTION("DIDD table driver for diva drivers");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("Eicon diva drivers");
MODULE_LICENSE("GPL");

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern int diddfunc_init(void);
extern void diddfunc_finit(void);

extern void DIVA_DIDD_Read(void *, int);

static struct proc_dir_entry *proc_didd;
struct proc_dir_entry *proc_net_eicon = NULL;

EXPORT_SYMBOL(DIVA_DIDD_Read);
EXPORT_SYMBOL(proc_net_eicon);

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

static int divadidd_proc_show(struct seq_file *m, void *v)
{
	char tmprev[32];

	strcpy(tmprev, main_revision);
	seq_printf(m, "%s\n", DRIVERNAME);
	seq_printf(m, "name     : %s\n", DRIVERLNAME);
	seq_printf(m, "release  : %s\n", DRIVERRELEASE_DIDD);
	seq_printf(m, "build    : %s(%s)\n",
		       diva_didd_common_code_build, DIVA_BUILD);
	seq_printf(m, "revision : %s\n", getrev(tmprev));

	return 0;
}

static int divadidd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, divadidd_proc_show, NULL);
}

static const struct file_operations divadidd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= divadidd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int DIVA_INIT_FUNCTION create_proc(void)
{
	proc_net_eicon = proc_mkdir("eicon", init_net.proc_net);

	if (proc_net_eicon) {
		proc_didd = proc_create(DRIVERLNAME, S_IRUGO, proc_net_eicon,
					&divadidd_proc_fops);
		return (1);
	}
	return (0);
}

static void remove_proc(void)
{
	remove_proc_entry(DRIVERLNAME, proc_net_eicon);
	remove_proc_entry("eicon", init_net.proc_net);
}

static int DIVA_INIT_FUNCTION divadidd_init(void)
{
	char tmprev[32];
	int ret = 0;

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_DIDD);
	strcpy(tmprev, main_revision);
	printk("%s  Build:%s(%s)\n", getrev(tmprev),
	       diva_didd_common_code_build, DIVA_BUILD);

	if (!create_proc()) {
		printk(KERN_ERR "%s: could not create proc entry\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}

	if (!diddfunc_init()) {
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
#ifdef MODULE
		remove_proc();
#endif
		ret = -EIO;
		goto out;
	}

      out:
	return (ret);
}

static void DIVA_EXIT_FUNCTION divadidd_exit(void)
{
	diddfunc_finit();
	remove_proc();
	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divadidd_init);
module_exit(divadidd_exit);
