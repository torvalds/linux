// SPDX-License-Identifier: GPL-2.0
/*
 * debugfs.c - Designware USB2 DRD controller debugfs
 *
 * Copyright (C) 2015 Intel Corporation
 * Mian Yousaf Kaukab <yousaf.kaukab@intel.com>
 */

#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "core.h"
#include "debug.h"

#if IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL) || \
	IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE)

/**
 * testmode_write() - change usb test mode state.
 * @file: The  file to write to.
 * @ubuf: The buffer where user wrote.
 * @count: The ubuf size.
 * @ppos: Unused parameter.
 */
static ssize_t testmode_write(struct file *file, const char __user *ubuf, size_t
		count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc2_hsotg	*hsotg = s->private;
	unsigned long		flags;
	u32			testmode = 0;
	char			buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "test_j", 6))
		testmode = TEST_J;
	else if (!strncmp(buf, "test_k", 6))
		testmode = TEST_K;
	else if (!strncmp(buf, "test_se0_nak", 12))
		testmode = TEST_SE0_NAK;
	else if (!strncmp(buf, "test_packet", 11))
		testmode = TEST_PACKET;
	else if (!strncmp(buf, "test_force_enable", 17))
		testmode = TEST_FORCE_EN;
	else
		testmode = 0;

	spin_lock_irqsave(&hsotg->lock, flags);
	dwc2_hsotg_set_test_mode(hsotg, testmode);
	spin_unlock_irqrestore(&hsotg->lock, flags);
	return count;
}

/**
 * testmode_show() - debugfs: show usb test mode state
 * @s: The seq file to write to.
 * @unused: Unused parameter.
 *
 * This debugfs entry shows which usb test mode is currently enabled.
 */
static int testmode_show(struct seq_file *s, void *unused)
{
	struct dwc2_hsotg *hsotg = s->private;
	unsigned long flags;
	int dctl;

	spin_lock_irqsave(&hsotg->lock, flags);
	dctl = dwc2_readl(hsotg, DCTL);
	dctl &= DCTL_TSTCTL_MASK;
	dctl >>= DCTL_TSTCTL_SHIFT;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	switch (dctl) {
	case 0:
		seq_puts(s, "no test\n");
		break;
	case TEST_J:
		seq_puts(s, "test_j\n");
		break;
	case TEST_K:
		seq_puts(s, "test_k\n");
		break;
	case TEST_SE0_NAK:
		seq_puts(s, "test_se0_nak\n");
		break;
	case TEST_PACKET:
		seq_puts(s, "test_packet\n");
		break;
	case TEST_FORCE_EN:
		seq_puts(s, "test_force_enable\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %d\n", dctl);
	}

	return 0;
}

static int testmode_open(struct inode *inode, struct file *file)
{
	return single_open(file, testmode_show, inode->i_private);
}

