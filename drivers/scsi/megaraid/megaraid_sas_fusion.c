/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2013  LSI Corporation
 *  Copyright (c) 2013-2014  Avago Technologies
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  FILE: megaraid_sas_fusion.c
 *
 *  Authors: Avago Technologies
 *           Sumant Patro
 *           Adam Radford
 *           Kashyap Desai <kashyap.desai@avagotech.com>
 *           Sumit Saxena <sumit.saxena@avagotech.com>
 *
 *  Send feedback to: megaraidlinux.pdl@avagotech.com
 *
 *  Mail to: Avago Technologies, 350 West Trimble Road, Building 90,
 *  San Jose, California 95131
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
megasas_clear_intr_fusion(struct megasas_register_set __iomem *regs);
int
megasas_issue_polled(struct megasas_instance *instance,
		     struct megasas_cmd *cmd);
void
megasas_check_and_restore_queue_depth(struct megasas_instance *instance);

int megasas_transition_to_ready(struct megasas_instance *instance, int ocr);
void megaraid_sas_kill_hba(struct megasas_instance *instance);

extern u32 megasas_dbg_lvl;
void megasas_sriov_heartbeat_handler(unsigned long instance_addr);
int megasas_sriov_start_heartbeat(struct megasas_instance *instance,
				  int initial);
void megasas_start_timer(struct megasas_instance *instance,
			struct timer_list *timer,
			 void *fn, unsigned long interval);
extern struct megasas_mgmt_info megasas_mgmt_info;
extern unsigned int resetwaittime;
extern unsigned int dual_qdepth_disable;
static void megasas_free_rdpq_fusion(struct megasas_instance *instance);
static void megasas_free_reply_fusion(struct megasas_instance *instance);



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
	readl(&regs->outbound_intr_mask);
}

/**
 * megasas_disable_intr_fusion - Disables interrupt
 * @regs:			 MFI register set
 */
void
megasas_disable_intr_fusion(struct megasas_instance *instance)
{
	u32 mask = 0xFFFFFFFF;
	u32 status;
	struct megasas_register_set __iomem *regs;
	regs = instance->reg_set;
	instance->mask_interrupts = 1;

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
	memset(cmd->io_request, 0, sizeof(struct MPI2_RAID_SCSI_IO_REQUEST));
}

/**
 * megasas_fire_cmd_fusion -	Sends command to the FW
 */
