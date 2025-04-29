// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aic94xx SAS/SATA driver initialization.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#include <scsi/sas_ata.h>
#include <scsi/scsi_host.h>

#include "aic94xx.h"
#include "aic94xx_reg.h"
#include "aic94xx_hwi.h"
#include "aic94xx_seq.h"
#include "aic94xx_sds.h"

/* The format is "version.release.patchlevel" */
#define ASD_DRIVER_VERSION "1.0.3"
#define DRV_NAME "aic94xx"

static int use_msi = 0;
module_param_named(use_msi, use_msi, int, S_IRUGO);
MODULE_PARM_DESC(use_msi, "\n"
	"\tEnable(1) or disable(0) using PCI MSI.\n"
	"\tDefault: 0");

static struct scsi_transport_template *aic94xx_transport_template;
static int asd_scan_finished(struct Scsi_Host *, unsigned long);
static void asd_scan_start(struct Scsi_Host *);
static const struct attribute_group *asd_sdev_groups[];

static const struct scsi_host_template aic94xx_sht = {
	LIBSAS_SHT_BASE
	.scan_finished		= asd_scan_finished,
	.scan_start		= asd_scan_start,
	.can_queue		= 1,
	.sg_tablesize		= SG_ALL,
	.track_queue_depth	= 1,
	.sdev_groups		= asd_sdev_groups,
};

static int asd_map_memio(struct asd_ha_struct *asd_ha)
{
	int err, i;
	struct asd_ha_addrspace *io_handle;

	asd_ha->iospace = 0;
	for (i = 0; i < 3; i += 2) {
		io_handle = &asd_ha->io_handle[i==0?0:1];
		io_handle->start = pci_resource_start(asd_ha->pcidev, i);
		io_handle->len   = pci_resource_len(asd_ha->pcidev, i);
		io_handle->flags = pci_resource_flags(asd_ha->pcidev, i);
		err = -ENODEV;
		if (!io_handle->start || !io_handle->len) {
			asd_printk("MBAR%d start or length for %s is 0.\n",
				   i==0?0:1, pci_name(asd_ha->pcidev));
			goto Err;
		}
		err = pci_request_region(asd_ha->pcidev, i, ASD_DRIVER_NAME);
		if (err) {
			asd_printk("couldn't reserve memory region for %s\n",
				   pci_name(asd_ha->pcidev));
			goto Err;
		}
		io_handle->addr = ioremap(io_handle->start, io_handle->len);
		if (!io_handle->addr) {
			asd_printk("couldn't map MBAR%d of %s\n", i==0?0:1,
				   pci_name(asd_ha->pcidev));
			err = -ENOMEM;
			goto Err_unreq;
		}
	}

	return 0;
Err_unreq:
	pci_release_region(asd_ha->pcidev, i);
Err:
	if (i > 0) {
		io_handle = &asd_ha->io_handle[0];
		iounmap(io_handle->addr);
		pci_release_region(asd_ha->pcidev, 0);
	}
	return err;
}

static void asd_unmap_memio(struct asd_ha_struct *asd_ha)
{
	struct asd_ha_addrspace *io_handle;

	io_handle = &asd_ha->io_handle[1];
	iounmap(io_handle->addr);
	pci_release_region(asd_ha->pcidev, 2);

	io_handle = &asd_ha->io_handle[0];
	iounmap(io_handle->addr);
	pci_release_region(asd_ha->pcidev, 0);
}

static int asd_map_ioport(struct asd_ha_struct *asd_ha)
{
	int i = PCI_IOBAR_OFFSET, err;
	struct asd_ha_addrspace *io_handle = &asd_ha->io_handle[0];

	asd_ha->iospace = 1;
	io_handle->start = pci_resource_start(asd_ha->pcidev, i);
	io_handle->len   = pci_resource_len(asd_ha->pcidev, i);
	io_handle->flags = pci_resource_flags(asd_ha->pcidev, i);
	io_handle->addr  = (void __iomem *) io_handle->start;
	if (!io_handle->start || !io_handle->len) {
		asd_printk("couldn't get IO ports for %s\n",
			   pci_name(asd_ha->pcidev));
		return -ENODEV;
	}
	err = pci_request_region(asd_ha->pcidev, i, ASD_DRIVER_NAME);
	if (err) {
		asd_printk("couldn't reserve io space for %s\n",
			   pci_name(asd_ha->pcidev));
	}

	return err;
}

