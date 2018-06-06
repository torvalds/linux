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
#include <linux/ktime.h>
#include <linux/blkdev.h>
#include <linux/io.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_eh.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>

#include "mvumi.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jyli@marvell.com");
MODULE_DESCRIPTION("Marvell UMI Driver");

static const struct pci_device_id mvumi_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_EXT, PCI_DEVICE_ID_MARVELL_MV9143) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL_EXT, PCI_DEVICE_ID_MARVELL_MV9580) },
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
	struct mvumi_res *res = kzalloc(sizeof(*res), GFP_ATOMIC);

	if (!res) {
		dev_err(&mhba->pdev->dev,
			"Failed to allocate memory for resource manager.\n");
		return NULL;
	}

	switch (type) {
	case RESOURCE_CACHED_MEMORY:
		res->virt_addr = kzalloc(size, GFP_ATOMIC);
		if (!res->virt_addr) {
			dev_err(&mhba->pdev->dev,
				"unable to allocate memory,size = %d.\n", size);
			kfree(res);
			return NULL;
		}
		break;

	case RESOURCE_UNCACHED_MEMORY:
		size = round_up(size, 8);
		res->virt_addr = pci_zalloc_consistent(mhba->pdev, size,
						       &res->bus_addr);
		if (!res->virt_addr) {
			dev_err(&mhba->pdev->dev,
					"unable to allocate consistent mem,"
							"size = %d.\n", size);
			kfree(res);
			return NULL;
		}
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

	sg = scsi_sglist(scmd);
	*sg_count = pci_map_sg(mhba->pdev, sg, sgnum,
			       (int) scmd->sc_data_direction);
	if (*sg_count > mhba->max_sge) {
		dev_err(&mhba->pdev->dev,
			"sg count[0x%x] is bigger than max sg[0x%x].\n",
			*sg_count, mhba->max_sge);
		pci_unmap_sg(mhba->pdev, sg, sgnum,
			     (int) scmd->sc_data_direction);
		return -1;
	}
	for (i = 0; i < *sg_count; i++) {
		busaddr = sg_dma_address(&sg[i]);
		m_sg->baseaddr_l = cpu_to_le32(lower_32_bits(busaddr));
		m_sg->baseaddr_h = cpu_to_le32(upper_32_bits(busaddr));
		m_sg->flags = 0;
		sgd_setsz(mhba, m_sg, cpu_to_le32(sg_dma_len(&sg[i])));
		if ((i + 1) == *sg_count)
			m_sg->flags |= 1U << mhba->eot_flag;

		sgd_inc(mhba, m_sg);
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

	virt_addr = pci_zalloc_consistent(mhba->pdev, size, &phy_addr);
	if (!virt_addr)
		return -1;

	m_sg = (struct mvumi_sgl *) &cmd->frame->payload[0];
	cmd->frame->sg_counts = 1;
	cmd->data_buf = virt_addr;

	m_sg->baseaddr_l = cpu_to_le32(lower_32_bits(phy_addr));
	m_sg->baseaddr_h = cpu_to_le32(upper_32_bits(phy_addr));
	m_sg->flags = 1U << mhba->eot_flag;
	sgd_setsz(mhba, m_sg, cpu_to_le32(size));

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

	cmd->frame = pci_alloc_consistent(mhba->pdev,
				mhba->ib_max_size, &cmd->frame_phys);
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
			pci_free_consistent(mhba->pdev, mhba->ib_max_size,
					cmd->frame, cmd->frame_phys);
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
			sgd_getsz(mhba, m_sg, size);

			phy_addr = (dma_addr_t) m_sg->baseaddr_l |
				(dma_addr_t) ((m_sg->baseaddr_h << 16) << 16);

			pci_free_consistent(mhba->pdev, size, cmd->data_buf,
								phy_addr);
		}
		pci_free_consistent(mhba->pdev, mhba->ib_max_size,
				cmd->frame, cmd->frame_phys);
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
		if (!(mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC))
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
		if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC) {
			cmd->frame = mhba->ib_frame + i * mhba->ib_max_size;
			cmd->frame_phys = mhba->ib_frame_phys
						+ i * mhba->ib_max_size;
		} else
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
		if (!(mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC))
			kfree(cmd->frame);
		kfree(cmd);
	}
	return -ENOMEM;
}

static unsigned int mvumi_check_ib_list_9143(struct mvumi_hba *mhba)
{
	unsigned int ib_rp_reg;
	struct mvumi_hw_regs *regs = mhba->regs;

	ib_rp_reg = ioread32(mhba->regs->inb_read_pointer);

	if (unlikely(((ib_rp_reg & regs->cl_slot_num_mask) ==
			(mhba->ib_cur_slot & regs->cl_slot_num_mask)) &&
			((ib_rp_reg & regs->cl_pointer_toggle)
			 != (mhba->ib_cur_slot & regs->cl_pointer_toggle)))) {
		dev_warn(&mhba->pdev->dev, "no free slot to use.\n");
		return 0;
	}
	if (atomic_read(&mhba->fw_outstanding) >= mhba->max_io) {
		dev_warn(&mhba->pdev->dev, "firmware io overflow.\n");
		return 0;
	} else {
		return mhba->max_io - atomic_read(&mhba->fw_outstanding);
	}
}

static unsigned int mvumi_check_ib_list_9580(struct mvumi_hba *mhba)
{
	unsigned int count;
	if (atomic_read(&mhba->fw_outstanding) >= (mhba->max_io - 1))
		return 0;
	count = ioread32(mhba->ib_shadow);
	if (count == 0xffff)
		return 0;
	return count;
}

static void mvumi_get_ib_list_entry(struct mvumi_hba *mhba, void **ib_entry)
{
	unsigned int cur_ib_entry;

	cur_ib_entry = mhba->ib_cur_slot & mhba->regs->cl_slot_num_mask;
	cur_ib_entry++;
	if (cur_ib_entry >= mhba->list_num_io) {
		cur_ib_entry -= mhba->list_num_io;
		mhba->ib_cur_slot ^= mhba->regs->cl_pointer_toggle;
	}
	mhba->ib_cur_slot &= ~mhba->regs->cl_slot_num_mask;
	mhba->ib_cur_slot |= (cur_ib_entry & mhba->regs->cl_slot_num_mask);
	if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC) {
		*ib_entry = mhba->ib_list + cur_ib_entry *
				sizeof(struct mvumi_dyn_list_entry);
	} else {
		*ib_entry = mhba->ib_list + cur_ib_entry * mhba->ib_max_size;
	}
	atomic_inc(&mhba->fw_outstanding);
}

