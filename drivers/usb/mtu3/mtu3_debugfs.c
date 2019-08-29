// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_debugfs.c - debugfs interface
 *
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/uaccess.h>

#include "mtu3.h"
#include "mtu3_dr.h"
#include "mtu3_debug.h"

#define dump_register(nm)		\
{					\
	.name = __stringify(nm),	\
	.offset = U3D_ ##nm,		\
}

#define dump_prb_reg(nm, os)	\
{				\
	.name = nm,		\
	.offset = os,		\
}

static const struct debugfs_reg32 mtu3_ippc_regs[] = {
	dump_register(SSUSB_IP_PW_CTRL0),
	dump_register(SSUSB_IP_PW_CTRL1),
	dump_register(SSUSB_IP_PW_CTRL2),
	dump_register(SSUSB_IP_PW_CTRL3),
	dump_register(SSUSB_OTG_STS),
	dump_register(SSUSB_IP_XHCI_CAP),
	dump_register(SSUSB_IP_DEV_CAP),
	dump_register(SSUSB_U3_CTRL_0P),
	dump_register(SSUSB_U2_CTRL_0P),
	dump_register(SSUSB_HW_ID),
	dump_register(SSUSB_HW_SUB_ID),
	dump_register(SSUSB_IP_SPARE0),
};

static const struct debugfs_reg32 mtu3_dev_regs[] = {
	dump_register(LV1ISR),
	dump_register(LV1IER),
	dump_register(EPISR),
	dump_register(EPIER),
	dump_register(EP0CSR),
	dump_register(RXCOUNT0),
	dump_register(QISAR0),
	dump_register(QIER0),
	dump_register(QISAR1),
	dump_register(QIER1),
	dump_register(CAP_EPNTXFFSZ),
	dump_register(CAP_EPNRXFFSZ),
	dump_register(CAP_EPINFO),
	dump_register(MISC_CTRL),
};

static const struct debugfs_reg32 mtu3_csr_regs[] = {
	dump_register(DEVICE_CONF),
	dump_register(DEV_LINK_INTR_ENABLE),
	dump_register(DEV_LINK_INTR),
	dump_register(LTSSM_CTRL),
	dump_register(USB3_CONFIG),
	dump_register(LINK_STATE_MACHINE),
	dump_register(LTSSM_INTR_ENABLE),
	dump_register(LTSSM_INTR),
	dump_register(U3U2_SWITCH_CTRL),
	dump_register(POWER_MANAGEMENT),
	dump_register(DEVICE_CONTROL),
	dump_register(COMMON_USB_INTR_ENABLE),
	dump_register(COMMON_USB_INTR),
	dump_register(USB20_MISC_CONTROL),
	dump_register(USB20_OPSTATE),
};

static int mtu3_link_state_show(struct seq_file *sf, void *unused)
{
	struct mtu3 *mtu = sf->private;
	void __iomem *mbase = mtu->mac_base;

	seq_printf(sf, "opstate: %#x, ltssm: %#x\n",
		   mtu3_readl(mbase, U3D_USB20_OPSTATE),
		   LTSSM_STATE(mtu3_readl(mbase, U3D_LINK_STATE_MACHINE)));

	return 0;
}

static int mtu3_ep_used_show(struct seq_file *sf, void *unused)
{
	struct mtu3 *mtu = sf->private;
	struct mtu3_ep *mep;
	unsigned long flags;
	int used = 0;
	int i;

	spin_lock_irqsave(&mtu->lock, flags);

	for (i = 0; i < mtu->num_eps; i++) {
		mep = mtu->in_eps + i;
		if (mep->flags & MTU3_EP_ENABLED) {
			seq_printf(sf, "%s - type: %d\n", mep->name, mep->type);
			used++;
		}

		mep = mtu->out_eps + i;
		if (mep->flags & MTU3_EP_ENABLED) {
			seq_printf(sf, "%s - type: %d\n", mep->name, mep->type);
			used++;
		}
	}
	seq_printf(sf, "total used: %d eps\n", used);

	spin_unlock_irqrestore(&mtu->lock, flags);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mtu3_link_state);
DEFINE_SHOW_ATTRIBUTE(mtu3_ep_used);

static void mtu3_debugfs_regset(struct mtu3 *mtu, void __iomem *base,
				const struct debugfs_reg32 *regs, size_t nregs,
				const char *name, struct dentry *parent)
{
	struct debugfs_regset32 *regset;
	struct mtu3_regset *mregs;

	mregs = devm_kzalloc(mtu->dev, sizeof(*regset), GFP_KERNEL);
	if (!mregs)
		return;

	sprintf(mregs->name, "%s", name);
	regset = &mregs->regset;
	regset->regs = regs;
	regset->nregs = nregs;
	regset->base = base;

	debugfs_create_regset32(mregs->name, 0444, parent, regset);
}

