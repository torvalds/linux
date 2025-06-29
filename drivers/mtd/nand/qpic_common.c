// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/qcom_adm.h>
#include <linux/dma/qcom_bam_dma.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/nand-qpic-common.h>

/**
 * qcom_free_bam_transaction() - Frees the BAM transaction memory
 * @nandc: qpic nand controller
 *
 * This function frees the bam transaction memory
 */
void qcom_free_bam_transaction(struct qcom_nand_controller *nandc)
{
	struct bam_transaction *bam_txn = nandc->bam_txn;

	kfree(bam_txn);
}
EXPORT_SYMBOL(qcom_free_bam_transaction);

/**
 * qcom_alloc_bam_transaction() - allocate BAM transaction
 * @nandc: qpic nand controller
 *
 * This function will allocate and initialize the BAM transaction structure
 */
struct bam_transaction *
qcom_alloc_bam_transaction(struct qcom_nand_controller *nandc)
{
	struct bam_transaction *bam_txn;
	size_t bam_txn_size;
	unsigned int num_cw = nandc->max_cwperpage;
	void *bam_txn_buf;

	bam_txn_size =
		sizeof(*bam_txn) + num_cw *
		((sizeof(*bam_txn->bam_ce) * QPIC_PER_CW_CMD_ELEMENTS) +
		(sizeof(*bam_txn->cmd_sgl) * QPIC_PER_CW_CMD_SGL) +
		(sizeof(*bam_txn->data_sgl) * QPIC_PER_CW_DATA_SGL));

	bam_txn_buf = kzalloc(bam_txn_size, GFP_KERNEL);
	if (!bam_txn_buf)
		return NULL;

	bam_txn = bam_txn_buf;
	bam_txn_buf += sizeof(*bam_txn);

	bam_txn->bam_ce = bam_txn_buf;
	bam_txn_buf +=
		sizeof(*bam_txn->bam_ce) * QPIC_PER_CW_CMD_ELEMENTS * num_cw;

	bam_txn->cmd_sgl = bam_txn_buf;
	bam_txn_buf +=
		sizeof(*bam_txn->cmd_sgl) * QPIC_PER_CW_CMD_SGL * num_cw;

	bam_txn->data_sgl = bam_txn_buf;

	init_completion(&bam_txn->txn_done);

	return bam_txn;
}
EXPORT_SYMBOL(qcom_alloc_bam_transaction);

/**
 * qcom_clear_bam_transaction() - Clears the BAM transaction
 * @nandc: qpic nand controller
 *
 * This function will clear the BAM transaction indexes.
 */
void qcom_clear_bam_transaction(struct qcom_nand_controller *nandc)
{
	struct bam_transaction *bam_txn = nandc->bam_txn;

	if (!nandc->props->supports_bam)
		return;

	memset(&bam_txn->bam_positions, 0, sizeof(bam_txn->bam_positions));
	bam_txn->last_data_desc = NULL;

	sg_init_table(bam_txn->cmd_sgl, nandc->max_cwperpage *
		      QPIC_PER_CW_CMD_SGL);
	sg_init_table(bam_txn->data_sgl, nandc->max_cwperpage *
		      QPIC_PER_CW_DATA_SGL);

	reinit_completion(&bam_txn->txn_done);
}
EXPORT_SYMBOL(qcom_clear_bam_transaction);

/**
 * qcom_qpic_bam_dma_done() - Callback for DMA descriptor completion
 * @data: data pointer
 *
 * This function is a callback for DMA descriptor completion
 */
void qcom_qpic_bam_dma_done(void *data)
{
	struct bam_transaction *bam_txn = data;

	complete(&bam_txn->txn_done);
}
EXPORT_SYMBOL(qcom_qpic_bam_dma_done);

/**
 * qcom_nandc_dev_to_mem() - Check for dma sync for cpu or device
 * @nandc: qpic nand controller
 * @is_cpu: cpu or Device
 *
 * This function will check for dma sync for cpu or device
 */
