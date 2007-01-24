/*
 * bsg.c - block layer implementation of the sg v3 interface
 *
 * Copyright (C) 2004 Jens Axboe <axboe@suse.de> SUSE Labs
 * Copyright (C) 2004 Peter M. Jones <pjones@redhat.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License version 2.  See the file "COPYING" in the main directory of this
 *  archive for more details.
 *
 */
/*
 * TODO
 *	- Should this get merged, block/scsi_ioctl.c will be migrated into
 *	  this file. To keep maintenance down, it's easier to have them
 *	  seperated right now.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/percpu.h>
#include <linux/uio.h>
#include <linux/bsg.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/sg.h>

static char bsg_version[] = "block layer sg (bsg) 0.4";

struct bsg_device {
	struct gendisk *disk;
	request_queue_t *queue;
	spinlock_t lock;
	struct list_head busy_list;
	struct list_head done_list;
	struct hlist_node dev_list;
	atomic_t ref_count;
	int minor;
	int queued_cmds;
	int done_cmds;
	wait_queue_head_t wq_done;
	wait_queue_head_t wq_free;
	char name[BDEVNAME_SIZE];
	int max_queue;
	unsigned long flags;
};

enum {
	BSG_F_BLOCK		= 1,
	BSG_F_WRITE_PERM	= 2,
};

#define BSG_DEFAULT_CMDS	64

#undef BSG_DEBUG

#ifdef BSG_DEBUG
#define dprintk(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ##args)
#else
#define dprintk(fmt, args...)
#endif

#define list_entry_bc(entry)	list_entry((entry), struct bsg_command, list)

/*
 * just for testing
 */
#define BSG_MAJOR	(240)

static DEFINE_MUTEX(bsg_mutex);
static int bsg_device_nr;

#define BSG_LIST_SIZE	(8)
#define bsg_list_idx(minor)	((minor) & (BSG_LIST_SIZE - 1))
static struct hlist_head bsg_device_list[BSG_LIST_SIZE];

static struct class *bsg_class;
static LIST_HEAD(bsg_class_list);

static struct kmem_cache *bsg_cmd_cachep;

/*
 * our internal command type
 */
struct bsg_command {
	struct bsg_device *bd;
	struct list_head list;
	struct request *rq;
	struct bio *bio;
	int err;
	struct sg_io_v4 hdr;
	struct sg_io_v4 __user *uhdr;
	char sense[SCSI_SENSE_BUFFERSIZE];
};

static void bsg_free_command(struct bsg_command *bc)
{
	struct bsg_device *bd = bc->bd;
	unsigned long flags;

	kmem_cache_free(bsg_cmd_cachep, bc);

	spin_lock_irqsave(&bd->lock, flags);
	bd->queued_cmds--;
	spin_unlock_irqrestore(&bd->lock, flags);

	wake_up(&bd->wq_free);
}

static struct bsg_command *__bsg_alloc_command(struct bsg_device *bd)
{
	struct bsg_command *bc = NULL;

	spin_lock_irq(&bd->lock);

	if (bd->queued_cmds >= bd->max_queue)
		goto out;

	bd->queued_cmds++;
	spin_unlock_irq(&bd->lock);

	bc = kmem_cache_alloc(bsg_cmd_cachep, GFP_USER);
	if (unlikely(!bc)) {
		spin_lock_irq(&bd->lock);
		bd->queued_cmds--;
		goto out;
	}

	memset(bc, 0, sizeof(*bc));
	bc->bd = bd;
	INIT_LIST_HEAD(&bc->list);
	dprintk("%s: returning free cmd %p\n", bd->name, bc);
	return bc;
out:
	spin_unlock_irq(&bd->lock);
	return bc;
}

static inline void
bsg_del_done_cmd(struct bsg_device *bd, struct bsg_command *bc)
{
	bd->done_cmds--;
	list_del(&bc->list);
}

static inline void
bsg_add_done_cmd(struct bsg_device *bd, struct bsg_command *bc)
{
	bd->done_cmds++;
	list_add_tail(&bc->list, &bd->done_list);
	wake_up(&bd->wq_done);
}

