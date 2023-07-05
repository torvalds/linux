// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010,2015,2019,2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt)     "qcom-scm: %s: " fmt, __func__

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/qcom_scm.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/reboot.h>
#include <linux/clk.h>
#include <linux/reset-controller.h>
#include <linux/arm-smccc.h>
#include <soc/qcom/qseecom_scm.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>

#include "qcom_scm.h"
#include "qtee_shmbridge_internal.h"

static bool download_mode = IS_ENABLED(CONFIG_QCOM_SCM_DOWNLOAD_MODE_DEFAULT);
module_param(download_mode, bool, 0);

static unsigned int pas_shutdown_retry_interval = 100;
module_param(pas_shutdown_retry_interval, uint, 0644);
static unsigned int pas_shutdown_retry_max = 50;
module_param(pas_shutdown_retry_max, uint, 0644);

#define SCM_HAS_CORE_CLK	BIT(0)
#define SCM_HAS_IFACE_CLK	BIT(1)
#define SCM_HAS_BUS_CLK		BIT(2)

struct qcom_scm_waitq {
	struct idr idr;
	spinlock_t idr_lock;
	struct work_struct scm_irq_work;
	u64 call_ctx_cnt;
	u64 irq;
	enum qcom_scm_wq_feature wq_feature;
};

struct qcom_scm {
	struct device *dev;
	struct clk *core_clk;
	struct clk *iface_clk;
	struct clk *bus_clk;
	struct icc_path *path;
	struct reset_controller_dev reset;
	struct notifier_block restart_nb;
	struct qcom_scm_waitq waitq;

	/* control access to the interconnect path */
	struct mutex scm_bw_lock;
	int scm_vote_count;

	u64 dload_mode_addr;
};

#define QCOM_SCM_FLAG_COLDBOOT_CPU0	0x00
#define QCOM_SCM_FLAG_COLDBOOT_CPU1	0x01
#define QCOM_SCM_FLAG_COLDBOOT_CPU2	0x08
#define QCOM_SCM_FLAG_COLDBOOT_CPU3	0x20

#define QCOM_SCM_FLAG_WARMBOOT_CPU0	0x04
#define QCOM_SCM_FLAG_WARMBOOT_CPU1	0x02
#define QCOM_SCM_FLAG_WARMBOOT_CPU2	0x10
#define QCOM_SCM_FLAG_WARMBOOT_CPU3	0x40

#define QCOM_SMC_WAITQ_FLAG_WAKE_ONE	BIT(0)
#define QCOM_SMC_WAITQ_FLAG_WAKE_ALL	BIT(1)

struct qcom_scm_wb_entry {
	int flag;
	void *entry;
};

static struct qcom_scm_wb_entry qcom_scm_wb[] = {
	{ .flag = QCOM_SCM_FLAG_WARMBOOT_CPU0 },
	{ .flag = QCOM_SCM_FLAG_WARMBOOT_CPU1 },
	{ .flag = QCOM_SCM_FLAG_WARMBOOT_CPU2 },
	{ .flag = QCOM_SCM_FLAG_WARMBOOT_CPU3 },
};

static const char * const qcom_scm_convention_names[] = {
	[SMC_CONVENTION_UNKNOWN] = "unknown",
	[SMC_CONVENTION_ARM_32] = "smc arm 32",
	[SMC_CONVENTION_ARM_64] = "smc arm 64",
	[SMC_CONVENTION_LEGACY] = "smc legacy",
};

static struct qcom_scm *__scm;

static int qcom_scm_clk_enable(void)
{
	int ret;

	ret = clk_prepare_enable(__scm->core_clk);
	if (ret)
		goto bail;

	ret = clk_prepare_enable(__scm->iface_clk);
	if (ret)
		goto disable_core;

	ret = clk_prepare_enable(__scm->bus_clk);
	if (ret)
		goto disable_iface;

	return 0;

disable_iface:
	clk_disable_unprepare(__scm->iface_clk);
disable_core:
	clk_disable_unprepare(__scm->core_clk);
bail:
	return ret;
}

static void qcom_scm_clk_disable(void)
{
	clk_disable_unprepare(__scm->core_clk);
	clk_disable_unprepare(__scm->iface_clk);
	clk_disable_unprepare(__scm->bus_clk);
}

static int qcom_scm_bw_enable(void)
{
	int ret = 0;

	if (!__scm->path)
		return 0;

	if (IS_ERR(__scm->path))
		return -EINVAL;

	mutex_lock(&__scm->scm_bw_lock);
	if (!__scm->scm_vote_count) {
		ret = icc_set_bw(__scm->path, 0, UINT_MAX);
		if (ret < 0) {
			dev_err(__scm->dev, "failed to set bandwidth request\n");
			goto err_bw;
		}
	}
	__scm->scm_vote_count++;
err_bw:
	mutex_unlock(&__scm->scm_bw_lock);

	return ret;
}

static void qcom_scm_bw_disable(void)
{
	if (IS_ERR_OR_NULL(__scm->path))
		return;

	mutex_lock(&__scm->scm_bw_lock);
	if (__scm->scm_vote_count-- == 1)
		icc_set_bw(__scm->path, 0, 0);
	mutex_unlock(&__scm->scm_bw_lock);
}

enum qcom_scm_convention qcom_scm_convention = SMC_CONVENTION_UNKNOWN;
static DEFINE_SPINLOCK(scm_query_lock);

static enum qcom_scm_convention __get_convention(void)
{
	unsigned long flags;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.args[0] = SCM_SMC_FNID(QCOM_SCM_SVC_INFO,
					   QCOM_SCM_INFO_IS_CALL_AVAIL) |
			   (ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT),
		.arginfo = QCOM_SCM_ARGS(1),
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	enum qcom_scm_convention probed_convention;
	int ret;
	bool forced = false;

	if (likely(qcom_scm_convention != SMC_CONVENTION_UNKNOWN))
		return qcom_scm_convention;

	/*
	 * Device isn't required as there is only one argument - no device
	 * needed to dma_map_single to secure world
	 */
	probed_convention = SMC_CONVENTION_ARM_64;
	ret = __scm_smc_call(NULL, &desc, probed_convention, &res, true);
	if (!ret && res.result[0] == 1)
		goto found;

	/*
	 * Some SC7180 firmwares didn't implement the
	 * QCOM_SCM_INFO_IS_CALL_AVAIL call, so we fallback to forcing ARM_64
	 * calling conventions on these firmwares. Luckily we don't make any
	 * early calls into the firmware on these SoCs so the device pointer
	 * will be valid here to check if the compatible matches.
	 */
	if (of_device_is_compatible(__scm ? __scm->dev->of_node : NULL, "qcom,scm-sc7180")) {
		forced = true;
		goto found;
	}

	probed_convention = SMC_CONVENTION_ARM_32;
	ret = __scm_smc_call(NULL, &desc, probed_convention, &res, true);
	if (!ret && res.result[0] == 1)
		goto found;

	probed_convention = SMC_CONVENTION_LEGACY;
found:
	spin_lock_irqsave(&scm_query_lock, flags);
	if (probed_convention != qcom_scm_convention) {
		qcom_scm_convention = probed_convention;
		pr_info("qcom_scm: convention: %s%s\n",
			qcom_scm_convention_names[qcom_scm_convention],
			forced ? " (forced)" : "");
	}
	spin_unlock_irqrestore(&scm_query_lock, flags);

	return qcom_scm_convention;
}

/**
 * qcom_scm_call() - Invoke a syscall in the secure world
 * @dev:	device
 * @svc_id:	service identifier
 * @cmd_id:	command identifier
 * @desc:	Descriptor structure containing arguments and return values
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 * This should *only* be called in pre-emptible context.
 */
static int qcom_scm_call(struct device *dev, const struct qcom_scm_desc *desc,
			 struct qcom_scm_res *res)
{
	might_sleep();
	switch (__get_convention()) {
	case SMC_CONVENTION_ARM_32:
	case SMC_CONVENTION_ARM_64:
		return scm_smc_call(dev, desc, res, QCOM_SCM_CALL_NORMAL);
	case SMC_CONVENTION_LEGACY:
		return scm_legacy_call(dev, desc, res);
	default:
		pr_err("Unknown current SCM calling convention.\n");
		return -EINVAL;
	}
}

/**
 * qcom_scm_call_atomic() - atomic variation of qcom_scm_call()
 * @dev:	device
 * @svc_id:	service identifier
 * @cmd_id:	command identifier
 * @desc:	Descriptor structure containing arguments and return values
 * @res:	Structure containing results from SMC/HVC call
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 * This can be called in atomic context.
 */
static int qcom_scm_call_atomic(struct device *dev,
				const struct qcom_scm_desc *desc,
				struct qcom_scm_res *res)
{
	switch (__get_convention()) {
	case SMC_CONVENTION_ARM_32:
	case SMC_CONVENTION_ARM_64:
		return scm_smc_call(dev, desc, res, QCOM_SCM_CALL_ATOMIC);
	case SMC_CONVENTION_LEGACY:
		return scm_legacy_call_atomic(dev, desc, res);
	default:
		pr_err("Unknown current SCM calling convention.\n");
		return -EINVAL;
	}
}

