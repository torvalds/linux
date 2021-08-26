// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_guc_ct.h"
#include "gt/intel_gt.h"

static inline struct intel_guc *ct_to_guc(struct intel_guc_ct *ct)
{
	return container_of(ct, struct intel_guc, ct);
}

static inline struct intel_gt *ct_to_gt(struct intel_guc_ct *ct)
{
	return guc_to_gt(ct_to_guc(ct));
}

static inline struct drm_i915_private *ct_to_i915(struct intel_guc_ct *ct)
{
	return ct_to_gt(ct)->i915;
}

static inline struct drm_device *ct_to_drm(struct intel_guc_ct *ct)
{
	return &ct_to_i915(ct)->drm;
}

#define CT_ERROR(_ct, _fmt, ...) \
	drm_err(ct_to_drm(_ct), "CT: " _fmt, ##__VA_ARGS__)
#ifdef CONFIG_DRM_I915_DEBUG_GUC
#define CT_DEBUG(_ct, _fmt, ...) \
	drm_dbg(ct_to_drm(_ct), "CT: " _fmt, ##__VA_ARGS__)
#else
#define CT_DEBUG(...)	do { } while (0)
#endif
#define CT_PROBE_ERROR(_ct, _fmt, ...) \
	i915_probe_error(ct_to_i915(ct), "CT: " _fmt, ##__VA_ARGS__)

/**
 * DOC: CTB Blob
 *
 * We allocate single blob to hold both CTB descriptors and buffers:
 *
 *      +--------+-----------------------------------------------+------+
 *      | offset | contents                                      | size |
 *      +========+===============================================+======+
 *      | 0x0000 | H2G `CTB Descriptor`_ (send)                  |      |
 *      +--------+-----------------------------------------------+  4K  |
 *      | 0x0800 | G2H `CTB Descriptor`_ (recv)                  |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | H2G `CT Buffer`_ (send)                       | n*4K |
 *      |        |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | G2H `CT Buffer`_ (recv)                       | m*4K |
 *      | + n*4K |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *
 * Size of each `CT Buffer`_ must be multiple of 4K.
 * As we don't expect too many messages, for now use minimum sizes.
 */
#define CTB_DESC_SIZE		ALIGN(sizeof(struct guc_ct_buffer_desc), SZ_2K)
#define CTB_H2G_BUFFER_SIZE	(SZ_4K)
#define CTB_G2H_BUFFER_SIZE	(SZ_4K)

struct ct_request {
	struct list_head link;
	u32 fence;
	u32 status;
	u32 response_len;
	u32 *response_buf;
};

struct ct_incoming_msg {
	struct list_head link;
	u32 size;
	u32 msg[];
};

enum { CTB_SEND = 0, CTB_RECV = 1 };

enum { CTB_OWNER_HOST = 0 };

static void ct_receive_tasklet_func(struct tasklet_struct *t);
static void ct_incoming_request_worker_func(struct work_struct *w);

/**
 * intel_guc_ct_init_early - Initialize CT state without requiring device access
 * @ct: pointer to CT struct
 */
void intel_guc_ct_init_early(struct intel_guc_ct *ct)
{
	spin_lock_init(&ct->ctbs.send.lock);
	spin_lock_init(&ct->ctbs.recv.lock);
	spin_lock_init(&ct->requests.lock);
	INIT_LIST_HEAD(&ct->requests.pending);
	INIT_LIST_HEAD(&ct->requests.incoming);
	INIT_WORK(&ct->requests.worker, ct_incoming_request_worker_func);
	tasklet_setup(&ct->receive_tasklet, ct_receive_tasklet_func);
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
				    u32 cmds_addr, u32 size)
{
	memset(desc, 0, sizeof(*desc));
	desc->addr = cmds_addr;
	desc->size = size;
	desc->owner = CTB_OWNER_HOST;
}

static void guc_ct_buffer_reset(struct intel_guc_ct_buffer *ctb, u32 cmds_addr)
{
	guc_ct_buffer_desc_init(ctb->desc, cmds_addr, ctb->size);
}

static void guc_ct_buffer_init(struct intel_guc_ct_buffer *ctb,
			       struct guc_ct_buffer_desc *desc,
			       u32 *cmds, u32 size)
{
	GEM_BUG_ON(size % 4);

	ctb->desc = desc;
	ctb->cmds = cmds;
	ctb->size = size;

	guc_ct_buffer_reset(ctb, 0);
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

	/* Can't use generic send(), CT registration must go over MMIO */
	return intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
}

static int ct_register_buffer(struct intel_guc_ct *ct, u32 desc_addr, u32 type)
{
	int err = guc_action_register_ct_buffer(ct_to_guc(ct), desc_addr, type);

	if (unlikely(err))
		CT_ERROR(ct, "Failed to register %s buffer (err=%d)\n",
			 guc_ct_buffer_type_to_str(type), err);
	return err;
}

static int guc_action_deregister_ct_buffer(struct intel_guc *guc, u32 type)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER,
		CTB_OWNER_HOST,
		type
	};

	/* Can't use generic send(), CT deregistration must go over MMIO */
	return intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
}

static int ct_deregister_buffer(struct intel_guc_ct *ct, u32 type)
{
	int err = guc_action_deregister_ct_buffer(ct_to_guc(ct), type);

	if (unlikely(err))
		CT_ERROR(ct, "Failed to deregister %s buffer (err=%d)\n",
			 guc_ct_buffer_type_to_str(type), err);
	return err;
}

/**
 * intel_guc_ct_init - Init buffer-based communication
 * @ct: pointer to CT struct
 *
 * Allocate memory required for buffer-based communication.
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_guc_ct_init(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	struct guc_ct_buffer_desc *desc;
	u32 blob_size;
	u32 cmds_size;
	void *blob;
	u32 *cmds;
	int err;

	GEM_BUG_ON(ct->vma);

	blob_size = 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE + CTB_G2H_BUFFER_SIZE;
	err = intel_guc_allocate_and_map_vma(guc, blob_size, &ct->vma, &blob);
	if (unlikely(err)) {
		CT_PROBE_ERROR(ct, "Failed to allocate %u for CTB data (%pe)\n",
			       blob_size, ERR_PTR(err));
		return err;
	}

	CT_DEBUG(ct, "base=%#x size=%u\n", intel_guc_ggtt_offset(guc, ct->vma), blob_size);

	/* store pointers to desc and cmds for send ctb */
	desc = blob;
	cmds = blob + 2 * CTB_DESC_SIZE;
	cmds_size = CTB_H2G_BUFFER_SIZE;
	CT_DEBUG(ct, "%s desc %#tx cmds %#tx size %u\n", "send",
		 ptrdiff(desc, blob), ptrdiff(cmds, blob), cmds_size);

	guc_ct_buffer_init(&ct->ctbs.send, desc, cmds, cmds_size);

	/* store pointers to desc and cmds for recv ctb */
	desc = blob + CTB_DESC_SIZE;
	cmds = blob + 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE;
	cmds_size = CTB_G2H_BUFFER_SIZE;
	CT_DEBUG(ct, "%s desc %#tx cmds %#tx size %u\n", "recv",
		 ptrdiff(desc, blob), ptrdiff(cmds, blob), cmds_size);

	guc_ct_buffer_init(&ct->ctbs.recv, desc, cmds, cmds_size);

	return 0;
}

