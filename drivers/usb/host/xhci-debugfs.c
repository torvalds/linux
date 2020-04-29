// SPDX-License-Identifier: GPL-2.0
/*
 * xhci-debugfs.c - xHCI debugfs interface
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#include <linux/slab.h>
#include <linux/uaccess.h>

#include "xhci.h"
#include "xhci-debugfs.h"

static const struct debugfs_reg32 xhci_cap_regs[] = {
	dump_register(CAPLENGTH),
	dump_register(HCSPARAMS1),
	dump_register(HCSPARAMS2),
	dump_register(HCSPARAMS3),
	dump_register(HCCPARAMS1),
	dump_register(DOORBELLOFF),
	dump_register(RUNTIMEOFF),
	dump_register(HCCPARAMS2),
};

static const struct debugfs_reg32 xhci_op_regs[] = {
	dump_register(USBCMD),
	dump_register(USBSTS),
	dump_register(PAGESIZE),
	dump_register(DNCTRL),
	dump_register(CRCR),
	dump_register(DCBAAP_LOW),
	dump_register(DCBAAP_HIGH),
	dump_register(CONFIG),
};

static const struct debugfs_reg32 xhci_runtime_regs[] = {
	dump_register(MFINDEX),
	dump_register(IR0_IMAN),
	dump_register(IR0_IMOD),
	dump_register(IR0_ERSTSZ),
	dump_register(IR0_ERSTBA_LOW),
	dump_register(IR0_ERSTBA_HIGH),
	dump_register(IR0_ERDP_LOW),
	dump_register(IR0_ERDP_HIGH),
};

static const struct debugfs_reg32 xhci_extcap_legsup[] = {
	dump_register(EXTCAP_USBLEGSUP),
	dump_register(EXTCAP_USBLEGCTLSTS),
};

static const struct debugfs_reg32 xhci_extcap_protocol[] = {
	dump_register(EXTCAP_REVISION),
	dump_register(EXTCAP_NAME),
	dump_register(EXTCAP_PORTINFO),
	dump_register(EXTCAP_PORTTYPE),
	dump_register(EXTCAP_MANTISSA1),
	dump_register(EXTCAP_MANTISSA2),
	dump_register(EXTCAP_MANTISSA3),
	dump_register(EXTCAP_MANTISSA4),
	dump_register(EXTCAP_MANTISSA5),
	dump_register(EXTCAP_MANTISSA6),
};

static const struct debugfs_reg32 xhci_extcap_dbc[] = {
	dump_register(EXTCAP_DBC_CAPABILITY),
	dump_register(EXTCAP_DBC_DOORBELL),
	dump_register(EXTCAP_DBC_ERSTSIZE),
	dump_register(EXTCAP_DBC_ERST_LOW),
	dump_register(EXTCAP_DBC_ERST_HIGH),
	dump_register(EXTCAP_DBC_ERDP_LOW),
	dump_register(EXTCAP_DBC_ERDP_HIGH),
	dump_register(EXTCAP_DBC_CONTROL),
	dump_register(EXTCAP_DBC_STATUS),
	dump_register(EXTCAP_DBC_PORTSC),
	dump_register(EXTCAP_DBC_CONT_LOW),
	dump_register(EXTCAP_DBC_CONT_HIGH),
	dump_register(EXTCAP_DBC_DEVINFO1),
	dump_register(EXTCAP_DBC_DEVINFO2),
};

static struct dentry *xhci_debugfs_root;

static struct xhci_regset *xhci_debugfs_alloc_regset(struct xhci_hcd *xhci)
{
	struct xhci_regset	*regset;

	regset = kzalloc(sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return NULL;

	/*
	 * The allocation and free of regset are executed in order.
	 * We needn't a lock here.
	 */
	INIT_LIST_HEAD(&regset->list);
	list_add_tail(&regset->list, &xhci->regset_list);

	return regset;
}

static void xhci_debugfs_free_regset(struct xhci_regset *regset)
{
	if (!regset)
		return;

	list_del(&regset->list);
	kfree(regset);
}