/**
 * qcom_scm_call_noretry() - noretry variation of qcom_scm_call()
 * @dev:	device
 * @svc_id:	service identifier
 * @cmd_id:	command identifier
 * @desc:	Descriptor structure containing arguments and return values
 * @res:	Structure containing results from SMC/HVC call
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 */
static int qcom_scm_call_noretry(struct device *dev,
				const struct qcom_scm_desc *desc,
				struct qcom_scm_res *res)
{
	switch (__get_convention()) {
	case SMC_CONVENTION_ARM_32:
	case SMC_CONVENTION_ARM_64:
		return scm_smc_call(dev, desc, res, QCOM_SCM_CALL_NORETRY);
	case SMC_CONVENTION_LEGACY:
		BUG_ON(1); /* No current implementation */
	default:
		pr_err("Unknown current SCM calling convention.\n");
		return -EINVAL;
	}
}

static bool __qcom_scm_is_call_available(struct device *dev, u32 svc_id,
					 u32 cmd_id)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	desc.arginfo = QCOM_SCM_ARGS(1);
	switch (__get_convention()) {
	case SMC_CONVENTION_ARM_32:
	case SMC_CONVENTION_ARM_64:
		desc.args[0] = SCM_SMC_FNID(svc_id, cmd_id) |
				(ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT);
		break;
	case SMC_CONVENTION_LEGACY:
		desc.args[0] = SCM_LEGACY_FNID(svc_id, cmd_id);
		break;
	default:
		pr_err("Unknown SMC convention being used\n");
		return false;
	}

	ret = qcom_scm_call(dev, &desc, &res);

	return ret ? false : !!res.result[0];
}

/**
 * qcom_scm_set_warm_boot_addr() - Set the warm boot address for cpus
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the Linux entry point for the SCM to transfer control to when coming
 * out of a power down. CPU power down may be executed on cpuidle or hotplug.
 */
int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus)
{
	int ret;
	int flags = 0;
	int cpu;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SET_ADDR,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	/*
	 * Reassign only if we are switching from hotplug entry point
	 * to cpuidle entry point or vice versa.
	 */
	for_each_cpu(cpu, cpus) {
		if (entry == qcom_scm_wb[cpu].entry)
			continue;
		flags |= qcom_scm_wb[cpu].flag;
	}

	/* No change in entry function */
	if (!flags)
		return 0;

	desc.args[0] = flags;
	desc.args[1] = virt_to_phys(entry);

	ret = qcom_scm_call(__scm->dev, &desc, NULL);
	if (!ret) {
		for_each_cpu(cpu, cpus)
			qcom_scm_wb[cpu].entry = entry;
	}

	return ret;
}
EXPORT_SYMBOL(qcom_scm_set_warm_boot_addr);

/**
 * qcom_scm_set_cold_boot_addr() - Set the cold boot address for cpus
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the cold boot address of the cpus. Any cpu outside the supported
 * range would be removed from the cpu present mask.
 */
int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus)
{
	int flags = 0;
	int cpu;
	int scm_cb_flags[] = {
		QCOM_SCM_FLAG_COLDBOOT_CPU0,
		QCOM_SCM_FLAG_COLDBOOT_CPU1,
		QCOM_SCM_FLAG_COLDBOOT_CPU2,
		QCOM_SCM_FLAG_COLDBOOT_CPU3,
	};
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SET_ADDR,
		.arginfo = QCOM_SCM_ARGS(2),
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	if (!cpus || cpumask_empty(cpus))
		return -EINVAL;

	for_each_cpu(cpu, cpus) {
		if (cpu < ARRAY_SIZE(scm_cb_flags))
			flags |= scm_cb_flags[cpu];
		else
			set_cpu_present(cpu, false);
	}

	desc.args[0] = flags;
	desc.args[1] = virt_to_phys(entry);

	return qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_set_cold_boot_addr);

/**
 * qcom_scm_cpu_power_down() - Power down the cpu
 * @flags - Flags to flush cache
 *
 * This is an end point to power down cpu. If there was a pending interrupt,
 * the control would return from this function, otherwise, the cpu jumps to the
 * warm boot entry point set for this cpu upon reset.
 */
void qcom_scm_cpu_power_down(u32 flags)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_TERMINATE_PC,
		.args[0] = flags & QCOM_SCM_FLUSH_FLAG_MASK,
		.arginfo = QCOM_SCM_ARGS(1),
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_cpu_power_down);

/**
 * qcm_scm_sec_wdog_deactivate() - Deactivate secure watchdog
 */
int qcom_scm_sec_wdog_deactivate(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SEC_WDOG_DIS,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 1,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_sec_wdog_deactivate);

/**
 * qcom_scm_sec_wdog_trigger() - Trigger secure watchdog
 */
int qcom_scm_sec_wdog_trigger(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SEC_WDOG_TRIGGER,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_sec_wdog_trigger);

/**
 * qcom_scm_disable_sdi() - Disable SDI
 */
void qcom_scm_disable_sdi(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_WDOG_DEBUG_PART,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 1,
		.args[1] = 0,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	ret = qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
	if (ret)
		pr_err("Failed to disable secure wdog debug: %d\n", ret);
}
EXPORT_SYMBOL(qcom_scm_disable_sdi);

int qcom_scm_set_remote_state(u32 state, u32 id)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SET_REMOTE_STATE,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = state,
		.args[1] = id,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_set_remote_state);

int qcom_scm_spin_cpu(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SPIN_CPU,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_spin_cpu);

static int __qcom_scm_set_dload_mode(struct device *dev, enum qcom_download_mode mode)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SET_DLOAD_MODE,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = mode,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	desc.args[1] = 0;

	return qcom_scm_call_atomic(__scm->dev, &desc, NULL);
}

void qcom_scm_set_download_mode(enum qcom_download_mode mode, phys_addr_t tcsr_boot_misc)
{
	bool avail;
	int ret = 0;
	struct device *dev = __scm ? __scm->dev : NULL;

	avail = __qcom_scm_is_call_available(dev,
					     QCOM_SCM_SVC_BOOT,
					     QCOM_SCM_BOOT_SET_DLOAD_MODE);
	if (avail) {
		ret = __qcom_scm_set_dload_mode(dev, mode);
	} else if (tcsr_boot_misc || (__scm && __scm->dload_mode_addr)) {
		ret = qcom_scm_io_writel(tcsr_boot_misc ? : __scm->dload_mode_addr, mode);
	} else {
		dev_err(dev,
			"No available mechanism for setting download mode\n");
	}

	if (ret)
		dev_err(dev, "failed to set download mode: %d\n", ret);
}
EXPORT_SYMBOL(qcom_scm_set_download_mode);

int qcom_scm_get_download_mode(unsigned int *mode, phys_addr_t tcsr_boot_misc)
{
	int ret = -EINVAL;
	struct device *dev = __scm ? __scm->dev : NULL;

	if (tcsr_boot_misc || (__scm && __scm->dload_mode_addr)) {
		ret = qcom_scm_io_readl(tcsr_boot_misc ? : __scm->dload_mode_addr, mode);
	} else {
		dev_err(dev,
			"No available mechanism for getting download mode\n");
	}

	if (ret)
		dev_err(dev, "failed to get download mode: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(qcom_scm_get_download_mode);

int qcom_scm_config_cpu_errata(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_CONFIG_CPU_ERRATA,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = 0xffffffff,
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_config_cpu_errata);

void qcom_scm_phy_update_scm_level_shifter(u32 val)
{
	struct device *dev = __scm ? __scm->dev : NULL;

	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_QUSB2PHY_LVL_SHIFTER_CMD_ID,
		.owner = ARM_SMCCC_OWNER_SIP
	};

	desc.args[0] = val;
	desc.args[1] = 0;
	desc.arginfo = QCOM_SCM_ARGS(2);

	ret = qcom_scm_call(dev, &desc, NULL);
	if (ret)
		pr_err("Failed to update scm level shifter=0x%x\n", ret);

}
EXPORT_SYMBOL(qcom_scm_phy_update_scm_level_shifter);


/**
 * qcom_scm_pas_init_image() - Initialize peripheral authentication service
 *			       state machine for a given peripheral, using the
 *			       metadata
 * @peripheral: peripheral id
 * @metadata:	pointer to memory containing ELF header, program header table
 *		and optional blob of data used for authenticating the metadata
 *		and the rest of the firmware
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_init_image(u32 peripheral, dma_addr_t metadata)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_INIT_IMAGE,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_RW),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		return ret;

	desc.args[1] = metadata;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	qcom_scm_bw_disable();
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_pas_init_image);

/**
 * qcom_scm_pas_mem_setup() - Prepare the memory related to a given peripheral
 *			      for firmware loading
 * @peripheral:	peripheral id
 * @addr:	start address of memory area to prepare
 * @size:	size of the memory area to prepare
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr, phys_addr_t size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_MEM_SETUP,
		.arginfo = QCOM_SCM_ARGS(3),
		.args[0] = peripheral,
		.args[1] = addr,
		.args[2] = size,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		return ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	qcom_scm_bw_disable();
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_pas_mem_setup);

/**
 * qcom_scm_pas_auth_and_reset() - Authenticate the given peripheral firmware
 *				   and reset the remote processor
 * @peripheral:	peripheral id
 *
 * Return 0 on success.
 */
int qcom_scm_pas_auth_and_reset(u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_AUTH_AND_RESET,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		return ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	qcom_scm_bw_disable();
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_pas_auth_and_reset);

/**
 * qcom_scm_pas_shutdown() - Shut down the remote processor
 * @peripheral: peripheral id
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_shutdown(u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_SHUTDOWN,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		return ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	qcom_scm_bw_disable();
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_pas_shutdown);

int qcom_scm_pas_shutdown_retry(u32 peripheral)
{
	int ret;
	int retry_num = 0;

	ret = qcom_scm_pas_shutdown(peripheral);
	if (!ret)
		return ret;

	pr_err("PAS Shutdown: First call to shutdown failed with error: %d\n", ret);

	while (retry_num < pas_shutdown_retry_max && ret) {
		retry_num++;
		msleep(pas_shutdown_retry_interval);
		ret = qcom_scm_pas_shutdown(peripheral);
	}
	pr_err("PAS Shutdown: Attempting to shutdown peripheral %d time(s)\n", retry_num);

	return ret;
}
EXPORT_SYMBOL(qcom_scm_pas_shutdown_retry);

/**
 * qcom_scm_pas_supported() - Check if the peripheral authentication service is
 *			      available for the given peripherial
 * @peripheral:	peripheral id
 *
 * Returns true if PAS is supported for this peripheral, otherwise false.
 */
bool qcom_scm_pas_supported(u32 peripheral)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_IS_SUPPORTED,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	if (!__qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_PIL,
					  QCOM_SCM_PIL_PAS_IS_SUPPORTED))
		return false;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? false : !!res.result[0];
}
EXPORT_SYMBOL(qcom_scm_pas_supported);

