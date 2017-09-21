/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/firmware.h>
#include "rsi_mgmt.h"
#include "rsi_hal.h"
#include "rsi_sdio.h"

/* FLASH Firmware */
static struct ta_metadata metadata_flash_content[] = {
	{"flash_content", 0x00010000},
	{"rsi/rs9113_wlan_qspi.rps", 0x00010000},
};

/**
 * rsi_send_data_pkt() - This function sends the recieved data packet from
 *			 driver to device.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_send_data_pkt(struct rsi_common *common, struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hdr *tmp_hdr;
	struct ieee80211_tx_info *info;
	struct skb_info *tx_params;
	struct ieee80211_bss_conf *bss;
	int status;
	u8 ieee80211_size = MIN_802_11_HDR_LEN;
	u8 extnd_size;
	__le16 *frame_desc;
	u16 seq_num;

	info = IEEE80211_SKB_CB(skb);
	bss = &info->control.vif->bss_conf;
	tx_params = (struct skb_info *)info->driver_data;

	if (!bss->assoc) {
		status = -EINVAL;
		goto err;
	}

	tmp_hdr = (struct ieee80211_hdr *)&skb->data[0];
	seq_num = (le16_to_cpu(tmp_hdr->seq_ctrl) >> 4);

	extnd_size = ((uintptr_t)skb->data & 0x3);

	if ((FRAME_DESC_SZ + extnd_size) > skb_headroom(skb)) {
		rsi_dbg(ERR_ZONE, "%s: Unable to send pkt\n", __func__);
		status = -ENOSPC;
		goto err;
	}

	skb_push(skb, (FRAME_DESC_SZ + extnd_size));
	frame_desc = (__le16 *)&skb->data[0];
	memset((u8 *)frame_desc, 0, FRAME_DESC_SZ);

	if (ieee80211_is_data_qos(tmp_hdr->frame_control)) {
		ieee80211_size += 2;
		frame_desc[6] |= cpu_to_le16(BIT(12));
	}

	if ((!(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT)) &&
	    (common->secinfo.security_enable)) {
		if (rsi_is_cipher_wep(common))
			ieee80211_size += 4;
		else
			ieee80211_size += 8;
		frame_desc[6] |= cpu_to_le16(BIT(15));
	}

	frame_desc[0] = cpu_to_le16((skb->len - FRAME_DESC_SZ) |
				    (RSI_WIFI_DATA_Q << 12));
	frame_desc[2] = cpu_to_le16((extnd_size) | (ieee80211_size) << 8);

	if (common->min_rate != 0xffff) {
		/* Send fixed rate */
		frame_desc[3] = cpu_to_le16(RATE_INFO_ENABLE);
		frame_desc[4] = cpu_to_le16(common->min_rate);

		if (conf_is_ht40(&common->priv->hw->conf))
			frame_desc[5] = cpu_to_le16(FULL40M_ENABLE);

		if (common->vif_info[0].sgi) {
			if (common->min_rate & 0x100) /* Only MCS rates */
				frame_desc[4] |=
					cpu_to_le16(ENABLE_SHORTGI_RATE);
		}

	}

	frame_desc[6] |= cpu_to_le16(seq_num & 0xfff);
	frame_desc[7] = cpu_to_le16(((tx_params->tid & 0xf) << 4) |
				    (skb->priority & 0xf) |
				    (tx_params->sta_id << 8));

	status = adapter->host_intf_ops->write_pkt(common->priv, skb->data,
						   skb->len);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed to write pkt\n",
			__func__);

err:
	++common->tx_stats.total_tx_pkt_freed[skb->priority];
	rsi_indicate_tx_status(common->priv, skb, status);
	return status;
}

