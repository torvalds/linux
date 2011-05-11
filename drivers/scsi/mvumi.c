/*
 * Marvell UMI driver
 *
 * Copyright 2011 Marvell. <jyli@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/io.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_eh.h>
#include <linux/uaccess.h>

#include "mvumi.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jyli@marvell.com");
MODULE_DESCRIPTION("Marvell UMI Driver");

static DEFINE_PCI_DEVICE_TABLE(mvumi_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_2, PCI_DEVICE_ID_MARVELL_MV9143) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, mvumi_pci_table);

static void tag_init(struct mvumi_tag *st, unsigned short size)
{
	unsigned short i;
	BUG_ON(size != st->size);
	st->top = size;
	for (i = 0; i < size; i++)
		st->stack[i] = size - 1 - i;
}

static unsigned short tag_get_one(struct mvumi_hba *mhba, struct mvumi_tag *st)
{
	BUG_ON(st->top <= 0);
	return st->stack[--st->top];
}

static void tag_release_one(struct mvumi_hba *mhba, struct mvumi_tag *st,
							unsigned short tag)
{
	BUG_ON(st->top >= st->size);
	st->stack[st->top++] = tag;
}

static bool tag_is_empty(struct mvumi_tag *st)
{
	if (st->top == 0)
		return 1;
	else
		return 0;
}

static void mvumi_unmap_pci_addr(struct pci_dev *dev, void **addr_array)
{
	int i;

	for (i = 0; i < MAX_BASE_ADDRESS; i++)
		if ((pci_resource_flags(dev, i) & IORESOURCE_MEM) &&
								addr_array[i])
			pci_iounmap(dev, addr_array[i]);
}

static int mvumi_map_pci_addr(struct pci_dev *dev, void **addr_array)
{
	int i;

	for (i = 0; i < MAX_BASE_ADDRESS; i++) {
		if (pci_resource_flags(dev, i) & IORESOURCE_MEM) {
			addr_array[i] = pci_iomap(dev, i, 0);
			if (!addr_array[i]) {
				dev_err(&dev->dev, "failed to map Bar[%d]\n",
									i);
				mvumi_unmap_pci_addr(dev, addr_array);
				return -ENOMEM;
			}
		} else
			addr_array[i] = NULL;

		dev_dbg(&dev->dev, "Bar %d : %p.\n", i, addr_array[i]);
	}

	return 0;
}

static struct mvumi_res *mvumi_alloc_mem_resource(struct mvumi_hba *mhba,
				enum resource_type type, unsigned int size)
{
	struct mvumi_res *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (!res) {
		dev_err(&mhba->pdev->dev,
			"Failed to allocate memory for resouce manager.\n");
		return NULL;
	}

	switch (type) {
	case RESOURCE_CACHED_MEMORY:
		res->virt_addr = kzalloc(size, GFP_KERNEL);
		if (!res->virt_addr) {
			dev_err(&mhba->pdev->dev,
				"unable to allocate memory,size = %d.\n", size);
			kfree(res);
			return NULL;
		}
		break;

	case RESOURCE_UNCACHED_MEMORY:
		size = round_up(size, 8);
		res->virt_addr = pci_alloc_consistent(mhba->pdev, size,
							&res->bus_addr);
		if (!res->virt_addr) {
			dev_err(&mhba->pdev->dev,
					"unable to allocate consistent mem,"
							"size = %d.\n", size);
			kfree(res);
			return NULL;
		}
		memset(res->virt_addr, 0, size);
		break;

	default:
		dev_err(&mhba->pdev->dev, "unknown resource type %d.\n", type);
		kfree(res);
		return NULL;
	}

	res->type = type;
	res->size = size;
	INIT_LIST_HEAD(&res->entry);
	list_add_tail(&res->entry, &mhba->res_list);

	return res;
}

static void mvumi_release_mem_resource(struct mvumi_hba *mhba)
{
	struct mvumi_res *res, *tmp;

	list_for_each_entry_safe(res, tmp, &mhba->res_list, entry) {
		switch (res->type) {
		case RESOURCE_UNCACHED_MEMORY:
			pci_free_consistent(mhba->pdev, res->size,
						res->virt_addr, res->bus_addr);
			break;
		case RESOURCE_CACHED_MEMORY:
			kfree(res->virt_addr);
			break;
		default:
			dev_err(&mhba->pdev->dev,
				"unknown resource type %d\n", res->type);
			break;
		}
		list_del(&res->entry);
		kfree(res);
	}
	mhba->fw_flag &= ~MVUMI_FW_ALLOC;
}

/**
 * mvumi_make_sgl -	Prepares  SGL
 * @mhba:		Adapter soft state
 * @scmd:		SCSI command from the mid-layer
 * @sgl_p:		SGL to be filled in
 * @sg_count		return the number of SG elements
 *
 * If successful, this function returns 0. otherwise, it returns -1.
 */
static int mvumi_make_sgl(struct mvumi_hba *mhba, struct scsi_cmnd *scmd,
					void *sgl_p, unsigned char *sg_count)
{
	struct scatterlist *sg;
	struct mvumi_sgl *m_sg = (struct mvumi_sgl *) sgl_p;
	unsigned int i;
	unsigned int sgnum = scsi_sg_count(scmd);
	dma_addr_t busaddr;

	if (sgnum) {
		sg = scsi_sglist(scmd);
		*sg_count = pci_map_sg(mhba->pdev, sg, sgnum,
				(int) scmd->sc_data_direction);
		if (*sg_count > mhba->max_sge) {
			dev_err(&mhba->pdev->dev, "sg count[0x%x] is bigger "
						"than max sg[0x%x].\n",
						*sg_count, mhba->max_sge);
			return -1;
		}
		for (i = 0; i < *sg_count; i++) {
			busaddr = sg_dma_address(&sg[i]);
			m_sg->baseaddr_l = cpu_to_le32(lower_32_bits(busaddr));
			m_sg->baseaddr_h = cpu_to_le32(upper_32_bits(busaddr));
			m_sg->flags = 0;
			m_sg->size = cpu_to_le32(sg_dma_len(&sg[i]));
			if ((i + 1) == *sg_count)
				m_sg->flags |= SGD_EOT;

			m_sg++;
		}
	} else {
		scmd->SCp.dma_handle = scsi_bufflen(scmd) ?
			pci_map_single(mhba->pdev, scsi_sglist(scmd),
				scsi_bufflen(scmd),
				(int) scmd->sc_data_direction)
			: 0;
		busaddr = scmd->SCp.dma_handle;
		m_sg->baseaddr_l = cpu_to_le32(lower_32_bits(busaddr));
		m_sg->baseaddr_h = cpu_to_le32(upper_32_bits(busaddr));
		m_sg->flags = SGD_EOT;
		m_sg->size = cpu_to_le32(scsi_bufflen(scmd));
		*sg_count = 1;
	}

	return 0;
}

static int mvumi_internal_cmd_sgl(struct mvumi_hba *mhba, struct mvumi_cmd *cmd,
							unsigned int size)
{
	struct mvumi_sgl *m_sg;
	void *virt_addr;
	dma_addr_t phy_addr;

	if (size == 0)
		return 0;

	virt_addr = pci_alloc_consistent(mhba->pdev, size, &phy_addr);
	if (!virt_addr)
		return -1;

	memset(virt_addr, 0, size);

	m_sg = (struct mvumi_sgl *) &cmd->frame->payload[0];
	cmd->frame->sg_counts = 1;
	cmd->data_buf = virt_addr;

	m_sg->baseaddr_l = cpu_to_le32(lower_32_bits(phy_addr));
	m_sg->baseaddr_h = cpu_to_le32(upper_32_bits(phy_addr));
	m_sg->flags = SGD_EOT;
	m_sg->size = cpu_to_le32(size);

	return 0;
}

static struct mvumi_cmd *mvumi_create_internal_cmd(struct mvumi_hba *mhba,
				unsigned int buf_size)
{
	struct mvumi_cmd *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		dev_err(&mhba->pdev->dev, "failed to create a internal cmd\n");
		return NULL;
	}
	INIT_LIST_HEAD(&cmd->queue_pointer);

	cmd->frame = kzalloc(mhba->ib_max_size, GFP_KERNEL);
	if (!cmd->frame) {
		dev_err(&mhba->pdev->dev, "failed to allocate memory for FW"
			" frame,size = %d.\n", mhba->ib_max_size);
		kfree(cmd);
		return NULL;
	}

	if (buf_size) {
		if (mvumi_internal_cmd_sgl(mhba, cmd, buf_size)) {
			dev_err(&mhba->pdev->dev, "failed to allocate memory"
						" for internal frame\n");
			kfree(cmd->frame);
			kfree(cmd);
			return NULL;
		}
	} else
		cmd->frame->sg_counts = 0;

	return cmd;
}

static void mvumi_delete_internal_cmd(struct mvumi_hba *mhba,
						struct mvumi_cmd *cmd)
{
	struct mvumi_sgl *m_sg;
	unsigned int size;
	dma_addr_t phy_addr;

