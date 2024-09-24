/*
 *
 * (C) COPYRIGHT 2022-2023 Arm Limited.
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

#include "ethosn_proc_mem_allocator.h"

#include "ethosn_device.h"
#include "ethosn_core.h"
#include "ethosn_buffer.h"
#include "ethosn_asset_allocator.h"
#include "ethosn_network.h"
#include "uapi/ethosn.h"

#include <linux/anon_inodes.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/uaccess.h>

static const struct file_operations allocator_fops;

static bool is_ethosn_allocator_file(const struct file *const file)
{
	return file->f_op == &allocator_fops;
}

/**
 * proc_mem_allocator_release() - Destroy and free the proc_mem_allocator object
 * @inode: Pointer to inode struct
 * @filep: Pointer to file struct
 *
 * The proc_mem_allocator ceases to exist but the asset_allocator and the
 * memory allocations created by it lives on and the process can continue to use
 * it. The asset_allocator is free to be assigned to another process only after
 * all the objects created using it are freed.
 *
 * Return: 0 on success, else error code
 */
static int proc_mem_allocator_release(struct inode *inode,
				      struct file *filep)
{
	struct ethosn_allocator *proc_mem_allocator = filep->private_data;
	struct ethosn_device *ethosn;
	struct ethosn_dma_allocator *asset_allocator;
	int ret = 0;

	if (WARN_ON(!is_ethosn_allocator_file(filep)))
		return -EBADF;

	if (!proc_mem_allocator || !proc_mem_allocator->ethosn)
		return -EINVAL;

	ethosn = proc_mem_allocator->ethosn;
	asset_allocator = proc_mem_allocator->asset_allocator;

	ret = mutex_lock_interruptible(&ethosn->mutex);
	if (ret)
		return ret;

	if (WARN_ON(ethosn_asset_allocator_put(asset_allocator) < 0))
		dev_warn(ethosn->dev,
			 "%s failed for proc_mem_allocator handle 0x%pK\n",
			 __func__, proc_mem_allocator);

	devm_kfree(ethosn->dev, proc_mem_allocator);

	mutex_unlock(&ethosn->mutex);

	dev_dbg(ethosn->dev, "Released process memory allocator");

	return ret;
}

static void print_buffer_info(struct ethosn_device *ethosn,
			      const char *prefix,
			      u32 ninfos,
			      const struct ethosn_buffer_info __user *infos)
{
	char buf[200];
	size_t n = 0;
	u32 i;
	const char *delim = "";

	n += scnprintf(&buf[n], sizeof(buf) - n, "    %s: ", prefix);

	for (i = 0; i < ninfos; ++i) {
		struct ethosn_buffer_info info;

		if (copy_from_user(&info, &infos[i], sizeof(info)))
			break;

		n += scnprintf(&buf[n], sizeof(buf) - n, "%s{%u, %u, %u}",
			       delim, info.id, info.offset, info.size);

		delim = ", ";
	}

	dev_dbg(ethosn->dev, "%s\n", buf);
}

/**
 * proc_mem_allocator_ioctl() - Handle proc_mem_allocator commands from
 *                              userspace
 * @filep: File struct
 * @cmd: User command
 * @arg: Arguments to the ioctl command.
 *
 * Return: File descriptor on success, else error code
 */