/**
 * intel_guc_ct_fini - Fini buffer-based communication
 * @ct: pointer to CT struct
 *
 * Deallocate memory required for buffer-based communication.
 */
void intel_guc_ct_fini(struct intel_guc_ct *ct)
{
	GEM_BUG_ON(ct->enabled);

	tasklet_kill(&ct->receive_tasklet);
	i915_vma_unpin_and_release(&ct->vma, I915_VMA_RELEASE_MAP);
	memset(ct, 0, sizeof(*ct));
}

/**
 * intel_guc_ct_enable - Enable buffer based command transport.
 * @ct: pointer to CT struct
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_guc_ct_enable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	u32 base, cmds;
	void *blob;
	int err;

	GEM_BUG_ON(ct->enabled);

	/* vma should be already allocated and map'ed */
	GEM_BUG_ON(!ct->vma);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(ct->vma->obj));
	base = intel_guc_ggtt_offset(guc, ct->vma);

	/* blob should start with send descriptor */
	blob = __px_vaddr(ct->vma->obj);
	GEM_BUG_ON(blob != ct->ctbs.send.desc);

	/* (re)initialize descriptors */
	cmds = base + ptrdiff(ct->ctbs.send.cmds, blob);
	guc_ct_buffer_reset(&ct->ctbs.send, cmds);

	cmds = base + ptrdiff(ct->ctbs.recv.cmds, blob);
	guc_ct_buffer_reset(&ct->ctbs.recv, cmds);

	/*
	 * Register both CT buffers starting with RECV buffer.
	 * Descriptors are in first half of the blob.
	 */
	err = ct_register_buffer(ct, base + ptrdiff(ct->ctbs.recv.desc, blob),
				 INTEL_GUC_CT_BUFFER_TYPE_RECV);
	if (unlikely(err))
		goto err_out;

	err = ct_register_buffer(ct, base + ptrdiff(ct->ctbs.send.desc, blob),
				 INTEL_GUC_CT_BUFFER_TYPE_SEND);
	if (unlikely(err))
		goto err_deregister;

	ct->enabled = true;

	return 0;

