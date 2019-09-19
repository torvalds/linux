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
#include "t4_values.h"
#include "t4fw_api.h"
#include "cxgb4_debugfs.h"
#include "clip_tbl.h"
#include "l2t.h"
#include "cudbg_if.h"
#include "cudbg_lib_common.h"
#include "cudbg_entity.h"
#include "cudbg_lib.h"

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

static int cim_la_show_t6(struct seq_file *seq, void *v, int idx)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Status   Inst    Data      PC     LS0Stat  "
			 "LS0Addr  LS0Data  LS1Stat  LS1Addr  LS1Data\n");
	} else {
		const u32 *p = v;

		seq_printf(seq, "  %02x   %04x%04x %04x%04x %04x%04x %08x %08x %08x %08x %08x %08x\n",
			   (p[9] >> 16) & 0xff,       /* Status */
			   p[9] & 0xffff, p[8] >> 16, /* Inst */
			   p[8] & 0xffff, p[7] >> 16, /* Data */
			   p[7] & 0xffff, p[6] >> 16, /* PC */
			   p[2], p[1], p[0],      /* LS0 Stat, Addr and Data */
			   p[5], p[4], p[3]);     /* LS1 Stat, Addr and Data */
	}
	return 0;
}

static int cim_la_show_pc_t6(struct seq_file *seq, void *v, int idx)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Status   Inst    Data      PC\n");
	} else {
		const u32 *p = v;

		seq_printf(seq, "  %02x   %08x %08x %08x\n",
			   p[3] & 0xff, p[2], p[1], p[0]);
		seq_printf(seq, "  %02x   %02x%06x %02x%06x %02x%06x\n",
			   (p[6] >> 8) & 0xff, p[6] & 0xff, p[5] >> 8,
			   p[5] & 0xff, p[4] >> 8, p[4] & 0xff, p[3] >> 8);
		seq_printf(seq, "  %02x   %04x%04x %04x%04x %04x%04x\n",
			   (p[9] >> 16) & 0xff, p[9] & 0xffff, p[8] >> 16,
			   p[8] & 0xffff, p[7] >> 16, p[7] & 0xffff,
			   p[6] >> 16);
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

	if (is_t6(adap->params.chip)) {
		/* +1 to account for integer division of CIMLA_SIZE/10 */
		p = seq_open_tab(file, (adap->params.cim_la_size / 10) + 1,
				 10 * sizeof(u32), 1,
				 cfg & UPDBGLACAPTPCONLY_F ?
					cim_la_show_pc_t6 : cim_la_show_t6);
	} else {
		p = seq_open_tab(file, adap->params.cim_la_size / 8,
				 8 * sizeof(u32), 1,
				 cfg & UPDBGLACAPTPCONLY_F ? cim_la_show_3in1 :
							     cim_la_show);
	}
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

static int cim_pif_la_show(struct seq_file *seq, void *v, int idx)
{
	const u32 *p = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Cntl ID DataBE   Addr                 Data\n");
	} else if (idx < CIM_PIFLA_SIZE) {
		seq_printf(seq, " %02x  %02x  %04x  %08x %08x%08x%08x%08x\n",
			   (p[5] >> 22) & 0xff, (p[5] >> 16) & 0x3f,
			   p[5] & 0xffff, p[4], p[3], p[2], p[1], p[0]);
	} else {
		if (idx == CIM_PIFLA_SIZE)
			seq_puts(seq, "\nCntl ID               Data\n");
		seq_printf(seq, " %02x  %02x %08x%08x%08x%08x\n",
			   (p[4] >> 6) & 0xff, p[4] & 0x3f,
			   p[3], p[2], p[1], p[0]);
	}
	return 0;
}

static int cim_pif_la_open(struct inode *inode, struct file *file)
{
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	p = seq_open_tab(file, 2 * CIM_PIFLA_SIZE, 6 * sizeof(u32), 1,
			 cim_pif_la_show);
	if (!p)
		return -ENOMEM;

	t4_cim_read_pif_la(adap, (u32 *)p->data,
			   (u32 *)p->data + 6 * CIM_PIFLA_SIZE, NULL, NULL);
	return 0;
}

static const struct file_operations cim_pif_la_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_pif_la_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private
};

static int cim_ma_la_show(struct seq_file *seq, void *v, int idx)
{
	const u32 *p = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "\n");
	} else if (idx < CIM_MALA_SIZE) {
		seq_printf(seq, "%02x%08x%08x%08x%08x\n",
			   p[4], p[3], p[2], p[1], p[0]);
	} else {
		if (idx == CIM_MALA_SIZE)
			seq_puts(seq,
				 "\nCnt ID Tag UE       Data       RDY VLD\n");
		seq_printf(seq, "%3u %2u  %x   %u %08x%08x  %u   %u\n",
			   (p[2] >> 10) & 0xff, (p[2] >> 7) & 7,
			   (p[2] >> 3) & 0xf, (p[2] >> 2) & 1,
			   (p[1] >> 2) | ((p[2] & 3) << 30),
			   (p[0] >> 2) | ((p[1] & 3) << 30), (p[0] >> 1) & 1,
			   p[0] & 1);
	}
	return 0;
}

static int cim_ma_la_open(struct inode *inode, struct file *file)
{
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	p = seq_open_tab(file, 2 * CIM_MALA_SIZE, 5 * sizeof(u32), 1,
			 cim_ma_la_show);
	if (!p)
		return -ENOMEM;

	t4_cim_read_ma_la(adap, (u32 *)p->data,
			  (u32 *)p->data + 5 * CIM_MALA_SIZE);
	return 0;
}

static const struct file_operations cim_ma_la_fops = {
	.owner   = THIS_MODULE,
	.open    = cim_ma_la_open,
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
DEFINE_SHOW_ATTRIBUTE(cim_qcfg);

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

struct field_desc {
	const char *name;
	unsigned int start;
	unsigned int width;
};

static void field_desc_show(struct seq_file *seq, u64 v,
			    const struct field_desc *p)
{
	char buf[32];
	int line_size = 0;

	while (p->name) {
		u64 mask = (1ULL << p->width) - 1;
		int len = scnprintf(buf, sizeof(buf), "%s: %llu", p->name,
				    ((unsigned long long)v >> p->start) & mask);

		if (line_size + len >= 79) {
			line_size = 8;
			seq_puts(seq, "\n        ");
		}
		seq_printf(seq, "%s ", buf);
		line_size += len + 1;
		p++;
	}
	seq_putc(seq, '\n');
}

static struct field_desc tp_la0[] = {
	{ "RcfOpCodeOut", 60, 4 },
	{ "State", 56, 4 },
	{ "WcfState", 52, 4 },
	{ "RcfOpcSrcOut", 50, 2 },
	{ "CRxError", 49, 1 },
	{ "ERxError", 48, 1 },
	{ "SanityFailed", 47, 1 },
	{ "SpuriousMsg", 46, 1 },
	{ "FlushInputMsg", 45, 1 },
	{ "FlushInputCpl", 44, 1 },
	{ "RssUpBit", 43, 1 },
	{ "RssFilterHit", 42, 1 },
	{ "Tid", 32, 10 },
	{ "InitTcb", 31, 1 },
	{ "LineNumber", 24, 7 },
	{ "Emsg", 23, 1 },
	{ "EdataOut", 22, 1 },
	{ "Cmsg", 21, 1 },
	{ "CdataOut", 20, 1 },
	{ "EreadPdu", 19, 1 },
	{ "CreadPdu", 18, 1 },
	{ "TunnelPkt", 17, 1 },
	{ "RcfPeerFin", 16, 1 },
	{ "RcfReasonOut", 12, 4 },
	{ "TxCchannel", 10, 2 },
	{ "RcfTxChannel", 8, 2 },
	{ "RxEchannel", 6, 2 },
	{ "RcfRxChannel", 5, 1 },
	{ "RcfDataOutSrdy", 4, 1 },
	{ "RxDvld", 3, 1 },
	{ "RxOoDvld", 2, 1 },
	{ "RxCongestion", 1, 1 },
	{ "TxCongestion", 0, 1 },
	{ NULL }
};

static int tp_la_show(struct seq_file *seq, void *v, int idx)
{
	const u64 *p = v;

	field_desc_show(seq, *p, tp_la0);
	return 0;
}

static int tp_la_show2(struct seq_file *seq, void *v, int idx)
{
	const u64 *p = v;

	if (idx)
		seq_putc(seq, '\n');
	field_desc_show(seq, p[0], tp_la0);
	if (idx < (TPLA_SIZE / 2 - 1) || p[1] != ~0ULL)
		field_desc_show(seq, p[1], tp_la0);
	return 0;
}

static int tp_la_show3(struct seq_file *seq, void *v, int idx)
{
	static struct field_desc tp_la1[] = {
		{ "CplCmdIn", 56, 8 },
		{ "CplCmdOut", 48, 8 },
		{ "ESynOut", 47, 1 },
		{ "EAckOut", 46, 1 },
		{ "EFinOut", 45, 1 },
		{ "ERstOut", 44, 1 },
		{ "SynIn", 43, 1 },
		{ "AckIn", 42, 1 },
		{ "FinIn", 41, 1 },
		{ "RstIn", 40, 1 },
		{ "DataIn", 39, 1 },
		{ "DataInVld", 38, 1 },
		{ "PadIn", 37, 1 },
		{ "RxBufEmpty", 36, 1 },
		{ "RxDdp", 35, 1 },
		{ "RxFbCongestion", 34, 1 },
		{ "TxFbCongestion", 33, 1 },
		{ "TxPktSumSrdy", 32, 1 },
		{ "RcfUlpType", 28, 4 },
		{ "Eread", 27, 1 },
		{ "Ebypass", 26, 1 },
		{ "Esave", 25, 1 },
		{ "Static0", 24, 1 },
		{ "Cread", 23, 1 },
		{ "Cbypass", 22, 1 },
		{ "Csave", 21, 1 },
		{ "CPktOut", 20, 1 },
		{ "RxPagePoolFull", 18, 2 },
		{ "RxLpbkPkt", 17, 1 },
		{ "TxLpbkPkt", 16, 1 },
		{ "RxVfValid", 15, 1 },
		{ "SynLearned", 14, 1 },
		{ "SetDelEntry", 13, 1 },
		{ "SetInvEntry", 12, 1 },
		{ "CpcmdDvld", 11, 1 },
		{ "CpcmdSave", 10, 1 },
		{ "RxPstructsFull", 8, 2 },
		{ "EpcmdDvld", 7, 1 },
		{ "EpcmdFlush", 6, 1 },
		{ "EpcmdTrimPrefix", 5, 1 },
		{ "EpcmdTrimPostfix", 4, 1 },
		{ "ERssIp4Pkt", 3, 1 },
		{ "ERssIp6Pkt", 2, 1 },
		{ "ERssTcpUdpPkt", 1, 1 },
		{ "ERssFceFipPkt", 0, 1 },
		{ NULL }
	};
	static struct field_desc tp_la2[] = {
		{ "CplCmdIn", 56, 8 },
		{ "MpsVfVld", 55, 1 },
		{ "MpsPf", 52, 3 },
		{ "MpsVf", 44, 8 },
		{ "SynIn", 43, 1 },
		{ "AckIn", 42, 1 },
		{ "FinIn", 41, 1 },
		{ "RstIn", 40, 1 },
		{ "DataIn", 39, 1 },
		{ "DataInVld", 38, 1 },
		{ "PadIn", 37, 1 },
		{ "RxBufEmpty", 36, 1 },
		{ "RxDdp", 35, 1 },
		{ "RxFbCongestion", 34, 1 },
		{ "TxFbCongestion", 33, 1 },
		{ "TxPktSumSrdy", 32, 1 },
		{ "RcfUlpType", 28, 4 },
		{ "Eread", 27, 1 },
		{ "Ebypass", 26, 1 },
		{ "Esave", 25, 1 },
		{ "Static0", 24, 1 },
		{ "Cread", 23, 1 },
		{ "Cbypass", 22, 1 },
		{ "Csave", 21, 1 },
		{ "CPktOut", 20, 1 },
		{ "RxPagePoolFull", 18, 2 },
		{ "RxLpbkPkt", 17, 1 },
		{ "TxLpbkPkt", 16, 1 },
		{ "RxVfValid", 15, 1 },
		{ "SynLearned", 14, 1 },
		{ "SetDelEntry", 13, 1 },
		{ "SetInvEntry", 12, 1 },
		{ "CpcmdDvld", 11, 1 },
		{ "CpcmdSave", 10, 1 },
		{ "RxPstructsFull", 8, 2 },
		{ "EpcmdDvld", 7, 1 },
		{ "EpcmdFlush", 6, 1 },
		{ "EpcmdTrimPrefix", 5, 1 },
		{ "EpcmdTrimPostfix", 4, 1 },
		{ "ERssIp4Pkt", 3, 1 },
		{ "ERssIp6Pkt", 2, 1 },
		{ "ERssTcpUdpPkt", 1, 1 },
		{ "ERssFceFipPkt", 0, 1 },
		{ NULL }
	};
	const u64 *p = v;

	if (idx)
		seq_putc(seq, '\n');
	field_desc_show(seq, p[0], tp_la0);
	if (idx < (TPLA_SIZE / 2 - 1) || p[1] != ~0ULL)
		field_desc_show(seq, p[1], (p[0] & BIT(17)) ? tp_la2 : tp_la1);
	return 0;
}

static int tp_la_open(struct inode *inode, struct file *file)
{
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	switch (DBGLAMODE_G(t4_read_reg(adap, TP_DBG_LA_CONFIG_A))) {
	case 2:
		p = seq_open_tab(file, TPLA_SIZE / 2, 2 * sizeof(u64), 0,
				 tp_la_show2);
		break;
	case 3:
		p = seq_open_tab(file, TPLA_SIZE / 2, 2 * sizeof(u64), 0,
				 tp_la_show3);
		break;
	default:
		p = seq_open_tab(file, TPLA_SIZE, sizeof(u64), 0, tp_la_show);
	}
	if (!p)
		return -ENOMEM;

	t4_tp_read_la(adap, (u64 *)p->data, NULL);
	return 0;
}

static ssize_t tp_la_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	int err;
	char s[32];
	unsigned long val;
	size_t size = min(sizeof(s) - 1, count);
	struct adapter *adap = file_inode(file)->i_private;

	if (copy_from_user(s, buf, size))
		return -EFAULT;
	s[size] = '\0';
	err = kstrtoul(s, 0, &val);
	if (err)
		return err;
	if (val > 0xffff)
		return -EINVAL;
	adap->params.tp.la_mask = val << 16;
	t4_set_reg_field(adap, TP_DBG_LA_CONFIG_A, 0xffff0000U,
			 adap->params.tp.la_mask);
	return count;
}

