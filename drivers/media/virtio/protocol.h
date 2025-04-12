/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0+ */

/*
 * Definitions of virtio-media protocol structures.
 *
 * Copyright (c) 2024-2025 Google LLC.
 */

#ifndef __VIRTIO_MEDIA_PROTOCOL_H
#define __VIRTIO_MEDIA_PROTOCOL_H

#include <linux/videodev2.h>

/*
 * Virtio protocol definition.
 */

/**
 * struct virtio_media_cmd_header - Header for all virtio-media commands.
 * @cmd: one of VIRTIO_MEDIA_CMD_*.
 * @__reserved: must be set to zero by the driver.
 *
 * This header starts all commands from the driver to the device on the
 * commandq.
 */
struct virtio_media_cmd_header {
	u32 cmd;
	u32 __reserved;
};

/**
 * struct virtio_media_resp_header - Header for all virtio-media responses.
 * @status: 0 if the command was successful, or one of the standard Linux error
 * codes.
 * @__reserved: must be set to zero by the device.
 *
 * This header starts all responses from the device to the driver on the
 * commandq.
 */
struct virtio_media_resp_header {
	u32 status;
	u32 __reserved;
};

/**
 * VIRTIO_MEDIA_CMD_OPEN - Command for creating a new session.
 *
 * This is the equivalent of calling `open` on a V4L2 device node. Upon
 * success, a session id is returned which can be used to perform other
 * commands on the session, notably ioctls.
 */
#define VIRTIO_MEDIA_CMD_OPEN 1

/**
 * struct virtio_media_cmd_open - Driver command for VIRTIO_MEDIA_CMD_OPEN.
 * @hdr: header with cmd member set to VIRTIO_MEDIA_CMD_OPEN.
 */
struct virtio_media_cmd_open {
	struct virtio_media_cmd_header hdr;
};

/**
 * struct virtio_media_resp_open - Device response for VIRTIO_MEDIA_CMD_OPEN.
 * @hdr: header containing the status of the command.
 * @session_id: if hdr.status == 0, contains the id of the newly created session.
 * @__reserved: must be set to zero by the device.
 */
struct virtio_media_resp_open {
	struct virtio_media_resp_header hdr;
	u32 session_id;
	u32 __reserved;
};

/**
 * VIRTIO_MEDIA_CMD_CLOSE - Command for closing an active session.
 *
 * This is the equivalent of calling `close` on a previously opened V4L2
 * session. All resources associated with this session will be freed and the
 * session ID shall not be used again after queueing this command.
 *
 * This command does not require a response from the device.
 */
#define VIRTIO_MEDIA_CMD_CLOSE 2

/**
 * struct virtio_media_cmd_close - Driver command for VIRTIO_MEDIA_CMD_CLOSE.
 * @hdr: header with cmd member set to VIRTIO_MEDIA_CMD_CLOSE.
 * @session_id: id of the session to close.
 * @__reserved: must be set to zero by the driver.
 */
struct virtio_media_cmd_close {
	struct virtio_media_cmd_header hdr;
	u32 session_id;
	u32 __reserved;
};

/**
 * VIRTIO_MEDIA_CMD_IOCTL - Driver command for executing an ioctl.
 *
 * This command asks the device to run one of the `VIDIOC_*` ioctls on the
 * active session.
 *
 * The code of the ioctl is extracted from the VIDIOC_* definitions in
 * `videodev2.h`, and consists of the second argument of the `_IO*` macro.
 *
 * Each ioctl has a payload, which is defined by the third argument of the
 * `_IO*` macro defining it. It can be writable by the driver (`_IOW`), the
 * device (`_IOR`), or both (`_IOWR`).
 *
 * If an ioctl is writable by the driver, it must be followed by a
 * driver-writable descriptor containing the payload.
 *
 * If an ioctl is writable by the device, it must be followed by a
 * device-writable descriptor of the size of the payload that the device will
 * write into.
 *
 */
#define VIRTIO_MEDIA_CMD_IOCTL 3

/**
 * struct virtio_media_cmd_ioctl - Driver command for VIRTIO_MEDIA_CMD_IOCTL.
 * @hdr: header with cmd member set to VIRTIO_MEDIA_CMD_IOCTL.
 * @session_id: id of the session to run the ioctl on.
 * @code: code of the ioctl to run.
 */
struct virtio_media_cmd_ioctl {
	struct virtio_media_cmd_header hdr;
	u32 session_id;
	u32 code;
};

/**
 * struct virtio_media_resp_ioctl - Device response for VIRTIO_MEDIA_CMD_IOCTL.
 * @hdr: header containing the status of the ioctl.
 */
struct virtio_media_resp_ioctl {
	struct virtio_media_resp_header hdr;
};

/**
 * struct virtio_media_sg_entry - Description of part of a scattered guest memory.
 * @start: start guest address of the memory segment.
 * @len: length of this memory segment.
 * @__reserved: must be set to zero by the driver.
 */
struct virtio_media_sg_entry {
	u64 start;
	u32 len;
	u32 __reserved;
};

