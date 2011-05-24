/*
 *  drivers/s390/char/tape_block.c
 *    block device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 */

#define KMSG_COMPONENT "tape"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>

#include <asm/debug.h>

#define TAPE_DBF_AREA	tape_core_dbf

#include "tape.h"

#define TAPEBLOCK_MAX_SEC	100
#define TAPEBLOCK_MIN_REQUEUE	3

/*
 * 2003/11/25  Stefan Bader <shbader@de.ibm.com>
 *
 * In 2.5/2.6 the block device request function is very likely to be called
 * with disabled interrupts (e.g. generic_unplug_device). So the driver can't
 * just call any function that tries to allocate CCW requests from that con-
 * text since it might sleep. There are two choices to work around this:
 *	a) do not allocate with kmalloc but use its own memory pool
 *      b) take requests from the queue outside that context, knowing that
 *         allocation might sleep
 */

/*
 * file operation structure for tape block frontend
 */
static DEFINE_MUTEX(tape_block_mutex);
static int tapeblock_open(struct block_device *, fmode_t);
static int tapeblock_release(struct gendisk *, fmode_t);
static unsigned int tapeblock_check_events(struct gendisk *, unsigned int);
static int tapeblock_revalidate_disk(struct gendisk *);

static const struct block_device_operations tapeblock_fops = {
	.owner		 = THIS_MODULE,
	.open		 = tapeblock_open,
	.release	 = tapeblock_release,
	.check_events	 = tapeblock_check_events,
	.revalidate_disk = tapeblock_revalidate_disk,
};

static int tapeblock_major = 0;

static void
tapeblock_trigger_requeue(struct tape_device *device)
{
	/* Protect against rescheduling. */
	if (atomic_cmpxchg(&device->blk_data.requeue_scheduled, 0, 1) != 0)
		return;
	schedule_work(&device->blk_data.requeue_task);
}

/*
 * Post finished request.
 */
static void
__tapeblock_end_request(struct tape_request *ccw_req, void *data)
{
	struct tape_device *device;
	struct request *req;

	DBF_LH(6, "__tapeblock_end_request()\n");

	device = ccw_req->device;
	req = (struct request *) data;
	blk_end_request_all(req, (ccw_req->rc == 0) ? 0 : -EIO);
	if (ccw_req->rc == 0)
		/* Update position. */
		device->blk_data.block_position =
		  (blk_rq_pos(req) + blk_rq_sectors(req)) >> TAPEBLOCK_HSEC_S2B;
	else
		/* We lost the position information due to an error. */
		device->blk_data.block_position = -1;
	device->discipline->free_bread(ccw_req);
	if (!list_empty(&device->req_queue) ||
	    blk_peek_request(device->blk_data.request_queue))
		tapeblock_trigger_requeue(device);
}

/*
 * Feed the tape device CCW queue with requests supplied in a list.
 */
static int
tapeblock_start_request(struct tape_device *device, struct request *req)
{
	struct tape_request *	ccw_req;
	int			rc;

	DBF_LH(6, "tapeblock_start_request(%p, %p)\n", device, req);

	ccw_req = device->discipline->bread(device, req);
	if (IS_ERR(ccw_req)) {
		DBF_EVENT(1, "TBLOCK: bread failed\n");
		blk_end_request_all(req, -EIO);
		return PTR_ERR(ccw_req);
	}
	ccw_req->callback = __tapeblock_end_request;
	ccw_req->callback_data = (void *) req;
	ccw_req->retries = TAPEBLOCK_RETRIES;

	rc = tape_do_io_async(device, ccw_req);
	if (rc) {
		/*
		 * Start/enqueueing failed. No retries in
		 * this case.
		 */
		blk_end_request_all(req, -EIO);
		device->discipline->free_bread(ccw_req);
	}

	return rc;
}

/*
 * Move requests from the block device request queue to the tape device ccw
 * queue.
 */
