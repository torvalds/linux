/*
 *  intel_sst_interface.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *  Harsha Priya <priya.harsha@intel.com>
 *  Dharageswari R <dharageswari.r@intel.com>
 *  Jeeja KP <jeeja.kp@intel.com>
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This driver exposes the audio engine functionalities to the ALSA
 *	and middleware.
 *  Upper layer interfaces (MAD driver, MMF) to SST driver
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/ioctl.h>
#ifdef CONFIG_MRST_RAR_HANDLER
#include <linux/rar_register.h>
#include "../../../drivers/staging/memrar/memrar.h"
#endif
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intel_sst_fw_ipc.h"
#include "intel_sst_common.h"

#define AM_MODULE 1
#define STREAM_MODULE 0


/**
* intel_sst_check_device - checks SST device
*
* This utility function checks the state of SST device and downlaods FW if
* not done, or resumes the device if suspended
*/

static int intel_sst_check_device(void)
{
	int retval = 0;
	if (sst_drv_ctx->pmic_state != SND_MAD_INIT_DONE) {
		pr_warn("Sound card not available\n");
		return -EIO;
	}
	if (sst_drv_ctx->sst_state == SST_SUSPENDED) {
		pr_debug("Resuming from Suspended state\n");
		retval = intel_sst_resume(sst_drv_ctx->pci);
		if (retval) {
			pr_debug("Resume Failed= %#x,abort\n", retval);
			return retval;
		}
	}

	if (sst_drv_ctx->sst_state == SST_UN_INIT) {
		/* FW is not downloaded */
		retval = sst_download_fw();
		if (retval)
			return -ENODEV;
		if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID) {
			retval = sst_drv_ctx->rx_time_slot_status;
			if (retval != RX_TIMESLOT_UNINIT
					&& sst_drv_ctx->pmic_vendor != SND_NC)
				sst_enable_rx_timeslot(retval);
		}
	}
	return 0;
}

/**
 * intel_sst_open - opens a handle to driver
 *
 * @i_node:	inode structure
 * @file_ptr:pointer to file
 *
 * This function is called by OS when a user space component
 * tries to get a driver handle. Only one handle at a time
 * will be allowed
 */
int intel_sst_open(struct inode *i_node, struct file *file_ptr)
{
	unsigned int retval;

	mutex_lock(&sst_drv_ctx->stream_lock);
	pm_runtime_get_sync(&sst_drv_ctx->pci->dev);
	retval = intel_sst_check_device();
	if (retval) {
		pm_runtime_put(&sst_drv_ctx->pci->dev);
		mutex_unlock(&sst_drv_ctx->stream_lock);
		return retval;
	}

	if (sst_drv_ctx->encoded_cnt < MAX_ENC_STREAM) {
		struct ioctl_pvt_data *data =
			kzalloc(sizeof(struct ioctl_pvt_data), GFP_KERNEL);
		if (!data) {
			pm_runtime_put(&sst_drv_ctx->pci->dev);
			mutex_unlock(&sst_drv_ctx->stream_lock);
			return -ENOMEM;
		}

		sst_drv_ctx->encoded_cnt++;
		mutex_unlock(&sst_drv_ctx->stream_lock);
		data->pvt_id = sst_assign_pvt_id(sst_drv_ctx);
		data->str_id = 0;
		file_ptr->private_data = (void *)data;
		pr_debug("pvt_id handle = %d!\n", data->pvt_id);
	} else {
		retval = -EUSERS;
		pm_runtime_put(&sst_drv_ctx->pci->dev);
		mutex_unlock(&sst_drv_ctx->stream_lock);
	}
	return retval;
}

/**
 * intel_sst_open_cntrl - opens a handle to driver
 *
 * @i_node:	inode structure
 * @file_ptr:pointer to file
 *
 * This function is called by OS when a user space component
 * tries to get a driver handle to /dev/intel_sst_control.
 * Only one handle at a time will be allowed
 * This is for control operations only
 */
int intel_sst_open_cntrl(struct inode *i_node, struct file *file_ptr)
{
	unsigned int retval;

	/* audio manager open */
	mutex_lock(&sst_drv_ctx->stream_lock);
	pm_runtime_get_sync(&sst_drv_ctx->pci->dev);
	retval = intel_sst_check_device();
	if (retval) {
		pm_runtime_put(&sst_drv_ctx->pci->dev);
		mutex_unlock(&sst_drv_ctx->stream_lock);
		return retval;
	}

	if (sst_drv_ctx->am_cnt < MAX_AM_HANDLES) {
		sst_drv_ctx->am_cnt++;
		pr_debug("AM handle opened...\n");
		file_ptr->private_data = NULL;
	} else {
		retval = -EACCES;
		pm_runtime_put(&sst_drv_ctx->pci->dev);
	}

	mutex_unlock(&sst_drv_ctx->stream_lock);
	return retval;
}

/**
 * intel_sst_release - releases a handle to driver
 *
 * @i_node:	inode structure
 * @file_ptr:	pointer to file
 *
 * This function is called by OS when a user space component
 * tries to release a driver handle.
 */
int intel_sst_release(struct inode *i_node, struct file *file_ptr)
{
	struct ioctl_pvt_data *data = file_ptr->private_data;

	pr_debug("Release called, closing app handle\n");
	mutex_lock(&sst_drv_ctx->stream_lock);
	sst_drv_ctx->encoded_cnt--;
	sst_drv_ctx->stream_cnt--;
	pm_runtime_put(&sst_drv_ctx->pci->dev);
	mutex_unlock(&sst_drv_ctx->stream_lock);
	free_stream_context(data->str_id);
	kfree(data);
	return 0;
}