inline void qcom_nandc_dev_to_mem(struct qcom_nand_controller *nandc, bool is_cpu)
{
	if (!nandc->props->supports_bam)
		return;

	if (is_cpu)
		dma_sync_single_for_cpu(nandc->dev, nandc->reg_read_dma,
					MAX_REG_RD *
					sizeof(*nandc->reg_read_buf),
					DMA_FROM_DEVICE);
	else
		dma_sync_single_for_device(nandc->dev, nandc->reg_read_dma,
					   MAX_REG_RD *
					   sizeof(*nandc->reg_read_buf),
					   DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(qcom_nandc_dev_to_mem);

/**
 * qcom_prepare_bam_async_desc() - Prepare DMA descriptor
 * @nandc: qpic nand controller
 * @chan: dma channel
 * @flags: flags to control DMA descriptor preparation
 *
 * This function maps the scatter gather list for DMA transfer and forms the
 * DMA descriptor for BAM.This descriptor will be added in the NAND DMA
 * descriptor queue which will be submitted to DMA engine.
 */
int qcom_prepare_bam_async_desc(struct qcom_nand_controller *nandc,
				struct dma_chan *chan, unsigned long flags)
{
	struct desc_info *desc;
	struct scatterlist *sgl;
	unsigned int sgl_cnt;
	int ret;
	struct bam_transaction *bam_txn = nandc->bam_txn;
	enum dma_transfer_direction dir_eng;
	struct dma_async_tx_descriptor *dma_desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	if (chan == nandc->cmd_chan) {
		sgl = &bam_txn->cmd_sgl[bam_txn->cmd_sgl_start];
		sgl_cnt = bam_txn->cmd_sgl_pos - bam_txn->cmd_sgl_start;
		bam_txn->cmd_sgl_start = bam_txn->cmd_sgl_pos;
		dir_eng = DMA_MEM_TO_DEV;
		desc->dir = DMA_TO_DEVICE;
	} else if (chan == nandc->tx_chan) {
		sgl = &bam_txn->data_sgl[bam_txn->tx_sgl_start];
		sgl_cnt = bam_txn->tx_sgl_pos - bam_txn->tx_sgl_start;
		bam_txn->tx_sgl_start = bam_txn->tx_sgl_pos;
		dir_eng = DMA_MEM_TO_DEV;
		desc->dir = DMA_TO_DEVICE;
	} else {
		sgl = &bam_txn->data_sgl[bam_txn->rx_sgl_start];
		sgl_cnt = bam_txn->rx_sgl_pos - bam_txn->rx_sgl_start;
		bam_txn->rx_sgl_start = bam_txn->rx_sgl_pos;
		dir_eng = DMA_DEV_TO_MEM;
		desc->dir = DMA_FROM_DEVICE;
	}

	sg_mark_end(sgl + sgl_cnt - 1);
	ret = dma_map_sg(nandc->dev, sgl, sgl_cnt, desc->dir);
	if (ret == 0) {
		dev_err(nandc->dev, "failure in mapping desc\n");
		kfree(desc);
		return -ENOMEM;
	}

	desc->sgl_cnt = sgl_cnt;
	desc->bam_sgl = sgl;

	dma_desc = dmaengine_prep_slave_sg(chan, sgl, sgl_cnt, dir_eng,
					   flags);

	if (!dma_desc) {
		dev_err(nandc->dev, "failure in prep desc\n");
		dma_unmap_sg(nandc->dev, sgl, sgl_cnt, desc->dir);
		kfree(desc);
		return -EINVAL;
	}

	desc->dma_desc = dma_desc;

	/* update last data/command descriptor */
	if (chan == nandc->cmd_chan)
		bam_txn->last_cmd_desc = dma_desc;
	else
		bam_txn->last_data_desc = dma_desc;

	list_add_tail(&desc->node, &nandc->desc_list);

	return 0;
}
EXPORT_SYMBOL(qcom_prepare_bam_async_desc);

/**
 * qcom_prep_bam_dma_desc_cmd() - Prepares the command descriptor for BAM DMA
 * @nandc: qpic nand controller
 * @read: read or write type
 * @reg_off: offset within the controller's data buffer
 * @vaddr: virtual address of the buffer we want to write to
 * @size: DMA transaction size in bytes
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares the command descriptor for BAM DMA
 * which will be used for NAND register reads and writes.
 */
int qcom_prep_bam_dma_desc_cmd(struct qcom_nand_controller *nandc, bool read,
			       int reg_off, const void *vaddr,
			       int size, unsigned int flags)
{
	int bam_ce_size;
	int i, ret;
	struct bam_cmd_element *bam_ce_buffer;
	struct bam_transaction *bam_txn = nandc->bam_txn;
	u32 offset;

	bam_ce_buffer = &bam_txn->bam_ce[bam_txn->bam_ce_pos];

	/* fill the command desc */
	for (i = 0; i < size; i++) {
		offset = nandc->props->bam_offset + reg_off + 4 * i;
		if (read)
			bam_prep_ce(&bam_ce_buffer[i],
				    offset, BAM_READ_COMMAND,
				    reg_buf_dma_addr(nandc,
						     (__le32 *)vaddr + i));
		else
			bam_prep_ce_le32(&bam_ce_buffer[i],
					 offset, BAM_WRITE_COMMAND,
					 *((__le32 *)vaddr + i));
	}

	bam_txn->bam_ce_pos += size;

	/* use the separate sgl after this command */
	if (flags & NAND_BAM_NEXT_SGL) {
		bam_ce_buffer = &bam_txn->bam_ce[bam_txn->bam_ce_start];
		bam_ce_size = (bam_txn->bam_ce_pos -
				bam_txn->bam_ce_start) *
				sizeof(struct bam_cmd_element);
		sg_set_buf(&bam_txn->cmd_sgl[bam_txn->cmd_sgl_pos],
			   bam_ce_buffer, bam_ce_size);
		bam_txn->cmd_sgl_pos++;
		bam_txn->bam_ce_start = bam_txn->bam_ce_pos;

		if (flags & NAND_BAM_NWD) {
			ret = qcom_prepare_bam_async_desc(nandc, nandc->cmd_chan,
							  DMA_PREP_FENCE | DMA_PREP_CMD);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(qcom_prep_bam_dma_desc_cmd);

/**
 * qcom_prep_bam_dma_desc_data() - Prepares the data descriptor for BAM DMA
 * @nandc: qpic nand controller
 * @read: read or write type
 * @vaddr: virtual address of the buffer we want to write to
 * @size: DMA transaction size in bytes
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares the data descriptor for BAM DMA which
 * will be used for NAND data reads and writes.
 */
int qcom_prep_bam_dma_desc_data(struct qcom_nand_controller *nandc, bool read,
				const void *vaddr, int size, unsigned int flags)
{
	int ret;
	struct bam_transaction *bam_txn = nandc->bam_txn;

	if (read) {
		sg_set_buf(&bam_txn->data_sgl[bam_txn->rx_sgl_pos],
			   vaddr, size);
		bam_txn->rx_sgl_pos++;
	} else {
		sg_set_buf(&bam_txn->data_sgl[bam_txn->tx_sgl_pos],
			   vaddr, size);
		bam_txn->tx_sgl_pos++;

		/*
		 * BAM will only set EOT for DMA_PREP_INTERRUPT so if this flag
		 * is not set, form the DMA descriptor
		 */
		if (!(flags & NAND_BAM_NO_EOT)) {
			ret = qcom_prepare_bam_async_desc(nandc, nandc->tx_chan,
							  DMA_PREP_INTERRUPT);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(qcom_prep_bam_dma_desc_data);

/**
 * qcom_prep_adm_dma_desc() - Prepare descriptor for adma
 * @nandc: qpic nand controller
 * @read: read or write type
 * @reg_off: offset within the controller's data buffer
 * @vaddr: virtual address of the buffer we want to write to
 * @size: adm dma transaction size in bytes
 * @flow_control: flow controller
 *
 * This function will prepare descriptor for adma
 */
int qcom_prep_adm_dma_desc(struct qcom_nand_controller *nandc, bool read,
			   int reg_off, const void *vaddr, int size,
			   bool flow_control)
{
	struct qcom_adm_peripheral_config periph_conf = {};
	struct dma_async_tx_descriptor *dma_desc;
	struct dma_slave_config slave_conf = {0};
	enum dma_transfer_direction dir_eng;
	struct desc_info *desc;
	struct scatterlist *sgl;
	int ret;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	sgl = &desc->adm_sgl;

	sg_init_one(sgl, vaddr, size);

	if (read) {
		dir_eng = DMA_DEV_TO_MEM;
		desc->dir = DMA_FROM_DEVICE;
	} else {
		dir_eng = DMA_MEM_TO_DEV;
		desc->dir = DMA_TO_DEVICE;
	}

	ret = dma_map_sg(nandc->dev, sgl, 1, desc->dir);
	if (!ret) {
		ret = -ENOMEM;
		goto err;
	}

	slave_conf.device_fc = flow_control;
	if (read) {
		slave_conf.src_maxburst = 16;
		slave_conf.src_addr = nandc->base_dma + reg_off;
		if (nandc->data_crci) {
			periph_conf.crci = nandc->data_crci;
			slave_conf.peripheral_config = &periph_conf;
			slave_conf.peripheral_size = sizeof(periph_conf);
		}
	} else {
		slave_conf.dst_maxburst = 16;
		slave_conf.dst_addr = nandc->base_dma + reg_off;
		if (nandc->cmd_crci) {
			periph_conf.crci = nandc->cmd_crci;
			slave_conf.peripheral_config = &periph_conf;
			slave_conf.peripheral_size = sizeof(periph_conf);
		}
	}

	ret = dmaengine_slave_config(nandc->chan, &slave_conf);
	if (ret) {
		dev_err(nandc->dev, "failed to configure dma channel\n");
		goto err;
	}

	dma_desc = dmaengine_prep_slave_sg(nandc->chan, sgl, 1, dir_eng, 0);
	if (!dma_desc) {
		dev_err(nandc->dev, "failed to prepare desc\n");
		ret = -EINVAL;
		goto err;
	}

	desc->dma_desc = dma_desc;

	list_add_tail(&desc->node, &nandc->desc_list);

	return 0;
err:
	kfree(desc);

	return ret;
}
EXPORT_SYMBOL(qcom_prep_adm_dma_desc);

/**
 * qcom_read_reg_dma() - read a given number of registers to the reg_read_buf pointer
 * @nandc: qpic nand controller
 * @first: offset of the first register in the contiguous block
 * @num_regs: number of registers to read
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares a descriptor to read a given number of
 * contiguous registers to the reg_read_buf pointer.
 */
int qcom_read_reg_dma(struct qcom_nand_controller *nandc, int first,
		      int num_regs, unsigned int flags)
{
	bool flow_control = false;
	void *vaddr;

	vaddr = nandc->reg_read_buf + nandc->reg_read_pos;
	nandc->reg_read_pos += num_regs;

	if (first == NAND_DEV_CMD_VLD || first == NAND_DEV_CMD1)
		first = dev_cmd_reg_addr(nandc, first);

	if (nandc->props->supports_bam)
		return qcom_prep_bam_dma_desc_cmd(nandc, true, first, vaddr,
					     num_regs, flags);

	if (first == NAND_READ_ID || first == NAND_FLASH_STATUS)
		flow_control = true;

	return qcom_prep_adm_dma_desc(nandc, true, first, vaddr,
				      num_regs * sizeof(u32), flow_control);
}
EXPORT_SYMBOL(qcom_read_reg_dma);

/**
 * qcom_write_reg_dma() - write a given number of registers
 * @nandc: qpic nand controller
 * @vaddr: contiguous memory from where register value will
 *	   be written
 * @first: offset of the first register in the contiguous block
 * @num_regs: number of registers to write
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares a descriptor to write a given number of
 * contiguous registers
 */
int qcom_write_reg_dma(struct qcom_nand_controller *nandc, __le32 *vaddr,
		       int first, int num_regs, unsigned int flags)
{
	bool flow_control = false;

	if (first == NAND_EXEC_CMD)
		flags |= NAND_BAM_NWD;

	if (first == NAND_DEV_CMD1_RESTORE || first == NAND_DEV_CMD1)
		first = dev_cmd_reg_addr(nandc, NAND_DEV_CMD1);

	if (first == NAND_DEV_CMD_VLD_RESTORE || first == NAND_DEV_CMD_VLD)
		first = dev_cmd_reg_addr(nandc, NAND_DEV_CMD_VLD);

	if (nandc->props->supports_bam)
		return qcom_prep_bam_dma_desc_cmd(nandc, false, first, vaddr,
						  num_regs, flags);

	if (first == NAND_FLASH_CMD)
		flow_control = true;

	return qcom_prep_adm_dma_desc(nandc, false, first, vaddr,
				      num_regs * sizeof(u32), flow_control);
}
EXPORT_SYMBOL(qcom_write_reg_dma);

/**
 * qcom_read_data_dma() - transfer data
 * @nandc: qpic nand controller
 * @reg_off: offset within the controller's data buffer
 * @vaddr: virtual address of the buffer we want to write to
 * @size: DMA transaction size in bytes
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares a DMA descriptor to transfer data from the
 * controller's internal buffer to the buffer 'vaddr'
 */
int qcom_read_data_dma(struct qcom_nand_controller *nandc, int reg_off,
		       const u8 *vaddr, int size, unsigned int flags)
{
	if (nandc->props->supports_bam)
		return qcom_prep_bam_dma_desc_data(nandc, true, vaddr, size, flags);

	return qcom_prep_adm_dma_desc(nandc, true, reg_off, vaddr, size, false);
}
EXPORT_SYMBOL(qcom_read_data_dma);

/**
 * qcom_write_data_dma() - transfer data
 * @nandc: qpic nand controller
 * @reg_off: offset within the controller's data buffer
 * @vaddr: virtual address of the buffer we want to read from
 * @size: DMA transaction size in bytes
 * @flags: flags to control DMA descriptor preparation
 *
 * This function will prepares a DMA descriptor to transfer data from
 * 'vaddr' to the controller's internal buffer
 */
int qcom_write_data_dma(struct qcom_nand_controller *nandc, int reg_off,
			const u8 *vaddr, int size, unsigned int flags)
{
	if (nandc->props->supports_bam)
		return qcom_prep_bam_dma_desc_data(nandc, false, vaddr, size, flags);

	return qcom_prep_adm_dma_desc(nandc, false, reg_off, vaddr, size, false);
}
EXPORT_SYMBOL(qcom_write_data_dma);

/**
 * qcom_submit_descs() - submit dma descriptor
 * @nandc: qpic nand controller
 *
 * This function will submit all the prepared dma descriptor
 * cmd or data descriptor
 */
int qcom_submit_descs(struct qcom_nand_controller *nandc)
{
	struct desc_info *desc, *n;
	dma_cookie_t cookie = 0;
	struct bam_transaction *bam_txn = nandc->bam_txn;
	int ret = 0;

	if (nandc->props->supports_bam) {
		if (bam_txn->rx_sgl_pos > bam_txn->rx_sgl_start) {
			ret = qcom_prepare_bam_async_desc(nandc, nandc->rx_chan, 0);
			if (ret)
				goto err_unmap_free_desc;
		}

		if (bam_txn->tx_sgl_pos > bam_txn->tx_sgl_start) {
			ret = qcom_prepare_bam_async_desc(nandc, nandc->tx_chan,
							  DMA_PREP_INTERRUPT);
			if (ret)
				goto err_unmap_free_desc;
		}

		if (bam_txn->cmd_sgl_pos > bam_txn->cmd_sgl_start) {
			ret = qcom_prepare_bam_async_desc(nandc, nandc->cmd_chan,
							  DMA_PREP_CMD);
			if (ret)
				goto err_unmap_free_desc;
		}
	}

	list_for_each_entry(desc, &nandc->desc_list, node)
		cookie = dmaengine_submit(desc->dma_desc);

	if (nandc->props->supports_bam) {
		bam_txn->last_cmd_desc->callback = qcom_qpic_bam_dma_done;
		bam_txn->last_cmd_desc->callback_param = bam_txn;

		dma_async_issue_pending(nandc->tx_chan);
		dma_async_issue_pending(nandc->rx_chan);
		dma_async_issue_pending(nandc->cmd_chan);

		if (!wait_for_completion_timeout(&bam_txn->txn_done,
						 QPIC_NAND_COMPLETION_TIMEOUT))
			ret = -ETIMEDOUT;
	} else {
		if (dma_sync_wait(nandc->chan, cookie) != DMA_COMPLETE)
			ret = -ETIMEDOUT;
	}

err_unmap_free_desc:
	/*
	 * Unmap the dma sg_list and free the desc allocated by both
	 * qcom_prepare_bam_async_desc() and qcom_prep_adm_dma_desc() functions.
	 */
	list_for_each_entry_safe(desc, n, &nandc->desc_list, node) {
		list_del(&desc->node);

		if (nandc->props->supports_bam)
			dma_unmap_sg(nandc->dev, desc->bam_sgl,
				     desc->sgl_cnt, desc->dir);
		else
			dma_unmap_sg(nandc->dev, &desc->adm_sgl, 1,
				     desc->dir);

		kfree(desc);
	}

	return ret;
}
EXPORT_SYMBOL(qcom_submit_descs);

/**
 * qcom_clear_read_regs() - reset the read register buffer
 * @nandc: qpic nand controller
 *
 * This function reset the register read buffer for next NAND operation
 */
void qcom_clear_read_regs(struct qcom_nand_controller *nandc)
{
	nandc->reg_read_pos = 0;
	qcom_nandc_dev_to_mem(nandc, false);
}
EXPORT_SYMBOL(qcom_clear_read_regs);

/**
 * qcom_nandc_unalloc() - unallocate qpic nand controller
 * @nandc: qpic nand controller
 *
 * This function will unallocate memory alloacted for qpic nand controller
 */
void qcom_nandc_unalloc(struct qcom_nand_controller *nandc)
{
	if (nandc->props->supports_bam) {
		if (!dma_mapping_error(nandc->dev, nandc->reg_read_dma))
			dma_unmap_single(nandc->dev, nandc->reg_read_dma,
					 MAX_REG_RD *
					 sizeof(*nandc->reg_read_buf),
					 DMA_FROM_DEVICE);

		if (nandc->tx_chan)
			dma_release_channel(nandc->tx_chan);

		if (nandc->rx_chan)
			dma_release_channel(nandc->rx_chan);

		if (nandc->cmd_chan)
			dma_release_channel(nandc->cmd_chan);
	} else {
		if (nandc->chan)
			dma_release_channel(nandc->chan);
	}
}
EXPORT_SYMBOL(qcom_nandc_unalloc);

/**
 * qcom_nandc_alloc() - Allocate qpic nand controller
 * @nandc: qpic nand controller
 *
 * This function will allocate memory for qpic nand controller
 */
int qcom_nandc_alloc(struct qcom_nand_controller *nandc)
{
	int ret;

	ret = dma_set_coherent_mask(nandc->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(nandc->dev, "failed to set DMA mask\n");
		return ret;
	}

	/*
	 * we use the internal buffer for reading ONFI params, reading small
	 * data like ID and status, and preforming read-copy-write operations
	 * when writing to a codeword partially. 532 is the maximum possible
	 * size of a codeword for our nand controller
	 */
	nandc->buf_size = 532;

	nandc->data_buffer = devm_kzalloc(nandc->dev, nandc->buf_size, GFP_KERNEL);
	if (!nandc->data_buffer)
		return -ENOMEM;

	nandc->regs = devm_kzalloc(nandc->dev, sizeof(*nandc->regs), GFP_KERNEL);
	if (!nandc->regs)
		return -ENOMEM;

	nandc->reg_read_buf = devm_kcalloc(nandc->dev, MAX_REG_RD,
					   sizeof(*nandc->reg_read_buf),
					   GFP_KERNEL);
	if (!nandc->reg_read_buf)
		return -ENOMEM;

	if (nandc->props->supports_bam) {
		nandc->reg_read_dma =
			dma_map_single(nandc->dev, nandc->reg_read_buf,
				       MAX_REG_RD *
				       sizeof(*nandc->reg_read_buf),
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(nandc->dev, nandc->reg_read_dma)) {
			dev_err(nandc->dev, "failed to DMA MAP reg buffer\n");
			return -EIO;
		}

		nandc->tx_chan = dma_request_chan(nandc->dev, "tx");
		if (IS_ERR(nandc->tx_chan)) {
			ret = PTR_ERR(nandc->tx_chan);
			nandc->tx_chan = NULL;
			dev_err_probe(nandc->dev, ret,
				      "tx DMA channel request failed\n");
			goto unalloc;
		}

		nandc->rx_chan = dma_request_chan(nandc->dev, "rx");
		if (IS_ERR(nandc->rx_chan)) {
			ret = PTR_ERR(nandc->rx_chan);
			nandc->rx_chan = NULL;
			dev_err_probe(nandc->dev, ret,
				      "rx DMA channel request failed\n");
			goto unalloc;
		}

		nandc->cmd_chan = dma_request_chan(nandc->dev, "cmd");
		if (IS_ERR(nandc->cmd_chan)) {
			ret = PTR_ERR(nandc->cmd_chan);
			nandc->cmd_chan = NULL;
			dev_err_probe(nandc->dev, ret,
				      "cmd DMA channel request failed\n");
			goto unalloc;
		}

		/*
		 * Initially allocate BAM transaction to read ONFI param page.
		 * After detecting all the devices, this BAM transaction will
		 * be freed and the next BAM transaction will be allocated with
		 * maximum codeword size
		 */
		nandc->max_cwperpage = 1;
		nandc->bam_txn = qcom_alloc_bam_transaction(nandc);
		if (!nandc->bam_txn) {
			dev_err(nandc->dev,
				"failed to allocate bam transaction\n");
			ret = -ENOMEM;
			goto unalloc;
		}
	} else {
		nandc->chan = dma_request_chan(nandc->dev, "rxtx");
		if (IS_ERR(nandc->chan)) {
			ret = PTR_ERR(nandc->chan);
			nandc->chan = NULL;
			dev_err_probe(nandc->dev, ret,
				      "rxtx DMA channel request failed\n");
			return ret;
		}
	}

	INIT_LIST_HEAD(&nandc->desc_list);
	INIT_LIST_HEAD(&nandc->host_list);

	return 0;
unalloc:
	qcom_nandc_unalloc(nandc);
	return ret;
}
EXPORT_SYMBOL(qcom_nandc_alloc);

MODULE_DESCRIPTION("QPIC controller common api");
MODULE_LICENSE("GPL");
