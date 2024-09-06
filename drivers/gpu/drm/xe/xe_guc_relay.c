// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/delay.h>

#include <drm/drm_managed.h>

#include <kunit/static_stub.h>
#include <kunit/test-bug.h>

#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_relay_actions_abi.h"
#include "abi/guc_relay_communication_abi.h"

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sriov_printk.h"
#include "xe_gt_sriov_pf_service.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_hxg_helpers.h"
#include "xe_guc_relay.h"
#include "xe_guc_relay_types.h"
#include "xe_sriov.h"

/*
 * How long should we wait for the response?
 * XXX this value is subject for the profiling.
 */
#define RELAY_TIMEOUT_MSEC	(2500)

static void relays_worker_fn(struct work_struct *w);

static struct xe_guc *relay_to_guc(struct xe_guc_relay *relay)
{
	return container_of(relay, struct xe_guc, relay);
}

static struct xe_guc_ct *relay_to_ct(struct xe_guc_relay *relay)
{
	return &relay_to_guc(relay)->ct;
}

static struct xe_gt *relay_to_gt(struct xe_guc_relay *relay)
{
	return guc_to_gt(relay_to_guc(relay));
}

static struct xe_device *relay_to_xe(struct xe_guc_relay *relay)
{
	return gt_to_xe(relay_to_gt(relay));
}

#define relay_assert(relay, condition)	xe_gt_assert(relay_to_gt(relay), condition)
#define relay_notice(relay, msg...)	xe_gt_sriov_notice(relay_to_gt(relay), "relay: " msg)
#define relay_debug(relay, msg...)	xe_gt_sriov_dbg_verbose(relay_to_gt(relay), "relay: " msg)

static int relay_get_totalvfs(struct xe_guc_relay *relay)
{
	struct xe_device *xe = relay_to_xe(relay);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);

	KUNIT_STATIC_STUB_REDIRECT(relay_get_totalvfs, relay);
	return IS_SRIOV_VF(xe) ? 0 : pci_sriov_get_totalvfs(pdev);
}

static bool relay_is_ready(struct xe_guc_relay *relay)
{
	return mempool_initialized(&relay->pool);
}

static u32 relay_get_next_rid(struct xe_guc_relay *relay)
{
	u32 rid;

	spin_lock(&relay->lock);
	rid = ++relay->last_rid;
	spin_unlock(&relay->lock);

	return rid;
}

/**
 * struct relay_transaction - internal data used to handle transactions
 *
 * Relation between struct relay_transaction members::
 *
 *                 <-------------------- GUC_CTB_MAX_DWORDS -------------->
 *                                  <-------- GUC_RELAY_MSG_MAX_LEN --->
 *                 <--- offset ---> <--- request_len ------->
 *                +----------------+-------------------------+----------+--+
 *                |                |                         |          |  |
 *                +----------------+-------------------------+----------+--+
 *                ^                ^
 *               /                /
 *    request_buf          request
 *
 *                 <-------------------- GUC_CTB_MAX_DWORDS -------------->
 *                                  <-------- GUC_RELAY_MSG_MAX_LEN --->
 *                 <--- offset ---> <--- response_len --->
 *                +----------------+----------------------+-------------+--+
 *                |                |                      |             |  |
 *                +----------------+----------------------+-------------+--+
 *                ^                ^
 *               /                /
 *   response_buf         response
 */
struct relay_transaction {
	/**
	 * @incoming: indicates whether this transaction represents an incoming
	 *            request from the remote VF/PF or this transaction
	 *            represents outgoing request to the remote VF/PF.
	 */
	bool incoming;

	/**
	 * @remote: PF/VF identifier of the origin (or target) of the relay
	 *          request message.
	 */
	u32 remote;

	/** @rid: identifier of the VF/PF relay message. */
	u32 rid;

	/**
	 * @request: points to the inner VF/PF request message, copied to the
	 *           #response_buf starting at #offset.
	 */
	u32 *request;

	/** @request_len: length of the inner VF/PF request message. */
	u32 request_len;