static long proc_mem_allocator_ioctl(struct file *filep,
				     unsigned int cmd,
				     unsigned long arg)
{
	struct ethosn_allocator *proc_mem_allocator = filep->private_data;
	struct ethosn_device *ethosn;
	const void __user *udata = (void __user *)arg;
	int ret = -EINVAL;

	if (!proc_mem_allocator || !proc_mem_allocator->ethosn)
		return -EINVAL;

	ethosn = proc_mem_allocator->ethosn;

	switch (cmd) {
	case ETHOSN_IOCTL_REGISTER_NETWORK: {
		struct ethosn_network_req net_req;

		if (copy_from_user(&net_req, udata, sizeof(net_req))) {
			ret = -EFAULT;
			break;
		}

		dev_dbg(ethosn->dev,
			"IOCTL: Register network. num_dma=%u, num_cu=%u, num_intermediates=%u, num_inputs=%u, num_outputs=%u\n",
			net_req.dma_buffers.num,
			net_req.cu_buffers.num,
			net_req.intermediate_desc.buffers.num,
			net_req.input_buffers.num,
			net_req.output_buffers.num);

		print_buffer_info(ethosn, "dma", net_req.dma_buffers.num,
				  net_req.dma_buffers.info);
		print_buffer_info(ethosn, "cu", net_req.cu_buffers.num,
				  net_req.cu_buffers.info);
		print_buffer_info(ethosn, "intermediate",
				  net_req.intermediate_desc.buffers.num,
				  net_req.intermediate_desc.buffers.info);
		print_buffer_info(ethosn, "input", net_req.input_buffers.num,
				  net_req.input_buffers.info);
		print_buffer_info(ethosn, "output", net_req.output_buffers.num,
				  net_req.output_buffers.info);

		if (net_req.intermediate_desc.buffers.num &&
		    net_req.intermediate_desc.memory.type == ALLOCATE &&
		    proc_mem_allocator->asset_allocator->is_protected) {
			dev_dbg(ethosn->dev,
				"IOCTL: Register Network requires imported intermediate buffers while in protected context\n");
			ret = -EPERM;
			break;
		}

		ret = mutex_lock_interruptible(&ethosn->mutex);
		if (ret)
			break;

		ret = ethosn_network_register(
			ethosn,
			proc_mem_allocator->asset_allocator,
			&net_req);

		mutex_unlock(&ethosn->mutex);

		dev_dbg(ethosn->dev,
			"IOCTL: Registered network. fd=%d\n", ret);

		break;
	}
	case ETHOSN_IOCTL_CREATE_BUFFER: {
		struct ethosn_buffer_req buf_req;

		if (copy_from_user(&buf_req, udata, sizeof(buf_req))) {
			ret = -EFAULT;
			break;
		}

		if (proc_mem_allocator->asset_allocator->is_protected) {
			dev_dbg(ethosn->dev,
				"IOCTL: Create buffer not allowed when in protected context\n");
			ret = -EPERM;
			break;
		}

		ret = mutex_lock_interruptible(&ethosn->mutex);
		if (ret)
			break;

		dev_dbg(ethosn->dev,
			"IOCTL: Create buffer. size=%u, flags=0x%x\n",
			buf_req.size, buf_req.flags);

		ret = ethosn_buffer_register(
			ethosn,
			proc_mem_allocator->asset_allocator,
			&buf_req);

		dev_dbg(ethosn->dev,
			"IOCTL: Created buffer. fd=%d\n", ret);

		mutex_unlock(&ethosn->mutex);

		break;
	}
	case ETHOSN_IOCTL_IMPORT_BUFFER: {
		struct ethosn_dma_buf_req dma_buf_req;

		if (copy_from_user(&dma_buf_req, udata, sizeof(dma_buf_req))) {
			ret = -EFAULT;
			break;
		}

		ret = mutex_lock_interruptible(&ethosn->mutex);
		if (ret)
			break;

		dev_dbg(ethosn->dev,
			"IOCTL: Import buffer. size=%zu, flags=0x%x\n",
			dma_buf_req.size, dma_buf_req.flags);

		ret = ethosn_buffer_import(ethosn,
					   proc_mem_allocator->asset_allocator,
					   &dma_buf_req);

		dev_dbg(ethosn->dev,
			"IOCTL: Imported buffer. fd=%d\n", ret);

		mutex_unlock(&ethosn->mutex);

		break;
	}
	default: {
		ret = -EINVAL;
	}
	}

	return ret;
}

static const struct file_operations allocator_fops = {
	.owner          = THIS_MODULE,
	/* release the assigned asset allocator */
	.release        = &proc_mem_allocator_release,
	/* IOCTLs for proc_mem_allocator creation */
	.unlocked_ioctl = &proc_mem_allocator_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = &proc_mem_allocator_ioctl,
#endif
};

