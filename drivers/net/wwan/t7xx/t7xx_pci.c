// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_wakeup.h>

#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_reg.h"
#include "t7xx_state_monitor.h"

#define T7XX_PCI_IREG_BASE		0
#define T7XX_PCI_EREG_BASE		2

#define PM_ACK_TIMEOUT_MS		1500
#define PM_RESOURCE_POLL_TIMEOUT_US	10000
#define PM_RESOURCE_POLL_STEP_US	100

enum t7xx_pm_state {
	MTK_PM_EXCEPTION,
	MTK_PM_INIT,		/* Device initialized, but handshake not completed */
	MTK_PM_SUSPENDED,
	MTK_PM_RESUMED,
};

static int t7xx_wait_pm_config(struct t7xx_pci_dev *t7xx_dev)
{
	int ret, val;

	ret = read_poll_timeout(ioread32, val,
				(val & T7XX_PCIE_RESOURCE_STS_MSK) == T7XX_PCIE_RESOURCE_STS_MSK,
				PM_RESOURCE_POLL_STEP_US, PM_RESOURCE_POLL_TIMEOUT_US, true,
				IREG_BASE(t7xx_dev) + T7XX_PCIE_RESOURCE_STATUS);
	if (ret == -ETIMEDOUT)
		dev_err(&t7xx_dev->pdev->dev, "PM configuration timed out\n");

	return ret;
}

static int t7xx_pci_pm_init(struct t7xx_pci_dev *t7xx_dev)
{
	struct pci_dev *pdev = t7xx_dev->pdev;

	INIT_LIST_HEAD(&t7xx_dev->md_pm_entities);
	mutex_init(&t7xx_dev->md_pm_entity_mtx);
	init_completion(&t7xx_dev->pm_sr_ack);
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_INIT);

	device_init_wakeup(&pdev->dev, true);
	dev_pm_set_driver_flags(&pdev->dev, pdev->dev.power.driver_flags |
				DPM_FLAG_NO_DIRECT_COMPLETE);

	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);

	return t7xx_wait_pm_config(t7xx_dev);
}

void t7xx_pci_pm_init_late(struct t7xx_pci_dev *t7xx_dev)
{
	/* Enable the PCIe resource lock only after MD deep sleep is done */
	t7xx_mhccif_mask_clr(t7xx_dev,
			     D2H_INT_SUSPEND_ACK |
			     D2H_INT_RESUME_ACK |
			     D2H_INT_SUSPEND_ACK_AP |
			     D2H_INT_RESUME_ACK_AP);
	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_RESUMED);
}

static int t7xx_pci_pm_reinit(struct t7xx_pci_dev *t7xx_dev)
{
	/* The device is kept in FSM re-init flow
	 * so just roll back PM setting to the init setting.
	 */
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_INIT);
	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);
	return t7xx_wait_pm_config(t7xx_dev);
}

void t7xx_pci_pm_exp_detected(struct t7xx_pci_dev *t7xx_dev)
{
	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);
	t7xx_wait_pm_config(t7xx_dev);
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_EXCEPTION);
}

int t7xx_pci_pm_entity_register(struct t7xx_pci_dev *t7xx_dev, struct md_pm_entity *pm_entity)
{
	struct md_pm_entity *entity;

	mutex_lock(&t7xx_dev->md_pm_entity_mtx);
	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity->id == pm_entity->id) {
			mutex_unlock(&t7xx_dev->md_pm_entity_mtx);
			return -EEXIST;
		}
	}

	list_add_tail(&pm_entity->entity, &t7xx_dev->md_pm_entities);
	mutex_unlock(&t7xx_dev->md_pm_entity_mtx);
	return 0;
}

int t7xx_pci_pm_entity_unregister(struct t7xx_pci_dev *t7xx_dev, struct md_pm_entity *pm_entity)
{
	struct md_pm_entity *entity, *tmp_entity;

	mutex_lock(&t7xx_dev->md_pm_entity_mtx);
	list_for_each_entry_safe(entity, tmp_entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity->id == pm_entity->id) {
			list_del(&pm_entity->entity);
			mutex_unlock(&t7xx_dev->md_pm_entity_mtx);
			return 0;
		}
	}

	mutex_unlock(&t7xx_dev->md_pm_entity_mtx);

	return -ENXIO;
}

