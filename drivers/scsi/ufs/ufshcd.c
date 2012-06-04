/*
 * Universal Flash Storage Host controller driver
 *
 * This code is based on drivers/scsi/ufs/ufshcd.c
 * Copyright (C) 2011-2012 Samsung India Software Operations
 *
 * Santosh Yaraganavi <santosh.sy@samsung.com>
 * Vinayak Holikatti <h.vinayak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/bitops.h>

#include <asm/irq.h>
#include <asm/byteorder.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>

#include "ufs.h"
#include "ufshci.h"

#define UFSHCD "ufshcd"
#define UFSHCD_DRIVER_VERSION "0.1"

enum {
	UFSHCD_MAX_CHANNEL	= 0,
	UFSHCD_MAX_ID		= 1,
	UFSHCD_MAX_LUNS		= 8,
	UFSHCD_CMD_PER_LUN	= 32,
	UFSHCD_CAN_QUEUE	= 32,
};

/* UFSHCD states */
enum {
	UFSHCD_STATE_OPERATIONAL,
	UFSHCD_STATE_RESET,
	UFSHCD_STATE_ERROR,
};

/* Interrupt configuration options */
enum {
	UFSHCD_INT_DISABLE,
	UFSHCD_INT_ENABLE,
	UFSHCD_INT_CLEAR,
};

/* Interrupt aggregation options */
enum {
	INT_AGGR_RESET,
	INT_AGGR_CONFIG,
};

/**
 * struct uic_command - UIC command structure
 * @command: UIC command
 * @argument1: UIC command argument 1
 * @argument2: UIC command argument 2
 * @argument3: UIC command argument 3
 * @cmd_active: Indicate if UIC command is outstanding
 * @result: UIC command result
 */
struct uic_command {
	u32 command;
	u32 argument1;
	u32 argument2;
	u32 argument3;
	int cmd_active;
	int result;
};

/**
 * struct ufs_hba - per adapter private structure
 * @mmio_base: UFSHCI base register address
 * @ucdl_base_addr: UFS Command Descriptor base address
 * @utrdl_base_addr: UTP Transfer Request Descriptor base address
 * @utmrdl_base_addr: UTP Task Management Descriptor base address
 * @ucdl_dma_addr: UFS Command Descriptor DMA address
 * @utrdl_dma_addr: UTRDL DMA address
 * @utmrdl_dma_addr: UTMRDL DMA address
 * @host: Scsi_Host instance of the driver
 * @pdev: PCI device handle
 * @lrb: local reference block
 * @outstanding_tasks: Bits representing outstanding task requests
 * @outstanding_reqs: Bits representing outstanding transfer requests
 * @capabilities: UFS Controller Capabilities
 * @nutrs: Transfer Request Queue depth supported by controller
 * @nutmrs: Task Management Queue depth supported by controller
 * @active_uic_cmd: handle of active UIC command
 * @ufshcd_tm_wait_queue: wait queue for task management
 * @tm_condition: condition variable for task management
 * @ufshcd_state: UFSHCD states
 * @int_enable_mask: Interrupt Mask Bits
 * @uic_workq: Work queue for UIC completion handling
 * @feh_workq: Work queue for fatal controller error handling
 * @errors: HBA errors
 */
struct ufs_hba {
	void __iomem *mmio_base;

	/* Virtual memory reference */
	struct utp_transfer_cmd_desc *ucdl_base_addr;
	struct utp_transfer_req_desc *utrdl_base_addr;
	struct utp_task_req_desc *utmrdl_base_addr;

	/* DMA memory reference */
	dma_addr_t ucdl_dma_addr;
	dma_addr_t utrdl_dma_addr;
	dma_addr_t utmrdl_dma_addr;

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	struct ufshcd_lrb *lrb;

	unsigned long outstanding_tasks;
	unsigned long outstanding_reqs;

	u32 capabilities;
	int nutrs;
	int nutmrs;
	u32 ufs_version;

	struct uic_command active_uic_cmd;
	wait_queue_head_t ufshcd_tm_wait_queue;
	unsigned long tm_condition;

	u32 ufshcd_state;
	u32 int_enable_mask;

	/* Work Queues */
	struct work_struct uic_workq;
	struct work_struct feh_workq;

	/* HBA Errors */
	u32 errors;
};

/**
 * struct ufshcd_lrb - local reference block
 * @utr_descriptor_ptr: UTRD address of the command
 * @ucd_cmd_ptr: UCD address of the command
 * @ucd_rsp_ptr: Response UPIU address for this command
 * @ucd_prdt_ptr: PRDT address of the command
 * @cmd: pointer to SCSI command
 * @sense_buffer: pointer to sense buffer address of the SCSI command
 * @sense_bufflen: Length of the sense buffer
 * @scsi_status: SCSI status of the command
 * @command_type: SCSI, UFS, Query.
 * @task_tag: Task tag of the command
 * @lun: LUN of the command
 */
struct ufshcd_lrb {
	struct utp_transfer_req_desc *utr_descriptor_ptr;
	struct utp_upiu_cmd *ucd_cmd_ptr;
	struct utp_upiu_rsp *ucd_rsp_ptr;
	struct ufshcd_sg_entry *ucd_prdt_ptr;

	struct scsi_cmnd *cmd;
	u8 *sense_buffer;
	unsigned int sense_bufflen;
	int scsi_status;

	int command_type;
	int task_tag;
	unsigned int lun;
};

/**
 * ufshcd_get_ufs_version - Get the UFS version supported by the HBA
 * @hba - Pointer to adapter instance
 *
 * Returns UFSHCI version supported by the controller
 */
static inline u32 ufshcd_get_ufs_version(struct ufs_hba *hba)
{
	return readl(hba->mmio_base + REG_UFS_VERSION);
}

/**
 * ufshcd_is_device_present - Check if any device connected to
 *			      the host controller
 * @reg_hcs - host controller status register value
 *
 * Returns 0 if device present, non-zero if no device detected
 */
static inline int ufshcd_is_device_present(u32 reg_hcs)
{
	return (DEVICE_PRESENT & reg_hcs) ? 0 : -1;
}

/**
 * ufshcd_get_tr_ocs - Get the UTRD Overall Command Status
 * @lrb: pointer to local command reference block
 *
 * This function is used to get the OCS field from UTRD
 * Returns the OCS field in the UTRD
 */
static inline int ufshcd_get_tr_ocs(struct ufshcd_lrb *lrbp)
{
	return lrbp->utr_descriptor_ptr->header.dword_2 & MASK_OCS;
}

/**
 * ufshcd_get_tmr_ocs - Get the UTMRD Overall Command Status
 * @task_req_descp: pointer to utp_task_req_desc structure
 *
 * This function is used to get the OCS field from UTMRD
 * Returns the OCS field in the UTMRD
 */
static inline int
ufshcd_get_tmr_ocs(struct utp_task_req_desc *task_req_descp)
{
	return task_req_descp->header.dword_2 & MASK_OCS;
}

/**
 * ufshcd_get_tm_free_slot - get a free slot for task management request
 * @hba: per adapter instance
 *
 * Returns maximum number of task management request slots in case of
 * task management queue full or returns the free slot number
 */
static inline int ufshcd_get_tm_free_slot(struct ufs_hba *hba)
{
	return find_first_zero_bit(&hba->outstanding_tasks, hba->nutmrs);
}

/**
 * ufshcd_utrl_clear - Clear a bit in UTRLCLR register
 * @hba: per adapter instance
 * @pos: position of the bit to be cleared
 */
static inline void ufshcd_utrl_clear(struct ufs_hba *hba, u32 pos)
{
	writel(~(1 << pos),
		(hba->mmio_base + REG_UTP_TRANSFER_REQ_LIST_CLEAR));
}

