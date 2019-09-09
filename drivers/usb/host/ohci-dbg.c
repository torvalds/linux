// SPDX-License-Identifier: GPL-1.0+
/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *
 * This file is licenced under the GPL.
 */

/*-------------------------------------------------------------------------*/

#define edstring(ed_type) ({ char *temp; \
	switch (ed_type) { \
	case PIPE_CONTROL:	temp = "ctrl"; break; \
	case PIPE_BULK:		temp = "bulk"; break; \
	case PIPE_INTERRUPT:	temp = "intr"; break; \
	default:		temp = "isoc"; break; \
	} temp;})
#define pipestring(pipe) edstring(usb_pipetype(pipe))


#define ohci_dbg_sw(ohci, next, size, format, arg...) \
	do { \
	if (next != NULL) { \
		unsigned s_len; \
		s_len = scnprintf (*next, *size, format, ## arg ); \
		*size -= s_len; *next += s_len; \
	} else \
		ohci_dbg(ohci,format, ## arg ); \
	} while (0);

/* Version for use where "next" is the address of a local variable */
#define ohci_dbg_nosw(ohci, next, size, format, arg...) \
	do { \
		unsigned s_len; \
		s_len = scnprintf(*next, *size, format, ## arg); \
		*size -= s_len; *next += s_len; \
	} while (0);


static void ohci_dump_intr_mask (
	struct ohci_hcd *ohci,
	char *label,
	u32 mask,
	char **next,
	unsigned *size)
{
	ohci_dbg_sw (ohci, next, size, "%s 0x%08x%s%s%s%s%s%s%s%s%s\n",
		label,
		mask,
		(mask & OHCI_INTR_MIE) ? " MIE" : "",
		(mask & OHCI_INTR_OC) ? " OC" : "",
		(mask & OHCI_INTR_RHSC) ? " RHSC" : "",
		(mask & OHCI_INTR_FNO) ? " FNO" : "",
		(mask & OHCI_INTR_UE) ? " UE" : "",
		(mask & OHCI_INTR_RD) ? " RD" : "",
		(mask & OHCI_INTR_SF) ? " SF" : "",
		(mask & OHCI_INTR_WDH) ? " WDH" : "",
		(mask & OHCI_INTR_SO) ? " SO" : ""
		);
}

static void maybe_print_eds (
	struct ohci_hcd *ohci,
	char *label,
	u32 value,
	char **next,
	unsigned *size)
{
	if (value)
		ohci_dbg_sw (ohci, next, size, "%s %08x\n", label, value);
}

static char *hcfs2string (int state)
{
	switch (state) {
		case OHCI_USB_RESET:	return "reset";
		case OHCI_USB_RESUME:	return "resume";
		case OHCI_USB_OPER:	return "operational";
		case OHCI_USB_SUSPEND:	return "suspend";
	}
	return "?";
}

static const char *rh_state_string(struct ohci_hcd *ohci)
{
	switch (ohci->rh_state) {
	case OHCI_RH_HALTED:
		return "halted";
	case OHCI_RH_SUSPENDED:
		return "suspended";
	case OHCI_RH_RUNNING:
		return "running";
	}
	return "?";
}

// dump control and status registers
static void
ohci_dump_status (struct ohci_hcd *controller, char **next, unsigned *size)
{
	struct ohci_regs __iomem *regs = controller->regs;
	u32			temp;

	temp = ohci_readl (controller, &regs->revision) & 0xff;
	ohci_dbg_sw (controller, next, size,
		"OHCI %d.%d, %s legacy support registers, rh state %s\n",
		0x03 & (temp >> 4), (temp & 0x0f),
		(temp & 0x0100) ? "with" : "NO",
		rh_state_string(controller));

	temp = ohci_readl (controller, &regs->control);
	ohci_dbg_sw (controller, next, size,
		"control 0x%03x%s%s%s HCFS=%s%s%s%s%s CBSR=%d\n",
		temp,
		(temp & OHCI_CTRL_RWE) ? " RWE" : "",
		(temp & OHCI_CTRL_RWC) ? " RWC" : "",
		(temp & OHCI_CTRL_IR) ? " IR" : "",
		hcfs2string (temp & OHCI_CTRL_HCFS),
		(temp & OHCI_CTRL_BLE) ? " BLE" : "",
		(temp & OHCI_CTRL_CLE) ? " CLE" : "",
		(temp & OHCI_CTRL_IE) ? " IE" : "",
		(temp & OHCI_CTRL_PLE) ? " PLE" : "",
		temp & OHCI_CTRL_CBSR
		);

	temp = ohci_readl (controller, &regs->cmdstatus);
	ohci_dbg_sw (controller, next, size,
		"cmdstatus 0x%05x SOC=%d%s%s%s%s\n", temp,
		(temp & OHCI_SOC) >> 16,
		(temp & OHCI_OCR) ? " OCR" : "",
		(temp & OHCI_BLF) ? " BLF" : "",
		(temp & OHCI_CLF) ? " CLF" : "",
		(temp & OHCI_HCR) ? " HCR" : ""
		);

	ohci_dump_intr_mask (controller, "intrstatus",
			ohci_readl (controller, &regs->intrstatus),
			next, size);
	ohci_dump_intr_mask (controller, "intrenable",
			ohci_readl (controller, &regs->intrenable),
			next, size);
	// intrdisable always same as intrenable

	maybe_print_eds (controller, "ed_periodcurrent",
			ohci_readl (controller, &regs->ed_periodcurrent),
			next, size);

	maybe_print_eds (controller, "ed_controlhead",
			ohci_readl (controller, &regs->ed_controlhead),
			next, size);
	maybe_print_eds (controller, "ed_controlcurrent",
			ohci_readl (controller, &regs->ed_controlcurrent),
			next, size);

	maybe_print_eds (controller, "ed_bulkhead",
			ohci_readl (controller, &regs->ed_bulkhead),
			next, size);
	maybe_print_eds (controller, "ed_bulkcurrent",
			ohci_readl (controller, &regs->ed_bulkcurrent),
			next, size);

	maybe_print_eds (controller, "donehead",
			ohci_readl (controller, &regs->donehead), next, size);
}

#define dbg_port_sw(hc,num,value,next,size) \
	ohci_dbg_sw (hc, next, size, \
		"roothub.portstatus [%d] " \
		"0x%08x%s%s%s%s%s%s%s%s%s%s%s%s\n", \
		num, temp, \
		(temp & RH_PS_PRSC) ? " PRSC" : "", \
		(temp & RH_PS_OCIC) ? " OCIC" : "", \
		(temp & RH_PS_PSSC) ? " PSSC" : "", \
		(temp & RH_PS_PESC) ? " PESC" : "", \
		(temp & RH_PS_CSC) ? " CSC" : "", \
		\
		(temp & RH_PS_LSDA) ? " LSDA" : "", \
		(temp & RH_PS_PPS) ? " PPS" : "", \
		(temp & RH_PS_PRS) ? " PRS" : "", \
		(temp & RH_PS_POCI) ? " POCI" : "", \
		(temp & RH_PS_PSS) ? " PSS" : "", \
		\
		(temp & RH_PS_PES) ? " PES" : "", \
		(temp & RH_PS_CCS) ? " CCS" : "" \
		);


static void
ohci_dump_roothub (
	struct ohci_hcd *controller,
	int verbose,
	char **next,
	unsigned *size)
{
	u32			temp, i;

	temp = roothub_a (controller);
	if (temp == ~(u32)0)
		return;

	if (verbose) {
		ohci_dbg_sw (controller, next, size,
			"roothub.a %08x POTPGT=%d%s%s%s%s%s NDP=%d(%d)\n", temp,
			((temp & RH_A_POTPGT) >> 24) & 0xff,
			(temp & RH_A_NOCP) ? " NOCP" : "",
			(temp & RH_A_OCPM) ? " OCPM" : "",
			(temp & RH_A_DT) ? " DT" : "",
			(temp & RH_A_NPS) ? " NPS" : "",
			(temp & RH_A_PSM) ? " PSM" : "",
			(temp & RH_A_NDP), controller->num_ports
			);
		temp = roothub_b (controller);
		ohci_dbg_sw (controller, next, size,
			"roothub.b %08x PPCM=%04x DR=%04x\n",
			temp,
			(temp & RH_B_PPCM) >> 16,
			(temp & RH_B_DR)
			);
		temp = roothub_status (controller);
		ohci_dbg_sw (controller, next, size,
			"roothub.status %08x%s%s%s%s%s%s\n",
			temp,
			(temp & RH_HS_CRWE) ? " CRWE" : "",
			(temp & RH_HS_OCIC) ? " OCIC" : "",
			(temp & RH_HS_LPSC) ? " LPSC" : "",
			(temp & RH_HS_DRWE) ? " DRWE" : "",
			(temp & RH_HS_OCI) ? " OCI" : "",
			(temp & RH_HS_LPS) ? " LPS" : ""
			);
	}

	for (i = 0; i < controller->num_ports; i++) {
		temp = roothub_portstatus (controller, i);
		dbg_port_sw (controller, i, temp, next, size);
	}
}

static void ohci_dump(struct ohci_hcd *controller)
{
	ohci_dbg (controller, "OHCI controller state\n");

	// dumps some of the state we know about
	ohci_dump_status (controller, NULL, NULL);
	if (controller->hcca)
		ohci_dbg (controller,
			"hcca frame #%04x\n", ohci_frame_no(controller));
	ohci_dump_roothub (controller, 1, NULL, NULL);
}

static const char data0 [] = "DATA0";
static const char data1 [] = "DATA1";

static void ohci_dump_td (const struct ohci_hcd *ohci, const char *label,
		const struct td *td)
{
	u32	tmp = hc32_to_cpup (ohci, &td->hwINFO);

	ohci_dbg (ohci, "%s td %p%s; urb %p index %d; hw next td %08x\n",
		label, td,
		(tmp & TD_DONE) ? " (DONE)" : "",
		td->urb, td->index,
		hc32_to_cpup (ohci, &td->hwNextTD));
	if ((tmp & TD_ISO) == 0) {
		const char	*toggle, *pid;
		u32	cbp, be;

		switch (tmp & TD_T) {
		case TD_T_DATA0: toggle = data0; break;
		case TD_T_DATA1: toggle = data1; break;
		case TD_T_TOGGLE: toggle = "(CARRY)"; break;
		default: toggle = "(?)"; break;
		}
		switch (tmp & TD_DP) {
		case TD_DP_SETUP: pid = "SETUP"; break;
		case TD_DP_IN: pid = "IN"; break;
		case TD_DP_OUT: pid = "OUT"; break;
		default: pid = "(bad pid)"; break;
		}
		ohci_dbg (ohci, "     info %08x CC=%x %s DI=%d %s %s\n", tmp,
			TD_CC_GET(tmp), /* EC, */ toggle,
			(tmp & TD_DI) >> 21, pid,
			(tmp & TD_R) ? "R" : "");
		cbp = hc32_to_cpup (ohci, &td->hwCBP);
		be = hc32_to_cpup (ohci, &td->hwBE);
		ohci_dbg (ohci, "     cbp %08x be %08x (len %d)\n", cbp, be,
			cbp ? (be + 1 - cbp) : 0);
	} else {
		unsigned	i;
		ohci_dbg (ohci, "  info %08x CC=%x FC=%d DI=%d SF=%04x\n", tmp,
			TD_CC_GET(tmp),
			(tmp >> 24) & 0x07,
			(tmp & TD_DI) >> 21,
			tmp & 0x0000ffff);
		ohci_dbg (ohci, "  bp0 %08x be %08x\n",
			hc32_to_cpup (ohci, &td->hwCBP) & ~0x0fff,
			hc32_to_cpup (ohci, &td->hwBE));
		for (i = 0; i < MAXPSW; i++) {
			u16	psw = ohci_hwPSW (ohci, td, i);
			int	cc = (psw >> 12) & 0x0f;
			ohci_dbg (ohci, "    psw [%d] = %2x, CC=%x %s=%d\n", i,
				psw, cc,
				(cc >= 0x0e) ? "OFFSET" : "SIZE",
				psw & 0x0fff);
		}
	}
}

/* caller MUST own hcd spinlock if verbose is set! */
static void __maybe_unused
ohci_dump_ed (const struct ohci_hcd *ohci, const char *label,
		const struct ed *ed, int verbose)
{
	u32	tmp = hc32_to_cpu (ohci, ed->hwINFO);
	char	*type = "";

	ohci_dbg (ohci, "%s, ed %p state 0x%x type %s; next ed %08x\n",
		label,
		ed, ed->state, edstring (ed->type),
		hc32_to_cpup (ohci, &ed->hwNextED));
	switch (tmp & (ED_IN|ED_OUT)) {
	case ED_OUT: type = "-OUT"; break;
	case ED_IN: type = "-IN"; break;
	/* else from TDs ... control */
	}
	ohci_dbg (ohci,
		"  info %08x MAX=%d%s%s%s%s EP=%d%s DEV=%d\n", tmp,
		0x03ff & (tmp >> 16),
		(tmp & ED_DEQUEUE) ? " DQ" : "",
		(tmp & ED_ISO) ? " ISO" : "",
		(tmp & ED_SKIP) ? " SKIP" : "",
		(tmp & ED_LOWSPEED) ? " LOW" : "",
		0x000f & (tmp >> 7),
		type,
		0x007f & tmp);
	tmp = hc32_to_cpup (ohci, &ed->hwHeadP);
	ohci_dbg (ohci, "  tds: head %08x %s%s tail %08x%s\n",
		tmp,
		(tmp & ED_C) ? data1 : data0,
		(tmp & ED_H) ? " HALT" : "",
		hc32_to_cpup (ohci, &ed->hwTailP),
		verbose ? "" : " (not listing)");
	if (verbose) {
		struct list_head	*tmp;

		/* use ed->td_list because HC concurrently modifies
		 * hwNextTD as it accumulates ed_donelist.
		 */
		list_for_each (tmp, &ed->td_list) {
			struct td		*td;
			td = list_entry (tmp, struct td, td_list);
			ohci_dump_td (ohci, "  ->", td);
		}
	}
}

/*-------------------------------------------------------------------------*/

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

static struct dentry *ohci_debug_root;

struct debug_buffer {
	ssize_t (*fill_func)(struct debug_buffer *);	/* fill method */
	struct ohci_hcd *ohci;
	struct mutex mutex;	/* protect filling of buffer */
	size_t count;		/* number of characters filled into buffer */
	char *page;
};

static ssize_t
show_list (struct ohci_hcd *ohci, char *buf, size_t count, struct ed *ed)
{
	unsigned		temp, size = count;

	if (!ed)
		return 0;

	/* print first --> last */
	while (ed->ed_prev)
		ed = ed->ed_prev;

	/* dump a snapshot of the bulk or control schedule */
	while (ed) {
		u32		info = hc32_to_cpu (ohci, ed->hwINFO);
		u32		headp = hc32_to_cpu (ohci, ed->hwHeadP);
		struct list_head *entry;
		struct td	*td;

		temp = scnprintf (buf, size,
			"ed/%p %cs dev%d ep%d%s max %d %08x%s%s %s",
			ed,
			(info & ED_LOWSPEED) ? 'l' : 'f',
			info & 0x7f,
			(info >> 7) & 0xf,
			(info & ED_IN) ? "in" : "out",
			0x03ff & (info >> 16),
			info,
			(info & ED_SKIP) ? " s" : "",
			(headp & ED_H) ? " H" : "",
			(headp & ED_C) ? data1 : data0);
		size -= temp;
		buf += temp;

		list_for_each (entry, &ed->td_list) {
			u32		cbp, be;

			td = list_entry (entry, struct td, td_list);
			info = hc32_to_cpup (ohci, &td->hwINFO);
			cbp = hc32_to_cpup (ohci, &td->hwCBP);
			be = hc32_to_cpup (ohci, &td->hwBE);
			temp = scnprintf (buf, size,
					"\n\ttd %p %s %d cc=%x urb %p (%08x)",
					td,
					({ char *pid;
					switch (info & TD_DP) {
					case TD_DP_SETUP: pid = "setup"; break;
					case TD_DP_IN: pid = "in"; break;
					case TD_DP_OUT: pid = "out"; break;
					default: pid = "(?)"; break;
					 } pid;}),
					cbp ? (be + 1 - cbp) : 0,
					TD_CC_GET (info), td->urb, info);
			size -= temp;
			buf += temp;
		}

		temp = scnprintf (buf, size, "\n");
		size -= temp;
		buf += temp;

		ed = ed->ed_next;
	}
	return count - size;
}

static ssize_t fill_async_buffer(struct debug_buffer *buf)
{
	struct ohci_hcd		*ohci;
	size_t			temp, size;
	unsigned long		flags;

	ohci = buf->ohci;
	size = PAGE_SIZE;

	/* display control and bulk lists together, for simplicity */
	spin_lock_irqsave (&ohci->lock, flags);
	temp = show_list(ohci, buf->page, size, ohci->ed_controltail);
	temp += show_list(ohci, buf->page + temp, size - temp,
			  ohci->ed_bulktail);
	spin_unlock_irqrestore (&ohci->lock, flags);

	return temp;
}

#define DBG_SCHED_LIMIT 64

static ssize_t fill_periodic_buffer(struct debug_buffer *buf)
{
	struct ohci_hcd		*ohci;
	struct ed		**seen, *ed;
	unsigned long		flags;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i;

	seen = kmalloc_array(DBG_SCHED_LIMIT, sizeof(*seen), GFP_ATOMIC);
	if (!seen)
		return 0;
	seen_count = 0;

	ohci = buf->ohci;
	next = buf->page;
	size = PAGE_SIZE;

	temp = scnprintf (next, size, "size = %d\n", NUM_INTS);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule (and load) */
	spin_lock_irqsave (&ohci->lock, flags);
	for (i = 0; i < NUM_INTS; i++) {
		ed = ohci->periodic[i];
		if (!ed)
			continue;

		temp = scnprintf (next, size, "%2d [%3d]:", i, ohci->load [i]);
		size -= temp;
		next += temp;

		do {
			temp = scnprintf (next, size, " ed%d/%p",
				ed->interval, ed);
			size -= temp;
			next += temp;
			for (temp = 0; temp < seen_count; temp++) {
				if (seen [temp] == ed)
					break;
			}

			/* show more info the first time around */
			if (temp == seen_count) {
				u32	info = hc32_to_cpu (ohci, ed->hwINFO);
				struct list_head	*entry;
				unsigned		qlen = 0;

				/* qlen measured here in TDs, not urbs */
				list_for_each (entry, &ed->td_list)
					qlen++;

				temp = scnprintf (next, size,
					" (%cs dev%d ep%d%s-%s qlen %u"
					" max %d %08x%s%s)",
					(info & ED_LOWSPEED) ? 'l' : 'f',
					info & 0x7f,
					(info >> 7) & 0xf,
					(info & ED_IN) ? "in" : "out",
					(info & ED_ISO) ? "iso" : "int",
					qlen,
					0x03ff & (info >> 16),
					info,
					(info & ED_SKIP) ? " K" : "",
					(ed->hwHeadP &
						cpu_to_hc32(ohci, ED_H)) ?
							" H" : "");
				size -= temp;
				next += temp;

				if (seen_count < DBG_SCHED_LIMIT)
					seen [seen_count++] = ed;

				ed = ed->ed_next;

			} else {
				/* we've seen it and what's after */
				temp = 0;
				ed = NULL;
			}

		} while (ed);

		temp = scnprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&ohci->lock, flags);
	kfree (seen);

	return PAGE_SIZE - size;
}
#undef DBG_SCHED_LIMIT

static ssize_t fill_registers_buffer(struct debug_buffer *buf)
{
	struct usb_hcd		*hcd;
	struct ohci_hcd		*ohci;
	struct ohci_regs __iomem *regs;
	unsigned long		flags;
	unsigned		temp, size;
	char			*next;
	u32			rdata;

	ohci = buf->ohci;
	hcd = ohci_to_hcd(ohci);
	regs = ohci->regs;
	next = buf->page;
	size = PAGE_SIZE;

	spin_lock_irqsave (&ohci->lock, flags);

	/* dump driver info, then registers in spec order */

	ohci_dbg_nosw(ohci, &next, &size,
		"bus %s, device %s\n"
		"%s\n"
		"%s\n",
		hcd->self.controller->bus->name,
		dev_name(hcd->self.controller),
		hcd->product_desc,
		hcd_name);

	if (!HCD_HW_ACCESSIBLE(hcd)) {
		size -= scnprintf (next, size,
			"SUSPENDED (no register access)\n");
		goto done;
	}

	ohci_dump_status(ohci, &next, &size);

	/* hcca */
	if (ohci->hcca)
		ohci_dbg_nosw(ohci, &next, &size,
			"hcca frame 0x%04x\n", ohci_frame_no(ohci));

	/* other registers mostly affect frame timings */
	rdata = ohci_readl (ohci, &regs->fminterval);
	temp = scnprintf (next, size,
			"fmintvl 0x%08x %sFSMPS=0x%04x FI=0x%04x\n",
			rdata, (rdata >> 31) ? "FIT " : "",
			(rdata >> 16) & 0xefff, rdata & 0xffff);
	size -= temp;
	next += temp;

	rdata = ohci_readl (ohci, &regs->fmremaining);
	temp = scnprintf (next, size, "fmremaining 0x%08x %sFR=0x%04x\n",
			rdata, (rdata >> 31) ? "FRT " : "",
			rdata & 0x3fff);
	size -= temp;
	next += temp;

	rdata = ohci_readl (ohci, &regs->periodicstart);
	temp = scnprintf (next, size, "periodicstart 0x%04x\n",
			rdata & 0x3fff);
	size -= temp;
	next += temp;

	rdata = ohci_readl (ohci, &regs->lsthresh);
	temp = scnprintf (next, size, "lsthresh 0x%04x\n",
			rdata & 0x3fff);
	size -= temp;
	next += temp;

	temp = scnprintf (next, size, "hub poll timer %s\n",
			HCD_POLL_RH(ohci_to_hcd(ohci)) ? "ON" : "off");
	size -= temp;
	next += temp;

	/* roothub */
	ohci_dump_roothub (ohci, 1, &next, &size);

done:
	spin_unlock_irqrestore (&ohci->lock, flags);

	return PAGE_SIZE - size;
}

static struct debug_buffer *alloc_buffer(struct ohci_hcd *ohci,
				ssize_t (*fill_func)(struct debug_buffer *))
{
	struct debug_buffer *buf;

	buf = kzalloc(sizeof(struct debug_buffer), GFP_KERNEL);

	if (buf) {
		buf->ohci = ohci;
		buf->fill_func = fill_func;
		mutex_init(&buf->mutex);
	}

	return buf;
}

static int fill_buffer(struct debug_buffer *buf)
{
	int ret = 0;

	if (!buf->page)
		buf->page = (char *)get_zeroed_page(GFP_KERNEL);

	if (!buf->page) {
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
				      buf->page, buf->count);

out:
	return ret;

}

static int debug_close(struct inode *inode, struct file *file)
{
	struct debug_buffer *buf = file->private_data;

	if (buf) {
		if (buf->page)
			free_page((unsigned long)buf->page);
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
	file->private_data = alloc_buffer(inode->i_private,
					  fill_periodic_buffer);

	return file->private_data ? 0 : -ENOMEM;
}

static int debug_registers_open(struct inode *inode, struct file *file)
{
	file->private_data = alloc_buffer(inode->i_private,
					  fill_registers_buffer);

	return file->private_data ? 0 : -ENOMEM;
}
static inline void create_debug_files (struct ohci_hcd *ohci)
{
	struct usb_bus *bus = &ohci_to_hcd(ohci)->self;
	struct dentry *root;

	root = debugfs_create_dir(bus->bus_name, ohci_debug_root);
	ohci->debug_dir = root;

	debugfs_create_file("async", S_IRUGO, root, ohci, &debug_async_fops);
	debugfs_create_file("periodic", S_IRUGO, root, ohci,
			    &debug_periodic_fops);
	debugfs_create_file("registers", S_IRUGO, root, ohci,
			    &debug_registers_fops);

	ohci_dbg (ohci, "created debug files\n");
}

static inline void remove_debug_files (struct ohci_hcd *ohci)
{
	debugfs_remove_recursive(ohci->debug_dir);
}

/*-------------------------------------------------------------------------*/