static void mtu3_debugfs_ep_regset(struct mtu3 *mtu, struct mtu3_ep *mep,
				   struct dentry *parent)
{
	struct debugfs_reg32 *regs;
	int epnum = mep->epnum;
	int in = mep->is_in;

	regs = devm_kcalloc(mtu->dev, 7, sizeof(*regs), GFP_KERNEL);
	if (!regs)
		return;

	regs[0].name = in ? "TCR0" : "RCR0";
	regs[0].offset = in ? MU3D_EP_TXCR0(epnum) : MU3D_EP_RXCR0(epnum);
	regs[1].name = in ? "TCR1" : "RCR1";
	regs[1].offset = in ? MU3D_EP_TXCR1(epnum) : MU3D_EP_RXCR1(epnum);
	regs[2].name = in ? "TCR2" : "RCR2";
	regs[2].offset = in ? MU3D_EP_TXCR2(epnum) : MU3D_EP_RXCR2(epnum);
	regs[3].name = in ? "TQHIAR" : "RQHIAR";
	regs[3].offset = in ? USB_QMU_TQHIAR(epnum) : USB_QMU_RQHIAR(epnum);
	regs[4].name = in ? "TQCSR" : "RQCSR";
	regs[4].offset = in ? USB_QMU_TQCSR(epnum) : USB_QMU_RQCSR(epnum);
	regs[5].name = in ? "TQSAR" : "RQSAR";
	regs[5].offset = in ? USB_QMU_TQSAR(epnum) : USB_QMU_RQSAR(epnum);
	regs[6].name = in ? "TQCPR" : "RQCPR";
	regs[6].offset = in ? USB_QMU_TQCPR(epnum) : USB_QMU_RQCPR(epnum);

	mtu3_debugfs_regset(mtu, mtu->mac_base, regs, 7, "ep-regs", parent);
}

static int mtu3_ep_info_show(struct seq_file *sf, void *unused)
{
	struct mtu3_ep *mep = sf->private;
	struct mtu3 *mtu = mep->mtu;
	unsigned long flags;

	spin_lock_irqsave(&mtu->lock, flags);
	seq_printf(sf, "ep - type:%d, maxp:%d, slot:%d, flags:%x\n",
		   mep->type, mep->maxp, mep->slot, mep->flags);
	spin_unlock_irqrestore(&mtu->lock, flags);

	return 0;
}

static int mtu3_fifo_show(struct seq_file *sf, void *unused)
{
	struct mtu3_ep *mep = sf->private;
	struct mtu3 *mtu = mep->mtu;
	unsigned long flags;

	spin_lock_irqsave(&mtu->lock, flags);
	seq_printf(sf, "fifo - seg_size:%d, addr:%d, size:%d\n",
		   mep->fifo_seg_size, mep->fifo_addr, mep->fifo_size);
	spin_unlock_irqrestore(&mtu->lock, flags);

	return 0;
}

static int mtu3_qmu_ring_show(struct seq_file *sf, void *unused)
{
	struct mtu3_ep *mep = sf->private;
	struct mtu3 *mtu = mep->mtu;
	struct mtu3_gpd_ring *ring;
	unsigned long flags;

	ring = &mep->gpd_ring;
	spin_lock_irqsave(&mtu->lock, flags);
	seq_printf(sf,
		   "qmu-ring - dma:%pad, start:%p, end:%p, enq:%p, dep:%p\n",
		   &ring->dma, ring->start, ring->end,
		   ring->enqueue, ring->dequeue);
	spin_unlock_irqrestore(&mtu->lock, flags);

	return 0;
}

static int mtu3_qmu_gpd_show(struct seq_file *sf, void *unused)
{
	struct mtu3_ep *mep = sf->private;
	struct mtu3 *mtu = mep->mtu;
	struct mtu3_gpd_ring *ring;
	struct qmu_gpd *gpd;
	dma_addr_t dma;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mtu->lock, flags);
	ring = &mep->gpd_ring;
	gpd = ring->start;
	if (!gpd || !(mep->flags & MTU3_EP_ENABLED)) {
		seq_puts(sf, "empty!\n");
		goto out;
	}

	for (i = 0; i < MAX_GPD_NUM; i++, gpd++) {
		dma = ring->dma + i * sizeof(*gpd);
		seq_printf(sf, "gpd.%03d -> %pad, %p: %08x %08x %08x %08x\n",
			   i, &dma, gpd, gpd->dw0_info, gpd->next_gpd,
			   gpd->buffer, gpd->dw3_info);
	}

