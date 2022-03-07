// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * Filename:  target_core_pscsi.c
 *
 * This file contains the generic target mode <-> Linux SCSI subsystem plugin.
 *
 * (c) Copyright 2003-2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 ******************************************************************************/

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/cdrom.h>
#include <linux/ratelimit.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "target_core_alua.h"
#include "target_core_internal.h"
#include "target_core_pscsi.h"

static inline struct pscsi_dev_virt *PSCSI_DEV(struct se_device *dev)
{
	return container_of(dev, struct pscsi_dev_virt, dev);
}

static sense_reason_t pscsi_execute_cmd(struct se_cmd *cmd);
static void pscsi_req_done(struct request *, blk_status_t);

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
	phv->phv_mode = PHV_VIRTUAL_HOST_ID;

	hba->hba_ptr = phv;

	pr_debug("CORE_HBA[%d] - TCM SCSI HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		PSCSI_VERSION, TARGET_CORE_VERSION);
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
		phv->phv_mode = PHV_VIRTUAL_HOST_ID;

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
	if (!sh) {
		pr_err("pSCSI: Unable to locate SCSI Host for"
			" phv_host_id: %d\n", phv->phv_host_id);
		return -EINVAL;
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
		goto out_free;

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
	sdev->sector_size = get_unaligned_be24(&buf[9]);
out_free:
	if (!sdev->sector_size)
		sdev->sector_size = 1024;

	kfree(buf);
}

static void
pscsi_set_inquiry_info(struct scsi_device *sdev, struct t10_wwn *wwn)
{
	if (sdev->inquiry_len < INQUIRY_LEN)
		return;
	/*
	 * Use sdev->inquiry data from drivers/scsi/scsi_scan.c:scsi_add_lun()
	 */
	BUILD_BUG_ON(sizeof(wwn->vendor) != INQUIRY_VENDOR_LEN + 1);
	snprintf(wwn->vendor, sizeof(wwn->vendor),
		 "%." __stringify(INQUIRY_VENDOR_LEN) "s", sdev->vendor);
	BUILD_BUG_ON(sizeof(wwn->model) != INQUIRY_MODEL_LEN + 1);
	snprintf(wwn->model, sizeof(wwn->model),
		 "%." __stringify(INQUIRY_MODEL_LEN) "s", sdev->model);
	BUILD_BUG_ON(sizeof(wwn->revision) != INQUIRY_REVISION_LEN + 1);
	snprintf(wwn->revision, sizeof(wwn->revision),
		 "%." __stringify(INQUIRY_REVISION_LEN) "s", sdev->rev);
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
	put_unaligned_be16(INQUIRY_VPD_SERIAL_LEN, &cdb[3]);

	ret = scsi_execute_req(sdev, cdb, DMA_FROM_DEVICE, buf,
			      INQUIRY_VPD_SERIAL_LEN, NULL, HZ, 1, NULL);
	if (ret)
		goto out_free;

	snprintf(&wwn->unit_serial[0], INQUIRY_VPD_SERIAL_LEN, "%s", &buf[4]);

	wwn->t10_dev->dev_flags |= DF_FIRMWARE_VPD_UNIT_SERIAL;

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
	put_unaligned_be16(INQUIRY_VPD_DEVICE_IDENTIFIER_LEN, &cdb[3]);

	ret = scsi_execute_req(sdev, cdb, DMA_FROM_DEVICE, buf,
			      INQUIRY_VPD_DEVICE_IDENTIFIER_LEN,
			      NULL, HZ, 1, NULL);
	if (ret)
		goto out;

	page_len = get_unaligned_be16(&buf[2]);
	while (page_len > 0) {
		/* Grab a pointer to the Identification descriptor */
		page_83 = &buf[off];
		ident_len = page_83[3];
		if (!ident_len) {
			pr_err("page_83[3]: identifier"
					" length zero!\n");
			break;
		}
		pr_debug("T10 VPD Identifier Length: %d\n", ident_len);

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

static int pscsi_add_device_to_list(struct se_device *dev,
		struct scsi_device *sd)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct request_queue *q = sd->request_queue;

	pdv->pdv_sd = sd;

	if (!sd->queue_depth) {
		sd->queue_depth = PSCSI_DEFAULT_QUEUEDEPTH;

		pr_err("Set broken SCSI Device %d:%d:%llu"
			" queue_depth to %d\n", sd->channel, sd->id,
				sd->lun, sd->queue_depth);
	}

	dev->dev_attrib.hw_block_size =
		min_not_zero((int)sd->sector_size, 512);
	dev->dev_attrib.hw_max_sectors =
		min_not_zero(sd->host->max_sectors, queue_max_hw_sectors(q));
	dev->dev_attrib.hw_queue_depth = sd->queue_depth;

	/*
	 * Setup our standard INQUIRY info into se_dev->t10_wwn
	 */
	pscsi_set_inquiry_info(sd, &dev->t10_wwn);

	/*
	 * Locate VPD WWN Information used for various purposes within
	 * the Storage Engine.
	 */
	if (!pscsi_get_inquiry_vpd_serial(sd, &dev->t10_wwn)) {
		/*
		 * If VPD Unit Serial returned GOOD status, try
		 * VPD Device Identification page (0x83).
		 */
		pscsi_get_inquiry_vpd_device_ident(sd, &dev->t10_wwn);
	}

	/*
	 * For TYPE_TAPE, attempt to determine blocksize with MODE_SENSE.
	 */
	if (sd->type == TYPE_TAPE) {
		pscsi_tape_read_blocksize(dev, sd);
		dev->dev_attrib.hw_block_size = sd->sector_size;
	}
	return 0;
}

static struct se_device *pscsi_alloc_device(struct se_hba *hba,
		const char *name)
{
	struct pscsi_dev_virt *pdv;

	pdv = kzalloc(sizeof(struct pscsi_dev_virt), GFP_KERNEL);
	if (!pdv) {
		pr_err("Unable to allocate memory for struct pscsi_dev_virt\n");
		return NULL;
	}

	pr_debug("PSCSI: Allocated pdv: %p for %s\n", pdv, name);
	return &pdv->dev;
}

/*
 * Called with struct Scsi_Host->host_lock called.
 */
static int pscsi_create_type_disk(struct se_device *dev, struct scsi_device *sd)
	__releases(sh->host_lock)
{
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct Scsi_Host *sh = sd->host;
	struct block_device *bd;
	int ret;

	if (scsi_device_get(sd)) {
		pr_err("scsi_device_get() failed for %d:%d:%d:%llu\n",
			sh->host_no, sd->channel, sd->id, sd->lun);
		spin_unlock_irq(sh->host_lock);
		return -EIO;
	}
	spin_unlock_irq(sh->host_lock);
	/*
	 * Claim exclusive struct block_device access to struct scsi_device
	 * for TYPE_DISK and TYPE_ZBC using supplied udev_path
	 */
	bd = blkdev_get_by_path(dev->udev_path,
				FMODE_WRITE|FMODE_READ|FMODE_EXCL, pdv);
	if (IS_ERR(bd)) {
		pr_err("pSCSI: blkdev_get_by_path() failed\n");
		scsi_device_put(sd);
		return PTR_ERR(bd);
	}
	pdv->pdv_bd = bd;

	ret = pscsi_add_device_to_list(dev, sd);
	if (ret) {
		blkdev_put(pdv->pdv_bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);
		scsi_device_put(sd);
		return ret;
	}

	pr_debug("CORE_PSCSI[%d] - Added TYPE_%s for %d:%d:%d:%llu\n",
		phv->phv_host_id, sd->type == TYPE_DISK ? "DISK" : "ZBC",
		sh->host_no, sd->channel, sd->id, sd->lun);
	return 0;
}

/*
 * Called with struct Scsi_Host->host_lock called.
 */
static int pscsi_create_type_nondisk(struct se_device *dev, struct scsi_device *sd)
	__releases(sh->host_lock)
{
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
	struct Scsi_Host *sh = sd->host;
	int ret;

	if (scsi_device_get(sd)) {
		pr_err("scsi_device_get() failed for %d:%d:%d:%llu\n",
			sh->host_no, sd->channel, sd->id, sd->lun);
		spin_unlock_irq(sh->host_lock);
		return -EIO;
	}
	spin_unlock_irq(sh->host_lock);

	ret = pscsi_add_device_to_list(dev, sd);
	if (ret) {
		scsi_device_put(sd);
		return ret;
	}
	pr_debug("CORE_PSCSI[%d] - Added Type: %s for %d:%d:%d:%llu\n",
		phv->phv_host_id, scsi_device_type(sd->type), sh->host_no,
		sd->channel, sd->id, sd->lun);

	return 0;
}

static int pscsi_configure_device(struct se_device *dev)
{
	struct se_hba *hba = dev->se_hba;
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct scsi_device *sd;
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
	struct Scsi_Host *sh = phv->phv_lld_host;
	int legacy_mode_enable = 0;
	int ret;

	if (!(pdv->pdv_flags & PDF_HAS_CHANNEL_ID) ||
	    !(pdv->pdv_flags & PDF_HAS_TARGET_ID) ||
	    !(pdv->pdv_flags & PDF_HAS_LUN_ID)) {
		pr_err("Missing scsi_channel_id=, scsi_target_id= and"
			" scsi_lun_id= parameters\n");
		return -EINVAL;
	}

	/*
	 * If not running in PHV_LLD_SCSI_HOST_NO mode, locate the
	 * struct Scsi_Host we will need to bring the TCM/pSCSI object online
	 */
	if (!sh) {
		if (phv->phv_mode == PHV_LLD_SCSI_HOST_NO) {
			pr_err("pSCSI: Unable to locate struct"
				" Scsi_Host for PHV_LLD_SCSI_HOST_NO\n");
			return -ENODEV;
		}
		/*
		 * For the newer PHV_VIRTUAL_HOST_ID struct scsi_device
		 * reference, we enforce that udev_path has been set
		 */
		if (!(dev->dev_flags & DF_USING_UDEV_PATH)) {
			pr_err("pSCSI: udev_path attribute has not"
				" been set before ENABLE=1\n");
			return -EINVAL;
		}
		/*
		 * If no scsi_host_id= was passed for PHV_VIRTUAL_HOST_ID,
		 * use the original TCM hba ID to reference Linux/SCSI Host No
		 * and enable for PHV_LLD_SCSI_HOST_NO mode.
		 */
		if (!(pdv->pdv_flags & PDF_HAS_VIRT_HOST_ID)) {
			if (hba->dev_count) {
				pr_err("pSCSI: Unable to set hba_mode"
					" with active devices\n");
				return -EEXIST;
			}

			if (pscsi_pmode_enable_hba(hba, 1) != 1)
				return -ENODEV;

			legacy_mode_enable = 1;
			hba->hba_flags |= HBA_FLAGS_PSCSI_MODE;
			sh = phv->phv_lld_host;
		} else {
			sh = scsi_host_lookup(pdv->pdv_host_id);
			if (!sh) {
				pr_err("pSCSI: Unable to locate"
					" pdv_host_id: %d\n", pdv->pdv_host_id);
				return -EINVAL;
			}
			pdv->pdv_lld_host = sh;
		}
	} else {
		if (phv->phv_mode == PHV_VIRTUAL_HOST_ID) {
			pr_err("pSCSI: PHV_VIRTUAL_HOST_ID set while"
				" struct Scsi_Host exists\n");
			return -EEXIST;
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
		case TYPE_ZBC:
			ret = pscsi_create_type_disk(dev, sd);
			break;
		default:
			ret = pscsi_create_type_nondisk(dev, sd);
			break;
		}

		if (ret) {
			if (phv->phv_mode == PHV_VIRTUAL_HOST_ID)
				scsi_host_put(sh);
			else if (legacy_mode_enable) {
				pscsi_pmode_enable_hba(hba, 0);
				hba->hba_flags &= ~HBA_FLAGS_PSCSI_MODE;
			}
			pdv->pdv_sd = NULL;
			return ret;
		}
		return 0;
	}
	spin_unlock_irq(sh->host_lock);

	pr_err("pSCSI: Unable to locate %d:%d:%d:%d\n", sh->host_no,
		pdv->pdv_channel_id,  pdv->pdv_target_id, pdv->pdv_lun_id);

	if (phv->phv_mode == PHV_VIRTUAL_HOST_ID)
		scsi_host_put(sh);
	else if (legacy_mode_enable) {
		pscsi_pmode_enable_hba(hba, 0);
		hba->hba_flags &= ~HBA_FLAGS_PSCSI_MODE;
	}

	return -ENODEV;
}

static void pscsi_dev_call_rcu(struct rcu_head *p)
{
	struct se_device *dev = container_of(p, struct se_device, rcu_head);
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);

	kfree(pdv);
}

static void pscsi_free_device(struct se_device *dev)
{
	call_rcu(&dev->rcu_head, pscsi_dev_call_rcu);
}

static void pscsi_destroy_device(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
	struct scsi_device *sd = pdv->pdv_sd;

	if (sd) {
		/*
		 * Release exclusive pSCSI internal struct block_device claim for
		 * struct scsi_device with TYPE_DISK or TYPE_ZBC
		 * from pscsi_create_type_disk()
		 */
		if ((sd->type == TYPE_DISK || sd->type == TYPE_ZBC) &&
		    pdv->pdv_bd) {
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
		else if (pdv->pdv_lld_host)
			scsi_host_put(pdv->pdv_lld_host);

		scsi_device_put(sd);

		pdv->pdv_sd = NULL;
	}
}

static void pscsi_complete_cmd(struct se_cmd *cmd, u8 scsi_status,
			       unsigned char *req_sense)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(cmd->se_dev);
	struct scsi_device *sd = pdv->pdv_sd;
	struct pscsi_plugin_task *pt = cmd->priv;
	unsigned char *cdb;
	/*
	 * Special case for REPORT_LUNs handling where pscsi_plugin_task has
	 * not been allocated because TCM is handling the emulation directly.
	 */
	if (!pt)
		return;

	cdb = &pt->pscsi_cdb[0];
	/*
	 * Hack to make sure that Write-Protect modepage is set if R/O mode is
	 * forced.
	 */
	if (!cmd->data_length)
		goto after_mode_sense;

	if (((cdb[0] == MODE_SENSE) || (cdb[0] == MODE_SENSE_10)) &&
	    scsi_status == SAM_STAT_GOOD) {
		bool read_only = target_lun_is_rdonly(cmd);

		if (read_only) {
			unsigned char *buf;

			buf = transport_kmap_data_sg(cmd);
			if (!buf) {
				; /* XXX: TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE */
			} else {
				if (cdb[0] == MODE_SENSE_10) {
					if (!(buf[3] & 0x80))
						buf[3] |= 0x80;
				} else {
					if (!(buf[2] & 0x80))
						buf[2] |= 0x80;
				}

				transport_kunmap_data_sg(cmd);
			}
		}
	}
after_mode_sense:

	if (sd->type != TYPE_TAPE || !cmd->data_length)
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
	     scsi_status == SAM_STAT_GOOD) {
		unsigned char *buf;
		u16 bdl;
		u32 blocksize;

		buf = sg_virt(&cmd->t_data_sg[0]);
		if (!buf) {
			pr_err("Unable to get buf for scatterlist\n");
			goto after_mode_select;
		}

		if (cdb[0] == MODE_SELECT)
			bdl = buf[3];
		else
			bdl = get_unaligned_be16(&buf[6]);

		if (!bdl)
			goto after_mode_select;

		if (cdb[0] == MODE_SELECT)
			blocksize = get_unaligned_be24(&buf[9]);
		else
			blocksize = get_unaligned_be24(&buf[13]);

		sd->sector_size = blocksize;
	}
after_mode_select:

	if (scsi_status == SAM_STAT_CHECK_CONDITION) {
		transport_copy_sense_to_cmd(cmd, req_sense);

		/*
		 * check for TAPE device reads with
		 * FM/EOM/ILI set, so that we can get data
		 * back despite framework assumption that a
		 * check condition means there is no data
		 */
		if (sd->type == TYPE_TAPE &&
		    cmd->data_direction == DMA_FROM_DEVICE) {
			/*
			 * is sense data valid, fixed format,
			 * and have FM, EOM, or ILI set?
			 */
			if (req_sense[0] == 0xf0 &&	/* valid, fixed format */
			    req_sense[2] & 0xe0 &&	/* FM, EOM, or ILI */
			    (req_sense[2] & 0xf) == 0) { /* key==NO_SENSE */
				pr_debug("Tape FM/EOM/ILI status detected. Treat as normal read.\n");
				cmd->se_cmd_flags |= SCF_TREAT_READ_AS_NORMAL;
			}
		}
	}
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

static ssize_t pscsi_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
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
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			pdv->pdv_host_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Host ID:"
				" %d\n", phv->phv_host_id, pdv->pdv_host_id);
			pdv->pdv_flags |= PDF_HAS_VIRT_HOST_ID;
			break;
		case Opt_scsi_channel_id:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			pdv->pdv_channel_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Channel"
				" ID: %d\n",  phv->phv_host_id,
				pdv->pdv_channel_id);
			pdv->pdv_flags |= PDF_HAS_CHANNEL_ID;
			break;
		case Opt_scsi_target_id:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			pdv->pdv_target_id = arg;
			pr_debug("PSCSI[%d]: Referencing SCSI Target"
				" ID: %d\n", phv->phv_host_id,
				pdv->pdv_target_id);
			pdv->pdv_flags |= PDF_HAS_TARGET_ID;
			break;
		case Opt_scsi_lun_id:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
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

