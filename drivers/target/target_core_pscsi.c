/*******************************************************************************
 * Filename:  target_core_pscsi.c
 *
 * This file contains the generic target mode <-> Linux SCSI subsystem plugin.
 *
 * Copyright (c) 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/cdrom.h>
#include <linux/file.h>
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "target_core_pscsi.h"

#define ISPRINT(a)  ((a >= ' ') && (a <= '~'))

static struct se_subsystem_api pscsi_template;

static void pscsi_req_done(struct request *, int);

/*	pscsi_attach_hba():
 *
 * 	pscsi_get_sh() used scsi_host_lookup() to locate struct Scsi_Host.
 *	from the passed SCSI Host ID.
 */
static int pscsi_attach_hba(struct se_hba *hba, u32 host_id)
{
	struct pscsi_hba_virt *phv;

	phv = kzalloc(sizeof(struct pscsi_hba_virt), GFP_KERNEL);
	if (!phv) {
		pr_err("Unable to allocate struct pscsi_hba_virt\n");
		return -ENOMEM;
	}
	phv->phv_host_id = host_id;
	phv->phv_mode = PHV_VIRUTAL_HOST_ID;

	hba->hba_ptr = phv;

	pr_debug("CORE_HBA[%d] - TCM SCSI HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		PSCSI_VERSION, TARGET_CORE_MOD_VERSION);
	pr_debug("CORE_HBA[%d] - Attached SCSI HBA to Generic\n",
	       hba->hba_id);

	return 0;
}

static void pscsi_detach_hba(struct se_hba *hba)
{
	struct pscsi_hba_virt *phv = hba->hba_ptr;
	struct Scsi_Host *scsi_host = phv->phv_lld_host;

	if (scsi_host) {
		scsi_host_put(scsi_host);

		pr_debug("CORE_HBA[%d] - Detached SCSI HBA: %s from"
			" Generic Target Core\n", hba->hba_id,
			(scsi_host->hostt->name) ? (scsi_host->hostt->name) :
			"Unknown");
	} else
		pr_debug("CORE_HBA[%d] - Detached Virtual SCSI HBA"
			" from Generic Target Core\n", hba->hba_id);

	kfree(phv);
	hba->hba_ptr = NULL;
}

static int pscsi_pmode_enable_hba(struct se_hba *hba, unsigned long mode_flag)
{
	struct pscsi_hba_virt *phv = hba->hba_ptr;
	struct Scsi_Host *sh = phv->phv_lld_host;
	/*
	 * Release the struct Scsi_Host
	 */
	if (!mode_flag) {
		if (!sh)
			return 0;

		phv->phv_lld_host = NULL;
		phv->phv_mode = PHV_VIRUTAL_HOST_ID;

		pr_debug("CORE_HBA[%d] - Disabled pSCSI HBA Passthrough"
			" %s\n", hba->hba_id, (sh->hostt->name) ?
			(sh->hostt->name) : "Unknown");

		scsi_host_put(sh);
		return 0;
	}
	/*
	 * Otherwise, locate struct Scsi_Host from the original passed
	 * pSCSI Host ID and enable for phba mode
	 */
	sh = scsi_host_lookup(phv->phv_host_id);
	if (IS_ERR(sh)) {
		pr_err("pSCSI: Unable to locate SCSI Host for"
			" phv_host_id: %d\n", phv->phv_host_id);
		return PTR_ERR(sh);
	}

	phv->phv_lld_host = sh;
	phv->phv_mode = PHV_LLD_SCSI_HOST_NO;

	pr_debug("CORE_HBA[%d] - Enabled pSCSI HBA Passthrough %s\n",
		hba->hba_id, (sh->hostt->name) ? (sh->hostt->name) : "Unknown");

	return 1;
}

static void pscsi_tape_read_blocksize(struct se_device *dev,
		struct scsi_device *sdev)
{
	unsigned char cdb[MAX_COMMAND_SIZE], *buf;
	int ret;

	buf = kzalloc(12, GFP_KERNEL);
	if (!buf)
		return;

	memset(cdb, 0, MAX_COMMAND_SIZE);
	cdb[0] = MODE_SENSE;
	cdb[4] = 0x0c; /* 12 bytes */

	ret = scsi_execute_req(sdev, cdb, DMA_FROM_DEVICE, buf, 12, NULL,
			HZ, 1, NULL);
	if (ret)
		goto out_free;

	/*
	 * If MODE_SENSE still returns zero, set the default value to 1024.
	 */
	sdev->sector_size = (buf[9] << 16) | (buf[10] << 8) | (buf[11]);
	if (!sdev->sector_size)
		sdev->sector_size = 1024;
out_free:
	kfree(buf);
}

static void
pscsi_set_inquiry_info(struct scsi_device *sdev, struct t10_wwn *wwn)
{
	unsigned char *buf;

	if (sdev->inquiry_len < INQUIRY_LEN)
		return;

	buf = sdev->inquiry;
	if (!buf)
		return;
	/*
	 * Use sdev->inquiry from drivers/scsi/scsi_scan.c:scsi_alloc_sdev()
	 */
	memcpy(&wwn->vendor[0], &buf[8], sizeof(wwn->vendor));
	memcpy(&wwn->model[0], &buf[16], sizeof(wwn->model));
	memcpy(&wwn->revision[0], &buf[32], sizeof(wwn->revision));
}

static int
pscsi_get_inquiry_vpd_serial(struct scsi_device *sdev, struct t10_wwn *wwn)
{
	unsigned char cdb[MAX_COMMAND_SIZE], *buf;
	int ret;

	buf = kzalloc(INQUIRY_VPD_SERIAL_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(cdb, 0, MAX_COMMAND_SIZE);
	cdb[0] = INQUIRY;
	cdb[1] = 0x01; /* Query VPD */
	cdb[2] = 0x80; /* Unit Serial Number */
	cdb[3] = (INQUIRY_VPD_SERIAL_LEN >> 8) & 0xff;
	cdb[4] = (INQUIRY_VPD_SERIAL_LEN & 0xff);

	ret = scsi_execute_req(sdev, cdb, DMA_FROM_DEVICE, buf,
			      INQUIRY_VPD_SERIAL_LEN, NULL, HZ, 1, NULL);
	if (ret)
		goto out_free;

	snprintf(&wwn->unit_serial[0], INQUIRY_VPD_SERIAL_LEN, "%s", &buf[4]);

	wwn->t10_sub_dev->su_dev_flags |= SDF_FIRMWARE_VPD_UNIT_SERIAL;

	kfree(buf);
	return 0;

out_free:
	kfree(buf);
	return -EPERM;
}

static void
pscsi_get_inquiry_vpd_device_ident(struct scsi_device *sdev,
		struct t10_wwn *wwn)
{
	unsigned char cdb[MAX_COMMAND_SIZE], *buf, *page_83;
	int ident_len, page_len, off = 4, ret;
	struct t10_vpd *vpd;

	buf = kzalloc(INQUIRY_VPD_SERIAL_LEN, GFP_KERNEL);
	if (!buf)
		return;

	memset(cdb, 0, MAX_COMMAND_SIZE);
	cdb[0] = INQUIRY;
	cdb[1] = 0x01; /* Query VPD */
	cdb[2] = 0x83; /* Device Identifier */
	cdb[3] = (INQUIRY_VPD_DEVICE_IDENTIFIER_LEN >> 8) & 0xff;
	cdb[4] = (INQUIRY_VPD_DEVICE_IDENTIFIER_LEN & 0xff);

	ret = scsi_execute_req(sdev, cdb, DMA_FROM_DEVICE, buf,
			      INQUIRY_VPD_DEVICE_IDENTIFIER_LEN,
			      NULL, HZ, 1, NULL);
	if (ret)
		goto out;

	page_len = (buf[2] << 8) | buf[3];
	while (page_len > 0) {
		/* Grab a pointer to the Identification descriptor */
		page_83 = &buf[off];
		ident_len = page_83[3];
		if (!ident_len) {
			pr_err("page_83[3]: identifier"
					" length zero!\n");
			break;
		}
		pr_debug("T10 VPD Identifer Length: %d\n", ident_len);

		vpd = kzalloc(sizeof(struct t10_vpd), GFP_KERNEL);
		if (!vpd) {
			pr_err("Unable to allocate memory for"
					" struct t10_vpd\n");
			goto out;
		}
		INIT_LIST_HEAD(&vpd->vpd_list);

		transport_set_vpd_proto_id(vpd, page_83);
		transport_set_vpd_assoc(vpd, page_83);

		if (transport_set_vpd_ident_type(vpd, page_83) < 0) {
			off += (ident_len + 4);
			page_len -= (ident_len + 4);
			kfree(vpd);
			continue;
		}
		if (transport_set_vpd_ident(vpd, page_83) < 0) {
			off += (ident_len + 4);
			page_len -= (ident_len + 4);
			kfree(vpd);
			continue;
		}

		list_add_tail(&vpd->vpd_list, &wwn->t10_vpd_list);
		off += (ident_len + 4);
		page_len -= (ident_len + 4);
	}

out:
	kfree(buf);
}

/*	pscsi_add_device_to_list():
 *
 *
 */
static struct se_device *pscsi_add_device_to_list(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	struct pscsi_dev_virt *pdv,
	struct scsi_device *sd,
	int dev_flags)
{
	struct se_device *dev;
	struct se_dev_limits dev_limits;
	struct request_queue *q;
	struct queue_limits *limits;

	memset(&dev_limits, 0, sizeof(struct se_dev_limits));

	if (!sd->queue_depth) {
		sd->queue_depth = PSCSI_DEFAULT_QUEUEDEPTH;

		pr_err("Set broken SCSI Device %d:%d:%d"
			" queue_depth to %d\n", sd->channel, sd->id,
				sd->lun, sd->queue_depth);
	}
	/*
	 * Setup the local scope queue_limits from struct request_queue->limits
	 * to pass into transport_add_device_to_core_hba() as struct se_dev_limits.
	 */
	q = sd->request_queue;
	limits = &dev_limits.limits;
	limits->logical_block_size = sd->sector_size;
	limits->max_hw_sectors = min_t(int, sd->host->max_sectors, queue_max_hw_sectors(q));
	limits->max_sectors = min_t(int, sd->host->max_sectors, queue_max_sectors(q));
	dev_limits.hw_queue_depth = sd->queue_depth;
	dev_limits.queue_depth = sd->queue_depth;
	/*
	 * Setup our standard INQUIRY info into se_dev->t10_wwn
	 */
	pscsi_set_inquiry_info(sd, &se_dev->t10_wwn);

	/*
	 * Set the pointer pdv->pdv_sd to from passed struct scsi_device,
	 * which has already been referenced with Linux SCSI code with
	 * scsi_device_get() in this file's pscsi_create_virtdevice().
	 *
	 * The passthrough operations called by the transport_add_device_*
	 * function below will require this pointer to be set for passthroug
	 *  ops.
	 *
	 * For the shutdown case in pscsi_free_device(), this struct
	 * scsi_device  reference is released with Linux SCSI code
	 * scsi_device_put() and the pdv->pdv_sd cleared.
	 */
	pdv->pdv_sd = sd;
	dev = transport_add_device_to_core_hba(hba, &pscsi_template,
				se_dev, dev_flags, pdv,
				&dev_limits, NULL, NULL);
	if (!dev) {
		pdv->pdv_sd = NULL;
		return NULL;
	}

	/*
	 * Locate VPD WWN Information used for various purposes within
	 * the Storage Engine.
	 */
	if (!pscsi_get_inquiry_vpd_serial(sd, &se_dev->t10_wwn)) {
		/*
		 * If VPD Unit Serial returned GOOD status, try
		 * VPD Device Identification page (0x83).
		 */
		pscsi_get_inquiry_vpd_device_ident(sd, &se_dev->t10_wwn);
	}

	/*
	 * For TYPE_TAPE, attempt to determine blocksize with MODE_SENSE.
	 */
	if (sd->type == TYPE_TAPE)
		pscsi_tape_read_blocksize(dev, sd);
	return dev;
}

static void *pscsi_allocate_virtdevice(struct se_hba *hba, const char *name)
{
	struct pscsi_dev_virt *pdv;

	pdv = kzalloc(sizeof(struct pscsi_dev_virt), GFP_KERNEL);
	if (!pdv) {
		pr_err("Unable to allocate memory for struct pscsi_dev_virt\n");
		return NULL;
	}
	pdv->pdv_se_hba = hba;

	pr_debug("PSCSI: Allocated pdv: %p for %s\n", pdv, name);
	return pdv;
}

/*
 * Called with struct Scsi_Host->host_lock called.
 */
static struct se_device *pscsi_create_type_disk(
	struct scsi_device *sd,
	struct pscsi_dev_virt *pdv,
	struct se_subsystem_dev *se_dev,
	struct se_hba *hba)
	__releases(sh->host_lock)
{
	struct se_device *dev;
	struct pscsi_hba_virt *phv = pdv->pdv_se_hba->hba_ptr;
	struct Scsi_Host *sh = sd->host;
	struct block_device *bd;
	u32 dev_flags = 0;

	if (scsi_device_get(sd)) {
		pr_err("scsi_device_get() failed for %d:%d:%d:%d\n",
			sh->host_no, sd->channel, sd->id, sd->lun);
		spin_unlock_irq(sh->host_lock);
		return NULL;
	}
	spin_unlock_irq(sh->host_lock);
	/*
	 * Claim exclusive struct block_device access to struct scsi_device
	 * for TYPE_DISK using supplied udev_path
	 */
	bd = blkdev_get_by_path(se_dev->se_dev_udev_path,
				FMODE_WRITE|FMODE_READ|FMODE_EXCL, pdv);
	if (IS_ERR(bd)) {
		pr_err("pSCSI: blkdev_get_by_path() failed\n");
		scsi_device_put(sd);
		return NULL;
	}
	pdv->pdv_bd = bd;

	dev = pscsi_add_device_to_list(hba, se_dev, pdv, sd, dev_flags);
	if (!dev) {
		blkdev_put(pdv->pdv_bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);
		scsi_device_put(sd);
		return NULL;
	}
	pr_debug("CORE_PSCSI[%d] - Added TYPE_DISK for %d:%d:%d:%d\n",
		phv->phv_host_id, sh->host_no, sd->channel, sd->id, sd->lun);

	return dev;
}

/*
 * Called with struct Scsi_Host->host_lock called.
 */
static struct se_device *pscsi_create_type_rom(
	struct scsi_device *sd,
	struct pscsi_dev_virt *pdv,
	struct se_subsystem_dev *se_dev,
	struct se_hba *hba)
	__releases(sh->host_lock)
{
	struct se_device *dev;
	struct pscsi_hba_virt *phv = pdv->pdv_se_hba->hba_ptr;
	struct Scsi_Host *sh = sd->host;
	u32 dev_flags = 0;

	if (scsi_device_get(sd)) {
		pr_err("scsi_device_get() failed for %d:%d:%d:%d\n",
			sh->host_no, sd->channel, sd->id, sd->lun);
		spin_unlock_irq(sh->host_lock);
		return NULL;
	}
	spin_unlock_irq(sh->host_lock);

	dev = pscsi_add_device_to_list(hba, se_dev, pdv, sd, dev_flags);
	if (!dev) {
		scsi_device_put(sd);
		return NULL;
	}
	pr_debug("CORE_PSCSI[%d] - Added Type: %s for %d:%d:%d:%d\n",
		phv->phv_host_id, scsi_device_type(sd->type), sh->host_no,
		sd->channel, sd->id, sd->lun);

	return dev;
}

/*
 *Called with struct Scsi_Host->host_lock called.
 */
static struct se_device *pscsi_create_type_other(
	struct scsi_device *sd,
	struct pscsi_dev_virt *pdv,
	struct se_subsystem_dev *se_dev,
	struct se_hba *hba)
	__releases(sh->host_lock)
{
	struct se_device *dev;
	struct pscsi_hba_virt *phv = pdv->pdv_se_hba->hba_ptr;
	struct Scsi_Host *sh = sd->host;
	u32 dev_flags = 0;

	spin_unlock_irq(sh->host_lock);
	dev = pscsi_add_device_to_list(hba, se_dev, pdv, sd, dev_flags);
	if (!dev)
		return NULL;

	pr_debug("CORE_PSCSI[%d] - Added Type: %s for %d:%d:%d:%d\n",
		phv->phv_host_id, scsi_device_type(sd->type), sh->host_no,
		sd->channel, sd->id, sd->lun);

	return dev;
}

static struct se_device *pscsi_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p)
{
	struct pscsi_dev_virt *pdv = p;
	struct se_device *dev;
	struct scsi_device *sd;
	struct pscsi_hba_virt *phv = hba->hba_ptr;
	struct Scsi_Host *sh = phv->phv_lld_host;
	int legacy_mode_enable = 0;

	if (!pdv) {
		pr_err("Unable to locate struct pscsi_dev_virt"
				" parameter\n");
		return ERR_PTR(-EINVAL);
	}
	/*
	 * If not running in PHV_LLD_SCSI_HOST_NO mode, locate the
	 * struct Scsi_Host we will need to bring the TCM/pSCSI object online
	 */
	if (!sh) {
		if (phv->phv_mode == PHV_LLD_SCSI_HOST_NO) {
			pr_err("pSCSI: Unable to locate struct"
				" Scsi_Host for PHV_LLD_SCSI_HOST_NO\n");
			return ERR_PTR(-ENODEV);
		}
		/*
		 * For the newer PHV_VIRUTAL_HOST_ID struct scsi_device
		 * reference, we enforce that udev_path has been set
		 */
		if (!(se_dev->su_dev_flags & SDF_USING_UDEV_PATH)) {
			pr_err("pSCSI: udev_path attribute has not"
				" been set before ENABLE=1\n");
			return ERR_PTR(-EINVAL);
		}
		/*
		 * If no scsi_host_id= was passed for PHV_VIRUTAL_HOST_ID,
		 * use the original TCM hba ID to reference Linux/SCSI Host No
		 * and enable for PHV_LLD_SCSI_HOST_NO mode.
		 */
		if (!(pdv->pdv_flags & PDF_HAS_VIRT_HOST_ID)) {
			spin_lock(&hba->device_lock);
			if (!list_empty(&hba->hba_dev_list)) {
				pr_err("pSCSI: Unable to set hba_mode"
					" with active devices\n");
				spin_unlock(&hba->device_lock);
				return ERR_PTR(-EEXIST);
			}
			spin_unlock(&hba->device_lock);

			if (pscsi_pmode_enable_hba(hba, 1) != 1)
				return ERR_PTR(-ENODEV);

			legacy_mode_enable = 1;
			hba->hba_flags |= HBA_FLAGS_PSCSI_MODE;
			sh = phv->phv_lld_host;
		} else {
			sh = scsi_host_lookup(pdv->pdv_host_id);
			if (IS_ERR(sh)) {
				pr_err("pSCSI: Unable to locate"
					" pdv_host_id: %d\n", pdv->pdv_host_id);
				return ERR_CAST(sh);
			}
		}
	} else {
		if (phv->phv_mode == PHV_VIRUTAL_HOST_ID) {
			pr_err("pSCSI: PHV_VIRUTAL_HOST_ID set while"
				" struct Scsi_Host exists\n");
			return ERR_PTR(-EEXIST);
		}
	}

	spin_lock_irq(sh->host_lock);
	list_for_each_entry(sd, &sh->__devices, siblings) {
		if ((pdv->pdv_channel_id != sd->channel) ||
		    (pdv->pdv_target_id != sd->id) ||
		    (pdv->pdv_lun_id != sd->lun))
			continue;
		/*
		 * Functions will release the held struct scsi_host->host_lock
		 * before calling calling pscsi_add_device_to_list() to register
		 * struct scsi_device with target_core_mod.
		 */
		switch (sd->type) {
		case TYPE_DISK:
			dev = pscsi_create_type_disk(sd, pdv, se_dev, hba);
			break;
		case TYPE_ROM:
			dev = pscsi_create_type_rom(sd, pdv, se_dev, hba);
			break;
		default:
			dev = pscsi_create_type_other(sd, pdv, se_dev, hba);
			break;
		}

		if (!dev) {
			if (phv->phv_mode == PHV_VIRUTAL_HOST_ID)
				scsi_host_put(sh);
			else if (legacy_mode_enable) {
				pscsi_pmode_enable_hba(hba, 0);
				hba->hba_flags &= ~HBA_FLAGS_PSCSI_MODE;
			}
			pdv->pdv_sd = NULL;
			return ERR_PTR(-ENODEV);
		}
		return dev;
	}
	spin_unlock_irq(sh->host_lock);

	pr_err("pSCSI: Unable to locate %d:%d:%d:%d\n", sh->host_no,
		pdv->pdv_channel_id,  pdv->pdv_target_id, pdv->pdv_lun_id);

	if (phv->phv_mode == PHV_VIRUTAL_HOST_ID)
		scsi_host_put(sh);
	else if (legacy_mode_enable) {
		pscsi_pmode_enable_hba(hba, 0);
		hba->hba_flags &= ~HBA_FLAGS_PSCSI_MODE;
	}

	return ERR_PTR(-ENODEV);
}

/*	pscsi_free_device(): (Part of se_subsystem_api_t template)
 *
 *
 */
static void pscsi_free_device(void *p)
{
	struct pscsi_dev_virt *pdv = p;
	struct pscsi_hba_virt *phv = pdv->pdv_se_hba->hba_ptr;
	struct scsi_device *sd = pdv->pdv_sd;

	if (sd) {
		/*
		 * Release exclusive pSCSI internal struct block_device claim for
		 * struct scsi_device with TYPE_DISK from pscsi_create_type_disk()
		 */
		if ((sd->type == TYPE_DISK) && pdv->pdv_bd) {
			blkdev_put(pdv->pdv_bd,
				   FMODE_WRITE|FMODE_READ|FMODE_EXCL);
			pdv->pdv_bd = NULL;
		}
		/*
		 * For HBA mode PHV_LLD_SCSI_HOST_NO, release the reference
		 * to struct Scsi_Host now.
		 */
		if ((phv->phv_mode == PHV_LLD_SCSI_HOST_NO) &&
		    (phv->phv_lld_host != NULL))
			scsi_host_put(phv->phv_lld_host);

		if ((sd->type == TYPE_DISK) || (sd->type == TYPE_ROM))
			scsi_device_put(sd);

		pdv->pdv_sd = NULL;
	}

	kfree(pdv);
}

static inline struct pscsi_plugin_task *PSCSI_TASK(struct se_task *task)
{
	return container_of(task, struct pscsi_plugin_task, pscsi_task);
}


/*	pscsi_transport_complete():
 *
 *
 */
static int pscsi_transport_complete(struct se_task *task)
{
	struct pscsi_dev_virt *pdv = task->task_se_cmd->se_dev->dev_ptr;
	struct scsi_device *sd = pdv->pdv_sd;
	int result;
	struct pscsi_plugin_task *pt = PSCSI_TASK(task);
	unsigned char *cdb = &pt->pscsi_cdb[0];

	result = pt->pscsi_result;
	/*
	 * Hack to make sure that Write-Protect modepage is set if R/O mode is
	 * forced.
	 */
	if (((cdb[0] == MODE_SENSE) || (cdb[0] == MODE_SENSE_10)) &&
	     (status_byte(result) << 1) == SAM_STAT_GOOD) {
		if (!task->task_se_cmd->se_deve)
			goto after_mode_sense;

		if (task->task_se_cmd->se_deve->lun_flags &
				TRANSPORT_LUNFLAGS_READ_ONLY) {
			unsigned char *buf = transport_kmap_first_data_page(task->task_se_cmd);

			if (cdb[0] == MODE_SENSE_10) {
				if (!(buf[3] & 0x80))
					buf[3] |= 0x80;
			} else {
				if (!(buf[2] & 0x80))
					buf[2] |= 0x80;
			}

			transport_kunmap_first_data_page(task->task_se_cmd);
		}
	}
after_mode_sense:

	if (sd->type != TYPE_TAPE)
		goto after_mode_select;

	/*
	 * Hack to correctly obtain the initiator requested blocksize for
	 * TYPE_TAPE.  Since this value is dependent upon each tape media,
	 * struct scsi_device->sector_size will not contain the correct value
	 * by default, so we go ahead and set it so
	 * TRANSPORT(dev)->get_blockdev() returns the correct value to the
	 * storage engine.
	 */
	if (((cdb[0] == MODE_SELECT) || (cdb[0] == MODE_SELECT_10)) &&
	      (status_byte(result) << 1) == SAM_STAT_GOOD) {
		unsigned char *buf;
		struct scatterlist *sg = task->task_sg;
		u16 bdl;
		u32 blocksize;

		buf = sg_virt(&sg[0]);
		if (!buf) {
			pr_err("Unable to get buf for scatterlist\n");
			goto after_mode_select;
		}

		if (cdb[0] == MODE_SELECT)
			bdl = (buf[3]);
		else
			bdl = (buf[6] << 8) | (buf[7]);

		if (!bdl)
			goto after_mode_select;

		if (cdb[0] == MODE_SELECT)
			blocksize = (buf[9] << 16) | (buf[10] << 8) |
					(buf[11]);
		else
			blocksize = (buf[13] << 16) | (buf[14] << 8) |
					(buf[15]);

		sd->sector_size = blocksize;
	}
after_mode_select:

	if (status_byte(result) & CHECK_CONDITION)
		return 1;

	return 0;
}

static struct se_task *
pscsi_alloc_task(unsigned char *cdb)
{
	struct pscsi_plugin_task *pt;

	/*
	 * Dynamically alloc cdb space, since it may be larger than
	 * TCM_MAX_COMMAND_SIZE
	 */
	pt = kzalloc(sizeof(*pt) + scsi_command_size(cdb), GFP_KERNEL);
	if (!pt) {
		pr_err("Unable to allocate struct pscsi_plugin_task\n");
		return NULL;
	}

	return &pt->pscsi_task;
}

static void pscsi_free_task(struct se_task *task)
{
	struct pscsi_plugin_task *pt = PSCSI_TASK(task);

	/*
	 * We do not release the bio(s) here associated with this task, as
	 * this is handled by bio_put() and pscsi_bi_endio().
	 */
	kfree(pt);
}

enum {
	Opt_scsi_host_id, Opt_scsi_channel_id, Opt_scsi_target_id,
	Opt_scsi_lun_id, Opt_err
};

static match_table_t tokens = {
	{Opt_scsi_host_id, "scsi_host_id=%d"},
	{Opt_scsi_channel_id, "scsi_channel_id=%d"},
	{Opt_scsi_target_id, "scsi_target_id=%d"},
	{Opt_scsi_lun_id, "scsi_lun_id=%d"},
	{Opt_err, NULL}
};

static ssize_t pscsi_set_configfs_dev_params(struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	const char *page,
	ssize_t count)
{
	struct pscsi_dev_virt *pdv = se_dev->se_dev_su_ptr;
	struct pscsi_hba_virt *phv = hba->hba_ptr;
	char *orig, *ptr, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, arg, token;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_scsi_host_id:
			if (phv->phv_mode == PHV_LLD_SCSI_HOST_NO) {
				pr_err("PSCSI[%d]: Unable to accept"
					" scsi_host_id while phv_mode =="
					" PHV_LLD_SCSI_HOST_NO\n",
					phv->phv_host_id);
				ret = -EINVAL;
				goto out;
			}
			match_int(args, &arg);
			pdv->pdv_host_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Host ID:"
				" %d\n", phv->phv_host_id, pdv->pdv_host_id);
			pdv->pdv_flags |= PDF_HAS_VIRT_HOST_ID;
			break;
		case Opt_scsi_channel_id:
			match_int(args, &arg);
			pdv->pdv_channel_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Channel"
				" ID: %d\n",  phv->phv_host_id,
				pdv->pdv_channel_id);
			pdv->pdv_flags |= PDF_HAS_CHANNEL_ID;
			break;
		case Opt_scsi_target_id:
			match_int(args, &arg);
			pdv->pdv_target_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Target"
				" ID: %d\n", phv->phv_host_id,
				pdv->pdv_target_id);
			pdv->pdv_flags |= PDF_HAS_TARGET_ID;
			break;
		case Opt_scsi_lun_id:
			match_int(args, &arg);
			pdv->pdv_lun_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI LUN ID:"
				" %d\n", phv->phv_host_id, pdv->pdv_lun_id);
			pdv->pdv_flags |= PDF_HAS_LUN_ID;
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t pscsi_check_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev)
{
	struct pscsi_dev_virt *pdv = se_dev->se_dev_su_ptr;

	if (!(pdv->pdv_flags & PDF_HAS_CHANNEL_ID) ||
	    !(pdv->pdv_flags & PDF_HAS_TARGET_ID) ||
	    !(pdv->pdv_flags & PDF_HAS_LUN_ID)) {
		pr_err("Missing scsi_channel_id=, scsi_target_id= and"
			" scsi_lun_id= parameters\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t pscsi_show_configfs_dev_params(struct se_hba *hba,
					      struct se_subsystem_dev *se_dev,
					      char *b)
{
	struct pscsi_hba_virt *phv = hba->hba_ptr;
        struct pscsi_dev_virt *pdv = se_dev->se_dev_su_ptr;
	struct scsi_device *sd = pdv->pdv_sd;
	unsigned char host_id[16];
	ssize_t bl;
	int i;

	if (phv->phv_mode == PHV_VIRUTAL_HOST_ID)
		snprintf(host_id, 16, "%d", pdv->pdv_host_id);
	else
		snprintf(host_id, 16, "PHBA Mode");

	bl = sprintf(b, "SCSI Device Bus Location:"
		" Channel ID: %d Target ID: %d LUN: %d Host ID: %s\n",
		pdv->pdv_channel_id, pdv->pdv_target_id, pdv->pdv_lun_id,
		host_id);

	if (sd) {
		bl += sprintf(b + bl, "        ");
		bl += sprintf(b + bl, "Vendor: ");
		for (i = 0; i < 8; i++) {
			if (ISPRINT(sd->vendor[i]))   /* printable character? */
				bl += sprintf(b + bl, "%c", sd->vendor[i]);
			else
				bl += sprintf(b + bl, " ");
		}
		bl += sprintf(b + bl, " Model: ");
		for (i = 0; i < 16; i++) {
			if (ISPRINT(sd->model[i]))   /* printable character ? */
				bl += sprintf(b + bl, "%c", sd->model[i]);
			else
				bl += sprintf(b + bl, " ");
		}
		bl += sprintf(b + bl, " Rev: ");
		for (i = 0; i < 4; i++) {
			if (ISPRINT(sd->rev[i]))   /* printable character ? */
				bl += sprintf(b + bl, "%c", sd->rev[i]);
			else
				bl += sprintf(b + bl, " ");
		}
		bl += sprintf(b + bl, "\n");
	}
	return bl;
}

static void pscsi_bi_endio(struct bio *bio, int error)
{
	bio_put(bio);
}

static inline struct bio *pscsi_get_bio(int sg_num)
{
	struct bio *bio;
	/*
	 * Use bio_malloc() following the comment in for bio -> struct request
	 * in block/blk-core.c:blk_make_request()
	 */
	bio = bio_kmalloc(GFP_KERNEL, sg_num);
	if (!bio) {
		pr_err("PSCSI: bio_kmalloc() failed\n");
		return NULL;
	}
	bio->bi_end_io = pscsi_bi_endio;

	return bio;
}

static int pscsi_map_sg(struct se_task *task, struct scatterlist *task_sg,
		struct bio **hbio)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct pscsi_dev_virt *pdv = task->task_se_cmd->se_dev->dev_ptr;
	u32 task_sg_num = task->task_sg_nents;
	struct bio *bio = NULL, *tbio = NULL;
	struct page *page;
	struct scatterlist *sg;
	u32 data_len = task->task_size, i, len, bytes, off;
	int nr_pages = (task->task_size + task_sg[0].offset +
			PAGE_SIZE - 1) >> PAGE_SHIFT;
	int nr_vecs = 0, rc;
	int rw = (task->task_data_direction == DMA_TO_DEVICE);

	*hbio = NULL;

	pr_debug("PSCSI: nr_pages: %d\n", nr_pages);

	for_each_sg(task_sg, sg, task_sg_num, i) {
		page = sg_page(sg);
		off = sg->offset;
		len = sg->length;

		pr_debug("PSCSI: i: %d page: %p len: %d off: %d\n", i,
			page, len, off);

		while (len > 0 && data_len > 0) {
			bytes = min_t(unsigned int, len, PAGE_SIZE - off);
			bytes = min(bytes, data_len);

			if (!bio) {
				nr_vecs = min_t(int, BIO_MAX_PAGES, nr_pages);
				nr_pages -= nr_vecs;
				/*
				 * Calls bio_kmalloc() and sets bio->bi_end_io()
				 */
				bio = pscsi_get_bio(nr_vecs);
				if (!bio)
					goto fail;

				if (rw)
					bio->bi_rw |= REQ_WRITE;

				pr_debug("PSCSI: Allocated bio: %p,"
					" dir: %s nr_vecs: %d\n", bio,
					(rw) ? "rw" : "r", nr_vecs);
				/*
				 * Set *hbio pointer to handle the case:
				 * nr_pages > BIO_MAX_PAGES, where additional
				 * bios need to be added to complete a given
				 * struct se_task
				 */
				if (!*hbio)
					*hbio = tbio = bio;
				else
					tbio = tbio->bi_next = bio;
			}

			pr_debug("PSCSI: Calling bio_add_pc_page() i: %d"
				" bio: %p page: %p len: %d off: %d\n", i, bio,
				page, len, off);

			rc = bio_add_pc_page(pdv->pdv_sd->request_queue,
					bio, page, bytes, off);
			if (rc != bytes)
				goto fail;

			pr_debug("PSCSI: bio->bi_vcnt: %d nr_vecs: %d\n",
				bio->bi_vcnt, nr_vecs);

			if (bio->bi_vcnt > nr_vecs) {
				pr_debug("PSCSI: Reached bio->bi_vcnt max:"
					" %d i: %d bio: %p, allocating another"
					" bio\n", bio->bi_vcnt, i, bio);
				/*
				 * Clear the pointer so that another bio will
				 * be allocated with pscsi_get_bio() above, the
				 * current bio has already been set *tbio and
				 * bio->bi_next.
				 */
				bio = NULL;
			}

			page++;
			len -= bytes;
			data_len -= bytes;
			off = 0;
		}
	}

	return task->task_sg_nents;
fail:
	while (*hbio) {
		bio = *hbio;
		*hbio = (*hbio)->bi_next;
		bio->bi_next = NULL;
		bio_endio(bio, 0);	/* XXX: should be error */
	}
	cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	return -ENOMEM;
}

static int pscsi_do_task(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct pscsi_dev_virt *pdv = task->task_se_cmd->se_dev->dev_ptr;
	struct pscsi_plugin_task *pt = PSCSI_TASK(task);
	struct request *req;
	struct bio *hbio;
	int ret;

	target_get_task_cdb(task, pt->pscsi_cdb);

	if (task->task_se_cmd->se_cmd_flags & SCF_SCSI_NON_DATA_CDB) {
		req = blk_get_request(pdv->pdv_sd->request_queue,
				(task->task_data_direction == DMA_TO_DEVICE),
				GFP_KERNEL);
		if (!req || IS_ERR(req)) {
			pr_err("PSCSI: blk_get_request() failed: %ld\n",
					req ? IS_ERR(req) : -ENOMEM);
			cmd->scsi_sense_reason =
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			return -ENODEV;
		}
	} else {
		BUG_ON(!task->task_size);

		/*
		 * Setup the main struct request for the task->task_sg[] payload
		 */
		ret = pscsi_map_sg(task, task->task_sg, &hbio);
		if (ret < 0) {
			cmd->scsi_sense_reason =
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			return ret;
		}

		req = blk_make_request(pdv->pdv_sd->request_queue, hbio,
				       GFP_KERNEL);
		if (IS_ERR(req)) {
			pr_err("pSCSI: blk_make_request() failed\n");
			goto fail;
		}
	}

	req->cmd_type = REQ_TYPE_BLOCK_PC;
	req->end_io = pscsi_req_done;
	req->end_io_data = task;
	req->cmd_len = scsi_command_size(pt->pscsi_cdb);
	req->cmd = &pt->pscsi_cdb[0];
	req->sense = &pt->pscsi_sense[0];
	req->sense_len = 0;
	if (pdv->pdv_sd->type == TYPE_DISK)
		req->timeout = PS_TIMEOUT_DISK;
	else
		req->timeout = PS_TIMEOUT_OTHER;
	req->retries = PS_RETRY;

	blk_execute_rq_nowait(pdv->pdv_sd->request_queue, NULL, req,
			(task->task_se_cmd->sam_task_attr == MSG_HEAD_TAG),
			pscsi_req_done);

	return 0;

fail:
	while (hbio) {
		struct bio *bio = hbio;
		hbio = hbio->bi_next;
		bio->bi_next = NULL;
		bio_endio(bio, 0);	/* XXX: should be error */
	}
	cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	return -ENOMEM;
}

/*	pscsi_get_sense_buffer():
 *
 *
 */
static unsigned char *pscsi_get_sense_buffer(struct se_task *task)
{
	struct pscsi_plugin_task *pt = PSCSI_TASK(task);

	return pt->pscsi_sense;
}

/*	pscsi_get_device_rev():
 *
 *
 */
static u32 pscsi_get_device_rev(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = dev->dev_ptr;
	struct scsi_device *sd = pdv->pdv_sd;

	return (sd->scsi_level - 1) ? sd->scsi_level - 1 : 1;
}

/*	pscsi_get_device_type():
 *
 *
 */
static u32 pscsi_get_device_type(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = dev->dev_ptr;
	struct scsi_device *sd = pdv->pdv_sd;

	return sd->type;
}

static sector_t pscsi_get_blocks(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = dev->dev_ptr;

	if (pdv->pdv_bd && pdv->pdv_bd->bd_part)
		return pdv->pdv_bd->bd_part->nr_sects;

	dump_stack();
	return 0;
}

/*	pscsi_handle_SAM_STATUS_failures():
 *
 *
 */
static inline void pscsi_process_SAM_status(
	struct se_task *task,
	struct pscsi_plugin_task *pt)
{
	task->task_scsi_status = status_byte(pt->pscsi_result);
	if (task->task_scsi_status) {
		task->task_scsi_status <<= 1;
		pr_debug("PSCSI Status Byte exception at task: %p CDB:"
			" 0x%02x Result: 0x%08x\n", task, pt->pscsi_cdb[0],
			pt->pscsi_result);
	}

	switch (host_byte(pt->pscsi_result)) {
	case DID_OK:
		transport_complete_task(task, (!task->task_scsi_status));
		break;
	default:
		pr_debug("PSCSI Host Byte exception at task: %p CDB:"
			" 0x%02x Result: 0x%08x\n", task, pt->pscsi_cdb[0],
			pt->pscsi_result);
		task->task_scsi_status = SAM_STAT_CHECK_CONDITION;
		task->task_se_cmd->scsi_sense_reason =
					TCM_UNSUPPORTED_SCSI_OPCODE;
		transport_complete_task(task, 0);
		break;
	}
}

static void pscsi_req_done(struct request *req, int uptodate)
{
	struct se_task *task = req->end_io_data;
	struct pscsi_plugin_task *pt = PSCSI_TASK(task);

	pt->pscsi_result = req->errors;
	pt->pscsi_resid = req->resid_len;

	pscsi_process_SAM_status(task, pt);
	__blk_put_request(req->q, req);
}

static struct se_subsystem_api pscsi_template = {
	.name			= "pscsi",
	.owner			= THIS_MODULE,
	.transport_type		= TRANSPORT_PLUGIN_PHBA_PDEV,
	.attach_hba		= pscsi_attach_hba,
	.detach_hba		= pscsi_detach_hba,
	.pmode_enable_hba	= pscsi_pmode_enable_hba,
	.allocate_virtdevice	= pscsi_allocate_virtdevice,
	.create_virtdevice	= pscsi_create_virtdevice,
	.free_device		= pscsi_free_device,
	.transport_complete	= pscsi_transport_complete,
	.alloc_task		= pscsi_alloc_task,
	.do_task		= pscsi_do_task,
	.free_task		= pscsi_free_task,
	.check_configfs_dev_params = pscsi_check_configfs_dev_params,
	.set_configfs_dev_params = pscsi_set_configfs_dev_params,
	.show_configfs_dev_params = pscsi_show_configfs_dev_params,
	.get_sense_buffer	= pscsi_get_sense_buffer,
	.get_device_rev		= pscsi_get_device_rev,
	.get_device_type	= pscsi_get_device_type,
	.get_blocks		= pscsi_get_blocks,
};

static int __init pscsi_module_init(void)
{
	return transport_subsystem_register(&pscsi_template);
}

static void pscsi_module_exit(void)
{
	transport_subsystem_release(&pscsi_template);
}

MODULE_DESCRIPTION("TCM PSCSI subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(pscsi_module_init);
module_exit(pscsi_module_exit);
