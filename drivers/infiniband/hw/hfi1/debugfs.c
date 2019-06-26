/*
 * Copyright(c) 2015-2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ratelimit.h>
#include <linux/fault-inject.h>

#include "hfi.h"
#include "trace.h"
#include "debugfs.h"
#include "device.h"
#include "qp.h"
#include "sdma.h"
#include "fault.h"

static struct dentry *hfi1_dbg_root;

/* wrappers to enforce srcu in seq file */
ssize_t hfi1_seq_read(struct file *file, char __user *buf, size_t size,
		      loff_t *ppos)
{
	struct dentry *d = file->f_path.dentry;
	ssize_t r;

	r = debugfs_file_get(d);
	if (unlikely(r))
		return r;
	r = seq_read(file, buf, size, ppos);
	debugfs_file_put(d);
	return r;
}

loff_t hfi1_seq_lseek(struct file *file, loff_t offset, int whence)
{
	struct dentry *d = file->f_path.dentry;
	loff_t r;

	r = debugfs_file_get(d);
	if (unlikely(r))
		return r;
	r = seq_lseek(file, offset, whence);
	debugfs_file_put(d);
	return r;
}

#define private2dd(file) (file_inode(file)->i_private)
#define private2ppd(file) (file_inode(file)->i_private)

static void *_opcode_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_opcode_stats_perctx *opstats;

	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}

static void *_opcode_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_opcode_stats_perctx *opstats;

	++*pos;
	if (*pos >= ARRAY_SIZE(opstats->stats))
		return NULL;
	return pos;
}

static void _opcode_stats_seq_stop(struct seq_file *s, void *v)
{
}

static int opcode_stats_show(struct seq_file *s, u8 i, u64 packets, u64 bytes)
{
	if (!packets && !bytes)
		return SEQ_SKIP;
	seq_printf(s, "%02x %llu/%llu\n", i,
		   (unsigned long long)packets,
		   (unsigned long long)bytes);

	return 0;
}

static int _opcode_stats_seq_show(struct seq_file *s, void *v)
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
	return opcode_stats_show(s, i, n_packets, n_bytes);
}

DEBUGFS_SEQ_FILE_OPS(opcode_stats);
DEBUGFS_SEQ_FILE_OPEN(opcode_stats)
DEBUGFS_FILE_OPS(opcode_stats);

static void *_tx_opcode_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	return _opcode_stats_seq_start(s, pos);
}

static void *_tx_opcode_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return _opcode_stats_seq_next(s, v, pos);
}

static void _tx_opcode_stats_seq_stop(struct seq_file *s, void *v)
{
}

static int _tx_opcode_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos = v;
	loff_t i = *spos;
	int j;
	u64 n_packets = 0, n_bytes = 0;
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	for_each_possible_cpu(j) {
		struct hfi1_opcode_stats_perctx *s =
			per_cpu_ptr(dd->tx_opstats, j);
		n_packets += s->stats[i].n_packets;
		n_bytes += s->stats[i].n_bytes;
	}
	return opcode_stats_show(s, i, n_packets, n_bytes);
}

DEBUGFS_SEQ_FILE_OPS(tx_opcode_stats);
DEBUGFS_SEQ_FILE_OPEN(tx_opcode_stats)
DEBUGFS_FILE_OPS(tx_opcode_stats);

static void *_ctx_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	if (!*pos)
		return SEQ_START_TOKEN;
	if (*pos >= dd->first_dyn_alloc_ctxt)
		return NULL;
	return pos;
}

static void *_ctx_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	if (v == SEQ_START_TOKEN)
		return pos;

	++*pos;
	if (*pos >= dd->first_dyn_alloc_ctxt)
		return NULL;
	return pos;
}

static void _ctx_stats_seq_stop(struct seq_file *s, void *v)
{
	/* nothing allocated */
}

static int _ctx_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos;
	loff_t i, j;
	u64 n_packets = 0;
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	struct hfi1_ctxtdata *rcd;

	if (v == SEQ_START_TOKEN) {
		seq_puts(s, "Ctx:npkts\n");
		return 0;
	}

	spos = v;
	i = *spos;

	rcd = hfi1_rcd_get_by_index_safe(dd, i);
	if (!rcd)
		return SEQ_SKIP;

	for (j = 0; j < ARRAY_SIZE(rcd->opstats->stats); j++)
		n_packets += rcd->opstats->stats[j].n_packets;

	hfi1_rcd_put(rcd);

	if (!n_packets)
		return SEQ_SKIP;

	seq_printf(s, "  %llu:%llu\n", i, n_packets);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(ctx_stats);