static const struct file_operations tp_la_fops = {
	.owner   = THIS_MODULE,
	.open    = tp_la_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
	.write   = tp_la_write
};

static int ulprx_la_show(struct seq_file *seq, void *v, int idx)
{
	const u32 *p = v;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "      Pcmd        Type   Message"
			 "                Data\n");
	else
		seq_printf(seq, "%08x%08x  %4x  %08x  %08x%08x%08x%08x\n",
			   p[1], p[0], p[2], p[3], p[7], p[6], p[5], p[4]);
	return 0;
}

static int ulprx_la_open(struct inode *inode, struct file *file)
{
	struct seq_tab *p;
	struct adapter *adap = inode->i_private;

	p = seq_open_tab(file, ULPRX_LA_SIZE, 8 * sizeof(u32), 1,
			 ulprx_la_show);
	if (!p)
		return -ENOMEM;

	t4_ulprx_read_la(adap, (u32 *)p->data);
	return 0;
}

static const struct file_operations ulprx_la_fops = {
	.owner   = THIS_MODULE,
	.open    = ulprx_la_open,
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
	u32 tx_cnt[T6_PM_NSTATS], rx_cnt[T6_PM_NSTATS];
	u64 tx_cyc[T6_PM_NSTATS], rx_cyc[T6_PM_NSTATS];
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

	if (CHELSIO_CHIP_VERSION(adap->params.chip) > CHELSIO_T5) {
		/* In T5 the granularity of the total wait is too fine.
		 * It is not useful as it reaches the max value too fast.
		 * Hence display this Input FIFO wait for T6 onwards.
		 */
		seq_printf(seq, "%13s %10s  %20s\n",
			   " ", "Total wait", "Total Occupancy");
		seq_printf(seq, "Tx FIFO wait  %10u  %20llu\n",
			   tx_cnt[i], tx_cyc[i]);
		seq_printf(seq, "Rx FIFO wait  %10u  %20llu\n",
			   rx_cnt[i], rx_cyc[i]);

		/* Skip index 6 as there is nothing useful ihere */
		i += 2;

		/* At index 7, a new stat for read latency (count, total wait)
		 * is added.
		 */
		seq_printf(seq, "%13s %10s  %20s\n",
			   " ", "Reads", "Total wait");
		seq_printf(seq, "Tx latency    %10u  %20llu\n",
			   tx_cnt[i], tx_cyc[i]);
		seq_printf(seq, "Rx latency    %10u  %20llu\n",
			   rx_cnt[i], rx_cyc[i]);
	}
	return 0;
}

static int pm_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_stats_show, inode->i_private);
}

static ssize_t pm_stats_clear(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct adapter *adap = file_inode(file)->i_private;

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

static int tx_rate_show(struct seq_file *seq, void *v)
{
	u64 nrate[NCHAN], orate[NCHAN];
	struct adapter *adap = seq->private;

	t4_get_chan_txrate(adap, nrate, orate);
	if (adap->params.arch.nchan == NCHAN) {
		seq_puts(seq, "              channel 0   channel 1   "
			 "channel 2   channel 3\n");
		seq_printf(seq, "NIC B/s:     %10llu  %10llu  %10llu  %10llu\n",
			   (unsigned long long)nrate[0],
			   (unsigned long long)nrate[1],
			   (unsigned long long)nrate[2],
			   (unsigned long long)nrate[3]);
		seq_printf(seq, "Offload B/s: %10llu  %10llu  %10llu  %10llu\n",
			   (unsigned long long)orate[0],
			   (unsigned long long)orate[1],
			   (unsigned long long)orate[2],
			   (unsigned long long)orate[3]);
	} else {
		seq_puts(seq, "              channel 0   channel 1\n");
		seq_printf(seq, "NIC B/s:     %10llu  %10llu\n",
			   (unsigned long long)nrate[0],
			   (unsigned long long)nrate[1]);
		seq_printf(seq, "Offload B/s: %10llu  %10llu\n",
			   (unsigned long long)orate[0],
			   (unsigned long long)orate[1]);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tx_rate);

static int cctrl_tbl_show(struct seq_file *seq, void *v)
{
	static const char * const dec_fac[] = {
		"0.5", "0.5625", "0.625", "0.6875", "0.75", "0.8125", "0.875",
		"0.9375" };

	int i;
	u16 (*incr)[NCCTRL_WIN];
	struct adapter *adap = seq->private;

	incr = kmalloc_array(NMTUS, sizeof(*incr), GFP_KERNEL);
	if (!incr)
		return -ENOMEM;

	t4_read_cong_tbl(adap, incr);

	for (i = 0; i < NCCTRL_WIN; ++i) {
		seq_printf(seq, "%2d: %4u %4u %4u %4u %4u %4u %4u %4u\n", i,
			   incr[0][i], incr[1][i], incr[2][i], incr[3][i],
			   incr[4][i], incr[5][i], incr[6][i], incr[7][i]);
		seq_printf(seq, "%8u %4u %4u %4u %4u %4u %4u %4u %5u %s\n",
			   incr[8][i], incr[9][i], incr[10][i], incr[11][i],
			   incr[12][i], incr[13][i], incr[14][i], incr[15][i],
			   adap->params.a_wnd[i],
			   dec_fac[adap->params.b_wnd[i]]);
	}

	kfree(incr);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cctrl_tbl);

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
DEFINE_SHOW_ATTRIBUTE(clk);

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
	[FW_DEVLOG_FACILITY_CF]         = "CF",
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
			   be32_to_cpu(e->seqno),
			   be64_to_cpu(e->timestamp),
			   (e->level < ARRAY_SIZE(devlog_level_strings)
			    ? devlog_level_strings[e->level]
			    : "UNKNOWN"),
			   (e->facility < ARRAY_SIZE(devlog_facility_strings)
			    ? devlog_facility_strings[e->facility]
			    : "UNKNOWN"));
		seq_printf(seq, e->fmt,
			   be32_to_cpu(e->params[0]),
			   be32_to_cpu(e->params[1]),
			   be32_to_cpu(e->params[2]),
			   be32_to_cpu(e->params[3]),
			   be32_to_cpu(e->params[4]),
			   be32_to_cpu(e->params[5]),
			   be32_to_cpu(e->params[6]),
			   be32_to_cpu(e->params[7]));
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

	/* Find the earliest (lowest Sequence Number) log entry in the
	 * circular Device Log.
	 */
	for (fseqno = ~((u32)0), index = 0; index < dinfo->nentries; index++) {
		struct fw_devlog_e *e = &dinfo->log[index];
		__u32 seqno;

		if (e->timestamp == 0)
			continue;

		seqno = be32_to_cpu(e->seqno);
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

/* Show Firmware Mailbox Command/Reply Log
 *
 * Note that we don't do any locking when dumping the Firmware Mailbox Log so
 * it's possible that we can catch things during a log update and therefore
 * see partially corrupted log entries.  But it's probably Good Enough(tm).
 * If we ever decide that we want to make sure that we're dumping a coherent
 * log, we'd need to perform locking in the mailbox logging and in
 * mboxlog_open() where we'd need to grab the entire mailbox log in one go
 * like we do for the Firmware Device Log.
 */
static int mboxlog_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	struct mbox_cmd_log *log = adapter->mbox_log;
	struct mbox_cmd *entry;
	int entry_idx, i;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq,
			   "%10s  %15s  %5s  %5s  %s\n",
			   "Seq#", "Tstamp", "Atime", "Etime",
			   "Command/Reply");
		return 0;
	}

	entry_idx = log->cursor + ((uintptr_t)v - 2);
	if (entry_idx >= log->size)
		entry_idx -= log->size;
	entry = mbox_cmd_log_entry(log, entry_idx);

	/* skip over unused entries */
	if (entry->timestamp == 0)
		return 0;

	seq_printf(seq, "%10u  %15llu  %5d  %5d",
		   entry->seqno, entry->timestamp,
		   entry->access, entry->execute);
	for (i = 0; i < MBOX_LEN / 8; i++) {
		u64 flit = entry->cmd[i];
		u32 hi = (u32)(flit >> 32);
		u32 lo = (u32)flit;

		seq_printf(seq, "  %08x %08x", hi, lo);
	}
	seq_puts(seq, "\n");
	return 0;
}

