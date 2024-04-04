// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <uapi/linux/idxd.h>
#include "idxd.h"
#include "registers.h"

static struct dentry *idxd_debugfs_dir;

static void dump_event_entry(struct idxd_device *idxd, struct seq_file *s,
			     u16 index, int *count, bool processed)
{
	struct idxd_evl *evl = idxd->evl;
	struct dsa_evl_entry *entry;
	struct dsa_completion_record *cr;
	u64 *raw;
	int i;
	int evl_strides = evl_ent_size(idxd) / sizeof(u64);

	entry = (struct dsa_evl_entry *)evl->log + index;

	if (!entry->e.desc_valid)
		return;

	seq_printf(s, "Event Log entry %d (real index %u) processed: %u\n",
		   *count, index, processed);

	seq_printf(s, "desc valid %u wq idx valid %u\n"
		   "batch %u fault rw %u priv %u error 0x%x\n"
		   "wq idx %u op %#x pasid %u batch idx %u\n"
		   "fault addr %#llx\n",
		   entry->e.desc_valid, entry->e.wq_idx_valid,
		   entry->e.batch, entry->e.fault_rw, entry->e.priv,
		   entry->e.error, entry->e.wq_idx, entry->e.operation,
		   entry->e.pasid, entry->e.batch_idx, entry->e.fault_addr);

	cr = &entry->cr;
	seq_printf(s, "status %#x result %#x fault_info %#x bytes_completed %u\n"
		   "fault addr %#llx inv flags %#x\n\n",
		   cr->status, cr->result, cr->fault_info, cr->bytes_completed,
		   cr->fault_addr, cr->invalid_flags);

	raw = (u64 *)entry;

	for (i = 0; i < evl_strides; i++)
		seq_printf(s, "entry[%d] = %#llx\n", i, raw[i]);

	seq_puts(s, "\n");
	*count += 1;
}

static int debugfs_evl_show(struct seq_file *s, void *d)
{
	struct idxd_device *idxd = s->private;
	struct idxd_evl *evl = idxd->evl;
	union evl_status_reg evl_status;
	u16 h, t, evl_size, i;
	int count = 0;
	bool processed = true;

	if (!evl || !evl->log)
		return 0;

	mutex_lock(&evl->lock);

	evl_status.bits = ioread64(idxd->reg_base + IDXD_EVLSTATUS_OFFSET);
	t = evl_status.tail;
	h = evl_status.head;
	evl_size = evl->size;

	seq_printf(s, "Event Log head %u tail %u interrupt pending %u\n\n",
		   evl_status.head, evl_status.tail, evl_status.int_pending);

	i = t;
	while (1) {
		i = (i + 1) % evl_size;
		if (i == t)
			break;

		if (processed && i == h)
			processed = false;
		dump_event_entry(idxd, s, i, &count, processed);
	}

	mutex_unlock(&evl->lock);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(debugfs_evl);

int idxd_device_init_debugfs(struct idxd_device *idxd)
{
	if (IS_ERR_OR_NULL(idxd_debugfs_dir))
		return 0;

	idxd->dbgfs_dir = debugfs_create_dir(dev_name(idxd_confdev(idxd)), idxd_debugfs_dir);
	if (IS_ERR(idxd->dbgfs_dir))
		return PTR_ERR(idxd->dbgfs_dir);

	if (idxd->evl) {
		idxd->dbgfs_evl_file = debugfs_create_file("event_log", 0400,
							   idxd->dbgfs_dir, idxd,
							   &debugfs_evl_fops);
		if (IS_ERR(idxd->dbgfs_evl_file)) {
			debugfs_remove_recursive(idxd->dbgfs_dir);
			idxd->dbgfs_dir = NULL;
			return PTR_ERR(idxd->dbgfs_evl_file);
		}
	}

	return 0;
}

void idxd_device_remove_debugfs(struct idxd_device *idxd)
{
	debugfs_remove_recursive(idxd->dbgfs_dir);
}

int idxd_init_debugfs(void)
{
	if (!debugfs_initialized())
		return 0;

	idxd_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(idxd_debugfs_dir))
		return  PTR_ERR(idxd_debugfs_dir);
	return 0;
}

void idxd_remove_debugfs(void)
{
	debugfs_remove_recursive(idxd_debugfs_dir);
}
