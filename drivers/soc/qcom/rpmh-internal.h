/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#ifndef __RPM_INTERNAL_H__
#define __RPM_INTERNAL_H__

#include <linux/bitmap.h>
#include <linux/wait.h>
#include <soc/qcom/tcs.h>

#define MAX_NAME_LENGTH			20

#define CH0				0
#define CH1				1
#define MAX_CHANNEL			2

#define TCS_TYPE_NR			5
#define MAX_CMDS_PER_TCS		16
#define MAX_TCS_PER_TYPE		3
#define MAX_TCS_NR			(MAX_TCS_PER_TYPE * TCS_TYPE_NR)
#define MAX_TCS_SLOTS			(MAX_CMDS_PER_TCS * MAX_TCS_PER_TYPE)

/* CTRLR specific flags */
#define SOLVER_PRESENT			1
#define HW_CHANNEL_PRESENT		2

struct rsc_drv;

/**
 * struct tcs_group: group of Trigger Command Sets (TCS) to send state requests
 * to the controller
 *
 * @drv:       The controller.
 * @type:      Type of the TCS in this group - active, sleep, wake.
 * @mask:      Mask of the TCSes relative to all the TCSes in the RSC.
 * @offset:    Start of the TCS group relative to the TCSes in the RSC.
 * @num_tcs:   Number of TCSes in this type.
 * @ncpt:      Number of commands in each TCS.
 * @req:       Requests that are sent from the TCS; only used for ACTIVE_ONLY
 *             transfers (could be on a wake/sleep TCS if we are borrowing for
 *             an ACTIVE_ONLY transfer).
 *             Start: grab drv->lock, set req, set tcs_in_use, drop drv->lock,
 *                    trigger
 *             End: get irq, access req,
 *                  grab drv->lock, clear tcs_in_use, drop drv->lock
 * @slots:     Indicates which of @cmd_addr are occupied; only used for
 *             SLEEP / WAKE TCSs.  Things are tightly packed in the
 *             case that (ncpt < MAX_CMDS_PER_TCS).  That is if ncpt = 2 and
 *             MAX_CMDS_PER_TCS = 16 then bit[2] = the first bit in 2nd TCS.
 */
struct tcs_group {
	struct rsc_drv *drv;
	int type;
	u32 mask;
	u32 offset;
	int num_tcs;
	int ncpt;
	const struct tcs_request *req[MAX_TCS_PER_TYPE];
	DECLARE_BITMAP(slots, MAX_TCS_SLOTS);
};

/**
 * struct rpmh_request: the message to be sent to rpmh-rsc
 *
 * @msg: the request
 * @cmd: the payload that will be part of the @msg
 * @completion: triggered when request is done
 * @dev: the device making the request
 * @needs_free: check to free dynamically allocated request object
 */
struct rpmh_request {
	struct tcs_request msg;
	struct tcs_cmd cmd[MAX_RPMH_PAYLOAD];
	struct completion *completion;
	const struct device *dev;
	bool needs_free;
};

/**
 * struct rpmh_ctrlr: our representation of the controller
 *
 * @cache: the list of cached requests
 * @cache_lock: synchronize access to the cache data
 * @dirty: was the cache updated since flush
 * @in_solver_mode: Controller is busy in solver mode
 * @flags: Controller specific flags
 * @batch_cache: Cache sleep and wake requests sent as batch
 */
struct rpmh_ctrlr {
	struct list_head cache;
	spinlock_t cache_lock;
	bool dirty;
	bool in_solver_mode;
	u32 flags;
	struct list_head batch_cache;
};

/**
 * struct drv_channel: our representation of the drv channels
 *
 * @tcs:                TCS groups.
 * @drv:                DRV containing the channel
 * @initialized:        Whether channel is initialized
 */
struct drv_channel {
	struct tcs_group tcs[TCS_TYPE_NR];
	struct rsc_drv *drv;
	bool initialized;
};

/**
 * struct rsc_drv_top: our representation of the top RSC device
 *
 * @name:               Controller RSC device name.
 * @drv_count:          No. of DRV controllers in the RSC device
 * @drv:                Controller for each DRV
 * @dev:                RSC top device
 * @list:               RSC device added in rpmh_rsc_dev_list.
 */
