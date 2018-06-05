// SPDX-License-Identifier: GPL-2.0+
/*
 * f_fs.c -- user mode file system API for USB composite function controllers
 *
 * Copyright (C) 2010 Samsung Electronics
 * Author: Michal Nazarewicz <mina86@mina86.com>
 *
 * Based on inode.c (GadgetFS) which was:
 * Copyright (C) 2003-2004 David Brownell
 * Copyright (C) 2003 Agilent Technologies
 */


/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/export.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/uio.h>
#include <asm/unaligned.h>

#include <linux/usb/composite.h>
#include <linux/usb/functionfs.h>

#include <linux/aio.h>
#include <linux/mmu_context.h>
#include <linux/poll.h>
#include <linux/eventfd.h>

#include "u_fs.h"
#include "u_f.h"
#include "u_os_desc.h"
#include "configfs.h"

#define FUNCTIONFS_MAGIC	0xa647361 /* Chosen by a honest dice roll ;) */

/* Reference counter handling */
static void ffs_data_get(struct ffs_data *ffs);
static void ffs_data_put(struct ffs_data *ffs);
/* Creates new ffs_data object. */
static struct ffs_data *__must_check ffs_data_new(const char *dev_name)
	__attribute__((malloc));

/* Opened counter handling. */
static void ffs_data_opened(struct ffs_data *ffs);
static void ffs_data_closed(struct ffs_data *ffs);

/* Called with ffs->mutex held; take over ownership of data. */
static int __must_check
__ffs_data_got_descs(struct ffs_data *ffs, char *data, size_t len);
static int __must_check
__ffs_data_got_strings(struct ffs_data *ffs, char *data, size_t len);


/* The function structure ***************************************************/

struct ffs_ep;

struct ffs_function {
	struct usb_configuration	*conf;
	struct usb_gadget		*gadget;
	struct ffs_data			*ffs;

	struct ffs_ep			*eps;
	u8				eps_revmap[16];
	short				*interfaces_nums;

	struct usb_function		function;
};


static struct ffs_function *ffs_func_from_usb(struct usb_function *f)
{
	return container_of(f, struct ffs_function, function);
}


static inline enum ffs_setup_state
ffs_setup_state_clear_cancelled(struct ffs_data *ffs)
{
	return (enum ffs_setup_state)
		cmpxchg(&ffs->setup_state, FFS_SETUP_CANCELLED, FFS_NO_SETUP);
}


static void ffs_func_eps_disable(struct ffs_function *func);
static int __must_check ffs_func_eps_enable(struct ffs_function *func);

static int ffs_func_bind(struct usb_configuration *,
			 struct usb_function *);
static int ffs_func_set_alt(struct usb_function *, unsigned, unsigned);
static void ffs_func_disable(struct usb_function *);
static int ffs_func_setup(struct usb_function *,
			  const struct usb_ctrlrequest *);
static bool ffs_func_req_match(struct usb_function *,
			       const struct usb_ctrlrequest *,
			       bool config0);
static void ffs_func_suspend(struct usb_function *);
static void ffs_func_resume(struct usb_function *);


static int ffs_func_revmap_ep(struct ffs_function *func, u8 num);
static int ffs_func_revmap_intf(struct ffs_function *func, u8 intf);


/* The endpoints structures *************************************************/

struct ffs_ep {
	struct usb_ep			*ep;	/* P: ffs->eps_lock */
	struct usb_request		*req;	/* P: epfile->mutex */

	/* [0]: full speed, [1]: high speed, [2]: super speed */
	struct usb_endpoint_descriptor	*descs[3];

	u8				num;

	int				status;	/* P: epfile->mutex */
};

struct ffs_epfile {
	/* Protects ep->ep and ep->req. */
	struct mutex			mutex;

	struct ffs_data			*ffs;
	struct ffs_ep			*ep;	/* P: ffs->eps_lock */

	struct dentry			*dentry;

	/*
	 * Buffer for holding data from partial reads which may happen since
	 * we’re rounding user read requests to a multiple of a max packet size.
	 *
	 * The pointer is initialised with NULL value and may be set by
	 * __ffs_epfile_read_data function to point to a temporary buffer.
	 *
	 * In normal operation, calls to __ffs_epfile_read_buffered will consume
	 * data from said buffer and eventually free it.  Importantly, while the
	 * function is using the buffer, it sets the pointer to NULL.  This is
	 * all right since __ffs_epfile_read_data and __ffs_epfile_read_buffered
	 * can never run concurrently (they are synchronised by epfile->mutex)
	 * so the latter will not assign a new value to the pointer.
	 *
	 * Meanwhile ffs_func_eps_disable frees the buffer (if the pointer is
	 * valid) and sets the pointer to READ_BUFFER_DROP value.  This special
	 * value is crux of the synchronisation between ffs_func_eps_disable and
	 * __ffs_epfile_read_data.
	 *
	 * Once __ffs_epfile_read_data is about to finish it will try to set the
	 * pointer back to its old value (as described above), but seeing as the
	 * pointer is not-NULL (namely READ_BUFFER_DROP) it will instead free
	 * the buffer.
	 *
	 * == State transitions ==
	 *
	 * • ptr == NULL:  (initial state)
	 *   ◦ __ffs_epfile_read_buffer_free: go to ptr == DROP
	 *   ◦ __ffs_epfile_read_buffered:    nop
	 *   ◦ __ffs_epfile_read_data allocates temp buffer: go to ptr == buf
	 *   ◦ reading finishes:              n/a, not in ‘and reading’ state
	 * • ptr == DROP:
	 *   ◦ __ffs_epfile_read_buffer_free: nop
	 *   ◦ __ffs_epfile_read_buffered:    go to ptr == NULL
	 *   ◦ __ffs_epfile_read_data allocates temp buffer: free buf, nop
	 *   ◦ reading finishes:              n/a, not in ‘and reading’ state
	 * • ptr == buf:
	 *   ◦ __ffs_epfile_read_buffer_free: free buf, go to ptr == DROP
	 *   ◦ __ffs_epfile_read_buffered:    go to ptr == NULL and reading
	 *   ◦ __ffs_epfile_read_data:        n/a, __ffs_epfile_read_buffered
	 *                                    is always called first
	 *   ◦ reading finishes:              n/a, not in ‘and reading’ state
	 * • ptr == NULL and reading:
	 *   ◦ __ffs_epfile_read_buffer_free: go to ptr == DROP and reading
	 *   ◦ __ffs_epfile_read_buffered:    n/a, mutex is held
	 *   ◦ __ffs_epfile_read_data:        n/a, mutex is held
	 *   ◦ reading finishes and …
	 *     … all data read:               free buf, go to ptr == NULL
	 *     … otherwise:                   go to ptr == buf and reading
	 * • ptr == DROP and reading:
	 *   ◦ __ffs_epfile_read_buffer_free: nop
	 *   ◦ __ffs_epfile_read_buffered:    n/a, mutex is held
	 *   ◦ __ffs_epfile_read_data:        n/a, mutex is held
	 *   ◦ reading finishes:              free buf, go to ptr == DROP
	 */
	struct ffs_buffer		*read_buffer;
#define READ_BUFFER_DROP ((struct ffs_buffer *)ERR_PTR(-ESHUTDOWN))

	char				name[5];

	unsigned char			in;	/* P: ffs->eps_lock */
	unsigned char			isoc;	/* P: ffs->eps_lock */

	unsigned char			_pad;
};

struct ffs_buffer {
	size_t length;
	char *data;
	char storage[];
};

/*  ffs_io_data structure ***************************************************/

struct ffs_io_data {
	bool aio;
	bool read;

	struct kiocb *kiocb;
	struct iov_iter data;
	const void *to_free;
	char *buf;

	struct mm_struct *mm;
	struct work_struct work;

	struct usb_ep *ep;
	struct usb_request *req;

	struct ffs_data *ffs;
};

struct ffs_desc_helper {
	struct ffs_data *ffs;
	unsigned interfaces_count;
	unsigned eps_count;
};

static int  __must_check ffs_epfiles_create(struct ffs_data *ffs);
static void ffs_epfiles_destroy(struct ffs_epfile *epfiles, unsigned count);

static struct dentry *
ffs_sb_create_file(struct super_block *sb, const char *name, void *data,
		   const struct file_operations *fops);

/* Devices management *******************************************************/

DEFINE_MUTEX(ffs_lock);
EXPORT_SYMBOL_GPL(ffs_lock);

static struct ffs_dev *_ffs_find_dev(const char *name);
static struct ffs_dev *_ffs_alloc_dev(void);
static void _ffs_free_dev(struct ffs_dev *dev);
static void *ffs_acquire_dev(const char *dev_name);
static void ffs_release_dev(struct ffs_data *ffs_data);
static int ffs_ready(struct ffs_data *ffs);
static void ffs_closed(struct ffs_data *ffs);

/* Misc helper functions ****************************************************/

static int ffs_mutex_lock(struct mutex *mutex, unsigned nonblock)
	__attribute__((warn_unused_result, nonnull));
static char *ffs_prepare_buffer(const char __user *buf, size_t len)
	__attribute__((warn_unused_result, nonnull));


/* Control file aka ep0 *****************************************************/

static void ffs_ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct ffs_data *ffs = req->context;

	complete(&ffs->ep0req_completion);
}

static int __ffs_ep0_queue_wait(struct ffs_data *ffs, char *data, size_t len)
	__releases(&ffs->ev.waitq.lock)
{
	struct usb_request *req = ffs->ep0req;
	int ret;

	req->zero     = len < le16_to_cpu(ffs->ev.setup.wLength);

	spin_unlock_irq(&ffs->ev.waitq.lock);

	req->buf      = data;
	req->length   = len;

	/*
	 * UDC layer requires to provide a buffer even for ZLP, but should
	 * not use it at all. Let's provide some poisoned pointer to catch
	 * possible bug in the driver.
	 */
	if (req->buf == NULL)
		req->buf = (void *)0xDEADBABE;

	reinit_completion(&ffs->ep0req_completion);

	ret = usb_ep_queue(ffs->gadget->ep0, req, GFP_ATOMIC);
	if (unlikely(ret < 0))
		return ret;

	ret = wait_for_completion_interruptible(&ffs->ep0req_completion);
	if (unlikely(ret)) {
		usb_ep_dequeue(ffs->gadget->ep0, req);
		return -EINTR;
	}

	ffs->setup_state = FFS_NO_SETUP;
	return req->status ? req->status : req->actual;
}

static int __ffs_ep0_stall(struct ffs_data *ffs)
{
	if (ffs->ev.can_stall) {
		pr_vdebug("ep0 stall\n");
		usb_ep_set_halt(ffs->gadget->ep0);
		ffs->setup_state = FFS_NO_SETUP;
		return -EL2HLT;
	} else {
		pr_debug("bogus ep0 stall!\n");
		return -ESRCH;
	}
}

static ssize_t ffs_ep0_write(struct file *file, const char __user *buf,
			     size_t len, loff_t *ptr)
{
	struct ffs_data *ffs = file->private_data;
	ssize_t ret;
	char *data;

	ENTER();

	/* Fast check if setup was canceled */
	if (ffs_setup_state_clear_cancelled(ffs) == FFS_SETUP_CANCELLED)
		return -EIDRM;

	/* Acquire mutex */
	ret = ffs_mutex_lock(&ffs->mutex, file->f_flags & O_NONBLOCK);
	if (unlikely(ret < 0))
		return ret;

	/* Check state */
	switch (ffs->state) {
	case FFS_READ_DESCRIPTORS:
	case FFS_READ_STRINGS:
		/* Copy data */
		if (unlikely(len < 16)) {
			ret = -EINVAL;
			break;
		}

		data = ffs_prepare_buffer(buf, len);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			break;
		}

		/* Handle data */
		if (ffs->state == FFS_READ_DESCRIPTORS) {
			pr_info("read descriptors\n");
			ret = __ffs_data_got_descs(ffs, data, len);
			if (unlikely(ret < 0))
				break;

			ffs->state = FFS_READ_STRINGS;
			ret = len;
		} else {
			pr_info("read strings\n");
			ret = __ffs_data_got_strings(ffs, data, len);
			if (unlikely(ret < 0))
				break;

			ret = ffs_epfiles_create(ffs);
			if (unlikely(ret)) {
				ffs->state = FFS_CLOSING;
				break;
			}

			ffs->state = FFS_ACTIVE;
			mutex_unlock(&ffs->mutex);

			ret = ffs_ready(ffs);
			if (unlikely(ret < 0)) {
				ffs->state = FFS_CLOSING;
				return ret;
			}

			return len;
		}
		break;

	case FFS_ACTIVE:
		data = NULL;
		/*
		 * We're called from user space, we can use _irq
		 * rather then _irqsave
		 */
		spin_lock_irq(&ffs->ev.waitq.lock);
		switch (ffs_setup_state_clear_cancelled(ffs)) {
		case FFS_SETUP_CANCELLED:
			ret = -EIDRM;
			goto done_spin;

		case FFS_NO_SETUP:
			ret = -ESRCH;
			goto done_spin;

		case FFS_SETUP_PENDING:
			break;
		}

		/* FFS_SETUP_PENDING */
		if (!(ffs->ev.setup.bRequestType & USB_DIR_IN)) {
			spin_unlock_irq(&ffs->ev.waitq.lock);
			ret = __ffs_ep0_stall(ffs);
			break;
		}

		/* FFS_SETUP_PENDING and not stall */
		len = min(len, (size_t)le16_to_cpu(ffs->ev.setup.wLength));

		spin_unlock_irq(&ffs->ev.waitq.lock);

		data = ffs_prepare_buffer(buf, len);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			break;
		}

		spin_lock_irq(&ffs->ev.waitq.lock);

		/*
		 * We are guaranteed to be still in FFS_ACTIVE state
		 * but the state of setup could have changed from
		 * FFS_SETUP_PENDING to FFS_SETUP_CANCELLED so we need
		 * to check for that.  If that happened we copied data
		 * from user space in vain but it's unlikely.
		 *
		 * For sure we are not in FFS_NO_SETUP since this is
		 * the only place FFS_SETUP_PENDING -> FFS_NO_SETUP
		 * transition can be performed and it's protected by
		 * mutex.
		 */
		if (ffs_setup_state_clear_cancelled(ffs) ==
		    FFS_SETUP_CANCELLED) {
			ret = -EIDRM;
done_spin:
			spin_unlock_irq(&ffs->ev.waitq.lock);
		} else {
			/* unlocks spinlock */
			ret = __ffs_ep0_queue_wait(ffs, data, len);
		}
		kfree(data);
		break;

	default:
		ret = -EBADFD;
		break;
	}

	mutex_unlock(&ffs->mutex);
	return ret;
}

