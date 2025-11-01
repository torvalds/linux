// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI K3 R5F (MCU) Remote Processor driver
 *
 * Copyright (C) 2017-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_platform.h>
#include <linux/omap-mailbox.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"
#include "ti_sci_proc.h"
#include "ti_k3_common.h"

/* This address can either be for ATCM or BTCM with the other at address 0x0 */
#define K3_R5_TCM_DEV_ADDR	0x41010000

/* R5 TI-SCI Processor Configuration Flags */
#define PROC_BOOT_CFG_FLAG_R5_DBG_EN			0x00000001
#define PROC_BOOT_CFG_FLAG_R5_DBG_NIDEN			0x00000002
#define PROC_BOOT_CFG_FLAG_R5_LOCKSTEP			0x00000100
#define PROC_BOOT_CFG_FLAG_R5_TEINIT			0x00000200
#define PROC_BOOT_CFG_FLAG_R5_NMFI_EN			0x00000400
#define PROC_BOOT_CFG_FLAG_R5_TCM_RSTBASE		0x00000800
#define PROC_BOOT_CFG_FLAG_R5_BTCM_EN			0x00001000
#define PROC_BOOT_CFG_FLAG_R5_ATCM_EN			0x00002000
/* Available from J7200 SoCs onwards */
#define PROC_BOOT_CFG_FLAG_R5_MEM_INIT_DIS		0x00004000
/* Applicable to only AM64x SoCs */
#define PROC_BOOT_CFG_FLAG_R5_SINGLE_CORE		0x00008000

/* R5 TI-SCI Processor Control Flags */
#define PROC_BOOT_CTRL_FLAG_R5_CORE_HALT		0x00000001

/* R5 TI-SCI Processor Status Flags */
#define PROC_BOOT_STATUS_FLAG_R5_WFE			0x00000001
#define PROC_BOOT_STATUS_FLAG_R5_WFI			0x00000002
#define PROC_BOOT_STATUS_FLAG_R5_CLK_GATED		0x00000004
#define PROC_BOOT_STATUS_FLAG_R5_LOCKSTEP_PERMITTED	0x00000100
/* Applicable to only AM64x SoCs */
#define PROC_BOOT_STATUS_FLAG_R5_SINGLECORE_ONLY	0x00000200

/*
 * All cluster mode values are not applicable on all SoCs. The following
 * are the modes supported on various SoCs:
 *   Split mode       : AM65x, J721E, J7200 and AM64x SoCs
 *   LockStep mode    : AM65x, J721E and J7200 SoCs
 *   Single-CPU mode  : AM64x SoCs only
 *   Single-Core mode : AM62x, AM62A SoCs
 */
enum cluster_mode {
	CLUSTER_MODE_SPLIT = 0,
	CLUSTER_MODE_LOCKSTEP,
	CLUSTER_MODE_SINGLECPU,
	CLUSTER_MODE_SINGLECORE
};

/**
 * struct k3_r5_soc_data - match data to handle SoC variations
 * @tcm_is_double: flag to denote the larger unified TCMs in certain modes
 * @tcm_ecc_autoinit: flag to denote the auto-initialization of TCMs for ECC
 * @single_cpu_mode: flag to denote if SoC/IP supports Single-CPU mode
 * @is_single_core: flag to denote if SoC/IP has only single core R5
 * @core_data: pointer to R5-core-specific device data
 */
struct k3_r5_soc_data {
	bool tcm_is_double;
	bool tcm_ecc_autoinit;
	bool single_cpu_mode;
	bool is_single_core;
	const struct k3_rproc_dev_data *core_data;
};

/**
 * struct k3_r5_cluster - K3 R5F Cluster structure
 * @dev: cached device pointer
 * @mode: Mode to configure the Cluster - Split or LockStep
 * @cores: list of R5 cores within the cluster
 * @core_transition: wait queue to sync core state changes
 * @soc_data: SoC-specific feature data for a R5FSS
 */
struct k3_r5_cluster {
	struct device *dev;
	enum cluster_mode mode;
	struct list_head cores;
	wait_queue_head_t core_transition;
	const struct k3_r5_soc_data *soc_data;
};

/**
 * struct k3_r5_core - K3 R5 core structure
 * @elem: linked list item
 * @dev: cached device pointer
 * @kproc: K3 rproc handle representing this core
 * @cluster: cached pointer to parent cluster structure
 * @sram: on-chip SRAM memory regions data
 * @num_sram: number of on-chip SRAM memory regions
 * @atcm_enable: flag to control ATCM enablement
 * @btcm_enable: flag to control BTCM enablement
 * @loczrama: flag to dictate which TCM is at device address 0x0
 * @released_from_reset: flag to signal when core is out of reset
 */
struct k3_r5_core {
	struct list_head elem;
	struct device *dev;
	struct k3_rproc *kproc;
	struct k3_r5_cluster *cluster;
	struct k3_rproc_mem *sram;
	int num_sram;
	u32 atcm_enable;
	u32 btcm_enable;
	u32 loczrama;
	bool released_from_reset;
};

static int k3_r5_split_reset(struct k3_rproc *kproc)
{
	int ret;

	ret = reset_control_assert(kproc->reset);
	if (ret) {
		dev_err(kproc->dev, "local-reset assert failed, ret = %d\n",
			ret);
		return ret;
	}

	ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(kproc->dev, "module-reset assert failed, ret = %d\n",
			ret);
		if (reset_control_deassert(kproc->reset))
			dev_warn(kproc->dev, "local-reset deassert back failed\n");
	}

	return ret;
}

static int k3_r5_split_release(struct k3_rproc *kproc)
{
	int ret;

	ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(kproc->dev, "module-reset deassert failed, ret = %d\n",
			ret);
		return ret;
	}

	ret = reset_control_deassert(kproc->reset);
	if (ret) {
		dev_err(kproc->dev, "local-reset deassert failed, ret = %d\n",
			ret);
		if (kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							  kproc->ti_sci_id))
			dev_warn(kproc->dev, "module-reset assert back failed\n");
	}

	return ret;
}

static int k3_r5_lockstep_reset(struct k3_r5_cluster *cluster)
{
	struct k3_r5_core *core;
	struct k3_rproc *kproc;
	int ret;

	/* assert local reset on all applicable cores */
	list_for_each_entry(core, &cluster->cores, elem) {
		ret = reset_control_assert(core->kproc->reset);
		if (ret) {
			dev_err(core->dev, "local-reset assert failed, ret = %d\n",
				ret);
			core = list_prev_entry(core, elem);
			goto unroll_local_reset;
		}
	}

	/* disable PSC modules on all applicable cores */
	list_for_each_entry(core, &cluster->cores, elem) {
		kproc = core->kproc;
		ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret) {
			dev_err(core->dev, "module-reset assert failed, ret = %d\n",
				ret);
			goto unroll_module_reset;
		}
	}

	return 0;