static inline int bsg_io_schedule(struct bsg_device *bd, int state)
{
	DEFINE_WAIT(wait);
	int ret = 0;

	spin_lock_irq(&bd->lock);

	BUG_ON(bd->done_cmds > bd->queued_cmds);

	/*
	 * -ENOSPC or -ENODATA?  I'm going for -ENODATA, meaning "I have no
	 * work to do", even though we return -ENOSPC after this same test
	 * during bsg_write() -- there, it means our buffer can't have more
	 * bsg_commands added to it, thus has no space left.
	 */
	if (bd->done_cmds == bd->queued_cmds) {
		ret = -ENODATA;
		goto unlock;
	}

	if (!test_bit(BSG_F_BLOCK, &bd->flags)) {
		ret = -EAGAIN;
		goto unlock;
	}

	prepare_to_wait(&bd->wq_done, &wait, state);
	spin_unlock_irq(&bd->lock);
	io_schedule();
	finish_wait(&bd->wq_done, &wait);

	if ((state == TASK_INTERRUPTIBLE) && signal_pending(current))
		ret = -ERESTARTSYS;

	return ret;
unlock:
	spin_unlock_irq(&bd->lock);
	return ret;
}

/*
 * get a new free command, blocking if needed and specified
 */
static struct bsg_command *bsg_get_command(struct bsg_device *bd)
{
	struct bsg_command *bc;
	int ret;

	do {
		bc = __bsg_alloc_command(bd);
		if (bc)
			break;

		ret = bsg_io_schedule(bd, TASK_INTERRUPTIBLE);
		if (ret) {
			bc = ERR_PTR(ret);
			break;
		}

	} while (1);

	return bc;
}

static int blk_fill_sgv4_hdr_rq(request_queue_t *q, struct request *rq,
				struct sg_io_v4 *hdr, int has_write_perm)
{
	memset(rq->cmd, 0, BLK_MAX_CDB); /* ATAPI hates garbage after CDB */

	if (copy_from_user(rq->cmd, (void *)(unsigned long)hdr->request,
			   hdr->request_len))
		return -EFAULT;
	if (blk_verify_command(rq->cmd, has_write_perm))
		return -EPERM;

	/*
	 * fill in request structure
	 */
	rq->cmd_len = hdr->request_len;
	rq->cmd_type = REQ_TYPE_BLOCK_PC;

	rq->timeout = (hdr->timeout * HZ) / 1000;
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_SG_TIMEOUT;

	return 0;
}

/*
 * Check if sg_io_v4 from user is allowed and valid
 */
static int
bsg_validate_sgv4_hdr(request_queue_t *q, struct sg_io_v4 *hdr, int *rw)
{
	if (hdr->guard != 'Q')
		return -EINVAL;
	if (hdr->request_len > BLK_MAX_CDB)
		return -EINVAL;
	if (hdr->dout_xfer_len > (q->max_sectors << 9) ||
	    hdr->din_xfer_len > (q->max_sectors << 9))
		return -EIO;

	/* not supported currently */
	if (hdr->protocol || hdr->subprotocol)
		return -EINVAL;

	/*
	 * looks sane, if no data then it should be fine from our POV
	 */
	if (!hdr->dout_xfer_len && !hdr->din_xfer_len)
		return 0;

	/* not supported currently */
	if (hdr->dout_xfer_len && hdr->din_xfer_len)
		return -EINVAL;

	*rw = hdr->dout_xfer_len ? WRITE : READ;

	return 0;
}

/*
 * map sg_io_v4 to a request.
 */
