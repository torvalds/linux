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

static struct dentry *uhci_debugfs_root = NULL;

/* Handle REALLY large printks so we don't overflow buffers */
static inline void lprintk(char *buf)
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

static int uhci_show_qh(struct uhci_qh *qh, char *buf, int len, int space)
{
	char *out = buf;
	struct urb_priv *urbp;
	struct list_head *head, *tmp;
	struct uhci_td *td;
	int i = 0, checked = 0, prevactive = 0;
	__le32 element = qh_element(qh);

	/* Try to make sure there's enough memory */
	if (len < 80 * 6)
		return 0;

	out += sprintf(out, "%*s[%p] link (%08x) element (%08x)\n", space, "",
			qh, le32_to_cpu(qh->link), le32_to_cpu(element));

	if (element & UHCI_PTR_QH)
		out += sprintf(out, "%*s  Element points to QH (bug?)\n", space, "");

	if (element & UHCI_PTR_DEPTH)
		out += sprintf(out, "%*s  Depth traverse\n", space, "");

	if (element & cpu_to_le32(8))
		out += sprintf(out, "%*s  Bit 3 set (bug?)\n", space, "");

	if (!(element & ~(UHCI_PTR_QH | UHCI_PTR_DEPTH)))
		out += sprintf(out, "%*s  Element is NULL (bug?)\n", space, "");

	if (!qh->urbp) {
		out += sprintf(out, "%*s  urbp == NULL\n", space, "");
		goto out;
	}

	urbp = qh->urbp;

	head = &urbp->td_list;
	tmp = head->next;

	td = list_entry(tmp, struct uhci_td, list);

	if (cpu_to_le32(td->dma_handle) != (element & ~UHCI_PTR_BITS))
		out += sprintf(out, "%*s Element != First TD\n", space, "");

	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		out += sprintf(out, "%*s%d: ", space + 2, "", i++);
		out += uhci_show_td(td, out, len - (out - buf), 0);

		if (i > 10 && !checked && prevactive && tmp != head &&
		    debug <= 2) {
			struct list_head *ntmp = tmp;
			struct uhci_td *ntd = td;
			int active = 1, ni = i;

			checked = 1;

			while (ntmp != head && ntmp->next != head && active) {
				ntd = list_entry(ntmp, struct uhci_td, list);

				ntmp = ntmp->next;

				active = td_status(ntd) & TD_CTRL_ACTIVE;

				ni++;
			}

			if (active && ni > i) {
				out += sprintf(out, "%*s[skipped %d active TDs]\n", space, "", ni - i);
				tmp = ntmp;
				td = ntd;
				i = ni;
			}
		}

		prevactive = td_status(td) & TD_CTRL_ACTIVE;
	}

	if (list_empty(&urbp->queue_list) || urbp->queued)
		goto out;

	out += sprintf(out, "%*sQueued QHs:\n", -space, "--");

	head = &urbp->queue_list;
	tmp = head->next;

	while (tmp != head) {
		struct urb_priv *nurbp = list_entry(tmp, struct urb_priv,
						queue_list);
		tmp = tmp->next;

		out += uhci_show_qh(nurbp->qh, out, len - (out - buf), space);
	}

out:
	return out - buf;
}

#define show_frame_num()	\
	if (!shown) {		\
	  shown = 1;		\
	  out += sprintf(out, "- Frame %d\n", i); \
	}

#ifdef CONFIG_PROC_FS
static const char * const qh_names[] = {
  "skel_int128_qh", "skel_int64_qh",
  "skel_int32_qh", "skel_int16_qh",
  "skel_int8_qh", "skel_int4_qh",
  "skel_int2_qh", "skel_int1_qh",
  "skel_ls_control_qh", "skel_fs_control_qh",
  "skel_bulk_qh", "skel_term_qh"
};

