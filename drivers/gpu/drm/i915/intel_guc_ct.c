/*
 * Copyright © 2016-2017 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "i915_drv.h"
#include "intel_guc_ct.h"

#ifdef CONFIG_DRM_I915_DEBUG_GUC
#define CT_DEBUG_DRIVER(...)	DRM_DEBUG_DRIVER(__VA_ARGS__)
#else
#define CT_DEBUG_DRIVER(...)	do { } while (0)
#endif

struct ct_request {
	struct list_head link;
	u32 fence;
	u32 status;
	u32 response_len;
	u32 *response_buf;
};

struct ct_incoming_request {
	struct list_head link;
	u32 msg[];
};

enum { CTB_SEND = 0, CTB_RECV = 1 };

enum { CTB_OWNER_HOST = 0 };

static void ct_incoming_request_worker_func(struct work_struct *w);

/**
 * intel_guc_ct_init_early - Initialize CT state without requiring device access
 * @ct: pointer to CT struct
 */
void intel_guc_ct_init_early(struct intel_guc_ct *ct)
{
	/* we're using static channel owners */
	ct->host_channel.owner = CTB_OWNER_HOST;

	spin_lock_init(&ct->lock);
	INIT_LIST_HEAD(&ct->pending_requests);
	INIT_LIST_HEAD(&ct->incoming_requests);
	INIT_WORK(&ct->worker, ct_incoming_request_worker_func);
}

static inline struct intel_guc *ct_to_guc(struct intel_guc_ct *ct)
{
	return container_of(ct, struct intel_guc, ct);
}

static inline const char *guc_ct_buffer_type_to_str(u32 type)
{
	switch (type) {
	case INTEL_GUC_CT_BUFFER_TYPE_SEND:
		return "SEND";
	case INTEL_GUC_CT_BUFFER_TYPE_RECV:
		return "RECV";
	default:
		return "<invalid>";
	}
}

static void guc_ct_buffer_desc_init(struct guc_ct_buffer_desc *desc,
				    u32 cmds_addr, u32 size, u32 owner)
{
	CT_DEBUG_DRIVER("CT: desc %p init addr=%#x size=%u owner=%u\n",
			desc, cmds_addr, size, owner);
	memset(desc, 0, sizeof(*desc));
	desc->addr = cmds_addr;
	desc->size = size;
	desc->owner = owner;
}

static void guc_ct_buffer_desc_reset(struct guc_ct_buffer_desc *desc)
{
	CT_DEBUG_DRIVER("CT: desc %p reset head=%u tail=%u\n",
			desc, desc->head, desc->tail);
	desc->head = 0;
	desc->tail = 0;
	desc->is_in_error = 0;
}

static int guc_action_register_ct_buffer(struct intel_guc *guc,
					 u32 desc_addr,
					 u32 type)
{
	u32 action[] = {
		INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER,
		desc_addr,
		sizeof(struct guc_ct_buffer_desc),
		type
	};
	int err;

	/* Can't use generic send(), CT registration must go over MMIO */
	err = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
	if (err)
		DRM_ERROR("CT: register %s buffer failed; err=%d\n",
			  guc_ct_buffer_type_to_str(type), err);
	return err;
}

static int guc_action_deregister_ct_buffer(struct intel_guc *guc,
					   u32 owner,
					   u32 type)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER,
		owner,
		type
	};
	int err;

	/* Can't use generic send(), CT deregistration must go over MMIO */
	err = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
	if (err)
		DRM_ERROR("CT: deregister %s buffer failed; owner=%d err=%d\n",
			  guc_ct_buffer_type_to_str(type), owner, err);
	return err;
}

static bool ctch_is_open(struct intel_guc_ct_channel *ctch)
{
	return ctch->vma != NULL;
}

static int ctch_init(struct intel_guc *guc,
		     struct intel_guc_ct_channel *ctch)
{
	struct i915_vma *vma;
	void *blob;
	int err;
	int i;

	GEM_BUG_ON(ctch->vma);

