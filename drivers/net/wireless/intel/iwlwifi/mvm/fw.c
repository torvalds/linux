/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
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
#include <net/mac80211.h>
#include <linux/netdevice.h>
#include <linux/acpi.h>

#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-fw.h"
#include "iwl-debug.h"
#include "iwl-csr.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-io.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-prph.h"
#include "iwl-eeprom-parse.h"

#include "mvm.h"
#include "fw-dbg.h"
#include "iwl-phy-db.h"

#define MVM_UCODE_ALIVE_TIMEOUT	HZ
#define MVM_UCODE_CALIB_TIMEOUT	(2*HZ)

#define UCODE_VALID_OK	cpu_to_le32(0x1)

struct iwl_mvm_alive_data {
	bool valid;
	u32 scd_base_addr;
};

static int iwl_send_tx_ant_cfg(struct iwl_mvm *mvm, u8 valid_tx_ant)
{
	struct iwl_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = cpu_to_le32(valid_tx_ant),
	};

	IWL_DEBUG_FW(mvm, "select valid tx ant: %u\n", valid_tx_ant);
	return iwl_mvm_send_cmd_pdu(mvm, TX_ANT_CONFIGURATION_CMD, 0,
				    sizeof(tx_ant_cmd), &tx_ant_cmd);
}

static int iwl_send_rss_cfg_cmd(struct iwl_mvm *mvm)
{
	int i;
	struct iwl_rss_config_cmd cmd = {
		.flags = cpu_to_le32(IWL_RSS_ENABLE),
		.hash_mask = IWL_RSS_HASH_TYPE_IPV4_TCP |
			     IWL_RSS_HASH_TYPE_IPV4_UDP |
			     IWL_RSS_HASH_TYPE_IPV4_PAYLOAD |
			     IWL_RSS_HASH_TYPE_IPV6_TCP |
			     IWL_RSS_HASH_TYPE_IPV6_UDP |
			     IWL_RSS_HASH_TYPE_IPV6_PAYLOAD,
	};

	if (mvm->trans->num_rx_queues == 1)
		return 0;

	/* Do not direct RSS traffic to Q 0 which is our fallback queue */
	for (i = 0; i < ARRAY_SIZE(cmd.indirection_table); i++)
		cmd.indirection_table[i] =
			1 + (i % (mvm->trans->num_rx_queues - 1));
	netdev_rss_key_fill(cmd.secret_key, sizeof(cmd.secret_key));

	return iwl_mvm_send_cmd_pdu(mvm, RSS_CONFIG_CMD, 0, sizeof(cmd), &cmd);
}

static int iwl_mvm_send_dqa_cmd(struct iwl_mvm *mvm)
{
	struct iwl_dqa_enable_cmd dqa_cmd = {
		.cmd_queue = cpu_to_le32(IWL_MVM_DQA_CMD_QUEUE),
	};
	u32 cmd_id = iwl_cmd_id(DQA_ENABLE_CMD, DATA_PATH_GROUP, 0);
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send DQA enabling command: %d\n", ret);
	else
		IWL_DEBUG_FW(mvm, "Working in DQA mode\n");

	return ret;
}

void iwl_free_fw_paging(struct iwl_mvm *mvm)
{
	int i;

	if (!mvm->fw_paging_db[0].fw_paging_block)
		return;

	for (i = 0; i < NUM_OF_FW_PAGING_BLOCKS; i++) {
		struct iwl_fw_paging *paging = &mvm->fw_paging_db[i];

		if (!paging->fw_paging_block) {
			IWL_DEBUG_FW(mvm,
				     "Paging: block %d already freed, continue to next page\n",
				     i);

			continue;
		}
		dma_unmap_page(mvm->trans->dev, paging->fw_paging_phys,
			       paging->fw_paging_size, DMA_BIDIRECTIONAL);

		__free_pages(paging->fw_paging_block,
			     get_order(paging->fw_paging_size));
		paging->fw_paging_block = NULL;
	}
	kfree(mvm->trans->paging_download_buf);
	mvm->trans->paging_download_buf = NULL;
	mvm->trans->paging_db = NULL;

	memset(mvm->fw_paging_db, 0, sizeof(mvm->fw_paging_db));
}

static int iwl_fill_paging_mem(struct iwl_mvm *mvm, const struct fw_img *image)
{
	int sec_idx, idx;
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
		IWL_ERR(mvm, "Paging: Missing CSS and/or paging sections\n");
		iwl_free_fw_paging(mvm);
		return -EINVAL;
	}

	/* copy the CSS block to the dram */
	IWL_DEBUG_FW(mvm, "Paging: load paging CSS to FW, sec = %d\n",
		     sec_idx);

	memcpy(page_address(mvm->fw_paging_db[0].fw_paging_block),
	       image->sec[sec_idx].data,
	       mvm->fw_paging_db[0].fw_paging_size);
	dma_sync_single_for_device(mvm->trans->dev,
				   mvm->fw_paging_db[0].fw_paging_phys,
				   mvm->fw_paging_db[0].fw_paging_size,
				   DMA_BIDIRECTIONAL);

	IWL_DEBUG_FW(mvm,
		     "Paging: copied %d CSS bytes to first block\n",
		     mvm->fw_paging_db[0].fw_paging_size);

	sec_idx++;

	/*
	 * copy the paging blocks to the dram
	 * loop index start from 1 since that CSS block already copied to dram
	 * and CSS index is 0.
	 * loop stop at num_of_paging_blk since that last block is not full.
	 */
	for (idx = 1; idx < mvm->num_of_paging_blk; idx++) {
		struct iwl_fw_paging *block = &mvm->fw_paging_db[idx];

		memcpy(page_address(block->fw_paging_block),
		       image->sec[sec_idx].data + offset,
		       block->fw_paging_size);
		dma_sync_single_for_device(mvm->trans->dev,
					   block->fw_paging_phys,
					   block->fw_paging_size,
					   DMA_BIDIRECTIONAL);


		IWL_DEBUG_FW(mvm,
			     "Paging: copied %d paging bytes to block %d\n",
			     mvm->fw_paging_db[idx].fw_paging_size,
			     idx);

		offset += mvm->fw_paging_db[idx].fw_paging_size;
	}

	/* copy the last paging block */
	if (mvm->num_of_pages_in_last_blk > 0) {
		struct iwl_fw_paging *block = &mvm->fw_paging_db[idx];

		memcpy(page_address(block->fw_paging_block),
		       image->sec[sec_idx].data + offset,
		       FW_PAGING_SIZE * mvm->num_of_pages_in_last_blk);
		dma_sync_single_for_device(mvm->trans->dev,
					   block->fw_paging_phys,
					   block->fw_paging_size,
					   DMA_BIDIRECTIONAL);

		IWL_DEBUG_FW(mvm,
			     "Paging: copied %d pages in the last block %d\n",
			     mvm->num_of_pages_in_last_blk, idx);
	}

	return 0;
}

