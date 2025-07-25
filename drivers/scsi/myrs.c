// SPDX-License-Identifier: GPL-2.0
/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * This driver supports the newer, SCSI-based firmware interface only.
 *
 * Copyright 2017 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver, which has
 * Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
 * Portions Copyright 2002 by Mylex (An IBM Business Unit)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/raid_class.h>
#include <linux/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include "myrs.h"

static struct raid_template *myrs_raid_template;

static struct myrs_devstate_name_entry {
	enum myrs_devstate state;
	char *name;
} myrs_devstate_name_list[] = {
	{ MYRS_DEVICE_UNCONFIGURED, "Unconfigured" },
	{ MYRS_DEVICE_ONLINE, "Online" },
	{ MYRS_DEVICE_REBUILD, "Rebuild" },
	{ MYRS_DEVICE_MISSING, "Missing" },
	{ MYRS_DEVICE_SUSPECTED_CRITICAL, "SuspectedCritical" },
	{ MYRS_DEVICE_OFFLINE, "Offline" },
	{ MYRS_DEVICE_CRITICAL, "Critical" },
	{ MYRS_DEVICE_SUSPECTED_DEAD, "SuspectedDead" },
	{ MYRS_DEVICE_COMMANDED_OFFLINE, "CommandedOffline" },
	{ MYRS_DEVICE_STANDBY, "Standby" },
	{ MYRS_DEVICE_INVALID_STATE, "Invalid" },
};

static char *myrs_devstate_name(enum myrs_devstate state)
{
	struct myrs_devstate_name_entry *entry = myrs_devstate_name_list;
	int i;

	for (i = 0; i < ARRAY_SIZE(myrs_devstate_name_list); i++) {
		if (entry[i].state == state)
			return entry[i].name;
	}
	return NULL;
}

static struct myrs_raid_level_name_entry {
	enum myrs_raid_level level;
	char *name;
} myrs_raid_level_name_list[] = {
	{ MYRS_RAID_LEVEL0, "RAID0" },
	{ MYRS_RAID_LEVEL1, "RAID1" },
	{ MYRS_RAID_LEVEL3, "RAID3 right asymmetric parity" },
	{ MYRS_RAID_LEVEL5, "RAID5 right asymmetric parity" },
	{ MYRS_RAID_LEVEL6, "RAID6" },
	{ MYRS_RAID_JBOD, "JBOD" },
	{ MYRS_RAID_NEWSPAN, "New Mylex SPAN" },
	{ MYRS_RAID_LEVEL3F, "RAID3 fixed parity" },
	{ MYRS_RAID_LEVEL3L, "RAID3 left symmetric parity" },
	{ MYRS_RAID_SPAN, "Mylex SPAN" },
	{ MYRS_RAID_LEVEL5L, "RAID5 left symmetric parity" },
	{ MYRS_RAID_LEVELE, "RAIDE (concatenation)" },
	{ MYRS_RAID_PHYSICAL, "Physical device" },
};

static char *myrs_raid_level_name(enum myrs_raid_level level)
{
	struct myrs_raid_level_name_entry *entry = myrs_raid_level_name_list;
	int i;

	for (i = 0; i < ARRAY_SIZE(myrs_raid_level_name_list); i++) {
		if (entry[i].level == level)
			return entry[i].name;
	}
	return NULL;
}

/*
 * myrs_reset_cmd - clears critical fields in struct myrs_cmdblk
 */
static inline void myrs_reset_cmd(struct myrs_cmdblk *cmd_blk)
{
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;

	memset(mbox, 0, sizeof(union myrs_cmd_mbox));
	cmd_blk->status = 0;
}

/*
 * myrs_qcmd - queues Command for DAC960 V2 Series Controllers.
 */
static void myrs_qcmd(struct myrs_hba *cs, struct myrs_cmdblk *cmd_blk)
{
	void __iomem *base = cs->io_base;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	union myrs_cmd_mbox *next_mbox = cs->next_cmd_mbox;

	cs->write_cmd_mbox(next_mbox, mbox);

	if (cs->prev_cmd_mbox1->words[0] == 0 ||
	    cs->prev_cmd_mbox2->words[0] == 0)
		cs->get_cmd_mbox(base);

	cs->prev_cmd_mbox2 = cs->prev_cmd_mbox1;
	cs->prev_cmd_mbox1 = next_mbox;

	if (++next_mbox > cs->last_cmd_mbox)
		next_mbox = cs->first_cmd_mbox;

	cs->next_cmd_mbox = next_mbox;
}

/*
 * myrs_exec_cmd - executes V2 Command and waits for completion.
 */
static void myrs_exec_cmd(struct myrs_hba *cs,
		struct myrs_cmdblk *cmd_blk)
{
	DECLARE_COMPLETION_ONSTACK(complete);
	unsigned long flags;

	cmd_blk->complete = &complete;
	spin_lock_irqsave(&cs->queue_lock, flags);
	myrs_qcmd(cs, cmd_blk);
	spin_unlock_irqrestore(&cs->queue_lock, flags);

	wait_for_completion(&complete);
}

/*
 * myrs_report_progress - prints progress message
 */
static void myrs_report_progress(struct myrs_hba *cs, unsigned short ldev_num,
		unsigned char *msg, unsigned long blocks,
		unsigned long size)
{
	shost_printk(KERN_INFO, cs->host,
		     "Logical Drive %d: %s in Progress: %d%% completed\n",
		     ldev_num, msg,
		     (100 * (int)(blocks >> 7)) / (int)(size >> 7));
}

/*
 * myrs_get_ctlr_info - executes a Controller Information IOCTL Command
 */
static unsigned char myrs_get_ctlr_info(struct myrs_hba *cs)
{
	struct myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t ctlr_info_addr;
	union myrs_sgl *sgl;
	unsigned char status;
	unsigned short ldev_present, ldev_critical, ldev_offline;

	ldev_present = cs->ctlr_info->ldev_present;
	ldev_critical = cs->ctlr_info->ldev_critical;
	ldev_offline = cs->ctlr_info->ldev_offline;

	ctlr_info_addr = dma_map_single(&cs->pdev->dev, cs->ctlr_info,
					sizeof(struct myrs_ctlr_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, ctlr_info_addr))
		return MYRS_STATUS_FAILED;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->ctlr_info.id = MYRS_DCMD_TAG;
	mbox->ctlr_info.opcode = MYRS_CMD_OP_IOCTL;
	mbox->ctlr_info.control.dma_ctrl_to_host = true;
	mbox->ctlr_info.control.no_autosense = true;
	mbox->ctlr_info.dma_size = sizeof(struct myrs_ctlr_info);
	mbox->ctlr_info.ctlr_num = 0;
	mbox->ctlr_info.ioctl_opcode = MYRS_IOCTL_GET_CTLR_INFO;
	sgl = &mbox->ctlr_info.dma_addr;
	sgl->sge[0].sge_addr = ctlr_info_addr;
	sgl->sge[0].sge_count = mbox->ctlr_info.dma_size;
	dev_dbg(&cs->host->shost_gendev, "Sending GetControllerInfo\n");
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, ctlr_info_addr,
			 sizeof(struct myrs_ctlr_info), DMA_FROM_DEVICE);
	if (status == MYRS_STATUS_SUCCESS) {
		if (cs->ctlr_info->bg_init_active +
		    cs->ctlr_info->ldev_init_active +
		    cs->ctlr_info->pdev_init_active +
		    cs->ctlr_info->cc_active +
		    cs->ctlr_info->rbld_active +
		    cs->ctlr_info->exp_active != 0)
			cs->needs_update = true;
		if (cs->ctlr_info->ldev_present != ldev_present ||
		    cs->ctlr_info->ldev_critical != ldev_critical ||
		    cs->ctlr_info->ldev_offline != ldev_offline)
			shost_printk(KERN_INFO, cs->host,
				     "Logical drive count changes (%d/%d/%d)\n",
				     cs->ctlr_info->ldev_critical,
				     cs->ctlr_info->ldev_offline,
				     cs->ctlr_info->ldev_present);
	}

	return status;
}

/*
 * myrs_get_ldev_info - executes a Logical Device Information IOCTL Command
 */
static unsigned char myrs_get_ldev_info(struct myrs_hba *cs,
		unsigned short ldev_num, struct myrs_ldev_info *ldev_info)
{
	struct myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t ldev_info_addr;
	struct myrs_ldev_info ldev_info_orig;
	union myrs_sgl *sgl;
	unsigned char status;

	memcpy(&ldev_info_orig, ldev_info, sizeof(struct myrs_ldev_info));
	ldev_info_addr = dma_map_single(&cs->pdev->dev, ldev_info,
					sizeof(struct myrs_ldev_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, ldev_info_addr))
		return MYRS_STATUS_FAILED;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->ldev_info.id = MYRS_DCMD_TAG;
	mbox->ldev_info.opcode = MYRS_CMD_OP_IOCTL;
	mbox->ldev_info.control.dma_ctrl_to_host = true;
	mbox->ldev_info.control.no_autosense = true;
	mbox->ldev_info.dma_size = sizeof(struct myrs_ldev_info);
	mbox->ldev_info.ldev.ldev_num = ldev_num;
	mbox->ldev_info.ioctl_opcode = MYRS_IOCTL_GET_LDEV_INFO_VALID;
	sgl = &mbox->ldev_info.dma_addr;
	sgl->sge[0].sge_addr = ldev_info_addr;
	sgl->sge[0].sge_count = mbox->ldev_info.dma_size;
	dev_dbg(&cs->host->shost_gendev,
		"Sending GetLogicalDeviceInfoValid for ldev %d\n", ldev_num);
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, ldev_info_addr,
			 sizeof(struct myrs_ldev_info), DMA_FROM_DEVICE);
	if (status == MYRS_STATUS_SUCCESS) {
		unsigned short ldev_num = ldev_info->ldev_num;
		struct myrs_ldev_info *new = ldev_info;
		struct myrs_ldev_info *old = &ldev_info_orig;
		unsigned long ldev_size = new->cfg_devsize;

		if (new->dev_state != old->dev_state) {
			const char *name;

			name = myrs_devstate_name(new->dev_state);
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d is now %s\n",
				     ldev_num, name ? name : "Invalid");
		}
		if ((new->soft_errs != old->soft_errs) ||
		    (new->cmds_failed != old->cmds_failed) ||
		    (new->deferred_write_errs != old->deferred_write_errs))
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d Errors: Soft = %d, Failed = %d, Deferred Write = %d\n",
				     ldev_num, new->soft_errs,
				     new->cmds_failed,
				     new->deferred_write_errs);
		if (new->bg_init_active)
			myrs_report_progress(cs, ldev_num,
					     "Background Initialization",
					     new->bg_init_lba, ldev_size);
		else if (new->fg_init_active)
			myrs_report_progress(cs, ldev_num,
					     "Foreground Initialization",
					     new->fg_init_lba, ldev_size);
		else if (new->migration_active)
			myrs_report_progress(cs, ldev_num,
					     "Data Migration",
					     new->migration_lba, ldev_size);
		else if (new->patrol_active)
			myrs_report_progress(cs, ldev_num,
					     "Patrol Operation",
					     new->patrol_lba, ldev_size);
		if (old->bg_init_active && !new->bg_init_active)
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d: Background Initialization %s\n",
				     ldev_num,
				     (new->ldev_control.ldev_init_done ?
				      "Completed" : "Failed"));
	}
	return status;
}

/*
 * myrs_get_pdev_info - executes a "Read Physical Device Information" Command
 */
static unsigned char myrs_get_pdev_info(struct myrs_hba *cs,
		unsigned char channel, unsigned char target, unsigned char lun,
		struct myrs_pdev_info *pdev_info)
{
	struct myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t pdev_info_addr;
	union myrs_sgl *sgl;
	unsigned char status;

	pdev_info_addr = dma_map_single(&cs->pdev->dev, pdev_info,
					sizeof(struct myrs_pdev_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, pdev_info_addr))
		return MYRS_STATUS_FAILED;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->pdev_info.opcode = MYRS_CMD_OP_IOCTL;
	mbox->pdev_info.id = MYRS_DCMD_TAG;
	mbox->pdev_info.control.dma_ctrl_to_host = true;
	mbox->pdev_info.control.no_autosense = true;
	mbox->pdev_info.dma_size = sizeof(struct myrs_pdev_info);
	mbox->pdev_info.pdev.lun = lun;
	mbox->pdev_info.pdev.target = target;
	mbox->pdev_info.pdev.channel = channel;
	mbox->pdev_info.ioctl_opcode = MYRS_IOCTL_GET_PDEV_INFO_VALID;
	sgl = &mbox->pdev_info.dma_addr;
	sgl->sge[0].sge_addr = pdev_info_addr;
	sgl->sge[0].sge_count = mbox->pdev_info.dma_size;
	dev_dbg(&cs->host->shost_gendev,
		"Sending GetPhysicalDeviceInfoValid for pdev %d:%d:%d\n",
		channel, target, lun);
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, pdev_info_addr,
			 sizeof(struct myrs_pdev_info), DMA_FROM_DEVICE);
	return status;
}

/*
 * myrs_dev_op - executes a "Device Operation" Command
 */