	/**
	 * @response: points to the placeholder buffer where inner VF/PF
	 *            response will be located, for outgoing transaction
	 *            this could be caller's buffer (if provided) otherwise
	 *            it points to the #response_buf starting at #offset.
	 */
	u32 *response;

	/**
	 * @response_len: length of the inner VF/PF response message (only
	 *                if #status is 0), initially set to the size of the
	 *                placeholder buffer where response message will be
	 *                copied.
	 */
	u32 response_len;

	/**
	 * @offset: offset to the start of the inner VF/PF relay message inside
	 *          buffers; this offset is equal the length of the outer GuC
	 *          relay header message.
	 */
	u32 offset;

	/**
	 * @request_buf: buffer with VF/PF request message including outer
	 *               transport message.
	 */
	u32 request_buf[GUC_CTB_MAX_DWORDS];

	/**
	 * @response_buf: buffer with VF/PF response message including outer
	 *                transport message.
	 */
	u32 response_buf[GUC_CTB_MAX_DWORDS];

	/**
	 * @reply: status of the reply, 0 means that data pointed by the
	 *         #response is valid.
	 */
	int reply;

	/** @done: completion of the outgoing transaction. */
	struct completion done;

	/** @link: transaction list link */
	struct list_head link;
};

static u32 prepare_pf2guc(u32 *msg, u32 target, u32 rid)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		 FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, XE_GUC_ACTION_PF2GUC_RELAY_TO_VF);
	msg[1] = FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, target);
	msg[2] = FIELD_PREP(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, rid);

	return PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN;
}

static u32 prepare_vf2guc(u32 *msg, u32 rid)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		 FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, XE_GUC_ACTION_VF2GUC_RELAY_TO_PF);
	msg[1] = FIELD_PREP(VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID, rid);

	return VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN;
}

static struct relay_transaction *
__relay_get_transaction(struct xe_guc_relay *relay, bool incoming, u32 remote, u32 rid,
			const u32 *action, u32 action_len, u32 *resp, u32 resp_size)
{
	struct relay_transaction *txn;

	relay_assert(relay, action_len >= GUC_RELAY_MSG_MIN_LEN);
	relay_assert(relay, action_len <= GUC_RELAY_MSG_MAX_LEN);
	relay_assert(relay, !(!!resp ^ !!resp_size));
	relay_assert(relay, resp_size <= GUC_RELAY_MSG_MAX_LEN);
	relay_assert(relay, resp_size == 0 || resp_size >= GUC_RELAY_MSG_MIN_LEN);

	if (unlikely(!relay_is_ready(relay)))
		return ERR_PTR(-ENODEV);

	/*
	 * For incoming requests we can't use GFP_KERNEL as those are delivered
	 * with CTB lock held which is marked as used in the reclaim path.
	 * Btw, that's one of the reason why we use mempool here!
	 */
	txn = mempool_alloc(&relay->pool, incoming ? GFP_ATOMIC : GFP_KERNEL);
	if (!txn)
		return ERR_PTR(-ENOMEM);

	txn->incoming = incoming;
	txn->remote = remote;
	txn->rid = rid;
	txn->offset = remote ?
		prepare_pf2guc(incoming ? txn->response_buf : txn->request_buf, remote, rid) :
		prepare_vf2guc(incoming ? txn->response_buf : txn->request_buf, rid);

	relay_assert(relay, txn->offset);
	relay_assert(relay, txn->offset + GUC_RELAY_MSG_MAX_LEN <= ARRAY_SIZE(txn->request_buf));
	relay_assert(relay, txn->offset + GUC_RELAY_MSG_MAX_LEN <= ARRAY_SIZE(txn->response_buf));

	txn->request = txn->request_buf + txn->offset;
	memcpy(&txn->request_buf[txn->offset], action, sizeof(u32) * action_len);
	txn->request_len = action_len;

	txn->response = resp ?: txn->response_buf + txn->offset;
	txn->response_len = resp_size ?: GUC_RELAY_MSG_MAX_LEN;
	txn->reply = -ENOMSG;
	INIT_LIST_HEAD(&txn->link);
	init_completion(&txn->done);

	return txn;
}