static int iwl_alloc_fw_paging_mem(struct iwl_mvm *mvm,
				   const struct fw_img *image)
{
	struct page *block;
	dma_addr_t phys = 0;
	int blk_idx, order, num_of_pages, size, dma_enabled;

	if (mvm->fw_paging_db[0].fw_paging_block)
		return 0;

	dma_enabled = is_device_dma_capable(mvm->trans->dev);

	/* ensure BLOCK_2_EXP_SIZE is power of 2 of PAGING_BLOCK_SIZE */
	BUILD_BUG_ON(BIT(BLOCK_2_EXP_SIZE) != PAGING_BLOCK_SIZE);

	num_of_pages = image->paging_mem_size / FW_PAGING_SIZE;
	mvm->num_of_paging_blk =
		DIV_ROUND_UP(num_of_pages, NUM_OF_PAGE_PER_GROUP);
	mvm->num_of_pages_in_last_blk =
		num_of_pages -
		NUM_OF_PAGE_PER_GROUP * (mvm->num_of_paging_blk - 1);

	IWL_DEBUG_FW(mvm,
		     "Paging: allocating mem for %d paging blocks, each block holds 8 pages, last block holds %d pages\n",
		     mvm->num_of_paging_blk,
		     mvm->num_of_pages_in_last_blk);

	/*
	 * Allocate CSS and paging blocks in dram.
	 */
	for (blk_idx = 0; blk_idx < mvm->num_of_paging_blk + 1; blk_idx++) {
		/* For CSS allocate 4KB, for others PAGING_BLOCK_SIZE (32K) */
		size = blk_idx ? PAGING_BLOCK_SIZE : FW_PAGING_SIZE;
		order = get_order(size);
		block = alloc_pages(GFP_KERNEL, order);
		if (!block) {
			/* free all the previous pages since we failed */
			iwl_free_fw_paging(mvm);
			return -ENOMEM;
		}

		mvm->fw_paging_db[blk_idx].fw_paging_block = block;
		mvm->fw_paging_db[blk_idx].fw_paging_size = size;

		if (dma_enabled) {
			phys = dma_map_page(mvm->trans->dev, block, 0,
					    PAGE_SIZE << order,
					    DMA_BIDIRECTIONAL);
			if (dma_mapping_error(mvm->trans->dev, phys)) {
				/*
				 * free the previous pages and the current one
				 * since we failed to map_page.
				 */
				iwl_free_fw_paging(mvm);
				return -ENOMEM;
			}
			mvm->fw_paging_db[blk_idx].fw_paging_phys = phys;
		} else {
			mvm->fw_paging_db[blk_idx].fw_paging_phys =
				PAGING_ADDR_SIG |
				blk_idx << BLOCK_2_EXP_SIZE;
		}

		if (!blk_idx)
			IWL_DEBUG_FW(mvm,
				     "Paging: allocated 4K(CSS) bytes (order %d) for firmware paging.\n",
				     order);
		else
			IWL_DEBUG_FW(mvm,
				     "Paging: allocated 32K bytes (order %d) for firmware paging.\n",
				     order);
	}

	return 0;
}

static int iwl_save_fw_paging(struct iwl_mvm *mvm,
			      const struct fw_img *fw)
{
	int ret;

	ret = iwl_alloc_fw_paging_mem(mvm, fw);
	if (ret)
		return ret;

	return iwl_fill_paging_mem(mvm, fw);
}

