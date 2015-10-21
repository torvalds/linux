/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
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

#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-fw.h"
#include "iwl-debug.h"
#include "iwl-csr.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-io.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-prph.h"
#include "iwl-eeprom-parse.h"

#include "mvm.h"
#include "iwl-phy-db.h"

#define MVM_UCODE_ALIVE_TIMEOUT	HZ
#define MVM_UCODE_CALIB_TIMEOUT	(2*HZ)

#define UCODE_VALID_OK	cpu_to_le32(0x1)

struct iwl_mvm_alive_data {
	bool valid;
	u32 scd_base_addr;
};

static inline const struct fw_img *
iwl_get_ucode_image(struct iwl_mvm *mvm, enum iwl_ucode_type ucode_type)
{
	if (ucode_type >= IWL_UCODE_TYPE_MAX)
		return NULL;

	return &mvm->fw->img[ucode_type];
}

static int iwl_send_tx_ant_cfg(struct iwl_mvm *mvm, u8 valid_tx_ant)
{
	struct iwl_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = cpu_to_le32(valid_tx_ant),
	};

	IWL_DEBUG_FW(mvm, "select valid tx ant: %u\n", valid_tx_ant);
	return iwl_mvm_send_cmd_pdu(mvm, TX_ANT_CONFIGURATION_CMD, 0,
				    sizeof(tx_ant_cmd), &tx_ant_cmd);
}

