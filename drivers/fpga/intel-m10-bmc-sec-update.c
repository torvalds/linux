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

struct m10bmc_sec;

struct m10bmc_sec_ops {
	int (*rsu_status)(struct m10bmc_sec *sec);
};

struct m10bmc_sec {
	struct device *dev;
	struct intel_m10bmc *m10bmc;
	struct fw_upload *fwl;
	char *fw_name;
	u32 fw_name_id;
	bool cancel_request;
	const struct m10bmc_sec_ops *ops;
};

static DEFINE_XARRAY_ALLOC(fw_upload_xa);

/* Root Entry Hash (REH) support */
#define REH_SHA256_SIZE		32
#define REH_SHA384_SIZE		48
#define REH_MAGIC		GENMASK(15, 0)
#define REH_SHA_NUM_BYTES	GENMASK(31, 16)

static int m10bmc_sec_write(struct m10bmc_sec *sec, const u8 *buf, u32 offset, u32 size)
{
	struct intel_m10bmc *m10bmc = sec->m10bmc;
	unsigned int stride = regmap_get_reg_stride(m10bmc->regmap);
	u32 write_count = size / stride;
	u32 leftover_offset = write_count * stride;
	u32 leftover_size = size - leftover_offset;
	u32 leftover_tmp = 0;
	int ret;

	if (sec->m10bmc->flash_bulk_ops)
		return sec->m10bmc->flash_bulk_ops->write(m10bmc, buf, offset, size);

	if (WARN_ON_ONCE(stride > sizeof(leftover_tmp)))
		return -EINVAL;

	ret = regmap_bulk_write(m10bmc->regmap, M10BMC_STAGING_BASE + offset,
				buf + offset, write_count);
	if (ret)
		return ret;

	/* If size is not aligned to stride, handle the remainder bytes with regmap_write() */
	if (leftover_size) {
		memcpy(&leftover_tmp, buf + leftover_offset, leftover_size);
		ret = regmap_write(m10bmc->regmap, M10BMC_STAGING_BASE + offset + leftover_offset,
				   leftover_tmp);
		if (ret)
			return ret;
	}

	return 0;
}

static int m10bmc_sec_read(struct m10bmc_sec *sec, u8 *buf, u32 addr, u32 size)
{
	struct intel_m10bmc *m10bmc = sec->m10bmc;
	unsigned int stride = regmap_get_reg_stride(m10bmc->regmap);
	u32 read_count = size / stride;
	u32 leftover_offset = read_count * stride;
	u32 leftover_size = size - leftover_offset;
	u32 leftover_tmp;
	int ret;

	if (sec->m10bmc->flash_bulk_ops)
		return sec->m10bmc->flash_bulk_ops->read(m10bmc, buf, addr, size);

	if (WARN_ON_ONCE(stride > sizeof(leftover_tmp)))
		return -EINVAL;

	ret = regmap_bulk_read(m10bmc->regmap, addr, buf, read_count);
	if (ret)
		return ret;

	/* If size is not aligned to stride, handle the remainder bytes with regmap_read() */
	if (leftover_size) {
		ret = regmap_read(m10bmc->regmap, addr + leftover_offset, &leftover_tmp);
		if (ret)
			return ret;
		memcpy(buf + leftover_offset, &leftover_tmp, leftover_size);
	}

	return 0;
}


static ssize_t
show_root_entry_hash(struct device *dev, u32 exp_magic,
		     u32 prog_addr, u32 reh_addr, char *buf)
{
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	int sha_num_bytes, i, ret, cnt = 0;
	u8 hash[REH_SHA384_SIZE];
	u32 magic;

	ret = m10bmc_sec_read(sec, (u8 *)&magic, prog_addr, sizeof(magic));
	if (ret)
		return ret;

	if (FIELD_GET(REH_MAGIC, magic) != exp_magic)
		return sysfs_emit(buf, "hash not programmed\n");

	sha_num_bytes = FIELD_GET(REH_SHA_NUM_BYTES, magic) / 8;
	if (sha_num_bytes != REH_SHA256_SIZE &&
	    sha_num_bytes != REH_SHA384_SIZE) {
		dev_err(sec->dev, "%s bad sha num bytes %d\n", __func__,
			sha_num_bytes);
		return -EINVAL;
	}

	ret = m10bmc_sec_read(sec, hash, reh_addr, sha_num_bytes);
	if (ret) {
		dev_err(dev, "failed to read root entry hash\n");
		return ret;
	}

	for (i = 0; i < sha_num_bytes; i++)
		cnt += sprintf(buf + cnt, "%02x", hash[i]);
	cnt += sprintf(buf + cnt, "\n");

	return cnt;
}

