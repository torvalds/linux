// SPDX-License-Identifier: GPL-2.0
/*
 * Stream data over Thunderbolt/USB4 cable
 *
 * Copyright (C) 2026, Intel Corporation
 * Authors: Alan Borzeszkowski <alan.borzeszkowski@linux.intel.com>
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#define pr_fmt(fmt) "tbstream: " fmt

#include <linux/configfs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sizes.h>
#include <linux/thunderbolt.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/uuid.h>
#include <linux/wait.h>

/*
 * USB4STREAM - Stream data directly over Thunderbolt/USB4 cable
 *
 * HopIDs are configured by the user. In Linux this is done through
 * ConfigFS. Once that is done paths are be established the first time
 * the stream is opened. Typically the read side is opened first to make
 * sure all the data will be received.
 *
 * End-to-end flow control is mandatory on both sides.
 *
 * Data is sent to the other side as tunneled DATA packets. All the data
 * is owned by the user and passed as-is from the writer to the reader.
 *
 * Once the stream device is closed, a CLOSE packet is sent to the peer
 * so it can take the necessary action. On Linux this typically results
 * in EOF being returned to the reader.
 *
 * Tunneled packet types:
 *
 * +-------+---------+------------------+
 * |  PDF  |  Type   | Payload size     |
 * +-------+---------+------------------+
 * |   2   | DATA    | up to 4 KiB      |
 * |   3   | CLOSE   | up to 256 bytes  |
 * +-------+---------+------------------+
 *
 * Each stream can optionally publish configuration values under its own
 * XDomain property directory. The name of the directory is the name of
 * the stream in question and the UUID is up to the stream. For example
 * if the stream exposes video output then the directory name could be
 * "video".
 *
 * Below values are reserved and can be used by the stream:
 *
 * +----------+-----------+-------------------------+
 * |   Key    |   Type    | Contents                |
 * +----------+-----------+-------------------------+
 * | inhopid  | IMMEDIATE | Configured input HopID  |
 * | outhopid | IMMEDIATE | Configured output HopID |
 * +----------+-----------+-------------------------+
 *
 * It is allowed to add more stream specific properties as well if the
 * above are not enough.
 */

#define TBSTREAM_DEV_RING_SIZE		256
#define TBSTREAM_DEV_MIN_RING_SIZE	32
#define TBSTREAM_DEV_MAX_RING_SIZE	4096
#define TBSTREAM_DEV_THROTTLING		8192
#define TBSTREAM_DEV_MAX_THROTTLING	16776960

/**
 * enum tbstream_frame_pdf - PDF numbers for tunneled frames
 * @TBSTREAM_FRAME_START: PDF of the start of the frame
 * @TBSTREAM_DATA: PDF of the DATA frame
 * @TBSTREAM_CLOSE: PDF of the CLOSE frame
 */
enum tbstream_frame_pdf {
	TBSTREAM_FRAME_START = 1,
	TBSTREAM_DATA,
	TBSTREAM_CLOSE,
};

/**
 * struct tbstream_frame - Frame submitted to/from the rings
 * @sdev: Pointer to the stream device
 * @page: Page holding the packet
 * @offset: Offset inside @page if partial read is done
 * @completed: %true if the RX frame is completed
 * @frame: Underlying frame structure
 */
struct tbstream_frame {
	struct tbstream_dev *sdev;
	struct page *page;
	unsigned int offset;
	bool completed;
	struct ring_frame frame;
};

/**
 * struct tbstream_ring - Stream RX/TX ring structure
 * @ring: Pointer to the API ring
 * @prod: Current value of producer
 * @cons: Current value of consumer
 * @frames: Holds the ring frames
 */
struct tbstream_ring {
	struct tb_ring *ring;
	unsigned long prod;
	unsigned long cons;
	struct tbstream_frame *frames;
};

/**
 * struct tbstream_dev - Stream character device
 * @group: ConfigFS group for this device
 * @stream: Pointer to the stream if it is attached (%NULL otherwise)
 * @misc: Character device used for tunneling
 * @kref: Reference count
 * @index: Unique identifier for the character device
 * @in_hopid: In HopID
 * @out_hopid: Out HopID
 * @ring_size: Size of the rings
 * @throttling: Interrupt throttling rate in ns
 * @users: Number of times @cdev has been opened
 * @closed: CLOSE packet was received
 * @removed: Userspace removed the ConfigFS group underneath.
 * @wait: Waitqueue for open, read and write
 * @lock: Lock protecting this structure
 * @tx_ring: Transmit ring
 * @rx_ring: Receive ring
 * @list: Stream devices are linked through this
 */
struct tbstream_dev {
	struct config_group group;
	struct tbstream *stream;
	struct miscdevice misc;
	struct kref kref;
	int index;
	int in_hopid;
	int out_hopid;
	unsigned int ring_size;
	unsigned int throttling;
	int users;
	bool closed;
	bool removed;
	wait_queue_head_t wait;
	struct mutex lock;
	struct tbstream_ring tx_ring;
	struct tbstream_ring rx_ring;
	struct list_head list;
};

/**
 * struct tbstream_group - Config group for stream
 * @group: ConfigFS group for @stream
 * @stream: Stream the ConfigFS group is attached to. %NULL if there is
 *	    no stream attached.
 * @lock: Lock protecting this structure
 * @dev_list: List of stream devices
 *
 * This is the ConfigFS directory for one connection to another host.
 * There can be several &struct stream_dev linked through @dev_list of
 * this structure. Reference count managed through @group.
 */
struct tbstream_group {
	struct config_group group;
	struct tbstream *stream;
	struct mutex lock;
	struct list_head dev_list;
};

/**
 * struct tbstream - Stream service private data
 * @kref: Reference count
 * @svc: Pointer to the service device
 * @list: Streams are linked through this in @stream_list
 *
 * This represents the actual physical connection between two hosts.
 */
struct tbstream {
	struct kref kref;
	struct tb_service *svc;
	struct list_head list;
};

static DEFINE_IDA(tbstream_indices);

/* Protects tbstream_list */
static DEFINE_MUTEX(tbstream_lock);
static LIST_HEAD(tbstream_list);

/* Serializes tbstream_get()/put() */
static DEFINE_MUTEX(tbstream_kref_lock);

/* Serializes tbstream_dev_get()/put() */
static DEFINE_MUTEX(tbstream_dev_kref_lock);