static void asd_unmap_ioport(struct asd_ha_struct *asd_ha)
{
	pci_release_region(asd_ha->pcidev, PCI_IOBAR_OFFSET);
}

static int asd_map_ha(struct asd_ha_struct *asd_ha)
{
	int err;
	u16 cmd_reg;

	err = pci_read_config_word(asd_ha->pcidev, PCI_COMMAND, &cmd_reg);
	if (err) {
		asd_printk("couldn't read command register of %s\n",
			   pci_name(asd_ha->pcidev));
		goto Err;
	}

	err = -ENODEV;
	if (cmd_reg & PCI_COMMAND_MEMORY) {
		if ((err = asd_map_memio(asd_ha)))
			goto Err;
	} else if (cmd_reg & PCI_COMMAND_IO) {
		if ((err = asd_map_ioport(asd_ha)))
			goto Err;
		asd_printk("%s ioport mapped -- upgrade your hardware\n",
			   pci_name(asd_ha->pcidev));
	} else {
		asd_printk("no proper device access to %s\n",
			   pci_name(asd_ha->pcidev));
		goto Err;
	}

	return 0;
Err:
	return err;
}

static void asd_unmap_ha(struct asd_ha_struct *asd_ha)
{
	if (asd_ha->iospace)
		asd_unmap_ioport(asd_ha);
	else
		asd_unmap_memio(asd_ha);
}

static const char *asd_dev_rev[30] = {
	[0] = "A0",
	[1] = "A1",
	[8] = "B0",
};

static int asd_common_setup(struct asd_ha_struct *asd_ha)
{
	int err, i;

	asd_ha->revision_id = asd_ha->pcidev->revision;

	err = -ENODEV;
	if (asd_ha->revision_id < AIC9410_DEV_REV_B0) {
		asd_printk("%s is revision %s (%X), which is not supported\n",
			   pci_name(asd_ha->pcidev),
			   asd_dev_rev[asd_ha->revision_id],
			   asd_ha->revision_id);
		goto Err;
	}
	/* Provide some sane default values. */
	asd_ha->hw_prof.max_scbs = 512;
	asd_ha->hw_prof.max_ddbs = ASD_MAX_DDBS;
	asd_ha->hw_prof.num_phys = ASD_MAX_PHYS;
	/* All phys are enabled, by default. */
	asd_ha->hw_prof.enabled_phys = 0xFF;
	for (i = 0; i < ASD_MAX_PHYS; i++) {
		asd_ha->hw_prof.phy_desc[i].max_sas_lrate =
			SAS_LINK_RATE_3_0_GBPS;
		asd_ha->hw_prof.phy_desc[i].min_sas_lrate =
			SAS_LINK_RATE_1_5_GBPS;
		asd_ha->hw_prof.phy_desc[i].max_sata_lrate =
			SAS_LINK_RATE_1_5_GBPS;
		asd_ha->hw_prof.phy_desc[i].min_sata_lrate =
			SAS_LINK_RATE_1_5_GBPS;
	}

	return 0;
Err:
	return err;
}

static int asd_aic9410_setup(struct asd_ha_struct *asd_ha)
{
	int err = asd_common_setup(asd_ha);

	if (err)
		return err;

	asd_ha->hw_prof.addr_range = 8;
	asd_ha->hw_prof.port_name_base = 0;
	asd_ha->hw_prof.dev_name_base = 8;
	asd_ha->hw_prof.sata_name_base = 16;

	return 0;
}

static int asd_aic9405_setup(struct asd_ha_struct *asd_ha)
{
	int err = asd_common_setup(asd_ha);

	if (err)
		return err;

	asd_ha->hw_prof.addr_range = 4;
	asd_ha->hw_prof.port_name_base = 0;
	asd_ha->hw_prof.dev_name_base = 4;
	asd_ha->hw_prof.sata_name_base = 8;

	return 0;
}

static ssize_t asd_show_dev_rev(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct asd_ha_struct *asd_ha = dev_to_asd_ha(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			asd_dev_rev[asd_ha->revision_id]);
}
static DEVICE_ATTR(aic_revision, S_IRUGO, asd_show_dev_rev, NULL);

