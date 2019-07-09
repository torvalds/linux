// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/skbuff.h>

#include "ccm.h"
#include "nfp_net.h"

/* CCM messages via the mailbox.  CMSGs get wrapped into simple TLVs
 * and copied into the mailbox.  Multiple messages can be copied to
 * form a batch.  Threads come in with CMSG formed in an skb, then
 * enqueue that skb onto the request queue.  If threads skb is first
 * in queue this thread will handle the mailbox operation.  It copies
 * up to 64 messages into the mailbox (making sure that both requests
 * and replies will fit.  After FW is done processing the batch it
 * copies the data out and wakes waiting threads.
 * If a thread is waiting it either gets its the message completed
 * (response is copied into the same skb as the request, overwriting
 * it), or becomes the first in queue.
 * Completions and next-to-run are signaled via the control buffer
 * to limit potential cache line bounces.
 */

#define NFP_CCM_MBOX_BATCH_LIMIT	64
#define NFP_CCM_TIMEOUT			(NFP_NET_POLL_TIMEOUT * 1000)
#define NFP_CCM_MAX_QLEN		1024

enum nfp_net_mbox_cmsg_state {
	NFP_NET_MBOX_CMSG_STATE_QUEUED,
	NFP_NET_MBOX_CMSG_STATE_NEXT,
	NFP_NET_MBOX_CMSG_STATE_BUSY,
	NFP_NET_MBOX_CMSG_STATE_REPLY_FOUND,
	NFP_NET_MBOX_CMSG_STATE_DONE,
};

/**
 * struct nfp_ccm_mbox_skb_cb - CCM mailbox specific info
 * @state:	processing state (/stage) of the message
 * @err:	error encountered during processing if any
 * @max_len:	max(request_len, reply_len)
 * @exp_reply:	expected reply length (0 means don't validate)
 * @posted:	the message was posted and nobody waits for the reply
 */
struct nfp_ccm_mbox_cmsg_cb {
	enum nfp_net_mbox_cmsg_state state;
	int err;
	unsigned int max_len;
	unsigned int exp_reply;
	bool posted;
};

static u32 nfp_ccm_mbox_max_msg(struct nfp_net *nn)
{
	return round_down(nn->tlv_caps.mbox_len, 4) -
		NFP_NET_CFG_MBOX_SIMPLE_VAL - /* common mbox command header */
		4 * 2; /* Msg TLV plus End TLV headers */
}

static void
nfp_ccm_mbox_msg_init(struct sk_buff *skb, unsigned int exp_reply, int max_len)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	cb->state = NFP_NET_MBOX_CMSG_STATE_QUEUED;
	cb->err = 0;
	cb->max_len = max_len;
	cb->exp_reply = exp_reply;
	cb->posted = false;
}

static int nfp_ccm_mbox_maxlen(const struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	return cb->max_len;
}

static bool nfp_ccm_mbox_done(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	return cb->state == NFP_NET_MBOX_CMSG_STATE_DONE;
}

static bool nfp_ccm_mbox_in_progress(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	return cb->state != NFP_NET_MBOX_CMSG_STATE_QUEUED &&
	       cb->state != NFP_NET_MBOX_CMSG_STATE_NEXT;
}

static void nfp_ccm_mbox_set_busy(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	cb->state = NFP_NET_MBOX_CMSG_STATE_BUSY;
}

static bool nfp_ccm_mbox_is_posted(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	return cb->posted;
}

static void nfp_ccm_mbox_mark_posted(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	cb->posted = true;
}

static bool nfp_ccm_mbox_is_first(struct nfp_net *nn, struct sk_buff *skb)
{
	return skb_queue_is_first(&nn->mbox_cmsg.queue, skb);
}

static bool nfp_ccm_mbox_should_run(struct nfp_net *nn, struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	return cb->state == NFP_NET_MBOX_CMSG_STATE_NEXT;
}

static void nfp_ccm_mbox_mark_next_runner(struct nfp_net *nn)
{
	struct nfp_ccm_mbox_cmsg_cb *cb;
	struct sk_buff *skb;

	skb = skb_peek(&nn->mbox_cmsg.queue);
	if (!skb)
		return;

	cb = (void *)skb->cb;
	cb->state = NFP_NET_MBOX_CMSG_STATE_NEXT;
	if (cb->posted)
		queue_work(nn->mbox_cmsg.workq, &nn->mbox_cmsg.runq_work);
}

