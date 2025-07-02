// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitmap.h>

#include "debugfs.h"
#include "fault.h"
#include "trace.h"

#define HFI1_FAULT_DIR_TX   BIT(0)
#define HFI1_FAULT_DIR_RX   BIT(1)
#define HFI1_FAULT_DIR_TXRX (HFI1_FAULT_DIR_TX | HFI1_FAULT_DIR_RX)

static void *_fault_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_opcode_stats_perctx *opstats;

	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}

static void *_fault_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_opcode_stats_perctx *opstats;

	++*pos;
	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}

static void _fault_stats_seq_stop(struct seq_file *s, void *v)
{
}

static int _fault_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos = v;
	loff_t i = *spos, j;
	u64 n_packets = 0, n_bytes = 0;
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	struct hfi1_ctxtdata *rcd;

	for (j = 0; j < dd->first_dyn_alloc_ctxt; j++) {
		rcd = hfi1_rcd_get_by_index(dd, j);
		if (rcd) {
			n_packets += rcd->opstats->stats[i].n_packets;
			n_bytes += rcd->opstats->stats[i].n_bytes;
		}
		hfi1_rcd_put(rcd);
	}
	for_each_possible_cpu(j) {
		struct hfi1_opcode_stats_perctx *sp =
			per_cpu_ptr(dd->tx_opstats, j);

		n_packets += sp->stats[i].n_packets;
		n_bytes += sp->stats[i].n_bytes;
	}
	if (!n_packets && !n_bytes)
		return SEQ_SKIP;
	if (!ibd->fault->n_rxfaults[i] && !ibd->fault->n_txfaults[i])
		return SEQ_SKIP;
	seq_printf(s, "%02llx %llu/%llu (faults rx:%llu faults: tx:%llu)\n", i,
		   (unsigned long long)n_packets,
		   (unsigned long long)n_bytes,
		   (unsigned long long)ibd->fault->n_rxfaults[i],
		   (unsigned long long)ibd->fault->n_txfaults[i]);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(fault_stats);
DEBUGFS_SEQ_FILE_OPEN(fault_stats);
DEBUGFS_FILE_OPS(fault_stats);

static int fault_opcodes_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return nonseekable_open(inode, file);
}

