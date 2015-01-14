/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/string_helpers.h>
#include <linux/sort.h>

#include "cxgb4.h"
#include "t4_regs.h"
#include "t4fw_api.h"
#include "cxgb4_debugfs.h"
#include "clip_tbl.h"
#include "l2t.h"

/* generic seq_file support for showing a table of size rows x width. */
static void *seq_tab_get_idx(struct seq_tab *tb, loff_t pos)
{
	pos -= tb->skip_first;
	return pos >= tb->rows ? NULL : &tb->data[pos * tb->width];
}

static void *seq_tab_start(struct seq_file *seq, loff_t *pos)
{
	struct seq_tab *tb = seq->private;

	if (tb->skip_first && *pos == 0)
		return SEQ_START_TOKEN;

	return seq_tab_get_idx(tb, *pos);
}

static void *seq_tab_next(struct seq_file *seq, void *v, loff_t *pos)
{
	v = seq_tab_get_idx(seq->private, *pos + 1);
	if (v)
		++*pos;
	return v;
}

static void seq_tab_stop(struct seq_file *seq, void *v)
{
}

static int seq_tab_show(struct seq_file *seq, void *v)
{
	const struct seq_tab *tb = seq->private;

	return tb->show(seq, v, ((char *)v - tb->data) / tb->width);
}

static const struct seq_operations seq_tab_ops = {
	.start = seq_tab_start,
	.next  = seq_tab_next,
	.stop  = seq_tab_stop,
	.show  = seq_tab_show
};

struct seq_tab *seq_open_tab(struct file *f, unsigned int rows,
			     unsigned int width, unsigned int have_header,
			     int (*show)(struct seq_file *seq, void *v, int i))
{
	struct seq_tab *p;

	p = __seq_open_private(f, &seq_tab_ops, sizeof(*p) + rows * width);
	if (p) {
		p->show = show;
		p->rows = rows;
		p->width = width;
		p->skip_first = have_header != 0;
	}
	return p;
}

static int cim_la_show(struct seq_file *seq, void *v, int idx)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Status   Data      PC     LS0Stat  LS0Addr "
			 "            LS0Data\n");
	else {
		const u32 *p = v;

		seq_printf(seq,
			   "  %02x  %x%07x %x%07x %08x %08x %08x%08x%08x%08x\n",
			   (p[0] >> 4) & 0xff, p[0] & 0xf, p[1] >> 4,
			   p[1] & 0xf, p[2] >> 4, p[2] & 0xf, p[3], p[4], p[5],
			   p[6], p[7]);
	}
	return 0;
}

static int cim_la_show_3in1(struct seq_file *seq, void *v, int idx)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Status   Data      PC\n");
	} else {
		const u32 *p = v;

		seq_printf(seq, "  %02x   %08x %08x\n", p[5] & 0xff, p[6],
			   p[7]);
		seq_printf(seq, "  %02x   %02x%06x %02x%06x\n",
			   (p[3] >> 8) & 0xff, p[3] & 0xff, p[4] >> 8,
			   p[4] & 0xff, p[5] >> 8);
		seq_printf(seq, "  %02x   %x%07x %x%07x\n", (p[0] >> 4) & 0xff,
			   p[0] & 0xf, p[1] >> 4, p[1] & 0xf, p[2] >> 4);
	}
	return 0;
}

static int cim_la_open(struct inode *inode, struct file *file)
{
	int ret;
	unsigned int cfg;
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	ret = t4_cim_read(adap, UP_UP_DBG_LA_CFG_A, 1, &cfg);
	if (ret)
		return ret;

	p = seq_open_tab(file, adap->params.cim_la_size / 8, 8 * sizeof(u32), 1,
			 cfg & UPDBGLACAPTPCONLY_F ?
			 cim_la_show_3in1 : cim_la_show);
	if (!p)
		return -ENOMEM;

	ret = t4_cim_read_la(adap, (u32 *)p->data, NULL);
	if (ret)
		seq_release_private(inode, file);
	return ret;
}