static inline void *mboxlog_get_idx(struct seq_file *seq, loff_t pos)
{
	struct adapter *adapter = seq->private;
	struct mbox_cmd_log *log = adapter->mbox_log;

	return ((pos <= log->size) ? (void *)(uintptr_t)(pos + 1) : NULL);
}

static void *mboxlog_start(struct seq_file *seq, loff_t *pos)
{
	return *pos ? mboxlog_get_idx(seq, *pos) : SEQ_START_TOKEN;
}

static void *mboxlog_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return mboxlog_get_idx(seq, *pos);
}

static void mboxlog_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations mboxlog_seq_ops = {
	.start = mboxlog_start,
	.next  = mboxlog_next,
	.stop  = mboxlog_stop,
	.show  = mboxlog_show
};

static int mboxlog_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &mboxlog_seq_ops);

	if (!res) {
		struct seq_file *seq = file->private_data;

		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations mboxlog_fops = {
	.owner   = THIS_MODULE,
	.open    = mboxlog_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int mbox_show(struct seq_file *seq, void *v)
{
	static const char * const owner[] = { "none", "FW", "driver",
					      "unknown", "<unread>" };

	int i;
	unsigned int mbox = (uintptr_t)seq->private & 7;
	struct adapter *adap = seq->private - mbox;
	void __iomem *addr = adap->regs + PF_REG(mbox, CIM_PF_MAILBOX_DATA_A);

	/* For T4 we don't have a shadow copy of the Mailbox Control register.
	 * And since reading that real register causes a side effect of
	 * granting ownership, we're best of simply not reading it at all.
	 */
	if (is_t4(adap->params.chip)) {
		i = 4; /* index of "<unread>" */
	} else {
		unsigned int ctrl_reg = CIM_PF_MAILBOX_CTRL_SHADOW_COPY_A;
		void __iomem *ctrl = adap->regs + PF_REG(mbox, ctrl_reg);

		i = MBOWNER_G(readl(ctrl));
	}

	seq_printf(seq, "mailbox owned by %s\n\n", owner[i]);

	for (i = 0; i < MBOX_LEN; i += 8)
		seq_printf(seq, "%016llx\n",
			   (unsigned long long)readq(addr + i));
	return 0;
}

static int mbox_open(struct inode *inode, struct file *file)
{
	return single_open(file, mbox_show, inode->i_private);
}

static ssize_t mbox_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *pos)
{
	int i;
	char c = '\n', s[256];
	unsigned long long data[8];
	const struct inode *ino;
	unsigned int mbox;
	struct adapter *adap;
	void __iomem *addr;
	void __iomem *ctrl;

	if (count > sizeof(s) - 1 || !count)
		return -EINVAL;
	if (copy_from_user(s, buf, count))
		return -EFAULT;
	s[count] = '\0';

	if (sscanf(s, "%llx %llx %llx %llx %llx %llx %llx %llx%c", &data[0],
		   &data[1], &data[2], &data[3], &data[4], &data[5], &data[6],
		   &data[7], &c) < 8 || c != '\n')
		return -EINVAL;

	ino = file_inode(file);
	mbox = (uintptr_t)ino->i_private & 7;
	adap = ino->i_private - mbox;
	addr = adap->regs + PF_REG(mbox, CIM_PF_MAILBOX_DATA_A);
	ctrl = addr + MBOX_LEN;

	if (MBOWNER_G(readl(ctrl)) != X_MBOWNER_PL)
		return -EBUSY;

	for (i = 0; i < 8; i++)
		writeq(data[i], addr + 8 * i);

	writel(MBMSGVALID_F | MBOWNER_V(X_MBOWNER_FW), ctrl);
	return count;
}

static const struct file_operations mbox_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = mbox_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write   = mbox_write
};

static int mps_trc_show(struct seq_file *seq, void *v)
{
	int enabled, i;
	struct trace_params tp;
	unsigned int trcidx = (uintptr_t)seq->private & 3;
	struct adapter *adap = seq->private - trcidx;

	t4_get_trace_filter(adap, &tp, trcidx, &enabled);
	if (!enabled) {
		seq_puts(seq, "tracer is disabled\n");
		return 0;
	}

	if (tp.skip_ofst * 8 >= TRACE_LEN) {
		dev_err(adap->pdev_dev, "illegal trace pattern skip offset\n");
		return -EINVAL;
	}
	if (tp.port < 8) {
		i = adap->chan_map[tp.port & 3];
		if (i >= MAX_NPORTS) {
			dev_err(adap->pdev_dev, "tracer %u is assigned "
				"to non-existing port\n", trcidx);
			return -EINVAL;
		}
		seq_printf(seq, "tracer is capturing %s %s, ",
			   adap->port[i]->name, tp.port < 4 ? "Rx" : "Tx");
	} else
		seq_printf(seq, "tracer is capturing loopback %d, ",
			   tp.port - 8);
	seq_printf(seq, "snap length: %u, min length: %u\n", tp.snap_len,
		   tp.min_len);
	seq_printf(seq, "packets captured %smatch filter\n",
		   tp.invert ? "do not " : "");

	if (tp.skip_ofst) {
		seq_puts(seq, "filter pattern: ");
		for (i = 0; i < tp.skip_ofst * 2; i += 2)
			seq_printf(seq, "%08x%08x", tp.data[i], tp.data[i + 1]);
		seq_putc(seq, '/');
		for (i = 0; i < tp.skip_ofst * 2; i += 2)
			seq_printf(seq, "%08x%08x", tp.mask[i], tp.mask[i + 1]);
		seq_puts(seq, "@0\n");
	}

	seq_puts(seq, "filter pattern: ");
	for (i = tp.skip_ofst * 2; i < TRACE_LEN / 4; i += 2)
		seq_printf(seq, "%08x%08x", tp.data[i], tp.data[i + 1]);
	seq_putc(seq, '/');
	for (i = tp.skip_ofst * 2; i < TRACE_LEN / 4; i += 2)
		seq_printf(seq, "%08x%08x", tp.mask[i], tp.mask[i + 1]);
	seq_printf(seq, "@%u\n", (tp.skip_ofst + tp.skip_len) * 8);
	return 0;
}

static int mps_trc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mps_trc_show, inode->i_private);
}

static unsigned int xdigit2int(unsigned char c)
{
	return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10;
}

#define TRC_PORT_NONE 0xff
#define TRC_RSS_ENABLE 0x33
#define TRC_RSS_DISABLE 0x13

/* Set an MPS trace filter.  Syntax is:
 *
 * disable
 *
 * to disable tracing, or
 *
 * interface qid=<qid no> [snaplen=<val>] [minlen=<val>] [not] [<pattern>]...
 *
 * where interface is one of rxN, txN, or loopbackN, N = 0..3, qid can be one
 * of the NIC's response qid obtained from sge_qinfo and pattern has the form
 *
 * <pattern data>[/<pattern mask>][@<anchor>]
 *
 * Up to 2 filter patterns can be specified.  If 2 are supplied the first one
 * must be anchored at 0.  An omitted mask is taken as a mask of 1s, an omitted
 * anchor is taken as 0.
 */