static void mvumi_send_ib_list_entry(struct mvumi_hba *mhba)
{
	iowrite32(0xffff, mhba->ib_shadow);
	iowrite32(mhba->ib_cur_slot, mhba->regs->inb_write_pointer);
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

static int mvumi_check_ob_list_9143(struct mvumi_hba *mhba,
			unsigned int *cur_obf, unsigned int *assign_obf_end)
{
	unsigned int ob_write, ob_write_shadow;
	struct mvumi_hw_regs *regs = mhba->regs;

	do {
		ob_write = ioread32(regs->outb_copy_pointer);
		ob_write_shadow = ioread32(mhba->ob_shadow);
	} while ((ob_write & regs->cl_slot_num_mask) != ob_write_shadow);

	*cur_obf = mhba->ob_cur_slot & mhba->regs->cl_slot_num_mask;
	*assign_obf_end = ob_write & mhba->regs->cl_slot_num_mask;

	if ((ob_write & regs->cl_pointer_toggle) !=
			(mhba->ob_cur_slot & regs->cl_pointer_toggle)) {
		*assign_obf_end += mhba->list_num_io;
	}
	return 0;
}

static int mvumi_check_ob_list_9580(struct mvumi_hba *mhba,
			unsigned int *cur_obf, unsigned int *assign_obf_end)
{
	unsigned int ob_write;
	struct mvumi_hw_regs *regs = mhba->regs;

	ob_write = ioread32(regs->outb_read_pointer);
	ob_write = ioread32(regs->outb_copy_pointer);
	*cur_obf = mhba->ob_cur_slot & mhba->regs->cl_slot_num_mask;
	*assign_obf_end = ob_write & mhba->regs->cl_slot_num_mask;
	if (*assign_obf_end < *cur_obf)
		*assign_obf_end += mhba->list_num_io;
	else if (*assign_obf_end == *cur_obf)
		return -1;
	return 0;
}

static void mvumi_receive_ob_list_entry(struct mvumi_hba *mhba)
{
	unsigned int cur_obf, assign_obf_end, i;
	struct mvumi_ob_data *ob_data;
	struct mvumi_rsp_frame *p_outb_frame;
	struct mvumi_hw_regs *regs = mhba->regs;

	if (mhba->instancet->check_ob_list(mhba, &cur_obf, &assign_obf_end))
		return;

	for (i = (assign_obf_end - cur_obf); i != 0; i--) {
		cur_obf++;
		if (cur_obf >= mhba->list_num_io) {
			cur_obf -= mhba->list_num_io;
			mhba->ob_cur_slot ^= regs->cl_pointer_toggle;
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
				mhba->ob_cur_slot ^= regs->cl_pointer_toggle;
			} else
				cur_obf -= 1;
			break;
		}

		memcpy(ob_data->data, p_outb_frame, mhba->ob_max_size);
		p_outb_frame->tag = 0xff;

		list_add_tail(&ob_data->list, &mhba->free_ob_list);
	}
	mhba->ob_cur_slot &= ~regs->cl_slot_num_mask;
	mhba->ob_cur_slot |= (cur_obf & regs->cl_slot_num_mask);
	iowrite32(mhba->ob_cur_slot, regs->outb_read_pointer);
}

static void mvumi_reset(struct mvumi_hba *mhba)
{
	struct mvumi_hw_regs *regs = mhba->regs;

	iowrite32(0, regs->enpointa_mask_reg);
	if (ioread32(regs->arm_to_pciea_msg1) != HANDSHAKE_DONESTATE)
		return;

	iowrite32(DRBL_SOFT_RESET, regs->pciea_to_arm_drbl_reg);
}

static unsigned char mvumi_start(struct mvumi_hba *mhba);

static int mvumi_wait_for_outstanding(struct mvumi_hba *mhba)
{
	mhba->fw_state = FW_STATE_ABORT;
	mvumi_reset(mhba);

	if (mvumi_start(mhba))
		return FAILED;
	else
		return SUCCESS;
}

static int mvumi_wait_for_fw(struct mvumi_hba *mhba)
{
	struct mvumi_hw_regs *regs = mhba->regs;
	u32 tmp;
	unsigned long before;
	before = jiffies;

	iowrite32(0, regs->enpointa_mask_reg);
	tmp = ioread32(regs->arm_to_pciea_msg1);
	while (tmp != HANDSHAKE_READYSTATE) {
		iowrite32(DRBL_MU_RESET, regs->pciea_to_arm_drbl_reg);
		if (time_after(jiffies, before + FW_MAX_DELAY * HZ)) {
			dev_err(&mhba->pdev->dev,
				"FW reset failed [0x%x].\n", tmp);
			return FAILED;
		}

		msleep(500);
		rmb();
		tmp = ioread32(regs->arm_to_pciea_msg1);
	}

	return SUCCESS;
}

static void mvumi_backup_bar_addr(struct mvumi_hba *mhba)
{
	unsigned char i;

	for (i = 0; i < MAX_BASE_ADDRESS; i++) {
		pci_read_config_dword(mhba->pdev, 0x10 + i * 4,
						&mhba->pci_base[i]);
	}
}

static void mvumi_restore_bar_addr(struct mvumi_hba *mhba)
{
	unsigned char i;

	for (i = 0; i < MAX_BASE_ADDRESS; i++) {
		if (mhba->pci_base[i])
			pci_write_config_dword(mhba->pdev, 0x10 + i * 4,
						mhba->pci_base[i]);
	}
}

static unsigned int mvumi_pci_set_master(struct pci_dev *pdev)
{
	unsigned int ret = 0;
	pci_set_master(pdev);

	if (IS_DMA64) {
		if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
			ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	} else
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));

	return ret;
}

static int mvumi_reset_host_9580(struct mvumi_hba *mhba)
{
	mhba->fw_state = FW_STATE_ABORT;

	iowrite32(0, mhba->regs->reset_enable);
	iowrite32(0xf, mhba->regs->reset_request);

	iowrite32(0x10, mhba->regs->reset_enable);
	iowrite32(0x10, mhba->regs->reset_request);
	msleep(100);
	pci_disable_device(mhba->pdev);

	if (pci_enable_device(mhba->pdev)) {
		dev_err(&mhba->pdev->dev, "enable device failed\n");
		return FAILED;
	}
	if (mvumi_pci_set_master(mhba->pdev)) {
		dev_err(&mhba->pdev->dev, "set master failed\n");
		return FAILED;
	}
	mvumi_restore_bar_addr(mhba);
	if (mvumi_wait_for_fw(mhba) == FAILED)
		return FAILED;

	return mvumi_wait_for_outstanding(mhba);
}

