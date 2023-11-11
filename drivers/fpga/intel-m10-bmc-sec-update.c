// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MAX10 Board Management Controller Secure Update Driver
 *
 * Copyright (C) 2019-2022 Intel Corporation. All rights reserved.
 *
 */
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct m10bmc_sec {
	struct device *dev;
	struct intel_m10bmc *m10bmc;
	struct fw_upload *fwl;
	char *fw_name;
	u32 fw_name_id;
	bool cancel_request;
};

static DEFINE_XARRAY_ALLOC(fw_upload_xa);

/* Root Entry Hash (REH) support */
#define REH_SHA256_SIZE		32
#define REH_SHA384_SIZE		48
#define REH_MAGIC		GENMASK(15, 0)
#define REH_SHA_NUM_BYTES	GENMASK(31, 16)

static ssize_t
show_root_entry_hash(struct device *dev, u32 exp_magic,
		     u32 prog_addr, u32 reh_addr, char *buf)
{
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	int sha_num_bytes, i, ret, cnt = 0;
	u8 hash[REH_SHA384_SIZE];
	unsigned int stride;
	u32 magic;

	stride = regmap_get_reg_stride(sec->m10bmc->regmap);
	ret = m10bmc_raw_read(sec->m10bmc, prog_addr, &magic);
	if (ret)
		return ret;

	if (FIELD_GET(REH_MAGIC, magic) != exp_magic)
		return sysfs_emit(buf, "hash not programmed\n");

	sha_num_bytes = FIELD_GET(REH_SHA_NUM_BYTES, magic) / 8;
	if ((sha_num_bytes % stride) ||
	    (sha_num_bytes != REH_SHA256_SIZE &&
	     sha_num_bytes != REH_SHA384_SIZE))   {
		dev_err(sec->dev, "%s bad sha num bytes %d\n", __func__,
			sha_num_bytes);
		return -EINVAL;
	}

	ret = regmap_bulk_read(sec->m10bmc->regmap, reh_addr,
			       hash, sha_num_bytes / stride);
	if (ret) {
		dev_err(dev, "failed to read root entry hash: %x cnt %x: %d\n",
			reh_addr, sha_num_bytes / stride, ret);
		return ret;
	}

	for (i = 0; i < sha_num_bytes; i++)
		cnt += sprintf(buf + cnt, "%02x", hash[i]);
	cnt += sprintf(buf + cnt, "\n");

	return cnt;
}