unroll_module_reset:
	list_for_each_entry_continue_reverse(core, &cluster->cores, elem) {
		kproc = core->kproc;
		if (kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							  kproc->ti_sci_id))
			dev_warn(core->dev, "module-reset assert back failed\n");
	}
	core = list_last_entry(&cluster->cores, struct k3_r5_core, elem);
unroll_local_reset:
	list_for_each_entry_from_reverse(core, &cluster->cores, elem) {
		if (reset_control_deassert(core->kproc->reset))
			dev_warn(core->dev, "local-reset deassert back failed\n");
	}

	return ret;
}

static int k3_r5_lockstep_release(struct k3_r5_cluster *cluster)
{
	struct k3_r5_core *core;
	struct k3_rproc *kproc;
	int ret;

	/* enable PSC modules on all applicable cores */
	list_for_each_entry_reverse(core, &cluster->cores, elem) {
		kproc = core->kproc;
		ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret) {
			dev_err(core->dev, "module-reset deassert failed, ret = %d\n",
				ret);
			core = list_next_entry(core, elem);
			goto unroll_module_reset;
		}
	}

	/* deassert local reset on all applicable cores */
	list_for_each_entry_reverse(core, &cluster->cores, elem) {
		ret = reset_control_deassert(core->kproc->reset);
		if (ret) {
			dev_err(core->dev, "module-reset deassert failed, ret = %d\n",
				ret);
			goto unroll_local_reset;
		}
	}

	return 0;

unroll_local_reset:
	list_for_each_entry_continue(core, &cluster->cores, elem) {
		if (reset_control_assert(core->kproc->reset))
			dev_warn(core->dev, "local-reset assert back failed\n");
	}
	core = list_first_entry(&cluster->cores, struct k3_r5_core, elem);
unroll_module_reset:
	list_for_each_entry_from(core, &cluster->cores, elem) {
		kproc = core->kproc;
		if (kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							  kproc->ti_sci_id))
			dev_warn(core->dev, "module-reset assert back failed\n");
	}

	return ret;
}

static inline int k3_r5_core_halt(struct k3_rproc *kproc)
{
	return ti_sci_proc_set_control(kproc->tsp,
				       PROC_BOOT_CTRL_FLAG_R5_CORE_HALT, 0);
}

static inline int k3_r5_core_run(struct k3_rproc *kproc)
{
	return ti_sci_proc_set_control(kproc->tsp,
				       0, PROC_BOOT_CTRL_FLAG_R5_CORE_HALT);
}

/*
 * The R5F cores have controls for both a reset and a halt/run. The code
 * execution from DDR requires the initial boot-strapping code to be run
 * from the internal TCMs. This function is used to release the resets on
 * applicable cores to allow loading into the TCMs. The .prepare() ops is
 * invoked by remoteproc core before any firmware loading, and is followed
 * by the .start() ops after loading to actually let the R5 cores run.
 *
 * The Single-CPU mode on applicable SoCs (eg: AM64x) only uses Core0 to
 * execute code, but combines the TCMs from both cores. The resets for both
 * cores need to be released to make this possible, as the TCMs are in general
 * private to each core. Only Core0 needs to be unhalted for running the
 * cluster in this mode. The function uses the same reset logic as LockStep
 * mode for this (though the behavior is agnostic of the reset release order).
 * This callback is invoked only in remoteproc mode.
 */
static int k3_r5_rproc_prepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct k3_r5_core *core = kproc->priv, *core0, *core1;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *dev = kproc->dev;
	u32 ctrl = 0, cfg = 0, stat = 0;
	u64 boot_vec = 0;
	bool mem_init_dis;
	int ret;

	/*
	 * R5 cores require to be powered on sequentially, core0 should be in
	 * higher power state than core1 in a cluster. So, wait for core0 to
	 * power up before proceeding to core1 and put timeout of 2sec. This
	 * waiting mechanism is necessary because rproc_auto_boot_callback() for
	 * core1 can be called before core0 due to thread execution order.
	 *
	 * By placing the wait mechanism here in .prepare() ops, this condition
	 * is enforced for rproc boot requests from sysfs as well.
	 */
	core0 = list_first_entry(&cluster->cores, struct k3_r5_core, elem);
	core1 = list_last_entry(&cluster->cores, struct k3_r5_core, elem);
	if (cluster->mode == CLUSTER_MODE_SPLIT && core == core1 &&
	    !core0->released_from_reset) {
		ret = wait_event_interruptible_timeout(cluster->core_transition,
						       core0->released_from_reset,
						       msecs_to_jiffies(2000));
		if (ret <= 0) {
			dev_err(dev, "can not power up core1 before core0");
			return -EPERM;
		}
	}

	ret = ti_sci_proc_get_status(kproc->tsp, &boot_vec, &cfg, &ctrl, &stat);
	if (ret < 0)
		return ret;
	mem_init_dis = !!(cfg & PROC_BOOT_CFG_FLAG_R5_MEM_INIT_DIS);

	/* Re-use LockStep-mode reset logic for Single-CPU mode */
	ret = (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
	       cluster->mode == CLUSTER_MODE_SINGLECPU) ?
		k3_r5_lockstep_release(cluster) : k3_r5_split_release(kproc);
	if (ret) {
		dev_err(dev, "unable to enable cores for TCM loading, ret = %d\n",
			ret);
		return ret;
	}

	/*
	 * Notify all threads in the wait queue when core0 state has changed so
	 * that threads waiting for this condition can be executed.
	 */
	core->released_from_reset = true;
	if (core == core0)
		wake_up_interruptible(&cluster->core_transition);

	/*
	 * Newer IP revisions like on J7200 SoCs support h/w auto-initialization
	 * of TCMs, so there is no need to perform the s/w memzero. This bit is
	 * configurable through System Firmware, the default value does perform
	 * auto-init, but account for it in case it is disabled
	 */
	if (cluster->soc_data->tcm_ecc_autoinit && !mem_init_dis) {
		dev_dbg(dev, "leveraging h/w init for TCM memories\n");
		return 0;
	}

	/*
	 * Zero out both TCMs unconditionally (access from v8 Arm core is not
	 * affected by ATCM & BTCM enable configuration values) so that ECC
	 * can be effective on all TCM addresses.
	 */
	dev_dbg(dev, "zeroing out ATCM memory\n");
	memset_io(kproc->mem[0].cpu_addr, 0x00, kproc->mem[0].size);

	dev_dbg(dev, "zeroing out BTCM memory\n");
	memset_io(kproc->mem[1].cpu_addr, 0x00, kproc->mem[1].size);

	return 0;
}

