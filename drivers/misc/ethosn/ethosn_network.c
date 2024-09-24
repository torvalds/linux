/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
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

#include "ethosn_network.h"

#include "ethosn_asset_allocator.h"
#include "ethosn_backport.h"
#include "ethosn_buffer.h"
#include "ethosn_debug.h"
#include "ethosn_device.h"
#include "ethosn_dma.h"
#include "ethosn_firmware.h"
#include "uapi/ethosn.h"

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

static struct device *net_to_dev(const struct ethosn_network *const net)
{
	return net->ethosn->dev;
}

static struct device *ifr_to_dev(const struct ethosn_inference *const ifr)
{
	return net_to_dev(ifr->network);
}

static struct ethosn_buffer_array *get_inference_header(
	const struct ethosn_network *const network,
	uint32_t core_id)
{
	return network->inference_data[core_id]->cpu_addr;
}

static int set_binding(struct ethosn_network *network,
		       uint32_t core_id,
		       struct ethosn_buffer_info *buf_info,
		       ethosn_address_t container_start,
		       ethosn_address_t container_size,
		       bool check_in_container,
		       enum ethosn_buffer_type buffer_type)
{
	ethosn_address_t buf_start = container_start + buf_info->offset;
	ethosn_address_t buf_end = buf_start + buf_info->size;
	ethosn_address_t container_end = container_start + container_size;
	struct ethosn_buffer_array *buffers =
		get_inference_header(network, core_id);
	struct device *dev = net_to_dev(network);

	if (buf_start > buf_end) {
		dev_err(dev, "Overflow in inference binding: %llu > %llu\n",
			buf_start, buf_end);

		return -EINVAL;
	}

	if (check_in_container && (buf_end > container_end)) {
		dev_err(dev,
			"Inference binding outside of container: { %u, %u } > { 0, %llu }\n", buf_info->offset, buf_info->offset + buf_info->size,
			container_size);

		return -EINVAL;
	}

	buffers->buffers[buf_info->id].address = buf_start;
	buffers->buffers[buf_info->id].size = buf_info->size;
	buffers->buffers[buf_info->id].type = buffer_type;

	return 0;
}

static int update_bindings(struct ethosn_network *network,
			   uint32_t core_id,
			   u32 num_buffer_infos,
			   struct ethosn_buffer_info *buffer_infos,
			   ethosn_address_t container_start,
			   ethosn_address_t container_size,
			   bool check_duplicates,
			   bool check_in_container,
			   enum ethosn_buffer_type buffer_type)
{
	u32 i;
	ethosn_address_t min_buf_start = container_size;
	ethosn_address_t max_buf_end = 0;
	struct ethosn_buffer_array *buffers =
		get_inference_header(network, core_id);
	struct device *dev = net_to_dev(network);

	for (i = 0; i < num_buffer_infos; ++i) {
		struct ethosn_buffer_info *const buf_info =
			&buffer_infos[i];
		ethosn_address_t buf_start = buf_info->offset;
		ethosn_address_t buf_end = buf_start + buf_info->size;
		int ret;

		if (buf_info->id >= buffers->num_buffers) {
			dev_err(dev, "Invalid inference binding id: %u >= %u\n",
				buf_info->id, buffers->num_buffers);

			return -EINVAL;
		}

		if (check_duplicates &&
		    (buffers->buffers[buf_info->id].size != 0)) {
			dev_err(dev, "Duplicate inference binding id: %u\n",
				buf_info->id);

			return -EINVAL;
		}

		ret = set_binding(network,
				  core_id,
				  buf_info,
				  container_start,
				  container_size,
				  check_in_container,
				  buffer_type);
		if (ret)
			return ret;

		if (buf_start < min_buf_start)
			min_buf_start = buf_start;

		if (buf_end > max_buf_end)
			max_buf_end = buf_end;
	}

	if (check_in_container &&
	    ((min_buf_start > 0) || (max_buf_end < container_size)))
		/* Buffers have alignment requirements and this below
		 * is only an indication
		 */
		dev_dbg(dev,
			"Unused buffer data { %llu, %llu } <> { 0, %llu }\n",
			min_buf_start, max_buf_end, container_size);

	return 0;
}

static void get_network(struct ethosn_network *network)
{
	get_file(network->file);
}

static void put_network(struct ethosn_network *network)
{
	fput(network->file);
}

static void free_buffers(struct device *dev,
			 const u32 n,
			 struct ethosn_buffer **bufs)
{
	u32 i;

	if (IS_ERR_OR_NULL(bufs))
		return;

	for (i = 0; i < n; ++i)
		put_ethosn_buffer(bufs[i]);

	devm_kfree(dev, bufs);
}

static void free_inference(struct ethosn_inference *inference)
{
	struct device *dev = ifr_to_dev(inference);

	dev_dbg(dev, "Freeing inference. handle=0x%pK\n", inference);

	free_buffers(dev, inference->network->num_inputs,
		     inference->inputs);
	free_buffers(dev, inference->network->num_outputs,
		     inference->outputs);

	put_network(inference->network);

	devm_kfree(dev, inference);
}

static struct ethosn_buffer **read_buffer_fds(struct ethosn_network *network,
					      u32 n,
					      const int __user *fds,
					      struct ethosn_buffer_info *infos)
{
	struct ethosn_buffer **bufs;
	int error;
	u32 i;
	struct device *dev = net_to_dev(network);