out:
	spin_unlock_irqrestore(&mtu->lock, flags);

	return 0;
}

static const struct mtu3_file_map mtu3_ep_files[] = {
	{"ep-info", mtu3_ep_info_show, },
	{"fifo", mtu3_fifo_show, },
	{"qmu-ring", mtu3_qmu_ring_show, },
	{"qmu-gpd", mtu3_qmu_gpd_show, },
};

static int mtu3_ep_open(struct inode *inode, struct file *file)
{
	const char *file_name = file_dentry(file)->d_iname;
	const struct mtu3_file_map *f_map;
	int i;

	for (i = 0; i < ARRAY_SIZE(mtu3_ep_files); i++) {
		f_map = &mtu3_ep_files[i];

		if (strcmp(f_map->name, file_name) == 0)
			break;
	}

	return single_open(file, f_map->show, inode->i_private);
}

static const struct file_operations mtu3_ep_fops = {
	.open = mtu3_ep_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct debugfs_reg32 mtu3_prb_regs[] = {
	dump_prb_reg("enable", U3D_SSUSB_PRB_CTRL0),
	dump_prb_reg("byte-sell", U3D_SSUSB_PRB_CTRL1),
	dump_prb_reg("byte-selh", U3D_SSUSB_PRB_CTRL2),
	dump_prb_reg("module-sel", U3D_SSUSB_PRB_CTRL3),
	dump_prb_reg("sw-out", U3D_SSUSB_PRB_CTRL4),
	dump_prb_reg("data", U3D_SSUSB_PRB_CTRL5),
};

static int mtu3_probe_show(struct seq_file *sf, void *unused)
{
	const char *file_name = file_dentry(sf->file)->d_iname;
	struct mtu3 *mtu = sf->private;
	const struct debugfs_reg32 *regs;
	int i;

	for (i = 0; i < ARRAY_SIZE(mtu3_prb_regs); i++) {
		regs = &mtu3_prb_regs[i];

		if (strcmp(regs->name, file_name) == 0)
			break;
	}

	seq_printf(sf, "0x%04x - 0x%08x\n", (u32)regs->offset,
		   mtu3_readl(mtu->ippc_base, (u32)regs->offset));

	return 0;
}

static int mtu3_probe_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtu3_probe_show, inode->i_private);
}

static ssize_t mtu3_probe_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	const char *file_name = file_dentry(file)->d_iname;
	struct seq_file *sf = file->private_data;
	struct mtu3 *mtu = sf->private;
	const struct debugfs_reg32 *regs;
	char buf[32];
	u32 val;
	int i;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(mtu3_prb_regs); i++) {
		regs = &mtu3_prb_regs[i];

		if (strcmp(regs->name, file_name) == 0)
			break;
	}
	mtu3_writel(mtu->ippc_base, (u32)regs->offset, val);

	return count;
}