	/* We allocate 1 page to hold both descriptors and both buffers.
	 *       ___________.....................
	 *      |desc (SEND)|                   :
	 *      |___________|                   PAGE/4
	 *      :___________....................:
	 *      |desc (RECV)|                   :
	 *      |___________|                   PAGE/4
	 *      :_______________________________:
	 *      |cmds (SEND)                    |
	 *      |                               PAGE/4
	 *      |_______________________________|
	 *      |cmds (RECV)                    |
	 *      |                               PAGE/4
	 *      |_______________________________|
	 *
	 * Each message can use a maximum of 32 dwords and we don't expect to
	 * have more than 1 in flight at any time, so we have enough space.
	 * Some logic further ahead will rely on the fact that there is only 1
	 * page and that it is always mapped, so if the size is changed the
	 * other code will need updating as well.
	 */

	/* allocate vma */
	vma = intel_guc_allocate_vma(guc, PAGE_SIZE);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_out;
	}
	ctch->vma = vma;

	/* map first page */
	blob = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(blob)) {
		err = PTR_ERR(blob);
		goto err_vma;
	}
	CT_DEBUG_DRIVER("CT: vma base=%#x\n",
			intel_guc_ggtt_offset(guc, ctch->vma));

	/* store pointers to desc and cmds */
	for (i = 0; i < ARRAY_SIZE(ctch->ctbs); i++) {
		GEM_BUG_ON((i != CTB_SEND) && (i != CTB_RECV));
		ctch->ctbs[i].desc = blob + PAGE_SIZE/4 * i;
		ctch->ctbs[i].cmds = blob + PAGE_SIZE/4 * i + PAGE_SIZE/2;
	}

	return 0;

err_vma:
	i915_vma_unpin_and_release(&ctch->vma);
err_out:
	CT_DEBUG_DRIVER("CT: channel %d initialization failed; err=%d\n",
			ctch->owner, err);
	return err;
}

static void ctch_fini(struct intel_guc *guc,
		      struct intel_guc_ct_channel *ctch)
{
	GEM_BUG_ON(!ctch->vma);

	i915_gem_object_unpin_map(ctch->vma->obj);
	i915_vma_unpin_and_release(&ctch->vma);
}

static int ctch_open(struct intel_guc *guc,
		     struct intel_guc_ct_channel *ctch)
{
	u32 base;
	int err;
	int i;

	CT_DEBUG_DRIVER("CT: channel %d reopen=%s\n",
			ctch->owner, yesno(ctch_is_open(ctch)));

	if (!ctch->vma) {
		err = ctch_init(guc, ctch);
		if (unlikely(err))
			goto err_out;
		GEM_BUG_ON(!ctch->vma);
	}

	/* vma should be already allocated and map'ed */
	base = intel_guc_ggtt_offset(guc, ctch->vma);

	/* (re)initialize descriptors
	 * cmds buffers are in the second half of the blob page
	 */
	for (i = 0; i < ARRAY_SIZE(ctch->ctbs); i++) {
		GEM_BUG_ON((i != CTB_SEND) && (i != CTB_RECV));
		guc_ct_buffer_desc_init(ctch->ctbs[i].desc,
					base + PAGE_SIZE/4 * i + PAGE_SIZE/2,
					PAGE_SIZE/4,
					ctch->owner);
	}

	/* register buffers, starting wirh RECV buffer
	 * descriptors are in first half of the blob
	 */
	err = guc_action_register_ct_buffer(guc,
					    base + PAGE_SIZE/4 * CTB_RECV,
					    INTEL_GUC_CT_BUFFER_TYPE_RECV);
	if (unlikely(err))
		goto err_fini;

	err = guc_action_register_ct_buffer(guc,
					    base + PAGE_SIZE/4 * CTB_SEND,
					    INTEL_GUC_CT_BUFFER_TYPE_SEND);
	if (unlikely(err))
		goto err_deregister;

	return 0;

err_deregister:
	guc_action_deregister_ct_buffer(guc,
					ctch->owner,
					INTEL_GUC_CT_BUFFER_TYPE_RECV);
err_fini:
	ctch_fini(guc, ctch);
err_out:
	DRM_ERROR("CT: can't open channel %d; err=%d\n", ctch->owner, err);
	return err;
}