/**
 * ufshcd_get_lists_status - Check UCRDY, UTRLRDY and UTMRLRDY
 * @reg: Register value of host controller status
 *
 * Returns integer, 0 on Success and positive value if failed
 */
static inline int ufshcd_get_lists_status(u32 reg)
{
	/*
	 * The mask 0xFF is for the following HCS register bits
	 * Bit		Description
	 *  0		Device Present
	 *  1		UTRLRDY
	 *  2		UTMRLRDY
	 *  3		UCRDY
	 *  4		HEI
	 *  5		DEI
	 * 6-7		reserved
	 */
	return (((reg) & (0xFF)) >> 1) ^ (0x07);
}

/**
 * ufshcd_get_uic_cmd_result - Get the UIC command result
 * @hba: Pointer to adapter instance
 *
 * This function gets the result of UIC command completion
 * Returns 0 on success, non zero value on error
 */
static inline int ufshcd_get_uic_cmd_result(struct ufs_hba *hba)
{
	return readl(hba->mmio_base + REG_UIC_COMMAND_ARG_2) &
	       MASK_UIC_COMMAND_RESULT;
}

/**
 * ufshcd_free_hba_memory - Free allocated memory for LRB, request
 *			    and task lists
 * @hba: Pointer to adapter instance
 */
static inline void ufshcd_free_hba_memory(struct ufs_hba *hba)
{
	size_t utmrdl_size, utrdl_size, ucdl_size;

	kfree(hba->lrb);

	if (hba->utmrdl_base_addr) {
		utmrdl_size = sizeof(struct utp_task_req_desc) * hba->nutmrs;
		dma_free_coherent(&hba->pdev->dev, utmrdl_size,
				  hba->utmrdl_base_addr, hba->utmrdl_dma_addr);
	}

	if (hba->utrdl_base_addr) {
		utrdl_size =
		(sizeof(struct utp_transfer_req_desc) * hba->nutrs);
		dma_free_coherent(&hba->pdev->dev, utrdl_size,
				  hba->utrdl_base_addr, hba->utrdl_dma_addr);
	}

	if (hba->ucdl_base_addr) {
		ucdl_size =
		(sizeof(struct utp_transfer_cmd_desc) * hba->nutrs);
		dma_free_coherent(&hba->pdev->dev, ucdl_size,
				  hba->ucdl_base_addr, hba->ucdl_dma_addr);
	}
}

/**
 * ufshcd_is_valid_req_rsp - checks if controller TR response is valid
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * This function checks the response UPIU for valid transaction type in
 * response field
 * Returns 0 on success, non-zero on failure
 */
static inline int
ufshcd_is_valid_req_rsp(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return ((be32_to_cpu(ucd_rsp_ptr->header.dword_0) >> 24) ==
		 UPIU_TRANSACTION_RESPONSE) ? 0 : DID_ERROR << 16;
}

/**
 * ufshcd_get_rsp_upiu_result - Get the result from response UPIU
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * This function gets the response status and scsi_status from response UPIU
 * Returns the response result code.
 */
static inline int
ufshcd_get_rsp_upiu_result(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return be32_to_cpu(ucd_rsp_ptr->header.dword_1) & MASK_RSP_UPIU_RESULT;
}

/**
 * ufshcd_config_int_aggr - Configure interrupt aggregation values.
 *		Currently there is no use case where we want to configure
 *		interrupt aggregation dynamically. So to configure interrupt
 *		aggregation, #define INT_AGGR_COUNTER_THRESHOLD_VALUE and
 *		INT_AGGR_TIMEOUT_VALUE are used.
 * @hba: per adapter instance
 * @option: Interrupt aggregation option
 */
static inline void
ufshcd_config_int_aggr(struct ufs_hba *hba, int option)
{
	switch (option) {
	case INT_AGGR_RESET:
		writel((INT_AGGR_ENABLE |
			INT_AGGR_COUNTER_AND_TIMER_RESET),
			(hba->mmio_base +
			 REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL));
		break;
	case INT_AGGR_CONFIG:
		writel((INT_AGGR_ENABLE |
			INT_AGGR_PARAM_WRITE |
			INT_AGGR_COUNTER_THRESHOLD_VALUE |
			INT_AGGR_TIMEOUT_VALUE),
			(hba->mmio_base +
			 REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL));
		break;
	}
}

/**
 * ufshcd_enable_run_stop_reg - Enable run-stop registers,
 *			When run-stop registers are set to 1, it indicates the
 *			host controller that it can process the requests
 * @hba: per adapter instance
 */
static void ufshcd_enable_run_stop_reg(struct ufs_hba *hba)
{
	writel(UTP_TASK_REQ_LIST_RUN_STOP_BIT,
	       (hba->mmio_base +
		REG_UTP_TASK_REQ_LIST_RUN_STOP));
	writel(UTP_TRANSFER_REQ_LIST_RUN_STOP_BIT,
	       (hba->mmio_base +
		REG_UTP_TRANSFER_REQ_LIST_RUN_STOP));
}

/**
 * ufshcd_hba_stop - Send controller to reset state
 * @hba: per adapter instance
 */
static inline void ufshcd_hba_stop(struct ufs_hba *hba)
{
	writel(CONTROLLER_DISABLE, (hba->mmio_base + REG_CONTROLLER_ENABLE));
}

/**
 * ufshcd_hba_start - Start controller initialization sequence
 * @hba: per adapter instance
 */
static inline void ufshcd_hba_start(struct ufs_hba *hba)
{
	writel(CONTROLLER_ENABLE , (hba->mmio_base + REG_CONTROLLER_ENABLE));
}

/**
 * ufshcd_is_hba_active - Get controller state
 * @hba: per adapter instance
 *
 * Returns zero if controller is active, 1 otherwise
 */
static inline int ufshcd_is_hba_active(struct ufs_hba *hba)
{
	return (readl(hba->mmio_base + REG_CONTROLLER_ENABLE) & 0x1) ? 0 : 1;
}

/**
 * ufshcd_send_command - Send SCSI or device management commands
 * @hba: per adapter instance
 * @task_tag: Task tag of the command
 */
static inline
void ufshcd_send_command(struct ufs_hba *hba, unsigned int task_tag)
{
	__set_bit(task_tag, &hba->outstanding_reqs);
	writel((1 << task_tag),
	       (hba->mmio_base + REG_UTP_TRANSFER_REQ_DOOR_BELL));
}

/**
 * ufshcd_copy_sense_data - Copy sense data in case of check condition
 * @lrb - pointer to local reference block
 */
static inline void ufshcd_copy_sense_data(struct ufshcd_lrb *lrbp)
{
	int len;
	if (lrbp->sense_buffer) {
		len = be16_to_cpu(lrbp->ucd_rsp_ptr->sense_data_len);
		memcpy(lrbp->sense_buffer,
			lrbp->ucd_rsp_ptr->sense_data,
			min_t(int, len, SCSI_SENSE_BUFFERSIZE));
	}
}

/**
 * ufshcd_hba_capabilities - Read controller capabilities
 * @hba: per adapter instance
 */
static inline void ufshcd_hba_capabilities(struct ufs_hba *hba)
{
	hba->capabilities =
		readl(hba->mmio_base + REG_CONTROLLER_CAPABILITIES);

	/* nutrs and nutmrs are 0 based values */
	hba->nutrs = (hba->capabilities & MASK_TRANSFER_REQUESTS_SLOTS) + 1;
	hba->nutmrs =
	((hba->capabilities & MASK_TASK_MANAGEMENT_REQUEST_SLOTS) >> 16) + 1;
}

/**
 * ufshcd_send_uic_command - Send UIC commands to unipro layers
 * @hba: per adapter instance
 * @uic_command: UIC command
 */