static ssize_t pscsi_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct pscsi_hba_virt *phv = dev->se_hba->hba_ptr;
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct scsi_device *sd = pdv->pdv_sd;
	unsigned char host_id[16];
	ssize_t bl;

	if (phv->phv_mode == PHV_VIRTUAL_HOST_ID)
		snprintf(host_id, 16, "%d", pdv->pdv_host_id);
	else
		snprintf(host_id, 16, "PHBA Mode");

	bl = sprintf(b, "SCSI Device Bus Location:"
		" Channel ID: %d Target ID: %d LUN: %d Host ID: %s\n",
		pdv->pdv_channel_id, pdv->pdv_target_id, pdv->pdv_lun_id,
		host_id);

	if (sd) {
		bl += sprintf(b + bl, "        Vendor: %."
			__stringify(INQUIRY_VENDOR_LEN) "s", sd->vendor);
		bl += sprintf(b + bl, " Model: %."
			__stringify(INQUIRY_MODEL_LEN) "s", sd->model);
		bl += sprintf(b + bl, " Rev: %."
			__stringify(INQUIRY_REVISION_LEN) "s\n", sd->rev);
	}
	return bl;
}

static void pscsi_bi_endio(struct bio *bio)
{
	bio_put(bio);
}

static inline struct bio *pscsi_get_bio(int nr_vecs)
{
	struct bio *bio;
	/*
	 * Use bio_malloc() following the comment in for bio -> struct request
	 * in block/blk-core.c:blk_make_request()
	 */
	bio = bio_kmalloc(GFP_KERNEL, nr_vecs);
	if (!bio) {
		pr_err("PSCSI: bio_kmalloc() failed\n");
		return NULL;
	}
	bio->bi_end_io = pscsi_bi_endio;

	return bio;
}