/* Stream property directory UUID: 3a1cb984-c4d9-4469-a277-ce2fdfd11f0d */
static const uuid_t tbstream_dir_uuid =
	UUID_INIT(0x3a1cb984, 0xc4d9, 0x4469,
		  0xa2, 0x77, 0xce, 0x2f, 0xdf, 0xd1, 0x1f, 0x0d);

static struct tb_property_dir *tbstream_dir;

static void tbstream_release(struct kref *kref)
{
	struct tbstream *stream = container_of(kref, typeof(*stream), kref);

	tb_service_put(stream->svc);
	kfree(stream);
}

static void tbstream_put(struct tbstream *stream)
{
	if (stream) {
		guard(mutex)(&tbstream_kref_lock);
		kref_put(&stream->kref, tbstream_release);
	}
}

static struct tbstream *tbstream_get(struct tbstream *stream)
{
	if (stream) {
		guard(mutex)(&tbstream_kref_lock);
		kref_get(&stream->kref);
	}
	return stream;
}

static inline bool tbstream_valid(const struct tbstream *stream)
{
	if (stream)
		return !tb_service_parent(stream->svc)->is_unplugged;
	return false;
}

static void tbstream_ring_free(struct tbstream_ring *ring)
{
	struct device *dma_dev = tb_ring_dma_device(ring->ring);
	enum dma_data_direction dir;
	int i;

	if (ring->ring->is_tx)
		dir = DMA_TO_DEVICE;
	else
		dir = DMA_FROM_DEVICE;

	for (i = 0; i < tb_ring_size(ring->ring); i++) {
		struct tbstream_frame *sf = &ring->frames[i];

		if (sf->frame.buffer_phy)
			dma_unmap_page(dma_dev, sf->frame.buffer_phy,
				       tb_ring_frame_size(&sf->frame), dir);
		sf->frame.buffer_phy = 0;
		if (sf->page)
			__free_page(sf->page);
		sf->page = NULL;
	}

	ring->prod = 0;
	ring->cons = 0;
	kfree(ring->frames);
}

static inline bool tbstream_ring_available(const struct tbstream_ring *ring)
{
	return ring->prod > ring->cons;
}

static inline struct tb_xdomain *tbstream_dev_xdomain(struct tbstream_dev *sdev)
{
	if (sdev->stream)
		return tb_service_parent(sdev->stream->svc);
	return NULL;
}

static void tbstream_dev_release(struct kref *kref)
{
	struct tbstream_dev *sdev = container_of(kref, struct tbstream_dev, kref);

	if (sdev->stream) {
		struct tb_xdomain *xd = tbstream_dev_xdomain(sdev);

		if (sdev->out_hopid > 0)
			tb_xdomain_release_out_hopid(xd, sdev->out_hopid);
		if (sdev->in_hopid > 0)
			tb_xdomain_release_in_hopid(xd, sdev->in_hopid);

		tbstream_put(sdev->stream);
	}
	ida_free(&tbstream_indices, sdev->index);
	kfree(sdev->misc.name);
	kfree(sdev);
}

static inline void tbstream_dev_put(struct tbstream_dev *sdev)
{
	guard(mutex)(&tbstream_dev_kref_lock);
	kref_put(&sdev->kref, tbstream_dev_release);
}

static inline struct tbstream_dev *tbstream_dev_get(struct tbstream_dev *sdev)
{
	guard(mutex)(&tbstream_dev_kref_lock);
	kref_get(&sdev->kref);
	return sdev;
}

static inline struct tbstream_dev *to_tbstream_dev(struct miscdevice *misc)
{
	return container_of(misc, struct tbstream_dev, misc);
}

static inline int tbstream_dev_valid(const struct tbstream_dev *sdev)
{
	const struct tbstream *stream = sdev->stream;

	if (!tbstream_valid(stream))
		return -ENXIO;
	if (sdev->in_hopid <= 0 || sdev->out_hopid <= 0)
		return -EINVAL;
	return 0;
}

static inline bool tbstream_dev_removed(const struct tbstream_dev *sdev)
{
	return sdev->removed;
}

static inline bool tbstream_dev_closed(const struct tbstream_dev *sdev)
{
	return sdev->closed;
}

static void
tbstream_dev_rx_callback(struct tb_ring *ring, struct ring_frame *frame,
			 bool canceled)
{
	struct tbstream_frame *sf = container_of(frame, typeof(*sf), frame);
	struct tbstream_dev *sdev = sf->sdev;

	if (canceled)
		return;

	sf->completed = true;
	sdev->rx_ring.prod++;

	if (sf->frame.flags & RING_DESC_CRC_ERROR)
		pr_warn("RX CRC error\n");
	else if (sf->frame.flags & RING_DESC_BUFFER_OVERRUN)
		pr_warn("RX buffer overrun\n");
	else
		wake_up_interruptible_poll(&sdev->wait, EPOLLIN | EPOLLRDNORM);
}

static struct tbstream_frame *
tbstream_dev_completed_rx(struct tbstream_dev *sdev)
{
	struct device *dma_dev = tb_ring_dma_device(sdev->rx_ring.ring);
	struct tbstream_frame *sf;
	int index;

	index = sdev->rx_ring.cons % tb_ring_size(sdev->rx_ring.ring);
	sf = &sdev->rx_ring.frames[index];
	if (!sf->completed)
		return NULL;

	dma_sync_single_for_cpu(dma_dev, sf->frame.buffer_phy,
				tb_ring_frame_size(&sf->frame),
				DMA_FROM_DEVICE);
	return sf;
}

static int tbstream_dev_consume_rx(struct tbstream_dev *sdev)
{
	struct device *dma_dev = tb_ring_dma_device(sdev->rx_ring.ring);
	struct tbstream_frame *sf;
	int index;

	index = sdev->rx_ring.cons % tb_ring_size(sdev->rx_ring.ring);
	sdev->rx_ring.cons++;

	sf = &sdev->rx_ring.frames[index];
	sf->completed = false;
	sf->offset = 0;
	sf->frame.size = 0;

	dma_sync_single_for_device(dma_dev, sf->frame.buffer_phy,
				   tb_ring_frame_size(&sf->frame),
				   DMA_FROM_DEVICE);

	return tb_ring_rx(sdev->rx_ring.ring, &sf->frame);
}

