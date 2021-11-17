/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay OCS AES Crypto Driver.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */

#ifndef _CRYPTO_OCS_AES_H
#define _CRYPTO_OCS_AES_H

#include <linux/dma-mapping.h>

enum ocs_cipher {
	OCS_AES = 0,
	OCS_SM4 = 1,
};

enum ocs_mode {
	OCS_MODE_ECB = 0,
	OCS_MODE_CBC = 1,
	OCS_MODE_CTR = 2,
	OCS_MODE_CCM = 6,
	OCS_MODE_GCM = 7,
	OCS_MODE_CTS = 9,
};

enum ocs_instruction {
	OCS_ENCRYPT = 0,
	OCS_DECRYPT = 1,
	OCS_EXPAND  = 2,
	OCS_BYPASS  = 3,
};

/**
 * struct ocs_aes_dev - AES device context.
 * @list:			List head for insertion into device list hold
 *				by driver.
 * @dev:			OCS AES device.
 * @irq:			IRQ number.
 * @base_reg:			IO base address of OCS AES.
 * @irq_copy_completion:	Completion to indicate IRQ has been triggered.
 * @dma_err_mask:		Error reported by OCS DMA interrupts.
 * @engine:			Crypto engine for the device.
 */
struct ocs_aes_dev {
	struct list_head list;
	struct device *dev;
	int irq;
	void __iomem *base_reg;
	struct completion irq_completion;
	u32 dma_err_mask;
	struct crypto_engine *engine;
};

/**
 * struct ocs_dll_desc - Descriptor of an OCS DMA Linked List.
 * @vaddr:	Virtual address of the linked list head.
 * @dma_addr:	DMA address of the linked list head.
 * @size:	Size (in bytes) of the linked list.
 */
struct ocs_dll_desc {
	void		*vaddr;
	dma_addr_t	dma_addr;
	size_t		size;
};

int ocs_aes_set_key(struct ocs_aes_dev *aes_dev, const u32 key_size,
		    const u8 *key, const enum ocs_cipher cipher);

int ocs_aes_op(struct ocs_aes_dev *aes_dev,
	       enum ocs_mode mode,
	       enum ocs_cipher cipher,
	       enum ocs_instruction instruction,
	       dma_addr_t dst_dma_list,
	       dma_addr_t src_dma_list,
	       u32 src_size,
	       u8 *iv,
	       u32 iv_size);

/**
 * ocs_aes_bypass_op() - Use OCS DMA to copy data.
 * @aes_dev:            The OCS AES device to use.
 * @dst_dma_list:	The OCS DMA list mapping the memory where input data
 *			will be copied to.
 * @src_dma_list:	The OCS DMA list mapping input data.
 * @src_size:		The amount of data to copy.
 */
static inline int ocs_aes_bypass_op(struct ocs_aes_dev *aes_dev,
				    dma_addr_t dst_dma_list,
				    dma_addr_t src_dma_list, u32 src_size)
{
	return ocs_aes_op(aes_dev, OCS_MODE_ECB, OCS_AES, OCS_BYPASS,
			  dst_dma_list, src_dma_list, src_size, NULL, 0);
}

int ocs_aes_gcm_op(struct ocs_aes_dev *aes_dev,
		   enum ocs_cipher cipher,
		   enum ocs_instruction instruction,
		   dma_addr_t dst_dma_list,
		   dma_addr_t src_dma_list,
		   u32 src_size,
		   const u8 *iv,
		   dma_addr_t aad_dma_list,
		   u32 aad_size,
		   u8 *out_tag,
		   u32 tag_size);

int ocs_aes_ccm_op(struct ocs_aes_dev *aes_dev,
		   enum ocs_cipher cipher,
		   enum ocs_instruction instruction,
		   dma_addr_t dst_dma_list,
		   dma_addr_t src_dma_list,
		   u32 src_size,
		   u8 *iv,
		   dma_addr_t adata_dma_list,
		   u32 adata_size,
		   u8 *in_tag,
		   u32 tag_size);

int ocs_create_linked_list_from_sg(const struct ocs_aes_dev *aes_dev,
				   struct scatterlist *sg,
				   int sg_dma_count,
				   struct ocs_dll_desc *dll_desc,
				   size_t data_size,
				   size_t data_offset);

irqreturn_t ocs_aes_irq_handler(int irq, void *dev_id);

#endif
