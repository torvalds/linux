// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include "internal.h"

static int mhi_debugfs_states_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;

	/* states */
	seq_printf(m, "PM state: %s Device: %s MHI state: %s EE: %s wake: %s\n",
		   to_mhi_pm_state_str(mhi_cntrl->pm_state),
		   mhi_is_active(mhi_cntrl) ? "Active" : "Inactive",
		   TO_MHI_STATE_STR(mhi_cntrl->dev_state),
		   TO_MHI_EXEC_STR(mhi_cntrl->ee),
		   mhi_cntrl->wake_set ? "true" : "false");

	/* counters */
	seq_printf(m, "M0: %u M2: %u M3: %u", mhi_cntrl->M0, mhi_cntrl->M2,
		   mhi_cntrl->M3);

	seq_printf(m, " device wake: %u pending packets: %u\n",
		   atomic_read(&mhi_cntrl->dev_wake),
		   atomic_read(&mhi_cntrl->pending_pkts));

	return 0;
}

static int mhi_debugfs_events_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;
	struct mhi_event *mhi_event;
	struct mhi_event_ctxt *er_ctxt;
	int i;

	if (!mhi_is_active(mhi_cntrl)) {
		seq_puts(m, "Device not ready\n");
		return -ENODEV;
	}

	er_ctxt = mhi_cntrl->mhi_ctxt->er_ctxt;
	mhi_event = mhi_cntrl->mhi_event;
	for (i = 0; i < mhi_cntrl->total_ev_rings;
						i++, er_ctxt++, mhi_event++) {
		struct mhi_ring *ring = &mhi_event->ring;

		if (mhi_event->offload_ev) {
			seq_printf(m, "Index: %d is an offload event ring\n",
				   i);
			continue;
		}

		seq_printf(m, "Index: %d intmod count: %lu time: %lu",
			   i, (er_ctxt->intmod & EV_CTX_INTMODC_MASK) >>
			   EV_CTX_INTMODC_SHIFT,
			   (er_ctxt->intmod & EV_CTX_INTMODT_MASK) >>
			   EV_CTX_INTMODT_SHIFT);

		seq_printf(m, " base: 0x%0llx len: 0x%llx", er_ctxt->rbase,
			   er_ctxt->rlen);

		seq_printf(m, " rp: 0x%llx wp: 0x%llx", er_ctxt->rp,
			   er_ctxt->wp);

		seq_printf(m, " local rp: 0x%pK db: 0x%pad\n", ring->rp,
			   &mhi_event->db_cfg.db_val);
	}

	return 0;
}

static int mhi_debugfs_channels_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;
	struct mhi_chan *mhi_chan;
	struct mhi_chan_ctxt *chan_ctxt;
	int i;

	if (!mhi_is_active(mhi_cntrl)) {
		seq_puts(m, "Device not ready\n");
		return -ENODEV;
	}

	mhi_chan = mhi_cntrl->mhi_chan;
	chan_ctxt = mhi_cntrl->mhi_ctxt->chan_ctxt;
	for (i = 0; i < mhi_cntrl->max_chan; i++, chan_ctxt++, mhi_chan++) {
		struct mhi_ring *ring = &mhi_chan->tre_ring;

		if (mhi_chan->offload_ch) {
			seq_printf(m, "%s(%u) is an offload channel\n",
				   mhi_chan->name, mhi_chan->chan);
			continue;
		}

		if (!mhi_chan->mhi_dev)
			continue;

		seq_printf(m,
			   "%s(%u) state: 0x%lx brstmode: 0x%lx pollcfg: 0x%lx",
			   mhi_chan->name, mhi_chan->chan, (chan_ctxt->chcfg &
			   CHAN_CTX_CHSTATE_MASK) >> CHAN_CTX_CHSTATE_SHIFT,
			   (chan_ctxt->chcfg & CHAN_CTX_BRSTMODE_MASK) >>
			   CHAN_CTX_BRSTMODE_SHIFT, (chan_ctxt->chcfg &
			   CHAN_CTX_POLLCFG_MASK) >> CHAN_CTX_POLLCFG_SHIFT);

		seq_printf(m, " type: 0x%x event ring: %u", chan_ctxt->chtype,
			   chan_ctxt->erindex);

		seq_printf(m, " base: 0x%llx len: 0x%llx rp: 0x%llx wp: 0x%llx",
			   chan_ctxt->rbase, chan_ctxt->rlen, chan_ctxt->rp,
			   chan_ctxt->wp);

		seq_printf(m, " local rp: 0x%pK local wp: 0x%pK db: 0x%pad\n",
			   ring->rp, ring->wp,
			   &mhi_chan->db_cfg.db_val);
	}

	return 0;
}

