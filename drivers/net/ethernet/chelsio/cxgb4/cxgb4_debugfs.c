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
#include <linux/ctype.h>

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

/* Trim the size of a seq_tab to the supplied number of rows.  The operation is
 * irreversible.
 */
static int seq_tab_trim(struct seq_tab *p, unsigned int new_rows)
{
	if (new_rows > p->rows)
		return -EINVAL;
	p->rows = new_rows;
	return 0;
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

static int cimq_show(struct seq_file *seq, void *v, int idx)
{
	const u32 *p = v;

	seq_printf(seq, "%#06x: %08x %08x %08x %08x\n", idx * 16, p[0], p[1],
		   p[2], p[3]);
	return 0;
}

static int cim_ibq_open(struct inode *inode, struct file *file)
{
	int ret;
	struct seq_tab *p;
	unsigned int qid = (uintptr_t)inode->i_private & 7;
	struct adapter *adap = inode->i_private - qid;

	p = seq_open_tab(file, CIM_IBQ_SIZE, 4 * sizeof(u32), 0, cimq_show);
	if (!p)
		return -ENOMEM;

	ret = t4_read_cim_ibq(adap, qid, (u32 *)p->data, CIM_IBQ_SIZE * 4);
	if (ret < 0)
		seq_release_private(inode, file);
	else
		ret = 0;
	return ret;
}

static const struct file_operations cim_ibq_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_ibq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

static int cim_obq_open(struct inode *inode, struct file *file)
{
	int ret;
	struct seq_tab *p;
	unsigned int qid = (uintptr_t)inode->i_private & 7;
	struct adapter *adap = inode->i_private - qid;

	p = seq_open_tab(file, 6 * CIM_OBQ_SIZE, 4 * sizeof(u32), 0, cimq_show);
	if (!p)
		return -ENOMEM;

	ret = t4_read_cim_obq(adap, qid, (u32 *)p->data, 6 * CIM_OBQ_SIZE * 4);
	if (ret < 0) {
		seq_release_private(inode, file);
	} else {
		seq_tab_trim(p, ret / 4);
		ret = 0;
	}
	return ret;
}

static const struct file_operations cim_obq_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_obq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

/* Show the PM memory stats.  These stats include:
 *
 * TX:
 *   Read: memory read operation
 *   Write Bypass: cut-through
 *   Bypass + mem: cut-through and save copy
 *
 * RX:
 *   Read: memory read
 *   Write Bypass: cut-through
 *   Flush: payload trim or drop
 */
static int pm_stats_show(struct seq_file *seq, void *v)
{
	static const char * const tx_pm_stats[] = {
		"Read:", "Write bypass:", "Write mem:", "Bypass + mem:"
	};
	static const char * const rx_pm_stats[] = {
		"Read:", "Write bypass:", "Write mem:", "Flush:"
	};

	int i;
	u32 tx_cnt[PM_NSTATS], rx_cnt[PM_NSTATS];
	u64 tx_cyc[PM_NSTATS], rx_cyc[PM_NSTATS];
	struct adapter *adap = seq->private;

	t4_pmtx_get_stats(adap, tx_cnt, tx_cyc);
	t4_pmrx_get_stats(adap, rx_cnt, rx_cyc);

	seq_printf(seq, "%13s %10s  %20s\n", " ", "Tx pcmds", "Tx bytes");
	for (i = 0; i < PM_NSTATS - 1; i++)
		seq_printf(seq, "%-13s %10u  %20llu\n",
			   tx_pm_stats[i], tx_cnt[i], tx_cyc[i]);

	seq_printf(seq, "%13s %10s  %20s\n", " ", "Rx pcmds", "Rx bytes");
	for (i = 0; i < PM_NSTATS - 1; i++)
		seq_printf(seq, "%-13s %10u  %20llu\n",
			   rx_pm_stats[i], rx_cnt[i], rx_cyc[i]);
	return 0;
}

static int pm_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_stats_show, inode->i_private);
}

static ssize_t pm_stats_clear(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct adapter *adap = FILE_DATA(file)->i_private;

	t4_write_reg(adap, PM_RX_STAT_CONFIG_A, 0);
	t4_write_reg(adap, PM_TX_STAT_CONFIG_A, 0);
	return count;
}

static const struct file_operations pm_stats_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = pm_stats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write   = pm_stats_clear
};

/* Format a value in a unit that differs from the value's native unit by the
 * given factor.
 */
static char *unit_conv(char *buf, size_t len, unsigned int val,
		       unsigned int factor)
{
	unsigned int rem = val % factor;

	if (rem == 0) {
		snprintf(buf, len, "%u", val / factor);
	} else {
		while (rem % 10 == 0)
			rem /= 10;
		snprintf(buf, len, "%u.%u", val / factor, rem);
	}
	return buf;
}

