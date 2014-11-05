/*
 * Faraday FUSBH200 EHCI-like driver
 *
 * Copyright (c) 2013 Faraday Technology Corporation
 *
 * Author: Yuan-Hsin Chen <yhchen@faraday-tech.com>
 * 	   Feng-Hsin Chiang <john453@faraday-tech.com>
 * 	   Po-Yu Chuang <ratbert.chuang@gmail.com>
 *
 * Most of code borrowed from the Linux-3.7 EHCI driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
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

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

/*-------------------------------------------------------------------------*/
#define DRIVER_AUTHOR "Yuan-Hsin Chen"
#define DRIVER_DESC "FUSBH200 Host Controller (EHCI) Driver"

static const char	hcd_name [] = "fusbh200_hcd";

#undef FUSBH200_URB_TRACE

/* magic numbers that can affect system performance */
#define	FUSBH200_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define	FUSBH200_TUNE_RL_HS		4	/* nak throttle; see 4.9 */
#define	FUSBH200_TUNE_RL_TT		0
#define	FUSBH200_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define	FUSBH200_TUNE_MULT_TT	1
/*
 * Some drivers think it's safe to schedule isochronous transfers more than
 * 256 ms into the future (partly as a result of an old bug in the scheduling
 * code).  In an attempt to avoid trouble, we will use a minimum scheduling
 * length of 512 frames instead of 256.
 */
#define	FUSBH200_TUNE_FLS		1	/* (medium) 512-frame schedule */

/* Initial IRQ latency:  faster than hw default */
static int log2_irq_thresh = 0;		// 0 to 6
module_param (log2_irq_thresh, int, S_IRUGO);
MODULE_PARM_DESC (log2_irq_thresh, "log2 IRQ latency, 1-64 microframes");

/* initial park setting:  slower than hw default */
static unsigned park = 0;
module_param (park, uint, S_IRUGO);
MODULE_PARM_DESC (park, "park setting; 1-3 back-to-back async packets");

/* for link power management(LPM) feature */
static unsigned int hird;
module_param(hird, int, S_IRUGO);
MODULE_PARM_DESC(hird, "host initiated resume duration, +1 for each 75us");

#define	INTR_MASK (STS_IAA | STS_FATAL | STS_PCD | STS_ERR | STS_INT)

#include "fusbh200.h"

/*-------------------------------------------------------------------------*/

#define fusbh200_dbg(fusbh200, fmt, args...) \
	dev_dbg (fusbh200_to_hcd(fusbh200)->self.controller , fmt , ## args )
#define fusbh200_err(fusbh200, fmt, args...) \
	dev_err (fusbh200_to_hcd(fusbh200)->self.controller , fmt , ## args )
#define fusbh200_info(fusbh200, fmt, args...) \
	dev_info (fusbh200_to_hcd(fusbh200)->self.controller , fmt , ## args )
#define fusbh200_warn(fusbh200, fmt, args...) \
	dev_warn (fusbh200_to_hcd(fusbh200)->self.controller , fmt , ## args )

/* check the values in the HCSPARAMS register
 * (host controller _Structural_ parameters)
 * see EHCI spec, Table 2-4 for each value
 */
static void dbg_hcs_params (struct fusbh200_hcd *fusbh200, char *label)
{
	u32	params = fusbh200_readl(fusbh200, &fusbh200->caps->hcs_params);

	fusbh200_dbg (fusbh200,
		"%s hcs_params 0x%x ports=%d\n",
		label, params,
		HCS_N_PORTS (params)
		);
}

/* check the values in the HCCPARAMS register
 * (host controller _Capability_ parameters)
 * see EHCI Spec, Table 2-5 for each value
 * */
static void dbg_hcc_params (struct fusbh200_hcd *fusbh200, char *label)
{
	u32	params = fusbh200_readl(fusbh200, &fusbh200->caps->hcc_params);

	fusbh200_dbg (fusbh200,
		"%s hcc_params %04x uframes %s%s\n",
		label,
		params,
		HCC_PGM_FRAMELISTLEN(params) ? "256/512/1024" : "1024",
		HCC_CANPARK(params) ? " park" : "");
}

static void __maybe_unused
dbg_qtd (const char *label, struct fusbh200_hcd *fusbh200, struct fusbh200_qtd *qtd)
{
	fusbh200_dbg(fusbh200, "%s td %p n%08x %08x t%08x p0=%08x\n", label, qtd,
		hc32_to_cpup(fusbh200, &qtd->hw_next),
		hc32_to_cpup(fusbh200, &qtd->hw_alt_next),
		hc32_to_cpup(fusbh200, &qtd->hw_token),
		hc32_to_cpup(fusbh200, &qtd->hw_buf [0]));
	if (qtd->hw_buf [1])
		fusbh200_dbg(fusbh200, "  p1=%08x p2=%08x p3=%08x p4=%08x\n",
			hc32_to_cpup(fusbh200, &qtd->hw_buf[1]),
			hc32_to_cpup(fusbh200, &qtd->hw_buf[2]),
			hc32_to_cpup(fusbh200, &qtd->hw_buf[3]),
			hc32_to_cpup(fusbh200, &qtd->hw_buf[4]));
}

static void __maybe_unused
dbg_qh (const char *label, struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	struct fusbh200_qh_hw *hw = qh->hw;

	fusbh200_dbg (fusbh200, "%s qh %p n%08x info %x %x qtd %x\n", label,
		qh, hw->hw_next, hw->hw_info1, hw->hw_info2, hw->hw_current);
	dbg_qtd("overlay", fusbh200, (struct fusbh200_qtd *) &hw->hw_qtd_next);
}

static void __maybe_unused
dbg_itd (const char *label, struct fusbh200_hcd *fusbh200, struct fusbh200_itd *itd)
{
	fusbh200_dbg (fusbh200, "%s [%d] itd %p, next %08x, urb %p\n",
		label, itd->frame, itd, hc32_to_cpu(fusbh200, itd->hw_next),
		itd->urb);
	fusbh200_dbg (fusbh200,
		"  trans: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		hc32_to_cpu(fusbh200, itd->hw_transaction[0]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[1]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[2]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[3]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[4]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[5]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[6]),
		hc32_to_cpu(fusbh200, itd->hw_transaction[7]));
	fusbh200_dbg (fusbh200,
		"  buf:   %08x %08x %08x %08x %08x %08x %08x\n",
		hc32_to_cpu(fusbh200, itd->hw_bufp[0]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[1]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[2]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[3]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[4]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[5]),
		hc32_to_cpu(fusbh200, itd->hw_bufp[6]));
	fusbh200_dbg (fusbh200, "  index: %d %d %d %d %d %d %d %d\n",
		itd->index[0], itd->index[1], itd->index[2],
		itd->index[3], itd->index[4], itd->index[5],
		itd->index[6], itd->index[7]);
}

static int __maybe_unused
dbg_status_buf (char *buf, unsigned len, const char *label, u32 status)
{
	return scnprintf (buf, len,
		"%s%sstatus %04x%s%s%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", status,
		(status & STS_ASS) ? " Async" : "",
		(status & STS_PSS) ? " Periodic" : "",
		(status & STS_RECL) ? " Recl" : "",
		(status & STS_HALT) ? " Halt" : "",
		(status & STS_IAA) ? " IAA" : "",
		(status & STS_FATAL) ? " FATAL" : "",
		(status & STS_FLR) ? " FLR" : "",
		(status & STS_PCD) ? " PCD" : "",
		(status & STS_ERR) ? " ERR" : "",
		(status & STS_INT) ? " INT" : ""
		);
}

static int __maybe_unused
dbg_intr_buf (char *buf, unsigned len, const char *label, u32 enable)
{
	return scnprintf (buf, len,
		"%s%sintrenable %02x%s%s%s%s%s%s",
		label, label [0] ? " " : "", enable,
		(enable & STS_IAA) ? " IAA" : "",
		(enable & STS_FATAL) ? " FATAL" : "",
		(enable & STS_FLR) ? " FLR" : "",
		(enable & STS_PCD) ? " PCD" : "",
		(enable & STS_ERR) ? " ERR" : "",
		(enable & STS_INT) ? " INT" : ""
		);
}

static const char *const fls_strings [] =
    { "1024", "512", "256", "??" };

static int
dbg_command_buf (char *buf, unsigned len, const char *label, u32 command)
{
	return scnprintf (buf, len,
		"%s%scommand %07x %s=%d ithresh=%d%s%s%s "
		"period=%s%s %s",
		label, label [0] ? " " : "", command,
		(command & CMD_PARK) ? " park" : "(park)",
		CMD_PARK_CNT (command),
		(command >> 16) & 0x3f,
		(command & CMD_IAAD) ? " IAAD" : "",
		(command & CMD_ASE) ? " Async" : "",
		(command & CMD_PSE) ? " Periodic" : "",
		fls_strings [(command >> 2) & 0x3],
		(command & CMD_RESET) ? " Reset" : "",
		(command & CMD_RUN) ? "RUN" : "HALT"
		);
}

static int
dbg_port_buf (char *buf, unsigned len, const char *label, int port, u32 status)
{
	char	*sig;

	/* signaling state */
	switch (status & (3 << 10)) {
	case 0 << 10: sig = "se0"; break;
	case 1 << 10: sig = "k"; break;		/* low speed */
	case 2 << 10: sig = "j"; break;
	default: sig = "?"; break;
	}

	return scnprintf (buf, len,
		"%s%sport:%d status %06x %d "
		"sig=%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", port, status,
		status>>25,/*device address */
		sig,
		(status & PORT_RESET) ? " RESET" : "",
		(status & PORT_SUSPEND) ? " SUSPEND" : "",
		(status & PORT_RESUME) ? " RESUME" : "",
		(status & PORT_PEC) ? " PEC" : "",
		(status & PORT_PE) ? " PE" : "",
		(status & PORT_CSC) ? " CSC" : "",
		(status & PORT_CONNECT) ? " CONNECT" : "");
}

/* functions have the "wrong" filename when they're output... */
#define dbg_status(fusbh200, label, status) { \
	char _buf [80]; \
	dbg_status_buf (_buf, sizeof _buf, label, status); \
	fusbh200_dbg (fusbh200, "%s\n", _buf); \
}

#define dbg_cmd(fusbh200, label, command) { \
	char _buf [80]; \
	dbg_command_buf (_buf, sizeof _buf, label, command); \
	fusbh200_dbg (fusbh200, "%s\n", _buf); \
}

#define dbg_port(fusbh200, label, port, status) { \
	char _buf [80]; \
	dbg_port_buf (_buf, sizeof _buf, label, port, status); \
	fusbh200_dbg (fusbh200, "%s\n", _buf); \
}

/*-------------------------------------------------------------------------*/

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

static struct dentry *fusbh200_debug_root;

struct debug_buffer {
	ssize_t (*fill_func)(struct debug_buffer *);	/* fill method */
	struct usb_bus *bus;
	struct mutex mutex;	/* protect filling of buffer */
	size_t count;		/* number of characters filled into buffer */
	char *output_buf;
	size_t alloc_size;
};

#define speed_char(info1) ({ char tmp; \
		switch (info1 & (3 << 12)) { \
		case QH_FULL_SPEED: tmp = 'f'; break; \
		case QH_LOW_SPEED:  tmp = 'l'; break; \
		case QH_HIGH_SPEED: tmp = 'h'; break; \
		default: tmp = '?'; break; \
		} tmp; })

static inline char token_mark(struct fusbh200_hcd *fusbh200, __hc32 token)
{
	__u32 v = hc32_to_cpu(fusbh200, token);

	if (v & QTD_STS_ACTIVE)
		return '*';
	if (v & QTD_STS_HALT)
		return '-';
	if (!IS_SHORT_READ (v))
		return ' ';
	/* tries to advance through hw_alt_next */
	return '/';
}

