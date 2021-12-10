// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>

#include <xen/platform_pci.h>
#include <xen/xen.h>
#include <xen/xenbus.h>

#include <xen/xen-front-pgdir-shbuf.h>
#include <xen/interface/io/displif.h>

#include "xen_drm_front.h"
#include "xen_drm_front_cfg.h"
#include "xen_drm_front_evtchnl.h"
#include "xen_drm_front_gem.h"
#include "xen_drm_front_kms.h"

struct xen_drm_front_dbuf {
	struct list_head list;
	u64 dbuf_cookie;
	u64 fb_cookie;

	struct xen_front_pgdir_shbuf shbuf;
};

static void dbuf_add_to_list(struct xen_drm_front_info *front_info,
			     struct xen_drm_front_dbuf *dbuf, u64 dbuf_cookie)
{
	dbuf->dbuf_cookie = dbuf_cookie;
	list_add(&dbuf->list, &front_info->dbuf_list);
}

static struct xen_drm_front_dbuf *dbuf_get(struct list_head *dbuf_list,
					   u64 dbuf_cookie)
{
	struct xen_drm_front_dbuf *buf, *q;

	list_for_each_entry_safe(buf, q, dbuf_list, list)
		if (buf->dbuf_cookie == dbuf_cookie)
			return buf;

	return NULL;
}

static void dbuf_free(struct list_head *dbuf_list, u64 dbuf_cookie)
{
	struct xen_drm_front_dbuf *buf, *q;

	list_for_each_entry_safe(buf, q, dbuf_list, list)
		if (buf->dbuf_cookie == dbuf_cookie) {
			list_del(&buf->list);
			xen_front_pgdir_shbuf_unmap(&buf->shbuf);
			xen_front_pgdir_shbuf_free(&buf->shbuf);
			kfree(buf);
			break;
		}
}

static void dbuf_free_all(struct list_head *dbuf_list)
{
	struct xen_drm_front_dbuf *buf, *q;

	list_for_each_entry_safe(buf, q, dbuf_list, list) {
		list_del(&buf->list);
		xen_front_pgdir_shbuf_unmap(&buf->shbuf);
		xen_front_pgdir_shbuf_free(&buf->shbuf);
		kfree(buf);
	}
}

static struct xendispl_req *
be_prepare_req(struct xen_drm_front_evtchnl *evtchnl, u8 operation)
{
	struct xendispl_req *req;

	req = RING_GET_REQUEST(&evtchnl->u.req.ring,
			       evtchnl->u.req.ring.req_prod_pvt);
	req->operation = operation;
	req->id = evtchnl->evt_next_id++;
	evtchnl->evt_id = req->id;
	return req;
}

static int be_stream_do_io(struct xen_drm_front_evtchnl *evtchnl,
			   struct xendispl_req *req)
{
	reinit_completion(&evtchnl->u.req.completion);
	if (unlikely(evtchnl->state != EVTCHNL_STATE_CONNECTED))
		return -EIO;

	xen_drm_front_evtchnl_flush(evtchnl);
	return 0;
}

static int be_stream_wait_io(struct xen_drm_front_evtchnl *evtchnl)
{
	if (wait_for_completion_timeout(&evtchnl->u.req.completion,
			msecs_to_jiffies(XEN_DRM_FRONT_WAIT_BACK_MS)) <= 0)
		return -ETIMEDOUT;

	return evtchnl->u.req.resp_status;
}