/* send paging cmd to FW in case CPU2 has paging image */
static int iwl_send_paging_cmd(struct iwl_mvm *mvm, const struct fw_img *fw)
{
	struct iwl_fw_paging_cmd paging_cmd = {
		.flags =
			cpu_to_le32(PAGING_CMD_IS_SECURED |
				    PAGING_CMD_IS_ENABLED |
				    (mvm->num_of_pages_in_last_blk <<
				    PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = cpu_to_le32(BLOCK_2_EXP_SIZE),
		.block_num = cpu_to_le32(mvm->num_of_paging_blk),
	};
	int blk_idx, size = sizeof(paging_cmd);

	/* A bit hard coded - but this is the old API and will be deprecated */
	if (!iwl_mvm_has_new_tx_api(mvm))
		size -= NUM_OF_FW_PAGING_BLOCKS * 4;

	/* loop for for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < mvm->num_of_paging_blk + 1; blk_idx++) {
		dma_addr_t addr = mvm->fw_paging_db[blk_idx].fw_paging_phys;

		addr = addr >> PAGE_2_EXP_SIZE;

		if (iwl_mvm_has_new_tx_api(mvm)) {
			__le64 phy_addr = cpu_to_le64(addr);

			paging_cmd.device_phy_addr.addr64[blk_idx] = phy_addr;
		} else {
			__le32 phy_addr = cpu_to_le32(addr);

			paging_cmd.device_phy_addr.addr32[blk_idx] = phy_addr;
		}
	}

	return iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(FW_PAGING_BLOCK_CMD,
						    IWL_ALWAYS_LONG_GROUP, 0),
				    0, size, &paging_cmd);
}

/*
 * Send paging item cmd to FW in case CPU2 has paging image
 */
static int iwl_trans_get_paging_item(struct iwl_mvm *mvm)
{
	int ret;
	struct iwl_fw_get_item_cmd fw_get_item_cmd = {
		.item_id = cpu_to_le32(IWL_FW_ITEM_ID_PAGING),
	};

	struct iwl_fw_get_item_resp *item_resp;
	struct iwl_host_cmd cmd = {
		.id = iwl_cmd_id(FW_GET_ITEM_CMD, IWL_ALWAYS_LONG_GROUP, 0),
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &fw_get_item_cmd, },
	};

	cmd.len[0] = sizeof(struct iwl_fw_get_item_cmd);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm,
			"Paging: Failed to send FW_GET_ITEM_CMD cmd (err = %d)\n",
			ret);
		return ret;
	}

	item_resp = (void *)((struct iwl_rx_packet *)cmd.resp_pkt)->data;
	if (item_resp->item_id != cpu_to_le32(IWL_FW_ITEM_ID_PAGING)) {
		IWL_ERR(mvm,
			"Paging: got wrong item in FW_GET_ITEM_CMD resp (item_id = %u)\n",
			le32_to_cpu(item_resp->item_id));
		ret = -EIO;
		goto exit;
	}

	/* Add an extra page for headers */
	mvm->trans->paging_download_buf = kzalloc(PAGING_BLOCK_SIZE +
						  FW_PAGING_SIZE,
						  GFP_KERNEL);
	if (!mvm->trans->paging_download_buf) {
		ret = -ENOMEM;
		goto exit;
	}
	mvm->trans->paging_req_addr = le32_to_cpu(item_resp->item_val);
	mvm->trans->paging_db = mvm->fw_paging_db;
	IWL_DEBUG_FW(mvm,
		     "Paging: got paging request address (paging_req_addr 0x%08x)\n",
		     mvm->trans->paging_req_addr);

exit:
	iwl_free_resp(&cmd);

	return ret;
}

static bool iwl_alive_fn(struct iwl_notif_wait_data *notif_wait,
			 struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_alive_data *alive_data = data;
	struct mvm_alive_resp_v3 *palive3;
	struct mvm_alive_resp *palive;
	struct iwl_umac_alive *umac;
	struct iwl_lmac_alive *lmac1;
	struct iwl_lmac_alive *lmac2 = NULL;
	u16 status;

	if (iwl_rx_packet_payload_len(pkt) == sizeof(*palive)) {
		palive = (void *)pkt->data;
		umac = &palive->umac_data;
		lmac1 = &palive->lmac_data[0];
		lmac2 = &palive->lmac_data[1];
		status = le16_to_cpu(palive->status);
	} else {
		palive3 = (void *)pkt->data;
		umac = &palive3->umac_data;
		lmac1 = &palive3->lmac_data;
		status = le16_to_cpu(palive3->status);
	}

	mvm->error_event_table[0] = le32_to_cpu(lmac1->error_event_table_ptr);
	if (lmac2)
		mvm->error_event_table[1] =
			le32_to_cpu(lmac2->error_event_table_ptr);
	mvm->log_event_table = le32_to_cpu(lmac1->log_event_table_ptr);
	mvm->sf_space.addr = le32_to_cpu(lmac1->st_fwrd_addr);
	mvm->sf_space.size = le32_to_cpu(lmac1->st_fwrd_size);

	mvm->umac_error_event_table = le32_to_cpu(umac->error_info_addr);

	alive_data->scd_base_addr = le32_to_cpu(lmac1->scd_base_ptr);
	alive_data->valid = status == IWL_ALIVE_STATUS_OK;
	if (mvm->umac_error_event_table)
		mvm->support_umac_log = true;

	IWL_DEBUG_FW(mvm,
		     "Alive ucode status 0x%04x revision 0x%01X 0x%01X\n",
		     status, lmac1->ver_type, lmac1->ver_subtype);

	if (lmac2)
		IWL_DEBUG_FW(mvm, "Alive ucode CDB\n");

	IWL_DEBUG_FW(mvm,
		     "UMAC version: Major - 0x%x, Minor - 0x%x\n",
		     le32_to_cpu(umac->umac_major),
		     le32_to_cpu(umac->umac_minor));

	return true;
}

static bool iwl_wait_init_complete(struct iwl_notif_wait_data *notif_wait,
				   struct iwl_rx_packet *pkt, void *data)
{
	WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);

	return true;
}

static bool iwl_wait_phy_db_entry(struct iwl_notif_wait_data *notif_wait,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_phy_db *phy_db = data;

	if (pkt->hdr.cmd != CALIB_RES_NOTIF_PHY_DB) {
		WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);
		return true;
	}

	WARN_ON(iwl_phy_db_set_section(phy_db, pkt));

	return false;
}