static void ctch_close(struct intel_guc *guc,
		       struct intel_guc_ct_channel *ctch)
{
	GEM_BUG_ON(!ctch_is_open(ctch));

	guc_action_deregister_ct_buffer(guc,
					ctch->owner,
					INTEL_GUC_CT_BUFFER_TYPE_SEND);
	guc_action_deregister_ct_buffer(guc,
					ctch->owner,
					INTEL_GUC_CT_BUFFER_TYPE_RECV);
	ctch_fini(guc, ctch);
}

static u32 ctch_get_next_fence(struct intel_guc_ct_channel *ctch)
{
	/* For now it's trivial */
	return ++ctch->next_fence;
}

/**
 * DOC: CTB Host to GuC request
 *
 * Format of the CTB Host to GuC request message is as follows::
 *
 *      +------------+---------+---------+---------+---------+
 *      |   msg[0]   |   [1]   |   [2]   |   ...   |  [n-1]  |
 *      +------------+---------+---------+---------+---------+
 *      |   MESSAGE  |       MESSAGE PAYLOAD                 |
 *      +   HEADER   +---------+---------+---------+---------+
 *      |            |    0    |    1    |   ...   |    n    |
 *      +============+=========+=========+=========+=========+
 *      |  len >= 1  |  FENCE  |     request specific data   |
 *      +------+-----+---------+---------+---------+---------+
 *
 *                   ^-----------------len-------------------^
 */

static int ctb_write(struct intel_guc_ct_buffer *ctb,
		     const u32 *action,
		     u32 len /* in dwords */,
		     u32 fence,
		     bool want_response)
{
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head = desc->head / 4;	/* in dwords */
	u32 tail = desc->tail / 4;	/* in dwords */
	u32 size = desc->size / 4;	/* in dwords */
	u32 used;			/* in dwords */
	u32 header;
	u32 *cmds = ctb->cmds;
	unsigned int i;

	GEM_BUG_ON(desc->size % 4);
	GEM_BUG_ON(desc->head % 4);
	GEM_BUG_ON(desc->tail % 4);
	GEM_BUG_ON(tail >= size);

	/*
	 * tail == head condition indicates empty. GuC FW does not support
	 * using up the entire buffer to get tail == head meaning full.
	 */
	if (tail < head)
		used = (size - head) + tail;
	else
		used = tail - head;

	/* make sure there is a space including extra dw for the fence */
	if (unlikely(used + len + 1 >= size))
		return -ENOSPC;

	/*
	 * Write the message. The format is the following:
	 * DW0: header (including action code)
	 * DW1: fence
	 * DW2+: action data
	 */
	header = (len << GUC_CT_MSG_LEN_SHIFT) |
		 (GUC_CT_MSG_WRITE_FENCE_TO_DESC) |
		 (want_response ? GUC_CT_MSG_SEND_STATUS : 0) |
		 (action[0] << GUC_CT_MSG_ACTION_SHIFT);

	CT_DEBUG_DRIVER("CT: writing %*phn %*phn %*phn\n",
			4, &header, 4, &fence,
			4 * (len - 1), &action[1]);

	cmds[tail] = header;
	tail = (tail + 1) % size;

	cmds[tail] = fence;
	tail = (tail + 1) % size;

	for (i = 1; i < len; i++) {
		cmds[tail] = action[i];
		tail = (tail + 1) % size;
	}

	/* now update desc tail (back in bytes) */
	desc->tail = tail * 4;
	GEM_BUG_ON(desc->tail > desc->size);

	return 0;
}

/**
 * wait_for_ctb_desc_update - Wait for the CT buffer descriptor update.
 * @desc:	buffer descriptor
 * @fence:	response fence
 * @status:	placeholder for status
 *
 * Guc will update CT buffer descriptor with new fence and status
 * after processing the command identified by the fence. Wait for
 * specified fence and then read from the descriptor status of the
 * command.
 *
 * Return:
 * *	0 response received (status is valid)
 * *	-ETIMEDOUT no response within hardcoded timeout
 * *	-EPROTO no response, CT buffer is in error
 */