	if (cmd && cmd->frame) {
		if (cmd->frame->sg_counts) {
			m_sg = (struct mvumi_sgl *) &cmd->frame->payload[0];
			size = m_sg->size;

			phy_addr = (dma_addr_t) m_sg->baseaddr_l |
				(dma_addr_t) ((m_sg->baseaddr_h << 16) << 16);

			pci_free_consistent(mhba->pdev, size, cmd->data_buf,
								phy_addr);
		}
		kfree(cmd->frame);
		kfree(cmd);
	}
}

/**
 * mvumi_get_cmd -	Get a command from the free pool
 * @mhba:		Adapter soft state
 *
 * Returns a free command from the pool
 */
static struct mvumi_cmd *mvumi_get_cmd(struct mvumi_hba *mhba)
{
	struct mvumi_cmd *cmd = NULL;

	if (likely(!list_empty(&mhba->cmd_pool))) {
		cmd = list_entry((&mhba->cmd_pool)->next,
				struct mvumi_cmd, queue_pointer);
		list_del_init(&cmd->queue_pointer);
	} else
		dev_warn(&mhba->pdev->dev, "command pool is empty!\n");

	return cmd;
}

/**
 * mvumi_return_cmd -	Return a cmd to free command pool
 * @mhba:		Adapter soft state
 * @cmd:		Command packet to be returned to free command pool
 */
static inline void mvumi_return_cmd(struct mvumi_hba *mhba,
						struct mvumi_cmd *cmd)
{
	cmd->scmd = NULL;
	list_add_tail(&cmd->queue_pointer, &mhba->cmd_pool);
}

/**
 * mvumi_free_cmds -	Free all the cmds in the free cmd pool
 * @mhba:		Adapter soft state
 */
static void mvumi_free_cmds(struct mvumi_hba *mhba)
{
	struct mvumi_cmd *cmd;

	while (!list_empty(&mhba->cmd_pool)) {
		cmd = list_first_entry(&mhba->cmd_pool, struct mvumi_cmd,
							queue_pointer);
		list_del(&cmd->queue_pointer);
		kfree(cmd->frame);
		kfree(cmd);
	}
}

/**
 * mvumi_alloc_cmds -	Allocates the command packets
 * @mhba:		Adapter soft state
 *
 */
static int mvumi_alloc_cmds(struct mvumi_hba *mhba)
{
	int i;
	struct mvumi_cmd *cmd;

	for (i = 0; i < mhba->max_io; i++) {
		cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
		if (!cmd)
			goto err_exit;

		INIT_LIST_HEAD(&cmd->queue_pointer);
		list_add_tail(&cmd->queue_pointer, &mhba->cmd_pool);
		cmd->frame = kzalloc(mhba->ib_max_size, GFP_KERNEL);
		if (!cmd->frame)
			goto err_exit;
	}
	return 0;

err_exit:
	dev_err(&mhba->pdev->dev,
			"failed to allocate memory for cmd[0x%x].\n", i);
	while (!list_empty(&mhba->cmd_pool)) {
		cmd = list_first_entry(&mhba->cmd_pool, struct mvumi_cmd,
						queue_pointer);
		list_del(&cmd->queue_pointer);
		kfree(cmd->frame);
		kfree(cmd);
	}
	return -ENOMEM;
}

static int mvumi_get_ib_list_entry(struct mvumi_hba *mhba, void **ib_entry)
{
	unsigned int ib_rp_reg, cur_ib_entry;

	if (atomic_read(&mhba->fw_outstanding) >= mhba->max_io) {
		dev_warn(&mhba->pdev->dev, "firmware io overflow.\n");
		return -1;
	}
	ib_rp_reg = ioread32(mhba->mmio + CLA_INB_READ_POINTER);

	if (unlikely(((ib_rp_reg & CL_SLOT_NUM_MASK) ==
			(mhba->ib_cur_slot & CL_SLOT_NUM_MASK)) &&
			((ib_rp_reg & CL_POINTER_TOGGLE) !=
			(mhba->ib_cur_slot & CL_POINTER_TOGGLE)))) {
		dev_warn(&mhba->pdev->dev, "no free slot to use.\n");
		return -1;
	}

	cur_ib_entry = mhba->ib_cur_slot & CL_SLOT_NUM_MASK;
	cur_ib_entry++;
	if (cur_ib_entry >= mhba->list_num_io) {
		cur_ib_entry -= mhba->list_num_io;
		mhba->ib_cur_slot ^= CL_POINTER_TOGGLE;
	}
	mhba->ib_cur_slot &= ~CL_SLOT_NUM_MASK;
	mhba->ib_cur_slot |= (cur_ib_entry & CL_SLOT_NUM_MASK);
	*ib_entry = mhba->ib_list + cur_ib_entry * mhba->ib_max_size;
	atomic_inc(&mhba->fw_outstanding);

	return 0;
}

static void mvumi_send_ib_list_entry(struct mvumi_hba *mhba)
{
	iowrite32(0xfff, mhba->ib_shadow);
	iowrite32(mhba->ib_cur_slot, mhba->mmio + CLA_INB_WRITE_POINTER);
}

static char mvumi_check_ob_frame(struct mvumi_hba *mhba,
		unsigned int cur_obf, struct mvumi_rsp_frame *p_outb_frame)
{
	unsigned short tag, request_id;

	udelay(1);
	p_outb_frame = mhba->ob_list + cur_obf * mhba->ob_max_size;
	request_id = p_outb_frame->request_id;
	tag = p_outb_frame->tag;
	if (tag > mhba->tag_pool.size) {
		dev_err(&mhba->pdev->dev, "ob frame data error\n");
		return -1;
	}
	if (mhba->tag_cmd[tag] == NULL) {
		dev_err(&mhba->pdev->dev, "tag[0x%x] with NO command\n", tag);
		return -1;
	} else if (mhba->tag_cmd[tag]->request_id != request_id &&
						mhba->request_id_enabled) {
			dev_err(&mhba->pdev->dev, "request ID from FW:0x%x,"
					"cmd request ID:0x%x\n", request_id,
					mhba->tag_cmd[tag]->request_id);
			return -1;
	}

	return 0;
}

static void mvumi_receive_ob_list_entry(struct mvumi_hba *mhba)
{
	unsigned int ob_write_reg, ob_write_shadow_reg;
	unsigned int cur_obf, assign_obf_end, i;
	struct mvumi_ob_data *ob_data;
	struct mvumi_rsp_frame *p_outb_frame;

	do {
		ob_write_reg = ioread32(mhba->mmio + CLA_OUTB_COPY_POINTER);
		ob_write_shadow_reg = ioread32(mhba->ob_shadow);
	} while ((ob_write_reg & CL_SLOT_NUM_MASK) != ob_write_shadow_reg);

	cur_obf = mhba->ob_cur_slot & CL_SLOT_NUM_MASK;
	assign_obf_end = ob_write_reg & CL_SLOT_NUM_MASK;

	if ((ob_write_reg & CL_POINTER_TOGGLE) !=
				(mhba->ob_cur_slot & CL_POINTER_TOGGLE)) {
		assign_obf_end += mhba->list_num_io;
	}

	for (i = (assign_obf_end - cur_obf); i != 0; i--) {
		cur_obf++;
		if (cur_obf >= mhba->list_num_io) {
			cur_obf -= mhba->list_num_io;
			mhba->ob_cur_slot ^= CL_POINTER_TOGGLE;
		}

		p_outb_frame = mhba->ob_list + cur_obf * mhba->ob_max_size;

		/* Copy pointer may point to entry in outbound list
		*  before entry has valid data
		*/
		if (unlikely(p_outb_frame->tag > mhba->tag_pool.size ||
			mhba->tag_cmd[p_outb_frame->tag] == NULL ||
			p_outb_frame->request_id !=
				mhba->tag_cmd[p_outb_frame->tag]->request_id))
			if (mvumi_check_ob_frame(mhba, cur_obf, p_outb_frame))
				continue;

		if (!list_empty(&mhba->ob_data_list)) {
			ob_data = (struct mvumi_ob_data *)
				list_first_entry(&mhba->ob_data_list,
					struct mvumi_ob_data, list);
			list_del_init(&ob_data->list);
		} else {
			ob_data = NULL;
			if (cur_obf == 0) {
				cur_obf = mhba->list_num_io - 1;
				mhba->ob_cur_slot ^= CL_POINTER_TOGGLE;
			} else
				cur_obf -= 1;
			break;
		}

		memcpy(ob_data->data, p_outb_frame, mhba->ob_max_size);
		p_outb_frame->tag = 0xff;

		list_add_tail(&ob_data->list, &mhba->free_ob_list);
	}
	mhba->ob_cur_slot &= ~CL_SLOT_NUM_MASK;
	mhba->ob_cur_slot |= (cur_obf & CL_SLOT_NUM_MASK);
	iowrite32(mhba->ob_cur_slot, mhba->mmio + CLA_OUTB_READ_POINTER);
}

