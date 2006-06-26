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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/smp_lock.h>
#include <asm/io.h>

#include "uhci-hcd.h"

#define uhci_debug_operations (* (struct file_operations *) NULL)
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

static int uhci_show_td(struct uhci_td *td, char *buf, int len, int space)
{
	char *out = buf;
	char *spid;
	u32 status, token;

	/* Try to make sure there's enough memory */
	if (len < 160)
		return 0;

	status = td_status(td);
	out += sprintf(out, "%*s[%p] link (%08x) ", space, "", td, le32_to_cpu(td->link));
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

	token = td_token(td);
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
	out += sprintf(out, "(buf=%08x)\n", le32_to_cpu(td->buffer));

	return out - buf;
}

static int uhci_show_urbp(struct urb_priv *urbp, char *buf, int len, int space)
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
	out += sprintf(out, " Actlen=%d", urbp->urb->actual_length);

	if (urbp->urb->status != -EINPROGRESS)
		out += sprintf(out, " Status=%d", urbp->urb->status);
	out += sprintf(out, "\n");

	i = nactive = ninactive = 0;
	list_for_each_entry(td, &urbp->td_list, list) {
		if (urbp->qh->type != USB_ENDPOINT_XFER_ISOC &&
				(++i <= 10 || debug > 2)) {
			out += sprintf(out, "%*s%d: ", space + 2, "", i);
			out += uhci_show_td(td, out, len - (out - buf), 0);
		} else {
			if (td_status(td) & TD_CTRL_ACTIVE)
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

static int uhci_show_qh(struct uhci_qh *qh, char *buf, int len, int space)
{
	char *out = buf;
	int i, nurbs;
	__le32 element = qh_element(qh);
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
			le32_to_cpu(qh->link), le32_to_cpu(element));
	if (qh->type == USB_ENDPOINT_XFER_ISOC)
		out += sprintf(out, "%*s    period %d frame %x desc [%p]\n",
				space, "", qh->period, qh->iso_frame,
				qh->iso_packet_desc);

	if (element & UHCI_PTR_QH)
		out += sprintf(out, "%*s  Element points to QH (bug?)\n", space, "");

	if (element & UHCI_PTR_DEPTH)
		out += sprintf(out, "%*s  Depth traverse\n", space, "");

	if (element & cpu_to_le32(8))
		out += sprintf(out, "%*s  Bit 3 set (bug?)\n", space, "");

	if (!(element & ~(UHCI_PTR_QH | UHCI_PTR_DEPTH)))
		out += sprintf(out, "%*s  Element is NULL (bug?)\n", space, "");

	if (list_empty(&qh->queue)) {
		out += sprintf(out, "%*s  queue is empty\n", space, "");
	} else {
		struct urb_priv *urbp = list_entry(qh->queue.next,
				struct urb_priv, node);
		struct uhci_td *td = list_entry(urbp->td_list.next,
				struct uhci_td, list);

		if (cpu_to_le32(td->dma_handle) != (element & ~UHCI_PTR_BITS))
			out += sprintf(out, "%*s Element != First TD\n",
					space, "");
		i = nurbs = 0;
		list_for_each_entry(urbp, &qh->queue, node) {
			if (++i <= 10)
				out += uhci_show_urbp(urbp, out,
						len - (out - buf), space + 2);
			else
				++nurbs;
		}
		if (nurbs > 0)
			out += sprintf(out, "%*s Skipped %d URBs\n",
					space, "", nurbs);
	}

	if (qh->udev) {
		out += sprintf(out, "%*s  Dummy TD\n", space, "");
		out += uhci_show_td(qh->dummy_td, out, len - (out - buf), 0);
	}

	return out - buf;
}

static const char * const qh_names[] = {
  "skel_unlink_qh", "skel_iso_qh",
  "skel_int128_qh", "skel_int64_qh",
  "skel_int32_qh", "skel_int16_qh",
  "skel_int8_qh", "skel_int4_qh",
  "skel_int2_qh", "skel_int1_qh",
  "skel_ls_control_qh", "skel_fs_control_qh",
  "skel_bulk_qh", "skel_term_qh"
};

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
	unsigned long io_addr = uhci->io_addr;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	/* Try to make sure there's enough memory */
	if (len < 80 * 9)
		return 0;

	usbcmd    = inw(io_addr + 0);
	usbstat   = inw(io_addr + 2);
	usbint    = inw(io_addr + 4);
	usbfrnum  = inw(io_addr + 6);
	flbaseadd = inl(io_addr + 8);
	sof       = inb(io_addr + 12);
	portsc1   = inw(io_addr + 16);
	portsc2   = inw(io_addr + 18);

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

	out += uhci_show_root_hub_state(uhci, out, len - (out - buf));
	out += sprintf(out, "HC status\n");
	out += uhci_show_status(uhci, out, len - (out - buf));
	if (debug <= 1)
		return out - buf;

	out += sprintf(out, "Frame List\n");
	for (i = 0; i < UHCI_NUMFRAMES; ++i) {
		td = uhci->frame_cpu[i];
		if (!td)
			continue;

		out += sprintf(out, "- Frame %d\n", i); \
		if (td->dma_handle != (dma_addr_t)uhci->frame[i])
			out += sprintf(out, "    frame list does not match td->dma_handle!\n");

		head = &td->fl_list;
		tmp = head;
		do {
			td = list_entry(tmp, struct uhci_td, fl_list);
			tmp = tmp->next;
			out += uhci_show_td(td, out, len - (out - buf), 4);
		} while (tmp != head);
	}

	out += sprintf(out, "Skeleton QHs\n");

	for (i = 0; i < UHCI_NUM_SKELQH; ++i) {
		int cnt = 0;

		qh = uhci->skelqh[i];
		out += sprintf(out, "- %s\n", qh_names[i]); \
		out += uhci_show_qh(qh, out, len - (out - buf), 4);

		/* Last QH is the Terminating QH, it's different */
		if (i == UHCI_NUM_SKELQH - 1) {
			if (qh->link != UHCI_PTR_TERM)
				out += sprintf(out, "    bandwidth reclamation on!\n");

			if (qh_element(qh) != cpu_to_le32(uhci->term_td->dma_handle))
				out += sprintf(out, "    skel_term_qh element is not set to term_td!\n");

			continue;
		}

		j = (i < 9) ? 9 : i+1;		/* Next skeleton */
		head = &qh->node;
		tmp = head->next;

		while (tmp != head) {
			qh = list_entry(tmp, struct uhci_qh, node);
			tmp = tmp->next;
			if (++cnt <= 10)
				out += uhci_show_qh(qh, out,
						len - (out - buf), 4);
		}
		if ((cnt -= 10) > 0)
			out += sprintf(out, "    Skipped %d QHs\n", cnt);

		if (i > 1 && i < UHCI_NUM_SKELQH - 1) {
			if (qh->link !=
			    (cpu_to_le32(uhci->skelqh[j]->dma_handle) | UHCI_PTR_QH))
				out += sprintf(out, "    last QH not linked to next skeleton!\n");
		}
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
	struct uhci_hcd *uhci = inode->u.generic_ip;
	struct uhci_debug *up;
	int ret = -ENOMEM;
	unsigned long flags;

	lock_kernel();
	up = kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		goto out;

	up->data = kmalloc(MAX_OUTPUT, GFP_KERNEL);
	if (!up->data) {
		kfree(up);
		goto out;
	}

	up->size = 0;
	spin_lock_irqsave(&uhci->lock, flags);
	if (uhci->is_initialized)
		up->size = uhci_sprint_schedule(uhci, up->data, MAX_OUTPUT);
	spin_unlock_irqrestore(&uhci->lock, flags);

	file->private_data = up;

	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static loff_t uhci_debug_lseek(struct file *file, loff_t off, int whence)
{
	struct uhci_debug *up;
	loff_t new = -1;

	lock_kernel();
	up = file->private_data;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	}
	if (new < 0 || new > up->size) {
		unlock_kernel();
		return -EINVAL;
	}
	unlock_kernel();
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

#undef uhci_debug_operations
static struct file_operations uhci_debug_operations = {
	.owner =	THIS_MODULE,
	.open =		uhci_debug_open,
	.llseek =	uhci_debug_lseek,
	.read =		uhci_debug_read,
	.release =	uhci_debug_release,
};

#endif	/* CONFIG_DEBUG_FS */

#else	/* DEBUG */

static inline void lprintk(char *buf)
{}

static inline int uhci_show_qh(struct uhci_qh *qh, char *buf,
		int len, int space)
{
	return 0;
}

static inline int uhci_sprint_schedule(struct uhci_hcd *uhci,
		char *buf, int len)
{
	return 0;
}

#endif