static int iwl_mvm_init_paging(struct iwl_mvm *mvm)
{
	const struct fw_img *fw = &mvm->fw->img[mvm->cur_ucode];
	int ret;

	/*
	 * Configure and operate fw paging mechanism.
	 * The driver configures the paging flow only once.
	 * The CPU2 paging image is included in the IWL_UCODE_INIT image.
	 */
	if (!fw->paging_mem_size)
		return 0;

	/*
	 * When dma is not enabled, the driver needs to copy / write
	 * the downloaded / uploaded page to / from the smem.
	 * This gets the location of the place were the pages are
	 * stored.
	 */
	if (!is_device_dma_capable(mvm->trans->dev)) {
		ret = iwl_trans_get_paging_item(mvm);
		if (ret) {
			IWL_ERR(mvm, "failed to get FW paging item\n");
			return ret;
		}
	}

	ret = iwl_save_fw_paging(mvm, fw);
	if (ret) {
		IWL_ERR(mvm, "failed to save the FW paging image\n");
		return ret;
	}

	ret = iwl_send_paging_cmd(mvm, fw);
	if (ret) {
		IWL_ERR(mvm, "failed to send the paging cmd\n");
		iwl_free_fw_paging(mvm);
		return ret;
	}

	return 0;
}
static int iwl_mvm_load_ucode_wait_alive(struct iwl_mvm *mvm,
					 enum iwl_ucode_type ucode_type)
{
	struct iwl_notification_wait alive_wait;
	struct iwl_mvm_alive_data alive_data;
	const struct fw_img *fw;
	int ret, i;
	enum iwl_ucode_type old_type = mvm->cur_ucode;
	static const u16 alive_cmd[] = { MVM_ALIVE };
	struct iwl_sf_region st_fwrd_space;

	if (ucode_type == IWL_UCODE_REGULAR &&
	    iwl_fw_dbg_conf_usniffer(mvm->fw, FW_DBG_START_FROM_ALIVE) &&
	    !(fw_has_capa(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_CAPA_USNIFFER_UNIFIED)))
		fw = iwl_get_ucode_image(mvm->fw, IWL_UCODE_REGULAR_USNIFFER);
	else
		fw = iwl_get_ucode_image(mvm->fw, ucode_type);
	if (WARN_ON(!fw))
		return -EINVAL;
	mvm->cur_ucode = ucode_type;
	mvm->ucode_loaded = false;

	iwl_init_notification_wait(&mvm->notif_wait, &alive_wait,
				   alive_cmd, ARRAY_SIZE(alive_cmd),
				   iwl_alive_fn, &alive_data);

	ret = iwl_trans_start_fw(mvm->trans, fw, ucode_type == IWL_UCODE_INIT);
	if (ret) {
		mvm->cur_ucode = old_type;
		iwl_remove_notification(&mvm->notif_wait, &alive_wait);
		return ret;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &alive_wait,
				    MVM_UCODE_ALIVE_TIMEOUT);
	if (ret) {
		if (mvm->trans->cfg->device_family == IWL_DEVICE_FAMILY_8000)
			IWL_ERR(mvm,
				"SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
				iwl_read_prph(mvm->trans, SB_CPU_1_STATUS),
				iwl_read_prph(mvm->trans, SB_CPU_2_STATUS));
		mvm->cur_ucode = old_type;
		return ret;
	}

	if (!alive_data.valid) {
		IWL_ERR(mvm, "Loaded ucode is not valid!\n");
		mvm->cur_ucode = old_type;
		return -EIO;
	}

	/*
	 * update the sdio allocation according to the pointer we get in the
	 * alive notification.
	 */
	st_fwrd_space.addr = mvm->sf_space.addr;
	st_fwrd_space.size = mvm->sf_space.size;
	ret = iwl_trans_update_sf(mvm->trans, &st_fwrd_space);
	if (ret) {
		IWL_ERR(mvm, "Failed to update SF size. ret %d\n", ret);
		return ret;
	}

	iwl_trans_fw_alive(mvm->trans, alive_data.scd_base_addr);

	/*
	 * Note: all the queues are enabled as part of the interface
	 * initialization, but in firmware restart scenarios they
	 * could be stopped, so wake them up. In firmware restart,
	 * mac80211 will have the queues stopped as well until the
	 * reconfiguration completes. During normal startup, they
	 * will be empty.
	 */

	memset(&mvm->queue_info, 0, sizeof(mvm->queue_info));
	if (iwl_mvm_is_dqa_supported(mvm))
		mvm->queue_info[IWL_MVM_DQA_CMD_QUEUE].hw_queue_refcount = 1;
	else
		mvm->queue_info[IWL_MVM_CMD_QUEUE].hw_queue_refcount = 1;

	for (i = 0; i < IEEE80211_MAX_QUEUES; i++)
		atomic_set(&mvm->mac80211_queue_stop_count[i], 0);

	mvm->ucode_loaded = true;

	return 0;
}

