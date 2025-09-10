// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox boot control driver
 *
 * This driver provides a sysfs interface for systems management
 * software to manage reset-time actions.
 *
 * Copyright (C) 2019 Mellanox Technologies
 */

#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "mlxbf-bootctl.h"

#define MLXBF_BOOTCTL_SB_SECURE_MASK		0x03
#define MLXBF_BOOTCTL_SB_TEST_MASK		0x0c
#define MLXBF_BOOTCTL_SB_DEV_MASK		BIT(4)

#define MLXBF_SB_KEY_NUM			4

/* UUID used to probe ATF service. */
static const char *mlxbf_bootctl_svc_uuid_str =
	"89c036b4-e7d7-11e6-8797-001aca00bfc4";

struct mlxbf_bootctl_name {
	u32 value;
	const char *name;
};

static struct mlxbf_bootctl_name boot_names[] = {
	{ MLXBF_BOOTCTL_EXTERNAL, "external" },
	{ MLXBF_BOOTCTL_EMMC, "emmc" },
	{ MLNX_BOOTCTL_SWAP_EMMC, "swap_emmc" },
	{ MLXBF_BOOTCTL_EMMC_LEGACY, "emmc_legacy" },
	{ MLXBF_BOOTCTL_NONE, "none" },
};

enum {
	MLXBF_BOOTCTL_SB_LIFECYCLE_PRODUCTION = 0,
	MLXBF_BOOTCTL_SB_LIFECYCLE_GA_SECURE = 1,
	MLXBF_BOOTCTL_SB_LIFECYCLE_GA_NON_SECURE = 2,
	MLXBF_BOOTCTL_SB_LIFECYCLE_RMA = 3
};

static const char * const mlxbf_bootctl_lifecycle_states[] = {
	[MLXBF_BOOTCTL_SB_LIFECYCLE_PRODUCTION] = "Production",
	[MLXBF_BOOTCTL_SB_LIFECYCLE_GA_SECURE] = "GA Secured",
	[MLXBF_BOOTCTL_SB_LIFECYCLE_GA_NON_SECURE] = "GA Non-Secured",
	[MLXBF_BOOTCTL_SB_LIFECYCLE_RMA] = "RMA",
};

/* Log header format. */
#define MLXBF_RSH_LOG_TYPE_MASK		GENMASK_ULL(59, 56)
#define MLXBF_RSH_LOG_LEN_MASK		GENMASK_ULL(54, 48)
#define MLXBF_RSH_LOG_LEVEL_MASK	GENMASK_ULL(7, 0)

/* Log module ID and type (only MSG type in Linux driver for now). */
#define MLXBF_RSH_LOG_TYPE_MSG		0x04ULL

/* Log ctl/data register offset. */
#define MLXBF_RSH_SCRATCH_BUF_CTL_OFF	0
#define MLXBF_RSH_SCRATCH_BUF_DATA_OFF	0x10

/* Log message levels. */
enum {
	MLXBF_RSH_LOG_INFO,
	MLXBF_RSH_LOG_WARN,
	MLXBF_RSH_LOG_ERR,
	MLXBF_RSH_LOG_ASSERT
};

/* Mapped pointer for RSH_BOOT_FIFO_DATA and RSH_BOOT_FIFO_COUNT register. */
static void __iomem *mlxbf_rsh_boot_data;
static void __iomem *mlxbf_rsh_boot_cnt;

/* Mapped pointer for rsh log semaphore/ctrl/data register. */
static void __iomem *mlxbf_rsh_semaphore;
static void __iomem *mlxbf_rsh_scratch_buf_ctl;
static void __iomem *mlxbf_rsh_scratch_buf_data;

/* Rsh log levels. */
static const char * const mlxbf_rsh_log_level[] = {
	"INFO", "WARN", "ERR", "ASSERT"};

static DEFINE_MUTEX(icm_ops_lock);
static DEFINE_MUTEX(os_up_lock);
static DEFINE_MUTEX(mfg_ops_lock);
static DEFINE_MUTEX(rtc_ops_lock);

/*
 * Objects are stored within the MFG partition per type.
 * Type 0 is not supported.
 */
