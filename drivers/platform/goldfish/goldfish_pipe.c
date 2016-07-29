/*
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 * Copyright (C) 2013 Intel, Inc.
 * Copyright (C) 2014 Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* This source file contains the implementation of the legacy version of
 * a goldfish pipe device driver. See goldfish_pipe_v2.c for the current
 * version.
 */
#include "goldfish_pipe.h"

/*
 * IMPORTANT: The following constants must match the ones used and defined
 * in external/qemu/hw/goldfish_pipe.c in the Android source tree.
 */

/* pipe device registers */
#define PIPE_REG_COMMAND		0x00  /* write: value = command */
#define PIPE_REG_STATUS			0x04  /* read */
#define PIPE_REG_CHANNEL		0x08  /* read/write: channel id */
#define PIPE_REG_CHANNEL_HIGH	        0x30  /* read/write: channel id */
#define PIPE_REG_SIZE			0x0c  /* read/write: buffer size */
#define PIPE_REG_ADDRESS		0x10  /* write: physical address */
#define PIPE_REG_ADDRESS_HIGH	        0x34  /* write: physical address */
#define PIPE_REG_WAKES			0x14  /* read: wake flags */
#define PIPE_REG_PARAMS_ADDR_LOW	0x18  /* read/write: batch data address */
#define PIPE_REG_PARAMS_ADDR_HIGH	0x1c  /* read/write: batch data address */
#define PIPE_REG_ACCESS_PARAMS		0x20  /* write: batch access */
#define PIPE_REG_VERSION		0x24  /* read: device version */

/* list of commands for PIPE_REG_COMMAND */
#define CMD_OPEN			1  /* open new channel */
#define CMD_CLOSE			2  /* close channel (from guest) */
#define CMD_POLL			3  /* poll read/write status */

/* List of bitflags returned in status of CMD_POLL command */
#define PIPE_POLL_IN			(1 << 0)
#define PIPE_POLL_OUT			(1 << 1)
#define PIPE_POLL_HUP			(1 << 2)

/* The following commands are related to write operations */
#define CMD_WRITE_BUFFER	4  /* send a user buffer to the emulator */
#define CMD_WAKE_ON_WRITE	5  /* tell the emulator to wake us when writing
				     is possible */
#define CMD_READ_BUFFER        6  /* receive a user buffer from the emulator */
#define CMD_WAKE_ON_READ       7  /* tell the emulator to wake us when reading
				   * is possible */

/* Possible status values used to signal errors - see goldfish_pipe_error_convert */
#define PIPE_ERROR_INVAL       -1
#define PIPE_ERROR_AGAIN       -2
#define PIPE_ERROR_NOMEM       -3
#define PIPE_ERROR_IO          -4

/* Bit-flags used to signal events from the emulator */
#define PIPE_WAKE_CLOSED       (1 << 0)  /* emulator closed pipe */
#define PIPE_WAKE_READ         (1 << 1)  /* pipe can now be read from */
#define PIPE_WAKE_WRITE        (1 << 2)  /* pipe can now be written to */

#define MAX_PAGES_TO_GRAB 32

#define DEBUG 0

#if DEBUG
#define DPRINT(...) { printk(KERN_ERR __VA_ARGS__); }
#else
#define DPRINT(...)
#endif

/* This data type models a given pipe instance */
struct goldfish_pipe {
	struct goldfish_pipe_dev *dev;
	struct mutex lock;
	unsigned long flags;
	wait_queue_head_t wake_queue;
};

struct access_params {
	unsigned long channel;
	u32 size;
	unsigned long address;
	u32 cmd;
	u32 result;
	/* reserved for future extension */
	u32 flags;
};

/* Bit flags for the 'flags' field */
enum {
	BIT_CLOSED_ON_HOST = 0,  /* pipe closed by host */
	BIT_WAKE_ON_WRITE  = 1,  /* want to be woken on writes */
	BIT_WAKE_ON_READ   = 2,  /* want to be woken on reads */
};