static int iwl_send_phy_cfg_cmd(struct iwl_mvm *mvm)
{
	struct iwl_phy_cfg_cmd phy_cfg_cmd;
	enum iwl_ucode_type ucode_type = mvm->cur_ucode;

	/* Set parameters */
	phy_cfg_cmd.phy_cfg = cpu_to_le32(iwl_mvm_get_phy_config(mvm));
	phy_cfg_cmd.calib_control.event_trigger =
		mvm->fw->default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
		mvm->fw->default_calib[ucode_type].flow_trigger;

	IWL_DEBUG_INFO(mvm, "Sending Phy CFG command: 0x%x\n",
		       phy_cfg_cmd.phy_cfg);

	return iwl_mvm_send_cmd_pdu(mvm, PHY_CONFIGURATION_CMD, 0,
				    sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

int iwl_run_init_mvm_ucode(struct iwl_mvm *mvm, bool read_nvm)
{
	struct iwl_notification_wait calib_wait;
	static const u16 init_complete[] = {
		INIT_COMPLETE_NOTIF,
		CALIB_RES_NOTIF_PHY_DB
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(mvm->calibrating))
		return 0;

	iwl_init_notification_wait(&mvm->notif_wait,
				   &calib_wait,
				   init_complete,
				   ARRAY_SIZE(init_complete),
				   iwl_wait_phy_db_entry,
				   mvm->phy_db);

	/* Will also start the device */
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_INIT);
	if (ret) {
		IWL_ERR(mvm, "Failed to start INIT ucode: %d\n", ret);
		goto error;
	}

	ret = iwl_send_bt_init_conf(mvm);
	if (ret)
		goto error;

	/* Read the NVM only at driver load time, no need to do this twice */
	if (read_nvm) {
		/* Read nvm */
		ret = iwl_nvm_init(mvm, true);
		if (ret) {
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			goto error;
		}
	}

	/* In case we read the NVM from external file, load it to the NIC */
	if (mvm->nvm_file_name)
		iwl_mvm_load_nvm_to_nic(mvm);

	ret = iwl_nvm_check_version(mvm->nvm_data, mvm->trans);
	WARN_ON(ret);

	/*
	 * abort after reading the nvm in case RF Kill is on, we will complete
	 * the init seq later when RF kill will switch to off
	 */
	if (iwl_mvm_is_radio_hw_killed(mvm)) {
		IWL_DEBUG_RF_KILL(mvm,
				  "jump over all phy activities due to RF kill\n");
		iwl_remove_notification(&mvm->notif_wait, &calib_wait);
		ret = 1;
		goto out;
	}

	mvm->calibrating = true;

	/* Send TX valid antennas before triggering calibrations */
	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto error;

	/*
	 * Send phy configurations command to init uCode
	 * to start the 16.0 uCode init image internal calibrations.
	 */
	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n",
			ret);
		goto error;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the calibration complete notification.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &calib_wait,
			MVM_UCODE_CALIB_TIMEOUT);

	if (ret && iwl_mvm_is_radio_hw_killed(mvm)) {
		IWL_DEBUG_RF_KILL(mvm, "RFKILL while calibrating.\n");
		ret = 1;
	}
	goto out;

error:
	iwl_remove_notification(&mvm->notif_wait, &calib_wait);
out:
	mvm->calibrating = false;
	if (iwlmvm_mod_params.init_dbg && !mvm->nvm_data) {
		/* we want to debug INIT and we have no NVM - fake */
		mvm->nvm_data = kzalloc(sizeof(struct iwl_nvm_data) +
					sizeof(struct ieee80211_channel) +
					sizeof(struct ieee80211_rate),
					GFP_KERNEL);
		if (!mvm->nvm_data)
			return -ENOMEM;
		mvm->nvm_data->bands[0].channels = mvm->nvm_data->channels;
		mvm->nvm_data->bands[0].n_channels = 1;
		mvm->nvm_data->bands[0].n_bitrates = 1;
		mvm->nvm_data->bands[0].bitrates =
			(void *)mvm->nvm_data->channels + 1;
		mvm->nvm_data->bands[0].bitrates->hw_value = 10;
	}

	return ret;
}

int iwl_run_unified_mvm_ucode(struct iwl_mvm *mvm, bool read_nvm)
{
	struct iwl_notification_wait init_wait;
	struct iwl_nvm_access_complete_cmd nvm_complete = {};
	static const u16 init_complete[] = {
		INIT_COMPLETE_NOTIF,
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	iwl_init_notification_wait(&mvm->notif_wait,
				   &init_wait,
				   init_complete,
				   ARRAY_SIZE(init_complete),
				   iwl_wait_init_complete,
				   NULL);

	/* Will also start the device */
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
	if (ret) {
		IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}

	/* TODO: remove when integrating context info */
	ret = iwl_mvm_init_paging(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to init paging: %d\n",
			ret);
		goto error;
	}

	/* Read the NVM only at driver load time, no need to do this twice */
	if (read_nvm) {
		/* Read nvm */
		ret = iwl_nvm_init(mvm, true);
		if (ret) {
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			goto error;
		}
	}

	/* In case we read the NVM from external file, load it to the NIC */
	if (mvm->nvm_file_name)
		iwl_mvm_load_nvm_to_nic(mvm);

	ret = iwl_nvm_check_version(mvm->nvm_data, mvm->trans);
	if (WARN_ON(ret))
		goto error;

	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(REGULATORY_AND_NVM_GROUP,
						NVM_ACCESS_COMPLETE), 0,
				   sizeof(nvm_complete), &nvm_complete);
	if (ret) {
		IWL_ERR(mvm, "Failed to run complete NVM access: %d\n",
			ret);
		goto error;
	}

	/* We wait for the INIT complete notification */
	return iwl_wait_notification(&mvm->notif_wait, &init_wait,
				     MVM_UCODE_ALIVE_TIMEOUT);

error:
	iwl_remove_notification(&mvm->notif_wait, &init_wait);
	return ret;
}

static void iwl_mvm_parse_shared_mem_a000(struct iwl_mvm *mvm,
					  struct iwl_rx_packet *pkt)
{
	struct iwl_shared_mem_cfg *mem_cfg = (void *)pkt->data;
	int i;

