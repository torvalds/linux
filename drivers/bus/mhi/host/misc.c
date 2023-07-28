// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include "internal.h"

static const char * const mhi_log_level_str[MHI_MSG_LVL_MAX] = {
	[MHI_MSG_LVL_VERBOSE] = "Verbose",
	[MHI_MSG_LVL_INFO] = "Info",
	[MHI_MSG_LVL_ERROR] = "Error",
	[MHI_MSG_LVL_CRITICAL] = "Critical",
	[MHI_MSG_LVL_MASK_ALL] = "Mask all",
};
#define TO_MHI_LOG_LEVEL_STR(level) ((level >= MHI_MSG_LVL_MAX || \
				     !mhi_log_level_str[level]) ? \
				     "Mask all" : mhi_log_level_str[level])

#define MHI_NUMERIC_DEVICE_ID(dev, domain, bus, slot) \
	((dev & 0xFFFF) << 16 | (domain & 0xF) << 12 | (bus & 0xFF) << 4 | \
	 (slot & 0xF))

#define MHI_DTR_CHANNEL 19

struct mhi_bus mhi_bus;

void mhi_misc_init(void)
{
	mutex_init(&mhi_bus.lock);
	INIT_LIST_HEAD(&mhi_bus.controller_list);
}

void mhi_misc_exit(void)
{
	mutex_destroy(&mhi_bus.lock);
}

static void *mhi_misc_to_virtual(struct mhi_ring *ring, dma_addr_t addr)
{
	return (addr - ring->iommu_base) + ring->base;
}

static dma_addr_t mhi_misc_to_physical(struct mhi_ring *ring, void *addr)
{
	return (addr - ring->base) + ring->iommu_base;
}

static ssize_t time_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	u64 t_host, t_device;
	int ret;

	ret = mhi_get_remote_time_sync(mhi_dev, &t_host, &t_device);
	if (ret) {
		MHI_ERR(dev, "Failed to obtain time, ret:%d\n", ret);
		return scnprintf(buf, PAGE_SIZE,
				 "Request failed or feature unsupported\n");
	}

	return scnprintf(buf, PAGE_SIZE, "local: %llu remote: %llu (ticks)\n",
			 t_host, t_device);
}
static DEVICE_ATTR_RO(time);

static void mhi_time_async_cb(struct mhi_device *mhi_dev, u32 sequence,
			      u64 local_time, u64 remote_time)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_dev->dev;

	MHI_LOG(dev, "Time response: seq:%llx local: %llu remote: %llu (ticks)\n",
		sequence, local_time, remote_time);
}

static ssize_t time_async_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	u32 seq = get_random_u32();
	int ret;

	if (!seq)
		seq = 1;

	ret = mhi_get_remote_time(mhi_dev, seq, &mhi_time_async_cb);
	if (ret) {
		MHI_ERR(dev, "Failed to request time, seq:%llx, ret:%d\n", seq, ret);
		return scnprintf(buf, PAGE_SIZE,
				 "Request failed or feature unsupported\n");
	}

	return scnprintf(buf, PAGE_SIZE,
			 "Requested time asynchronously with seq:%llx\n", seq);
}
static DEVICE_ATTR_RO(time_async);

static struct attribute *mhi_tsync_attrs[] = {
	&dev_attr_time.attr,
	&dev_attr_time_async.attr,
	NULL,
};

static const struct attribute_group mhi_tsync_group = {
	.attrs = mhi_tsync_attrs,
};

static ssize_t log_level_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct mhi_private *mhi_priv = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	if (!mhi_priv)
		return -EIO;

	return scnprintf(buf, PAGE_SIZE, "IPC log level begins from: %s\n",
			 TO_MHI_LOG_LEVEL_STR(mhi_priv->log_lvl));
}

static ssize_t log_level_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct mhi_private *mhi_priv = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);
	enum MHI_DEBUG_LEVEL log_level;

	if (kstrtou32(buf, 0, &log_level) < 0)
		return -EINVAL;

	if (!mhi_priv)
		return -EIO;

	mhi_priv->log_lvl = log_level;

	MHI_LOG(dev, "IPC log level changed to: %s\n",
		TO_MHI_LOG_LEVEL_STR(log_level));

	return count;
}
static DEVICE_ATTR_RW(log_level);

static struct attribute *mhi_misc_attrs[] = {
	&dev_attr_log_level.attr,
	NULL,
};

static const struct attribute_group mhi_misc_group = {
	.attrs = mhi_misc_attrs,
};

void mhi_force_reg_write(struct mhi_controller *mhi_cntrl)
{
	struct mhi_private *mhi_priv =
				dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	if (!(mhi_cntrl->db_access & MHI_PM_M2))
		flush_work(&mhi_priv->reg_write_work);
}

void mhi_reset_reg_write_q(struct mhi_controller *mhi_cntrl)
{
	struct mhi_private *mhi_priv =
				dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	if (mhi_cntrl->db_access & MHI_PM_M2)
		return;

	cancel_work_sync(&mhi_priv->reg_write_work);
	memset(mhi_priv->reg_write_q, 0,
	       sizeof(struct reg_write_info) * REG_WRITE_QUEUE_LEN);
	mhi_priv->read_idx = 0;
	atomic_set(&mhi_priv->write_idx, -1);
}

static void mhi_reg_write_enqueue(struct mhi_private *mhi_priv,
	void __iomem *reg_addr, u32 val)
{
	u32 q_index = atomic_inc_return(&mhi_priv->write_idx);

	q_index = q_index & (REG_WRITE_QUEUE_LEN - 1);

	if (mhi_priv->reg_write_q[q_index].valid)
		panic("queue full idx %d", q_index);

	mhi_priv->reg_write_q[q_index].reg_addr = reg_addr;
	mhi_priv->reg_write_q[q_index].val = val;

	/*
	 * prevent reordering to make sure val is set before valid is set to
	 * true. This prevents offload worker running on another core to write
	 * stale value to register with valid set to true.
	 */
	smp_wmb();

	mhi_priv->reg_write_q[q_index].valid = true;

	/*
	 * make sure valid value is visible to other cores to prevent offload
	 * worker from skipping the reg write.
	 */
	 smp_wmb();
}

void mhi_write_reg_offload(struct mhi_controller *mhi_cntrl,
		   void __iomem *base,
		   u32 offset,
		   u32 val)
{
	struct mhi_private *mhi_priv =
				dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	mhi_reg_write_enqueue(mhi_priv, base + offset, val);
	queue_work(mhi_priv->offload_wq, &mhi_priv->reg_write_work);
}

void mhi_write_offload_wakedb(struct mhi_controller *mhi_cntrl, int db_val)
{
	mhi_write_reg_offload(mhi_cntrl, mhi_cntrl->wake_db, 4,
			      upper_32_bits(db_val));
	mhi_write_reg_offload(mhi_cntrl, mhi_cntrl->wake_db, 0,
			      lower_32_bits(db_val));
}

void mhi_reg_write_work(struct work_struct *w)
{
	struct mhi_private *mhi_priv = container_of(w,
						struct mhi_private,
						reg_write_work);
	struct mhi_controller *mhi_cntrl = mhi_priv->mhi_cntrl;
	struct pci_dev *parent = to_pci_dev(mhi_cntrl->cntrl_dev);
	struct reg_write_info *info =
				&mhi_priv->reg_write_q[mhi_priv->read_idx];

	if (!info->valid)
		return;

	if (!mhi_is_active(mhi_cntrl))
		return;

	if (msm_pcie_prevent_l1(parent))
		return;

	while (info->valid) {
		if (!mhi_is_active(mhi_cntrl))
			break;

		writel_relaxed(info->val, info->reg_addr);
		info->valid = false;
		mhi_priv->read_idx =
				(mhi_priv->read_idx + 1) &
						(REG_WRITE_QUEUE_LEN - 1);
		info = &mhi_priv->reg_write_q[mhi_priv->read_idx];
	}

	msm_pcie_allow_l1(parent);
}

int mhi_misc_sysfs_create(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret = 0;

	ret = sysfs_create_group(&dev->kobj, &mhi_misc_group);
	if (ret) {
		MHI_ERR(dev, "Failed to create misc sysfs group\n");
		return ret;
	}

	ret = sysfs_create_group(&dev->kobj, &mhi_tsync_group);
	if (ret) {
		MHI_ERR(dev, "Failed to create time synchronization sysfs group\n");
		return ret;
	}

	return ret;
}

void  mhi_misc_sysfs_destroy(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;

	sysfs_remove_group(&dev->kobj, &mhi_tsync_group);
	sysfs_remove_group(&dev->kobj, &mhi_misc_group);
}