	bufs = devm_kcalloc(dev, n, sizeof(*bufs), GFP_KERNEL);
	if (!bufs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < n;) {
		u32 buf_size = infos[i].size;
		struct ethosn_buffer *buf;
		int fd;

		if (copy_from_user(&fd, fds + i, sizeof(fd))) {
			error = -EFAULT;
			goto err_free_bufs;
		}

		buf = ethosn_buffer_get(fd);
		if (IS_ERR(buf)) {
			error = PTR_ERR(buf);
			dev_err(dev,
				"ethosn_buffer_get returned an error: %d\n",
				error);
			goto err_free_bufs;
		}

		if (!buf) {
			error = -EFAULT;
			dev_err(dev,
				"ethosn_buffer_get returned an empty buffer\n");
			goto err_free_bufs;
		}

		bufs[i] = buf;

		++i;

		if (buf->ethosn->dev != dev) {
			dev_err(dev,
				"device buffer 0x%pK belongs to a different dev\n",
				buf);
			error = -EINVAL;
			goto err_free_bufs;
		}

		if (buf->dma_info->size < buf_size) {
			dev_err(dev,
				"Network size does not match buffer size. handle=0x%pK, buf_size=%zu, network_size=%u, fd=%d\n", buf, buf->dma_info->size, buf_size,
				fd);
			error = -EINVAL;
			goto err_free_bufs;
		}
	}

	return bufs;

err_free_bufs:
	free_buffers(dev, i, bufs);

	return ERR_PTR(error);
}

/* ethosn_check_if_ok_to_schedule() - Check if `inference` can be scheduled or
 * if it share imported intermediate buffers
 * Return:
 * * 1 - OK to schedule
 * * 0 - Not ok
 */

static bool ethosn_check_if_ok_to_schedule(struct ethosn_device *ethosn,
					   struct ethosn_inference *inference)
{
	int ii;

	if (inference == NULL) {
		dev_dbg(ethosn->dev, "Inference is NULL\n");

		return false;
	}

	if (!inference->network->num_intermediates ||
	    !inference->network->intermediate_data[0]->imported)

		/* If the inference isn't using any imported intermediate
		 * buffer(s) it's okay to schedule.
		 *
		 * Note: Since imported intermediate buffers are shared between
		 * the cores, it is enough to check the intermediate data
		 * assigned to core 0.
		 */
		return true;

	for (ii = 0; ii < ethosn->num_cores; ++ii) {
		struct ethosn_inference *core_inference =
			ethosn->core[ii]->current_inference;
		if (!core_inference)
			continue;

		/* If the inference to schedule is on a network using shared
		 * intermediate buffers, we must check so the same network isn't
		 * running on another core.
		 */
		if (core_inference->network == inference->network) {
			dev_dbg(ethosn->dev,
				"Network 0x%pK has already one running inference 0x%pK on core %d --> Do not schedule inference 0x%pK!\n",
				core_inference->network,
				core_inference,
				ethosn->core[ii]->core_id,
				inference);

			return false;
		}
	}

	return true;
}

/**
 * ethosn_schedule_inference() - Send an inference to Ethos-N
 *
 * If an inference isn't already running, send it to Ethos-N for execution.
 * Return:
 * * 0 - OK
 * * Negative error code
 */
int ethosn_schedule_inference(struct ethosn_inference *inference)
{
	struct ethosn_network *network = inference->network;
	struct ethosn_core *core = inference->core;
	struct ethosn_device *ethosn = core->parent;
	uint32_t core_id = core->core_id;
	struct device *core_dev = core->dev;
	u32 i;
	int ret;

	if (!network->asset_allocator) {
		dev_err(core_dev, "Inference has no asset allocator");
		ret = -EINVAL;
		goto out_inference_error;
	}

	if (inference->status == ETHOSN_INFERENCE_RUNNING) {
		dev_err(core_dev, "Core %d got an inference while busy",
			core_id);
		ethosn->status_mask |=
			(1 << INFERENCE_SCHEDULED_ON_BUSY_CORE);

		return 0;
	}

	if (inference->status != ETHOSN_INFERENCE_SCHEDULED)
		return 0;

	inference->status = ETHOSN_INFERENCE_RUNNING;
	if (core->set_is_protected != network->asset_allocator->is_protected ||
	    core->set_alloc_id != network->asset_allocator->alloc_id) {
		dev_dbg(core_dev,
			"Restarting core due to alloc_id changed (%d to %d) or protected context changed (%d to %d)",
			core->set_alloc_id, network->asset_allocator->alloc_id, core->set_is_protected,
			network->asset_allocator->is_protected);
		ret = ethosn_reset_and_start_ethosn(core,
						    network->asset_allocator->alloc_id);
		if (ret)
			goto out_inference_error;
	}

	for (i = 0; i < network->num_inputs; ++i) {
		struct ethosn_dma_info *dma_info =
			inference->inputs[i]->dma_info;

		ret = update_bindings(network,
				      core_id,
				      1,
				      &network->inputs[i],
				      dma_info->iova_addr,
				      dma_info->size,
				      false,
				      true,
				      ETHOSN_BUFFER_INPUT);
		if (ret)
			goto out_inference_error;
	}

	for (i = 0; i < network->num_outputs; ++i) {
		struct ethosn_dma_info *dma_info =
			inference->outputs[i]->dma_info;

		ret = update_bindings(network,
				      core_id,
				      1,
				      &network->outputs[i],
				      dma_info->iova_addr,
				      dma_info->size,
				      false,
				      true,
				      ETHOSN_BUFFER_OUTPUT);
		if (ret)
			goto out_inference_error;
	}

	ret = update_bindings(network,
			      core_id,
			      network->num_intermediates,
			      network->intermediates,
			      network->intermediate_data[core_id] == NULL ? 0 :
			      network->intermediate_data[core_id]->iova_addr,
			      network->intermediate_data[core_id] == NULL ? 0 :
			      network->intermediate_data[core_id]->size,
			      false,
			      true,
			      ETHOSN_BUFFER_INTERMEDIATE);

	if (ret)
		goto out_inference_error;

	/* kick off execution */
	dev_dbg(core_dev, "Starting execution of inference");
	ethosn_dma_sync_for_device(
		network->asset_allocator,
		network->inference_data[core_id]);
	core->current_inference = inference;

	pm_runtime_get_sync(core->dev);

	/* send the inference to the core assigned to it */
	ret = ethosn_send_inference(core,
				    network->inference_data[core_id]->iova_addr,
				    (ptrdiff_t)inference);
	if (ret) {
		core->current_inference = NULL;
		pm_runtime_mark_last_busy(core->dev);
		pm_runtime_put(core->dev);

		goto out_inference_error;
	}

	ethosn->current_busy_cores |= (1 << core_id);
	dev_dbg(core_dev, "Scheduled inference 0x%pK on core_id = %u\n",
		inference,
		core_id);

	return 0;

out_inference_error:
	dev_err(core_dev,
		"Error scheduling inference 0x%pK: %d on core_id = %u\n",
		inference, ret, core_id);
	inference->status = ETHOSN_INFERENCE_ERROR;

	return ret;
}