/* Called with ffs->ev.waitq.lock and ffs->mutex held, both released on exit. */
static ssize_t __ffs_ep0_read_events(struct ffs_data *ffs, char __user *buf,
				     size_t n)
	__releases(&ffs->ev.waitq.lock)
{
	/*
	 * n cannot be bigger than ffs->ev.count, which cannot be bigger than
	 * size of ffs->ev.types array (which is four) so that's how much space
	 * we reserve.
	 */
	struct usb_functionfs_event events[ARRAY_SIZE(ffs->ev.types)];
	const size_t size = n * sizeof *events;
	unsigned i = 0;

	memset(events, 0, size);

	do {
		events[i].type = ffs->ev.types[i];
		if (events[i].type == FUNCTIONFS_SETUP) {
			events[i].u.setup = ffs->ev.setup;
			ffs->setup_state = FFS_SETUP_PENDING;
		}
	} while (++i < n);

	ffs->ev.count -= n;
	if (ffs->ev.count)
		memmove(ffs->ev.types, ffs->ev.types + n,
			ffs->ev.count * sizeof *ffs->ev.types);

	spin_unlock_irq(&ffs->ev.waitq.lock);
	mutex_unlock(&ffs->mutex);

	return unlikely(copy_to_user(buf, events, size)) ? -EFAULT : size;
}

static ssize_t ffs_ep0_read(struct file *file, char __user *buf,
			    size_t len, loff_t *ptr)
{
	struct ffs_data *ffs = file->private_data;
	char *data = NULL;
	size_t n;
	int ret;

	ENTER();

	/* Fast check if setup was canceled */
	if (ffs_setup_state_clear_cancelled(ffs) == FFS_SETUP_CANCELLED)
		return -EIDRM;

	/* Acquire mutex */
	ret = ffs_mutex_lock(&ffs->mutex, file->f_flags & O_NONBLOCK);
	if (unlikely(ret < 0))
		return ret;

	/* Check state */
	if (ffs->state != FFS_ACTIVE) {
		ret = -EBADFD;
		goto done_mutex;
	}

	/*
	 * We're called from user space, we can use _irq rather then
	 * _irqsave
	 */
	spin_lock_irq(&ffs->ev.waitq.lock);

	switch (ffs_setup_state_clear_cancelled(ffs)) {
	case FFS_SETUP_CANCELLED:
		ret = -EIDRM;
		break;

	case FFS_NO_SETUP:
		n = len / sizeof(struct usb_functionfs_event);
		if (unlikely(!n)) {
			ret = -EINVAL;
			break;
		}

		if ((file->f_flags & O_NONBLOCK) && !ffs->ev.count) {
			ret = -EAGAIN;
			break;
		}

		if (wait_event_interruptible_exclusive_locked_irq(ffs->ev.waitq,
							ffs->ev.count)) {
			ret = -EINTR;
			break;
		}

		/* unlocks spinlock */
		return __ffs_ep0_read_events(ffs, buf,
					     min(n, (size_t)ffs->ev.count));

	case FFS_SETUP_PENDING:
		if (ffs->ev.setup.bRequestType & USB_DIR_IN) {
			spin_unlock_irq(&ffs->ev.waitq.lock);
			ret = __ffs_ep0_stall(ffs);
			goto done_mutex;
		}

		len = min(len, (size_t)le16_to_cpu(ffs->ev.setup.wLength));

		spin_unlock_irq(&ffs->ev.waitq.lock);

		if (likely(len)) {
			data = kmalloc(len, GFP_KERNEL);
			if (unlikely(!data)) {
				ret = -ENOMEM;
				goto done_mutex;
			}
		}

		spin_lock_irq(&ffs->ev.waitq.lock);

		/* See ffs_ep0_write() */
		if (ffs_setup_state_clear_cancelled(ffs) ==
		    FFS_SETUP_CANCELLED) {
			ret = -EIDRM;
			break;
		}

		/* unlocks spinlock */
		ret = __ffs_ep0_queue_wait(ffs, data, len);
		if (likely(ret > 0) && unlikely(copy_to_user(buf, data, len)))
			ret = -EFAULT;
		goto done_mutex;

	default:
		ret = -EBADFD;
		break;
	}

	spin_unlock_irq(&ffs->ev.waitq.lock);
done_mutex:
	mutex_unlock(&ffs->mutex);
	kfree(data);
	return ret;
}

static int ffs_ep0_open(struct inode *inode, struct file *file)
{
	struct ffs_data *ffs = inode->i_private;

	ENTER();

	if (unlikely(ffs->state == FFS_CLOSING))
		return -EBUSY;

	file->private_data = ffs;
	ffs_data_opened(ffs);

	return 0;
}

static int ffs_ep0_release(struct inode *inode, struct file *file)
{
	struct ffs_data *ffs = file->private_data;

	ENTER();

	ffs_data_closed(ffs);

	return 0;
}

static long ffs_ep0_ioctl(struct file *file, unsigned code, unsigned long value)
{
	struct ffs_data *ffs = file->private_data;
	struct usb_gadget *gadget = ffs->gadget;
	long ret;

	ENTER();

	if (code == FUNCTIONFS_INTERFACE_REVMAP) {
		struct ffs_function *func = ffs->func;
		ret = func ? ffs_func_revmap_intf(func, value) : -ENODEV;
	} else if (gadget && gadget->ops->ioctl) {
		ret = gadget->ops->ioctl(gadget, code, value);
	} else {
		ret = -ENOTTY;
	}

	return ret;
}

static __poll_t ffs_ep0_poll(struct file *file, poll_table *wait)
{
	struct ffs_data *ffs = file->private_data;
	__poll_t mask = EPOLLWRNORM;
	int ret;

	poll_wait(file, &ffs->ev.waitq, wait);

	ret = ffs_mutex_lock(&ffs->mutex, file->f_flags & O_NONBLOCK);
	if (unlikely(ret < 0))
		return mask;

	switch (ffs->state) {
	case FFS_READ_DESCRIPTORS:
	case FFS_READ_STRINGS:
		mask |= EPOLLOUT;
		break;

	case FFS_ACTIVE:
		switch (ffs->setup_state) {
		case FFS_NO_SETUP:
			if (ffs->ev.count)
				mask |= EPOLLIN;
			break;

		case FFS_SETUP_PENDING:
		case FFS_SETUP_CANCELLED:
			mask |= (EPOLLIN | EPOLLOUT);
			break;
		}
	case FFS_CLOSING:
		break;
	case FFS_DEACTIVATED:
		break;
	}

	mutex_unlock(&ffs->mutex);

	return mask;
}

static const struct file_operations ffs_ep0_operations = {
	.llseek =	no_llseek,

	.open =		ffs_ep0_open,
	.write =	ffs_ep0_write,
	.read =		ffs_ep0_read,
	.release =	ffs_ep0_release,
	.unlocked_ioctl =	ffs_ep0_ioctl,
	.poll =		ffs_ep0_poll,
};


/* "Normal" endpoints operations ********************************************/

static void ffs_epfile_io_complete(struct usb_ep *_ep, struct usb_request *req)
{
	ENTER();
	if (likely(req->context)) {
		struct ffs_ep *ep = _ep->driver_data;
		ep->status = req->status ? req->status : req->actual;
		complete(req->context);
	}
}

static ssize_t ffs_copy_to_iter(void *data, int data_len, struct iov_iter *iter)
{
	ssize_t ret = copy_to_iter(data, data_len, iter);
	if (likely(ret == data_len))
		return ret;

	if (unlikely(iov_iter_count(iter)))
		return -EFAULT;

	/*
	 * Dear user space developer!
	 *
	 * TL;DR: To stop getting below error message in your kernel log, change
	 * user space code using functionfs to align read buffers to a max
	 * packet size.
	 *
	 * Some UDCs (e.g. dwc3) require request sizes to be a multiple of a max
	 * packet size.  When unaligned buffer is passed to functionfs, it
	 * internally uses a larger, aligned buffer so that such UDCs are happy.
	 *
	 * Unfortunately, this means that host may send more data than was
	 * requested in read(2) system call.  f_fs doesn’t know what to do with
	 * that excess data so it simply drops it.
	 *
	 * Was the buffer aligned in the first place, no such problem would
	 * happen.
	 *
	 * Data may be dropped only in AIO reads.  Synchronous reads are handled
	 * by splitting a request into multiple parts.  This splitting may still
	 * be a problem though so it’s likely best to align the buffer
	 * regardless of it being AIO or not..
	 *
	 * This only affects OUT endpoints, i.e. reading data with a read(2),
	 * aio_read(2) etc. system calls.  Writing data to an IN endpoint is not
	 * affected.
	 */
	pr_err("functionfs read size %d > requested size %zd, dropping excess data. "
	       "Align read buffer size to max packet size to avoid the problem.\n",
	       data_len, ret);

	return ret;
}

static void ffs_user_copy_worker(struct work_struct *work)
{
	struct ffs_io_data *io_data = container_of(work, struct ffs_io_data,
						   work);
	int ret = io_data->req->status ? io_data->req->status :
					 io_data->req->actual;
	bool kiocb_has_eventfd = io_data->kiocb->ki_flags & IOCB_EVENTFD;

	if (io_data->read && ret > 0) {
		mm_segment_t oldfs = get_fs();

		set_fs(USER_DS);
		use_mm(io_data->mm);
		ret = ffs_copy_to_iter(io_data->buf, ret, &io_data->data);
		unuse_mm(io_data->mm);
		set_fs(oldfs);
	}

	io_data->kiocb->ki_complete(io_data->kiocb, ret, ret);

	if (io_data->ffs->ffs_eventfd && !kiocb_has_eventfd)
		eventfd_signal(io_data->ffs->ffs_eventfd, 1);

	usb_ep_free_request(io_data->ep, io_data->req);

	if (io_data->read)
		kfree(io_data->to_free);
	kfree(io_data->buf);
	kfree(io_data);
}

static void ffs_epfile_async_io_complete(struct usb_ep *_ep,
					 struct usb_request *req)
{
	struct ffs_io_data *io_data = req->context;
	struct ffs_data *ffs = io_data->ffs;

	ENTER();

	INIT_WORK(&io_data->work, ffs_user_copy_worker);
	queue_work(ffs->io_completion_wq, &io_data->work);
}

static void __ffs_epfile_read_buffer_free(struct ffs_epfile *epfile)
{
	/*
	 * See comment in struct ffs_epfile for full read_buffer pointer
	 * synchronisation story.
	 */
	struct ffs_buffer *buf = xchg(&epfile->read_buffer, READ_BUFFER_DROP);
	if (buf && buf != READ_BUFFER_DROP)
		kfree(buf);
}

/* Assumes epfile->mutex is held. */
static ssize_t __ffs_epfile_read_buffered(struct ffs_epfile *epfile,
					  struct iov_iter *iter)
{
	/*
	 * Null out epfile->read_buffer so ffs_func_eps_disable does not free
	 * the buffer while we are using it.  See comment in struct ffs_epfile
	 * for full read_buffer pointer synchronisation story.
	 */
	struct ffs_buffer *buf = xchg(&epfile->read_buffer, NULL);
	ssize_t ret;
	if (!buf || buf == READ_BUFFER_DROP)
		return 0;

	ret = copy_to_iter(buf->data, buf->length, iter);
	if (buf->length == ret) {
		kfree(buf);
		return ret;
	}

	if (unlikely(iov_iter_count(iter))) {
		ret = -EFAULT;
	} else {
		buf->length -= ret;
		buf->data += ret;
	}

	if (cmpxchg(&epfile->read_buffer, NULL, buf))
		kfree(buf);

	return ret;
}

/* Assumes epfile->mutex is held. */
static ssize_t __ffs_epfile_read_data(struct ffs_epfile *epfile,
				      void *data, int data_len,
				      struct iov_iter *iter)
{
	struct ffs_buffer *buf;

	ssize_t ret = copy_to_iter(data, data_len, iter);
	if (likely(data_len == ret))
		return ret;

	if (unlikely(iov_iter_count(iter)))
		return -EFAULT;

	/* See ffs_copy_to_iter for more context. */
	pr_warn("functionfs read size %d > requested size %zd, splitting request into multiple reads.",
		data_len, ret);

	data_len -= ret;
	buf = kmalloc(sizeof(*buf) + data_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	buf->length = data_len;
	buf->data = buf->storage;
	memcpy(buf->storage, data + ret, data_len);

	/*
	 * At this point read_buffer is NULL or READ_BUFFER_DROP (if
	 * ffs_func_eps_disable has been called in the meanwhile).  See comment
	 * in struct ffs_epfile for full read_buffer pointer synchronisation
	 * story.
	 */
	if (unlikely(cmpxchg(&epfile->read_buffer, NULL, buf)))
		kfree(buf);

	return ret;
}

static ssize_t ffs_epfile_io(struct file *file, struct ffs_io_data *io_data)
{
	struct ffs_epfile *epfile = file->private_data;
	struct usb_request *req;
	struct ffs_ep *ep;
	char *data = NULL;
	ssize_t ret, data_len = -EINVAL;
	int halt;

	/* Are we still active? */
	if (WARN_ON(epfile->ffs->state != FFS_ACTIVE))
		return -ENODEV;

	/* Wait for endpoint to be enabled */
	ep = epfile->ep;
	if (!ep) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(
				epfile->ffs->wait, (ep = epfile->ep));
		if (ret)
			return -EINTR;
	}

