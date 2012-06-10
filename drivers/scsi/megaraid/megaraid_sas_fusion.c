/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2011  LSI Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *  FILE: megaraid_sas_fusion.c
 *
 *  Authors: LSI Corporation
 *           Sumant Patro
 *           Adam Radford <linuxraid@lsi.com>
 *
 *  Send feedback to: <megaraidlinux@lsi.com>
 *
 *  Mail to: LSI Corporation, 1621 Barber Lane, Milpitas, CA 95035
 *     ATTN: Linuxraid
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

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "megaraid_sas_fusion.h"
#include "megaraid_sas.h"

extern void megasas_free_cmds(struct megasas_instance *instance);
extern struct megasas_cmd *megasas_get_cmd(struct megasas_instance
					   *instance);
extern void
megasas_complete_cmd(struct megasas_instance *instance,
		     struct megasas_cmd *cmd, u8 alt_status);
int megasas_is_ldio(struct scsi_cmnd *cmd);
int
wait_and_poll(struct megasas_instance *instance, struct megasas_cmd *cmd);

void
megasas_return_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd);
int megasas_alloc_cmds(struct megasas_instance *instance);
int
megasas_clear_intr_fusion(struct megasas_register_set __iomem *regs);
int
megasas_issue_polled(struct megasas_instance *instance,
		     struct megasas_cmd *cmd);

u8
MR_BuildRaidContext(struct megasas_instance *instance,
		    struct IO_REQUEST_INFO *io_info,
		    struct RAID_CONTEXT *pRAID_Context,
		    struct MR_FW_RAID_MAP_ALL *map);
u16 MR_TargetIdToLdGet(u32 ldTgtId, struct MR_FW_RAID_MAP_ALL *map);
struct MR_LD_RAID *MR_LdRaidGet(u32 ld, struct MR_FW_RAID_MAP_ALL *map);

u16 MR_GetLDTgtId(u32 ld, struct MR_FW_RAID_MAP_ALL *map);

void
megasas_check_and_restore_queue_depth(struct megasas_instance *instance);

u8 MR_ValidateMapInfo(struct MR_FW_RAID_MAP_ALL *map,
		      struct LD_LOAD_BALANCE_INFO *lbInfo);
u16 get_updated_dev_handle(struct LD_LOAD_BALANCE_INFO *lbInfo,
			   struct IO_REQUEST_INFO *in_info);
int megasas_transition_to_ready(struct megasas_instance *instance, int ocr);
void megaraid_sas_kill_hba(struct megasas_instance *instance);

extern u32 megasas_dbg_lvl;

/**
 * megasas_enable_intr_fusion -	Enables interrupts
 * @regs:			MFI register set
 */
