// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Express Precision Time Measurement
 * Copyright (c) 2016, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include "../pci.h"

/*
 * If the next upstream device supports PTM, return it; otherwise return
 * NULL.  PTM Messages are local, so both link partners must support it.
 */
static struct pci_dev *pci_upstream_ptm(struct pci_dev *dev)
{
	struct pci_dev *ups = pci_upstream_bridge(dev);

	/*
	 * Switch Downstream Ports are not permitted to have a PTM
	 * capability; their PTM behavior is controlled by the Upstream
	 * Port (PCIe r5.0, sec 7.9.16), so if the upstream bridge is a
	 * Switch Downstream Port, look up one more level.
	 */
	if (ups && pci_pcie_type(ups) == PCI_EXP_TYPE_DOWNSTREAM)
		ups = pci_upstream_bridge(ups);

	if (ups && ups->ptm_cap)
		return ups;

	return NULL;
}

/*
 * Find the PTM Capability (if present) and extract the information we need
 * to use it.
 */
void pci_ptm_init(struct pci_dev *dev)
{
	u16 ptm;
	u32 cap;
	struct pci_dev *ups;

	if (!pci_is_pcie(dev))
		return;

	ptm = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_PTM);
	if (!ptm)
		return;

	dev->ptm_cap = ptm;
	pci_add_ext_cap_save_buffer(dev, PCI_EXT_CAP_ID_PTM, sizeof(u32));

	pci_read_config_dword(dev, ptm + PCI_PTM_CAP, &cap);
	dev->ptm_granularity = FIELD_GET(PCI_PTM_GRANULARITY_MASK, cap);

	/*
	 * Per the spec recommendation (PCIe r6.0, sec 7.9.15.3), select the
	 * furthest upstream Time Source as the PTM Root.  For Endpoints,
	 * "the Effective Granularity is the maximum Local Clock Granularity
	 * reported by the PTM Root and all intervening PTM Time Sources."
	 */
	ups = pci_upstream_ptm(dev);
	if (ups) {
		if (ups->ptm_granularity == 0)
			dev->ptm_granularity = 0;
		else if (ups->ptm_granularity > dev->ptm_granularity)
			dev->ptm_granularity = ups->ptm_granularity;
	} else if (cap & PCI_PTM_CAP_ROOT) {
		dev->ptm_root = 1;
	} else if (pci_pcie_type(dev) == PCI_EXP_TYPE_RC_END) {

		/*
		 * Per sec 7.9.15.3, this should be the Local Clock
		 * Granularity of the associated Time Source.  But it
		 * doesn't say how to find that Time Source.
		 */
		dev->ptm_granularity = 0;
	}

	if (pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT ||
	    pci_pcie_type(dev) == PCI_EXP_TYPE_UPSTREAM)
		pci_enable_ptm(dev, NULL);
}

void pci_save_ptm_state(struct pci_dev *dev)
{
	u16 ptm = dev->ptm_cap;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!ptm)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_PTM);
	if (!save_state)
		return;

	cap = (u32 *)&save_state->cap.data[0];
	pci_read_config_dword(dev, ptm + PCI_PTM_CTRL, cap);
}

void pci_restore_ptm_state(struct pci_dev *dev)
{
	u16 ptm = dev->ptm_cap;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!ptm)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_PTM);
	if (!save_state)
		return;

	cap = (u32 *)&save_state->cap.data[0];
	pci_write_config_dword(dev, ptm + PCI_PTM_CTRL, *cap);
}

/* Enable PTM in the Control register if possible */
static int __pci_enable_ptm(struct pci_dev *dev)
{
	u16 ptm = dev->ptm_cap;
	struct pci_dev *ups;
	u32 ctrl;

	if (!ptm)
		return -EINVAL;

	/*
	 * A device uses local PTM Messages to request time information
	 * from a PTM Root that's farther upstream.  Every device along the
	 * path must support PTM and have it enabled so it can handle the
	 * messages.  Therefore, if this device is not a PTM Root, the
	 * upstream link partner must have PTM enabled before we can enable
	 * PTM.
	 */
	if (!dev->ptm_root) {
		ups = pci_upstream_ptm(dev);
		if (!ups || !ups->ptm_enabled)
			return -EINVAL;
	}

	pci_read_config_dword(dev, ptm + PCI_PTM_CTRL, &ctrl);

	ctrl |= PCI_PTM_CTRL_ENABLE;
	ctrl &= ~PCI_PTM_GRANULARITY_MASK;
	ctrl |= FIELD_PREP(PCI_PTM_GRANULARITY_MASK, dev->ptm_granularity);
	if (dev->ptm_root)
		ctrl |= PCI_PTM_CTRL_ROOT;

	pci_write_config_dword(dev, ptm + PCI_PTM_CTRL, ctrl);
	return 0;
}

