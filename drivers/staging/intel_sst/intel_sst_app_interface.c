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

#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/ioctl.h>
#include <linux/smp_lock.h>
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
		pr_warn("sst: Sound card not availble\n ");
		return -EIO;
	}
	if (sst_drv_ctx->sst_state == SST_SUSPENDED) {
		pr_debug("sst: Resuming from Suspended state\n");
		retval = intel_sst_resume(sst_drv_ctx->pci);
		if (retval) {
			pr_debug("sst: Resume Failed= %#x,abort\n", retval);
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
	unsigned int retval = intel_sst_check_device();
	if (retval)
		return retval;

	mutex_lock(&sst_drv_ctx->stream_lock);
	if (sst_drv_ctx->encoded_cnt < MAX_ENC_STREAM) {
		struct ioctl_pvt_data *data =
			kzalloc(sizeof(struct ioctl_pvt_data), GFP_KERNEL);
		if (!data) {
			mutex_unlock(&sst_drv_ctx->stream_lock);
			return -ENOMEM;
		}

		sst_drv_ctx->encoded_cnt++;
		mutex_unlock(&sst_drv_ctx->stream_lock);
		data->pvt_id = sst_assign_pvt_id(sst_drv_ctx);
		data->str_id = 0;
		file_ptr->private_data = (void *)data;
		pr_debug("sst: pvt_id handle = %d!\n", data->pvt_id);
	} else {
		retval = -EUSERS;
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
	unsigned int retval = intel_sst_check_device();
	if (retval)
		return retval;

	/* audio manager open */
	mutex_lock(&sst_drv_ctx->stream_lock);
	if (sst_drv_ctx->am_cnt < MAX_AM_HANDLES) {
		sst_drv_ctx->am_cnt++;
		pr_debug("sst: AM handle opened...\n");
		file_ptr->private_data = NULL;
	} else
		retval = -EACCES;

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

	pr_debug("sst: Release called, closing app handle\n");
	mutex_lock(&sst_drv_ctx->stream_lock);
	sst_drv_ctx->encoded_cnt--;
	sst_drv_ctx->stream_cnt--;
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
	mutex_unlock(&sst_drv_ctx->stream_lock);
	pr_debug("sst: AM handle closed\n");
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
	pr_debug("sst: called for stream %d length 0x%x\n", str_id, length);

	if (length > sst_drv_ctx->mmap_len)
		return -ENOMEM;
	if (!sst_drv_ctx->mmap_mem)
		return -EIO;

	/* round it up to the page bondary  */
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

	pr_debug("sst: mmap ret 0x%x\n", retval);
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

	pr_debug("sst:called for str_id %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return -EINVAL;
	BUG_ON(!mmap_buf);

	stream = &sst_drv_ctx->streams[str_id];
	if (stream->mmapped != true)
		return -EIO;

	if (stream->status == STREAM_UN_INIT ||
		stream->status == STREAM_DECODE) {
		return -EBADRQC;
	}
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;

	pr_debug("sst:new buffers count %d status %d\n",
			mmap_buf->entries, stream->status);
	buf_entry = mmap_buf->buff;
	for (i = 0; i < mmap_buf->entries; i++) {
		BUG_ON(!buf_entry);
		bufs = kzalloc(sizeof(*bufs), GFP_KERNEL);
		if (!bufs)
			return -ENOMEM;
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
				pr_warn("sst: play frames fail\n");
				mutex_unlock(&stream->lock);
				return -EIO;
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			if (sst_capture_frame(str_id) < 0) {
				pr_warn("sst: capture frame fail\n");
				mutex_unlock(&stream->lock);
				return -EIO;
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
	pr_debug("sst:end of play/rec ioctl bytes = %d!!\n", retval);
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
		pr_debug("sst: Stream isn't in started state %d, prev %d\n",
			stream->status, stream->prev);
	} else if ((stream->status == STREAM_RUNNING ||
			stream->status == STREAM_PAUSED) &&
			stream->need_draining != true) {
		/* stream is started */
		if (stream->ops == STREAM_OPS_PLAYBACK ||
				stream->ops == STREAM_OPS_PLAYBACK_DRM) {
			if (sst_play_frame(str_id) < 0) {
				pr_warn("sst: play frames failed\n");
				mutex_unlock(&stream->lock);
				return -EIO;
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			if (sst_capture_frame(str_id) < 0) {
				pr_warn("sst: capture frames failed\n ");
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
		pr_debug("sst: wait returned error...\n");
	}
	return retval;
}

/* fills kernel list with buffer addresses for SST DSP driver to process*/
static int snd_sst_fill_kernel_list(struct stream_info *stream,
			const struct iovec *iovec, unsigned long nr_segs,
			struct list_head *copy_to_list)
{
	struct sst_stream_bufs *stream_bufs;
	unsigned long index, data_not_copied, mmap_len;
	unsigned char *bufp;
	unsigned long size, copied_size;
	int retval = 0, add_to_list = 0;
	static int sent_offset;
	static unsigned long sent_index;

	stream_bufs = kzalloc(sizeof(*stream_bufs), GFP_KERNEL);
	if (!stream_bufs)
		return -ENOMEM;
	stream_bufs->addr = sst_drv_ctx->mmap_mem;
#ifdef CONFIG_MRST_RAR_HANDLER
	if (stream->ops == STREAM_OPS_PLAYBACK_DRM) {
		for (index = stream->sg_index; index < nr_segs; index++) {
			__u32 rar_handle;
			struct sst_stream_bufs *stream_bufs =
				kzalloc(sizeof(*stream_bufs), GFP_KERNEL);

			stream->sg_index = index;
			if (!stream_bufs)
				return -ENOMEM;
			retval = copy_from_user((void *) &rar_handle,
						iovec[index].iov_base,
						sizeof(__u32));
			if (retval != 0)
				return -EFAULT;
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
			data_not_copied = copy_from_user(
				(void *)(stream_bufs->addr + copied_size),
				bufp, size);
			if (data_not_copied > 0) {
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
			pr_debug("sst: Buffer overflows");
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
		pr_debug("sst: copied_size - %lx\n", copied_size);
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
	unsigned long data_not_copied;

	/* copy sent buffers */
	pr_debug("sst: capture stream copying to user now...\n");
	list_for_each_entry_safe(kbufs, _kbufs, &stream->bufs, node) {
		if (kbufs->in_use == true) {
			/* copy to user */
			list_for_each_entry_safe(entry, _entry,
						copy_to_list, node) {
				data_not_copied = copy_to_user((void *)
					iovec[entry->iov_index].iov_base +
						entry->iov_offset,
					kbufs->addr + entry->offset,
					entry->size);
				if (data_not_copied > 0) {
					/* Clean up the list and return error */
					retval = -EFAULT;
					break;
				}
				list_del(&entry->node);
				kfree(entry);
			}
		}
	}
	pr_debug("sst: end of cap copy\n");
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
		pr_warn("sst: user write and stream is mapped");
		return -EIO;
	}
	if (!count)
		return -EINVAL;
	stream->curr_bytes = 0;
	stream->cumm_bytes = 0;
	/* copy user buf details */
	pr_debug("sst: new buffers %p, copy size %d, status %d\n" ,
			buf, (int) count, (int) stream->status);

	stream->buf_type = SST_BUF_USER_STATIC;
	iovec.iov_base = (void *)buf;
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
	pr_debug("sst: end of play/rec bytes = %d!!\n", retval);
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

	pr_debug("sst: called for %d\n", str_id);
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

	pr_debug("sst: entry - %ld\n", nr_segs);

	if (is_sync_kiocb(kiocb) == false)
		return -EINVAL;

	pr_debug("sst: called for str_id %d\n", str_id);
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
	pr_debug("sst: new segs %ld, offset %d, status %d\n" ,
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
	pr_debug("sst: end of play/rec bytes = %d!!\n", retval);
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

	pr_debug("sst: called for %d\n", str_id);
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

	pr_debug("sst: entry - %ld\n", nr_segs);

	if (is_sync_kiocb(kiocb) == false) {
		pr_debug("sst: aio_read from user space is not allowed\n");
		return -EINVAL;
	}

	pr_debug("sst: called for str_id %d\n", str_id);
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

	pr_debug("sst: new segs %ld, offset %d, status %d\n" ,
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
	pr_debug("sst: end of play/rec bytes = %d!!\n", retval);
	return retval;
}

/* sst_print_stream_params - prints the stream parameters (debug fn)*/
static void sst_print_stream_params(struct snd_sst_get_stream_params *get_prm)
{
	pr_debug("sst: codec params:result =%d\n",
				get_prm->codec_params.result);
	pr_debug("sst: codec params:stream = %d\n",
				get_prm->codec_params.stream_id);
	pr_debug("sst: codec params:codec = %d\n",
				get_prm->codec_params.codec);
	pr_debug("sst: codec params:ops = %d\n",
				get_prm->codec_params.ops);
	pr_debug("sst: codec params:stream_type= %d\n",
				get_prm->codec_params.stream_type);
	pr_debug("sst: pcmparams:sfreq= %d\n",
				get_prm->pcm_params.sfreq);
	pr_debug("sst: pcmparams:num_chan= %d\n",
				get_prm->pcm_params.num_chan);
	pr_debug("sst: pcmparams:pcm_wd_sz= %d\n",
				get_prm->pcm_params.pcm_wd_sz);
	return;
}

/**
 * intel_sst_ioctl - recieves the device ioctl's
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

	lock_kernel();

	data = file_ptr->private_data;
	if (data) {
		minor = 0;
		str_id = data->str_id;
	} else
		minor = 1;

	if (sst_drv_ctx->sst_state != SST_FW_RUNNING) {
		unlock_kernel();
		return -EBUSY;
	}

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(SNDRV_SST_STREAM_PAUSE):
		pr_debug("sst: IOCTL_PAUSE recieved for %d!\n", str_id);
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_pause_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_RESUME):
		pr_debug("sst: SNDRV_SST_IOCTL_RESUME recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_resume_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_SET_PARAMS): {
		struct snd_sst_params *str_param = (struct snd_sst_params *)arg;

		pr_debug("sst: IOCTL_SET_PARAMS recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}

		if (!str_id) {

			retval = sst_get_stream(str_param);
			if (retval > 0) {
				struct stream_info *str_info;
				sst_drv_ctx->stream_cnt++;
				data->str_id = retval;
				str_info = &sst_drv_ctx->streams[retval];
				str_info->src = SST_DRV;
				retval = copy_to_user(&str_param->stream_id,
						&retval, sizeof(__u32));
			} else {
				if (retval == -SST_ERR_INVALID_PARAMS)
					retval = -EINVAL;
			}
		} else {
			pr_debug("sst: SET_STREAM_PARAMS recieved!\n");
			/* allocated set params only */
			retval = sst_set_stream_param(str_id, str_param);
			/* Block the call for reply */
			if (!retval) {
				int sfreq = 0, word_size = 0, num_channel = 0;
				sfreq =	str_param->sparams.uc.pcm_params.sfreq;
				word_size = str_param->sparams.
						uc.pcm_params.pcm_wd_sz;
				num_channel = str_param->
					sparams.uc.pcm_params.num_chan;
				if (str_param->ops == STREAM_OPS_CAPTURE) {
					sst_drv_ctx->scard_ops->\
					set_pcm_audio_params(sfreq,
						word_size, num_channel);
				}
			}
		}
		break;
	}
	case _IOC_NR(SNDRV_SST_SET_VOL): {
		struct snd_sst_vol *set_vol;
		struct snd_sst_vol *rec_vol = (struct snd_sst_vol *)arg;
		pr_debug("sst: SET_VOLUME recieved for %d!\n",
				rec_vol->stream_id);
		if (minor == STREAM_MODULE && rec_vol->stream_id == 0) {
			pr_debug("sst: invalid operation!\n");
			retval = -EPERM;
			break;
		}
		set_vol = kzalloc(sizeof(*set_vol), GFP_ATOMIC);
		if (!set_vol) {
			pr_debug("sst: mem allocation failed\n");
			retval = -ENOMEM;
			break;
		}
		retval = copy_from_user(set_vol, rec_vol, sizeof(*set_vol));
		if (retval) {
			pr_debug("sst: copy failed\n");
			retval = -EAGAIN;
			break;
		}
		retval = sst_set_vol(set_vol);
		kfree(set_vol);
		break;
	}
	case _IOC_NR(SNDRV_SST_GET_VOL): {
		struct snd_sst_vol *rec_vol = (struct snd_sst_vol *)arg;
		struct snd_sst_vol get_vol;
		pr_debug("sst: IOCTL_GET_VOLUME recieved for stream = %d!\n",
				rec_vol->stream_id);
		if (minor == STREAM_MODULE && rec_vol->stream_id == 0) {
			pr_debug("sst: invalid operation!\n");
			retval = -EPERM;
			break;
		}
		get_vol.stream_id = rec_vol->stream_id;
		retval = sst_get_vol(&get_vol);
		if (retval) {
			retval = -EIO;
			break;
		}
		pr_debug("sst: id:%d\n, vol:%d, ramp_dur:%d, ramp_type:%d\n",
				get_vol.stream_id, get_vol.volume,
				get_vol.ramp_duration, get_vol.ramp_type);
		retval = copy_to_user((struct snd_sst_vol *)arg,
						&get_vol, sizeof(get_vol));
		if (retval) {
			retval = -EIO;
			break;
		}
		/*sst_print_get_vol_info(str_id, &get_vol);*/
		break;
	}

	case _IOC_NR(SNDRV_SST_MUTE): {
		struct snd_sst_mute *set_mute;
		struct snd_sst_vol *rec_mute = (struct snd_sst_vol *)arg;
		pr_debug("sst: SNDRV_SST_SET_VOLUME recieved for %d!\n",
			rec_mute->stream_id);
		if (minor == STREAM_MODULE && rec_mute->stream_id == 0) {
			retval = -EPERM;
			break;
		}
		set_mute = kzalloc(sizeof(*set_mute), GFP_ATOMIC);
		if (!set_mute) {
			retval = -ENOMEM;
			break;
		}
		retval = copy_from_user(set_mute, rec_mute, sizeof(*set_mute));
		if (retval) {
			retval = -EFAULT;
			break;
		}
		retval = sst_set_mute(set_mute);
		kfree(set_mute);
		break;
	}
	case _IOC_NR(SNDRV_SST_STREAM_GET_PARAMS): {
		struct snd_sst_get_stream_params get_params;

		pr_debug("sst: IOCTL_GET_PARAMS recieved!\n");
		if (minor != 0) {
			retval = -EBADRQC;
			break;
		}

		retval = sst_get_stream_params(str_id, &get_params);
		if (retval) {
			retval = -EIO;
			break;
		}
		retval = copy_to_user((struct snd_sst_get_stream_params *)arg,
					&get_params, sizeof(get_params));
		if (retval) {
			retval = -EBUSY;
			break;
		}
		sst_print_stream_params(&get_params);
		break;
	}

	case _IOC_NR(SNDRV_SST_MMAP_PLAY):
	case _IOC_NR(SNDRV_SST_MMAP_CAPTURE):
		pr_debug("sst: SNDRV_SST_MMAP_PLAY/CAPTURE recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = intel_sst_mmap_play_capture(str_id,
				(struct snd_sst_mmap_buffs *)arg);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_DROP):
		pr_debug("sst: SNDRV_SST_IOCTL_DROP recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		retval = sst_drop_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_GET_TSTAMP): {
		unsigned long long *ms = (unsigned long long *)arg;
		struct snd_sst_tstamp tstamp = {0};
		unsigned long long time, freq, mod;

		pr_debug("sst: SNDRV_SST_STREAM_GET_TSTAMP recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		memcpy_fromio(&tstamp,
			((void *)(sst_drv_ctx->mailbox + SST_TIME_STAMP)
			+(str_id * sizeof(tstamp))),
			sizeof(tstamp));
		time = tstamp.samples_rendered;
		freq = (unsigned long long) tstamp.sampling_frequency;
		time = time * 1000; /* converting it to ms */
		mod = do_div(time, freq);
		retval = copy_to_user(ms, &time, sizeof(*ms));
		if (retval)
			retval = -EFAULT;
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_START):{
		struct stream_info *stream;

		pr_debug("sst: SNDRV_SST_STREAM_START recieved!\n");
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
				mutex_unlock(
					&sst_drv_ctx->streams[str_id].lock);
				break;
			}
			if (retval < 0) {
				stream->status = STREAM_INIT;
				mutex_unlock(
					&sst_drv_ctx->streams[str_id].lock);
				break;
			}
		} else {
			retval = -EINVAL;
		}
		mutex_unlock(&sst_drv_ctx->streams[str_id].lock);
		break;
	}

	case _IOC_NR(SNDRV_SST_SET_TARGET_DEVICE): {
		struct snd_sst_target_device *target_device;

		pr_debug("sst: SET_TARGET_DEVICE recieved!\n");
		target_device =	(struct snd_sst_target_device *)arg;
		BUG_ON(!target_device);
		if (minor != AM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		retval = sst_target_device_select(target_device);
		break;
	}

	case _IOC_NR(SNDRV_SST_DRIVER_INFO): {
		struct snd_sst_driver_info *info =
			(struct snd_sst_driver_info *)arg;

		pr_debug("sst: SNDRV_SST_DRIVER_INFO recived\n");
		info->version = SST_VERSION_NUM;
		/* hard coding, shud get sumhow later */
		info->active_pcm_streams = sst_drv_ctx->stream_cnt -
						sst_drv_ctx->encoded_cnt;
		info->active_enc_streams = sst_drv_ctx->encoded_cnt;
		info->max_pcm_streams = MAX_ACTIVE_STREAM - MAX_ENC_STREAM;
		info->max_enc_streams = MAX_ENC_STREAM;
		info->buf_per_stream = sst_drv_ctx->mmap_len;
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_DECODE): {
		struct snd_sst_dbufs *param =
				(struct snd_sst_dbufs *)arg, dbufs_local;
		int i;
		struct snd_sst_buffs ibufs, obufs;
		struct snd_sst_buff_entry ibuf_temp[param->ibufs->entries],
				obuf_temp[param->obufs->entries];

		pr_debug("sst: SNDRV_SST_STREAM_DECODE recived\n");
		if (minor != STREAM_MODULE) {
			retval = -EBADRQC;
			break;
		}
		if (!param) {
			retval = -EINVAL;
			break;
		}

		dbufs_local.input_bytes_consumed = param->input_bytes_consumed;
		dbufs_local.output_bytes_produced =
					param->output_bytes_produced;
		dbufs_local.ibufs = &ibufs;
		dbufs_local.obufs = &obufs;
		dbufs_local.ibufs->entries = param->ibufs->entries;
		dbufs_local.ibufs->type = param->ibufs->type;
		dbufs_local.obufs->entries = param->obufs->entries;
		dbufs_local.obufs->type = param->obufs->type;

		dbufs_local.ibufs->buff_entry = ibuf_temp;
		for (i = 0; i < dbufs_local.ibufs->entries; i++) {
			ibuf_temp[i].buffer =
				param->ibufs->buff_entry[i].buffer;
			ibuf_temp[i].size =
				param->ibufs->buff_entry[i].size;
		}
		dbufs_local.obufs->buff_entry = obuf_temp;
		for (i = 0; i < dbufs_local.obufs->entries; i++) {
			obuf_temp[i].buffer =
				param->obufs->buff_entry[i].buffer;
			obuf_temp[i].size =
				param->obufs->buff_entry[i].size;
		}
		retval = sst_decode(str_id, &dbufs_local);
		if (retval)
			retval =  -EAGAIN;
		retval = copy_to_user(&param->input_bytes_consumed,
			&dbufs_local.input_bytes_consumed,
			sizeof(unsigned long long));
		if (retval) {
			retval =  -EFAULT;
			break;
		}
		retval = copy_to_user(&param->output_bytes_produced,
				&dbufs_local.output_bytes_produced,
				sizeof(unsigned long long));
		if (retval) {
			retval =  -EFAULT;
			break;
		}
		break;
	}

	case _IOC_NR(SNDRV_SST_STREAM_DRAIN):
		pr_debug("sst: SNDRV_SST_STREAM_DRAIN recived\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		retval = sst_drain_stream(str_id);
		break;

	case _IOC_NR(SNDRV_SST_STREAM_BYTES_DECODED): {
		unsigned long long *bytes = (unsigned long long *)arg;
		struct snd_sst_tstamp tstamp = {0};

		pr_debug("sst: STREAM_BYTES_DECODED recieved!\n");
		if (minor != STREAM_MODULE) {
			retval = -EINVAL;
			break;
		}
		memcpy_fromio(&tstamp,
			((void *)(sst_drv_ctx->mailbox + SST_TIME_STAMP)
			+(str_id * sizeof(tstamp))),
			sizeof(tstamp));
		retval = copy_to_user(bytes, &tstamp.bytes_processed,
					sizeof(*bytes));
		if (retval)
			retval = -EFAULT;
		break;
	}
	case _IOC_NR(SNDRV_SST_FW_INFO): {
		struct snd_sst_fw_info *fw_info;

		pr_debug("sst: SNDRV_SST_FW_INFO recived\n");

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
		retval = copy_to_user((struct snd_sst_dbufs *)arg,
					fw_info, sizeof(*fw_info));
		if (retval) {
			kfree(fw_info);
			retval = -EFAULT;
			break;
		}
		/*sst_print_fw_info(fw_info);*/
		kfree(fw_info);
		break;
	}
	default:
		retval = -EINVAL;
	}
	unlock_kernel();
	pr_debug("sst: intel_sst_ioctl:complete ret code = %d\n", retval);
	return retval;
}

