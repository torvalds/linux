/*
 * RapidIO mport character device
 *
 * Copyright 2014-2015 Integrated Device Technology, Inc.
 *    Alexandre Bounine <alexandre.bounine@idt.com>
 * Copyright 2014-2015 Prodrive Technologies
 *    Andre van Herk <andre.van.herk@prodrive-technologies.com>
 *    Jerry Jacobs <jerry.jacobs@prodrive-technologies.com>
 * Copyright (C) 2014 Texas Instruments Incorporated
 *    Aurelien Jacquiot <a-jacquiot@ti.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/kfifo.h>

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>

#include <linux/dma-mapping.h>
#ifdef CONFIG_RAPIDIO_DMA_ENGINE
#include <linux/dmaengine.h>
#endif

#include <linux/rio.h>
#include <linux/rio_ids.h>
#include <linux/rio_drv.h>
#include <linux/rio_mport_cdev.h>

#include "../rio.h"

#define DRV_NAME	"rio_mport"
#define DRV_PREFIX	DRV_NAME ": "
#define DEV_NAME	"rio_mport"
#define DRV_VERSION     "1.0.0"

/* Debug output filtering masks */
enum {
	DBG_NONE	= 0,
	DBG_INIT	= BIT(0), /* driver init */
	DBG_EXIT	= BIT(1), /* driver exit */
	DBG_MPORT	= BIT(2), /* mport add/remove */
	DBG_RDEV	= BIT(3), /* RapidIO device add/remove */
	DBG_DMA		= BIT(4), /* DMA transfer messages */
	DBG_MMAP	= BIT(5), /* mapping messages */
	DBG_IBW		= BIT(6), /* inbound window */
	DBG_EVENT	= BIT(7), /* event handling messages */
	DBG_OBW		= BIT(8), /* outbound window messages */
	DBG_DBELL	= BIT(9), /* doorbell messages */
	DBG_ALL		= ~0,
};