static void
megasas_fire_cmd_fusion(struct megasas_instance *instance,
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
	mmiowb();
	spin_unlock_irqrestore(&instance->hba_lock, flags);
#endif
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
	struct megasas_register_set __iomem *reg_set;

	reg_set = instance->reg_set;

	cur_max_fw_cmds = readl(&instance->reg_set->outbound_scratch_pad_3) & 0x00FFFF;

	if (dual_qdepth_disable || !cur_max_fw_cmds)
		cur_max_fw_cmds = instance->instancet->read_fw_status_reg(reg_set) & 0x00FFFF;
	else
		ldio_threshold =
			(instance->instancet->read_fw_status_reg(reg_set) & 0x00FFFF) - MEGASAS_FUSION_IOCTL_CMDS;

	dev_info(&instance->pdev->dev,
			"Current firmware maximum commands: %d\t LDIO threshold: %d\n",
			cur_max_fw_cmds, ldio_threshold);

	if (fw_boot_context == OCR_CONTEXT) {
		cur_max_fw_cmds = cur_max_fw_cmds - 1;
		if (cur_max_fw_cmds <= instance->max_fw_cmds) {
			instance->cur_can_queue =
				cur_max_fw_cmds - (MEGASAS_FUSION_INTERNAL_CMDS +
						MEGASAS_FUSION_IOCTL_CMDS);
			instance->host->can_queue = instance->cur_can_queue;
			instance->ldio_threshold = ldio_threshold;
		}
	} else {
		instance->max_fw_cmds = cur_max_fw_cmds;
		instance->ldio_threshold = ldio_threshold;

		if (!instance->is_rdpq)
			instance->max_fw_cmds = min_t(u16, instance->max_fw_cmds, 1024);

		if (reset_devices)
			instance->max_fw_cmds = min(instance->max_fw_cmds,
						(u16)MEGASAS_KDUMP_QUEUE_DEPTH);
		/*
		* Reduce the max supported cmds by 1. This is to ensure that the
		* reply_q_sz (1 more than the max cmd that driver may send)
		* does not exceed max cmds that the FW can support
		*/
		instance->max_fw_cmds = instance->max_fw_cmds-1;

		instance->max_scsi_cmds = instance->max_fw_cmds -
				(MEGASAS_FUSION_INTERNAL_CMDS +
				MEGASAS_FUSION_IOCTL_CMDS);
		instance->cur_can_queue = instance->max_scsi_cmds;
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

	/* SG, Sense */
	for (i = 0; i < instance->max_fw_cmds; i++) {
		cmd = fusion->cmd_list[i];
		if (cmd) {
			if (cmd->sg_frame)
				pci_pool_free(fusion->sg_dma_pool, cmd->sg_frame,
				      cmd->sg_frame_phys_addr);
			if (cmd->sense)
				pci_pool_free(fusion->sense_dma_pool, cmd->sense,
				      cmd->sense_phys_addr);
		}
	}

	if (fusion->sg_dma_pool) {
		pci_pool_destroy(fusion->sg_dma_pool);
		fusion->sg_dma_pool = NULL;
	}
	if (fusion->sense_dma_pool) {
		pci_pool_destroy(fusion->sense_dma_pool);
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
		pci_pool_free(fusion->io_request_frames_pool,
			fusion->io_request_frames,
			fusion->io_request_frames_phys);
	if (fusion->io_request_frames_pool) {
		pci_pool_destroy(fusion->io_request_frames_pool);
		fusion->io_request_frames_pool = NULL;
	}


	/* cmd_list */
	for (i = 0; i < instance->max_fw_cmds; i++)
		kfree(fusion->cmd_list[i]);

	kfree(fusion->cmd_list);
}

/**
 * megasas_create_sg_sense_fusion -	Creates DMA pool for cmd frames
 * @instance:			Adapter soft state
 *
 */
static int megasas_create_sg_sense_fusion(struct megasas_instance *instance)
{
	int i;
	u32 max_cmd;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;

	fusion = instance->ctrl_context;
	max_cmd = instance->max_fw_cmds;


	fusion->sg_dma_pool =
			pci_pool_create("mr_sg", instance->pdev,
				instance->max_chain_frame_sz, 4, 0);
	/* SCSI_SENSE_BUFFERSIZE  = 96 bytes */
	fusion->sense_dma_pool =
			pci_pool_create("mr_sense", instance->pdev,
				SCSI_SENSE_BUFFERSIZE, 64, 0);

	if (!fusion->sense_dma_pool || !fusion->sg_dma_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	/*
	 * Allocate and attach a frame to each of the commands in cmd_list
	 */
	for (i = 0; i < max_cmd; i++) {
		cmd = fusion->cmd_list[i];
		cmd->sg_frame = pci_pool_alloc(fusion->sg_dma_pool,
					GFP_KERNEL, &cmd->sg_frame_phys_addr);

		cmd->sense = pci_pool_alloc(fusion->sense_dma_pool,
					GFP_KERNEL, &cmd->sense_phys_addr);
		if (!cmd->sg_frame || !cmd->sense) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}
	return 0;
}

int
megasas_alloc_cmdlist_fusion(struct megasas_instance *instance)
{
	u32 max_cmd, i;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	max_cmd = instance->max_fw_cmds;

	/*
	 * fusion->cmd_list is an array of struct megasas_cmd_fusion pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */
	fusion->cmd_list = kzalloc(sizeof(struct megasas_cmd_fusion *) * max_cmd,
						GFP_KERNEL);
	if (!fusion->cmd_list) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	for (i = 0; i < max_cmd; i++) {
		fusion->cmd_list[i] = kzalloc(sizeof(struct megasas_cmd_fusion),
					      GFP_KERNEL);
		if (!fusion->cmd_list[i]) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}
	}
	return 0;
}
int
megasas_alloc_request_fusion(struct megasas_instance *instance)
{
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	fusion->req_frames_desc =
		dma_alloc_coherent(&instance->pdev->dev,
			fusion->request_alloc_sz,
			&fusion->req_frames_desc_phys, GFP_KERNEL);
	if (!fusion->req_frames_desc) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->io_request_frames_pool =
			pci_pool_create("mr_ioreq", instance->pdev,
				fusion->io_frames_alloc_sz, 16, 0);

	if (!fusion->io_request_frames_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->io_request_frames =
			pci_pool_alloc(fusion->io_request_frames_pool,
				GFP_KERNEL, &fusion->io_request_frames_phys);
	if (!fusion->io_request_frames) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}
	return 0;
}

int
megasas_alloc_reply_fusion(struct megasas_instance *instance)
{
	int i, count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;
	fusion = instance->ctrl_context;

	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	fusion->reply_frames_desc_pool =
			pci_pool_create("mr_reply", instance->pdev,
				fusion->reply_alloc_sz * count, 16, 0);

	if (!fusion->reply_frames_desc_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	fusion->reply_frames_desc[0] =
		pci_pool_alloc(fusion->reply_frames_desc_pool,
			GFP_KERNEL, &fusion->reply_frames_desc_phys[0]);
	if (!fusion->reply_frames_desc[0]) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
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

int
megasas_alloc_rdpq_fusion(struct megasas_instance *instance)
{
	int i, j, count;
	struct fusion_context *fusion;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;

	fusion = instance->ctrl_context;

	fusion->rdpq_virt = pci_alloc_consistent(instance->pdev,
				sizeof(struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY) * MAX_MSIX_QUEUES_FUSION,
				&fusion->rdpq_phys);
	if (!fusion->rdpq_virt) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	memset(fusion->rdpq_virt, 0,
			sizeof(struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY) * MAX_MSIX_QUEUES_FUSION);
	count = instance->msix_vectors > 0 ? instance->msix_vectors : 1;
	fusion->reply_frames_desc_pool = pci_pool_create("mr_rdpq",
							 instance->pdev, fusion->reply_alloc_sz, 16, 0);

	if (!fusion->reply_frames_desc_pool) {
		dev_err(&instance->pdev->dev,
			"Failed from %s %d\n",  __func__, __LINE__);
		return -ENOMEM;
	}

	for (i = 0; i < count; i++) {
		fusion->reply_frames_desc[i] =
				pci_pool_alloc(fusion->reply_frames_desc_pool,
					GFP_KERNEL, &fusion->reply_frames_desc_phys[i]);
		if (!fusion->reply_frames_desc[i]) {
			dev_err(&instance->pdev->dev,
				"Failed from %s %d\n",  __func__, __LINE__);
			return -ENOMEM;
		}

		fusion->rdpq_virt[i].RDPQBaseAddress =
			fusion->reply_frames_desc_phys[i];

		reply_desc = fusion->reply_frames_desc[i];
		for (j = 0; j < fusion->reply_q_depth; j++, reply_desc++)
			reply_desc->Words = cpu_to_le64(ULLONG_MAX);
	}
	return 0;
}

static void
megasas_free_rdpq_fusion(struct megasas_instance *instance) {

	int i;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	for (i = 0; i < MAX_MSIX_QUEUES_FUSION; i++) {
		if (fusion->reply_frames_desc[i])
			pci_pool_free(fusion->reply_frames_desc_pool,
				fusion->reply_frames_desc[i],
				fusion->reply_frames_desc_phys[i]);
	}

	if (fusion->reply_frames_desc_pool)
		pci_pool_destroy(fusion->reply_frames_desc_pool);

	if (fusion->rdpq_virt)
		pci_free_consistent(instance->pdev,
			sizeof(struct MPI2_IOC_INIT_RDPQ_ARRAY_ENTRY) * MAX_MSIX_QUEUES_FUSION,
			fusion->rdpq_virt, fusion->rdpq_phys);
}

static void
megasas_free_reply_fusion(struct megasas_instance *instance) {

	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (fusion->reply_frames_desc[0])
		pci_pool_free(fusion->reply_frames_desc_pool,
			fusion->reply_frames_desc[0],
			fusion->reply_frames_desc_phys[0]);

	if (fusion->reply_frames_desc_pool)
		pci_pool_destroy(fusion->reply_frames_desc_pool);

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
	int i;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *cmd;
	u32 offset;
	dma_addr_t io_req_base_phys;
	u8 *io_req_base;


	fusion = instance->ctrl_context;

	if (megasas_alloc_cmdlist_fusion(instance))
		goto fail_exit;

	if (megasas_alloc_request_fusion(instance))
		goto fail_exit;

	if (instance->is_rdpq) {
		if (megasas_alloc_rdpq_fusion(instance))
			goto fail_exit;
	} else
		if (megasas_alloc_reply_fusion(instance))
			goto fail_exit;


	/* The first 256 bytes (SMID 0) is not used. Don't add to the cmd list */
	io_req_base = fusion->io_request_frames + MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;
	io_req_base_phys = fusion->io_request_frames_phys + MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;

	/*
	 * Add all the commands to command pool (fusion->cmd_pool)
	 */

	/* SMID 0 is reserved. Set SMID/index from 1 */
	for (i = 0; i < instance->max_fw_cmds; i++) {
		cmd = fusion->cmd_list[i];
		offset = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE * i;
		memset(cmd, 0, sizeof(struct megasas_cmd_fusion));
		cmd->index = i + 1;
		cmd->scmd = NULL;
		cmd->sync_cmd_idx = (i >= instance->max_scsi_cmds) ?
				(i - instance->max_scsi_cmds) :
				(u32)ULONG_MAX; /* Set to Invalid */
		cmd->instance = instance;
		cmd->io_request =
			(struct MPI2_RAID_SCSI_IO_REQUEST *)
		  (io_req_base + offset);
		memset(cmd->io_request, 0,
		       sizeof(struct MPI2_RAID_SCSI_IO_REQUEST));
		cmd->io_request_phys_addr = io_req_base_phys + offset;
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
	struct fusion_context *fusion;

	u32 msecs = seconds * 1000;

	fusion = instance->ctrl_context;
	/*
	 * Wait for cmd_status to change
	 */
	for (i = 0; (i < msecs) && (frame_hdr->cmd_status == 0xff); i += 20) {
		rmb();
		msleep(20);
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
	u32 scratch_pad_2;

	fusion = instance->ctrl_context;

	cmd = megasas_get_cmd(instance);

	if (!cmd) {
		dev_err(&instance->pdev->dev, "Could not allocate cmd for INIT Frame\n");
		ret = 1;
		goto fail_get_cmd;
	}

	scratch_pad_2 = readl
		(&instance->reg_set->outbound_scratch_pad_2);

	cur_rdpq_mode = (scratch_pad_2 & MR_RDPQ_MODE_OFFSET) ? 1 : 0;

	if (instance->is_rdpq && !cur_rdpq_mode) {
		dev_err(&instance->pdev->dev, "Firmware downgrade *NOT SUPPORTED*"
			" from RDPQ mode to non RDPQ mode\n");
		ret = 1;
		goto fail_fw_init;
	}

	IOCInitMessage =
	  dma_alloc_coherent(&instance->pdev->dev,
			     sizeof(struct MPI2_IOC_INIT_REQUEST),
			     &ioc_init_handle, GFP_KERNEL);

	if (!IOCInitMessage) {
		dev_err(&instance->pdev->dev, "Could not allocate memory for "
		       "IOCInitMessage\n");
		ret = 1;
		goto fail_fw_init;
	}

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
	IOCInitMessage->HostMSIxVectors = instance->msix_vectors;
	init_frame = (struct megasas_init_frame *)cmd->frame;
	memset(init_frame, 0, MEGAMFI_FRAME_SIZE);

	frame_hdr = &cmd->frame->hdr;
	frame_hdr->cmd_status = 0xFF;
	frame_hdr->flags = cpu_to_le16(
		le16_to_cpu(frame_hdr->flags) |
		MFI_FRAME_DONT_POST_IN_REPLY_QUEUE);

	init_frame->cmd	= MFI_CMD_INIT;
	init_frame->cmd_status = 0xFF;

	drv_ops = (MFI_CAPABILITIES *) &(init_frame->driver_operations);

	/* driver support Extended MSIX */
	if (fusion->adapter_type == INVADER_SERIES)
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
	/* Convert capability to LE32 */
	cpu_to_le32s((u32 *)&init_frame->driver_operations.mfi_capabilities);

	sys_info = dmi_get_system_info(DMI_PRODUCT_UUID);
	if (instance->system_info_buf && sys_info) {
		memcpy(instance->system_info_buf->systemId, sys_info,
			strlen(sys_info) > 64 ? 64 : strlen(sys_info));
		instance->system_info_buf->systemIdLength =
			strlen(sys_info) > 64 ? 64 : strlen(sys_info);
		init_frame->system_info_lo = instance->system_info_h;
		init_frame->system_info_hi = 0;
	}

	init_frame->queue_info_new_phys_addr_hi =
		cpu_to_le32(upper_32_bits(ioc_init_handle));
	init_frame->queue_info_new_phys_addr_lo =
		cpu_to_le32(lower_32_bits(ioc_init_handle));
	init_frame->data_xfer_len = cpu_to_le32(sizeof(struct MPI2_IOC_INIT_REQUEST));

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
		if (readl(&instance->reg_set->doorbell) & 1)
			msleep(20);
		else
			break;
	}

	megasas_fire_cmd_fusion(instance, &req_desc);

	wait_and_poll(instance, cmd, MFI_POLL_TIMEOUT_SECS);

	frame_hdr = &cmd->frame->hdr;
	if (frame_hdr->cmd_status != 0) {
		ret = 1;
		goto fail_fw_init;
	}
	dev_info(&instance->pdev->dev, "Init cmd success\n");

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
	u32 pd_seq_map_sz;
	struct megasas_cmd *cmd;
	struct megasas_dcmd_frame *dcmd;
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	dma_addr_t pd_seq_h;

	pd_sync = (void *)fusion->pd_seq_sync[(instance->pd_seq_map_id & 1)];
	pd_seq_h = fusion->pd_seq_phys[(instance->pd_seq_map_id & 1)];
	pd_seq_map_sz = sizeof(struct MR_PD_CFG_SEQ_NUM_SYNC) +
			(sizeof(struct MR_PD_CFG_SEQ) *
			(MAX_PHYSICAL_DEVICES - 1));

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
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(pd_seq_map_sz);
	dcmd->opcode = cpu_to_le32(MR_DCMD_SYSTEM_PD_MAP_GET_INFO);
	dcmd->sgl.sge32[0].phys_addr = cpu_to_le32(pd_seq_h);
	dcmd->sgl.sge32[0].length = cpu_to_le32(pd_seq_map_sz);

	if (pend) {
		dcmd->mbox.b[0] = MEGASAS_DCMD_MBOX_PEND_FLAG;
		dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_WRITE);
		instance->jbod_seq_cmd = cmd;
		instance->instancet->issue_dcmd(instance, cmd);
		return 0;
	}

	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_READ);

	/* Below code is only for non pended DCMD */
	if (instance->ctrl_context && !instance->mask_interrupts)
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

	if (ret == DCMD_TIMEOUT && instance->ctrl_context)
		megaraid_sas_kill_hba(instance);

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
#if VD_EXT_DEBUG
	dev_dbg(&instance->pdev->dev,
		"%s sending MR_DCMD_LD_MAP_GET_INFO with size %d\n",
		__func__, cpu_to_le32(size_map_info));
#endif
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_READ);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(size_map_info);
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_MAP_GET_INFO);
	dcmd->sgl.sge32[0].phys_addr = cpu_to_le32(ci_h);
	dcmd->sgl.sge32[0].length = cpu_to_le32(size_map_info);

	if (instance->ctrl_context && !instance->mask_interrupts)
		ret = megasas_issue_blocked_cmd(instance, cmd,
			MFI_IO_TIMEOUT_SECS);
	else
		ret = megasas_issue_polled(instance, cmd);

	if (ret == DCMD_TIMEOUT && instance->ctrl_context)
		megaraid_sas_kill_hba(instance);

	megasas_return_cmd(instance, cmd);

	return ret;
}

u8
megasas_get_map_info(struct megasas_instance *instance)
{
	struct fusion_context *fusion = instance->ctrl_context;

	fusion->fast_path_io = 0;
	if (!megasas_get_ld_map_info(instance)) {
		if (MR_ValidateMapInfo(instance)) {
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

	size_sync_info = sizeof(struct MR_LD_TARGET_SYNC) *num_lds;

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
	dcmd->flags = cpu_to_le16(MFI_FRAME_DIR_WRITE);
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = cpu_to_le32(size_map_info);
	dcmd->mbox.b[0] = num_lds;
	dcmd->mbox.b[1] = MEGASAS_DCMD_MBOX_PEND_FLAG;
	dcmd->opcode = cpu_to_le32(MR_DCMD_LD_MAP_GET_INFO);
	dcmd->sgl.sge32[0].phys_addr = cpu_to_le32(ci_h);
	dcmd->sgl.sge32[0].length = cpu_to_le32(size_map_info);

	instance->map_update_cmd = cmd;

	instance->instancet->issue_dcmd(instance, cmd);

	return ret;
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
	u32 max_cmd, scratch_pad_2;
	int i = 0, count;

	fusion = instance->ctrl_context;

	reg_set = instance->reg_set;

	megasas_fusion_update_can_queue(instance, PROBE_CONTEXT);

	/*
	 * Reduce the max supported cmds by 1. This is to ensure that the
	 * reply_q_sz (1 more than the max cmd that driver may send)
	 * does not exceed max cmds that the FW can support
	 */
	instance->max_fw_cmds = instance->max_fw_cmds-1;

	/*
	 * Only Driver's internal DCMDs and IOCTL DCMDs needs to have MFI frames
	 */
	instance->max_mfi_cmds =
		MEGASAS_FUSION_INTERNAL_CMDS + MEGASAS_FUSION_IOCTL_CMDS;

	max_cmd = instance->max_fw_cmds;

	fusion->reply_q_depth = 2 * (((max_cmd + 1 + 15)/16)*16);

	fusion->request_alloc_sz =
		sizeof(union MEGASAS_REQUEST_DESCRIPTOR_UNION) *max_cmd;
	fusion->reply_alloc_sz = sizeof(union MPI2_REPLY_DESCRIPTORS_UNION)
		*(fusion->reply_q_depth);
	fusion->io_frames_alloc_sz = MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE +
		(MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE *
		 (max_cmd + 1)); /* Extra 1 for SMID 0 */

	scratch_pad_2 = readl(&instance->reg_set->outbound_scratch_pad_2);
	/* If scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK is set,
	 * Firmware support extended IO chain frame which is 4 times more than
	 * legacy Firmware.
	 * Legacy Firmware - Frame size is (8 * 128) = 1K
	 * 1M IO Firmware  - Frame size is (8 * 128 * 4)  = 4K
	 */
	if (scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK)
		instance->max_chain_frame_sz =
			((scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_MASK) >>
			MEGASAS_MAX_CHAIN_SHIFT) * MEGASAS_1MB_IO;
	else
		instance->max_chain_frame_sz =
			((scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_MASK) >>
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
	 * For fusion adapters, 3 commands for IOCTL and 5 commands
	 * for driver's internal DCMDs.
	 */
	instance->max_scsi_cmds = instance->max_fw_cmds -
				(MEGASAS_FUSION_INTERNAL_CMDS +
				MEGASAS_FUSION_IOCTL_CMDS);
	sema_init(&instance->ioctl_sem, MEGASAS_FUSION_IOCTL_CMDS);

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

	megasas_display_intel_branding(instance);
	if (megasas_get_ctrl_info(instance)) {
		dev_err(&instance->pdev->dev,
			"Could not get controller info. Fail from %s %d\n",
			__func__, __LINE__);
		goto fail_ioc_init;
	}

	instance->flag_ieee = 1;
	fusion->fast_path_io = 0;

	fusion->drv_map_pages = get_order(fusion->drv_map_sz);
	for (i = 0; i < 2; i++) {
		fusion->ld_map[i] = NULL;
		fusion->ld_drv_map[i] = (void *)__get_free_pages(GFP_KERNEL,
			fusion->drv_map_pages);
		if (!fusion->ld_drv_map[i]) {
			dev_err(&instance->pdev->dev, "Could not allocate "
				"memory for local map info for %d pages\n",
				fusion->drv_map_pages);
			if (i == 1)
				free_pages((ulong)fusion->ld_drv_map[0],
					fusion->drv_map_pages);
			goto fail_ioc_init;
		}
		memset(fusion->ld_drv_map[i], 0,
			((1 << PAGE_SHIFT) << fusion->drv_map_pages));
	}

	for (i = 0; i < 2; i++) {
		fusion->ld_map[i] = dma_alloc_coherent(&instance->pdev->dev,
						       fusion->max_map_sz,
						       &fusion->ld_map_phys[i],
						       GFP_KERNEL);
		if (!fusion->ld_map[i]) {
			dev_err(&instance->pdev->dev, "Could not allocate memory "
			       "for map info\n");
			goto fail_map_info;
		}
	}

	if (!megasas_get_map_info(instance))
		megasas_sync_map_info(instance);

	return 0;

fail_map_info:
	if (i == 1)
		dma_free_coherent(&instance->pdev->dev, fusion->max_map_sz,
				  fusion->ld_map[0], fusion->ld_map_phys[0]);
fail_ioc_init:
	megasas_free_cmds_fusion(instance);
fail_alloc_cmds:
	megasas_free_cmds(instance);
fail_alloc_mfi_cmds:
	return 1;
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
		dev_printk(KERN_DEBUG, &cmd->instance->pdev->dev, "FW status %#x\n", status);
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

	if (fusion->adapter_type == INVADER_SERIES) {
		struct MPI25_IEEE_SGE_CHAIN64 *sgl_ptr_end = sgl_ptr;
		sgl_ptr_end += fusion->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}

	sge_count = scsi_dma_map(scp);

	BUG_ON(sge_count < 0);

	if (sge_count > instance->max_num_sge || !sge_count)
		return sge_count;

	scsi_for_each_sg(scp, os_sgl, sge_count, i) {
		sgl_ptr->Length = cpu_to_le32(sg_dma_len(os_sgl));
		sgl_ptr->Address = cpu_to_le64(sg_dma_address(os_sgl));
		sgl_ptr->Flags = 0;
		if (fusion->adapter_type == INVADER_SERIES)
			if (i == sge_count - 1)
				sgl_ptr->Flags = IEEE_SGE_FLAGS_END_OF_LIST;
		sgl_ptr++;

		sg_processed = i + 1;

		if ((sg_processed ==  (fusion->max_sge_in_main_msg - 1)) &&
		    (sge_count > fusion->max_sge_in_main_msg)) {

			struct MPI25_IEEE_SGE_CHAIN64 *sg_chain;
			if (fusion->adapter_type == INVADER_SERIES) {
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
			if (fusion->adapter_type == INVADER_SERIES)
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
		   struct MR_DRV_RAID_MAP_ALL *local_map_ptr, u32 ref_tag)
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
		io_request->CDB.EEDP32.PrimaryApplicationTagMask = cpu_to_be16(0xffff);
		io_request->IoFlags = cpu_to_le16(32); /* Specify 32-byte cdb */

		/* Transfer length */
		cdb[28] = (u8)((num_blocks >> 24) & 0xff);
		cdb[29] = (u8)((num_blocks >> 16) & 0xff);
		cdb[30] = (u8)((num_blocks >> 8) & 0xff);
		cdb[31] = (u8)(num_blocks & 0xff);

		/* set SCSI IO EEDPFlags */
		if (scp->sc_data_direction == PCI_DMA_FROMDEVICE) {
			io_request->EEDPFlags = cpu_to_le16(
				MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG  |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP |
				MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG |
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
	u32 start_lba_lo, start_lba_hi, device_id, datalength = 0;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	struct IO_REQUEST_INFO io_info;
	struct fusion_context *fusion;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	u8 *raidLUN;

	device_id = MEGASAS_DEV_INDEX(scp);

	fusion = instance->ctrl_context;

	io_request = cmd->io_request;
	io_request->RaidContext.VirtualDiskTgtId = cpu_to_le16(device_id);
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
	io_request->DataLength = cpu_to_le32(scsi_bufflen(scp));

	if (scp->sc_data_direction == PCI_DMA_FROMDEVICE)
		io_info.isRead = 1;

	local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];

	if ((MR_TargetIdToLdGet(device_id, local_map_ptr) >=
		instance->fw_supported_vd_count) || (!fusion->fast_path_io)) {
		io_request->RaidContext.regLockFlags  = 0;
		fp_possible = 0;
	} else {
		if (MR_BuildRaidContext(instance, &io_info,
					&io_request->RaidContext,
					local_map_ptr, &raidLUN))
			fp_possible = io_info.fpOkForIo;
	}

	/* Use raw_smp_processor_id() for now until cmd->request->cpu is CPU
	   id by default, not CPU group id, otherwise all MSI-X queues won't
	   be utilized */
	cmd->request_desc->SCSIIO.MSIxIndex = instance->msix_vectors ?
		raw_smp_processor_id() % instance->msix_vectors : 0;

	if (fp_possible) {
		megasas_set_pd_lba(io_request, scp->cmd_len, &io_info, scp,
				   local_map_ptr, start_lba_lo);
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_FP_IO
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (fusion->adapter_type == INVADER_SERIES) {
			if (io_request->RaidContext.regLockFlags ==
			    REGION_TYPE_UNUSED)
				cmd->request_desc->SCSIIO.RequestFlags =
					(MEGASAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
					MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			io_request->RaidContext.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.nseg = 0x1;
			io_request->IoFlags |= cpu_to_le16(MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH);
			io_request->RaidContext.regLockFlags |=
			  (MR_RL_FLAGS_GRANT_DESTINATION_CUDA |
			   MR_RL_FLAGS_SEQ_NUM_ENABLE);
		}
		if ((fusion->load_balance_info[device_id].loadBalanceFlag) &&
		    (io_info.isRead)) {
			io_info.devHandle =
				get_updated_dev_handle(instance,
					&fusion->load_balance_info[device_id],
					&io_info);
			scp->SCp.Status |= MEGASAS_LOAD_BALANCE_FLAG;
			cmd->pd_r1_lb = io_info.pd_after_lb;
		} else
			scp->SCp.Status &= ~MEGASAS_LOAD_BALANCE_FLAG;

		if ((raidLUN[0] == 1) &&
			(local_map_ptr->raidMap.devHndlInfo[io_info.pd_after_lb].validHandles > 1)) {
			instance->dev_handle = !(instance->dev_handle);
			io_info.devHandle =
				local_map_ptr->raidMap.devHndlInfo[io_info.pd_after_lb].devHandle[instance->dev_handle];
		}

		cmd->request_desc->SCSIIO.DevHandle = io_info.devHandle;
		io_request->DevHandle = io_info.devHandle;
		/* populate the LUN field */
		memcpy(io_request->LUN, raidLUN, 8);
	} else {
		io_request->RaidContext.timeoutValue =
			cpu_to_le16(local_map_ptr->raidMap.fpPdIoTimeoutSec);
		cmd->request_desc->SCSIIO.RequestFlags =
			(MEGASAS_REQ_DESCRIPT_FLAGS_LD_IO
			 << MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (fusion->adapter_type == INVADER_SERIES) {
			if (io_info.do_fp_rlbypass ||
				(io_request->RaidContext.regLockFlags == REGION_TYPE_UNUSED))
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
	u16 pd_index = 0;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	struct fusion_context *fusion = instance->ctrl_context;
	u8                          span, physArm;
	__le16                      devHandle;
	u32                         ld, arRef, pd;
	struct MR_LD_RAID                  *raid;
	struct RAID_CONTEXT                *pRAID_Context;
	u8 fp_possible = 1;

	io_request = cmd->io_request;
	device_id = MEGASAS_DEV_INDEX(scmd);
	pd_index = MEGASAS_PD_INDEX(scmd);
	local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];
	io_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext;
	/* Check with FW team */
	pRAID_Context->VirtualDiskTgtId = cpu_to_le16(device_id);
	pRAID_Context->regLockRowLBA    = 0;
	pRAID_Context->regLockLength    = 0;

	if (fusion->fast_path_io && (
		device_id < instance->fw_supported_vd_count)) {

		ld = MR_TargetIdToLdGet(device_id, local_map_ptr);
		if (ld >= instance->fw_supported_vd_count)
			fp_possible = 0;

		raid = MR_LdRaidGet(ld, local_map_ptr);
		if (!(raid->capability.fpNonRWCapable))
			fp_possible = 0;
	} else
		fp_possible = 0;

	if (!fp_possible) {
		io_request->Function  = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = cpu_to_le16(device_id);
		io_request->LUN[1] = scmd->device->lun;
		pRAID_Context->timeoutValue =
			cpu_to_le16 (scmd->request->timeout / HZ);
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
			MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	} else {

		/* set RAID context values */
		pRAID_Context->configSeqNum = raid->seqNum;
		pRAID_Context->regLockFlags = REGION_TYPE_SHARED_READ;
		pRAID_Context->timeoutValue = cpu_to_le16(raid->fpIoTimeoutForLd);

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
	struct scsi_cmnd *scmd, struct megasas_cmd_fusion *cmd, u8 fp_possible)
{
	u32 device_id;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request;
	u16 pd_index = 0;
	u16 os_timeout_value;
	u16 timeout_limit;
	struct MR_DRV_RAID_MAP_ALL *local_map_ptr;
	struct RAID_CONTEXT	*pRAID_Context;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	struct fusion_context *fusion = instance->ctrl_context;
	pd_sync = (void *)fusion->pd_seq_sync[(instance->pd_seq_map_id - 1) & 1];

	device_id = MEGASAS_DEV_INDEX(scmd);
	pd_index = MEGASAS_PD_INDEX(scmd);
	os_timeout_value = scmd->request->timeout / HZ;

	io_request = cmd->io_request;
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext;
	pRAID_Context->regLockFlags = 0;
	pRAID_Context->regLockRowLBA = 0;
	pRAID_Context->regLockLength = 0;
	io_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	io_request->LUN[1] = scmd->device->lun;
	pRAID_Context->RAIDFlags = MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD
		<< MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT;

	/* If FW supports PD sequence number */
	if (instance->use_seqnum_jbod_fp &&
		instance->pd_list[pd_index].driveType == TYPE_DISK) {
		/* TgtId must be incremented by 255 as jbod seq number is index
		 * below raid map
		 */
		pRAID_Context->VirtualDiskTgtId =
			cpu_to_le16(device_id + (MAX_PHYSICAL_DEVICES - 1));
		pRAID_Context->configSeqNum = pd_sync->seq[pd_index].seqNum;
		io_request->DevHandle = pd_sync->seq[pd_index].devHandle;
		pRAID_Context->regLockFlags |=
			(MR_RL_FLAGS_SEQ_NUM_ENABLE|MR_RL_FLAGS_GRANT_DESTINATION_CUDA);
		pRAID_Context->Type = MPI2_TYPE_CUDA;
		pRAID_Context->nseg = 0x1;
	} else if (fusion->fast_path_io) {
		pRAID_Context->VirtualDiskTgtId = cpu_to_le16(device_id);
		pRAID_Context->configSeqNum = 0;
		local_map_ptr = fusion->ld_drv_map[(instance->map_id & 1)];
		io_request->DevHandle =
			local_map_ptr->raidMap.devHndlInfo[device_id].curDevHdl;
	} else {
		/* Want to send all IO via FW path */
		pRAID_Context->VirtualDiskTgtId = cpu_to_le16(device_id);
		pRAID_Context->configSeqNum = 0;
		io_request->DevHandle = cpu_to_le16(0xFFFF);
	}

	cmd->request_desc->SCSIIO.DevHandle = io_request->DevHandle;
	cmd->request_desc->SCSIIO.MSIxIndex =
		instance->msix_vectors ?
		(raw_smp_processor_id() % instance->msix_vectors) : 0;


	if (!fp_possible) {
		/* system pd firmware path */
		io_request->Function  = MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
			(MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
				MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		pRAID_Context->timeoutValue = cpu_to_le16(os_timeout_value);
		pRAID_Context->VirtualDiskTgtId = cpu_to_le16(device_id);
	} else {
		/* system pd Fast Path */
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		timeout_limit = (scmd->device->type == TYPE_DISK) ?
				255 : 0xFFFF;
		pRAID_Context->timeoutValue =
			cpu_to_le16((os_timeout_value > timeout_limit) ?
			timeout_limit : os_timeout_value);
		if (fusion->adapter_type == INVADER_SERIES)
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
int
megasas_build_io_fusion(struct megasas_instance *instance,
			struct scsi_cmnd *scp,
			struct megasas_cmd_fusion *cmd)
{
	u16 sge_count;
	u8  cmd_type;
	struct MPI2_RAID_SCSI_IO_REQUEST *io_request = cmd->io_request;

	/* Zero out some fields so they don't get reused */
	memset(io_request->LUN, 0x0, 8);
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
	io_request->IoFlags = cpu_to_le16(scp->cmd_len);

	switch (cmd_type = megasas_cmd_type(scp)) {
	case READ_WRITE_LDIO:
		megasas_build_ldio_fusion(instance, scp, cmd);
		break;
	case NON_READ_WRITE_LDIO:
		megasas_build_ld_nonrw_fusion(instance, scp, cmd);
		break;
	case READ_WRITE_SYSPDIO:
	case NON_READ_WRITE_SYSPDIO:
		if (instance->secure_jbod_support &&
			(cmd_type == NON_READ_WRITE_SYSPDIO))
			megasas_build_syspd_fusion(instance, scp, cmd, 0);
		else
			megasas_build_syspd_fusion(instance, scp, cmd, 1);
		break;
	default:
		break;
	}

	/*
	 * Construct SGL
	 */

	sge_count =
		megasas_make_sgl_fusion(instance, scp,
					(struct MPI25_IEEE_SGE_CHAIN64 *)
					&io_request->SGL, cmd);

	if (sge_count > instance->max_num_sge) {
		dev_err(&instance->pdev->dev, "Error. sge_count (0x%x) exceeds "
		       "max (0x%x) allowed\n", sge_count,
		       instance->max_num_sge);
		return 1;
	}

	/* numSGE store lower 8 bit of sge_count.
	 * numSGEExt store higher 8 bit of sge_count
	 */
	io_request->RaidContext.numSGE = sge_count;
	io_request->RaidContext.numSGEExt = (u8)(sge_count >> 8);

	io_request->SGLFlags = cpu_to_le16(MPI2_SGE_FLAGS_64_BIT_ADDRESSING);

	if (scp->sc_data_direction == PCI_DMA_TODEVICE)
		io_request->Control |= cpu_to_le32(MPI2_SCSIIO_CONTROL_WRITE);
	else if (scp->sc_data_direction == PCI_DMA_FROMDEVICE)
		io_request->Control |= cpu_to_le32(MPI2_SCSIIO_CONTROL_READ);

	io_request->SGLOffset0 =
		offsetof(struct MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4;

	io_request->SenseBufferLowAddress = cpu_to_le32(cmd->sense_phys_addr);
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
		dev_err(&instance->pdev->dev, "Invalid SMID (0x%x)request for "
		       "descriptor for scsi%d\n", index,
			instance->host->host_no);
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

	if ((megasas_cmd_type(scmd) == READ_WRITE_LDIO) &&
		instance->ldio_threshold &&
		(atomic_inc_return(&instance->ldio_outstanding) >
		instance->ldio_threshold)) {
		atomic_dec(&instance->ldio_outstanding);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	cmd = megasas_get_cmd_fusion(instance, scmd->request->tag);

	index = cmd->index;

	req_desc = megasas_get_request_descriptor(instance, index-1);
	if (!req_desc)
		return SCSI_MLQUEUE_HOST_BUSY;

	req_desc->Words = 0;
	cmd->request_desc = req_desc;

	if (megasas_build_io_fusion(instance, scmd, cmd)) {
		megasas_return_cmd_fusion(instance, cmd);
		dev_err(&instance->pdev->dev, "Error building command\n");
		cmd->request_desc = NULL;
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	req_desc = cmd->request_desc;
	req_desc->SCSIIO.SMID = cpu_to_le16(index);

	if (cmd->io_request->ChainOffset != 0 &&
	    cmd->io_request->ChainOffset != 0xF)
		dev_err(&instance->pdev->dev, "The chain offset value is not "
		       "correct : %x\n", cmd->io_request->ChainOffset);

	/*
	 * Issue the command to the FW
	 */
	atomic_inc(&instance->fw_outstanding);

	megasas_fire_cmd_fusion(instance, req_desc);

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
	u8 reply_descript_type;
	u32 status, extStatus, device_id;
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

		scsi_io_req =
			(struct MPI2_RAID_SCSI_IO_REQUEST *)
		  cmd_fusion->io_request;

		if (cmd_fusion->scmd)
			cmd_fusion->scmd->SCp.ptr = NULL;

		scmd_local = cmd_fusion->scmd;
		status = scsi_io_req->RaidContext.status;
		extStatus = scsi_io_req->RaidContext.exStatus;

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
			device_id = MEGASAS_DEV_INDEX(scmd_local);
			lbinfo = &fusion->load_balance_info[device_id];
			if (cmd_fusion->scmd->SCp.Status &
			    MEGASAS_LOAD_BALANCE_FLAG) {
				atomic_dec(&lbinfo->scsi_pending_cmds[cmd_fusion->pd_r1_lb]);
				cmd_fusion->scmd->SCp.Status &=
					~MEGASAS_LOAD_BALANCE_FLAG;
			}
			if (reply_descript_type ==
			    MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS) {
				if (megasas_dbg_lvl == 5)
					dev_err(&instance->pdev->dev, "\nFAST Path "
					       "IO Success\n");
			}
			/* Fall thru and complete IO */
		case MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST: /* LD-IO Path */
			/* Map the FW Cmd Status */
			map_cmd_status(cmd_fusion, status, extStatus);
			scsi_io_req->RaidContext.status = 0;
			scsi_io_req->RaidContext.exStatus = 0;
			if (megasas_cmd_type(scmd_local) == READ_WRITE_LDIO)
				atomic_dec(&instance->ldio_outstanding);
			megasas_return_cmd_fusion(instance, cmd_fusion);
			scsi_dma_unmap(scmd_local);
			scmd_local->scsi_done(scmd_local);
			atomic_dec(&instance->fw_outstanding);

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
		if (threshold_reply_count >= THRESHOLD_REPLY_COUNT) {
			if (fusion->adapter_type == INVADER_SERIES)
				writel(((MSIxIndex & 0x7) << 24) |
					fusion->last_reply_idx[MSIxIndex],
					instance->reply_post_host_index_addr[MSIxIndex/8]);
			else
				writel((MSIxIndex << 24) |
					fusion->last_reply_idx[MSIxIndex],
					instance->reply_post_host_index_addr[0]);
			threshold_reply_count = 0;
		}
	}

	if (!num_completed)
		return IRQ_NONE;

	wmb();
	if (fusion->adapter_type == INVADER_SERIES)
		writel(((MSIxIndex & 0x7) << 24) |
			fusion->last_reply_idx[MSIxIndex],
			instance->reply_post_host_index_addr[MSIxIndex/8]);
	else
		writel((MSIxIndex << 24) |
			fusion->last_reply_idx[MSIxIndex],
			instance->reply_post_host_index_addr[0]);
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
	if (atomic_read(&instance->adprecovery) == MEGASAS_HW_CRITICAL_ERROR) {
		spin_unlock_irqrestore(&instance->hba_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&instance->hba_lock, flags);

	for (MSIxIndex = 0 ; MSIxIndex < count; MSIxIndex++)
		complete_cmd_fusion(instance, MSIxIndex);
}

/**
 * megasas_isr_fusion - isr entry point
 */
irqreturn_t megasas_isr_fusion(int irq, void *devp)
{
	struct megasas_irq_context *irq_context = devp;
	struct megasas_instance *instance = irq_context->instance;
	u32 mfiStatus, fw_state, dma_state;

	if (instance->mask_interrupts)
		return IRQ_NONE;

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
		dma_state = instance->instancet->read_fw_status_reg
			(instance->reg_set) & MFI_STATE_DMADONE;
		if (instance->crash_dump_drv_support &&
			instance->crash_dump_app_support) {
			/* Start collecting crash, if DMA bit is done */
			if ((fw_state == MFI_STATE_FAULT) && dma_state)
				schedule_work(&instance->crash_init);
			else if (fw_state == MFI_STATE_FAULT)
				schedule_work(&instance->work_init);
		} else if (fw_state == MFI_STATE_FAULT) {
			dev_warn(&instance->pdev->dev, "Iop2SysDoorbellInt"
			       "for scsi%d\n", instance->host->host_no);
			schedule_work(&instance->work_init);
		}
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

	if (fusion->adapter_type == INVADER_SERIES) {
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

	mpi25_ieee_chain->Length = cpu_to_le32(instance->max_chain_frame_sz);

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
		dev_err(&instance->pdev->dev, "Couldn't build MFI pass thru cmd\n");
		return NULL;
	}

	index = cmd->context.smid;

	req_desc = megasas_get_request_descriptor(instance, index - 1);

	if (!req_desc)
		return NULL;

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
int
megasas_issue_dcmd_fusion(struct megasas_instance *instance,
			  struct megasas_cmd *cmd)
{
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	req_desc = build_mpt_cmd(instance, cmd);
	if (!req_desc) {
		dev_info(&instance->pdev->dev, "Failed from %s %d\n",
					__func__, __LINE__);
		return DCMD_NOT_FIRED;
	}

	megasas_fire_cmd_fusion(instance, req_desc);
	return DCMD_SUCCESS;
}

/**
 * megasas_release_fusion -	Reverses the FW initialization
 * @instance:			Adapter soft state
 */
void
megasas_release_fusion(struct megasas_instance *instance)
{
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
megasas_read_fw_status_reg_fusion(struct megasas_register_set __iomem *regs)
{
	return readl(&(regs)->outbound_scratch_pad);
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

	instance->crash_buf_pages = get_order(CRASH_DMA_BUF_SIZE);
	for (i = 0; i < MAX_CRASH_DUMP_SIZE; i++) {
		instance->crash_buf[i] = (void	*)__get_free_pages(GFP_KERNEL,
				instance->crash_buf_pages);
		if (!instance->crash_buf[i]) {
			dev_info(&instance->pdev->dev, "Firmware crash dump "
				"memory allocation failed at index %d\n", i);
			break;
		}
		memset(instance->crash_buf[i], 0,
			((1 << PAGE_SHIFT) << instance->crash_buf_pages));
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
	unsigned int i
;
	for (i = 0; i < instance->drv_buf_alloc; i++) {
		if (instance->crash_buf[i])
			free_pages((ulong)instance->crash_buf[i],
					instance->crash_buf_pages);
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
	host_diag = readl(&instance->reg_set->fusion_host_diag);
	retry = 0;
	while (!(host_diag & HOST_DIAG_WRITE_ENABLE)) {
		msleep(100);
		host_diag = readl(&instance->reg_set->fusion_host_diag);
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
	host_diag = readl(&instance->reg_set->fusion_host_diag);
	retry = 0;
	while (host_diag & HOST_DIAG_RESET_ADAPTER) {
		msleep(100);
		host_diag = readl(&instance->reg_set->fusion_host_diag);
		if (retry++ == 1000) {
			dev_warn(&instance->pdev->dev,
				"Diag reset adapter never cleared %s %d\n",
				__func__, __LINE__);
			break;
		}
	}
	if (host_diag & HOST_DIAG_RESET_ADAPTER)
		return -1;

	abs_state = instance->instancet->read_fw_status_reg(instance->reg_set)
			& MFI_STATE_MASK;
	retry = 0;

	while ((abs_state <= MFI_STATE_FW_INIT) && (retry++ < 1000)) {
		msleep(100);
		abs_state = instance->instancet->
			read_fw_status_reg(instance->reg_set) & MFI_STATE_MASK;
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

/* This function waits for outstanding commands on fusion to complete */
int megasas_wait_for_outstanding_fusion(struct megasas_instance *instance,
					int reason, int *convert)
{
	int i, outstanding, retval = 0, hb_seconds_missed = 0;
	u32 fw_state;

	for (i = 0; i < resetwaittime; i++) {
		/* Check if firmware is in fault state */
		fw_state = instance->instancet->read_fw_status_reg(
			instance->reg_set) & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT) {
			dev_warn(&instance->pdev->dev, "Found FW in FAULT state,"
			       " will reset adapter scsi%d.\n",
				instance->host->host_no);
			megasas_complete_cmd_dpc_fusion((unsigned long)instance);
			retval = 1;
			goto out;
		}

		if (reason == MFI_IO_TIMEOUT_OCR) {
			dev_info(&instance->pdev->dev,
				"MFI IO is timed out, initiating OCR\n");
			megasas_complete_cmd_dpc_fusion((unsigned long)instance);
			retval = 1;
			goto out;
		}

		/* If SR-IOV VF mode & heartbeat timeout, don't wait */
		if (instance->requestorId && !reason) {
			retval = 1;
			goto out;
		}

		/* If SR-IOV VF mode & I/O timeout, check for HB timeout */
		if (instance->requestorId && reason) {
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

		outstanding = atomic_read(&instance->fw_outstanding);
		if (!outstanding)
			goto out;

		if (!(i % MEGASAS_RESET_NOTICE_INTERVAL)) {
			dev_notice(&instance->pdev->dev, "[%2d]waiting for %d "
			       "commands to complete for scsi%d\n", i,
			       outstanding, instance->host->host_no);
			megasas_complete_cmd_dpc_fusion(
				(unsigned long)instance);
		}
		msleep(1000);
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
void megasas_refire_mgmt_cmd(struct megasas_instance *instance)
{
	int j;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	struct megasas_cmd *cmd_mfi;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u16 smid;
	bool refire_cmd = 0;

	fusion = instance->ctrl_context;

	/* Re-fire management commands.
	 * Do not traverse complet MPT frame pool. Start from max_scsi_cmds.
	 */
	for (j = instance->max_scsi_cmds ; j < instance->max_fw_cmds; j++) {
		cmd_fusion = fusion->cmd_list[j];
		cmd_mfi = instance->cmd_list[cmd_fusion->sync_cmd_idx];
		smid = le16_to_cpu(cmd_mfi->context.smid);

		if (!smid)
			continue;
		req_desc = megasas_get_request_descriptor
					(instance, smid - 1);
		refire_cmd = req_desc && ((cmd_mfi->frame->dcmd.opcode !=
				cpu_to_le32(MR_DCMD_LD_MAP_GET_INFO)) &&
				 (cmd_mfi->frame->dcmd.opcode !=
				cpu_to_le32(MR_DCMD_SYSTEM_PD_MAP_GET_INFO)))
				&& !(cmd_mfi->flags & DRV_DCMD_SKIP_REFIRE);
		if (refire_cmd)
			megasas_fire_cmd_fusion(instance, req_desc);
		else
			megasas_return_cmd(instance, cmd_mfi);
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
	uint channel, uint id, u16 smid_task, u8 type)
{
	struct MR_TASK_MANAGE_REQUEST *mr_request;
	struct MPI2_SCSI_TASK_MANAGE_REQUEST *mpi_request;
	unsigned long timeleft;
	struct megasas_cmd_fusion *cmd_fusion;
	struct megasas_cmd *cmd_mfi;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	struct fusion_context *fusion;
	struct megasas_cmd_fusion *scsi_lookup;
	int rc;
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
	if (!req_desc) {
		dev_err(&instance->pdev->dev, "Failed from %s %d\n",
			__func__, __LINE__);
		megasas_return_cmd(instance, cmd_mfi);
		return -ENOMEM;
	}

	cmd_fusion->request_desc = req_desc;
	req_desc->Words = 0;

	scsi_lookup = fusion->cmd_list[smid_task - 1];

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

	timeleft = wait_for_completion_timeout(&cmd_fusion->done, 50 * HZ);

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
		if (scsi_lookup->scmd == NULL)
			break;
		else {
			instance->instancet->disable_intr(instance);
			msleep(1000);
			megasas_complete_cmd_dpc_fusion
					((unsigned long)instance);
			instance->instancet->enable_intr(instance);
			if (scsi_lookup->scmd == NULL)
				break;
		}
		rc = FAILED;
		break;

	case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		if ((channel == 0xFFFFFFFF) && (id == 0xFFFFFFFF))
			break;
		instance->instancet->disable_intr(instance);
		msleep(1000);
		megasas_complete_cmd_dpc_fusion
				((unsigned long)instance);
		rc = megasas_track_scsiio(instance, id, channel);
		instance->instancet->enable_intr(instance);

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

	if (sdev->channel < MEGASAS_MAX_PD_CHANNELS) {
		if (instance->use_seqnum_jbod_fp) {
				pd_index = (sdev->channel * MEGASAS_MAX_DEV_PER_CHANNEL) +
						sdev->id;
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
	struct fusion_context *fusion;
	int ret;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	mr_device_priv_data = scmd->device->hostdata;


	instance = (struct megasas_instance *)scmd->device->host->hostdata;
	fusion = instance->ctrl_context;

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
			" issued is not found in oustanding commands\n");
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
		"attempting task abort! scmd(%p) tm_dev_handle 0x%x\n",
		scmd, devhandle);

	mr_device_priv_data->tm_busy = 1;
	ret = megasas_issue_tm(instance, devhandle,
			scmd->device->channel, scmd->device->id, smid,
			MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK);
	mr_device_priv_data->tm_busy = 0;

	mutex_unlock(&instance->reset_mutex);
out:
	sdev_printk(KERN_INFO, scmd->device, "task abort: %s scmd(%p)\n",
			((ret == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

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
	struct fusion_context *fusion;
	struct MR_PRIV_DEVICE *mr_device_priv_data;
	mr_device_priv_data = scmd->device->hostdata;

	instance = (struct megasas_instance *)scmd->device->host->hostdata;
	fusion = instance->ctrl_context;

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
	devhandle = megasas_get_tm_devhandle(scmd->device);

	if (devhandle == (u16)ULONG_MAX) {
		ret = SUCCESS;
		sdev_printk(KERN_INFO, scmd->device,
			"target reset issued for invalid devhandle\n");
		mutex_unlock(&instance->reset_mutex);
		goto out;
	}

	sdev_printk(KERN_INFO, scmd->device,
		"attempting target reset! scmd(%p) tm_dev_handle 0x%x\n",
		scmd, devhandle);
	mr_device_priv_data->tm_busy = 1;
	ret = megasas_issue_tm(instance, devhandle,
			scmd->device->channel, scmd->device->id, 0,
			MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET);
	mr_device_priv_data->tm_busy = 0;
	mutex_unlock(&instance->reset_mutex);
out:
	scmd_printk(KERN_NOTICE, scmd, "megasas: target reset %s!!\n",
		(ret == SUCCESS) ? "SUCCESS" : "FAILED");

	return ret;
}

/*SRIOV get other instance in cluster if any*/
struct megasas_instance *megasas_get_peer_instance(struct megasas_instance *instance)
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
	int retval = (DID_RESET << 16);

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
	int retval = SUCCESS, i, convert = 0;
	struct megasas_instance *instance;
	struct megasas_cmd_fusion *cmd_fusion;
	struct fusion_context *fusion;
	u32 abs_state, status_reg, reset_adapter;
	u32 io_timeout_in_crash_mode = 0;
	struct scsi_cmnd *scmd_local = NULL;
	struct scsi_device *sdev;

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
	status_reg = instance->instancet->read_fw_status_reg(instance->reg_set);
	abs_state = status_reg & MFI_STATE_MASK;

	/* IO timeout detected, forcibly put FW in FAULT state */
	if (abs_state != MFI_STATE_FAULT && instance->crash_dump_buf &&
		instance->crash_dump_app_support && reason) {
		dev_info(&instance->pdev->dev, "IO/DCMD timeout is detected, "
			"forcibly FAULT Firmware\n");
		atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
		status_reg = readl(&instance->reg_set->doorbell);
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
	atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_POLLING);
	instance->instancet->disable_intr(instance);
	msleep(1000);

	/* First try waiting for commands to complete */
	if (megasas_wait_for_outstanding_fusion(instance, reason,
						&convert)) {
		atomic_set(&instance->adprecovery, MEGASAS_ADPRESET_SM_INFAULT);
		dev_warn(&instance->pdev->dev, "resetting fusion "
		       "adapter scsi%d.\n", instance->host->host_no);
		if (convert)
			reason = 0;

		/* Now return commands back to the OS */
		for (i = 0 ; i < instance->max_scsi_cmds; i++) {
			cmd_fusion = fusion->cmd_list[i];
			scmd_local = cmd_fusion->scmd;
			if (cmd_fusion->scmd) {
				scmd_local->result =
					megasas_check_mpio_paths(instance,
							scmd_local);
				if (megasas_cmd_type(scmd_local) == READ_WRITE_LDIO)
					atomic_dec(&instance->ldio_outstanding);
				megasas_return_cmd_fusion(instance, cmd_fusion);
				scsi_dma_unmap(scmd_local);
				scmd_local->scsi_done(scmd_local);
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
			dev_warn(&instance->pdev->dev, "Reset not supported"
			       ", killing adapter scsi%d.\n",
				instance->host->host_no);
			megaraid_sas_kill_hba(instance);
			instance->skip_heartbeat_timer_del = 1;
			retval = FAILED;
			goto out;
		}

		/* Let SR-IOV VF & PF sync up if there was a HB failure */
		if (instance->requestorId && !reason) {
			msleep(MEGASAS_OCR_SETTLE_TIME_VF);
			goto transition_to_ready;
		}

		/* Now try to reset the chip */
		for (i = 0; i < MEGASAS_FUSION_MAX_RESET_TRIES; i++) {

			if (instance->instancet->adp_reset
				(instance, instance->reg_set))
				continue;
transition_to_ready:
			/* Wait for FW to become ready */
			if (megasas_transition_to_ready(instance, 1)) {
				dev_warn(&instance->pdev->dev,
					"Failed to transition controller to ready for "
					"scsi%d.\n", instance->host->host_no);
				if (instance->requestorId && !reason)
					goto fail_kill_adapter;
				else
					continue;
			}
			megasas_reset_reply_desc(instance);
			megasas_fusion_update_can_queue(instance, OCR_CONTEXT);

			if (megasas_ioc_init_fusion(instance)) {
				dev_warn(&instance->pdev->dev,
				       "megasas_ioc_init_fusion() failed! for "
				       "scsi%d\n", instance->host->host_no);
				if (instance->requestorId && !reason)
					goto fail_kill_adapter;
				else
					continue;
			}

			megasas_refire_mgmt_cmd(instance);

			if (megasas_get_ctrl_info(instance)) {
				dev_info(&instance->pdev->dev,
					"Failed from %s %d\n",
					__func__, __LINE__);
				megaraid_sas_kill_hba(instance);
				retval = FAILED;
			}
			/* Reset load balance info */
			memset(fusion->load_balance_info, 0,
			       sizeof(struct LD_LOAD_BALANCE_INFO)
			       *MAX_LOGICAL_DRIVES_EXT);

			if (!megasas_get_map_info(instance))
				megasas_sync_map_info(instance);

			megasas_setup_jbod_map(instance);

			shost_for_each_device(sdev, shost)
				megasas_update_sdev_properties(sdev);

			clear_bit(MEGASAS_FUSION_IN_RESET,
				  &instance->reset_flags);
			instance->instancet->enable_intr(instance);
			atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);

			/* Restart SR-IOV heartbeat */
			if (instance->requestorId) {
				if (!megasas_sriov_start_heartbeat(instance, 0))
					megasas_start_timer(instance,
							    &instance->sriov_heartbeat_timer,
							    megasas_sriov_heartbeat_handler,
							    MEGASAS_SRIOV_HEARTBEAT_INTERVAL_VF);
				else
					instance->skip_heartbeat_timer_del = 1;
			}

			/* Adapter reset completed successfully */
			dev_warn(&instance->pdev->dev, "Reset "
			       "successful for scsi%d.\n",
				instance->host->host_no);

			if (instance->crash_dump_drv_support &&
				instance->crash_dump_app_support)
				megasas_set_crash_dump_params(instance,
					MR_CRASH_BUF_TURN_ON);
			else
				megasas_set_crash_dump_params(instance,
					MR_CRASH_BUF_TURN_OFF);

			retval = SUCCESS;
			goto out;
		}
fail_kill_adapter:
		/* Reset failed, kill the adapter */
		dev_warn(&instance->pdev->dev, "Reset failed, killing "
		       "adapter scsi%d.\n", instance->host->host_no);
		megaraid_sas_kill_hba(instance);
		instance->skip_heartbeat_timer_del = 1;
		retval = FAILED;
	} else {
		/* For VF: Restart HB timer if we didn't OCR */
		if (instance->requestorId) {
			megasas_start_timer(instance,
					    &instance->sriov_heartbeat_timer,
					    megasas_sriov_heartbeat_handler,
					    MEGASAS_SRIOV_HEARTBEAT_INTERVAL_VF);
		}
		clear_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
		instance->instancet->enable_intr(instance);
		atomic_set(&instance->adprecovery, MEGASAS_HBA_OPERATIONAL);
	}
out:
	clear_bit(MEGASAS_FUSION_IN_RESET, &instance->reset_flags);
	mutex_unlock(&instance->reset_mutex);
	return retval;
}

/* Fusion Crash dump collection work queue */
void  megasas_fusion_crash_dump_wq(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance, crash_init);
	u32 status_reg;
	u8 partial_copy = 0;


	status_reg = instance->instancet->read_fw_status_reg(instance->reg_set);

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
				&instance->reg_set->outbound_scratch_pad);
			readl(&instance->reg_set->outbound_scratch_pad);
			return;
		}
		megasas_alloc_host_crash_buffer(instance);
		dev_info(&instance->pdev->dev, "Number of host crash buffers "
			"allocated: %d\n", instance->drv_buf_alloc);
	}

	/*
	 * Driver has allocated max buffers, which can be allocated
	 * and FW has more crash dump data, then driver will
	 * ignore the data.
	 */
	if (instance->drv_buf_index >= (instance->drv_buf_alloc)) {
		dev_info(&instance->pdev->dev, "Driver is done copying "
			"the buffer: %d\n", instance->drv_buf_alloc);
		status_reg |= MFI_STATE_CRASH_DUMP_DONE;
		partial_copy = 1;
	} else {
		memcpy(instance->crash_buf[instance->drv_buf_index],
			instance->crash_dump_buf, CRASH_DMA_BUF_SIZE);
		instance->drv_buf_index++;
		status_reg &= ~MFI_STATE_DMADONE;
	}

	if (status_reg & MFI_STATE_CRASH_DUMP_DONE) {
		dev_info(&instance->pdev->dev, "Crash Dump is available,number "
			"of copied buffers: %d\n", instance->drv_buf_index);
		instance->fw_crash_buffer_size =  instance->drv_buf_index;
		instance->fw_crash_state = AVAILABLE;
		instance->drv_buf_index = 0;
		writel(status_reg, &instance->reg_set->outbound_scratch_pad);
		readl(&instance->reg_set->outbound_scratch_pad);
		if (!partial_copy)
			megasas_reset_fusion(instance->host, 0);
	} else {
		writel(status_reg, &instance->reg_set->outbound_scratch_pad);
		readl(&instance->reg_set->outbound_scratch_pad);
	}
}


/* Fusion OCR work queue */
void megasas_fusion_ocr_wq(struct work_struct *work)
{
	struct megasas_instance *instance =
		container_of(work, struct megasas_instance, work_init);

	megasas_reset_fusion(instance->host, 0);
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