/**
 * pci_enable_ptm() - Enable Precision Time Measurement
 * @dev: PCI device
 * @granularity: pointer to return granularity
 *
 * Enable Precision Time Measurement for @dev.  If successful and
 * @granularity is non-NULL, return the Effective Granularity.
 *
 * Return: zero if successful, or -EINVAL if @dev lacks a PTM Capability or
 * is not a PTM Root and lacks an upstream path of PTM-enabled devices.
 */
int pci_enable_ptm(struct pci_dev *dev, u8 *granularity)
{
	int rc;
	char clock_desc[8];

	rc = __pci_enable_ptm(dev);
	if (rc)
		return rc;

	dev->ptm_enabled = 1;

	if (granularity)
		*granularity = dev->ptm_granularity;

	switch (dev->ptm_granularity) {
	case 0:
		snprintf(clock_desc, sizeof(clock_desc), "unknown");
		break;
	case 255:
		snprintf(clock_desc, sizeof(clock_desc), ">254ns");
		break;
	default:
		snprintf(clock_desc, sizeof(clock_desc), "%uns",
			 dev->ptm_granularity);
		break;
	}
	pci_info(dev, "PTM enabled%s, %s granularity\n",
		 dev->ptm_root ? " (root)" : "", clock_desc);

	return 0;
}
EXPORT_SYMBOL(pci_enable_ptm);

static void __pci_disable_ptm(struct pci_dev *dev)
{
	u16 ptm = dev->ptm_cap;
	u32 ctrl;

	if (!ptm)
		return;

	pci_read_config_dword(dev, ptm + PCI_PTM_CTRL, &ctrl);
	ctrl &= ~(PCI_PTM_CTRL_ENABLE | PCI_PTM_CTRL_ROOT);
	pci_write_config_dword(dev, ptm + PCI_PTM_CTRL, ctrl);
}

/**
 * pci_disable_ptm() - Disable Precision Time Measurement
 * @dev: PCI device
 *
 * Disable Precision Time Measurement for @dev.
 */
void pci_disable_ptm(struct pci_dev *dev)
{
	if (dev->ptm_enabled) {
		__pci_disable_ptm(dev);
		dev->ptm_enabled = 0;
	}
}
EXPORT_SYMBOL(pci_disable_ptm);

/*
 * Disable PTM, but preserve dev->ptm_enabled so we silently re-enable it on
 * resume if necessary.
 */
void pci_suspend_ptm(struct pci_dev *dev)
{
	if (dev->ptm_enabled)
		__pci_disable_ptm(dev);
}

/* If PTM was enabled before suspend, re-enable it when resuming */
void pci_resume_ptm(struct pci_dev *dev)
{
	if (dev->ptm_enabled)
		__pci_enable_ptm(dev);
}

bool pcie_ptm_enabled(struct pci_dev *dev)
{
	if (!dev)
		return false;

	return dev->ptm_enabled;
}
EXPORT_SYMBOL(pcie_ptm_enabled);

#if IS_ENABLED(CONFIG_DEBUG_FS)
static ssize_t context_update_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct pci_ptm_debugfs *ptm_debugfs = file->private_data;
	char buf[7];
	int ret;
	u8 mode;

	if (!ptm_debugfs->ops->context_update_write)
		return -EOPNOTSUPP;

	if (count < 1 || count >= sizeof(buf))
		return -EINVAL;

	ret = copy_from_user(buf, ubuf, count);
	if (ret)
		return -EFAULT;

	buf[count] = '\0';

	if (sysfs_streq(buf, "auto"))
		mode = PCIE_PTM_CONTEXT_UPDATE_AUTO;
	else if (sysfs_streq(buf, "manual"))
		mode = PCIE_PTM_CONTEXT_UPDATE_MANUAL;
	else
		return -EINVAL;

	mutex_lock(&ptm_debugfs->lock);
	ret = ptm_debugfs->ops->context_update_write(ptm_debugfs->pdata, mode);
	mutex_unlock(&ptm_debugfs->lock);
	if (ret)
		return ret;

	return count;
}

static ssize_t context_update_read(struct file *file, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct pci_ptm_debugfs *ptm_debugfs = file->private_data;
	char buf[8]; /* Extra space for NULL termination at the end */
	ssize_t pos;
	u8 mode;

	if (!ptm_debugfs->ops->context_update_read)
		return -EOPNOTSUPP;

	mutex_lock(&ptm_debugfs->lock);
	ptm_debugfs->ops->context_update_read(ptm_debugfs->pdata, &mode);
	mutex_unlock(&ptm_debugfs->lock);

	if (mode == PCIE_PTM_CONTEXT_UPDATE_AUTO)
		pos = scnprintf(buf, sizeof(buf), "auto\n");
	else
		pos = scnprintf(buf, sizeof(buf), "manual\n");

	return simple_read_from_buffer(ubuf, count, ppos, buf, pos);
}

static const struct file_operations context_update_fops = {
	.open = simple_open,
	.read = context_update_read,
	.write = context_update_write,
};

