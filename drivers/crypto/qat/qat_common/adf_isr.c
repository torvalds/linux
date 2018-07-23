/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_cfg_common.h"
#include "adf_transport_access_macros.h"
#include "adf_transport_internal.h"

static int adf_enable_msix(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 msix_num_entries = 1;

	/* If SR-IOV is disabled, add entries for each bank */
	if (!accel_dev->pf.vf_info) {
		int i;

		msix_num_entries += hw_data->num_banks;
		for (i = 0; i < msix_num_entries; i++)
			pci_dev_info->msix_entries.entries[i].entry = i;
	} else {
		pci_dev_info->msix_entries.entries[0].entry =
			hw_data->num_banks;
	}

	if (pci_enable_msix_exact(pci_dev_info->pci_dev,
				  pci_dev_info->msix_entries.entries,
				  msix_num_entries)) {
		dev_err(&GET_DEV(accel_dev), "Failed to enable MSI-X IRQ(s)\n");
		return -EFAULT;
	}
	return 0;
}

static void adf_disable_msix(struct adf_accel_pci *pci_dev_info)
{
	pci_disable_msix(pci_dev_info->pci_dev);
}

static irqreturn_t adf_msix_isr_bundle(int irq, void *bank_ptr)
{
	struct adf_etr_bank_data *bank = bank_ptr;

	WRITE_CSR_INT_FLAG_AND_COL(bank->csr_addr, bank->bank_number, 0);
	tasklet_hi_schedule(&bank->resp_handler);
	return IRQ_HANDLED;
}

static irqreturn_t adf_msix_isr_ae(int irq, void *dev_ptr)
{
	struct adf_accel_dev *accel_dev = dev_ptr;

#ifdef CONFIG_PCI_IOV
	/* If SR-IOV is enabled (vf_info is non-NULL), check for VF->PF ints */
	if (accel_dev->pf.vf_info) {
		struct adf_hw_device_data *hw_data = accel_dev->hw_device;
		struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
		void __iomem *pmisc_bar_addr = pmisc->virt_addr;
		u32 vf_mask;

		/* Get the interrupt sources triggered by VFs */
		vf_mask = ((ADF_CSR_RD(pmisc_bar_addr, ADF_ERRSOU5) &
			    0x0000FFFF) << 16) |
			  ((ADF_CSR_RD(pmisc_bar_addr, ADF_ERRSOU3) &
			    0x01FFFE00) >> 9);

		if (vf_mask) {
			struct adf_accel_vf_info *vf_info;
			bool irq_handled = false;
			int i;

			/* Disable VF2PF interrupts for VFs with pending ints */
			adf_disable_vf2pf_interrupts(accel_dev, vf_mask);

			/*
			 * Schedule tasklets to handle VF2PF interrupt BHs
			 * unless the VF is malicious and is attempting to
			 * flood the host OS with VF2PF interrupts.
			 */
			for_each_set_bit(i, (const unsigned long *)&vf_mask,
					 (sizeof(vf_mask) * BITS_PER_BYTE)) {
				vf_info = accel_dev->pf.vf_info + i;

				if (!__ratelimit(&vf_info->vf2pf_ratelimit)) {
					dev_info(&GET_DEV(accel_dev),
						 "Too many ints from VF%d\n",
						  vf_info->vf_nr + 1);
					continue;
				}

				/* Tasklet will re-enable ints from this VF */
				tasklet_hi_schedule(&vf_info->vf2pf_bh_tasklet);
				irq_handled = true;
			}

			if (irq_handled)
				return IRQ_HANDLED;
		}
	}
#endif /* CONFIG_PCI_IOV */

	dev_dbg(&GET_DEV(accel_dev), "qat_dev%d spurious AE interrupt\n",
		accel_dev->accel_id);

	return IRQ_NONE;
}

static int adf_request_irqs(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
	struct adf_etr_data *etr_data = accel_dev->transport;
	int ret, i = 0;
	char *name;

	/* Request msix irq for all banks unless SR-IOV enabled */
	if (!accel_dev->pf.vf_info) {
		for (i = 0; i < hw_data->num_banks; i++) {
			struct adf_etr_bank_data *bank = &etr_data->banks[i];
			unsigned int cpu, cpus = num_online_cpus();

			name = *(pci_dev_info->msix_entries.names + i);
			snprintf(name, ADF_MAX_MSIX_VECTOR_NAME,
				 "qat%d-bundle%d", accel_dev->accel_id, i);
			ret = request_irq(msixe[i].vector,
					  adf_msix_isr_bundle, 0, name, bank);
			if (ret) {
				dev_err(&GET_DEV(accel_dev),
					"failed to enable irq %d for %s\n",
					msixe[i].vector, name);
				return ret;
			}

			cpu = ((accel_dev->accel_id * hw_data->num_banks) +
			       i) % cpus;
			irq_set_affinity_hint(msixe[i].vector,
					      get_cpu_mask(cpu));
		}
	}

	/* Request msix irq for AE */
	name = *(pci_dev_info->msix_entries.names + i);
	snprintf(name, ADF_MAX_MSIX_VECTOR_NAME,
		 "qat%d-ae-cluster", accel_dev->accel_id);
	ret = request_irq(msixe[i].vector, adf_msix_isr_ae, 0, name, accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"failed to enable irq %d, for %s\n",
			msixe[i].vector, name);
		return ret;
	}
	return ret;
}

