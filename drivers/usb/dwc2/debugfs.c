/**
 * debugfs.c - Designware USB2 DRD controller debugfs
 *
 * Copyright (C) 2015 Intel Corporation
 * Mian Yousaf Kaukab <yousaf.kaukab@intel.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
 * testmode_write - debugfs: change usb test mode
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry modify the current usb test mode.
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
	s3c_hsotg_set_test_mode(hsotg, testmode);
	spin_unlock_irqrestore(&hsotg->lock, flags);
	return count;
}

/**
 * testmode_show - debugfs: show usb test mode state
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows which usb test mode is currently enabled.
 */
static int testmode_show(struct seq_file *s, void *unused)
{
	struct dwc2_hsotg *hsotg = s->private;
	unsigned long flags;
	int dctl;

	spin_lock_irqsave(&hsotg->lock, flags);
	dctl = readl(hsotg->regs + DCTL);
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
	void __iomem *regs = hsotg->regs;
	int idx;

	seq_printf(seq, "DCFG=0x%08x, DCTL=0x%08x, DSTS=0x%08x\n",
		 readl(regs + DCFG),
		 readl(regs + DCTL),
		 readl(regs + DSTS));

	seq_printf(seq, "DIEPMSK=0x%08x, DOEPMASK=0x%08x\n",
		   readl(regs + DIEPMSK), readl(regs + DOEPMSK));

	seq_printf(seq, "GINTMSK=0x%08x, GINTSTS=0x%08x\n",
		   readl(regs + GINTMSK),
		   readl(regs + GINTSTS));

	seq_printf(seq, "DAINTMSK=0x%08x, DAINT=0x%08x\n",
		   readl(regs + DAINTMSK),
		   readl(regs + DAINT));

	seq_printf(seq, "GNPTXSTS=0x%08x, GRXSTSR=%08x\n",
		   readl(regs + GNPTXSTS),
		   readl(regs + GRXSTSR));

	seq_puts(seq, "\nEndpoint status:\n");

	for (idx = 0; idx < hsotg->num_of_eps; idx++) {
		u32 in, out;

		in = readl(regs + DIEPCTL(idx));
		out = readl(regs + DOEPCTL(idx));

		seq_printf(seq, "ep%d: DIEPCTL=0x%08x, DOEPCTL=0x%08x",
			   idx, in, out);

		in = readl(regs + DIEPTSIZ(idx));
		out = readl(regs + DOEPTSIZ(idx));

		seq_printf(seq, ", DIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x",
			   in, out);

		seq_puts(seq, "\n");
	}

	return 0;
}

static int state_open(struct inode *inode, struct file *file)
{
	return single_open(file, state_show, inode->i_private);
}

static const struct file_operations state_fops = {
	.owner		= THIS_MODULE,
	.open		= state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	seq_puts(seq, "Non-periodic FIFOs:\n");
	seq_printf(seq, "RXFIFO: Size %d\n", readl(regs + GRXFSIZ));

	val = readl(regs + GNPTXFSIZ);
	seq_printf(seq, "NPTXFIFO: Size %d, Start 0x%08x\n",
		   val >> FIFOSIZE_DEPTH_SHIFT,
		   val & FIFOSIZE_DEPTH_MASK);

	seq_puts(seq, "\nPeriodic TXFIFOs:\n");

	for (idx = 1; idx < hsotg->num_of_eps; idx++) {
		val = readl(regs + DPTXFSIZN(idx));

		seq_printf(seq, "\tDPTXFIFO%2d: Size %d, Start 0x%08x\n", idx,
			   val >> FIFOSIZE_DEPTH_SHIFT,
			   val & FIFOSIZE_STARTADDR_MASK);
	}

	return 0;
}

static int fifo_open(struct inode *inode, struct file *file)
{
	return single_open(file, fifo_show, inode->i_private);
}

