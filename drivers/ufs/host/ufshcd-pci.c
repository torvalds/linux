// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Flash Storage Host controller PCI glue driver
 *
 * Copyright (C) 2011-2013 Samsung India Software Operations
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 */

#include <ufs/ufshcd.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/debugfs.h>
#include <linux/uuid.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>

#define MAX_SUPP_MAC 64

struct ufs_host {
	void (*late_init)(struct ufs_hba *hba);
};

enum intel_ufs_dsm_func_id {
	INTEL_DSM_FNS		=  0,
	INTEL_DSM_RESET		=  1,
};

struct intel_host {
	struct ufs_host ufs_host;
	u32		dsm_fns;
	u32		active_ltr;
	u32		idle_ltr;
	struct dentry	*debugfs_root;
	struct gpio_desc *reset_gpio;
};

static const guid_t intel_dsm_guid =
	GUID_INIT(0x1A4832A0, 0x7D03, 0x43CA,
		  0xB0, 0x20, 0xF6, 0xDC, 0xD1, 0x2A, 0x19, 0x50);

static bool __intel_dsm_supported(struct intel_host *host,
				  enum intel_ufs_dsm_func_id fn)
{
	return fn < 32 && fn >= 0 && (host->dsm_fns & (1u << fn));
}

#define INTEL_DSM_SUPPORTED(host, name) \
	__intel_dsm_supported(host, INTEL_DSM_##name)

static int __intel_dsm(struct intel_host *intel_host, struct device *dev,
		       unsigned int fn, u32 *result)
{
	union acpi_object *obj;
	int err = 0;
	size_t len;

	obj = acpi_evaluate_dsm_typed(ACPI_HANDLE(dev), &intel_dsm_guid, 0, fn, NULL,
				      ACPI_TYPE_BUFFER);
	if (!obj)
		return -EOPNOTSUPP;

	if (obj->buffer.length < 1) {
		err = -EINVAL;
		goto out;
	}

	len = min_t(size_t, obj->buffer.length, 4);

	*result = 0;
	memcpy(result, obj->buffer.pointer, len);
out:
	ACPI_FREE(obj);

	return err;
}

static int intel_dsm(struct intel_host *intel_host, struct device *dev,
		     unsigned int fn, u32 *result)
{
	if (!__intel_dsm_supported(intel_host, fn))
		return -EOPNOTSUPP;

	return __intel_dsm(intel_host, dev, fn, result);
}

static void intel_dsm_init(struct intel_host *intel_host, struct device *dev)
{
	int err;

	err = __intel_dsm(intel_host, dev, INTEL_DSM_FNS, &intel_host->dsm_fns);
	dev_dbg(dev, "DSM fns %#x, error %d\n", intel_host->dsm_fns, err);
}

static int ufs_intel_hce_enable_notify(struct ufs_hba *hba,
				       enum ufs_notify_change_status status)
{
	/* Cannot enable ICE until after HC enable */
	if (status == POST_CHANGE && hba->caps & UFSHCD_CAP_CRYPTO) {
		u32 hce = ufshcd_readl(hba, REG_CONTROLLER_ENABLE);

		hce |= CRYPTO_GENERAL_ENABLE;
		ufshcd_writel(hba, hce, REG_CONTROLLER_ENABLE);
	}

	return 0;
}

static int ufs_intel_disable_lcc(struct ufs_hba *hba)
{
	u32 attr = UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE);
	u32 lcc_enable = 0;

	ufshcd_dme_get(hba, attr, &lcc_enable);
	if (lcc_enable)
		ufshcd_disable_host_tx_lcc(hba);

	return 0;
}

static int ufs_intel_link_startup_notify(struct ufs_hba *hba,
					 enum ufs_notify_change_status status)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		err = ufs_intel_disable_lcc(hba);
		break;
	case POST_CHANGE:
		break;
	default:
		break;
	}

	return err;
}

static int ufs_intel_set_lanes(struct ufs_hba *hba, u32 lanes)
{
	struct ufs_pa_layer_attr pwr_info = hba->pwr_info;
	int ret;

	pwr_info.lane_rx = lanes;
	pwr_info.lane_tx = lanes;
	ret = ufshcd_config_pwr_mode(hba, &pwr_info);
	if (ret)
		dev_err(hba->dev, "%s: Setting %u lanes, err = %d\n",
			__func__, lanes, ret);
	return ret;
}

