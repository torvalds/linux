// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/sched/debug.h>
#include <linux/suspend.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <net/cnss.h>
#include "cnss_common.h"
#include <net/cfg80211.h>

#define AR6320_REV1_VERSION             0x5000000
#define AR6320_REV1_1_VERSION           0x5000001
#define AR6320_REV1_3_VERSION           0x5000003
#define AR6320_REV2_1_VERSION           0x5010000
#define AR6320_REV3_VERSION             0x5020000
#define AR6320_REV3_2_VERSION           0x5030000
#define AR900B_DEV_VERSION              0x1000000
#define QCA9377_REV1_1_VERSION          0x5020001

static struct cnss_fw_files FW_FILES_QCA6174_FW_1_1 = {
	"qwlan11.bin", "bdwlan11.bin", "otp11.bin", "utf11.bin",
	"utfbd11.bin", "epping11.bin", "evicted11.bin"};
static struct cnss_fw_files FW_FILES_QCA6174_FW_2_0 = {
	"qwlan20.bin", "bdwlan20.bin", "otp20.bin", "utf20.bin",
	"utfbd20.bin", "epping20.bin", "evicted20.bin"};
static struct cnss_fw_files FW_FILES_QCA6174_FW_1_3 = {
	"qwlan13.bin", "bdwlan13.bin", "otp13.bin", "utf13.bin",
	"utfbd13.bin", "epping13.bin", "evicted13.bin"};
static struct cnss_fw_files FW_FILES_QCA6174_FW_3_0 = {
	"qwlan30.bin", "bdwlan30.bin", "otp30.bin", "utf30.bin",
	"utfbd30.bin", "epping30.bin", "evicted30.bin"};
static struct cnss_fw_files FW_FILES_DEFAULT = {
	"qwlan.bin", "bdwlan.bin", "otp.bin", "utf.bin",
	"utfbd.bin", "epping.bin", "evicted.bin"};

enum cnss_dev_bus_type {
	CNSS_BUS_NONE = -1,
	CNSS_BUS_PCI,
	CNSS_BUS_SDIO
};

static DEFINE_MUTEX(unsafe_channel_list_lock);
static DEFINE_MUTEX(dfs_nol_info_lock);

static struct cnss_unsafe_channel_list {
	u16 unsafe_ch_count;
	u16 unsafe_ch_list[CNSS_MAX_CH_NUM];
} unsafe_channel_list;

static struct cnss_dfs_nol_info {
	void *dfs_nol_info;
	u16 dfs_nol_info_len;
} dfs_nol_info;

static enum cnss_cc_src cnss_cc_source = CNSS_SOURCE_CORE;

