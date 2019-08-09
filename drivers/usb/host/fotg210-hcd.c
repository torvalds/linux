// SPDX-License-Identifier: GPL-2.0+
/* Faraday FOTG210 EHCI-like driver
 *
 * Copyright (c) 2013 Faraday Technology Corporation
 *
 * Author: Yuan-Hsin Chen <yhchen@faraday-tech.com>
 *	   Feng-Hsin Chiang <john453@faraday-tech.com>
 *	   Po-Yu Chuang <ratbert.chuang@gmail.com>
 *
 * Most of code borrowed from the Linux-3.7 EHCI driver
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

#define DRIVER_AUTHOR "Yuan-Hsin Chen"
#define DRIVER_DESC "FOTG210 Host Controller (EHCI) Driver"
static const char hcd_name[] = "fotg210_hcd";

#undef FOTG210_URB_TRACE
#define FOTG210_STATS

/* magic numbers that can affect system performance */
#define FOTG210_TUNE_CERR	3 /* 0-3 qtd retries; 0 == don't stop */
#define FOTG210_TUNE_RL_HS	4 /* nak throttle; see 4.9 */
#define FOTG210_TUNE_RL_TT	0
#define FOTG210_TUNE_MULT_HS	1 /* 1-3 transactions/uframe; 4.10.3 */
#define FOTG210_TUNE_MULT_TT	1

/* Some drivers think it's safe to schedule isochronous transfers more than 256
 * ms into the future (partly as a result of an old bug in the scheduling
 * code).  In an attempt to avoid trouble, we will use a minimum scheduling
 * length of 512 frames instead of 256.
 */
#define FOTG210_TUNE_FLS 1 /* (medium) 512-frame schedule */

/* Initial IRQ latency:  faster than hw default */
static int log2_irq_thresh; /* 0 to 6 */
module_param(log2_irq_thresh, int, S_IRUGO);
MODULE_PARM_DESC(log2_irq_thresh, "log2 IRQ latency, 1-64 microframes");

/* initial park setting:  slower than hw default */
static unsigned park;
module_param(park, uint, S_IRUGO);
MODULE_PARM_DESC(park, "park setting; 1-3 back-to-back async packets");

/* for link power management(LPM) feature */
static unsigned int hird;
module_param(hird, int, S_IRUGO);
MODULE_PARM_DESC(hird, "host initiated resume duration, +1 for each 75us");

#define INTR_MASK (STS_IAA | STS_FATAL | STS_PCD | STS_ERR | STS_INT)

#include "fotg210.h"