int mhi_misc_register_controller(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = kzalloc(sizeof(*mhi_priv), GFP_KERNEL);
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;
	struct pci_dev *parent = to_pci_dev(mhi_cntrl->cntrl_dev);
	int ret = 0;

	if (!mhi_priv)
		return -ENOMEM;

	if (parent) {
		dev_set_name(&mhi_dev->dev, "mhi_%04x_%02u.%02u.%02u",
			     parent->device, pci_domain_nr(parent->bus),
			     parent->bus->number, PCI_SLOT(parent->devfn));
		mhi_dev->name = dev_name(&mhi_dev->dev);

		mhi_priv->numeric_id = MHI_NUMERIC_DEVICE_ID(parent->device,
						    pci_domain_nr(parent->bus),
						    parent->bus->number,
						    PCI_SLOT(parent->devfn));
	}

	mhi_priv->log_buf = ipc_log_context_create(MHI_IPC_LOG_PAGES,
						   mhi_dev->name, 0);
	mhi_priv->mhi_cntrl = mhi_cntrl;

	/* adding it to this list only for debug purpose */
	mutex_lock(&mhi_bus.lock);
	list_add_tail(&mhi_priv->node, &mhi_bus.controller_list);
	mutex_unlock(&mhi_bus.lock);

	dev_set_drvdata(dev, mhi_priv);

	mhi_priv->offload_wq = alloc_ordered_workqueue("mhi_offload_wq",
						       WQ_HIGHPRI);
	if (!mhi_priv->offload_wq) {
		dev_err(mhi_cntrl->cntrl_dev,
			"Failed to allocate offload workqueue\n");
		ret = -ENOMEM;
		goto ipc_ctx_cleanup;
	}

	INIT_WORK(&mhi_priv->reg_write_work, mhi_reg_write_work);

	mhi_priv->reg_write_q = kcalloc(REG_WRITE_QUEUE_LEN,
					sizeof(*mhi_priv->reg_write_q),
					GFP_KERNEL);
	if (!mhi_priv->reg_write_q) {
		ret = -ENOMEM;
		goto wq_cleanup;
	}

	atomic_set(&mhi_priv->write_idx, -1);

	return 0;

wq_cleanup:
	destroy_workqueue(mhi_priv->offload_wq);
ipc_ctx_cleanup:
	ipc_log_context_destroy(mhi_priv->log_buf);

	return ret;
}

void mhi_misc_unregister_controller(struct mhi_controller *mhi_cntrl)
{
	struct mhi_private *mhi_priv = dev_get_drvdata(&mhi_cntrl->mhi_dev->dev);

	if (!mhi_priv)
		return;

	mutex_lock(&mhi_bus.lock);
	list_del(&mhi_priv->node);
	mutex_unlock(&mhi_bus.lock);

	kfree(mhi_priv->reg_write_q);

	if (mhi_priv->sfr_info)
		kfree(mhi_priv->sfr_info->str);
	kfree(mhi_priv->sfr_info);
	kfree(mhi_priv->timesync);
	kfree(mhi_priv);
}

void *mhi_controller_get_privdata(struct mhi_controller *mhi_cntrl)
{
	struct mhi_device *mhi_dev;
	struct mhi_private *mhi_priv;

	if (!mhi_cntrl)
		return NULL;

	mhi_dev = mhi_cntrl->mhi_dev;
	if (!mhi_dev)
		return NULL;

	mhi_priv = dev_get_drvdata(&mhi_dev->dev);
	if (!mhi_priv)
		return NULL;

	return mhi_priv->priv_data;
}
EXPORT_SYMBOL(mhi_controller_get_privdata);

void mhi_controller_set_privdata(struct mhi_controller *mhi_cntrl, void *priv)
{
	struct mhi_device *mhi_dev;
	struct mhi_private *mhi_priv;

	if (!mhi_cntrl)
		return;

	mhi_dev = mhi_cntrl->mhi_dev;
	if (!mhi_dev)
		return;

	mhi_priv = dev_get_drvdata(&mhi_dev->dev);
	if (!mhi_priv)
		return;

	mhi_priv->priv_data = priv;
}
EXPORT_SYMBOL(mhi_controller_set_privdata);

static struct mhi_controller *find_mhi_controller_by_name(const char *name)
{
	struct mhi_private *mhi_priv, *tmp_priv;
	struct mhi_controller *mhi_cntrl;

	list_for_each_entry_safe(mhi_priv, tmp_priv, &mhi_bus.controller_list,
				 node) {
		mhi_cntrl = mhi_priv->mhi_cntrl;
		if (mhi_cntrl->mhi_dev->name && (!strcmp(name, mhi_cntrl->mhi_dev->name)))
			return mhi_cntrl;
	}

	return NULL;
}

struct mhi_controller *mhi_bdf_to_controller(u32 domain,
					     u32 bus,
					     u32 slot,
					     u32 dev_id)
{
	char name[32];

	snprintf(name, sizeof(name), "mhi_%04x_%02u.%02u.%02u", dev_id, domain,
		 bus, slot);

	return find_mhi_controller_by_name(name);
}
EXPORT_SYMBOL(mhi_bdf_to_controller);

static int mhi_notify_fatal_cb(struct device *dev, void *data)
{
	mhi_notify(to_mhi_device(dev), MHI_CB_FATAL_ERROR);

	return 0;
}

int mhi_report_error(struct mhi_controller *mhi_cntrl)
{
	struct device *dev;
	struct mhi_private *mhi_priv;
	struct mhi_sfr_info *sfr_info;
	enum mhi_pm_state cur_state;
	unsigned long flags;

	if (!mhi_cntrl)
		return -EINVAL;

	dev = &mhi_cntrl->mhi_dev->dev;
	mhi_priv = dev_get_drvdata(dev);
	sfr_info = mhi_priv->sfr_info;

	write_lock_irqsave(&mhi_cntrl->pm_lock, flags);

	cur_state = mhi_tryset_pm_state(mhi_cntrl, MHI_PM_SYS_ERR_DETECT);
	if (cur_state != MHI_PM_SYS_ERR_DETECT) {
		MHI_ERR(dev,
			"Failed to move to state: %s from: %s\n",
			to_mhi_pm_state_str(MHI_PM_SYS_ERR_DETECT),
			to_mhi_pm_state_str(mhi_cntrl->pm_state));

		write_unlock_irqrestore(&mhi_cntrl->pm_lock, flags);
		return -EPERM;
	}

	/* force inactive/error state */
	mhi_cntrl->dev_state = MHI_STATE_SYS_ERR;
	wake_up_all(&mhi_cntrl->state_event);
	write_unlock_irqrestore(&mhi_cntrl->pm_lock, flags);

	/* copy subsystem failure reason string if supported */
	if (sfr_info && sfr_info->buf_addr) {
		memcpy(sfr_info->str, sfr_info->buf_addr, sfr_info->len);
		MHI_ERR(dev, "mhi: %s sfr: %s\n", dev_name(dev), sfr_info->buf_addr);
	}

	/* Notify fatal error to all client drivers to halt processing */
	device_for_each_child(&mhi_cntrl->mhi_dev->dev, NULL,
			      mhi_notify_fatal_cb);

	return 0;
}
EXPORT_SYMBOL(mhi_report_error);

int mhi_device_configure(struct mhi_device *mhi_dev,
			 enum dma_data_direction dir,
			 struct mhi_buf *cfg_tbl,
			 int elements)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_chan *mhi_chan;
	struct mhi_event_ctxt *er_ctxt;
	struct mhi_chan_ctxt *ch_ctxt;
	int er_index, chan;

	switch (dir) {
	case DMA_TO_DEVICE:
		mhi_chan = mhi_dev->ul_chan;
		break;
	case DMA_BIDIRECTIONAL:
	case DMA_FROM_DEVICE:
	case DMA_NONE:
		mhi_chan = mhi_dev->dl_chan;
		break;
	default:
		return -EINVAL;
	}

	er_index = mhi_chan->er_index;
	chan = mhi_chan->chan;

	for (; elements > 0; elements--, cfg_tbl++) {
		/* update event context array */
		if (!strcmp(cfg_tbl->name, "ECA")) {
			er_ctxt = &mhi_cntrl->mhi_ctxt->er_ctxt[er_index];
			if (sizeof(*er_ctxt) != cfg_tbl->len) {
				MHI_ERR(dev,
					"Invalid ECA size, expected:%zu actual%zu\n",
					sizeof(*er_ctxt), cfg_tbl->len);
				return -EINVAL;
			}
			memcpy((void *)er_ctxt, cfg_tbl->buf, sizeof(*er_ctxt));
			continue;
		}

		/* update channel context array */
		if (!strcmp(cfg_tbl->name, "CCA")) {
			ch_ctxt = &mhi_cntrl->mhi_ctxt->chan_ctxt[chan];
			if (cfg_tbl->len != sizeof(*ch_ctxt)) {
				MHI_ERR(dev,
					"Invalid CCA size, expected:%zu actual:%zu\n",
					sizeof(*ch_ctxt), cfg_tbl->len);
				return -EINVAL;
			}
			memcpy((void *)ch_ctxt, cfg_tbl->buf, sizeof(*ch_ctxt));
			continue;
		}

		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mhi_device_configure);