static void mvumi_reset(void *regs)
{
	iowrite32(0, regs + CPU_ENPOINTA_MASK_REG);
	if (ioread32(regs + CPU_ARM_TO_PCIEA_MSG1) != HANDSHAKE_DONESTATE)
		return;

	iowrite32(DRBL_SOFT_RESET, regs + CPU_PCIEA_TO_ARM_DRBL_REG);
}

static unsigned char mvumi_start(struct mvumi_hba *mhba);

static int mvumi_wait_for_outstanding(struct mvumi_hba *mhba)
{
	mhba->fw_state = FW_STATE_ABORT;
	mvumi_reset(mhba->mmio);

	if (mvumi_start(mhba))
		return FAILED;
	else
		return SUCCESS;
}

static int mvumi_host_reset(struct scsi_cmnd *scmd)
{
	struct mvumi_hba *mhba;

	mhba = (struct mvumi_hba *) scmd->device->host->hostdata;

	scmd_printk(KERN_NOTICE, scmd, "RESET -%ld cmd=%x retries=%x\n",
			scmd->serial_number, scmd->cmnd[0], scmd->retries);

	return mvumi_wait_for_outstanding(mhba);
}

static int mvumi_issue_blocked_cmd(struct mvumi_hba *mhba,
						struct mvumi_cmd *cmd)
{
	unsigned long flags;

	cmd->cmd_status = REQ_STATUS_PENDING;

	if (atomic_read(&cmd->sync_cmd)) {
		dev_err(&mhba->pdev->dev,
			"last blocked cmd not finished, sync_cmd = %d\n",
						atomic_read(&cmd->sync_cmd));
		BUG_ON(1);
		return -1;
	}
	atomic_inc(&cmd->sync_cmd);
	spin_lock_irqsave(mhba->shost->host_lock, flags);
	mhba->instancet->fire_cmd(mhba, cmd);
	spin_unlock_irqrestore(mhba->shost->host_lock, flags);

	wait_event_timeout(mhba->int_cmd_wait_q,
		(cmd->cmd_status != REQ_STATUS_PENDING),
		MVUMI_INTERNAL_CMD_WAIT_TIME * HZ);

	/* command timeout */
	if (atomic_read(&cmd->sync_cmd)) {
		spin_lock_irqsave(mhba->shost->host_lock, flags);
		atomic_dec(&cmd->sync_cmd);
		if (mhba->tag_cmd[cmd->frame->tag]) {
			mhba->tag_cmd[cmd->frame->tag] = 0;
			dev_warn(&mhba->pdev->dev, "TIMEOUT:release tag [%d]\n",
							cmd->frame->tag);
			tag_release_one(mhba, &mhba->tag_pool, cmd->frame->tag);
		}
		if (!list_empty(&cmd->queue_pointer)) {
			dev_warn(&mhba->pdev->dev,
				"TIMEOUT:A internal command doesn't send!\n");
			list_del_init(&cmd->queue_pointer);
		} else
			atomic_dec(&mhba->fw_outstanding);

		spin_unlock_irqrestore(mhba->shost->host_lock, flags);
	}
	return 0;
}

static void mvumi_release_fw(struct mvumi_hba *mhba)
{
	mvumi_free_cmds(mhba);
	mvumi_release_mem_resource(mhba);
	mvumi_unmap_pci_addr(mhba->pdev, mhba->base_addr);
	kfree(mhba->handshake_page);
	pci_release_regions(mhba->pdev);
}

static unsigned char mvumi_flush_cache(struct mvumi_hba *mhba)
{
	struct mvumi_cmd *cmd;
	struct mvumi_msg_frame *frame;
	unsigned char device_id, retry = 0;
	unsigned char bitcount = sizeof(unsigned char) * 8;

	for (device_id = 0; device_id < mhba->max_target_id; device_id++) {
		if (!(mhba->target_map[device_id / bitcount] &
				(1 << (device_id % bitcount))))
			continue;
get_cmd:	cmd = mvumi_create_internal_cmd(mhba, 0);
		if (!cmd) {
			if (retry++ >= 5) {
				dev_err(&mhba->pdev->dev, "failed to get memory"
					" for internal flush cache cmd for "
					"device %d", device_id);
				retry = 0;
				continue;
			} else
				goto get_cmd;
		}
		cmd->scmd = NULL;
		cmd->cmd_status = REQ_STATUS_PENDING;
		atomic_set(&cmd->sync_cmd, 0);
		frame = cmd->frame;
		frame->req_function = CL_FUN_SCSI_CMD;
		frame->device_id = device_id;
		frame->cmd_flag = CMD_FLAG_NON_DATA;
		frame->data_transfer_length = 0;
		frame->cdb_length = MAX_COMMAND_SIZE;
		memset(frame->cdb, 0, MAX_COMMAND_SIZE);
		frame->cdb[0] = SCSI_CMD_MARVELL_SPECIFIC;
		frame->cdb[2] = CDB_CORE_SHUTDOWN;

		mvumi_issue_blocked_cmd(mhba, cmd);
		if (cmd->cmd_status != SAM_STAT_GOOD) {
			dev_err(&mhba->pdev->dev,
				"device %d flush cache failed, status=0x%x.\n",
				device_id, cmd->cmd_status);
		}

		mvumi_delete_internal_cmd(mhba, cmd);
	}
	return 0;
}

static unsigned char
mvumi_calculate_checksum(struct mvumi_hs_header *p_header,
							unsigned short len)
{
	unsigned char *ptr;
	unsigned char ret = 0, i;

	ptr = (unsigned char *) p_header->frame_content;
	for (i = 0; i < len; i++) {
		ret ^= *ptr;
		ptr++;
	}

	return ret;
}

void mvumi_hs_build_page(struct mvumi_hba *mhba,
				struct mvumi_hs_header *hs_header)
{
	struct mvumi_hs_page2 *hs_page2;
	struct mvumi_hs_page4 *hs_page4;
	struct mvumi_hs_page3 *hs_page3;
	struct timeval time;
	unsigned int local_time;

	switch (hs_header->page_code) {
	case HS_PAGE_HOST_INFO:
		hs_page2 = (struct mvumi_hs_page2 *) hs_header;
		hs_header->frame_length = sizeof(*hs_page2) - 4;
		memset(hs_header->frame_content, 0, hs_header->frame_length);
		hs_page2->host_type = 3; /* 3 mean linux*/
		hs_page2->host_ver.ver_major = VER_MAJOR;
		hs_page2->host_ver.ver_minor = VER_MINOR;
		hs_page2->host_ver.ver_oem = VER_OEM;
		hs_page2->host_ver.ver_build = VER_BUILD;
		hs_page2->system_io_bus = 0;
		hs_page2->slot_number = 0;
		hs_page2->intr_level = 0;
		hs_page2->intr_vector = 0;
		do_gettimeofday(&time);
		local_time = (unsigned int) (time.tv_sec -
						(sys_tz.tz_minuteswest * 60));
		hs_page2->seconds_since1970 = local_time;
		hs_header->checksum = mvumi_calculate_checksum(hs_header,
						hs_header->frame_length);
		break;

	case HS_PAGE_FIRM_CTL:
		hs_page3 = (struct mvumi_hs_page3 *) hs_header;
		hs_header->frame_length = sizeof(*hs_page3) - 4;
		memset(hs_header->frame_content, 0, hs_header->frame_length);
		hs_header->checksum = mvumi_calculate_checksum(hs_header,
						hs_header->frame_length);
		break;

	case HS_PAGE_CL_INFO:
		hs_page4 = (struct mvumi_hs_page4 *) hs_header;
		hs_header->frame_length = sizeof(*hs_page4) - 4;
		memset(hs_header->frame_content, 0, hs_header->frame_length);
		hs_page4->ib_baseaddr_l = lower_32_bits(mhba->ib_list_phys);
		hs_page4->ib_baseaddr_h = upper_32_bits(mhba->ib_list_phys);

		hs_page4->ob_baseaddr_l = lower_32_bits(mhba->ob_list_phys);
		hs_page4->ob_baseaddr_h = upper_32_bits(mhba->ob_list_phys);
		hs_page4->ib_entry_size = mhba->ib_max_size_setting;
		hs_page4->ob_entry_size = mhba->ob_max_size_setting;
		hs_page4->ob_depth = mhba->list_num_io;
		hs_page4->ib_depth = mhba->list_num_io;
		hs_header->checksum = mvumi_calculate_checksum(hs_header,
						hs_header->frame_length);
		break;

	default:
		dev_err(&mhba->pdev->dev, "cannot build page, code[0x%x]\n",
			hs_header->page_code);
		break;
	}
}

/**
 * mvumi_init_data -	Initialize requested date for FW
 * @mhba:			Adapter soft state
 */
