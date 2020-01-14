// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2013  LSI Corporation
 *  Copyright (c) 2013-2016  Avago Technologies
 *  Copyright (c) 2016-2018  Broadcom Inc.
 *
 *  FILE: megaraid_sas_fusion.c
 *
 *  Authors: Broadcom Inc.
 *           Sumant Patro
 *           Adam Radford
 *           Kashyap Desai <kashyap.desai@broadcom.com>
 *           Sumit Saxena <sumit.saxena@broadcom.com>
 *
 *  Send feedback to: megaraidlinux.pdl@broadcom.com
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/irq_poll.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_dbg.h>
#include <linux/dmi.h>

#include "megaraid_sas_fusion.h"
#include "megaraid_sas.h"


extern void megasas_free_cmds(struct megasas_instance *instance);
extern struct megasas_cmd *megasas_get_cmd(struct megasas_instance
					   *instance);
extern void
megasas_complete_cmd(struct megasas_instance *instance,
		     struct megasas_cmd *cmd, u8 alt_status);
int
wait_and_poll(struct megasas_instance *instance, struct megasas_cmd *cmd,
	      int seconds);

void
megasas_return_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd);
int megasas_alloc_cmds(struct megasas_instance *instance);
int
megasas_clear_intr_fusion(struct megasas_instance *instance);
int
megasas_issue_polled(struct megasas_instance *instance,
		     struct megasas_cmd *cmd);
void
megasas_check_and_restore_queue_depth(struct megasas_instance *instance);

int megasas_transition_to_ready(struct megasas_instance *instance, int ocr);
void megaraid_sas_kill_hba(struct megasas_instance *instance);

extern u32 megasas_dbg_lvl;
int megasas_sriov_start_heartbeat(struct megasas_instance *instance,
				  int initial);
void megasas_start_timer(struct megasas_instance *instance);
extern struct megasas_mgmt_info megasas_mgmt_info;
extern unsigned int resetwaittime;
extern unsigned int dual_qdepth_disable;
static void megasas_free_rdpq_fusion(struct megasas_instance *instance);
static void megasas_free_reply_fusion(struct megasas_instance *instance);
static inline
void megasas_configure_queue_sizes(struct megasas_instance *instance);
static void megasas_fusion_crash_dump(struct megasas_instance *instance);
extern u32 megasas_readl(struct megasas_instance *instance,
			 const volatile void __iomem *addr);

/**
 * megasas_adp_reset_wait_for_ready -	initiate chip reset and wait for
 *					controller to come to ready state
 * @instance -				adapter's soft state
 * @do_adp_reset -			If true, do a chip reset
 * @ocr_context -			If called from OCR context this will
 *					be set to 1, else 0
 *
 * This function initates a chip reset followed by a wait for controller to
 * transition to ready state.
 * During this, driver will block all access to PCI config space from userspace
 */
int
megasas_adp_reset_wait_for_ready(struct megasas_instance *instance,
				 bool do_adp_reset,
				 int ocr_context)
{
	int ret = FAILED;

	/*
	 * Block access to PCI config space from userspace
	 * when diag reset is initiated from driver
	 */
	if (megasas_dbg_lvl & OCR_DEBUG)
		dev_info(&instance->pdev->dev,
			 "Block access to PCI config space %s %d\n",
			 __func__, __LINE__);

	pci_cfg_access_lock(instance->pdev);

	if (do_adp_reset) {
		if (instance->instancet->adp_reset
			(instance, instance->reg_set))
			goto out;
	}

	/* Wait for FW to become ready */
	if (megasas_transition_to_ready(instance, ocr_context)) {
		dev_warn(&instance->pdev->dev,
			 "Failed to transition controller to ready for scsi%d.\n",
			 instance->host->host_no);
		goto out;
	}

	ret = SUCCESS;
out:
	if (megasas_dbg_lvl & OCR_DEBUG)
		dev_info(&instance->pdev->dev,
			 "Unlock access to PCI config space %s %d\n",
			 __func__, __LINE__);

	pci_cfg_access_unlock(instance->pdev);

	return ret;
}

/**
 * megasas_check_same_4gb_region -	check if allocation
 *					crosses same 4GB boundary or not
 * @instance -				adapter's soft instance
 * start_addr -			start address of DMA allocation
 * size -				size of allocation in bytes
 * return -				true : allocation does not cross same
 *					4GB boundary
 *					false: allocation crosses same
 *					4GB boundary
 */
static inline bool megasas_check_same_4gb_region
	(struct megasas_instance *instance, dma_addr_t start_addr, size_t size)
{
	dma_addr_t end_addr;

	end_addr = start_addr + size;

	if (upper_32_bits(start_addr) != upper_32_bits(end_addr)) {
		dev_err(&instance->pdev->dev,
			"Failed to get same 4GB boundary: start_addr: 0x%llx end_addr: 0x%llx\n",
			(unsigned long long)start_addr,
			(unsigned long long)end_addr);
		return false;
	}

	return true;
}

/**
 * megasas_enable_intr_fusion -	Enables interrupts
 * @regs:			MFI register set
 */
