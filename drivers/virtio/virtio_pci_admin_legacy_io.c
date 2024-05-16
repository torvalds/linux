// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/virtio_pci_admin.h>
#include "virtio_pci_common.h"

/*
 * virtio_pci_admin_has_legacy_io - Checks whether the legacy IO
 * commands are supported
 * @dev: VF pci_dev
 *
 * Returns true on success.
 */
bool virtio_pci_admin_has_legacy_io(struct pci_dev *pdev)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_pci_device *vp_dev;

	if (!virtio_dev)
		return false;

	if (!virtio_has_feature(virtio_dev, VIRTIO_F_ADMIN_VQ))
		return false;

	vp_dev = to_vp_device(virtio_dev);

	if ((vp_dev->admin_vq.supported_cmds & VIRTIO_LEGACY_ADMIN_CMD_BITMAP) ==
		VIRTIO_LEGACY_ADMIN_CMD_BITMAP)
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_has_legacy_io);

static int virtio_pci_admin_legacy_io_write(struct pci_dev *pdev, u16 opcode,
					    u8 offset, u8 size, u8 *buf)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_legacy_wr_data *data;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist data_sg;
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	data = kzalloc(sizeof(*data) + size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->offset = offset;
	memcpy(data->registers, buf, size);
	sg_init_one(&data_sg, data, sizeof(*data) + size);
	cmd.opcode = cpu_to_le16(opcode);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);

	kfree(data);
	return ret;
}

/*
 * virtio_pci_admin_legacy_io_write_common - Write legacy common configuration
 * of a member device
 * @dev: VF pci_dev
 * @offset: starting byte offset within the common configuration area to write to
 * @size: size of the data to write
 * @buf: buffer which holds the data
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_legacy_common_io_write(struct pci_dev *pdev, u8 offset,
					    u8 size, u8 *buf)
{
	return virtio_pci_admin_legacy_io_write(pdev,
					VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_WRITE,
					offset, size, buf);
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_legacy_common_io_write);

/*
 * virtio_pci_admin_legacy_io_write_device - Write legacy device configuration
 * of a member device
 * @dev: VF pci_dev
 * @offset: starting byte offset within the device configuration area to write to
 * @size: size of the data to write
 * @buf: buffer which holds the data
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_legacy_device_io_write(struct pci_dev *pdev, u8 offset,
					    u8 size, u8 *buf)
{
	return virtio_pci_admin_legacy_io_write(pdev,
					VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_WRITE,
					offset, size, buf);
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_legacy_device_io_write);

static int virtio_pci_admin_legacy_io_read(struct pci_dev *pdev, u16 opcode,
					   u8 offset, u8 size, u8 *buf)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_legacy_rd_data *data;
	struct scatterlist data_sg, result_sg;
	struct virtio_admin_cmd cmd = {};
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->offset = offset;
	sg_init_one(&data_sg, data, sizeof(*data));
	sg_init_one(&result_sg, buf, size);
	cmd.opcode = cpu_to_le16(opcode);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	cmd.result_sg = &result_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);

	kfree(data);
	return ret;
}

/*
 * virtio_pci_admin_legacy_device_io_read - Read legacy device configuration of
 * a member device
 * @dev: VF pci_dev
 * @offset: starting byte offset within the device configuration area to read from
 * @size: size of the data to be read
 * @buf: buffer to hold the returned data
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_legacy_device_io_read(struct pci_dev *pdev, u8 offset,
					   u8 size, u8 *buf)
{
	return virtio_pci_admin_legacy_io_read(pdev,
					VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_READ,
					offset, size, buf);
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_legacy_device_io_read);

/*
 * virtio_pci_admin_legacy_common_io_read - Read legacy common configuration of
 * a member device
 * @dev: VF pci_dev
 * @offset: starting byte offset within the common configuration area to read from
 * @size: size of the data to be read
 * @buf: buffer to hold the returned data
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_legacy_common_io_read(struct pci_dev *pdev, u8 offset,
					   u8 size, u8 *buf)
{
	return virtio_pci_admin_legacy_io_read(pdev,
					VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_READ,
					offset, size, buf);
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_legacy_common_io_read);

/*
 * virtio_pci_admin_legacy_io_notify_info - Read the queue notification
 * information for legacy interface
 * @dev: VF pci_dev
 * @req_bar_flags: requested bar flags
 * @bar: on output the BAR number of the owner or member device
 * @bar_offset: on output the offset within bar
 *
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_legacy_io_notify_info(struct pci_dev *pdev,
					   u8 req_bar_flags, u8 *bar,
					   u64 *bar_offset)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_notify_info_result *result;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist result_sg;
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	sg_init_one(&result_sg, result, sizeof(*result));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_LEGACY_NOTIFY_INFO);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.result_sg = &result_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (!ret) {
		struct virtio_admin_cmd_notify_info_data *entry;
		int i;

		ret = -ENOENT;
		for (i = 0; i < VIRTIO_ADMIN_CMD_MAX_NOTIFY_INFO; i++) {
			entry = &result->entries[i];
			if (entry->flags == VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_END)
				break;
			if (entry->flags != req_bar_flags)
				continue;
			*bar = entry->bar;
			*bar_offset = le64_to_cpu(entry->offset);
			ret = 0;
			break;
		}
	}

	kfree(result);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_legacy_io_notify_info);