static unsigned char myrs_dev_op(struct myrs_hba *cs,
		enum myrs_ioctl_opcode opcode, enum myrs_opdev opdev)
{
	struct myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	unsigned char status;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->dev_op.opcode = MYRS_CMD_OP_IOCTL;
	mbox->dev_op.id = MYRS_DCMD_TAG;
	mbox->dev_op.control.dma_ctrl_to_host = true;
	mbox->dev_op.control.no_autosense = true;
	mbox->dev_op.ioctl_opcode = opcode;
	mbox->dev_op.opdev = opdev;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	return status;
}

/*
 * myrs_translate_pdev - translates a Physical Device Channel and
 * TargetID into a Logical Device.
 */
static unsigned char myrs_translate_pdev(struct myrs_hba *cs,
		unsigned char channel, unsigned char target, unsigned char lun,
		struct myrs_devmap *devmap)
{
	struct pci_dev *pdev = cs->pdev;
	dma_addr_t devmap_addr;
	struct myrs_cmdblk *cmd_blk;
	union myrs_cmd_mbox *mbox;
	union myrs_sgl *sgl;
	unsigned char status;

	memset(devmap, 0x0, sizeof(struct myrs_devmap));
	devmap_addr = dma_map_single(&pdev->dev, devmap,
				     sizeof(struct myrs_devmap),
				     DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, devmap_addr))
		return MYRS_STATUS_FAILED;

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	mbox = &cmd_blk->mbox;
	mbox->pdev_info.opcode = MYRS_CMD_OP_IOCTL;
	mbox->pdev_info.control.dma_ctrl_to_host = true;
	mbox->pdev_info.control.no_autosense = true;
	mbox->pdev_info.dma_size = sizeof(struct myrs_devmap);
	mbox->pdev_info.pdev.target = target;
	mbox->pdev_info.pdev.channel = channel;
	mbox->pdev_info.pdev.lun = lun;
	mbox->pdev_info.ioctl_opcode = MYRS_IOCTL_XLATE_PDEV_TO_LDEV;
	sgl = &mbox->pdev_info.dma_addr;
	sgl->sge[0].sge_addr = devmap_addr;
	sgl->sge[0].sge_count = mbox->pdev_info.dma_size;

	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&pdev->dev, devmap_addr,
			 sizeof(struct myrs_devmap), DMA_FROM_DEVICE);
	return status;
}

/*
 * myrs_get_event - executes a Get Event Command
 */
static unsigned char myrs_get_event(struct myrs_hba *cs,
		unsigned int event_num, struct myrs_event *event_buf)
{
	struct pci_dev *pdev = cs->pdev;
	dma_addr_t event_addr;
	struct myrs_cmdblk *cmd_blk = &cs->mcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	union myrs_sgl *sgl;
	unsigned char status;

	event_addr = dma_map_single(&pdev->dev, event_buf,
				    sizeof(struct myrs_event), DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, event_addr))
		return MYRS_STATUS_FAILED;

	mbox->get_event.opcode = MYRS_CMD_OP_IOCTL;
	mbox->get_event.dma_size = sizeof(struct myrs_event);
	mbox->get_event.evnum_upper = event_num >> 16;
	mbox->get_event.ctlr_num = 0;
	mbox->get_event.ioctl_opcode = MYRS_IOCTL_GET_EVENT;
	mbox->get_event.evnum_lower = event_num & 0xFFFF;
	sgl = &mbox->get_event.dma_addr;
	sgl->sge[0].sge_addr = event_addr;
	sgl->sge[0].sge_count = mbox->get_event.dma_size;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	dma_unmap_single(&pdev->dev, event_addr,
			 sizeof(struct myrs_event), DMA_FROM_DEVICE);

	return status;
}

/*
 * myrs_get_fwstatus - executes a Get Health Status Command
 */
static unsigned char myrs_get_fwstatus(struct myrs_hba *cs)
{
	struct myrs_cmdblk *cmd_blk = &cs->mcmd_blk;
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	union myrs_sgl *sgl;
	unsigned char status = cmd_blk->status;

	myrs_reset_cmd(cmd_blk);
	mbox->common.opcode = MYRS_CMD_OP_IOCTL;
	mbox->common.id = MYRS_MCMD_TAG;
	mbox->common.control.dma_ctrl_to_host = true;
	mbox->common.control.no_autosense = true;
	mbox->common.dma_size = sizeof(struct myrs_fwstat);
	mbox->common.ioctl_opcode = MYRS_IOCTL_GET_HEALTH_STATUS;
	sgl = &mbox->common.dma_addr;
	sgl->sge[0].sge_addr = cs->fwstat_addr;
	sgl->sge[0].sge_count = mbox->ctlr_info.dma_size;
	dev_dbg(&cs->host->shost_gendev, "Sending GetHealthStatus\n");
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;

	return status;
}

/*
 * myrs_enable_mmio_mbox - enables the Memory Mailbox Interface
 */
static bool myrs_enable_mmio_mbox(struct myrs_hba *cs,
		enable_mbox_t enable_mbox_fn)
{
	void __iomem *base = cs->io_base;
	struct pci_dev *pdev = cs->pdev;
	union myrs_cmd_mbox *cmd_mbox;
	struct myrs_stat_mbox *stat_mbox;
	union myrs_cmd_mbox *mbox;
	dma_addr_t mbox_addr;
	unsigned char status = MYRS_STATUS_FAILED;

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)))
		if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
			dev_err(&pdev->dev, "DMA mask out of range\n");
			return false;
		}

	/* Temporary dma mapping, used only in the scope of this function */
	mbox = dma_alloc_coherent(&pdev->dev, sizeof(union myrs_cmd_mbox),
				  &mbox_addr, GFP_KERNEL);
	if (!mbox)
		return false;

	/* These are the base addresses for the command memory mailbox array */
	cs->cmd_mbox_size = MYRS_MAX_CMD_MBOX * sizeof(union myrs_cmd_mbox);
	cmd_mbox = dma_alloc_coherent(&pdev->dev, cs->cmd_mbox_size,
				      &cs->cmd_mbox_addr, GFP_KERNEL);
	if (!cmd_mbox) {
		dev_err(&pdev->dev, "Failed to map command mailbox\n");
		goto out_free;
	}
	cs->first_cmd_mbox = cmd_mbox;
	cmd_mbox += MYRS_MAX_CMD_MBOX - 1;
	cs->last_cmd_mbox = cmd_mbox;
	cs->next_cmd_mbox = cs->first_cmd_mbox;
	cs->prev_cmd_mbox1 = cs->last_cmd_mbox;
	cs->prev_cmd_mbox2 = cs->last_cmd_mbox - 1;

	/* These are the base addresses for the status memory mailbox array */
	cs->stat_mbox_size = MYRS_MAX_STAT_MBOX * sizeof(struct myrs_stat_mbox);
	stat_mbox = dma_alloc_coherent(&pdev->dev, cs->stat_mbox_size,
				       &cs->stat_mbox_addr, GFP_KERNEL);
	if (!stat_mbox) {
		dev_err(&pdev->dev, "Failed to map status mailbox\n");
		goto out_free;
	}

	cs->first_stat_mbox = stat_mbox;
	stat_mbox += MYRS_MAX_STAT_MBOX - 1;
	cs->last_stat_mbox = stat_mbox;
	cs->next_stat_mbox = cs->first_stat_mbox;

	cs->fwstat_buf = dma_alloc_coherent(&pdev->dev,
					    sizeof(struct myrs_fwstat),
					    &cs->fwstat_addr, GFP_KERNEL);
	if (!cs->fwstat_buf) {
		dev_err(&pdev->dev, "Failed to map firmware health buffer\n");
		cs->fwstat_buf = NULL;
		goto out_free;
	}
	cs->ctlr_info = kzalloc(sizeof(struct myrs_ctlr_info), GFP_KERNEL);
	if (!cs->ctlr_info)
		goto out_free;

	cs->event_buf = kzalloc(sizeof(struct myrs_event), GFP_KERNEL);
	if (!cs->event_buf)
		goto out_free;

	/* Enable the Memory Mailbox Interface. */
	memset(mbox, 0, sizeof(union myrs_cmd_mbox));
	mbox->set_mbox.id = 1;
	mbox->set_mbox.opcode = MYRS_CMD_OP_IOCTL;
	mbox->set_mbox.control.no_autosense = true;
	mbox->set_mbox.first_cmd_mbox_size_kb =
		(MYRS_MAX_CMD_MBOX * sizeof(union myrs_cmd_mbox)) >> 10;
	mbox->set_mbox.first_stat_mbox_size_kb =
		(MYRS_MAX_STAT_MBOX * sizeof(struct myrs_stat_mbox)) >> 10;
	mbox->set_mbox.second_cmd_mbox_size_kb = 0;
	mbox->set_mbox.second_stat_mbox_size_kb = 0;
	mbox->set_mbox.sense_len = 0;
	mbox->set_mbox.ioctl_opcode = MYRS_IOCTL_SET_MEM_MBOX;
	mbox->set_mbox.fwstat_buf_size_kb = 1;
	mbox->set_mbox.fwstat_buf_addr = cs->fwstat_addr;
	mbox->set_mbox.first_cmd_mbox_addr = cs->cmd_mbox_addr;
	mbox->set_mbox.first_stat_mbox_addr = cs->stat_mbox_addr;
	status = enable_mbox_fn(base, mbox_addr);

out_free:
	dma_free_coherent(&pdev->dev, sizeof(union myrs_cmd_mbox),
			  mbox, mbox_addr);
	if (status != MYRS_STATUS_SUCCESS)
		dev_err(&pdev->dev, "Failed to enable mailbox, status %X\n",
			status);
	return (status == MYRS_STATUS_SUCCESS);
}

/*
 * myrs_get_config - reads the Configuration Information
 */
static int myrs_get_config(struct myrs_hba *cs)
{
	struct myrs_ctlr_info *info = cs->ctlr_info;
	struct Scsi_Host *shost = cs->host;
	unsigned char status;
	unsigned char model[20];
	unsigned char fw_version[12];
	int i, model_len;

	/* Get data into dma-able area, then copy into permanent location */
	mutex_lock(&cs->cinfo_mutex);
	status = myrs_get_ctlr_info(cs);
	mutex_unlock(&cs->cinfo_mutex);
	if (status != MYRS_STATUS_SUCCESS) {
		shost_printk(KERN_ERR, shost,
			     "Failed to get controller information\n");
		return -ENODEV;
	}

	/* Initialize the Controller Model Name and Full Model Name fields. */
	model_len = sizeof(info->ctlr_name);
	if (model_len > sizeof(model)-1)
		model_len = sizeof(model)-1;
	memcpy(model, info->ctlr_name, model_len);
	model_len--;
	while (model[model_len] == ' ' || model[model_len] == '\0')
		model_len--;
	model[++model_len] = '\0';
	strcpy(cs->model_name, "DAC960 ");
	strcat(cs->model_name, model);
	/* Initialize the Controller Firmware Version field. */
	sprintf(fw_version, "%d.%02d-%02d",
		info->fw_major_version, info->fw_minor_version,
		info->fw_turn_number);
	if (info->fw_major_version == 6 &&
	    info->fw_minor_version == 0 &&
	    info->fw_turn_number < 1) {
		shost_printk(KERN_WARNING, shost,
			"FIRMWARE VERSION %s DOES NOT PROVIDE THE CONTROLLER\n"
			"STATUS MONITORING FUNCTIONALITY NEEDED BY THIS DRIVER.\n"
			"PLEASE UPGRADE TO VERSION 6.00-01 OR ABOVE.\n",
			fw_version);
		return -ENODEV;
	}
	/* Initialize the Controller Channels and Targets. */
	shost->max_channel = info->physchan_present + info->virtchan_present;
	shost->max_id = info->max_targets[0];
	for (i = 1; i < 16; i++) {
		if (!info->max_targets[i])
			continue;
		if (shost->max_id < info->max_targets[i])
			shost->max_id = info->max_targets[i];
	}

	/*
	 * Initialize the Controller Queue Depth, Driver Queue Depth,
	 * Logical Drive Count, Maximum Blocks per Command, Controller
	 * Scatter/Gather Limit, and Driver Scatter/Gather Limit.
	 * The Driver Queue Depth must be at most three less than
	 * the Controller Queue Depth; tag '1' is reserved for
	 * direct commands, and tag '2' for monitoring commands.
	 */
	shost->can_queue = info->max_tcq - 3;
	if (shost->can_queue > MYRS_MAX_CMD_MBOX - 3)
		shost->can_queue = MYRS_MAX_CMD_MBOX - 3;
	shost->max_sectors = info->max_transfer_size;
	shost->sg_tablesize = info->max_sge;
	if (shost->sg_tablesize > MYRS_SG_LIMIT)
		shost->sg_tablesize = MYRS_SG_LIMIT;

	shost_printk(KERN_INFO, shost,
		"Configuring %s PCI RAID Controller\n", model);
	shost_printk(KERN_INFO, shost,
		"  Firmware Version: %s, Channels: %d, Memory Size: %dMB\n",
		fw_version, info->physchan_present, info->mem_size_mb);

	shost_printk(KERN_INFO, shost,
		     "  Controller Queue Depth: %d, Maximum Blocks per Command: %d\n",
		     shost->can_queue, shost->max_sectors);

	shost_printk(KERN_INFO, shost,
		     "  Driver Queue Depth: %d, Scatter/Gather Limit: %d of %d Segments\n",
		     shost->can_queue, shost->sg_tablesize, MYRS_SG_LIMIT);
	for (i = 0; i < info->physchan_max; i++) {
		if (!info->max_targets[i])
			continue;
		shost_printk(KERN_INFO, shost,
			     "  Device Channel %d: max %d devices\n",
			     i, info->max_targets[i]);
	}
	shost_printk(KERN_INFO, shost,
		     "  Physical: %d/%d channels, %d disks, %d devices\n",
		     info->physchan_present, info->physchan_max,
		     info->pdisk_present, info->pdev_present);

	shost_printk(KERN_INFO, shost,
		     "  Logical: %d/%d channels, %d disks\n",
		     info->virtchan_present, info->virtchan_max,
		     info->ldev_present);
	return 0;
}