static ssize_t mps_trc_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	int i, enable, ret;
	u32 *data, *mask;
	struct trace_params tp;
	const struct inode *ino;
	unsigned int trcidx;
	char *s, *p, *word, *end;
	struct adapter *adap;
	u32 j;

	ino = file_inode(file);
	trcidx = (uintptr_t)ino->i_private & 3;
	adap = ino->i_private - trcidx;

	/* Don't accept input more than 1K, can't be anything valid except lots
	 * of whitespace.  Well, use less.
	 */
	if (count > 1024)
		return -EFBIG;
	p = s = kzalloc(count + 1, GFP_USER);
	if (!s)
		return -ENOMEM;
	if (copy_from_user(s, buf, count)) {
		count = -EFAULT;
		goto out;
	}

	if (s[count - 1] == '\n')
		s[count - 1] = '\0';

	enable = strcmp("disable", s) != 0;
	if (!enable)
		goto apply;

	/* enable or disable trace multi rss filter */
	if (adap->trace_rss)
		t4_write_reg(adap, MPS_TRC_CFG_A, TRC_RSS_ENABLE);
	else
		t4_write_reg(adap, MPS_TRC_CFG_A, TRC_RSS_DISABLE);

	memset(&tp, 0, sizeof(tp));
	tp.port = TRC_PORT_NONE;
	i = 0;	/* counts pattern nibbles */

	while (p) {
		while (isspace(*p))
			p++;
		word = strsep(&p, " ");
		if (!*word)
			break;

		if (!strncmp(word, "qid=", 4)) {
			end = (char *)word + 4;
			ret = kstrtouint(end, 10, &j);
			if (ret)
				goto out;
			if (!adap->trace_rss) {
				t4_write_reg(adap, MPS_T5_TRC_RSS_CONTROL_A, j);
				continue;
			}

			switch (trcidx) {
			case 0:
				t4_write_reg(adap, MPS_TRC_RSS_CONTROL_A, j);
				break;
			case 1:
				t4_write_reg(adap,
					     MPS_TRC_FILTER1_RSS_CONTROL_A, j);
				break;
			case 2:
				t4_write_reg(adap,
					     MPS_TRC_FILTER2_RSS_CONTROL_A, j);
				break;
			case 3:
				t4_write_reg(adap,
					     MPS_TRC_FILTER3_RSS_CONTROL_A, j);
				break;
			}
			continue;
		}
		if (!strncmp(word, "snaplen=", 8)) {
			end = (char *)word + 8;
			ret = kstrtouint(end, 10, &j);
			if (ret || j > 9600) {
inval:				count = -EINVAL;
				goto out;
			}
			tp.snap_len = j;
			continue;
		}
		if (!strncmp(word, "minlen=", 7)) {
			end = (char *)word + 7;
			ret = kstrtouint(end, 10, &j);
			if (ret || j > TFMINPKTSIZE_M)
				goto inval;
			tp.min_len = j;
			continue;
		}
		if (!strcmp(word, "not")) {
			tp.invert = !tp.invert;
			continue;
		}
		if (!strncmp(word, "loopback", 8) && tp.port == TRC_PORT_NONE) {
			if (word[8] < '0' || word[8] > '3' || word[9])
				goto inval;
			tp.port = word[8] - '0' + 8;
			continue;
		}
		if (!strncmp(word, "tx", 2) && tp.port == TRC_PORT_NONE) {
			if (word[2] < '0' || word[2] > '3' || word[3])
				goto inval;
			tp.port = word[2] - '0' + 4;
			if (adap->chan_map[tp.port & 3] >= MAX_NPORTS)
				goto inval;
			continue;
		}
		if (!strncmp(word, "rx", 2) && tp.port == TRC_PORT_NONE) {
			if (word[2] < '0' || word[2] > '3' || word[3])
				goto inval;
			tp.port = word[2] - '0';
			if (adap->chan_map[tp.port] >= MAX_NPORTS)
				goto inval;
			continue;
		}
		if (!isxdigit(*word))
			goto inval;

		/* we have found a trace pattern */
		if (i) {                            /* split pattern */
			if (tp.skip_len)            /* too many splits */
				goto inval;
			tp.skip_ofst = i / 16;
		}

		data = &tp.data[i / 8];
		mask = &tp.mask[i / 8];
		j = i;

		while (isxdigit(*word)) {
			if (i >= TRACE_LEN * 2) {
				count = -EFBIG;
				goto out;
			}
			*data = (*data << 4) + xdigit2int(*word++);
			if (++i % 8 == 0)
				data++;
		}
		if (*word == '/') {
			word++;
			while (isxdigit(*word)) {
				if (j >= i)         /* mask longer than data */
					goto inval;
				*mask = (*mask << 4) + xdigit2int(*word++);
				if (++j % 8 == 0)
					mask++;
			}
			if (i != j)                 /* mask shorter than data */
				goto inval;
		} else {                            /* no mask, use all 1s */
			for ( ; i - j >= 8; j += 8)
				*mask++ = 0xffffffff;
			if (i % 8)
				*mask = (1 << (i % 8) * 4) - 1;
		}
		if (*word == '@') {
			end = (char *)word + 1;
			ret = kstrtouint(end, 10, &j);
			if (*end && *end != '\n')
				goto inval;
			if (j & 7)          /* doesn't start at multiple of 8 */
				goto inval;
			j /= 8;
			if (j < tp.skip_ofst)     /* overlaps earlier pattern */
				goto inval;
			if (j - tp.skip_ofst > 31)            /* skip too big */
				goto inval;
			tp.skip_len = j - tp.skip_ofst;
		}
		if (i % 8) {
			*data <<= (8 - i % 8) * 4;
			*mask <<= (8 - i % 8) * 4;
			i = (i + 15) & ~15;         /* 8-byte align */
		}
	}

	if (tp.port == TRC_PORT_NONE)
		goto inval;

apply:
	i = t4_set_trace_filter(adap, &tp, trcidx, enable);
	if (i)
		count = i;
out:
	kfree(s);
	return count;
}

static const struct file_operations mps_trc_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = mps_trc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.write   = mps_trc_write
};

static ssize_t flash_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	loff_t pos = *ppos;
	loff_t avail = file_inode(file)->i_size;
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
	.llseek  = default_llseek,
};

static inline void tcamxy2valmask(u64 x, u64 y, u8 *addr, u64 *mask)
{
	*mask = x | y;
	y = (__force u64)cpu_to_be64(y);
	memcpy(addr, (char *)&y + 2, ETH_ALEN);
}

static int mps_tcam_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;
	unsigned int chip_ver = CHELSIO_CHIP_VERSION(adap->params.chip);
	if (v == SEQ_START_TOKEN) {
		if (chip_ver > CHELSIO_T5) {
			seq_puts(seq, "Idx  Ethernet address     Mask     "
				 "  VNI   Mask   IVLAN Vld "
				 "DIP_Hit   Lookup  Port "
				 "Vld Ports PF  VF                           "
				 "Replication                                "
				 "    P0 P1 P2 P3  ML\n");
		} else {
			if (adap->params.arch.mps_rplc_size > 128)
				seq_puts(seq, "Idx  Ethernet address     Mask     "
					 "Vld Ports PF  VF                           "
					 "Replication                                "
					 "    P0 P1 P2 P3  ML\n");
			else
				seq_puts(seq, "Idx  Ethernet address     Mask     "
					 "Vld Ports PF  VF              Replication"
					 "	         P0 P1 P2 P3  ML\n");
		}
	} else {
		u64 mask;
		u8 addr[ETH_ALEN];
		bool replicate, dip_hit = false, vlan_vld = false;
		unsigned int idx = (uintptr_t)v - 2;
		u64 tcamy, tcamx, val;
		u32 cls_lo, cls_hi, ctl, data2, vnix = 0, vniy = 0;
		u32 rplc[8] = {0};
		u8 lookup_type = 0, port_num = 0;
		u16 ivlan = 0;

		if (chip_ver > CHELSIO_T5) {
			/* CtlCmdType - 0: Read, 1: Write
			 * CtlTcamSel - 0: TCAM0, 1: TCAM1
			 * CtlXYBitSel- 0: Y bit, 1: X bit
			 */

			/* Read tcamy */
			ctl = CTLCMDTYPE_V(0) | CTLXYBITSEL_V(0);
			if (idx < 256)
				ctl |= CTLTCAMINDEX_V(idx) | CTLTCAMSEL_V(0);
			else
				ctl |= CTLTCAMINDEX_V(idx - 256) |
				       CTLTCAMSEL_V(1);
			t4_write_reg(adap, MPS_CLS_TCAM_DATA2_CTL_A, ctl);
			val = t4_read_reg(adap, MPS_CLS_TCAM_DATA1_A);
			tcamy = DMACH_G(val) << 32;
			tcamy |= t4_read_reg(adap, MPS_CLS_TCAM_DATA0_A);
			data2 = t4_read_reg(adap, MPS_CLS_TCAM_DATA2_CTL_A);
			lookup_type = DATALKPTYPE_G(data2);
			/* 0 - Outer header, 1 - Inner header
			 * [71:48] bit locations are overloaded for
			 * outer vs. inner lookup types.
			 */
			if (lookup_type && (lookup_type != DATALKPTYPE_M)) {
				/* Inner header VNI */
				vniy = (data2 & DATAVIDH2_F) |
				       (DATAVIDH1_G(data2) << 16) | VIDL_G(val);
				dip_hit = data2 & DATADIPHIT_F;
			} else {
				vlan_vld = data2 & DATAVIDH2_F;
				ivlan = VIDL_G(val);
			}
			port_num = DATAPORTNUM_G(data2);

			/* Read tcamx. Change the control param */
			vnix = 0;
			ctl |= CTLXYBITSEL_V(1);
			t4_write_reg(adap, MPS_CLS_TCAM_DATA2_CTL_A, ctl);
			val = t4_read_reg(adap, MPS_CLS_TCAM_DATA1_A);
			tcamx = DMACH_G(val) << 32;
			tcamx |= t4_read_reg(adap, MPS_CLS_TCAM_DATA0_A);
			data2 = t4_read_reg(adap, MPS_CLS_TCAM_DATA2_CTL_A);
			if (lookup_type && (lookup_type != DATALKPTYPE_M)) {
				/* Inner header VNI mask */
				vnix = (data2 & DATAVIDH2_F) |
				       (DATAVIDH1_G(data2) << 16) | VIDL_G(val);
			}
		} else {
			tcamy = t4_read_reg64(adap, MPS_CLS_TCAM_Y_L(idx));
			tcamx = t4_read_reg64(adap, MPS_CLS_TCAM_X_L(idx));
		}

		cls_lo = t4_read_reg(adap, MPS_CLS_SRAM_L(idx));
		cls_hi = t4_read_reg(adap, MPS_CLS_SRAM_H(idx));

		if (tcamx & tcamy) {
			seq_printf(seq, "%3u         -\n", idx);
			goto out;
		}

		rplc[0] = rplc[1] = rplc[2] = rplc[3] = 0;
		if (chip_ver > CHELSIO_T5)
			replicate = (cls_lo & T6_REPLICATE_F);
		else
			replicate = (cls_lo & REPLICATE_F);

		if (replicate) {
			struct fw_ldst_cmd ldst_cmd;
			int ret;
			struct fw_ldst_mps_rplc mps_rplc;
			u32 ldst_addrspc;

			memset(&ldst_cmd, 0, sizeof(ldst_cmd));
			ldst_addrspc =
				FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_MPS);
			ldst_cmd.op_to_addrspace =
				htonl(FW_CMD_OP_V(FW_LDST_CMD) |
				      FW_CMD_REQUEST_F |
				      FW_CMD_READ_F |
				      ldst_addrspc);
			ldst_cmd.cycles_to_len16 = htonl(FW_LEN16(ldst_cmd));
			ldst_cmd.u.mps.rplc.fid_idx =
				htons(FW_LDST_CMD_FID_V(FW_LDST_MPS_RPLC) |
				      FW_LDST_CMD_IDX_V(idx));
			ret = t4_wr_mbox(adap, adap->mbox, &ldst_cmd,
					 sizeof(ldst_cmd), &ldst_cmd);
			if (ret)
				dev_warn(adap->pdev_dev, "Can't read MPS "
					 "replication map for idx %d: %d\n",
					 idx, -ret);
			else {
				mps_rplc = ldst_cmd.u.mps.rplc;
				rplc[0] = ntohl(mps_rplc.rplc31_0);
				rplc[1] = ntohl(mps_rplc.rplc63_32);
				rplc[2] = ntohl(mps_rplc.rplc95_64);
				rplc[3] = ntohl(mps_rplc.rplc127_96);
				if (adap->params.arch.mps_rplc_size > 128) {
					rplc[4] = ntohl(mps_rplc.rplc159_128);
					rplc[5] = ntohl(mps_rplc.rplc191_160);
					rplc[6] = ntohl(mps_rplc.rplc223_192);
					rplc[7] = ntohl(mps_rplc.rplc255_224);
				}
			}
		}

		tcamxy2valmask(tcamx, tcamy, addr, &mask);
		if (chip_ver > CHELSIO_T5) {
			/* Inner header lookup */
			if (lookup_type && (lookup_type != DATALKPTYPE_M)) {
				seq_printf(seq,
					   "%3u %02x:%02x:%02x:%02x:%02x:%02x "
					   "%012llx %06x %06x    -    -   %3c"
					   "      'I'  %4x   "
					   "%3c   %#x%4u%4d", idx, addr[0],
					   addr[1], addr[2], addr[3],
					   addr[4], addr[5],
					   (unsigned long long)mask,
					   vniy, (vnix | vniy),
					   dip_hit ? 'Y' : 'N',
					   port_num,
					   (cls_lo & T6_SRAM_VLD_F) ? 'Y' : 'N',
					   PORTMAP_G(cls_hi),
					   T6_PF_G(cls_lo),
					   (cls_lo & T6_VF_VALID_F) ?
					   T6_VF_G(cls_lo) : -1);
			} else {
				seq_printf(seq,
					   "%3u %02x:%02x:%02x:%02x:%02x:%02x "
					   "%012llx    -       -   ",
					   idx, addr[0], addr[1], addr[2],
					   addr[3], addr[4], addr[5],
					   (unsigned long long)mask);

				if (vlan_vld)
					seq_printf(seq, "%4u   Y     ", ivlan);
				else
					seq_puts(seq, "  -    N     ");

				seq_printf(seq,
					   "-      %3c  %4x   %3c   %#x%4u%4d",
					   lookup_type ? 'I' : 'O', port_num,
					   (cls_lo & T6_SRAM_VLD_F) ? 'Y' : 'N',
					   PORTMAP_G(cls_hi),
					   T6_PF_G(cls_lo),
					   (cls_lo & T6_VF_VALID_F) ?
					   T6_VF_G(cls_lo) : -1);
			}
		} else
			seq_printf(seq, "%3u %02x:%02x:%02x:%02x:%02x:%02x "
				   "%012llx%3c   %#x%4u%4d",
				   idx, addr[0], addr[1], addr[2], addr[3],
				   addr[4], addr[5], (unsigned long long)mask,
				   (cls_lo & SRAM_VLD_F) ? 'Y' : 'N',
				   PORTMAP_G(cls_hi),
				   PF_G(cls_lo),
				   (cls_lo & VF_VALID_F) ? VF_G(cls_lo) : -1);

		if (replicate) {
			if (adap->params.arch.mps_rplc_size > 128)
				seq_printf(seq, " %08x %08x %08x %08x "
					   "%08x %08x %08x %08x",
					   rplc[7], rplc[6], rplc[5], rplc[4],
					   rplc[3], rplc[2], rplc[1], rplc[0]);
			else
				seq_printf(seq, " %08x %08x %08x %08x",
					   rplc[3], rplc[2], rplc[1], rplc[0]);
		} else {
			if (adap->params.arch.mps_rplc_size > 128)
				seq_printf(seq, "%72c", ' ');
			else
				seq_printf(seq, "%36c", ' ');
		}

		if (chip_ver > CHELSIO_T5)
			seq_printf(seq, "%4u%3u%3u%3u %#x\n",
				   T6_SRAM_PRIO0_G(cls_lo),
				   T6_SRAM_PRIO1_G(cls_lo),
				   T6_SRAM_PRIO2_G(cls_lo),
				   T6_SRAM_PRIO3_G(cls_lo),
				   (cls_lo >> T6_MULTILISTEN0_S) & 0xf);
		else
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
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
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
DEFINE_SHOW_ATTRIBUTE(sensors);

