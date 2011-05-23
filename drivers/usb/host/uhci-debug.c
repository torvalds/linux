/*
 * UHCI-specific debugging code. Invaluable when something
 * goes wrong, but don't get in my face.
 *
 * Kernel visible pointers are surrounded in []s and bus
 * visible pointers are surrounded in ()s
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2001 Johannes Erdfelt
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <asm/io.h>

#include "uhci-hcd.h"

static struct dentry *uhci_debugfs_root;

#ifdef DEBUG

/* Handle REALLY large printks so we don't overflow buffers */
static void lprintk(char *buf)
{
	char *p;

	/* Just write one line at a time */
	while (buf) {
		p = strchr(buf, '\n');
		if (p)
			*p = 0;
		printk(KERN_DEBUG "%s\n", buf);
		buf = p;
		if (buf)
			buf++;
	}
}

static int uhci_show_td(struct uhci_hcd *uhci, struct uhci_td *td, char *buf,
			int len, int space)
{
	char *out = buf;
	char *spid;
	u32 status, token;

	/* Try to make sure there's enough memory */
	if (len < 160)
		return 0;

	status = td_status(uhci, td);
	out += sprintf(out, "%*s[%p] link (%08x) ", space, "", td,
		hc32_to_cpu(uhci, td->link));
	out += sprintf(out, "e%d %s%s%s%s%s%s%s%s%s%sLength=%x ",
		((status >> 27) & 3),
		(status & TD_CTRL_SPD) ?      "SPD " : "",
		(status & TD_CTRL_LS) ?       "LS " : "",
		(status & TD_CTRL_IOC) ?      "IOC " : "",
		(status & TD_CTRL_ACTIVE) ?   "Active " : "",
		(status & TD_CTRL_STALLED) ?  "Stalled " : "",
		(status & TD_CTRL_DBUFERR) ?  "DataBufErr " : "",
		(status & TD_CTRL_BABBLE) ?   "Babble " : "",
		(status & TD_CTRL_NAK) ?      "NAK " : "",
		(status & TD_CTRL_CRCTIMEO) ? "CRC/Timeo " : "",
		(status & TD_CTRL_BITSTUFF) ? "BitStuff " : "",
		status & 0x7ff);

	token = td_token(uhci, td);
	switch (uhci_packetid(token)) {
		case USB_PID_SETUP:
			spid = "SETUP";
			break;
		case USB_PID_OUT:
			spid = "OUT";
			break;
		case USB_PID_IN:
			spid = "IN";
			break;
		default:
			spid = "?";
			break;
	}

	out += sprintf(out, "MaxLen=%x DT%d EndPt=%x Dev=%x, PID=%x(%s) ",
		token >> 21,
		((token >> 19) & 1),
		(token >> 15) & 15,
		(token >> 8) & 127,
		(token & 0xff),
		spid);
	out += sprintf(out, "(buf=%08x)\n", hc32_to_cpu(uhci, td->buffer));

	return out - buf;
}

static int uhci_show_urbp(struct uhci_hcd *uhci, struct urb_priv *urbp,
			char *buf, int len, int space)
{
	char *out = buf;
	struct uhci_td *td;
	int i, nactive, ninactive;
	char *ptype;

	if (len < 200)
		return 0;

	out += sprintf(out, "urb_priv [%p] ", urbp);
	out += sprintf(out, "urb [%p] ", urbp->urb);
	out += sprintf(out, "qh [%p] ", urbp->qh);
	out += sprintf(out, "Dev=%d ", usb_pipedevice(urbp->urb->pipe));
	out += sprintf(out, "EP=%x(%s) ", usb_pipeendpoint(urbp->urb->pipe),
			(usb_pipein(urbp->urb->pipe) ? "IN" : "OUT"));

	switch (usb_pipetype(urbp->urb->pipe)) {
	case PIPE_ISOCHRONOUS: ptype = "ISO"; break;
	case PIPE_INTERRUPT: ptype = "INT"; break;
	case PIPE_BULK: ptype = "BLK"; break;
	default:
	case PIPE_CONTROL: ptype = "CTL"; break;
	}

	out += sprintf(out, "%s%s", ptype, (urbp->fsbr ? " FSBR" : ""));
	out += sprintf(out, " Actlen=%d%s", urbp->urb->actual_length,
			(urbp->qh->type == USB_ENDPOINT_XFER_CONTROL ?
				"-8" : ""));

	if (urbp->urb->unlinked)
		out += sprintf(out, " Unlinked=%d", urbp->urb->unlinked);
	out += sprintf(out, "\n");

	i = nactive = ninactive = 0;
	list_for_each_entry(td, &urbp->td_list, list) {
		if (urbp->qh->type != USB_ENDPOINT_XFER_ISOC &&
				(++i <= 10 || debug > 2)) {
			out += sprintf(out, "%*s%d: ", space + 2, "", i);
			out += uhci_show_td(uhci, td, out,
					len - (out - buf), 0);
		} else {
			if (td_status(uhci, td) & TD_CTRL_ACTIVE)
				++nactive;
			else
				++ninactive;
		}
	}
	if (nactive + ninactive > 0)
		out += sprintf(out, "%*s[skipped %d inactive and %d active "
				"TDs]\n",
				space, "", ninactive, nactive);

	return out - buf;
}