static struct relay_transaction *
relay_new_transaction(struct xe_guc_relay *relay, u32 target, const u32 *action, u32 len,
		      u32 *resp, u32 resp_size)
{
	u32 rid = relay_get_next_rid(relay);

	return __relay_get_transaction(relay, false, target, rid, action, len, resp, resp_size);
}

static struct relay_transaction *
relay_new_incoming_transaction(struct xe_guc_relay *relay, u32 origin, u32 rid,
			       const u32 *action, u32 len)
{
	return __relay_get_transaction(relay, true, origin, rid, action, len, NULL, 0);
}

static void relay_release_transaction(struct xe_guc_relay *relay, struct relay_transaction *txn)
{
	relay_assert(relay, list_empty(&txn->link));

	txn->offset = 0;
	txn->response = NULL;
	txn->reply = -ESTALE;
	mempool_free(txn, &relay->pool);
}

static int relay_send_transaction(struct xe_guc_relay *relay, struct relay_transaction *txn)
{
	u32 len = txn->incoming ? txn->response_len : txn->request_len;
	u32 *buf = txn->incoming ? txn->response_buf : txn->request_buf;
	u32 *msg = buf + txn->offset;
	int ret;

	relay_assert(relay, txn->offset);
	relay_assert(relay, txn->offset + len <= GUC_CTB_MAX_DWORDS);
	relay_assert(relay, len >= GUC_RELAY_MSG_MIN_LEN);
	relay_assert(relay, len <= GUC_RELAY_MSG_MAX_LEN);

	relay_debug(relay, "sending %s.%u to %u = %*ph\n",
		    guc_hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
		    txn->rid, txn->remote, (int)sizeof(u32) * len, msg);

	ret = xe_guc_ct_send_block(relay_to_ct(relay), buf, len + txn->offset);

	if (unlikely(ret > 0)) {
		relay_notice(relay, "Unexpected data=%d from GuC, wrong ABI?\n", ret);
		ret = -EPROTO;
	}
	if (unlikely(ret < 0)) {
		relay_notice(relay, "Failed to send %s.%x to GuC (%pe) %*ph ...\n",
			     guc_hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, buf[0])),
			     FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, buf[0]),
			     ERR_PTR(ret), (int)sizeof(u32) * txn->offset, buf);
		relay_notice(relay, "Failed to send %s.%u to %u (%pe) %*ph\n",
			     guc_hxg_type_to_string(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])),
			     txn->rid, txn->remote, ERR_PTR(ret), (int)sizeof(u32) * len, msg);
	}

	return ret;
}

static void __fini_relay(struct drm_device *drm, void *arg)
{
	struct xe_guc_relay *relay = arg;

	mempool_exit(&relay->pool);
}

/**
 * xe_guc_relay_init - Initialize a &xe_guc_relay
 * @relay: the &xe_guc_relay to initialize
 *
 * Initialize remaining members of &xe_guc_relay that may depend
 * on the SR-IOV mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_relay_init(struct xe_guc_relay *relay)
{
	const int XE_RELAY_MEMPOOL_MIN_NUM = 1;
	struct xe_device *xe = relay_to_xe(relay);
	int err;

	relay_assert(relay, !relay_is_ready(relay));

	if (!IS_SRIOV(xe))
		return 0;

	spin_lock_init(&relay->lock);
	INIT_WORK(&relay->worker, relays_worker_fn);
	INIT_LIST_HEAD(&relay->pending_relays);
	INIT_LIST_HEAD(&relay->incoming_actions);

	err = mempool_init_kmalloc_pool(&relay->pool, XE_RELAY_MEMPOOL_MIN_NUM +
					relay_get_totalvfs(relay),
					sizeof(struct relay_transaction));
	if (err)
		return err;

	relay_debug(relay, "using mempool with %d elements\n", relay->pool.min_nr);

	return drmm_add_action_or_reset(&xe->drm, __fini_relay, relay);
}

static u32 to_relay_error(int err)
{
	/* XXX: assume that relay errors match errno codes */
	return err < 0 ? -err : GUC_RELAY_ERROR_UNDISCLOSED;
}

