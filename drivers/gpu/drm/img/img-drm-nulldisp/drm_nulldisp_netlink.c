/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#else
#include <drm/drmP.h>
#endif

#include <drm/drm_crtc.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/bug.h>

#include "drm_netlink_gem.h"
#include "drm_nulldisp_drv.h"
#include "drm_nulldisp_netlink.h"
#include "kernel_compatibility.h"

#include "netlink.h"

struct nlpvrdpy {
	atomic_t connected;
	struct net *net;
	u32 dst_portid;
	struct drm_device *dev;
	nlpvrdpy_disconnect_cb disconnect_cb;
	void *disconnect_cb_data;
	nlpvrdpy_flipped_cb flipped_cb;
	void *flipped_cb_data;
	nlpvrdpy_copied_cb copied_cb;
	void *copied_cb_data;
	struct mutex mutex;
	struct list_head nl_list;
	bool gem_names_required;
};
#define NLPVRDPY_MINOR(nlpvrdpy) \
	((unsigned int)((nlpvrdpy)->dev->primary->index))

/* Command internal flags */
#define	NLPVRDPY_CIF_NLPVRDPY_NOT_CONNECTED	0x00000001
#define	NLPVRDPY_CIF_NLPVRDPY			0x00000002

static LIST_HEAD(nlpvrdpy_list);
static DEFINE_MUTEX(nlpvrdpy_list_mutex);

static inline void nlpvrdpy_lock(struct nlpvrdpy *nlpvrdpy)
{
	mutex_lock(&nlpvrdpy->mutex);
}

static inline void nlpvrdpy_unlock(struct nlpvrdpy *nlpvrdpy)
{
	mutex_unlock(&nlpvrdpy->mutex);
}

struct nlpvrdpy *nlpvrdpy_create(struct drm_device *dev,
					nlpvrdpy_disconnect_cb disconnect_cb,
					void *disconnect_cb_data,
					nlpvrdpy_flipped_cb flipped_cb,
					void *flipped_cb_data,
					nlpvrdpy_copied_cb copied_cb,
					void *copied_cb_data)
{
	struct nlpvrdpy *nlpvrdpy = kzalloc(sizeof(*nlpvrdpy), GFP_KERNEL);

	if (!nlpvrdpy)
		return NULL;

	mutex_init(&nlpvrdpy->mutex);
	INIT_LIST_HEAD(&nlpvrdpy->nl_list);

	atomic_set(&nlpvrdpy->connected, 0);

	nlpvrdpy->dev = dev;
	nlpvrdpy->disconnect_cb = disconnect_cb;
	nlpvrdpy->disconnect_cb_data = disconnect_cb_data;
	nlpvrdpy->flipped_cb = flipped_cb;
	nlpvrdpy->flipped_cb_data = flipped_cb_data;
	nlpvrdpy->copied_cb = copied_cb;
	nlpvrdpy->copied_cb_data = copied_cb_data;

	mutex_lock(&nlpvrdpy_list_mutex);
	list_add_tail(&nlpvrdpy->nl_list, &nlpvrdpy_list);
	mutex_unlock(&nlpvrdpy_list_mutex);

	return nlpvrdpy;
}

void nlpvrdpy_destroy(struct nlpvrdpy *nlpvrdpy)
{
	if (!nlpvrdpy)
		return;

	mutex_lock(&nlpvrdpy_list_mutex);
	nlpvrdpy_lock(nlpvrdpy);
	list_del(&nlpvrdpy->nl_list);
	nlpvrdpy_unlock(nlpvrdpy);
	mutex_unlock(&nlpvrdpy_list_mutex);

	mutex_destroy(&nlpvrdpy->mutex);

	kfree(nlpvrdpy);
}

static struct nlpvrdpy *nlpvrdpy_lookup(u32 minor)
{
	struct nlpvrdpy *nlpvrdpy = NULL;
	struct nlpvrdpy *iter;

	mutex_lock(&nlpvrdpy_list_mutex);
	list_for_each_entry(iter, &nlpvrdpy_list, nl_list) {
		if (NLPVRDPY_MINOR(iter) == minor) {
			nlpvrdpy = iter;
			nlpvrdpy_lock(nlpvrdpy);
			break;
		}
	}
	mutex_unlock(&nlpvrdpy_list_mutex);

	return nlpvrdpy;
}