static u32 goldfish_cmd_status(struct goldfish_pipe *pipe, u32 cmd)
{
	unsigned long flags;
	u32 status;
	struct goldfish_pipe_dev *dev = pipe->dev;

	spin_lock_irqsave(&dev->lock, flags);
	gf_write_ptr(pipe, dev->base + PIPE_REG_CHANNEL,
		     dev->base + PIPE_REG_CHANNEL_HIGH);
	writel(cmd, dev->base + PIPE_REG_COMMAND);
	status = readl(dev->base + PIPE_REG_STATUS);
	spin_unlock_irqrestore(&dev->lock, flags);
	return status;
}

static void goldfish_cmd(struct goldfish_pipe *pipe, u32 cmd)
{
	unsigned long flags;
	struct goldfish_pipe_dev *dev = pipe->dev;

	spin_lock_irqsave(&dev->lock, flags);
	gf_write_ptr(pipe, dev->base + PIPE_REG_CHANNEL,
		     dev->base + PIPE_REG_CHANNEL_HIGH);
	writel(cmd, dev->base + PIPE_REG_COMMAND);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* This function converts an error code returned by the emulator through
 * the PIPE_REG_STATUS i/o register into a valid negative errno value.
 */
static int goldfish_pipe_error_convert(int status)
{
	switch (status) {
	case PIPE_ERROR_AGAIN:
		return -EAGAIN;
	case PIPE_ERROR_NOMEM:
		return -ENOMEM;
	case PIPE_ERROR_IO:
		return -EIO;
	default:
		return -EINVAL;
	}
}

/*
 * Notice: QEMU will return 0 for un-known register access, indicating
 * param_acess is supported or not
 */
static int valid_batchbuffer_addr(struct goldfish_pipe_dev *dev,
				  struct access_params *aps)
{
	u32 aph, apl;
	u64 paddr;
	aph = readl(dev->base + PIPE_REG_PARAMS_ADDR_HIGH);
	apl = readl(dev->base + PIPE_REG_PARAMS_ADDR_LOW);

	paddr = ((u64)aph << 32) | apl;
	if (paddr != (__pa(aps)))
		return 0;
	return 1;
}

/* 0 on success */
static int setup_access_params_addr(struct platform_device *pdev,
					struct goldfish_pipe_dev *dev)
{
	u64 paddr;
	struct access_params *aps;

	aps = devm_kzalloc(&pdev->dev, sizeof(struct access_params), GFP_KERNEL);
	if (!aps)
		return -1;

	/* FIXME */
	paddr = __pa(aps);
	writel((u32)(paddr >> 32), dev->base + PIPE_REG_PARAMS_ADDR_HIGH);
	writel((u32)paddr, dev->base + PIPE_REG_PARAMS_ADDR_LOW);

	if (valid_batchbuffer_addr(dev, aps)) {
		dev->aps = aps;
		return 0;
	} else {
		devm_kfree(&pdev->dev, aps);
		return -1;
	}
}

/* A value that will not be set by qemu emulator */
#define INITIAL_BATCH_RESULT (0xdeadbeaf)
static int access_with_param(struct goldfish_pipe_dev *dev, const int cmd,
				unsigned long address, unsigned long avail,
				struct goldfish_pipe *pipe, int *status)
{
	struct access_params *aps = dev->aps;

	if (aps == NULL)
		return -1;

	aps->result = INITIAL_BATCH_RESULT;
	aps->channel = (unsigned long)pipe;
	aps->size = avail;
	aps->address = address;
	aps->cmd = cmd;
	writel(cmd, dev->base + PIPE_REG_ACCESS_PARAMS);
	/*
	 * If the aps->result has not changed, that means
	 * that the batch command failed
	 */
	if (aps->result == INITIAL_BATCH_RESULT)
		return -1;
	*status = aps->result;
	return 0;
}

static ssize_t goldfish_pipe_read_write(struct file *filp, char __user *buffer,
				    size_t bufflen, int is_write)
{
	unsigned long irq_flags;
	struct goldfish_pipe *pipe = filp->private_data;
	struct goldfish_pipe_dev *dev = pipe->dev;
	unsigned long address, address_end;
	struct page* pages[MAX_PAGES_TO_GRAB] = {};
	int count = 0, ret = -EINVAL;

	/* If the emulator already closed the pipe, no need to go further */
	if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags))
		return -EIO;

	/* Null reads or writes succeeds */
	if (unlikely(bufflen == 0))
		return 0;

	/* Check the buffer range for access */
	if (!access_ok(is_write ? VERIFY_WRITE : VERIFY_READ,
			buffer, bufflen))
		return -EFAULT;

	/* Serialize access to the pipe */
	if (mutex_lock_interruptible(&pipe->lock))
		return -ERESTARTSYS;

	address = (unsigned long)(void *)buffer;
	address_end = address + bufflen;

	while (address < address_end) {
		unsigned long page_end = (address & PAGE_MASK) + PAGE_SIZE;
		unsigned long next, avail;
		int status, wakeBit, page_i, num_contiguous_pages;
		long first_page, last_page, requested_pages;
		unsigned long xaddr, xaddr_prev, xaddr_i;

		/*
		 * Attempt to grab multiple physically contiguous pages.
		 */
		first_page = address & PAGE_MASK;
		last_page = (address_end - 1) & PAGE_MASK;
		requested_pages = ((last_page - first_page) >> PAGE_SHIFT) + 1;
		if (requested_pages > MAX_PAGES_TO_GRAB) {
			requested_pages = MAX_PAGES_TO_GRAB;
		}
		ret = get_user_pages_fast(first_page, requested_pages,
				!is_write, pages);

		DPRINT("%s: requested pages: %d %d %p\n", __FUNCTION__,
			ret, requested_pages, first_page);
		if (ret == 0) {
			DPRINT("%s: error: (requested pages == 0) (wanted %d)\n",
					__FUNCTION__, requested_pages);
			return ret;
		}
		if (ret < 0) {
			DPRINT("%s: (requested pages < 0) %d \n",
				 	__FUNCTION__, requested_pages);
			return ret;
		}

		xaddr = page_to_phys(pages[0]) | (address & ~PAGE_MASK);
		xaddr_prev = xaddr;
		num_contiguous_pages = ret == 0 ? 0 : 1;
		for (page_i = 1; page_i < ret; page_i++) {
			xaddr_i = page_to_phys(pages[page_i]) | (address & ~PAGE_MASK);
			if (xaddr_i == xaddr_prev + PAGE_SIZE) {
				page_end += PAGE_SIZE;
				xaddr_prev = xaddr_i;
				num_contiguous_pages++;
			} else {
				DPRINT("%s: discontinuous page boundary: %d pages instead\n",
						__FUNCTION__, page_i);
				break;
			}
		}
		next = page_end < address_end ? page_end : address_end;
		avail = next - address;

		/* Now, try to transfer the bytes in the current page */
		spin_lock_irqsave(&dev->lock, irq_flags);
		if (access_with_param(dev,
					is_write ? CMD_WRITE_BUFFER : CMD_READ_BUFFER,
					xaddr, avail, pipe, &status)) {
			gf_write_ptr(pipe, dev->base + PIPE_REG_CHANNEL,
				     dev->base + PIPE_REG_CHANNEL_HIGH);
			writel(avail, dev->base + PIPE_REG_SIZE);
			gf_write_ptr((void *)xaddr,
				     dev->base + PIPE_REG_ADDRESS,
				     dev->base + PIPE_REG_ADDRESS_HIGH);
			writel(is_write ? CMD_WRITE_BUFFER : CMD_READ_BUFFER,
			       dev->base + PIPE_REG_COMMAND);
			status = readl(dev->base + PIPE_REG_STATUS);
		}
		spin_unlock_irqrestore(&dev->lock, irq_flags);

		for (page_i = 0; page_i < ret; page_i++) {
			if (status > 0 && !is_write &&
				page_i < num_contiguous_pages) {
				set_page_dirty(pages[page_i]);
			}
			put_page(pages[page_i]);
		}

		if (status > 0) { /* Correct transfer */
			count += status;
			address += status;
			continue;
		} else if (status == 0) { /* EOF */
			ret = 0;
			break;
		} else if (status < 0 && count > 0) {
			/*
			 * An error occured and we already transfered
			 * something on one of the previous pages.
			 * Just return what we already copied and log this
			 * err.
			 *
			 * Note: This seems like an incorrect approach but
			 * cannot change it until we check if any user space
			 * ABI relies on this behavior.
			 */
			if (status != PIPE_ERROR_AGAIN)
				pr_info_ratelimited("goldfish_pipe: backend returned error %d on %s\n",
						status, is_write ? "write" : "read");
			ret = 0;
			break;
		}

		/*
		 * If the error is not PIPE_ERROR_AGAIN, or if we are not in
		 * non-blocking mode, just return the error code.
		 */
		if (status != PIPE_ERROR_AGAIN ||
				(filp->f_flags & O_NONBLOCK) != 0) {
			ret = goldfish_pipe_error_convert(status);
			break;
		}

		/*
		 * The backend blocked the read/write, wait until the backend
		 * tells us it's ready to process more data.
		 */
		wakeBit = is_write ? BIT_WAKE_ON_WRITE : BIT_WAKE_ON_READ;
		set_bit(wakeBit, &pipe->flags);

		/* Tell the emulator we're going to wait for a wake event */
		goldfish_cmd(pipe,
				is_write ? CMD_WAKE_ON_WRITE : CMD_WAKE_ON_READ);

		/* Unlock the pipe, then wait for the wake signal */
		mutex_unlock(&pipe->lock);

		while (test_bit(wakeBit, &pipe->flags)) {
			if (wait_event_interruptible(
					pipe->wake_queue,
					!test_bit(wakeBit, &pipe->flags)))
				return -ERESTARTSYS;

			if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags))
				return -EIO;
		}

		/* Try to re-acquire the lock */
		if (mutex_lock_interruptible(&pipe->lock)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	mutex_unlock(&pipe->lock);

	if (ret < 0)
		return ret;
	else
		return count;
}

