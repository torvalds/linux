/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include "adf_accel_devices.h"
#include "adf_transport_internal.h"
#include "adf_transport_access_macros.h"

static DEFINE_MUTEX(ring_read_lock);
static DEFINE_MUTEX(bank_read_lock);

static void *adf_ring_start(struct seq_file *sfile, loff_t *pos)
{
	struct adf_etr_ring_data *ring = sfile->private;

	mutex_lock(&ring_read_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos >= (ADF_SIZE_TO_RING_SIZE_IN_BYTES(ring->ring_size) /
		     ADF_MSG_SIZE_TO_BYTES(ring->msg_size)))
		return NULL;

	return ring->base_addr +
		(ADF_MSG_SIZE_TO_BYTES(ring->msg_size) * (*pos)++);
}

static void *adf_ring_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	struct adf_etr_ring_data *ring = sfile->private;

	if (*pos >= (ADF_SIZE_TO_RING_SIZE_IN_BYTES(ring->ring_size) /
		     ADF_MSG_SIZE_TO_BYTES(ring->msg_size)))
		return NULL;

	return ring->base_addr +
		(ADF_MSG_SIZE_TO_BYTES(ring->msg_size) * (*pos)++);
}

static int adf_ring_show(struct seq_file *sfile, void *v)
{
	struct adf_etr_ring_data *ring = sfile->private;
	struct adf_etr_bank_data *bank = ring->bank;
	uint32_t *msg = v;
	void __iomem *csr = ring->bank->csr_addr;
	int i, x;

	if (v == SEQ_START_TOKEN) {
		int head, tail, empty;

		head = READ_CSR_RING_HEAD(csr, bank->bank_number,
					  ring->ring_number);
		tail = READ_CSR_RING_TAIL(csr, bank->bank_number,
					  ring->ring_number);
		empty = READ_CSR_E_STAT(csr, bank->bank_number);

		seq_puts(sfile, "------- Ring configuration -------\n");
		seq_printf(sfile, "ring name: %s\n",
			   ring->ring_debug->ring_name);
		seq_printf(sfile, "ring num %d, bank num %d\n",
			   ring->ring_number, ring->bank->bank_number);
		seq_printf(sfile, "head %x, tail %x, empty: %d\n",
			   head, tail, (empty & 1 << ring->ring_number)
			   >> ring->ring_number);
		seq_printf(sfile, "ring size %d, msg size %d\n",
			   ADF_SIZE_TO_RING_SIZE_IN_BYTES(ring->ring_size),
			   ADF_MSG_SIZE_TO_BYTES(ring->msg_size));
		seq_puts(sfile, "----------- Ring data ------------\n");
		return 0;
	}
	seq_printf(sfile, "%p:", msg);
	x = 0;
	i = 0;
	for (; i < (ADF_MSG_SIZE_TO_BYTES(ring->msg_size) >> 2); i++) {
		seq_printf(sfile, " %08X", *(msg + i));
		if ((ADF_MSG_SIZE_TO_BYTES(ring->msg_size) >> 2) != i + 1 &&
		    (++x == 8)) {
			seq_printf(sfile, "\n%p:", msg + i + 1);
			x = 0;
		}
	}
	seq_puts(sfile, "\n");
	return 0;
}

static void adf_ring_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&ring_read_lock);
}

static const struct seq_operations adf_ring_sops = {
	.start = adf_ring_start,
	.next = adf_ring_next,
	.stop = adf_ring_stop,
	.show = adf_ring_show
};

static int adf_ring_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_ring_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations adf_ring_debug_fops = {
	.open = adf_ring_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

int adf_ring_debugfs_add(struct adf_etr_ring_data *ring, const char *name)
{
	struct adf_etr_ring_debug_entry *ring_debug;
	char entry_name[8];

	ring_debug = kzalloc(sizeof(*ring_debug), GFP_KERNEL);
	if (!ring_debug)
		return -ENOMEM;

	strlcpy(ring_debug->ring_name, name, sizeof(ring_debug->ring_name));
	snprintf(entry_name, sizeof(entry_name), "ring_%02d",
		 ring->ring_number);

	ring_debug->debug = debugfs_create_file(entry_name, S_IRUSR,
						ring->bank->bank_debug_dir,
						ring, &adf_ring_debug_fops);
	if (!ring_debug->debug) {
		pr_err("QAT: Failed to create ring debug entry.\n");
		kfree(ring_debug);
		return -EFAULT;
	}
	ring->ring_debug = ring_debug;
	return 0;
}

void adf_ring_debugfs_rm(struct adf_etr_ring_data *ring)
{
	if (ring->ring_debug) {
		debugfs_remove(ring->ring_debug->debug);
		kfree(ring->ring_debug);
		ring->ring_debug = NULL;
	}
}

static void *adf_bank_start(struct seq_file *sfile, loff_t *pos)
{
	mutex_lock(&bank_read_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos >= ADF_ETR_MAX_RINGS_PER_BANK)
		return NULL;

	return pos;
}

static void *adf_bank_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	if (++(*pos) >= ADF_ETR_MAX_RINGS_PER_BANK)
		return NULL;

	return pos;
}

static int adf_bank_show(struct seq_file *sfile, void *v)
{
	struct adf_etr_bank_data *bank = sfile->private;

	if (v == SEQ_START_TOKEN) {
		seq_printf(sfile, "------- Bank %d configuration -------\n",
			   bank->bank_number);
	} else {
		int ring_id = *((int *)v) - 1;
		struct adf_etr_ring_data *ring = &bank->rings[ring_id];
		void __iomem *csr = bank->csr_addr;
		int head, tail, empty;

		if (!(bank->ring_mask & 1 << ring_id))
			return 0;

		head = READ_CSR_RING_HEAD(csr, bank->bank_number,
					  ring->ring_number);
		tail = READ_CSR_RING_TAIL(csr, bank->bank_number,
					  ring->ring_number);
		empty = READ_CSR_E_STAT(csr, bank->bank_number);

		seq_printf(sfile,
			   "ring num %02d, head %04x, tail %04x, empty: %d\n",
			   ring->ring_number, head, tail,
			   (empty & 1 << ring->ring_number) >>
			   ring->ring_number);
	}
	return 0;
}

static void adf_bank_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&bank_read_lock);
}

static const struct seq_operations adf_bank_sops = {
	.start = adf_bank_start,
	.next = adf_bank_next,
	.stop = adf_bank_stop,
	.show = adf_bank_show
};

static int adf_bank_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_bank_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations adf_bank_debug_fops = {
	.open = adf_bank_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

int adf_bank_debugfs_add(struct adf_etr_bank_data *bank)
{
	struct adf_accel_dev *accel_dev = bank->accel_dev;
	struct dentry *parent = accel_dev->transport->debug;
	char name[8];

	snprintf(name, sizeof(name), "bank_%02d", bank->bank_number);
	bank->bank_debug_dir = debugfs_create_dir(name, parent);
	if (!bank->bank_debug_dir) {
		pr_err("QAT: Failed to create bank debug dir.\n");
		return -EFAULT;
	}

	bank->bank_debug_cfg = debugfs_create_file("config", S_IRUSR,
						   bank->bank_debug_dir, bank,
						   &adf_bank_debug_fops);
	if (!bank->bank_debug_cfg) {
		pr_err("QAT: Failed to create bank debug entry.\n");
		debugfs_remove(bank->bank_debug_dir);
		return -EFAULT;
	}
	return 0;
}

void adf_bank_debugfs_rm(struct adf_etr_bank_data *bank)
{
	debugfs_remove(bank->bank_debug_cfg);
	debugfs_remove(bank->bank_debug_dir);
}