static int nlpvrdpy_pre_cmd(const struct genl_ops *ops,
				struct sk_buff *skb,
				struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct nlpvrdpy *nlpvrdpy = NULL;
	int ret;

	if (ops->internal_flags & NLPVRDPY_CIF_NLPVRDPY_NOT_CONNECTED) {
		if (!(ops->flags & GENL_ADMIN_PERM))
			return -EINVAL;
	}

	if (ops->internal_flags & (NLPVRDPY_CIF_NLPVRDPY_NOT_CONNECTED |
					NLPVRDPY_CIF_NLPVRDPY)) {
		u32 minor;

		if (!attrs[NLPVRDPY_ATTR_MINOR])
			return -EINVAL;

		minor = nla_get_u32(attrs[NLPVRDPY_ATTR_MINOR]);

		nlpvrdpy = nlpvrdpy_lookup(minor);
		if (!nlpvrdpy)
			return -ENODEV;

		if (ops->internal_flags & NLPVRDPY_CIF_NLPVRDPY) {
			if (!atomic_read(&nlpvrdpy->connected)) {
				ret = -ENOTCONN;
				goto err_unlock;
			}
			if ((nlpvrdpy->net != genl_info_net(info)) ||
				(nlpvrdpy->dst_portid != info->snd_portid)) {
				ret = -EPROTO;
				goto err_unlock;
			}
		}

		info->user_ptr[0] = nlpvrdpy;
	}

	ret = 0;

err_unlock:
	nlpvrdpy_unlock(nlpvrdpy);
	return ret;
}

static void nlpvrdpy_post_cmd(const struct genl_ops *ops,
				struct sk_buff *skb,
				struct genl_info *info)
{
}

static struct genl_family nlpvrdpy_family = {
	.name = "nlpvrdpy",
	.version = 1,
	.maxattr = NLPVRDPY_ATTR_MAX,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0))
	.policy = nlpvrdpy_policy,
#endif
	.pre_doit = &nlpvrdpy_pre_cmd,
	.post_doit = &nlpvrdpy_post_cmd
};

/* Must be called with the struct nlpvrdpy mutex held */
static int nlpvrdpy_send_msg_locked(struct nlpvrdpy *nlpvrdpy,
					struct sk_buff *msg)
{
	int err;

	if (atomic_read(&nlpvrdpy->connected)) {
		err = genlmsg_unicast(nlpvrdpy->net, msg, nlpvrdpy->dst_portid);
		if (err == -ECONNREFUSED)
			atomic_set(&nlpvrdpy->connected, 0);
	} else {
		err = -ENOTCONN;
		nlmsg_free(msg);
	}

	return err;
}

static int nlpvrdpy_send_msg(struct nlpvrdpy *nlpvrdpy, struct sk_buff *msg)
{
	int err;

	nlpvrdpy_lock(nlpvrdpy);
	err = nlpvrdpy_send_msg_locked(nlpvrdpy, msg);
	nlpvrdpy_unlock(nlpvrdpy);

	return err;
}

void nlpvrdpy_send_disconnect(struct nlpvrdpy *nlpvrdpy)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	if (!atomic_read(&nlpvrdpy->connected))
		return;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = genlmsg_put(msg, nlpvrdpy->dst_portid, 0,
				&nlpvrdpy_family, 0, NLPVRDPY_CMD_DISCONNECT);
	if (!hdr)
		goto err_msg_free;

	err = nla_put_u32(msg, NLPVRDPY_ATTR_MINOR, NLPVRDPY_MINOR(nlpvrdpy));
	if (err)
		goto err_msg_free;

	genlmsg_end(msg, hdr);

	nlpvrdpy_lock(nlpvrdpy);

	(void) nlpvrdpy_send_msg_locked(nlpvrdpy, msg);

	atomic_set(&nlpvrdpy->connected, 0);
	nlpvrdpy->net = NULL;
	nlpvrdpy->dst_portid = 0;

	nlpvrdpy_unlock(nlpvrdpy);

	return;

err_msg_free:
	nlmsg_free(msg);
}