static ssize_t goldfish_pipe_read(struct file *filp, char __user *buffer,
			      size_t bufflen, loff_t *ppos)
{
	return goldfish_pipe_read_write(filp, buffer, bufflen, 0);
}

static ssize_t goldfish_pipe_write(struct file *filp,
				const char __user *buffer, size_t bufflen,
				loff_t *ppos)
{
	return goldfish_pipe_read_write(filp, (char __user *)buffer,
								bufflen, 1);
}


static unsigned int goldfish_pipe_poll(struct file *filp, poll_table *wait)
{
	struct goldfish_pipe *pipe = filp->private_data;
	unsigned int mask = 0;
	int status;

	mutex_lock(&pipe->lock);

	poll_wait(filp, &pipe->wake_queue, wait);

	status = goldfish_cmd_status(pipe, CMD_POLL);

	mutex_unlock(&pipe->lock);

	if (status & PIPE_POLL_IN)
		mask |= POLLIN | POLLRDNORM;

	if (status & PIPE_POLL_OUT)
		mask |= POLLOUT | POLLWRNORM;

	if (status & PIPE_POLL_HUP)
		mask |= POLLHUP;

	if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags))
		mask |= POLLERR;

	return mask;
}

static irqreturn_t goldfish_pipe_interrupt(int irq, void *dev_id)
{
	struct goldfish_pipe_dev *dev = dev_id;
	unsigned long irq_flags;
	int count = 0;

	/*
	 * We're going to read from the emulator a list of (channel,flags)
	 * pairs corresponding to the wake events that occured on each
	 * blocked pipe (i.e. channel).
	 */
	spin_lock_irqsave(&dev->lock, irq_flags);
	for (;;) {
		/* First read the channel, 0 means the end of the list */
		struct goldfish_pipe *pipe;
		unsigned long wakes;
		unsigned long channel = 0;

#ifdef CONFIG_64BIT
		channel = (u64)readl(dev->base + PIPE_REG_CHANNEL_HIGH) << 32;

		if (channel == 0)
			break;
#endif
		channel |= readl(dev->base + PIPE_REG_CHANNEL);

		if (channel == 0)
			break;

		/* Convert channel to struct pipe pointer + read wake flags */
		wakes = readl(dev->base + PIPE_REG_WAKES);
		pipe  = (struct goldfish_pipe *)(ptrdiff_t)channel;

		/* Did the emulator just closed a pipe? */
		if (wakes & PIPE_WAKE_CLOSED) {
			set_bit(BIT_CLOSED_ON_HOST, &pipe->flags);
			wakes |= PIPE_WAKE_READ | PIPE_WAKE_WRITE;
		}
		if (wakes & PIPE_WAKE_READ)
			clear_bit(BIT_WAKE_ON_READ, &pipe->flags);
		if (wakes & PIPE_WAKE_WRITE)
			clear_bit(BIT_WAKE_ON_WRITE, &pipe->flags);

		wake_up_interruptible(&pipe->wake_queue);
		count++;
	}
	spin_unlock_irqrestore(&dev->lock, irq_flags);

	return (count == 0) ? IRQ_NONE : IRQ_HANDLED;
}