static void xhci_debugfs_regset(struct xhci_hcd *xhci, u32 base,
				const struct debugfs_reg32 *regs,
				size_t nregs, struct dentry *parent,
				const char *fmt, ...)
{
	struct xhci_regset	*rgs;
	va_list			args;
	struct debugfs_regset32	*regset;
	struct usb_hcd		*hcd = xhci_to_hcd(xhci);

	rgs = xhci_debugfs_alloc_regset(xhci);
	if (!rgs)
		return;

	va_start(args, fmt);
	vsnprintf(rgs->name, sizeof(rgs->name), fmt, args);
	va_end(args);

	regset = &rgs->regset;
	regset->regs = regs;
	regset->nregs = nregs;
	regset->base = hcd->regs + base;

	debugfs_create_regset32((const char *)rgs->name, 0444, parent, regset);
}

static void xhci_debugfs_extcap_regset(struct xhci_hcd *xhci, int cap_id,
				       const struct debugfs_reg32 *regs,
				       size_t n, const char *cap_name)
{
	u32			offset;
	int			index = 0;
	size_t			psic, nregs = n;
	void __iomem		*base = &xhci->cap_regs->hc_capbase;

	offset = xhci_find_next_ext_cap(base, 0, cap_id);
	while (offset) {
		if (cap_id == XHCI_EXT_CAPS_PROTOCOL) {
			psic = XHCI_EXT_PORT_PSIC(readl(base + offset + 8));
			nregs = min(4 + psic, n);
		}

		xhci_debugfs_regset(xhci, offset, regs, nregs,
				    xhci->debugfs_root, "%s:%02d",
				    cap_name, index);
		offset = xhci_find_next_ext_cap(base, offset, cap_id);
		index++;
	}
}

static int xhci_ring_enqueue_show(struct seq_file *s, void *unused)
{
	dma_addr_t		dma;
	struct xhci_ring	*ring = *(struct xhci_ring **)s->private;

	dma = xhci_trb_virt_to_dma(ring->enq_seg, ring->enqueue);
	seq_printf(s, "%pad\n", &dma);

	return 0;
}

static int xhci_ring_dequeue_show(struct seq_file *s, void *unused)
{
	dma_addr_t		dma;
	struct xhci_ring	*ring = *(struct xhci_ring **)s->private;

	dma = xhci_trb_virt_to_dma(ring->deq_seg, ring->dequeue);
	seq_printf(s, "%pad\n", &dma);

	return 0;
}

static int xhci_ring_cycle_show(struct seq_file *s, void *unused)
{
	struct xhci_ring	*ring = *(struct xhci_ring **)s->private;

	seq_printf(s, "%d\n", ring->cycle_state);

	return 0;
}

static void xhci_ring_dump_segment(struct seq_file *s,
				   struct xhci_segment *seg)
{
	int			i;
	dma_addr_t		dma;
	union xhci_trb		*trb;

	for (i = 0; i < TRBS_PER_SEGMENT; i++) {
		trb = &seg->trbs[i];
		dma = seg->dma + i * sizeof(*trb);
		seq_printf(s, "%pad: %s\n", &dma,
			   xhci_decode_trb(le32_to_cpu(trb->generic.field[0]),
					   le32_to_cpu(trb->generic.field[1]),
					   le32_to_cpu(trb->generic.field[2]),
					   le32_to_cpu(trb->generic.field[3])));
	}
}

static int xhci_ring_trb_show(struct seq_file *s, void *unused)
{
	int			i;
	struct xhci_ring	*ring = *(struct xhci_ring **)s->private;
	struct xhci_segment	*seg = ring->first_seg;

	for (i = 0; i < ring->num_segs; i++) {
		xhci_ring_dump_segment(s, seg);
		seg = seg->next;
	}

	return 0;
}

static struct xhci_file_map ring_files[] = {
	{"enqueue",		xhci_ring_enqueue_show, },
	{"dequeue",		xhci_ring_dequeue_show, },
	{"cycle",		xhci_ring_cycle_show, },
	{"trbs",		xhci_ring_trb_show, },
};

