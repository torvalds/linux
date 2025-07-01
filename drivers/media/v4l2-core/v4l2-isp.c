// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Video4Linux2 generic ISP parameters and statistics support
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Author: Jacopo Mondi <jacopo.mondi@ideasonboard.com>
 */

#include <media/v4l2-isp.h>

#include <linux/bitops.h>
#include <linux/device.h>

#include <media/videobuf2-core.h>

int v4l2_isp_params_validate_buffer_size(struct device *dev,
					 struct vb2_buffer *vb,
					 size_t max_size)
{
	size_t header_size = offsetof(struct v4l2_isp_params_buffer, data);
	size_t payload_size = vb2_get_plane_payload(vb, 0);

	/* Payload size can't be greater than the destination buffer size */
	if (payload_size > max_size) {
		dev_dbg(dev, "Payload size is too large: %zu\n", payload_size);
		return -EINVAL;
	}

	/* Payload size can't be smaller than the header size */
	if (payload_size < header_size) {
		dev_dbg(dev, "Payload size is too small: %zu\n", payload_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_isp_params_validate_buffer_size);

int v4l2_isp_params_validate_buffer(struct device *dev, struct vb2_buffer *vb,
				    const struct v4l2_isp_params_buffer *buffer,
				    const struct v4l2_isp_params_block_info *info,
				    size_t num_blocks)
{
	size_t header_size = offsetof(struct v4l2_isp_params_buffer, data);
	size_t payload_size = vb2_get_plane_payload(vb, 0);
	size_t block_offset = 0;
	size_t buffer_size;

	/*
	 * Currently only the first version of the V4L2 ISP parameters format is
	 * supported. We accept both V0 and V1 to support existing drivers
	 * compatible with V4L2 ISP that use either 0 or 1 as their "first
	 * version" identifiers.
	 */
	if (buffer->version != V4L2_ISP_PARAMS_VERSION_V0 &&
	    buffer->version != V4L2_ISP_PARAMS_VERSION_V1) {
		dev_dbg(dev,
			"Unsupported V4L2 ISP parameters format version: %u\n",
			buffer->version);
		return -EINVAL;
	}

	/* Validate the size reported in the header */
	buffer_size = header_size + buffer->data_size;
	if (buffer_size != payload_size) {
		dev_dbg(dev, "Data size %zu and payload size %zu are different\n",
			buffer_size, payload_size);
		return -EINVAL;
	}

	/* Walk the list of ISP configuration blocks and validate them. */
	buffer_size = buffer->data_size;
	while (buffer_size >= sizeof(struct v4l2_isp_params_block_header)) {
		const struct v4l2_isp_params_block_info *block_info;
		const struct v4l2_isp_params_block_header *block;

		block = (const struct v4l2_isp_params_block_header *)
			(buffer->data + block_offset);

		if (block->type >= num_blocks) {
			dev_dbg(dev,
				"Invalid block type %u at offset %zu\n",
				block->type, block_offset);
			return -EINVAL;
		}

		if (block->size > buffer_size) {
			dev_dbg(dev, "Premature end of parameters data\n");
			return -EINVAL;
		}

		/* It's invalid to specify both ENABLE and DISABLE. */
		if ((block->flags & (V4L2_ISP_PARAMS_FL_BLOCK_ENABLE |
				     V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)) ==
		     (V4L2_ISP_PARAMS_FL_BLOCK_ENABLE |
		     V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)) {
			dev_dbg(dev, "Invalid block flags %x at offset %zu\n",
				block->flags, block_offset);
			return -EINVAL;
		}

		/*
		 * Match the block reported size against the info provided
		 * one, but allow the block to only contain the header in
		 * case it is going to be disabled.
		 */
		block_info = &info[block->type];
		if (block->size != block_info->size &&
		    (!(block->flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) ||
		    block->size != sizeof(*block))) {
			dev_dbg(dev,
				"Invalid block size %u (expected %zu) at offset %zu\n",
				block->size, block_info->size, block_offset);
			return -EINVAL;
		}

		block_offset += block->size;
		buffer_size -= block->size;
	}

	if (buffer_size) {
		dev_dbg(dev, "Unexpected data after the parameters buffer end\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_isp_params_validate_buffer);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jacopo Mondi <jacopo.mondi@ideasonboard.com");
MODULE_DESCRIPTION("V4L2 generic ISP parameters and statistics helpers");
