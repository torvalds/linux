/* $Id: divasproc.c,v 1.19.4.3 2005/01/31 12:22:20 armin Exp $
 *
 * Low level driver for Eicon DIVA Server ISDN cards.
 * /proc functions
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include "platform.h"
#include "debuglib.h"
#undef ID_MASK
#undef N_DATA
#include "pc.h"
#include "di_defs.h"
#include "divasync.h"
#include "di.h"
#include "io.h"
#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "diva.h"
#include "diva_pci.h"


extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
extern void divas_get_version(char *);
extern void diva_get_vserial_number(PISDN_ADAPTER IoAdapter, char *buf);

/*********************************************************
 ** Functions for /proc interface / File operations
 *********************************************************/

static char *divas_proc_name = "divas";
static char *adapter_dir_name = "adapter";
static char *info_proc_name = "info";
static char *grp_opt_proc_name = "group_optimization";
static char *d_l1_down_proc_name = "dynamic_l1_down";

/*
** "divas" entry
*/

extern struct proc_dir_entry *proc_net_eicon;
static struct proc_dir_entry *divas_proc_entry = NULL;

static ssize_t
divas_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
	int len = 0;
	int cadapter;
	char tmpbuf[80];
	char tmpser[16];

	if (*off)
		return 0;

	divas_get_version(tmpbuf);
	if (copy_to_user(buf + len, &tmpbuf, strlen(tmpbuf)))
		return -EFAULT;
	len += strlen(tmpbuf);

	for (cadapter = 0; cadapter < MAX_ADAPTER; cadapter++) {
		if (IoAdapters[cadapter]) {
			diva_get_vserial_number(IoAdapters[cadapter],
						tmpser);
			sprintf(tmpbuf,
				"%2d: %-30s Serial:%-10s IRQ:%2d\n",
				cadapter + 1,
				IoAdapters[cadapter]->Properties.Name,
				tmpser,
				IoAdapters[cadapter]->irq_info.irq_nr);
			if ((strlen(tmpbuf) + len) > count)
				break;
			if (copy_to_user
			    (buf + len, &tmpbuf,
			     strlen(tmpbuf))) return -EFAULT;
			len += strlen(tmpbuf);
		}
	}

	*off += len;
	return (len);
}

static ssize_t
divas_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
	return (-ENODEV);
}

static unsigned int divas_poll(struct file *file, poll_table *wait)
{
	return (POLLERR);
}

static int divas_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int divas_close(struct inode *inode, struct file *file)
{
	return (0);
}

static const struct file_operations divas_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = divas_read,
	.write   = divas_write,
	.poll    = divas_poll,
	.open    = divas_open,
	.release = divas_close
};

int create_divas_proc(void)
{
	divas_proc_entry = proc_create(divas_proc_name, S_IFREG | S_IRUGO,
				       proc_net_eicon, &divas_fops);
	if (!divas_proc_entry)
		return (0);

	return (1);
}

void remove_divas_proc(void)
{
	if (divas_proc_entry) {
		remove_proc_entry(divas_proc_name, proc_net_eicon);
		divas_proc_entry = NULL;
	}
}

static ssize_t grp_opt_proc_write(struct file *file, const char __user *buffer,
				  size_t count, loff_t *pos)
{
	diva_os_xdi_adapter_t *a = PDE(file->f_path.dentry->d_inode)->data;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];

	if ((count == 1) || (count == 2)) {
		char c;
		if (get_user(c, buffer))
			return -EFAULT;
		switch (c) {
		case '0':
			IoAdapter->capi_cfg.cfg_1 &=
				~DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON;
			break;
		case '1':
			IoAdapter->capi_cfg.cfg_1 |=
				DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON;
			break;
		default:
			return (-EINVAL);
		}
		return (count);
	}
	return (-EINVAL);
}

static ssize_t d_l1_down_proc_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t *pos)
{
	diva_os_xdi_adapter_t *a = PDE(file->f_path.dentry->d_inode)->data;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];

	if ((count == 1) || (count == 2)) {
		char c;
		if (get_user(c, buffer))
			return -EFAULT;
		switch (c) {
		case '0':
			IoAdapter->capi_cfg.cfg_1 &=
				~DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON;
			break;
		case '1':
			IoAdapter->capi_cfg.cfg_1 |=
				DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON;
			break;
		default:
			return (-EINVAL);
		}
		return (count);
	}
	return (-EINVAL);
}

static int d_l1_down_proc_show(struct seq_file *m, void *v)
{
	diva_os_xdi_adapter_t *a = m->private;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];

	seq_printf(m, "%s\n",
		   (IoAdapter->capi_cfg.
		    cfg_1 & DIVA_XDI_CAPI_CFG_1_DYNAMIC_L1_ON) ? "1" :
		   "0");
	return 0;
}

static int d_l1_down_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, d_l1_down_proc_show, PDE(inode)->data);
}

static const struct file_operations d_l1_down_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= d_l1_down_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= d_l1_down_proc_write,
};

static int grp_opt_proc_show(struct seq_file *m, void *v)
{
	diva_os_xdi_adapter_t *a = m->private;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];

	seq_printf(m, "%s\n",
		   (IoAdapter->capi_cfg.
		    cfg_1 & DIVA_XDI_CAPI_CFG_1_GROUP_POPTIMIZATION_ON)
		   ? "1" : "0");
	return 0;
}

static int grp_opt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, grp_opt_proc_show, PDE(inode)->data);
}

static const struct file_operations grp_opt_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= grp_opt_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= grp_opt_proc_write,
};