DEBUGFS_SEQ_FILE_OPEN(ctx_stats)
DEBUGFS_FILE_OPS(ctx_stats);

static void *_qp_stats_seq_start(struct seq_file *s, loff_t *pos)
	__acquires(RCU)
{
	struct rvt_qp_iter *iter;
	loff_t n = *pos;

	iter = rvt_qp_iter_init(s->private, 0, NULL);

	/* stop calls rcu_read_unlock */
	rcu_read_lock();

	if (!iter)
		return NULL;

	do {
		if (rvt_qp_iter_next(iter)) {
			kfree(iter);
			return NULL;
		}
	} while (n--);

	return iter;
}

static void *_qp_stats_seq_next(struct seq_file *s, void *iter_ptr,
				loff_t *pos)
	__must_hold(RCU)
{
	struct rvt_qp_iter *iter = iter_ptr;

	(*pos)++;

	if (rvt_qp_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

static void _qp_stats_seq_stop(struct seq_file *s, void *iter_ptr)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int _qp_stats_seq_show(struct seq_file *s, void *iter_ptr)
{
	struct rvt_qp_iter *iter = iter_ptr;

	if (!iter)
		return 0;

	qp_iter_print(s, iter);

	return 0;
}

DEBUGFS_SEQ_FILE_OPS(qp_stats);
DEBUGFS_SEQ_FILE_OPEN(qp_stats)
DEBUGFS_FILE_OPS(qp_stats);

static void *_sdes_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_ibdev *ibd;
	struct hfi1_devdata *dd;

	ibd = (struct hfi1_ibdev *)s->private;
	dd = dd_from_dev(ibd);
	if (!dd->per_sdma || *pos >= dd->num_sdma)
		return NULL;
	return pos;
}

static void *_sdes_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	++*pos;
	if (!dd->per_sdma || *pos >= dd->num_sdma)
		return NULL;
	return pos;
}

static void _sdes_seq_stop(struct seq_file *s, void *v)
{
}

static int _sdes_seq_show(struct seq_file *s, void *v)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	loff_t *spos = v;
	loff_t i = *spos;

	sdma_seqfile_dump_sde(s, &dd->per_sdma[i]);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(sdes);
DEBUGFS_SEQ_FILE_OPEN(sdes)
DEBUGFS_FILE_OPS(sdes);

static void *_rcds_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_ibdev *ibd;
	struct hfi1_devdata *dd;

	ibd = (struct hfi1_ibdev *)s->private;
	dd = dd_from_dev(ibd);
	if (!dd->rcd || *pos >= dd->n_krcv_queues)
		return NULL;
	return pos;
}

static void *_rcds_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	++*pos;
	if (!dd->rcd || *pos >= dd->n_krcv_queues)
		return NULL;
	return pos;
}

static void _rcds_seq_stop(struct seq_file *s, void *v)
{
}

static int _rcds_seq_show(struct seq_file *s, void *v)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	struct hfi1_ctxtdata *rcd;
	loff_t *spos = v;
	loff_t i = *spos;

	rcd = hfi1_rcd_get_by_index_safe(dd, i);
	if (rcd)
		seqfile_dump_rcd(s, rcd);
	hfi1_rcd_put(rcd);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(rcds);
DEBUGFS_SEQ_FILE_OPEN(rcds)
DEBUGFS_FILE_OPS(rcds);

static void *_pios_seq_start(struct seq_file *s, loff_t *pos)
{
	struct hfi1_ibdev *ibd;
	struct hfi1_devdata *dd;

	ibd = (struct hfi1_ibdev *)s->private;
	dd = dd_from_dev(ibd);
	if (!dd->send_contexts || *pos >= dd->num_send_contexts)
		return NULL;
	return pos;
}

static void *_pios_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);

	++*pos;
	if (!dd->send_contexts || *pos >= dd->num_send_contexts)
		return NULL;
	return pos;
}

static void _pios_seq_stop(struct seq_file *s, void *v)
{
}