#define fotg210_dbg(fotg210, fmt, args...) \
	dev_dbg(fotg210_to_hcd(fotg210)->self.controller, fmt, ## args)
#define fotg210_err(fotg210, fmt, args...) \
	dev_err(fotg210_to_hcd(fotg210)->self.controller, fmt, ## args)
#define fotg210_info(fotg210, fmt, args...) \
	dev_info(fotg210_to_hcd(fotg210)->self.controller, fmt, ## args)
#define fotg210_warn(fotg210, fmt, args...) \
	dev_warn(fotg210_to_hcd(fotg210)->self.controller, fmt, ## args)

/* check the values in the HCSPARAMS register (host controller _Structural_
 * parameters) see EHCI spec, Table 2-4 for each value
 */
static void dbg_hcs_params(struct fotg210_hcd *fotg210, char *label)
{
	u32 params = fotg210_readl(fotg210, &fotg210->caps->hcs_params);

	fotg210_dbg(fotg210, "%s hcs_params 0x%x ports=%d\n", label, params,
			HCS_N_PORTS(params));
}

/* check the values in the HCCPARAMS register (host controller _Capability_
 * parameters) see EHCI Spec, Table 2-5 for each value
 */
static void dbg_hcc_params(struct fotg210_hcd *fotg210, char *label)
{
	u32 params = fotg210_readl(fotg210, &fotg210->caps->hcc_params);

	fotg210_dbg(fotg210, "%s hcc_params %04x uframes %s%s\n", label,
			params,
			HCC_PGM_FRAMELISTLEN(params) ? "256/512/1024" : "1024",
			HCC_CANPARK(params) ? " park" : "");
}

static void __maybe_unused
dbg_qtd(const char *label, struct fotg210_hcd *fotg210, struct fotg210_qtd *qtd)
{
	fotg210_dbg(fotg210, "%s td %p n%08x %08x t%08x p0=%08x\n", label, qtd,
			hc32_to_cpup(fotg210, &qtd->hw_next),
			hc32_to_cpup(fotg210, &qtd->hw_alt_next),
			hc32_to_cpup(fotg210, &qtd->hw_token),
			hc32_to_cpup(fotg210, &qtd->hw_buf[0]));
	if (qtd->hw_buf[1])
		fotg210_dbg(fotg210, "  p1=%08x p2=%08x p3=%08x p4=%08x\n",
				hc32_to_cpup(fotg210, &qtd->hw_buf[1]),
				hc32_to_cpup(fotg210, &qtd->hw_buf[2]),
				hc32_to_cpup(fotg210, &qtd->hw_buf[3]),
				hc32_to_cpup(fotg210, &qtd->hw_buf[4]));
}

static void __maybe_unused
dbg_qh(const char *label, struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	struct fotg210_qh_hw *hw = qh->hw;

	fotg210_dbg(fotg210, "%s qh %p n%08x info %x %x qtd %x\n", label, qh,
			hw->hw_next, hw->hw_info1, hw->hw_info2,
			hw->hw_current);

	dbg_qtd("overlay", fotg210, (struct fotg210_qtd *) &hw->hw_qtd_next);
}

static void __maybe_unused
dbg_itd(const char *label, struct fotg210_hcd *fotg210, struct fotg210_itd *itd)
{
	fotg210_dbg(fotg210, "%s[%d] itd %p, next %08x, urb %p\n", label,
			itd->frame, itd, hc32_to_cpu(fotg210, itd->hw_next),
			itd->urb);

	fotg210_dbg(fotg210,
			"  trans: %08x %08x %08x %08x %08x %08x %08x %08x\n",
			hc32_to_cpu(fotg210, itd->hw_transaction[0]),
			hc32_to_cpu(fotg210, itd->hw_transaction[1]),
			hc32_to_cpu(fotg210, itd->hw_transaction[2]),
			hc32_to_cpu(fotg210, itd->hw_transaction[3]),
			hc32_to_cpu(fotg210, itd->hw_transaction[4]),
			hc32_to_cpu(fotg210, itd->hw_transaction[5]),
			hc32_to_cpu(fotg210, itd->hw_transaction[6]),
			hc32_to_cpu(fotg210, itd->hw_transaction[7]));

	fotg210_dbg(fotg210,
			"  buf:   %08x %08x %08x %08x %08x %08x %08x\n",
			hc32_to_cpu(fotg210, itd->hw_bufp[0]),
			hc32_to_cpu(fotg210, itd->hw_bufp[1]),
			hc32_to_cpu(fotg210, itd->hw_bufp[2]),
			hc32_to_cpu(fotg210, itd->hw_bufp[3]),
			hc32_to_cpu(fotg210, itd->hw_bufp[4]),
			hc32_to_cpu(fotg210, itd->hw_bufp[5]),
			hc32_to_cpu(fotg210, itd->hw_bufp[6]));

	fotg210_dbg(fotg210, "  index: %d %d %d %d %d %d %d %d\n",
			itd->index[0], itd->index[1], itd->index[2],
			itd->index[3], itd->index[4], itd->index[5],
			itd->index[6], itd->index[7]);
}

static int __maybe_unused
dbg_status_buf(char *buf, unsigned len, const char *label, u32 status)
{
	return scnprintf(buf, len, "%s%sstatus %04x%s%s%s%s%s%s%s%s%s%s",
			label, label[0] ? " " : "", status,
			(status & STS_ASS) ? " Async" : "",
			(status & STS_PSS) ? " Periodic" : "",
			(status & STS_RECL) ? " Recl" : "",
			(status & STS_HALT) ? " Halt" : "",
			(status & STS_IAA) ? " IAA" : "",
			(status & STS_FATAL) ? " FATAL" : "",
			(status & STS_FLR) ? " FLR" : "",
			(status & STS_PCD) ? " PCD" : "",
			(status & STS_ERR) ? " ERR" : "",
			(status & STS_INT) ? " INT" : "");
}

static int __maybe_unused
dbg_intr_buf(char *buf, unsigned len, const char *label, u32 enable)
{
	return scnprintf(buf, len, "%s%sintrenable %02x%s%s%s%s%s%s",
			label, label[0] ? " " : "", enable,
			(enable & STS_IAA) ? " IAA" : "",
			(enable & STS_FATAL) ? " FATAL" : "",
			(enable & STS_FLR) ? " FLR" : "",
			(enable & STS_PCD) ? " PCD" : "",
			(enable & STS_ERR) ? " ERR" : "",
			(enable & STS_INT) ? " INT" : "");
}

static const char *const fls_strings[] = { "1024", "512", "256", "??" };

static int dbg_command_buf(char *buf, unsigned len, const char *label,
		u32 command)
{
	return scnprintf(buf, len,
			"%s%scommand %07x %s=%d ithresh=%d%s%s%s period=%s%s %s",
			label, label[0] ? " " : "", command,
			(command & CMD_PARK) ? " park" : "(park)",
			CMD_PARK_CNT(command),
			(command >> 16) & 0x3f,
			(command & CMD_IAAD) ? " IAAD" : "",
			(command & CMD_ASE) ? " Async" : "",
			(command & CMD_PSE) ? " Periodic" : "",
			fls_strings[(command >> 2) & 0x3],
			(command & CMD_RESET) ? " Reset" : "",
			(command & CMD_RUN) ? "RUN" : "HALT");
}

static char *dbg_port_buf(char *buf, unsigned len, const char *label, int port,
		u32 status)
{
	char *sig;

	/* signaling state */
	switch (status & (3 << 10)) {
	case 0 << 10:
		sig = "se0";
		break;
	case 1 << 10:
		sig = "k";
		break; /* low speed */
	case 2 << 10:
		sig = "j";
		break;
	default:
		sig = "?";
		break;
	}

	scnprintf(buf, len, "%s%sport:%d status %06x %d sig=%s%s%s%s%s%s%s%s",
			label, label[0] ? " " : "", port, status,
			status >> 25, /*device address */
			sig,
			(status & PORT_RESET) ? " RESET" : "",
			(status & PORT_SUSPEND) ? " SUSPEND" : "",
			(status & PORT_RESUME) ? " RESUME" : "",
			(status & PORT_PEC) ? " PEC" : "",
			(status & PORT_PE) ? " PE" : "",
			(status & PORT_CSC) ? " CSC" : "",
			(status & PORT_CONNECT) ? " CONNECT" : "");

	return buf;
}

/* functions have the "wrong" filename when they're output... */
#define dbg_status(fotg210, label, status) {			\
	char _buf[80];						\
	dbg_status_buf(_buf, sizeof(_buf), label, status);	\
	fotg210_dbg(fotg210, "%s\n", _buf);			\
}

#define dbg_cmd(fotg210, label, command) {			\
	char _buf[80];						\
	dbg_command_buf(_buf, sizeof(_buf), label, command);	\
	fotg210_dbg(fotg210, "%s\n", _buf);			\
}

#define dbg_port(fotg210, label, port, status) {			       \
	char _buf[80];							       \
	fotg210_dbg(fotg210, "%s\n",					       \
			dbg_port_buf(_buf, sizeof(_buf), label, port, status));\
}

/* troubleshooting help: expose state in debugfs */
static int debug_async_open(struct inode *, struct file *);
static int debug_periodic_open(struct inode *, struct file *);
static int debug_registers_open(struct inode *, struct file *);
static int debug_async_open(struct inode *, struct file *);

static ssize_t debug_output(struct file*, char __user*, size_t, loff_t*);
static int debug_close(struct inode *, struct file *);

static const struct file_operations debug_async_fops = {
	.owner		= THIS_MODULE,
	.open		= debug_async_open,
	.read		= debug_output,
	.release	= debug_close,
	.llseek		= default_llseek,
};
static const struct file_operations debug_periodic_fops = {
	.owner		= THIS_MODULE,
	.open		= debug_periodic_open,
	.read		= debug_output,
	.release	= debug_close,
	.llseek		= default_llseek,
};
static const struct file_operations debug_registers_fops = {
	.owner		= THIS_MODULE,
	.open		= debug_registers_open,
	.read		= debug_output,
	.release	= debug_close,
	.llseek		= default_llseek,
};

static struct dentry *fotg210_debug_root;

struct debug_buffer {
	ssize_t (*fill_func)(struct debug_buffer *);	/* fill method */
	struct usb_bus *bus;
	struct mutex mutex;	/* protect filling of buffer */
	size_t count;		/* number of characters filled into buffer */
	char *output_buf;
	size_t alloc_size;
};

static inline char speed_char(u32 scratch)
{
	switch (scratch & (3 << 12)) {
	case QH_FULL_SPEED:
		return 'f';

	case QH_LOW_SPEED:
		return 'l';

	case QH_HIGH_SPEED:
		return 'h';

	default:
		return '?';
	}
}

static inline char token_mark(struct fotg210_hcd *fotg210, __hc32 token)
{
	__u32 v = hc32_to_cpu(fotg210, token);

	if (v & QTD_STS_ACTIVE)
		return '*';
	if (v & QTD_STS_HALT)
		return '-';
	if (!IS_SHORT_READ(v))
		return ' ';
	/* tries to advance through hw_alt_next */
	return '/';
}

static void qh_lines(struct fotg210_hcd *fotg210, struct fotg210_qh *qh,
		char **nextp, unsigned *sizep)
{
	u32 scratch;
	u32 hw_curr;
	struct fotg210_qtd *td;
	unsigned temp;
	unsigned size = *sizep;
	char *next = *nextp;
	char mark;
	__le32 list_end = FOTG210_LIST_END(fotg210);
	struct fotg210_qh_hw *hw = qh->hw;

	if (hw->hw_qtd_next == list_end) /* NEC does this */
		mark = '@';
	else
		mark = token_mark(fotg210, hw->hw_token);
	if (mark == '/') { /* qh_alt_next controls qh advance? */
		if ((hw->hw_alt_next & QTD_MASK(fotg210)) ==
		    fotg210->async->hw->hw_alt_next)
			mark = '#'; /* blocked */
		else if (hw->hw_alt_next == list_end)
			mark = '.'; /* use hw_qtd_next */
		/* else alt_next points to some other qtd */
	}
	scratch = hc32_to_cpup(fotg210, &hw->hw_info1);
	hw_curr = (mark == '*') ? hc32_to_cpup(fotg210, &hw->hw_current) : 0;
	temp = scnprintf(next, size,
			"qh/%p dev%d %cs ep%d %08x %08x(%08x%c %s nak%d)",
			qh, scratch & 0x007f,
			speed_char(scratch),
			(scratch >> 8) & 0x000f,
			scratch, hc32_to_cpup(fotg210, &hw->hw_info2),
			hc32_to_cpup(fotg210, &hw->hw_token), mark,
			(cpu_to_hc32(fotg210, QTD_TOGGLE) & hw->hw_token)
				? "data1" : "data0",
			(hc32_to_cpup(fotg210, &hw->hw_alt_next) >> 1) & 0x0f);
	size -= temp;
	next += temp;

	/* hc may be modifying the list as we read it ... */
	list_for_each_entry(td, &qh->qtd_list, qtd_list) {
		scratch = hc32_to_cpup(fotg210, &td->hw_token);
		mark = ' ';
		if (hw_curr == td->qtd_dma)
			mark = '*';
		else if (hw->hw_qtd_next == cpu_to_hc32(fotg210, td->qtd_dma))
			mark = '+';
		else if (QTD_LENGTH(scratch)) {
			if (td->hw_alt_next == fotg210->async->hw->hw_alt_next)
				mark = '#';
			else if (td->hw_alt_next != list_end)
				mark = '/';
		}
		temp = snprintf(next, size,
				"\n\t%p%c%s len=%d %08x urb %p",
				td, mark, ({ char *tmp;
				 switch ((scratch>>8)&0x03) {
				 case 0:
					tmp = "out";
					break;
				 case 1:
					tmp = "in";
					break;
				 case 2:
					tmp = "setup";
					break;
				 default:
					tmp = "?";
					break;
				 } tmp; }),
				(scratch >> 16) & 0x7fff,
				scratch,
				td->urb);
		if (size < temp)
			temp = size;
		size -= temp;
		next += temp;
		if (temp == size)
			goto done;
	}

	temp = snprintf(next, size, "\n");
	if (size < temp)
		temp = size;

	size -= temp;
	next += temp;

done:
	*sizep = size;
	*nextp = next;
}

static ssize_t fill_async_buffer(struct debug_buffer *buf)
{
	struct usb_hcd *hcd;
	struct fotg210_hcd *fotg210;
	unsigned long flags;
	unsigned temp, size;
	char *next;
	struct fotg210_qh *qh;

	hcd = bus_to_hcd(buf->bus);
	fotg210 = hcd_to_fotg210(hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	*next = 0;

	/* dumps a snapshot of the async schedule.
	 * usually empty except for long-term bulk reads, or head.
	 * one QH per line, and TDs we know about
	 */
	spin_lock_irqsave(&fotg210->lock, flags);
	for (qh = fotg210->async->qh_next.qh; size > 0 && qh;
			qh = qh->qh_next.qh)
		qh_lines(fotg210, qh, &next, &size);
	if (fotg210->async_unlink && size > 0) {
		temp = scnprintf(next, size, "\nunlink =\n");
		size -= temp;
		next += temp;

		for (qh = fotg210->async_unlink; size > 0 && qh;
				qh = qh->unlink_next)
			qh_lines(fotg210, qh, &next, &size);
	}
	spin_unlock_irqrestore(&fotg210->lock, flags);

	return strlen(buf->output_buf);
}

/* count tds, get ep direction */
static unsigned output_buf_tds_dir(char *buf, struct fotg210_hcd *fotg210,
		struct fotg210_qh_hw *hw, struct fotg210_qh *qh, unsigned size)
{
	u32 scratch = hc32_to_cpup(fotg210, &hw->hw_info1);
	struct fotg210_qtd *qtd;
	char *type = "";
	unsigned temp = 0;

	/* count tds, get ep direction */
	list_for_each_entry(qtd, &qh->qtd_list, qtd_list) {
		temp++;
		switch ((hc32_to_cpu(fotg210, qtd->hw_token) >> 8) & 0x03) {
		case 0:
			type = "out";
			continue;
		case 1:
			type = "in";
			continue;
		}
	}

	return scnprintf(buf, size, "(%c%d ep%d%s [%d/%d] q%d p%d)",
			speed_char(scratch), scratch & 0x007f,
			(scratch >> 8) & 0x000f, type, qh->usecs,
			qh->c_usecs, temp, (scratch >> 16) & 0x7ff);
}

#define DBG_SCHED_LIMIT 64
static ssize_t fill_periodic_buffer(struct debug_buffer *buf)
{
	struct usb_hcd *hcd;
	struct fotg210_hcd *fotg210;
	unsigned long flags;
	union fotg210_shadow p, *seen;
	unsigned temp, size, seen_count;
	char *next;
	unsigned i;
	__hc32 tag;

	seen = kmalloc_array(DBG_SCHED_LIMIT, sizeof(*seen), GFP_ATOMIC);
	if (!seen)
		return 0;

	seen_count = 0;

	hcd = bus_to_hcd(buf->bus);
	fotg210 = hcd_to_fotg210(hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	temp = scnprintf(next, size, "size = %d\n", fotg210->periodic_size);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule.
	 * iso changes, interrupt usually doesn't.
	 */
	spin_lock_irqsave(&fotg210->lock, flags);
	for (i = 0; i < fotg210->periodic_size; i++) {
		p = fotg210->pshadow[i];
		if (likely(!p.ptr))
			continue;

		tag = Q_NEXT_TYPE(fotg210, fotg210->periodic[i]);

		temp = scnprintf(next, size, "%4d: ", i);
		size -= temp;
		next += temp;

		do {
			struct fotg210_qh_hw *hw;

			switch (hc32_to_cpu(fotg210, tag)) {
			case Q_TYPE_QH:
				hw = p.qh->hw;
				temp = scnprintf(next, size, " qh%d-%04x/%p",
						p.qh->period,
						hc32_to_cpup(fotg210,
							&hw->hw_info2)
							/* uframe masks */
							& (QH_CMASK | QH_SMASK),
						p.qh);
				size -= temp;
				next += temp;
				/* don't repeat what follows this qh */
				for (temp = 0; temp < seen_count; temp++) {
					if (seen[temp].ptr != p.ptr)
						continue;
					if (p.qh->qh_next.ptr) {
						temp = scnprintf(next, size,
								" ...");
						size -= temp;
						next += temp;
					}
					break;
				}
				/* show more info the first time around */
				if (temp == seen_count) {
					temp = output_buf_tds_dir(next,
							fotg210, hw,
							p.qh, size);

					if (seen_count < DBG_SCHED_LIMIT)
						seen[seen_count++].qh = p.qh;
				} else
					temp = 0;
				tag = Q_NEXT_TYPE(fotg210, hw->hw_next);
				p = p.qh->qh_next;
				break;
			case Q_TYPE_FSTN:
				temp = scnprintf(next, size,
						" fstn-%8x/%p",
						p.fstn->hw_prev, p.fstn);
				tag = Q_NEXT_TYPE(fotg210, p.fstn->hw_next);
				p = p.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				temp = scnprintf(next, size,
						" itd/%p", p.itd);
				tag = Q_NEXT_TYPE(fotg210, p.itd->hw_next);
				p = p.itd->itd_next;
				break;
			}
			size -= temp;
			next += temp;
		} while (p.ptr);

		temp = scnprintf(next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore(&fotg210->lock, flags);
	kfree(seen);

	return buf->alloc_size - size;
}
#undef DBG_SCHED_LIMIT

static const char *rh_state_string(struct fotg210_hcd *fotg210)
{
	switch (fotg210->rh_state) {
	case FOTG210_RH_HALTED:
		return "halted";
	case FOTG210_RH_SUSPENDED:
		return "suspended";
	case FOTG210_RH_RUNNING:
		return "running";
	case FOTG210_RH_STOPPING:
		return "stopping";
	}
	return "?";
}

static ssize_t fill_registers_buffer(struct debug_buffer *buf)
{
	struct usb_hcd *hcd;
	struct fotg210_hcd *fotg210;
	unsigned long flags;
	unsigned temp, size, i;
	char *next, scratch[80];
	static const char fmt[] = "%*s\n";
	static const char label[] = "";

	hcd = bus_to_hcd(buf->bus);
	fotg210 = hcd_to_fotg210(hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	spin_lock_irqsave(&fotg210->lock, flags);

	if (!HCD_HW_ACCESSIBLE(hcd)) {
		size = scnprintf(next, size,
				"bus %s, device %s\n"
				"%s\n"
				"SUSPENDED(no register access)\n",
				hcd->self.controller->bus->name,
				dev_name(hcd->self.controller),
				hcd->product_desc);
		goto done;
	}

	/* Capability Registers */
	i = HC_VERSION(fotg210, fotg210_readl(fotg210,
			&fotg210->caps->hc_capbase));
	temp = scnprintf(next, size,
			"bus %s, device %s\n"
			"%s\n"
			"EHCI %x.%02x, rh state %s\n",
			hcd->self.controller->bus->name,
			dev_name(hcd->self.controller),
			hcd->product_desc,
			i >> 8, i & 0x0ff, rh_state_string(fotg210));
	size -= temp;
	next += temp;

	/* FIXME interpret both types of params */
	i = fotg210_readl(fotg210, &fotg210->caps->hcs_params);
	temp = scnprintf(next, size, "structural params 0x%08x\n", i);
	size -= temp;
	next += temp;

	i = fotg210_readl(fotg210, &fotg210->caps->hcc_params);
	temp = scnprintf(next, size, "capability params 0x%08x\n", i);
	size -= temp;
	next += temp;

	/* Operational Registers */
	temp = dbg_status_buf(scratch, sizeof(scratch), label,
			fotg210_readl(fotg210, &fotg210->regs->status));
	temp = scnprintf(next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_command_buf(scratch, sizeof(scratch), label,
			fotg210_readl(fotg210, &fotg210->regs->command));
	temp = scnprintf(next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_intr_buf(scratch, sizeof(scratch), label,
			fotg210_readl(fotg210, &fotg210->regs->intr_enable));
	temp = scnprintf(next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = scnprintf(next, size, "uframe %04x\n",
			fotg210_read_frame_index(fotg210));
	size -= temp;
	next += temp;

	if (fotg210->async_unlink) {
		temp = scnprintf(next, size, "async unlink qh %p\n",
				fotg210->async_unlink);
		size -= temp;
		next += temp;
	}

#ifdef FOTG210_STATS
	temp = scnprintf(next, size,
			"irq normal %ld err %ld iaa %ld(lost %ld)\n",
			fotg210->stats.normal, fotg210->stats.error,
			fotg210->stats.iaa, fotg210->stats.lost_iaa);
	size -= temp;
	next += temp;

	temp = scnprintf(next, size, "complete %ld unlink %ld\n",
			fotg210->stats.complete, fotg210->stats.unlink);
	size -= temp;
	next += temp;
#endif

done:
	spin_unlock_irqrestore(&fotg210->lock, flags);

	return buf->alloc_size - size;
}

static struct debug_buffer
*alloc_buffer(struct usb_bus *bus, ssize_t (*fill_func)(struct debug_buffer *))
{
	struct debug_buffer *buf;

	buf = kzalloc(sizeof(struct debug_buffer), GFP_KERNEL);

	if (buf) {
		buf->bus = bus;
		buf->fill_func = fill_func;
		mutex_init(&buf->mutex);
		buf->alloc_size = PAGE_SIZE;
	}

	return buf;
}

static int fill_buffer(struct debug_buffer *buf)
{
	int ret = 0;

	if (!buf->output_buf)
		buf->output_buf = vmalloc(buf->alloc_size);

	if (!buf->output_buf) {
		ret = -ENOMEM;
		goto out;
	}

	ret = buf->fill_func(buf);

	if (ret >= 0) {
		buf->count = ret;
		ret = 0;
	}

out:
	return ret;
}

static ssize_t debug_output(struct file *file, char __user *user_buf,
		size_t len, loff_t *offset)
{
	struct debug_buffer *buf = file->private_data;
	int ret = 0;

	mutex_lock(&buf->mutex);
	if (buf->count == 0) {
		ret = fill_buffer(buf);
		if (ret != 0) {
			mutex_unlock(&buf->mutex);
			goto out;
		}
	}
	mutex_unlock(&buf->mutex);

	ret = simple_read_from_buffer(user_buf, len, offset,
			buf->output_buf, buf->count);

out:
	return ret;

}

static int debug_close(struct inode *inode, struct file *file)
{
	struct debug_buffer *buf = file->private_data;

	if (buf) {
		vfree(buf->output_buf);
		kfree(buf);
	}

	return 0;
}
static int debug_async_open(struct inode *inode, struct file *file)
{
	file->private_data = alloc_buffer(inode->i_private, fill_async_buffer);

	return file->private_data ? 0 : -ENOMEM;
}

static int debug_periodic_open(struct inode *inode, struct file *file)
{
	struct debug_buffer *buf;

	buf = alloc_buffer(inode->i_private, fill_periodic_buffer);
	if (!buf)
		return -ENOMEM;

	buf->alloc_size = (sizeof(void *) == 4 ? 6 : 8)*PAGE_SIZE;
	file->private_data = buf;
	return 0;
}

static int debug_registers_open(struct inode *inode, struct file *file)
{
	file->private_data = alloc_buffer(inode->i_private,
			fill_registers_buffer);

	return file->private_data ? 0 : -ENOMEM;
}

static inline void create_debug_files(struct fotg210_hcd *fotg210)
{
	struct usb_bus *bus = &fotg210_to_hcd(fotg210)->self;
	struct dentry *root;

	root = debugfs_create_dir(bus->bus_name, fotg210_debug_root);
	fotg210->debug_dir = root;

	debugfs_create_file("async", S_IRUGO, root, bus, &debug_async_fops);
	debugfs_create_file("periodic", S_IRUGO, root, bus,
			    &debug_periodic_fops);
	debugfs_create_file("registers", S_IRUGO, root, bus,
			    &debug_registers_fops);
}

static inline void remove_debug_files(struct fotg210_hcd *fotg210)
{
	debugfs_remove_recursive(fotg210->debug_dir);
}

/* handshake - spin reading hc until handshake completes or fails
 * @ptr: address of hc register to be read
 * @mask: bits to look at in result of read
 * @done: value of those bits when handshake succeeds
 * @usec: timeout in microseconds
 *
 * Returns negative errno, or zero on success
 *
 * Success happens when the "mask" bits have the specified value (hardware
 * handshake done).  There are two failure modes:  "usec" have passed (major
 * hardware flakeout), or the register reads as all-ones (hardware removed).
 *
 * That last failure should_only happen in cases like physical cardbus eject
 * before driver shutdown. But it also seems to be caused by bugs in cardbus
 * bridge shutdown:  shutting down the bridge before the devices using it.
 */
static int handshake(struct fotg210_hcd *fotg210, void __iomem *ptr,
		u32 mask, u32 done, int usec)
{
	u32 result;

	do {
		result = fotg210_readl(fotg210, ptr);
		if (result == ~(u32)0)		/* card removed */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay(1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

/* Force HC to halt state from unknown (EHCI spec section 2.3).
 * Must be called with interrupts enabled and the lock not held.
 */
static int fotg210_halt(struct fotg210_hcd *fotg210)
{
	u32 temp;

	spin_lock_irq(&fotg210->lock);

	/* disable any irqs left enabled by previous code */
	fotg210_writel(fotg210, 0, &fotg210->regs->intr_enable);

	/*
	 * This routine gets called during probe before fotg210->command
	 * has been initialized, so we can't rely on its value.
	 */
	fotg210->command &= ~CMD_RUN;
	temp = fotg210_readl(fotg210, &fotg210->regs->command);
	temp &= ~(CMD_RUN | CMD_IAAD);
	fotg210_writel(fotg210, temp, &fotg210->regs->command);

	spin_unlock_irq(&fotg210->lock);
	synchronize_irq(fotg210_to_hcd(fotg210)->irq);

	return handshake(fotg210, &fotg210->regs->status,
			STS_HALT, STS_HALT, 16 * 125);
}

/* Reset a non-running (STS_HALT == 1) controller.
 * Must be called with interrupts enabled and the lock not held.
 */
static int fotg210_reset(struct fotg210_hcd *fotg210)
{
	int retval;
	u32 command = fotg210_readl(fotg210, &fotg210->regs->command);

	/* If the EHCI debug controller is active, special care must be
	 * taken before and after a host controller reset
	 */
	if (fotg210->debug && !dbgp_reset_prep(fotg210_to_hcd(fotg210)))
		fotg210->debug = NULL;

	command |= CMD_RESET;
	dbg_cmd(fotg210, "reset", command);
	fotg210_writel(fotg210, command, &fotg210->regs->command);
	fotg210->rh_state = FOTG210_RH_HALTED;
	fotg210->next_statechange = jiffies;
	retval = handshake(fotg210, &fotg210->regs->command,
			CMD_RESET, 0, 250 * 1000);

	if (retval)
		return retval;

	if (fotg210->debug)
		dbgp_external_startup(fotg210_to_hcd(fotg210));

	fotg210->port_c_suspend = fotg210->suspended_ports =
			fotg210->resuming_ports = 0;
	return retval;
}

/* Idle the controller (turn off the schedules).
 * Must be called with interrupts enabled and the lock not held.
 */
static void fotg210_quiesce(struct fotg210_hcd *fotg210)
{
	u32 temp;

	if (fotg210->rh_state != FOTG210_RH_RUNNING)
		return;

	/* wait for any schedule enables/disables to take effect */
	temp = (fotg210->command << 10) & (STS_ASS | STS_PSS);
	handshake(fotg210, &fotg210->regs->status, STS_ASS | STS_PSS, temp,
			16 * 125);

	/* then disable anything that's still active */
	spin_lock_irq(&fotg210->lock);
	fotg210->command &= ~(CMD_ASE | CMD_PSE);
	fotg210_writel(fotg210, fotg210->command, &fotg210->regs->command);
	spin_unlock_irq(&fotg210->lock);

	/* hardware can take 16 microframes to turn off ... */
	handshake(fotg210, &fotg210->regs->status, STS_ASS | STS_PSS, 0,
			16 * 125);
}

static void end_unlink_async(struct fotg210_hcd *fotg210);
static void unlink_empty_async(struct fotg210_hcd *fotg210);
static void fotg210_work(struct fotg210_hcd *fotg210);
static void start_unlink_intr(struct fotg210_hcd *fotg210,
			      struct fotg210_qh *qh);
static void end_unlink_intr(struct fotg210_hcd *fotg210, struct fotg210_qh *qh);

/* Set a bit in the USBCMD register */
static void fotg210_set_command_bit(struct fotg210_hcd *fotg210, u32 bit)
{
	fotg210->command |= bit;
	fotg210_writel(fotg210, fotg210->command, &fotg210->regs->command);

	/* unblock posted write */
	fotg210_readl(fotg210, &fotg210->regs->command);
}

/* Clear a bit in the USBCMD register */
static void fotg210_clear_command_bit(struct fotg210_hcd *fotg210, u32 bit)
{
	fotg210->command &= ~bit;
	fotg210_writel(fotg210, fotg210->command, &fotg210->regs->command);

	/* unblock posted write */
	fotg210_readl(fotg210, &fotg210->regs->command);
}

/* EHCI timer support...  Now using hrtimers.
 *
 * Lots of different events are triggered from fotg210->hrtimer.  Whenever
 * the timer routine runs, it checks each possible event; events that are
 * currently enabled and whose expiration time has passed get handled.
 * The set of enabled events is stored as a collection of bitflags in
 * fotg210->enabled_hrtimer_events, and they are numbered in order of
 * increasing delay values (ranging between 1 ms and 100 ms).
 *
 * Rather than implementing a sorted list or tree of all pending events,
 * we keep track only of the lowest-numbered pending event, in
 * fotg210->next_hrtimer_event.  Whenever fotg210->hrtimer gets restarted, its
 * expiration time is set to the timeout value for this event.
 *
 * As a result, events might not get handled right away; the actual delay
 * could be anywhere up to twice the requested delay.  This doesn't
 * matter, because none of the events are especially time-critical.  The
 * ones that matter most all have a delay of 1 ms, so they will be
 * handled after 2 ms at most, which is okay.  In addition to this, we
 * allow for an expiration range of 1 ms.
 */

/* Delay lengths for the hrtimer event types.
 * Keep this list sorted by delay length, in the same order as
 * the event types indexed by enum fotg210_hrtimer_event in fotg210.h.
 */
static unsigned event_delays_ns[] = {
	1 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_POLL_ASS */
	1 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_POLL_PSS */
	1 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_POLL_DEAD */
	1125 * NSEC_PER_USEC,	/* FOTG210_HRTIMER_UNLINK_INTR */
	2 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_FREE_ITDS */
	6 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_ASYNC_UNLINKS */
	10 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_IAA_WATCHDOG */
	10 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_DISABLE_PERIODIC */
	15 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_DISABLE_ASYNC */
	100 * NSEC_PER_MSEC,	/* FOTG210_HRTIMER_IO_WATCHDOG */
};

/* Enable a pending hrtimer event */
static void fotg210_enable_event(struct fotg210_hcd *fotg210, unsigned event,
		bool resched)
{
	ktime_t *timeout = &fotg210->hr_timeouts[event];

	if (resched)
		*timeout = ktime_add(ktime_get(), event_delays_ns[event]);
	fotg210->enabled_hrtimer_events |= (1 << event);

	/* Track only the lowest-numbered pending event */
	if (event < fotg210->next_hrtimer_event) {
		fotg210->next_hrtimer_event = event;
		hrtimer_start_range_ns(&fotg210->hrtimer, *timeout,
				NSEC_PER_MSEC, HRTIMER_MODE_ABS);
	}
}


/* Poll the STS_ASS status bit; see when it agrees with CMD_ASE */
static void fotg210_poll_ASS(struct fotg210_hcd *fotg210)
{
	unsigned actual, want;

	/* Don't enable anything if the controller isn't running (e.g., died) */
	if (fotg210->rh_state != FOTG210_RH_RUNNING)
		return;

	want = (fotg210->command & CMD_ASE) ? STS_ASS : 0;
	actual = fotg210_readl(fotg210, &fotg210->regs->status) & STS_ASS;

	if (want != actual) {

		/* Poll again later, but give up after about 20 ms */
		if (fotg210->ASS_poll_count++ < 20) {
			fotg210_enable_event(fotg210, FOTG210_HRTIMER_POLL_ASS,
					true);
			return;
		}
		fotg210_dbg(fotg210, "Waited too long for the async schedule status (%x/%x), giving up\n",
				want, actual);
	}
	fotg210->ASS_poll_count = 0;

	/* The status is up-to-date; restart or stop the schedule as needed */
	if (want == 0) {	/* Stopped */
		if (fotg210->async_count > 0)
			fotg210_set_command_bit(fotg210, CMD_ASE);

	} else {		/* Running */
		if (fotg210->async_count == 0) {

			/* Turn off the schedule after a while */
			fotg210_enable_event(fotg210,
					FOTG210_HRTIMER_DISABLE_ASYNC,
					true);
		}
	}
}

/* Turn off the async schedule after a brief delay */
static void fotg210_disable_ASE(struct fotg210_hcd *fotg210)
{
	fotg210_clear_command_bit(fotg210, CMD_ASE);
}


/* Poll the STS_PSS status bit; see when it agrees with CMD_PSE */
static void fotg210_poll_PSS(struct fotg210_hcd *fotg210)
{
	unsigned actual, want;

	/* Don't do anything if the controller isn't running (e.g., died) */
	if (fotg210->rh_state != FOTG210_RH_RUNNING)
		return;

	want = (fotg210->command & CMD_PSE) ? STS_PSS : 0;
	actual = fotg210_readl(fotg210, &fotg210->regs->status) & STS_PSS;

	if (want != actual) {

		/* Poll again later, but give up after about 20 ms */
		if (fotg210->PSS_poll_count++ < 20) {
			fotg210_enable_event(fotg210, FOTG210_HRTIMER_POLL_PSS,
					true);
			return;
		}
		fotg210_dbg(fotg210, "Waited too long for the periodic schedule status (%x/%x), giving up\n",
				want, actual);
	}
	fotg210->PSS_poll_count = 0;

	/* The status is up-to-date; restart or stop the schedule as needed */
	if (want == 0) {	/* Stopped */
		if (fotg210->periodic_count > 0)
			fotg210_set_command_bit(fotg210, CMD_PSE);

	} else {		/* Running */
		if (fotg210->periodic_count == 0) {

			/* Turn off the schedule after a while */
			fotg210_enable_event(fotg210,
					FOTG210_HRTIMER_DISABLE_PERIODIC,
					true);
		}
	}
}

/* Turn off the periodic schedule after a brief delay */
static void fotg210_disable_PSE(struct fotg210_hcd *fotg210)
{
	fotg210_clear_command_bit(fotg210, CMD_PSE);
}


/* Poll the STS_HALT status bit; see when a dead controller stops */
static void fotg210_handle_controller_death(struct fotg210_hcd *fotg210)
{
	if (!(fotg210_readl(fotg210, &fotg210->regs->status) & STS_HALT)) {

		/* Give up after a few milliseconds */
		if (fotg210->died_poll_count++ < 5) {
			/* Try again later */
			fotg210_enable_event(fotg210,
					FOTG210_HRTIMER_POLL_DEAD, true);
			return;
		}
		fotg210_warn(fotg210, "Waited too long for the controller to stop, giving up\n");
	}

	/* Clean up the mess */
	fotg210->rh_state = FOTG210_RH_HALTED;
	fotg210_writel(fotg210, 0, &fotg210->regs->intr_enable);
	fotg210_work(fotg210);
	end_unlink_async(fotg210);

	/* Not in process context, so don't try to reset the controller */
}


/* Handle unlinked interrupt QHs once they are gone from the hardware */
static void fotg210_handle_intr_unlinks(struct fotg210_hcd *fotg210)
{
	bool stopped = (fotg210->rh_state < FOTG210_RH_RUNNING);

	/*
	 * Process all the QHs on the intr_unlink list that were added
	 * before the current unlink cycle began.  The list is in
	 * temporal order, so stop when we reach the first entry in the
	 * current cycle.  But if the root hub isn't running then
	 * process all the QHs on the list.
	 */
	fotg210->intr_unlinking = true;
	while (fotg210->intr_unlink) {
		struct fotg210_qh *qh = fotg210->intr_unlink;

		if (!stopped && qh->unlink_cycle == fotg210->intr_unlink_cycle)
			break;
		fotg210->intr_unlink = qh->unlink_next;
		qh->unlink_next = NULL;
		end_unlink_intr(fotg210, qh);
	}

	/* Handle remaining entries later */
	if (fotg210->intr_unlink) {
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_UNLINK_INTR,
				true);
		++fotg210->intr_unlink_cycle;
	}
	fotg210->intr_unlinking = false;
}


/* Start another free-iTDs/siTDs cycle */
static void start_free_itds(struct fotg210_hcd *fotg210)
{
	if (!(fotg210->enabled_hrtimer_events &
			BIT(FOTG210_HRTIMER_FREE_ITDS))) {
		fotg210->last_itd_to_free = list_entry(
				fotg210->cached_itd_list.prev,
				struct fotg210_itd, itd_list);
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_FREE_ITDS, true);
	}
}

/* Wait for controller to stop using old iTDs and siTDs */
static void end_free_itds(struct fotg210_hcd *fotg210)
{
	struct fotg210_itd *itd, *n;

	if (fotg210->rh_state < FOTG210_RH_RUNNING)
		fotg210->last_itd_to_free = NULL;

	list_for_each_entry_safe(itd, n, &fotg210->cached_itd_list, itd_list) {
		list_del(&itd->itd_list);
		dma_pool_free(fotg210->itd_pool, itd, itd->itd_dma);
		if (itd == fotg210->last_itd_to_free)
			break;
	}

	if (!list_empty(&fotg210->cached_itd_list))
		start_free_itds(fotg210);
}


/* Handle lost (or very late) IAA interrupts */
static void fotg210_iaa_watchdog(struct fotg210_hcd *fotg210)
{
	if (fotg210->rh_state != FOTG210_RH_RUNNING)
		return;

	/*
	 * Lost IAA irqs wedge things badly; seen first with a vt8235.
	 * So we need this watchdog, but must protect it against both
	 * (a) SMP races against real IAA firing and retriggering, and
	 * (b) clean HC shutdown, when IAA watchdog was pending.
	 */
	if (fotg210->async_iaa) {
		u32 cmd, status;

		/* If we get here, IAA is *REALLY* late.  It's barely
		 * conceivable that the system is so busy that CMD_IAAD
		 * is still legitimately set, so let's be sure it's
		 * clear before we read STS_IAA.  (The HC should clear
		 * CMD_IAAD when it sets STS_IAA.)
		 */
		cmd = fotg210_readl(fotg210, &fotg210->regs->command);

		/*
		 * If IAA is set here it either legitimately triggered
		 * after the watchdog timer expired (_way_ late, so we'll
		 * still count it as lost) ... or a silicon erratum:
		 * - VIA seems to set IAA without triggering the IRQ;
		 * - IAAD potentially cleared without setting IAA.
		 */
		status = fotg210_readl(fotg210, &fotg210->regs->status);
		if ((status & STS_IAA) || !(cmd & CMD_IAAD)) {
			INCR(fotg210->stats.lost_iaa);
			fotg210_writel(fotg210, STS_IAA,
					&fotg210->regs->status);
		}

		fotg210_dbg(fotg210, "IAA watchdog: status %x cmd %x\n",
				status, cmd);
		end_unlink_async(fotg210);
	}
}


/* Enable the I/O watchdog, if appropriate */
static void turn_on_io_watchdog(struct fotg210_hcd *fotg210)
{
	/* Not needed if the controller isn't running or it's already enabled */
	if (fotg210->rh_state != FOTG210_RH_RUNNING ||
			(fotg210->enabled_hrtimer_events &
			BIT(FOTG210_HRTIMER_IO_WATCHDOG)))
		return;

	/*
	 * Isochronous transfers always need the watchdog.
	 * For other sorts we use it only if the flag is set.
	 */
	if (fotg210->isoc_count > 0 || (fotg210->need_io_watchdog &&
			fotg210->async_count + fotg210->intr_count > 0))
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_IO_WATCHDOG,
				true);
}


/* Handler functions for the hrtimer event types.
 * Keep this array in the same order as the event types indexed by
 * enum fotg210_hrtimer_event in fotg210.h.
 */
static void (*event_handlers[])(struct fotg210_hcd *) = {
	fotg210_poll_ASS,			/* FOTG210_HRTIMER_POLL_ASS */
	fotg210_poll_PSS,			/* FOTG210_HRTIMER_POLL_PSS */
	fotg210_handle_controller_death,	/* FOTG210_HRTIMER_POLL_DEAD */
	fotg210_handle_intr_unlinks,	/* FOTG210_HRTIMER_UNLINK_INTR */
	end_free_itds,			/* FOTG210_HRTIMER_FREE_ITDS */
	unlink_empty_async,		/* FOTG210_HRTIMER_ASYNC_UNLINKS */
	fotg210_iaa_watchdog,		/* FOTG210_HRTIMER_IAA_WATCHDOG */
	fotg210_disable_PSE,		/* FOTG210_HRTIMER_DISABLE_PERIODIC */
	fotg210_disable_ASE,		/* FOTG210_HRTIMER_DISABLE_ASYNC */
	fotg210_work,			/* FOTG210_HRTIMER_IO_WATCHDOG */
};

static enum hrtimer_restart fotg210_hrtimer_func(struct hrtimer *t)
{
	struct fotg210_hcd *fotg210 =
			container_of(t, struct fotg210_hcd, hrtimer);
	ktime_t now;
	unsigned long events;
	unsigned long flags;
	unsigned e;

	spin_lock_irqsave(&fotg210->lock, flags);

	events = fotg210->enabled_hrtimer_events;
	fotg210->enabled_hrtimer_events = 0;
	fotg210->next_hrtimer_event = FOTG210_HRTIMER_NO_EVENT;

	/*
	 * Check each pending event.  If its time has expired, handle
	 * the event; otherwise re-enable it.
	 */
	now = ktime_get();
	for_each_set_bit(e, &events, FOTG210_HRTIMER_NUM_EVENTS) {
		if (ktime_compare(now, fotg210->hr_timeouts[e]) >= 0)
			event_handlers[e](fotg210);
		else
			fotg210_enable_event(fotg210, e, false);
	}

	spin_unlock_irqrestore(&fotg210->lock, flags);
	return HRTIMER_NORESTART;
}

#define fotg210_bus_suspend NULL
#define fotg210_bus_resume NULL

static int check_reset_complete(struct fotg210_hcd *fotg210, int index,
		u32 __iomem *status_reg, int port_status)
{
	if (!(port_status & PORT_CONNECT))
		return port_status;

	/* if reset finished and it's still not enabled -- handoff */
	if (!(port_status & PORT_PE))
		/* with integrated TT, there's nobody to hand it to! */
		fotg210_dbg(fotg210, "Failed to enable port %d on root hub TT\n",
				index + 1);
	else
		fotg210_dbg(fotg210, "port %d reset complete, port enabled\n",
				index + 1);

	return port_status;
}


/* build "status change" packet (one or two bytes) from HC registers */

static int fotg210_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	u32 temp, status;
	u32 mask;
	int retval = 1;
	unsigned long flags;

	/* init status to no-changes */
	buf[0] = 0;

	/* Inform the core about resumes-in-progress by returning
	 * a non-zero value even if there are no status changes.
	 */
	status = fotg210->resuming_ports;

	mask = PORT_CSC | PORT_PEC;
	/* PORT_RESUME from hardware ~= PORT_STAT_C_SUSPEND */

	/* no hub change reports (bit 0) for now (power, ...) */

	/* port N changes (bit N)? */
	spin_lock_irqsave(&fotg210->lock, flags);

	temp = fotg210_readl(fotg210, &fotg210->regs->port_status);

	/*
	 * Return status information even for ports with OWNER set.
	 * Otherwise hub_wq wouldn't see the disconnect event when a
	 * high-speed device is switched over to the companion
	 * controller by the user.
	 */

	if ((temp & mask) != 0 || test_bit(0, &fotg210->port_c_suspend) ||
			(fotg210->reset_done[0] &&
			time_after_eq(jiffies, fotg210->reset_done[0]))) {
		buf[0] |= 1 << 1;
		status = STS_PCD;
	}
	/* FIXME autosuspend idle root hubs */
	spin_unlock_irqrestore(&fotg210->lock, flags);
	return status ? retval : 0;
}

static void fotg210_hub_descriptor(struct fotg210_hcd *fotg210,
		struct usb_hub_descriptor *desc)
{
	int ports = HCS_N_PORTS(fotg210->hcs_params);
	u16 temp;

	desc->bDescriptorType = USB_DT_HUB;
	desc->bPwrOn2PwrGood = 10;	/* fotg210 1.0, 2.3.9 says 20ms max */
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	temp = HUB_CHAR_INDV_PORT_OCPM;	/* per-port overcurrent reporting */
	temp |= HUB_CHAR_NO_LPSM;	/* no power switching */
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

static int fotg210_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		u16 wIndex, char *buf, u16 wLength)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	int ports = HCS_N_PORTS(fotg210->hcs_params);
	u32 __iomem *status_reg = &fotg210->regs->port_status;
	u32 temp, temp1, status;
	unsigned long flags;
	int retval = 0;
	unsigned selector;

	/*
	 * FIXME:  support SetPortFeatures USB_PORT_FEAT_INDICATOR.
	 * HCS_INDICATOR may say we can change LEDs to off/amber/green.
	 * (track current state ourselves) ... blink for diagnostics,
	 * power, "this is the one", etc.  EHCI spec supports this.
	 */

	spin_lock_irqsave(&fotg210->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* no hub-wide feature/status flags */
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = fotg210_readl(fotg210, status_reg);
		temp &= ~PORT_RWC_BITS;

		/*
		 * Even if OWNER is set, so the port is owned by the
		 * companion controller, hub_wq needs to be able to clear
		 * the port-change status bits (especially
		 * USB_PORT_STAT_C_CONNECTION).
		 */

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			fotg210_writel(fotg210, temp & ~PORT_PE, status_reg);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			fotg210_writel(fotg210, temp | PORT_PEC, status_reg);
			break;
		case USB_PORT_FEAT_SUSPEND:
			if (temp & PORT_RESET)
				goto error;
			if (!(temp & PORT_SUSPEND))
				break;
			if ((temp & PORT_PE) == 0)
				goto error;

			/* resume signaling for 20 msec */
			fotg210_writel(fotg210, temp | PORT_RESUME, status_reg);
			fotg210->reset_done[wIndex] = jiffies
					+ msecs_to_jiffies(USB_RESUME_TIMEOUT);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			clear_bit(wIndex, &fotg210->port_c_suspend);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			fotg210_writel(fotg210, temp | PORT_CSC, status_reg);
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			fotg210_writel(fotg210, temp | OTGISR_OVC,
					&fotg210->regs->otgisr);
			break;
		case USB_PORT_FEAT_C_RESET:
			/* GetPortStatus clears reset */
			break;
		default:
			goto error;
		}
		fotg210_readl(fotg210, &fotg210->regs->command);
		break;
	case GetHubDescriptor:
		fotg210_hub_descriptor(fotg210, (struct usb_hub_descriptor *)
				buf);
		break;
	case GetHubStatus:
		/* no hub-wide feature/status flags */
		memset(buf, 0, 4);
		/*cpu_to_le32s ((u32 *) buf); */
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		status = 0;
		temp = fotg210_readl(fotg210, status_reg);

		/* wPortChange bits */
		if (temp & PORT_CSC)
			status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (temp & PORT_PEC)
			status |= USB_PORT_STAT_C_ENABLE << 16;

		temp1 = fotg210_readl(fotg210, &fotg210->regs->otgisr);
		if (temp1 & OTGISR_OVC)
			status |= USB_PORT_STAT_C_OVERCURRENT << 16;

		/* whoever resumes must GetPortStatus to complete it!! */
		if (temp & PORT_RESUME) {

			/* Remote Wakeup received? */
			if (!fotg210->reset_done[wIndex]) {
				/* resume signaling for 20 msec */
				fotg210->reset_done[wIndex] = jiffies
						+ msecs_to_jiffies(20);
				/* check the port again */
				mod_timer(&fotg210_to_hcd(fotg210)->rh_timer,
						fotg210->reset_done[wIndex]);
			}

			/* resume completed? */
			else if (time_after_eq(jiffies,
					fotg210->reset_done[wIndex])) {
				clear_bit(wIndex, &fotg210->suspended_ports);
				set_bit(wIndex, &fotg210->port_c_suspend);
				fotg210->reset_done[wIndex] = 0;

				/* stop resume signaling */
				temp = fotg210_readl(fotg210, status_reg);
				fotg210_writel(fotg210, temp &
						~(PORT_RWC_BITS | PORT_RESUME),
						status_reg);
				clear_bit(wIndex, &fotg210->resuming_ports);
				retval = handshake(fotg210, status_reg,
						PORT_RESUME, 0, 2000);/* 2ms */
				if (retval != 0) {
					fotg210_err(fotg210,
							"port %d resume error %d\n",
							wIndex + 1, retval);
					goto error;
				}
				temp &= ~(PORT_SUSPEND|PORT_RESUME|(3<<10));
			}
		}

		/* whoever resets must GetPortStatus to complete it!! */
		if ((temp & PORT_RESET) && time_after_eq(jiffies,
				fotg210->reset_done[wIndex])) {
			status |= USB_PORT_STAT_C_RESET << 16;
			fotg210->reset_done[wIndex] = 0;
			clear_bit(wIndex, &fotg210->resuming_ports);

			/* force reset to complete */
			fotg210_writel(fotg210,
					temp & ~(PORT_RWC_BITS | PORT_RESET),
					status_reg);
			/* REVISIT:  some hardware needs 550+ usec to clear
			 * this bit; seems too long to spin routinely...
			 */
			retval = handshake(fotg210, status_reg,
					PORT_RESET, 0, 1000);
			if (retval != 0) {
				fotg210_err(fotg210, "port %d reset error %d\n",
						wIndex + 1, retval);
				goto error;
			}

			/* see what we found out */
			temp = check_reset_complete(fotg210, wIndex, status_reg,
					fotg210_readl(fotg210, status_reg));
		}

		if (!(temp & (PORT_RESUME|PORT_RESET))) {
			fotg210->reset_done[wIndex] = 0;
			clear_bit(wIndex, &fotg210->resuming_ports);
		}

		/* transfer dedicated ports to the companion hc */
		if ((temp & PORT_CONNECT) &&
				test_bit(wIndex, &fotg210->companion_ports)) {
			temp &= ~PORT_RWC_BITS;
			fotg210_writel(fotg210, temp, status_reg);
			fotg210_dbg(fotg210, "port %d --> companion\n",
					wIndex + 1);
			temp = fotg210_readl(fotg210, status_reg);
		}

		/*
		 * Even if OWNER is set, there's no harm letting hub_wq
		 * see the wPortStatus values (they should all be 0 except
		 * for PORT_POWER anyway).
		 */

		if (temp & PORT_CONNECT) {
			status |= USB_PORT_STAT_CONNECTION;
			status |= fotg210_port_speed(fotg210, temp);
		}
		if (temp & PORT_PE)
			status |= USB_PORT_STAT_ENABLE;

		/* maybe the port was unsuspended without our knowledge */
		if (temp & (PORT_SUSPEND|PORT_RESUME)) {
			status |= USB_PORT_STAT_SUSPEND;
		} else if (test_bit(wIndex, &fotg210->suspended_ports)) {
			clear_bit(wIndex, &fotg210->suspended_ports);
			clear_bit(wIndex, &fotg210->resuming_ports);
			fotg210->reset_done[wIndex] = 0;
			if (temp & PORT_PE)
				set_bit(wIndex, &fotg210->port_c_suspend);
		}

		temp1 = fotg210_readl(fotg210, &fotg210->regs->otgisr);
		if (temp1 & OTGISR_OVC)
			status |= USB_PORT_STAT_OVERCURRENT;
		if (temp & PORT_RESET)
			status |= USB_PORT_STAT_RESET;
		if (test_bit(wIndex, &fotg210->port_c_suspend))
			status |= USB_PORT_STAT_C_SUSPEND << 16;

		if (status & ~0xffff)	/* only if wPortChange is interesting */
			dbg_port(fotg210, "GetStatus", wIndex + 1, temp);
		put_unaligned_le32(status, buf);
		break;
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* no hub-wide feature/status flags */
			break;
		default:
			goto error;
		}
		break;
	case SetPortFeature:
		selector = wIndex >> 8;
		wIndex &= 0xff;

		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = fotg210_readl(fotg210, status_reg);
		temp &= ~PORT_RWC_BITS;
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if ((temp & PORT_PE) == 0
					|| (temp & PORT_RESET) != 0)
				goto error;

			/* After above check the port must be connected.
			 * Set appropriate bit thus could put phy into low power
			 * mode if we have hostpc feature
			 */
			fotg210_writel(fotg210, temp | PORT_SUSPEND,
					status_reg);
			set_bit(wIndex, &fotg210->suspended_ports);
			break;
		case USB_PORT_FEAT_RESET:
			if (temp & PORT_RESUME)
				goto error;
			/* line status bits may report this as low speed,
			 * which can be fine if this root hub has a
			 * transaction translator built in.
			 */
			fotg210_dbg(fotg210, "port %d reset\n", wIndex + 1);
			temp |= PORT_RESET;
			temp &= ~PORT_PE;

			/*
			 * caller must wait, then call GetPortStatus
			 * usb 2.0 spec says 50 ms resets on root
			 */
			fotg210->reset_done[wIndex] = jiffies
					+ msecs_to_jiffies(50);
			fotg210_writel(fotg210, temp, status_reg);
			break;

		/* For downstream facing ports (these):  one hub port is put
		 * into test mode according to USB2 11.24.2.13, then the hub
		 * must be reset (which for root hub now means rmmod+modprobe,
		 * or else system reboot).  See EHCI 2.3.9 and 4.14 for info
		 * about the EHCI-specific stuff.
		 */
		case USB_PORT_FEAT_TEST:
			if (!selector || selector > 5)
				goto error;
			spin_unlock_irqrestore(&fotg210->lock, flags);
			fotg210_quiesce(fotg210);
			spin_lock_irqsave(&fotg210->lock, flags);

			/* Put all enabled ports into suspend */
			temp = fotg210_readl(fotg210, status_reg) &
				~PORT_RWC_BITS;
			if (temp & PORT_PE)
				fotg210_writel(fotg210, temp | PORT_SUSPEND,
						status_reg);

			spin_unlock_irqrestore(&fotg210->lock, flags);
			fotg210_halt(fotg210);
			spin_lock_irqsave(&fotg210->lock, flags);

			temp = fotg210_readl(fotg210, status_reg);
			temp |= selector << 16;
			fotg210_writel(fotg210, temp, status_reg);
			break;

		default:
			goto error;
		}
		fotg210_readl(fotg210, &fotg210->regs->command);
		break;

	default:
error:
		/* "stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&fotg210->lock, flags);
	return retval;
}

static void __maybe_unused fotg210_relinquish_port(struct usb_hcd *hcd,
		int portnum)
{
	return;
}

static int __maybe_unused fotg210_port_handed_over(struct usb_hcd *hcd,
		int portnum)
{
	return 0;
}

/* There's basically three types of memory:
 *	- data used only by the HCD ... kmalloc is fine
 *	- async and periodic schedules, shared by HC and HCD ... these
 *	  need to use dma_pool or dma_alloc_coherent
 *	- driver buffers, read/written by HC ... single shot DMA mapped
 *
 * There's also "register" data (e.g. PCI or SOC), which is memory mapped.
 * No memory seen by this driver is pageable.
 */

/* Allocate the key transfer structures from the previously allocated pool */
static inline void fotg210_qtd_init(struct fotg210_hcd *fotg210,
		struct fotg210_qtd *qtd, dma_addr_t dma)
{
	memset(qtd, 0, sizeof(*qtd));
	qtd->qtd_dma = dma;
	qtd->hw_token = cpu_to_hc32(fotg210, QTD_STS_HALT);
	qtd->hw_next = FOTG210_LIST_END(fotg210);
	qtd->hw_alt_next = FOTG210_LIST_END(fotg210);
	INIT_LIST_HEAD(&qtd->qtd_list);
}

static struct fotg210_qtd *fotg210_qtd_alloc(struct fotg210_hcd *fotg210,
		gfp_t flags)
{
	struct fotg210_qtd *qtd;
	dma_addr_t dma;

	qtd = dma_pool_alloc(fotg210->qtd_pool, flags, &dma);
	if (qtd != NULL)
		fotg210_qtd_init(fotg210, qtd, dma);

	return qtd;
}

static inline void fotg210_qtd_free(struct fotg210_hcd *fotg210,
		struct fotg210_qtd *qtd)
{
	dma_pool_free(fotg210->qtd_pool, qtd, qtd->qtd_dma);
}


static void qh_destroy(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	/* clean qtds first, and know this is not linked */
	if (!list_empty(&qh->qtd_list) || qh->qh_next.ptr) {
		fotg210_dbg(fotg210, "unused qh not empty!\n");
		BUG();
	}
	if (qh->dummy)
		fotg210_qtd_free(fotg210, qh->dummy);
	dma_pool_free(fotg210->qh_pool, qh->hw, qh->qh_dma);
	kfree(qh);
}

static struct fotg210_qh *fotg210_qh_alloc(struct fotg210_hcd *fotg210,
		gfp_t flags)
{
	struct fotg210_qh *qh;
	dma_addr_t dma;

	qh = kzalloc(sizeof(*qh), GFP_ATOMIC);
	if (!qh)
		goto done;
	qh->hw = dma_pool_zalloc(fotg210->qh_pool, flags, &dma);
	if (!qh->hw)
		goto fail;
	qh->qh_dma = dma;
	INIT_LIST_HEAD(&qh->qtd_list);

	/* dummy td enables safe urb queuing */
	qh->dummy = fotg210_qtd_alloc(fotg210, flags);
	if (qh->dummy == NULL) {
		fotg210_dbg(fotg210, "no dummy td\n");
		goto fail1;
	}
done:
	return qh;
fail1:
	dma_pool_free(fotg210->qh_pool, qh->hw, qh->qh_dma);
fail:
	kfree(qh);
	return NULL;
}

/* The queue heads and transfer descriptors are managed from pools tied
 * to each of the "per device" structures.
 * This is the initialisation and cleanup code.
 */

static void fotg210_mem_cleanup(struct fotg210_hcd *fotg210)
{
	if (fotg210->async)
		qh_destroy(fotg210, fotg210->async);
	fotg210->async = NULL;

	if (fotg210->dummy)
		qh_destroy(fotg210, fotg210->dummy);
	fotg210->dummy = NULL;

	/* DMA consistent memory and pools */
	dma_pool_destroy(fotg210->qtd_pool);
	fotg210->qtd_pool = NULL;

	dma_pool_destroy(fotg210->qh_pool);
	fotg210->qh_pool = NULL;

	dma_pool_destroy(fotg210->itd_pool);
	fotg210->itd_pool = NULL;

	if (fotg210->periodic)
		dma_free_coherent(fotg210_to_hcd(fotg210)->self.controller,
				fotg210->periodic_size * sizeof(u32),
				fotg210->periodic, fotg210->periodic_dma);
	fotg210->periodic = NULL;

	/* shadow periodic table */
	kfree(fotg210->pshadow);
	fotg210->pshadow = NULL;
}

/* remember to add cleanup code (above) if you add anything here */
static int fotg210_mem_init(struct fotg210_hcd *fotg210, gfp_t flags)
{
	int i;

	/* QTDs for control/bulk/intr transfers */
	fotg210->qtd_pool = dma_pool_create("fotg210_qtd",
			fotg210_to_hcd(fotg210)->self.controller,
			sizeof(struct fotg210_qtd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fotg210->qtd_pool)
		goto fail;

	/* QHs for control/bulk/intr transfers */
	fotg210->qh_pool = dma_pool_create("fotg210_qh",
			fotg210_to_hcd(fotg210)->self.controller,
			sizeof(struct fotg210_qh_hw),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fotg210->qh_pool)
		goto fail;

	fotg210->async = fotg210_qh_alloc(fotg210, flags);
	if (!fotg210->async)
		goto fail;

	/* ITD for high speed ISO transfers */
	fotg210->itd_pool = dma_pool_create("fotg210_itd",
			fotg210_to_hcd(fotg210)->self.controller,
			sizeof(struct fotg210_itd),
			64 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fotg210->itd_pool)
		goto fail;

	/* Hardware periodic table */
	fotg210->periodic = (__le32 *)
		dma_alloc_coherent(fotg210_to_hcd(fotg210)->self.controller,
				fotg210->periodic_size * sizeof(__le32),
				&fotg210->periodic_dma, 0);
	if (fotg210->periodic == NULL)
		goto fail;

	for (i = 0; i < fotg210->periodic_size; i++)
		fotg210->periodic[i] = FOTG210_LIST_END(fotg210);

	/* software shadow of hardware table */
	fotg210->pshadow = kcalloc(fotg210->periodic_size, sizeof(void *),
			flags);
	if (fotg210->pshadow != NULL)
		return 0;

fail:
	fotg210_dbg(fotg210, "couldn't init memory\n");
	fotg210_mem_cleanup(fotg210);
	return -ENOMEM;
}
/* EHCI hardware queue manipulation ... the core.  QH/QTD manipulation.
 *
 * Control, bulk, and interrupt traffic all use "qh" lists.  They list "qtd"
 * entries describing USB transactions, max 16-20kB/entry (with 4kB-aligned
 * buffers needed for the larger number).  We use one QH per endpoint, queue
 * multiple urbs (all three types) per endpoint.  URBs may need several qtds.
 *
 * ISO traffic uses "ISO TD" (itd) records, and (along with
 * interrupts) needs careful scheduling.  Performance improvements can be
 * an ongoing challenge.  That's in "ehci-sched.c".
 *
 * USB 1.1 devices are handled (a) by "companion" OHCI or UHCI root hubs,
 * or otherwise through transaction translators (TTs) in USB 2.0 hubs using
 * (b) special fields in qh entries or (c) split iso entries.  TTs will
 * buffer low/full speed data so the host collects it at high speed.
 */

/* fill a qtd, returning how much of the buffer we were able to queue up */
static int qtd_fill(struct fotg210_hcd *fotg210, struct fotg210_qtd *qtd,
		dma_addr_t buf, size_t len, int token, int maxpacket)
{
	int i, count;
	u64 addr = buf;

	/* one buffer entry per 4K ... first might be short or unaligned */
	qtd->hw_buf[0] = cpu_to_hc32(fotg210, (u32)addr);
	qtd->hw_buf_hi[0] = cpu_to_hc32(fotg210, (u32)(addr >> 32));
	count = 0x1000 - (buf & 0x0fff);	/* rest of that page */
	if (likely(len < count))		/* ... iff needed */
		count = len;
	else {
		buf +=  0x1000;
		buf &= ~0x0fff;

		/* per-qtd limit: from 16K to 20K (best alignment) */
		for (i = 1; count < len && i < 5; i++) {
			addr = buf;
			qtd->hw_buf[i] = cpu_to_hc32(fotg210, (u32)addr);
			qtd->hw_buf_hi[i] = cpu_to_hc32(fotg210,
					(u32)(addr >> 32));
			buf += 0x1000;
			if ((count + 0x1000) < len)
				count += 0x1000;
			else
				count = len;
		}

		/* short packets may only terminate transfers */
		if (count != len)
			count -= (count % maxpacket);
	}
	qtd->hw_token = cpu_to_hc32(fotg210, (count << 16) | token);
	qtd->length = count;

	return count;
}

static inline void qh_update(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh, struct fotg210_qtd *qtd)
{
	struct fotg210_qh_hw *hw = qh->hw;

	/* writes to an active overlay are unsafe */
	BUG_ON(qh->qh_state != QH_STATE_IDLE);

	hw->hw_qtd_next = QTD_NEXT(fotg210, qtd->qtd_dma);
	hw->hw_alt_next = FOTG210_LIST_END(fotg210);

	/* Except for control endpoints, we make hardware maintain data
	 * toggle (like OHCI) ... here (re)initialize the toggle in the QH,
	 * and set the pseudo-toggle in udev. Only usb_clear_halt() will
	 * ever clear it.
	 */
	if (!(hw->hw_info1 & cpu_to_hc32(fotg210, QH_TOGGLE_CTL))) {
		unsigned is_out, epnum;

		is_out = qh->is_out;
		epnum = (hc32_to_cpup(fotg210, &hw->hw_info1) >> 8) & 0x0f;
		if (unlikely(!usb_gettoggle(qh->dev, epnum, is_out))) {
			hw->hw_token &= ~cpu_to_hc32(fotg210, QTD_TOGGLE);
			usb_settoggle(qh->dev, epnum, is_out, 1);
		}
	}

	hw->hw_token &= cpu_to_hc32(fotg210, QTD_TOGGLE | QTD_STS_PING);
}

/* if it weren't for a common silicon quirk (writing the dummy into the qh
 * overlay, so qh->hw_token wrongly becomes inactive/halted), only fault
 * recovery (including urb dequeue) would need software changes to a QH...
 */
static void qh_refresh(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	struct fotg210_qtd *qtd;

	if (list_empty(&qh->qtd_list))
		qtd = qh->dummy;
	else {
		qtd = list_entry(qh->qtd_list.next,
				struct fotg210_qtd, qtd_list);
		/*
		 * first qtd may already be partially processed.
		 * If we come here during unlink, the QH overlay region
		 * might have reference to the just unlinked qtd. The
		 * qtd is updated in qh_completions(). Update the QH
		 * overlay here.
		 */
		if (cpu_to_hc32(fotg210, qtd->qtd_dma) == qh->hw->hw_current) {
			qh->hw->hw_qtd_next = qtd->hw_next;
			qtd = NULL;
		}
	}

	if (qtd)
		qh_update(fotg210, qh, qtd);
}

static void qh_link_async(struct fotg210_hcd *fotg210, struct fotg210_qh *qh);

static void fotg210_clear_tt_buffer_complete(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	struct fotg210_qh *qh = ep->hcpriv;
	unsigned long flags;

	spin_lock_irqsave(&fotg210->lock, flags);
	qh->clearing_tt = 0;
	if (qh->qh_state == QH_STATE_IDLE && !list_empty(&qh->qtd_list)
			&& fotg210->rh_state == FOTG210_RH_RUNNING)
		qh_link_async(fotg210, qh);
	spin_unlock_irqrestore(&fotg210->lock, flags);
}

static void fotg210_clear_tt_buffer(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh, struct urb *urb, u32 token)
{

	/* If an async split transaction gets an error or is unlinked,
	 * the TT buffer may be left in an indeterminate state.  We
	 * have to clear the TT buffer.
	 *
	 * Note: this routine is never called for Isochronous transfers.
	 */
	if (urb->dev->tt && !usb_pipeint(urb->pipe) && !qh->clearing_tt) {
		struct usb_device *tt = urb->dev->tt->hub;

		dev_dbg(&tt->dev,
				"clear tt buffer port %d, a%d ep%d t%08x\n",
				urb->dev->ttport, urb->dev->devnum,
				usb_pipeendpoint(urb->pipe), token);

		if (urb->dev->tt->hub !=
				fotg210_to_hcd(fotg210)->self.root_hub) {
			if (usb_hub_clear_tt_buffer(urb) == 0)
				qh->clearing_tt = 1;
		}
	}
}

static int qtd_copy_status(struct fotg210_hcd *fotg210, struct urb *urb,
		size_t length, u32 token)
{
	int status = -EINPROGRESS;

	/* count IN/OUT bytes, not SETUP (even short packets) */
	if (likely(QTD_PID(token) != 2))
		urb->actual_length += length - QTD_LENGTH(token);

	/* don't modify error codes */
	if (unlikely(urb->unlinked))
		return status;

	/* force cleanup after short read; not always an error */
	if (unlikely(IS_SHORT_READ(token)))
		status = -EREMOTEIO;

	/* serious "can't proceed" faults reported by the hardware */
	if (token & QTD_STS_HALT) {
		if (token & QTD_STS_BABBLE) {
			/* FIXME "must" disable babbling device's port too */
			status = -EOVERFLOW;
		/* CERR nonzero + halt --> stall */
		} else if (QTD_CERR(token)) {
			status = -EPIPE;

		/* In theory, more than one of the following bits can be set
		 * since they are sticky and the transaction is retried.
		 * Which to test first is rather arbitrary.
		 */
		} else if (token & QTD_STS_MMF) {
			/* fs/ls interrupt xfer missed the complete-split */
			status = -EPROTO;
		} else if (token & QTD_STS_DBE) {
			status = (QTD_PID(token) == 1) /* IN ? */
				? -ENOSR  /* hc couldn't read data */
				: -ECOMM; /* hc couldn't write data */
		} else if (token & QTD_STS_XACT) {
			/* timeout, bad CRC, wrong PID, etc */
			fotg210_dbg(fotg210, "devpath %s ep%d%s 3strikes\n",
					urb->dev->devpath,
					usb_pipeendpoint(urb->pipe),
					usb_pipein(urb->pipe) ? "in" : "out");
			status = -EPROTO;
		} else {	/* unknown */
			status = -EPROTO;
		}

		fotg210_dbg(fotg210,
				"dev%d ep%d%s qtd token %08x --> status %d\n",
				usb_pipedevice(urb->pipe),
				usb_pipeendpoint(urb->pipe),
				usb_pipein(urb->pipe) ? "in" : "out",
				token, status);
	}

	return status;
}

static void fotg210_urb_done(struct fotg210_hcd *fotg210, struct urb *urb,
		int status)
__releases(fotg210->lock)
__acquires(fotg210->lock)
{
	if (likely(urb->hcpriv != NULL)) {
		struct fotg210_qh *qh = (struct fotg210_qh *) urb->hcpriv;

		/* S-mask in a QH means it's an interrupt urb */
		if ((qh->hw->hw_info2 & cpu_to_hc32(fotg210, QH_SMASK)) != 0) {

			/* ... update hc-wide periodic stats (for usbfs) */
			fotg210_to_hcd(fotg210)->self.bandwidth_int_reqs--;
		}
	}

	if (unlikely(urb->unlinked)) {
		INCR(fotg210->stats.unlink);
	} else {
		/* report non-error and short read status as zero */
		if (status == -EINPROGRESS || status == -EREMOTEIO)
			status = 0;
		INCR(fotg210->stats.complete);
	}

#ifdef FOTG210_URB_TRACE
	fotg210_dbg(fotg210,
			"%s %s urb %p ep%d%s status %d len %d/%d\n",
			__func__, urb->dev->devpath, urb,
			usb_pipeendpoint(urb->pipe),
			usb_pipein(urb->pipe) ? "in" : "out",
			status,
			urb->actual_length, urb->transfer_buffer_length);
#endif

	/* complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(fotg210_to_hcd(fotg210), urb);
	spin_unlock(&fotg210->lock);
	usb_hcd_giveback_urb(fotg210_to_hcd(fotg210), urb, status);
	spin_lock(&fotg210->lock);
}

static int qh_schedule(struct fotg210_hcd *fotg210, struct fotg210_qh *qh);

/* Process and free completed qtds for a qh, returning URBs to drivers.
 * Chases up to qh->hw_current.  Returns number of completions called,
 * indicating how much "real" work we did.
 */
static unsigned qh_completions(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh)
{
	struct fotg210_qtd *last, *end = qh->dummy;
	struct fotg210_qtd *qtd, *tmp;
	int last_status;
	int stopped;
	unsigned count = 0;
	u8 state;
	struct fotg210_qh_hw *hw = qh->hw;

	if (unlikely(list_empty(&qh->qtd_list)))
		return count;

	/* completions (or tasks on other cpus) must never clobber HALT
	 * till we've gone through and cleaned everything up, even when
	 * they add urbs to this qh's queue or mark them for unlinking.
	 *
	 * NOTE:  unlinking expects to be done in queue order.
	 *
	 * It's a bug for qh->qh_state to be anything other than
	 * QH_STATE_IDLE, unless our caller is scan_async() or
	 * scan_intr().
	 */
	state = qh->qh_state;
	qh->qh_state = QH_STATE_COMPLETING;
	stopped = (state == QH_STATE_IDLE);

rescan:
	last = NULL;
	last_status = -EINPROGRESS;
	qh->needs_rescan = 0;

	/* remove de-activated QTDs from front of queue.
	 * after faults (including short reads), cleanup this urb
	 * then let the queue advance.
	 * if queue is stopped, handles unlinks.
	 */
	list_for_each_entry_safe(qtd, tmp, &qh->qtd_list, qtd_list) {
		struct urb *urb;
		u32 token = 0;

		urb = qtd->urb;

		/* clean up any state from previous QTD ...*/
		if (last) {
			if (likely(last->urb != urb)) {
				fotg210_urb_done(fotg210, last->urb,
						last_status);
				count++;
				last_status = -EINPROGRESS;
			}
			fotg210_qtd_free(fotg210, last);
			last = NULL;
		}

		/* ignore urbs submitted during completions we reported */
		if (qtd == end)
			break;

		/* hardware copies qtd out of qh overlay */
		rmb();
		token = hc32_to_cpu(fotg210, qtd->hw_token);

		/* always clean up qtds the hc de-activated */
retry_xacterr:
		if ((token & QTD_STS_ACTIVE) == 0) {

			/* Report Data Buffer Error: non-fatal but useful */
			if (token & QTD_STS_DBE)
				fotg210_dbg(fotg210,
					"detected DataBufferErr for urb %p ep%d%s len %d, qtd %p [qh %p]\n",
					urb, usb_endpoint_num(&urb->ep->desc),
					usb_endpoint_dir_in(&urb->ep->desc)
						? "in" : "out",
					urb->transfer_buffer_length, qtd, qh);

			/* on STALL, error, and short reads this urb must
			 * complete and all its qtds must be recycled.
			 */
			if ((token & QTD_STS_HALT) != 0) {

				/* retry transaction errors until we
				 * reach the software xacterr limit
				 */
				if ((token & QTD_STS_XACT) &&
						QTD_CERR(token) == 0 &&
						++qh->xacterrs < QH_XACTERR_MAX &&
						!urb->unlinked) {
					fotg210_dbg(fotg210,
						"detected XactErr len %zu/%zu retry %d\n",
						qtd->length - QTD_LENGTH(token),
						qtd->length,
						qh->xacterrs);

					/* reset the token in the qtd and the
					 * qh overlay (which still contains
					 * the qtd) so that we pick up from
					 * where we left off
					 */
					token &= ~QTD_STS_HALT;
					token |= QTD_STS_ACTIVE |
						 (FOTG210_TUNE_CERR << 10);
					qtd->hw_token = cpu_to_hc32(fotg210,
							token);
					wmb();
					hw->hw_token = cpu_to_hc32(fotg210,
							token);
					goto retry_xacterr;
				}
				stopped = 1;

			/* magic dummy for some short reads; qh won't advance.
			 * that silicon quirk can kick in with this dummy too.
			 *
			 * other short reads won't stop the queue, including
			 * control transfers (status stage handles that) or
			 * most other single-qtd reads ... the queue stops if
			 * URB_SHORT_NOT_OK was set so the driver submitting
			 * the urbs could clean it up.
			 */
			} else if (IS_SHORT_READ(token) &&
					!(qtd->hw_alt_next &
					FOTG210_LIST_END(fotg210))) {
				stopped = 1;
			}

		/* stop scanning when we reach qtds the hc is using */
		} else if (likely(!stopped
				&& fotg210->rh_state >= FOTG210_RH_RUNNING)) {
			break;

		/* scan the whole queue for unlinks whenever it stops */
		} else {
			stopped = 1;

			/* cancel everything if we halt, suspend, etc */
			if (fotg210->rh_state < FOTG210_RH_RUNNING)
				last_status = -ESHUTDOWN;

			/* this qtd is active; skip it unless a previous qtd
			 * for its urb faulted, or its urb was canceled.
			 */
			else if (last_status == -EINPROGRESS && !urb->unlinked)
				continue;

			/* qh unlinked; token in overlay may be most current */
			if (state == QH_STATE_IDLE &&
					cpu_to_hc32(fotg210, qtd->qtd_dma)
					== hw->hw_current) {
				token = hc32_to_cpu(fotg210, hw->hw_token);

				/* An unlink may leave an incomplete
				 * async transaction in the TT buffer.
				 * We have to clear it.
				 */
				fotg210_clear_tt_buffer(fotg210, qh, urb,
						token);
			}
		}

		/* unless we already know the urb's status, collect qtd status
		 * and update count of bytes transferred.  in common short read
		 * cases with only one data qtd (including control transfers),
		 * queue processing won't halt.  but with two or more qtds (for
		 * example, with a 32 KB transfer), when the first qtd gets a
		 * short read the second must be removed by hand.
		 */
		if (last_status == -EINPROGRESS) {
			last_status = qtd_copy_status(fotg210, urb,
					qtd->length, token);
			if (last_status == -EREMOTEIO &&
					(qtd->hw_alt_next &
					FOTG210_LIST_END(fotg210)))
				last_status = -EINPROGRESS;

			/* As part of low/full-speed endpoint-halt processing
			 * we must clear the TT buffer (11.17.5).
			 */
			if (unlikely(last_status != -EINPROGRESS &&
					last_status != -EREMOTEIO)) {
				/* The TT's in some hubs malfunction when they
				 * receive this request following a STALL (they
				 * stop sending isochronous packets).  Since a
				 * STALL can't leave the TT buffer in a busy
				 * state (if you believe Figures 11-48 - 11-51
				 * in the USB 2.0 spec), we won't clear the TT
				 * buffer in this case.  Strictly speaking this
				 * is a violation of the spec.
				 */
				if (last_status != -EPIPE)
					fotg210_clear_tt_buffer(fotg210, qh,
							urb, token);
			}
		}

		/* if we're removing something not at the queue head,
		 * patch the hardware queue pointer.
		 */
		if (stopped && qtd->qtd_list.prev != &qh->qtd_list) {
			last = list_entry(qtd->qtd_list.prev,
					struct fotg210_qtd, qtd_list);
			last->hw_next = qtd->hw_next;
		}

		/* remove qtd; it's recycled after possible urb completion */
		list_del(&qtd->qtd_list);
		last = qtd;

		/* reinit the xacterr counter for the next qtd */
		qh->xacterrs = 0;
	}

	/* last urb's completion might still need calling */
	if (likely(last != NULL)) {
		fotg210_urb_done(fotg210, last->urb, last_status);
		count++;
		fotg210_qtd_free(fotg210, last);
	}

	/* Do we need to rescan for URBs dequeued during a giveback? */
	if (unlikely(qh->needs_rescan)) {
		/* If the QH is already unlinked, do the rescan now. */
		if (state == QH_STATE_IDLE)
			goto rescan;

		/* Otherwise we have to wait until the QH is fully unlinked.
		 * Our caller will start an unlink if qh->needs_rescan is
		 * set.  But if an unlink has already started, nothing needs
		 * to be done.
		 */
		if (state != QH_STATE_LINKED)
			qh->needs_rescan = 0;
	}

	/* restore original state; caller must unlink or relink */
	qh->qh_state = state;

	/* be sure the hardware's done with the qh before refreshing
	 * it after fault cleanup, or recovering from silicon wrongly
	 * overlaying the dummy qtd (which reduces DMA chatter).
	 */
	if (stopped != 0 || hw->hw_qtd_next == FOTG210_LIST_END(fotg210)) {
		switch (state) {
		case QH_STATE_IDLE:
			qh_refresh(fotg210, qh);
			break;
		case QH_STATE_LINKED:
			/* We won't refresh a QH that's linked (after the HC
			 * stopped the queue).  That avoids a race:
			 *  - HC reads first part of QH;
			 *  - CPU updates that first part and the token;
			 *  - HC reads rest of that QH, including token
			 * Result:  HC gets an inconsistent image, and then
			 * DMAs to/from the wrong memory (corrupting it).
			 *
			 * That should be rare for interrupt transfers,
			 * except maybe high bandwidth ...
			 */

			/* Tell the caller to start an unlink */
			qh->needs_rescan = 1;
			break;
		/* otherwise, unlink already started */
		}
	}

	return count;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))
/* ... and packet size, for any kind of endpoint descriptor */
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)

/* reverse of qh_urb_transaction:  free a list of TDs.
 * used for cleanup after errors, before HC sees an URB's TDs.
 */
static void qtd_list_free(struct fotg210_hcd *fotg210, struct urb *urb,
		struct list_head *head)
{
	struct fotg210_qtd *qtd, *temp;

	list_for_each_entry_safe(qtd, temp, head, qtd_list) {
		list_del(&qtd->qtd_list);
		fotg210_qtd_free(fotg210, qtd);
	}
}

/* create a list of filled qtds for this URB; won't link into qh.
 */
static struct list_head *qh_urb_transaction(struct fotg210_hcd *fotg210,
		struct urb *urb, struct list_head *head, gfp_t flags)
{
	struct fotg210_qtd *qtd, *qtd_prev;
	dma_addr_t buf;
	int len, this_sg_len, maxpacket;
	int is_input;
	u32 token;
	int i;
	struct scatterlist *sg;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = fotg210_qtd_alloc(fotg210, flags);
	if (unlikely(!qtd))
		return NULL;
	list_add_tail(&qtd->qtd_list, head);
	qtd->urb = urb;

	token = QTD_STS_ACTIVE;
	token |= (FOTG210_TUNE_CERR << 10);
	/* for split transactions, SplitXState initialized to zero */

	len = urb->transfer_buffer_length;
	is_input = usb_pipein(urb->pipe);
	if (usb_pipecontrol(urb->pipe)) {
		/* SETUP pid */
		qtd_fill(fotg210, qtd, urb->setup_dma,
				sizeof(struct usb_ctrlrequest),
				token | (2 /* "setup" */ << 8), 8);

		/* ... and always at least one more pid */
		token ^= QTD_TOGGLE;
		qtd_prev = qtd;
		qtd = fotg210_qtd_alloc(fotg210, flags);
		if (unlikely(!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(fotg210, qtd->qtd_dma);
		list_add_tail(&qtd->qtd_list, head);

		/* for zero length DATA stages, STATUS is always IN */
		if (len == 0)
			token |= (1 /* "in" */ << 8);
	}

	/*
	 * data transfer stage:  buffer setup
	 */
	i = urb->num_mapped_sgs;
	if (len > 0 && i > 0) {
		sg = urb->sg;
		buf = sg_dma_address(sg);

		/* urb->transfer_buffer_length may be smaller than the
		 * size of the scatterlist (or vice versa)
		 */
		this_sg_len = min_t(int, sg_dma_len(sg), len);
	} else {
		sg = NULL;
		buf = urb->transfer_dma;
		this_sg_len = len;
	}

	if (is_input)
		token |= (1 /* "in" */ << 8);
	/* else it's already initted to "out" pid (0 << 8) */

	maxpacket = max_packet(usb_maxpacket(urb->dev, urb->pipe, !is_input));

	/*
	 * buffer gets wrapped in one or more qtds;
	 * last one may be "short" (including zero len)
	 * and may serve as a control status ack
	 */
	for (;;) {
		int this_qtd_len;

		this_qtd_len = qtd_fill(fotg210, qtd, buf, this_sg_len, token,
				maxpacket);
		this_sg_len -= this_qtd_len;
		len -= this_qtd_len;
		buf += this_qtd_len;

		/*
		 * short reads advance to a "magic" dummy instead of the next
		 * qtd ... that forces the queue to stop, for manual cleanup.
		 * (this will usually be overridden later.)
		 */
		if (is_input)
			qtd->hw_alt_next = fotg210->async->hw->hw_alt_next;

		/* qh makes control packets use qtd toggle; maybe switch it */
		if ((maxpacket & (this_qtd_len + (maxpacket - 1))) == 0)
			token ^= QTD_TOGGLE;

		if (likely(this_sg_len <= 0)) {
			if (--i <= 0 || len <= 0)
				break;
			sg = sg_next(sg);
			buf = sg_dma_address(sg);
			this_sg_len = min_t(int, sg_dma_len(sg), len);
		}

		qtd_prev = qtd;
		qtd = fotg210_qtd_alloc(fotg210, flags);
		if (unlikely(!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(fotg210, qtd->qtd_dma);
		list_add_tail(&qtd->qtd_list, head);
	}

	/*
	 * unless the caller requires manual cleanup after short reads,
	 * have the alt_next mechanism keep the queue running after the
	 * last data qtd (the only one, for control and most other cases).
	 */
	if (likely((urb->transfer_flags & URB_SHORT_NOT_OK) == 0 ||
			usb_pipecontrol(urb->pipe)))
		qtd->hw_alt_next = FOTG210_LIST_END(fotg210);

	/*
	 * control requests may need a terminating data "status" ack;
	 * other OUT ones may need a terminating short packet
	 * (zero length).
	 */
	if (likely(urb->transfer_buffer_length != 0)) {
		int one_more = 0;

		if (usb_pipecontrol(urb->pipe)) {
			one_more = 1;
			token ^= 0x0100;	/* "in" <--> "out"  */
			token |= QTD_TOGGLE;	/* force DATA1 */
		} else if (usb_pipeout(urb->pipe)
				&& (urb->transfer_flags & URB_ZERO_PACKET)
				&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd_prev = qtd;
			qtd = fotg210_qtd_alloc(fotg210, flags);
			if (unlikely(!qtd))
				goto cleanup;
			qtd->urb = urb;
			qtd_prev->hw_next = QTD_NEXT(fotg210, qtd->qtd_dma);
			list_add_tail(&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill(fotg210, qtd, 0, 0, token, 0);
		}
	}

	/* by default, enable interrupt on urb completion */
	if (likely(!(urb->transfer_flags & URB_NO_INTERRUPT)))
		qtd->hw_token |= cpu_to_hc32(fotg210, QTD_IOC);
	return head;

cleanup:
	qtd_list_free(fotg210, urb, head);
	return NULL;
}

/* Would be best to create all qh's from config descriptors,
 * when each interface/altsetting is established.  Unlink
 * any previous qh and cancel its urbs first; endpoints are
 * implicitly reset then (data toggle too).
 * That'd mean updating how usbcore talks to HCDs. (2.7?)
*/


/* Each QH holds a qtd list; a QH is used for everything except iso.
 *
 * For interrupt urbs, the scheduler must set the microframe scheduling
 * mask(s) each time the QH gets scheduled.  For highspeed, that's
 * just one microframe in the s-mask.  For split interrupt transactions
 * there are additional complications: c-mask, maybe FSTNs.
 */
static struct fotg210_qh *qh_make(struct fotg210_hcd *fotg210, struct urb *urb,
		gfp_t flags)
{
	struct fotg210_qh *qh = fotg210_qh_alloc(fotg210, flags);
	u32 info1 = 0, info2 = 0;
	int is_input, type;
	int maxp = 0;
	struct usb_tt *tt = urb->dev->tt;
	struct fotg210_qh_hw *hw;

	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	info1 |= usb_pipeendpoint(urb->pipe) << 8;
	info1 |= usb_pipedevice(urb->pipe) << 0;

	is_input = usb_pipein(urb->pipe);
	type = usb_pipetype(urb->pipe);
	maxp = usb_maxpacket(urb->dev, urb->pipe, !is_input);

	/* 1024 byte maxpacket is a hardware ceiling.  High bandwidth
	 * acts like up to 3KB, but is built from smaller packets.
	 */
	if (max_packet(maxp) > 1024) {
		fotg210_dbg(fotg210, "bogus qh maxpacket %d\n",
				max_packet(maxp));
		goto done;
	}

	/* Compute interrupt scheduling parameters just once, and save.
	 * - allowing for high bandwidth, how many nsec/uframe are used?
	 * - split transactions need a second CSPLIT uframe; same question
	 * - splits also need a schedule gap (for full/low speed I/O)
	 * - qh has a polling interval
	 *
	 * For control/bulk requests, the HC or TT handles these.
	 */
	if (type == PIPE_INTERRUPT) {
		qh->usecs = NS_TO_US(usb_calc_bus_time(USB_SPEED_HIGH,
				is_input, 0,
				hb_mult(maxp) * max_packet(maxp)));
		qh->start = NO_FRAME;

		if (urb->dev->speed == USB_SPEED_HIGH) {
			qh->c_usecs = 0;
			qh->gap_uf = 0;

			qh->period = urb->interval >> 3;
			if (qh->period == 0 && urb->interval != 1) {
				/* NOTE interval 2 or 4 uframes could work.
				 * But interval 1 scheduling is simpler, and
				 * includes high bandwidth.
				 */
				urb->interval = 1;
			} else if (qh->period > fotg210->periodic_size) {
				qh->period = fotg210->periodic_size;
				urb->interval = qh->period << 3;
			}
		} else {
			int think_time;

			/* gap is f(FS/LS transfer times) */
			qh->gap_uf = 1 + usb_calc_bus_time(urb->dev->speed,
					is_input, 0, maxp) / (125 * 1000);

			/* FIXME this just approximates SPLIT/CSPLIT times */
			if (is_input) {		/* SPLIT, gap, CSPLIT+DATA */
				qh->c_usecs = qh->usecs + HS_USECS(0);
				qh->usecs = HS_USECS(1);
			} else {		/* SPLIT+DATA, gap, CSPLIT */
				qh->usecs += HS_USECS(1);
				qh->c_usecs = HS_USECS(0);
			}

			think_time = tt ? tt->think_time : 0;
			qh->tt_usecs = NS_TO_US(think_time +
					usb_calc_bus_time(urb->dev->speed,
					is_input, 0, max_packet(maxp)));
			qh->period = urb->interval;
			if (qh->period > fotg210->periodic_size) {
				qh->period = fotg210->periodic_size;
				urb->interval = qh->period;
			}
		}
	}

	/* support for tt scheduling, and access to toggles */
	qh->dev = urb->dev;

	/* using TT? */
	switch (urb->dev->speed) {
	case USB_SPEED_LOW:
		info1 |= QH_LOW_SPEED;
		/* FALL THROUGH */

	case USB_SPEED_FULL:
		/* EPS 0 means "full" */
		if (type != PIPE_INTERRUPT)
			info1 |= (FOTG210_TUNE_RL_TT << 28);
		if (type == PIPE_CONTROL) {
			info1 |= QH_CONTROL_EP;		/* for TT */
			info1 |= QH_TOGGLE_CTL;		/* toggle from qtd */
		}
		info1 |= maxp << 16;

		info2 |= (FOTG210_TUNE_MULT_TT << 30);

		/* Some Freescale processors have an erratum in which the
		 * port number in the queue head was 0..N-1 instead of 1..N.
		 */
		if (fotg210_has_fsl_portno_bug(fotg210))
			info2 |= (urb->dev->ttport-1) << 23;
		else
			info2 |= urb->dev->ttport << 23;

		/* set the address of the TT; for TDI's integrated
		 * root hub tt, leave it zeroed.
		 */
		if (tt && tt->hub != fotg210_to_hcd(fotg210)->self.root_hub)
			info2 |= tt->hub->devnum << 16;

		/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets c-mask } */

		break;

	case USB_SPEED_HIGH:		/* no TT involved */
		info1 |= QH_HIGH_SPEED;
		if (type == PIPE_CONTROL) {
			info1 |= (FOTG210_TUNE_RL_HS << 28);
			info1 |= 64 << 16;	/* usb2 fixed maxpacket */
			info1 |= QH_TOGGLE_CTL;	/* toggle from qtd */
			info2 |= (FOTG210_TUNE_MULT_HS << 30);
		} else if (type == PIPE_BULK) {
			info1 |= (FOTG210_TUNE_RL_HS << 28);
			/* The USB spec says that high speed bulk endpoints
			 * always use 512 byte maxpacket.  But some device
			 * vendors decided to ignore that, and MSFT is happy
			 * to help them do so.  So now people expect to use
			 * such nonconformant devices with Linux too; sigh.
			 */
			info1 |= max_packet(maxp) << 16;
			info2 |= (FOTG210_TUNE_MULT_HS << 30);
		} else {		/* PIPE_INTERRUPT */
			info1 |= max_packet(maxp) << 16;
			info2 |= hb_mult(maxp) << 30;
		}
		break;
	default:
		fotg210_dbg(fotg210, "bogus dev %p speed %d\n", urb->dev,
				urb->dev->speed);
done:
		qh_destroy(fotg210, qh);
		return NULL;
	}

	/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets s-mask } */

	/* init as live, toggle clear, advance to dummy */
	qh->qh_state = QH_STATE_IDLE;
	hw = qh->hw;
	hw->hw_info1 = cpu_to_hc32(fotg210, info1);
	hw->hw_info2 = cpu_to_hc32(fotg210, info2);
	qh->is_out = !is_input;
	usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe), !is_input, 1);
	qh_refresh(fotg210, qh);
	return qh;
}

static void enable_async(struct fotg210_hcd *fotg210)
{
	if (fotg210->async_count++)
		return;

	/* Stop waiting to turn off the async schedule */
	fotg210->enabled_hrtimer_events &= ~BIT(FOTG210_HRTIMER_DISABLE_ASYNC);

	/* Don't start the schedule until ASS is 0 */
	fotg210_poll_ASS(fotg210);
	turn_on_io_watchdog(fotg210);
}

static void disable_async(struct fotg210_hcd *fotg210)
{
	if (--fotg210->async_count)
		return;

	/* The async schedule and async_unlink list are supposed to be empty */
	WARN_ON(fotg210->async->qh_next.qh || fotg210->async_unlink);

	/* Don't turn off the schedule until ASS is 1 */
	fotg210_poll_ASS(fotg210);
}

/* move qh (and its qtds) onto async queue; maybe enable queue.  */

static void qh_link_async(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	__hc32 dma = QH_NEXT(fotg210, qh->qh_dma);
	struct fotg210_qh *head;

	/* Don't link a QH if there's a Clear-TT-Buffer pending */
	if (unlikely(qh->clearing_tt))
		return;

	WARN_ON(qh->qh_state != QH_STATE_IDLE);

	/* clear halt and/or toggle; and maybe recover from silicon quirk */
	qh_refresh(fotg210, qh);

	/* splice right after start */
	head = fotg210->async;
	qh->qh_next = head->qh_next;
	qh->hw->hw_next = head->hw->hw_next;
	wmb();

	head->qh_next.qh = qh;
	head->hw->hw_next = dma;

	qh->xacterrs = 0;
	qh->qh_state = QH_STATE_LINKED;
	/* qtd completions reported later by interrupt */

	enable_async(fotg210);
}

/* For control/bulk/interrupt, return QH with these TDs appended.
 * Allocates and initializes the QH if necessary.
 * Returns null if it can't allocate a QH it needs to.
 * If the QH has TDs (urbs) already, that's great.
 */
static struct fotg210_qh *qh_append_tds(struct fotg210_hcd *fotg210,
		struct urb *urb, struct list_head *qtd_list,
		int epnum, void **ptr)
{
	struct fotg210_qh *qh = NULL;
	__hc32 qh_addr_mask = cpu_to_hc32(fotg210, 0x7f);

	qh = (struct fotg210_qh *) *ptr;
	if (unlikely(qh == NULL)) {
		/* can't sleep here, we have fotg210->lock... */
		qh = qh_make(fotg210, urb, GFP_ATOMIC);
		*ptr = qh;
	}
	if (likely(qh != NULL)) {
		struct fotg210_qtd *qtd;

		if (unlikely(list_empty(qtd_list)))
			qtd = NULL;
		else
			qtd = list_entry(qtd_list->next, struct fotg210_qtd,
					qtd_list);

		/* control qh may need patching ... */
		if (unlikely(epnum == 0)) {
			/* usb_reset_device() briefly reverts to address 0 */
			if (usb_pipedevice(urb->pipe) == 0)
				qh->hw->hw_info1 &= ~qh_addr_mask;
		}

		/* just one way to queue requests: swap with the dummy qtd.
		 * only hc or qh_refresh() ever modify the overlay.
		 */
		if (likely(qtd != NULL)) {
			struct fotg210_qtd *dummy;
			dma_addr_t dma;
			__hc32 token;

			/* to avoid racing the HC, use the dummy td instead of
			 * the first td of our list (becomes new dummy).  both
			 * tds stay deactivated until we're done, when the
			 * HC is allowed to fetch the old dummy (4.10.2).
			 */
			token = qtd->hw_token;
			qtd->hw_token = HALT_BIT(fotg210);

			dummy = qh->dummy;

			dma = dummy->qtd_dma;
			*dummy = *qtd;
			dummy->qtd_dma = dma;

			list_del(&qtd->qtd_list);
			list_add(&dummy->qtd_list, qtd_list);
			list_splice_tail(qtd_list, &qh->qtd_list);

			fotg210_qtd_init(fotg210, qtd, qtd->qtd_dma);
			qh->dummy = qtd;

			/* hc must see the new dummy at list end */
			dma = qtd->qtd_dma;
			qtd = list_entry(qh->qtd_list.prev,
					struct fotg210_qtd, qtd_list);
			qtd->hw_next = QTD_NEXT(fotg210, dma);

			/* let the hc process these next qtds */
			wmb();
			dummy->hw_token = token;

			urb->hcpriv = qh;
		}
	}
	return qh;
}

static int submit_async(struct fotg210_hcd *fotg210, struct urb *urb,
		struct list_head *qtd_list, gfp_t mem_flags)
{
	int epnum;
	unsigned long flags;
	struct fotg210_qh *qh = NULL;
	int rc;

	epnum = urb->ep->desc.bEndpointAddress;

#ifdef FOTG210_URB_TRACE
	{
		struct fotg210_qtd *qtd;

		qtd = list_entry(qtd_list->next, struct fotg210_qtd, qtd_list);
		fotg210_dbg(fotg210,
				"%s %s urb %p ep%d%s len %d, qtd %p [qh %p]\n",
				__func__, urb->dev->devpath, urb,
				epnum & 0x0f, (epnum & USB_DIR_IN)
					? "in" : "out",
				urb->transfer_buffer_length,
				qtd, urb->ep->hcpriv);
	}
#endif

	spin_lock_irqsave(&fotg210->lock, flags);
	if (unlikely(!HCD_HW_ACCESSIBLE(fotg210_to_hcd(fotg210)))) {
		rc = -ESHUTDOWN;
		goto done;
	}
	rc = usb_hcd_link_urb_to_ep(fotg210_to_hcd(fotg210), urb);
	if (unlikely(rc))
		goto done;

	qh = qh_append_tds(fotg210, urb, qtd_list, epnum, &urb->ep->hcpriv);
	if (unlikely(qh == NULL)) {
		usb_hcd_unlink_urb_from_ep(fotg210_to_hcd(fotg210), urb);
		rc = -ENOMEM;
		goto done;
	}

	/* Control/bulk operations through TTs don't need scheduling,
	 * the HC and TT handle it when the TT has a buffer ready.
	 */
	if (likely(qh->qh_state == QH_STATE_IDLE))
		qh_link_async(fotg210, qh);
done:
	spin_unlock_irqrestore(&fotg210->lock, flags);
	if (unlikely(qh == NULL))
		qtd_list_free(fotg210, urb, qtd_list);
	return rc;
}

static void single_unlink_async(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh)
{
	struct fotg210_qh *prev;

	/* Add to the end of the list of QHs waiting for the next IAAD */
	qh->qh_state = QH_STATE_UNLINK;
	if (fotg210->async_unlink)
		fotg210->async_unlink_last->unlink_next = qh;
	else
		fotg210->async_unlink = qh;
	fotg210->async_unlink_last = qh;

	/* Unlink it from the schedule */
	prev = fotg210->async;
	while (prev->qh_next.qh != qh)
		prev = prev->qh_next.qh;

	prev->hw->hw_next = qh->hw->hw_next;
	prev->qh_next = qh->qh_next;
	if (fotg210->qh_scan_next == qh)
		fotg210->qh_scan_next = qh->qh_next.qh;
}

static void start_iaa_cycle(struct fotg210_hcd *fotg210, bool nested)
{
	/*
	 * Do nothing if an IAA cycle is already running or
	 * if one will be started shortly.
	 */
	if (fotg210->async_iaa || fotg210->async_unlinking)
		return;

	/* Do all the waiting QHs at once */
	fotg210->async_iaa = fotg210->async_unlink;
	fotg210->async_unlink = NULL;

	/* If the controller isn't running, we don't have to wait for it */
	if (unlikely(fotg210->rh_state < FOTG210_RH_RUNNING)) {
		if (!nested)		/* Avoid recursion */
			end_unlink_async(fotg210);

	/* Otherwise start a new IAA cycle */
	} else if (likely(fotg210->rh_state == FOTG210_RH_RUNNING)) {
		/* Make sure the unlinks are all visible to the hardware */
		wmb();

		fotg210_writel(fotg210, fotg210->command | CMD_IAAD,
				&fotg210->regs->command);
		fotg210_readl(fotg210, &fotg210->regs->command);
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_IAA_WATCHDOG,
				true);
	}
}

/* the async qh for the qtds being unlinked are now gone from the HC */

static void end_unlink_async(struct fotg210_hcd *fotg210)
{
	struct fotg210_qh *qh;

	/* Process the idle QHs */
restart:
	fotg210->async_unlinking = true;
	while (fotg210->async_iaa) {
		qh = fotg210->async_iaa;
		fotg210->async_iaa = qh->unlink_next;
		qh->unlink_next = NULL;

		qh->qh_state = QH_STATE_IDLE;
		qh->qh_next.qh = NULL;

		qh_completions(fotg210, qh);
		if (!list_empty(&qh->qtd_list) &&
				fotg210->rh_state == FOTG210_RH_RUNNING)
			qh_link_async(fotg210, qh);
		disable_async(fotg210);
	}
	fotg210->async_unlinking = false;

	/* Start a new IAA cycle if any QHs are waiting for it */
	if (fotg210->async_unlink) {
		start_iaa_cycle(fotg210, true);
		if (unlikely(fotg210->rh_state < FOTG210_RH_RUNNING))
			goto restart;
	}
}

static void unlink_empty_async(struct fotg210_hcd *fotg210)
{
	struct fotg210_qh *qh, *next;
	bool stopped = (fotg210->rh_state < FOTG210_RH_RUNNING);
	bool check_unlinks_later = false;

	/* Unlink all the async QHs that have been empty for a timer cycle */
	next = fotg210->async->qh_next.qh;
	while (next) {
		qh = next;
		next = qh->qh_next.qh;

		if (list_empty(&qh->qtd_list) &&
				qh->qh_state == QH_STATE_LINKED) {
			if (!stopped && qh->unlink_cycle ==
					fotg210->async_unlink_cycle)
				check_unlinks_later = true;
			else
				single_unlink_async(fotg210, qh);
		}
	}

	/* Start a new IAA cycle if any QHs are waiting for it */
	if (fotg210->async_unlink)
		start_iaa_cycle(fotg210, false);

	/* QHs that haven't been empty for long enough will be handled later */
	if (check_unlinks_later) {
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_ASYNC_UNLINKS,
				true);
		++fotg210->async_unlink_cycle;
	}
}

/* makes sure the async qh will become idle */
/* caller must own fotg210->lock */

static void start_unlink_async(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh)
{
	/*
	 * If the QH isn't linked then there's nothing we can do
	 * unless we were called during a giveback, in which case
	 * qh_completions() has to deal with it.
	 */
	if (qh->qh_state != QH_STATE_LINKED) {
		if (qh->qh_state == QH_STATE_COMPLETING)
			qh->needs_rescan = 1;
		return;
	}

	single_unlink_async(fotg210, qh);
	start_iaa_cycle(fotg210, false);
}

static void scan_async(struct fotg210_hcd *fotg210)
{
	struct fotg210_qh *qh;
	bool check_unlinks_later = false;

	fotg210->qh_scan_next = fotg210->async->qh_next.qh;
	while (fotg210->qh_scan_next) {
		qh = fotg210->qh_scan_next;
		fotg210->qh_scan_next = qh->qh_next.qh;
rescan:
		/* clean any finished work for this qh */
		if (!list_empty(&qh->qtd_list)) {
			int temp;

			/*
			 * Unlinks could happen here; completion reporting
			 * drops the lock.  That's why fotg210->qh_scan_next
			 * always holds the next qh to scan; if the next qh
			 * gets unlinked then fotg210->qh_scan_next is adjusted
			 * in single_unlink_async().
			 */
			temp = qh_completions(fotg210, qh);
			if (qh->needs_rescan) {
				start_unlink_async(fotg210, qh);
			} else if (list_empty(&qh->qtd_list)
					&& qh->qh_state == QH_STATE_LINKED) {
				qh->unlink_cycle = fotg210->async_unlink_cycle;
				check_unlinks_later = true;
			} else if (temp != 0)
				goto rescan;
		}
	}

	/*
	 * Unlink empty entries, reducing DMA usage as well
	 * as HCD schedule-scanning costs.  Delay for any qh
	 * we just scanned, there's a not-unusual case that it
	 * doesn't stay idle for long.
	 */
	if (check_unlinks_later && fotg210->rh_state == FOTG210_RH_RUNNING &&
			!(fotg210->enabled_hrtimer_events &
			BIT(FOTG210_HRTIMER_ASYNC_UNLINKS))) {
		fotg210_enable_event(fotg210,
				FOTG210_HRTIMER_ASYNC_UNLINKS, true);
		++fotg210->async_unlink_cycle;
	}
}
/* EHCI scheduled transaction support:  interrupt, iso, split iso
 * These are called "periodic" transactions in the EHCI spec.
 *
 * Note that for interrupt transfers, the QH/QTD manipulation is shared
 * with the "asynchronous" transaction support (control/bulk transfers).
 * The only real difference is in how interrupt transfers are scheduled.
 *
 * For ISO, we make an "iso_stream" head to serve the same role as a QH.
 * It keeps track of every ITD (or SITD) that's linked, and holds enough
 * pre-calculated schedule data to make appending to the queue be quick.
 */
static int fotg210_get_frame(struct usb_hcd *hcd);

/* periodic_next_shadow - return "next" pointer on shadow list
 * @periodic: host pointer to qh/itd
 * @tag: hardware tag for type of this record
 */
static union fotg210_shadow *periodic_next_shadow(struct fotg210_hcd *fotg210,
		union fotg210_shadow *periodic, __hc32 tag)
{
	switch (hc32_to_cpu(fotg210, tag)) {
	case Q_TYPE_QH:
		return &periodic->qh->qh_next;
	case Q_TYPE_FSTN:
		return &periodic->fstn->fstn_next;
	default:
		return &periodic->itd->itd_next;
	}
}

static __hc32 *shadow_next_periodic(struct fotg210_hcd *fotg210,
		union fotg210_shadow *periodic, __hc32 tag)
{
	switch (hc32_to_cpu(fotg210, tag)) {
	/* our fotg210_shadow.qh is actually software part */
	case Q_TYPE_QH:
		return &periodic->qh->hw->hw_next;
	/* others are hw parts */
	default:
		return periodic->hw_next;
	}
}

/* caller must hold fotg210->lock */
static void periodic_unlink(struct fotg210_hcd *fotg210, unsigned frame,
		void *ptr)
{
	union fotg210_shadow *prev_p = &fotg210->pshadow[frame];
	__hc32 *hw_p = &fotg210->periodic[frame];
	union fotg210_shadow here = *prev_p;

	/* find predecessor of "ptr"; hw and shadow lists are in sync */
	while (here.ptr && here.ptr != ptr) {
		prev_p = periodic_next_shadow(fotg210, prev_p,
				Q_NEXT_TYPE(fotg210, *hw_p));
		hw_p = shadow_next_periodic(fotg210, &here,
				Q_NEXT_TYPE(fotg210, *hw_p));
		here = *prev_p;
	}
	/* an interrupt entry (at list end) could have been shared */
	if (!here.ptr)
		return;

	/* update shadow and hardware lists ... the old "next" pointers
	 * from ptr may still be in use, the caller updates them.
	 */
	*prev_p = *periodic_next_shadow(fotg210, &here,
			Q_NEXT_TYPE(fotg210, *hw_p));

	*hw_p = *shadow_next_periodic(fotg210, &here,
			Q_NEXT_TYPE(fotg210, *hw_p));
}

/* how many of the uframe's 125 usecs are allocated? */
static unsigned short periodic_usecs(struct fotg210_hcd *fotg210,
		unsigned frame, unsigned uframe)
{
	__hc32 *hw_p = &fotg210->periodic[frame];
	union fotg210_shadow *q = &fotg210->pshadow[frame];
	unsigned usecs = 0;
	struct fotg210_qh_hw *hw;

	while (q->ptr) {
		switch (hc32_to_cpu(fotg210, Q_NEXT_TYPE(fotg210, *hw_p))) {
		case Q_TYPE_QH:
			hw = q->qh->hw;
			/* is it in the S-mask? */
			if (hw->hw_info2 & cpu_to_hc32(fotg210, 1 << uframe))
				usecs += q->qh->usecs;
			/* ... or C-mask? */
			if (hw->hw_info2 & cpu_to_hc32(fotg210,
					1 << (8 + uframe)))
				usecs += q->qh->c_usecs;
			hw_p = &hw->hw_next;
			q = &q->qh->qh_next;
			break;
		/* case Q_TYPE_FSTN: */
		default:
			/* for "save place" FSTNs, count the relevant INTR
			 * bandwidth from the previous frame
			 */
			if (q->fstn->hw_prev != FOTG210_LIST_END(fotg210))
				fotg210_dbg(fotg210, "ignoring FSTN cost ...\n");

			hw_p = &q->fstn->hw_next;
			q = &q->fstn->fstn_next;
			break;
		case Q_TYPE_ITD:
			if (q->itd->hw_transaction[uframe])
				usecs += q->itd->stream->usecs;
			hw_p = &q->itd->hw_next;
			q = &q->itd->itd_next;
			break;
		}
	}
	if (usecs > fotg210->uframe_periodic_max)
		fotg210_err(fotg210, "uframe %d sched overrun: %d usecs\n",
				frame * 8 + uframe, usecs);
	return usecs;
}

static int same_tt(struct usb_device *dev1, struct usb_device *dev2)
{
	if (!dev1->tt || !dev2->tt)
		return 0;
	if (dev1->tt != dev2->tt)
		return 0;
	if (dev1->tt->multi)
		return dev1->ttport == dev2->ttport;
	else
		return 1;
}

/* return true iff the device's transaction translator is available
 * for a periodic transfer starting at the specified frame, using
 * all the uframes in the mask.
 */
static int tt_no_collision(struct fotg210_hcd *fotg210, unsigned period,
		struct usb_device *dev, unsigned frame, u32 uf_mask)
{
	if (period == 0)	/* error */
		return 0;

	/* note bandwidth wastage:  split never follows csplit
	 * (different dev or endpoint) until the next uframe.
	 * calling convention doesn't make that distinction.
	 */
	for (; frame < fotg210->periodic_size; frame += period) {
		union fotg210_shadow here;
		__hc32 type;
		struct fotg210_qh_hw *hw;

		here = fotg210->pshadow[frame];
		type = Q_NEXT_TYPE(fotg210, fotg210->periodic[frame]);
		while (here.ptr) {
			switch (hc32_to_cpu(fotg210, type)) {
			case Q_TYPE_ITD:
				type = Q_NEXT_TYPE(fotg210, here.itd->hw_next);
				here = here.itd->itd_next;
				continue;
			case Q_TYPE_QH:
				hw = here.qh->hw;
				if (same_tt(dev, here.qh->dev)) {
					u32 mask;

					mask = hc32_to_cpu(fotg210,
							hw->hw_info2);
					/* "knows" no gap is needed */
					mask |= mask >> 8;
					if (mask & uf_mask)
						break;
				}
				type = Q_NEXT_TYPE(fotg210, hw->hw_next);
				here = here.qh->qh_next;
				continue;
			/* case Q_TYPE_FSTN: */
			default:
				fotg210_dbg(fotg210,
						"periodic frame %d bogus type %d\n",
						frame, type);
			}

			/* collision or error */
			return 0;
		}
	}

	/* no collision */
	return 1;
}

static void enable_periodic(struct fotg210_hcd *fotg210)
{
	if (fotg210->periodic_count++)
		return;

	/* Stop waiting to turn off the periodic schedule */
	fotg210->enabled_hrtimer_events &=
		~BIT(FOTG210_HRTIMER_DISABLE_PERIODIC);

	/* Don't start the schedule until PSS is 0 */
	fotg210_poll_PSS(fotg210);
	turn_on_io_watchdog(fotg210);
}

static void disable_periodic(struct fotg210_hcd *fotg210)
{
	if (--fotg210->periodic_count)
		return;

	/* Don't turn off the schedule until PSS is 1 */
	fotg210_poll_PSS(fotg210);
}

/* periodic schedule slots have iso tds (normal or split) first, then a
 * sparse tree for active interrupt transfers.
 *
 * this just links in a qh; caller guarantees uframe masks are set right.
 * no FSTN support (yet; fotg210 0.96+)
 */
static void qh_link_periodic(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	unsigned i;
	unsigned period = qh->period;

	dev_dbg(&qh->dev->dev,
			"link qh%d-%04x/%p start %d [%d/%d us]\n", period,
			hc32_to_cpup(fotg210, &qh->hw->hw_info2) &
			(QH_CMASK | QH_SMASK), qh, qh->start, qh->usecs,
			qh->c_usecs);

	/* high bandwidth, or otherwise every microframe */
	if (period == 0)
		period = 1;

	for (i = qh->start; i < fotg210->periodic_size; i += period) {
		union fotg210_shadow *prev = &fotg210->pshadow[i];
		__hc32 *hw_p = &fotg210->periodic[i];
		union fotg210_shadow here = *prev;
		__hc32 type = 0;

		/* skip the iso nodes at list head */
		while (here.ptr) {
			type = Q_NEXT_TYPE(fotg210, *hw_p);
			if (type == cpu_to_hc32(fotg210, Q_TYPE_QH))
				break;
			prev = periodic_next_shadow(fotg210, prev, type);
			hw_p = shadow_next_periodic(fotg210, &here, type);
			here = *prev;
		}

		/* sorting each branch by period (slow-->fast)
		 * enables sharing interior tree nodes
		 */
		while (here.ptr && qh != here.qh) {
			if (qh->period > here.qh->period)
				break;
			prev = &here.qh->qh_next;
			hw_p = &here.qh->hw->hw_next;
			here = *prev;
		}
		/* link in this qh, unless some earlier pass did that */
		if (qh != here.qh) {
			qh->qh_next = here;
			if (here.qh)
				qh->hw->hw_next = *hw_p;
			wmb();
			prev->qh = qh;
			*hw_p = QH_NEXT(fotg210, qh->qh_dma);
		}
	}
	qh->qh_state = QH_STATE_LINKED;
	qh->xacterrs = 0;

	/* update per-qh bandwidth for usbfs */
	fotg210_to_hcd(fotg210)->self.bandwidth_allocated += qh->period
		? ((qh->usecs + qh->c_usecs) / qh->period)
		: (qh->usecs * 8);

	list_add(&qh->intr_node, &fotg210->intr_qh_list);

	/* maybe enable periodic schedule processing */
	++fotg210->intr_count;
	enable_periodic(fotg210);
}

static void qh_unlink_periodic(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh)
{
	unsigned i;
	unsigned period;

	/*
	 * If qh is for a low/full-speed device, simply unlinking it
	 * could interfere with an ongoing split transaction.  To unlink
	 * it safely would require setting the QH_INACTIVATE bit and
	 * waiting at least one frame, as described in EHCI 4.12.2.5.
	 *
	 * We won't bother with any of this.  Instead, we assume that the
	 * only reason for unlinking an interrupt QH while the current URB
	 * is still active is to dequeue all the URBs (flush the whole
	 * endpoint queue).
	 *
	 * If rebalancing the periodic schedule is ever implemented, this
	 * approach will no longer be valid.
	 */

	/* high bandwidth, or otherwise part of every microframe */
	period = qh->period;
	if (!period)
		period = 1;

	for (i = qh->start; i < fotg210->periodic_size; i += period)
		periodic_unlink(fotg210, i, qh);

	/* update per-qh bandwidth for usbfs */
	fotg210_to_hcd(fotg210)->self.bandwidth_allocated -= qh->period
		? ((qh->usecs + qh->c_usecs) / qh->period)
		: (qh->usecs * 8);

	dev_dbg(&qh->dev->dev,
			"unlink qh%d-%04x/%p start %d [%d/%d us]\n",
			qh->period, hc32_to_cpup(fotg210, &qh->hw->hw_info2) &
			(QH_CMASK | QH_SMASK), qh, qh->start, qh->usecs,
			qh->c_usecs);

	/* qh->qh_next still "live" to HC */
	qh->qh_state = QH_STATE_UNLINK;
	qh->qh_next.ptr = NULL;

	if (fotg210->qh_scan_next == qh)
		fotg210->qh_scan_next = list_entry(qh->intr_node.next,
				struct fotg210_qh, intr_node);
	list_del(&qh->intr_node);
}

static void start_unlink_intr(struct fotg210_hcd *fotg210,
		struct fotg210_qh *qh)
{
	/* If the QH isn't linked then there's nothing we can do
	 * unless we were called during a giveback, in which case
	 * qh_completions() has to deal with it.
	 */
	if (qh->qh_state != QH_STATE_LINKED) {
		if (qh->qh_state == QH_STATE_COMPLETING)
			qh->needs_rescan = 1;
		return;
	}

	qh_unlink_periodic(fotg210, qh);

	/* Make sure the unlinks are visible before starting the timer */
	wmb();

	/*
	 * The EHCI spec doesn't say how long it takes the controller to
	 * stop accessing an unlinked interrupt QH.  The timer delay is
	 * 9 uframes; presumably that will be long enough.
	 */
	qh->unlink_cycle = fotg210->intr_unlink_cycle;

	/* New entries go at the end of the intr_unlink list */
	if (fotg210->intr_unlink)
		fotg210->intr_unlink_last->unlink_next = qh;
	else
		fotg210->intr_unlink = qh;
	fotg210->intr_unlink_last = qh;

	if (fotg210->intr_unlinking)
		;	/* Avoid recursive calls */
	else if (fotg210->rh_state < FOTG210_RH_RUNNING)
		fotg210_handle_intr_unlinks(fotg210);
	else if (fotg210->intr_unlink == qh) {
		fotg210_enable_event(fotg210, FOTG210_HRTIMER_UNLINK_INTR,
				true);
		++fotg210->intr_unlink_cycle;
	}
}

static void end_unlink_intr(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	struct fotg210_qh_hw *hw = qh->hw;
	int rc;

	qh->qh_state = QH_STATE_IDLE;
	hw->hw_next = FOTG210_LIST_END(fotg210);

	qh_completions(fotg210, qh);

	/* reschedule QH iff another request is queued */
	if (!list_empty(&qh->qtd_list) &&
			fotg210->rh_state == FOTG210_RH_RUNNING) {
		rc = qh_schedule(fotg210, qh);

		/* An error here likely indicates handshake failure
		 * or no space left in the schedule.  Neither fault
		 * should happen often ...
		 *
		 * FIXME kill the now-dysfunctional queued urbs
		 */
		if (rc != 0)
			fotg210_err(fotg210, "can't reschedule qh %p, err %d\n",
					qh, rc);
	}

	/* maybe turn off periodic schedule */
	--fotg210->intr_count;
	disable_periodic(fotg210);
}

static int check_period(struct fotg210_hcd *fotg210, unsigned frame,
		unsigned uframe, unsigned period, unsigned usecs)
{
	int claimed;

	/* complete split running into next frame?
	 * given FSTN support, we could sometimes check...
	 */
	if (uframe >= 8)
		return 0;

	/* convert "usecs we need" to "max already claimed" */
	usecs = fotg210->uframe_periodic_max - usecs;

	/* we "know" 2 and 4 uframe intervals were rejected; so
	 * for period 0, check _every_ microframe in the schedule.
	 */
	if (unlikely(period == 0)) {
		do {
			for (uframe = 0; uframe < 7; uframe++) {
				claimed = periodic_usecs(fotg210, frame,
						uframe);
				if (claimed > usecs)
					return 0;
			}
		} while ((frame += 1) < fotg210->periodic_size);

	/* just check the specified uframe, at that period */
	} else {
		do {
			claimed = periodic_usecs(fotg210, frame, uframe);
			if (claimed > usecs)
				return 0;
		} while ((frame += period) < fotg210->periodic_size);
	}

	/* success! */
	return 1;
}

static int check_intr_schedule(struct fotg210_hcd *fotg210, unsigned frame,
		unsigned uframe, const struct fotg210_qh *qh, __hc32 *c_maskp)
{
	int retval = -ENOSPC;
	u8 mask = 0;

	if (qh->c_usecs && uframe >= 6)		/* FSTN territory? */
		goto done;

	if (!check_period(fotg210, frame, uframe, qh->period, qh->usecs))
		goto done;
	if (!qh->c_usecs) {
		retval = 0;
		*c_maskp = 0;
		goto done;
	}

	/* Make sure this tt's buffer is also available for CSPLITs.
	 * We pessimize a bit; probably the typical full speed case
	 * doesn't need the second CSPLIT.
	 *
	 * NOTE:  both SPLIT and CSPLIT could be checked in just
	 * one smart pass...
	 */
	mask = 0x03 << (uframe + qh->gap_uf);
	*c_maskp = cpu_to_hc32(fotg210, mask << 8);

	mask |= 1 << uframe;
	if (tt_no_collision(fotg210, qh->period, qh->dev, frame, mask)) {
		if (!check_period(fotg210, frame, uframe + qh->gap_uf + 1,
				qh->period, qh->c_usecs))
			goto done;
		if (!check_period(fotg210, frame, uframe + qh->gap_uf,
				qh->period, qh->c_usecs))
			goto done;
		retval = 0;
	}
done:
	return retval;
}

/* "first fit" scheduling policy used the first time through,
 * or when the previous schedule slot can't be re-used.
 */
static int qh_schedule(struct fotg210_hcd *fotg210, struct fotg210_qh *qh)
{
	int status;
	unsigned uframe;
	__hc32 c_mask;
	unsigned frame;	/* 0..(qh->period - 1), or NO_FRAME */
	struct fotg210_qh_hw *hw = qh->hw;

	qh_refresh(fotg210, qh);
	hw->hw_next = FOTG210_LIST_END(fotg210);
	frame = qh->start;

	/* reuse the previous schedule slots, if we can */
	if (frame < qh->period) {
		uframe = ffs(hc32_to_cpup(fotg210, &hw->hw_info2) & QH_SMASK);
		status = check_intr_schedule(fotg210, frame, --uframe,
				qh, &c_mask);
	} else {
		uframe = 0;
		c_mask = 0;
		status = -ENOSPC;
	}

	/* else scan the schedule to find a group of slots such that all
	 * uframes have enough periodic bandwidth available.
	 */
	if (status) {
		/* "normal" case, uframing flexible except with splits */
		if (qh->period) {
			int i;

			for (i = qh->period; status && i > 0; --i) {
				frame = ++fotg210->random_frame % qh->period;
				for (uframe = 0; uframe < 8; uframe++) {
					status = check_intr_schedule(fotg210,
							frame, uframe, qh,
							&c_mask);
					if (status == 0)
						break;
				}
			}

		/* qh->period == 0 means every uframe */
		} else {
			frame = 0;
			status = check_intr_schedule(fotg210, 0, 0, qh,
					&c_mask);
		}
		if (status)
			goto done;
		qh->start = frame;

		/* reset S-frame and (maybe) C-frame masks */
		hw->hw_info2 &= cpu_to_hc32(fotg210, ~(QH_CMASK | QH_SMASK));
		hw->hw_info2 |= qh->period
			? cpu_to_hc32(fotg210, 1 << uframe)
			: cpu_to_hc32(fotg210, QH_SMASK);
		hw->hw_info2 |= c_mask;
	} else
		fotg210_dbg(fotg210, "reused qh %p schedule\n", qh);

	/* stuff into the periodic schedule */
	qh_link_periodic(fotg210, qh);
done:
	return status;
}

static int intr_submit(struct fotg210_hcd *fotg210, struct urb *urb,
		struct list_head *qtd_list, gfp_t mem_flags)
{
	unsigned epnum;
	unsigned long flags;
	struct fotg210_qh *qh;
	int status;
	struct list_head empty;

	/* get endpoint and transfer/schedule data */
	epnum = urb->ep->desc.bEndpointAddress;

	spin_lock_irqsave(&fotg210->lock, flags);

	if (unlikely(!HCD_HW_ACCESSIBLE(fotg210_to_hcd(fotg210)))) {
		status = -ESHUTDOWN;
		goto done_not_linked;
	}
	status = usb_hcd_link_urb_to_ep(fotg210_to_hcd(fotg210), urb);
	if (unlikely(status))
		goto done_not_linked;

	/* get qh and force any scheduling errors */
	INIT_LIST_HEAD(&empty);
	qh = qh_append_tds(fotg210, urb, &empty, epnum, &urb->ep->hcpriv);
	if (qh == NULL) {
		status = -ENOMEM;
		goto done;
	}
	if (qh->qh_state == QH_STATE_IDLE) {
		status = qh_schedule(fotg210, qh);
		if (status)
			goto done;
	}

	/* then queue the urb's tds to the qh */
	qh = qh_append_tds(fotg210, urb, qtd_list, epnum, &urb->ep->hcpriv);
	BUG_ON(qh == NULL);

	/* ... update usbfs periodic stats */
	fotg210_to_hcd(fotg210)->self.bandwidth_int_reqs++;

done:
	if (unlikely(status))
		usb_hcd_unlink_urb_from_ep(fotg210_to_hcd(fotg210), urb);
done_not_linked:
	spin_unlock_irqrestore(&fotg210->lock, flags);
	if (status)
		qtd_list_free(fotg210, urb, qtd_list);

	return status;
}

static void scan_intr(struct fotg210_hcd *fotg210)
{
	struct fotg210_qh *qh;

	list_for_each_entry_safe(qh, fotg210->qh_scan_next,
			&fotg210->intr_qh_list, intr_node) {
rescan:
		/* clean any finished work for this qh */
		if (!list_empty(&qh->qtd_list)) {
			int temp;

			/*
			 * Unlinks could happen here; completion reporting
			 * drops the lock.  That's why fotg210->qh_scan_next
			 * always holds the next qh to scan; if the next qh
			 * gets unlinked then fotg210->qh_scan_next is adjusted
			 * in qh_unlink_periodic().
			 */
			temp = qh_completions(fotg210, qh);
			if (unlikely(qh->needs_rescan ||
					(list_empty(&qh->qtd_list) &&
					qh->qh_state == QH_STATE_LINKED)))
				start_unlink_intr(fotg210, qh);
			else if (temp != 0)
				goto rescan;
		}
	}
}

/* fotg210_iso_stream ops work with both ITD and SITD */

static struct fotg210_iso_stream *iso_stream_alloc(gfp_t mem_flags)
{
	struct fotg210_iso_stream *stream;

	stream = kzalloc(sizeof(*stream), mem_flags);
	if (likely(stream != NULL)) {
		INIT_LIST_HEAD(&stream->td_list);
		INIT_LIST_HEAD(&stream->free_list);
		stream->next_uframe = -1;
	}
	return stream;
}

static void iso_stream_init(struct fotg210_hcd *fotg210,
		struct fotg210_iso_stream *stream, struct usb_device *dev,
		int pipe, unsigned interval)
{
	u32 buf1;
	unsigned epnum, maxp;
	int is_input;
	long bandwidth;
	unsigned multi;

	/*
	 * this might be a "high bandwidth" highspeed endpoint,
	 * as encoded in the ep descriptor's wMaxPacket field
	 */
	epnum = usb_pipeendpoint(pipe);
	is_input = usb_pipein(pipe) ? USB_DIR_IN : 0;
	maxp = usb_maxpacket(dev, pipe, !is_input);
	if (is_input)
		buf1 = (1 << 11);
	else
		buf1 = 0;

	maxp = max_packet(maxp);
	multi = hb_mult(maxp);
	buf1 |= maxp;
	maxp *= multi;

	stream->buf0 = cpu_to_hc32(fotg210, (epnum << 8) | dev->devnum);
	stream->buf1 = cpu_to_hc32(fotg210, buf1);
	stream->buf2 = cpu_to_hc32(fotg210, multi);

	/* usbfs wants to report the average usecs per frame tied up
	 * when transfers on this endpoint are scheduled ...
	 */
	if (dev->speed == USB_SPEED_FULL) {
		interval <<= 3;
		stream->usecs = NS_TO_US(usb_calc_bus_time(dev->speed,
				is_input, 1, maxp));
		stream->usecs /= 8;
	} else {
		stream->highspeed = 1;
		stream->usecs = HS_USECS_ISO(maxp);
	}
	bandwidth = stream->usecs * 8;
	bandwidth /= interval;

	stream->bandwidth = bandwidth;
	stream->udev = dev;
	stream->bEndpointAddress = is_input | epnum;
	stream->interval = interval;
	stream->maxp = maxp;
}

static struct fotg210_iso_stream *iso_stream_find(struct fotg210_hcd *fotg210,
		struct urb *urb)
{
	unsigned epnum;
	struct fotg210_iso_stream *stream;
	struct usb_host_endpoint *ep;
	unsigned long flags;

	epnum = usb_pipeendpoint(urb->pipe);
	if (usb_pipein(urb->pipe))
		ep = urb->dev->ep_in[epnum];
	else
		ep = urb->dev->ep_out[epnum];

	spin_lock_irqsave(&fotg210->lock, flags);
	stream = ep->hcpriv;

	if (unlikely(stream == NULL)) {
		stream = iso_stream_alloc(GFP_ATOMIC);
		if (likely(stream != NULL)) {
			ep->hcpriv = stream;
			stream->ep = ep;
			iso_stream_init(fotg210, stream, urb->dev, urb->pipe,
					urb->interval);
		}

	/* if dev->ep[epnum] is a QH, hw is set */
	} else if (unlikely(stream->hw != NULL)) {
		fotg210_dbg(fotg210, "dev %s ep%d%s, not iso??\n",
				urb->dev->devpath, epnum,
				usb_pipein(urb->pipe) ? "in" : "out");
		stream = NULL;
	}

	spin_unlock_irqrestore(&fotg210->lock, flags);
	return stream;
}

/* fotg210_iso_sched ops can be ITD-only or SITD-only */

static struct fotg210_iso_sched *iso_sched_alloc(unsigned packets,
		gfp_t mem_flags)
{
	struct fotg210_iso_sched *iso_sched;
	int size = sizeof(*iso_sched);

	size += packets * sizeof(struct fotg210_iso_packet);
	iso_sched = kzalloc(size, mem_flags);
	if (likely(iso_sched != NULL))
		INIT_LIST_HEAD(&iso_sched->td_list);

	return iso_sched;
}

static inline void itd_sched_init(struct fotg210_hcd *fotg210,
		struct fotg210_iso_sched *iso_sched,
		struct fotg210_iso_stream *stream, struct urb *urb)
{
	unsigned i;
	dma_addr_t dma = urb->transfer_dma;

	/* how many uframes are needed for these transfers */
	iso_sched->span = urb->number_of_packets * stream->interval;

	/* figure out per-uframe itd fields that we'll need later
	 * when we fit new itds into the schedule.
	 */
	for (i = 0; i < urb->number_of_packets; i++) {
		struct fotg210_iso_packet *uframe = &iso_sched->packet[i];
		unsigned length;
		dma_addr_t buf;
		u32 trans;

		length = urb->iso_frame_desc[i].length;
		buf = dma + urb->iso_frame_desc[i].offset;

		trans = FOTG210_ISOC_ACTIVE;
		trans |= buf & 0x0fff;
		if (unlikely(((i + 1) == urb->number_of_packets))
				&& !(urb->transfer_flags & URB_NO_INTERRUPT))
			trans |= FOTG210_ITD_IOC;
		trans |= length << 16;
		uframe->transaction = cpu_to_hc32(fotg210, trans);

		/* might need to cross a buffer page within a uframe */
		uframe->bufp = (buf & ~(u64)0x0fff);
		buf += length;
		if (unlikely((uframe->bufp != (buf & ~(u64)0x0fff))))
			uframe->cross = 1;
	}
}

static void iso_sched_free(struct fotg210_iso_stream *stream,
		struct fotg210_iso_sched *iso_sched)
{
	if (!iso_sched)
		return;
	/* caller must hold fotg210->lock!*/
	list_splice(&iso_sched->td_list, &stream->free_list);
	kfree(iso_sched);
}

static int itd_urb_transaction(struct fotg210_iso_stream *stream,
		struct fotg210_hcd *fotg210, struct urb *urb, gfp_t mem_flags)
{
	struct fotg210_itd *itd;
	dma_addr_t itd_dma;
	int i;
	unsigned num_itds;
	struct fotg210_iso_sched *sched;
	unsigned long flags;

	sched = iso_sched_alloc(urb->number_of_packets, mem_flags);
	if (unlikely(sched == NULL))
		return -ENOMEM;

	itd_sched_init(fotg210, sched, stream, urb);

	if (urb->interval < 8)
		num_itds = 1 + (sched->span + 7) / 8;
	else
		num_itds = urb->number_of_packets;

	/* allocate/init ITDs */
	spin_lock_irqsave(&fotg210->lock, flags);
	for (i = 0; i < num_itds; i++) {

		/*
		 * Use iTDs from the free list, but not iTDs that may
		 * still be in use by the hardware.
		 */
		if (likely(!list_empty(&stream->free_list))) {
			itd = list_first_entry(&stream->free_list,
					struct fotg210_itd, itd_list);
			if (itd->frame == fotg210->now_frame)
				goto alloc_itd;
			list_del(&itd->itd_list);
			itd_dma = itd->itd_dma;
		} else {
alloc_itd:
			spin_unlock_irqrestore(&fotg210->lock, flags);
			itd = dma_pool_zalloc(fotg210->itd_pool, mem_flags,
					&itd_dma);
			spin_lock_irqsave(&fotg210->lock, flags);
			if (!itd) {
				iso_sched_free(stream, sched);
				spin_unlock_irqrestore(&fotg210->lock, flags);
				return -ENOMEM;
			}
		}

		itd->itd_dma = itd_dma;
		list_add(&itd->itd_list, &sched->td_list);
	}
	spin_unlock_irqrestore(&fotg210->lock, flags);

	/* temporarily store schedule info in hcpriv */
	urb->hcpriv = sched;
	urb->error_count = 0;
	return 0;
}

static inline int itd_slot_ok(struct fotg210_hcd *fotg210, u32 mod, u32 uframe,
		u8 usecs, u32 period)
{
	uframe %= period;
	do {
		/* can't commit more than uframe_periodic_max usec */
		if (periodic_usecs(fotg210, uframe >> 3, uframe & 0x7)
				> (fotg210->uframe_periodic_max - usecs))
			return 0;

		/* we know urb->interval is 2^N uframes */
		uframe += period;
	} while (uframe < mod);
	return 1;
}

/* This scheduler plans almost as far into the future as it has actual
 * periodic schedule slots.  (Affected by TUNE_FLS, which defaults to
 * "as small as possible" to be cache-friendlier.)  That limits the size
 * transfers you can stream reliably; avoid more than 64 msec per urb.
 * Also avoid queue depths of less than fotg210's worst irq latency (affected
 * by the per-urb URB_NO_INTERRUPT hint, the log2_irq_thresh module parameter,
 * and other factors); or more than about 230 msec total (for portability,
 * given FOTG210_TUNE_FLS and the slop).  Or, write a smarter scheduler!
 */

#define SCHEDULE_SLOP 80 /* microframes */

static int iso_stream_schedule(struct fotg210_hcd *fotg210, struct urb *urb,
		struct fotg210_iso_stream *stream)
{
	u32 now, next, start, period, span;
	int status;
	unsigned mod = fotg210->periodic_size << 3;
	struct fotg210_iso_sched *sched = urb->hcpriv;

	period = urb->interval;
	span = sched->span;

	if (span > mod - SCHEDULE_SLOP) {
		fotg210_dbg(fotg210, "iso request %p too long\n", urb);
		status = -EFBIG;
		goto fail;
	}

	now = fotg210_read_frame_index(fotg210) & (mod - 1);

	/* Typical case: reuse current schedule, stream is still active.
	 * Hopefully there are no gaps from the host falling behind
	 * (irq delays etc), but if there are we'll take the next
	 * slot in the schedule, implicitly assuming URB_ISO_ASAP.
	 */
	if (likely(!list_empty(&stream->td_list))) {
		u32 excess;

		/* For high speed devices, allow scheduling within the
		 * isochronous scheduling threshold.  For full speed devices
		 * and Intel PCI-based controllers, don't (work around for
		 * Intel ICH9 bug).
		 */
		if (!stream->highspeed && fotg210->fs_i_thresh)
			next = now + fotg210->i_thresh;
		else
			next = now;

		/* Fell behind (by up to twice the slop amount)?
		 * We decide based on the time of the last currently-scheduled
		 * slot, not the time of the next available slot.
		 */
		excess = (stream->next_uframe - period - next) & (mod - 1);
		if (excess >= mod - 2 * SCHEDULE_SLOP)
			start = next + excess - mod + period *
					DIV_ROUND_UP(mod - excess, period);
		else
			start = next + excess + period;
		if (start - now >= mod) {
			fotg210_dbg(fotg210, "request %p would overflow (%d+%d >= %d)\n",
					urb, start - now - period, period,
					mod);
			status = -EFBIG;
			goto fail;
		}
	}

	/* need to schedule; when's the next (u)frame we could start?
	 * this is bigger than fotg210->i_thresh allows; scheduling itself
	 * isn't free, the slop should handle reasonably slow cpus.  it
	 * can also help high bandwidth if the dma and irq loads don't
	 * jump until after the queue is primed.
	 */
	else {
		int done = 0;

		start = SCHEDULE_SLOP + (now & ~0x07);

		/* NOTE:  assumes URB_ISO_ASAP, to limit complexity/bugs */

		/* find a uframe slot with enough bandwidth.
		 * Early uframes are more precious because full-speed
		 * iso IN transfers can't use late uframes,
		 * and therefore they should be allocated last.
		 */
		next = start;
		start += period;
		do {
			start--;
			/* check schedule: enough space? */
			if (itd_slot_ok(fotg210, mod, start,
					stream->usecs, period))
				done = 1;
		} while (start > next && !done);

		/* no room in the schedule */
		if (!done) {
			fotg210_dbg(fotg210, "iso resched full %p (now %d max %d)\n",
					urb, now, now + mod);
			status = -ENOSPC;
			goto fail;
		}
	}

	/* Tried to schedule too far into the future? */
	if (unlikely(start - now + span - period >=
			mod - 2 * SCHEDULE_SLOP)) {
		fotg210_dbg(fotg210, "request %p would overflow (%d+%d >= %d)\n",
				urb, start - now, span - period,
				mod - 2 * SCHEDULE_SLOP);
		status = -EFBIG;
		goto fail;
	}

	stream->next_uframe = start & (mod - 1);

	/* report high speed start in uframes; full speed, in frames */
	urb->start_frame = stream->next_uframe;
	if (!stream->highspeed)
		urb->start_frame >>= 3;

	/* Make sure scan_isoc() sees these */
	if (fotg210->isoc_count == 0)
		fotg210->next_frame = now >> 3;
	return 0;

fail:
	iso_sched_free(stream, sched);
	urb->hcpriv = NULL;
	return status;
}

static inline void itd_init(struct fotg210_hcd *fotg210,
		struct fotg210_iso_stream *stream, struct fotg210_itd *itd)
{
	int i;

	/* it's been recently zeroed */
	itd->hw_next = FOTG210_LIST_END(fotg210);
	itd->hw_bufp[0] = stream->buf0;
	itd->hw_bufp[1] = stream->buf1;
	itd->hw_bufp[2] = stream->buf2;

	for (i = 0; i < 8; i++)
		itd->index[i] = -1;

	/* All other fields are filled when scheduling */
}

static inline void itd_patch(struct fotg210_hcd *fotg210,
		struct fotg210_itd *itd, struct fotg210_iso_sched *iso_sched,
		unsigned index, u16 uframe)
{
	struct fotg210_iso_packet *uf = &iso_sched->packet[index];
	unsigned pg = itd->pg;

	uframe &= 0x07;
	itd->index[uframe] = index;

	itd->hw_transaction[uframe] = uf->transaction;
	itd->hw_transaction[uframe] |= cpu_to_hc32(fotg210, pg << 12);
	itd->hw_bufp[pg] |= cpu_to_hc32(fotg210, uf->bufp & ~(u32)0);
	itd->hw_bufp_hi[pg] |= cpu_to_hc32(fotg210, (u32)(uf->bufp >> 32));

	/* iso_frame_desc[].offset must be strictly increasing */
	if (unlikely(uf->cross)) {
		u64 bufp = uf->bufp + 4096;

		itd->pg = ++pg;
		itd->hw_bufp[pg] |= cpu_to_hc32(fotg210, bufp & ~(u32)0);
		itd->hw_bufp_hi[pg] |= cpu_to_hc32(fotg210, (u32)(bufp >> 32));
	}
}

static inline void itd_link(struct fotg210_hcd *fotg210, unsigned frame,
		struct fotg210_itd *itd)
{
	union fotg210_shadow *prev = &fotg210->pshadow[frame];
	__hc32 *hw_p = &fotg210->periodic[frame];
	union fotg210_shadow here = *prev;
	__hc32 type = 0;

	/* skip any iso nodes which might belong to previous microframes */
	while (here.ptr) {
		type = Q_NEXT_TYPE(fotg210, *hw_p);
		if (type == cpu_to_hc32(fotg210, Q_TYPE_QH))
			break;
		prev = periodic_next_shadow(fotg210, prev, type);
		hw_p = shadow_next_periodic(fotg210, &here, type);
		here = *prev;
	}

	itd->itd_next = here;
	itd->hw_next = *hw_p;
	prev->itd = itd;
	itd->frame = frame;
	wmb();
	*hw_p = cpu_to_hc32(fotg210, itd->itd_dma | Q_TYPE_ITD);
}

/* fit urb's itds into the selected schedule slot; activate as needed */
static void itd_link_urb(struct fotg210_hcd *fotg210, struct urb *urb,
		unsigned mod, struct fotg210_iso_stream *stream)
{
	int packet;
	unsigned next_uframe, uframe, frame;
	struct fotg210_iso_sched *iso_sched = urb->hcpriv;
	struct fotg210_itd *itd;

	next_uframe = stream->next_uframe & (mod - 1);

	if (unlikely(list_empty(&stream->td_list))) {
		fotg210_to_hcd(fotg210)->self.bandwidth_allocated
				+= stream->bandwidth;
		fotg210_dbg(fotg210,
			"schedule devp %s ep%d%s-iso period %d start %d.%d\n",
			urb->dev->devpath, stream->bEndpointAddress & 0x0f,
			(stream->bEndpointAddress & USB_DIR_IN) ? "in" : "out",
			urb->interval,
			next_uframe >> 3, next_uframe & 0x7);
	}

	/* fill iTDs uframe by uframe */
	for (packet = 0, itd = NULL; packet < urb->number_of_packets;) {
		if (itd == NULL) {
			/* ASSERT:  we have all necessary itds */

			/* ASSERT:  no itds for this endpoint in this uframe */

			itd = list_entry(iso_sched->td_list.next,
					struct fotg210_itd, itd_list);
			list_move_tail(&itd->itd_list, &stream->td_list);
			itd->stream = stream;
			itd->urb = urb;
			itd_init(fotg210, stream, itd);
		}

		uframe = next_uframe & 0x07;
		frame = next_uframe >> 3;

		itd_patch(fotg210, itd, iso_sched, packet, uframe);

		next_uframe += stream->interval;
		next_uframe &= mod - 1;
		packet++;

		/* link completed itds into the schedule */
		if (((next_uframe >> 3) != frame)
				|| packet == urb->number_of_packets) {
			itd_link(fotg210, frame & (fotg210->periodic_size - 1),
					itd);
			itd = NULL;
		}
	}
	stream->next_uframe = next_uframe;

	/* don't need that schedule data any more */
	iso_sched_free(stream, iso_sched);
	urb->hcpriv = NULL;

	++fotg210->isoc_count;
	enable_periodic(fotg210);
}

#define ISO_ERRS (FOTG210_ISOC_BUF_ERR | FOTG210_ISOC_BABBLE |\
		FOTG210_ISOC_XACTERR)

/* Process and recycle a completed ITD.  Return true iff its urb completed,
 * and hence its completion callback probably added things to the hardware
 * schedule.
 *
 * Note that we carefully avoid recycling this descriptor until after any
 * completion callback runs, so that it won't be reused quickly.  That is,
 * assuming (a) no more than two urbs per frame on this endpoint, and also
 * (b) only this endpoint's completions submit URBs.  It seems some silicon
 * corrupts things if you reuse completed descriptors very quickly...
 */
static bool itd_complete(struct fotg210_hcd *fotg210, struct fotg210_itd *itd)
{
	struct urb *urb = itd->urb;
	struct usb_iso_packet_descriptor *desc;
	u32 t;
	unsigned uframe;
	int urb_index = -1;
	struct fotg210_iso_stream *stream = itd->stream;
	struct usb_device *dev;
	bool retval = false;

	/* for each uframe with a packet */
	for (uframe = 0; uframe < 8; uframe++) {
		if (likely(itd->index[uframe] == -1))
			continue;
		urb_index = itd->index[uframe];
		desc = &urb->iso_frame_desc[urb_index];

		t = hc32_to_cpup(fotg210, &itd->hw_transaction[uframe]);
		itd->hw_transaction[uframe] = 0;

		/* report transfer status */
		if (unlikely(t & ISO_ERRS)) {
			urb->error_count++;
			if (t & FOTG210_ISOC_BUF_ERR)
				desc->status = usb_pipein(urb->pipe)
					? -ENOSR  /* hc couldn't read */
					: -ECOMM; /* hc couldn't write */
			else if (t & FOTG210_ISOC_BABBLE)
				desc->status = -EOVERFLOW;
			else /* (t & FOTG210_ISOC_XACTERR) */
				desc->status = -EPROTO;

			/* HC need not update length with this error */
			if (!(t & FOTG210_ISOC_BABBLE)) {
				desc->actual_length =
					fotg210_itdlen(urb, desc, t);
				urb->actual_length += desc->actual_length;
			}
		} else if (likely((t & FOTG210_ISOC_ACTIVE) == 0)) {
			desc->status = 0;
			desc->actual_length = fotg210_itdlen(urb, desc, t);
			urb->actual_length += desc->actual_length;
		} else {
			/* URB was too late */
			desc->status = -EXDEV;
		}
	}

	/* handle completion now? */
	if (likely((urb_index + 1) != urb->number_of_packets))
		goto done;

	/* ASSERT: it's really the last itd for this urb
	 * list_for_each_entry (itd, &stream->td_list, itd_list)
	 *	BUG_ON (itd->urb == urb);
	 */

	/* give urb back to the driver; completion often (re)submits */
	dev = urb->dev;
	fotg210_urb_done(fotg210, urb, 0);
	retval = true;
	urb = NULL;

	--fotg210->isoc_count;
	disable_periodic(fotg210);

	if (unlikely(list_is_singular(&stream->td_list))) {
		fotg210_to_hcd(fotg210)->self.bandwidth_allocated
				-= stream->bandwidth;
		fotg210_dbg(fotg210,
			"deschedule devp %s ep%d%s-iso\n",
			dev->devpath, stream->bEndpointAddress & 0x0f,
			(stream->bEndpointAddress & USB_DIR_IN) ? "in" : "out");
	}

done:
	itd->urb = NULL;

	/* Add to the end of the free list for later reuse */
	list_move_tail(&itd->itd_list, &stream->free_list);

	/* Recycle the iTDs when the pipeline is empty (ep no longer in use) */
	if (list_empty(&stream->td_list)) {
		list_splice_tail_init(&stream->free_list,
				&fotg210->cached_itd_list);
		start_free_itds(fotg210);
	}

	return retval;
}

static int itd_submit(struct fotg210_hcd *fotg210, struct urb *urb,
		gfp_t mem_flags)
{
	int status = -EINVAL;
	unsigned long flags;
	struct fotg210_iso_stream *stream;

	/* Get iso_stream head */
	stream = iso_stream_find(fotg210, urb);
	if (unlikely(stream == NULL)) {
		fotg210_dbg(fotg210, "can't get iso stream\n");
		return -ENOMEM;
	}
	if (unlikely(urb->interval != stream->interval &&
			fotg210_port_speed(fotg210, 0) ==
			USB_PORT_STAT_HIGH_SPEED)) {
		fotg210_dbg(fotg210, "can't change iso interval %d --> %d\n",
				stream->interval, urb->interval);
		goto done;
	}

#ifdef FOTG210_URB_TRACE
	fotg210_dbg(fotg210,
			"%s %s urb %p ep%d%s len %d, %d pkts %d uframes[%p]\n",
			__func__, urb->dev->devpath, urb,
			usb_pipeendpoint(urb->pipe),
			usb_pipein(urb->pipe) ? "in" : "out",
			urb->transfer_buffer_length,
			urb->number_of_packets, urb->interval,
			stream);
#endif

	/* allocate ITDs w/o locking anything */
	status = itd_urb_transaction(stream, fotg210, urb, mem_flags);
	if (unlikely(status < 0)) {
		fotg210_dbg(fotg210, "can't init itds\n");
		goto done;
	}

	/* schedule ... need to lock */
	spin_lock_irqsave(&fotg210->lock, flags);
	if (unlikely(!HCD_HW_ACCESSIBLE(fotg210_to_hcd(fotg210)))) {
		status = -ESHUTDOWN;
		goto done_not_linked;
	}
	status = usb_hcd_link_urb_to_ep(fotg210_to_hcd(fotg210), urb);
	if (unlikely(status))
		goto done_not_linked;
	status = iso_stream_schedule(fotg210, urb, stream);
	if (likely(status == 0))
		itd_link_urb(fotg210, urb, fotg210->periodic_size << 3, stream);
	else
		usb_hcd_unlink_urb_from_ep(fotg210_to_hcd(fotg210), urb);
done_not_linked:
	spin_unlock_irqrestore(&fotg210->lock, flags);
done:
	return status;
}

static inline int scan_frame_queue(struct fotg210_hcd *fotg210, unsigned frame,
		unsigned now_frame, bool live)
{
	unsigned uf;
	bool modified;
	union fotg210_shadow q, *q_p;
	__hc32 type, *hw_p;

	/* scan each element in frame's queue for completions */
	q_p = &fotg210->pshadow[frame];
	hw_p = &fotg210->periodic[frame];
	q.ptr = q_p->ptr;
	type = Q_NEXT_TYPE(fotg210, *hw_p);
	modified = false;

	while (q.ptr) {
		switch (hc32_to_cpu(fotg210, type)) {
		case Q_TYPE_ITD:
			/* If this ITD is still active, leave it for
			 * later processing ... check the next entry.
			 * No need to check for activity unless the
			 * frame is current.
			 */
			if (frame == now_frame && live) {
				rmb();
				for (uf = 0; uf < 8; uf++) {
					if (q.itd->hw_transaction[uf] &
							ITD_ACTIVE(fotg210))
						break;
				}
				if (uf < 8) {
					q_p = &q.itd->itd_next;
					hw_p = &q.itd->hw_next;
					type = Q_NEXT_TYPE(fotg210,
							q.itd->hw_next);
					q = *q_p;
					break;
				}
			}

			/* Take finished ITDs out of the schedule
			 * and process them:  recycle, maybe report
			 * URB completion.  HC won't cache the
			 * pointer for much longer, if at all.
			 */
			*q_p = q.itd->itd_next;
			*hw_p = q.itd->hw_next;
			type = Q_NEXT_TYPE(fotg210, q.itd->hw_next);
			wmb();
			modified = itd_complete(fotg210, q.itd);
			q = *q_p;
			break;
		default:
			fotg210_dbg(fotg210, "corrupt type %d frame %d shadow %p\n",
					type, frame, q.ptr);
			/* FALL THROUGH */
		case Q_TYPE_QH:
		case Q_TYPE_FSTN:
			/* End of the iTDs and siTDs */
			q.ptr = NULL;
			break;
		}

		/* assume completion callbacks modify the queue */
		if (unlikely(modified && fotg210->isoc_count > 0))
			return -EINVAL;
	}
	return 0;
}

static void scan_isoc(struct fotg210_hcd *fotg210)
{
	unsigned uf, now_frame, frame, ret;
	unsigned fmask = fotg210->periodic_size - 1;
	bool live;

	/*
	 * When running, scan from last scan point up to "now"
	 * else clean up by scanning everything that's left.
	 * Touches as few pages as possible:  cache-friendly.
	 */
	if (fotg210->rh_state >= FOTG210_RH_RUNNING) {
		uf = fotg210_read_frame_index(fotg210);
		now_frame = (uf >> 3) & fmask;
		live = true;
	} else  {
		now_frame = (fotg210->next_frame - 1) & fmask;
		live = false;
	}
	fotg210->now_frame = now_frame;

	frame = fotg210->next_frame;
	for (;;) {
		ret = 1;
		while (ret != 0)
			ret = scan_frame_queue(fotg210, frame,
					now_frame, live);

		/* Stop when we have reached the current frame */
		if (frame == now_frame)
			break;
		frame = (frame + 1) & fmask;
	}
	fotg210->next_frame = now_frame;
}

/* Display / Set uframe_periodic_max
 */
static ssize_t uframe_periodic_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fotg210_hcd *fotg210;
	int n;

	fotg210 = hcd_to_fotg210(bus_to_hcd(dev_get_drvdata(dev)));
	n = scnprintf(buf, PAGE_SIZE, "%d\n", fotg210->uframe_periodic_max);
	return n;
}


static ssize_t uframe_periodic_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fotg210_hcd *fotg210;
	unsigned uframe_periodic_max;
	unsigned frame, uframe;
	unsigned short allocated_max;
	unsigned long flags;
	ssize_t ret;

	fotg210 = hcd_to_fotg210(bus_to_hcd(dev_get_drvdata(dev)));
	if (kstrtouint(buf, 0, &uframe_periodic_max) < 0)
		return -EINVAL;

	if (uframe_periodic_max < 100 || uframe_periodic_max >= 125) {
		fotg210_info(fotg210, "rejecting invalid request for uframe_periodic_max=%u\n",
				uframe_periodic_max);
		return -EINVAL;
	}

	ret = -EINVAL;

	/*
	 * lock, so that our checking does not race with possible periodic
	 * bandwidth allocation through submitting new urbs.
	 */
	spin_lock_irqsave(&fotg210->lock, flags);

	/*
	 * for request to decrease max periodic bandwidth, we have to check
	 * every microframe in the schedule to see whether the decrease is
	 * possible.
	 */
	if (uframe_periodic_max < fotg210->uframe_periodic_max) {
		allocated_max = 0;

		for (frame = 0; frame < fotg210->periodic_size; ++frame)
			for (uframe = 0; uframe < 7; ++uframe)
				allocated_max = max(allocated_max,
						periodic_usecs(fotg210, frame,
						uframe));

		if (allocated_max > uframe_periodic_max) {
			fotg210_info(fotg210,
					"cannot decrease uframe_periodic_max because periodic bandwidth is already allocated (%u > %u)\n",
					allocated_max, uframe_periodic_max);
			goto out_unlock;
		}
	}

	/* increasing is always ok */

	fotg210_info(fotg210,
			"setting max periodic bandwidth to %u%% (== %u usec/uframe)\n",
			100 * uframe_periodic_max/125, uframe_periodic_max);

	if (uframe_periodic_max != 100)
		fotg210_warn(fotg210, "max periodic bandwidth set is non-standard\n");

	fotg210->uframe_periodic_max = uframe_periodic_max;
	ret = count;

out_unlock:
	spin_unlock_irqrestore(&fotg210->lock, flags);
	return ret;
}

static DEVICE_ATTR_RW(uframe_periodic_max);

static inline int create_sysfs_files(struct fotg210_hcd *fotg210)
{
	struct device *controller = fotg210_to_hcd(fotg210)->self.controller;

	return device_create_file(controller, &dev_attr_uframe_periodic_max);
}

static inline void remove_sysfs_files(struct fotg210_hcd *fotg210)
{
	struct device *controller = fotg210_to_hcd(fotg210)->self.controller;

	device_remove_file(controller, &dev_attr_uframe_periodic_max);
}
/* On some systems, leaving remote wakeup enabled prevents system shutdown.
 * The firmware seems to think that powering off is a wakeup event!
 * This routine turns off remote wakeup and everything else, on all ports.
 */
static void fotg210_turn_off_all_ports(struct fotg210_hcd *fotg210)
{
	u32 __iomem *status_reg = &fotg210->regs->port_status;

	fotg210_writel(fotg210, PORT_RWC_BITS, status_reg);
}

/* Halt HC, turn off all ports, and let the BIOS use the companion controllers.
 * Must be called with interrupts enabled and the lock not held.
 */
static void fotg210_silence_controller(struct fotg210_hcd *fotg210)
{
	fotg210_halt(fotg210);

	spin_lock_irq(&fotg210->lock);
	fotg210->rh_state = FOTG210_RH_HALTED;
	fotg210_turn_off_all_ports(fotg210);
	spin_unlock_irq(&fotg210->lock);
}

/* fotg210_shutdown kick in for silicon on any bus (not just pci, etc).
 * This forcibly disables dma and IRQs, helping kexec and other cases
 * where the next system software may expect clean state.
 */
static void fotg210_shutdown(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);

	spin_lock_irq(&fotg210->lock);
	fotg210->shutdown = true;
	fotg210->rh_state = FOTG210_RH_STOPPING;
	fotg210->enabled_hrtimer_events = 0;
	spin_unlock_irq(&fotg210->lock);

	fotg210_silence_controller(fotg210);

	hrtimer_cancel(&fotg210->hrtimer);
}

/* fotg210_work is called from some interrupts, timers, and so on.
 * it calls driver completion functions, after dropping fotg210->lock.
 */
static void fotg210_work(struct fotg210_hcd *fotg210)
{
	/* another CPU may drop fotg210->lock during a schedule scan while
	 * it reports urb completions.  this flag guards against bogus
	 * attempts at re-entrant schedule scanning.
	 */
	if (fotg210->scanning) {
		fotg210->need_rescan = true;
		return;
	}
	fotg210->scanning = true;

rescan:
	fotg210->need_rescan = false;
	if (fotg210->async_count)
		scan_async(fotg210);
	if (fotg210->intr_count > 0)
		scan_intr(fotg210);
	if (fotg210->isoc_count > 0)
		scan_isoc(fotg210);
	if (fotg210->need_rescan)
		goto rescan;
	fotg210->scanning = false;

	/* the IO watchdog guards against hardware or driver bugs that
	 * misplace IRQs, and should let us run completely without IRQs.
	 * such lossage has been observed on both VT6202 and VT8235.
	 */
	turn_on_io_watchdog(fotg210);
}

/* Called when the fotg210_hcd module is removed.
 */
static void fotg210_stop(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);

	fotg210_dbg(fotg210, "stop\n");

	/* no more interrupts ... */

	spin_lock_irq(&fotg210->lock);
	fotg210->enabled_hrtimer_events = 0;
	spin_unlock_irq(&fotg210->lock);

	fotg210_quiesce(fotg210);
	fotg210_silence_controller(fotg210);
	fotg210_reset(fotg210);

	hrtimer_cancel(&fotg210->hrtimer);
	remove_sysfs_files(fotg210);
	remove_debug_files(fotg210);

	/* root hub is shut down separately (first, when possible) */
	spin_lock_irq(&fotg210->lock);
	end_free_itds(fotg210);
	spin_unlock_irq(&fotg210->lock);
	fotg210_mem_cleanup(fotg210);

#ifdef FOTG210_STATS
	fotg210_dbg(fotg210, "irq normal %ld err %ld iaa %ld (lost %ld)\n",
			fotg210->stats.normal, fotg210->stats.error,
			fotg210->stats.iaa, fotg210->stats.lost_iaa);
	fotg210_dbg(fotg210, "complete %ld unlink %ld\n",
			fotg210->stats.complete, fotg210->stats.unlink);
#endif

	dbg_status(fotg210, "fotg210_stop completed",
			fotg210_readl(fotg210, &fotg210->regs->status));
}