int xen_drm_front_mode_set(struct xen_drm_front_drm_pipeline *pipeline,
			   u32 x, u32 y, u32 width, u32 height,
			   u32 bpp, u64 fb_cookie)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xen_drm_front_info *front_info;
	struct xendispl_req *req;
	unsigned long flags;
	int ret;

	front_info = pipeline->drm_info->front_info;
	evtchnl = &front_info->evt_pairs[pipeline->index].req;
	if (unlikely(!evtchnl))
		return -EIO;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_SET_CONFIG);
	req->op.set_config.x = x;
	req->op.set_config.y = y;
	req->op.set_config.width = width;
	req->op.set_config.height = height;
	req->op.set_config.bpp = bpp;
	req->op.set_config.fb_cookie = fb_cookie;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_drm_front_dbuf_create(struct xen_drm_front_info *front_info,
			      u64 dbuf_cookie, u32 width, u32 height,
			      u32 bpp, u64 size, u32 offset,
			      struct page **pages)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xen_drm_front_dbuf *dbuf;
	struct xendispl_req *req;
	struct xen_front_pgdir_shbuf_cfg buf_cfg;
	unsigned long flags;
	int ret;

	evtchnl = &front_info->evt_pairs[GENERIC_OP_EVT_CHNL].req;
	if (unlikely(!evtchnl))
		return -EIO;

	dbuf = kzalloc(sizeof(*dbuf), GFP_KERNEL);
	if (!dbuf)
		return -ENOMEM;

	dbuf_add_to_list(front_info, dbuf, dbuf_cookie);

	memset(&buf_cfg, 0, sizeof(buf_cfg));
	buf_cfg.xb_dev = front_info->xb_dev;
	buf_cfg.num_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	buf_cfg.pages = pages;
	buf_cfg.pgdir = &dbuf->shbuf;
	buf_cfg.be_alloc = front_info->cfg.be_alloc;

	ret = xen_front_pgdir_shbuf_alloc(&buf_cfg);
	if (ret < 0)
		goto fail_shbuf_alloc;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_DBUF_CREATE);
	req->op.dbuf_create.gref_directory =
			xen_front_pgdir_shbuf_get_dir_start(&dbuf->shbuf);
	req->op.dbuf_create.buffer_sz = size;
	req->op.dbuf_create.data_ofs = offset;
	req->op.dbuf_create.dbuf_cookie = dbuf_cookie;
	req->op.dbuf_create.width = width;
	req->op.dbuf_create.height = height;
	req->op.dbuf_create.bpp = bpp;
	if (buf_cfg.be_alloc)
		req->op.dbuf_create.flags |= XENDISPL_DBUF_FLG_REQ_ALLOC;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret < 0)
		goto fail;

	ret = be_stream_wait_io(evtchnl);
	if (ret < 0)
		goto fail;

	ret = xen_front_pgdir_shbuf_map(&dbuf->shbuf);
	if (ret < 0)
		goto fail;

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return 0;

fail:
	mutex_unlock(&evtchnl->u.req.req_io_lock);
fail_shbuf_alloc:
	dbuf_free(&front_info->dbuf_list, dbuf_cookie);
	return ret;
}