static int context_valid_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	bool valid;
	int ret;

	if (!ptm_debugfs->ops->context_valid_read)
		return -EOPNOTSUPP;

	mutex_lock(&ptm_debugfs->lock);
	ret = ptm_debugfs->ops->context_valid_read(ptm_debugfs->pdata, &valid);
	mutex_unlock(&ptm_debugfs->lock);
	if (ret)
		return ret;

	*val = valid;

	return 0;
}

static int context_valid_set(void *data, u64 val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	int ret;

	if (!ptm_debugfs->ops->context_valid_write)
		return -EOPNOTSUPP;

	mutex_lock(&ptm_debugfs->lock);
	ret = ptm_debugfs->ops->context_valid_write(ptm_debugfs->pdata, !!val);
	mutex_unlock(&ptm_debugfs->lock);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(context_valid_fops, context_valid_get,
			 context_valid_set, "%llu\n");

static int local_clock_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->local_clock_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->local_clock_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(local_clock_fops, local_clock_get, NULL, "%llu\n");

static int master_clock_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->master_clock_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->master_clock_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(master_clock_fops, master_clock_get, NULL, "%llu\n");

static int t1_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->t1_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->t1_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(t1_fops, t1_get, NULL, "%llu\n");

static int t2_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->t2_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->t2_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(t2_fops, t2_get, NULL, "%llu\n");

static int t3_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->t3_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->t3_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(t3_fops, t3_get, NULL, "%llu\n");

static int t4_get(void *data, u64 *val)
{
	struct pci_ptm_debugfs *ptm_debugfs = data;
	u64 clock;
	int ret;

	if (!ptm_debugfs->ops->t4_read)
		return -EOPNOTSUPP;

	ret = ptm_debugfs->ops->t4_read(ptm_debugfs->pdata, &clock);
	if (ret)
		return ret;

	*val = clock;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(t4_fops, t4_get, NULL, "%llu\n");

#define pcie_ptm_create_debugfs_file(pdata, mode, attr)			\
	do {								\
		if (ops->attr##_visible && ops->attr##_visible(pdata))	\
			debugfs_create_file(#attr, mode, ptm_debugfs->debugfs, \
					    ptm_debugfs, &attr##_fops);	\
	} while (0)

/*
 * pcie_ptm_create_debugfs() - Create debugfs entries for the PTM context
 * @dev: PTM capable component device
 * @pdata: Private data of the PTM capable component device
 * @ops: PTM callback structure
 *
 * Create debugfs entries for exposing the PTM context of the PTM capable
 * components such as Root Complex and Endpoint controllers.
 *
 * Return: Pointer to 'struct pci_ptm_debugfs' if success, NULL otherwise.
 */
struct pci_ptm_debugfs *pcie_ptm_create_debugfs(struct device *dev, void *pdata,
			  const struct pcie_ptm_ops *ops)
{
	struct pci_ptm_debugfs *ptm_debugfs;
	char *dirname;
	int ret;

	/* Caller must provide check_capability() callback */
	if (!ops->check_capability)
		return NULL;

	/* Check for PTM capability before creating debugfs attrbutes */
	ret = ops->check_capability(pdata);
	if (!ret) {
		dev_dbg(dev, "PTM capability not present\n");
		return NULL;
	}

	ptm_debugfs = kzalloc(sizeof(*ptm_debugfs), GFP_KERNEL);
	if (!ptm_debugfs)
		return NULL;

	dirname = devm_kasprintf(dev, GFP_KERNEL, "pcie_ptm_%s", dev_name(dev));
	if (!dirname)
		return NULL;

	ptm_debugfs->debugfs = debugfs_create_dir(dirname, NULL);
	ptm_debugfs->pdata = pdata;
	ptm_debugfs->ops = ops;
	mutex_init(&ptm_debugfs->lock);

	pcie_ptm_create_debugfs_file(pdata, 0644, context_update);
	pcie_ptm_create_debugfs_file(pdata, 0644, context_valid);
	pcie_ptm_create_debugfs_file(pdata, 0444, local_clock);
	pcie_ptm_create_debugfs_file(pdata, 0444, master_clock);
	pcie_ptm_create_debugfs_file(pdata, 0444, t1);
	pcie_ptm_create_debugfs_file(pdata, 0444, t2);
	pcie_ptm_create_debugfs_file(pdata, 0444, t3);
	pcie_ptm_create_debugfs_file(pdata, 0444, t4);

	return ptm_debugfs;
}
EXPORT_SYMBOL_GPL(pcie_ptm_create_debugfs);

/*
 * pcie_ptm_destroy_debugfs() - Destroy debugfs entries for the PTM context
 * @ptm_debugfs: Pointer to the PTM debugfs struct
 */
void pcie_ptm_destroy_debugfs(struct pci_ptm_debugfs *ptm_debugfs)
{
	if (!ptm_debugfs)
		return;

	mutex_destroy(&ptm_debugfs->lock);
	debugfs_remove_recursive(ptm_debugfs->debugfs);
}
EXPORT_SYMBOL_GPL(pcie_ptm_destroy_debugfs);
#endif