static int t7xx_send_pm_request(struct t7xx_pci_dev *t7xx_dev, u32 request)
{
	unsigned long wait_ret;

	reinit_completion(&t7xx_dev->pm_sr_ack);
	t7xx_mhccif_h2d_swint_trigger(t7xx_dev, request);
	wait_ret = wait_for_completion_timeout(&t7xx_dev->pm_sr_ack,
					       msecs_to_jiffies(PM_ACK_TIMEOUT_MS));
	if (!wait_ret)
		return -ETIMEDOUT;

	return 0;
}

static int __t7xx_pci_pm_suspend(struct pci_dev *pdev)
{
	enum t7xx_pm_id entity_id = PM_ENTITY_ID_INVALID;
	struct t7xx_pci_dev *t7xx_dev;
	struct md_pm_entity *entity;
	int ret;

	t7xx_dev = pci_get_drvdata(pdev);
	if (atomic_read(&t7xx_dev->md_pm_state) <= MTK_PM_INIT) {
		dev_err(&pdev->dev, "[PM] Exiting suspend, modem in invalid state\n");
		return -EFAULT;
	}

	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);
	ret = t7xx_wait_pm_config(t7xx_dev);
	if (ret) {
		iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
		return ret;
	}

	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_SUSPENDED);
	t7xx_pcie_mac_clear_int(t7xx_dev, SAP_RGU_INT);
	t7xx_dev->rgu_pci_irq_en = false;

	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (!entity->suspend)
			continue;

		ret = entity->suspend(t7xx_dev, entity->entity_param);
		if (ret) {
			entity_id = entity->id;
			dev_err(&pdev->dev, "[PM] Suspend error: %d, id: %d\n", ret, entity_id);
			goto abort_suspend;
		}
	}

	ret = t7xx_send_pm_request(t7xx_dev, H2D_CH_SUSPEND_REQ);
	if (ret) {
		dev_err(&pdev->dev, "[PM] MD suspend error: %d\n", ret);
		goto abort_suspend;
	}

	ret = t7xx_send_pm_request(t7xx_dev, H2D_CH_SUSPEND_REQ_AP);
	if (ret) {
		t7xx_send_pm_request(t7xx_dev, H2D_CH_RESUME_REQ);
		dev_err(&pdev->dev, "[PM] SAP suspend error: %d\n", ret);
		goto abort_suspend;
	}

	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity->suspend_late)
			entity->suspend_late(t7xx_dev, entity->entity_param);
	}

	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
	return 0;

abort_suspend:
	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity_id == entity->id)
			break;

		if (entity->resume)
			entity->resume(t7xx_dev, entity->entity_param);
	}

	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_RESUMED);
	t7xx_pcie_mac_set_int(t7xx_dev, SAP_RGU_INT);
	return ret;
}

static void t7xx_pcie_interrupt_reinit(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_pcie_set_mac_msix_cfg(t7xx_dev, EXT_INT_NUM);

	/* Disable interrupt first and let the IPs enable them */
	iowrite32(MSIX_MSK_SET_ALL, IREG_BASE(t7xx_dev) + IMASK_HOST_MSIX_CLR_GRP0_0);

	/* Device disables PCIe interrupts during resume and
	 * following function will re-enable PCIe interrupts.
	 */
	t7xx_pcie_mac_interrupts_en(t7xx_dev);
	t7xx_pcie_mac_set_int(t7xx_dev, MHCCIF_INT);
}

static int t7xx_pcie_reinit(struct t7xx_pci_dev *t7xx_dev, bool is_d3)
{
	int ret;

	ret = pcim_enable_device(t7xx_dev->pdev);
	if (ret)
		return ret;

	t7xx_pcie_mac_atr_init(t7xx_dev);
	t7xx_pcie_interrupt_reinit(t7xx_dev);

	if (is_d3) {
		t7xx_mhccif_init(t7xx_dev);
		return t7xx_pci_pm_reinit(t7xx_dev);
	}

	return 0;
}