static int nlpvrdpy_get_offsets_and_sizes(struct drm_framebuffer *fb,
					  struct drm_gem_object **objs,
					  u64 *addr, u64 *size)
{
	int i;

	for (i = 0; i < nulldisp_drm_fb_num_planes(fb); i++) {
		int err;
		struct drm_gem_object *obj = objs[i];

		err = drm_gem_create_mmap_offset(obj);
		if (err) {
			DRM_ERROR(
			    "Failed to get mmap offset for buffer[%d] = %p\n",
			    i, obj);
			return err;
		}

		addr[i] = drm_vma_node_offset_addr(&obj->vma_node);
		size[i] = obj->size;
	}

	return 0;
}

static int nlpvrdpy_put_fb_attributes(struct sk_buff *msg,
				      struct drm_framebuffer *fb,
				      struct nlpvrdpy *nlpvrdpy,
				      struct drm_gem_object **objs)
{
	int i, err;
	const int num_planes = nulldisp_drm_fb_num_planes(fb);
	u64 plane_addr[NLPVRDPY_MAX_NUM_PLANES],
	    plane_size[NLPVRDPY_MAX_NUM_PLANES];

	err = nlpvrdpy_get_offsets_and_sizes(fb, objs, &plane_addr[0], &plane_size[0]);
	if (err) {
		pr_err("%s: nlpvrdpy_get_offsets_and_sizes failed", __func__);
		return err;
	}

	err = nla_put_u32(msg, NLPVRDPY_ATTR_MINOR, NLPVRDPY_MINOR(nlpvrdpy));
	if (err) {
		pr_err("%s: nla_put_u32 NLPVRDPY_ATTR_MINOR failed", __func__);
		return err;
	}

	err = nla_put_u8(msg, NLPVRDPY_ATTR_NUM_PLANES, num_planes);
	if (err) {
		pr_err("%s: nla_put_u8 NLPVRDPY_ATTR_NUM_PLANES failed", __func__);
		return err;
	}

	err = nla_put_u32(msg, NLPVRDPY_ATTR_WIDTH, fb->width);
	if (err) {
		pr_err("%s: nla_put_u32 NLPVRDPY_ATTR_WIDTH failed", __func__);
		return err;
	}

	err = nla_put_u32(msg, NLPVRDPY_ATTR_HEIGHT, fb->height);
	if (err) {
		pr_err("%s: nla_put_u32 NLPVRDPY_ATTR_HEIGHT failed", __func__);
		return err;
	}

	err = nla_put_u32(msg, NLPVRDPY_ATTR_PIXFMT, nulldisp_drm_fb_format(fb));
	if (err) {
		pr_err("%s: nla_put_u32 NLPVRDPY_ATTR_PIXFMT failed",
		       __func__);
		return err;
	}

	err = nla_put_u64_64bit(msg, NLPVRDPY_ATTR_FB_MODIFIER,
				nulldisp_drm_fb_modifier(fb), NLPVRDPY_ATTR_PAD);
	if (err) {
		pr_err("%s: nla_put_u64_64bit NLPVRDPY_ATTR_FB_MODIFIER "
		       "NLPVRDPY_ATTR_PAD failed", __func__);
		return err;
	}

	/* IMG_COLORSPACE_BT601_CONFORMANT_RANGE */
	err = nla_put_u8(msg, NLPVRDPY_ATTR_YUV_CSC, 1);
	if (err) {
		pr_err("%s: nla_put_u8 NLPVRDPY_ATTR_YUV_CSC 1 failed", __func__);
		return err;
	}

	/* 8-bit per sample */
	err = nla_put_u8(msg, NLPVRDPY_ATTR_YUV_BPP, 8);
	if (err) {
		pr_err("%s: nla_put_u8 NLPVRDPY_ATTR_YUV_BPP 8 failed", __func__);
		return err;
	}