#ifdef DEBUG
#define rmcd_debug(level, fmt, arg...)		\
	do {					\
		if (DBG_##level & dbg_level)	\
			pr_debug(DRV_PREFIX "%s: " fmt "\n", __func__, ##arg); \
	} while (0)
#else
#define rmcd_debug(level, fmt, arg...) \
		no_printk(KERN_DEBUG pr_fmt(DRV_PREFIX fmt "\n"), ##arg)
#endif

#define rmcd_warn(fmt, arg...) \
	pr_warn(DRV_PREFIX "%s WARNING " fmt "\n", __func__, ##arg)

#define rmcd_error(fmt, arg...) \
	pr_err(DRV_PREFIX "%s ERROR " fmt "\n", __func__, ##arg)

MODULE_AUTHOR("Jerry Jacobs <jerry.jacobs@prodrive-technologies.com>");
MODULE_AUTHOR("Aurelien Jacquiot <a-jacquiot@ti.com>");
MODULE_AUTHOR("Alexandre Bounine <alexandre.bounine@idt.com>");
MODULE_AUTHOR("Andre van Herk <andre.van.herk@prodrive-technologies.com>");
MODULE_DESCRIPTION("RapidIO mport character device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int dma_timeout = 3000; /* DMA transfer timeout in msec */
module_param(dma_timeout, int, S_IRUGO);
MODULE_PARM_DESC(dma_timeout, "DMA Transfer Timeout in msec (default: 3000)");

#ifdef DEBUG
static u32 dbg_level = DBG_NONE;
module_param(dbg_level, uint, S_IWUSR | S_IWGRP | S_IRUGO);
MODULE_PARM_DESC(dbg_level, "Debugging output level (default 0 = none)");
#endif

/*
 * An internal DMA coherent buffer
 */
struct mport_dma_buf {
	void		*ib_base;
	dma_addr_t	ib_phys;
	u32		ib_size;
	u64		ib_rio_base;
	bool		ib_map;
	struct file	*filp;
};

/*
 * Internal memory mapping structure
 */
enum rio_mport_map_dir {
	MAP_INBOUND,
	MAP_OUTBOUND,
	MAP_DMA,
};

struct rio_mport_mapping {
	struct list_head node;
	struct mport_dev *md;
	enum rio_mport_map_dir dir;
	u16 rioid;
	u64 rio_addr;
	dma_addr_t phys_addr; /* for mmap */
	void *virt_addr; /* kernel address, for dma_free_coherent */
	u64 size;
	struct kref ref; /* refcount of vmas sharing the mapping */
	struct file *filp;
};

struct rio_mport_dma_map {
	int valid;
	u64 length;
	void *vaddr;
	dma_addr_t paddr;
};

#define MPORT_MAX_DMA_BUFS	16
#define MPORT_EVENT_DEPTH	10

/*
 * mport_dev  driver-specific structure that represents mport device
 * @active    mport device status flag
 * @node      list node to maintain list of registered mports
 * @cdev      character device
 * @dev       associated device object
 * @mport     associated subsystem's master port device object
 * @buf_mutex lock for buffer handling
 * @file_mutex - lock for open files list
 * @file_list  - list of open files on given mport
 * @properties properties of this mport
 * @portwrites queue of inbound portwrites
 * @pw_lock    lock for port write queue
 * @mappings   queue for memory mappings
 * @dma_chan   DMA channels associated with this device
 * @dma_ref:
 * @comp:
 */
struct mport_dev {
	atomic_t		active;
	struct list_head	node;
	struct cdev		cdev;
	struct device		dev;
	struct rio_mport	*mport;
	struct mutex		buf_mutex;
	struct mutex		file_mutex;
	struct list_head	file_list;
	struct rio_mport_properties	properties;
	struct list_head		doorbells;
	spinlock_t			db_lock;
	struct list_head		portwrites;
	spinlock_t			pw_lock;
	struct list_head	mappings;
#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	struct dma_chan *dma_chan;
	struct kref	dma_ref;
	struct completion comp;
#endif
};

/*
 * mport_cdev_priv - data structure specific to individual file object
 *                   associated with an open device
 * @md    master port character device object
 * @async_queue - asynchronous notification queue
 * @list - file objects tracking list
 * @db_filters    inbound doorbell filters for this descriptor
 * @pw_filters    portwrite filters for this descriptor
 * @event_fifo    event fifo for this descriptor
 * @event_rx_wait wait queue for this descriptor
 * @fifo_lock     lock for event_fifo
 * @event_mask    event mask for this descriptor
 * @dmach DMA engine channel allocated for specific file object
 */
struct mport_cdev_priv {
	struct mport_dev	*md;
	struct fasync_struct	*async_queue;
	struct list_head	list;
	struct list_head	db_filters;
	struct list_head        pw_filters;
	struct kfifo            event_fifo;
	wait_queue_head_t       event_rx_wait;
	spinlock_t              fifo_lock;
	u32			event_mask; /* RIO_DOORBELL, RIO_PORTWRITE */
#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	struct dma_chan		*dmach;
	struct list_head	async_list;
	spinlock_t              req_lock;
	struct mutex		dma_lock;
	struct kref		dma_ref;
	struct completion	comp;
#endif
};

/*
 * rio_mport_pw_filter - structure to describe a portwrite filter
 * md_node   node in mport device's list
 * priv_node node in private file object's list
 * priv      reference to private data
 * filter    actual portwrite filter
 */
struct rio_mport_pw_filter {
	struct list_head md_node;
	struct list_head priv_node;
	struct mport_cdev_priv *priv;
	struct rio_pw_filter filter;
};

/*
 * rio_mport_db_filter - structure to describe a doorbell filter
 * @data_node reference to device node
 * @priv_node node in private data
 * @priv      reference to private data
 * @filter    actual doorbell filter
 */
struct rio_mport_db_filter {
	struct list_head data_node;
	struct list_head priv_node;
	struct mport_cdev_priv *priv;
	struct rio_doorbell_filter filter;
};

static LIST_HEAD(mport_devs);
static DEFINE_MUTEX(mport_devs_lock);

#if (0) /* used by commented out portion of poll function : FIXME */
static DECLARE_WAIT_QUEUE_HEAD(mport_cdev_wait);
#endif

static struct class *dev_class;
static dev_t dev_number;

static void mport_release_mapping(struct kref *ref);

static int rio_mport_maint_rd(struct mport_cdev_priv *priv, void __user *arg,
			      int local)
{
	struct rio_mport *mport = priv->md->mport;
	struct rio_mport_maint_io maint_io;
	u32 *buffer;
	u32 offset;
	size_t length;
	int ret, i;

	if (unlikely(copy_from_user(&maint_io, arg, sizeof(maint_io))))
		return -EFAULT;

	if ((maint_io.offset % 4) ||
	    (maint_io.length == 0) || (maint_io.length % 4) ||
	    (maint_io.length + maint_io.offset) > RIO_MAINT_SPACE_SZ)
		return -EINVAL;

	buffer = vmalloc(maint_io.length);
	if (buffer == NULL)
		return -ENOMEM;
	length = maint_io.length/sizeof(u32);
	offset = maint_io.offset;

	for (i = 0; i < length; i++) {
		if (local)
			ret = __rio_local_read_config_32(mport,
				offset, &buffer[i]);
		else
			ret = rio_mport_read_config_32(mport, maint_io.rioid,
				maint_io.hopcount, offset, &buffer[i]);
		if (ret)
			goto out;

		offset += 4;
	}

	if (unlikely(copy_to_user((void __user *)(uintptr_t)maint_io.buffer,
				   buffer, maint_io.length)))
		ret = -EFAULT;
out:
	vfree(buffer);
	return ret;
}

static int rio_mport_maint_wr(struct mport_cdev_priv *priv, void __user *arg,
			      int local)
{
	struct rio_mport *mport = priv->md->mport;
	struct rio_mport_maint_io maint_io;
	u32 *buffer;
	u32 offset;
	size_t length;
	int ret = -EINVAL, i;

	if (unlikely(copy_from_user(&maint_io, arg, sizeof(maint_io))))
		return -EFAULT;

	if ((maint_io.offset % 4) ||
	    (maint_io.length == 0) || (maint_io.length % 4) ||
	    (maint_io.length + maint_io.offset) > RIO_MAINT_SPACE_SZ)
		return -EINVAL;

	buffer = vmalloc(maint_io.length);
	if (buffer == NULL)
		return -ENOMEM;
	length = maint_io.length;

	if (unlikely(copy_from_user(buffer,
			(void __user *)(uintptr_t)maint_io.buffer, length))) {
		ret = -EFAULT;
		goto out;
	}

	offset = maint_io.offset;
	length /= sizeof(u32);

	for (i = 0; i < length; i++) {
		if (local)
			ret = __rio_local_write_config_32(mport,
							  offset, buffer[i]);
		else
			ret = rio_mport_write_config_32(mport, maint_io.rioid,
							maint_io.hopcount,
							offset, buffer[i]);
		if (ret)
			goto out;

		offset += 4;
	}

out:
	vfree(buffer);
	return ret;
}


/*
 * Inbound/outbound memory mapping functions
 */
static int
rio_mport_create_outbound_mapping(struct mport_dev *md, struct file *filp,
				  u16 rioid, u64 raddr, u32 size,
				  dma_addr_t *paddr)
{
	struct rio_mport *mport = md->mport;
	struct rio_mport_mapping *map;
	int ret;

	rmcd_debug(OBW, "did=%d ra=0x%llx sz=0x%x", rioid, raddr, size);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	ret = rio_map_outb_region(mport, rioid, raddr, size, 0, paddr);
	if (ret < 0)
		goto err_map_outb;

	map->dir = MAP_OUTBOUND;
	map->rioid = rioid;
	map->rio_addr = raddr;
	map->size = size;
	map->phys_addr = *paddr;
	map->filp = filp;
	map->md = md;
	kref_init(&map->ref);
	list_add_tail(&map->node, &md->mappings);
	return 0;
err_map_outb:
	kfree(map);
	return ret;
}

static int
rio_mport_get_outbound_mapping(struct mport_dev *md, struct file *filp,
			       u16 rioid, u64 raddr, u32 size,
			       dma_addr_t *paddr)
{
	struct rio_mport_mapping *map;
	int err = -ENOMEM;

	mutex_lock(&md->buf_mutex);
	list_for_each_entry(map, &md->mappings, node) {
		if (map->dir != MAP_OUTBOUND)
			continue;
		if (rioid == map->rioid &&
		    raddr == map->rio_addr && size == map->size) {
			*paddr = map->phys_addr;
			err = 0;
			break;
		} else if (rioid == map->rioid &&
			   raddr < (map->rio_addr + map->size - 1) &&
			   (raddr + size) > map->rio_addr) {
			err = -EBUSY;
			break;
		}
	}

	/* If not found, create new */
	if (err == -ENOMEM)
		err = rio_mport_create_outbound_mapping(md, filp, rioid, raddr,
						size, paddr);
	mutex_unlock(&md->buf_mutex);
	return err;
}

static int rio_mport_obw_map(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *data = priv->md;
	struct rio_mmap map;
	dma_addr_t paddr;
	int ret;

	if (unlikely(copy_from_user(&map, arg, sizeof(map))))
		return -EFAULT;

	rmcd_debug(OBW, "did=%d ra=0x%llx sz=0x%llx",
		   map.rioid, map.rio_addr, map.length);

	ret = rio_mport_get_outbound_mapping(data, filp, map.rioid,
					     map.rio_addr, map.length, &paddr);
	if (ret < 0) {
		rmcd_error("Failed to set OBW err= %d", ret);
		return ret;
	}

	map.handle = paddr;

	if (unlikely(copy_to_user(arg, &map, sizeof(map))))
		return -EFAULT;
	return 0;
}

/*
 * rio_mport_obw_free() - unmap an OutBound Window from RapidIO address space
 *
 * @priv: driver private data
 * @arg:  buffer handle returned by allocation routine
 */
static int rio_mport_obw_free(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md = priv->md;
	u64 handle;
	struct rio_mport_mapping *map, *_map;

	if (!md->mport->ops->unmap_outb)
		return -EPROTONOSUPPORT;

	if (copy_from_user(&handle, arg, sizeof(handle)))
		return -EFAULT;

	rmcd_debug(OBW, "h=0x%llx", handle);

	mutex_lock(&md->buf_mutex);
	list_for_each_entry_safe(map, _map, &md->mappings, node) {
		if (map->dir == MAP_OUTBOUND && map->phys_addr == handle) {
			if (map->filp == filp) {
				rmcd_debug(OBW, "kref_put h=0x%llx", handle);
				map->filp = NULL;
				kref_put(&map->ref, mport_release_mapping);
			}
			break;
		}
	}
	mutex_unlock(&md->buf_mutex);

	return 0;
}

/*
 * maint_hdid_set() - Set the host Device ID
 * @priv: driver private data
 * @arg:	Device Id
 */
static int maint_hdid_set(struct mport_cdev_priv *priv, void __user *arg)
{
	struct mport_dev *md = priv->md;
	u16 hdid;

	if (copy_from_user(&hdid, arg, sizeof(hdid)))
		return -EFAULT;

	md->mport->host_deviceid = hdid;
	md->properties.hdid = hdid;
	rio_local_set_device_id(md->mport, hdid);

	rmcd_debug(MPORT, "Set host device Id to %d", hdid);

	return 0;
}

/*
 * maint_comptag_set() - Set the host Component Tag
 * @priv: driver private data
 * @arg:	Component Tag
 */
static int maint_comptag_set(struct mport_cdev_priv *priv, void __user *arg)
{
	struct mport_dev *md = priv->md;
	u32 comptag;

	if (copy_from_user(&comptag, arg, sizeof(comptag)))
		return -EFAULT;

	rio_local_write_config_32(md->mport, RIO_COMPONENT_TAG_CSR, comptag);

	rmcd_debug(MPORT, "Set host Component Tag to %d", comptag);

	return 0;
}

#ifdef CONFIG_RAPIDIO_DMA_ENGINE

struct mport_dma_req {
	struct kref refcount;
	struct list_head node;
	struct file *filp;
	struct mport_cdev_priv *priv;
	enum rio_transfer_sync sync;
	struct sg_table sgt;
	struct page **page_list;
	unsigned int nr_pages;
	struct rio_mport_mapping *map;
	struct dma_chan *dmach;
	enum dma_data_direction dir;
	dma_cookie_t cookie;
	enum dma_status	status;
	struct completion req_comp;
};

static void mport_release_def_dma(struct kref *dma_ref)
{
	struct mport_dev *md =
			container_of(dma_ref, struct mport_dev, dma_ref);

	rmcd_debug(EXIT, "DMA_%d", md->dma_chan->chan_id);
	rio_release_dma(md->dma_chan);
	md->dma_chan = NULL;
}

static void mport_release_dma(struct kref *dma_ref)
{
	struct mport_cdev_priv *priv =
			container_of(dma_ref, struct mport_cdev_priv, dma_ref);

	rmcd_debug(EXIT, "DMA_%d", priv->dmach->chan_id);
	complete(&priv->comp);
}

static void dma_req_free(struct kref *ref)
{
	struct mport_dma_req *req = container_of(ref, struct mport_dma_req,
			refcount);
	struct mport_cdev_priv *priv = req->priv;
	unsigned int i;

	dma_unmap_sg(req->dmach->device->dev,
		     req->sgt.sgl, req->sgt.nents, req->dir);
	sg_free_table(&req->sgt);
	if (req->page_list) {
		for (i = 0; i < req->nr_pages; i++)
			put_page(req->page_list[i]);
		kfree(req->page_list);
	}

	if (req->map) {
		mutex_lock(&req->map->md->buf_mutex);
		kref_put(&req->map->ref, mport_release_mapping);
		mutex_unlock(&req->map->md->buf_mutex);
	}

	kref_put(&priv->dma_ref, mport_release_dma);

	kfree(req);
}

static void dma_xfer_callback(void *param)
{
	struct mport_dma_req *req = (struct mport_dma_req *)param;
	struct mport_cdev_priv *priv = req->priv;

	req->status = dma_async_is_tx_complete(priv->dmach, req->cookie,
					       NULL, NULL);
	complete(&req->req_comp);
	kref_put(&req->refcount, dma_req_free);
}

/*
 * prep_dma_xfer() - Configure and send request to DMAengine to prepare DMA
 *                   transfer object.
 * Returns pointer to DMA transaction descriptor allocated by DMA driver on
 * success or ERR_PTR (and/or NULL) if failed. Caller must check returned
 * non-NULL pointer using IS_ERR macro.
 */
static struct dma_async_tx_descriptor
*prep_dma_xfer(struct dma_chan *chan, struct rio_transfer_io *transfer,
	struct sg_table *sgt, int nents, enum dma_transfer_direction dir,
	enum dma_ctrl_flags flags)
{
	struct rio_dma_data tx_data;

	tx_data.sg = sgt->sgl;
	tx_data.sg_len = nents;
	tx_data.rio_addr_u = 0;
	tx_data.rio_addr = transfer->rio_addr;
	if (dir == DMA_MEM_TO_DEV) {
		switch (transfer->method) {
		case RIO_EXCHANGE_NWRITE:
			tx_data.wr_type = RDW_ALL_NWRITE;
			break;
		case RIO_EXCHANGE_NWRITE_R_ALL:
			tx_data.wr_type = RDW_ALL_NWRITE_R;
			break;
		case RIO_EXCHANGE_NWRITE_R:
			tx_data.wr_type = RDW_LAST_NWRITE_R;
			break;
		case RIO_EXCHANGE_DEFAULT:
			tx_data.wr_type = RDW_DEFAULT;
			break;
		default:
			return ERR_PTR(-EINVAL);
		}
	}

	return rio_dma_prep_xfer(chan, transfer->rioid, &tx_data, dir, flags);
}

/* Request DMA channel associated with this mport device.
 * Try to request DMA channel for every new process that opened given
 * mport. If a new DMA channel is not available use default channel
 * which is the first DMA channel opened on mport device.
 */
static int get_dma_channel(struct mport_cdev_priv *priv)
{
	mutex_lock(&priv->dma_lock);
	if (!priv->dmach) {
		priv->dmach = rio_request_mport_dma(priv->md->mport);
		if (!priv->dmach) {
			/* Use default DMA channel if available */
			if (priv->md->dma_chan) {
				priv->dmach = priv->md->dma_chan;
				kref_get(&priv->md->dma_ref);
			} else {
				rmcd_error("Failed to get DMA channel");
				mutex_unlock(&priv->dma_lock);
				return -ENODEV;
			}
		} else if (!priv->md->dma_chan) {
			/* Register default DMA channel if we do not have one */
			priv->md->dma_chan = priv->dmach;
			kref_init(&priv->md->dma_ref);
			rmcd_debug(DMA, "Register DMA_chan %d as default",
				   priv->dmach->chan_id);
		}

		kref_init(&priv->dma_ref);
		init_completion(&priv->comp);
	}

	kref_get(&priv->dma_ref);
	mutex_unlock(&priv->dma_lock);
	return 0;
}

static void put_dma_channel(struct mport_cdev_priv *priv)
{
	kref_put(&priv->dma_ref, mport_release_dma);
}

/*
 * DMA transfer functions
 */
static int do_dma_request(struct mport_dma_req *req,
			  struct rio_transfer_io *xfer,
			  enum rio_transfer_sync sync, int nents)
{
	struct mport_cdev_priv *priv;
	struct sg_table *sgt;
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;
	unsigned long tmo = msecs_to_jiffies(dma_timeout);
	enum dma_transfer_direction dir;
	long wret;
	int ret = 0;

	priv = req->priv;
	sgt = &req->sgt;

	chan = priv->dmach;
	dir = (req->dir == DMA_FROM_DEVICE) ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;

	rmcd_debug(DMA, "%s(%d) uses %s for DMA_%s",
		   current->comm, task_pid_nr(current),
		   dev_name(&chan->dev->device),
		   (dir == DMA_DEV_TO_MEM)?"READ":"WRITE");

	/* Initialize DMA transaction request */
	tx = prep_dma_xfer(chan, xfer, sgt, nents, dir,
			   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);

	if (!tx) {
		rmcd_debug(DMA, "prep error for %s A:0x%llx L:0x%llx",
			(dir == DMA_DEV_TO_MEM)?"READ":"WRITE",
			xfer->rio_addr, xfer->length);
		ret = -EIO;
		goto err_out;
	} else if (IS_ERR(tx)) {
		ret = PTR_ERR(tx);
		rmcd_debug(DMA, "prep error %d for %s A:0x%llx L:0x%llx", ret,
			(dir == DMA_DEV_TO_MEM)?"READ":"WRITE",
			xfer->rio_addr, xfer->length);
		goto err_out;
	}

	tx->callback = dma_xfer_callback;
	tx->callback_param = req;

	req->status = DMA_IN_PROGRESS;
	kref_get(&req->refcount);

	cookie = dmaengine_submit(tx);
	req->cookie = cookie;

	rmcd_debug(DMA, "pid=%d DMA_%s tx_cookie = %d", task_pid_nr(current),
		   (dir == DMA_DEV_TO_MEM)?"READ":"WRITE", cookie);

	if (dma_submit_error(cookie)) {
		rmcd_error("submit err=%d (addr:0x%llx len:0x%llx)",
			   cookie, xfer->rio_addr, xfer->length);
		kref_put(&req->refcount, dma_req_free);
		ret = -EIO;
		goto err_out;
	}

	dma_async_issue_pending(chan);

	if (sync == RIO_TRANSFER_ASYNC) {
		spin_lock(&priv->req_lock);
		list_add_tail(&req->node, &priv->async_list);
		spin_unlock(&priv->req_lock);
		return cookie;
	} else if (sync == RIO_TRANSFER_FAF)
		return 0;

	wret = wait_for_completion_interruptible_timeout(&req->req_comp, tmo);

	if (wret == 0) {
		/* Timeout on wait occurred */
		rmcd_error("%s(%d) timed out waiting for DMA_%s %d",
		       current->comm, task_pid_nr(current),
		       (dir == DMA_DEV_TO_MEM)?"READ":"WRITE", cookie);
		return -ETIMEDOUT;
	} else if (wret == -ERESTARTSYS) {
		/* Wait_for_completion was interrupted by a signal but DMA may
		 * be in progress
		 */
		rmcd_error("%s(%d) wait for DMA_%s %d was interrupted",
			current->comm, task_pid_nr(current),
			(dir == DMA_DEV_TO_MEM)?"READ":"WRITE", cookie);
		return -EINTR;
	}

	if (req->status != DMA_COMPLETE) {
		/* DMA transaction completion was signaled with error */
		rmcd_error("%s(%d) DMA_%s %d completed with status %d (ret=%d)",
			current->comm, task_pid_nr(current),
			(dir == DMA_DEV_TO_MEM)?"READ":"WRITE",
			cookie, req->status, ret);
		ret = -EIO;
	}

err_out:
	return ret;
}

/*
 * rio_dma_transfer() - Perform RapidIO DMA data transfer to/from
 *                      the remote RapidIO device
 * @filp: file pointer associated with the call
 * @transfer_mode: DMA transfer mode
 * @sync: synchronization mode
 * @dir: DMA transfer direction (DMA_MEM_TO_DEV = write OR
 *                               DMA_DEV_TO_MEM = read)
 * @xfer: data transfer descriptor structure
 */
static int
rio_dma_transfer(struct file *filp, u32 transfer_mode,
		 enum rio_transfer_sync sync, enum dma_data_direction dir,
		 struct rio_transfer_io *xfer)
{
	struct mport_cdev_priv *priv = filp->private_data;
	unsigned long nr_pages = 0;
	struct page **page_list = NULL;
	struct mport_dma_req *req;
	struct mport_dev *md = priv->md;
	struct dma_chan *chan;
	int i, ret;
	int nents;

	if (xfer->length == 0)
		return -EINVAL;
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = get_dma_channel(priv);
	if (ret) {
		kfree(req);
		return ret;
	}
	chan = priv->dmach;

	kref_init(&req->refcount);
	init_completion(&req->req_comp);
	req->dir = dir;
	req->filp = filp;
	req->priv = priv;
	req->dmach = chan;
	req->sync = sync;

	/*
	 * If parameter loc_addr != NULL, we are transferring data from/to
	 * data buffer allocated in user-space: lock in memory user-space
	 * buffer pages and build an SG table for DMA transfer request
	 *
	 * Otherwise (loc_addr == NULL) contiguous kernel-space buffer is
	 * used for DMA data transfers: build single entry SG table using
	 * offset within the internal buffer specified by handle parameter.
	 */
	if (xfer->loc_addr) {
		unsigned int offset;
		long pinned;

		offset = lower_32_bits(offset_in_page(xfer->loc_addr));
		nr_pages = PAGE_ALIGN(xfer->length + offset) >> PAGE_SHIFT;

		page_list = kmalloc_array(nr_pages,
					  sizeof(*page_list), GFP_KERNEL);
		if (page_list == NULL) {
			ret = -ENOMEM;
			goto err_req;
		}

		pinned = get_user_pages_fast(
				(unsigned long)xfer->loc_addr & PAGE_MASK,
				nr_pages,
				dir == DMA_FROM_DEVICE ? FOLL_WRITE : 0,
				page_list);

		if (pinned != nr_pages) {
			if (pinned < 0) {
				rmcd_error("get_user_pages_unlocked err=%ld",
					   pinned);
				nr_pages = 0;
			} else
				rmcd_error("pinned %ld out of %ld pages",
					   pinned, nr_pages);
			ret = -EFAULT;
			goto err_pg;
		}

		ret = sg_alloc_table_from_pages(&req->sgt, page_list, nr_pages,
					offset, xfer->length, GFP_KERNEL);
		if (ret) {
			rmcd_error("sg_alloc_table failed with err=%d", ret);
			goto err_pg;
		}

		req->page_list = page_list;
		req->nr_pages = nr_pages;
	} else {
		dma_addr_t baddr;
		struct rio_mport_mapping *map;

		baddr = (dma_addr_t)xfer->handle;

		mutex_lock(&md->buf_mutex);
		list_for_each_entry(map, &md->mappings, node) {
			if (baddr >= map->phys_addr &&
			    baddr < (map->phys_addr + map->size)) {
				kref_get(&map->ref);
				req->map = map;
				break;
			}
		}
		mutex_unlock(&md->buf_mutex);

		if (req->map == NULL) {
			ret = -ENOMEM;
			goto err_req;
		}

		if (xfer->length + xfer->offset > map->size) {
			ret = -EINVAL;
			goto err_req;
		}

		ret = sg_alloc_table(&req->sgt, 1, GFP_KERNEL);
		if (unlikely(ret)) {
			rmcd_error("sg_alloc_table failed for internal buf");
			goto err_req;
		}

		sg_set_buf(req->sgt.sgl,
			   map->virt_addr + (baddr - map->phys_addr) +
				xfer->offset, xfer->length);
	}

	nents = dma_map_sg(chan->device->dev,
			   req->sgt.sgl, req->sgt.nents, dir);
	if (nents == 0) {
		rmcd_error("Failed to map SG list");
		ret = -EFAULT;
		goto err_pg;
	}

	ret = do_dma_request(req, xfer, sync, nents);

	if (ret >= 0) {
		if (sync == RIO_TRANSFER_ASYNC)
			return ret; /* return ASYNC cookie */
	} else {
		rmcd_debug(DMA, "do_dma_request failed with err=%d", ret);
	}

err_pg:
	if (!req->page_list) {
		for (i = 0; i < nr_pages; i++)
			put_page(page_list[i]);
		kfree(page_list);
	}
err_req:
	kref_put(&req->refcount, dma_req_free);
	return ret;
}

static int rio_mport_transfer_ioctl(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct rio_transaction transaction;
	struct rio_transfer_io *transfer;
	enum dma_data_direction dir;
	int i, ret = 0;

	if (unlikely(copy_from_user(&transaction, arg, sizeof(transaction))))
		return -EFAULT;

	if (transaction.count != 1) /* only single transfer for now */
		return -EINVAL;

	if ((transaction.transfer_mode &
	     priv->md->properties.transfer_mode) == 0)
		return -ENODEV;

	transfer = vmalloc(array_size(sizeof(*transfer), transaction.count));
	if (!transfer)
		return -ENOMEM;

	if (unlikely(copy_from_user(transfer,
				    (void __user *)(uintptr_t)transaction.block,
				    transaction.count * sizeof(*transfer)))) {
		ret = -EFAULT;
		goto out_free;
	}

	dir = (transaction.dir == RIO_TRANSFER_DIR_READ) ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE;
	for (i = 0; i < transaction.count && ret == 0; i++)
		ret = rio_dma_transfer(filp, transaction.transfer_mode,
			transaction.sync, dir, &transfer[i]);

	if (unlikely(copy_to_user((void __user *)(uintptr_t)transaction.block,
				  transfer,
				  transaction.count * sizeof(*transfer))))
		ret = -EFAULT;

out_free:
	vfree(transfer);

	return ret;
}

static int rio_mport_wait_for_async_dma(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv;
	struct rio_async_tx_wait w_param;
	struct mport_dma_req *req;
	dma_cookie_t cookie;
	unsigned long tmo;
	long wret;
	int found = 0;
	int ret;

	priv = (struct mport_cdev_priv *)filp->private_data;

	if (unlikely(copy_from_user(&w_param, arg, sizeof(w_param))))
		return -EFAULT;

	cookie = w_param.token;
	if (w_param.timeout)
		tmo = msecs_to_jiffies(w_param.timeout);
	else /* Use default DMA timeout */
		tmo = msecs_to_jiffies(dma_timeout);

	spin_lock(&priv->req_lock);
	list_for_each_entry(req, &priv->async_list, node) {
		if (req->cookie == cookie) {
			list_del(&req->node);
			found = 1;
			break;
		}
	}
	spin_unlock(&priv->req_lock);

	if (!found)
		return -EAGAIN;

	wret = wait_for_completion_interruptible_timeout(&req->req_comp, tmo);

	if (wret == 0) {
		/* Timeout on wait occurred */
		rmcd_error("%s(%d) timed out waiting for ASYNC DMA_%s",
		       current->comm, task_pid_nr(current),
		       (req->dir == DMA_FROM_DEVICE)?"READ":"WRITE");
		ret = -ETIMEDOUT;
		goto err_tmo;
	} else if (wret == -ERESTARTSYS) {
		/* Wait_for_completion was interrupted by a signal but DMA may
		 * be still in progress
		 */
		rmcd_error("%s(%d) wait for ASYNC DMA_%s was interrupted",
			current->comm, task_pid_nr(current),
			(req->dir == DMA_FROM_DEVICE)?"READ":"WRITE");
		ret = -EINTR;
		goto err_tmo;
	}

	if (req->status != DMA_COMPLETE) {
		/* DMA transaction completion signaled with transfer error */
		rmcd_error("%s(%d) ASYNC DMA_%s completion with status %d",
			current->comm, task_pid_nr(current),
			(req->dir == DMA_FROM_DEVICE)?"READ":"WRITE",
			req->status);
		ret = -EIO;
	} else
		ret = 0;

	if (req->status != DMA_IN_PROGRESS && req->status != DMA_PAUSED)
		kref_put(&req->refcount, dma_req_free);

	return ret;

err_tmo:
	/* Return request back into async queue */
	spin_lock(&priv->req_lock);
	list_add_tail(&req->node, &priv->async_list);
	spin_unlock(&priv->req_lock);
	return ret;
}

static int rio_mport_create_dma_mapping(struct mport_dev *md, struct file *filp,
			u64 size, struct rio_mport_mapping **mapping)
{
	struct rio_mport_mapping *map;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	map->virt_addr = dma_alloc_coherent(md->mport->dev.parent, size,
					    &map->phys_addr, GFP_KERNEL);
	if (map->virt_addr == NULL) {
		kfree(map);
		return -ENOMEM;
	}

	map->dir = MAP_DMA;
	map->size = size;
	map->filp = filp;
	map->md = md;
	kref_init(&map->ref);
	mutex_lock(&md->buf_mutex);
	list_add_tail(&map->node, &md->mappings);
	mutex_unlock(&md->buf_mutex);
	*mapping = map;

	return 0;
}

static int rio_mport_alloc_dma(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md = priv->md;
	struct rio_dma_mem map;
	struct rio_mport_mapping *mapping = NULL;
	int ret;

	if (unlikely(copy_from_user(&map, arg, sizeof(map))))
		return -EFAULT;

	ret = rio_mport_create_dma_mapping(md, filp, map.length, &mapping);
	if (ret)
		return ret;

	map.dma_handle = mapping->phys_addr;

	if (unlikely(copy_to_user(arg, &map, sizeof(map)))) {
		mutex_lock(&md->buf_mutex);
		kref_put(&mapping->ref, mport_release_mapping);
		mutex_unlock(&md->buf_mutex);
		return -EFAULT;
	}

	return 0;
}

static int rio_mport_free_dma(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md = priv->md;
	u64 handle;
	int ret = -EFAULT;
	struct rio_mport_mapping *map, *_map;

	if (copy_from_user(&handle, arg, sizeof(handle)))
		return -EFAULT;
	rmcd_debug(EXIT, "filp=%p", filp);

	mutex_lock(&md->buf_mutex);
	list_for_each_entry_safe(map, _map, &md->mappings, node) {
		if (map->dir == MAP_DMA && map->phys_addr == handle &&
		    map->filp == filp) {
			kref_put(&map->ref, mport_release_mapping);
			ret = 0;
			break;
		}
	}
	mutex_unlock(&md->buf_mutex);

	if (ret == -EFAULT) {
		rmcd_debug(DMA, "ERR no matching mapping");
		return ret;
	}

	return 0;
}
#else
static int rio_mport_transfer_ioctl(struct file *filp, void *arg)
{
	return -ENODEV;
}

static int rio_mport_wait_for_async_dma(struct file *filp, void __user *arg)
{
	return -ENODEV;
}

static int rio_mport_alloc_dma(struct file *filp, void __user *arg)
{
	return -ENODEV;
}

static int rio_mport_free_dma(struct file *filp, void __user *arg)
{
	return -ENODEV;
}
#endif /* CONFIG_RAPIDIO_DMA_ENGINE */

/*
 * Inbound/outbound memory mapping functions
 */

static int
rio_mport_create_inbound_mapping(struct mport_dev *md, struct file *filp,
				u64 raddr, u64 size,
				struct rio_mport_mapping **mapping)
{
	struct rio_mport *mport = md->mport;
	struct rio_mport_mapping *map;
	int ret;

	/* rio_map_inb_region() accepts u32 size */
	if (size > 0xffffffff)
		return -EINVAL;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return -ENOMEM;

	map->virt_addr = dma_alloc_coherent(mport->dev.parent, size,
					    &map->phys_addr, GFP_KERNEL);
	if (map->virt_addr == NULL) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	if (raddr == RIO_MAP_ANY_ADDR)
		raddr = map->phys_addr;
	ret = rio_map_inb_region(mport, map->phys_addr, raddr, (u32)size, 0);
	if (ret < 0)
		goto err_map_inb;

	map->dir = MAP_INBOUND;
	map->rio_addr = raddr;
	map->size = size;
	map->filp = filp;
	map->md = md;
	kref_init(&map->ref);
	mutex_lock(&md->buf_mutex);
	list_add_tail(&map->node, &md->mappings);
	mutex_unlock(&md->buf_mutex);
	*mapping = map;
	return 0;

err_map_inb:
	dma_free_coherent(mport->dev.parent, size,
			  map->virt_addr, map->phys_addr);
err_dma_alloc:
	kfree(map);
	return ret;
}

static int
rio_mport_get_inbound_mapping(struct mport_dev *md, struct file *filp,
			      u64 raddr, u64 size,
			      struct rio_mport_mapping **mapping)
{
	struct rio_mport_mapping *map;
	int err = -ENOMEM;

	if (raddr == RIO_MAP_ANY_ADDR)
		goto get_new;

	mutex_lock(&md->buf_mutex);
	list_for_each_entry(map, &md->mappings, node) {
		if (map->dir != MAP_INBOUND)
			continue;
		if (raddr == map->rio_addr && size == map->size) {
			/* allow exact match only */
			*mapping = map;
			err = 0;
			break;
		} else if (raddr < (map->rio_addr + map->size - 1) &&
			   (raddr + size) > map->rio_addr) {
			err = -EBUSY;
			break;
		}
	}
	mutex_unlock(&md->buf_mutex);

	if (err != -ENOMEM)
		return err;
get_new:
	/* not found, create new */
	return rio_mport_create_inbound_mapping(md, filp, raddr, size, mapping);
}

static int rio_mport_map_inbound(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md = priv->md;
	struct rio_mmap map;
	struct rio_mport_mapping *mapping = NULL;
	int ret;

	if (!md->mport->ops->map_inb)
		return -EPROTONOSUPPORT;
	if (unlikely(copy_from_user(&map, arg, sizeof(map))))
		return -EFAULT;

	rmcd_debug(IBW, "%s filp=%p", dev_name(&priv->md->dev), filp);

	ret = rio_mport_get_inbound_mapping(md, filp, map.rio_addr,
					    map.length, &mapping);
	if (ret)
		return ret;

	map.handle = mapping->phys_addr;
	map.rio_addr = mapping->rio_addr;

	if (unlikely(copy_to_user(arg, &map, sizeof(map)))) {
		/* Delete mapping if it was created by this request */
		if (ret == 0 && mapping->filp == filp) {
			mutex_lock(&md->buf_mutex);
			kref_put(&mapping->ref, mport_release_mapping);
			mutex_unlock(&md->buf_mutex);
		}
		return -EFAULT;
	}

	return 0;
}

/*
 * rio_mport_inbound_free() - unmap from RapidIO address space and free
 *                    previously allocated inbound DMA coherent buffer
 * @priv: driver private data
 * @arg:  buffer handle returned by allocation routine
 */
static int rio_mport_inbound_free(struct file *filp, void __user *arg)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md = priv->md;
	u64 handle;
	struct rio_mport_mapping *map, *_map;

	rmcd_debug(IBW, "%s filp=%p", dev_name(&priv->md->dev), filp);

	if (!md->mport->ops->unmap_inb)
		return -EPROTONOSUPPORT;

	if (copy_from_user(&handle, arg, sizeof(handle)))
		return -EFAULT;

	mutex_lock(&md->buf_mutex);
	list_for_each_entry_safe(map, _map, &md->mappings, node) {
		if (map->dir == MAP_INBOUND && map->phys_addr == handle) {
			if (map->filp == filp) {
				map->filp = NULL;
				kref_put(&map->ref, mport_release_mapping);
			}
			break;
		}
	}
	mutex_unlock(&md->buf_mutex);

	return 0;
}

/*
 * maint_port_idx_get() - Get the port index of the mport instance
 * @priv: driver private data
 * @arg:  port index
 */
static int maint_port_idx_get(struct mport_cdev_priv *priv, void __user *arg)
{
	struct mport_dev *md = priv->md;
	u32 port_idx = md->mport->index;

	rmcd_debug(MPORT, "port_index=%d", port_idx);

	if (copy_to_user(arg, &port_idx, sizeof(port_idx)))
		return -EFAULT;

	return 0;
}

static int rio_mport_add_event(struct mport_cdev_priv *priv,
			       struct rio_event *event)
{
	int overflow;

	if (!(priv->event_mask & event->header))
		return -EACCES;

	spin_lock(&priv->fifo_lock);
	overflow = kfifo_avail(&priv->event_fifo) < sizeof(*event)
		|| kfifo_in(&priv->event_fifo, (unsigned char *)event,
			sizeof(*event)) != sizeof(*event);
	spin_unlock(&priv->fifo_lock);

	wake_up_interruptible(&priv->event_rx_wait);

	if (overflow) {
		dev_warn(&priv->md->dev, DRV_NAME ": event fifo overflow\n");
		return -EBUSY;
	}

	return 0;
}

static void rio_mport_doorbell_handler(struct rio_mport *mport, void *dev_id,
				       u16 src, u16 dst, u16 info)
{
	struct mport_dev *data = dev_id;
	struct mport_cdev_priv *priv;
	struct rio_mport_db_filter *db_filter;
	struct rio_event event;
	int handled;

	event.header = RIO_DOORBELL;
	event.u.doorbell.rioid = src;
	event.u.doorbell.payload = info;

	handled = 0;
	spin_lock(&data->db_lock);
	list_for_each_entry(db_filter, &data->doorbells, data_node) {
		if (((db_filter->filter.rioid == RIO_INVALID_DESTID ||
		      db_filter->filter.rioid == src)) &&
		      info >= db_filter->filter.low &&
		      info <= db_filter->filter.high) {
			priv = db_filter->priv;
			rio_mport_add_event(priv, &event);
			handled = 1;
		}
	}
	spin_unlock(&data->db_lock);

	if (!handled)
		dev_warn(&data->dev,
			"%s: spurious DB received from 0x%x, info=0x%04x\n",
			__func__, src, info);
}

static int rio_mport_add_db_filter(struct mport_cdev_priv *priv,
				   void __user *arg)
{
	struct mport_dev *md = priv->md;
	struct rio_mport_db_filter *db_filter;
	struct rio_doorbell_filter filter;
	unsigned long flags;
	int ret;

	if (copy_from_user(&filter, arg, sizeof(filter)))
		return -EFAULT;

	if (filter.low > filter.high)
		return -EINVAL;

	ret = rio_request_inb_dbell(md->mport, md, filter.low, filter.high,
				    rio_mport_doorbell_handler);
	if (ret) {
		rmcd_error("%s failed to register IBDB, err=%d",
			   dev_name(&md->dev), ret);
		return ret;
	}

	db_filter = kzalloc(sizeof(*db_filter), GFP_KERNEL);
	if (db_filter == NULL) {
		rio_release_inb_dbell(md->mport, filter.low, filter.high);
		return -ENOMEM;
	}

	db_filter->filter = filter;
	db_filter->priv = priv;
	spin_lock_irqsave(&md->db_lock, flags);
	list_add_tail(&db_filter->priv_node, &priv->db_filters);
	list_add_tail(&db_filter->data_node, &md->doorbells);
	spin_unlock_irqrestore(&md->db_lock, flags);

	return 0;
}

static void rio_mport_delete_db_filter(struct rio_mport_db_filter *db_filter)
{
	list_del(&db_filter->data_node);
	list_del(&db_filter->priv_node);
	kfree(db_filter);
}

static int rio_mport_remove_db_filter(struct mport_cdev_priv *priv,
				      void __user *arg)
{
	struct rio_mport_db_filter *db_filter;
	struct rio_doorbell_filter filter;
	unsigned long flags;
	int ret = -EINVAL;

	if (copy_from_user(&filter, arg, sizeof(filter)))
		return -EFAULT;

	if (filter.low > filter.high)
		return -EINVAL;

	spin_lock_irqsave(&priv->md->db_lock, flags);
	list_for_each_entry(db_filter, &priv->db_filters, priv_node) {
		if (db_filter->filter.rioid == filter.rioid &&
		    db_filter->filter.low == filter.low &&
		    db_filter->filter.high == filter.high) {
			rio_mport_delete_db_filter(db_filter);
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&priv->md->db_lock, flags);

	if (!ret)
		rio_release_inb_dbell(priv->md->mport, filter.low, filter.high);

	return ret;
}

static int rio_mport_match_pw(union rio_pw_msg *msg,
			      struct rio_pw_filter *filter)
{
	if ((msg->em.comptag & filter->mask) < filter->low ||
		(msg->em.comptag & filter->mask) > filter->high)
		return 0;
	return 1;
}

static int rio_mport_pw_handler(struct rio_mport *mport, void *context,
				union rio_pw_msg *msg, int step)
{
	struct mport_dev *md = context;
	struct mport_cdev_priv *priv;
	struct rio_mport_pw_filter *pw_filter;
	struct rio_event event;
	int handled;

	event.header = RIO_PORTWRITE;
	memcpy(event.u.portwrite.payload, msg->raw, RIO_PW_MSG_SIZE);

	handled = 0;
	spin_lock(&md->pw_lock);
	list_for_each_entry(pw_filter, &md->portwrites, md_node) {
		if (rio_mport_match_pw(msg, &pw_filter->filter)) {
			priv = pw_filter->priv;
			rio_mport_add_event(priv, &event);
			handled = 1;
		}
	}
	spin_unlock(&md->pw_lock);

	if (!handled) {
		printk_ratelimited(KERN_WARNING DRV_NAME
			": mport%d received spurious PW from 0x%08x\n",
			mport->id, msg->em.comptag);
	}

	return 0;
}

static int rio_mport_add_pw_filter(struct mport_cdev_priv *priv,
				   void __user *arg)
{
	struct mport_dev *md = priv->md;
	struct rio_mport_pw_filter *pw_filter;
	struct rio_pw_filter filter;
	unsigned long flags;
	int hadd = 0;

	if (copy_from_user(&filter, arg, sizeof(filter)))
		return -EFAULT;

	pw_filter = kzalloc(sizeof(*pw_filter), GFP_KERNEL);
	if (pw_filter == NULL)
		return -ENOMEM;

	pw_filter->filter = filter;
	pw_filter->priv = priv;
	spin_lock_irqsave(&md->pw_lock, flags);
	if (list_empty(&md->portwrites))
		hadd = 1;
	list_add_tail(&pw_filter->priv_node, &priv->pw_filters);
	list_add_tail(&pw_filter->md_node, &md->portwrites);
	spin_unlock_irqrestore(&md->pw_lock, flags);

	if (hadd) {
		int ret;

		ret = rio_add_mport_pw_handler(md->mport, md,
					       rio_mport_pw_handler);
		if (ret) {
			dev_err(&md->dev,
				"%s: failed to add IB_PW handler, err=%d\n",
				__func__, ret);
			return ret;
		}
		rio_pw_enable(md->mport, 1);
	}

	return 0;
}

static void rio_mport_delete_pw_filter(struct rio_mport_pw_filter *pw_filter)
{
	list_del(&pw_filter->md_node);
	list_del(&pw_filter->priv_node);
	kfree(pw_filter);
}

static int rio_mport_match_pw_filter(struct rio_pw_filter *a,
				     struct rio_pw_filter *b)
{
	if ((a->mask == b->mask) && (a->low == b->low) && (a->high == b->high))
		return 1;
	return 0;
}

static int rio_mport_remove_pw_filter(struct mport_cdev_priv *priv,
				      void __user *arg)
{
	struct mport_dev *md = priv->md;
	struct rio_mport_pw_filter *pw_filter;
	struct rio_pw_filter filter;
	unsigned long flags;
	int ret = -EINVAL;
	int hdel = 0;

	if (copy_from_user(&filter, arg, sizeof(filter)))
		return -EFAULT;

	spin_lock_irqsave(&md->pw_lock, flags);
	list_for_each_entry(pw_filter, &priv->pw_filters, priv_node) {
		if (rio_mport_match_pw_filter(&pw_filter->filter, &filter)) {
			rio_mport_delete_pw_filter(pw_filter);
			ret = 0;
			break;
		}
	}

	if (list_empty(&md->portwrites))
		hdel = 1;
	spin_unlock_irqrestore(&md->pw_lock, flags);

	if (hdel) {
		rio_del_mport_pw_handler(md->mport, priv->md,
					 rio_mport_pw_handler);
		rio_pw_enable(md->mport, 0);
	}

	return ret;
}

/*
 * rio_release_dev - release routine for kernel RIO device object
 * @dev: kernel device object associated with a RIO device structure
 *
 * Frees a RIO device struct associated a RIO device struct.
 * The RIO device struct is freed.
 */
static void rio_release_dev(struct device *dev)
{
	struct rio_dev *rdev;

	rdev = to_rio_dev(dev);
	pr_info(DRV_PREFIX "%s: %s\n", __func__, rio_name(rdev));
	kfree(rdev);
}


static void rio_release_net(struct device *dev)
{
	struct rio_net *net;

	net = to_rio_net(dev);
	rmcd_debug(RDEV, "net_%d", net->id);
	kfree(net);
}


/*
 * rio_mport_add_riodev - creates a kernel RIO device object
 *
 * Allocates a RIO device data structure and initializes required fields based
 * on device's configuration space contents.
 * If the device has switch capabilities, then a switch specific portion is
 * allocated and configured.
 */
static int rio_mport_add_riodev(struct mport_cdev_priv *priv,
				   void __user *arg)
{
	struct mport_dev *md = priv->md;
	struct rio_rdev_info dev_info;
	struct rio_dev *rdev;
	struct rio_switch *rswitch = NULL;
	struct rio_mport *mport;
	size_t size;
	u32 rval;
	u32 swpinfo = 0;
	u16 destid;
	u8 hopcount;
	int err;

	if (copy_from_user(&dev_info, arg, sizeof(dev_info)))
		return -EFAULT;

	rmcd_debug(RDEV, "name:%s ct:0x%x did:0x%x hc:0x%x", dev_info.name,
		   dev_info.comptag, dev_info.destid, dev_info.hopcount);

	if (bus_find_device_by_name(&rio_bus_type, NULL, dev_info.name)) {
		rmcd_debug(RDEV, "device %s already exists", dev_info.name);
		return -EEXIST;
	}

	size = sizeof(*rdev);
	mport = md->mport;
	destid = dev_info.destid;
	hopcount = dev_info.hopcount;

	if (rio_mport_read_config_32(mport, destid, hopcount,
				     RIO_PEF_CAR, &rval))
		return -EIO;

	if (rval & RIO_PEF_SWITCH) {
		rio_mport_read_config_32(mport, destid, hopcount,
					 RIO_SWP_INFO_CAR, &swpinfo);
		size += (RIO_GET_TOTAL_PORTS(swpinfo) *
			 sizeof(rswitch->nextdev[0])) + sizeof(*rswitch);
	}

	rdev = kzalloc(size, GFP_KERNEL);
	if (rdev == NULL)
		return -ENOMEM;

	if (mport->net == NULL) {
		struct rio_net *net;

		net = rio_alloc_net(mport);
		if (!net) {
			err = -ENOMEM;
			rmcd_debug(RDEV, "failed to allocate net object");
			goto cleanup;
		}

		net->id = mport->id;
		net->hport = mport;
		dev_set_name(&net->dev, "rnet_%d", net->id);
		net->dev.parent = &mport->dev;
		net->dev.release = rio_release_net;
		err = rio_add_net(net);
		if (err) {
			rmcd_debug(RDEV, "failed to register net, err=%d", err);
			kfree(net);
			goto cleanup;
		}
	}

	rdev->net = mport->net;
	rdev->pef = rval;
	rdev->swpinfo = swpinfo;
	rio_mport_read_config_32(mport, destid, hopcount,
				 RIO_DEV_ID_CAR, &rval);
	rdev->did = rval >> 16;
	rdev->vid = rval & 0xffff;
	rio_mport_read_config_32(mport, destid, hopcount, RIO_DEV_INFO_CAR,
				 &rdev->device_rev);
	rio_mport_read_config_32(mport, destid, hopcount, RIO_ASM_ID_CAR,
				 &rval);
	rdev->asm_did = rval >> 16;
	rdev->asm_vid = rval & 0xffff;
	rio_mport_read_config_32(mport, destid, hopcount, RIO_ASM_INFO_CAR,
				 &rval);
	rdev->asm_rev = rval >> 16;

	if (rdev->pef & RIO_PEF_EXT_FEATURES) {
		rdev->efptr = rval & 0xffff;
		rdev->phys_efptr = rio_mport_get_physefb(mport, 0, destid,
						hopcount, &rdev->phys_rmap);

		rdev->em_efptr = rio_mport_get_feature(mport, 0, destid,
						hopcount, RIO_EFB_ERR_MGMNT);
	}

	rio_mport_read_config_32(mport, destid, hopcount, RIO_SRC_OPS_CAR,
				 &rdev->src_ops);
	rio_mport_read_config_32(mport, destid, hopcount, RIO_DST_OPS_CAR,
				 &rdev->dst_ops);

	rdev->comp_tag = dev_info.comptag;
	rdev->destid = destid;
	/* hopcount is stored as specified by a caller, regardles of EP or SW */
	rdev->hopcount = hopcount;

	if (rdev->pef & RIO_PEF_SWITCH) {
		rswitch = rdev->rswitch;
		rswitch->route_table = NULL;
	}

	if (strlen(dev_info.name))
		dev_set_name(&rdev->dev, "%s", dev_info.name);
	else if (rdev->pef & RIO_PEF_SWITCH)
		dev_set_name(&rdev->dev, "%02x:s:%04x", mport->id,
			     rdev->comp_tag & RIO_CTAG_UDEVID);
	else
		dev_set_name(&rdev->dev, "%02x:e:%04x", mport->id,
			     rdev->comp_tag & RIO_CTAG_UDEVID);

	INIT_LIST_HEAD(&rdev->net_list);
	rdev->dev.parent = &mport->net->dev;
	rio_attach_device(rdev);
	rdev->dev.release = rio_release_dev;

	if (rdev->dst_ops & RIO_DST_OPS_DOORBELL)
		rio_init_dbell_res(&rdev->riores[RIO_DOORBELL_RESOURCE],
				   0, 0xffff);
	err = rio_add_device(rdev);
	if (err)
		goto cleanup;
	rio_dev_get(rdev);

	return 0;
cleanup:
	kfree(rdev);
	return err;
}

static int rio_mport_del_riodev(struct mport_cdev_priv *priv, void __user *arg)
{
	struct rio_rdev_info dev_info;
	struct rio_dev *rdev = NULL;
	struct device  *dev;
	struct rio_mport *mport;
	struct rio_net *net;

	if (copy_from_user(&dev_info, arg, sizeof(dev_info)))
		return -EFAULT;

	mport = priv->md->mport;

	/* If device name is specified, removal by name has priority */
	if (strlen(dev_info.name)) {
		dev = bus_find_device_by_name(&rio_bus_type, NULL,
					      dev_info.name);
		if (dev)
			rdev = to_rio_dev(dev);
	} else {
		do {
			rdev = rio_get_comptag(dev_info.comptag, rdev);
			if (rdev && rdev->dev.parent == &mport->net->dev &&
			    rdev->destid == dev_info.destid &&
			    rdev->hopcount == dev_info.hopcount)
				break;
		} while (rdev);
	}

	if (!rdev) {
		rmcd_debug(RDEV,
			"device name:%s ct:0x%x did:0x%x hc:0x%x not found",
			dev_info.name, dev_info.comptag, dev_info.destid,
			dev_info.hopcount);
		return -ENODEV;
	}

	net = rdev->net;
	rio_dev_put(rdev);
	rio_del_device(rdev, RIO_DEVICE_SHUTDOWN);

	if (list_empty(&net->devices)) {
		rio_free_net(net);
		mport->net = NULL;
	}

	return 0;
}

/*
 * Mport cdev management
 */

/*
 * mport_cdev_open() - Open character device (mport)
 */
static int mport_cdev_open(struct inode *inode, struct file *filp)
{
	int ret;
	int minor = iminor(inode);
	struct mport_dev *chdev;
	struct mport_cdev_priv *priv;

	/* Test for valid device */
	if (minor >= RIO_MAX_MPORTS) {
		rmcd_error("Invalid minor device number");
		return -EINVAL;
	}

	chdev = container_of(inode->i_cdev, struct mport_dev, cdev);

	rmcd_debug(INIT, "%s filp=%p", dev_name(&chdev->dev), filp);

	if (atomic_read(&chdev->active) == 0)
		return -ENODEV;

	get_device(&chdev->dev);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		put_device(&chdev->dev);
		return -ENOMEM;
	}

	priv->md = chdev;

	mutex_lock(&chdev->file_mutex);
	list_add_tail(&priv->list, &chdev->file_list);
	mutex_unlock(&chdev->file_mutex);

	INIT_LIST_HEAD(&priv->db_filters);
	INIT_LIST_HEAD(&priv->pw_filters);
	spin_lock_init(&priv->fifo_lock);
	init_waitqueue_head(&priv->event_rx_wait);
	ret = kfifo_alloc(&priv->event_fifo,
			  sizeof(struct rio_event) * MPORT_EVENT_DEPTH,
			  GFP_KERNEL);
	if (ret < 0) {
		dev_err(&chdev->dev, DRV_NAME ": kfifo_alloc failed\n");
		ret = -ENOMEM;
		goto err_fifo;
	}

#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	INIT_LIST_HEAD(&priv->async_list);
	spin_lock_init(&priv->req_lock);
	mutex_init(&priv->dma_lock);
#endif

	filp->private_data = priv;
	goto out;
err_fifo:
	kfree(priv);
out:
	return ret;
}

static int mport_cdev_fasync(int fd, struct file *filp, int mode)
{
	struct mport_cdev_priv *priv = filp->private_data;

	return fasync_helper(fd, filp, mode, &priv->async_queue);
}

#ifdef CONFIG_RAPIDIO_DMA_ENGINE
static void mport_cdev_release_dma(struct file *filp)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md;
	struct mport_dma_req *req, *req_next;
	unsigned long tmo = msecs_to_jiffies(dma_timeout);
	long wret;
	LIST_HEAD(list);

	rmcd_debug(EXIT, "from filp=%p %s(%d)",
		   filp, current->comm, task_pid_nr(current));

	if (!priv->dmach) {
		rmcd_debug(EXIT, "No DMA channel for filp=%p", filp);
		return;
	}

	md = priv->md;

	spin_lock(&priv->req_lock);
	if (!list_empty(&priv->async_list)) {
		rmcd_debug(EXIT, "async list not empty filp=%p %s(%d)",
			   filp, current->comm, task_pid_nr(current));
		list_splice_init(&priv->async_list, &list);
	}
	spin_unlock(&priv->req_lock);

	if (!list_empty(&list)) {
		rmcd_debug(EXIT, "temp list not empty");
		list_for_each_entry_safe(req, req_next, &list, node) {
			rmcd_debug(EXIT, "free req->filp=%p cookie=%d compl=%s",
				   req->filp, req->cookie,
				   completion_done(&req->req_comp)?"yes":"no");
			list_del(&req->node);
			kref_put(&req->refcount, dma_req_free);
		}
	}

	put_dma_channel(priv);
	wret = wait_for_completion_interruptible_timeout(&priv->comp, tmo);

	if (wret <= 0) {
		rmcd_error("%s(%d) failed waiting for DMA release err=%ld",
			current->comm, task_pid_nr(current), wret);
	}

	if (priv->dmach != priv->md->dma_chan) {
		rmcd_debug(EXIT, "Release DMA channel for filp=%p %s(%d)",
			   filp, current->comm, task_pid_nr(current));
		rio_release_dma(priv->dmach);
	} else {
		rmcd_debug(EXIT, "Adjust default DMA channel refcount");
		kref_put(&md->dma_ref, mport_release_def_dma);
	}

	priv->dmach = NULL;
}
#else
#define mport_cdev_release_dma(priv) do {} while (0)
#endif

/*
 * mport_cdev_release() - Release character device
 */
static int mport_cdev_release(struct inode *inode, struct file *filp)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *chdev;
	struct rio_mport_pw_filter *pw_filter, *pw_filter_next;
	struct rio_mport_db_filter *db_filter, *db_filter_next;
	struct rio_mport_mapping *map, *_map;
	unsigned long flags;

	rmcd_debug(EXIT, "%s filp=%p", dev_name(&priv->md->dev), filp);

	chdev = priv->md;
	mport_cdev_release_dma(filp);

	priv->event_mask = 0;

	spin_lock_irqsave(&chdev->pw_lock, flags);
	if (!list_empty(&priv->pw_filters)) {
		list_for_each_entry_safe(pw_filter, pw_filter_next,
					 &priv->pw_filters, priv_node)
			rio_mport_delete_pw_filter(pw_filter);
	}
	spin_unlock_irqrestore(&chdev->pw_lock, flags);

	spin_lock_irqsave(&chdev->db_lock, flags);
	list_for_each_entry_safe(db_filter, db_filter_next,
				 &priv->db_filters, priv_node) {
		rio_mport_delete_db_filter(db_filter);
	}
	spin_unlock_irqrestore(&chdev->db_lock, flags);

	kfifo_free(&priv->event_fifo);

	mutex_lock(&chdev->buf_mutex);
	list_for_each_entry_safe(map, _map, &chdev->mappings, node) {
		if (map->filp == filp) {
			rmcd_debug(EXIT, "release mapping %p filp=%p",
				   map->virt_addr, filp);
			kref_put(&map->ref, mport_release_mapping);
		}
	}
	mutex_unlock(&chdev->buf_mutex);

	mport_cdev_fasync(-1, filp, 0);
	filp->private_data = NULL;
	mutex_lock(&chdev->file_mutex);
	list_del(&priv->list);
	mutex_unlock(&chdev->file_mutex);
	put_device(&chdev->dev);
	kfree(priv);
	return 0;
}

/*
 * mport_cdev_ioctl() - IOCTLs for character device
 */
static long mport_cdev_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	int err = -EINVAL;
	struct mport_cdev_priv *data = filp->private_data;
	struct mport_dev *md = data->md;

	if (atomic_read(&md->active) == 0)
		return -ENODEV;

	switch (cmd) {
	case RIO_MPORT_MAINT_READ_LOCAL:
		return rio_mport_maint_rd(data, (void __user *)arg, 1);
	case RIO_MPORT_MAINT_WRITE_LOCAL:
		return rio_mport_maint_wr(data, (void __user *)arg, 1);
	case RIO_MPORT_MAINT_READ_REMOTE:
		return rio_mport_maint_rd(data, (void __user *)arg, 0);
	case RIO_MPORT_MAINT_WRITE_REMOTE:
		return rio_mport_maint_wr(data, (void __user *)arg, 0);
	case RIO_MPORT_MAINT_HDID_SET:
		return maint_hdid_set(data, (void __user *)arg);
	case RIO_MPORT_MAINT_COMPTAG_SET:
		return maint_comptag_set(data, (void __user *)arg);
	case RIO_MPORT_MAINT_PORT_IDX_GET:
		return maint_port_idx_get(data, (void __user *)arg);
	case RIO_MPORT_GET_PROPERTIES:
		md->properties.hdid = md->mport->host_deviceid;
		if (copy_to_user((void __user *)arg, &(md->properties),
				 sizeof(md->properties)))
			return -EFAULT;
		return 0;
	case RIO_ENABLE_DOORBELL_RANGE:
		return rio_mport_add_db_filter(data, (void __user *)arg);
	case RIO_DISABLE_DOORBELL_RANGE:
		return rio_mport_remove_db_filter(data, (void __user *)arg);
	case RIO_ENABLE_PORTWRITE_RANGE:
		return rio_mport_add_pw_filter(data, (void __user *)arg);
	case RIO_DISABLE_PORTWRITE_RANGE:
		return rio_mport_remove_pw_filter(data, (void __user *)arg);
	case RIO_SET_EVENT_MASK:
		data->event_mask = (u32)arg;
		return 0;
	case RIO_GET_EVENT_MASK:
		if (copy_to_user((void __user *)arg, &data->event_mask,
				    sizeof(u32)))
			return -EFAULT;
		return 0;
	case RIO_MAP_OUTBOUND:
		return rio_mport_obw_map(filp, (void __user *)arg);
	case RIO_MAP_INBOUND:
		return rio_mport_map_inbound(filp, (void __user *)arg);
	case RIO_UNMAP_OUTBOUND:
		return rio_mport_obw_free(filp, (void __user *)arg);
	case RIO_UNMAP_INBOUND:
		return rio_mport_inbound_free(filp, (void __user *)arg);
	case RIO_ALLOC_DMA:
		return rio_mport_alloc_dma(filp, (void __user *)arg);
	case RIO_FREE_DMA:
		return rio_mport_free_dma(filp, (void __user *)arg);
	case RIO_WAIT_FOR_ASYNC:
		return rio_mport_wait_for_async_dma(filp, (void __user *)arg);
	case RIO_TRANSFER:
		return rio_mport_transfer_ioctl(filp, (void __user *)arg);
	case RIO_DEV_ADD:
		return rio_mport_add_riodev(data, (void __user *)arg);
	case RIO_DEV_DEL:
		return rio_mport_del_riodev(data, (void __user *)arg);
	default:
		break;
	}

	return err;
}

/*
 * mport_release_mapping - free mapping resources and info structure
 * @ref: a pointer to the kref within struct rio_mport_mapping
 *
 * NOTE: Shall be called while holding buf_mutex.
 */
static void mport_release_mapping(struct kref *ref)
{
	struct rio_mport_mapping *map =
			container_of(ref, struct rio_mport_mapping, ref);
	struct rio_mport *mport = map->md->mport;

	rmcd_debug(MMAP, "type %d mapping @ %p (phys = %pad) for %s",
		   map->dir, map->virt_addr,
		   &map->phys_addr, mport->name);

	list_del(&map->node);

	switch (map->dir) {
	case MAP_INBOUND:
		rio_unmap_inb_region(mport, map->phys_addr);
		/* fall through */
	case MAP_DMA:
		dma_free_coherent(mport->dev.parent, map->size,
				  map->virt_addr, map->phys_addr);
		break;
	case MAP_OUTBOUND:
		rio_unmap_outb_region(mport, map->rioid, map->rio_addr);
		break;
	}
	kfree(map);
}

static void mport_mm_open(struct vm_area_struct *vma)
{
	struct rio_mport_mapping *map = vma->vm_private_data;

	rmcd_debug(MMAP, "%pad", &map->phys_addr);
	kref_get(&map->ref);
}

static void mport_mm_close(struct vm_area_struct *vma)
{
	struct rio_mport_mapping *map = vma->vm_private_data;

	rmcd_debug(MMAP, "%pad", &map->phys_addr);
	mutex_lock(&map->md->buf_mutex);
	kref_put(&map->ref, mport_release_mapping);
	mutex_unlock(&map->md->buf_mutex);
}

static const struct vm_operations_struct vm_ops = {
	.open =	mport_mm_open,
	.close = mport_mm_close,
};

static int mport_cdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct mport_dev *md;
	size_t size = vma->vm_end - vma->vm_start;
	dma_addr_t baddr;
	unsigned long offset;
	int found = 0, ret;
	struct rio_mport_mapping *map;

	rmcd_debug(MMAP, "0x%x bytes at offset 0x%lx",
		   (unsigned int)size, vma->vm_pgoff);

	md = priv->md;
	baddr = ((dma_addr_t)vma->vm_pgoff << PAGE_SHIFT);

	mutex_lock(&md->buf_mutex);
	list_for_each_entry(map, &md->mappings, node) {
		if (baddr >= map->phys_addr &&
		    baddr < (map->phys_addr + map->size)) {
			found = 1;
			break;
		}
	}
	mutex_unlock(&md->buf_mutex);

	if (!found)
		return -ENOMEM;

	offset = baddr - map->phys_addr;

	if (size + offset > map->size)
		return -EINVAL;

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	rmcd_debug(MMAP, "MMAP adjusted offset = 0x%lx", vma->vm_pgoff);

	if (map->dir == MAP_INBOUND || map->dir == MAP_DMA)
		ret = dma_mmap_coherent(md->mport->dev.parent, vma,
				map->virt_addr, map->phys_addr, map->size);
	else if (map->dir == MAP_OUTBOUND) {
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		ret = vm_iomap_memory(vma, map->phys_addr, map->size);
	} else {
		rmcd_error("Attempt to mmap unsupported mapping type");
		ret = -EIO;
	}

	if (!ret) {
		vma->vm_private_data = map;
		vma->vm_ops = &vm_ops;
		mport_mm_open(vma);
	} else {
		rmcd_error("MMAP exit with err=%d", ret);
	}

	return ret;
}

static __poll_t mport_cdev_poll(struct file *filp, poll_table *wait)
{
	struct mport_cdev_priv *priv = filp->private_data;

	poll_wait(filp, &priv->event_rx_wait, wait);
	if (kfifo_len(&priv->event_fifo))
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

static ssize_t mport_read(struct file *filp, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct mport_cdev_priv *priv = filp->private_data;
	int copied;
	ssize_t ret;

	if (!count)
		return 0;

	if (kfifo_is_empty(&priv->event_fifo) &&
	    (filp->f_flags & O_NONBLOCK))
		return -EAGAIN;

	if (count % sizeof(struct rio_event))
		return -EINVAL;

	ret = wait_event_interruptible(priv->event_rx_wait,
					kfifo_len(&priv->event_fifo) != 0);
	if (ret)
		return ret;

	while (ret < count) {
		if (kfifo_to_user(&priv->event_fifo, buf,
		      sizeof(struct rio_event), &copied))
			return -EFAULT;
		ret += copied;
		buf += copied;
	}

	return ret;
}

static ssize_t mport_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct mport_cdev_priv *priv = filp->private_data;
	struct rio_mport *mport = priv->md->mport;
	struct rio_event event;
	int len, ret;

	if (!count)
		return 0;

	if (count % sizeof(event))
		return -EINVAL;

	len = 0;
	while ((count - len) >= (int)sizeof(event)) {
		if (copy_from_user(&event, buf, sizeof(event)))
			return -EFAULT;

		if (event.header != RIO_DOORBELL)
			return -EINVAL;

		ret = rio_mport_send_doorbell(mport,
					      event.u.doorbell.rioid,
					      event.u.doorbell.payload);
		if (ret < 0)
			return ret;

		len += sizeof(event);
		buf += sizeof(event);
	}

	return len;
}

static const struct file_operations mport_fops = {
	.owner		= THIS_MODULE,
	.open		= mport_cdev_open,
	.release	= mport_cdev_release,
	.poll		= mport_cdev_poll,
	.read		= mport_read,
	.write		= mport_write,
	.mmap		= mport_cdev_mmap,
	.fasync		= mport_cdev_fasync,
	.unlocked_ioctl = mport_cdev_ioctl
};

/*
 * Character device management
 */

static void mport_device_release(struct device *dev)
{
	struct mport_dev *md;

	rmcd_debug(EXIT, "%s", dev_name(dev));
	md = container_of(dev, struct mport_dev, dev);
	kfree(md);
}

/*
 * mport_cdev_add() - Create mport_dev from rio_mport
 * @mport:	RapidIO master port
 */
static struct mport_dev *mport_cdev_add(struct rio_mport *mport)
{
	int ret = 0;
	struct mport_dev *md;
	struct rio_mport_attr attr;

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (!md) {
		rmcd_error("Unable allocate a device object");
		return NULL;
	}

	md->mport = mport;
	mutex_init(&md->buf_mutex);
	mutex_init(&md->file_mutex);
	INIT_LIST_HEAD(&md->file_list);

	device_initialize(&md->dev);
	md->dev.devt = MKDEV(MAJOR(dev_number), mport->id);
	md->dev.class = dev_class;
	md->dev.parent = &mport->dev;
	md->dev.release = mport_device_release;
	dev_set_name(&md->dev, DEV_NAME "%d", mport->id);
	atomic_set(&md->active, 1);

	cdev_init(&md->cdev, &mport_fops);
	md->cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&md->cdev, &md->dev);
	if (ret) {
		rmcd_error("Failed to register mport %d (err=%d)",
		       mport->id, ret);
		goto err_cdev;
	}

	INIT_LIST_HEAD(&md->doorbells);
	spin_lock_init(&md->db_lock);
	INIT_LIST_HEAD(&md->portwrites);
	spin_lock_init(&md->pw_lock);
	INIT_LIST_HEAD(&md->mappings);

	md->properties.id = mport->id;
	md->properties.sys_size = mport->sys_size;
	md->properties.hdid = mport->host_deviceid;
	md->properties.index = mport->index;

	/* The transfer_mode property will be returned through mport query
	 * interface
	 */
#ifdef CONFIG_FSL_RIO /* for now: only on Freescale's SoCs */
	md->properties.transfer_mode |= RIO_TRANSFER_MODE_MAPPED;
#else
	md->properties.transfer_mode |= RIO_TRANSFER_MODE_TRANSFER;
#endif
	ret = rio_query_mport(mport, &attr);
	if (!ret) {
		md->properties.flags = attr.flags;
		md->properties.link_speed = attr.link_speed;
		md->properties.link_width = attr.link_width;
		md->properties.dma_max_sge = attr.dma_max_sge;
		md->properties.dma_max_size = attr.dma_max_size;
		md->properties.dma_align = attr.dma_align;
		md->properties.cap_sys_size = 0;
		md->properties.cap_transfer_mode = 0;
		md->properties.cap_addr_size = 0;
	} else
		pr_info(DRV_PREFIX "Failed to obtain info for %s cdev(%d:%d)\n",
			mport->name, MAJOR(dev_number), mport->id);

	mutex_lock(&mport_devs_lock);
	list_add_tail(&md->node, &mport_devs);
	mutex_unlock(&mport_devs_lock);

	pr_info(DRV_PREFIX "Added %s cdev(%d:%d)\n",
		mport->name, MAJOR(dev_number), mport->id);

	return md;

err_cdev:
	put_device(&md->dev);
	return NULL;
}

