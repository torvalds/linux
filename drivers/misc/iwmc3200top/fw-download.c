/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/fw-download.c
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#include <linux/firmware.h>
#include <linux/mmc/sdio_func.h>
#include <asm/unaligned.h>

#include "iwmc3200top.h"
#include "log.h"
#include "fw-msg.h"

#define CHECKSUM_BYTES_NUM sizeof(u32)

/**
  init parser struct with file
 */
static int iwmct_fw_parser_init(struct iwmct_priv *priv, const u8 *file,
			      size_t file_size, size_t block_size)
{
	struct iwmct_parser *parser = &priv->parser;
	struct iwmct_fw_hdr *fw_hdr = &parser->versions;

	LOG_TRACE(priv, FW_DOWNLOAD, "-->\n");

	LOG_INFO(priv, FW_DOWNLOAD, "file_size=%zd\n", file_size);

	parser->file = file;
	parser->file_size = file_size;
	parser->cur_pos = 0;
	parser->entry_point = 0;
	parser->buf = kzalloc(block_size, GFP_KERNEL);
	if (!parser->buf) {
		LOG_ERROR(priv, FW_DOWNLOAD, "kzalloc error\n");
		return -ENOMEM;
	}
	parser->buf_size = block_size;

	/* extract fw versions */
	memcpy(fw_hdr, parser->file, sizeof(struct iwmct_fw_hdr));
	LOG_INFO(priv, FW_DOWNLOAD, "fw versions are:\n"
		"top %u.%u.%u gps %u.%u.%u bt %u.%u.%u tic %s\n",
		fw_hdr->top_major, fw_hdr->top_minor, fw_hdr->top_revision,
		fw_hdr->gps_major, fw_hdr->gps_minor, fw_hdr->gps_revision,
		fw_hdr->bt_major, fw_hdr->bt_minor, fw_hdr->bt_revision,
		fw_hdr->tic_name);

	parser->cur_pos += sizeof(struct iwmct_fw_hdr);

	LOG_TRACE(priv, FW_DOWNLOAD, "<--\n");
	return 0;
}

static bool iwmct_checksum(struct iwmct_priv *priv)
{
	struct iwmct_parser *parser = &priv->parser;
	__le32 *file = (__le32 *)parser->file;
	int i, pad, steps;
	u32 accum = 0;
	u32 checksum;
	u32 mask = 0xffffffff;

	pad = (parser->file_size - CHECKSUM_BYTES_NUM) % 4;
	steps =  (parser->file_size - CHECKSUM_BYTES_NUM) / 4;

	LOG_INFO(priv, FW_DOWNLOAD, "pad=%d steps=%d\n", pad, steps);

	for (i = 0; i < steps; i++)
		accum += le32_to_cpu(file[i]);

	if (pad) {
		mask <<= 8 * (4 - pad);
		accum += le32_to_cpu(file[steps]) & mask;
	}

	checksum = get_unaligned_le32((__le32 *)(parser->file +
			parser->file_size - CHECKSUM_BYTES_NUM));

	LOG_INFO(priv, FW_DOWNLOAD,
		"compare checksum accum=0x%x to checksum=0x%x\n",
		accum, checksum);

	return checksum == accum;
}

static int iwmct_parse_next_section(struct iwmct_priv *priv, const u8 **p_sec,
				  size_t *sec_size, __le32 *sec_addr)
{
	struct iwmct_parser *parser = &priv->parser;
	struct iwmct_dbg *dbg = &priv->dbg;
	struct iwmct_fw_sec_hdr *sec_hdr;

	LOG_TRACE(priv, FW_DOWNLOAD, "-->\n");

	while (parser->cur_pos + sizeof(struct iwmct_fw_sec_hdr)
		<= parser->file_size) {

		sec_hdr = (struct iwmct_fw_sec_hdr *)
				(parser->file + parser->cur_pos);
		parser->cur_pos += sizeof(struct iwmct_fw_sec_hdr);

		LOG_INFO(priv, FW_DOWNLOAD,
			"sec hdr: type=%s addr=0x%x size=%d\n",
			sec_hdr->type, sec_hdr->target_addr,
			sec_hdr->data_size);

		if (strcmp(sec_hdr->type, "ENT") == 0)
			parser->entry_point = le32_to_cpu(sec_hdr->target_addr);
		else if (strcmp(sec_hdr->type, "LBL") == 0)
			strcpy(dbg->label_fw, parser->file + parser->cur_pos);
		else if (((strcmp(sec_hdr->type, "TOP") == 0) &&
			  (priv->barker & BARKER_DNLOAD_TOP_MSK)) ||
			 ((strcmp(sec_hdr->type, "GPS") == 0) &&
			  (priv->barker & BARKER_DNLOAD_GPS_MSK)) ||
			 ((strcmp(sec_hdr->type, "BTH") == 0) &&
			  (priv->barker & BARKER_DNLOAD_BT_MSK))) {
			*sec_addr = sec_hdr->target_addr;
			*sec_size = le32_to_cpu(sec_hdr->data_size);
			*p_sec = parser->file + parser->cur_pos;
			parser->cur_pos += le32_to_cpu(sec_hdr->data_size);
			return 1;
		} else if (strcmp(sec_hdr->type, "LOG") != 0)
			LOG_WARNING(priv, FW_DOWNLOAD,
				    "skipping section type %s\n",
				    sec_hdr->type);

		parser->cur_pos += le32_to_cpu(sec_hdr->data_size);
		LOG_INFO(priv, FW_DOWNLOAD,
			"finished with section cur_pos=%zd\n", parser->cur_pos);
	}

	LOG_TRACE(priv, INIT, "<--\n");
	return 0;
}