static ssize_t asd_show_dev_bios_build(struct device *dev,
				       struct device_attribute *attr,char *buf)
{
	struct asd_ha_struct *asd_ha = dev_to_asd_ha(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", asd_ha->hw_prof.bios.bld);
}
static DEVICE_ATTR(bios_build, S_IRUGO, asd_show_dev_bios_build, NULL);

static ssize_t asd_show_dev_pcba_sn(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct asd_ha_struct *asd_ha = dev_to_asd_ha(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n", asd_ha->hw_prof.pcba_sn);
}
static DEVICE_ATTR(pcba_sn, S_IRUGO, asd_show_dev_pcba_sn, NULL);

#define FLASH_CMD_NONE      0x00
#define FLASH_CMD_UPDATE    0x01
#define FLASH_CMD_VERIFY    0x02

struct flash_command {
     u8      command[8];
     int     code;
};

static struct flash_command flash_command_table[] =
{
     {"verify",      FLASH_CMD_VERIFY},
     {"update",      FLASH_CMD_UPDATE},
     {"",            FLASH_CMD_NONE}      /* Last entry should be NULL. */
};

struct error_bios {
     char    *reason;
     int     err_code;
};

static struct error_bios flash_error_table[] =
{
     {"Failed to open bios image file",      FAIL_OPEN_BIOS_FILE},
     {"PCI ID mismatch",                     FAIL_CHECK_PCI_ID},
     {"Checksum mismatch",                   FAIL_CHECK_SUM},
     {"Unknown Error",                       FAIL_UNKNOWN},
     {"Failed to verify.",                   FAIL_VERIFY},
     {"Failed to reset flash chip.",         FAIL_RESET_FLASH},
     {"Failed to find flash chip type.",     FAIL_FIND_FLASH_ID},
     {"Failed to erash flash chip.",         FAIL_ERASE_FLASH},
     {"Failed to program flash chip.",       FAIL_WRITE_FLASH},
     {"Flash in progress",                   FLASH_IN_PROGRESS},
     {"Image file size Error",               FAIL_FILE_SIZE},
     {"Input parameter error",               FAIL_PARAMETERS},
     {"Out of memory",                       FAIL_OUT_MEMORY},
     {"OK", 0}	/* Last entry err_code = 0. */
};

static ssize_t asd_store_update_bios(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct asd_ha_struct *asd_ha = dev_to_asd_ha(dev);
	char *cmd_ptr, *filename_ptr;
	struct bios_file_header header, *hdr_ptr;
	int res, i;
	u32 csum = 0;
	int flash_command = FLASH_CMD_NONE;
	int err = 0;

	cmd_ptr = kcalloc(count, 2, GFP_KERNEL);

	if (!cmd_ptr) {
		err = FAIL_OUT_MEMORY;
		goto out;
	}

	filename_ptr = cmd_ptr + count;
	res = sscanf(buf, "%s %s", cmd_ptr, filename_ptr);
	if (res != 2) {
		err = FAIL_PARAMETERS;
		goto out1;
	}

	for (i = 0; flash_command_table[i].code != FLASH_CMD_NONE; i++) {
		if (!memcmp(flash_command_table[i].command,
				 cmd_ptr, strlen(cmd_ptr))) {
			flash_command = flash_command_table[i].code;
			break;
		}
	}
	if (flash_command == FLASH_CMD_NONE) {
		err = FAIL_PARAMETERS;
		goto out1;
	}

	if (asd_ha->bios_status == FLASH_IN_PROGRESS) {
		err = FLASH_IN_PROGRESS;
		goto out1;
	}
	err = request_firmware(&asd_ha->bios_image,
				   filename_ptr,
				   &asd_ha->pcidev->dev);
	if (err) {
		asd_printk("Failed to load bios image file %s, error %d\n",
			   filename_ptr, err);
		err = FAIL_OPEN_BIOS_FILE;
		goto out1;
	}

	hdr_ptr = (struct bios_file_header *)asd_ha->bios_image->data;

	if ((hdr_ptr->contrl_id.vendor != asd_ha->pcidev->vendor ||
		hdr_ptr->contrl_id.device != asd_ha->pcidev->device) &&
		(hdr_ptr->contrl_id.sub_vendor != asd_ha->pcidev->vendor ||
		hdr_ptr->contrl_id.sub_device != asd_ha->pcidev->device)) {

		ASD_DPRINTK("The PCI vendor or device id does not match\n");
		ASD_DPRINTK("vendor=%x dev=%x sub_vendor=%x sub_dev=%x"
		" pci vendor=%x pci dev=%x\n",
		hdr_ptr->contrl_id.vendor,
		hdr_ptr->contrl_id.device,
		hdr_ptr->contrl_id.sub_vendor,
		hdr_ptr->contrl_id.sub_device,
		asd_ha->pcidev->vendor,
		asd_ha->pcidev->device);
		err = FAIL_CHECK_PCI_ID;
		goto out2;
	}

	if (hdr_ptr->filelen != asd_ha->bios_image->size) {
		err = FAIL_FILE_SIZE;
		goto out2;
	}

	/* calculate checksum */
	for (i = 0; i < hdr_ptr->filelen; i++)
		csum += asd_ha->bios_image->data[i];

	if ((csum & 0x0000ffff) != hdr_ptr->checksum) {
		ASD_DPRINTK("BIOS file checksum mismatch\n");
		err = FAIL_CHECK_SUM;
		goto out2;
	}
	if (flash_command == FLASH_CMD_UPDATE) {
		asd_ha->bios_status = FLASH_IN_PROGRESS;
		err = asd_write_flash_seg(asd_ha,
			&asd_ha->bios_image->data[sizeof(*hdr_ptr)],
			0, hdr_ptr->filelen-sizeof(*hdr_ptr));
		if (!err)
			err = asd_verify_flash_seg(asd_ha,
				&asd_ha->bios_image->data[sizeof(*hdr_ptr)],
				0, hdr_ptr->filelen-sizeof(*hdr_ptr));
	} else {
		asd_ha->bios_status = FLASH_IN_PROGRESS;
		err = asd_verify_flash_seg(asd_ha,
			&asd_ha->bios_image->data[sizeof(header)],
			0, hdr_ptr->filelen-sizeof(header));
	}

out2:
	release_firmware(asd_ha->bios_image);
out1:
	kfree(cmd_ptr);
out:
	asd_ha->bios_status = err;

	if (!err)
		return count;
	else
		return -err;
}

static ssize_t asd_show_update_bios(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int i;
	struct asd_ha_struct *asd_ha = dev_to_asd_ha(dev);

	for (i = 0; flash_error_table[i].err_code != 0; i++) {
		if (flash_error_table[i].err_code == asd_ha->bios_status)
			break;
	}
	if (asd_ha->bios_status != FLASH_IN_PROGRESS)
		asd_ha->bios_status = FLASH_OK;

	return snprintf(buf, PAGE_SIZE, "status=%x %s\n",
			flash_error_table[i].err_code,
			flash_error_table[i].reason);
}

static DEVICE_ATTR(update_bios, S_IRUGO|S_IWUSR,
	asd_show_update_bios, asd_store_update_bios);

static int asd_create_dev_attrs(struct asd_ha_struct *asd_ha)
{
	int err;

	err = device_create_file(&asd_ha->pcidev->dev, &dev_attr_aic_revision);
	if (err)
		return err;

	err = device_create_file(&asd_ha->pcidev->dev, &dev_attr_bios_build);
	if (err)
		goto err_rev;

	err = device_create_file(&asd_ha->pcidev->dev, &dev_attr_pcba_sn);
	if (err)
		goto err_biosb;
	err = device_create_file(&asd_ha->pcidev->dev, &dev_attr_update_bios);
	if (err)
		goto err_update_bios;

	return 0;

err_update_bios:
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_pcba_sn);
err_biosb:
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_bios_build);
err_rev:
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_aic_revision);
	return err;
}

