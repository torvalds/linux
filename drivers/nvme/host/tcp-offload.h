/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

/* Linux includes */
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/nvme-tcp.h>

/* Driver includes */
#include "nvme.h"
#include "fabrics.h"

/* Forward declarations */
struct nvme_tcp_ofld_ops;

/* Representation of a vendor-specific device. This is the struct used to
 * register to the offload layer by the vendor-specific driver during its probe
 * function.
 * Allocated by vendor-specific driver.
 */
struct nvme_tcp_ofld_dev {
	struct list_head entry;
	struct net_device *ndev;
	struct nvme_tcp_ofld_ops *ops;

	/* Vendor specific driver context */
	int num_hw_vectors;
};

/* Per IO struct holding the nvme_request and command
 * Allocated by blk-mq.
 */
struct nvme_tcp_ofld_req {
	struct nvme_request req;
	struct nvme_command nvme_cmd;
	struct list_head queue_entry;
	struct nvme_tcp_ofld_queue *queue;

	/* Vendor specific driver context */
	void *private_data;

	/* async flag is used to distinguish between async and IO flow
	 * in common send_req() of nvme_tcp_ofld_ops.
	 */
	bool async;

	void (*done)(struct nvme_tcp_ofld_req *req,
		     union nvme_result *result,
		     __le16 status);
};

enum nvme_tcp_ofld_queue_flags {
	NVME_TCP_OFLD_Q_ALLOCATED = 0,
	NVME_TCP_OFLD_Q_LIVE = 1,
};

/* Allocated by nvme_tcp_ofld */
struct nvme_tcp_ofld_queue {
	/* Offload device associated to this queue */
	struct nvme_tcp_ofld_dev *dev;
	struct nvme_tcp_ofld_ctrl *ctrl;
	unsigned long flags;
	size_t cmnd_capsule_len;

	/* mutex used during stop_queue */
	struct mutex queue_lock;

	u8 hdr_digest;
	u8 data_digest;
	u8 tos;

	/* Vendor specific driver context */
	void *private_data;

	/* Error callback function */
	int (*report_err)(struct nvme_tcp_ofld_queue *queue);
};

/* Connectivity (routing) params used for establishing a connection */
struct nvme_tcp_ofld_ctrl_con_params {
	struct sockaddr_storage remote_ip_addr;

	/* If NVMF_OPT_HOST_TRADDR is provided it will be set in local_ip_addr
	 * in nvme_tcp_ofld_create_ctrl().
	 * If NVMF_OPT_HOST_TRADDR is not provided the local_ip_addr will be
	 * initialized by claim_dev().
	 */
	struct sockaddr_storage local_ip_addr;
};

/* Allocated by nvme_tcp_ofld */
struct nvme_tcp_ofld_ctrl {
	struct nvme_ctrl nctrl;
	struct list_head list;
	struct nvme_tcp_ofld_dev *dev;

	/* admin and IO queues */
	struct blk_mq_tag_set tag_set;
	struct blk_mq_tag_set admin_tag_set;
	struct nvme_tcp_ofld_queue *queues;

	struct work_struct err_work;
	struct delayed_work connect_work;

	/*
	 * Each entry in the array indicates the number of queues of
	 * corresponding type.
	 */
	u32 io_queues[HCTX_MAX_TYPES];

	/* Connectivity params */
	struct nvme_tcp_ofld_ctrl_con_params conn_params;

	struct nvme_tcp_ofld_req async_req;

	/* Vendor specific driver context */
	void *private_data;
};

struct nvme_tcp_ofld_ops {
	const char *name;
	struct module *module;

	/* For vendor-specific driver to report what opts it supports.
	 * It could be different than the ULP supported opts due to hardware
	 * limitations. Also it could be different among different vendor
	 * drivers.
	 */
	int required_opts; /* bitmap using enum nvmf_parsing_opts */
	int allowed_opts; /* bitmap using enum nvmf_parsing_opts */

	/* For vendor-specific max num of segments and IO sizes */
	u32 max_hw_sectors;
	u32 max_segments;

	/**
	 * claim_dev: Return True if addr is reachable via offload device.
	 * @dev: The offload device to check.
	 * @ctrl: The offload ctrl have the conn_params field. The
	 * conn_params is to be filled with routing params by the lower
	 * driver.
	 */
	int (*claim_dev)(struct nvme_tcp_ofld_dev *dev,
			 struct nvme_tcp_ofld_ctrl *ctrl);

	/**
	 * setup_ctrl: Setup device specific controller structures.
	 * @ctrl: The offload ctrl.
	 */
	int (*setup_ctrl)(struct nvme_tcp_ofld_ctrl *ctrl);

	/**
	 * release_ctrl: Release/Free device specific controller structures.
	 * @ctrl: The offload ctrl.
	 */
	int (*release_ctrl)(struct nvme_tcp_ofld_ctrl *ctrl);

	/**
	 * create_queue: Create offload queue and establish TCP + NVMeTCP
	 * (icreq+icresp) connection. Return true on successful connection.
	 * Based on nvme_tcp_alloc_queue.
	 * @queue: The queue itself - used as input and output.
	 * @qid: The queue ID associated with the requested queue.
	 * @q_size: The queue depth.
	 */
	int (*create_queue)(struct nvme_tcp_ofld_queue *queue, int qid,
			    size_t queue_size);

	/**
	 * drain_queue: Drain a given queue - blocking function call.
	 * Return from this function ensures that no additional
	 * completions will arrive on this queue and that the HW will
	 * not access host memory.
	 * @queue: The queue to drain.
	 */
	void (*drain_queue)(struct nvme_tcp_ofld_queue *queue);

	/**
	 * destroy_queue: Close the TCP + NVMeTCP connection of a given queue
	 * and make sure its no longer active (no completions will arrive on the
	 * queue).
	 * @queue: The queue to destroy.
	 */
	void (*destroy_queue)(struct nvme_tcp_ofld_queue *queue);

	/**
	 * poll_queue: Poll a given queue for completions.
	 * @queue: The queue to poll.
	 */
	int (*poll_queue)(struct nvme_tcp_ofld_queue *queue);

	/**
	 * send_req: Dispatch a request. Returns the execution status.
	 * @req: Ptr to request to be sent.
	 */
	int (*send_req)(struct nvme_tcp_ofld_req *req);
};

/* Exported functions for lower vendor specific offload drivers */
int nvme_tcp_ofld_register_dev(struct nvme_tcp_ofld_dev *dev);
void nvme_tcp_ofld_unregister_dev(struct nvme_tcp_ofld_dev *dev);
void nvme_tcp_ofld_error_recovery(struct nvme_ctrl *nctrl);
inline size_t nvme_tcp_ofld_inline_data_size(struct nvme_tcp_ofld_queue *queue);