static int mvumi_init_data(struct mvumi_hba *mhba)
{
	struct mvumi_ob_data *ob_pool;
	struct mvumi_res *res_mgnt;
	unsigned int tmp_size, offset, i;
	void *virmem, *v;
	dma_addr_t p;

	if (mhba->fw_flag & MVUMI_FW_ALLOC)
		return 0;

	tmp_size = mhba->ib_max_size * mhba->max_io;
	tmp_size += 128 + mhba->ob_max_size * mhba->max_io;
	tmp_size += 8 + sizeof(u32) + 16;

	res_mgnt = mvumi_alloc_mem_resource(mhba,
					RESOURCE_UNCACHED_MEMORY, tmp_size);
	if (!res_mgnt) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for inbound list\n");
		goto fail_alloc_dma_buf;
	}

	p = res_mgnt->bus_addr;
	v = res_mgnt->virt_addr;
	/* ib_list */
	offset = round_up(p, 128) - p;
	p += offset;
	v += offset;
	mhba->ib_list = v;
	mhba->ib_list_phys = p;
	v += mhba->ib_max_size * mhba->max_io;
	p += mhba->ib_max_size * mhba->max_io;
	/* ib shadow */
	offset = round_up(p, 8) - p;
	p += offset;
	v += offset;
	mhba->ib_shadow = v;
	mhba->ib_shadow_phys = p;
	p += sizeof(u32);
	v += sizeof(u32);
	/* ob shadow */
	offset = round_up(p, 8) - p;
	p += offset;
	v += offset;
	mhba->ob_shadow = v;
	mhba->ob_shadow_phys = p;
	p += 8;
	v += 8;

	/* ob list */
	offset = round_up(p, 128) - p;
	p += offset;
	v += offset;

	mhba->ob_list = v;
	mhba->ob_list_phys = p;

	/* ob data pool */
	tmp_size = mhba->max_io * (mhba->ob_max_size + sizeof(*ob_pool));
	tmp_size = round_up(tmp_size, 8);

	res_mgnt = mvumi_alloc_mem_resource(mhba,
				RESOURCE_CACHED_MEMORY, tmp_size);
	if (!res_mgnt) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for outbound data buffer\n");
		goto fail_alloc_dma_buf;
	}
	virmem = res_mgnt->virt_addr;

	for (i = mhba->max_io; i != 0; i--) {
		ob_pool = (struct mvumi_ob_data *) virmem;
		list_add_tail(&ob_pool->list, &mhba->ob_data_list);
		virmem += mhba->ob_max_size + sizeof(*ob_pool);
	}

	tmp_size = sizeof(unsigned short) * mhba->max_io +
				sizeof(struct mvumi_cmd *) * mhba->max_io;
	tmp_size += round_up(mhba->max_target_id, sizeof(unsigned char) * 8) /
						(sizeof(unsigned char) * 8);

	res_mgnt = mvumi_alloc_mem_resource(mhba,
				RESOURCE_CACHED_MEMORY, tmp_size);
	if (!res_mgnt) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for tag and target map\n");
		goto fail_alloc_dma_buf;
	}

	virmem = res_mgnt->virt_addr;
	mhba->tag_pool.stack = virmem;
	mhba->tag_pool.size = mhba->max_io;
	tag_init(&mhba->tag_pool, mhba->max_io);
	virmem += sizeof(unsigned short) * mhba->max_io;

	mhba->tag_cmd = virmem;
	virmem += sizeof(struct mvumi_cmd *) * mhba->max_io;

	mhba->target_map = virmem;

	mhba->fw_flag |= MVUMI_FW_ALLOC;
	return 0;

fail_alloc_dma_buf:
	mvumi_release_mem_resource(mhba);
	return -1;
}

static int mvumi_hs_process_page(struct mvumi_hba *mhba,
				struct mvumi_hs_header *hs_header)
{
	struct mvumi_hs_page1 *hs_page1;
	unsigned char page_checksum;

	page_checksum = mvumi_calculate_checksum(hs_header,
						hs_header->frame_length);
	if (page_checksum != hs_header->checksum) {
		dev_err(&mhba->pdev->dev, "checksum error\n");
		return -1;
	}

	switch (hs_header->page_code) {
	case HS_PAGE_FIRM_CAP:
		hs_page1 = (struct mvumi_hs_page1 *) hs_header;

		mhba->max_io = hs_page1->max_io_support;
		mhba->list_num_io = hs_page1->cl_inout_list_depth;
		mhba->max_transfer_size = hs_page1->max_transfer_size;
		mhba->max_target_id = hs_page1->max_devices_support;
		mhba->hba_capability = hs_page1->capability;
		mhba->ib_max_size_setting = hs_page1->cl_in_max_entry_size;
		mhba->ib_max_size = (1 << hs_page1->cl_in_max_entry_size) << 2;

		mhba->ob_max_size_setting = hs_page1->cl_out_max_entry_size;
		mhba->ob_max_size = (1 << hs_page1->cl_out_max_entry_size) << 2;

		dev_dbg(&mhba->pdev->dev, "FW version:%d\n",
						hs_page1->fw_ver.ver_build);

		break;
	default:
		dev_err(&mhba->pdev->dev, "handshake: page code error\n");
		return -1;
	}
	return 0;
}

/**
 * mvumi_handshake -	Move the FW to READY state
 * @mhba:				Adapter soft state
 *
 * During the initialization, FW passes can potentially be in any one of
 * several possible states. If the FW in operational, waiting-for-handshake
 * states, driver must take steps to bring it to ready state. Otherwise, it
 * has to wait for the ready state.
 */
static int mvumi_handshake(struct mvumi_hba *mhba)
{
	unsigned int hs_state, tmp, hs_fun;
	struct mvumi_hs_header *hs_header;
	void *regs = mhba->mmio;

	if (mhba->fw_state == FW_STATE_STARTING)
		hs_state = HS_S_START;
	else {
		tmp = ioread32(regs + CPU_ARM_TO_PCIEA_MSG0);
		hs_state = HS_GET_STATE(tmp);
		dev_dbg(&mhba->pdev->dev, "handshake state[0x%x].\n", hs_state);
		if (HS_GET_STATUS(tmp) != HS_STATUS_OK) {
			mhba->fw_state = FW_STATE_STARTING;
			return -1;
		}
	}

	hs_fun = 0;
	switch (hs_state) {
	case HS_S_START:
		mhba->fw_state = FW_STATE_HANDSHAKING;
		HS_SET_STATUS(hs_fun, HS_STATUS_OK);
		HS_SET_STATE(hs_fun, HS_S_RESET);
		iowrite32(HANDSHAKE_SIGNATURE, regs + CPU_PCIEA_TO_ARM_MSG1);
		iowrite32(hs_fun, regs + CPU_PCIEA_TO_ARM_MSG0);
		iowrite32(DRBL_HANDSHAKE, regs + CPU_PCIEA_TO_ARM_DRBL_REG);
		break;

	case HS_S_RESET:
		iowrite32(lower_32_bits(mhba->handshake_page_phys),
					regs + CPU_PCIEA_TO_ARM_MSG1);
		iowrite32(upper_32_bits(mhba->handshake_page_phys),
					regs + CPU_ARM_TO_PCIEA_MSG1);
		HS_SET_STATUS(hs_fun, HS_STATUS_OK);
		HS_SET_STATE(hs_fun, HS_S_PAGE_ADDR);
		iowrite32(hs_fun, regs + CPU_PCIEA_TO_ARM_MSG0);
		iowrite32(DRBL_HANDSHAKE, regs + CPU_PCIEA_TO_ARM_DRBL_REG);

		break;

	case HS_S_PAGE_ADDR:
	case HS_S_QUERY_PAGE:
	case HS_S_SEND_PAGE:
		hs_header = (struct mvumi_hs_header *) mhba->handshake_page;
		if (hs_header->page_code == HS_PAGE_FIRM_CAP) {
			mhba->hba_total_pages =
			((struct mvumi_hs_page1 *) hs_header)->total_pages;

			if (mhba->hba_total_pages == 0)
				mhba->hba_total_pages = HS_PAGE_TOTAL-1;
		}

		if (hs_state == HS_S_QUERY_PAGE) {
			if (mvumi_hs_process_page(mhba, hs_header)) {
				HS_SET_STATE(hs_fun, HS_S_ABORT);
				return -1;
			}
			if (mvumi_init_data(mhba)) {
				HS_SET_STATE(hs_fun, HS_S_ABORT);
				return -1;
			}
		} else if (hs_state == HS_S_PAGE_ADDR) {
			hs_header->page_code = 0;
			mhba->hba_total_pages = HS_PAGE_TOTAL-1;
		}

		if ((hs_header->page_code + 1) <= mhba->hba_total_pages) {
			hs_header->page_code++;
			if (hs_header->page_code != HS_PAGE_FIRM_CAP) {
				mvumi_hs_build_page(mhba, hs_header);
				HS_SET_STATE(hs_fun, HS_S_SEND_PAGE);
			} else
				HS_SET_STATE(hs_fun, HS_S_QUERY_PAGE);
		} else
			HS_SET_STATE(hs_fun, HS_S_END);

		HS_SET_STATUS(hs_fun, HS_STATUS_OK);
		iowrite32(hs_fun, regs + CPU_PCIEA_TO_ARM_MSG0);
		iowrite32(DRBL_HANDSHAKE, regs + CPU_PCIEA_TO_ARM_DRBL_REG);
		break;

	case HS_S_END:
		/* Set communication list ISR */
		tmp = ioread32(regs + CPU_ENPOINTA_MASK_REG);
		tmp |= INT_MAP_COMAOUT | INT_MAP_COMAERR;
		iowrite32(tmp, regs + CPU_ENPOINTA_MASK_REG);
		iowrite32(mhba->list_num_io, mhba->ib_shadow);
		/* Set InBound List Avaliable count shadow */
		iowrite32(lower_32_bits(mhba->ib_shadow_phys),
					regs + CLA_INB_AVAL_COUNT_BASEL);
		iowrite32(upper_32_bits(mhba->ib_shadow_phys),
					regs + CLA_INB_AVAL_COUNT_BASEH);

		/* Set OutBound List Avaliable count shadow */
		iowrite32((mhba->list_num_io-1) | CL_POINTER_TOGGLE,
						mhba->ob_shadow);
		iowrite32(lower_32_bits(mhba->ob_shadow_phys), regs + 0x5B0);
		iowrite32(upper_32_bits(mhba->ob_shadow_phys), regs + 0x5B4);

		mhba->ib_cur_slot = (mhba->list_num_io - 1) | CL_POINTER_TOGGLE;
		mhba->ob_cur_slot = (mhba->list_num_io - 1) | CL_POINTER_TOGGLE;
		mhba->fw_state = FW_STATE_STARTED;

		break;
	default:
		dev_err(&mhba->pdev->dev, "unknown handshake state [0x%x].\n",
								hs_state);
		return -1;
	}
	return 0;
}