static void
tapeblock_requeue(struct work_struct *work) {
	struct tape_blk_data *	blkdat;
	struct tape_device *	device;
	struct request_queue *	queue;
	int			nr_queued;
	struct request *	req;
	struct list_head *	l;
	int			rc;

	blkdat = container_of(work, struct tape_blk_data, requeue_task);
	device = blkdat->device;
	if (!device)
		return;

	spin_lock_irq(get_ccwdev_lock(device->cdev));
	queue  = device->blk_data.request_queue;

	/* Count number of requests on ccw queue. */
	nr_queued = 0;
	list_for_each(l, &device->req_queue)
		nr_queued++;
	spin_unlock(get_ccwdev_lock(device->cdev));

	spin_lock_irq(&device->blk_data.request_queue_lock);
	while (
		blk_peek_request(queue) &&
		nr_queued < TAPEBLOCK_MIN_REQUEUE
	) {
		req = blk_fetch_request(queue);
		if (rq_data_dir(req) == WRITE) {
			DBF_EVENT(1, "TBLOCK: Rejecting write request\n");
			spin_unlock_irq(&device->blk_data.request_queue_lock);
			blk_end_request_all(req, -EIO);
			spin_lock_irq(&device->blk_data.request_queue_lock);
			continue;
		}
		nr_queued++;
		spin_unlock_irq(&device->blk_data.request_queue_lock);
		rc = tapeblock_start_request(device, req);
		spin_lock_irq(&device->blk_data.request_queue_lock);
	}
	spin_unlock_irq(&device->blk_data.request_queue_lock);
	atomic_set(&device->blk_data.requeue_scheduled, 0);
}

/*
 * Tape request queue function. Called from ll_rw_blk.c
 */
static void
tapeblock_request_fn(struct request_queue *queue)
{
	struct tape_device *device;

	device = (struct tape_device *) queue->queuedata;
	DBF_LH(6, "tapeblock_request_fn(device=%p)\n", device);
	BUG_ON(device == NULL);
	tapeblock_trigger_requeue(device);
}

/*
 * This function is called for every new tapedevice
 */
int
tapeblock_setup_device(struct tape_device * device)
{
	struct tape_blk_data *	blkdat;
	struct gendisk *	disk;
	int			rc;

	blkdat = &device->blk_data;
	blkdat->device = device;
	spin_lock_init(&blkdat->request_queue_lock);
	atomic_set(&blkdat->requeue_scheduled, 0);

	blkdat->request_queue = blk_init_queue(
		tapeblock_request_fn,
		&blkdat->request_queue_lock
	);
	if (!blkdat->request_queue)
		return -ENOMEM;

	rc = elevator_change(blkdat->request_queue, "noop");
	if (rc)
		goto cleanup_queue;

	blk_queue_logical_block_size(blkdat->request_queue, TAPEBLOCK_HSEC_SIZE);
	blk_queue_max_hw_sectors(blkdat->request_queue, TAPEBLOCK_MAX_SEC);
	blk_queue_max_segments(blkdat->request_queue, -1L);
	blk_queue_max_segment_size(blkdat->request_queue, -1L);
	blk_queue_segment_boundary(blkdat->request_queue, -1L);

	disk = alloc_disk(1);
	if (!disk) {
		rc = -ENOMEM;
		goto cleanup_queue;
	}

	disk->major = tapeblock_major;
	disk->first_minor = device->first_minor;
	disk->fops = &tapeblock_fops;
	disk->private_data = tape_get_device(device);
	disk->queue = blkdat->request_queue;
	set_capacity(disk, 0);
	sprintf(disk->disk_name, "btibm%d",
		device->first_minor / TAPE_MINORS_PER_DEV);

	blkdat->disk = disk;
	blkdat->medium_changed = 1;
	blkdat->request_queue->queuedata = tape_get_device(device);

	add_disk(disk);

	tape_get_device(device);
	INIT_WORK(&blkdat->requeue_task, tapeblock_requeue);

	return 0;

cleanup_queue:
	blk_cleanup_queue(blkdat->request_queue);
	blkdat->request_queue = NULL;

	return rc;
}

void
tapeblock_cleanup_device(struct tape_device *device)
{
	flush_work_sync(&device->blk_data.requeue_task);
	tape_put_device(device);

	if (!device->blk_data.disk) {
		goto cleanup_queue;
	}

	del_gendisk(device->blk_data.disk);
	device->blk_data.disk->private_data = NULL;
	tape_put_device(device);
	put_disk(device->blk_data.disk);

	device->blk_data.disk = NULL;
cleanup_queue:
	device->blk_data.request_queue->queuedata = NULL;
	tape_put_device(device);

	blk_cleanup_queue(device->blk_data.request_queue);
	device->blk_data.request_queue = NULL;
}