#if IS_ENABLED(CONFIG_IPV6)
DEFINE_SHOW_ATTRIBUTE(clip_tbl);
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
	struct adapter *adap = inode->i_private;
	int ret, nentries;
	struct seq_tab *p;

	nentries = t4_chip_rss_size(adap);
	p = seq_open_tab(file, nentries / 8, 8 * sizeof(u16), 0, rss_show);
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
	else
		seq_printf(seq, "  VfWrAddr:      %3d\n",
			   T6_VFWRADDR_G(rssconf));
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
DEFINE_SHOW_ATTRIBUTE(rss_config);

/* RSS Secret Key.
 */

static int rss_key_show(struct seq_file *seq, void *v)
{
	u32 key[10];

	t4_read_rss_key(seq->private, key, true);
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
	struct adapter *adap = file_inode(file)->i_private;

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

	t4_write_rss_key(adap, key, -1, true);
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
	rss_pf_map = t4_read_rss_pf_map(adapter, true);
	rss_pf_mask = t4_read_rss_pf_mask(adapter, true);
	for (pf = 0; pf < 8; pf++) {
		pfconf[pf].rss_pf_map = rss_pf_map;
		pfconf[pf].rss_pf_mask = rss_pf_mask;
		t4_read_rss_pf_config(adapter, pf, &pfconf[pf].rss_pf_config,
				      true);
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
	int vf, vfcount = adapter->params.arch.vfcount;

	p = seq_open_tab(file, vfcount, sizeof(*vfconf), 1, rss_vf_config_show);
	if (!p)
		return -ENOMEM;

	vfconf = (struct rss_vf_conf *)p->data;
	for (vf = 0; vf < vfcount; vf++) {
		t4_read_rss_vf_config(adapter, vf, &vfconf[vf].rss_vf_vfl,
				      &vfconf[vf].rss_vf_vfh, true);
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

#ifdef CONFIG_CHELSIO_T4_DCB
extern char *dcb_ver_array[];

/* Data Center Briging information for each port.
 */
static int dcb_info_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Data Center Bridging Information\n");
	} else {
		int port = (uintptr_t)v - 2;
		struct net_device *dev = adap->port[port];
		struct port_info *pi = netdev2pinfo(dev);
		struct port_dcb_info *dcb = &pi->dcb;

		seq_puts(seq, "\n");
		seq_printf(seq, "Port: %d (DCB negotiated: %s)\n",
			   port,
			   cxgb4_dcb_enabled(dev) ? "yes" : "no");

		if (cxgb4_dcb_enabled(dev))
			seq_printf(seq, "[ DCBx Version %s ]\n",
				   dcb_ver_array[dcb->dcb_version]);

		if (dcb->msgs) {
			int i;

			seq_puts(seq, "\n  Index\t\t\t  :\t");
			for (i = 0; i < 8; i++)
				seq_printf(seq, " %3d", i);
			seq_puts(seq, "\n\n");
		}

		if (dcb->msgs & CXGB4_DCB_FW_PGID) {
			int prio, pgid;

			seq_puts(seq, "  Priority Group IDs\t  :\t");
			for (prio = 0; prio < 8; prio++) {
				pgid = (dcb->pgid >> 4 * (7 - prio)) & 0xf;
				seq_printf(seq, " %3d", pgid);
			}
			seq_puts(seq, "\n");
		}

		if (dcb->msgs & CXGB4_DCB_FW_PGRATE) {
			int pg;

			seq_puts(seq, "  Priority Group BW(%)\t  :\t");
			for (pg = 0; pg < 8; pg++)
				seq_printf(seq, " %3d", dcb->pgrate[pg]);
			seq_puts(seq, "\n");

			if (dcb->dcb_version == FW_PORT_DCB_VER_IEEE) {
				seq_puts(seq, "  TSA Algorithm\t\t  :\t");
				for (pg = 0; pg < 8; pg++)
					seq_printf(seq, " %3d", dcb->tsa[pg]);
				seq_puts(seq, "\n");
			}

			seq_printf(seq, "  Max PG Traffic Classes  [%3d  ]\n",
				   dcb->pg_num_tcs_supported);

			seq_puts(seq, "\n");
		}

		if (dcb->msgs & CXGB4_DCB_FW_PRIORATE) {
			int prio;

			seq_puts(seq, "  Priority Rate\t:\t");
			for (prio = 0; prio < 8; prio++)
				seq_printf(seq, " %3d", dcb->priorate[prio]);
			seq_puts(seq, "\n");
		}

		if (dcb->msgs & CXGB4_DCB_FW_PFC) {
			int prio;

			seq_puts(seq, "  Priority Flow Control   :\t");
			for (prio = 0; prio < 8; prio++) {
				int pfcen = (dcb->pfcen >> 1 * (7 - prio))
					    & 0x1;
				seq_printf(seq, " %3d", pfcen);
			}
			seq_puts(seq, "\n");

			seq_printf(seq, "  Max PFC Traffic Classes [%3d  ]\n",
				   dcb->pfc_num_tcs_supported);

			seq_puts(seq, "\n");
		}

		if (dcb->msgs & CXGB4_DCB_FW_APP_ID) {
			int app, napps;

			seq_puts(seq, "  Application Information:\n");
			seq_puts(seq, "  App    Priority    Selection         Protocol\n");
			seq_puts(seq, "  Index  Map         Field             ID\n");
			for (app = 0, napps = 0;
			     app < CXGB4_MAX_DCBX_APP_SUPPORTED; app++) {
				struct app_priority *ap;
				static const char * const sel_names[] = {
					"Ethertype",
					"Socket TCP",
					"Socket UDP",
					"Socket All",
				};
				const char *sel_name;

				ap = &dcb->app_priority[app];
				/* skip empty slots */
				if (ap->protocolid == 0)
					continue;
				napps++;

				if (ap->sel_field < ARRAY_SIZE(sel_names))
					sel_name = sel_names[ap->sel_field];
				else
					sel_name = "UNKNOWN";

				seq_printf(seq, "  %3d    %#04x        %-10s (%d)    %#06x (%d)\n",
					   app,
					   ap->user_prio_map,
					   sel_name, ap->sel_field,
					   ap->protocolid, ap->protocolid);
			}
			if (napps == 0)
				seq_puts(seq, "    --- None ---\n");
		}
	}
	return 0;
}

static inline void *dcb_info_get_idx(struct adapter *adap, loff_t pos)
{
	return (pos <= adap->params.nports
		? (void *)((uintptr_t)pos + 1)
		: NULL);
}

static void *dcb_info_start(struct seq_file *seq, loff_t *pos)
{
	struct adapter *adap = seq->private;

	return (*pos
		? dcb_info_get_idx(adap, *pos)
		: SEQ_START_TOKEN);
}

static void dcb_info_stop(struct seq_file *seq, void *v)
{
}

static void *dcb_info_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct adapter *adap = seq->private;

	(*pos)++;
	return dcb_info_get_idx(adap, *pos);
}

static const struct seq_operations dcb_info_seq_ops = {
	.start = dcb_info_start,
	.next  = dcb_info_next,
	.stop  = dcb_info_stop,
	.show  = dcb_info_show
};

static int dcb_info_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &dcb_info_seq_ops);

	if (!res) {
		struct seq_file *seq = file->private_data;

		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations dcb_info_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = dcb_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};
#endif /* CONFIG_CHELSIO_T4_DCB */

static int resources_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	struct pf_resources *pfres = &adapter->params.pfres;

	#define S(desc, fmt, var) \
		seq_printf(seq, "%-60s " fmt "\n", \
			   desc " (" #var "):", pfres->var)

	S("Virtual Interfaces", "%d", nvi);
	S("Egress Queues", "%d", neq);
	S("Ethernet Control", "%d", nethctrl);
	S("Ingress Queues/w Free Lists/Interrupts", "%d", niqflint);
	S("Ingress Queues", "%d", niq);
	S("Traffic Class", "%d", tc);
	S("Port Access Rights Mask", "%#x", pmask);
	S("MAC Address Filters", "%d", nexactf);
	S("Firmware Command Read Capabilities", "%#x", r_caps);
	S("Firmware Command Write/Execute Capabilities", "%#x", wx_caps);

	#undef S

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(resources);

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
	BUG();
	return NULL;
}