static sense_reason_t
pscsi_map_sg(struct se_cmd *cmd, struct scatterlist *sgl, u32 sgl_nents,
		struct request *req)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(cmd->se_dev);
	struct bio *bio = NULL;
	struct page *page;
	struct scatterlist *sg;
	u32 data_len = cmd->data_length, i, len, bytes, off;
	int nr_pages = (cmd->data_length + sgl[0].offset +
			PAGE_SIZE - 1) >> PAGE_SHIFT;
	int nr_vecs = 0, rc;
	int rw = (cmd->data_direction == DMA_TO_DEVICE);

	BUG_ON(!cmd->data_length);

	pr_debug("PSCSI: nr_pages: %d\n", nr_pages);

	for_each_sg(sgl, sg, sgl_nents, i) {
		page = sg_page(sg);
		off = sg->offset;
		len = sg->length;

		pr_debug("PSCSI: i: %d page: %p len: %d off: %d\n", i,
			page, len, off);

		/*
		 * We only have one page of data in each sg element,
		 * we can not cross a page boundary.
		 */
		if (off + len > PAGE_SIZE)
			goto fail;

		if (len > 0 && data_len > 0) {
			bytes = min_t(unsigned int, len, PAGE_SIZE - off);
			bytes = min(bytes, data_len);

			if (!bio) {
new_bio:
				nr_vecs = bio_max_segs(nr_pages);
				/*
				 * Calls bio_kmalloc() and sets bio->bi_end_io()
				 */
				bio = pscsi_get_bio(nr_vecs);
				if (!bio)
					goto fail;

				if (rw)
					bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

				pr_debug("PSCSI: Allocated bio: %p,"
					" dir: %s nr_vecs: %d\n", bio,
					(rw) ? "rw" : "r", nr_vecs);
			}

			pr_debug("PSCSI: Calling bio_add_pc_page() i: %d"
				" bio: %p page: %p len: %d off: %d\n", i, bio,
				page, len, off);

			rc = bio_add_pc_page(pdv->pdv_sd->request_queue,
					bio, page, bytes, off);
			pr_debug("PSCSI: bio->bi_vcnt: %d nr_vecs: %d\n",
				bio_segments(bio), nr_vecs);
			if (rc != bytes) {
				pr_debug("PSCSI: Reached bio->bi_vcnt max:"
					" %d i: %d bio: %p, allocating another"
					" bio\n", bio->bi_vcnt, i, bio);

				rc = blk_rq_append_bio(req, bio);
				if (rc) {
					pr_err("pSCSI: failed to append bio\n");
					goto fail;
				}

				/*
				 * Clear the pointer so that another bio will
				 * be allocated with pscsi_get_bio() above.
				 */
				bio = NULL;
				goto new_bio;
			}

			data_len -= bytes;
		}
	}

	if (bio) {
		rc = blk_rq_append_bio(req, bio);
		if (rc) {
			pr_err("pSCSI: failed to append bio\n");
			goto fail;
		}
	}

	return 0;