/**
 * ethosn_schedule_queued_inference() - Schedule a queue inference.
 * @core:	Ethos-N core.
 *
 * Pop the inference queue until either the queue is empty or an inference has
 * been successfully scheduled.
 */
void ethosn_schedule_queued_inference(struct ethosn_core *core)
{
	struct ethosn_inference *inference = NULL;
	struct ethosn_device *ethosn = core->parent;
	int ret = 0;
	bool schedule_inference = false;

	/* This will be invoked from the irq handlers of multiple npus.
	 * The inference queue needs to be protected against concurrent
	 * operation.
	 */
	ret = mutex_lock_interruptible(
		&ethosn->queue.inference_queue_mutex);
	if (ret)
		return;

	/* A network using imported intermediate buffers cannot run concurrent
	 * inferences, since the buffers are shared. The inference to be
	 * scheduled must therefore be checked against running inferences on all
	 * cores. If this is the case, then skip the inference and check next.
	 * The first inference that meets the neccessary criteria is scheduled.
	 * If none, then the inference queue is checked again after next request
	 */
	if (!list_empty(&ethosn->queue.inference_queue)) {
		list_for_each_entry(inference,
				    &ethosn->queue.inference_queue,
				    queue_node) {
			schedule_inference = ethosn_check_if_ok_to_schedule(
				ethosn,
				inference);
			if (schedule_inference) {
				list_del(&inference->queue_node);
				break;
			}
		}
	}

	mutex_unlock(&ethosn->queue.inference_queue_mutex);

	if (schedule_inference) {
		/* Schedule the inference on a particular core */
		inference->core = core;
		dev_dbg(ethosn->dev,
			"Schedule inference 0x%pK, NW 0x%pK on Core #%d\n",
			inference, inference->network,
			inference->core->core_id);
		(void)ethosn_schedule_inference(inference);
	}
}

/**
 * inference_create() - Create and schedule an inference job
 * @network: Inference network
 * @ifr_req: Inference description
 * @inference_ptr: Output resulting inference struct
 *
 * Return: Valid pointer on success, else error pointer.
 */
static
struct ethosn_inference *inference_create(struct ethosn_network *network,
					  struct ethosn_inference_req *ifr_req)
{
	struct ethosn_inference *inference;
	int ret;
	struct device *dev = net_to_dev(network);

	if ((ifr_req->num_inputs != network->num_inputs) ||
	    (ifr_req->num_outputs != network->num_outputs)) {
		dev_err(dev, "Input/output mismatch: %d != %d or %d != %d",
			ifr_req->num_inputs, network->num_inputs,
			ifr_req->num_outputs, network->num_outputs);

		return ERR_PTR(-EINVAL);
	}

	inference = devm_kzalloc(dev, sizeof(*inference), GFP_KERNEL);
	if (!inference)
		return ERR_PTR(-ENOMEM);

	get_network(network);

	inference->network = network;
	inference->status = ETHOSN_INFERENCE_SCHEDULED;
	init_waitqueue_head(&inference->poll_wqh);

	inference->inputs = read_buffer_fds(network,
					    ifr_req->num_inputs,
					    ifr_req->input_fds,
					    network->inputs);
	if (IS_ERR(inference->inputs)) {
		ret = PTR_ERR(inference->inputs);
		goto err_free_inference;
	}

	inference->outputs = read_buffer_fds(network,
					     ifr_req->num_outputs,
					     ifr_req->output_fds,
					     network->outputs);
	if (IS_ERR(inference->outputs)) {
		ret = PTR_ERR(inference->outputs);
		goto err_free_inference;
	}

	return inference;

err_free_inference:
	free_inference(inference);

	return ERR_PTR(ret);
}

static int inference_release(struct inode *inode,
			     struct file *filep)
{
	struct ethosn_inference *inference = filep->private_data;
	struct ethosn_core *core = inference->core;

	dev_dbg(core->dev, "%s handle = 0x%pK\n", __func__, inference);

	/*
	 * Prevent concurrency problems with the inference finishing
	 * in parallel with this function.
	 * Note we don't use mutex_lock_interruptible here as we need to make
	 * sure we release the network so we don't leak resources.
	 * This would prevent the kernel module from being unloaded
	 * when requested.
	 */
	mutex_lock(&core->mutex);

	if (inference->status == ETHOSN_INFERENCE_SCHEDULED) {
		/*
		 * Use the same mutex that is used for adding
		 * inference to the list.
		 */
		struct ethosn_device *ethosn = inference->network->ethosn;

		mutex_lock(
			&ethosn->queue.inference_queue_mutex);

		/* Inference might be running or completed by now */
		if (inference->status == ETHOSN_INFERENCE_SCHEDULED)
			list_del(&inference->queue_node);

		mutex_unlock(
			&ethosn->queue.inference_queue_mutex);
	}