static int mvumi_reset_host_9143(struct mvumi_hba *mhba)
{
	return mvumi_wait_for_outstanding(mhba);
}

static int mvumi_host_reset(struct scsi_cmnd *scmd)
{
	struct mvumi_hba *mhba;

	mhba = (struct mvumi_hba *) scmd->device->host->hostdata;

	scmd_printk(KERN_NOTICE, scmd, "RESET -%ld cmd=%x retries=%x\n",
			scmd->serial_number, scmd->cmnd[0], scmd->retries);

	return mhba->instancet->reset_host(mhba);
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
	pci_free_consistent(mhba->pdev, HSP_MAX_SIZE,
		mhba->handshake_page, mhba->handshake_page_phys);
	kfree(mhba->regs);
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
		frame->cdb[1] = CDB_CORE_MODULE;
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

static void mvumi_hs_build_page(struct mvumi_hba *mhba,
				struct mvumi_hs_header *hs_header)
{
	struct mvumi_hs_page2 *hs_page2;
	struct mvumi_hs_page4 *hs_page4;
	struct mvumi_hs_page3 *hs_page3;
	u64 time;
	u64 local_time;

	switch (hs_header->page_code) {
	case HS_PAGE_HOST_INFO:
		hs_page2 = (struct mvumi_hs_page2 *) hs_header;
		hs_header->frame_length = sizeof(*hs_page2) - 4;
		memset(hs_header->frame_content, 0, hs_header->frame_length);
		hs_page2->host_type = 3; /* 3 mean linux*/
		if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC)
			hs_page2->host_cap = 0x08;/* host dynamic source mode */
		hs_page2->host_ver.ver_major = VER_MAJOR;
		hs_page2->host_ver.ver_minor = VER_MINOR;
		hs_page2->host_ver.ver_oem = VER_OEM;
		hs_page2->host_ver.ver_build = VER_BUILD;
		hs_page2->system_io_bus = 0;
		hs_page2->slot_number = 0;
		hs_page2->intr_level = 0;
		hs_page2->intr_vector = 0;
		time = ktime_get_real_seconds();
		local_time = (time - (sys_tz.tz_minuteswest * 60));
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
		if (mhba->hba_capability
			& HS_CAPABILITY_NEW_PAGE_IO_DEPTH_DEF) {
			hs_page4->ob_depth = find_first_bit((unsigned long *)
							    &mhba->list_num_io,
							    BITS_PER_LONG);
			hs_page4->ib_depth = find_first_bit((unsigned long *)
							    &mhba->list_num_io,
							    BITS_PER_LONG);
		} else {
			hs_page4->ob_depth = (u8) mhba->list_num_io;
			hs_page4->ib_depth = (u8) mhba->list_num_io;
		}
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
	if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC)
		tmp_size += sizeof(struct mvumi_dyn_list_entry) * mhba->max_io;

	tmp_size += 128 + mhba->ob_max_size * mhba->max_io;
	tmp_size += 8 + sizeof(u32)*2 + 16;

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
	if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC) {
		v += sizeof(struct mvumi_dyn_list_entry) * mhba->max_io;
		p += sizeof(struct mvumi_dyn_list_entry) * mhba->max_io;
		mhba->ib_frame = v;
		mhba->ib_frame_phys = p;
	}
	v += mhba->ib_max_size * mhba->max_io;
	p += mhba->ib_max_size * mhba->max_io;

	/* ib shadow */
	offset = round_up(p, 8) - p;
	p += offset;
	v += offset;
	mhba->ib_shadow = v;
	mhba->ib_shadow_phys = p;
	p += sizeof(u32)*2;
	v += sizeof(u32)*2;
	/* ob shadow */
	if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9580) {
		offset = round_up(p, 8) - p;
		p += offset;
		v += offset;
		mhba->ob_shadow = v;
		mhba->ob_shadow_phys = p;
		p += 8;
		v += 8;
	} else {
		offset = round_up(p, 4) - p;
		p += offset;
		v += offset;
		mhba->ob_shadow = v;
		mhba->ob_shadow_phys = p;
		p += 4;
		v += 4;
	}

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

		if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_COMPACT_SG)
			mhba->eot_flag = 22;
		else
			mhba->eot_flag = 27;
		if (mhba->hba_capability & HS_CAPABILITY_NEW_PAGE_IO_DEPTH_DEF)
			mhba->list_num_io = 1 << hs_page1->cl_inout_list_depth;
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
	struct mvumi_hw_regs *regs = mhba->regs;

	if (mhba->fw_state == FW_STATE_STARTING)
		hs_state = HS_S_START;
	else {
		tmp = ioread32(regs->arm_to_pciea_msg0);
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
		iowrite32(HANDSHAKE_SIGNATURE, regs->pciea_to_arm_msg1);
		iowrite32(hs_fun, regs->pciea_to_arm_msg0);
		iowrite32(DRBL_HANDSHAKE, regs->pciea_to_arm_drbl_reg);
		break;

	case HS_S_RESET:
		iowrite32(lower_32_bits(mhba->handshake_page_phys),
					regs->pciea_to_arm_msg1);
		iowrite32(upper_32_bits(mhba->handshake_page_phys),
					regs->arm_to_pciea_msg1);
		HS_SET_STATUS(hs_fun, HS_STATUS_OK);
		HS_SET_STATE(hs_fun, HS_S_PAGE_ADDR);
		iowrite32(hs_fun, regs->pciea_to_arm_msg0);
		iowrite32(DRBL_HANDSHAKE, regs->pciea_to_arm_drbl_reg);
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
		iowrite32(hs_fun, regs->pciea_to_arm_msg0);
		iowrite32(DRBL_HANDSHAKE, regs->pciea_to_arm_drbl_reg);
		break;

	case HS_S_END:
		/* Set communication list ISR */
		tmp = ioread32(regs->enpointa_mask_reg);
		tmp |= regs->int_comaout | regs->int_comaerr;
		iowrite32(tmp, regs->enpointa_mask_reg);
		iowrite32(mhba->list_num_io, mhba->ib_shadow);
		/* Set InBound List Available count shadow */
		iowrite32(lower_32_bits(mhba->ib_shadow_phys),
					regs->inb_aval_count_basel);
		iowrite32(upper_32_bits(mhba->ib_shadow_phys),
					regs->inb_aval_count_baseh);

		if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9143) {
			/* Set OutBound List Available count shadow */
			iowrite32((mhba->list_num_io-1) |
							regs->cl_pointer_toggle,
							mhba->ob_shadow);
			iowrite32(lower_32_bits(mhba->ob_shadow_phys),
							regs->outb_copy_basel);
			iowrite32(upper_32_bits(mhba->ob_shadow_phys),
							regs->outb_copy_baseh);
		}

		mhba->ib_cur_slot = (mhba->list_num_io - 1) |
							regs->cl_pointer_toggle;
		mhba->ob_cur_slot = (mhba->list_num_io - 1) |
							regs->cl_pointer_toggle;
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
		isr_status = mhba->instancet->read_fw_status_reg(mhba);

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
	unsigned int tmp;
	unsigned long before;

	before = jiffies;
	tmp = ioread32(mhba->regs->arm_to_pciea_msg1);
	while ((tmp != HANDSHAKE_READYSTATE) && (tmp != HANDSHAKE_DONESTATE)) {
		if (tmp != HANDSHAKE_READYSTATE)
			iowrite32(DRBL_MU_RESET,
					mhba->regs->pciea_to_arm_drbl_reg);
		if (time_after(jiffies, before + FW_MAX_DELAY * HZ)) {
			dev_err(&mhba->pdev->dev,
				"invalid signature [0x%x].\n", tmp);
			return -1;
		}
		usleep_range(1000, 2000);
		rmb();
		tmp = ioread32(mhba->regs->arm_to_pciea_msg1);
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
	unsigned int tmp;
	struct mvumi_hw_regs *regs = mhba->regs;

	/* clear Door bell */
	tmp = ioread32(regs->arm_to_pciea_drbl_reg);
	iowrite32(tmp, regs->arm_to_pciea_drbl_reg);

	iowrite32(regs->int_drbl_int_mask, regs->arm_to_pciea_mask_reg);
	tmp = ioread32(regs->enpointa_mask_reg) | regs->int_dl_cpu2pciea;
	iowrite32(tmp, regs->enpointa_mask_reg);
	msleep(100);
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

	if (scsi_bufflen(scmd))
		pci_unmap_sg(mhba->pdev, scsi_sglist(scmd),
			     scsi_sg_count(scmd),
			     (int) scmd->sc_data_direction);
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

static int mvumi_handle_hotplug(struct mvumi_hba *mhba, u16 devid, int status)
{
	struct scsi_device *sdev;
	int ret = -1;

	if (status == DEVICE_OFFLINE) {
		sdev = scsi_device_lookup(mhba->shost, 0, devid, 0);
		if (sdev) {
			dev_dbg(&mhba->pdev->dev, "remove disk %d-%d-%d.\n", 0,
								sdev->id, 0);
			scsi_remove_device(sdev);
			scsi_device_put(sdev);
			ret = 0;
		} else
			dev_err(&mhba->pdev->dev, " no disk[%d] to remove\n",
									devid);
	} else if (status == DEVICE_ONLINE) {
		sdev = scsi_device_lookup(mhba->shost, 0, devid, 0);
		if (!sdev) {
			scsi_add_device(mhba->shost, 0, devid, 0);
			dev_dbg(&mhba->pdev->dev, " add disk %d-%d-%d.\n", 0,
								devid, 0);
			ret = 0;
		} else {
			dev_err(&mhba->pdev->dev, " don't add disk %d-%d-%d.\n",
								0, devid, 0);
			scsi_device_put(sdev);
		}
	}
	return ret;
}

static u64 mvumi_inquiry(struct mvumi_hba *mhba,
	unsigned int id, struct mvumi_cmd *cmd)
{
	struct mvumi_msg_frame *frame;
	u64 wwid = 0;
	int cmd_alloc = 0;
	int data_buf_len = 64;

	if (!cmd) {
		cmd = mvumi_create_internal_cmd(mhba, data_buf_len);
		if (cmd)
			cmd_alloc = 1;
		else
			return 0;
	} else {
		memset(cmd->data_buf, 0, data_buf_len);
	}
	cmd->scmd = NULL;
	cmd->cmd_status = REQ_STATUS_PENDING;
	atomic_set(&cmd->sync_cmd, 0);
	frame = cmd->frame;
	frame->device_id = (u16) id;
	frame->cmd_flag = CMD_FLAG_DATA_IN;
	frame->req_function = CL_FUN_SCSI_CMD;
	frame->cdb_length = 6;
	frame->data_transfer_length = MVUMI_INQUIRY_LENGTH;
	memset(frame->cdb, 0, frame->cdb_length);
	frame->cdb[0] = INQUIRY;
	frame->cdb[4] = frame->data_transfer_length;

	mvumi_issue_blocked_cmd(mhba, cmd);

	if (cmd->cmd_status == SAM_STAT_GOOD) {
		if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9143)
			wwid = id + 1;
		else
			memcpy((void *)&wwid,
			       (cmd->data_buf + MVUMI_INQUIRY_UUID_OFF),
			       MVUMI_INQUIRY_UUID_LEN);
		dev_dbg(&mhba->pdev->dev,
			"inquiry device(0:%d:0) wwid(%llx)\n", id, wwid);
	} else {
		wwid = 0;
	}
	if (cmd_alloc)
		mvumi_delete_internal_cmd(mhba, cmd);

	return wwid;
}