static void asd_remove_dev_attrs(struct asd_ha_struct *asd_ha)
{
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_aic_revision);
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_bios_build);
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_pcba_sn);
	device_remove_file(&asd_ha->pcidev->dev, &dev_attr_update_bios);
}

/* The first entry, 0, is used for dynamic ids, the rest for devices
 * we know about.
 */
static const struct asd_pcidev_struct {
	const char * name;
	int (*setup)(struct asd_ha_struct *asd_ha);
} asd_pcidev_data[] = {
	/* Id 0 is used for dynamic ids. */
	{ .name  = "Adaptec AIC-94xx SAS/SATA Host Adapter",
	  .setup = asd_aic9410_setup
	},
	{ .name  = "Adaptec AIC-9410W SAS/SATA Host Adapter",
	  .setup = asd_aic9410_setup
	},
	{ .name  = "Adaptec AIC-9405W SAS/SATA Host Adapter",
	  .setup = asd_aic9405_setup
	},
};

static int asd_create_ha_caches(struct asd_ha_struct *asd_ha)
{
	asd_ha->scb_pool = dma_pool_create(ASD_DRIVER_NAME "_scb_pool",
					   &asd_ha->pcidev->dev,
					   sizeof(struct scb),
					   8, 0);
	if (!asd_ha->scb_pool) {
		asd_printk("couldn't create scb pool\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * asd_free_edbs -- free empty data buffers
 * asd_ha: pointer to host adapter structure
 */
static void asd_free_edbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int i;

	for (i = 0; i < seq->num_edbs; i++)
		asd_free_coherent(asd_ha, seq->edb_arr[i]);
	kfree(seq->edb_arr);
	seq->edb_arr = NULL;
}

static void asd_free_escbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int i;

	for (i = 0; i < seq->num_escbs; i++) {
		if (!list_empty(&seq->escb_arr[i]->list))
			list_del_init(&seq->escb_arr[i]->list);

		asd_ascb_free(seq->escb_arr[i]);
	}
	kfree(seq->escb_arr);
	seq->escb_arr = NULL;
}

static void asd_destroy_ha_caches(struct asd_ha_struct *asd_ha)
{
	int i;

	if (asd_ha->hw_prof.ddb_ext)
		asd_free_coherent(asd_ha, asd_ha->hw_prof.ddb_ext);
	if (asd_ha->hw_prof.scb_ext)
		asd_free_coherent(asd_ha, asd_ha->hw_prof.scb_ext);

	kfree(asd_ha->hw_prof.ddb_bitmap);
	asd_ha->hw_prof.ddb_bitmap = NULL;

	for (i = 0; i < ASD_MAX_PHYS; i++) {
		struct asd_phy *phy = &asd_ha->phys[i];

		asd_free_coherent(asd_ha, phy->id_frm_tok);
	}
	if (asd_ha->seq.escb_arr)
		asd_free_escbs(asd_ha);
	if (asd_ha->seq.edb_arr)
		asd_free_edbs(asd_ha);
	if (asd_ha->hw_prof.ue.area) {
		kfree(asd_ha->hw_prof.ue.area);
		asd_ha->hw_prof.ue.area = NULL;
	}
	if (asd_ha->seq.tc_index_array) {
		kfree(asd_ha->seq.tc_index_array);
		kfree(asd_ha->seq.tc_index_bitmap);
		asd_ha->seq.tc_index_array = NULL;
		asd_ha->seq.tc_index_bitmap = NULL;
	}
	if (asd_ha->seq.actual_dl) {
			asd_free_coherent(asd_ha, asd_ha->seq.actual_dl);
			asd_ha->seq.actual_dl = NULL;
			asd_ha->seq.dl = NULL;
	}
	if (asd_ha->seq.next_scb.vaddr) {
		dma_pool_free(asd_ha->scb_pool, asd_ha->seq.next_scb.vaddr,
			      asd_ha->seq.next_scb.dma_handle);
		asd_ha->seq.next_scb.vaddr = NULL;
	}
	dma_pool_destroy(asd_ha->scb_pool);
	asd_ha->scb_pool = NULL;
}

struct kmem_cache *asd_dma_token_cache;
struct kmem_cache *asd_ascb_cache;

static int asd_create_global_caches(void)
{
	if (!asd_dma_token_cache) {
		asd_dma_token_cache
			= kmem_cache_create(ASD_DRIVER_NAME "_dma_token",
					    sizeof(struct asd_dma_tok),
					    0,
					    SLAB_HWCACHE_ALIGN,
					    NULL);
		if (!asd_dma_token_cache) {
			asd_printk("couldn't create dma token cache\n");
			return -ENOMEM;
		}
	}

	if (!asd_ascb_cache) {
		asd_ascb_cache = kmem_cache_create(ASD_DRIVER_NAME "_ascb",
						   sizeof(struct asd_ascb),
						   0,
						   SLAB_HWCACHE_ALIGN,
						   NULL);
		if (!asd_ascb_cache) {
			asd_printk("couldn't create ascb cache\n");
			goto Err;
		}
	}

	return 0;
Err:
	kmem_cache_destroy(asd_dma_token_cache);
	asd_dma_token_cache = NULL;
	return -ENOMEM;
}

static void asd_destroy_global_caches(void)
{
	kmem_cache_destroy(asd_dma_token_cache);
	asd_dma_token_cache = NULL;

	kmem_cache_destroy(asd_ascb_cache);
	asd_ascb_cache = NULL;
}

static int asd_register_sas_ha(struct asd_ha_struct *asd_ha)
{
	int i;
	struct asd_sas_phy   **sas_phys =
		kcalloc(ASD_MAX_PHYS, sizeof(*sas_phys), GFP_KERNEL);
	struct asd_sas_port  **sas_ports =
		kcalloc(ASD_MAX_PHYS, sizeof(*sas_ports), GFP_KERNEL);

	if (!sas_phys || !sas_ports) {
		kfree(sas_phys);
		kfree(sas_ports);
		return -ENOMEM;
	}

	asd_ha->sas_ha.sas_ha_name = (char *) asd_ha->name;
	asd_ha->sas_ha.sas_addr = &asd_ha->hw_prof.sas_addr[0];

	for (i = 0; i < ASD_MAX_PHYS; i++) {
		sas_phys[i] = &asd_ha->phys[i].sas_phy;
		sas_ports[i] = &asd_ha->ports[i];
	}

	asd_ha->sas_ha.sas_phy = sas_phys;
	asd_ha->sas_ha.sas_port= sas_ports;
	asd_ha->sas_ha.num_phys= ASD_MAX_PHYS;

	return sas_register_ha(&asd_ha->sas_ha);
}

static int asd_unregister_sas_ha(struct asd_ha_struct *asd_ha)
{
	int err;

	err = sas_unregister_ha(&asd_ha->sas_ha);

	sas_remove_host(asd_ha->sas_ha.shost);
	scsi_host_put(asd_ha->sas_ha.shost);

	kfree(asd_ha->sas_ha.sas_phy);
	kfree(asd_ha->sas_ha.sas_port);

	return err;
}

static int asd_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	const struct asd_pcidev_struct *asd_dev;
	unsigned asd_id = (unsigned) id->driver_data;
	struct asd_ha_struct *asd_ha;
	struct Scsi_Host *shost;
	int err;

	if (asd_id >= ARRAY_SIZE(asd_pcidev_data)) {
		asd_printk("wrong driver_data in PCI table\n");
		return -ENODEV;
	}

	if ((err = pci_enable_device(dev))) {
		asd_printk("couldn't enable device %s\n", pci_name(dev));
		return err;
	}

	pci_set_master(dev);

	err = -ENOMEM;

	shost = scsi_host_alloc(&aic94xx_sht, sizeof(void *));
	if (!shost)
		goto Err;

	asd_dev = &asd_pcidev_data[asd_id];

	asd_ha = kzalloc(sizeof(*asd_ha), GFP_KERNEL);
	if (!asd_ha) {
		asd_printk("out of memory\n");
		goto Err_put;
	}
	asd_ha->pcidev = dev;
	asd_ha->sas_ha.dev = &asd_ha->pcidev->dev;
	asd_ha->sas_ha.lldd_ha = asd_ha;

	asd_ha->bios_status = FLASH_OK;
	asd_ha->name = asd_dev->name;
	asd_printk("found %s, device %s\n", asd_ha->name, pci_name(dev));

	SHOST_TO_SAS_HA(shost) = &asd_ha->sas_ha;
	asd_ha->sas_ha.shost = shost;
	shost->transportt = aic94xx_transport_template;
	shost->max_id = ~0;
	shost->max_lun = ~0;
	shost->max_cmd_len = 16;

	err = scsi_add_host(shost, &dev->dev);
	if (err)
		goto Err_free;

	err = asd_dev->setup(asd_ha);
	if (err)
		goto Err_remove;

	err = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
	if (err) {
		err = -ENODEV;
		asd_printk("no suitable DMA mask for %s\n", pci_name(dev));
		goto Err_remove;
	}

	pci_set_drvdata(dev, asd_ha);

	err = asd_map_ha(asd_ha);
	if (err)
		goto Err_remove;

	err = asd_create_ha_caches(asd_ha);
        if (err)
		goto Err_unmap;

	err = asd_init_hw(asd_ha);
	if (err)
		goto Err_free_cache;

	asd_printk("device %s: SAS addr %llx, PCBA SN %s, %d phys, %d enabled "
		   "phys, flash %s, BIOS %s%d\n",
		   pci_name(dev), SAS_ADDR(asd_ha->hw_prof.sas_addr),
		   asd_ha->hw_prof.pcba_sn, asd_ha->hw_prof.max_phys,
		   asd_ha->hw_prof.num_phys,
		   asd_ha->hw_prof.flash.present ? "present" : "not present",
		   asd_ha->hw_prof.bios.present ? "build " : "not present",
		   asd_ha->hw_prof.bios.bld);

	shost->can_queue = asd_ha->seq.can_queue;

	if (use_msi)
		pci_enable_msi(asd_ha->pcidev);

	err = request_irq(asd_ha->pcidev->irq, asd_hw_isr, IRQF_SHARED,
			  ASD_DRIVER_NAME, asd_ha);
	if (err) {
		asd_printk("couldn't get irq %d for %s\n",
			   asd_ha->pcidev->irq, pci_name(asd_ha->pcidev));
		goto Err_irq;
	}
	asd_enable_ints(asd_ha);

	err = asd_init_post_escbs(asd_ha);
	if (err) {
		asd_printk("couldn't post escbs for %s\n",
			   pci_name(asd_ha->pcidev));
		goto Err_escbs;
	}
	ASD_DPRINTK("escbs posted\n");

	err = asd_create_dev_attrs(asd_ha);
	if (err)
		goto Err_dev_attrs;

	err = asd_register_sas_ha(asd_ha);
	if (err)
		goto Err_reg_sas;

	scsi_scan_host(shost);

	return 0;

Err_reg_sas:
	asd_remove_dev_attrs(asd_ha);
Err_dev_attrs:
Err_escbs:
	asd_disable_ints(asd_ha);
	free_irq(dev->irq, asd_ha);
Err_irq:
	if (use_msi)
		pci_disable_msi(dev);
	asd_chip_hardrst(asd_ha);
Err_free_cache:
	asd_destroy_ha_caches(asd_ha);
Err_unmap:
	asd_unmap_ha(asd_ha);
Err_remove:
	scsi_remove_host(shost);
Err_free:
	kfree(asd_ha);
Err_put:
	scsi_host_put(shost);
Err:
	pci_disable_device(dev);
	return err;
}