#define show_qh_name()		\
	if (!shown) {		\
	  shown = 1;		\
	  out += sprintf(out, "- %s\n", qh_names[i]); \
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
	out += sprintf(out, "Root-hub state: %s\n", rh_state);
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
	if (len < 80 * 6)
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

	return out - buf;
}

static int uhci_show_urbp(struct uhci_hcd *uhci, struct urb_priv *urbp, char *buf, int len)
{
	struct list_head *tmp;
	char *out = buf;
	int count = 0;

	if (len < 200)
		return 0;

	out += sprintf(out, "urb_priv [%p] ", urbp);
	out += sprintf(out, "urb [%p] ", urbp->urb);
	out += sprintf(out, "qh [%p] ", urbp->qh);
	out += sprintf(out, "Dev=%d ", usb_pipedevice(urbp->urb->pipe));
	out += sprintf(out, "EP=%x(%s) ", usb_pipeendpoint(urbp->urb->pipe), (usb_pipein(urbp->urb->pipe) ? "IN" : "OUT"));

	switch (usb_pipetype(urbp->urb->pipe)) {
	case PIPE_ISOCHRONOUS: out += sprintf(out, "ISO "); break;
	case PIPE_INTERRUPT: out += sprintf(out, "INT "); break;
	case PIPE_BULK: out += sprintf(out, "BLK "); break;
	case PIPE_CONTROL: out += sprintf(out, "CTL "); break;
	}

	out += sprintf(out, "%s", (urbp->fsbr ? "FSBR " : ""));
	out += sprintf(out, "%s", (urbp->fsbr_timeout ? "FSBR_TO " : ""));

	if (urbp->urb->status != -EINPROGRESS)
		out += sprintf(out, "Status=%d ", urbp->urb->status);
	//out += sprintf(out, "FSBRtime=%lx ",urbp->fsbrtime);

	count = 0;
	list_for_each(tmp, &urbp->td_list)
		count++;
	out += sprintf(out, "TDs=%d ",count);

	if (urbp->queued)
		out += sprintf(out, "queued\n");
	else {
		count = 0;
		list_for_each(tmp, &urbp->queue_list)
			count++;
		out += sprintf(out, "queued URBs=%d\n", count);
	}

	return out - buf;
}

static int uhci_show_lists(struct uhci_hcd *uhci, char *buf, int len)
{
	char *out = buf;
	struct list_head *head, *tmp;
	int count;

	out += sprintf(out, "Main list URBs:");
	if (list_empty(&uhci->urb_list))
		out += sprintf(out, " Empty\n");
	else {
		out += sprintf(out, "\n");
		count = 0;
		head = &uhci->urb_list;
		tmp = head->next;
		while (tmp != head) {
			struct urb_priv *urbp = list_entry(tmp, struct urb_priv, urb_list);

			out += sprintf(out, "  %d: ", ++count);
			out += uhci_show_urbp(uhci, urbp, out, len - (out - buf));
			tmp = tmp->next;
		}
	}

	out += sprintf(out, "Remove list URBs:");
	if (list_empty(&uhci->urb_remove_list))
		out += sprintf(out, " Empty\n");
	else {
		out += sprintf(out, "\n");
		count = 0;
		head = &uhci->urb_remove_list;
		tmp = head->next;
		while (tmp != head) {
			struct urb_priv *urbp = list_entry(tmp, struct urb_priv, urb_list);

			out += sprintf(out, "  %d: ", ++count);
			out += uhci_show_urbp(uhci, urbp, out, len - (out - buf));
			tmp = tmp->next;
		}
	}

	out += sprintf(out, "Complete list URBs:");
	if (list_empty(&uhci->complete_list))
		out += sprintf(out, " Empty\n");
	else {
		out += sprintf(out, "\n");
		count = 0;
		head = &uhci->complete_list;
		tmp = head->next;
		while (tmp != head) {
			struct urb_priv *urbp = list_entry(tmp, struct urb_priv, urb_list);

			out += sprintf(out, "  %d: ", ++count);
			out += uhci_show_urbp(uhci, urbp, out, len - (out - buf));
			tmp = tmp->next;
		}
	}

	return out - buf;
}