static void qh_lines (
	struct fusbh200_hcd *fusbh200,
	struct fusbh200_qh *qh,
	char **nextp,
	unsigned *sizep
)
{
	u32			scratch;
	u32			hw_curr;
	struct fusbh200_qtd		*td;
	unsigned		temp;
	unsigned		size = *sizep;
	char			*next = *nextp;
	char			mark;
	__le32			list_end = FUSBH200_LIST_END(fusbh200);
	struct fusbh200_qh_hw	*hw = qh->hw;

	if (hw->hw_qtd_next == list_end)	/* NEC does this */
		mark = '@';
	else
		mark = token_mark(fusbh200, hw->hw_token);
	if (mark == '/') {	/* qh_alt_next controls qh advance? */
		if ((hw->hw_alt_next & QTD_MASK(fusbh200))
				== fusbh200->async->hw->hw_alt_next)
			mark = '#';	/* blocked */
		else if (hw->hw_alt_next == list_end)
			mark = '.';	/* use hw_qtd_next */
		/* else alt_next points to some other qtd */
	}
	scratch = hc32_to_cpup(fusbh200, &hw->hw_info1);
	hw_curr = (mark == '*') ? hc32_to_cpup(fusbh200, &hw->hw_current) : 0;
	temp = scnprintf (next, size,
			"qh/%p dev%d %cs ep%d %08x %08x (%08x%c %s nak%d)",
			qh, scratch & 0x007f,
			speed_char (scratch),
			(scratch >> 8) & 0x000f,
			scratch, hc32_to_cpup(fusbh200, &hw->hw_info2),
			hc32_to_cpup(fusbh200, &hw->hw_token), mark,
			(cpu_to_hc32(fusbh200, QTD_TOGGLE) & hw->hw_token)
				? "data1" : "data0",
			(hc32_to_cpup(fusbh200, &hw->hw_alt_next) >> 1) & 0x0f);
	size -= temp;
	next += temp;

	/* hc may be modifying the list as we read it ... */
	list_for_each_entry(td, &qh->qtd_list, qtd_list) {
		scratch = hc32_to_cpup(fusbh200, &td->hw_token);
		mark = ' ';
		if (hw_curr == td->qtd_dma)
			mark = '*';
		else if (hw->hw_qtd_next == cpu_to_hc32(fusbh200, td->qtd_dma))
			mark = '+';
		else if (QTD_LENGTH (scratch)) {
			if (td->hw_alt_next == fusbh200->async->hw->hw_alt_next)
				mark = '#';
			else if (td->hw_alt_next != list_end)
				mark = '/';
		}
		temp = snprintf (next, size,
				"\n\t%p%c%s len=%d %08x urb %p",
				td, mark, ({ char *tmp;
				 switch ((scratch>>8)&0x03) {
				 case 0: tmp = "out"; break;
				 case 1: tmp = "in"; break;
				 case 2: tmp = "setup"; break;
				 default: tmp = "?"; break;
				 } tmp;}),
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

	temp = snprintf (next, size, "\n");
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
	struct usb_hcd		*hcd;
	struct fusbh200_hcd	*fusbh200;
	unsigned long		flags;
	unsigned		temp, size;
	char			*next;
	struct fusbh200_qh		*qh;

	hcd = bus_to_hcd(buf->bus);
	fusbh200 = hcd_to_fusbh200 (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	*next = 0;

	/* dumps a snapshot of the async schedule.
	 * usually empty except for long-term bulk reads, or head.
	 * one QH per line, and TDs we know about
	 */
	spin_lock_irqsave (&fusbh200->lock, flags);
	for (qh = fusbh200->async->qh_next.qh; size > 0 && qh; qh = qh->qh_next.qh)
		qh_lines (fusbh200, qh, &next, &size);
	if (fusbh200->async_unlink && size > 0) {
		temp = scnprintf(next, size, "\nunlink =\n");
		size -= temp;
		next += temp;

		for (qh = fusbh200->async_unlink; size > 0 && qh;
				qh = qh->unlink_next)
			qh_lines (fusbh200, qh, &next, &size);
	}
	spin_unlock_irqrestore (&fusbh200->lock, flags);

	return strlen(buf->output_buf);
}

#define DBG_SCHED_LIMIT 64
static ssize_t fill_periodic_buffer(struct debug_buffer *buf)
{
	struct usb_hcd		*hcd;
	struct fusbh200_hcd		*fusbh200;
	unsigned long		flags;
	union fusbh200_shadow	p, *seen;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i;
	__hc32			tag;

	if (!(seen = kmalloc (DBG_SCHED_LIMIT * sizeof *seen, GFP_ATOMIC)))
		return 0;
	seen_count = 0;

	hcd = bus_to_hcd(buf->bus);
	fusbh200 = hcd_to_fusbh200 (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	temp = scnprintf (next, size, "size = %d\n", fusbh200->periodic_size);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule.
	 * iso changes, interrupt usually doesn't.
	 */
	spin_lock_irqsave (&fusbh200->lock, flags);
	for (i = 0; i < fusbh200->periodic_size; i++) {
		p = fusbh200->pshadow [i];
		if (likely (!p.ptr))
			continue;
		tag = Q_NEXT_TYPE(fusbh200, fusbh200->periodic [i]);

		temp = scnprintf (next, size, "%4d: ", i);
		size -= temp;
		next += temp;

		do {
			struct fusbh200_qh_hw *hw;

			switch (hc32_to_cpu(fusbh200, tag)) {
			case Q_TYPE_QH:
				hw = p.qh->hw;
				temp = scnprintf (next, size, " qh%d-%04x/%p",
						p.qh->period,
						hc32_to_cpup(fusbh200,
							&hw->hw_info2)
							/* uframe masks */
							& (QH_CMASK | QH_SMASK),
						p.qh);
				size -= temp;
				next += temp;
				/* don't repeat what follows this qh */
				for (temp = 0; temp < seen_count; temp++) {
					if (seen [temp].ptr != p.ptr)
						continue;
					if (p.qh->qh_next.ptr) {
						temp = scnprintf (next, size,
							" ...");
						size -= temp;
						next += temp;
					}
					break;
				}
				/* show more info the first time around */
				if (temp == seen_count) {
					u32	scratch = hc32_to_cpup(fusbh200,
							&hw->hw_info1);
					struct fusbh200_qtd	*qtd;
					char		*type = "";

					/* count tds, get ep direction */
					temp = 0;
					list_for_each_entry (qtd,
							&p.qh->qtd_list,
							qtd_list) {
						temp++;
						switch (0x03 & (hc32_to_cpu(
							fusbh200,
							qtd->hw_token) >> 8)) {
						case 0: type = "out"; continue;
						case 1: type = "in"; continue;
						}
					}

					temp = scnprintf (next, size,
						" (%c%d ep%d%s "
						"[%d/%d] q%d p%d)",
						speed_char (scratch),
						scratch & 0x007f,
						(scratch >> 8) & 0x000f, type,
						p.qh->usecs, p.qh->c_usecs,
						temp,
						0x7ff & (scratch >> 16));

					if (seen_count < DBG_SCHED_LIMIT)
						seen [seen_count++].qh = p.qh;
				} else
					temp = 0;
				tag = Q_NEXT_TYPE(fusbh200, hw->hw_next);
				p = p.qh->qh_next;
				break;
			case Q_TYPE_FSTN:
				temp = scnprintf (next, size,
					" fstn-%8x/%p", p.fstn->hw_prev,
					p.fstn);
				tag = Q_NEXT_TYPE(fusbh200, p.fstn->hw_next);
				p = p.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				temp = scnprintf (next, size,
					" itd/%p", p.itd);
				tag = Q_NEXT_TYPE(fusbh200, p.itd->hw_next);
				p = p.itd->itd_next;
				break;
			}
			size -= temp;
			next += temp;
		} while (p.ptr);

		temp = scnprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	kfree (seen);

	return buf->alloc_size - size;
}
#undef DBG_SCHED_LIMIT

static const char *rh_state_string(struct fusbh200_hcd *fusbh200)
{
	switch (fusbh200->rh_state) {
	case FUSBH200_RH_HALTED:
		return "halted";
	case FUSBH200_RH_SUSPENDED:
		return "suspended";
	case FUSBH200_RH_RUNNING:
		return "running";
	case FUSBH200_RH_STOPPING:
		return "stopping";
	}
	return "?";
}

static ssize_t fill_registers_buffer(struct debug_buffer *buf)
{
	struct usb_hcd		*hcd;
	struct fusbh200_hcd	*fusbh200;
	unsigned long		flags;
	unsigned		temp, size, i;
	char			*next, scratch [80];
	static char		fmt [] = "%*s\n";
	static char		label [] = "";

	hcd = bus_to_hcd(buf->bus);
	fusbh200 = hcd_to_fusbh200 (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	spin_lock_irqsave (&fusbh200->lock, flags);

	if (!HCD_HW_ACCESSIBLE(hcd)) {
		size = scnprintf (next, size,
			"bus %s, device %s\n"
			"%s\n"
			"SUSPENDED (no register access)\n",
			hcd->self.controller->bus->name,
			dev_name(hcd->self.controller),
			hcd->product_desc);
		goto done;
	}

	/* Capability Registers */
	i = HC_VERSION(fusbh200, fusbh200_readl(fusbh200, &fusbh200->caps->hc_capbase));
	temp = scnprintf (next, size,
		"bus %s, device %s\n"
		"%s\n"
		"EHCI %x.%02x, rh state %s\n",
		hcd->self.controller->bus->name,
		dev_name(hcd->self.controller),
		hcd->product_desc,
		i >> 8, i & 0x0ff, rh_state_string(fusbh200));
	size -= temp;
	next += temp;

	// FIXME interpret both types of params
	i = fusbh200_readl(fusbh200, &fusbh200->caps->hcs_params);
	temp = scnprintf (next, size, "structural params 0x%08x\n", i);
	size -= temp;
	next += temp;

	i = fusbh200_readl(fusbh200, &fusbh200->caps->hcc_params);
	temp = scnprintf (next, size, "capability params 0x%08x\n", i);
	size -= temp;
	next += temp;

	/* Operational Registers */
	temp = dbg_status_buf (scratch, sizeof scratch, label,
			fusbh200_readl(fusbh200, &fusbh200->regs->status));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_command_buf (scratch, sizeof scratch, label,
			fusbh200_readl(fusbh200, &fusbh200->regs->command));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_intr_buf (scratch, sizeof scratch, label,
			fusbh200_readl(fusbh200, &fusbh200->regs->intr_enable));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = scnprintf (next, size, "uframe %04x\n",
			fusbh200_read_frame_index(fusbh200));
	size -= temp;
	next += temp;

	if (fusbh200->async_unlink) {
		temp = scnprintf(next, size, "async unlink qh %p\n",
				fusbh200->async_unlink);
		size -= temp;
		next += temp;
	}

	temp = scnprintf (next, size,
		"irq normal %ld err %ld iaa %ld (lost %ld)\n",
		fusbh200->stats.normal, fusbh200->stats.error, fusbh200->stats.iaa,
		fusbh200->stats.lost_iaa);
	size -= temp;
	next += temp;

	temp = scnprintf (next, size, "complete %ld unlink %ld\n",
		fusbh200->stats.complete, fusbh200->stats.unlink);
	size -= temp;
	next += temp;

done:
	spin_unlock_irqrestore (&fusbh200->lock, flags);

	return buf->alloc_size - size;
}

static struct debug_buffer *alloc_buffer(struct usb_bus *bus,
				ssize_t (*fill_func)(struct debug_buffer *))
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

static inline void create_debug_files (struct fusbh200_hcd *fusbh200)
{
	struct usb_bus *bus = &fusbh200_to_hcd(fusbh200)->self;

	fusbh200->debug_dir = debugfs_create_dir(bus->bus_name, fusbh200_debug_root);
	if (!fusbh200->debug_dir)
		return;

	if (!debugfs_create_file("async", S_IRUGO, fusbh200->debug_dir, bus,
						&debug_async_fops))
		goto file_error;

	if (!debugfs_create_file("periodic", S_IRUGO, fusbh200->debug_dir, bus,
						&debug_periodic_fops))
		goto file_error;

	if (!debugfs_create_file("registers", S_IRUGO, fusbh200->debug_dir, bus,
						    &debug_registers_fops))
		goto file_error;

	return;

file_error:
	debugfs_remove_recursive(fusbh200->debug_dir);
}

static inline void remove_debug_files (struct fusbh200_hcd *fusbh200)
{
	debugfs_remove_recursive(fusbh200->debug_dir);
}

/*-------------------------------------------------------------------------*/

/*
 * handshake - spin reading hc until handshake completes or fails
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
static int handshake (struct fusbh200_hcd *fusbh200, void __iomem *ptr,
		      u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = fusbh200_readl(fusbh200, ptr);
		if (result == ~(u32)0)		/* card removed */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay (1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

/*
 * Force HC to halt state from unknown (EHCI spec section 2.3).
 * Must be called with interrupts enabled and the lock not held.
 */
static int fusbh200_halt (struct fusbh200_hcd *fusbh200)
{
	u32	temp;

	spin_lock_irq(&fusbh200->lock);

	/* disable any irqs left enabled by previous code */
	fusbh200_writel(fusbh200, 0, &fusbh200->regs->intr_enable);

	/*
	 * This routine gets called during probe before fusbh200->command
	 * has been initialized, so we can't rely on its value.
	 */
	fusbh200->command &= ~CMD_RUN;
	temp = fusbh200_readl(fusbh200, &fusbh200->regs->command);
	temp &= ~(CMD_RUN | CMD_IAAD);
	fusbh200_writel(fusbh200, temp, &fusbh200->regs->command);

	spin_unlock_irq(&fusbh200->lock);
	synchronize_irq(fusbh200_to_hcd(fusbh200)->irq);

	return handshake(fusbh200, &fusbh200->regs->status,
			  STS_HALT, STS_HALT, 16 * 125);
}

/*
 * Reset a non-running (STS_HALT == 1) controller.
 * Must be called with interrupts enabled and the lock not held.
 */
static int fusbh200_reset (struct fusbh200_hcd *fusbh200)
{
	int	retval;
	u32	command = fusbh200_readl(fusbh200, &fusbh200->regs->command);

	/* If the EHCI debug controller is active, special care must be
	 * taken before and after a host controller reset */
	if (fusbh200->debug && !dbgp_reset_prep(fusbh200_to_hcd(fusbh200)))
		fusbh200->debug = NULL;

	command |= CMD_RESET;
	dbg_cmd (fusbh200, "reset", command);
	fusbh200_writel(fusbh200, command, &fusbh200->regs->command);
	fusbh200->rh_state = FUSBH200_RH_HALTED;
	fusbh200->next_statechange = jiffies;
	retval = handshake (fusbh200, &fusbh200->regs->command,
			    CMD_RESET, 0, 250 * 1000);

	if (retval)
		return retval;

	if (fusbh200->debug)
		dbgp_external_startup(fusbh200_to_hcd(fusbh200));

	fusbh200->port_c_suspend = fusbh200->suspended_ports =
			fusbh200->resuming_ports = 0;
	return retval;
}

/*
 * Idle the controller (turn off the schedules).
 * Must be called with interrupts enabled and the lock not held.
 */
static void fusbh200_quiesce (struct fusbh200_hcd *fusbh200)
{
	u32	temp;

	if (fusbh200->rh_state != FUSBH200_RH_RUNNING)
		return;

	/* wait for any schedule enables/disables to take effect */
	temp = (fusbh200->command << 10) & (STS_ASS | STS_PSS);
	handshake(fusbh200, &fusbh200->regs->status, STS_ASS | STS_PSS, temp, 16 * 125);

	/* then disable anything that's still active */
	spin_lock_irq(&fusbh200->lock);
	fusbh200->command &= ~(CMD_ASE | CMD_PSE);
	fusbh200_writel(fusbh200, fusbh200->command, &fusbh200->regs->command);
	spin_unlock_irq(&fusbh200->lock);

	/* hardware can take 16 microframes to turn off ... */
	handshake(fusbh200, &fusbh200->regs->status, STS_ASS | STS_PSS, 0, 16 * 125);
}

/*-------------------------------------------------------------------------*/

static void end_unlink_async(struct fusbh200_hcd *fusbh200);
static void unlink_empty_async(struct fusbh200_hcd *fusbh200);
static void fusbh200_work(struct fusbh200_hcd *fusbh200);
static void start_unlink_intr(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh);
static void end_unlink_intr(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh);

/*-------------------------------------------------------------------------*/

/* Set a bit in the USBCMD register */
static void fusbh200_set_command_bit(struct fusbh200_hcd *fusbh200, u32 bit)
{
	fusbh200->command |= bit;
	fusbh200_writel(fusbh200, fusbh200->command, &fusbh200->regs->command);

	/* unblock posted write */
	fusbh200_readl(fusbh200, &fusbh200->regs->command);
}

/* Clear a bit in the USBCMD register */
static void fusbh200_clear_command_bit(struct fusbh200_hcd *fusbh200, u32 bit)
{
	fusbh200->command &= ~bit;
	fusbh200_writel(fusbh200, fusbh200->command, &fusbh200->regs->command);

	/* unblock posted write */
	fusbh200_readl(fusbh200, &fusbh200->regs->command);
}

/*-------------------------------------------------------------------------*/

/*
 * EHCI timer support...  Now using hrtimers.
 *
 * Lots of different events are triggered from fusbh200->hrtimer.  Whenever
 * the timer routine runs, it checks each possible event; events that are
 * currently enabled and whose expiration time has passed get handled.
 * The set of enabled events is stored as a collection of bitflags in
 * fusbh200->enabled_hrtimer_events, and they are numbered in order of
 * increasing delay values (ranging between 1 ms and 100 ms).
 *
 * Rather than implementing a sorted list or tree of all pending events,
 * we keep track only of the lowest-numbered pending event, in
 * fusbh200->next_hrtimer_event.  Whenever fusbh200->hrtimer gets restarted, its
 * expiration time is set to the timeout value for this event.
 *
 * As a result, events might not get handled right away; the actual delay
 * could be anywhere up to twice the requested delay.  This doesn't
 * matter, because none of the events are especially time-critical.  The
 * ones that matter most all have a delay of 1 ms, so they will be
 * handled after 2 ms at most, which is okay.  In addition to this, we
 * allow for an expiration range of 1 ms.
 */

/*
 * Delay lengths for the hrtimer event types.
 * Keep this list sorted by delay length, in the same order as
 * the event types indexed by enum fusbh200_hrtimer_event in fusbh200.h.
 */
static unsigned event_delays_ns[] = {
	1 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_POLL_ASS */
	1 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_POLL_PSS */
	1 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_POLL_DEAD */
	1125 * NSEC_PER_USEC,	/* FUSBH200_HRTIMER_UNLINK_INTR */
	2 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_FREE_ITDS */
	6 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_ASYNC_UNLINKS */
	10 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_IAA_WATCHDOG */
	10 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_DISABLE_PERIODIC */
	15 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_DISABLE_ASYNC */
	100 * NSEC_PER_MSEC,	/* FUSBH200_HRTIMER_IO_WATCHDOG */
};

/* Enable a pending hrtimer event */
static void fusbh200_enable_event(struct fusbh200_hcd *fusbh200, unsigned event,
		bool resched)
{
	ktime_t		*timeout = &fusbh200->hr_timeouts[event];

	if (resched)
		*timeout = ktime_add(ktime_get(),
				ktime_set(0, event_delays_ns[event]));
	fusbh200->enabled_hrtimer_events |= (1 << event);

	/* Track only the lowest-numbered pending event */
	if (event < fusbh200->next_hrtimer_event) {
		fusbh200->next_hrtimer_event = event;
		hrtimer_start_range_ns(&fusbh200->hrtimer, *timeout,
				NSEC_PER_MSEC, HRTIMER_MODE_ABS);
	}
}


/* Poll the STS_ASS status bit; see when it agrees with CMD_ASE */
static void fusbh200_poll_ASS(struct fusbh200_hcd *fusbh200)
{
	unsigned	actual, want;

	/* Don't enable anything if the controller isn't running (e.g., died) */
	if (fusbh200->rh_state != FUSBH200_RH_RUNNING)
		return;

	want = (fusbh200->command & CMD_ASE) ? STS_ASS : 0;
	actual = fusbh200_readl(fusbh200, &fusbh200->regs->status) & STS_ASS;

	if (want != actual) {

		/* Poll again later, but give up after about 20 ms */
		if (fusbh200->ASS_poll_count++ < 20) {
			fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_POLL_ASS, true);
			return;
		}
		fusbh200_dbg(fusbh200, "Waited too long for the async schedule status (%x/%x), giving up\n",
				want, actual);
	}
	fusbh200->ASS_poll_count = 0;

	/* The status is up-to-date; restart or stop the schedule as needed */
	if (want == 0) {	/* Stopped */
		if (fusbh200->async_count > 0)
			fusbh200_set_command_bit(fusbh200, CMD_ASE);

	} else {		/* Running */
		if (fusbh200->async_count == 0) {

			/* Turn off the schedule after a while */
			fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_DISABLE_ASYNC,
					true);
		}
	}
}

/* Turn off the async schedule after a brief delay */
static void fusbh200_disable_ASE(struct fusbh200_hcd *fusbh200)
{
	fusbh200_clear_command_bit(fusbh200, CMD_ASE);
}


/* Poll the STS_PSS status bit; see when it agrees with CMD_PSE */
static void fusbh200_poll_PSS(struct fusbh200_hcd *fusbh200)
{
	unsigned	actual, want;

	/* Don't do anything if the controller isn't running (e.g., died) */
	if (fusbh200->rh_state != FUSBH200_RH_RUNNING)
		return;

	want = (fusbh200->command & CMD_PSE) ? STS_PSS : 0;
	actual = fusbh200_readl(fusbh200, &fusbh200->regs->status) & STS_PSS;

	if (want != actual) {

		/* Poll again later, but give up after about 20 ms */
		if (fusbh200->PSS_poll_count++ < 20) {
			fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_POLL_PSS, true);
			return;
		}
		fusbh200_dbg(fusbh200, "Waited too long for the periodic schedule status (%x/%x), giving up\n",
				want, actual);
	}
	fusbh200->PSS_poll_count = 0;

	/* The status is up-to-date; restart or stop the schedule as needed */
	if (want == 0) {	/* Stopped */
		if (fusbh200->periodic_count > 0)
			fusbh200_set_command_bit(fusbh200, CMD_PSE);

	} else {		/* Running */
		if (fusbh200->periodic_count == 0) {

			/* Turn off the schedule after a while */
			fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_DISABLE_PERIODIC,
					true);
		}
	}
}

/* Turn off the periodic schedule after a brief delay */
static void fusbh200_disable_PSE(struct fusbh200_hcd *fusbh200)
{
	fusbh200_clear_command_bit(fusbh200, CMD_PSE);
}


/* Poll the STS_HALT status bit; see when a dead controller stops */
static void fusbh200_handle_controller_death(struct fusbh200_hcd *fusbh200)
{
	if (!(fusbh200_readl(fusbh200, &fusbh200->regs->status) & STS_HALT)) {

		/* Give up after a few milliseconds */
		if (fusbh200->died_poll_count++ < 5) {
			/* Try again later */
			fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_POLL_DEAD, true);
			return;
		}
		fusbh200_warn(fusbh200, "Waited too long for the controller to stop, giving up\n");
	}

	/* Clean up the mess */
	fusbh200->rh_state = FUSBH200_RH_HALTED;
	fusbh200_writel(fusbh200, 0, &fusbh200->regs->intr_enable);
	fusbh200_work(fusbh200);
	end_unlink_async(fusbh200);

	/* Not in process context, so don't try to reset the controller */
}


/* Handle unlinked interrupt QHs once they are gone from the hardware */
static void fusbh200_handle_intr_unlinks(struct fusbh200_hcd *fusbh200)
{
	bool		stopped = (fusbh200->rh_state < FUSBH200_RH_RUNNING);

	/*
	 * Process all the QHs on the intr_unlink list that were added
	 * before the current unlink cycle began.  The list is in
	 * temporal order, so stop when we reach the first entry in the
	 * current cycle.  But if the root hub isn't running then
	 * process all the QHs on the list.
	 */
	fusbh200->intr_unlinking = true;
	while (fusbh200->intr_unlink) {
		struct fusbh200_qh	*qh = fusbh200->intr_unlink;

		if (!stopped && qh->unlink_cycle == fusbh200->intr_unlink_cycle)
			break;
		fusbh200->intr_unlink = qh->unlink_next;
		qh->unlink_next = NULL;
		end_unlink_intr(fusbh200, qh);
	}

	/* Handle remaining entries later */
	if (fusbh200->intr_unlink) {
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_UNLINK_INTR, true);
		++fusbh200->intr_unlink_cycle;
	}
	fusbh200->intr_unlinking = false;
}


/* Start another free-iTDs/siTDs cycle */
static void start_free_itds(struct fusbh200_hcd *fusbh200)
{
	if (!(fusbh200->enabled_hrtimer_events & BIT(FUSBH200_HRTIMER_FREE_ITDS))) {
		fusbh200->last_itd_to_free = list_entry(
				fusbh200->cached_itd_list.prev,
				struct fusbh200_itd, itd_list);
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_FREE_ITDS, true);
	}
}

/* Wait for controller to stop using old iTDs and siTDs */
static void end_free_itds(struct fusbh200_hcd *fusbh200)
{
	struct fusbh200_itd		*itd, *n;

	if (fusbh200->rh_state < FUSBH200_RH_RUNNING) {
		fusbh200->last_itd_to_free = NULL;
	}

	list_for_each_entry_safe(itd, n, &fusbh200->cached_itd_list, itd_list) {
		list_del(&itd->itd_list);
		dma_pool_free(fusbh200->itd_pool, itd, itd->itd_dma);
		if (itd == fusbh200->last_itd_to_free)
			break;
	}

	if (!list_empty(&fusbh200->cached_itd_list))
		start_free_itds(fusbh200);
}


/* Handle lost (or very late) IAA interrupts */
static void fusbh200_iaa_watchdog(struct fusbh200_hcd *fusbh200)
{
	if (fusbh200->rh_state != FUSBH200_RH_RUNNING)
		return;

	/*
	 * Lost IAA irqs wedge things badly; seen first with a vt8235.
	 * So we need this watchdog, but must protect it against both
	 * (a) SMP races against real IAA firing and retriggering, and
	 * (b) clean HC shutdown, when IAA watchdog was pending.
	 */
	if (fusbh200->async_iaa) {
		u32 cmd, status;

		/* If we get here, IAA is *REALLY* late.  It's barely
		 * conceivable that the system is so busy that CMD_IAAD
		 * is still legitimately set, so let's be sure it's
		 * clear before we read STS_IAA.  (The HC should clear
		 * CMD_IAAD when it sets STS_IAA.)
		 */
		cmd = fusbh200_readl(fusbh200, &fusbh200->regs->command);

		/*
		 * If IAA is set here it either legitimately triggered
		 * after the watchdog timer expired (_way_ late, so we'll
		 * still count it as lost) ... or a silicon erratum:
		 * - VIA seems to set IAA without triggering the IRQ;
		 * - IAAD potentially cleared without setting IAA.
		 */
		status = fusbh200_readl(fusbh200, &fusbh200->regs->status);
		if ((status & STS_IAA) || !(cmd & CMD_IAAD)) {
			COUNT(fusbh200->stats.lost_iaa);
			fusbh200_writel(fusbh200, STS_IAA, &fusbh200->regs->status);
		}

		fusbh200_dbg(fusbh200, "IAA watchdog: status %x cmd %x\n",
				status, cmd);
		end_unlink_async(fusbh200);
	}
}


/* Enable the I/O watchdog, if appropriate */
static void turn_on_io_watchdog(struct fusbh200_hcd *fusbh200)
{
	/* Not needed if the controller isn't running or it's already enabled */
	if (fusbh200->rh_state != FUSBH200_RH_RUNNING ||
			(fusbh200->enabled_hrtimer_events &
				BIT(FUSBH200_HRTIMER_IO_WATCHDOG)))
		return;

	/*
	 * Isochronous transfers always need the watchdog.
	 * For other sorts we use it only if the flag is set.
	 */
	if (fusbh200->isoc_count > 0 || (fusbh200->need_io_watchdog &&
			fusbh200->async_count + fusbh200->intr_count > 0))
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_IO_WATCHDOG, true);
}


/*
 * Handler functions for the hrtimer event types.
 * Keep this array in the same order as the event types indexed by
 * enum fusbh200_hrtimer_event in fusbh200.h.
 */
static void (*event_handlers[])(struct fusbh200_hcd *) = {
	fusbh200_poll_ASS,			/* FUSBH200_HRTIMER_POLL_ASS */
	fusbh200_poll_PSS,			/* FUSBH200_HRTIMER_POLL_PSS */
	fusbh200_handle_controller_death,	/* FUSBH200_HRTIMER_POLL_DEAD */
	fusbh200_handle_intr_unlinks,	/* FUSBH200_HRTIMER_UNLINK_INTR */
	end_free_itds,			/* FUSBH200_HRTIMER_FREE_ITDS */
	unlink_empty_async,		/* FUSBH200_HRTIMER_ASYNC_UNLINKS */
	fusbh200_iaa_watchdog,		/* FUSBH200_HRTIMER_IAA_WATCHDOG */
	fusbh200_disable_PSE,		/* FUSBH200_HRTIMER_DISABLE_PERIODIC */
	fusbh200_disable_ASE,		/* FUSBH200_HRTIMER_DISABLE_ASYNC */
	fusbh200_work,			/* FUSBH200_HRTIMER_IO_WATCHDOG */
};

static enum hrtimer_restart fusbh200_hrtimer_func(struct hrtimer *t)
{
	struct fusbh200_hcd	*fusbh200 = container_of(t, struct fusbh200_hcd, hrtimer);
	ktime_t		now;
	unsigned long	events;
	unsigned long	flags;
	unsigned	e;

	spin_lock_irqsave(&fusbh200->lock, flags);

	events = fusbh200->enabled_hrtimer_events;
	fusbh200->enabled_hrtimer_events = 0;
	fusbh200->next_hrtimer_event = FUSBH200_HRTIMER_NO_EVENT;

	/*
	 * Check each pending event.  If its time has expired, handle
	 * the event; otherwise re-enable it.
	 */
	now = ktime_get();
	for_each_set_bit(e, &events, FUSBH200_HRTIMER_NUM_EVENTS) {
		if (now.tv64 >= fusbh200->hr_timeouts[e].tv64)
			event_handlers[e](fusbh200);
		else
			fusbh200_enable_event(fusbh200, e, false);
	}

	spin_unlock_irqrestore(&fusbh200->lock, flags);
	return HRTIMER_NORESTART;
}

/*-------------------------------------------------------------------------*/

#define fusbh200_bus_suspend	NULL
#define fusbh200_bus_resume	NULL

/*-------------------------------------------------------------------------*/