static int clk_show(struct seq_file *seq, void *v)
{
	char buf[32];
	struct adapter *adap = seq->private;
	unsigned int cclk_ps = 1000000000 / adap->params.vpd.cclk;  /* in ps */
	u32 res = t4_read_reg(adap, TP_TIMER_RESOLUTION_A);
	unsigned int tre = TIMERRESOLUTION_G(res);
	unsigned int dack_re = DELAYEDACKRESOLUTION_G(res);
	unsigned long long tp_tick_us = (cclk_ps << tre) / 1000000; /* in us */

	seq_printf(seq, "Core clock period: %s ns\n",
		   unit_conv(buf, sizeof(buf), cclk_ps, 1000));
	seq_printf(seq, "TP timer tick: %s us\n",
		   unit_conv(buf, sizeof(buf), (cclk_ps << tre), 1000000));
	seq_printf(seq, "TCP timestamp tick: %s us\n",
		   unit_conv(buf, sizeof(buf),
			     (cclk_ps << TIMESTAMPRESOLUTION_G(res)), 1000000));
	seq_printf(seq, "DACK tick: %s us\n",
		   unit_conv(buf, sizeof(buf), (cclk_ps << dack_re), 1000000));
	seq_printf(seq, "DACK timer: %u us\n",
		   ((cclk_ps << dack_re) / 1000000) *
		   t4_read_reg(adap, TP_DACK_TIMER_A));
	seq_printf(seq, "Retransmit min: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_RXT_MIN_A));
	seq_printf(seq, "Retransmit max: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_RXT_MAX_A));
	seq_printf(seq, "Persist timer min: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_PERS_MIN_A));
	seq_printf(seq, "Persist timer max: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_PERS_MAX_A));
	seq_printf(seq, "Keepalive idle timer: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_KEEP_IDLE_A));
	seq_printf(seq, "Keepalive interval: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_KEEP_INTVL_A));
	seq_printf(seq, "Initial SRTT: %llu us\n",
		   tp_tick_us * INITSRTT_G(t4_read_reg(adap, TP_INIT_SRTT_A)));
	seq_printf(seq, "FINWAIT2 timer: %llu us\n",
		   tp_tick_us * t4_read_reg(adap, TP_FINWAIT2_TIMER_A));

	return 0;
}

DEFINE_SIMPLE_DEBUGFS_FILE(clk);

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

static ssize_t flash_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	loff_t pos = *ppos;
	loff_t avail = FILE_DATA(file)->i_size;
	struct adapter *adap = file->private_data;

	if (pos < 0)
		return -EINVAL;
	if (pos >= avail)
		return 0;
	if (count > avail - pos)
		count = avail - pos;

	while (count) {
		size_t len;
		int ret, ofst;
		u8 data[256];

		ofst = pos & 3;
		len = min(count + ofst, sizeof(data));
		ret = t4_read_flash(adap, pos - ofst, (len + 3) / 4,
				    (u32 *)data, 1);
		if (ret)
			return ret;

		len -= ofst;
		if (copy_to_user(buf, data + ofst, len))
			return -EFAULT;

		buf += len;
		pos += len;
		count -= len;
	}
	count = pos - *ppos;
	*ppos = pos;
	return count;
}

static const struct file_operations flash_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = mem_open,
	.read    = flash_read,
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

/* Display various sensor information.
 */
static int sensors_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;
	u32 param[7], val[7];
	int ret;

	/* Note that if the sensors haven't been initialized and turned on
	 * we'll get values of 0, so treat those as "<unknown>" ...
	 */
	param[0] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		    FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DIAG) |
		    FW_PARAMS_PARAM_Y_V(FW_PARAM_DEV_DIAG_TMP));
	param[1] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		    FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DIAG) |
		    FW_PARAMS_PARAM_Y_V(FW_PARAM_DEV_DIAG_VDD));
	ret = t4_query_params(adap, adap->mbox, adap->fn, 0, 2,
			      param, val);

	if (ret < 0 || val[0] == 0)
		seq_puts(seq, "Temperature: <unknown>\n");
	else
		seq_printf(seq, "Temperature: %dC\n", val[0]);

	if (ret < 0 || val[1] == 0)
		seq_puts(seq, "Core VDD:    <unknown>\n");
	else
		seq_printf(seq, "Core VDD:    %dmV\n", val[1]);

	return 0;
}

DEFINE_SIMPLE_DEBUGFS_FILE(sensors);

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

/*RSS Table.
 */

static int rss_show(struct seq_file *seq, void *v, int idx)
{
	u16 *entry = v;

	seq_printf(seq, "%4d:  %4u  %4u  %4u  %4u  %4u  %4u  %4u  %4u\n",
		   idx * 8, entry[0], entry[1], entry[2], entry[3], entry[4],
		   entry[5], entry[6], entry[7]);
	return 0;
}

static int rss_open(struct inode *inode, struct file *file)
{
	int ret;
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	p = seq_open_tab(file, RSS_NENTRIES / 8, 8 * sizeof(u16), 0, rss_show);
	if (!p)
		return -ENOMEM;

	ret = t4_read_rss(adap, (u16 *)p->data);
	if (ret)
		seq_release_private(inode, file);

	return ret;
}