/*
 * mport_cdev_terminate_dma() - Stop all active DMA data transfers and release
 *                              associated DMA channels.
 */
static void mport_cdev_terminate_dma(struct mport_dev *md)
{
#ifdef CONFIG_RAPIDIO_DMA_ENGINE
	struct mport_cdev_priv *client;

	rmcd_debug(DMA, "%s", dev_name(&md->dev));

	mutex_lock(&md->file_mutex);
	list_for_each_entry(client, &md->file_list, list) {
		if (client->dmach) {
			dmaengine_terminate_all(client->dmach);
			rio_release_dma(client->dmach);
		}
	}
	mutex_unlock(&md->file_mutex);

	if (md->dma_chan) {
		dmaengine_terminate_all(md->dma_chan);
		rio_release_dma(md->dma_chan);
		md->dma_chan = NULL;
	}
#endif
}


/*
 * mport_cdev_kill_fasync() - Send SIGIO signal to all processes with open
 *                            mport_cdev files.
 */
static int mport_cdev_kill_fasync(struct mport_dev *md)
{
	unsigned int files = 0;
	struct mport_cdev_priv *client;

	mutex_lock(&md->file_mutex);
	list_for_each_entry(client, &md->file_list, list) {
		if (client->async_queue)
			kill_fasync(&client->async_queue, SIGIO, POLL_HUP);
		files++;
	}
	mutex_unlock(&md->file_mutex);
	return files;
}