static void mvumi_detach_devices(struct mvumi_hba *mhba)
{
	struct mvumi_device *mv_dev = NULL , *dev_next;
	struct scsi_device *sdev = NULL;

	mutex_lock(&mhba->device_lock);

	/* detach Hard Disk */
	list_for_each_entry_safe(mv_dev, dev_next,
		&mhba->shost_dev_list, list) {
		mvumi_handle_hotplug(mhba, mv_dev->id, DEVICE_OFFLINE);
		list_del_init(&mv_dev->list);
		dev_dbg(&mhba->pdev->dev, "release device(0:%d:0) wwid(%llx)\n",
			mv_dev->id, mv_dev->wwid);
		kfree(mv_dev);
	}
	list_for_each_entry_safe(mv_dev, dev_next, &mhba->mhba_dev_list, list) {
		list_del_init(&mv_dev->list);
		dev_dbg(&mhba->pdev->dev, "release device(0:%d:0) wwid(%llx)\n",
			mv_dev->id, mv_dev->wwid);
		kfree(mv_dev);
	}

	/* detach virtual device */
	if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9580)
		sdev = scsi_device_lookup(mhba->shost, 0,
						mhba->max_target_id - 1, 0);

	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}

	mutex_unlock(&mhba->device_lock);
}

static void mvumi_rescan_devices(struct mvumi_hba *mhba, int id)
{
	struct scsi_device *sdev;

	sdev = scsi_device_lookup(mhba->shost, 0, id, 0);
	if (sdev) {
		scsi_rescan_device(&sdev->sdev_gendev);
		scsi_device_put(sdev);
	}
}