static const struct file_operations rss_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = rss_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

/* RSS Configuration.
 */

/* Small utility function to return the strings "yes" or "no" if the supplied
 * argument is non-zero.
 */
static const char *yesno(int x)
{
	static const char *yes = "yes";
	static const char *no = "no";

	return x ? yes : no;
}

static int rss_config_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	static const char * const keymode[] = {
		"global",
		"global and per-VF scramble",
		"per-PF and per-VF scramble",
		"per-VF and per-VF scramble",
	};
	u32 rssconf;

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_A);
	seq_printf(seq, "TP_RSS_CONFIG: %#x\n", rssconf);
	seq_printf(seq, "  Tnl4TupEnIpv6: %3s\n", yesno(rssconf &
							TNL4TUPENIPV6_F));
	seq_printf(seq, "  Tnl2TupEnIpv6: %3s\n", yesno(rssconf &
							TNL2TUPENIPV6_F));
	seq_printf(seq, "  Tnl4TupEnIpv4: %3s\n", yesno(rssconf &
							TNL4TUPENIPV4_F));
	seq_printf(seq, "  Tnl2TupEnIpv4: %3s\n", yesno(rssconf &
							TNL2TUPENIPV4_F));
	seq_printf(seq, "  TnlTcpSel:     %3s\n", yesno(rssconf & TNLTCPSEL_F));
	seq_printf(seq, "  TnlIp6Sel:     %3s\n", yesno(rssconf & TNLIP6SEL_F));
	seq_printf(seq, "  TnlVrtSel:     %3s\n", yesno(rssconf & TNLVRTSEL_F));
	seq_printf(seq, "  TnlMapEn:      %3s\n", yesno(rssconf & TNLMAPEN_F));
	seq_printf(seq, "  OfdHashSave:   %3s\n", yesno(rssconf &
							OFDHASHSAVE_F));
	seq_printf(seq, "  OfdVrtSel:     %3s\n", yesno(rssconf & OFDVRTSEL_F));
	seq_printf(seq, "  OfdMapEn:      %3s\n", yesno(rssconf & OFDMAPEN_F));
	seq_printf(seq, "  OfdLkpEn:      %3s\n", yesno(rssconf & OFDLKPEN_F));
	seq_printf(seq, "  Syn4TupEnIpv6: %3s\n", yesno(rssconf &
							SYN4TUPENIPV6_F));
	seq_printf(seq, "  Syn2TupEnIpv6: %3s\n", yesno(rssconf &
							SYN2TUPENIPV6_F));
	seq_printf(seq, "  Syn4TupEnIpv4: %3s\n", yesno(rssconf &
							SYN4TUPENIPV4_F));
	seq_printf(seq, "  Syn2TupEnIpv4: %3s\n", yesno(rssconf &
							SYN2TUPENIPV4_F));
	seq_printf(seq, "  Syn4TupEnIpv6: %3s\n", yesno(rssconf &
							SYN4TUPENIPV6_F));
	seq_printf(seq, "  SynIp6Sel:     %3s\n", yesno(rssconf & SYNIP6SEL_F));
	seq_printf(seq, "  SynVrt6Sel:    %3s\n", yesno(rssconf & SYNVRTSEL_F));
	seq_printf(seq, "  SynMapEn:      %3s\n", yesno(rssconf & SYNMAPEN_F));
	seq_printf(seq, "  SynLkpEn:      %3s\n", yesno(rssconf & SYNLKPEN_F));
	seq_printf(seq, "  ChnEn:         %3s\n", yesno(rssconf &
							CHANNELENABLE_F));
	seq_printf(seq, "  PrtEn:         %3s\n", yesno(rssconf &
							PORTENABLE_F));
	seq_printf(seq, "  TnlAllLkp:     %3s\n", yesno(rssconf &
							TNLALLLOOKUP_F));
	seq_printf(seq, "  VrtEn:         %3s\n", yesno(rssconf &
							VIRTENABLE_F));
	seq_printf(seq, "  CngEn:         %3s\n", yesno(rssconf &
							CONGESTIONENABLE_F));
	seq_printf(seq, "  HashToeplitz:  %3s\n", yesno(rssconf &
							HASHTOEPLITZ_F));
	seq_printf(seq, "  Udp4En:        %3s\n", yesno(rssconf & UDPENABLE_F));
	seq_printf(seq, "  Disable:       %3s\n", yesno(rssconf & DISABLE_F));

	seq_puts(seq, "\n");

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_TNL_A);
	seq_printf(seq, "TP_RSS_CONFIG_TNL: %#x\n", rssconf);
	seq_printf(seq, "  MaskSize:      %3d\n", MASKSIZE_G(rssconf));
	seq_printf(seq, "  MaskFilter:    %3d\n", MASKFILTER_G(rssconf));
	if (CHELSIO_CHIP_VERSION(adapter->params.chip) > CHELSIO_T5) {
		seq_printf(seq, "  HashAll:     %3s\n",
			   yesno(rssconf & HASHALL_F));
		seq_printf(seq, "  HashEth:     %3s\n",
			   yesno(rssconf & HASHETH_F));
	}
	seq_printf(seq, "  UseWireCh:     %3s\n", yesno(rssconf & USEWIRECH_F));

	seq_puts(seq, "\n");

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_OFD_A);
	seq_printf(seq, "TP_RSS_CONFIG_OFD: %#x\n", rssconf);
	seq_printf(seq, "  MaskSize:      %3d\n", MASKSIZE_G(rssconf));
	seq_printf(seq, "  RRCplMapEn:    %3s\n", yesno(rssconf &
							RRCPLMAPEN_F));
	seq_printf(seq, "  RRCplQueWidth: %3d\n", RRCPLQUEWIDTH_G(rssconf));

	seq_puts(seq, "\n");

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_SYN_A);
	seq_printf(seq, "TP_RSS_CONFIG_SYN: %#x\n", rssconf);
	seq_printf(seq, "  MaskSize:      %3d\n", MASKSIZE_G(rssconf));
	seq_printf(seq, "  UseWireCh:     %3s\n", yesno(rssconf & USEWIRECH_F));

	seq_puts(seq, "\n");

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_VRT_A);
	seq_printf(seq, "TP_RSS_CONFIG_VRT: %#x\n", rssconf);
	if (CHELSIO_CHIP_VERSION(adapter->params.chip) > CHELSIO_T5) {
		seq_printf(seq, "  KeyWrAddrX:     %3d\n",
			   KEYWRADDRX_G(rssconf));
		seq_printf(seq, "  KeyExtend:      %3s\n",
			   yesno(rssconf & KEYEXTEND_F));
	}
	seq_printf(seq, "  VfRdRg:        %3s\n", yesno(rssconf & VFRDRG_F));
	seq_printf(seq, "  VfRdEn:        %3s\n", yesno(rssconf & VFRDEN_F));
	seq_printf(seq, "  VfPerrEn:      %3s\n", yesno(rssconf & VFPERREN_F));
	seq_printf(seq, "  KeyPerrEn:     %3s\n", yesno(rssconf & KEYPERREN_F));
	seq_printf(seq, "  DisVfVlan:     %3s\n", yesno(rssconf &
							DISABLEVLAN_F));
	seq_printf(seq, "  EnUpSwt:       %3s\n", yesno(rssconf & ENABLEUP0_F));
	seq_printf(seq, "  HashDelay:     %3d\n", HASHDELAY_G(rssconf));
	if (CHELSIO_CHIP_VERSION(adapter->params.chip) <= CHELSIO_T5)
		seq_printf(seq, "  VfWrAddr:      %3d\n", VFWRADDR_G(rssconf));
	seq_printf(seq, "  KeyMode:       %s\n", keymode[KEYMODE_G(rssconf)]);
	seq_printf(seq, "  VfWrEn:        %3s\n", yesno(rssconf & VFWREN_F));
	seq_printf(seq, "  KeyWrEn:       %3s\n", yesno(rssconf & KEYWREN_F));
	seq_printf(seq, "  KeyWrAddr:     %3d\n", KEYWRADDR_G(rssconf));

	seq_puts(seq, "\n");

	rssconf = t4_read_reg(adapter, TP_RSS_CONFIG_CNG_A);
	seq_printf(seq, "TP_RSS_CONFIG_CNG: %#x\n", rssconf);
	seq_printf(seq, "  ChnCount3:     %3s\n", yesno(rssconf & CHNCOUNT3_F));
	seq_printf(seq, "  ChnCount2:     %3s\n", yesno(rssconf & CHNCOUNT2_F));
	seq_printf(seq, "  ChnCount1:     %3s\n", yesno(rssconf & CHNCOUNT1_F));
	seq_printf(seq, "  ChnCount0:     %3s\n", yesno(rssconf & CHNCOUNT0_F));
	seq_printf(seq, "  ChnUndFlow3:   %3s\n", yesno(rssconf &
							CHNUNDFLOW3_F));
	seq_printf(seq, "  ChnUndFlow2:   %3s\n", yesno(rssconf &
							CHNUNDFLOW2_F));
	seq_printf(seq, "  ChnUndFlow1:   %3s\n", yesno(rssconf &
							CHNUNDFLOW1_F));
	seq_printf(seq, "  ChnUndFlow0:   %3s\n", yesno(rssconf &
							CHNUNDFLOW0_F));
	seq_printf(seq, "  RstChn3:       %3s\n", yesno(rssconf & RSTCHN3_F));
	seq_printf(seq, "  RstChn2:       %3s\n", yesno(rssconf & RSTCHN2_F));
	seq_printf(seq, "  RstChn1:       %3s\n", yesno(rssconf & RSTCHN1_F));
	seq_printf(seq, "  RstChn0:       %3s\n", yesno(rssconf & RSTCHN0_F));
	seq_printf(seq, "  UpdVld:        %3s\n", yesno(rssconf & UPDVLD_F));
	seq_printf(seq, "  Xoff:          %3s\n", yesno(rssconf & XOFF_F));
	seq_printf(seq, "  UpdChn3:       %3s\n", yesno(rssconf & UPDCHN3_F));
	seq_printf(seq, "  UpdChn2:       %3s\n", yesno(rssconf & UPDCHN2_F));
	seq_printf(seq, "  UpdChn1:       %3s\n", yesno(rssconf & UPDCHN1_F));
	seq_printf(seq, "  UpdChn0:       %3s\n", yesno(rssconf & UPDCHN0_F));
	seq_printf(seq, "  Queue:         %3d\n", QUEUE_G(rssconf));

	return 0;
}

