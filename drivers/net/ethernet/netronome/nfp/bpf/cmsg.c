/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/wait.h>

#include "../nfp_app.h"
#include "../nfp_net.h"
#include "fw.h"
#include "main.h"

#define cmsg_warn(bpf, msg...)	nn_dp_warn(&(bpf)->app->ctrl->dp, msg)

#define NFP_BPF_TAG_ALLOC_SPAN	(U16_MAX / 4)

static bool nfp_bpf_all_tags_busy(struct nfp_app_bpf *bpf)
{
	u16 used_tags;

	used_tags = bpf->tag_alloc_next - bpf->tag_alloc_last;

	return used_tags > NFP_BPF_TAG_ALLOC_SPAN;
}

static int nfp_bpf_alloc_tag(struct nfp_app_bpf *bpf)
{
	/* All FW communication for BPF is request-reply.  To make sure we
	 * don't reuse the message ID too early after timeout - limit the
	 * number of requests in flight.
	 */
	if (nfp_bpf_all_tags_busy(bpf)) {
		cmsg_warn(bpf, "all FW request contexts busy!\n");
		return -EAGAIN;
	}

	WARN_ON(__test_and_set_bit(bpf->tag_alloc_next, bpf->tag_allocator));
	return bpf->tag_alloc_next++;
}

static void nfp_bpf_free_tag(struct nfp_app_bpf *bpf, u16 tag)
{
	WARN_ON(!__test_and_clear_bit(tag, bpf->tag_allocator));

	while (!test_bit(bpf->tag_alloc_last, bpf->tag_allocator) &&
	       bpf->tag_alloc_last != bpf->tag_alloc_next)
		bpf->tag_alloc_last++;
}

static unsigned int nfp_bpf_cmsg_get_tag(struct sk_buff *skb)
{
	struct cmsg_hdr *hdr;

	hdr = (struct cmsg_hdr *)skb->data;

	return be16_to_cpu(hdr->tag);
}

static struct sk_buff *__nfp_bpf_reply(struct nfp_app_bpf *bpf, u16 tag)
{
	unsigned int msg_tag;
	struct sk_buff *skb;

	skb_queue_walk(&bpf->cmsg_replies, skb) {
		msg_tag = nfp_bpf_cmsg_get_tag(skb);
		if (msg_tag == tag) {
			nfp_bpf_free_tag(bpf, tag);
			__skb_unlink(skb, &bpf->cmsg_replies);
			return skb;
		}
	}

	return NULL;
}

static struct sk_buff *nfp_bpf_reply(struct nfp_app_bpf *bpf, u16 tag)
{
	struct sk_buff *skb;

	nfp_ctrl_lock(bpf->app->ctrl);
	skb = __nfp_bpf_reply(bpf, tag);
	nfp_ctrl_unlock(bpf->app->ctrl);

	return skb;
}

static struct sk_buff *nfp_bpf_reply_drop_tag(struct nfp_app_bpf *bpf, u16 tag)
{
	struct sk_buff *skb;

	nfp_ctrl_lock(bpf->app->ctrl);
	skb = __nfp_bpf_reply(bpf, tag);
	if (!skb)
		nfp_bpf_free_tag(bpf, tag);
	nfp_ctrl_unlock(bpf->app->ctrl);

	return skb;
}

static struct sk_buff *
nfp_bpf_cmsg_wait_reply(struct nfp_app_bpf *bpf, enum nfp_bpf_cmsg_type type,
			int tag)
{
	struct sk_buff *skb;
	int err;

	err = wait_event_interruptible_timeout(bpf->cmsg_wq,
					       skb = nfp_bpf_reply(bpf, tag),
					       msecs_to_jiffies(5000));
	/* We didn't get a response - try last time and atomically drop
	 * the tag even if no response is matched.
	 */
	if (!skb)
		skb = nfp_bpf_reply_drop_tag(bpf, tag);
	if (err < 0) {
		cmsg_warn(bpf, "%s waiting for response to 0x%02x: %d\n",
			  err == ERESTARTSYS ? "interrupted" : "error",
			  type, err);
		return ERR_PTR(err);
	}
	if (!skb) {
		cmsg_warn(bpf, "timeout waiting for response to 0x%02x\n",
			  type);
		return ERR_PTR(-ETIMEDOUT);
	}

	return skb;
}

struct sk_buff *
nfp_bpf_cmsg_communicate(struct nfp_app_bpf *bpf, struct sk_buff *skb,
			 enum nfp_bpf_cmsg_type type, unsigned int reply_size)
{
	struct cmsg_hdr *hdr;
	int tag;

	nfp_ctrl_lock(bpf->app->ctrl);
	tag = nfp_bpf_alloc_tag(bpf);
	if (tag < 0) {
		nfp_ctrl_unlock(bpf->app->ctrl);
		dev_kfree_skb_any(skb);
		return ERR_PTR(tag);
	}

	hdr = (void *)skb->data;
	hdr->ver = CMSG_MAP_ABI_VERSION;
	hdr->type = type;
	hdr->tag = cpu_to_be16(tag);

	__nfp_app_ctrl_tx(bpf->app, skb);

	nfp_ctrl_unlock(bpf->app->ctrl);

	skb = nfp_bpf_cmsg_wait_reply(bpf, type, tag);
	if (IS_ERR(skb))
		return skb;

	hdr = (struct cmsg_hdr *)skb->data;
	/* 0 reply_size means caller will do the validation */
	if (reply_size && skb->len != reply_size) {
		cmsg_warn(bpf, "cmsg drop - wrong size %d != %d!\n",
			  skb->len, reply_size);
		goto err_free;
	}
	if (hdr->type != __CMSG_REPLY(type)) {
		cmsg_warn(bpf, "cmsg drop - wrong type 0x%02x != 0x%02lx!\n",
			  hdr->type, __CMSG_REPLY(type));
		goto err_free;
	}

	return skb;
err_free:
	dev_kfree_skb_any(skb);
	return ERR_PTR(-EIO);
}

void nfp_bpf_ctrl_msg_rx(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_app_bpf *bpf = app->priv;
	unsigned int tag;

	if (unlikely(skb->len < sizeof(struct cmsg_reply_map_simple))) {
		cmsg_warn(bpf, "cmsg drop - too short %d!\n", skb->len);
		goto err_free;
	}

	nfp_ctrl_lock(bpf->app->ctrl);

	tag = nfp_bpf_cmsg_get_tag(skb);
	if (unlikely(!test_bit(tag, bpf->tag_allocator))) {
		cmsg_warn(bpf, "cmsg drop - no one is waiting for tag %u!\n",
			  tag);
		goto err_unlock;
	}

	__skb_queue_tail(&bpf->cmsg_replies, skb);
	wake_up_interruptible_all(&bpf->cmsg_wq);

	nfp_ctrl_unlock(bpf->app->ctrl);

	return;
err_unlock:
	nfp_ctrl_unlock(bpf->app->ctrl);
err_free:
	dev_kfree_skb_any(skb);
}