static struct request *
bsg_map_hdr(struct bsg_device *bd, struct sg_io_v4 *hdr)
{
	request_queue_t *q = bd->queue;
	struct request *rq;
	int ret, rw = 0; /* shut up gcc */
	unsigned int dxfer_len;
	void *dxferp = NULL;

	dprintk("map hdr %llx/%u %llx/%u\n", (unsigned long long) hdr->dout_xferp,
		hdr->dout_xfer_len, (unsigned long long) hdr->din_xferp,
		hdr->din_xfer_len);

	ret = bsg_validate_sgv4_hdr(q, hdr, &rw);
	if (ret)
		return ERR_PTR(ret);

	/*
	 * map scatter-gather elements seperately and string them to request
	 */
	rq = blk_get_request(q, rw, GFP_KERNEL);
	ret = blk_fill_sgv4_hdr_rq(q, rq, hdr, test_bit(BSG_F_WRITE_PERM,
						       &bd->flags));
	if (ret) {
		blk_put_request(rq);
		return ERR_PTR(ret);
	}

	if (hdr->dout_xfer_len) {
		dxfer_len = hdr->dout_xfer_len;
		dxferp = (void*)(unsigned long)hdr->dout_xferp;
	} else if (hdr->din_xfer_len) {
		dxfer_len = hdr->din_xfer_len;
		dxferp = (void*)(unsigned long)hdr->din_xferp;
	} else
		dxfer_len = 0;

	if (dxfer_len) {
		ret = blk_rq_map_user(q, rq, dxferp, dxfer_len);
		if (ret) {
			dprintk("failed map at %d\n", ret);
			blk_put_request(rq);
			rq = ERR_PTR(ret);
		}
	}

	return rq;
}

/*
 * async completion call-back from the block layer, when scsi/ide/whatever
 * calls end_that_request_last() on a request
 */
static void bsg_rq_end_io(struct request *rq, int uptodate)
{
	struct bsg_command *bc = rq->end_io_data;
	struct bsg_device *bd = bc->bd;
	unsigned long flags;

	dprintk("%s: finished rq %p bc %p, bio %p stat %d\n",
		bd->name, rq, bc, bc->bio, uptodate);

	bc->hdr.duration = jiffies_to_msecs(jiffies - bc->hdr.duration);

	spin_lock_irqsave(&bd->lock, flags);
	list_del(&bc->list);
	bsg_add_done_cmd(bd, bc);
	spin_unlock_irqrestore(&bd->lock, flags);
}

/*
 * do final setup of a 'bc' and submit the matching 'rq' to the block
 * layer for io
 */
static void bsg_add_command(struct bsg_device *bd, request_queue_t *q,
			    struct bsg_command *bc, struct request *rq)
{
	rq->sense = bc->sense;
	rq->sense_len = 0;

	/*
	 * add bc command to busy queue and submit rq for io
	 */
	bc->rq = rq;
	bc->bio = rq->bio;
	bc->hdr.duration = jiffies;
	spin_lock_irq(&bd->lock);
	list_add_tail(&bc->list, &bd->busy_list);
	spin_unlock_irq(&bd->lock);

	dprintk("%s: queueing rq %p, bc %p\n", bd->name, rq, bc);

	rq->end_io_data = bc;
	blk_execute_rq_nowait(q, bd->disk, rq, 1, bsg_rq_end_io);
}

static inline struct bsg_command *bsg_next_done_cmd(struct bsg_device *bd)
{
	struct bsg_command *bc = NULL;

	spin_lock_irq(&bd->lock);
	if (bd->done_cmds) {
		bc = list_entry_bc(bd->done_list.next);
		bsg_del_done_cmd(bd, bc);
	}
	spin_unlock_irq(&bd->lock);

	return bc;
}

/*
 * Get a finished command from the done list
 */
static struct bsg_command *__bsg_get_done_cmd(struct bsg_device *bd, int state)
{
	struct bsg_command *bc;
	int ret;

	do {
		bc = bsg_next_done_cmd(bd);
		if (bc)
			break;

		ret = bsg_io_schedule(bd, state);
		if (ret) {
			bc = ERR_PTR(ret);
			break;
		}
	} while (1);

	dprintk("%s: returning done %p\n", bd->name, bc);

	return bc;
}

static struct bsg_command *
bsg_get_done_cmd(struct bsg_device *bd, const struct iovec *iov)
{
	return __bsg_get_done_cmd(bd, TASK_INTERRUPTIBLE);
}

static struct bsg_command *
bsg_get_done_cmd_nosignals(struct bsg_device *bd)
{
	return __bsg_get_done_cmd(bd, TASK_UNINTERRUPTIBLE);
}

static int blk_complete_sgv4_hdr_rq(struct request *rq, struct sg_io_v4 *hdr,
				    struct bio *bio)
{
	int ret = 0;