static int xen_drm_front_dbuf_destroy(struct xen_drm_front_info *front_info,
				      u64 dbuf_cookie)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xendispl_req *req;
	unsigned long flags;
	bool be_alloc;
	int ret;

	evtchnl = &front_info->evt_pairs[GENERIC_OP_EVT_CHNL].req;
	if (unlikely(!evtchnl))
		return -EIO;

	be_alloc = front_info->cfg.be_alloc;

	/*
	 * For the backend allocated buffer release references now, so backend
	 * can free the buffer.
	 */
	if (be_alloc)
		dbuf_free(&front_info->dbuf_list, dbuf_cookie);

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_DBUF_DESTROY);
	req->op.dbuf_destroy.dbuf_cookie = dbuf_cookie;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	/*
	 * Do this regardless of communication status with the backend:
	 * if we cannot remove remote resources remove what we can locally.
	 */
	if (!be_alloc)
		dbuf_free(&front_info->dbuf_list, dbuf_cookie);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_drm_front_fb_attach(struct xen_drm_front_info *front_info,
			    u64 dbuf_cookie, u64 fb_cookie, u32 width,
			    u32 height, u32 pixel_format)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xen_drm_front_dbuf *buf;
	struct xendispl_req *req;
	unsigned long flags;
	int ret;

	evtchnl = &front_info->evt_pairs[GENERIC_OP_EVT_CHNL].req;
	if (unlikely(!evtchnl))
		return -EIO;

	buf = dbuf_get(&front_info->dbuf_list, dbuf_cookie);
	if (!buf)
		return -EINVAL;

	buf->fb_cookie = fb_cookie;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_FB_ATTACH);
	req->op.fb_attach.dbuf_cookie = dbuf_cookie;
	req->op.fb_attach.fb_cookie = fb_cookie;
	req->op.fb_attach.width = width;
	req->op.fb_attach.height = height;
	req->op.fb_attach.pixel_format = pixel_format;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_drm_front_fb_detach(struct xen_drm_front_info *front_info,
			    u64 fb_cookie)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xendispl_req *req;
	unsigned long flags;
	int ret;

	evtchnl = &front_info->evt_pairs[GENERIC_OP_EVT_CHNL].req;
	if (unlikely(!evtchnl))
		return -EIO;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_FB_DETACH);
	req->op.fb_detach.fb_cookie = fb_cookie;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_drm_front_page_flip(struct xen_drm_front_info *front_info,
			    int conn_idx, u64 fb_cookie)
{
	struct xen_drm_front_evtchnl *evtchnl;
	struct xendispl_req *req;
	unsigned long flags;
	int ret;

	if (unlikely(conn_idx >= front_info->num_evt_pairs))
		return -EINVAL;

	evtchnl = &front_info->evt_pairs[conn_idx].req;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	spin_lock_irqsave(&front_info->io_lock, flags);
	req = be_prepare_req(evtchnl, XENDISPL_OP_PG_FLIP);
	req->op.pg_flip.fb_cookie = fb_cookie;

	ret = be_stream_do_io(evtchnl, req);
	spin_unlock_irqrestore(&front_info->io_lock, flags);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

void xen_drm_front_on_frame_done(struct xen_drm_front_info *front_info,
				 int conn_idx, u64 fb_cookie)
{
	struct xen_drm_front_drm_info *drm_info = front_info->drm_info;

	if (unlikely(conn_idx >= front_info->cfg.num_connectors))
		return;

	xen_drm_front_kms_on_frame_done(&drm_info->pipeline[conn_idx],
					fb_cookie);
}

void xen_drm_front_gem_object_free(struct drm_gem_object *obj)
{
	struct xen_drm_front_drm_info *drm_info = obj->dev->dev_private;
	int idx;

	if (drm_dev_enter(obj->dev, &idx)) {
		xen_drm_front_dbuf_destroy(drm_info->front_info,
					   xen_drm_front_dbuf_to_cookie(obj));
		drm_dev_exit(idx);
	} else {
		dbuf_free(&drm_info->front_info->dbuf_list,
			  xen_drm_front_dbuf_to_cookie(obj));
	}

	xen_drm_front_gem_free_object_unlocked(obj);
}

static int xen_drm_drv_dumb_create(struct drm_file *filp,
				   struct drm_device *dev,
				   struct drm_mode_create_dumb *args)
{
	struct xen_drm_front_drm_info *drm_info = dev->dev_private;
	struct drm_gem_object *obj;
	int ret;

	/*
	 * Dumb creation is a two stage process: first we create a fully
	 * constructed GEM object which is communicated to the backend, and
	 * only after that we can create GEM's handle. This is done so,
	 * because of the possible races: once you create a handle it becomes
	 * immediately visible to user-space, so the latter can try accessing
	 * object without pages etc.
	 * For details also see drm_gem_handle_create
	 */
	args->pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	args->size = args->pitch * args->height;

	obj = xen_drm_front_gem_create(dev, args->size);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail;
	}

	ret = xen_drm_front_dbuf_create(drm_info->front_info,
					xen_drm_front_dbuf_to_cookie(obj),
					args->width, args->height, args->bpp,
					args->size, 0,
					xen_drm_front_gem_get_pages(obj));
	if (ret)
		goto fail_backend;

	/* This is the tail of GEM object creation */
	ret = drm_gem_handle_create(filp, obj, &args->handle);
	if (ret)
		goto fail_handle;

	/* Drop reference from allocate - handle holds it now */
	drm_gem_object_put(obj);
	return 0;

fail_handle:
	xen_drm_front_dbuf_destroy(drm_info->front_info,
				   xen_drm_front_dbuf_to_cookie(obj));
fail_backend:
	/* drop reference from allocate */
	drm_gem_object_put(obj);
fail:
	DRM_ERROR("Failed to create dumb buffer: %d\n", ret);
	return ret;
}

