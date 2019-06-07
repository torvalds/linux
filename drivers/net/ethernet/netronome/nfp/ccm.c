// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2016-2019 Netronome Systems, Inc. */

#include <linux/bitops.h>

#include "ccm.h"
#include "nfp_app.h"
#include "nfp_net.h"

#define NFP_CCM_TYPE_REPLY_BIT		7
#define __NFP_CCM_REPLY(req)		(BIT(NFP_CCM_TYPE_REPLY_BIT) | (req))

#define ccm_warn(app, msg...)	nn_dp_warn(&(app)->ctrl->dp, msg)

#define NFP_CCM_TAG_ALLOC_SPAN	(U16_MAX / 4)

static bool nfp_ccm_all_tags_busy(struct nfp_ccm *ccm)
{
	u16 used_tags;

	used_tags = ccm->tag_alloc_next - ccm->tag_alloc_last;

	return used_tags > NFP_CCM_TAG_ALLOC_SPAN;
}

static int nfp_ccm_alloc_tag(struct nfp_ccm *ccm)
{
	/* CCM is for FW communication which is request-reply.  To make sure
	 * we don't reuse the message ID too early after timeout - limit the
	 * number of requests in flight.
	 */
	if (unlikely(nfp_ccm_all_tags_busy(ccm))) {
		ccm_warn(ccm->app, "all FW request contexts busy!\n");
		return -EAGAIN;
	}

	WARN_ON(__test_and_set_bit(ccm->tag_alloc_next, ccm->tag_allocator));
	return ccm->tag_alloc_next++;
}

static void nfp_ccm_free_tag(struct nfp_ccm *ccm, u16 tag)
{
	WARN_ON(!__test_and_clear_bit(tag, ccm->tag_allocator));

	while (!test_bit(ccm->tag_alloc_last, ccm->tag_allocator) &&
	       ccm->tag_alloc_last != ccm->tag_alloc_next)
		ccm->tag_alloc_last++;
}

static struct sk_buff *__nfp_ccm_reply(struct nfp_ccm *ccm, u16 tag)
{
	unsigned int msg_tag;
	struct sk_buff *skb;

	skb_queue_walk(&ccm->replies, skb) {
		msg_tag = nfp_ccm_get_tag(skb);
		if (msg_tag == tag) {
			nfp_ccm_free_tag(ccm, tag);
			__skb_unlink(skb, &ccm->replies);
			return skb;
		}
	}

	return NULL;
}

static struct sk_buff *
nfp_ccm_reply(struct nfp_ccm *ccm, struct nfp_app *app, u16 tag)
{
	struct sk_buff *skb;

	nfp_ctrl_lock(app->ctrl);
	skb = __nfp_ccm_reply(ccm, tag);
	nfp_ctrl_unlock(app->ctrl);

	return skb;
}

static struct sk_buff *
nfp_ccm_reply_drop_tag(struct nfp_ccm *ccm, struct nfp_app *app, u16 tag)
{
	struct sk_buff *skb;

	nfp_ctrl_lock(app->ctrl);
	skb = __nfp_ccm_reply(ccm, tag);
	if (!skb)
		nfp_ccm_free_tag(ccm, tag);
	nfp_ctrl_unlock(app->ctrl);

	return skb;
}

static struct sk_buff *
nfp_ccm_wait_reply(struct nfp_ccm *ccm, struct nfp_app *app,
		   enum nfp_ccm_type type, int tag)
{
	struct sk_buff *skb;
	int i, err;

	for (i = 0; i < 50; i++) {
		udelay(4);
		skb = nfp_ccm_reply(ccm, app, tag);
		if (skb)
			return skb;
	}

	err = wait_event_interruptible_timeout(ccm->wq,
					       skb = nfp_ccm_reply(ccm, app,
								   tag),
					       msecs_to_jiffies(5000));
	/* We didn't get a response - try last time and atomically drop
	 * the tag even if no response is matched.
	 */
	if (!skb)
		skb = nfp_ccm_reply_drop_tag(ccm, app, tag);
	if (err < 0) {
		ccm_warn(app, "%s waiting for response to 0x%02x: %d\n",
			 err == ERESTARTSYS ? "interrupted" : "error",
			 type, err);
		return ERR_PTR(err);
	}
	if (!skb) {
		ccm_warn(app, "timeout waiting for response to 0x%02x\n", type);
		return ERR_PTR(-ETIMEDOUT);
	}

	return skb;
}

struct sk_buff *
nfp_ccm_communicate(struct nfp_ccm *ccm, struct sk_buff *skb,
		    enum nfp_ccm_type type, unsigned int reply_size)
{
	struct nfp_app *app = ccm->app;
	struct nfp_ccm_hdr *hdr;
	int reply_type, tag;

	nfp_ctrl_lock(app->ctrl);
	tag = nfp_ccm_alloc_tag(ccm);
	if (tag < 0) {
		nfp_ctrl_unlock(app->ctrl);
		dev_kfree_skb_any(skb);
		return ERR_PTR(tag);
	}

	hdr = (void *)skb->data;
	hdr->ver = NFP_CCM_ABI_VERSION;
	hdr->type = type;
	hdr->tag = cpu_to_be16(tag);

	__nfp_app_ctrl_tx(app, skb);

	nfp_ctrl_unlock(app->ctrl);

	skb = nfp_ccm_wait_reply(ccm, app, type, tag);
	if (IS_ERR(skb))
		return skb;

	reply_type = nfp_ccm_get_type(skb);
	if (reply_type != __NFP_CCM_REPLY(type)) {
		ccm_warn(app, "cmsg drop - wrong type 0x%02x != 0x%02lx!\n",
			 reply_type, __NFP_CCM_REPLY(type));
		goto err_free;
	}
	/* 0 reply_size means caller will do the validation */
	if (reply_size && skb->len != reply_size) {
		ccm_warn(app, "cmsg drop - type 0x%02x wrong size %d != %d!\n",
			 type, skb->len, reply_size);
		goto err_free;
	}

	return skb;
err_free:
	dev_kfree_skb_any(skb);
	return ERR_PTR(-EIO);
}

void nfp_ccm_rx(struct nfp_ccm *ccm, struct sk_buff *skb)
{
	struct nfp_app *app = ccm->app;
	unsigned int tag;

	if (unlikely(skb->len < sizeof(struct nfp_ccm_hdr))) {
		ccm_warn(app, "cmsg drop - too short %d!\n", skb->len);
		goto err_free;
	}

	nfp_ctrl_lock(app->ctrl);

	tag = nfp_ccm_get_tag(skb);
	if (unlikely(!test_bit(tag, ccm->tag_allocator))) {
		ccm_warn(app, "cmsg drop - no one is waiting for tag %u!\n",
			 tag);
		goto err_unlock;
	}

	__skb_queue_tail(&ccm->replies, skb);
	wake_up_interruptible_all(&ccm->wq);

	nfp_ctrl_unlock(app->ctrl);
	return;

err_unlock:
	nfp_ctrl_unlock(app->ctrl);
err_free:
	dev_kfree_skb_any(skb);
}

int nfp_ccm_init(struct nfp_ccm *ccm, struct nfp_app *app)
{
	ccm->app = app;
	skb_queue_head_init(&ccm->replies);
	init_waitqueue_head(&ccm->wq);
	return 0;
}

void nfp_ccm_clean(struct nfp_ccm *ccm)
{
	WARN_ON(!skb_queue_empty(&ccm->replies));
}