static const struct file_operations fifo_fops = {
	.owner		= THIS_MODULE,
	.open		= fifo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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
	struct s3c_hsotg_ep *ep = seq->private;
	struct dwc2_hsotg *hsotg = ep->parent;
	struct s3c_hsotg_req *req;
	void __iomem *regs = hsotg->regs;
	int index = ep->index;
	int show_limit = 15;
	unsigned long flags;

	seq_printf(seq, "Endpoint index %d, named %s,  dir %s:\n",
		   ep->index, ep->ep.name, decode_direction(ep->dir_in));

	/* first show the register state */

	seq_printf(seq, "\tDIEPCTL=0x%08x, DOEPCTL=0x%08x\n",
		   readl(regs + DIEPCTL(index)),
		   readl(regs + DOEPCTL(index)));

	seq_printf(seq, "\tDIEPDMA=0x%08x, DOEPDMA=0x%08x\n",
		   readl(regs + DIEPDMA(index)),
		   readl(regs + DOEPDMA(index)));

	seq_printf(seq, "\tDIEPINT=0x%08x, DOEPINT=0x%08x\n",
		   readl(regs + DIEPINT(index)),
		   readl(regs + DOEPINT(index)));

	seq_printf(seq, "\tDIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x\n",
		   readl(regs + DIEPTSIZ(index)),
		   readl(regs + DOEPTSIZ(index)));

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

static int ep_open(struct inode *inode, struct file *file)
{
	return single_open(file, ep_show, inode->i_private);
}

static const struct file_operations ep_fops = {
	.owner		= THIS_MODULE,
	.open		= ep_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * s3c_hsotg_create_debug - create debugfs directory and files
 * @hsotg: The driver state
 *
 * Create the debugfs files to allow the user to get information
 * about the state of the system. The directory name is created
 * with the same name as the device itself, in case we end up
 * with multiple blocks in future systems.
 */
static void s3c_hsotg_create_debug(struct dwc2_hsotg *hsotg)
{
	struct dentry *root;
	struct dentry *file;
	unsigned epidx;

	root = hsotg->debug_root;

	/* create general state file */

	file = debugfs_create_file("state", S_IRUGO, root, hsotg, &state_fops);
	if (IS_ERR(file))
		dev_err(hsotg->dev, "%s: failed to create state\n", __func__);

	file = debugfs_create_file("testmode", S_IRUGO | S_IWUSR, root, hsotg,
							&testmode_fops);
	if (IS_ERR(file))
		dev_err(hsotg->dev, "%s: failed to create testmode\n",
				__func__);

	file = debugfs_create_file("fifo", S_IRUGO, root, hsotg, &fifo_fops);
	if (IS_ERR(file))
		dev_err(hsotg->dev, "%s: failed to create fifo\n", __func__);

	/* Create one file for each out endpoint */
	for (epidx = 0; epidx < hsotg->num_of_eps; epidx++) {
		struct s3c_hsotg_ep *ep;

		ep = hsotg->eps_out[epidx];
		if (ep) {
			file = debugfs_create_file(ep->name, S_IRUGO,
							  root, ep, &ep_fops);
			if (IS_ERR(file))
				dev_err(hsotg->dev, "failed to create %s debug file\n",
					ep->name);
		}
	}
	/* Create one file for each in endpoint. EP0 is handled with out eps */
	for (epidx = 1; epidx < hsotg->num_of_eps; epidx++) {
		struct s3c_hsotg_ep *ep;

		ep = hsotg->eps_in[epidx];
		if (ep) {
			file = debugfs_create_file(ep->name, S_IRUGO,
							  root, ep, &ep_fops);
			if (IS_ERR(file))
				dev_err(hsotg->dev, "failed to create %s debug file\n",
					ep->name);
		}
	}
}
#else
static inline void s3c_hsotg_create_debug(struct dwc2_hsotg *hsotg) {}
#endif

/* s3c_hsotg_delete_debug is removed as cleanup in done in dwc2_debugfs_exit */

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
	dump_register(GRXSTSP),
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

int dwc2_debugfs_init(struct dwc2_hsotg *hsotg)
{
	int			ret;
	struct dentry		*file;

	hsotg->debug_root = debugfs_create_dir(dev_name(hsotg->dev), NULL);
	if (!hsotg->debug_root) {
		ret = -ENOMEM;
		goto err0;
	}

	/* Add gadget debugfs nodes */
	s3c_hsotg_create_debug(hsotg);

	hsotg->regset = devm_kzalloc(hsotg->dev, sizeof(*hsotg->regset),
								GFP_KERNEL);
	if (!hsotg->regset) {
		ret = -ENOMEM;
		goto err1;
	}

	hsotg->regset->regs = dwc2_regs;
	hsotg->regset->nregs = ARRAY_SIZE(dwc2_regs);
	hsotg->regset->base = hsotg->regs;

	file = debugfs_create_regset32("regdump", S_IRUGO, hsotg->debug_root,
								hsotg->regset);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}

	return 0;
err1:
	debugfs_remove_recursive(hsotg->debug_root);
err0:
	return ret;
}
EXPORT_SYMBOL_GPL(dwc2_debugfs_init);

void dwc2_debugfs_exit(struct dwc2_hsotg *hsotg)
{
	debugfs_remove_recursive(hsotg->debug_root);
	hsotg->debug_root = NULL;
}
EXPORT_SYMBOL_GPL(dwc2_debugfs_exit);
