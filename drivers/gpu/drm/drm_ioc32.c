/*
 * \file drm_ioc32.c
 *
 * 32-bit ioctl compatibility routines for the DRM.
 *
 * \author Paul Mackerras <paulus@samba.org>
 *
 * Copyright (C) Paul Mackerras 2005.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <linux/compat.h>
#include <linux/ratelimit.h>
#include <linux/export.h>

#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"
#include "drm_legacy.h"

#define DRM_IOCTL_VERSION32		DRM_IOWR(0x00, drm_version32_t)
#define DRM_IOCTL_GET_UNIQUE32		DRM_IOWR(0x01, drm_unique32_t)
#define DRM_IOCTL_GET_MAP32		DRM_IOWR(0x04, drm_map32_t)
#define DRM_IOCTL_GET_CLIENT32		DRM_IOWR(0x05, drm_client32_t)
#define DRM_IOCTL_GET_STATS32		DRM_IOR( 0x06, drm_stats32_t)

#define DRM_IOCTL_SET_UNIQUE32		DRM_IOW( 0x10, drm_unique32_t)
#define DRM_IOCTL_ADD_MAP32		DRM_IOWR(0x15, drm_map32_t)
#define DRM_IOCTL_ADD_BUFS32		DRM_IOWR(0x16, drm_buf_desc32_t)
#define DRM_IOCTL_MARK_BUFS32		DRM_IOW( 0x17, drm_buf_desc32_t)
#define DRM_IOCTL_INFO_BUFS32		DRM_IOWR(0x18, drm_buf_info32_t)
#define DRM_IOCTL_MAP_BUFS32		DRM_IOWR(0x19, drm_buf_map32_t)
#define DRM_IOCTL_FREE_BUFS32		DRM_IOW( 0x1a, drm_buf_free32_t)

#define DRM_IOCTL_RM_MAP32		DRM_IOW( 0x1b, drm_map32_t)

#define DRM_IOCTL_SET_SAREA_CTX32	DRM_IOW( 0x1c, drm_ctx_priv_map32_t)
#define DRM_IOCTL_GET_SAREA_CTX32	DRM_IOWR(0x1d, drm_ctx_priv_map32_t)

#define DRM_IOCTL_RES_CTX32		DRM_IOWR(0x26, drm_ctx_res32_t)
#define DRM_IOCTL_DMA32			DRM_IOWR(0x29, drm_dma32_t)

#define DRM_IOCTL_AGP_ENABLE32		DRM_IOW( 0x32, drm_agp_mode32_t)
#define DRM_IOCTL_AGP_INFO32		DRM_IOR( 0x33, drm_agp_info32_t)
#define DRM_IOCTL_AGP_ALLOC32		DRM_IOWR(0x34, drm_agp_buffer32_t)
#define DRM_IOCTL_AGP_FREE32		DRM_IOW( 0x35, drm_agp_buffer32_t)
#define DRM_IOCTL_AGP_BIND32		DRM_IOW( 0x36, drm_agp_binding32_t)
#define DRM_IOCTL_AGP_UNBIND32		DRM_IOW( 0x37, drm_agp_binding32_t)

#define DRM_IOCTL_SG_ALLOC32		DRM_IOW( 0x38, drm_scatter_gather32_t)
#define DRM_IOCTL_SG_FREE32		DRM_IOW( 0x39, drm_scatter_gather32_t)

#define DRM_IOCTL_UPDATE_DRAW32		DRM_IOW( 0x3f, drm_update_draw32_t)

#define DRM_IOCTL_WAIT_VBLANK32		DRM_IOWR(0x3a, drm_wait_vblank32_t)

#define DRM_IOCTL_MODE_ADDFB232		DRM_IOWR(0xb8, drm_mode_fb_cmd232_t)

typedef struct drm_version_32 {
	int version_major;	  /* Major version */
	int version_minor;	  /* Minor version */
	int version_patchlevel;	   /* Patch level */
	u32 name_len;		  /* Length of name buffer */
	u32 name;		  /* Name of driver */
	u32 date_len;		  /* Length of date buffer */
	u32 date;		  /* User-space buffer to hold date */
	u32 desc_len;		  /* Length of desc buffer */
	u32 desc;		  /* User-space buffer to hold desc */
} drm_version32_t;