static unsigned char mvumi_handshake_event(struct mvumi_hba *mhba)
{
	unsigned int isr_status;
	unsigned long before;

	before = jiffies;
	mvumi_handshake(mhba);
	do {
		isr_status = mhba->instancet->read_fw_status_reg(mhba->mmio);

		if (mhba->fw_state == FW_STATE_STARTED)
			return 0;
		if (time_after(jiffies, before + FW_MAX_DELAY * HZ)) {
			dev_err(&mhba->pdev->dev,
				"no handshake response at state 0x%x.\n",
				  mhba->fw_state);
			dev_err(&mhba->pdev->dev,
				"isr : global=0x%x,status=0x%x.\n",
					mhba->global_isr, isr_status);
			return -1;
		}
		rmb();
		usleep_range(1000, 2000);
	} while (!(isr_status & DRBL_HANDSHAKE_ISR));

	return 0;
}

static unsigned char mvumi_check_handshake(struct mvumi_hba *mhba)
{
	void *regs = mhba->mmio;
	unsigned int tmp;
	unsigned long before;

	before = jiffies;
	tmp = ioread32(regs + CPU_ARM_TO_PCIEA_MSG1);
	while ((tmp != HANDSHAKE_READYSTATE) && (tmp != HANDSHAKE_DONESTATE)) {
		if (tmp != HANDSHAKE_READYSTATE)
			iowrite32(DRBL_MU_RESET,
					regs + CPU_PCIEA_TO_ARM_DRBL_REG);
		if (time_after(jiffies, before + FW_MAX_DELAY * HZ)) {
			dev_err(&mhba->pdev->dev,
				"invalid signature [0x%x].\n", tmp);
			return -1;
		}
		usleep_range(1000, 2000);
		rmb();
		tmp = ioread32(regs + CPU_ARM_TO_PCIEA_MSG1);
	}

	mhba->fw_state = FW_STATE_STARTING;
	dev_dbg(&mhba->pdev->dev, "start firmware handshake...\n");
	do {
		if (mvumi_handshake_event(mhba)) {
			dev_err(&mhba->pdev->dev,
					"handshake failed at state 0x%x.\n",
						mhba->fw_state);
			return -1;
		}
	} while (mhba->fw_state != FW_STATE_STARTED);

	dev_dbg(&mhba->pdev->dev, "firmware handshake done\n");

	return 0;
}

static unsigned char mvumi_start(struct mvumi_hba *mhba)
{
	void *regs = mhba->mmio;
	unsigned int tmp;
	/* clear Door bell */
	tmp = ioread32(regs + CPU_ARM_TO_PCIEA_DRBL_REG);
	iowrite32(tmp, regs + CPU_ARM_TO_PCIEA_DRBL_REG);

	iowrite32(0x3FFFFFFF, regs + CPU_ARM_TO_PCIEA_MASK_REG);
	tmp = ioread32(regs + CPU_ENPOINTA_MASK_REG) | INT_MAP_DL_CPU2PCIEA;
	iowrite32(tmp, regs + CPU_ENPOINTA_MASK_REG);
	if (mvumi_check_handshake(mhba))
		return -1;

	return 0;
}

/**
 * mvumi_complete_cmd -	Completes a command
 * @mhba:			Adapter soft state
 * @cmd:			Command to be completed
 */
static void mvumi_complete_cmd(struct mvumi_hba *mhba, struct mvumi_cmd *cmd,
					struct mvumi_rsp_frame *ob_frame)
{
	struct scsi_cmnd *scmd = cmd->scmd;

	cmd->scmd->SCp.ptr = NULL;
	scmd->result = ob_frame->req_status;

	switch (ob_frame->req_status) {
	case SAM_STAT_GOOD:
		scmd->result |= DID_OK << 16;
		break;
	case SAM_STAT_BUSY:
		scmd->result |= DID_BUS_BUSY << 16;
		break;
	case SAM_STAT_CHECK_CONDITION:
		scmd->result |= (DID_OK << 16);
		if (ob_frame->rsp_flag & CL_RSP_FLAG_SENSEDATA) {
			memcpy(cmd->scmd->sense_buffer, ob_frame->payload,
				sizeof(struct mvumi_sense_data));
			scmd->result |=  (DRIVER_SENSE << 24);
		}
		break;
	default:
		scmd->result |= (DRIVER_INVALID << 24) | (DID_ABORT << 16);
		break;
	}

	if (scsi_bufflen(scmd)) {
		if (scsi_sg_count(scmd)) {
			pci_unmap_sg(mhba->pdev,
				scsi_sglist(scmd),
				scsi_sg_count(scmd),
				(int) scmd->sc_data_direction);
		} else {
			pci_unmap_single(mhba->pdev,
				scmd->SCp.dma_handle,
				scsi_bufflen(scmd),
				(int) scmd->sc_data_direction);

			scmd->SCp.dma_handle = 0;
		}
	}
	cmd->scmd->scsi_done(scmd);
	mvumi_return_cmd(mhba, cmd);
}
static void mvumi_complete_internal_cmd(struct mvumi_hba *mhba,
						struct mvumi_cmd *cmd,
					struct mvumi_rsp_frame *ob_frame)
{
	if (atomic_read(&cmd->sync_cmd)) {
		cmd->cmd_status = ob_frame->req_status;

		if ((ob_frame->req_status == SAM_STAT_CHECK_CONDITION) &&
				(ob_frame->rsp_flag & CL_RSP_FLAG_SENSEDATA) &&
				cmd->data_buf) {
			memcpy(cmd->data_buf, ob_frame->payload,
					sizeof(struct mvumi_sense_data));
		}
		atomic_dec(&cmd->sync_cmd);
		wake_up(&mhba->int_cmd_wait_q);
	}
}

static void mvumi_show_event(struct mvumi_hba *mhba,
			struct mvumi_driver_event *ptr)
{
	unsigned int i;

	dev_warn(&mhba->pdev->dev,
		"Event[0x%x] id[0x%x] severity[0x%x] device id[0x%x]\n",
		ptr->sequence_no, ptr->event_id, ptr->severity, ptr->device_id);
	if (ptr->param_count) {
		printk(KERN_WARNING "Event param(len 0x%x): ",
						ptr->param_count);
		for (i = 0; i < ptr->param_count; i++)
			printk(KERN_WARNING "0x%x ", ptr->params[i]);

		printk(KERN_WARNING "\n");
	}

	if (ptr->sense_data_length) {
		printk(KERN_WARNING "Event sense data(len 0x%x): ",
						ptr->sense_data_length);
		for (i = 0; i < ptr->sense_data_length; i++)
			printk(KERN_WARNING "0x%x ", ptr->sense_data[i]);
		printk(KERN_WARNING "\n");
	}
}

static void mvumi_notification(struct mvumi_hba *mhba, u8 msg, void *buffer)
{
	if (msg == APICDB1_EVENT_GETEVENT) {
		int i, count;
		struct mvumi_driver_event *param = NULL;
		struct mvumi_event_req *er = buffer;
		count = er->count;
		if (count > MAX_EVENTS_RETURNED) {
			dev_err(&mhba->pdev->dev, "event count[0x%x] is bigger"
					" than max event count[0x%x].\n",
					count, MAX_EVENTS_RETURNED);
			return;
		}
		for (i = 0; i < count; i++) {
			param = &er->events[i];
			mvumi_show_event(mhba, param);
		}
	}
}