static int sge_qinfo_uld_txq_entries(const struct adapter *adap, int uld)
{
	const struct sge_uld_txq_info *utxq_info = adap->sge.uld_txq_info[uld];

	if (!utxq_info)
		return 0;

	return DIV_ROUND_UP(utxq_info->ntxq, 4);
}

static int sge_qinfo_uld_rspq_entries(const struct adapter *adap, int uld,
				      bool ciq)
{
	const struct sge_uld_rxq_info *urxq_info = adap->sge.uld_rxq_info[uld];

	if (!urxq_info)
		return 0;

	return ciq ? DIV_ROUND_UP(urxq_info->nciq, 4) :
		     DIV_ROUND_UP(urxq_info->nrxq, 4);
}

static int sge_qinfo_uld_rxq_entries(const struct adapter *adap, int uld)
{
	return sge_qinfo_uld_rspq_entries(adap, uld, false);
}

static int sge_qinfo_uld_ciq_entries(const struct adapter *adap, int uld)
{
	return sge_qinfo_uld_rspq_entries(adap, uld, true);
}

static int sge_qinfo_show(struct seq_file *seq, void *v)
{
	int uld_rxq_entries[CXGB4_ULD_MAX] = { 0 };
	int uld_ciq_entries[CXGB4_ULD_MAX] = { 0 };
	int uld_txq_entries[CXGB4_TX_MAX] = { 0 };
	const struct sge_uld_txq_info *utxq_info;
	const struct sge_uld_rxq_info *urxq_info;
	struct adapter *adap = seq->private;
	int i, n, r = (uintptr_t)v - 1;
	int eth_entries, ctrl_entries;
	struct sge *s = &adap->sge;

	eth_entries = DIV_ROUND_UP(adap->sge.ethqsets, 4);
	ctrl_entries = DIV_ROUND_UP(MAX_CTRL_QUEUES, 4);

	mutex_lock(&uld_mutex);
	if (s->uld_txq_info)
		for (i = 0; i < ARRAY_SIZE(uld_txq_entries); i++)
			uld_txq_entries[i] = sge_qinfo_uld_txq_entries(adap, i);

	if (s->uld_rxq_info) {
		for (i = 0; i < ARRAY_SIZE(uld_rxq_entries); i++) {
			uld_rxq_entries[i] = sge_qinfo_uld_rxq_entries(adap, i);
			uld_ciq_entries[i] = sge_qinfo_uld_ciq_entries(adap, i);
		}
	}

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
#define T3(fmt_spec, s, v) S3(fmt_spec, s, tx[i].v)
#define T(s, v) S3("u", s, tx[i].v)
#define TL(s, v) T3("lu", s, v)
#define R3(fmt_spec, s, v) S3(fmt_spec, s, rx[i].v)
#define R(s, v) S3("u", s, rx[i].v)
#define RL(s, v) R3("lu", s, v)

	if (r < eth_entries) {
		int base_qset = r * 4;
		const struct sge_eth_rxq *rx = &s->ethrxq[base_qset];
		const struct sge_eth_txq *tx = &s->ethtxq[base_qset];

		n = min(4, s->ethqsets - 4 * r);

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
		S3("u", "Intr pktcnt:", s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);
		RL("RxPackets:", stats.pkts);
		RL("RxCSO:", stats.rx_cso);
		RL("VLANxtract:", stats.vlan_ex);
		RL("LROmerged:", stats.lro_merged);
		RL("LROpackets:", stats.lro_pkts);
		RL("RxDrops:", stats.rx_drops);
		RL("RxBadPkts:", stats.bad_rx_pkts);
		TL("TSO:", tso);
		TL("TxCSO:", tx_cso);
		TL("VLANins:", vlan_ins);
		TL("TxQFull:", q.stops);
		TL("TxQRestarts:", q.restarts);
		TL("TxMapErr:", mapping_err);
		RL("FLAllocErr:", fl.alloc_failed);
		RL("FLLrgAlcErr:", fl.large_alloc_failed);
		RL("FLMapErr:", fl.mapping_err);
		RL("FLLow:", fl.low);
		RL("FLStarving:", fl.starving);

		goto unlock;
	}

	r -= eth_entries;
	if (r < uld_txq_entries[CXGB4_TX_OFLD]) {
		const struct sge_uld_txq *tx;

		utxq_info = s->uld_txq_info[CXGB4_TX_OFLD];
		tx = &utxq_info->uldtxq[r * 4];
		n = min(4, utxq_info->ntxq - 4 * r);

		S("QType:", "OFLD-TXQ");
		T("TxQ ID:", q.cntxt_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ CIDX:", q.cidx);
		T("TxQ PIDX:", q.pidx);

		goto unlock;
	}

	r -= uld_txq_entries[CXGB4_TX_OFLD];
	if (r < uld_rxq_entries[CXGB4_ULD_RDMA]) {
		const struct sge_ofld_rxq *rx;

		urxq_info = s->uld_rxq_info[CXGB4_ULD_RDMA];
		rx = &urxq_info->uldrxq[r * 4];
		n = min(4, urxq_info->nrxq - 4 * r);

		S("QType:", "RDMA-CPL");
		S("Interface:",
		  rx[i].rspq.netdev ? rx[i].rspq.netdev->name : "N/A");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);

		goto unlock;
	}

	r -= uld_rxq_entries[CXGB4_ULD_RDMA];
	if (r < uld_ciq_entries[CXGB4_ULD_RDMA]) {
		const struct sge_ofld_rxq *rx;
		int ciq_idx = 0;

		urxq_info = s->uld_rxq_info[CXGB4_ULD_RDMA];
		ciq_idx = urxq_info->nrxq + (r * 4);
		rx = &urxq_info->uldrxq[ciq_idx];
		n = min(4, urxq_info->nciq - 4 * r);

		S("QType:", "RDMA-CIQ");
		S("Interface:",
		  rx[i].rspq.netdev ? rx[i].rspq.netdev->name : "N/A");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);

		goto unlock;
	}

	r -= uld_ciq_entries[CXGB4_ULD_RDMA];
	if (r < uld_rxq_entries[CXGB4_ULD_ISCSI]) {
		const struct sge_ofld_rxq *rx;

		urxq_info = s->uld_rxq_info[CXGB4_ULD_ISCSI];
		rx = &urxq_info->uldrxq[r * 4];
		n = min(4, urxq_info->nrxq - 4 * r);

		S("QType:", "iSCSI");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);

		goto unlock;
	}

	r -= uld_rxq_entries[CXGB4_ULD_ISCSI];
	if (r < uld_rxq_entries[CXGB4_ULD_ISCSIT]) {
		const struct sge_ofld_rxq *rx;

		urxq_info = s->uld_rxq_info[CXGB4_ULD_ISCSIT];
		rx = &urxq_info->uldrxq[r * 4];
		n = min(4, urxq_info->nrxq - 4 * r);

		S("QType:", "iSCSIT");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);

		goto unlock;
	}

	r -= uld_rxq_entries[CXGB4_ULD_ISCSIT];
	if (r < uld_rxq_entries[CXGB4_ULD_TLS]) {
		const struct sge_ofld_rxq *rx;

		urxq_info = s->uld_rxq_info[CXGB4_ULD_TLS];
		rx = &urxq_info->uldrxq[r * 4];
		n = min(4, urxq_info->nrxq - 4 * r);

		S("QType:", "TLS");
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		R("RspQ CIDX:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		S3("u", "Intr delay:", qtimer_val(adap, &rx[i].rspq));
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);

		goto unlock;
	}

	r -= uld_rxq_entries[CXGB4_ULD_TLS];
	if (r < uld_txq_entries[CXGB4_TX_CRYPTO]) {
		const struct sge_ofld_rxq *rx;
		const struct sge_uld_txq *tx;

		utxq_info = s->uld_txq_info[CXGB4_TX_CRYPTO];
		urxq_info = s->uld_rxq_info[CXGB4_ULD_CRYPTO];
		tx = &utxq_info->uldtxq[r * 4];
		rx = &urxq_info->uldrxq[r * 4];
		n = min(4, utxq_info->ntxq - 4 * r);

		S("QType:", "Crypto");
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
		S3("u", "Intr pktcnt:",	s->counter_val[rx[i].rspq.pktcnt_idx]);
		R("FL ID:", fl.cntxt_id);
		R("FL size:", fl.size - 8);
		R("FL pend:", fl.pend_cred);
		R("FL avail:", fl.avail);
		R("FL PIDX:", fl.pidx);
		R("FL CIDX:", fl.cidx);

		goto unlock;
	}

	r -= uld_txq_entries[CXGB4_TX_CRYPTO];
	if (r < ctrl_entries) {
		const struct sge_ctrl_txq *tx = &s->ctrlq[r * 4];

		n = min(4, adap->params.nports - 4 * r);

		S("QType:", "Control");
		T("TxQ ID:", q.cntxt_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ CIDX:", q.cidx);
		T("TxQ PIDX:", q.pidx);
		TL("TxQFull:", q.stops);
		TL("TxQRestarts:", q.restarts);

		goto unlock;
	}

	r -= ctrl_entries;
	if (r < 1) {
		const struct sge_rspq *evtq = &s->fw_evtq;

		seq_printf(seq, "%-12s %16s\n", "QType:", "FW event queue");
		seq_printf(seq, "%-12s %16u\n", "RspQ ID:", evtq->abs_id);
		seq_printf(seq, "%-12s %16u\n", "RspQ size:", evtq->size);
		seq_printf(seq, "%-12s %16u\n", "RspQE size:", evtq->iqe_len);
		seq_printf(seq, "%-12s %16u\n", "RspQ CIDX:", evtq->cidx);
		seq_printf(seq, "%-12s %16u\n", "RspQ Gen:", evtq->gen);
		seq_printf(seq, "%-12s %16u\n", "Intr delay:",
			   qtimer_val(adap, evtq));
		seq_printf(seq, "%-12s %16u\n", "Intr pktcnt:",
			   s->counter_val[evtq->pktcnt_idx]);

		goto unlock;
	}

unlock:
	mutex_unlock(&uld_mutex);
#undef R
#undef RL
#undef T
#undef TL
#undef S
#undef R3
#undef T3
#undef S3
	return 0;
}

static int sge_queue_entries(const struct adapter *adap)
{
	int tot_uld_entries = 0;
	int i;

	mutex_lock(&uld_mutex);
	for (i = 0; i < CXGB4_TX_MAX; i++)
		tot_uld_entries += sge_qinfo_uld_txq_entries(adap, i);

	for (i = 0; i < CXGB4_ULD_MAX; i++) {
		tot_uld_entries += sge_qinfo_uld_rxq_entries(adap, i);
		tot_uld_entries += sge_qinfo_uld_ciq_entries(adap, i);
	}
	mutex_unlock(&uld_mutex);

	return DIV_ROUND_UP(adap->sge.ethqsets, 4) +
	       tot_uld_entries +
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

	mem = (uintptr_t)file->private_data & 0x7;
	adap = file->private_data - mem;

	(void)t4_fwcache(adap, FW_PARAM_DEV_FWCACHE_FLUSH);

	return 0;
}