/*
 * mport_cdev_remove() - Remove mport character device
 * @dev:	Mport device to remove
 */
static void mport_cdev_remove(struct mport_dev *md)
{
	struct rio_mport_mapping *map, *_map;

	rmcd_debug(EXIT, "Remove %s cdev", md->mport->name);
	atomic_set(&md->active, 0);
	mport_cdev_terminate_dma(md);
	rio_del_mport_pw_handler(md->mport, md, rio_mport_pw_handler);
	cdev_device_del(&md->cdev, &md->dev);
	mport_cdev_kill_fasync(md);

	/* TODO: do we need to give clients some time to close file
	 * descriptors? Simple wait for XX, or kref?
	 */

	/*
	 * Release DMA buffers allocated for the mport device.
	 * Disable associated inbound Rapidio requests mapping if applicable.
	 */
	mutex_lock(&md->buf_mutex);
	list_for_each_entry_safe(map, _map, &md->mappings, node) {
		kref_put(&map->ref, mport_release_mapping);
	}
	mutex_unlock(&md->buf_mutex);

	if (!list_empty(&md->mappings))
		rmcd_warn("WARNING: %s pending mappings on removal",
			  md->mport->name);

	rio_release_inb_dbell(md->mport, 0, 0x0fff);

	put_device(&md->dev);
}