static void asd_free_queues(struct asd_ha_struct *asd_ha)
{
	unsigned long flags;
	LIST_HEAD(pending);
	struct list_head *n, *pos;

	spin_lock_irqsave(&asd_ha->seq.pend_q_lock, flags);
	asd_ha->seq.pending = 0;
	list_splice_init(&asd_ha->seq.pend_q, &pending);
	spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);

	if (!list_empty(&pending))
		ASD_DPRINTK("Uh-oh! Pending is not empty!\n");

	list_for_each_safe(pos, n, &pending) {
		struct asd_ascb *ascb = list_entry(pos, struct asd_ascb, list);
		/*
		 * Delete unexpired ascb timers.  This may happen if we issue
		 * a CONTROL PHY scb to an adapter and rmmod before the scb
		 * times out.  Apparently we don't wait for the CONTROL PHY
		 * to complete, so it doesn't matter if we kill the timer.
		 */
		timer_delete_sync(&ascb->timer);
		WARN_ON(ascb->scb->header.opcode != CONTROL_PHY);

		list_del_init(pos);
		ASD_DPRINTK("freeing from pending\n");
		asd_ascb_free(ascb);
	}
}

static void asd_turn_off_leds(struct asd_ha_struct *asd_ha)
{
	u8 phy_mask = asd_ha->hw_prof.enabled_phys;
	u8 i;

	for_each_phy(phy_mask, phy_mask, i) {
		asd_turn_led(asd_ha, i, 0);
		asd_control_led(asd_ha, i, 0);
	}
}