static int wait_for_ctb_desc_update(struct guc_ct_buffer_desc *desc,
				    u32 fence,
				    u32 *status)
{
	int err;

	/*
	 * Fast commands should complete in less than 10us, so sample quickly
	 * up to that length of time, then switch to a slower sleep-wait loop.
	 * No GuC command should ever take longer than 10ms.
	 */
#define done (READ_ONCE(desc->fence) == fence)
	err = wait_for_us(done, 10);
	if (err)
		err = wait_for(done, 10);
#undef done

	if (unlikely(err)) {
		DRM_ERROR("CT: fence %u failed; reported fence=%u\n",
			  fence, desc->fence);

		if (WARN_ON(desc->is_in_error)) {
			/* Something went wrong with the messaging, try to reset
			 * the buffer and hope for the best
			 */
			guc_ct_buffer_desc_reset(desc);
			err = -EPROTO;
		}
	}

	*status = desc->status;
	return err;
}

/**
 * wait_for_ct_request_update - Wait for CT request state update.
 * @req:	pointer to pending request
 * @status:	placeholder for status
 *
 * For each sent request, Guc shall send bac CT response message.
 * Our message handler will update status of tracked request once
 * response message with given fence is received. Wait here and
 * check for valid response status value.
 *
 * Return:
 * *	0 response received (status is valid)
 * *	-ETIMEDOUT no response within hardcoded timeout
 */
static int wait_for_ct_request_update(struct ct_request *req, u32 *status)
{
	int err;

	/*
	 * Fast commands should complete in less than 10us, so sample quickly
	 * up to that length of time, then switch to a slower sleep-wait loop.
	 * No GuC command should ever take longer than 10ms.
	 */
#define done INTEL_GUC_MSG_IS_RESPONSE(READ_ONCE(req->status))
	err = wait_for_us(done, 10);
	if (err)
		err = wait_for(done, 10);
#undef done

	if (unlikely(err))
		DRM_ERROR("CT: fence %u err %d\n", req->fence, err);

	*status = req->status;
	return err;
}

static int ctch_send(struct intel_guc_ct *ct,
		     struct intel_guc_ct_channel *ctch,
		     const u32 *action,
		     u32 len,
		     u32 *response_buf,
		     u32 response_buf_size,
		     u32 *status)
{
	struct intel_guc_ct_buffer *ctb = &ctch->ctbs[CTB_SEND];
	struct guc_ct_buffer_desc *desc = ctb->desc;
	struct ct_request request;
	unsigned long flags;
	u32 fence;
	int err;

	GEM_BUG_ON(!ctch_is_open(ctch));
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len & ~GUC_CT_MSG_LEN_MASK);
	GEM_BUG_ON(!response_buf && response_buf_size);

	fence = ctch_get_next_fence(ctch);
	request.fence = fence;
	request.status = 0;
	request.response_len = response_buf_size;
	request.response_buf = response_buf;

	spin_lock_irqsave(&ct->lock, flags);
	list_add_tail(&request.link, &ct->pending_requests);
	spin_unlock_irqrestore(&ct->lock, flags);

	err = ctb_write(ctb, action, len, fence, !!response_buf);
	if (unlikely(err))
		goto unlink;

	intel_guc_notify(ct_to_guc(ct));

	if (response_buf)
		err = wait_for_ct_request_update(&request, status);
	else
		err = wait_for_ctb_desc_update(desc, fence, status);
	if (unlikely(err))
		goto unlink;

	if (!INTEL_GUC_MSG_IS_RESPONSE_SUCCESS(*status)) {
		err = -EIO;
		goto unlink;
	}

	if (response_buf) {
		/* There shall be no data in the status */
		WARN_ON(INTEL_GUC_MSG_TO_DATA(request.status));
		/* Return actual response len */
		err = request.response_len;
	} else {
		/* There shall be no response payload */
		WARN_ON(request.response_len);
		/* Return data decoded from the status dword */
		err = INTEL_GUC_MSG_TO_DATA(*status);
	}

