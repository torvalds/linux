/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/string.h>
#include "isci.h"
#include "task.h"
#include "sci_controller_constants.h"
#include "scic_remote_device.h"
#include "sci_environment.h"

static struct scsi_transport_template *isci_transport_template;
struct kmem_cache *isci_kmem_cache;

static DEFINE_PCI_DEVICE_TABLE(isci_id_table) = {
	{ PCI_VDEVICE(INTEL, 0x1D61),},
	{ PCI_VDEVICE(INTEL, 0x1D63),},
	{ PCI_VDEVICE(INTEL, 0x1D65),},
	{ PCI_VDEVICE(INTEL, 0x1D67),},
	{ PCI_VDEVICE(INTEL, 0x1D69),},
	{ PCI_VDEVICE(INTEL, 0x1D6B),},
	{ PCI_VDEVICE(INTEL, 0x1D60),},
	{ PCI_VDEVICE(INTEL, 0x1D62),},
	{ PCI_VDEVICE(INTEL, 0x1D64),},
	{ PCI_VDEVICE(INTEL, 0x1D66),},
	{ PCI_VDEVICE(INTEL, 0x1D68),},
	{ PCI_VDEVICE(INTEL, 0x1D6A),},
	{}
};

struct isci_firmware *isci_firmware;

static int __devinit isci_pci_probe(
	struct pci_dev *pdev,
	const struct pci_device_id *device_id_p);

static void __devexit isci_pci_remove(struct pci_dev *pdev);

MODULE_DEVICE_TABLE(pci, isci_id_table);

static struct pci_driver isci_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= isci_id_table,
	.probe		= isci_pci_probe,
	.remove		= __devexit_p(isci_pci_remove),
};

/* linux isci specific settings */