static int mhi_device_info_show(struct device *dev, void *data)
{
	struct mhi_device *mhi_dev;

	if (dev->bus != &mhi_bus_type)
		return 0;

	mhi_dev = to_mhi_device(dev);

	seq_printf((struct seq_file *)data, "%s: type: %s dev_wake: %u",
		   mhi_dev->name, mhi_dev->dev_type ? "Controller" : "Transfer",
		   mhi_dev->dev_wake);

	/* for transfer device types only */
	if (mhi_dev->dev_type == MHI_DEVICE_XFER)
		seq_printf((struct seq_file *)data, " channels: %u(UL)/%u(DL)",
			   mhi_dev->ul_chan_id, mhi_dev->dl_chan_id);

	seq_puts((struct seq_file *)data, "\n");

	return 0;
}

static int mhi_debugfs_devices_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;

	if (!mhi_is_active(mhi_cntrl)) {
		seq_puts(m, "Device not ready\n");
		return -ENODEV;
	}

	/* Show controller and client(s) info */
	mhi_device_info_show(&mhi_cntrl->mhi_dev->dev, m);
	device_for_each_child(&mhi_cntrl->mhi_dev->dev, m, mhi_device_info_show);

	return 0;
}

static int mhi_debugfs_regdump_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;
	enum mhi_state state;
	enum mhi_ee_type ee;
	int i, ret = -EIO;
	u32 val;
	void __iomem *mhi_base = mhi_cntrl->regs;
	void __iomem *bhi_base = mhi_cntrl->bhi;
	void __iomem *bhie_base = mhi_cntrl->bhie;
	void __iomem *wake_db = mhi_cntrl->wake_db;
	struct {
		const char *name;
		int offset;
		void __iomem *base;
	} regs[] = {
		{ "MHI_REGLEN", MHIREGLEN, mhi_base},
		{ "MHI_VER", MHIVER, mhi_base},
		{ "MHI_CFG", MHICFG, mhi_base},
		{ "MHI_CTRL", MHICTRL, mhi_base},
		{ "MHI_STATUS", MHISTATUS, mhi_base},
		{ "MHI_WAKE_DB", 0, wake_db},
		{ "BHI_EXECENV", BHI_EXECENV, bhi_base},
		{ "BHI_STATUS", BHI_STATUS, bhi_base},
		{ "BHI_ERRCODE", BHI_ERRCODE, bhi_base},
		{ "BHI_ERRDBG1", BHI_ERRDBG1, bhi_base},
		{ "BHI_ERRDBG2", BHI_ERRDBG2, bhi_base},
		{ "BHI_ERRDBG3", BHI_ERRDBG3, bhi_base},
		{ "BHIE_TXVEC_DB", BHIE_TXVECDB_OFFS, bhie_base},
		{ "BHIE_TXVEC_STATUS", BHIE_TXVECSTATUS_OFFS, bhie_base},
		{ "BHIE_RXVEC_DB", BHIE_RXVECDB_OFFS, bhie_base},
		{ "BHIE_RXVEC_STATUS", BHIE_RXVECSTATUS_OFFS, bhie_base},
		{ NULL },
	};

	if (!MHI_REG_ACCESS_VALID(mhi_cntrl->pm_state))
		return ret;

	seq_printf(m, "Host PM state: %s Device state: %s EE: %s\n",
		   to_mhi_pm_state_str(mhi_cntrl->pm_state),
		   TO_MHI_STATE_STR(mhi_cntrl->dev_state),
		   TO_MHI_EXEC_STR(mhi_cntrl->ee));

	state = mhi_get_mhi_state(mhi_cntrl);
	ee = mhi_get_exec_env(mhi_cntrl);
	seq_printf(m, "Device EE: %s state: %s\n", TO_MHI_EXEC_STR(ee),
		   TO_MHI_STATE_STR(state));

	for (i = 0; regs[i].name; i++) {
		if (!regs[i].base)
			continue;
		ret = mhi_read_reg(mhi_cntrl, regs[i].base, regs[i].offset,
				   &val);
		if (ret)
			continue;

		seq_printf(m, "%s: 0x%x\n", regs[i].name, val);
	}

	return 0;
}