	/* Do we halt? */
	halt = (!io_data->read == !epfile->in);
	if (halt && epfile->isoc)
		return -EINVAL;

	/* We will be using request and read_buffer */
	ret = ffs_mutex_lock(&epfile->mutex, file->f_flags & O_NONBLOCK);
	if (unlikely(ret))
		goto error;

	/* Allocate & copy */
	if (!halt) {
		struct usb_gadget *gadget;

		/*
		 * Do we have buffered data from previous partial read?  Check
		 * that for synchronous case only because we do not have
		 * facility to ‘wake up’ a pending asynchronous read and push
		 * buffered data to it which we would need to make things behave
		 * consistently.
		 */
		if (!io_data->aio && io_data->read) {
			ret = __ffs_epfile_read_buffered(epfile, &io_data->data);
			if (ret)
				goto error_mutex;
		}

		/*
		 * if we _do_ wait above, the epfile->ffs->gadget might be NULL
		 * before the waiting completes, so do not assign to 'gadget'
		 * earlier
		 */
		gadget = epfile->ffs->gadget;

		spin_lock_irq(&epfile->ffs->eps_lock);
		/* In the meantime, endpoint got disabled or changed. */
		if (epfile->ep != ep) {
			ret = -ESHUTDOWN;
			goto error_lock;
		}
		data_len = iov_iter_count(&io_data->data);
		/*
		 * Controller may require buffer size to be aligned to
		 * maxpacketsize of an out endpoint.
		 */
		if (io_data->read)
			data_len = usb_ep_align_maybe(gadget, ep->ep, data_len);
		spin_unlock_irq(&epfile->ffs->eps_lock);

		data = kmalloc(data_len, GFP_KERNEL);
		if (unlikely(!data)) {
			ret = -ENOMEM;
			goto error_mutex;
		}
		if (!io_data->read &&
		    !copy_from_iter_full(data, data_len, &io_data->data)) {
			ret = -EFAULT;
			goto error_mutex;
		}
	}

	spin_lock_irq(&epfile->ffs->eps_lock);

	if (epfile->ep != ep) {
		/* In the meantime, endpoint got disabled or changed. */
		ret = -ESHUTDOWN;
	} else if (halt) {
		ret = usb_ep_set_halt(ep->ep);
		if (!ret)
			ret = -EBADMSG;
	} else if (unlikely(data_len == -EINVAL)) {
		/*
		 * Sanity Check: even though data_len can't be used
		 * uninitialized at the time I write this comment, some
		 * compilers complain about this situation.
		 * In order to keep the code clean from warnings, data_len is
		 * being initialized to -EINVAL during its declaration, which
		 * means we can't rely on compiler anymore to warn no future
		 * changes won't result in data_len being used uninitialized.
		 * For such reason, we're adding this redundant sanity check
		 * here.
		 */
		WARN(1, "%s: data_len == -EINVAL\n", __func__);
		ret = -EINVAL;
	} else if (!io_data->aio) {
		DECLARE_COMPLETION_ONSTACK(done);
		bool interrupted = false;

		req = ep->req;
		req->buf      = data;
		req->length   = data_len;

		req->context  = &done;
		req->complete = ffs_epfile_io_complete;

		ret = usb_ep_queue(ep->ep, req, GFP_ATOMIC);
		if (unlikely(ret < 0))
			goto error_lock;

		spin_unlock_irq(&epfile->ffs->eps_lock);

		if (unlikely(wait_for_completion_interruptible(&done))) {
			/*
			 * To avoid race condition with ffs_epfile_io_complete,
			 * dequeue the request first then check
			 * status. usb_ep_dequeue API should guarantee no race
			 * condition with req->complete callback.
			 */
			usb_ep_dequeue(ep->ep, req);
			interrupted = ep->status < 0;
		}

		if (interrupted)
			ret = -EINTR;
		else if (io_data->read && ep->status > 0)
			ret = __ffs_epfile_read_data(epfile, data, ep->status,
						     &io_data->data);
		else
			ret = ep->status;
		goto error_mutex;
	} else if (!(req = usb_ep_alloc_request(ep->ep, GFP_ATOMIC))) {
		ret = -ENOMEM;
	} else {
		req->buf      = data;
		req->length   = data_len;

		io_data->buf = data;
		io_data->ep = ep->ep;
		io_data->req = req;
		io_data->ffs = epfile->ffs;

		req->context  = io_data;
		req->complete = ffs_epfile_async_io_complete;

		ret = usb_ep_queue(ep->ep, req, GFP_ATOMIC);
		if (unlikely(ret)) {
			usb_ep_free_request(ep->ep, req);
			goto error_lock;
		}

		ret = -EIOCBQUEUED;
		/*
		 * Do not kfree the buffer in this function.  It will be freed
		 * by ffs_user_copy_worker.
		 */
		data = NULL;
	}

error_lock:
	spin_unlock_irq(&epfile->ffs->eps_lock);
error_mutex:
	mutex_unlock(&epfile->mutex);
error:
	kfree(data);
	return ret;
}

static int
ffs_epfile_open(struct inode *inode, struct file *file)
{
	struct ffs_epfile *epfile = inode->i_private;

	ENTER();

	if (WARN_ON(epfile->ffs->state != FFS_ACTIVE))
		return -ENODEV;

	file->private_data = epfile;
	ffs_data_opened(epfile->ffs);

	return 0;
}

static int ffs_aio_cancel(struct kiocb *kiocb)
{
	struct ffs_io_data *io_data = kiocb->private;
	struct ffs_epfile *epfile = kiocb->ki_filp->private_data;
	int value;

	ENTER();

	spin_lock_irq(&epfile->ffs->eps_lock);

	if (likely(io_data && io_data->ep && io_data->req))
		value = usb_ep_dequeue(io_data->ep, io_data->req);
	else
		value = -EINVAL;

	spin_unlock_irq(&epfile->ffs->eps_lock);

	return value;
}

static ssize_t ffs_epfile_write_iter(struct kiocb *kiocb, struct iov_iter *from)
{
	struct ffs_io_data io_data, *p = &io_data;
	ssize_t res;

	ENTER();

	if (!is_sync_kiocb(kiocb)) {
		p = kmalloc(sizeof(io_data), GFP_KERNEL);
		if (unlikely(!p))
			return -ENOMEM;
		p->aio = true;
	} else {
		p->aio = false;
	}

	p->read = false;
	p->kiocb = kiocb;
	p->data = *from;
	p->mm = current->mm;

	kiocb->private = p;

	if (p->aio)
		kiocb_set_cancel_fn(kiocb, ffs_aio_cancel);

	res = ffs_epfile_io(kiocb->ki_filp, p);
	if (res == -EIOCBQUEUED)
		return res;
	if (p->aio)
		kfree(p);
	else
		*from = p->data;
	return res;
}

static ssize_t ffs_epfile_read_iter(struct kiocb *kiocb, struct iov_iter *to)
{
	struct ffs_io_data io_data, *p = &io_data;
	ssize_t res;

	ENTER();

	if (!is_sync_kiocb(kiocb)) {
		p = kmalloc(sizeof(io_data), GFP_KERNEL);
		if (unlikely(!p))
			return -ENOMEM;
		p->aio = true;
	} else {
		p->aio = false;
	}

	p->read = true;
	p->kiocb = kiocb;
	if (p->aio) {
		p->to_free = dup_iter(&p->data, to, GFP_KERNEL);
		if (!p->to_free) {
			kfree(p);
			return -ENOMEM;
		}
	} else {
		p->data = *to;
		p->to_free = NULL;
	}
	p->mm = current->mm;

	kiocb->private = p;

	if (p->aio)
		kiocb_set_cancel_fn(kiocb, ffs_aio_cancel);

	res = ffs_epfile_io(kiocb->ki_filp, p);
	if (res == -EIOCBQUEUED)
		return res;

	if (p->aio) {
		kfree(p->to_free);
		kfree(p);
	} else {
		*to = p->data;
	}
	return res;
}

static int
ffs_epfile_release(struct inode *inode, struct file *file)
{
	struct ffs_epfile *epfile = inode->i_private;

	ENTER();

	__ffs_epfile_read_buffer_free(epfile);
	ffs_data_closed(epfile->ffs);

	return 0;
}

static long ffs_epfile_ioctl(struct file *file, unsigned code,
			     unsigned long value)
{
	struct ffs_epfile *epfile = file->private_data;
	struct ffs_ep *ep;
	int ret;

	ENTER();

	if (WARN_ON(epfile->ffs->state != FFS_ACTIVE))
		return -ENODEV;

	/* Wait for endpoint to be enabled */
	ep = epfile->ep;
	if (!ep) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(
				epfile->ffs->wait, (ep = epfile->ep));
		if (ret)
			return -EINTR;
	}

	spin_lock_irq(&epfile->ffs->eps_lock);

	/* In the meantime, endpoint got disabled or changed. */
	if (epfile->ep != ep) {
		spin_unlock_irq(&epfile->ffs->eps_lock);
		return -ESHUTDOWN;
	}

	switch (code) {
	case FUNCTIONFS_FIFO_STATUS:
		ret = usb_ep_fifo_status(epfile->ep->ep);
		break;
	case FUNCTIONFS_FIFO_FLUSH:
		usb_ep_fifo_flush(epfile->ep->ep);
		ret = 0;
		break;
	case FUNCTIONFS_CLEAR_HALT:
		ret = usb_ep_clear_halt(epfile->ep->ep);
		break;
	case FUNCTIONFS_ENDPOINT_REVMAP:
		ret = epfile->ep->num;
		break;
	case FUNCTIONFS_ENDPOINT_DESC:
	{
		int desc_idx;
		struct usb_endpoint_descriptor *desc;

		switch (epfile->ffs->gadget->speed) {
		case USB_SPEED_SUPER:
			desc_idx = 2;
			break;
		case USB_SPEED_HIGH:
			desc_idx = 1;
			break;
		default:
			desc_idx = 0;
		}
		desc = epfile->ep->descs[desc_idx];

		spin_unlock_irq(&epfile->ffs->eps_lock);
		ret = copy_to_user((void __user *)value, desc, desc->bLength);
		if (ret)
			ret = -EFAULT;
		return ret;
	}
	default:
		ret = -ENOTTY;
	}
	spin_unlock_irq(&epfile->ffs->eps_lock);

	return ret;
}

#ifdef CONFIG_COMPAT
static long ffs_epfile_compat_ioctl(struct file *file, unsigned code,
		unsigned long value)
{
	return ffs_epfile_ioctl(file, code, value);
}
#endif

static const struct file_operations ffs_epfile_operations = {
	.llseek =	no_llseek,

	.open =		ffs_epfile_open,
	.write_iter =	ffs_epfile_write_iter,
	.read_iter =	ffs_epfile_read_iter,
	.release =	ffs_epfile_release,
	.unlocked_ioctl =	ffs_epfile_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ffs_epfile_compat_ioctl,
#endif
};


/* File system and super block operations ***********************************/

/*
 * Mounting the file system creates a controller file, used first for
 * function configuration then later for event monitoring.
 */

static struct inode *__must_check
ffs_sb_make_inode(struct super_block *sb, void *data,
		  const struct file_operations *fops,
		  const struct inode_operations *iops,
		  struct ffs_file_perms *perms)
{
	struct inode *inode;

	ENTER();

	inode = new_inode(sb);

	if (likely(inode)) {
		struct timespec ts = current_time(inode);

		inode->i_ino	 = get_next_ino();
		inode->i_mode    = perms->mode;
		inode->i_uid     = perms->uid;
		inode->i_gid     = perms->gid;
		inode->i_atime   = ts;
		inode->i_mtime   = ts;
		inode->i_ctime   = ts;
		inode->i_private = data;
		if (fops)
			inode->i_fop = fops;
		if (iops)
			inode->i_op  = iops;
	}

	return inode;
}

/* Create "regular" file */
static struct dentry *ffs_sb_create_file(struct super_block *sb,
					const char *name, void *data,
					const struct file_operations *fops)
{
	struct ffs_data	*ffs = sb->s_fs_info;
	struct dentry	*dentry;
	struct inode	*inode;

	ENTER();

	dentry = d_alloc_name(sb->s_root, name);
	if (unlikely(!dentry))
		return NULL;

	inode = ffs_sb_make_inode(sb, data, fops, NULL, &ffs->file_perms);
	if (unlikely(!inode)) {
		dput(dentry);
		return NULL;
	}

	d_add(dentry, inode);
	return dentry;
}

/* Super block */
static const struct super_operations ffs_sb_operations = {
	.statfs =	simple_statfs,
	.drop_inode =	generic_delete_inode,
};

struct ffs_sb_fill_data {
	struct ffs_file_perms perms;
	umode_t root_mode;
	const char *dev_name;
	bool no_disconnect;
	struct ffs_data *ffs_data;
};

static int ffs_sb_fill(struct super_block *sb, void *_data, int silent)
{
	struct ffs_sb_fill_data *data = _data;
	struct inode	*inode;
	struct ffs_data	*ffs = data->ffs_data;

	ENTER();

	ffs->sb              = sb;
	data->ffs_data       = NULL;
	sb->s_fs_info        = ffs;
	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic          = FUNCTIONFS_MAGIC;
	sb->s_op             = &ffs_sb_operations;
	sb->s_time_gran      = 1;

	/* Root inode */
	data->perms.mode = data->root_mode;
	inode = ffs_sb_make_inode(sb, NULL,
				  &simple_dir_operations,
				  &simple_dir_inode_operations,
				  &data->perms);
	sb->s_root = d_make_root(inode);
	if (unlikely(!sb->s_root))
		return -ENOMEM;

	/* EP0 file */
	if (unlikely(!ffs_sb_create_file(sb, "ep0", ffs,
					 &ffs_ep0_operations)))
		return -ENOMEM;

	return 0;
}