#if defined(CONFIG_PBG_HBA_A0)
int isci_si_rev = ISCI_SI_REVA0;
#elif defined(CONFIG_PBG_HBA_A2)
int isci_si_rev = ISCI_SI_REVA2;
#else
int isci_si_rev = ISCI_SI_REVB0;
#endif
module_param(isci_si_rev, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(isci_si_rev, "override default si rev (0: A0 1: A2 2: B0)");

static struct scsi_host_template isci_sht = {

	.module				= THIS_MODULE,
	.name				= DRV_NAME,
	.proc_name			= DRV_NAME,
	.queuecommand			= sas_queuecommand,
	.target_alloc			= sas_target_alloc,
	.slave_configure		= sas_slave_configure,
	.slave_destroy			= sas_slave_destroy,
	.scan_finished			= isci_host_scan_finished,
	.scan_start			= isci_host_scan_start,
	.change_queue_depth		= sas_change_queue_depth,
	.change_queue_type		= sas_change_queue_type,
	.bios_param			= sas_bios_param,
	.can_queue			= ISCI_CAN_QUEUE_VAL,
	.cmd_per_lun			= 1,
	.this_id			= -1,
	.sg_tablesize			= SG_ALL,
	.max_sectors			= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering			= ENABLE_CLUSTERING,
	.eh_device_reset_handler	= sas_eh_device_reset_handler,
	.eh_bus_reset_handler		= isci_bus_reset_handler,
	.slave_alloc			= sas_slave_alloc,
	.target_destroy			= sas_target_destroy,
	.ioctl				= sas_ioctl,
};

static struct sas_domain_function_template isci_transport_ops  = {

	/* The class calls these to notify the LLDD of an event. */
	.lldd_port_formed	= isci_port_formed,
	.lldd_port_deformed	= isci_port_deformed,

	/* The class calls these when a device is found or gone. */
	.lldd_dev_found		= isci_remote_device_found,
	.lldd_dev_gone		= isci_remote_device_gone,

	.lldd_execute_task	= isci_task_execute_task,
	/* Task Management Functions. Must be called from process context. */
	.lldd_abort_task	= isci_task_abort_task,
	.lldd_abort_task_set	= isci_task_abort_task_set,
	.lldd_clear_aca		= isci_task_clear_aca,
	.lldd_clear_task_set	= isci_task_clear_task_set,
	.lldd_I_T_nexus_reset	= isci_task_I_T_nexus_reset,
	.lldd_lu_reset		= isci_task_lu_reset,
	.lldd_query_task	= isci_task_query_task,

	/* Port and Adapter management */
	.lldd_clear_nexus_port	= isci_task_clear_nexus_port,
	.lldd_clear_nexus_ha	= isci_task_clear_nexus_ha,

	/* Phy management */
	.lldd_control_phy	= isci_phy_control,
};


/******************************************************************************
* P R O T E C T E D  M E T H O D S
******************************************************************************/



/**
 * isci_register_sas_ha() - This method initializes various lldd
 *    specific members of the sas_ha struct and calls the libsas
 *    sas_register_ha() function.
 * @isci_host: This parameter specifies the lldd specific wrapper for the
 *    libsas sas_ha struct.
 *
 * This method returns an error code indicating sucess or failure. The user
 * should check for possible memory allocation error return otherwise, a zero
 * indicates success.
 */
static int isci_register_sas_ha(struct isci_host *isci_host)
{
	int i;
	struct sas_ha_struct *sas_ha = &(isci_host->sas_ha);
	struct asd_sas_phy **sas_phys;
	struct asd_sas_port **sas_ports;

	sas_phys = devm_kzalloc(&isci_host->pdev->dev,
				SCI_MAX_PHYS * sizeof(void *),
				GFP_KERNEL);
	if (!sas_phys)
		return -ENOMEM;

	sas_ports = devm_kzalloc(&isci_host->pdev->dev,
				 SCI_MAX_PORTS * sizeof(void *),
				 GFP_KERNEL);
	if (!sas_ports)
		return -ENOMEM;

	/*----------------- Libsas Initialization Stuff----------------------
	 * Set various fields in the sas_ha struct:
	 */

	sas_ha->sas_ha_name = DRV_NAME;
	sas_ha->lldd_module = THIS_MODULE;
	sas_ha->sas_addr    = &isci_host->phys[0].sas_addr[0];

	/* set the array of phy and port structs.  */
	for (i = 0; i < SCI_MAX_PHYS; i++) {
		sas_phys[i] = &(isci_host->phys[i].sas_phy);
		sas_ports[i] = &(isci_host->sas_ports[i]);
	}

	sas_ha->sas_phy  = sas_phys;
	sas_ha->sas_port = sas_ports;
	sas_ha->num_phys = SCI_MAX_PHYS;

	sas_ha->lldd_queue_size = ISCI_CAN_QUEUE_VAL;
	sas_ha->lldd_max_execute_num = 1;
	sas_ha->strict_wide_ports = 1;

	sas_register_ha(sas_ha);

	return 0;
}

static void isci_unregister_sas_ha(struct isci_host *isci_host)
{
	if (!isci_host)
		return;

	sas_unregister_ha(&(isci_host->sas_ha));

	sas_remove_host(isci_host->shost);
	scsi_remove_host(isci_host->shost);
	scsi_host_put(isci_host->shost);
}

static int __devinit isci_pci_init(struct pci_dev *pdev)
{
	int err, bar_num, bar_mask;
	void __iomem * const *iomap;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"failed enable PCI device %s!\n",
			pci_name(pdev));
		return err;
	}

	for (bar_num = 0; bar_num < SCI_PCI_BAR_COUNT; bar_num++)
		bar_mask |= 1 << (bar_num * 2);

	err = pcim_iomap_regions(pdev, bar_mask, DRV_NAME);
	if (err)
		return err;

	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return -ENOMEM;

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err)
			return err;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err)
			return err;
	}

	return 0;
}

static int num_controllers(struct pci_dev *pdev)
{
	/* bar size alone can tell us if we are running with a dual controller
	 * part, no need to trust revision ids that might be under broken firmware
	 * control
	 */
	resource_size_t scu_bar_size = pci_resource_len(pdev, SCI_SCU_BAR*2);
	resource_size_t smu_bar_size = pci_resource_len(pdev, SCI_SMU_BAR*2);
	
	if (scu_bar_size >= SCI_SCU_BAR_SIZE*SCI_MAX_CONTROLLERS &&
	    smu_bar_size >= SCI_SMU_BAR_SIZE*SCI_MAX_CONTROLLERS)
		return SCI_MAX_CONTROLLERS;
	else
		return 1;
}