static int tbstream_dev_alloc_rx_buffers(struct tbstream_dev *sdev)
{
	size_t ring_size = tb_ring_size(sdev->rx_ring.ring);
	int i;

	sdev->rx_ring.frames = kcalloc(ring_size, sizeof(struct tbstream_frame),
				       GFP_KERNEL);
	if (!sdev->rx_ring.frames)
		return -ENOMEM;

	for (i = 0; i < ring_size; i++) {
		struct device *dma_dev = tb_ring_dma_device(sdev->rx_ring.ring);
		struct tbstream_frame *sf = &sdev->rx_ring.frames[i];
		dma_addr_t dma_addr;

		sf->page = alloc_page(GFP_KERNEL);
		if (!sf->page)
			return -ENOMEM;

		dma_addr = dma_map_page(dma_dev, sf->page, 0, TB_MAX_FRAME_SIZE,
					DMA_FROM_DEVICE);
		if (dma_mapping_error(dma_dev, dma_addr)) {
			__free_page(sf->page);
			sf->page = NULL;
			return -ENOMEM;
		}

		sf->sdev = sdev;
		sf->frame.callback = tbstream_dev_rx_callback;
		sf->frame.buffer_phy = dma_addr;

		tb_ring_rx(sdev->rx_ring.ring, &sf->frame);
	}

	sdev->rx_ring.cons = 0;
	sdev->rx_ring.prod = 0;
	return 0;
}

static void
tbstream_dev_tx_callback(struct tb_ring *ring, struct ring_frame *frame,
			 bool canceled)
{
	struct tbstream_frame *sf = container_of(frame, typeof(*sf), frame);
	struct tbstream_dev *sdev = sf->sdev;

	if (canceled)
		return;

	sdev->tx_ring.prod++;
	if (sf->frame.eof == TBSTREAM_DATA)
		wake_up_interruptible_poll(&sdev->wait, EPOLLOUT | EPOLLWRNORM);
}

static int tbstream_dev_alloc_tx_buffers(struct tbstream_dev *sdev)
{
	struct device *dma_dev = tb_ring_dma_device(sdev->tx_ring.ring);
	size_t ring_size = tb_ring_size(sdev->tx_ring.ring);
	int i;

	sdev->tx_ring.frames = kcalloc(ring_size, sizeof(struct tbstream_frame),
				       GFP_KERNEL);
	if (!sdev->tx_ring.frames)
		return -ENOMEM;

	for (i = 0; i < ring_size; i++) {
		struct tbstream_frame *sf = &sdev->tx_ring.frames[i];
		dma_addr_t dma_addr;

		sf->page = alloc_page(GFP_KERNEL);
		if (!sf->page)
			return -ENOMEM;

		dma_addr = dma_map_page(dma_dev, sf->page, 0, TB_MAX_FRAME_SIZE,
					DMA_TO_DEVICE);
		if (dma_mapping_error(dma_dev, dma_addr)) {
			__free_page(sf->page);
			sf->page = NULL;
			return -ENOMEM;
		}

		sf->sdev = sdev;
		sf->frame.callback = tbstream_dev_tx_callback;
		sf->frame.buffer_phy = dma_addr;
		sf->frame.sof = TBSTREAM_FRAME_START;
	}

	sdev->tx_ring.cons = 0;
	sdev->tx_ring.prod = ring_size - 1;
	return 0;
}

static struct tbstream_frame *
tbstream_dev_alloc_tx(struct tbstream_dev *sdev, enum tbstream_frame_pdf pdf,
		      struct iov_iter *from, size_t size)
{
	struct device *dma_dev = tb_ring_dma_device(sdev->tx_ring.ring);
	struct tbstream_frame *sf;
	int index;

	if (!tbstream_ring_available(&sdev->tx_ring))
		return ERR_PTR(-ENOBUFS);

	index = sdev->tx_ring.cons % tb_ring_size(sdev->tx_ring.ring);
	sdev->tx_ring.cons++;

	sf = &sdev->tx_ring.frames[index];
	sf->frame.size = size < TB_MAX_FRAME_SIZE ? size : 0;
	sf->frame.eof = pdf;

	dma_sync_single_for_cpu(dma_dev, sf->frame.buffer_phy, size,
				DMA_TO_DEVICE);
	if (pdf == TBSTREAM_DATA) {
		if (copy_page_from_iter(sf->page, 0, size, from) != size)
			return ERR_PTR(-EFAULT);
	} else {
		memset(page_address(sf->page), 0, size);
	}
	dma_sync_single_for_device(dma_dev, sf->frame.buffer_phy, size,
				   DMA_TO_DEVICE);
	return sf;
}

static int
tbstream_dev_send_data(struct tbstream_dev *sdev, struct iov_iter *from,
		       size_t size)
{
	struct tbstream_frame *sf;

	sf = tbstream_dev_alloc_tx(sdev, TBSTREAM_DATA, from, size);
	if (IS_ERR(sf))
		return PTR_ERR(sf);
	return tb_ring_tx(sdev->tx_ring.ring, &sf->frame);
}

static int tbstream_dev_send_close(struct tbstream_dev *sdev)
{
	struct tbstream_frame *sf;

	sf = tbstream_dev_alloc_tx(sdev, TBSTREAM_CLOSE, NULL, SZ_256);
	if (IS_ERR(sf))
		return PTR_ERR(sf);
	return tb_ring_tx(sdev->tx_ring.ring, &sf->frame);
}

static int tbstream_dev_start(struct tbstream_dev *sdev)
{
	struct tb_xdomain *xd = tbstream_dev_xdomain(sdev);
	u16 sof_mask, eof_mask;
	struct tb_ring *ring;
	int ret, e2e_tx_hop;

	ring = tb_ring_alloc_tx(xd->tb->nhi, -1, sdev->ring_size,
				RING_FLAG_FRAME | RING_FLAG_E2E);
	if (!ring)
		return -ENOMEM;
	sdev->tx_ring.ring = ring;

	ret = tbstream_dev_alloc_tx_buffers(sdev);
	if (ret)
		goto err_free_tx;

	e2e_tx_hop = ring->hop;
	sof_mask = BIT(TBSTREAM_FRAME_START);
	eof_mask = BIT(TBSTREAM_DATA) | BIT(TBSTREAM_CLOSE);

	ring = tb_ring_alloc_rx(xd->tb->nhi, -1, sdev->ring_size,
				RING_FLAG_FRAME | RING_FLAG_E2E, e2e_tx_hop,
				sof_mask, eof_mask, NULL, NULL);
	if (!ring) {
		ret = -ENOMEM;
		goto err_free_tx_buffers;
	}
	sdev->rx_ring.ring = ring;

	ret = tb_xdomain_enable_paths(xd, sdev->out_hopid,
				     sdev->tx_ring.ring->hop,
				     sdev->in_hopid,
				     sdev->rx_ring.ring->hop);
	if (ret)
		goto err_free_rx;

	tb_ring_throttling(sdev->tx_ring.ring, sdev->throttling);
	tb_ring_throttling(sdev->rx_ring.ring, sdev->throttling);

	tb_ring_start(sdev->tx_ring.ring);
	tb_ring_start(sdev->rx_ring.ring);

	ret = tbstream_dev_alloc_rx_buffers(sdev);
	if (ret)
		goto err_stop;
	return 0;

err_stop:
	tb_ring_stop(sdev->rx_ring.ring);
	tb_ring_stop(sdev->tx_ring.ring);
err_free_rx:
	tb_ring_free(sdev->rx_ring.ring);
err_free_tx_buffers:
	tbstream_ring_free(&sdev->tx_ring);
err_free_tx:
	tb_ring_free(sdev->tx_ring.ring);

	return ret;
}