static const struct file_operations mtu3_probe_fops = {
	.open = mtu3_probe_open,
	.write = mtu3_probe_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void mtu3_debugfs_create_prb_files(struct mtu3 *mtu)
{
	struct ssusb_mtk *ssusb = mtu->ssusb;
	struct debugfs_reg32 *regs;
	struct dentry *dir_prb;
	int i;

	dir_prb = debugfs_create_dir("probe", ssusb->dbgfs_root);

	for (i = 0; i < ARRAY_SIZE(mtu3_prb_regs); i++) {
		regs = &mtu3_prb_regs[i];
		debugfs_create_file(regs->name, 0644, dir_prb,
				    mtu, &mtu3_probe_fops);
	}

	mtu3_debugfs_regset(mtu, mtu->ippc_base, mtu3_prb_regs,
			    ARRAY_SIZE(mtu3_prb_regs), "regs", dir_prb);
}

static void mtu3_debugfs_create_ep_dir(struct mtu3_ep *mep,
				       struct dentry *parent)
{
	const struct mtu3_file_map *files;
	struct dentry *dir_ep;
	int i;

	dir_ep = debugfs_create_dir(mep->name, parent);
	mtu3_debugfs_ep_regset(mep->mtu, mep, dir_ep);

	for (i = 0; i < ARRAY_SIZE(mtu3_ep_files); i++) {
		files = &mtu3_ep_files[i];

		debugfs_create_file(files->name, 0444, dir_ep,
				    mep, &mtu3_ep_fops);
	}
}

static void mtu3_debugfs_create_ep_dirs(struct mtu3 *mtu)
{
	struct ssusb_mtk *ssusb = mtu->ssusb;
	struct dentry *dir_eps;
	int i;

	dir_eps = debugfs_create_dir("eps", ssusb->dbgfs_root);

	for (i = 1; i < mtu->num_eps; i++) {
		mtu3_debugfs_create_ep_dir(mtu->in_eps + i, dir_eps);
		mtu3_debugfs_create_ep_dir(mtu->out_eps + i, dir_eps);
	}
}

void ssusb_dev_debugfs_init(struct ssusb_mtk *ssusb)
{
	struct mtu3 *mtu = ssusb->u3d;
	struct dentry *dir_regs;

	dir_regs = debugfs_create_dir("regs", ssusb->dbgfs_root);

	mtu3_debugfs_regset(mtu, mtu->ippc_base,
			    mtu3_ippc_regs, ARRAY_SIZE(mtu3_ippc_regs),
			    "reg-ippc", dir_regs);

	mtu3_debugfs_regset(mtu, mtu->mac_base,
			    mtu3_dev_regs, ARRAY_SIZE(mtu3_dev_regs),
			    "reg-dev", dir_regs);

	mtu3_debugfs_regset(mtu, mtu->mac_base,
			    mtu3_csr_regs, ARRAY_SIZE(mtu3_csr_regs),
			    "reg-csr", dir_regs);

	mtu3_debugfs_create_ep_dirs(mtu);

	mtu3_debugfs_create_prb_files(mtu);

	debugfs_create_file("link-state", 0444, ssusb->dbgfs_root,
			    mtu, &mtu3_link_state_fops);
	debugfs_create_file("ep-used", 0444, ssusb->dbgfs_root,
			    mtu, &mtu3_ep_used_fops);
}

static int ssusb_mode_show(struct seq_file *sf, void *unused)
{
	struct ssusb_mtk *ssusb = sf->private;

	seq_printf(sf, "current mode: %s(%s drd)\n(echo device/host)\n",
		   ssusb->is_host ? "host" : "device",
		   ssusb->otg_switch.manual_drd_enabled ? "manual" : "auto");

	return 0;
}

static int ssusb_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, ssusb_mode_show, inode->i_private);
}

static ssize_t ssusb_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *sf = file->private_data;
	struct ssusb_mtk *ssusb = sf->private;
	char buf[16];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "host", 4) && !ssusb->is_host) {
		ssusb_mode_switch(ssusb, 1);
	} else if (!strncmp(buf, "device", 6) && ssusb->is_host) {
		ssusb_mode_switch(ssusb, 0);
	} else {
		dev_err(ssusb->dev, "wrong or duplicated setting\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations ssusb_mode_fops = {
	.open = ssusb_mode_open,
	.write = ssusb_mode_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ssusb_vbus_show(struct seq_file *sf, void *unused)
{
	struct ssusb_mtk *ssusb = sf->private;
	struct otg_switch_mtk *otg_sx = &ssusb->otg_switch;

	seq_printf(sf, "vbus state: %s\n(echo on/off)\n",
		   regulator_is_enabled(otg_sx->vbus) ? "on" : "off");

	return 0;
}

static int ssusb_vbus_open(struct inode *inode, struct file *file)
{
	return single_open(file, ssusb_vbus_show, inode->i_private);
}

static ssize_t ssusb_vbus_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *sf = file->private_data;
	struct ssusb_mtk *ssusb = sf->private;
	struct otg_switch_mtk *otg_sx = &ssusb->otg_switch;
	char buf[16];
	bool enable;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (kstrtobool(buf, &enable)) {
		dev_err(ssusb->dev, "wrong setting\n");
		return -EINVAL;
	}

	ssusb_set_vbus(otg_sx, enable);

	return count;
}

static const struct file_operations ssusb_vbus_fops = {
	.open = ssusb_vbus_open,
	.write = ssusb_vbus_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void ssusb_dr_debugfs_init(struct ssusb_mtk *ssusb)
{
	struct dentry *root = ssusb->dbgfs_root;

	debugfs_create_file("mode", 0644, root, ssusb, &ssusb_mode_fops);
	debugfs_create_file("vbus", 0644, root, ssusb, &ssusb_vbus_fops);
}

void ssusb_debugfs_create_root(struct ssusb_mtk *ssusb)
{
	ssusb->dbgfs_root =
		debugfs_create_dir(dev_name(ssusb->dev), usb_debug_root);
}

void ssusb_debugfs_remove_root(struct ssusb_mtk *ssusb)
{
	debugfs_remove_recursive(ssusb->dbgfs_root);
	ssusb->dbgfs_root = NULL;
}
