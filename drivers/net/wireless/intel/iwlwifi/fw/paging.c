/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "iwl-drv.h"
#include "runtime.h"
#include "fw/api/commands.h"

void iwl_free_fw_paging(struct iwl_fw_runtime *fwrt)
{
	int i;

	if (!fwrt->fw_paging_db[0].fw_paging_block)
		return;

	for (i = 0; i < NUM_OF_FW_PAGING_BLOCKS; i++) {
		struct iwl_fw_paging *paging = &fwrt->fw_paging_db[i];

		if (!paging->fw_paging_block) {
			IWL_DEBUG_FW(fwrt,
				     "Paging: block %d already freed, continue to next page\n",
				     i);

			continue;
		}
		dma_unmap_page(fwrt->trans->dev, paging->fw_paging_phys,
			       paging->fw_paging_size, DMA_BIDIRECTIONAL);

		__free_pages(paging->fw_paging_block,
			     get_order(paging->fw_paging_size));
		paging->fw_paging_block = NULL;
	}

	memset(fwrt->fw_paging_db, 0, sizeof(fwrt->fw_paging_db));
}
IWL_EXPORT_SYMBOL(iwl_free_fw_paging);

static int iwl_alloc_fw_paging_mem(struct iwl_fw_runtime *fwrt,
				   const struct fw_img *image)
{
	struct page *block;
	dma_addr_t phys = 0;
	int blk_idx, order, num_of_pages, size;

	if (fwrt->fw_paging_db[0].fw_paging_block)
		return 0;

	/* ensure BLOCK_2_EXP_SIZE is power of 2 of PAGING_BLOCK_SIZE */
	BUILD_BUG_ON(BIT(BLOCK_2_EXP_SIZE) != PAGING_BLOCK_SIZE);

	num_of_pages = image->paging_mem_size / FW_PAGING_SIZE;
	fwrt->num_of_paging_blk =
		DIV_ROUND_UP(num_of_pages, NUM_OF_PAGE_PER_GROUP);
	fwrt->num_of_pages_in_last_blk =
		num_of_pages -
		NUM_OF_PAGE_PER_GROUP * (fwrt->num_of_paging_blk - 1);

	IWL_DEBUG_FW(fwrt,
		     "Paging: allocating mem for %d paging blocks, each block holds 8 pages, last block holds %d pages\n",
		     fwrt->num_of_paging_blk,
		     fwrt->num_of_pages_in_last_blk);

	/*
	 * Allocate CSS and paging blocks in dram.
	 */
	for (blk_idx = 0; blk_idx < fwrt->num_of_paging_blk + 1; blk_idx++) {
		/* For CSS allocate 4KB, for others PAGING_BLOCK_SIZE (32K) */
		size = blk_idx ? PAGING_BLOCK_SIZE : FW_PAGING_SIZE;
		order = get_order(size);
		block = alloc_pages(GFP_KERNEL, order);
		if (!block) {
			/* free all the previous pages since we failed */
			iwl_free_fw_paging(fwrt);
			return -ENOMEM;
		}

		fwrt->fw_paging_db[blk_idx].fw_paging_block = block;
		fwrt->fw_paging_db[blk_idx].fw_paging_size = size;

		phys = dma_map_page(fwrt->trans->dev, block, 0,
				    PAGE_SIZE << order,
				    DMA_BIDIRECTIONAL);
		if (dma_mapping_error(fwrt->trans->dev, phys)) {
			/*
			 * free the previous pages and the current one
			 * since we failed to map_page.
			 */
			iwl_free_fw_paging(fwrt);
			return -ENOMEM;
		}
		fwrt->fw_paging_db[blk_idx].fw_paging_phys = phys;

		if (!blk_idx)
			IWL_DEBUG_FW(fwrt,
				     "Paging: allocated 4K(CSS) bytes (order %d) for firmware paging.\n",
				     order);
		else
			IWL_DEBUG_FW(fwrt,
				     "Paging: allocated 32K bytes (order %d) for firmware paging.\n",
				     order);
	}

	return 0;
}