	if (inference->status == ETHOSN_INFERENCE_RUNNING) {
		/* Inference might be completed by now */
		dev_warn(core->dev,
			 "Reset Ethos-N due to error inference abort. handle=0x%pK\n",
			 inference);

		(void)ethosn_reset_and_start_ethosn(core,
						    core->set_alloc_id);
		ethosn_set_inference_done(core, inference,
					  ETHOSN_INFERENCE_ERROR, 0);
	}

	mutex_unlock(&core->mutex);

	wake_up_poll(&inference->poll_wqh, EPOLLHUP);

	free_inference(inference);

	return 0;
}

static __poll_t inference_poll(struct file *file,
			       poll_table *wait)
{
	struct ethosn_inference *inference = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &inference->poll_wqh, wait);

	if ((inference->status < ETHOSN_INFERENCE_SCHEDULED) ||
	    (inference->status >= ETHOSN_INFERENCE_ERROR))
		ret = EPOLLERR;

	else if (inference->status > ETHOSN_INFERENCE_RUNNING)
		ret = EPOLLIN;

	return ret;
}

static ssize_t inference_read(struct file *file,
			      char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	struct ethosn_inference *inference = file->private_data;

	if (WARN_ON((inference->status < ETHOSN_INFERENCE_SCHEDULED) ||
		    (inference->status > ETHOSN_INFERENCE_ERROR)))
		return -EINVAL;

	if (count != sizeof(inference->status))
		return -EINVAL;

	return put_user(inference->status,
			(int32_t __user *)buf) ? -EFAULT :
	       sizeof(inference->status);
}

/**
 * inference_ioctl() - Take inference command from user space
 * @filep: File struct
 * @cmd: User command
 * * ETHOSN_IOCTL_GET_CYCLE_COUNT
 *
 * Return:
 * * Zero on success
 * * Negative error code on failure
 */
static long inference_ioctl(struct file *filep,
			    unsigned int cmd,
			    unsigned long arg)
{
	struct ethosn_inference *inference = filep->private_data;
	void __user *udata = (void __user *)arg;
	int ret;

	switch (cmd) {
	case ETHOSN_IOCTL_GET_CYCLE_COUNT: {
		if (copy_to_user(udata, &inference->cycle_count,
				 sizeof(inference->cycle_count)))
			return -EFAULT;

		ret = 0;

		break;
	}
	default: {
		ret = -EINVAL;
	}
	}

	return ret;
}

/**
 * ethosn_inference_register() - Create an inference job
 *
 * Return: File descriptor on success, else error code.
 */
static int ethosn_inference_register(struct ethosn_network *network,
				     struct ethosn_inference_req *req)
{
	static const struct file_operations inference_fops = {
		.owner          = THIS_MODULE,
		.release        = &inference_release,
		.poll           = &inference_poll,
		.read           = &inference_read,
		.unlocked_ioctl = &inference_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl   = &inference_ioctl,
#endif
	};
	int i = 0;
	bool found = false;
	struct ethosn_device *ethosn = network->ethosn;
	struct ethosn_core *core = ethosn->core[0];
	struct ethosn_inference *inference;
	int ret_fd, ret;
	struct device *dev = net_to_dev(network);

	inference = inference_create(network, req);
	if (IS_ERR(inference))
		return PTR_ERR(inference);

	if (network->asset_allocator->is_protected) {
		for (i = 0; i < req->num_inputs; ++i)
			if (!inference->inputs[i]->dma_info->imported) {
				dev_err(dev,
					"Only imported input buffers are allowed in protected context\n");
				ret_fd = -EPERM;
				goto err_free_inference;
			}

		for (i = 0; i < req->num_outputs; ++i)
			if (!inference->outputs[i]->dma_info->imported) {
				dev_err(dev,
					"Only imported output buffers are allowed in protected context\n");
				ret_fd = -EPERM;
				goto err_free_inference;
			}
	}

	/* Verify that all buffers have the same asset allocator. Must be done
	 * regardless of context
	 */
	for (i = 0; i < req->num_inputs; ++i)
		if (inference->inputs[i]->asset_allocator !=
		    network->asset_allocator) {
			dev_err(dev,
				"Input buffer %d doesn't have the same asset allocator as the network\n",
				i);
			ret_fd = -EPERM;
			goto err_free_inference;
		}

	for (i = 0; i < req->num_outputs; ++i)
		if (inference->outputs[i]->asset_allocator !=
		    network->asset_allocator) {
			dev_err(dev,
				"Output buffer %d doesn't have the same asset allocator as the network\n",
				i);
			ret_fd = -EPERM;
			goto err_free_inference;
		}

	ret_fd = anon_inode_getfd("ethosn-inference",
				  &inference_fops,
				  inference,
				  O_RDONLY | O_CLOEXEC);

	if (ret_fd < 0)
		goto err_free_inference;

	dev_dbg(dev, "Registered %sprotected inference. handle=0x%pK\n",
		network->asset_allocator->is_protected ? "" : "non-",
		inference);

	ret = mutex_lock_interruptible(
		&ethosn->queue.inference_queue_mutex);
	/* Return the file descriptor */
	if (ret) {
		/* Queue node hasn't been added to the list.
		 * Make sure that nothing is removed from the list on release
		 */
		inference->status = ETHOSN_INFERENCE_ERROR;
		goto end;
	}

	/* Queue and schedule inference. */
	list_add_tail(&inference->queue_node, &ethosn->queue.inference_queue);

	mutex_unlock(&ethosn->queue.inference_queue_mutex);