static int compat_drm_version(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	drm_version32_t v32;
	struct drm_version v;
	int err;

	if (copy_from_user(&v32, (void __user *)arg, sizeof(v32)))
		return -EFAULT;

	memset(&v, 0, sizeof(v));

	v = (struct drm_version) {
		.name_len = v32.name_len,
		.name = compat_ptr(v32.name),
		.date_len = v32.date_len,
		.date = compat_ptr(v32.date),
		.desc_len = v32.desc_len,
		.desc = compat_ptr(v32.desc),
	};
	err = drm_ioctl_kernel(file, drm_version, &v,
			       DRM_RENDER_ALLOW);
	if (err)
		return err;

	v32.version_major = v.version_major;
	v32.version_minor = v.version_minor;
	v32.version_patchlevel = v.version_patchlevel;
	v32.name_len = v.name_len;
	v32.date_len = v.date_len;
	v32.desc_len = v.desc_len;
	if (copy_to_user((void __user *)arg, &v32, sizeof(v32)))
		return -EFAULT;
	return 0;
}

typedef struct drm_unique32 {
	u32 unique_len;	/* Length of unique */
	u32 unique;	/* Unique name for driver instantiation */
} drm_unique32_t;

static int compat_drm_getunique(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_unique32_t uq32;
	struct drm_unique uq;
	int err;

	if (copy_from_user(&uq32, (void __user *)arg, sizeof(uq32)))
		return -EFAULT;

	memset(&uq, 0, sizeof(uq));

	uq = (struct drm_unique){
		.unique_len = uq32.unique_len,
		.unique = compat_ptr(uq32.unique),
	};

	err = drm_ioctl_kernel(file, drm_getunique, &uq, 0);
	if (err)
		return err;

	uq32.unique_len = uq.unique_len;
	if (copy_to_user((void __user *)arg, &uq32, sizeof(uq32)))
		return -EFAULT;
	return 0;
}

static int compat_drm_setunique(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	/* it's dead */
	return -EINVAL;
}

#if IS_ENABLED(CONFIG_DRM_LEGACY)
typedef struct drm_map32 {
	u32 offset;		/* Requested physical address (0 for SAREA) */
	u32 size;		/* Requested physical size (bytes) */
	enum drm_map_type type;	/* Type of memory to map */
	enum drm_map_flags flags;	/* Flags */
	u32 handle;		/* User-space: "Handle" to pass to mmap() */
	int mtrr;		/* MTRR slot used */
} drm_map32_t;

static int compat_drm_getmap(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	drm_map32_t __user *argp = (void __user *)arg;
	drm_map32_t m32;
	struct drm_map map;
	int err;

	if (copy_from_user(&m32, argp, sizeof(m32)))
		return -EFAULT;

	map.offset = m32.offset;
	err = drm_ioctl_kernel(file, drm_legacy_getmap_ioctl, &map, 0);
	if (err)
		return err;

	m32.offset = map.offset;
	m32.size = map.size;
	m32.type = map.type;
	m32.flags = map.flags;
	m32.handle = ptr_to_compat((void __user *)map.handle);
	m32.mtrr = map.mtrr;
	if (copy_to_user(argp, &m32, sizeof(m32)))
		return -EFAULT;
	return 0;

}