static ssize_t mem_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	loff_t pos = *ppos;
	loff_t avail = file_inode(file)->i_size;
	unsigned int mem = (uintptr_t)file->private_data & 0x7;
	struct adapter *adap = file->private_data - mem;
	__be32 *data;
	int ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= avail)
		return 0;
	if (count > avail - pos)
		count = avail - pos;

	data = kvzalloc(count, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock(&adap->win0_lock);
	ret = t4_memory_rw(adap, 0, mem, pos, count, data, T4_MEMORY_READ);
	spin_unlock(&adap->win0_lock);
	if (ret) {
		kvfree(data);
		return ret;
	}
	ret = copy_to_user(buf, data, count);

	kvfree(data);
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

static int tid_info_show(struct seq_file *seq, void *v)
{
	unsigned int tid_start = 0;
	struct adapter *adap = seq->private;
	const struct tid_info *t = &adap->tids;
	enum chip_type chip = CHELSIO_CHIP_VERSION(adap->params.chip);

	if (chip > CHELSIO_T5)
		tid_start = t4_read_reg(adap, LE_DB_ACTIVE_TABLE_START_INDEX_A);

	if (t4_read_reg(adap, LE_DB_CONFIG_A) & HASHEN_F) {
		unsigned int sb;
		seq_printf(seq, "Connections in use: %u\n",
			   atomic_read(&t->conns_in_use));

		if (chip <= CHELSIO_T5)
			sb = t4_read_reg(adap, LE_DB_SERVER_INDEX_A) / 4;
		else
			sb = t4_read_reg(adap, LE_DB_SRVR_START_INDEX_A);

		if (sb) {
			seq_printf(seq, "TID range: %u..%u/%u..%u", tid_start,
				   sb - 1, adap->tids.hash_base,
				   t->ntids - 1);
			seq_printf(seq, ", in use: %u/%u\n",
				   atomic_read(&t->tids_in_use),
				   atomic_read(&t->hash_tids_in_use));
		} else if (adap->flags & CXGB4_FW_OFLD_CONN) {
			seq_printf(seq, "TID range: %u..%u/%u..%u",
				   t->aftid_base,
				   t->aftid_end,
				   adap->tids.hash_base,
				   t->ntids - 1);
			seq_printf(seq, ", in use: %u/%u\n",
				   atomic_read(&t->tids_in_use),
				   atomic_read(&t->hash_tids_in_use));
		} else {
			seq_printf(seq, "TID range: %u..%u",
				   adap->tids.hash_base,
				   t->ntids - 1);
			seq_printf(seq, ", in use: %u\n",
				   atomic_read(&t->hash_tids_in_use));
		}
	} else if (t->ntids) {
		seq_printf(seq, "Connections in use: %u\n",
			   atomic_read(&t->conns_in_use));

		seq_printf(seq, "TID range: %u..%u", tid_start,
			   tid_start + t->ntids - 1);
		seq_printf(seq, ", in use: %u\n",
			   atomic_read(&t->tids_in_use));
	}

	if (t->nstids)
		seq_printf(seq, "STID range: %u..%u, in use-IPv4/IPv6: %u/%u\n",
			   (!t->stid_base &&
			   (chip <= CHELSIO_T5)) ?
			   t->stid_base + 1 : t->stid_base,
			   t->stid_base + t->nstids - 1,
			   t->stids_in_use - t->v6_stids_in_use,
			   t->v6_stids_in_use);

	if (t->natids)
		seq_printf(seq, "ATID range: 0..%u, in use: %u\n",
			   t->natids - 1, t->atids_in_use);
	seq_printf(seq, "FTID range: %u..%u\n", t->ftid_base,
		   t->ftid_base + t->nftids - 1);
	if (t->nsftids)
		seq_printf(seq, "SFTID range: %u..%u in use: %u\n",
			   t->sftid_base, t->sftid_base + t->nsftids - 2,
			   t->sftids_in_use);
	if (t->ntids)
		seq_printf(seq, "HW TID usage: %u IP users, %u IPv6 users\n",
			   t4_read_reg(adap, LE_DB_ACT_CNT_IPV4_A),
			   t4_read_reg(adap, LE_DB_ACT_CNT_IPV6_A));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tid_info);

static void add_debugfs_mem(struct adapter *adap, const char *name,
			    unsigned int idx, unsigned int size_mb)
{
	debugfs_create_file_size(name, 0400, adap->debugfs_root,
				 (void *)adap + idx, &mem_debugfs_fops,
				 size_mb << 20);
}

static ssize_t blocked_fl_read(struct file *filp, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	int len;
	const struct adapter *adap = filp->private_data;
	char *buf;
	ssize_t size = (adap->sge.egr_sz + 3) / 4 +
			adap->sge.egr_sz / 32 + 2; /* includes ,/\n/\0 */

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = snprintf(buf, size - 1, "%*pb\n",
		       adap->sge.egr_sz, adap->sge.blocked_fl);
	len += sprintf(buf + len, "\n");
	size = simple_read_from_buffer(ubuf, count, ppos, buf, len);
	kvfree(buf);
	return size;
}

static ssize_t blocked_fl_write(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	int err;
	unsigned long *t;
	struct adapter *adap = filp->private_data;

	t = kcalloc(BITS_TO_LONGS(adap->sge.egr_sz), sizeof(long), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	err = bitmap_parse_user(ubuf, count, t, adap->sge.egr_sz);
	if (err) {
		kvfree(t);
		return err;
	}

	bitmap_copy(adap->sge.blocked_fl, t, adap->sge.egr_sz);
	kvfree(t);
	return count;
}

static const struct file_operations blocked_fl_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	.read    = blocked_fl_read,
	.write   = blocked_fl_write,
	.llseek  = generic_file_llseek,
};

static void mem_region_show(struct seq_file *seq, const char *name,
			    unsigned int from, unsigned int to)
{
	char buf[40];

	string_get_size((u64)to - from + 1, 1, STRING_UNITS_2, buf,
			sizeof(buf));
	seq_printf(seq, "%-15s %#x-%#x [%s]\n", name, from, to, buf);
}

static int meminfo_show(struct seq_file *seq, void *v)
{
	static const char * const memory[] = { "EDC0:", "EDC1:", "MC:",
					       "MC0:", "MC1:", "HMA:"};
	struct adapter *adap = seq->private;
	struct cudbg_meminfo meminfo;
	int i, rc;

	memset(&meminfo, 0, sizeof(struct cudbg_meminfo));
	rc = cudbg_fill_meminfo(adap, &meminfo);
	if (rc)
		return -ENXIO;

	for (i = 0; i < meminfo.avail_c; i++)
		mem_region_show(seq, memory[meminfo.avail[i].idx],
				meminfo.avail[i].base,
				meminfo.avail[i].limit - 1);

	seq_putc(seq, '\n');
	for (i = 0; i < meminfo.mem_c; i++) {
		if (meminfo.mem[i].idx >= ARRAY_SIZE(cudbg_region))
			continue;                        /* skip holes */
		if (!meminfo.mem[i].limit)
			meminfo.mem[i].limit =
				i < meminfo.mem_c - 1 ?
				meminfo.mem[i + 1].base - 1 : ~0;
		mem_region_show(seq, cudbg_region[meminfo.mem[i].idx],
				meminfo.mem[i].base, meminfo.mem[i].limit);
	}

	seq_putc(seq, '\n');
	mem_region_show(seq, "uP RAM:", meminfo.up_ram_lo, meminfo.up_ram_hi);
	mem_region_show(seq, "uP Extmem2:", meminfo.up_extmem2_lo,
			meminfo.up_extmem2_hi);

	seq_printf(seq, "\n%u Rx pages (%u free) of size %uKiB for %u channels\n",
		   meminfo.rx_pages_data[0], meminfo.free_rx_cnt,
		   meminfo.rx_pages_data[1], meminfo.rx_pages_data[2]);

	seq_printf(seq, "%u Tx pages (%u free) of size %u%ciB for %u channels\n",
		   meminfo.tx_pages_data[0], meminfo.free_tx_cnt,
		   meminfo.tx_pages_data[1], meminfo.tx_pages_data[2],
		   meminfo.tx_pages_data[3]);

	seq_printf(seq, "%u p-structs (%u free)\n\n",
		   meminfo.p_structs, meminfo.p_structs_free_cnt);

	for (i = 0; i < 4; i++)
		/* For T6 these are MAC buffer groups */
		seq_printf(seq, "Port %d using %u pages out of %u allocated\n",
			   i, meminfo.port_used[i], meminfo.port_alloc[i]);

	for (i = 0; i < adap->params.arch.nchan; i++)
		/* For T6 these are MAC buffer groups */
		seq_printf(seq,
			   "Loopback %d using %u pages out of %u allocated\n",
			   i, meminfo.loopback_used[i],
			   meminfo.loopback_alloc[i]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(meminfo);

static int chcr_stats_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;

	seq_puts(seq, "Chelsio Crypto Accelerator Stats \n");
	seq_printf(seq, "Cipher Ops: %10u \n",
		   atomic_read(&adap->chcr_stats.cipher_rqst));
	seq_printf(seq, "Digest Ops: %10u \n",
		   atomic_read(&adap->chcr_stats.digest_rqst));
	seq_printf(seq, "Aead Ops: %10u \n",
		   atomic_read(&adap->chcr_stats.aead_rqst));
	seq_printf(seq, "Completion: %10u \n",
		   atomic_read(&adap->chcr_stats.complete));
	seq_printf(seq, "Error: %10u \n",
		   atomic_read(&adap->chcr_stats.error));
	seq_printf(seq, "Fallback: %10u \n",
		   atomic_read(&adap->chcr_stats.fallback));
	seq_printf(seq, "IPSec PDU: %10u\n",
		   atomic_read(&adap->chcr_stats.ipsec_cnt));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(chcr_stats);

#define PRINT_ADAP_STATS(string, value) \
	seq_printf(seq, "%-25s %-20llu\n", (string), \
		   (unsigned long long)(value))

#define PRINT_CH_STATS(string, value) \
do { \
	seq_printf(seq, "%-25s ", (string)); \
	for (i = 0; i < adap->params.arch.nchan; i++) \
		seq_printf(seq, "%-20llu ", \
			   (unsigned long long)stats.value[i]); \
	seq_printf(seq, "\n"); \
} while (0)

#define PRINT_CH_STATS2(string, value) \
do { \
	seq_printf(seq, "%-25s ", (string)); \
	for (i = 0; i < adap->params.arch.nchan; i++) \
		seq_printf(seq, "%-20llu ", \
			   (unsigned long long)stats[i].value); \
	seq_printf(seq, "\n"); \
} while (0)

static void show_tcp_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_tcp_stats v4, v6;

	spin_lock(&adap->stats_lock);
	t4_tp_get_tcp_stats(adap, &v4, &v6, false);
	spin_unlock(&adap->stats_lock);

	PRINT_ADAP_STATS("tcp_ipv4_out_rsts:", v4.tcp_out_rsts);
	PRINT_ADAP_STATS("tcp_ipv4_in_segs:", v4.tcp_in_segs);
	PRINT_ADAP_STATS("tcp_ipv4_out_segs:", v4.tcp_out_segs);
	PRINT_ADAP_STATS("tcp_ipv4_retrans_segs:", v4.tcp_retrans_segs);
	PRINT_ADAP_STATS("tcp_ipv6_out_rsts:", v6.tcp_out_rsts);
	PRINT_ADAP_STATS("tcp_ipv6_in_segs:", v6.tcp_in_segs);
	PRINT_ADAP_STATS("tcp_ipv6_out_segs:", v6.tcp_out_segs);
	PRINT_ADAP_STATS("tcp_ipv6_retrans_segs:", v6.tcp_retrans_segs);
}

static void show_ddp_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_usm_stats stats;

	spin_lock(&adap->stats_lock);
	t4_get_usm_stats(adap, &stats, false);
	spin_unlock(&adap->stats_lock);

	PRINT_ADAP_STATS("usm_ddp_frames:", stats.frames);
	PRINT_ADAP_STATS("usm_ddp_octets:", stats.octets);
	PRINT_ADAP_STATS("usm_ddp_drops:", stats.drops);
}

static void show_rdma_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_rdma_stats stats;

	spin_lock(&adap->stats_lock);
	t4_tp_get_rdma_stats(adap, &stats, false);
	spin_unlock(&adap->stats_lock);

	PRINT_ADAP_STATS("rdma_no_rqe_mod_defer:", stats.rqe_dfr_mod);
	PRINT_ADAP_STATS("rdma_no_rqe_pkt_defer:", stats.rqe_dfr_pkt);
}