static int ffs_fs_parse_opts(struct ffs_sb_fill_data *data, char *opts)
{
	ENTER();

	if (!opts || !*opts)
		return 0;

	for (;;) {
		unsigned long value;
		char *eq, *comma;

		/* Option limit */
		comma = strchr(opts, ',');
		if (comma)
			*comma = 0;

		/* Value limit */
		eq = strchr(opts, '=');
		if (unlikely(!eq)) {
			pr_err("'=' missing in %s\n", opts);
			return -EINVAL;
		}
		*eq = 0;

		/* Parse value */
		if (kstrtoul(eq + 1, 0, &value)) {
			pr_err("%s: invalid value: %s\n", opts, eq + 1);
			return -EINVAL;
		}

		/* Interpret option */
		switch (eq - opts) {
		case 13:
			if (!memcmp(opts, "no_disconnect", 13))
				data->no_disconnect = !!value;
			else
				goto invalid;
			break;
		case 5:
			if (!memcmp(opts, "rmode", 5))
				data->root_mode  = (value & 0555) | S_IFDIR;
			else if (!memcmp(opts, "fmode", 5))
				data->perms.mode = (value & 0666) | S_IFREG;
			else
				goto invalid;
			break;

		case 4:
			if (!memcmp(opts, "mode", 4)) {
				data->root_mode  = (value & 0555) | S_IFDIR;
				data->perms.mode = (value & 0666) | S_IFREG;
			} else {
				goto invalid;
			}
			break;

		case 3:
			if (!memcmp(opts, "uid", 3)) {
				data->perms.uid = make_kuid(current_user_ns(), value);
				if (!uid_valid(data->perms.uid)) {
					pr_err("%s: unmapped value: %lu\n", opts, value);
					return -EINVAL;
				}
			} else if (!memcmp(opts, "gid", 3)) {
				data->perms.gid = make_kgid(current_user_ns(), value);
				if (!gid_valid(data->perms.gid)) {
					pr_err("%s: unmapped value: %lu\n", opts, value);
					return -EINVAL;
				}
			} else {
				goto invalid;
			}
			break;

		default:
invalid:
			pr_err("%s: invalid option\n", opts);
			return -EINVAL;
		}

		/* Next iteration */
		if (!comma)
			break;
		opts = comma + 1;
	}

	return 0;
}

/* "mount -t functionfs dev_name /dev/function" ends up here */

static struct dentry *
ffs_fs_mount(struct file_system_type *t, int flags,
	      const char *dev_name, void *opts)
{
	struct ffs_sb_fill_data data = {
		.perms = {
			.mode = S_IFREG | 0600,
			.uid = GLOBAL_ROOT_UID,
			.gid = GLOBAL_ROOT_GID,
		},
		.root_mode = S_IFDIR | 0500,
		.no_disconnect = false,
	};
	struct dentry *rv;
	int ret;
	void *ffs_dev;
	struct ffs_data	*ffs;

	ENTER();

	ret = ffs_fs_parse_opts(&data, opts);
	if (unlikely(ret < 0))
		return ERR_PTR(ret);

	ffs = ffs_data_new(dev_name);
	if (unlikely(!ffs))
		return ERR_PTR(-ENOMEM);
	ffs->file_perms = data.perms;
	ffs->no_disconnect = data.no_disconnect;

	ffs->dev_name = kstrdup(dev_name, GFP_KERNEL);
	if (unlikely(!ffs->dev_name)) {
		ffs_data_put(ffs);
		return ERR_PTR(-ENOMEM);
	}

	ffs_dev = ffs_acquire_dev(dev_name);
	if (IS_ERR(ffs_dev)) {
		ffs_data_put(ffs);
		return ERR_CAST(ffs_dev);
	}
	ffs->private_data = ffs_dev;
	data.ffs_data = ffs;

	rv = mount_nodev(t, flags, &data, ffs_sb_fill);
	if (IS_ERR(rv) && data.ffs_data) {
		ffs_release_dev(data.ffs_data);
		ffs_data_put(data.ffs_data);
	}
	return rv;
}

static void
ffs_fs_kill_sb(struct super_block *sb)
{
	ENTER();

	kill_litter_super(sb);
	if (sb->s_fs_info) {
		ffs_release_dev(sb->s_fs_info);
		ffs_data_closed(sb->s_fs_info);
	}
}

static struct file_system_type ffs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "functionfs",
	.mount		= ffs_fs_mount,
	.kill_sb	= ffs_fs_kill_sb,
};
MODULE_ALIAS_FS("functionfs");


/* Driver's main init/cleanup functions *************************************/

static int functionfs_init(void)
{
	int ret;

	ENTER();

	ret = register_filesystem(&ffs_fs_type);
	if (likely(!ret))
		pr_info("file system registered\n");
	else
		pr_err("failed registering file system (%d)\n", ret);

	return ret;
}

static void functionfs_cleanup(void)
{
	ENTER();

	pr_info("unloading\n");
	unregister_filesystem(&ffs_fs_type);
}


/* ffs_data and ffs_function construction and destruction code **************/

static void ffs_data_clear(struct ffs_data *ffs);
static void ffs_data_reset(struct ffs_data *ffs);

static void ffs_data_get(struct ffs_data *ffs)
{
	ENTER();

	refcount_inc(&ffs->ref);
}

static void ffs_data_opened(struct ffs_data *ffs)
{
	ENTER();

	refcount_inc(&ffs->ref);
	if (atomic_add_return(1, &ffs->opened) == 1 &&
			ffs->state == FFS_DEACTIVATED) {
		ffs->state = FFS_CLOSING;
		ffs_data_reset(ffs);
	}
}

static void ffs_data_put(struct ffs_data *ffs)
{
	ENTER();

	if (unlikely(refcount_dec_and_test(&ffs->ref))) {
		pr_info("%s(): freeing\n", __func__);
		ffs_data_clear(ffs);
		BUG_ON(waitqueue_active(&ffs->ev.waitq) ||
		       waitqueue_active(&ffs->ep0req_completion.wait) ||
		       waitqueue_active(&ffs->wait));
		destroy_workqueue(ffs->io_completion_wq);
		kfree(ffs->dev_name);
		kfree(ffs);
	}
}

static void ffs_data_closed(struct ffs_data *ffs)
{
	ENTER();

	if (atomic_dec_and_test(&ffs->opened)) {
		if (ffs->no_disconnect) {
			ffs->state = FFS_DEACTIVATED;
			if (ffs->epfiles) {
				ffs_epfiles_destroy(ffs->epfiles,
						   ffs->eps_count);
				ffs->epfiles = NULL;
			}
			if (ffs->setup_state == FFS_SETUP_PENDING)
				__ffs_ep0_stall(ffs);
		} else {
			ffs->state = FFS_CLOSING;
			ffs_data_reset(ffs);
		}
	}
	if (atomic_read(&ffs->opened) < 0) {
		ffs->state = FFS_CLOSING;
		ffs_data_reset(ffs);
	}

	ffs_data_put(ffs);
}

static struct ffs_data *ffs_data_new(const char *dev_name)
{
	struct ffs_data *ffs = kzalloc(sizeof *ffs, GFP_KERNEL);
	if (unlikely(!ffs))
		return NULL;

	ENTER();

	ffs->io_completion_wq = alloc_ordered_workqueue("%s", 0, dev_name);
	if (!ffs->io_completion_wq) {
		kfree(ffs);
		return NULL;
	}

	refcount_set(&ffs->ref, 1);
	atomic_set(&ffs->opened, 0);
	ffs->state = FFS_READ_DESCRIPTORS;
	mutex_init(&ffs->mutex);
	spin_lock_init(&ffs->eps_lock);
	init_waitqueue_head(&ffs->ev.waitq);
	init_waitqueue_head(&ffs->wait);
	init_completion(&ffs->ep0req_completion);

	/* XXX REVISIT need to update it in some places, or do we? */
	ffs->ev.can_stall = 1;

	return ffs;
}

static void ffs_data_clear(struct ffs_data *ffs)
{
	ENTER();

	ffs_closed(ffs);

	BUG_ON(ffs->gadget);

	if (ffs->epfiles)
		ffs_epfiles_destroy(ffs->epfiles, ffs->eps_count);

	if (ffs->ffs_eventfd)
		eventfd_ctx_put(ffs->ffs_eventfd);

	kfree(ffs->raw_descs_data);
	kfree(ffs->raw_strings);
	kfree(ffs->stringtabs);
}

static void ffs_data_reset(struct ffs_data *ffs)
{
	ENTER();

	ffs_data_clear(ffs);

	ffs->epfiles = NULL;
	ffs->raw_descs_data = NULL;
	ffs->raw_descs = NULL;
	ffs->raw_strings = NULL;
	ffs->stringtabs = NULL;

	ffs->raw_descs_length = 0;
	ffs->fs_descs_count = 0;
	ffs->hs_descs_count = 0;
	ffs->ss_descs_count = 0;

	ffs->strings_count = 0;
	ffs->interfaces_count = 0;
	ffs->eps_count = 0;

	ffs->ev.count = 0;

	ffs->state = FFS_READ_DESCRIPTORS;
	ffs->setup_state = FFS_NO_SETUP;
	ffs->flags = 0;
}


static int functionfs_bind(struct ffs_data *ffs, struct usb_composite_dev *cdev)
{
	struct usb_gadget_strings **lang;
	int first_id;

	ENTER();

	if (WARN_ON(ffs->state != FFS_ACTIVE
		 || test_and_set_bit(FFS_FL_BOUND, &ffs->flags)))
		return -EBADFD;

	first_id = usb_string_ids_n(cdev, ffs->strings_count);
	if (unlikely(first_id < 0))
		return first_id;

	ffs->ep0req = usb_ep_alloc_request(cdev->gadget->ep0, GFP_KERNEL);
	if (unlikely(!ffs->ep0req))
		return -ENOMEM;
	ffs->ep0req->complete = ffs_ep0_complete;
	ffs->ep0req->context = ffs;

	lang = ffs->stringtabs;
	if (lang) {
		for (; *lang; ++lang) {
			struct usb_string *str = (*lang)->strings;
			int id = first_id;
			for (; str->s; ++id, ++str)
				str->id = id;
		}
	}

	ffs->gadget = cdev->gadget;
	ffs_data_get(ffs);
	return 0;
}

static void functionfs_unbind(struct ffs_data *ffs)
{
	ENTER();

	if (!WARN_ON(!ffs->gadget)) {
		usb_ep_free_request(ffs->gadget->ep0, ffs->ep0req);
		ffs->ep0req = NULL;
		ffs->gadget = NULL;
		clear_bit(FFS_FL_BOUND, &ffs->flags);
		ffs_data_put(ffs);
	}
}

static int ffs_epfiles_create(struct ffs_data *ffs)
{
	struct ffs_epfile *epfile, *epfiles;
	unsigned i, count;

	ENTER();

	count = ffs->eps_count;
	epfiles = kcalloc(count, sizeof(*epfiles), GFP_KERNEL);
	if (!epfiles)
		return -ENOMEM;

	epfile = epfiles;
	for (i = 1; i <= count; ++i, ++epfile) {
		epfile->ffs = ffs;
		mutex_init(&epfile->mutex);
		if (ffs->user_flags & FUNCTIONFS_VIRTUAL_ADDR)
			sprintf(epfile->name, "ep%02x", ffs->eps_addrmap[i]);
		else
			sprintf(epfile->name, "ep%u", i);
		epfile->dentry = ffs_sb_create_file(ffs->sb, epfile->name,
						 epfile,
						 &ffs_epfile_operations);
		if (unlikely(!epfile->dentry)) {
			ffs_epfiles_destroy(epfiles, i - 1);
			return -ENOMEM;
		}
	}

	ffs->epfiles = epfiles;
	return 0;
}

static void ffs_epfiles_destroy(struct ffs_epfile *epfiles, unsigned count)
{
	struct ffs_epfile *epfile = epfiles;

	ENTER();

	for (; count; --count, ++epfile) {
		BUG_ON(mutex_is_locked(&epfile->mutex));
		if (epfile->dentry) {
			d_delete(epfile->dentry);
			dput(epfile->dentry);
			epfile->dentry = NULL;
		}
	}

	kfree(epfiles);
}

static void ffs_func_eps_disable(struct ffs_function *func)
{
	struct ffs_ep *ep         = func->eps;
	struct ffs_epfile *epfile = func->ffs->epfiles;
	unsigned count            = func->ffs->eps_count;
	unsigned long flags;

	spin_lock_irqsave(&func->ffs->eps_lock, flags);
	while (count--) {
		/* pending requests get nuked */
		if (likely(ep->ep))
			usb_ep_disable(ep->ep);
		++ep;

		if (epfile) {
			epfile->ep = NULL;
			__ffs_epfile_read_buffer_free(epfile);
			++epfile;
		}
	}
	spin_unlock_irqrestore(&func->ffs->eps_lock, flags);
}

static int ffs_func_eps_enable(struct ffs_function *func)
{
	struct ffs_data *ffs      = func->ffs;
	struct ffs_ep *ep         = func->eps;
	struct ffs_epfile *epfile = ffs->epfiles;
	unsigned count            = ffs->eps_count;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&func->ffs->eps_lock, flags);
	while(count--) {
		ep->ep->driver_data = ep;

		ret = config_ep_by_speed(func->gadget, &func->function, ep->ep);
		if (ret) {
			pr_err("%s: config_ep_by_speed(%s) returned %d\n",
					__func__, ep->ep->name, ret);
			break;
		}

		ret = usb_ep_enable(ep->ep);
		if (likely(!ret)) {
			epfile->ep = ep;
			epfile->in = usb_endpoint_dir_in(ep->ep->desc);
			epfile->isoc = usb_endpoint_xfer_isoc(ep->ep->desc);
		} else {
			break;
		}

		++ep;
		++epfile;
	}

	wake_up_interruptible(&ffs->wait);
	spin_unlock_irqrestore(&func->ffs->eps_lock, flags);

	return ret;
}


/* Parsing and building descriptors and strings *****************************/

/*
 * This validates if data pointed by data is a valid USB descriptor as
 * well as record how many interfaces, endpoints and strings are
 * required by given configuration.  Returns address after the
 * descriptor or NULL if data is invalid.
 */

enum ffs_entity_type {
	FFS_DESCRIPTOR, FFS_INTERFACE, FFS_STRING, FFS_ENDPOINT
};