void mhi_set_m2_timeout_ms(struct mhi_controller *mhi_cntrl, u32 timeout)
{
	struct mhi_device *mhi_dev;
	struct mhi_private *mhi_priv;

	if (!mhi_cntrl)
		return;

	mhi_dev = mhi_cntrl->mhi_dev;
	if (!mhi_dev)
		return;

	mhi_priv = dev_get_drvdata(&mhi_dev->dev);
	if (!mhi_priv)
		return;

	mhi_priv->m2_timeout_ms = timeout;
}
EXPORT_SYMBOL(mhi_set_m2_timeout_ms);

int mhi_pm_fast_resume(struct mhi_controller *mhi_cntrl, bool notify_clients)
{
	struct mhi_chan *itr, *tmp;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	MHI_VERB(dev, "Entered with PM state: %s, MHI state: %s notify: %s\n",
		 to_mhi_pm_state_str(mhi_cntrl->pm_state),
		 mhi_state_str(mhi_cntrl->dev_state),
		 notify_clients ? "true" : "false");

	if (mhi_cntrl->pm_state == MHI_PM_DISABLE)
		return 0;

	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state))
		return -EIO;

	read_lock_bh(&mhi_cntrl->pm_lock);
	WARN_ON(mhi_cntrl->pm_state != MHI_PM_M3);
	read_unlock_bh(&mhi_cntrl->pm_lock);

	if (mhi_cntrl->rddm_image && mhi_get_exec_env(mhi_cntrl) == MHI_EE_RDDM
	    && mhi_is_active(mhi_cntrl)) {
		mhi_cntrl->ee = MHI_EE_RDDM;

		MHI_ERR(dev, "RDDM event occurred!\n");

		/* notify critical clients with early notifications */
		mhi_report_error(mhi_cntrl);

		mhi_cntrl->status_cb(mhi_cntrl, MHI_CB_EE_RDDM);
		wake_up_all(&mhi_cntrl->state_event);

		return 0;
	}

	/* Notify clients about exiting LPM */
	if (notify_clients) {
		list_for_each_entry_safe(itr, tmp, &mhi_cntrl->lpm_chans,
					 node) {
			mutex_lock(&itr->mutex);
			if (itr->mhi_dev)
				mhi_notify(itr->mhi_dev, MHI_CB_LPM_EXIT);
			mutex_unlock(&itr->mutex);
		}
	}

	/* disable primary event ring processing to prevent interference */
	tasklet_disable(&mhi_cntrl->mhi_event->task);

	write_lock_irq(&mhi_cntrl->pm_lock);

	/* re-check to make sure no error has occurred before proceeding */
	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state)) {
		write_unlock_irq(&mhi_cntrl->pm_lock);
		tasklet_enable(&mhi_cntrl->mhi_event->task);
		return -EIO;
	}

	/* restore the states */
	mhi_cntrl->pm_state = mhi_priv->saved_pm_state;
	mhi_cntrl->dev_state = mhi_priv->saved_dev_state;

	write_unlock_irq(&mhi_cntrl->pm_lock);

	switch (mhi_cntrl->pm_state) {
	case MHI_PM_M0:
		mhi_pm_m0_transition(mhi_cntrl);
		break;
	case MHI_PM_M2:
		read_lock_bh(&mhi_cntrl->pm_lock);
		mhi_cntrl->wake_get(mhi_cntrl, true);
		mhi_cntrl->wake_put(mhi_cntrl, true);
		read_unlock_bh(&mhi_cntrl->pm_lock);
		break;
	default:
		MHI_ERR(dev, "Unexpected PM state:%s after restore\n",
			to_mhi_pm_state_str(mhi_cntrl->pm_state));
	}

	/* enable primary event ring processing and check for events */
	tasklet_enable(&mhi_cntrl->mhi_event->task);
	mhi_irq_handler(0, mhi_cntrl->mhi_event);

	return 0;
}
EXPORT_SYMBOL(mhi_pm_fast_resume);

int mhi_pm_fast_suspend(struct mhi_controller *mhi_cntrl, bool notify_clients)
{
	struct mhi_chan *itr, *tmp;
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;
	struct device *dev = &mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	enum mhi_pm_state new_state;
	int ret;

	if (mhi_cntrl->pm_state == MHI_PM_DISABLE)
		return -EINVAL;

	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state))
		return -EIO;

	/* check if host/clients have any bus votes or packets to be sent */
	if (atomic_read(&mhi_cntrl->pending_pkts))
		return -EBUSY;

	/* wait for the device to attempt a low power mode (M2 entry) */
	wait_event_timeout(mhi_cntrl->state_event,
			   mhi_cntrl->dev_state == MHI_STATE_M2,
			   msecs_to_jiffies(mhi_priv->m2_timeout_ms));

	/* disable primary event ring processing to prevent interference */
	tasklet_disable(&mhi_cntrl->mhi_event->task);

	write_lock_irq(&mhi_cntrl->pm_lock);

	/* re-check if host/clients have any bus votes or packets to be sent */
	if (atomic_read(&mhi_cntrl->pending_pkts)) {
		ret = -EBUSY;
		goto error_suspend;
	}

	/* re-check to make sure no error has occurred before proceeding */
	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state)) {
		ret = -EIO;
		goto error_suspend;
	}

	MHI_VERB(dev, "Allowing Fast M3 transition with notify: %s\n",
		notify_clients ? "true" : "false");

	/* save the current states */
	mhi_priv->saved_pm_state = mhi_cntrl->pm_state;
	mhi_priv->saved_dev_state = mhi_cntrl->dev_state;

	/* move from M2 to M0 as device can allow the transition but not host */
	if (mhi_cntrl->pm_state == MHI_PM_M2) {
		new_state = mhi_tryset_pm_state(mhi_cntrl, MHI_PM_M0);
		if (new_state != MHI_PM_M0) {
			MHI_ERR(dev, "Error setting to PM state: %s from: %s\n",
				to_mhi_pm_state_str(MHI_PM_M0),
				to_mhi_pm_state_str(mhi_cntrl->pm_state));
			ret = -EIO;
			goto error_suspend;
		}
	}

	new_state = mhi_tryset_pm_state(mhi_cntrl, MHI_PM_M3_ENTER);
	if (new_state != MHI_PM_M3_ENTER) {
		MHI_ERR(dev, "Error setting to PM state: %s from: %s\n",
			to_mhi_pm_state_str(MHI_PM_M3_ENTER),
			to_mhi_pm_state_str(mhi_cntrl->pm_state));
		ret = -EIO;
		goto error_suspend;
	}

	/* set dev_state to M3_FAST and host pm_state to M3 */
	new_state = mhi_tryset_pm_state(mhi_cntrl, MHI_PM_M3);
	if (new_state != MHI_PM_M3) {
		MHI_ERR(dev, "Error setting to PM state: %s from: %s\n",
			to_mhi_pm_state_str(MHI_PM_M3),
			to_mhi_pm_state_str(mhi_cntrl->pm_state));
		ret = -EIO;
		goto error_suspend;
	}

	mhi_cntrl->dev_state = MHI_STATE_M3_FAST;
	mhi_cntrl->M3_fast++;

	write_unlock_irq(&mhi_cntrl->pm_lock);

	/* finish reg writes before DRV hand-off to avoid noc err */
	mhi_force_reg_write(mhi_cntrl);

	/* enable primary event ring processing and check for events */
	tasklet_enable(&mhi_cntrl->mhi_event->task);
	mhi_irq_handler(0, mhi_cntrl->mhi_event);

	/* Notify clients about entering LPM */
	if (notify_clients) {
		list_for_each_entry_safe(itr, tmp, &mhi_cntrl->lpm_chans,
					 node) {
			mutex_lock(&itr->mutex);
			if (itr->mhi_dev)
				mhi_notify(itr->mhi_dev, MHI_CB_LPM_ENTER);
			mutex_unlock(&itr->mutex);
		}
	}

	return 0;

