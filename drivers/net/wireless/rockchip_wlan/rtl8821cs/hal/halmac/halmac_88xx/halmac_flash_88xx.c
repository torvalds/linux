/******************************************************************************
 *
 * Copyright(c) 2017 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_flash_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_common_88xx.h"

#if HALMAC_88XX_SUPPORT

/**
 * download_flash_88xx() -download firmware to flash
 * @adapter : the adapter of halmac
 * @fw_bin : pointer to fw
 * @size : fw size
 * @rom_addr : flash start address where fw should be download
 * Author : Pablo Chiu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
download_flash_88xx(struct halmac_adapter *adapter, u8 *fw_bin, u32 size,
		    u32 rom_addr)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status rc;
	struct halmac_h2c_header_info hdr_info;
	u8 value8;
	u8 restore[3];
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = {0};
	u16 seq_num = 0;
	u16 h2c_info_offset;
	u32 pkt_size;
	u32 mem_offset;
	u32 cnt;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value8 = HALMAC_REG_R8(REG_CR + 1);
	restore[0] = value8;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_W8(REG_CR + 1, value8);

	value8 = HALMAC_REG_R8(REG_BCN_CTRL);
	restore[1] = value8;
	value8 = (u8)((value8 & ~(BIT(3))) | BIT(4));
	HALMAC_REG_W8(REG_BCN_CTRL, value8);

	value8 = HALMAC_REG_R8(REG_FWHW_TXQ_CTRL + 2);
	restore[2] = value8;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, value8);

	/* Download FW to Flash flow */
	h2c_info_offset = adapter->txff_alloc.rsvd_h2c_info_addr -
					adapter->txff_alloc.rsvd_boundary;
	mem_offset = 0;

	while (size != 0) {
		if (size >= (DL_FLASH_RSVDPG_SIZE - 48))
			pkt_size = DL_FLASH_RSVDPG_SIZE - 48;
		else
			pkt_size = size;

		rc = dl_rsvd_page_88xx(adapter,
				       adapter->txff_alloc.rsvd_h2c_info_addr,
				       fw_bin + mem_offset, pkt_size);
		if (rc != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]dl rsvd pg!!\n");
			return rc;
		}

		DOWNLOAD_FLASH_SET_SPI_CMD(h2c_buf, 0x02);
		DOWNLOAD_FLASH_SET_LOCATION(h2c_buf, h2c_info_offset);
		DOWNLOAD_FLASH_SET_SIZE(h2c_buf, pkt_size);
		DOWNLOAD_FLASH_SET_START_ADDR(h2c_buf, rom_addr);

		hdr_info.sub_cmd_id = SUB_CMD_ID_DOWNLOAD_FLASH;
		hdr_info.content_size = 20;
		hdr_info.ack = 1;
		set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

		rc = send_h2c_pkt_88xx(adapter, h2c_buf);

		if (rc != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]send h2c!!\n");
			return rc;
		}

		value8 = HALMAC_REG_R8(REG_MCUTST_I);
		value8 |= BIT(0);
		HALMAC_REG_W8(REG_MCUTST_I, value8);

		rom_addr += pkt_size;
		mem_offset += pkt_size;
		size -= pkt_size;

		cnt = 1000;
		while (((HALMAC_REG_R8(REG_MCUTST_I)) & BIT(0)) != 0) {
			if (cnt == 0) {
				PLTFM_MSG_ERR("[ERR]dl flash!!\n");
				return  HALMAC_RET_DLFW_FAIL;
			}
			cnt--;
			PLTFM_DELAY_US(1000);
		}
	}

	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, restore[2]);
	HALMAC_REG_W8(REG_BCN_CTRL, restore[1]);
	HALMAC_REG_W8(REG_CR + 1, restore[0]);
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * read_flash_88xx() -read data from flash
 * @adapter : the adapter of halmac
 * @addr : flash start address where fw should be read
 * Author : Pablo Chiu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
