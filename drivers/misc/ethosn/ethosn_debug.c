/*
 *
 * (C) COPYRIGHT 2021-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_debug.h"

#include "ethosn_buffer.h"

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/slab.h>

#if defined(DEBUG)

/**
 * ethosn_get_dma_view_fd() - Creates a file handle that provides access to an
 * existing DMA buffer.
 *
 * Return: File descriptor on success (positive), else error code (negative).
 */
int ethosn_get_dma_view_fd(struct ethosn_device *ethosn,
			   struct ethosn_dma_allocator *allocator,
			   struct ethosn_dma_info *dma_info)
{
	struct ethosn_buffer *buf;
	const struct file_operations *fops = ethosn_get_dma_view_fops();
	int fd;

	if (dma_info == NULL) {
		dev_err(ethosn->dev,
			"Failed to crate DMA view handle - dma_info is NULL\n");

		return -EINVAL;
	}

	/* Re-use the ethosn_buffer struct as there is a lot of overlap in
	 * functionality
	 */
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dev_dbg(ethosn->dev,
		"Create DMA view handle. handle=0x%pK\n", buf);

	buf->ethosn = ethosn;
	buf->dma_info = dma_info;
	buf->asset_allocator = allocator;

	fd = anon_inode_getfd("ethosn-dma-view", fops, buf,
			      O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		goto err_kfree;

	buf->file = fget(fd);
	buf->file->f_mode |= FMODE_LSEEK;

	fput(buf->file);

	get_device(ethosn->dev);

	return fd;

err_kfree:
	kfree(buf);

	return fd;
}

#else
int ethosn_get_dma_view_fd(struct ethosn_device *ethosn,
			   struct ethosn_dma_allocator *allocator,
			   struct ethosn_dma_info *dma_info)
{
	dev_err(ethosn->dev, "Buffer view only available in debug mode\n");

	return -EPERM;
}

#endif