/*
 * RIO rio_mport_interface driver
 */

/*
 * mport_add_mport() - Add rio_mport from LDM device struct
 * @dev:		Linux device model struct
 * @class_intf:	Linux class_interface
 */
static int mport_add_mport(struct device *dev,
		struct class_interface *class_intf)
{
	struct rio_mport *mport = NULL;
	struct mport_dev *chdev = NULL;

	mport = to_rio_mport(dev);
	if (!mport)
		return -ENODEV;

	chdev = mport_cdev_add(mport);
	if (!chdev)
		return -ENODEV;

	return 0;
}

/*
 * mport_remove_mport() - Remove rio_mport from global list
 * TODO remove device from global mport_dev list
 */
static void mport_remove_mport(struct device *dev,
		struct class_interface *class_intf)
{
	struct rio_mport *mport = NULL;
	struct mport_dev *chdev;
	int found = 0;

	mport = to_rio_mport(dev);
	rmcd_debug(EXIT, "Remove %s", mport->name);

	mutex_lock(&mport_devs_lock);
	list_for_each_entry(chdev, &mport_devs, node) {
		if (chdev->mport->id == mport->id) {
			atomic_set(&chdev->active, 0);
			list_del(&chdev->node);
			found = 1;
			break;
		}
	}
	mutex_unlock(&mport_devs_lock);