static int mvumi_match_devices(struct mvumi_hba *mhba, int id, u64 wwid)
{
	struct mvumi_device *mv_dev = NULL;

	list_for_each_entry(mv_dev, &mhba->shost_dev_list, list) {
		if (mv_dev->wwid == wwid) {
			if (mv_dev->id != id) {
				dev_err(&mhba->pdev->dev,
					"%s has same wwid[%llx] ,"
					" but different id[%d %d]\n",
					__func__, mv_dev->wwid, mv_dev->id, id);
				return -1;
			} else {
				if (mhba->pdev->device ==
						PCI_DEVICE_ID_MARVELL_MV9143)
					mvumi_rescan_devices(mhba, id);
				return 1;
			}
		}
	}
	return 0;
}

static void mvumi_remove_devices(struct mvumi_hba *mhba, int id)
{
	struct mvumi_device *mv_dev = NULL, *dev_next;

	list_for_each_entry_safe(mv_dev, dev_next,
				&mhba->shost_dev_list, list) {
		if (mv_dev->id == id) {
			dev_dbg(&mhba->pdev->dev,
				"detach device(0:%d:0) wwid(%llx) from HOST\n",
				mv_dev->id, mv_dev->wwid);
			mvumi_handle_hotplug(mhba, mv_dev->id, DEVICE_OFFLINE);
			list_del_init(&mv_dev->list);
			kfree(mv_dev);
		}
	}
}

static int mvumi_probe_devices(struct mvumi_hba *mhba)
{
	int id, maxid;
	u64 wwid = 0;
	struct mvumi_device *mv_dev = NULL;
	struct mvumi_cmd *cmd = NULL;
	int found = 0;

	cmd = mvumi_create_internal_cmd(mhba, 64);
	if (!cmd)
		return -1;

	if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9143)
		maxid = mhba->max_target_id;
	else
		maxid = mhba->max_target_id - 1;

	for (id = 0; id < maxid; id++) {
		wwid = mvumi_inquiry(mhba, id, cmd);
		if (!wwid) {
			/* device no response, remove it */
			mvumi_remove_devices(mhba, id);
		} else {
			/* device response, add it */
			found = mvumi_match_devices(mhba, id, wwid);
			if (!found) {
				mvumi_remove_devices(mhba, id);
				mv_dev = kzalloc(sizeof(struct mvumi_device),
								GFP_KERNEL);
				if (!mv_dev) {
					dev_err(&mhba->pdev->dev,
						"%s alloc mv_dev failed\n",
						__func__);
					continue;
				}
				mv_dev->id = id;
				mv_dev->wwid = wwid;
				mv_dev->sdev = NULL;
				INIT_LIST_HEAD(&mv_dev->list);
				list_add_tail(&mv_dev->list,
					      &mhba->mhba_dev_list);
				dev_dbg(&mhba->pdev->dev,
					"probe a new device(0:%d:0)"
					" wwid(%llx)\n", id, mv_dev->wwid);
			} else if (found == -1)
				return -1;
			else
				continue;
		}
	}

	if (cmd)
		mvumi_delete_internal_cmd(mhba, cmd);

	return 0;
}

static int mvumi_rescan_bus(void *data)
{
	int ret = 0;
	struct mvumi_hba *mhba = (struct mvumi_hba *) data;
	struct mvumi_device *mv_dev = NULL , *dev_next;

	while (!kthread_should_stop()) {

		set_current_state(TASK_INTERRUPTIBLE);
		if (!atomic_read(&mhba->pnp_count))
			schedule();
		msleep(1000);
		atomic_set(&mhba->pnp_count, 0);
		__set_current_state(TASK_RUNNING);

		mutex_lock(&mhba->device_lock);
		ret = mvumi_probe_devices(mhba);
		if (!ret) {
			list_for_each_entry_safe(mv_dev, dev_next,
						 &mhba->mhba_dev_list, list) {
				if (mvumi_handle_hotplug(mhba, mv_dev->id,
							 DEVICE_ONLINE)) {
					dev_err(&mhba->pdev->dev,
						"%s add device(0:%d:0) failed"
						"wwid(%llx) has exist\n",
						__func__,
						mv_dev->id, mv_dev->wwid);
					list_del_init(&mv_dev->list);
					kfree(mv_dev);
				} else {
					list_move_tail(&mv_dev->list,
						       &mhba->shost_dev_list);
				}
			}
		}
		mutex_unlock(&mhba->device_lock);
	}
	return 0;
}

static void mvumi_proc_msg(struct mvumi_hba *mhba,
					struct mvumi_hotplug_event *param)
{
	u16 size = param->size;
	const unsigned long *ar_bitmap;
	const unsigned long *re_bitmap;
	int index;

