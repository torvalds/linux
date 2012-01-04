/*
 * Copyright (c) 2001-2002 by David Brownell
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

/* this file is part of ehci-hcd.c */

#define ehci_dbg(ehci, fmt, args...) \
	dev_dbg (ehci_to_hcd(ehci)->self.controller , fmt , ## args )
#define ehci_err(ehci, fmt, args...) \
	dev_err (ehci_to_hcd(ehci)->self.controller , fmt , ## args )
#define ehci_info(ehci, fmt, args...) \
	dev_info (ehci_to_hcd(ehci)->self.controller , fmt , ## args )
#define ehci_warn(ehci, fmt, args...) \
	dev_warn (ehci_to_hcd(ehci)->self.controller , fmt , ## args )

#ifdef VERBOSE_DEBUG
#	define ehci_vdbg ehci_dbg
#else
	static inline void ehci_vdbg(struct ehci_hcd *ehci, ...) {}
#endif

#ifdef	DEBUG

/* check the values in the HCSPARAMS register
 * (host controller _Structural_ parameters)
 * see EHCI spec, Table 2-4 for each value
 */
static void dbg_hcs_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = ehci_readl(ehci, &ehci->caps->hcs_params);

	ehci_dbg (ehci,
		"%s hcs_params 0x%x dbg=%d%s cc=%d pcc=%d%s%s ports=%d\n",
		label, params,
		HCS_DEBUG_PORT (params),
		HCS_INDICATOR (params) ? " ind" : "",
		HCS_N_CC (params),
		HCS_N_PCC (params),
		HCS_PORTROUTED (params) ? "" : " ordered",
		HCS_PPC (params) ? "" : " !ppc",
		HCS_N_PORTS (params)
		);
	/* Port routing, per EHCI 0.95 Spec, Section 2.2.5 */
	if (HCS_PORTROUTED (params)) {
		int i;
		char buf [46], tmp [7], byte;

		buf[0] = 0;
		for (i = 0; i < HCS_N_PORTS (params); i++) {
			// FIXME MIPS won't readb() ...
			byte = readb (&ehci->caps->portroute[(i>>1)]);
			sprintf(tmp, "%d ",
				((i & 0x1) ? ((byte)&0xf) : ((byte>>4)&0xf)));
			strcat(buf, tmp);
		}
		ehci_dbg (ehci, "%s portroute %s\n",
				label, buf);
	}
}
#else

static inline void dbg_hcs_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

/* check the values in the HCCPARAMS register
 * (host controller _Capability_ parameters)
 * see EHCI Spec, Table 2-5 for each value
 * */
static void dbg_hcc_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = ehci_readl(ehci, &ehci->caps->hcc_params);

	if (HCC_ISOC_CACHE (params)) {
		ehci_dbg (ehci,
			"%s hcc_params %04x caching frame %s%s%s\n",
			label, params,
			HCC_PGM_FRAMELISTLEN(params) ? "256/512/1024" : "1024",
			HCC_CANPARK(params) ? " park" : "",
			HCC_64BIT_ADDR(params) ? " 64 bit addr" : "");
	} else {
		ehci_dbg (ehci,
			"%s hcc_params %04x thresh %d uframes %s%s%s%s%s%s%s\n",
			label,
			params,
			HCC_ISOC_THRES(params),
			HCC_PGM_FRAMELISTLEN(params) ? "256/512/1024" : "1024",
			HCC_CANPARK(params) ? " park" : "",
			HCC_64BIT_ADDR(params) ? " 64 bit addr" : "",
			HCC_LPM(params) ? " LPM" : "",
			HCC_PER_PORT_CHANGE_EVENT(params) ? " ppce" : "",
			HCC_HW_PREFETCH(params) ? " hw prefetch" : "",
			HCC_32FRAME_PERIODIC_LIST(params) ?
				" 32 peridic list" : "");
	}
}
#else

static inline void dbg_hcc_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

static void __maybe_unused
dbg_qtd (const char *label, struct ehci_hcd *ehci, struct ehci_qtd *qtd)
{
	ehci_dbg(ehci, "%s td %p n%08x %08x t%08x p0=%08x\n", label, qtd,
		hc32_to_cpup(ehci, &qtd->hw_next),
		hc32_to_cpup(ehci, &qtd->hw_alt_next),
		hc32_to_cpup(ehci, &qtd->hw_token),
		hc32_to_cpup(ehci, &qtd->hw_buf [0]));
	if (qtd->hw_buf [1])
		ehci_dbg(ehci, "  p1=%08x p2=%08x p3=%08x p4=%08x\n",
			hc32_to_cpup(ehci, &qtd->hw_buf[1]),
			hc32_to_cpup(ehci, &qtd->hw_buf[2]),
			hc32_to_cpup(ehci, &qtd->hw_buf[3]),
			hc32_to_cpup(ehci, &qtd->hw_buf[4]));
}

static void __maybe_unused
dbg_qh (const char *label, struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	struct ehci_qh_hw *hw = qh->hw;

	ehci_dbg (ehci, "%s qh %p n%08x info %x %x qtd %x\n", label,
		qh, hw->hw_next, hw->hw_info1, hw->hw_info2, hw->hw_current);
	dbg_qtd("overlay", ehci, (struct ehci_qtd *) &hw->hw_qtd_next);
}

static void __maybe_unused
dbg_itd (const char *label, struct ehci_hcd *ehci, struct ehci_itd *itd)
{
	ehci_dbg (ehci, "%s [%d] itd %p, next %08x, urb %p\n",
		label, itd->frame, itd, hc32_to_cpu(ehci, itd->hw_next),
		itd->urb);
	ehci_dbg (ehci,
		"  trans: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		hc32_to_cpu(ehci, itd->hw_transaction[0]),
		hc32_to_cpu(ehci, itd->hw_transaction[1]),
		hc32_to_cpu(ehci, itd->hw_transaction[2]),
		hc32_to_cpu(ehci, itd->hw_transaction[3]),
		hc32_to_cpu(ehci, itd->hw_transaction[4]),
		hc32_to_cpu(ehci, itd->hw_transaction[5]),
		hc32_to_cpu(ehci, itd->hw_transaction[6]),
		hc32_to_cpu(ehci, itd->hw_transaction[7]));
	ehci_dbg (ehci,
		"  buf:   %08x %08x %08x %08x %08x %08x %08x\n",
		hc32_to_cpu(ehci, itd->hw_bufp[0]),
		hc32_to_cpu(ehci, itd->hw_bufp[1]),
		hc32_to_cpu(ehci, itd->hw_bufp[2]),
		hc32_to_cpu(ehci, itd->hw_bufp[3]),
		hc32_to_cpu(ehci, itd->hw_bufp[4]),
		hc32_to_cpu(ehci, itd->hw_bufp[5]),
		hc32_to_cpu(ehci, itd->hw_bufp[6]));
	ehci_dbg (ehci, "  index: %d %d %d %d %d %d %d %d\n",
		itd->index[0], itd->index[1], itd->index[2],
		itd->index[3], itd->index[4], itd->index[5],
		itd->index[6], itd->index[7]);
}

static void __maybe_unused
dbg_sitd (const char *label, struct ehci_hcd *ehci, struct ehci_sitd *sitd)
{
	ehci_dbg (ehci, "%s [%d] sitd %p, next %08x, urb %p\n",
		label, sitd->frame, sitd, hc32_to_cpu(ehci, sitd->hw_next),
		sitd->urb);
	ehci_dbg (ehci,
		"  addr %08x sched %04x result %08x buf %08x %08x\n",
		hc32_to_cpu(ehci, sitd->hw_fullspeed_ep),
		hc32_to_cpu(ehci, sitd->hw_uframe),
		hc32_to_cpu(ehci, sitd->hw_results),
		hc32_to_cpu(ehci, sitd->hw_buf[0]),
		hc32_to_cpu(ehci, sitd->hw_buf[1]));
}

static int __maybe_unused
dbg_status_buf (char *buf, unsigned len, const char *label, u32 status)
{
	return scnprintf (buf, len,
		"%s%sstatus %04x%s%s%s%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", status,
		(status & STS_PPCE_MASK) ? " PPCE" : "",
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
		"%s%sintrenable %02x%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", enable,
		(enable & STS_PPCE_MASK) ? " PPCE" : "",
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
		"%s%scommand %07x %s%s%s%s%s%s=%d ithresh=%d%s%s%s%s "
		"period=%s%s %s",
		label, label [0] ? " " : "", command,
		(command & CMD_HIRD) ? " HIRD" : "",
		(command & CMD_PPCEE) ? " PPCEE" : "",
		(command & CMD_FSP) ? " FSP" : "",
		(command & CMD_ASPE) ? " ASPE" : "",
		(command & CMD_PSPE) ? " PSPE" : "",
		(command & CMD_PARK) ? " park" : "(park)",
		CMD_PARK_CNT (command),
		(command >> 16) & 0x3f,
		(command & CMD_LRESET) ? " LReset" : "",
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
		"%s%sport:%d status %06x %d %s%s%s%s%s%s "
		"sig=%s%s%s%s%s%s%s%s%s%s%s",
		label, label [0] ? " " : "", port, status,
		status>>25,/*device address */
		(status & PORT_SSTS)>>23 == PORTSC_SUSPEND_STS_ACK ?
						" ACK" : "",
		(status & PORT_SSTS)>>23 == PORTSC_SUSPEND_STS_NYET ?
						" NYET" : "",
		(status & PORT_SSTS)>>23 == PORTSC_SUSPEND_STS_STALL ?
						" STALL" : "",
		(status & PORT_SSTS)>>23 == PORTSC_SUSPEND_STS_ERR ?
						" ERR" : "",
		(status & PORT_POWER) ? " POWER" : "",
		(status & PORT_OWNER) ? " OWNER" : "",
		sig,
		(status & PORT_LPM) ? " LPM" : "",
		(status & PORT_RESET) ? " RESET" : "",
		(status & PORT_SUSPEND) ? " SUSPEND" : "",
		(status & PORT_RESUME) ? " RESUME" : "",
		(status & PORT_OCC) ? " OCC" : "",
		(status & PORT_OC) ? " OC" : "",
		(status & PORT_PEC) ? " PEC" : "",
		(status & PORT_PE) ? " PE" : "",
		(status & PORT_CSC) ? " CSC" : "",
		(status & PORT_CONNECT) ? " CONNECT" : "");
}

#else
static inline void __maybe_unused
dbg_qh (char *label, struct ehci_hcd *ehci, struct ehci_qh *qh)
{}

static inline int __maybe_unused
dbg_status_buf (char *buf, unsigned len, const char *label, u32 status)
{ return 0; }

static inline int __maybe_unused
dbg_command_buf (char *buf, unsigned len, const char *label, u32 command)
{ return 0; }

static inline int __maybe_unused
dbg_intr_buf (char *buf, unsigned len, const char *label, u32 enable)
{ return 0; }

static inline int __maybe_unused
dbg_port_buf (char *buf, unsigned len, const char *label, int port, u32 status)
{ return 0; }

#endif	/* DEBUG */

/* functions have the "wrong" filename when they're output... */
#define dbg_status(ehci, label, status) { \
	char _buf [80]; \
	dbg_status_buf (_buf, sizeof _buf, label, status); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

#define dbg_cmd(ehci, label, command) { \
	char _buf [80]; \
	dbg_command_buf (_buf, sizeof _buf, label, command); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

#define dbg_port(ehci, label, port, status) { \
	char _buf [80]; \
	dbg_port_buf (_buf, sizeof _buf, label, port, status); \
	ehci_dbg (ehci, "%s\n", _buf); \
}

/*-------------------------------------------------------------------------*/

#ifdef STUB_DEBUG_FILES

static inline void create_debug_files (struct ehci_hcd *bus) { }
static inline void remove_debug_files (struct ehci_hcd *bus) { }

#else

/* troubleshooting help: expose state in debugfs */

static int debug_async_open(struct inode *, struct file *);
static int debug_periodic_open(struct inode *, struct file *);
static int debug_registers_open(struct inode *, struct file *);
static int debug_async_open(struct inode *, struct file *);
static int debug_lpm_open(struct inode *, struct file *);
static ssize_t debug_lpm_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos);
static ssize_t debug_lpm_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *ppos);
static int debug_lpm_close(struct inode *inode, struct file *file);

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
static const struct file_operations debug_lpm_fops = {
	.owner		= THIS_MODULE,
	.open		= debug_lpm_open,
	.read		= debug_lpm_read,
	.write		= debug_lpm_write,
	.release	= debug_lpm_close,
	.llseek		= noop_llseek,
};