error_suspend:
	write_unlock_irq(&mhi_cntrl->pm_lock);

	/* enable primary event ring processing and check for events */
	tasklet_enable(&mhi_cntrl->mhi_event->task);
	mhi_irq_handler(0, mhi_cntrl->mhi_event);

	return ret;
}
EXPORT_SYMBOL(mhi_pm_fast_suspend);

static void mhi_process_sfr(struct mhi_controller *mhi_cntrl,
			    struct file_info *info)
{
	struct mhi_buf *mhi_buf = mhi_cntrl->rddm_image->mhi_buf;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	u8 *sfr_buf, *file_offset = info->file_offset;
	u32 file_size = info->file_size;
	u32 rem_seg_len = info->rem_seg_len;
	u32 seg_idx = info->seg_idx;

	sfr_buf = kzalloc(file_size + 1, GFP_KERNEL);
	if (!sfr_buf)
		return;

	while (file_size) {
		/* file offset starting from seg base */
		if (!rem_seg_len) {
			file_offset = mhi_buf[seg_idx].buf;
			if (file_size > mhi_buf[seg_idx].len)
				rem_seg_len = mhi_buf[seg_idx].len;
			else
				rem_seg_len = file_size;
		}

		if (file_size <= rem_seg_len) {
			memcpy(sfr_buf, file_offset, file_size);
			break;
		}

		memcpy(sfr_buf, file_offset, rem_seg_len);
		sfr_buf += rem_seg_len;
		file_size -= rem_seg_len;
		rem_seg_len = 0;
		seg_idx++;
		if (seg_idx == mhi_cntrl->rddm_image->entries) {
			MHI_ERR(dev, "invalid size for SFR file\n");
			goto err;
		}
	}
	sfr_buf[info->file_size] = '\0';

	/* force sfr string to log in kernel msg */
	MHI_ERR(dev, "%s\n", sfr_buf);
err:
	kfree(sfr_buf);
}

static int mhi_find_next_file_offset(struct mhi_controller *mhi_cntrl,
				     struct file_info *info,
				     struct rddm_table_info *table_info)
{
	struct mhi_buf *mhi_buf = mhi_cntrl->rddm_image->mhi_buf;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;

	if (info->rem_seg_len >= table_info->size) {
		info->file_offset += table_info->size;
		info->rem_seg_len -= table_info->size;
		return 0;
	}

	info->file_size = table_info->size - info->rem_seg_len;
	info->rem_seg_len = 0;
	/* iterate over segments until eof is reached */
	while (info->file_size) {
		info->seg_idx++;
		if (info->seg_idx == mhi_cntrl->rddm_image->entries) {
			MHI_ERR(dev, "invalid size for file %s\n",
				table_info->file_name);
			return -EINVAL;
		}
		if (info->file_size > mhi_buf[info->seg_idx].len) {
			info->file_size -= mhi_buf[info->seg_idx].len;
		} else {
			info->file_offset = mhi_buf[info->seg_idx].buf +
				info->file_size;
			info->rem_seg_len = mhi_buf[info->seg_idx].len -
				info->file_size;
			info->file_size = 0;
		}
	}

	return 0;
}

void mhi_dump_sfr(struct mhi_controller *mhi_cntrl)
{
	struct mhi_buf *mhi_buf = mhi_cntrl->rddm_image->mhi_buf;
	struct rddm_header *rddm_header =
		(struct rddm_header *)mhi_buf->buf;
	struct rddm_table_info *table_info;
	struct file_info info;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	u32 table_size, n;

	memset(&info, 0, sizeof(info));

	if (rddm_header->header_size > sizeof(*rddm_header) ||
			rddm_header->header_size < 8) {
		MHI_ERR(dev, "invalid reported header size %u\n",
			rddm_header->header_size);
		return;
	}

	table_size = (rddm_header->header_size - 8) / sizeof(*table_info);
	if (!table_size) {
		MHI_ERR(dev, "invalid rddm table size %u\n", table_size);
		return;
	}

	info.file_offset = (u8 *)rddm_header + rddm_header->header_size;
	info.rem_seg_len = mhi_buf[0].len - rddm_header->header_size;
	for (n = 0; n < table_size; n++) {
		table_info = &rddm_header->table_info[n];

		if (!strcmp(table_info->file_name, "Q6-SFR.bin")) {
			info.file_size = table_info->size;
			mhi_process_sfr(mhi_cntrl, &info);
			return;
		}

		if (mhi_find_next_file_offset(mhi_cntrl, &info, table_info))
			return;
	}
}
EXPORT_SYMBOL(mhi_dump_sfr);

bool mhi_scan_rddm_cookie(struct mhi_controller *mhi_cntrl, u32 cookie)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret;
	u32 val;

	if (!mhi_cntrl->rddm_image || !cookie)
		return false;

	MHI_VERB(dev, "Checking BHI debug register for 0x%x\n", cookie);

	if (!MHI_REG_ACCESS_VALID(mhi_cntrl->pm_state))
		return false;

	ret = mhi_read_reg(mhi_cntrl, mhi_cntrl->bhi, BHI_ERRDBG2, &val);
	if (ret)
		return false;

	MHI_VERB(dev, "BHI_ERRDBG2 value:0x%x\n", val);
	if (val == cookie)
		return true;

	return false;
}
EXPORT_SYMBOL(mhi_scan_rddm_cookie);

void mhi_debug_reg_dump(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	enum mhi_state state;
	enum mhi_ee_type ee;
	int i, ret;
	u32 val;
	void __iomem *mhi_base = mhi_cntrl->regs;
	void __iomem *bhi_base = mhi_cntrl->bhi;
	void __iomem *bhie_base = mhi_cntrl->bhie;
	void __iomem *wake_db = mhi_cntrl->wake_db;
	struct {
		const char *name;
		int offset;
		void __iomem *base;
	} debug_reg[] = {
		{ "BHI_ERRDBG2", BHI_ERRDBG2, bhi_base},
		{ "BHI_ERRDBG3", BHI_ERRDBG3, bhi_base},
		{ "BHI_ERRDBG1", BHI_ERRDBG1, bhi_base},
		{ "BHI_ERRCODE", BHI_ERRCODE, bhi_base},
		{ "BHI_EXECENV", BHI_EXECENV, bhi_base},
		{ "BHI_STATUS", BHI_STATUS, bhi_base},
		{ "MHI_CNTRL", MHICTRL, mhi_base},
		{ "MHI_STATUS", MHISTATUS, mhi_base},
		{ "MHI_WAKE_DB", 0, wake_db},
		{ "BHIE_TXVEC_DB", BHIE_TXVECDB_OFFS, bhie_base},
		{ "BHIE_TXVEC_STATUS", BHIE_TXVECSTATUS_OFFS, bhie_base},
		{ "BHIE_RXVEC_DB", BHIE_RXVECDB_OFFS, bhie_base},
		{ "BHIE_RXVEC_STATUS", BHIE_RXVECSTATUS_OFFS, bhie_base},
		{ NULL },
	};

	MHI_ERR(dev, "host pm_state:%s dev_state:%s ee:%s\n",
		to_mhi_pm_state_str(mhi_cntrl->pm_state),
		mhi_state_str(mhi_cntrl->dev_state),
		TO_MHI_EXEC_STR(mhi_cntrl->ee));

	state = mhi_get_mhi_state(mhi_cntrl);
	ee = mhi_get_exec_env(mhi_cntrl);

	MHI_ERR(dev, "device ee: %s dev_state: %s\n", TO_MHI_EXEC_STR(ee),
		mhi_state_str(state));

	for (i = 0; debug_reg[i].name; i++) {
		if (!debug_reg[i].base)
			continue;
		ret = mhi_read_reg(mhi_cntrl, debug_reg[i].base,
				   debug_reg[i].offset, &val);
		MHI_ERR(dev, "reg: %s val: 0x%x, ret: %d\n", debug_reg[i].name,
			val, ret);
	}
}
EXPORT_SYMBOL(mhi_debug_reg_dump);

