// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <xen/interface/platform.h>
#include <asm/xen/hypercall.h>
#include <xen/xen-ops.h>
#include "xenfs.h"


#define XEN_KSYM_NAME_LEN 127 /* Hypervisor may have different name length */

struct xensyms {
	struct xen_platform_op op;
	char *name;
	uint32_t namelen;
};

/* Grab next output page from the hypervisor */
static int xensyms_next_sym(struct xensyms *xs)
{
	int ret;
	struct xenpf_symdata *symdata = &xs->op.u.symdata;
	uint64_t symnum;

	memset(xs->name, 0, xs->namelen);
	symdata->namelen = xs->namelen;

	symnum = symdata->symnum;

	ret = HYPERVISOR_platform_op(&xs->op);
	if (ret < 0)
		return ret;

	/*
	 * If hypervisor's symbol didn't fit into the buffer then allocate
	 * a larger buffer and try again.
	 */
	if (unlikely(symdata->namelen > xs->namelen)) {
		kfree(xs->name);

		xs->namelen = symdata->namelen;
		xs->name = kzalloc(xs->namelen, GFP_KERNEL);
		if (!xs->name)
			return -ENOMEM;

		set_xen_guest_handle(symdata->name, xs->name);
		symdata->symnum--; /* Rewind */

		ret = HYPERVISOR_platform_op(&xs->op);
		if (ret < 0)
			return ret;
	}

	if (symdata->symnum == symnum)
		/* End of symbols */
		return 1;

	return 0;
}

static void *xensyms_start(struct seq_file *m, loff_t *pos)
{
	struct xensyms *xs = m->private;

	xs->op.u.symdata.symnum = *pos;

	if (xensyms_next_sym(xs))
		return NULL;

	return m->private;
}

static void *xensyms_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct xensyms *xs = m->private;

	xs->op.u.symdata.symnum = ++(*pos);

	if (xensyms_next_sym(xs))
		return NULL;

	return p;
}

static int xensyms_show(struct seq_file *m, void *p)
{
	struct xensyms *xs = m->private;
	struct xenpf_symdata *symdata = &xs->op.u.symdata;

	seq_printf(m, "%016llx %c %s\n", symdata->address,
		   symdata->type, xs->name);

	return 0;
}

static void xensyms_stop(struct seq_file *m, void *p)
{
}

static const struct seq_operations xensyms_seq_ops = {
	.start = xensyms_start,
	.next = xensyms_next,
	.show = xensyms_show,
	.stop = xensyms_stop,
};

static int xensyms_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	struct xensyms *xs;
	int ret;

	ret = seq_open_private(file, &xensyms_seq_ops,
			       sizeof(struct xensyms));
	if (ret)
		return ret;

	m = file->private_data;
	xs = m->private;

	xs->namelen = XEN_KSYM_NAME_LEN + 1;
	xs->name = kzalloc(xs->namelen, GFP_KERNEL);
	if (!xs->name) {
		seq_release_private(inode, file);
		return -ENOMEM;
	}
	set_xen_guest_handle(xs->op.u.symdata.name, xs->name);
	xs->op.cmd = XENPF_get_symbol;
	xs->op.u.symdata.namelen = xs->namelen;

	return 0;
}

static int xensyms_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct xensyms *xs = m->private;

	kfree(xs->name);
	return seq_release_private(inode, file);
}

const struct file_operations xensyms_ops = {
	.open = xensyms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = xensyms_release
};