static int _pios_seq_show(struct seq_file *s, void *v)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	struct send_context_info *sci;
	loff_t *spos = v;
	loff_t i = *spos;
	unsigned long flags;

	spin_lock_irqsave(&dd->sc_lock, flags);
	sci = &dd->send_contexts[i];
	if (sci && sci->type != SC_USER && sci->allocated && sci->sc)
		seqfile_dump_sci(s, i, sci);
	spin_unlock_irqrestore(&dd->sc_lock, flags);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(pios);
DEBUGFS_SEQ_FILE_OPEN(pios)
DEBUGFS_FILE_OPS(pios);

/* read the per-device counters */
static ssize_t dev_counters_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	u64 *counters;
	size_t avail;
	struct hfi1_devdata *dd;
	ssize_t rval;

	dd = private2dd(file);
	avail = hfi1_read_cntrs(dd, NULL, &counters);
	rval =  simple_read_from_buffer(buf, count, ppos, counters, avail);
	return rval;
}

/* read the per-device counters */
static ssize_t dev_names_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	char *names;
	size_t avail;
	struct hfi1_devdata *dd;
	ssize_t rval;

	dd = private2dd(file);
	avail = hfi1_read_cntrs(dd, &names, NULL);
	rval =  simple_read_from_buffer(buf, count, ppos, names, avail);
	return rval;
}

struct counter_info {
	char *name;
	const struct file_operations ops;
};

/*
 * Could use file_inode(file)->i_ino to figure out which file,
 * instead of separate routine for each, but for now, this works...
 */

/* read the per-port names (same for each port) */
static ssize_t portnames_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	char *names;
	size_t avail;
	struct hfi1_devdata *dd;
	ssize_t rval;

	dd = private2dd(file);
	avail = hfi1_read_portcntrs(dd->pport, &names, NULL);
	rval = simple_read_from_buffer(buf, count, ppos, names, avail);
	return rval;
}

/* read the per-port counters */
static ssize_t portcntrs_debugfs_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	u64 *counters;
	size_t avail;
	struct hfi1_pportdata *ppd;
	ssize_t rval;

	ppd = private2ppd(file);
	avail = hfi1_read_portcntrs(ppd, NULL, &counters);
	rval = simple_read_from_buffer(buf, count, ppos, counters, avail);
	return rval;
}

static void check_dyn_flag(u64 scratch0, char *p, int size, int *used,
			   int this_hfi, int hfi, u32 flag, const char *what)
{
	u32 mask;

	mask = flag << (hfi ? CR_DYN_SHIFT : 0);
	if (scratch0 & mask) {
		*used += scnprintf(p + *used, size - *used,
				   "  0x%08x - HFI%d %s in use, %s device\n",
				   mask, hfi, what,
				   this_hfi == hfi ? "this" : "other");
	}
}

static ssize_t asic_flags_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	u64 scratch0;
	char *tmp;
	int ret = 0;
	int size;
	int used;
	int i;

	ppd = private2ppd(file);
	dd = ppd->dd;
	size = PAGE_SIZE;
	used = 0;
	tmp = kmalloc(size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	used += scnprintf(tmp + used, size - used,
			  "Resource flags: 0x%016llx\n", scratch0);

	/* check permanent flag */
	if (scratch0 & CR_THERM_INIT) {
		used += scnprintf(tmp + used, size - used,
				  "  0x%08x - thermal monitoring initialized\n",
				  (u32)CR_THERM_INIT);
	}

	/* check each dynamic flag on each HFI */
	for (i = 0; i < 2; i++) {
		check_dyn_flag(scratch0, tmp, size, &used, dd->hfi1_id, i,
			       CR_SBUS, "SBus");
		check_dyn_flag(scratch0, tmp, size, &used, dd->hfi1_id, i,
			       CR_EPROM, "EPROM");
		check_dyn_flag(scratch0, tmp, size, &used, dd->hfi1_id, i,
			       CR_I2C1, "i2c chain 1");
		check_dyn_flag(scratch0, tmp, size, &used, dd->hfi1_id, i,
			       CR_I2C2, "i2c chain 2");
	}
	used += scnprintf(tmp + used, size - used, "Write bits to clear\n");

	ret = simple_read_from_buffer(buf, count, ppos, tmp, used);
	kfree(tmp);
	return ret;
}