DEFINE_SIMPLE_DEBUGFS_FILE(rss_config);

/* RSS Secret Key.
 */

static int rss_key_show(struct seq_file *seq, void *v)
{
	u32 key[10];

	t4_read_rss_key(seq->private, key);
	seq_printf(seq, "%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x\n",
		   key[9], key[8], key[7], key[6], key[5], key[4], key[3],
		   key[2], key[1], key[0]);
	return 0;
}

static int rss_key_open(struct inode *inode, struct file *file)
{
	return single_open(file, rss_key_show, inode->i_private);
}

static ssize_t rss_key_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	int i, j;
	u32 key[10];
	char s[100], *p;
	struct adapter *adap = FILE_DATA(file)->i_private;

	if (count > sizeof(s) - 1)
		return -EINVAL;
	if (copy_from_user(s, buf, count))
		return -EFAULT;
	for (i = count; i > 0 && isspace(s[i - 1]); i--)
		;
	s[i] = '\0';

	for (p = s, i = 9; i >= 0; i--) {
		key[i] = 0;
		for (j = 0; j < 8; j++, p++) {
			if (!isxdigit(*p))
				return -EINVAL;
			key[i] = (key[i] << 4) | hex2val(*p);
		}
	}

	t4_write_rss_key(adap, key, -1);
	return count;
}