static int ufs_intel_lkf_pwr_change_notify(struct ufs_hba *hba,
				enum ufs_notify_change_status status,
				const struct ufs_pa_layer_attr *dev_max_params,
				struct ufs_pa_layer_attr *dev_req_params)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		if (ufshcd_is_hs_mode(dev_max_params) &&
		    (hba->pwr_info.lane_rx != 2 || hba->pwr_info.lane_tx != 2))
			ufs_intel_set_lanes(hba, 2);
		memcpy(dev_req_params, dev_max_params, sizeof(*dev_req_params));
		break;
	case POST_CHANGE:
		if (ufshcd_is_hs_mode(dev_req_params)) {
			u32 peer_granularity;

			usleep_range(1000, 1250);
			err = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY),
						  &peer_granularity);
		}
		break;
	default:
		break;
	}

	return err;
}

static int ufs_intel_lkf_apply_dev_quirks(struct ufs_hba *hba)
{
	u32 granularity, peer_granularity;
	u32 pa_tactivate, peer_pa_tactivate;
	int ret;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &granularity);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &peer_granularity);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &pa_tactivate);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &peer_pa_tactivate);
	if (ret)
		goto out;

	if (granularity == peer_granularity) {
		u32 new_peer_pa_tactivate = pa_tactivate + 2;

		ret = ufshcd_dme_peer_set(hba, UIC_ARG_MIB(PA_TACTIVATE), new_peer_pa_tactivate);
	}
out:
	return ret;
}

#define INTEL_ACTIVELTR		0x804
#define INTEL_IDLELTR		0x808

#define INTEL_LTR_REQ		BIT(15)
#define INTEL_LTR_SCALE_MASK	GENMASK(11, 10)
#define INTEL_LTR_SCALE_1US	(2 << 10)
#define INTEL_LTR_SCALE_32US	(3 << 10)
#define INTEL_LTR_VALUE_MASK	GENMASK(9, 0)

static void intel_cache_ltr(struct ufs_hba *hba)
{
	struct intel_host *host = ufshcd_get_variant(hba);

	host->active_ltr = readl(hba->mmio_base + INTEL_ACTIVELTR);
	host->idle_ltr = readl(hba->mmio_base + INTEL_IDLELTR);
}

static void intel_ltr_set(struct device *dev, s32 val)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct intel_host *host = ufshcd_get_variant(hba);
	u32 ltr;

	pm_runtime_get_sync(dev);

	/*
	 * Program latency tolerance (LTR) accordingly what has been asked
	 * by the PM QoS layer or disable it in case we were passed
	 * negative value or PM_QOS_LATENCY_ANY.
	 */
	ltr = readl(hba->mmio_base + INTEL_ACTIVELTR);

	if (val == PM_QOS_LATENCY_ANY || val < 0) {
		ltr &= ~INTEL_LTR_REQ;
	} else {
		ltr |= INTEL_LTR_REQ;
		ltr &= ~INTEL_LTR_SCALE_MASK;
		ltr &= ~INTEL_LTR_VALUE_MASK;

		if (val > INTEL_LTR_VALUE_MASK) {
			val >>= 5;
			if (val > INTEL_LTR_VALUE_MASK)
				val = INTEL_LTR_VALUE_MASK;
			ltr |= INTEL_LTR_SCALE_32US | val;
		} else {
			ltr |= INTEL_LTR_SCALE_1US | val;
		}
	}

	if (ltr == host->active_ltr)
		goto out;

	writel(ltr, hba->mmio_base + INTEL_ACTIVELTR);
	writel(ltr, hba->mmio_base + INTEL_IDLELTR);

	/* Cache the values into intel_host structure */
	intel_cache_ltr(hba);
out:
	pm_runtime_put(dev);
}

static void intel_ltr_expose(struct device *dev)
{
	dev->power.set_latency_tolerance = intel_ltr_set;
	dev_pm_qos_expose_latency_tolerance(dev);
}

static void intel_ltr_hide(struct device *dev)
{
	dev_pm_qos_hide_latency_tolerance(dev);
	dev->power.set_latency_tolerance = NULL;
}