/* one-time init, only for memory state */
static int hcd_fotg210_init(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	u32 temp;
	int retval;
	u32 hcc_params;
	struct fotg210_qh_hw *hw;

	spin_lock_init(&fotg210->lock);

	/*
	 * keep io watchdog by default, those good HCDs could turn off it later
	 */
	fotg210->need_io_watchdog = 1;

	hrtimer_init(&fotg210->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fotg210->hrtimer.function = fotg210_hrtimer_func;
	fotg210->next_hrtimer_event = FOTG210_HRTIMER_NO_EVENT;

	hcc_params = fotg210_readl(fotg210, &fotg210->caps->hcc_params);

	/*
	 * by default set standard 80% (== 100 usec/uframe) max periodic
	 * bandwidth as required by USB 2.0
	 */
	fotg210->uframe_periodic_max = 100;

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	fotg210->periodic_size = DEFAULT_I_TDPS;
	INIT_LIST_HEAD(&fotg210->intr_qh_list);
	INIT_LIST_HEAD(&fotg210->cached_itd_list);

	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		switch (FOTG210_TUNE_FLS) {
		case 0:
			fotg210->periodic_size = 1024;
			break;
		case 1:
			fotg210->periodic_size = 512;
			break;
		case 2:
			fotg210->periodic_size = 256;
			break;
		default:
			BUG();
		}
	}
	retval = fotg210_mem_init(fotg210, GFP_KERNEL);
	if (retval < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	fotg210->i_thresh = 2;

	/*
	 * dedicate a qh for the async ring head, since we couldn't unlink
	 * a 'real' qh without stopping the async schedule [4.8].  use it
	 * as the 'reclamation list head' too.
	 * its dummy is used in hw_alt_next of many tds, to prevent the qh
	 * from automatically advancing to the next td after short reads.
	 */
	fotg210->async->qh_next.qh = NULL;
	hw = fotg210->async->hw;
	hw->hw_next = QH_NEXT(fotg210, fotg210->async->qh_dma);
	hw->hw_info1 = cpu_to_hc32(fotg210, QH_HEAD);
	hw->hw_token = cpu_to_hc32(fotg210, QTD_STS_HALT);
	hw->hw_qtd_next = FOTG210_LIST_END(fotg210);
	fotg210->async->qh_state = QH_STATE_LINKED;
	hw->hw_alt_next = QTD_NEXT(fotg210, fotg210->async->dummy->qtd_dma);

	/* clear interrupt enables, set irq latency */
	if (log2_irq_thresh < 0 || log2_irq_thresh > 6)
		log2_irq_thresh = 0;
	temp = 1 << (16 + log2_irq_thresh);
	if (HCC_CANPARK(hcc_params)) {
		/* HW default park == 3, on hardware that supports it (like
		 * NVidia and ALI silicon), maximizes throughput on the async
		 * schedule by avoiding QH fetches between transfers.
		 *
		 * With fast usb storage devices and NForce2, "park" seems to
		 * make problems:  throughput reduction (!), data errors...
		 */
		if (park) {
			park = min_t(unsigned, park, 3);
			temp |= CMD_PARK;
			temp |= park << 8;
		}
		fotg210_dbg(fotg210, "park %d\n", park);
	}
	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		temp &= ~(3 << 2);
		temp |= (FOTG210_TUNE_FLS << 2);
	}
	fotg210->command = temp;

	/* Accept arbitrarily long scatter-gather lists */
	if (!hcd->localmem_pool)
		hcd->self.sg_tablesize = ~0;
	return 0;
}