err_deregister:
	ct_deregister_buffer(ct, INTEL_GUC_CT_BUFFER_TYPE_RECV);
err_out:
	CT_PROBE_ERROR(ct, "Failed to enable CTB (%pe)\n", ERR_PTR(err));
	return err;
}

/**
 * intel_guc_ct_disable - Disable buffer based command transport.
 * @ct: pointer to CT struct
 */
void intel_guc_ct_disable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);

	GEM_BUG_ON(!ct->enabled);

	ct->enabled = false;

	if (intel_guc_is_fw_running(guc)) {
		ct_deregister_buffer(ct, INTEL_GUC_CT_BUFFER_TYPE_SEND);
		ct_deregister_buffer(ct, INTEL_GUC_CT_BUFFER_TYPE_RECV);
	}
}

static u32 ct_get_next_fence(struct intel_guc_ct *ct)
{
	/* For now it's trivial */
	return ++ct->requests.last_fence;
}

static void write_barrier(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	struct intel_gt *gt = guc_to_gt(guc);

	if (i915_gem_object_is_lmem(guc->ct.vma->obj)) {
		GEM_BUG_ON(guc->send_regs.fw_domains);
		/*
		 * This register is used by the i915 and GuC for MMIO based
		 * communication. Once we are in this code CTBs are the only
		 * method the i915 uses to communicate with the GuC so it is
		 * safe to write to this register (a value of 0 is NOP for MMIO
		 * communication). If we ever start mixing CTBs and MMIOs a new
		 * register will have to be chosen.
		 */
		intel_uncore_write_fw(gt->uncore, GEN11_SOFT_SCRATCH(0), 0);
	} else {
		/* wmb() sufficient for a barrier if in smem */
		wmb();
	}
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

static int ct_write(struct intel_guc_ct *ct,
		    const u32 *action,
		    u32 len /* in dwords */,
		    u32 fence)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.send;
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head = desc->head;
	u32 tail = desc->tail;
	u32 size = ctb->size;
	u32 used;
	u32 header;
	u32 *cmds = ctb->cmds;
	unsigned int i;

	if (unlikely(desc->is_in_error))
		return -EPIPE;

	if (unlikely(!IS_ALIGNED(head | tail, 4) ||
		     (tail | head) >= size))
		goto corrupted;

	/* later calculations will be done in dwords */
	head /= 4;
	tail /= 4;
	size /= 4;

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
		 GUC_CT_MSG_SEND_STATUS |
		 (action[0] << GUC_CT_MSG_ACTION_SHIFT);

	CT_DEBUG(ct, "writing %*ph %*ph %*ph\n",
		 4, &header, 4, &fence, 4 * (len - 1), &action[1]);

	cmds[tail] = header;
	tail = (tail + 1) % size;

	cmds[tail] = fence;
	tail = (tail + 1) % size;

	for (i = 1; i < len; i++) {
		cmds[tail] = action[i];
		tail = (tail + 1) % size;
	}
	GEM_BUG_ON(tail > size);

	/*
	 * make sure H2G buffer update and LRC tail update (if this triggering a
	 * submission) are visible before updating the descriptor tail
	 */
	write_barrier(ct);

	/* now update desc tail (back in bytes) */
	desc->tail = tail * 4;
	return 0;

corrupted:
	CT_ERROR(ct, "Corrupted descriptor addr=%#x head=%u tail=%u size=%u\n",
		 desc->addr, desc->head, desc->tail, desc->size);
	desc->is_in_error = 1;
	return -EPIPE;
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

static int ct_send(struct intel_guc_ct *ct,
		   const u32 *action,
		   u32 len,
		   u32 *response_buf,
		   u32 response_buf_size,
		   u32 *status)
{
	struct ct_request request;
	unsigned long flags;
	u32 fence;
	int err;

	GEM_BUG_ON(!ct->enabled);
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len & ~GUC_CT_MSG_LEN_MASK);
	GEM_BUG_ON(!response_buf && response_buf_size);

	spin_lock_irqsave(&ct->ctbs.send.lock, flags);

	fence = ct_get_next_fence(ct);
	request.fence = fence;
	request.status = 0;
	request.response_len = response_buf_size;
	request.response_buf = response_buf;

	spin_lock(&ct->requests.lock);
	list_add_tail(&request.link, &ct->requests.pending);
	spin_unlock(&ct->requests.lock);

	err = ct_write(ct, action, len, fence);

	spin_unlock_irqrestore(&ct->ctbs.send.lock, flags);

	if (unlikely(err))
		goto unlink;

	intel_guc_notify(ct_to_guc(ct));

	err = wait_for_ct_request_update(&request, status);
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
	spin_lock_irqsave(&ct->requests.lock, flags);
	list_del(&request.link);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	return err;
}

