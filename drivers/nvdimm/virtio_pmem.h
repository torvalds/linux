/* SPDX-License-Identifier: GPL-2.0 */
/*
 * virtio_pmem.h: virtio pmem Driver
 *
 * Discovers persistent memory range information
 * from host and provides a virtio based flushing
 * interface.
 **/

#ifndef _LINUX_VIRTIO_PMEM_H
#define _LINUX_VIRTIO_PMEM_H

#include <linux/module.h>
#include <uapi/linux/virtio_pmem.h>
#include <linux/libnvdimm.h>
#include <linux/spinlock.h>

struct virtio_pmem_request {
	struct virtio_pmem_req req;
	struct virtio_pmem_resp resp;

	/* Wait queue to process deferred work after ack from host */
	wait_queue_head_t host_acked;
	bool done;

	/* Wait queue to process deferred work after virt queue buffer avail */
	wait_queue_head_t wq_buf;
	bool wq_buf_avail;
	struct list_head list;
};

struct virtio_pmem {
	struct virtio_device *vdev;

	/* Virtio pmem request queue */
	struct virtqueue *req_vq;

	/* nvdimm bus registers virtio pmem device */
	struct nvdimm_bus *nvdimm_bus;
	struct nvdimm_bus_descriptor nd_desc;

	/* List to store deferred work if virtqueue is full */
	struct list_head req_list;

	/* Synchronize virtqueue data */
	spinlock_t pmem_lock;

	/* Memory region information */
	__u64 start;
	__u64 size;
};

void virtio_pmem_host_ack(struct virtqueue *vq);
int async_pmem_flush(struct nd_region *nd_region, struct bio *bio);
#endif