/*
 * This function implements the .unprepare() ops and performs the complimentary
 * operations to that of the .prepare() ops. The function is used to assert the
 * resets on all applicable cores for the rproc device (depending on LockStep
 * or Split mode). This completes the second portion of powering down the R5F
 * cores. The cores themselves are only halted in the .stop() ops, and the
 * .unprepare() ops is invoked by the remoteproc core after the remoteproc is
 * stopped.
 *
 * The Single-CPU mode on applicable SoCs (eg: AM64x) combines the TCMs from
 * both cores. The access is made possible only with releasing the resets for
 * both cores, but with only Core0 unhalted. This function re-uses the same
 * reset assert logic as LockStep mode for this mode (though the behavior is
 * agnostic of the reset assert order). This callback is invoked only in
 * remoteproc mode.
 */
static int k3_r5_rproc_unprepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct k3_r5_core *core = kproc->priv, *core0, *core1;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *dev = kproc->dev;
	int ret;

	/*
	 * Ensure power-down of cores is sequential in split mode. Core1 must
	 * power down before Core0 to maintain the expected state. By placing
	 * the wait mechanism here in .unprepare() ops, this condition is
	 * enforced for rproc stop or shutdown requests from sysfs and device
	 * removal as well.
	 */
	core0 = list_first_entry(&cluster->cores, struct k3_r5_core, elem);
	core1 = list_last_entry(&cluster->cores, struct k3_r5_core, elem);
	if (cluster->mode == CLUSTER_MODE_SPLIT && core == core0 &&
	    core1->released_from_reset) {
		ret = wait_event_interruptible_timeout(cluster->core_transition,
						       !core1->released_from_reset,
						       msecs_to_jiffies(2000));
		if (ret <= 0) {
			dev_err(dev, "can not power down core0 before core1");
			return -EPERM;
		}
	}

	/* Re-use LockStep-mode reset logic for Single-CPU mode */
	ret = (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
	       cluster->mode == CLUSTER_MODE_SINGLECPU) ?
		k3_r5_lockstep_reset(cluster) : k3_r5_split_reset(kproc);
	if (ret)
		dev_err(dev, "unable to disable cores, ret = %d\n", ret);

	/*
	 * Notify all threads in the wait queue when core1 state has changed so
	 * that threads waiting for this condition can be executed.
	 */
	core->released_from_reset = false;
	if (core == core1)
		wake_up_interruptible(&cluster->core_transition);

	return ret;
}

/*
 * The R5F start sequence includes two different operations
 * 1. Configure the boot vector for R5F core(s)
 * 2. Unhalt/Run the R5F core(s)
 *
 * The sequence is different between LockStep and Split modes. The LockStep
 * mode requires the boot vector to be configured only for Core0, and then
 * unhalt both the cores to start the execution - Core1 needs to be unhalted
 * first followed by Core0. The Split-mode requires that Core0 to be maintained
 * always in a higher power state that Core1 (implying Core1 needs to be started
 * always only after Core0 is started).
 *
 * The Single-CPU mode on applicable SoCs (eg: AM64x) only uses Core0 to execute
 * code, so only Core0 needs to be unhalted. The function uses the same logic
 * flow as Split-mode for this. This callback is invoked only in remoteproc
 * mode.
 */
static int k3_r5_rproc_start(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct k3_r5_core *core = kproc->priv;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *dev = kproc->dev;
	u32 boot_addr;
	int ret;

	boot_addr = rproc->bootaddr;
	/* TODO: add boot_addr sanity checking */
	dev_dbg(dev, "booting R5F core using boot addr = 0x%x\n", boot_addr);

	/* boot vector need not be programmed for Core1 in LockStep mode */
	ret = ti_sci_proc_set_config(kproc->tsp, boot_addr, 0, 0);
	if (ret)
		return ret;

	/* unhalt/run all applicable cores */
	if (cluster->mode == CLUSTER_MODE_LOCKSTEP) {
		list_for_each_entry_reverse(core, &cluster->cores, elem) {
			ret = k3_r5_core_run(core->kproc);
			if (ret)
				goto unroll_core_run;
		}
	} else {
		ret = k3_r5_core_run(core->kproc);
		if (ret)
			return ret;
	}

	return 0;

unroll_core_run:
	list_for_each_entry_continue(core, &cluster->cores, elem) {
		if (k3_r5_core_halt(core->kproc))
			dev_warn(core->dev, "core halt back failed\n");
	}
	return ret;
}

/*
 * The R5F stop function includes the following operations
 * 1. Halt R5F core(s)
 *
 * The sequence is different between LockStep and Split modes, and the order
 * of cores the operations are performed are also in general reverse to that
 * of the start function. The LockStep mode requires each operation to be
 * performed first on Core0 followed by Core1. The Split-mode requires that
 * Core0 to be maintained always in a higher power state that Core1 (implying
 * Core1 needs to be stopped first before Core0).
 *
 * The Single-CPU mode on applicable SoCs (eg: AM64x) only uses Core0 to execute
 * code, so only Core0 needs to be halted. The function uses the same logic
 * flow as Split-mode for this.
 *
 * Note that the R5F halt operation in general is not effective when the R5F
 * core is running, but is needed to make sure the core won't run after
 * deasserting the reset the subsequent time. The asserting of reset can
 * be done here, but is preferred to be done in the .unprepare() ops - this
 * maintains the symmetric behavior between the .start(), .stop(), .prepare()
 * and .unprepare() ops, and also balances them well between sysfs 'state'
 * flow and device bind/unbind or module removal. This callback is invoked
 * only in remoteproc mode.
 */
static int k3_r5_rproc_stop(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct k3_r5_core *core = kproc->priv;
	struct k3_r5_cluster *cluster = core->cluster;
	int ret;

	/* halt all applicable cores */
	if (cluster->mode == CLUSTER_MODE_LOCKSTEP) {
		list_for_each_entry(core, &cluster->cores, elem) {
			ret = k3_r5_core_halt(core->kproc);
			if (ret) {
				core = list_prev_entry(core, elem);
				goto unroll_core_halt;
			}
		}
	} else {
		ret = k3_r5_core_halt(core->kproc);
		if (ret)
			goto out;
	}

	return 0;

unroll_core_halt:
	list_for_each_entry_from_reverse(core, &cluster->cores, elem) {
		if (k3_r5_core_run(core->kproc))
			dev_warn(core->dev, "core run back failed\n");
	}
out:
	return ret;
}