static int compat_drm_addmap(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	drm_map32_t __user *argp = (void __user *)arg;
	drm_map32_t m32;
	struct drm_map map;
	int err;

	if (copy_from_user(&m32, argp, sizeof(m32)))
		return -EFAULT;

	map.offset = m32.offset;
	map.size = m32.size;
	map.type = m32.type;
	map.flags = m32.flags;

	err = drm_ioctl_kernel(file, drm_legacy_addmap_ioctl, &map,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
	if (err)
		return err;

	m32.offset = map.offset;
	m32.mtrr = map.mtrr;
	m32.handle = ptr_to_compat((void __user *)map.handle);
	if (map.handle != compat_ptr(m32.handle))
		pr_err_ratelimited("compat_drm_addmap truncated handle %p for type %d offset %x\n",
				   map.handle, m32.type, m32.offset);

	if (copy_to_user(argp, &m32, sizeof(m32)))
		return -EFAULT;

	return 0;
}

static int compat_drm_rmmap(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	drm_map32_t __user *argp = (void __user *)arg;
	struct drm_map map;
	u32 handle;

	if (get_user(handle, &argp->handle))
		return -EFAULT;
	map.handle = compat_ptr(handle);
	return drm_ioctl_kernel(file, drm_legacy_rmmap_ioctl, &map, DRM_AUTH);
}
#endif

typedef struct drm_client32 {
	int idx;	/* Which client desired? */
	int auth;	/* Is client authenticated? */
	u32 pid;	/* Process ID */
	u32 uid;	/* User ID */
	u32 magic;	/* Magic */
	u32 iocs;	/* Ioctl count */
} drm_client32_t;

static int compat_drm_getclient(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_client32_t c32;
	drm_client32_t __user *argp = (void __user *)arg;
	struct drm_client client;
	int err;

	if (copy_from_user(&c32, argp, sizeof(c32)))
		return -EFAULT;

	memset(&client, 0, sizeof(client));

	client.idx = c32.idx;

	err = drm_ioctl_kernel(file, drm_getclient, &client, 0);
	if (err)
		return err;

	c32.idx = client.idx;
	c32.auth = client.auth;
	c32.pid = client.pid;
	c32.uid = client.uid;
	c32.magic = client.magic;
	c32.iocs = client.iocs;

	if (copy_to_user(argp, &c32, sizeof(c32)))
		return -EFAULT;
	return 0;
}

typedef struct drm_stats32 {
	u32 count;
	struct {
		u32 value;
		enum drm_stat_type type;
	} data[15];
} drm_stats32_t;

static int compat_drm_getstats(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_stats32_t __user *argp = (void __user *)arg;

	/* getstats is defunct, just clear */
	if (clear_user(argp, sizeof(drm_stats32_t)))
		return -EFAULT;
	return 0;
}

#if IS_ENABLED(CONFIG_DRM_LEGACY)
typedef struct drm_buf_desc32 {
	int count;		 /* Number of buffers of this size */
	int size;		 /* Size in bytes */
	int low_mark;		 /* Low water mark */
	int high_mark;		 /* High water mark */
	int flags;
	u32 agp_start;		 /* Start address in the AGP aperture */
} drm_buf_desc32_t;

static int compat_drm_addbufs(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	drm_buf_desc32_t __user *argp = (void __user *)arg;
	drm_buf_desc32_t desc32;
	struct drm_buf_desc desc;
	int err;

	if (copy_from_user(&desc32, argp, sizeof(drm_buf_desc32_t)))
		return -EFAULT;

	desc = (struct drm_buf_desc){
		desc32.count, desc32.size, desc32.low_mark, desc32.high_mark,
		desc32.flags, desc32.agp_start
	};

	err = drm_ioctl_kernel(file, drm_legacy_addbufs, &desc,
				   DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
	if (err)
		return err;

	desc32 = (drm_buf_desc32_t){
		desc.count, desc.size, desc.low_mark, desc.high_mark,
		desc.flags, desc.agp_start
	};
	if (copy_to_user(argp, &desc32, sizeof(drm_buf_desc32_t)))
		return -EFAULT;

	return 0;
}

static int compat_drm_markbufs(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_buf_desc32_t b32;
	drm_buf_desc32_t __user *argp = (void __user *)arg;
	struct drm_buf_desc buf;

	if (copy_from_user(&b32, argp, sizeof(b32)))
		return -EFAULT;

	buf.size = b32.size;
	buf.low_mark = b32.low_mark;
	buf.high_mark = b32.high_mark;

	return drm_ioctl_kernel(file, drm_legacy_markbufs, &buf,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

typedef struct drm_buf_info32 {
	int count;		/**< Entries in list */
	u32 list;
} drm_buf_info32_t;

static int copy_one_buf32(void *data, int count, struct drm_buf_entry *from)
{
	drm_buf_info32_t *request = data;
	drm_buf_desc32_t __user *to = compat_ptr(request->list);
	drm_buf_desc32_t v = {.count = from->buf_count,
			      .size = from->buf_size,
			      .low_mark = from->low_mark,
			      .high_mark = from->high_mark};

	if (copy_to_user(to + count, &v, offsetof(drm_buf_desc32_t, flags)))
		return -EFAULT;
	return 0;
}

static int drm_legacy_infobufs32(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	drm_buf_info32_t *request = data;

	return __drm_legacy_infobufs(dev, data, &request->count, copy_one_buf32);
}

static int compat_drm_infobufs(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_buf_info32_t req32;
	drm_buf_info32_t __user *argp = (void __user *)arg;
	int err;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	if (req32.count < 0)
		req32.count = 0;

	err = drm_ioctl_kernel(file, drm_legacy_infobufs32, &req32, DRM_AUTH);
	if (err)
		return err;

	if (put_user(req32.count, &argp->count))
		return -EFAULT;

	return 0;
}

typedef struct drm_buf_pub32 {
	int idx;		/**< Index into the master buffer list */
	int total;		/**< Buffer size */
	int used;		/**< Amount of buffer in use (for DMA) */
	u32 address;		/**< Address of buffer */
} drm_buf_pub32_t;

typedef struct drm_buf_map32 {
	int count;		/**< Length of the buffer list */
	u32 virtual;		/**< Mmap'd area in user-virtual */
	u32 list;		/**< Buffer information */
} drm_buf_map32_t;

static int map_one_buf32(void *data, int idx, unsigned long virtual,
			struct drm_buf *buf)
{
	drm_buf_map32_t *request = data;
	drm_buf_pub32_t __user *to = compat_ptr(request->list) + idx;
	drm_buf_pub32_t v;

	v.idx = buf->idx;
	v.total = buf->total;
	v.used = 0;
	v.address = virtual + buf->offset;
	if (copy_to_user(to, &v, sizeof(v)))
		return -EFAULT;
	return 0;
}

static int drm_legacy_mapbufs32(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	drm_buf_map32_t *request = data;
	void __user *v;
	int err = __drm_legacy_mapbufs(dev, data, &request->count,
				    &v, map_one_buf32,
				    file_priv);
	request->virtual = ptr_to_compat(v);
	return err;
}

static int compat_drm_mapbufs(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	drm_buf_map32_t __user *argp = (void __user *)arg;
	drm_buf_map32_t req32;
	int err;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;
	if (req32.count < 0)
		return -EINVAL;

	err = drm_ioctl_kernel(file, drm_legacy_mapbufs32, &req32, DRM_AUTH);
	if (err)
		return err;

	if (put_user(req32.count, &argp->count)
	    || put_user(req32.virtual, &argp->virtual))
		return -EFAULT;

	return 0;
}

typedef struct drm_buf_free32 {
	int count;
	u32 list;
} drm_buf_free32_t;

static int compat_drm_freebufs(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_buf_free32_t req32;
	struct drm_buf_free request;
	drm_buf_free32_t __user *argp = (void __user *)arg;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	request.count = req32.count;
	request.list = compat_ptr(req32.list);
	return drm_ioctl_kernel(file, drm_legacy_freebufs, &request, DRM_AUTH);
}

typedef struct drm_ctx_priv_map32 {
	unsigned int ctx_id;	 /**< Context requesting private mapping */
	u32 handle;		/**< Handle of map */
} drm_ctx_priv_map32_t;

static int compat_drm_setsareactx(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	drm_ctx_priv_map32_t req32;
	struct drm_ctx_priv_map request;
	drm_ctx_priv_map32_t __user *argp = (void __user *)arg;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	request.ctx_id = req32.ctx_id;
	request.handle = compat_ptr(req32.handle);
	return drm_ioctl_kernel(file, drm_legacy_setsareactx, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

static int compat_drm_getsareactx(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct drm_ctx_priv_map req;
	drm_ctx_priv_map32_t req32;
	drm_ctx_priv_map32_t __user *argp = (void __user *)arg;
	int err;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	req.ctx_id = req32.ctx_id;
	err = drm_ioctl_kernel(file, drm_legacy_getsareactx, &req, DRM_AUTH);
	if (err)
		return err;

	req32.handle = ptr_to_compat((void __user *)req.handle);
	if (copy_to_user(argp, &req32, sizeof(req32)))
		return -EFAULT;

	return 0;
}

typedef struct drm_ctx_res32 {
	int count;
	u32 contexts;
} drm_ctx_res32_t;

static int compat_drm_resctx(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	drm_ctx_res32_t __user *argp = (void __user *)arg;
	drm_ctx_res32_t res32;
	struct drm_ctx_res res;
	int err;

	if (copy_from_user(&res32, argp, sizeof(res32)))
		return -EFAULT;

	res.count = res32.count;
	res.contexts = compat_ptr(res32.contexts);
	err = drm_ioctl_kernel(file, drm_legacy_resctx, &res, DRM_AUTH);
	if (err)
		return err;

	res32.count = res.count;
	if (copy_to_user(argp, &res32, sizeof(res32)))
		return -EFAULT;

	return 0;
}

typedef struct drm_dma32 {
	int context;		  /**< Context handle */
	int send_count;		  /**< Number of buffers to send */
	u32 send_indices;	  /**< List of handles to buffers */
	u32 send_sizes;		  /**< Lengths of data to send */
	enum drm_dma_flags flags;		  /**< Flags */
	int request_count;	  /**< Number of buffers requested */
	int request_size;	  /**< Desired size for buffers */
	u32 request_indices;	  /**< Buffer information */
	u32 request_sizes;
	int granted_count;	  /**< Number of buffers granted */
} drm_dma32_t;

static int compat_drm_dma(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	drm_dma32_t d32;
	drm_dma32_t __user *argp = (void __user *)arg;
	struct drm_dma d;
	int err;

	if (copy_from_user(&d32, argp, sizeof(d32)))
		return -EFAULT;

	d.context = d32.context;
	d.send_count = d32.send_count;
	d.send_indices = compat_ptr(d32.send_indices);
	d.send_sizes = compat_ptr(d32.send_sizes);
	d.flags = d32.flags;
	d.request_count = d32.request_count;
	d.request_indices = compat_ptr(d32.request_indices);
	d.request_sizes = compat_ptr(d32.request_sizes);
	err = drm_ioctl_kernel(file, drm_legacy_dma_ioctl, &d, DRM_AUTH);
	if (err)
		return err;

	if (put_user(d.request_size, &argp->request_size)
	    || put_user(d.granted_count, &argp->granted_count))
		return -EFAULT;

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRM_LEGACY)
#if IS_ENABLED(CONFIG_AGP)
typedef struct drm_agp_mode32 {
	u32 mode;	/**< AGP mode */
} drm_agp_mode32_t;

static int compat_drm_agp_enable(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	drm_agp_mode32_t __user *argp = (void __user *)arg;
	struct drm_agp_mode mode;

	if (get_user(mode.mode, &argp->mode))
		return -EFAULT;

	return drm_ioctl_kernel(file,  drm_legacy_agp_enable_ioctl, &mode,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

typedef struct drm_agp_info32 {
	int agp_version_major;
	int agp_version_minor;
	u32 mode;
	u32 aperture_base;	/* physical address */
	u32 aperture_size;	/* bytes */
	u32 memory_allowed;	/* bytes */
	u32 memory_used;

	/* PCI information */
	unsigned short id_vendor;
	unsigned short id_device;
} drm_agp_info32_t;

static int compat_drm_agp_info(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_agp_info32_t __user *argp = (void __user *)arg;
	drm_agp_info32_t i32;
	struct drm_agp_info info;
	int err;

	err = drm_ioctl_kernel(file, drm_legacy_agp_info_ioctl, &info, DRM_AUTH);
	if (err)
		return err;

	i32.agp_version_major = info.agp_version_major;
	i32.agp_version_minor = info.agp_version_minor;
	i32.mode = info.mode;
	i32.aperture_base = info.aperture_base;
	i32.aperture_size = info.aperture_size;
	i32.memory_allowed = info.memory_allowed;
	i32.memory_used = info.memory_used;
	i32.id_vendor = info.id_vendor;
	i32.id_device = info.id_device;
	if (copy_to_user(argp, &i32, sizeof(i32)))
		return -EFAULT;

	return 0;
}

typedef struct drm_agp_buffer32 {
	u32 size;	/**< In bytes -- will round to page boundary */
	u32 handle;	/**< Used for binding / unbinding */
	u32 type;	/**< Type of memory to allocate */
	u32 physical;	/**< Physical used by i810 */
} drm_agp_buffer32_t;

static int compat_drm_agp_alloc(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_agp_buffer32_t __user *argp = (void __user *)arg;
	drm_agp_buffer32_t req32;
	struct drm_agp_buffer request;
	int err;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	request.size = req32.size;
	request.type = req32.type;
	err = drm_ioctl_kernel(file, drm_legacy_agp_alloc_ioctl, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
	if (err)
		return err;

	req32.handle = request.handle;
	req32.physical = request.physical;
	if (copy_to_user(argp, &req32, sizeof(req32))) {
		drm_ioctl_kernel(file, drm_legacy_agp_free_ioctl, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
		return -EFAULT;
	}

	return 0;
}

static int compat_drm_agp_free(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_agp_buffer32_t __user *argp = (void __user *)arg;
	struct drm_agp_buffer request;

	if (get_user(request.handle, &argp->handle))
		return -EFAULT;

	return drm_ioctl_kernel(file, drm_legacy_agp_free_ioctl, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

typedef struct drm_agp_binding32 {
	u32 handle;	/**< From drm_agp_buffer */
	u32 offset;	/**< In bytes -- will round to page boundary */
} drm_agp_binding32_t;

static int compat_drm_agp_bind(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_agp_binding32_t __user *argp = (void __user *)arg;
	drm_agp_binding32_t req32;
	struct drm_agp_binding request;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	request.handle = req32.handle;
	request.offset = req32.offset;
	return drm_ioctl_kernel(file, drm_legacy_agp_bind_ioctl, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

static int compat_drm_agp_unbind(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	drm_agp_binding32_t __user *argp = (void __user *)arg;
	struct drm_agp_binding request;

	if (get_user(request.handle, &argp->handle))
		return -EFAULT;

	return drm_ioctl_kernel(file, drm_legacy_agp_unbind_ioctl, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}
#endif /* CONFIG_AGP */

typedef struct drm_scatter_gather32 {
	u32 size;	/**< In bytes -- will round to page boundary */
	u32 handle;	/**< Used for mapping / unmapping */
} drm_scatter_gather32_t;

static int compat_drm_sg_alloc(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_scatter_gather32_t __user *argp = (void __user *)arg;
	struct drm_scatter_gather request;
	int err;

	if (get_user(request.size, &argp->size))
		return -EFAULT;

	err = drm_ioctl_kernel(file, drm_legacy_sg_alloc, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
	if (err)
		return err;

	/* XXX not sure about the handle conversion here... */
	if (put_user(request.handle >> PAGE_SHIFT, &argp->handle))
		return -EFAULT;

	return 0;
}

static int compat_drm_sg_free(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	drm_scatter_gather32_t __user *argp = (void __user *)arg;
	struct drm_scatter_gather request;
	unsigned long x;

	if (get_user(x, &argp->handle))
		return -EFAULT;
	request.handle = x << PAGE_SHIFT;
	return drm_ioctl_kernel(file, drm_legacy_sg_free, &request,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}
#endif
#if defined(CONFIG_X86)
typedef struct drm_update_draw32 {
	drm_drawable_t handle;
	unsigned int type;
	unsigned int num;
	/* 64-bit version has a 32-bit pad here */
	u64 data;	/**< Pointer */
} __attribute__((packed)) drm_update_draw32_t;

static int compat_drm_update_draw(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	/* update_draw is defunct */
	return 0;
}
#endif

struct drm_wait_vblank_request32 {
	enum drm_vblank_seq_type type;
	unsigned int sequence;
	u32 signal;
};

struct drm_wait_vblank_reply32 {
	enum drm_vblank_seq_type type;
	unsigned int sequence;
	s32 tval_sec;
	s32 tval_usec;
};

typedef union drm_wait_vblank32 {
	struct drm_wait_vblank_request32 request;
	struct drm_wait_vblank_reply32 reply;
} drm_wait_vblank32_t;

static int compat_drm_wait_vblank(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	drm_wait_vblank32_t __user *argp = (void __user *)arg;
	drm_wait_vblank32_t req32;
	union drm_wait_vblank req;
	int err;

	if (copy_from_user(&req32, argp, sizeof(req32)))
		return -EFAULT;

	memset(&req, 0, sizeof(req));

	req.request.type = req32.request.type;
	req.request.sequence = req32.request.sequence;
	req.request.signal = req32.request.signal;
	err = drm_ioctl_kernel(file, drm_wait_vblank_ioctl, &req, DRM_UNLOCKED);

	req32.reply.type = req.reply.type;
	req32.reply.sequence = req.reply.sequence;
	req32.reply.tval_sec = req.reply.tval_sec;
	req32.reply.tval_usec = req.reply.tval_usec;
	if (copy_to_user(argp, &req32, sizeof(req32)))
		return -EFAULT;

	return err;
}

#if defined(CONFIG_X86)
typedef struct drm_mode_fb_cmd232 {
	u32 fb_id;
	u32 width;
	u32 height;
	u32 pixel_format;
	u32 flags;
	u32 handles[4];
	u32 pitches[4];
	u32 offsets[4];
	u64 modifier[4];
} __attribute__((packed)) drm_mode_fb_cmd232_t;

static int compat_drm_mode_addfb2(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct drm_mode_fb_cmd232 __user *argp = (void __user *)arg;
	struct drm_mode_fb_cmd2 req64;
	int err;

	memset(&req64, 0, sizeof(req64));

	if (copy_from_user(&req64, argp,
			   offsetof(drm_mode_fb_cmd232_t, modifier)))
		return -EFAULT;

	if (copy_from_user(&req64.modifier, &argp->modifier,
			   sizeof(req64.modifier)))
		return -EFAULT;

	err = drm_ioctl_kernel(file, drm_mode_addfb2, &req64, 0);
	if (err)
		return err;

	if (put_user(req64.fb_id, &argp->fb_id))
		return -EFAULT;

	return 0;
}
#endif

static struct {
	drm_ioctl_compat_t *fn;
	char *name;
} drm_compat_ioctls[] = {
#define DRM_IOCTL32_DEF(n, f) [DRM_IOCTL_NR(n##32)] = {.fn = f, .name = #n}
	DRM_IOCTL32_DEF(DRM_IOCTL_VERSION, compat_drm_version),
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_UNIQUE, compat_drm_getunique),
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_MAP, compat_drm_getmap),
#endif
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_CLIENT, compat_drm_getclient),
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_STATS, compat_drm_getstats),
	DRM_IOCTL32_DEF(DRM_IOCTL_SET_UNIQUE, compat_drm_setunique),
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	DRM_IOCTL32_DEF(DRM_IOCTL_ADD_MAP, compat_drm_addmap),
	DRM_IOCTL32_DEF(DRM_IOCTL_ADD_BUFS, compat_drm_addbufs),
	DRM_IOCTL32_DEF(DRM_IOCTL_MARK_BUFS, compat_drm_markbufs),
	DRM_IOCTL32_DEF(DRM_IOCTL_INFO_BUFS, compat_drm_infobufs),
	DRM_IOCTL32_DEF(DRM_IOCTL_MAP_BUFS, compat_drm_mapbufs),
	DRM_IOCTL32_DEF(DRM_IOCTL_FREE_BUFS, compat_drm_freebufs),
	DRM_IOCTL32_DEF(DRM_IOCTL_RM_MAP, compat_drm_rmmap),
	DRM_IOCTL32_DEF(DRM_IOCTL_SET_SAREA_CTX, compat_drm_setsareactx),
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_SAREA_CTX, compat_drm_getsareactx),
	DRM_IOCTL32_DEF(DRM_IOCTL_RES_CTX, compat_drm_resctx),
	DRM_IOCTL32_DEF(DRM_IOCTL_DMA, compat_drm_dma),
#if IS_ENABLED(CONFIG_AGP)
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_ENABLE, compat_drm_agp_enable),
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_INFO, compat_drm_agp_info),
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_ALLOC, compat_drm_agp_alloc),
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_FREE, compat_drm_agp_free),
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_BIND, compat_drm_agp_bind),
	DRM_IOCTL32_DEF(DRM_IOCTL_AGP_UNBIND, compat_drm_agp_unbind),
#endif
#endif
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	DRM_IOCTL32_DEF(DRM_IOCTL_SG_ALLOC, compat_drm_sg_alloc),
	DRM_IOCTL32_DEF(DRM_IOCTL_SG_FREE, compat_drm_sg_free),
#endif
#if defined(CONFIG_X86) || defined(CONFIG_IA64)
	DRM_IOCTL32_DEF(DRM_IOCTL_UPDATE_DRAW, compat_drm_update_draw),
#endif
	DRM_IOCTL32_DEF(DRM_IOCTL_WAIT_VBLANK, compat_drm_wait_vblank),
#if defined(CONFIG_X86) || defined(CONFIG_IA64)
	DRM_IOCTL32_DEF(DRM_IOCTL_MODE_ADDFB2, compat_drm_mode_addfb2),
#endif
};

/**
 * drm_compat_ioctl - 32bit IOCTL compatibility handler for DRM drivers
 * @filp: file this ioctl is called on
 * @cmd: ioctl cmd number
 * @arg: user argument
 *
 * Compatibility handler for 32 bit userspace running on 64 kernels. All actual
 * IOCTL handling is forwarded to drm_ioctl(), while marshalling structures as
 * appropriate. Note that this only handles DRM core IOCTLs, if the driver has
 * botched IOCTL itself, it must handle those by wrapping this function.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
long drm_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	drm_ioctl_compat_t *fn;
	int ret;

	/* Assume that ioctls without an explicit compat routine will just
	 * work.  This may not always be a good assumption, but it's better
	 * than always failing.
	 */
	if (nr >= ARRAY_SIZE(drm_compat_ioctls))
		return drm_ioctl(filp, cmd, arg);

	fn = drm_compat_ioctls[nr].fn;
	if (!fn)
		return drm_ioctl(filp, cmd, arg);

	drm_dbg_core(dev, "comm=\"%s\", pid=%d, dev=0x%lx, auth=%d, %s\n",
		     current->comm, task_pid_nr(current),
		     (long)old_encode_dev(file_priv->minor->kdev->devt),
		     file_priv->authenticated,
		     drm_compat_ioctls[nr].name);
	ret = (*fn)(filp, cmd, arg);
	if (ret)
		drm_dbg_core(dev, "ret = %d\n", ret);
	return ret;
}
EXPORT_SYMBOL(drm_compat_ioctl);
