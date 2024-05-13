// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>

#include <linux/pds/pds_common.h>

#include "core.h"

MODULE_DESCRIPTION(PDSC_DRV_DESCRIPTION);
MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_LICENSE("GPL");

/* Supported devices */
static const struct pci_device_id pdsc_id_table[] = {
	{ PCI_VDEVICE(PENSANDO, PCI_DEVICE_ID_PENSANDO_CORE_PF) },
	{ PCI_VDEVICE(PENSANDO, PCI_DEVICE_ID_PENSANDO_VDPA_VF) },
	{ 0, }	/* end of table */
};
MODULE_DEVICE_TABLE(pci, pdsc_id_table);

static void pdsc_wdtimer_cb(struct timer_list *t)
{
	struct pdsc *pdsc = from_timer(pdsc, t, wdtimer);

	dev_dbg(pdsc->dev, "%s: jiffies %ld\n", __func__, jiffies);
	mod_timer(&pdsc->wdtimer,
		  round_jiffies(jiffies + pdsc->wdtimer_period));

	queue_work(pdsc->wq, &pdsc->health_work);
}

static void pdsc_unmap_bars(struct pdsc *pdsc)
{
	struct pdsc_dev_bar *bars = pdsc->bars;
	unsigned int i;

	pdsc->info_regs = NULL;
	pdsc->cmd_regs = NULL;
	pdsc->intr_status = NULL;
	pdsc->intr_ctrl = NULL;

	for (i = 0; i < PDS_CORE_BARS_MAX; i++) {
		if (bars[i].vaddr)
			pci_iounmap(pdsc->pdev, bars[i].vaddr);
		bars[i].vaddr = NULL;
	}
}

static int pdsc_map_bars(struct pdsc *pdsc)
{
	struct pdsc_dev_bar *bar = pdsc->bars;
	struct pci_dev *pdev = pdsc->pdev;
	struct device *dev = pdsc->dev;
	struct pdsc_dev_bar *bars;
	unsigned int i, j;
	int num_bars = 0;
	int err;
	u32 sig;

	bars = pdsc->bars;

	/* Since the PCI interface in the hardware is configurable,
	 * we need to poke into all the bars to find the set we're
	 * expecting.
	 */
	for (i = 0, j = 0; i < PDS_CORE_BARS_MAX; i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;

		bars[j].len = pci_resource_len(pdev, i);
		bars[j].bus_addr = pci_resource_start(pdev, i);
		bars[j].res_index = i;

		/* only map the whole bar 0 */
		if (j > 0) {
			bars[j].vaddr = NULL;
		} else {
			bars[j].vaddr = pci_iomap(pdev, i, bars[j].len);
			if (!bars[j].vaddr) {
				dev_err(dev, "Cannot map BAR %d, aborting\n", i);
				return -ENODEV;
			}
		}

		j++;
	}
	num_bars = j;

	/* BAR0: dev_cmd and interrupts */
	if (num_bars < 1) {
		dev_err(dev, "No bars found\n");
		err = -EFAULT;
		goto err_out;
	}

	if (bar->len < PDS_CORE_BAR0_SIZE) {
		dev_err(dev, "Resource bar size %lu too small\n", bar->len);
		err = -EFAULT;
		goto err_out;
	}

	pdsc->info_regs = bar->vaddr + PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET;
	pdsc->cmd_regs = bar->vaddr + PDS_CORE_BAR0_DEV_CMD_REGS_OFFSET;
	pdsc->intr_status = bar->vaddr + PDS_CORE_BAR0_INTR_STATUS_OFFSET;
	pdsc->intr_ctrl = bar->vaddr + PDS_CORE_BAR0_INTR_CTRL_OFFSET;

	sig = ioread32(&pdsc->info_regs->signature);
	if (sig != PDS_CORE_DEV_INFO_SIGNATURE) {
		dev_err(dev, "Incompatible firmware signature %x", sig);
		err = -EFAULT;
		goto err_out;
	}

	/* BAR1: doorbells */
	bar++;
	if (num_bars < 2) {
		dev_err(dev, "Doorbell bar missing\n");
		err = -EFAULT;
		goto err_out;
	}

	pdsc->db_pages = bar->vaddr;
	pdsc->phy_db_pages = bar->bus_addr;

	return 0;

err_out:
	pdsc_unmap_bars(pdsc);
	return err;
}

void __iomem *pdsc_map_dbpage(struct pdsc *pdsc, int page_num)
{
	return pci_iomap_range(pdsc->pdev,
			       pdsc->bars[PDS_CORE_PCI_BAR_DBELL].res_index,
			       (u64)page_num << PAGE_SHIFT, PAGE_SIZE);
}