static int mvumi_get_event(struct mvumi_hba *mhba, unsigned char msg)
{
	struct mvumi_cmd *cmd;
	struct mvumi_msg_frame *frame;

	cmd = mvumi_create_internal_cmd(mhba, 512);
	if (!cmd)
		return -1;
	cmd->scmd = NULL;
	cmd->cmd_status = REQ_STATUS_PENDING;
	atomic_set(&cmd->sync_cmd, 0);
	frame = cmd->frame;
	frame->device_id = 0;
	frame->cmd_flag = CMD_FLAG_DATA_IN;
	frame->req_function = CL_FUN_SCSI_CMD;
	frame->cdb_length = MAX_COMMAND_SIZE;
	frame->data_transfer_length = sizeof(struct mvumi_event_req);
	memset(frame->cdb, 0, MAX_COMMAND_SIZE);
	frame->cdb[0] = APICDB0_EVENT;
	frame->cdb[1] = msg;
	mvumi_issue_blocked_cmd(mhba, cmd);

	if (cmd->cmd_status != SAM_STAT_GOOD)
		dev_err(&mhba->pdev->dev, "get event failed, status=0x%x.\n",
							cmd->cmd_status);
	else
		mvumi_notification(mhba, cmd->frame->cdb[1], cmd->data_buf);

	mvumi_delete_internal_cmd(mhba, cmd);
	return 0;
}

static void mvumi_scan_events(struct work_struct *work)
{
	struct mvumi_events_wq *mu_ev =
		container_of(work, struct mvumi_events_wq, work_q);

	mvumi_get_event(mu_ev->mhba, mu_ev->event);
	kfree(mu_ev);
}

static void mvumi_launch_events(struct mvumi_hba *mhba, u8 msg)
{
	struct mvumi_events_wq *mu_ev;

	mu_ev = kzalloc(sizeof(*mu_ev), GFP_ATOMIC);
	if (mu_ev) {
		INIT_WORK(&mu_ev->work_q, mvumi_scan_events);
		mu_ev->mhba = mhba;
		mu_ev->event = msg;
		mu_ev->param = NULL;
		schedule_work(&mu_ev->work_q);
	}
}

static void mvumi_handle_clob(struct mvumi_hba *mhba)
{
	struct mvumi_rsp_frame *ob_frame;
	struct mvumi_cmd *cmd;
	struct mvumi_ob_data *pool;

	while (!list_empty(&mhba->free_ob_list)) {
		pool = list_first_entry(&mhba->free_ob_list,
						struct mvumi_ob_data, list);
		list_del_init(&pool->list);
		list_add_tail(&pool->list, &mhba->ob_data_list);

		ob_frame = (struct mvumi_rsp_frame *) &pool->data[0];
		cmd = mhba->tag_cmd[ob_frame->tag];

		atomic_dec(&mhba->fw_outstanding);
		mhba->tag_cmd[ob_frame->tag] = 0;
		tag_release_one(mhba, &mhba->tag_pool, ob_frame->tag);
		if (cmd->scmd)
			mvumi_complete_cmd(mhba, cmd, ob_frame);
		else
			mvumi_complete_internal_cmd(mhba, cmd, ob_frame);
	}
	mhba->instancet->fire_cmd(mhba, NULL);
}

static irqreturn_t mvumi_isr_handler(int irq, void *devp)
{
	struct mvumi_hba *mhba = (struct mvumi_hba *) devp;
	unsigned long flags;

	spin_lock_irqsave(mhba->shost->host_lock, flags);
	if (unlikely(mhba->instancet->clear_intr(mhba) || !mhba->global_isr)) {
		spin_unlock_irqrestore(mhba->shost->host_lock, flags);
		return IRQ_NONE;
	}

	if (mhba->global_isr & INT_MAP_DL_CPU2PCIEA) {
		if (mhba->isr_status & DRBL_HANDSHAKE_ISR) {
			dev_warn(&mhba->pdev->dev, "enter handshake again!\n");
			mvumi_handshake(mhba);
		}
		if (mhba->isr_status & DRBL_EVENT_NOTIFY)
			mvumi_launch_events(mhba, APICDB1_EVENT_GETEVENT);
	}

	if (mhba->global_isr & INT_MAP_COMAOUT)
		mvumi_receive_ob_list_entry(mhba);

	mhba->global_isr = 0;
	mhba->isr_status = 0;
	if (mhba->fw_state == FW_STATE_STARTED)
		mvumi_handle_clob(mhba);
	spin_unlock_irqrestore(mhba->shost->host_lock, flags);
	return IRQ_HANDLED;
}

static enum mvumi_qc_result mvumi_send_command(struct mvumi_hba *mhba,
						struct mvumi_cmd *cmd)
{
	void *ib_entry;
	struct mvumi_msg_frame *ib_frame;
	unsigned int frame_len;

	ib_frame = cmd->frame;
	if (unlikely(mhba->fw_state != FW_STATE_STARTED)) {
		dev_dbg(&mhba->pdev->dev, "firmware not ready.\n");
		return MV_QUEUE_COMMAND_RESULT_NO_RESOURCE;
	}
	if (tag_is_empty(&mhba->tag_pool)) {
		dev_dbg(&mhba->pdev->dev, "no free tag.\n");
		return MV_QUEUE_COMMAND_RESULT_NO_RESOURCE;
	}
	if (mvumi_get_ib_list_entry(mhba, &ib_entry))
		return MV_QUEUE_COMMAND_RESULT_NO_RESOURCE;

	cmd->frame->tag = tag_get_one(mhba, &mhba->tag_pool);
	cmd->frame->request_id = mhba->io_seq++;
	cmd->request_id = cmd->frame->request_id;
	mhba->tag_cmd[cmd->frame->tag] = cmd;
	frame_len = sizeof(*ib_frame) - 4 +
				ib_frame->sg_counts * sizeof(struct mvumi_sgl);
	memcpy(ib_entry, ib_frame, frame_len);
	return MV_QUEUE_COMMAND_RESULT_SENT;
}

static void mvumi_fire_cmd(struct mvumi_hba *mhba, struct mvumi_cmd *cmd)
{
	unsigned short num_of_cl_sent = 0;
	enum mvumi_qc_result result;

	if (cmd)
		list_add_tail(&cmd->queue_pointer, &mhba->waiting_req_list);

	while (!list_empty(&mhba->waiting_req_list)) {
		cmd = list_first_entry(&mhba->waiting_req_list,
					 struct mvumi_cmd, queue_pointer);
		list_del_init(&cmd->queue_pointer);
		result = mvumi_send_command(mhba, cmd);
		switch (result) {
		case MV_QUEUE_COMMAND_RESULT_SENT:
			num_of_cl_sent++;
			break;
		case MV_QUEUE_COMMAND_RESULT_NO_RESOURCE:
			list_add(&cmd->queue_pointer, &mhba->waiting_req_list);
			if (num_of_cl_sent > 0)
				mvumi_send_ib_list_entry(mhba);

			return;
		}
	}
	if (num_of_cl_sent > 0)
		mvumi_send_ib_list_entry(mhba);
}

/**
 * mvumi_enable_intr -	Enables interrupts
 * @regs:			FW register set
 */
static void mvumi_enable_intr(void *regs)
{
	unsigned int mask;

	iowrite32(0x3FFFFFFF, regs + CPU_ARM_TO_PCIEA_MASK_REG);
	mask = ioread32(regs + CPU_ENPOINTA_MASK_REG);
	mask |= INT_MAP_DL_CPU2PCIEA | INT_MAP_COMAOUT | INT_MAP_COMAERR;
	iowrite32(mask, regs + CPU_ENPOINTA_MASK_REG);
}

/**
 * mvumi_disable_intr -Disables interrupt
 * @regs:			FW register set
 */
static void mvumi_disable_intr(void *regs)
{
	unsigned int mask;

	iowrite32(0, regs + CPU_ARM_TO_PCIEA_MASK_REG);
	mask = ioread32(regs + CPU_ENPOINTA_MASK_REG);
	mask &= ~(INT_MAP_DL_CPU2PCIEA | INT_MAP_COMAOUT | INT_MAP_COMAERR);
	iowrite32(mask, regs + CPU_ENPOINTA_MASK_REG);
}

