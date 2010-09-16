/*
 * bsg.c - block layer implementation of the sg v4 interface
 *
 * Copyright (C) 2004 Jens Axboe <axboe@suse.de> SUSE Labs
 * Copyright (C) 2004 Peter M. Jones <pjones@redhat.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License version 2.  See the file "COPYING" in the main directory of this
 *  archive for more details.
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
#include <linux/idr.h>
#include <linux/bsg.h>
#include <linux/smp_lock.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/sg.h>

#define BSG_DESCRIPTION	"Block layer SCSI generic (bsg) driver"
#define BSG_VERSION	"0.4"

struct bsg_device {
	struct request_queue *queue;
	spinlock_t lock;
	struct list_head busy_list;
	struct list_head done_list;
	struct hlist_node dev_list;
	atomic_t ref_count;
	int queued_cmds;
	int done_cmds;
	wait_queue_head_t wq_done;
	wait_queue_head_t wq_free;
	char name[20];
	int max_queue;
	unsigned long flags;
};

enum {
	BSG_F_BLOCK		= 1,
};

#define BSG_DEFAULT_CMDS	64
#define BSG_MAX_DEVS		32768

#undef BSG_DEBUG

#ifdef BSG_DEBUG
#define dprintk(fmt, args...) printk(KERN_ERR "%s: " fmt, __func__, ##args)
#else
#define dprintk(fmt, args...)
#endif

static DEFINE_MUTEX(bsg_mutex);
static DEFINE_IDR(bsg_minor_idr);

#define BSG_LIST_ARRAY_SIZE	8
static struct hlist_head bsg_device_list[BSG_LIST_ARRAY_SIZE];

static struct class *bsg_class;
static int bsg_major;

static struct kmem_cache *bsg_cmd_cachep;

/*
 * our internal command type
 */
struct bsg_command {
	struct bsg_device *bd;
	struct list_head list;
	struct request *rq;
	struct bio *bio;
	struct bio *bidi_bio;
	int err;
	struct sg_io_v4 hdr;
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

static struct bsg_command *bsg_alloc_command(struct bsg_device *bd)
{
	struct bsg_command *bc = ERR_PTR(-EINVAL);

	spin_lock_irq(&bd->lock);

	if (bd->queued_cmds >= bd->max_queue)
		goto out;

	bd->queued_cmds++;
	spin_unlock_irq(&bd->lock);

	bc = kmem_cache_zalloc(bsg_cmd_cachep, GFP_KERNEL);
	if (unlikely(!bc)) {
		spin_lock_irq(&bd->lock);
		bd->queued_cmds--;
		bc = ERR_PTR(-ENOMEM);
		goto out;
	}

	bc->bd = bd;
	INIT_LIST_HEAD(&bc->list);
	dprintk("%s: returning free cmd %p\n", bd->name, bc);
	return bc;
out:
	spin_unlock_irq(&bd->lock);
	return bc;
}

static inline struct hlist_head *bsg_dev_idx_hash(int index)
{
	return &bsg_device_list[index & (BSG_LIST_ARRAY_SIZE - 1)];
}

static int bsg_io_schedule(struct bsg_device *bd)
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

	prepare_to_wait(&bd->wq_done, &wait, TASK_UNINTERRUPTIBLE);
	spin_unlock_irq(&bd->lock);
	io_schedule();
	finish_wait(&bd->wq_done, &wait);

	return ret;
unlock:
	spin_unlock_irq(&bd->lock);
	return ret;
}

static int blk_fill_sgv4_hdr_rq(struct request_queue *q, struct request *rq,
				struct sg_io_v4 *hdr, struct bsg_device *bd,
				fmode_t has_write_perm)
{
	if (hdr->request_len > BLK_MAX_CDB) {
		rq->cmd = kzalloc(hdr->request_len, GFP_KERNEL);
		if (!rq->cmd)
			return -ENOMEM;
	}

	if (copy_from_user(rq->cmd, (void *)(unsigned long)hdr->request,
			   hdr->request_len))
		return -EFAULT;

	if (hdr->subprotocol == BSG_SUB_PROTOCOL_SCSI_CMD) {
		if (blk_verify_command(rq->cmd, has_write_perm))
			return -EPERM;
	} else if (!capable(CAP_SYS_RAWIO))
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
	if (rq->timeout < BLK_MIN_SG_TIMEOUT)
		rq->timeout = BLK_MIN_SG_TIMEOUT;

	return 0;
}