static void xen_drm_drv_release(struct drm_device *dev)
{
	struct xen_drm_front_drm_info *drm_info = dev->dev_private;
	struct xen_drm_front_info *front_info = drm_info->front_info;

	xen_drm_front_kms_fini(drm_info);

	drm_atomic_helper_shutdown(dev);
	drm_mode_config_cleanup(dev);

	if (front_info->cfg.be_alloc)
		xenbus_switch_state(front_info->xb_dev,
				    XenbusStateInitialising);

	kfree(drm_info);
}

static const struct file_operations xen_drm_dev_fops = {
	.owner          = THIS_MODULE,
	.open           = drm_open,
	.release        = drm_release,
	.unlocked_ioctl = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = drm_compat_ioctl,
#endif
	.poll           = drm_poll,
	.read           = drm_read,
	.llseek         = no_llseek,
	.mmap           = xen_drm_front_gem_mmap,
};

static const struct drm_driver xen_drm_driver = {
	.driver_features           = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.release                   = xen_drm_drv_release,
	.prime_handle_to_fd        = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle        = drm_gem_prime_fd_to_handle,
	.gem_prime_import_sg_table = xen_drm_front_gem_import_sg_table,
	.gem_prime_mmap            = xen_drm_front_gem_prime_mmap,
	.dumb_create               = xen_drm_drv_dumb_create,
	.fops                      = &xen_drm_dev_fops,
	.name                      = "xendrm-du",
	.desc                      = "Xen PV DRM Display Unit",
	.date                      = "20180221",
	.major                     = 1,
	.minor                     = 0,

};

static int xen_drm_drv_init(struct xen_drm_front_info *front_info)
{
	struct device *dev = &front_info->xb_dev->dev;
	struct xen_drm_front_drm_info *drm_info;
	struct drm_device *drm_dev;
	int ret;

	DRM_INFO("Creating %s\n", xen_drm_driver.desc);

	drm_info = kzalloc(sizeof(*drm_info), GFP_KERNEL);
	if (!drm_info) {
		ret = -ENOMEM;
		goto fail;
	}

	drm_info->front_info = front_info;
	front_info->drm_info = drm_info;

	drm_dev = drm_dev_alloc(&xen_drm_driver, dev);
	if (IS_ERR(drm_dev)) {
		ret = PTR_ERR(drm_dev);
		goto fail_dev;
	}

	drm_info->drm_dev = drm_dev;

	drm_dev->dev_private = drm_info;

	ret = xen_drm_front_kms_init(drm_info);
	if (ret) {
		DRM_ERROR("Failed to initialize DRM/KMS, ret %d\n", ret);
		goto fail_modeset;
	}

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto fail_register;

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 xen_drm_driver.name, xen_drm_driver.major,
		 xen_drm_driver.minor, xen_drm_driver.patchlevel,
		 xen_drm_driver.date, drm_dev->primary->index);

	return 0;

fail_register:
	drm_dev_unregister(drm_dev);
fail_modeset:
	drm_kms_helper_poll_fini(drm_dev);
	drm_mode_config_cleanup(drm_dev);
	drm_dev_put(drm_dev);