static ssize_t asic_flags_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	char *buff;
	int ret;
	unsigned long long value;
	u64 scratch0;
	u64 clear;

	ppd = private2ppd(file);
	dd = ppd->dd;

	/* zero terminate and read the expected integer */
	buff = memdup_user_nul(buf, count);
	if (IS_ERR(buff))
		return PTR_ERR(buff);

	ret = kstrtoull(buff, 0, &value);
	if (ret)
		goto do_free;
	clear = value;

	/* obtain exclusive access */
	mutex_lock(&dd->asic_data->asic_resource_mutex);
	acquire_hw_mutex(dd);

	scratch0 = read_csr(dd, ASIC_CFG_SCRATCH);
	scratch0 &= ~clear;
	write_csr(dd, ASIC_CFG_SCRATCH, scratch0);
	/* force write to be visible to other HFI on another OS */
	(void)read_csr(dd, ASIC_CFG_SCRATCH);

	release_hw_mutex(dd);
	mutex_unlock(&dd->asic_data->asic_resource_mutex);

	/* return the number of bytes written */
	ret = count;

 do_free:
	kfree(buff);
	return ret;
}

/* read the dc8051 memory */
static ssize_t dc8051_memory_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd = private2ppd(file);
	ssize_t rval;
	void *tmp;
	loff_t start, end;

	/* the checks below expect the position to be positive */
	if (*ppos < 0)
		return -EINVAL;

	tmp = kzalloc(DC8051_DATA_MEM_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/*
	 * Fill in the requested portion of the temporary buffer from the
	 * 8051 memory.  The 8051 memory read is done in terms of 8 bytes.
	 * Adjust start and end to fit.  Skip reading anything if out of
	 * range.
	 */
	start = *ppos & ~0x7;	/* round down */
	if (start < DC8051_DATA_MEM_SIZE) {
		end = (*ppos + count + 7) & ~0x7; /* round up */
		if (end > DC8051_DATA_MEM_SIZE)
			end = DC8051_DATA_MEM_SIZE;
		rval = read_8051_data(ppd->dd, start, end - start,
				      (u64 *)(tmp + start));
		if (rval)
			goto done;
	}

	rval = simple_read_from_buffer(buf, count, ppos, tmp,
				       DC8051_DATA_MEM_SIZE);
done:
	kfree(tmp);
	return rval;
}

static ssize_t debugfs_lcb_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd = private2ppd(file);
	struct hfi1_devdata *dd = ppd->dd;
	unsigned long total, csr_off;
	u64 data;

	if (*ppos < 0)
		return -EINVAL;
	/* only read 8 byte quantities */
	if ((count % 8) != 0)
		return -EINVAL;
	/* offset must be 8-byte aligned */
	if ((*ppos % 8) != 0)
		return -EINVAL;
	/* do nothing if out of range or zero count */
	if (*ppos >= (LCB_END - LCB_START) || !count)
		return 0;
	/* reduce count if needed */
	if (*ppos + count > LCB_END - LCB_START)
		count = (LCB_END - LCB_START) - *ppos;

	csr_off = LCB_START + *ppos;
	for (total = 0; total < count; total += 8, csr_off += 8) {
		if (read_lcb_csr(dd, csr_off, (u64 *)&data))
			break; /* failed */
		if (put_user(data, (unsigned long __user *)(buf + total)))
			break;
	}
	*ppos += total;
	return total;
}

static ssize_t debugfs_lcb_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd = private2ppd(file);
	struct hfi1_devdata *dd = ppd->dd;
	unsigned long total, csr_off, data;

	if (*ppos < 0)
		return -EINVAL;
	/* only write 8 byte quantities */
	if ((count % 8) != 0)
		return -EINVAL;
	/* offset must be 8-byte aligned */
	if ((*ppos % 8) != 0)
		return -EINVAL;
	/* do nothing if out of range or zero count */
	if (*ppos >= (LCB_END - LCB_START) || !count)
		return 0;
	/* reduce count if needed */
	if (*ppos + count > LCB_END - LCB_START)
		count = (LCB_END - LCB_START) - *ppos;

	csr_off = LCB_START + *ppos;
	for (total = 0; total < count; total += 8, csr_off += 8) {
		if (get_user(data, (unsigned long __user *)(buf + total)))
			break;
		if (write_lcb_csr(dd, csr_off, data))
			break; /* failed */
	}
	*ppos += total;
	return total;
}

/*
 * read the per-port QSFP data for ppd
 */
