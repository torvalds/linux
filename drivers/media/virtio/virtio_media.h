/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0+ */

/*
 * Virtio-media structures & functions declarations.
 *
 * Copyright (c) 2024-2025 Google LLC.
 */

#ifndef __VIRTIO_MEDIA_H
#define __VIRTIO_MEDIA_H

#include <linux/virtio_config.h>
#include <media/v4l2-device.h>

#include "protocol.h"

#define DESC_CHAIN_MAX_LEN SG_MAX_SINGLE_ALLOC

#define VIRTIO_MEDIA_DEFAULT_DRIVER_NAME "virtio-media"

extern char *virtio_media_driver_name;
extern bool virtio_media_allow_userptr;

/**
 * struct virtio_media - Virtio-media device.
 * @v4l2_dev: v4l2_device for the media device.
 * @video_dev: video_device for the media device.
 * @virtio_dev: virtio device for the media device.
 * @commandq: virtio command queue.
 * @eventq: virtio event queue.
 * @eventq_work: work to run when events are received on @eventq.
 * @mmap_region: region into which MMAP buffers are mapped by the host.
 * @event_buffer: buffer for event descriptors.
 * @sessions: list of active sessions on the device.
 * @sessions_lock: protects @sessions and ``virtio_media_session::list``.
 * @events_lock: prevents concurrent processing of events.
 * @cmd: union of device-related commands.
 * @resp: union of device-related responses.
 * @vlock: serializes access to the command queue.
 * @wq: waitqueue for host responses on the command queue.
 */
struct virtio_media {
	struct v4l2_device v4l2_dev;
	struct video_device video_dev;

	struct virtio_device *virtio_dev;
	struct virtqueue *commandq;
	struct virtqueue *eventq;
	struct work_struct eventq_work;

	struct virtio_shm_region mmap_region;

	void *event_buffer;

	struct list_head sessions;
	struct mutex sessions_lock;

	struct mutex events_lock;

	union {
		struct virtio_media_cmd_open open;
		struct virtio_media_cmd_munmap munmap;
	} cmd;

	union {
		struct virtio_media_resp_open open;
		struct virtio_media_resp_munmap munmap;
	} resp;

	struct mutex vlock;
	wait_queue_head_t wq;
};

static inline struct virtio_media *
to_virtio_media(struct video_device *video_dev)
{
	return container_of(video_dev, struct virtio_media, video_dev);
}

/* virtio_media_driver.c */

int virtio_media_send_command(struct virtio_media *vv, struct scatterlist **sgs,
			      const size_t out_sgs, const size_t in_sgs,
			      size_t minimum_resp_len, size_t *resp_len);
void virtio_media_process_events(struct virtio_media *vv);

/* virtio_media_ioctls.c */

long virtio_media_device_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg);
extern const struct v4l2_ioctl_ops virtio_media_ioctl_ops;

#endif // __VIRTIO_MEDIA_H