static void
nfp_ccm_mbox_write_tlv(struct nfp_net *nn, u32 off, u32 type, u32 len)
{
	nn_writel(nn, off,
		  FIELD_PREP(NFP_NET_MBOX_TLV_TYPE, type) |
		  FIELD_PREP(NFP_NET_MBOX_TLV_LEN, len));
}

static void nfp_ccm_mbox_copy_in(struct nfp_net *nn, struct sk_buff *last)
{
	struct sk_buff *skb;
	int reserve, i, cnt;
	__be32 *data;
	u32 off, len;

	off = nn->tlv_caps.mbox_off + NFP_NET_CFG_MBOX_SIMPLE_VAL;
	skb = __skb_peek(&nn->mbox_cmsg.queue);
	while (true) {
		nfp_ccm_mbox_write_tlv(nn, off, NFP_NET_MBOX_TLV_TYPE_MSG,
				       skb->len);
		off += 4;

		/* Write data word by word, skb->data should be aligned */
		data = (__be32 *)skb->data;
		cnt = skb->len / 4;
		for (i = 0 ; i < cnt; i++) {
			nn_writel(nn, off, be32_to_cpu(data[i]));
			off += 4;
		}
		if (skb->len & 3) {
			__be32 tmp = 0;

			memcpy(&tmp, &data[i], skb->len & 3);
			nn_writel(nn, off, be32_to_cpu(tmp));
			off += 4;
		}

		/* Reserve space if reply is bigger */
		len = round_up(skb->len, 4);
		reserve = nfp_ccm_mbox_maxlen(skb) - len;
		if (reserve > 0) {
			nfp_ccm_mbox_write_tlv(nn, off,
					       NFP_NET_MBOX_TLV_TYPE_RESV,
					       reserve);
			off += 4 + reserve;
		}

		if (skb == last)
			break;
		skb = skb_queue_next(&nn->mbox_cmsg.queue, skb);
	}

	nfp_ccm_mbox_write_tlv(nn, off, NFP_NET_MBOX_TLV_TYPE_END, 0);
}

static struct sk_buff *
nfp_ccm_mbox_find_req(struct nfp_net *nn, __be16 tag, struct sk_buff *last)
{
	struct sk_buff *skb;

	skb = __skb_peek(&nn->mbox_cmsg.queue);
	while (true) {
		if (__nfp_ccm_get_tag(skb) == tag)
			return skb;

		if (skb == last)
			return NULL;
		skb = skb_queue_next(&nn->mbox_cmsg.queue, skb);
	}
}