static void intel_add_debugfs(struct ufs_hba *hba)
{
	struct dentry *dir = debugfs_create_dir(dev_name(hba->dev), NULL);
	struct intel_host *host = ufshcd_get_variant(hba);

	intel_cache_ltr(hba);

	host->debugfs_root = dir;
	debugfs_create_x32("active_ltr", 0444, dir, &host->active_ltr);
	debugfs_create_x32("idle_ltr", 0444, dir, &host->idle_ltr);
}

static void intel_remove_debugfs(struct ufs_hba *hba)
{
	struct intel_host *host = ufshcd_get_variant(hba);

	debugfs_remove_recursive(host->debugfs_root);
}

static int ufs_intel_device_reset(struct ufs_hba *hba)
{
	struct intel_host *host = ufshcd_get_variant(hba);

	if (INTEL_DSM_SUPPORTED(host, RESET)) {
		u32 result = 0;
		int err;

		err = intel_dsm(host, hba->dev, INTEL_DSM_RESET, &result);
		if (!err && !result)
			err = -EIO;
		if (err)
			dev_err(hba->dev, "%s: DSM error %d result %u\n",
				__func__, err, result);
		return err;
	}

	if (!host->reset_gpio)
		return -EOPNOTSUPP;

	gpiod_set_value_cansleep(host->reset_gpio, 1);
	usleep_range(10, 15);

	gpiod_set_value_cansleep(host->reset_gpio, 0);
	usleep_range(10, 15);

	return 0;
}

static struct gpio_desc *ufs_intel_get_reset_gpio(struct device *dev)
{
	/* GPIO in _DSD has active low setting */
	return devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
}