/*
 * Command Transport (CT) buffer based GuC send function.
 */
int intel_guc_ct_send(struct intel_guc_ct *ct, const u32 *action, u32 len,
		      u32 *response_buf, u32 response_buf_size)
{
	u32 status = ~0; /* undefined */
	int ret;

	if (unlikely(!ct->enabled)) {
		WARN(1, "Unexpected send: action=%#x\n", *action);
		return -ENODEV;
	}

	ret = ct_send(ct, action, len, response_buf, response_buf_size, &status);
	if (unlikely(ret < 0)) {
		CT_ERROR(ct, "Sending action %#x failed (err=%d status=%#X)\n",
			 action[0], ret, status);
	} else if (unlikely(ret)) {
		CT_DEBUG(ct, "send action %#x returned %d (%#x)\n",
			 action[0], ret, ret);
	}

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
	return !!(header & GUC_CT_MSG_IS_RESPONSE);
}

static struct ct_incoming_msg *ct_alloc_msg(u32 num_dwords)
{
	struct ct_incoming_msg *msg;

	msg = kmalloc(sizeof(*msg) + sizeof(u32) * num_dwords, GFP_ATOMIC);
	if (msg)
		msg->size = num_dwords;
	return msg;
}

static void ct_free_msg(struct ct_incoming_msg *msg)
{
	kfree(msg);
}

/*
 * Return: number available remaining dwords to read (0 if empty)
 *         or a negative error code on failure
 */