static void asd_pci_remove(struct pci_dev *dev)
{
	struct asd_ha_struct *asd_ha = pci_get_drvdata(dev);

	if (!asd_ha)
		return;

	asd_unregister_sas_ha(asd_ha);

	asd_disable_ints(asd_ha);

	asd_remove_dev_attrs(asd_ha);

	/* XXX more here as needed */

	free_irq(dev->irq, asd_ha);
	if (use_msi)
		pci_disable_msi(asd_ha->pcidev);
	asd_turn_off_leds(asd_ha);
	asd_chip_hardrst(asd_ha);
	asd_free_queues(asd_ha);
	asd_destroy_ha_caches(asd_ha);
	asd_unmap_ha(asd_ha);
	kfree(asd_ha);
	pci_disable_device(dev);
	return;
}

static void asd_scan_start(struct Scsi_Host *shost)
{
	struct asd_ha_struct *asd_ha;
	int err;

	asd_ha = SHOST_TO_SAS_HA(shost)->lldd_ha;
	err = asd_enable_phys(asd_ha, asd_ha->hw_prof.enabled_phys);
	if (err)
		asd_printk("Couldn't enable phys, err:%d\n", err);
}

static int asd_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	/* give the phy enabling interrupt event time to come in (1s
	 * is empirically about all it takes) */
	if (time < HZ)
		return 0;
	/* Wait for discovery to finish */
	sas_drain_work(SHOST_TO_SAS_HA(shost));
	return 1;
}