static int pdsc_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct pdsc *pdsc = pci_get_drvdata(pdev);
	struct device *dev = pdsc->dev;
	int ret = 0;

	if (num_vfs > 0) {
		pdsc->vfs = kcalloc(num_vfs, sizeof(struct pdsc_vf),
				    GFP_KERNEL);
		if (!pdsc->vfs)
			return -ENOMEM;
		pdsc->num_vfs = num_vfs;

		ret = pci_enable_sriov(pdev, num_vfs);
		if (ret) {
			dev_err(dev, "Cannot enable SRIOV: %pe\n",
				ERR_PTR(ret));
			goto no_vfs;
		}

		return num_vfs;
	}

no_vfs:
	pci_disable_sriov(pdev);

	kfree(pdsc->vfs);
	pdsc->vfs = NULL;
	pdsc->num_vfs = 0;

	return ret;
}

static int pdsc_init_vf(struct pdsc *vf)
{
	struct devlink *dl;
	struct pdsc *pf;
	int err;

	pf = pdsc_get_pf_struct(vf->pdev);
	if (IS_ERR_OR_NULL(pf))
		return PTR_ERR(pf) ?: -1;

	vf->vf_id = pci_iov_vf_id(vf->pdev);

	dl = priv_to_devlink(vf);
	devl_lock(dl);
	devl_register(dl);
	devl_unlock(dl);

	pf->vfs[vf->vf_id].vf = vf;
	err = pdsc_auxbus_dev_add(vf, pf);
	if (err) {
		devl_lock(dl);
		devl_unregister(dl);
		devl_unlock(dl);
	}

	return err;
}

static const struct devlink_health_reporter_ops pdsc_fw_reporter_ops = {
	.name = "fw",
	.diagnose = pdsc_fw_reporter_diagnose,
};

static const struct devlink_param pdsc_dl_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_VNET,
			      BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			      pdsc_dl_enable_get,
			      pdsc_dl_enable_set,
			      pdsc_dl_enable_validate),
};

#define PDSC_WQ_NAME_LEN 24

static int pdsc_init_pf(struct pdsc *pdsc)
{
	struct devlink_health_reporter *hr;
	char wq_name[PDSC_WQ_NAME_LEN];
	struct devlink *dl;
	int err;

	pcie_print_link_status(pdsc->pdev);

	err = pci_request_regions(pdsc->pdev, PDS_CORE_DRV_NAME);
	if (err) {
		dev_err(pdsc->dev, "Cannot request PCI regions: %pe\n",
			ERR_PTR(err));
		return err;
	}

	err = pdsc_map_bars(pdsc);
	if (err)
		goto err_out_release_regions;

	/* General workqueue and timer, but don't start timer yet */
	snprintf(wq_name, sizeof(wq_name), "%s.%d", PDS_CORE_DRV_NAME, pdsc->uid);
	pdsc->wq = create_singlethread_workqueue(wq_name);
	INIT_WORK(&pdsc->health_work, pdsc_health_thread);
	INIT_WORK(&pdsc->pci_reset_work, pdsc_pci_reset_thread);
	timer_setup(&pdsc->wdtimer, pdsc_wdtimer_cb, 0);
	pdsc->wdtimer_period = PDSC_WATCHDOG_SECS * HZ;

	mutex_init(&pdsc->devcmd_lock);
	mutex_init(&pdsc->config_lock);
	spin_lock_init(&pdsc->adminq_lock);

	mutex_lock(&pdsc->config_lock);
	set_bit(PDSC_S_FW_DEAD, &pdsc->state);

	err = pdsc_setup(pdsc, PDSC_SETUP_INIT);
	if (err) {
		mutex_unlock(&pdsc->config_lock);
		goto err_out_unmap_bars;
	}

	err = pdsc_start(pdsc);
	if (err) {
		mutex_unlock(&pdsc->config_lock);
		goto err_out_teardown;
	}

	mutex_unlock(&pdsc->config_lock);

	dl = priv_to_devlink(pdsc);
	devl_lock(dl);
	err = devl_params_register(dl, pdsc_dl_params,
				   ARRAY_SIZE(pdsc_dl_params));
	if (err) {
		devl_unlock(dl);
		dev_warn(pdsc->dev, "Failed to register devlink params: %pe\n",
			 ERR_PTR(err));
		goto err_out_stop;
	}

	hr = devl_health_reporter_create(dl, &pdsc_fw_reporter_ops, 0, pdsc);
	if (IS_ERR(hr)) {
		devl_unlock(dl);
		dev_warn(pdsc->dev, "Failed to create fw reporter: %pe\n", hr);
		err = PTR_ERR(hr);
		goto err_out_unreg_params;
	}
	pdsc->fw_reporter = hr;

	devl_register(dl);
	devl_unlock(dl);

	/* Lastly, start the health check timer */
	mod_timer(&pdsc->wdtimer, round_jiffies(jiffies + pdsc->wdtimer_period));

	return 0;

err_out_unreg_params:
	devlink_params_unregister(dl, pdsc_dl_params,
				  ARRAY_SIZE(pdsc_dl_params));
err_out_stop:
	pdsc_stop(pdsc);
err_out_teardown:
	pdsc_teardown(pdsc, PDSC_TEARDOWN_REMOVING);
err_out_unmap_bars:
	timer_shutdown_sync(&pdsc->wdtimer);
	if (pdsc->wq)
		destroy_workqueue(pdsc->wq);
	mutex_destroy(&pdsc->config_lock);
	mutex_destroy(&pdsc->devcmd_lock);
	pci_free_irq_vectors(pdsc->pdev);
	pdsc_unmap_bars(pdsc);
err_out_release_regions:
	pci_release_regions(pdsc->pdev);

	return err;
}