enum {
	MLNX_MFG_TYPE_OOB_MAC = 1,
	MLNX_MFG_TYPE_OPN_0,
	MLNX_MFG_TYPE_OPN_1,
	MLNX_MFG_TYPE_OPN_2,
	MLNX_MFG_TYPE_SKU_0,
	MLNX_MFG_TYPE_SKU_1,
	MLNX_MFG_TYPE_SKU_2,
	MLNX_MFG_TYPE_MODL_0,
	MLNX_MFG_TYPE_MODL_1,
	MLNX_MFG_TYPE_MODL_2,
	MLNX_MFG_TYPE_SN_0,
	MLNX_MFG_TYPE_SN_1,
	MLNX_MFG_TYPE_SN_2,
	MLNX_MFG_TYPE_UUID_0,
	MLNX_MFG_TYPE_UUID_1,
	MLNX_MFG_TYPE_UUID_2,
	MLNX_MFG_TYPE_UUID_3,
	MLNX_MFG_TYPE_UUID_4,
	MLNX_MFG_TYPE_REV,
};

#define MLNX_MFG_OPN_VAL_LEN         24
#define MLNX_MFG_SKU_VAL_LEN         24
#define MLNX_MFG_MODL_VAL_LEN        24
#define MLNX_MFG_SN_VAL_LEN          24
#define MLNX_MFG_UUID_VAL_LEN        40
#define MLNX_MFG_REV_VAL_LEN         8
#define MLNX_MFG_VAL_QWORD_CNT(type) \
	(MLNX_MFG_##type##_VAL_LEN / sizeof(u64))

/*
 * The MAC address consists of 6 bytes (2 digits each) separated by ':'.
 * The expected format is: "XX:XX:XX:XX:XX:XX"
 */
#define MLNX_MFG_OOB_MAC_FORMAT_LEN \
	((ETH_ALEN * 2) + (ETH_ALEN - 1))

/* ARM SMC call which is atomic and no need for lock. */
static int mlxbf_bootctl_smc(unsigned int smc_op, int smc_arg)
{
	struct arm_smccc_res res;

	arm_smccc_smc(smc_op, smc_arg, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

/* Return the action in integer or an error code. */
static int mlxbf_bootctl_reset_action_to_val(const char *action)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(boot_names); i++)
		if (sysfs_streq(boot_names[i].name, action))
			return boot_names[i].value;

	return -EINVAL;
}

/* Return the action in string. */
static const char *mlxbf_bootctl_action_to_string(int action)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(boot_names); i++)
		if (boot_names[i].value == action)
			return boot_names[i].name;

	return "invalid action";
}

static ssize_t post_reset_wdog_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int ret;

	ret = mlxbf_bootctl_smc(MLXBF_BOOTCTL_GET_POST_RESET_WDOG, 0);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", ret);
}

static ssize_t post_reset_wdog_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;

	ret = mlxbf_bootctl_smc(MLXBF_BOOTCTL_SET_POST_RESET_WDOG, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t mlxbf_bootctl_show(int smc_op, char *buf)
{
	int action;

	action = mlxbf_bootctl_smc(smc_op, 0);
	if (action < 0)
		return action;

	return sysfs_emit(buf, "%s\n", mlxbf_bootctl_action_to_string(action));
}

static int mlxbf_bootctl_store(int smc_op, const char *buf, size_t count)
{
	int ret, action;

	action = mlxbf_bootctl_reset_action_to_val(buf);
	if (action < 0)
		return action;

	ret = mlxbf_bootctl_smc(smc_op, action);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t reset_action_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return mlxbf_bootctl_show(MLXBF_BOOTCTL_GET_RESET_ACTION, buf);
}

static ssize_t reset_action_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	return mlxbf_bootctl_store(MLXBF_BOOTCTL_SET_RESET_ACTION, buf, count);
}

static ssize_t second_reset_action_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return mlxbf_bootctl_show(MLXBF_BOOTCTL_GET_SECOND_RESET_ACTION, buf);
}

static ssize_t second_reset_action_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	return mlxbf_bootctl_store(MLXBF_BOOTCTL_SET_SECOND_RESET_ACTION, buf,
				   count);
}