static ssize_t qsfp_debugfs_dump(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct hfi1_pportdata *ppd;
	char *tmp;
	int ret;

	ppd = private2ppd(file);
	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = qsfp_dump(ppd, tmp, PAGE_SIZE);
	if (ret > 0)
		ret = simple_read_from_buffer(buf, count, ppos, tmp, ret);
	kfree(tmp);
	return ret;
}

/* Do an i2c write operation on the chain for the given HFI. */
static ssize_t __i2c_debugfs_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos, u32 target)
{
	struct hfi1_pportdata *ppd;
	char *buff;
	int ret;
	int i2c_addr;
	int offset;
	int total_written;

	ppd = private2ppd(file);

	/* byte offset format: [offsetSize][i2cAddr][offsetHigh][offsetLow] */
	i2c_addr = (*ppos >> 16) & 0xffff;
	offset = *ppos & 0xffff;

	/* explicitly reject invalid address 0 to catch cp and cat */
	if (i2c_addr == 0)
		return -EINVAL;

	buff = memdup_user(buf, count);
	if (IS_ERR(buff))
		return PTR_ERR(buff);

	total_written = i2c_write(ppd, target, i2c_addr, offset, buff, count);
	if (total_written < 0) {
		ret = total_written;
		goto _free;
	}

	*ppos += total_written;

	ret = total_written;

 _free:
	kfree(buff);
	return ret;
}

/* Do an i2c write operation on chain for HFI 0. */
static ssize_t i2c1_debugfs_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	return __i2c_debugfs_write(file, buf, count, ppos, 0);
}

/* Do an i2c write operation on chain for HFI 1. */
static ssize_t i2c2_debugfs_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	return __i2c_debugfs_write(file, buf, count, ppos, 1);
}

/* Do an i2c read operation on the chain for the given HFI. */
static ssize_t __i2c_debugfs_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos, u32 target)
{
	struct hfi1_pportdata *ppd;
	char *buff;
	int ret;
	int i2c_addr;
	int offset;
	int total_read;

	ppd = private2ppd(file);

	/* byte offset format: [offsetSize][i2cAddr][offsetHigh][offsetLow] */
	i2c_addr = (*ppos >> 16) & 0xffff;
	offset = *ppos & 0xffff;

	/* explicitly reject invalid address 0 to catch cp and cat */
	if (i2c_addr == 0)
		return -EINVAL;

	buff = kmalloc(count, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	total_read = i2c_read(ppd, target, i2c_addr, offset, buff, count);
	if (total_read < 0) {
		ret = total_read;
		goto _free;
	}

	*ppos += total_read;

	ret = copy_to_user(buf, buff, total_read);
	if (ret > 0) {
		ret = -EFAULT;
		goto _free;
	}

	ret = total_read;

 _free:
	kfree(buff);
	return ret;
}

/* Do an i2c read operation on chain for HFI 0. */
static ssize_t i2c1_debugfs_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return __i2c_debugfs_read(file, buf, count, ppos, 0);
}

/* Do an i2c read operation on chain for HFI 1. */
static ssize_t i2c2_debugfs_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return __i2c_debugfs_read(file, buf, count, ppos, 1);
}

/* Do a QSFP write operation on the i2c chain for the given HFI. */
static ssize_t __qsfp_debugfs_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos, u32 target)
{
	struct hfi1_pportdata *ppd;
	char *buff;
	int ret;
	int total_written;

	if (*ppos + count > QSFP_PAGESIZE * 4) /* base page + page00-page03 */
		return -EINVAL;

	ppd = private2ppd(file);

	buff = memdup_user(buf, count);
	if (IS_ERR(buff))
		return PTR_ERR(buff);

	total_written = qsfp_write(ppd, target, *ppos, buff, count);
	if (total_written < 0) {
		ret = total_written;
		goto _free;
	}

	*ppos += total_written;

	ret = total_written;

 _free:
	kfree(buff);
	return ret;
}

/* Do a QSFP write operation on i2c chain for HFI 0. */
static ssize_t qsfp1_debugfs_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return __qsfp_debugfs_write(file, buf, count, ppos, 0);
}

/* Do a QSFP write operation on i2c chain for HFI 1. */
static ssize_t qsfp2_debugfs_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return __qsfp_debugfs_write(file, buf, count, ppos, 1);
}

