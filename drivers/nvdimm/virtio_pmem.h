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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <uapi/linux/virtio_pmem.h>
#include <linux/kref.h>
#include <linux/libnvdimm.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct virtio_pmem_request {
	struct kref kref;

	/* Wait queue to process deferred work after ack from host */
	wait_queue_head_t host_acked;
	bool done;

	/* Wait queue to process deferred work after virt queue buffer avail */
	wait_queue_head_t wq_buf;
	bool wq_buf_avail;
	struct list_head list;

	struct virtio_pmem_req req;
	__dma_from_device_group_begin(resp);
	struct virtio_pmem_resp resp;
	__dma_from_device_group_end(resp);
};

struct virtio_pmem {
	struct virtio_device *vdev;

	/* Virtio pmem request queue */
	struct virtqueue *req_vq;

	/* Serialize flush requests to the device. */
	struct mutex flush_lock;

	/* Complete asynchronous FUA flushes outside the submit path. */
	struct workqueue_struct *flush_wq;

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