static const struct file_operations cim_la_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_la_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

static int cim_qcfg_show(struct seq_file *seq, void *v)
{
	static const char * const qname[] = {
		"TP0", "TP1", "ULP", "SGE0", "SGE1", "NC-SI",
		"ULP0", "ULP1", "ULP2", "ULP3", "SGE", "NC-SI",
		"SGE0-RX", "SGE1-RX"
	};

	int i;
	struct adapter *adap = seq->private;
	u16 base[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	u16 size[CIM_NUM_IBQ + CIM_NUM_OBQ_T5];
	u32 stat[(4 * (CIM_NUM_IBQ + CIM_NUM_OBQ_T5))];
	u16 thres[CIM_NUM_IBQ];
	u32 obq_wr_t4[2 * CIM_NUM_OBQ], *wr;
	u32 obq_wr_t5[2 * CIM_NUM_OBQ_T5];
	u32 *p = stat;
	int cim_num_obq = is_t4(adap->params.chip) ?
				CIM_NUM_OBQ : CIM_NUM_OBQ_T5;

	i = t4_cim_read(adap, is_t4(adap->params.chip) ? UP_IBQ_0_RDADDR_A :
			UP_IBQ_0_SHADOW_RDADDR_A,
			ARRAY_SIZE(stat), stat);
	if (!i) {
		if (is_t4(adap->params.chip)) {
			i = t4_cim_read(adap, UP_OBQ_0_REALADDR_A,
					ARRAY_SIZE(obq_wr_t4), obq_wr_t4);
				wr = obq_wr_t4;
		} else {
			i = t4_cim_read(adap, UP_OBQ_0_SHADOW_REALADDR_A,
					ARRAY_SIZE(obq_wr_t5), obq_wr_t5);
				wr = obq_wr_t5;
		}
	}
	if (i)
		return i;

	t4_read_cimq_cfg(adap, base, size, thres);

	seq_printf(seq,
		   "  Queue  Base  Size Thres  RdPtr WrPtr  SOP  EOP Avail\n");
	for (i = 0; i < CIM_NUM_IBQ; i++, p += 4)
		seq_printf(seq, "%7s %5x %5u %5u %6x  %4x %4u %4u %5u\n",
			   qname[i], base[i], size[i], thres[i],
			   IBQRDADDR_G(p[0]), IBQWRADDR_G(p[1]),
			   QUESOPCNT_G(p[3]), QUEEOPCNT_G(p[3]),
			   QUEREMFLITS_G(p[2]) * 16);
	for ( ; i < CIM_NUM_IBQ + cim_num_obq; i++, p += 4, wr += 2)
		seq_printf(seq, "%7s %5x %5u %12x  %4x %4u %4u %5u\n",
			   qname[i], base[i], size[i],
			   QUERDADDR_G(p[0]) & 0x3fff, wr[0] - base[i],
			   QUESOPCNT_G(p[3]), QUEEOPCNT_G(p[3]),
			   QUEREMFLITS_G(p[2]) * 16);
	return 0;
}

static int cim_qcfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, cim_qcfg_show, inode->i_private);
}

static const struct file_operations cim_qcfg_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_qcfg_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Firmware Device Log dump. */
static const char * const devlog_level_strings[] = {
	[FW_DEVLOG_LEVEL_EMERG]		= "EMERG",
	[FW_DEVLOG_LEVEL_CRIT]		= "CRIT",
	[FW_DEVLOG_LEVEL_ERR]		= "ERR",
	[FW_DEVLOG_LEVEL_NOTICE]	= "NOTICE",
	[FW_DEVLOG_LEVEL_INFO]		= "INFO",
	[FW_DEVLOG_LEVEL_DEBUG]		= "DEBUG"
};