	mvm->shared_mem_cfg.num_txfifo_entries =
		ARRAY_SIZE(mvm->shared_mem_cfg.txfifo_size);
	for (i = 0; i < ARRAY_SIZE(mem_cfg->txfifo_size); i++)
		mvm->shared_mem_cfg.txfifo_size[i] =
			le32_to_cpu(mem_cfg->txfifo_size[i]);
	for (i = 0; i < ARRAY_SIZE(mvm->shared_mem_cfg.rxfifo_size); i++)
		mvm->shared_mem_cfg.rxfifo_size[i] =
			le32_to_cpu(mem_cfg->rxfifo_size[i]);

	BUILD_BUG_ON(sizeof(mvm->shared_mem_cfg.internal_txfifo_size) !=
		     sizeof(mem_cfg->internal_txfifo_size));

	for (i = 0; i < ARRAY_SIZE(mvm->shared_mem_cfg.internal_txfifo_size);
	     i++)
		mvm->shared_mem_cfg.internal_txfifo_size[i] =
			le32_to_cpu(mem_cfg->internal_txfifo_size[i]);
}

static void iwl_mvm_parse_shared_mem(struct iwl_mvm *mvm,
				     struct iwl_rx_packet *pkt)
{
	struct iwl_shared_mem_cfg_v1 *mem_cfg = (void *)pkt->data;
	int i;

	mvm->shared_mem_cfg.num_txfifo_entries =
		ARRAY_SIZE(mvm->shared_mem_cfg.txfifo_size);
	for (i = 0; i < ARRAY_SIZE(mem_cfg->txfifo_size); i++)
		mvm->shared_mem_cfg.txfifo_size[i] =
			le32_to_cpu(mem_cfg->txfifo_size[i]);
	for (i = 0; i < ARRAY_SIZE(mvm->shared_mem_cfg.rxfifo_size); i++)
		mvm->shared_mem_cfg.rxfifo_size[i] =
			le32_to_cpu(mem_cfg->rxfifo_size[i]);

	/* new API has more data, from rxfifo_addr field and on */
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)) {
		BUILD_BUG_ON(sizeof(mvm->shared_mem_cfg.internal_txfifo_size) !=
			     sizeof(mem_cfg->internal_txfifo_size));

		for (i = 0;
		     i < ARRAY_SIZE(mvm->shared_mem_cfg.internal_txfifo_size);
		     i++)
			mvm->shared_mem_cfg.internal_txfifo_size[i] =
				le32_to_cpu(mem_cfg->internal_txfifo_size[i]);
	}
}

static void iwl_mvm_get_shared_mem_conf(struct iwl_mvm *mvm)
{
	struct iwl_host_cmd cmd = {
		.flags = CMD_WANT_SKB,
		.data = { NULL, },
		.len = { 0, },
	};
	struct iwl_rx_packet *pkt;

	lockdep_assert_held(&mvm->mutex);

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG))
		cmd.id = iwl_cmd_id(SHARED_MEM_CFG_CMD, SYSTEM_GROUP, 0);
	else
		cmd.id = SHARED_MEM_CFG;

	if (WARN_ON(iwl_mvm_send_cmd(mvm, &cmd)))
		return;

	pkt = cmd.resp_pkt;
	if (iwl_mvm_has_new_tx_api(mvm))
		iwl_mvm_parse_shared_mem_a000(mvm, pkt);
	else
		iwl_mvm_parse_shared_mem(mvm, pkt);

	IWL_DEBUG_INFO(mvm, "SHARED MEM CFG: got memory offsets/sizes\n");

	iwl_free_resp(&cmd);
}

static int iwl_mvm_config_ltr(struct iwl_mvm *mvm)
{
	struct iwl_ltr_config_cmd cmd = {
		.flags = cpu_to_le32(LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!mvm->trans->ltr_enabled)
		return 0;

	return iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0,
				    sizeof(cmd), &cmd);
}

#define ACPI_WRDS_METHOD	"WRDS"
#define ACPI_WRDS_WIFI		(0x07)
#define ACPI_WRDS_TABLE_SIZE	10

struct iwl_mvm_sar_table {
	bool enabled;
	u8 values[ACPI_WRDS_TABLE_SIZE];
};

#ifdef CONFIG_ACPI
static int iwl_mvm_sar_get_wrds(struct iwl_mvm *mvm, union acpi_object *wrds,
				struct iwl_mvm_sar_table *sar_table)
{
	union acpi_object *data_pkg;
	u32 i;

	/* We need at least two packages, one for the revision and one
	 * for the data itself.  Also check that the revision is valid
	 * (i.e. it is an integer set to 0).
	*/
	if (wrds->type != ACPI_TYPE_PACKAGE ||
	    wrds->package.count < 2 ||
	    wrds->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    wrds->package.elements[0].integer.value != 0) {
		IWL_DEBUG_RADIO(mvm, "Unsupported wrds structure\n");
		return -EINVAL;
	}

	/* loop through all the packages to find the one for WiFi */
	for (i = 1; i < wrds->package.count; i++) {
		union acpi_object *domain;

		data_pkg = &wrds->package.elements[i];

		/* Skip anything that is not a package with the right
		 * amount of elements (i.e. domain_type,
		 * enabled/disabled plus the sar table size.
		 */
		if (data_pkg->type != ACPI_TYPE_PACKAGE ||
		    data_pkg->package.count != ACPI_WRDS_TABLE_SIZE + 2)
			continue;

		domain = &data_pkg->package.elements[0];
		if (domain->type == ACPI_TYPE_INTEGER &&
		    domain->integer.value == ACPI_WRDS_WIFI)
			break;

		data_pkg = NULL;
	}

