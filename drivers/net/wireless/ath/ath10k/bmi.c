// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2014,2016-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "bmi.h"
#include "hif.h"
#include "debug.h"
#include "htc.h"
#include "hw.h"

void ath10k_bmi_start(struct ath10k *ar)
{
	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi start\n");

	ar->bmi.done_sent = false;
}
EXPORT_SYMBOL(ath10k_bmi_start);

int ath10k_bmi_done(struct ath10k *ar)
{
	struct bmi_cmd cmd;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.done);
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi done\n");

	if (ar->bmi.done_sent) {
		ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi skipped\n");
		return 0;
	}

	ar->bmi.done_sent = true;
	cmd.id = __cpu_to_le32(BMI_DONE);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn(ar, "unable to write to the device: %d\n", ret);
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

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi get target info\n");

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "BMI Get Target Info Command disallowed\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_GET_TARGET_INFO);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
	if (ret) {
		ath10k_warn(ar, "unable to get target info from device\n");
		return ret;
	}

	if (resplen < sizeof(resp.get_target_info)) {
		ath10k_warn(ar, "invalid get_target_info response length (%d)\n",
			    resplen);
		return -EIO;
	}

	target_info->version = __le32_to_cpu(resp.get_target_info.version);
	target_info->type    = __le32_to_cpu(resp.get_target_info.type);

	return 0;
}

#define TARGET_VERSION_SENTINAL 0xffffffffu

int ath10k_bmi_get_target_info_sdio(struct ath10k *ar,
				    struct bmi_target_info *target_info)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.get_target_info);
	u32 resplen, ver_len;
	__le32 tmp;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi get target info SDIO\n");

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "BMI Get Target Info Command disallowed\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_GET_TARGET_INFO);

	/* Step 1: Read 4 bytes of the target info and check if it is
	 * the special sentinel version word or the first word in the
	 * version response.
	 */
	resplen = sizeof(u32);
	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &tmp, &resplen);
	if (ret) {
		ath10k_warn(ar, "unable to read from device\n");
		return ret;
	}

	/* Some SDIO boards have a special sentinel byte before the real
	 * version response.
	 */
	if (__le32_to_cpu(tmp) == TARGET_VERSION_SENTINAL) {
		/* Step 1b: Read the version length */
		resplen = sizeof(u32);
		ret = ath10k_hif_exchange_bmi_msg(ar, NULL, 0, &tmp,
						  &resplen);
		if (ret) {
			ath10k_warn(ar, "unable to read from device\n");
			return ret;
		}
	}

	ver_len = __le32_to_cpu(tmp);

	/* Step 2: Check the target info length */
	if (ver_len != sizeof(resp.get_target_info)) {
		ath10k_warn(ar, "Unexpected target info len: %u. Expected: %zu\n",
			    ver_len, sizeof(resp.get_target_info));
		return -EINVAL;
	}

	/* Step 3: Read the rest of the version response */
	resplen = sizeof(resp.get_target_info) - sizeof(u32);
	ret = ath10k_hif_exchange_bmi_msg(ar, NULL, 0,
					  &resp.get_target_info.version,
					  &resplen);
	if (ret) {
		ath10k_warn(ar, "unable to read from device\n");
		return ret;
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

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read address 0x%x length %d\n",
		   address, length);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
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
			ath10k_warn(ar, "unable to read from the device (%d)\n",
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
EXPORT_SYMBOL(ath10k_bmi_read_memory);

int ath10k_bmi_write_soc_reg(struct ath10k *ar, u32 address, u32 reg_val)
{
	struct bmi_cmd cmd;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.write_soc_reg);
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI,
		   "bmi write soc register 0x%08x val 0x%08x\n",
		   address, reg_val);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "bmi write soc register command in progress\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_WRITE_SOC_REGISTER);
	cmd.write_soc_reg.addr = __cpu_to_le32(address);
	cmd.write_soc_reg.value = __cpu_to_le32(reg_val);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn(ar, "Unable to write soc register to device: %d\n",
			    ret);
		return ret;
	}

	return 0;
}

int ath10k_bmi_read_soc_reg(struct ath10k *ar, u32 address, u32 *reg_val)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.read_soc_reg);
	u32 resplen = sizeof(resp.read_soc_reg);
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read soc register 0x%08x\n",
		   address);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "bmi read soc register command in progress\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_READ_SOC_REGISTER);
	cmd.read_soc_reg.addr = __cpu_to_le32(address);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
	if (ret) {
		ath10k_warn(ar, "Unable to read soc register from device: %d\n",
			    ret);
		return ret;
	}

	*reg_val = __le32_to_cpu(resp.read_soc_reg.value);

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read soc register value 0x%08x\n",
		   *reg_val);

	return 0;
}