static inline void
ufshcd_send_uic_command(struct ufs_hba *hba, struct uic_command *uic_cmnd)
{
	/* Write Args */
	writel(uic_cmnd->argument1,
	      (hba->mmio_base + REG_UIC_COMMAND_ARG_1));
	writel(uic_cmnd->argument2,
	      (hba->mmio_base + REG_UIC_COMMAND_ARG_2));
	writel(uic_cmnd->argument3,
	      (hba->mmio_base + REG_UIC_COMMAND_ARG_3));

	/* Write UIC Cmd */
	writel((uic_cmnd->command & COMMAND_OPCODE_MASK),
	       (hba->mmio_base + REG_UIC_COMMAND));
}

/**
 * ufshcd_map_sg - Map scatter-gather list to prdt
 * @lrbp - pointer to local reference block
 *
 * Returns 0 in case of success, non-zero value in case of failure
 */
static int ufshcd_map_sg(struct ufshcd_lrb *lrbp)
{
	struct ufshcd_sg_entry *prd_table;
	struct scatterlist *sg;
	struct scsi_cmnd *cmd;
	int sg_segments;
	int i;

	cmd = lrbp->cmd;
	sg_segments = scsi_dma_map(cmd);
	if (sg_segments < 0)
		return sg_segments;

	if (sg_segments) {
		lrbp->utr_descriptor_ptr->prd_table_length =
					cpu_to_le16((u16) (sg_segments));

		prd_table = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;

		scsi_for_each_sg(cmd, sg, sg_segments, i) {
			prd_table[i].size  =
				cpu_to_le32(((u32) sg_dma_len(sg))-1);
			prd_table[i].base_addr =
				cpu_to_le32(lower_32_bits(sg->dma_address));
			prd_table[i].upper_addr =
				cpu_to_le32(upper_32_bits(sg->dma_address));
		}
	} else {
		lrbp->utr_descriptor_ptr->prd_table_length = 0;
	}

	return 0;
}

/**
 * ufshcd_int_config - enable/disable interrupts
 * @hba: per adapter instance
 * @option: interrupt option
 */
static void ufshcd_int_config(struct ufs_hba *hba, u32 option)
{
	switch (option) {
	case UFSHCD_INT_ENABLE:
		writel(hba->int_enable_mask,
		      (hba->mmio_base + REG_INTERRUPT_ENABLE));
		break;
	case UFSHCD_INT_DISABLE:
		if (hba->ufs_version == UFSHCI_VERSION_10)
			writel(INTERRUPT_DISABLE_MASK_10,
			      (hba->mmio_base + REG_INTERRUPT_ENABLE));
		else
			writel(INTERRUPT_DISABLE_MASK_11,
			       (hba->mmio_base + REG_INTERRUPT_ENABLE));
		break;
	}
}

/**
 * ufshcd_compose_upiu - form UFS Protocol Information Unit(UPIU)
 * @lrb - pointer to local reference block
 */
static void ufshcd_compose_upiu(struct ufshcd_lrb *lrbp)
{
	struct utp_transfer_req_desc *req_desc;
	struct utp_upiu_cmd *ucd_cmd_ptr;
	u32 data_direction;
	u32 upiu_flags;

	ucd_cmd_ptr = lrbp->ucd_cmd_ptr;
	req_desc = lrbp->utr_descriptor_ptr;

	switch (lrbp->command_type) {
	case UTP_CMD_TYPE_SCSI:
		if (lrbp->cmd->sc_data_direction == DMA_FROM_DEVICE) {
			data_direction = UTP_DEVICE_TO_HOST;
			upiu_flags = UPIU_CMD_FLAGS_READ;
		} else if (lrbp->cmd->sc_data_direction == DMA_TO_DEVICE) {
			data_direction = UTP_HOST_TO_DEVICE;
			upiu_flags = UPIU_CMD_FLAGS_WRITE;
		} else {
			data_direction = UTP_NO_DATA_TRANSFER;
			upiu_flags = UPIU_CMD_FLAGS_NONE;
		}

		/* Transfer request descriptor header fields */
		req_desc->header.dword_0 =
			cpu_to_le32(data_direction | UTP_SCSI_COMMAND);

		/*
		 * assigning invalid value for command status. Controller
		 * updates OCS on command completion, with the command
		 * status
		 */
		req_desc->header.dword_2 =
			cpu_to_le32(OCS_INVALID_COMMAND_STATUS);

		/* command descriptor fields */
		ucd_cmd_ptr->header.dword_0 =
			cpu_to_be32(UPIU_HEADER_DWORD(UPIU_TRANSACTION_COMMAND,
						      upiu_flags,
						      lrbp->lun,
						      lrbp->task_tag));
		ucd_cmd_ptr->header.dword_1 =
			cpu_to_be32(
				UPIU_HEADER_DWORD(UPIU_COMMAND_SET_TYPE_SCSI,
						  0,
						  0,
						  0));

		/* Total EHS length and Data segment length will be zero */
		ucd_cmd_ptr->header.dword_2 = 0;

		ucd_cmd_ptr->exp_data_transfer_len =
			cpu_to_be32(lrbp->cmd->transfersize);

		memcpy(ucd_cmd_ptr->cdb,
		       lrbp->cmd->cmnd,
		       (min_t(unsigned short,
			      lrbp->cmd->cmd_len,
			      MAX_CDB_SIZE)));
		break;
	case UTP_CMD_TYPE_DEV_MANAGE:
		/* For query function implementation */
		break;
	case UTP_CMD_TYPE_UFS:
		/* For UFS native command implementation */
		break;
	} /* end of switch */
}

/**
 * ufshcd_queuecommand - main entry point for SCSI requests
 * @cmd: command from SCSI Midlayer
 * @done: call back function
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	struct ufshcd_lrb *lrbp;
	struct ufs_hba *hba;
	unsigned long flags;
	int tag;
	int err = 0;

	hba = shost_priv(host);

	tag = cmd->request->tag;

	if (hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL) {
		err = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	lrbp = &hba->lrb[tag];

	lrbp->cmd = cmd;
	lrbp->sense_bufflen = SCSI_SENSE_BUFFERSIZE;
	lrbp->sense_buffer = cmd->sense_buffer;
	lrbp->task_tag = tag;
	lrbp->lun = cmd->device->lun;

	lrbp->command_type = UTP_CMD_TYPE_SCSI;

	/* form UPIU before issuing the command */
	ufshcd_compose_upiu(lrbp);
	err = ufshcd_map_sg(lrbp);
	if (err)
		goto out;

	/* issue command to the controller */
	spin_lock_irqsave(hba->host->host_lock, flags);
	ufshcd_send_command(hba, tag);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
out:
	return err;
}