/*
 * myrs_log_event - prints a Controller Event message
 */
static struct {
	int ev_code;
	unsigned char *ev_msg;
} myrs_ev_list[] = {
	/* Physical Device Events (0x0000 - 0x007F) */
	{ 0x0001, "P Online" },
	{ 0x0002, "P Standby" },
	{ 0x0005, "P Automatic Rebuild Started" },
	{ 0x0006, "P Manual Rebuild Started" },
	{ 0x0007, "P Rebuild Completed" },
	{ 0x0008, "P Rebuild Cancelled" },
	{ 0x0009, "P Rebuild Failed for Unknown Reasons" },
	{ 0x000A, "P Rebuild Failed due to New Physical Device" },
	{ 0x000B, "P Rebuild Failed due to Logical Drive Failure" },
	{ 0x000C, "S Offline" },
	{ 0x000D, "P Found" },
	{ 0x000E, "P Removed" },
	{ 0x000F, "P Unconfigured" },
	{ 0x0010, "P Expand Capacity Started" },
	{ 0x0011, "P Expand Capacity Completed" },
	{ 0x0012, "P Expand Capacity Failed" },
	{ 0x0013, "P Command Timed Out" },
	{ 0x0014, "P Command Aborted" },
	{ 0x0015, "P Command Retried" },
	{ 0x0016, "P Parity Error" },
	{ 0x0017, "P Soft Error" },
	{ 0x0018, "P Miscellaneous Error" },
	{ 0x0019, "P Reset" },
	{ 0x001A, "P Active Spare Found" },
	{ 0x001B, "P Warm Spare Found" },
	{ 0x001C, "S Sense Data Received" },
	{ 0x001D, "P Initialization Started" },
	{ 0x001E, "P Initialization Completed" },
	{ 0x001F, "P Initialization Failed" },
	{ 0x0020, "P Initialization Cancelled" },
	{ 0x0021, "P Failed because Write Recovery Failed" },
	{ 0x0022, "P Failed because SCSI Bus Reset Failed" },
	{ 0x0023, "P Failed because of Double Check Condition" },
	{ 0x0024, "P Failed because Device Cannot Be Accessed" },
	{ 0x0025, "P Failed because of Gross Error on SCSI Processor" },
	{ 0x0026, "P Failed because of Bad Tag from Device" },
	{ 0x0027, "P Failed because of Command Timeout" },
	{ 0x0028, "P Failed because of System Reset" },
	{ 0x0029, "P Failed because of Busy Status or Parity Error" },
	{ 0x002A, "P Failed because Host Set Device to Failed State" },
	{ 0x002B, "P Failed because of Selection Timeout" },
	{ 0x002C, "P Failed because of SCSI Bus Phase Error" },
	{ 0x002D, "P Failed because Device Returned Unknown Status" },
	{ 0x002E, "P Failed because Device Not Ready" },
	{ 0x002F, "P Failed because Device Not Found at Startup" },
	{ 0x0030, "P Failed because COD Write Operation Failed" },
	{ 0x0031, "P Failed because BDT Write Operation Failed" },
	{ 0x0039, "P Missing at Startup" },
	{ 0x003A, "P Start Rebuild Failed due to Physical Drive Too Small" },
	{ 0x003C, "P Temporarily Offline Device Automatically Made Online" },
	{ 0x003D, "P Standby Rebuild Started" },
	/* Logical Device Events (0x0080 - 0x00FF) */
	{ 0x0080, "M Consistency Check Started" },
	{ 0x0081, "M Consistency Check Completed" },
	{ 0x0082, "M Consistency Check Cancelled" },
	{ 0x0083, "M Consistency Check Completed With Errors" },
	{ 0x0084, "M Consistency Check Failed due to Logical Drive Failure" },
	{ 0x0085, "M Consistency Check Failed due to Physical Device Failure" },
	{ 0x0086, "L Offline" },
	{ 0x0087, "L Critical" },
	{ 0x0088, "L Online" },
	{ 0x0089, "M Automatic Rebuild Started" },
	{ 0x008A, "M Manual Rebuild Started" },
	{ 0x008B, "M Rebuild Completed" },
	{ 0x008C, "M Rebuild Cancelled" },
	{ 0x008D, "M Rebuild Failed for Unknown Reasons" },
	{ 0x008E, "M Rebuild Failed due to New Physical Device" },
	{ 0x008F, "M Rebuild Failed due to Logical Drive Failure" },
	{ 0x0090, "M Initialization Started" },
	{ 0x0091, "M Initialization Completed" },
	{ 0x0092, "M Initialization Cancelled" },
	{ 0x0093, "M Initialization Failed" },
	{ 0x0094, "L Found" },
	{ 0x0095, "L Deleted" },
	{ 0x0096, "M Expand Capacity Started" },
	{ 0x0097, "M Expand Capacity Completed" },
	{ 0x0098, "M Expand Capacity Failed" },
	{ 0x0099, "L Bad Block Found" },
	{ 0x009A, "L Size Changed" },
	{ 0x009B, "L Type Changed" },
	{ 0x009C, "L Bad Data Block Found" },
	{ 0x009E, "L Read of Data Block in BDT" },
	{ 0x009F, "L Write Back Data for Disk Block Lost" },
	{ 0x00A0, "L Temporarily Offline RAID-5/3 Drive Made Online" },
	{ 0x00A1, "L Temporarily Offline RAID-6/1/0/7 Drive Made Online" },
	{ 0x00A2, "L Standby Rebuild Started" },
	/* Fault Management Events (0x0100 - 0x017F) */
	{ 0x0140, "E Fan %d Failed" },
	{ 0x0141, "E Fan %d OK" },
	{ 0x0142, "E Fan %d Not Present" },
	{ 0x0143, "E Power Supply %d Failed" },
	{ 0x0144, "E Power Supply %d OK" },
	{ 0x0145, "E Power Supply %d Not Present" },
	{ 0x0146, "E Temperature Sensor %d Temperature Exceeds Safe Limit" },
	{ 0x0147, "E Temperature Sensor %d Temperature Exceeds Working Limit" },
	{ 0x0148, "E Temperature Sensor %d Temperature Normal" },
	{ 0x0149, "E Temperature Sensor %d Not Present" },
	{ 0x014A, "E Enclosure Management Unit %d Access Critical" },
	{ 0x014B, "E Enclosure Management Unit %d Access OK" },
	{ 0x014C, "E Enclosure Management Unit %d Access Offline" },
	/* Controller Events (0x0180 - 0x01FF) */
	{ 0x0181, "C Cache Write Back Error" },
	{ 0x0188, "C Battery Backup Unit Found" },
	{ 0x0189, "C Battery Backup Unit Charge Level Low" },
	{ 0x018A, "C Battery Backup Unit Charge Level OK" },
	{ 0x0193, "C Installation Aborted" },
	{ 0x0195, "C Battery Backup Unit Physically Removed" },
	{ 0x0196, "C Memory Error During Warm Boot" },
	{ 0x019E, "C Memory Soft ECC Error Corrected" },
	{ 0x019F, "C Memory Hard ECC Error Corrected" },
	{ 0x01A2, "C Battery Backup Unit Failed" },
	{ 0x01AB, "C Mirror Race Recovery Failed" },
	{ 0x01AC, "C Mirror Race on Critical Drive" },
	/* Controller Internal Processor Events */
	{ 0x0380, "C Internal Controller Hung" },
	{ 0x0381, "C Internal Controller Firmware Breakpoint" },
	{ 0x0390, "C Internal Controller i960 Processor Specific Error" },
	{ 0x03A0, "C Internal Controller StrongARM Processor Specific Error" },
	{ 0, "" }
};

static void myrs_log_event(struct myrs_hba *cs, struct myrs_event *ev)
{
	unsigned char msg_buf[MYRS_LINE_BUFFER_SIZE];
	int ev_idx = 0, ev_code;
	unsigned char ev_type, *ev_msg;
	struct Scsi_Host *shost = cs->host;
	struct scsi_device *sdev;
	struct scsi_sense_hdr sshdr = {0};
	unsigned char sense_info[4];
	unsigned char cmd_specific[4];

	if (ev->ev_code == 0x1C) {
		if (!scsi_normalize_sense(ev->sense_data, 40, &sshdr)) {
			memset(&sshdr, 0x0, sizeof(sshdr));
			memset(sense_info, 0x0, sizeof(sense_info));
			memset(cmd_specific, 0x0, sizeof(cmd_specific));
		} else {
			memcpy(sense_info, &ev->sense_data[3], 4);
			memcpy(cmd_specific, &ev->sense_data[7], 4);
		}
	}
	if (sshdr.sense_key == VENDOR_SPECIFIC &&
	    (sshdr.asc == 0x80 || sshdr.asc == 0x81))
		ev->ev_code = ((sshdr.asc - 0x80) << 8 | sshdr.ascq);
	while (true) {
		ev_code = myrs_ev_list[ev_idx].ev_code;
		if (ev_code == ev->ev_code || ev_code == 0)
			break;
		ev_idx++;
	}
	ev_type = myrs_ev_list[ev_idx].ev_msg[0];
	ev_msg = &myrs_ev_list[ev_idx].ev_msg[2];
	if (ev_code == 0) {
		shost_printk(KERN_WARNING, shost,
			     "Unknown Controller Event Code %04X\n",
			     ev->ev_code);
		return;
	}
	switch (ev_type) {
	case 'P':
		sdev = scsi_device_lookup(shost, ev->channel,
					  ev->target, 0);
		sdev_printk(KERN_INFO, sdev, "event %d: Physical Device %s\n",
			    ev->ev_seq, ev_msg);
		if (sdev && sdev->hostdata &&
		    sdev->channel < cs->ctlr_info->physchan_present) {
			struct myrs_pdev_info *pdev_info = sdev->hostdata;

			switch (ev->ev_code) {
			case 0x0001:
			case 0x0007:
				pdev_info->dev_state = MYRS_DEVICE_ONLINE;
				break;
			case 0x0002:
				pdev_info->dev_state = MYRS_DEVICE_STANDBY;
				break;
			case 0x000C:
				pdev_info->dev_state = MYRS_DEVICE_OFFLINE;
				break;
			case 0x000E:
				pdev_info->dev_state = MYRS_DEVICE_MISSING;
				break;
			case 0x000F:
				pdev_info->dev_state = MYRS_DEVICE_UNCONFIGURED;
				break;
			}
		}
		break;
	case 'L':
		shost_printk(KERN_INFO, shost,
			     "event %d: Logical Drive %d %s\n",
			     ev->ev_seq, ev->lun, ev_msg);
		cs->needs_update = true;
		break;
	case 'M':
		shost_printk(KERN_INFO, shost,
			     "event %d: Logical Drive %d %s\n",
			     ev->ev_seq, ev->lun, ev_msg);
		cs->needs_update = true;
		break;
	case 'S':
		if (sshdr.sense_key == NO_SENSE ||
		    (sshdr.sense_key == NOT_READY &&
		     sshdr.asc == 0x04 && (sshdr.ascq == 0x01 ||
					    sshdr.ascq == 0x02)))
			break;
		shost_printk(KERN_INFO, shost,
			     "event %d: Physical Device %d:%d %s\n",
			     ev->ev_seq, ev->channel, ev->target, ev_msg);
		shost_printk(KERN_INFO, shost,
			     "Physical Device %d:%d Sense Key = %X, ASC = %02X, ASCQ = %02X\n",
			     ev->channel, ev->target,
			     sshdr.sense_key, sshdr.asc, sshdr.ascq);
		shost_printk(KERN_INFO, shost,
			     "Physical Device %d:%d Sense Information = %02X%02X%02X%02X %02X%02X%02X%02X\n",
			     ev->channel, ev->target,
			     sense_info[0], sense_info[1],
			     sense_info[2], sense_info[3],
			     cmd_specific[0], cmd_specific[1],
			     cmd_specific[2], cmd_specific[3]);
		break;
	case 'E':
		if (cs->disable_enc_msg)
			break;
		sprintf(msg_buf, ev_msg, ev->lun);
		shost_printk(KERN_INFO, shost, "event %d: Enclosure %d %s\n",
			     ev->ev_seq, ev->target, msg_buf);
		break;
	case 'C':
		shost_printk(KERN_INFO, shost, "event %d: Controller %s\n",
			     ev->ev_seq, ev_msg);
		break;
	default:
		shost_printk(KERN_INFO, shost,
			     "event %d: Unknown Event Code %04X\n",
			     ev->ev_seq, ev->ev_code);
		break;
	}
}

/*
 * SCSI sysfs interface functions
 */