static int __qcom_scm_pas_mss_reset(struct device *dev, bool reset)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_MSS_RESET,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = reset,
		.args[1] = 0,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}

static int qcom_scm_pas_reset_assert(struct reset_controller_dev *rcdev,
				     unsigned long idx)
{
	if (idx != 0)
		return -EINVAL;

	return __qcom_scm_pas_mss_reset(__scm->dev, 1);
}

static int qcom_scm_pas_reset_deassert(struct reset_controller_dev *rcdev,
				       unsigned long idx)
{
	if (idx != 0)
		return -EINVAL;

	return __qcom_scm_pas_mss_reset(__scm->dev, 0);
}

static const struct reset_control_ops qcom_scm_pas_reset_ops = {
	.assert = qcom_scm_pas_reset_assert,
	.deassert = qcom_scm_pas_reset_deassert,
};

int qcom_scm_get_sec_dump_state(u32 *dump_state)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_UTIL,
		.cmd = QCOM_SCM_UTIL_GET_SEC_DUMP_STATE,
		.owner = ARM_SMCCC_OWNER_SIP
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm ? __scm->dev : NULL, &desc, &res);

	if (dump_state)
		*dump_state = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_get_sec_dump_state);

int qcom_scm_assign_dump_table_region(bool is_assign, phys_addr_t addr, size_t size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_UTIL,
		.cmd = QCOM_SCM_UTIL_DUMP_TABLE_ASSIGN,
		.arginfo = QCOM_SCM_ARGS(3),
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = is_assign,
		.args[1] = addr,
		.args[2] = size,
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_assign_dump_table_region);

int qcom_scm_tz_blsp_modify_owner(int food, u64 subsystem, int *out)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_TZ,
		.cmd = QOCM_SCM_TZ_BLSP_MODIFY_OWNER,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = subsystem,
		.args[1] = food,
		.arginfo = QCOM_SCM_ARGS(2),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (out)
		*out = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_tz_blsp_modify_owner);

int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_READ,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = addr,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;


	ret = qcom_scm_call_atomic(__scm->dev, &desc, &res);
	if (ret >= 0)
		*val = res.result[0];

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(qcom_scm_io_readl);

int qcom_scm_io_writel(phys_addr_t addr, unsigned int val)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_WRITE,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = addr,
		.args[1] = val,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	return qcom_scm_call_atomic(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_io_writel);

/**
 * qcom_scm_io_reset()
 */
int qcom_scm_io_reset(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_RESET,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_io_reset);

bool qcom_scm_is_secure_wdog_trigger_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_BOOT,
						QCOM_SCM_BOOT_SEC_WDOG_TRIGGER);
}
EXPORT_SYMBOL(qcom_scm_is_secure_wdog_trigger_available);

bool qcom_scm_is_mode_switch_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_BOOT,
						QCOM_SCM_BOOT_SWITCH_MODE);
}
EXPORT_SYMBOL(qcom_scm_is_mode_switch_available);

int __qcom_scm_get_feat_version(struct device *dev, u64 feat_id, u64 *version)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_GET_FEAT_VERSION_CMD,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = feat_id,
		.arginfo = QCOM_SCM_ARGS(1),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (version)
		*version = res.result[0];

	return ret;
}

int qcom_scm_get_jtag_etm_feat_id(u64 *version)
{
	return __qcom_scm_get_feat_version(__scm ? __scm->dev : NULL,
					QCOM_SCM_TZ_DBG_ETM_FEAT_ID, version);
}
EXPORT_SYMBOL(qcom_scm_get_jtag_etm_feat_id);

/**
 * qcom_halt_spmi_pmic_arbiter() - Halt SPMI PMIC arbiter
 *
 * Force the SPMI PMIC arbiter to shutdown so that no more SPMI transactions
 * are sent from the MSM to the PMIC. This is required in order to avoid an
 * SPMI lockup on certain PMIC chips if PS_HOLD is lowered in the middle of
 * an SPMI transaction.
 */
void qcom_scm_halt_spmi_pmic_arbiter(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PWR,
		.cmd = QCOM_SCM_PWR_IO_DISABLE_PMIC_ARBITER,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	ret = qcom_scm_call_atomic(__scm->dev, &desc, NULL);
	if (ret)
		pr_debug("Failed to halt_spmi_pmic_arbiter=0x%x\n", ret);
}
EXPORT_SYMBOL(qcom_scm_halt_spmi_pmic_arbiter);

/**
 * qcom_deassert_ps_hold() - Deassert PS_HOLD
 *
 * Deassert PS_HOLD to signal the PMIC that we are ready to power down or reset.
 *
 * This function should never return if the SCM call is available.
 */
void qcom_scm_deassert_ps_hold(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PWR,
		.cmd = QCOM_SCM_PWR_IO_DEASSERT_PS_HOLD,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	ret = qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
	if (ret)
		pr_err("Failed to deassert_ps_hold=0x%x\n", ret);
}
EXPORT_SYMBOL(qcom_scm_deassert_ps_hold);

void qcom_scm_mmu_sync(bool sync)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PWR,
		.cmd = QCOM_SCM_PWR_MMU_SYNC,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = sync,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	ret = qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);

	if (ret)
		pr_err("MMU sync with Hypervisor off %x\n", ret);
}
EXPORT_SYMBOL(qcom_scm_mmu_sync);

/**
 * qcom_scm_restore_sec_cfg_available() - Check if secure environment
 * supports restore security config interface.
 *
 * Return true if restore-cfg interface is supported, false if not.
 */
bool qcom_scm_restore_sec_cfg_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_MP,
					    QCOM_SCM_MP_RESTORE_SEC_CFG);
}
EXPORT_SYMBOL(qcom_scm_restore_sec_cfg_available);

int qcom_scm_restore_sec_cfg(u32 device_id, u32 spare)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_RESTORE_SEC_CFG,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = device_id,
		.args[1] = spare,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_restore_sec_cfg);

int qcom_scm_iommu_secure_ptbl_size(u32 spare, size_t *size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_IOMMU_SECURE_PTBL_SIZE,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = spare,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (size)
		*size = res.result[0];

	return ret ? : res.result[1];
}
EXPORT_SYMBOL(qcom_scm_iommu_secure_ptbl_size);

int qcom_scm_iommu_secure_ptbl_init(u64 addr, u32 size, u32 spare)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_IOMMU_SECURE_PTBL_INIT,
		.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_RW, QCOM_SCM_VAL,
					 QCOM_SCM_VAL),
		.args[0] = addr,
		.args[1] = size,
		.args[2] = spare,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, NULL);

	/* the pg table has been initialized already, ignore the error */
	if (ret == -EPERM)
		ret = 0;

	return ret;
}
EXPORT_SYMBOL(qcom_scm_iommu_secure_ptbl_init);

int qcom_scm_mem_protect_video_var(u32 cp_start, u32 cp_size,
				   u32 cp_nonpixel_start,
				   u32 cp_nonpixel_size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_VIDEO_VAR,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_VAL, QCOM_SCM_VAL,
					 QCOM_SCM_VAL, QCOM_SCM_VAL),
		.args[0] = cp_start,
		.args[1] = cp_size,
		.args[2] = cp_nonpixel_start,
		.args[3] = cp_nonpixel_size,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_mem_protect_video_var);

int qcom_scm_mem_protect_region_id(phys_addr_t paddr, size_t size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_MEM_PROTECT_REGION_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = paddr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_mem_protect_region_id);

int qcom_scm_mem_protect_lock_id2_flat(phys_addr_t list_addr,
				size_t list_size, size_t chunk_size,
				size_t memory_usage, int lock)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_MEM_PROTECT_LOCK_ID2_FLAT,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = list_addr,
		.args[1] = list_size,
		.args[2] = chunk_size,
		.args[3] = memory_usage,
		.args[4] = lock,
		.args[5] = 0,
		.arginfo = QCOM_SCM_ARGS(6, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_mem_protect_lock_id2_flat);

int qcom_scm_iommu_secure_map(phys_addr_t sg_list_addr, size_t num_sg,
				size_t sg_block_size, u64 sec_id, int cbndx,
				unsigned long iova, size_t total_len)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_IOMMU_SECURE_MAP2_FLAT,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = sg_list_addr,
		.args[1] = num_sg,
		.args[2] = sg_block_size,
		.args[3] = sec_id,
		.args[4] = cbndx,
		.args[5] = iova,
		.args[6] = total_len,
		.args[7] = 0,
		.arginfo = QCOM_SCM_ARGS(8, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_iommu_secure_map);