static int iwl_fill_paging_mem(struct iwl_fw_runtime *fwrt,
			       const struct fw_img *image)
{
	int sec_idx, idx, ret;
	u32 offset = 0;

	/*
	 * find where is the paging image start point:
	 * if CPU2 exist and it's in paging format, then the image looks like:
	 * CPU1 sections (2 or more)
	 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between CPU1 to CPU2
	 * CPU2 sections (not paged)
	 * PAGING_SEPARATOR_SECTION delimiter - separate between CPU2
	 * non paged to CPU2 paging sec
	 * CPU2 paging CSS
	 * CPU2 paging image (including instruction and data)
	 */
	for (sec_idx = 0; sec_idx < image->num_sec; sec_idx++) {
		if (image->sec[sec_idx].offset == PAGING_SEPARATOR_SECTION) {
			sec_idx++;
			break;
		}
	}

	/*
	 * If paging is enabled there should be at least 2 more sections left
	 * (one for CSS and one for Paging data)
	 */
	if (sec_idx >= image->num_sec - 1) {
		IWL_ERR(fwrt, "Paging: Missing CSS and/or paging sections\n");
		ret = -EINVAL;
		goto err;
	}

	/* copy the CSS block to the dram */
	IWL_DEBUG_FW(fwrt, "Paging: load paging CSS to FW, sec = %d\n",
		     sec_idx);

	if (image->sec[sec_idx].len > fwrt->fw_paging_db[0].fw_paging_size) {
		IWL_ERR(fwrt, "CSS block is larger than paging size\n");
		ret = -EINVAL;
		goto err;
	}

	memcpy(page_address(fwrt->fw_paging_db[0].fw_paging_block),
	       image->sec[sec_idx].data,
	       image->sec[sec_idx].len);
	dma_sync_single_for_device(fwrt->trans->dev,
				   fwrt->fw_paging_db[0].fw_paging_phys,
				   fwrt->fw_paging_db[0].fw_paging_size,
				   DMA_BIDIRECTIONAL);

	IWL_DEBUG_FW(fwrt,
		     "Paging: copied %d CSS bytes to first block\n",
		     fwrt->fw_paging_db[0].fw_paging_size);

	sec_idx++;

	/*
	 * Copy the paging blocks to the dram.  The loop index starts
	 * from 1 since the CSS block (index 0) was already copied to
	 * dram.  We use num_of_paging_blk + 1 to account for that.
	 */
	for (idx = 1; idx < fwrt->num_of_paging_blk + 1; idx++) {
		struct iwl_fw_paging *block = &fwrt->fw_paging_db[idx];
		int remaining = image->sec[sec_idx].len - offset;
		int len = block->fw_paging_size;

		/*
		 * For the last block, we copy all that is remaining,
		 * for all other blocks, we copy fw_paging_size at a
		 * time. */
		if (idx == fwrt->num_of_paging_blk) {
			len = remaining;
			if (remaining !=
			    fwrt->num_of_pages_in_last_blk * FW_PAGING_SIZE) {
				IWL_ERR(fwrt,
					"Paging: last block contains more data than expected %d\n",
					remaining);
				ret = -EINVAL;
				goto err;
			}
		} else if (block->fw_paging_size > remaining) {
			IWL_ERR(fwrt,
				"Paging: not enough data in other in block %d (%d)\n",
				idx, remaining);
			ret = -EINVAL;
			goto err;
		}

		memcpy(page_address(block->fw_paging_block),
		       image->sec[sec_idx].data + offset, len);
		dma_sync_single_for_device(fwrt->trans->dev,
					   block->fw_paging_phys,
					   block->fw_paging_size,
					   DMA_BIDIRECTIONAL);

		IWL_DEBUG_FW(fwrt,
			     "Paging: copied %d paging bytes to block %d\n",
			     len, idx);

		offset += block->fw_paging_size;
	}

	return 0;

err:
	iwl_free_fw_paging(fwrt);
	return ret;
}

static int iwl_save_fw_paging(struct iwl_fw_runtime *fwrt,
			      const struct fw_img *fw)
{
	int ret;

	ret = iwl_alloc_fw_paging_mem(fwrt, fw);
	if (ret)
		return ret;

	return iwl_fill_paging_mem(fwrt, fw);
}

/* send paging cmd to FW in case CPU2 has paging image */
static int iwl_send_paging_cmd(struct iwl_fw_runtime *fwrt,
			       const struct fw_img *fw)
{
	struct iwl_fw_paging_cmd paging_cmd = {
		.flags = cpu_to_le32(PAGING_CMD_IS_SECURED |
				     PAGING_CMD_IS_ENABLED |
				     (fwrt->num_of_pages_in_last_blk <<
				      PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = cpu_to_le32(BLOCK_2_EXP_SIZE),
		.block_num = cpu_to_le32(fwrt->num_of_paging_blk),
	};
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(FW_PAGING_BLOCK_CMD, IWL_ALWAYS_LONG_GROUP, 0),
		.len = { sizeof(paging_cmd), },
		.data = { &paging_cmd, },
	};
	int blk_idx;

	/* loop for for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < fwrt->num_of_paging_blk + 1; blk_idx++) {
		dma_addr_t addr = fwrt->fw_paging_db[blk_idx].fw_paging_phys;
		__le32 phy_addr;

		addr = addr >> PAGE_2_EXP_SIZE;
		phy_addr = cpu_to_le32(addr);
		paging_cmd.device_phy_addr[blk_idx] = phy_addr;
	}

	return iwl_trans_send_cmd(fwrt->trans, &hcmd);
}

int iwl_init_paging(struct iwl_fw_runtime *fwrt, enum iwl_ucode_type type)
{
	const struct fw_img *fw = &fwrt->fw->img[type];
	int ret;

	if (fwrt->trans->trans_cfg->gen2)
		return 0;

	/*
	 * Configure and operate fw paging mechanism.
	 * The driver configures the paging flow only once.
	 * The CPU2 paging image is included in the IWL_UCODE_INIT image.
	 */
	if (!fw->paging_mem_size)
		return 0;

	ret = iwl_save_fw_paging(fwrt, fw);
	if (ret) {
		IWL_ERR(fwrt, "failed to save the FW paging image\n");
		return ret;
	}

	ret = iwl_send_paging_cmd(fwrt, fw);
	if (ret) {
		IWL_ERR(fwrt, "failed to send the paging cmd\n");
		iwl_free_fw_paging(fwrt);
		return ret;
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_init_paging);