static ssize_t raid_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	int ret;

	if (!sdev->hostdata)
		return snprintf(buf, 16, "Unknown\n");

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		struct myrs_ldev_info *ldev_info = sdev->hostdata;
		const char *name;

		name = myrs_devstate_name(ldev_info->dev_state);
		if (name)
			ret = snprintf(buf, 64, "%s\n", name);
		else
			ret = snprintf(buf, 64, "Invalid (%02X)\n",
				       ldev_info->dev_state);
	} else {
		struct myrs_pdev_info *pdev_info;
		const char *name;

		pdev_info = sdev->hostdata;
		name = myrs_devstate_name(pdev_info->dev_state);
		if (name)
			ret = snprintf(buf, 64, "%s\n", name);
		else
			ret = snprintf(buf, 64, "Invalid (%02X)\n",
				       pdev_info->dev_state);
	}
	return ret;
}

static ssize_t raid_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_cmdblk *cmd_blk;
	union myrs_cmd_mbox *mbox;
	enum myrs_devstate new_state;
	unsigned short ldev_num;
	unsigned char status;

	if (!strncmp(buf, "offline", 7) ||
	    !strncmp(buf, "kill", 4))
		new_state = MYRS_DEVICE_OFFLINE;
	else if (!strncmp(buf, "online", 6))
		new_state = MYRS_DEVICE_ONLINE;
	else if (!strncmp(buf, "standby", 7))
		new_state = MYRS_DEVICE_STANDBY;
	else
		return -EINVAL;

	if (sdev->channel < cs->ctlr_info->physchan_present) {
		struct myrs_pdev_info *pdev_info = sdev->hostdata;
		struct myrs_devmap *pdev_devmap =
			(struct myrs_devmap *)&pdev_info->rsvd13;

		if (pdev_info->dev_state == new_state) {
			sdev_printk(KERN_INFO, sdev,
				    "Device already in %s\n",
				    myrs_devstate_name(new_state));
			return count;
		}
		status = myrs_translate_pdev(cs, sdev->channel, sdev->id,
					     sdev->lun, pdev_devmap);
		if (status != MYRS_STATUS_SUCCESS)
			return -ENXIO;
		ldev_num = pdev_devmap->ldev_num;
	} else {
		struct myrs_ldev_info *ldev_info = sdev->hostdata;

		if (ldev_info->dev_state == new_state) {
			sdev_printk(KERN_INFO, sdev,
				    "Device already in %s\n",
				    myrs_devstate_name(new_state));
			return count;
		}
		ldev_num = ldev_info->ldev_num;
	}
	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->common.opcode = MYRS_CMD_OP_IOCTL;
	mbox->common.id = MYRS_DCMD_TAG;
	mbox->common.control.dma_ctrl_to_host = true;
	mbox->common.control.no_autosense = true;
	mbox->set_devstate.ioctl_opcode = MYRS_IOCTL_SET_DEVICE_STATE;
	mbox->set_devstate.state = new_state;
	mbox->set_devstate.ldev.ldev_num = ldev_num;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status == MYRS_STATUS_SUCCESS) {
		if (sdev->channel < cs->ctlr_info->physchan_present) {
			struct myrs_pdev_info *pdev_info = sdev->hostdata;

			pdev_info->dev_state = new_state;
		} else {
			struct myrs_ldev_info *ldev_info = sdev->hostdata;

			ldev_info->dev_state = new_state;
		}
		sdev_printk(KERN_INFO, sdev,
			    "Set device state to %s\n",
			    myrs_devstate_name(new_state));
		return count;
	}
	sdev_printk(KERN_INFO, sdev,
		    "Failed to set device state to %s, status 0x%02x\n",
		    myrs_devstate_name(new_state), status);
	return -EINVAL;
}
static DEVICE_ATTR_RW(raid_state);

static ssize_t raid_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	const char *name = NULL;

	if (!sdev->hostdata)
		return snprintf(buf, 16, "Unknown\n");

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		struct myrs_ldev_info *ldev_info;

		ldev_info = sdev->hostdata;
		name = myrs_raid_level_name(ldev_info->raid_level);
		if (!name)
			return snprintf(buf, 64, "Invalid (%02X)\n",
					ldev_info->dev_state);

	} else
		name = myrs_raid_level_name(MYRS_RAID_PHYSICAL);

	return snprintf(buf, 64, "%s\n", name);
}
static DEVICE_ATTR_RO(raid_level);

static ssize_t rebuild_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info;
	unsigned short ldev_num;
	unsigned char status;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return snprintf(buf, 64, "physical device - not rebuilding\n");

	ldev_info = sdev->hostdata;
	ldev_num = ldev_info->ldev_num;
	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != MYRS_STATUS_SUCCESS) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}
	if (ldev_info->rbld_active) {
		return snprintf(buf, 64, "rebuilding block %zu of %zu\n",
				(size_t)ldev_info->rbld_lba,
				(size_t)ldev_info->cfg_devsize);
	} else
		return snprintf(buf, 64, "not rebuilding\n");
}

static ssize_t rebuild_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info;
	struct myrs_cmdblk *cmd_blk;
	union myrs_cmd_mbox *mbox;
	unsigned short ldev_num;
	unsigned char status;
	int rebuild, ret;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return -EINVAL;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;

	ret = kstrtoint(buf, 0, &rebuild);
	if (ret)
		return ret;

	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != MYRS_STATUS_SUCCESS) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}

	if (rebuild && ldev_info->rbld_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not Initiated; already in progress\n");
		return -EALREADY;
	}
	if (!rebuild && !ldev_info->rbld_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not Cancelled; no rebuild in progress\n");
		return count;
	}

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->common.opcode = MYRS_CMD_OP_IOCTL;
	mbox->common.id = MYRS_DCMD_TAG;
	mbox->common.control.dma_ctrl_to_host = true;
	mbox->common.control.no_autosense = true;
	if (rebuild) {
		mbox->ldev_info.ldev.ldev_num = ldev_num;
		mbox->ldev_info.ioctl_opcode = MYRS_IOCTL_RBLD_DEVICE_START;
	} else {
		mbox->ldev_info.ldev.ldev_num = ldev_num;
		mbox->ldev_info.ioctl_opcode = MYRS_IOCTL_RBLD_DEVICE_STOP;
	}
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not %s, status 0x%02x\n",
			    rebuild ? "Initiated" : "Cancelled", status);
		ret = -EIO;
	} else {
		sdev_printk(KERN_INFO, sdev, "Rebuild %s\n",
			    rebuild ? "Initiated" : "Cancelled");
		ret = count;
	}

	return ret;
}
static DEVICE_ATTR_RW(rebuild);

static ssize_t consistency_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info;
	unsigned short ldev_num;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return snprintf(buf, 64, "physical device - not checking\n");

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;
	myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (ldev_info->cc_active)
		return snprintf(buf, 64, "checking block %zu of %zu\n",
				(size_t)ldev_info->cc_lba,
				(size_t)ldev_info->cfg_devsize);
	else
		return snprintf(buf, 64, "not checking\n");
}

static ssize_t consistency_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info;
	struct myrs_cmdblk *cmd_blk;
	union myrs_cmd_mbox *mbox;
	unsigned short ldev_num;
	unsigned char status;
	int check, ret;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return -EINVAL;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;

	ret = kstrtoint(buf, 0, &check);
	if (ret)
		return ret;

	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != MYRS_STATUS_SUCCESS) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}
	if (check && ldev_info->cc_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not Initiated; "
			    "already in progress\n");
		return -EALREADY;
	}
	if (!check && !ldev_info->cc_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not Cancelled; "
			    "check not in progress\n");
		return count;
	}

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->common.opcode = MYRS_CMD_OP_IOCTL;
	mbox->common.id = MYRS_DCMD_TAG;
	mbox->common.control.dma_ctrl_to_host = true;
	mbox->common.control.no_autosense = true;
	if (check) {
		mbox->cc.ldev.ldev_num = ldev_num;
		mbox->cc.ioctl_opcode = MYRS_IOCTL_CC_START;
		mbox->cc.restore_consistency = true;
		mbox->cc.initialized_area_only = false;
	} else {
		mbox->cc.ldev.ldev_num = ldev_num;
		mbox->cc.ioctl_opcode = MYRS_IOCTL_CC_STOP;
	}
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status != MYRS_STATUS_SUCCESS) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not %s, status 0x%02x\n",
			    check ? "Initiated" : "Cancelled", status);
		ret = -EIO;
	} else {
		sdev_printk(KERN_INFO, sdev, "Consistency Check %s\n",
			    check ? "Initiated" : "Cancelled");
		ret = count;
	}

	return ret;
}
static DEVICE_ATTR_RW(consistency_check);

static struct attribute *myrs_sdev_attrs[] = {
	&dev_attr_consistency_check.attr,
	&dev_attr_rebuild.attr,
	&dev_attr_raid_state.attr,
	&dev_attr_raid_level.attr,
	NULL,
};

ATTRIBUTE_GROUPS(myrs_sdev);

static ssize_t serial_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);
	char serial[17];

	memcpy(serial, cs->ctlr_info->serial_number, 16);
	serial[16] = '\0';
	return snprintf(buf, 16, "%s\n", serial);
}
static DEVICE_ATTR_RO(serial);

static ssize_t ctlr_num_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 20, "%d\n", cs->host->host_no);
}
static DEVICE_ATTR_RO(ctlr_num);

static struct myrs_cpu_type_tbl {
	enum myrs_cpu_type type;
	char *name;
} myrs_cpu_type_names[] = {
	{ MYRS_CPUTYPE_i960CA, "i960CA" },
	{ MYRS_CPUTYPE_i960RD, "i960RD" },
	{ MYRS_CPUTYPE_i960RN, "i960RN" },
	{ MYRS_CPUTYPE_i960RP, "i960RP" },
	{ MYRS_CPUTYPE_NorthBay, "NorthBay" },
	{ MYRS_CPUTYPE_StrongArm, "StrongARM" },
	{ MYRS_CPUTYPE_i960RM, "i960RM" },
};

static ssize_t processor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);
	struct myrs_cpu_type_tbl *tbl;
	const char *first_processor = NULL;
	const char *second_processor = NULL;
	struct myrs_ctlr_info *info = cs->ctlr_info;
	ssize_t ret;
	int i;

	if (info->cpu[0].cpu_count) {
		tbl = myrs_cpu_type_names;
		for (i = 0; i < ARRAY_SIZE(myrs_cpu_type_names); i++) {
			if (tbl[i].type == info->cpu[0].cpu_type) {
				first_processor = tbl[i].name;
				break;
			}
		}
	}
	if (info->cpu[1].cpu_count) {
		tbl = myrs_cpu_type_names;
		for (i = 0; i < ARRAY_SIZE(myrs_cpu_type_names); i++) {
			if (tbl[i].type == info->cpu[1].cpu_type) {
				second_processor = tbl[i].name;
				break;
			}
		}
	}
	if (first_processor && second_processor)
		ret = snprintf(buf, 64, "1: %s (%s, %d cpus)\n"
			       "2: %s (%s, %d cpus)\n",
			       info->cpu[0].cpu_name,
			       first_processor, info->cpu[0].cpu_count,
			       info->cpu[1].cpu_name,
			       second_processor, info->cpu[1].cpu_count);
	else if (first_processor && !second_processor)
		ret = snprintf(buf, 64, "1: %s (%s, %d cpus)\n2: absent\n",
			       info->cpu[0].cpu_name,
			       first_processor, info->cpu[0].cpu_count);
	else if (!first_processor && second_processor)
		ret = snprintf(buf, 64, "1: absent\n2: %s (%s, %d cpus)\n",
			       info->cpu[1].cpu_name,
			       second_processor, info->cpu[1].cpu_count);
	else
		ret = snprintf(buf, 64, "1: absent\n2: absent\n");

	return ret;
}
static DEVICE_ATTR_RO(processor);

static ssize_t model_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 28, "%s\n", cs->model_name);
}
static DEVICE_ATTR_RO(model);

static ssize_t ctlr_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 4, "%d\n", cs->ctlr_info->ctlr_type);
}
static DEVICE_ATTR_RO(ctlr_type);

static ssize_t cache_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 8, "%d MB\n", cs->ctlr_info->cache_size_mb);
}
static DEVICE_ATTR_RO(cache_size);

static ssize_t firmware_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 16, "%d.%02d-%02d\n",
			cs->ctlr_info->fw_major_version,
			cs->ctlr_info->fw_minor_version,
			cs->ctlr_info->fw_turn_number);
}
static DEVICE_ATTR_RO(firmware);

static ssize_t discovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);
	struct myrs_cmdblk *cmd_blk;
	union myrs_cmd_mbox *mbox;
	unsigned char status;

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->common.opcode = MYRS_CMD_OP_IOCTL;
	mbox->common.id = MYRS_DCMD_TAG;
	mbox->common.control.dma_ctrl_to_host = true;
	mbox->common.control.no_autosense = true;
	mbox->common.ioctl_opcode = MYRS_IOCTL_START_DISCOVERY;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status != MYRS_STATUS_SUCCESS) {
		shost_printk(KERN_INFO, shost,
			     "Discovery Not Initiated, status %02X\n",
			     status);
		return -EINVAL;
	}
	shost_printk(KERN_INFO, shost, "Discovery Initiated\n");
	cs->next_evseq = 0;
	cs->needs_update = true;
	queue_delayed_work(cs->work_q, &cs->monitor_work, 1);
	flush_delayed_work(&cs->monitor_work);
	shost_printk(KERN_INFO, shost, "Discovery Completed\n");

	return count;
}
static DEVICE_ATTR_WO(discovery);