static const struct devlink_ops pdsc_dl_ops = {
	.info_get	= pdsc_dl_info_get,
	.flash_update	= pdsc_dl_flash_update,
};

static const struct devlink_ops pdsc_dl_vf_ops = {
};

static DEFINE_IDA(pdsc_ida);

static int pdsc_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	const struct devlink_ops *ops;
	struct devlink *dl;
	struct pdsc *pdsc;
	bool is_pf;
	int err;

	is_pf = !pdev->is_virtfn;
	ops = is_pf ? &pdsc_dl_ops : &pdsc_dl_vf_ops;
	dl = devlink_alloc(ops, sizeof(struct pdsc), dev);
	if (!dl)
		return -ENOMEM;
	pdsc = devlink_priv(dl);

	pdsc->pdev = pdev;
	pdsc->dev = &pdev->dev;
	set_bit(PDSC_S_INITING_DRIVER, &pdsc->state);
	pci_set_drvdata(pdev, pdsc);
	pdsc_debugfs_add_dev(pdsc);

	err = ida_alloc(&pdsc_ida, GFP_KERNEL);
	if (err < 0) {
		dev_err(pdsc->dev, "%s: id alloc failed: %pe\n",
			__func__, ERR_PTR(err));
		goto err_out_free_devlink;
	}
	pdsc->uid = err;

	/* Query system for DMA addressing limitation for the device. */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(PDS_CORE_ADDR_LEN));
	if (err) {
		dev_err(dev, "Unable to obtain 64-bit DMA for consistent allocations, aborting: %pe\n",
			ERR_PTR(err));
		goto err_out_free_ida;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device: %pe\n", ERR_PTR(err));
		goto err_out_free_ida;
	}
	pci_set_master(pdev);

	if (is_pf)
		err = pdsc_init_pf(pdsc);
	else
		err = pdsc_init_vf(pdsc);
	if (err) {
		dev_err(dev, "Cannot init device: %pe\n", ERR_PTR(err));
		goto err_out_disable_device;
	}

	clear_bit(PDSC_S_INITING_DRIVER, &pdsc->state);
	return 0;

err_out_disable_device:
	pci_disable_device(pdev);
err_out_free_ida:
	ida_free(&pdsc_ida, pdsc->uid);
err_out_free_devlink:
	pdsc_debugfs_del_dev(pdsc);
	devlink_free(dl);

	return err;
}

static void pdsc_remove(struct pci_dev *pdev)
{
	struct pdsc *pdsc = pci_get_drvdata(pdev);
	struct devlink *dl;

	/* Unhook the registrations first to be sure there
	 * are no requests while we're stopping.
	 */
	dl = priv_to_devlink(pdsc);
	devl_lock(dl);
	devl_unregister(dl);
	if (!pdev->is_virtfn) {
		if (pdsc->fw_reporter) {
			devl_health_reporter_destroy(pdsc->fw_reporter);
			pdsc->fw_reporter = NULL;
		}
		devl_params_unregister(dl, pdsc_dl_params,
				       ARRAY_SIZE(pdsc_dl_params));
	}
	devl_unlock(dl);

	if (pdev->is_virtfn) {
		struct pdsc *pf;

		pf = pdsc_get_pf_struct(pdsc->pdev);
		if (!IS_ERR(pf)) {
			pdsc_auxbus_dev_del(pdsc, pf);
			pf->vfs[pdsc->vf_id].vf = NULL;
		}
	} else {
		/* Remove the VFs and their aux_bus connections before other
		 * cleanup so that the clients can use the AdminQ to cleanly
		 * shut themselves down.
		 */
		pdsc_sriov_configure(pdev, 0);

		timer_shutdown_sync(&pdsc->wdtimer);
		if (pdsc->wq)
			destroy_workqueue(pdsc->wq);

		mutex_lock(&pdsc->config_lock);
		set_bit(PDSC_S_STOPPING_DRIVER, &pdsc->state);

		pdsc_stop(pdsc);
		pdsc_teardown(pdsc, PDSC_TEARDOWN_REMOVING);
		mutex_unlock(&pdsc->config_lock);
		mutex_destroy(&pdsc->config_lock);
		mutex_destroy(&pdsc->devcmd_lock);

		pdsc_unmap_bars(pdsc);
		pci_release_regions(pdev);
	}

	pci_disable_device(pdev);

	ida_free(&pdsc_ida, pdsc->uid);
	pdsc_debugfs_del_dev(pdsc);
	devlink_free(dl);
}