static const struct file_operations rss_key_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = rss_key_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write   = rss_key_write
};

/* PF RSS Configuration.
 */

struct rss_pf_conf {
	u32 rss_pf_map;
	u32 rss_pf_mask;
	u32 rss_pf_config;
};

static int rss_pf_config_show(struct seq_file *seq, void *v, int idx)
{
	struct rss_pf_conf *pfconf;

	if (v == SEQ_START_TOKEN) {
		/* use the 0th entry to dump the PF Map Index Size */
		pfconf = seq->private + offsetof(struct seq_tab, data);
		seq_printf(seq, "PF Map Index Size = %d\n\n",
			   LKPIDXSIZE_G(pfconf->rss_pf_map));

		seq_puts(seq, "     RSS              PF   VF    Hash Tuple Enable         Default\n");
		seq_puts(seq, "     Enable       IPF Mask Mask  IPv6      IPv4      UDP   Queue\n");
		seq_puts(seq, " PF  Map Chn Prt  Map Size Size  Four Two  Four Two  Four  Ch1  Ch0\n");
	} else {
		#define G_PFnLKPIDX(map, n) \
			(((map) >> PF1LKPIDX_S*(n)) & PF0LKPIDX_M)
		#define G_PFnMSKSIZE(mask, n) \
			(((mask) >> PF1MSKSIZE_S*(n)) & PF1MSKSIZE_M)

		pfconf = v;
		seq_printf(seq, "%3d  %3s %3s %3s  %3d  %3d  %3d   %3s %3s   %3s %3s   %3s  %3d  %3d\n",
			   idx,
			   yesno(pfconf->rss_pf_config & MAPENABLE_F),
			   yesno(pfconf->rss_pf_config & CHNENABLE_F),
			   yesno(pfconf->rss_pf_config & PRTENABLE_F),
			   G_PFnLKPIDX(pfconf->rss_pf_map, idx),
			   G_PFnMSKSIZE(pfconf->rss_pf_mask, idx),
			   IVFWIDTH_G(pfconf->rss_pf_config),
			   yesno(pfconf->rss_pf_config & IP6FOURTUPEN_F),
			   yesno(pfconf->rss_pf_config & IP6TWOTUPEN_F),
			   yesno(pfconf->rss_pf_config & IP4FOURTUPEN_F),
			   yesno(pfconf->rss_pf_config & IP4TWOTUPEN_F),
			   yesno(pfconf->rss_pf_config & UDPFOURTUPEN_F),
			   CH1DEFAULTQUEUE_G(pfconf->rss_pf_config),
			   CH0DEFAULTQUEUE_G(pfconf->rss_pf_config));

		#undef G_PFnLKPIDX
		#undef G_PFnMSKSIZE
	}
	return 0;
}