	for (i = 0; i < ethosn->num_cores && !found; ++i) {
		/* Check the status of the core
		 */
		core = ethosn->core[i];

		ret = mutex_lock_interruptible(&core->mutex);

		/* Return the file descriptor */
		if (ret)
			goto end;

		if (core->current_inference == NULL) {
			found = true;
			ethosn_schedule_queued_inference(core);
		}

		mutex_unlock(&core->mutex);
	}

	if (!found)
		dev_dbg(ethosn->dev,
			"Could not find any free core. Total cores = %d\n",
			ethosn->num_cores);

end:

	return ret_fd;

err_free_inference:
	free_inference(inference);

	return ret_fd;
}

/**
 * network_ioctl() - Take network command from user space
 * @filep: File struct
 * @cmd: User command
 * * ETHOSN_IOCTL_SCHEDULE_INFERENCE
 * * ETHOSN_IOCTL_GET_INTERMEDIATE_BUFFER
 *
 * Return:
 * * Inference file descriptor on success
 * * Negative error code on failure
 */
static long network_ioctl(struct file *filep,
			  unsigned int cmd,
			  unsigned long arg)
{
	struct ethosn_network *network = filep->private_data;
	const void __user *udata = (void __user *)arg;
	int ret;
	u64 time;
	struct device *dev = net_to_dev(network);

	time = ktime_get_ns();

	switch (cmd) {
	case ETHOSN_IOCTL_SCHEDULE_INFERENCE: {
		struct ethosn_inference_req infer_req;

		if (copy_from_user(&infer_req, udata, sizeof(infer_req))) {
			ret = -EFAULT;
			break;
		}

		ret = ethosn_inference_register(network, &infer_req);

		dev_dbg(dev, "SCHEDULE_INFERENCE: time %llu",
			time);

		break;
	}
	case ETHOSN_IOCTL_GET_INTERMEDIATE_BUFFER: {
		if (network->asset_allocator->is_protected) {
			dev_dbg(
				dev,
				"Not allowed to get intermediate buffers while in protected context\n");
			ret = -EPERM;
			break;
		}

		if (network->ethosn->num_cores > 1)
			dev_warn(dev,
				 "Intermediate buffer for multi-core system: core 0 will be returned.");

		ethosn_dma_sync_for_cpu(network->asset_allocator,
					network->intermediate_data[0]);

		ret = ethosn_get_dma_view_fd(network->ethosn,
					     network->asset_allocator,
					     network->intermediate_data[0]);
		break;
	}
	default: {
		ret = -EINVAL;
	}
	}

	return ret;
}

static int init_bindings(struct ethosn_network *network,
			 uint32_t core_id,
			 u32 num_binfos,
			 const struct ethosn_buffer_info __user *binfos_user,
			 ethosn_address_t container_start,
			 ethosn_address_t container_size,
			 bool check_in_container,
			 struct ethosn_buffer_info **binfos_save,
			 enum ethosn_buffer_type buffer_type)
{
	struct ethosn_buffer_info *binfos;
	size_t binfos_size;
	int ret;
	struct device *dev = net_to_dev(network);

	binfos_size = num_binfos * sizeof(*binfos);
	binfos = devm_kzalloc(dev, binfos_size, GFP_KERNEL);

	if (!binfos) {
		ret = -ENOMEM;
		goto out_clean_binfos;
	}

	if (copy_from_user(binfos, binfos_user, binfos_size)) {
		dev_err(dev, "Error reading binfos\n");
		ret = -EFAULT;
		goto out_free_binfos;
	}

	ret = update_bindings(network,
			      core_id,
			      num_binfos,
			      binfos,
			      container_start,
			      container_size,
			      true,
			      check_in_container,
			      buffer_type);

	if (ret || !binfos_save)
		goto out_free_binfos;

	*binfos_save = binfos;

	return ret;

out_free_binfos:
	devm_kfree(dev, binfos);
out_clean_binfos:
	if (binfos_save)
		*binfos_save = NULL;

	return ret;
}

static int init_inference_data(struct ethosn_network *network,
			       struct ethosn_core *core,
			       u32 num_bindings,
			       struct ethosn_network_req *net_req,
			       uint32_t core_id)
{
	u32 i;
	int ret;
	struct ethosn_buffer_array *buffers =
		get_inference_header(network, core_id);
	struct device *dev = net_to_dev(network);

	buffers->num_buffers = num_bindings;

	for (i = 0; i < num_bindings; ++i)
		memset(&buffers->buffers[i], 0, sizeof(buffers->buffers[i]));

	if (network->constant_dma_data) {
		ethosn_dma_sync_for_device(network->asset_allocator,
					   network->constant_dma_data);
		ret = init_bindings(network,
				    core_id,
				    net_req->dma_buffers.num,
				    net_req->dma_buffers.info,
				    network->constant_dma_data->iova_addr,
				    net_req->dma_data.size,
				    true,
				    NULL,
				    ETHOSN_BUFFER_CONSTANT);
		if (ret)
			return ret;
	}

	ethosn_dma_sync_for_device(network->asset_allocator,
				   network->constant_cu_data);
	ret = init_bindings(network,
			    core_id,
			    net_req->cu_buffers.num,
			    net_req->cu_buffers.info,
			    to_ethosn_addr(network->constant_cu_data->iova_addr,
					   &core->dma_map),
			    net_req->cu_data.size,
			    true,
			    NULL,
			    ETHOSN_BUFFER_CMD_FW);
	if (ret)
		return ret;

	ret = init_bindings(network,
			    core_id,
			    net_req->intermediate_desc.buffers.num,
			    net_req->intermediate_desc.buffers.info,
			    0,
			    0,
			    false,
			    &network->intermediates,
			    ETHOSN_BUFFER_INTERMEDIATE);
	if (ret)
		return ret;

	network->num_intermediates =
		net_req->intermediate_desc.buffers.num;

	ret = init_bindings(network,
			    core_id,
			    net_req->input_buffers.num,
			    net_req->input_buffers.info,
			    0,
			    0,
			    false,
			    &network->inputs,
			    ETHOSN_BUFFER_INPUT);
	if (ret)
		return ret;

	network->num_inputs = net_req->input_buffers.num;

	for (i = 0; i < network->num_inputs; ++i) {
		if (network->inputs[i].offset != 0)
			dev_warn(dev, "Ignored input offset %u\n",
				 network->inputs[i].offset);

		network->inputs[i].offset = 0;
	}

	ret = init_bindings(network,
			    core_id,
			    net_req->output_buffers.num,
			    net_req->output_buffers.info,
			    0,
			    0,
			    false,
			    &network->outputs,
			    ETHOSN_BUFFER_OUTPUT);
	if (ret)
		return ret;

	network->num_outputs = net_req->output_buffers.num;

	for (i = 0; i < network->num_outputs; ++i) {
		if (network->outputs[i].offset != 0)
			dev_warn(dev, "Ignored output offset %u\n",
				 network->outputs[i].offset);

		network->outputs[i].offset = 0;
	}

	for (i = 0; i < num_bindings; ++i)
		if (buffers->buffers[i].size == 0) {
			dev_err(dev, "Missing inference binding id\n");

			return -EINVAL;
		}

	return 0;
}