fail:
	if (bio)
		bio_put(bio);
	while (req->bio) {
		bio = req->bio;
		req->bio = bio->bi_next;
		bio_put(bio);
	}
	req->biotail = NULL;
	return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

static sense_reason_t
pscsi_parse_cdb(struct se_cmd *cmd)
{
	if (cmd->se_cmd_flags & SCF_BIDI)
		return TCM_UNSUPPORTED_SCSI_OPCODE;

	return passthrough_parse_cdb(cmd, pscsi_execute_cmd);
}

static sense_reason_t
pscsi_execute_cmd(struct se_cmd *cmd)
{
	struct scatterlist *sgl = cmd->t_data_sg;
	u32 sgl_nents = cmd->t_data_nents;
	struct pscsi_dev_virt *pdv = PSCSI_DEV(cmd->se_dev);
	struct pscsi_plugin_task *pt;
	struct request *req;
	sense_reason_t ret;

	/*
	 * Dynamically alloc cdb space, since it may be larger than
	 * TCM_MAX_COMMAND_SIZE
	 */
	pt = kzalloc(sizeof(*pt) + scsi_command_size(cmd->t_task_cdb), GFP_KERNEL);
	if (!pt) {
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	cmd->priv = pt;

	memcpy(pt->pscsi_cdb, cmd->t_task_cdb,
		scsi_command_size(cmd->t_task_cdb));

	req = scsi_alloc_request(pdv->pdv_sd->request_queue,
			cmd->data_direction == DMA_TO_DEVICE ?
			REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(req)) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto fail;
	}

	if (sgl) {
		ret = pscsi_map_sg(cmd, sgl, sgl_nents, req);
		if (ret)
			goto fail_put_request;
	}

	req->end_io = pscsi_req_done;
	req->end_io_data = cmd;
	scsi_req(req)->cmd_len = scsi_command_size(pt->pscsi_cdb);
	scsi_req(req)->cmd = &pt->pscsi_cdb[0];
	if (pdv->pdv_sd->type == TYPE_DISK ||
	    pdv->pdv_sd->type == TYPE_ZBC)
		req->timeout = PS_TIMEOUT_DISK;
	else
		req->timeout = PS_TIMEOUT_OTHER;
	scsi_req(req)->retries = PS_RETRY;

	blk_execute_rq_nowait(req, cmd->sam_task_attr == TCM_HEAD_TAG,
			pscsi_req_done);

	return 0;

fail_put_request:
	blk_mq_free_request(req);
fail:
	kfree(pt);
	return ret;
}