/* start HC running; it's halted, hcd_fotg210_init() has been run (once) */
static int fotg210_run(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	u32 temp;
	u32 hcc_params;

	hcd->uses_new_polling = 1;

	/* EHCI spec section 4.1 */

	fotg210_writel(fotg210, fotg210->periodic_dma,
			&fotg210->regs->frame_list);
	fotg210_writel(fotg210, (u32)fotg210->async->qh_dma,
			&fotg210->regs->async_next);

	/*
	 * hcc_params controls whether fotg210->regs->segment must (!!!)
	 * be used; it constrains QH/ITD/SITD and QTD locations.
	 * dma_pool consistent memory always uses segment zero.
	 * streaming mappings for I/O buffers, like pci_map_single(),
	 * can return segments above 4GB, if the device allows.
	 *
	 * NOTE:  the dma mask is visible through dev->dma_mask, so
	 * drivers can pass this info along ... like NETIF_F_HIGHDMA,
	 * Scsi_Host.highmem_io, and so forth.  It's readonly to all
	 * host side drivers though.
	 */
	hcc_params = fotg210_readl(fotg210, &fotg210->caps->hcc_params);

	/*
	 * Philips, Intel, and maybe others need CMD_RUN before the
	 * root hub will detect new devices (why?); NEC doesn't
	 */
	fotg210->command &= ~(CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	fotg210->command |= CMD_RUN;
	fotg210_writel(fotg210, fotg210->command, &fotg210->regs->command);
	dbg_cmd(fotg210, "init", fotg210->command);

	/*
	 * Start, enabling full USB 2.0 functionality ... usb 1.1 devices
	 * are explicitly handed to companion controller(s), so no TT is
	 * involved with the root hub.  (Except where one is integrated,
	 * and there's no companion controller unless maybe for USB OTG.)
	 *
	 * Turning on the CF flag will transfer ownership of all ports
	 * from the companions to the EHCI controller.  If any of the
	 * companions are in the middle of a port reset at the time, it
	 * could cause trouble.  Write-locking ehci_cf_port_reset_rwsem
	 * guarantees that no resets are in progress.  After we set CF,
	 * a short delay lets the hardware catch up; new resets shouldn't
	 * be started before the port switching actions could complete.
	 */
	down_write(&ehci_cf_port_reset_rwsem);
	fotg210->rh_state = FOTG210_RH_RUNNING;
	/* unblock posted writes */
	fotg210_readl(fotg210, &fotg210->regs->command);
	usleep_range(5000, 10000);
	up_write(&ehci_cf_port_reset_rwsem);
	fotg210->last_periodic_enable = ktime_get_real();

	temp = HC_VERSION(fotg210,
			fotg210_readl(fotg210, &fotg210->caps->hc_capbase));
	fotg210_info(fotg210,
			"USB %x.%x started, EHCI %x.%02x\n",
			((fotg210->sbrn & 0xf0) >> 4), (fotg210->sbrn & 0x0f),
			temp >> 8, temp & 0xff);

	fotg210_writel(fotg210, INTR_MASK,
			&fotg210->regs->intr_enable); /* Turn On Interrupts */

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	create_debug_files(fotg210);
	create_sysfs_files(fotg210);

	return 0;
}

static int fotg210_setup(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	int retval;

	fotg210->regs = (void __iomem *)fotg210->caps +
			HC_LENGTH(fotg210,
			fotg210_readl(fotg210, &fotg210->caps->hc_capbase));
	dbg_hcs_params(fotg210, "reset");
	dbg_hcc_params(fotg210, "reset");

	/* cache this readonly data; minimize chip reads */
	fotg210->hcs_params = fotg210_readl(fotg210,
			&fotg210->caps->hcs_params);

	fotg210->sbrn = HCD_USB2;

	/* data structure init */
	retval = hcd_fotg210_init(hcd);
	if (retval)
		return retval;

	retval = fotg210_halt(fotg210);
	if (retval)
		return retval;

	fotg210_reset(fotg210);

	return 0;
}

static irqreturn_t fotg210_irq(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	u32 status, masked_status, pcd_status = 0, cmd;
	int bh;

	spin_lock(&fotg210->lock);

	status = fotg210_readl(fotg210, &fotg210->regs->status);

	/* e.g. cardbus physical eject */
	if (status == ~(u32) 0) {
		fotg210_dbg(fotg210, "device removed\n");
		goto dead;
	}

	/*
	 * We don't use STS_FLR, but some controllers don't like it to
	 * remain on, so mask it out along with the other status bits.
	 */
	masked_status = status & (INTR_MASK | STS_FLR);

	/* Shared IRQ? */
	if (!masked_status ||
			unlikely(fotg210->rh_state == FOTG210_RH_HALTED)) {
		spin_unlock(&fotg210->lock);
		return IRQ_NONE;
	}

	/* clear (just) interrupts */
	fotg210_writel(fotg210, masked_status, &fotg210->regs->status);
	cmd = fotg210_readl(fotg210, &fotg210->regs->command);
	bh = 0;

	/* unrequested/ignored: Frame List Rollover */
	dbg_status(fotg210, "irq", status);

	/* INT, ERR, and IAA interrupt rates can be throttled */

	/* normal [4.15.1.2] or error [4.15.1.1] completion */
	if (likely((status & (STS_INT|STS_ERR)) != 0)) {
		if (likely((status & STS_ERR) == 0))
			INCR(fotg210->stats.normal);
		else
			INCR(fotg210->stats.error);
		bh = 1;
	}

	/* complete the unlinking of some qh [4.15.2.3] */
	if (status & STS_IAA) {

		/* Turn off the IAA watchdog */
		fotg210->enabled_hrtimer_events &=
			~BIT(FOTG210_HRTIMER_IAA_WATCHDOG);

		/*
		 * Mild optimization: Allow another IAAD to reset the
		 * hrtimer, if one occurs before the next expiration.
		 * In theory we could always cancel the hrtimer, but
		 * tests show that about half the time it will be reset
		 * for some other event anyway.
		 */
		if (fotg210->next_hrtimer_event == FOTG210_HRTIMER_IAA_WATCHDOG)
			++fotg210->next_hrtimer_event;

		/* guard against (alleged) silicon errata */
		if (cmd & CMD_IAAD)
			fotg210_dbg(fotg210, "IAA with IAAD still set?\n");
		if (fotg210->async_iaa) {
			INCR(fotg210->stats.iaa);
			end_unlink_async(fotg210);
		} else
			fotg210_dbg(fotg210, "IAA with nothing unlinked?\n");
	}

	/* remote wakeup [4.3.1] */
	if (status & STS_PCD) {
		int pstatus;
		u32 __iomem *status_reg = &fotg210->regs->port_status;

		/* kick root hub later */
		pcd_status = status;

		/* resume root hub? */
		if (fotg210->rh_state == FOTG210_RH_SUSPENDED)
			usb_hcd_resume_root_hub(hcd);

		pstatus = fotg210_readl(fotg210, status_reg);

		if (test_bit(0, &fotg210->suspended_ports) &&
				((pstatus & PORT_RESUME) ||
				!(pstatus & PORT_SUSPEND)) &&
				(pstatus & PORT_PE) &&
				fotg210->reset_done[0] == 0) {

			/* start 20 msec resume signaling from this port,
			 * and make hub_wq collect PORT_STAT_C_SUSPEND to
			 * stop that signaling.  Use 5 ms extra for safety,
			 * like usb_port_resume() does.
			 */
			fotg210->reset_done[0] = jiffies + msecs_to_jiffies(25);
			set_bit(0, &fotg210->resuming_ports);
			fotg210_dbg(fotg210, "port 1 remote wakeup\n");
			mod_timer(&hcd->rh_timer, fotg210->reset_done[0]);
		}
	}

	/* PCI errors [4.15.2.4] */
	if (unlikely((status & STS_FATAL) != 0)) {
		fotg210_err(fotg210, "fatal error\n");
		dbg_cmd(fotg210, "fatal", cmd);
		dbg_status(fotg210, "fatal", status);
dead:
		usb_hc_died(hcd);

		/* Don't let the controller do anything more */
		fotg210->shutdown = true;
		fotg210->rh_state = FOTG210_RH_STOPPING;
		fotg210->command &= ~(CMD_RUN | CMD_ASE | CMD_PSE);
		fotg210_writel(fotg210, fotg210->command,
				&fotg210->regs->command);
		fotg210_writel(fotg210, 0, &fotg210->regs->intr_enable);
		fotg210_handle_controller_death(fotg210);

		/* Handle completions when the controller stops */
		bh = 0;
	}

	if (bh)
		fotg210_work(fotg210);
	spin_unlock(&fotg210->lock);
	if (pcd_status)
		usb_hcd_poll_rh_status(hcd);
	return IRQ_HANDLED;
}

/* non-error returns are a promise to giveback() the urb later
 * we drop ownership so next owner (or urb unlink) can get it
 *
 * urb + dev is in hcd.self.controller.urb_list
 * we're queueing TDs onto software and hardware lists
 *
 * hcd-specific init for hcpriv hasn't been done yet
 *
 * NOTE:  control, bulk, and interrupt share the same code to append TDs
 * to a (possibly active) QH, and the same QH scanning code.
 */
static int fotg210_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	struct list_head qtd_list;

	INIT_LIST_HEAD(&qtd_list);

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		/* qh_completions() code doesn't handle all the fault cases
		 * in multi-TD control transfers.  Even 1KB is rare anyway.
		 */
		if (urb->transfer_buffer_length > (16 * 1024))
			return -EMSGSIZE;
		/* FALLTHROUGH */
	/* case PIPE_BULK: */
	default:
		if (!qh_urb_transaction(fotg210, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return submit_async(fotg210, urb, &qtd_list, mem_flags);

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction(fotg210, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return intr_submit(fotg210, urb, &qtd_list, mem_flags);

	case PIPE_ISOCHRONOUS:
		return itd_submit(fotg210, urb, mem_flags);
	}
}

/* remove from hardware lists
 * completions normally happen asynchronously
 */

static int fotg210_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	struct fotg210_qh *qh;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&fotg210->lock, flags);
	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc)
		goto done;

	switch (usb_pipetype(urb->pipe)) {
	/* case PIPE_CONTROL: */
	/* case PIPE_BULK:*/
	default:
		qh = (struct fotg210_qh *) urb->hcpriv;
		if (!qh)
			break;
		switch (qh->qh_state) {
		case QH_STATE_LINKED:
		case QH_STATE_COMPLETING:
			start_unlink_async(fotg210, qh);
			break;
		case QH_STATE_UNLINK:
		case QH_STATE_UNLINK_WAIT:
			/* already started */
			break;
		case QH_STATE_IDLE:
			/* QH might be waiting for a Clear-TT-Buffer */
			qh_completions(fotg210, qh);
			break;
		}
		break;

	case PIPE_INTERRUPT:
		qh = (struct fotg210_qh *) urb->hcpriv;
		if (!qh)
			break;
		switch (qh->qh_state) {
		case QH_STATE_LINKED:
		case QH_STATE_COMPLETING:
			start_unlink_intr(fotg210, qh);
			break;
		case QH_STATE_IDLE:
			qh_completions(fotg210, qh);
			break;
		default:
			fotg210_dbg(fotg210, "bogus qh %p state %d\n",
					qh, qh->qh_state);
			goto done;
		}
		break;

	case PIPE_ISOCHRONOUS:
		/* itd... */

		/* wait till next completion, do it then. */
		/* completion irqs can wait up to 1024 msec, */
		break;
	}