static int mvumi_clear_intr(void *extend)
{
	struct mvumi_hba *mhba = (struct mvumi_hba *) extend;
	unsigned int status, isr_status = 0, tmp = 0;
	void *regs = mhba->mmio;

	status = ioread32(regs + CPU_MAIN_INT_CAUSE_REG);
	if (!(status & INT_MAP_MU) || status == 0xFFFFFFFF)
		return 1;
	if (unlikely(status & INT_MAP_COMAERR)) {
		tmp = ioread32(regs + CLA_ISR_CAUSE);
		if (tmp & (CLIC_IN_ERR_IRQ | CLIC_OUT_ERR_IRQ))
			iowrite32(tmp & (CLIC_IN_ERR_IRQ | CLIC_OUT_ERR_IRQ),
					regs + CLA_ISR_CAUSE);
		status ^= INT_MAP_COMAERR;
		/* inbound or outbound parity error, command will timeout */
	}
	if (status & INT_MAP_COMAOUT) {
		tmp = ioread32(regs + CLA_ISR_CAUSE);
		if (tmp & CLIC_OUT_IRQ)
			iowrite32(tmp & CLIC_OUT_IRQ, regs + CLA_ISR_CAUSE);
	}
	if (status & INT_MAP_DL_CPU2PCIEA) {
		isr_status = ioread32(regs + CPU_ARM_TO_PCIEA_DRBL_REG);
		if (isr_status)
			iowrite32(isr_status, regs + CPU_ARM_TO_PCIEA_DRBL_REG);
	}

	mhba->global_isr = status;
	mhba->isr_status = isr_status;

	return 0;
}

/**
 * mvumi_read_fw_status_reg - returns the current FW status value
 * @regs:			FW register set
 */
static unsigned int mvumi_read_fw_status_reg(void *regs)
{
	unsigned int status;

	status = ioread32(regs + CPU_ARM_TO_PCIEA_DRBL_REG);
	if (status)
		iowrite32(status, regs + CPU_ARM_TO_PCIEA_DRBL_REG);
	return status;
}

static struct mvumi_instance_template mvumi_instance_template = {
	.fire_cmd = mvumi_fire_cmd,
	.enable_intr = mvumi_enable_intr,
	.disable_intr = mvumi_disable_intr,
	.clear_intr = mvumi_clear_intr,
	.read_fw_status_reg = mvumi_read_fw_status_reg,
};

static int mvumi_slave_configure(struct scsi_device *sdev)
{
	struct mvumi_hba *mhba;
	unsigned char bitcount = sizeof(unsigned char) * 8;

	mhba = (struct mvumi_hba *) sdev->host->hostdata;
	if (sdev->id >= mhba->max_target_id)
		return -EINVAL;

	mhba->target_map[sdev->id / bitcount] |= (1 << (sdev->id % bitcount));
	return 0;
}

/**
 * mvumi_build_frame -	Prepares a direct cdb (DCDB) command
 * @mhba:		Adapter soft state
 * @scmd:		SCSI command
 * @cmd:		Command to be prepared in
 *
 * This function prepares CDB commands. These are typcially pass-through
 * commands to the devices.
 */
static unsigned char mvumi_build_frame(struct mvumi_hba *mhba,
				struct scsi_cmnd *scmd, struct mvumi_cmd *cmd)
{
	struct mvumi_msg_frame *pframe;

	cmd->scmd = scmd;
	cmd->cmd_status = REQ_STATUS_PENDING;
	pframe = cmd->frame;
	pframe->device_id = ((unsigned short) scmd->device->id) |
				(((unsigned short) scmd->device->lun) << 8);
	pframe->cmd_flag = 0;

	switch (scmd->sc_data_direction) {
	case DMA_NONE:
		pframe->cmd_flag |= CMD_FLAG_NON_DATA;
		break;
	case DMA_FROM_DEVICE:
		pframe->cmd_flag |= CMD_FLAG_DATA_IN;
		break;
	case DMA_TO_DEVICE:
		pframe->cmd_flag |= CMD_FLAG_DATA_OUT;
		break;
	case DMA_BIDIRECTIONAL:
	default:
		dev_warn(&mhba->pdev->dev, "unexpected data direction[%d] "
			"cmd[0x%x]\n", scmd->sc_data_direction, scmd->cmnd[0]);
		goto error;
	}

	pframe->cdb_length = scmd->cmd_len;
	memcpy(pframe->cdb, scmd->cmnd, pframe->cdb_length);
	pframe->req_function = CL_FUN_SCSI_CMD;
	if (scsi_bufflen(scmd)) {
		if (mvumi_make_sgl(mhba, scmd, &pframe->payload[0],
			&pframe->sg_counts))
			goto error;

		pframe->data_transfer_length = scsi_bufflen(scmd);
	} else {
		pframe->sg_counts = 0;
		pframe->data_transfer_length = 0;
	}
	return 0;

error:
	scmd->result = (DID_OK << 16) | (DRIVER_SENSE << 24) |
		SAM_STAT_CHECK_CONDITION;
	scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST, 0x24,
									0);
	return -1;
}

/**
 * mvumi_queue_command -	Queue entry point
 * @scmd:			SCSI command to be queued
 * @done:			Callback entry point
 */
static int mvumi_queue_command(struct Scsi_Host *shost,
					struct scsi_cmnd *scmd)
{
	struct mvumi_cmd *cmd;
	struct mvumi_hba *mhba;
	unsigned long irq_flags;

	spin_lock_irqsave(shost->host_lock, irq_flags);
	scsi_cmd_get_serial(shost, scmd);

	mhba = (struct mvumi_hba *) shost->hostdata;
	scmd->result = 0;
	cmd = mvumi_get_cmd(mhba);
	if (unlikely(!cmd)) {
		spin_unlock_irqrestore(shost->host_lock, irq_flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	if (unlikely(mvumi_build_frame(mhba, scmd, cmd)))
		goto out_return_cmd;

	cmd->scmd = scmd;
	scmd->SCp.ptr = (char *) cmd;
	mhba->instancet->fire_cmd(mhba, cmd);
	spin_unlock_irqrestore(shost->host_lock, irq_flags);
	return 0;

out_return_cmd:
	mvumi_return_cmd(mhba, cmd);
	scmd->scsi_done(scmd);
	spin_unlock_irqrestore(shost->host_lock, irq_flags);
	return 0;
}

static enum blk_eh_timer_return mvumi_timed_out(struct scsi_cmnd *scmd)
{
	struct mvumi_cmd *cmd = (struct mvumi_cmd *) scmd->SCp.ptr;
	struct Scsi_Host *host = scmd->device->host;
	struct mvumi_hba *mhba = shost_priv(host);
	unsigned long flags;

	spin_lock_irqsave(mhba->shost->host_lock, flags);

	if (mhba->tag_cmd[cmd->frame->tag]) {
		mhba->tag_cmd[cmd->frame->tag] = 0;
		tag_release_one(mhba, &mhba->tag_pool, cmd->frame->tag);
	}
	if (!list_empty(&cmd->queue_pointer))
		list_del_init(&cmd->queue_pointer);
	else
		atomic_dec(&mhba->fw_outstanding);

	scmd->result = (DRIVER_INVALID << 24) | (DID_ABORT << 16);
	scmd->SCp.ptr = NULL;
	if (scsi_bufflen(scmd)) {
		if (scsi_sg_count(scmd)) {
			pci_unmap_sg(mhba->pdev,
				scsi_sglist(scmd),
				scsi_sg_count(scmd),
				(int)scmd->sc_data_direction);
		} else {
			pci_unmap_single(mhba->pdev,
				scmd->SCp.dma_handle,
				scsi_bufflen(scmd),
				(int)scmd->sc_data_direction);

			scmd->SCp.dma_handle = 0;
		}
	}
	mvumi_return_cmd(mhba, cmd);
	spin_unlock_irqrestore(mhba->shost->host_lock, flags);

	return BLK_EH_NOT_HANDLED;
}

static int
mvumi_bios_param(struct scsi_device *sdev, struct block_device *bdev,
			sector_t capacity, int geom[])
{
	int heads, sectors;
	sector_t cylinders;
	unsigned long tmp;

	heads = 64;
	sectors = 32;
	tmp = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders, tmp);

	if (capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		tmp = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders, tmp);
	}
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
}

static struct scsi_host_template mvumi_template = {

	.module = THIS_MODULE,
	.name = "Marvell Storage Controller",
	.slave_configure = mvumi_slave_configure,
	.queuecommand = mvumi_queue_command,
	.eh_host_reset_handler = mvumi_host_reset,
	.bios_param = mvumi_bios_param,
	.this_id = -1,
};

static struct scsi_transport_template mvumi_transport_template = {
	.eh_timed_out = mvumi_timed_out,
};

/**
 * mvumi_init_fw -	Initializes the FW
 * @mhba:		Adapter soft state
 *
 * This is the main function for initializing firmware.
 */