static int from_relay_error(u32 error)
{
	/* XXX: assume that relay errors match errno codes */
	return error ? -error : -ENODATA;
}

static u32 sanitize_relay_error(u32 error)
{
	/* XXX TBD if generic error codes will be allowed */
	if (!IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		error = GUC_RELAY_ERROR_UNDISCLOSED;
	return error;
}

static u32 sanitize_relay_error_hint(u32 hint)
{
	/* XXX TBD if generic error codes will be allowed */
	if (!IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		hint = 0;
	return hint;
}

static u32 prepare_error_reply(u32 *msg, u32 error, u32 hint)
{
	msg[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		 FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_FAILURE) |
		 FIELD_PREP(GUC_HXG_FAILURE_MSG_0_HINT, hint) |
		 FIELD_PREP(GUC_HXG_FAILURE_MSG_0_ERROR, error);

	XE_WARN_ON(!FIELD_FIT(GUC_HXG_FAILURE_MSG_0_ERROR, error));
	XE_WARN_ON(!FIELD_FIT(GUC_HXG_FAILURE_MSG_0_HINT, hint));

	return GUC_HXG_FAILURE_MSG_LEN;
}

static void relay_testonly_nop(struct xe_guc_relay *relay)
{
	KUNIT_STATIC_STUB_REDIRECT(relay_testonly_nop, relay);
}

static int relay_send_message_and_wait(struct xe_guc_relay *relay,
				       struct relay_transaction *txn,
				       u32 *buf, u32 buf_size)
{
	unsigned long timeout = msecs_to_jiffies(RELAY_TIMEOUT_MSEC);
	u32 *msg = &txn->request_buf[txn->offset];
	u32 len = txn->request_len;
	u32 type, action, data0;
	int ret;
	long n;

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);
	action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);
	data0 = FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]);

	relay_debug(relay, "%s.%u to %u action %#x:%u\n",
		    guc_hxg_type_to_string(type),
		    txn->rid, txn->remote, action, data0);

	/* list ordering does not need to match RID ordering */
	spin_lock(&relay->lock);
	list_add_tail(&txn->link, &relay->pending_relays);
	spin_unlock(&relay->lock);

resend:
	ret = relay_send_transaction(relay, txn);
	if (unlikely(ret < 0))
		goto unlink;

wait:
	n = wait_for_completion_timeout(&txn->done, timeout);
	if (unlikely(n == 0 && txn->reply)) {
		ret = -ETIME;
		goto unlink;
	}

	relay_debug(relay, "%u.%u reply %d after %u msec\n",
		    txn->remote, txn->rid, txn->reply, jiffies_to_msecs(timeout - n));
	if (unlikely(txn->reply)) {
		reinit_completion(&txn->done);
		if (txn->reply == -EAGAIN)
			goto resend;
		if (txn->reply == -EBUSY) {
			relay_testonly_nop(relay);
			goto wait;
		}
		if (txn->reply > 0)
			ret = from_relay_error(txn->reply);
		else
			ret = txn->reply;
		goto unlink;
	}

	relay_debug(relay, "%u.%u response %*ph\n", txn->remote, txn->rid,
		    (int)sizeof(u32) * txn->response_len, txn->response);
	relay_assert(relay, txn->response_len >= GUC_RELAY_MSG_MIN_LEN);
	ret = txn->response_len;

unlink:
	spin_lock(&relay->lock);
	list_del_init(&txn->link);
	spin_unlock(&relay->lock);

	if (unlikely(ret < 0)) {
		relay_notice(relay, "Unsuccessful %s.%u %#x:%u to %u (%pe) %*ph\n",
			     guc_hxg_type_to_string(type), txn->rid,
			     action, data0, txn->remote, ERR_PTR(ret),
			     (int)sizeof(u32) * len, msg);
	}

	return ret;
}