static void tbstream_dev_stop(struct tbstream_dev *sdev)
{
	struct tb_xdomain *xd;

	/* Wait for the ring to complete any outstanding frames */
	tb_ring_flush(sdev->tx_ring.ring, 500);
	tb_ring_stop(sdev->tx_ring.ring);
	tb_ring_flush(sdev->rx_ring.ring, 500);
	tb_ring_stop(sdev->rx_ring.ring);

	xd = tbstream_dev_xdomain(sdev);
	if (xd) {
		tb_xdomain_disable_paths(xd, sdev->out_hopid,
					 sdev->tx_ring.ring->hop,
					 sdev->in_hopid,
					 sdev->rx_ring.ring->hop);
	}

	tbstream_ring_free(&sdev->rx_ring);
	tb_ring_free(sdev->rx_ring.ring);
	sdev->rx_ring.ring = NULL;
	tbstream_ring_free(&sdev->tx_ring);
	tb_ring_free(sdev->tx_ring.ring);
	sdev->tx_ring.ring = NULL;
}

static ssize_t
tbstream_dev_fops_read_iter(struct kiocb *kiocb, struct iov_iter *to)
{
	struct file *file = kiocb->ki_filp;
	struct tbstream_dev *sdev = to_tbstream_dev(file->private_data);
	size_t nbytes;
	int ret;

	ret = tbstream_dev_valid(sdev);
	if (ret)
		return ret;

	if (mutex_lock_interruptible(&sdev->lock))
		return -ERESTARTSYS;

	while (!tbstream_ring_available(&sdev->rx_ring)) {
		mutex_unlock(&sdev->lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(sdev->wait,
				tbstream_ring_available(&sdev->rx_ring) ||
				tbstream_dev_valid(sdev) != 0 ||
				tbstream_dev_closed(sdev) ||
				tbstream_dev_removed(sdev));
		if (ret)
			return ret;

		ret = tbstream_dev_valid(sdev);
		if (ret)
			return ret;

		if (tbstream_dev_closed(sdev) || tbstream_dev_removed(sdev))
			return 0;

		if (mutex_lock_interruptible(&sdev->lock))
			return -ERESTARTSYS;
	}

	nbytes = 0;
	while (nbytes < iov_iter_count(to)) {
		struct tbstream_frame *sf;
		size_t size, sf_size;

		sf = tbstream_dev_completed_rx(sdev);
		if (!sf)
			break;
		/*
		 * CLOSE tunneled packet. If userspace already read
		 * something then we stop processing now and return
		 * those bytes. Next time the first frame will be CLOSE
		 * in which case we return EOF to the user.
		 */
		if (sf->frame.eof == TBSTREAM_CLOSE) {
			if (!nbytes) {
				tbstream_dev_consume_rx(sdev);
				sdev->closed = true;
			}
			break;
		}

		sf_size = tb_ring_frame_size(&sf->frame);
		size = min(iov_iter_count(to) - nbytes, sf_size);

		if (copy_page_to_iter(sf->page, sf->offset, size, to) != size) {
			ret = -EFAULT;
			break;
		}

		/*
		 * If not all data from the frame is read so leave it in
		 * place and update the offset accordingly so next read
		 * gets the rest.
		 */
		if (size < sf_size) {
			sf->offset += size;
			sf->frame.size = sf_size - size;
		} else {
			ret = tbstream_dev_consume_rx(sdev);
			if (ret)
				break;
		}

		nbytes += size;
	}

	mutex_unlock(&sdev->lock);
	if (ret)
		return ret;
	return nbytes;
}

static ssize_t
tbstream_dev_fops_write_iter(struct kiocb *kiocb, struct iov_iter *from)
{
	struct file *file = kiocb->ki_filp;
	struct tbstream_dev *sdev = to_tbstream_dev(file->private_data);
	size_t nbytes;
	int ret;

	ret = tbstream_dev_valid(sdev);
	if (ret)
		return ret;

	if (mutex_lock_interruptible(&sdev->lock))
		return -ERESTARTSYS;

	while (!tbstream_ring_available(&sdev->tx_ring)) {
		mutex_unlock(&sdev->lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(sdev->wait,
				tbstream_ring_available(&sdev->tx_ring) ||
				tbstream_dev_valid(sdev) != 0 ||
				tbstream_dev_closed(sdev) ||
				tbstream_dev_removed(sdev));
		if (ret)
			return ret;

		ret = tbstream_dev_valid(sdev);
		if (ret)
			return ret;

		if (tbstream_dev_closed(sdev) || tbstream_dev_removed(sdev))
			return -ENXIO;

		if (mutex_lock_interruptible(&sdev->lock))
			return -ERESTARTSYS;
	}

	nbytes = 0;
	while (nbytes < iov_iter_count(from)) {
		size_t size;

		size = min(iov_iter_count(from) - nbytes, TB_MAX_FRAME_SIZE);
		ret = tbstream_dev_send_data(sdev, from, size);
		if (ret) {
			/*
			 * If there are no more buffers we are done for
			 * this write.
			 */
			if (ret == -ENOBUFS)
				ret = 0;
			break;
		}

		nbytes += size;
	}

	mutex_unlock(&sdev->lock);
	if (ret)
		return ret;
	return nbytes;
}

static __poll_t
tbstream_dev_fops_poll(struct file *file, struct poll_table_struct *wait)
{
	struct tbstream_dev *sdev = to_tbstream_dev(file->private_data);
	__poll_t mask = 0;

	poll_wait(file, &sdev->wait, wait);
	guard(mutex)(&sdev->lock);
	if (tbstream_dev_valid(sdev) != 0) {
		mask |= EPOLLHUP | EPOLLERR;
	} else {
		if (tbstream_ring_available(&sdev->tx_ring))
			mask |= EPOLLOUT | EPOLLWRNORM;
		if (tbstream_ring_available(&sdev->rx_ring))
			mask |= EPOLLIN | EPOLLRDNORM;
	}
	return mask;
}

static int tbstream_dev_fops_open(struct inode *inode, struct file *file)
{
	struct tbstream_dev *sdev = to_tbstream_dev(file->private_data);
	int ret;

	tbstream_dev_get(sdev);

	if (mutex_lock_interruptible(&sdev->lock)) {
		tbstream_dev_put(sdev);
		return -ERESTARTSYS;
	}

	/*
	 * If there is no stream attached yet, block until it appears
	 * unless this is opened in non-blocking mode.
	 */
	while ((ret = tbstream_dev_valid(sdev))) {
		mutex_unlock(&sdev->lock);

		if (ret != -ENXIO || (file->f_flags & O_NONBLOCK))
			goto err_put;

		ret = wait_event_interruptible(sdev->wait,
				tbstream_dev_valid(sdev) == 0 ||
				tbstream_dev_removed(sdev));
		if (ret)
			goto err_put;

		if (tbstream_dev_removed(sdev)) {
			ret = -ENXIO;
			goto err_put;
		}

		if (mutex_lock_interruptible(&sdev->lock)) {
			ret = -ERESTARTSYS;
			goto err_put;
		}
	}

	/* Only on first open we allocate rings and enable paths */
	if (!sdev->users++) {
		ret = tbstream_dev_start(sdev);
		if (ret) {
			sdev->users--;
			goto err_unlock;
		}
		sdev->closed = false;
	}

	mutex_unlock(&sdev->lock);
	return 0;

err_unlock:
	mutex_unlock(&sdev->lock);
err_put:
	tbstream_dev_put(sdev);

	return ret;
}

static int tbstream_dev_fops_release(struct inode *inode, struct file *file)
{
	struct tbstream_dev *sdev = to_tbstream_dev(file->private_data);

	mutex_lock(&sdev->lock);
	if (--sdev->users == 0) {
		/*
		 * Send CLOSE tunneled packet to notify the other end
		 * that we are closing the file. We do this twice if the
		 * first one fails.
		 */
		tbstream_dev_send_close(sdev);
		tbstream_dev_stop(sdev);
	}
	mutex_unlock(&sdev->lock);

	tbstream_dev_put(sdev);
	return 0;
}

static const struct file_operations tbstream_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.read_iter = tbstream_dev_fops_read_iter,
	.write_iter = tbstream_dev_fops_write_iter,
	.poll = tbstream_dev_fops_poll,
	.open = tbstream_dev_fops_open,
	.release = tbstream_dev_fops_release,
};

static inline struct tbstream_dev *
tbstream_dev_from_group(struct config_group *group)
{
	return container_of(group, struct tbstream_dev, group);
}

static ssize_t tbstream_dev_index_show(struct config_item *item, char *buf)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	return sysfs_emit(buf, "%d\n", sdev->index);
}
CONFIGFS_ATTR_RO(tbstream_dev_, index);