read_flash_88xx(struct halmac_adapter *adapter, u32 addr, u32 length, u8 *data)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status;
	struct halmac_h2c_header_info hdr_info;
	u8 value8;
	u8 restore[3];
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = {0};
	u16 seq_num = 0;
	u16 h2c_pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;
	u16 rsvd_pg_addr = adapter->txff_alloc.rsvd_boundary;
	u16 h2c_info_addr;
	u32 cnt;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value8 = HALMAC_REG_R8(REG_CR + 1);
	restore[0] = value8;
	value8 = (u8)(value8 | BIT(0));
	HALMAC_REG_W8(REG_CR + 1, value8);

	value8 = HALMAC_REG_R8(REG_BCN_CTRL);
	restore[1] = value8;
	value8 = (u8)((value8 & ~(BIT(3))) | BIT(4));
	HALMAC_REG_W8(REG_BCN_CTRL, value8);

	value8 = HALMAC_REG_R8(REG_FWHW_TXQ_CTRL + 2);
	restore[2] = value8;
	value8 = (u8)(value8 & ~(BIT(6)));
	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, value8);

	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2, h2c_pg_addr);
	value8 = HALMAC_REG_R8(REG_MCUTST_I);
	value8 |= BIT(0);
	HALMAC_REG_W8(REG_MCUTST_I, value8);

	/* Construct H2C Content */
	DOWNLOAD_FLASH_SET_SPI_CMD(h2c_buf, 0x03);
	DOWNLOAD_FLASH_SET_LOCATION(h2c_buf, h2c_pg_addr - rsvd_pg_addr);
	DOWNLOAD_FLASH_SET_SIZE(h2c_buf, length);
	DOWNLOAD_FLASH_SET_START_ADDR(h2c_buf, addr);

	/* Fill in H2C Header */
	hdr_info.sub_cmd_id = SUB_CMD_ID_DOWNLOAD_FLASH;
	hdr_info.content_size = 16;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	/* Send H2C Cmd Packet */
	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");
		return status;
	}

	cnt = 5000;
	while (((HALMAC_REG_R8(REG_MCUTST_I)) & BIT(0)) != 0) {
		if (cnt == 0) {
			PLTFM_MSG_ERR("[ERR]read flash!!\n");
			return  HALMAC_RET_FAIL;
		}
		cnt--;
		PLTFM_DELAY_US(1000);
	}

	HALMAC_REG_W8_CLR(REG_MCUTST_I, BIT(0));

	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2, rsvd_pg_addr);
	HALMAC_REG_W8(REG_FWHW_TXQ_CTRL + 2, restore[2]);
	HALMAC_REG_W8(REG_BCN_CTRL, restore[1]);
	HALMAC_REG_W8(REG_CR + 1, restore[0]);

	h2c_info_addr = h2c_pg_addr << TX_PAGE_SIZE_SHIFT_88XX;
	status = dump_fifo_88xx(adapter, HAL_FIFO_SEL_TX, h2c_info_addr,
				length, data);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]dump fifo!!\n");
		return status;
	}
	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * erase_flash_88xx() -erase flash data
 * @adapter : the adapter of halmac
 * @erase_cmd : erase command
 * @addr : flash start address where fw should be erased
 * Author : Pablo Chiu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
erase_flash_88xx(struct halmac_adapter *adapter, u8 erase_cmd, u32 addr)
{
	enum halmac_ret_status status;
	struct halmac_h2c_header_info hdr_info;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 value8;
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = {0};
	u16 seq_num = 0;
	u32 cnt;

	/* Construct H2C Content */
	DOWNLOAD_FLASH_SET_SPI_CMD(h2c_buf, erase_cmd);
	DOWNLOAD_FLASH_SET_LOCATION(h2c_buf, 0);
	DOWNLOAD_FLASH_SET_START_ADDR(h2c_buf, addr);
	DOWNLOAD_FLASH_SET_SIZE(h2c_buf, 0);

	value8 = HALMAC_REG_R8(REG_MCUTST_I);
	value8 |= BIT(0);
	HALMAC_REG_W8(REG_MCUTST_I, value8);

	/* Fill in H2C Header */
	hdr_info.sub_cmd_id = SUB_CMD_ID_DOWNLOAD_FLASH;
	hdr_info.content_size = 16;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);

	/* Send H2C Cmd Packet */
	status = send_h2c_pkt_88xx(adapter, h2c_buf);

	if (status != HALMAC_RET_SUCCESS)
		PLTFM_MSG_ERR("[ERR]send h2c!!\n");

	cnt = 5000;
	while (((HALMAC_REG_R8(REG_MCUTST_I)) & BIT(0)) != 0 && cnt != 0) {
		PLTFM_DELAY_US(1000);
		cnt--;
	}

	if (cnt == 0)
		return HALMAC_RET_FAIL;
	else
		return HALMAC_RET_SUCCESS;
}

/**
 * check_flash_88xx() -check flash data
 * @adapter : the adapter of halmac
 * @fw_bin : pointer to fw
 * @size : fw size
 * @addr : flash start address where fw should be checked
 * Author : Pablo Chiu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
check_flash_88xx(struct halmac_adapter *adapter, u8 *fw_bin, u32 size,
		 u32 addr)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 value8;
	u16 i;
	u16 residue;
	u16 pg_addr;
	u32 pkt_size;
	u32 start_page;
	u32 cnt;
	u8 *data;

	pg_addr = adapter->txff_alloc.rsvd_h2c_info_addr;

	data = (u8 *)PLTFM_MALLOC(4096);

	while (size != 0) {
		start_page = ((pg_addr << 7) >> 12) + 0x780;
		residue = (pg_addr << 7) & (4096 - 1);

		if (size >= DL_FLASH_RSVDPG_SIZE)
			pkt_size = DL_FLASH_RSVDPG_SIZE;
		else
			pkt_size = size;

		read_flash_88xx(adapter, addr, 4096, data);

		cnt = 0;
		while (cnt < pkt_size) {
			HALMAC_REG_W16(REG_PKTBUF_DBG_CTRL, (u16)(start_page));
			for (i = 0x8000 + residue; i <= 0x8FFF; i++) {
				value8 = HALMAC_REG_R8(i);
				if (*fw_bin != value8) {
					PLTFM_MSG_ERR("[ERR]check flash!!\n");
					PLTFM_FREE(data, 4096);
					return HALMAC_RET_FAIL;
				}

				fw_bin++;
				cnt++;
				if (cnt == pkt_size)
					break;
			}
			residue = 0;
			start_page++;
		}
		addr += pkt_size;
		size -= pkt_size;
	}

	PLTFM_FREE(data, 4096);

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_88XX_SUPPORT */