static int ufs_intel_common_init(struct ufs_hba *hba)
{
	struct intel_host *host;

	hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND;

	host = devm_kzalloc(hba->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;
	ufshcd_set_variant(hba, host);
	intel_dsm_init(host, hba->dev);
	if (INTEL_DSM_SUPPORTED(host, RESET)) {
		if (hba->vops->device_reset)
			hba->caps |= UFSHCD_CAP_DEEPSLEEP;
	} else {
		if (hba->vops->device_reset)
			host->reset_gpio = ufs_intel_get_reset_gpio(hba->dev);
		if (IS_ERR(host->reset_gpio)) {
			dev_err(hba->dev, "%s: failed to get reset GPIO, error %ld\n",
				__func__, PTR_ERR(host->reset_gpio));
			host->reset_gpio = NULL;
		}
		if (host->reset_gpio) {
			gpiod_set_value_cansleep(host->reset_gpio, 0);
			hba->caps |= UFSHCD_CAP_DEEPSLEEP;
		}
	}
	intel_ltr_expose(hba->dev);
	intel_add_debugfs(hba);
	return 0;
}

static void ufs_intel_common_exit(struct ufs_hba *hba)
{
	intel_remove_debugfs(hba);
	intel_ltr_hide(hba->dev);
}

static int ufs_intel_resume(struct ufs_hba *hba, enum ufs_pm_op op)
{
	if (ufshcd_is_link_hibern8(hba)) {
		int ret = ufshcd_uic_hibern8_exit(hba);

		if (!ret) {
			ufshcd_set_link_active(hba);
		} else {
			dev_err(hba->dev, "%s: hibern8 exit failed %d\n",
				__func__, ret);
			/*
			 * Force reset and restore. Any other actions can lead
			 * to an unrecoverable state.
			 */
			ufshcd_set_link_off(hba);
		}
	}

	return 0;
}

static int ufs_intel_ehl_init(struct ufs_hba *hba)
{
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;
	return ufs_intel_common_init(hba);
}

static void ufs_intel_lkf_late_init(struct ufs_hba *hba)
{
	/* LKF always needs a full reset, so set PM accordingly */
	if (hba->caps & UFSHCD_CAP_DEEPSLEEP) {
		hba->spm_lvl = UFS_PM_LVL_6;
		hba->rpm_lvl = UFS_PM_LVL_6;
	} else {
		hba->spm_lvl = UFS_PM_LVL_5;
		hba->rpm_lvl = UFS_PM_LVL_5;
	}
}

static int ufs_intel_lkf_init(struct ufs_hba *hba)
{
	struct ufs_host *ufs_host;
	int err;

	hba->nop_out_timeout = 200;
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;
	hba->caps |= UFSHCD_CAP_CRYPTO;
	err = ufs_intel_common_init(hba);
	ufs_host = ufshcd_get_variant(hba);
	ufs_host->late_init = ufs_intel_lkf_late_init;
	return err;
}

static int ufs_intel_adl_init(struct ufs_hba *hba)
{
	hba->nop_out_timeout = 200;
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;
	hba->caps |= UFSHCD_CAP_WB_EN;
	return ufs_intel_common_init(hba);
}

static int ufs_intel_mtl_init(struct ufs_hba *hba)
{
	hba->caps |= UFSHCD_CAP_CRYPTO | UFSHCD_CAP_WB_EN;
	return ufs_intel_common_init(hba);
}

static int ufs_qemu_get_hba_mac(struct ufs_hba *hba)
{
	return MAX_SUPP_MAC;
}

static int ufs_qemu_mcq_config_resource(struct ufs_hba *hba)
{
	hba->mcq_base = hba->mmio_base + ufshcd_mcq_queue_cfg_addr(hba);

	return 0;
}

static int ufs_qemu_op_runtime_config(struct ufs_hba *hba)
{
	struct ufshcd_mcq_opr_info_t *opr;
	int i;

	u32 sqdao = ufsmcq_readl(hba, ufshcd_mcq_cfg_offset(REG_SQDAO, 0));
	u32 sqisao = ufsmcq_readl(hba, ufshcd_mcq_cfg_offset(REG_SQISAO, 0));
	u32 cqdao = ufsmcq_readl(hba, ufshcd_mcq_cfg_offset(REG_CQDAO, 0));
	u32 cqisao = ufsmcq_readl(hba, ufshcd_mcq_cfg_offset(REG_CQISAO, 0));

	hba->mcq_opr[OPR_SQD].offset = sqdao;
	hba->mcq_opr[OPR_SQIS].offset = sqisao;
	hba->mcq_opr[OPR_CQD].offset = cqdao;
	hba->mcq_opr[OPR_CQIS].offset = cqisao;

	for (i = 0; i < OPR_MAX; i++) {
		opr = &hba->mcq_opr[i];
		opr->stride = 48;
		opr->base = hba->mmio_base + opr->offset;
	}

	return 0;
}

static struct ufs_hba_variant_ops ufs_qemu_hba_vops = {
	.name                   = "qemu-pci",
	.get_hba_mac		= ufs_qemu_get_hba_mac,
	.mcq_config_resource	= ufs_qemu_mcq_config_resource,
	.op_runtime_config	= ufs_qemu_op_runtime_config,
};

static struct ufs_hba_variant_ops ufs_intel_cnl_hba_vops = {
	.name                   = "intel-pci",
	.init			= ufs_intel_common_init,
	.exit			= ufs_intel_common_exit,
	.link_startup_notify	= ufs_intel_link_startup_notify,
	.resume			= ufs_intel_resume,
};

static struct ufs_hba_variant_ops ufs_intel_ehl_hba_vops = {
	.name                   = "intel-pci",
	.init			= ufs_intel_ehl_init,
	.exit			= ufs_intel_common_exit,
	.link_startup_notify	= ufs_intel_link_startup_notify,
	.resume			= ufs_intel_resume,
};

static struct ufs_hba_variant_ops ufs_intel_lkf_hba_vops = {
	.name                   = "intel-pci",
	.init			= ufs_intel_lkf_init,
	.exit			= ufs_intel_common_exit,
	.hce_enable_notify	= ufs_intel_hce_enable_notify,
	.link_startup_notify	= ufs_intel_link_startup_notify,
	.pwr_change_notify	= ufs_intel_lkf_pwr_change_notify,
	.apply_dev_quirks	= ufs_intel_lkf_apply_dev_quirks,
	.resume			= ufs_intel_resume,
	.device_reset		= ufs_intel_device_reset,
};

static struct ufs_hba_variant_ops ufs_intel_adl_hba_vops = {
	.name			= "intel-pci",
	.init			= ufs_intel_adl_init,
	.exit			= ufs_intel_common_exit,
	.link_startup_notify	= ufs_intel_link_startup_notify,
	.resume			= ufs_intel_resume,
	.device_reset		= ufs_intel_device_reset,
};

static struct ufs_hba_variant_ops ufs_intel_mtl_hba_vops = {
	.name                   = "intel-pci",
	.init			= ufs_intel_mtl_init,
	.exit			= ufs_intel_common_exit,
	.hce_enable_notify	= ufs_intel_hce_enable_notify,
	.link_startup_notify	= ufs_intel_link_startup_notify,
	.resume			= ufs_intel_resume,
	.device_reset		= ufs_intel_device_reset,
};

#ifdef CONFIG_PM_SLEEP
static int ufshcd_pci_restore(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	/* Force a full reset and restore */
	ufshcd_set_link_off(hba);

	return ufshcd_system_resume(dev);
}
#endif

/**
 * ufshcd_pci_remove - de-allocate PCI/SCSI host and host memory space
 *		data structure memory
 * @pdev: pointer to PCI handle
 */
static void ufshcd_pci_remove(struct pci_dev *pdev)
{
	struct ufs_hba *hba = pci_get_drvdata(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	ufshcd_remove(hba);
}

/**
 * ufshcd_pci_probe - probe routine of the driver
 * @pdev: pointer to PCI device handle
 * @id: PCI device id
 *
 * Return: 0 on success, non-zero value on failure.
 */
static int
ufshcd_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ufs_host *ufs_host;
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	int err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pcim_enable_device failed\n");
		return err;
	}

	pci_set_master(pdev);

	mmio_base = pcim_iomap_region(pdev, 0, UFSHCD);
	if (IS_ERR(mmio_base)) {
		dev_err(&pdev->dev, "request and iomap failed\n");
		return PTR_ERR(mmio_base);
	}

	err = ufshcd_alloc_host(&pdev->dev, &hba);
	if (err) {
		dev_err(&pdev->dev, "Allocation failed\n");
		return err;
	}

	hba->vops = (struct ufs_hba_variant_ops *)id->driver_data;

	err = ufshcd_init(hba, mmio_base, pdev->irq);
	if (err) {
		dev_err(&pdev->dev, "Initialization failed\n");
		return err;
	}

	ufs_host = ufshcd_get_variant(hba);
	if (ufs_host && ufs_host->late_init)
		ufs_host->late_init(hba);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops ufshcd_pci_pm_ops = {
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
#ifdef CONFIG_PM_SLEEP
	.suspend	= ufshcd_system_suspend,
	.resume		= ufshcd_system_resume,
	.freeze		= ufshcd_system_suspend,
	.thaw		= ufshcd_system_resume,
	.poweroff	= ufshcd_system_suspend,
	.restore	= ufshcd_pci_restore,
	.prepare	= ufshcd_suspend_prepare,
	.complete	= ufshcd_resume_complete,
#endif
};

static const struct pci_device_id ufshcd_pci_tbl[] = {
	{ PCI_VENDOR_ID_REDHAT, 0x0013, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		(kernel_ulong_t)&ufs_qemu_hba_vops },
	{ PCI_VENDOR_ID_SAMSUNG, 0xC00C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VDEVICE(INTEL, 0x9DFA), (kernel_ulong_t)&ufs_intel_cnl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x4B41), (kernel_ulong_t)&ufs_intel_ehl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x4B43), (kernel_ulong_t)&ufs_intel_ehl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x98FA), (kernel_ulong_t)&ufs_intel_lkf_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x51FF), (kernel_ulong_t)&ufs_intel_adl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x54FF), (kernel_ulong_t)&ufs_intel_adl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x7E47), (kernel_ulong_t)&ufs_intel_mtl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0xA847), (kernel_ulong_t)&ufs_intel_mtl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0x7747), (kernel_ulong_t)&ufs_intel_mtl_hba_vops },
	{ PCI_VDEVICE(INTEL, 0xE447), (kernel_ulong_t)&ufs_intel_mtl_hba_vops },
	{ }	/* terminate list */
};

MODULE_DEVICE_TABLE(pci, ufshcd_pci_tbl);

static struct pci_driver ufshcd_pci_driver = {
	.name = UFSHCD,
	.id_table = ufshcd_pci_tbl,
	.probe = ufshcd_pci_probe,
	.remove = ufshcd_pci_remove,
	.driver = {
		.pm = &ufshcd_pci_pm_ops
	},
};

module_pci_driver(ufshcd_pci_driver);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("UFS host controller PCI glue driver");
MODULE_LICENSE("GPL");