static void nfp_ccm_mbox_copy_out(struct nfp_net *nn, struct sk_buff *last)
{
	struct nfp_ccm_mbox_cmsg_cb *cb;
	u8 __iomem *data, *end;
	struct sk_buff *skb;

	data = nn->dp.ctrl_bar + nn->tlv_caps.mbox_off +
		NFP_NET_CFG_MBOX_SIMPLE_VAL;
	end = data + nn->tlv_caps.mbox_len;

	while (true) {
		unsigned int length, offset, type;
		struct nfp_ccm_hdr hdr;
		u32 tlv_hdr;

		tlv_hdr = readl(data);
		type = FIELD_GET(NFP_NET_MBOX_TLV_TYPE, tlv_hdr);
		length = FIELD_GET(NFP_NET_MBOX_TLV_LEN, tlv_hdr);
		offset = data - nn->dp.ctrl_bar;

		/* Advance past the header */
		data += 4;

		if (data + length > end) {
			nn_dp_warn(&nn->dp, "mailbox oversized TLV type:%d offset:%u len:%u\n",
				   type, offset, length);
			break;
		}

		if (type == NFP_NET_MBOX_TLV_TYPE_END)
			break;
		if (type == NFP_NET_MBOX_TLV_TYPE_RESV)
			goto next_tlv;
		if (type != NFP_NET_MBOX_TLV_TYPE_MSG &&
		    type != NFP_NET_MBOX_TLV_TYPE_MSG_NOSUP) {
			nn_dp_warn(&nn->dp, "mailbox unknown TLV type:%d offset:%u len:%u\n",
				   type, offset, length);
			break;
		}

		if (length < 4) {
			nn_dp_warn(&nn->dp, "mailbox msg too short to contain header TLV type:%d offset:%u len:%u\n",
				   type, offset, length);
			break;
		}

		hdr.raw = cpu_to_be32(readl(data));

		skb = nfp_ccm_mbox_find_req(nn, hdr.tag, last);
		if (!skb) {
			nn_dp_warn(&nn->dp, "mailbox request not found:%u\n",
				   be16_to_cpu(hdr.tag));
			break;
		}
		cb = (void *)skb->cb;

		if (type == NFP_NET_MBOX_TLV_TYPE_MSG_NOSUP) {
			nn_dp_warn(&nn->dp,
				   "mailbox msg not supported type:%d\n",
				   nfp_ccm_get_type(skb));
			cb->err = -EIO;
			goto next_tlv;
		}

		if (hdr.type != __NFP_CCM_REPLY(nfp_ccm_get_type(skb))) {
			nn_dp_warn(&nn->dp, "mailbox msg reply wrong type:%u expected:%lu\n",
				   hdr.type,
				   __NFP_CCM_REPLY(nfp_ccm_get_type(skb)));
			cb->err = -EIO;
			goto next_tlv;
		}
		if (cb->exp_reply && length != cb->exp_reply) {
			nn_dp_warn(&nn->dp, "mailbox msg reply wrong size type:%u expected:%u have:%u\n",
				   hdr.type, length, cb->exp_reply);
			cb->err = -EIO;
			goto next_tlv;
		}
		if (length > cb->max_len) {
			nn_dp_warn(&nn->dp, "mailbox msg oversized reply type:%u max:%u have:%u\n",
				   hdr.type, cb->max_len, length);
			cb->err = -EIO;
			goto next_tlv;
		}

		if (!cb->posted) {
			__be32 *skb_data;
			int i, cnt;

			if (length <= skb->len)
				__skb_trim(skb, length);
			else
				skb_put(skb, length - skb->len);

			/* We overcopy here slightly, but that's okay,
			 * the skb is large enough, and the garbage will
			 * be ignored (beyond skb->len).
			 */
			skb_data = (__be32 *)skb->data;
			memcpy(skb_data, &hdr, 4);

			cnt = DIV_ROUND_UP(length, 4);
			for (i = 1 ; i < cnt; i++)
				skb_data[i] = cpu_to_be32(readl(data + i * 4));
		}

		cb->state = NFP_NET_MBOX_CMSG_STATE_REPLY_FOUND;
next_tlv:
		data += round_up(length, 4);
		if (data + 4 > end) {
			nn_dp_warn(&nn->dp,
				   "reached end of MBOX without END TLV\n");
			break;
		}
	}

	smp_wmb(); /* order the skb->data vs. cb->state */
	spin_lock_bh(&nn->mbox_cmsg.queue.lock);
	do {
		skb = __skb_dequeue(&nn->mbox_cmsg.queue);
		cb = (void *)skb->cb;

		if (cb->state != NFP_NET_MBOX_CMSG_STATE_REPLY_FOUND) {
			cb->err = -ENOENT;
			smp_wmb(); /* order the cb->err vs. cb->state */
		}
		cb->state = NFP_NET_MBOX_CMSG_STATE_DONE;

		if (cb->posted) {
			if (cb->err)
				nn_dp_warn(&nn->dp,
					   "mailbox posted msg failed type:%u err:%d\n",
					   nfp_ccm_get_type(skb), cb->err);
			dev_consume_skb_any(skb);
		}
	} while (skb != last);

	nfp_ccm_mbox_mark_next_runner(nn);
	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);
}

static void
nfp_ccm_mbox_mark_all_err(struct nfp_net *nn, struct sk_buff *last, int err)
{
	struct nfp_ccm_mbox_cmsg_cb *cb;
	struct sk_buff *skb;

	spin_lock_bh(&nn->mbox_cmsg.queue.lock);
	do {
		skb = __skb_dequeue(&nn->mbox_cmsg.queue);
		cb = (void *)skb->cb;

		cb->err = err;
		smp_wmb(); /* order the cb->err vs. cb->state */
		cb->state = NFP_NET_MBOX_CMSG_STATE_DONE;
	} while (skb != last);

	nfp_ccm_mbox_mark_next_runner(nn);
	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);
}