static ssize_t version_show(struct device_driver *driver, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", ASD_DRIVER_VERSION);
}
static DRIVER_ATTR_RO(version);

static int asd_create_driver_attrs(struct device_driver *driver)
{
	return driver_create_file(driver, &driver_attr_version);
}

static void asd_remove_driver_attrs(struct device_driver *driver)
{
	driver_remove_file(driver, &driver_attr_version);
}

static const struct attribute_group *asd_sdev_groups[] = {
	&sas_ata_sdev_attr_group,
	NULL
};

static struct sas_domain_function_template aic94xx_transport_functions = {
	.lldd_dev_found		= asd_dev_found,
	.lldd_dev_gone		= asd_dev_gone,

	.lldd_execute_task	= asd_execute_task,

	.lldd_abort_task	= asd_abort_task,
	.lldd_abort_task_set	= asd_abort_task_set,
	.lldd_clear_task_set	= asd_clear_task_set,
	.lldd_I_T_nexus_reset	= asd_I_T_nexus_reset,
	.lldd_lu_reset		= asd_lu_reset,
	.lldd_query_task	= asd_query_task,

	.lldd_clear_nexus_port	= asd_clear_nexus_port,
	.lldd_clear_nexus_ha	= asd_clear_nexus_ha,

	.lldd_control_phy	= asd_control_phy,