/**
 * ufshcd_memory_alloc - allocate memory for host memory space data structures
 * @hba: per adapter instance
 *
 * 1. Allocate DMA memory for Command Descriptor array
 *	Each command descriptor consist of Command UPIU, Response UPIU and PRDT
 * 2. Allocate DMA memory for UTP Transfer Request Descriptor List (UTRDL).
 * 3. Allocate DMA memory for UTP Task Management Request Descriptor List
 *	(UTMRDL)
 * 4. Allocate memory for local reference block(lrb).
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_memory_alloc(struct ufs_hba *hba)
{
	size_t utmrdl_size, utrdl_size, ucdl_size;

	/* Allocate memory for UTP command descriptors */
	ucdl_size = (sizeof(struct utp_transfer_cmd_desc) * hba->nutrs);
	hba->ucdl_base_addr = dma_alloc_coherent(&hba->pdev->dev,
						 ucdl_size,
						 &hba->ucdl_dma_addr,
						 GFP_KERNEL);

	/*
	 * UFSHCI requires UTP command descriptor to be 128 byte aligned.
	 * make sure hba->ucdl_dma_addr is aligned to PAGE_SIZE
	 * if hba->ucdl_dma_addr is aligned to PAGE_SIZE, then it will
	 * be aligned to 128 bytes as well
	 */
	if (!hba->ucdl_base_addr ||
	    WARN_ON(hba->ucdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(&hba->pdev->dev,
			"Command Descriptor Memory allocation failed\n");
		goto out;
	}

	/*
	 * Allocate memory for UTP Transfer descriptors
	 * UFSHCI requires 1024 byte alignment of UTRD
	 */
	utrdl_size = (sizeof(struct utp_transfer_req_desc) * hba->nutrs);
	hba->utrdl_base_addr = dma_alloc_coherent(&hba->pdev->dev,
						  utrdl_size,
						  &hba->utrdl_dma_addr,
						  GFP_KERNEL);
	if (!hba->utrdl_base_addr ||
	    WARN_ON(hba->utrdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(&hba->pdev->dev,
			"Transfer Descriptor Memory allocation failed\n");
		goto out;
	}

	/*
	 * Allocate memory for UTP Task Management descriptors
	 * UFSHCI requires 1024 byte alignment of UTMRD
	 */
	utmrdl_size = sizeof(struct utp_task_req_desc) * hba->nutmrs;
	hba->utmrdl_base_addr = dma_alloc_coherent(&hba->pdev->dev,
						   utmrdl_size,
						   &hba->utmrdl_dma_addr,
						   GFP_KERNEL);
	if (!hba->utmrdl_base_addr ||
	    WARN_ON(hba->utmrdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(&hba->pdev->dev,
		"Task Management Descriptor Memory allocation failed\n");
		goto out;
	}

	/* Allocate memory for local reference block */
	hba->lrb = kcalloc(hba->nutrs, sizeof(struct ufshcd_lrb), GFP_KERNEL);
	if (!hba->lrb) {
		dev_err(&hba->pdev->dev, "LRB Memory allocation failed\n");
		goto out;
	}
	return 0;
out:
	ufshcd_free_hba_memory(hba);
	return -ENOMEM;
}

/**
 * ufshcd_host_memory_configure - configure local reference block with
 *				memory offsets
 * @hba: per adapter instance
 *
 * Configure Host memory space
 * 1. Update Corresponding UTRD.UCDBA and UTRD.UCDBAU with UCD DMA
 * address.
 * 2. Update each UTRD with Response UPIU offset, Response UPIU length
 * and PRDT offset.
 * 3. Save the corresponding addresses of UTRD, UCD.CMD, UCD.RSP and UCD.PRDT
 * into local reference block.
 */
static void ufshcd_host_memory_configure(struct ufs_hba *hba)
{
	struct utp_transfer_cmd_desc *cmd_descp;
	struct utp_transfer_req_desc *utrdlp;
	dma_addr_t cmd_desc_dma_addr;
	dma_addr_t cmd_desc_element_addr;
	u16 response_offset;
	u16 prdt_offset;
	int cmd_desc_size;
	int i;

	utrdlp = hba->utrdl_base_addr;
	cmd_descp = hba->ucdl_base_addr;

	response_offset =
		offsetof(struct utp_transfer_cmd_desc, response_upiu);
	prdt_offset =
		offsetof(struct utp_transfer_cmd_desc, prd_table);

	cmd_desc_size = sizeof(struct utp_transfer_cmd_desc);
	cmd_desc_dma_addr = hba->ucdl_dma_addr;

	for (i = 0; i < hba->nutrs; i++) {
		/* Configure UTRD with command descriptor base address */
		cmd_desc_element_addr =
				(cmd_desc_dma_addr + (cmd_desc_size * i));
		utrdlp[i].command_desc_base_addr_lo =
				cpu_to_le32(lower_32_bits(cmd_desc_element_addr));
		utrdlp[i].command_desc_base_addr_hi =
				cpu_to_le32(upper_32_bits(cmd_desc_element_addr));

		/* Response upiu and prdt offset should be in double words */
		utrdlp[i].response_upiu_offset =
				cpu_to_le16((response_offset >> 2));
		utrdlp[i].prd_table_offset =
				cpu_to_le16((prdt_offset >> 2));
		utrdlp[i].response_upiu_length =
				cpu_to_le16(ALIGNED_UPIU_SIZE);

		hba->lrb[i].utr_descriptor_ptr = (utrdlp + i);
		hba->lrb[i].ucd_cmd_ptr =
			(struct utp_upiu_cmd *)(cmd_descp + i);
		hba->lrb[i].ucd_rsp_ptr =
			(struct utp_upiu_rsp *)cmd_descp[i].response_upiu;
		hba->lrb[i].ucd_prdt_ptr =
			(struct ufshcd_sg_entry *)cmd_descp[i].prd_table;
	}
}

/**
 * ufshcd_dme_link_startup - Notify Unipro to perform link startup
 * @hba: per adapter instance
 *
 * UIC_CMD_DME_LINK_STARTUP command must be issued to Unipro layer,
 * in order to initialize the Unipro link startup procedure.
 * Once the Unipro links are up, the device connected to the controller
 * is detected.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_dme_link_startup(struct ufs_hba *hba)
{
	struct uic_command *uic_cmd;
	unsigned long flags;

	/* check if controller is ready to accept UIC commands */
	if (((readl(hba->mmio_base + REG_CONTROLLER_STATUS)) &
	    UIC_COMMAND_READY) == 0x0) {
		dev_err(&hba->pdev->dev,
			"Controller not ready"
			" to accept UIC commands\n");
		return -EIO;
	}

	spin_lock_irqsave(hba->host->host_lock, flags);

	/* form UIC command */
	uic_cmd = &hba->active_uic_cmd;
	uic_cmd->command = UIC_CMD_DME_LINK_STARTUP;
	uic_cmd->argument1 = 0;
	uic_cmd->argument2 = 0;
	uic_cmd->argument3 = 0;

	/* enable UIC related interrupts */
	hba->int_enable_mask |= UIC_COMMAND_COMPL;
	ufshcd_int_config(hba, UFSHCD_INT_ENABLE);

	/* sending UIC commands to controller */
	ufshcd_send_uic_command(hba, uic_cmd);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return 0;
}

/**
 * ufshcd_make_hba_operational - Make UFS controller operational
 * @hba: per adapter instance
 *
 * To bring UFS host controller to operational state,
 * 1. Check if device is present
 * 2. Configure run-stop-registers
 * 3. Enable required interrupts
 * 4. Configure interrupt aggregation
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_make_hba_operational(struct ufs_hba *hba)
{
	int err = 0;
	u32 reg;

	/* check if device present */
	reg = readl((hba->mmio_base + REG_CONTROLLER_STATUS));
	if (ufshcd_is_device_present(reg)) {
		dev_err(&hba->pdev->dev, "cc: Device not present\n");
		err = -ENXIO;
		goto out;
	}

	/*
	 * UCRDY, UTMRLDY and UTRLRDY bits must be 1
	 * DEI, HEI bits must be 0
	 */
	if (!(ufshcd_get_lists_status(reg))) {
		ufshcd_enable_run_stop_reg(hba);
	} else {
		dev_err(&hba->pdev->dev,
			"Host controller not ready to process requests");
		err = -EIO;
		goto out;
	}

	/* Enable required interrupts */
	hba->int_enable_mask |= (UTP_TRANSFER_REQ_COMPL |
				 UIC_ERROR |
				 UTP_TASK_REQ_COMPL |
				 DEVICE_FATAL_ERROR |
				 CONTROLLER_FATAL_ERROR |
				 SYSTEM_BUS_FATAL_ERROR);
	ufshcd_int_config(hba, UFSHCD_INT_ENABLE);

	/* Configure interrupt aggregation */
	ufshcd_config_int_aggr(hba, INT_AGGR_CONFIG);

	if (hba->ufshcd_state == UFSHCD_STATE_RESET)
		scsi_unblock_requests(hba->host);

	hba->ufshcd_state = UFSHCD_STATE_OPERATIONAL;
	scsi_scan_host(hba->host);
out:
	return err;
}