static ssize_t tbstream_dev_in_hopid_show(struct config_item *item, char *buf)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	return sysfs_emit(buf, "%d\n", sdev->in_hopid);
}

/* svc->lock must be held */
static void service_remove_properties(struct tb_service *svc, const char *name)
{
	struct tb_property *p;

	if (!svc->local_properties)
		return;

	p = tb_property_find(svc->local_properties, name,
			     TB_PROPERTY_TYPE_DIRECTORY);
	if (p) {
		tb_property_free_dir(p->value.dir);
		tb_property_remove(p);

		dev_dbg(&svc->dev, "removed local directory %s\n", name);

		/*
		 * Is the service directory empty already? If it is then
		 * we can release it as well.
		 */
		tb_property_for_each(svc->local_properties, p) {
			if (p->type == TB_PROPERTY_TYPE_DIRECTORY)
				return;
		}

		tb_property_free_dir(svc->local_properties);
		svc->local_properties = NULL;
	}
}

static int service_update_properties(struct tb_service *svc, const char *name,
				     int in_hopid, int out_hopid)
{
	struct tb_property_dir *dir;
	struct tb_property *p;

	guard(mutex)(&svc->lock);

	if (in_hopid < 8 || out_hopid < 8) {
		service_remove_properties(svc, name);
		return 0;
	}

	if (!svc->local_properties) {
		/*
		 * Add the service directory first time we
		 * populate the entries.
		 */
		svc->local_properties = tb_property_copy_dir(tbstream_dir);
		if (!svc->local_properties)
			return -ENOMEM;
	}

	p = tb_property_find(svc->local_properties, name,
			     TB_PROPERTY_TYPE_DIRECTORY);
	if (p) {
		dir = p->value.dir;

		p = tb_property_find(dir, "inhopid", TB_PROPERTY_TYPE_VALUE);
		if (p && p->value.immediate != in_hopid)
			p->value.immediate = in_hopid;
		p = tb_property_find(dir, "outhopid", TB_PROPERTY_TYPE_VALUE);
		if (p && p->value.immediate != out_hopid)
			p->value.immediate = out_hopid;

		dev_dbg(&svc->dev,
			"updated local directory %s: in HopID %d, out HopID %d\n",
			name, in_hopid, out_hopid);
	} else {
		uuid_t uuid;
		int ret;

		uuid_gen(&uuid);
		dir = tb_property_create_dir(&uuid);
		if (!dir)
			return -ENOMEM;

		tb_property_add_immediate(dir, "inhopid", in_hopid);
		tb_property_add_immediate(dir, "outhopid", out_hopid);

		ret = tb_property_add_dir(svc->local_properties, name, dir);
		if (ret) {
			tb_property_free_dir(dir);
			return ret;
		}

		dev_dbg(&svc->dev,
			"added local directory %s: in HopID %d, out HopID %d\n",
			name, in_hopid, out_hopid);
	}

	return 0;
}

static int tbstream_dev_update_properties(struct tbstream_dev *sdev)
{
	struct tbstream *stream;
	int ret;

	stream = tbstream_get(sdev->stream);
	if (!stream)
		return 0;

	ret = service_update_properties(stream->svc,
					config_item_name(&sdev->group.cg_item),
					sdev->in_hopid, sdev->out_hopid);
	if (!ret)
		tb_service_properties_changed(stream->svc);

	tbstream_put(stream);
	return ret;
}

