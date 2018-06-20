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
 * @drv:       the controller
 * @type:      type of the TCS in this group - active, sleep, wake
 * @mask:      mask of the TCSes relative to all the TCSes in the RSC
 * @offset:    start of the TCS group relative to the TCSes in the RSC
 * @num_tcs:   number of TCSes in this type
 * @ncpt:      number of commands in each TCS
 * @lock:      lock for synchronizing this TCS writes
 * @req:       requests that are sent from the TCS
 * @cmd_cache: flattened cache of cmds in sleep/wake TCS
 * @slots:     indicates which of @cmd_addr are occupied
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
	u32 *cmd_cache;
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
 */
struct rpmh_ctrlr {
	struct list_head cache;
	spinlock_t cache_lock;
	bool dirty;
};

/**
 * struct rsc_drv: the Direct Resource Voter (DRV) of the
 * Resource State Coordinator controller (RSC)
 *
 * @name:       controller identifier
 * @tcs_base:   start address of the TCS registers in this controller
 * @id:         instance id in the controller (Direct Resource Voter)
 * @num_tcs:    number of TCSes in this DRV
 * @tcs:        TCS groups
 * @tcs_in_use: s/w state of the TCS
 * @lock:       synchronize state of the controller
 * @client:     handle to the DRV's client.
 */
struct rsc_drv {
	const char *name;
	void __iomem *tcs_base;
	int id;
	int num_tcs;
	struct tcs_group tcs[TCS_TYPE_NR];
	DECLARE_BITMAP(tcs_in_use, MAX_TCS_NR);
	spinlock_t lock;
	struct rpmh_ctrlr client;
};

int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg);
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv,
			     const struct tcs_request *msg);
int rpmh_rsc_invalidate(struct rsc_drv *drv);

void rpmh_tx_done(const struct tcs_request *msg, int r);

#endif /* __RPM_INTERNAL_H__ */