/**
 * enum virtio_media_memory - Memory types supported by virtio-media.
 * @VIRTIO_MEDIA_MMAP: memory allocated and managed by device. Can be mapped
 * into the guest using VIRTIO_MEDIA_CMD_MMAP.
 * @VIRTIO_MEDIA_SHARED_PAGES: memory allocated by the driver. Passed to the
 * device using virtio_media_sg_entry.
 * @VIRTIO_MEDIA_OBJECT: memory backed by a virtio object.
 */
enum virtio_media_memory {
	VIRTIO_MEDIA_MMAP = V4L2_MEMORY_MMAP,
	VIRTIO_MEDIA_SHARED_PAGES = V4L2_MEMORY_USERPTR,
	VIRTIO_MEDIA_OBJECT = V4L2_MEMORY_DMABUF,
};

#define VIRTIO_MEDIA_MMAP_FLAG_RW (1 << 0)

/**
 * VIRTIO_MEDIA_CMD_MMAP - Command for mapping a MMAP buffer into the driver's
 * address space.
 *
 */
#define VIRTIO_MEDIA_CMD_MMAP 4

/**
 * struct virtio_media_cmd_mmap - Driver command for VIRTIO_MEDIA_CMD_MMAP.
 * @hdr: header with cmd member set to VIRTIO_MEDIA_CMD_MMAP.
 * @session_id: ID of the session we are mapping for.
 * @flags: combination of VIRTIO_MEDIA_MMAP_FLAG_*.
 * @offset: mem_offset field of the plane to map, as returned by VIDIOC_QUERYBUF.
 */
struct virtio_media_cmd_mmap {
	struct virtio_media_cmd_header hdr;
	u32 session_id;
	u32 flags;
	u32 offset;
};

/**
 * struct virtio_media_resp_mmap - Device response for VIRTIO_MEDIA_CMD_MMAP.
 * @hdr: header containing the status of the command.
 * @driver_addr: offset into SHM region 0 of the start of the mapping.
 * @len: length of the mapping.
 */
struct virtio_media_resp_mmap {
	struct virtio_media_resp_header hdr;
	u64 driver_addr;
	u64 len;
};

/**
 * VIRTIO_MEDIA_CMD_MUNMAP - Unmap a MMAP buffer previously mapped using
 * VIRTIO_MEDIA_CMD_MMAP.
 */
#define VIRTIO_MEDIA_CMD_MUNMAP 5

/**
 * struct virtio_media_cmd_munmap - Driver command for VIRTIO_MEDIA_CMD_MUNMAP.
 * @hdr: header with cmd member set to VIRTIO_MEDIA_CMD_MUNMAP.
 * @driver_addr: offset into SHM region 0 at which the buffer has been previously
 * mapped.
 */
struct virtio_media_cmd_munmap {
	struct virtio_media_cmd_header hdr;
	u64 driver_addr;
};

/**
 * struct virtio_media_resp_munmap - Device response for VIRTIO_MEDIA_CMD_MUNMAP.
 * @hdr: header containing the status of the command.
 */
struct virtio_media_resp_munmap {
	struct virtio_media_resp_header hdr;
};

#define VIRTIO_MEDIA_EVT_ERROR 0
#define VIRTIO_MEDIA_EVT_DQBUF 1
#define VIRTIO_MEDIA_EVT_EVENT 2

/**
 * struct virtio_media_event_header - Header for events on the eventq.
 * @event: one of VIRTIO_MEDIA_EVT_*
 * @session_id: ID of the session the event applies to.
 */
struct virtio_media_event_header {
	u32 event;
	u32 session_id;
};

/**
 * struct virtio_media_event_error - Unrecoverable device-side error.
 * @hdr: header for the event.
 * @errno: error code describing the kind of error that occurred.
 * @__reserved: must to set to zero by the device.
 *
 * Upon receiving this event, the session mentioned in the header is considered
 * corrupted and closed.
 *
 */
struct virtio_media_event_error {
	struct virtio_media_event_header hdr;
	u32 errno;
	u32 __reserved;
};

#define VIRTIO_MEDIA_MAX_PLANES VIDEO_MAX_PLANES

/**
 * struct virtio_media_event_dqbuf - Dequeued buffer event.
 * @hdr: header for the event.
 * @buffer: struct v4l2_buffer describing the buffer that has been dequeued.
 * @planes: plane information for the dequeued buffer.
 *
 * This event is used to signal that a buffer is not being used anymore by the
 * device and is returned to the driver.
 */
struct virtio_media_event_dqbuf {
	struct virtio_media_event_header hdr;
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[VIRTIO_MEDIA_MAX_PLANES];
};

/**
 * struct virtio_media_event_event - V4L2 event.
 * @hdr: header for the event.
 * @event: description of the event that occurred.
 *
 * This event signals that a V4L2 event has been emitted for a session.
 */
struct virtio_media_event_event {
	struct virtio_media_event_header hdr;
	struct v4l2_event event;
};

/* Maximum size of an event. We will queue descriptors of this size on the eventq. */
#define VIRTIO_MEDIA_EVENT_MAX_SIZE sizeof(struct virtio_media_event_dqbuf)

#endif // __VIRTIO_MEDIA_PROTOCOL_H
