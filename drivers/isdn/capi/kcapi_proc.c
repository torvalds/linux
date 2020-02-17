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
#include <linux/export.h>

static char *state2str(unsigned short state)
{
	switch (state) {
	case CAPI_CTR_DETECTED:	return "detected";
	case CAPI_CTR_LOADING:	return "loading";
	case CAPI_CTR_RUNNING:	return "running";
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
	__acquires(capi_controller_lock)
{
	mutex_lock(&capi_controller_lock);

	if (*pos < CAPI_MAXCONTR)
		return &capi_controller[*pos];

	return NULL;
}

static void *controller_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	if (*pos < CAPI_MAXCONTR)
		return &capi_controller[*pos];

	return NULL;
}

static void controller_stop(struct seq_file *seq, void *v)
	__releases(capi_controller_lock)
{
	mutex_unlock(&capi_controller_lock);
}

static int controller_show(struct seq_file *seq, void *v)
{
	struct capi_ctr *ctr = *(struct capi_ctr **) v;

	if (!ctr)
		return 0;

	seq_printf(seq, "%d %-10s %-8s %-16s %s\n",
		   ctr->cnr, ctr->driver_name,
		   state2str(ctr->state),
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

static const struct seq_operations seq_controller_ops = {
	.start	= controller_start,
	.next	= controller_next,
	.stop	= controller_stop,
	.show	= controller_show,
};

static const struct seq_operations seq_contrstats_ops = {
	.start	= controller_start,
	.next	= controller_next,
	.stop	= controller_stop,
	.show	= contrstats_show,
};

// /proc/capi/applications:
//      applid l3cnt dblkcnt dblklen #ncci recvqueuelen
// /proc/capi/applstats:
//      applid nrecvctlpkt nrecvdatapkt nsentctlpkt nsentdatapkt
// ---------------------------------------------------------------------------

static void *applications_start(struct seq_file *seq, loff_t *pos)
	__acquires(capi_controller_lock)
{
	mutex_lock(&capi_controller_lock);

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

static void applications_stop(struct seq_file *seq, void *v)
	__releases(capi_controller_lock)
{
	mutex_unlock(&capi_controller_lock);
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

static const struct seq_operations seq_applications_ops = {
	.start	= applications_start,
	.next	= applications_next,
	.stop	= applications_stop,
	.show	= applications_show,
};

static const struct seq_operations seq_applstats_ops = {
	.start	= applications_start,
	.next	= applications_next,
	.stop	= applications_stop,
	.show	= applstats_show,
};

// ---------------------------------------------------------------------------

/* /proc/capi/drivers is always empty */
static ssize_t empty_read(struct file *file, char __user *buf,
			  size_t size, loff_t *off)
{
	return 0;
}

static const struct proc_ops empty_proc_ops = {
	.proc_read	= empty_read,
};

// ---------------------------------------------------------------------------

void __init
kcapi_proc_init(void)
{
	proc_mkdir("capi",             NULL);
	proc_mkdir("capi/controllers", NULL);
	proc_create_seq("capi/controller",   0, NULL, &seq_controller_ops);
	proc_create_seq("capi/contrstats",   0, NULL, &seq_contrstats_ops);
	proc_create_seq("capi/applications", 0, NULL, &seq_applications_ops);
	proc_create_seq("capi/applstats",    0, NULL, &seq_applstats_ops);
	proc_create("capi/driver",           0, NULL, &empty_proc_ops);
}

void
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