static int xhci_ring_open(struct inode *inode, struct file *file)
{
	int			i;
	struct xhci_file_map	*f_map;
	const char		*file_name = file_dentry(file)->d_iname;

	for (i = 0; i < ARRAY_SIZE(ring_files); i++) {
		f_map = &ring_files[i];

		if (strcmp(f_map->name, file_name) == 0)
			break;
	}

	return single_open(file, f_map->show, inode->i_private);
}

static const struct file_operations xhci_ring_fops = {
	.open			= xhci_ring_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int xhci_slot_context_show(struct seq_file *s, void *unused)
{
	struct xhci_hcd		*xhci;
	struct xhci_slot_ctx	*slot_ctx;
	struct xhci_slot_priv	*priv = s->private;
	struct xhci_virt_device	*dev = priv->dev;

	xhci = hcd_to_xhci(bus_to_hcd(dev->udev->bus));
	slot_ctx = xhci_get_slot_ctx(xhci, dev->out_ctx);
	seq_printf(s, "%pad: %s\n", &dev->out_ctx->dma,
		   xhci_decode_slot_context(le32_to_cpu(slot_ctx->dev_info),
					    le32_to_cpu(slot_ctx->dev_info2),
					    le32_to_cpu(slot_ctx->tt_info),
					    le32_to_cpu(slot_ctx->dev_state)));

	return 0;
}

static int xhci_endpoint_context_show(struct seq_file *s, void *unused)
{
	int			dci;
	dma_addr_t		dma;
	struct xhci_hcd		*xhci;
	struct xhci_ep_ctx	*ep_ctx;
	struct xhci_slot_priv	*priv = s->private;
	struct xhci_virt_device	*dev = priv->dev;

	xhci = hcd_to_xhci(bus_to_hcd(dev->udev->bus));

	for (dci = 1; dci < 32; dci++) {
		ep_ctx = xhci_get_ep_ctx(xhci, dev->out_ctx, dci);
		dma = dev->out_ctx->dma + dci * CTX_SIZE(xhci->hcc_params);
		seq_printf(s, "%pad: %s\n", &dma,
			   xhci_decode_ep_context(le32_to_cpu(ep_ctx->ep_info),
						  le32_to_cpu(ep_ctx->ep_info2),
						  le64_to_cpu(ep_ctx->deq),
						  le32_to_cpu(ep_ctx->tx_info)));
	}

	return 0;
}

static int xhci_device_name_show(struct seq_file *s, void *unused)
{
	struct xhci_slot_priv	*priv = s->private;
	struct xhci_virt_device	*dev = priv->dev;

	seq_printf(s, "%s\n", dev_name(&dev->udev->dev));

	return 0;
}

static struct xhci_file_map context_files[] = {
	{"name",		xhci_device_name_show, },
	{"slot-context",	xhci_slot_context_show, },
	{"ep-context",		xhci_endpoint_context_show, },
};

static int xhci_context_open(struct inode *inode, struct file *file)
{
	int			i;
	struct xhci_file_map	*f_map;
	const char		*file_name = file_dentry(file)->d_iname;

	for (i = 0; i < ARRAY_SIZE(context_files); i++) {
		f_map = &context_files[i];

		if (strcmp(f_map->name, file_name) == 0)
			break;
	}

	return single_open(file, f_map->show, inode->i_private);
}

static const struct file_operations xhci_context_fops = {
	.open			= xhci_context_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};



static int xhci_portsc_show(struct seq_file *s, void *unused)
{
	struct xhci_port	*port = s->private;
	u32			portsc;

	portsc = readl(port->addr);
	seq_printf(s, "%s\n", xhci_decode_portsc(portsc));

	return 0;
}

static int xhci_port_open(struct inode *inode, struct file *file)
{
	return single_open(file, xhci_portsc_show, inode->i_private);
}

static ssize_t xhci_port_write(struct file *file,  const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct seq_file         *s = file->private_data;
	struct xhci_port	*port = s->private;
	struct xhci_hcd		*xhci = hcd_to_xhci(port->rhub->hcd);
	char                    buf[32];
	u32			portsc;
	unsigned long		flags;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "compliance", 10)) {
		/* If CTC is clear, compliance is enabled by default */
		if (!HCC2_CTC(xhci->hcc_params2))
			return count;
		spin_lock_irqsave(&xhci->lock, flags);
		/* compliance mode can only be enabled on ports in RxDetect */
		portsc = readl(port->addr);
		if ((portsc & PORT_PLS_MASK) != XDEV_RXDETECT) {
			spin_unlock_irqrestore(&xhci->lock, flags);
			return -EPERM;
		}
		portsc = xhci_port_state_to_neutral(portsc);
		portsc &= ~PORT_PLS_MASK;
		portsc |= PORT_LINK_STROBE | XDEV_COMP_MODE;
		writel(portsc, port->addr);
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else {
		return -EINVAL;
	}
	return count;
}