	for (i = 0; i < num_planes; i++) {
		err = nla_put_u64_64bit(msg, NLPVRDPY_ATTR_PLANE(i, ADDR),
					plane_addr[i], NLPVRDPY_ATTR_PAD);
		if (err) {
			pr_err("%s: nla_put_u64_64bit NLPVRDPY_ATTR_PLANE(%d, ADDR)"
			       " NLPVRDPY_ATTR_PAD failed", __func__, i);
			return err;
		}

		err = nla_put_u64_64bit(msg, NLPVRDPY_ATTR_PLANE(i, SIZE),
					plane_size[i], NLPVRDPY_ATTR_PAD);
		if (err) {
			pr_err("%s: nla_put_u64_64bit NLPVRDPY_ATTR_PLANE(%d, SIZE)"
			       " NLPVRDPY_ATTR_PAD failed", __func__, i);
			return err;
		}

		err = nla_put_u64_64bit(msg, NLPVRDPY_ATTR_PLANE(i, OFFSET),
					fb->offsets[i], NLPVRDPY_ATTR_PAD);
		if (err) {
			pr_err("%s: nla_put_u64_64bit NLPVRDPY_ATTR_PLANE(%d, OFFSET)"
			       " NLPVRDPY_ATTR_PAD failed", __func__, i);
			return err;
		}

		err = nla_put_u64_64bit(msg, NLPVRDPY_ATTR_PLANE(i, PITCH),
					fb->pitches[i], NLPVRDPY_ATTR_PAD);
		if (err) {
			pr_err("%s: nla_put_u64_64bit NLPVRDPY_ATTR_PLANE(%d, PITCH)"
			       " NLPVRDPY_ATTR_PAD failed", __func__, i);
			return err;
		}

		err = nla_put_u32(msg, NLPVRDPY_ATTR_PLANE(i, GEM_OBJ_NAME), (u32)objs[0]->name);
		if (err) {
			pr_err("%s: nla_put_u32 NLPVRDPY_ATTR_PLANE(%d, GEM_OBJ_NAME)"
			       " failed", __func__, i);
			return err;
		}
	}

	WARN_ONCE(num_planes > NLPVRDPY_MAX_NUM_PLANES,
		  "NLPVRDPY_MAX_NUM_PLANES = [%d], num_planes = [%d]\n",
		  NLPVRDPY_MAX_NUM_PLANES, num_planes);

	return 0;
}

static int nlpvrdpy_name_gem_obj(struct drm_device *dev,
				 struct drm_gem_object *obj)
{
	int ret;

	mutex_lock(&dev->object_name_lock);
	if (!obj->name) {
		ret = idr_alloc(&dev->object_name_idr, obj, 1, 0, GFP_KERNEL);
		if (ret < 0)
			goto exit_unlock;

		obj->name = ret;
	}

	ret = 0;

exit_unlock:
	mutex_unlock(&dev->object_name_lock);
	return ret;
}

static int nlpvrdpy_name_gem_objs(struct drm_framebuffer *fb,
				  struct drm_gem_object **objs)
{
	int i;
	struct drm_device *dev = fb->dev;

	for (i = 0; i < nulldisp_drm_fb_num_planes(fb); i++) {
		int err = nlpvrdpy_name_gem_obj(dev, objs[i]);

		if (err < 0)
			return err;
	}

	return 0;
}

int nlpvrdpy_send_flip(struct nlpvrdpy *nlpvrdpy,
		       struct drm_framebuffer *fb,
		       struct drm_gem_object **objs)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	if (!atomic_read(&nlpvrdpy->connected))
		return -ENOTCONN;

	if (nlpvrdpy->gem_names_required) {
		err = nlpvrdpy_name_gem_objs(fb, objs);
		if (err)
			return err;
	}

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, nlpvrdpy->dst_portid, 0,
				&nlpvrdpy_family, 0, NLPVRDPY_CMD_FLIP);
	if (!hdr) {
		err = -ENOMEM;
		goto err_msg_free;
	}

	err = nlpvrdpy_put_fb_attributes(msg, fb, nlpvrdpy, objs);
	if (err)
		goto err_msg_free;

	genlmsg_end(msg, hdr);

	return nlpvrdpy_send_msg(nlpvrdpy, msg);

err_msg_free:
	nlmsg_free(msg);
	return err;
}

int nlpvrdpy_send_copy(struct nlpvrdpy *nlpvrdpy,
		       struct drm_framebuffer *fb,
		       struct drm_gem_object **objs)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	if (!atomic_read(&nlpvrdpy->connected))
		return -ENOTCONN;

	if (nlpvrdpy->gem_names_required) {
		err = nlpvrdpy_name_gem_objs(fb, objs);
		if (err)
			return err;
	}

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, nlpvrdpy->dst_portid, 0,
				&nlpvrdpy_family, 0, NLPVRDPY_CMD_COPY);
	if (!hdr) {
		err = -ENOMEM;
		goto err_msg_free;
	}

	err = nlpvrdpy_put_fb_attributes(msg, fb, nlpvrdpy, objs);
	if (err)
		goto err_msg_free;

	genlmsg_end(msg, hdr);

	return nlpvrdpy_send_msg(nlpvrdpy, msg);