static int t7xx_send_fsm_command(struct t7xx_pci_dev *t7xx_dev, u32 event)
{
	struct t7xx_fsm_ctl *fsm_ctl = t7xx_dev->md->fsm_ctl;
	struct device *dev = &t7xx_dev->pdev->dev;
	int ret = -EINVAL;

	switch (event) {
	case FSM_CMD_STOP:
		ret = t7xx_fsm_append_cmd(fsm_ctl, FSM_CMD_STOP, FSM_CMD_FLAG_WAIT_FOR_COMPLETION);
		break;

	case FSM_CMD_START:
		t7xx_pcie_mac_clear_int(t7xx_dev, SAP_RGU_INT);
		t7xx_pcie_mac_clear_int_status(t7xx_dev, SAP_RGU_INT);
		t7xx_dev->rgu_pci_irq_en = true;
		t7xx_pcie_mac_set_int(t7xx_dev, SAP_RGU_INT);
		ret = t7xx_fsm_append_cmd(fsm_ctl, FSM_CMD_START, 0);
		break;

	default:
		break;
	}

	if (ret)
		dev_err(dev, "Failure handling FSM command %u, %d\n", event, ret);

	return ret;
}

static int __t7xx_pci_pm_resume(struct pci_dev *pdev, bool state_check)
{
	struct t7xx_pci_dev *t7xx_dev;
	struct md_pm_entity *entity;
	u32 prev_state;
	int ret = 0;

	t7xx_dev = pci_get_drvdata(pdev);
	if (atomic_read(&t7xx_dev->md_pm_state) <= MTK_PM_INIT) {
		iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
		return 0;
	}

	t7xx_pcie_mac_interrupts_en(t7xx_dev);
	prev_state = ioread32(IREG_BASE(t7xx_dev) + T7XX_PCIE_PM_RESUME_STATE);

	if (state_check) {
		/* For D3/L3 resume, the device could boot so quickly that the
		 * initial value of the dummy register might be overwritten.
		 * Identify new boots if the ATR source address register is not initialized.
		 */
		u32 atr_reg_val = ioread32(IREG_BASE(t7xx_dev) +
					   ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR);
		if (prev_state == PM_RESUME_REG_STATE_L3 ||
		    (prev_state == PM_RESUME_REG_STATE_INIT &&
		     atr_reg_val == ATR_SRC_ADDR_INVALID)) {
			ret = t7xx_send_fsm_command(t7xx_dev, FSM_CMD_STOP);
			if (ret)
				return ret;

			ret = t7xx_pcie_reinit(t7xx_dev, true);
			if (ret)
				return ret;

			t7xx_clear_rgu_irq(t7xx_dev);
			return t7xx_send_fsm_command(t7xx_dev, FSM_CMD_START);
		}

		if (prev_state == PM_RESUME_REG_STATE_EXP ||
		    prev_state == PM_RESUME_REG_STATE_L2_EXP) {
			if (prev_state == PM_RESUME_REG_STATE_L2_EXP) {
				ret = t7xx_pcie_reinit(t7xx_dev, false);
				if (ret)
					return ret;
			}

			atomic_set(&t7xx_dev->md_pm_state, MTK_PM_SUSPENDED);
			t7xx_dev->rgu_pci_irq_en = true;
			t7xx_pcie_mac_set_int(t7xx_dev, SAP_RGU_INT);

			t7xx_mhccif_mask_clr(t7xx_dev,
					     D2H_INT_EXCEPTION_INIT |
					     D2H_INT_EXCEPTION_INIT_DONE |
					     D2H_INT_EXCEPTION_CLEARQ_DONE |
					     D2H_INT_EXCEPTION_ALLQ_RESET |
					     D2H_INT_PORT_ENUM);

			return ret;
		}

		if (prev_state == PM_RESUME_REG_STATE_L2) {
			ret = t7xx_pcie_reinit(t7xx_dev, false);
			if (ret)
				return ret;

		} else if (prev_state != PM_RESUME_REG_STATE_L1 &&
			   prev_state != PM_RESUME_REG_STATE_INIT) {
			ret = t7xx_send_fsm_command(t7xx_dev, FSM_CMD_STOP);
			if (ret)
				return ret;

			t7xx_clear_rgu_irq(t7xx_dev);
			atomic_set(&t7xx_dev->md_pm_state, MTK_PM_SUSPENDED);
			return 0;
		}
	}

	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + DISABLE_ASPM_LOWPWR);
	t7xx_wait_pm_config(t7xx_dev);

	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity->resume_early)
			entity->resume_early(t7xx_dev, entity->entity_param);
	}

	ret = t7xx_send_pm_request(t7xx_dev, H2D_CH_RESUME_REQ);
	if (ret)
		dev_err(&pdev->dev, "[PM] MD resume error: %d\n", ret);

	ret = t7xx_send_pm_request(t7xx_dev, H2D_CH_RESUME_REQ_AP);
	if (ret)
		dev_err(&pdev->dev, "[PM] SAP resume error: %d\n", ret);

	list_for_each_entry(entity, &t7xx_dev->md_pm_entities, entity) {
		if (entity->resume) {
			ret = entity->resume(t7xx_dev, entity->entity_param);
			if (ret)
				dev_err(&pdev->dev, "[PM] Resume entry ID: %d error: %d\n",
					entity->id, ret);
		}
	}

	t7xx_dev->rgu_pci_irq_en = true;
	t7xx_pcie_mac_set_int(t7xx_dev, SAP_RGU_INT);
	iowrite32(T7XX_L1_BIT(0), IREG_BASE(t7xx_dev) + ENABLE_ASPM_LOWPWR);
	atomic_set(&t7xx_dev->md_pm_state, MTK_PM_RESUMED);

	return ret;
}