int qcom_scm_iommu_secure_unmap(u64 sec_id, int cbndx, unsigned long iova,
				size_t total_len)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_IOMMU_SECURE_UNMAP2_FLAT,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = sec_id,
		.args[1] = cbndx,
		.args[2] = iova,
		.args[3] = total_len,
		.args[4] = QCOM_SCM_IOMMU_TLBINVAL_FLAG,
		.arginfo = QCOM_SCM_ARGS(5),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_iommu_secure_unmap);

static int __qcom_scm_assign_mem(struct device *dev, phys_addr_t mem_region,
				 size_t mem_sz, phys_addr_t src, size_t src_sz,
				 phys_addr_t dest, size_t dest_sz)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_ASSIGN,
		.arginfo = QCOM_SCM_ARGS(7, QCOM_SCM_RO, QCOM_SCM_VAL,
					 QCOM_SCM_RO, QCOM_SCM_VAL, QCOM_SCM_RO,
					 QCOM_SCM_VAL, QCOM_SCM_VAL),
		.args[0] = mem_region,
		.args[1] = mem_sz,
		.args[2] = src,
		.args[3] = src_sz,
		.args[4] = dest,
		.args[5] = dest_sz,
		.args[6] = 0,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(dev, &desc, &res);

	return ret ? : res.result[0];
}

/**
 * qcom_scm_assign_mem() - Make a secure call to reassign memory ownership
 * @mem_addr: mem region whose ownership need to be reassigned
 * @mem_sz:   size of the region.
 * @srcvm:    vmid for current set of owners, each set bit in
 *            flag indicate a unique owner
 * @newvm:    array having new owners and corresponding permission
 *            flags
 * @dest_cnt: number of owners in next set.
 *
 * Return negative errno on failure or 0 on success with @srcvm updated.
 */
int qcom_scm_assign_mem(phys_addr_t mem_addr, size_t mem_sz,
			u64 *srcvm,
			const struct qcom_scm_vmperm *newvm,
			unsigned int dest_cnt)
{
	struct qcom_scm_current_perm_info *destvm;
	struct qcom_scm_mem_map_info *mem_to_map;
	phys_addr_t mem_to_map_phys;
	phys_addr_t dest_phys;
	dma_addr_t ptr_phys;
	size_t mem_to_map_sz;
	size_t dest_sz;
	size_t src_sz;
	size_t ptr_sz;
	int next_vm;
	__le32 *src;
	void *ptr;
	int ret, i, b;
	u64 srcvm_bits = *srcvm;

	src_sz = hweight64(srcvm_bits) * sizeof(*src);
	mem_to_map_sz = sizeof(*mem_to_map);
	dest_sz = dest_cnt * sizeof(*destvm);
	ptr_sz = ALIGN(src_sz, SZ_64) + ALIGN(mem_to_map_sz, SZ_64) +
			ALIGN(dest_sz, SZ_64);

	ptr = dma_alloc_coherent(__scm->dev, ptr_sz, &ptr_phys, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	/* Fill source vmid detail */
	src = ptr;
	i = 0;
	for (b = 0; b < BITS_PER_TYPE(u64); b++) {
		if (srcvm_bits & BIT(b))
			src[i++] = cpu_to_le32(b);
	}

	/* Fill details of mem buff to map */
	mem_to_map = ptr + ALIGN(src_sz, SZ_64);
	mem_to_map_phys = ptr_phys + ALIGN(src_sz, SZ_64);
	mem_to_map->mem_addr = cpu_to_le64(mem_addr);
	mem_to_map->mem_size = cpu_to_le64(mem_sz);

	next_vm = 0;
	/* Fill details of next vmid detail */
	destvm = ptr + ALIGN(mem_to_map_sz, SZ_64) + ALIGN(src_sz, SZ_64);
	dest_phys = ptr_phys + ALIGN(mem_to_map_sz, SZ_64) + ALIGN(src_sz, SZ_64);
	for (i = 0; i < dest_cnt; i++, destvm++, newvm++) {
		destvm->vmid = cpu_to_le32(newvm->vmid);
		destvm->perm = cpu_to_le32(newvm->perm);
		destvm->ctx = 0;
		destvm->ctx_size = 0;
		next_vm |= BIT(newvm->vmid);
	}

	ret = __qcom_scm_assign_mem(__scm->dev, mem_to_map_phys, mem_to_map_sz,
				    ptr_phys, src_sz, dest_phys, dest_sz);
	dma_free_coherent(__scm->dev, ptr_sz, ptr, ptr_phys);
	if (ret) {
		dev_err(__scm->dev,
			"Assign memory protection call failed %d\n", ret);
		return -EINVAL;
	}

	*srcvm = next_vm;
	return 0;
}
EXPORT_SYMBOL(qcom_scm_assign_mem);

/**
 * qcom_scm_assign_mem_regions() - Make a secure call to reassign memory
 *				   ownership of several memory regions
 * @mem_regions:    A buffer describing the set of memory regions that need to
 *		    be reassigned
 * @mem_regions_sz: The size of the buffer describing the set of memory
 *                  regions that need to be reassigned (in bytes)
 * @srcvms:	    A buffer populated with he vmid(s) for the current set of
 *		    owners
 * @src_sz:	    The size of the src_vms buffer (in bytes)
 * @newvms:	    A buffer populated with the new owners and corresponding
 *		    permission flags.
 * @newvms_sz:	    The size of the new_vms buffer (in bytes)
 *
 * NOTE: It is up to the caller to ensure that the buffers that will be accessed
 * by the secure world are cache aligned, and have been flushed prior to
 * invoking this call.
 *
 * Return negative errno on failure, 0 on success.
 */
int qcom_scm_assign_mem_regions(struct qcom_scm_mem_map_info *mem_regions,
				size_t mem_regions_sz, u32 *srcvms,
				size_t src_sz,
				struct qcom_scm_current_perm_info *newvms,
				size_t newvms_sz)
{
	return __qcom_scm_assign_mem(__scm ? __scm->dev : NULL,
				     virt_to_phys(mem_regions), mem_regions_sz,
				     virt_to_phys(srcvms), src_sz,
				     virt_to_phys(newvms), newvms_sz);
}
EXPORT_SYMBOL(qcom_scm_assign_mem_regions);

/**
 * qcom_scm_mem_protect_sd_ctrl() - SDE memory protect.
 *
 */
int qcom_scm_mem_protect_sd_ctrl(u32 devid, phys_addr_t mem_addr, u64 mem_size,
				u32 vmid)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_CMD_SD_CTRL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = devid,
		.args[1] = mem_addr,
		.args[2] = mem_size,
		.args[3] = vmid,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_VAL, QCOM_SCM_RW,
					 QCOM_SCM_VAL, QCOM_SCM_VAL)
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_mem_protect_sd_ctrl);

bool qcom_scm_kgsl_set_smmu_aperture_available(void)
{
	int ret;

	ret = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_MP,
					QCOM_SCM_MP_CP_SMMU_APERTURE_ID);

	return ret > 0;
}
EXPORT_SYMBOL(qcom_scm_kgsl_set_smmu_aperture_available);

int qcom_scm_kgsl_set_smmu_aperture(unsigned int num_context_bank)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_CP_SMMU_APERTURE_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0xffff0000
			   | ((QCOM_SCM_CP_APERTURE_REG & 0xff) << 8)
			   | (num_context_bank & 0xff),
		.args[1] = 0xffffffff,
		.args[2] = 0xffffffff,
		.args[3] = 0xffffffff,
		.arginfo = QCOM_SCM_ARGS(4),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_kgsl_set_smmu_aperture);

int qcom_scm_kgsl_set_smmu_lpac_aperture(unsigned int num_context_bank)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_CP_SMMU_APERTURE_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0xffff0000
			   | ((QCOM_SCM_CP_LPAC_APERTURE_REG & 0xff) << 8)
			   | (num_context_bank & 0xff),
		.args[1] = 0xffffffff,
		.args[2] = 0xffffffff,
		.args[3] = 0xffffffff,
		.arginfo = QCOM_SCM_ARGS(4),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_kgsl_set_smmu_lpac_aperture);

int qcom_scm_kgsl_init_regs(u32 gpu_req)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_GPU,
		.cmd = QCOM_SCM_SVC_GPU_INIT_REGS,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = gpu_req,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_kgsl_init_regs);

int qcom_scm_enable_shm_bridge(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MEMP_SHM_BRIDGE_ENABLE,
		.owner = ARM_SMCCC_OWNER_SIP
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_enable_shm_bridge);

int qcom_scm_delete_shm_bridge(u64 handle)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MEMP_SHM_BRIDGE_DELETE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = handle,
		.arginfo = QCOM_SCM_ARGS(1, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_delete_shm_bridge);

int qcom_scm_create_shm_bridge(u64 pfn_and_ns_perm_flags,
	u64 ipfn_and_s_perm_flags, u64 size_and_flags, u64 ns_vmids,
	u64 *handle)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MEMP_SHM_BRDIGE_CREATE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = pfn_and_ns_perm_flags,
		.args[1] = ipfn_and_s_perm_flags,
		.args[2] = size_and_flags,
		.args[3] = ns_vmids,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (handle)
		*handle = res.result[1];

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_create_shm_bridge);

int qcom_scm_smmu_prepare_atos_id(u64 dev_id, int cb_num, int operation)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_SMMU_PREPARE_ATOS_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = dev_id,
		.args[1] = cb_num,
		.args[2] = operation,
		.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_smmu_prepare_atos_id);