static int uhci_show_qh(struct uhci_hcd *uhci,
		struct uhci_qh *qh, char *buf, int len, int space)
{
	char *out = buf;
	int i, nurbs;
	__hc32 element = qh_element(qh);
	char *qtype;

	/* Try to make sure there's enough memory */
	if (len < 80 * 7)
		return 0;

	switch (qh->type) {
	case USB_ENDPOINT_XFER_ISOC: qtype = "ISO"; break;
	case USB_ENDPOINT_XFER_INT: qtype = "INT"; break;
	case USB_ENDPOINT_XFER_BULK: qtype = "BLK"; break;
	case USB_ENDPOINT_XFER_CONTROL: qtype = "CTL"; break;
	default: qtype = "Skel" ; break;
	}

	out += sprintf(out, "%*s[%p] %s QH link (%08x) element (%08x)\n",
			space, "", qh, qtype,
			hc32_to_cpu(uhci, qh->link),
			hc32_to_cpu(uhci, element));
	if (qh->type == USB_ENDPOINT_XFER_ISOC)
		out += sprintf(out, "%*s    period %d phase %d load %d us, "
				"frame %x desc [%p]\n",
				space, "", qh->period, qh->phase, qh->load,
				qh->iso_frame, qh->iso_packet_desc);
	else if (qh->type == USB_ENDPOINT_XFER_INT)
		out += sprintf(out, "%*s    period %d phase %d load %d us\n",
				space, "", qh->period, qh->phase, qh->load);

	if (element & UHCI_PTR_QH(uhci))
		out += sprintf(out, "%*s  Element points to QH (bug?)\n", space, "");

	if (element & UHCI_PTR_DEPTH(uhci))
		out += sprintf(out, "%*s  Depth traverse\n", space, "");

	if (element & cpu_to_hc32(uhci, 8))
		out += sprintf(out, "%*s  Bit 3 set (bug?)\n", space, "");

	if (!(element & ~(UHCI_PTR_QH(uhci) | UHCI_PTR_DEPTH(uhci))))
		out += sprintf(out, "%*s  Element is NULL (bug?)\n", space, "");

	if (list_empty(&qh->queue)) {
		out += sprintf(out, "%*s  queue is empty\n", space, "");
		if (qh == uhci->skel_async_qh)
			out += uhci_show_td(uhci, uhci->term_td, out,
					len - (out - buf), 0);
	} else {
		struct urb_priv *urbp = list_entry(qh->queue.next,
				struct urb_priv, node);
		struct uhci_td *td = list_entry(urbp->td_list.next,
				struct uhci_td, list);

		if (element != LINK_TO_TD(uhci, td))
			out += sprintf(out, "%*s Element != First TD\n",
					space, "");
		i = nurbs = 0;
		list_for_each_entry(urbp, &qh->queue, node) {
			if (++i <= 10)
				out += uhci_show_urbp(uhci, urbp, out,
						len - (out - buf), space + 2);
			else
				++nurbs;
		}
		if (nurbs > 0)
			out += sprintf(out, "%*s Skipped %d URBs\n",
					space, "", nurbs);
	}