static ssize_t flush_cache_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);
	unsigned char status;

	status = myrs_dev_op(cs, MYRS_IOCTL_FLUSH_DEVICE_DATA,
			     MYRS_RAID_CONTROLLER);
	if (status == MYRS_STATUS_SUCCESS) {
		shost_printk(KERN_INFO, shost, "Cache Flush Completed\n");
		return count;
	}
	shost_printk(KERN_INFO, shost,
		     "Cache Flush failed, status 0x%02x\n", status);
	return -EIO;
}
static DEVICE_ATTR_WO(flush_cache);

static ssize_t disable_enclosure_messages_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct myrs_hba *cs = shost_priv(shost);

	return snprintf(buf, 3, "%d\n", cs->disable_enc_msg);
}

static ssize_t disable_enclosure_messages_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	int value, ret;

	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return ret;

	if (value > 2)
		return -EINVAL;

	cs->disable_enc_msg = value;
	return count;
}
static DEVICE_ATTR_RW(disable_enclosure_messages);

static struct attribute *myrs_shost_attrs[] = {
	&dev_attr_serial.attr,
	&dev_attr_ctlr_num.attr,
	&dev_attr_processor.attr,
	&dev_attr_model.attr,
	&dev_attr_ctlr_type.attr,
	&dev_attr_cache_size.attr,
	&dev_attr_firmware.attr,
	&dev_attr_discovery.attr,
	&dev_attr_flush_cache.attr,
	&dev_attr_disable_enclosure_messages.attr,
	NULL,
};

ATTRIBUTE_GROUPS(myrs_shost);

/*
 * SCSI midlayer interface
 */
static int myrs_host_reset(struct scsi_cmnd *scmd)
{
	struct Scsi_Host *shost = scmd->device->host;
	struct myrs_hba *cs = shost_priv(shost);

	cs->reset(cs->io_base);
	return SUCCESS;
}

static void myrs_mode_sense(struct myrs_hba *cs, struct scsi_cmnd *scmd,
		struct myrs_ldev_info *ldev_info)
{
	unsigned char modes[32], *mode_pg;
	bool dbd;
	size_t mode_len;

	dbd = (scmd->cmnd[1] & 0x08) == 0x08;
	if (dbd) {
		mode_len = 24;
		mode_pg = &modes[4];
	} else {
		mode_len = 32;
		mode_pg = &modes[12];
	}
	memset(modes, 0, sizeof(modes));
	modes[0] = mode_len - 1;
	modes[2] = 0x10; /* Enable FUA */
	if (ldev_info->ldev_control.wce == MYRS_LOGICALDEVICE_RO)
		modes[2] |= 0x80;
	if (!dbd) {
		unsigned char *block_desc = &modes[4];

		modes[3] = 8;
		put_unaligned_be32(ldev_info->cfg_devsize, &block_desc[0]);
		put_unaligned_be32(ldev_info->devsize_bytes, &block_desc[5]);
	}
	mode_pg[0] = 0x08;
	mode_pg[1] = 0x12;
	if (ldev_info->ldev_control.rce == MYRS_READCACHE_DISABLED)
		mode_pg[2] |= 0x01;
	if (ldev_info->ldev_control.wce == MYRS_WRITECACHE_ENABLED ||
	    ldev_info->ldev_control.wce == MYRS_INTELLIGENT_WRITECACHE_ENABLED)
		mode_pg[2] |= 0x04;
	if (ldev_info->cacheline_size) {
		mode_pg[2] |= 0x08;
		put_unaligned_be16(1 << ldev_info->cacheline_size,
				   &mode_pg[14]);
	}

	scsi_sg_copy_from_buffer(scmd, modes, mode_len);
}

static int myrs_queuecommand(struct Scsi_Host *shost,
		struct scsi_cmnd *scmd)
{
	struct request *rq = scsi_cmd_to_rq(scmd);
	struct myrs_hba *cs = shost_priv(shost);
	struct myrs_cmdblk *cmd_blk = scsi_cmd_priv(scmd);
	union myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	struct scsi_device *sdev = scmd->device;
	union myrs_sgl *hw_sge;
	dma_addr_t sense_addr;
	struct scatterlist *sgl;
	unsigned long flags, timeout;
	int nsge;

	if (!scmd->device->hostdata) {
		scmd->result = (DID_NO_CONNECT << 16);
		scsi_done(scmd);
		return 0;
	}

	switch (scmd->cmnd[0]) {
	case REPORT_LUNS:
		scsi_build_sense(scmd, 0, ILLEGAL_REQUEST, 0x20, 0x0);
		scsi_done(scmd);
		return 0;
	case MODE_SENSE:
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			struct myrs_ldev_info *ldev_info = sdev->hostdata;

			if ((scmd->cmnd[2] & 0x3F) != 0x3F &&
			    (scmd->cmnd[2] & 0x3F) != 0x08) {
				/* Illegal request, invalid field in CDB */
				scsi_build_sense(scmd, 0, ILLEGAL_REQUEST, 0x24, 0);
			} else {
				myrs_mode_sense(cs, scmd, ldev_info);
				scmd->result = (DID_OK << 16);
			}
			scsi_done(scmd);
			return 0;
		}
		break;
	}

	myrs_reset_cmd(cmd_blk);
	cmd_blk->sense = dma_pool_alloc(cs->sense_pool, GFP_ATOMIC,
					&sense_addr);
	if (!cmd_blk->sense)
		return SCSI_MLQUEUE_HOST_BUSY;
	cmd_blk->sense_addr = sense_addr;

	timeout = rq->timeout;
	if (scmd->cmd_len <= 10) {
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			struct myrs_ldev_info *ldev_info = sdev->hostdata;

			mbox->SCSI_10.opcode = MYRS_CMD_OP_SCSI_10;
			mbox->SCSI_10.pdev.lun = ldev_info->lun;
			mbox->SCSI_10.pdev.target = ldev_info->target;
			mbox->SCSI_10.pdev.channel = ldev_info->channel;
			mbox->SCSI_10.pdev.ctlr = 0;
		} else {
			mbox->SCSI_10.opcode = MYRS_CMD_OP_SCSI_10_PASSTHRU;
			mbox->SCSI_10.pdev.lun = sdev->lun;
			mbox->SCSI_10.pdev.target = sdev->id;
			mbox->SCSI_10.pdev.channel = sdev->channel;
		}
		mbox->SCSI_10.id = rq->tag + 3;
		mbox->SCSI_10.control.dma_ctrl_to_host =
			(scmd->sc_data_direction == DMA_FROM_DEVICE);
		if (rq->cmd_flags & REQ_FUA)
			mbox->SCSI_10.control.fua = true;
		mbox->SCSI_10.dma_size = scsi_bufflen(scmd);
		mbox->SCSI_10.sense_addr = cmd_blk->sense_addr;
		mbox->SCSI_10.sense_len = MYRS_SENSE_SIZE;
		mbox->SCSI_10.cdb_len = scmd->cmd_len;
		if (timeout > 60) {
			mbox->SCSI_10.tmo.tmo_scale = MYRS_TMO_SCALE_MINUTES;
			mbox->SCSI_10.tmo.tmo_val = timeout / 60;
		} else {
			mbox->SCSI_10.tmo.tmo_scale = MYRS_TMO_SCALE_SECONDS;
			mbox->SCSI_10.tmo.tmo_val = timeout;
		}
		memcpy(&mbox->SCSI_10.cdb, scmd->cmnd, scmd->cmd_len);
		hw_sge = &mbox->SCSI_10.dma_addr;
		cmd_blk->dcdb = NULL;
	} else {
		dma_addr_t dcdb_dma;

		cmd_blk->dcdb = dma_pool_alloc(cs->dcdb_pool, GFP_ATOMIC,
					       &dcdb_dma);
		if (!cmd_blk->dcdb) {
			dma_pool_free(cs->sense_pool, cmd_blk->sense,
				      cmd_blk->sense_addr);
			cmd_blk->sense = NULL;
			cmd_blk->sense_addr = 0;
			return SCSI_MLQUEUE_HOST_BUSY;
		}
		cmd_blk->dcdb_dma = dcdb_dma;
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			struct myrs_ldev_info *ldev_info = sdev->hostdata;

			mbox->SCSI_255.opcode = MYRS_CMD_OP_SCSI_256;
			mbox->SCSI_255.pdev.lun = ldev_info->lun;
			mbox->SCSI_255.pdev.target = ldev_info->target;
			mbox->SCSI_255.pdev.channel = ldev_info->channel;
			mbox->SCSI_255.pdev.ctlr = 0;
		} else {
			mbox->SCSI_255.opcode = MYRS_CMD_OP_SCSI_255_PASSTHRU;
			mbox->SCSI_255.pdev.lun = sdev->lun;
			mbox->SCSI_255.pdev.target = sdev->id;
			mbox->SCSI_255.pdev.channel = sdev->channel;
		}
		mbox->SCSI_255.id = rq->tag + 3;
		mbox->SCSI_255.control.dma_ctrl_to_host =
			(scmd->sc_data_direction == DMA_FROM_DEVICE);
		if (rq->cmd_flags & REQ_FUA)
			mbox->SCSI_255.control.fua = true;
		mbox->SCSI_255.dma_size = scsi_bufflen(scmd);
		mbox->SCSI_255.sense_addr = cmd_blk->sense_addr;
		mbox->SCSI_255.sense_len = MYRS_SENSE_SIZE;
		mbox->SCSI_255.cdb_len = scmd->cmd_len;
		mbox->SCSI_255.cdb_addr = cmd_blk->dcdb_dma;
		if (timeout > 60) {
			mbox->SCSI_255.tmo.tmo_scale = MYRS_TMO_SCALE_MINUTES;
			mbox->SCSI_255.tmo.tmo_val = timeout / 60;
		} else {
			mbox->SCSI_255.tmo.tmo_scale = MYRS_TMO_SCALE_SECONDS;
			mbox->SCSI_255.tmo.tmo_val = timeout;
		}
		memcpy(cmd_blk->dcdb, scmd->cmnd, scmd->cmd_len);
		hw_sge = &mbox->SCSI_255.dma_addr;
	}
	if (scmd->sc_data_direction == DMA_NONE)
		goto submit;
	nsge = scsi_dma_map(scmd);
	if (nsge == 1) {
		sgl = scsi_sglist(scmd);
		hw_sge->sge[0].sge_addr = (u64)sg_dma_address(sgl);
		hw_sge->sge[0].sge_count = (u64)sg_dma_len(sgl);
	} else {
		struct myrs_sge *hw_sgl;
		dma_addr_t hw_sgl_addr;
		int i;

		if (nsge > 2) {
			hw_sgl = dma_pool_alloc(cs->sg_pool, GFP_ATOMIC,
						&hw_sgl_addr);
			if (WARN_ON(!hw_sgl)) {
				if (cmd_blk->dcdb) {
					dma_pool_free(cs->dcdb_pool,
						      cmd_blk->dcdb,
						      cmd_blk->dcdb_dma);
					cmd_blk->dcdb = NULL;
					cmd_blk->dcdb_dma = 0;
				}
				dma_pool_free(cs->sense_pool,
					      cmd_blk->sense,
					      cmd_blk->sense_addr);
				cmd_blk->sense = NULL;
				cmd_blk->sense_addr = 0;
				return SCSI_MLQUEUE_HOST_BUSY;
			}
			cmd_blk->sgl = hw_sgl;
			cmd_blk->sgl_addr = hw_sgl_addr;
			if (scmd->cmd_len <= 10)
				mbox->SCSI_10.control.add_sge_mem = true;
			else
				mbox->SCSI_255.control.add_sge_mem = true;
			hw_sge->ext.sge0_len = nsge;
			hw_sge->ext.sge0_addr = cmd_blk->sgl_addr;
		} else
			hw_sgl = hw_sge->sge;

		scsi_for_each_sg(scmd, sgl, nsge, i) {
			if (WARN_ON(!hw_sgl)) {
				scsi_dma_unmap(scmd);
				scmd->result = (DID_ERROR << 16);
				scsi_done(scmd);
				return 0;
			}
			hw_sgl->sge_addr = (u64)sg_dma_address(sgl);
			hw_sgl->sge_count = (u64)sg_dma_len(sgl);
			hw_sgl++;
		}
	}
submit:
	spin_lock_irqsave(&cs->queue_lock, flags);
	myrs_qcmd(cs, cmd_blk);
	spin_unlock_irqrestore(&cs->queue_lock, flags);

	return 0;
}

static unsigned short myrs_translate_ldev(struct myrs_hba *cs,
		struct scsi_device *sdev)
{
	unsigned short ldev_num;
	unsigned int chan_offset =
		sdev->channel - cs->ctlr_info->physchan_present;

	ldev_num = sdev->id + chan_offset * sdev->host->max_id;

	return ldev_num;
}