/*	pscsi_get_device_type():
 *
 *
 */
static u32 pscsi_get_device_type(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);
	struct scsi_device *sd = pdv->pdv_sd;

	return (sd) ? sd->type : TYPE_NO_LUN;
}

static sector_t pscsi_get_blocks(struct se_device *dev)
{
	struct pscsi_dev_virt *pdv = PSCSI_DEV(dev);

	if (pdv->pdv_bd)
		return bdev_nr_sectors(pdv->pdv_bd);
	return 0;
}

static void pscsi_req_done(struct request *req, blk_status_t status)
{
	struct se_cmd *cmd = req->end_io_data;
	struct pscsi_plugin_task *pt = cmd->priv;
	int result = scsi_req(req)->result;
	enum sam_status scsi_status = result & 0xff;

	if (scsi_status != SAM_STAT_GOOD) {
		pr_debug("PSCSI Status Byte exception at cmd: %p CDB:"
			" 0x%02x Result: 0x%08x\n", cmd, pt->pscsi_cdb[0],
			result);
	}

	pscsi_complete_cmd(cmd, scsi_status, scsi_req(req)->sense);

	switch (host_byte(result)) {
	case DID_OK:
		target_complete_cmd_with_length(cmd, scsi_status,
			cmd->data_length - scsi_req(req)->resid_len);
		break;
	default:
		pr_debug("PSCSI Host Byte exception at cmd: %p CDB:"
			" 0x%02x Result: 0x%08x\n", cmd, pt->pscsi_cdb[0],
			result);
		target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
		break;
	}

	blk_mq_free_request(req);
	kfree(pt);
}