void
megasas_enable_intr_fusion(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;

	instance->mask_interrupts = 0;
	/* For Thunderbolt/Invader also clear intr on enable */
	writel(~0, &regs->outbound_intr_status);
	readl(&regs->outbound_intr_status);

	writel(~MFI_FUSION_ENABLE_INTERRUPT_MASK, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	dev_info(&instance->pdev->dev, "%s is called outbound_intr_mask:0x%08x\n",
		 __func__, readl(&regs->outbound_intr_mask));
}

/**
 * megasas_disable_intr_fusion - Disables interrupt
 * @regs:			 MFI register set
 */
void
megasas_disable_intr_fusion(struct megasas_instance *instance)
{
	u32 mask = 0xFFFFFFFF;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;
	instance->mask_interrupts = 1;

	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	dev_info(&instance->pdev->dev, "%s is called outbound_intr_mask:0x%08x\n",
		 __func__, readl(&regs->outbound_intr_mask));
}

int
megasas_clear_intr_fusion(struct megasas_instance *instance)
{
	u32 status;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;
	/*
	 * Check if it is our interrupt
	 */
	status = megasas_readl(instance,
			       &regs->outbound_intr_status);

	if (status & 1) {
		writel(status, &regs->outbound_intr_status);
		readl(&regs->outbound_intr_status);
		return 1;
	}
	if (!(status & MFI_FUSION_ENABLE_INTERRUPT_MASK))
		return 0;

	return 1;
}

/**
 * megasas_get_cmd_fusion -	Get a command from the free pool
 * @instance:		Adapter soft state
 *
 * Returns a blk_tag indexed mpt frame
 */
inline struct megasas_cmd_fusion *megasas_get_cmd_fusion(struct megasas_instance
						  *instance, u32 blk_tag)
{
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;
	return fusion->cmd_list[blk_tag];
}

/**
 * megasas_return_cmd_fusion -	Return a cmd to free command pool
 * @instance:		Adapter soft state
 * @cmd:		Command packet to be returned to free command pool
 */
inline void megasas_return_cmd_fusion(struct megasas_instance *instance,
	struct megasas_cmd_fusion *cmd)
{
	cmd->scmd = NULL;
	memset(cmd->io_request, 0, MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE);
	cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;
	cmd->cmd_completed = false;
}

/**
 * megasas_write_64bit_req_desc -	PCI writes 64bit request descriptor
 * @instance:				Adapter soft state
 * @req_desc:				64bit Request descriptor
 */
static void
megasas_write_64bit_req_desc(struct megasas_instance *instance,
		union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc)
{
#if defined(writeq) && defined(CONFIG_64BIT)
	u64 req_data = (((u64)le32_to_cpu(req_desc->u.high) << 32) |
		le32_to_cpu(req_desc->u.low));
	writeq(req_data, &instance->reg_set->inbound_low_queue_port);
#else
	unsigned long flags;
	spin_lock_irqsave(&instance->hba_lock, flags);
	writel(le32_to_cpu(req_desc->u.low),
		&instance->reg_set->inbound_low_queue_port);
	writel(le32_to_cpu(req_desc->u.high),
		&instance->reg_set->inbound_high_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
#endif
}

/**
 * megasas_fire_cmd_fusion -	Sends command to the FW
 * @instance:			Adapter soft state
 * @req_desc:			32bit or 64bit Request descriptor
 *
 * Perform PCI Write. AERO SERIES supports 32 bit Descriptor.
 * Prior to AERO_SERIES support 64 bit Descriptor.
 */
static void
megasas_fire_cmd_fusion(struct megasas_instance *instance,
		union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc)
{
	if (instance->atomic_desc_support)
		writel(le32_to_cpu(req_desc->u.low),
			&instance->reg_set->inbound_single_queue_port);
	else
		megasas_write_64bit_req_desc(instance, req_desc);
}

/**
 * megasas_fusion_update_can_queue -	Do all Adapter Queue depth related calculations here
 * @instance:							Adapter soft state
 * fw_boot_context:						Whether this function called during probe or after OCR
 *
 * This function is only for fusion controllers.
 * Update host can queue, if firmware downgrade max supported firmware commands.
 * Firmware upgrade case will be skiped because underlying firmware has
 * more resource than exposed to the OS.
 *
 */
static void
megasas_fusion_update_can_queue(struct megasas_instance *instance, int fw_boot_context)
{
	u16 cur_max_fw_cmds = 0;
	u16 ldio_threshold = 0;

	/* ventura FW does not fill outbound_scratch_pad_2 with queue depth */
	if (instance->adapter_type < VENTURA_SERIES)
		cur_max_fw_cmds =
		megasas_readl(instance,
			      &instance->reg_set->outbound_scratch_pad_2) & 0x00FFFF;

	if (dual_qdepth_disable || !cur_max_fw_cmds)
		cur_max_fw_cmds = instance->instancet->read_fw_status_reg(instance) & 0x00FFFF;
	else
		ldio_threshold =
			(instance->instancet->read_fw_status_reg(instance) & 0x00FFFF) - MEGASAS_FUSION_IOCTL_CMDS;

	dev_info(&instance->pdev->dev,
		 "Current firmware supports maximum commands: %d\t LDIO threshold: %d\n",
		 cur_max_fw_cmds, ldio_threshold);

	if (fw_boot_context == OCR_CONTEXT) {
		cur_max_fw_cmds = cur_max_fw_cmds - 1;
		if (cur_max_fw_cmds < instance->max_fw_cmds) {
			instance->cur_can_queue =
				cur_max_fw_cmds - (MEGASAS_FUSION_INTERNAL_CMDS +
						MEGASAS_FUSION_IOCTL_CMDS);
			instance->host->can_queue = instance->cur_can_queue;
			instance->ldio_threshold = ldio_threshold;
		}
	} else {
		instance->max_fw_cmds = cur_max_fw_cmds;
		instance->ldio_threshold = ldio_threshold;

		if (reset_devices)
			instance->max_fw_cmds = min(instance->max_fw_cmds,
						(u16)MEGASAS_KDUMP_QUEUE_DEPTH);
		/*
		* Reduce the max supported cmds by 1. This is to ensure that the
		* reply_q_sz (1 more than the max cmd that driver may send)
		* does not exceed max cmds that the FW can support
		*/
		instance->max_fw_cmds = instance->max_fw_cmds-1;
	}
}
/**
 * megasas_free_cmds_fusion -	Free all the cmds in the free cmd pool
 * @instance:		Adapter soft state
 */
void
megasas_free_cmds_fusion(struct megasas_instance *instance)
{
	int i;
	struct fusion_context *fusion = instance->ctrl_context;
	struct megasas_cmd_fusion *cmd;

	if (fusion->sense)
		dma_pool_free(fusion->sense_dma_pool, fusion->sense,
			      fusion->sense_phys_addr);

	/* SG */
	if (fusion->cmd_list) {
		for (i = 0; i < instance->max_mpt_cmds; i++) {
			cmd = fusion->cmd_list[i];
			if (cmd) {
				if (cmd->sg_frame)
					dma_pool_free(fusion->sg_dma_pool,
						      cmd->sg_frame,
						      cmd->sg_frame_phys_addr);
			}
			kfree(cmd);
		}
		kfree(fusion->cmd_list);
	}

	if (fusion->sg_dma_pool) {
		dma_pool_destroy(fusion->sg_dma_pool);
		fusion->sg_dma_pool = NULL;
	}
	if (fusion->sense_dma_pool) {
		dma_pool_destroy(fusion->sense_dma_pool);
		fusion->sense_dma_pool = NULL;
	}


	/* Reply Frame, Desc*/
	if (instance->is_rdpq)
		megasas_free_rdpq_fusion(instance);
	else
		megasas_free_reply_fusion(instance);

	/* Request Frame, Desc*/
	if (fusion->req_frames_desc)
		dma_free_coherent(&instance->pdev->dev,
			fusion->request_alloc_sz, fusion->req_frames_desc,
			fusion->req_frames_desc_phys);
	if (fusion->io_request_frames)
		dma_pool_free(fusion->io_request_frames_pool,
			fusion->io_request_frames,
			fusion->io_request_frames_phys);
	if (fusion->io_request_frames_pool) {
		dma_pool_destroy(fusion->io_request_frames_pool);
		fusion->io_request_frames_pool = NULL;
	}
}

/**
 * megasas_create_sg_sense_fusion -	Creates DMA pool for cmd frames
 * @instance:			Adapter soft state
 *
 */
static int megasas_create_sg_sense_fusion(struct megasas_instance *instance)
{
	int i;
	u16 max_cmd;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;
	int sense_sz;
	u32 offset;

	fusion = instance->ctrl_context;
	max_cmd = instance->max_fw_cmds;
	sense_sz = instance->max_mpt_cmds * SCSI_SENSE_BUFFERSIZE;

	fusion->sg_dma_pool =
			dma_pool_create("mr_sg", &instance->pdev->dev,
				instance->max_chain_frame_sz,
				MR_DEFAULT_NVME_PAGE_SIZE, 0);
	/* SCSI_SENSE_BUFFERSIZE  = 96 bytes */
	fusion->sense_dma_pool =
			dma_pool_create("mr_sense", &instance->pdev->dev,
				sense_sz, 64, 0);

	if (!fusion->sense_dma_pool || !fusion->sg_dma_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->sense = dma_pool_alloc(fusion->sense_dma_pool,
				       GFP_KERNEL, &fusion->sense_phys_addr);
	if (!fusion->sense) {
		dev_err(&instance->pdev->dev,
			"failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	/* sense buffer, request frame and reply desc pool requires to be in
	 * same 4 gb region. Below function will check this.
	 * In case of failure, new pci pool will be created with updated
	 * alignment.
	 * Older allocation and pool will be destroyed.
	 * Alignment will be used such a way that next allocation if success,
	 * will always meet same 4gb region requirement.
	 * Actual requirement is not alignment, but we need start and end of
	 * DMA address must have same upper 32 bit address.
	 */

	if (!megasas_check_same_4gb_region(instance, fusion->sense_phys_addr,
					   sense_sz)) {
		dma_pool_free(fusion->sense_dma_pool, fusion->sense,
			      fusion->sense_phys_addr);
		fusion->sense = NULL;
		dma_pool_destroy(fusion->sense_dma_pool);

		fusion->sense_dma_pool =
			dma_pool_create("mr_sense_align", &instance->pdev->dev,
					sense_sz, roundup_pow_of_two(sense_sz),
					0);
		if (!fusion->sense_dma_pool) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
		fusion->sense = dma_pool_alloc(fusion->sense_dma_pool,
					       GFP_KERNEL,
					       &fusion->sense_phys_addr);
		if (!fusion->sense) {
			dev_err(&instance->pdev->dev,
				"failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	/*
	 * Allocate and attach a frame to each of the commands in cmd_list
	 */
	for (i = 0; i < max_cmd; i++) {
		cmd = fusion->cmd_list[i];
		cmd->sg_frame = dma_pool_alloc(fusion->sg_dma_pool,
					GFP_KERNEL, &cmd->sg_frame_phys_addr);

		offset = SCSI_SENSE_BUFFERSIZE * i;
		cmd->sense = (u8 *)fusion->sense + offset;
		cmd->sense_phys_addr = fusion->sense_phys_addr + offset;

		if (!cmd->sg_frame) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	/* create sense buffer for the raid 1/10 fp */
	for (i = max_cmd; i < instance->max_mpt_cmds; i++) {
		cmd = fusion->cmd_list[i];
		offset = SCSI_SENSE_BUFFERSIZE * i;
		cmd->sense = (u8 *)fusion->sense + offset;
		cmd->sense_phys_addr = fusion->sense_phys_addr + offset;

	}

	return 0;
}

static int
megasas_alloc_cmdlist_fusion(struct megasas_instance *instance)
{
	u32 max_mpt_cmd, i, j;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	max_mpt_cmd = instance->max_mpt_cmds;

	/*
	 * fusion->cmd_list is an array of struct megasas_cmd_fusion pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */
	fusion->cmd_list =
		kcalloc(max_mpt_cmd, sizeof(struct megasas_cmd_fusion *),
			GFP_KERNEL);
	if (!fusion->cmd_list) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	for (i = 0; i < max_mpt_cmd; i++) {
		fusion->cmd_list[i] = kzalloc(sizeof(struct megasas_cmd_fusion),
					      GFP_KERNEL);
		if (!fusion->cmd_list[i]) {
			for (j = 0; j < i; j++)
				kfree(fusion->cmd_list[j]);
			kfree(fusion->cmd_list);
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	return 0;
}

static int
megasas_alloc_request_fusion(struct megasas_instance *instance)
{
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

retry_alloc:
	fusion->io_request_frames_pool =
			dma_pool_create("mr_ioreq", &instance->pdev->dev,
				fusion->io_frames_alloc_sz, 16, 0);

	if (!fusion->io_request_frames_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->io_request_frames =
			dma_pool_alloc(fusion->io_request_frames_pool,
				GFP_KERNEL, &fusion->io_request_frames_phys);
	if (!fusion->io_request_frames) {
		if (instance->max_fw_cmds >= (MEGASAS_REDUCE_QD_COUNT * 2)) {
			instance->max_fw_cmds -= MEGASAS_REDUCE_QD_COUNT;
			dma_pool_destroy(fusion->io_request_frames_pool);
			megasas_configure_queue_sizes(instance);
			goto retry_alloc;
		} else {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	if (!megasas_check_same_4gb_region(instance,
					   fusion->io_request_frames_phys,
					   fusion->io_frames_alloc_sz)) {
		dma_pool_free(fusion->io_request_frames_pool,
			      fusion->io_request_frames,
			      fusion->io_request_frames_phys);
		fusion->io_request_frames = NULL;
		dma_pool_destroy(fusion->io_request_frames_pool);

		fusion->io_request_frames_pool =
			dma_pool_create("mr_ioreq_align",
					&instance->pdev->dev,
					fusion->io_frames_alloc_sz,
					roundup_pow_of_two(fusion->io_frames_alloc_sz),
					0);

		if (!fusion->io_request_frames_pool) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}

		fusion->io_request_frames =
			dma_pool_alloc(fusion->io_request_frames_pool,
				       GFP_KERNEL,
				       &fusion->io_request_frames_phys);

		if (!fusion->io_request_frames) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	fusion->req_frames_desc =
		dma_alloc_coherent(&instance->pdev->dev,
				   fusion->request_alloc_sz,
				   &fusion->req_frames_desc_phys, GFP_KERNEL);
	if (!fusion->req_frames_desc) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	return 0;
}

static int
megasas_alloc_reply_fusion(struct megasas_instance *instance)
{
	int i, count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;
	fusion = instance->ctrl_context;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	fusion->reply_frames_desc_pool =
			dma_pool_create("mr_reply", &instance->pdev->dev,
				fusion->reply_alloc_sz * count, 16, 0);

	if (!fusion->reply_frames_desc_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->reply_frames_desc[0] =
		dma_pool_alloc(fusion->reply_frames_desc_pool,
			GFP_KERNEL, &fusion->reply_frames_desc_phys[0]);
	if (!fusion->reply_frames_desc[0]) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	if (!megasas_check_same_4gb_region(instance,
					   fusion->reply_frames_desc_phys[0],
					   (fusion->reply_alloc_sz * count))) {
		dma_pool_free(fusion->reply_frames_desc_pool,
			      fusion->reply_frames_desc[0],
			      fusion->reply_frames_desc_phys[0]);
		fusion->reply_frames_desc[0] = NULL;
		dma_pool_destroy(fusion->reply_frames_desc_pool);

		fusion->reply_frames_desc_pool =
			dma_pool_create("mr_reply_align",
					&instance->pdev->dev,
					fusion->reply_alloc_sz * count,
					roundup_pow_of_two(fusion->reply_alloc_sz * count),
					0);

		if (!fusion->reply_frames_desc_pool) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}

		fusion->reply_frames_desc[0] =
			dma_pool_alloc(fusion->reply_frames_desc_pool,
				       GFP_KERNEL,
				       &fusion->reply_frames_desc_phys[0]);

		if (!fusion->reply_frames_desc[0]) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}

	reply_desc = fusion->reply_frames_desc[0];
	for (i = 0; i < fusion->reply_q_depth * count; i++, reply_desc++)
		reply_desc->Words = cpu_to_le64(ULLONG_MAX);

	/* This is not a rdpq mode, but driver still populate
	 * reply_frame_desc array to use same msix index in ISR path.
	 */
	for (i = 0; i < (count - 1); i++)
		fusion->reply_frames_desc[i + 1] =
			fusion->reply_frames_desc[i] +
			(fusion->reply_alloc_sz)/sizeof(union MPI2_REPLY_DESCRIPTORS_UNION);

	return 0;
}

static int
megasas_alloc_rdpq_fusion(struct megasas_instance *instance)
{
	int i, j, k, msix_count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;
	union MPI2_REPLY_DESCRIPTORS_UNION *rdpq_chunk_virt[RDPQ_MAX_CHUNK_COUNT];
	dma_addr_t rdpq_chunk_phys[RDPQ_MAX_CHUNK_COUNT];
	u8 dma_alloc_count, abs_index;
	u32 chunk_size, array_size, offset;

	fusion = instance->ctrl_context;
	chunk_size = fusion->reply_alloc_sz * RDPQ_MAX_INDEX_IN_ONE_CHUNK;
	array_size = sizeof(struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY) *
		     MAX_MSIX_QUEUES_FUSION;

	fusion->rdpq_virt = dma_alloc_coherent(&instance->pdev->dev,
					       array_size, &fusion->rdpq_phys,
					       GFP_KERNEL);
	if (!fusion->rdpq_virt) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	msix_count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	fusion->reply_frames_desc_pool = dma_pool_create("mr_rdpq",
							 &instance->pdev->dev,
							 chunk_size, 16, 0);
	fusion->reply_frames_desc_pool_align =
				dma_pool_create("mr_rdpq_align",
						&instance->pdev->dev,
						chunk_size,
						roundup_pow_of_two(chunk_size),
						0);

	if (!fusion->reply_frames_desc_pool ||
	    !fusion->reply_frames_desc_pool_align) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

/*
 * For INVADER_SERIES each set of 8 reply queues(0-7, 8-15, ..) and
 * VENTURA_SERIES each set of 16 reply queues(0-15, 16-31, ..) should be
 * within 4GB boundary and also reply queues in a set must have same
 * upper 32-bits in their memory address. so here driver is allocating the
 * DMA'able memory for reply queues according. Driver uses limitation of
 * VENTURA_SERIES to manage INVADER_SERIES as well.
 */
	dma_alloc_count = DIV_ROUND_UP(msix_count, RDPQ_MAX_INDEX_IN_ONE_CHUNK);

	for (i = 0; i < dma_alloc_count; i++) {
		rdpq_chunk_virt[i] =
			dma_pool_alloc(fusion->reply_frames_desc_pool,
				       GFP_KERNEL, &rdpq_chunk_phys[i]);
		if (!rdpq_chunk_virt[i]) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
		/* reply desc pool requires to be in same 4 gb region.
		 * Below function will check this.
		 * In case of failure, new pci pool will be created with updated
		 * alignment.
		 * For RDPQ buffers, driver always allocate two separate pci pool.
		 * Alignment will be used such a way that next allocation if
		 * success, will always meet same 4gb region requirement.
		 * rdpq_tracker keep track of each buffer's physical,
		 * virtual address and pci pool descriptor. It will help driver
		 * while freeing the resources.
		 *
		 */
		if (!megasas_check_same_4gb_region(instance, rdpq_chunk_phys[i],
						   chunk_size)) {
			dma_pool_free(fusion->reply_frames_desc_pool,
				      rdpq_chunk_virt[i],
				      rdpq_chunk_phys[i]);

			rdpq_chunk_virt[i] =
				dma_pool_alloc(fusion->reply_frames_desc_pool_align,
					       GFP_KERNEL, &rdpq_chunk_phys[i]);
			if (!rdpq_chunk_virt[i]) {
				dev_err(&instance->pdev->dev,
					"Failed from %s %d\n",
					__func__, __LINE__);
				return -ENOMEM;
			}
			fusion->rdpq_tracker[i].dma_pool_ptr =
					fusion->reply_frames_desc_pool_align;
		} else {
			fusion->rdpq_tracker[i].dma_pool_ptr =
					fusion->reply_frames_desc_pool;
		}

		fusion->rdpq_tracker[i].pool_entry_phys = rdpq_chunk_phys[i];
		fusion->rdpq_tracker[i].pool_entry_virt = rdpq_chunk_virt[i];
	}

	for (k = 0; k < dma_alloc_count; k++) {
		for (i = 0; i < RDPQ_MAX_INDEX_IN_ONE_CHUNK; i++) {
			abs_index = (k * RDPQ_MAX_INDEX_IN_ONE_CHUNK) + i;

			if (abs_index == msix_count)
				break;
			offset = fusion->reply_alloc_sz * i;
			fusion->rdpq_virt[abs_index].RDPQBaseAddress =
					cpu_to_le64(rdpq_chunk_phys[k] + offset);
			fusion->reply_frames_desc_phys[abs_index] =
					rdpq_chunk_phys[k] + offset;
			fusion->reply_frames_desc[abs_index] =
					(union MPI2_REPLY_DESCRIPTORS_UNION *)((u8 *)rdpq_chunk_virt[k] + offset);

			reply_desc = fusion->reply_frames_desc[abs_index];
			for (j = 0; j < fusion->reply_q_depth; j++, reply_desc++)
				reply_desc->Words = ULLONG_MAX;
		}
	}

	return 0;
}

static void
megasas_free_rdpq_fusion(struct megasas_instance *instance) {

	int i;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	for (i = 0; i < RDPQ_MAX_CHUNK_COUNT; i++) {
		if (fusion->rdpq_tracker[i].pool_entry_virt)
			dma_pool_free(fusion->rdpq_tracker[i].dma_pool_ptr,
				      fusion->rdpq_tracker[i].pool_entry_virt,
				      fusion->rdpq_tracker[i].pool_entry_phys);

	}

	dma_pool_destroy(fusion->reply_frames_desc_pool);
	dma_pool_destroy(fusion->reply_frames_desc_pool_align);

	if (fusion->rdpq_virt)
		dma_free_coherent(&instance->pdev->dev,
			sizeof(struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY) * MAX_MSIX_QUEUES_FUSION,
			fusion->rdpq_virt, fusion->rdpq_phys);
}

static void
megasas_free_reply_fusion(struct megasas_instance *instance) {

	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (fusion->reply_frames_desc[0])
		dma_pool_free(fusion->reply_frames_desc_pool,
			fusion->reply_frames_desc[0],
			fusion->reply_frames_desc_phys[0]);

	dma_pool_destroy(fusion->reply_frames_desc_pool);

}


/**
 * megasas_alloc_cmds_fusion -	Allocates the command packets
 * @instance:		Adapter soft state
 *
 *
 * Each frame has a 32-bit field called context. This context is used to get
 * back the megasas_cmd_fusion from the frame when a frame gets completed
 * In this driver, the 32 bit values are the indices into an array cmd_list.
 * This array is used only to look up the megasas_cmd_fusion given the context.
 * The free commands themselves are maintained in a linked list called cmd_pool.
 *
 * cmds are formed in the io_request and sg_frame members of the
 * megasas_cmd_fusion. The context field is used to get a request descriptor
 * and is used as SMID of the cmd.
 * SMID value range is from 1 to max_fw_cmds.
 */
static int
megasas_alloc_cmds_fusion(struct megasas_instance *instance)
{
	int i;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;
	u32 offset;
	dma_addr_t io_req_base_phys;
	u8 *io_req_base;


	fusion = instance->ctrl_context;

	if (megasas_alloc_request_fusion(instance))
		goto fail_exit;

	if (instance->is_rdpq) {
		if (megasas_alloc_rdpq_fusion(instance))
			goto fail_exit;
	} else
		if (megasas_alloc_reply_fusion(instance))
			goto fail_exit;

	if (megasas_alloc_cmdlist_fusion(instance))
		goto fail_exit;

	dev_info(&instance->pdev->dev, "Configured max firmware commands: %d\n",
		 instance->max_fw_cmds);

	/* The first 256 bytes (SMID 0) is not used. Don't add to the cmd list */
	io_req_base = fusion->io_request_frames + MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;
	io_req_base_phys = fusion->io_request_frames_phys + MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;

	/*
	 * Add all the commands to command pool (fusion->cmd_pool)
	 */

	/* SMID 0 is reserved. Set SMID/index from 1 */
	for (i = 0; i < instance->max_mpt_cmds; i++) {
		cmd = fusion->cmd_list[i];
		offset = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE * i;
		memset(cmd, 0, sizeof(struct megasas_cmd_fusion));
		cmd->index = i + 1;
		cmd->scmd = NULL;
		cmd->sync_cmd_idx =
		(i >= instance->max_scsi_cmds && i < instance->max_fw_cmds) ?
				(i - instance->max_scsi_cmds) :
				(u32)ULONG_MAX; /* Set to Invalid */
		cmd->instance = instance;
		cmd->io_request =
			(struct MPI2_RAID_SCSI_IO_REQUEST *)
		  (io_req_base + offset);
		memset(cmd->io_request, 0,
		       sizeof(struct MPI2_RAID_SCSI_IO_REQUEST));
		cmd->io_request_phys_addr = io_req_base_phys + offset;
		cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;
	}

	if (megasas_create_sg_sense_fusion(instance))
		goto fail_exit;

	return 0;

fail_exit:
	megasas_free_cmds_fusion(instance);
	return -ENOMEM;
}

/**
 * wait_and_poll -	Issues a polling command
 * @instance:			Adapter soft state
 * @cmd:			Command packet to be issued
 *
 * For polling, MFI requires the cmd_status to be set to 0xFF before posting.
 */
int
wait_and_poll(struct megasas_instance *instance, struct megasas_cmd *cmd,
	int seconds)
{
	int i;
	struct megasas_header *frame_hdr = &cmd->frame->hdr;
	u32 status_reg;

	u32 msecs = seconds * 1000;

	/*
	 * Wait for cmd_status to change
	 */
	for (i = 0; (i < msecs) && (frame_hdr->cmd_status == 0xff); i += 20) {
		rmb();
		msleep(20);
		if (!(i % 5000)) {
			status_reg = instance->instancet->read_fw_status_reg(instance)
					& MFI_STATE_MASK;
			if (status_reg == MFI_STATE_FAULT)
				break;
		}
	}

	if (frame_hdr->cmd_status == MFI_STAT_INVALID_STATUS)
		return DCMD_TIMEOUT;
	else if (frame_hdr->cmd_status == MFI_STAT_OK)
		return DCMD_SUCCESS;
	else
		return DCMD_FAILED;
}

/**
 * megasas_ioc_init_fusion -	Initializes the FW
 * @instance:		Adapter soft state
 *
 * Issues the IOC Init cmd
 */
int
megasas_ioc_init_fusion(struct megasas_instance *instance)
{
	struct megasas_init_frame *init_frame;
	struct MPI2_IOC_INIT_REQUEST *IOCInitMessage = NULL;
	dma_addr_t	ioc_init_handle;
	struct megasas_cmd *cmd;
	u8 ret, cur_rdpq_mode;
	struct fusion_context *fusion;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION req_desc;
	int i;
	struct megasas_header *frame_hdr;
	const char *sys_info;
	MFI_CAPABILITIES *drv_ops;
	u32 scratch_pad_1;
	ktime_t time;
	bool cur_fw_64bit_dma_capable;
	bool cur_intr_coalescing;

	fusion = instance->ctrl_context;

	ioc_init_handle = fusion->ioc_init_request_phys;
	IOCInitMessage = fusion->ioc_init_request;

	cmd = fusion->ioc_init_cmd;

	scratch_pad_1 = megasas_readl
		(instance, &instance->reg_set->outbound_scratch_pad_1);

	cur_rdpq_mode = (scratch_pad_1 & MR_RDPQ_MODE_OFFSET) ? 1 : 0;

	if (instance->adapter_type == INVADER_SERIES) {
		cur_fw_64bit_dma_capable =
			(scratch_pad_1 & MR_CAN_HANDLE_64_BIT_DMA_OFFSET) ? true : false;

		if (instance->consistent_mask_64bit && !cur_fw_64bit_dma_capable) {
			dev_err(&instance->pdev->dev, "Driver was operating on 64bit "
				"DMA mask, but upcoming FW does not support 64bit DMA mask\n");
			megaraid_sas_kill_hba(instance);
			ret = 1;
			goto fail_fw_init;
		}
	}

	if (instance->is_rdpq && !cur_rdpq_mode) {
		dev_err(&instance->pdev->dev, "Firmware downgrade *NOT SUPPORTED*"
			" from RDPQ mode to non RDPQ mode\n");
		ret = 1;
		goto fail_fw_init;
	}

	cur_intr_coalescing = (scratch_pad_1 & MR_INTR_COALESCING_SUPPORT_OFFSET) ?
							true : false;

	if ((instance->low_latency_index_start ==
		MR_HIGH_IOPS_QUEUE_COUNT) && cur_intr_coalescing)
		instance->perf_mode = MR_BALANCED_PERF_MODE;

	dev_info(&instance->pdev->dev, "Performance mode :%s\n",
		MEGASAS_PERF_MODE_2STR(instance->perf_mode));

	instance->fw_sync_cache_support = (scratch_pad_1 &
		MR_CAN_HANDLE_SYNC_CACHE_OFFSET) ? 1 : 0;
	dev_info(&instance->pdev->dev, "FW supports sync cache\t: %s\n",
		 instance->fw_sync_cache_support ? "Yes" : "No");

	memset(IOCInitMessage, 0, sizeof(struct MPI2_IOC_INIT_REQUEST));

	IOCInitMessage->Function = MPI2_FUNCTION_IOC_INIT;
	IOCInitMessage->WhoInit	= MPI2_WHOINIT_HOST_DRIVER;
	IOCInitMessage->MsgVersion = cpu_to_le16(MPI2_VERSION);
	IOCInitMessage->HeaderVersion = cpu_to_le16(MPI2_HEADER_VERSION);
	IOCInitMessage->SystemRequestFrameSize = cpu_to_le16(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE / 4);

	IOCInitMessage->ReplyDescriptorPostQueueDepth = cpu_to_le16(fusion->reply_q_depth);
	IOCInitMessage->ReplyDescriptorPostQueueAddress = instance->is_rdpq ?
			cpu_to_le64(fusion->rdpq_phys) :
			cpu_to_le64(fusion->reply_frames_desc_phys[0]);
	IOCInitMessage->MsgFlags = instance->is_rdpq ?
			MPI2_IOCINIT_MSGFLAG_RDPQ_ARRAY_MODE : 0;
	IOCInitMessage->SystemRequestFrameBaseAddress = cpu_to_le64(fusion->io_request_frames_phys);
	IOCInitMessage->SenseBufferAddressHigh = cpu_to_le32(upper_32_bits(fusion->sense_phys_addr));
	IOCInitMessage->HostMSIxVectors = instance->msix_vectors;
	IOCInitMessage->HostPageSize = MR_DEFAULT_NVME_PAGE_SHIFT;

	time = ktime_get_real();
	/* Convert to milliseconds as per FW requirement */
	IOCInitMessage->TimeStamp = cpu_to_le64(ktime_to_ms(time));

	init_frame = (struct megasas_init_frame *)cmd->frame;
	memset(init_frame, 0, IOC_INIT_FRAME_SIZE);

	frame_hdr = &cmd->frame->hdr;
	frame_hdr->cmd_status = 0xFF;
	frame_hdr->flags |= cpu_to_le16(MFI_FRAME_DONT_POST_IN_REPLY_QUEUE);

	init_frame->cmd	= MFI_CMD_INIT;
	init_frame->cmd_status = 0xFF;

	drv_ops = (MFI_CAPABILITIES *) &(init_frame->driver_operations);

	/* driver support Extended MSIX */
	if (instance->adapter_type >= INVADER_SERIES)
		drv_ops->mfi_capabilities.support_additional_msix = 1;
	/* driver supports HA / Remote LUN over Fast Path interface */
	drv_ops->mfi_capabilities.support_fp_remote_lun = 1;

	drv_ops->mfi_capabilities.support_max_255lds = 1;
	drv_ops->mfi_capabilities.support_ndrive_r1_lb = 1;
	drv_ops->mfi_capabilities.security_protocol_cmds_fw = 1;

	if (instance->max_chain_frame_sz > MEGASAS_CHAIN_FRAME_SZ_MIN)
		drv_ops->mfi_capabilities.support_ext_io_size = 1;

	drv_ops->mfi_capabilities.support_fp_rlbypass = 1;
	if (!dual_qdepth_disable)
		drv_ops->mfi_capabilities.support_ext_queue_depth = 1;

	drv_ops->mfi_capabilities.support_qd_throttling = 1;
	drv_ops->mfi_capabilities.support_pd_map_target_id = 1;
	drv_ops->mfi_capabilities.support_nvme_passthru = 1;
	drv_ops->mfi_capabilities.support_fw_exposed_dev_list = 1;

	if (instance->consistent_mask_64bit)
		drv_ops->mfi_capabilities.support_64bit_mode = 1;

	/* Convert capability to LE32 */
	cpu_to_le32s((u32 *)&init_frame->driver_operations.mfi_capabilities);

	sys_info = dmi_get_system_info(DMI_PRODUCT_UUID);
	if (instance->system_info_buf && sys_info) {
		memcpy(instance->system_info_buf->systemId, sys_info,
			strlen(sys_info) > 64 ? 64 : strlen(sys_info));
		instance->system_info_buf->systemIdLength =
			strlen(sys_info) > 64 ? 64 : strlen(sys_info);
		init_frame->system_info_lo = cpu_to_le32(lower_32_bits(instance->system_info_h));
		init_frame->system_info_hi = cpu_to_le32(upper_32_bits(instance->system_info_h));
	}

	init_frame->queue_info_new_phys_addr_hi =
		cpu_to_le32(upper_32_bits(ioc_init_handle));
	init_frame->queue_info_new_phys_addr_lo =
		cpu_to_le32(lower_32_bits(ioc_init_handle));
	init_frame->data_xfer_len = cpu_to_le32(sizeof(struct MPI2_IOC_INIT_REQUEST));

	/*
	 * Each bit in replyqueue_mask represents one group of MSI-x vectors
	 * (each group has 8 vectors)
	 */
	switch (instance->perf_mode) {
	case MR_BALANCED_PERF_MODE:
		init_frame->replyqueue_mask =
		       cpu_to_le16(~(~0 << instance->low_latency_index_start/8));
		break;
	case MR_IOPS_PERF_MODE:
		init_frame->replyqueue_mask =
		       cpu_to_le16(~(~0 << instance->msix_vectors/8));
		break;
	}


	req_desc.u.low = cpu_to_le32(lower_32_bits(cmd->frame_phys_addr));
	req_desc.u.high = cpu_to_le32(upper_32_bits(cmd->frame_phys_addr));
	req_desc.MFAIo.RequestFlags =
		(MEGASAS_REQ_DESCRIPT_FLAGS_MFA <<
		MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	/*
	 * disable the intr before firing the init frame
	 */
	instance->instancet->disable_intr(instance);

	for (i = 0; i < (10 * 1000); i += 20) {
		if (megasas_readl(instance, &instance->reg_set->doorbell) & 1)
			msleep(20);
		else
			break;
	}

	/* For AERO also, IOC_INIT requires 64 bit descriptor write */
	megasas_write_64bit_req_desc(instance, &req_desc);

	wait_and_poll(instance, cmd, MFI_IO_TIMEOUT_SECS);

	frame_hdr = &cmd->frame->hdr;
	if (frame_hdr->cmd_status != 0) {
		ret = 1;
		goto fail_fw_init;
	}

	if (instance->adapter_type >= AERO_SERIES) {
		scratch_pad_1 = megasas_readl
			(instance, &instance->reg_set->outbound_scratch_pad_1);

		instance->atomic_desc_support =
			(scratch_pad_1 & MR_ATOMIC_DESCRIPTOR_SUPPORT_OFFSET) ? 1 : 0;

		dev_info(&instance->pdev->dev, "FW supports atomic descriptor\t: %s\n",
			instance->atomic_desc_support ? "Yes" : "No");
	}

	return 0;

fail_fw_init:
	dev_err(&instance->pdev->dev,
		"Init cmd return status FAILED for SCSI host %d\n",
		instance->host->host_no);

	return ret;
}

/**
 * megasas_sync_pd_seq_num -	JBOD SEQ MAP
 * @instance:		Adapter soft state
 * @pend:		set to 1, if it is pended jbod map.
 *
 * Issue Jbod map to the firmware. If it is pended command,
 * issue command and return. If it is first instance of jbod map
 * issue and receive command.
 */
int
megasas_sync_pd_seq_num(struct megasas_instance *instance, bool pend) {
	int ret = 0;
	size_t pd_seq_map_sz;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	dma_addr_t pd_seq_h;

	pd_sync = (void *)fusion->pd_seq_sync[(instance->pd_seq_map_id & 1)];
	pd_seq_h = fusion->pd_seq_phys[(instance->pd_seq_map_id & 1)];
	pd_seq_map_sz = struct_size(pd_sync, seq, MAX_PHYSICAL_DEVICES - 1);

	cmd = megasas_get_cmd(instance);
	if (!cmd) {
		dev_err(&instance->pdev->dev,
			"Could not get mfi cmd. Fail from %s %d\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	dcmd = &cmd->frame->dcmd;

	memset(pd_sync, 0, pd_seq_map_sz);
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	if (pend) {
		dcmd->mbox.b[0] = MEGASAS_DCMD_MBOX_PEND_FLAG;
		dcmd->flags = MFI_FRAME_DIR_WRITE;
		instance->jbod_seq_cmd = cmd;
	} else {
		dcmd->flags = MFI_FRAME_DIR_READ;
	}

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(pd_seq_map_sz);
	dcmd->opcode = cpu_to_le32(MR_DCMD_SYSTEM_PD_MAP_GET_INFO);

	megasas_set_dma_settings(instance, dcmd, pd_seq_h, pd_seq_map_sz);

	if (pend) {
		instance->instancet->issue_dcmd(instance, cmd);
		return 0;
	}

	/* Below code is only for non pended DCMD */
	if (!instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd,
			MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	if (le32_to_cpu(pd_sync->count) > MAX_PHYSICAL_DEVICES) {
		dev_warn(&instance->pdev->dev,
			"driver supports max %d JBOD, but FW reports %d\n",
			MAX_PHYSICAL_DEVICES, le32_to_cpu(pd_sync->count));
		ret = -EINVAL;
	}

	if (ret == DCMD_TIMEOUT)
		dev_warn(&instance->pdev->dev,
			 "%s DCMD timed out, continue without JBOD sequence map\n",
			 __func__);

	if (ret == DCMD_SUCCESS)
		instance->pd_seq_map_id++;

	megasas_return_cmd(instance, cmd);
	return ret;
}

/*
 * megasas_get_ld_map_info -	Returns FW's ld_map structure
 * @instance:				Adapter soft state
 * @pend:				Pend the command or not
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 * dcmd.mbox value setting for MR_DCMD_LD_MAP_GET_INFO
 * dcmd.mbox.b[0]	- number of LDs being sync'd
 * dcmd.mbox.b[1]	- 0 - complete command immediately.
 *			- 1 - pend till config change
 * dcmd.mbox.b[2]	- 0 - supports max 64 lds and uses legacy MR_FW_RAID_MAP
 *			- 1 - supports max MAX_LOGICAL_DRIVES_EXT lds and
 *				uses extended struct MR_FW_RAID_MAP_EXT
 */
static int
megasas_get_ld_map_info(struct megasas_instance *instance)
{
	int ret = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	void *ci;
	dma_addr_t ci_h = 0;
	u32 size_map_info;
	struct fusion_context *fusion;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to get cmd for map info\n");
		return -ENOMEM;
	}

	fusion = instance->ctrl_context;

	if (!fusion) {
		megasas_return_cmd(instance, cmd);
		return -ENXIO;
	}

	dcmd = &cmd->frame->dcmd;

	size_map_info = fusion->current_map_sz;

	ci = (void *) fusion->ld_map[(instance->map_id & 1)];
	ci_h = fusion->ld_map_phys[(instance->map_id & 1)];

	if (!ci) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to alloc mem for ld_map_info\n");
		megasas_return_cmd(instance, cmd);
		return -ENOMEM;
	}

	memset(ci, 0, fusion->max_map_sz);
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(size_map_info);
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_MAP_GET_INFO);

	megasas_set_dma_settings(instance, dcmd, ci_h, size_map_info);

	if (!instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd,
			MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	if (ret == DCMD_TIMEOUT)
		dev_warn(&instance->pdev->dev,
			 "%s DCMD timed out, RAID map is disabled\n",
			 __func__);

	megasas_return_cmd(instance, cmd);

	return ret;
}

u8
megasas_get_map_info(struct megasas_instance *instance)
{
	struct fusion_context *fusion = instance->ctrl_context;

	fusion->fast_path_io = 0;
	if (!megasas_get_ld_map_info(instance)) {
		if (MR_ValidateMapInfo(instance, instance->map_id)) {
			fusion->fast_path_io = 1;
			return 0;
		}
	}
	return 1;
}

/*
 * megasas_sync_map_info -	Returns FW's ld_map structure
 * @instance:				Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
int
megasas_sync_map_info(struct megasas_instance *instance)
{
	int i;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	u16 num_lds;
	struct fusion_context *fusion;
	struct MR_LD_TARGET_SYNC *ci = NULL;
	struct MR_DRV_RAID_MAP_ALL *map;
	struct MR_LD_RAID  *raid;
	struct MR_LD_TARGET_SYNC *ld_sync;
	dma_addr_t ci_h = 0;
	u32 size_map_info;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_printk(KERN_DEBUG, &instance->pdev->dev, "Failed to get cmd for sync info\n");
		return -ENOMEM;
	}

	fusion = instance->ctrl_context;

	if (!fusion) {
		megasas_return_cmd(instance, cmd);
		return 1;
	}

	map = fusion->ld_drv_map[instance->map_id & 1];

	num_lds = le16_to_cpu(map->raidMap.ldCount);

	dcmd = &cmd->frame->dcmd;

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	ci = (struct MR_LD_TARGET_SYNC *)
	  fusion->ld_map[(instance->map_id - 1) & 1];
	memset(ci, 0, fusion->max_map_sz);

	ci_h = fusion->ld_map_phys[(instance->map_id - 1) & 1];

	ld_sync = (struct MR_LD_TARGET_SYNC *)ci;

	for (i = 0; i < num_lds; i++, ld_sync++) {
		raid = MR_LdRaidGet(i, map);
		ld_sync->targetId = MR_GetLDTgtId(i, map);
		ld_sync->seqNum = raid->seqNum;
	}

	size_map_info = fusion->current_map_sz;

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_WRITE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(size_map_info);
	dcmd->mbox.b[0] = num_lds;
	dcmd->mbox.b[1] = MEGASAS_DCMD_MBOX_PEND_FLAG;
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_MAP_GET_INFO);

	megasas_set_dma_settings(instance, dcmd, ci_h, size_map_info);

	instance->map_update_cmd = cmd;

	instance->instancet->issue_dcmd(instance, cmd);

	return 0;
}

/*
 * meagasas_display_intel_branding - Display branding string
 * @instance: per adapter object
 *
 * Return nothing.
 */
static void
megasas_display_intel_branding(struct megasas_instance *instance)
{
	if (instance->pdev->subsystem_vendor != PCI_VENDOR_ID_INTEL)
		return;

	switch (instance->pdev->device) {
	case PCI_DEVICE_ID_LSI_INVADER:
		switch (instance->pdev->subsystem_device) {
		case MEGARAID_INTEL_RS3DC080_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3DC080_BRANDING);
			break;
		case MEGARAID_INTEL_RS3DC040_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3DC040_BRANDING);
			break;
		case MEGARAID_INTEL_RS3SC008_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3SC008_BRANDING);
			break;
		case MEGARAID_INTEL_RS3MC044_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3MC044_BRANDING);
			break;
		default:
			break;
		}
		break;
	case PCI_DEVICE_ID_LSI_FURY:
		switch (instance->pdev->subsystem_device) {
		case MEGARAID_INTEL_RS3WC080_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3WC080_BRANDING);
			break;
		case MEGARAID_INTEL_RS3WC040_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RS3WC040_BRANDING);
			break;
		default:
			break;
		}
		break;
	case PCI_DEVICE_ID_LSI_CUTLASS_52:
	case PCI_DEVICE_ID_LSI_CUTLASS_53:
		switch (instance->pdev->subsystem_device) {
		case MEGARAID_INTEL_RMS3BC160_SSDID:
			dev_info(&instance->pdev->dev, "scsi host %d: %s\n",
				instance->host->host_no,
				MEGARAID_INTEL_RMS3BC160_BRANDING);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/**
 * megasas_allocate_raid_maps -	Allocate memory for RAID maps
 * @instance:				Adapter soft state
 *
 * return:				if success: return 0
 *					failed:  return -ENOMEM
 */
static inline int megasas_allocate_raid_maps(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	int i = 0;

	fusion = instance->ctrl_context;

	fusion->drv_map_pages = get_order(fusion->drv_map_sz);

	for (i = 0; i < 2; i++) {
		fusion->ld_map[i] = NULL;

		fusion->ld_drv_map[i] = (void *)
			__get_free_pages(__GFP_ZERO | GFP_KERNEL,
					 fusion->drv_map_pages);

		if (!fusion->ld_drv_map[i]) {
			fusion->ld_drv_map[i] = vzalloc(fusion->drv_map_sz);

			if (!fusion->ld_drv_map[i]) {
				dev_err(&instance->pdev->dev,
					"Could not allocate memory for local map"
					" size requested: %d\n",
					fusion->drv_map_sz);
				goto ld_drv_map_alloc_fail;
			}
		}
	}

	for (i = 0; i < 2; i++) {
		fusion->ld_map[i] = dma_alloc_coherent(&instance->pdev->dev,
						       fusion->max_map_sz,
						       &fusion->ld_map_phys[i],
						       GFP_KERNEL);
		if (!fusion->ld_map[i]) {
			dev_err(&instance->pdev->dev,
				"Could not allocate memory for map info %s:%d\n",
				__func__, __LINE__);
			goto ld_map_alloc_fail;
		}
	}

	return 0;

ld_map_alloc_fail:
	for (i = 0; i < 2; i++) {
		if (fusion->ld_map[i])
			dma_free_coherent(&instance->pdev->dev,
					  fusion->max_map_sz,
					  fusion->ld_map[i],
					  fusion->ld_map_phys[i]);
	}

ld_drv_map_alloc_fail:
	for (i = 0; i < 2; i++) {
		if (fusion->ld_drv_map[i]) {
			if (is_vmalloc_addr(fusion->ld_drv_map[i]))
				vfree(fusion->ld_drv_map[i]);
			else
				free_pages((ulong)fusion->ld_drv_map[i],
					   fusion->drv_map_pages);
		}
	}

	return -ENOMEM;
}

/**
 * megasas_configure_queue_sizes -	Calculate size of request desc queue,
 *					reply desc queue,
 *					IO request frame queue, set can_queue.
 * @instance:				Adapter soft state
 * @return:				void
 */
static inline
void megasas_configure_queue_sizes(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	u16 max_cmd;

	fusion = instance->ctrl_context;
	max_cmd = instance->max_fw_cmds;

	if (instance->adapter_type >= VENTURA_SERIES)
		instance->max_mpt_cmds = instance->max_fw_cmds * RAID_1_PEER_CMDS;
	else
		instance->max_mpt_cmds = instance->max_fw_cmds;

	instance->max_scsi_cmds = instance->max_fw_cmds - instance->max_mfi_cmds;
	instance->cur_can_queue = instance->max_scsi_cmds;
	instance->host->can_queue = instance->cur_can_queue;

	fusion->reply_q_depth = 2 * ((max_cmd + 1 + 15) / 16) * 16;

	fusion->request_alloc_sz = sizeof(union MEGASAS_REQUEST_DESCRIPTOR_UNION) *
					  instance->max_mpt_cmds;
	fusion->reply_alloc_sz = sizeof(union MPI2_REPLY_DESCRIPTORS_UNION) *
					(fusion->reply_q_depth);
	fusion->io_frames_alloc_sz = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE +
		(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE
		 * (instance->max_mpt_cmds + 1)); /* Extra 1 for SMID 0 */
}

static int megasas_alloc_ioc_init_frame(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	struct megasas_cmd *cmd;

	fusion = instance->ctrl_context;

	cmd = kzalloc(sizeof(struct megasas_cmd), GFP_KERNEL);

	if (!cmd) {
		dev_err(&instance->pdev->dev, "Failed from func: %s line: %d\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	cmd->frame = dma_alloc_coherent(&instance->pdev->dev,
					IOC_INIT_FRAME_SIZE,
					&cmd->frame_phys_addr, GFP_KERNEL);

	if (!cmd->frame) {
		dev_err(&instance->pdev->dev, "Failed from func: %s line: %d\n",
			__func__, __LINE__);
		kfree(cmd);
		return -ENOMEM;
	}

	fusion->ioc_init_cmd = cmd;
	return 0;
}

/**
 * megasas_free_ioc_init_cmd -	Free IOC INIT command frame
 * @instance:		Adapter soft state
 */
static inline void megasas_free_ioc_init_cmd(struct megasas_instance *instance)
{
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (fusion->ioc_init_cmd && fusion->ioc_init_cmd->frame)
		dma_free_coherent(&instance->pdev->dev,
				  IOC_INIT_FRAME_SIZE,
				  fusion->ioc_init_cmd->frame,
				  fusion->ioc_init_cmd->frame_phys_addr);

	kfree(fusion->ioc_init_cmd);
}

/**
 * megasas_init_adapter_fusion -	Initializes the FW
 * @instance:		Adapter soft state
 *
 * This is the main function for initializing firmware.
 */
static u32
megasas_init_adapter_fusion(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	u32 scratch_pad_1;
	int i = 0, count;
	u32 status_reg;

	fusion = instance->ctrl_context;

	megasas_fusion_update_can_queue(instance, PROBE_CONTEXT);

	/*
	 * Only Driver's internal DCMDs and IOCTL DCMDs needs to have MFI frames
	 */
	instance->max_mfi_cmds =
		MEGASAS_FUSION_INTERNAL_CMDS + MEGASAS_FUSION_IOCTL_CMDS;

	megasas_configure_queue_sizes(instance);

	scratch_pad_1 = megasas_readl(instance,
				      &instance->reg_set->outbound_scratch_pad_1);
	/* If scratch_pad_1 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK is set,
	 * Firmware support extended IO chain frame which is 4 times more than
	 * legacy Firmware.
	 * Legacy Firmware - Frame size is (8 * 128) = 1K
	 * 1M IO Firmware  - Frame size is (8 * 128 * 4)  = 4K
	 */
	if (scratch_pad_1 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK)
		instance->max_chain_frame_sz =
			((scratch_pad_1 & MEGASAS_MAX_CHAIN_SIZE_MASK) >>
			MEGASAS_MAX_CHAIN_SHIFT) * MEGASAS_1MB_IO;
	else
		instance->max_chain_frame_sz =
			((scratch_pad_1 & MEGASAS_MAX_CHAIN_SIZE_MASK) >>
			MEGASAS_MAX_CHAIN_SHIFT) * MEGASAS_256K_IO;

	if (instance->max_chain_frame_sz < MEGASAS_CHAIN_FRAME_SZ_MIN) {
		dev_warn(&instance->pdev->dev, "frame size %d invalid, fall back to legacy max frame size %d\n",
			instance->max_chain_frame_sz,
			MEGASAS_CHAIN_FRAME_SZ_MIN);
		instance->max_chain_frame_sz = MEGASAS_CHAIN_FRAME_SZ_MIN;
	}

	fusion->max_sge_in_main_msg =
		(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE
			- offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL))/16;

	fusion->max_sge_in_chain =
		instance->max_chain_frame_sz
			/ sizeof(union MPI2_SGE_IO_UNION);

	instance->max_num_sge =
		rounddown_pow_of_two(fusion->max_sge_in_main_msg
			+ fusion->max_sge_in_chain - 2);

	/* Used for pass thru MFI frame (DCMD) */
	fusion->chain_offset_mfi_pthru =
		offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL)/16;

	fusion->chain_offset_io_request =
		(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE -
		 sizeof(union MPI2_SGE_IO_UNION))/16;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	for (i = 0 ; i < count; i++)
		fusion->last_reply_idx[i] = 0;

	/*
	 * For fusion adapters, 3 commands for IOCTL and 8 commands
	 * for driver's internal DCMDs.
	 */
	instance->max_scsi_cmds = instance->max_fw_cmds -
				(MEGASAS_FUSION_INTERNAL_CMDS +
				MEGASAS_FUSION_IOCTL_CMDS);
	sema_init(&instance->ioctl_sem, MEGASAS_FUSION_IOCTL_CMDS);

	if (megasas_alloc_ioc_init_frame(instance))
		return 1;

	/*
	 * Allocate memory for descriptors
	 * Create a pool of commands
	 */
	if (megasas_alloc_cmds(instance))
		goto fail_alloc_mfi_cmds;
	if (megasas_alloc_cmds_fusion(instance))
		goto fail_alloc_cmds;

	if (megasas_ioc_init_fusion(instance)) {
		status_reg = instance->instancet->read_fw_status_reg(instance);
		if (((status_reg & MFI_STATE_MASK) == MFI_STATE_FAULT) &&
		    (status_reg & MFI_RESET_ADAPTER)) {
			/* Do a chip reset and then retry IOC INIT once */
			if (megasas_adp_reset_wait_for_ready
				(instance, true, 0) == FAILED)
				goto fail_ioc_init;

			if (megasas_ioc_init_fusion(instance))
				goto fail_ioc_init;
		} else {
			goto fail_ioc_init;
		}
	}

	megasas_display_intel_branding(instance);
	if (megasas_get_ctrl_info(instance)) {
		dev_err(&instance->pdev->dev,
			"Could not get controller info. Fail from %s %d\n",
			__func__, __LINE__);
		goto fail_ioc_init;
	}

	instance->flag_ieee = 1;
	instance->r1_ldio_hint_default =  MR_R1_LDIO_PIGGYBACK_DEFAULT;
	instance->threshold_reply_count = instance->max_fw_cmds / 4;
	fusion->fast_path_io = 0;

	if (megasas_allocate_raid_maps(instance))
		goto fail_ioc_init;

	if (!megasas_get_map_info(instance))
		megasas_sync_map_info(instance);

	return 0;

fail_ioc_init:
	megasas_free_cmds_fusion(instance);
fail_alloc_cmds:
	megasas_free_cmds(instance);
fail_alloc_mfi_cmds:
	megasas_free_ioc_init_cmd(instance);
	return 1;
}

/**
 * megasas_fault_detect_work	-	Worker function of
 *					FW fault handling workqueue.
 */
static void
megasas_fault_detect_work(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance,
			     fw_fault_work.work);
	u32 fw_state, dma_state, status;

	/* Check the fw state */
	fw_state = instance->instancet->read_fw_status_reg(instance) &
			MFI_STATE_MASK;

	if (fw_state == MFI_STATE_FAULT) {
		dma_state = instance->instancet->read_fw_status_reg(instance) &
				MFI_STATE_DMADONE;
		/* Start collecting crash, if DMA bit is done */
		if (instance->crash_dump_drv_support &&
		    instance->crash_dump_app_support && dma_state) {
			megasas_fusion_crash_dump(instance);
		} else {
			if (instance->unload == 0) {
				status = megasas_reset_fusion(instance->host, 0);
				if (status != SUCCESS) {
					dev_err(&instance->pdev->dev,
						"Failed from %s %d, do not re-arm timer\n",
						__func__, __LINE__);
					return;
				}
			}
		}
	}

	if (instance->fw_fault_work_q)
		queue_delayed_work(instance->fw_fault_work_q,
			&instance->fw_fault_work,
			msecs_to_jiffies(MEGASAS_WATCHDOG_THREAD_INTERVAL));
}

int
megasas_fusion_start_watchdog(struct megasas_instance *instance)
{
	/* Check if the Fault WQ is already started */
	if (instance->fw_fault_work_q)
		return SUCCESS;

	INIT_DELAYED_WORK(&instance->fw_fault_work, megasas_fault_detect_work);

	snprintf(instance->fault_handler_work_q_name,
		 sizeof(instance->fault_handler_work_q_name),
		 "poll_megasas%d_status", instance->host->host_no);

	instance->fw_fault_work_q =
		create_singlethread_workqueue(instance->fault_handler_work_q_name);
	if (!instance->fw_fault_work_q) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return FAILED;
	}

	queue_delayed_work(instance->fw_fault_work_q,
			   &instance->fw_fault_work,
			   msecs_to_jiffies(MEGASAS_WATCHDOG_THREAD_INTERVAL));

	return SUCCESS;
}

void
megasas_fusion_stop_watchdog(struct megasas_instance *instance)
{
	struct workqueue_struct *wq;

	if (instance->fw_fault_work_q) {
		wq = instance->fw_fault_work_q;
		instance->fw_fault_work_q = NULL;
		if (!cancel_delayed_work_sync(&instance->fw_fault_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * map_cmd_status -	Maps FW cmd status to OS cmd status
 * @cmd :		Pointer to cmd
 * @status :		status of cmd returned by FW
 * @ext_status :	ext status of cmd returned by FW
 */

static void
map_cmd_status(struct fusion_context *fusion,
		struct scsi_cmnd *scmd, u8 status, u8 ext_status,
		u32 data_length, u8 *sense)
{
	u8 cmd_type;
	int resid;

	cmd_type = megasas_cmd_type(scmd);
	switch (status) {

	case MFI_STAT_OK:
		scmd->result = DID_OK << 16;
		break;

	case MFI_STAT_SCSI_IO_FAILED:
	case MFI_STAT_LD_INIT_IN_PROGRESS:
		scmd->result = (DID_ERROR << 16) | ext_status;
		break;

	case MFI_STAT_SCSI_DONE_WITH_ERROR:

		scmd->result = (DID_OK << 16) | ext_status;
		if (ext_status == SAM_STAT_CHECK_CONDITION) {
			memset(scmd->sense_buffer, 0,
			       SCSI_SENSE_BUFFERSIZE);
			memcpy(scmd->sense_buffer, sense,
			       SCSI_SENSE_BUFFERSIZE);
			scmd->result |= DRIVER_SENSE << 24;
		}

		/*
		 * If the  IO request is partially completed, then MR FW will
		 * update "io_request->DataLength" field with actual number of
		 * bytes transferred.Driver will set residual bytes count in
		 * SCSI command structure.
		 */
		resid = (scsi_bufflen(scmd) - data_length);
		scsi_set_resid(scmd, resid);

		if (resid &&
			((cmd_type == READ_WRITE_LDIO) ||
			(cmd_type == READ_WRITE_SYSPDIO)))
			scmd_printk(KERN_INFO, scmd, "BRCM Debug mfi stat 0x%x, data len"
				" requested/completed 0x%x/0x%x\n",
				status, scsi_bufflen(scmd), data_length);
		break;

	case MFI_STAT_LD_OFFLINE:
	case MFI_STAT_DEVICE_NOT_FOUND:
		scmd->result = DID_BAD_TARGET << 16;
		break;
	case MFI_STAT_CONFIG_SEQ_MISMATCH:
		scmd->result = DID_IMM_RETRY << 16;
		break;
	default:
		scmd->result = DID_ERROR << 16;
		break;
	}
}

/**
 * megasas_is_prp_possible -
 * Checks if native NVMe PRPs can be built for the IO
 *
 * @instance:		Adapter soft state
 * @scmd:		SCSI command from the mid-layer
 * @sge_count:		scatter gather element count.
 *
 * Returns:		true: PRPs can be built
 *			false: IEEE SGLs needs to be built
 */
static bool
megasas_is_prp_possible(struct megasas_instance *instance,
			struct scsi_cmnd *scmd, int sge_count)
{
	int i;
	u32 data_length = 0;
	struct scatterlist *sg_scmd;
	bool build_prp = false;
	u32 mr_nvme_pg_size;

	mr_nvme_pg_size = max_t(u32, instance->nvme_page_size,
				MR_DEFAULT_NVME_PAGE_SIZE);
	data_length = scsi_bufflen(scmd);
	sg_scmd = scsi_sglist(scmd);

	/*
	 * NVMe uses one PRP for each page (or part of a page)
	 * look at the data length - if 4 pages or less then IEEE is OK
	 * if  > 5 pages then we need to build a native SGL
	 * if > 4 and <= 5 pages, then check physical address of 1st SG entry
	 * if this first size in the page is >= the residual beyond 4 pages
	 * then use IEEE, otherwise use native SGL
	 */

	if (data_length > (mr_nvme_pg_size * 5)) {
		build_prp = true;
	} else if ((data_length > (mr_nvme_pg_size * 4)) &&
			(data_length <= (mr_nvme_pg_size * 5)))  {
		/* check if 1st SG entry size is < residual beyond 4 pages */
		if (sg_dma_len(sg_scmd) < (data_length - (mr_nvme_pg_size * 4)))
			build_prp = true;
	}

/*
 * Below code detects gaps/holes in IO data buffers.
 * What does holes/gaps mean?
 * Any SGE except first one in a SGL starts at non NVME page size
 * aligned address OR Any SGE except last one in a SGL ends at
 * non NVME page size boundary.
 *
 * Driver has already informed block layer by setting boundary rules for
 * bio merging done at NVME page size boundary calling kernel API
 * blk_queue_virt_boundary inside slave_config.
 * Still there is possibility of IO coming with holes to driver because of
 * IO merging done by IO scheduler.
 *
 * With SCSI BLK MQ enabled, there will be no IO with holes as there is no
 * IO scheduling so no IO merging.
 *
 * With SCSI BLK MQ disabled, IO scheduler may attempt to merge IOs and
 * then sending IOs with holes.
 *
 * Though driver can request block layer to disable IO merging by calling-
 * blk_queue_flag_set(QUEUE_FLAG_NOMERGES, sdev->request_queue) but
 * user may tune sysfs parameter- nomerges again to 0 or 1.
 *
 * If in future IO scheduling is enabled with SCSI BLK MQ,
 * this algorithm to detect holes will be required in driver
 * for SCSI BLK MQ enabled case as well.
 *
 *
 */
	scsi_for_each_sg(scmd, sg_scmd, sge_count, i) {
		if ((i != 0) && (i != (sge_count - 1))) {
			if (mega_mod64(sg_dma_len(sg_scmd), mr_nvme_pg_size) ||
			    mega_mod64(sg_dma_address(sg_scmd),
				       mr_nvme_pg_size)) {
				build_prp = false;
				break;
			}
		}

		if ((sge_count > 1) && (i == 0)) {
			if ((mega_mod64((sg_dma_address(sg_scmd) +
					sg_dma_len(sg_scmd)),
					mr_nvme_pg_size))) {
				build_prp = false;
				break;
			}
		}

		if ((sge_count > 1) && (i == (sge_count - 1))) {
			if (mega_mod64(sg_dma_address(sg_scmd),
				       mr_nvme_pg_size)) {
				build_prp = false;
				break;
			}
		}
	}

	return build_prp;
}

/**
 * megasas_make_prp_nvme -
 * Prepare PRPs(Physical Region Page)- SGLs specific to NVMe drives only
 *
 * @instance:		Adapter soft state
 * @scmd:		SCSI command from the mid-layer
 * @sgl_ptr:		SGL to be filled in
 * @cmd:		Fusion command frame
 * @sge_count:		scatter gather element count.
 *
 * Returns:		true: PRPs are built
 *			false: IEEE SGLs needs to be built
 */
static bool
megasas_make_prp_nvme(struct megasas_instance *instance, struct scsi_cmnd *scmd,
		      struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr,
		      struct megasas_cmd_fusion *cmd, int sge_count)
{
	int sge_len, offset, num_prp_in_chain = 0;
	struct MPI25_IEEE_SGE_CHAIN64 *main_chain_element, *ptr_first_sgl;
	u64 *ptr_sgl;
	dma_addr_t ptr_sgl_phys;
	u64 sge_addr;
	u32 page_mask, page_mask_result;
	struct scatterlist *sg_scmd;
	u32 first_prp_len;
	bool build_prp = false;
	int data_len = scsi_bufflen(scmd);
	u32 mr_nvme_pg_size = max_t(u32, instance->nvme_page_size,
					MR_DEFAULT_NVME_PAGE_SIZE);

	build_prp = megasas_is_prp_possible(instance, scmd, sge_count);

	if (!build_prp)
		return false;

	/*
	 * Nvme has a very convoluted prp format.  One prp is required
	 * for each page or partial page. Driver need to split up OS sg_list
	 * entries if it is longer than one page or cross a page
	 * boundary.  Driver also have to insert a PRP list pointer entry as
	 * the last entry in each physical page of the PRP list.
	 *
	 * NOTE: The first PRP "entry" is actually placed in the first
	 * SGL entry in the main message as IEEE 64 format.  The 2nd
	 * entry in the main message is the chain element, and the rest
	 * of the PRP entries are built in the contiguous pcie buffer.
	 */
	page_mask = mr_nvme_pg_size - 1;
	ptr_sgl = (u64 *)cmd->sg_frame;
	ptr_sgl_phys = cmd->sg_frame_phys_addr;
	memset(ptr_sgl, 0, instance->max_chain_frame_sz);

	/* Build chain frame element which holds all prps except first*/
	main_chain_element = (struct MPI25_IEEE_SGE_CHAIN64 *)
	    ((u8 *)sgl_ptr + sizeof(struct MPI25_IEEE_SGE_CHAIN64));

	main_chain_element->Address = cpu_to_le64(ptr_sgl_phys);
	main_chain_element->NextChainOffset = 0;
	main_chain_element->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT |
					IEEE_SGE_FLAGS_SYSTEM_ADDR |
					MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP;

	/* Build first prp, sge need not to be page aligned*/
	ptr_first_sgl = sgl_ptr;
	sg_scmd = scsi_sglist(scmd);
	sge_addr = sg_dma_address(sg_scmd);
	sge_len = sg_dma_len(sg_scmd);

	offset = (u32)(sge_addr & page_mask);
	first_prp_len = mr_nvme_pg_size - offset;

	ptr_first_sgl->Address = cpu_to_le64(sge_addr);
	ptr_first_sgl->Length = cpu_to_le32(first_prp_len);

	data_len -= first_prp_len;

	if (sge_len > first_prp_len) {
		sge_addr += first_prp_len;
		sge_len -= first_prp_len;
	} else if (sge_len == first_prp_len) {
		sg_scmd = sg_next(sg_scmd);
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	for (;;) {
		offset = (u32)(sge_addr & page_mask);

		/* Put PRP pointer due to page boundary*/
		page_mask_result = (uintptr_t)(ptr_sgl + 1) & page_mask;
		if (unlikely(!page_mask_result)) {
			scmd_printk(KERN_NOTICE,
				    scmd, "page boundary ptr_sgl: 0x%p\n",
				    ptr_sgl);
			ptr_sgl_phys += 8;
			*ptr_sgl = cpu_to_le64(ptr_sgl_phys);
			ptr_sgl++;
			num_prp_in_chain++;
		}

		*ptr_sgl = cpu_to_le64(sge_addr);
		ptr_sgl++;
		ptr_sgl_phys += 8;
		num_prp_in_chain++;

		sge_addr += mr_nvme_pg_size;
		sge_len -= mr_nvme_pg_size;
		data_len -= mr_nvme_pg_size;

		if (data_len <= 0)
			break;

		if (sge_len > 0)
			continue;

		sg_scmd = sg_next(sg_scmd);
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	main_chain_element->Length =
			cpu_to_le32(num_prp_in_chain * sizeof(u64));

	return build_prp;
}

/**
 * megasas_make_sgl_fusion -	Prepares 32-bit SGL
 * @instance:		Adapter soft state
 * @scp:		SCSI command from the mid-layer
 * @sgl_ptr:		SGL to be filled in
 * @cmd:		cmd we are working on
 * @sge_count		sge count
 *
 */
static void
megasas_make_sgl_fusion(struct megasas_instance *instance,
			struct scsi_cmnd *scp,
			struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr,
			struct megasas_cmd_fusion *cmd, int sge_count)
{
	int i, sg_processed;
	struct scatterlist *os_sgl;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (instance->adapter_type >= INVADER_SERIES) {
		struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr_end = sgl_ptr;
		sgl_ptr_end += fusion->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}

	scsi_for_each_sg(scp, os_sgl, sge_count, i) {
		sgl_ptr->Length = cpu_to_le32(sg_dma_len(os_sgl));
		sgl_ptr->Address = cpu_to_le64(sg_dma_address(os_sgl));
		sgl_ptr->Flags = 0;
		if (instance->adapter_type >= INVADER_SERIES)
			if (i == sge_count - 1)
				sgl_ptr->Flags = IEEE_SGE_FLAGS_END_OF_LIST;
		sgl_ptr++;
		sg_processed = i + 1;

		if ((sg_processed ==  (fusion->max_sge_in_main_msg - 1)) &&
		    (sge_count > fusion->max_sge_in_main_msg)) {

			struct MPI25_IEEE_SGE_CHAIN64 *sg_chain;
			if (instance->adapter_type >= INVADER_SERIES) {
				if ((le16_to_cpu(cmd->io_request->IoFlags) &
					MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH) !=
					MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH)
					cmd->io_request->ChainOffset =
						fusion->
						chain_offset_io_request;
				else
					cmd->io_request->ChainOffset = 0;
			} else
				cmd->io_request->ChainOffset =
					fusion->chain_offset_io_request;

			sg_chain = sgl_ptr;
			/* Prepare chain element */
			sg_chain->NextChainOffset = 0;
			if (instance->adapter_type >= INVADER_SERIES)
				sg_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT;
			else
				sg_chain->Flags =
					(IEEE_SGE_FLAGS_CHAIN_ELEMENT |
					 MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR);
			sg_chain->Length =  cpu_to_le32((sizeof(union MPI2_SGE_IO_UNION) * (sge_count - sg_processed)));
			sg_chain->Address = cpu_to_le64(cmd->sg_frame_phys_addr);

			sgl_ptr =
			  (struct MPI25_IEEE_SGE_CHAIN64 *)cmd->sg_frame;
			memset(sgl_ptr, 0, instance->max_chain_frame_sz);
		}
	}
}

/**
 * megasas_make_sgl -	Build Scatter Gather List(SGLs)
 * @scp:		SCSI command pointer
 * @instance:		Soft instance of controller
 * @cmd:		Fusion command pointer
 *
 * This function will build sgls based on device type.
 * For nvme drives, there is different way of building sgls in nvme native
 * format- PRPs(Physical Region Page).
 *
 * Returns the number of sg lists actually used, zero if the sg lists
 * is NULL, or -ENOMEM if the mapping failed
 */
static
int megasas_make_sgl(struct megasas_instance *instance, struct scsi_cmnd *scp,
		     struct megasas_cmd_fusion *cmd)
{
	int sge_count;
	bool build_prp = false;
	struct MPI25_IEEE_SGE_CHAIN64 *sgl_chain64;

	sge_count = scsi_dma_map(scp);

	if ((sge_count > instance->max_num_sge) || (sge_count <= 0))
		return sge_count;

	sgl_chain64 = (struct MPI25_IEEE_SGE_CHAIN64 *)&cmd->io_request->SGL;
	if ((le16_to_cpu(cmd->io_request->IoFlags) &
	    MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH) &&
	    (cmd->pd_interface == NVME_PD))
		build_prp = megasas_make_prp_nvme(instance, scp, sgl_chain64,
						  cmd, sge_count);

	if (!build_prp)
		megasas_make_sgl_fusion(instance, scp, sgl_chain64,
					cmd, sge_count);

	return sge_count;
}

/**
 * megasas_set_pd_lba -	Sets PD LBA
 * @cdb:		CDB
 * @cdb_len:		cdb length
 * @start_blk:		Start block of IO
 *
 * Used to set the PD LBA in CDB for FP IOs
 */
static void
megasas_set_pd_lba(struct MPI2_RAID_SCSI_IO_REQUEST *io_request, u8 cdb_len,
		   struct IO_REQUEST_INFO *io_info, struct scsi_cmnd *scp,
		   struct MR_DRV_RAID_MAP_ALL *local_map_ptr, u32 ref_tag)
{
	struct MR_LD_RAID *raid;
	u16 ld;
	u64 start_blk = io_info->pdBlock;
	u8 *cdb = io_request->CDB.CDB32;
	u32 num_blocks = io_info->numBlocks;
	u8 opcode = 0, flagvals = 0, groupnum = 0, control = 0;

	/* Check if T10 PI (DIF) is enabled for this LD */
	ld = MR_TargetIdToLdGet(io_info->ldTgtId, local_map_ptr);
	raid = MR_LdRaidGet(ld, local_map_ptr);
	if (raid->capability.ldPiMode == MR_PROT_INFO_TYPE_CONTROLLER) {
		memset(cdb, 0, sizeof(io_request->CDB.CDB32));
		cdb[0] =  MEGASAS_SCSI_VARIABLE_LENGTH_CMD;
		cdb[7] =  MEGASAS_SCSI_ADDL_CDB_LEN;

		if (scp->sc_data_direction == DMA_FROM_DEVICE)
			cdb[9] = MEGASAS_SCSI_SERVICE_ACTION_READ32;
		else
			cdb[9] = MEGASAS_SCSI_SERVICE_ACTION_WRITE32;
		cdb[10] = MEGASAS_RD_WR_PROTECT_CHECK_ALL;

		/* LBA */
		cdb[12] = (u8)((start_blk >> 56) & 0xff);
		cdb[13] = (u8)((start_blk >> 48) & 0xff);
		cdb[14] = (u8)((start_blk >> 40) & 0xff);
		cdb[15] = (u8)((start_blk >> 32) & 0xff);
		cdb[16] = (u8)((start_blk >> 24) & 0xff);
		cdb[17] = (u8)((start_blk >> 16) & 0xff);
		cdb[18] = (u8)((start_blk >> 8) & 0xff);
		cdb[19] = (u8)(start_blk & 0xff);

		/* Logical block reference tag */
		io_request->CDB.EEDP32.PrimaryReferenceTag =
			cpu_to_be32(ref_tag);
		io_request->CDB.EEDP32.PrimaryApplicationTagMask = cpu_to_be16(0xffff);
		io_request->IoFlags = cpu_to_le16(32); /* Specify 32-byte cdb */

		/* Transfer length */
		cdb[28] = (u8)((num_blocks >> 24) & 0xff);
		cdb[29] = (u8)((num_blocks >> 16) & 0xff);
		cdb[30] = (u8)((num_blocks >> 8) & 0xff);
		cdb[31] = (u8)(num_blocks & 0xff);

		/* set SCSI IO EEDPFlags */
		if (scp->sc_data_direction == DMA_FROM_DEVICE) {
			io_request->EEDPFlags = cpu_to_le16(
				MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG  |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG |
				MPI25_SCSIIO_EEDPFLAGS_DO_NOT_DISABLE_MODE |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD);
		} else {
			io_request->EEDPFlags = cpu_to_le16(
				MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
				MPI2_SCSIIO_EEDPFLAGS_INSERT_OP);
		}
		io_request->Control |= cpu_to_le32((0x4 << 26));
		io_request->EEDPBlockSize = cpu_to_le32(scp->device->sector_size);
	} else {
		/* Some drives don't support 16/12 byte CDB's, convert to 10 */
		if (((cdb_len == 12) || (cdb_len == 16)) &&
		    (start_blk <= 0xffffffff)) {
			if (cdb_len == 16) {
				opcode = cdb[0] == READ_16 ? READ_10 : WRITE_10;
				flagvals = cdb[1];
				groupnum = cdb[14];
				control = cdb[15];
			} else {
				opcode = cdb[0] == READ_12 ? READ_10 : WRITE_10;
				flagvals = cdb[1];
				groupnum = cdb[10];
				control = cdb[11];
			}

			memset(cdb, 0, sizeof(io_request->CDB.CDB32));

			cdb[0] = opcode;
			cdb[1] = flagvals;
			cdb[6] = groupnum;
			cdb[9] = control;

			/* Transfer length */
			cdb[8] = (u8)(num_blocks & 0xff);
			cdb[7] = (u8)((num_blocks >> 8) & 0xff);

			io_request->IoFlags = cpu_to_le16(10); /* Specify 10-byte cdb */
			cdb_len = 10;
		} else if ((cdb_len < 16) && (start_blk > 0xffffffff)) {
			/* Convert to 16 byte CDB for large LBA's */
			switch (cdb_len) {
			case 6:
				opcode = cdb[0] == READ_6 ? READ_16 : WRITE_16;
				control = cdb[5];
				break;
			case 10:
				opcode =
					cdb[0] == READ_10 ? READ_16 : WRITE_16;
				flagvals = cdb[1];
				groupnum = cdb[6];
				control = cdb[9];
				break;
			case 12:
				opcode =
					cdb[0] == READ_12 ? READ_16 : WRITE_16;
				flagvals = cdb[1];
				groupnum = cdb[10];
				control = cdb[11];
				break;
			}

			memset(cdb, 0, sizeof(io_request->CDB.CDB32));

			cdb[0] = opcode;
			cdb[1] = flagvals;
			cdb[14] = groupnum;
			cdb[15] = control;

			/* Transfer length */
			cdb[13] = (u8)(num_blocks & 0xff);
			cdb[12] = (u8)((num_blocks >> 8) & 0xff);
			cdb[11] = (u8)((num_blocks >> 16) & 0xff);
			cdb[10] = (u8)((num_blocks >> 24) & 0xff);

			io_request->IoFlags = cpu_to_le16(16); /* Specify 16-byte cdb */
			cdb_len = 16;
		}

		/* Normal case, just load LBA here */
		switch (cdb_len) {
		case 6:
		{
			u8 val = cdb[1] & 0xE0;
			cdb[3] = (u8)(start_blk & 0xff);
			cdb[2] = (u8)((start_blk >> 8) & 0xff);
			cdb[1] = val | ((u8)(start_blk >> 16) & 0x1f);
			break;
		}
		case 10:
			cdb[5] = (u8)(start_blk & 0xff);
			cdb[4] = (u8)((start_blk >> 8) & 0xff);
			cdb[3] = (u8)((start_blk >> 16) & 0xff);
			cdb[2] = (u8)((start_blk >> 24) & 0xff);
			break;
		case 12:
			cdb[5]    = (u8)(start_blk & 0xff);
			cdb[4]    = (u8)((start_blk >> 8) & 0xff);
			cdb[3]    = (u8)((start_blk >> 16) & 0xff);
			cdb[2]    = (u8)((start_blk >> 24) & 0xff);
			break;
		case 16:
			cdb[9]    = (u8)(start_blk & 0xff);
			cdb[8]    = (u8)((start_blk >> 8) & 0xff);
			cdb[7]    = (u8)((start_blk >> 16) & 0xff);
			cdb[6]    = (u8)((start_blk >> 24) & 0xff);
			cdb[5]    = (u8)((start_blk >> 32) & 0xff);
			cdb[4]    = (u8)((start_blk >> 40) & 0xff);
			cdb[3]    = (u8)((start_blk >> 48) & 0xff);
			cdb[2]    = (u8)((start_blk >> 56) & 0xff);
			break;
		}
	}
}

/**
 * megasas_stream_detect -	stream detection on read and and write IOs
 * @instance:		Adapter soft state
 * @cmd:		    Command to be prepared
 * @io_info:		IO Request info
 *
 */

/** stream detection on read and and write IOs */
static void megasas_stream_detect(struct megasas_instance *instance,
				  struct megasas_cmd_fusion *cmd,
				  struct IO_REQUEST_INFO *io_info)
{
	struct fusion_context *fusion = instance->ctrl_context;
	u32 device_id = io_info->ldTgtId;
	struct LD_STREAM_DETECT *current_ld_sd
		= fusion->stream_detect_by_ld[device_id];
	u32 *track_stream = &current_ld_sd->mru_bit_map, stream_num;
	u32 shifted_values, unshifted_values;
	u32 index_value_mask, shifted_values_mask;
	int i;
	bool is_read_ahead = false;
	struct STREAM_DETECT *current_sd;
	/* find possible stream */
	for (i = 0; i < MAX_STREAMS_TRACKED; ++i) {
		stream_num = (*track_stream >>
			(i * BITS_PER_INDEX_STREAM)) &
			STREAM_MASK;
		current_sd = &current_ld_sd->stream_track[stream_num];
		/* if we found a stream, update the raid
		 *  context and also update the mruBitMap
		 */
		/*	boundary condition */
		if ((current_sd->next_seq_lba) &&
		    (io_info->ldStartBlock >= current_sd->next_seq_lba) &&
		    (io_info->ldStartBlock <= (current_sd->next_seq_lba + 32)) &&
		    (current_sd->is_read == io_info->isRead)) {

			if ((io_info->ldStartBlock != current_sd->next_seq_lba)	&&
			    ((!io_info->isRead) || (!is_read_ahead)))
				/*
				 * Once the API availible we need to change this.
				 * At this point we are not allowing any gap
				 */
				continue;

			SET_STREAM_DETECTED(cmd->io_request->RaidContext.raid_context_g35);
			current_sd->next_seq_lba =
			io_info->ldStartBlock + io_info->numBlocks;
			/*
			 *	update the mruBitMap LRU
			 */
			shifted_values_mask =
				(1 <<  i * BITS_PER_INDEX_STREAM) - 1;
			shifted_values = ((*track_stream & shifted_values_mask)
						<< BITS_PER_INDEX_STREAM);
			index_value_mask =
				STREAM_MASK << i * BITS_PER_INDEX_STREAM;
			unshifted_values =
				*track_stream & ~(shifted_values_mask |
				index_value_mask);
			*track_stream =
				unshifted_values | shifted_values | stream_num;
			return;
		}
	}
	/*
	 * if we did not find any stream, create a new one
	 * from the least recently used
	 */
	stream_num = (*track_stream >>
		((MAX_STREAMS_TRACKED - 1) * BITS_PER_INDEX_STREAM)) &
		STREAM_MASK;
	current_sd = &current_ld_sd->stream_track[stream_num];
	current_sd->is_read = io_info->isRead;
	current_sd->next_seq_lba = io_info->ldStartBlock + io_info->numBlocks;
	*track_stream = (((*track_stream & ZERO_LAST_STREAM) << 4) | stream_num);
	return;
}

/**
 * megasas_set_raidflag_cpu_affinity - This function sets the cpu
 * affinity (cpu of the controller) and raid_flags in the raid context
 * based on IO type.
 *
 * @praid_context:	IO RAID context
 * @raid:		LD raid map
 * @fp_possible:	Is fast path possible?
 * @is_read:		Is read IO?
 *
 */
static void
megasas_set_raidflag_cpu_affinity(struct fusion_context *fusion,
				union RAID_CONTEXT_UNION *praid_context,
				struct MR_LD_RAID *raid, bool fp_possible,
				u8 is_read, u32 scsi_buff_len)
{
	u8 cpu_sel = MR_RAID_CTX_CPUSEL_0;
	struct RAID_CONTEXT_G35 *rctx_g35;

	rctx_g35 = &praid_context->raid_context_g35;
	if (fp_possible) {
		if (is_read) {
			if ((raid->cpuAffinity.pdRead.cpu0) &&
			    (raid->cpuAffinity.pdRead.cpu1))
				cpu_sel = MR_RAID_CTX_CPUSEL_FCFS;
			else if (raid->cpuAffinity.pdRead.cpu1)
				cpu_sel = MR_RAID_CTX_CPUSEL_1;
		} else {
			if ((raid->cpuAffinity.pdWrite.cpu0) &&
			    (raid->cpuAffinity.pdWrite.cpu1))
				cpu_sel = MR_RAID_CTX_CPUSEL_FCFS;
			else if (raid->cpuAffinity.pdWrite.cpu1)
				cpu_sel = MR_RAID_CTX_CPUSEL_1;
			/* Fast path cache by pass capable R0/R1 VD */
			if ((raid->level <= 1) &&
			    (raid->capability.fp_cache_bypass_capable)) {
				rctx_g35->routing_flags |=
					(1 << MR_RAID_CTX_ROUTINGFLAGS_SLD_SHIFT);
				rctx_g35->raid_flags =
					(MR_RAID_FLAGS_IO_SUB_TYPE_CACHE_BYPASS
					<< MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT);
			}
		}
	} else {
		if (is_read) {
			if ((raid->cpuAffinity.ldRead.cpu0) &&
			    (raid->cpuAffinity.ldRead.cpu1))
				cpu_sel = MR_RAID_CTX_CPUSEL_FCFS;
			else if (raid->cpuAffinity.ldRead.cpu1)
				cpu_sel = MR_RAID_CTX_CPUSEL_1;
		} else {
			if ((raid->cpuAffinity.ldWrite.cpu0) &&
			    (raid->cpuAffinity.ldWrite.cpu1))
				cpu_sel = MR_RAID_CTX_CPUSEL_FCFS;
			else if (raid->cpuAffinity.ldWrite.cpu1)
				cpu_sel = MR_RAID_CTX_CPUSEL_1;

			if (is_stream_detected(rctx_g35) &&
			    ((raid->level == 5) || (raid->level == 6)) &&
			    (raid->writeMode == MR_RL_WRITE_THROUGH_MODE) &&
			    (cpu_sel == MR_RAID_CTX_CPUSEL_FCFS))
				cpu_sel = MR_RAID_CTX_CPUSEL_0;
		}
	}

	rctx_g35->routing_flags |=
		(cpu_sel << MR_RAID_CTX_ROUTINGFLAGS_CPUSEL_SHIFT);

	/* Always give priority to MR_RAID_FLAGS_IO_SUB_TYPE_LDIO_BW_LIMIT
	 * vs MR_RAID_FLAGS_IO_SUB_TYPE_CACHE_BYPASS.
	 * IO Subtype is not bitmap.
	 */
	if ((fusion->pcie_bw_limitation) && (raid->level == 1) && (!is_read) &&
			(scsi_buff_len > MR_LARGE_IO_MIN_SIZE)) {
		praid_context->raid_context_g35.raid_flags =
			(MR_RAID_FLAGS_IO_SUB_TYPE_LDIO_BW_LIMIT
			<< MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT);
	}
}

/**
 * megasas_build_ldio_fusion -	Prepares IOs to devices
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Prepares the io_request and chain elements (sg_frame) for IO
 * The IO can be for PD (Fast Path) or LD
 */
static void
megasas_build_ldio_fusion(struct megasas_instance *instance,
			  struct scsi_cmnd *scp,
			  struct megasas_cmd_fusion *cmd)
{
	bool fp_possible;
	u16 ld;
	u32 start_lba_lo, start_lba_hi, device_id, datalength = 0;
	u32 scsi_buff_len;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	struct IO_REQUEST_INFO io_info;
	struct fusion_context *fusion;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	u8 *raidLUN;
	unsigned long spinlock_flags;
	struct MR_LD_RAID *raid = NULL;
	struct MR_PRIV_DEVICE *mrdev_priv;
	struct RAID_CONTEXT *rctx;
	struct RAID_CONTEXT_G35 *rctx_g35;

	device_id = MEGASAS_DEV_INDEX(scp);

	fusion = instance->ctrl_context;

	io_request = cmd->io_request;
	rctx = &io_request->RaidContext.raid_context;
	rctx_g35 = &io_request->RaidContext.raid_context_g35;

	rctx->virtual_disk_tgt_id = cpu_to_le16(device_id);
	rctx->status = 0;
	rctx->ex_status = 0;

	start_lba_lo = 0;
	start_lba_hi = 0;
	fp_possible = false;

	/*
	 * 6-byte READ(0x08) or WRITE(0x0A) cdb
	 */
	if (scp->cmd_len == 6) {
		datalength = (u32) scp->cmnd[4];
		start_lba_lo = ((u32) scp->cmnd[1] << 16) |
			((u32) scp->cmnd[2] << 8) | (u32) scp->cmnd[3];

		start_lba_lo &= 0x1FFFFF;
	}

	/*
	 * 10-byte READ(0x28) or WRITE(0x2A) cdb
	 */
	else if (scp->cmd_len == 10) {
		datalength = (u32) scp->cmnd[8] |
			((u32) scp->cmnd[7] << 8);
		start_lba_lo = ((u32) scp->cmnd[2] << 24) |
			((u32) scp->cmnd[3] << 16) |
			((u32) scp->cmnd[4] << 8) | (u32) scp->cmnd[5];
	}

	/*
	 * 12-byte READ(0xA8) or WRITE(0xAA) cdb
	 */
	else if (scp->cmd_len == 12) {
		datalength = ((u32) scp->cmnd[6] << 24) |
			((u32) scp->cmnd[7] << 16) |
			((u32) scp->cmnd[8] << 8) | (u32) scp->cmnd[9];
		start_lba_lo = ((u32) scp->cmnd[2] << 24) |
			((u32) scp->cmnd[3] << 16) |
			((u32) scp->cmnd[4] << 8) | (u32) scp->cmnd[5];
	}

	/*
	 * 16-byte READ(0x88) or WRITE(0x8A) cdb
	 */
	else if (scp->cmd_len == 16) {
		datalength = ((u32) scp->cmnd[10] << 24) |
			((u32) scp->cmnd[11] << 16) |
			((u32) scp->cmnd[12] << 8) | (u32) scp->cmnd[13];
		start_lba_lo = ((u32) scp->cmnd[6] << 24) |
			((u32) scp->cmnd[7] << 16) |
			((u32) scp->cmnd[8] << 8) | (u32) scp->cmnd[9];

		start_lba_hi = ((u32) scp->cmnd[2] << 24) |
			((u32) scp->cmnd[3] << 16) |
			((u32) scp->cmnd[4] << 8) | (u32) scp->cmnd[5];
	}

	memset(&io_info, 0, sizeof(struct IO_REQUEST_INFO));
	io_info.ldStartBlock = ((u64)start_lba_hi << 32) | start_lba_lo;
	io_info.numBlocks = datalength;
	io_info.ldTgtId = device_id;
	io_info.r1_alt_dev_handle = MR_DEVHANDLE_INVALID;
	scsi_buff_len = scsi_bufflen(scp);
	io_request->DataLength = cpu_to_le32(scsi_buff_len);
	io_info.data_arms = 1;

	if (scp->sc_data_direction == DMA_FROM_DEVICE)
		io_info.isRead = 1;

	local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];
	ld = MR_TargetIdToLdGet(device_id, local_map_ptr);

	if (ld < instance->fw_supported_vd_count)
		raid = MR_LdRaidGet(ld, local_map_ptr);

	if (!raid || (!fusion->fast_path_io)) {
		rctx->reg_lock_flags  = 0;
		fp_possible = false;
	} else {
		if (MR_BuildRaidContext(instance, &io_info, rctx,
					local_map_ptr, &raidLUN))
			fp_possible = (io_info.fpOkForIo > 0) ? true : false;
	}

	if ((instance->perf_mode == MR_BALANCED_PERF_MODE) &&
		atomic_read(&scp->device->device_busy) >
		(io_info.data_arms * MR_DEVICE_HIGH_IOPS_DEPTH))
		cmd->request_desc->SCSIIO.MSIxIndex =
			mega_mod64((atomic64_add_return(1, &instance->high_iops_outstanding) /
				MR_HIGH_IOPS_BATCH_COUNT), instance->low_latency_index_start);
	else if (instance->msix_load_balance)
		cmd->request_desc->SCSIIO.MSIxIndex =
			(mega_mod64(atomic64_add_return(1, &instance->total_io_count),
				    instance->msix_vectors));
	else
		cmd->request_desc->SCSIIO.MSIxIndex =
			instance->reply_map[raw_smp_processor_id()];

	if (instance->adapter_type >= VENTURA_SERIES) {
		/* FP for Optimal raid level 1.
		 * All large RAID-1 writes (> 32 KiB, both WT and WB modes)
		 * are built by the driver as LD I/Os.
		 * All small RAID-1 WT writes (<= 32 KiB) are built as FP I/Os
		 * (there is never a reason to process these as buffered writes)
		 * All small RAID-1 WB writes (<= 32 KiB) are built as FP I/Os
		 * with the SLD bit asserted.
		 */
		if (io_info.r1_alt_dev_handle != MR_DEVHANDLE_INVALID) {
			mrdev_priv = scp->device->hostdata;

			if (atomic_inc_return(&instance->fw_outstanding) >
				(instance->host->can_queue)) {
				fp_possible = false;
				atomic_dec(&instance->fw_outstanding);
			} else if (fusion->pcie_bw_limitation &&
				((scsi_buff_len > MR_LARGE_IO_MIN_SIZE) ||
				   (atomic_dec_if_positive(&mrdev_priv->r1_ldio_hint) > 0))) {
				fp_possible = false;
				atomic_dec(&instance->fw_outstanding);
				if (scsi_buff_len > MR_LARGE_IO_MIN_SIZE)
					atomic_set(&mrdev_priv->r1_ldio_hint,
						   instance->r1_ldio_hint_default);
			}
		}

		if (!fp_possible ||
		    (io_info.isRead && io_info.ra_capable)) {
			spin_lock_irqsave(&instance->stream_lock,
					  spinlock_flags);
			megasas_stream_detect(instance, cmd, &io_info);
			spin_unlock_irqrestore(&instance->stream_lock,
					       spinlock_flags);
			/* In ventura if stream detected for a read and it is
			 * read ahead capable make this IO as LDIO
			 */
			if (is_stream_detected(rctx_g35))
				fp_possible = false;
		}

		/* If raid is NULL, set CPU affinity to default CPU0 */
		if (raid)
			megasas_set_raidflag_cpu_affinity(fusion, &io_request->RaidContext,
				raid, fp_possible, io_info.isRead,
				scsi_buff_len);
		else
			rctx_g35->routing_flags |=
				(MR_RAID_CTX_CPUSEL_0 << MR_RAID_CTX_ROUTINGFLAGS_CPUSEL_SHIFT);
	}

	if (fp_possible) {
		megasas_set_pd_lba(io_request, scp->cmd_len, &io_info, scp,
				   local_map_ptr, start_lba_lo);
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_FP_IO
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (instance->adapter_type == INVADER_SERIES) {
			rctx->type = MPI2_TYPE_CUDA;
			rctx->nseg = 0x1;
			io_request->IoFlags |= cpu_to_le16(MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH);
			rctx->reg_lock_flags |=
			  (MR_RL_FLAGS_GRANT_DESTINATION_CUDA |
			   MR_RL_FLAGS_SEQ_NUM_ENABLE);
		} else if (instance->adapter_type >= VENTURA_SERIES) {
			rctx_g35->nseg_type |= (1 << RAID_CONTEXT_NSEG_SHIFT);
			rctx_g35->nseg_type |= (MPI2_TYPE_CUDA << RAID_CONTEXT_TYPE_SHIFT);
			rctx_g35->routing_flags |= (1 << MR_RAID_CTX_ROUTINGFLAGS_SQN_SHIFT);
			io_request->IoFlags |=
				cpu_to_le16(MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH);
		}
		if (fusion->load_balance_info &&
			(fusion->load_balance_info[device_id].loadBalanceFlag) &&
			(io_info.isRead)) {
			io_info.devHandle =
				get_updated_dev_handle(instance,
					&fusion->load_balance_info[device_id],
					&io_info, local_map_ptr);
			scp->SCp.Status |= MEGASAS_LOAD_BALANCE_FLAG;
			cmd->pd_r1_lb = io_info.pd_after_lb;
			if (instance->adapter_type >= VENTURA_SERIES)
				rctx_g35->span_arm = io_info.span_arm;
			else
				rctx->span_arm = io_info.span_arm;

		} else
			scp->SCp.Status &= ~MEGASAS_LOAD_BALANCE_FLAG;

		if (instance->adapter_type >= VENTURA_SERIES)
			cmd->r1_alt_dev_handle = io_info.r1_alt_dev_handle;
		else
			cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;

		if ((raidLUN[0] == 1) &&
			(local_map_ptr->raidMap.devHndlInfo[io_info.pd_after_lb].validHandles > 1)) {
			instance->dev_handle = !(instance->dev_handle);
			io_info.devHandle =
				local_map_ptr->raidMap.devHndlInfo[io_info.pd_after_lb].devHandle[instance->dev_handle];
		}

		cmd->request_desc->SCSIIO.DevHandle = io_info.devHandle;
		io_request->DevHandle = io_info.devHandle;
		cmd->pd_interface = io_info.pd_interface;
		/* populate the LUN field */
		memcpy(io_request->LUN, raidLUN, 8);
	} else {
		rctx->timeout_value =
			cpu_to_le16(local_map_ptr->raidMap.fpPdIoTimeoutSec);
		cmd->request_desc->SCSIIO.RequestFlags =
			(MEGASAS_REQ_DESCRIPT_FLAGS_LD_IO
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (instance->adapter_type == INVADER_SERIES) {
			if (io_info.do_fp_rlbypass ||
			(rctx->reg_lock_flags == REGION_TYPE_UNUSED))
				cmd->request_desc->SCSIIO.RequestFlags =
					(MEGASAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
					MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			rctx->type = MPI2_TYPE_CUDA;
			rctx->reg_lock_flags |=
				(MR_RL_FLAGS_GRANT_DESTINATION_CPU0 |
					MR_RL_FLAGS_SEQ_NUM_ENABLE);
			rctx->nseg = 0x1;
		} else if (instance->adapter_type >= VENTURA_SERIES) {
			rctx_g35->routing_flags |= (1 << MR_RAID_CTX_ROUTINGFLAGS_SQN_SHIFT);
			rctx_g35->nseg_type |= (1 << RAID_CONTEXT_NSEG_SHIFT);
			rctx_g35->nseg_type |= (MPI2_TYPE_CUDA << RAID_CONTEXT_TYPE_SHIFT);
		}
		io_request->Function = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = cpu_to_le16(device_id);

	} /* Not FP */
}

/**
 * megasas_build_ld_nonrw_fusion - prepares non rw ios for virtual disk
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Prepares the io_request frame for non-rw io cmds for vd.
 */
static void megasas_build_ld_nonrw_fusion(struct megasas_instance *instance,
			  struct scsi_cmnd *scmd, struct megasas_cmd_fusion *cmd)
{
	u32 device_id;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	u16 ld;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	struct fusion_context *fusion = instance->ctrl_context;
	u8                          span, physArm;
	__le16                      devHandle;
	u32                         arRef, pd;
	struct MR_LD_RAID                  *raid;
	struct RAID_CONTEXT                *pRAID_Context;
	u8 fp_possible = 1;

	io_request = cmd->io_request;
	device_id = MEGASAS_DEV_INDEX(scmd);
	local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];
	io_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext.raid_context;
	/* Check with FW team */
	pRAID_Context->virtual_disk_tgt_id = cpu_to_le16(device_id);
	pRAID_Context->reg_lock_row_lba    = 0;
	pRAID_Context->reg_lock_length    = 0;

	if (fusion->fast_path_io && (
		device_id < instance->fw_supported_vd_count)) {

		ld = MR_TargetIdToLdGet(device_id, local_map_ptr);
		if (ld >= instance->fw_supported_vd_count - 1)
			fp_possible = 0;
		else {
			raid = MR_LdRaidGet(ld, local_map_ptr);
			if (!(raid->capability.fpNonRWCapable))
				fp_possible = 0;
		}
	} else
		fp_possible = 0;

	if (!fp_possible) {
		io_request->Function  = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = cpu_to_le16(device_id);
		io_request->LUN[1] = scmd->device->lun;
		pRAID_Context->timeout_value =
			cpu_to_le16 (scmd->request->timeout / HZ);
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
			MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	} else {

		/* set RAID context values */
		pRAID_Context->config_seq_num = raid->seqNum;
		if (instance->adapter_type < VENTURA_SERIES)
			pRAID_Context->reg_lock_flags = REGION_TYPE_SHARED_READ;
		pRAID_Context->timeout_value =
			cpu_to_le16(raid->fpIoTimeoutForLd);

		/* get the DevHandle for the PD (since this is
		   fpNonRWCapable, this is a single disk RAID0) */
		span = physArm = 0;
		arRef = MR_LdSpanArrayGet(ld, span, local_map_ptr);
		pd = MR_ArPdGet(arRef, physArm, local_map_ptr);
		devHandle = MR_PdDevHandleGet(pd, local_map_ptr);

		/* build request descriptor */
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_FP_IO <<
			MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		cmd->request_desc->SCSIIO.DevHandle = devHandle;

		/* populate the LUN field */
		memcpy(io_request->LUN, raid->LUN, 8);

		/* build the raidScsiIO structure */
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		io_request->DevHandle = devHandle;
	}
}

/**
 * megasas_build_syspd_fusion - prepares rw/non-rw ios for syspd
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 * @fp_possible:	parameter to detect fast path or firmware path io.
 *
 * Prepares the io_request frame for rw/non-rw io cmds for syspds
 */
static void
megasas_build_syspd_fusion(struct megasas_instance *instance,
	struct scsi_cmnd *scmd, struct megasas_cmd_fusion *cmd,
	bool fp_possible)
{
	u32 device_id;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	u16 pd_index = 0;
	u16 os_timeout_value;
	u16 timeout_limit;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	struct RAID_CONTEXT	*pRAID_Context;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	struct fusion_context *fusion = instance->ctrl_context;
	pd_sync = (void *)fusion->pd_seq_sync[(instance->pd_seq_map_id - 1) & 1];

	device_id = MEGASAS_DEV_INDEX(scmd);
	pd_index = MEGASAS_PD_INDEX(scmd);
	os_timeout_value = scmd->request->timeout / HZ;
	mr_device_priv_data = scmd->device->hostdata;
	cmd->pd_interface = mr_device_priv_data->interface_type;

	io_request = cmd->io_request;
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext.raid_context;
	pRAID_Context->reg_lock_flags = 0;
	pRAID_Context->reg_lock_row_lba = 0;
	pRAID_Context->reg_lock_length = 0;
	io_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	io_request->LUN[1] = scmd->device->lun;
	pRAID_Context->raid_flags = MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD
		<< MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT;

	/* If FW supports PD sequence number */
	if (instance->support_seqnum_jbod_fp) {
		if (instance->use_seqnum_jbod_fp &&
			instance->pd_list[pd_index].driveType == TYPE_DISK) {

			/* More than 256 PD/JBOD support for Ventura */
			if (instance->support_morethan256jbod)
				pRAID_Context->virtual_disk_tgt_id =
					pd_sync->seq[pd_index].pd_target_id;
			else
				pRAID_Context->virtual_disk_tgt_id =
					cpu_to_le16(device_id +
					(MAX_PHYSICAL_DEVICES - 1));
			pRAID_Context->config_seq_num =
				pd_sync->seq[pd_index].seqNum;
			io_request->DevHandle =
				pd_sync->seq[pd_index].devHandle;
			if (instance->adapter_type >= VENTURA_SERIES) {
				io_request->RaidContext.raid_context_g35.routing_flags |=
					(1 << MR_RAID_CTX_ROUTINGFLAGS_SQN_SHIFT);
				io_request->RaidContext.raid_context_g35.nseg_type |=
					(1 << RAID_CONTEXT_NSEG_SHIFT);
				io_request->RaidContext.raid_context_g35.nseg_type |=
					(MPI2_TYPE_CUDA << RAID_CONTEXT_TYPE_SHIFT);
			} else {
				pRAID_Context->type = MPI2_TYPE_CUDA;
				pRAID_Context->nseg = 0x1;
				pRAID_Context->reg_lock_flags |=
					(MR_RL_FLAGS_SEQ_NUM_ENABLE |
					 MR_RL_FLAGS_GRANT_DESTINATION_CUDA);
			}
		} else {
			pRAID_Context->virtual_disk_tgt_id =
				cpu_to_le16(device_id +
				(MAX_PHYSICAL_DEVICES - 1));
			pRAID_Context->config_seq_num = 0;
			io_request->DevHandle = cpu_to_le16(0xFFFF);
		}
	} else {
		pRAID_Context->virtual_disk_tgt_id = cpu_to_le16(device_id);
		pRAID_Context->config_seq_num = 0;

		if (fusion->fast_path_io) {
			local_map_ptr =
				fusion->ld_drv_map[(instance->map_id & 1)];
			io_request->DevHandle =
				local_map_ptr->raidMap.devHndlInfo[device_id].curDevHdl;
		} else {
			io_request->DevHandle = cpu_to_le16(0xFFFF);
		}
	}

	cmd->request_desc->SCSIIO.DevHandle = io_request->DevHandle;

	if ((instance->perf_mode == MR_BALANCED_PERF_MODE) &&
		atomic_read(&scmd->device->device_busy) > MR_DEVICE_HIGH_IOPS_DEPTH)
		cmd->request_desc->SCSIIO.MSIxIndex =
			mega_mod64((atomic64_add_return(1, &instance->high_iops_outstanding) /
				MR_HIGH_IOPS_BATCH_COUNT), instance->low_latency_index_start);
	else if (instance->msix_load_balance)
		cmd->request_desc->SCSIIO.MSIxIndex =
			(mega_mod64(atomic64_add_return(1, &instance->total_io_count),
				    instance->msix_vectors));
	else
		cmd->request_desc->SCSIIO.MSIxIndex =
			instance->reply_map[raw_smp_processor_id()];

	if (!fp_possible) {
		/* system pd firmware path */
		io_request->Function  = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
				MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		pRAID_Context->timeout_value = cpu_to_le16(os_timeout_value);
		pRAID_Context->virtual_disk_tgt_id = cpu_to_le16(device_id);
	} else {
		if (os_timeout_value)
			os_timeout_value++;

		/* system pd Fast Path */
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		timeout_limit = (scmd->device->type == TYPE_DISK) ?
				255 : 0xFFFF;
		pRAID_Context->timeout_value =
			cpu_to_le16((os_timeout_value > timeout_limit) ?
			timeout_limit : os_timeout_value);
		if (instance->adapter_type >= INVADER_SERIES)
			io_request->IoFlags |=
				cpu_to_le16(MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH);

		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_FP_IO <<
				MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	}
}

/**
 * megasas_build_io_fusion -	Prepares IOs to devices
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Invokes helper functions to prepare request frames
 * and sets flags appropriate for IO/Non-IO cmd
 */
static int
megasas_build_io_fusion(struct megasas_instance *instance,
			struct scsi_cmnd *scp,
			struct megasas_cmd_fusion *cmd)
{
	int sge_count;
	u8  cmd_type;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request = cmd->io_request;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	mr_device_priv_data = scp->device->hostdata;

	/* Zero out some fields so they don't get reused */
	memset(io_request->LUN, 0x0, 8);
	io_request->CDB.EEDP32.PrimaryReferenceTag = 0;
	io_request->CDB.EEDP32.PrimaryApplicationTagMask = 0;
	io_request->EEDPFlags = 0;
	io_request->Control = 0;
	io_request->EEDPBlockSize = 0;
	io_request->ChainOffset = 0;
	io_request->RaidContext.raid_context.raid_flags = 0;
	io_request->RaidContext.raid_context.type = 0;
	io_request->RaidContext.raid_context.nseg = 0;

	memcpy(io_request->CDB.CDB32, scp->cmnd, scp->cmd_len);
	/*
	 * Just the CDB length,rest of the Flags are zero
	 * This will be modified for FP in build_ldio_fusion
	 */
	io_request->IoFlags = cpu_to_le16(scp->cmd_len);

	switch (cmd_type = megasas_cmd_type(scp)) {
	case READ_WRITE_LDIO:
		megasas_build_ldio_fusion(instance, scp, cmd);
		break;
	case NON_READ_WRITE_LDIO:
		megasas_build_ld_nonrw_fusion(instance, scp, cmd);
		break;
	case READ_WRITE_SYSPDIO:
		megasas_build_syspd_fusion(instance, scp, cmd, true);
		break;
	case NON_READ_WRITE_SYSPDIO:
		if (instance->secure_jbod_support ||
		    mr_device_priv_data->is_tm_capable)
			megasas_build_syspd_fusion(instance, scp, cmd, false);
		else
			megasas_build_syspd_fusion(instance, scp, cmd, true);
		break;
	default:
		break;
	}

	/*
	 * Construct SGL
	 */

	sge_count = megasas_make_sgl(instance, scp, cmd);

	if (sge_count > instance->max_num_sge || (sge_count < 0)) {
		dev_err(&instance->pdev->dev,
			"%s %d sge_count (%d) is out of range. Range is:  0-%d\n",
			__func__, __LINE__, sge_count, instance->max_num_sge);
		return 1;
	}

	if (instance->adapter_type >= VENTURA_SERIES) {
		set_num_sge(&io_request->RaidContext.raid_context_g35, sge_count);
		cpu_to_le16s(&io_request->RaidContext.raid_context_g35.routing_flags);
		cpu_to_le16s(&io_request->RaidContext.raid_context_g35.nseg_type);
	} else {
		/* numSGE store lower 8 bit of sge_count.
		 * numSGEExt store higher 8 bit of sge_count
		 */
		io_request->RaidContext.raid_context.num_sge = sge_count;
		io_request->RaidContext.raid_context.num_sge_ext =
			(u8)(sge_count >> 8);
	}

	io_request->SGLFlags = cpu_to_le16(MPI2_SGE_FLAGS_64_BIT_ADDRESSING);

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		io_request->Control |= cpu_to_le32(MPI2_SCSIIO_CONTROL_WRITE);
	else if (scp->sc_data_direction == DMA_FROM_DEVICE)
		io_request->Control |= cpu_to_le32(MPI2_SCSIIO_CONTROL_READ);

	io_request->SGLOffset0 =
		offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4;

	io_request->SenseBufferLowAddress =
		cpu_to_le32(lower_32_bits(cmd->sense_phys_addr));
	io_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;

	cmd->scmd = scp;
	scp->SCp.ptr = (char *)cmd;

	return 0;
}

static union MEGASAS_REQUEST_DESCRIPTOR_UNION *
megasas_get_request_descriptor(struct megasas_instance *instance, u16 index)
{
	u8 *p;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;
	p = fusion->req_frames_desc +
		sizeof(union MEGASAS_REQUEST_DESCRIPTOR_UNION) * index;

	return (union MEGASAS_REQUEST_DESCRIPTOR_UNION *)p;
}


/* megasas_prepate_secondRaid1_IO
 *  It prepares the raid 1 second IO
 */
static void megasas_prepare_secondRaid1_IO(struct megasas_instance *instance,
					   struct megasas_cmd_fusion *cmd,
					   struct megasas_cmd_fusion *r1_cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc, *req_desc2 = NULL;
	struct fusion_context *fusion;
	fusion = instance->ctrl_context;
	req_desc = cmd->request_desc;
	/* copy the io request frame as well as 8 SGEs data for r1 command*/
	memcpy(r1_cmd->io_request, cmd->io_request,
	       (sizeof(struct MPI2_RAID_SCSI_IO_REQUEST)));
	memcpy(&r1_cmd->io_request->SGL, &cmd->io_request->SGL,
	       (fusion->max_sge_in_main_msg * sizeof(union MPI2_SGE_IO_UNION)));
	/*sense buffer is different for r1 command*/
	r1_cmd->io_request->SenseBufferLowAddress =
			cpu_to_le32(lower_32_bits(r1_cmd->sense_phys_addr));
	r1_cmd->scmd = cmd->scmd;
	req_desc2 = megasas_get_request_descriptor(instance,
						   (r1_cmd->index - 1));
	req_desc2->Words = 0;
	r1_cmd->request_desc = req_desc2;
	req_desc2->SCSIIO.SMID = cpu_to_le16(r1_cmd->index);
	req_desc2->SCSIIO.RequestFlags = req_desc->SCSIIO.RequestFlags;
	r1_cmd->request_desc->SCSIIO.DevHandle = cmd->r1_alt_dev_handle;
	r1_cmd->io_request->DevHandle = cmd->r1_alt_dev_handle;
	r1_cmd->r1_alt_dev_handle = cmd->io_request->DevHandle;
	cmd->io_request->RaidContext.raid_context_g35.flow_specific.peer_smid =
			cpu_to_le16(r1_cmd->index);
	r1_cmd->io_request->RaidContext.raid_context_g35.flow_specific.peer_smid =
			cpu_to_le16(cmd->index);
	/*MSIxIndex of both commands request descriptors should be same*/
	r1_cmd->request_desc->SCSIIO.MSIxIndex =
			cmd->request_desc->SCSIIO.MSIxIndex;
	/*span arm is different for r1 cmd*/
	r1_cmd->io_request->RaidContext.raid_context_g35.span_arm =
			cmd->io_request->RaidContext.raid_context_g35.span_arm + 1;
}

/**
 * megasas_build_and_issue_cmd_fusion -Main routine for building and
 *                                     issuing non IOCTL cmd
 * @instance:			Adapter soft state
 * @scmd:			pointer to scsi cmd from OS
 */
static u32
megasas_build_and_issue_cmd_fusion(struct megasas_instance *instance,
				   struct scsi_cmnd *scmd)
{
	struct megasas_cmd_fusion *cmd, *r1_cmd = NULL;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u32 index;

	if ((megasas_cmd_type(scmd) == READ_WRITE_LDIO) &&
		instance->ldio_threshold &&
		(atomic_inc_return(&instance->ldio_outstanding) >
		instance->ldio_threshold)) {
		atomic_dec(&instance->ldio_outstanding);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	if (atomic_inc_return(&instance->fw_outstanding) >
			instance->host->can_queue) {
		atomic_dec(&instance->fw_outstanding);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	cmd = megasas_get_cmd_fusion(instance, scmd->request->tag);

	if (!cmd) {
		atomic_dec(&instance->fw_outstanding);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	index = cmd->index;

	req_desc = megasas_get_request_descriptor(instance, index-1);

	req_desc->Words = 0;
	cmd->request_desc = req_desc;

	if (megasas_build_io_fusion(instance, scmd, cmd)) {
		megasas_return_cmd_fusion(instance, cmd);
		dev_err(&instance->pdev->dev, "Error building command\n");
		cmd->request_desc = NULL;
		atomic_dec(&instance->fw_outstanding);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	req_desc = cmd->request_desc;
	req_desc->SCSIIO.SMID = cpu_to_le16(index);

	if (cmd->io_request->ChainOffset != 0 &&
	    cmd->io_request->ChainOffset != 0xF)
		dev_err(&instance->pdev->dev, "The chain offset value is not "
		       "correct : %x\n", cmd->io_request->ChainOffset);
	/*
	 *	if it is raid 1/10 fp write capable.
	 *	try to get second command from pool and construct it.
	 *	From FW, it has confirmed that lba values of two PDs
	 *	corresponds to single R1/10 LD are always same
	 *
	 */
	/*	driver side count always should be less than max_fw_cmds
	 *	to get new command
	 */
	if (cmd->r1_alt_dev_handle != MR_DEVHANDLE_INVALID) {
		r1_cmd = megasas_get_cmd_fusion(instance,
				(scmd->request->tag + instance->max_fw_cmds));
		megasas_prepare_secondRaid1_IO(instance, cmd, r1_cmd);
	}


	/*
	 * Issue the command to the FW
	 */

	megasas_fire_cmd_fusion(instance, req_desc);

	if (r1_cmd)
		megasas_fire_cmd_fusion(instance, r1_cmd->request_desc);


	return 0;
}

/**
 * megasas_complete_r1_command -
 * completes R1 FP write commands which has valid peer smid
 * @instance:			Adapter soft state
 * @cmd_fusion:			MPT command frame
 *
 */
static inline void
megasas_complete_r1_command(struct megasas_instance *instance,
			    struct megasas_cmd_fusion *cmd)
{
	u8 *sense, status, ex_status;
	u32 data_length;
	u16 peer_smid;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *r1_cmd = NULL;
	struct scsi_cmnd *scmd_local = NULL;
	struct RAID_CONTEXT_G35 *rctx_g35;

	rctx_g35 = &cmd->io_request->RaidContext.raid_context_g35;
	fusion = instance->ctrl_context;
	peer_smid = le16_to_cpu(rctx_g35->flow_specific.peer_smid);

	r1_cmd = fusion->cmd_list[peer_smid - 1];
	scmd_local = cmd->scmd;
	status = rctx_g35->status;
	ex_status = rctx_g35->ex_status;
	data_length = cmd->io_request->DataLength;
	sense = cmd->sense;

	cmd->cmd_completed = true;

	/* Check if peer command is completed or not*/
	if (r1_cmd->cmd_completed) {
		rctx_g35 = &r1_cmd->io_request->RaidContext.raid_context_g35;
		if (rctx_g35->status != MFI_STAT_OK) {
			status = rctx_g35->status;
			ex_status = rctx_g35->ex_status;
			data_length = r1_cmd->io_request->DataLength;
			sense = r1_cmd->sense;
		}

		megasas_return_cmd_fusion(instance, r1_cmd);
		map_cmd_status(fusion, scmd_local, status, ex_status,
			       le32_to_cpu(data_length), sense);
		if (instance->ldio_threshold &&
		    megasas_cmd_type(scmd_local) == READ_WRITE_LDIO)
			atomic_dec(&instance->ldio_outstanding);
		scmd_local->SCp.ptr = NULL;
		megasas_return_cmd_fusion(instance, cmd);
		scsi_dma_unmap(scmd_local);
		scmd_local->scsi_done(scmd_local);
	}
}

/**
 * complete_cmd_fusion -	Completes command
 * @instance:			Adapter soft state
 * Completes all commands that is in reply descriptor queue
 */
static int
complete_cmd_fusion(struct megasas_instance *instance, u32 MSIxIndex,
		    struct megasas_irq_context *irq_context)
{
	union MPI2_REPLY_DESCRIPTORS_UNION *desc;
	struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *reply_desc;
	struct MPI2_RAID_SCSI_IO_REQUEST *scsi_io_req;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;
	struct megasas_cmd_fusion *cmd_fusion;
	u16 smid, num_completed;
	u8 reply_descript_type, *sense, status, extStatus;
	u32 device_id, data_length;
	union desc_value d_val;
	struct LD_LOAD_BALANCE_INFO *lbinfo;
	int threshold_reply_count = 0;
	struct scsi_cmnd *scmd_local = NULL;
	struct MR_TASK_MANAGE_REQUEST *mr_tm_req;
	struct MPI2_SCSI_TASK_MANAGE_REQUEST *mpi_tm_req;

	fusion = instance->ctrl_context;

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)
		return IRQ_HANDLED;

	desc = fusion->reply_frames_desc[MSIxIndex] +
				fusion->last_reply_idx[MSIxIndex];

	reply_desc = (struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;

	d_val.word = desc->Words;

	reply_descript_type = reply_desc->ReplyFlags &
		MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

	if (reply_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
		return IRQ_NONE;

	num_completed = 0;

	while (d_val.u.low != cpu_to_le32(UINT_MAX) &&
	       d_val.u.high != cpu_to_le32(UINT_MAX)) {

		smid = le16_to_cpu(reply_desc->SMID);
		cmd_fusion = fusion->cmd_list[smid - 1];
		scsi_io_req = (struct MPI2_RAID_SCSI_IO_REQUEST *)
						cmd_fusion->io_request;

		scmd_local = cmd_fusion->scmd;
		status = scsi_io_req->RaidContext.raid_context.status;
		extStatus = scsi_io_req->RaidContext.raid_context.ex_status;
		sense = cmd_fusion->sense;
		data_length = scsi_io_req->DataLength;

		switch (scsi_io_req->Function) {
		case MPI2_FUNCTION_SCSI_TASK_MGMT:
			mr_tm_req = (struct MR_TASK_MANAGE_REQUEST *)
						cmd_fusion->io_request;
			mpi_tm_req = (struct MPI2_SCSI_TASK_MANAGE_REQUEST *)
						&mr_tm_req->TmRequest;
			dev_dbg(&instance->pdev->dev, "TM completion:"
				"type: 0x%x TaskMID: 0x%x\n",
				mpi_tm_req->TaskType, mpi_tm_req->TaskMID);
			complete(&cmd_fusion->done);
			break;
		case MPI2_FUNCTION_SCSI_IO_REQUEST:  /*Fast Path IO.*/
			/* Update load balancing info */
			if (fusion->load_balance_info &&
			    (cmd_fusion->scmd->SCp.Status &
			    MEGASAS_LOAD_BALANCE_FLAG)) {
				device_id = MEGASAS_DEV_INDEX(scmd_local);
				lbinfo = &fusion->load_balance_info[device_id];
				atomic_dec(&lbinfo->scsi_pending_cmds[cmd_fusion->pd_r1_lb]);
				cmd_fusion->scmd->SCp.Status &= ~MEGASAS_LOAD_BALANCE_FLAG;
			}
			/* Fall through - and complete IO */
		case MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST: /* LD-IO Path */
			atomic_dec(&instance->fw_outstanding);
			if (cmd_fusion->r1_alt_dev_handle == MR_DEVHANDLE_INVALID) {
				map_cmd_status(fusion, scmd_local, status,
					       extStatus, le32_to_cpu(data_length),
					       sense);
				if (instance->ldio_threshold &&
				    (megasas_cmd_type(scmd_local) == READ_WRITE_LDIO))
					atomic_dec(&instance->ldio_outstanding);
				scmd_local->SCp.ptr = NULL;
				megasas_return_cmd_fusion(instance, cmd_fusion);
				scsi_dma_unmap(scmd_local);
				scmd_local->scsi_done(scmd_local);
			} else	/* Optimal VD - R1 FP command completion. */
				megasas_complete_r1_command(instance, cmd_fusion);
			break;
		case MEGASAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST: /*MFI command */
			cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];
			/* Poll mode. Dummy free.
			 * In case of Interrupt mode, caller has reverse check.
			 */
			if (cmd_mfi->flags & DRV_DCMD_POLLED_MODE) {
				cmd_mfi->flags &= ~DRV_DCMD_POLLED_MODE;
				megasas_return_cmd(instance, cmd_mfi);
			} else
				megasas_complete_cmd(instance, cmd_mfi, DID_OK);
			break;
		}

		fusion->last_reply_idx[MSIxIndex]++;
		if (fusion->last_reply_idx[MSIxIndex] >=
		    fusion->reply_q_depth)
			fusion->last_reply_idx[MSIxIndex] = 0;

		desc->Words = cpu_to_le64(ULLONG_MAX);
		num_completed++;
		threshold_reply_count++;

		/* Get the next reply descriptor */
		if (!fusion->last_reply_idx[MSIxIndex])
			desc = fusion->reply_frames_desc[MSIxIndex];
		else
			desc++;

		reply_desc =
		  (struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;

		d_val.word = desc->Words;

		reply_descript_type = reply_desc->ReplyFlags &
			MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

		if (reply_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			break;
		/*
		 * Write to reply post host index register after completing threshold
		 * number of reply counts and still there are more replies in reply queue
		 * pending to be completed
		 */
		if (threshold_reply_count >= instance->threshold_reply_count) {
			if (instance->msix_combined)
				writel(((MSIxIndex & 0x7) << 24) |
					fusion->last_reply_idx[MSIxIndex],
					instance->reply_post_host_index_addr[MSIxIndex/8]);
			else
				writel((MSIxIndex << 24) |
					fusion->last_reply_idx[MSIxIndex],
					instance->reply_post_host_index_addr[0]);
			threshold_reply_count = 0;
			if (irq_context) {
				if (!irq_context->irq_poll_scheduled) {
					irq_context->irq_poll_scheduled = true;
					irq_context->irq_line_enable = true;
					irq_poll_sched(&irq_context->irqpoll);
				}
				return num_completed;
			}
		}
	}

	if (num_completed) {
		wmb();
		if (instance->msix_combined)
			writel(((MSIxIndex & 0x7) << 24) |
				fusion->last_reply_idx[MSIxIndex],
				instance->reply_post_host_index_addr[MSIxIndex/8]);
		else
			writel((MSIxIndex << 24) |
				fusion->last_reply_idx[MSIxIndex],
				instance->reply_post_host_index_addr[0]);
		megasas_check_and_restore_queue_depth(instance);
	}
	return num_completed;
}

/**
 * megasas_enable_irq_poll() - enable irqpoll
 */
static void megasas_enable_irq_poll(struct megasas_instance *instance)
{
	u32 count, i;
	struct megasas_irq_context *irq_ctx;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	for (i = 0; i < count; i++) {
		irq_ctx = &instance->irq_context[i];
		irq_poll_enable(&irq_ctx->irqpoll);
	}
}

/**
 * megasas_sync_irqs -	Synchronizes all IRQs owned by adapter
 * @instance:			Adapter soft state
 */
static void megasas_sync_irqs(unsigned long instance_addr)
{
	u32 count, i;
	struct megasas_instance *instance =
		(struct megasas_instance *)instance_addr;
	struct megasas_irq_context *irq_ctx;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	for (i = 0; i < count; i++) {
		synchronize_irq(pci_irq_vector(instance->pdev, i));
		irq_ctx = &instance->irq_context[i];
		irq_poll_disable(&irq_ctx->irqpoll);
		if (irq_ctx->irq_poll_scheduled) {
			irq_ctx->irq_poll_scheduled = false;
			enable_irq(irq_ctx->os_irq);
		}
	}
}

/**
 * megasas_irqpoll() - process a queue for completed reply descriptors
 * @irqpoll:	IRQ poll structure associated with queue to poll.
 * @budget:	Threshold of reply descriptors to process per poll.
 *
 * Return: The number of entries processed.
 */

int megasas_irqpoll(struct irq_poll *irqpoll, int budget)
{
	struct megasas_irq_context *irq_ctx;
	struct megasas_instance *instance;
	int num_entries;

	irq_ctx = container_of(irqpoll, struct megasas_irq_context, irqpoll);
	instance = irq_ctx->instance;

	if (irq_ctx->irq_line_enable) {
		disable_irq(irq_ctx->os_irq);
		irq_ctx->irq_line_enable = false;
	}

	num_entries = complete_cmd_fusion(instance, irq_ctx->MSIxIndex, irq_ctx);
	if (num_entries < budget) {
		irq_poll_complete(irqpoll);
		irq_ctx->irq_poll_scheduled = false;
		enable_irq(irq_ctx->os_irq);
	}

	return num_entries;
}

/**
 * megasas_complete_cmd_dpc_fusion -	Completes command
 * @instance:			Adapter soft state
 *
 * Tasklet to complete cmds
 */
static void
megasas_complete_cmd_dpc_fusion(unsigned long instance_addr)
{
	struct megasas_instance *instance =
		(struct megasas_instance *)instance_addr;
	u32 count, MSIxIndex;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	/* If we have already declared adapter dead, donot complete cmds */
	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR)
		return;

	for (MSIxIndex = 0 ; MSIxIndex < count; MSIxIndex++)
		complete_cmd_fusion(instance, MSIxIndex, NULL);
}

/**
 * megasas_isr_fusion - isr entry point
 */
static irqreturn_t megasas_isr_fusion(int irq, void *devp)
{
	struct megasas_irq_context *irq_context = devp;
	struct megasas_instance *instance = irq_context->instance;
	u32 mfiStatus;

	if (instance->mask_interrupts)
		return IRQ_NONE;

#if defined(ENABLE_IRQ_POLL)
	if (irq_context->irq_poll_scheduled)
		return IRQ_HANDLED;
#endif

	if (!instance->msix_vectors) {
		mfiStatus = instance->instancet->clear_intr(instance);
		if (!mfiStatus)
			return IRQ_NONE;
	}

	/* If we are resetting, bail */
	if (test_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags)) {
		instance->instancet->clear_intr(instance);
		return IRQ_HANDLED;
	}

	return complete_cmd_fusion(instance, irq_context->MSIxIndex, irq_context)
			? IRQ_HANDLED : IRQ_NONE;
}

/**
 * build_mpt_mfi_pass_thru - builds a cmd fo MFI Pass thru
 * @instance:			Adapter soft state
 * mfi_cmd:			megasas_cmd pointer
 *
 */
static void
build_mpt_mfi_pass_thru(struct megasas_instance *instance,
			struct megasas_cmd *mfi_cmd)
{
	struct MPI25_IEEE_SGE_CHAIN64 *mpi25_ieee_chain;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_req;
	struct megasas_cmd_fusion *cmd;
	struct fusion_context *fusion;
	struct megasas_header *frame_hdr = &mfi_cmd->frame->hdr;

	fusion = instance->ctrl_context;

	cmd = megasas_get_cmd_fusion(instance,
			instance->max_scsi_cmds + mfi_cmd->index);

	/*  Save the smid. To be used for returning the cmd */
	mfi_cmd->context.smid = cmd->index;

	/*
	 * For cmds where the flag is set, store the flag and check
	 * on completion. For cmds with this flag, don't call
	 * megasas_complete_cmd
	 */

	if (frame_hdr->flags & cpu_to_le16(MFI_FRAME_DONT_POST_IN_REPLY_QUEUE))
		mfi_cmd->flags |= DRV_DCMD_POLLED_MODE;

	io_req = cmd->io_request;

	if (instance->adapter_type >= INVADER_SERIES) {
		struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr_end =
			(struct MPI25_IEEE_SGE_CHAIN64 *)&io_req->SGL;
		sgl_ptr_end += fusion->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}

	mpi25_ieee_chain =
	  (struct MPI25_IEEE_SGE_CHAIN64 *)&io_req->SGL.IeeeChain;

	io_req->Function    = MEGASAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST;
	io_req->SGLOffset0  = offsetof(struct MPI2_RAID_SCSI_IO_REQUEST,
				       SGL) / 4;
	io_req->ChainOffset = fusion->chain_offset_mfi_pthru;

	mpi25_ieee_chain->Address = cpu_to_le64(mfi_cmd->frame_phys_addr);

	mpi25_ieee_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT |
		MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR;

	mpi25_ieee_chain->Length = cpu_to_le32(instance->mfi_frame_size);
}

/**
 * build_mpt_cmd - Calls helper function to build a cmd MFI Pass thru cmd
 * @instance:			Adapter soft state
 * @cmd:			mfi cmd to build
 *
 */
static union MEGASAS_REQUEST_DESCRIPTOR_UNION *
build_mpt_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc = NULL;
	u16 index;

	build_mpt_mfi_pass_thru(instance, cmd);
	index = cmd->context.smid;

	req_desc = megasas_get_request_descriptor(instance, index - 1);

	req_desc->Words = 0;
	req_desc->SCSIIO.RequestFlags = (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
					 MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	req_desc->SCSIIO.SMID = cpu_to_le16(index);

	return req_desc;
}

/**
 * megasas_issue_dcmd_fusion - Issues a MFI Pass thru cmd
 * @instance:			Adapter soft state
 * @cmd:			mfi cmd pointer
 *
 */
static void
megasas_issue_dcmd_fusion(struct megasas_instance *instance,
			  struct megasas_cmd *cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	req_desc = build_mpt_cmd(instance, cmd);

	megasas_fire_cmd_fusion(instance, req_desc);
	return;
}

/**
 * megasas_release_fusion -	Reverses the FW initialization
 * @instance:			Adapter soft state
 */
void
megasas_release_fusion(struct megasas_instance *instance)
{
	megasas_free_ioc_init_cmd(instance);
	megasas_free_cmds(instance);
	megasas_free_cmds_fusion(instance);

	iounmap(instance->reg_set);

	pci_release_selected_regions(instance->pdev, 1<<instance->bar);
}

/**
 * megasas_read_fw_status_reg_fusion - returns the current FW status value
 * @regs:			MFI register set
 */
static u32
megasas_read_fw_status_reg_fusion(struct megasas_instance *instance)
{
	return megasas_readl(instance, &instance->reg_set->outbound_scratch_pad_0);
}

/**
 * megasas_alloc_host_crash_buffer -	Host buffers for Crash dump collection from Firmware
 * @instance:				Controller's soft instance
 * return:			        Number of allocated host crash buffers
 */
static void
megasas_alloc_host_crash_buffer(struct megasas_instance *instance)
{
	unsigned int i;

	for (i = 0; i < MAX_CRASH_DUMP_SIZE; i++) {
		instance->crash_buf[i] = vzalloc(CRASH_DMA_BUF_SIZE);
		if (!instance->crash_buf[i]) {
			dev_info(&instance->pdev->dev, "Firmware crash dump "
				"memory allocation failed at index %d\n", i);
			break;
		}
	}
	instance->drv_buf_alloc = i;
}

/**
 * megasas_free_host_crash_buffer -	Host buffers for Crash dump collection from Firmware
 * @instance:				Controller's soft instance
 */
void
megasas_free_host_crash_buffer(struct megasas_instance *instance)
{
	unsigned int i;
	for (i = 0; i < instance->drv_buf_alloc; i++) {
		if (instance->crash_buf[i])
			vfree(instance->crash_buf[i]);
	}
	instance->drv_buf_index = 0;
	instance->drv_buf_alloc = 0;
	instance->fw_crash_state = UNAVAILABLE;
	instance->fw_crash_buffer_size = 0;
}

/**
 * megasas_adp_reset_fusion -	For controller reset
 * @regs:				MFI register set
 */
static int
megasas_adp_reset_fusion(struct megasas_instance *instance,
			 struct megasas_register_set __iomem *regs)
{
	u32 host_diag, abs_state, retry;

	/* Now try to reset the chip */
	writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_1ST_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_2ND_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_3RD_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_4TH_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_5TH_KEY_VALUE, &instance->reg_set->fusion_seq_offset);
	writel(MPI2_WRSEQ_6TH_KEY_VALUE, &instance->reg_set->fusion_seq_offset);

	/* Check that the diag write enable (DRWE) bit is on */
	host_diag = megasas_readl(instance, &instance->reg_set->fusion_host_diag);
	retry = 0;
	while (!(host_diag & HOST_DIAG_WRITE_ENABLE)) {
		msleep(100);
		host_diag = megasas_readl(instance,
					  &instance->reg_set->fusion_host_diag);
		if (retry++ == 100) {
			dev_warn(&instance->pdev->dev,
				"Host diag unlock failed from %s %d\n",
				__func__, __LINE__);
			break;
		}
	}
	if (!(host_diag & HOST_DIAG_WRITE_ENABLE))
		return -1;

	/* Send chip reset command */
	writel(host_diag | HOST_DIAG_RESET_ADAPTER,
		&instance->reg_set->fusion_host_diag);
	msleep(3000);

	/* Make sure reset adapter bit is cleared */
	host_diag = megasas_readl(instance, &instance->reg_set->fusion_host_diag);
	retry = 0;
	while (host_diag & HOST_DIAG_RESET_ADAPTER) {
		msleep(100);
		host_diag = megasas_readl(instance,
					  &instance->reg_set->fusion_host_diag);
		if (retry++ == 1000) {
			dev_warn(&instance->pdev->dev,
				"Diag reset adapter never cleared %s %d\n",
				__func__, __LINE__);
			break;
		}
	}
	if (host_diag & HOST_DIAG_RESET_ADAPTER)
		return -1;

	abs_state = instance->instancet->read_fw_status_reg(instance)
			& MFI_STATE_MASK;
	retry = 0;

	while ((abs_state <= MFI_STATE_FW_INIT) && (retry++ < 1000)) {
		msleep(100);
		abs_state = instance->instancet->
			read_fw_status_reg(instance) & MFI_STATE_MASK;
	}
	if (abs_state <= MFI_STATE_FW_INIT) {
		dev_warn(&instance->pdev->dev,
			"fw state < MFI_STATE_FW_INIT, state = 0x%x %s %d\n",
			abs_state, __func__, __LINE__);
		return -1;
	}

	return 0;
}

/**
 * megasas_check_reset_fusion -	For controller reset check
 * @regs:				MFI register set
 */
static int
megasas_check_reset_fusion(struct megasas_instance *instance,
			   struct megasas_register_set __iomem *regs)
{
	return 0;
}

/**
 * megasas_trigger_snap_dump -	Trigger snap dump in FW
 * @instance:			Soft instance of adapter
 */
static inline void megasas_trigger_snap_dump(struct megasas_instance *instance)
{
	int j;
	u32 fw_state, abs_state;

	if (!instance->disableOnlineCtrlReset) {
		dev_info(&instance->pdev->dev, "Trigger snap dump\n");
		writel(MFI_ADP_TRIGGER_SNAP_DUMP,
		       &instance->reg_set->doorbell);
		readl(&instance->reg_set->doorbell);
	}

	for (j = 0; j < instance->snapdump_wait_time; j++) {
		abs_state = instance->instancet->read_fw_status_reg(instance);
		fw_state = abs_state & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT) {
			dev_printk(KERN_ERR, &instance->pdev->dev,
				   "FW in FAULT state Fault code:0x%x subcode:0x%x func:%s\n",
				   abs_state & MFI_STATE_FAULT_CODE,
				   abs_state & MFI_STATE_FAULT_SUBCODE, __func__);
			return;
		}
		msleep(1000);
	}
}

/* This function waits for outstanding commands on fusion to complete */
static int
megasas_wait_for_outstanding_fusion(struct megasas_instance *instance,
				    int reason, int *convert)
{
	int i, outstanding, retval = 0, hb_seconds_missed = 0;
	u32 fw_state, abs_state;
	u32 waittime_for_io_completion;

	waittime_for_io_completion =
		min_t(u32, resetwaittime,
			(resetwaittime - instance->snapdump_wait_time));

	if (reason == MFI_IO_TIMEOUT_OCR) {
		dev_info(&instance->pdev->dev,
			"MFI command is timed out\n");
		megasas_complete_cmd_dpc_fusion((unsigned long)instance);
		if (instance->snapdump_wait_time)
			megasas_trigger_snap_dump(instance);
		retval = 1;
		goto out;
	}

	for (i = 0; i < waittime_for_io_completion; i++) {
		/* Check if firmware is in fault state */
		abs_state = instance->instancet->read_fw_status_reg(instance);
		fw_state = abs_state & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT) {
			dev_printk(KERN_ERR, &instance->pdev->dev,
				   "FW in FAULT state Fault code:0x%x subcode:0x%x func:%s\n",
				   abs_state & MFI_STATE_FAULT_CODE,
				   abs_state & MFI_STATE_FAULT_SUBCODE, __func__);
			megasas_complete_cmd_dpc_fusion((unsigned long)instance);
			if (instance->requestorId && reason) {
				dev_warn(&instance->pdev->dev, "SR-IOV Found FW in FAULT"
				" state while polling during"
				" I/O timeout handling for %d\n",
				instance->host->host_no);
				*convert = 1;
			}

			retval = 1;
			goto out;
		}


		/* If SR-IOV VF mode & heartbeat timeout, don't wait */
		if (instance->requestorId && !reason) {
			retval = 1;
			goto out;
		}

		/* If SR-IOV VF mode & I/O timeout, check for HB timeout */
		if (instance->requestorId && (reason == SCSIIO_TIMEOUT_OCR)) {
			if (instance->hb_host_mem->HB.fwCounter !=
			    instance->hb_host_mem->HB.driverCounter) {
				instance->hb_host_mem->HB.driverCounter =
					instance->hb_host_mem->HB.fwCounter;
				hb_seconds_missed = 0;
			} else {
				hb_seconds_missed++;
				if (hb_seconds_missed ==
				    (MEGASAS_SRIOV_HEARTBEAT_INTERVAL_VF/HZ)) {
					dev_warn(&instance->pdev->dev, "SR-IOV:"
					       " Heartbeat never completed "
					       " while polling during I/O "
					       " timeout handling for "
					       "scsi%d.\n",
					       instance->host->host_no);
					       *convert = 1;
					       retval = 1;
					       goto out;
				}
			}
		}

		megasas_complete_cmd_dpc_fusion((unsigned long)instance);
		outstanding = atomic_read(&instance->fw_outstanding);
		if (!outstanding)
			goto out;

		if (!(i % MEGASAS_RESET_NOTICE_INTERVAL)) {
			dev_notice(&instance->pdev->dev, "[%2d]waiting for %d "
			       "commands to complete for scsi%d\n", i,
			       outstanding, instance->host->host_no);
		}
		msleep(1000);
	}

	if (instance->snapdump_wait_time) {
		megasas_trigger_snap_dump(instance);
		retval = 1;
		goto out;
	}

	if (atomic_read(&instance->fw_outstanding)) {
		dev_err(&instance->pdev->dev, "pending commands remain after waiting, "
		       "will reset adapter scsi%d.\n",
		       instance->host->host_no);
		*convert = 1;
		retval = 1;
	}

out:
	return retval;
}

void  megasas_reset_reply_desc(struct megasas_instance *instance)
{
	int i, j, count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;

	fusion = instance->ctrl_context;
	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	for (i = 0 ; i < count ; i++) {
		fusion->last_reply_idx[i] = 0;
		reply_desc = fusion->reply_frames_desc[i];
		for (j = 0 ; j < fusion->reply_q_depth; j++, reply_desc++)
			reply_desc->Words = cpu_to_le64(ULLONG_MAX);
	}
}

/*
 * megasas_refire_mgmt_cmd :	Re-fire management commands
 * @instance:				Controller's soft instance
*/
void megasas_refire_mgmt_cmd(struct megasas_instance *instance,
			     bool return_ioctl)
{
	int j;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u16 smid;
	bool refire_cmd = 0;
	u8 result;
	u32 opcode = 0;

	fusion = instance->ctrl_context;

	/* Re-fire management commands.
	 * Do not traverse complet MPT frame pool. Start from max_scsi_cmds.
	 */
	for (j = instance->max_scsi_cmds ; j < instance->max_fw_cmds; j++) {
		cmd_fusion = fusion->cmd_list[j];
		cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];
		smid = le16_to_cpu(cmd_mfi->context.smid);
		result = REFIRE_CMD;

		if (!smid)
			continue;

		req_desc = megasas_get_request_descriptor(instance, smid - 1);

		switch (cmd_mfi->frame->hdr.cmd) {
		case MFI_CMD_DCMD:
			opcode = le32_to_cpu(cmd_mfi->frame->dcmd.opcode);
			 /* Do not refire shutdown command */
			if (opcode == MR_DCMD_CTRL_SHUTDOWN) {
				cmd_mfi->frame->dcmd.cmd_status = MFI_STAT_OK;
				result = COMPLETE_CMD;
				break;
			}

			refire_cmd = ((opcode != MR_DCMD_LD_MAP_GET_INFO)) &&
				      (opcode != MR_DCMD_SYSTEM_PD_MAP_GET_INFO) &&
				      !(cmd_mfi->flags & DRV_DCMD_SKIP_REFIRE);

			if (!refire_cmd)
				result = RETURN_CMD;

			break;
		case MFI_CMD_NVME:
			if (!instance->support_nvme_passthru) {
				cmd_mfi->frame->hdr.cmd_status = MFI_STAT_INVALID_CMD;
				result = COMPLETE_CMD;
			}

			break;
		case MFI_CMD_TOOLBOX:
			if (!instance->support_pci_lane_margining) {
				cmd_mfi->frame->hdr.cmd_status = MFI_STAT_INVALID_CMD;
				result = COMPLETE_CMD;
			}

			break;
		default:
			break;
		}

		if (return_ioctl && cmd_mfi->sync_cmd &&
		    cmd_mfi->frame->hdr.cmd != MFI_CMD_ABORT) {
			dev_err(&instance->pdev->dev,
				"return -EBUSY from %s %d cmd 0x%x opcode 0x%x\n",
				__func__, __LINE__, cmd_mfi->frame->hdr.cmd,
				le32_to_cpu(cmd_mfi->frame->dcmd.opcode));
			cmd_mfi->cmd_status_drv = DCMD_BUSY;
			result = COMPLETE_CMD;
		}

		switch (result) {
		case REFIRE_CMD:
			megasas_fire_cmd_fusion(instance, req_desc);
			break;
		case RETURN_CMD:
			megasas_return_cmd(instance, cmd_mfi);
			break;
		case COMPLETE_CMD:
			megasas_complete_cmd(instance, cmd_mfi, DID_OK);
			break;
		}
	}
}

/*
 * megasas_return_polled_cmds: Return polled mode commands back to the pool
 *			       before initiating an OCR.
 * @instance:                  Controller's soft instance
 */
static void
megasas_return_polled_cmds(struct megasas_instance *instance)
{
	int i;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;

	fusion = instance->ctrl_context;

	for (i = instance->max_scsi_cmds; i < instance->max_fw_cmds; i++) {
		cmd_fusion = fusion->cmd_list[i];
		cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];

		if (cmd_mfi->flags & DRV_DCMD_POLLED_MODE) {
			if (megasas_dbg_lvl & OCR_DEBUG)
				dev_info(&instance->pdev->dev,
					 "%s %d return cmd 0x%x opcode 0x%x\n",
					 __func__, __LINE__, cmd_mfi->frame->hdr.cmd,
					 le32_to_cpu(cmd_mfi->frame->dcmd.opcode));
			cmd_mfi->flags &= ~DRV_DCMD_POLLED_MODE;
			megasas_return_cmd(instance, cmd_mfi);
		}
	}
}

/*
 * megasas_track_scsiio : Track SCSI IOs outstanding to a SCSI device
 * @instance: per adapter struct
 * @channel: the channel assigned by the OS
 * @id: the id assigned by the OS
 *
 * Returns SUCCESS if no IOs pending to SCSI device, else return FAILED
 */

static int megasas_track_scsiio(struct megasas_instance *instance,
		int id, int channel)
{
	int i, found = 0;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	fusion = instance->ctrl_context;

	for (i = 0 ; i < instance->max_scsi_cmds; i++) {
		cmd_fusion = fusion->cmd_list[i];
		if (cmd_fusion->scmd &&
			(cmd_fusion->scmd->device->id == id &&
			cmd_fusion->scmd->device->channel == channel)) {
			dev_info(&instance->pdev->dev,
				"SCSI commands pending to target"
				"channel %d id %d \tSMID: 0x%x\n",
				channel, id, cmd_fusion->index);
			scsi_print_command(cmd_fusion->scmd);
			found = 1;
			break;
		}
	}

	return found ? FAILED : SUCCESS;
}

/**
 * megasas_tm_response_code - translation of device response code
 * @ioc: per adapter object
 * @mpi_reply: MPI reply returned by firmware
 *
 * Return nothing.
 */
static void
megasas_tm_response_code(struct megasas_instance *instance,
		struct MPI2_SCSI_TASK_MANAGE_REPLY *mpi_reply)
{
	char *desc;

	switch (mpi_reply->ResponseCode) {
	case MPI2_SCSITASKMGMT_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI2_SCSITASKMGMT_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case 0xA:
		desc = "overlapped tag attempted";
		break;
	case MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	dev_dbg(&instance->pdev->dev, "response_code(%01x): %s\n",
		mpi_reply->ResponseCode, desc);
	dev_dbg(&instance->pdev->dev,
		"TerminationCount/DevHandle/Function/TaskType/IOCStat/IOCLoginfo"
		" 0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
		mpi_reply->TerminationCount, mpi_reply->DevHandle,
		mpi_reply->Function, mpi_reply->TaskType,
		mpi_reply->IOCStatus, mpi_reply->IOCLogInfo);
}

/**
 * megasas_issue_tm - main routine for sending tm requests
 * @instance: per adapter struct
 * @device_handle: device handle
 * @channel: the channel assigned by the OS
 * @id: the id assigned by the OS
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in megaraid_sas_fusion.c)
 * @smid_task: smid assigned to the task
 * @m_type: TM_MUTEX_ON or TM_MUTEX_OFF
 * Context: user
 *
 * MegaRaid use MPT interface for Task Magement request.
 * A generic API for sending task management requests to firmware.
 *
 * Return SUCCESS or FAILED.
 */
static int
megasas_issue_tm(struct megasas_instance *instance, u16 device_handle,
	uint channel, uint id, u16 smid_task, u8 type,
	struct MR_PRIV_DEVICE *mr_device_priv_data)
{
	struct MR_TASK_MANAGE_REQUEST *mr_request;
	struct MPI2_SCSI_TASK_MANAGE_REQUEST *mpi_request;
	unsigned long timeleft;
	struct megasas_cmd_fusion *cmd_fusion;
	struct megasas_cmd *cmd_mfi;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	struct fusion_context *fusion = NULL;
	struct megasas_cmd_fusion *scsi_lookup;
	int rc;
	int timeout = MEGASAS_DEFAULT_TM_TIMEOUT;
	struct MPI2_SCSI_TASK_MANAGE_REPLY *mpi_reply;

	fusion = instance->ctrl_context;

	cmd_mfi = megasas_get_cmd(instance);

	if (!cmd_mfi) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	cmd_fusion = megasas_get_cmd_fusion(instance,
			instance->max_scsi_cmds + cmd_mfi->index);

	/*  Save the smid. To be used for returning the cmd */
	cmd_mfi->context.smid = cmd_fusion->index;

	req_desc = megasas_get_request_descriptor(instance,
			(cmd_fusion->index - 1));

	cmd_fusion->request_desc = req_desc;
	req_desc->Words = 0;

	mr_request = (struct MR_TASK_MANAGE_REQUEST *) cmd_fusion->io_request;
	memset(mr_request, 0, sizeof(struct MR_TASK_MANAGE_REQUEST));
	mpi_request = (struct MPI2_SCSI_TASK_MANAGE_REQUEST *) &mr_request->TmRequest;
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(device_handle);
	mpi_request->TaskType = type;
	mpi_request->TaskMID = cpu_to_le16(smid_task);
	mpi_request->LUN[1] = 0;


	req_desc = cmd_fusion->request_desc;
	req_desc->HighPriority.SMID = cpu_to_le16(cmd_fusion->index);
	req_desc->HighPriority.RequestFlags =
		(MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY <<
		MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	req_desc->HighPriority.MSIxIndex =  0;
	req_desc->HighPriority.LMID = 0;
	req_desc->HighPriority.Reserved1 = 0;

	if (channel < MEGASAS_MAX_PD_CHANNELS)
		mr_request->tmReqFlags.isTMForPD = 1;
	else
		mr_request->tmReqFlags.isTMForLD = 1;

	init_completion(&cmd_fusion->done);
	megasas_fire_cmd_fusion(instance, req_desc);

	switch (type) {
	case MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		timeout = mr_device_priv_data->task_abort_tmo;
		break;
	case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		timeout = mr_device_priv_data->target_reset_tmo;
		break;
	}

	timeleft = wait_for_completion_timeout(&cmd_fusion->done, timeout * HZ);

	if (!timeleft) {
		dev_err(&instance->pdev->dev,
			"task mgmt type 0x%x timed out\n", type);
		cmd_mfi->flags |= DRV_DCMD_SKIP_REFIRE;
		mutex_unlock(&instance->reset_mutex);
		rc = megasas_reset_fusion(instance->host, MFI_IO_TIMEOUT_OCR);
		mutex_lock(&instance->reset_mutex);
		return rc;
	}

	mpi_reply = (struct MPI2_SCSI_TASK_MANAGE_REPLY *) &mr_request->TMReply;
	megasas_tm_response_code(instance, mpi_reply);

	megasas_return_cmd(instance, cmd_mfi);
	rc = SUCCESS;
	switch (type) {
	case MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		scsi_lookup = fusion->cmd_list[smid_task - 1];

		if (scsi_lookup->scmd == NULL)
			break;
		else {
			instance->instancet->disable_intr(instance);
			megasas_sync_irqs((unsigned long)instance);
			instance->instancet->enable_intr(instance);
			megasas_enable_irq_poll(instance);
			if (scsi_lookup->scmd == NULL)
				break;
		}
		rc = FAILED;
		break;

	case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		if ((channel == 0xFFFFFFFF) && (id == 0xFFFFFFFF))
			break;
		instance->instancet->disable_intr(instance);
		megasas_sync_irqs((unsigned long)instance);
		rc = megasas_track_scsiio(instance, id, channel);
		instance->instancet->enable_intr(instance);
		megasas_enable_irq_poll(instance);

		break;
	case MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK:
		break;
	default:
		rc = FAILED;
		break;
	}

	return rc;

}

/*
 * megasas_fusion_smid_lookup : Look for fusion command correpspodning to SCSI
 * @instance: per adapter struct
 *
 * Return Non Zero index, if SMID found in outstanding commands
 */
static u16 megasas_fusion_smid_lookup(struct scsi_cmnd *scmd)
{
	int i, ret = 0;
	struct megasas_instance *instance;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	fusion = instance->ctrl_context;

	for (i = 0; i < instance->max_scsi_cmds; i++) {
		cmd_fusion = fusion->cmd_list[i];
		if (cmd_fusion->scmd && (cmd_fusion->scmd == scmd)) {
			scmd_printk(KERN_NOTICE, scmd, "Abort request is for"
				" SMID: %d\n", cmd_fusion->index);
			ret = cmd_fusion->index;
			break;
		}
	}

	return ret;
}

/*
* megasas_get_tm_devhandle - Get devhandle for TM request
* @sdev-		     OS provided scsi device
*
* Returns-		     devhandle/targetID of SCSI device
*/
static u16 megasas_get_tm_devhandle(struct scsi_device *sdev)
{
	u16 pd_index = 0;
	u32 device_id;
	struct megasas_instance *instance;
	struct fusion_context *fusion;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	u16 devhandle = (u16)ULONG_MAX;

	instance = (struct megasas_instance *)sdev->host->hostdata;
	fusion = instance->ctrl_context;

	if (!MEGASAS_IS_LOGICAL(sdev)) {
		if (instance->use_seqnum_jbod_fp) {
			pd_index = (sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL)
				    + sdev->id;
			pd_sync = (void *)fusion->pd_seq_sync
					[(instance->pd_seq_map_id - 1) & 1];
			devhandle = pd_sync->seq[pd_index].devHandle;
		} else
			sdev_printk(KERN_ERR, sdev, "Firmware expose tmCapable"
				" without JBOD MAP support from %s %d\n", __func__, __LINE__);
	} else {
		device_id = ((sdev->channel % 2) * MEGASAS_MAX_DEV_PER_CHANNEL)
				+ sdev->id;
		devhandle = device_id;
	}

	return devhandle;
}

/*
 * megasas_task_abort_fusion : SCSI task abort function for fusion adapters
 * @scmd : pointer to scsi command object
 *
 * Return SUCCESS, if command aborted else FAILED
 */

int megasas_task_abort_fusion(struct scsi_cmnd *scmd)
{
	struct megasas_instance *instance;
	u16 smid, devhandle;
	int ret;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	mr_device_priv_data = scmd->device->hostdata;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) {
		dev_err(&instance->pdev->dev, "Controller is not OPERATIONAL,"
		"SCSI host:%d\n", instance->host->host_no);
		ret = FAILED;
		return ret;
	}

	if (!mr_device_priv_data) {
		sdev_printk(KERN_INFO, scmd->device, "device been deleted! "
			"scmd(%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		ret = SUCCESS;
		goto out;
	}

	if (!mr_device_priv_data->is_tm_capable) {
		ret = FAILED;
		goto out;
	}

	mutex_lock(&instance->reset_mutex);

	smid = megasas_fusion_smid_lookup(scmd);

	if (!smid) {
		ret = SUCCESS;
		scmd_printk(KERN_NOTICE, scmd, "Command for which abort is"
			" issued is not found in outstanding commands\n");
		mutex_unlock(&instance->reset_mutex);
		goto out;
	}

	devhandle = megasas_get_tm_devhandle(scmd->device);

	if (devhandle == (u16)ULONG_MAX) {
		ret = SUCCESS;
		sdev_printk(KERN_INFO, scmd->device,
			"task abort issued for invalid devhandle\n");
		mutex_unlock(&instance->reset_mutex);
		goto out;
	}
	sdev_printk(KERN_INFO, scmd->device,
		"attempting task abort! scmd(0x%p) tm_dev_handle 0x%x\n",
		scmd, devhandle);

	mr_device_priv_data->tm_busy = 1;
	ret = megasas_issue_tm(instance, devhandle,
			scmd->device->channel, scmd->device->id, smid,
			MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK,
			mr_device_priv_data);
	mr_device_priv_data->tm_busy = 0;

	mutex_unlock(&instance->reset_mutex);
	scmd_printk(KERN_INFO, scmd, "task abort %s!! scmd(0x%p)\n",
			((ret == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
out:
	scsi_print_command(scmd);
	if (megasas_dbg_lvl & TM_DEBUG)
		megasas_dump_fusion_io(scmd);

	return ret;
}

/*
 * megasas_reset_target_fusion : target reset function for fusion adapters
 * scmd: SCSI command pointer
 *
 * Returns SUCCESS if all commands associated with target aborted else FAILED
 */

int megasas_reset_target_fusion(struct scsi_cmnd *scmd)
{

	struct megasas_instance *instance;
	int ret = FAILED;
	u16 devhandle;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	mr_device_priv_data = scmd->device->hostdata;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;

	if (atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) {
		dev_err(&instance->pdev->dev, "Controller is not OPERATIONAL,"
		"SCSI host:%d\n", instance->host->host_no);
		ret = FAILED;
		return ret;
	}

	if (!mr_device_priv_data) {
		sdev_printk(KERN_INFO, scmd->device,
			    "device been deleted! scmd: (0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		ret = SUCCESS;
		goto out;
	}

	if (!mr_device_priv_data->is_tm_capable) {
		ret = FAILED;
		goto out;
	}

	mutex_lock(&instance->reset_mutex);
	devhandle = megasas_get_tm_devhandle(scmd->device);

	if (devhandle == (u16)ULONG_MAX) {
		ret = SUCCESS;
		sdev_printk(KERN_INFO, scmd->device,
			"target reset issued for invalid devhandle\n");
		mutex_unlock(&instance->reset_mutex);
		goto out;
	}

	sdev_printk(KERN_INFO, scmd->device,
		"attempting target reset! scmd(0x%p) tm_dev_handle: 0x%x\n",
		scmd, devhandle);
	mr_device_priv_data->tm_busy = 1;
	ret = megasas_issue_tm(instance, devhandle,
			scmd->device->channel, scmd->device->id, 0,
			MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
			mr_device_priv_data);
	mr_device_priv_data->tm_busy = 0;
	mutex_unlock(&instance->reset_mutex);
	scmd_printk(KERN_NOTICE, scmd, "target reset %s!!\n",
		(ret == SUCCESS) ? "SUCCESS" : "FAILED");

out:
	return ret;
}

/*SRIOV get other instance in cluster if any*/
static struct
megasas_instance *megasas_get_peer_instance(struct megasas_instance *instance)
{
	int i;

	for (i = 0; i < MAX_MGMT_ADAPTERS; i++) {
		if (megasas_mgmt_info.instance[i] &&
			(megasas_mgmt_info.instance[i] != instance) &&
			 megasas_mgmt_info.instance[i]->requestorId &&
			 megasas_mgmt_info.instance[i]->peerIsPresent &&
			(memcmp((megasas_mgmt_info.instance[i]->clusterId),
			instance->clusterId, MEGASAS_CLUSTER_ID_SIZE) == 0))
			return megasas_mgmt_info.instance[i];
	}
	return NULL;
}

/* Check for a second path that is currently UP */
int megasas_check_mpio_paths(struct megasas_instance *instance,
	struct scsi_cmnd *scmd)
{
	struct megasas_instance *peer_instance = NULL;
	int retval = (DID_REQUEUE << 16);

	if (instance->peerIsPresent) {
		peer_instance = megasas_get_peer_instance(instance);
		if ((peer_instance) &&
			(atomic_read(&peer_instance->adprecovery) ==
			MEGASAS_HBA_OPERATIONAL))
			retval = (DID_NO_CONNECT << 16);
	}
	return retval;
}

/* Core fusion reset function */
int megasas_reset_fusion(struct Scsi_Host *shost, int reason)
{
	int retval = SUCCESS, i, j, convert = 0;
	struct megasas_instance *instance;
	struct megasas_cmd_fusion *cmd_fusion, *r1_cmd;
	struct fusion_context *fusion;
	u32 abs_state, status_reg, reset_adapter, fpio_count = 0;
	u32 io_timeout_in_crash_mode = 0;
	struct scsi_cmnd *scmd_local = NULL;
	struct scsi_device *sdev;
	int ret_target_prop = DCMD_FAILED;
	bool is_target_prop = false;
	bool do_adp_reset = true;
	int max_reset_tries = MEGASAS_FUSION_MAX_RESET_TRIES;

	instance = (struct megasas_instance *)shost->hostdata;
	fusion = instance->ctrl_context;

	mutex_lock(&instance->reset_mutex);

	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		dev_warn(&instance->pdev->dev, "Hardware critical error, "
		       "returning FAILED for scsi%d.\n",
			instance->host->host_no);
		mutex_unlock(&instance->reset_mutex);
		return FAILED;
	}
	status_reg = instance->instancet->read_fw_status_reg(instance);
	abs_state = status_reg & MFI_STATE_MASK;

	/* IO timeout detected, forcibly put FW in FAULT state */
	if (abs_state != MFI_STATE_FAULT && instance->crash_dump_buf &&
		instance->crash_dump_app_support && reason) {
		dev_info(&instance->pdev->dev, "IO/DCMD timeout is detected, "
			"forcibly FAULT Firmware\n");
		atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
		status_reg = megasas_readl(instance, &instance->reg_set->doorbell);
		writel(status_reg | MFI_STATE_FORCE_OCR,
			&instance->reg_set->doorbell);
		readl(&instance->reg_set->doorbell);
		mutex_unlock(&instance->reset_mutex);
		do {
			ssleep(3);
			io_timeout_in_crash_mode++;
			dev_dbg(&instance->pdev->dev, "waiting for [%d] "
				"seconds for crash dump collection and OCR "
				"to be done\n", (io_timeout_in_crash_mode * 3));
		} while ((atomic_read(&instance->adprecovery) != MEGASAS_HBA_OPERATIONAL) &&
			(io_timeout_in_crash_mode < 80));

		if (atomic_read(&instance->adprecovery) == MEGASAS_HBA_OPERATIONAL) {
			dev_info(&instance->pdev->dev, "OCR done for IO "
				"timeout case\n");
			retval = SUCCESS;
		} else {
			dev_info(&instance->pdev->dev, "Controller is not "
				"operational after 240 seconds wait for IO "
				"timeout case in FW crash dump mode\n do "
				"OCR/kill adapter\n");
			retval = megasas_reset_fusion(shost, 0);
		}
		return retval;
	}

	if (instance->requestorId && !instance->skip_heartbeat_timer_del)
		del_timer_sync(&instance->sriov_heartbeat_timer);
	set_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
	set_bit(MEGASAS_FUSION_OCR_NOT_POSSIBLE, &instance->reset_flags);
	atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_POLLING);
	instance->instancet->disable_intr(instance);
	megasas_sync_irqs((unsigned long)instance);

	/* First try waiting for commands to complete */
	if (megasas_wait_for_outstanding_fusion(instance, reason,
						&convert)) {
		atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
		dev_warn(&instance->pdev->dev, "resetting fusion "
		       "adapter scsi%d.\n", instance->host->host_no);
		if (convert)
			reason = 0;

		if (megasas_dbg_lvl & OCR_DEBUG)
			dev_info(&instance->pdev->dev, "\nPending SCSI commands:\n");

		/* Now return commands back to the OS */
		for (i = 0 ; i < instance->max_scsi_cmds; i++) {
			cmd_fusion = fusion->cmd_list[i];
			/*check for extra commands issued by driver*/
			if (instance->adapter_type >= VENTURA_SERIES) {
				r1_cmd = fusion->cmd_list[i + instance->max_fw_cmds];
				megasas_return_cmd_fusion(instance, r1_cmd);
			}
			scmd_local = cmd_fusion->scmd;
			if (cmd_fusion->scmd) {
				if (megasas_dbg_lvl & OCR_DEBUG) {
					sdev_printk(KERN_INFO,
						cmd_fusion->scmd->device, "SMID: 0x%x\n",
						cmd_fusion->index);
					megasas_dump_fusion_io(cmd_fusion->scmd);
				}

				if (cmd_fusion->io_request->Function ==
					MPI2_FUNCTION_SCSI_IO_REQUEST)
					fpio_count++;

				scmd_local->result =
					megasas_check_mpio_paths(instance,
							scmd_local);
				if (instance->ldio_threshold &&
					megasas_cmd_type(scmd_local) == READ_WRITE_LDIO)
					atomic_dec(&instance->ldio_outstanding);
				megasas_return_cmd_fusion(instance, cmd_fusion);
				scsi_dma_unmap(scmd_local);
				scmd_local->scsi_done(scmd_local);
			}
		}

		dev_info(&instance->pdev->dev, "Outstanding fastpath IOs: %d\n",
			fpio_count);

		atomic_set(&instance->fw_outstanding, 0);

		status_reg = instance->instancet->read_fw_status_reg(instance);
		abs_state = status_reg & MFI_STATE_MASK;
		reset_adapter = status_reg & MFI_RESET_ADAPTER;
		if (instance->disableOnlineCtrlReset ||
		    (abs_state == MFI_STATE_FAULT && !reset_adapter)) {
			/* Reset not supported, kill adapter */
			dev_warn(&instance->pdev->dev, "Reset not supported"
			       ", killing adapter scsi%d.\n",
				instance->host->host_no);
			goto kill_hba;
		}

		/* Let SR-IOV VF & PF sync up if there was a HB failure */
		if (instance->requestorId && !reason) {
			msleep(MEGASAS_OCR_SETTLE_TIME_VF);
			do_adp_reset = false;
			max_reset_tries = MEGASAS_SRIOV_MAX_RESET_TRIES_VF;
		}

		/* Now try to reset the chip */
		for (i = 0; i < max_reset_tries; i++) {
			/*
			 * Do adp reset and wait for
			 * controller to transition to ready
			 */
			if (megasas_adp_reset_wait_for_ready(instance,
				do_adp_reset, 1) == FAILED)
				continue;

			/* Wait for FW to become ready */
			if (megasas_transition_to_ready(instance, 1)) {
				dev_warn(&instance->pdev->dev,
					"Failed to transition controller to ready for "
					"scsi%d.\n", instance->host->host_no);
				continue;
			}
			megasas_reset_reply_desc(instance);
			megasas_fusion_update_can_queue(instance, OCR_CONTEXT);

			if (megasas_ioc_init_fusion(instance)) {
				continue;
			}

			if (megasas_get_ctrl_info(instance)) {
				dev_info(&instance->pdev->dev,
					"Failed from %s %d\n",
					__func__, __LINE__);
				goto kill_hba;
			}

			megasas_refire_mgmt_cmd(instance,
						(i == (MEGASAS_FUSION_MAX_RESET_TRIES - 1)
							? 1 : 0));

			/* Reset load balance info */
			if (fusion->load_balance_info)
				memset(fusion->load_balance_info, 0,
				       (sizeof(struct LD_LOAD_BALANCE_INFO) *
				       MAX_LOGICAL_DRIVES_EXT));

			if (!megasas_get_map_info(instance)) {
				megasas_sync_map_info(instance);
			} else {
				/*
				 * Return pending polled mode cmds before
				 * retrying OCR
				 */
				megasas_return_polled_cmds(instance);
				continue;
			}

			megasas_setup_jbod_map(instance);

			/* reset stream detection array */
			if (instance->adapter_type >= VENTURA_SERIES) {
				for (j = 0; j < MAX_LOGICAL_DRIVES_EXT; ++j) {
					memset(fusion->stream_detect_by_ld[j],
					0, sizeof(struct LD_STREAM_DETECT));
				 fusion->stream_detect_by_ld[j]->mru_bit_map
						= MR_STREAM_BITMAP;
				}
			}

			clear_bit(MEGASAS_FUSION_IN_RESET,
				  &instance->reset_flags);
			instance->instancet->enable_intr(instance);
			megasas_enable_irq_poll(instance);
			shost_for_each_device(sdev, shost) {
				if ((instance->tgt_prop) &&
				    (instance->nvme_page_size))
					ret_target_prop = megasas_get_target_prop(instance, sdev);

				is_target_prop = (ret_target_prop == DCMD_SUCCESS) ? true : false;
				megasas_set_dynamic_target_properties(sdev, is_target_prop);
			}

			status_reg = instance->instancet->read_fw_status_reg
					(instance);
			abs_state = status_reg & MFI_STATE_MASK;
			if (abs_state != MFI_STATE_OPERATIONAL) {
				dev_info(&instance->pdev->dev,
					 "Adapter is not OPERATIONAL, state 0x%x for scsi:%d\n",
					 abs_state, instance->host->host_no);
				goto out;
			}
			atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);

			dev_info(&instance->pdev->dev,
				 "Adapter is OPERATIONAL for scsi:%d\n",
				 instance->host->host_no);

			/* Restart SR-IOV heartbeat */
			if (instance->requestorId) {
				if (!megasas_sriov_start_heartbeat(instance, 0))
					megasas_start_timer(instance);
				else
					instance->skip_heartbeat_timer_del = 1;
			}

			if (instance->crash_dump_drv_support &&
				instance->crash_dump_app_support)
				megasas_set_crash_dump_params(instance,
					MR_CRASH_BUF_TURN_ON);
			else
				megasas_set_crash_dump_params(instance,
					MR_CRASH_BUF_TURN_OFF);

			if (instance->snapdump_wait_time) {
				megasas_get_snapdump_properties(instance);
				dev_info(&instance->pdev->dev,
					 "Snap dump wait time\t: %d\n",
					 instance->snapdump_wait_time);
			}

			retval = SUCCESS;

			/* Adapter reset completed successfully */
			dev_warn(&instance->pdev->dev,
				 "Reset successful for scsi%d.\n",
				 instance->host->host_no);

			goto out;
		}
		/* Reset failed, kill the adapter */
		dev_warn(&instance->pdev->dev, "Reset failed, killing "
		       "adapter scsi%d.\n", instance->host->host_no);
		goto kill_hba;
	} else {
		/* For VF: Restart HB timer if we didn't OCR */
		if (instance->requestorId) {
			megasas_start_timer(instance);
		}
		clear_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
		instance->instancet->enable_intr(instance);
		megasas_enable_irq_poll(instance);
		atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);
		goto out;
	}
kill_hba:
	megaraid_sas_kill_hba(instance);
	megasas_enable_irq_poll(instance);
	instance->skip_heartbeat_timer_del = 1;
	retval = FAILED;
out:
	clear_bit(MEGASAS_FUSION_OCR_NOT_POSSIBLE, &instance->reset_flags);
	mutex_unlock(&instance->reset_mutex);
	return retval;
}

/* Fusion Crash dump collection */
static void  megasas_fusion_crash_dump(struct megasas_instance *instance)
{
	u32 status_reg;
	u8 partial_copy = 0;
	int wait = 0;


	status_reg = instance->instancet->read_fw_status_reg(instance);

	/*
	 * Allocate host crash buffers to copy data from 1 MB DMA crash buffer
	 * to host crash buffers
	 */
	if (instance->drv_buf_index == 0) {
		/* Buffer is already allocated for old Crash dump.
		 * Do OCR and do not wait for crash dump collection
		 */
		if (instance->drv_buf_alloc) {
			dev_info(&instance->pdev->dev, "earlier crash dump is "
				"not yet copied by application, ignoring this "
				"crash dump and initiating OCR\n");
			status_reg |= MFI_STATE_CRASH_DUMP_DONE;
			writel(status_reg,
				&instance->reg_set->outbound_scratch_pad_0);
			readl(&instance->reg_set->outbound_scratch_pad_0);
			return;
		}
		megasas_alloc_host_crash_buffer(instance);
		dev_info(&instance->pdev->dev, "Number of host crash buffers "
			"allocated: %d\n", instance->drv_buf_alloc);
	}

	while (!(status_reg & MFI_STATE_CRASH_DUMP_DONE) &&
	       (wait < MEGASAS_WATCHDOG_WAIT_COUNT)) {
		if (!(status_reg & MFI_STATE_DMADONE)) {
			/*
			 * Next crash dump buffer is not yet DMA'd by FW
			 * Check after 10ms. Wait for 1 second for FW to
			 * post the next buffer. If not bail out.
			 */
			wait++;
			msleep(MEGASAS_WAIT_FOR_NEXT_DMA_MSECS);
			status_reg = instance->instancet->read_fw_status_reg(
					instance);
			continue;
		}

		wait = 0;
		if (instance->drv_buf_index >= instance->drv_buf_alloc) {
			dev_info(&instance->pdev->dev,
				 "Driver is done copying the buffer: %d\n",
				 instance->drv_buf_alloc);
			status_reg |= MFI_STATE_CRASH_DUMP_DONE;
			partial_copy = 1;
			break;
		} else {
			memcpy(instance->crash_buf[instance->drv_buf_index],
			       instance->crash_dump_buf, CRASH_DMA_BUF_SIZE);
			instance->drv_buf_index++;
			status_reg &= ~MFI_STATE_DMADONE;
		}

		writel(status_reg, &instance->reg_set->outbound_scratch_pad_0);
		readl(&instance->reg_set->outbound_scratch_pad_0);

		msleep(MEGASAS_WAIT_FOR_NEXT_DMA_MSECS);
		status_reg = instance->instancet->read_fw_status_reg(instance);
	}

	if (status_reg & MFI_STATE_CRASH_DUMP_DONE) {
		dev_info(&instance->pdev->dev, "Crash Dump is available,number "
			"of copied buffers: %d\n", instance->drv_buf_index);
		instance->fw_crash_buffer_size =  instance->drv_buf_index;
		instance->fw_crash_state = AVAILABLE;
		instance->drv_buf_index = 0;
		writel(status_reg, &instance->reg_set->outbound_scratch_pad_0);
		readl(&instance->reg_set->outbound_scratch_pad_0);
		if (!partial_copy)
			megasas_reset_fusion(instance->host, 0);
	}
}


/* Fusion OCR work queue */
void megasas_fusion_ocr_wq(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance, work_init);

	megasas_reset_fusion(instance->host, 0);
}

/* Allocate fusion context */
int
megasas_alloc_fusion_context(struct megasas_instance *instance)
{
	struct fusion_context *fusion;

	instance->ctrl_context = kzalloc(sizeof(struct fusion_context),
					 GFP_KERNEL);
	if (!instance->ctrl_context) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	fusion = instance->ctrl_context;

	fusion->log_to_span_pages = get_order(MAX_LOGICAL_DRIVES_EXT *
					      sizeof(LD_SPAN_INFO));
	fusion->log_to_span =
		(PLD_SPAN_INFO)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						fusion->log_to_span_pages);
	if (!fusion->log_to_span) {
		fusion->log_to_span =
			vzalloc(array_size(MAX_LOGICAL_DRIVES_EXT,
					   sizeof(LD_SPAN_INFO)));
		if (!fusion->log_to_span) {
			dev_err(&instance->pdev->dev, "Failed from %s %d\n",
				__func__, __LINE__);
			return -ENOMEM;
		}
	}

	fusion->load_balance_info_pages = get_order(MAX_LOGICAL_DRIVES_EXT *
		sizeof(struct LD_LOAD_BALANCE_INFO));
	fusion->load_balance_info =
		(struct LD_LOAD_BALANCE_INFO *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
		fusion->load_balance_info_pages);
	if (!fusion->load_balance_info) {
		fusion->load_balance_info =
			vzalloc(array_size(MAX_LOGICAL_DRIVES_EXT,
					   sizeof(struct LD_LOAD_BALANCE_INFO)));
		if (!fusion->load_balance_info)
			dev_err(&instance->pdev->dev, "Failed to allocate load_balance_info, "
				"continuing without Load Balance support\n");
	}

	return 0;
}

void
megasas_free_fusion_context(struct megasas_instance *instance)
{
	struct fusion_context *fusion = instance->ctrl_context;

	if (fusion) {
		if (fusion->load_balance_info) {
			if (is_vmalloc_addr(fusion->load_balance_info))
				vfree(fusion->load_balance_info);
			else
				free_pages((ulong)fusion->load_balance_info,
					fusion->load_balance_info_pages);
		}

		if (fusion->log_to_span) {
			if (is_vmalloc_addr(fusion->log_to_span))
				vfree(fusion->log_to_span);
			else
				free_pages((ulong)fusion->log_to_span,
					   fusion->log_to_span_pages);
		}

		kfree(fusion);
	}
}

struct megasas_instance_template megasas_instance_template_fusion = {
	.enable_intr = megasas_enable_intr_fusion,
	.disable_intr = megasas_disable_intr_fusion,
	.clear_intr = megasas_clear_intr_fusion,
	.read_fw_status_reg = megasas_read_fw_status_reg_fusion,
	.adp_reset = megasas_adp_reset_fusion,
	.check_reset = megasas_check_reset_fusion,
	.service_isr = megasas_isr_fusion,
	.tasklet = megasas_complete_cmd_dpc_fusion,
	.init_adapter = megasas_init_adapter_fusion,
	.build_and_issue_cmd = megasas_build_and_issue_cmd_fusion,
	.issue_dcmd = megasas_issue_dcmd_fusion,
};