int cnss_set_wlan_unsafe_channel(u16 *unsafe_ch_list, u16 ch_count)
{
	mutex_lock(&unsafe_channel_list_lock);
	if (!unsafe_ch_list || ch_count > CNSS_MAX_CH_NUM) {
		mutex_unlock(&unsafe_channel_list_lock);
		return -EINVAL;
	}

	unsafe_channel_list.unsafe_ch_count = ch_count;

	if (ch_count != 0) {
		memcpy((char *)unsafe_channel_list.unsafe_ch_list,
		       (char *)unsafe_ch_list, ch_count * sizeof(u16));
	}
	mutex_unlock(&unsafe_channel_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(cnss_set_wlan_unsafe_channel);

int cnss_get_wlan_unsafe_channel(u16 *unsafe_ch_list,
				 u16 *ch_count, u16 buf_len)
{
	mutex_lock(&unsafe_channel_list_lock);
	if (!unsafe_ch_list || !ch_count) {
		mutex_unlock(&unsafe_channel_list_lock);
		return -EINVAL;
	}

	if (buf_len < (unsafe_channel_list.unsafe_ch_count * sizeof(u16))) {
		mutex_unlock(&unsafe_channel_list_lock);
		return -ENOMEM;
	}

	*ch_count = unsafe_channel_list.unsafe_ch_count;
	memcpy((char *)unsafe_ch_list,
	       (char *)unsafe_channel_list.unsafe_ch_list,
	       unsafe_channel_list.unsafe_ch_count * sizeof(u16));
	mutex_unlock(&unsafe_channel_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(cnss_get_wlan_unsafe_channel);

int cnss_wlan_set_dfs_nol(const void *info, u16 info_len)
{
	void *temp;
	struct cnss_dfs_nol_info *dfs_info;

	mutex_lock(&dfs_nol_info_lock);
	if (!info || !info_len) {
		mutex_unlock(&dfs_nol_info_lock);
		return -EINVAL;
	}

	temp = kmemdup(info, info_len, GFP_KERNEL);
	if (!temp) {
		mutex_unlock(&dfs_nol_info_lock);
		return -ENOMEM;
	}

	dfs_info = &dfs_nol_info;
	kfree(dfs_info->dfs_nol_info);

	dfs_info->dfs_nol_info = temp;
	dfs_info->dfs_nol_info_len = info_len;
	mutex_unlock(&dfs_nol_info_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(cnss_wlan_set_dfs_nol);

int cnss_wlan_get_dfs_nol(void *info, u16 info_len)
{
	int len;
	struct cnss_dfs_nol_info *dfs_info;

	mutex_lock(&dfs_nol_info_lock);
	if (!info || !info_len) {
		mutex_unlock(&dfs_nol_info_lock);
		return -EINVAL;
	}

	dfs_info = &dfs_nol_info;

	if (!dfs_info->dfs_nol_info || dfs_info->dfs_nol_info_len == 0) {
		mutex_unlock(&dfs_nol_info_lock);
		return -ENOENT;
	}

	len = min(info_len, dfs_info->dfs_nol_info_len);

	memcpy(info, dfs_info->dfs_nol_info, len);
	mutex_unlock(&dfs_nol_info_lock);

	return len;
}
EXPORT_SYMBOL_GPL(cnss_wlan_get_dfs_nol);

void cnss_init_work(struct work_struct *work, work_func_t func)
{
	INIT_WORK(work, func);
}
EXPORT_SYMBOL_GPL(cnss_init_work);

void cnss_flush_work(void *work)
{
	struct work_struct *cnss_work = work;

	cancel_work_sync(cnss_work);
}
EXPORT_SYMBOL_GPL(cnss_flush_work);

void cnss_flush_delayed_work(void *dwork)
{
	struct delayed_work *cnss_dwork = dwork;

	cancel_delayed_work_sync(cnss_dwork);
}
EXPORT_SYMBOL_GPL(cnss_flush_delayed_work);

void cnss_pm_wake_lock_init(struct wakeup_source **ws, const char *name)
{
	*ws = wakeup_source_register(NULL, name);
}
EXPORT_SYMBOL_GPL(cnss_pm_wake_lock_init);

void cnss_pm_wake_lock(struct wakeup_source *ws)
{
	__pm_stay_awake(ws);
}
EXPORT_SYMBOL_GPL(cnss_pm_wake_lock);

void cnss_pm_wake_lock_timeout(struct wakeup_source *ws, ulong msec)
{
	__pm_wakeup_event(ws, msec);
}
EXPORT_SYMBOL_GPL(cnss_pm_wake_lock_timeout);

void cnss_pm_wake_lock_release(struct wakeup_source *ws)
{
	__pm_relax(ws);
}
EXPORT_SYMBOL_GPL(cnss_pm_wake_lock_release);

void cnss_pm_wake_lock_destroy(struct wakeup_source *ws)
{
	wakeup_source_unregister(ws);
}
EXPORT_SYMBOL_GPL(cnss_pm_wake_lock_destroy);

void cnss_get_monotonic_boottime(struct timespec64 *ts)
{
	ktime_get_boottime_ts64(ts);
}
EXPORT_SYMBOL_GPL(cnss_get_monotonic_boottime);

void cnss_get_boottime(struct timespec64 *ts)
{
	ktime_get_ts64(ts);
}
EXPORT_SYMBOL_GPL(cnss_get_boottime);

void cnss_init_delayed_work(struct delayed_work *work, work_func_t func)
{
	INIT_DELAYED_WORK(work, func);
}
EXPORT_SYMBOL_GPL(cnss_init_delayed_work);

int cnss_vendor_cmd_reply(struct sk_buff *skb)
{
	return cfg80211_vendor_cmd_reply(skb);
}
EXPORT_SYMBOL_GPL(cnss_vendor_cmd_reply);

int cnss_set_cpus_allowed_ptr(struct task_struct *task, ulong cpu)
{
	return set_cpus_allowed_ptr(task, cpumask_of(cpu));
}
EXPORT_SYMBOL_GPL(cnss_set_cpus_allowed_ptr);

/* wlan prop driver cannot invoke show_stack
 * function directly, so to invoke this function it
 * call wcnss_dump_stack function
 */
void cnss_dump_stack(struct task_struct *task)
{
	show_stack(task, NULL, NULL);
}
EXPORT_SYMBOL_GPL(cnss_dump_stack);

struct cnss_dev_platform_ops *cnss_get_platform_ops(struct device *dev)
{
	if (!dev)
		return NULL;
	else
		return dev->platform_data;
}

int cnss_common_request_bus_bandwidth(struct device *dev, int bandwidth)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->request_bus_bandwidth)
		return pf_ops->request_bus_bandwidth(bandwidth);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_common_request_bus_bandwidth);

void *cnss_common_get_virt_ramdump_mem(struct device *dev, unsigned long *size)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->get_virt_ramdump_mem)
		return pf_ops->get_virt_ramdump_mem(size);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(cnss_common_get_virt_ramdump_mem);

void cnss_common_device_self_recovery(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->device_self_recovery)
		pf_ops->device_self_recovery();
}
EXPORT_SYMBOL_GPL(cnss_common_device_self_recovery);

void cnss_common_schedule_recovery_work(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->schedule_recovery_work)
		pf_ops->schedule_recovery_work();
}
EXPORT_SYMBOL_GPL(cnss_common_schedule_recovery_work);

void cnss_common_device_crashed(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->device_crashed)
		pf_ops->device_crashed();
}
EXPORT_SYMBOL_GPL(cnss_common_device_crashed);