static ssize_t lifecycle_state_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int status_bits;
	int use_dev_key;
	int test_state;
	int lc_state;

	status_bits = mlxbf_bootctl_smc(MLXBF_BOOTCTL_GET_TBB_FUSE_STATUS,
					MLXBF_BOOTCTL_FUSE_STATUS_LIFECYCLE);
	if (status_bits < 0)
		return status_bits;

	use_dev_key = status_bits & MLXBF_BOOTCTL_SB_DEV_MASK;
	test_state = status_bits & MLXBF_BOOTCTL_SB_TEST_MASK;
	lc_state = status_bits & MLXBF_BOOTCTL_SB_SECURE_MASK;

	/*
	 * If the test bits are set, we specify that the current state may be
	 * due to using the test bits.
	 */
	if (test_state) {
		return sysfs_emit(buf, "%s(test)\n",
			       mlxbf_bootctl_lifecycle_states[lc_state]);
	} else if (use_dev_key &&
		   (lc_state == MLXBF_BOOTCTL_SB_LIFECYCLE_GA_SECURE)) {
		return sysfs_emit(buf, "Secured (development)\n");
	}

	return sysfs_emit(buf, "%s\n", mlxbf_bootctl_lifecycle_states[lc_state]);
}

static ssize_t secure_boot_fuse_state_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	int burnt, valid, key, key_state, buf_len = 0, upper_key_used = 0;
	const char *status;

	key_state = mlxbf_bootctl_smc(MLXBF_BOOTCTL_GET_TBB_FUSE_STATUS,
				      MLXBF_BOOTCTL_FUSE_STATUS_KEYS);
	if (key_state < 0)
		return key_state;

	/*
	 * key_state contains the bits for 4 Key versions, loaded from eFuses
	 * after a hard reset. Lower 4 bits are a thermometer code indicating
	 * key programming has started for key n (0000 = none, 0001 = version 0,
	 * 0011 = version 1, 0111 = version 2, 1111 = version 3). Upper 4 bits
	 * are a thermometer code indicating key programming has completed for
	 * key n (same encodings as the start bits). This allows for detection
	 * of an interruption in the programming process which has left the key
	 * partially programmed (and thus invalid). The process is to burn the
	 * eFuse for the new key start bit, burn the key eFuses, then burn the
	 * eFuse for the new key complete bit.
	 *
	 * For example 0000_0000: no key valid, 0001_0001: key version 0 valid,
	 * 0011_0011: key 1 version valid, 0011_0111: key version 2 started
	 * programming but did not complete, etc. The most recent key for which
	 * both start and complete bit is set is loaded. On soft reset, this
	 * register is not modified.
	 */
	for (key = MLXBF_SB_KEY_NUM - 1; key >= 0; key--) {
		burnt = key_state & BIT(key);
		valid = key_state & BIT(key + MLXBF_SB_KEY_NUM);

		if (burnt && valid)
			upper_key_used = 1;

		if (upper_key_used) {
			if (burnt)
				status = valid ? "Used" : "Wasted";
			else
				status = valid ? "Invalid" : "Skipped";
		} else {
			if (burnt)
				status = valid ? "InUse" : "Incomplete";
			else
				status = valid ? "Invalid" : "Free";
		}
		buf_len += sysfs_emit_at(buf, buf_len, "%d:%s ", key, status);
	}
	buf_len += sysfs_emit_at(buf, buf_len, "\n");

	return buf_len;
}

static ssize_t fw_reset_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned long key;
	int err;

	err = kstrtoul(buf, 16, &key);
	if (err)
		return err;

	if (mlxbf_bootctl_smc(MLXBF_BOOTCTL_FW_RESET, key) < 0)
		return -EINVAL;

	return count;
}

/* Size(8-byte words) of the log buffer. */
#define RSH_SCRATCH_BUF_CTL_IDX_MASK	0x7f

/* 100ms timeout */
#define RSH_SCRATCH_BUF_POLL_TIMEOUT	100000

static int mlxbf_rsh_log_sem_lock(void)
{
	unsigned long reg;

	return readq_poll_timeout(mlxbf_rsh_semaphore, reg, !reg, 0,
				  RSH_SCRATCH_BUF_POLL_TIMEOUT);
}

static void mlxbf_rsh_log_sem_unlock(void)
{
	writeq(0, mlxbf_rsh_semaphore);
}