/**
 * ufshcd_hba_enable - initialize the controller
 * @hba: per adapter instance
 *
 * The controller resets itself and controller firmware initialization
 * sequence kicks off. When controller is ready it will set
 * the Host Controller Enable bit to 1.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_hba_enable(struct ufs_hba *hba)
{
	int retry;

	/*
	 * msleep of 1 and 5 used in this function might result in msleep(20),
	 * but it was necessary to send the UFS FPGA to reset mode during
	 * development and testing of this driver. msleep can be changed to
	 * mdelay and retry count can be reduced based on the controller.
	 */
	if (!ufshcd_is_hba_active(hba)) {

		/* change controller state to "reset state" */
		ufshcd_hba_stop(hba);

		/*
		 * This delay is based on the testing done with UFS host
		 * controller FPGA. The delay can be changed based on the
		 * host controller used.
		 */
		msleep(5);
	}

	/* start controller initialization sequence */
	ufshcd_hba_start(hba);

	/*
	 * To initialize a UFS host controller HCE bit must be set to 1.
	 * During initialization the HCE bit value changes from 1->0->1.
	 * When the host controller completes initialization sequence
	 * it sets the value of HCE bit to 1. The same HCE bit is read back
	 * to check if the controller has completed initialization sequence.
	 * So without this delay the value HCE = 1, set in the previous
	 * instruction might be read back.
	 * This delay can be changed based on the controller.
	 */
	msleep(1);

	/* wait for the host controller to complete initialization */
	retry = 10;
	while (ufshcd_is_hba_active(hba)) {
		if (retry) {
			retry--;
		} else {
			dev_err(&hba->pdev->dev,
				"Controller enable failed\n");
			return -EIO;
		}
		msleep(5);
	}
	return 0;
}

/**
 * ufshcd_initialize_hba - start the initialization process
 * @hba: per adapter instance
 *
 * 1. Enable the controller via ufshcd_hba_enable.
 * 2. Program the Transfer Request List Address with the starting address of
 * UTRDL.
 * 3. Program the Task Management Request List Address with starting address
 * of UTMRDL.
 *
 * Returns 0 on success, non-zero value on failure.
 */
static int ufshcd_initialize_hba(struct ufs_hba *hba)
{
	if (ufshcd_hba_enable(hba))
		return -EIO;

	/* Configure UTRL and UTMRL base address registers */
	writel(lower_32_bits(hba->utrdl_dma_addr),
	       (hba->mmio_base + REG_UTP_TRANSFER_REQ_LIST_BASE_L));
	writel(upper_32_bits(hba->utrdl_dma_addr),
	       (hba->mmio_base + REG_UTP_TRANSFER_REQ_LIST_BASE_H));
	writel(lower_32_bits(hba->utmrdl_dma_addr),
	       (hba->mmio_base + REG_UTP_TASK_REQ_LIST_BASE_L));
	writel(upper_32_bits(hba->utmrdl_dma_addr),
	       (hba->mmio_base + REG_UTP_TASK_REQ_LIST_BASE_H));

	/* Initialize unipro link startup procedure */
	return ufshcd_dme_link_startup(hba);
}

/**
 * ufshcd_do_reset - reset the host controller
 * @hba: per adapter instance
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_do_reset(struct ufs_hba *hba)
{
	struct ufshcd_lrb *lrbp;
	unsigned long flags;
	int tag;

	/* block commands from midlayer */
	scsi_block_requests(hba->host);

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->ufshcd_state = UFSHCD_STATE_RESET;

	/* send controller to reset state */
	ufshcd_hba_stop(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/* abort outstanding commands */
	for (tag = 0; tag < hba->nutrs; tag++) {
		if (test_bit(tag, &hba->outstanding_reqs)) {
			lrbp = &hba->lrb[tag];
			scsi_dma_unmap(lrbp->cmd);
			lrbp->cmd->result = DID_RESET << 16;
			lrbp->cmd->scsi_done(lrbp->cmd);
			lrbp->cmd = NULL;
		}
	}

	/* clear outstanding request/task bit maps */
	hba->outstanding_reqs = 0;
	hba->outstanding_tasks = 0;

	/* start the initialization process */
	if (ufshcd_initialize_hba(hba)) {
		dev_err(&hba->pdev->dev,
			"Reset: Controller initialization failed\n");
		return FAILED;
	}
	return SUCCESS;
}

/**
 * ufshcd_slave_alloc - handle initial SCSI device configurations
 * @sdev: pointer to SCSI device
 *
 * Returns success
 */
static int ufshcd_slave_alloc(struct scsi_device *sdev)
{
	struct ufs_hba *hba;

	hba = shost_priv(sdev->host);
	sdev->tagged_supported = 1;

	/* Mode sense(6) is not supported by UFS, so use Mode sense(10) */
	sdev->use_10_for_ms = 1;
	scsi_set_tag_type(sdev, MSG_SIMPLE_TAG);

	/*
	 * Inform SCSI Midlayer that the LUN queue depth is same as the
	 * controller queue depth. If a LUN queue depth is less than the
	 * controller queue depth and if the LUN reports
	 * SAM_STAT_TASK_SET_FULL, the LUN queue depth will be adjusted
	 * with scsi_adjust_queue_depth.
	 */
	scsi_activate_tcq(sdev, hba->nutrs);
	return 0;
}

/**
 * ufshcd_slave_destroy - remove SCSI device configurations
 * @sdev: pointer to SCSI device
 */
static void ufshcd_slave_destroy(struct scsi_device *sdev)
{
	struct ufs_hba *hba;

	hba = shost_priv(sdev->host);
	scsi_deactivate_tcq(sdev, hba->nutrs);
}

/**
 * ufshcd_task_req_compl - handle task management request completion
 * @hba: per adapter instance
 * @index: index of the completed request
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_task_req_compl(struct ufs_hba *hba, u32 index)
{
	struct utp_task_req_desc *task_req_descp;
	struct utp_upiu_task_rsp *task_rsp_upiup;
	unsigned long flags;
	int ocs_value;
	int task_result;

	spin_lock_irqsave(hba->host->host_lock, flags);

	/* Clear completed tasks from outstanding_tasks */
	__clear_bit(index, &hba->outstanding_tasks);

	task_req_descp = hba->utmrdl_base_addr;
	ocs_value = ufshcd_get_tmr_ocs(&task_req_descp[index]);

	if (ocs_value == OCS_SUCCESS) {
		task_rsp_upiup = (struct utp_upiu_task_rsp *)
				task_req_descp[index].task_rsp_upiu;
		task_result = be32_to_cpu(task_rsp_upiup->header.dword_1);
		task_result = ((task_result & MASK_TASK_RESPONSE) >> 8);

		if (task_result != UPIU_TASK_MANAGEMENT_FUNC_COMPL &&
		    task_result != UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED)
			task_result = FAILED;
	} else {
		task_result = FAILED;
		dev_err(&hba->pdev->dev,
			"trc: Invalid ocs = %x\n", ocs_value);
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return task_result;
}

/**
 * ufshcd_adjust_lun_qdepth - Update LUN queue depth if device responds with
 *			      SAM_STAT_TASK_SET_FULL SCSI command status.
 * @cmd: pointer to SCSI command
 */
static void ufshcd_adjust_lun_qdepth(struct scsi_cmnd *cmd)
{
	struct ufs_hba *hba;
	int i;
	int lun_qdepth = 0;

	hba = shost_priv(cmd->device->host);

	/*
	 * LUN queue depth can be obtained by counting outstanding commands
	 * on the LUN.
	 */
	for (i = 0; i < hba->nutrs; i++) {
		if (test_bit(i, &hba->outstanding_reqs)) {

			/*
			 * Check if the outstanding command belongs
			 * to the LUN which reported SAM_STAT_TASK_SET_FULL.
			 */
			if (cmd->device->lun == hba->lrb[i].lun)
				lun_qdepth++;
		}
	}

	/*
	 * LUN queue depth will be total outstanding commands, except the
	 * command for which the LUN reported SAM_STAT_TASK_SET_FULL.
	 */
	scsi_adjust_queue_depth(cmd->device, MSG_SIMPLE_TAG, lun_qdepth - 1);
}