/*
 * Internal Memory translation helper
 *
 * Custom function implementing the rproc .da_to_va ops to provide address
 * translation (device address to kernel virtual address) for internal RAMs
 * present in a DSP or IPU device). The translated addresses can be used
 * either by the remoteproc core for loading, or by any rpmsg bus drivers.
 */
static void *k3_r5_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct k3_rproc *kproc = rproc->priv;
	struct k3_r5_core *core = kproc->priv;
	void __iomem *va = NULL;
	u32 dev_addr, offset;
	size_t size;
	int i;

	if (len == 0)
		return NULL;

	/* handle any SRAM regions using SoC-view addresses */
	for (i = 0; i < core->num_sram; i++) {
		dev_addr = core->sram[i].dev_addr;
		size = core->sram[i].size;

		if (da >= dev_addr && ((da + len) <= (dev_addr + size))) {
			offset = da - dev_addr;
			va = core->sram[i].cpu_addr + offset;
			return (__force void *)va;
		}
	}

	/* handle both TCM and DDR memory regions */
	return k3_rproc_da_to_va(rproc, da, len, is_iomem);
}

static const struct rproc_ops k3_r5_rproc_ops = {
	.prepare	= k3_r5_rproc_prepare,
	.unprepare	= k3_r5_rproc_unprepare,
	.start		= k3_r5_rproc_start,
	.stop		= k3_r5_rproc_stop,
	.kick		= k3_rproc_kick,
	.da_to_va	= k3_r5_rproc_da_to_va,
};

/*
 * Internal R5F Core configuration
 *
 * Each R5FSS has a cluster-level setting for configuring the processor
 * subsystem either in a safety/fault-tolerant LockStep mode or a performance
 * oriented Split mode on most SoCs. A fewer SoCs support a non-safety mode
 * as an alternate for LockStep mode that exercises only a single R5F core
 * called Single-CPU mode. Each R5F core has a number of settings to either
 * enable/disable each of the TCMs, control which TCM appears at the R5F core's
 * address 0x0. These settings need to be configured before the resets for the
 * corresponding core are released. These settings are all protected and managed
 * by the System Processor.
 *
 * This function is used to pre-configure these settings for each R5F core, and
 * the configuration is all done through various ti_sci_proc functions that
 * communicate with the System Processor. The function also ensures that both
 * the cores are halted before the .prepare() step.
 *
 * The function is called from k3_r5_cluster_rproc_init() and is invoked either
 * once (in LockStep mode or Single-CPU modes) or twice (in Split mode). Support
 * for LockStep-mode is dictated by an eFUSE register bit, and the config
 * settings retrieved from DT are adjusted accordingly as per the permitted
 * cluster mode. Another eFUSE register bit dictates if the R5F cluster only
 * supports a Single-CPU mode. All cluster level settings like Cluster mode and
 * TEINIT (exception handling state dictating ARM or Thumb mode) can only be set
 * and retrieved using Core0.
 *
 * The function behavior is different based on the cluster mode. The R5F cores
 * are configured independently as per their individual settings in Split mode.
 * They are identically configured in LockStep mode using the primary Core0
 * settings. However, some individual settings cannot be set in LockStep mode.
 * This is overcome by switching to Split-mode initially and then programming
 * both the cores with the same settings, before reconfiguing again for
 * LockStep mode.
 */
static int k3_r5_rproc_configure(struct k3_rproc *kproc)
{
	struct k3_r5_core *temp, *core0, *core = kproc->priv;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *dev = kproc->dev;
	u32 ctrl = 0, cfg = 0, stat = 0;
	u32 set_cfg = 0, clr_cfg = 0;
	u64 boot_vec = 0;
	bool lockstep_en;
	bool single_cpu;
	int ret;

	core0 = list_first_entry(&cluster->cores, struct k3_r5_core, elem);
	if (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
	    cluster->mode == CLUSTER_MODE_SINGLECPU ||
	    cluster->mode == CLUSTER_MODE_SINGLECORE) {
		core = core0;
	} else {
		core = kproc->priv;
	}

	ret = ti_sci_proc_get_status(core->kproc->tsp, &boot_vec, &cfg, &ctrl,
				     &stat);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "boot_vector = 0x%llx, cfg = 0x%x ctrl = 0x%x stat = 0x%x\n",
		boot_vec, cfg, ctrl, stat);

	single_cpu = !!(stat & PROC_BOOT_STATUS_FLAG_R5_SINGLECORE_ONLY);
	lockstep_en = !!(stat & PROC_BOOT_STATUS_FLAG_R5_LOCKSTEP_PERMITTED);

	/* Override to single CPU mode if set in status flag */
	if (single_cpu && cluster->mode == CLUSTER_MODE_SPLIT) {
		dev_err(cluster->dev, "split-mode not permitted, force configuring for single-cpu mode\n");
		cluster->mode = CLUSTER_MODE_SINGLECPU;
	}

	/* Override to split mode if lockstep enable bit is not set in status flag */
	if (!lockstep_en && cluster->mode == CLUSTER_MODE_LOCKSTEP) {
		dev_err(cluster->dev, "lockstep mode not permitted, force configuring for split-mode\n");
		cluster->mode = CLUSTER_MODE_SPLIT;
	}

	/* always enable ARM mode and set boot vector to 0 */
	boot_vec = 0x0;
	if (core == core0) {
		clr_cfg = PROC_BOOT_CFG_FLAG_R5_TEINIT;
		/*
		 * Single-CPU configuration bit can only be configured
		 * on Core0 and system firmware will NACK any requests
		 * with the bit configured, so program it only on
		 * permitted cores
		 */
		if (cluster->mode == CLUSTER_MODE_SINGLECPU ||
		    cluster->mode == CLUSTER_MODE_SINGLECORE) {
			set_cfg = PROC_BOOT_CFG_FLAG_R5_SINGLE_CORE;
		} else {
			/*
			 * LockStep configuration bit is Read-only on Split-mode
			 * _only_ devices and system firmware will NACK any
			 * requests with the bit configured, so program it only
			 * on permitted devices
			 */
			if (lockstep_en)
				clr_cfg |= PROC_BOOT_CFG_FLAG_R5_LOCKSTEP;
		}
	}

	if (core->atcm_enable)
		set_cfg |= PROC_BOOT_CFG_FLAG_R5_ATCM_EN;
	else
		clr_cfg |= PROC_BOOT_CFG_FLAG_R5_ATCM_EN;

	if (core->btcm_enable)
		set_cfg |= PROC_BOOT_CFG_FLAG_R5_BTCM_EN;
	else
		clr_cfg |= PROC_BOOT_CFG_FLAG_R5_BTCM_EN;

	if (core->loczrama)
		set_cfg |= PROC_BOOT_CFG_FLAG_R5_TCM_RSTBASE;
	else
		clr_cfg |= PROC_BOOT_CFG_FLAG_R5_TCM_RSTBASE;

	if (cluster->mode == CLUSTER_MODE_LOCKSTEP) {
		/*
		 * work around system firmware limitations to make sure both
		 * cores are programmed symmetrically in LockStep. LockStep
		 * and TEINIT config is only allowed with Core0.
		 */
		list_for_each_entry(temp, &cluster->cores, elem) {
			ret = k3_r5_core_halt(temp->kproc);
			if (ret)
				goto out;

			if (temp != core) {
				clr_cfg &= ~PROC_BOOT_CFG_FLAG_R5_LOCKSTEP;
				clr_cfg &= ~PROC_BOOT_CFG_FLAG_R5_TEINIT;
			}
			ret = ti_sci_proc_set_config(temp->kproc->tsp, boot_vec,
						     set_cfg, clr_cfg);
			if (ret)
				goto out;
		}

		set_cfg = PROC_BOOT_CFG_FLAG_R5_LOCKSTEP;
		clr_cfg = 0;
		ret = ti_sci_proc_set_config(core->kproc->tsp, boot_vec,
					     set_cfg, clr_cfg);
	} else {
		ret = k3_r5_core_halt(core->kproc);
		if (ret)
			goto out;

		ret = ti_sci_proc_set_config(core->kproc->tsp, boot_vec,
					     set_cfg, clr_cfg);
	}