/* Do a QSFP read operation on the i2c chain for the given HFI. */
static ssize_t __qsfp_debugfs_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos, u32 target)
{
	struct hfi1_pportdata *ppd;
	char *buff;
	int ret;
	int total_read;

	if (*ppos + count > QSFP_PAGESIZE * 4) { /* base page + page00-page03 */
		ret = -EINVAL;
		goto _return;
	}

	ppd = private2ppd(file);

	buff = kmalloc(count, GFP_KERNEL);
	if (!buff) {
		ret = -ENOMEM;
		goto _return;
	}

	total_read = qsfp_read(ppd, target, *ppos, buff, count);
	if (total_read < 0) {
		ret = total_read;
		goto _free;
	}

	*ppos += total_read;

	ret = copy_to_user(buf, buff, total_read);
	if (ret > 0) {
		ret = -EFAULT;
		goto _free;
	}

	ret = total_read;

 _free:
	kfree(buff);
 _return:
	return ret;
}

/* Do a QSFP read operation on i2c chain for HFI 0. */
static ssize_t qsfp1_debugfs_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	return __qsfp_debugfs_read(file, buf, count, ppos, 0);
}

/* Do a QSFP read operation on i2c chain for HFI 1. */
static ssize_t qsfp2_debugfs_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	return __qsfp_debugfs_read(file, buf, count, ppos, 1);
}

static int __i2c_debugfs_open(struct inode *in, struct file *fp, u32 target)
{
	struct hfi1_pportdata *ppd;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	ppd = private2ppd(fp);

	ret = acquire_chip_resource(ppd->dd, i2c_target(target), 0);
	if (ret) /* failed - release the module */
		module_put(THIS_MODULE);

	return ret;
}

static int i2c1_debugfs_open(struct inode *in, struct file *fp)
{
	return __i2c_debugfs_open(in, fp, 0);
}

static int i2c2_debugfs_open(struct inode *in, struct file *fp)
{
	return __i2c_debugfs_open(in, fp, 1);
}

static int __i2c_debugfs_release(struct inode *in, struct file *fp, u32 target)
{
	struct hfi1_pportdata *ppd;

	ppd = private2ppd(fp);

	release_chip_resource(ppd->dd, i2c_target(target));
	module_put(THIS_MODULE);

	return 0;
}

static int i2c1_debugfs_release(struct inode *in, struct file *fp)
{
	return __i2c_debugfs_release(in, fp, 0);
}

static int i2c2_debugfs_release(struct inode *in, struct file *fp)
{
	return __i2c_debugfs_release(in, fp, 1);
}

static int __qsfp_debugfs_open(struct inode *in, struct file *fp, u32 target)
{
	struct hfi1_pportdata *ppd;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	ppd = private2ppd(fp);

	ret = acquire_chip_resource(ppd->dd, i2c_target(target), 0);
	if (ret) /* failed - release the module */
		module_put(THIS_MODULE);

	return ret;
}

static int qsfp1_debugfs_open(struct inode *in, struct file *fp)
{
	return __qsfp_debugfs_open(in, fp, 0);
}

static int qsfp2_debugfs_open(struct inode *in, struct file *fp)
{
	return __qsfp_debugfs_open(in, fp, 1);
}

static int __qsfp_debugfs_release(struct inode *in, struct file *fp, u32 target)
{
	struct hfi1_pportdata *ppd;

	ppd = private2ppd(fp);

	release_chip_resource(ppd->dd, i2c_target(target));
	module_put(THIS_MODULE);

	return 0;
}

static int qsfp1_debugfs_release(struct inode *in, struct file *fp)
{
	return __qsfp_debugfs_release(in, fp, 0);
}

static int qsfp2_debugfs_release(struct inode *in, struct file *fp)
{
	return __qsfp_debugfs_release(in, fp, 1);
}

#define DEBUGFS_OPS(nm, readroutine, writeroutine)	\
{ \
	.name = nm, \
	.ops = { \
		.read = readroutine, \
		.write = writeroutine, \
		.llseek = generic_file_llseek, \
	}, \
}

#define DEBUGFS_XOPS(nm, readf, writef, openf, releasef) \
{ \
	.name = nm, \
	.ops = { \
		.read = readf, \
		.write = writef, \
		.llseek = generic_file_llseek, \
		.open = openf, \
		.release = releasef \
	}, \
}

static const struct counter_info cntr_ops[] = {
	DEBUGFS_OPS("counter_names", dev_names_read, NULL),
	DEBUGFS_OPS("counters", dev_counters_read, NULL),
	DEBUGFS_OPS("portcounter_names", portnames_read, NULL),
};