/**
 * ufshcd_scsi_cmd_status - Update SCSI command result based on SCSI status
 * @lrb: pointer to local reference block of completed command
 * @scsi_status: SCSI command status
 *
 * Returns value base on SCSI command status
 */
static inline int
ufshcd_scsi_cmd_status(struct ufshcd_lrb *lrbp, int scsi_status)
{
	int result = 0;

	switch (scsi_status) {
	case SAM_STAT_GOOD:
		result |= DID_OK << 16 |
			  COMMAND_COMPLETE << 8 |
			  SAM_STAT_GOOD;
		break;
	case SAM_STAT_CHECK_CONDITION:
		result |= DID_OK << 16 |
			  COMMAND_COMPLETE << 8 |
			  SAM_STAT_CHECK_CONDITION;
		ufshcd_copy_sense_data(lrbp);
		break;
	case SAM_STAT_BUSY:
		result |= SAM_STAT_BUSY;
		break;
	case SAM_STAT_TASK_SET_FULL:

		/*
		 * If a LUN reports SAM_STAT_TASK_SET_FULL, then the LUN queue
		 * depth needs to be adjusted to the exact number of
		 * outstanding commands the LUN can handle at any given time.
		 */
		ufshcd_adjust_lun_qdepth(lrbp->cmd);
		result |= SAM_STAT_TASK_SET_FULL;
		break;
	case SAM_STAT_TASK_ABORTED:
		result |= SAM_STAT_TASK_ABORTED;
		break;
	default:
		result |= DID_ERROR << 16;
		break;
	} /* end of switch */

	return result;
}

/**
 * ufshcd_transfer_rsp_status - Get overall status of the response
 * @hba: per adapter instance
 * @lrb: pointer to local reference block of completed command
 *
 * Returns result of the command to notify SCSI midlayer
 */
static inline int
ufshcd_transfer_rsp_status(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	int result = 0;
	int scsi_status;
	int ocs;

	/* overall command status of utrd */
	ocs = ufshcd_get_tr_ocs(lrbp);

	switch (ocs) {
	case OCS_SUCCESS:

		/* check if the returned transfer response is valid */
		result = ufshcd_is_valid_req_rsp(lrbp->ucd_rsp_ptr);
		if (result) {
			dev_err(&hba->pdev->dev,
				"Invalid response = %x\n", result);
			break;
		}

		/*
		 * get the response UPIU result to extract
		 * the SCSI command status
		 */
		result = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr);

		/*
		 * get the result based on SCSI status response
		 * to notify the SCSI midlayer of the command status
		 */
		scsi_status = result & MASK_SCSI_STATUS;
		result = ufshcd_scsi_cmd_status(lrbp, scsi_status);
		break;
	case OCS_ABORTED:
		result |= DID_ABORT << 16;
		break;
	case OCS_INVALID_CMD_TABLE_ATTR:
	case OCS_INVALID_PRDT_ATTR:
	case OCS_MISMATCH_DATA_BUF_SIZE:
	case OCS_MISMATCH_RESP_UPIU_SIZE:
	case OCS_PEER_COMM_FAILURE:
	case OCS_FATAL_ERROR:
	default:
		result |= DID_ERROR << 16;
		dev_err(&hba->pdev->dev,
		"OCS error from controller = %x\n", ocs);
		break;
	} /* end of switch */

	return result;
}

/**
 * ufshcd_transfer_req_compl - handle SCSI and query command completion
 * @hba: per adapter instance
 */
static void ufshcd_transfer_req_compl(struct ufs_hba *hba)
{
	struct ufshcd_lrb *lrb;
	unsigned long completed_reqs;
	u32 tr_doorbell;
	int result;
	int index;

	lrb = hba->lrb;
	tr_doorbell =
		readl(hba->mmio_base + REG_UTP_TRANSFER_REQ_DOOR_BELL);
	completed_reqs = tr_doorbell ^ hba->outstanding_reqs;

	for (index = 0; index < hba->nutrs; index++) {
		if (test_bit(index, &completed_reqs)) {

			result = ufshcd_transfer_rsp_status(hba, &lrb[index]);

			if (lrb[index].cmd) {
				scsi_dma_unmap(lrb[index].cmd);
				lrb[index].cmd->result = result;
				lrb[index].cmd->scsi_done(lrb[index].cmd);

				/* Mark completed command as NULL in LRB */
				lrb[index].cmd = NULL;
			}
		} /* end of if */
	} /* end of for */

	/* clear corresponding bits of completed commands */
	hba->outstanding_reqs ^= completed_reqs;

	/* Reset interrupt aggregation counters */
	ufshcd_config_int_aggr(hba, INT_AGGR_RESET);
}

/**
 * ufshcd_uic_cc_handler - handle UIC command completion
 * @work: pointer to a work queue structure
 *
 * Returns 0 on success, non-zero value on failure
 */
static void ufshcd_uic_cc_handler (struct work_struct *work)
{
	struct ufs_hba *hba;

	hba = container_of(work, struct ufs_hba, uic_workq);

	if ((hba->active_uic_cmd.command == UIC_CMD_DME_LINK_STARTUP) &&
	    !(ufshcd_get_uic_cmd_result(hba))) {

		if (ufshcd_make_hba_operational(hba))
			dev_err(&hba->pdev->dev,
				"cc: hba not operational state\n");
		return;
	}
}

/**
 * ufshcd_fatal_err_handler - handle fatal errors
 * @hba: per adapter instance
 */
static void ufshcd_fatal_err_handler(struct work_struct *work)
{
	struct ufs_hba *hba;
	hba = container_of(work, struct ufs_hba, feh_workq);

	/* check if reset is already in progress */
	if (hba->ufshcd_state != UFSHCD_STATE_RESET)
		ufshcd_do_reset(hba);
}

/**
 * ufshcd_err_handler - Check for fatal errors
 * @work: pointer to a work queue structure
 */