#define DEVICE_ATTR_SEC_REH_RO(_name)						\
static ssize_t _name##_root_entry_hash_show(struct device *dev, \
					    struct device_attribute *attr, \
					    char *buf) \
{										\
	struct m10bmc_sec *sec = dev_get_drvdata(dev);				\
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;	\
										\
	return show_root_entry_hash(dev, csr_map->_name##_magic,		\
				    csr_map->_name##_prog_addr,			\
				    csr_map->_name##_reh_addr,			\
				    buf);					\
}										\
static DEVICE_ATTR_RO(_name##_root_entry_hash)

DEVICE_ATTR_SEC_REH_RO(bmc);
DEVICE_ATTR_SEC_REH_RO(sr);
DEVICE_ATTR_SEC_REH_RO(pr);

#define CSK_BIT_LEN		128U
#define CSK_32ARRAY_SIZE	DIV_ROUND_UP(CSK_BIT_LEN, 32)

static ssize_t
show_canceled_csk(struct device *dev, u32 addr, char *buf)
{
	unsigned int i, size = CSK_32ARRAY_SIZE * sizeof(u32);
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	DECLARE_BITMAP(csk_map, CSK_BIT_LEN);
	__le32 csk_le32[CSK_32ARRAY_SIZE];
	u32 csk32[CSK_32ARRAY_SIZE];
	int ret;

	ret = m10bmc_sec_read(sec, (u8 *)&csk_le32, addr, size);
	if (ret) {
		dev_err(sec->dev, "failed to read CSK vector\n");
		return ret;
	}

	for (i = 0; i < CSK_32ARRAY_SIZE; i++)
		csk32[i] = le32_to_cpu(((csk_le32[i])));

	bitmap_from_arr32(csk_map, csk32, CSK_BIT_LEN);
	bitmap_complement(csk_map, csk_map, CSK_BIT_LEN);
	return bitmap_print_to_pagebuf(1, buf, csk_map, CSK_BIT_LEN);
}

#define DEVICE_ATTR_SEC_CSK_RO(_name)						\
static ssize_t _name##_canceled_csks_show(struct device *dev, \
					  struct device_attribute *attr, \
					  char *buf) \
{										\
	struct m10bmc_sec *sec = dev_get_drvdata(dev);				\
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;	\
										\
	return show_canceled_csk(dev,						\
				 csr_map->_name##_prog_addr + CSK_VEC_OFFSET,	\
				 buf);						\
}										\
static DEVICE_ATTR_RO(_name##_canceled_csks)

#define CSK_VEC_OFFSET 0x34

DEVICE_ATTR_SEC_CSK_RO(bmc);
DEVICE_ATTR_SEC_CSK_RO(sr);
DEVICE_ATTR_SEC_CSK_RO(pr);

#define FLASH_COUNT_SIZE 4096	/* count stored as inverted bit vector */

static ssize_t flash_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct m10bmc_sec *sec = dev_get_drvdata(dev);
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	unsigned int num_bits;
	u8 *flash_buf;
	int cnt, ret;

	num_bits = FLASH_COUNT_SIZE * 8;

	flash_buf = kmalloc(FLASH_COUNT_SIZE, GFP_KERNEL);
	if (!flash_buf)
		return -ENOMEM;

	ret = m10bmc_sec_read(sec, flash_buf, csr_map->rsu_update_counter,
			      FLASH_COUNT_SIZE);
	if (ret) {
		dev_err(sec->dev, "failed to read flash count\n");
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
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 auth_result;

	dev_err(sec->dev, "Doorbell: 0x%08x\n", doorbell);

	if (!m10bmc_sys_read(sec->m10bmc, csr_map->auth_result, &auth_result))
		dev_err(sec->dev, "RSU auth result: 0x%08x\n", auth_result);
}

static int m10bmc_sec_n3000_rsu_status(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 doorbell;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, &doorbell);
	if (ret)
		return ret;

	return FIELD_GET(DRBL_RSU_STATUS, doorbell);
}

static int m10bmc_sec_n6000_rsu_status(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 auth_result;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->auth_result, &auth_result);
	if (ret)
		return ret;

	return FIELD_GET(AUTH_RESULT_RSU_STATUS, auth_result);
}

static bool rsu_status_ok(u32 status)
{
	return (status == RSU_STAT_NORMAL ||
		status == RSU_STAT_NIOS_OK ||
		status == RSU_STAT_USER_OK ||
		status == RSU_STAT_FACTORY_OK);
}

static bool rsu_progress_done(u32 progress)
{
	return (progress == RSU_PROG_IDLE ||
		progress == RSU_PROG_RSU_DONE);
}

static bool rsu_progress_busy(u32 progress)
{
	return (progress == RSU_PROG_AUTHENTICATING ||
		progress == RSU_PROG_COPYING ||
		progress == RSU_PROG_UPDATE_CANCEL ||
		progress == RSU_PROG_PROGRAM_KEY_HASH);
}