	.lldd_ata_set_dmamode	= asd_set_dmamode,
};

static const struct pci_device_id aic94xx_pci_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x410),0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x412),0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x416),0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x41E),0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x41F),0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x430),0, 0, 2},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x432),0, 0, 2},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x43E),0, 0, 2},
	{PCI_DEVICE(PCI_VENDOR_ID_ADAPTEC2, 0x43F),0, 0, 2},
	{}
};

MODULE_DEVICE_TABLE(pci, aic94xx_pci_table);

static struct pci_driver aic94xx_pci_driver = {
	.name		= ASD_DRIVER_NAME,
	.id_table	= aic94xx_pci_table,
	.probe		= asd_pci_probe,
	.remove		= asd_pci_remove,
};

static int __init aic94xx_init(void)
{
	int err;


	asd_printk("%s version %s loaded\n", ASD_DRIVER_DESCRIPTION,
		   ASD_DRIVER_VERSION);

	err = asd_create_global_caches();
	if (err)
		return err;

	aic94xx_transport_template =
		sas_domain_attach_transport(&aic94xx_transport_functions);
	if (!aic94xx_transport_template) {
		err = -ENOMEM;
		goto out_destroy_caches;
	}

	err = pci_register_driver(&aic94xx_pci_driver);
	if (err)
		goto out_release_transport;

	err = asd_create_driver_attrs(&aic94xx_pci_driver.driver);
	if (err)
		goto out_unregister_pcidrv;

	return err;

 out_unregister_pcidrv:
	pci_unregister_driver(&aic94xx_pci_driver);
 out_release_transport:
	sas_release_transport(aic94xx_transport_template);
 out_destroy_caches:
	asd_destroy_global_caches();

	return err;
}

static void __exit aic94xx_exit(void)
{
	asd_remove_driver_attrs(&aic94xx_pci_driver.driver);
	pci_unregister_driver(&aic94xx_pci_driver);
	sas_release_transport(aic94xx_transport_template);
	asd_release_firmware();
	asd_destroy_global_caches();
	asd_printk("%s version %s unloaded\n", ASD_DRIVER_DESCRIPTION,
		   ASD_DRIVER_VERSION);
}

module_init(aic94xx_init);
module_exit(aic94xx_exit);

MODULE_AUTHOR("Luben Tuikov <luben_tuikov@adaptec.com>");
MODULE_DESCRIPTION(ASD_DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ASD_DRIVER_VERSION);