static int isci_setup_interrupts(struct pci_dev *pdev)
{
	int err, i, num_msix;
	struct isci_pci_info *pci_info = to_pci_info(pdev);

	/*
	 *  Determine the number of vectors associated with this
	 *  PCI function.
	 */
	num_msix = num_controllers(pdev) * SCI_NUM_MSI_X_INT;

	for (i = 0; i < num_msix; i++)
		pci_info->msix_entries[i].entry = i;

	err = pci_enable_msix(pdev, pci_info->msix_entries, num_msix);
	if (err)
		goto intx;

	for (i = 0; i < num_msix; i++) {
		int id = i / SCI_NUM_MSI_X_INT;
		struct msix_entry *msix = &pci_info->msix_entries[i];
		struct isci_host *isci_host = pci_info->hosts[id];
		irq_handler_t isr;

		/* odd numbered vectors are error interrupts */
		if (i & 1)
			isr = isci_error_isr;
		else
			isr = isci_msix_isr;

		BUG_ON(!isci_host);

		err = devm_request_irq(&pdev->dev, msix->vector, isr, 0,
				       DRV_NAME"-msix", isci_host);
		if (!err)
			continue;

		dev_info(&pdev->dev, "msix setup failed falling back to intx\n");
		while (i--) {
			id = i / SCI_NUM_MSI_X_INT;
			isci_host = pci_info->hosts[id];
			msix = &pci_info->msix_entries[i];
			devm_free_irq(&pdev->dev, msix->vector, isci_host);
		}
		pci_disable_msix(pdev);
		goto intx;
	}

	return 0;

 intx:
	err = devm_request_irq(&pdev->dev, pdev->irq, isci_intx_isr,
			       IRQF_SHARED, DRV_NAME"-intx", pdev);

	return err;
}

/**
 * isci_parse_oem_parameters() - This method will take OEM parameters
 *    from the module init parameters and copy them to oem_params. This will
 *    only copy values that are not set to the module parameter default values
 * @oem_parameters: This parameter specifies the controller default OEM
 *    parameters. It is expected that this has been initialized to the default
 *    parameters for the controller
 *
 *
 */
enum sci_status isci_parse_oem_parameters(union scic_oem_parameters *oem_params,
					  int scu_index,
					  struct isci_firmware *fw)
{
	int i;

	/* check for valid inputs */
	if (!(scu_index >= 0
	      && scu_index < SCI_MAX_CONTROLLERS
	      && oem_params != NULL)) {
		return SCI_FAILURE;
	}

	for (i = 0; i < SCI_MAX_PHYS; i++) {
		int array_idx = i + (SCI_MAX_PHYS * scu_index);
		u64 sas_addr = fw->sas_addrs[array_idx];

		if (sas_addr != 0) {
			oem_params->sds1.phys[i].sas_address.low =
				(u32)(sas_addr & 0xffffffff);
			oem_params->sds1.phys[i].sas_address.high =
				(u32)((sas_addr >> 32) & 0xffffffff);
		}
	}

	for (i = 0; i < SCI_MAX_PORTS; i++) {
		int array_idx = i + (SCI_MAX_PORTS * scu_index);
		u32 pmask = fw->phy_masks[array_idx];

		oem_params->sds1.ports[i].phy_mask = pmask;
	}

	return SCI_SUCCESS;
}

/**
 * isci_parse_user_parameters() - This method will take user parameters
 *    from the module init parameters and copy them to user_params. This will
 *    only copy values that are not set to the module parameter default values
 * @user_parameters: This parameter specifies the controller default user
 *    parameters. It is expected that this has been initialized to the default
 *    parameters for the controller
 *
 *
 */
enum sci_status isci_parse_user_parameters(
	union scic_user_parameters *user_params,
	int scu_index,
	struct isci_firmware *fw)
{
	int i;