static const struct counter_info port_cntr_ops[] = {
	DEBUGFS_OPS("port%dcounters", portcntrs_debugfs_read, NULL),
	DEBUGFS_XOPS("i2c1", i2c1_debugfs_read, i2c1_debugfs_write,
		     i2c1_debugfs_open, i2c1_debugfs_release),
	DEBUGFS_XOPS("i2c2", i2c2_debugfs_read, i2c2_debugfs_write,
		     i2c2_debugfs_open, i2c2_debugfs_release),
	DEBUGFS_OPS("qsfp_dump%d", qsfp_debugfs_dump, NULL),
	DEBUGFS_XOPS("qsfp1", qsfp1_debugfs_read, qsfp1_debugfs_write,
		     qsfp1_debugfs_open, qsfp1_debugfs_release),
	DEBUGFS_XOPS("qsfp2", qsfp2_debugfs_read, qsfp2_debugfs_write,
		     qsfp2_debugfs_open, qsfp2_debugfs_release),
	DEBUGFS_OPS("asic_flags", asic_flags_read, asic_flags_write),
	DEBUGFS_OPS("dc8051_memory", dc8051_memory_read, NULL),
	DEBUGFS_OPS("lcb", debugfs_lcb_read, debugfs_lcb_write),
};

static void *_sdma_cpu_list_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= num_online_cpus())
		return NULL;

	return pos;
}

static void *_sdma_cpu_list_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	++*pos;
	if (*pos >= num_online_cpus())
		return NULL;

	return pos;
}

static void _sdma_cpu_list_seq_stop(struct seq_file *s, void *v)
{
	/* nothing allocated */
}

static int _sdma_cpu_list_seq_show(struct seq_file *s, void *v)
{
	struct hfi1_ibdev *ibd = (struct hfi1_ibdev *)s->private;
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	loff_t *spos = v;
	loff_t i = *spos;

	sdma_seqfile_dump_cpu_list(s, dd, (unsigned long)i);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(sdma_cpu_list);
DEBUGFS_SEQ_FILE_OPEN(sdma_cpu_list)
DEBUGFS_FILE_OPS(sdma_cpu_list);

void hfi1_dbg_ibdev_init(struct hfi1_ibdev *ibd)
{
	char name[sizeof("port0counters") + 1];
	char link[10];
	struct hfi1_devdata *dd = dd_from_dev(ibd);
	struct hfi1_pportdata *ppd;
	struct dentry *root;
	int unit = dd->unit;
	int i, j;

	if (!hfi1_dbg_root)
		return;
	snprintf(name, sizeof(name), "%s_%d", class_name(), unit);
	snprintf(link, sizeof(link), "%d", unit);
	root = debugfs_create_dir(name, hfi1_dbg_root);
	ibd->hfi1_ibdev_dbg = root;

	ibd->hfi1_ibdev_link =
		debugfs_create_symlink(link, hfi1_dbg_root, name);

	debugfs_create_file("opcode_stats", 0444, root, ibd,
			    &_opcode_stats_file_ops);
	debugfs_create_file("tx_opcode_stats", 0444, root, ibd,
			    &_tx_opcode_stats_file_ops);
	debugfs_create_file("ctx_stats", 0444, root, ibd, &_ctx_stats_file_ops);
	debugfs_create_file("qp_stats", 0444, root, ibd, &_qp_stats_file_ops);
	debugfs_create_file("sdes", 0444, root, ibd, &_sdes_file_ops);
	debugfs_create_file("rcds", 0444, root, ibd, &_rcds_file_ops);
	debugfs_create_file("pios", 0444, root, ibd, &_pios_file_ops);
	debugfs_create_file("sdma_cpu_list", 0444, root, ibd,
			    &_sdma_cpu_list_file_ops);

	/* dev counter files */
	for (i = 0; i < ARRAY_SIZE(cntr_ops); i++)
		debugfs_create_file(cntr_ops[i].name, 0444, root, dd,
				    &cntr_ops[i].ops);

	/* per port files */
	for (ppd = dd->pport, j = 0; j < dd->num_pports; j++, ppd++)
		for (i = 0; i < ARRAY_SIZE(port_cntr_ops); i++) {
			snprintf(name,
				 sizeof(name),
				 port_cntr_ops[i].name,
				 j + 1);
			debugfs_create_file(name,
					    !port_cntr_ops[i].ops.write ?
						    S_IRUGO :
						    S_IRUGO | S_IWUSR,
					    root, ppd, &port_cntr_ops[i].ops);
		}

	hfi1_fault_init_debugfs(ibd);
}

void hfi1_dbg_ibdev_exit(struct hfi1_ibdev *ibd)
{
	if (!hfi1_dbg_root)
		goto out;
	hfi1_fault_exit_debugfs(ibd);
	debugfs_remove(ibd->hfi1_ibdev_link);
	debugfs_remove_recursive(ibd->hfi1_ibdev_dbg);
out:
	ibd->hfi1_ibdev_dbg = NULL;
}

/*
 * driver stats field names, one line per stat, single string.  Used by
 * programs like hfistats to print the stats in a way which works for
 * different versions of drivers, without changing program source.
 * if hfi1_ib_stats changes, this needs to change.  Names need to be
 * 12 chars or less (w/o newline), for proper display by hfistats utility.
 */
static const char * const hfi1_statnames[] = {
	/* must be element 0*/
	"KernIntr",
	"ErrorIntr",
	"Tx_Errs",
	"Rcv_Errs",
	"H/W_Errs",
	"NoPIOBufs",
	"CtxtsOpen",
	"RcvLen_Errs",
	"EgrBufFull",
	"EgrHdrFull"
};

static void *_driver_stats_names_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= ARRAY_SIZE(hfi1_statnames))
		return NULL;
	return pos;
}