/*
 * Check if sg_io_v4 from user is allowed and valid
 */
static int
bsg_validate_sgv4_hdr(struct request_queue *q, struct sg_io_v4 *hdr, int *rw)
{
	int ret = 0;

	if (hdr->guard != 'Q')
		return -EINVAL;

	switch (hdr->protocol) {
	case BSG_PROTOCOL_SCSI:
		switch (hdr->subprotocol) {
		case BSG_SUB_PROTOCOL_SCSI_CMD:
		case BSG_SUB_PROTOCOL_SCSI_TRANSPORT:
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	*rw = hdr->dout_xfer_len ? WRITE : READ;
	return ret;
}

/*
 * map sg_io_v4 to a request.
 */
static struct request *
bsg_map_hdr(struct bsg_device *bd, struct sg_io_v4 *hdr, fmode_t has_write_perm,
	    u8 *sense)
{
	struct request_queue *q = bd->queue;
	struct request *rq, *next_rq = NULL;
	int ret, rw;
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
	if (!rq)
		return ERR_PTR(-ENOMEM);
	ret = blk_fill_sgv4_hdr_rq(q, rq, hdr, bd, has_write_perm);
	if (ret)
		goto out;

	if (rw == WRITE && hdr->din_xfer_len) {
		if (!test_bit(QUEUE_FLAG_BIDI, &q->queue_flags)) {
			ret = -EOPNOTSUPP;
			goto out;
		}

		next_rq = blk_get_request(q, READ, GFP_KERNEL);
		if (!next_rq) {
			ret = -ENOMEM;
			goto out;
		}
		rq->next_rq = next_rq;
		next_rq->cmd_type = rq->cmd_type;

		dxferp = (void*)(unsigned long)hdr->din_xferp;
		ret =  blk_rq_map_user(q, next_rq, NULL, dxferp,
				       hdr->din_xfer_len, GFP_KERNEL);
		if (ret)
			goto out;
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
		ret = blk_rq_map_user(q, rq, NULL, dxferp, dxfer_len,
				      GFP_KERNEL);
		if (ret)
			goto out;
	}

	rq->sense = sense;
	rq->sense_len = 0;

	return rq;
out:
	if (rq->cmd != rq->__cmd)
		kfree(rq->cmd);
	blk_put_request(rq);
	if (next_rq) {
		blk_rq_unmap_user(next_rq->bio);
		blk_put_request(next_rq);
	}
	return ERR_PTR(ret);
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
	list_move_tail(&bc->list, &bd->done_list);
	bd->done_cmds++;
	spin_unlock_irqrestore(&bd->lock, flags);

	wake_up(&bd->wq_done);
}

/*
 * do final setup of a 'bc' and submit the matching 'rq' to the block
 * layer for io
 */
static void bsg_add_command(struct bsg_device *bd, struct request_queue *q,
			    struct bsg_command *bc, struct request *rq)
{
	int at_head = (0 == (bc->hdr.flags & BSG_FLAG_Q_AT_TAIL));

	/*
	 * add bc command to busy queue and submit rq for io
	 */
	bc->rq = rq;
	bc->bio = rq->bio;
	if (rq->next_rq)
		bc->bidi_bio = rq->next_rq->bio;
	bc->hdr.duration = jiffies;
	spin_lock_irq(&bd->lock);
	list_add_tail(&bc->list, &bd->busy_list);
	spin_unlock_irq(&bd->lock);

	dprintk("%s: queueing rq %p, bc %p\n", bd->name, rq, bc);

	rq->end_io_data = bc;
	blk_execute_rq_nowait(q, NULL, rq, at_head, bsg_rq_end_io);
}

static struct bsg_command *bsg_next_done_cmd(struct bsg_device *bd)
{
	struct bsg_command *bc = NULL;

	spin_lock_irq(&bd->lock);
	if (bd->done_cmds) {
		bc = list_first_entry(&bd->done_list, struct bsg_command, list);
		list_del(&bc->list);
		bd->done_cmds--;
	}
	spin_unlock_irq(&bd->lock);

	return bc;
}

/*
 * Get a finished command from the done list
 */
static struct bsg_command *bsg_get_done_cmd(struct bsg_device *bd)
{
	struct bsg_command *bc;
	int ret;

	do {
		bc = bsg_next_done_cmd(bd);
		if (bc)
			break;

		if (!test_bit(BSG_F_BLOCK, &bd->flags)) {
			bc = ERR_PTR(-EAGAIN);
			break;
		}

		ret = wait_event_interruptible(bd->wq_done, bd->done_cmds);
		if (ret) {
			bc = ERR_PTR(-ERESTARTSYS);
			break;
		}
	} while (1);

	dprintk("%s: returning done %p\n", bd->name, bc);

	return bc;
}

static int blk_complete_sgv4_hdr_rq(struct request *rq, struct sg_io_v4 *hdr,
				    struct bio *bio, struct bio *bidi_bio)
{
	int ret = 0;

	dprintk("rq %p bio %p 0x%x\n", rq, bio, rq->errors);
	/*
	 * fill in all the output members
	 */
	hdr->device_status = rq->errors & 0xff;
	hdr->transport_status = host_byte(rq->errors);
	hdr->driver_status = driver_byte(rq->errors);
	hdr->info = 0;
	if (hdr->device_status || hdr->transport_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->response_len = 0;

	if (rq->sense_len && hdr->response) {
		int len = min_t(unsigned int, hdr->max_response_len,
					rq->sense_len);

		ret = copy_to_user((void*)(unsigned long)hdr->response,
				   rq->sense, len);
		if (!ret)
			hdr->response_len = len;
		else
			ret = -EFAULT;
	}

	if (rq->next_rq) {
		hdr->dout_resid = rq->resid_len;
		hdr->din_resid = rq->next_rq->resid_len;
		blk_rq_unmap_user(bidi_bio);
		blk_put_request(rq->next_rq);
	} else if (rq_data_dir(rq) == READ)
		hdr->din_resid = rq->resid_len;
	else
		hdr->dout_resid = rq->resid_len;

	/*
	 * If the request generated a negative error number, return it
	 * (providing we aren't already returning an error); if it's
	 * just a protocol response (i.e. non negative), that gets
	 * processed above.
	 */
	if (!ret && rq->errors < 0)
		ret = rq->errors;

	blk_rq_unmap_user(bio);
	if (rq->cmd != rq->__cmd)
		kfree(rq->cmd);
	blk_put_request(rq);

	return ret;
}

static int bsg_complete_all_commands(struct bsg_device *bd)
{
	struct bsg_command *bc;
	int ret, tret;

	dprintk("%s: entered\n", bd->name);

	/*
	 * wait for all commands to complete
	 */
	ret = 0;
	do {
		ret = bsg_io_schedule(bd);
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
		spin_lock_irq(&bd->lock);
		if (!bd->queued_cmds) {
			spin_unlock_irq(&bd->lock);
			break;
		}
		spin_unlock_irq(&bd->lock);

		bc = bsg_get_done_cmd(bd);
		if (IS_ERR(bc))
			break;

		tret = blk_complete_sgv4_hdr_rq(bc->rq, &bc->hdr, bc->bio,
						bc->bidi_bio);
		if (!ret)
			ret = tret;

		bsg_free_command(bc);
	} while (1);

	return ret;
}

static int
__bsg_read(char __user *buf, size_t count, struct bsg_device *bd,
	   const struct iovec *iov, ssize_t *bytes_read)
{
	struct bsg_command *bc;
	int nr_commands, ret;

	if (count % sizeof(struct sg_io_v4))
		return -EINVAL;

	ret = 0;
	nr_commands = count / sizeof(struct sg_io_v4);
	while (nr_commands) {
		bc = bsg_get_done_cmd(bd);
		if (IS_ERR(bc)) {
			ret = PTR_ERR(bc);
			break;
		}

		/*
		 * this is the only case where we need to copy data back
		 * after completing the request. so do that here,
		 * bsg_complete_work() cannot do that for us
		 */
		ret = blk_complete_sgv4_hdr_rq(bc->rq, &bc->hdr, bc->bio,
					       bc->bidi_bio);

		if (copy_to_user(buf, &bc->hdr, sizeof(bc->hdr)))
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

/*
 * Check if the error is a "real" error that we should return.
 */
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
	ret = __bsg_read(buf, count, bd, NULL, &bytes_read);
	*ppos = bytes_read;

	if (!bytes_read || (bytes_read && err_block_err(ret)))
		bytes_read = ret;

	return bytes_read;
}

static int __bsg_write(struct bsg_device *bd, const char __user *buf,
		       size_t count, ssize_t *bytes_written,
		       fmode_t has_write_perm)
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
		struct request_queue *q = bd->queue;

		bc = bsg_alloc_command(bd);
		if (IS_ERR(bc)) {
			ret = PTR_ERR(bc);
			bc = NULL;
			break;
		}

		if (copy_from_user(&bc->hdr, buf, sizeof(bc->hdr))) {
			ret = -EFAULT;
			break;
		}

		/*
		 * get a request, fill in the blanks, and add to request queue
		 */
		rq = bsg_map_hdr(bd, &bc->hdr, has_write_perm, bc->sense);
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
		*bytes_written += sizeof(struct sg_io_v4);
	}

	if (bc)
		bsg_free_command(bc);

	return ret;
}

static ssize_t
bsg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct bsg_device *bd = file->private_data;
	ssize_t bytes_written;
	int ret;

	dprintk("%s: write %Zd bytes\n", bd->name, count);

	bsg_set_block(bd, file);

	bytes_written = 0;
	ret = __bsg_write(bd, buf, count, &bytes_written,
			  file->f_mode & FMODE_WRITE);

	*ppos = bytes_written;

	/*
	 * return bytes written on non-fatal errors
	 */
	if (!bytes_written || (bytes_written && err_block_err(ret)))
		bytes_written = ret;

	dprintk("%s: returning %Zd\n", bd->name, bytes_written);
	return bytes_written;
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

static void bsg_kref_release_function(struct kref *kref)
{
	struct bsg_class_device *bcd =
		container_of(kref, struct bsg_class_device, ref);
	struct device *parent = bcd->parent;

	if (bcd->release)
		bcd->release(bcd->parent);

	put_device(parent);
}

static int bsg_put_device(struct bsg_device *bd)
{
	int ret = 0, do_free;
	struct request_queue *q = bd->queue;

	mutex_lock(&bsg_mutex);

	do_free = atomic_dec_and_test(&bd->ref_count);
	if (!do_free) {
		mutex_unlock(&bsg_mutex);
		goto out;
	}

	hlist_del(&bd->dev_list);
	mutex_unlock(&bsg_mutex);

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

	kfree(bd);
out:
	kref_put(&q->bsg_dev.ref, bsg_kref_release_function);
	if (do_free)
		blk_put_queue(q);
	return ret;
}

static struct bsg_device *bsg_add_device(struct inode *inode,
					 struct request_queue *rq,
					 struct file *file)
{
	struct bsg_device *bd;
	int ret;
#ifdef BSG_DEBUG
	unsigned char buf[32];
#endif
	ret = blk_get_queue(rq);
	if (ret)
		return ERR_PTR(-ENXIO);

	bd = bsg_alloc_device();
	if (!bd) {
		blk_put_queue(rq);
		return ERR_PTR(-ENOMEM);
	}

	bd->queue = rq;

	bsg_set_block(bd, file);

	atomic_set(&bd->ref_count, 1);
	mutex_lock(&bsg_mutex);
	hlist_add_head(&bd->dev_list, bsg_dev_idx_hash(iminor(inode)));

	strncpy(bd->name, dev_name(rq->bsg_dev.class_dev), sizeof(bd->name) - 1);
	dprintk("bound to <%s>, max queue %d\n",
		format_dev_t(buf, inode->i_rdev), bd->max_queue);

	mutex_unlock(&bsg_mutex);
	return bd;
}

static struct bsg_device *__bsg_get_device(int minor, struct request_queue *q)
{
	struct bsg_device *bd;
	struct hlist_node *entry;

	mutex_lock(&bsg_mutex);

	hlist_for_each_entry(bd, entry, bsg_dev_idx_hash(minor), dev_list) {
		if (bd->queue == q) {
			atomic_inc(&bd->ref_count);
			goto found;
		}
	}
	bd = NULL;
found:
	mutex_unlock(&bsg_mutex);
	return bd;
}

static struct bsg_device *bsg_get_device(struct inode *inode, struct file *file)
{
	struct bsg_device *bd;
	struct bsg_class_device *bcd;

	/*
	 * find the class device
	 */
	mutex_lock(&bsg_mutex);
	bcd = idr_find(&bsg_minor_idr, iminor(inode));
	if (bcd)
		kref_get(&bcd->ref);
	mutex_unlock(&bsg_mutex);

	if (!bcd)
		return ERR_PTR(-ENODEV);

	bd = __bsg_get_device(iminor(inode), bcd->queue);
	if (bd)
		return bd;

	bd = bsg_add_device(inode, bcd->queue, file);
	if (IS_ERR(bd))
		kref_put(&bcd->ref, bsg_kref_release_function);

	return bd;
}

static int bsg_open(struct inode *inode, struct file *file)
{
	struct bsg_device *bd;

	lock_kernel();
	bd = bsg_get_device(inode, file);
	unlock_kernel();

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

static long bsg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bsg_device *bd = file->private_data;
	int __user *uarg = (int __user *) arg;
	int ret;

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
		return scsi_cmd_ioctl(bd->queue, NULL, file->f_mode, cmd, uarg);
	}
	case SG_IO: {
		struct request *rq;
		struct bio *bio, *bidi_bio = NULL;
		struct sg_io_v4 hdr;
		int at_head;
		u8 sense[SCSI_SENSE_BUFFERSIZE];

		if (copy_from_user(&hdr, uarg, sizeof(hdr)))
			return -EFAULT;

		rq = bsg_map_hdr(bd, &hdr, file->f_mode & FMODE_WRITE, sense);
		if (IS_ERR(rq))
			return PTR_ERR(rq);

		bio = rq->bio;
		if (rq->next_rq)
			bidi_bio = rq->next_rq->bio;

		at_head = (0 == (hdr.flags & BSG_FLAG_Q_AT_TAIL));
		blk_execute_rq(bd->queue, NULL, rq, at_head);
		ret = blk_complete_sgv4_hdr_rq(rq, &hdr, bio, bidi_bio);

		if (copy_to_user(uarg, &hdr, sizeof(hdr)))
			return -EFAULT;

		return ret;
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

static const struct file_operations bsg_fops = {
	.read		=	bsg_read,
	.write		=	bsg_write,
	.poll		=	bsg_poll,
	.open		=	bsg_open,
	.release	=	bsg_release,
	.unlocked_ioctl	=	bsg_ioctl,
	.owner		=	THIS_MODULE,
};

void bsg_unregister_queue(struct request_queue *q)
{
	struct bsg_class_device *bcd = &q->bsg_dev;

	if (!bcd->class_dev)
		return;

	mutex_lock(&bsg_mutex);
	idr_remove(&bsg_minor_idr, bcd->minor);
	sysfs_remove_link(&q->kobj, "bsg");
	device_unregister(bcd->class_dev);
	bcd->class_dev = NULL;
	kref_put(&bcd->ref, bsg_kref_release_function);
	mutex_unlock(&bsg_mutex);
}
EXPORT_SYMBOL_GPL(bsg_unregister_queue);

int bsg_register_queue(struct request_queue *q, struct device *parent,
		       const char *name, void (*release)(struct device *))
{
	struct bsg_class_device *bcd;
	dev_t dev;
	int ret, minor;
	struct device *class_dev = NULL;
	const char *devname;

	if (name)
		devname = name;
	else
		devname = dev_name(parent);

	/*
	 * we need a proper transport to send commands, not a stacked device
	 */
	if (!q->request_fn)
		return 0;

	bcd = &q->bsg_dev;
	memset(bcd, 0, sizeof(*bcd));

	mutex_lock(&bsg_mutex);

	ret = idr_pre_get(&bsg_minor_idr, GFP_KERNEL);
	if (!ret) {
		ret = -ENOMEM;
		goto unlock;
	}

	ret = idr_get_new(&bsg_minor_idr, bcd, &minor);
	if (ret < 0)
		goto unlock;

	if (minor >= BSG_MAX_DEVS) {
		printk(KERN_ERR "bsg: too many bsg devices\n");
		ret = -EINVAL;
		goto remove_idr;
	}

	bcd->minor = minor;
	bcd->queue = q;
	bcd->parent = get_device(parent);
	bcd->release = release;
	kref_init(&bcd->ref);
	dev = MKDEV(bsg_major, bcd->minor);
	class_dev = device_create(bsg_class, parent, dev, NULL, "%s", devname);
	if (IS_ERR(class_dev)) {
		ret = PTR_ERR(class_dev);
		goto put_dev;
	}
	bcd->class_dev = class_dev;

	if (q->kobj.sd) {
		ret = sysfs_create_link(&q->kobj, &bcd->class_dev->kobj, "bsg");
		if (ret)
			goto unregister_class_dev;
	}

	mutex_unlock(&bsg_mutex);
	return 0;

unregister_class_dev:
	device_unregister(class_dev);
put_dev:
	put_device(parent);
remove_idr:
	idr_remove(&bsg_minor_idr, minor);
unlock:
	mutex_unlock(&bsg_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(bsg_register_queue);

static struct cdev bsg_cdev;

static char *bsg_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "bsg/%s", dev_name(dev));
}

static int __init bsg_init(void)
{
	int ret, i;
	dev_t devid;

	bsg_cmd_cachep = kmem_cache_create("bsg_cmd",
				sizeof(struct bsg_command), 0, 0, NULL);
	if (!bsg_cmd_cachep) {
		printk(KERN_ERR "bsg: failed creating slab cache\n");
		return -ENOMEM;
	}

	for (i = 0; i < BSG_LIST_ARRAY_SIZE; i++)
		INIT_HLIST_HEAD(&bsg_device_list[i]);

	bsg_class = class_create(THIS_MODULE, "bsg");
	if (IS_ERR(bsg_class)) {
		ret = PTR_ERR(bsg_class);
		goto destroy_kmemcache;
	}
	bsg_class->devnode = bsg_devnode;

	ret = alloc_chrdev_region(&devid, 0, BSG_MAX_DEVS, "bsg");
	if (ret)
		goto destroy_bsg_class;

	bsg_major = MAJOR(devid);

	cdev_init(&bsg_cdev, &bsg_fops);
	ret = cdev_add(&bsg_cdev, MKDEV(bsg_major, 0), BSG_MAX_DEVS);
	if (ret)
		goto unregister_chrdev;

	printk(KERN_INFO BSG_DESCRIPTION " version " BSG_VERSION
	       " loaded (major %d)\n", bsg_major);
	return 0;
unregister_chrdev:
	unregister_chrdev_region(MKDEV(bsg_major, 0), BSG_MAX_DEVS);
destroy_bsg_class:
	class_destroy(bsg_class);
destroy_kmemcache:
	kmem_cache_destroy(bsg_cmd_cachep);
	return ret;
}

MODULE_AUTHOR("Jens Axboe");
MODULE_DESCRIPTION(BSG_DESCRIPTION);
MODULE_LICENSE("GPL");

device_initcall(bsg_init);