/**
 * qcom_mdf_assign_memory_to_subsys - SDE memory protect.
 *
 */
int qcom_mdf_assign_memory_to_subsys(u64 start_addr, u64 end_addr,
		phys_addr_t paddr, u64 size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_MPU_LOCK_NS_REGION,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = start_addr,
		.args[1] = end_addr,
		.args[2] = paddr,
		.args[3] = size,
		.arginfo = QCOM_SCM_ARGS(4),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_mdf_assign_memory_to_subsys);

/**
 * qcom_scm_dcvs_core_available() - check if core DCVS operations are available
 */
bool qcom_scm_dcvs_core_available(void)
{
	struct device *dev = __scm ? __scm->dev : NULL;

	return __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					    QCOM_SCM_DCVS_INIT) &&
	       __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					    QCOM_SCM_DCVS_UPDATE) &&
	       __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					    QCOM_SCM_DCVS_RESET);
}
EXPORT_SYMBOL(qcom_scm_dcvs_core_available);

/**
 * qcom_scm_dcvs_ca_available() - check if context aware DCVS operations are
 * available
 */
bool qcom_scm_dcvs_ca_available(void)
{
	struct device *dev = __scm ? __scm->dev : NULL;

	return __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					    QCOM_SCM_DCVS_INIT_CA_V2) &&
	       __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					    QCOM_SCM_DCVS_UPDATE_CA_V2);
}
EXPORT_SYMBOL(qcom_scm_dcvs_ca_available);

/**
 * qcom_scm_dcvs_reset()
 */
int qcom_scm_dcvs_reset(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_RESET,
		.owner = ARM_SMCCC_OWNER_SIP
	};

	return qcom_scm_call(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_dcvs_reset);

int qcom_scm_dcvs_init_v2(phys_addr_t addr, size_t size, int *version)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_INIT_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = addr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (ret >= 0)
		*version = res.result[0];
	return ret;
}
EXPORT_SYMBOL(qcom_scm_dcvs_init_v2);

int qcom_scm_dcvs_init_ca_v2(phys_addr_t addr, size_t size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_INIT_CA_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = addr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_dcvs_init_ca_v2);

int qcom_scm_dcvs_update(int level, s64 total_time, s64 busy_time)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.arginfo = QCOM_SCM_ARGS(3),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call_atomic(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_dcvs_update);

int qcom_scm_dcvs_update_v2(int level, s64 total_time, s64 busy_time)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.arginfo = QCOM_SCM_ARGS(3),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_dcvs_update_v2);

int qcom_scm_dcvs_update_ca_v2(int level, s64 total_time, s64 busy_time,
			       int context_count)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE_CA_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.args[3] = context_count,
		.arginfo = QCOM_SCM_ARGS(4),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_dcvs_update_ca_v2);

int qcom_scm_get_feat_version_cp(u64 *version)
{
	return __qcom_scm_get_feat_version(__scm->dev, QCOM_SCM_MP_CP_FEAT_ID,
						version);
}
EXPORT_SYMBOL(qcom_scm_get_feat_version_cp);

/**
 * qcom_scm_ocmem_lock_available() - is OCMEM lock/unlock interface available
 */
bool qcom_scm_ocmem_lock_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_OCMEM,
					    QCOM_SCM_OCMEM_LOCK_CMD);
}
EXPORT_SYMBOL(qcom_scm_ocmem_lock_available);

/**
 * qcom_scm_ocmem_lock() - call OCMEM lock interface to assign an OCMEM
 * region to the specified initiator
 *
 * @id:     tz initiator id
 * @offset: OCMEM offset
 * @size:   OCMEM size
 * @mode:   access mode (WIDE/NARROW)
 */
int qcom_scm_ocmem_lock(enum qcom_scm_ocmem_client id, u32 offset, u32 size,
			u32 mode)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_OCMEM,
		.cmd = QCOM_SCM_OCMEM_LOCK_CMD,
		.args[0] = id,
		.args[1] = offset,
		.args[2] = size,
		.args[3] = mode,
		.arginfo = QCOM_SCM_ARGS(4),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_ocmem_lock);

/**
 * qcom_scm_ocmem_unlock() - call OCMEM unlock interface to release an OCMEM
 * region from the specified initiator
 *
 * @id:     tz initiator id
 * @offset: OCMEM offset
 * @size:   OCMEM size
 */
int qcom_scm_ocmem_unlock(enum qcom_scm_ocmem_client id, u32 offset, u32 size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_OCMEM,
		.cmd = QCOM_SCM_OCMEM_UNLOCK_CMD,
		.args[0] = id,
		.args[1] = offset,
		.args[2] = size,
		.arginfo = QCOM_SCM_ARGS(3),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_ocmem_unlock);

/**
 * qcom_scm_ice_available() - Is the ICE key programming interface available?
 *
 * Return: true iff the SCM calls wrapped by qcom_scm_ice_invalidate_key() and
 *	   qcom_scm_ice_set_key() are available.
 */
bool qcom_scm_ice_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_ES,
					    QCOM_SCM_ES_INVALIDATE_ICE_KEY) &&
		__qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_ES,
					     QCOM_SCM_ES_CONFIG_SET_ICE_KEY);
}
EXPORT_SYMBOL(qcom_scm_ice_available);

/**
 * qcom_scm_ice_invalidate_key() - Invalidate an inline encryption key
 * @index: the keyslot to invalidate
 *
 * The UFSHCI and eMMC standards define a standard way to do this, but it
 * doesn't work on these SoCs; only this SCM call does.
 *
 * It is assumed that the SoC has only one ICE instance being used, as this SCM
 * call doesn't specify which ICE instance the keyslot belongs to.
 *
 * Return: 0 on success; -errno on failure.
 */
int qcom_scm_ice_invalidate_key(u32 index)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_ES,
		.cmd = QCOM_SCM_ES_INVALIDATE_ICE_KEY,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = index,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_ice_invalidate_key);

/**
 * qcom_scm_ice_set_key() - Set an inline encryption key
 * @index: the keyslot into which to set the key
 * @key: the key to program
 * @key_size: the size of the key in bytes
 * @cipher: the encryption algorithm the key is for
 * @data_unit_size: the encryption data unit size, i.e. the size of each
 *		    individual plaintext and ciphertext.  Given in 512-byte
 *		    units, e.g. 1 = 512 bytes, 8 = 4096 bytes, etc.
 *
 * Program a key into a keyslot of Qualcomm ICE (Inline Crypto Engine), where it
 * can then be used to encrypt/decrypt UFS or eMMC I/O requests inline.
 *
 * The UFSHCI and eMMC standards define a standard way to do this, but it
 * doesn't work on these SoCs; only this SCM call does.
 *
 * It is assumed that the SoC has only one ICE instance being used, as this SCM
 * call doesn't specify which ICE instance the keyslot belongs to.
 *
 * Return: 0 on success; -errno on failure.
 */
int qcom_scm_ice_set_key(u32 index, const u8 *key, u32 key_size,
			 enum qcom_scm_ice_cipher cipher, u32 data_unit_size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_ES,
		.cmd = QCOM_SCM_ES_CONFIG_SET_ICE_KEY,
		.arginfo = QCOM_SCM_ARGS(5, QCOM_SCM_VAL, QCOM_SCM_RW,
					 QCOM_SCM_VAL, QCOM_SCM_VAL,
					 QCOM_SCM_VAL),
		.args[0] = index,
		.args[2] = key_size,
		.args[3] = cipher,
		.args[4] = data_unit_size,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	void *keybuf;
	dma_addr_t key_phys;
	int ret;

	/*
	 * 'key' may point to vmalloc()'ed memory, but we need to pass a
	 * physical address that's been properly flushed.  The sanctioned way to
	 * do this is by using the DMA API.  But as is best practice for crypto
	 * keys, we also must wipe the key after use.  This makes kmemdup() +
	 * dma_map_single() not clearly correct, since the DMA API can use
	 * bounce buffers.  Instead, just use dma_alloc_coherent().  Programming
	 * keys is normally rare and thus not performance-critical.
	 */

	keybuf = dma_alloc_coherent(__scm->dev, key_size, &key_phys,
				    GFP_KERNEL);
	if (!keybuf)
		return -ENOMEM;
	memcpy(keybuf, key, key_size);
	desc.args[1] = key_phys;

	ret = qcom_scm_call(__scm->dev, &desc, NULL);

	memzero_explicit(keybuf, key_size);

	dma_free_coherent(__scm->dev, key_size, keybuf, key_phys);
	return ret;
}
EXPORT_SYMBOL(qcom_scm_ice_set_key);

int qcom_scm_config_set_ice_key(uint32_t index, phys_addr_t paddr, size_t size,
				uint32_t cipher, unsigned int data_unit,
				unsigned int ce)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_ES,
		.cmd = QCOM_SCM_ES_CONFIG_SET_ICE_KEY_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = index,
		.args[1] = paddr,
		.args[2] = size,
		.args[3] = cipher,
		.args[4] = data_unit,
		.args[5] = ce,
		.arginfo = QCOM_SCM_ARGS(6, QCOM_SCM_VAL, QCOM_SCM_RW,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_config_set_ice_key);

int qcom_scm_clear_ice_key(uint32_t index,  unsigned int ce)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_ES,
		.cmd = QCOM_SCM_ES_CLEAR_ICE_KEY,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = index,
		.args[1] = ce,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_clear_ice_key);