	if (!data_pkg)
		return -ENOENT;

	if (data_pkg->package.elements[1].type != ACPI_TYPE_INTEGER)
		return -EINVAL;

	sar_table->enabled = !!(data_pkg->package.elements[1].integer.value);

	for (i = 0; i < ACPI_WRDS_TABLE_SIZE; i++) {
		union acpi_object *entry;

		entry = &data_pkg->package.elements[i + 2];
		if ((entry->type != ACPI_TYPE_INTEGER) ||
		    (entry->integer.value > U8_MAX))
			return -EINVAL;

		sar_table->values[i] = entry->integer.value;
	}

	return 0;
}

static int iwl_mvm_sar_get_table(struct iwl_mvm *mvm,
				 struct iwl_mvm_sar_table *sar_table)
{
	acpi_handle root_handle;
	acpi_handle handle;
	struct acpi_buffer wrds = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;
	int ret;

	root_handle = ACPI_HANDLE(mvm->dev);
	if (!root_handle) {
		IWL_DEBUG_RADIO(mvm,
				"Could not retrieve root port ACPI handle\n");
		return -ENOENT;
	}

	/* Get the method's handle */
	status = acpi_get_handle(root_handle, (acpi_string)ACPI_WRDS_METHOD,
				 &handle);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_RADIO(mvm, "WRDS method not found\n");
		return -ENOENT;
	}

	/* Call WRDS with no arguments */
	status = acpi_evaluate_object(handle, NULL, NULL, &wrds);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_RADIO(mvm, "WRDS invocation failed (0x%x)\n", status);
		return -ENOENT;
	}

	ret = iwl_mvm_sar_get_wrds(mvm, wrds.pointer, sar_table);
	kfree(wrds.pointer);

	return ret;
}
#else /* CONFIG_ACPI */
static int iwl_mvm_sar_get_table(struct iwl_mvm *mvm,
				 struct iwl_mvm_sar_table *sar_table)
{
	return -ENOENT;
}
#endif /* CONFIG_ACPI */

static int iwl_mvm_sar_init(struct iwl_mvm *mvm)
{
	struct iwl_mvm_sar_table sar_table;
	struct iwl_dev_tx_power_cmd cmd = {
		.v3.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_CHAINS),
	};
	int ret, i, j, idx;
	int len = sizeof(cmd);

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TX_POWER_ACK))
		len = sizeof(cmd.v3);

	ret = iwl_mvm_sar_get_table(mvm, &sar_table);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mvm,
				"SAR BIOS table invalid or unavailable. (%d)\n",
				ret);
		/* we don't fail if the table is not available */
		return 0;
	}

	if (!sar_table.enabled)
		return 0;

	IWL_DEBUG_RADIO(mvm, "Sending REDUCE_TX_POWER_CMD per chain\n");

	BUILD_BUG_ON(IWL_NUM_CHAIN_LIMITS * IWL_NUM_SUB_BANDS !=
		     ACPI_WRDS_TABLE_SIZE);

	for (i = 0; i < IWL_NUM_CHAIN_LIMITS; i++) {
		IWL_DEBUG_RADIO(mvm, "  Chain[%d]:\n", i);
		for (j = 0; j < IWL_NUM_SUB_BANDS; j++) {
			idx = (i * IWL_NUM_SUB_BANDS) + j;
			cmd.v3.per_chain_restriction[i][j] =
				cpu_to_le16(sar_table.values[idx]);
			IWL_DEBUG_RADIO(mvm, "    Band[%d] = %d * .125dBm\n",
					j, sar_table.values[idx]);
		}
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, REDUCE_TX_POWER_CMD, 0, len, &cmd);
	if (ret)
		IWL_ERR(mvm, "failed to set per-chain TX power: %d\n", ret);

	return ret;
}

static int iwl_mvm_load_rt_fw(struct iwl_mvm *mvm)
{
	int ret;

	if (iwl_mvm_has_new_tx_api(mvm))
		return iwl_run_unified_mvm_ucode(mvm, false);

	ret = iwl_run_init_mvm_ucode(mvm, false);

	if (iwlmvm_mod_params.init_dbg)
		return 0;

	if (ret) {
		IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);
		/* this can't happen */
		if (WARN_ON(ret > 0))
			ret = -ERFKILL;
		return ret;
	}

	/*
	 * Stop and start the transport without entering low power
	 * mode. This will save the state of other components on the
	 * device that are triggered by the INIT firwmare (MFUART).
	 */
	_iwl_trans_stop_device(mvm->trans, false);
	ret = _iwl_trans_start_hw(mvm->trans, false);
	if (ret)
		return ret;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
	if (ret)
		return ret;

	return iwl_mvm_init_paging(mvm);
}