err_msg_free:
	nlmsg_free(msg);
	return err;
}

static int nlpvrdpy_cmd_connect(struct sk_buff *skb, struct genl_info *info)
{
	struct nlpvrdpy *nlpvrdpy = info->user_ptr[0];
	struct sk_buff *msg;
	void *hdr;
	int err;

	if (info->attrs[NLPVRDPY_ATTR_NAMING_REQUIRED])
		nlpvrdpy->gem_names_required = true;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put_reply(msg, info, &nlpvrdpy_family,
						0, NLPVRDPY_CMD_CONNECTED);
	if (!hdr) {
		err = -ENOMEM;
		goto err_msg_free;
	}

	err = nla_put_string(msg, NLPVRDPY_ATTR_NAME,
				nlpvrdpy->dev->driver->name);
	if (err)
		goto err_msg_free;

	genlmsg_end(msg, hdr);

	err = genlmsg_reply(msg, info);

	if (!err) {
		nlpvrdpy_lock(nlpvrdpy);

		nlpvrdpy->net = genl_info_net(info);
		nlpvrdpy->dst_portid = info->snd_portid;
		atomic_set(&nlpvrdpy->connected, 1);

		nlpvrdpy_unlock(nlpvrdpy);
	}

	return err;

err_msg_free:
	nlmsg_free(msg);
	return err;
}

static int nlpvrdpy_cmd_disconnect(struct sk_buff *skb, struct genl_info *info)
{
	struct nlpvrdpy *nlpvrdpy = info->user_ptr[0];

	atomic_set(&nlpvrdpy->connected, 0);

	if (nlpvrdpy->disconnect_cb)
		nlpvrdpy->disconnect_cb(nlpvrdpy->disconnect_cb_data);

	return 0;
}

static int nlpvrdpy_cmd_flipped(struct sk_buff *skb, struct genl_info *info)
{
	struct nlpvrdpy *nlpvrdpy = info->user_ptr[0];

	return (nlpvrdpy->flipped_cb) ?
			nlpvrdpy->flipped_cb(nlpvrdpy->flipped_cb_data) :
			0;
}

static int nlpvrdpy_cmd_copied(struct sk_buff *skb, struct genl_info *info)
{
	struct nlpvrdpy *nlpvrdpy = info->user_ptr[0];

	return (nlpvrdpy->copied_cb) ?
			nlpvrdpy->copied_cb(nlpvrdpy->copied_cb_data) :
			0;
}

static struct genl_ops nlpvrdpy_ops[] = {
	{
		.cmd = NLPVRDPY_CMD_CONNECT,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		.policy = nlpvrdpy_policy,
#endif
		.doit = nlpvrdpy_cmd_connect,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NLPVRDPY_CIF_NLPVRDPY_NOT_CONNECTED
	},
	{
		.cmd = NLPVRDPY_CMD_DISCONNECT,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		.policy = nlpvrdpy_policy,
#endif
		.doit = nlpvrdpy_cmd_disconnect,
		.flags = 0,
		.internal_flags = NLPVRDPY_CIF_NLPVRDPY
	},
	{
		.cmd = NLPVRDPY_CMD_FLIPPED,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		.policy = nlpvrdpy_policy,
#endif
		.doit = nlpvrdpy_cmd_flipped,
		.flags = 0,
		.internal_flags = NLPVRDPY_CIF_NLPVRDPY
	},
	{
		.cmd = NLPVRDPY_CMD_COPIED,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		.policy = nlpvrdpy_policy,
#endif
		.doit = nlpvrdpy_cmd_copied,
		.flags = 0,
		.internal_flags = NLPVRDPY_CIF_NLPVRDPY
	}
};

int nlpvrdpy_register(void)
{
	nlpvrdpy_family.module = THIS_MODULE;
	nlpvrdpy_family.ops = nlpvrdpy_ops;
	nlpvrdpy_family.n_ops = ARRAY_SIZE(nlpvrdpy_ops);

	return genl_register_family(&nlpvrdpy_family);
}

int nlpvrdpy_unregister(void)
{
	return genl_unregister_family(&nlpvrdpy_family);
}