/**
 * ethosn_process_mem_allocator_create() - Create process memory allocator
 *                                         object.
 * @ethosn: Pointer to the ethosn device structure.
 * @pid: Id of the process requesting proc_mem_allocator.
 *
 * This function is used to create a memory allocator object, assign a free
 * allocator and fd for it. Only one asset allocator is assigned per process.
 *
 * Reference count on the asset_allocator object is incremented/decremented
 * during the following events
 *     1) Initialized to 1 when it is reserved for a process.
 *     2) Incremented during create buffer(ETHOSN_IOCTL_CREATE_BUFFER).
 *     3) Incremented during network register(ETHOSN_IOCTL_REGISTER_NETWORK).
 *     4) Decremented when fd for proc_mem_allocator object is closed
 *        (proc_mem_allocator_release).
 *     5) Decremented when buffer fd is closed(ethosn_buffer_release()).
 *     6) Decremented when network fd is closed(network_release()).
 *
 * The proc_mem_allocator object is freed and ref count is decremented in
 * proc_mem_allocator_release() when its fd is closed by the user space. But
 * the asset_allocator object continues to live and is associated to the same
 * process. The asset_allocator is free to be assigned to another only after
 * all the memory allocations done using it are freed and unmapped.
 *
 * Return:
 * fd - A valid file descriptor on success.
 * -EACCES - If requesting process already holds an asset_allocator.
 * -EINVAL - Invalid argument.
 * -ENOMEM - Failed to allocate memory or a free asset_allocator.
 */
int ethosn_process_mem_allocator_create(struct ethosn_device *ethosn,
					pid_t pid,
					bool protected)
{
	int ret = 0;
	int fd;
	struct ethosn_allocator *proc_mem_allocator;
	struct ethosn_dma_allocator *asset_allocator =
		ethosn_asset_allocator_find(ethosn, pid);

	if (pid <= 0) {
		dev_err(ethosn->dev, "%s: Unsupported pid=%d, must be >0\n",
			__func__, pid);

		return -EINVAL;
	}

	dev_dbg(ethosn->dev,
		"Process %d requests a %sprotected asset allocator", pid,
		protected ? "" : "non-");

#if !defined(ETHOSN_TZMP1)
	if (protected) {
		dev_err(ethosn->dev,
			"Protected allocator is not allowed unless kernel module is built with TZMP1 support.\n");

		return -EPERM;
	}

#endif

	proc_mem_allocator = devm_kzalloc(ethosn->dev,
					  sizeof(*proc_mem_allocator),
					  GFP_KERNEL);

	if (!proc_mem_allocator)
		return -ENOMEM;

	proc_mem_allocator->ethosn = ethosn;

	if (IS_ERR_OR_NULL(asset_allocator)) {
		/* No existing allocator for process */
		proc_mem_allocator->asset_allocator =
			ethosn_asset_allocator_reserve(ethosn, pid);

		if (!proc_mem_allocator->asset_allocator) {
			dev_err(ethosn->dev,
				"No free asset allocators available");
			ret = -EBUSY;
			goto asset_allocator_reserve_fail;
		}
	} else {
		/* Process already has an allocator */
		if (asset_allocator->is_protected == protected) {
			proc_mem_allocator->asset_allocator = asset_allocator;
			ethosn_asset_allocator_get(asset_allocator);
		} else {
			/* It is not allowed to mix protected/non-protected
			 * context
			 */
			dev_err(ethosn->dev,
				"Mixing protected context not allowed");
			ret = -EINVAL;
			goto asset_allocator_reserve_fail;
		}
	}

	proc_mem_allocator->asset_allocator->is_protected = protected;

	fd = anon_inode_getfd("ethosn-memory-allocator",
			      &allocator_fops,
			      proc_mem_allocator,
			      O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = -ENOMEM;
		goto get_fd_fail;
	}

	proc_mem_allocator->file = fget(fd);

	fput(proc_mem_allocator->file);

	dev_dbg(ethosn->dev,
		"Assigned %sprotected asset_allocator. proc memory allocator handle=0x%pK\n",
		protected ? "" : "non-", proc_mem_allocator);

	return fd;

get_fd_fail:
	ethosn_asset_allocator_put(proc_mem_allocator->asset_allocator);
asset_allocator_reserve_fail:
	devm_kfree(ethosn->dev, proc_mem_allocator);

	return ret;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_process_mem_allocator_create);