static int myrs_sdev_init(struct scsi_device *sdev)
{
	struct myrs_hba *cs = shost_priv(sdev->host);
	unsigned char status;

	if (sdev->channel > sdev->host->max_channel)
		return 0;

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		struct myrs_ldev_info *ldev_info;
		unsigned short ldev_num;

		if (sdev->lun > 0)
			return -ENXIO;

		ldev_num = myrs_translate_ldev(cs, sdev);

		ldev_info = kzalloc(sizeof(*ldev_info), GFP_KERNEL);
		if (!ldev_info)
			return -ENOMEM;

		status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
		if (status != MYRS_STATUS_SUCCESS) {
			sdev->hostdata = NULL;
			kfree(ldev_info);
		} else {
			enum raid_level level;

			dev_dbg(&sdev->sdev_gendev,
				"Logical device mapping %d:%d:%d -> %d\n",
				ldev_info->channel, ldev_info->target,
				ldev_info->lun, ldev_info->ldev_num);

			sdev->hostdata = ldev_info;
			switch (ldev_info->raid_level) {
			case MYRS_RAID_LEVEL0:
				level = RAID_LEVEL_LINEAR;
				break;
			case MYRS_RAID_LEVEL1:
				level = RAID_LEVEL_1;
				break;
			case MYRS_RAID_LEVEL3:
			case MYRS_RAID_LEVEL3F:
			case MYRS_RAID_LEVEL3L:
				level = RAID_LEVEL_3;
				break;
			case MYRS_RAID_LEVEL5:
			case MYRS_RAID_LEVEL5L:
				level = RAID_LEVEL_5;
				break;
			case MYRS_RAID_LEVEL6:
				level = RAID_LEVEL_6;
				break;
			case MYRS_RAID_LEVELE:
			case MYRS_RAID_NEWSPAN:
			case MYRS_RAID_SPAN:
				level = RAID_LEVEL_LINEAR;
				break;
			case MYRS_RAID_JBOD:
				level = RAID_LEVEL_JBOD;
				break;
			default:
				level = RAID_LEVEL_UNKNOWN;
				break;
			}
			raid_set_level(myrs_raid_template,
				       &sdev->sdev_gendev, level);
			if (ldev_info->dev_state != MYRS_DEVICE_ONLINE) {
				const char *name;

				name = myrs_devstate_name(ldev_info->dev_state);
				sdev_printk(KERN_DEBUG, sdev,
					    "logical device in state %s\n",
					    name ? name : "Invalid");
			}
		}
	} else {
		struct myrs_pdev_info *pdev_info;

		pdev_info = kzalloc(sizeof(*pdev_info), GFP_KERNEL);
		if (!pdev_info)
			return -ENOMEM;

		status = myrs_get_pdev_info(cs, sdev->channel,
					    sdev->id, sdev->lun,
					    pdev_info);
		if (status != MYRS_STATUS_SUCCESS) {
			sdev->hostdata = NULL;
			kfree(pdev_info);
			return -ENXIO;
		}
		sdev->hostdata = pdev_info;
	}
	return 0;
}

static int myrs_sdev_configure(struct scsi_device *sdev,
			       struct queue_limits *lim)
{
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info;

	if (sdev->channel > sdev->host->max_channel)
		return -ENXIO;

	if (sdev->channel < cs->ctlr_info->physchan_present) {
		/* Skip HBA device */
		if (sdev->type == TYPE_RAID)
			return -ENXIO;
		sdev->no_uld_attach = 1;
		return 0;
	}
	if (sdev->lun != 0)
		return -ENXIO;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	if (ldev_info->ldev_control.wce == MYRS_WRITECACHE_ENABLED ||
	    ldev_info->ldev_control.wce == MYRS_INTELLIGENT_WRITECACHE_ENABLED)
		sdev->wce_default_on = 1;
	sdev->tagged_supported = 1;
	return 0;
}

static void myrs_sdev_destroy(struct scsi_device *sdev)
{
	kfree(sdev->hostdata);
}

static const struct scsi_host_template myrs_template = {
	.module			= THIS_MODULE,
	.name			= "DAC960",
	.proc_name		= "myrs",
	.queuecommand		= myrs_queuecommand,
	.eh_host_reset_handler	= myrs_host_reset,
	.sdev_init		= myrs_sdev_init,
	.sdev_configure		= myrs_sdev_configure,
	.sdev_destroy		= myrs_sdev_destroy,
	.cmd_size		= sizeof(struct myrs_cmdblk),
	.shost_groups		= myrs_shost_groups,
	.sdev_groups		= myrs_sdev_groups,
	.this_id		= -1,
};

static struct myrs_hba *myrs_alloc_host(struct pci_dev *pdev,
		const struct pci_device_id *entry)
{
	struct Scsi_Host *shost;
	struct myrs_hba *cs;

	shost = scsi_host_alloc(&myrs_template, sizeof(struct myrs_hba));
	if (!shost)
		return NULL;

	shost->max_cmd_len = 16;
	shost->max_lun = 256;
	cs = shost_priv(shost);
	mutex_init(&cs->dcmd_mutex);
	mutex_init(&cs->cinfo_mutex);
	cs->host = shost;

	return cs;
}

/*
 * RAID template functions
 */

/**
 * myrs_is_raid - return boolean indicating device is raid volume
 * @dev: the device struct object
 */
static int
myrs_is_raid(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);

	return (sdev->channel >= cs->ctlr_info->physchan_present) ? 1 : 0;
}

/**
 * myrs_get_resync - get raid volume resync percent complete
 * @dev: the device struct object
 */
static void
myrs_get_resync(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info = sdev->hostdata;
	u64 percent_complete = 0;

	if (sdev->channel < cs->ctlr_info->physchan_present || !ldev_info)
		return;
	if (ldev_info->rbld_active) {
		unsigned short ldev_num = ldev_info->ldev_num;

		myrs_get_ldev_info(cs, ldev_num, ldev_info);
		percent_complete = ldev_info->rbld_lba * 100;
		do_div(percent_complete, ldev_info->cfg_devsize);
	}
	raid_set_resync(myrs_raid_template, dev, percent_complete);
}

/**
 * myrs_get_state - get raid volume status
 * @dev: the device struct object
 */
static void
myrs_get_state(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct myrs_hba *cs = shost_priv(sdev->host);
	struct myrs_ldev_info *ldev_info = sdev->hostdata;
	enum raid_state state = RAID_STATE_UNKNOWN;

	if (sdev->channel < cs->ctlr_info->physchan_present || !ldev_info)
		state = RAID_STATE_UNKNOWN;
	else {
		switch (ldev_info->dev_state) {
		case MYRS_DEVICE_ONLINE:
			state = RAID_STATE_ACTIVE;
			break;
		case MYRS_DEVICE_SUSPECTED_CRITICAL:
		case MYRS_DEVICE_CRITICAL:
			state = RAID_STATE_DEGRADED;
			break;
		case MYRS_DEVICE_REBUILD:
			state = RAID_STATE_RESYNCING;
			break;
		case MYRS_DEVICE_UNCONFIGURED:
		case MYRS_DEVICE_INVALID_STATE:
			state = RAID_STATE_UNKNOWN;
			break;
		default:
			state = RAID_STATE_OFFLINE;
		}
	}
	raid_set_state(myrs_raid_template, dev, state);
}

static struct raid_function_template myrs_raid_functions = {
	.cookie		= &myrs_template,
	.is_raid	= myrs_is_raid,
	.get_resync	= myrs_get_resync,
	.get_state	= myrs_get_state,
};

/*
 * PCI interface functions
 */
static void myrs_flush_cache(struct myrs_hba *cs)
{
	myrs_dev_op(cs, MYRS_IOCTL_FLUSH_DEVICE_DATA, MYRS_RAID_CONTROLLER);
}

static void myrs_handle_scsi(struct myrs_hba *cs, struct myrs_cmdblk *cmd_blk,
		struct scsi_cmnd *scmd)
{
	unsigned char status;

	if (!cmd_blk)
		return;

	scsi_dma_unmap(scmd);
	status = cmd_blk->status;
	if (cmd_blk->sense) {
		if (status == MYRS_STATUS_FAILED && cmd_blk->sense_len) {
			unsigned int sense_len = SCSI_SENSE_BUFFERSIZE;

			if (sense_len > cmd_blk->sense_len)
				sense_len = cmd_blk->sense_len;
			memcpy(scmd->sense_buffer, cmd_blk->sense, sense_len);
		}
		dma_pool_free(cs->sense_pool, cmd_blk->sense,
			      cmd_blk->sense_addr);
		cmd_blk->sense = NULL;
		cmd_blk->sense_addr = 0;
	}
	if (cmd_blk->dcdb) {
		dma_pool_free(cs->dcdb_pool, cmd_blk->dcdb,
			      cmd_blk->dcdb_dma);
		cmd_blk->dcdb = NULL;
		cmd_blk->dcdb_dma = 0;
	}
	if (cmd_blk->sgl) {
		dma_pool_free(cs->sg_pool, cmd_blk->sgl,
			      cmd_blk->sgl_addr);
		cmd_blk->sgl = NULL;
		cmd_blk->sgl_addr = 0;
	}
	if (cmd_blk->residual)
		scsi_set_resid(scmd, cmd_blk->residual);
	if (status == MYRS_STATUS_DEVICE_NON_RESPONSIVE ||
	    status == MYRS_STATUS_DEVICE_NON_RESPONSIVE2)
		scmd->result = (DID_BAD_TARGET << 16);
	else
		scmd->result = (DID_OK << 16) | status;
	scsi_done(scmd);
}

static void myrs_handle_cmdblk(struct myrs_hba *cs, struct myrs_cmdblk *cmd_blk)
{
	if (!cmd_blk)
		return;

	if (cmd_blk->complete) {
		complete(cmd_blk->complete);
		cmd_blk->complete = NULL;
	}
}

static void myrs_monitor(struct work_struct *work)
{
	struct myrs_hba *cs = container_of(work, struct myrs_hba,
					   monitor_work.work);
	struct Scsi_Host *shost = cs->host;
	struct myrs_ctlr_info *info = cs->ctlr_info;
	unsigned int epoch = cs->fwstat_buf->epoch;
	unsigned long interval = MYRS_PRIMARY_MONITOR_INTERVAL;
	unsigned char status;

	dev_dbg(&shost->shost_gendev, "monitor tick\n");

	status = myrs_get_fwstatus(cs);

	if (cs->needs_update) {
		cs->needs_update = false;
		mutex_lock(&cs->cinfo_mutex);
		status = myrs_get_ctlr_info(cs);
		mutex_unlock(&cs->cinfo_mutex);
	}
	if (cs->fwstat_buf->next_evseq - cs->next_evseq > 0) {
		status = myrs_get_event(cs, cs->next_evseq,
					cs->event_buf);
		if (status == MYRS_STATUS_SUCCESS) {
			myrs_log_event(cs, cs->event_buf);
			cs->next_evseq++;
			interval = 1;
		}
	}

	if (time_after(jiffies, cs->secondary_monitor_time
		       + MYRS_SECONDARY_MONITOR_INTERVAL))
		cs->secondary_monitor_time = jiffies;

	if (info->bg_init_active +
	    info->ldev_init_active +
	    info->pdev_init_active +
	    info->cc_active +
	    info->rbld_active +
	    info->exp_active != 0) {
		struct scsi_device *sdev;

		shost_for_each_device(sdev, shost) {
			struct myrs_ldev_info *ldev_info;
			int ldev_num;

			if (sdev->channel < info->physchan_present)
				continue;
			ldev_info = sdev->hostdata;
			if (!ldev_info)
				continue;
			ldev_num = ldev_info->ldev_num;
			myrs_get_ldev_info(cs, ldev_num, ldev_info);
		}
		cs->needs_update = true;
	}
	if (epoch == cs->epoch &&
	    cs->fwstat_buf->next_evseq == cs->next_evseq &&
	    (cs->needs_update == false ||
	     time_before(jiffies, cs->primary_monitor_time
			 + MYRS_PRIMARY_MONITOR_INTERVAL))) {
		interval = MYRS_SECONDARY_MONITOR_INTERVAL;
	}

	if (interval > 1)
		cs->primary_monitor_time = jiffies;
	queue_delayed_work(cs->work_q, &cs->monitor_work, interval);
}

static bool myrs_create_mempools(struct pci_dev *pdev, struct myrs_hba *cs)
{
	struct Scsi_Host *shost = cs->host;
	size_t elem_size, elem_align;

	elem_align = sizeof(struct myrs_sge);
	elem_size = shost->sg_tablesize * elem_align;
	cs->sg_pool = dma_pool_create("myrs_sg", &pdev->dev,
				      elem_size, elem_align, 0);
	if (cs->sg_pool == NULL) {
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate SG pool\n");
		return false;
	}

	cs->sense_pool = dma_pool_create("myrs_sense", &pdev->dev,
					 MYRS_SENSE_SIZE, sizeof(int), 0);
	if (cs->sense_pool == NULL) {
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate sense data pool\n");
		return false;
	}

	cs->dcdb_pool = dma_pool_create("myrs_dcdb", &pdev->dev,
					MYRS_DCDB_SIZE,
					sizeof(unsigned char), 0);
	if (!cs->dcdb_pool) {
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		dma_pool_destroy(cs->sense_pool);
		cs->sense_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate DCDB pool\n");
		return false;
	}

	cs->work_q = alloc_ordered_workqueue("myrs_wq_%d", WQ_MEM_RECLAIM,
					     shost->host_no);
	if (!cs->work_q) {
		dma_pool_destroy(cs->dcdb_pool);
		cs->dcdb_pool = NULL;
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		dma_pool_destroy(cs->sense_pool);
		cs->sense_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to create workqueue\n");
		return false;
	}

	/* Initialize the Monitoring Timer. */
	INIT_DELAYED_WORK(&cs->monitor_work, myrs_monitor);
	queue_delayed_work(cs->work_q, &cs->monitor_work, 1);

	return true;
}

static void myrs_destroy_mempools(struct myrs_hba *cs)
{
	cancel_delayed_work_sync(&cs->monitor_work);
	destroy_workqueue(cs->work_q);

	dma_pool_destroy(cs->sg_pool);
	dma_pool_destroy(cs->dcdb_pool);
	dma_pool_destroy(cs->sense_pool);
}