static int uhci_sprint_schedule(struct uhci_hcd *uhci, char *buf, int len)
{
	unsigned long flags;
	char *out = buf;
	int i, j;
	struct uhci_qh *qh;
	struct uhci_td *td;
	struct list_head *tmp, *head;

	spin_lock_irqsave(&uhci->lock, flags);

	out += uhci_show_root_hub_state(uhci, out, len - (out - buf));
	out += sprintf(out, "HC status\n");
	out += uhci_show_status(uhci, out, len - (out - buf));

	out += sprintf(out, "Frame List\n");
	for (i = 0; i < UHCI_NUMFRAMES; ++i) {
		int shown = 0;
		td = uhci->frame_cpu[i];
		if (!td)
			continue;

		if (td->dma_handle != (dma_addr_t)uhci->frame[i]) {
			show_frame_num();
			out += sprintf(out, "    frame list does not match td->dma_handle!\n");
		}
		show_frame_num();

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
		int shown = 0;

		qh = uhci->skelqh[i];

		if (debug > 1) {
			show_qh_name();
			out += uhci_show_qh(qh, out, len - (out - buf), 4);
		}

		/* Last QH is the Terminating QH, it's different */
		if (i == UHCI_NUM_SKELQH - 1) {
			if (qh->link != UHCI_PTR_TERM)
				out += sprintf(out, "    bandwidth reclamation on!\n");

			if (qh_element(qh) != cpu_to_le32(uhci->term_td->dma_handle))
				out += sprintf(out, "    skel_term_qh element is not set to term_td!\n");

			continue;
		}

		j = (i < 7) ? 7 : i+1;		/* Next skeleton */
		if (list_empty(&qh->list)) {
			if (i < UHCI_NUM_SKELQH - 1) {
				if (qh->link !=
				    (cpu_to_le32(uhci->skelqh[j]->dma_handle) | UHCI_PTR_QH)) {
					show_qh_name();
					out += sprintf(out, "    skeleton QH not linked to next skeleton QH!\n");
				}
			}

			continue;
		}

		show_qh_name();

		head = &qh->list;
		tmp = head->next;

		while (tmp != head) {
			qh = list_entry(tmp, struct uhci_qh, list);

			tmp = tmp->next;

			out += uhci_show_qh(qh, out, len - (out - buf), 4);
		}

		if (i < UHCI_NUM_SKELQH - 1) {
			if (qh->link !=
			    (cpu_to_le32(uhci->skelqh[j]->dma_handle) | UHCI_PTR_QH))
				out += sprintf(out, "    last QH not linked to next skeleton!\n");
		}
	}

	if (debug > 2)
		out += uhci_show_lists(uhci, out, len - (out - buf));

	spin_unlock_irqrestore(&uhci->lock, flags);

	return out - buf;
}

#define MAX_OUTPUT	(64 * 1024)

struct uhci_debug {
	int size;
	char *data;
	struct uhci_hcd *uhci;
};

static int uhci_debug_open(struct inode *inode, struct file *file)
{
	struct uhci_hcd *uhci = inode->u.generic_ip;
	struct uhci_debug *up;
	int ret = -ENOMEM;

	lock_kernel();
	up = kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		goto out;

	up->data = kmalloc(MAX_OUTPUT, GFP_KERNEL);
	if (!up->data) {
		kfree(up);
		goto out;
	}

	up->size = uhci_sprint_schedule(uhci, up->data, MAX_OUTPUT);

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

static struct file_operations uhci_debug_operations = {
	.open =		uhci_debug_open,
	.llseek =	uhci_debug_lseek,
	.read =		uhci_debug_read,
	.release =	uhci_debug_release,
};

#else	/* CONFIG_DEBUG_FS */

#define uhci_debug_operations (* (struct file_operations *) NULL)

#endif