static int relay_send_to(struct xe_guc_relay *relay, u32 target,
			 const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	struct relay_transaction *txn;
	int ret;

	relay_assert(relay, len >= GUC_RELAY_MSG_MIN_LEN);
	relay_assert(relay, len <= GUC_RELAY_MSG_MAX_LEN);
	relay_assert(relay, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) == GUC_HXG_ORIGIN_HOST);
	relay_assert(relay, guc_hxg_type_is_action(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])));

	if (unlikely(!relay_is_ready(relay)))
		return -ENODEV;

	txn = relay_new_transaction(relay, target, msg, len, buf, buf_size);
	if (IS_ERR(txn))
		return PTR_ERR(txn);

	switch (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0])) {
	case GUC_HXG_TYPE_REQUEST:
		ret = relay_send_message_and_wait(relay, txn, buf, buf_size);
		break;
	case GUC_HXG_TYPE_FAST_REQUEST:
		relay_assert(relay, !GUC_HXG_TYPE_FAST_REQUEST);
		fallthrough;
	case GUC_HXG_TYPE_EVENT:
		ret = relay_send_transaction(relay, txn);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	relay_release_transaction(relay, txn);
	return ret;
}

#ifdef CONFIG_PCI_IOV
/**
 * xe_guc_relay_send_to_vf - Send a message to the VF.
 * @relay: the &xe_guc_relay which will send the message
 * @target: target VF number
 * @msg: request message to be sent
 * @len: length of the request message (in dwords, can't be 0)
 * @buf: placeholder for the response message
 * @buf_size: size of the response message placeholder (in dwords)
 *
 * This function can only be used by the driver running in the SR-IOV PF mode.
 *
 * Return: Non-negative response length (in dwords) or
 *         a negative error code on failure.
 */
int xe_guc_relay_send_to_vf(struct xe_guc_relay *relay, u32 target,
			    const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	relay_assert(relay, IS_SRIOV_PF(relay_to_xe(relay)));

	return relay_send_to(relay, target, msg, len, buf, buf_size);
}
#endif

/**
 * xe_guc_relay_send_to_pf - Send a message to the PF.
 * @relay: the &xe_guc_relay which will send the message
 * @msg: request message to be sent
 * @len: length of the message (in dwords, can't be 0)
 * @buf: placeholder for the response message
 * @buf_size: size of the response message placeholder (in dwords)
 *
 * This function can only be used by driver running in SR-IOV VF mode.
 *
 * Return: Non-negative response length (in dwords) or
 *         a negative error code on failure.
 */
int xe_guc_relay_send_to_pf(struct xe_guc_relay *relay,
			    const u32 *msg, u32 len, u32 *buf, u32 buf_size)
{
	relay_assert(relay, IS_SRIOV_VF(relay_to_xe(relay)));

	return relay_send_to(relay, PFID, msg, len, buf, buf_size);
}

static int relay_handle_reply(struct xe_guc_relay *relay, u32 origin,
			      u32 rid, int reply, const u32 *msg, u32 len)
{
	struct relay_transaction *pending;
	int err = -ESRCH;

	spin_lock(&relay->lock);
	list_for_each_entry(pending, &relay->pending_relays, link) {
		if (pending->remote != origin || pending->rid != rid) {
			relay_debug(relay, "%u.%u still awaits response\n",
				    pending->remote, pending->rid);
			continue;
		}
		err = 0; /* found! */
		if (reply == 0) {
			if (len > pending->response_len) {
				reply = -ENOBUFS;
				err = -ENOBUFS;
			} else {
				memcpy(pending->response, msg, 4 * len);
				pending->response_len = len;
			}
		}
		pending->reply = reply;
		complete_all(&pending->done);
		break;
	}
	spin_unlock(&relay->lock);

	return err;
}