static void nfp_ccm_mbox_run_queue_unlock(struct nfp_net *nn)
	__releases(&nn->mbox_cmsg.queue.lock)
{
	int space = nn->tlv_caps.mbox_len - NFP_NET_CFG_MBOX_SIMPLE_VAL;
	struct sk_buff *skb, *last;
	int cnt, err;

	space -= 4; /* for End TLV */

	/* First skb must fit, because it's ours and we checked it fits */
	cnt = 1;
	last = skb = __skb_peek(&nn->mbox_cmsg.queue);
	space -= 4 + nfp_ccm_mbox_maxlen(skb);

	while (!skb_queue_is_last(&nn->mbox_cmsg.queue, last)) {
		skb = skb_queue_next(&nn->mbox_cmsg.queue, last);
		space -= 4 + nfp_ccm_mbox_maxlen(skb);
		if (space < 0)
			break;
		last = skb;
		nfp_ccm_mbox_set_busy(skb);
		cnt++;
		if (cnt == NFP_CCM_MBOX_BATCH_LIMIT)
			break;
	}
	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);

	/* Now we own all skb's marked in progress, new requests may arrive
	 * at the end of the queue.
	 */

	nn_ctrl_bar_lock(nn);

	nfp_ccm_mbox_copy_in(nn, last);

	err = nfp_net_mbox_reconfig(nn, NFP_NET_CFG_MBOX_CMD_TLV_CMSG);
	if (!err)
		nfp_ccm_mbox_copy_out(nn, last);
	else
		nfp_ccm_mbox_mark_all_err(nn, last, -EIO);

	nn_ctrl_bar_unlock(nn);

	wake_up_all(&nn->mbox_cmsg.wq);
}

static int nfp_ccm_mbox_skb_return(struct sk_buff *skb)
{
	struct nfp_ccm_mbox_cmsg_cb *cb = (void *)skb->cb;

	if (cb->err)
		dev_kfree_skb_any(skb);
	return cb->err;
}

/* If wait timed out but the command is already in progress we have
 * to wait until it finishes.  Runners has ownership of the skbs marked
 * as busy.
 */
static int
nfp_ccm_mbox_unlink_unlock(struct nfp_net *nn, struct sk_buff *skb,
			   enum nfp_ccm_type type)
	__releases(&nn->mbox_cmsg.queue.lock)
{
	bool was_first;

	if (nfp_ccm_mbox_in_progress(skb)) {
		spin_unlock_bh(&nn->mbox_cmsg.queue.lock);

		wait_event(nn->mbox_cmsg.wq, nfp_ccm_mbox_done(skb));
		smp_rmb(); /* pairs with smp_wmb() after data is written */
		return nfp_ccm_mbox_skb_return(skb);
	}

	was_first = nfp_ccm_mbox_should_run(nn, skb);
	__skb_unlink(skb, &nn->mbox_cmsg.queue);
	if (was_first)
		nfp_ccm_mbox_mark_next_runner(nn);

	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);

	if (was_first)
		wake_up_all(&nn->mbox_cmsg.wq);

	nn_dp_warn(&nn->dp, "time out waiting for mbox response to 0x%02x\n",
		   type);
	return -ETIMEDOUT;
}

static int
nfp_ccm_mbox_msg_prepare(struct nfp_net *nn, struct sk_buff *skb,
			 enum nfp_ccm_type type,
			 unsigned int reply_size, unsigned int max_reply_size,
			 gfp_t flags)
{
	const unsigned int mbox_max = nfp_ccm_mbox_max_msg(nn);
	unsigned int max_len;
	ssize_t undersize;
	int err;

	if (unlikely(!(nn->tlv_caps.mbox_cmsg_types & BIT(type)))) {
		nn_dp_warn(&nn->dp,
			   "message type %d not supported by mailbox\n", type);
		return -EINVAL;
	}

	/* If the reply size is unknown assume it will take the entire
	 * mailbox, the callers should do their best for this to never
	 * happen.
	 */
	if (!max_reply_size)
		max_reply_size = mbox_max;
	max_reply_size = round_up(max_reply_size, 4);

	/* Make sure we can fit the entire reply into the skb,
	 * and that we don't have to slow down the mbox handler
	 * with allocations.
	 */
	undersize = max_reply_size - (skb_end_pointer(skb) - skb->data);
	if (undersize > 0) {
		err = pskb_expand_head(skb, 0, undersize, flags);
		if (err) {
			nn_dp_warn(&nn->dp,
				   "can't allocate reply buffer for mailbox\n");
			return err;
		}
	}

	/* Make sure that request and response both fit into the mailbox */
	max_len = max(max_reply_size, round_up(skb->len, 4));
	if (max_len > mbox_max) {
		nn_dp_warn(&nn->dp,
			   "message too big for tha mailbox: %u/%u vs %u\n",
			   skb->len, max_reply_size, mbox_max);
		return -EMSGSIZE;
	}

	nfp_ccm_mbox_msg_init(skb, reply_size, max_len);

	return 0;
}