	if (mhba->fw_flag & MVUMI_FW_ATTACH) {
		index = -1;
		ar_bitmap = (const unsigned long *) param->bitmap;
		re_bitmap = (const unsigned long *) &param->bitmap[size >> 3];

		mutex_lock(&mhba->sas_discovery_mutex);
		do {
			index = find_next_zero_bit(ar_bitmap, size, index + 1);
			if (index >= size)
				break;
			mvumi_handle_hotplug(mhba, index, DEVICE_ONLINE);
		} while (1);

		index = -1;
		do {
			index = find_next_zero_bit(re_bitmap, size, index + 1);
			if (index >= size)
				break;
			mvumi_handle_hotplug(mhba, index, DEVICE_OFFLINE);
		} while (1);
		mutex_unlock(&mhba->sas_discovery_mutex);
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
	} else if (msg == APICDB1_HOST_GETEVENT) {
		mvumi_proc_msg(mhba, buffer);
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

static void mvumi_launch_events(struct mvumi_hba *mhba, u32 isr_status)
{
	struct mvumi_events_wq *mu_ev;

	while (isr_status & (DRBL_BUS_CHANGE | DRBL_EVENT_NOTIFY)) {
		if (isr_status & DRBL_BUS_CHANGE) {
			atomic_inc(&mhba->pnp_count);
			wake_up_process(mhba->dm_thread);
			isr_status &= ~(DRBL_BUS_CHANGE);
			continue;
		}

		mu_ev = kzalloc(sizeof(*mu_ev), GFP_ATOMIC);
		if (mu_ev) {
			INIT_WORK(&mu_ev->work_q, mvumi_scan_events);
			mu_ev->mhba = mhba;
			mu_ev->event = APICDB1_EVENT_GETEVENT;
			isr_status &= ~(DRBL_EVENT_NOTIFY);
			mu_ev->param = NULL;
			schedule_work(&mu_ev->work_q);
		}
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

	if (mhba->global_isr & mhba->regs->int_dl_cpu2pciea) {
		if (mhba->isr_status & (DRBL_BUS_CHANGE | DRBL_EVENT_NOTIFY))
			mvumi_launch_events(mhba, mhba->isr_status);
		if (mhba->isr_status & DRBL_HANDSHAKE_ISR) {
			dev_warn(&mhba->pdev->dev, "enter handshake again!\n");
			mvumi_handshake(mhba);
		}

	}

	if (mhba->global_isr & mhba->regs->int_comaout)
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
	mvumi_get_ib_list_entry(mhba, &ib_entry);

	cmd->frame->tag = tag_get_one(mhba, &mhba->tag_pool);
	cmd->frame->request_id = mhba->io_seq++;
	cmd->request_id = cmd->frame->request_id;
	mhba->tag_cmd[cmd->frame->tag] = cmd;
	frame_len = sizeof(*ib_frame) - 4 +
				ib_frame->sg_counts * sizeof(struct mvumi_sgl);
	if (mhba->hba_capability & HS_CAPABILITY_SUPPORT_DYN_SRC) {
		struct mvumi_dyn_list_entry *dle;
		dle = ib_entry;
		dle->src_low_addr =
			cpu_to_le32(lower_32_bits(cmd->frame_phys));
		dle->src_high_addr =
			cpu_to_le32(upper_32_bits(cmd->frame_phys));
		dle->if_length = (frame_len >> 2) & 0xFFF;
	} else {
		memcpy(ib_entry, ib_frame, frame_len);
	}
	return MV_QUEUE_COMMAND_RESULT_SENT;
}

static void mvumi_fire_cmd(struct mvumi_hba *mhba, struct mvumi_cmd *cmd)
{
	unsigned short num_of_cl_sent = 0;
	unsigned int count;
	enum mvumi_qc_result result;

	if (cmd)
		list_add_tail(&cmd->queue_pointer, &mhba->waiting_req_list);
	count = mhba->instancet->check_ib_list(mhba);
	if (list_empty(&mhba->waiting_req_list) || !count)
		return;

	do {
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
	} while (!list_empty(&mhba->waiting_req_list) && count--);

	if (num_of_cl_sent > 0)
		mvumi_send_ib_list_entry(mhba);
}

/**
 * mvumi_enable_intr -	Enables interrupts
 * @mhba:		Adapter soft state
 */
static void mvumi_enable_intr(struct mvumi_hba *mhba)
{
	unsigned int mask;
	struct mvumi_hw_regs *regs = mhba->regs;

	iowrite32(regs->int_drbl_int_mask, regs->arm_to_pciea_mask_reg);
	mask = ioread32(regs->enpointa_mask_reg);
	mask |= regs->int_dl_cpu2pciea | regs->int_comaout | regs->int_comaerr;
	iowrite32(mask, regs->enpointa_mask_reg);
}

/**
 * mvumi_disable_intr -Disables interrupt
 * @mhba:		Adapter soft state
 */
static void mvumi_disable_intr(struct mvumi_hba *mhba)
{
	unsigned int mask;
	struct mvumi_hw_regs *regs = mhba->regs;

	iowrite32(0, regs->arm_to_pciea_mask_reg);
	mask = ioread32(regs->enpointa_mask_reg);
	mask &= ~(regs->int_dl_cpu2pciea | regs->int_comaout |
							regs->int_comaerr);
	iowrite32(mask, regs->enpointa_mask_reg);
}

static int mvumi_clear_intr(void *extend)
{
	struct mvumi_hba *mhba = (struct mvumi_hba *) extend;
	unsigned int status, isr_status = 0, tmp = 0;
	struct mvumi_hw_regs *regs = mhba->regs;

	status = ioread32(regs->main_int_cause_reg);
	if (!(status & regs->int_mu) || status == 0xFFFFFFFF)
		return 1;
	if (unlikely(status & regs->int_comaerr)) {
		tmp = ioread32(regs->outb_isr_cause);
		if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9580) {
			if (tmp & regs->clic_out_err) {
				iowrite32(tmp & regs->clic_out_err,
							regs->outb_isr_cause);
			}
		} else {
			if (tmp & (regs->clic_in_err | regs->clic_out_err))
				iowrite32(tmp & (regs->clic_in_err |
						regs->clic_out_err),
						regs->outb_isr_cause);
		}
		status ^= mhba->regs->int_comaerr;
		/* inbound or outbound parity error, command will timeout */
	}
	if (status & regs->int_comaout) {
		tmp = ioread32(regs->outb_isr_cause);
		if (tmp & regs->clic_irq)
			iowrite32(tmp & regs->clic_irq, regs->outb_isr_cause);
	}
	if (status & regs->int_dl_cpu2pciea) {
		isr_status = ioread32(regs->arm_to_pciea_drbl_reg);
		if (isr_status)
			iowrite32(isr_status, regs->arm_to_pciea_drbl_reg);
	}

	mhba->global_isr = status;
	mhba->isr_status = isr_status;

	return 0;
}

/**
 * mvumi_read_fw_status_reg - returns the current FW status value
 * @mhba:		Adapter soft state
 */
static unsigned int mvumi_read_fw_status_reg(struct mvumi_hba *mhba)
{
	unsigned int status;

	status = ioread32(mhba->regs->arm_to_pciea_drbl_reg);
	if (status)
		iowrite32(status, mhba->regs->arm_to_pciea_drbl_reg);
	return status;
}

static struct mvumi_instance_template mvumi_instance_9143 = {
	.fire_cmd = mvumi_fire_cmd,
	.enable_intr = mvumi_enable_intr,
	.disable_intr = mvumi_disable_intr,
	.clear_intr = mvumi_clear_intr,
	.read_fw_status_reg = mvumi_read_fw_status_reg,
	.check_ib_list = mvumi_check_ib_list_9143,
	.check_ob_list = mvumi_check_ob_list_9143,
	.reset_host = mvumi_reset_host_9143,
};

static struct mvumi_instance_template mvumi_instance_9580 = {
	.fire_cmd = mvumi_fire_cmd,
	.enable_intr = mvumi_enable_intr,
	.disable_intr = mvumi_disable_intr,
	.clear_intr = mvumi_clear_intr,
	.read_fw_status_reg = mvumi_read_fw_status_reg,
	.check_ib_list = mvumi_check_ib_list_9580,
	.check_ob_list = mvumi_check_ob_list_9580,
	.reset_host = mvumi_reset_host_9580,
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
		pci_unmap_sg(mhba->pdev, scsi_sglist(scmd),
			     scsi_sg_count(scmd),
			     (int)scmd->sc_data_direction);
	}
	mvumi_return_cmd(mhba, cmd);
	spin_unlock_irqrestore(mhba->shost->host_lock, flags);

	return BLK_EH_DONE;
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
	.eh_timed_out = mvumi_timed_out,
	.eh_host_reset_handler = mvumi_host_reset,
	.bios_param = mvumi_bios_param,
	.this_id = -1,
};

static int mvumi_cfg_hw_reg(struct mvumi_hba *mhba)
{
	void *base = NULL;
	struct mvumi_hw_regs *regs;

	switch (mhba->pdev->device) {
	case PCI_DEVICE_ID_MARVELL_MV9143:
		mhba->mmio = mhba->base_addr[0];
		base = mhba->mmio;
		if (!mhba->regs) {
			mhba->regs = kzalloc(sizeof(*regs), GFP_KERNEL);
			if (mhba->regs == NULL)
				return -ENOMEM;
		}
		regs = mhba->regs;

		/* For Arm */
		regs->ctrl_sts_reg          = base + 0x20104;
		regs->rstoutn_mask_reg      = base + 0x20108;
		regs->sys_soft_rst_reg      = base + 0x2010C;
		regs->main_int_cause_reg    = base + 0x20200;
		regs->enpointa_mask_reg     = base + 0x2020C;
		regs->rstoutn_en_reg        = base + 0xF1400;
		/* For Doorbell */
		regs->pciea_to_arm_drbl_reg = base + 0x20400;
		regs->arm_to_pciea_drbl_reg = base + 0x20408;
		regs->arm_to_pciea_mask_reg = base + 0x2040C;
		regs->pciea_to_arm_msg0     = base + 0x20430;
		regs->pciea_to_arm_msg1     = base + 0x20434;
		regs->arm_to_pciea_msg0     = base + 0x20438;
		regs->arm_to_pciea_msg1     = base + 0x2043C;

		/* For Message Unit */

		regs->inb_aval_count_basel  = base + 0x508;
		regs->inb_aval_count_baseh  = base + 0x50C;
		regs->inb_write_pointer     = base + 0x518;
		regs->inb_read_pointer      = base + 0x51C;
		regs->outb_coal_cfg         = base + 0x568;
		regs->outb_copy_basel       = base + 0x5B0;
		regs->outb_copy_baseh       = base + 0x5B4;
		regs->outb_copy_pointer     = base + 0x544;
		regs->outb_read_pointer     = base + 0x548;
		regs->outb_isr_cause        = base + 0x560;
		regs->outb_coal_cfg         = base + 0x568;
		/* Bit setting for HW */
		regs->int_comaout           = 1 << 8;
		regs->int_comaerr           = 1 << 6;
		regs->int_dl_cpu2pciea      = 1 << 1;
		regs->cl_pointer_toggle     = 1 << 12;
		regs->clic_irq              = 1 << 1;
		regs->clic_in_err           = 1 << 8;
		regs->clic_out_err          = 1 << 12;
		regs->cl_slot_num_mask      = 0xFFF;
		regs->int_drbl_int_mask     = 0x3FFFFFFF;
		regs->int_mu = regs->int_dl_cpu2pciea | regs->int_comaout |
							regs->int_comaerr;
		break;
	case PCI_DEVICE_ID_MARVELL_MV9580:
		mhba->mmio = mhba->base_addr[2];
		base = mhba->mmio;
		if (!mhba->regs) {
			mhba->regs = kzalloc(sizeof(*regs), GFP_KERNEL);
			if (mhba->regs == NULL)
				return -ENOMEM;
		}
		regs = mhba->regs;
		/* For Arm */
		regs->ctrl_sts_reg          = base + 0x20104;
		regs->rstoutn_mask_reg      = base + 0x1010C;
		regs->sys_soft_rst_reg      = base + 0x10108;
		regs->main_int_cause_reg    = base + 0x10200;
		regs->enpointa_mask_reg     = base + 0x1020C;
		regs->rstoutn_en_reg        = base + 0xF1400;

		/* For Doorbell */
		regs->pciea_to_arm_drbl_reg = base + 0x10460;
		regs->arm_to_pciea_drbl_reg = base + 0x10480;
		regs->arm_to_pciea_mask_reg = base + 0x10484;
		regs->pciea_to_arm_msg0     = base + 0x10400;
		regs->pciea_to_arm_msg1     = base + 0x10404;
		regs->arm_to_pciea_msg0     = base + 0x10420;
		regs->arm_to_pciea_msg1     = base + 0x10424;

		/* For reset*/
		regs->reset_request         = base + 0x10108;
		regs->reset_enable          = base + 0x1010c;

		/* For Message Unit */
		regs->inb_aval_count_basel  = base + 0x4008;
		regs->inb_aval_count_baseh  = base + 0x400C;
		regs->inb_write_pointer     = base + 0x4018;
		regs->inb_read_pointer      = base + 0x401C;
		regs->outb_copy_basel       = base + 0x4058;
		regs->outb_copy_baseh       = base + 0x405C;
		regs->outb_copy_pointer     = base + 0x406C;
		regs->outb_read_pointer     = base + 0x4070;
		regs->outb_coal_cfg         = base + 0x4080;
		regs->outb_isr_cause        = base + 0x4088;
		/* Bit setting for HW */
		regs->int_comaout           = 1 << 4;
		regs->int_dl_cpu2pciea      = 1 << 12;
		regs->int_comaerr           = 1 << 29;
		regs->cl_pointer_toggle     = 1 << 14;
		regs->cl_slot_num_mask      = 0x3FFF;
		regs->clic_irq              = 1 << 0;
		regs->clic_out_err          = 1 << 1;
		regs->int_drbl_int_mask     = 0x3FFFFFFF;
		regs->int_mu = regs->int_dl_cpu2pciea | regs->int_comaout;
		break;
	default:
		return -1;
		break;
	}

	return 0;
}

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