static ssize_t rsh_log_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int rc, idx, num, len, level = MLXBF_RSH_LOG_INFO;
	size_t size = count;
	u64 data;

	if (!size)
		return -EINVAL;

	if (!mlxbf_rsh_semaphore || !mlxbf_rsh_scratch_buf_ctl)
		return -EOPNOTSUPP;

	/* Ignore line break at the end. */
	if (buf[size - 1] == '\n')
		size--;

	/* Check the message prefix. */
	for (idx = 0; idx < ARRAY_SIZE(mlxbf_rsh_log_level); idx++) {
		len = strlen(mlxbf_rsh_log_level[idx]);
		if (len + 1 < size &&
		    !strncmp(buf, mlxbf_rsh_log_level[idx], len)) {
			buf += len;
			size -= len;
			level = idx;
			break;
		}
	}

	/* Ignore leading spaces. */
	while (size > 0 && buf[0] == ' ') {
		size--;
		buf++;
	}

	/* Take the semaphore. */
	rc = mlxbf_rsh_log_sem_lock();
	if (rc)
		return rc;

	/* Calculate how many words are available. */
	idx = readq(mlxbf_rsh_scratch_buf_ctl);
	num = min((int)DIV_ROUND_UP(size, sizeof(u64)),
		  RSH_SCRATCH_BUF_CTL_IDX_MASK - idx - 1);
	if (num <= 0)
		goto done;

	/* Write Header. */
	data = FIELD_PREP(MLXBF_RSH_LOG_TYPE_MASK, MLXBF_RSH_LOG_TYPE_MSG);
	data |= FIELD_PREP(MLXBF_RSH_LOG_LEN_MASK, num);
	data |= FIELD_PREP(MLXBF_RSH_LOG_LEVEL_MASK, level);
	writeq(data, mlxbf_rsh_scratch_buf_data);

	/* Write message. */
	for (idx = 0; idx < num && size > 0; idx++) {
		if (size < sizeof(u64)) {
			data = 0;
			memcpy(&data, buf, size);
			size = 0;
		} else {
			memcpy(&data, buf, sizeof(u64));
			size -= sizeof(u64);
			buf += sizeof(u64);
		}
		writeq(data, mlxbf_rsh_scratch_buf_data);
	}

done:
	/* Release the semaphore. */
	mlxbf_rsh_log_sem_unlock();

	/* Ignore the rest if no more space. */
	return count;
}

static ssize_t large_icm_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct arm_smccc_res res;

	mutex_lock(&icm_ops_lock);
	arm_smccc_smc(MLNX_HANDLE_GET_ICM_INFO, 0, 0, 0, 0,
		      0, 0, 0, &res);
	mutex_unlock(&icm_ops_lock);
	if (res.a0)
		return -EPERM;

	return sysfs_emit(buf, "0x%lx", res.a1);
}

static ssize_t large_icm_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct arm_smccc_res res;
	unsigned long icm_data;
	int err;

	err = kstrtoul(buf, MLXBF_LARGE_ICMC_MAX_STRING_SIZE, &icm_data);
	if (err)
		return err;

	if ((icm_data != 0 && icm_data < MLXBF_LARGE_ICMC_SIZE_MIN) ||
	    icm_data > MLXBF_LARGE_ICMC_SIZE_MAX || icm_data % MLXBF_LARGE_ICMC_GRANULARITY)
		return -EPERM;

	mutex_lock(&icm_ops_lock);
	arm_smccc_smc(MLNX_HANDLE_SET_ICM_INFO, icm_data, 0, 0, 0, 0, 0, 0, &res);
	mutex_unlock(&icm_ops_lock);

	return res.a0 ? -EPERM : count;
}

static ssize_t rtc_battery_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct arm_smccc_res res;

	mutex_lock(&rtc_ops_lock);
	arm_smccc_smc(MLNX_HANDLE_GET_RTC_LOW_BATT, 0, 0, 0, 0,
		      0, 0, 0, &res);
	mutex_unlock(&rtc_ops_lock);

	if (res.a0)
		return -EPERM;

	return sysfs_emit(buf, "0x%lx\n", res.a1);
}