enum ffs_os_desc_type {
	FFS_OS_DESC, FFS_OS_DESC_EXT_COMPAT, FFS_OS_DESC_EXT_PROP
};

typedef int (*ffs_entity_callback)(enum ffs_entity_type entity,
				   u8 *valuep,
				   struct usb_descriptor_header *desc,
				   void *priv);

typedef int (*ffs_os_desc_callback)(enum ffs_os_desc_type entity,
				    struct usb_os_desc_header *h, void *data,
				    unsigned len, void *priv);

static int __must_check ffs_do_single_desc(char *data, unsigned len,
					   ffs_entity_callback entity,
					   void *priv)
{
	struct usb_descriptor_header *_ds = (void *)data;
	u8 length;
	int ret;

	ENTER();

	/* At least two bytes are required: length and type */
	if (len < 2) {
		pr_vdebug("descriptor too short\n");
		return -EINVAL;
	}

	/* If we have at least as many bytes as the descriptor takes? */
	length = _ds->bLength;
	if (len < length) {
		pr_vdebug("descriptor longer then available data\n");
		return -EINVAL;
	}

#define __entity_check_INTERFACE(val)  1
#define __entity_check_STRING(val)     (val)
#define __entity_check_ENDPOINT(val)   ((val) & USB_ENDPOINT_NUMBER_MASK)
#define __entity(type, val) do {					\
		pr_vdebug("entity " #type "(%02x)\n", (val));		\
		if (unlikely(!__entity_check_ ##type(val))) {		\
			pr_vdebug("invalid entity's value\n");		\
			return -EINVAL;					\
		}							\
		ret = entity(FFS_ ##type, &val, _ds, priv);		\
		if (unlikely(ret < 0)) {				\
			pr_debug("entity " #type "(%02x); ret = %d\n",	\
				 (val), ret);				\
			return ret;					\
		}							\
	} while (0)

	/* Parse descriptor depending on type. */
	switch (_ds->bDescriptorType) {
	case USB_DT_DEVICE:
	case USB_DT_CONFIG:
	case USB_DT_STRING:
	case USB_DT_DEVICE_QUALIFIER:
		/* function can't have any of those */
		pr_vdebug("descriptor reserved for gadget: %d\n",
		      _ds->bDescriptorType);
		return -EINVAL;

	case USB_DT_INTERFACE: {
		struct usb_interface_descriptor *ds = (void *)_ds;
		pr_vdebug("interface descriptor\n");
		if (length != sizeof *ds)
			goto inv_length;

		__entity(INTERFACE, ds->bInterfaceNumber);
		if (ds->iInterface)
			__entity(STRING, ds->iInterface);
	}
		break;

	case USB_DT_ENDPOINT: {
		struct usb_endpoint_descriptor *ds = (void *)_ds;
		pr_vdebug("endpoint descriptor\n");
		if (length != USB_DT_ENDPOINT_SIZE &&
		    length != USB_DT_ENDPOINT_AUDIO_SIZE)
			goto inv_length;
		__entity(ENDPOINT, ds->bEndpointAddress);
	}
		break;

	case HID_DT_HID:
		pr_vdebug("hid descriptor\n");
		if (length != sizeof(struct hid_descriptor))
			goto inv_length;
		break;

	case USB_DT_OTG:
		if (length != sizeof(struct usb_otg_descriptor))
			goto inv_length;
		break;

	case USB_DT_INTERFACE_ASSOCIATION: {
		struct usb_interface_assoc_descriptor *ds = (void *)_ds;
		pr_vdebug("interface association descriptor\n");
		if (length != sizeof *ds)
			goto inv_length;
		if (ds->iFunction)
			__entity(STRING, ds->iFunction);
	}
		break;

	case USB_DT_SS_ENDPOINT_COMP:
		pr_vdebug("EP SS companion descriptor\n");
		if (length != sizeof(struct usb_ss_ep_comp_descriptor))
			goto inv_length;
		break;

	case USB_DT_OTHER_SPEED_CONFIG:
	case USB_DT_INTERFACE_POWER:
	case USB_DT_DEBUG:
	case USB_DT_SECURITY:
	case USB_DT_CS_RADIO_CONTROL:
		/* TODO */
		pr_vdebug("unimplemented descriptor: %d\n", _ds->bDescriptorType);
		return -EINVAL;

	default:
		/* We should never be here */
		pr_vdebug("unknown descriptor: %d\n", _ds->bDescriptorType);
		return -EINVAL;

inv_length:
		pr_vdebug("invalid length: %d (descriptor %d)\n",
			  _ds->bLength, _ds->bDescriptorType);
		return -EINVAL;
	}

#undef __entity
#undef __entity_check_DESCRIPTOR
#undef __entity_check_INTERFACE
#undef __entity_check_STRING
#undef __entity_check_ENDPOINT

	return length;
}

static int __must_check ffs_do_descs(unsigned count, char *data, unsigned len,
				     ffs_entity_callback entity, void *priv)
{
	const unsigned _len = len;
	unsigned long num = 0;

	ENTER();

	for (;;) {
		int ret;

		if (num == count)
			data = NULL;

		/* Record "descriptor" entity */
		ret = entity(FFS_DESCRIPTOR, (u8 *)num, (void *)data, priv);
		if (unlikely(ret < 0)) {
			pr_debug("entity DESCRIPTOR(%02lx); ret = %d\n",
				 num, ret);
			return ret;
		}

		if (!data)
			return _len - len;

		ret = ffs_do_single_desc(data, len, entity, priv);
		if (unlikely(ret < 0)) {
			pr_debug("%s returns %d\n", __func__, ret);
			return ret;
		}

		len -= ret;
		data += ret;
		++num;
	}
}

static int __ffs_data_do_entity(enum ffs_entity_type type,
				u8 *valuep, struct usb_descriptor_header *desc,
				void *priv)
{
	struct ffs_desc_helper *helper = priv;
	struct usb_endpoint_descriptor *d;

	ENTER();

	switch (type) {
	case FFS_DESCRIPTOR:
		break;

	case FFS_INTERFACE:
		/*
		 * Interfaces are indexed from zero so if we
		 * encountered interface "n" then there are at least
		 * "n+1" interfaces.
		 */
		if (*valuep >= helper->interfaces_count)
			helper->interfaces_count = *valuep + 1;
		break;

	case FFS_STRING:
		/*
		 * Strings are indexed from 1 (0 is reserved
		 * for languages list)
		 */
		if (*valuep > helper->ffs->strings_count)
			helper->ffs->strings_count = *valuep;
		break;

	case FFS_ENDPOINT:
		d = (void *)desc;
		helper->eps_count++;
		if (helper->eps_count >= FFS_MAX_EPS_COUNT)
			return -EINVAL;
		/* Check if descriptors for any speed were already parsed */
		if (!helper->ffs->eps_count && !helper->ffs->interfaces_count)
			helper->ffs->eps_addrmap[helper->eps_count] =
				d->bEndpointAddress;
		else if (helper->ffs->eps_addrmap[helper->eps_count] !=
				d->bEndpointAddress)
			return -EINVAL;
		break;
	}

	return 0;
}

static int __ffs_do_os_desc_header(enum ffs_os_desc_type *next_type,
				   struct usb_os_desc_header *desc)
{
	u16 bcd_version = le16_to_cpu(desc->bcdVersion);
	u16 w_index = le16_to_cpu(desc->wIndex);

	if (bcd_version != 1) {
		pr_vdebug("unsupported os descriptors version: %d",
			  bcd_version);
		return -EINVAL;
	}
	switch (w_index) {
	case 0x4:
		*next_type = FFS_OS_DESC_EXT_COMPAT;
		break;
	case 0x5:
		*next_type = FFS_OS_DESC_EXT_PROP;
		break;
	default:
		pr_vdebug("unsupported os descriptor type: %d", w_index);
		return -EINVAL;
	}

	return sizeof(*desc);
}

/*
 * Process all extended compatibility/extended property descriptors
 * of a feature descriptor
 */
static int __must_check ffs_do_single_os_desc(char *data, unsigned len,
					      enum ffs_os_desc_type type,
					      u16 feature_count,
					      ffs_os_desc_callback entity,
					      void *priv,
					      struct usb_os_desc_header *h)
{
	int ret;
	const unsigned _len = len;

	ENTER();

	/* loop over all ext compat/ext prop descriptors */
	while (feature_count--) {
		ret = entity(type, h, data, len, priv);
		if (unlikely(ret < 0)) {
			pr_debug("bad OS descriptor, type: %d\n", type);
			return ret;
		}
		data += ret;
		len -= ret;
	}
	return _len - len;
}

/* Process a number of complete Feature Descriptors (Ext Compat or Ext Prop) */
static int __must_check ffs_do_os_descs(unsigned count,
					char *data, unsigned len,
					ffs_os_desc_callback entity, void *priv)
{
	const unsigned _len = len;
	unsigned long num = 0;

	ENTER();

	for (num = 0; num < count; ++num) {
		int ret;
		enum ffs_os_desc_type type;
		u16 feature_count;
		struct usb_os_desc_header *desc = (void *)data;

		if (len < sizeof(*desc))
			return -EINVAL;

		/*
		 * Record "descriptor" entity.
		 * Process dwLength, bcdVersion, wIndex, get b/wCount.
		 * Move the data pointer to the beginning of extended
		 * compatibilities proper or extended properties proper
		 * portions of the data
		 */
		if (le32_to_cpu(desc->dwLength) > len)
			return -EINVAL;

		ret = __ffs_do_os_desc_header(&type, desc);
		if (unlikely(ret < 0)) {
			pr_debug("entity OS_DESCRIPTOR(%02lx); ret = %d\n",
				 num, ret);
			return ret;
		}
		/*
		 * 16-bit hex "?? 00" Little Endian looks like 8-bit hex "??"
		 */
		feature_count = le16_to_cpu(desc->wCount);
		if (type == FFS_OS_DESC_EXT_COMPAT &&
		    (feature_count > 255 || desc->Reserved))
				return -EINVAL;
		len -= ret;
		data += ret;

		/*
		 * Process all function/property descriptors
		 * of this Feature Descriptor
		 */
		ret = ffs_do_single_os_desc(data, len, type,
					    feature_count, entity, priv, desc);
		if (unlikely(ret < 0)) {
			pr_debug("%s returns %d\n", __func__, ret);
			return ret;
		}

		len -= ret;
		data += ret;
	}
	return _len - len;
}

/**
 * Validate contents of the buffer from userspace related to OS descriptors.
 */
static int __ffs_data_do_os_desc(enum ffs_os_desc_type type,
				 struct usb_os_desc_header *h, void *data,
				 unsigned len, void *priv)
{
	struct ffs_data *ffs = priv;
	u8 length;

	ENTER();

	switch (type) {
	case FFS_OS_DESC_EXT_COMPAT: {
		struct usb_ext_compat_desc *d = data;
		int i;

		if (len < sizeof(*d) ||
		    d->bFirstInterfaceNumber >= ffs->interfaces_count)
			return -EINVAL;
		if (d->Reserved1 != 1) {
			/*
			 * According to the spec, Reserved1 must be set to 1
			 * but older kernels incorrectly rejected non-zero
			 * values.  We fix it here to avoid returning EINVAL
			 * in response to values we used to accept.
			 */
			pr_debug("usb_ext_compat_desc::Reserved1 forced to 1\n");
			d->Reserved1 = 1;
		}
		for (i = 0; i < ARRAY_SIZE(d->Reserved2); ++i)
			if (d->Reserved2[i])
				return -EINVAL;

		length = sizeof(struct usb_ext_compat_desc);
	}
		break;
	case FFS_OS_DESC_EXT_PROP: {
		struct usb_ext_prop_desc *d = data;
		u32 type, pdl;
		u16 pnl;

		if (len < sizeof(*d) || h->interface >= ffs->interfaces_count)
			return -EINVAL;
		length = le32_to_cpu(d->dwSize);
		if (len < length)
			return -EINVAL;
		type = le32_to_cpu(d->dwPropertyDataType);
		if (type < USB_EXT_PROP_UNICODE ||
		    type > USB_EXT_PROP_UNICODE_MULTI) {
			pr_vdebug("unsupported os descriptor property type: %d",
				  type);
			return -EINVAL;
		}
		pnl = le16_to_cpu(d->wPropertyNameLength);
		if (length < 14 + pnl) {
			pr_vdebug("invalid os descriptor length: %d pnl:%d (descriptor %d)\n",
				  length, pnl, type);
			return -EINVAL;
		}
		pdl = le32_to_cpu(*(__le32 *)((u8 *)data + 10 + pnl));
		if (length != 14 + pnl + pdl) {
			pr_vdebug("invalid os descriptor length: %d pnl:%d pdl:%d (descriptor %d)\n",
				  length, pnl, pdl, type);
			return -EINVAL;
		}
		++ffs->ms_os_descs_ext_prop_count;
		/* property name reported to the host as "WCHAR"s */
		ffs->ms_os_descs_ext_prop_name_len += pnl * 2;
		ffs->ms_os_descs_ext_prop_data_len += pdl;
	}
		break;
	default:
		pr_vdebug("unknown descriptor: %d\n", type);
		return -EINVAL;
	}
	return length;
}

static int __ffs_data_got_descs(struct ffs_data *ffs,
				char *const _data, size_t len)
{
	char *data = _data, *raw_descs;
	unsigned os_descs_count = 0, counts[3], flags;
	int ret = -EINVAL, i;
	struct ffs_desc_helper helper;

	ENTER();

	if (get_unaligned_le32(data + 4) != len)
		goto error;

	switch (get_unaligned_le32(data)) {
	case FUNCTIONFS_DESCRIPTORS_MAGIC:
		flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC;
		data += 8;
		len  -= 8;
		break;
	case FUNCTIONFS_DESCRIPTORS_MAGIC_V2:
		flags = get_unaligned_le32(data + 8);
		ffs->user_flags = flags;
		if (flags & ~(FUNCTIONFS_HAS_FS_DESC |
			      FUNCTIONFS_HAS_HS_DESC |
			      FUNCTIONFS_HAS_SS_DESC |
			      FUNCTIONFS_HAS_MS_OS_DESC |
			      FUNCTIONFS_VIRTUAL_ADDR |
			      FUNCTIONFS_EVENTFD |
			      FUNCTIONFS_ALL_CTRL_RECIP |
			      FUNCTIONFS_CONFIG0_SETUP)) {
			ret = -ENOSYS;
			goto error;
		}
		data += 12;
		len  -= 12;
		break;
	default:
		goto error;
	}

	if (flags & FUNCTIONFS_EVENTFD) {
		if (len < 4)
			goto error;
		ffs->ffs_eventfd =
			eventfd_ctx_fdget((int)get_unaligned_le32(data));
		if (IS_ERR(ffs->ffs_eventfd)) {
			ret = PTR_ERR(ffs->ffs_eventfd);
			ffs->ffs_eventfd = NULL;
			goto error;
		}
		data += 4;
		len  -= 4;
	}

	/* Read fs_count, hs_count and ss_count (if present) */
	for (i = 0; i < 3; ++i) {
		if (!(flags & (1 << i))) {
			counts[i] = 0;
		} else if (len < 4) {
			goto error;
		} else {
			counts[i] = get_unaligned_le32(data);
			data += 4;
			len  -= 4;
		}
	}
	if (flags & (1 << i)) {
		if (len < 4) {
			goto error;
		}
		os_descs_count = get_unaligned_le32(data);
		data += 4;
		len -= 4;
	};

	/* Read descriptors */
	raw_descs = data;
	helper.ffs = ffs;
	for (i = 0; i < 3; ++i) {
		if (!counts[i])
			continue;
		helper.interfaces_count = 0;
		helper.eps_count = 0;
		ret = ffs_do_descs(counts[i], data, len,
				   __ffs_data_do_entity, &helper);
		if (ret < 0)
			goto error;
		if (!ffs->eps_count && !ffs->interfaces_count) {
			ffs->eps_count = helper.eps_count;
			ffs->interfaces_count = helper.interfaces_count;
		} else {
			if (ffs->eps_count != helper.eps_count) {
				ret = -EINVAL;
				goto error;
			}
			if (ffs->interfaces_count != helper.interfaces_count) {
				ret = -EINVAL;
				goto error;
			}
		}
		data += ret;
		len  -= ret;
	}
	if (os_descs_count) {
		ret = ffs_do_os_descs(os_descs_count, data, len,
				      __ffs_data_do_os_desc, ffs);
		if (ret < 0)
			goto error;
		data += ret;
		len -= ret;
	}

	if (raw_descs == data || len) {
		ret = -EINVAL;
		goto error;
	}

	ffs->raw_descs_data	= _data;
	ffs->raw_descs		= raw_descs;
	ffs->raw_descs_length	= data - raw_descs;
	ffs->fs_descs_count	= counts[0];
	ffs->hs_descs_count	= counts[1];
	ffs->ss_descs_count	= counts[2];
	ffs->ms_os_descs_count	= os_descs_count;

	return 0;

error:
	kfree(_data);
	return ret;
}

static int __ffs_data_got_strings(struct ffs_data *ffs,
				  char *const _data, size_t len)
{
	u32 str_count, needed_count, lang_count;
	struct usb_gadget_strings **stringtabs, *t;
	const char *data = _data;
	struct usb_string *s;

	ENTER();

	if (unlikely(len < 16 ||
		     get_unaligned_le32(data) != FUNCTIONFS_STRINGS_MAGIC ||
		     get_unaligned_le32(data + 4) != len))
		goto error;
	str_count  = get_unaligned_le32(data + 8);
	lang_count = get_unaligned_le32(data + 12);

	/* if one is zero the other must be zero */
	if (unlikely(!str_count != !lang_count))
		goto error;

	/* Do we have at least as many strings as descriptors need? */
	needed_count = ffs->strings_count;
	if (unlikely(str_count < needed_count))
		goto error;

	/*
	 * If we don't need any strings just return and free all
	 * memory.
	 */
	if (!needed_count) {
		kfree(_data);
		return 0;
	}

	/* Allocate everything in one chunk so there's less maintenance. */
	{
		unsigned i = 0;
		vla_group(d);
		vla_item(d, struct usb_gadget_strings *, stringtabs,
			lang_count + 1);
		vla_item(d, struct usb_gadget_strings, stringtab, lang_count);
		vla_item(d, struct usb_string, strings,
			lang_count*(needed_count+1));

		char *vlabuf = kmalloc(vla_group_size(d), GFP_KERNEL);

		if (unlikely(!vlabuf)) {
			kfree(_data);
			return -ENOMEM;
		}

		/* Initialize the VLA pointers */
		stringtabs = vla_ptr(vlabuf, d, stringtabs);
		t = vla_ptr(vlabuf, d, stringtab);
		i = lang_count;
		do {
			*stringtabs++ = t++;
		} while (--i);
		*stringtabs = NULL;

		/* stringtabs = vlabuf = d_stringtabs for later kfree */
		stringtabs = vla_ptr(vlabuf, d, stringtabs);
		t = vla_ptr(vlabuf, d, stringtab);
		s = vla_ptr(vlabuf, d, strings);
	}

	/* For each language */
	data += 16;
	len -= 16;

	do { /* lang_count > 0 so we can use do-while */
		unsigned needed = needed_count;

		if (unlikely(len < 3))
			goto error_free;
		t->language = get_unaligned_le16(data);
		t->strings  = s;
		++t;

		data += 2;
		len -= 2;

		/* For each string */
		do { /* str_count > 0 so we can use do-while */
			size_t length = strnlen(data, len);

			if (unlikely(length == len))
				goto error_free;

			/*
			 * User may provide more strings then we need,
			 * if that's the case we simply ignore the
			 * rest
			 */
			if (likely(needed)) {
				/*
				 * s->id will be set while adding
				 * function to configuration so for
				 * now just leave garbage here.
				 */
				s->s = data;
				--needed;
				++s;
			}

			data += length + 1;
			len -= length + 1;
		} while (--str_count);

		s->id = 0;   /* terminator */
		s->s = NULL;
		++s;

	} while (--lang_count);

	/* Some garbage left? */
	if (unlikely(len))
		goto error_free;

	/* Done! */
	ffs->stringtabs = stringtabs;
	ffs->raw_strings = _data;

	return 0;

error_free:
	kfree(stringtabs);
error:
	kfree(_data);
	return -EINVAL;
}


/* Events handling and management *******************************************/

static void __ffs_event_add(struct ffs_data *ffs,
			    enum usb_functionfs_event_type type)
{
	enum usb_functionfs_event_type rem_type1, rem_type2 = type;
	int neg = 0;

	/*
	 * Abort any unhandled setup
	 *
	 * We do not need to worry about some cmpxchg() changing value
	 * of ffs->setup_state without holding the lock because when
	 * state is FFS_SETUP_PENDING cmpxchg() in several places in
	 * the source does nothing.
	 */
	if (ffs->setup_state == FFS_SETUP_PENDING)
		ffs->setup_state = FFS_SETUP_CANCELLED;

	/*
	 * Logic of this function guarantees that there are at most four pending
	 * evens on ffs->ev.types queue.  This is important because the queue
	 * has space for four elements only and __ffs_ep0_read_events function
	 * depends on that limit as well.  If more event types are added, those
	 * limits have to be revisited or guaranteed to still hold.
	 */
	switch (type) {
	case FUNCTIONFS_RESUME:
		rem_type2 = FUNCTIONFS_SUSPEND;
		/* FALL THROUGH */
	case FUNCTIONFS_SUSPEND:
	case FUNCTIONFS_SETUP:
		rem_type1 = type;
		/* Discard all similar events */
		break;

	case FUNCTIONFS_BIND:
	case FUNCTIONFS_UNBIND:
	case FUNCTIONFS_DISABLE:
	case FUNCTIONFS_ENABLE:
		/* Discard everything other then power management. */
		rem_type1 = FUNCTIONFS_SUSPEND;
		rem_type2 = FUNCTIONFS_RESUME;
		neg = 1;
		break;

	default:
		WARN(1, "%d: unknown event, this should not happen\n", type);
		return;
	}

	{
		u8 *ev  = ffs->ev.types, *out = ev;
		unsigned n = ffs->ev.count;
		for (; n; --n, ++ev)
			if ((*ev == rem_type1 || *ev == rem_type2) == neg)
				*out++ = *ev;
			else
				pr_vdebug("purging event %d\n", *ev);
		ffs->ev.count = out - ffs->ev.types;
	}

	pr_vdebug("adding event %d\n", type);
	ffs->ev.types[ffs->ev.count++] = type;
	wake_up_locked(&ffs->ev.waitq);
	if (ffs->ffs_eventfd)
		eventfd_signal(ffs->ffs_eventfd, 1);
}

static void ffs_event_add(struct ffs_data *ffs,
			  enum usb_functionfs_event_type type)
{
	unsigned long flags;
	spin_lock_irqsave(&ffs->ev.waitq.lock, flags);
	__ffs_event_add(ffs, type);
	spin_unlock_irqrestore(&ffs->ev.waitq.lock, flags);
}

/* Bind/unbind USB function hooks *******************************************/

static int ffs_ep_addr2idx(struct ffs_data *ffs, u8 endpoint_address)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(ffs->eps_addrmap); ++i)
		if (ffs->eps_addrmap[i] == endpoint_address)
			return i;
	return -ENOENT;
}