fail_dev:
	kfree(drm_info);
	front_info->drm_info = NULL;
fail:
	return ret;
}

static void xen_drm_drv_fini(struct xen_drm_front_info *front_info)
{
	struct xen_drm_front_drm_info *drm_info = front_info->drm_info;
	struct drm_device *dev;

	if (!drm_info)
		return;

	dev = drm_info->drm_dev;
	if (!dev)
		return;

	/* Nothing to do if device is already unplugged */
	if (drm_dev_is_unplugged(dev))
		return;

	drm_kms_helper_poll_fini(dev);
	drm_dev_unplug(dev);
	drm_dev_put(dev);

	front_info->drm_info = NULL;

	xen_drm_front_evtchnl_free_all(front_info);
	dbuf_free_all(&front_info->dbuf_list);

	/*
	 * If we are not using backend allocated buffers, then tell the
	 * backend we are ready to (re)initialize. Otherwise, wait for
	 * drm_driver.release.
	 */
	if (!front_info->cfg.be_alloc)
		xenbus_switch_state(front_info->xb_dev,
				    XenbusStateInitialising);
}

static int displback_initwait(struct xen_drm_front_info *front_info)
{
	struct xen_drm_front_cfg *cfg = &front_info->cfg;
	int ret;

	cfg->front_info = front_info;
	ret = xen_drm_front_cfg_card(front_info, cfg);
	if (ret < 0)
		return ret;

	DRM_INFO("Have %d connector(s)\n", cfg->num_connectors);
	/* Create event channels for all connectors and publish */
	ret = xen_drm_front_evtchnl_create_all(front_info);
	if (ret < 0)
		return ret;

	return xen_drm_front_evtchnl_publish_all(front_info);
}

static int displback_connect(struct xen_drm_front_info *front_info)
{
	xen_drm_front_evtchnl_set_state(front_info, EVTCHNL_STATE_CONNECTED);
	return xen_drm_drv_init(front_info);
}

static void displback_disconnect(struct xen_drm_front_info *front_info)
{
	if (!front_info->drm_info)
		return;

	/* Tell the backend to wait until we release the DRM driver. */
	xenbus_switch_state(front_info->xb_dev, XenbusStateReconfiguring);

	xen_drm_drv_fini(front_info);
}

static void displback_changed(struct xenbus_device *xb_dev,
			      enum xenbus_state backend_state)
{
	struct xen_drm_front_info *front_info = dev_get_drvdata(&xb_dev->dev);
	int ret;

	DRM_DEBUG("Backend state is %s, front is %s\n",
		  xenbus_strstate(backend_state),
		  xenbus_strstate(xb_dev->state));

	switch (backend_state) {
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateInitialised:
		break;

	case XenbusStateInitialising:
		if (xb_dev->state == XenbusStateReconfiguring)
			break;

		/* recovering after backend unexpected closure */
		displback_disconnect(front_info);
		break;

	case XenbusStateInitWait:
		if (xb_dev->state == XenbusStateReconfiguring)
			break;

		/* recovering after backend unexpected closure */
		displback_disconnect(front_info);
		if (xb_dev->state != XenbusStateInitialising)
			break;

		ret = displback_initwait(front_info);
		if (ret < 0)
			xenbus_dev_fatal(xb_dev, ret, "initializing frontend");
		else
			xenbus_switch_state(xb_dev, XenbusStateInitialised);
		break;

	case XenbusStateConnected:
		if (xb_dev->state != XenbusStateInitialised)
			break;

		ret = displback_connect(front_info);
		if (ret < 0) {
			displback_disconnect(front_info);
			xenbus_dev_fatal(xb_dev, ret, "connecting backend");
		} else {
			xenbus_switch_state(xb_dev, XenbusStateConnected);
		}
		break;

	case XenbusStateClosing:
		/*
		 * in this state backend starts freeing resources,
		 * so let it go into closed state, so we can also
		 * remove ours
		 */
		break;

	case XenbusStateUnknown:
	case XenbusStateClosed:
		if (xb_dev->state == XenbusStateClosed)
			break;

		displback_disconnect(front_info);
		break;
	}
}