static const char * const devlog_facility_strings[] = {
	[FW_DEVLOG_FACILITY_CORE]	= "CORE",
	[FW_DEVLOG_FACILITY_SCHED]	= "SCHED",
	[FW_DEVLOG_FACILITY_TIMER]	= "TIMER",
	[FW_DEVLOG_FACILITY_RES]	= "RES",
	[FW_DEVLOG_FACILITY_HW]		= "HW",
	[FW_DEVLOG_FACILITY_FLR]	= "FLR",
	[FW_DEVLOG_FACILITY_DMAQ]	= "DMAQ",
	[FW_DEVLOG_FACILITY_PHY]	= "PHY",
	[FW_DEVLOG_FACILITY_MAC]	= "MAC",
	[FW_DEVLOG_FACILITY_PORT]	= "PORT",
	[FW_DEVLOG_FACILITY_VI]		= "VI",
	[FW_DEVLOG_FACILITY_FILTER]	= "FILTER",
	[FW_DEVLOG_FACILITY_ACL]	= "ACL",
	[FW_DEVLOG_FACILITY_TM]		= "TM",
	[FW_DEVLOG_FACILITY_QFC]	= "QFC",
	[FW_DEVLOG_FACILITY_DCB]	= "DCB",
	[FW_DEVLOG_FACILITY_ETH]	= "ETH",
	[FW_DEVLOG_FACILITY_OFLD]	= "OFLD",
	[FW_DEVLOG_FACILITY_RI]		= "RI",
	[FW_DEVLOG_FACILITY_ISCSI]	= "ISCSI",
	[FW_DEVLOG_FACILITY_FCOE]	= "FCOE",
	[FW_DEVLOG_FACILITY_FOISCSI]	= "FOISCSI",
	[FW_DEVLOG_FACILITY_FOFCOE]	= "FOFCOE"
};

/* Information gathered by Device Log Open routine for the display routine.
 */
struct devlog_info {
	unsigned int nentries;		/* number of entries in log[] */
	unsigned int first;		/* first [temporal] entry in log[] */
	struct fw_devlog_e log[0];	/* Firmware Device Log */
};

/* Dump a Firmaware Device Log entry.
 */
static int devlog_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%10s  %15s  %8s  %8s  %s\n",
			   "Seq#", "Tstamp", "Level", "Facility", "Message");
	else {
		struct devlog_info *dinfo = seq->private;
		int fidx = (uintptr_t)v - 2;
		unsigned long index;
		struct fw_devlog_e *e;

		/* Get a pointer to the log entry to display.  Skip unused log
		 * entries.
		 */
		index = dinfo->first + fidx;
		if (index >= dinfo->nentries)
			index -= dinfo->nentries;
		e = &dinfo->log[index];
		if (e->timestamp == 0)
			return 0;

		/* Print the message.  This depends on the firmware using
		 * exactly the same formating strings as the kernel so we may
		 * eventually have to put a format interpreter in here ...
		 */
		seq_printf(seq, "%10d  %15llu  %8s  %8s  ",
			   e->seqno, e->timestamp,
			   (e->level < ARRAY_SIZE(devlog_level_strings)
			    ? devlog_level_strings[e->level]
			    : "UNKNOWN"),
			   (e->facility < ARRAY_SIZE(devlog_facility_strings)
			    ? devlog_facility_strings[e->facility]
			    : "UNKNOWN"));
		seq_printf(seq, e->fmt, e->params[0], e->params[1],
			   e->params[2], e->params[3], e->params[4],
			   e->params[5], e->params[6], e->params[7]);
	}
	return 0;
}

/* Sequential File Operations for Device Log.
 */
static inline void *devlog_get_idx(struct devlog_info *dinfo, loff_t pos)
{
	if (pos > dinfo->nentries)
		return NULL;

	return (void *)(uintptr_t)(pos + 1);
}

static void *devlog_start(struct seq_file *seq, loff_t *pos)
{
	struct devlog_info *dinfo = seq->private;

	return (*pos
		? devlog_get_idx(dinfo, *pos)
		: SEQ_START_TOKEN);
}