static void iwl_free_fw_paging(struct iwl_mvm *mvm)
{
	int i;

	if (!mvm->fw_paging_db[0].fw_paging_block)
		return;

	for (i = 0; i < NUM_OF_FW_PAGING_BLOCKS; i++) {
		if (!mvm->fw_paging_db[i].fw_paging_block) {
			IWL_DEBUG_FW(mvm,
				     "Paging: block %d already freed, continue to next page\n",
				     i);

			continue;
		}

		__free_pages(mvm->fw_paging_db[i].fw_paging_block,
			     get_order(mvm->fw_paging_db[i].fw_paging_size));
	}
	kfree(mvm->trans->paging_download_buf);
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
	for (sec_idx = 0; sec_idx < IWL_UCODE_SECTION_MAX; sec_idx++) {
		if (image->sec[sec_idx].offset == PAGING_SEPARATOR_SECTION) {
			sec_idx++;
			break;
		}
	}

	if (sec_idx >= IWL_UCODE_SECTION_MAX) {
		IWL_ERR(mvm, "driver didn't find paging image\n");
		iwl_free_fw_paging(mvm);
		return -EINVAL;
	}

	/* copy the CSS block to the dram */
	IWL_DEBUG_FW(mvm, "Paging: load paging CSS to FW, sec = %d\n",
		     sec_idx);

	memcpy(page_address(mvm->fw_paging_db[0].fw_paging_block),
	       image->sec[sec_idx].data,
	       mvm->fw_paging_db[0].fw_paging_size);

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
		memcpy(page_address(mvm->fw_paging_db[idx].fw_paging_block),
		       image->sec[sec_idx].data + offset,
		       mvm->fw_paging_db[idx].fw_paging_size);

		IWL_DEBUG_FW(mvm,
			     "Paging: copied %d paging bytes to block %d\n",
			     mvm->fw_paging_db[idx].fw_paging_size,
			     idx);

		offset += mvm->fw_paging_db[idx].fw_paging_size;
	}

	/* copy the last paging block */
	if (mvm->num_of_pages_in_last_blk > 0) {
		memcpy(page_address(mvm->fw_paging_db[idx].fw_paging_block),
		       image->sec[sec_idx].data + offset,
		       FW_PAGING_SIZE * mvm->num_of_pages_in_last_blk);

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
	int blk_idx = 0;
	int order, num_of_pages;
	int dma_enabled;

	if (mvm->fw_paging_db[0].fw_paging_block)
		return 0;

	dma_enabled = is_device_dma_capable(mvm->trans->dev);

	/* ensure BLOCK_2_EXP_SIZE is power of 2 of PAGING_BLOCK_SIZE */
	BUILD_BUG_ON(BIT(BLOCK_2_EXP_SIZE) != PAGING_BLOCK_SIZE);

	num_of_pages = image->paging_mem_size / FW_PAGING_SIZE;
	mvm->num_of_paging_blk = ((num_of_pages - 1) /
				    NUM_OF_PAGE_PER_GROUP) + 1;

	mvm->num_of_pages_in_last_blk =
		num_of_pages -
		NUM_OF_PAGE_PER_GROUP * (mvm->num_of_paging_blk - 1);

	IWL_DEBUG_FW(mvm,
		     "Paging: allocating mem for %d paging blocks, each block holds 8 pages, last block holds %d pages\n",
		     mvm->num_of_paging_blk,
		     mvm->num_of_pages_in_last_blk);

	/* allocate block of 4Kbytes for paging CSS */
	order = get_order(FW_PAGING_SIZE);
	block = alloc_pages(GFP_KERNEL, order);
	if (!block) {
		/* free all the previous pages since we failed */
		iwl_free_fw_paging(mvm);
		return -ENOMEM;
	}

	mvm->fw_paging_db[blk_idx].fw_paging_block = block;
	mvm->fw_paging_db[blk_idx].fw_paging_size = FW_PAGING_SIZE;

	if (dma_enabled) {
		phys = dma_map_page(mvm->trans->dev, block, 0,
				    PAGE_SIZE << order, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(mvm->trans->dev, phys)) {
			/*
			 * free the previous pages and the current one since
			 * we failed to map_page.
			 */
			iwl_free_fw_paging(mvm);
			return -ENOMEM;
		}
		mvm->fw_paging_db[blk_idx].fw_paging_phys = phys;
	} else {
		mvm->fw_paging_db[blk_idx].fw_paging_phys = PAGING_ADDR_SIG |
			blk_idx << BLOCK_2_EXP_SIZE;
	}

	IWL_DEBUG_FW(mvm,
		     "Paging: allocated 4K(CSS) bytes (order %d) for firmware paging.\n",
		     order);

	/*
	 * allocate blocks in dram.
	 * since that CSS allocated in fw_paging_db[0] loop start from index 1
	 */
	for (blk_idx = 1; blk_idx < mvm->num_of_paging_blk + 1; blk_idx++) {
		/* allocate block of PAGING_BLOCK_SIZE (32K) */
		order = get_order(PAGING_BLOCK_SIZE);
		block = alloc_pages(GFP_KERNEL, order);
		if (!block) {
			/* free all the previous pages since we failed */
			iwl_free_fw_paging(mvm);
			return -ENOMEM;
		}

		mvm->fw_paging_db[blk_idx].fw_paging_block = block;
		mvm->fw_paging_db[blk_idx].fw_paging_size = PAGING_BLOCK_SIZE;

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
	int blk_idx;
	__le32 dev_phy_addr;
	struct iwl_fw_paging_cmd fw_paging_cmd = {
		.flags =
			cpu_to_le32(PAGING_CMD_IS_SECURED |
				    PAGING_CMD_IS_ENABLED |
				    (mvm->num_of_pages_in_last_blk <<
				    PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = cpu_to_le32(BLOCK_2_EXP_SIZE),
		.block_num = cpu_to_le32(mvm->num_of_paging_blk),
	};

	/* loop for for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < mvm->num_of_paging_blk + 1; blk_idx++) {
		dev_phy_addr =
			cpu_to_le32(mvm->fw_paging_db[blk_idx].fw_paging_phys >>
				    PAGE_2_EXP_SIZE);
		fw_paging_cmd.device_phy_addr[blk_idx] = dev_phy_addr;
	}

	return iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(FW_PAGING_BLOCK_CMD,
						    IWL_ALWAYS_LONG_GROUP, 0),
				    0, sizeof(fw_paging_cmd), &fw_paging_cmd);
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

	mvm->trans->paging_download_buf = kzalloc(MAX_PAGING_IMAGE_SIZE,
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
	struct mvm_alive_resp_ver1 *palive1;
	struct mvm_alive_resp_ver2 *palive2;
	struct mvm_alive_resp *palive;

	if (iwl_rx_packet_payload_len(pkt) == sizeof(*palive1)) {
		palive1 = (void *)pkt->data;

		mvm->support_umac_log = false;
		mvm->error_event_table =
			le32_to_cpu(palive1->error_event_table_ptr);
		mvm->log_event_table =
			le32_to_cpu(palive1->log_event_table_ptr);
		alive_data->scd_base_addr = le32_to_cpu(palive1->scd_base_ptr);

		alive_data->valid = le16_to_cpu(palive1->status) ==
				    IWL_ALIVE_STATUS_OK;
		IWL_DEBUG_FW(mvm,
			     "Alive VER1 ucode status 0x%04x revision 0x%01X 0x%01X flags 0x%01X\n",
			     le16_to_cpu(palive1->status), palive1->ver_type,
			     palive1->ver_subtype, palive1->flags);
	} else if (iwl_rx_packet_payload_len(pkt) == sizeof(*palive2)) {
		palive2 = (void *)pkt->data;

		mvm->error_event_table =
			le32_to_cpu(palive2->error_event_table_ptr);
		mvm->log_event_table =
			le32_to_cpu(palive2->log_event_table_ptr);
		alive_data->scd_base_addr = le32_to_cpu(palive2->scd_base_ptr);
		mvm->umac_error_event_table =
			le32_to_cpu(palive2->error_info_addr);
		mvm->sf_space.addr = le32_to_cpu(palive2->st_fwrd_addr);
		mvm->sf_space.size = le32_to_cpu(palive2->st_fwrd_size);

		alive_data->valid = le16_to_cpu(palive2->status) ==
				    IWL_ALIVE_STATUS_OK;
		if (mvm->umac_error_event_table)
			mvm->support_umac_log = true;

		IWL_DEBUG_FW(mvm,
			     "Alive VER2 ucode status 0x%04x revision 0x%01X 0x%01X flags 0x%01X\n",
			     le16_to_cpu(palive2->status), palive2->ver_type,
			     palive2->ver_subtype, palive2->flags);

		IWL_DEBUG_FW(mvm,
			     "UMAC version: Major - 0x%x, Minor - 0x%x\n",
			     palive2->umac_major, palive2->umac_minor);
	} else if (iwl_rx_packet_payload_len(pkt) == sizeof(*palive)) {
		palive = (void *)pkt->data;

		mvm->error_event_table =
			le32_to_cpu(palive->error_event_table_ptr);
		mvm->log_event_table =
			le32_to_cpu(palive->log_event_table_ptr);
		alive_data->scd_base_addr = le32_to_cpu(palive->scd_base_ptr);
		mvm->umac_error_event_table =
			le32_to_cpu(palive->error_info_addr);
		mvm->sf_space.addr = le32_to_cpu(palive->st_fwrd_addr);
		mvm->sf_space.size = le32_to_cpu(palive->st_fwrd_size);

		alive_data->valid = le16_to_cpu(palive->status) ==
				    IWL_ALIVE_STATUS_OK;
		if (mvm->umac_error_event_table)
			mvm->support_umac_log = true;

		IWL_DEBUG_FW(mvm,
			     "Alive VER3 ucode status 0x%04x revision 0x%01X 0x%01X flags 0x%01X\n",
			     le16_to_cpu(palive->status), palive->ver_type,
			     palive->ver_subtype, palive->flags);

		IWL_DEBUG_FW(mvm,
			     "UMAC version: Major - 0x%x, Minor - 0x%x\n",
			     le32_to_cpu(palive->umac_major),
			     le32_to_cpu(palive->umac_minor));
	}

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

	WARN_ON(iwl_phy_db_set_section(phy_db, pkt, GFP_ATOMIC));

	return false;
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
	    iwl_fw_dbg_conf_usniffer(mvm->fw, FW_DBG_START_FROM_ALIVE))
		fw = iwl_get_ucode_image(mvm, IWL_UCODE_REGULAR_USNIFFER);
	else
		fw = iwl_get_ucode_image(mvm, ucode_type);
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
	 * configure and operate fw paging mechanism.
	 * driver configures the paging flow only once, CPU2 paging image
	 * included in the IWL_UCODE_INIT image.
	 */
	if (fw->paging_mem_size) {
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
	}

	/*
	 * Note: all the queues are enabled as part of the interface
	 * initialization, but in firmware restart scenarios they
	 * could be stopped, so wake them up. In firmware restart,
	 * mac80211 will have the queues stopped as well until the
	 * reconfiguration completes. During normal startup, they
	 * will be empty.
	 */

	for (i = 0; i < IWL_MAX_HW_QUEUES; i++) {
		if (i < mvm->first_agg_queue && i != IWL_MVM_CMD_QUEUE)
			mvm->queue_to_mac80211[i] = i;
		else
			mvm->queue_to_mac80211[i] = IWL_INVALID_MAC80211_QUEUE;
	}

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

static void iwl_mvm_get_shared_mem_conf(struct iwl_mvm *mvm)
{
	struct iwl_host_cmd cmd = {
		.id = SHARED_MEM_CFG,
		.flags = CMD_WANT_SKB,
		.data = { NULL, },
		.len = { 0, },
	};
	struct iwl_rx_packet *pkt;
	struct iwl_shared_mem_cfg *mem_cfg;
	u32 i;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_send_cmd(mvm, &cmd)))
		return;

	pkt = cmd.resp_pkt;
	mem_cfg = (void *)pkt->data;

	mvm->shared_mem_cfg.shared_mem_addr =
		le32_to_cpu(mem_cfg->shared_mem_addr);
	mvm->shared_mem_cfg.shared_mem_size =
		le32_to_cpu(mem_cfg->shared_mem_size);
	mvm->shared_mem_cfg.sample_buff_addr =
		le32_to_cpu(mem_cfg->sample_buff_addr);
	mvm->shared_mem_cfg.sample_buff_size =
		le32_to_cpu(mem_cfg->sample_buff_size);
	mvm->shared_mem_cfg.txfifo_addr = le32_to_cpu(mem_cfg->txfifo_addr);
	for (i = 0; i < ARRAY_SIZE(mvm->shared_mem_cfg.txfifo_size); i++)
		mvm->shared_mem_cfg.txfifo_size[i] =
			le32_to_cpu(mem_cfg->txfifo_size[i]);
	for (i = 0; i < ARRAY_SIZE(mvm->shared_mem_cfg.rxfifo_size); i++)
		mvm->shared_mem_cfg.rxfifo_size[i] =
			le32_to_cpu(mem_cfg->rxfifo_size[i]);
	mvm->shared_mem_cfg.page_buff_addr =
		le32_to_cpu(mem_cfg->page_buff_addr);
	mvm->shared_mem_cfg.page_buff_size =
		le32_to_cpu(mem_cfg->page_buff_size);
	IWL_DEBUG_INFO(mvm, "SHARED MEM CFG: got memory offsets/sizes\n");

	iwl_free_resp(&cmd);
}

int iwl_mvm_fw_dbg_collect_desc(struct iwl_mvm *mvm,
				struct iwl_mvm_dump_desc *desc,
				struct iwl_fw_dbg_trigger_tlv *trigger)
{
	unsigned int delay = 0;

	if (trigger)
		delay = msecs_to_jiffies(le32_to_cpu(trigger->stop_delay));

	if (test_and_set_bit(IWL_MVM_STATUS_DUMPING_FW_LOG, &mvm->status))
		return -EBUSY;

	if (WARN_ON(mvm->fw_dump_desc))
		iwl_mvm_free_fw_dump_desc(mvm);

	IWL_WARN(mvm, "Collecting data: trigger %d fired.\n",
		 le32_to_cpu(desc->trig_desc.type));

	mvm->fw_dump_desc = desc;
	mvm->fw_dump_trig = trigger;

	queue_delayed_work(system_wq, &mvm->fw_dump_wk, delay);

	return 0;
}

int iwl_mvm_fw_dbg_collect(struct iwl_mvm *mvm, enum iwl_fw_dbg_trigger trig,
			   const char *str, size_t len,
			   struct iwl_fw_dbg_trigger_tlv *trigger)
{
	struct iwl_mvm_dump_desc *desc;

	desc = kzalloc(sizeof(*desc) + len, GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;

	desc->len = len;
	desc->trig_desc.type = cpu_to_le32(trig);
	memcpy(desc->trig_desc.data, str, len);

	return iwl_mvm_fw_dbg_collect_desc(mvm, desc, trigger);
}

int iwl_mvm_fw_dbg_collect_trig(struct iwl_mvm *mvm,
				struct iwl_fw_dbg_trigger_tlv *trigger,
				const char *fmt, ...)
{
	u16 occurrences = le16_to_cpu(trigger->occurrences);
	int ret, len = 0;
	char buf[64];

	if (!occurrences)
		return 0;

	if (fmt) {
		va_list ap;

		buf[sizeof(buf) - 1] = '\0';

		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		/* check for truncation */
		if (WARN_ON_ONCE(buf[sizeof(buf) - 1]))
			buf[sizeof(buf) - 1] = '\0';

		len = strlen(buf) + 1;
	}

	ret = iwl_mvm_fw_dbg_collect(mvm, le32_to_cpu(trigger->id), buf, len,
				     trigger);

	if (ret)
		return ret;

	trigger->occurrences = cpu_to_le16(occurrences - 1);
	return 0;
}

static inline void iwl_mvm_restart_early_start(struct iwl_mvm *mvm)
{
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000)
		iwl_clear_bits_prph(mvm->trans, MON_BUFF_SAMPLE_CTL, 0x100);
	else
		iwl_write_prph(mvm->trans, DBGC_IN_SAMPLE, 1);
}

int iwl_mvm_start_fw_dbg_conf(struct iwl_mvm *mvm, u8 conf_id)
{
	u8 *ptr;
	int ret;
	int i;

	if (WARN_ONCE(conf_id >= ARRAY_SIZE(mvm->fw->dbg_conf_tlv),
		      "Invalid configuration %d\n", conf_id))
		return -EINVAL;

	/* EARLY START - firmware's configuration is hard coded */
	if ((!mvm->fw->dbg_conf_tlv[conf_id] ||
	     !mvm->fw->dbg_conf_tlv[conf_id]->num_of_hcmds) &&
	    conf_id == FW_DBG_START_FROM_ALIVE) {
		iwl_mvm_restart_early_start(mvm);
		return 0;
	}

	if (!mvm->fw->dbg_conf_tlv[conf_id])
		return -EINVAL;

	if (mvm->fw_dbg_conf != FW_DBG_INVALID)
		IWL_WARN(mvm, "FW already configured (%d) - re-configuring\n",
			 mvm->fw_dbg_conf);

	/* Send all HCMDs for configuring the FW debug */
	ptr = (void *)&mvm->fw->dbg_conf_tlv[conf_id]->hcmd;
	for (i = 0; i < mvm->fw->dbg_conf_tlv[conf_id]->num_of_hcmds; i++) {
		struct iwl_fw_dbg_conf_hcmd *cmd = (void *)ptr;

		ret = iwl_mvm_send_cmd_pdu(mvm, cmd->id, 0,
					   le16_to_cpu(cmd->len), cmd->data);
		if (ret)
			return ret;

		ptr += sizeof(*cmd);
		ptr += le16_to_cpu(cmd->len);
	}

	mvm->fw_dbg_conf = conf_id;
	return ret;
}

static int iwl_mvm_config_ltr_v1(struct iwl_mvm *mvm)
{
	struct iwl_ltr_config_cmd_v1 cmd_v1 = {
		.flags = cpu_to_le32(LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!mvm->trans->ltr_enabled)
		return 0;

	return iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0,
				    sizeof(cmd_v1), &cmd_v1);
}

static int iwl_mvm_config_ltr(struct iwl_mvm *mvm)
{
	struct iwl_ltr_config_cmd cmd = {
		.flags = cpu_to_le32(LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!mvm->trans->ltr_enabled)
		return 0;

	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_HDC_PHASE_0))
		return iwl_mvm_config_ltr_v1(mvm);

	return iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0,
				    sizeof(cmd), &cmd);
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

	/*
	 * If we haven't completed the run of the init ucode during
	 * module loading, load init ucode now
	 * (for example, if we were in RFKILL)
	 */
	ret = iwl_run_init_mvm_ucode(mvm, false);
	if (ret && !iwlmvm_mod_params.init_dbg) {
		IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);
		/* this can't happen */
		if (WARN_ON(ret > 0))
			ret = -ERFKILL;
		goto error;
	}
	if (!iwlmvm_mod_params.init_dbg) {
		/*
		 * Stop and start the transport without entering low power
		 * mode. This will save the state of other components on the
		 * device that are triggered by the INIT firwmare (MFUART).
		 */
		_iwl_trans_stop_device(mvm->trans, false);
		ret = _iwl_trans_start_hw(mvm->trans, false);
		if (ret)
			goto error;
	}

	if (iwlmvm_mod_params.init_dbg)
		return 0;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
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
	ret = iwl_send_phy_db_data(mvm->phy_db);
	if (ret)
		goto error;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret)
		goto error;

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++)
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);

	mvm->tdls_cs.peer.sta_id = IWL_MVM_STATION_COUNT;

	/* reset quota debouncing buffer - 0xff will yield invalid data */
	memset(&mvm->last_quota_cmd, 0xff, sizeof(mvm->last_quota_cmd));

	/* Add auxiliary station for scanning */
	ret = iwl_mvm_add_aux_sta(mvm);
	if (ret)
		goto error;

	/* Add all the PHY contexts */
	chan = &mvm->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->channels[0];
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

	/* Initialize tx backoffs to the minimal possible */
	iwl_mvm_tt_tx_backoff(mvm, 0);

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

	IWL_DEBUG_INFO(mvm, "RT uCode started.\n");
	return 0;
 error:
	iwl_trans_stop_device(mvm->trans);
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
	iwl_trans_stop_device(mvm->trans);
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
}