static int relay_handle_failure(struct xe_guc_relay *relay, u32 origin,
				u32 rid, const u32 *msg, u32 len)
{
	int error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, msg[0]);
	u32 hint __maybe_unused = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, msg[0]);

	relay_assert(relay, len);
	relay_debug(relay, "%u.%u error %#x (%pe) hint %u debug %*ph\n",
		    origin, rid, error, ERR_PTR(-error), hint, 4 * (len - 1), msg + 1);

	return relay_handle_reply(relay, origin, rid, error ?: -EREMOTEIO, NULL, 0);
}

static int relay_testloop_action_handler(struct xe_guc_relay *relay, u32 origin,
					 const u32 *msg, u32 len, u32 *response, u32 size)
{
	static ktime_t last_reply = 0;
	u32 type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);
	u32 action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);
	u32 opcode = FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]);
	ktime_t now = ktime_get();
	bool busy;
	int ret;

	relay_assert(relay, guc_hxg_type_is_action(type));
	relay_assert(relay, action == GUC_RELAY_ACTION_VFXPF_TESTLOOP);

	if (!IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV))
		return -ECONNREFUSED;

	if (!last_reply)
		last_reply = now;
	busy = ktime_before(now, ktime_add_ms(last_reply, 2 * RELAY_TIMEOUT_MSEC));
	if (!busy)
		last_reply = now;

	switch (opcode) {
	case VFXPF_TESTLOOP_OPCODE_NOP:
		if (type == GUC_HXG_TYPE_EVENT)
			return 0;
		return guc_hxg_msg_encode_success(response, 0);
	case VFXPF_TESTLOOP_OPCODE_BUSY:
		if (type == GUC_HXG_TYPE_EVENT)
			return -EPROTO;
		msleep(RELAY_TIMEOUT_MSEC / 8);
		if (busy)
			return -EINPROGRESS;
		return guc_hxg_msg_encode_success(response, 0);
	case VFXPF_TESTLOOP_OPCODE_RETRY:
		if (type == GUC_HXG_TYPE_EVENT)
			return -EPROTO;
		msleep(RELAY_TIMEOUT_MSEC / 8);
		if (busy)
			return guc_hxg_msg_encode_retry(response, 0);
		return guc_hxg_msg_encode_success(response, 0);
	case VFXPF_TESTLOOP_OPCODE_ECHO:
		if (type == GUC_HXG_TYPE_EVENT)
			return -EPROTO;
		if (size < len)
			return -ENOBUFS;
		ret = guc_hxg_msg_encode_success(response, len);
		memcpy(response + ret, msg + ret, (len - ret) * sizeof(u32));
		return len;
	case VFXPF_TESTLOOP_OPCODE_FAIL:
		return -EHWPOISON;
	default:
		break;
	}

	relay_notice(relay, "Unexpected action %#x opcode %#x\n", action, opcode);
	return -EBADRQC;
}

static int relay_action_handler(struct xe_guc_relay *relay, u32 origin,
				const u32 *msg, u32 len, u32 *response, u32 size)
{
	struct xe_gt *gt = relay_to_gt(relay);
	u32 type;
	int ret;

	relay_assert(relay, len >= GUC_HXG_MSG_MIN_LEN);

	if (FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]) == GUC_RELAY_ACTION_VFXPF_TESTLOOP)
		return relay_testloop_action_handler(relay, origin, msg, len, response, size);

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);

	if (IS_SRIOV_PF(relay_to_xe(relay)))
		ret = xe_gt_sriov_pf_service_process_request(gt, origin, msg, len, response, size);
	else
		ret = -EOPNOTSUPP;

	if (type == GUC_HXG_TYPE_EVENT)
		relay_assert(relay, ret <= 0);

	return ret;
}

static struct relay_transaction *relay_dequeue_transaction(struct xe_guc_relay *relay)
{
	struct relay_transaction *txn;

	spin_lock(&relay->lock);
	txn = list_first_entry_or_null(&relay->incoming_actions, struct relay_transaction, link);
	if (txn)
		list_del_init(&txn->link);
	spin_unlock(&relay->lock);