/*
 * Detect number of blocks of the tape.
 * FIXME: can we extent this to detect the blocks size as well ?
 */
static int
tapeblock_revalidate_disk(struct gendisk *disk)
{
	struct tape_device *	device;
	unsigned int		nr_of_blks;
	int			rc;

	device = (struct tape_device *) disk->private_data;
	BUG_ON(!device);

	if (!device->blk_data.medium_changed)
		return 0;

	rc = tape_mtop(device, MTFSFM, 1);
	if (rc)
		return rc;

	rc = tape_mtop(device, MTTELL, 1);
	if (rc < 0)
		return rc;

	pr_info("%s: Determining the size of the recorded area...\n",
		dev_name(&device->cdev->dev));
	DBF_LH(3, "Image file ends at %d\n", rc);
	nr_of_blks = rc;

	/* This will fail for the first file. Catch the error by checking the
	 * position. */
	tape_mtop(device, MTBSF, 1);

	rc = tape_mtop(device, MTTELL, 1);
	if (rc < 0)
		return rc;

	if (rc > nr_of_blks)
		return -EINVAL;

	DBF_LH(3, "Image file starts at %d\n", rc);
	device->bof = rc;
	nr_of_blks -= rc;

	pr_info("%s: The size of the recorded area is %i blocks\n",
		dev_name(&device->cdev->dev), nr_of_blks);
	set_capacity(device->blk_data.disk,
		nr_of_blks*(TAPEBLOCK_HSEC_SIZE/512));

	device->blk_data.block_position = 0;
	device->blk_data.medium_changed = 0;
	return 0;
}

static unsigned int
tapeblock_check_events(struct gendisk *disk, unsigned int clearing)
{
	struct tape_device *device;

	device = (struct tape_device *) disk->private_data;
	DBF_LH(6, "tapeblock_medium_changed(%p) = %d\n",
		device, device->blk_data.medium_changed);

	return device->blk_data.medium_changed ? DISK_EVENT_MEDIA_CHANGE : 0;
}

/*
 * Block frontend tape device open function.
 */
static int
tapeblock_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *	disk = bdev->bd_disk;
	struct tape_device *	device;
	int			rc;

	mutex_lock(&tape_block_mutex);
	device = tape_get_device(disk->private_data);

	if (device->required_tapemarks) {
		DBF_EVENT(2, "TBLOCK: missing tapemarks\n");
		pr_warning("%s: Opening the tape failed because of missing "
			   "end-of-file marks\n", dev_name(&device->cdev->dev));
		rc = -EPERM;
		goto put_device;
	}

	rc = tape_open(device);
	if (rc)
		goto put_device;

	rc = tapeblock_revalidate_disk(disk);
	if (rc)
		goto release;

	/*
	 * Note: The reference to <device> is hold until the release function
	 *       is called.
	 */
	tape_state_set(device, TS_BLKUSE);
	mutex_unlock(&tape_block_mutex);
	return 0;

release:
	tape_release(device);
 put_device:
	tape_put_device(device);
	mutex_unlock(&tape_block_mutex);
	return rc;
}

/*
 * Block frontend tape device release function.
 *
 * Note: One reference to the tape device was made by the open function. So
 *       we just get the pointer here and release the reference.
 */
static int
tapeblock_release(struct gendisk *disk, fmode_t mode)
{
	struct tape_device *device = disk->private_data;
 
	mutex_lock(&tape_block_mutex);
	tape_state_set(device, TS_IN_USE);
	tape_release(device);
	tape_put_device(device);
	mutex_unlock(&tape_block_mutex);

	return 0;
}

/*
 * Initialize block device frontend.
 */
int
tapeblock_init(void)
{
	int rc;

	/* Register the tape major number to the kernel */
	rc = register_blkdev(tapeblock_major, "tBLK");
	if (rc < 0)
		return rc;

	if (tapeblock_major == 0)
		tapeblock_major = rc;
	return 0;
}

/*
 * Deregister major for block device frontend
 */
void
tapeblock_exit(void)
{
	unregister_blkdev(tapeblock_major, "tBLK");
}