static ssize_t os_up_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct arm_smccc_res res;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val != 1)
		return -EINVAL;

	mutex_lock(&os_up_lock);
	arm_smccc_smc(MLNX_HANDLE_OS_UP, 0, 0, 0, 0, 0, 0, 0, &res);
	mutex_unlock(&os_up_lock);

	return count;
}

static ssize_t oob_mac_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct arm_smccc_res res;
	u8 *mac_byte_ptr;

	mutex_lock(&mfg_ops_lock);
	arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO, MLNX_MFG_TYPE_OOB_MAC, 0, 0, 0,
		      0, 0, 0, &res);
	mutex_unlock(&mfg_ops_lock);
	if (res.a0)
		return -EPERM;

	mac_byte_ptr = (u8 *)&res.a1;

	return sysfs_format_mac(buf, mac_byte_ptr, ETH_ALEN);
}

static ssize_t oob_mac_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned int byte[MLNX_MFG_OOB_MAC_FORMAT_LEN] = { 0 };
	struct arm_smccc_res res;
	int byte_idx, len;
	u64 mac_addr = 0;
	u8 *mac_byte_ptr;

	if ((count - 1) != MLNX_MFG_OOB_MAC_FORMAT_LEN)
		return -EINVAL;

	len = sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
		     &byte[0], &byte[1], &byte[2],
		     &byte[3], &byte[4], &byte[5]);
	if (len != ETH_ALEN)
		return -EINVAL;

	mac_byte_ptr = (u8 *)&mac_addr;

	for (byte_idx = 0; byte_idx < ETH_ALEN; byte_idx++)
		mac_byte_ptr[byte_idx] = (u8)byte[byte_idx];

	mutex_lock(&mfg_ops_lock);
	arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO, MLNX_MFG_TYPE_OOB_MAC,
		      ETH_ALEN, mac_addr, 0, 0, 0, 0, &res);
	mutex_unlock(&mfg_ops_lock);

	return res.a0 ? -EPERM : count;
}

static ssize_t opn_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u64 opn_data[MLNX_MFG_VAL_QWORD_CNT(OPN) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(OPN); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_OPN_0 + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		opn_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)opn_data);
}

static ssize_t opn_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	u64 opn[MLNX_MFG_VAL_QWORD_CNT(OPN)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_OPN_VAL_LEN)
		return -EINVAL;

	memcpy(opn, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(OPN); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_OPN_0 + word,
			      sizeof(u64), opn[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t sku_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u64 sku_data[MLNX_MFG_VAL_QWORD_CNT(SKU) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(SKU); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_SKU_0 + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		sku_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)sku_data);
}

static ssize_t sku_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	u64 sku[MLNX_MFG_VAL_QWORD_CNT(SKU)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_SKU_VAL_LEN)
		return -EINVAL;

	memcpy(sku, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(SKU); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_SKU_0 + word,
			      sizeof(u64), sku[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t modl_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	u64 modl_data[MLNX_MFG_VAL_QWORD_CNT(MODL) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(MODL); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_MODL_0 + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		modl_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)modl_data);
}

static ssize_t modl_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	u64 modl[MLNX_MFG_VAL_QWORD_CNT(MODL)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_MODL_VAL_LEN)
		return -EINVAL;

	memcpy(modl, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(MODL); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_MODL_0 + word,
			      sizeof(u64), modl[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t sn_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	u64 sn_data[MLNX_MFG_VAL_QWORD_CNT(SN) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(SN); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_SN_0 + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		sn_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)sn_data);
}

static ssize_t sn_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	u64 sn[MLNX_MFG_VAL_QWORD_CNT(SN)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_SN_VAL_LEN)
		return -EINVAL;

	memcpy(sn, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(SN); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_SN_0 + word,
			      sizeof(u64), sn[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t uuid_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	u64 uuid_data[MLNX_MFG_VAL_QWORD_CNT(UUID) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(UUID); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_UUID_0 + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		uuid_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)uuid_data);
}