static void *devlog_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct devlog_info *dinfo = seq->private;

	(*pos)++;
	return devlog_get_idx(dinfo, *pos);
}

static void devlog_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations devlog_seq_ops = {
	.start = devlog_start,
	.next  = devlog_next,
	.stop  = devlog_stop,
	.show  = devlog_show
};

/* Set up for reading the firmware's device log.  We read the entire log here
 * and then display it incrementally in devlog_show().
 */
static int devlog_open(struct inode *inode, struct file *file)
{
	struct adapter *adap = inode->i_private;
	struct devlog_params *dparams = &adap->params.devlog;
	struct devlog_info *dinfo;
	unsigned int index;
	u32 fseqno;
	int ret;

	/* If we don't know where the log is we can't do anything.
	 */
	if (dparams->start == 0)
		return -ENXIO;

	/* Allocate the space to read in the firmware's device log and set up
	 * for the iterated call to our display function.
	 */
	dinfo = __seq_open_private(file, &devlog_seq_ops,
				   sizeof(*dinfo) + dparams->size);
	if (!dinfo)
		return -ENOMEM;

	/* Record the basic log buffer information and read in the raw log.
	 */
	dinfo->nentries = (dparams->size / sizeof(struct fw_devlog_e));
	dinfo->first = 0;
	spin_lock(&adap->win0_lock);
	ret = t4_memory_rw(adap, adap->params.drv_memwin, dparams->memtype,
			   dparams->start, dparams->size, (__be32 *)dinfo->log,
			   T4_MEMORY_READ);
	spin_unlock(&adap->win0_lock);
	if (ret) {
		seq_release_private(inode, file);
		return ret;
	}

	/* Translate log multi-byte integral elements into host native format
	 * and determine where the first entry in the log is.
	 */
	for (fseqno = ~((u32)0), index = 0; index < dinfo->nentries; index++) {
		struct fw_devlog_e *e = &dinfo->log[index];
		int i;
		__u32 seqno;

		if (e->timestamp == 0)
			continue;

		e->timestamp = (__force __be64)be64_to_cpu(e->timestamp);
		seqno = be32_to_cpu(e->seqno);
		for (i = 0; i < 8; i++)
			e->params[i] =
				(__force __be32)be32_to_cpu(e->params[i]);

		if (seqno < fseqno) {
			fseqno = seqno;
			dinfo->first = index;
		}
	}
	return 0;
}