	return txn;
}

static void relay_process_incoming_action(struct xe_guc_relay *relay)
{
	struct relay_transaction *txn;
	bool again = false;
	u32 type;
	int ret;

	txn = relay_dequeue_transaction(relay);
	if (!txn)
		return;

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, txn->request_buf[txn->offset]);

	ret = relay_action_handler(relay, txn->remote,
				   txn->request_buf + txn->offset, txn->request_len,
				   txn->response_buf + txn->offset,
				   ARRAY_SIZE(txn->response_buf) - txn->offset);

	if (ret == -EINPROGRESS) {
		again = true;
		ret = guc_hxg_msg_encode_busy(txn->response_buf + txn->offset, 0);
	}

	if (ret > 0) {
		txn->response_len = ret;
		ret = relay_send_transaction(relay, txn);
	}

	if (ret < 0) {
		u32 error = to_relay_error(ret);

		relay_notice(relay, "Failed to handle %s.%u from %u (%pe) %*ph\n",
			     guc_hxg_type_to_string(type), txn->rid, txn->remote,
			     ERR_PTR(ret), 4 * txn->request_len, txn->request_buf + txn->offset);

		txn->response_len = prepare_error_reply(txn->response_buf + txn->offset,
							txn->remote ?
							sanitize_relay_error(error) : error,
							txn->remote ?
							sanitize_relay_error_hint(-ret) : -ret);
		ret = relay_send_transaction(relay, txn);
		again = false;
	}

	if (again) {
		spin_lock(&relay->lock);
		list_add(&txn->link, &relay->incoming_actions);
		spin_unlock(&relay->lock);
		return;
	}

	if (unlikely(ret < 0))
		relay_notice(relay, "Failed to process action.%u (%pe) %*ph\n",
			     txn->rid, ERR_PTR(ret), 4 * txn->request_len,
			     txn->request_buf + txn->offset);

	relay_release_transaction(relay, txn);
}

static bool relay_needs_worker(struct xe_guc_relay *relay)
{
	bool is_empty;

	spin_lock(&relay->lock);
	is_empty = list_empty(&relay->incoming_actions);
	spin_unlock(&relay->lock);

	return !is_empty;

}

static void relay_kick_worker(struct xe_guc_relay *relay)
{
	KUNIT_STATIC_STUB_REDIRECT(relay_kick_worker, relay);
	queue_work(relay_to_xe(relay)->sriov.wq, &relay->worker);
}

static void relays_worker_fn(struct work_struct *w)
{
	struct xe_guc_relay *relay = container_of(w, struct xe_guc_relay, worker);

	relay_process_incoming_action(relay);

	if (relay_needs_worker(relay))
		relay_kick_worker(relay);
}

static int relay_queue_action_msg(struct xe_guc_relay *relay, u32 origin, u32 rid,
				  const u32 *msg, u32 len)
{
	struct relay_transaction *txn;

	txn = relay_new_incoming_transaction(relay, origin, rid, msg, len);
	if (IS_ERR(txn))
		return PTR_ERR(txn);

	spin_lock(&relay->lock);
	list_add_tail(&txn->link, &relay->incoming_actions);
	spin_unlock(&relay->lock);

	relay_kick_worker(relay);
	return 0;
}