	dprintk("rq %p bio %p %u\n", rq, bio, rq->errors);
	/*
	 * fill in all the output members
	 */
	hdr->device_status = status_byte(rq->errors);
	hdr->transport_status = host_byte(rq->errors);
	hdr->driver_status = driver_byte(rq->errors);
	hdr->info = 0;
	if (hdr->device_status || hdr->transport_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->din_resid = rq->data_len;
	hdr->response_len = 0;

	if (rq->sense_len && hdr->response) {
		int len = min((unsigned int) hdr->max_response_len,
			      rq->sense_len);

		ret = copy_to_user((void*)(unsigned long)hdr->response,
				   rq->sense, len);
		if (!ret)
			hdr->response_len = len;
		else
			ret = -EFAULT;
	}

	blk_rq_unmap_user(bio);
	blk_put_request(rq);

	return ret;
}

static int bsg_complete_all_commands(struct bsg_device *bd)
{
	struct bsg_command *bc;
	int ret, tret;

	dprintk("%s: entered\n", bd->name);

	set_bit(BSG_F_BLOCK, &bd->flags);

	/*
	 * wait for all commands to complete
	 */
	ret = 0;
	do {
		ret = bsg_io_schedule(bd, TASK_UNINTERRUPTIBLE);
		/*
		 * look for -ENODATA specifically -- we'll sometimes get
		 * -ERESTARTSYS when we've taken a signal, but we can't
		 * return until we're done freeing the queue, so ignore
		 * it.  The signal will get handled when we're done freeing
		 * the bsg_device.
		 */
	} while (ret != -ENODATA);

	/*
	 * discard done commands
	 */
	ret = 0;
	do {
		bc = bsg_get_done_cmd_nosignals(bd);

		/*
		 * we _must_ complete before restarting, because
		 * bsg_release can't handle this failing.
		 */
		if (PTR_ERR(bc) == -ERESTARTSYS)
			continue;
		if (IS_ERR(bc)) {
			ret = PTR_ERR(bc);
			break;
		}

		tret = blk_complete_sgv4_hdr_rq(bc->rq, &bc->hdr, bc->bio);
		if (!ret)
			ret = tret;

		bsg_free_command(bc);
	} while (1);

	return ret;
}

typedef struct bsg_command *(*bsg_command_callback)(struct bsg_device *bd, const struct iovec *iov);

static ssize_t
__bsg_read(char __user *buf, size_t count, bsg_command_callback get_bc,
	   struct bsg_device *bd, const struct iovec *iov, ssize_t *bytes_read)
{
	struct bsg_command *bc;
	int nr_commands, ret;

	if (count % sizeof(struct sg_io_v4))
		return -EINVAL;

	ret = 0;
	nr_commands = count / sizeof(struct sg_io_v4);
	while (nr_commands) {
		bc = get_bc(bd, iov);
		if (IS_ERR(bc)) {
			ret = PTR_ERR(bc);
			break;
		}

		/*
		 * this is the only case where we need to copy data back
		 * after completing the request. so do that here,
		 * bsg_complete_work() cannot do that for us
		 */
		ret = blk_complete_sgv4_hdr_rq(bc->rq, &bc->hdr, bc->bio);

		if (copy_to_user(buf, (char *) &bc->hdr, sizeof(bc->hdr)))
			ret = -EFAULT;

		bsg_free_command(bc);

		if (ret)
			break;

		buf += sizeof(struct sg_io_v4);
		*bytes_read += sizeof(struct sg_io_v4);
		nr_commands--;
	}

	return ret;
}

static inline void bsg_set_block(struct bsg_device *bd, struct file *file)
{
	if (file->f_flags & O_NONBLOCK)
		clear_bit(BSG_F_BLOCK, &bd->flags);
	else
		set_bit(BSG_F_BLOCK, &bd->flags);
}

static inline void bsg_set_write_perm(struct bsg_device *bd, struct file *file)
{
	if (file->f_mode & FMODE_WRITE)
		set_bit(BSG_F_WRITE_PERM, &bd->flags);
	else
		clear_bit(BSG_F_WRITE_PERM, &bd->flags);
}

static inline int err_block_err(int ret)
{
	if (ret && ret != -ENOSPC && ret != -ENODATA && ret != -EAGAIN)
		return 1;

	return 0;
}

static ssize_t
bsg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct bsg_device *bd = file->private_data;
	int ret;
	ssize_t bytes_read;