static ssize_t uuid_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	u64 uuid[MLNX_MFG_VAL_QWORD_CNT(UUID)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_UUID_VAL_LEN)
		return -EINVAL;

	memcpy(uuid, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(UUID); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_UUID_0 + word,
			      sizeof(u64), uuid[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t rev_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u64 rev_data[MLNX_MFG_VAL_QWORD_CNT(REV) + 1] = { 0 };
	struct arm_smccc_res res;
	int word;

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(REV); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_GET_MFG_INFO,
			      MLNX_MFG_TYPE_REV + word,
			      0, 0, 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
		rev_data[word] = res.a1;
	}
	mutex_unlock(&mfg_ops_lock);

	return sysfs_emit(buf, "%s", (char *)rev_data);
}

static ssize_t rev_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	u64 rev[MLNX_MFG_VAL_QWORD_CNT(REV)] = { 0 };
	struct arm_smccc_res res;
	int word;

	if (count > MLNX_MFG_REV_VAL_LEN)
		return -EINVAL;

	memcpy(rev, buf, count);

	mutex_lock(&mfg_ops_lock);
	for (word = 0; word < MLNX_MFG_VAL_QWORD_CNT(REV); word++) {
		arm_smccc_smc(MLXBF_BOOTCTL_SET_MFG_INFO,
			      MLNX_MFG_TYPE_REV + word,
			      sizeof(u64), rev[word], 0, 0, 0, 0, &res);
		if (res.a0) {
			mutex_unlock(&mfg_ops_lock);
			return -EPERM;
		}
	}
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static ssize_t mfg_lock_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct arm_smccc_res res;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val != 1)
		return -EINVAL;

	mutex_lock(&mfg_ops_lock);
	arm_smccc_smc(MLXBF_BOOTCTL_LOCK_MFG_INFO, 0, 0, 0, 0, 0, 0, 0, &res);
	mutex_unlock(&mfg_ops_lock);

	return count;
}

static DEVICE_ATTR_RW(post_reset_wdog);
static DEVICE_ATTR_RW(reset_action);
static DEVICE_ATTR_RW(second_reset_action);
static DEVICE_ATTR_RO(lifecycle_state);
static DEVICE_ATTR_RO(secure_boot_fuse_state);
static DEVICE_ATTR_WO(fw_reset);
static DEVICE_ATTR_WO(rsh_log);
static DEVICE_ATTR_RW(large_icm);
static DEVICE_ATTR_WO(os_up);
static DEVICE_ATTR_RW(oob_mac);
static DEVICE_ATTR_RW(opn);
static DEVICE_ATTR_RW(sku);
static DEVICE_ATTR_RW(modl);
static DEVICE_ATTR_RW(sn);
static DEVICE_ATTR_RW(uuid);
static DEVICE_ATTR_RW(rev);
static DEVICE_ATTR_WO(mfg_lock);
static DEVICE_ATTR_RO(rtc_battery);

static struct attribute *mlxbf_bootctl_attrs[] = {
	&dev_attr_post_reset_wdog.attr,
	&dev_attr_reset_action.attr,
	&dev_attr_second_reset_action.attr,
	&dev_attr_lifecycle_state.attr,
	&dev_attr_secure_boot_fuse_state.attr,
	&dev_attr_fw_reset.attr,
	&dev_attr_rsh_log.attr,
	&dev_attr_large_icm.attr,
	&dev_attr_os_up.attr,
	&dev_attr_oob_mac.attr,
	&dev_attr_opn.attr,
	&dev_attr_sku.attr,
	&dev_attr_modl.attr,
	&dev_attr_sn.attr,
	&dev_attr_uuid.attr,
	&dev_attr_rev.attr,
	&dev_attr_mfg_lock.attr,
	&dev_attr_rtc_battery.attr,
	NULL
};

ATTRIBUTE_GROUPS(mlxbf_bootctl);