void
megasas_enable_intr_fusion(struct megasas_register_set __iomem *regs)
{
	/* For Thunderbolt/Invader also clear intr on enable */
	writel(~0, &regs->outbound_intr_status);
	readl(&regs->outbound_intr_status);

	writel(~MFI_FUSION_ENABLE_INTERRUPT_MASK, &(regs)->outbound_intr_mask);

	/* Dummy readl to force pci flush */
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_fusion - Disables interrupt
 * @regs:			 MFI register set
 */
void
megasas_disable_intr_fusion(struct megasas_register_set __iomem *regs)
{
	u32 mask = 0xFFFFFFFF;
	u32 status;

	writel(mask, &regs->outbound_intr_mask);
	/* Dummy readl to force pci flush */
	status = readl(&regs->outbound_intr_mask);
}

int
megasas_clear_intr_fusion(struct megasas_register_set __iomem *regs)
{
	u32 status;
	/*
	 * Check if it is our interrupt
	 */
	status = readl(&regs->outbound_intr_status);

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
 * Returns a free command from the pool
 */
struct megasas_cmd_fusion *megasas_get_cmd_fusion(struct megasas_instance
						  *instance)
{
	unsigned long flags;
	struct fusion_context *fusion =
		(struct fusion_context *)instance->ctrl_context;
	struct megasas_cmd_fusion *cmd = NULL;

	spin_lock_irqsave(&fusion->cmd_pool_lock, flags);

	if (!list_empty(&fusion->cmd_pool)) {
		cmd = list_entry((&fusion->cmd_pool)->next,
				 struct megasas_cmd_fusion, list);
		list_del_init(&cmd->list);
	} else {
		printk(KERN_ERR "megasas: Command pool (fusion) empty!\n");
	}

	spin_unlock_irqrestore(&fusion->cmd_pool_lock, flags);
	return cmd;
}

/**
 * megasas_return_cmd_fusion -	Return a cmd to free command pool
 * @instance:		Adapter soft state
 * @cmd:		Command packet to be returned to free command pool
 */
static inline void
megasas_return_cmd_fusion(struct megasas_instance *instance,
			  struct megasas_cmd_fusion *cmd)
{
	unsigned long flags;
	struct fusion_context *fusion =
		(struct fusion_context *)instance->ctrl_context;

	spin_lock_irqsave(&fusion->cmd_pool_lock, flags);

	cmd->scmd = NULL;
	cmd->sync_cmd_idx = (u32)ULONG_MAX;
	list_add_tail(&cmd->list, &fusion->cmd_pool);

	spin_unlock_irqrestore(&fusion->cmd_pool_lock, flags);
}

/**
 * megasas_teardown_frame_pool_fusion -	Destroy the cmd frame DMA pool
 * @instance:				Adapter soft state
 */
static void megasas_teardown_frame_pool_fusion(
	struct megasas_instance *instance)
{
	int i;
	struct fusion_context *fusion = instance->ctrl_context;

	u16 max_cmd = instance->max_fw_cmds;

	struct megasas_cmd_fusion *cmd;

	if (!fusion->sg_dma_pool || !fusion->sense_dma_pool) {
		printk(KERN_ERR "megasas: dma pool is null. SG Pool %p, "
		       "sense pool : %p\n", fusion->sg_dma_pool,
		       fusion->sense_dma_pool);
		return;
	}

	/*
	 * Return all frames to pool
	 */
	for (i = 0; i < max_cmd; i++) {

		cmd = fusion->cmd_list[i];

		if (cmd->sg_frame)
			pci_pool_free(fusion->sg_dma_pool, cmd->sg_frame,
				      cmd->sg_frame_phys_addr);

		if (cmd->sense)
			pci_pool_free(fusion->sense_dma_pool, cmd->sense,
				      cmd->sense_phys_addr);
	}

	/*
	 * Now destroy the pool itself
	 */
	pci_pool_destroy(fusion->sg_dma_pool);
	pci_pool_destroy(fusion->sense_dma_pool);

	fusion->sg_dma_pool = NULL;
	fusion->sense_dma_pool = NULL;
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

	u32 max_cmds, req_sz, reply_sz, io_frames_sz;


	req_sz = fusion->request_alloc_sz;
	reply_sz = fusion->reply_alloc_sz;
	io_frames_sz = fusion->io_frames_alloc_sz;

	max_cmds = instance->max_fw_cmds;

	/* Free descriptors and request Frames memory */
	if (fusion->req_frames_desc)
		dma_free_coherent(&instance->pdev->dev, req_sz,
				  fusion->req_frames_desc,
				  fusion->req_frames_desc_phys);

	if (fusion->reply_frames_desc) {
		pci_pool_free(fusion->reply_frames_desc_pool,
			      fusion->reply_frames_desc,
			      fusion->reply_frames_desc_phys);
		pci_pool_destroy(fusion->reply_frames_desc_pool);
	}

	if (fusion->io_request_frames) {
		pci_pool_free(fusion->io_request_frames_pool,
			      fusion->io_request_frames,
			      fusion->io_request_frames_phys);
		pci_pool_destroy(fusion->io_request_frames_pool);
	}

	/* Free the Fusion frame pool */
	megasas_teardown_frame_pool_fusion(instance);

	/* Free all the commands in the cmd_list */
	for (i = 0; i < max_cmds; i++)
		kfree(fusion->cmd_list[i]);

	/* Free the cmd_list buffer itself */
	kfree(fusion->cmd_list);
	fusion->cmd_list = NULL;

	INIT_LIST_HEAD(&fusion->cmd_pool);
}

/**
 * megasas_create_frame_pool_fusion -	Creates DMA pool for cmd frames
 * @instance:			Adapter soft state
 *
 */
static int megasas_create_frame_pool_fusion(struct megasas_instance *instance)
{
	int i;
	u32 max_cmd;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;
	u32 total_sz_chain_frame;

	fusion = instance->ctrl_context;
	max_cmd = instance->max_fw_cmds;

	total_sz_chain_frame = MEGASAS_MAX_SZ_CHAIN_FRAME;

	/*
	 * Use DMA pool facility provided by PCI layer
	 */

	fusion->sg_dma_pool = pci_pool_create("megasas sg pool fusion",
					      instance->pdev,
					      total_sz_chain_frame, 4,
					      0);
	if (!fusion->sg_dma_pool) {
		printk(KERN_DEBUG "megasas: failed to setup request pool "
		       "fusion\n");
		return -ENOMEM;
	}
	fusion->sense_dma_pool = pci_pool_create("megasas sense pool fusion",
						 instance->pdev,
						 SCSI_SENSE_BUFFERSIZE, 64, 0);

	if (!fusion->sense_dma_pool) {
		printk(KERN_DEBUG "megasas: failed to setup sense pool "
		       "fusion\n");
		pci_pool_destroy(fusion->sg_dma_pool);
		fusion->sg_dma_pool = NULL;
		return -ENOMEM;
	}

	/*
	 * Allocate and attach a frame to each of the commands in cmd_list
	 */
	for (i = 0; i < max_cmd; i++) {

		cmd = fusion->cmd_list[i];

		cmd->sg_frame = pci_pool_alloc(fusion->sg_dma_pool,
					       GFP_KERNEL,
					       &cmd->sg_frame_phys_addr);

		cmd->sense = pci_pool_alloc(fusion->sense_dma_pool,
					    GFP_KERNEL, &cmd->sense_phys_addr);
		/*
		 * megasas_teardown_frame_pool_fusion() takes care of freeing
		 * whatever has been allocated
		 */
		if (!cmd->sg_frame || !cmd->sense) {
			printk(KERN_DEBUG "megasas: pci_pool_alloc failed\n");
			megasas_teardown_frame_pool_fusion(instance);
			return -ENOMEM;
		}
	}
	return 0;
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
int
megasas_alloc_cmds_fusion(struct megasas_instance *instance)
{
	int i, j, count;
	u32 max_cmd, io_frames_sz;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;
	u32 offset;
	dma_addr_t io_req_base_phys;
	u8 *io_req_base;

	fusion = instance->ctrl_context;

	max_cmd = instance->max_fw_cmds;

	fusion->req_frames_desc =
		dma_alloc_coherent(&instance->pdev->dev,
				   fusion->request_alloc_sz,
				   &fusion->req_frames_desc_phys, GFP_KERNEL);

	if (!fusion->req_frames_desc) {
		printk(KERN_ERR "megasas; Could not allocate memory for "
		       "request_frames\n");
		goto fail_req_desc;
	}

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	fusion->reply_frames_desc_pool =
		pci_pool_create("reply_frames pool", instance->pdev,
				fusion->reply_alloc_sz * count, 16, 0);

	if (!fusion->reply_frames_desc_pool) {
		printk(KERN_ERR "megasas; Could not allocate memory for "
		       "reply_frame pool\n");
		goto fail_reply_desc;
	}

	fusion->reply_frames_desc =
		pci_pool_alloc(fusion->reply_frames_desc_pool, GFP_KERNEL,
			       &fusion->reply_frames_desc_phys);
	if (!fusion->reply_frames_desc) {
		printk(KERN_ERR "megasas; Could not allocate memory for "
		       "reply_frame pool\n");
		pci_pool_destroy(fusion->reply_frames_desc_pool);
		goto fail_reply_desc;
	}

	reply_desc = fusion->reply_frames_desc;
	for (i = 0; i < fusion->reply_q_depth * count; i++, reply_desc++)
		reply_desc->Words = ULLONG_MAX;

	io_frames_sz = fusion->io_frames_alloc_sz;

	fusion->io_request_frames_pool =
		pci_pool_create("io_request_frames pool", instance->pdev,
				fusion->io_frames_alloc_sz, 16, 0);

	if (!fusion->io_request_frames_pool) {
		printk(KERN_ERR "megasas: Could not allocate memory for "
		       "io_request_frame pool\n");
		goto fail_io_frames;
	}

	fusion->io_request_frames =
		pci_pool_alloc(fusion->io_request_frames_pool, GFP_KERNEL,
			       &fusion->io_request_frames_phys);
	if (!fusion->io_request_frames) {
		printk(KERN_ERR "megasas: Could not allocate memory for "
		       "io_request_frames frames\n");
		pci_pool_destroy(fusion->io_request_frames_pool);
		goto fail_io_frames;
	}

	/*
	 * fusion->cmd_list is an array of struct megasas_cmd_fusion pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */
	fusion->cmd_list = kmalloc(sizeof(struct megasas_cmd_fusion *)
				   *max_cmd, GFP_KERNEL);

	if (!fusion->cmd_list) {
		printk(KERN_DEBUG "megasas: out of memory. Could not alloc "
		       "memory for cmd_list_fusion\n");
		goto fail_cmd_list;
	}

	memset(fusion->cmd_list, 0, sizeof(struct megasas_cmd_fusion *)
	       *max_cmd);

	max_cmd = instance->max_fw_cmds;
	for (i = 0; i < max_cmd; i++) {
		fusion->cmd_list[i] = kmalloc(sizeof(struct megasas_cmd_fusion),
					      GFP_KERNEL);
		if (!fusion->cmd_list[i]) {
			printk(KERN_ERR "Could not alloc cmd list fusion\n");

			for (j = 0; j < i; j++)
				kfree(fusion->cmd_list[j]);

			kfree(fusion->cmd_list);
			fusion->cmd_list = NULL;
			goto fail_cmd_list;
		}
	}

	/* The first 256 bytes (SMID 0) is not used. Don't add to cmd list */
	io_req_base = fusion->io_request_frames +
		MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;
	io_req_base_phys = fusion->io_request_frames_phys +
		MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;

	/*
	 * Add all the commands to command pool (fusion->cmd_pool)
	 */

	/* SMID 0 is reserved. Set SMID/index from 1 */
	for (i = 0; i < max_cmd; i++) {
		cmd = fusion->cmd_list[i];
		offset = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE * i;
		memset(cmd, 0, sizeof(struct megasas_cmd_fusion));
		cmd->index = i + 1;
		cmd->scmd = NULL;
		cmd->sync_cmd_idx = (u32)ULONG_MAX; /* Set to Invalid */
		cmd->instance = instance;
		cmd->io_request =
			(struct MPI2_RAID_SCSI_IO_REQUEST *)
		  (io_req_base + offset);
		memset(cmd->io_request, 0,
		       sizeof(struct MPI2_RAID_SCSI_IO_REQUEST));
		cmd->io_request_phys_addr = io_req_base_phys + offset;

		list_add_tail(&cmd->list, &fusion->cmd_pool);
	}

	/*
	 * Create a frame pool and assign one frame to each cmd
	 */
	if (megasas_create_frame_pool_fusion(instance)) {
		printk(KERN_DEBUG "megasas: Error creating frame DMA pool\n");
		megasas_free_cmds_fusion(instance);
		goto fail_req_desc;
	}

	return 0;

fail_cmd_list:
	pci_pool_free(fusion->io_request_frames_pool, fusion->io_request_frames,
		      fusion->io_request_frames_phys);
	pci_pool_destroy(fusion->io_request_frames_pool);
fail_io_frames:
	dma_free_coherent(&instance->pdev->dev, fusion->request_alloc_sz,
			  fusion->reply_frames_desc,
			  fusion->reply_frames_desc_phys);
	pci_pool_free(fusion->reply_frames_desc_pool,
		      fusion->reply_frames_desc,
		      fusion->reply_frames_desc_phys);
	pci_pool_destroy(fusion->reply_frames_desc_pool);

fail_reply_desc:
	dma_free_coherent(&instance->pdev->dev, fusion->request_alloc_sz,
			  fusion->req_frames_desc,
			  fusion->req_frames_desc_phys);
fail_req_desc:
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
wait_and_poll(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	int i;
	struct megasas_header *frame_hdr = &cmd->frame->hdr;

	u32 msecs = MFI_POLL_TIMEOUT_SECS * 1000;

	/*
	 * Wait for cmd_status to change
	 */
	for (i = 0; (i < msecs) && (frame_hdr->cmd_status == 0xff); i += 20) {
		rmb();
		msleep(20);
	}

	if (frame_hdr->cmd_status == 0xff)
		return -ETIME;

	return 0;
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
	struct MPI2_IOC_INIT_REQUEST *IOCInitMessage;
	dma_addr_t	ioc_init_handle;
	struct megasas_cmd *cmd;
	u8 ret;
	struct fusion_context *fusion;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	int i;
	struct megasas_header *frame_hdr;

	fusion = instance->ctrl_context;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		printk(KERN_ERR "Could not allocate cmd for INIT Frame\n");
		ret = 1;
		goto fail_get_cmd;
	}

	IOCInitMessage =
	  dma_alloc_coherent(&instance->pdev->dev,
			     sizeof(struct MPI2_IOC_INIT_REQUEST),
			     &ioc_init_handle, GFP_KERNEL);

	if (!IOCInitMessage) {
		printk(KERN_ERR "Could not allocate memory for "
		       "IOCInitMessage\n");
		ret = 1;
		goto fail_fw_init;
	}

	memset(IOCInitMessage, 0, sizeof(struct MPI2_IOC_INIT_REQUEST));

	IOCInitMessage->Function = MPI2_FUNCTION_IOC_INIT;
	IOCInitMessage->WhoInit	= MPI2_WHOINIT_HOST_DRIVER;
	IOCInitMessage->MsgVersion = MPI2_VERSION;
	IOCInitMessage->HeaderVersion = MPI2_HEADER_VERSION;
	IOCInitMessage->SystemRequestFrameSize =
		MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE / 4;

	IOCInitMessage->ReplyDescriptorPostQueueDepth = fusion->reply_q_depth;
	IOCInitMessage->ReplyDescriptorPostQueueAddress	=
		fusion->reply_frames_desc_phys;
	IOCInitMessage->SystemRequestFrameBaseAddress =
		fusion->io_request_frames_phys;
	IOCInitMessage->HostMSIxVectors = instance->msix_vectors;
	init_frame = (struct megasas_init_frame *)cmd->frame;
	memset(init_frame, 0, MEGAMFI_FRAME_SIZE);

	frame_hdr = &cmd->frame->hdr;
	frame_hdr->cmd_status = 0xFF;
	frame_hdr->flags |= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	init_frame->cmd	= MFI_CMD_INIT;
	init_frame->cmd_status = 0xFF;

	init_frame->queue_info_new_phys_addr_lo = ioc_init_handle;
	init_frame->data_xfer_len = sizeof(struct MPI2_IOC_INIT_REQUEST);

	req_desc =
	  (union MEGASAS_REQUEST_DESCRIPTOR_UNION *)fusion->req_frames_desc;

	req_desc->Words = cmd->frame_phys_addr;
	req_desc->MFAIo.RequestFlags =
		(MEGASAS_REQ_DESCRIPT_FLAGS_MFA <<
		 MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	/*
	 * disable the intr before firing the init frame
	 */
	instance->instancet->disable_intr(instance->reg_set);

	for (i = 0; i < (10 * 1000); i += 20) {
		if (readl(&instance->reg_set->doorbell) & 1)
			msleep(20);
		else
			break;
	}

	instance->instancet->fire_cmd(instance, req_desc->u.low,
				      req_desc->u.high, instance->reg_set);

	wait_and_poll(instance, cmd);

	frame_hdr = &cmd->frame->hdr;
	if (frame_hdr->cmd_status != 0) {
		ret = 1;
		goto fail_fw_init;
	}
	printk(KERN_ERR "megasas:IOC Init cmd success\n");

	ret = 0;

fail_fw_init:
	megasas_return_cmd(instance, cmd);
	if (IOCInitMessage)
		dma_free_coherent(&instance->pdev->dev,
				  sizeof(struct MPI2_IOC_INIT_REQUEST),
				  IOCInitMessage, ioc_init_handle);
fail_get_cmd:
	return ret;
}

/*
 * megasas_get_ld_map_info -	Returns FW's ld_map structure
 * @instance:				Adapter soft state
 * @pend:				Pend the command or not
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
static int
megasas_get_ld_map_info(struct megasas_instance *instance)
{
	int ret = 0;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct MR_FW_RAID_MAP_ALL *ci;
	dma_addr_t ci_h = 0;
	u32 size_map_info;
	struct fusion_context *fusion;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		printk(KERN_DEBUG "megasas: Failed to get cmd for map info.\n");
		return -ENOMEM;
	}

	fusion = instance->ctrl_context;

	if (!fusion) {
		megasas_return_cmd(instance, cmd);
		return 1;
	}

	dcmd = &cmd->frame->dcmd;

	size_map_info = sizeof(struct MR_FW_RAID_MAP) +
		(sizeof(struct MR_LD_SPAN_MAP) *(MAX_LOGICAL_DRIVES - 1));

	ci = fusion->ld_map[(instance->map_id & 1)];
	ci_h = fusion->ld_map_phys[(instance->map_id & 1)];

	if (!ci) {
		printk(KERN_DEBUG "Failed to alloc mem for ld_map_info\n");
		megasas_return_cmd(instance, cmd);
		return -ENOMEM;
	}

	memset(ci, 0, sizeof(*ci));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = size_map_info;
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = ci_h;
	dcmd->sgl.sge32[0].length = size_map_info;

	if (!megasas_issue_polled(instance, cmd))
		ret = 0;
	else {
		printk(KERN_ERR "megasas: Get LD Map Info Failed\n");
		ret = -1;
	}

	megasas_return_cmd(instance, cmd);

	return ret;
}

u8
megasas_get_map_info(struct megasas_instance *instance)
{
	struct fusion_context *fusion = instance->ctrl_context;

	fusion->fast_path_io = 0;
	if (!megasas_get_ld_map_info(instance)) {
		if (MR_ValidateMapInfo(fusion->ld_map[(instance->map_id & 1)],
				       fusion->load_balance_info)) {
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
	int ret = 0, i;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	u32 size_sync_info, num_lds;
	struct fusion_context *fusion;
	struct MR_LD_TARGET_SYNC *ci = NULL;
	struct MR_FW_RAID_MAP_ALL *map;
	struct MR_LD_RAID  *raid;
	struct MR_LD_TARGET_SYNC *ld_sync;
	dma_addr_t ci_h = 0;
	u32 size_map_info;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		printk(KERN_DEBUG "megasas: Failed to get cmd for sync"
		       "info.\n");
		return -ENOMEM;
	}

	fusion = instance->ctrl_context;

	if (!fusion) {
		megasas_return_cmd(instance, cmd);
		return 1;
	}

	map = fusion->ld_map[instance->map_id & 1];

	num_lds = map->raidMap.ldCount;

	dcmd = &cmd->frame->dcmd;

	size_sync_info = sizeof(struct MR_LD_TARGET_SYNC) *num_lds;

	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	ci = (struct MR_LD_TARGET_SYNC *)
	  fusion->ld_map[(instance->map_id - 1) & 1];
	memset(ci, 0, sizeof(struct MR_FW_RAID_MAP_ALL));

	ci_h = fusion->ld_map_phys[(instance->map_id - 1) & 1];

	ld_sync = (struct MR_LD_TARGET_SYNC *)ci;

	for (i = 0; i < num_lds; i++, ld_sync++) {
		raid = MR_LdRaidGet(i, map);
		ld_sync->targetId = MR_GetLDTgtId(i, map);
		ld_sync->seqNum = raid->seqNum;
	}

	size_map_info = sizeof(struct MR_FW_RAID_MAP) +
		(sizeof(struct MR_LD_SPAN_MAP) *(MAX_LOGICAL_DRIVES - 1));

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_WRITE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = size_map_info;
	dcmd->mbox.b[0] = num_lds;
	dcmd->mbox.b[1] = MEGASAS_DCMD_MBOX_PEND_FLAG;
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = ci_h;
	dcmd->sgl.sge32[0].length = size_map_info;

	instance->map_update_cmd = cmd;

	instance->instancet->issue_dcmd(instance, cmd);

	return ret;
}

/**
 * megasas_init_adapter_fusion -	Initializes the FW
 * @instance:		Adapter soft state
 *
 * This is the main function for initializing firmware.
 */
u32
megasas_init_adapter_fusion(struct megasas_instance *instance)
{
	struct megasas_register_set __iomem *reg_set;
	struct fusion_context *fusion;
	u32 max_cmd;
	int i = 0, count;

	fusion = instance->ctrl_context;

	reg_set = instance->reg_set;

	/*
	 * Get various operational parameters from status register
	 */
	instance->max_fw_cmds =
		instance->instancet->read_fw_status_reg(reg_set) & 0x00FFFF;
	instance->max_fw_cmds = min(instance->max_fw_cmds, (u16)1008);

	/*
	 * Reduce the max supported cmds by 1. This is to ensure that the
	 * reply_q_sz (1 more than the max cmd that driver may send)
	 * does not exceed max cmds that the FW can support
	 */
	instance->max_fw_cmds = instance->max_fw_cmds-1;
	/* Only internal cmds (DCMD) need to have MFI frames */
	instance->max_mfi_cmds = MEGASAS_INT_CMDS;

	max_cmd = instance->max_fw_cmds;

	fusion->reply_q_depth = ((max_cmd + 1 + 15)/16)*16;

	fusion->request_alloc_sz =
		sizeof(union MEGASAS_REQUEST_DESCRIPTOR_UNION) *max_cmd;
	fusion->reply_alloc_sz = sizeof(union MPI2_REPLY_DESCRIPTORS_UNION)
		*(fusion->reply_q_depth);
	fusion->io_frames_alloc_sz = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE +
		(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE *
		 (max_cmd + 1)); /* Extra 1 for SMID 0 */

	fusion->max_sge_in_main_msg =
	  (MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE -
	   offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL))/16;

	fusion->max_sge_in_chain =
		MEGASAS_MAX_SZ_CHAIN_FRAME / sizeof(union MPI2_SGE_IO_UNION);

	instance->max_num_sge = fusion->max_sge_in_main_msg +
		fusion->max_sge_in_chain - 2;

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
	 * Allocate memory for descriptors
	 * Create a pool of commands
	 */
	if (megasas_alloc_cmds(instance))
		goto fail_alloc_mfi_cmds;
	if (megasas_alloc_cmds_fusion(instance))
		goto fail_alloc_cmds;

	if (megasas_ioc_init_fusion(instance))
		goto fail_ioc_init;

	instance->flag_ieee = 1;

	fusion->map_sz =  sizeof(struct MR_FW_RAID_MAP) +
	  (sizeof(struct MR_LD_SPAN_MAP) *(MAX_LOGICAL_DRIVES - 1));

	fusion->fast_path_io = 0;

	for (i = 0; i < 2; i++) {
		fusion->ld_map[i] = dma_alloc_coherent(&instance->pdev->dev,
						       fusion->map_sz,
						       &fusion->ld_map_phys[i],
						       GFP_KERNEL);
		if (!fusion->ld_map[i]) {
			printk(KERN_ERR "megasas: Could not allocate memory "
			       "for map info\n");
			goto fail_map_info;
		}
	}

	if (!megasas_get_map_info(instance))
		megasas_sync_map_info(instance);

	return 0;

fail_map_info:
	if (i == 1)
		dma_free_coherent(&instance->pdev->dev, fusion->map_sz,
				  fusion->ld_map[0], fusion->ld_map_phys[0]);
fail_ioc_init:
	megasas_free_cmds_fusion(instance);
fail_alloc_cmds:
	megasas_free_cmds(instance);
fail_alloc_mfi_cmds:
	return 1;
}

/**
 * megasas_fire_cmd_fusion -	Sends command to the FW
 * @frame_phys_addr :		Physical address of cmd
 * @frame_count :		Number of frames for the command
 * @regs :			MFI register set
 */
void
megasas_fire_cmd_fusion(struct megasas_instance *instance,
			dma_addr_t req_desc_lo,
			u32 req_desc_hi,
			struct megasas_register_set __iomem *regs)
{
	unsigned long flags;

	spin_lock_irqsave(&instance->hba_lock, flags);

	writel(req_desc_lo,
	       &(regs)->inbound_low_queue_port);
	writel(req_desc_hi, &(regs)->inbound_high_queue_port);
	spin_unlock_irqrestore(&instance->hba_lock, flags);
}

/**
 * map_cmd_status -	Maps FW cmd status to OS cmd status
 * @cmd :		Pointer to cmd
 * @status :		status of cmd returned by FW
 * @ext_status :	ext status of cmd returned by FW
 */

void
map_cmd_status(struct megasas_cmd_fusion *cmd, u8 status, u8 ext_status)
{

	switch (status) {

	case MFI_STAT_OK:
		cmd->scmd->result = DID_OK << 16;
		break;

	case MFI_STAT_SCSI_IO_FAILED:
	case MFI_STAT_LD_INIT_IN_PROGRESS:
		cmd->scmd->result = (DID_ERROR << 16) | ext_status;
		break;

	case MFI_STAT_SCSI_DONE_WITH_ERROR:

		cmd->scmd->result = (DID_OK << 16) | ext_status;
		if (ext_status == SAM_STAT_CHECK_CONDITION) {
			memset(cmd->scmd->sense_buffer, 0,
			       SCSI_SENSE_BUFFERSIZE);
			memcpy(cmd->scmd->sense_buffer, cmd->sense,
			       SCSI_SENSE_BUFFERSIZE);
			cmd->scmd->result |= DRIVER_SENSE << 24;
		}
		break;

	case MFI_STAT_LD_OFFLINE:
	case MFI_STAT_DEVICE_NOT_FOUND:
		cmd->scmd->result = DID_BAD_TARGET << 16;
		break;
	case MFI_STAT_CONFIG_SEQ_MISMATCH:
		cmd->scmd->result = DID_IMM_RETRY << 16;
		break;
	default:
		printk(KERN_DEBUG "megasas: FW status %#x\n", status);
		cmd->scmd->result = DID_ERROR << 16;
		break;
	}
}

/**
 * megasas_make_sgl_fusion -	Prepares 32-bit SGL
 * @instance:		Adapter soft state
 * @scp:		SCSI command from the mid-layer
 * @sgl_ptr:		SGL to be filled in
 * @cmd:		cmd we are working on
 *
 * If successful, this function returns the number of SG elements.
 */
static int
megasas_make_sgl_fusion(struct megasas_instance *instance,
			struct scsi_cmnd *scp,
			struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr,
			struct megasas_cmd_fusion *cmd)
{
	int i, sg_processed, sge_count;
	struct scatterlist *os_sgl;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) {
		struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr_end = sgl_ptr;
		sgl_ptr_end += fusion->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}

	sge_count = scsi_dma_map(scp);

	BUG_ON(sge_count < 0);

	if (sge_count > instance->max_num_sge || !sge_count)
		return sge_count;

	scsi_for_each_sg(scp, os_sgl, sge_count, i) {
		sgl_ptr->Length = sg_dma_len(os_sgl);
		sgl_ptr->Address = sg_dma_address(os_sgl);
		sgl_ptr->Flags = 0;
		if (instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) {
			if (i == sge_count - 1)
				sgl_ptr->Flags = IEEE_SGE_FLAGS_END_OF_LIST;
		}
		sgl_ptr++;

		sg_processed = i + 1;

		if ((sg_processed ==  (fusion->max_sge_in_main_msg - 1)) &&
		    (sge_count > fusion->max_sge_in_main_msg)) {

			struct MPI25_IEEE_SGE_CHAIN64 *sg_chain;
			if (instance->pdev->device ==
			    PCI_DEVICE_ID_LSI_INVADER) {
				if ((cmd->io_request->IoFlags &
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
			if (instance->pdev->device ==
			    PCI_DEVICE_ID_LSI_INVADER)
				sg_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT;
			else
				sg_chain->Flags =
					(IEEE_SGE_FLAGS_CHAIN_ELEMENT |
					 MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR);
			sg_chain->Length =  (sizeof(union MPI2_SGE_IO_UNION)
					     *(sge_count - sg_processed));
			sg_chain->Address = cmd->sg_frame_phys_addr;

			sgl_ptr =
			  (struct MPI25_IEEE_SGE_CHAIN64 *)cmd->sg_frame;
		}
	}

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
void
megasas_set_pd_lba(struct MPI2_RAID_SCSI_IO_REQUEST *io_request, u8 cdb_len,
		   struct IO_REQUEST_INFO *io_info, struct scsi_cmnd *scp,
		   struct MR_FW_RAID_MAP_ALL *local_map_ptr, u32 ref_tag)
{
	struct MR_LD_RAID *raid;
	u32 ld;
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

		if (scp->sc_data_direction == PCI_DMA_FROMDEVICE)
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
		io_request->CDB.EEDP32.PrimaryApplicationTagMask = 0xffff;

		io_request->DataLength = num_blocks * 512;
		io_request->IoFlags = 32; /* Specify 32-byte cdb */

		/* Transfer length */
		cdb[28] = (u8)((num_blocks >> 24) & 0xff);
		cdb[29] = (u8)((num_blocks >> 16) & 0xff);
		cdb[30] = (u8)((num_blocks >> 8) & 0xff);
		cdb[31] = (u8)(num_blocks & 0xff);

		/* set SCSI IO EEDPFlags */
		if (scp->sc_data_direction == PCI_DMA_FROMDEVICE) {
			io_request->EEDPFlags =
				MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG  |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		} else {
			io_request->EEDPFlags =
				MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
				MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
		}
		io_request->Control |= (0x4 << 26);
		io_request->EEDPBlockSize = MEGASAS_EEDPBLOCKSIZE;
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

			io_request->IoFlags = 10; /* Specify 10-byte cdb */
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

			io_request->IoFlags = 16; /* Specify 16-byte cdb */
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
 * megasas_build_ldio_fusion -	Prepares IOs to devices
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Prepares the io_request and chain elements (sg_frame) for IO
 * The IO can be for PD (Fast Path) or LD
 */
void
megasas_build_ldio_fusion(struct megasas_instance *instance,
			  struct scsi_cmnd *scp,
			  struct megasas_cmd_fusion *cmd)
{
	u8 fp_possible;
	u32 start_lba_lo, start_lba_hi, device_id;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	struct IO_REQUEST_INFO io_info;
	struct fusion_context *fusion;
	struct MR_FW_RAID_MAP_ALL *local_map_ptr;

	device_id = MEGASAS_DEV_INDEX(instance, scp);

	fusion = instance->ctrl_context;

	io_request = cmd->io_request;
	io_request->RaidContext.VirtualDiskTgtId = device_id;
	io_request->RaidContext.status = 0;
	io_request->RaidContext.exStatus = 0;

	req_desc = (union MEGASAS_REQUEST_DESCRIPTOR_UNION *)cmd->request_desc;

	start_lba_lo = 0;
	start_lba_hi = 0;
	fp_possible = 0;

	/*
	 * 6-byte READ(0x08) or WRITE(0x0A) cdb
	 */
	if (scp->cmd_len == 6) {
		io_request->DataLength = (u32) scp->cmnd[4];
		start_lba_lo = ((u32) scp->cmnd[1] << 16) |
			((u32) scp->cmnd[2] << 8) | (u32) scp->cmnd[3];

		start_lba_lo &= 0x1FFFFF;
	}

	/*
	 * 10-byte READ(0x28) or WRITE(0x2A) cdb
	 */
	else if (scp->cmd_len == 10) {
		io_request->DataLength = (u32) scp->cmnd[8] |
			((u32) scp->cmnd[7] << 8);
		start_lba_lo = ((u32) scp->cmnd[2] << 24) |
			((u32) scp->cmnd[3] << 16) |
			((u32) scp->cmnd[4] << 8) | (u32) scp->cmnd[5];
	}

	/*
	 * 12-byte READ(0xA8) or WRITE(0xAA) cdb
	 */
	else if (scp->cmd_len == 12) {
		io_request->DataLength = ((u32) scp->cmnd[6] << 24) |
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
		io_request->DataLength = ((u32) scp->cmnd[10] << 24) |
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
	io_info.numBlocks = io_request->DataLength;
	io_info.ldTgtId = device_id;

	if (scp->sc_data_direction == PCI_DMA_FROMDEVICE)
		io_info.isRead = 1;

	local_map_ptr = fusion->ld_map[(instance->map_id & 1)];

	if ((MR_TargetIdToLdGet(device_id, local_map_ptr) >=
	     MAX_LOGICAL_DRIVES) || (!fusion->fast_path_io)) {
		io_request->RaidContext.regLockFlags  = 0;
		fp_possible = 0;
	} else {
		if (MR_BuildRaidContext(instance, &io_info,
					&io_request->RaidContext,
					local_map_ptr))
			fp_possible = io_info.fpOkForIo;
	}

	/* Use smp_processor_id() for now until cmd->request->cpu is CPU
	   id by default, not CPU group id, otherwise all MSI-X queues won't
	   be utilized */
	cmd->request_desc->SCSIIO.MSIxIndex = instance->msix_vectors ?
		smp_processor_id() % instance->msix_vectors : 0;

	if (fp_possible) {
		megasas_set_pd_lba(io_request, scp->cmd_len, &io_info, scp,
				   local_map_ptr, start_lba_lo);
		io_request->DataLength = scsi_bufflen(scp);
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) {
			if (io_request->RaidContext.regLockFlags ==
			    REGION_TYPE_UNUSED)
				cmd->request_desc->SCSIIO.RequestFlags =
					(MEGASAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
					MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			io_request->RaidContext.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.nseg = 0x1;
			io_request->IoFlags |=
			  MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH;
			io_request->RaidContext.regLockFlags |=
			  (MR_RL_FLAGS_GRANT_DESTINATION_CUDA |
			   MR_RL_FLAGS_SEQ_NUM_ENABLE);
		}
		if ((fusion->load_balance_info[device_id].loadBalanceFlag) &&
		    (io_info.isRead)) {
			io_info.devHandle =
				get_updated_dev_handle(
					&fusion->load_balance_info[device_id],
					&io_info);
			scp->SCp.Status |= MEGASAS_LOAD_BALANCE_FLAG;
		} else
			scp->SCp.Status &= ~MEGASAS_LOAD_BALANCE_FLAG;
		cmd->request_desc->SCSIIO.DevHandle = io_info.devHandle;
		io_request->DevHandle = io_info.devHandle;
	} else {
		io_request->RaidContext.timeoutValue =
			local_map_ptr->raidMap.fpPdIoTimeoutSec;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MEGASAS_REQ_DESCRIPT_FLAGS_LD_IO
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) {
			if (io_request->RaidContext.regLockFlags ==
			    REGION_TYPE_UNUSED)
				cmd->request_desc->SCSIIO.RequestFlags =
					(MEGASAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
					MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			io_request->RaidContext.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.regLockFlags |=
				(MR_RL_FLAGS_GRANT_DESTINATION_CPU0 |
				 MR_RL_FLAGS_SEQ_NUM_ENABLE);
			io_request->RaidContext.nseg = 0x1;
		}
		io_request->Function = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = device_id;
	} /* Not FP */
}

/**
 * megasas_build_dcdb_fusion -	Prepares IOs to devices
 * @instance:		Adapter soft state
 * @scp:		SCSI command
 * @cmd:		Command to be prepared
 *
 * Prepares the io_request frame for non-io cmds
 */
static void
megasas_build_dcdb_fusion(struct megasas_instance *instance,
			  struct scsi_cmnd *scmd,
			  struct megasas_cmd_fusion *cmd)
{
	u32 device_id;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	u16 pd_index = 0;
	struct MR_FW_RAID_MAP_ALL *local_map_ptr;
	struct fusion_context *fusion = instance->ctrl_context;

	io_request = cmd->io_request;
	device_id = MEGASAS_DEV_INDEX(instance, scmd);
	pd_index = (scmd->device->channel * MEGASAS_MAX_DEV_PER_CHANNEL)
		+scmd->device->id;
	local_map_ptr = fusion->ld_map[(instance->map_id & 1)];

	/* Check if this is a system PD I/O */
	if (instance->pd_list[pd_index].driveState == MR_PD_STATE_SYSTEM) {
		io_request->Function = 0;
		io_request->DevHandle =
			local_map_ptr->raidMap.devHndlInfo[device_id].curDevHdl;
		io_request->RaidContext.timeoutValue =
			local_map_ptr->raidMap.fpPdIoTimeoutSec;
		io_request->RaidContext.regLockFlags = 0;
		io_request->RaidContext.regLockRowLBA = 0;
		io_request->RaidContext.regLockLength = 0;
		io_request->RaidContext.RAIDFlags =
			MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD <<
			MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY <<
			 MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	} else {
		io_request->Function  = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = device_id;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
			 MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	}
	io_request->RaidContext.VirtualDiskTgtId = device_id;
	io_request->LUN[1] = scmd->device->lun;
	io_request->DataLength = scsi_bufflen(scmd);
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
int
megasas_build_io_fusion(struct megasas_instance *instance,
			struct scsi_cmnd *scp,
			struct megasas_cmd_fusion *cmd)
{
	u32 device_id, sge_count;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request = cmd->io_request;

	device_id = MEGASAS_DEV_INDEX(instance, scp);

	/* Zero out some fields so they don't get reused */
	io_request->LUN[1] = 0;
	io_request->CDB.EEDP32.PrimaryReferenceTag = 0;
	io_request->CDB.EEDP32.PrimaryApplicationTagMask = 0;
	io_request->EEDPFlags = 0;
	io_request->Control = 0;
	io_request->EEDPBlockSize = 0;
	io_request->ChainOffset = 0;
	io_request->RaidContext.RAIDFlags = 0;
	io_request->RaidContext.Type = 0;
	io_request->RaidContext.nseg = 0;

	memcpy(io_request->CDB.CDB32, scp->cmnd, scp->cmd_len);
	/*
	 * Just the CDB length,rest of the Flags are zero
	 * This will be modified for FP in build_ldio_fusion
	 */
	io_request->IoFlags = scp->cmd_len;

	if (megasas_is_ldio(scp))
		megasas_build_ldio_fusion(instance, scp, cmd);
	else
		megasas_build_dcdb_fusion(instance, scp, cmd);

	/*
	 * Construct SGL
	 */

	sge_count =
		megasas_make_sgl_fusion(instance, scp,
					(struct MPI25_IEEE_SGE_CHAIN64 *)
					&io_request->SGL, cmd);

	if (sge_count > instance->max_num_sge) {
		printk(KERN_ERR "megasas: Error. sge_count (0x%x) exceeds "
		       "max (0x%x) allowed\n", sge_count,
		       instance->max_num_sge);
		return 1;
	}

	io_request->RaidContext.numSGE = sge_count;

	io_request->SGLFlags = MPI2_SGE_FLAGS_64_BIT_ADDRESSING;

	if (scp->sc_data_direction == PCI_DMA_TODEVICE)
		io_request->Control |= MPI2_SCSIIO_CONTROL_WRITE;
	else if (scp->sc_data_direction == PCI_DMA_FROMDEVICE)
		io_request->Control |= MPI2_SCSIIO_CONTROL_READ;

	io_request->SGLOffset0 =
		offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4;

	io_request->SenseBufferLowAddress = cmd->sense_phys_addr;
	io_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;

	cmd->scmd = scp;
	scp->SCp.ptr = (char *)cmd;

	return 0;
}

union MEGASAS_REQUEST_DESCRIPTOR_UNION *
megasas_get_request_descriptor(struct megasas_instance *instance, u16 index)
{
	u8 *p;
	struct fusion_context *fusion;

	if (index >= instance->max_fw_cmds) {
		printk(KERN_ERR "megasas: Invalid SMID (0x%x)request for "
		       "descriptor\n", index);
		return NULL;
	}
	fusion = instance->ctrl_context;
	p = fusion->req_frames_desc
		+sizeof(union MEGASAS_REQUEST_DESCRIPTOR_UNION) *index;

	return (union MEGASAS_REQUEST_DESCRIPTOR_UNION *)p;
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
	struct megasas_cmd_fusion *cmd;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u32 index;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	cmd = megasas_get_cmd_fusion(instance);
	if (!cmd)
		return SCSI_MLQUEUE_HOST_BUSY;

	index = cmd->index;

	req_desc = megasas_get_request_descriptor(instance, index-1);
	if (!req_desc)
		return 1;

	req_desc->Words = 0;
	cmd->request_desc = req_desc;

	if (megasas_build_io_fusion(instance, scmd, cmd)) {
		megasas_return_cmd_fusion(instance, cmd);
		printk(KERN_ERR "megasas: Error building command.\n");
		cmd->request_desc = NULL;
		return 1;
	}

	req_desc = cmd->request_desc;
	req_desc->SCSIIO.SMID = index;

	if (cmd->io_request->ChainOffset != 0 &&
	    cmd->io_request->ChainOffset != 0xF)
		printk(KERN_ERR "megasas: The chain offset value is not "
		       "correct : %x\n", cmd->io_request->ChainOffset);

	/*
	 * Issue the command to the FW
	 */
	atomic_inc(&instance->fw_outstanding);

	instance->instancet->fire_cmd(instance,
				      req_desc->u.low, req_desc->u.high,
				      instance->reg_set);

	return 0;
}

/**
 * complete_cmd_fusion -	Completes command
 * @instance:			Adapter soft state
 * Completes all commands that is in reply descriptor queue
 */
int
complete_cmd_fusion(struct megasas_instance *instance, u32 MSIxIndex)
{
	union MPI2_REPLY_DESCRIPTORS_UNION *desc;
	struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *reply_desc;
	struct MPI2_RAID_SCSI_IO_REQUEST *scsi_io_req;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;
	struct megasas_cmd_fusion *cmd_fusion;
	u16 smid, num_completed;
	u8 reply_descript_type, arm;
	u32 status, extStatus, device_id;
	union desc_value d_val;
	struct LD_LOAD_BALANCE_INFO *lbinfo;

	fusion = instance->ctrl_context;

	if (instance->adprecovery == MEGASAS_HW_CRITICAL_ERROR)
		return IRQ_HANDLED;

	desc = fusion->reply_frames_desc;
	desc += ((MSIxIndex * fusion->reply_alloc_sz)/
		 sizeof(union MPI2_REPLY_DESCRIPTORS_UNION)) +
		fusion->last_reply_idx[MSIxIndex];

	reply_desc = (struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;

	d_val.word = desc->Words;

	reply_descript_type = reply_desc->ReplyFlags &
		MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

	if (reply_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
		return IRQ_NONE;

	d_val.word = desc->Words;

	num_completed = 0;

	while ((d_val.u.low != UINT_MAX) && (d_val.u.high != UINT_MAX)) {
		smid = reply_desc->SMID;

		cmd_fusion = fusion->cmd_list[smid - 1];

		scsi_io_req =
			(struct MPI2_RAID_SCSI_IO_REQUEST *)
		  cmd_fusion->io_request;

		if (cmd_fusion->scmd)
			cmd_fusion->scmd->SCp.ptr = NULL;

		status = scsi_io_req->RaidContext.status;
		extStatus = scsi_io_req->RaidContext.exStatus;

		switch (scsi_io_req->Function) {
		case MPI2_FUNCTION_SCSI_IO_REQUEST:  /*Fast Path IO.*/
			/* Update load balancing info */
			device_id = MEGASAS_DEV_INDEX(instance,
						      cmd_fusion->scmd);
			lbinfo = &fusion->load_balance_info[device_id];
			if (cmd_fusion->scmd->SCp.Status &
			    MEGASAS_LOAD_BALANCE_FLAG) {
				arm = lbinfo->raid1DevHandle[0] ==
					cmd_fusion->io_request->DevHandle ? 0 :
					1;
				atomic_dec(&lbinfo->scsi_pending_cmds[arm]);
				cmd_fusion->scmd->SCp.Status &=
					~MEGASAS_LOAD_BALANCE_FLAG;
			}
			if (reply_descript_type ==
			    MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS) {
				if (megasas_dbg_lvl == 5)
					printk(KERN_ERR "\nmegasas: FAST Path "
					       "IO Success\n");
			}
			/* Fall thru and complete IO */
		case MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST: /* LD-IO Path */
			/* Map the FW Cmd Status */
			map_cmd_status(cmd_fusion, status, extStatus);
			scsi_dma_unmap(cmd_fusion->scmd);
			cmd_fusion->scmd->scsi_done(cmd_fusion->scmd);
			scsi_io_req->RaidContext.status = 0;
			scsi_io_req->RaidContext.exStatus = 0;
			megasas_return_cmd_fusion(instance, cmd_fusion);
			atomic_dec(&instance->fw_outstanding);

			break;
		case MEGASAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST: /*MFI command */
			cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];
			megasas_complete_cmd(instance, cmd_mfi, DID_OK);
			cmd_fusion->flags = 0;
			megasas_return_cmd_fusion(instance, cmd_fusion);

			break;
		}

		fusion->last_reply_idx[MSIxIndex]++;
		if (fusion->last_reply_idx[MSIxIndex] >=
		    fusion->reply_q_depth)
			fusion->last_reply_idx[MSIxIndex] = 0;

		desc->Words = ULLONG_MAX;
		num_completed++;

		/* Get the next reply descriptor */
		if (!fusion->last_reply_idx[MSIxIndex])
			desc = fusion->reply_frames_desc +
				((MSIxIndex * fusion->reply_alloc_sz)/
				 sizeof(union MPI2_REPLY_DESCRIPTORS_UNION));
		else
			desc++;

		reply_desc =
		  (struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;

		d_val.word = desc->Words;

		reply_descript_type = reply_desc->ReplyFlags &
			MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

		if (reply_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			break;
	}

	if (!num_completed)
		return IRQ_NONE;

	wmb();
	writel((MSIxIndex << 24) | fusion->last_reply_idx[MSIxIndex],
	       &instance->reg_set->reply_post_host_index);
	megasas_check_and_restore_queue_depth(instance);
	return IRQ_HANDLED;
}

/**
 * megasas_complete_cmd_dpc_fusion -	Completes command
 * @instance:			Adapter soft state
 *
 * Tasklet to complete cmds
 */
void
megasas_complete_cmd_dpc_fusion(unsigned long instance_addr)
{
	struct megasas_instance *instance =
		(struct megasas_instance *)instance_addr;
	unsigned long flags;
	u32 count, MSIxIndex;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;

	/* If we have already declared adapter dead, donot complete cmds */
	spin_lock_irqsave(&instance->hba_lock, flags);
	if (instance->adprecovery == MEGASAS_HW_CRITICAL_ERROR) {
		spin_unlock_irqrestore(&instance->hba_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&instance->hba_lock, flags);

	spin_lock_irqsave(&instance->completion_lock, flags);
	for (MSIxIndex = 0 ; MSIxIndex < count; MSIxIndex++)
		complete_cmd_fusion(instance, MSIxIndex);
	spin_unlock_irqrestore(&instance->completion_lock, flags);
}

/**
 * megasas_isr_fusion - isr entry point
 */
irqreturn_t megasas_isr_fusion(int irq, void *devp)
{
	struct megasas_irq_context *irq_context = devp;
	struct megasas_instance *instance = irq_context->instance;
	u32 mfiStatus, fw_state;

	if (!instance->msix_vectors) {
		mfiStatus = instance->instancet->clear_intr(instance->reg_set);
		if (!mfiStatus)
			return IRQ_NONE;
	}

	/* If we are resetting, bail */
	if (test_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags)) {
		instance->instancet->clear_intr(instance->reg_set);
		return IRQ_HANDLED;
	}

	if (!complete_cmd_fusion(instance, irq_context->MSIxIndex)) {
		instance->instancet->clear_intr(instance->reg_set);
		/* If we didn't complete any commands, check for FW fault */
		fw_state = instance->instancet->read_fw_status_reg(
			instance->reg_set) & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT)
			schedule_work(&instance->work_init);
	}

	return IRQ_HANDLED;
}

/**
 * build_mpt_mfi_pass_thru - builds a cmd fo MFI Pass thru
 * @instance:			Adapter soft state
 * mfi_cmd:			megasas_cmd pointer
 *
 */
u8
build_mpt_mfi_pass_thru(struct megasas_instance *instance,
			struct megasas_cmd *mfi_cmd)
{
	struct MPI25_IEEE_SGE_CHAIN64 *mpi25_ieee_chain;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_req;
	struct megasas_cmd_fusion *cmd;
	struct fusion_context *fusion;
	struct megasas_header *frame_hdr = &mfi_cmd->frame->hdr;

	cmd = megasas_get_cmd_fusion(instance);
	if (!cmd)
		return 1;

	/*  Save the smid. To be used for returning the cmd */
	mfi_cmd->context.smid = cmd->index;

	cmd->sync_cmd_idx = mfi_cmd->index;

	/*
	 * For cmds where the flag is set, store the flag and check
	 * on completion. For cmds with this flag, don't call
	 * megasas_complete_cmd
	 */

	if (frame_hdr->flags & MFI_FRAME_DONT_POST_IN_REPLY_QUEUE)
		cmd->flags = MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	fusion = instance->ctrl_context;
	io_req = cmd->io_request;

	if (instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) {
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

	mpi25_ieee_chain->Address = mfi_cmd->frame_phys_addr;

	mpi25_ieee_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT |
		MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR;

	mpi25_ieee_chain->Length = MEGASAS_MAX_SZ_CHAIN_FRAME;

	return 0;
}

/**
 * build_mpt_cmd - Calls helper function to build a cmd MFI Pass thru cmd
 * @instance:			Adapter soft state
 * @cmd:			mfi cmd to build
 *
 */
union MEGASAS_REQUEST_DESCRIPTOR_UNION *
build_mpt_cmd(struct megasas_instance *instance, struct megasas_cmd *cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u16 index;

	if (build_mpt_mfi_pass_thru(instance, cmd)) {
		printk(KERN_ERR "Couldn't build MFI pass thru cmd\n");
		return NULL;
	}

	index = cmd->context.smid;

	req_desc = megasas_get_request_descriptor(instance, index - 1);

	if (!req_desc)
		return NULL;

	req_desc->Words = 0;
	req_desc->SCSIIO.RequestFlags = (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
					 MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	req_desc->SCSIIO.SMID = index;

	return req_desc;
}

/**
 * megasas_issue_dcmd_fusion - Issues a MFI Pass thru cmd
 * @instance:			Adapter soft state
 * @cmd:			mfi cmd pointer
 *
 */
void
megasas_issue_dcmd_fusion(struct megasas_instance *instance,
			  struct megasas_cmd *cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	req_desc = build_mpt_cmd(instance, cmd);
	if (!req_desc) {
		printk(KERN_ERR "Couldn't issue MFI pass thru cmd\n");
		return;
	}
	instance->instancet->fire_cmd(instance, req_desc->u.low,
				      req_desc->u.high, instance->reg_set);
}

/**
 * megasas_release_fusion -	Reverses the FW initialization
 * @intance:			Adapter soft state
 */
void
megasas_release_fusion(struct megasas_instance *instance)
{
	megasas_free_cmds(instance);
	megasas_free_cmds_fusion(instance);

	iounmap(instance->reg_set);

	pci_release_selected_regions(instance->pdev, instance->bar);
}

/**
 * megasas_read_fw_status_reg_fusion - returns the current FW status value
 * @regs:			MFI register set
 */
static u32
megasas_read_fw_status_reg_fusion(struct megasas_register_set __iomem *regs)
{
	return readl(&(regs)->outbound_scratch_pad);
}

/**
 * megasas_adp_reset_fusion -	For controller reset
 * @regs:				MFI register set
 */
static int
megasas_adp_reset_fusion(struct megasas_instance *instance,
			 struct megasas_register_set __iomem *regs)
{
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

/* This function waits for outstanding commands on fusion to complete */
int megasas_wait_for_outstanding_fusion(struct megasas_instance *instance)
{
	int i, outstanding, retval = 0;
	u32 fw_state, wait_time = MEGASAS_RESET_WAIT_TIME;

	for (i = 0; i < wait_time; i++) {
		/* Check if firmware is in fault state */
		fw_state = instance->instancet->read_fw_status_reg(
			instance->reg_set) & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT) {
			printk(KERN_WARNING "megasas: Found FW in FAULT state,"
			       " will reset adapter.\n");
			retval = 1;
			goto out;
		}

		outstanding = atomic_read(&instance->fw_outstanding);
		if (!outstanding)
			goto out;

		if (!(i % MEGASAS_RESET_NOTICE_INTERVAL)) {
			printk(KERN_NOTICE "megasas: [%2d]waiting for %d "
			       "commands to complete\n", i, outstanding);
			megasas_complete_cmd_dpc_fusion(
				(unsigned long)instance);
		}
		msleep(1000);
	}

	if (atomic_read(&instance->fw_outstanding)) {
		printk("megaraid_sas: pending commands remain after waiting, "
		       "will reset adapter.\n");
		retval = 1;
	}
out:
	return retval;
}

void  megasas_reset_reply_desc(struct megasas_instance *instance)
{
	int i, count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;

	fusion = instance->ctrl_context;
	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	for (i = 0 ; i < count ; i++)
		fusion->last_reply_idx[i] = 0;
	reply_desc = fusion->reply_frames_desc;
	for (i = 0 ; i < fusion->reply_q_depth * count; i++, reply_desc++)
		reply_desc->Words = ULLONG_MAX;
}

/* Core fusion reset function */
int megasas_reset_fusion(struct Scsi_Host *shost)
{
	int retval = SUCCESS, i, j, retry = 0;
	struct megasas_instance *instance;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u32 host_diag, abs_state, status_reg, reset_adapter;

	instance = (struct megasas_instance *)shost->hostdata;
	fusion = instance->ctrl_context;

	if (instance->adprecovery == MEGASAS_HW_CRITICAL_ERROR) {
		printk(KERN_WARNING "megaraid_sas: Hardware critical error, "
		       "returning FAILED.\n");
		return FAILED;
	}

	mutex_lock(&instance->reset_mutex);
	set_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
	instance->adprecovery = MEGASAS_ADPRESET_SM_INFAULT;
	instance->instancet->disable_intr(instance->reg_set);
	msleep(1000);

	/* First try waiting for commands to complete */
	if (megasas_wait_for_outstanding_fusion(instance)) {
		printk(KERN_WARNING "megaraid_sas: resetting fusion "
		       "adapter.\n");
		/* Now return commands back to the OS */
		for (i = 0 ; i < instance->max_fw_cmds; i++) {
			cmd_fusion = fusion->cmd_list[i];
			if (cmd_fusion->scmd) {
				scsi_dma_unmap(cmd_fusion->scmd);
				cmd_fusion->scmd->result = (DID_RESET << 16);
				cmd_fusion->scmd->scsi_done(cmd_fusion->scmd);
				megasas_return_cmd_fusion(instance, cmd_fusion);
				atomic_dec(&instance->fw_outstanding);
			}
		}

		status_reg = instance->instancet->read_fw_status_reg(
			instance->reg_set);
		abs_state = status_reg & MFI_STATE_MASK;
		reset_adapter = status_reg & MFI_RESET_ADAPTER;
		if (instance->disableOnlineCtrlReset ||
		    (abs_state == MFI_STATE_FAULT && !reset_adapter)) {
			/* Reset not supported, kill adapter */
			printk(KERN_WARNING "megaraid_sas: Reset not supported"
			       ", killing adapter.\n");
			megaraid_sas_kill_hba(instance);
			instance->adprecovery = MEGASAS_HW_CRITICAL_ERROR;
			retval = FAILED;
			goto out;
		}

		/* Now try to reset the chip */
		for (i = 0; i < MEGASAS_FUSION_MAX_RESET_TRIES; i++) {
			writel(MPI2_WRSEQ_FLUSH_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_1ST_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_2ND_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_3RD_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_4TH_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_5TH_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);
			writel(MPI2_WRSEQ_6TH_KEY_VALUE,
			       &instance->reg_set->fusion_seq_offset);

			/* Check that the diag write enable (DRWE) bit is on */
			host_diag = readl(&instance->reg_set->fusion_host_diag);
			retry = 0;
			while (!(host_diag & HOST_DIAG_WRITE_ENABLE)) {
				msleep(100);
				host_diag =
				readl(&instance->reg_set->fusion_host_diag);
				if (retry++ == 100) {
					printk(KERN_WARNING "megaraid_sas: "
					       "Host diag unlock failed!\n");
					break;
				}
			}
			if (!(host_diag & HOST_DIAG_WRITE_ENABLE))
				continue;

			/* Send chip reset command */
			writel(host_diag | HOST_DIAG_RESET_ADAPTER,
			       &instance->reg_set->fusion_host_diag);
			msleep(3000);

			/* Make sure reset adapter bit is cleared */
			host_diag = readl(&instance->reg_set->fusion_host_diag);
			retry = 0;
			while (host_diag & HOST_DIAG_RESET_ADAPTER) {
				msleep(100);
				host_diag =
				readl(&instance->reg_set->fusion_host_diag);
				if (retry++ == 1000) {
					printk(KERN_WARNING "megaraid_sas: "
					       "Diag reset adapter never "
					       "cleared!\n");
					break;
				}
			}
			if (host_diag & HOST_DIAG_RESET_ADAPTER)
				continue;

			abs_state =
				instance->instancet->read_fw_status_reg(
					instance->reg_set) & MFI_STATE_MASK;
			retry = 0;

			while ((abs_state <= MFI_STATE_FW_INIT) &&
			       (retry++ < 1000)) {
				msleep(100);
				abs_state =
				instance->instancet->read_fw_status_reg(
					instance->reg_set) & MFI_STATE_MASK;
			}
			if (abs_state <= MFI_STATE_FW_INIT) {
				printk(KERN_WARNING "megaraid_sas: firmware "
				       "state < MFI_STATE_FW_INIT, state = "
				       "0x%x\n", abs_state);
				continue;
			}

			/* Wait for FW to become ready */
			if (megasas_transition_to_ready(instance, 1)) {
				printk(KERN_WARNING "megaraid_sas: Failed to "
				       "transition controller to ready.\n");
				continue;
			}

			megasas_reset_reply_desc(instance);
			if (megasas_ioc_init_fusion(instance)) {
				printk(KERN_WARNING "megaraid_sas: "
				       "megasas_ioc_init_fusion() failed!\n");
				continue;
			}

			clear_bit(MEGASAS_FUSION_IN_RESET,
				  &instance->reset_flags);
			instance->instancet->enable_intr(instance->reg_set);
			instance->adprecovery = MEGASAS_HBA_OPERATIONAL;

			/* Re-fire management commands */
			for (j = 0 ; j < instance->max_fw_cmds; j++) {
				cmd_fusion = fusion->cmd_list[j];
				if (cmd_fusion->sync_cmd_idx !=
				    (u32)ULONG_MAX) {
					cmd_mfi =
					instance->
					cmd_list[cmd_fusion->sync_cmd_idx];
					if (cmd_mfi->frame->dcmd.opcode ==
					    MR_DCMD_LD_MAP_GET_INFO) {
						megasas_return_cmd(instance,
								   cmd_mfi);
						megasas_return_cmd_fusion(
							instance, cmd_fusion);
					} else  {
						req_desc =
						megasas_get_request_descriptor(
							instance,
							cmd_mfi->context.smid
							-1);
						if (!req_desc)
							printk(KERN_WARNING
							       "req_desc NULL"
							       "\n");
						else {
							instance->instancet->
							fire_cmd(instance,
								 req_desc->
								 u.low,
								 req_desc->
								 u.high,
								 instance->
								 reg_set);
						}
					}
				}
			}

			/* Reset load balance info */
			memset(fusion->load_balance_info, 0,
			       sizeof(struct LD_LOAD_BALANCE_INFO)
			       *MAX_LOGICAL_DRIVES);

			if (!megasas_get_map_info(instance))
				megasas_sync_map_info(instance);

			/* Adapter reset completed successfully */
			printk(KERN_WARNING "megaraid_sas: Reset "
			       "successful.\n");
			retval = SUCCESS;
			goto out;
		}
		/* Reset failed, kill the adapter */
		printk(KERN_WARNING "megaraid_sas: Reset failed, killing "
		       "adapter.\n");
		megaraid_sas_kill_hba(instance);
		retval = FAILED;
	} else {
		clear_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
		instance->instancet->enable_intr(instance->reg_set);
		instance->adprecovery = MEGASAS_HBA_OPERATIONAL;
	}
out:
	clear_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
	mutex_unlock(&instance->reset_mutex);
	return retval;
}

/* Fusion OCR work queue */
void megasas_fusion_ocr_wq(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance, work_init);

	megasas_reset_fusion(instance->host);
}

struct megasas_instance_template megasas_instance_template_fusion = {
	.fire_cmd = megasas_fire_cmd_fusion,
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