static int rss_pf_config_open(struct inode *inode, struct file *file)
{
	struct adapter *adapter = inode->i_private;
	struct seq_tab *p;
	u32 rss_pf_map, rss_pf_mask;
	struct rss_pf_conf *pfconf;
	int pf;

	p = seq_open_tab(file, 8, sizeof(*pfconf), 1, rss_pf_config_show);
	if (!p)
		return -ENOMEM;

	pfconf = (struct rss_pf_conf *)p->data;
	rss_pf_map = t4_read_rss_pf_map(adapter);
	rss_pf_mask = t4_read_rss_pf_mask(adapter);
	for (pf = 0; pf < 8; pf++) {
		pfconf[pf].rss_pf_map = rss_pf_map;
		pfconf[pf].rss_pf_mask = rss_pf_mask;
		t4_read_rss_pf_config(adapter, pf, &pfconf[pf].rss_pf_config);
	}
	return 0;
}

static const struct file_operations rss_pf_config_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = rss_pf_config_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

/* VF RSS Configuration.
 */

struct rss_vf_conf {
	u32 rss_vf_vfl;
	u32 rss_vf_vfh;
};

static int rss_vf_config_show(struct seq_file *seq, void *v, int idx)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "     RSS                     Hash Tuple Enable\n");
		seq_puts(seq, "     Enable   IVF  Dis  Enb  IPv6      IPv4      UDP    Def  Secret Key\n");
		seq_puts(seq, " VF  Chn Prt  Map  VLAN  uP  Four Two  Four Two  Four   Que  Idx       Hash\n");
	} else {
		struct rss_vf_conf *vfconf = v;

		seq_printf(seq, "%3d  %3s %3s  %3d   %3s %3s   %3s %3s   %3s  %3s   %3s  %4d  %3d %#10x\n",
			   idx,
			   yesno(vfconf->rss_vf_vfh & VFCHNEN_F),
			   yesno(vfconf->rss_vf_vfh & VFPRTEN_F),
			   VFLKPIDX_G(vfconf->rss_vf_vfh),
			   yesno(vfconf->rss_vf_vfh & VFVLNEX_F),
			   yesno(vfconf->rss_vf_vfh & VFUPEN_F),
			   yesno(vfconf->rss_vf_vfh & VFIP4FOURTUPEN_F),
			   yesno(vfconf->rss_vf_vfh & VFIP6TWOTUPEN_F),
			   yesno(vfconf->rss_vf_vfh & VFIP4FOURTUPEN_F),
			   yesno(vfconf->rss_vf_vfh & VFIP4TWOTUPEN_F),
			   yesno(vfconf->rss_vf_vfh & ENABLEUDPHASH_F),
			   DEFAULTQUEUE_G(vfconf->rss_vf_vfh),
			   KEYINDEX_G(vfconf->rss_vf_vfh),
			   vfconf->rss_vf_vfl);
	}
	return 0;
}

static int rss_vf_config_open(struct inode *inode, struct file *file)
{
	struct adapter *adapter = inode->i_private;
	struct seq_tab *p;
	struct rss_vf_conf *vfconf;
	int vf;

	p = seq_open_tab(file, 128, sizeof(*vfconf), 1, rss_vf_config_show);
	if (!p)
		return -ENOMEM;

	vfconf = (struct rss_vf_conf *)p->data;
	for (vf = 0; vf < 128; vf++) {
		t4_read_rss_vf_config(adapter, vf, &vfconf[vf].rss_vf_vfl,
				      &vfconf[vf].rss_vf_vfh);
	}
	return 0;
}

static const struct file_operations rss_vf_config_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = rss_vf_config_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

/**
 * ethqset2pinfo - return port_info of an Ethernet Queue Set
 * @adap: the adapter
 * @qset: Ethernet Queue Set
 */
static inline struct port_info *ethqset2pinfo(struct adapter *adap, int qset)
{
	int pidx;

	for_each_port(adap, pidx) {
		struct port_info *pi = adap2pinfo(adap, pidx);

		if (qset >= pi->first_qset &&
		    qset < pi->first_qset + pi->nqsets)
			return pi;
	}

	/* should never happen! */
	BUG_ON(1);
	return NULL;
}

static int sge_qinfo_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;
	int eth_entries = DIV_ROUND_UP(adap->sge.ethqsets, 4);
	int toe_entries = DIV_ROUND_UP(adap->sge.ofldqsets, 4);
	int rdma_entries = DIV_ROUND_UP(adap->sge.rdmaqs, 4);
	int ciq_entries = DIV_ROUND_UP(adap->sge.rdmaciqs, 4);
	int ctrl_entries = DIV_ROUND_UP(MAX_CTRL_QUEUES, 4);
	int i, r = (uintptr_t)v - 1;
	int toe_idx = r - eth_entries;
	int rdma_idx = toe_idx - toe_entries;
	int ciq_idx = rdma_idx - rdma_entries;
	int ctrl_idx =  ciq_idx - ciq_entries;
	int fq_idx =  ctrl_idx - ctrl_entries;

	if (r)
		seq_putc(seq, '\n');