done:
	spin_unlock_irqrestore(&fotg210->lock, flags);
	return rc;
}

/* bulk qh holds the data toggle */

static void fotg210_endpoint_disable(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	unsigned long flags;
	struct fotg210_qh *qh, *tmp;

	/* ASSERT:  any requests/urbs are being unlinked */
	/* ASSERT:  nobody can be submitting urbs for this any more */

rescan:
	spin_lock_irqsave(&fotg210->lock, flags);
	qh = ep->hcpriv;
	if (!qh)
		goto done;

	/* endpoints can be iso streams.  for now, we don't
	 * accelerate iso completions ... so spin a while.
	 */
	if (qh->hw == NULL) {
		struct fotg210_iso_stream *stream = ep->hcpriv;

		if (!list_empty(&stream->td_list))
			goto idle_timeout;

		/* BUG_ON(!list_empty(&stream->free_list)); */
		kfree(stream);
		goto done;
	}

	if (fotg210->rh_state < FOTG210_RH_RUNNING)
		qh->qh_state = QH_STATE_IDLE;
	switch (qh->qh_state) {
	case QH_STATE_LINKED:
	case QH_STATE_COMPLETING:
		for (tmp = fotg210->async->qh_next.qh;
				tmp && tmp != qh;
				tmp = tmp->qh_next.qh)
			continue;
		/* periodic qh self-unlinks on empty, and a COMPLETING qh
		 * may already be unlinked.
		 */
		if (tmp)
			start_unlink_async(fotg210, qh);
		/* FALL THROUGH */
	case QH_STATE_UNLINK:		/* wait for hw to finish? */
	case QH_STATE_UNLINK_WAIT:
idle_timeout:
		spin_unlock_irqrestore(&fotg210->lock, flags);
		schedule_timeout_uninterruptible(1);
		goto rescan;
	case QH_STATE_IDLE:		/* fully unlinked */
		if (qh->clearing_tt)
			goto idle_timeout;
		if (list_empty(&qh->qtd_list)) {
			qh_destroy(fotg210, qh);
			break;
		}
		/* fall through */
	default:
		/* caller was supposed to have unlinked any requests;
		 * that's not our job.  just leak this memory.
		 */
		fotg210_err(fotg210, "qh %p (#%02x) state %d%s\n",
				qh, ep->desc.bEndpointAddress, qh->qh_state,
				list_empty(&qh->qtd_list) ? "" : "(has tds)");
		break;
	}
done:
	ep->hcpriv = NULL;
	spin_unlock_irqrestore(&fotg210->lock, flags);
}