static const struct acpi_device_id mlxbf_bootctl_acpi_ids[] = {
	{"MLNXBF04", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, mlxbf_bootctl_acpi_ids);

static ssize_t mlxbf_bootctl_bootfifo_read(struct file *filp,
					   struct kobject *kobj,
					   const struct bin_attribute *bin_attr,
					   char *buf, loff_t pos,
					   size_t count)
{
	unsigned long timeout = msecs_to_jiffies(500);
	unsigned long expire = jiffies + timeout;
	u64 data, cnt = 0;
	char *p = buf;

	while (count >= sizeof(data)) {
		/* Give up reading if no more data within 500ms. */
		if (!cnt) {
			cnt = readq(mlxbf_rsh_boot_cnt);
			if (!cnt) {
				if (time_after(jiffies, expire))
					break;
				usleep_range(10, 50);
				continue;
			}
		}

		data = readq(mlxbf_rsh_boot_data);
		memcpy(p, &data, sizeof(data));
		count -= sizeof(data);
		p += sizeof(data);
		cnt--;
		expire = jiffies + timeout;
	}

	return p - buf;
}

static const struct bin_attribute mlxbf_bootctl_bootfifo_sysfs_attr = {
	.attr = { .name = "bootfifo", .mode = 0400 },
	.read = mlxbf_bootctl_bootfifo_read,
};

static bool mlxbf_bootctl_guid_match(const guid_t *guid,
				     const struct arm_smccc_res *res)
{
	guid_t id = GUID_INIT(res->a0, res->a1, res->a1 >> 16,
			      res->a2, res->a2 >> 8, res->a2 >> 16,
			      res->a2 >> 24, res->a3, res->a3 >> 8,
			      res->a3 >> 16, res->a3 >> 24);

	return guid_equal(guid, &id);
}

static int mlxbf_bootctl_probe(struct platform_device *pdev)
{
	struct arm_smccc_res res = { 0 };
	void __iomem *reg;
	guid_t guid;
	int ret;

	/* Map the resource of the bootfifo data register. */
	mlxbf_rsh_boot_data = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mlxbf_rsh_boot_data))
		return PTR_ERR(mlxbf_rsh_boot_data);

	/* Map the resource of the bootfifo counter register. */
	mlxbf_rsh_boot_cnt = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mlxbf_rsh_boot_cnt))
		return PTR_ERR(mlxbf_rsh_boot_cnt);

	/* Map the resource of the rshim semaphore register. */
	mlxbf_rsh_semaphore = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(mlxbf_rsh_semaphore))
		return PTR_ERR(mlxbf_rsh_semaphore);

	/* Map the resource of the scratch buffer (log) registers. */
	reg = devm_platform_ioremap_resource(pdev, 3);
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	mlxbf_rsh_scratch_buf_ctl = reg + MLXBF_RSH_SCRATCH_BUF_CTL_OFF;
	mlxbf_rsh_scratch_buf_data = reg + MLXBF_RSH_SCRATCH_BUF_DATA_OFF;

	/* Ensure we have the UUID we expect for this service. */
	arm_smccc_smc(MLXBF_BOOTCTL_SIP_SVC_UID, 0, 0, 0, 0, 0, 0, 0, &res);
	guid_parse(mlxbf_bootctl_svc_uuid_str, &guid);
	if (!mlxbf_bootctl_guid_match(&guid, &res))
		return -ENODEV;

	/*
	 * When watchdog is used, it sets boot mode to MLXBF_BOOTCTL_SWAP_EMMC
	 * in case of boot failures. However it doesn't clear the state if there
	 * is no failure. Restore the default boot mode here to avoid any
	 * unnecessary boot partition swapping.
	 */
	ret = mlxbf_bootctl_smc(MLXBF_BOOTCTL_SET_RESET_ACTION,
				MLXBF_BOOTCTL_EMMC);
	if (ret < 0)
		dev_warn(&pdev->dev, "Unable to reset the EMMC boot mode\n");

	ret = sysfs_create_bin_file(&pdev->dev.kobj,
				    &mlxbf_bootctl_bootfifo_sysfs_attr);
	if (ret)
		pr_err("Unable to create bootfifo sysfs file, error %d\n", ret);

	return ret;
}

static void mlxbf_bootctl_remove(struct platform_device *pdev)
{
	sysfs_remove_bin_file(&pdev->dev.kobj,
			      &mlxbf_bootctl_bootfifo_sysfs_attr);
}

static struct platform_driver mlxbf_bootctl_driver = {
	.probe = mlxbf_bootctl_probe,
	.remove = mlxbf_bootctl_remove,
	.driver = {
		.name = "mlxbf-bootctl",
		.dev_groups = mlxbf_bootctl_groups,
		.acpi_match_table = mlxbf_bootctl_acpi_ids,
	}
};

module_platform_driver(mlxbf_bootctl_driver);

MODULE_DESCRIPTION("Mellanox boot control driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mellanox Technologies");