unlink:
	spin_lock_irqsave(&ct->lock, flags);
	list_del(&request.link);
	spin_unlock_irqrestore(&ct->lock, flags);

	return err;
}

/*
 * Command Transport (CT) buffer based GuC send function.
 */
static int intel_guc_send_ct(struct intel_guc *guc, const u32 *action, u32 len,
			     u32 *response_buf, u32 response_buf_size)
{
	struct intel_guc_ct *ct = &guc->ct;
	struct intel_guc_ct_channel *ctch = &ct->host_channel;
	u32 status = ~0; /* undefined */
	int ret;

	mutex_lock(&guc->send_mutex);

	ret = ctch_send(ct, ctch, action, len, response_buf, response_buf_size,
			&status);
	if (unlikely(ret < 0)) {
		DRM_ERROR("CT: send action %#X failed; err=%d status=%#X\n",
			  action[0], ret, status);
	} else if (unlikely(ret)) {
		CT_DEBUG_DRIVER("CT: send action %#x returned %d (%#x)\n",
				action[0], ret, ret);
	}

	mutex_unlock(&guc->send_mutex);
	return ret;
}

static inline unsigned int ct_header_get_len(u32 header)
{
	return (header >> GUC_CT_MSG_LEN_SHIFT) & GUC_CT_MSG_LEN_MASK;
}

static inline unsigned int ct_header_get_action(u32 header)
{
	return (header >> GUC_CT_MSG_ACTION_SHIFT) & GUC_CT_MSG_ACTION_MASK;
}

static inline bool ct_header_is_response(u32 header)
{
	return ct_header_get_action(header) == INTEL_GUC_ACTION_DEFAULT;
}

static int ctb_read(struct intel_guc_ct_buffer *ctb, u32 *data)
{
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head = desc->head / 4;	/* in dwords */
	u32 tail = desc->tail / 4;	/* in dwords */
	u32 size = desc->size / 4;	/* in dwords */
	u32 *cmds = ctb->cmds;
	s32 available;			/* in dwords */
	unsigned int len;
	unsigned int i;

	GEM_BUG_ON(desc->size % 4);
	GEM_BUG_ON(desc->head % 4);
	GEM_BUG_ON(desc->tail % 4);
	GEM_BUG_ON(tail >= size);
	GEM_BUG_ON(head >= size);

	/* tail == head condition indicates empty */
	available = tail - head;
	if (unlikely(available == 0))
		return -ENODATA;

	/* beware of buffer wrap case */
	if (unlikely(available < 0))
		available += size;
	CT_DEBUG_DRIVER("CT: available %d (%u:%u)\n", available, head, tail);
	GEM_BUG_ON(available < 0);

	data[0] = cmds[head];
	head = (head + 1) % size;

	/* message len with header */
	len = ct_header_get_len(data[0]) + 1;
	if (unlikely(len > (u32)available)) {
		DRM_ERROR("CT: incomplete message %*phn %*phn %*phn\n",
			  4, data,
			  4 * (head + available - 1 > size ?
			       size - head : available - 1), &cmds[head],
			  4 * (head + available - 1 > size ?
			       available - 1 - size + head : 0), &cmds[0]);
		return -EPROTO;
	}

	for (i = 1; i < len; i++) {
		data[i] = cmds[head];
		head = (head + 1) % size;
	}
	CT_DEBUG_DRIVER("CT: received %*phn\n", 4 * len, data);

	desc->head = head * 4;
	return 0;
}

/**
 * DOC: CTB GuC to Host response
 *
 * Format of the CTB GuC to Host response message is as follows::
 *
 *      +------------+---------+---------+---------+---------+---------+
 *      |   msg[0]   |   [1]   |   [2]   |   [3]   |   ...   |  [n-1]  |
 *      +------------+---------+---------+---------+---------+---------+
 *      |   MESSAGE  |       MESSAGE PAYLOAD                           |
 *      +   HEADER   +---------+---------+---------+---------+---------+
 *      |            |    0    |    1    |    2    |   ...   |    n    |
 *      +============+=========+=========+=========+=========+=========+
 *      |  len >= 2  |  FENCE  |  STATUS |   response specific data    |
 *      +------+-----+---------+---------+---------+---------+---------+
 *
 *                   ^-----------------------len-----------------------^
 */