	if (qh->dummy_td) {
		out += sprintf(out, "%*s  Dummy TD\n", space, "");
		out += uhci_show_td(uhci, qh->dummy_td, out,
				len - (out - buf), 0);
	}

	return out - buf;
}

static int uhci_show_sc(int port, unsigned short status, char *buf, int len)
{
	char *out = buf;

	/* Try to make sure there's enough memory */
	if (len < 160)
		return 0;

	out += sprintf(out, "  stat%d     =     %04x  %s%s%s%s%s%s%s%s%s%s\n",
		port,
		status,
		(status & USBPORTSC_SUSP) ?	" Suspend" : "",
		(status & USBPORTSC_OCC) ?	" OverCurrentChange" : "",
		(status & USBPORTSC_OC) ?	" OverCurrent" : "",
		(status & USBPORTSC_PR) ?	" Reset" : "",
		(status & USBPORTSC_LSDA) ?	" LowSpeed" : "",
		(status & USBPORTSC_RD) ?	" ResumeDetect" : "",
		(status & USBPORTSC_PEC) ?	" EnableChange" : "",
		(status & USBPORTSC_PE) ?	" Enabled" : "",
		(status & USBPORTSC_CSC) ?	" ConnectChange" : "",
		(status & USBPORTSC_CCS) ?	" Connected" : "");

	return out - buf;
}

static int uhci_show_root_hub_state(struct uhci_hcd *uhci, char *buf, int len)
{
	char *out = buf;
	char *rh_state;

	/* Try to make sure there's enough memory */
	if (len < 60)
		return 0;

	switch (uhci->rh_state) {
	    case UHCI_RH_RESET:
		rh_state = "reset";		break;
	    case UHCI_RH_SUSPENDED:
		rh_state = "suspended";		break;
	    case UHCI_RH_AUTO_STOPPED:
		rh_state = "auto-stopped";	break;
	    case UHCI_RH_RESUMING:
		rh_state = "resuming";		break;
	    case UHCI_RH_SUSPENDING:
		rh_state = "suspending";	break;
	    case UHCI_RH_RUNNING:
		rh_state = "running";		break;
	    case UHCI_RH_RUNNING_NODEVS:
		rh_state = "running, no devs";	break;
	    default:
		rh_state = "?";			break;
	}
	out += sprintf(out, "Root-hub state: %s   FSBR: %d\n",
			rh_state, uhci->fsbr_is_on);
	return out - buf;
}

static int uhci_show_status(struct uhci_hcd *uhci, char *buf, int len)
{
	char *out = buf;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	/* Try to make sure there's enough memory */
	if (len < 80 * 9)
		return 0;

	usbcmd    = uhci_readw(uhci, 0);
	usbstat   = uhci_readw(uhci, 2);
	usbint    = uhci_readw(uhci, 4);
	usbfrnum  = uhci_readw(uhci, 6);
	flbaseadd = uhci_readl(uhci, 8);
	sof       = uhci_readb(uhci, 12);
	portsc1   = uhci_readw(uhci, 16);
	portsc2   = uhci_readw(uhci, 18);

	out += sprintf(out, "  usbcmd    =     %04x   %s%s%s%s%s%s%s%s\n",
		usbcmd,
		(usbcmd & USBCMD_MAXP) ?    "Maxp64 " : "Maxp32 ",
		(usbcmd & USBCMD_CF) ?      "CF " : "",
		(usbcmd & USBCMD_SWDBG) ?   "SWDBG " : "",
		(usbcmd & USBCMD_FGR) ?     "FGR " : "",
		(usbcmd & USBCMD_EGSM) ?    "EGSM " : "",
		(usbcmd & USBCMD_GRESET) ?  "GRESET " : "",
		(usbcmd & USBCMD_HCRESET) ? "HCRESET " : "",
		(usbcmd & USBCMD_RS) ?      "RS " : "");

	out += sprintf(out, "  usbstat   =     %04x   %s%s%s%s%s%s\n",
		usbstat,
		(usbstat & USBSTS_HCH) ?    "HCHalted " : "",
		(usbstat & USBSTS_HCPE) ?   "HostControllerProcessError " : "",
		(usbstat & USBSTS_HSE) ?    "HostSystemError " : "",
		(usbstat & USBSTS_RD) ?     "ResumeDetect " : "",
		(usbstat & USBSTS_ERROR) ?  "USBError " : "",
		(usbstat & USBSTS_USBINT) ? "USBINT " : "");

	out += sprintf(out, "  usbint    =     %04x\n", usbint);
	out += sprintf(out, "  usbfrnum  =   (%d)%03x\n", (usbfrnum >> 10) & 1,
		0xfff & (4*(unsigned int)usbfrnum));
	out += sprintf(out, "  flbaseadd = %08x\n", flbaseadd);
	out += sprintf(out, "  sof       =       %02x\n", sof);
	out += uhci_show_sc(1, portsc1, out, len - (out - buf));
	out += uhci_show_sc(2, portsc2, out, len - (out - buf));
	out += sprintf(out, "Most recent frame: %x (%d)   "
			"Last ISO frame: %x (%d)\n",
			uhci->frame_number, uhci->frame_number & 1023,
			uhci->last_iso_frame, uhci->last_iso_frame & 1023);

	return out - buf;
}