int mhi_device_get_sync_atomic(struct mhi_device *mhi_dev, int timeout_us,
			       bool in_panic)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_dev->dev;
	unsigned long pm_lock_flags;

	read_lock_irqsave(&mhi_cntrl->pm_lock, pm_lock_flags);
	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state)) {
		read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
		return -EIO;
	}

	mhi_cntrl->wake_get(mhi_cntrl, true);
	read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);

	mhi_dev->dev_wake++;
	pm_wakeup_event(&mhi_cntrl->mhi_dev->dev, 0);
	mhi_cntrl->runtime_get(mhi_cntrl);

	/* Return if client doesn't want us to wait */
	if (!timeout_us) {
		if (mhi_cntrl->pm_state != MHI_PM_M0)
			MHI_ERR(dev, "Return without waiting for M0\n");

		mhi_cntrl->runtime_put(mhi_cntrl);
		return 0;
	}

	if (in_panic) {
		while (mhi_get_mhi_state(mhi_cntrl) != MHI_STATE_M0 &&
		       !MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state) &&
		       timeout_us > 0) {
			udelay(MHI_FORCE_WAKE_DELAY_US);
			timeout_us -= MHI_FORCE_WAKE_DELAY_US;
		}
	} else {
		while (mhi_cntrl->pm_state != MHI_PM_M0 &&
		       !MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state) &&
		       timeout_us > 0) {
			udelay(MHI_FORCE_WAKE_DELAY_US);
			timeout_us -= MHI_FORCE_WAKE_DELAY_US;
		}
	}

	if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state) || timeout_us <= 0) {
		MHI_ERR(dev, "Did not enter M0, cur_state: %s pm_state: %s\n",
			mhi_state_str(mhi_cntrl->dev_state),
			to_mhi_pm_state_str(mhi_cntrl->pm_state));
		read_lock_irqsave(&mhi_cntrl->pm_lock, pm_lock_flags);
		mhi_cntrl->wake_put(mhi_cntrl, false);
		read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
		mhi_dev->dev_wake--;
		mhi_cntrl->runtime_put(mhi_cntrl);
		return -ETIMEDOUT;
	}

	mhi_cntrl->runtime_put(mhi_cntrl);

	return 0;
}
EXPORT_SYMBOL(mhi_device_get_sync_atomic);

static int mhi_get_capability_offset(struct mhi_controller *mhi_cntrl,
				     u32 capability, u32 *offset)
{
	u32 cur_cap, next_offset;
	int ret;

	/* get the 1st supported capability offset */
	ret = mhi_read_reg_field(mhi_cntrl, mhi_cntrl->regs, MISC_OFFSET,
				 MISC_CAP_MASK, offset);
	if (ret)
		return ret;
	do {
		if (*offset >= MHI_REG_SIZE)
			return -ENXIO;

		ret = mhi_read_reg_field(mhi_cntrl, mhi_cntrl->regs, *offset,
					 CAP_CAPID_MASK, &cur_cap);
		if (ret)
			return ret;

		if (cur_cap == capability)
			return 0;

		ret = mhi_read_reg_field(mhi_cntrl, mhi_cntrl->regs, *offset,
					 CAP_NEXT_CAP_MASK, &next_offset);
		if (ret)
			return ret;

		*offset = next_offset;
	} while (next_offset);

	return -ENXIO;
}

/* to be used only if a single event ring with the type is present */
static int mhi_get_er_index(struct mhi_controller *mhi_cntrl,
			    enum mhi_er_data_type type)
{
	int i;
	struct mhi_event *mhi_event = mhi_cntrl->mhi_event;

	/* find event ring for requested type */
	for (i = 0; i < mhi_cntrl->total_ev_rings; i++, mhi_event++) {
		if (mhi_event->data_type == type)
			return mhi_event->er_index;
	}

	return -ENOENT;
}

static int mhi_init_bw_scale(struct mhi_controller *mhi_cntrl,
			     void __iomem *bw_scale_db)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	int ret, er_index;
	u32 bw_cfg_offset;

	/* controller doesn't support dynamic bw switch */
	if (!mhi_priv->bw_scale)
		return -ENODEV;

	ret = mhi_get_capability_offset(mhi_cntrl, BW_SCALE_CAP_ID,
					&bw_cfg_offset);
	if (ret)
		return ret;

	/* No ER configured to support BW scale */
	er_index = mhi_get_er_index(mhi_cntrl, MHI_ER_BW_SCALE);
	if (er_index < 0)
		return er_index;

	bw_cfg_offset += BW_SCALE_CFG_OFFSET;

	mhi_priv->bw_scale_db = bw_scale_db;

	/* advertise host support */
	mhi_write_reg(mhi_cntrl, mhi_cntrl->regs, bw_cfg_offset,
		      MHI_BW_SCALE_SETUP(er_index));

	MHI_VERB(dev, "Bandwidth scaling setup complete. Event ring:%d\n",
		er_index);

	return 0;
}

int mhi_controller_setup_timesync(struct mhi_controller *mhi_cntrl,
				  u64 (*time_get)(struct mhi_controller *c),
				  int (*lpm_disable)(struct mhi_controller *c),
				  int (*lpm_enable)(struct mhi_controller *c))
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_timesync *mhi_tsync = kzalloc(sizeof(*mhi_tsync),
						 GFP_KERNEL);

	if (!mhi_tsync)
		return -ENOMEM;

	mhi_tsync->time_get = time_get;
	mhi_tsync->lpm_disable = lpm_disable;
	mhi_tsync->lpm_enable = lpm_enable;

	mhi_priv->timesync = mhi_tsync;

	return 0;
}
EXPORT_SYMBOL(mhi_controller_setup_timesync);

static int mhi_init_timesync(struct mhi_controller *mhi_cntrl,
			     void __iomem *time_db)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_timesync *mhi_tsync = mhi_priv->timesync;
	u32 time_offset;
	int ret, er_index;

	if (!mhi_tsync)
		return -EINVAL;

	ret = mhi_get_capability_offset(mhi_cntrl, TIMESYNC_CAP_ID,
					&time_offset);
	if (ret)
		return ret;

	/* save time_offset for obtaining time via MMIO register reads */
	mhi_tsync->time_reg = mhi_cntrl->regs + time_offset;

	mutex_init(&mhi_tsync->mutex);

	/* get timesync event ring configuration */
	er_index = mhi_get_er_index(mhi_cntrl, MHI_ER_TIMESYNC);
	if (er_index < 0)
		return 0;

	spin_lock_init(&mhi_tsync->lock);
	INIT_LIST_HEAD(&mhi_tsync->head);

	mhi_tsync->time_db = time_db;

	/* advertise host support */
	mhi_write_reg(mhi_cntrl, mhi_tsync->time_reg, TIMESYNC_CFG_OFFSET,
		      MHI_TIMESYNC_DB_SETUP(er_index));

	MHI_VERB(dev, "Time synchronization DB mode setup complete. Event ring:%d\n",
		 er_index);

	return 0;
}

int mhi_init_host_notification(struct mhi_controller *mhi_cntrl,
							void __iomem *host_notify_db)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret;
	u32 host_notify_cfg_offset;

	ret = mhi_get_capability_offset(mhi_cntrl, MHI_HOST_NOTIFY_CAP_ID,
									&host_notify_cfg_offset);
	if (ret)
		return ret;

	host_notify_cfg_offset += MHI_HOST_NOTIFY_CFG_OFFSET;
	mhi_cntrl->host_notify_db = host_notify_db;

	/* advertise host support */
	mhi_write_reg(mhi_cntrl, mhi_cntrl->regs, host_notify_cfg_offset,
			     MHI_HOST_NOTIFY_CFG_SETUP);

	MHI_VERB(dev, "Host notification DB setup complete.\n");

	return 0;
}

int mhi_misc_init_mmio(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	u32 chdb_off;
	int ret;

	/* Read channel db offset */
	ret = mhi_read_reg(mhi_cntrl, mhi_cntrl->regs, CHDBOFF,
				 &chdb_off);
	if (ret) {
		MHI_ERR(dev, "Unable to read CHDBOFF register\n");
		return -EIO;
	}

	ret = mhi_init_bw_scale(mhi_cntrl, (mhi_cntrl->regs + chdb_off +
					    (8 * MHI_BW_SCALE_CHAN_DB)));
	if (ret)
		MHI_LOG(dev, "BW scale setup failure\n");

	ret = mhi_init_timesync(mhi_cntrl, (mhi_cntrl->regs + chdb_off +
					    (8 * MHI_TIMESYNC_CHAN_DB)));
	if (ret)
		MHI_LOG(dev, "Time synchronization setup failure\n");

	ret = mhi_init_host_notification(mhi_cntrl, (mhi_cntrl->regs + chdb_off +
						(8 * MHI_HOST_NOTIFY_DB)));
	if (ret)
		MHI_LOG(dev, "Host notification doorbell setup failure\n");

	return 0;
}