#define DEVICE_ATTR_SEC_REH_RO(_name, _magic, _prog_addr, _reh_addr) \
static ssize_t _name##_root_entry_hash_show(struct device *dev, \
					    struct device_attribute *attr, \
					    char *buf) \
{ return show_root_entry_hash(dev, _magic, _prog_addr, _reh_addr, buf); } \
static DEVICE_ATTR_RO(_name##_root_entry_hash)

DEVICE_ATTR_SEC_REH_RO(bmc, BMC_PROG_MAGIC, BMC_PROG_ADDR, BMC_REH_ADDR);
DEVICE_ATTR_SEC_REH_RO(sr, SR_PROG_MAGIC, SR_PROG_ADDR, SR_REH_ADDR);
DEVICE_ATTR_SEC_REH_RO(pr, PR_PROG_MAGIC, PR_PROG_ADDR, PR_REH_ADDR);

#define CSK_BIT_LEN		128U
#define CSK_32ARRAY_SIZE	DIV_ROUND_UP(CSK_BIT_LEN, 32)

static ssize_t
show_canceled_csk(struct device *dev, u32 addr, char *buf)
{
	unsigned int i, stride, size = CSK_32ARRAY_SIZE * sizeof(u32);
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	DECLARE_BITMAP(csk_map, CSK_BIT_LEN);
	__le32 csk_le32[CSK_32ARRAY_SIZE];
	u32 csk32[CSK_32ARRAY_SIZE];
	int ret;

	stride = regmap_get_reg_stride(sec->m10bmc->regmap);
	if (size % stride) {
		dev_err(sec->dev,
			"CSK vector size (0x%x) not aligned to stride (0x%x)\n",
			size, stride);
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	ret = regmap_bulk_read(sec->m10bmc->regmap, addr, csk_le32,
			       size / stride);
	if (ret) {
		dev_err(sec->dev, "failed to read CSK vector: %x cnt %x: %d\n",
			addr, size / stride, ret);
		return ret;
	}

	for (i = 0; i < CSK_32ARRAY_SIZE; i++)
		csk32[i] = le32_to_cpu(((csk_le32[i])));

	bitmap_from_arr32(csk_map, csk32, CSK_BIT_LEN);
	bitmap_complement(csk_map, csk_map, CSK_BIT_LEN);
	return bitmap_print_to_pagebuf(1, buf, csk_map, CSK_BIT_LEN);
}

#define DEVICE_ATTR_SEC_CSK_RO(_name, _addr) \
static ssize_t _name##_canceled_csks_show(struct device *dev, \
					  struct device_attribute *attr, \
					  char *buf) \
{ return show_canceled_csk(dev, _addr, buf); } \
static DEVICE_ATTR_RO(_name##_canceled_csks)

#define CSK_VEC_OFFSET 0x34

DEVICE_ATTR_SEC_CSK_RO(bmc, BMC_PROG_ADDR + CSK_VEC_OFFSET);
DEVICE_ATTR_SEC_CSK_RO(sr, SR_PROG_ADDR + CSK_VEC_OFFSET);
DEVICE_ATTR_SEC_CSK_RO(pr, PR_PROG_ADDR + CSK_VEC_OFFSET);

#define FLASH_COUNT_SIZE 4096	/* count stored as inverted bit vector */

static ssize_t flash_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	unsigned int stride, num_bits;
	u8 *flash_buf;
	int cnt, ret;

	stride = regmap_get_reg_stride(sec->m10bmc->regmap);
	num_bits = FLASH_COUNT_SIZE * 8;

	if (FLASH_COUNT_SIZE % stride) {
		dev_err(sec->dev,
			"FLASH_COUNT_SIZE (0x%x) not aligned to stride (0x%x)\n",
			FLASH_COUNT_SIZE, stride);
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	flash_buf = kmalloc(FLASH_COUNT_SIZE, GFP_KERNEL);
	if (!flash_buf)
		return -ENOMEM;

	ret = regmap_bulk_read(sec->m10bmc->regmap, STAGING_FLASH_COUNT,
			       flash_buf, FLASH_COUNT_SIZE / stride);
	if (ret) {
		dev_err(sec->dev,
			"failed to read flash count: %x cnt %x: %d\n",
			STAGING_FLASH_COUNT, FLASH_COUNT_SIZE / stride, ret);
		goto exit_free;
	}
	cnt = num_bits - bitmap_weight((unsigned long *)flash_buf, num_bits);

exit_free:
	kfree(flash_buf);

	return ret ? : sysfs_emit(buf, "%u\n", cnt);
}
static DEVICE_ATTR_RO(flash_count);

static struct attribute *m10bmc_security_attrs[] = {
	&dev_attr_flash_count.attr,
	&dev_attr_bmc_root_entry_hash.attr,
	&dev_attr_sr_root_entry_hash.attr,
	&dev_attr_pr_root_entry_hash.attr,
	&dev_attr_sr_canceled_csks.attr,
	&dev_attr_pr_canceled_csks.attr,
	&dev_attr_bmc_canceled_csks.attr,
	NULL,
};

static struct attribute_group m10bmc_security_attr_group = {
	.name = "security",
	.attrs = m10bmc_security_attrs,
};

static const struct attribute_group *m10bmc_sec_attr_groups[] = {
	&m10bmc_security_attr_group,
	NULL,
};

static void log_error_regs(struct m10bmc_sec *sec, u32 doorbell)
{
	u32 auth_result;

	dev_err(sec->dev, "RSU error status: 0x%08x\n", doorbell);

	if (!m10bmc_sys_read(sec->m10bmc, M10BMC_AUTH_RESULT, &auth_result))
		dev_err(sec->dev, "RSU auth result: 0x%08x\n", auth_result);
}

static enum fw_upload_err rsu_check_idle(struct m10bmc_sec *sec)
{
	u32 doorbell;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (rsu_prog(doorbell) != RSU_PROG_IDLE &&
	    rsu_prog(doorbell) != RSU_PROG_RSU_DONE) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_BUSY;
	}

	return FW_UPLOAD_ERR_NONE;
}

static inline bool rsu_start_done(u32 doorbell)
{
	u32 status, progress;

	if (doorbell & DRBL_RSU_REQUEST)
		return false;

	status = rsu_stat(doorbell);
	if (status == RSU_STAT_ERASE_FAIL || status == RSU_STAT_WEAROUT)
		return true;

	progress = rsu_prog(doorbell);
	if (progress != RSU_PROG_IDLE && progress != RSU_PROG_RSU_DONE)
		return true;

	return false;
}

static enum fw_upload_err rsu_update_init(struct m10bmc_sec *sec)
{
	u32 doorbell, status;
	int ret;

	ret = regmap_update_bits(sec->m10bmc->regmap,
				 M10BMC_SYS_BASE + M10BMC_DOORBELL,
				 DRBL_RSU_REQUEST | DRBL_HOST_STATUS,
				 DRBL_RSU_REQUEST |
				 FIELD_PREP(DRBL_HOST_STATUS,
					    HOST_STATUS_IDLE));
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	ret = regmap_read_poll_timeout(sec->m10bmc->regmap,
				       M10BMC_SYS_BASE + M10BMC_DOORBELL,
				       doorbell,
				       rsu_start_done(doorbell),
				       NIOS_HANDSHAKE_INTERVAL_US,
				       NIOS_HANDSHAKE_TIMEOUT_US);

	if (ret == -ETIMEDOUT) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (ret) {
		return FW_UPLOAD_ERR_RW_ERROR;
	}

	status = rsu_stat(doorbell);
	if (status == RSU_STAT_WEAROUT) {
		dev_warn(sec->dev, "Excessive flash update count detected\n");
		return FW_UPLOAD_ERR_WEAROUT;
	} else if (status == RSU_STAT_ERASE_FAIL) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err rsu_prog_ready(struct m10bmc_sec *sec)
{
	unsigned long poll_timeout;
	u32 doorbell, progress;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	poll_timeout = jiffies + msecs_to_jiffies(RSU_PREP_TIMEOUT_MS);
	while (rsu_prog(doorbell) == RSU_PROG_PREPARE) {
		msleep(RSU_PREP_INTERVAL_MS);
		if (time_after(jiffies, poll_timeout))
			break;

		ret = m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, &doorbell);
		if (ret)
			return FW_UPLOAD_ERR_RW_ERROR;
	}

	progress = rsu_prog(doorbell);
	if (progress == RSU_PROG_PREPARE) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (progress != RSU_PROG_READY) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err rsu_send_data(struct m10bmc_sec *sec)
{
	u32 doorbell;
	int ret;

	ret = regmap_update_bits(sec->m10bmc->regmap,
				 M10BMC_SYS_BASE + M10BMC_DOORBELL,
				 DRBL_HOST_STATUS,
				 FIELD_PREP(DRBL_HOST_STATUS,
					    HOST_STATUS_WRITE_DONE));
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	ret = regmap_read_poll_timeout(sec->m10bmc->regmap,
				       M10BMC_SYS_BASE + M10BMC_DOORBELL,
				       doorbell,
				       rsu_prog(doorbell) != RSU_PROG_READY,
				       NIOS_HANDSHAKE_INTERVAL_US,
				       NIOS_HANDSHAKE_TIMEOUT_US);

	if (ret == -ETIMEDOUT) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (ret) {
		return FW_UPLOAD_ERR_RW_ERROR;
	}

	switch (rsu_stat(doorbell)) {
	case RSU_STAT_NORMAL:
	case RSU_STAT_NIOS_OK:
	case RSU_STAT_USER_OK:
	case RSU_STAT_FACTORY_OK:
		break;
	default:
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static int rsu_check_complete(struct m10bmc_sec *sec, u32 *doorbell)
{
	if (m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, doorbell))
		return -EIO;

	switch (rsu_stat(*doorbell)) {
	case RSU_STAT_NORMAL:
	case RSU_STAT_NIOS_OK:
	case RSU_STAT_USER_OK:
	case RSU_STAT_FACTORY_OK:
		break;
	default:
		return -EINVAL;
	}

	switch (rsu_prog(*doorbell)) {
	case RSU_PROG_IDLE:
	case RSU_PROG_RSU_DONE:
		return 0;
	case RSU_PROG_AUTHENTICATING:
	case RSU_PROG_COPYING:
	case RSU_PROG_UPDATE_CANCEL:
	case RSU_PROG_PROGRAM_KEY_HASH:
		return -EAGAIN;
	default:
		return -EINVAL;
	}
}

static enum fw_upload_err rsu_cancel(struct m10bmc_sec *sec)
{
	u32 doorbell;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (rsu_prog(doorbell) != RSU_PROG_READY)
		return FW_UPLOAD_ERR_BUSY;

	ret = regmap_update_bits(sec->m10bmc->regmap,
				 M10BMC_SYS_BASE + M10BMC_DOORBELL,
				 DRBL_HOST_STATUS,
				 FIELD_PREP(DRBL_HOST_STATUS,
					    HOST_STATUS_ABORT_RSU));
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	return FW_UPLOAD_ERR_CANCELED;
}

static enum fw_upload_err m10bmc_sec_prepare(struct fw_upload *fwl,
					     const u8 *data, u32 size)
{
	struct m10bmc_sec *sec = fwl->dd_handle;
	u32 ret;

	sec->cancel_request = false;

	if (!size || size > M10BMC_STAGING_SIZE)
		return FW_UPLOAD_ERR_INVALID_SIZE;

	ret = rsu_check_idle(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = rsu_update_init(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	ret = rsu_prog_ready(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		return ret;

	if (sec->cancel_request)
		return rsu_cancel(sec);

	return FW_UPLOAD_ERR_NONE;
}

#define WRITE_BLOCK_SIZE 0x4000	/* Default write-block size is 0x4000 bytes */

static enum fw_upload_err m10bmc_sec_write(struct fw_upload *fwl, const u8 *data,
					   u32 offset, u32 size, u32 *written)
{
	struct m10bmc_sec *sec = fwl->dd_handle;
	u32 blk_size, doorbell, extra_offset;
	unsigned int stride, extra = 0;
	int ret;

	stride = regmap_get_reg_stride(sec->m10bmc->regmap);
	if (sec->cancel_request)
		return rsu_cancel(sec);

	ret = m10bmc_sys_read(sec->m10bmc, M10BMC_DOORBELL, &doorbell);
	if (ret) {
		return FW_UPLOAD_ERR_RW_ERROR;
	} else if (rsu_prog(doorbell) != RSU_PROG_READY) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	WARN_ON_ONCE(WRITE_BLOCK_SIZE % stride);
	blk_size = min_t(u32, WRITE_BLOCK_SIZE, size);
	ret = regmap_bulk_write(sec->m10bmc->regmap,
				M10BMC_STAGING_BASE + offset,
				(void *)data + offset,
				blk_size / stride);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	/*
	 * If blk_size is not aligned to stride, then handle the extra
	 * bytes with regmap_write.
	 */
	if (blk_size % stride) {
		extra_offset = offset + ALIGN_DOWN(blk_size, stride);
		memcpy(&extra, (u8 *)(data + extra_offset), blk_size % stride);
		ret = regmap_write(sec->m10bmc->regmap,
				   M10BMC_STAGING_BASE + extra_offset, extra);
		if (ret)
			return FW_UPLOAD_ERR_RW_ERROR;
	}

	*written = blk_size;
	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err m10bmc_sec_poll_complete(struct fw_upload *fwl)
{
	struct m10bmc_sec *sec = fwl->dd_handle;
	unsigned long poll_timeout;
	u32 doorbell, result;
	int ret;

	if (sec->cancel_request)
		return rsu_cancel(sec);

	result = rsu_send_data(sec);
	if (result != FW_UPLOAD_ERR_NONE)
		return result;

	poll_timeout = jiffies + msecs_to_jiffies(RSU_COMPLETE_TIMEOUT_MS);
	do {
		msleep(RSU_COMPLETE_INTERVAL_MS);
		ret = rsu_check_complete(sec, &doorbell);
	} while (ret == -EAGAIN && !time_after(jiffies, poll_timeout));

	if (ret == -EAGAIN) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (ret == -EIO) {
		return FW_UPLOAD_ERR_RW_ERROR;
	} else if (ret) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

/*
 * m10bmc_sec_cancel() may be called asynchronously with an on-going update.
 * All other functions are called sequentially in a single thread. To avoid
 * contention on register accesses, m10bmc_sec_cancel() must only update
 * the cancel_request flag. Other functions will check this flag and handle
 * the cancel request synchronously.
 */
static void m10bmc_sec_cancel(struct fw_upload *fwl)
{
	struct m10bmc_sec *sec = fwl->dd_handle;

	sec->cancel_request = true;
}

static void m10bmc_sec_cleanup(struct fw_upload *fwl)
{
	struct m10bmc_sec *sec = fwl->dd_handle;

	(void)rsu_cancel(sec);
}

static const struct fw_upload_ops m10bmc_ops = {
	.prepare = m10bmc_sec_prepare,
	.write = m10bmc_sec_write,
	.poll_complete = m10bmc_sec_poll_complete,
	.cancel = m10bmc_sec_cancel,
	.cleanup = m10bmc_sec_cleanup,
};

#define SEC_UPDATE_LEN_MAX 32
static int m10bmc_sec_probe(struct platform_device *pdev)
{
	char buf[SEC_UPDATE_LEN_MAX];
	struct m10bmc_sec *sec;
	struct fw_upload *fwl;
	unsigned int len;
	int  ret;

	sec = devm_kzalloc(&pdev->dev, sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	sec->dev = &pdev->dev;
	sec->m10bmc = dev_get_drvdata(pdev->dev.parent);
	dev_set_drvdata(&pdev->dev, sec);

	ret = xa_alloc(&fw_upload_xa, &sec->fw_name_id, sec,
		       xa_limit_32b, GFP_KERNEL);
	if (ret)
		return ret;

	len = scnprintf(buf, SEC_UPDATE_LEN_MAX, "secure-update%d",
			sec->fw_name_id);
	sec->fw_name = kmemdup_nul(buf, len, GFP_KERNEL);
	if (!sec->fw_name) {
		ret = -ENOMEM;
		goto fw_name_fail;
	}

	fwl = firmware_upload_register(THIS_MODULE, sec->dev, sec->fw_name,
				       &m10bmc_ops, sec);
	if (IS_ERR(fwl)) {
		dev_err(sec->dev, "Firmware Upload driver failed to start\n");
		ret = PTR_ERR(fwl);
		goto fw_uploader_fail;
	}

	sec->fwl = fwl;
	return 0;

fw_uploader_fail:
	kfree(sec->fw_name);
fw_name_fail:
	xa_erase(&fw_upload_xa, sec->fw_name_id);
	return ret;
}

static int m10bmc_sec_remove(struct platform_device *pdev)
{
	struct m10bmc_sec *sec = dev_get_drvdata(&pdev->dev);

	firmware_upload_unregister(sec->fwl);
	kfree(sec->fw_name);
	xa_erase(&fw_upload_xa, sec->fw_name_id);

	return 0;
}

static const struct platform_device_id intel_m10bmc_sec_ids[] = {
	{
		.name = "n3000bmc-sec-update",
	},
	{
		.name = "d5005bmc-sec-update",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, intel_m10bmc_sec_ids);

static struct platform_driver intel_m10bmc_sec_driver = {
	.probe = m10bmc_sec_probe,
	.remove = m10bmc_sec_remove,
	.driver = {
		.name = "intel-m10bmc-sec-update",
		.dev_groups = m10bmc_sec_attr_groups,
	},
	.id_table = intel_m10bmc_sec_ids,
};
module_platform_driver(intel_m10bmc_sec_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel MAX10 BMC Secure Update");
MODULE_LICENSE("GPL");