int qcom_scm_derive_raw_secret(phys_addr_t paddr_key, size_t size_key,
		phys_addr_t paddr_secret, size_t size_secret)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_ES,
		.cmd = QCOM_SCM_ES_DERIVE_RAW_SECRET,
		.owner = ARM_SMCCC_OWNER_SIP
	};

	desc.args[0] = paddr_key;
	desc.args[1] = size_key;
	desc.args[2] = paddr_secret;
	desc.args[3] = size_secret;
	desc.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_RW, QCOM_SCM_VAL);
	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_derive_raw_secret);

/**
 * qcom_scm_hdcp_available() - Check if secure environment supports HDCP.
 *
 * Return true if HDCP is supported, false if not.
 */
bool qcom_scm_hdcp_available(void)
{
	bool avail;
	int ret = qcom_scm_clk_enable();

	if (ret)
		return ret;

	avail = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_HDCP,
						QCOM_SCM_HDCP_INVOKE);

	qcom_scm_clk_disable();

	return avail;
}
EXPORT_SYMBOL(qcom_scm_hdcp_available);

/**
 * qcom_scm_hdcp_req() - Send HDCP request.
 * @req: HDCP request array
 * @req_cnt: HDCP request array count
 * @resp: response buffer passed to SCM
 *
 * Write HDCP register(s) through SCM.
 */
int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt, u32 *resp)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_HDCP,
		.cmd = QCOM_SCM_HDCP_INVOKE,
		.arginfo = QCOM_SCM_ARGS(10),
		.args = {
			req[0].addr,
			req[0].val,
			req[1].addr,
			req[1].val,
			req[2].addr,
			req[2].val,
			req[3].addr,
			req[3].val,
			req[4].addr,
			req[4].val
		},
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	if (req_cnt > QCOM_SCM_HDCP_MAX_REQ_CNT)
		return -ERANGE;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	*resp = res.result[0];

	qcom_scm_clk_disable();

	return ret;
}
EXPORT_SYMBOL(qcom_scm_hdcp_req);

bool qcom_scm_is_lmh_debug_set_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH,
					QCOM_SCM_LMH_DEBUG_SET);
}
EXPORT_SYMBOL(qcom_scm_is_lmh_debug_set_available);

bool qcom_scm_is_lmh_debug_read_buf_size_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH,
					QCOM_SCM_LMH_DEBUG_READ_BUF_SIZE);
}
EXPORT_SYMBOL(qcom_scm_is_lmh_debug_read_buf_size_available);

bool qcom_scm_is_lmh_debug_read_buf_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH,
					QCOM_SCM_LMH_DEBUG_READ);
}
EXPORT_SYMBOL(qcom_scm_is_lmh_debug_read_buf_available);

bool qcom_scm_is_lmh_debug_get_type_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH,
					QCOM_SCM_LMH_DEBUG_GET_TYPE);
}
EXPORT_SYMBOL(qcom_scm_is_lmh_debug_get_type_available);

int qcom_scm_lmh_read_buf_size(int *size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_DEBUG_READ_BUF_SIZE,
		.owner = ARM_SMCCC_OWNER_SIP
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (size)
		*size = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_lmh_read_buf_size);

int qcom_scm_lmh_limit_dcvsh(phys_addr_t payload, uint32_t payload_size,
			u64 limit_node, uint32_t node_id, u64 version)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_LIMIT_DCVSH,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = payload,
		.args[1] = payload_size,
		.args[2] = limit_node,
		.args[3] = node_id,
		.args[4] = version,
		.arginfo = QCOM_SCM_ARGS(5, QCOM_SCM_RO, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_lmh_limit_dcvsh);

int qcom_scm_lmh_debug_read(phys_addr_t payload, uint32_t size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_DEBUG_READ,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = payload,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_lmh_debug_read);

int __qcom_scm_lmh_debug_config_write(struct device *dev, u64 cmd_id,
			phys_addr_t payload, int payload_size, uint32_t *buf,
			int buf_size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = cmd_id,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = payload,
		.args[1] = payload_size,
		.args[2] = buf[0],
		.args[3] = buf[1],
		.args[4] = buf[2],
		.arginfo = QCOM_SCM_ARGS(5, QCOM_SCM_RO, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL),
	};

	if (buf_size < 3)
		return -EINVAL;

	return qcom_scm_call(dev, &desc, NULL);
}

int qcom_scm_lmh_debug_set_config_write(phys_addr_t payload, int payload_size,
					uint32_t *buf, int buf_size)
{
	return __qcom_scm_lmh_debug_config_write(__scm->dev,
			QCOM_SCM_LMH_DEBUG_SET, payload, payload_size, buf,
			buf_size);
}
EXPORT_SYMBOL(qcom_scm_lmh_debug_set_config_write);

int qcom_scm_lmh_get_type(phys_addr_t payload, u64 payload_size,
		u64 debug_type, uint32_t get_from, uint32_t *size)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_DEBUG_GET_TYPE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = payload,
		.args[1] = payload_size,
		.args[2] = debug_type,
		.args[3] = get_from,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (size)
		*size = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_lmh_get_type);

int qcom_scm_lmh_fetch_data(u32 node_id, u32 debug_type, uint32_t *peak,
		uint32_t *avg)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_DEBUG_FETCH_DATA,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = node_id,
		.args[1] = debug_type,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH,
					   QCOM_SCM_LMH_DEBUG_FETCH_DATA);
	if (ret <= 0)
		return ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (peak)
		*peak = res.result[0];
	if (avg)
		*avg = res.result[1];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_lmh_fetch_data);

int qcom_scm_smmu_change_pgtbl_format(u64 dev_id, int cbndx)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMMU_PROGRAM,
		.cmd = QCOM_SCM_SMMU_CHANGE_PGTBL_FORMAT,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = dev_id,
		.args[1] = cbndx,
		.args[2] = 1,	/* Enable */
		.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_smmu_change_pgtbl_format);

int qcom_scm_qsmmu500_wait_safe_toggle(bool en)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMMU_PROGRAM,
		.cmd = QCOM_SCM_SMMU_CONFIG_ERRATA1,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = QCOM_SCM_SMMU_CONFIG_ERRATA1_CLIENT_ALL,
		.args[1] = en,
		.owner = ARM_SMCCC_OWNER_SIP,
	};


	return qcom_scm_call_atomic(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_qsmmu500_wait_safe_toggle);

int qcom_scm_smmu_notify_secure_lut(u64 dev_id, bool secure)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMMU_PROGRAM,
		.cmd = QCOM_SCM_SMMU_SECURE_LUT,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = dev_id,
		.args[1] = secure,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_smmu_notify_secure_lut);

int qcom_scm_qdss_invoke(phys_addr_t paddr, size_t size, u64 *out)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QDSS,
		.cmd = QCOM_SCM_QDSS_INVOKE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = paddr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RO, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (out)
		*out = res.result[1];

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_qdss_invoke);

int qcom_scm_camera_protect_all(uint32_t protect, uint32_t param)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_CAMERA,
		.cmd = QCOM_SCM_CAMERA_PROTECT_ALL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = protect,
		.args[1] = param,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_camera_protect_all);

int qcom_scm_camera_protect_phy_lanes(bool protect, u64 regmask)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_CAMERA,
		.cmd = QCOM_SCM_CAMERA_PROTECT_PHY_LANES,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = protect,
		.args[1] = regmask,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_camera_protect_phy_lanes);

int qcom_scm_tsens_reinit(int *tsens_ret)
{
	unsigned int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_TSENS,
		.cmd = QCOM_SCM_TSENS_INIT_ID,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	if (tsens_ret)
		*tsens_ret = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_tsens_reinit);

static int qcom_scm_reboot(struct device *dev)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_OEM_POWER,
		.cmd = QCOM_SCM_OEM_POWER_REBOOT,
		.owner = ARM_SMCCC_OWNER_OEM,
	};

	return qcom_scm_call_atomic(dev, &desc, NULL);
}

int qcom_scm_ice_restore_cfg(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_KEYSTORE,
		.cmd = QCOM_SCM_ICE_RESTORE_KEY_ID,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_ice_restore_cfg);

bool qcom_scm_lmh_dcvsh_available(void)
{
	return __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_LMH, QCOM_SCM_LMH_LIMIT_DCVSH);
}
EXPORT_SYMBOL(qcom_scm_lmh_dcvsh_available);

int qcom_scm_lmh_profile_change(u32 profile_id)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_LIMIT_PROFILE_CHANGE,
		.arginfo = QCOM_SCM_ARGS(1, QCOM_SCM_VAL),
		.args[0] = profile_id,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_lmh_profile_change);

int qcom_scm_lmh_dcvsh(u32 payload_fn, u32 payload_reg, u32 payload_val,
		       u64 limit_node, u32 node_id, u64 version)
{
	dma_addr_t payload_phys;
	u32 *payload_buf;
	int ret, payload_size = 5 * sizeof(u32);

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_LMH,
		.cmd = QCOM_SCM_LMH_LIMIT_DCVSH,
		.arginfo = QCOM_SCM_ARGS(5, QCOM_SCM_RO, QCOM_SCM_VAL, QCOM_SCM_VAL,
					QCOM_SCM_VAL, QCOM_SCM_VAL),
		.args[1] = payload_size,
		.args[2] = limit_node,
		.args[3] = node_id,
		.args[4] = version,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	payload_buf = dma_alloc_coherent(__scm->dev, payload_size, &payload_phys, GFP_KERNEL);
	if (!payload_buf)
		return -ENOMEM;

	payload_buf[0] = payload_fn;
	payload_buf[1] = 0;
	payload_buf[2] = payload_reg;
	payload_buf[3] = 1;
	payload_buf[4] = payload_val;

	desc.args[0] = payload_phys;

	ret = qcom_scm_call(__scm->dev, &desc, NULL);

	dma_free_coherent(__scm->dev, payload_size, payload_buf, payload_phys);
	return ret;
}
EXPORT_SYMBOL(qcom_scm_lmh_dcvsh);