static int relay_process_msg(struct xe_guc_relay *relay, u32 origin, u32 rid,
			     const u32 *msg, u32 len)
{
	u32 type;
	int err;

	if (unlikely(len < GUC_HXG_MSG_MIN_LEN))
		return -EPROTO;

	if (FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_HOST)
		return -EPROTO;

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]);
	relay_debug(relay, "received %s.%u from %u = %*ph\n",
		    guc_hxg_type_to_string(type), rid, origin, 4 * len, msg);

	switch (type) {
	case GUC_HXG_TYPE_REQUEST:
	case GUC_HXG_TYPE_FAST_REQUEST:
	case GUC_HXG_TYPE_EVENT:
		err = relay_queue_action_msg(relay, origin, rid, msg, len);
		break;
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
		err = relay_handle_reply(relay, origin, rid, 0, msg, len);
		break;
	case GUC_HXG_TYPE_NO_RESPONSE_BUSY:
		err = relay_handle_reply(relay, origin, rid, -EBUSY, NULL, 0);
		break;
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
		err = relay_handle_reply(relay, origin, rid, -EAGAIN, NULL, 0);
		break;
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
		err = relay_handle_failure(relay, origin, rid, msg, len);
		break;
	default:
		err = -EBADRQC;
	}

	if (unlikely(err))
		relay_notice(relay, "Failed to process %s.%u from %u (%pe) %*ph\n",
			     guc_hxg_type_to_string(type), rid, origin,
			     ERR_PTR(err), 4 * len, msg);

	return err;
}

/**
 * xe_guc_relay_process_guc2vf - Handle relay notification message from the GuC.
 * @relay: the &xe_guc_relay which will handle the message
 * @msg: message to be handled
 * @len: length of the message (in dwords)
 *
 * This function will handle relay messages received from the GuC.
 *
 * This function is can only be used if driver is running in SR-IOV mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_relay_process_guc2vf(struct xe_guc_relay *relay, const u32 *msg, u32 len)
{
	u32 rid;

	relay_assert(relay, len >= GUC_HXG_MSG_MIN_LEN);
	relay_assert(relay, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) == GUC_HXG_ORIGIN_GUC);
	relay_assert(relay, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_EVENT);
	relay_assert(relay, FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) ==
		     XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF);

	if (unlikely(!IS_SRIOV_VF(relay_to_xe(relay)) && !kunit_get_current_test()))
		return -EPERM;

	if (unlikely(!relay_is_ready(relay)))
		return -ENODEV;

	if (unlikely(len < GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN))
		return -EPROTO;

	if (unlikely(len > GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN))
		return -EMSGSIZE;

	if (unlikely(FIELD_GET(GUC_HXG_EVENT_MSG_0_DATA0, msg[0])))
		return -EPFNOSUPPORT;

	rid = FIELD_GET(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, msg[1]);

	return relay_process_msg(relay, PFID, rid,
				 msg + GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN,
				 len - GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN);
}

#ifdef CONFIG_PCI_IOV
/**
 * xe_guc_relay_process_guc2pf - Handle relay notification message from the GuC.
 * @relay: the &xe_guc_relay which will handle the message
 * @msg: message to be handled
 * @len: length of the message (in dwords)
 *
 * This function will handle relay messages received from the GuC.
 *
 * This function can only be used if driver is running in SR-IOV PF mode.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_relay_process_guc2pf(struct xe_guc_relay *relay, const u32 *msg, u32 len)
{
	u32 origin, rid;
	int err;

	relay_assert(relay, len >= GUC_HXG_EVENT_MSG_MIN_LEN);
	relay_assert(relay, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) == GUC_HXG_ORIGIN_GUC);
	relay_assert(relay, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_EVENT);
	relay_assert(relay, FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) ==
		     XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF);

	if (unlikely(!IS_SRIOV_PF(relay_to_xe(relay)) && !kunit_get_current_test()))
		return -EPERM;

	if (unlikely(!relay_is_ready(relay)))
		return -ENODEV;

	if (unlikely(len < GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN))
		return -EPROTO;

	if (unlikely(len > GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN))
		return -EMSGSIZE;

	if (unlikely(FIELD_GET(GUC_HXG_EVENT_MSG_0_DATA0, msg[0])))
		return -EPFNOSUPPORT;

	origin = FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, msg[1]);
	rid = FIELD_GET(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, msg[2]);

	if (unlikely(origin > relay_get_totalvfs(relay)))
		return -ENOENT;

	err = relay_process_msg(relay, origin, rid,
				msg + GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN,
				len - GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN);

	return err;
}
#endif

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_guc_relay_test.c"
#endif