static int
nfp_ccm_mbox_msg_enqueue(struct nfp_net *nn, struct sk_buff *skb,
			 enum nfp_ccm_type type, bool critical)
{
	struct nfp_ccm_hdr *hdr;

	assert_spin_locked(&nn->mbox_cmsg.queue.lock);

	if (!critical && nn->mbox_cmsg.queue.qlen >= NFP_CCM_MAX_QLEN) {
		nn_dp_warn(&nn->dp, "mailbox request queue too long\n");
		return -EBUSY;
	}

	hdr = (void *)skb->data;
	hdr->ver = NFP_CCM_ABI_VERSION;
	hdr->type = type;
	hdr->tag = cpu_to_be16(nn->mbox_cmsg.tag++);

	__skb_queue_tail(&nn->mbox_cmsg.queue, skb);

	return 0;
}

int __nfp_ccm_mbox_communicate(struct nfp_net *nn, struct sk_buff *skb,
			       enum nfp_ccm_type type,
			       unsigned int reply_size,
			       unsigned int max_reply_size, bool critical)
{
	int err;

	err = nfp_ccm_mbox_msg_prepare(nn, skb, type, reply_size,
				       max_reply_size, GFP_KERNEL);
	if (err)
		goto err_free_skb;

	spin_lock_bh(&nn->mbox_cmsg.queue.lock);

	err = nfp_ccm_mbox_msg_enqueue(nn, skb, type, critical);
	if (err)
		goto err_unlock;

	/* First in queue takes the mailbox lock and processes the batch */
	if (!nfp_ccm_mbox_is_first(nn, skb)) {
		bool to;

		spin_unlock_bh(&nn->mbox_cmsg.queue.lock);

		to = !wait_event_timeout(nn->mbox_cmsg.wq,
					 nfp_ccm_mbox_done(skb) ||
					 nfp_ccm_mbox_should_run(nn, skb),
					 msecs_to_jiffies(NFP_CCM_TIMEOUT));

		/* fast path for those completed by another thread */
		if (nfp_ccm_mbox_done(skb)) {
			smp_rmb(); /* pairs with wmb after data is written */
			return nfp_ccm_mbox_skb_return(skb);
		}

		spin_lock_bh(&nn->mbox_cmsg.queue.lock);

		if (!nfp_ccm_mbox_is_first(nn, skb)) {
			WARN_ON(!to);

			err = nfp_ccm_mbox_unlink_unlock(nn, skb, type);
			if (err)
				goto err_free_skb;
			return 0;
		}
	}

	/* run queue expects the lock held */
	nfp_ccm_mbox_run_queue_unlock(nn);
	return nfp_ccm_mbox_skb_return(skb);

err_unlock:
	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);
err_free_skb:
	dev_kfree_skb_any(skb);
	return err;
}

int nfp_ccm_mbox_communicate(struct nfp_net *nn, struct sk_buff *skb,
			     enum nfp_ccm_type type,
			     unsigned int reply_size,
			     unsigned int max_reply_size)
{
	return __nfp_ccm_mbox_communicate(nn, skb, type, reply_size,
					  max_reply_size, false);
}

static void nfp_ccm_mbox_post_runq_work(struct work_struct *work)
{
	struct sk_buff *skb;
	struct nfp_net *nn;

	nn = container_of(work, struct nfp_net, mbox_cmsg.runq_work);

	spin_lock_bh(&nn->mbox_cmsg.queue.lock);

	skb = __skb_peek(&nn->mbox_cmsg.queue);
	if (WARN_ON(!skb || !nfp_ccm_mbox_is_posted(skb) ||
		    !nfp_ccm_mbox_should_run(nn, skb))) {
		spin_unlock_bh(&nn->mbox_cmsg.queue.lock);
		return;
	}

	nfp_ccm_mbox_run_queue_unlock(nn);
}