static int check_reset_complete (
	struct fusbh200_hcd	*fusbh200,
	int		index,
	u32 __iomem	*status_reg,
	int		port_status
) {
	if (!(port_status & PORT_CONNECT))
		return port_status;

	/* if reset finished and it's still not enabled -- handoff */
	if (!(port_status & PORT_PE)) {
		/* with integrated TT, there's nobody to hand it to! */
		fusbh200_dbg (fusbh200,
			"Failed to enable port %d on root hub TT\n",
			index+1);
		return port_status;
	} else {
		fusbh200_dbg(fusbh200, "port %d reset complete, port enabled\n",
			index + 1);
	}

	return port_status;
}

/*-------------------------------------------------------------------------*/


/* build "status change" packet (one or two bytes) from HC registers */

static int
fusbh200_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct fusbh200_hcd	*fusbh200 = hcd_to_fusbh200 (hcd);
	u32		temp, status;
	u32		mask;
	int		retval = 1;
	unsigned long	flags;

	/* init status to no-changes */
	buf [0] = 0;

	/* Inform the core about resumes-in-progress by returning
	 * a non-zero value even if there are no status changes.
	 */
	status = fusbh200->resuming_ports;

	mask = PORT_CSC | PORT_PEC;
	// PORT_RESUME from hardware ~= PORT_STAT_C_SUSPEND

	/* no hub change reports (bit 0) for now (power, ...) */

	/* port N changes (bit N)? */
	spin_lock_irqsave (&fusbh200->lock, flags);

	temp = fusbh200_readl(fusbh200, &fusbh200->regs->port_status);

	/*
	 * Return status information even for ports with OWNER set.
	 * Otherwise hub_wq wouldn't see the disconnect event when a
	 * high-speed device is switched over to the companion
	 * controller by the user.
	 */

	if ((temp & mask) != 0 || test_bit(0, &fusbh200->port_c_suspend)
			|| (fusbh200->reset_done[0] && time_after_eq(
				jiffies, fusbh200->reset_done[0]))) {
		buf [0] |= 1 << 1;
		status = STS_PCD;
	}
	/* FIXME autosuspend idle root hubs */
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	return status ? retval : 0;
}

/*-------------------------------------------------------------------------*/

static void
fusbh200_hub_descriptor (
	struct fusbh200_hcd		*fusbh200,
	struct usb_hub_descriptor	*desc
) {
	int		ports = HCS_N_PORTS (fusbh200->hcs_params);
	u16		temp;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 10;	/* fusbh200 1.0, 2.3.9 says 20ms max */
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	temp = 0x0008;		/* per-port overcurrent reporting */
	temp |= 0x0002;		/* no power switching */
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

/*-------------------------------------------------------------------------*/

static int fusbh200_hub_control (
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct fusbh200_hcd	*fusbh200 = hcd_to_fusbh200 (hcd);
	int		ports = HCS_N_PORTS (fusbh200->hcs_params);
	u32 __iomem	*status_reg = &fusbh200->regs->port_status;
	u32		temp, temp1, status;
	unsigned long	flags;
	int		retval = 0;
	unsigned	selector;

	/*
	 * FIXME:  support SetPortFeatures USB_PORT_FEAT_INDICATOR.
	 * HCS_INDICATOR may say we can change LEDs to off/amber/green.
	 * (track current state ourselves) ... blink for diagnostics,
	 * power, "this is the one", etc.  EHCI spec supports this.
	 */

	spin_lock_irqsave (&fusbh200->lock, flags);
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
		temp = fusbh200_readl(fusbh200, status_reg);
		temp &= ~PORT_RWC_BITS;

		/*
		 * Even if OWNER is set, so the port is owned by the
		 * companion controller, hub_wq needs to be able to clear
		 * the port-change status bits (especially
		 * USB_PORT_STAT_C_CONNECTION).
		 */

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			fusbh200_writel(fusbh200, temp & ~PORT_PE, status_reg);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			fusbh200_writel(fusbh200, temp | PORT_PEC, status_reg);
			break;
		case USB_PORT_FEAT_SUSPEND:
			if (temp & PORT_RESET)
				goto error;
			if (!(temp & PORT_SUSPEND))
				break;
			if ((temp & PORT_PE) == 0)
				goto error;

			/* resume signaling for 20 msec */
			fusbh200_writel(fusbh200, temp | PORT_RESUME, status_reg);
			fusbh200->reset_done[wIndex] = jiffies
					+ msecs_to_jiffies(20);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			clear_bit(wIndex, &fusbh200->port_c_suspend);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			fusbh200_writel(fusbh200, temp | PORT_CSC, status_reg);
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			fusbh200_writel(fusbh200, temp | BMISR_OVC, &fusbh200->regs->bmisr);
			break;
		case USB_PORT_FEAT_C_RESET:
			/* GetPortStatus clears reset */
			break;
		default:
			goto error;
		}
		fusbh200_readl(fusbh200, &fusbh200->regs->command);	/* unblock posted write */
		break;
	case GetHubDescriptor:
		fusbh200_hub_descriptor (fusbh200, (struct usb_hub_descriptor *)
			buf);
		break;
	case GetHubStatus:
		/* no hub-wide feature/status flags */
		memset (buf, 0, 4);
		//cpu_to_le32s ((u32 *) buf);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		status = 0;
		temp = fusbh200_readl(fusbh200, status_reg);

		// wPortChange bits
		if (temp & PORT_CSC)
			status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (temp & PORT_PEC)
			status |= USB_PORT_STAT_C_ENABLE << 16;

		temp1 = fusbh200_readl(fusbh200, &fusbh200->regs->bmisr);
		if (temp1 & BMISR_OVC)
			status |= USB_PORT_STAT_C_OVERCURRENT << 16;

		/* whoever resumes must GetPortStatus to complete it!! */
		if (temp & PORT_RESUME) {

			/* Remote Wakeup received? */
			if (!fusbh200->reset_done[wIndex]) {
				/* resume signaling for 20 msec */
				fusbh200->reset_done[wIndex] = jiffies
						+ msecs_to_jiffies(20);
				/* check the port again */
				mod_timer(&fusbh200_to_hcd(fusbh200)->rh_timer,
						fusbh200->reset_done[wIndex]);
			}

			/* resume completed? */
			else if (time_after_eq(jiffies,
					fusbh200->reset_done[wIndex])) {
				clear_bit(wIndex, &fusbh200->suspended_ports);
				set_bit(wIndex, &fusbh200->port_c_suspend);
				fusbh200->reset_done[wIndex] = 0;

				/* stop resume signaling */
				temp = fusbh200_readl(fusbh200, status_reg);
				fusbh200_writel(fusbh200,
					temp & ~(PORT_RWC_BITS | PORT_RESUME),
					status_reg);
				clear_bit(wIndex, &fusbh200->resuming_ports);
				retval = handshake(fusbh200, status_reg,
					   PORT_RESUME, 0, 2000 /* 2msec */);
				if (retval != 0) {
					fusbh200_err(fusbh200,
						"port %d resume error %d\n",
						wIndex + 1, retval);
					goto error;
				}
				temp &= ~(PORT_SUSPEND|PORT_RESUME|(3<<10));
			}
		}

		/* whoever resets must GetPortStatus to complete it!! */
		if ((temp & PORT_RESET)
				&& time_after_eq(jiffies,
					fusbh200->reset_done[wIndex])) {
			status |= USB_PORT_STAT_C_RESET << 16;
			fusbh200->reset_done [wIndex] = 0;
			clear_bit(wIndex, &fusbh200->resuming_ports);

			/* force reset to complete */
			fusbh200_writel(fusbh200, temp & ~(PORT_RWC_BITS | PORT_RESET),
					status_reg);
			/* REVISIT:  some hardware needs 550+ usec to clear
			 * this bit; seems too long to spin routinely...
			 */
			retval = handshake(fusbh200, status_reg,
					PORT_RESET, 0, 1000);
			if (retval != 0) {
				fusbh200_err (fusbh200, "port %d reset error %d\n",
					wIndex + 1, retval);
				goto error;
			}

			/* see what we found out */
			temp = check_reset_complete (fusbh200, wIndex, status_reg,
					fusbh200_readl(fusbh200, status_reg));
		}

		if (!(temp & (PORT_RESUME|PORT_RESET))) {
			fusbh200->reset_done[wIndex] = 0;
			clear_bit(wIndex, &fusbh200->resuming_ports);
		}

		/* transfer dedicated ports to the companion hc */
		if ((temp & PORT_CONNECT) &&
				test_bit(wIndex, &fusbh200->companion_ports)) {
			temp &= ~PORT_RWC_BITS;
			fusbh200_writel(fusbh200, temp, status_reg);
			fusbh200_dbg(fusbh200, "port %d --> companion\n", wIndex + 1);
			temp = fusbh200_readl(fusbh200, status_reg);
		}

		/*
		 * Even if OWNER is set, there's no harm letting hub_wq
		 * see the wPortStatus values (they should all be 0 except
		 * for PORT_POWER anyway).
		 */

		if (temp & PORT_CONNECT) {
			status |= USB_PORT_STAT_CONNECTION;
			status |= fusbh200_port_speed(fusbh200, temp);
		}
		if (temp & PORT_PE)
			status |= USB_PORT_STAT_ENABLE;

		/* maybe the port was unsuspended without our knowledge */
		if (temp & (PORT_SUSPEND|PORT_RESUME)) {
			status |= USB_PORT_STAT_SUSPEND;
		} else if (test_bit(wIndex, &fusbh200->suspended_ports)) {
			clear_bit(wIndex, &fusbh200->suspended_ports);
			clear_bit(wIndex, &fusbh200->resuming_ports);
			fusbh200->reset_done[wIndex] = 0;
			if (temp & PORT_PE)
				set_bit(wIndex, &fusbh200->port_c_suspend);
		}

		temp1 = fusbh200_readl(fusbh200, &fusbh200->regs->bmisr);
		if (temp1 & BMISR_OVC)
			status |= USB_PORT_STAT_OVERCURRENT;
		if (temp & PORT_RESET)
			status |= USB_PORT_STAT_RESET;
		if (test_bit(wIndex, &fusbh200->port_c_suspend))
			status |= USB_PORT_STAT_C_SUSPEND << 16;

		if (status & ~0xffff)	/* only if wPortChange is interesting */
			dbg_port(fusbh200, "GetStatus", wIndex + 1, temp);
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
		temp = fusbh200_readl(fusbh200, status_reg);
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
			fusbh200_writel(fusbh200, temp | PORT_SUSPEND, status_reg);
			set_bit(wIndex, &fusbh200->suspended_ports);
			break;
		case USB_PORT_FEAT_RESET:
			if (temp & PORT_RESUME)
				goto error;
			/* line status bits may report this as low speed,
			 * which can be fine if this root hub has a
			 * transaction translator built in.
			 */
			fusbh200_dbg(fusbh200, "port %d reset\n", wIndex + 1);
			temp |= PORT_RESET;
			temp &= ~PORT_PE;

			/*
			 * caller must wait, then call GetPortStatus
			 * usb 2.0 spec says 50 ms resets on root
			 */
			fusbh200->reset_done [wIndex] = jiffies
					+ msecs_to_jiffies (50);
			fusbh200_writel(fusbh200, temp, status_reg);
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
			spin_unlock_irqrestore(&fusbh200->lock, flags);
			fusbh200_quiesce(fusbh200);
			spin_lock_irqsave(&fusbh200->lock, flags);

			/* Put all enabled ports into suspend */
			temp = fusbh200_readl(fusbh200, status_reg) & ~PORT_RWC_BITS;
			if (temp & PORT_PE)
				fusbh200_writel(fusbh200, temp | PORT_SUSPEND,
						status_reg);

			spin_unlock_irqrestore(&fusbh200->lock, flags);
			fusbh200_halt(fusbh200);
			spin_lock_irqsave(&fusbh200->lock, flags);

			temp = fusbh200_readl(fusbh200, status_reg);
			temp |= selector << 16;
			fusbh200_writel(fusbh200, temp, status_reg);
			break;

		default:
			goto error;
		}
		fusbh200_readl(fusbh200, &fusbh200->regs->command);	/* unblock posted writes */
		break;

	default:
error:
		/* "stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	return retval;
}

static void __maybe_unused fusbh200_relinquish_port(struct usb_hcd *hcd,
		int portnum)
{
	return;
}

static int __maybe_unused fusbh200_port_handed_over(struct usb_hcd *hcd,
		int portnum)
{
	return 0;
}
/*-------------------------------------------------------------------------*/
/*
 * There's basically three types of memory:
 *	- data used only by the HCD ... kmalloc is fine
 *	- async and periodic schedules, shared by HC and HCD ... these
 *	  need to use dma_pool or dma_alloc_coherent
 *	- driver buffers, read/written by HC ... single shot DMA mapped
 *
 * There's also "register" data (e.g. PCI or SOC), which is memory mapped.
 * No memory seen by this driver is pageable.
 */

/*-------------------------------------------------------------------------*/

/* Allocate the key transfer structures from the previously allocated pool */

static inline void fusbh200_qtd_init(struct fusbh200_hcd *fusbh200, struct fusbh200_qtd *qtd,
				  dma_addr_t dma)
{
	memset (qtd, 0, sizeof *qtd);
	qtd->qtd_dma = dma;
	qtd->hw_token = cpu_to_hc32(fusbh200, QTD_STS_HALT);
	qtd->hw_next = FUSBH200_LIST_END(fusbh200);
	qtd->hw_alt_next = FUSBH200_LIST_END(fusbh200);
	INIT_LIST_HEAD (&qtd->qtd_list);
}

static struct fusbh200_qtd *fusbh200_qtd_alloc (struct fusbh200_hcd *fusbh200, gfp_t flags)
{
	struct fusbh200_qtd		*qtd;
	dma_addr_t		dma;

	qtd = dma_pool_alloc (fusbh200->qtd_pool, flags, &dma);
	if (qtd != NULL) {
		fusbh200_qtd_init(fusbh200, qtd, dma);
	}
	return qtd;
}

static inline void fusbh200_qtd_free (struct fusbh200_hcd *fusbh200, struct fusbh200_qtd *qtd)
{
	dma_pool_free (fusbh200->qtd_pool, qtd, qtd->qtd_dma);
}


static void qh_destroy(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	/* clean qtds first, and know this is not linked */
	if (!list_empty (&qh->qtd_list) || qh->qh_next.ptr) {
		fusbh200_dbg (fusbh200, "unused qh not empty!\n");
		BUG ();
	}
	if (qh->dummy)
		fusbh200_qtd_free (fusbh200, qh->dummy);
	dma_pool_free(fusbh200->qh_pool, qh->hw, qh->qh_dma);
	kfree(qh);
}

static struct fusbh200_qh *fusbh200_qh_alloc (struct fusbh200_hcd *fusbh200, gfp_t flags)
{
	struct fusbh200_qh		*qh;
	dma_addr_t		dma;

	qh = kzalloc(sizeof *qh, GFP_ATOMIC);
	if (!qh)
		goto done;
	qh->hw = (struct fusbh200_qh_hw *)
		dma_pool_alloc(fusbh200->qh_pool, flags, &dma);
	if (!qh->hw)
		goto fail;
	memset(qh->hw, 0, sizeof *qh->hw);
	qh->qh_dma = dma;
	// INIT_LIST_HEAD (&qh->qh_list);
	INIT_LIST_HEAD (&qh->qtd_list);

	/* dummy td enables safe urb queuing */
	qh->dummy = fusbh200_qtd_alloc (fusbh200, flags);
	if (qh->dummy == NULL) {
		fusbh200_dbg (fusbh200, "no dummy td\n");
		goto fail1;
	}
done:
	return qh;
fail1:
	dma_pool_free(fusbh200->qh_pool, qh->hw, qh->qh_dma);
fail:
	kfree(qh);
	return NULL;
}

/*-------------------------------------------------------------------------*/

/* The queue heads and transfer descriptors are managed from pools tied
 * to each of the "per device" structures.
 * This is the initialisation and cleanup code.
 */

static void fusbh200_mem_cleanup (struct fusbh200_hcd *fusbh200)
{
	if (fusbh200->async)
		qh_destroy(fusbh200, fusbh200->async);
	fusbh200->async = NULL;

	if (fusbh200->dummy)
		qh_destroy(fusbh200, fusbh200->dummy);
	fusbh200->dummy = NULL;

	/* DMA consistent memory and pools */
	if (fusbh200->qtd_pool)
		dma_pool_destroy (fusbh200->qtd_pool);
	fusbh200->qtd_pool = NULL;

	if (fusbh200->qh_pool) {
		dma_pool_destroy (fusbh200->qh_pool);
		fusbh200->qh_pool = NULL;
	}

	if (fusbh200->itd_pool)
		dma_pool_destroy (fusbh200->itd_pool);
	fusbh200->itd_pool = NULL;

	if (fusbh200->periodic)
		dma_free_coherent (fusbh200_to_hcd(fusbh200)->self.controller,
			fusbh200->periodic_size * sizeof (u32),
			fusbh200->periodic, fusbh200->periodic_dma);
	fusbh200->periodic = NULL;

	/* shadow periodic table */
	kfree(fusbh200->pshadow);
	fusbh200->pshadow = NULL;
}

/* remember to add cleanup code (above) if you add anything here */
static int fusbh200_mem_init (struct fusbh200_hcd *fusbh200, gfp_t flags)
{
	int i;

	/* QTDs for control/bulk/intr transfers */
	fusbh200->qtd_pool = dma_pool_create ("fusbh200_qtd",
			fusbh200_to_hcd(fusbh200)->self.controller,
			sizeof (struct fusbh200_qtd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fusbh200->qtd_pool) {
		goto fail;
	}

	/* QHs for control/bulk/intr transfers */
	fusbh200->qh_pool = dma_pool_create ("fusbh200_qh",
			fusbh200_to_hcd(fusbh200)->self.controller,
			sizeof(struct fusbh200_qh_hw),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fusbh200->qh_pool) {
		goto fail;
	}
	fusbh200->async = fusbh200_qh_alloc (fusbh200, flags);
	if (!fusbh200->async) {
		goto fail;
	}

	/* ITD for high speed ISO transfers */
	fusbh200->itd_pool = dma_pool_create ("fusbh200_itd",
			fusbh200_to_hcd(fusbh200)->self.controller,
			sizeof (struct fusbh200_itd),
			64 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!fusbh200->itd_pool) {
		goto fail;
	}

	/* Hardware periodic table */
	fusbh200->periodic = (__le32 *)
		dma_alloc_coherent (fusbh200_to_hcd(fusbh200)->self.controller,
			fusbh200->periodic_size * sizeof(__le32),
			&fusbh200->periodic_dma, 0);
	if (fusbh200->periodic == NULL) {
		goto fail;
	}

		for (i = 0; i < fusbh200->periodic_size; i++)
			fusbh200->periodic[i] = FUSBH200_LIST_END(fusbh200);

	/* software shadow of hardware table */
	fusbh200->pshadow = kcalloc(fusbh200->periodic_size, sizeof(void *), flags);
	if (fusbh200->pshadow != NULL)
		return 0;

fail:
	fusbh200_dbg (fusbh200, "couldn't init memory\n");
	fusbh200_mem_cleanup (fusbh200);
	return -ENOMEM;
}
/*-------------------------------------------------------------------------*/
/*
 * EHCI hardware queue manipulation ... the core.  QH/QTD manipulation.
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

/*-------------------------------------------------------------------------*/

/* fill a qtd, returning how much of the buffer we were able to queue up */

static int
qtd_fill(struct fusbh200_hcd *fusbh200, struct fusbh200_qtd *qtd, dma_addr_t buf,
		  size_t len, int token, int maxpacket)
{
	int	i, count;
	u64	addr = buf;

	/* one buffer entry per 4K ... first might be short or unaligned */
	qtd->hw_buf[0] = cpu_to_hc32(fusbh200, (u32)addr);
	qtd->hw_buf_hi[0] = cpu_to_hc32(fusbh200, (u32)(addr >> 32));
	count = 0x1000 - (buf & 0x0fff);	/* rest of that page */
	if (likely (len < count))		/* ... iff needed */
		count = len;
	else {
		buf +=  0x1000;
		buf &= ~0x0fff;

		/* per-qtd limit: from 16K to 20K (best alignment) */
		for (i = 1; count < len && i < 5; i++) {
			addr = buf;
			qtd->hw_buf[i] = cpu_to_hc32(fusbh200, (u32)addr);
			qtd->hw_buf_hi[i] = cpu_to_hc32(fusbh200,
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
	qtd->hw_token = cpu_to_hc32(fusbh200, (count << 16) | token);
	qtd->length = count;

	return count;
}

/*-------------------------------------------------------------------------*/

static inline void
qh_update (struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh, struct fusbh200_qtd *qtd)
{
	struct fusbh200_qh_hw *hw = qh->hw;

	/* writes to an active overlay are unsafe */
	BUG_ON(qh->qh_state != QH_STATE_IDLE);

	hw->hw_qtd_next = QTD_NEXT(fusbh200, qtd->qtd_dma);
	hw->hw_alt_next = FUSBH200_LIST_END(fusbh200);

	/* Except for control endpoints, we make hardware maintain data
	 * toggle (like OHCI) ... here (re)initialize the toggle in the QH,
	 * and set the pseudo-toggle in udev. Only usb_clear_halt() will
	 * ever clear it.
	 */
	if (!(hw->hw_info1 & cpu_to_hc32(fusbh200, QH_TOGGLE_CTL))) {
		unsigned	is_out, epnum;

		is_out = qh->is_out;
		epnum = (hc32_to_cpup(fusbh200, &hw->hw_info1) >> 8) & 0x0f;
		if (unlikely (!usb_gettoggle (qh->dev, epnum, is_out))) {
			hw->hw_token &= ~cpu_to_hc32(fusbh200, QTD_TOGGLE);
			usb_settoggle (qh->dev, epnum, is_out, 1);
		}
	}

	hw->hw_token &= cpu_to_hc32(fusbh200, QTD_TOGGLE | QTD_STS_PING);
}

/* if it weren't for a common silicon quirk (writing the dummy into the qh
 * overlay, so qh->hw_token wrongly becomes inactive/halted), only fault
 * recovery (including urb dequeue) would need software changes to a QH...
 */
static void
qh_refresh (struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	struct fusbh200_qtd *qtd;

	if (list_empty (&qh->qtd_list))
		qtd = qh->dummy;
	else {
		qtd = list_entry (qh->qtd_list.next,
				struct fusbh200_qtd, qtd_list);
		/*
		 * first qtd may already be partially processed.
		 * If we come here during unlink, the QH overlay region
		 * might have reference to the just unlinked qtd. The
		 * qtd is updated in qh_completions(). Update the QH
		 * overlay here.
		 */
		if (cpu_to_hc32(fusbh200, qtd->qtd_dma) == qh->hw->hw_current) {
			qh->hw->hw_qtd_next = qtd->hw_next;
			qtd = NULL;
		}
	}

	if (qtd)
		qh_update (fusbh200, qh, qtd);
}

/*-------------------------------------------------------------------------*/

static void qh_link_async(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh);

static void fusbh200_clear_tt_buffer_complete(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200(hcd);
	struct fusbh200_qh		*qh = ep->hcpriv;
	unsigned long		flags;

	spin_lock_irqsave(&fusbh200->lock, flags);
	qh->clearing_tt = 0;
	if (qh->qh_state == QH_STATE_IDLE && !list_empty(&qh->qtd_list)
			&& fusbh200->rh_state == FUSBH200_RH_RUNNING)
		qh_link_async(fusbh200, qh);
	spin_unlock_irqrestore(&fusbh200->lock, flags);
}

static void fusbh200_clear_tt_buffer(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh,
		struct urb *urb, u32 token)
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
		    fusbh200_to_hcd(fusbh200)->self.root_hub) {
			if (usb_hub_clear_tt_buffer(urb) == 0)
				qh->clearing_tt = 1;
		}
	}
}

static int qtd_copy_status (
	struct fusbh200_hcd *fusbh200,
	struct urb *urb,
	size_t length,
	u32 token
)
{
	int	status = -EINPROGRESS;

	/* count IN/OUT bytes, not SETUP (even short packets) */
	if (likely (QTD_PID (token) != 2))
		urb->actual_length += length - QTD_LENGTH (token);

	/* don't modify error codes */
	if (unlikely(urb->unlinked))
		return status;

	/* force cleanup after short read; not always an error */
	if (unlikely (IS_SHORT_READ (token)))
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
			status = (QTD_PID (token) == 1) /* IN ? */
				? -ENOSR  /* hc couldn't read data */
				: -ECOMM; /* hc couldn't write data */
		} else if (token & QTD_STS_XACT) {
			/* timeout, bad CRC, wrong PID, etc */
			fusbh200_dbg(fusbh200, "devpath %s ep%d%s 3strikes\n",
				urb->dev->devpath,
				usb_pipeendpoint(urb->pipe),
				usb_pipein(urb->pipe) ? "in" : "out");
			status = -EPROTO;
		} else {	/* unknown */
			status = -EPROTO;
		}

		fusbh200_dbg(fusbh200,
			"dev%d ep%d%s qtd token %08x --> status %d\n",
			usb_pipedevice (urb->pipe),
			usb_pipeendpoint (urb->pipe),
			usb_pipein (urb->pipe) ? "in" : "out",
			token, status);
	}

	return status;
}