#define S3(fmt_spec, s, v) \
do { \
	seq_printf(seq, "%-12s", s); \
	for (i = 0; i < n; ++i) \
		seq_printf(seq, " %16" fmt_spec, v); \
		seq_putc(seq, '\n'); \
} while (0)
#define S(s, v) S3("s", s, v)
#define T(s, v) S3("u", s, tx[i].v)
#define R(s, v) S3("u", s, rx[i].v)

	if (r < eth_entries) {
		int base_qset = r * 4;
		const struct sge_eth_rxq *rx = &adap->sge.ethrxq[base_qset];
		const struct sge_eth_txq *tx = &adap->sge.ethtxq[base_qset];
		int n = min(4, adap->sge.ethqsets - 4 * r);

		S("QType:", "Ethernet");
		S("Interface:",
		  rx[i].rspq.netdev ? rx[i].rspq.netdev->name : "N/A");
		T("TxQ ID:", q.cntxt_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ CIDX:", q.cidx);
		T("TxQ PIDX:", q.pidx);
#ifdef CONFIG_CHELSIO_T4_DCB
		T("DCB Prio:", dcb_prio);
		S3("u", "DCB PGID:",
		   (ethqset2pinfo(adap, base_qset + i)->dcb.pgid >>
		    4*(7-tx[i].dcb_prio)) & 0xf);
		S3("u", "DCB PFC:",
		   (ethqset2pinfo(adap, base_qset + i)->dcb.pfcen >>
		    1*(7-tx[i].dcb_prio)) & 0x1);
#endif
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",
		   adap->sge.counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);
	} else if (toe_idx < toe_entries) {
		const struct sge_ofld_rxq *rx = &adap->sge.ofldrxq[toe_idx * 4];
		const struct sge_ofld_txq *tx = &adap->sge.ofldtxq[toe_idx * 4];
		int n = min(4, adap->sge.ofldqsets - 4 * toe_idx);

		S("QType:", "TOE");
		T("TxQ ID:", q.cntxt_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ CIDX:", q.cidx);
		T("TxQ PIDX:", q.pidx);
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",
		   adap->sge.counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);
	} else if (rdma_idx < rdma_entries) {
		const struct sge_ofld_rxq *rx =
				&adap->sge.rdmarxq[rdma_idx * 4];
		int n = min(4, adap->sge.rdmaqs - 4 * rdma_idx);

		S("QType:", "RDMA-CPL");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",
		   adap->sge.counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);
	} else if (ciq_idx < ciq_entries) {
		const struct sge_ofld_rxq *rx = &adap->sge.rdmaciq[ciq_idx * 4];
		int n = min(4, adap->sge.rdmaciqs - 4 * ciq_idx);

		S("QType:", "RDMA-CIQ");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",
		   adap->sge.counter_val[rx[i].rspq.pktcnt_idx]);
	} else if (ctrl_idx < ctrl_entries) {
		const struct sge_ctrl_txq *tx = &adap->sge.ctrlq[ctrl_idx * 4];
		int n = min(4, adap->params.nports - 4 * ctrl_idx);

		S("QType:", "Control");
		T("TxQ ID:", q.cntxt_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ CIDX:", q.cidx);
		T("TxQ PIDX:", q.pidx);
	} else if (fq_idx == 0) {
		const struct sge_rspq *evtq = &adap->sge.fw_evtq;

		seq_printf(seq, "%-12s %16s\n", "QType:", "FW event queue");
		seq_printf(seq, "%-12s %16u\n", "RspQ ID:", evtq->abs_id);
		seq_printf(seq, "%-12s %16u\n", "RspQ size:", evtq->size);
		seq_printf(seq, "%-12s %16u\n", "RspQE size:", evtq->iqe_len);
		seq_printf(seq, "%-12s %16u\n", "RspQ CIDX:", evtq->cidx);
		seq_printf(seq, "%-12s %16u\n", "RspQ Gen:", evtq->gen);
		seq_printf(seq, "%-12s %16u\n", "Intr delay:",
			   qtimer_val(adap, evtq));
		seq_printf(seq, "%-12s %16u\n", "Intr pktcnt:",
			   adap->sge.counter_val[evtq->pktcnt_idx]);
	}
#undef R
#undef T
#undef S
#undef S3
return 0;
}

static int sge_queue_entries(const struct adapter *adap)
{
	return DIV_ROUND_UP(adap->sge.ethqsets, 4) +
	       DIV_ROUND_UP(adap->sge.ofldqsets, 4) +
	       DIV_ROUND_UP(adap->sge.rdmaqs, 4) +
	       DIV_ROUND_UP(adap->sge.rdmaciqs, 4) +
	       DIV_ROUND_UP(MAX_CTRL_QUEUES, 4) + 1;
}