static int import_intermediate_data(struct ethosn_network *network,
				    struct ethosn_network_req *req,
				    u32 num_bindings)
{
	int ret = -ENOMEM;
	int i = 0;
	int num_cores = network->ethosn->num_cores;
	struct device *dev = net_to_dev(network);

	if (!network->ethosn->smmu_available) {
		dev_dbg(dev,
			"Cannot import intermediate buffer. SMMU not available\n");

		return -ENODEV;
	}

	/* The data size must be greater than zero */
	if (!req->intermediate_desc.memory.dma_req.size) {
		dev_dbg(dev,
			"Importing intermediate buffers with zero size isn't allowed!\n");

		return -EINVAL;
	}

	/* When importing intermediate data the memory will be shared between
	 * the cores. So import and map it for core 0, then assign the same
	 * memory to all cores. This way the cleanup if something goes wrong
	 * will still work.
	 */
	network->intermediate_data[0] =
		ethosn_dma_import(
			network->asset_allocator,
			req->intermediate_desc.memory.dma_req.fd,
			req->intermediate_desc.memory.dma_req.size,
			ETHOSN_STREAM_INTERMEDIATE_BUFFER);
	if (IS_ERR_OR_NULL(network->intermediate_data[0])) {
		dev_dbg(dev, "DMA import of intermediate buffer failed\n");

		return ret;
	}

	ret = ethosn_dma_map(
		network->asset_allocator,
		network->intermediate_data[0],
		ETHOSN_PROT_READ |
		ETHOSN_PROT_WRITE);

	if (ret < 0) {
		dev_dbg(dev, "DMA mapping of intermediate buffer failed\n");

		return ret;
	}

	/* Assign the imported intermediate data from core 0 to all cores */
	for (i = 1; i < num_cores; i++)

		network->intermediate_data[i] =
			network->intermediate_data[0];

	return ret;
}

static int alloc_intermediate_data(struct ethosn_network *network,
				   struct ethosn_network_req *req,
				   u32 num_bindings)
{
	int i = 0;
	int num_cores = network->ethosn->num_cores;
	struct device *dev = net_to_dev(network);

	if (network->asset_allocator->is_protected) {
		dev_dbg(dev,
			"Not allowed to allocate intermediate buffers while in protected context\n");

		return -EPERM;
	}

	/* The data size must be greater than zero */
	if (!req->intermediate_desc.memory.data_size) {
		dev_dbg(dev,
			"Allocating intermediate buffers with zero size isn't allowed!\n");

		return -EINVAL;
	}

	for (i = 0; i < num_cores; ++i) {
		network->intermediate_data[i] =
			ethosn_dma_alloc_and_map(
				network->asset_allocator,
				req->intermediate_desc.memory.data_size,
				ETHOSN_PROT_READ | ETHOSN_PROT_WRITE,
				ETHOSN_STREAM_INTERMEDIATE_BUFFER,
				GFP_KERNEL,
				"network-intermediate-data");

		if (IS_ERR_OR_NULL(network->intermediate_data[i])) {
			dev_dbg(dev,
				"DMA alloc and map of network-intermediate-data failed\n");

			return -ENOMEM;
		}
	}

	return 0;
}

static int alloc_init_inference_data(struct ethosn_network *network,
				     struct ethosn_network_req *req)
{
	u32 num_bindings;
	size_t size;
	int ret = -ENOMEM;
	int i = 0;
	int num_cores = network->ethosn->num_cores;
	struct device *dev = net_to_dev(network);

	num_bindings = req->cu_buffers.num;
	num_bindings += req->dma_buffers.num;
	num_bindings += req->intermediate_desc.buffers.num;
	num_bindings += req->input_buffers.num;
	num_bindings += req->output_buffers.num;

	size = sizeof(struct ethosn_buffer_array) + num_bindings *
	       sizeof(struct ethosn_buffer_desc);

	/*
	 * The inference data (which is ethosn_buffer_array) needs to be
	 * allocated per core. The reason being each core may have a
	 * unique entry for the "intermediate data" inside the
	 * ethosn_buffer_array.
	 */
	network->inference_data =
		devm_kzalloc(dev,
			     (sizeof(*(network->inference_data)) * num_cores),
			     GFP_KERNEL);
	if (!network->inference_data)
		return ret;

