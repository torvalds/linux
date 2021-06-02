// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
/* Kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>

/* Driver includes */
#include "tcp-offload.h"

static LIST_HEAD(nvme_tcp_ofld_devices);
static DEFINE_MUTEX(nvme_tcp_ofld_devices_mutex);

/**
 * nvme_tcp_ofld_register_dev() - NVMeTCP Offload Library registration
 * function.
 * @dev:	NVMeTCP offload device instance to be registered to the
 *		common tcp offload instance.
 *
 * API function that registers the type of vendor specific driver
 * being implemented to the common NVMe over TCP offload library. Part of
 * the overall init sequence of starting up an offload driver.
 */
int nvme_tcp_ofld_register_dev(struct nvme_tcp_ofld_dev *dev)
{
	struct nvme_tcp_ofld_ops *ops = dev->ops;

	if (!ops->claim_dev ||
	    !ops->setup_ctrl ||
	    !ops->release_ctrl ||
	    !ops->create_queue ||
	    !ops->drain_queue ||
	    !ops->destroy_queue ||
	    !ops->poll_queue ||
	    !ops->send_req)
		return -EINVAL;

	mutex_lock(&nvme_tcp_ofld_devices_mutex);
	list_add_tail(&dev->entry, &nvme_tcp_ofld_devices);
	mutex_unlock(&nvme_tcp_ofld_devices_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_register_dev);

/**
 * nvme_tcp_ofld_unregister_dev() - NVMeTCP Offload Library unregistration
 * function.
 * @dev:	NVMeTCP offload device instance to be unregistered from the
 *		common tcp offload instance.
 *
 * API function that unregisters the type of vendor specific driver being
 * implemented from the common NVMe over TCP offload library.
 * Part of the overall exit sequence of unloading the implemented driver.
 */
void nvme_tcp_ofld_unregister_dev(struct nvme_tcp_ofld_dev *dev)
{
	mutex_lock(&nvme_tcp_ofld_devices_mutex);
	list_del(&dev->entry);
	mutex_unlock(&nvme_tcp_ofld_devices_mutex);
}
EXPORT_SYMBOL_GPL(nvme_tcp_ofld_unregister_dev);

/**
 * nvme_tcp_ofld_report_queue_err() - NVMeTCP Offload report error event
 * callback function. Pointed to by nvme_tcp_ofld_queue->report_err.
 * @queue:	NVMeTCP offload queue instance on which the error has occurred.
 *
 * API function that allows the vendor specific offload driver to reports errors
 * to the common offload layer, to invoke error recovery.
 */
int nvme_tcp_ofld_report_queue_err(struct nvme_tcp_ofld_queue *queue)
{
	/* Placeholder - invoke error recovery flow */

	return 0;
}

/**
 * nvme_tcp_ofld_req_done() - NVMeTCP Offload request done callback
 * function. Pointed to by nvme_tcp_ofld_req->done.
 * Handles both NVME_TCP_F_DATA_SUCCESS flag and NVMe CQ.
 * @req:	NVMeTCP offload request to complete.
 * @result:     The nvme_result.
 * @status:     The completion status.
 *
 * API function that allows the vendor specific offload driver to report request
 * completions to the common offload layer.
 */
void nvme_tcp_ofld_req_done(struct nvme_tcp_ofld_req *req,
			    union nvme_result *result,
			    __le16 status)
{
	/* Placeholder - complete request with/without error */
}

static struct nvmf_transport_ops nvme_tcp_ofld_transport = {
	.name		= "tcp_offload",
	.module		= THIS_MODULE,
	.required_opts	= NVMF_OPT_TRADDR,
	.allowed_opts	= NVMF_OPT_TRSVCID | NVMF_OPT_NR_WRITE_QUEUES  |
			  NVMF_OPT_HOST_TRADDR | NVMF_OPT_CTRL_LOSS_TMO |
			  NVMF_OPT_RECONNECT_DELAY | NVMF_OPT_HDR_DIGEST |
			  NVMF_OPT_DATA_DIGEST | NVMF_OPT_NR_POLL_QUEUES |
			  NVMF_OPT_TOS,
};

static int __init nvme_tcp_ofld_init_module(void)
{
	nvmf_register_transport(&nvme_tcp_ofld_transport);

	return 0;
}

static void __exit nvme_tcp_ofld_cleanup_module(void)
{
	nvmf_unregister_transport(&nvme_tcp_ofld_transport);
}

module_init(nvme_tcp_ofld_init_module);
module_exit(nvme_tcp_ofld_cleanup_module);
MODULE_LICENSE("GPL v2");