static int t7xx_pci_pm_resume_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct t7xx_pci_dev *t7xx_dev;

	t7xx_dev = pci_get_drvdata(pdev);
	t7xx_pcie_mac_interrupts_dis(t7xx_dev);

	return 0;
}

static void t7xx_pci_shutdown(struct pci_dev *pdev)
{
	__t7xx_pci_pm_suspend(pdev);
}

static int t7xx_pci_pm_suspend(struct device *dev)
{
	return __t7xx_pci_pm_suspend(to_pci_dev(dev));
}

static int t7xx_pci_pm_resume(struct device *dev)
{
	return __t7xx_pci_pm_resume(to_pci_dev(dev), true);
}

static int t7xx_pci_pm_thaw(struct device *dev)
{
	return __t7xx_pci_pm_resume(to_pci_dev(dev), false);
}

static const struct dev_pm_ops t7xx_pci_pm_ops = {
	.suspend = t7xx_pci_pm_suspend,
	.resume = t7xx_pci_pm_resume,
	.resume_noirq = t7xx_pci_pm_resume_noirq,
	.freeze = t7xx_pci_pm_suspend,
	.thaw = t7xx_pci_pm_thaw,
	.poweroff = t7xx_pci_pm_suspend,
	.restore = t7xx_pci_pm_resume,
	.restore_noirq = t7xx_pci_pm_resume_noirq,
};

static int t7xx_request_irq(struct pci_dev *pdev)
{
	struct t7xx_pci_dev *t7xx_dev;
	int ret, i;

	t7xx_dev = pci_get_drvdata(pdev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		const char *irq_descr;
		int irq_vec;

		if (!t7xx_dev->intr_handler[i])
			continue;

		irq_descr = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_%d",
					   dev_driver_string(&pdev->dev), i);
		if (!irq_descr) {
			ret = -ENOMEM;
			break;
		}

		irq_vec = pci_irq_vector(pdev, i);
		ret = request_threaded_irq(irq_vec, t7xx_dev->intr_handler[i],
					   t7xx_dev->intr_thread[i], 0, irq_descr,
					   t7xx_dev->callback_param[i]);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
			break;
		}
	}

	if (ret) {
		while (i--) {
			if (!t7xx_dev->intr_handler[i])
				continue;

			free_irq(pci_irq_vector(pdev, i), t7xx_dev->callback_param[i]);
		}
	}

	return ret;
}

static int t7xx_setup_msix(struct t7xx_pci_dev *t7xx_dev)
{
	struct pci_dev *pdev = t7xx_dev->pdev;
	int ret;

	/* Only using 6 interrupts, but HW-design requires power-of-2 IRQs allocation */
	ret = pci_alloc_irq_vectors(pdev, EXT_INT_NUM, EXT_INT_NUM, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to allocate MSI-X entry: %d\n", ret);
		return ret;
	}

	ret = t7xx_request_irq(pdev);
	if (ret) {
		pci_free_irq_vectors(pdev);
		return ret;
	}

	t7xx_pcie_set_mac_msix_cfg(t7xx_dev, EXT_INT_NUM);
	return 0;
}