static int tbstream_dev_alloc_in_hopid(struct tbstream_dev *sdev, int hopid)
{
	struct tb_xdomain *xd = tbstream_dev_xdomain(sdev);
	int ret;

	if (sdev->in_hopid > 0 && sdev->in_hopid != hopid)
		tb_xdomain_release_in_hopid(xd, sdev->in_hopid);
	if (!hopid) {
		sdev->in_hopid = hopid;
		return 0;
	}
	ret = tb_xdomain_alloc_in_hopid(xd, hopid);
	if (ret < 0)
		return ret;
	/*
	 * If specific HopID was asked by the user and we did not get
	 * that one then release and return error instead.
	 */
	if (hopid > 0 && hopid != ret) {
		tb_xdomain_release_in_hopid(xd, ret);
		return -EBUSY;
	}
	sdev->in_hopid = ret;
	return 0;
}

static int tbstream_dev_alloc_out_hopid(struct tbstream_dev *sdev, int hopid)
{
	struct tb_xdomain *xd = tbstream_dev_xdomain(sdev);
	int ret;

	if (sdev->out_hopid > 0 && sdev->out_hopid != hopid)
		tb_xdomain_release_out_hopid(xd, sdev->out_hopid);
	if (!hopid) {
		sdev->out_hopid = hopid;
		return 0;
	}
	ret = tb_xdomain_alloc_out_hopid(xd, hopid);
	if (ret < 0)
		return ret;
	if (hopid > 0 && hopid != ret) {
		tb_xdomain_release_out_hopid(xd, ret);
		return -EBUSY;
	}
	sdev->out_hopid = ret;
	return 0;
}

static ssize_t
tbstream_dev_in_hopid_store(struct config_item *item, const char *buf,
			    size_t count)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);
	int ret, in_hopid;

	ret = kstrtoint(buf, 0, &in_hopid);
	if (ret)
		return ret;

	guard(mutex)(&sdev->lock);
	if (sdev->users)
		return -EBUSY;
	if (sdev->stream) {
		ret = tbstream_dev_alloc_in_hopid(sdev, in_hopid);
		if (ret)
			return ret;
		ret = tbstream_dev_update_properties(sdev);
	} else {
		sdev->in_hopid = in_hopid;
	}
	return ret ? ret : count;
}
CONFIGFS_ATTR(tbstream_dev_, in_hopid);

static ssize_t tbstream_dev_out_hopid_show(struct config_item *item, char *buf)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	return sysfs_emit(buf, "%d\n", sdev->out_hopid);
}

static ssize_t
tbstream_dev_out_hopid_store(struct config_item *item, const char *buf,
			     size_t count)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);
	int ret, out_hopid;

	ret = kstrtoint(buf, 0, &out_hopid);
	if (ret)
		return ret;

	guard(mutex)(&sdev->lock);
	if (sdev->users)
		return -EBUSY;
	if (sdev->stream) {
		ret = tbstream_dev_alloc_out_hopid(sdev, out_hopid);
		if (ret)
			return ret;
		ret = tbstream_dev_update_properties(sdev);
	} else {
		sdev->out_hopid = out_hopid;
	}
	return ret ? ret : count;
}
CONFIGFS_ATTR(tbstream_dev_, out_hopid);

static ssize_t tbstream_dev_ring_size_show(struct config_item *item, char *buf)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	return sysfs_emit(buf, "%u\n", sdev->ring_size);
}

static ssize_t
tbstream_dev_ring_size_store(struct config_item *item, const char *buf,
			     size_t count)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);
	unsigned int ring_size;
	int ret;

	ret = kstrtouint(buf, 0, &ring_size);
	if (ret)
		return ret;

	if (ring_size < TBSTREAM_DEV_MIN_RING_SIZE ||
	    ring_size > TBSTREAM_DEV_MAX_RING_SIZE)
		return -EINVAL;

	guard(mutex)(&sdev->lock);
	if (sdev->users)
		return -EBUSY;
	sdev->ring_size = ring_size;
	return count;
}
CONFIGFS_ATTR(tbstream_dev_, ring_size);

static ssize_t tbstream_dev_throttling_show(struct config_item *item, char *buf)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	return sysfs_emit(buf, "%u\n", sdev->throttling);
}

static ssize_t
tbstream_dev_throttling_store(struct config_item *item, const char *buf,
			      size_t count)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);
	unsigned int throttling;
	int ret;

	ret = kstrtouint(buf, 0, &throttling);
	if (ret)
		return ret;

	if (throttling > TBSTREAM_DEV_MAX_THROTTLING)
		return -EINVAL;

	guard(mutex)(&sdev->lock);
	if (sdev->users)
		return -EBUSY;
	sdev->throttling = throttling;
	return count;
}
CONFIGFS_ATTR(tbstream_dev_, throttling);

static struct configfs_attribute *tbstream_dev_attrs[] = {
	&tbstream_dev_attr_index,
	&tbstream_dev_attr_in_hopid,
	&tbstream_dev_attr_out_hopid,
	&tbstream_dev_attr_ring_size,
	&tbstream_dev_attr_throttling,
	NULL,
};

static void tbstream_dev_item_release(struct config_item *item)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(group);

	misc_deregister(&sdev->misc);
	tbstream_dev_put(sdev);
}

static struct configfs_item_operations tbstream_dev_item_ops = {
	.release = tbstream_dev_item_release,
};

static const struct config_item_type tbstream_dev_type = {
	.ct_owner = THIS_MODULE,
	.ct_item_ops = &tbstream_dev_item_ops,
	.ct_attrs = tbstream_dev_attrs,
};

static void service_get_hopids(struct tb_service *svc, const char *name,
			       int *in_hopid, int *out_hopid)
{
	struct tb_property_dir *dir;
	struct tb_property *p;

	guard(mutex)(&svc->lock);

	/* See if we have directory entry with the matching name */
	p = tb_property_find(svc->remote_properties, name,
			     TB_PROPERTY_TYPE_DIRECTORY);
	if (!p)
		return;

	dir = p->value.dir;

	/*
	 * We need to reverse the HopIDs on our end so that in becomes
	 * out and vice versa.
	 */
	p = tb_property_find(dir, "inhopid", TB_PROPERTY_TYPE_VALUE);
	if (p && p->value.immediate >= 8)
		*out_hopid = p->value.immediate;
	p = tb_property_find(dir, "outhopid", TB_PROPERTY_TYPE_VALUE);
	if (p && p->value.immediate >= 8)
		*in_hopid = p->value.immediate;
}