static const struct file_operations devlog_fops = {
	.owner   = THIS_MODULE,
	.open    = devlog_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

static inline void tcamxy2valmask(u64 x, u64 y, u8 *addr, u64 *mask)
{
	*mask = x | y;
	y = (__force u64)cpu_to_be64(y);
	memcpy(addr, (char *)&y + 2, ETH_ALEN);
}

static int mps_tcam_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Idx  Ethernet address     Mask     Vld Ports PF"
			 "  VF              Replication             "
			 "P0 P1 P2 P3  ML\n");
	else {
		u64 mask;
		u8 addr[ETH_ALEN];
		struct adapter *adap = seq->private;
		unsigned int idx = (uintptr_t)v - 2;
		u64 tcamy = t4_read_reg64(adap, MPS_CLS_TCAM_Y_L(idx));
		u64 tcamx = t4_read_reg64(adap, MPS_CLS_TCAM_X_L(idx));
		u32 cls_lo = t4_read_reg(adap, MPS_CLS_SRAM_L(idx));
		u32 cls_hi = t4_read_reg(adap, MPS_CLS_SRAM_H(idx));
		u32 rplc[4] = {0, 0, 0, 0};

		if (tcamx & tcamy) {
			seq_printf(seq, "%3u         -\n", idx);
			goto out;
		}

		if (cls_lo & REPLICATE_F) {
			struct fw_ldst_cmd ldst_cmd;
			int ret;

			memset(&ldst_cmd, 0, sizeof(ldst_cmd));
			ldst_cmd.op_to_addrspace =
				htonl(FW_CMD_OP_V(FW_LDST_CMD) |
				      FW_CMD_REQUEST_F |
				      FW_CMD_READ_F |
				      FW_LDST_CMD_ADDRSPACE_V(
					      FW_LDST_ADDRSPC_MPS));
			ldst_cmd.cycles_to_len16 = htonl(FW_LEN16(ldst_cmd));
			ldst_cmd.u.mps.fid_ctl =
				htons(FW_LDST_CMD_FID_V(FW_LDST_MPS_RPLC) |
				      FW_LDST_CMD_CTL_V(idx));
			ret = t4_wr_mbox(adap, adap->mbox, &ldst_cmd,
					 sizeof(ldst_cmd), &ldst_cmd);
			if (ret)
				dev_warn(adap->pdev_dev, "Can't read MPS "
					 "replication map for idx %d: %d\n",
					 idx, -ret);
			else {
				rplc[0] = ntohl(ldst_cmd.u.mps.rplc31_0);
				rplc[1] = ntohl(ldst_cmd.u.mps.rplc63_32);
				rplc[2] = ntohl(ldst_cmd.u.mps.rplc95_64);
				rplc[3] = ntohl(ldst_cmd.u.mps.rplc127_96);
			}
		}

		tcamxy2valmask(tcamx, tcamy, addr, &mask);
		seq_printf(seq, "%3u %02x:%02x:%02x:%02x:%02x:%02x %012llx"
			   "%3c   %#x%4u%4d",
			   idx, addr[0], addr[1], addr[2], addr[3], addr[4],
			   addr[5], (unsigned long long)mask,
			   (cls_lo & SRAM_VLD_F) ? 'Y' : 'N', PORTMAP_G(cls_hi),
			   PF_G(cls_lo),
			   (cls_lo & VF_VALID_F) ? VF_G(cls_lo) : -1);
		if (cls_lo & REPLICATE_F)
			seq_printf(seq, " %08x %08x %08x %08x",
				   rplc[3], rplc[2], rplc[1], rplc[0]);
		else
			seq_printf(seq, "%36c", ' ');
		seq_printf(seq, "%4u%3u%3u%3u %#x\n",
			   SRAM_PRIO0_G(cls_lo), SRAM_PRIO1_G(cls_lo),
			   SRAM_PRIO2_G(cls_lo), SRAM_PRIO3_G(cls_lo),
			   (cls_lo >> MULTILISTEN0_S) & 0xf);
	}
out:	return 0;
}

static inline void *mps_tcam_get_idx(struct seq_file *seq, loff_t pos)
{
	struct adapter *adap = seq->private;
	int max_mac_addr = is_t4(adap->params.chip) ?
				NUM_MPS_CLS_SRAM_L_INSTANCES :
				NUM_MPS_T5_CLS_SRAM_L_INSTANCES;
	return ((pos <= max_mac_addr) ? (void *)(uintptr_t)(pos + 1) : NULL);
}

static void *mps_tcam_start(struct seq_file *seq, loff_t *pos)
{
	return *pos ? mps_tcam_get_idx(seq, *pos) : SEQ_START_TOKEN;
}

static void *mps_tcam_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return mps_tcam_get_idx(seq, *pos);
}

static void mps_tcam_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations mps_tcam_seq_ops = {
	.start = mps_tcam_start,
	.next  = mps_tcam_next,
	.stop  = mps_tcam_stop,
	.show  = mps_tcam_show
};

static int mps_tcam_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &mps_tcam_seq_ops);

	if (!res) {
		struct seq_file *seq = file->private_data;

		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations mps_tcam_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = mps_tcam_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

#if IS_ENABLED(CONFIG_IPV6)
static int clip_tbl_open(struct inode *inode, struct file *file)
{
	return single_open(file, clip_tbl_show, PDE_DATA(inode));
}

static const struct file_operations clip_tbl_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = clip_tbl_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};
#endif