static int iwmct_download_section(struct iwmct_priv *priv, const u8 *p_sec,
				size_t sec_size, __le32 addr)
{
	struct iwmct_parser *parser = &priv->parser;
	struct iwmct_fw_load_hdr *hdr = (struct iwmct_fw_load_hdr *)parser->buf;
	const u8 *cur_block = p_sec;
	size_t sent = 0;
	int cnt = 0;
	int ret = 0;
	u32 cmd = 0;

	LOG_TRACE(priv, FW_DOWNLOAD, "-->\n");
	LOG_INFO(priv, FW_DOWNLOAD, "Download address 0x%x size 0x%zx\n",
				addr, sec_size);

	while (sent < sec_size) {
		int i;
		u32 chksm = 0;
		u32 reset = atomic_read(&priv->reset);
		/* actual FW data */
		u32 data_size = min(parser->buf_size - sizeof(*hdr),
				    sec_size - sent);
		/* Pad to block size */
		u32 trans_size = (data_size + sizeof(*hdr) +
				  IWMC_SDIO_BLK_SIZE - 1) &
				  ~(IWMC_SDIO_BLK_SIZE - 1);
		++cnt;

		/* in case of reset, interrupt FW DOWNLAOD */
		if (reset) {
			LOG_INFO(priv, FW_DOWNLOAD,
				 "Reset detected. Abort FW download!!!");
			ret = -ECANCELED;
			goto exit;
		}

		memset(parser->buf, 0, parser->buf_size);
		cmd |= IWMC_OPCODE_WRITE << CMD_HDR_OPCODE_POS;
		cmd |= IWMC_CMD_SIGNATURE << CMD_HDR_SIGNATURE_POS;
		cmd |= (priv->dbg.direct ? 1 : 0) << CMD_HDR_DIRECT_ACCESS_POS;
		cmd |= (priv->dbg.checksum ? 1 : 0) << CMD_HDR_USE_CHECKSUM_POS;
		hdr->data_size = cpu_to_le32(data_size);
		hdr->target_addr = addr;

		/* checksum is allowed for sizes divisible by 4 */
		if (data_size & 0x3)
			cmd &= ~CMD_HDR_USE_CHECKSUM_MSK;

		memcpy(hdr->data, cur_block, data_size);


		if (cmd & CMD_HDR_USE_CHECKSUM_MSK) {

			chksm = data_size + le32_to_cpu(addr) + cmd;
			for (i = 0; i < data_size >> 2; i++)
				chksm += ((u32 *)cur_block)[i];

			hdr->block_chksm = cpu_to_le32(chksm);
			LOG_INFO(priv, FW_DOWNLOAD, "Checksum = 0x%X\n",
				 hdr->block_chksm);
		}

		LOG_INFO(priv, FW_DOWNLOAD, "trans#%d, len=%d, sent=%zd, "
				"sec_size=%zd, startAddress 0x%X\n",
				cnt, trans_size, sent, sec_size, addr);

		if (priv->dbg.dump)
			LOG_HEXDUMP(FW_DOWNLOAD, parser->buf, trans_size);


		hdr->cmd = cpu_to_le32(cmd);
		/* send it down */
		/* TODO: add more proper sending and error checking */
		ret = iwmct_tx(priv, parser->buf, trans_size);
		if (ret != 0) {
			LOG_INFO(priv, FW_DOWNLOAD,
				"iwmct_tx returned %d\n", ret);
			goto exit;
		}

		addr = cpu_to_le32(le32_to_cpu(addr) + data_size);
		sent += data_size;
		cur_block = p_sec + sent;

		if (priv->dbg.blocks && (cnt + 1) >= priv->dbg.blocks) {
			LOG_INFO(priv, FW_DOWNLOAD,
				"Block number limit is reached [%d]\n",
				priv->dbg.blocks);
			break;
		}
	}

	if (sent < sec_size)
		ret = -EINVAL;
exit:
	LOG_TRACE(priv, FW_DOWNLOAD, "<--\n");
	return ret;
}

