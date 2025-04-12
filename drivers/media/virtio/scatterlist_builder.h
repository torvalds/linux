/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0+ */

/*
 * Scatterlist builder helpers for virtio-media.
 *
 * Copyright (c) 2024-2025 Google LLC.
 */

#ifndef __VIRTIO_MEDIA_SCATTERLIST_BUILDER_H
#define __VIRTIO_MEDIA_SCATTERLIST_BUILDER_H

#include <linux/scatterlist.h>

#include "session.h"

/**
 * struct scatterlist_builder - helper to build a scatterlist from data.
 * @descs: pool of descriptors to use.
 * @num_descs: number of entries in descs.
 * @cur_desc: next descriptor to be used in @descs.
 * @shadow_buffer: pointer to a shadow buffer where elements that cannot be
 * mapped directly into the scatterlist get copied.
 * @shadow_buffer_size: size of @shadow_buffer.
 * @shadow_buffer_pos: current position in @shadow_buffer.
 * @sgs: descriptor chain to eventually pass to virtio functions.
 * @num_sgs: total number of entries in @sgs.
 * @cur_sg: next entry in @sgs to be used.
 *
 * Virtio passes data from the driver to the device (through e.g.
 * ``virtqueue_add_sgs``) via a scatterlist that the device interprets as a
 * linear view over scattered driver memory.
 *
 * In virtio-media, the payload of ioctls from user-space can for the most part
 * be passed as-is, or after slight modification, which makes it tempting to
 * just forward the ioctl payload received from user-space as-is instead of
 * doing another copy into a dedicated buffer. This structure helps with this.
 *
 * virtio-media descriptor chains are typically made of the following parts:
 *
 * Device-readable:
 * - A command structure, i.e. ``virtio_media_cmd_*``,
 * - An ioctl payload (one of the regular ioctl parameters),
 * - (optionally) arrays of ``virtio_media_sg_entry`` describing the content of
 *   buffers in guest memory.
 *
 * Device-writable:
 * - A response structure, i.e. ``virtio_media_resp_*``,
 * - An ioctl payload, that the device will write to.
 *
 * This structure helps laying out the descriptor chain into its @sgs member in
 * an optimal way, by building a scatterlist adapted to the originating memory
 * of the data we want to pass to the device while avoiding copies when
 * possible.
 *
 * It is made of a pool of ``struct scatterlist`` (@descs) that is used to
 * build the final descriptor chain @sgs, and a @shadow_buffer where data that
 * cannot (or should not) be mapped directly by the host can be temporarily
 * copied.
 */
struct scatterlist_builder {
	struct scatterlist *descs;
	size_t num_descs;
	size_t cur_desc;

	void *shadow_buffer;
	size_t shadow_buffer_size;
	size_t shadow_buffer_pos;

	struct scatterlist **sgs;
	size_t num_sgs;
	size_t cur_sg;
};

int scatterlist_builder_add_descriptor(struct scatterlist_builder *builder,
				       size_t desc_index);

int scatterlist_builder_add_data(struct scatterlist_builder *builder,
				 void *data, size_t len);

int scatterlist_builder_retrieve_data(struct scatterlist_builder *builder,
				      size_t sg_index, void *data);

int scatterlist_builder_add_ioctl_cmd(struct scatterlist_builder *builder,
				      struct virtio_media_session *session,
				      u32 ioctl_code);

int scatterlist_builder_add_ioctl_resp(struct scatterlist_builder *builder,
				       struct virtio_media_session *session);

int scatterlist_builder_add_buffer(struct scatterlist_builder *builder,
				   struct v4l2_buffer *buffer);

int scatterlist_builder_add_buffer_userptr(struct scatterlist_builder *builder,
					   struct v4l2_buffer *b);

int scatterlist_builder_retrieve_buffer(struct scatterlist_builder *builder,
					size_t sg_index,
					struct v4l2_buffer *buffer,
					struct v4l2_plane *orig_planes);

int scatterlist_builder_add_ext_ctrls(struct scatterlist_builder *builder,
				      struct v4l2_ext_controls *ctrls);

int scatterlist_builder_add_ext_ctrls_userptrs(
	struct scatterlist_builder *builder, struct v4l2_ext_controls *ctrls);

int scatterlist_builder_retrieve_ext_ctrls(struct scatterlist_builder *builder,
					   size_t sg_index,
					   struct v4l2_ext_controls *ctrls);

#endif // __VIRTIO_MEDIA_SCATTERLIST_BUILDER_H