static int uhci_sprint_schedule(struct uhci_hcd *uhci, char *buf, int len)
{
	char *out = buf;
	int i, j;
	struct uhci_qh *qh;
	struct uhci_td *td;
	struct list_head *tmp, *head;
	int nframes, nerrs;
	__hc32 link;
	__hc32 fsbr_link;

	static const char * const qh_names[] = {
		"unlink", "iso", "int128", "int64", "int32", "int16",
		"int8", "int4", "int2", "async", "term"
	};

	out += uhci_show_root_hub_state(uhci, out, len - (out - buf));
	out += sprintf(out, "HC status\n");
	out += uhci_show_status(uhci, out, len - (out - buf));

	out += sprintf(out, "Periodic load table\n");
	for (i = 0; i < MAX_PHASE; ++i) {
		out += sprintf(out, "\t%d", uhci->load[i]);
		if (i % 8 == 7)
			*out++ = '\n';
	}
	out += sprintf(out, "Total: %d, #INT: %d, #ISO: %d\n",
			uhci->total_load,
			uhci_to_hcd(uhci)->self.bandwidth_int_reqs,
			uhci_to_hcd(uhci)->self.bandwidth_isoc_reqs);
	if (debug <= 1)
		return out - buf;

	out += sprintf(out, "Frame List\n");
	nframes = 10;
	nerrs = 0;
	for (i = 0; i < UHCI_NUMFRAMES; ++i) {
		__hc32 qh_dma;

		j = 0;
		td = uhci->frame_cpu[i];
		link = uhci->frame[i];
		if (!td)
			goto check_link;

		if (nframes > 0) {
			out += sprintf(out, "- Frame %d -> (%08x)\n",
					i, hc32_to_cpu(uhci, link));
			j = 1;
		}

		head = &td->fl_list;
		tmp = head;
		do {
			td = list_entry(tmp, struct uhci_td, fl_list);
			tmp = tmp->next;
			if (link != LINK_TO_TD(uhci, td)) {
				if (nframes > 0)
					out += sprintf(out, "    link does "
						"not match list entry!\n");
				else
					++nerrs;
			}
			if (nframes > 0)
				out += uhci_show_td(uhci, td, out,
						len - (out - buf), 4);
			link = td->link;
		} while (tmp != head);

check_link:
		qh_dma = uhci_frame_skel_link(uhci, i);
		if (link != qh_dma) {
			if (nframes > 0) {
				if (!j) {
					out += sprintf(out,
						"- Frame %d -> (%08x)\n",
						i, hc32_to_cpu(uhci, link));
					j = 1;
				}
				out += sprintf(out, "   link does not match "
					"QH (%08x)!\n",
					hc32_to_cpu(uhci, qh_dma));
			} else
				++nerrs;
		}
		nframes -= j;
	}
	if (nerrs > 0)
		out += sprintf(out, "Skipped %d bad links\n", nerrs);

	out += sprintf(out, "Skeleton QHs\n");

	fsbr_link = 0;
	for (i = 0; i < UHCI_NUM_SKELQH; ++i) {
		int cnt = 0;

		qh = uhci->skelqh[i];
		out += sprintf(out, "- skel_%s_qh\n", qh_names[i]); \
		out += uhci_show_qh(uhci, qh, out, len - (out - buf), 4);

		/* Last QH is the Terminating QH, it's different */
		if (i == SKEL_TERM) {
			if (qh_element(qh) != LINK_TO_TD(uhci, uhci->term_td))
				out += sprintf(out, "    skel_term_qh element is not set to term_td!\n");
			link = fsbr_link;
			if (!link)
				link = LINK_TO_QH(uhci, uhci->skel_term_qh);
			goto check_qh_link;
		}

		head = &qh->node;
		tmp = head->next;

		while (tmp != head) {
			qh = list_entry(tmp, struct uhci_qh, node);
			tmp = tmp->next;
			if (++cnt <= 10)
				out += uhci_show_qh(uhci, qh, out,
						len - (out - buf), 4);
			if (!fsbr_link && qh->skel >= SKEL_FSBR)
				fsbr_link = LINK_TO_QH(uhci, qh);
		}
		if ((cnt -= 10) > 0)
			out += sprintf(out, "    Skipped %d QHs\n", cnt);

		link = UHCI_PTR_TERM(uhci);
		if (i <= SKEL_ISO)
			;
		else if (i < SKEL_ASYNC)
			link = LINK_TO_QH(uhci, uhci->skel_async_qh);
		else if (!uhci->fsbr_is_on)
			;
		else
			link = LINK_TO_QH(uhci, uhci->skel_term_qh);
check_qh_link:
		if (qh->link != link)
			out += sprintf(out, "    last QH not linked to next skeleton!\n");
	}

	return out - buf;
}