	switch (mhba->pdev->device) {
	case PCI_DEVICE_ID_MARVELL_MV9143:
		mhba->instancet = &mvumi_instance_9143;
		mhba->io_seq = 0;
		mhba->max_sge = MVUMI_MAX_SG_ENTRY;
		mhba->request_id_enabled = 1;
		break;
	case PCI_DEVICE_ID_MARVELL_MV9580:
		mhba->instancet = &mvumi_instance_9580;
		mhba->io_seq = 0;
		mhba->max_sge = MVUMI_MAX_SG_ENTRY;
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
	ret = mvumi_cfg_hw_reg(mhba);
	if (ret) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for reg\n");
		ret = -ENOMEM;
		goto fail_alloc_mem;
	}
	mhba->handshake_page = pci_alloc_consistent(mhba->pdev, HSP_MAX_SIZE,
						&mhba->handshake_page_phys);
	if (!mhba->handshake_page) {
		dev_err(&mhba->pdev->dev,
			"failed to allocate memory for handshake\n");
		ret = -ENOMEM;
		goto fail_alloc_page;
	}

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
	pci_free_consistent(mhba->pdev, HSP_MAX_SIZE,
		mhba->handshake_page, mhba->handshake_page_phys);
fail_alloc_page:
	kfree(mhba->regs);
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
	struct scsi_device *sdev = NULL;
	int ret;
	unsigned int max_sg = (mhba->ib_max_size + 4 -
		sizeof(struct mvumi_msg_frame)) / sizeof(struct mvumi_sgl);