	if (!(scu_index >= 0
	      && scu_index < SCI_MAX_CONTROLLERS
	      && user_params != NULL)) {
		return SCI_FAILURE;
	}

	for (i = 0; i < SCI_MAX_PORTS; i++) {
		int array_idx = i + (SCI_MAX_PORTS * scu_index);
		u32 gen = fw->phy_gens[array_idx];

		user_params->sds1.phys[i].max_speed_generation = gen;

	}

	return SCI_SUCCESS;
}

static struct isci_host *isci_host_alloc(struct pci_dev *pdev, int id)
{
	struct isci_host *isci_host;
	struct Scsi_Host *shost;
	int err;

	isci_host = devm_kzalloc(&pdev->dev, sizeof(*isci_host), GFP_KERNEL);
	if (!isci_host)
		return NULL;

	isci_host->pdev = pdev;
	isci_host->id = id;

	shost = scsi_host_alloc(&isci_sht, sizeof(void *));
	if (!shost)
		return NULL;
	isci_host->shost = shost;

	err = isci_host_init(isci_host);
	if (err)
		goto err_shost;

	SHOST_TO_SAS_HA(shost) = &isci_host->sas_ha;
	isci_host->sas_ha.core.shost = shost;
	shost->transportt = isci_transport_template;

	shost->max_id = ~0;
	shost->max_lun = ~0;
	shost->max_cmd_len = MAX_COMMAND_SIZE;

	err = scsi_add_host(shost, &pdev->dev);
	if (err)
		goto err_shost;

	err = isci_register_sas_ha(isci_host);
	if (err)
		goto err_shost_remove;

	return isci_host;

 err_shost_remove:
	scsi_remove_host(shost);
 err_shost:
	scsi_host_put(shost);

	return NULL;
}

static void check_si_rev(struct pci_dev *pdev)
{
	if (num_controllers(pdev) > 1)
		isci_si_rev = ISCI_SI_REVB0;
	else {
		switch (pdev->revision) {
		case 0:
		case 1:
			/* if the id is ambiguous don't update isci_si_rev */
			break;
		case 3:
			isci_si_rev = ISCI_SI_REVA2;
			break;
		default:
		case 4:
			isci_si_rev = ISCI_SI_REVB0;
			break;
		}
	}

	dev_info(&pdev->dev, "driver configured for %s silicon (rev: %d)\n",
		 isci_si_rev == ISCI_SI_REVA0 ? "A0" :
		 isci_si_rev == ISCI_SI_REVA2 ? "A2" : "B0", pdev->revision);
		
}

static int isci_verify_firmware(const struct firmware *fw,
				struct isci_firmware *isci_fw)
{
	const u8 *tmp;

	if (fw->size < ISCI_FIRMWARE_MIN_SIZE)
		return -EINVAL;

	tmp = fw->data;

	/* 12th char should be the NULL terminate for the ID string */
	if (tmp[11] != '\0')
		return -EINVAL;

	if (strncmp("#SCU MAGIC#", tmp, 11) != 0)
		return -EINVAL;

	isci_fw->id = tmp;
	isci_fw->version = fw->data[ISCI_FW_VER_OFS];
	isci_fw->subversion = fw->data[ISCI_FW_SUBVER_OFS];

	tmp = fw->data + ISCI_FW_DATA_OFS;

	while (*tmp != ISCI_FW_HDR_EOF) {
		switch (*tmp) {
		case ISCI_FW_HDR_PHYMASK:
			tmp++;
			isci_fw->phy_masks_size = *tmp;
			tmp++;
			isci_fw->phy_masks = (const u32 *)tmp;
			tmp += sizeof(u32) * isci_fw->phy_masks_size;
			break;

		case ISCI_FW_HDR_PHYGEN:
			tmp++;
			isci_fw->phy_gens_size = *tmp;
			tmp++;
			isci_fw->phy_gens = (const u32 *)tmp;
			tmp += sizeof(u32) * isci_fw->phy_gens_size;
			break;

		case ISCI_FW_HDR_SASADDR:
			tmp++;
			isci_fw->sas_addrs_size = *tmp;
			tmp++;
			isci_fw->sas_addrs = (const u64 *)tmp;
			tmp += sizeof(u64) * isci_fw->sas_addrs_size;
			break;

		default:
			pr_err("bad field in firmware binary blob\n");
			return -EINVAL;
		}
	}

