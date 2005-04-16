/*
 * Kernel CAPI 2.0 Module - /proc/capi handling
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * Copyright 2002 by Kai Germaschewski <kai@germaschewski.name>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */


#include "kcapi.h"
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>

static char *
cardstate2str(unsigned short cardstate)
{
	switch (cardstate) {
	case CARD_DETECTED:	return "detected";
	case CARD_LOADING:	return "loading";
	case CARD_RUNNING:	return "running";
	default:	        return "???";
	}
}

// /proc/capi
// ===========================================================================

// /proc/capi/controller: 
//      cnr driver cardstate name driverinfo
// /proc/capi/contrstats:
//      cnr nrecvctlpkt nrecvdatapkt nsentctlpkt nsentdatapkt
// ---------------------------------------------------------------------------

static void *controller_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos < CAPI_MAXCONTR)
		return &capi_cards[*pos];

	return NULL;
}

static void *controller_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	if (*pos < CAPI_MAXCONTR)
		return &capi_cards[*pos];

	return NULL;
}

static void controller_stop(struct seq_file *seq, void *v)
{
}

static int controller_show(struct seq_file *seq, void *v)
{
	struct capi_ctr *ctr = *(struct capi_ctr **) v;

	if (!ctr)
		return 0;

	seq_printf(seq, "%d %-10s %-8s %-16s %s\n",
		   ctr->cnr, ctr->driver_name,
		   cardstate2str(ctr->cardstate),
		   ctr->name,
		   ctr->procinfo ?  ctr->procinfo(ctr) : "");

	return 0;
}

static int contrstats_show(struct seq_file *seq, void *v)
{
	struct capi_ctr *ctr = *(struct capi_ctr **) v;

	if (!ctr)
		return 0;

	seq_printf(seq, "%d %lu %lu %lu %lu\n",
		   ctr->cnr, 
		   ctr->nrecvctlpkt,
		   ctr->nrecvdatapkt,
		   ctr->nsentctlpkt,
		   ctr->nsentdatapkt);

	return 0;
}

struct seq_operations seq_controller_ops = {
	.start	= controller_start,
	.next	= controller_next,
	.stop	= controller_stop,
	.show	= controller_show,
};

struct seq_operations seq_contrstats_ops = {
	.start	= controller_start,
	.next	= controller_next,
	.stop	= controller_stop,
	.show	= contrstats_show,
};

static int seq_controller_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &seq_controller_ops);
}

static int seq_contrstats_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &seq_contrstats_ops);
}

static struct file_operations proc_controller_ops = {
	.open		= seq_controller_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct file_operations proc_contrstats_ops = {
	.open		= seq_contrstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

// /proc/capi/applications: 
//      applid l3cnt dblkcnt dblklen #ncci recvqueuelen
// /proc/capi/applstats: 
//      applid nrecvctlpkt nrecvdatapkt nsentctlpkt nsentdatapkt
// ---------------------------------------------------------------------------

static void *
applications_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos < CAPI_MAXAPPL)
		return &capi_applications[*pos];

	return NULL;
}

static void *
applications_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	if (*pos < CAPI_MAXAPPL)
		return &capi_applications[*pos];

	return NULL;
}

static void
applications_stop(struct seq_file *seq, void *v)
{
}

static int
applications_show(struct seq_file *seq, void *v)
{
	struct capi20_appl *ap = *(struct capi20_appl **) v;

	if (!ap)
		return 0;

	seq_printf(seq, "%u %d %d %d\n",
		   ap->applid,
		   ap->rparam.level3cnt,
		   ap->rparam.datablkcnt,
		   ap->rparam.datablklen);

	return 0;
}

static int
applstats_show(struct seq_file *seq, void *v)
{
	struct capi20_appl *ap = *(struct capi20_appl **) v;

	if (!ap)
		return 0;

	seq_printf(seq, "%u %lu %lu %lu %lu\n",
		   ap->applid,
		   ap->nrecvctlpkt,
		   ap->nrecvdatapkt,
		   ap->nsentctlpkt,
		   ap->nsentdatapkt);

	return 0;
}

struct seq_operations seq_applications_ops = {
	.start	= applications_start,
	.next	= applications_next,
	.stop	= applications_stop,
	.show	= applications_show,
};

struct seq_operations seq_applstats_ops = {
	.start	= applications_start,
	.next	= applications_next,
	.stop	= applications_stop,
	.show	= applstats_show,
};

static int
seq_applications_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &seq_applications_ops);
}

static int
seq_applstats_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &seq_applstats_ops);
}

static struct file_operations proc_applications_ops = {
	.open		= seq_applications_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct file_operations proc_applstats_ops = {
	.open		= seq_applstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void
create_seq_entry(char *name, mode_t mode, struct file_operations *f)
{
	struct proc_dir_entry *entry;
	entry = create_proc_entry(name, mode, NULL);
	if (entry)
		entry->proc_fops = f;
}

// ---------------------------------------------------------------------------


static __inline__ struct capi_driver *capi_driver_get_idx(loff_t pos)
{
	struct capi_driver *drv = NULL;
	struct list_head *l;
	loff_t i;

	i = 0;
	list_for_each(l, &capi_drivers) {
		drv = list_entry(l, struct capi_driver, list);
		if (i++ == pos)
			return drv;
	}
	return NULL;
}

static void *capi_driver_start(struct seq_file *seq, loff_t *pos)
{
	struct capi_driver *drv;
	read_lock(&capi_drivers_list_lock);
	drv = capi_driver_get_idx(*pos);
	return drv;
}

static void *capi_driver_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct capi_driver *drv = (struct capi_driver *)v;
	++*pos;
	if (drv->list.next == &capi_drivers) return NULL;
	return list_entry(drv->list.next, struct capi_driver, list);
}

static void capi_driver_stop(struct seq_file *seq, void *v)
{
	read_unlock(&capi_drivers_list_lock);
}

static int capi_driver_show(struct seq_file *seq, void *v)
{
	struct capi_driver *drv = (struct capi_driver *)v;
	seq_printf(seq, "%-32s %s\n", drv->name, drv->revision);
	return 0;
}

struct seq_operations seq_capi_driver_ops = {
	.start	= capi_driver_start,
	.next	= capi_driver_next,
	.stop	= capi_driver_stop,
	.show	= capi_driver_show,
};

static int
seq_capi_driver_open(struct inode *inode, struct file *file)
{
	int err;
	err = seq_open(file, &seq_capi_driver_ops);
	return err;
}

static struct file_operations proc_driver_ops = {
	.open		= seq_capi_driver_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

// ---------------------------------------------------------------------------

void __init 
kcapi_proc_init(void)
{
	proc_mkdir("capi",             NULL);
	proc_mkdir("capi/controllers", NULL);
	create_seq_entry("capi/controller",   0, &proc_controller_ops);
	create_seq_entry("capi/contrstats",   0, &proc_contrstats_ops);
	create_seq_entry("capi/applications", 0, &proc_applications_ops);
	create_seq_entry("capi/applstats",    0, &proc_applstats_ops);
	create_seq_entry("capi/driver",       0, &proc_driver_ops);
}

void __exit
kcapi_proc_exit(void)
{
	remove_proc_entry("capi/driver",       NULL);
	remove_proc_entry("capi/controller",   NULL);
	remove_proc_entry("capi/contrstats",   NULL);
	remove_proc_entry("capi/applications", NULL);
	remove_proc_entry("capi/applstats",    NULL);
	remove_proc_entry("capi/controllers",  NULL);
	remove_proc_entry("capi",              NULL);
}