	for (i = 0; i < num_cores; i++) {
		network->inference_data[i] =
			ethosn_dma_alloc_and_map(
				network->asset_allocator,
				size, ETHOSN_PROT_READ,
				ETHOSN_STREAM_COMMAND_STREAM,
				GFP_KERNEL,
				"network-inference-data");
		if (IS_ERR_OR_NULL(network->inference_data[i])) {
			dev_dbg(dev,
				"DMA alloc and map of network-inference-data failed\n");
			ret = -ENOMEM;
			goto out_free_inference_data;
		}
	}

	/*
	 * Each core may need intermediate data. It reads/writes to
	 * this data during the execution of an inference.
	 */

	network->intermediate_data =
		devm_kzalloc(dev,
			     (sizeof(*(network->intermediate_data)) * num_cores),
			     GFP_KERNEL);

	if (!network->intermediate_data) {
		ret = -ENOMEM;
		goto out_free_inference_data;
	}

	if (req->intermediate_desc.buffers.num) {
		/* If there are intermediate buffers, then allocate or import
		 * memory for them.
		 */
		if (req->intermediate_desc.memory.type == IMPORT)
			ret = import_intermediate_data(network, req,
						       num_bindings);
		else
			ret = alloc_intermediate_data(network, req,
						      num_bindings);
	} else {
		ret = 0;
	}

	if (!ret)
		for (i = 0; i < num_cores; i++) {
			ret = init_inference_data(network,
						  network->ethosn->core[i],
						  num_bindings,
						  req, i);

			if (ret) {
				dev_dbg(dev, "Init inference data failed\n");

				goto out_free_intermediate_data;
			}
		}

	return ret;

out_free_intermediate_data:
	for (i = 0; i < num_cores; i++)
		/* Free allocated DMA memory from core */
		/* Intermediate and inference data exist per core */
		if (network->intermediate_data[i])
			ethosn_dma_unmap_and_release(
				network->asset_allocator,
				&network->intermediate_data[i]);

	devm_kfree(dev, network->intermediate_data);
	network->intermediate_data = NULL;

out_free_inference_data:

	for (i = 0; i < num_cores; i++)
		/* Free allocated DMA memory from core */
		/* Intermediate and inference data exist per core */
		if (network->inference_data[i])
			ethosn_dma_unmap_and_release(
				network->asset_allocator,
				&network->inference_data[i]);

	devm_kfree(dev, network->inference_data);
	network->inference_data = NULL;

	return ret;
}

static void free_network(struct ethosn_network *network)
{
	int i = 0;
	struct ethosn_device *ethosn = network->ethosn;
	struct device *dev = net_to_dev(network);

	dev_dbg(dev, "Released network. handle=0x%pK\n", network);

	/* Unmap virtual addresses from core */
	/* Constant data shared between cores */
	if (network->constant_dma_data)
		ethosn_dma_unmap(network->asset_allocator,
				 network->constant_dma_data);

	ethosn_dma_unmap(network->asset_allocator,
			 network->constant_cu_data);

	for (i = 0; i < ethosn->num_cores; i++) {
		/* Free allocated DMA memory from core */
		/* Intermediate and inference data exist per core */
		if (network->intermediate_data)
			ethosn_dma_unmap_and_release(
				network->asset_allocator,
				&network->intermediate_data[i]);

		if (network->inference_data)
			ethosn_dma_unmap_and_release(
				network->asset_allocator,
				&network->inference_data[i]);
	}

	/* Free allocated dma from top level device */
	if (network->constant_dma_data)
		ethosn_dma_release(network->asset_allocator,
				   &network->constant_dma_data);

	ethosn_dma_release(network->asset_allocator,
			   &network->constant_cu_data);

	devm_kfree(dev, network->intermediate_data);
	devm_kfree(dev, network->inference_data);
	devm_kfree(dev, network->intermediates);
	devm_kfree(dev, network->inputs);
	devm_kfree(dev, network->outputs);

	put_device(dev);

	devm_kfree(dev, network);
}

/**
 * create_network() - Create a new network
 * @ethosn: Ethos-N device
 * @net_req: Network description
 * @asset_alloc: Pointer to asset_allocator object
 *
 * Return: Network pointer on success, else error code.
 */
static
struct ethosn_network *create_network(struct ethosn_device *ethosn,
				      struct ethosn_network_req *net_req,
				      struct ethosn_dma_allocator *asset_alloc)
{
	/* Note:- We register network on ethosn.
	 * For carveout :- We allocate constant data. inference data
	 *                 and intermediate data on top level device.
	 *                 All the cores can access the same buffer as
	 *                 the complete carveout memory is shared.
	 * For smmu :- The constant data is allocated on parent and
	 *             mapped on all cores. Inference data and
	 *             intermediate data are allocated and mapped
	 *             on all cores.
	 */
	struct ethosn_network *network;
	int ret = -ENOMEM;
	struct device *dev = ethosn->dev;

	network = devm_kzalloc(dev, sizeof(*network), GFP_KERNEL);
	if (!network)
		return ERR_PTR(-ENOMEM);

	network->ethosn = ethosn;
	network->asset_allocator = asset_alloc;

	/* Increment ref-count on device. Not sure why this is necessary,
	 * but it needs to be before any potential failures so that when we
	 * decrement the ref-count in free_network we can rely on it having been
	 * previously incremented.
	 */
	get_device(dev);

	if (net_req->dma_data.size > 0) {
		network->constant_dma_data = ethosn_dma_alloc(
			asset_alloc,
			net_req->dma_data.size,
			ETHOSN_STREAM_WEIGHT_DATA,
			GFP_KERNEL,
			"network-constant-dma-data");

		if (IS_ERR_OR_NULL(network->constant_dma_data)) {
			ret = -ENOMEM;
			goto err_free_network;
		}

		ret = ethosn_dma_map(
			asset_alloc,
			network->constant_dma_data,
			ETHOSN_PROT_READ);
		if (ret)
			goto err_free_const_dma_data;

		if (copy_from_user(network->constant_dma_data->cpu_addr,
				   net_req->dma_data.data,
				   net_req->dma_data.size)) {
			dev_err(dev, "Error reading constant dma data\n");
			ret = -EINVAL;
			goto err_unmap_const_dma_data;
		}
	}