int intel_sst_release_cntrl(struct inode *i_node, struct file *file_ptr)
{
	/* audio manager close */
	mutex_lock(&sst_drv_ctx->stream_lock);
	sst_drv_ctx->am_cnt--;
	pm_runtime_put(&sst_drv_ctx->pci->dev);
	mutex_unlock(&sst_drv_ctx->stream_lock);
	pr_debug("AM handle closed\n");
	return 0;
}

/**
* intel_sst_mmap - mmaps a kernel buffer to user space for copying data
*
* @vma:		vm area structure instance
* @file_ptr:	pointer to file
*
* This function is called by OS when a user space component
* tries to get mmap memory from driver
*/
int intel_sst_mmap(struct file *file_ptr, struct vm_area_struct *vma)
{
	int retval, length;
	struct ioctl_pvt_data *data =
		(struct ioctl_pvt_data *)file_ptr->private_data;
	int str_id = data->str_id;
	void *mem_area;

	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;

	length = vma->vm_end - vma->vm_start;
	pr_debug("called for stream %d length 0x%x\n", str_id, length);

	if (length > sst_drv_ctx->mmap_len)
		return -ENOMEM;
	if (!sst_drv_ctx->mmap_mem)
		return -EIO;

	/* round it up to the page boundary  */
	/*mem_area = (void *)((((unsigned long)sst_drv_ctx->mmap_mem)
				+ PAGE_SIZE - 1) & PAGE_MASK);*/
	mem_area = (void *) PAGE_ALIGN((unsigned int) sst_drv_ctx->mmap_mem);

	/* map the whole physically contiguous area in one piece  */
	retval = remap_pfn_range(vma,
			vma->vm_start,
			virt_to_phys((void *)mem_area) >> PAGE_SHIFT,
			length,
			vma->vm_page_prot);
	if (retval)
		sst_drv_ctx->streams[str_id].mmapped = false;
	else
		sst_drv_ctx->streams[str_id].mmapped = true;

	pr_debug("mmap ret 0x%x\n", retval);
	return retval;
}

/* sets mmap data buffers to play/capture*/
static int intel_sst_mmap_play_capture(u32 str_id,
		struct snd_sst_mmap_buffs *mmap_buf)
{
	struct sst_stream_bufs *bufs;
	int retval, i;
	struct stream_info *stream;
	struct snd_sst_mmap_buff_entry *buf_entry;
	struct snd_sst_mmap_buff_entry *tmp_buf;