static int __ffs_func_bind_do_descs(enum ffs_entity_type type, u8 *valuep,
				    struct usb_descriptor_header *desc,
				    void *priv)
{
	struct usb_endpoint_descriptor *ds = (void *)desc;
	struct ffs_function *func = priv;
	struct ffs_ep *ffs_ep;
	unsigned ep_desc_id;
	int idx;
	static const char *speed_names[] = { "full", "high", "super" };

	if (type != FFS_DESCRIPTOR)
		return 0;

	/*
	 * If ss_descriptors is not NULL, we are reading super speed
	 * descriptors; if hs_descriptors is not NULL, we are reading high
	 * speed descriptors; otherwise, we are reading full speed
	 * descriptors.
	 */
	if (func->function.ss_descriptors) {
		ep_desc_id = 2;
		func->function.ss_descriptors[(long)valuep] = desc;
	} else if (func->function.hs_descriptors) {
		ep_desc_id = 1;
		func->function.hs_descriptors[(long)valuep] = desc;
	} else {
		ep_desc_id = 0;
		func->function.fs_descriptors[(long)valuep]    = desc;
	}

	if (!desc || desc->bDescriptorType != USB_DT_ENDPOINT)
		return 0;

	idx = ffs_ep_addr2idx(func->ffs, ds->bEndpointAddress) - 1;
	if (idx < 0)
		return idx;

	ffs_ep = func->eps + idx;

	if (unlikely(ffs_ep->descs[ep_desc_id])) {
		pr_err("two %sspeed descriptors for EP %d\n",
			  speed_names[ep_desc_id],
			  ds->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
		return -EINVAL;
	}
	ffs_ep->descs[ep_desc_id] = ds;

	ffs_dump_mem(": Original  ep desc", ds, ds->bLength);
	if (ffs_ep->ep) {
		ds->bEndpointAddress = ffs_ep->descs[0]->bEndpointAddress;
		if (!ds->wMaxPacketSize)
			ds->wMaxPacketSize = ffs_ep->descs[0]->wMaxPacketSize;
	} else {
		struct usb_request *req;
		struct usb_ep *ep;
		u8 bEndpointAddress;

		/*
		 * We back up bEndpointAddress because autoconfig overwrites
		 * it with physical endpoint address.
		 */
		bEndpointAddress = ds->bEndpointAddress;
		pr_vdebug("autoconfig\n");
		ep = usb_ep_autoconfig(func->gadget, ds);
		if (unlikely(!ep))
			return -ENOTSUPP;
		ep->driver_data = func->eps + idx;

		req = usb_ep_alloc_request(ep, GFP_KERNEL);
		if (unlikely(!req))
			return -ENOMEM;

		ffs_ep->ep  = ep;
		ffs_ep->req = req;
		func->eps_revmap[ds->bEndpointAddress &
				 USB_ENDPOINT_NUMBER_MASK] = idx + 1;
		/*
		 * If we use virtual address mapping, we restore
		 * original bEndpointAddress value.
		 */
		if (func->ffs->user_flags & FUNCTIONFS_VIRTUAL_ADDR)
			ds->bEndpointAddress = bEndpointAddress;
	}
	ffs_dump_mem(": Rewritten ep desc", ds, ds->bLength);

	return 0;
}

static int __ffs_func_bind_do_nums(enum ffs_entity_type type, u8 *valuep,
				   struct usb_descriptor_header *desc,
				   void *priv)
{
	struct ffs_function *func = priv;
	unsigned idx;
	u8 newValue;

	switch (type) {
	default:
	case FFS_DESCRIPTOR:
		/* Handled in previous pass by __ffs_func_bind_do_descs() */
		return 0;

	case FFS_INTERFACE:
		idx = *valuep;
		if (func->interfaces_nums[idx] < 0) {
			int id = usb_interface_id(func->conf, &func->function);
			if (unlikely(id < 0))
				return id;
			func->interfaces_nums[idx] = id;
		}
		newValue = func->interfaces_nums[idx];
		break;

	case FFS_STRING:
		/* String' IDs are allocated when fsf_data is bound to cdev */
		newValue = func->ffs->stringtabs[0]->strings[*valuep - 1].id;
		break;

	case FFS_ENDPOINT:
		/*
		 * USB_DT_ENDPOINT are handled in
		 * __ffs_func_bind_do_descs().
		 */
		if (desc->bDescriptorType == USB_DT_ENDPOINT)
			return 0;

		idx = (*valuep & USB_ENDPOINT_NUMBER_MASK) - 1;
		if (unlikely(!func->eps[idx].ep))
			return -EINVAL;

		{
			struct usb_endpoint_descriptor **descs;
			descs = func->eps[idx].descs;
			newValue = descs[descs[0] ? 0 : 1]->bEndpointAddress;
		}
		break;
	}

	pr_vdebug("%02x -> %02x\n", *valuep, newValue);
	*valuep = newValue;
	return 0;
}

static int __ffs_func_bind_do_os_desc(enum ffs_os_desc_type type,
				      struct usb_os_desc_header *h, void *data,
				      unsigned len, void *priv)
{
	struct ffs_function *func = priv;
	u8 length = 0;

	switch (type) {
	case FFS_OS_DESC_EXT_COMPAT: {
		struct usb_ext_compat_desc *desc = data;
		struct usb_os_desc_table *t;

		t = &func->function.os_desc_table[desc->bFirstInterfaceNumber];
		t->if_id = func->interfaces_nums[desc->bFirstInterfaceNumber];
		memcpy(t->os_desc->ext_compat_id, &desc->CompatibleID,
		       ARRAY_SIZE(desc->CompatibleID) +
		       ARRAY_SIZE(desc->SubCompatibleID));
		length = sizeof(*desc);
	}
		break;
	case FFS_OS_DESC_EXT_PROP: {
		struct usb_ext_prop_desc *desc = data;
		struct usb_os_desc_table *t;
		struct usb_os_desc_ext_prop *ext_prop;
		char *ext_prop_name;
		char *ext_prop_data;

		t = &func->function.os_desc_table[h->interface];
		t->if_id = func->interfaces_nums[h->interface];

		ext_prop = func->ffs->ms_os_descs_ext_prop_avail;
		func->ffs->ms_os_descs_ext_prop_avail += sizeof(*ext_prop);

		ext_prop->type = le32_to_cpu(desc->dwPropertyDataType);
		ext_prop->name_len = le16_to_cpu(desc->wPropertyNameLength);
		ext_prop->data_len = le32_to_cpu(*(__le32 *)
			usb_ext_prop_data_len_ptr(data, ext_prop->name_len));
		length = ext_prop->name_len + ext_prop->data_len + 14;

		ext_prop_name = func->ffs->ms_os_descs_ext_prop_name_avail;
		func->ffs->ms_os_descs_ext_prop_name_avail +=
			ext_prop->name_len;

		ext_prop_data = func->ffs->ms_os_descs_ext_prop_data_avail;
		func->ffs->ms_os_descs_ext_prop_data_avail +=
			ext_prop->data_len;
		memcpy(ext_prop_data,
		       usb_ext_prop_data_ptr(data, ext_prop->name_len),
		       ext_prop->data_len);
		/* unicode data reported to the host as "WCHAR"s */
		switch (ext_prop->type) {
		case USB_EXT_PROP_UNICODE:
		case USB_EXT_PROP_UNICODE_ENV:
		case USB_EXT_PROP_UNICODE_LINK:
		case USB_EXT_PROP_UNICODE_MULTI:
			ext_prop->data_len *= 2;
			break;
		}
		ext_prop->data = ext_prop_data;

		memcpy(ext_prop_name, usb_ext_prop_name_ptr(data),
		       ext_prop->name_len);
		/* property name reported to the host as "WCHAR"s */
		ext_prop->name_len *= 2;
		ext_prop->name = ext_prop_name;

		t->os_desc->ext_prop_len +=
			ext_prop->name_len + ext_prop->data_len + 14;
		++t->os_desc->ext_prop_count;
		list_add_tail(&ext_prop->entry, &t->os_desc->ext_prop);
	}
		break;
	default:
		pr_vdebug("unknown descriptor: %d\n", type);
	}

	return length;
}

static inline struct f_fs_opts *ffs_do_functionfs_bind(struct usb_function *f,
						struct usb_configuration *c)
{
	struct ffs_function *func = ffs_func_from_usb(f);
	struct f_fs_opts *ffs_opts =
		container_of(f->fi, struct f_fs_opts, func_inst);
	int ret;

	ENTER();

	/*
	 * Legacy gadget triggers binding in functionfs_ready_callback,
	 * which already uses locking; taking the same lock here would
	 * cause a deadlock.
	 *
	 * Configfs-enabled gadgets however do need ffs_dev_lock.
	 */
	if (!ffs_opts->no_configfs)
		ffs_dev_lock();
	ret = ffs_opts->dev->desc_ready ? 0 : -ENODEV;
	func->ffs = ffs_opts->dev->ffs_data;
	if (!ffs_opts->no_configfs)
		ffs_dev_unlock();
	if (ret)
		return ERR_PTR(ret);

	func->conf = c;
	func->gadget = c->cdev->gadget;

	/*
	 * in drivers/usb/gadget/configfs.c:configfs_composite_bind()
	 * configurations are bound in sequence with list_for_each_entry,
	 * in each configuration its functions are bound in sequence
	 * with list_for_each_entry, so we assume no race condition
	 * with regard to ffs_opts->bound access
	 */
	if (!ffs_opts->refcnt) {
		ret = functionfs_bind(func->ffs, c->cdev);
		if (ret)
			return ERR_PTR(ret);
	}
	ffs_opts->refcnt++;
	func->function.strings = func->ffs->stringtabs;

	return ffs_opts;
}

static int _ffs_func_bind(struct usb_configuration *c,
			  struct usb_function *f)
{
	struct ffs_function *func = ffs_func_from_usb(f);
	struct ffs_data *ffs = func->ffs;

	const int full = !!func->ffs->fs_descs_count;
	const int high = !!func->ffs->hs_descs_count;
	const int super = !!func->ffs->ss_descs_count;

	int fs_len, hs_len, ss_len, ret, i;
	struct ffs_ep *eps_ptr;

	/* Make it a single chunk, less management later on */
	vla_group(d);
	vla_item_with_sz(d, struct ffs_ep, eps, ffs->eps_count);
	vla_item_with_sz(d, struct usb_descriptor_header *, fs_descs,
		full ? ffs->fs_descs_count + 1 : 0);
	vla_item_with_sz(d, struct usb_descriptor_header *, hs_descs,
		high ? ffs->hs_descs_count + 1 : 0);
	vla_item_with_sz(d, struct usb_descriptor_header *, ss_descs,
		super ? ffs->ss_descs_count + 1 : 0);
	vla_item_with_sz(d, short, inums, ffs->interfaces_count);
	vla_item_with_sz(d, struct usb_os_desc_table, os_desc_table,
			 c->cdev->use_os_string ? ffs->interfaces_count : 0);
	vla_item_with_sz(d, char[16], ext_compat,
			 c->cdev->use_os_string ? ffs->interfaces_count : 0);
	vla_item_with_sz(d, struct usb_os_desc, os_desc,
			 c->cdev->use_os_string ? ffs->interfaces_count : 0);
	vla_item_with_sz(d, struct usb_os_desc_ext_prop, ext_prop,
			 ffs->ms_os_descs_ext_prop_count);
	vla_item_with_sz(d, char, ext_prop_name,
			 ffs->ms_os_descs_ext_prop_name_len);
	vla_item_with_sz(d, char, ext_prop_data,
			 ffs->ms_os_descs_ext_prop_data_len);
	vla_item_with_sz(d, char, raw_descs, ffs->raw_descs_length);
	char *vlabuf;

	ENTER();

	/* Has descriptors only for speeds gadget does not support */
	if (unlikely(!(full | high | super)))
		return -ENOTSUPP;

	/* Allocate a single chunk, less management later on */
	vlabuf = kzalloc(vla_group_size(d), GFP_KERNEL);
	if (unlikely(!vlabuf))
		return -ENOMEM;

	ffs->ms_os_descs_ext_prop_avail = vla_ptr(vlabuf, d, ext_prop);
	ffs->ms_os_descs_ext_prop_name_avail =
		vla_ptr(vlabuf, d, ext_prop_name);
	ffs->ms_os_descs_ext_prop_data_avail =
		vla_ptr(vlabuf, d, ext_prop_data);

	/* Copy descriptors  */
	memcpy(vla_ptr(vlabuf, d, raw_descs), ffs->raw_descs,
	       ffs->raw_descs_length);

	memset(vla_ptr(vlabuf, d, inums), 0xff, d_inums__sz);
	eps_ptr = vla_ptr(vlabuf, d, eps);
	for (i = 0; i < ffs->eps_count; i++)
		eps_ptr[i].num = -1;

	/* Save pointers
	 * d_eps == vlabuf, func->eps used to kfree vlabuf later
	*/
	func->eps             = vla_ptr(vlabuf, d, eps);
	func->interfaces_nums = vla_ptr(vlabuf, d, inums);

	/*
	 * Go through all the endpoint descriptors and allocate
	 * endpoints first, so that later we can rewrite the endpoint
	 * numbers without worrying that it may be described later on.
	 */
	if (likely(full)) {
		func->function.fs_descriptors = vla_ptr(vlabuf, d, fs_descs);
		fs_len = ffs_do_descs(ffs->fs_descs_count,
				      vla_ptr(vlabuf, d, raw_descs),
				      d_raw_descs__sz,
				      __ffs_func_bind_do_descs, func);
		if (unlikely(fs_len < 0)) {
			ret = fs_len;
			goto error;
		}
	} else {
		fs_len = 0;
	}

	if (likely(high)) {
		func->function.hs_descriptors = vla_ptr(vlabuf, d, hs_descs);
		hs_len = ffs_do_descs(ffs->hs_descs_count,
				      vla_ptr(vlabuf, d, raw_descs) + fs_len,
				      d_raw_descs__sz - fs_len,
				      __ffs_func_bind_do_descs, func);
		if (unlikely(hs_len < 0)) {
			ret = hs_len;
			goto error;
		}
	} else {
		hs_len = 0;
	}

	if (likely(super)) {
		func->function.ss_descriptors = vla_ptr(vlabuf, d, ss_descs);
		ss_len = ffs_do_descs(ffs->ss_descs_count,
				vla_ptr(vlabuf, d, raw_descs) + fs_len + hs_len,
				d_raw_descs__sz - fs_len - hs_len,
				__ffs_func_bind_do_descs, func);
		if (unlikely(ss_len < 0)) {
			ret = ss_len;
			goto error;
		}
	} else {
		ss_len = 0;
	}

	/*
	 * Now handle interface numbers allocation and interface and
	 * endpoint numbers rewriting.  We can do that in one go
	 * now.
	 */
	ret = ffs_do_descs(ffs->fs_descs_count +
			   (high ? ffs->hs_descs_count : 0) +
			   (super ? ffs->ss_descs_count : 0),
			   vla_ptr(vlabuf, d, raw_descs), d_raw_descs__sz,
			   __ffs_func_bind_do_nums, func);
	if (unlikely(ret < 0))
		goto error;

	func->function.os_desc_table = vla_ptr(vlabuf, d, os_desc_table);
	if (c->cdev->use_os_string) {
		for (i = 0; i < ffs->interfaces_count; ++i) {
			struct usb_os_desc *desc;

			desc = func->function.os_desc_table[i].os_desc =
				vla_ptr(vlabuf, d, os_desc) +
				i * sizeof(struct usb_os_desc);
			desc->ext_compat_id =
				vla_ptr(vlabuf, d, ext_compat) + i * 16;
			INIT_LIST_HEAD(&desc->ext_prop);
		}
		ret = ffs_do_os_descs(ffs->ms_os_descs_count,
				      vla_ptr(vlabuf, d, raw_descs) +
				      fs_len + hs_len + ss_len,
				      d_raw_descs__sz - fs_len - hs_len -
				      ss_len,
				      __ffs_func_bind_do_os_desc, func);
		if (unlikely(ret < 0))
			goto error;
	}
	func->function.os_desc_n =
		c->cdev->use_os_string ? ffs->interfaces_count : 0;

	/* And we're done */
	ffs_event_add(ffs, FUNCTIONFS_BIND);
	return 0;

error:
	/* XXX Do we need to release all claimed endpoints here? */
	return ret;
}

static int ffs_func_bind(struct usb_configuration *c,
			 struct usb_function *f)
{
	struct f_fs_opts *ffs_opts = ffs_do_functionfs_bind(f, c);
	struct ffs_function *func = ffs_func_from_usb(f);
	int ret;

	if (IS_ERR(ffs_opts))
		return PTR_ERR(ffs_opts);

	ret = _ffs_func_bind(c, f);
	if (ret && !--ffs_opts->refcnt)
		functionfs_unbind(func->ffs);

	return ret;
}


/* Other USB function hooks *************************************************/

static void ffs_reset_work(struct work_struct *work)
{
	struct ffs_data *ffs = container_of(work,
		struct ffs_data, reset_work);
	ffs_data_reset(ffs);
}

static int ffs_func_set_alt(struct usb_function *f,
			    unsigned interface, unsigned alt)
{
	struct ffs_function *func = ffs_func_from_usb(f);
	struct ffs_data *ffs = func->ffs;
	int ret = 0, intf;

	if (alt != (unsigned)-1) {
		intf = ffs_func_revmap_intf(func, interface);
		if (unlikely(intf < 0))
			return intf;
	}

	if (ffs->func)
		ffs_func_eps_disable(ffs->func);

	if (ffs->state == FFS_DEACTIVATED) {
		ffs->state = FFS_CLOSING;
		INIT_WORK(&ffs->reset_work, ffs_reset_work);
		schedule_work(&ffs->reset_work);
		return -ENODEV;
	}

	if (ffs->state != FFS_ACTIVE)
		return -ENODEV;

	if (alt == (unsigned)-1) {
		ffs->func = NULL;
		ffs_event_add(ffs, FUNCTIONFS_DISABLE);
		return 0;
	}

	ffs->func = func;
	ret = ffs_func_eps_enable(func);
	if (likely(ret >= 0))
		ffs_event_add(ffs, FUNCTIONFS_ENABLE);
	return ret;
}

static void ffs_func_disable(struct usb_function *f)
{
	ffs_func_set_alt(f, 0, (unsigned)-1);
}

static int ffs_func_setup(struct usb_function *f,
			  const struct usb_ctrlrequest *creq)
{
	struct ffs_function *func = ffs_func_from_usb(f);
	struct ffs_data *ffs = func->ffs;
	unsigned long flags;
	int ret;

	ENTER();

	pr_vdebug("creq->bRequestType = %02x\n", creq->bRequestType);
	pr_vdebug("creq->bRequest     = %02x\n", creq->bRequest);
	pr_vdebug("creq->wValue       = %04x\n", le16_to_cpu(creq->wValue));
	pr_vdebug("creq->wIndex       = %04x\n", le16_to_cpu(creq->wIndex));
	pr_vdebug("creq->wLength      = %04x\n", le16_to_cpu(creq->wLength));

	/*
	 * Most requests directed to interface go through here
	 * (notable exceptions are set/get interface) so we need to
	 * handle them.  All other either handled by composite or
	 * passed to usb_configuration->setup() (if one is set).  No
	 * matter, we will handle requests directed to endpoint here
	 * as well (as it's straightforward).  Other request recipient
	 * types are only handled when the user flag FUNCTIONFS_ALL_CTRL_RECIP
	 * is being used.
	 */
	if (ffs->state != FFS_ACTIVE)
		return -ENODEV;

	switch (creq->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		ret = ffs_func_revmap_intf(func, le16_to_cpu(creq->wIndex));
		if (unlikely(ret < 0))
			return ret;
		break;

	case USB_RECIP_ENDPOINT:
		ret = ffs_func_revmap_ep(func, le16_to_cpu(creq->wIndex));
		if (unlikely(ret < 0))
			return ret;
		if (func->ffs->user_flags & FUNCTIONFS_VIRTUAL_ADDR)
			ret = func->ffs->eps_addrmap[ret];
		break;

	default:
		if (func->ffs->user_flags & FUNCTIONFS_ALL_CTRL_RECIP)
			ret = le16_to_cpu(creq->wIndex);
		else
			return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&ffs->ev.waitq.lock, flags);
	ffs->ev.setup = *creq;
	ffs->ev.setup.wIndex = cpu_to_le16(ret);
	__ffs_event_add(ffs, FUNCTIONFS_SETUP);
	spin_unlock_irqrestore(&ffs->ev.waitq.lock, flags);

	return USB_GADGET_DELAYED_STATUS;
}

static bool ffs_func_req_match(struct usb_function *f,
			       const struct usb_ctrlrequest *creq,
			       bool config0)
{
	struct ffs_function *func = ffs_func_from_usb(f);

	if (config0 && !(func->ffs->user_flags & FUNCTIONFS_CONFIG0_SETUP))
		return false;

	switch (creq->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		return (ffs_func_revmap_intf(func,
					     le16_to_cpu(creq->wIndex)) >= 0);
	case USB_RECIP_ENDPOINT:
		return (ffs_func_revmap_ep(func,
					   le16_to_cpu(creq->wIndex)) >= 0);
	default:
		return (bool) (func->ffs->user_flags &
			       FUNCTIONFS_ALL_CTRL_RECIP);
	}
}

static void ffs_func_suspend(struct usb_function *f)
{
	ENTER();
	ffs_event_add(ffs_func_from_usb(f)->ffs, FUNCTIONFS_SUSPEND);
}

static void ffs_func_resume(struct usb_function *f)
{
	ENTER();
	ffs_event_add(ffs_func_from_usb(f)->ffs, FUNCTIONFS_RESUME);
}


/* Endpoint and interface numbers reverse mapping ***************************/

static int ffs_func_revmap_ep(struct ffs_function *func, u8 num)
{
	num = func->eps_revmap[num & USB_ENDPOINT_NUMBER_MASK];
	return num ? num : -EDOM;
}

static int ffs_func_revmap_intf(struct ffs_function *func, u8 intf)
{
	short *nums = func->interfaces_nums;
	unsigned count = func->ffs->interfaces_count;

	for (; count; --count, ++nums) {
		if (*nums >= 0 && *nums == intf)
			return nums - func->interfaces_nums;
	}

	return -EDOM;
}


/* Devices management *******************************************************/

static LIST_HEAD(ffs_devices);

static struct ffs_dev *_ffs_do_find_dev(const char *name)
{
	struct ffs_dev *dev;

	if (!name)
		return NULL;

	list_for_each_entry(dev, &ffs_devices, entry) {
		if (strcmp(dev->name, name) == 0)
			return dev;
	}

	return NULL;
}

/*
 * ffs_lock must be taken by the caller of this function
 */
static struct ffs_dev *_ffs_get_single_dev(void)
{
	struct ffs_dev *dev;

	if (list_is_singular(&ffs_devices)) {
		dev = list_first_entry(&ffs_devices, struct ffs_dev, entry);
		if (dev->single)
			return dev;
	}

	return NULL;
}

/*
 * ffs_lock must be taken by the caller of this function
 */
static struct ffs_dev *_ffs_find_dev(const char *name)
{
	struct ffs_dev *dev;

	dev = _ffs_get_single_dev();
	if (dev)
		return dev;

	return _ffs_do_find_dev(name);
}

/* Configfs support *********************************************************/

static inline struct f_fs_opts *to_ffs_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_fs_opts,
			    func_inst.group);
}