#ifdef CONFIG_DEBUG_FS

#define MAX_OUTPUT	(64 * 1024)

struct uhci_debug {
	int size;
	char *data;
};

static int uhci_debug_open(struct inode *inode, struct file *file)
{
	struct uhci_hcd *uhci = inode->i_private;
	struct uhci_debug *up;
	unsigned long flags;

	up = kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	up->data = kmalloc(MAX_OUTPUT, GFP_KERNEL);
	if (!up->data) {
		kfree(up);
		return -ENOMEM;
	}

	up->size = 0;
	spin_lock_irqsave(&uhci->lock, flags);
	if (uhci->is_initialized)
		up->size = uhci_sprint_schedule(uhci, up->data, MAX_OUTPUT);
	spin_unlock_irqrestore(&uhci->lock, flags);

	file->private_data = up;

	return 0;
}

static loff_t uhci_debug_lseek(struct file *file, loff_t off, int whence)
{
	struct uhci_debug *up;
	loff_t new = -1;

	up = file->private_data;

	/* XXX: atomic 64bit seek access, but that needs to be fixed in the VFS */
	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	}

	if (new < 0 || new > up->size)
		return -EINVAL;

	return (file->f_pos = new);
}

static ssize_t uhci_debug_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct uhci_debug *up = file->private_data;
	return simple_read_from_buffer(buf, nbytes, ppos, up->data, up->size);
}

static int uhci_debug_release(struct inode *inode, struct file *file)
{
	struct uhci_debug *up = file->private_data;

	kfree(up->data);
	kfree(up);

	return 0;
}

static const struct file_operations uhci_debug_operations = {
	.owner =	THIS_MODULE,
	.open =		uhci_debug_open,
	.llseek =	uhci_debug_lseek,
	.read =		uhci_debug_read,
	.release =	uhci_debug_release,
};
#define UHCI_DEBUG_OPS

#endif	/* CONFIG_DEBUG_FS */

#else	/* DEBUG */

static inline void lprintk(char *buf)
{}

static inline int uhci_show_qh(struct uhci_hcd *uhci,
		struct uhci_qh *qh, char *buf, int len, int space)
{
	return 0;
}

static inline int uhci_sprint_schedule(struct uhci_hcd *uhci,
		char *buf, int len)
{
	return 0;
}

#endif