static ssize_t mem_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	loff_t pos = *ppos;
	loff_t avail = file_inode(file)->i_size;
	unsigned int mem = (uintptr_t)file->private_data & 3;
	struct adapter *adap = file->private_data - mem;
	__be32 *data;
	int ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= avail)
		return 0;
	if (count > avail - pos)
		count = avail - pos;

	data = t4_alloc_mem(count);
	if (!data)
		return -ENOMEM;

	spin_lock(&adap->win0_lock);
	ret = t4_memory_rw(adap, 0, mem, pos, count, data, T4_MEMORY_READ);
	spin_unlock(&adap->win0_lock);
	if (ret) {
		t4_free_mem(data);
		return ret;
	}
	ret = copy_to_user(buf, data, count);

	t4_free_mem(data);
	if (ret)
		return -EFAULT;

	*ppos = pos + count;
	return count;
}

static const struct file_operations mem_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	.read    = mem_read,
	.llseek  = default_llseek,
};

static void add_debugfs_mem(struct adapter *adap, const char *name,
			    unsigned int idx, unsigned int size_mb)
{
	struct dentry *de;

	de = debugfs_create_file(name, S_IRUSR, adap->debugfs_root,
				 (void *)adap + idx, &mem_debugfs_fops);
	if (de && de->d_inode)
		de->d_inode->i_size = size_mb << 20;
}

/* Add an array of Debug FS files.
 */
void add_debugfs_files(struct adapter *adap,
		       struct t4_debugfs_entry *files,
		       unsigned int nfiles)
{
	int i;

	/* debugfs support is best effort */
	for (i = 0; i < nfiles; i++)
		debugfs_create_file(files[i].name, files[i].mode,
				    adap->debugfs_root,
				    (void *)adap + files[i].data,
				    files[i].ops);
}

int t4_setup_debugfs(struct adapter *adap)
{
	int i;
	u32 size;

	static struct t4_debugfs_entry t4_debugfs_files[] = {
		{ "cim_la", &cim_la_fops, S_IRUSR, 0 },
		{ "cim_qcfg", &cim_qcfg_fops, S_IRUSR, 0 },
		{ "devlog", &devlog_fops, S_IRUSR, 0 },
		{ "l2t", &t4_l2t_fops, S_IRUSR, 0},
		{ "mps_tcam", &mps_tcam_debugfs_fops, S_IRUSR, 0 },
#if IS_ENABLED(CONFIG_IPV6)
		{ "clip_tbl", &clip_tbl_debugfs_fops, S_IRUSR, 0 },
#endif
	};

	add_debugfs_files(adap,
			  t4_debugfs_files,
			  ARRAY_SIZE(t4_debugfs_files));

	i = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
	if (i & EDRAM0_ENABLE_F) {
		size = t4_read_reg(adap, MA_EDRAM0_BAR_A);
		add_debugfs_mem(adap, "edc0", MEM_EDC0, EDRAM0_SIZE_G(size));
	}
	if (i & EDRAM1_ENABLE_F) {
		size = t4_read_reg(adap, MA_EDRAM1_BAR_A);
		add_debugfs_mem(adap, "edc1", MEM_EDC1, EDRAM1_SIZE_G(size));
	}
	if (is_t4(adap->params.chip)) {
		size = t4_read_reg(adap, MA_EXT_MEMORY_BAR_A);
		if (i & EXT_MEM_ENABLE_F)
			add_debugfs_mem(adap, "mc", MEM_MC,
					EXT_MEM_SIZE_G(size));
	} else {
		if (i & EXT_MEM0_ENABLE_F) {
			size = t4_read_reg(adap, MA_EXT_MEMORY0_BAR_A);
			add_debugfs_mem(adap, "mc0", MEM_MC0,
					EXT_MEM0_SIZE_G(size));
		}
		if (i & EXT_MEM1_ENABLE_F) {
			size = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
			add_debugfs_mem(adap, "mc1", MEM_MC1,
					EXT_MEM1_SIZE_G(size));
		}
	}
	return 0;
}