out:
	return ret;
}

/*
 * Each R5F core within a typical R5FSS instance has a total of 64 KB of TCMs,
 * split equally into two 32 KB banks between ATCM and BTCM. The TCMs from both
 * cores are usable in Split-mode, but only the Core0 TCMs can be used in
 * LockStep-mode. The newer revisions of the R5FSS IP maximizes these TCMs by
 * leveraging the Core1 TCMs as well in certain modes where they would have
 * otherwise been unusable (Eg: LockStep-mode on J7200 SoCs, Single-CPU mode on
 * AM64x SoCs). This is done by making a Core1 TCM visible immediately after the
 * corresponding Core0 TCM. The SoC memory map uses the larger 64 KB sizes for
 * the Core0 TCMs, and the dts representation reflects this increased size on
 * supported SoCs. The Core0 TCM sizes therefore have to be adjusted to only
 * half the original size in Split mode.
 */
static void k3_r5_adjust_tcm_sizes(struct k3_rproc *kproc)
{
	struct k3_r5_core *core0, *core = kproc->priv;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *cdev = core->dev;

	if (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
	    cluster->mode == CLUSTER_MODE_SINGLECPU ||
	    cluster->mode == CLUSTER_MODE_SINGLECORE ||
	    !cluster->soc_data->tcm_is_double)
		return;

	core0 = list_first_entry(&cluster->cores, struct k3_r5_core, elem);
	if (core == core0) {
		WARN_ON(kproc->mem[0].size != SZ_64K);
		WARN_ON(kproc->mem[1].size != SZ_64K);

		kproc->mem[0].size /= 2;
		kproc->mem[1].size /= 2;

		dev_dbg(cdev, "adjusted TCM sizes, ATCM = 0x%zx BTCM = 0x%zx\n",
			kproc->mem[0].size, kproc->mem[1].size);
	}
}

/*
 * This function checks and configures a R5F core for IPC-only or remoteproc
 * mode. The driver is configured to be in IPC-only mode for a R5F core when
 * the core has been loaded and started by a bootloader. The IPC-only mode is
 * detected by querying the System Firmware for reset, power on and halt status
 * and ensuring that the core is running. Any incomplete steps at bootloader
 * are validated and errored out.
 *
 * In IPC-only mode, the driver state flags for ATCM, BTCM and LOCZRAMA settings
 * and cluster mode parsed originally from kernel DT are updated to reflect the
 * actual values configured by bootloader. The driver internal device memory
 * addresses for TCMs are also updated.
 */
static int k3_r5_rproc_configure_mode(struct k3_rproc *kproc)
{
	struct k3_r5_core *core0, *core = kproc->priv;
	struct k3_r5_cluster *cluster = core->cluster;
	struct device *cdev = core->dev;
	bool r_state = false, c_state = false, lockstep_en = false, single_cpu = false;
	u32 ctrl = 0, cfg = 0, stat = 0, halted = 0;
	u64 boot_vec = 0;
	u32 atcm_enable, btcm_enable, loczrama;
	enum cluster_mode mode = cluster->mode;
	int reset_ctrl_status;
	int ret;

	core0 = list_first_entry(&cluster->cores, struct k3_r5_core, elem);

	ret = kproc->ti_sci->ops.dev_ops.is_on(kproc->ti_sci, kproc->ti_sci_id,
					       &r_state, &c_state);
	if (ret) {
		dev_err(cdev, "failed to get initial state, mode cannot be determined, ret = %d\n",
			ret);
		return ret;
	}
	if (r_state != c_state) {
		dev_warn(cdev, "R5F core may have been powered on by a different host, programmed state (%d) != actual state (%d)\n",
			 r_state, c_state);
	}

	reset_ctrl_status = reset_control_status(kproc->reset);
	if (reset_ctrl_status < 0) {
		dev_err(cdev, "failed to get initial local reset status, ret = %d\n",
			reset_ctrl_status);
		return reset_ctrl_status;
	}

	/*
	 * Skip the waiting mechanism for sequential power-on of cores if the
	 * core has already been booted by another entity.
	 */
	core->released_from_reset = c_state;

	ret = ti_sci_proc_get_status(kproc->tsp, &boot_vec, &cfg, &ctrl,
				     &stat);
	if (ret < 0) {
		dev_err(cdev, "failed to get initial processor status, ret = %d\n",
			ret);
		return ret;
	}
	atcm_enable = cfg & PROC_BOOT_CFG_FLAG_R5_ATCM_EN ?  1 : 0;
	btcm_enable = cfg & PROC_BOOT_CFG_FLAG_R5_BTCM_EN ?  1 : 0;
	loczrama = cfg & PROC_BOOT_CFG_FLAG_R5_TCM_RSTBASE ?  1 : 0;
	single_cpu = cfg & PROC_BOOT_CFG_FLAG_R5_SINGLE_CORE ? 1 : 0;
	lockstep_en = cfg & PROC_BOOT_CFG_FLAG_R5_LOCKSTEP ? 1 : 0;

	if (single_cpu && mode != CLUSTER_MODE_SINGLECORE)
		mode = CLUSTER_MODE_SINGLECPU;
	if (lockstep_en)
		mode = CLUSTER_MODE_LOCKSTEP;

	halted = ctrl & PROC_BOOT_CTRL_FLAG_R5_CORE_HALT;

	/*
	 * IPC-only mode detection requires both local and module resets to
	 * be deasserted and R5F core to be unhalted. Local reset status is
	 * irrelevant if module reset is asserted (POR value has local reset
	 * deasserted), and is deemed as remoteproc mode
	 */
	if (c_state && !reset_ctrl_status && !halted) {
		dev_info(cdev, "configured R5F for IPC-only mode\n");
		kproc->rproc->state = RPROC_DETACHED;
		ret = 1;
		/* override rproc ops with only required IPC-only mode ops */
		kproc->rproc->ops->prepare = NULL;
		kproc->rproc->ops->unprepare = NULL;
		kproc->rproc->ops->start = NULL;
		kproc->rproc->ops->stop = NULL;
		kproc->rproc->ops->attach = k3_rproc_attach;
		kproc->rproc->ops->detach = k3_rproc_detach;
		kproc->rproc->ops->get_loaded_rsc_table =
						k3_get_loaded_rsc_table;
	} else if (!c_state) {
		dev_info(cdev, "configured R5F for remoteproc mode\n");
		ret = 0;
	} else {
		dev_err(cdev, "mismatched mode: local_reset = %s, module_reset = %s, core_state = %s\n",
			!reset_ctrl_status ? "deasserted" : "asserted",
			c_state ? "deasserted" : "asserted",
			halted ? "halted" : "unhalted");
		ret = -EINVAL;
	}

	/* fixup TCMs, cluster & core flags to actual values in IPC-only mode */
	if (ret > 0) {
		if (core == core0)
			cluster->mode = mode;
		core->atcm_enable = atcm_enable;
		core->btcm_enable = btcm_enable;
		core->loczrama = loczrama;
		kproc->mem[0].dev_addr = loczrama ? 0 : K3_R5_TCM_DEV_ADDR;
		kproc->mem[1].dev_addr = loczrama ? K3_R5_TCM_DEV_ADDR : 0;
	}

	return ret;
}