static void ffs_attr_release(struct config_item *item)
{
	struct f_fs_opts *opts = to_ffs_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations ffs_item_ops = {
	.release	= ffs_attr_release,
};

static const struct config_item_type ffs_func_type = {
	.ct_item_ops	= &ffs_item_ops,
	.ct_owner	= THIS_MODULE,
};


/* Function registration interface ******************************************/

static void ffs_free_inst(struct usb_function_instance *f)
{
	struct f_fs_opts *opts;

	opts = to_f_fs_opts(f);
	ffs_dev_lock();
	_ffs_free_dev(opts->dev);
	ffs_dev_unlock();
	kfree(opts);
}

static int ffs_set_inst_name(struct usb_function_instance *fi, const char *name)
{
	if (strlen(name) >= FIELD_SIZEOF(struct ffs_dev, name))
		return -ENAMETOOLONG;
	return ffs_name_dev(to_f_fs_opts(fi)->dev, name);
}

static struct usb_function_instance *ffs_alloc_inst(void)
{
	struct f_fs_opts *opts;
	struct ffs_dev *dev;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.set_inst_name = ffs_set_inst_name;
	opts->func_inst.free_func_inst = ffs_free_inst;
	ffs_dev_lock();
	dev = _ffs_alloc_dev();
	ffs_dev_unlock();
	if (IS_ERR(dev)) {
		kfree(opts);
		return ERR_CAST(dev);
	}
	opts->dev = dev;
	dev->opts = opts;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &ffs_func_type);
	return &opts->func_inst;
}