static void
tbstream_dev_attach_stream(struct tbstream_dev *sdev, struct tbstream_group *sg)
{
	const char *name = config_item_name(&sdev->group.cg_item);
	struct tbstream *stream;

	stream = tbstream_get(sg->stream);
	if (!stream)
		return;

	scoped_guard(mutex, &sdev->lock) {
		sdev->stream = stream;
		/*
		 * If there is no existing configuration (or automatic
		 * configuration is being used) check if the other side
		 * has configuration for this and use it.
		 */
		if (sdev->in_hopid <= 0 && sdev->out_hopid <= 0)
			service_get_hopids(stream->svc, name, &sdev->in_hopid,
					   &sdev->out_hopid);
		if (sdev->in_hopid)
			tbstream_dev_alloc_in_hopid(sdev, sdev->in_hopid);
		if (sdev->out_hopid)
			tbstream_dev_alloc_out_hopid(sdev, sdev->out_hopid);
	}

	service_update_properties(stream->svc, name, sdev->in_hopid,
				  sdev->out_hopid);
	tb_service_properties_changed(stream->svc);

	/* Notify any openerers that the stream is now attached */
	wake_up_interruptible(&sdev->wait);
}

static void tbstream_dev_detach_stream(struct tbstream_dev *sdev)
{
	const char *name = config_item_name(&sdev->group.cg_item);
	struct tbstream *stream;
	struct tb_xdomain *xd;

	scoped_guard(mutex, &sdev->lock) {
		stream = sdev->stream;
		if (!stream)
			return;
		sdev->stream = NULL;
		xd = tb_service_parent(stream->svc);
		if (sdev->out_hopid > 0)
			tb_xdomain_release_out_hopid(xd, sdev->out_hopid);
		if (sdev->in_hopid > 0)
			tb_xdomain_release_in_hopid(xd, sdev->in_hopid);
	}

	service_update_properties(stream->svc, name, 0, 0);
	tb_service_properties_changed(stream->svc);

	tbstream_put(stream);

	/* Notify any task that the stream is not valid anymore */
	wake_up_interruptible_poll(&sdev->wait, EPOLLHUP | EPOLLERR);
}

static inline struct tbstream_group *
to_tbstream_group(struct config_group *group)
{
	return container_of(group, struct tbstream_group, group);
}

static struct config_group *
tbstream_dev_make_group(struct config_group *group, const char *name)
{
	struct tbstream_group *sg = to_tbstream_group(group);
	struct tbstream_dev *sdev;
	int ret, index;

	/*
	 * We want the names to be suitable for passing as property
	 * directory names.
	 */
	if (strlen(name) > TB_PROPERTY_KEY_SIZE)
		return ERR_PTR(-ENAMETOOLONG);

	sdev = kzalloc_obj(*sdev, GFP_KERNEL);
	if (!sdev)
		return ERR_PTR(-ENOMEM);

	index = ida_alloc(&tbstream_indices, GFP_KERNEL);
	if (index < 0) {
		kfree(sdev);
		return ERR_PTR(index);
	}

	sdev->index = index;
	sdev->ring_size = TBSTREAM_DEV_RING_SIZE;
	sdev->throttling = TBSTREAM_DEV_THROTTLING;
	mutex_init(&sdev->lock);
	init_waitqueue_head(&sdev->wait);
	INIT_LIST_HEAD(&sdev->list);
	/* This point forward tbstream_dev_put() must be used to release sdev */
	kref_init(&sdev->kref);

	config_group_init_type_name(&sdev->group, name, &tbstream_dev_type);

	scoped_guard(mutex, &sg->lock)
		list_add_tail(&sdev->list, &sg->dev_list);

	tbstream_dev_attach_stream(sdev, sg);

	sdev->misc.name = kasprintf(GFP_KERNEL, "tbstream%d", index);
	sdev->misc.minor = MISC_DYNAMIC_MINOR;
	sdev->misc.fops = &tbstream_dev_fops;

	ret = misc_register(&sdev->misc);
	if (ret) {
		tbstream_dev_detach_stream(sdev);
		scoped_guard(mutex, &sg->lock)
			list_del(&sdev->list);
		/* Calls tbstream_dev_put() */
		config_group_put(&sdev->group);
		return ERR_PTR(ret);
	}

	return &sdev->group;
}

static void
tbstream_dev_drop_item(struct config_group *group, struct config_item *item)
{
	struct config_group *sdev_group = to_config_group(item);
	struct tbstream_dev *sdev = tbstream_dev_from_group(sdev_group);
	struct tbstream_group *sg = to_tbstream_group(group);

	scoped_guard(mutex, &sg->lock)
		list_del(&sdev->list);
	/* Notify any task that the underlying group was removed  */
	sdev->removed = true;
	wake_up_interruptible_poll(&sdev->wait, EPOLLHUP | EPOLLERR);
	config_item_put(item);
}

static struct configfs_group_operations tbstream_dev_group_ops = {
	.make_group = tbstream_dev_make_group,
	.drop_item = tbstream_dev_drop_item,
};

static void tbstream_item_release(struct config_item *item)
{
	struct config_group *group = to_config_group(item);
	struct tbstream_group *sg = to_tbstream_group(group);

	tbstream_put(sg->stream);
	kfree(sg);
}

static struct configfs_item_operations tbstream_item_ops = {
	.release = tbstream_item_release,
};

static const struct config_item_type tbstream_dev_group_type = {
	.ct_owner = THIS_MODULE,
	.ct_group_ops = &tbstream_dev_group_ops,
	.ct_item_ops = &tbstream_item_ops,
};