static void myrs_unmap(struct myrs_hba *cs)
{
	kfree(cs->event_buf);
	kfree(cs->ctlr_info);
	if (cs->fwstat_buf) {
		dma_free_coherent(&cs->pdev->dev, sizeof(struct myrs_fwstat),
				  cs->fwstat_buf, cs->fwstat_addr);
		cs->fwstat_buf = NULL;
	}
	if (cs->first_stat_mbox) {
		dma_free_coherent(&cs->pdev->dev, cs->stat_mbox_size,
				  cs->first_stat_mbox, cs->stat_mbox_addr);
		cs->first_stat_mbox = NULL;
	}
	if (cs->first_cmd_mbox) {
		dma_free_coherent(&cs->pdev->dev, cs->cmd_mbox_size,
				  cs->first_cmd_mbox, cs->cmd_mbox_addr);
		cs->first_cmd_mbox = NULL;
	}
}

static void myrs_cleanup(struct myrs_hba *cs)
{
	struct pci_dev *pdev = cs->pdev;

	/* Free the memory mailbox, status, and related structures */
	myrs_unmap(cs);

	if (cs->mmio_base) {
		if (cs->disable_intr)
			cs->disable_intr(cs);
		iounmap(cs->mmio_base);
		cs->mmio_base = NULL;
	}
	if (cs->irq)
		free_irq(cs->irq, cs);
	if (cs->io_addr)
		release_region(cs->io_addr, 0x80);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	scsi_host_put(cs->host);
}

static struct myrs_hba *myrs_detect(struct pci_dev *pdev,
		const struct pci_device_id *entry)
{
	struct myrs_privdata *privdata =
		(struct myrs_privdata *)entry->driver_data;
	irq_handler_t irq_handler = privdata->irq_handler;
	unsigned int mmio_size = privdata->mmio_size;
	struct myrs_hba *cs = NULL;

	cs = myrs_alloc_host(pdev, entry);
	if (!cs) {
		dev_err(&pdev->dev, "Unable to allocate Controller\n");
		return NULL;
	}
	cs->pdev = pdev;

	if (pci_enable_device(pdev))
		goto Failure;

	cs->pci_addr = pci_resource_start(pdev, 0);

	pci_set_drvdata(pdev, cs);
	spin_lock_init(&cs->queue_lock);
	/* Map the Controller Register Window. */
	if (mmio_size < PAGE_SIZE)
		mmio_size = PAGE_SIZE;
	cs->mmio_base = ioremap(cs->pci_addr & PAGE_MASK, mmio_size);
	if (cs->mmio_base == NULL) {
		dev_err(&pdev->dev,
			"Unable to map Controller Register Window\n");
		goto Failure;
	}

	cs->io_base = cs->mmio_base + (cs->pci_addr & ~PAGE_MASK);
	if (privdata->hw_init(pdev, cs, cs->io_base))
		goto Failure;

	/* Acquire shared access to the IRQ Channel. */
	if (request_irq(pdev->irq, irq_handler, IRQF_SHARED, "myrs", cs) < 0) {
		dev_err(&pdev->dev,
			"Unable to acquire IRQ Channel %d\n", pdev->irq);
		goto Failure;
	}
	cs->irq = pdev->irq;
	return cs;

Failure:
	dev_err(&pdev->dev,
		"Failed to initialize Controller\n");
	myrs_cleanup(cs);
	return NULL;
}

/*
 * myrs_err_status reports Controller BIOS Messages passed through
 * the Error Status Register when the driver performs the BIOS handshaking.
 * It returns true for fatal errors and false otherwise.
 */

static bool myrs_err_status(struct myrs_hba *cs, unsigned char status,
		unsigned char parm0, unsigned char parm1)
{
	struct pci_dev *pdev = cs->pdev;

	switch (status) {
	case 0x00:
		dev_info(&pdev->dev,
			 "Physical Device %d:%d Not Responding\n",
			 parm1, parm0);
		break;
	case 0x08:
		dev_notice(&pdev->dev, "Spinning Up Drives\n");
		break;
	case 0x30:
		dev_notice(&pdev->dev, "Configuration Checksum Error\n");
		break;
	case 0x60:
		dev_notice(&pdev->dev, "Mirror Race Recovery Failed\n");
		break;
	case 0x70:
		dev_notice(&pdev->dev, "Mirror Race Recovery In Progress\n");
		break;
	case 0x90:
		dev_notice(&pdev->dev, "Physical Device %d:%d COD Mismatch\n",
			   parm1, parm0);
		break;
	case 0xA0:
		dev_notice(&pdev->dev, "Logical Drive Installation Aborted\n");
		break;
	case 0xB0:
		dev_notice(&pdev->dev, "Mirror Race On A Critical Logical Drive\n");
		break;
	case 0xD0:
		dev_notice(&pdev->dev, "New Controller Configuration Found\n");
		break;
	case 0xF0:
		dev_err(&pdev->dev, "Fatal Memory Parity Error\n");
		return true;
	default:
		dev_err(&pdev->dev, "Unknown Initialization Error %02X\n",
			status);
		return true;
	}
	return false;
}

/*
 * Hardware-specific functions
 */

/*
 * DAC960 GEM Series Controllers.
 */

static inline void DAC960_GEM_hw_mbox_new_cmd(void __iomem *base)
{
	__le32 val = cpu_to_le32(DAC960_GEM_IDB_HWMBOX_NEW_CMD << 24);

	writel(val, base + DAC960_GEM_IDB_READ_OFFSET);
}

static inline void DAC960_GEM_ack_hw_mbox_status(void __iomem *base)
{
	__le32 val = cpu_to_le32(DAC960_GEM_IDB_HWMBOX_ACK_STS << 24);

	writel(val, base + DAC960_GEM_IDB_CLEAR_OFFSET);
}

static inline void DAC960_GEM_reset_ctrl(void __iomem *base)
{
	__le32 val = cpu_to_le32(DAC960_GEM_IDB_CTRL_RESET << 24);

	writel(val, base + DAC960_GEM_IDB_READ_OFFSET);
}

static inline void DAC960_GEM_mem_mbox_new_cmd(void __iomem *base)
{
	__le32 val = cpu_to_le32(DAC960_GEM_IDB_HWMBOX_NEW_CMD << 24);

	writel(val, base + DAC960_GEM_IDB_READ_OFFSET);
}

static inline bool DAC960_GEM_hw_mbox_is_full(void __iomem *base)
{
	__le32 val;

	val = readl(base + DAC960_GEM_IDB_READ_OFFSET);
	return (le32_to_cpu(val) >> 24) & DAC960_GEM_IDB_HWMBOX_FULL;
}

static inline bool DAC960_GEM_init_in_progress(void __iomem *base)
{
	__le32 val;

	val = readl(base + DAC960_GEM_IDB_READ_OFFSET);
	return (le32_to_cpu(val) >> 24) & DAC960_GEM_IDB_INIT_IN_PROGRESS;
}

static inline void DAC960_GEM_ack_hw_mbox_intr(void __iomem *base)
{
	__le32 val = cpu_to_le32(DAC960_GEM_ODB_HWMBOX_ACK_IRQ << 24);

	writel(val, base + DAC960_GEM_ODB_CLEAR_OFFSET);
}

static inline void DAC960_GEM_ack_intr(void __iomem *base)
{
	__le32 val = cpu_to_le32((DAC960_GEM_ODB_HWMBOX_ACK_IRQ |
				  DAC960_GEM_ODB_MMBOX_ACK_IRQ) << 24);

	writel(val, base + DAC960_GEM_ODB_CLEAR_OFFSET);
}

static inline bool DAC960_GEM_hw_mbox_status_available(void __iomem *base)
{
	__le32 val;

	val = readl(base + DAC960_GEM_ODB_READ_OFFSET);
	return (le32_to_cpu(val) >> 24) & DAC960_GEM_ODB_HWMBOX_STS_AVAIL;
}

static inline void DAC960_GEM_enable_intr(void __iomem *base)
{
	__le32 val = cpu_to_le32((DAC960_GEM_IRQMASK_HWMBOX_IRQ |
				  DAC960_GEM_IRQMASK_MMBOX_IRQ) << 24);
	writel(val, base + DAC960_GEM_IRQMASK_CLEAR_OFFSET);
}

static inline void DAC960_GEM_disable_intr(void __iomem *base)
{
	__le32 val = 0;

	writel(val, base + DAC960_GEM_IRQMASK_READ_OFFSET);
}

static inline void DAC960_GEM_write_cmd_mbox(union myrs_cmd_mbox *mem_mbox,
		union myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->words[1], &mbox->words[1],
	       sizeof(union myrs_cmd_mbox) - sizeof(unsigned int));
	/* Barrier to avoid reordering */
	wmb();
	mem_mbox->words[0] = mbox->words[0];
	/* Barrier to force PCI access */
	mb();
}

static inline void DAC960_GEM_write_hw_mbox(void __iomem *base,
		dma_addr_t cmd_mbox_addr)
{
	dma_addr_writeql(cmd_mbox_addr, base + DAC960_GEM_CMDMBX_OFFSET);
}

static inline unsigned char DAC960_GEM_read_cmd_status(void __iomem *base)
{
	return readw(base + DAC960_GEM_CMDSTS_OFFSET + 2);
}

static inline bool
DAC960_GEM_read_error_status(void __iomem *base, unsigned char *error,
		unsigned char *param0, unsigned char *param1)
{
	__le32 val;

	val = readl(base + DAC960_GEM_ERRSTS_READ_OFFSET);
	if (!((le32_to_cpu(val) >> 24) & DAC960_GEM_ERRSTS_PENDING))
		return false;
	*error = val & ~(DAC960_GEM_ERRSTS_PENDING << 24);
	*param0 = readb(base + DAC960_GEM_CMDMBX_OFFSET + 0);
	*param1 = readb(base + DAC960_GEM_CMDMBX_OFFSET + 1);
	writel(0x03000000, base + DAC960_GEM_ERRSTS_CLEAR_OFFSET);
	return true;
}

static inline unsigned char
DAC960_GEM_mbox_init(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_GEM_hw_mbox_is_full(base))
		udelay(1);
	DAC960_GEM_write_hw_mbox(base, mbox_addr);
	DAC960_GEM_hw_mbox_new_cmd(base);
	while (!DAC960_GEM_hw_mbox_status_available(base))
		udelay(1);
	status = DAC960_GEM_read_cmd_status(base);
	DAC960_GEM_ack_hw_mbox_intr(base);
	DAC960_GEM_ack_hw_mbox_status(base);

	return status;
}

static int DAC960_GEM_hw_init(struct pci_dev *pdev,
		struct myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_GEM_disable_intr(base);
	DAC960_GEM_ack_hw_mbox_status(base);
	udelay(1000);
	while (DAC960_GEM_init_in_progress(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_GEM_read_error_status(base, &status,
						 &parm0, &parm1) &&
		    myrs_err_status(cs, status, parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_GEM_mbox_init)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_GEM_reset_ctrl(base);
		return -EAGAIN;
	}
	DAC960_GEM_enable_intr(base);
	cs->write_cmd_mbox = DAC960_GEM_write_cmd_mbox;
	cs->get_cmd_mbox = DAC960_GEM_mem_mbox_new_cmd;
	cs->disable_intr = DAC960_GEM_disable_intr;
	cs->reset = DAC960_GEM_reset_ctrl;
	return 0;
}

static irqreturn_t DAC960_GEM_intr_handler(int irq, void *arg)
{
	struct myrs_hba *cs = arg;
	void __iomem *base = cs->io_base;
	struct myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_GEM_ack_intr(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		struct myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(struct myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (cmd_blk) {
			if (id < 3)
				myrs_handle_cmdblk(cs, cmd_blk);
			else
				myrs_handle_scsi(cs, cmd_blk, scmd);
		}
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

static struct myrs_privdata DAC960_GEM_privdata = {
	.hw_init =		DAC960_GEM_hw_init,
	.irq_handler =		DAC960_GEM_intr_handler,
	.mmio_size =		DAC960_GEM_mmio_size,
};

/*
 * DAC960 BA Series Controllers.
 */

static inline void DAC960_BA_hw_mbox_new_cmd(void __iomem *base)
{
	writeb(DAC960_BA_IDB_HWMBOX_NEW_CMD, base + DAC960_BA_IDB_OFFSET);
}

static inline void DAC960_BA_ack_hw_mbox_status(void __iomem *base)
{
	writeb(DAC960_BA_IDB_HWMBOX_ACK_STS, base + DAC960_BA_IDB_OFFSET);
}

static inline void DAC960_BA_reset_ctrl(void __iomem *base)
{
	writeb(DAC960_BA_IDB_CTRL_RESET, base + DAC960_BA_IDB_OFFSET);
}

static inline void DAC960_BA_mem_mbox_new_cmd(void __iomem *base)
{
	writeb(DAC960_BA_IDB_MMBOX_NEW_CMD, base + DAC960_BA_IDB_OFFSET);
}

static inline bool DAC960_BA_hw_mbox_is_full(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_BA_IDB_OFFSET);
	return !(val & DAC960_BA_IDB_HWMBOX_EMPTY);
}

static inline bool DAC960_BA_init_in_progress(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_BA_IDB_OFFSET);
	return !(val & DAC960_BA_IDB_INIT_DONE);
}

static inline void DAC960_BA_ack_hw_mbox_intr(void __iomem *base)
{
	writeb(DAC960_BA_ODB_HWMBOX_ACK_IRQ, base + DAC960_BA_ODB_OFFSET);
}

static inline void DAC960_BA_ack_intr(void __iomem *base)
{
	writeb(DAC960_BA_ODB_HWMBOX_ACK_IRQ | DAC960_BA_ODB_MMBOX_ACK_IRQ,
	       base + DAC960_BA_ODB_OFFSET);
}

static inline bool DAC960_BA_hw_mbox_status_available(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_BA_ODB_OFFSET);
	return val & DAC960_BA_ODB_HWMBOX_STS_AVAIL;
}

static inline void DAC960_BA_enable_intr(void __iomem *base)
{
	writeb(~DAC960_BA_IRQMASK_DISABLE_IRQ, base + DAC960_BA_IRQMASK_OFFSET);
}

static inline void DAC960_BA_disable_intr(void __iomem *base)
{
	writeb(0xFF, base + DAC960_BA_IRQMASK_OFFSET);
}

static inline void DAC960_BA_write_cmd_mbox(union myrs_cmd_mbox *mem_mbox,
		union myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->words[1], &mbox->words[1],
	       sizeof(union myrs_cmd_mbox) - sizeof(unsigned int));
	/* Barrier to avoid reordering */
	wmb();
	mem_mbox->words[0] = mbox->words[0];
	/* Barrier to force PCI access */
	mb();
}