	dprintk("%s: read %Zd bytes\n", bd->name, count);

	bsg_set_block(bd, file);
	bytes_read = 0;
	ret = __bsg_read(buf, count, bsg_get_done_cmd,
			bd, NULL, &bytes_read);
	*ppos = bytes_read;

	if (!bytes_read || (bytes_read && err_block_err(ret)))
		bytes_read = ret;

	return bytes_read;
}

static ssize_t __bsg_write(struct bsg_device *bd, const char __user *buf,
			   size_t count, ssize_t *bytes_read)
{
	struct bsg_command *bc;
	struct request *rq;
	int ret, nr_commands;

	if (count % sizeof(struct sg_io_v4))
		return -EINVAL;

	nr_commands = count / sizeof(struct sg_io_v4);
	rq = NULL;
	bc = NULL;
	ret = 0;
	while (nr_commands) {
		request_queue_t *q = bd->queue;

		bc = bsg_get_command(bd);
		if (!bc)
			break;
		if (IS_ERR(bc)) {
			ret = PTR_ERR(bc);
			bc = NULL;
			break;
		}

		bc->uhdr = (struct sg_io_v4 __user *) buf;
		if (copy_from_user(&bc->hdr, buf, sizeof(bc->hdr))) {
			ret = -EFAULT;
			break;
		}

		/*
		 * get a request, fill in the blanks, and add to request queue
		 */
		rq = bsg_map_hdr(bd, &bc->hdr);
		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			rq = NULL;
			break;
		}

		bsg_add_command(bd, q, bc, rq);
		bc = NULL;
		rq = NULL;
		nr_commands--;
		buf += sizeof(struct sg_io_v4);
		*bytes_read += sizeof(struct sg_io_v4);
	}

	if (bc)
		bsg_free_command(bc);

	return ret;
}

static ssize_t
bsg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct bsg_device *bd = file->private_data;
	ssize_t bytes_read;
	int ret;

	dprintk("%s: write %Zd bytes\n", bd->name, count);

	bsg_set_block(bd, file);
	bsg_set_write_perm(bd, file);

	bytes_read = 0;
	ret = __bsg_write(bd, buf, count, &bytes_read);
	*ppos = bytes_read;

	/*
	 * return bytes written on non-fatal errors
	 */
	if (!bytes_read || (bytes_read && err_block_err(ret)))
		bytes_read = ret;

	dprintk("%s: returning %Zd\n", bd->name, bytes_read);
	return bytes_read;
}

static struct bsg_device *bsg_alloc_device(void)
{
	struct bsg_device *bd;

	bd = kzalloc(sizeof(struct bsg_device), GFP_KERNEL);
	if (unlikely(!bd))
		return NULL;

	spin_lock_init(&bd->lock);

	bd->max_queue = BSG_DEFAULT_CMDS;

	INIT_LIST_HEAD(&bd->busy_list);
	INIT_LIST_HEAD(&bd->done_list);
	INIT_HLIST_NODE(&bd->dev_list);

	init_waitqueue_head(&bd->wq_free);
	init_waitqueue_head(&bd->wq_done);
	return bd;
}

static int bsg_put_device(struct bsg_device *bd)
{
	int ret = 0;

	mutex_lock(&bsg_mutex);

	if (!atomic_dec_and_test(&bd->ref_count))
		goto out;

	dprintk("%s: tearing down\n", bd->name);

	/*
	 * close can always block
	 */
	set_bit(BSG_F_BLOCK, &bd->flags);

	/*
	 * correct error detection baddies here again. it's the responsibility
	 * of the app to properly reap commands before close() if it wants
	 * fool-proof error detection
	 */
	ret = bsg_complete_all_commands(bd);

	blk_put_queue(bd->queue);
	hlist_del(&bd->dev_list);
	kfree(bd);
out:
	mutex_unlock(&bsg_mutex);
	return ret;
}

static struct bsg_device *bsg_add_device(struct inode *inode,
					 struct gendisk *disk,
					 struct file *file)
{
	struct bsg_device *bd = NULL;
#ifdef BSG_DEBUG
	unsigned char buf[32];
#endif

	bd = bsg_alloc_device();
	if (!bd)
		return ERR_PTR(-ENOMEM);