int mhi_host_notify_db_disable_trace(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	enum mhi_state state;
	enum mhi_ee_type ee;
	unsigned long pm_lock_flags;

	if (mhi_cntrl->host_notify_db) {
		read_lock_irqsave(&mhi_cntrl->pm_lock, pm_lock_flags);
		if (mhi_cntrl->pm_state == MHI_PM_DISABLE) {
			read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
			return -EINVAL;
		}

		if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state)) {
			read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
			return -EIO;
		}

		state = mhi_get_mhi_state(mhi_cntrl);
		ee = mhi_get_exec_env(mhi_cntrl);

		MHI_VERB(dev, "Entered with MHI state: %s, EE: %s\n",
			mhi_state_str(state),
			TO_MHI_EXEC_STR(ee));

		/* Make sure that we are indeed in M0 state and not in RDDM as well */
		if (state == MHI_STATE_M0 && ee == MHI_EE_AMSS) {
			mhi_write_db(mhi_cntrl, mhi_cntrl->host_notify_db, 1);
			read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
			MHI_LOG(dev, "Host notification DB write Success\n");
			return 0;
		}

		read_unlock_irqrestore(&mhi_cntrl->pm_lock, pm_lock_flags);
		MHI_LOG(dev, "Cannot invoke DB due to invalid M state and/or EE\n");
		return -EPERM;
	}

	MHI_LOG(dev, "Host notifiction DB feature NOT supported or enabled\n");
	return -EPERM;
}
EXPORT_SYMBOL(mhi_host_notify_db_disable_trace);

/* Recycle by fast forwarding WP to the last posted event */
static void mhi_recycle_fwd_ev_ring_element
		(struct mhi_controller *mhi_cntrl, struct mhi_ring *ring)
{
	dma_addr_t ctxt_wp;

	/* update the WP */
	ring->wp += ring->el_size;
	if (ring->wp >= (ring->base + ring->len))
		ring->wp = ring->base;

	/* update the context WP based on the RP to support fast forwarding */
	ctxt_wp = ring->iommu_base + (ring->wp - ring->base);
	*ring->ctxt_wp = ctxt_wp;

	/* update the RP */
	ring->rp += ring->el_size;
	if (ring->rp >= (ring->base + ring->len))
		ring->rp = ring->base;

	/* visible to other cores */
	smp_wmb();
}

/* dedicated bw scale event ring processing */
int mhi_process_misc_tsync_ev_ring(struct mhi_controller *mhi_cntrl,
				   struct mhi_event *mhi_event,
				   u32 event_quota)
{
	struct mhi_ring_element *dev_rp;
	struct mhi_ring *ev_ring = &mhi_event->ring;
	struct mhi_event_ctxt *er_ctxt =
		&mhi_cntrl->mhi_ctxt->er_ctxt[mhi_event->er_index];
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_timesync *mhi_tsync = mhi_priv->timesync;
	u32 sequence;
	u64 remote_time;
	int ret = 0;

	spin_lock_bh(&mhi_event->lock);
	dev_rp = mhi_misc_to_virtual(ev_ring, er_ctxt->rp);
	if (ev_ring->rp == dev_rp) {
		spin_unlock_bh(&mhi_event->lock);
		goto exit_tsync_process;
	}

	/* if rp points to base, we need to wrap it around */
	if (dev_rp == ev_ring->base)
		dev_rp = ev_ring->base + ev_ring->len;
	dev_rp--;

	/* fast forward to currently processed element and recycle er */
	ev_ring->rp = dev_rp;
	ev_ring->wp = dev_rp - 1;
	if (ev_ring->wp < ev_ring->base)
		ev_ring->wp = ev_ring->base + ev_ring->len - ev_ring->el_size;
	mhi_recycle_fwd_ev_ring_element(mhi_cntrl, ev_ring);

	if (WARN_ON(MHI_TRE_GET_EV_TYPE(dev_rp) != MHI_PKT_TYPE_TSYNC_EVENT)) {
		MHI_ERR(dev, "!TIMESYNC event\n");
		ret = -EINVAL;
		spin_unlock_bh(&mhi_event->lock);
		goto exit_tsync_process;
	}

	sequence = MHI_TRE_GET_EV_SEQ(dev_rp);
	remote_time = MHI_TRE_GET_EV_TIME(dev_rp);

	MHI_VERB(dev, "Received TSYNC event with seq: 0x%llx time: 0x%llx\n",
		 sequence, remote_time);

	read_lock_bh(&mhi_cntrl->pm_lock);
	if (likely(MHI_DB_ACCESS_VALID(mhi_cntrl)))
		mhi_ring_er_db(mhi_event);
	read_unlock_bh(&mhi_cntrl->pm_lock);
	spin_unlock_bh(&mhi_event->lock);

	mutex_lock(&mhi_tsync->mutex);

	if (WARN_ON(mhi_tsync->int_sequence != sequence)) {
		MHI_ERR(dev, "Unexpected response: 0x%llx Expected: 0x%llx\n",
			sequence, mhi_tsync->int_sequence);

		mhi_cntrl->runtime_put(mhi_cntrl);
		mhi_device_put(mhi_cntrl->mhi_dev);

		mutex_unlock(&mhi_tsync->mutex);
		ret = -EINVAL;
		goto exit_tsync_process;
	}

	do {
		struct tsync_node *tsync_node;

		spin_lock(&mhi_tsync->lock);
		tsync_node = list_first_entry_or_null(&mhi_tsync->head,
					struct tsync_node, node);
		if (!tsync_node) {
			spin_unlock(&mhi_tsync->lock);
			break;
		}

		list_del(&tsync_node->node);
		spin_unlock(&mhi_tsync->lock);

		tsync_node->cb_func(tsync_node->mhi_dev,
				    tsync_node->sequence,
				    mhi_tsync->local_time, remote_time);
		kfree(tsync_node);
	} while (true);

	mhi_tsync->db_pending = false;
	mhi_tsync->remote_time = remote_time;
	complete(&mhi_tsync->completion);

	mhi_cntrl->runtime_put(mhi_cntrl);
	mhi_device_put(mhi_cntrl->mhi_dev);

	mutex_unlock(&mhi_tsync->mutex);

exit_tsync_process:
	MHI_VERB(dev, "exit er_index: %u, ret: %d\n", mhi_event->er_index, ret);

	return ret;
}

/* dedicated bw scale event ring processing */
int mhi_process_misc_bw_ev_ring(struct mhi_controller *mhi_cntrl,
				struct mhi_event *mhi_event,
				u32 event_quota)
{
	struct mhi_ring_element *dev_rp;
	struct mhi_ring *ev_ring = &mhi_event->ring;
	struct mhi_event_ctxt *er_ctxt =
		&mhi_cntrl->mhi_ctxt->er_ctxt[mhi_event->er_index];
	struct mhi_link_info link_info, *cur_info = &mhi_cntrl->mhi_link_info;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	enum mhi_bw_scale_req_status result = MHI_BW_SCALE_NACK;
	int ret = -EINVAL;

	if (!MHI_IN_MISSION_MODE(mhi_cntrl->ee))
		goto exit_bw_scale_process;

	spin_lock_bh(&mhi_event->lock);
	dev_rp = mhi_misc_to_virtual(ev_ring, er_ctxt->rp);

	/**
	 * Check the ev ring local pointer is same as ctxt pointer
	 * if both are same do not process ev ring.
	 */
	if (ev_ring->rp == dev_rp) {
		MHI_VERB(dev, "Ignore BW event:0x%llx ev_ring RP:0x%llx\n",
			 dev_rp->ptr,
			 (u64)mhi_misc_to_physical(ev_ring, ev_ring->rp));
		spin_unlock_bh(&mhi_event->lock);
		return 0;
	}

	/* if rp points to base, we need to wrap it around */
	if (dev_rp == ev_ring->base)
		dev_rp = ev_ring->base + ev_ring->len;
	dev_rp--;

	/* fast forward to currently processed element and recycle er */
	ev_ring->rp = dev_rp;
	ev_ring->wp = dev_rp - 1;
	if (ev_ring->wp < ev_ring->base)
		ev_ring->wp = ev_ring->base + ev_ring->len - ev_ring->el_size;
	mhi_recycle_fwd_ev_ring_element(mhi_cntrl, ev_ring);

	if (WARN_ON(MHI_TRE_GET_EV_TYPE(dev_rp) != MHI_PKT_TYPE_BW_REQ_EVENT)) {
		MHI_ERR(dev, "!BW SCALE REQ event\n");
		spin_unlock_bh(&mhi_event->lock);
		goto exit_bw_scale_process;
	}

	link_info.target_link_speed = MHI_TRE_GET_EV_LINKSPEED(dev_rp);
	link_info.target_link_width = MHI_TRE_GET_EV_LINKWIDTH(dev_rp);
	link_info.sequence_num = MHI_TRE_GET_EV_BW_REQ_SEQ(dev_rp);

	MHI_VERB(dev, "Received BW_REQ with seq:%d link speed:0x%x width:0x%x\n",
		link_info.sequence_num,
		link_info.target_link_speed,
		link_info.target_link_width);

	read_lock_bh(&mhi_cntrl->pm_lock);
	if (likely(MHI_DB_ACCESS_VALID(mhi_cntrl)))
		mhi_ring_er_db(mhi_event);
	read_unlock_bh(&mhi_cntrl->pm_lock);
	spin_unlock_bh(&mhi_event->lock);

	ret = mhi_device_get_sync(mhi_cntrl->mhi_dev);
	if (ret)
		goto exit_bw_scale_process;
	mhi_cntrl->runtime_get(mhi_cntrl);

	mutex_lock(&mhi_cntrl->pm_mutex);

	ret = mhi_priv->bw_scale(mhi_cntrl, &link_info);
	if (!ret) {
		*cur_info = link_info;
		result = MHI_BW_SCALE_SUCCESS;
	} else if (ret == -EINVAL) {
		result = MHI_BW_SCALE_INVALID;
	}

	write_lock_bh(&mhi_cntrl->pm_lock);
	mhi_priv->bw_response = MHI_BW_SCALE_RESULT(result,
						    link_info.sequence_num);
	if (likely(MHI_DB_ACCESS_VALID(mhi_cntrl))) {
		mhi_write_reg(mhi_cntrl, mhi_priv->bw_scale_db, 0,
			      mhi_priv->bw_response);
		mhi_priv->bw_response = 0;
	} else {
		MHI_VERB(dev, "Cached BW response for seq: %u, result: %d\n",
			 link_info.sequence_num, mhi_priv->bw_response);
	}
	write_unlock_bh(&mhi_cntrl->pm_lock);

	mhi_cntrl->runtime_put(mhi_cntrl);
	mhi_device_put(mhi_cntrl->mhi_dev);

	mutex_unlock(&mhi_cntrl->pm_mutex);

exit_bw_scale_process:
	MHI_VERB(dev, "exit er_index:%u ret:%d\n", mhi_event->er_index, ret);

	return ret;
}