/**
 *	goldfish_pipe_open	-	open a channel to the AVD
 *	@inode: inode of device
 *	@file: file struct of opener
 *
 *	Create a new pipe link between the emulator and the use application.
 *	Each new request produces a new pipe.
 *
 *	Note: we use the pipe ID as a mux. All goldfish emulations are 32bit
 *	right now so this is fine. A move to 64bit will need this addressing
 */
static int goldfish_pipe_open(struct inode *inode, struct file *file)
{
	struct goldfish_pipe *pipe;
	struct goldfish_pipe_dev *dev = pipe_dev;
	int32_t status;

	/* Allocate new pipe kernel object */
	pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
	if (pipe == NULL)
		return -ENOMEM;

	pipe->dev = dev;
	mutex_init(&pipe->lock);
	DPRINT("%s: call. pipe_dev pipe_dev=0x%lx new_pipe_addr=0x%lx file=0x%lx\n", __FUNCTION__, pipe_dev, pipe, file);
	// spin lock init, write head of list, i guess
	init_waitqueue_head(&pipe->wake_queue);

	/*
	 * Now, tell the emulator we're opening a new pipe. We use the
	 * pipe object's address as the channel identifier for simplicity.
	 */

	status = goldfish_cmd_status(pipe, CMD_OPEN);
	if (status < 0) {
		kfree(pipe);
		return status;
	}

	/* All is done, save the pipe into the file's private data field */
	file->private_data = pipe;
	return 0;
}