u8 *cnss_common_get_wlan_mac_address(struct device *dev, u32 *num)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->get_wlan_mac_address)
		return pf_ops->get_wlan_mac_address(num);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(cnss_common_get_wlan_mac_address);

int cnss_common_set_wlan_mac_address(struct device *dev, const u8 *in, u32 len)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->set_wlan_mac_address)
		return pf_ops->set_wlan_mac_address(in, len);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_common_set_wlan_mac_address);

int cnss_power_up(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->power_up)
		return pf_ops->power_up(dev);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_power_up);

int cnss_power_down(struct device *dev)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->power_down)
		return pf_ops->power_down(dev);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_power_down);

void cnss_get_qca9377_fw_files(struct cnss_fw_files *pfw_files,
			       u32 size, u32 tufello_dual_fw)
{
	if (tufello_dual_fw)
		memcpy(pfw_files, &FW_FILES_DEFAULT, sizeof(*pfw_files));
	else
		memcpy(pfw_files, &FW_FILES_QCA6174_FW_3_0, sizeof(*pfw_files));
}
EXPORT_SYMBOL_GPL(cnss_get_qca9377_fw_files);

int cnss_get_fw_files_for_target(struct cnss_fw_files *pfw_files,
				 u32 target_type, u32 target_version)
{
	if (!pfw_files)
		return -ENODEV;

	switch (target_version) {
	case AR6320_REV1_VERSION:
	case AR6320_REV1_1_VERSION:
		memcpy(pfw_files, &FW_FILES_QCA6174_FW_1_1, sizeof(*pfw_files));
		break;
	case AR6320_REV1_3_VERSION:
		memcpy(pfw_files, &FW_FILES_QCA6174_FW_1_3, sizeof(*pfw_files));
		break;
	case AR6320_REV2_1_VERSION:
		memcpy(pfw_files, &FW_FILES_QCA6174_FW_2_0, sizeof(*pfw_files));
		break;
	case AR6320_REV3_VERSION:
	case AR6320_REV3_2_VERSION:
		memcpy(pfw_files, &FW_FILES_QCA6174_FW_3_0, sizeof(*pfw_files));
		break;
	default:
		memcpy(pfw_files, &FW_FILES_DEFAULT, sizeof(*pfw_files));
		pr_err("%s default version 0x%X 0x%X\n", __func__,
		       target_type, target_version);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(cnss_get_fw_files_for_target);

void cnss_set_cc_source(enum cnss_cc_src cc_source)
{
	cnss_cc_source = cc_source;
}
EXPORT_SYMBOL_GPL(cnss_set_cc_source);

enum cnss_cc_src cnss_get_cc_source(void)
{
	return cnss_cc_source;
}
EXPORT_SYMBOL_GPL(cnss_get_cc_source);

const char *cnss_wlan_get_evicted_data_file(void)
{
	return FW_FILES_QCA6174_FW_3_0.evicted_data;
}

int cnss_common_register_tsf_captured_handler(struct device *dev,
					      irq_handler_t handler, void *ctx)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->register_tsf_captured_handler)
		return pf_ops->register_tsf_captured_handler(handler, ctx);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_common_register_tsf_captured_handler);

int cnss_common_unregister_tsf_captured_handler(struct device *dev,
						void *ctx)
{
	struct cnss_dev_platform_ops *pf_ops = cnss_get_platform_ops(dev);

	if (pf_ops && pf_ops->unregister_tsf_captured_handler)
		return pf_ops->unregister_tsf_captured_handler(ctx);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(cnss_common_unregister_tsf_captured_handler);