void mhi_misc_dbs_pending(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	if (mhi_priv->bw_scale && mhi_priv->bw_response) {
		mhi_write_reg(mhi_cntrl, mhi_priv->bw_scale_db, 0,
			      mhi_priv->bw_response);
		MHI_VERB(dev, "Completed BW response: %d\n", mhi_priv->bw_response);
		mhi_priv->bw_response = 0;
	}
}

void mhi_controller_set_bw_scale_cb(struct mhi_controller *mhi_cntrl,
			int (*cb_func)(struct mhi_controller *mhi_cntrl,
			struct mhi_link_info *link_info))
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	mhi_priv->bw_scale = cb_func;
}
EXPORT_SYMBOL(mhi_controller_set_bw_scale_cb);

void mhi_controller_set_base(struct mhi_controller *mhi_cntrl, phys_addr_t base)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	mhi_priv->base_addr = base;
}
EXPORT_SYMBOL(mhi_controller_set_base);

int mhi_controller_get_base(struct mhi_controller *mhi_cntrl, phys_addr_t *base)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	if (mhi_priv->base_addr) {
		*base = mhi_priv->base_addr;
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(mhi_controller_get_base);

u32 mhi_controller_get_numeric_id(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	return mhi_priv->numeric_id;
}
EXPORT_SYMBOL(mhi_controller_get_numeric_id);

int mhi_get_channel_db_base(struct mhi_device *mhi_dev, phys_addr_t *value)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	u32 offset;
	int ret;

	if (!MHI_REG_ACCESS_VALID(mhi_cntrl->pm_state))
		return -EIO;

	ret = mhi_read_reg(mhi_cntrl, mhi_cntrl->regs, CHDBOFF,
				 &offset);
	if (ret)
		return -EIO;

	*value = mhi_priv->base_addr + offset;

	return ret;
}
EXPORT_SYMBOL(mhi_get_channel_db_base);

int mhi_get_event_ring_db_base(struct mhi_device *mhi_dev, phys_addr_t *value)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	u32 offset;
	int ret;

	if (!MHI_REG_ACCESS_VALID(mhi_cntrl->pm_state))
		return -EIO;

	ret = mhi_read_reg(mhi_cntrl, mhi_cntrl->regs, ERDBOFF,
				 &offset);
	if (ret)
		return -EIO;

	*value = mhi_priv->base_addr + offset;

	return ret;
}
EXPORT_SYMBOL(mhi_get_event_ring_db_base);

struct mhi_device *mhi_get_device_for_channel(struct mhi_controller *mhi_cntrl,
					      u32 channel)
{
	if (channel >= mhi_cntrl->max_chan)
		return NULL;

	return mhi_cntrl->mhi_chan[channel].mhi_dev;
}
EXPORT_SYMBOL(mhi_get_device_for_channel);

void mhi_controller_set_loglevel(struct mhi_controller *mhi_cntrl,
				 enum MHI_DEBUG_LEVEL lvl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);

	mhi_priv->log_lvl = lvl;
}
EXPORT_SYMBOL(mhi_controller_set_loglevel);

#if !IS_ENABLED(CONFIG_MHI_DTR)
long mhi_device_ioctl(struct mhi_device *mhi_dev, unsigned int cmd,
		      unsigned long arg)
{
	return -EIO;
}
EXPORT_SYMBOL(mhi_device_ioctl);
#endif

int mhi_controller_set_sfr_support(struct mhi_controller *mhi_cntrl, size_t len)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_sfr_info *sfr_info;

	sfr_info = kzalloc(sizeof(*sfr_info), GFP_KERNEL);
	if (!sfr_info)
		return -ENOMEM;

	sfr_info->len = len;
	sfr_info->str = kzalloc(len, GFP_KERNEL);
	if (!sfr_info->str)
		return -ENOMEM;

	mhi_priv->sfr_info = sfr_info;

	return 0;
}
EXPORT_SYMBOL(mhi_controller_set_sfr_support);

void mhi_misc_mission_mode(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_sfr_info *sfr_info = mhi_priv->sfr_info;
	struct mhi_device *dtr_dev;
	u64 local, remote;
	int ret = -EIO;

	/* Attempt to print local and remote SOC time delta for debug */
	ret = mhi_get_remote_time_sync(mhi_cntrl->mhi_dev, &local, &remote);
	if (!ret)
		MHI_LOG(dev, "Timesync: local: %llx, remote: %llx\n", local, remote);

	/* IP_CTRL DTR channel ID */
	dtr_dev = mhi_get_device_for_channel(mhi_cntrl, MHI_DTR_CHANNEL);
	if (dtr_dev)
		mhi_notify(dtr_dev, MHI_CB_DTR_START_CHANNELS);

	/* initialize SFR */
	if (!sfr_info)
		return;

	/* do a clean-up if we reach here post SSR */
	memset(sfr_info->str, 0, sfr_info->len);

	sfr_info->buf_addr = dma_alloc_coherent(mhi_cntrl->cntrl_dev,
						sfr_info->len,
						&sfr_info->dma_addr,
						GFP_KERNEL);
	if (!sfr_info->buf_addr) {
		MHI_ERR(dev, "Failed to allocate memory for sfr\n");
		return;
	}

	init_completion(&sfr_info->completion);

	ret = mhi_send_cmd(mhi_cntrl, NULL, MHI_CMD_SFR_CFG);
	if (ret) {
		MHI_ERR(dev, "Failed to send sfr cfg cmd\n");
		return;
	}

	ret = wait_for_completion_timeout(&sfr_info->completion,
			msecs_to_jiffies(mhi_cntrl->timeout_ms));
	if (!ret || sfr_info->ccs != MHI_EV_CC_SUCCESS)
		MHI_ERR(dev, "Failed to get sfr cfg cmd completion\n");
}

void mhi_misc_disable(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_sfr_info *sfr_info = mhi_priv->sfr_info;

	if (sfr_info && sfr_info->buf_addr) {
		dma_free_coherent(mhi_cntrl->cntrl_dev, sfr_info->len,
				  sfr_info->buf_addr, sfr_info->dma_addr);
		sfr_info->buf_addr = NULL;
	}
}

void mhi_misc_cmd_configure(struct mhi_controller *mhi_cntrl, unsigned int type,
			    u64 *ptr, u32 *dword0, u32 *dword1)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_sfr_info *sfr_info = mhi_priv->sfr_info;

	if (type == MHI_CMD_SFR_CFG && sfr_info) {
		*ptr = MHI_TRE_CMD_SFR_CFG_PTR(sfr_info->dma_addr);
		*dword0 = MHI_TRE_CMD_SFR_CFG_DWORD0(sfr_info->len - 1);
		*dword1 = MHI_TRE_CMD_SFR_CFG_DWORD1;
	}
}