/**
 * rsi_send_mgmt_pkt() - This functions sends the received management packet
 *			 from driver to device.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_send_mgmt_pkt(struct rsi_common *common,
		      struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hdr *wh;
	struct ieee80211_tx_info *info;
	struct ieee80211_bss_conf *bss;
	struct ieee80211_hw *hw = adapter->hw;
	struct ieee80211_conf *conf = &hw->conf;
	struct skb_info *tx_params;
	int status = -E2BIG;
	__le16 *msg;
	u8 extnd_size;
	u8 vap_id = 0;

	info = IEEE80211_SKB_CB(skb);
	tx_params = (struct skb_info *)info->driver_data;
	extnd_size = ((uintptr_t)skb->data & 0x3);

	if (tx_params->flags & INTERNAL_MGMT_PKT) {
		if ((extnd_size) > skb_headroom(skb)) {
			rsi_dbg(ERR_ZONE, "%s: Unable to send pkt\n", __func__);
			dev_kfree_skb(skb);
			return -ENOSPC;
		}
		skb_push(skb, extnd_size);
		skb->data[extnd_size + 4] = extnd_size;
		status = adapter->host_intf_ops->write_pkt(common->priv,
							   (u8 *)skb->data,
							   skb->len);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to write the packet\n", __func__);
		}
		dev_kfree_skb(skb);
		return status;
	}

	bss = &info->control.vif->bss_conf;
	wh = (struct ieee80211_hdr *)&skb->data[0];

	if (FRAME_DESC_SZ > skb_headroom(skb))
		goto err;

	skb_push(skb, FRAME_DESC_SZ);
	memset(skb->data, 0, FRAME_DESC_SZ);
	msg = (__le16 *)skb->data;

	if (skb->len > MAX_MGMT_PKT_SIZE) {
		rsi_dbg(INFO_ZONE, "%s: Dropping mgmt pkt > 512\n", __func__);
		goto err;
	}

	msg[0] = cpu_to_le16((skb->len - FRAME_DESC_SZ) |
			    (RSI_WIFI_MGMT_Q << 12));
	msg[1] = cpu_to_le16(TX_DOT11_MGMT);
	msg[2] = cpu_to_le16(MIN_802_11_HDR_LEN << 8);
	msg[3] = cpu_to_le16(RATE_INFO_ENABLE);
	msg[6] = cpu_to_le16(le16_to_cpu(wh->seq_ctrl) >> 4);

	if (wh->addr1[0] & BIT(0))
		msg[3] |= cpu_to_le16(RSI_BROADCAST_PKT);

	if (common->band == NL80211_BAND_2GHZ)
		msg[4] = cpu_to_le16(RSI_11B_MODE);
	else
		msg[4] = cpu_to_le16((RSI_RATE_6 & 0x0f) | RSI_11G_MODE);

	if (conf_is_ht40(conf)) {
		msg[4] = cpu_to_le16(0xB | RSI_11G_MODE);
		msg[5] = cpu_to_le16(0x6);
	}

	/* Indicate to firmware to give cfm */
	if ((skb->data[16] == IEEE80211_STYPE_PROBE_REQ) && (!bss->assoc)) {
		msg[1] |= cpu_to_le16(BIT(10));
		msg[7] = cpu_to_le16(PROBEREQ_CONFIRM);
		common->mgmt_q_block = true;
	}

	msg[7] |= cpu_to_le16(vap_id << 8);

	status = adapter->host_intf_ops->write_pkt(common->priv, (u8 *)msg,
						   skb->len);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed to write the packet\n", __func__);

err:
	rsi_indicate_tx_status(common->priv, skb, status);
	return status;
}

static void bl_cmd_timeout(unsigned long priv)
{
	struct rsi_hw *adapter = (struct rsi_hw *)priv;

	adapter->blcmd_timer_expired = true;
	del_timer(&adapter->bl_cmd_timer);
}

static int bl_start_cmd_timer(struct rsi_hw *adapter, u32 timeout)
{
	init_timer(&adapter->bl_cmd_timer);
	adapter->bl_cmd_timer.data = (unsigned long)adapter;
	adapter->bl_cmd_timer.function = (void *)&bl_cmd_timeout;
	adapter->bl_cmd_timer.expires = (msecs_to_jiffies(timeout) + jiffies);

	adapter->blcmd_timer_expired = false;
	add_timer(&adapter->bl_cmd_timer);

	return 0;
}