static void ufshcd_err_handler(struct ufs_hba *hba)
{
	u32 reg;

	if (hba->errors & INT_FATAL_ERRORS)
		goto fatal_eh;

	if (hba->errors & UIC_ERROR) {

		reg = readl(hba->mmio_base +
			    REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
		if (reg & UIC_DATA_LINK_LAYER_ERROR_PA_INIT)
			goto fatal_eh;
	}
	return;
fatal_eh:
	hba->ufshcd_state = UFSHCD_STATE_ERROR;
	schedule_work(&hba->feh_workq);
}

/**
 * ufshcd_tmc_handler - handle task management function completion
 * @hba: per adapter instance
 */
static void ufshcd_tmc_handler(struct ufs_hba *hba)
{
	u32 tm_doorbell;

	tm_doorbell = readl(hba->mmio_base + REG_UTP_TASK_REQ_DOOR_BELL);
	hba->tm_condition = tm_doorbell ^ hba->outstanding_tasks;
	wake_up_interruptible(&hba->ufshcd_tm_wait_queue);
}

/**
 * ufshcd_sl_intr - Interrupt service routine
 * @hba: per adapter instance
 * @intr_status: contains interrupts generated by the controller
 */
static void ufshcd_sl_intr(struct ufs_hba *hba, u32 intr_status)
{
	hba->errors = UFSHCD_ERROR_MASK & intr_status;
	if (hba->errors)
		ufshcd_err_handler(hba);

	if (intr_status & UIC_COMMAND_COMPL)
		schedule_work(&hba->uic_workq);

	if (intr_status & UTP_TASK_REQ_COMPL)
		ufshcd_tmc_handler(hba);

	if (intr_status & UTP_TRANSFER_REQ_COMPL)
		ufshcd_transfer_req_compl(hba);
}

/**
 * ufshcd_intr - Main interrupt service routine
 * @irq: irq number
 * @__hba: pointer to adapter instance
 *
 * Returns IRQ_HANDLED - If interrupt is valid
 *		IRQ_NONE - If invalid interrupt
 */
static irqreturn_t ufshcd_intr(int irq, void *__hba)
{
	u32 intr_status;
	irqreturn_t retval = IRQ_NONE;
	struct ufs_hba *hba = __hba;

	spin_lock(hba->host->host_lock);
	intr_status = readl(hba->mmio_base + REG_INTERRUPT_STATUS);

	if (intr_status) {
		ufshcd_sl_intr(hba, intr_status);

		/* If UFSHCI 1.0 then clear interrupt status register */
		if (hba->ufs_version == UFSHCI_VERSION_10)
			writel(intr_status,
			       (hba->mmio_base + REG_INTERRUPT_STATUS));
		retval = IRQ_HANDLED;
	}
	spin_unlock(hba->host->host_lock);
	return retval;
}

/**
 * ufshcd_issue_tm_cmd - issues task management commands to controller
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 *
 * Returns SUCCESS/FAILED
 */
static int
ufshcd_issue_tm_cmd(struct ufs_hba *hba,
		    struct ufshcd_lrb *lrbp,
		    u8 tm_function)
{
	struct utp_task_req_desc *task_req_descp;
	struct utp_upiu_task_req *task_req_upiup;
	struct Scsi_Host *host;
	unsigned long flags;
	int free_slot = 0;
	int err;

	host = hba->host;

	spin_lock_irqsave(host->host_lock, flags);

	/* If task management queue is full */
	free_slot = ufshcd_get_tm_free_slot(hba);
	if (free_slot >= hba->nutmrs) {
		spin_unlock_irqrestore(host->host_lock, flags);
		dev_err(&hba->pdev->dev, "Task management queue full\n");
		err = FAILED;
		goto out;
	}

	task_req_descp = hba->utmrdl_base_addr;
	task_req_descp += free_slot;

	/* Configure task request descriptor */
	task_req_descp->header.dword_0 = cpu_to_le32(UTP_REQ_DESC_INT_CMD);
	task_req_descp->header.dword_2 =
			cpu_to_le32(OCS_INVALID_COMMAND_STATUS);

	/* Configure task request UPIU */
	task_req_upiup =
		(struct utp_upiu_task_req *) task_req_descp->task_req_upiu;
	task_req_upiup->header.dword_0 =
		cpu_to_be32(UPIU_HEADER_DWORD(UPIU_TRANSACTION_TASK_REQ, 0,
					      lrbp->lun, lrbp->task_tag));
	task_req_upiup->header.dword_1 =
	cpu_to_be32(UPIU_HEADER_DWORD(0, tm_function, 0, 0));

	task_req_upiup->input_param1 = lrbp->lun;
	task_req_upiup->input_param1 =
		cpu_to_be32(task_req_upiup->input_param1);
	task_req_upiup->input_param2 = lrbp->task_tag;
	task_req_upiup->input_param2 =
		cpu_to_be32(task_req_upiup->input_param2);

	/* send command to the controller */
	__set_bit(free_slot, &hba->outstanding_tasks);
	writel((1 << free_slot),
	       (hba->mmio_base + REG_UTP_TASK_REQ_DOOR_BELL));

	spin_unlock_irqrestore(host->host_lock, flags);

	/* wait until the task management command is completed */
	err =
	wait_event_interruptible_timeout(hba->ufshcd_tm_wait_queue,
					 (test_bit(free_slot,
					 &hba->tm_condition) != 0),
					 60 * HZ);
	if (!err) {
		dev_err(&hba->pdev->dev,
			"Task management command timed-out\n");
		err = FAILED;
		goto out;
	}
	clear_bit(free_slot, &hba->tm_condition);
	return ufshcd_task_req_compl(hba, free_slot);
out:
	return err;
}

/**
 * ufshcd_device_reset - reset device and abort all the pending commands
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_device_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host;
	struct ufs_hba *hba;
	unsigned int tag;
	u32 pos;
	int err;

	host = cmd->device->host;
	hba = shost_priv(host);
	tag = cmd->request->tag;

	err = ufshcd_issue_tm_cmd(hba, &hba->lrb[tag], UFS_LOGICAL_RESET);
	if (err)
		goto out;

	for (pos = 0; pos < hba->nutrs; pos++) {
		if (test_bit(pos, &hba->outstanding_reqs) &&
		    (hba->lrb[tag].lun == hba->lrb[pos].lun)) {

			/* clear the respective UTRLCLR register bit */
			ufshcd_utrl_clear(hba, pos);

			clear_bit(pos, &hba->outstanding_reqs);

			if (hba->lrb[pos].cmd) {
				scsi_dma_unmap(hba->lrb[pos].cmd);
				hba->lrb[pos].cmd->result =
						DID_ABORT << 16;
				hba->lrb[pos].cmd->scsi_done(cmd);
				hba->lrb[pos].cmd = NULL;
			}
		}
	} /* end of for */
out:
	return err;
}

/**
 * ufshcd_host_reset - Main reset function registered with scsi layer
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_host_reset(struct scsi_cmnd *cmd)
{
	struct ufs_hba *hba;

	hba = shost_priv(cmd->device->host);

	if (hba->ufshcd_state == UFSHCD_STATE_RESET)
		return SUCCESS;

	return (ufshcd_do_reset(hba) == SUCCESS) ? SUCCESS : FAILED;
}

/**
 * ufshcd_abort - abort a specific command
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host;
	struct ufs_hba *hba;
	unsigned long flags;
	unsigned int tag;
	int err;

	host = cmd->device->host;
	hba = shost_priv(host);
	tag = cmd->request->tag;

	spin_lock_irqsave(host->host_lock, flags);

	/* check if command is still pending */
	if (!(test_bit(tag, &hba->outstanding_reqs))) {
		err = FAILED;
		spin_unlock_irqrestore(host->host_lock, flags);
		goto out;
	}
	spin_unlock_irqrestore(host->host_lock, flags);

	err = ufshcd_issue_tm_cmd(hba, &hba->lrb[tag], UFS_ABORT_TASK);
	if (err)
		goto out;

	scsi_dma_unmap(cmd);

	spin_lock_irqsave(host->host_lock, flags);

	/* clear the respective UTRLCLR register bit */
	ufshcd_utrl_clear(hba, tag);

	__clear_bit(tag, &hba->outstanding_reqs);
	hba->lrb[tag].cmd = NULL;
	spin_unlock_irqrestore(host->host_lock, flags);
out:
	return err;
}

static struct scsi_host_template ufshcd_driver_template = {
	.module			= THIS_MODULE,
	.name			= UFSHCD,
	.proc_name		= UFSHCD,
	.queuecommand		= ufshcd_queuecommand,
	.slave_alloc		= ufshcd_slave_alloc,
	.slave_destroy		= ufshcd_slave_destroy,
	.eh_abort_handler	= ufshcd_abort,
	.eh_device_reset_handler = ufshcd_device_reset,
	.eh_host_reset_handler	= ufshcd_host_reset,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= UFSHCD_CMD_PER_LUN,
	.can_queue		= UFSHCD_CAN_QUEUE,
};

/**
 * ufshcd_shutdown - main function to put the controller in reset state
 * @pdev: pointer to PCI device handle
 */
static void ufshcd_shutdown(struct pci_dev *pdev)
{
	ufshcd_hba_stop((struct ufs_hba *)pci_get_drvdata(pdev));
}