static void *_driver_stats_names_seq_next(
	struct seq_file *s,
	void *v,
	loff_t *pos)
{
	++*pos;
	if (*pos >= ARRAY_SIZE(hfi1_statnames))
		return NULL;
	return pos;
}

static void _driver_stats_names_seq_stop(struct seq_file *s, void *v)
{
}

static int _driver_stats_names_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos = v;

	seq_printf(s, "%s\n", hfi1_statnames[*spos]);
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(driver_stats_names);
DEBUGFS_SEQ_FILE_OPEN(driver_stats_names)
DEBUGFS_FILE_OPS(driver_stats_names);

static void *_driver_stats_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= ARRAY_SIZE(hfi1_statnames))
		return NULL;
	return pos;
}

static void *_driver_stats_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	++*pos;
	if (*pos >= ARRAY_SIZE(hfi1_statnames))
		return NULL;
	return pos;
}

static void _driver_stats_seq_stop(struct seq_file *s, void *v)
{
}

static u64 hfi1_sps_ints(void)
{
	unsigned long flags;
	struct hfi1_devdata *dd;
	u64 sps_ints = 0;

	spin_lock_irqsave(&hfi1_devs_lock, flags);
	list_for_each_entry(dd, &hfi1_dev_list, list) {
		sps_ints += get_all_cpu_total(dd->int_counter);
	}
	spin_unlock_irqrestore(&hfi1_devs_lock, flags);
	return sps_ints;
}

static int _driver_stats_seq_show(struct seq_file *s, void *v)
{
	loff_t *spos = v;
	char *buffer;
	u64 *stats = (u64 *)&hfi1_stats;
	size_t sz = seq_get_buf(s, &buffer);

	if (sz < sizeof(u64))
		return SEQ_SKIP;
	/* special case for interrupts */
	if (*spos == 0)
		*(u64 *)buffer = hfi1_sps_ints();
	else
		*(u64 *)buffer = stats[*spos];
	seq_commit(s,  sizeof(u64));
	return 0;
}

DEBUGFS_SEQ_FILE_OPS(driver_stats);
DEBUGFS_SEQ_FILE_OPEN(driver_stats)
DEBUGFS_FILE_OPS(driver_stats);

void hfi1_dbg_init(void)
{
	hfi1_dbg_root  = debugfs_create_dir(DRIVER_NAME, NULL);
	debugfs_create_file("driver_stats_names", 0444, hfi1_dbg_root, NULL,
			    &_driver_stats_names_file_ops);
	debugfs_create_file("driver_stats", 0444, hfi1_dbg_root, NULL,
			    &_driver_stats_file_ops);
}

void hfi1_dbg_exit(void)
{
	debugfs_remove_recursive(hfi1_dbg_root);
	hfi1_dbg_root = NULL;
}