static int goldfish_pipe_release(struct inode *inode, struct file *filp)
{
	struct goldfish_pipe *pipe = filp->private_data;

	DPRINT("%s: call. pipe=0x%lx file=0x%lx\n", __FUNCTION__, pipe, filp);
	/* The guest is closing the channel, so tell the emulator right now */
	goldfish_cmd(pipe, CMD_CLOSE);
	kfree(pipe);
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations goldfish_pipe_fops = {
	.owner = THIS_MODULE,
	.read = goldfish_pipe_read,
	.write = goldfish_pipe_write,
	.poll = goldfish_pipe_poll,
	.open = goldfish_pipe_open,
	.release = goldfish_pipe_release,
};

static struct miscdevice goldfish_pipe_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "goldfish_pipe",
	.fops = &goldfish_pipe_fops,
};

int goldfish_pipe_device_init_v1(struct platform_device *pdev)
{
	struct goldfish_pipe_dev *dev = pipe_dev;
	int err = devm_request_irq(&pdev->dev, dev->irq, goldfish_pipe_interrupt,
				IRQF_SHARED, "goldfish_pipe", dev);
	if (err) {
		dev_err(&pdev->dev, "unable to allocate IRQ for v1\n");
		return err;
	}

	err = misc_register(&goldfish_pipe_dev);
	if (err) {
		dev_err(&pdev->dev, "unable to register v1 device\n");
		return err;
	}

	setup_access_params_addr(pdev, dev);
	return 0;
}

void goldfish_pipe_device_deinit_v1(struct platform_device *pdev)
{
    misc_deregister(&goldfish_pipe_dev);
}