static int k3_r5_core_of_get_internal_memories(struct platform_device *pdev,
					       struct k3_rproc *kproc)
{
	const struct k3_rproc_dev_data *data = kproc->data;
	struct device *dev = &pdev->dev;
	struct k3_r5_core *core = kproc->priv;
	int num_mems;
	int i, ret;

	num_mems = data->num_mems;
	kproc->mem = devm_kcalloc(kproc->dev, num_mems, sizeof(*kproc->mem),
				  GFP_KERNEL);
	if (!kproc->mem)
		return -ENOMEM;

	ret = k3_rproc_of_get_memories(pdev, kproc);
	if (ret)
		return ret;

	for (i = 0; i < num_mems; i++) {
		/*
		 * TODO:
		 * The R5F cores can place ATCM & BTCM anywhere in its address
		 * based on the corresponding Region Registers in the System
		 * Control coprocessor. For now, place ATCM and BTCM at
		 * addresses 0 and 0x41010000 (same as the bus address on AM65x
		 * SoCs) based on loczrama setting overriding default assignment
		 * done by k3_rproc_of_get_memories().
		 */
		if (!strcmp(data->mems[i].name, "atcm")) {
			kproc->mem[i].dev_addr = core->loczrama ?
							0 : K3_R5_TCM_DEV_ADDR;
		} else {
			kproc->mem[i].dev_addr = core->loczrama ?
							K3_R5_TCM_DEV_ADDR : 0;
		}

		dev_dbg(dev, "Updating bus addr %pa of memory %5s\n",
			&kproc->mem[i].bus_addr, data->mems[i].name);
	}

	return 0;
}

static int k3_r5_core_of_get_sram_memories(struct platform_device *pdev,
					   struct k3_r5_core *core)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct device_node *sram_np;
	struct resource res;
	int num_sram;
	int i, ret;

	num_sram = of_property_count_elems_of_size(np, "sram", sizeof(phandle));
	if (num_sram <= 0) {
		dev_dbg(dev, "device does not use reserved on-chip memories, num_sram = %d\n",
			num_sram);
		return 0;
	}

	core->sram = devm_kcalloc(dev, num_sram, sizeof(*core->sram), GFP_KERNEL);
	if (!core->sram)
		return -ENOMEM;

	for (i = 0; i < num_sram; i++) {
		sram_np = of_parse_phandle(np, "sram", i);
		if (!sram_np)
			return -EINVAL;

		if (!of_device_is_available(sram_np)) {
			of_node_put(sram_np);
			return -EINVAL;
		}

		ret = of_address_to_resource(sram_np, 0, &res);
		of_node_put(sram_np);
		if (ret)
			return -EINVAL;

		core->sram[i].bus_addr = res.start;
		core->sram[i].dev_addr = res.start;
		core->sram[i].size = resource_size(&res);
		core->sram[i].cpu_addr = devm_ioremap_wc(dev, res.start,
							 resource_size(&res));
		if (!core->sram[i].cpu_addr) {
			dev_err(dev, "failed to parse and map sram%d memory at %pad\n",
				i, &res.start);
			return -ENOMEM;
		}

		dev_dbg(dev, "memory sram%d: bus addr %pa size 0x%zx va %p da 0x%x\n",
			i, &core->sram[i].bus_addr,
			core->sram[i].size, core->sram[i].cpu_addr,
			core->sram[i].dev_addr);
	}
	core->num_sram = num_sram;

	return 0;
}

