/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "bmi.h"
#include "hif.h"
#include "debug.h"
#include "htc.h"

void ath10k_bmi_start(struct ath10k *ar)
{
	ath10k_dbg(ATH10K_DBG_BMI, "bmi start\n");

	ar->bmi.done_sent = false;
}

int ath10k_bmi_done(struct ath10k *ar)
{
	struct bmi_cmd cmd;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.done);
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi done\n");

	if (ar->bmi.done_sent) {
		ath10k_dbg(ATH10K_DBG_BMI, "bmi skipped\n");
		return 0;
	}

	ar->bmi.done_sent = true;
	cmd.id = __cpu_to_le32(BMI_DONE);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn("unable to write to the device: %d\n", ret);
		return ret;
	}

	return 0;
}

int ath10k_bmi_get_target_info(struct ath10k *ar,
			       struct bmi_target_info *target_info)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.get_target_info);
	u32 resplen = sizeof(resp.get_target_info);
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi get target info\n");

	if (ar->bmi.done_sent) {
		ath10k_warn("BMI Get Target Info Command disallowed\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_GET_TARGET_INFO);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
	if (ret) {
		ath10k_warn("unable to get target info from device\n");
		return ret;
	}

	if (resplen < sizeof(resp.get_target_info)) {
		ath10k_warn("invalid get_target_info response length (%d)\n",
			    resplen);
		return -EIO;
	}

	target_info->version = __le32_to_cpu(resp.get_target_info.version);
	target_info->type    = __le32_to_cpu(resp.get_target_info.type);

	return 0;
}

int ath10k_bmi_read_memory(struct ath10k *ar,
			   u32 address, void *buffer, u32 length)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.read_mem);
	u32 rxlen;
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi read address 0x%x length %d\n",
		   address, length);

	if (ar->bmi.done_sent) {
		ath10k_warn("command disallowed\n");
		return -EBUSY;
	}

	while (length) {
		rxlen = min_t(u32, length, BMI_MAX_DATA_SIZE);

		cmd.id            = __cpu_to_le32(BMI_READ_MEMORY);
		cmd.read_mem.addr = __cpu_to_le32(address);
		cmd.read_mem.len  = __cpu_to_le32(rxlen);

		ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen,
						  &resp, &rxlen);
		if (ret) {
			ath10k_warn("unable to read from the device (%d)\n",
				    ret);
			return ret;
		}

		memcpy(buffer, resp.read_mem.payload, rxlen);
		address += rxlen;
		buffer  += rxlen;
		length  -= rxlen;
	}

	return 0;
}

int ath10k_bmi_write_memory(struct ath10k *ar,
			    u32 address, const void *buffer, u32 length)
{
	struct bmi_cmd cmd;
	u32 hdrlen = sizeof(cmd.id) + sizeof(cmd.write_mem);
	u32 txlen;
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi write address 0x%x length %d\n",
		   address, length);

	if (ar->bmi.done_sent) {
		ath10k_warn("command disallowed\n");
		return -EBUSY;
	}

	while (length) {
		txlen = min(length, BMI_MAX_DATA_SIZE - hdrlen);

		/* copy before roundup to avoid reading beyond buffer*/
		memcpy(cmd.write_mem.payload, buffer, txlen);
		txlen = roundup(txlen, 4);

		cmd.id             = __cpu_to_le32(BMI_WRITE_MEMORY);
		cmd.write_mem.addr = __cpu_to_le32(address);
		cmd.write_mem.len  = __cpu_to_le32(txlen);

		ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, hdrlen + txlen,
						  NULL, NULL);
		if (ret) {
			ath10k_warn("unable to write to the device (%d)\n",
				    ret);
			return ret;
		}

		/* fixup roundup() so `length` zeroes out for last chunk */
		txlen = min(txlen, length);

		address += txlen;
		buffer  += txlen;
		length  -= txlen;
	}

	return 0;
}

int ath10k_bmi_execute(struct ath10k *ar, u32 address, u32 *param)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.execute);
	u32 resplen = sizeof(resp.execute);
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi execute address 0x%x param 0x%x\n",
		   address, *param);

	if (ar->bmi.done_sent) {
		ath10k_warn("command disallowed\n");
		return -EBUSY;
	}

	cmd.id            = __cpu_to_le32(BMI_EXECUTE);
	cmd.execute.addr  = __cpu_to_le32(address);
	cmd.execute.param = __cpu_to_le32(*param);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
	if (ret) {
		ath10k_warn("unable to read from the device\n");
		return ret;
	}

	if (resplen < sizeof(resp.execute)) {
		ath10k_warn("invalid execute response length (%d)\n",
			    resplen);
		return ret;
	}

	*param = __le32_to_cpu(resp.execute.result);
	return 0;
}

int ath10k_bmi_lz_data(struct ath10k *ar, const void *buffer, u32 length)
{
	struct bmi_cmd cmd;
	u32 hdrlen = sizeof(cmd.id) + sizeof(cmd.lz_data);
	u32 txlen;
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi lz data buffer 0x%p length %d\n",
		   buffer, length);

	if (ar->bmi.done_sent) {
		ath10k_warn("command disallowed\n");
		return -EBUSY;
	}

	while (length) {
		txlen = min(length, BMI_MAX_DATA_SIZE - hdrlen);

		WARN_ON_ONCE(txlen & 3);

		cmd.id          = __cpu_to_le32(BMI_LZ_DATA);
		cmd.lz_data.len = __cpu_to_le32(txlen);
		memcpy(cmd.lz_data.payload, buffer, txlen);

		ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, hdrlen + txlen,
						  NULL, NULL);
		if (ret) {
			ath10k_warn("unable to write to the device\n");
			return ret;
		}

		buffer += txlen;
		length -= txlen;
	}

	return 0;
}

int ath10k_bmi_lz_stream_start(struct ath10k *ar, u32 address)
{
	struct bmi_cmd cmd;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.lz_start);
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI, "bmi lz stream start address 0x%x\n",
		   address);

	if (ar->bmi.done_sent) {
		ath10k_warn("command disallowed\n");
		return -EBUSY;
	}

	cmd.id            = __cpu_to_le32(BMI_LZ_STREAM_START);
	cmd.lz_start.addr = __cpu_to_le32(address);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn("unable to Start LZ Stream to the device\n");
		return ret;
	}

	return 0;
}

int ath10k_bmi_fast_download(struct ath10k *ar,
			     u32 address, const void *buffer, u32 length)
{
	u8 trailer[4] = {};
	u32 head_len = rounddown(length, 4);
	u32 trailer_len = length - head_len;
	int ret;

	ath10k_dbg(ATH10K_DBG_BMI,
		   "bmi fast download address 0x%x buffer 0x%p length %d\n",
		   address, buffer, length);

	ret = ath10k_bmi_lz_stream_start(ar, address);
	if (ret)
		return ret;

	/* copy the last word into a zero padded buffer */
	if (trailer_len > 0)
		memcpy(trailer, buffer + head_len, trailer_len);

	ret = ath10k_bmi_lz_data(ar, buffer, head_len);
	if (ret)
		return ret;

	if (trailer_len > 0)
		ret = ath10k_bmi_lz_data(ar, trailer, 4);

	if (ret != 0)
		return ret;

	/*
	 * Close compressed stream and open a new (fake) one.
	 * This serves mainly to flush Target caches.
	 */
	ret = ath10k_bmi_lz_stream_start(ar, 0x00);

	return ret;
}