	bd->disk = disk;
	bd->queue = disk->queue;
	kobject_get(&disk->queue->kobj);
	bsg_set_block(bd, file);

	atomic_set(&bd->ref_count, 1);
	bd->minor = iminor(inode);
	mutex_lock(&bsg_mutex);
	hlist_add_head(&bd->dev_list,&bsg_device_list[bsg_list_idx(bd->minor)]);

	strncpy(bd->name, disk->disk_name, sizeof(bd->name) - 1);
	dprintk("bound to <%s>, max queue %d\n",
		format_dev_t(buf, inode->i_rdev), bd->max_queue);

	mutex_unlock(&bsg_mutex);
	return bd;
}

static struct bsg_device *__bsg_get_device(int minor)
{
	struct hlist_head *list = &bsg_device_list[bsg_list_idx(minor)];
	struct bsg_device *bd = NULL;
	struct hlist_node *entry;

	mutex_lock(&bsg_mutex);

	hlist_for_each(entry, list) {
		bd = hlist_entry(entry, struct bsg_device, dev_list);
		if (bd->minor == minor) {
			atomic_inc(&bd->ref_count);
			break;
		}

		bd = NULL;
	}

	mutex_unlock(&bsg_mutex);
	return bd;
}

static struct bsg_device *bsg_get_device(struct inode *inode, struct file *file)
{
	struct bsg_device *bd = __bsg_get_device(iminor(inode));
	struct bsg_class_device *bcd, *__bcd;

	if (bd)
		return bd;

	/*
	 * find the class device
	 */
	bcd = NULL;
	mutex_lock(&bsg_mutex);
	list_for_each_entry(__bcd, &bsg_class_list, list) {
		if (__bcd->minor == iminor(inode)) {
			bcd = __bcd;
			break;
		}
	}
	mutex_unlock(&bsg_mutex);

	if (!bcd)
		return ERR_PTR(-ENODEV);

	return bsg_add_device(inode, bcd->disk, file);
}

static int bsg_open(struct inode *inode, struct file *file)
{
	struct bsg_device *bd = bsg_get_device(inode, file);

	if (IS_ERR(bd))
		return PTR_ERR(bd);

	file->private_data = bd;
	return 0;
}

static int bsg_release(struct inode *inode, struct file *file)
{
	struct bsg_device *bd = file->private_data;

	file->private_data = NULL;
	return bsg_put_device(bd);
}

static unsigned int bsg_poll(struct file *file, poll_table *wait)
{
	struct bsg_device *bd = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &bd->wq_done, wait);
	poll_wait(file, &bd->wq_free, wait);

	spin_lock_irq(&bd->lock);
	if (!list_empty(&bd->done_list))
		mask |= POLLIN | POLLRDNORM;
	if (bd->queued_cmds >= bd->max_queue)
		mask |= POLLOUT;
	spin_unlock_irq(&bd->lock);

	return mask;
}

static int
bsg_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	struct bsg_device *bd = file->private_data;
	int __user *uarg = (int __user *) arg;

	if (!bd)
		return -ENXIO;

	switch (cmd) {
		/*
		 * our own ioctls
		 */
	case SG_GET_COMMAND_Q:
		return put_user(bd->max_queue, uarg);
	case SG_SET_COMMAND_Q: {
		int queue;

		if (get_user(queue, uarg))
			return -EFAULT;
		if (queue < 1)
			return -EINVAL;

		spin_lock_irq(&bd->lock);
		bd->max_queue = queue;
		spin_unlock_irq(&bd->lock);
		return 0;
	}

	/*
	 * SCSI/sg ioctls
	 */
	case SG_GET_VERSION_NUM:
	case SCSI_IOCTL_GET_IDLUN:
	case SCSI_IOCTL_GET_BUS_NUMBER:
	case SG_SET_TIMEOUT:
	case SG_GET_TIMEOUT:
	case SG_GET_RESERVED_SIZE:
	case SG_SET_RESERVED_SIZE:
	case SG_EMULATED_HOST:
	case SCSI_IOCTL_SEND_COMMAND: {
		void __user *uarg = (void __user *) arg;
		return scsi_cmd_ioctl(file, bd->disk, cmd, uarg);
	}
	case SG_IO: {
		struct request *rq;
		struct bio *bio;
		struct sg_io_v4 hdr;

		if (copy_from_user(&hdr, uarg, sizeof(hdr)))
			return -EFAULT;

		rq = bsg_map_hdr(bd, &hdr);
		if (IS_ERR(rq))
			return PTR_ERR(rq);

		bio = rq->bio;
		blk_execute_rq(bd->queue, bd->disk, rq, 0);
		blk_complete_sgv4_hdr_rq(rq, &hdr, bio);

		if (copy_to_user(uarg, &hdr, sizeof(hdr)))
			return -EFAULT;

		return 0;
	}
	/*
	 * block device ioctls
	 */
	default:
#if 0
		return ioctl_by_bdev(bd->bdev, cmd, arg);
#else
		return -ENOTTY;
#endif
	}
}