int iwl_mvm_up(struct iwl_mvm *mvm)
{
	int ret, i;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	ret = iwl_mvm_load_rt_fw(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}

	iwl_mvm_get_shared_mem_conf(mvm);

	ret = iwl_mvm_sf_update(mvm, NULL, false);
	if (ret)
		IWL_ERR(mvm, "Failed to initialize Smart Fifo\n");

	mvm->fw_dbg_conf = FW_DBG_INVALID;
	/* if we have a destination, assume EARLY START */
	if (mvm->fw->dbg_dest_tlv)
		mvm->fw_dbg_conf = FW_DBG_START_FROM_ALIVE;
	iwl_mvm_start_fw_dbg_conf(mvm, FW_DBG_START_FROM_ALIVE);

	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto error;

	ret = iwl_send_bt_init_conf(mvm);
	if (ret)
		goto error;

	/* Send phy db control command and then phy db calibration*/
	if (!iwl_mvm_has_new_tx_api(mvm)) {
		ret = iwl_send_phy_db_data(mvm->phy_db);
		if (ret)
			goto error;

		ret = iwl_send_phy_cfg_cmd(mvm);
		if (ret)
			goto error;
	}

	/* Init RSS configuration */
	if (iwl_mvm_has_new_rx_api(mvm)) {
		ret = iwl_send_rss_cfg_cmd(mvm);
		if (ret) {
			IWL_ERR(mvm, "Failed to configure RSS queues: %d\n",
				ret);
			goto error;
		}
	}

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++)
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);

	mvm->tdls_cs.peer.sta_id = IWL_MVM_STATION_COUNT;

	/* reset quota debouncing buffer - 0xff will yield invalid data */
	memset(&mvm->last_quota_cmd, 0xff, sizeof(mvm->last_quota_cmd));

	/* Enable DQA-mode if required */
	if (iwl_mvm_is_dqa_supported(mvm)) {
		ret = iwl_mvm_send_dqa_cmd(mvm);
		if (ret)
			goto error;
	} else {
		IWL_DEBUG_FW(mvm, "Working in non-DQA mode\n");
	}

	/* Add auxiliary station for scanning */
	ret = iwl_mvm_add_aux_sta(mvm);
	if (ret)
		goto error;

	/* Add all the PHY contexts */
	chan = &mvm->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[0];
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
	for (i = 0; i < NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		ret = iwl_mvm_phy_ctxt_add(mvm, &mvm->phy_ctxts[i],
					   &chandef, 1, 1);
		if (ret)
			goto error;
	}

#ifdef CONFIG_THERMAL
	if (iwl_mvm_is_tt_in_fw(mvm)) {
		/* in order to give the responsibility of ct-kill and
		 * TX backoff to FW we need to send empty temperature reporting
		 * cmd during init time
		 */
		iwl_mvm_send_temp_report_ths_cmd(mvm);
	} else {
		/* Initialize tx backoffs to the minimal possible */
		iwl_mvm_tt_tx_backoff(mvm, 0);
	}

	/* TODO: read the budget from BIOS / Platform NVM */
	if (iwl_mvm_is_ctdp_supported(mvm) && mvm->cooling_dev.cur_state > 0) {
		ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_START,
					   mvm->cooling_dev.cur_state);
		if (ret)
			goto error;
	}
#else
	/* Initialize tx backoffs to the minimal possible */
	iwl_mvm_tt_tx_backoff(mvm, 0);
#endif

	WARN_ON(iwl_mvm_config_ltr(mvm));

	ret = iwl_mvm_power_update_device(mvm);
	if (ret)
		goto error;

	/*
	 * RTNL is not taken during Ct-kill, but we don't need to scan/Tx
	 * anyway, so don't init MCC.
	 */
	if (!test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status)) {
		ret = iwl_mvm_init_mcc(mvm);
		if (ret)
			goto error;
	}

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		mvm->scan_type = IWL_SCAN_TYPE_NOT_SET;
		ret = iwl_mvm_config_scan(mvm);
		if (ret)
			goto error;
	}

	if (iwl_mvm_is_csum_supported(mvm) &&
	    mvm->cfg->features & NETIF_F_RXCSUM)
		iwl_trans_write_prph(mvm->trans, RX_EN_CSUM, 0x3);

	/* allow FW/transport low power modes if not during restart */
	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		iwl_mvm_unref(mvm, IWL_MVM_REF_UCODE_DOWN);

	ret = iwl_mvm_sar_init(mvm);
	if (ret)
		goto error;

	IWL_DEBUG_INFO(mvm, "RT uCode started.\n");
	return 0;
 error:
	iwl_mvm_stop_device(mvm);
	return ret;
}

int iwl_mvm_load_d3_fw(struct iwl_mvm *mvm)
{
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_WOWLAN);
	if (ret) {
		IWL_ERR(mvm, "Failed to start WoWLAN firmware: %d\n", ret);
		goto error;
	}

	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto error;

	/* Send phy db control command and then phy db calibration*/
	ret = iwl_send_phy_db_data(mvm->phy_db);
	if (ret)
		goto error;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret)
		goto error;

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++)
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);

	/* Add auxiliary station for scanning */
	ret = iwl_mvm_add_aux_sta(mvm);
	if (ret)
		goto error;

	return 0;
 error:
	iwl_mvm_stop_device(mvm);
	return ret;
}

void iwl_mvm_rx_card_state_notif(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_card_state_notif *card_state_notif = (void *)pkt->data;
	u32 flags = le32_to_cpu(card_state_notif->flags);

	IWL_DEBUG_RF_KILL(mvm, "Card state received: HW:%s SW:%s CT:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & CT_KILL_CARD_DISABLED) ?
			  "Reached" : "Not reached");
}

void iwl_mvm_rx_mfuart_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mfuart_load_notif *mfuart_notif = (void *)pkt->data;

	IWL_DEBUG_INFO(mvm,
		       "MFUART: installed ver: 0x%08x, external ver: 0x%08x, status: 0x%08x, duration: 0x%08x\n",
		       le32_to_cpu(mfuart_notif->installed_ver),
		       le32_to_cpu(mfuart_notif->external_ver),
		       le32_to_cpu(mfuart_notif->status),
		       le32_to_cpu(mfuart_notif->duration));

	if (iwl_rx_packet_payload_len(pkt) == sizeof(*mfuart_notif))
		IWL_DEBUG_INFO(mvm,
			       "MFUART: image size: 0x%08x\n",
			       le32_to_cpu(mfuart_notif->image_size));
}