static int m10bmc_sec_progress_status(struct m10bmc_sec *sec, u32 *doorbell_reg,
				      u32 *progress, u32 *status)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, doorbell_reg);
	if (ret)
		return ret;

	ret = sec->ops->rsu_status(sec);
	if (ret < 0)
		return ret;

	*status = ret;
	*progress = rsu_prog(*doorbell_reg);

	return 0;
}

static enum fw_upload_err rsu_check_idle(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 doorbell;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (!rsu_progress_done(rsu_prog(doorbell))) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_BUSY;
	}

	return FW_UPLOAD_ERR_NONE;
}

static inline bool rsu_start_done(u32 doorbell_reg, u32 progress, u32 status)
{
	if (doorbell_reg & DRBL_RSU_REQUEST)
		return false;

	if (status == RSU_STAT_ERASE_FAIL || status == RSU_STAT_WEAROUT)
		return true;

	if (!rsu_progress_done(progress))
		return true;

	return false;
}

static enum fw_upload_err rsu_update_init(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 doorbell_reg, progress, status;
	int ret, err;

	ret = m10bmc_sys_update_bits(sec->m10bmc, csr_map->doorbell,
				     DRBL_RSU_REQUEST | DRBL_HOST_STATUS,
				     DRBL_RSU_REQUEST |
				     FIELD_PREP(DRBL_HOST_STATUS,
						HOST_STATUS_IDLE));
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	ret = read_poll_timeout(m10bmc_sec_progress_status, err,
				err < 0 || rsu_start_done(doorbell_reg, progress, status),
				NIOS_HANDSHAKE_INTERVAL_US,
				NIOS_HANDSHAKE_TIMEOUT_US,
				false,
				sec, &doorbell_reg, &progress, &status);

	if (ret == -ETIMEDOUT) {
		log_error_regs(sec, doorbell_reg);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (err) {
		return FW_UPLOAD_ERR_RW_ERROR;
	}

	if (status == RSU_STAT_WEAROUT) {
		dev_warn(sec->dev, "Excessive flash update count detected\n");
		return FW_UPLOAD_ERR_WEAROUT;
	} else if (status == RSU_STAT_ERASE_FAIL) {
		log_error_regs(sec, doorbell_reg);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err rsu_prog_ready(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	unsigned long poll_timeout;
	u32 doorbell, progress;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	poll_timeout = jiffies + msecs_to_jiffies(RSU_PREP_TIMEOUT_MS);
	while (rsu_prog(doorbell) == RSU_PROG_PREPARE) {
		msleep(RSU_PREP_INTERVAL_MS);
		if (time_after(jiffies, poll_timeout))
			break;

		ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, &doorbell);
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
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 doorbell_reg, status;
	int ret;

	ret = m10bmc_sys_update_bits(sec->m10bmc, csr_map->doorbell,
				     DRBL_HOST_STATUS,
				     FIELD_PREP(DRBL_HOST_STATUS,
						HOST_STATUS_WRITE_DONE));
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	ret = regmap_read_poll_timeout(sec->m10bmc->regmap,
				       csr_map->base + csr_map->doorbell,
				       doorbell_reg,
				       rsu_prog(doorbell_reg) != RSU_PROG_READY,
				       NIOS_HANDSHAKE_INTERVAL_US,
				       NIOS_HANDSHAKE_TIMEOUT_US);

	if (ret == -ETIMEDOUT) {
		log_error_regs(sec, doorbell_reg);
		return FW_UPLOAD_ERR_TIMEOUT;
	} else if (ret) {
		return FW_UPLOAD_ERR_RW_ERROR;
	}

	ret = sec->ops->rsu_status(sec);
	if (ret < 0)
		return FW_UPLOAD_ERR_HW_ERROR;
	status = ret;

	if (!rsu_status_ok(status)) {
		log_error_regs(sec, doorbell_reg);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	return FW_UPLOAD_ERR_NONE;
}

static int rsu_check_complete(struct m10bmc_sec *sec, u32 *doorbell_reg)
{
	u32 progress, status;

	if (m10bmc_sec_progress_status(sec, doorbell_reg, &progress, &status))
		return -EIO;

	if (!rsu_status_ok(status))
		return -EINVAL;

	if (rsu_progress_done(progress))
		return 0;

	if (rsu_progress_busy(progress))
		return -EAGAIN;

	return -EINVAL;
}

static enum fw_upload_err rsu_cancel(struct m10bmc_sec *sec)
{
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	u32 doorbell;
	int ret;

	ret = m10bmc_sys_read(sec->m10bmc, csr_map->doorbell, &doorbell);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (rsu_prog(doorbell) != RSU_PROG_READY)
		return FW_UPLOAD_ERR_BUSY;

	ret = m10bmc_sys_update_bits(sec->m10bmc, csr_map->doorbell,
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

	if (sec->m10bmc->flash_bulk_ops)
		if (sec->m10bmc->flash_bulk_ops->lock_write(sec->m10bmc))
			return FW_UPLOAD_ERR_BUSY;

	ret = rsu_check_idle(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		goto unlock_flash;

	m10bmc_fw_state_set(sec->m10bmc, M10BMC_FW_STATE_SEC_UPDATE_PREPARE);

	ret = rsu_update_init(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		goto fw_state_exit;

	ret = rsu_prog_ready(sec);
	if (ret != FW_UPLOAD_ERR_NONE)
		goto fw_state_exit;

	if (sec->cancel_request) {
		ret = rsu_cancel(sec);
		goto fw_state_exit;
	}

	m10bmc_fw_state_set(sec->m10bmc, M10BMC_FW_STATE_SEC_UPDATE_WRITE);

	return FW_UPLOAD_ERR_NONE;

fw_state_exit:
	m10bmc_fw_state_set(sec->m10bmc, M10BMC_FW_STATE_NORMAL);

unlock_flash:
	if (sec->m10bmc->flash_bulk_ops)
		sec->m10bmc->flash_bulk_ops->unlock_write(sec->m10bmc);
	return ret;
}

#define WRITE_BLOCK_SIZE 0x4000	/* Default write-block size is 0x4000 bytes */

static enum fw_upload_err m10bmc_sec_fw_write(struct fw_upload *fwl, const u8 *data,
					      u32 offset, u32 size, u32 *written)
{
	struct m10bmc_sec *sec = fwl->dd_handle;
	const struct m10bmc_csr_map *csr_map = sec->m10bmc->info->csr_map;
	struct intel_m10bmc *m10bmc = sec->m10bmc;
	u32 blk_size, doorbell;
	int ret;

	if (sec->cancel_request)
		return rsu_cancel(sec);

	ret = m10bmc_sys_read(m10bmc, csr_map->doorbell, &doorbell);
	if (ret) {
		return FW_UPLOAD_ERR_RW_ERROR;
	} else if (rsu_prog(doorbell) != RSU_PROG_READY) {
		log_error_regs(sec, doorbell);
		return FW_UPLOAD_ERR_HW_ERROR;
	}

	WARN_ON_ONCE(WRITE_BLOCK_SIZE % regmap_get_reg_stride(m10bmc->regmap));
	blk_size = min_t(u32, WRITE_BLOCK_SIZE, size);
	ret = m10bmc_sec_write(sec, data, offset, blk_size);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

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

	m10bmc_fw_state_set(sec->m10bmc, M10BMC_FW_STATE_SEC_UPDATE_PROGRAM);

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

	m10bmc_fw_state_set(sec->m10bmc, M10BMC_FW_STATE_NORMAL);

	if (sec->m10bmc->flash_bulk_ops)
		sec->m10bmc->flash_bulk_ops->unlock_write(sec->m10bmc);
}

static const struct fw_upload_ops m10bmc_ops = {
	.prepare = m10bmc_sec_prepare,
	.write = m10bmc_sec_fw_write,
	.poll_complete = m10bmc_sec_poll_complete,
	.cancel = m10bmc_sec_cancel,
	.cleanup = m10bmc_sec_cleanup,
};

static const struct m10bmc_sec_ops m10sec_n3000_ops = {
	.rsu_status = m10bmc_sec_n3000_rsu_status,
};

static const struct m10bmc_sec_ops m10sec_n6000_ops = {
	.rsu_status = m10bmc_sec_n6000_rsu_status,
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
	sec->ops = (struct m10bmc_sec_ops *)platform_get_device_id(pdev)->driver_data;
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

static void m10bmc_sec_remove(struct platform_device *pdev)
{
	struct m10bmc_sec *sec = dev_get_drvdata(&pdev->dev);

	firmware_upload_unregister(sec->fwl);
	kfree(sec->fw_name);
	xa_erase(&fw_upload_xa, sec->fw_name_id);
}

static const struct platform_device_id intel_m10bmc_sec_ids[] = {
	{
		.name = "n3000bmc-sec-update",
		.driver_data = (kernel_ulong_t)&m10sec_n3000_ops,
	},
	{
		.name = "d5005bmc-sec-update",
		.driver_data = (kernel_ulong_t)&m10sec_n3000_ops,
	},
	{
		.name = "n6000bmc-sec-update",
		.driver_data = (kernel_ulong_t)&m10sec_n6000_ops,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, intel_m10bmc_sec_ids);

static struct platform_driver intel_m10bmc_sec_driver = {
	.probe = m10bmc_sec_probe,
	.remove_new = m10bmc_sec_remove,
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
MODULE_IMPORT_NS(INTEL_M10_BMC_CORE);