static int k3_r5_cluster_rproc_init(struct platform_device *pdev)
{
	struct k3_r5_cluster *cluster = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct k3_rproc *kproc;
	struct k3_r5_core *core, *core1;
	struct device_node *np;
	struct device *cdev;
	const char *fw_name;
	struct rproc *rproc;
	int ret, ret1;

	core1 = list_last_entry(&cluster->cores, struct k3_r5_core, elem);
	list_for_each_entry(core, &cluster->cores, elem) {
		cdev = core->dev;
		np = dev_of_node(cdev);
		ret = rproc_of_parse_firmware(cdev, 0, &fw_name);
		if (ret) {
			dev_err(dev, "failed to parse firmware-name property, ret = %d\n",
				ret);
			goto out;
		}

		rproc = devm_rproc_alloc(cdev, dev_name(cdev), &k3_r5_rproc_ops,
					 fw_name, sizeof(*kproc));
		if (!rproc) {
			ret = -ENOMEM;
			goto out;
		}

		/* K3 R5s have a Region Address Translator (RAT) but no MMU */
		rproc->has_iommu = false;
		/* error recovery is not supported at present */
		rproc->recovery_disabled = true;

		kproc = rproc->priv;
		kproc->priv = core;
		kproc->dev = cdev;
		kproc->rproc = rproc;
		kproc->data = cluster->soc_data->core_data;
		core->kproc = kproc;

		kproc->ti_sci = devm_ti_sci_get_by_phandle(cdev, "ti,sci");
		if (IS_ERR(kproc->ti_sci)) {
			ret = dev_err_probe(cdev, PTR_ERR(kproc->ti_sci),
					    "failed to get ti-sci handle\n");
			kproc->ti_sci = NULL;
			goto out;
		}

		ret = of_property_read_u32(np, "ti,sci-dev-id", &kproc->ti_sci_id);
		if (ret) {
			dev_err(cdev, "missing 'ti,sci-dev-id' property\n");
			goto out;
		}

		kproc->reset = devm_reset_control_get_exclusive(cdev, NULL);
		if (IS_ERR_OR_NULL(kproc->reset)) {
			ret = PTR_ERR_OR_ZERO(kproc->reset);
			if (!ret)
				ret = -ENODEV;
			dev_err_probe(cdev, ret, "failed to get reset handle\n");
			goto out;
		}

		kproc->tsp = ti_sci_proc_of_get_tsp(cdev, kproc->ti_sci);
		if (IS_ERR(kproc->tsp)) {
			ret = dev_err_probe(cdev, PTR_ERR(kproc->tsp),
					    "failed to construct ti-sci proc control\n");
			goto out;
		}

		ret = k3_r5_core_of_get_internal_memories(to_platform_device(cdev), kproc);
		if (ret) {
			dev_err(cdev, "failed to get internal memories, ret = %d\n",
				ret);
			goto out;
		}

		ret = ti_sci_proc_request(kproc->tsp);
		if (ret < 0) {
			dev_err(cdev, "ti_sci_proc_request failed, ret = %d\n", ret);
			goto out;
		}

		ret = devm_add_action_or_reset(cdev, k3_release_tsp, kproc->tsp);
		if (ret)
			goto out;
	}

	list_for_each_entry(core, &cluster->cores, elem) {
		cdev = core->dev;
		kproc = core->kproc;
		rproc = kproc->rproc;

		ret = k3_rproc_request_mbox(rproc);
		if (ret)
			return ret;

		ret = k3_r5_rproc_configure_mode(kproc);
		if (ret < 0)
			goto out;
		if (ret)
			goto init_rmem;

		ret = k3_r5_rproc_configure(kproc);
		if (ret) {
			dev_err(cdev, "initial configure failed, ret = %d\n",
				ret);
			goto out;
		}

init_rmem:
		k3_r5_adjust_tcm_sizes(kproc);

		ret = k3_reserved_mem_init(kproc);
		if (ret) {
			dev_err(cdev, "reserved memory init failed, ret = %d\n",
				ret);
			goto out;
		}

		ret = devm_rproc_add(cdev, rproc);
		if (ret) {
			dev_err_probe(cdev, ret, "rproc_add failed\n");
			goto out;
		}

		/* create only one rproc in lockstep, single-cpu or
		 * single core mode
		 */
		if (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
		    cluster->mode == CLUSTER_MODE_SINGLECPU ||
		    cluster->mode == CLUSTER_MODE_SINGLECORE)
			break;
	}

	return 0;

err_split:
	if (rproc->state == RPROC_ATTACHED) {
		ret1 = rproc_detach(rproc);
		if (ret1) {
			dev_err(kproc->dev, "failed to detach rproc, ret = %d\n",
				ret1);
			return ret1;
		}
	}

out:
	/* undo core0 upon any failures on core1 in split-mode */
	if (cluster->mode == CLUSTER_MODE_SPLIT && core == core1) {
		core = list_prev_entry(core, elem);
		kproc = core->kproc;
		rproc = kproc->rproc;
		goto err_split;
	}
	return ret;
}

static void k3_r5_cluster_rproc_exit(void *data)
{
	struct k3_r5_cluster *cluster = platform_get_drvdata(data);
	struct k3_rproc *kproc;
	struct k3_r5_core *core;
	struct rproc *rproc;
	int ret;

	/*
	 * lockstep mode and single-cpu modes have only one rproc associated
	 * with first core, whereas split-mode has two rprocs associated with
	 * each core, and requires that core1 be powered down first
	 */
	core = (cluster->mode == CLUSTER_MODE_LOCKSTEP ||
		cluster->mode == CLUSTER_MODE_SINGLECPU) ?
		list_first_entry(&cluster->cores, struct k3_r5_core, elem) :
		list_last_entry(&cluster->cores, struct k3_r5_core, elem);

	list_for_each_entry_from_reverse(core, &cluster->cores, elem) {
		kproc = core->kproc;
		rproc = kproc->rproc;

		if (rproc->state == RPROC_ATTACHED) {
			ret = rproc_detach(rproc);
			if (ret) {
				dev_err(kproc->dev, "failed to detach rproc, ret = %d\n", ret);
				return;
			}
		}
	}
}

static int k3_r5_core_of_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct k3_r5_core *core;
	int ret;

	if (!devres_open_group(dev, k3_r5_core_of_init, GFP_KERNEL))
		return -ENOMEM;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core) {
		ret = -ENOMEM;
		goto err;
	}

	core->dev = dev;
	/*
	 * Use SoC Power-on-Reset values as default if no DT properties are
	 * used to dictate the TCM configurations
	 */
	core->atcm_enable = 0;
	core->btcm_enable = 1;
	core->loczrama = 1;

	ret = of_property_read_u32(np, "ti,atcm-enable", &core->atcm_enable);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "invalid format for ti,atcm-enable, ret = %d\n",
			ret);
		goto err;
	}

	ret = of_property_read_u32(np, "ti,btcm-enable", &core->btcm_enable);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "invalid format for ti,btcm-enable, ret = %d\n",
			ret);
		goto err;
	}

	ret = of_property_read_u32(np, "ti,loczrama", &core->loczrama);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "invalid format for ti,loczrama, ret = %d\n", ret);
		goto err;
	}

	ret = k3_r5_core_of_get_sram_memories(pdev, core);
	if (ret) {
		dev_err(dev, "failed to get sram memories, ret = %d\n", ret);
		goto err;
	}

	platform_set_drvdata(pdev, core);
	devres_close_group(dev, k3_r5_core_of_init);

	return 0;

err:
	devres_release_group(dev, k3_r5_core_of_init);
	return ret;
}

/*
 * free the resources explicitly since driver model is not being used
 * for the child R5F devices
 */
static void k3_r5_core_of_exit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	platform_set_drvdata(pdev, NULL);
	devres_release_group(dev, k3_r5_core_of_init);
}

