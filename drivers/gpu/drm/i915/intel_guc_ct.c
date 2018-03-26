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

enum { CTB_SEND = 0, CTB_RECV = 1 };

enum { CTB_OWNER_HOST = 0 };

/**
 * intel_guc_ct_init_early - Initialize CT state without requiring device access
 * @ct: pointer to CT struct
 */
void intel_guc_ct_init_early(struct intel_guc_ct *ct)
{
	/* we're using static channel owners */
	ct->host_channel.owner = CTB_OWNER_HOST;
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
	DRM_DEBUG_DRIVER("CT: desc %p init addr=%#x size=%u owner=%u\n",
			 desc, cmds_addr, size, owner);
	memset(desc, 0, sizeof(*desc));
	desc->addr = cmds_addr;
	desc->size = size;
	desc->owner = owner;
}

static void guc_ct_buffer_desc_reset(struct guc_ct_buffer_desc *desc)
{
	DRM_DEBUG_DRIVER("CT: desc %p reset head=%u tail=%u\n",
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
	err = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action));
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
	err = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action));
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
	DRM_DEBUG_DRIVER("CT: vma base=%#x\n",
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
	DRM_DEBUG_DRIVER("CT: channel %d initialization failed; err=%d\n",
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

	DRM_DEBUG_DRIVER("CT: channel %d reopen=%s\n",
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

static int ctb_write(struct intel_guc_ct_buffer *ctb,
		     const u32 *action,
		     u32 len /* in dwords */,
		     u32 fence)
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

	/* Write the message. The format is the following:
	 * DW0: header (including action code)
	 * DW1: fence
	 * DW2+: action data
	 */
	header = (len << GUC_CT_MSG_LEN_SHIFT) |
		 (GUC_CT_MSG_WRITE_FENCE_TO_DESC) |
		 (action[0] << GUC_CT_MSG_ACTION_SHIFT);

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

/* Wait for the response from the GuC.
 * @fence:	response fence
 * @status:	placeholder for status
 * return:	0 response received (status is valid)
 *		-ETIMEDOUT no response within hardcoded timeout
 *		-EPROTO no response, ct buffer was in error
 */
static int wait_for_response(struct guc_ct_buffer_desc *desc,
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

static int ctch_send(struct intel_guc *guc,
		     struct intel_guc_ct_channel *ctch,
		     const u32 *action,
		     u32 len,
		     u32 *status)
{
	struct intel_guc_ct_buffer *ctb = &ctch->ctbs[CTB_SEND];
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 fence;
	int err;

	GEM_BUG_ON(!ctch_is_open(ctch));
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len & ~GUC_CT_MSG_LEN_MASK);

	fence = ctch_get_next_fence(ctch);
	err = ctb_write(ctb, action, len, fence);
	if (unlikely(err))
		return err;

	intel_guc_notify(guc);

	err = wait_for_response(desc, fence, status);
	if (unlikely(err))
		return err;
	if (!INTEL_GUC_MSG_IS_RESPONSE_SUCCESS(*status))
		return -EIO;

	/* Use data from the GuC status as our return value */
	return INTEL_GUC_MSG_TO_DATA(*status);
}

/*
 * Command Transport (CT) buffer based GuC send function.
 */
static int intel_guc_send_ct(struct intel_guc *guc, const u32 *action, u32 len)
{
	struct intel_guc_ct_channel *ctch = &guc->ct.host_channel;
	u32 status = ~0; /* undefined */
	int ret;

	mutex_lock(&guc->send_mutex);

	ret = ctch_send(guc, ctch, action, len, &status);
	if (unlikely(ret < 0)) {
		DRM_ERROR("CT: send action %#X failed; err=%d status=%#X\n",
			  action[0], ret, status);
	}

	mutex_unlock(&guc->send_mutex);
	return ret;
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
	DRM_INFO("CT: %s\n", enableddisabled(false));
}