static int mvumi_init_fw(struct mvumi_hba *mhba)
{
	int ret = 0;

	if (pci_request_regions(mhba->pdev, MV_DRIVER_NAME)) {
		dev_err(&mhba->pdev->dev, "IO memory region busy!\n");
		return -EBUSY;
	}
	ret = mvumi_map_pci_addr(mhba->pdev, mhba->base_addr);
	if (ret)
		goto fail_ioremap;

	mhba->mmio = mhba->base_addr[0];

	switch (mhba->pdev->device) {
	case PCI_DEVICE_ID_MARVELL_MV9143:
		mhba->instancet = &mvumi_instance_template;
		mhba->io_seq = 0;
		mhba->max_sge = MVUMI_MAX_SG_ENTRY;
		mhba->request_id_enabled = 1;
		break;
	default:
		dev_err(&mhba->pdev->dev, "device 0x%x not supported!\n",
							mhba->pdev->device);
		mhba->instancet = NULL;
		ret = -EINVAL;
		goto fail_alloc_mem;
	}
	dev_dbg(&mhba->pdev->dev, "device id : %04X is found.\n",
							mhba->pdev->device);

	mhba->handshake_page = kzalloc(HSP_MAX_SIZE, GFP_KERNEL);
	if (!mhba->handshake_page) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for handshake\n");
		ret = -ENOMEM;
		goto fail_alloc_mem;
	}
	mhba->handshake_page_phys = virt_to_phys(mhba->handshake_page);

	if (mvumi_start(mhba)) {
		ret = -EINVAL;
		goto fail_ready_state;
	}
	ret = mvumi_alloc_cmds(mhba);
	if (ret)
		goto fail_ready_state;

	return 0;

fail_ready_state:
	mvumi_release_mem_resource(mhba);
	kfree(mhba->handshake_page);
fail_alloc_mem:
	mvumi_unmap_pci_addr(mhba->pdev, mhba->base_addr);
fail_ioremap:
	pci_release_regions(mhba->pdev);

	return ret;
}

/**
 * mvumi_io_attach -	Attaches this driver to SCSI mid-layer
 * @mhba:		Adapter soft state
 */
static int mvumi_io_attach(struct mvumi_hba *mhba)
{
	struct Scsi_Host *host = mhba->shost;
	int ret;
	unsigned int max_sg = (mhba->ib_max_size + 4 -
		sizeof(struct mvumi_msg_frame)) / sizeof(struct mvumi_sgl);

	host->irq = mhba->pdev->irq;
	host->unique_id = mhba->unique_id;
	host->can_queue = (mhba->max_io - 1) ? (mhba->max_io - 1) : 1;
	host->sg_tablesize = mhba->max_sge > max_sg ? max_sg : mhba->max_sge;
	host->max_sectors = mhba->max_transfer_size / 512;
	host->cmd_per_lun =  (mhba->max_io - 1) ? (mhba->max_io - 1) : 1;
	host->max_id = mhba->max_target_id;
	host->max_cmd_len = MAX_COMMAND_SIZE;
	host->transportt = &mvumi_transport_template;

	ret = scsi_add_host(host, &mhba->pdev->dev);
	if (ret) {
		dev_err(&mhba->pdev->dev, "scsi_add_host failed\n");
		return ret;
	}
	mhba->fw_flag |= MVUMI_FW_ATTACH;
	scsi_scan_host(host);

	return 0;
}

/**
 * mvumi_probe_one -	PCI hotplug entry point
 * @pdev:		PCI device structure
 * @id:			PCI ids of supported hotplugged adapter
 */
static int __devinit mvumi_probe_one(struct pci_dev *pdev,
					const struct pci_device_id *id)
{
	struct Scsi_Host *host;
	struct mvumi_hba *mhba;
	int ret;

	dev_dbg(&pdev->dev, " %#4.04x:%#4.04x:%#4.04x:%#4.04x: ",
			pdev->vendor, pdev->device, pdev->subsystem_vendor,
			pdev->subsystem_device);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	if (IS_DMA64) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
		if (ret) {
			ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
			if (ret)
				goto fail_set_dma_mask;
		}
	} else {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret)
			goto fail_set_dma_mask;
	}

	host = scsi_host_alloc(&mvumi_template, sizeof(*mhba));
	if (!host) {
		dev_err(&pdev->dev, "scsi_host_alloc failed\n");
		ret = -ENOMEM;
		goto fail_alloc_instance;
	}
	mhba = shost_priv(host);

	INIT_LIST_HEAD(&mhba->cmd_pool);
	INIT_LIST_HEAD(&mhba->ob_data_list);
	INIT_LIST_HEAD(&mhba->free_ob_list);
	INIT_LIST_HEAD(&mhba->res_list);
	INIT_LIST_HEAD(&mhba->waiting_req_list);
	atomic_set(&mhba->fw_outstanding, 0);
	init_waitqueue_head(&mhba->int_cmd_wait_q);

	mhba->pdev = pdev;
	mhba->shost = host;
	mhba->unique_id = pdev->bus->number << 8 | pdev->devfn;

	ret = mvumi_init_fw(mhba);
	if (ret)
		goto fail_init_fw;

	ret = request_irq(mhba->pdev->irq, mvumi_isr_handler, IRQF_SHARED,
				"mvumi", mhba);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IRQ\n");
		goto fail_init_irq;
	}
	mhba->instancet->enable_intr(mhba->mmio);
	pci_set_drvdata(pdev, mhba);

	ret = mvumi_io_attach(mhba);
	if (ret)
		goto fail_io_attach;
	dev_dbg(&pdev->dev, "probe mvumi driver successfully.\n");

	return 0;

fail_io_attach:
	pci_set_drvdata(pdev, NULL);
	mhba->instancet->disable_intr(mhba->mmio);
	free_irq(mhba->pdev->irq, mhba);
fail_init_irq:
	mvumi_release_fw(mhba);
fail_init_fw:
	scsi_host_put(host);

fail_alloc_instance:
fail_set_dma_mask:
	pci_disable_device(pdev);

	return ret;
}

static void mvumi_detach_one(struct pci_dev *pdev)
{
	struct Scsi_Host *host;
	struct mvumi_hba *mhba;

	mhba = pci_get_drvdata(pdev);
	host = mhba->shost;
	scsi_remove_host(mhba->shost);
	mvumi_flush_cache(mhba);

	mhba->instancet->disable_intr(mhba->mmio);
	free_irq(mhba->pdev->irq, mhba);
	mvumi_release_fw(mhba);
	scsi_host_put(host);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	dev_dbg(&pdev->dev, "driver is removed!\n");
}

/**
 * mvumi_shutdown -	Shutdown entry point
 * @device:		Generic device structure
 */
static void mvumi_shutdown(struct pci_dev *pdev)
{
	struct mvumi_hba *mhba = pci_get_drvdata(pdev);

	mvumi_flush_cache(mhba);
}

static int mvumi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mvumi_hba *mhba = NULL;

	mhba = pci_get_drvdata(pdev);
	mvumi_flush_cache(mhba);

	pci_set_drvdata(pdev, mhba);
	mhba->instancet->disable_intr(mhba->mmio);
	free_irq(mhba->pdev->irq, mhba);
	mvumi_unmap_pci_addr(pdev, mhba->base_addr);
	pci_release_regions(pdev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int mvumi_resume(struct pci_dev *pdev)
{
	int ret;
	struct mvumi_hba *mhba = NULL;

	mhba = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "enable device failed\n");
		return ret;
	}
	pci_set_master(pdev);
	if (IS_DMA64) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
		if (ret) {
			ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
			if (ret)
				goto fail;
		}
	} else {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret)
			goto fail;
	}
	ret = pci_request_regions(mhba->pdev, MV_DRIVER_NAME);
	if (ret)
		goto fail;
	ret = mvumi_map_pci_addr(mhba->pdev, mhba->base_addr);
	if (ret)
		goto release_regions;

	mhba->mmio = mhba->base_addr[0];
	mvumi_reset(mhba->mmio);

	if (mvumi_start(mhba)) {
		ret = -EINVAL;
		goto unmap_pci_addr;
	}

	ret = request_irq(mhba->pdev->irq, mvumi_isr_handler, IRQF_SHARED,
				"mvumi", mhba);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IRQ\n");
		goto unmap_pci_addr;
	}
	mhba->instancet->enable_intr(mhba->mmio);

	return 0;

unmap_pci_addr:
	mvumi_unmap_pci_addr(pdev, mhba->base_addr);
release_regions:
	pci_release_regions(pdev);
fail:
	pci_disable_device(pdev);

	return ret;
}

static struct pci_driver mvumi_pci_driver = {

	.name = MV_DRIVER_NAME,
	.id_table = mvumi_pci_table,
	.probe = mvumi_probe_one,
	.remove = __devexit_p(mvumi_detach_one),
	.shutdown = mvumi_shutdown,
#ifdef CONFIG_PM
	.suspend = mvumi_suspend,
	.resume = mvumi_resume,
#endif
};

/**
 * mvumi_init - Driver load entry point
 */
static int __init mvumi_init(void)
{
	return pci_register_driver(&mvumi_pci_driver);
}

/**
 * mvumi_exit - Driver unload entry point
 */
static void __exit mvumi_exit(void)
{

	pci_unregister_driver(&mvumi_pci_driver);
}

module_init(mvumi_init);
module_exit(mvumi_exit);