	if (found)
		mport_cdev_remove(chdev);
}

/* the rio_mport_interface is used to handle local mport devices */
static struct class_interface rio_mport_interface __refdata = {
	.class		= &rio_mport_class,
	.add_dev	= mport_add_mport,
	.remove_dev	= mport_remove_mport,
};

/*
 * Linux kernel module
 */

/*
 * mport_init - Driver module loading
 */
static int __init mport_init(void)
{
	int ret;

	/* Create device class needed by udev */
	dev_class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(dev_class)) {
		rmcd_error("Unable to create " DRV_NAME " class");
		return PTR_ERR(dev_class);
	}

	ret = alloc_chrdev_region(&dev_number, 0, RIO_MAX_MPORTS, DRV_NAME);
	if (ret < 0)
		goto err_chr;

	rmcd_debug(INIT, "Registered class with major=%d", MAJOR(dev_number));

	/* Register to rio_mport_interface */
	ret = class_interface_register(&rio_mport_interface);
	if (ret) {
		rmcd_error("class_interface_register() failed, err=%d", ret);
		goto err_cli;
	}

	return 0;

err_cli:
	unregister_chrdev_region(dev_number, RIO_MAX_MPORTS);
err_chr:
	class_destroy(dev_class);
	return ret;
}

/**
 * mport_exit - Driver module unloading
 */
static void __exit mport_exit(void)
{
	class_interface_unregister(&rio_mport_interface);
	class_destroy(dev_class);
	unregister_chrdev_region(dev_number, RIO_MAX_MPORTS);
}

module_init(mport_init);
module_exit(mport_exit);