static void pdsc_stop_health_thread(struct pdsc *pdsc)
{
	if (pdsc->pdev->is_virtfn)
		return;

	timer_shutdown_sync(&pdsc->wdtimer);
	if (pdsc->health_work.func)
		cancel_work_sync(&pdsc->health_work);
}

static void pdsc_restart_health_thread(struct pdsc *pdsc)
{
	if (pdsc->pdev->is_virtfn)
		return;

	timer_setup(&pdsc->wdtimer, pdsc_wdtimer_cb, 0);
	mod_timer(&pdsc->wdtimer, jiffies + 1);
}

static void pdsc_reset_prepare(struct pci_dev *pdev)
{
	struct pdsc *pdsc = pci_get_drvdata(pdev);

	pdsc_stop_health_thread(pdsc);
	pdsc_fw_down(pdsc);

	if (pdev->is_virtfn) {
		struct pdsc *pf;

		pf = pdsc_get_pf_struct(pdsc->pdev);
		if (!IS_ERR(pf))
			pdsc_auxbus_dev_del(pdsc, pf);
	}

	pdsc_unmap_bars(pdsc);
	pci_release_regions(pdev);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
}

static void pdsc_reset_done(struct pci_dev *pdev)
{
	struct pdsc *pdsc = pci_get_drvdata(pdev);
	struct device *dev = pdsc->dev;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device: %pe\n", ERR_PTR(err));
		return;
	}
	pci_set_master(pdev);

	if (!pdev->is_virtfn) {
		pcie_print_link_status(pdsc->pdev);

		err = pci_request_regions(pdsc->pdev, PDS_CORE_DRV_NAME);
		if (err) {
			dev_err(pdsc->dev, "Cannot request PCI regions: %pe\n",
				ERR_PTR(err));
			return;
		}

		err = pdsc_map_bars(pdsc);
		if (err)
			return;
	}

	pdsc_fw_up(pdsc);
	pdsc_restart_health_thread(pdsc);

	if (pdev->is_virtfn) {
		struct pdsc *pf;

		pf = pdsc_get_pf_struct(pdsc->pdev);
		if (!IS_ERR(pf))
			pdsc_auxbus_dev_add(pdsc, pf);
	}
}

static pci_ers_result_t pdsc_pci_error_detected(struct pci_dev *pdev,
						pci_channel_state_t error)
{
	if (error == pci_channel_io_frozen) {
		pdsc_reset_prepare(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
	}

	return PCI_ERS_RESULT_NONE;
}

static void pdsc_pci_error_resume(struct pci_dev *pdev)
{
	struct pdsc *pdsc = pci_get_drvdata(pdev);

	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state))
		pci_reset_function_locked(pdev);
}

static const struct pci_error_handlers pdsc_err_handler = {
	/* FLR handling */
	.reset_prepare      = pdsc_reset_prepare,
	.reset_done         = pdsc_reset_done,

	/* AER handling */
	.error_detected     = pdsc_pci_error_detected,
	.resume             = pdsc_pci_error_resume,
};

static struct pci_driver pdsc_driver = {
	.name = PDS_CORE_DRV_NAME,
	.id_table = pdsc_id_table,
	.probe = pdsc_probe,
	.remove = pdsc_remove,
	.sriov_configure = pdsc_sriov_configure,
	.err_handler = &pdsc_err_handler,
};

void *pdsc_get_pf_struct(struct pci_dev *vf_pdev)
{
	return pci_iov_get_pf_drvdata(vf_pdev, &pdsc_driver);
}
EXPORT_SYMBOL_GPL(pdsc_get_pf_struct);

static int __init pdsc_init_module(void)
{
	if (strcmp(KBUILD_MODNAME, PDS_CORE_DRV_NAME))
		return -EINVAL;

	pdsc_debugfs_create();
	return pci_register_driver(&pdsc_driver);
}

static void __exit pdsc_cleanup_module(void)
{
	pci_unregister_driver(&pdsc_driver);
	pdsc_debugfs_destroy();
}

module_init(pdsc_init_module);
module_exit(pdsc_cleanup_module);