struct rsc_drv_top {
	char name[MAX_NAME_LENGTH];
	int drv_count;
	struct rsc_drv *drv;
	struct device *dev;
	struct list_head list;
};

/**
 * struct rsc_drv: the Direct Resource Voter (DRV) of the
 * Resource State Coordinator controller (RSC)
 *
 * @name:               Controller identifier.
 * @base:               Base address of the RSC controller.
 * @tcs_base:           Start address of the TCS registers in this controller.
 * @reg:                Register offsets for RSC controller.
 * @id:                 Instance id in the controller (Direct Resource Voter).
 * @num_tcs:            Number of TCSes in this DRV.
 * @num_channels:       Number of channels in this DRV.
 * @irq:                IRQ at gic.
 * @in_solver_mode:     Controller is busy in solver mode
 * @initialized:        Whether DRV is initialized
 * @rsc_pm:             CPU PM notifier for controller.
 *                      Used when solver mode is not present.
 * @cpus_in_pm:         Number of CPUs not in idle power collapse.
 *                      Used when solver mode is not present.
 * @ch:                 DRV channels.
 * @tcs_in_use:         S/W state of the TCS; only set for ACTIVE_ONLY
 *                      transfers, but might show a sleep/wake TCS in use if
 *                      it was borrowed for an active_only transfer.  You
 *                      must hold the lock in this struct (AKA drv->lock) in
 *                      order to update this.
 * @lock:               Synchronize state of the controller.  If RPMH's cache
 *                      lock will also be held, the order is: drv->lock then
 *                      cache_lock.
 * @tcs_wait:           Wait queue used to wait for @tcs_in_use to free up a
 *                      slot
 * @client:             Handle to the DRV's client.
 * @genpd_nb:           PM Domain notifier
 * @dev:                RSC device
 * @ipc_log_ctx:        IPC logger handle
 * @pdev:               platform device
 */
struct rsc_drv {
	char name[MAX_NAME_LENGTH];
	void __iomem *base;
	void __iomem *tcs_base;
	u32 *regs;
	int id;
	int num_tcs;
	int num_channels;
	int irq;
	bool in_solver_mode;
	bool initialized;
	struct notifier_block rsc_pm;
	atomic_t cpus_in_pm;
	struct drv_channel ch[MAX_CHANNEL];
	DECLARE_BITMAP(tcs_in_use, MAX_TCS_NR);
	spinlock_t lock;
	wait_queue_head_t tcs_wait;
	struct rpmh_ctrlr client;
	struct notifier_block genpd_nb;
	struct device *dev;
	void *ipc_log_ctx;
	struct platform_device *pdev;
};

extern bool rpmh_standalone;

int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg, int ch);
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv,
			     const struct tcs_request *msg,
			     int ch);
void rpmh_rsc_invalidate(struct rsc_drv *drv, int ch);
void rpmh_rsc_debug(struct rsc_drv *drv, struct completion *compl);
void rpmh_rsc_debug_channel_busy(struct rsc_drv *drv);
int rpmh_rsc_mode_solver_set(struct rsc_drv *drv, bool enable);
int rpmh_rsc_get_channel(struct rsc_drv *drv);
int rpmh_rsc_switch_channel(struct rsc_drv *drv, int ch);
int rpmh_rsc_drv_enable(struct rsc_drv *drv, bool enable);
const struct device *rpmh_rsc_get_device(const char *name, u32 drv_id);

void rpmh_tx_done(const struct tcs_request *msg);
int rpmh_flush(struct rpmh_ctrlr *ctrlr, int ch);
int _rpmh_flush(struct rpmh_ctrlr *ctrlr, int ch);

int rpmh_rsc_init_fast_path(struct rsc_drv *drv, const struct tcs_request *msg, int ch);
int rpmh_rsc_update_fast_path(struct rsc_drv *drv,
			      const struct tcs_request *msg,
			      u32 update_mask, int ch);

#endif /* __RPM_INTERNAL_H__ */