static struct dentry *ehci_debug_root;

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
		case 0 << 12: tmp = 'f'; break; \
		case 1 << 12: tmp = 'l'; break; \
		case 2 << 12: tmp = 'h'; break; \
		default: tmp = '?'; break; \
		}; tmp; })

static inline char token_mark(struct ehci_hcd *ehci, __hc32 token)
{
	__u32 v = hc32_to_cpu(ehci, token);

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
	struct ehci_hcd *ehci,
	struct ehci_qh *qh,
	char **nextp,
	unsigned *sizep
)
{
	u32			scratch;
	u32			hw_curr;
	struct list_head	*entry;
	struct ehci_qtd		*td;
	unsigned		temp;
	unsigned		size = *sizep;
	char			*next = *nextp;
	char			mark;
	__le32			list_end = EHCI_LIST_END(ehci);
	struct ehci_qh_hw	*hw = qh->hw;

	if (hw->hw_qtd_next == list_end)	/* NEC does this */
		mark = '@';
	else
		mark = token_mark(ehci, hw->hw_token);
	if (mark == '/') {	/* qh_alt_next controls qh advance? */
		if ((hw->hw_alt_next & QTD_MASK(ehci))
				== ehci->async->hw->hw_alt_next)
			mark = '#';	/* blocked */
		else if (hw->hw_alt_next == list_end)
			mark = '.';	/* use hw_qtd_next */
		/* else alt_next points to some other qtd */
	}
	scratch = hc32_to_cpup(ehci, &hw->hw_info1);
	hw_curr = (mark == '*') ? hc32_to_cpup(ehci, &hw->hw_current) : 0;
	temp = scnprintf (next, size,
			"qh/%p dev%d %cs ep%d %08x %08x (%08x%c %s nak%d)",
			qh, scratch & 0x007f,
			speed_char (scratch),
			(scratch >> 8) & 0x000f,
			scratch, hc32_to_cpup(ehci, &hw->hw_info2),
			hc32_to_cpup(ehci, &hw->hw_token), mark,
			(cpu_to_hc32(ehci, QTD_TOGGLE) & hw->hw_token)
				? "data1" : "data0",
			(hc32_to_cpup(ehci, &hw->hw_alt_next) >> 1) & 0x0f);
	size -= temp;
	next += temp;

	/* hc may be modifying the list as we read it ... */
	list_for_each (entry, &qh->qtd_list) {
		td = list_entry (entry, struct ehci_qtd, qtd_list);
		scratch = hc32_to_cpup(ehci, &td->hw_token);
		mark = ' ';
		if (hw_curr == td->qtd_dma)
			mark = '*';
		else if (hw->hw_qtd_next == cpu_to_hc32(ehci, td->qtd_dma))
			mark = '+';
		else if (QTD_LENGTH (scratch)) {
			if (td->hw_alt_next == ehci->async->hw->hw_alt_next)
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
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	unsigned		temp, size;
	char			*next;
	struct ehci_qh		*qh;

	hcd = bus_to_hcd(buf->bus);
	ehci = hcd_to_ehci (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	*next = 0;

	/* dumps a snapshot of the async schedule.
	 * usually empty except for long-term bulk reads, or head.
	 * one QH per line, and TDs we know about
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	for (qh = ehci->async->qh_next.qh; size > 0 && qh; qh = qh->qh_next.qh)
		qh_lines (ehci, qh, &next, &size);
	if (ehci->reclaim && size > 0) {
		temp = scnprintf (next, size, "\nreclaim =\n");
		size -= temp;
		next += temp;

		for (qh = ehci->reclaim; size > 0 && qh; qh = qh->reclaim)
			qh_lines (ehci, qh, &next, &size);
	}
	spin_unlock_irqrestore (&ehci->lock, flags);

	return strlen(buf->output_buf);
}

#define DBG_SCHED_LIMIT 64
static ssize_t fill_periodic_buffer(struct debug_buffer *buf)
{
	struct usb_hcd		*hcd;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	union ehci_shadow	p, *seen;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i;
	__hc32			tag;

	if (!(seen = kmalloc (DBG_SCHED_LIMIT * sizeof *seen, GFP_ATOMIC)))
		return 0;
	seen_count = 0;

	hcd = bus_to_hcd(buf->bus);
	ehci = hcd_to_ehci (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	temp = scnprintf (next, size, "size = %d\n", ehci->periodic_size);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule.
	 * iso changes, interrupt usually doesn't.
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	for (i = 0; i < ehci->periodic_size; i++) {
		p = ehci->pshadow [i];
		if (likely (!p.ptr))
			continue;
		tag = Q_NEXT_TYPE(ehci, ehci->periodic [i]);

		temp = scnprintf (next, size, "%4d: ", i);
		size -= temp;
		next += temp;

		do {
			struct ehci_qh_hw *hw;

			switch (hc32_to_cpu(ehci, tag)) {
			case Q_TYPE_QH:
				hw = p.qh->hw;
				temp = scnprintf (next, size, " qh%d-%04x/%p",
						p.qh->period,
						hc32_to_cpup(ehci,
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
					u32	scratch = hc32_to_cpup(ehci,
							&hw->hw_info1);
					struct ehci_qtd	*qtd;
					char		*type = "";

					/* count tds, get ep direction */
					temp = 0;
					list_for_each_entry (qtd,
							&p.qh->qtd_list,
							qtd_list) {
						temp++;
						switch (0x03 & (hc32_to_cpu(
							ehci,
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
				if (p.qh) {
					tag = Q_NEXT_TYPE(ehci, hw->hw_next);
					p = p.qh->qh_next;
				}
				break;
			case Q_TYPE_FSTN:
				temp = scnprintf (next, size,
					" fstn-%8x/%p", p.fstn->hw_prev,
					p.fstn);
				tag = Q_NEXT_TYPE(ehci, p.fstn->hw_next);
				p = p.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				temp = scnprintf (next, size,
					" itd/%p", p.itd);
				tag = Q_NEXT_TYPE(ehci, p.itd->hw_next);
				p = p.itd->itd_next;
				break;
			case Q_TYPE_SITD:
				temp = scnprintf (next, size,
					" sitd%d-%04x/%p",
					p.sitd->stream->interval,
					hc32_to_cpup(ehci, &p.sitd->hw_uframe)
						& 0x0000ffff,
					p.sitd);
				tag = Q_NEXT_TYPE(ehci, p.sitd->hw_next);
				p = p.sitd->sitd_next;
				break;
			}
			size -= temp;
			next += temp;
		} while (p.ptr);

		temp = scnprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
	kfree (seen);

	return buf->alloc_size - size;
}
#undef DBG_SCHED_LIMIT

static const char *rh_state_string(struct ehci_hcd *ehci)
{
	switch (ehci->rh_state) {
	case EHCI_RH_HALTED:
		return "halted";
	case EHCI_RH_SUSPENDED:
		return "suspended";
	case EHCI_RH_RUNNING:
		return "running";
	}
	return "?";
}

static ssize_t fill_registers_buffer(struct debug_buffer *buf)
{
	struct usb_hcd		*hcd;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	unsigned		temp, size, i;
	char			*next, scratch [80];
	static char		fmt [] = "%*s\n";
	static char		label [] = "";

	hcd = bus_to_hcd(buf->bus);
	ehci = hcd_to_ehci (hcd);
	next = buf->output_buf;
	size = buf->alloc_size;

	spin_lock_irqsave (&ehci->lock, flags);

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
	i = HC_VERSION(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));
	temp = scnprintf (next, size,
		"bus %s, device %s\n"
		"%s\n"
		"EHCI %x.%02x, rh state %s\n",
		hcd->self.controller->bus->name,
		dev_name(hcd->self.controller),
		hcd->product_desc,
		i >> 8, i & 0x0ff, rh_state_string(ehci));
	size -= temp;
	next += temp;

#ifdef	CONFIG_PCI
	/* EHCI 0.96 and later may have "extended capabilities" */
	if (hcd->self.controller->bus == &pci_bus_type) {
		struct pci_dev	*pdev;
		u32		offset, cap, cap2;
		unsigned	count = 256/4;

		pdev = to_pci_dev(ehci_to_hcd(ehci)->self.controller);
		offset = HCC_EXT_CAPS(ehci_readl(ehci,
				&ehci->caps->hcc_params));
		while (offset && count--) {
			pci_read_config_dword (pdev, offset, &cap);
			switch (cap & 0xff) {
			case 1:
				temp = scnprintf (next, size,
					"ownership %08x%s%s\n", cap,
					(cap & (1 << 24)) ? " linux" : "",
					(cap & (1 << 16)) ? " firmware" : "");
				size -= temp;
				next += temp;

				offset += 4;
				pci_read_config_dword (pdev, offset, &cap2);
				temp = scnprintf (next, size,
					"SMI sts/enable 0x%08x\n", cap2);
				size -= temp;
				next += temp;
				break;
			case 0:		/* illegal reserved capability */
				cap = 0;
				/* FALLTHROUGH */
			default:		/* unknown */
				break;
			}
			temp = (cap >> 8) & 0xff;
		}
	}
#endif

	// FIXME interpret both types of params
	i = ehci_readl(ehci, &ehci->caps->hcs_params);
	temp = scnprintf (next, size, "structural params 0x%08x\n", i);
	size -= temp;
	next += temp;

	i = ehci_readl(ehci, &ehci->caps->hcc_params);
	temp = scnprintf (next, size, "capability params 0x%08x\n", i);
	size -= temp;
	next += temp;

	/* Operational Registers */
	temp = dbg_status_buf (scratch, sizeof scratch, label,
			ehci_readl(ehci, &ehci->regs->status));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_command_buf (scratch, sizeof scratch, label,
			ehci_readl(ehci, &ehci->regs->command));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = dbg_intr_buf (scratch, sizeof scratch, label,
			ehci_readl(ehci, &ehci->regs->intr_enable));
	temp = scnprintf (next, size, fmt, temp, scratch);
	size -= temp;
	next += temp;

	temp = scnprintf (next, size, "uframe %04x\n",
			ehci_read_frame_index(ehci));
	size -= temp;
	next += temp;

	for (i = 1; i <= HCS_N_PORTS (ehci->hcs_params); i++) {
		temp = dbg_port_buf (scratch, sizeof scratch, label, i,
				ehci_readl(ehci,
					&ehci->regs->port_status[i - 1]));
		temp = scnprintf (next, size, fmt, temp, scratch);
		size -= temp;
		next += temp;
		if (i == HCS_DEBUG_PORT(ehci->hcs_params) && ehci->debug) {
			temp = scnprintf (next, size,
					"    debug control %08x\n",
					ehci_readl(ehci,
						&ehci->debug->control));
			size -= temp;
			next += temp;
		}
	}

	if (ehci->reclaim) {
		temp = scnprintf(next, size, "reclaim qh %p\n", ehci->reclaim);
		size -= temp;
		next += temp;
	}

#ifdef EHCI_STATS
	temp = scnprintf (next, size,
		"irq normal %ld err %ld reclaim %ld (lost %ld)\n",
		ehci->stats.normal, ehci->stats.error, ehci->stats.reclaim,
		ehci->stats.lost_iaa);
	size -= temp;
	next += temp;

	temp = scnprintf (next, size, "complete %ld unlink %ld\n",
		ehci->stats.complete, ehci->stats.unlink);
	size -= temp;
	next += temp;
#endif

done:
	spin_unlock_irqrestore (&ehci->lock, flags);

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

static int debug_lpm_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static int debug_lpm_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t debug_lpm_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	/* TODO: show lpm stats */
	return 0;
}

static ssize_t debug_lpm_write(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct usb_hcd		*hcd;
	struct ehci_hcd		*ehci;
	char buf[50];
	size_t len;
	u32 temp;
	unsigned long port;
	u32 __iomem	*portsc ;
	u32 params;

	hcd = bus_to_hcd(file->private_data);
	ehci = hcd_to_ehci(hcd);

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	if (strncmp(buf, "enable", 5) == 0) {
		if (strict_strtoul(buf + 7, 10, &port))
			return -EINVAL;
		params = ehci_readl(ehci, &ehci->caps->hcs_params);
		if (port > HCS_N_PORTS(params)) {
			ehci_dbg(ehci, "ERR: LPM on bad port %lu\n", port);
			return -ENODEV;
		}
		portsc = &ehci->regs->port_status[port-1];
		temp = ehci_readl(ehci, portsc);
		if (!(temp & PORT_DEV_ADDR)) {
			ehci_dbg(ehci, "LPM: no device attached\n");
			return -ENODEV;
		}
		temp |= PORT_LPM;
		ehci_writel(ehci, temp, portsc);
		printk(KERN_INFO "force enable LPM for port %lu\n", port);
	} else if (strncmp(buf, "hird=", 5) == 0) {
		unsigned long hird;
		if (strict_strtoul(buf + 5, 16, &hird))
			return -EINVAL;
		printk(KERN_INFO "setting hird %s %lu\n", buf + 6, hird);
		temp = ehci_readl(ehci, &ehci->regs->command);
		temp &= ~CMD_HIRD;
		temp |= hird << 24;
		ehci_writel(ehci, temp, &ehci->regs->command);
	} else if (strncmp(buf, "disable", 7) == 0) {
		if (strict_strtoul(buf + 8, 10, &port))
			return -EINVAL;
		params = ehci_readl(ehci, &ehci->caps->hcs_params);
		if (port > HCS_N_PORTS(params)) {
			ehci_dbg(ehci, "ERR: LPM off bad port %lu\n", port);
			return -ENODEV;
		}
		portsc = &ehci->regs->port_status[port-1];
		temp = ehci_readl(ehci, portsc);
		if (!(temp & PORT_DEV_ADDR)) {
			ehci_dbg(ehci, "ERR: no device attached\n");
			return -ENODEV;
		}
		temp &= ~PORT_LPM;
		ehci_writel(ehci, temp, portsc);
		printk(KERN_INFO "disabled LPM for port %lu\n", port);
	} else
		return -EOPNOTSUPP;
	return count;
}

static inline void create_debug_files (struct ehci_hcd *ehci)
{
	struct usb_bus *bus = &ehci_to_hcd(ehci)->self;

	ehci->debug_dir = debugfs_create_dir(bus->bus_name, ehci_debug_root);
	if (!ehci->debug_dir)
		return;

	if (!debugfs_create_file("async", S_IRUGO, ehci->debug_dir, bus,
						&debug_async_fops))
		goto file_error;

	if (!debugfs_create_file("periodic", S_IRUGO, ehci->debug_dir, bus,
						&debug_periodic_fops))
		goto file_error;

	if (!debugfs_create_file("registers", S_IRUGO, ehci->debug_dir, bus,
						    &debug_registers_fops))
		goto file_error;

	if (!debugfs_create_file("lpm", S_IRUGO|S_IWUSR, ehci->debug_dir, bus,
						    &debug_lpm_fops))
		goto file_error;

	return;

file_error:
	debugfs_remove_recursive(ehci->debug_dir);
}

static inline void remove_debug_files (struct ehci_hcd *ehci)
{
	debugfs_remove_recursive(ehci->debug_dir);
}

#endif /* STUB_DEBUG_FILES */