static int t7xx_interrupt_init(struct t7xx_pci_dev *t7xx_dev)
{
	int ret, i;

	if (!t7xx_dev->pdev->msix_cap)
		return -EINVAL;

	ret = t7xx_setup_msix(t7xx_dev);
	if (ret)
		return ret;

	/* IPs enable interrupts when ready */
	for (i = 0; i < EXT_INT_NUM; i++)
		t7xx_pcie_mac_set_int(t7xx_dev, i);

	return 0;
}

static void t7xx_pci_infracfg_ao_calc(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_dev->base_addr.infracfg_ao_base = t7xx_dev->base_addr.pcie_ext_reg_base +
					      INFRACFG_AO_DEV_CHIP -
					      t7xx_dev->base_addr.pcie_dev_reg_trsl_addr;
}

static int t7xx_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct t7xx_pci_dev *t7xx_dev;
	int ret;

	t7xx_dev = devm_kzalloc(&pdev->dev, sizeof(*t7xx_dev), GFP_KERNEL);
	if (!t7xx_dev)
		return -ENOMEM;

	pci_set_drvdata(pdev, t7xx_dev);
	t7xx_dev->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pcim_iomap_regions(pdev, BIT(T7XX_PCI_IREG_BASE) | BIT(T7XX_PCI_EREG_BASE),
				 pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "Could not request BARs: %d\n", ret);
		return -ENOMEM;
	}

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "Could not set PCI DMA mask: %d\n", ret);
		return ret;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "Could not set consistent PCI DMA mask: %d\n", ret);
		return ret;
	}

	IREG_BASE(t7xx_dev) = pcim_iomap_table(pdev)[T7XX_PCI_IREG_BASE];
	t7xx_dev->base_addr.pcie_ext_reg_base = pcim_iomap_table(pdev)[T7XX_PCI_EREG_BASE];

	ret = t7xx_pci_pm_init(t7xx_dev);
	if (ret)
		return ret;

	t7xx_pcie_mac_atr_init(t7xx_dev);
	t7xx_pci_infracfg_ao_calc(t7xx_dev);
	t7xx_mhccif_init(t7xx_dev);

	ret = t7xx_md_init(t7xx_dev);
	if (ret)
		return ret;

	t7xx_pcie_mac_interrupts_dis(t7xx_dev);

	ret = t7xx_interrupt_init(t7xx_dev);
	if (ret) {
		t7xx_md_exit(t7xx_dev);
		return ret;
	}

	t7xx_pcie_mac_set_int(t7xx_dev, MHCCIF_INT);
	t7xx_pcie_mac_interrupts_en(t7xx_dev);

	return 0;
}

static void t7xx_pci_remove(struct pci_dev *pdev)
{
	struct t7xx_pci_dev *t7xx_dev;
	int i;

	t7xx_dev = pci_get_drvdata(pdev);
	t7xx_md_exit(t7xx_dev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		if (!t7xx_dev->intr_handler[i])
			continue;

		free_irq(pci_irq_vector(pdev, i), t7xx_dev->callback_param[i]);
	}

	pci_free_irq_vectors(t7xx_dev->pdev);
}

static const struct pci_device_id t7xx_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x4d75) },
	{ }
};
MODULE_DEVICE_TABLE(pci, t7xx_pci_table);

static struct pci_driver t7xx_pci_driver = {
	.name = "mtk_t7xx",
	.id_table = t7xx_pci_table,
	.probe = t7xx_pci_probe,
	.remove = t7xx_pci_remove,
	.driver.pm = &t7xx_pci_pm_ops,
	.shutdown = t7xx_pci_shutdown,
};

module_pci_driver(t7xx_pci_driver);

MODULE_AUTHOR("MediaTek Inc");
MODULE_DESCRIPTION("MediaTek PCIe 5G WWAN modem T7xx driver");
MODULE_LICENSE("GPL");