static ssize_t fault_opcodes_write(struct file *file, const char __user *buf,
				   size_t len, loff_t *pos)
{
	ssize_t ret = 0;
	/* 1280 = 256 opcodes * 4 chars/opcode + 255 commas + NULL */
	size_t copy, datalen = 1280;
	char *data, *token, *ptr, *end;
	struct fault *fault = file->private_data;

	data = kcalloc(datalen, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	copy = min(len, datalen - 1);
	if (copy_from_user(data, buf, copy)) {
		ret = -EFAULT;
		goto free_data;
	}

	ptr = data;
	token = ptr;
	for (ptr = data; *ptr; ptr = end + 1, token = ptr) {
		char *dash;
		unsigned long range_start, range_end, i;
		bool remove = false;
		unsigned long bound = 1U << BITS_PER_BYTE;

		end = strchr(ptr, ',');
		if (end)
			*end = '\0';
		if (token[0] == '-') {
			remove = true;
			token++;
		}
		dash = strchr(token, '-');
		if (dash)
			*dash = '\0';
		if (kstrtoul(token, 0, &range_start))
			break;
		if (dash) {
			token = dash + 1;
			if (kstrtoul(token, 0, &range_end))
				break;
		} else {
			range_end = range_start;
		}
		if (range_start == range_end && range_start == -1UL) {
			bitmap_zero(fault->opcodes, sizeof(fault->opcodes) *
				    BITS_PER_BYTE);
			break;
		}
		/* Check the inputs */
		if (range_start >= bound || range_end >= bound)
			break;

		for (i = range_start; i <= range_end; i++) {
			if (remove)
				clear_bit(i, fault->opcodes);
			else
				set_bit(i, fault->opcodes);
		}
		if (!end)
			break;
	}
	ret = len;

free_data:
	kfree(data);
	return ret;
}

static ssize_t fault_opcodes_read(struct file *file, char __user *buf,
				  size_t len, loff_t *pos)
{
	ssize_t ret = 0;
	char *data;
	size_t datalen = 1280, size = 0; /* see fault_opcodes_write() */
	unsigned long bit = 0, zero = 0;
	struct fault *fault = file->private_data;
	size_t bitsize = sizeof(fault->opcodes) * BITS_PER_BYTE;

	data = kcalloc(datalen, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	bit = find_first_bit(fault->opcodes, bitsize);
	while (bit < bitsize) {
		zero = find_next_zero_bit(fault->opcodes, bitsize, bit);
		if (zero - 1 != bit)
			size += scnprintf(data + size,
					 datalen - size - 1,
					 "0x%lx-0x%lx,", bit, zero - 1);
		else
			size += scnprintf(data + size,
					 datalen - size - 1, "0x%lx,",
					 bit);
		bit = find_next_bit(fault->opcodes, bitsize, zero);
	}
	data[size - 1] = '\n';
	data[size] = '\0';
	ret = simple_read_from_buffer(buf, len, pos, data, size);
	kfree(data);
	return ret;
}

static const struct file_operations __fault_opcodes_fops = {
	.owner = THIS_MODULE,
	.open = fault_opcodes_open,
	.read = fault_opcodes_read,
	.write = fault_opcodes_write,
};

void hfi1_fault_exit_debugfs(struct hfi1_ibdev *ibd)
{
	if (ibd->fault)
		debugfs_remove_recursive(ibd->fault->dir);
	kfree(ibd->fault);
	ibd->fault = NULL;
}

int hfi1_fault_init_debugfs(struct hfi1_ibdev *ibd)
{
	struct dentry *parent = ibd->hfi1_ibdev_dbg;
	struct dentry *fault_dir;

	ibd->fault = kzalloc(sizeof(*ibd->fault), GFP_KERNEL);
	if (!ibd->fault)
		return -ENOMEM;

	ibd->fault->attr.interval = 1;
	ibd->fault->attr.require_end = ULONG_MAX;
	ibd->fault->attr.stacktrace_depth = 32;
	ibd->fault->attr.dname = NULL;
	ibd->fault->attr.verbose = 0;
	ibd->fault->enable = false;
	ibd->fault->opcode = false;
	ibd->fault->fault_skip = 0;
	ibd->fault->skip = 0;
	ibd->fault->direction = HFI1_FAULT_DIR_TXRX;
	ibd->fault->suppress_err = false;
	bitmap_zero(ibd->fault->opcodes,
		    sizeof(ibd->fault->opcodes) * BITS_PER_BYTE);

	fault_dir =
		fault_create_debugfs_attr("fault", parent, &ibd->fault->attr);
	if (IS_ERR(fault_dir)) {
		kfree(ibd->fault);
		ibd->fault = NULL;
		return -ENOENT;
	}
	ibd->fault->dir = fault_dir;

	debugfs_create_file("fault_stats", 0444, fault_dir, ibd,
			    &_fault_stats_file_ops);
	debugfs_create_bool("enable", 0600, fault_dir, &ibd->fault->enable);
	debugfs_create_bool("suppress_err", 0600, fault_dir,
			    &ibd->fault->suppress_err);
	debugfs_create_bool("opcode_mode", 0600, fault_dir,
			    &ibd->fault->opcode);
	debugfs_create_file("opcodes", 0600, fault_dir, ibd->fault,
			    &__fault_opcodes_fops);
	debugfs_create_u64("skip_pkts", 0600, fault_dir,
			   &ibd->fault->fault_skip);
	debugfs_create_u64("skip_usec", 0600, fault_dir,
			   &ibd->fault->fault_skip_usec);
	debugfs_create_u8("direction", 0600, fault_dir, &ibd->fault->direction);

	return 0;
}

bool hfi1_dbg_fault_suppress_err(struct hfi1_ibdev *ibd)
{
	if (ibd->fault)
		return ibd->fault->suppress_err;
	return false;
}

static bool __hfi1_should_fault(struct hfi1_ibdev *ibd, u32 opcode,
				u8 direction)
{
	bool ret = false;

	if (!ibd->fault || !ibd->fault->enable)
		return false;
	if (!(ibd->fault->direction & direction))
		return false;
	if (ibd->fault->opcode) {
		if (bitmap_empty(ibd->fault->opcodes,
				 (sizeof(ibd->fault->opcodes) *
				  BITS_PER_BYTE)))
			return false;
		if (!(test_bit(opcode, ibd->fault->opcodes)))
			return false;
	}
	if (ibd->fault->fault_skip_usec &&
	    time_before(jiffies, ibd->fault->skip_usec))
		return false;
	if (ibd->fault->fault_skip && ibd->fault->skip) {
		ibd->fault->skip--;
		return false;
	}
	ret = should_fail(&ibd->fault->attr, 1);
	if (ret) {
		ibd->fault->skip = ibd->fault->fault_skip;
		ibd->fault->skip_usec = jiffies +
			usecs_to_jiffies(ibd->fault->fault_skip_usec);
	}
	return ret;
}

bool hfi1_dbg_should_fault_tx(struct rvt_qp *qp, u32 opcode)
{
	struct hfi1_ibdev *ibd = to_idev(qp->ibqp.device);

	if (__hfi1_should_fault(ibd, opcode, HFI1_FAULT_DIR_TX)) {
		trace_hfi1_fault_opcode(qp, opcode);
		ibd->fault->n_txfaults[opcode]++;
		return true;
	}
	return false;
}

bool hfi1_dbg_should_fault_rx(struct hfi1_packet *packet)
{
	struct hfi1_ibdev *ibd = &packet->rcd->dd->verbs_dev;

	if (__hfi1_should_fault(ibd, packet->opcode, HFI1_FAULT_DIR_RX)) {
		trace_hfi1_fault_packet(packet);
		ibd->fault->n_rxfaults[packet->opcode]++;
		return true;
	}
	return false;
}
