// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_cfg_common.h"
#include "adf_transport_access_macros.h"
#include "adf_transport_internal.h"

#define ADF_VINTSOU_OFFSET	0x204
#define ADF_VINTMSK_OFFSET	0x208
#define ADF_VINTSOU_BUN		BIT(0)
#define ADF_VINTSOU_PF2VF	BIT(1)

static struct workqueue_struct *adf_vf_stop_wq;

struct adf_vf_stop_data {
	struct adf_accel_dev *accel_dev;
	struct work_struct work;
};

void adf_enable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);

	ADF_CSR_WR(pmisc_addr, ADF_VINTMSK_OFFSET, 0x0);
}

void adf_disable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);

	ADF_CSR_WR(pmisc_addr, ADF_VINTMSK_OFFSET, 0x2);
}
EXPORT_SYMBOL_GPL(adf_disable_pf2vf_interrupts);

static int adf_enable_msi(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	int stat = pci_alloc_irq_vectors(pci_dev_info->pci_dev, 1, 1,
					 PCI_IRQ_MSI);
	if (unlikely(stat < 0)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to enable MSI interrupt: %d\n", stat);
		return stat;
	}

	return 0;
}

static void adf_disable_msi(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	pci_free_irq_vectors(pdev);
}

static void adf_dev_stop_async(struct work_struct *work)
{
	struct adf_vf_stop_data *stop_data =
		container_of(work, struct adf_vf_stop_data, work);
	struct adf_accel_dev *accel_dev = stop_data->accel_dev;

	adf_dev_restarting_notify(accel_dev);
	adf_dev_down(accel_dev);

	/* Re-enable PF2VF interrupts */
	adf_enable_pf2vf_interrupts(accel_dev);
	kfree(stop_data);
}

int adf_pf2vf_handle_pf_restarting(struct adf_accel_dev *accel_dev)
{
	struct adf_vf_stop_data *stop_data;

	clear_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status);
	stop_data = kzalloc(sizeof(*stop_data), GFP_ATOMIC);
	if (!stop_data) {
		dev_err(&GET_DEV(accel_dev),
			"Couldn't schedule stop for vf_%d\n",
			accel_dev->accel_id);
		return -ENOMEM;
	}
	stop_data->accel_dev = accel_dev;
	INIT_WORK(&stop_data->work, adf_dev_stop_async);
	queue_work(adf_vf_stop_wq, &stop_data->work);

	return 0;
}

static void adf_pf2vf_bh_handler(void *data)
{
	struct adf_accel_dev *accel_dev = data;
	bool ret;

	ret = adf_recv_and_handle_pf2vf_msg(accel_dev);
	if (ret)
		/* Re-enable PF2VF interrupts */
		adf_enable_pf2vf_interrupts(accel_dev);

	return;

}

static int adf_setup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	tasklet_init(&accel_dev->vf.pf2vf_bh_tasklet,
		     (void *)adf_pf2vf_bh_handler, (unsigned long)accel_dev);

	mutex_init(&accel_dev->vf.vf2pf_lock);
	return 0;
}

static void adf_cleanup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	tasklet_disable(&accel_dev->vf.pf2vf_bh_tasklet);
	tasklet_kill(&accel_dev->vf.pf2vf_bh_tasklet);
	mutex_destroy(&accel_dev->vf.vf2pf_lock);
}