static int bl_stop_cmd_timer(struct rsi_hw *adapter)
{
	adapter->blcmd_timer_expired = false;
	if (timer_pending(&adapter->bl_cmd_timer))
		del_timer(&adapter->bl_cmd_timer);

	return 0;
}

static int bl_write_cmd(struct rsi_hw *adapter, u8 cmd, u8 exp_resp,
			u16 *cmd_resp)
{
	struct rsi_host_intf_ops *hif_ops = adapter->host_intf_ops;
	u32 regin_val = 0, regout_val = 0;
	u32 regin_input = 0;
	u8 output = 0;
	int status;

	regin_input = (REGIN_INPUT | adapter->priv->coex_mode);

	while (!adapter->blcmd_timer_expired) {
		regin_val = 0;
		status = hif_ops->master_reg_read(adapter, SWBL_REGIN,
						  &regin_val, 2);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: Command %0x REGIN reading failed..\n",
				__func__, cmd);
			return status;
		}
		mdelay(1);
		if ((regin_val >> 12) != REGIN_VALID)
			break;
	}
	if (adapter->blcmd_timer_expired) {
		rsi_dbg(ERR_ZONE,
			"%s: Command %0x REGIN reading timed out..\n",
			__func__, cmd);
		return -ETIMEDOUT;
	}

	rsi_dbg(INFO_ZONE,
		"Issuing write to Regin val:%0x sending cmd:%0x\n",
		regin_val, (cmd | regin_input << 8));
	status = hif_ops->master_reg_write(adapter, SWBL_REGIN,
					   (cmd | regin_input << 8), 2);
	if (status < 0)
		return status;
	mdelay(1);

	if (cmd == LOAD_HOSTED_FW || cmd == JUMP_TO_ZERO_PC) {
		/* JUMP_TO_ZERO_PC doesn't expect
		 * any response. So return from here
		 */
		return 0;
	}

	while (!adapter->blcmd_timer_expired) {
		regout_val = 0;
		status = hif_ops->master_reg_read(adapter, SWBL_REGOUT,
					     &regout_val, 2);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: Command %0x REGOUT reading failed..\n",
				__func__, cmd);
			return status;
		}
		mdelay(1);
		if ((regout_val >> 8) == REGOUT_VALID)
			break;
	}
	if (adapter->blcmd_timer_expired) {
		rsi_dbg(ERR_ZONE,
			"%s: Command %0x REGOUT reading timed out..\n",
			__func__, cmd);
		return status;
	}

	*cmd_resp = ((u16 *)&regout_val)[0] & 0xffff;

	output = ((u8 *)&regout_val)[0] & 0xff;

	status = hif_ops->master_reg_write(adapter, SWBL_REGOUT,
					   (cmd | REGOUT_INVALID << 8), 2);
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Command %0x REGOUT writing failed..\n",
			__func__, cmd);
		return status;
	}
	mdelay(1);

	if (output != exp_resp) {
		rsi_dbg(ERR_ZONE,
			"%s: Recvd resp %x for cmd %0x\n",
			__func__, output, cmd);
		return -EINVAL;
	}
	rsi_dbg(INFO_ZONE,
		"%s: Recvd Expected resp %x for cmd %0x\n",
		__func__, output, cmd);

	return 0;
}

static int bl_cmd(struct rsi_hw *adapter, u8 cmd, u8 exp_resp, char *str)
{
	u16 regout_val = 0;
	u32 timeout;
	int status;

	if ((cmd == EOF_REACHED) || (cmd == PING_VALID) || (cmd == PONG_VALID))
		timeout = BL_BURN_TIMEOUT;
	else
		timeout = BL_CMD_TIMEOUT;

	bl_start_cmd_timer(adapter, timeout);
	status = bl_write_cmd(adapter, cmd, exp_resp, &regout_val);
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Command %s (%0x) writing failed..\n",
			__func__, str, cmd);
		return status;
	}
	bl_stop_cmd_timer(adapter);
	return 0;
}