	pr_info("isci firmware v%u.%u loaded.\n",
	       isci_fw->version, isci_fw->subversion);

	return SCI_SUCCESS;
}

static int __devinit isci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct isci_pci_info *pci_info;
	int err, i;
	struct isci_host *isci_host;
	const struct firmware *fw = NULL;

	check_si_rev(pdev);

	pci_info = devm_kzalloc(&pdev->dev, sizeof(*pci_info), GFP_KERNEL);
	if (!pci_info)
		return -ENOMEM;
	pci_set_drvdata(pdev, pci_info);

	err = request_firmware(&fw, ISCI_FW_NAME, &pdev->dev);
	if (err) {
		dev_warn(&pdev->dev,
			 "Loading firmware failed, using default values\n");
		dev_warn(&pdev->dev,
			 "Default OEM configuration being used:"
			 " 4 narrow ports, and default SAS Addresses\n");
	} else {
		isci_firmware = devm_kzalloc(&pdev->dev,
					     sizeof(struct isci_firmware),
					     GFP_KERNEL);
		if (isci_firmware) {
			err = isci_verify_firmware(fw, isci_firmware);
			if (err != SCI_SUCCESS) {
				dev_warn(&pdev->dev,
					 "firmware verification failed\n");
				dev_warn(&pdev->dev,
					 "Default OEM configuration being used:"
					 " 4 narrow ports, and default SAS "
					 "Addresses\n");
				devm_kfree(&pdev->dev, isci_firmware);
				isci_firmware = NULL;
			}
		}
		release_firmware(fw);
	}

	err = isci_pci_init(pdev);
	if (err)
		return err;

	for (i = 0; i < num_controllers(pdev); i++) {
		struct isci_host *h = isci_host_alloc(pdev, i);

		if (!h) {
			err = -ENOMEM;
			goto err_host_alloc;
		}
		pci_info->hosts[i] = h;
	}

	err = isci_setup_interrupts(pdev);
	if (err)
		goto err_host_alloc;

	for_each_isci_host(i, isci_host, pdev)
		scsi_scan_host(isci_host->shost);

	return 0;

 err_host_alloc:
	for_each_isci_host(i, isci_host, pdev)
		isci_unregister_sas_ha(isci_host);
	return err;
}

static void __devexit isci_pci_remove(struct pci_dev *pdev)
{
	struct isci_host *isci_host;
	int i;

	for_each_isci_host(i, isci_host, pdev) {
		isci_unregister_sas_ha(isci_host);
		isci_host_deinit(isci_host);
		scic_controller_disable_interrupts(isci_host->core_controller);
	}
}

static __init int isci_init(void)
{
	int err = -ENOMEM;

	pr_info("%s: Intel(R) C600 SAS Controller Driver\n", DRV_NAME);

	isci_kmem_cache = kmem_cache_create(DRV_NAME,
					    sizeof(struct isci_remote_device) +
					    scic_remote_device_get_object_size(),
					    0, 0, NULL);
	if (!isci_kmem_cache)
		return err;

	isci_transport_template = sas_domain_attach_transport(&isci_transport_ops);
	if (!isci_transport_template)
		goto err_kmem;

	err = pci_register_driver(&isci_pci_driver);
	if (err)
		goto err_sas;

	return 0;

 err_sas:
	sas_release_transport(isci_transport_template);
 err_kmem:
	kmem_cache_destroy(isci_kmem_cache);

	return err;
}

static __exit void isci_exit(void)
{
	pci_unregister_driver(&isci_pci_driver);
	sas_release_transport(isci_transport_template);
	kmem_cache_destroy(isci_kmem_cache);
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_FIRMWARE(ISCI_FW_NAME);
module_init(isci_init);
module_exit(isci_exit);