static inline void DAC960_BA_write_hw_mbox(void __iomem *base,
		dma_addr_t cmd_mbox_addr)
{
	dma_addr_writeql(cmd_mbox_addr, base + DAC960_BA_CMDMBX_OFFSET);
}

static inline unsigned char DAC960_BA_read_cmd_status(void __iomem *base)
{
	return readw(base + DAC960_BA_CMDSTS_OFFSET + 2);
}

static inline bool
DAC960_BA_read_error_status(void __iomem *base, unsigned char *error,
		unsigned char *param0, unsigned char *param1)
{
	u8 val;

	val = readb(base + DAC960_BA_ERRSTS_OFFSET);
	if (!(val & DAC960_BA_ERRSTS_PENDING))
		return false;
	val &= ~DAC960_BA_ERRSTS_PENDING;
	*error = val;
	*param0 = readb(base + DAC960_BA_CMDMBX_OFFSET + 0);
	*param1 = readb(base + DAC960_BA_CMDMBX_OFFSET + 1);
	writeb(0xFF, base + DAC960_BA_ERRSTS_OFFSET);
	return true;
}

static inline unsigned char
DAC960_BA_mbox_init(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_BA_hw_mbox_is_full(base))
		udelay(1);
	DAC960_BA_write_hw_mbox(base, mbox_addr);
	DAC960_BA_hw_mbox_new_cmd(base);
	while (!DAC960_BA_hw_mbox_status_available(base))
		udelay(1);
	status = DAC960_BA_read_cmd_status(base);
	DAC960_BA_ack_hw_mbox_intr(base);
	DAC960_BA_ack_hw_mbox_status(base);

	return status;
}

static int DAC960_BA_hw_init(struct pci_dev *pdev,
		struct myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_BA_disable_intr(base);
	DAC960_BA_ack_hw_mbox_status(base);
	udelay(1000);
	while (DAC960_BA_init_in_progress(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_BA_read_error_status(base, &status,
					      &parm0, &parm1) &&
		    myrs_err_status(cs, status, parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_BA_mbox_init)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_BA_reset_ctrl(base);
		return -EAGAIN;
	}
	DAC960_BA_enable_intr(base);
	cs->write_cmd_mbox = DAC960_BA_write_cmd_mbox;
	cs->get_cmd_mbox = DAC960_BA_mem_mbox_new_cmd;
	cs->disable_intr = DAC960_BA_disable_intr;
	cs->reset = DAC960_BA_reset_ctrl;
	return 0;
}

static irqreturn_t DAC960_BA_intr_handler(int irq, void *arg)
{
	struct myrs_hba *cs = arg;
	void __iomem *base = cs->io_base;
	struct myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_BA_ack_intr(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		struct myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(struct myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (cmd_blk) {
			if (id < 3)
				myrs_handle_cmdblk(cs, cmd_blk);
			else
				myrs_handle_scsi(cs, cmd_blk, scmd);
		}
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

static struct myrs_privdata DAC960_BA_privdata = {
	.hw_init =		DAC960_BA_hw_init,
	.irq_handler =		DAC960_BA_intr_handler,
	.mmio_size =		DAC960_BA_mmio_size,
};

/*
 * DAC960 LP Series Controllers.
 */

static inline void DAC960_LP_hw_mbox_new_cmd(void __iomem *base)
{
	writeb(DAC960_LP_IDB_HWMBOX_NEW_CMD, base + DAC960_LP_IDB_OFFSET);
}

static inline void DAC960_LP_ack_hw_mbox_status(void __iomem *base)
{
	writeb(DAC960_LP_IDB_HWMBOX_ACK_STS, base + DAC960_LP_IDB_OFFSET);
}

static inline void DAC960_LP_reset_ctrl(void __iomem *base)
{
	writeb(DAC960_LP_IDB_CTRL_RESET, base + DAC960_LP_IDB_OFFSET);
}

static inline void DAC960_LP_mem_mbox_new_cmd(void __iomem *base)
{
	writeb(DAC960_LP_IDB_MMBOX_NEW_CMD, base + DAC960_LP_IDB_OFFSET);
}

static inline bool DAC960_LP_hw_mbox_is_full(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_LP_IDB_OFFSET);
	return val & DAC960_LP_IDB_HWMBOX_FULL;
}

static inline bool DAC960_LP_init_in_progress(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_LP_IDB_OFFSET);
	return val & DAC960_LP_IDB_INIT_IN_PROGRESS;
}

static inline void DAC960_LP_ack_hw_mbox_intr(void __iomem *base)
{
	writeb(DAC960_LP_ODB_HWMBOX_ACK_IRQ, base + DAC960_LP_ODB_OFFSET);
}

static inline void DAC960_LP_ack_intr(void __iomem *base)
{
	writeb(DAC960_LP_ODB_HWMBOX_ACK_IRQ | DAC960_LP_ODB_MMBOX_ACK_IRQ,
	       base + DAC960_LP_ODB_OFFSET);
}

static inline bool DAC960_LP_hw_mbox_status_available(void __iomem *base)
{
	u8 val;

	val = readb(base + DAC960_LP_ODB_OFFSET);
	return val & DAC960_LP_ODB_HWMBOX_STS_AVAIL;
}

static inline void DAC960_LP_enable_intr(void __iomem *base)
{
	writeb(~DAC960_LP_IRQMASK_DISABLE_IRQ, base + DAC960_LP_IRQMASK_OFFSET);
}

static inline void DAC960_LP_disable_intr(void __iomem *base)
{
	writeb(0xFF, base + DAC960_LP_IRQMASK_OFFSET);
}

static inline void DAC960_LP_write_cmd_mbox(union myrs_cmd_mbox *mem_mbox,
		union myrs_cmd_mbox *mbox)
{
	memcpy(&mem_mbox->words[1], &mbox->words[1],
	       sizeof(union myrs_cmd_mbox) - sizeof(unsigned int));
	/* Barrier to avoid reordering */
	wmb();
	mem_mbox->words[0] = mbox->words[0];
	/* Barrier to force PCI access */
	mb();
}

static inline void DAC960_LP_write_hw_mbox(void __iomem *base,
		dma_addr_t cmd_mbox_addr)
{
	dma_addr_writeql(cmd_mbox_addr, base + DAC960_LP_CMDMBX_OFFSET);
}

static inline unsigned char DAC960_LP_read_cmd_status(void __iomem *base)
{
	return readw(base + DAC960_LP_CMDSTS_OFFSET + 2);
}

static inline bool
DAC960_LP_read_error_status(void __iomem *base, unsigned char *error,
		unsigned char *param0, unsigned char *param1)
{
	u8 val;

	val = readb(base + DAC960_LP_ERRSTS_OFFSET);
	if (!(val & DAC960_LP_ERRSTS_PENDING))
		return false;
	val &= ~DAC960_LP_ERRSTS_PENDING;
	*error = val;
	*param0 = readb(base + DAC960_LP_CMDMBX_OFFSET + 0);
	*param1 = readb(base + DAC960_LP_CMDMBX_OFFSET + 1);
	writeb(0xFF, base + DAC960_LP_ERRSTS_OFFSET);
	return true;
}

static inline unsigned char
DAC960_LP_mbox_init(void __iomem *base, dma_addr_t mbox_addr)
{
	unsigned char status;

	while (DAC960_LP_hw_mbox_is_full(base))
		udelay(1);
	DAC960_LP_write_hw_mbox(base, mbox_addr);
	DAC960_LP_hw_mbox_new_cmd(base);
	while (!DAC960_LP_hw_mbox_status_available(base))
		udelay(1);
	status = DAC960_LP_read_cmd_status(base);
	DAC960_LP_ack_hw_mbox_intr(base);
	DAC960_LP_ack_hw_mbox_status(base);

	return status;
}

static int DAC960_LP_hw_init(struct pci_dev *pdev,
		struct myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_LP_disable_intr(base);
	DAC960_LP_ack_hw_mbox_status(base);
	udelay(1000);
	while (DAC960_LP_init_in_progress(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_LP_read_error_status(base, &status,
					      &parm0, &parm1) &&
		    myrs_err_status(cs, status, parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_LP_mbox_init)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_LP_reset_ctrl(base);
		return -ENODEV;
	}
	DAC960_LP_enable_intr(base);
	cs->write_cmd_mbox = DAC960_LP_write_cmd_mbox;
	cs->get_cmd_mbox = DAC960_LP_mem_mbox_new_cmd;
	cs->disable_intr = DAC960_LP_disable_intr;
	cs->reset = DAC960_LP_reset_ctrl;

	return 0;
}

static irqreturn_t DAC960_LP_intr_handler(int irq, void *arg)
{
	struct myrs_hba *cs = arg;
	void __iomem *base = cs->io_base;
	struct myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_LP_ack_intr(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		struct myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(struct myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (cmd_blk) {
			if (id < 3)
				myrs_handle_cmdblk(cs, cmd_blk);
			else
				myrs_handle_scsi(cs, cmd_blk, scmd);
		}
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

static struct myrs_privdata DAC960_LP_privdata = {
	.hw_init =		DAC960_LP_hw_init,
	.irq_handler =		DAC960_LP_intr_handler,
	.mmio_size =		DAC960_LP_mmio_size,
};

/*
 * Module functions
 */
static int
myrs_probe(struct pci_dev *dev, const struct pci_device_id *entry)
{
	struct myrs_hba *cs;
	int ret;

	cs = myrs_detect(dev, entry);
	if (!cs)
		return -ENODEV;

	ret = myrs_get_config(cs);
	if (ret < 0) {
		myrs_cleanup(cs);
		return ret;
	}

	if (!myrs_create_mempools(dev, cs)) {
		ret = -ENOMEM;
		goto failed;
	}

	ret = scsi_add_host(cs->host, &dev->dev);
	if (ret) {
		dev_err(&dev->dev, "scsi_add_host failed with %d\n", ret);
		myrs_destroy_mempools(cs);
		goto failed;
	}
	scsi_scan_host(cs->host);
	return 0;
failed:
	myrs_cleanup(cs);
	return ret;
}


static void myrs_remove(struct pci_dev *pdev)
{
	struct myrs_hba *cs = pci_get_drvdata(pdev);

	if (cs == NULL)
		return;

	shost_printk(KERN_NOTICE, cs->host, "Flushing Cache...");
	myrs_flush_cache(cs);
	myrs_destroy_mempools(cs);
	myrs_cleanup(cs);
}


static const struct pci_device_id myrs_id_table[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_MYLEX,
			       PCI_DEVICE_ID_MYLEX_DAC960_GEM,
			       PCI_VENDOR_ID_MYLEX, PCI_ANY_ID),
		.driver_data	= (unsigned long) &DAC960_GEM_privdata,
	},
	{
		PCI_DEVICE_DATA(MYLEX, DAC960_BA, &DAC960_BA_privdata),
	},
	{
		PCI_DEVICE_DATA(MYLEX, DAC960_LP, &DAC960_LP_privdata),
	},
	{0, },
};

MODULE_DEVICE_TABLE(pci, myrs_id_table);

static struct pci_driver myrs_pci_driver = {
	.name		= "myrs",
	.id_table	= myrs_id_table,
	.probe		= myrs_probe,
	.remove		= myrs_remove,
};

static int __init myrs_init_module(void)
{
	int ret;

	myrs_raid_template = raid_class_attach(&myrs_raid_functions);
	if (!myrs_raid_template)
		return -ENODEV;

	ret = pci_register_driver(&myrs_pci_driver);
	if (ret)
		raid_class_release(myrs_raid_template);

	return ret;
}

static void __exit myrs_cleanup_module(void)
{
	pci_unregister_driver(&myrs_pci_driver);
	raid_class_release(myrs_raid_template);
}

module_init(myrs_init_module);
module_exit(myrs_cleanup_module);

MODULE_DESCRIPTION("Mylex DAC960/AcceleRAID/eXtremeRAID driver (SCSI Interface)");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.com>");
MODULE_LICENSE("GPL");