#define CHECK_SUM_OFFSET 20
#define LEN_OFFSET 8
#define ADDR_OFFSET 16
static int bl_write_header(struct rsi_hw *adapter, u8 *flash_content,
			   u32 content_size)
{
	struct rsi_host_intf_ops *hif_ops = adapter->host_intf_ops;
	struct bl_header bl_hdr;
	u32 write_addr, write_len;
	int status;

	bl_hdr.flags = 0;
	bl_hdr.image_no = cpu_to_le32(adapter->priv->coex_mode);
	bl_hdr.check_sum = cpu_to_le32(
				*(u32 *)&flash_content[CHECK_SUM_OFFSET]);
	bl_hdr.flash_start_address = cpu_to_le32(
					*(u32 *)&flash_content[ADDR_OFFSET]);
	bl_hdr.flash_len = cpu_to_le32(*(u32 *)&flash_content[LEN_OFFSET]);
	write_len = sizeof(struct bl_header);

	if (adapter->rsi_host_intf == RSI_HOST_INTF_USB) {
		write_addr = PING_BUFFER_ADDRESS;
		status = hif_ops->write_reg_multiple(adapter, write_addr,
						 (u8 *)&bl_hdr, write_len);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to load Version/CRC structure\n",
				__func__);
			return status;
		}
	} else {
		write_addr = PING_BUFFER_ADDRESS >> 16;
		status = hif_ops->master_access_msword(adapter, write_addr);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: Unable to set ms word to common reg\n",
				__func__);
			return status;
		}
		write_addr = RSI_SD_REQUEST_MASTER |
			     (PING_BUFFER_ADDRESS & 0xFFFF);
		status = hif_ops->write_reg_multiple(adapter, write_addr,
						 (u8 *)&bl_hdr, write_len);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to load Version/CRC structure\n",
				__func__);
			return status;
		}
	}
	return 0;
}

static u32 read_flash_capacity(struct rsi_hw *adapter)
{
	u32 flash_sz = 0;

	if ((adapter->host_intf_ops->master_reg_read(adapter, FLASH_SIZE_ADDR,
						     &flash_sz, 2)) < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Flash size reading failed..\n",
			__func__);
		return 0;
	}
	rsi_dbg(INIT_ZONE, "Flash capacity: %d KiloBytes\n", flash_sz);

	return (flash_sz * 1024); /* Return size in kbytes */
}

static int ping_pong_write(struct rsi_hw *adapter, u8 cmd, u8 *addr, u32 size)
{
	struct rsi_host_intf_ops *hif_ops = adapter->host_intf_ops;
	u32 block_size = adapter->block_size;
	u32 cmd_addr;
	u16 cmd_resp, cmd_req;
	u8 *str;
	int status;

	if (cmd == PING_WRITE) {
		cmd_addr = PING_BUFFER_ADDRESS;
		cmd_resp = PONG_AVAIL;
		cmd_req = PING_VALID;
		str = "PING_VALID";
	} else {
		cmd_addr = PONG_BUFFER_ADDRESS;
		cmd_resp = PING_AVAIL;
		cmd_req = PONG_VALID;
		str = "PONG_VALID";
	}

	status = hif_ops->load_data_master_write(adapter, cmd_addr, size,
					    block_size, addr);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Unable to write blk at addr %0x\n",
			__func__, *addr);
		return status;
	}

	status = bl_cmd(adapter, cmd_req, cmd_resp, str);
	if (status) {
		bl_stop_cmd_timer(adapter);
		return status;
	}
	return 0;
}