static int xen_drv_probe(struct xenbus_device *xb_dev,
			 const struct xenbus_device_id *id)
{
	struct xen_drm_front_info *front_info;
	struct device *dev = &xb_dev->dev;
	int ret;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret < 0) {
		DRM_ERROR("Cannot setup DMA mask, ret %d", ret);
		return ret;
	}

	front_info = devm_kzalloc(&xb_dev->dev,
				  sizeof(*front_info), GFP_KERNEL);
	if (!front_info)
		return -ENOMEM;

	front_info->xb_dev = xb_dev;
	spin_lock_init(&front_info->io_lock);
	INIT_LIST_HEAD(&front_info->dbuf_list);
	dev_set_drvdata(&xb_dev->dev, front_info);

	return xenbus_switch_state(xb_dev, XenbusStateInitialising);
}

static int xen_drv_remove(struct xenbus_device *dev)
{
	struct xen_drm_front_info *front_info = dev_get_drvdata(&dev->dev);
	int to = 100;

	xenbus_switch_state(dev, XenbusStateClosing);

	/*
	 * On driver removal it is disconnected from XenBus,
	 * so no backend state change events come via .otherend_changed
	 * callback. This prevents us from exiting gracefully, e.g.
	 * signaling the backend to free event channels, waiting for its
	 * state to change to XenbusStateClosed and cleaning at our end.
	 * Normally when front driver removed backend will finally go into
	 * XenbusStateInitWait state.
	 *
	 * Workaround: read backend's state manually and wait with time-out.
	 */
	while ((xenbus_read_unsigned(front_info->xb_dev->otherend, "state",
				     XenbusStateUnknown) != XenbusStateInitWait) &&
				     --to)
		msleep(10);

	if (!to) {
		unsigned int state;

		state = xenbus_read_unsigned(front_info->xb_dev->otherend,
					     "state", XenbusStateUnknown);
		DRM_ERROR("Backend state is %s while removing driver\n",
			  xenbus_strstate(state));
	}

	xen_drm_drv_fini(front_info);
	xenbus_frontend_closed(dev);
	return 0;
}

static const struct xenbus_device_id xen_driver_ids[] = {
	{ XENDISPL_DRIVER_NAME },
	{ "" }
};

static struct xenbus_driver xen_driver = {
	.ids = xen_driver_ids,
	.probe = xen_drv_probe,
	.remove = xen_drv_remove,
	.otherend_changed = displback_changed,
	.not_essential = true,
};

static int __init xen_drv_init(void)
{
	/* At the moment we only support case with XEN_PAGE_SIZE == PAGE_SIZE */
	if (XEN_PAGE_SIZE != PAGE_SIZE) {
		DRM_ERROR(XENDISPL_DRIVER_NAME ": different kernel and Xen page sizes are not supported: XEN_PAGE_SIZE (%lu) != PAGE_SIZE (%lu)\n",
			  XEN_PAGE_SIZE, PAGE_SIZE);
		return -ENODEV;
	}

	if (!xen_domain())
		return -ENODEV;

	if (!xen_has_pv_devices())
		return -ENODEV;

	DRM_INFO("Registering XEN PV " XENDISPL_DRIVER_NAME "\n");
	return xenbus_register_frontend(&xen_driver);
}

static void __exit xen_drv_fini(void)
{
	DRM_INFO("Unregistering XEN PV " XENDISPL_DRIVER_NAME "\n");
	xenbus_unregister_driver(&xen_driver);
}

module_init(xen_drv_init);
module_exit(xen_drv_fini);

MODULE_DESCRIPTION("Xen para-virtualized display device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:" XENDISPL_DRIVER_NAME);