	network->constant_cu_data =
		ethosn_dma_alloc(asset_alloc,
				 net_req->cu_data.size,
				 ETHOSN_STREAM_COMMAND_STREAM,
				 GFP_KERNEL,
				 "network-constant-cu-data");
	if (IS_ERR_OR_NULL(network->constant_cu_data)) {
		ret = -ENOMEM;
		goto err_unmap_const_dma_data;
	}

	ret = ethosn_dma_map(asset_alloc,
			     network->constant_cu_data,
			     ETHOSN_PROT_READ);
	if (ret)
		goto err_free_const_cu_data;

	if (copy_from_user(network->constant_cu_data->cpu_addr,
			   net_req->cu_data.data,
			   net_req->cu_data.size)) {
		dev_err(dev, "Error reading constant cu data\n");
		ret = -EINVAL;
		goto err_unmap_const_cu_data;
	}

	ret = alloc_init_inference_data(network, net_req);
	if (ret)
		goto err_unmap_const_cu_data;

	return network;

err_unmap_const_cu_data:
	ethosn_dma_unmap(network->asset_allocator,
			 network->constant_cu_data);
err_free_const_cu_data:
	ethosn_dma_release(network->asset_allocator,
			   &network->constant_cu_data);

err_unmap_const_dma_data:
	if (network->constant_dma_data)
		ethosn_dma_unmap(network->asset_allocator,
				 network->constant_dma_data);

err_free_const_dma_data:
	if (network->constant_dma_data)
		ethosn_dma_release(network->asset_allocator,
				   &network->constant_dma_data);

err_free_network:
	put_device(dev);
	devm_kfree(dev, network);

	return ERR_PTR(ret);
}

static int network_release(struct inode *inode,
			   struct file *filep)
{
	struct ethosn_network *network = filep->private_data;
	struct ethosn_device *ethosn = network->ethosn;
	struct ethosn_dma_allocator *asset_allocator;

	/* Note we don't use mutex_lock_interruptible here as we need to make
	 * sure we release the network so we don't leak resources.
	 * This would prevent the kernel module from being unloaded
	 * when requested.
	 */
	mutex_lock(&ethosn->mutex);

	asset_allocator = network->asset_allocator;

	free_network(network);

	dev_dbg(ethosn->dev,
		"Release network asset_allocator->pid = %d",
		asset_allocator->pid);

	if (asset_allocator->pid != ETHOSN_INVALID_PID)
		ethosn_asset_allocator_put(asset_allocator);

	mutex_unlock(&ethosn->mutex);

	return 0;
}

/**
 * ethosn_network_register() - Create a network
 * @ethosn:	Ethos-N device
 * @asset_allocator: Pointer to the asset_allocator object
 * @net_req:	Network description
 *
 * Return: FD on success, else error code
 */
int ethosn_network_register(struct ethosn_device *ethosn,
			    struct ethosn_dma_allocator *asset_allocator,
			    struct ethosn_network_req *net_req)
{
	static const struct file_operations network_fops = {
		.owner          = THIS_MODULE,
		.release        = &network_release,
		.unlocked_ioctl = &network_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl   = &network_ioctl,
#endif
	};

	struct ethosn_network *network;
	int fd;

	if (!asset_allocator) {
		dev_err(ethosn->dev, "Asset allocator NULL\n");

		return -EINVAL;
	}

	network = create_network(ethosn, net_req, asset_allocator);

	if (IS_ERR(network))
		return PTR_ERR(network);

	fd = anon_inode_getfd("ethosn-network",
			      &network_fops,
			      network,
			      O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		free_network(network);

		return fd;
	}

	network->file = fget(fd);
	fput(network->file);

	if (asset_allocator->pid != ETHOSN_INVALID_PID)
		ethosn_asset_allocator_get(asset_allocator);

	dev_dbg(ethosn->dev,
		"Registered %sprotected network. handle=0x%pK\n",
		network->asset_allocator->is_protected ? "" : "non-", network);

	return fd;
}

void ethosn_set_inference_done(struct ethosn_core *core,
			       struct ethosn_inference *inference,
			       int new_status,
			       u64 cycle_count)
{
	WARN_ON(new_status != ETHOSN_INFERENCE_COMPLETED &&
		new_status != ETHOSN_INFERENCE_ERROR);

	/* The inference may have been cancelled (by the user, or by power
	 * management),
	 * but finishes anyway due to parallelism.
	 * The ethosn_inference object may not even be valid at this point, so
	 * we can't even dereference the pointer.
	 * Therefore we ignore this call, as the inference will have already
	 * been re-scheduled
	 * if necessary, so there is nothing for us to do.
	 */
	if (core->current_inference != inference) {
		dev_dbg(core->dev,
			"%s: inference 0x%pK ignored because it is no longer the current inference.\n",
			__func__, inference);

		return;
	}

	inference->status = new_status;
	inference->cycle_count = cycle_count;

	wake_up_poll(&inference->poll_wqh, EPOLLIN);

	dev_dbg(core->dev,
		"END_INFERENCE: inference 0x%pK time %llu on core_id = %u",
		inference, ktime_get_ns(), core->core_id);

	pm_runtime_mark_last_busy(core->dev);
	pm_runtime_put(core->dev);

	/* Reset current running inference. */
	core->current_inference = NULL;

	/* Schedule next queued inference. */
	ethosn_schedule_queued_inference(core);
}