int qcom_scm_prefetch_tgt_ctrl(bool en)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_APP_MGR,
		.cmd = QCOM_SCM_PREFETCH_TGT_CTRL,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = en,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call_atomic(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL(qcom_scm_prefetch_tgt_ctrl);

int qcom_scm_get_tz_log_feat_id(u64 *version)
{
	return __qcom_scm_get_feat_version(__scm->dev, QCOM_SCM_FEAT_LOG_ID,
					   version);
}
EXPORT_SYMBOL(qcom_scm_get_tz_log_feat_id);

int qcom_scm_get_tz_feat_id_version(u64 feat_id, u64 *version)
{
	return __qcom_scm_get_feat_version(__scm->dev, feat_id,
					   version);
}
EXPORT_SYMBOL(qcom_scm_get_tz_feat_id_version);

int qcom_scm_register_qsee_log_buf(phys_addr_t buf, size_t len)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_QSEELOG_REGISTER,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = buf,
		.args[1] = len,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_register_qsee_log_buf);

int qcom_scm_query_encrypted_log_feature(u64 *enabled)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_QUERY_ENCR_LOG_FEAT_ID,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	if (enabled)
		*enabled = res.result[0];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_query_encrypted_log_feature);

int qcom_scm_request_encrypted_log(phys_addr_t buf,
				   size_t len,
				   uint32_t log_id,
				   bool is_full_tz_logs_supported,
				   bool is_full_tz_logs_enabled)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_REQUEST_ENCR_LOG_ID,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = buf,
		.args[1] = len,
		.args[2] = log_id
	};
	struct qcom_scm_res res;

	if (is_full_tz_logs_supported) {
		if (is_full_tz_logs_enabled) {
			/* requesting full logs */
			desc.args[3] = 1;
		} else {
			/* requesting incremental logs */
			desc.args[3] = 0;
		}
		desc.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW);
	} else {
		desc.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_RW);
	}
	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL(qcom_scm_request_encrypted_log);

int qcom_scm_invoke_smc_legacy(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_INVOKE_LEGACY,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = in_buf,
		.args[1] = in_buf_size,
		.args[2] = out_buf,
		.args[3] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
			QCOM_SCM_RW, QCOM_SCM_VAL),
		.multicall_allowed = true,
	};

	struct qcom_scm_res res;

	ret = qcom_scm_call_noretry(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_invoke_smc_legacy);

int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_INVOKE,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = in_buf,
		.args[1] = in_buf_size,
		.args[2] = out_buf,
		.args[3] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_RW, QCOM_SCM_VAL),
		.multicall_allowed = true,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call_noretry(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_invoke_smc);

int qcom_scm_invoke_callback_response(phys_addr_t out_buf,
	size_t out_buf_size, int32_t *result, u64 *response_type,
	unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_CB_RSP,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = out_buf,
		.args[1] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
		.multicall_allowed = true,
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call_noretry(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_invoke_callback_response);

int qcom_scm_qseecom_call(u32 cmd_id, struct qseecom_scm_desc *desc, bool retry)
{
	int ret;
	struct device *dev = __scm ? __scm->dev : NULL;
	struct qcom_scm_desc _desc = {
		.svc = (cmd_id & 0xff00) >> 8,
		.cmd = (cmd_id & 0xff),
		.owner = (cmd_id & 0x3f000000) >> 24,
		.args[0] = desc->args[0],
		.args[1] = desc->args[1],
		.args[2] = desc->args[2],
		.args[3] = desc->args[3],
		.args[4] = desc->args[4],
		.args[5] = desc->args[5],
		.args[6] = desc->args[6],
		.args[7] = desc->args[7],
		.args[8] = desc->args[8],
		.args[9] = desc->args[9],
		.arginfo = desc->arginfo,
	};
	struct qcom_scm_res res;

	if (retry)
		ret = qcom_scm_call(dev, &_desc, &res);
	else
		ret = qcom_scm_call_noretry(dev, &_desc, &res);

	desc->ret[0] = res.result[0];
	desc->ret[1] = res.result[1];
	desc->ret[2] = res.result[2];

	return ret;
}
EXPORT_SYMBOL(qcom_scm_qseecom_call);


static int qcom_scm_find_dload_address(struct device *dev, u64 *addr)
{
	struct device_node *tcsr;
	struct device_node *np = dev->of_node;
	struct resource res;
	u32 offset;
	int ret;

	tcsr = of_parse_phandle(np, "qcom,dload-mode", 0);
	if (!tcsr)
		return 0;

	ret = of_address_to_resource(tcsr, 0, &res);
	of_node_put(tcsr);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, "qcom,dload-mode", 1, &offset);
	if (ret < 0)
		return ret;

	*addr = res.start + offset;

	return 0;
}

/**
 * qcom_scm_is_available() - Checks if SCM is available
 */
bool qcom_scm_is_available(void)
{
	return !!__scm;
}
EXPORT_SYMBOL(qcom_scm_is_available);

void *qcom_get_scm_device(void)
{
	return __scm ? __scm->dev : NULL;
}
EXPORT_SYMBOL(qcom_get_scm_device);

static int qcom_scm_do_restart(struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	struct qcom_scm *scm = container_of(this, struct qcom_scm, restart_nb);

	if (reboot_mode == REBOOT_WARM)
		qcom_scm_reboot(scm->dev);

	return NOTIFY_OK;
}

static int qcom_scm_query_wq_queue_info(struct qcom_scm *scm)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_WAITQ,
		.cmd = QCOM_SCM_GET_WQ_QUEUE_INFO,
		.owner = ARM_SMCCC_OWNER_SIP
	};
	struct qcom_scm_res res;

	scm->waitq.wq_feature = QCOM_SCM_SINGLE_SMC_ALLOW;
	ret = qcom_scm_call_atomic(__scm->dev, &desc, &res);
	if (ret) {
		pr_err("%s: Failed to get wq queue info: %d\n", __func__, ret);
		return ret;
	}

	scm->waitq.call_ctx_cnt = res.result[0] & 0xFF;
	scm->waitq.irq = res.result[1] & 0xFFFF;
	scm->waitq.wq_feature = QCOM_SCM_MULTI_SMC_WHITE_LIST_ALLOW;

	pr_info("WQ Info, feature: %d call_ctx_cnt: %d irq: %d\n",
		scm->waitq.wq_feature, scm->waitq.call_ctx_cnt, scm->waitq.irq);

	return ret;
}

bool qcom_scm_multi_call_allow(struct device *dev, bool multicall_allowed)
{
	struct qcom_scm *scm;

	scm = dev_get_drvdata(dev);
	if (multicall_allowed &&
		scm->waitq.wq_feature == QCOM_SCM_MULTI_SMC_WHITE_LIST_ALLOW)
		return true;

	return false;
};

struct completion *qcom_scm_lookup_wq(struct qcom_scm *scm, u32 wq_ctx)
{
	struct completion *wq = NULL;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&scm->waitq.idr_lock, flags);
	wq = idr_find(&scm->waitq.idr, wq_ctx);
	if (wq)
		goto out;

	wq = devm_kzalloc(scm->dev, sizeof(*wq), GFP_ATOMIC);
	if (!wq) {
		wq = ERR_PTR(-ENOMEM);
		goto out;
	}

	init_completion(wq);

	err = idr_alloc_u32(&scm->waitq.idr, wq, &wq_ctx, wq_ctx, GFP_ATOMIC);
	if (err < 0) {
		devm_kfree(scm->dev, wq);
		wq = ERR_PTR(err);
	}

out:
	spin_unlock_irqrestore(&scm->waitq.idr_lock, flags);
	return wq;
}

void scm_waitq_flag_handler(struct completion *wq, u32 flags)
{
	switch (flags) {
	case QCOM_SMC_WAITQ_FLAG_WAKE_ONE:
		complete(wq);
		break;
	case QCOM_SMC_WAITQ_FLAG_WAKE_ALL:
		complete_all(wq);
		reinit_completion(wq);
		break;
	default:
		pr_err("invalid flags: %u\n", flags);
	}
}

static void scm_irq_work(struct work_struct *work)
{
	int ret;
	u32 wq_ctx, flags, more_pending = 0;
	struct completion *wq_to_wake;
	struct qcom_scm_waitq *w = container_of(work, struct qcom_scm_waitq, scm_irq_work);
	struct qcom_scm *scm = container_of(w, struct qcom_scm, waitq);

	if (qcom_scm_convention != SMC_CONVENTION_ARM_64) {
		/* Unsupported */
		return;
	}

	do {
		ret = scm_get_wq_ctx(&wq_ctx, &flags, &more_pending);
		if (ret) {
			pr_err("GET_WQ_CTX SMC call failed: %d\n", ret);
			return;
		}

		wq_to_wake = qcom_scm_lookup_wq(scm, wq_ctx);
		if (IS_ERR_OR_NULL(wq_to_wake)) {
			pr_err("No waitqueue found for wq_ctx %d: %d\n",
					wq_ctx, PTR_ERR(wq_to_wake));
			return;
		}

		scm_waitq_flag_handler(wq_to_wake, flags);
	} while (more_pending);
}