static void show_tp_err_adapter_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_err_stats stats;

	spin_lock(&adap->stats_lock);
	t4_tp_get_err_stats(adap, &stats, false);
	spin_unlock(&adap->stats_lock);

	PRINT_ADAP_STATS("tp_err_ofld_no_neigh:", stats.ofld_no_neigh);
	PRINT_ADAP_STATS("tp_err_ofld_cong_defer:", stats.ofld_cong_defer);
}

static void show_cpl_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_cpl_stats stats;
	u8 i;

	spin_lock(&adap->stats_lock);
	t4_tp_get_cpl_stats(adap, &stats, false);
	spin_unlock(&adap->stats_lock);

	PRINT_CH_STATS("tp_cpl_requests:", req);
	PRINT_CH_STATS("tp_cpl_responses:", rsp);
}

static void show_tp_err_channel_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_err_stats stats;
	u8 i;

	spin_lock(&adap->stats_lock);
	t4_tp_get_err_stats(adap, &stats, false);
	spin_unlock(&adap->stats_lock);

	PRINT_CH_STATS("tp_mac_in_errs:", mac_in_errs);
	PRINT_CH_STATS("tp_hdr_in_errs:", hdr_in_errs);
	PRINT_CH_STATS("tp_tcp_in_errs:", tcp_in_errs);
	PRINT_CH_STATS("tp_tcp6_in_errs:", tcp6_in_errs);
	PRINT_CH_STATS("tp_tnl_cong_drops:", tnl_cong_drops);
	PRINT_CH_STATS("tp_tnl_tx_drops:", tnl_tx_drops);
	PRINT_CH_STATS("tp_ofld_vlan_drops:", ofld_vlan_drops);
	PRINT_CH_STATS("tp_ofld_chan_drops:", ofld_chan_drops);
}

static void show_fcoe_stats(struct seq_file *seq)
{
	struct adapter *adap = seq->private;
	struct tp_fcoe_stats stats[NCHAN];
	u8 i;

	spin_lock(&adap->stats_lock);
	for (i = 0; i < adap->params.arch.nchan; i++)
		t4_get_fcoe_stats(adap, i, &stats[i], false);
	spin_unlock(&adap->stats_lock);

	PRINT_CH_STATS2("fcoe_octets_ddp", octets_ddp);
	PRINT_CH_STATS2("fcoe_frames_ddp", frames_ddp);
	PRINT_CH_STATS2("fcoe_frames_drop", frames_drop);
}

#undef PRINT_CH_STATS2
#undef PRINT_CH_STATS
#undef PRINT_ADAP_STATS

static int tp_stats_show(struct seq_file *seq, void *v)
{
	struct adapter *adap = seq->private;

	seq_puts(seq, "\n--------Adapter Stats--------\n");
	show_tcp_stats(seq);
	show_ddp_stats(seq);
	show_rdma_stats(seq);
	show_tp_err_adapter_stats(seq);

	seq_puts(seq, "\n-------- Channel Stats --------\n");
	if (adap->params.arch.nchan == NCHAN)
		seq_printf(seq, "%-25s %-20s %-20s %-20s %-20s\n",
			   " ", "channel 0", "channel 1",
			   "channel 2", "channel 3");
	else
		seq_printf(seq, "%-25s %-20s %-20s\n",
			   " ", "channel 0", "channel 1");
	show_cpl_stats(seq);
	show_tp_err_channel_stats(seq);
	show_fcoe_stats(seq);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tp_stats);

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
	u32 size = 0;
	struct dentry *de;

	static struct t4_debugfs_entry t4_debugfs_files[] = {
		{ "cim_la", &cim_la_fops, 0400, 0 },
		{ "cim_pif_la", &cim_pif_la_fops, 0400, 0 },
		{ "cim_ma_la", &cim_ma_la_fops, 0400, 0 },
		{ "cim_qcfg", &cim_qcfg_fops, 0400, 0 },
		{ "clk", &clk_fops, 0400, 0 },
		{ "devlog", &devlog_fops, 0400, 0 },
		{ "mboxlog", &mboxlog_fops, 0400, 0 },
		{ "mbox0", &mbox_debugfs_fops, 0600, 0 },
		{ "mbox1", &mbox_debugfs_fops, 0600, 1 },
		{ "mbox2", &mbox_debugfs_fops, 0600, 2 },
		{ "mbox3", &mbox_debugfs_fops, 0600, 3 },
		{ "mbox4", &mbox_debugfs_fops, 0600, 4 },
		{ "mbox5", &mbox_debugfs_fops, 0600, 5 },
		{ "mbox6", &mbox_debugfs_fops, 0600, 6 },
		{ "mbox7", &mbox_debugfs_fops, 0600, 7 },
		{ "trace0", &mps_trc_debugfs_fops, 0600, 0 },
		{ "trace1", &mps_trc_debugfs_fops, 0600, 1 },
		{ "trace2", &mps_trc_debugfs_fops, 0600, 2 },
		{ "trace3", &mps_trc_debugfs_fops, 0600, 3 },
		{ "l2t", &t4_l2t_fops, 0400, 0},
		{ "mps_tcam", &mps_tcam_debugfs_fops, 0400, 0 },
		{ "rss", &rss_debugfs_fops, 0400, 0 },
		{ "rss_config", &rss_config_fops, 0400, 0 },
		{ "rss_key", &rss_key_debugfs_fops, 0400, 0 },
		{ "rss_pf_config", &rss_pf_config_debugfs_fops, 0400, 0 },
		{ "rss_vf_config", &rss_vf_config_debugfs_fops, 0400, 0 },
		{ "resources", &resources_fops, 0400, 0 },
#ifdef CONFIG_CHELSIO_T4_DCB
		{ "dcb_info", &dcb_info_debugfs_fops, 0400, 0 },
#endif
		{ "sge_qinfo", &sge_qinfo_debugfs_fops, 0400, 0 },
		{ "ibq_tp0",  &cim_ibq_fops, 0400, 0 },
		{ "ibq_tp1",  &cim_ibq_fops, 0400, 1 },
		{ "ibq_ulp",  &cim_ibq_fops, 0400, 2 },
		{ "ibq_sge0", &cim_ibq_fops, 0400, 3 },
		{ "ibq_sge1", &cim_ibq_fops, 0400, 4 },
		{ "ibq_ncsi", &cim_ibq_fops, 0400, 5 },
		{ "obq_ulp0", &cim_obq_fops, 0400, 0 },
		{ "obq_ulp1", &cim_obq_fops, 0400, 1 },
		{ "obq_ulp2", &cim_obq_fops, 0400, 2 },
		{ "obq_ulp3", &cim_obq_fops, 0400, 3 },
		{ "obq_sge",  &cim_obq_fops, 0400, 4 },
		{ "obq_ncsi", &cim_obq_fops, 0400, 5 },
		{ "tp_la", &tp_la_fops, 0400, 0 },
		{ "ulprx_la", &ulprx_la_fops, 0400, 0 },
		{ "sensors", &sensors_fops, 0400, 0 },
		{ "pm_stats", &pm_stats_debugfs_fops, 0400, 0 },
		{ "tx_rate", &tx_rate_fops, 0400, 0 },
		{ "cctrl", &cctrl_tbl_fops, 0400, 0 },
#if IS_ENABLED(CONFIG_IPV6)
		{ "clip_tbl", &clip_tbl_fops, 0400, 0 },
#endif
		{ "tids", &tid_info_fops, 0400, 0},
		{ "blocked_fl", &blocked_fl_fops, 0600, 0 },
		{ "meminfo", &meminfo_fops, 0400, 0 },
		{ "crypto", &chcr_stats_fops, 0400, 0 },
		{ "tp_stats", &tp_stats_fops, 0400, 0 },
	};

	/* Debug FS nodes common to all T5 and later adapters.
	 */
	static struct t4_debugfs_entry t5_debugfs_files[] = {
		{ "obq_sge_rx_q0", &cim_obq_fops, 0400, 6 },
		{ "obq_sge_rx_q1", &cim_obq_fops, 0400, 7 },
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
	if (is_t5(adap->params.chip)) {
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
	} else {
		if (i & EXT_MEM_ENABLE_F) {
			size = t4_read_reg(adap, MA_EXT_MEMORY_BAR_A);
			add_debugfs_mem(adap, "mc", MEM_MC,
					EXT_MEM_SIZE_G(size));
		}

		if (i & HMA_MUX_F) {
			size = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
			add_debugfs_mem(adap, "hma", MEM_HMA,
					EXT_MEM1_SIZE_G(size));
		}
	}

	de = debugfs_create_file_size("flash", 0400, adap->debugfs_root, adap,
				      &flash_debugfs_fops, adap->params.sf_size);
	debugfs_create_bool("use_backdoor", 0600,
			    adap->debugfs_root, &adap->use_bd);
	debugfs_create_bool("trace_rss", 0600,
			    adap->debugfs_root, &adap->trace_rss);

	return 0;
}