static const struct file_operations port_fops = {
	.open			= xhci_port_open,
	.write                  = xhci_port_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static void xhci_debugfs_create_files(struct xhci_hcd *xhci,
				      struct xhci_file_map *files,
				      size_t nentries, void *data,
				      struct dentry *parent,
				      const struct file_operations *fops)
{
	int			i;

	for (i = 0; i < nentries; i++)
		debugfs_create_file(files[i].name, 0444, parent, data, fops);
}

static struct dentry *xhci_debugfs_create_ring_dir(struct xhci_hcd *xhci,
						   struct xhci_ring **ring,
						   const char *name,
						   struct dentry *parent)
{
	struct dentry		*dir;

	dir = debugfs_create_dir(name, parent);
	xhci_debugfs_create_files(xhci, ring_files, ARRAY_SIZE(ring_files),
				  ring, dir, &xhci_ring_fops);

	return dir;
}

static void xhci_debugfs_create_context_files(struct xhci_hcd *xhci,
					      struct dentry *parent,
					      int slot_id)
{
	struct xhci_virt_device	*dev = xhci->devs[slot_id];

	xhci_debugfs_create_files(xhci, context_files,
				  ARRAY_SIZE(context_files),
				  dev->debugfs_private,
				  parent, &xhci_context_fops);
}

void xhci_debugfs_create_endpoint(struct xhci_hcd *xhci,
				  struct xhci_virt_device *dev,
				  int ep_index)
{
	struct xhci_ep_priv	*epriv;
	struct xhci_slot_priv	*spriv = dev->debugfs_private;

	if (!spriv)
		return;

	if (spriv->eps[ep_index])
		return;

	epriv = kzalloc(sizeof(*epriv), GFP_KERNEL);
	if (!epriv)
		return;

	snprintf(epriv->name, sizeof(epriv->name), "ep%02d", ep_index);
	epriv->root = xhci_debugfs_create_ring_dir(xhci,
						   &dev->eps[ep_index].ring,
						   epriv->name,
						   spriv->root);
	spriv->eps[ep_index] = epriv;
}

void xhci_debugfs_remove_endpoint(struct xhci_hcd *xhci,
				  struct xhci_virt_device *dev,
				  int ep_index)
{
	struct xhci_ep_priv	*epriv;
	struct xhci_slot_priv	*spriv = dev->debugfs_private;

	if (!spriv || !spriv->eps[ep_index])
		return;

	epriv = spriv->eps[ep_index];
	debugfs_remove_recursive(epriv->root);
	spriv->eps[ep_index] = NULL;
	kfree(epriv);
}

void xhci_debugfs_create_slot(struct xhci_hcd *xhci, int slot_id)
{
	struct xhci_slot_priv	*priv;
	struct xhci_virt_device	*dev = xhci->devs[slot_id];

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return;

	snprintf(priv->name, sizeof(priv->name), "%02d", slot_id);
	priv->root = debugfs_create_dir(priv->name, xhci->debugfs_slots);
	priv->dev = dev;
	dev->debugfs_private = priv;

	xhci_debugfs_create_ring_dir(xhci, &dev->eps[0].ring,
				     "ep00", priv->root);

	xhci_debugfs_create_context_files(xhci, priv->root, slot_id);
}

void xhci_debugfs_remove_slot(struct xhci_hcd *xhci, int slot_id)
{
	int			i;
	struct xhci_slot_priv	*priv;
	struct xhci_virt_device	*dev = xhci->devs[slot_id];

	if (!dev || !dev->debugfs_private)
		return;

	priv = dev->debugfs_private;

	debugfs_remove_recursive(priv->root);

	for (i = 0; i < 31; i++)
		kfree(priv->eps[i]);

	kfree(priv);
	dev->debugfs_private = NULL;
}

static void xhci_debugfs_create_ports(struct xhci_hcd *xhci,
				      struct dentry *parent)
{
	unsigned int		num_ports;
	char			port_name[8];
	struct xhci_port	*port;
	struct dentry		*dir;

	num_ports = HCS_MAX_PORTS(xhci->hcs_params1);

	parent = debugfs_create_dir("ports", parent);

	while (num_ports--) {
		scnprintf(port_name, sizeof(port_name), "port%02d",
			  num_ports + 1);
		dir = debugfs_create_dir(port_name, parent);
		port = &xhci->hw_ports[num_ports];
		debugfs_create_file("portsc", 0644, dir, port, &port_fops);
	}
}

void xhci_debugfs_init(struct xhci_hcd *xhci)
{
	struct device		*dev = xhci_to_hcd(xhci)->self.controller;

	xhci->debugfs_root = debugfs_create_dir(dev_name(dev),
						xhci_debugfs_root);

	INIT_LIST_HEAD(&xhci->regset_list);

	xhci_debugfs_regset(xhci,
			    0,
			    xhci_cap_regs, ARRAY_SIZE(xhci_cap_regs),
			    xhci->debugfs_root, "reg-cap");

	xhci_debugfs_regset(xhci,
			    HC_LENGTH(readl(&xhci->cap_regs->hc_capbase)),
			    xhci_op_regs, ARRAY_SIZE(xhci_op_regs),
			    xhci->debugfs_root, "reg-op");

	xhci_debugfs_regset(xhci,
			    readl(&xhci->cap_regs->run_regs_off) & RTSOFF_MASK,
			    xhci_runtime_regs, ARRAY_SIZE(xhci_runtime_regs),
			    xhci->debugfs_root, "reg-runtime");

	xhci_debugfs_extcap_regset(xhci, XHCI_EXT_CAPS_LEGACY,
				   xhci_extcap_legsup,
				   ARRAY_SIZE(xhci_extcap_legsup),
				   "reg-ext-legsup");

	xhci_debugfs_extcap_regset(xhci, XHCI_EXT_CAPS_PROTOCOL,
				   xhci_extcap_protocol,
				   ARRAY_SIZE(xhci_extcap_protocol),
				   "reg-ext-protocol");

	xhci_debugfs_extcap_regset(xhci, XHCI_EXT_CAPS_DEBUG,
				   xhci_extcap_dbc,
				   ARRAY_SIZE(xhci_extcap_dbc),
				   "reg-ext-dbc");

	xhci_debugfs_create_ring_dir(xhci, &xhci->cmd_ring,
				     "command-ring",
				     xhci->debugfs_root);

	xhci_debugfs_create_ring_dir(xhci, &xhci->event_ring,
				     "event-ring",
				     xhci->debugfs_root);

	xhci->debugfs_slots = debugfs_create_dir("devices", xhci->debugfs_root);

	xhci_debugfs_create_ports(xhci, xhci->debugfs_root);
}

void xhci_debugfs_exit(struct xhci_hcd *xhci)
{
	struct xhci_regset	*rgs, *tmp;

	debugfs_remove_recursive(xhci->debugfs_root);
	xhci->debugfs_root = NULL;
	xhci->debugfs_slots = NULL;

	list_for_each_entry_safe(rgs, tmp, &xhci->regset_list, list)
		xhci_debugfs_free_regset(rgs);
}

void __init xhci_debugfs_create_root(void)
{
	xhci_debugfs_root = debugfs_create_dir("xhci", usb_debug_root);
}

void __exit xhci_debugfs_remove_root(void)
{
	debugfs_remove_recursive(xhci_debugfs_root);
	xhci_debugfs_root = NULL;
}