static int ct_read(struct intel_guc_ct *ct, struct ct_incoming_msg **msg)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.recv;
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head = desc->head;
	u32 tail = desc->tail;
	u32 size = ctb->size;
	u32 *cmds = ctb->cmds;
	s32 available;
	unsigned int len;
	unsigned int i;
	u32 header;

	if (unlikely(desc->is_in_error))
		return -EPIPE;

	if (unlikely(!IS_ALIGNED(head | tail, 4) ||
		     (tail | head) >= size))
		goto corrupted;

	/* later calculations will be done in dwords */
	head /= 4;
	tail /= 4;
	size /= 4;

	/* tail == head condition indicates empty */
	available = tail - head;
	if (unlikely(available == 0)) {
		*msg = NULL;
		return 0;
	}

	/* beware of buffer wrap case */
	if (unlikely(available < 0))
		available += size;
	CT_DEBUG(ct, "available %d (%u:%u)\n", available, head, tail);
	GEM_BUG_ON(available < 0);

	header = cmds[head];
	head = (head + 1) % size;

	/* message len with header */
	len = ct_header_get_len(header) + 1;
	if (unlikely(len > (u32)available)) {
		CT_ERROR(ct, "Incomplete message %*ph %*ph %*ph\n",
			 4, &header,
			 4 * (head + available - 1 > size ?
			      size - head : available - 1), &cmds[head],
			 4 * (head + available - 1 > size ?
			      available - 1 - size + head : 0), &cmds[0]);
		goto corrupted;
	}

	*msg = ct_alloc_msg(len);
	if (!*msg) {
		CT_ERROR(ct, "No memory for message %*ph %*ph %*ph\n",
			 4, &header,
			 4 * (head + available - 1 > size ?
			      size - head : available - 1), &cmds[head],
			 4 * (head + available - 1 > size ?
			      available - 1 - size + head : 0), &cmds[0]);
		return available;
	}

	(*msg)->msg[0] = header;

	for (i = 1; i < len; i++) {
		(*msg)->msg[i] = cmds[head];
		head = (head + 1) % size;
	}
	CT_DEBUG(ct, "received %*ph\n", 4 * len, (*msg)->msg);

	desc->head = head * 4;
	return available - len;

corrupted:
	CT_ERROR(ct, "Corrupted descriptor addr=%#x head=%u tail=%u size=%u\n",
		 desc->addr, desc->head, desc->tail, desc->size);
	desc->is_in_error = 1;
	return -EPIPE;
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

static int ct_handle_response(struct intel_guc_ct *ct, struct ct_incoming_msg *response)
{
	u32 header = response->msg[0];
	u32 len = ct_header_get_len(header);
	u32 fence;
	u32 status;
	u32 datalen;
	struct ct_request *req;
	unsigned long flags;
	bool found = false;
	int err = 0;

	GEM_BUG_ON(!ct_header_is_response(header));

	/* Response payload shall at least include fence and status */
	if (unlikely(len < 2)) {
		CT_ERROR(ct, "Corrupted response (len %u)\n", len);
		return -EPROTO;
	}

	fence = response->msg[1];
	status = response->msg[2];
	datalen = len - 2;

	/* Format of the status follows RESPONSE message */
	if (unlikely(!INTEL_GUC_MSG_IS_RESPONSE(status))) {
		CT_ERROR(ct, "Corrupted response (status %#x)\n", status);
		return -EPROTO;
	}

	CT_DEBUG(ct, "response fence %u status %#x\n", fence, status);

	spin_lock_irqsave(&ct->requests.lock, flags);
	list_for_each_entry(req, &ct->requests.pending, link) {
		if (unlikely(fence != req->fence)) {
			CT_DEBUG(ct, "request %u awaits response\n",
				 req->fence);
			continue;
		}
		if (unlikely(datalen > req->response_len)) {
			CT_ERROR(ct, "Response %u too long (datalen %u > %u)\n",
				 req->fence, datalen, req->response_len);
			datalen = min(datalen, req->response_len);
			err = -EMSGSIZE;
		}
		if (datalen)
			memcpy(req->response_buf, response->msg + 3, 4 * datalen);
		req->response_len = datalen;
		WRITE_ONCE(req->status, status);
		found = true;
		break;
	}
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	if (!found) {
		CT_ERROR(ct, "Unsolicited response (fence %u)\n", fence);
		return -ENOKEY;
	}

	if (unlikely(err))
		return err;

	ct_free_msg(response);
	return 0;
}