static void k3_r5_cluster_of_exit(void *data)
{
	struct k3_r5_cluster *cluster = platform_get_drvdata(data);
	struct platform_device *cpdev;
	struct k3_r5_core *core, *temp;

	list_for_each_entry_safe_reverse(core, temp, &cluster->cores, elem) {
		list_del(&core->elem);
		cpdev = to_platform_device(core->dev);
		k3_r5_core_of_exit(cpdev);
	}
}

static int k3_r5_cluster_of_init(struct platform_device *pdev)
{
	struct k3_r5_cluster *cluster = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct platform_device *cpdev;
	struct k3_r5_core *core;
	int ret;

	for_each_available_child_of_node_scoped(np, child) {
		cpdev = of_find_device_by_node(child);
		if (!cpdev) {
			ret = -ENODEV;
			dev_err(dev, "could not get R5 core platform device\n");
			goto fail;
		}

		ret = k3_r5_core_of_init(cpdev);
		if (ret) {
			dev_err(dev, "k3_r5_core_of_init failed, ret = %d\n",
				ret);
			put_device(&cpdev->dev);
			goto fail;
		}

		core = platform_get_drvdata(cpdev);
		core->cluster = cluster;
		put_device(&cpdev->dev);
		list_add_tail(&core->elem, &cluster->cores);
	}

	return 0;

fail:
	k3_r5_cluster_of_exit(pdev);
	return ret;
}

static int k3_r5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct k3_r5_cluster *cluster;
	const struct k3_r5_soc_data *data;
	int ret;
	int num_cores;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(dev, "SoC-specific data is not defined\n");
		return -ENODEV;
	}

	cluster = devm_kzalloc(dev, sizeof(*cluster), GFP_KERNEL);
	if (!cluster)
		return -ENOMEM;

	cluster->dev = dev;
	cluster->soc_data = data;
	INIT_LIST_HEAD(&cluster->cores);
	init_waitqueue_head(&cluster->core_transition);

	ret = of_property_read_u32(np, "ti,cluster-mode", &cluster->mode);
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(dev, ret, "invalid format for ti,cluster-mode\n");

	if (ret == -EINVAL) {
		/*
		 * default to most common efuse configurations - Split-mode on AM64x
		 * and LockStep-mode on all others
		 * default to most common efuse configurations -
		 * Split-mode on AM64x
		 * Single core on AM62x
		 * LockStep-mode on all others
		 */
		if (!data->is_single_core)
			cluster->mode = data->single_cpu_mode ?
					CLUSTER_MODE_SPLIT : CLUSTER_MODE_LOCKSTEP;
		else
			cluster->mode = CLUSTER_MODE_SINGLECORE;
	}

	if  ((cluster->mode == CLUSTER_MODE_SINGLECPU && !data->single_cpu_mode) ||
	     (cluster->mode == CLUSTER_MODE_SINGLECORE && !data->is_single_core))
		return dev_err_probe(dev, -EINVAL,
				     "Cluster mode = %d is not supported on this SoC\n",
				     cluster->mode);

	num_cores = of_get_available_child_count(np);
	if (num_cores != 2 && !data->is_single_core)
		return dev_err_probe(dev, -ENODEV,
				     "MCU cluster requires both R5F cores to be enabled but num_cores is set to = %d\n",
				     num_cores);

	if (num_cores != 1 && data->is_single_core)
		return dev_err_probe(dev, -ENODEV,
				     "SoC supports only single core R5 but num_cores is set to %d\n",
				     num_cores);

	platform_set_drvdata(pdev, cluster);

	ret = devm_of_platform_populate(dev);
	if (ret)
		return dev_err_probe(dev, ret, "devm_of_platform_populate failed\n");

	ret = k3_r5_cluster_of_init(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "k3_r5_cluster_of_init failed\n");

	ret = devm_add_action_or_reset(dev, k3_r5_cluster_of_exit, pdev);
	if (ret)
		return ret;

	ret = k3_r5_cluster_rproc_init(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "k3_r5_cluster_rproc_init failed\n");

	ret = devm_add_action_or_reset(dev, k3_r5_cluster_rproc_exit, pdev);
	if (ret)
		return ret;

	return 0;
}

static const struct k3_rproc_mem_data r5_mems[] = {
	{ .name = "atcm", .dev_addr = 0x0 },
	{ .name = "btcm", .dev_addr = K3_R5_TCM_DEV_ADDR },
};

static const struct k3_rproc_dev_data r5_data = {
	.mems = r5_mems,
	.num_mems = ARRAY_SIZE(r5_mems),
	.boot_align_addr = 0,
	.uses_lreset = true,
};

static const struct k3_r5_soc_data am65_j721e_soc_data = {
	.tcm_is_double = false,
	.tcm_ecc_autoinit = false,
	.single_cpu_mode = false,
	.is_single_core = false,
	.core_data = &r5_data,
};

static const struct k3_r5_soc_data j7200_j721s2_soc_data = {
	.tcm_is_double = true,
	.tcm_ecc_autoinit = true,
	.single_cpu_mode = false,
	.is_single_core = false,
	.core_data = &r5_data,
};

static const struct k3_r5_soc_data am64_soc_data = {
	.tcm_is_double = true,
	.tcm_ecc_autoinit = true,
	.single_cpu_mode = true,
	.is_single_core = false,
	.core_data = &r5_data,
};

static const struct k3_r5_soc_data am62_soc_data = {
	.tcm_is_double = false,
	.tcm_ecc_autoinit = true,
	.single_cpu_mode = false,
	.is_single_core = true,
	.core_data = &r5_data,
};

static const struct of_device_id k3_r5_of_match[] = {
	{ .compatible = "ti,am654-r5fss", .data = &am65_j721e_soc_data, },
	{ .compatible = "ti,j721e-r5fss", .data = &am65_j721e_soc_data, },
	{ .compatible = "ti,j7200-r5fss", .data = &j7200_j721s2_soc_data, },
	{ .compatible = "ti,am64-r5fss",  .data = &am64_soc_data, },
	{ .compatible = "ti,am62-r5fss",  .data = &am62_soc_data, },
	{ .compatible = "ti,j721s2-r5fss",  .data = &j7200_j721s2_soc_data, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, k3_r5_of_match);

static struct platform_driver k3_r5_rproc_driver = {
	.probe = k3_r5_probe,
	.driver = {
		.name = "k3_r5_rproc",
		.of_match_table = k3_r5_of_match,
	},
};

module_platform_driver(k3_r5_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI K3 R5F remote processor driver");
MODULE_AUTHOR("Suman Anna <s-anna@ti.com>");
