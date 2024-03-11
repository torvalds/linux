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

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

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
	err = drm_ioctl_kernel(file, drm_wait_vblank_ioctl, &req, 0);

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
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_CLIENT, compat_drm_getclient),
	DRM_IOCTL32_DEF(DRM_IOCTL_GET_STATS, compat_drm_getstats),
	DRM_IOCTL32_DEF(DRM_IOCTL_SET_UNIQUE, compat_drm_setunique),
#if defined(CONFIG_X86)
	DRM_IOCTL32_DEF(DRM_IOCTL_UPDATE_DRAW, compat_drm_update_draw),
#endif
	DRM_IOCTL32_DEF(DRM_IOCTL_WAIT_VBLANK, compat_drm_wait_vblank),
#if defined(CONFIG_X86)
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