	pr_debug("called for str_id %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;

	stream = &sst_drv_ctx->streams[str_id];
	if (stream->mmapped != true)
		return -EIO;

	if (stream->status == STREAM_UN_INIT ||
		stream->status == STREAM_DECODE) {
		return -EBADRQC;
	}
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;

	tmp_buf = kcalloc(mmap_buf->entries, sizeof(*tmp_buf), GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;
	if (copy_from_user(tmp_buf, (void __user *)mmap_buf->buff,
			mmap_buf->entries * sizeof(*tmp_buf))) {
		retval = -EFAULT;
		goto out_free;
	}

	pr_debug("new buffers count %d status %d\n",
			mmap_buf->entries, stream->status);
	buf_entry = tmp_buf;
	for (i = 0; i < mmap_buf->entries; i++) {
		bufs = kzalloc(sizeof(*bufs), GFP_KERNEL);
		if (!bufs) {
			retval = -ENOMEM;
			goto out_free;
		}
		bufs->size = buf_entry->size;
		bufs->offset = buf_entry->offset;
		bufs->addr = sst_drv_ctx->mmap_mem;
		bufs->in_use = false;
		buf_entry++;
		/* locking here */
		mutex_lock(&stream->lock);
		list_add_tail(&bufs->node, &stream->bufs);
		mutex_unlock(&stream->lock);
	}

	mutex_lock(&stream->lock);
	stream->data_blk.condition = false;
	stream->data_blk.ret_code = 0;
	if (stream->status == STREAM_INIT &&
			stream->prev != STREAM_UN_INIT &&
			stream->need_draining != true) {
		stream->prev = stream->status;
		stream->status = STREAM_RUNNING;
		if (stream->ops == STREAM_OPS_PLAYBACK) {
			if (sst_play_frame(str_id) < 0) {
				pr_warn("play frames fail\n");
				mutex_unlock(&stream->lock);
				retval = -EIO;
				goto out_free;
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			if (sst_capture_frame(str_id) < 0) {
				pr_warn("capture frame fail\n");
				mutex_unlock(&stream->lock);
				retval = -EIO;
				goto out_free;
			}
		}
	}
	mutex_unlock(&stream->lock);
	/* Block the call for reply */
	if (!list_empty(&stream->bufs)) {
		stream->data_blk.on = true;
		retval = sst_wait_interruptible(sst_drv_ctx,
					&stream->data_blk);
	}

	if (retval >= 0)
		retval = stream->cumm_bytes;
	pr_debug("end of play/rec ioctl bytes = %d!!\n", retval);

out_free:
	kfree(tmp_buf);
	return retval;
}

/*sets user data buffers to play/capture*/
static int intel_sst_play_capture(struct stream_info *stream, int str_id)
{
	int retval;

	stream->data_blk.ret_code = 0;
	stream->data_blk.on = true;
	stream->data_blk.condition = false;

	mutex_lock(&stream->lock);
	if (stream->status == STREAM_INIT && stream->prev != STREAM_UN_INIT) {
		/* stream is started */
		stream->prev = stream->status;
		stream->status = STREAM_RUNNING;
	}

	if (stream->status == STREAM_INIT && stream->prev == STREAM_UN_INIT) {
		/* stream is not started yet */
		pr_debug("Stream isn't in started state %d, prev %d\n",
			stream->status, stream->prev);
	} else if ((stream->status == STREAM_RUNNING ||
			stream->status == STREAM_PAUSED) &&
			stream->need_draining != true) {
		/* stream is started */
		if (stream->ops == STREAM_OPS_PLAYBACK ||
				stream->ops == STREAM_OPS_PLAYBACK_DRM) {
			if (sst_play_frame(str_id) < 0) {
				pr_warn("play frames failed\n");
				mutex_unlock(&stream->lock);
				return -EIO;
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			if (sst_capture_frame(str_id) < 0) {
				pr_warn("capture frames failed\n");
				mutex_unlock(&stream->lock);
				return -EIO;
			}
		}
	} else {
		mutex_unlock(&stream->lock);
		return -EIO;
	}
	mutex_unlock(&stream->lock);
	/* Block the call for reply */

	retval = sst_wait_interruptible(sst_drv_ctx, &stream->data_blk);
	if (retval) {
		stream->status = STREAM_INIT;
		pr_debug("wait returned error...\n");
	}
	return retval;
}

/* fills kernel list with buffer addresses for SST DSP driver to process*/
static int snd_sst_fill_kernel_list(struct stream_info *stream,
			const struct iovec *iovec, unsigned long nr_segs,
			struct list_head *copy_to_list)
{
	struct sst_stream_bufs *stream_bufs;
	unsigned long index, mmap_len;
	unsigned char __user *bufp;
	unsigned long size, copied_size;
	int retval = 0, add_to_list = 0;
	static int sent_offset;
	static unsigned long sent_index;

#ifdef CONFIG_MRST_RAR_HANDLER
	if (stream->ops == STREAM_OPS_PLAYBACK_DRM) {
		for (index = stream->sg_index; index < nr_segs; index++) {
			__u32 rar_handle;
			struct sst_stream_bufs *stream_bufs =
				kzalloc(sizeof(*stream_bufs), GFP_KERNEL);

			stream->sg_index = index;
			if (!stream_bufs)
				return -ENOMEM;
			if (copy_from_user((void *) &rar_handle,
					iovec[index].iov_base,
					sizeof(__u32))) {
				kfree(stream_bufs);
				return -EFAULT;
			}
			stream_bufs->addr = (char *)rar_handle;
			stream_bufs->in_use = false;
			stream_bufs->size = iovec[0].iov_len;
			/* locking here */
			mutex_lock(&stream->lock);
			list_add_tail(&stream_bufs->node, &stream->bufs);
			mutex_unlock(&stream->lock);
		}
		stream->sg_index = index;
		return retval;
	}
#endif
	stream_bufs = kzalloc(sizeof(*stream_bufs), GFP_KERNEL);
	if (!stream_bufs)
		return -ENOMEM;
	stream_bufs->addr = sst_drv_ctx->mmap_mem;
	mmap_len = sst_drv_ctx->mmap_len;
	stream_bufs->addr = sst_drv_ctx->mmap_mem;
	bufp = stream->cur_ptr;

	copied_size = 0;

	if (!stream->sg_index)
		sent_index = sent_offset = 0;

	for (index = stream->sg_index; index < nr_segs; index++) {
		stream->sg_index = index;
		if (!stream->cur_ptr)
			bufp = iovec[index].iov_base;

		size = ((unsigned long)iovec[index].iov_base
			+ iovec[index].iov_len) - (unsigned long) bufp;

		if ((copied_size + size) > mmap_len)
			size = mmap_len - copied_size;


		if (stream->ops == STREAM_OPS_PLAYBACK) {
			if (copy_from_user((void *)
					(stream_bufs->addr + copied_size),
					bufp, size)) {
				/* Clean up the list and return error code */
				retval = -EFAULT;
				break;
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			struct snd_sst_user_cap_list *entry =
				kzalloc(sizeof(*entry), GFP_KERNEL);

			if (!entry) {
				kfree(stream_bufs);
				return -ENOMEM;
			}
			entry->iov_index = index;
			entry->iov_offset = (unsigned long) bufp -
					(unsigned long)iovec[index].iov_base;
			entry->offset = copied_size;
			entry->size = size;
			list_add_tail(&entry->node, copy_to_list);
		}

		stream->cur_ptr = bufp + size;

		if (((unsigned long)iovec[index].iov_base
				+ iovec[index].iov_len) <
				((unsigned long)iovec[index].iov_base)) {
			pr_debug("Buffer overflows\n");
			kfree(stream_bufs);
			return -EINVAL;
		}

		if (((unsigned long)iovec[index].iov_base
					+ iovec[index].iov_len) ==
					(unsigned long)stream->cur_ptr) {
			stream->cur_ptr = NULL;
			stream->sg_index++;
		}

		copied_size += size;
		pr_debug("copied_size - %lx\n", copied_size);
		if ((copied_size >= mmap_len) ||
				(stream->sg_index == nr_segs)) {
			add_to_list = 1;
		}

		if (add_to_list) {
			stream_bufs->in_use = false;
			stream_bufs->size = copied_size;
			/* locking here */
			mutex_lock(&stream->lock);
			list_add_tail(&stream_bufs->node, &stream->bufs);
			mutex_unlock(&stream->lock);
			break;
		}
	}
	return retval;
}

/* This function copies the captured data returned from SST DSP engine
 * to the user buffers*/
static int snd_sst_copy_userbuf_capture(struct stream_info *stream,
			const struct iovec *iovec,
			struct list_head *copy_to_list)
{
	struct snd_sst_user_cap_list *entry, *_entry;
	struct sst_stream_bufs *kbufs = NULL, *_kbufs;
	int retval = 0;

	/* copy sent buffers */
	pr_debug("capture stream copying to user now...\n");
	list_for_each_entry_safe(kbufs, _kbufs, &stream->bufs, node) {
		if (kbufs->in_use == true) {
			/* copy to user */
			list_for_each_entry_safe(entry, _entry,
						copy_to_list, node) {
				if (copy_to_user(iovec[entry->iov_index].iov_base + entry->iov_offset,
					     kbufs->addr + entry->offset,
					     entry->size)) {
					/* Clean up the list and return error */
					retval = -EFAULT;
					break;
				}
				list_del(&entry->node);
				kfree(entry);
			}
		}
	}
	pr_debug("end of cap copy\n");
	return retval;
}

/*
 * snd_sst_userbufs_play_cap - constructs the list from user buffers
 *
 * @iovec:pointer to iovec structure
 * @nr_segs:number entries in the iovec structure
 * @str_id:stream id
 * @stream:pointer to stream_info structure
 *
 * This function will traverse the user list and copy the data to the kernel
 * space buffers.
 */
static int snd_sst_userbufs_play_cap(const struct iovec *iovec,
			unsigned long nr_segs, unsigned int str_id,
			struct stream_info *stream)
{
	int retval;
	LIST_HEAD(copy_to_list);


	retval = snd_sst_fill_kernel_list(stream, iovec, nr_segs,
		       &copy_to_list);

	retval = intel_sst_play_capture(stream, str_id);
	if (retval < 0)
		return retval;

	if (stream->ops == STREAM_OPS_CAPTURE) {
		retval = snd_sst_copy_userbuf_capture(stream, iovec,
				&copy_to_list);
	}
	return retval;
}

/* This function is common function across read/write
  for user buffers called from system calls*/
static int intel_sst_read_write(unsigned int str_id, char __user *buf,
					size_t count)
{
	int retval;
	struct stream_info *stream;
	struct iovec iovec;
	unsigned long nr_segs;

	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;
	stream = &sst_drv_ctx->streams[str_id];
	if (stream->mmapped == true) {
		pr_warn("user write and stream is mapped\n");
		return -EIO;
	}
	if (!count)
		return -EINVAL;
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;
	/* copy user buf details */
	pr_debug("new buffers %p, copy size %d, status %d\n" ,
			buf, (int) count, (int) stream->status);

	stream->buf_type = SST_BUF_USER_STATIC;
	iovec.iov_base = buf;
	iovec.iov_len  = count;
	nr_segs = 1;

	do {
		retval = snd_sst_userbufs_play_cap(
				&iovec, nr_segs, str_id, stream);
		if (retval < 0)
			break;

	} while (stream->sg_index < nr_segs);

	stream->sg_index = 0;
	stream->cur_ptr = NULL;
	if (retval >= 0)
		retval = stream->cumm_bytes;
	pr_debug("end of play/rec bytes = %d!!\n", retval);
	return retval;
}

/***
 * intel_sst_write - This function is called when user tries to play out data
 *
 * @file_ptr:pointer to file
 * @buf:user buffer to be played out
 * @count:size of tthe buffer
 * @offset:offset to start from
 *
 * writes the encoded data into DSP
 */
int intel_sst_write(struct file *file_ptr, const char __user *buf,
			size_t count, loff_t *offset)
{
	struct ioctl_pvt_data *data = file_ptr->private_data;
	int str_id = data->str_id;
	struct stream_info *stream = &sst_drv_ctx->streams[str_id];

	pr_debug("called for %d\n", str_id);
	if (stream->status == STREAM_UN_INIT ||
		stream->status == STREAM_DECODE) {
		return -EBADRQC;
	}
	return intel_sst_read_write(str_id, (char __user *)buf, count);
}

/*
 * intel_sst_aio_write - write buffers
 *
 * @kiocb:pointer to a structure containing file pointer
 * @iov:list of user buffer to be played out
 * @nr_segs:number of entries
 * @offset:offset to start from
 *
 * This function is called when user tries to play out multiple data buffers
 */
ssize_t intel_sst_aio_write(struct kiocb *kiocb, const struct iovec *iov,
			unsigned long nr_segs, loff_t  offset)
{
	int retval;
	struct ioctl_pvt_data *data = kiocb->ki_filp->private_data;
	int str_id = data->str_id;
	struct stream_info *stream;

	pr_debug("entry - %ld\n", nr_segs);

	if (is_sync_kiocb(kiocb) == false)
		return -EINVAL;

	pr_debug("called for str_id %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;
	stream = &sst_drv_ctx->streams[str_id];
	if (stream->mmapped == true)
		return -EIO;
	if (stream->status == STREAM_UN_INIT ||
		stream->status == STREAM_DECODE) {
		return -EBADRQC;
	}
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;
	pr_debug("new segs %ld, offset %d, status %d\n" ,
			nr_segs, (int) offset, (int) stream->status);
	stream->buf_type = SST_BUF_USER_STATIC;
	do {
		retval = snd_sst_userbufs_play_cap(iov, nr_segs,
						str_id, stream);
		if (retval < 0)
			break;

	} while (stream->sg_index < nr_segs);

	stream->sg_index = 0;
	stream->cur_ptr = NULL;
	if (retval >= 0)
		retval = stream->cumm_bytes;
	pr_debug("end of play/rec bytes = %d!!\n", retval);
	return retval;
}

/*
 * intel_sst_read - read the encoded data
 *
 * @file_ptr: pointer to file
 * @buf: user buffer to be filled with captured data
 * @count: size of tthe buffer
 * @offset: offset to start from
 *
 * This function is called when user tries to capture data
 */
int intel_sst_read(struct file *file_ptr, char __user *buf,
			size_t count, loff_t *offset)
{
	struct ioctl_pvt_data *data = file_ptr->private_data;
	int str_id = data->str_id;
	struct stream_info *stream = &sst_drv_ctx->streams[str_id];

	pr_debug("called for %d\n", str_id);
	if (stream->status == STREAM_UN_INIT ||
			stream->status == STREAM_DECODE)
		return -EBADRQC;
	return intel_sst_read_write(str_id, buf, count);
}

/*
 * intel_sst_aio_read - aio read
 *
 * @kiocb: pointer to a structure containing file pointer
 * @iov: list of user buffer to be filled with captured
 * @nr_segs: number of entries
 * @offset: offset to start from
 *
 * This function is called when user tries to capture out multiple data buffers
 */
ssize_t intel_sst_aio_read(struct kiocb *kiocb, const struct iovec *iov,
			 unsigned long nr_segs, loff_t offset)
{
	int retval;
	struct ioctl_pvt_data *data = kiocb->ki_filp->private_data;
	int str_id = data->str_id;
	struct stream_info *stream;

	pr_debug("entry - %ld\n", nr_segs);

	if (is_sync_kiocb(kiocb) == false) {
		pr_debug("aio_read from user space is not allowed\n");
		return -EINVAL;
	}

	pr_debug("called for str_id %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;
	stream = &sst_drv_ctx->streams[str_id];
	if (stream->mmapped == true)
		return -EIO;
	if (stream->status == STREAM_UN_INIT ||
			stream->status == STREAM_DECODE)
		return -EBADRQC;
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;

	pr_debug("new segs %ld, offset %d, status %d\n" ,
			nr_segs, (int) offset, (int) stream->status);
	stream->buf_type = SST_BUF_USER_STATIC;
	do {
		retval = snd_sst_userbufs_play_cap(iov, nr_segs,
						str_id, stream);
		if (retval < 0)
			break;

	} while (stream->sg_index < nr_segs);

	stream->sg_index = 0;
	stream->cur_ptr = NULL;
	if (retval >= 0)
		retval = stream->cumm_bytes;
	pr_debug("end of play/rec bytes = %d!!\n", retval);
	return retval;
}

/* sst_print_stream_params - prints the stream parameters (debug fn)*/
static void sst_print_stream_params(struct snd_sst_get_stream_params *get_prm)
{
	pr_debug("codec params:result = %d\n",
				get_prm->codec_params.result);
	pr_debug("codec params:stream = %d\n",
				get_prm->codec_params.stream_id);
	pr_debug("codec params:codec = %d\n",
				get_prm->codec_params.codec);
	pr_debug("codec params:ops = %d\n",
				get_prm->codec_params.ops);
	pr_debug("codec params:stream_type = %d\n",
				get_prm->codec_params.stream_type);
	pr_debug("pcmparams:sfreq = %d\n",
				get_prm->pcm_params.sfreq);
	pr_debug("pcmparams:num_chan = %d\n",
				get_prm->pcm_params.num_chan);
	pr_debug("pcmparams:pcm_wd_sz = %d\n",
				get_prm->pcm_params.pcm_wd_sz);
	return;
}

/**
 * sst_create_algo_ipc - create ipc msg for algorithm parameters
 *
 * @algo_params: Algorithm parameters
 * @msg: post msg pointer
 *
 * This function is called to create ipc msg
 */
int sst_create_algo_ipc(struct snd_ppp_params *algo_params,
					struct ipc_post **msg)
{
	if (sst_create_large_msg(msg))
		return -ENOMEM;
	sst_fill_header(&(*msg)->header,
			IPC_IA_ALG_PARAMS, 1, algo_params->str_id);
	(*msg)->header.part.data = sizeof(u32) +
			sizeof(*algo_params) + algo_params->size;
	memcpy((*msg)->mailbox_data, &(*msg)->header, sizeof(u32));
	memcpy((*msg)->mailbox_data + sizeof(u32),
				algo_params, sizeof(*algo_params));
	return 0;
}

/**
 * sst_send_algo_ipc - send ipc msg for algorithm parameters
 *
 * @msg: post msg pointer
 *
 * This function is called to send ipc msg
 */
int sst_send_algo_ipc(struct ipc_post **msg)
{
	sst_drv_ctx->ppp_params_blk.condition = false;
	sst_drv_ctx->ppp_params_blk.ret_code = 0;
	sst_drv_ctx->ppp_params_blk.on = true;
	sst_drv_ctx->ppp_params_blk.data = NULL;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&(*msg)->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	return sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->ppp_params_blk, SST_BLOCK_TIMEOUT);
}

/**
 * intel_sst_ioctl_dsp - receives the device ioctl's
 *
 * @cmd:Ioctl cmd
 * @arg:data
 *
 * This function is called when a user space component
 * sends a DSP Ioctl to SST driver
 */
long intel_sst_ioctl_dsp(unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct snd_ppp_params algo_params;
	struct snd_ppp_params *algo_params_copied;
	struct ipc_post *msg;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(SNDRV_SST_SET_ALGO):
		if (copy_from_user(&algo_params, (void __user *)arg,
							sizeof(algo_params)))
			return -EFAULT;
		if (algo_params.size > SST_MAILBOX_SIZE)
			return -EMSGSIZE;

		pr_debug("Algo ID %d Str id %d Enable %d Size %d\n",
			algo_params.algo_id, algo_params.str_id,
			algo_params.enable, algo_params.size);
		retval = sst_create_algo_ipc(&algo_params, &msg);
		if (retval)
			break;
		algo_params.reserved = 0;
		if (copy_from_user(msg->mailbox_data + sizeof(algo_params),
				algo_params.params, algo_params.size))
			return -EFAULT;

		retval = sst_send_algo_ipc(&msg);
		if (retval) {
			pr_debug("Error in sst_set_algo = %d\n", retval);
			retval = -EIO;
		}
		break;

	case _IOC_NR(SNDRV_SST_GET_ALGO):
		if (copy_from_user(&algo_params, (void __user *)arg,
							sizeof(algo_params)))
			return -EFAULT;
		pr_debug("Algo ID %d Str id %d Enable %d Size %d\n",
			algo_params.algo_id, algo_params.str_id,
			algo_params.enable, algo_params.size);
		retval = sst_create_algo_ipc(&algo_params, &msg);
		if (retval)
			break;
		algo_params.reserved = 1;
		retval = sst_send_algo_ipc(&msg);
		if (retval) {
			pr_debug("Error in sst_get_algo = %d\n", retval);
			retval = -EIO;
			break;
		}
		algo_params_copied = (struct snd_ppp_params *)
					sst_drv_ctx->ppp_params_blk.data;
		if (algo_params_copied->size > algo_params.size) {
			pr_debug("mem insufficient to copy\n");
			retval = -EMSGSIZE;
			goto free_mem;
		} else {
			char __user *tmp;

			if (copy_to_user(algo_params.params,
					algo_params_copied->params,
					algo_params_copied->size)) {
				retval = -EFAULT;
				goto free_mem;
			}
			tmp = (char __user *)arg + offsetof(
					struct snd_ppp_params, size);
			if (copy_to_user(tmp, &algo_params_copied->size,
						 sizeof(__u32))) {
				retval = -EFAULT;
				goto free_mem;
			}

		}
free_mem:
		kfree(algo_params_copied->params);
		kfree(algo_params_copied);
		break;
	}
	return retval;
}


int sst_ioctl_tuning_params(unsigned long arg)
{
	struct snd_sst_tuning_params params;
	struct ipc_post *msg;

	if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
		return -EFAULT;
	if (params.size > SST_MAILBOX_SIZE)
		return -ENOMEM;
	pr_debug("Parameter %d, Stream %d, Size %d\n", params.type,
			params.str_id, params.size);
	if (sst_create_large_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header, IPC_IA_TUNING_PARAMS, 1, params.str_id);
	msg->header.part.data = sizeof(u32) + sizeof(params) + params.size;
	memcpy(msg->mailbox_data, &msg->header.full, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), &params, sizeof(params));
	if (copy_from_user(msg->mailbox_data + sizeof(params),
			(void __user *)(unsigned long)params.addr,
			params.size)) {
		kfree(msg->mailbox_data);
		kfree(msg);
		return -EFAULT;
	}
	return sst_send_algo_ipc(&msg);
}
/**
 * intel_sst_ioctl - receives the device ioctl's
 * @file_ptr:pointer to file
 * @cmd:Ioctl cmd
 * @arg:data
 *
 * This function is called by OS when a user space component
 * sends an Ioctl to SST driver
 */
long intel_sst_ioctl(struct file *file_ptr, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct ioctl_pvt_data *data = NULL;
	int str_id = 0, minor = 0;

	data = file_ptr->private_data;
	if (data) {
		minor = 0;
		str_id = data->str_id;
	} else
		minor = 1;

	if (sst_drv_ctx->sst_state != SST_FW_RUNNING)
		return -EBUSY;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(SNDRV_SST_STREAM_PAUSE):
		pr_debug("IOCTL_PAUSE received for %d!\n", str_id);
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_pause_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_RESUME):
		pr_debug("SNDRV_SST_IOCTL_RESUME received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_resume_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_SET_PARAMS): {
		struct snd_sst_params str_param;

		pr_debug("IOCTL_SET_PARAMS received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}

		if (copy_from_user(&str_param, (void __user *)arg,
				sizeof(str_param))) {
			retval = -EFAULT;
			break;
		}

		if (!str_id) {

			retval = sst_get_stream(&str_param);
			if (retval > 0) {
				struct stream_info *str_info;
				char __user *dest;

				sst_drv_ctx->stream_cnt++;
				data->str_id = retval;
				str_info = &sst_drv_ctx->streams[retval];
				str_info->src = SST_DRV;
				dest = (char __user *)arg + offsetof(struct snd_sst_params, stream_id);
				retval = copy_to_user(dest, &retval, sizeof(__u32));
				if (retval)
					retval = -EFAULT;
			} else {
				if (retval == -SST_ERR_INVALID_PARAMS)
					retval = -EINVAL;
			}
		} else {
			pr_debug("SET_STREAM_PARAMS received!\n");
			/* allocated set params only */
			retval = sst_set_stream_param(str_id, &str_param);
			/* Block the call for reply */
			if (!retval) {
				int sfreq = 0, word_size = 0, num_channel = 0;
				sfreq =	str_param.sparams.uc.pcm_params.sfreq;
				word_size = str_param.sparams.uc.pcm_params.pcm_wd_sz;
				num_channel = str_param.sparams.uc.pcm_params.num_chan;
				if (str_param.ops == STREAM_OPS_CAPTURE) {
					sst_drv_ctx->scard_ops->\
					set_pcm_audio_params(sfreq,
						word_size, num_channel);
				}
			}
		}
		break;
	}
	case _IOC_NR(SNDRV_SST_SET_VOL): {
		struct snd_sst_vol set_vol;

		if (copy_from_user(&set_vol, (void __user *)arg,
				sizeof(set_vol))) {
			pr_debug("copy failed\n");
			retval = -EFAULT;
			break;
		}
		pr_debug("SET_VOLUME received for %d!\n",
				set_vol.stream_id);
		if (minor == STREAM_MODULE && set_vol.stream_id == 0) {
			pr_debug("invalid operation!\n");
			retval = -EPERM;
			break;
		}
		retval = sst_set_vol(&set_vol);
		break;
	}
	case _IOC_NR(SNDRV_SST_GET_VOL): {
		struct snd_sst_vol get_vol;

		if (copy_from_user(&get_vol, (void __user *)arg,
				sizeof(get_vol))) {
			retval = -EFAULT;
			break;
		}
		pr_debug("IOCTL_GET_VOLUME received for stream = %d!\n",
				get_vol.stream_id);
		if (minor == STREAM_MODULE && get_vol.stream_id == 0) {
			pr_debug("invalid operation!\n");
			retval = -EPERM;
			break;
		}
		retval = sst_get_vol(&get_vol);
		if (retval) {
			retval = -EIO;
			break;
		}
		pr_debug("id:%d\n, vol:%d, ramp_dur:%d, ramp_type:%d\n",
				get_vol.stream_id, get_vol.volume,
				get_vol.ramp_duration, get_vol.ramp_type);
		if (copy_to_user((struct snd_sst_vol __user *)arg,
				&get_vol, sizeof(get_vol))) {
			retval = -EFAULT;
			break;
		}
		/*sst_print_get_vol_info(str_id, &get_vol);*/
		break;
	}

	case _IOC_NR(SNDRV_SST_MUTE): {
		struct snd_sst_mute set_mute;

		if (copy_from_user(&set_mute, (void __user *)arg,
				sizeof(set_mute))) {
			retval = -EFAULT;
			break;
		}
		pr_debug("SNDRV_SST_SET_VOLUME received for %d!\n",
			set_mute.stream_id);
		if (minor == STREAM_MODULE && set_mute.stream_id == 0) {
			retval = -EPERM;
			break;
		}
		retval = sst_set_mute(&set_mute);
		break;
	}
	case _IOC_NR(SNDRV_SST_STREAM_GET_PARAMS): {
		struct snd_sst_get_stream_params get_params;

		pr_debug("IOCTL_GET_PARAMS received!\n");
		if (minor != 0) {
			retval = -EBADRQC;
			break;
		}

		retval = sst_get_stream_params(str_id, &get_params);
		if (retval) {
			retval = -EIO;
			break;
		}
		if (copy_to_user((struct snd_sst_get_stream_params __user *)arg,
					&get_params, sizeof(get_params))) {
			retval = -EFAULT;
			break;
		}
		sst_print_stream_params(&get_params);
		break;
	}

	case _IOC_NR(SNDRV_SST_MMAP_PLAY):
	case _IOC_NR(SNDRV_SST_MMAP_CAPTURE): {
		struct snd_sst_mmap_buffs mmap_buf;

		pr_debug("SNDRV_SST_MMAP_PLAY/CAPTURE received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		if (copy_from_user(&mmap_buf, (void __user *)arg,
				sizeof(mmap_buf))) {
			retval = -EFAULT;
			break;
		}
		retval = intel_sst_mmap_play_capture(str_id, &mmap_buf);
		break;
	}
	case _IOC_NR(SNDRV_SST_STREAM_DROP):
		pr_debug("SNDRV_SST_IOCTL_DROP received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		retval = sst_drop_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_GET_TSTAMP): {
		struct snd_sst_tstamp tstamp = {0};
		unsigned long long time, freq, mod;

		pr_debug("SNDRV_SST_STREAM_GET_TSTAMP received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		memcpy_fromio(&tstamp,
			sst_drv_ctx->mailbox + SST_TIME_STAMP + str_id * sizeof(tstamp),
			sizeof(tstamp));
		time = tstamp.samples_rendered;
		freq = (unsigned long long) tstamp.sampling_frequency;
		time = time * 1000; /* converting it to ms */
		mod = do_div(time, freq);
		if (copy_to_user((void __user *)arg, &time,
				sizeof(unsigned long long)))
			retval = -EFAULT;
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_START):{
		struct stream_info *stream;

		pr_debug("SNDRV_SST_STREAM_START received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		retval = sst_validate_strid(str_id);
		if (retval)
			break;
		stream = &sst_drv_ctx->streams[str_id];
		mutex_lock(&stream->lock);
		if (stream->status == STREAM_INIT &&
			stream->need_draining != true) {
			stream->prev = stream->status;
			stream->status = STREAM_RUNNING;
			if (stream->ops == STREAM_OPS_PLAYBACK ||
				stream->ops == STREAM_OPS_PLAYBACK_DRM) {
				retval = sst_play_frame(str_id);
			} else if (stream->ops == STREAM_OPS_CAPTURE)
				retval = sst_capture_frame(str_id);
			else {
				retval = -EINVAL;
				mutex_unlock(&stream->lock);
				break;
			}
			if (retval < 0) {
				stream->status = STREAM_INIT;
				mutex_unlock(&stream->lock);
				break;
			}
		} else {
			retval = -EINVAL;
		}
		mutex_unlock(&stream->lock);
		break;
	}

	case _IOC_NR(SNDRV_SST_SET_TARGET_DEVICE): {
		struct snd_sst_target_device target_device;

		pr_debug("SET_TARGET_DEVICE received!\n");
		if (copy_from_user(&target_device, (void __user *)arg,
				sizeof(target_device))) {
			retval = -EFAULT;
			break;
		}
		if (minor != AM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_target_device_select(&target_device);
		break;
	}

	case _IOC_NR(SNDRV_SST_DRIVER_INFO): {
		struct snd_sst_driver_info info;

		pr_debug("SNDRV_SST_DRIVER_INFO received\n");
		info.version = SST_VERSION_NUM;
		/* hard coding, shud get sumhow later */
		info.active_pcm_streams = sst_drv_ctx->stream_cnt -
						sst_drv_ctx->encoded_cnt;
		info.active_enc_streams = sst_drv_ctx->encoded_cnt;
		info.max_pcm_streams = MAX_ACTIVE_STREAM - MAX_ENC_STREAM;
		info.max_enc_streams = MAX_ENC_STREAM;
		info.buf_per_stream = sst_drv_ctx->mmap_len;
		if (copy_to_user((void __user *)arg, &info,
				sizeof(info)))
			retval = -EFAULT;
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_DECODE): {
		struct snd_sst_dbufs param;
		struct snd_sst_dbufs dbufs_local;
		struct snd_sst_buffs ibufs, obufs;
		struct snd_sst_buff_entry *ibuf_tmp, *obuf_tmp;
		char __user *dest;

		pr_debug("SNDRV_SST_STREAM_DECODE received\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		if (copy_from_user(&param, (void __user *)arg,
				sizeof(param))) {
			retval = -EFAULT;
			break;
		}

		dbufs_local.input_bytes_consumed = param.input_bytes_consumed;
		dbufs_local.output_bytes_produced =
					param.output_bytes_produced;

		if (copy_from_user(&ibufs, (void __user *)param.ibufs, sizeof(ibufs))) {
			retval = -EFAULT;
			break;
		}
		if (copy_from_user(&obufs, (void __user *)param.obufs, sizeof(obufs))) {
			retval = -EFAULT;
			break;
		}

		ibuf_tmp = kcalloc(ibufs.entries, sizeof(*ibuf_tmp), GFP_KERNEL);
		obuf_tmp = kcalloc(obufs.entries, sizeof(*obuf_tmp), GFP_KERNEL);
		if (!ibuf_tmp || !obuf_tmp) {
			retval = -ENOMEM;
			goto free_iobufs;
		}

		if (copy_from_user(ibuf_tmp, (void __user *)ibufs.buff_entry,
				ibufs.entries * sizeof(*ibuf_tmp))) {
			retval = -EFAULT;
			goto free_iobufs;
		}
		ibufs.buff_entry = ibuf_tmp;
		dbufs_local.ibufs = &ibufs;

		if (copy_from_user(obuf_tmp, (void __user *)obufs.buff_entry,
				obufs.entries * sizeof(*obuf_tmp))) {
			retval = -EFAULT;
			goto free_iobufs;
		}
		obufs.buff_entry = obuf_tmp;
		dbufs_local.obufs = &obufs;

		retval = sst_decode(str_id, &dbufs_local);
		if (retval) {
			retval = -EAGAIN;
			goto free_iobufs;
		}

		dest = (char __user *)arg + offsetof(struct snd_sst_dbufs, input_bytes_consumed);
		if (copy_to_user(dest,
				&dbufs_local.input_bytes_consumed,
				sizeof(unsigned long long))) {
			retval = -EFAULT;
			goto free_iobufs;
		}

		dest = (char __user *)arg + offsetof(struct snd_sst_dbufs, input_bytes_consumed);
		if (copy_to_user(dest,
				&dbufs_local.output_bytes_produced,
				sizeof(unsigned long long))) {
			retval = -EFAULT;
			goto free_iobufs;
		}
free_iobufs:
		kfree(ibuf_tmp);
		kfree(obuf_tmp);
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_DRAIN):
		pr_debug("SNDRV_SST_STREAM_DRAIN received\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		retval = sst_drain_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_BYTES_DECODED): {
		unsigned long long __user *bytes = (unsigned long long __user *)arg;
		struct snd_sst_tstamp tstamp = {0};

		pr_debug("STREAM_BYTES_DECODED received!\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		memcpy_fromio(&tstamp,
			sst_drv_ctx->mailbox + SST_TIME_STAMP + str_id * sizeof(tstamp),
			sizeof(tstamp));
		if (copy_to_user(bytes, &tstamp.bytes_processed,
				sizeof(*bytes)))
			retval = -EFAULT;
		break;
	}
	case _IOC_NR(SNDRV_SST_FW_INFO): {
		struct snd_sst_fw_info *fw_info;

		pr_debug("SNDRV_SST_FW_INFO received\n");

		fw_info = kzalloc(sizeof(*fw_info), GFP_ATOMIC);
		if (!fw_info) {
			retval = -ENOMEM;
			break;
		}
		retval = sst_get_fw_info(fw_info);
		if (retval) {
			retval = -EIO;
			kfree(fw_info);
			break;
		}
		if (copy_to_user((struct snd_sst_dbufs __user *)arg,
				fw_info, sizeof(*fw_info))) {
			kfree(fw_info);
			retval = -EFAULT;
			break;
		}
		/*sst_print_fw_info(fw_info);*/
		kfree(fw_info);
		break;
	}
	case _IOC_NR(SNDRV_SST_GET_ALGO):
	case _IOC_NR(SNDRV_SST_SET_ALGO):
		if (minor != AM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = intel_sst_ioctl_dsp(cmd, arg);
		break;

	case _IOC_NR(SNDRV_SST_TUNING_PARAMS):
		if (minor != AM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_ioctl_tuning_params(arg);
		break;

	default:
		retval = -EINVAL;
	}
	pr_debug("intel_sst_ioctl:complete ret code = %d\n", retval);
	return retval;
}

