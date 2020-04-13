/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */


#ifndef __RPM_INTERNAL_H__
#define __RPM_INTERNAL_H__

#include <linux/bitmap.h>
#include <soc/qcom/tcs.h>

#define TCS_TYPE_NR			4
#define MAX_CMDS_PER_TCS		16
#define MAX_TCS_PER_TYPE		3
#define MAX_TCS_NR			(MAX_TCS_PER_TYPE * TCS_TYPE_NR)
#define MAX_TCS_SLOTS			(MAX_CMDS_PER_TCS * MAX_TCS_PER_TYPE)

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
 * @lock:      Lock for synchronizing this TCS writes.
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
	spinlock_t lock;
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
 * @err: err return from the controller
 * @needs_free: check to free dynamically allocated request object
 */
struct rpmh_request {
	struct tcs_request msg;
	struct tcs_cmd cmd[MAX_RPMH_PAYLOAD];
	struct completion *completion;
	const struct device *dev;
	int err;
	bool needs_free;
};

/**
 * struct rpmh_ctrlr: our representation of the controller
 *
 * @cache: the list of cached requests
 * @cache_lock: synchronize access to the cache data
 * @dirty: was the cache updated since flush
 * @batch_cache: Cache sleep and wake requests sent as batch
 */
struct rpmh_ctrlr {
	struct list_head cache;
	spinlock_t cache_lock;
	bool dirty;
	struct list_head batch_cache;
};

/**
 * struct rsc_drv: the Direct Resource Voter (DRV) of the
 * Resource State Coordinator controller (RSC)
 *
 * @name:               Controller identifier.
 * @tcs_base:           Start address of the TCS registers in this controller.
 * @id:                 Instance id in the controller (Direct Resource Voter).
 * @num_tcs:            Number of TCSes in this DRV.
 * @rsc_pm:             CPU PM notifier for controller.
 *                      Used when solver mode is not present.
 * @cpus_entered_pm:    CPU mask for cpus in idle power collapse.
 *                      Used when solver mode is not present.
 * @tcs:                TCS groups.
 * @tcs_in_use:         S/W state of the TCS; only set for ACTIVE_ONLY
 *                      transfers, but might show a sleep/wake TCS in use if
 *                      it was borrowed for an active_only transfer.  You
 *                      must hold both the lock in this struct and the
 *                      tcs_lock for the TCS in order to mark a TCS as
 *                      in-use, but you only need the lock in this structure
 *                      (aka the drv->lock) to mark one freed.
 * @lock:               Synchronize state of the controller.  If you will be
 *                      grabbing this lock and a tcs_lock at the same time,
 *                      grab the tcs_lock first so we always have a
 *                      consistent lock ordering.
 * @pm_lock:            Synchronize during PM notifications.
 *                      Used when solver mode is not present.
 * @client:             Handle to the DRV's client.
 */
struct rsc_drv {
	const char *name;
	void __iomem *tcs_base;
	int id;
	int num_tcs;
	struct notifier_block rsc_pm;
	struct cpumask cpus_entered_pm;
	struct tcs_group tcs[TCS_TYPE_NR];
	DECLARE_BITMAP(tcs_in_use, MAX_TCS_NR);
	spinlock_t lock;
	spinlock_t pm_lock;
	struct rpmh_ctrlr client;
};

int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg);
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv,
			     const struct tcs_request *msg);
void rpmh_rsc_invalidate(struct rsc_drv *drv);

void rpmh_tx_done(const struct tcs_request *msg, int r);
int rpmh_flush(struct rpmh_ctrlr *ctrlr);

#endif /* __RPM_INTERNAL_H__ */