	host->irq = mhba->pdev->irq;
	host->unique_id = mhba->unique_id;
	host->can_queue = (mhba->max_io - 1) ? (mhba->max_io - 1) : 1;
	host->sg_tablesize = mhba->max_sge > max_sg ? max_sg : mhba->max_sge;
	host->max_sectors = mhba->max_transfer_size / 512;
	host->cmd_per_lun = (mhba->max_io - 1) ? (mhba->max_io - 1) : 1;
	host->max_id = mhba->max_target_id;
	host->max_cmd_len = MAX_COMMAND_SIZE;

	ret = scsi_add_host(host, &mhba->pdev->dev);
	if (ret) {
		dev_err(&mhba->pdev->dev, "scsi_add_host failed\n");
		return ret;
	}
	mhba->fw_flag |= MVUMI_FW_ATTACH;

	mutex_lock(&mhba->sas_discovery_mutex);
	if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9580)
		ret = scsi_add_device(host, 0, mhba->max_target_id - 1, 0);
	else
		ret = 0;
	if (ret) {
		dev_err(&mhba->pdev->dev, "add virtual device failed\n");
		mutex_unlock(&mhba->sas_discovery_mutex);
		goto fail_add_device;
	}

	mhba->dm_thread = kthread_create(mvumi_rescan_bus,
						mhba, "mvumi_scanthread");
	if (IS_ERR(mhba->dm_thread)) {
		dev_err(&mhba->pdev->dev,
			"failed to create device scan thread\n");
		mutex_unlock(&mhba->sas_discovery_mutex);
		goto fail_create_thread;
	}
	atomic_set(&mhba->pnp_count, 1);
	wake_up_process(mhba->dm_thread);

	mutex_unlock(&mhba->sas_discovery_mutex);
	return 0;

fail_create_thread:
	if (mhba->pdev->device == PCI_DEVICE_ID_MARVELL_MV9580)
		sdev = scsi_device_lookup(mhba->shost, 0,
						mhba->max_target_id - 1, 0);
	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
fail_add_device:
	scsi_remove_host(mhba->shost);
	return ret;
}

/**
 * mvumi_probe_one -	PCI hotplug entry point
 * @pdev:		PCI device structure
 * @id:			PCI ids of supported hotplugged adapter
 */
static int mvumi_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
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
	mutex_init(&mhba->device_lock);
	INIT_LIST_HEAD(&mhba->mhba_dev_list);
	INIT_LIST_HEAD(&mhba->shost_dev_list);
	atomic_set(&mhba->fw_outstanding, 0);
	init_waitqueue_head(&mhba->int_cmd_wait_q);
	mutex_init(&mhba->sas_discovery_mutex);

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

	mhba->instancet->enable_intr(mhba);
	pci_set_drvdata(pdev, mhba);

	ret = mvumi_io_attach(mhba);
	if (ret)
		goto fail_io_attach;

	mvumi_backup_bar_addr(mhba);
	dev_dbg(&pdev->dev, "probe mvumi driver successfully.\n");

	return 0;

fail_io_attach:
	mhba->instancet->disable_intr(mhba);
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
	if (mhba->dm_thread) {
		kthread_stop(mhba->dm_thread);
		mhba->dm_thread = NULL;
	}

	mvumi_detach_devices(mhba);
	host = mhba->shost;
	scsi_remove_host(mhba->shost);
	mvumi_flush_cache(mhba);

	mhba->instancet->disable_intr(mhba);
	free_irq(mhba->pdev->irq, mhba);
	mvumi_release_fw(mhba);
	scsi_host_put(host);
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

static int __maybe_unused mvumi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mvumi_hba *mhba = NULL;

	mhba = pci_get_drvdata(pdev);
	mvumi_flush_cache(mhba);

	pci_set_drvdata(pdev, mhba);
	mhba->instancet->disable_intr(mhba);
	free_irq(mhba->pdev->irq, mhba);
	mvumi_unmap_pci_addr(pdev, mhba->base_addr);
	pci_release_regions(pdev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int __maybe_unused mvumi_resume(struct pci_dev *pdev)
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

	if (mvumi_cfg_hw_reg(mhba)) {
		ret = -EINVAL;
		goto unmap_pci_addr;
	}

	mhba->mmio = mhba->base_addr[0];
	mvumi_reset(mhba);

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
	mhba->instancet->enable_intr(mhba);

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
	.remove = mvumi_detach_one,
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