static int auto_fw_upgrade(struct rsi_hw *adapter, u8 *flash_content,
			   u32 content_size)
{
	u8 cmd, *temp_flash_content;
	u32 temp_content_size, num_flash, index;
	u32 flash_start_address;
	int status;

	temp_flash_content = flash_content;

	if (content_size > MAX_FLASH_FILE_SIZE) {
		rsi_dbg(ERR_ZONE,
			"%s: Flash Content size is more than 400K %u\n",
			__func__, MAX_FLASH_FILE_SIZE);
		return -EINVAL;
	}

	flash_start_address = *(u32 *)&flash_content[FLASH_START_ADDRESS];
	rsi_dbg(INFO_ZONE, "flash start address: %08x\n", flash_start_address);

	if (flash_start_address < FW_IMAGE_MIN_ADDRESS) {
		rsi_dbg(ERR_ZONE,
			"%s: Fw image Flash Start Address is less than 64K\n",
			__func__);
		return -EINVAL;
	}

	if (flash_start_address % FLASH_SECTOR_SIZE) {
		rsi_dbg(ERR_ZONE,
			"%s: Flash Start Address is not multiple of 4K\n",
			__func__);
		return -EINVAL;
	}

	if ((flash_start_address + content_size) > adapter->flash_capacity) {
		rsi_dbg(ERR_ZONE,
			"%s: Flash Content will cross max flash size\n",
			__func__);
		return -EINVAL;
	}

	temp_content_size  = content_size;
	num_flash = content_size / FLASH_WRITE_CHUNK_SIZE;

	rsi_dbg(INFO_ZONE, "content_size: %d, num_flash: %d\n",
		content_size, num_flash);

	for (index = 0; index <= num_flash; index++) {
		rsi_dbg(INFO_ZONE, "flash index: %d\n", index);
		if (index != num_flash) {
			content_size = FLASH_WRITE_CHUNK_SIZE;
			rsi_dbg(INFO_ZONE, "QSPI content_size:%d\n",
				content_size);
		} else {
			content_size =
				temp_content_size % FLASH_WRITE_CHUNK_SIZE;
			rsi_dbg(INFO_ZONE,
				"Writing last sector content_size:%d\n",
				content_size);
			if (!content_size) {
				rsi_dbg(INFO_ZONE, "instruction size zero\n");
				break;
			}
		}

		if (index % 2)
			cmd = PING_WRITE;
		else
			cmd = PONG_WRITE;

		status = ping_pong_write(adapter, cmd, flash_content,
					 content_size);
		if (status) {
			rsi_dbg(ERR_ZONE, "%s: Unable to load %d block\n",
				__func__, index);
			return status;
		}

		rsi_dbg(INFO_ZONE,
			"%s: Successfully loaded %d instructions\n",
			__func__, index);
		flash_content += content_size;
	}

	status = bl_cmd(adapter, EOF_REACHED, FW_LOADING_SUCCESSFUL,
			"EOF_REACHED");
	if (status) {
		bl_stop_cmd_timer(adapter);
		return status;
	}
	rsi_dbg(INFO_ZONE, "FW loading is done and FW is running..\n");
	return 0;
}