static irqreturn_t adf_isr(int irq, void *privdata)
{
	struct adf_accel_dev *accel_dev = privdata;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_ops;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_bar_addr = pmisc->virt_addr;
	bool handled = false;
	u32 v_int, v_mask;

	/* Read VF INT source CSR to determine the source of VF interrupt */
	v_int = ADF_CSR_RD(pmisc_bar_addr, ADF_VINTSOU_OFFSET);

	/* Read VF INT mask CSR to determine which sources are masked */
	v_mask = ADF_CSR_RD(pmisc_bar_addr, ADF_VINTMSK_OFFSET);

	/*
	 * Recompute v_int ignoring sources that are masked. This is to
	 * avoid rescheduling the tasklet for interrupts already handled
	 */
	v_int &= ~v_mask;

	/* Check for PF2VF interrupt */
	if (v_int & ADF_VINTSOU_PF2VF) {
		/* Disable PF to VF interrupt */
		adf_disable_pf2vf_interrupts(accel_dev);

		/* Schedule tasklet to handle interrupt BH */
		tasklet_hi_schedule(&accel_dev->vf.pf2vf_bh_tasklet);
		handled = true;
	}

	/* Check bundle interrupt */
	if (v_int & ADF_VINTSOU_BUN) {
		struct adf_etr_data *etr_data = accel_dev->transport;
		struct adf_etr_bank_data *bank = &etr_data->banks[0];

		/* Disable Flag and Coalesce Ring Interrupts */
		csr_ops->write_csr_int_flag_and_col(bank->csr_addr,
						    bank->bank_number, 0);
		tasklet_hi_schedule(&bank->resp_handler);
		handled = true;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int adf_request_msi_irq(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	unsigned int cpu;
	int ret;

	snprintf(accel_dev->vf.irq_name, ADF_MAX_MSIX_VECTOR_NAME,
		 "qat_%02x:%02d.%02d", pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));
	ret = request_irq(pdev->irq, adf_isr, 0, accel_dev->vf.irq_name,
			  (void *)accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "failed to enable irq for %s\n",
			accel_dev->vf.irq_name);
		return ret;
	}
	cpu = accel_dev->accel_id % num_online_cpus();
	irq_set_affinity_hint(pdev->irq, get_cpu_mask(cpu));
	accel_dev->vf.irq_enabled = true;

	return ret;
}

static int adf_setup_bh(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;

	tasklet_init(&priv_data->banks[0].resp_handler, adf_response_handler,
		     (unsigned long)priv_data->banks);
	return 0;
}

static void adf_cleanup_bh(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;

	tasklet_disable(&priv_data->banks[0].resp_handler);
	tasklet_kill(&priv_data->banks[0].resp_handler);
}

/**
 * adf_vf_isr_resource_free() - Free IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function frees interrupts for acceleration device virtual function.
 */
void adf_vf_isr_resource_free(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	if (accel_dev->vf.irq_enabled) {
		irq_set_affinity_hint(pdev->irq, NULL);
		free_irq(pdev->irq, accel_dev);
	}
	adf_cleanup_bh(accel_dev);
	adf_cleanup_pf2vf_bh(accel_dev);
	adf_disable_msi(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_vf_isr_resource_free);

/**
 * adf_vf_isr_resource_alloc() - Allocate IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function allocates interrupts for acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_vf_isr_resource_alloc(struct adf_accel_dev *accel_dev)
{
	if (adf_enable_msi(accel_dev))
		goto err_out;

	if (adf_setup_pf2vf_bh(accel_dev))
		goto err_disable_msi;

	if (adf_setup_bh(accel_dev))
		goto err_cleanup_pf2vf_bh;

	if (adf_request_msi_irq(accel_dev))
		goto err_cleanup_bh;

	return 0;

err_cleanup_bh:
	adf_cleanup_bh(accel_dev);

err_cleanup_pf2vf_bh:
	adf_cleanup_pf2vf_bh(accel_dev);

err_disable_msi:
	adf_disable_msi(accel_dev);

err_out:
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_vf_isr_resource_alloc);

/**
 * adf_flush_vf_wq() - Flush workqueue for VF
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function disables the PF/VF interrupts on the VF so that no new messages
 * are received and flushes the workqueue 'adf_vf_stop_wq'.
 *
 * Return: void.
 */
void adf_flush_vf_wq(struct adf_accel_dev *accel_dev)
{
	adf_disable_pf2vf_interrupts(accel_dev);

	flush_workqueue(adf_vf_stop_wq);
}
EXPORT_SYMBOL_GPL(adf_flush_vf_wq);

/**
 * adf_init_vf_wq() - Init workqueue for VF
 *
 * Return: 0 on success, error code otherwise.
 */
int __init adf_init_vf_wq(void)
{
	adf_vf_stop_wq = alloc_workqueue("adf_vf_stop_wq", WQ_MEM_RECLAIM, 0);

	return !adf_vf_stop_wq ? -EFAULT : 0;
}

void adf_exit_vf_wq(void)
{
	if (adf_vf_stop_wq)
		destroy_workqueue(adf_vf_stop_wq);

	adf_vf_stop_wq = NULL;
}