static int ct_handle_response(struct intel_guc_ct *ct, const u32 *msg)
{
	u32 header = msg[0];
	u32 len = ct_header_get_len(header);
	u32 msglen = len + 1; /* total message length including header */
	u32 fence;
	u32 status;
	u32 datalen;
	struct ct_request *req;
	bool found = false;

	GEM_BUG_ON(!ct_header_is_response(header));
	GEM_BUG_ON(!in_irq());

	/* Response payload shall at least include fence and status */
	if (unlikely(len < 2)) {
		DRM_ERROR("CT: corrupted response %*phn\n", 4 * msglen, msg);
		return -EPROTO;
	}

	fence = msg[1];
	status = msg[2];
	datalen = len - 2;

	/* Format of the status follows RESPONSE message */
	if (unlikely(!INTEL_GUC_MSG_IS_RESPONSE(status))) {
		DRM_ERROR("CT: corrupted response %*phn\n", 4 * msglen, msg);
		return -EPROTO;
	}

	CT_DEBUG_DRIVER("CT: response fence %u status %#x\n", fence, status);

	spin_lock(&ct->lock);
	list_for_each_entry(req, &ct->pending_requests, link) {
		if (unlikely(fence != req->fence)) {
			CT_DEBUG_DRIVER("CT: request %u awaits response\n",
					req->fence);
			continue;
		}
		if (unlikely(datalen > req->response_len)) {
			DRM_ERROR("CT: response %u too long %*phn\n",
				  req->fence, 4 * msglen, msg);
			datalen = 0;
		}
		if (datalen)
			memcpy(req->response_buf, msg + 3, 4 * datalen);
		req->response_len = datalen;
		WRITE_ONCE(req->status, status);
		found = true;
		break;
	}
	spin_unlock(&ct->lock);

	if (!found)
		DRM_ERROR("CT: unsolicited response %*phn\n", 4 * msglen, msg);
	return 0;
}

static void ct_process_request(struct intel_guc_ct *ct,
			       u32 action, u32 len, const u32 *payload)
{
	struct intel_guc *guc = ct_to_guc(ct);

	CT_DEBUG_DRIVER("CT: request %x %*phn\n", action, 4 * len, payload);

	switch (action) {
	case INTEL_GUC_ACTION_DEFAULT:
		if (unlikely(len < 1))
			goto fail_unexpected;
		intel_guc_to_host_process_recv_msg(guc, *payload);
		break;

	default:
fail_unexpected:
		DRM_ERROR("CT: unexpected request %x %*phn\n",
			  action, 4 * len, payload);
		break;
	}
}

static bool ct_process_incoming_requests(struct intel_guc_ct *ct)
{
	unsigned long flags;
	struct ct_incoming_request *request;
	u32 header;
	u32 *payload;
	bool done;

	spin_lock_irqsave(&ct->lock, flags);
	request = list_first_entry_or_null(&ct->incoming_requests,
					   struct ct_incoming_request, link);
	if (request)
		list_del(&request->link);
	done = !!list_empty(&ct->incoming_requests);
	spin_unlock_irqrestore(&ct->lock, flags);

	if (!request)
		return true;

	header = request->msg[0];
	payload = &request->msg[1];
	ct_process_request(ct,
			   ct_header_get_action(header),
			   ct_header_get_len(header),
			   payload);

	kfree(request);
	return done;
}

static void ct_incoming_request_worker_func(struct work_struct *w)
{
	struct intel_guc_ct *ct = container_of(w, struct intel_guc_ct, worker);
	bool done;

	done = ct_process_incoming_requests(ct);
	if (!done)
		queue_work(system_unbound_wq, &ct->worker);
}