static int mhi_debugfs_device_wake_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;

	if (!mhi_is_active(mhi_cntrl)) {
		seq_puts(m, "Device not ready\n");
		return -ENODEV;
	}

	seq_printf(m,
		   "Wake count: %d\n%s\n", mhi_dev->dev_wake,
		   "Usage: echo get/put > device_wake to vote/unvote for M0");

	return 0;
}

static ssize_t mhi_debugfs_device_wake_write(struct file *file,
					     const char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct seq_file	*m = file->private_data;
	struct mhi_controller *mhi_cntrl = m->private;
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;
	char buf[16];
	int ret = -EINVAL;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "get", 3)) {
		ret = mhi_device_get_sync(mhi_dev);
	} else if (!strncmp(buf, "put", 3)) {
		mhi_device_put(mhi_dev);
		ret = 0;
	}

	return ret ? ret : count;
}

static int mhi_debugfs_timeout_ms_show(struct seq_file *m, void *d)
{
	struct mhi_controller *mhi_cntrl = m->private;

	seq_printf(m, "%u ms\n", mhi_cntrl->timeout_ms);

	return 0;
}

static ssize_t mhi_debugfs_timeout_ms_write(struct file *file,
					    const char __user *ubuf,
					    size_t count, loff_t *ppos)
{
	struct seq_file	*m = file->private_data;
	struct mhi_controller *mhi_cntrl = m->private;
	u32 timeout_ms;

	if (kstrtou32_from_user(ubuf, count, 0, &timeout_ms))
		return -EINVAL;

	mhi_cntrl->timeout_ms = timeout_ms;

	return count;
}

static int mhi_debugfs_states_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_states_show, inode->i_private);
}

static int mhi_debugfs_events_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_events_show, inode->i_private);
}

static int mhi_debugfs_channels_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_channels_show, inode->i_private);
}

static int mhi_debugfs_devices_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_devices_show, inode->i_private);
}

static int mhi_debugfs_regdump_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_regdump_show, inode->i_private);
}

static int mhi_debugfs_device_wake_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_device_wake_show, inode->i_private);
}

static int mhi_debugfs_timeout_ms_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_debugfs_timeout_ms_show, inode->i_private);
}

static const struct file_operations debugfs_states_fops = {
	.open = mhi_debugfs_states_open,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_events_fops = {
	.open = mhi_debugfs_events_open,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_channels_fops = {
	.open = mhi_debugfs_channels_open,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_devices_fops = {
	.open = mhi_debugfs_devices_open,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_regdump_fops = {
	.open = mhi_debugfs_regdump_open,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_device_wake_fops = {
	.open = mhi_debugfs_device_wake_open,
	.write = mhi_debugfs_device_wake_write,
	.release = single_release,
	.read = seq_read,
};

static const struct file_operations debugfs_timeout_ms_fops = {
	.open = mhi_debugfs_timeout_ms_open,
	.write = mhi_debugfs_timeout_ms_write,
	.release = single_release,
	.read = seq_read,
};

static struct dentry *mhi_debugfs_root;

void mhi_create_debugfs(struct mhi_controller *mhi_cntrl)
{
	mhi_cntrl->debugfs_dentry =
			debugfs_create_dir(dev_name(&mhi_cntrl->mhi_dev->dev),
					   mhi_debugfs_root);

	debugfs_create_file("states", 0444, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_states_fops);
	debugfs_create_file("events", 0444, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_events_fops);
	debugfs_create_file("channels", 0444, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_channels_fops);
	debugfs_create_file("devices", 0444, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_devices_fops);
	debugfs_create_file("regdump", 0444, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_regdump_fops);
	debugfs_create_file("device_wake", 0644, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_device_wake_fops);
	debugfs_create_file("timeout_ms", 0644, mhi_cntrl->debugfs_dentry,
			    mhi_cntrl, &debugfs_timeout_ms_fops);
}

void mhi_destroy_debugfs(struct mhi_controller *mhi_cntrl)
{
	debugfs_remove_recursive(mhi_cntrl->debugfs_dentry);
	mhi_cntrl->debugfs_dentry = NULL;
}

void mhi_debugfs_init(void)
{
	mhi_debugfs_root = debugfs_create_dir(mhi_bus_type.name, NULL);
}

void mhi_debugfs_exit(void)
{
	debugfs_remove_recursive(mhi_debugfs_root);
}