static int ct_process_request(struct intel_guc_ct *ct, struct ct_incoming_msg *request)
{
	struct intel_guc *guc = ct_to_guc(ct);
	u32 header, action, len;
	const u32 *payload;
	int ret;

	header = request->msg[0];
	payload = &request->msg[1];
	action = ct_header_get_action(header);
	len = ct_header_get_len(header);

	CT_DEBUG(ct, "request %x %*ph\n", action, 4 * len, payload);

	switch (action) {
	case INTEL_GUC_ACTION_DEFAULT:
		ret = intel_guc_to_host_process_recv_msg(guc, payload, len);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	if (unlikely(ret)) {
		CT_ERROR(ct, "Failed to process request %04x (%pe)\n",
			 action, ERR_PTR(ret));
		return ret;
	}

	ct_free_msg(request);
	return 0;
}

static bool ct_process_incoming_requests(struct intel_guc_ct *ct)
{
	unsigned long flags;
	struct ct_incoming_msg *request;
	bool done;
	int err;

	spin_lock_irqsave(&ct->requests.lock, flags);
	request = list_first_entry_or_null(&ct->requests.incoming,
					   struct ct_incoming_msg, link);
	if (request)
		list_del(&request->link);
	done = !!list_empty(&ct->requests.incoming);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	if (!request)
		return true;

	err = ct_process_request(ct, request);
	if (unlikely(err)) {
		CT_ERROR(ct, "Failed to process CT message (%pe) %*ph\n",
			 ERR_PTR(err), 4 * request->size, request->msg);
		ct_free_msg(request);
	}

	return done;
}

static void ct_incoming_request_worker_func(struct work_struct *w)
{
	struct intel_guc_ct *ct =
		container_of(w, struct intel_guc_ct, requests.worker);
	bool done;

	done = ct_process_incoming_requests(ct);
	if (!done)
		queue_work(system_unbound_wq, &ct->requests.worker);
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

static int ct_handle_request(struct intel_guc_ct *ct, struct ct_incoming_msg *request)
{
	unsigned long flags;

	GEM_BUG_ON(ct_header_is_response(request->msg[0]));

	spin_lock_irqsave(&ct->requests.lock, flags);
	list_add_tail(&request->link, &ct->requests.incoming);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	queue_work(system_unbound_wq, &ct->requests.worker);
	return 0;
}

static void ct_handle_msg(struct intel_guc_ct *ct, struct ct_incoming_msg *msg)
{
	u32 header = msg->msg[0];
	int err;

	if (ct_header_is_response(header))
		err = ct_handle_response(ct, msg);
	else
		err = ct_handle_request(ct, msg);

	if (unlikely(err)) {
		CT_ERROR(ct, "Failed to process CT message (%pe) %*ph\n",
			 ERR_PTR(err), 4 * msg->size, msg->msg);
		ct_free_msg(msg);
	}
}

/*
 * Return: number available remaining dwords to read (0 if empty)
 *         or a negative error code on failure
 */
static int ct_receive(struct intel_guc_ct *ct)
{
	struct ct_incoming_msg *msg = NULL;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ct->ctbs.recv.lock, flags);
	ret = ct_read(ct, &msg);
	spin_unlock_irqrestore(&ct->ctbs.recv.lock, flags);
	if (ret < 0)
		return ret;

	if (msg)
		ct_handle_msg(ct, msg);

	return ret;
}

static void ct_try_receive_message(struct intel_guc_ct *ct)
{
	int ret;

	if (GEM_WARN_ON(!ct->enabled))
		return;

	ret = ct_receive(ct);
	if (ret > 0)
		tasklet_hi_schedule(&ct->receive_tasklet);
}

static void ct_receive_tasklet_func(struct tasklet_struct *t)
{
	struct intel_guc_ct *ct = from_tasklet(ct, t, receive_tasklet);

	ct_try_receive_message(ct);
}

/*
 * When we're communicating with the GuC over CT, GuC uses events
 * to notify us about new messages being posted on the RECV buffer.
 */
void intel_guc_ct_event_handler(struct intel_guc_ct *ct)
{
	if (unlikely(!ct->enabled)) {
		WARN(1, "Unexpected GuC event received while CT disabled!\n");
		return;
	}

	ct_try_receive_message(ct);
}