static int rsi_load_firmware(struct rsi_hw *adapter)
{
	struct rsi_host_intf_ops *hif_ops = adapter->host_intf_ops;
	const struct firmware *fw_entry = NULL;
	u32 regout_val = 0, content_size;
	u16 tmp_regout_val = 0;
	u8 *flash_content = NULL;
	struct ta_metadata *metadata_p;
	int status;

	bl_start_cmd_timer(adapter, BL_CMD_TIMEOUT);

	while (!adapter->blcmd_timer_expired) {
		status = hif_ops->master_reg_read(adapter, SWBL_REGOUT,
					      &regout_val, 2);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"%s: REGOUT read failed\n", __func__);
			return status;
		}
		mdelay(1);
		if ((regout_val >> 8) == REGOUT_VALID)
			break;
	}
	if (adapter->blcmd_timer_expired) {
		rsi_dbg(ERR_ZONE, "%s: REGOUT read timedout\n", __func__);
		rsi_dbg(ERR_ZONE,
			"%s: Soft boot loader not present\n", __func__);
		return -ETIMEDOUT;
	}
	bl_stop_cmd_timer(adapter);

	rsi_dbg(INFO_ZONE, "Received Board Version Number: %x\n",
		(regout_val & 0xff));

	status = hif_ops->master_reg_write(adapter, SWBL_REGOUT,
					(REGOUT_INVALID | REGOUT_INVALID << 8),
					2);
	if (status < 0) {
		rsi_dbg(ERR_ZONE, "%s: REGOUT writing failed..\n", __func__);
		return status;
	}
	mdelay(1);

	status = bl_cmd(adapter, CONFIG_AUTO_READ_MODE, CMD_PASS,
			"AUTO_READ_CMD");
	if (status < 0)
		return status;

	adapter->flash_capacity = read_flash_capacity(adapter);
	if (adapter->flash_capacity <= 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to read flash size from EEPROM\n",
			__func__);
		return -EINVAL;
	}

	metadata_p = &metadata_flash_content[adapter->priv->coex_mode];

	rsi_dbg(INIT_ZONE, "%s: Loading file %s\n", __func__, metadata_p->name);
	adapter->fw_file_name = metadata_p->name;

	status = request_firmware(&fw_entry, metadata_p->name, adapter->device);
	if (status < 0) {
		rsi_dbg(ERR_ZONE, "%s: Failed to open file %s\n",
			__func__, metadata_p->name);
		return status;
	}
	flash_content = kmemdup(fw_entry->data, fw_entry->size, GFP_KERNEL);
	if (!flash_content) {
		rsi_dbg(ERR_ZONE, "%s: Failed to copy firmware\n", __func__);
		status = -EIO;
		goto fail;
	}
	content_size = fw_entry->size;
	rsi_dbg(INFO_ZONE, "FW Length = %d bytes\n", content_size);

	status = bl_write_header(adapter, flash_content, content_size);
	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: RPS Image header loading failed\n",
			__func__);
		goto fail;
	}

	bl_start_cmd_timer(adapter, BL_CMD_TIMEOUT);
	status = bl_write_cmd(adapter, CHECK_CRC, CMD_PASS, &tmp_regout_val);
	if (status) {
		bl_stop_cmd_timer(adapter);
		rsi_dbg(ERR_ZONE,
			"%s: CHECK_CRC Command writing failed..\n",
			__func__);
		if ((tmp_regout_val & 0xff) == CMD_FAIL) {
			rsi_dbg(ERR_ZONE,
				"CRC Fail.. Proceeding to Upgrade mode\n");
			goto fw_upgrade;
		}
	}
	bl_stop_cmd_timer(adapter);

	status = bl_cmd(adapter, POLLING_MODE, CMD_PASS, "POLLING_MODE");
	if (status)
		goto fail;

load_image_cmd:
	status = bl_cmd(adapter, LOAD_HOSTED_FW, LOADING_INITIATED,
			"LOAD_HOSTED_FW");
	if (status)
		goto fail;
	rsi_dbg(INFO_ZONE, "Load Image command passed..\n");
	goto success;

fw_upgrade:
	status = bl_cmd(adapter, BURN_HOSTED_FW, SEND_RPS_FILE, "FW_UPGRADE");
	if (status)
		goto fail;

	rsi_dbg(INFO_ZONE, "Burn Command Pass.. Upgrading the firmware\n");

	status = auto_fw_upgrade(adapter, flash_content, content_size);
	if (status == 0) {
		rsi_dbg(ERR_ZONE, "Firmware upgradation Done\n");
		goto load_image_cmd;
	}
	rsi_dbg(ERR_ZONE, "Firmware upgrade failed\n");

	status = bl_cmd(adapter, CONFIG_AUTO_READ_MODE, CMD_PASS,
			"AUTO_READ_MODE");
	if (status)
		goto fail;

success:
	rsi_dbg(ERR_ZONE, "***** Firmware Loading successful *****\n");
	kfree(flash_content);
	release_firmware(fw_entry);
	return 0;

fail:
	rsi_dbg(ERR_ZONE, "##### Firmware loading failed #####\n");
	kfree(flash_content);
	release_firmware(fw_entry);
	return status;
}

int rsi_hal_device_init(struct rsi_hw *adapter)
{
	struct rsi_common *common = adapter->priv;

	common->coex_mode = RSI_DEV_COEX_MODE_WIFI_ALONE;
	common->oper_mode = RSI_DEV_OPMODE_WIFI_ALONE;
	adapter->device_model = RSI_DEV_9113;

	switch (adapter->device_model) {
	case RSI_DEV_9113:
		if (rsi_load_firmware(adapter)) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to load TA instructions\n",
				__func__);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	common->fsm_state = FSM_CARD_NOT_READY;

	return 0;
}
EXPORT_SYMBOL_GPL(rsi_hal_device_init);