static void fotg210_endpoint_reset(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);
	struct fotg210_qh *qh;
	int eptype = usb_endpoint_type(&ep->desc);
	int epnum = usb_endpoint_num(&ep->desc);
	int is_out = usb_endpoint_dir_out(&ep->desc);
	unsigned long flags;

	if (eptype != USB_ENDPOINT_XFER_BULK && eptype != USB_ENDPOINT_XFER_INT)
		return;

	spin_lock_irqsave(&fotg210->lock, flags);
	qh = ep->hcpriv;

	/* For Bulk and Interrupt endpoints we maintain the toggle state
	 * in the hardware; the toggle bits in udev aren't used at all.
	 * When an endpoint is reset by usb_clear_halt() we must reset
	 * the toggle bit in the QH.
	 */
	if (qh) {
		usb_settoggle(qh->dev, epnum, is_out, 0);
		if (!list_empty(&qh->qtd_list)) {
			WARN_ONCE(1, "clear_halt for a busy endpoint\n");
		} else if (qh->qh_state == QH_STATE_LINKED ||
				qh->qh_state == QH_STATE_COMPLETING) {

			/* The toggle value in the QH can't be updated
			 * while the QH is active.  Unlink it now;
			 * re-linking will call qh_refresh().
			 */
			if (eptype == USB_ENDPOINT_XFER_BULK)
				start_unlink_async(fotg210, qh);
			else
				start_unlink_intr(fotg210, qh);
		}
	}
	spin_unlock_irqrestore(&fotg210->lock, flags);
}