static void *sge_queue_start(struct seq_file *seq, loff_t *pos)
{
	int entries = sge_queue_entries(seq->private);

	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static void sge_queue_stop(struct seq_file *seq, void *v)
{
}

static void *sge_queue_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int entries = sge_queue_entries(seq->private);

	++*pos;
	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static const struct seq_operations sge_qinfo_seq_ops = {
	.start = sge_queue_start,
	.next  = sge_queue_next,
	.stop  = sge_queue_stop,
	.show  = sge_qinfo_show
};

static int sge_qinfo_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &sge_qinfo_seq_ops);

	if (!res) {
		struct seq_file *seq = file->private_data;

		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations sge_qinfo_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = sge_qinfo_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

int mem_open(struct inode *inode, struct file *file)
{
	unsigned int mem;
	struct adapter *adap;

	file->private_data = inode->i_private;

	mem = (uintptr_t)file->private_data & 0x3;
	adap = file->private_data - mem;

	(void)t4_fwcache(adap, FW_PARAM_DEV_FWCACHE_FLUSH);

	return 0;
}

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

static void set_debugfs_file_size(struct dentry *de, loff_t size)
{
	if (!IS_ERR(de) && de->d_inode)
		de->d_inode->i_size = size;
}

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
	struct dentry *de;

	static struct t4_debugfs_entry t4_debugfs_files[] = {
		{ "cim_la", &cim_la_fops, S_IRUSR, 0 },
		{ "cim_qcfg", &cim_qcfg_fops, S_IRUSR, 0 },
		{ "clk", &clk_debugfs_fops, S_IRUSR, 0 },
		{ "devlog", &devlog_fops, S_IRUSR, 0 },
		{ "l2t", &t4_l2t_fops, S_IRUSR, 0},
		{ "mps_tcam", &mps_tcam_debugfs_fops, S_IRUSR, 0 },
		{ "rss", &rss_debugfs_fops, S_IRUSR, 0 },
		{ "rss_config", &rss_config_debugfs_fops, S_IRUSR, 0 },
		{ "rss_key", &rss_key_debugfs_fops, S_IRUSR, 0 },
		{ "rss_pf_config", &rss_pf_config_debugfs_fops, S_IRUSR, 0 },
		{ "rss_vf_config", &rss_vf_config_debugfs_fops, S_IRUSR, 0 },
		{ "sge_qinfo", &sge_qinfo_debugfs_fops, S_IRUSR, 0 },
		{ "ibq_tp0",  &cim_ibq_fops, S_IRUSR, 0 },
		{ "ibq_tp1",  &cim_ibq_fops, S_IRUSR, 1 },
		{ "ibq_ulp",  &cim_ibq_fops, S_IRUSR, 2 },
		{ "ibq_sge0", &cim_ibq_fops, S_IRUSR, 3 },
		{ "ibq_sge1", &cim_ibq_fops, S_IRUSR, 4 },
		{ "ibq_ncsi", &cim_ibq_fops, S_IRUSR, 5 },
		{ "obq_ulp0", &cim_obq_fops, S_IRUSR, 0 },
		{ "obq_ulp1", &cim_obq_fops, S_IRUSR, 1 },
		{ "obq_ulp2", &cim_obq_fops, S_IRUSR, 2 },
		{ "obq_ulp3", &cim_obq_fops, S_IRUSR, 3 },
		{ "obq_sge",  &cim_obq_fops, S_IRUSR, 4 },
		{ "obq_ncsi", &cim_obq_fops, S_IRUSR, 5 },
		{ "sensors", &sensors_debugfs_fops, S_IRUSR, 0 },
		{ "pm_stats", &pm_stats_debugfs_fops, S_IRUSR, 0 },
#if IS_ENABLED(CONFIG_IPV6)
		{ "clip_tbl", &clip_tbl_debugfs_fops, S_IRUSR, 0 },
#endif
	};

	/* Debug FS nodes common to all T5 and later adapters.
	 */
	static struct t4_debugfs_entry t5_debugfs_files[] = {
		{ "obq_sge_rx_q0", &cim_obq_fops, S_IRUSR, 6 },
		{ "obq_sge_rx_q1", &cim_obq_fops, S_IRUSR, 7 },
	};

	add_debugfs_files(adap,
			  t4_debugfs_files,
			  ARRAY_SIZE(t4_debugfs_files));
	if (!is_t4(adap->params.chip))
		add_debugfs_files(adap,
				  t5_debugfs_files,
				  ARRAY_SIZE(t5_debugfs_files));

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

	de = debugfs_create_file("flash", S_IRUSR, adap->debugfs_root, adap,
				 &flash_debugfs_fops);
	set_debugfs_file_size(de, adap->params.sf_size);

	return 0;
}
