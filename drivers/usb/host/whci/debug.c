/*
 * Wireless Host Controller (WHC) debug.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

struct whc_dbg {
	struct dentry *di_f;
	struct dentry *asl_f;
	struct dentry *pzl_f;
};

void qset_print(struct seq_file *s, struct whc_qset *qset)
{
	static const char *qh_type[] = {
		"ctrl", "isoc", "bulk", "intr", "rsvd", "rsvd", "rsvd", "lpintr", };
	struct whc_std *std;
	struct urb *urb = NULL;
	int i;

	seq_printf(s, "qset %08x", (u32)qset->qset_dma);
	if (&qset->list_node == qset->whc->async_list.prev) {
		seq_printf(s, " (dummy)\n");
	} else {
		seq_printf(s, " ep%d%s-%s maxpkt: %d\n",
			   qset->qh.info1 & 0x0f,
			   (qset->qh.info1 >> 4) & 0x1 ? "in" : "out",
			   qh_type[(qset->qh.info1 >> 5) & 0x7],
			   (qset->qh.info1 >> 16) & 0xffff);
	}
	seq_printf(s, "  -> %08x\n", (u32)qset->qh.link);
	seq_printf(s, "  info: %08x %08x %08x\n",
		   qset->qh.info1, qset->qh.info2,  qset->qh.info3);
	seq_printf(s, "  sts: %04x errs: %d curwin: %08x\n",
		   qset->qh.status, qset->qh.err_count, qset->qh.cur_window);
	seq_printf(s, "  TD: sts: %08x opts: %08x\n",
		   qset->qh.overlay.qtd.status, qset->qh.overlay.qtd.options);

	for (i = 0; i < WHCI_QSET_TD_MAX; i++) {
		seq_printf(s, "  %c%c TD[%d]: sts: %08x opts: %08x ptr: %08x\n",
			i == qset->td_start ? 'S' : ' ',
			i == qset->td_end ? 'E' : ' ',
			i, qset->qtd[i].status, qset->qtd[i].options,
			(u32)qset->qtd[i].page_list_ptr);
	}
	seq_printf(s, "  ntds: %d\n", qset->ntds);
	list_for_each_entry(std, &qset->stds, list_node) {
		if (urb != std->urb) {
			urb = std->urb;
			seq_printf(s, "  urb %p transferred: %d bytes\n", urb,
				urb->actual_length);
		}
		if (std->qtd)
			seq_printf(s, "    sTD[%td]: %zu bytes @ %08x\n",
				std->qtd - &qset->qtd[0],
				std->len, std->num_pointers ?
				(u32)(std->pl_virt[0].buf_ptr) : (u32)std->dma_addr);
		else
			seq_printf(s, "    sTD[-]: %zd bytes @ %08x\n",
				std->len, std->num_pointers ?
				(u32)(std->pl_virt[0].buf_ptr) : (u32)std->dma_addr);
	}
}

static int di_print(struct seq_file *s, void *p)
{
	struct whc *whc = s->private;
	char buf[72];
	int d;

	for (d = 0; d < whc->n_devices; d++) {
		struct di_buf_entry *di = &whc->di_buf[d];

		bitmap_scnprintf(buf, sizeof(buf),
				 (unsigned long *)di->availability_info, UWB_NUM_MAS);

		seq_printf(s, "DI[%d]\n", d);
		seq_printf(s, "  availability: %s\n", buf);
		seq_printf(s, "  %c%c key idx: %d dev addr: %d\n",
			   (di->addr_sec_info & WHC_DI_SECURE) ? 'S' : ' ',
			   (di->addr_sec_info & WHC_DI_DISABLE) ? 'D' : ' ',
			   (di->addr_sec_info & WHC_DI_KEY_IDX_MASK) >> 8,
			   (di->addr_sec_info & WHC_DI_DEV_ADDR_MASK));
	}
	return 0;
}

static int asl_print(struct seq_file *s, void *p)
{
	struct whc *whc = s->private;
	struct whc_qset *qset;

	list_for_each_entry(qset, &whc->async_list, list_node) {
		qset_print(s, qset);
	}

	return 0;
}

static int pzl_print(struct seq_file *s, void *p)
{
	struct whc *whc = s->private;
	struct whc_qset *qset;
	int period;

	for (period = 0; period < 5; period++) {
		seq_printf(s, "Period %d\n", period);
		list_for_each_entry(qset, &whc->periodic_list[period], list_node) {
			qset_print(s, qset);
		}
	}
	return 0;
}

static int di_open(struct inode *inode, struct file *file)
{
	return single_open(file, di_print, inode->i_private);
}

static int asl_open(struct inode *inode, struct file *file)
{
	return single_open(file, asl_print, inode->i_private);
}

static int pzl_open(struct inode *inode, struct file *file)
{
	return single_open(file, pzl_print, inode->i_private);
}

static const struct file_operations di_fops = {
	.open    = di_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

static const struct file_operations asl_fops = {
	.open    = asl_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

static const struct file_operations pzl_fops = {
	.open    = pzl_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

void whc_dbg_init(struct whc *whc)
{
	if (whc->wusbhc.pal.debugfs_dir == NULL)
		return;

	whc->dbg = kzalloc(sizeof(struct whc_dbg), GFP_KERNEL);
	if (whc->dbg == NULL)
		return;

	whc->dbg->di_f = debugfs_create_file("di", 0444,
					      whc->wusbhc.pal.debugfs_dir, whc,
					      &di_fops);
	whc->dbg->asl_f = debugfs_create_file("asl", 0444,
					      whc->wusbhc.pal.debugfs_dir, whc,
					      &asl_fops);
	whc->dbg->pzl_f = debugfs_create_file("pzl", 0444,
					      whc->wusbhc.pal.debugfs_dir, whc,
					      &pzl_fops);
}

void whc_dbg_clean_up(struct whc *whc)
{
	if (whc->dbg) {
		debugfs_remove(whc->dbg->pzl_f);
		debugfs_remove(whc->dbg->asl_f);
		debugfs_remove(whc->dbg->di_f);
		kfree(whc->dbg);
	}
}