static int fotg210_get_frame(struct usb_hcd *hcd)
{
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);

	return (fotg210_read_frame_index(fotg210) >> 3) %
		fotg210->periodic_size;
}

/* The EHCI in ChipIdea HDRC cannot be a separate module or device,
 * because its registers (and irq) are shared between host/gadget/otg
 * functions  and in order to facilitate role switching we cannot
 * give the fotg210 driver exclusive access to those.
 */
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");

static const struct hc_driver fotg210_fotg210_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Faraday USB2.0 Host Controller",
	.hcd_priv_size		= sizeof(struct fotg210_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= fotg210_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset			= hcd_fotg210_init,
	.start			= fotg210_run,
	.stop			= fotg210_stop,
	.shutdown		= fotg210_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= fotg210_urb_enqueue,
	.urb_dequeue		= fotg210_urb_dequeue,
	.endpoint_disable	= fotg210_endpoint_disable,
	.endpoint_reset		= fotg210_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number	= fotg210_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= fotg210_hub_status_data,
	.hub_control		= fotg210_hub_control,
	.bus_suspend		= fotg210_bus_suspend,
	.bus_resume		= fotg210_bus_resume,

	.relinquish_port	= fotg210_relinquish_port,
	.port_handed_over	= fotg210_port_handed_over,

	.clear_tt_buffer_complete = fotg210_clear_tt_buffer_complete,
};