static ssize_t info_proc_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	diva_os_xdi_adapter_t *a = PDE(file->f_path.dentry->d_inode)->data;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];
	char c[4];

	if (count <= 4)
		return -EINVAL;

	if (copy_from_user(c, buffer, 4))
		return -EFAULT;

	/* this is for test purposes only */
	if (!memcmp(c, "trap", 4)) {
		(*(IoAdapter->os_trap_nfy_Fnc)) (IoAdapter, IoAdapter->ANum);
		return (count);
	}
	return (-EINVAL);
}

static int info_proc_show(struct seq_file *m, void *v)
{
	int i = 0;
	char *p;
	char tmpser[16];
	diva_os_xdi_adapter_t *a = m->private;
	PISDN_ADAPTER IoAdapter = IoAdapters[a->controller - 1];

	seq_printf(m, "Name        : %s\n", IoAdapter->Properties.Name);
	seq_printf(m, "DSP state   : %08x\n", a->dsp_mask);
	seq_printf(m, "Channels    : %02d\n", IoAdapter->Properties.Channels);
	seq_printf(m, "E. max/used : %03d/%03d\n",
		   IoAdapter->e_max, IoAdapter->e_count);
	diva_get_vserial_number(IoAdapter, tmpser);
	seq_printf(m, "Serial      : %s\n", tmpser);
	seq_printf(m, "IRQ         : %d\n", IoAdapter->irq_info.irq_nr);
	seq_printf(m, "CardIndex   : %d\n", a->CardIndex);
	seq_printf(m, "CardOrdinal : %d\n", a->CardOrdinal);
	seq_printf(m, "Controller  : %d\n", a->controller);
	seq_printf(m, "Bus-Type    : %s\n",
		   (a->Bus ==
		    DIVAS_XDI_ADAPTER_BUS_ISA) ? "ISA" : "PCI");
	seq_printf(m, "Port-Name   : %s\n", a->port_name);
	if (a->Bus == DIVAS_XDI_ADAPTER_BUS_PCI) {
		seq_printf(m, "PCI-bus     : %d\n", a->resources.pci.bus);
		seq_printf(m, "PCI-func    : %d\n", a->resources.pci.func);
		for (i = 0; i < 8; i++) {
			if (a->resources.pci.bar[i]) {
				seq_printf(m,
					   "Mem / I/O %d : 0x%x / mapped : 0x%lx",
					   i, a->resources.pci.bar[i],
					   (unsigned long) a->resources.
					   pci.addr[i]);
				if (a->resources.pci.length[i]) {
					seq_printf(m,
						   " / length : %d",
						   a->resources.pci.
						   length[i]);
				}
				seq_putc(m, '\n');
			}
		}
	}
	if ((!a->xdi_adapter.port) &&
	    ((!a->xdi_adapter.ram) ||
	     (!a->xdi_adapter.reset)
	     || (!a->xdi_adapter.cfg))) {
		if (!IoAdapter->irq_info.irq_nr) {
			p = "slave";
		} else {
			p = "out of service";
		}
	} else if (a->xdi_adapter.trapped) {
		p = "trapped";
	} else if (a->xdi_adapter.Initialized) {
		p = "active";
	} else {
		p = "ready";
	}
	seq_printf(m, "State       : %s\n", p);

	return 0;
}

static int info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_proc_show, PDE(inode)->data);
}

static const struct file_operations info_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= info_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= info_proc_write,
};

/*
** adapter proc init/de-init
*/

/* --------------------------------------------------------------------------
   Create adapter directory and files in proc file system
   -------------------------------------------------------------------------- */
int create_adapter_proc(diva_os_xdi_adapter_t *a)
{
	struct proc_dir_entry *de, *pe;
	char tmp[16];

	sprintf(tmp, "%s%d", adapter_dir_name, a->controller);
	if (!(de = proc_mkdir(tmp, proc_net_eicon)))
		return (0);
	a->proc_adapter_dir = (void *) de;

	pe = proc_create_data(info_proc_name, S_IRUGO | S_IWUSR, de,
			      &info_proc_fops, a);
	if (!pe)
		return (0);
	a->proc_info = (void *) pe;

	pe = proc_create_data(grp_opt_proc_name, S_IRUGO | S_IWUSR, de,
			      &grp_opt_proc_fops, a);
	if (pe)
		a->proc_grp_opt = (void *) pe;
	pe = proc_create_data(d_l1_down_proc_name, S_IRUGO | S_IWUSR, de,
			      &d_l1_down_proc_fops, a);
	if (pe)
		a->proc_d_l1_down = (void *) pe;

	DBG_TRC(("proc entry %s created", tmp));

	return (1);
}

/* --------------------------------------------------------------------------
   Remove adapter directory and files in proc file system
   -------------------------------------------------------------------------- */
void remove_adapter_proc(diva_os_xdi_adapter_t *a)
{
	char tmp[16];

	if (a->proc_adapter_dir) {
		if (a->proc_d_l1_down) {
			remove_proc_entry(d_l1_down_proc_name,
					  (struct proc_dir_entry *) a->proc_adapter_dir);
		}
		if (a->proc_grp_opt) {
			remove_proc_entry(grp_opt_proc_name,
					  (struct proc_dir_entry *) a->proc_adapter_dir);
		}
		if (a->proc_info) {
			remove_proc_entry(info_proc_name,
					  (struct proc_dir_entry *) a->proc_adapter_dir);
		}
		sprintf(tmp, "%s%d", adapter_dir_name, a->controller);
		remove_proc_entry(tmp, proc_net_eicon);
		DBG_TRC(("proc entry %s%d removed", adapter_dir_name,
			 a->controller));
	}
}