static struct file_operations bsg_fops = {
	.read		=	bsg_read,
	.write		=	bsg_write,
	.poll		=	bsg_poll,
	.open		=	bsg_open,
	.release	=	bsg_release,
	.ioctl		=	bsg_ioctl,
	.owner		=	THIS_MODULE,
};

void bsg_unregister_disk(struct gendisk *disk)
{
	struct bsg_class_device *bcd = &disk->bsg_dev;

	if (!bcd->class_dev)
		return;

	mutex_lock(&bsg_mutex);
	sysfs_remove_link(&bcd->disk->queue->kobj, "bsg");
	class_device_destroy(bsg_class, MKDEV(BSG_MAJOR, bcd->minor));
	bcd->class_dev = NULL;
	list_del_init(&bcd->list);
	mutex_unlock(&bsg_mutex);
}

int bsg_register_disk(struct gendisk *disk)
{
	request_queue_t *q = disk->queue;
	struct bsg_class_device *bcd;
	dev_t dev;

	/*
	 * we need a proper transport to send commands, not a stacked device
	 */
	if (!q->request_fn)
		return 0;

	bcd = &disk->bsg_dev;
	memset(bcd, 0, sizeof(*bcd));
	INIT_LIST_HEAD(&bcd->list);

	mutex_lock(&bsg_mutex);
	dev = MKDEV(BSG_MAJOR, bsg_device_nr);
	bcd->minor = bsg_device_nr;
	bsg_device_nr++;
	bcd->disk = disk;
	bcd->class_dev = class_device_create(bsg_class, NULL, dev, bcd->dev, "%s", disk->disk_name);
	if (!bcd->class_dev)
		goto err;
	list_add_tail(&bcd->list, &bsg_class_list);
	if (sysfs_create_link(&q->kobj, &bcd->class_dev->kobj, "bsg"))
		goto err;
	mutex_unlock(&bsg_mutex);
	return 0;
err:
	bsg_device_nr--;
	if (bcd->class_dev)
		class_device_destroy(bsg_class, MKDEV(BSG_MAJOR, bcd->minor));
	mutex_unlock(&bsg_mutex);
	return -ENOMEM;
}

static int __init bsg_init(void)
{
	int ret, i;

	bsg_cmd_cachep = kmem_cache_create("bsg_cmd",
				sizeof(struct bsg_command), 0, 0, NULL, NULL);
	if (!bsg_cmd_cachep) {
		printk(KERN_ERR "bsg: failed creating slab cache\n");
		return -ENOMEM;
	}

	for (i = 0; i < BSG_LIST_SIZE; i++)
		INIT_HLIST_HEAD(&bsg_device_list[i]);

	bsg_class = class_create(THIS_MODULE, "bsg");
	if (IS_ERR(bsg_class)) {
		kmem_cache_destroy(bsg_cmd_cachep);
		return PTR_ERR(bsg_class);
	}

	ret = register_chrdev(BSG_MAJOR, "bsg", &bsg_fops);
	if (ret) {
		kmem_cache_destroy(bsg_cmd_cachep);
		class_destroy(bsg_class);
		return ret;
	}

	printk(KERN_INFO "%s loaded\n", bsg_version);
	return 0;
}

MODULE_AUTHOR("Jens Axboe");
MODULE_DESCRIPTION("Block layer SGSI generic (sg) driver");
MODULE_LICENSE("GPL");

subsys_initcall(bsg_init);