static struct config_group *
tbstream_make_group(struct config_group *group, const char *name)
{
	struct tbstream_group *sg;
	struct tbstream *stream;
	int domain, index;
	u64 route;

	/* Make sure the format is correct */
	if (sscanf(name, "%u-%llx.%u", &domain, &route, &index) != 3)
		return ERR_PTR(-EINVAL);

	sg = kzalloc_obj(*sg, GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	mutex_init(&sg->lock);
	INIT_LIST_HEAD(&sg->dev_list);

	guard(mutex)(&tbstream_lock);
	list_for_each_entry(stream, &tbstream_list, list) {
		tbstream_get(stream);
		if (sysfs_streq(name, dev_name(&stream->svc->dev))) {
			sg->stream = stream;
			break;
		}
		tbstream_put(stream);
	}

	config_group_init_type_name(&sg->group, name, &tbstream_dev_group_type);
	return &sg->group;
}

static struct configfs_group_operations tbstream_group_ops = {
	.make_group = tbstream_make_group,
};

static const struct config_item_type tbstream_group_type = {
	.ct_owner = THIS_MODULE,
	.ct_group_ops = &tbstream_group_ops,
};

static struct config_group tbstream_group = {
	.cg_item = {
		.ci_namebuf = "stream",
		.ci_type = &tbstream_group_type,
	},
};

/* Returns reference count increased */
static struct tbstream_group *tbstream_group_find(struct tbstream *stream)
{
	const char *name = dev_name(&stream->svc->dev);
	struct config_item *item;

	guard(mutex)(&tbstream_group.cg_subsys->su_mutex);
	item = config_group_find_item(&tbstream_group, name);
	if (!item)
		return NULL;
	return to_tbstream_group(to_config_group(item));
}

static void tbstream_group_attach_stream(struct tbstream *stream)
{
	struct tbstream_group *sg;
	struct tbstream_dev *sdev;

	sg = tbstream_group_find(stream);
	if (!sg)
		return;

	guard(mutex)(&sg->lock);
	if (WARN_ON(sg->stream)) {
		config_group_put(&sg->group);
		return;
	}
	sg->stream = tbstream_get(stream);
	/*
	 * If there are existing stream devices, attach the stream to
	 * them now.
	 */
	list_for_each_entry(sdev, &sg->dev_list, list) {
		tbstream_dev_get(sdev);
		tbstream_dev_attach_stream(sdev, sg);
		tbstream_dev_put(sdev);
	}

	config_group_put(&sg->group);
}

static void tbstream_group_detach_stream(struct tbstream *stream)
{
	struct tbstream_group *sg;
	struct tbstream_dev *sdev;

	sg = tbstream_group_find(stream);
	if (!sg)
		return;

	guard(mutex)(&sg->lock);
	if (sg->stream) {
		/* Detach this stream from the stream devices */
		list_for_each_entry_reverse(sdev, &sg->dev_list, list) {
			tbstream_dev_get(sdev);
			tbstream_dev_detach_stream(sdev);
			tbstream_dev_put(sdev);
		}
		tbstream_put(sg->stream);
		sg->stream = NULL;
	}

	config_group_put(&sg->group);
}

static int tbstream_probe(struct tb_service *svc, const struct tb_service_id *id)
{
	struct tbstream *stream;

	stream = kzalloc_obj(*stream, GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	/* After this point, release stream by calling tbstream_put() */
	kref_init(&stream->kref);
	stream->svc = tb_service_get(svc);
	INIT_LIST_HEAD(&stream->list);

	scoped_guard(mutex, &tbstream_lock)
		list_add_tail(&stream->list, &tbstream_list);

	tbstream_group_attach_stream(stream);
	tb_service_set_drvdata(svc, stream);
	return 0;
}

static void tbstream_remove(struct tb_service *svc)
{
	struct tbstream *stream = tb_service_get_drvdata(svc);

	tbstream_group_detach_stream(stream);
	scoped_guard(mutex, &tbstream_lock)
		list_del(&stream->list);
	tbstream_put(stream);
}

static int __maybe_unused tbstream_suspend(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tbstream *stream = tb_service_get_drvdata(svc);
	struct tbstream_group *sg;
	struct tbstream_dev *sdev;

	sg = tbstream_group_find(stream);
	if (!sg)
		return 0;

	list_for_each_entry_reverse(sdev, &sg->dev_list, list) {
		tbstream_dev_get(sdev);
		/* Stop the stream (if it was open) */
		if (sdev->users)
			tbstream_dev_stop(sdev);
		tbstream_dev_put(sdev);
	}

	config_group_put(&sg->group);
	return 0;
}

static int __maybe_unused tbstream_resume(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tbstream *stream = tb_service_get_drvdata(svc);
	struct tbstream_group *sg;
	struct tbstream_dev *sdev;

	sg = tbstream_group_find(stream);
	if (!sg)
		return 0;

	list_for_each_entry(sdev, &sg->dev_list, list) {
		tbstream_dev_get(sdev);
		if (sdev->users) {
			int ret;

			ret = tbstream_dev_start(sdev);
			if (ret) {
				tbstream_dev_put(sdev);
				config_group_put(&sg->group);
				return ret;
			}
		}
		tbstream_dev_put(sdev);
	}

	config_group_put(&sg->group);
	return 0;
}

static const struct dev_pm_ops tbstream_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tbstream_suspend, tbstream_resume)
};

static const struct tb_service_id tbstream_ids[] = {
	{ TB_SERVICE("stream", 1) },
	{ },
};
MODULE_DEVICE_TABLE(tbsvc, tbstream_ids);

static struct tb_service_driver tbstream_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "thunderbolt_stream",
		.pm = &tbstream_pm_ops,
	},
	.probe = tbstream_probe,
	.remove = tbstream_remove,
	.id_table = tbstream_ids,
};

static int __init tbstream_init(void)
{
	int ret;

	tbstream_dir = tb_property_create_dir(&tbstream_dir_uuid);
	if (!tbstream_dir)
		return -ENOMEM;

	tb_property_add_immediate(tbstream_dir, "prtcid", 1);
	tb_property_add_immediate(tbstream_dir, "prtcvers", 1);
	tb_property_add_immediate(tbstream_dir, "prtcrevs", 0);
	tb_property_add_immediate(tbstream_dir, "prtcstns", 0);

	ret = tb_register_property_dir("stream", tbstream_dir);
	if (ret)
		goto err_free_dir;

	config_group_init(&tbstream_group);
	ret = tb_configfs_register_group(&tbstream_group);
	if (ret)
		goto err_unregister_dir;

	ret = tb_register_service_driver(&tbstream_driver);
	if (ret)
		goto err_unregister_group;
	return 0;

err_unregister_group:
	tb_configfs_unregister_group(&tbstream_group);
err_unregister_dir:
	tb_unregister_property_dir("stream", tbstream_dir);
err_free_dir:
	tb_property_free_dir(tbstream_dir);
	return ret;
}
module_init(tbstream_init);

static void __exit tbstream_exit(void)
{
	tb_unregister_service_driver(&tbstream_driver);
	tb_configfs_unregister_group(&tbstream_group);
	tb_unregister_property_dir("stream", tbstream_dir);
	tb_property_free_dir(tbstream_dir);
	ida_destroy(&tbstream_indices);
}
module_exit(tbstream_exit);

MODULE_AUTHOR("Alan Borzeszkowski <alan.borzeszkowski@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Stream data over Thunderbolt/USB4 cable");
MODULE_LICENSE("GPL");