static const struct target_backend_ops pscsi_ops = {
	.name			= "pscsi",
	.owner			= THIS_MODULE,
	.transport_flags_default = TRANSPORT_FLAG_PASSTHROUGH |
				   TRANSPORT_FLAG_PASSTHROUGH_ALUA |
				   TRANSPORT_FLAG_PASSTHROUGH_PGR,
	.attach_hba		= pscsi_attach_hba,
	.detach_hba		= pscsi_detach_hba,
	.pmode_enable_hba	= pscsi_pmode_enable_hba,
	.alloc_device		= pscsi_alloc_device,
	.configure_device	= pscsi_configure_device,
	.destroy_device		= pscsi_destroy_device,
	.free_device		= pscsi_free_device,
	.parse_cdb		= pscsi_parse_cdb,
	.set_configfs_dev_params = pscsi_set_configfs_dev_params,
	.show_configfs_dev_params = pscsi_show_configfs_dev_params,
	.get_device_type	= pscsi_get_device_type,
	.get_blocks		= pscsi_get_blocks,
	.tb_dev_attrib_attrs	= passthrough_attrib_attrs,
};

static int __init pscsi_module_init(void)
{
	return transport_backend_register(&pscsi_ops);
}

static void __exit pscsi_module_exit(void)
{
	target_backend_unregister(&pscsi_ops);
}

MODULE_DESCRIPTION("TCM PSCSI subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(pscsi_module_init);
module_exit(pscsi_module_exit);