void mhi_misc_cmd_completion(struct mhi_controller *mhi_cntrl,
			     unsigned int type, unsigned int ccs)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_sfr_info *sfr_info = mhi_priv->sfr_info;

	if (type == MHI_CMD_SFR_CFG && sfr_info) {
		sfr_info->ccs = ccs;
		complete(&sfr_info->completion);
	}
}

int mhi_get_remote_time_sync(struct mhi_device *mhi_dev,
			     u64 *t_host,
			     u64 *t_dev)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_timesync *mhi_tsync = mhi_priv->timesync;
	u64 local_time;
	u32 tdev_lo = U32_MAX, tdev_hi = U32_MAX;
	int ret;

	/* not all devices support time features */
	if (!mhi_tsync)
		return -EINVAL;

	if (unlikely(MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state))) {
		MHI_ERR(dev, "MHI is not in active state, pm_state:%s\n",
			to_mhi_pm_state_str(mhi_cntrl->pm_state));
		return -EIO;
	}

	mutex_lock(&mhi_tsync->mutex);

	/* return times from last async request completion */
	if (mhi_tsync->db_pending) {
		local_time = mhi_tsync->local_time;
		mutex_unlock(&mhi_tsync->mutex);

		ret = wait_for_completion_timeout(&mhi_tsync->completion,
				       msecs_to_jiffies(mhi_cntrl->timeout_ms));
		if (MHI_PM_IN_ERROR_STATE(mhi_cntrl->pm_state) || !ret) {
			MHI_ERR(dev, "Pending DB request did not complete, abort\n");
			return -EAGAIN;
		}

		*t_host = local_time;
		*t_dev = mhi_tsync->remote_time;

		return 0;
	}

	/* bring to M0 state */
	ret = mhi_device_get_sync(mhi_cntrl->mhi_dev);
	if (ret)
		goto error_unlock;
	mhi_cntrl->runtime_get(mhi_cntrl);

	/* disable link level low power modes */
	ret = mhi_tsync->lpm_disable(mhi_cntrl);
	if (ret)
		goto error_invalid_state;

	/*
	 * time critical code to fetch device times,
	 * delay between these two steps should be
	 * deterministic as possible.
	 */
	preempt_disable();
	local_irq_disable();

	ret = mhi_read_reg(mhi_cntrl, mhi_tsync->time_reg,
			   TIMESYNC_TIME_HIGH_OFFSET, &tdev_hi);
	if (ret)
		MHI_ERR(dev, "Time HIGH register read error\n");

	ret = mhi_read_reg(mhi_cntrl, mhi_tsync->time_reg,
			   TIMESYNC_TIME_LOW_OFFSET, &tdev_lo);
	if (ret)
		MHI_ERR(dev, "Time LOW register read error\n");

	ret = mhi_read_reg(mhi_cntrl, mhi_tsync->time_reg,
			   TIMESYNC_TIME_HIGH_OFFSET, &tdev_hi);
	if (ret)
		MHI_ERR(dev, "Time HIGH register read error\n");

	*t_dev = (u64) tdev_hi << 32 | tdev_lo;
	*t_host = mhi_tsync->time_get(mhi_cntrl);

	local_irq_enable();
	preempt_enable();

	mhi_tsync->lpm_enable(mhi_cntrl);

error_invalid_state:
	mhi_cntrl->runtime_put(mhi_cntrl);
	mhi_device_put(mhi_cntrl->mhi_dev);
error_unlock:
	mutex_unlock(&mhi_tsync->mutex);
	return ret;
}
EXPORT_SYMBOL(mhi_get_remote_time_sync);

int mhi_get_remote_time(struct mhi_device *mhi_dev,
			u32 sequence,
			void (*cb_func)(struct mhi_device *mhi_dev,
					u32 sequence,
					u64 local_time,
					u64 remote_time))
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_private *mhi_priv = dev_get_drvdata(dev);
	struct mhi_timesync *mhi_tsync = mhi_priv->timesync;
	struct tsync_node *tsync_node;
	int ret = 0;

	/* not all devices support all time features */
	if (!mhi_tsync || !mhi_tsync->time_db)
		return -EINVAL;

	mutex_lock(&mhi_tsync->mutex);

	ret = mhi_device_get_sync(mhi_cntrl->mhi_dev);
	if (ret)
		goto error_unlock;
	mhi_cntrl->runtime_get(mhi_cntrl);

	MHI_LOG(dev, "Enter with pm_state:%s MHI_STATE:%s\n",
		 to_mhi_pm_state_str(mhi_cntrl->pm_state),
		 mhi_state_str(mhi_cntrl->dev_state));

	/*
	 * technically we can use GFP_KERNEL, but wants to avoid
	 * # of times scheduling out
	 */
	tsync_node = kzalloc(sizeof(*tsync_node), GFP_ATOMIC);
	if (!tsync_node) {
		ret = -ENOMEM;
		goto error_no_mem;
	}

	tsync_node->sequence = sequence;
	tsync_node->cb_func = cb_func;
	tsync_node->mhi_dev = mhi_dev;

	if (mhi_tsync->db_pending) {
		mhi_cntrl->runtime_put(mhi_cntrl);
		mhi_device_put(mhi_cntrl->mhi_dev);
		goto skip_tsync_db;
	}

	mhi_tsync->int_sequence++;
	if (mhi_tsync->int_sequence == 0xFFFFFFFF)
		mhi_tsync->int_sequence = 0;

	/* disable link level low power modes */
	ret = mhi_tsync->lpm_disable(mhi_cntrl);
	if (ret) {
		MHI_ERR(dev, "LPM disable request failed for %s!\n", mhi_dev->name);
		goto error_invalid_state;
	}

	/*
	 * time critical code, delay between these two steps should be
	 * deterministic as possible.
	 */
	preempt_disable();
	local_irq_disable();

	mhi_tsync->local_time = mhi_tsync->time_get(mhi_cntrl);
	mhi_write_reg(mhi_cntrl, mhi_tsync->time_db, 0, mhi_tsync->int_sequence);

	/* write must go through immediately */
	wmb();

	local_irq_enable();
	preempt_enable();

	mhi_tsync->lpm_enable(mhi_cntrl);

	MHI_VERB(dev, "time DB request with seq:0x%llx\n", mhi_tsync->int_sequence);

	mhi_tsync->db_pending = true;
	init_completion(&mhi_tsync->completion);

skip_tsync_db:
	spin_lock(&mhi_tsync->lock);
	list_add_tail(&tsync_node->node, &mhi_tsync->head);
	spin_unlock(&mhi_tsync->lock);

	mutex_unlock(&mhi_tsync->mutex);

	return 0;

error_invalid_state:
	kfree(tsync_node);
error_no_mem:
	mhi_cntrl->runtime_put(mhi_cntrl);
	mhi_device_put(mhi_cntrl->mhi_dev);
error_unlock:
	mutex_unlock(&mhi_tsync->mutex);
	return ret;
}
EXPORT_SYMBOL(mhi_get_remote_time);

/* MHI host reset request*/
int mhi_force_reset(struct mhi_controller *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;

	MHI_VERB(dev, "Entered with pm_state:%s dev_state:%s ee:%s\n",
		 to_mhi_pm_state_str(mhi_cntrl->pm_state),
		 mhi_state_str(mhi_cntrl->dev_state),
		 TO_MHI_EXEC_STR(mhi_cntrl->ee));

	/* notify critical clients in absence of RDDM */
	mhi_report_error(mhi_cntrl);

	mhi_soc_reset(mhi_cntrl);
	return mhi_rddm_download_status(mhi_cntrl);
}
EXPORT_SYMBOL(mhi_force_reset);

/* Get SoC info before registering mhi controller */
int mhi_get_soc_info(struct mhi_controller *mhi_cntrl)
{
	u32 soc_info;
	int ret;

	/* Read the MHI device info */
	ret = mhi_read_reg(mhi_cntrl, mhi_cntrl->regs,
			   SOC_HW_VERSION_OFFS, &soc_info);
	if (ret)
		goto done;

	mhi_cntrl->family_number = FIELD_GET(SOC_HW_VERSION_FAM_NUM_BMSK, soc_info);
	mhi_cntrl->device_number = FIELD_GET(SOC_HW_VERSION_DEV_NUM_BMSK, soc_info);
	mhi_cntrl->major_version = FIELD_GET(SOC_HW_VERSION_MAJOR_VER_BMSK, soc_info);
	mhi_cntrl->minor_version = FIELD_GET(SOC_HW_VERSION_MINOR_VER_BMSK, soc_info);

done:
	return ret;
}
EXPORT_SYMBOL(mhi_get_soc_info);