static void nfp_ccm_mbox_post_wait_work(struct work_struct *work)
{
	struct sk_buff *skb;
	struct nfp_net *nn;
	int err;

	nn = container_of(work, struct nfp_net, mbox_cmsg.wait_work);

	skb = skb_peek(&nn->mbox_cmsg.queue);
	if (WARN_ON(!skb || !nfp_ccm_mbox_is_posted(skb)))
		/* Should never happen so it's unclear what to do here.. */
		goto exit_unlock_wake;

	err = nfp_net_mbox_reconfig_wait_posted(nn);
	if (!err)
		nfp_ccm_mbox_copy_out(nn, skb);
	else
		nfp_ccm_mbox_mark_all_err(nn, skb, -EIO);
exit_unlock_wake:
	nn_ctrl_bar_unlock(nn);
	wake_up_all(&nn->mbox_cmsg.wq);
}

int nfp_ccm_mbox_post(struct nfp_net *nn, struct sk_buff *skb,
		      enum nfp_ccm_type type, unsigned int max_reply_size)
{
	int err;

	err = nfp_ccm_mbox_msg_prepare(nn, skb, type, 0, max_reply_size,
				       GFP_ATOMIC);
	if (err)
		goto err_free_skb;

	nfp_ccm_mbox_mark_posted(skb);

	spin_lock_bh(&nn->mbox_cmsg.queue.lock);

	err = nfp_ccm_mbox_msg_enqueue(nn, skb, type, false);
	if (err)
		goto err_unlock;

	if (nfp_ccm_mbox_is_first(nn, skb)) {
		if (nn_ctrl_bar_trylock(nn)) {
			nfp_ccm_mbox_copy_in(nn, skb);
			nfp_net_mbox_reconfig_post(nn,
						   NFP_NET_CFG_MBOX_CMD_TLV_CMSG);
			queue_work(nn->mbox_cmsg.workq,
				   &nn->mbox_cmsg.wait_work);
		} else {
			nfp_ccm_mbox_mark_next_runner(nn);
		}
	}

	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);

	return 0;

err_unlock:
	spin_unlock_bh(&nn->mbox_cmsg.queue.lock);
err_free_skb:
	dev_kfree_skb_any(skb);
	return err;
}

struct sk_buff *
nfp_ccm_mbox_msg_alloc(struct nfp_net *nn, unsigned int req_size,
		       unsigned int reply_size, gfp_t flags)
{
	unsigned int max_size;
	struct sk_buff *skb;

	if (!reply_size)
		max_size = nfp_ccm_mbox_max_msg(nn);
	else
		max_size = max(req_size, reply_size);
	max_size = round_up(max_size, 4);

	skb = alloc_skb(max_size, flags);
	if (!skb)
		return NULL;

	skb_put(skb, req_size);

	return skb;
}

bool nfp_ccm_mbox_fits(struct nfp_net *nn, unsigned int size)
{
	return nfp_ccm_mbox_max_msg(nn) >= size;
}

int nfp_ccm_mbox_init(struct nfp_net *nn)
{
	return 0;
}

void nfp_ccm_mbox_clean(struct nfp_net *nn)
{
	drain_workqueue(nn->mbox_cmsg.workq);
}

int nfp_ccm_mbox_alloc(struct nfp_net *nn)
{
	skb_queue_head_init(&nn->mbox_cmsg.queue);
	init_waitqueue_head(&nn->mbox_cmsg.wq);
	INIT_WORK(&nn->mbox_cmsg.wait_work, nfp_ccm_mbox_post_wait_work);
	INIT_WORK(&nn->mbox_cmsg.runq_work, nfp_ccm_mbox_post_runq_work);

	nn->mbox_cmsg.workq = alloc_workqueue("nfp-ccm-mbox", WQ_UNBOUND, 0);
	if (!nn->mbox_cmsg.workq)
		return -ENOMEM;
	return 0;
}

void nfp_ccm_mbox_free(struct nfp_net *nn)
{
	destroy_workqueue(nn->mbox_cmsg.workq);
	WARN_ON(!skb_queue_empty(&nn->mbox_cmsg.queue));
}