#ifdef CONFIG_PM
/**
 * ufshcd_suspend - suspend power management function
 * @pdev: pointer to PCI device handle
 * @state: power state
 *
 * Returns -ENOSYS
 */
static int ufshcd_suspend(struct pci_dev *pdev, pm_message_t state)
{
	/*
	 * TODO:
	 * 1. Block SCSI requests from SCSI midlayer
	 * 2. Change the internal driver state to non operational
	 * 3. Set UTRLRSR and UTMRLRSR bits to zero
	 * 4. Wait until outstanding commands are completed
	 * 5. Set HCE to zero to send the UFS host controller to reset state
	 */

	return -ENOSYS;
}

/**
 * ufshcd_resume - resume power management function
 * @pdev: pointer to PCI device handle
 *
 * Returns -ENOSYS
 */
static int ufshcd_resume(struct pci_dev *pdev)
{
	/*
	 * TODO:
	 * 1. Set HCE to 1, to start the UFS host controller
	 * initialization process
	 * 2. Set UTRLRSR and UTMRLRSR bits to 1
	 * 3. Change the internal driver state to operational
	 * 4. Unblock SCSI requests from SCSI midlayer
	 */

	return -ENOSYS;
}
#endif /* CONFIG_PM */

/**
 * ufshcd_hba_free - free allocated memory for
 *			host memory space data structures
 * @hba: per adapter instance
 */
static void ufshcd_hba_free(struct ufs_hba *hba)
{
	iounmap(hba->mmio_base);
	ufshcd_free_hba_memory(hba);
	pci_release_regions(hba->pdev);
}

/**
 * ufshcd_remove - de-allocate PCI/SCSI host and host memory space
 *		data structure memory
 * @pdev - pointer to PCI handle
 */
static void ufshcd_remove(struct pci_dev *pdev)
{
	struct ufs_hba *hba = pci_get_drvdata(pdev);

	/* disable interrupts */
	ufshcd_int_config(hba, UFSHCD_INT_DISABLE);
	free_irq(pdev->irq, hba);

	ufshcd_hba_stop(hba);
	ufshcd_hba_free(hba);

	scsi_remove_host(hba->host);
	scsi_host_put(hba->host);
	pci_set_drvdata(pdev, NULL);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

/**
 * ufshcd_set_dma_mask - Set dma mask based on the controller
 *			 addressing capability
 * @pdev: PCI device structure
 *
 * Returns 0 for success, non-zero for failure
 */
static int ufshcd_set_dma_mask(struct ufs_hba *hba)
{
	int err;
	u64 dma_mask;

	/*
	 * If controller supports 64 bit addressing mode, then set the DMA
	 * mask to 64-bit, else set the DMA mask to 32-bit
	 */
	if (hba->capabilities & MASK_64_ADDRESSING_SUPPORT)
		dma_mask = DMA_BIT_MASK(64);
	else
		dma_mask = DMA_BIT_MASK(32);

	err = pci_set_dma_mask(hba->pdev, dma_mask);
	if (err)
		return err;

	err = pci_set_consistent_dma_mask(hba->pdev, dma_mask);

	return err;
}

/**
 * ufshcd_probe - probe routine of the driver
 * @pdev: pointer to PCI device handle
 * @id: PCI device id
 *
 * Returns 0 on success, non-zero value on failure
 */
static int __devinit
ufshcd_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host *host;
	struct ufs_hba *hba;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		goto out_error;
	}

	pci_set_master(pdev);

	host = scsi_host_alloc(&ufshcd_driver_template,
				sizeof(struct ufs_hba));
	if (!host) {
		dev_err(&pdev->dev, "scsi_host_alloc failed\n");
		err = -ENOMEM;
		goto out_disable;
	}
	hba = shost_priv(host);

	err = pci_request_regions(pdev, UFSHCD);
	if (err < 0) {
		dev_err(&pdev->dev, "request regions failed\n");
		goto out_host_put;
	}

	hba->mmio_base = pci_ioremap_bar(pdev, 0);
	if (!hba->mmio_base) {
		dev_err(&pdev->dev, "memory map failed\n");
		err = -ENOMEM;
		goto out_release_regions;
	}

	hba->host = host;
	hba->pdev = pdev;

	/* Read capabilities registers */
	ufshcd_hba_capabilities(hba);

	/* Get UFS version supported by the controller */
	hba->ufs_version = ufshcd_get_ufs_version(hba);

	err = ufshcd_set_dma_mask(hba);
	if (err) {
		dev_err(&pdev->dev, "set dma mask failed\n");
		goto out_iounmap;
	}

	/* Allocate memory for host memory space */
	err = ufshcd_memory_alloc(hba);
	if (err) {
		dev_err(&pdev->dev, "Memory allocation failed\n");
		goto out_iounmap;
	}

	/* Configure LRB */
	ufshcd_host_memory_configure(hba);

	host->can_queue = hba->nutrs;
	host->cmd_per_lun = hba->nutrs;
	host->max_id = UFSHCD_MAX_ID;
	host->max_lun = UFSHCD_MAX_LUNS;
	host->max_channel = UFSHCD_MAX_CHANNEL;
	host->unique_id = host->host_no;
	host->max_cmd_len = MAX_CDB_SIZE;

	/* Initailize wait queue for task management */
	init_waitqueue_head(&hba->ufshcd_tm_wait_queue);

	/* Initialize work queues */
	INIT_WORK(&hba->uic_workq, ufshcd_uic_cc_handler);
	INIT_WORK(&hba->feh_workq, ufshcd_fatal_err_handler);

	/* IRQ registration */
	err = request_irq(pdev->irq, ufshcd_intr, IRQF_SHARED, UFSHCD, hba);
	if (err) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto out_lrb_free;
	}

	/* Enable SCSI tag mapping */
	err = scsi_init_shared_tag_map(host, host->can_queue);
	if (err) {
		dev_err(&pdev->dev, "init shared queue failed\n");
		goto out_free_irq;
	}

	pci_set_drvdata(pdev, hba);

	err = scsi_add_host(host, &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "scsi_add_host failed\n");
		goto out_free_irq;
	}

	/* Initialization routine */
	err = ufshcd_initialize_hba(hba);
	if (err) {
		dev_err(&pdev->dev, "Initialization failed\n");
		goto out_free_irq;
	}

	return 0;

out_free_irq:
	free_irq(pdev->irq, hba);
out_lrb_free:
	ufshcd_free_hba_memory(hba);
out_iounmap:
	iounmap(hba->mmio_base);
out_release_regions:
	pci_release_regions(pdev);
out_host_put:
	scsi_host_put(host);
out_disable:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
out_error:
	return err;
}

static DEFINE_PCI_DEVICE_TABLE(ufshcd_pci_tbl) = {
	{ PCI_VENDOR_ID_SAMSUNG, 0xC00C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }	/* terminate list */
};

MODULE_DEVICE_TABLE(pci, ufshcd_pci_tbl);

static struct pci_driver ufshcd_pci_driver = {
	.name = UFSHCD,
	.id_table = ufshcd_pci_tbl,
	.probe = ufshcd_probe,
	.remove = __devexit_p(ufshcd_remove),
	.shutdown = ufshcd_shutdown,
#ifdef CONFIG_PM
	.suspend = ufshcd_suspend,
	.resume = ufshcd_resume,
#endif
};

/**
 * ufshcd_init - Driver registration routine
 */
static int __init ufshcd_init(void)
{
	return pci_register_driver(&ufshcd_pci_driver);
}
module_init(ufshcd_init);

/**
 * ufshcd_exit - Driver exit clean-up routine
 */
static void __exit ufshcd_exit(void)
{
	pci_unregister_driver(&ufshcd_pci_driver);
}
module_exit(ufshcd_exit);


MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>, "
	      "Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("Generic UFS host controller driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);