static const struct file_operations testmode_fops = {
	.owner		= THIS_MODULE,
	.open		= testmode_open,
	.write		= testmode_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * state_show - debugfs: show overall driver and device state.
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the overall state of the hardware and
 * some general information about each of the endpoints available
 * to the system.
 */
static int state_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *hsotg = seq->private;
	int idx;

	seq_printf(seq, "DCFG=0x%08x, DCTL=0x%08x, DSTS=0x%08x\n",
		   dwc2_readl(hsotg, DCFG),
		 dwc2_readl(hsotg, DCTL),
		 dwc2_readl(hsotg, DSTS));

	seq_printf(seq, "DIEPMSK=0x%08x, DOEPMASK=0x%08x\n",
		   dwc2_readl(hsotg, DIEPMSK), dwc2_readl(hsotg, DOEPMSK));

	seq_printf(seq, "GINTMSK=0x%08x, GINTSTS=0x%08x\n",
		   dwc2_readl(hsotg, GINTMSK),
		   dwc2_readl(hsotg, GINTSTS));

	seq_printf(seq, "DAINTMSK=0x%08x, DAINT=0x%08x\n",
		   dwc2_readl(hsotg, DAINTMSK),
		   dwc2_readl(hsotg, DAINT));

	seq_printf(seq, "GNPTXSTS=0x%08x, GRXSTSR=%08x\n",
		   dwc2_readl(hsotg, GNPTXSTS),
		   dwc2_readl(hsotg, GRXSTSR));

	seq_puts(seq, "\nEndpoint status:\n");

	for (idx = 0; idx < hsotg->num_of_eps; idx++) {
		u32 in, out;

		in = dwc2_readl(hsotg, DIEPCTL(idx));
		out = dwc2_readl(hsotg, DOEPCTL(idx));

		seq_printf(seq, "ep%d: DIEPCTL=0x%08x, DOEPCTL=0x%08x",
			   idx, in, out);

		in = dwc2_readl(hsotg, DIEPTSIZ(idx));
		out = dwc2_readl(hsotg, DOEPTSIZ(idx));

		seq_printf(seq, ", DIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x",
			   in, out);

		seq_puts(seq, "\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(state);

/**
 * fifo_show - debugfs: show the fifo information
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * Show the FIFO information for the overall fifo and all the
 * periodic transmission FIFOs.
 */
static int fifo_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *hsotg = seq->private;
	u32 val;
	int idx;

	seq_puts(seq, "Non-periodic FIFOs:\n");
	seq_printf(seq, "RXFIFO: Size %d\n", dwc2_readl(hsotg, GRXFSIZ));

	val = dwc2_readl(hsotg, GNPTXFSIZ);
	seq_printf(seq, "NPTXFIFO: Size %d, Start 0x%08x\n",
		   val >> FIFOSIZE_DEPTH_SHIFT,
		   val & FIFOSIZE_STARTADDR_MASK);

	seq_puts(seq, "\nPeriodic TXFIFOs:\n");

	for (idx = 1; idx < hsotg->num_of_eps; idx++) {
		val = dwc2_readl(hsotg, DPTXFSIZN(idx));

		seq_printf(seq, "\tDPTXFIFO%2d: Size %d, Start 0x%08x\n", idx,
			   val >> FIFOSIZE_DEPTH_SHIFT,
			   val & FIFOSIZE_STARTADDR_MASK);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fifo);

static const char *decode_direction(int is_in)
{
	return is_in ? "in" : "out";
}

/**
 * ep_show - debugfs: show the state of an endpoint.
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the state of the given endpoint (one is
 * registered for each available).
 */
static int ep_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg_ep *ep = seq->private;
	struct dwc2_hsotg *hsotg = ep->parent;
	struct dwc2_hsotg_req *req;
	int index = ep->index;
	int show_limit = 15;
	unsigned long flags;

	seq_printf(seq, "Endpoint index %d, named %s,  dir %s:\n",
		   ep->index, ep->ep.name, decode_direction(ep->dir_in));

	/* first show the register state */

	seq_printf(seq, "\tDIEPCTL=0x%08x, DOEPCTL=0x%08x\n",
		   dwc2_readl(hsotg, DIEPCTL(index)),
		   dwc2_readl(hsotg, DOEPCTL(index)));

	seq_printf(seq, "\tDIEPDMA=0x%08x, DOEPDMA=0x%08x\n",
		   dwc2_readl(hsotg, DIEPDMA(index)),
		   dwc2_readl(hsotg, DOEPDMA(index)));

	seq_printf(seq, "\tDIEPINT=0x%08x, DOEPINT=0x%08x\n",
		   dwc2_readl(hsotg, DIEPINT(index)),
		   dwc2_readl(hsotg, DOEPINT(index)));

	seq_printf(seq, "\tDIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x\n",
		   dwc2_readl(hsotg, DIEPTSIZ(index)),
		   dwc2_readl(hsotg, DOEPTSIZ(index)));

	seq_puts(seq, "\n");
	seq_printf(seq, "mps %d\n", ep->ep.maxpacket);
	seq_printf(seq, "total_data=%ld\n", ep->total_data);

	seq_printf(seq, "request list (%p,%p):\n",
		   ep->queue.next, ep->queue.prev);

	spin_lock_irqsave(&hsotg->lock, flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (--show_limit < 0) {
			seq_puts(seq, "not showing more requests...\n");
			break;
		}

		seq_printf(seq, "%c req %p: %d bytes @%p, ",
			   req == ep->req ? '*' : ' ',
			   req, req->req.length, req->req.buf);
		seq_printf(seq, "%d done, res %d\n",
			   req->req.actual, req->req.status);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ep);

/**
 * dwc2_hsotg_create_debug - create debugfs directory and files
 * @hsotg: The driver state
 *
 * Create the debugfs files to allow the user to get information
 * about the state of the system. The directory name is created
 * with the same name as the device itself, in case we end up
 * with multiple blocks in future systems.
 */
static void dwc2_hsotg_create_debug(struct dwc2_hsotg *hsotg)
{
	struct dentry *root;
	unsigned int epidx;

	root = hsotg->debug_root;

	/* create general state file */
	debugfs_create_file("state", 0444, root, hsotg, &state_fops);
	debugfs_create_file("testmode", 0644, root, hsotg, &testmode_fops);
	debugfs_create_file("fifo", 0444, root, hsotg, &fifo_fops);

	/* Create one file for each out endpoint */
	for (epidx = 0; epidx < hsotg->num_of_eps; epidx++) {
		struct dwc2_hsotg_ep *ep;

		ep = hsotg->eps_out[epidx];
		if (ep)
			debugfs_create_file(ep->name, 0444, root, ep, &ep_fops);
	}
	/* Create one file for each in endpoint. EP0 is handled with out eps */
	for (epidx = 1; epidx < hsotg->num_of_eps; epidx++) {
		struct dwc2_hsotg_ep *ep;

		ep = hsotg->eps_in[epidx];
		if (ep)
			debugfs_create_file(ep->name, 0444, root, ep, &ep_fops);
	}
}
#else
static inline void dwc2_hsotg_create_debug(struct dwc2_hsotg *hsotg) {}
#endif

/* dwc2_hsotg_delete_debug is removed as cleanup in done in dwc2_debugfs_exit */

#define dump_register(nm)	\
{				\
	.name	= #nm,		\
	.offset	= nm,		\
}

static const struct debugfs_reg32 dwc2_regs[] = {
	/*
	 * Accessing registers like this can trigger mode mismatch interrupt.
	 * However, according to dwc2 databook, the register access, in this
	 * case, is completed on the processor bus but is ignored by the core
	 * and does not affect its operation.
	 */
	dump_register(GOTGCTL),
	dump_register(GOTGINT),
	dump_register(GAHBCFG),
	dump_register(GUSBCFG),
	dump_register(GRSTCTL),
	dump_register(GINTSTS),
	dump_register(GINTMSK),
	dump_register(GRXSTSR),
	/* Omit GRXSTSP */
	dump_register(GRXFSIZ),
	dump_register(GNPTXFSIZ),
	dump_register(GNPTXSTS),
	dump_register(GI2CCTL),
	dump_register(GPVNDCTL),
	dump_register(GGPIO),
	dump_register(GUID),
	dump_register(GSNPSID),
	dump_register(GHWCFG1),
	dump_register(GHWCFG2),
	dump_register(GHWCFG3),
	dump_register(GHWCFG4),
	dump_register(GLPMCFG),
	dump_register(GPWRDN),
	dump_register(GDFIFOCFG),
	dump_register(ADPCTL),
	dump_register(HPTXFSIZ),
	dump_register(DPTXFSIZN(1)),
	dump_register(DPTXFSIZN(2)),
	dump_register(DPTXFSIZN(3)),
	dump_register(DPTXFSIZN(4)),
	dump_register(DPTXFSIZN(5)),
	dump_register(DPTXFSIZN(6)),
	dump_register(DPTXFSIZN(7)),
	dump_register(DPTXFSIZN(8)),
	dump_register(DPTXFSIZN(9)),
	dump_register(DPTXFSIZN(10)),
	dump_register(DPTXFSIZN(11)),
	dump_register(DPTXFSIZN(12)),
	dump_register(DPTXFSIZN(13)),
	dump_register(DPTXFSIZN(14)),
	dump_register(DPTXFSIZN(15)),
	dump_register(DCFG),
	dump_register(DCTL),
	dump_register(DSTS),
	dump_register(DIEPMSK),
	dump_register(DOEPMSK),
	dump_register(DAINT),
	dump_register(DAINTMSK),
	dump_register(DTKNQR1),
	dump_register(DTKNQR2),
	dump_register(DTKNQR3),
	dump_register(DTKNQR4),
	dump_register(DVBUSDIS),
	dump_register(DVBUSPULSE),
	dump_register(DIEPCTL(0)),
	dump_register(DIEPCTL(1)),
	dump_register(DIEPCTL(2)),
	dump_register(DIEPCTL(3)),
	dump_register(DIEPCTL(4)),
	dump_register(DIEPCTL(5)),
	dump_register(DIEPCTL(6)),
	dump_register(DIEPCTL(7)),
	dump_register(DIEPCTL(8)),
	dump_register(DIEPCTL(9)),
	dump_register(DIEPCTL(10)),
	dump_register(DIEPCTL(11)),
	dump_register(DIEPCTL(12)),
	dump_register(DIEPCTL(13)),
	dump_register(DIEPCTL(14)),
	dump_register(DIEPCTL(15)),
	dump_register(DOEPCTL(0)),
	dump_register(DOEPCTL(1)),
	dump_register(DOEPCTL(2)),
	dump_register(DOEPCTL(3)),
	dump_register(DOEPCTL(4)),
	dump_register(DOEPCTL(5)),
	dump_register(DOEPCTL(6)),
	dump_register(DOEPCTL(7)),
	dump_register(DOEPCTL(8)),
	dump_register(DOEPCTL(9)),
	dump_register(DOEPCTL(10)),
	dump_register(DOEPCTL(11)),
	dump_register(DOEPCTL(12)),
	dump_register(DOEPCTL(13)),
	dump_register(DOEPCTL(14)),
	dump_register(DOEPCTL(15)),
	dump_register(DIEPINT(0)),
	dump_register(DIEPINT(1)),
	dump_register(DIEPINT(2)),
	dump_register(DIEPINT(3)),
	dump_register(DIEPINT(4)),
	dump_register(DIEPINT(5)),
	dump_register(DIEPINT(6)),
	dump_register(DIEPINT(7)),
	dump_register(DIEPINT(8)),
	dump_register(DIEPINT(9)),
	dump_register(DIEPINT(10)),
	dump_register(DIEPINT(11)),
	dump_register(DIEPINT(12)),
	dump_register(DIEPINT(13)),
	dump_register(DIEPINT(14)),
	dump_register(DIEPINT(15)),
	dump_register(DOEPINT(0)),
	dump_register(DOEPINT(1)),
	dump_register(DOEPINT(2)),
	dump_register(DOEPINT(3)),
	dump_register(DOEPINT(4)),
	dump_register(DOEPINT(5)),
	dump_register(DOEPINT(6)),
	dump_register(DOEPINT(7)),
	dump_register(DOEPINT(8)),
	dump_register(DOEPINT(9)),
	dump_register(DOEPINT(10)),
	dump_register(DOEPINT(11)),
	dump_register(DOEPINT(12)),
	dump_register(DOEPINT(13)),
	dump_register(DOEPINT(14)),
	dump_register(DOEPINT(15)),
	dump_register(DIEPTSIZ(0)),
	dump_register(DIEPTSIZ(1)),
	dump_register(DIEPTSIZ(2)),
	dump_register(DIEPTSIZ(3)),
	dump_register(DIEPTSIZ(4)),
	dump_register(DIEPTSIZ(5)),
	dump_register(DIEPTSIZ(6)),
	dump_register(DIEPTSIZ(7)),
	dump_register(DIEPTSIZ(8)),
	dump_register(DIEPTSIZ(9)),
	dump_register(DIEPTSIZ(10)),
	dump_register(DIEPTSIZ(11)),
	dump_register(DIEPTSIZ(12)),
	dump_register(DIEPTSIZ(13)),
	dump_register(DIEPTSIZ(14)),
	dump_register(DIEPTSIZ(15)),
	dump_register(DOEPTSIZ(0)),
	dump_register(DOEPTSIZ(1)),
	dump_register(DOEPTSIZ(2)),
	dump_register(DOEPTSIZ(3)),
	dump_register(DOEPTSIZ(4)),
	dump_register(DOEPTSIZ(5)),
	dump_register(DOEPTSIZ(6)),
	dump_register(DOEPTSIZ(7)),
	dump_register(DOEPTSIZ(8)),
	dump_register(DOEPTSIZ(9)),
	dump_register(DOEPTSIZ(10)),
	dump_register(DOEPTSIZ(11)),
	dump_register(DOEPTSIZ(12)),
	dump_register(DOEPTSIZ(13)),
	dump_register(DOEPTSIZ(14)),
	dump_register(DOEPTSIZ(15)),
	dump_register(DIEPDMA(0)),
	dump_register(DIEPDMA(1)),
	dump_register(DIEPDMA(2)),
	dump_register(DIEPDMA(3)),
	dump_register(DIEPDMA(4)),
	dump_register(DIEPDMA(5)),
	dump_register(DIEPDMA(6)),
	dump_register(DIEPDMA(7)),
	dump_register(DIEPDMA(8)),
	dump_register(DIEPDMA(9)),
	dump_register(DIEPDMA(10)),
	dump_register(DIEPDMA(11)),
	dump_register(DIEPDMA(12)),
	dump_register(DIEPDMA(13)),
	dump_register(DIEPDMA(14)),
	dump_register(DIEPDMA(15)),
	dump_register(DOEPDMA(0)),
	dump_register(DOEPDMA(1)),
	dump_register(DOEPDMA(2)),
	dump_register(DOEPDMA(3)),
	dump_register(DOEPDMA(4)),
	dump_register(DOEPDMA(5)),
	dump_register(DOEPDMA(6)),
	dump_register(DOEPDMA(7)),
	dump_register(DOEPDMA(8)),
	dump_register(DOEPDMA(9)),
	dump_register(DOEPDMA(10)),
	dump_register(DOEPDMA(11)),
	dump_register(DOEPDMA(12)),
	dump_register(DOEPDMA(13)),
	dump_register(DOEPDMA(14)),
	dump_register(DOEPDMA(15)),
	dump_register(DTXFSTS(0)),
	dump_register(DTXFSTS(1)),
	dump_register(DTXFSTS(2)),
	dump_register(DTXFSTS(3)),
	dump_register(DTXFSTS(4)),
	dump_register(DTXFSTS(5)),
	dump_register(DTXFSTS(6)),
	dump_register(DTXFSTS(7)),
	dump_register(DTXFSTS(8)),
	dump_register(DTXFSTS(9)),
	dump_register(DTXFSTS(10)),
	dump_register(DTXFSTS(11)),
	dump_register(DTXFSTS(12)),
	dump_register(DTXFSTS(13)),
	dump_register(DTXFSTS(14)),
	dump_register(DTXFSTS(15)),
	dump_register(PCGCTL),
	dump_register(HCFG),
	dump_register(HFIR),
	dump_register(HFNUM),
	dump_register(HPTXSTS),
	dump_register(HAINT),
	dump_register(HAINTMSK),
	dump_register(HFLBADDR),
	dump_register(HPRT0),
	dump_register(HCCHAR(0)),
	dump_register(HCCHAR(1)),
	dump_register(HCCHAR(2)),
	dump_register(HCCHAR(3)),
	dump_register(HCCHAR(4)),
	dump_register(HCCHAR(5)),
	dump_register(HCCHAR(6)),
	dump_register(HCCHAR(7)),
	dump_register(HCCHAR(8)),
	dump_register(HCCHAR(9)),
	dump_register(HCCHAR(10)),
	dump_register(HCCHAR(11)),
	dump_register(HCCHAR(12)),
	dump_register(HCCHAR(13)),
	dump_register(HCCHAR(14)),
	dump_register(HCCHAR(15)),
	dump_register(HCSPLT(0)),
	dump_register(HCSPLT(1)),
	dump_register(HCSPLT(2)),
	dump_register(HCSPLT(3)),
	dump_register(HCSPLT(4)),
	dump_register(HCSPLT(5)),
	dump_register(HCSPLT(6)),
	dump_register(HCSPLT(7)),
	dump_register(HCSPLT(8)),
	dump_register(HCSPLT(9)),
	dump_register(HCSPLT(10)),
	dump_register(HCSPLT(11)),
	dump_register(HCSPLT(12)),
	dump_register(HCSPLT(13)),
	dump_register(HCSPLT(14)),
	dump_register(HCSPLT(15)),
	dump_register(HCINT(0)),
	dump_register(HCINT(1)),
	dump_register(HCINT(2)),
	dump_register(HCINT(3)),
	dump_register(HCINT(4)),
	dump_register(HCINT(5)),
	dump_register(HCINT(6)),
	dump_register(HCINT(7)),
	dump_register(HCINT(8)),
	dump_register(HCINT(9)),
	dump_register(HCINT(10)),
	dump_register(HCINT(11)),
	dump_register(HCINT(12)),
	dump_register(HCINT(13)),
	dump_register(HCINT(14)),
	dump_register(HCINT(15)),
	dump_register(HCINTMSK(0)),
	dump_register(HCINTMSK(1)),
	dump_register(HCINTMSK(2)),
	dump_register(HCINTMSK(3)),
	dump_register(HCINTMSK(4)),
	dump_register(HCINTMSK(5)),
	dump_register(HCINTMSK(6)),
	dump_register(HCINTMSK(7)),
	dump_register(HCINTMSK(8)),
	dump_register(HCINTMSK(9)),
	dump_register(HCINTMSK(10)),
	dump_register(HCINTMSK(11)),
	dump_register(HCINTMSK(12)),
	dump_register(HCINTMSK(13)),
	dump_register(HCINTMSK(14)),
	dump_register(HCINTMSK(15)),
	dump_register(HCTSIZ(0)),
	dump_register(HCTSIZ(1)),
	dump_register(HCTSIZ(2)),
	dump_register(HCTSIZ(3)),
	dump_register(HCTSIZ(4)),
	dump_register(HCTSIZ(5)),
	dump_register(HCTSIZ(6)),
	dump_register(HCTSIZ(7)),
	dump_register(HCTSIZ(8)),
	dump_register(HCTSIZ(9)),
	dump_register(HCTSIZ(10)),
	dump_register(HCTSIZ(11)),
	dump_register(HCTSIZ(12)),
	dump_register(HCTSIZ(13)),
	dump_register(HCTSIZ(14)),
	dump_register(HCTSIZ(15)),
	dump_register(HCDMA(0)),
	dump_register(HCDMA(1)),
	dump_register(HCDMA(2)),
	dump_register(HCDMA(3)),
	dump_register(HCDMA(4)),
	dump_register(HCDMA(5)),
	dump_register(HCDMA(6)),
	dump_register(HCDMA(7)),
	dump_register(HCDMA(8)),
	dump_register(HCDMA(9)),
	dump_register(HCDMA(10)),
	dump_register(HCDMA(11)),
	dump_register(HCDMA(12)),
	dump_register(HCDMA(13)),
	dump_register(HCDMA(14)),
	dump_register(HCDMA(15)),
	dump_register(HCDMAB(0)),
	dump_register(HCDMAB(1)),
	dump_register(HCDMAB(2)),
	dump_register(HCDMAB(3)),
	dump_register(HCDMAB(4)),
	dump_register(HCDMAB(5)),
	dump_register(HCDMAB(6)),
	dump_register(HCDMAB(7)),
	dump_register(HCDMAB(8)),
	dump_register(HCDMAB(9)),
	dump_register(HCDMAB(10)),
	dump_register(HCDMAB(11)),
	dump_register(HCDMAB(12)),
	dump_register(HCDMAB(13)),
	dump_register(HCDMAB(14)),
	dump_register(HCDMAB(15)),
};

#define print_param(_seq, _ptr, _param) \
seq_printf((_seq), "%-30s: %d\n", #_param, (_ptr)->_param)

#define print_param_hex(_seq, _ptr, _param) \
seq_printf((_seq), "%-30s: 0x%x\n", #_param, (_ptr)->_param)

static int params_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *hsotg = seq->private;
	struct dwc2_core_params *p = &hsotg->params;
	int i;

	print_param(seq, p, otg_cap);
	print_param(seq, p, dma_desc_enable);
	print_param(seq, p, dma_desc_fs_enable);
	print_param(seq, p, speed);
	print_param(seq, p, enable_dynamic_fifo);
	print_param(seq, p, en_multiple_tx_fifo);
	print_param(seq, p, host_rx_fifo_size);
	print_param(seq, p, host_nperio_tx_fifo_size);
	print_param(seq, p, host_perio_tx_fifo_size);
	print_param(seq, p, max_transfer_size);
	print_param(seq, p, max_packet_count);
	print_param(seq, p, host_channels);
	print_param(seq, p, phy_type);
	print_param(seq, p, phy_utmi_width);
	print_param(seq, p, phy_ulpi_ddr);
	print_param(seq, p, phy_ulpi_ext_vbus);
	print_param(seq, p, i2c_enable);
	print_param(seq, p, ipg_isoc_en);
	print_param(seq, p, ulpi_fs_ls);
	print_param(seq, p, host_support_fs_ls_low_power);
	print_param(seq, p, host_ls_low_power_phy_clk);
	print_param(seq, p, ts_dline);
	print_param(seq, p, reload_ctl);
	print_param_hex(seq, p, ahbcfg);
	print_param(seq, p, uframe_sched);
	print_param(seq, p, external_id_pin_ctl);
	print_param(seq, p, power_down);
	print_param(seq, p, lpm);
	print_param(seq, p, lpm_clock_gating);
	print_param(seq, p, besl);
	print_param(seq, p, hird_threshold_en);
	print_param(seq, p, hird_threshold);
	print_param(seq, p, service_interval);
	print_param(seq, p, host_dma);
	print_param(seq, p, g_dma);
	print_param(seq, p, g_dma_desc);
	print_param(seq, p, g_rx_fifo_size);
	print_param(seq, p, g_np_tx_fifo_size);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		char str[32];

		snprintf(str, 32, "g_tx_fifo_size[%d]", i);
		seq_printf(seq, "%-30s: %d\n", str, p->g_tx_fifo_size[i]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(params);

static int hw_params_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *hsotg = seq->private;
	struct dwc2_hw_params *hw = &hsotg->hw_params;

	print_param(seq, hw, op_mode);
	print_param(seq, hw, arch);
	print_param(seq, hw, dma_desc_enable);
	print_param(seq, hw, enable_dynamic_fifo);
	print_param(seq, hw, en_multiple_tx_fifo);
	print_param(seq, hw, rx_fifo_size);
	print_param(seq, hw, host_nperio_tx_fifo_size);
	print_param(seq, hw, dev_nperio_tx_fifo_size);
	print_param(seq, hw, host_perio_tx_fifo_size);
	print_param(seq, hw, nperio_tx_q_depth);
	print_param(seq, hw, host_perio_tx_q_depth);
	print_param(seq, hw, dev_token_q_depth);
	print_param(seq, hw, max_transfer_size);
	print_param(seq, hw, max_packet_count);
	print_param(seq, hw, host_channels);
	print_param(seq, hw, hs_phy_type);
	print_param(seq, hw, fs_phy_type);
	print_param(seq, hw, i2c_enable);
	print_param(seq, hw, num_dev_ep);
	print_param(seq, hw, num_dev_perio_in_ep);
	print_param(seq, hw, total_fifo_size);
	print_param(seq, hw, power_optimized);
	print_param(seq, hw, utmi_phy_data_width);
	print_param_hex(seq, hw, snpsid);
	print_param_hex(seq, hw, dev_ep_dirs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hw_params);

static int dr_mode_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *hsotg = seq->private;
	const char *dr_mode = "";

	device_property_read_string(hsotg->dev, "dr_mode", &dr_mode);
	seq_printf(seq, "%s\n", dr_mode);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dr_mode);

int dwc2_debugfs_init(struct dwc2_hsotg *hsotg)
{
	int			ret;
	struct dentry		*root;

	root = debugfs_create_dir(dev_name(hsotg->dev), NULL);
	hsotg->debug_root = root;

	debugfs_create_file("params", 0444, root, hsotg, &params_fops);
	debugfs_create_file("hw_params", 0444, root, hsotg, &hw_params_fops);
	debugfs_create_file("dr_mode", 0444, root, hsotg, &dr_mode_fops);

	/* Add gadget debugfs nodes */
	dwc2_hsotg_create_debug(hsotg);

	hsotg->regset = devm_kzalloc(hsotg->dev, sizeof(*hsotg->regset),
								GFP_KERNEL);
	if (!hsotg->regset) {
		ret = -ENOMEM;
		goto err;
	}

	hsotg->regset->regs = dwc2_regs;
	hsotg->regset->nregs = ARRAY_SIZE(dwc2_regs);
	hsotg->regset->base = hsotg->regs;

	debugfs_create_regset32("regdump", 0444, root, hsotg->regset);

	return 0;
err:
	debugfs_remove_recursive(hsotg->debug_root);
	return ret;
}

void dwc2_debugfs_exit(struct dwc2_hsotg *hsotg)
{
	debugfs_remove_recursive(hsotg->debug_root);
	hsotg->debug_root = NULL;
}