static void fotg210_init(struct fotg210_hcd *fotg210)
{
	u32 value;

	iowrite32(GMIR_MDEV_INT | GMIR_MOTG_INT | GMIR_INT_POLARITY,
			&fotg210->regs->gmir);

	value = ioread32(&fotg210->regs->otgcsr);
	value &= ~OTGCSR_A_BUS_DROP;
	value |= OTGCSR_A_BUS_REQ;
	iowrite32(value, &fotg210->regs->otgcsr);
}

/**
 * fotg210_hcd_probe - initialize faraday FOTG210 HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int fotg210_hcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval = -ENODEV;
	struct fotg210_hcd *fotg210;

	if (usb_disabled())
		return -ENODEV;

	pdev->dev.power.power_state = PMSG_ON;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "Found HC with no IRQ. Check %s setup!\n",
				dev_name(dev));
		return -ENODEV;
	}

	irq = res->start;

	hcd = usb_create_hcd(&fotg210_fotg210_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "failed to create hcd with err %d\n", retval);
		retval = -ENOMEM;
		goto fail_create_hcd;
	}

	hcd->has_tt = 1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		retval = PTR_ERR(hcd->regs);
		goto failed_put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	fotg210 = hcd_to_fotg210(hcd);

	fotg210->caps = hcd->regs;

	/* It's OK not to supply this clock */
	fotg210->pclk = clk_get(dev, "PCLK");
	if (!IS_ERR(fotg210->pclk)) {
		retval = clk_prepare_enable(fotg210->pclk);
		if (retval) {
			dev_err(dev, "failed to enable PCLK\n");
			goto failed_put_hcd;
		}
	} else if (PTR_ERR(fotg210->pclk) == -EPROBE_DEFER) {
		/*
		 * Percolate deferrals, for anything else,
		 * just live without the clocking.
		 */
		retval = PTR_ERR(fotg210->pclk);
		goto failed_dis_clk;
	}

	retval = fotg210_setup(hcd);
	if (retval)
		goto failed_dis_clk;

	fotg210_init(fotg210);

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval) {
		dev_err(dev, "failed to add hcd with err %d\n", retval);
		goto failed_dis_clk;
	}
	device_wakeup_enable(hcd->self.controller);
	platform_set_drvdata(pdev, hcd);

	return retval;

failed_dis_clk:
	if (!IS_ERR(fotg210->pclk))
		clk_disable_unprepare(fotg210->pclk);
failed_put_hcd:
	usb_put_hcd(hcd);
fail_create_hcd:
	dev_err(dev, "init %s fail, %d\n", dev_name(dev), retval);
	return retval;
}

/**
 * fotg210_hcd_remove - shutdown processing for EHCI HCDs
 * @dev: USB Host Controller being removed
 *
 */
static int fotg210_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct fotg210_hcd *fotg210 = hcd_to_fotg210(hcd);

	if (!IS_ERR(fotg210->pclk))
		clk_disable_unprepare(fotg210->pclk);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id fotg210_of_match[] = {
	{ .compatible = "faraday,fotg210" },
	{},
};
MODULE_DEVICE_TABLE(of, fotg210_of_match);
#endif

static struct platform_driver fotg210_hcd_driver = {
	.driver = {
		.name   = "fotg210-hcd",
		.of_match_table = of_match_ptr(fotg210_of_match),
	},
	.probe  = fotg210_hcd_probe,
	.remove = fotg210_hcd_remove,
};

static int __init fotg210_hcd_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	set_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	if (test_bit(USB_UHCI_LOADED, &usb_hcds_loaded) ||
			test_bit(USB_OHCI_LOADED, &usb_hcds_loaded))
		pr_warn("Warning! fotg210_hcd should always be loaded before uhci_hcd and ohci_hcd, not after\n");

	pr_debug("%s: block sizes: qh %zd qtd %zd itd %zd\n",
			hcd_name, sizeof(struct fotg210_qh),
			sizeof(struct fotg210_qtd),
			sizeof(struct fotg210_itd));

	fotg210_debug_root = debugfs_create_dir("fotg210", usb_debug_root);

	retval = platform_driver_register(&fotg210_hcd_driver);
	if (retval < 0)
		goto clean;
	return retval;

clean:
	debugfs_remove(fotg210_debug_root);
	fotg210_debug_root = NULL;

	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	return retval;
}
module_init(fotg210_hcd_init);

static void __exit fotg210_hcd_cleanup(void)
{
	platform_driver_unregister(&fotg210_hcd_driver);
	debugfs_remove(fotg210_debug_root);
	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
}
module_exit(fotg210_hcd_cleanup);