/**
 * DOC: CTB GuC to Host request
 *
 * Format of the CTB GuC to Host request message is as follows::
 *
 *      +------------+---------+---------+---------+---------+---------+
 *      |   msg[0]   |   [1]   |   [2]   |   [3]   |   ...   |  [n-1]  |
 *      +------------+---------+---------+---------+---------+---------+
 *      |   MESSAGE  |       MESSAGE PAYLOAD                           |
 *      +   HEADER   +---------+---------+---------+---------+---------+
 *      |            |    0    |    1    |    2    |   ...   |    n    |
 *      +============+=========+=========+=========+=========+=========+
 *      |     len    |            request specific data                |
 *      +------+-----+---------+---------+---------+---------+---------+
 *
 *                   ^-----------------------len-----------------------^
 */

static int ct_handle_request(struct intel_guc_ct *ct, const u32 *msg)
{
	u32 header = msg[0];
	u32 len = ct_header_get_len(header);
	u32 msglen = len + 1; /* total message length including header */
	struct ct_incoming_request *request;
	unsigned long flags;

	GEM_BUG_ON(ct_header_is_response(header));

	request = kmalloc(sizeof(*request) + 4 * msglen, GFP_ATOMIC);
	if (unlikely(!request)) {
		DRM_ERROR("CT: dropping request %*phn\n", 4 * msglen, msg);
		return 0; /* XXX: -ENOMEM ? */
	}
	memcpy(request->msg, msg, 4 * msglen);

	spin_lock_irqsave(&ct->lock, flags);
	list_add_tail(&request->link, &ct->incoming_requests);
	spin_unlock_irqrestore(&ct->lock, flags);

	queue_work(system_unbound_wq, &ct->worker);
	return 0;
}

static void ct_process_host_channel(struct intel_guc_ct *ct)
{
	struct intel_guc_ct_channel *ctch = &ct->host_channel;
	struct intel_guc_ct_buffer *ctb = &ctch->ctbs[CTB_RECV];
	u32 msg[GUC_CT_MSG_LEN_MASK + 1]; /* one extra dw for the header */
	int err = 0;

	if (!ctch_is_open(ctch))
		return;

	do {
		err = ctb_read(ctb, msg);
		if (err)
			break;

		if (ct_header_is_response(msg[0]))
			err = ct_handle_response(ct, msg);
		else
			err = ct_handle_request(ct, msg);
	} while (!err);

	if (GEM_WARN_ON(err == -EPROTO)) {
		DRM_ERROR("CT: corrupted message detected!\n");
		ctb->desc->is_in_error = 1;
	}
}

/*
 * When we're communicating with the GuC over CT, GuC uses events
 * to notify us about new messages being posted on the RECV buffer.
 */
static void intel_guc_to_host_event_handler_ct(struct intel_guc *guc)
{
	struct intel_guc_ct *ct = &guc->ct;

	ct_process_host_channel(ct);
}

/**
 * intel_guc_ct_enable - Enable buffer based command transport.
 * @ct: pointer to CT struct
 *
 * Shall only be called for platforms with HAS_GUC_CT.
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_guc_ct_enable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	struct drm_i915_private *i915 = guc_to_i915(guc);
	struct intel_guc_ct_channel *ctch = &ct->host_channel;
	int err;

	GEM_BUG_ON(!HAS_GUC_CT(i915));

	err = ctch_open(guc, ctch);
	if (unlikely(err))
		return err;

	/* Switch into cmd transport buffer based send() */
	guc->send = intel_guc_send_ct;
	guc->handler = intel_guc_to_host_event_handler_ct;
	DRM_INFO("CT: %s\n", enableddisabled(true));
	return 0;
}

/**
 * intel_guc_ct_disable - Disable buffer based command transport.
 * @ct: pointer to CT struct
 *
 * Shall only be called for platforms with HAS_GUC_CT.
 */
void intel_guc_ct_disable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	struct drm_i915_private *i915 = guc_to_i915(guc);
	struct intel_guc_ct_channel *ctch = &ct->host_channel;

	GEM_BUG_ON(!HAS_GUC_CT(i915));

	if (!ctch_is_open(ctch))
		return;

	ctch_close(guc, ctch);

	/* Disable send */
	guc->send = intel_guc_send_nop;
	guc->handler = intel_guc_to_host_event_handler_nop;
	DRM_INFO("CT: %s\n", enableddisabled(false));
}