static void
fusbh200_urb_done(struct fusbh200_hcd *fusbh200, struct urb *urb, int status)
__releases(fusbh200->lock)
__acquires(fusbh200->lock)
{
	if (likely (urb->hcpriv != NULL)) {
		struct fusbh200_qh	*qh = (struct fusbh200_qh *) urb->hcpriv;

		/* S-mask in a QH means it's an interrupt urb */
		if ((qh->hw->hw_info2 & cpu_to_hc32(fusbh200, QH_SMASK)) != 0) {

			/* ... update hc-wide periodic stats (for usbfs) */
			fusbh200_to_hcd(fusbh200)->self.bandwidth_int_reqs--;
		}
	}

	if (unlikely(urb->unlinked)) {
		COUNT(fusbh200->stats.unlink);
	} else {
		/* report non-error and short read status as zero */
		if (status == -EINPROGRESS || status == -EREMOTEIO)
			status = 0;
		COUNT(fusbh200->stats.complete);
	}

#ifdef FUSBH200_URB_TRACE
	fusbh200_dbg (fusbh200,
		"%s %s urb %p ep%d%s status %d len %d/%d\n",
		__func__, urb->dev->devpath, urb,
		usb_pipeendpoint (urb->pipe),
		usb_pipein (urb->pipe) ? "in" : "out",
		status,
		urb->actual_length, urb->transfer_buffer_length);
#endif

	/* complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(fusbh200_to_hcd(fusbh200), urb);
	spin_unlock (&fusbh200->lock);
	usb_hcd_giveback_urb(fusbh200_to_hcd(fusbh200), urb, status);
	spin_lock (&fusbh200->lock);
}

static int qh_schedule (struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh);

/*
 * Process and free completed qtds for a qh, returning URBs to drivers.
 * Chases up to qh->hw_current.  Returns number of completions called,
 * indicating how much "real" work we did.
 */
static unsigned
qh_completions (struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	struct fusbh200_qtd		*last, *end = qh->dummy;
	struct list_head	*entry, *tmp;
	int			last_status;
	int			stopped;
	unsigned		count = 0;
	u8			state;
	struct fusbh200_qh_hw	*hw = qh->hw;

	if (unlikely (list_empty (&qh->qtd_list)))
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
	list_for_each_safe (entry, tmp, &qh->qtd_list) {
		struct fusbh200_qtd	*qtd;
		struct urb	*urb;
		u32		token = 0;

		qtd = list_entry (entry, struct fusbh200_qtd, qtd_list);
		urb = qtd->urb;

		/* clean up any state from previous QTD ...*/
		if (last) {
			if (likely (last->urb != urb)) {
				fusbh200_urb_done(fusbh200, last->urb, last_status);
				count++;
				last_status = -EINPROGRESS;
			}
			fusbh200_qtd_free (fusbh200, last);
			last = NULL;
		}

		/* ignore urbs submitted during completions we reported */
		if (qtd == end)
			break;

		/* hardware copies qtd out of qh overlay */
		rmb ();
		token = hc32_to_cpu(fusbh200, qtd->hw_token);

		/* always clean up qtds the hc de-activated */
 retry_xacterr:
		if ((token & QTD_STS_ACTIVE) == 0) {

			/* Report Data Buffer Error: non-fatal but useful */
			if (token & QTD_STS_DBE)
				fusbh200_dbg(fusbh200,
					"detected DataBufferErr for urb %p ep%d%s len %d, qtd %p [qh %p]\n",
					urb,
					usb_endpoint_num(&urb->ep->desc),
					usb_endpoint_dir_in(&urb->ep->desc) ? "in" : "out",
					urb->transfer_buffer_length,
					qtd,
					qh);

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
					fusbh200_dbg(fusbh200,
	"detected XactErr len %zu/%zu retry %d\n",
	qtd->length - QTD_LENGTH(token), qtd->length, qh->xacterrs);

					/* reset the token in the qtd and the
					 * qh overlay (which still contains
					 * the qtd) so that we pick up from
					 * where we left off
					 */
					token &= ~QTD_STS_HALT;
					token |= QTD_STS_ACTIVE |
							(FUSBH200_TUNE_CERR << 10);
					qtd->hw_token = cpu_to_hc32(fusbh200,
							token);
					wmb();
					hw->hw_token = cpu_to_hc32(fusbh200,
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
			} else if (IS_SHORT_READ (token)
					&& !(qtd->hw_alt_next
						& FUSBH200_LIST_END(fusbh200))) {
				stopped = 1;
			}

		/* stop scanning when we reach qtds the hc is using */
		} else if (likely (!stopped
				&& fusbh200->rh_state >= FUSBH200_RH_RUNNING)) {
			break;

		/* scan the whole queue for unlinks whenever it stops */
		} else {
			stopped = 1;

			/* cancel everything if we halt, suspend, etc */
			if (fusbh200->rh_state < FUSBH200_RH_RUNNING)
				last_status = -ESHUTDOWN;

			/* this qtd is active; skip it unless a previous qtd
			 * for its urb faulted, or its urb was canceled.
			 */
			else if (last_status == -EINPROGRESS && !urb->unlinked)
				continue;

			/* qh unlinked; token in overlay may be most current */
			if (state == QH_STATE_IDLE
					&& cpu_to_hc32(fusbh200, qtd->qtd_dma)
						== hw->hw_current) {
				token = hc32_to_cpu(fusbh200, hw->hw_token);

				/* An unlink may leave an incomplete
				 * async transaction in the TT buffer.
				 * We have to clear it.
				 */
				fusbh200_clear_tt_buffer(fusbh200, qh, urb, token);
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
			last_status = qtd_copy_status(fusbh200, urb,
					qtd->length, token);
			if (last_status == -EREMOTEIO
					&& (qtd->hw_alt_next
						& FUSBH200_LIST_END(fusbh200)))
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
					fusbh200_clear_tt_buffer(fusbh200, qh, urb,
							token);
			}
		}

		/* if we're removing something not at the queue head,
		 * patch the hardware queue pointer.
		 */
		if (stopped && qtd->qtd_list.prev != &qh->qtd_list) {
			last = list_entry (qtd->qtd_list.prev,
					struct fusbh200_qtd, qtd_list);
			last->hw_next = qtd->hw_next;
		}

		/* remove qtd; it's recycled after possible urb completion */
		list_del (&qtd->qtd_list);
		last = qtd;

		/* reinit the xacterr counter for the next qtd */
		qh->xacterrs = 0;
	}

	/* last urb's completion might still need calling */
	if (likely (last != NULL)) {
		fusbh200_urb_done(fusbh200, last->urb, last_status);
		count++;
		fusbh200_qtd_free (fusbh200, last);
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
	if (stopped != 0 || hw->hw_qtd_next == FUSBH200_LIST_END(fusbh200)) {
		switch (state) {
		case QH_STATE_IDLE:
			qh_refresh(fusbh200, qh);
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

/*-------------------------------------------------------------------------*/

// high bandwidth multiplier, as encoded in highspeed endpoint descriptors
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))
// ... and packet size, for any kind of endpoint descriptor
#define max_packet(wMaxPacketSize) ((wMaxPacketSize) & 0x07ff)

/*
 * reverse of qh_urb_transaction:  free a list of TDs.
 * used for cleanup after errors, before HC sees an URB's TDs.
 */
static void qtd_list_free (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct list_head	*qtd_list
) {
	struct list_head	*entry, *temp;

	list_for_each_safe (entry, temp, qtd_list) {
		struct fusbh200_qtd	*qtd;

		qtd = list_entry (entry, struct fusbh200_qtd, qtd_list);
		list_del (&qtd->qtd_list);
		fusbh200_qtd_free (fusbh200, qtd);
	}
}

/*
 * create a list of filled qtds for this URB; won't link into qh.
 */
static struct list_head *
qh_urb_transaction (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct list_head	*head,
	gfp_t			flags
) {
	struct fusbh200_qtd		*qtd, *qtd_prev;
	dma_addr_t		buf;
	int			len, this_sg_len, maxpacket;
	int			is_input;
	u32			token;
	int			i;
	struct scatterlist	*sg;

	/*
	 * URBs map to sequences of QTDs:  one logical transaction
	 */
	qtd = fusbh200_qtd_alloc (fusbh200, flags);
	if (unlikely (!qtd))
		return NULL;
	list_add_tail (&qtd->qtd_list, head);
	qtd->urb = urb;

	token = QTD_STS_ACTIVE;
	token |= (FUSBH200_TUNE_CERR << 10);
	/* for split transactions, SplitXState initialized to zero */

	len = urb->transfer_buffer_length;
	is_input = usb_pipein (urb->pipe);
	if (usb_pipecontrol (urb->pipe)) {
		/* SETUP pid */
		qtd_fill(fusbh200, qtd, urb->setup_dma,
				sizeof (struct usb_ctrlrequest),
				token | (2 /* "setup" */ << 8), 8);

		/* ... and always at least one more pid */
		token ^= QTD_TOGGLE;
		qtd_prev = qtd;
		qtd = fusbh200_qtd_alloc (fusbh200, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(fusbh200, qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);

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

		this_qtd_len = qtd_fill(fusbh200, qtd, buf, this_sg_len, token,
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
			qtd->hw_alt_next = fusbh200->async->hw->hw_alt_next;

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
		qtd = fusbh200_qtd_alloc (fusbh200, flags);
		if (unlikely (!qtd))
			goto cleanup;
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(fusbh200, qtd->qtd_dma);
		list_add_tail (&qtd->qtd_list, head);
	}

	/*
	 * unless the caller requires manual cleanup after short reads,
	 * have the alt_next mechanism keep the queue running after the
	 * last data qtd (the only one, for control and most other cases).
	 */
	if (likely ((urb->transfer_flags & URB_SHORT_NOT_OK) == 0
				|| usb_pipecontrol (urb->pipe)))
		qtd->hw_alt_next = FUSBH200_LIST_END(fusbh200);

	/*
	 * control requests may need a terminating data "status" ack;
	 * other OUT ones may need a terminating short packet
	 * (zero length).
	 */
	if (likely (urb->transfer_buffer_length != 0)) {
		int	one_more = 0;

		if (usb_pipecontrol (urb->pipe)) {
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
			qtd = fusbh200_qtd_alloc (fusbh200, flags);
			if (unlikely (!qtd))
				goto cleanup;
			qtd->urb = urb;
			qtd_prev->hw_next = QTD_NEXT(fusbh200, qtd->qtd_dma);
			list_add_tail (&qtd->qtd_list, head);

			/* never any data in such packets */
			qtd_fill(fusbh200, qtd, 0, 0, token, 0);
		}
	}

	/* by default, enable interrupt on urb completion */
	if (likely (!(urb->transfer_flags & URB_NO_INTERRUPT)))
		qtd->hw_token |= cpu_to_hc32(fusbh200, QTD_IOC);
	return head;

cleanup:
	qtd_list_free (fusbh200, urb, head);
	return NULL;
}

/*-------------------------------------------------------------------------*/

// Would be best to create all qh's from config descriptors,
// when each interface/altsetting is established.  Unlink
// any previous qh and cancel its urbs first; endpoints are
// implicitly reset then (data toggle too).
// That'd mean updating how usbcore talks to HCDs. (2.7?)


/*
 * Each QH holds a qtd list; a QH is used for everything except iso.
 *
 * For interrupt urbs, the scheduler must set the microframe scheduling
 * mask(s) each time the QH gets scheduled.  For highspeed, that's
 * just one microframe in the s-mask.  For split interrupt transactions
 * there are additional complications: c-mask, maybe FSTNs.
 */
static struct fusbh200_qh *
qh_make (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	gfp_t			flags
) {
	struct fusbh200_qh		*qh = fusbh200_qh_alloc (fusbh200, flags);
	u32			info1 = 0, info2 = 0;
	int			is_input, type;
	int			maxp = 0;
	struct usb_tt		*tt = urb->dev->tt;
	struct fusbh200_qh_hw	*hw;

	if (!qh)
		return qh;

	/*
	 * init endpoint/device data for this QH
	 */
	info1 |= usb_pipeendpoint (urb->pipe) << 8;
	info1 |= usb_pipedevice (urb->pipe) << 0;

	is_input = usb_pipein (urb->pipe);
	type = usb_pipetype (urb->pipe);
	maxp = usb_maxpacket (urb->dev, urb->pipe, !is_input);

	/* 1024 byte maxpacket is a hardware ceiling.  High bandwidth
	 * acts like up to 3KB, but is built from smaller packets.
	 */
	if (max_packet(maxp) > 1024) {
		fusbh200_dbg(fusbh200, "bogus qh maxpacket %d\n", max_packet(maxp));
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
			} else if (qh->period > fusbh200->periodic_size) {
				qh->period = fusbh200->periodic_size;
				urb->interval = qh->period << 3;
			}
		} else {
			int		think_time;

			/* gap is f(FS/LS transfer times) */
			qh->gap_uf = 1 + usb_calc_bus_time (urb->dev->speed,
					is_input, 0, maxp) / (125 * 1000);

			/* FIXME this just approximates SPLIT/CSPLIT times */
			if (is_input) {		// SPLIT, gap, CSPLIT+DATA
				qh->c_usecs = qh->usecs + HS_USECS (0);
				qh->usecs = HS_USECS (1);
			} else {		// SPLIT+DATA, gap, CSPLIT
				qh->usecs += HS_USECS (1);
				qh->c_usecs = HS_USECS (0);
			}

			think_time = tt ? tt->think_time : 0;
			qh->tt_usecs = NS_TO_US (think_time +
					usb_calc_bus_time (urb->dev->speed,
					is_input, 0, max_packet (maxp)));
			qh->period = urb->interval;
			if (qh->period > fusbh200->periodic_size) {
				qh->period = fusbh200->periodic_size;
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
			info1 |= (FUSBH200_TUNE_RL_TT << 28);
		if (type == PIPE_CONTROL) {
			info1 |= QH_CONTROL_EP;		/* for TT */
			info1 |= QH_TOGGLE_CTL;		/* toggle from qtd */
		}
		info1 |= maxp << 16;

		info2 |= (FUSBH200_TUNE_MULT_TT << 30);

		/* Some Freescale processors have an erratum in which the
		 * port number in the queue head was 0..N-1 instead of 1..N.
		 */
		if (fusbh200_has_fsl_portno_bug(fusbh200))
			info2 |= (urb->dev->ttport-1) << 23;
		else
			info2 |= urb->dev->ttport << 23;

		/* set the address of the TT; for TDI's integrated
		 * root hub tt, leave it zeroed.
		 */
		if (tt && tt->hub != fusbh200_to_hcd(fusbh200)->self.root_hub)
			info2 |= tt->hub->devnum << 16;

		/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets c-mask } */

		break;

	case USB_SPEED_HIGH:		/* no TT involved */
		info1 |= QH_HIGH_SPEED;
		if (type == PIPE_CONTROL) {
			info1 |= (FUSBH200_TUNE_RL_HS << 28);
			info1 |= 64 << 16;	/* usb2 fixed maxpacket */
			info1 |= QH_TOGGLE_CTL;	/* toggle from qtd */
			info2 |= (FUSBH200_TUNE_MULT_HS << 30);
		} else if (type == PIPE_BULK) {
			info1 |= (FUSBH200_TUNE_RL_HS << 28);
			/* The USB spec says that high speed bulk endpoints
			 * always use 512 byte maxpacket.  But some device
			 * vendors decided to ignore that, and MSFT is happy
			 * to help them do so.  So now people expect to use
			 * such nonconformant devices with Linux too; sigh.
			 */
			info1 |= max_packet(maxp) << 16;
			info2 |= (FUSBH200_TUNE_MULT_HS << 30);
		} else {		/* PIPE_INTERRUPT */
			info1 |= max_packet (maxp) << 16;
			info2 |= hb_mult (maxp) << 30;
		}
		break;
	default:
		fusbh200_dbg(fusbh200, "bogus dev %p speed %d\n", urb->dev,
			urb->dev->speed);
done:
		qh_destroy(fusbh200, qh);
		return NULL;
	}

	/* NOTE:  if (PIPE_INTERRUPT) { scheduler sets s-mask } */

	/* init as live, toggle clear, advance to dummy */
	qh->qh_state = QH_STATE_IDLE;
	hw = qh->hw;
	hw->hw_info1 = cpu_to_hc32(fusbh200, info1);
	hw->hw_info2 = cpu_to_hc32(fusbh200, info2);
	qh->is_out = !is_input;
	usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe), !is_input, 1);
	qh_refresh (fusbh200, qh);
	return qh;
}

/*-------------------------------------------------------------------------*/

static void enable_async(struct fusbh200_hcd *fusbh200)
{
	if (fusbh200->async_count++)
		return;

	/* Stop waiting to turn off the async schedule */
	fusbh200->enabled_hrtimer_events &= ~BIT(FUSBH200_HRTIMER_DISABLE_ASYNC);

	/* Don't start the schedule until ASS is 0 */
	fusbh200_poll_ASS(fusbh200);
	turn_on_io_watchdog(fusbh200);
}

static void disable_async(struct fusbh200_hcd *fusbh200)
{
	if (--fusbh200->async_count)
		return;

	/* The async schedule and async_unlink list are supposed to be empty */
	WARN_ON(fusbh200->async->qh_next.qh || fusbh200->async_unlink);

	/* Don't turn off the schedule until ASS is 1 */
	fusbh200_poll_ASS(fusbh200);
}

/* move qh (and its qtds) onto async queue; maybe enable queue.  */

static void qh_link_async (struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	__hc32		dma = QH_NEXT(fusbh200, qh->qh_dma);
	struct fusbh200_qh	*head;

	/* Don't link a QH if there's a Clear-TT-Buffer pending */
	if (unlikely(qh->clearing_tt))
		return;

	WARN_ON(qh->qh_state != QH_STATE_IDLE);

	/* clear halt and/or toggle; and maybe recover from silicon quirk */
	qh_refresh(fusbh200, qh);

	/* splice right after start */
	head = fusbh200->async;
	qh->qh_next = head->qh_next;
	qh->hw->hw_next = head->hw->hw_next;
	wmb ();

	head->qh_next.qh = qh;
	head->hw->hw_next = dma;

	qh->xacterrs = 0;
	qh->qh_state = QH_STATE_LINKED;
	/* qtd completions reported later by interrupt */

	enable_async(fusbh200);
}

/*-------------------------------------------------------------------------*/

/*
 * For control/bulk/interrupt, return QH with these TDs appended.
 * Allocates and initializes the QH if necessary.
 * Returns null if it can't allocate a QH it needs to.
 * If the QH has TDs (urbs) already, that's great.
 */
static struct fusbh200_qh *qh_append_tds (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct list_head	*qtd_list,
	int			epnum,
	void			**ptr
)
{
	struct fusbh200_qh		*qh = NULL;
	__hc32			qh_addr_mask = cpu_to_hc32(fusbh200, 0x7f);

	qh = (struct fusbh200_qh *) *ptr;
	if (unlikely (qh == NULL)) {
		/* can't sleep here, we have fusbh200->lock... */
		qh = qh_make (fusbh200, urb, GFP_ATOMIC);
		*ptr = qh;
	}
	if (likely (qh != NULL)) {
		struct fusbh200_qtd	*qtd;

		if (unlikely (list_empty (qtd_list)))
			qtd = NULL;
		else
			qtd = list_entry (qtd_list->next, struct fusbh200_qtd,
					qtd_list);

		/* control qh may need patching ... */
		if (unlikely (epnum == 0)) {

                        /* usb_reset_device() briefly reverts to address 0 */
                        if (usb_pipedevice (urb->pipe) == 0)
				qh->hw->hw_info1 &= ~qh_addr_mask;
		}

		/* just one way to queue requests: swap with the dummy qtd.
		 * only hc or qh_refresh() ever modify the overlay.
		 */
		if (likely (qtd != NULL)) {
			struct fusbh200_qtd		*dummy;
			dma_addr_t		dma;
			__hc32			token;

			/* to avoid racing the HC, use the dummy td instead of
			 * the first td of our list (becomes new dummy).  both
			 * tds stay deactivated until we're done, when the
			 * HC is allowed to fetch the old dummy (4.10.2).
			 */
			token = qtd->hw_token;
			qtd->hw_token = HALT_BIT(fusbh200);

			dummy = qh->dummy;

			dma = dummy->qtd_dma;
			*dummy = *qtd;
			dummy->qtd_dma = dma;

			list_del (&qtd->qtd_list);
			list_add (&dummy->qtd_list, qtd_list);
			list_splice_tail(qtd_list, &qh->qtd_list);

			fusbh200_qtd_init(fusbh200, qtd, qtd->qtd_dma);
			qh->dummy = qtd;

			/* hc must see the new dummy at list end */
			dma = qtd->qtd_dma;
			qtd = list_entry (qh->qtd_list.prev,
					struct fusbh200_qtd, qtd_list);
			qtd->hw_next = QTD_NEXT(fusbh200, dma);

			/* let the hc process these next qtds */
			wmb ();
			dummy->hw_token = token;

			urb->hcpriv = qh;
		}
	}
	return qh;
}

/*-------------------------------------------------------------------------*/

static int
submit_async (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct list_head	*qtd_list,
	gfp_t			mem_flags
) {
	int			epnum;
	unsigned long		flags;
	struct fusbh200_qh		*qh = NULL;
	int			rc;

	epnum = urb->ep->desc.bEndpointAddress;

#ifdef FUSBH200_URB_TRACE
	{
		struct fusbh200_qtd *qtd;
		qtd = list_entry(qtd_list->next, struct fusbh200_qtd, qtd_list);
		fusbh200_dbg(fusbh200,
			 "%s %s urb %p ep%d%s len %d, qtd %p [qh %p]\n",
			 __func__, urb->dev->devpath, urb,
			 epnum & 0x0f, (epnum & USB_DIR_IN) ? "in" : "out",
			 urb->transfer_buffer_length,
			 qtd, urb->ep->hcpriv);
	}
#endif

	spin_lock_irqsave (&fusbh200->lock, flags);
	if (unlikely(!HCD_HW_ACCESSIBLE(fusbh200_to_hcd(fusbh200)))) {
		rc = -ESHUTDOWN;
		goto done;
	}
	rc = usb_hcd_link_urb_to_ep(fusbh200_to_hcd(fusbh200), urb);
	if (unlikely(rc))
		goto done;

	qh = qh_append_tds(fusbh200, urb, qtd_list, epnum, &urb->ep->hcpriv);
	if (unlikely(qh == NULL)) {
		usb_hcd_unlink_urb_from_ep(fusbh200_to_hcd(fusbh200), urb);
		rc = -ENOMEM;
		goto done;
	}

	/* Control/bulk operations through TTs don't need scheduling,
	 * the HC and TT handle it when the TT has a buffer ready.
	 */
	if (likely (qh->qh_state == QH_STATE_IDLE))
		qh_link_async(fusbh200, qh);
 done:
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	if (unlikely (qh == NULL))
		qtd_list_free (fusbh200, urb, qtd_list);
	return rc;
}

/*-------------------------------------------------------------------------*/

static void single_unlink_async(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	struct fusbh200_qh		*prev;

	/* Add to the end of the list of QHs waiting for the next IAAD */
	qh->qh_state = QH_STATE_UNLINK;
	if (fusbh200->async_unlink)
		fusbh200->async_unlink_last->unlink_next = qh;
	else
		fusbh200->async_unlink = qh;
	fusbh200->async_unlink_last = qh;

	/* Unlink it from the schedule */
	prev = fusbh200->async;
	while (prev->qh_next.qh != qh)
		prev = prev->qh_next.qh;

	prev->hw->hw_next = qh->hw->hw_next;
	prev->qh_next = qh->qh_next;
	if (fusbh200->qh_scan_next == qh)
		fusbh200->qh_scan_next = qh->qh_next.qh;
}

static void start_iaa_cycle(struct fusbh200_hcd *fusbh200, bool nested)
{
	/*
	 * Do nothing if an IAA cycle is already running or
	 * if one will be started shortly.
	 */
	if (fusbh200->async_iaa || fusbh200->async_unlinking)
		return;

	/* Do all the waiting QHs at once */
	fusbh200->async_iaa = fusbh200->async_unlink;
	fusbh200->async_unlink = NULL;

	/* If the controller isn't running, we don't have to wait for it */
	if (unlikely(fusbh200->rh_state < FUSBH200_RH_RUNNING)) {
		if (!nested)		/* Avoid recursion */
			end_unlink_async(fusbh200);

	/* Otherwise start a new IAA cycle */
	} else if (likely(fusbh200->rh_state == FUSBH200_RH_RUNNING)) {
		/* Make sure the unlinks are all visible to the hardware */
		wmb();

		fusbh200_writel(fusbh200, fusbh200->command | CMD_IAAD,
				&fusbh200->regs->command);
		fusbh200_readl(fusbh200, &fusbh200->regs->command);
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_IAA_WATCHDOG, true);
	}
}

/* the async qh for the qtds being unlinked are now gone from the HC */

static void end_unlink_async(struct fusbh200_hcd *fusbh200)
{
	struct fusbh200_qh		*qh;

	/* Process the idle QHs */
 restart:
	fusbh200->async_unlinking = true;
	while (fusbh200->async_iaa) {
		qh = fusbh200->async_iaa;
		fusbh200->async_iaa = qh->unlink_next;
		qh->unlink_next = NULL;

		qh->qh_state = QH_STATE_IDLE;
		qh->qh_next.qh = NULL;

		qh_completions(fusbh200, qh);
		if (!list_empty(&qh->qtd_list) &&
				fusbh200->rh_state == FUSBH200_RH_RUNNING)
			qh_link_async(fusbh200, qh);
		disable_async(fusbh200);
	}
	fusbh200->async_unlinking = false;

	/* Start a new IAA cycle if any QHs are waiting for it */
	if (fusbh200->async_unlink) {
		start_iaa_cycle(fusbh200, true);
		if (unlikely(fusbh200->rh_state < FUSBH200_RH_RUNNING))
			goto restart;
	}
}

static void unlink_empty_async(struct fusbh200_hcd *fusbh200)
{
	struct fusbh200_qh		*qh, *next;
	bool			stopped = (fusbh200->rh_state < FUSBH200_RH_RUNNING);
	bool			check_unlinks_later = false;

	/* Unlink all the async QHs that have been empty for a timer cycle */
	next = fusbh200->async->qh_next.qh;
	while (next) {
		qh = next;
		next = qh->qh_next.qh;

		if (list_empty(&qh->qtd_list) &&
				qh->qh_state == QH_STATE_LINKED) {
			if (!stopped && qh->unlink_cycle ==
					fusbh200->async_unlink_cycle)
				check_unlinks_later = true;
			else
				single_unlink_async(fusbh200, qh);
		}
	}

	/* Start a new IAA cycle if any QHs are waiting for it */
	if (fusbh200->async_unlink)
		start_iaa_cycle(fusbh200, false);

	/* QHs that haven't been empty for long enough will be handled later */
	if (check_unlinks_later) {
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_ASYNC_UNLINKS, true);
		++fusbh200->async_unlink_cycle;
	}
}