static irqreturn_t qcom_scm_irq_handler(int irq, void *p)
{
	struct qcom_scm *scm = p;

	schedule_work(&scm->waitq.scm_irq_work);

	return IRQ_HANDLED;
}

static int __qcom_multi_smc_init(struct qcom_scm *__scm,
						struct platform_device *pdev)
{
	int ret = 0, irq;

	spin_lock_init(&__scm->waitq.idr_lock);
	idr_init(&__scm->waitq.idr);
	if (of_device_is_compatible(__scm->dev->of_node, "qcom,scm-v1.1")) {
		INIT_WORK(&__scm->waitq.scm_irq_work, scm_irq_work);

		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			dev_err(__scm->dev, "WQ IRQ is not specified: %d\n", irq);
			return irq;
		}

		ret = devm_request_threaded_irq(__scm->dev, irq, NULL,
			qcom_scm_irq_handler, IRQF_ONESHOT, "qcom-scm", __scm);
		if (ret < 0) {
			dev_err(__scm->dev, "Failed to request qcom-scm irq: %d\n", ret);
			return ret;
		}

		/* Detect Multi SMC support present or not */
		qcom_scm_query_wq_queue_info(__scm);
	}

	return ret;
}

/**
 * scm_mem_protection_init_do() - Makes core kernel bootup milestone call
 *                                to Kernel Protect (KP) in Hypervisor
 *                                to start kernel memory protection. KP will
 *                                start protection on kernel sections like
 *                                .text, .rodata, .bss, .data with applying
 *                                permissions in EL2 page table.
 *
 * @pid_offset:       Offset of PID in task_struct structure to pass in
 *                    hypervisor syscall.
 * @task_name_offset: Offset of task name in task_struct structure to pass in
 *                    hypervisor syscall.
 *
 * Returns 0 on success.
 */
int  scm_mem_protection_init_do(void)
{
	int ret = 0, resp;
	uint32_t pid_offset = 0;
	uint32_t task_name_offset = 0;
	struct qcom_scm_desc desc = {
		.svc = SCM_SVC_RTIC,
		.cmd = TZ_HLOS_NOTIFY_CORE_KERNEL_BOOTUP,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	struct qcom_scm_res res;

	if (!__scm) {
		pr_err("SCM dev is not initialized\n");
		return -1;
	}

	/*
	 * Fetching offset of PID and task_name from task_struct.
	 * This will be used by fault handler of Kernel Protect (KP)
	 * in hypervisor to read PID and task name of process for
	 * which KP fault handler is triggered. This is required to
	 * record PID and task name in integrity report of kernel.
	 */
	pid_offset = offsetof(struct task_struct, pid);
	task_name_offset = offsetof(struct task_struct, comm);

	pr_debug("offset of pid is %zu, offset of comm is %zu\n",
			pid_offset, task_name_offset);
	desc.args[0] = pid_offset,
	desc.args[1] = task_name_offset,

	ret = qcom_scm_call(__scm ? __scm->dev : NULL, &desc, &res);
	resp = res.result[0];

	pr_debug("SCM call values: ret %d, resp %d\n",
			ret, resp);

	if (ret || resp) {
		pr_err("SCM call failed %d, resp %d\n", ret, resp);
		if (ret)
			return ret;
	}

	return resp;
}

static int qcom_scm_probe(struct platform_device *pdev)
{
	struct qcom_scm *scm;
	unsigned long clks;
	int ret;

	scm = devm_kzalloc(&pdev->dev, sizeof(*scm), GFP_KERNEL);
	if (!scm)
		return -ENOMEM;

	ret = qcom_scm_find_dload_address(&pdev->dev, &scm->dload_mode_addr);
	if (ret < 0)
		return ret;

	mutex_init(&scm->scm_bw_lock);

	clks = (unsigned long)of_device_get_match_data(&pdev->dev);

	scm->path = devm_of_icc_get(&pdev->dev, NULL);
	if (IS_ERR(scm->path))
		return dev_err_probe(&pdev->dev, PTR_ERR(scm->path),
				     "failed to acquire interconnect path\n");

	scm->core_clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(scm->core_clk)) {
		if (PTR_ERR(scm->core_clk) == -EPROBE_DEFER)
			return PTR_ERR(scm->core_clk);

		if (clks & SCM_HAS_CORE_CLK) {
			dev_err(&pdev->dev, "failed to acquire core clk\n");
			return PTR_ERR(scm->core_clk);
		}

		scm->core_clk = NULL;
	}

	scm->iface_clk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(scm->iface_clk)) {
		if (PTR_ERR(scm->iface_clk) == -EPROBE_DEFER)
			return PTR_ERR(scm->iface_clk);

		if (clks & SCM_HAS_IFACE_CLK) {
			dev_err(&pdev->dev, "failed to acquire iface clk\n");
			return PTR_ERR(scm->iface_clk);
		}

		scm->iface_clk = NULL;
	}

	scm->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(scm->bus_clk)) {
		if (PTR_ERR(scm->bus_clk) == -EPROBE_DEFER)
			return PTR_ERR(scm->bus_clk);

		if (clks & SCM_HAS_BUS_CLK) {
			dev_err(&pdev->dev, "failed to acquire bus clk\n");
			return PTR_ERR(scm->bus_clk);
		}

		scm->bus_clk = NULL;
	}

	scm->reset.ops = &qcom_scm_pas_reset_ops;
	scm->reset.nr_resets = 1;
	scm->reset.of_node = pdev->dev.of_node;
	ret = devm_reset_controller_register(&pdev->dev, &scm->reset);
	if (ret)
		return ret;

	/* vote for max clk rate for highest performance */
	ret = clk_set_rate(scm->core_clk, INT_MAX);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	platform_set_drvdata(pdev, scm);

	__scm = scm;
	__scm->dev = &pdev->dev;

	__qcom_scm_init();
	__get_convention();
	ret  = __qcom_multi_smc_init(scm, pdev);
	if (ret)
		return ret;

	scm->restart_nb.notifier_call = qcom_scm_do_restart;
	scm->restart_nb.priority = 130;
	register_restart_handler(&scm->restart_nb);

	/*
	 * If requested enable "download mode", from this point on warmboot
	 * will cause the boot stages to enter download mode, unless
	 * disabled below by a clean shutdown/reboot.
	 */
	if (download_mode)
		qcom_scm_set_download_mode(QCOM_DOWNLOAD_FULLDUMP, 0);

	return 0;
}

static void qcom_scm_shutdown(struct platform_device *pdev)
{
	idr_destroy(&__scm->waitq.idr);
	qcom_scm_disable_sdi();
	qcom_scm_halt_spmi_pmic_arbiter();
	/* Clean shutdown, disable download mode to allow normal restart */
	if (download_mode)
		qcom_scm_set_download_mode(QCOM_DOWNLOAD_NODUMP, 0);
}

static const struct of_device_id qcom_scm_dt_match[] = {
	{ .compatible = "qcom,scm-apq8064",
	  /* FIXME: This should have .data = (void *) SCM_HAS_CORE_CLK */
	},
	{ .compatible = "qcom,scm-apq8084", .data = (void *)(SCM_HAS_CORE_CLK |
							     SCM_HAS_IFACE_CLK |
							     SCM_HAS_BUS_CLK)
	},
	{ .compatible = "qcom,scm-ipq4019" },
	{ .compatible = "qcom,scm-mdm9607", .data = (void *)(SCM_HAS_CORE_CLK |
							     SCM_HAS_IFACE_CLK |
							     SCM_HAS_BUS_CLK) },
	{ .compatible = "qcom,scm-msm8660", .data = (void *) SCM_HAS_CORE_CLK },
	{ .compatible = "qcom,scm-msm8960", .data = (void *) SCM_HAS_CORE_CLK },
	{ .compatible = "qcom,scm-msm8916", .data = (void *)(SCM_HAS_CORE_CLK |
							     SCM_HAS_IFACE_CLK |
							     SCM_HAS_BUS_CLK)
	},
	{ .compatible = "qcom,scm-msm8974", .data = (void *)(SCM_HAS_CORE_CLK |
							     SCM_HAS_IFACE_CLK |
							     SCM_HAS_BUS_CLK)
	},
	{ .compatible = "qcom,scm-msm8976", .data = (void *)(SCM_HAS_CORE_CLK |
							     SCM_HAS_IFACE_CLK |
							     SCM_HAS_BUS_CLK)
	},
	{ .compatible = "qcom,scm-msm8994" },
	{ .compatible = "qcom,scm-msm8996" },
	{ .compatible = "qcom,scm" },
	{ .compatible = "qcom,scm-v1.1" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_scm_dt_match);

static struct platform_driver qcom_scm_driver = {
	.driver = {
		.name	= "qcom_scm",
		.of_match_table = qcom_scm_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = qcom_scm_probe,
	.shutdown = qcom_scm_shutdown,
};

static int __init qcom_scm_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_scm_driver);
	if (ret)
		return ret;

	return qtee_shmbridge_driver_init();
}
subsys_initcall(qcom_scm_init);

#if IS_MODULE(CONFIG_QCOM_SCM)
static void __exit qcom_scm_exit(void)
{
	__qcom_scm_qcpe_exit();
	platform_driver_unregister(&qcom_scm_driver);
	qtee_shmbridge_driver_exit();
}
module_exit(qcom_scm_exit);
#endif

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SCM driver");
MODULE_LICENSE("GPL v2");