static void ffs_free(struct usb_function *f)
{
	kfree(ffs_func_from_usb(f));
}

static void ffs_func_unbind(struct usb_configuration *c,
			    struct usb_function *f)
{
	struct ffs_function *func = ffs_func_from_usb(f);
	struct ffs_data *ffs = func->ffs;
	struct f_fs_opts *opts =
		container_of(f->fi, struct f_fs_opts, func_inst);
	struct ffs_ep *ep = func->eps;
	unsigned count = ffs->eps_count;
	unsigned long flags;

	ENTER();
	if (ffs->func == func) {
		ffs_func_eps_disable(func);
		ffs->func = NULL;
	}

	if (!--opts->refcnt)
		functionfs_unbind(ffs);

	/* cleanup after autoconfig */
	spin_lock_irqsave(&func->ffs->eps_lock, flags);
	while (count--) {
		if (ep->ep && ep->req)
			usb_ep_free_request(ep->ep, ep->req);
		ep->req = NULL;
		++ep;
	}
	spin_unlock_irqrestore(&func->ffs->eps_lock, flags);
	kfree(func->eps);
	func->eps = NULL;
	/*
	 * eps, descriptors and interfaces_nums are allocated in the
	 * same chunk so only one free is required.
	 */
	func->function.fs_descriptors = NULL;
	func->function.hs_descriptors = NULL;
	func->function.ss_descriptors = NULL;
	func->interfaces_nums = NULL;

	ffs_event_add(ffs, FUNCTIONFS_UNBIND);
}

static struct usb_function *ffs_alloc(struct usb_function_instance *fi)
{
	struct ffs_function *func;

	ENTER();

	func = kzalloc(sizeof(*func), GFP_KERNEL);
	if (unlikely(!func))
		return ERR_PTR(-ENOMEM);

	func->function.name    = "Function FS Gadget";

	func->function.bind    = ffs_func_bind;
	func->function.unbind  = ffs_func_unbind;
	func->function.set_alt = ffs_func_set_alt;
	func->function.disable = ffs_func_disable;
	func->function.setup   = ffs_func_setup;
	func->function.req_match = ffs_func_req_match;
	func->function.suspend = ffs_func_suspend;
	func->function.resume  = ffs_func_resume;
	func->function.free_func = ffs_free;

	return &func->function;
}

/*
 * ffs_lock must be taken by the caller of this function
 */
static struct ffs_dev *_ffs_alloc_dev(void)
{
	struct ffs_dev *dev;
	int ret;

	if (_ffs_get_single_dev())
			return ERR_PTR(-EBUSY);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (list_empty(&ffs_devices)) {
		ret = functionfs_init();
		if (ret) {
			kfree(dev);
			return ERR_PTR(ret);
		}
	}

	list_add(&dev->entry, &ffs_devices);

	return dev;
}

int ffs_name_dev(struct ffs_dev *dev, const char *name)
{
	struct ffs_dev *existing;
	int ret = 0;

	ffs_dev_lock();

	existing = _ffs_do_find_dev(name);
	if (!existing)
		strlcpy(dev->name, name, ARRAY_SIZE(dev->name));
	else if (existing != dev)
		ret = -EBUSY;

	ffs_dev_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(ffs_name_dev);

int ffs_single_dev(struct ffs_dev *dev)
{
	int ret;

	ret = 0;
	ffs_dev_lock();

	if (!list_is_singular(&ffs_devices))
		ret = -EBUSY;
	else
		dev->single = true;

	ffs_dev_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(ffs_single_dev);

/*
 * ffs_lock must be taken by the caller of this function
 */
static void _ffs_free_dev(struct ffs_dev *dev)
{
	list_del(&dev->entry);

	/* Clear the private_data pointer to stop incorrect dev access */
	if (dev->ffs_data)
		dev->ffs_data->private_data = NULL;

	kfree(dev);
	if (list_empty(&ffs_devices))
		functionfs_cleanup();
}

static void *ffs_acquire_dev(const char *dev_name)
{
	struct ffs_dev *ffs_dev;

	ENTER();
	ffs_dev_lock();

	ffs_dev = _ffs_find_dev(dev_name);
	if (!ffs_dev)
		ffs_dev = ERR_PTR(-ENOENT);
	else if (ffs_dev->mounted)
		ffs_dev = ERR_PTR(-EBUSY);
	else if (ffs_dev->ffs_acquire_dev_callback &&
	    ffs_dev->ffs_acquire_dev_callback(ffs_dev))
		ffs_dev = ERR_PTR(-ENOENT);
	else
		ffs_dev->mounted = true;

	ffs_dev_unlock();
	return ffs_dev;
}

static void ffs_release_dev(struct ffs_data *ffs_data)
{
	struct ffs_dev *ffs_dev;

	ENTER();
	ffs_dev_lock();

	ffs_dev = ffs_data->private_data;
	if (ffs_dev) {
		ffs_dev->mounted = false;

		if (ffs_dev->ffs_release_dev_callback)
			ffs_dev->ffs_release_dev_callback(ffs_dev);
	}

	ffs_dev_unlock();
}

static int ffs_ready(struct ffs_data *ffs)
{
	struct ffs_dev *ffs_obj;
	int ret = 0;

	ENTER();
	ffs_dev_lock();

	ffs_obj = ffs->private_data;
	if (!ffs_obj) {
		ret = -EINVAL;
		goto done;
	}
	if (WARN_ON(ffs_obj->desc_ready)) {
		ret = -EBUSY;
		goto done;
	}

	ffs_obj->desc_ready = true;
	ffs_obj->ffs_data = ffs;

	if (ffs_obj->ffs_ready_callback) {
		ret = ffs_obj->ffs_ready_callback(ffs);
		if (ret)
			goto done;
	}

	set_bit(FFS_FL_CALL_CLOSED_CALLBACK, &ffs->flags);
done:
	ffs_dev_unlock();
	return ret;
}

static void ffs_closed(struct ffs_data *ffs)
{
	struct ffs_dev *ffs_obj;
	struct f_fs_opts *opts;
	struct config_item *ci;

	ENTER();
	ffs_dev_lock();

	ffs_obj = ffs->private_data;
	if (!ffs_obj)
		goto done;

	ffs_obj->desc_ready = false;
	ffs_obj->ffs_data = NULL;

	if (test_and_clear_bit(FFS_FL_CALL_CLOSED_CALLBACK, &ffs->flags) &&
	    ffs_obj->ffs_closed_callback)
		ffs_obj->ffs_closed_callback(ffs);

	if (ffs_obj->opts)
		opts = ffs_obj->opts;
	else
		goto done;

	if (opts->no_configfs || !opts->func_inst.group.cg_item.ci_parent
	    || !kref_read(&opts->func_inst.group.cg_item.ci_kref))
		goto done;

	ci = opts->func_inst.group.cg_item.ci_parent->ci_parent;
	ffs_dev_unlock();

	if (test_bit(FFS_FL_BOUND, &ffs->flags))
		unregister_gadget_item(ci);
	return;
done:
	ffs_dev_unlock();
}

/* Misc helper functions ****************************************************/

static int ffs_mutex_lock(struct mutex *mutex, unsigned nonblock)
{
	return nonblock
		? likely(mutex_trylock(mutex)) ? 0 : -EAGAIN
		: mutex_lock_interruptible(mutex);
}

static char *ffs_prepare_buffer(const char __user *buf, size_t len)
{
	char *data;

	if (unlikely(!len))
		return NULL;

	data = kmalloc(len, GFP_KERNEL);
	if (unlikely(!data))
		return ERR_PTR(-ENOMEM);

	if (unlikely(copy_from_user(data, buf, len))) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}

	pr_vdebug("Buffer from user space:\n");
	ffs_dump_mem("", data, len);

	return data;
}

DECLARE_USB_FUNCTION_INIT(ffs, ffs_alloc_inst, ffs_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Nazarewicz");