/* makes sure the async qh will become idle */
/* caller must own fusbh200->lock */

static void start_unlink_async(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
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

	single_unlink_async(fusbh200, qh);
	start_iaa_cycle(fusbh200, false);
}

/*-------------------------------------------------------------------------*/

static void scan_async (struct fusbh200_hcd *fusbh200)
{
	struct fusbh200_qh		*qh;
	bool			check_unlinks_later = false;

	fusbh200->qh_scan_next = fusbh200->async->qh_next.qh;
	while (fusbh200->qh_scan_next) {
		qh = fusbh200->qh_scan_next;
		fusbh200->qh_scan_next = qh->qh_next.qh;
 rescan:
		/* clean any finished work for this qh */
		if (!list_empty(&qh->qtd_list)) {
			int temp;

			/*
			 * Unlinks could happen here; completion reporting
			 * drops the lock.  That's why fusbh200->qh_scan_next
			 * always holds the next qh to scan; if the next qh
			 * gets unlinked then fusbh200->qh_scan_next is adjusted
			 * in single_unlink_async().
			 */
			temp = qh_completions(fusbh200, qh);
			if (qh->needs_rescan) {
				start_unlink_async(fusbh200, qh);
			} else if (list_empty(&qh->qtd_list)
					&& qh->qh_state == QH_STATE_LINKED) {
				qh->unlink_cycle = fusbh200->async_unlink_cycle;
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
	if (check_unlinks_later && fusbh200->rh_state == FUSBH200_RH_RUNNING &&
			!(fusbh200->enabled_hrtimer_events &
				BIT(FUSBH200_HRTIMER_ASYNC_UNLINKS))) {
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_ASYNC_UNLINKS, true);
		++fusbh200->async_unlink_cycle;
	}
}
/*-------------------------------------------------------------------------*/
/*
 * EHCI scheduled transaction support:  interrupt, iso, split iso
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

static int fusbh200_get_frame (struct usb_hcd *hcd);

/*-------------------------------------------------------------------------*/

/*
 * periodic_next_shadow - return "next" pointer on shadow list
 * @periodic: host pointer to qh/itd
 * @tag: hardware tag for type of this record
 */
static union fusbh200_shadow *
periodic_next_shadow(struct fusbh200_hcd *fusbh200, union fusbh200_shadow *periodic,
		__hc32 tag)
{
	switch (hc32_to_cpu(fusbh200, tag)) {
	case Q_TYPE_QH:
		return &periodic->qh->qh_next;
	case Q_TYPE_FSTN:
		return &periodic->fstn->fstn_next;
	default:
		return &periodic->itd->itd_next;
	}
}

static __hc32 *
shadow_next_periodic(struct fusbh200_hcd *fusbh200, union fusbh200_shadow *periodic,
		__hc32 tag)
{
	switch (hc32_to_cpu(fusbh200, tag)) {
	/* our fusbh200_shadow.qh is actually software part */
	case Q_TYPE_QH:
		return &periodic->qh->hw->hw_next;
	/* others are hw parts */
	default:
		return periodic->hw_next;
	}
}

/* caller must hold fusbh200->lock */
static void periodic_unlink (struct fusbh200_hcd *fusbh200, unsigned frame, void *ptr)
{
	union fusbh200_shadow	*prev_p = &fusbh200->pshadow[frame];
	__hc32			*hw_p = &fusbh200->periodic[frame];
	union fusbh200_shadow	here = *prev_p;

	/* find predecessor of "ptr"; hw and shadow lists are in sync */
	while (here.ptr && here.ptr != ptr) {
		prev_p = periodic_next_shadow(fusbh200, prev_p,
				Q_NEXT_TYPE(fusbh200, *hw_p));
		hw_p = shadow_next_periodic(fusbh200, &here,
				Q_NEXT_TYPE(fusbh200, *hw_p));
		here = *prev_p;
	}
	/* an interrupt entry (at list end) could have been shared */
	if (!here.ptr)
		return;

	/* update shadow and hardware lists ... the old "next" pointers
	 * from ptr may still be in use, the caller updates them.
	 */
	*prev_p = *periodic_next_shadow(fusbh200, &here,
			Q_NEXT_TYPE(fusbh200, *hw_p));

	*hw_p = *shadow_next_periodic(fusbh200, &here,
				Q_NEXT_TYPE(fusbh200, *hw_p));
}

/* how many of the uframe's 125 usecs are allocated? */
static unsigned short
periodic_usecs (struct fusbh200_hcd *fusbh200, unsigned frame, unsigned uframe)
{
	__hc32			*hw_p = &fusbh200->periodic [frame];
	union fusbh200_shadow	*q = &fusbh200->pshadow [frame];
	unsigned		usecs = 0;
	struct fusbh200_qh_hw	*hw;

	while (q->ptr) {
		switch (hc32_to_cpu(fusbh200, Q_NEXT_TYPE(fusbh200, *hw_p))) {
		case Q_TYPE_QH:
			hw = q->qh->hw;
			/* is it in the S-mask? */
			if (hw->hw_info2 & cpu_to_hc32(fusbh200, 1 << uframe))
				usecs += q->qh->usecs;
			/* ... or C-mask? */
			if (hw->hw_info2 & cpu_to_hc32(fusbh200,
					1 << (8 + uframe)))
				usecs += q->qh->c_usecs;
			hw_p = &hw->hw_next;
			q = &q->qh->qh_next;
			break;
		// case Q_TYPE_FSTN:
		default:
			/* for "save place" FSTNs, count the relevant INTR
			 * bandwidth from the previous frame
			 */
			if (q->fstn->hw_prev != FUSBH200_LIST_END(fusbh200)) {
				fusbh200_dbg (fusbh200, "ignoring FSTN cost ...\n");
			}
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
	if (usecs > fusbh200->uframe_periodic_max)
		fusbh200_err (fusbh200, "uframe %d sched overrun: %d usecs\n",
			frame * 8 + uframe, usecs);
	return usecs;
}

/*-------------------------------------------------------------------------*/

static int same_tt (struct usb_device *dev1, struct usb_device *dev2)
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
static int tt_no_collision (
	struct fusbh200_hcd		*fusbh200,
	unsigned		period,
	struct usb_device	*dev,
	unsigned		frame,
	u32			uf_mask
)
{
	if (period == 0)	/* error */
		return 0;

	/* note bandwidth wastage:  split never follows csplit
	 * (different dev or endpoint) until the next uframe.
	 * calling convention doesn't make that distinction.
	 */
	for (; frame < fusbh200->periodic_size; frame += period) {
		union fusbh200_shadow	here;
		__hc32			type;
		struct fusbh200_qh_hw	*hw;

		here = fusbh200->pshadow [frame];
		type = Q_NEXT_TYPE(fusbh200, fusbh200->periodic [frame]);
		while (here.ptr) {
			switch (hc32_to_cpu(fusbh200, type)) {
			case Q_TYPE_ITD:
				type = Q_NEXT_TYPE(fusbh200, here.itd->hw_next);
				here = here.itd->itd_next;
				continue;
			case Q_TYPE_QH:
				hw = here.qh->hw;
				if (same_tt (dev, here.qh->dev)) {
					u32		mask;

					mask = hc32_to_cpu(fusbh200,
							hw->hw_info2);
					/* "knows" no gap is needed */
					mask |= mask >> 8;
					if (mask & uf_mask)
						break;
				}
				type = Q_NEXT_TYPE(fusbh200, hw->hw_next);
				here = here.qh->qh_next;
				continue;
			// case Q_TYPE_FSTN:
			default:
				fusbh200_dbg (fusbh200,
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

/*-------------------------------------------------------------------------*/

static void enable_periodic(struct fusbh200_hcd *fusbh200)
{
	if (fusbh200->periodic_count++)
		return;

	/* Stop waiting to turn off the periodic schedule */
	fusbh200->enabled_hrtimer_events &= ~BIT(FUSBH200_HRTIMER_DISABLE_PERIODIC);

	/* Don't start the schedule until PSS is 0 */
	fusbh200_poll_PSS(fusbh200);
	turn_on_io_watchdog(fusbh200);
}

static void disable_periodic(struct fusbh200_hcd *fusbh200)
{
	if (--fusbh200->periodic_count)
		return;

	/* Don't turn off the schedule until PSS is 1 */
	fusbh200_poll_PSS(fusbh200);
}

/*-------------------------------------------------------------------------*/

/* periodic schedule slots have iso tds (normal or split) first, then a
 * sparse tree for active interrupt transfers.
 *
 * this just links in a qh; caller guarantees uframe masks are set right.
 * no FSTN support (yet; fusbh200 0.96+)
 */
static void qh_link_periodic(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	unsigned	i;
	unsigned	period = qh->period;

	dev_dbg (&qh->dev->dev,
		"link qh%d-%04x/%p start %d [%d/%d us]\n",
		period, hc32_to_cpup(fusbh200, &qh->hw->hw_info2)
			& (QH_CMASK | QH_SMASK),
		qh, qh->start, qh->usecs, qh->c_usecs);

	/* high bandwidth, or otherwise every microframe */
	if (period == 0)
		period = 1;

	for (i = qh->start; i < fusbh200->periodic_size; i += period) {
		union fusbh200_shadow	*prev = &fusbh200->pshadow[i];
		__hc32			*hw_p = &fusbh200->periodic[i];
		union fusbh200_shadow	here = *prev;
		__hc32			type = 0;

		/* skip the iso nodes at list head */
		while (here.ptr) {
			type = Q_NEXT_TYPE(fusbh200, *hw_p);
			if (type == cpu_to_hc32(fusbh200, Q_TYPE_QH))
				break;
			prev = periodic_next_shadow(fusbh200, prev, type);
			hw_p = shadow_next_periodic(fusbh200, &here, type);
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
			wmb ();
			prev->qh = qh;
			*hw_p = QH_NEXT (fusbh200, qh->qh_dma);
		}
	}
	qh->qh_state = QH_STATE_LINKED;
	qh->xacterrs = 0;

	/* update per-qh bandwidth for usbfs */
	fusbh200_to_hcd(fusbh200)->self.bandwidth_allocated += qh->period
		? ((qh->usecs + qh->c_usecs) / qh->period)
		: (qh->usecs * 8);

	list_add(&qh->intr_node, &fusbh200->intr_qh_list);

	/* maybe enable periodic schedule processing */
	++fusbh200->intr_count;
	enable_periodic(fusbh200);
}

static void qh_unlink_periodic(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	unsigned	i;
	unsigned	period;

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
	if ((period = qh->period) == 0)
		period = 1;

	for (i = qh->start; i < fusbh200->periodic_size; i += period)
		periodic_unlink (fusbh200, i, qh);

	/* update per-qh bandwidth for usbfs */
	fusbh200_to_hcd(fusbh200)->self.bandwidth_allocated -= qh->period
		? ((qh->usecs + qh->c_usecs) / qh->period)
		: (qh->usecs * 8);

	dev_dbg (&qh->dev->dev,
		"unlink qh%d-%04x/%p start %d [%d/%d us]\n",
		qh->period,
		hc32_to_cpup(fusbh200, &qh->hw->hw_info2) & (QH_CMASK | QH_SMASK),
		qh, qh->start, qh->usecs, qh->c_usecs);

	/* qh->qh_next still "live" to HC */
	qh->qh_state = QH_STATE_UNLINK;
	qh->qh_next.ptr = NULL;

	if (fusbh200->qh_scan_next == qh)
		fusbh200->qh_scan_next = list_entry(qh->intr_node.next,
				struct fusbh200_qh, intr_node);
	list_del(&qh->intr_node);
}

static void start_unlink_intr(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
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

	qh_unlink_periodic (fusbh200, qh);

	/* Make sure the unlinks are visible before starting the timer */
	wmb();

	/*
	 * The EHCI spec doesn't say how long it takes the controller to
	 * stop accessing an unlinked interrupt QH.  The timer delay is
	 * 9 uframes; presumably that will be long enough.
	 */
	qh->unlink_cycle = fusbh200->intr_unlink_cycle;

	/* New entries go at the end of the intr_unlink list */
	if (fusbh200->intr_unlink)
		fusbh200->intr_unlink_last->unlink_next = qh;
	else
		fusbh200->intr_unlink = qh;
	fusbh200->intr_unlink_last = qh;

	if (fusbh200->intr_unlinking)
		;	/* Avoid recursive calls */
	else if (fusbh200->rh_state < FUSBH200_RH_RUNNING)
		fusbh200_handle_intr_unlinks(fusbh200);
	else if (fusbh200->intr_unlink == qh) {
		fusbh200_enable_event(fusbh200, FUSBH200_HRTIMER_UNLINK_INTR, true);
		++fusbh200->intr_unlink_cycle;
	}
}

static void end_unlink_intr(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	struct fusbh200_qh_hw	*hw = qh->hw;
	int			rc;

	qh->qh_state = QH_STATE_IDLE;
	hw->hw_next = FUSBH200_LIST_END(fusbh200);

	qh_completions(fusbh200, qh);

	/* reschedule QH iff another request is queued */
	if (!list_empty(&qh->qtd_list) && fusbh200->rh_state == FUSBH200_RH_RUNNING) {
		rc = qh_schedule(fusbh200, qh);

		/* An error here likely indicates handshake failure
		 * or no space left in the schedule.  Neither fault
		 * should happen often ...
		 *
		 * FIXME kill the now-dysfunctional queued urbs
		 */
		if (rc != 0)
			fusbh200_err(fusbh200, "can't reschedule qh %p, err %d\n",
					qh, rc);
	}

	/* maybe turn off periodic schedule */
	--fusbh200->intr_count;
	disable_periodic(fusbh200);
}

/*-------------------------------------------------------------------------*/

static int check_period (
	struct fusbh200_hcd *fusbh200,
	unsigned	frame,
	unsigned	uframe,
	unsigned	period,
	unsigned	usecs
) {
	int		claimed;

	/* complete split running into next frame?
	 * given FSTN support, we could sometimes check...
	 */
	if (uframe >= 8)
		return 0;

	/* convert "usecs we need" to "max already claimed" */
	usecs = fusbh200->uframe_periodic_max - usecs;

	/* we "know" 2 and 4 uframe intervals were rejected; so
	 * for period 0, check _every_ microframe in the schedule.
	 */
	if (unlikely (period == 0)) {
		do {
			for (uframe = 0; uframe < 7; uframe++) {
				claimed = periodic_usecs (fusbh200, frame, uframe);
				if (claimed > usecs)
					return 0;
			}
		} while ((frame += 1) < fusbh200->periodic_size);

	/* just check the specified uframe, at that period */
	} else {
		do {
			claimed = periodic_usecs (fusbh200, frame, uframe);
			if (claimed > usecs)
				return 0;
		} while ((frame += period) < fusbh200->periodic_size);
	}

	// success!
	return 1;
}

static int check_intr_schedule (
	struct fusbh200_hcd		*fusbh200,
	unsigned		frame,
	unsigned		uframe,
	const struct fusbh200_qh	*qh,
	__hc32			*c_maskp
)
{
	int		retval = -ENOSPC;
	u8		mask = 0;

	if (qh->c_usecs && uframe >= 6)		/* FSTN territory? */
		goto done;

	if (!check_period (fusbh200, frame, uframe, qh->period, qh->usecs))
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
	*c_maskp = cpu_to_hc32(fusbh200, mask << 8);

	mask |= 1 << uframe;
	if (tt_no_collision (fusbh200, qh->period, qh->dev, frame, mask)) {
		if (!check_period (fusbh200, frame, uframe + qh->gap_uf + 1,
					qh->period, qh->c_usecs))
			goto done;
		if (!check_period (fusbh200, frame, uframe + qh->gap_uf,
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
static int qh_schedule(struct fusbh200_hcd *fusbh200, struct fusbh200_qh *qh)
{
	int		status;
	unsigned	uframe;
	__hc32		c_mask;
	unsigned	frame;		/* 0..(qh->period - 1), or NO_FRAME */
	struct fusbh200_qh_hw	*hw = qh->hw;

	qh_refresh(fusbh200, qh);
	hw->hw_next = FUSBH200_LIST_END(fusbh200);
	frame = qh->start;

	/* reuse the previous schedule slots, if we can */
	if (frame < qh->period) {
		uframe = ffs(hc32_to_cpup(fusbh200, &hw->hw_info2) & QH_SMASK);
		status = check_intr_schedule (fusbh200, frame, --uframe,
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
			int		i;

			for (i = qh->period; status && i > 0; --i) {
				frame = ++fusbh200->random_frame % qh->period;
				for (uframe = 0; uframe < 8; uframe++) {
					status = check_intr_schedule (fusbh200,
							frame, uframe, qh,
							&c_mask);
					if (status == 0)
						break;
				}
			}

		/* qh->period == 0 means every uframe */
		} else {
			frame = 0;
			status = check_intr_schedule (fusbh200, 0, 0, qh, &c_mask);
		}
		if (status)
			goto done;
		qh->start = frame;

		/* reset S-frame and (maybe) C-frame masks */
		hw->hw_info2 &= cpu_to_hc32(fusbh200, ~(QH_CMASK | QH_SMASK));
		hw->hw_info2 |= qh->period
			? cpu_to_hc32(fusbh200, 1 << uframe)
			: cpu_to_hc32(fusbh200, QH_SMASK);
		hw->hw_info2 |= c_mask;
	} else
		fusbh200_dbg (fusbh200, "reused qh %p schedule\n", qh);

	/* stuff into the periodic schedule */
	qh_link_periodic(fusbh200, qh);
done:
	return status;
}

static int intr_submit (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct list_head	*qtd_list,
	gfp_t			mem_flags
) {
	unsigned		epnum;
	unsigned long		flags;
	struct fusbh200_qh		*qh;
	int			status;
	struct list_head	empty;

	/* get endpoint and transfer/schedule data */
	epnum = urb->ep->desc.bEndpointAddress;

	spin_lock_irqsave (&fusbh200->lock, flags);

	if (unlikely(!HCD_HW_ACCESSIBLE(fusbh200_to_hcd(fusbh200)))) {
		status = -ESHUTDOWN;
		goto done_not_linked;
	}
	status = usb_hcd_link_urb_to_ep(fusbh200_to_hcd(fusbh200), urb);
	if (unlikely(status))
		goto done_not_linked;

	/* get qh and force any scheduling errors */
	INIT_LIST_HEAD (&empty);
	qh = qh_append_tds(fusbh200, urb, &empty, epnum, &urb->ep->hcpriv);
	if (qh == NULL) {
		status = -ENOMEM;
		goto done;
	}
	if (qh->qh_state == QH_STATE_IDLE) {
		if ((status = qh_schedule (fusbh200, qh)) != 0)
			goto done;
	}

	/* then queue the urb's tds to the qh */
	qh = qh_append_tds(fusbh200, urb, qtd_list, epnum, &urb->ep->hcpriv);
	BUG_ON (qh == NULL);

	/* ... update usbfs periodic stats */
	fusbh200_to_hcd(fusbh200)->self.bandwidth_int_reqs++;

done:
	if (unlikely(status))
		usb_hcd_unlink_urb_from_ep(fusbh200_to_hcd(fusbh200), urb);
done_not_linked:
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	if (status)
		qtd_list_free (fusbh200, urb, qtd_list);

	return status;
}

static void scan_intr(struct fusbh200_hcd *fusbh200)
{
	struct fusbh200_qh		*qh;

	list_for_each_entry_safe(qh, fusbh200->qh_scan_next, &fusbh200->intr_qh_list,
			intr_node) {
 rescan:
		/* clean any finished work for this qh */
		if (!list_empty(&qh->qtd_list)) {
			int temp;

			/*
			 * Unlinks could happen here; completion reporting
			 * drops the lock.  That's why fusbh200->qh_scan_next
			 * always holds the next qh to scan; if the next qh
			 * gets unlinked then fusbh200->qh_scan_next is adjusted
			 * in qh_unlink_periodic().
			 */
			temp = qh_completions(fusbh200, qh);
			if (unlikely(qh->needs_rescan ||
					(list_empty(&qh->qtd_list) &&
						qh->qh_state == QH_STATE_LINKED)))
				start_unlink_intr(fusbh200, qh);
			else if (temp != 0)
				goto rescan;
		}
	}
}

/*-------------------------------------------------------------------------*/

/* fusbh200_iso_stream ops work with both ITD and SITD */

static struct fusbh200_iso_stream *
iso_stream_alloc (gfp_t mem_flags)
{
	struct fusbh200_iso_stream *stream;

	stream = kzalloc(sizeof *stream, mem_flags);
	if (likely (stream != NULL)) {
		INIT_LIST_HEAD(&stream->td_list);
		INIT_LIST_HEAD(&stream->free_list);
		stream->next_uframe = -1;
	}
	return stream;
}

static void
iso_stream_init (
	struct fusbh200_hcd		*fusbh200,
	struct fusbh200_iso_stream	*stream,
	struct usb_device	*dev,
	int			pipe,
	unsigned		interval
)
{
	u32			buf1;
	unsigned		epnum, maxp;
	int			is_input;
	long			bandwidth;
	unsigned 		multi;

	/*
	 * this might be a "high bandwidth" highspeed endpoint,
	 * as encoded in the ep descriptor's wMaxPacket field
	 */
	epnum = usb_pipeendpoint (pipe);
	is_input = usb_pipein (pipe) ? USB_DIR_IN : 0;
	maxp = usb_maxpacket(dev, pipe, !is_input);
	if (is_input) {
		buf1 = (1 << 11);
	} else {
		buf1 = 0;
	}

	maxp = max_packet(maxp);
	multi = hb_mult(maxp);
	buf1 |= maxp;
	maxp *= multi;

	stream->buf0 = cpu_to_hc32(fusbh200, (epnum << 8) | dev->devnum);
	stream->buf1 = cpu_to_hc32(fusbh200, buf1);
	stream->buf2 = cpu_to_hc32(fusbh200, multi);

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
		stream->usecs = HS_USECS_ISO (maxp);
	}
	bandwidth = stream->usecs * 8;
	bandwidth /= interval;

	stream->bandwidth = bandwidth;
	stream->udev = dev;
	stream->bEndpointAddress = is_input | epnum;
	stream->interval = interval;
	stream->maxp = maxp;
}

static struct fusbh200_iso_stream *
iso_stream_find (struct fusbh200_hcd *fusbh200, struct urb *urb)
{
	unsigned		epnum;
	struct fusbh200_iso_stream	*stream;
	struct usb_host_endpoint *ep;
	unsigned long		flags;

	epnum = usb_pipeendpoint (urb->pipe);
	if (usb_pipein(urb->pipe))
		ep = urb->dev->ep_in[epnum];
	else
		ep = urb->dev->ep_out[epnum];

	spin_lock_irqsave (&fusbh200->lock, flags);
	stream = ep->hcpriv;

	if (unlikely (stream == NULL)) {
		stream = iso_stream_alloc(GFP_ATOMIC);
		if (likely (stream != NULL)) {
			ep->hcpriv = stream;
			stream->ep = ep;
			iso_stream_init(fusbh200, stream, urb->dev, urb->pipe,
					urb->interval);
		}

	/* if dev->ep [epnum] is a QH, hw is set */
	} else if (unlikely (stream->hw != NULL)) {
		fusbh200_dbg (fusbh200, "dev %s ep%d%s, not iso??\n",
			urb->dev->devpath, epnum,
			usb_pipein(urb->pipe) ? "in" : "out");
		stream = NULL;
	}

	spin_unlock_irqrestore (&fusbh200->lock, flags);
	return stream;
}

/*-------------------------------------------------------------------------*/

/* fusbh200_iso_sched ops can be ITD-only or SITD-only */

static struct fusbh200_iso_sched *
iso_sched_alloc (unsigned packets, gfp_t mem_flags)
{
	struct fusbh200_iso_sched	*iso_sched;
	int			size = sizeof *iso_sched;

	size += packets * sizeof (struct fusbh200_iso_packet);
	iso_sched = kzalloc(size, mem_flags);
	if (likely (iso_sched != NULL)) {
		INIT_LIST_HEAD (&iso_sched->td_list);
	}
	return iso_sched;
}

static inline void
itd_sched_init(
	struct fusbh200_hcd		*fusbh200,
	struct fusbh200_iso_sched	*iso_sched,
	struct fusbh200_iso_stream	*stream,
	struct urb		*urb
)
{
	unsigned	i;
	dma_addr_t	dma = urb->transfer_dma;

	/* how many uframes are needed for these transfers */
	iso_sched->span = urb->number_of_packets * stream->interval;

	/* figure out per-uframe itd fields that we'll need later
	 * when we fit new itds into the schedule.
	 */
	for (i = 0; i < urb->number_of_packets; i++) {
		struct fusbh200_iso_packet	*uframe = &iso_sched->packet [i];
		unsigned		length;
		dma_addr_t		buf;
		u32			trans;

		length = urb->iso_frame_desc [i].length;
		buf = dma + urb->iso_frame_desc [i].offset;

		trans = FUSBH200_ISOC_ACTIVE;
		trans |= buf & 0x0fff;
		if (unlikely (((i + 1) == urb->number_of_packets))
				&& !(urb->transfer_flags & URB_NO_INTERRUPT))
			trans |= FUSBH200_ITD_IOC;
		trans |= length << 16;
		uframe->transaction = cpu_to_hc32(fusbh200, trans);

		/* might need to cross a buffer page within a uframe */
		uframe->bufp = (buf & ~(u64)0x0fff);
		buf += length;
		if (unlikely ((uframe->bufp != (buf & ~(u64)0x0fff))))
			uframe->cross = 1;
	}
}

static void
iso_sched_free (
	struct fusbh200_iso_stream	*stream,
	struct fusbh200_iso_sched	*iso_sched
)
{
	if (!iso_sched)
		return;
	// caller must hold fusbh200->lock!
	list_splice (&iso_sched->td_list, &stream->free_list);
	kfree (iso_sched);
}

static int
itd_urb_transaction (
	struct fusbh200_iso_stream	*stream,
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	gfp_t			mem_flags
)
{
	struct fusbh200_itd		*itd;
	dma_addr_t		itd_dma;
	int			i;
	unsigned		num_itds;
	struct fusbh200_iso_sched	*sched;
	unsigned long		flags;

	sched = iso_sched_alloc (urb->number_of_packets, mem_flags);
	if (unlikely (sched == NULL))
		return -ENOMEM;

	itd_sched_init(fusbh200, sched, stream, urb);

	if (urb->interval < 8)
		num_itds = 1 + (sched->span + 7) / 8;
	else
		num_itds = urb->number_of_packets;

	/* allocate/init ITDs */
	spin_lock_irqsave (&fusbh200->lock, flags);
	for (i = 0; i < num_itds; i++) {

		/*
		 * Use iTDs from the free list, but not iTDs that may
		 * still be in use by the hardware.
		 */
		if (likely(!list_empty(&stream->free_list))) {
			itd = list_first_entry(&stream->free_list,
					struct fusbh200_itd, itd_list);
			if (itd->frame == fusbh200->now_frame)
				goto alloc_itd;
			list_del (&itd->itd_list);
			itd_dma = itd->itd_dma;
		} else {
 alloc_itd:
			spin_unlock_irqrestore (&fusbh200->lock, flags);
			itd = dma_pool_alloc (fusbh200->itd_pool, mem_flags,
					&itd_dma);
			spin_lock_irqsave (&fusbh200->lock, flags);
			if (!itd) {
				iso_sched_free(stream, sched);
				spin_unlock_irqrestore(&fusbh200->lock, flags);
				return -ENOMEM;
			}
		}

		memset (itd, 0, sizeof *itd);
		itd->itd_dma = itd_dma;
		list_add (&itd->itd_list, &sched->td_list);
	}
	spin_unlock_irqrestore (&fusbh200->lock, flags);

	/* temporarily store schedule info in hcpriv */
	urb->hcpriv = sched;
	urb->error_count = 0;
	return 0;
}

/*-------------------------------------------------------------------------*/

static inline int
itd_slot_ok (
	struct fusbh200_hcd		*fusbh200,
	u32			mod,
	u32			uframe,
	u8			usecs,
	u32			period
)
{
	uframe %= period;
	do {
		/* can't commit more than uframe_periodic_max usec */
		if (periodic_usecs (fusbh200, uframe >> 3, uframe & 0x7)
				> (fusbh200->uframe_periodic_max - usecs))
			return 0;

		/* we know urb->interval is 2^N uframes */
		uframe += period;
	} while (uframe < mod);
	return 1;
}

/*
 * This scheduler plans almost as far into the future as it has actual
 * periodic schedule slots.  (Affected by TUNE_FLS, which defaults to
 * "as small as possible" to be cache-friendlier.)  That limits the size
 * transfers you can stream reliably; avoid more than 64 msec per urb.
 * Also avoid queue depths of less than fusbh200's worst irq latency (affected
 * by the per-urb URB_NO_INTERRUPT hint, the log2_irq_thresh module parameter,
 * and other factors); or more than about 230 msec total (for portability,
 * given FUSBH200_TUNE_FLS and the slop).  Or, write a smarter scheduler!
 */

#define SCHEDULE_SLOP	80	/* microframes */

static int
iso_stream_schedule (
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	struct fusbh200_iso_stream	*stream
)
{
	u32			now, next, start, period, span;
	int			status;
	unsigned		mod = fusbh200->periodic_size << 3;
	struct fusbh200_iso_sched	*sched = urb->hcpriv;

	period = urb->interval;
	span = sched->span;

	if (span > mod - SCHEDULE_SLOP) {
		fusbh200_dbg (fusbh200, "iso request %p too long\n", urb);
		status = -EFBIG;
		goto fail;
	}

	now = fusbh200_read_frame_index(fusbh200) & (mod - 1);

	/* Typical case: reuse current schedule, stream is still active.
	 * Hopefully there are no gaps from the host falling behind
	 * (irq delays etc), but if there are we'll take the next
	 * slot in the schedule, implicitly assuming URB_ISO_ASAP.
	 */
	if (likely (!list_empty (&stream->td_list))) {
		u32	excess;

		/* For high speed devices, allow scheduling within the
		 * isochronous scheduling threshold.  For full speed devices
		 * and Intel PCI-based controllers, don't (work around for
		 * Intel ICH9 bug).
		 */
		if (!stream->highspeed && fusbh200->fs_i_thresh)
			next = now + fusbh200->i_thresh;
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
			fusbh200_dbg(fusbh200, "request %p would overflow (%d+%d >= %d)\n",
					urb, start - now - period, period,
					mod);
			status = -EFBIG;
			goto fail;
		}
	}

	/* need to schedule; when's the next (u)frame we could start?
	 * this is bigger than fusbh200->i_thresh allows; scheduling itself
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
			if (itd_slot_ok(fusbh200, mod, start,
					stream->usecs, period))
				done = 1;
		} while (start > next && !done);

		/* no room in the schedule */
		if (!done) {
			fusbh200_dbg(fusbh200, "iso resched full %p (now %d max %d)\n",
				urb, now, now + mod);
			status = -ENOSPC;
			goto fail;
		}
	}

	/* Tried to schedule too far into the future? */
	if (unlikely(start - now + span - period
				>= mod - 2 * SCHEDULE_SLOP)) {
		fusbh200_dbg(fusbh200, "request %p would overflow (%d+%d >= %d)\n",
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
	if (fusbh200->isoc_count == 0)
		fusbh200->next_frame = now >> 3;
	return 0;

 fail:
	iso_sched_free(stream, sched);
	urb->hcpriv = NULL;
	return status;
}

/*-------------------------------------------------------------------------*/

static inline void
itd_init(struct fusbh200_hcd *fusbh200, struct fusbh200_iso_stream *stream,
		struct fusbh200_itd *itd)
{
	int i;

	/* it's been recently zeroed */
	itd->hw_next = FUSBH200_LIST_END(fusbh200);
	itd->hw_bufp [0] = stream->buf0;
	itd->hw_bufp [1] = stream->buf1;
	itd->hw_bufp [2] = stream->buf2;

	for (i = 0; i < 8; i++)
		itd->index[i] = -1;

	/* All other fields are filled when scheduling */
}

static inline void
itd_patch(
	struct fusbh200_hcd		*fusbh200,
	struct fusbh200_itd		*itd,
	struct fusbh200_iso_sched	*iso_sched,
	unsigned		index,
	u16			uframe
)
{
	struct fusbh200_iso_packet	*uf = &iso_sched->packet [index];
	unsigned		pg = itd->pg;

	// BUG_ON (pg == 6 && uf->cross);

	uframe &= 0x07;
	itd->index [uframe] = index;

	itd->hw_transaction[uframe] = uf->transaction;
	itd->hw_transaction[uframe] |= cpu_to_hc32(fusbh200, pg << 12);
	itd->hw_bufp[pg] |= cpu_to_hc32(fusbh200, uf->bufp & ~(u32)0);
	itd->hw_bufp_hi[pg] |= cpu_to_hc32(fusbh200, (u32)(uf->bufp >> 32));

	/* iso_frame_desc[].offset must be strictly increasing */
	if (unlikely (uf->cross)) {
		u64	bufp = uf->bufp + 4096;

		itd->pg = ++pg;
		itd->hw_bufp[pg] |= cpu_to_hc32(fusbh200, bufp & ~(u32)0);
		itd->hw_bufp_hi[pg] |= cpu_to_hc32(fusbh200, (u32)(bufp >> 32));
	}
}

static inline void
itd_link (struct fusbh200_hcd *fusbh200, unsigned frame, struct fusbh200_itd *itd)
{
	union fusbh200_shadow	*prev = &fusbh200->pshadow[frame];
	__hc32			*hw_p = &fusbh200->periodic[frame];
	union fusbh200_shadow	here = *prev;
	__hc32			type = 0;

	/* skip any iso nodes which might belong to previous microframes */
	while (here.ptr) {
		type = Q_NEXT_TYPE(fusbh200, *hw_p);
		if (type == cpu_to_hc32(fusbh200, Q_TYPE_QH))
			break;
		prev = periodic_next_shadow(fusbh200, prev, type);
		hw_p = shadow_next_periodic(fusbh200, &here, type);
		here = *prev;
	}

	itd->itd_next = here;
	itd->hw_next = *hw_p;
	prev->itd = itd;
	itd->frame = frame;
	wmb ();
	*hw_p = cpu_to_hc32(fusbh200, itd->itd_dma | Q_TYPE_ITD);
}

/* fit urb's itds into the selected schedule slot; activate as needed */
static void itd_link_urb(
	struct fusbh200_hcd		*fusbh200,
	struct urb		*urb,
	unsigned		mod,
	struct fusbh200_iso_stream	*stream
)
{
	int			packet;
	unsigned		next_uframe, uframe, frame;
	struct fusbh200_iso_sched	*iso_sched = urb->hcpriv;
	struct fusbh200_itd		*itd;

	next_uframe = stream->next_uframe & (mod - 1);

	if (unlikely (list_empty(&stream->td_list))) {
		fusbh200_to_hcd(fusbh200)->self.bandwidth_allocated
				+= stream->bandwidth;
		fusbh200_dbg(fusbh200,
			"schedule devp %s ep%d%s-iso period %d start %d.%d\n",
			urb->dev->devpath, stream->bEndpointAddress & 0x0f,
			(stream->bEndpointAddress & USB_DIR_IN) ? "in" : "out",
			urb->interval,
			next_uframe >> 3, next_uframe & 0x7);
	}

	/* fill iTDs uframe by uframe */
	for (packet = 0, itd = NULL; packet < urb->number_of_packets; ) {
		if (itd == NULL) {
			/* ASSERT:  we have all necessary itds */
			// BUG_ON (list_empty (&iso_sched->td_list));

			/* ASSERT:  no itds for this endpoint in this uframe */

			itd = list_entry (iso_sched->td_list.next,
					struct fusbh200_itd, itd_list);
			list_move_tail (&itd->itd_list, &stream->td_list);
			itd->stream = stream;
			itd->urb = urb;
			itd_init (fusbh200, stream, itd);
		}

		uframe = next_uframe & 0x07;
		frame = next_uframe >> 3;

		itd_patch(fusbh200, itd, iso_sched, packet, uframe);

		next_uframe += stream->interval;
		next_uframe &= mod - 1;
		packet++;

		/* link completed itds into the schedule */
		if (((next_uframe >> 3) != frame)
				|| packet == urb->number_of_packets) {
			itd_link(fusbh200, frame & (fusbh200->periodic_size - 1), itd);
			itd = NULL;
		}
	}
	stream->next_uframe = next_uframe;

	/* don't need that schedule data any more */
	iso_sched_free (stream, iso_sched);
	urb->hcpriv = NULL;

	++fusbh200->isoc_count;
	enable_periodic(fusbh200);
}

#define	ISO_ERRS (FUSBH200_ISOC_BUF_ERR | FUSBH200_ISOC_BABBLE | FUSBH200_ISOC_XACTERR)

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
static bool itd_complete(struct fusbh200_hcd *fusbh200, struct fusbh200_itd *itd)
{
	struct urb				*urb = itd->urb;
	struct usb_iso_packet_descriptor	*desc;
	u32					t;
	unsigned				uframe;
	int					urb_index = -1;
	struct fusbh200_iso_stream			*stream = itd->stream;
	struct usb_device			*dev;
	bool					retval = false;

	/* for each uframe with a packet */
	for (uframe = 0; uframe < 8; uframe++) {
		if (likely (itd->index[uframe] == -1))
			continue;
		urb_index = itd->index[uframe];
		desc = &urb->iso_frame_desc [urb_index];

		t = hc32_to_cpup(fusbh200, &itd->hw_transaction [uframe]);
		itd->hw_transaction [uframe] = 0;

		/* report transfer status */
		if (unlikely (t & ISO_ERRS)) {
			urb->error_count++;
			if (t & FUSBH200_ISOC_BUF_ERR)
				desc->status = usb_pipein (urb->pipe)
					? -ENOSR  /* hc couldn't read */
					: -ECOMM; /* hc couldn't write */
			else if (t & FUSBH200_ISOC_BABBLE)
				desc->status = -EOVERFLOW;
			else /* (t & FUSBH200_ISOC_XACTERR) */
				desc->status = -EPROTO;

			/* HC need not update length with this error */
			if (!(t & FUSBH200_ISOC_BABBLE)) {
				desc->actual_length = fusbh200_itdlen(urb, desc, t);
				urb->actual_length += desc->actual_length;
			}
		} else if (likely ((t & FUSBH200_ISOC_ACTIVE) == 0)) {
			desc->status = 0;
			desc->actual_length = fusbh200_itdlen(urb, desc, t);
			urb->actual_length += desc->actual_length;
		} else {
			/* URB was too late */
			desc->status = -EXDEV;
		}
	}

	/* handle completion now? */
	if (likely ((urb_index + 1) != urb->number_of_packets))
		goto done;

	/* ASSERT: it's really the last itd for this urb
	list_for_each_entry (itd, &stream->td_list, itd_list)
		BUG_ON (itd->urb == urb);
	 */

	/* give urb back to the driver; completion often (re)submits */
	dev = urb->dev;
	fusbh200_urb_done(fusbh200, urb, 0);
	retval = true;
	urb = NULL;

	--fusbh200->isoc_count;
	disable_periodic(fusbh200);

	if (unlikely(list_is_singular(&stream->td_list))) {
		fusbh200_to_hcd(fusbh200)->self.bandwidth_allocated
				-= stream->bandwidth;
		fusbh200_dbg(fusbh200,
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
				&fusbh200->cached_itd_list);
		start_free_itds(fusbh200);
	}

	return retval;
}

/*-------------------------------------------------------------------------*/

static int itd_submit (struct fusbh200_hcd *fusbh200, struct urb *urb,
	gfp_t mem_flags)
{
	int			status = -EINVAL;
	unsigned long		flags;
	struct fusbh200_iso_stream	*stream;

	/* Get iso_stream head */
	stream = iso_stream_find (fusbh200, urb);
	if (unlikely (stream == NULL)) {
		fusbh200_dbg (fusbh200, "can't get iso stream\n");
		return -ENOMEM;
	}
	if (unlikely (urb->interval != stream->interval &&
		      fusbh200_port_speed(fusbh200, 0) == USB_PORT_STAT_HIGH_SPEED)) {
			fusbh200_dbg (fusbh200, "can't change iso interval %d --> %d\n",
				stream->interval, urb->interval);
			goto done;
	}

#ifdef FUSBH200_URB_TRACE
	fusbh200_dbg (fusbh200,
		"%s %s urb %p ep%d%s len %d, %d pkts %d uframes [%p]\n",
		__func__, urb->dev->devpath, urb,
		usb_pipeendpoint (urb->pipe),
		usb_pipein (urb->pipe) ? "in" : "out",
		urb->transfer_buffer_length,
		urb->number_of_packets, urb->interval,
		stream);
#endif

	/* allocate ITDs w/o locking anything */
	status = itd_urb_transaction (stream, fusbh200, urb, mem_flags);
	if (unlikely (status < 0)) {
		fusbh200_dbg (fusbh200, "can't init itds\n");
		goto done;
	}

	/* schedule ... need to lock */
	spin_lock_irqsave (&fusbh200->lock, flags);
	if (unlikely(!HCD_HW_ACCESSIBLE(fusbh200_to_hcd(fusbh200)))) {
		status = -ESHUTDOWN;
		goto done_not_linked;
	}
	status = usb_hcd_link_urb_to_ep(fusbh200_to_hcd(fusbh200), urb);
	if (unlikely(status))
		goto done_not_linked;
	status = iso_stream_schedule(fusbh200, urb, stream);
	if (likely (status == 0))
		itd_link_urb (fusbh200, urb, fusbh200->periodic_size << 3, stream);
	else
		usb_hcd_unlink_urb_from_ep(fusbh200_to_hcd(fusbh200), urb);
 done_not_linked:
	spin_unlock_irqrestore (&fusbh200->lock, flags);
 done:
	return status;
}

/*-------------------------------------------------------------------------*/

static void scan_isoc(struct fusbh200_hcd *fusbh200)
{
	unsigned	uf, now_frame, frame;
	unsigned	fmask = fusbh200->periodic_size - 1;
	bool		modified, live;

	/*
	 * When running, scan from last scan point up to "now"
	 * else clean up by scanning everything that's left.
	 * Touches as few pages as possible:  cache-friendly.
	 */
	if (fusbh200->rh_state >= FUSBH200_RH_RUNNING) {
		uf = fusbh200_read_frame_index(fusbh200);
		now_frame = (uf >> 3) & fmask;
		live = true;
	} else  {
		now_frame = (fusbh200->next_frame - 1) & fmask;
		live = false;
	}
	fusbh200->now_frame = now_frame;

	frame = fusbh200->next_frame;
	for (;;) {
		union fusbh200_shadow	q, *q_p;
		__hc32			type, *hw_p;

restart:
		/* scan each element in frame's queue for completions */
		q_p = &fusbh200->pshadow [frame];
		hw_p = &fusbh200->periodic [frame];
		q.ptr = q_p->ptr;
		type = Q_NEXT_TYPE(fusbh200, *hw_p);
		modified = false;

		while (q.ptr != NULL) {
			switch (hc32_to_cpu(fusbh200, type)) {
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
							    ITD_ACTIVE(fusbh200))
							break;
					}
					if (uf < 8) {
						q_p = &q.itd->itd_next;
						hw_p = &q.itd->hw_next;
						type = Q_NEXT_TYPE(fusbh200,
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
				type = Q_NEXT_TYPE(fusbh200, q.itd->hw_next);
				wmb();
				modified = itd_complete (fusbh200, q.itd);
				q = *q_p;
				break;
			default:
				fusbh200_dbg(fusbh200, "corrupt type %d frame %d shadow %p\n",
					type, frame, q.ptr);
				// BUG ();
				/* FALL THROUGH */
			case Q_TYPE_QH:
			case Q_TYPE_FSTN:
				/* End of the iTDs and siTDs */
				q.ptr = NULL;
				break;
			}

			/* assume completion callbacks modify the queue */
			if (unlikely(modified && fusbh200->isoc_count > 0))
				goto restart;
		}

		/* Stop when we have reached the current frame */
		if (frame == now_frame)
			break;
		frame = (frame + 1) & fmask;
	}
	fusbh200->next_frame = now_frame;
}
/*-------------------------------------------------------------------------*/
/*
 * Display / Set uframe_periodic_max
 */
static ssize_t show_uframe_periodic_max(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct fusbh200_hcd		*fusbh200;
	int			n;

	fusbh200 = hcd_to_fusbh200(bus_to_hcd(dev_get_drvdata(dev)));
	n = scnprintf(buf, PAGE_SIZE, "%d\n", fusbh200->uframe_periodic_max);
	return n;
}


static ssize_t store_uframe_periodic_max(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct fusbh200_hcd		*fusbh200;
	unsigned		uframe_periodic_max;
	unsigned		frame, uframe;
	unsigned short		allocated_max;
	unsigned long		flags;
	ssize_t			ret;

	fusbh200 = hcd_to_fusbh200(bus_to_hcd(dev_get_drvdata(dev)));
	if (kstrtouint(buf, 0, &uframe_periodic_max) < 0)
		return -EINVAL;

	if (uframe_periodic_max < 100 || uframe_periodic_max >= 125) {
		fusbh200_info(fusbh200, "rejecting invalid request for "
				"uframe_periodic_max=%u\n", uframe_periodic_max);
		return -EINVAL;
	}

	ret = -EINVAL;

	/*
	 * lock, so that our checking does not race with possible periodic
	 * bandwidth allocation through submitting new urbs.
	 */
	spin_lock_irqsave (&fusbh200->lock, flags);

	/*
	 * for request to decrease max periodic bandwidth, we have to check
	 * every microframe in the schedule to see whether the decrease is
	 * possible.
	 */
	if (uframe_periodic_max < fusbh200->uframe_periodic_max) {
		allocated_max = 0;

		for (frame = 0; frame < fusbh200->periodic_size; ++frame)
			for (uframe = 0; uframe < 7; ++uframe)
				allocated_max = max(allocated_max,
						    periodic_usecs (fusbh200, frame, uframe));

		if (allocated_max > uframe_periodic_max) {
			fusbh200_info(fusbh200,
				"cannot decrease uframe_periodic_max because "
				"periodic bandwidth is already allocated "
				"(%u > %u)\n",
				allocated_max, uframe_periodic_max);
			goto out_unlock;
		}
	}

	/* increasing is always ok */

	fusbh200_info(fusbh200, "setting max periodic bandwidth to %u%% "
			"(== %u usec/uframe)\n",
			100*uframe_periodic_max/125, uframe_periodic_max);

	if (uframe_periodic_max != 100)
		fusbh200_warn(fusbh200, "max periodic bandwidth set is non-standard\n");

	fusbh200->uframe_periodic_max = uframe_periodic_max;
	ret = count;

out_unlock:
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	return ret;
}
static DEVICE_ATTR(uframe_periodic_max, 0644, show_uframe_periodic_max, store_uframe_periodic_max);


static inline int create_sysfs_files(struct fusbh200_hcd *fusbh200)
{
	struct device	*controller = fusbh200_to_hcd(fusbh200)->self.controller;
	int	i = 0;

	if (i)
		goto out;

	i = device_create_file(controller, &dev_attr_uframe_periodic_max);
out:
	return i;
}

static inline void remove_sysfs_files(struct fusbh200_hcd *fusbh200)
{
	struct device	*controller = fusbh200_to_hcd(fusbh200)->self.controller;

	device_remove_file(controller, &dev_attr_uframe_periodic_max);
}
/*-------------------------------------------------------------------------*/

/* On some systems, leaving remote wakeup enabled prevents system shutdown.
 * The firmware seems to think that powering off is a wakeup event!
 * This routine turns off remote wakeup and everything else, on all ports.
 */
static void fusbh200_turn_off_all_ports(struct fusbh200_hcd *fusbh200)
{
	u32 __iomem *status_reg = &fusbh200->regs->port_status;

	fusbh200_writel(fusbh200, PORT_RWC_BITS, status_reg);
}

/*
 * Halt HC, turn off all ports, and let the BIOS use the companion controllers.
 * Must be called with interrupts enabled and the lock not held.
 */
static void fusbh200_silence_controller(struct fusbh200_hcd *fusbh200)
{
	fusbh200_halt(fusbh200);

	spin_lock_irq(&fusbh200->lock);
	fusbh200->rh_state = FUSBH200_RH_HALTED;
	fusbh200_turn_off_all_ports(fusbh200);
	spin_unlock_irq(&fusbh200->lock);
}

/* fusbh200_shutdown kick in for silicon on any bus (not just pci, etc).
 * This forcibly disables dma and IRQs, helping kexec and other cases
 * where the next system software may expect clean state.
 */
static void fusbh200_shutdown(struct usb_hcd *hcd)
{
	struct fusbh200_hcd	*fusbh200 = hcd_to_fusbh200(hcd);

	spin_lock_irq(&fusbh200->lock);
	fusbh200->shutdown = true;
	fusbh200->rh_state = FUSBH200_RH_STOPPING;
	fusbh200->enabled_hrtimer_events = 0;
	spin_unlock_irq(&fusbh200->lock);

	fusbh200_silence_controller(fusbh200);

	hrtimer_cancel(&fusbh200->hrtimer);
}

/*-------------------------------------------------------------------------*/

/*
 * fusbh200_work is called from some interrupts, timers, and so on.
 * it calls driver completion functions, after dropping fusbh200->lock.
 */
static void fusbh200_work (struct fusbh200_hcd *fusbh200)
{
	/* another CPU may drop fusbh200->lock during a schedule scan while
	 * it reports urb completions.  this flag guards against bogus
	 * attempts at re-entrant schedule scanning.
	 */
	if (fusbh200->scanning) {
		fusbh200->need_rescan = true;
		return;
	}
	fusbh200->scanning = true;

 rescan:
	fusbh200->need_rescan = false;
	if (fusbh200->async_count)
		scan_async(fusbh200);
	if (fusbh200->intr_count > 0)
		scan_intr(fusbh200);
	if (fusbh200->isoc_count > 0)
		scan_isoc(fusbh200);
	if (fusbh200->need_rescan)
		goto rescan;
	fusbh200->scanning = false;

	/* the IO watchdog guards against hardware or driver bugs that
	 * misplace IRQs, and should let us run completely without IRQs.
	 * such lossage has been observed on both VT6202 and VT8235.
	 */
	turn_on_io_watchdog(fusbh200);
}

/*
 * Called when the fusbh200_hcd module is removed.
 */
static void fusbh200_stop (struct usb_hcd *hcd)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);

	fusbh200_dbg (fusbh200, "stop\n");

	/* no more interrupts ... */

	spin_lock_irq(&fusbh200->lock);
	fusbh200->enabled_hrtimer_events = 0;
	spin_unlock_irq(&fusbh200->lock);

	fusbh200_quiesce(fusbh200);
	fusbh200_silence_controller(fusbh200);
	fusbh200_reset (fusbh200);

	hrtimer_cancel(&fusbh200->hrtimer);
	remove_sysfs_files(fusbh200);
	remove_debug_files (fusbh200);

	/* root hub is shut down separately (first, when possible) */
	spin_lock_irq (&fusbh200->lock);
	end_free_itds(fusbh200);
	spin_unlock_irq (&fusbh200->lock);
	fusbh200_mem_cleanup (fusbh200);

	fusbh200_dbg(fusbh200, "irq normal %ld err %ld iaa %ld (lost %ld)\n",
		fusbh200->stats.normal, fusbh200->stats.error, fusbh200->stats.iaa,
		fusbh200->stats.lost_iaa);
	fusbh200_dbg (fusbh200, "complete %ld unlink %ld\n",
		fusbh200->stats.complete, fusbh200->stats.unlink);

	dbg_status (fusbh200, "fusbh200_stop completed",
		    fusbh200_readl(fusbh200, &fusbh200->regs->status));
}

/* one-time init, only for memory state */
static int hcd_fusbh200_init(struct usb_hcd *hcd)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200(hcd);
	u32			temp;
	int			retval;
	u32			hcc_params;
	struct fusbh200_qh_hw	*hw;

	spin_lock_init(&fusbh200->lock);

	/*
	 * keep io watchdog by default, those good HCDs could turn off it later
	 */
	fusbh200->need_io_watchdog = 1;

	hrtimer_init(&fusbh200->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fusbh200->hrtimer.function = fusbh200_hrtimer_func;
	fusbh200->next_hrtimer_event = FUSBH200_HRTIMER_NO_EVENT;

	hcc_params = fusbh200_readl(fusbh200, &fusbh200->caps->hcc_params);

	/*
	 * by default set standard 80% (== 100 usec/uframe) max periodic
	 * bandwidth as required by USB 2.0
	 */
	fusbh200->uframe_periodic_max = 100;

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	fusbh200->periodic_size = DEFAULT_I_TDPS;
	INIT_LIST_HEAD(&fusbh200->intr_qh_list);
	INIT_LIST_HEAD(&fusbh200->cached_itd_list);

	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		switch (FUSBH200_TUNE_FLS) {
		case 0: fusbh200->periodic_size = 1024; break;
		case 1: fusbh200->periodic_size = 512; break;
		case 2: fusbh200->periodic_size = 256; break;
		default:	BUG();
		}
	}
	if ((retval = fusbh200_mem_init(fusbh200, GFP_KERNEL)) < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	fusbh200->i_thresh = 2;

	/*
	 * dedicate a qh for the async ring head, since we couldn't unlink
	 * a 'real' qh without stopping the async schedule [4.8].  use it
	 * as the 'reclamation list head' too.
	 * its dummy is used in hw_alt_next of many tds, to prevent the qh
	 * from automatically advancing to the next td after short reads.
	 */
	fusbh200->async->qh_next.qh = NULL;
	hw = fusbh200->async->hw;
	hw->hw_next = QH_NEXT(fusbh200, fusbh200->async->qh_dma);
	hw->hw_info1 = cpu_to_hc32(fusbh200, QH_HEAD);
	hw->hw_token = cpu_to_hc32(fusbh200, QTD_STS_HALT);
	hw->hw_qtd_next = FUSBH200_LIST_END(fusbh200);
	fusbh200->async->qh_state = QH_STATE_LINKED;
	hw->hw_alt_next = QTD_NEXT(fusbh200, fusbh200->async->dummy->qtd_dma);

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
			park = min(park, (unsigned) 3);
			temp |= CMD_PARK;
			temp |= park << 8;
		}
		fusbh200_dbg(fusbh200, "park %d\n", park);
	}
	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		temp &= ~(3 << 2);
		temp |= (FUSBH200_TUNE_FLS << 2);
	}
	fusbh200->command = temp;

	/* Accept arbitrarily long scatter-gather lists */
	if (!(hcd->driver->flags & HCD_LOCAL_MEM))
		hcd->self.sg_tablesize = ~0;
	return 0;
}

/* start HC running; it's halted, hcd_fusbh200_init() has been run (once) */
static int fusbh200_run (struct usb_hcd *hcd)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	u32			temp;
	u32			hcc_params;

	hcd->uses_new_polling = 1;

	/* EHCI spec section 4.1 */

	fusbh200_writel(fusbh200, fusbh200->periodic_dma, &fusbh200->regs->frame_list);
	fusbh200_writel(fusbh200, (u32)fusbh200->async->qh_dma, &fusbh200->regs->async_next);

	/*
	 * hcc_params controls whether fusbh200->regs->segment must (!!!)
	 * be used; it constrains QH/ITD/SITD and QTD locations.
	 * pci_pool consistent memory always uses segment zero.
	 * streaming mappings for I/O buffers, like pci_map_single(),
	 * can return segments above 4GB, if the device allows.
	 *
	 * NOTE:  the dma mask is visible through dma_supported(), so
	 * drivers can pass this info along ... like NETIF_F_HIGHDMA,
	 * Scsi_Host.highmem_io, and so forth.  It's readonly to all
	 * host side drivers though.
	 */
	hcc_params = fusbh200_readl(fusbh200, &fusbh200->caps->hcc_params);

	// Philips, Intel, and maybe others need CMD_RUN before the
	// root hub will detect new devices (why?); NEC doesn't
	fusbh200->command &= ~(CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	fusbh200->command |= CMD_RUN;
	fusbh200_writel(fusbh200, fusbh200->command, &fusbh200->regs->command);
	dbg_cmd (fusbh200, "init", fusbh200->command);

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
	fusbh200->rh_state = FUSBH200_RH_RUNNING;
	fusbh200_readl(fusbh200, &fusbh200->regs->command);	/* unblock posted writes */
	msleep(5);
	up_write(&ehci_cf_port_reset_rwsem);
	fusbh200->last_periodic_enable = ktime_get_real();

	temp = HC_VERSION(fusbh200, fusbh200_readl(fusbh200, &fusbh200->caps->hc_capbase));
	fusbh200_info (fusbh200,
		"USB %x.%x started, EHCI %x.%02x\n",
		((fusbh200->sbrn & 0xf0)>>4), (fusbh200->sbrn & 0x0f),
		temp >> 8, temp & 0xff);

	fusbh200_writel(fusbh200, INTR_MASK,
		    &fusbh200->regs->intr_enable); /* Turn On Interrupts */

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	create_debug_files(fusbh200);
	create_sysfs_files(fusbh200);

	return 0;
}

static int fusbh200_setup(struct usb_hcd *hcd)
{
	struct fusbh200_hcd *fusbh200 = hcd_to_fusbh200(hcd);
	int retval;

	fusbh200->regs = (void __iomem *)fusbh200->caps +
	    HC_LENGTH(fusbh200, fusbh200_readl(fusbh200, &fusbh200->caps->hc_capbase));
	dbg_hcs_params(fusbh200, "reset");
	dbg_hcc_params(fusbh200, "reset");

	/* cache this readonly data; minimize chip reads */
	fusbh200->hcs_params = fusbh200_readl(fusbh200, &fusbh200->caps->hcs_params);

	fusbh200->sbrn = HCD_USB2;

	/* data structure init */
	retval = hcd_fusbh200_init(hcd);
	if (retval)
		return retval;

	retval = fusbh200_halt(fusbh200);
	if (retval)
		return retval;

	fusbh200_reset(fusbh200);

	return 0;
}

/*-------------------------------------------------------------------------*/

static irqreturn_t fusbh200_irq (struct usb_hcd *hcd)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	u32			status, masked_status, pcd_status = 0, cmd;
	int			bh;

	spin_lock (&fusbh200->lock);

	status = fusbh200_readl(fusbh200, &fusbh200->regs->status);

	/* e.g. cardbus physical eject */
	if (status == ~(u32) 0) {
		fusbh200_dbg (fusbh200, "device removed\n");
		goto dead;
	}

	/*
	 * We don't use STS_FLR, but some controllers don't like it to
	 * remain on, so mask it out along with the other status bits.
	 */
	masked_status = status & (INTR_MASK | STS_FLR);

	/* Shared IRQ? */
	if (!masked_status || unlikely(fusbh200->rh_state == FUSBH200_RH_HALTED)) {
		spin_unlock(&fusbh200->lock);
		return IRQ_NONE;
	}

	/* clear (just) interrupts */
	fusbh200_writel(fusbh200, masked_status, &fusbh200->regs->status);
	cmd = fusbh200_readl(fusbh200, &fusbh200->regs->command);
	bh = 0;

	/* normal [4.15.1.2] or error [4.15.1.1] completion */
	if (likely ((status & (STS_INT|STS_ERR)) != 0)) {
		if (likely ((status & STS_ERR) == 0))
			COUNT (fusbh200->stats.normal);
		else
			COUNT (fusbh200->stats.error);
		bh = 1;
	}

	/* complete the unlinking of some qh [4.15.2.3] */
	if (status & STS_IAA) {

		/* Turn off the IAA watchdog */
		fusbh200->enabled_hrtimer_events &= ~BIT(FUSBH200_HRTIMER_IAA_WATCHDOG);

		/*
		 * Mild optimization: Allow another IAAD to reset the
		 * hrtimer, if one occurs before the next expiration.
		 * In theory we could always cancel the hrtimer, but
		 * tests show that about half the time it will be reset
		 * for some other event anyway.
		 */
		if (fusbh200->next_hrtimer_event == FUSBH200_HRTIMER_IAA_WATCHDOG)
			++fusbh200->next_hrtimer_event;

		/* guard against (alleged) silicon errata */
		if (cmd & CMD_IAAD)
			fusbh200_dbg(fusbh200, "IAA with IAAD still set?\n");
		if (fusbh200->async_iaa) {
			COUNT(fusbh200->stats.iaa);
			end_unlink_async(fusbh200);
		} else
			fusbh200_dbg(fusbh200, "IAA with nothing unlinked?\n");
	}

	/* remote wakeup [4.3.1] */
	if (status & STS_PCD) {
		int pstatus;
		u32 __iomem *status_reg = &fusbh200->regs->port_status;

		/* kick root hub later */
		pcd_status = status;

		/* resume root hub? */
		if (fusbh200->rh_state == FUSBH200_RH_SUSPENDED)
			usb_hcd_resume_root_hub(hcd);

		pstatus = fusbh200_readl(fusbh200, status_reg);

		if (test_bit(0, &fusbh200->suspended_ports) &&
				((pstatus & PORT_RESUME) ||
					!(pstatus & PORT_SUSPEND)) &&
				(pstatus & PORT_PE) &&
				fusbh200->reset_done[0] == 0) {

			/* start 20 msec resume signaling from this port,
			 * and make hub_wq collect PORT_STAT_C_SUSPEND to
			 * stop that signaling.  Use 5 ms extra for safety,
			 * like usb_port_resume() does.
			 */
			fusbh200->reset_done[0] = jiffies + msecs_to_jiffies(25);
			set_bit(0, &fusbh200->resuming_ports);
			fusbh200_dbg (fusbh200, "port 1 remote wakeup\n");
			mod_timer(&hcd->rh_timer, fusbh200->reset_done[0]);
		}
	}

	/* PCI errors [4.15.2.4] */
	if (unlikely ((status & STS_FATAL) != 0)) {
		fusbh200_err(fusbh200, "fatal error\n");
		dbg_cmd(fusbh200, "fatal", cmd);
		dbg_status(fusbh200, "fatal", status);
dead:
		usb_hc_died(hcd);

		/* Don't let the controller do anything more */
		fusbh200->shutdown = true;
		fusbh200->rh_state = FUSBH200_RH_STOPPING;
		fusbh200->command &= ~(CMD_RUN | CMD_ASE | CMD_PSE);
		fusbh200_writel(fusbh200, fusbh200->command, &fusbh200->regs->command);
		fusbh200_writel(fusbh200, 0, &fusbh200->regs->intr_enable);
		fusbh200_handle_controller_death(fusbh200);

		/* Handle completions when the controller stops */
		bh = 0;
	}

	if (bh)
		fusbh200_work (fusbh200);
	spin_unlock (&fusbh200->lock);
	if (pcd_status)
		usb_hcd_poll_rh_status(hcd);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

/*
 * non-error returns are a promise to giveback() the urb later
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
static int fusbh200_urb_enqueue (
	struct usb_hcd	*hcd,
	struct urb	*urb,
	gfp_t		mem_flags
) {
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	struct list_head	qtd_list;

	INIT_LIST_HEAD (&qtd_list);

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_CONTROL:
		/* qh_completions() code doesn't handle all the fault cases
		 * in multi-TD control transfers.  Even 1KB is rare anyway.
		 */
		if (urb->transfer_buffer_length > (16 * 1024))
			return -EMSGSIZE;
		/* FALLTHROUGH */
	/* case PIPE_BULK: */
	default:
		if (!qh_urb_transaction (fusbh200, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return submit_async(fusbh200, urb, &qtd_list, mem_flags);

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction (fusbh200, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return intr_submit(fusbh200, urb, &qtd_list, mem_flags);

	case PIPE_ISOCHRONOUS:
		return itd_submit (fusbh200, urb, mem_flags);
	}
}

/* remove from hardware lists
 * completions normally happen asynchronously
 */

static int fusbh200_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	struct fusbh200_qh		*qh;
	unsigned long		flags;
	int			rc;

	spin_lock_irqsave (&fusbh200->lock, flags);
	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc)
		goto done;

	switch (usb_pipetype (urb->pipe)) {
	// case PIPE_CONTROL:
	// case PIPE_BULK:
	default:
		qh = (struct fusbh200_qh *) urb->hcpriv;
		if (!qh)
			break;
		switch (qh->qh_state) {
		case QH_STATE_LINKED:
		case QH_STATE_COMPLETING:
			start_unlink_async(fusbh200, qh);
			break;
		case QH_STATE_UNLINK:
		case QH_STATE_UNLINK_WAIT:
			/* already started */
			break;
		case QH_STATE_IDLE:
			/* QH might be waiting for a Clear-TT-Buffer */
			qh_completions(fusbh200, qh);
			break;
		}
		break;

	case PIPE_INTERRUPT:
		qh = (struct fusbh200_qh *) urb->hcpriv;
		if (!qh)
			break;
		switch (qh->qh_state) {
		case QH_STATE_LINKED:
		case QH_STATE_COMPLETING:
			start_unlink_intr(fusbh200, qh);
			break;
		case QH_STATE_IDLE:
			qh_completions (fusbh200, qh);
			break;
		default:
			fusbh200_dbg (fusbh200, "bogus qh %p state %d\n",
					qh, qh->qh_state);
			goto done;
		}
		break;

	case PIPE_ISOCHRONOUS:
		// itd...

		// wait till next completion, do it then.
		// completion irqs can wait up to 1024 msec,
		break;
	}
done:
	spin_unlock_irqrestore (&fusbh200->lock, flags);
	return rc;
}

/*-------------------------------------------------------------------------*/

// bulk qh holds the data toggle

static void
fusbh200_endpoint_disable (struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	unsigned long		flags;
	struct fusbh200_qh		*qh, *tmp;

	/* ASSERT:  any requests/urbs are being unlinked */
	/* ASSERT:  nobody can be submitting urbs for this any more */

rescan:
	spin_lock_irqsave (&fusbh200->lock, flags);
	qh = ep->hcpriv;
	if (!qh)
		goto done;

	/* endpoints can be iso streams.  for now, we don't
	 * accelerate iso completions ... so spin a while.
	 */
	if (qh->hw == NULL) {
		struct fusbh200_iso_stream	*stream = ep->hcpriv;

		if (!list_empty(&stream->td_list))
			goto idle_timeout;

		/* BUG_ON(!list_empty(&stream->free_list)); */
		kfree(stream);
		goto done;
	}

	if (fusbh200->rh_state < FUSBH200_RH_RUNNING)
		qh->qh_state = QH_STATE_IDLE;
	switch (qh->qh_state) {
	case QH_STATE_LINKED:
	case QH_STATE_COMPLETING:
		for (tmp = fusbh200->async->qh_next.qh;
				tmp && tmp != qh;
				tmp = tmp->qh_next.qh)
			continue;
		/* periodic qh self-unlinks on empty, and a COMPLETING qh
		 * may already be unlinked.
		 */
		if (tmp)
			start_unlink_async(fusbh200, qh);
		/* FALL THROUGH */
	case QH_STATE_UNLINK:		/* wait for hw to finish? */
	case QH_STATE_UNLINK_WAIT:
idle_timeout:
		spin_unlock_irqrestore (&fusbh200->lock, flags);
		schedule_timeout_uninterruptible(1);
		goto rescan;
	case QH_STATE_IDLE:		/* fully unlinked */
		if (qh->clearing_tt)
			goto idle_timeout;
		if (list_empty (&qh->qtd_list)) {
			qh_destroy(fusbh200, qh);
			break;
		}
		/* else FALL THROUGH */
	default:
		/* caller was supposed to have unlinked any requests;
		 * that's not our job.  just leak this memory.
		 */
		fusbh200_err (fusbh200, "qh %p (#%02x) state %d%s\n",
			qh, ep->desc.bEndpointAddress, qh->qh_state,
			list_empty (&qh->qtd_list) ? "" : "(has tds)");
		break;
	}
 done:
	ep->hcpriv = NULL;
	spin_unlock_irqrestore (&fusbh200->lock, flags);
}

static void
fusbh200_endpoint_reset(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200(hcd);
	struct fusbh200_qh		*qh;
	int			eptype = usb_endpoint_type(&ep->desc);
	int			epnum = usb_endpoint_num(&ep->desc);
	int			is_out = usb_endpoint_dir_out(&ep->desc);
	unsigned long		flags;

	if (eptype != USB_ENDPOINT_XFER_BULK && eptype != USB_ENDPOINT_XFER_INT)
		return;

	spin_lock_irqsave(&fusbh200->lock, flags);
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
				start_unlink_async(fusbh200, qh);
			else
				start_unlink_intr(fusbh200, qh);
		}
	}
	spin_unlock_irqrestore(&fusbh200->lock, flags);
}

static int fusbh200_get_frame (struct usb_hcd *hcd)
{
	struct fusbh200_hcd		*fusbh200 = hcd_to_fusbh200 (hcd);
	return (fusbh200_read_frame_index(fusbh200) >> 3) % fusbh200->periodic_size;
}

/*-------------------------------------------------------------------------*/

/*
 * The EHCI in ChipIdea HDRC cannot be a separate module or device,
 * because its registers (and irq) are shared between host/gadget/otg
 * functions  and in order to facilitate role switching we cannot
 * give the fusbh200 driver exclusive access to those.
 */
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_LICENSE ("GPL");

static const struct hc_driver fusbh200_fusbh200_hc_driver = {
	.description 		= hcd_name,
	.product_desc 		= "Faraday USB2.0 Host Controller",
	.hcd_priv_size 		= sizeof(struct fusbh200_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq 			= fusbh200_irq,
	.flags 			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset 			= hcd_fusbh200_init,
	.start 			= fusbh200_run,
	.stop 			= fusbh200_stop,
	.shutdown 		= fusbh200_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue 		= fusbh200_urb_enqueue,
	.urb_dequeue 		= fusbh200_urb_dequeue,
	.endpoint_disable 	= fusbh200_endpoint_disable,
	.endpoint_reset 	= fusbh200_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number 	= fusbh200_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data 	= fusbh200_hub_status_data,
	.hub_control 		= fusbh200_hub_control,
	.bus_suspend 		= fusbh200_bus_suspend,
	.bus_resume 		= fusbh200_bus_resume,

	.relinquish_port 	= fusbh200_relinquish_port,
	.port_handed_over 	= fusbh200_port_handed_over,

	.clear_tt_buffer_complete = fusbh200_clear_tt_buffer_complete,
};

static void fusbh200_init(struct fusbh200_hcd *fusbh200)
{
	u32 reg;

	reg = fusbh200_readl(fusbh200, &fusbh200->regs->bmcsr);
	reg |= BMCSR_INT_POLARITY;
	reg &= ~BMCSR_VBUS_OFF;
	fusbh200_writel(fusbh200, reg, &fusbh200->regs->bmcsr);

	reg = fusbh200_readl(fusbh200, &fusbh200->regs->bmier);
	fusbh200_writel(fusbh200, reg | BMIER_OVC_EN | BMIER_VBUS_ERR_EN,
		&fusbh200->regs->bmier);
}

/**
 * fusbh200_hcd_probe - initialize faraday FUSBH200 HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int fusbh200_hcd_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	struct usb_hcd 			*hcd;
	struct resource			*res;
	int 				irq;
	int 				retval = -ENODEV;
	struct fusbh200_hcd 		*fusbh200;

	if (usb_disabled())
		return -ENODEV;

	pdev->dev.power.power_state = PMSG_ON;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(dev));
		return -ENODEV;
	}

	irq = res->start;

	hcd = usb_create_hcd(&fusbh200_fusbh200_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "failed to create hcd with err %d\n", retval);
		retval = -ENOMEM;
		goto fail_create_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(dev));
		retval = -ENODEV;
		goto fail_request_resource;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->has_tt = 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				fusbh200_fusbh200_hc_driver.description)) {
		dev_dbg(dev, "controller already in use\n");
		retval = -EBUSY;
		goto fail_request_resource;
	}

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(dev));
		retval = -ENODEV;
		goto fail_request_resource;
	}

	hcd->regs = ioremap_nocache(res->start, resource_size(res));
	if (hcd->regs == NULL) {
		dev_dbg(dev, "error mapping memory\n");
		retval = -EFAULT;
		goto fail_ioremap;
	}

	fusbh200 = hcd_to_fusbh200(hcd);

	fusbh200->caps = hcd->regs;

	retval = fusbh200_setup(hcd);
	if (retval)
		goto fail_add_hcd;

	fusbh200_init(fusbh200);

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval) {
		dev_err(dev, "failed to add hcd with err %d\n", retval);
		goto fail_add_hcd;
	}
	device_wakeup_enable(hcd->self.controller);

	return retval;

fail_add_hcd:
	iounmap(hcd->regs);
fail_ioremap:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
fail_request_resource:
	usb_put_hcd(hcd);
fail_create_hcd:
	dev_err(dev, "init %s fail, %d\n", dev_name(dev), retval);
	return retval;
}

/**
 * fusbh200_hcd_remove - shutdown processing for EHCI HCDs
 * @dev: USB Host Controller being removed
 *
 * Reverses the effect of fotg2xx_usb_hcd_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int fusbh200_hcd_remove(struct platform_device *pdev)
{
	struct device *dev	= &pdev->dev;
	struct usb_hcd *hcd	= dev_get_drvdata(dev);

	if (!hcd)
		return 0;

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver fusbh200_hcd_fusbh200_driver = {
	.driver = {
		.name   = "fusbh200",
	},
	.probe  = fusbh200_hcd_probe,
	.remove = fusbh200_hcd_remove,
};

static int __init fusbh200_hcd_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	printk(KERN_INFO "%s: " DRIVER_DESC "\n", hcd_name);
	set_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	if (test_bit(USB_UHCI_LOADED, &usb_hcds_loaded) ||
			test_bit(USB_OHCI_LOADED, &usb_hcds_loaded))
		printk(KERN_WARNING "Warning! fusbh200_hcd should always be loaded"
				" before uhci_hcd and ohci_hcd, not after\n");

	pr_debug("%s: block sizes: qh %Zd qtd %Zd itd %Zd\n",
		 hcd_name,
		 sizeof(struct fusbh200_qh), sizeof(struct fusbh200_qtd),
		 sizeof(struct fusbh200_itd));

	fusbh200_debug_root = debugfs_create_dir("fusbh200", usb_debug_root);
	if (!fusbh200_debug_root) {
		retval = -ENOENT;
		goto err_debug;
	}

	retval = platform_driver_register(&fusbh200_hcd_fusbh200_driver);
	if (retval < 0)
		goto clean;
	return retval;

	platform_driver_unregister(&fusbh200_hcd_fusbh200_driver);
clean:
	debugfs_remove(fusbh200_debug_root);
	fusbh200_debug_root = NULL;
err_debug:
	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
	return retval;
}
module_init(fusbh200_hcd_init);

static void __exit fusbh200_hcd_cleanup(void)
{
	platform_driver_unregister(&fusbh200_hcd_fusbh200_driver);
	debugfs_remove(fusbh200_debug_root);
	clear_bit(USB_EHCI_LOADED, &usb_hcds_loaded);
}
module_exit(fusbh200_hcd_cleanup);