static void adf_free_irqs(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
	struct adf_etr_data *etr_data = accel_dev->transport;
	int i = 0;

	if (pci_dev_info->msix_entries.num_entries > 1) {
		for (i = 0; i < hw_data->num_banks; i++) {
			irq_set_affinity_hint(msixe[i].vector, NULL);
			free_irq(msixe[i].vector, &etr_data->banks[i]);
		}
	}
	irq_set_affinity_hint(msixe[i].vector, NULL);
	free_irq(msixe[i].vector, accel_dev);
}

static int adf_isr_alloc_msix_entry_table(struct adf_accel_dev *accel_dev)
{
	int i;
	char **names;
	struct msix_entry *entries;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 msix_num_entries = 1;

	/* If SR-IOV is disabled (vf_info is NULL), add entries for each bank */
	if (!accel_dev->pf.vf_info)
		msix_num_entries += hw_data->num_banks;

	entries = kcalloc_node(msix_num_entries, sizeof(*entries),
			       GFP_KERNEL, dev_to_node(&GET_DEV(accel_dev)));
	if (!entries)
		return -ENOMEM;

	names = kcalloc(msix_num_entries, sizeof(char *), GFP_KERNEL);
	if (!names) {
		kfree(entries);
		return -ENOMEM;
	}
	for (i = 0; i < msix_num_entries; i++) {
		*(names + i) = kzalloc(ADF_MAX_MSIX_VECTOR_NAME, GFP_KERNEL);
		if (!(*(names + i)))
			goto err;
	}
	accel_dev->accel_pci_dev.msix_entries.num_entries = msix_num_entries;
	accel_dev->accel_pci_dev.msix_entries.entries = entries;
	accel_dev->accel_pci_dev.msix_entries.names = names;
	return 0;
err:
	for (i = 0; i < msix_num_entries; i++)
		kfree(*(names + i));
	kfree(entries);
	kfree(names);
	return -ENOMEM;
}

static void adf_isr_free_msix_entry_table(struct adf_accel_dev *accel_dev)
{
	char **names = accel_dev->accel_pci_dev.msix_entries.names;
	int i;

	kfree(accel_dev->accel_pci_dev.msix_entries.entries);
	for (i = 0; i < accel_dev->accel_pci_dev.msix_entries.num_entries; i++)
		kfree(*(names + i));
	kfree(names);
}

static int adf_setup_bh(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int i;

	for (i = 0; i < hw_data->num_banks; i++)
		tasklet_init(&priv_data->banks[i].resp_handler,
			     adf_response_handler,
			     (unsigned long)&priv_data->banks[i]);
	return 0;
}

static void adf_cleanup_bh(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int i;

	for (i = 0; i < hw_data->num_banks; i++) {
		tasklet_disable(&priv_data->banks[i].resp_handler);
		tasklet_kill(&priv_data->banks[i].resp_handler);
	}
}

/**
 * adf_isr_resource_free() - Free IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function frees interrupts for acceleration device.
 */
void adf_isr_resource_free(struct adf_accel_dev *accel_dev)
{
	adf_free_irqs(accel_dev);
	adf_cleanup_bh(accel_dev);
	adf_disable_msix(&accel_dev->accel_pci_dev);
	adf_isr_free_msix_entry_table(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_isr_resource_free);

/**
 * adf_isr_resource_alloc() - Allocate IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function allocates interrupts for acceleration device.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_isr_resource_alloc(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_isr_alloc_msix_entry_table(accel_dev);
	if (ret)
		return ret;
	if (adf_enable_msix(accel_dev))
		goto err_out;

	if (adf_setup_bh(accel_dev))
		goto err_out;

	if (adf_request_irqs(accel_dev))
		goto err_out;

	return 0;
err_out:
	adf_isr_resource_free(accel_dev);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_isr_resource_alloc);