static int iwmct_kick_fw(struct iwmct_priv *priv, bool jump)
{
	struct iwmct_parser *parser = &priv->parser;
	struct iwmct_fw_load_hdr *hdr = (struct iwmct_fw_load_hdr *)parser->buf;
	int ret;
	u32 cmd;

	LOG_TRACE(priv, FW_DOWNLOAD, "-->\n");

	memset(parser->buf, 0, parser->buf_size);
	cmd = IWMC_CMD_SIGNATURE << CMD_HDR_SIGNATURE_POS;
	if (jump) {
		cmd |= IWMC_OPCODE_JUMP << CMD_HDR_OPCODE_POS;
		hdr->target_addr = cpu_to_le32(parser->entry_point);
		LOG_INFO(priv, FW_DOWNLOAD, "jump address 0x%x\n",
				parser->entry_point);
	} else {
		cmd |= IWMC_OPCODE_LAST_COMMAND << CMD_HDR_OPCODE_POS;
		LOG_INFO(priv, FW_DOWNLOAD, "last command\n");
	}

	hdr->cmd = cpu_to_le32(cmd);

	LOG_HEXDUMP(FW_DOWNLOAD, parser->buf, sizeof(*hdr));
	/* send it down */
	/* TODO: add more proper sending and error checking */
	ret = iwmct_tx(priv, parser->buf, IWMC_SDIO_BLK_SIZE);
	if (ret)
		LOG_INFO(priv, FW_DOWNLOAD, "iwmct_tx returned %d", ret);

	LOG_TRACE(priv, FW_DOWNLOAD, "<--\n");
	return 0;
}

int iwmct_fw_load(struct iwmct_priv *priv)
{
	const u8 *fw_name = FW_NAME(FW_API_VER);
	const struct firmware *raw;
	const u8 *pdata;
	size_t len;
	__le32 addr;
	int ret;


	LOG_INFO(priv, FW_DOWNLOAD, "barker download request 0x%x is:\n",
			priv->barker);
	LOG_INFO(priv, FW_DOWNLOAD, "*******  Top FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_TOP_MSK) ? "was" : "not");
	LOG_INFO(priv, FW_DOWNLOAD, "*******  GPS FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_GPS_MSK) ? "was" : "not");
	LOG_INFO(priv, FW_DOWNLOAD, "*******  BT FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_BT_MSK) ? "was" : "not");


	/* get the firmware */
	ret = request_firmware(&raw, fw_name, &priv->func->dev);
	if (ret < 0) {
		LOG_ERROR(priv, FW_DOWNLOAD, "%s request_firmware failed %d\n",
			  fw_name, ret);
		goto exit;
	}

	if (raw->size < sizeof(struct iwmct_fw_sec_hdr)) {
		LOG_ERROR(priv, FW_DOWNLOAD, "%s smaller then (%zd) (%zd)\n",
			  fw_name, sizeof(struct iwmct_fw_sec_hdr), raw->size);
		goto exit;
	}

	LOG_INFO(priv, FW_DOWNLOAD, "Read firmware '%s'\n", fw_name);

	/* clear parser struct */
	ret = iwmct_fw_parser_init(priv, raw->data, raw->size, priv->trans_len);
	if (ret < 0) {
		LOG_ERROR(priv, FW_DOWNLOAD,
			  "iwmct_parser_init failed: Reason %d\n", ret);
		goto exit;
	}

	if (!iwmct_checksum(priv)) {
		LOG_ERROR(priv, FW_DOWNLOAD, "checksum error\n");
		ret = -EINVAL;
		goto exit;
	}

	/* download firmware to device */
	while (iwmct_parse_next_section(priv, &pdata, &len, &addr)) {
		ret = iwmct_download_section(priv, pdata, len, addr);
		if (ret) {
			LOG_ERROR(priv, FW_DOWNLOAD,
				  "%s download section failed\n", fw_name);
			goto exit;
		}
	}

	ret = iwmct_kick_fw(priv, !!(priv->barker & BARKER_DNLOAD_JUMP_MSK));

exit:
	kfree(priv->parser.buf);
	release_firmware(raw);
	return ret;
}