int ath10k_bmi_write_memory(struct ath10k *ar,
			    u32 address, const void *buffer, u32 length)
{
	struct bmi_cmd cmd;
	u32 hdrlen = sizeof(cmd.id) + sizeof(cmd.write_mem);
	u32 txlen;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi write address 0x%x length %d\n",
		   address, length);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
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
			ath10k_warn(ar, "unable to write to the device (%d)\n",
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

int ath10k_bmi_execute(struct ath10k *ar, u32 address, u32 param, u32 *result)
{
	struct bmi_cmd cmd;
	union bmi_resp resp;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.execute);
	u32 resplen = sizeof(resp.execute);
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi execute address 0x%x param 0x%x\n",
		   address, param);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
		return -EBUSY;
	}

	cmd.id            = __cpu_to_le32(BMI_EXECUTE);
	cmd.execute.addr  = __cpu_to_le32(address);
	cmd.execute.param = __cpu_to_le32(param);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
	if (ret) {
		ath10k_warn(ar, "unable to read from the device\n");
		return ret;
	}

	if (resplen < sizeof(resp.execute)) {
		ath10k_warn(ar, "invalid execute response length (%d)\n",
			    resplen);
		return -EIO;
	}

	*result = __le32_to_cpu(resp.execute.result);

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi execute result 0x%x\n", *result);

	return 0;
}

static int ath10k_bmi_lz_data_large(struct ath10k *ar, const void *buffer, u32 length)
{
	struct bmi_cmd *cmd;
	u32 hdrlen = sizeof(cmd->id) + sizeof(cmd->lz_data);
	u32 txlen;
	int ret;
	size_t buf_len;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "large bmi lz data buffer 0x%p length %d\n",
		   buffer, length);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
		return -EBUSY;
	}

	buf_len = sizeof(*cmd) + BMI_MAX_LARGE_DATA_SIZE - BMI_MAX_DATA_SIZE;
	cmd = kzalloc(buf_len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	while (length) {
		txlen = min(length, BMI_MAX_LARGE_DATA_SIZE - hdrlen);

		WARN_ON_ONCE(txlen & 3);

		cmd->id          = __cpu_to_le32(BMI_LZ_DATA);
		cmd->lz_data.len = __cpu_to_le32(txlen);
		memcpy(cmd->lz_data.payload, buffer, txlen);

		ret = ath10k_hif_exchange_bmi_msg(ar, cmd, hdrlen + txlen,
						  NULL, NULL);
		if (ret) {
			ath10k_warn(ar, "unable to write to the device\n");
			kfree(cmd);
			return ret;
		}

		buffer += txlen;
		length -= txlen;
	}

	kfree(cmd);

	return 0;
}

int ath10k_bmi_lz_data(struct ath10k *ar, const void *buffer, u32 length)
{
	struct bmi_cmd cmd;
	u32 hdrlen = sizeof(cmd.id) + sizeof(cmd.lz_data);
	u32 txlen;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi lz data buffer 0x%p length %d\n",
		   buffer, length);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
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
			ath10k_warn(ar, "unable to write to the device\n");
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

	ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi lz stream start address 0x%x\n",
		   address);

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "command disallowed\n");
		return -EBUSY;
	}

	cmd.id            = __cpu_to_le32(BMI_LZ_STREAM_START);
	cmd.lz_start.addr = __cpu_to_le32(address);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn(ar, "unable to Start LZ Stream to the device\n");
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

	ath10k_dbg(ar, ATH10K_DBG_BMI,
		   "bmi fast download address 0x%x buffer 0x%p length %d\n",
		   address, buffer, length);

	ret = ath10k_bmi_lz_stream_start(ar, address);
	if (ret)
		return ret;

	/* copy the last word into a zero padded buffer */
	if (trailer_len > 0)
		memcpy(trailer, buffer + head_len, trailer_len);

	if (ar->hw_params.bmi_large_size_download)
		ret = ath10k_bmi_lz_data_large(ar, buffer, head_len);
	else
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

int ath10k_bmi_set_start(struct ath10k *ar, u32 address)
{
	struct bmi_cmd cmd;
	u32 cmdlen = sizeof(cmd.id) + sizeof(cmd.set_app_start);
	int ret;

	if (ar->bmi.done_sent) {
		ath10k_warn(ar, "bmi set start command disallowed\n");
		return -EBUSY;
	}

	cmd.id = __cpu_to_le32(BMI_SET_APP_START);
	cmd.set_app_start.addr = __cpu_to_le32(address);

	ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
	if (ret) {
		ath10k_warn(ar, "unable to set start to the device:%d\n", ret);
		return ret;
	}

	return 0;
}
