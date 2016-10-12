/*
 * Copyright © 2014 Intel Corporation
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
 *
 */
#include <linux/firmware.h>
#include <linux/circ_buf.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include "i915_drv.h"
#include "intel_guc.h"

/**
 * DOC: GuC-based command submission
 *
 * i915_guc_client:
 * We use the term client to avoid confusion with contexts. A i915_guc_client is
 * equivalent to GuC object guc_context_desc. This context descriptor is
 * allocated from a pool of 1024 entries. Kernel driver will allocate doorbell
 * and workqueue for it. Also the process descriptor (guc_process_desc), which
 * is mapped to client space. So the client can write Work Item then ring the
 * doorbell.
 *
 * To simplify the implementation, we allocate one gem object that contains all
 * pages for doorbell, process descriptor and workqueue.
 *
 * The Scratch registers:
 * There are 16 MMIO-based registers start from 0xC180. The kernel driver writes
 * a value to the action register (SOFT_SCRATCH_0) along with any data. It then
 * triggers an interrupt on the GuC via another register write (0xC4C8).
 * Firmware writes a success/fail code back to the action register after
 * processes the request. The kernel driver polls waiting for this update and
 * then proceeds.
 * See host2guc_action()
 *
 * Doorbells:
 * Doorbells are interrupts to uKernel. A doorbell is a single cache line (QW)
 * mapped into process space.
 *
 * Work Items:
 * There are several types of work items that the host may place into a
 * workqueue, each with its own requirements and limitations. Currently only
 * WQ_TYPE_INORDER is needed to support legacy submission via GuC, which
 * represents in-order queue. The kernel driver packs ring tail pointer and an
 * ELSP context descriptor dword into Work Item.
 * See guc_wq_item_append()
 *
 */

/*
 * Read GuC command/status register (SOFT_SCRATCH_0)
 * Return true if it contains a response rather than a command
 */
static inline bool host2guc_action_response(struct drm_i915_private *dev_priv,
					    u32 *status)
{
	u32 val = I915_READ(SOFT_SCRATCH(0));
	*status = val;
	return GUC2HOST_IS_RESPONSE(val);
}

static int host2guc_action(struct intel_guc *guc, u32 *data, u32 len)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 status;
	int i;
	int ret;

	if (WARN_ON(len < 1 || len > 15))
		return -EINVAL;

	mutex_lock(&guc->action_lock);
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	dev_priv->guc.action_count += 1;
	dev_priv->guc.action_cmd = data[0];

	for (i = 0; i < len; i++)
		I915_WRITE(SOFT_SCRATCH(i), data[i]);

	POSTING_READ(SOFT_SCRATCH(i - 1));

	I915_WRITE(HOST2GUC_INTERRUPT, HOST2GUC_TRIGGER);

	/*
	 * Fast commands should complete in less than 10us, so sample quickly
	 * up to that length of time, then switch to a slower sleep-wait loop.
	 * No HOST2GUC command should ever take longer than 10ms.
	 */
	ret = wait_for_us(host2guc_action_response(dev_priv, &status), 10);
	if (ret)
		ret = wait_for(host2guc_action_response(dev_priv, &status), 10);
	if (status != GUC2HOST_STATUS_SUCCESS) {
		/*
		 * Either the GuC explicitly returned an error (which
		 * we convert to -EIO here) or no response at all was
		 * received within the timeout limit (-ETIMEDOUT)
		 */
		if (ret != -ETIMEDOUT)
			ret = -EIO;

		DRM_WARN("Action 0x%X failed; ret=%d status=0x%08X response=0x%08X\n",
			 data[0], ret, status, I915_READ(SOFT_SCRATCH(15)));

		dev_priv->guc.action_fail += 1;
		dev_priv->guc.action_err = ret;
	}
	dev_priv->guc.action_status = status;

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	mutex_unlock(&guc->action_lock);

	return ret;
}

/*
 * Tell the GuC to allocate or deallocate a specific doorbell
 */

static int host2guc_allocate_doorbell(struct intel_guc *guc,
				      struct i915_guc_client *client)
{
	u32 data[2];

	data[0] = HOST2GUC_ACTION_ALLOCATE_DOORBELL;
	data[1] = client->ctx_index;

	return host2guc_action(guc, data, 2);
}

static int host2guc_release_doorbell(struct intel_guc *guc,
				     struct i915_guc_client *client)
{
	u32 data[2];

	data[0] = HOST2GUC_ACTION_DEALLOCATE_DOORBELL;
	data[1] = client->ctx_index;

	return host2guc_action(guc, data, 2);
}

static int host2guc_sample_forcewake(struct intel_guc *guc,
				     struct i915_guc_client *client)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 data[2];

	data[0] = HOST2GUC_ACTION_SAMPLE_FORCEWAKE;
	/* WaRsDisableCoarsePowerGating:skl,bxt */
	if (!intel_enable_rc6() || NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
		data[1] = 0;
	else
		/* bit 0 and 1 are for Render and Media domain separately */
		data[1] = GUC_FORCEWAKE_RENDER | GUC_FORCEWAKE_MEDIA;

	return host2guc_action(guc, data, ARRAY_SIZE(data));
}

static int host2guc_logbuffer_flush_complete(struct intel_guc *guc)
{
	u32 data[1];

	data[0] = HOST2GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE;

	return host2guc_action(guc, data, 1);
}

/*
 * Initialise, update, or clear doorbell data shared with the GuC
 *
 * These functions modify shared data and so need access to the mapped
 * client object which contains the page being used for the doorbell
 */

static int guc_update_doorbell_id(struct intel_guc *guc,
				  struct i915_guc_client *client,
				  u16 new_id)
{
	struct sg_table *sg = guc->ctx_pool_vma->pages;
	void *doorbell_bitmap = guc->doorbell_bitmap;
	struct guc_doorbell_info *doorbell;
	struct guc_context_desc desc;
	size_t len;

	doorbell = client->client_base + client->doorbell_offset;

	if (client->doorbell_id != GUC_INVALID_DOORBELL_ID &&
	    test_bit(client->doorbell_id, doorbell_bitmap)) {
		/* Deactivate the old doorbell */
		doorbell->db_status = GUC_DOORBELL_DISABLED;
		(void)host2guc_release_doorbell(guc, client);
		__clear_bit(client->doorbell_id, doorbell_bitmap);
	}

	/* Update the GuC's idea of the doorbell ID */
	len = sg_pcopy_to_buffer(sg->sgl, sg->nents, &desc, sizeof(desc),
			     sizeof(desc) * client->ctx_index);
	if (len != sizeof(desc))
		return -EFAULT;
	desc.db_id = new_id;
	len = sg_pcopy_from_buffer(sg->sgl, sg->nents, &desc, sizeof(desc),
			     sizeof(desc) * client->ctx_index);
	if (len != sizeof(desc))
		return -EFAULT;

	client->doorbell_id = new_id;
	if (new_id == GUC_INVALID_DOORBELL_ID)
		return 0;

	/* Activate the new doorbell */
	__set_bit(new_id, doorbell_bitmap);
	doorbell->cookie = 0;
	doorbell->db_status = GUC_DOORBELL_ENABLED;
	return host2guc_allocate_doorbell(guc, client);
}

static int guc_init_doorbell(struct intel_guc *guc,
			      struct i915_guc_client *client,
			      uint16_t db_id)
{
	return guc_update_doorbell_id(guc, client, db_id);
}

static void guc_disable_doorbell(struct intel_guc *guc,
				 struct i915_guc_client *client)
{
	(void)guc_update_doorbell_id(guc, client, GUC_INVALID_DOORBELL_ID);

	/* XXX: wait for any interrupts */
	/* XXX: wait for workqueue to drain */
}

static uint16_t
select_doorbell_register(struct intel_guc *guc, uint32_t priority)
{
	/*
	 * The bitmap tracks which doorbell registers are currently in use.
	 * It is split into two halves; the first half is used for normal
	 * priority contexts, the second half for high-priority ones.
	 * Note that logically higher priorities are numerically less than
	 * normal ones, so the test below means "is it high-priority?"
	 */
	const bool hi_pri = (priority <= GUC_CTX_PRIORITY_HIGH);
	const uint16_t half = GUC_MAX_DOORBELLS / 2;
	const uint16_t start = hi_pri ? half : 0;
	const uint16_t end = start + half;
	uint16_t id;

	id = find_next_zero_bit(guc->doorbell_bitmap, end, start);
	if (id == end)
		id = GUC_INVALID_DOORBELL_ID;

	DRM_DEBUG_DRIVER("assigned %s priority doorbell id 0x%x\n",
			hi_pri ? "high" : "normal", id);

	return id;
}

/*
 * Select, assign and relase doorbell cachelines
 *
 * These functions track which doorbell cachelines are in use.
 * The data they manipulate is protected by the host2guc lock.
 */

static uint32_t select_doorbell_cacheline(struct intel_guc *guc)
{
	const uint32_t cacheline_size = cache_line_size();
	uint32_t offset;

	/* Doorbell uses a single cache line within a page */
	offset = offset_in_page(guc->db_cacheline);

	/* Moving to next cache line to reduce contention */
	guc->db_cacheline += cacheline_size;

	DRM_DEBUG_DRIVER("selected doorbell cacheline 0x%x, next 0x%x, linesize %u\n",
			offset, guc->db_cacheline, cacheline_size);

	return offset;
}

/*
 * Initialise the process descriptor shared with the GuC firmware.
 */
static void guc_proc_desc_init(struct intel_guc *guc,
			       struct i915_guc_client *client)
{
	struct guc_process_desc *desc;

	desc = client->client_base + client->proc_desc_offset;

	memset(desc, 0, sizeof(*desc));

	/*
	 * XXX: pDoorbell and WQVBaseAddress are pointers in process address
	 * space for ring3 clients (set them as in mmap_ioctl) or kernel
	 * space for kernel clients (map on demand instead? May make debug
	 * easier to have it mapped).
	 */
	desc->wq_base_addr = 0;
	desc->db_base_addr = 0;

	desc->context_id = client->ctx_index;
	desc->wq_size_bytes = client->wq_size;
	desc->wq_status = WQ_STATUS_ACTIVE;
	desc->priority = client->priority;
}

/*
 * Initialise/clear the context descriptor shared with the GuC firmware.
 *
 * This descriptor tells the GuC where (in GGTT space) to find the important
 * data structures relating to this client (doorbell, process descriptor,
 * write queue, etc).
 */

static void guc_ctx_desc_init(struct intel_guc *guc,
			      struct i915_guc_client *client)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx = client->owner;
	struct guc_context_desc desc;
	struct sg_table *sg;
	unsigned int tmp;
	u32 gfx_addr;

	memset(&desc, 0, sizeof(desc));

	desc.attribute = GUC_CTX_DESC_ATTR_ACTIVE | GUC_CTX_DESC_ATTR_KERNEL;
	desc.context_id = client->ctx_index;
	desc.priority = client->priority;
	desc.db_id = client->doorbell_id;

	for_each_engine_masked(engine, dev_priv, client->engines, tmp) {
		struct intel_context *ce = &ctx->engine[engine->id];
		uint32_t guc_engine_id = engine->guc_id;
		struct guc_execlist_context *lrc = &desc.lrc[guc_engine_id];

		/* TODO: We have a design issue to be solved here. Only when we
		 * receive the first batch, we know which engine is used by the
		 * user. But here GuC expects the lrc and ring to be pinned. It
		 * is not an issue for default context, which is the only one
		 * for now who owns a GuC client. But for future owner of GuC
		 * client, need to make sure lrc is pinned prior to enter here.
		 */
		if (!ce->state)
			break;	/* XXX: continue? */

		lrc->context_desc = lower_32_bits(ce->lrc_desc);

		/* The state page is after PPHWSP */
		lrc->ring_lcra =
			i915_ggtt_offset(ce->state) + LRC_STATE_PN * PAGE_SIZE;
		lrc->context_id = (client->ctx_index << GUC_ELC_CTXID_OFFSET) |
				(guc_engine_id << GUC_ELC_ENGINE_OFFSET);

		lrc->ring_begin = i915_ggtt_offset(ce->ring->vma);
		lrc->ring_end = lrc->ring_begin + ce->ring->size - 1;
		lrc->ring_next_free_location = lrc->ring_begin;
		lrc->ring_current_tail_pointer_value = 0;

		desc.engines_used |= (1 << guc_engine_id);
	}

	DRM_DEBUG_DRIVER("Host engines 0x%x => GuC engines used 0x%x\n",
			client->engines, desc.engines_used);
	WARN_ON(desc.engines_used == 0);

	/*
	 * The doorbell, process descriptor, and workqueue are all parts
	 * of the client object, which the GuC will reference via the GGTT
	 */
	gfx_addr = i915_ggtt_offset(client->vma);
	desc.db_trigger_phy = sg_dma_address(client->vma->pages->sgl) +
				client->doorbell_offset;
	desc.db_trigger_cpu = (uintptr_t)client->client_base +
				client->doorbell_offset;
	desc.db_trigger_uk = gfx_addr + client->doorbell_offset;
	desc.process_desc = gfx_addr + client->proc_desc_offset;
	desc.wq_addr = gfx_addr + client->wq_offset;
	desc.wq_size = client->wq_size;

	/*
	 * XXX: Take LRCs from an existing context if this is not an
	 * IsKMDCreatedContext client
	 */
	desc.desc_private = (uintptr_t)client;

	/* Pool context is pinned already */
	sg = guc->ctx_pool_vma->pages;
	sg_pcopy_from_buffer(sg->sgl, sg->nents, &desc, sizeof(desc),
			     sizeof(desc) * client->ctx_index);
}

static void guc_ctx_desc_fini(struct intel_guc *guc,
			      struct i915_guc_client *client)
{
	struct guc_context_desc desc;
	struct sg_table *sg;

	memset(&desc, 0, sizeof(desc));

	sg = guc->ctx_pool_vma->pages;
	sg_pcopy_from_buffer(sg->sgl, sg->nents, &desc, sizeof(desc),
			     sizeof(desc) * client->ctx_index);
}

/**
 * i915_guc_wq_reserve() - reserve space in the GuC's workqueue
 * @request:	request associated with the commands
 *
 * Return:	0 if space is available
 *		-EAGAIN if space is not currently available
 *
 * This function must be called (and must return 0) before a request
 * is submitted to the GuC via i915_guc_submit() below. Once a result
 * of 0 has been returned, it must be balanced by a corresponding
 * call to submit().
 *
 * Reservation allows the caller to determine in advance that space
 * will be available for the next submission before committing resources
 * to it, and helps avoid late failures with complicated recovery paths.
 */
int i915_guc_wq_reserve(struct drm_i915_gem_request *request)
{
	const size_t wqi_size = sizeof(struct guc_wq_item);
	struct i915_guc_client *gc = request->i915->guc.execbuf_client;
	struct guc_process_desc *desc = gc->client_base + gc->proc_desc_offset;
	u32 freespace;
	int ret;

	spin_lock(&gc->wq_lock);
	freespace = CIRC_SPACE(gc->wq_tail, desc->head, gc->wq_size);
	freespace -= gc->wq_rsvd;
	if (likely(freespace >= wqi_size)) {
		gc->wq_rsvd += wqi_size;
		ret = 0;
	} else {
		gc->no_wq_space++;
		ret = -EAGAIN;
	}
	spin_unlock(&gc->wq_lock);

	return ret;
}

void i915_guc_wq_unreserve(struct drm_i915_gem_request *request)
{
	const size_t wqi_size = sizeof(struct guc_wq_item);
	struct i915_guc_client *gc = request->i915->guc.execbuf_client;

	GEM_BUG_ON(READ_ONCE(gc->wq_rsvd) < wqi_size);

	spin_lock(&gc->wq_lock);
	gc->wq_rsvd -= wqi_size;
	spin_unlock(&gc->wq_lock);
}

/* Construct a Work Item and append it to the GuC's Work Queue */
static void guc_wq_item_append(struct i915_guc_client *gc,
			       struct drm_i915_gem_request *rq)
{
	/* wqi_len is in DWords, and does not include the one-word header */
	const size_t wqi_size = sizeof(struct guc_wq_item);
	const u32 wqi_len = wqi_size/sizeof(u32) - 1;
	struct intel_engine_cs *engine = rq->engine;
	struct guc_process_desc *desc;
	struct guc_wq_item *wqi;
	void *base;
	u32 freespace, tail, wq_off, wq_page;

	desc = gc->client_base + gc->proc_desc_offset;

	/* Free space is guaranteed, see i915_guc_wq_reserve() above */
	freespace = CIRC_SPACE(gc->wq_tail, desc->head, gc->wq_size);
	GEM_BUG_ON(freespace < wqi_size);

	/* The GuC firmware wants the tail index in QWords, not bytes */
	tail = rq->tail;
	GEM_BUG_ON(tail & 7);
	tail >>= 3;
	GEM_BUG_ON(tail > WQ_RING_TAIL_MAX);

	/* For now workqueue item is 4 DWs; workqueue buffer is 2 pages. So we
	 * should not have the case where structure wqi is across page, neither
	 * wrapped to the beginning. This simplifies the implementation below.
	 *
	 * XXX: if not the case, we need save data to a temp wqi and copy it to
	 * workqueue buffer dw by dw.
	 */
	BUILD_BUG_ON(wqi_size != 16);
	GEM_BUG_ON(gc->wq_rsvd < wqi_size);

	/* postincrement WQ tail for next time */
	wq_off = gc->wq_tail;
	GEM_BUG_ON(wq_off & (wqi_size - 1));
	gc->wq_tail += wqi_size;
	gc->wq_tail &= gc->wq_size - 1;
	gc->wq_rsvd -= wqi_size;

	/* WQ starts from the page after doorbell / process_desc */
	wq_page = (wq_off + GUC_DB_SIZE) >> PAGE_SHIFT;
	wq_off &= PAGE_SIZE - 1;
	base = kmap_atomic(i915_gem_object_get_page(gc->vma->obj, wq_page));
	wqi = (struct guc_wq_item *)((char *)base + wq_off);

	/* Now fill in the 4-word work queue item */
	wqi->header = WQ_TYPE_INORDER |
			(wqi_len << WQ_LEN_SHIFT) |
			(engine->guc_id << WQ_TARGET_SHIFT) |
			WQ_NO_WCFLUSH_WAIT;

	/* The GuC wants only the low-order word of the context descriptor */
	wqi->context_desc = (u32)intel_lr_context_descriptor(rq->ctx, engine);

	wqi->ring_tail = tail << WQ_RING_TAIL_SHIFT;
	wqi->fence_id = rq->fence.seqno;

	kunmap_atomic(base);
}

static int guc_ring_doorbell(struct i915_guc_client *gc)
{
	struct guc_process_desc *desc;
	union guc_doorbell_qw db_cmp, db_exc, db_ret;
	union guc_doorbell_qw *db;
	int attempt = 2, ret = -EAGAIN;

	desc = gc->client_base + gc->proc_desc_offset;

	/* Update the tail so it is visible to GuC */
	desc->tail = gc->wq_tail;

	/* current cookie */
	db_cmp.db_status = GUC_DOORBELL_ENABLED;
	db_cmp.cookie = gc->cookie;

	/* cookie to be updated */
	db_exc.db_status = GUC_DOORBELL_ENABLED;
	db_exc.cookie = gc->cookie + 1;
	if (db_exc.cookie == 0)
		db_exc.cookie = 1;

	/* pointer of current doorbell cacheline */
	db = gc->client_base + gc->doorbell_offset;

	while (attempt--) {
		/* lets ring the doorbell */
		db_ret.value_qw = atomic64_cmpxchg((atomic64_t *)db,
			db_cmp.value_qw, db_exc.value_qw);

		/* if the exchange was successfully executed */
		if (db_ret.value_qw == db_cmp.value_qw) {
			/* db was successfully rung */
			gc->cookie = db_exc.cookie;
			ret = 0;
			break;
		}

		/* XXX: doorbell was lost and need to acquire it again */
		if (db_ret.db_status == GUC_DOORBELL_DISABLED)
			break;

		DRM_WARN("Cookie mismatch. Expected %d, found %d\n",
			 db_cmp.cookie, db_ret.cookie);

		/* update the cookie to newly read cookie from GuC */
		db_cmp.cookie = db_ret.cookie;
		db_exc.cookie = db_ret.cookie + 1;
		if (db_exc.cookie == 0)
			db_exc.cookie = 1;
	}

	return ret;
}

/**
 * i915_guc_submit() - Submit commands through GuC
 * @rq:		request associated with the commands
 *
 * Return:	0 on success, otherwise an errno.
 * 		(Note: nonzero really shouldn't happen!)
 *
 * The caller must have already called i915_guc_wq_reserve() above with
 * a result of 0 (success), guaranteeing that there is space in the work
 * queue for the new request, so enqueuing the item cannot fail.
 *
 * Bad Things Will Happen if the caller violates this protocol e.g. calls
 * submit() when _reserve() says there's no space, or calls _submit()
 * a different number of times from (successful) calls to _reserve().
 *
 * The only error here arises if the doorbell hardware isn't functioning
 * as expected, which really shouln't happen.
 */
static void i915_guc_submit(struct drm_i915_gem_request *rq)
{
	unsigned int engine_id = rq->engine->id;
	struct intel_guc *guc = &rq->i915->guc;
	struct i915_guc_client *client = guc->execbuf_client;
	int b_ret;

	spin_lock(&client->wq_lock);
	guc_wq_item_append(client, rq);
	b_ret = guc_ring_doorbell(client);

	client->submissions[engine_id] += 1;
	client->retcode = b_ret;
	if (b_ret)
		client->b_fail += 1;

	guc->submissions[engine_id] += 1;
	guc->last_seqno[engine_id] = rq->fence.seqno;
	spin_unlock(&client->wq_lock);
}

/*
 * Everything below here is concerned with setup & teardown, and is
 * therefore not part of the somewhat time-critical batch-submission
 * path of i915_guc_submit() above.
 */

/**
 * guc_allocate_vma() - Allocate a GGTT VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 *
 * This is a wrapper to create an object for use with the GuC. In order to
 * use it inside the GuC, an object needs to be pinned lifetime, so we allocate
 * both some backing storage and a range inside the Global GTT. We must pin
 * it in the GGTT somewhere other than than [0, GUC_WOPCM_TOP) because that
 * range is reserved inside GuC.
 *
 * Return:	A i915_vma if successful, otherwise an ERR_PTR.
 */
static struct i915_vma *guc_allocate_vma(struct intel_guc *guc, u32 size)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int ret;

	obj = i915_gem_object_create(&dev_priv->drm, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_create(obj, &dev_priv->ggtt.base, NULL);
	if (IS_ERR(vma))
		goto err;

	ret = i915_vma_pin(vma, 0, PAGE_SIZE,
			   PIN_GLOBAL | PIN_OFFSET_BIAS | GUC_WOPCM_TOP);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	/* Invalidate GuC TLB to let GuC take the latest updates to GTT. */
	I915_WRITE(GEN8_GTCR, GEN8_GTCR_INVALIDATE);

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

static void
guc_client_free(struct drm_i915_private *dev_priv,
		struct i915_guc_client *client)
{
	struct intel_guc *guc = &dev_priv->guc;

	if (!client)
		return;

	/*
	 * XXX: wait for any outstanding submissions before freeing memory.
	 * Be sure to drop any locks
	 */

	if (client->client_base) {
		/*
		 * If we got as far as setting up a doorbell, make sure we
		 * shut it down before unmapping & deallocating the memory.
		 */
		guc_disable_doorbell(guc, client);

		kunmap(kmap_to_page(client->client_base));
	}

	i915_vma_unpin_and_release(&client->vma);

	if (client->ctx_index != GUC_INVALID_CTX_ID) {
		guc_ctx_desc_fini(guc, client);
		ida_simple_remove(&guc->ctx_ids, client->ctx_index);
	}

	kfree(client);
}

/* Check that a doorbell register is in the expected state */
static bool guc_doorbell_check(struct intel_guc *guc, uint16_t db_id)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	i915_reg_t drbreg = GEN8_DRBREGL(db_id);
	uint32_t value = I915_READ(drbreg);
	bool enabled = (value & GUC_DOORBELL_ENABLED) != 0;
	bool expected = test_bit(db_id, guc->doorbell_bitmap);

	if (enabled == expected)
		return true;

	DRM_DEBUG_DRIVER("Doorbell %d (reg 0x%x) 0x%x, should be %s\n",
			 db_id, drbreg.reg, value,
			 expected ? "active" : "inactive");

	return false;
}

/*
 * Borrow the first client to set up & tear down each unused doorbell
 * in turn, to ensure that all doorbell h/w is (re)initialised.
 */
static void guc_init_doorbell_hw(struct intel_guc *guc)
{
	struct i915_guc_client *client = guc->execbuf_client;
	uint16_t db_id;
	int i, err;

	/* Save client's original doorbell selection */
	db_id = client->doorbell_id;

	for (i = 0; i < GUC_MAX_DOORBELLS; ++i) {
		/* Skip if doorbell is OK */
		if (guc_doorbell_check(guc, i))
			continue;

		err = guc_update_doorbell_id(guc, client, i);
		if (err)
			DRM_DEBUG_DRIVER("Doorbell %d update failed, err %d\n",
					i, err);
	}

	/* Restore to original value */
	err = guc_update_doorbell_id(guc, client, db_id);
	if (err)
		DRM_WARN("Failed to restore doorbell to %d, err %d\n",
			 db_id, err);

	/* Read back & verify all doorbell registers */
	for (i = 0; i < GUC_MAX_DOORBELLS; ++i)
		(void)guc_doorbell_check(guc, i);
}

/**
 * guc_client_alloc() - Allocate an i915_guc_client
 * @dev_priv:	driver private data structure
 * @engines:	The set of engines to enable for this client
 * @priority:	four levels priority _CRITICAL, _HIGH, _NORMAL and _LOW
 * 		The kernel client to replace ExecList submission is created with
 * 		NORMAL priority. Priority of a client for scheduler can be HIGH,
 * 		while a preemption context can use CRITICAL.
 * @ctx:	the context that owns the client (we use the default render
 * 		context)
 *
 * Return:	An i915_guc_client object if success, else NULL.
 */
static struct i915_guc_client *
guc_client_alloc(struct drm_i915_private *dev_priv,
		 uint32_t engines,
		 uint32_t priority,
		 struct i915_gem_context *ctx)
{
	struct i915_guc_client *client;
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_vma *vma;
	uint16_t db_id;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	client->owner = ctx;
	client->guc = guc;
	client->engines = engines;
	client->priority = priority;
	client->doorbell_id = GUC_INVALID_DOORBELL_ID;

	client->ctx_index = (uint32_t)ida_simple_get(&guc->ctx_ids, 0,
			GUC_MAX_GPU_CONTEXTS, GFP_KERNEL);
	if (client->ctx_index >= GUC_MAX_GPU_CONTEXTS) {
		client->ctx_index = GUC_INVALID_CTX_ID;
		goto err;
	}

	/* The first page is doorbell/proc_desc. Two followed pages are wq. */
	vma = guc_allocate_vma(guc, GUC_DB_SIZE + GUC_WQ_SIZE);
	if (IS_ERR(vma))
		goto err;

	/* We'll keep just the first (doorbell/proc) page permanently kmap'd. */
	client->vma = vma;
	client->client_base = kmap(i915_vma_first_page(vma));

	spin_lock_init(&client->wq_lock);
	client->wq_offset = GUC_DB_SIZE;
	client->wq_size = GUC_WQ_SIZE;

	db_id = select_doorbell_register(guc, client->priority);
	if (db_id == GUC_INVALID_DOORBELL_ID)
		/* XXX: evict a doorbell instead? */
		goto err;

	client->doorbell_offset = select_doorbell_cacheline(guc);

	/*
	 * Since the doorbell only requires a single cacheline, we can save
	 * space by putting the application process descriptor in the same
	 * page. Use the half of the page that doesn't include the doorbell.
	 */
	if (client->doorbell_offset >= (GUC_DB_SIZE / 2))
		client->proc_desc_offset = 0;
	else
		client->proc_desc_offset = (GUC_DB_SIZE / 2);

	guc_proc_desc_init(guc, client);
	guc_ctx_desc_init(guc, client);
	if (guc_init_doorbell(guc, client, db_id))
		goto err;

	DRM_DEBUG_DRIVER("new priority %u client %p for engine(s) 0x%x: ctx_index %u\n",
		priority, client, client->engines, client->ctx_index);
	DRM_DEBUG_DRIVER("doorbell id %u, cacheline offset 0x%x\n",
		client->doorbell_id, client->doorbell_offset);

	return client;

err:
	guc_client_free(dev_priv, client);
	return NULL;
}

/*
 * Sub buffer switch callback. Called whenever relay has to switch to a new
 * sub buffer, relay stays on the same sub buffer if 0 is returned.
 */
static int subbuf_start_callback(struct rchan_buf *buf,
				 void *subbuf,
				 void *prev_subbuf,
				 size_t prev_padding)
{
	/* Use no-overwrite mode by default, where relay will stop accepting
	 * new data if there are no empty sub buffers left.
	 * There is no strict synchronization enforced by relay between Consumer
	 * and Producer. In overwrite mode, there is a possibility of getting
	 * inconsistent/garbled data, the producer could be writing on to the
	 * same sub buffer from which Consumer is reading. This can't be avoided
	 * unless Consumer is fast enough and can always run in tandem with
	 * Producer.
	 */
	if (relay_buf_full(buf))
		return 0;

	return 1;
}

/*
 * file_create() callback. Creates relay file in debugfs.
 */
static struct dentry *create_buf_file_callback(const char *filename,
					       struct dentry *parent,
					       umode_t mode,
					       struct rchan_buf *buf,
					       int *is_global)
{
	struct dentry *buf_file;

	if (!parent)
		return NULL;

	/* This to enable the use of a single buffer for the relay channel and
	 * correspondingly have a single file exposed to User, through which
	 * it can collect the logs in order without any post-processing.
	 */
	*is_global = 1;

	/* Not using the channel filename passed as an argument, since for each
	 * channel relay appends the corresponding CPU number to the filename
	 * passed in relay_open(). This should be fine as relay just needs a
	 * dentry of the file associated with the channel buffer and that file's
	 * name need not be same as the filename passed as an argument.
	 */
	buf_file = debugfs_create_file("guc_log", mode,
				       parent, buf, &relay_file_operations);
	return buf_file;
}

/*
 * file_remove() default callback. Removes relay file in debugfs.
 */
static int remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

/* relay channel callbacks */
static struct rchan_callbacks relay_callbacks = {
	.subbuf_start = subbuf_start_callback,
	.create_buf_file = create_buf_file_callback,
	.remove_buf_file = remove_buf_file_callback,
};

static void guc_log_remove_relay_file(struct intel_guc *guc)
{
	relay_close(guc->log.relay_chan);
}

static int guc_log_create_relay_file(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct rchan *guc_log_relay_chan;
	struct dentry *log_dir;
	size_t n_subbufs, subbuf_size;

	/* For now create the log file in /sys/kernel/debug/dri/0 dir */
	log_dir = dev_priv->drm.primary->debugfs_root;

	/* If /sys/kernel/debug/dri/0 location do not exist, then debugfs is
	 * not mounted and so can't create the relay file.
	 * The relay API seems to fit well with debugfs only, for availing relay
	 * there are 3 requirements which can be met for debugfs file only in a
	 * straightforward/clean manner :-
	 * i)   Need the associated dentry pointer of the file, while opening the
	 *      relay channel.
	 * ii)  Should be able to use 'relay_file_operations' fops for the file.
	 * iii) Set the 'i_private' field of file's inode to the pointer of
	 *	relay channel buffer.
	 */
	if (!log_dir) {
		DRM_ERROR("Debugfs dir not available yet for GuC log file\n");
		return -ENODEV;
	}

	/* Keep the size of sub buffers same as shared log buffer */
	subbuf_size = guc->log.vma->obj->base.size;

	/* Store up to 8 snapshots, which is large enough to buffer sufficient
	 * boot time logs and provides enough leeway to User, in terms of
	 * latency, for consuming the logs from relay. Also doesn't take
	 * up too much memory.
	 */
	n_subbufs = 8;

	guc_log_relay_chan = relay_open("guc_log", log_dir, subbuf_size,
					n_subbufs, &relay_callbacks, dev_priv);
	if (!guc_log_relay_chan) {
		DRM_ERROR("Couldn't create relay chan for GuC logging\n");
		return -ENOMEM;
	}

	GEM_BUG_ON(guc_log_relay_chan->subbuf_size < subbuf_size);
	/* FIXME: Cover the update under a lock ? */
	guc->log.relay_chan = guc_log_relay_chan;
	return 0;
}

static void guc_move_to_next_buf(struct intel_guc *guc)
{
	/* Make sure the updates made in the sub buffer are visible when
	 * Consumer sees the following update to offset inside the sub buffer.
	 */
	smp_wmb();

	/* All data has been written, so now move the offset of sub buffer. */
	relay_reserve(guc->log.relay_chan, guc->log.vma->obj->base.size);

	/* Switch to the next sub buffer */
	relay_flush(guc->log.relay_chan);
}

static void *guc_get_write_buffer(struct intel_guc *guc)
{
	/* FIXME: Cover the check under a lock ? */
	if (!guc->log.relay_chan)
		return NULL;

	/* Just get the base address of a new sub buffer and copy data into it
	 * ourselves. NULL will be returned in no-overwrite mode, if all sub
	 * buffers are full. Could have used the relay_write() to indirectly
	 * copy the data, but that would have been bit convoluted, as we need to
	 * write to only certain locations inside a sub buffer which cannot be
	 * done without using relay_reserve() along with relay_write(). So its
	 * better to use relay_reserve() alone.
	 */
	return relay_reserve(guc->log.relay_chan, 0);
}

static bool
guc_check_log_buf_overflow(struct intel_guc *guc,
			   enum guc_log_buffer_type type, unsigned int full_cnt)
{
	unsigned int prev_full_cnt = guc->log.prev_overflow_count[type];
	bool overflow = false;

	if (full_cnt != prev_full_cnt) {
		overflow = true;

		guc->log.prev_overflow_count[type] = full_cnt;
		guc->log.total_overflow_count[type] += full_cnt - prev_full_cnt;

		if (full_cnt < prev_full_cnt) {
			/* buffer_full_cnt is a 4 bit counter */
			guc->log.total_overflow_count[type] += 16;
		}
		DRM_ERROR_RATELIMITED("GuC log buffer overflow\n");
	}

	return overflow;
}

static unsigned int guc_get_log_buffer_size(enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_ISR_LOG_BUFFER:
		return (GUC_LOG_ISR_PAGES + 1) * PAGE_SIZE;
	case GUC_DPC_LOG_BUFFER:
		return (GUC_LOG_DPC_PAGES + 1) * PAGE_SIZE;
	case GUC_CRASH_DUMP_LOG_BUFFER:
		return (GUC_LOG_CRASH_PAGES + 1) * PAGE_SIZE;
	default:
		MISSING_CASE(type);
	}

	return 0;
}

static void guc_read_update_log_buffer(struct intel_guc *guc)
{
	unsigned int buffer_size, read_offset, write_offset, bytes_to_copy, full_cnt;
	struct guc_log_buffer_state *log_buf_state, *log_buf_snapshot_state;
	struct guc_log_buffer_state log_buf_state_local;
	enum guc_log_buffer_type type;
	void *src_data, *dst_data;
	bool new_overflow;

	if (WARN_ON(!guc->log.buf_addr))
		return;

	/* Get the pointer to shared GuC log buffer */
	log_buf_state = src_data = guc->log.buf_addr;

	/* Get the pointer to local buffer to store the logs */
	log_buf_snapshot_state = dst_data = guc_get_write_buffer(guc);

	/* Actual logs are present from the 2nd page */
	src_data += PAGE_SIZE;
	dst_data += PAGE_SIZE;

	for (type = GUC_ISR_LOG_BUFFER; type < GUC_MAX_LOG_BUFFER; type++) {
		/* Make a copy of the state structure, inside GuC log buffer
		 * (which is uncached mapped), on the stack to avoid reading
		 * from it multiple times.
		 */
		memcpy(&log_buf_state_local, log_buf_state,
		       sizeof(struct guc_log_buffer_state));
		buffer_size = guc_get_log_buffer_size(type);
		read_offset = log_buf_state_local.read_ptr;
		write_offset = log_buf_state_local.sampled_write_ptr;
		full_cnt = log_buf_state_local.buffer_full_cnt;

		/* Bookkeeping stuff */
		guc->log.flush_count[type] += log_buf_state_local.flush_to_file;
		new_overflow = guc_check_log_buf_overflow(guc, type, full_cnt);

		/* Update the state of shared log buffer */
		log_buf_state->read_ptr = write_offset;
		log_buf_state->flush_to_file = 0;
		log_buf_state++;

		if (unlikely(!log_buf_snapshot_state))
			continue;

		/* First copy the state structure in snapshot buffer */
		memcpy(log_buf_snapshot_state, &log_buf_state_local,
		       sizeof(struct guc_log_buffer_state));

		/* The write pointer could have been updated by GuC firmware,
		 * after sending the flush interrupt to Host, for consistency
		 * set write pointer value to same value of sampled_write_ptr
		 * in the snapshot buffer.
		 */
		log_buf_snapshot_state->write_ptr = write_offset;
		log_buf_snapshot_state++;

		/* Now copy the actual logs. */
		if (unlikely(new_overflow)) {
			/* copy the whole buffer in case of overflow */
			read_offset = 0;
			write_offset = buffer_size;
		} else if (unlikely((read_offset > buffer_size) ||
				    (write_offset > buffer_size))) {
			DRM_ERROR("invalid log buffer state\n");
			/* copy whole buffer as offsets are unreliable */
			read_offset = 0;
			write_offset = buffer_size;
		}

		/* Just copy the newly written data */
		if (read_offset > write_offset) {
			memcpy(dst_data, src_data, write_offset);
			bytes_to_copy = buffer_size - read_offset;
		} else {
			bytes_to_copy = write_offset - read_offset;
		}
		memcpy(dst_data + read_offset,
		       src_data + read_offset, bytes_to_copy);

		src_data += buffer_size;
		dst_data += buffer_size;

		/* FIXME: invalidate/flush for log buffer needed */
	}

	if (log_buf_snapshot_state)
		guc_move_to_next_buf(guc);
	else {
		/* Used rate limited to avoid deluge of messages, logs might be
		 * getting consumed by User at a slow rate.
		 */
		DRM_ERROR_RATELIMITED("no sub-buffer to capture logs\n");
		guc->log.capture_miss_count++;
	}
}

static void guc_capture_logs_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, guc.log.flush_work);

	i915_guc_capture_logs(dev_priv);
}

static void guc_log_cleanup(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	/* First disable the flush interrupt */
	gen9_disable_guc_interrupts(dev_priv);

	if (guc->log.flush_wq)
		destroy_workqueue(guc->log.flush_wq);

	guc->log.flush_wq = NULL;

	if (guc->log.relay_chan)
		guc_log_remove_relay_file(guc);

	guc->log.relay_chan = NULL;

	if (guc->log.buf_addr)
		i915_gem_object_unpin_map(guc->log.vma->obj);

	guc->log.buf_addr = NULL;
}

static int guc_log_create_extras(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	void *vaddr;
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	/* Nothing to do */
	if (i915.guc_log_level < 0)
		return 0;

	if (!guc->log.buf_addr) {
		/* Create a vmalloc mapping of log buffer pages */
		vaddr = i915_gem_object_pin_map(guc->log.vma->obj, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			ret = PTR_ERR(vaddr);
			DRM_ERROR("Couldn't map log buffer pages %d\n", ret);
			return ret;
		}

		guc->log.buf_addr = vaddr;
	}

	if (!guc->log.flush_wq) {
		INIT_WORK(&guc->log.flush_work, guc_capture_logs_work);

		/* Need a dedicated wq to process log buffer flush interrupts
		 * from GuC without much delay so as to avoid any loss of logs.
		 */
		guc->log.flush_wq = alloc_ordered_workqueue("i915-guc_log", WQ_HIGHPRI);
		if (guc->log.flush_wq == NULL) {
			DRM_ERROR("Couldn't allocate the wq for GuC logging\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static void guc_log_create(struct intel_guc *guc)
{
	struct i915_vma *vma;
	unsigned long offset;
	uint32_t size, flags;

	if (i915.guc_log_level > GUC_LOG_VERBOSITY_MAX)
		i915.guc_log_level = GUC_LOG_VERBOSITY_MAX;

	/* The first page is to save log buffer state. Allocate one
	 * extra page for others in case for overlap */
	size = (1 + GUC_LOG_DPC_PAGES + 1 +
		GUC_LOG_ISR_PAGES + 1 +
		GUC_LOG_CRASH_PAGES + 1) << PAGE_SHIFT;

	vma = guc->log.vma;
	if (!vma) {
		vma = guc_allocate_vma(guc, size);
		if (IS_ERR(vma)) {
			/* logging will be off */
			i915.guc_log_level = -1;
			return;
		}

		guc->log.vma = vma;

		if (guc_log_create_extras(guc)) {
			guc_log_cleanup(guc);
			i915_vma_unpin_and_release(&guc->log.vma);
			i915.guc_log_level = -1;
			return;
		}
	}

	/* each allocated unit is a page */
	flags = GUC_LOG_VALID | GUC_LOG_NOTIFY_ON_HALF_FULL |
		(GUC_LOG_DPC_PAGES << GUC_LOG_DPC_SHIFT) |
		(GUC_LOG_ISR_PAGES << GUC_LOG_ISR_SHIFT) |
		(GUC_LOG_CRASH_PAGES << GUC_LOG_CRASH_SHIFT);

	offset = i915_ggtt_offset(vma) >> PAGE_SHIFT; /* in pages */
	guc->log.flags = (offset << GUC_LOG_BUF_ADDR_SHIFT) | flags;
}

static int guc_log_late_setup(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	if (i915.guc_log_level < 0)
		return -EINVAL;

	/* If log_level was set as -1 at boot time, then setup needed to
	 * handle log buffer flush interrupts would not have been done yet,
	 * so do that now.
	 */
	ret = guc_log_create_extras(guc);
	if (ret)
		goto err;

	ret = guc_log_create_relay_file(guc);
	if (ret)
		goto err;

	return 0;
err:
	guc_log_cleanup(guc);
	/* logging will remain off */
	i915.guc_log_level = -1;
	return ret;
}

static void guc_policies_init(struct guc_policies *policies)
{
	struct guc_policy *policy;
	u32 p, i;

	policies->dpc_promote_time = 500000;
	policies->max_num_work_items = POLICY_MAX_NUM_WI;

	for (p = 0; p < GUC_CTX_PRIORITY_NUM; p++) {
		for (i = GUC_RENDER_ENGINE; i < GUC_MAX_ENGINES_NUM; i++) {
			policy = &policies->policy[p][i];

			policy->execution_quantum = 1000000;
			policy->preemption_time = 500000;
			policy->fault_time = 250000;
			policy->policy_flags = 0;
		}
	}

	policies->is_valid = 1;
}

static void guc_addon_create(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct i915_vma *vma;
	struct guc_ads *ads;
	struct guc_policies *policies;
	struct guc_mmio_reg_state *reg_state;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct page *page;
	u32 size;

	/* The ads obj includes the struct itself and buffers passed to GuC */
	size = sizeof(struct guc_ads) + sizeof(struct guc_policies) +
			sizeof(struct guc_mmio_reg_state) +
			GUC_S3_SAVE_SPACE_PAGES * PAGE_SIZE;

	vma = guc->ads_vma;
	if (!vma) {
		vma = guc_allocate_vma(guc, PAGE_ALIGN(size));
		if (IS_ERR(vma))
			return;

		guc->ads_vma = vma;
	}

	page = i915_vma_first_page(vma);
	ads = kmap(page);

	/*
	 * The GuC requires a "Golden Context" when it reinitialises
	 * engines after a reset. Here we use the Render ring default
	 * context, which must already exist and be pinned in the GGTT,
	 * so its address won't change after we've told the GuC where
	 * to find it.
	 */
	engine = dev_priv->engine[RCS];
	ads->golden_context_lrca = engine->status_page.ggtt_offset;

	for_each_engine(engine, dev_priv, id)
		ads->eng_state_size[engine->guc_id] = intel_lr_context_size(engine);

	/* GuC scheduling policies */
	policies = (void *)ads + sizeof(struct guc_ads);
	guc_policies_init(policies);

	ads->scheduler_policies =
		i915_ggtt_offset(vma) + sizeof(struct guc_ads);

	/* MMIO reg state */
	reg_state = (void *)policies + sizeof(struct guc_policies);

	for_each_engine(engine, dev_priv, id) {
		reg_state->mmio_white_list[engine->guc_id].mmio_start =
			engine->mmio_base + GUC_MMIO_WHITE_LIST_START;

		/* Nothing to be saved or restored for now. */
		reg_state->mmio_white_list[engine->guc_id].count = 0;
	}

	ads->reg_state_addr = ads->scheduler_policies +
			sizeof(struct guc_policies);

	ads->reg_state_buffer = ads->reg_state_addr +
			sizeof(struct guc_mmio_reg_state);

	kunmap(page);
}

/*
 * Set up the memory resources to be shared with the GuC.  At this point,
 * we require just one object that can be mapped through the GGTT.
 */
int i915_guc_submission_init(struct drm_i915_private *dev_priv)
{
	const size_t ctxsize = sizeof(struct guc_context_desc);
	const size_t poolsize = GUC_MAX_GPU_CONTEXTS * ctxsize;
	const size_t gemsize = round_up(poolsize, PAGE_SIZE);
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_vma *vma;

	/* Wipe bitmap & delete client in case of reinitialisation */
	bitmap_clear(guc->doorbell_bitmap, 0, GUC_MAX_DOORBELLS);
	i915_guc_submission_disable(dev_priv);

	if (!i915.enable_guc_submission)
		return 0; /* not enabled  */

	if (guc->ctx_pool_vma)
		return 0; /* already allocated */

	vma = guc_allocate_vma(guc, gemsize);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	guc->ctx_pool_vma = vma;
	ida_init(&guc->ctx_ids);
	mutex_init(&guc->action_lock);
	guc_log_create(guc);
	guc_addon_create(guc);

	return 0;
}

int i915_guc_submission_enable(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	struct drm_i915_gem_request *request;
	struct i915_guc_client *client;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* client for execbuf submission */
	client = guc_client_alloc(dev_priv,
				  INTEL_INFO(dev_priv)->ring_mask,
				  GUC_CTX_PRIORITY_KMD_NORMAL,
				  dev_priv->kernel_context);
	if (!client) {
		DRM_ERROR("Failed to create normal GuC client!\n");
		return -ENOMEM;
	}

	guc->execbuf_client = client;
	host2guc_sample_forcewake(guc, client);
	guc_init_doorbell_hw(guc);

	/* Take over from manual control of ELSP (execlists) */
	for_each_engine(engine, dev_priv, id) {
		engine->submit_request = i915_guc_submit;

		/* Replay the current set of previously submitted requests */
		list_for_each_entry(request, &engine->request_list, link) {
			client->wq_rsvd += sizeof(struct guc_wq_item);
			if (i915_sw_fence_done(&request->submit))
				i915_guc_submit(request);
		}
	}

	return 0;
}

void i915_guc_submission_disable(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	if (!guc->execbuf_client)
		return;

	/* Revert back to manual ELSP submission */
	intel_execlists_enable_submission(dev_priv);

	guc_client_free(dev_priv, guc->execbuf_client);
	guc->execbuf_client = NULL;
}

void i915_guc_submission_fini(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	i915_vma_unpin_and_release(&guc->ads_vma);
	i915_vma_unpin_and_release(&guc->log.vma);

	if (guc->ctx_pool_vma)
		ida_destroy(&guc->ctx_ids);
	i915_vma_unpin_and_release(&guc->ctx_pool_vma);
}

/**
 * intel_guc_suspend() - notify GuC entering suspend state
 * @dev:	drm device
 */
int intel_guc_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_gem_context *ctx;
	u32 data[3];

	if (guc->guc_fw.guc_fw_load_status != GUC_FIRMWARE_SUCCESS)
		return 0;

	gen9_disable_guc_interrupts(dev_priv);

	ctx = dev_priv->kernel_context;

	data[0] = HOST2GUC_ACTION_ENTER_S_STATE;
	/* any value greater than GUC_POWER_D0 */
	data[1] = GUC_POWER_D1;
	/* first page is shared data with GuC */
	data[2] = i915_ggtt_offset(ctx->engine[RCS].state);

	return host2guc_action(guc, data, ARRAY_SIZE(data));
}


/**
 * intel_guc_resume() - notify GuC resuming from suspend state
 * @dev:	drm device
 */
int intel_guc_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_gem_context *ctx;
	u32 data[3];

	if (guc->guc_fw.guc_fw_load_status != GUC_FIRMWARE_SUCCESS)
		return 0;

	if (i915.guc_log_level >= 0)
		gen9_enable_guc_interrupts(dev_priv);

	ctx = dev_priv->kernel_context;

	data[0] = HOST2GUC_ACTION_EXIT_S_STATE;
	data[1] = GUC_POWER_D0;
	/* first page is shared data with GuC */
	data[2] = i915_ggtt_offset(ctx->engine[RCS].state);

	return host2guc_action(guc, data, ARRAY_SIZE(data));
}

void i915_guc_capture_logs(struct drm_i915_private *dev_priv)
{
	guc_read_update_log_buffer(&dev_priv->guc);

	/* Generally device is expected to be active only at this
	 * time, so get/put should be really quick.
	 */
	intel_runtime_pm_get(dev_priv);
	host2guc_logbuffer_flush_complete(&dev_priv->guc);
	intel_runtime_pm_put(dev_priv);
}

void i915_guc_unregister(struct drm_i915_private *dev_priv)
{
	if (!i915.enable_guc_submission)
		return;

	mutex_lock(&dev_priv->drm.struct_mutex);
	guc_log_cleanup(&dev_priv->guc);
	mutex_unlock(&dev_priv->drm.struct_mutex);
}

void i915_guc_register(struct drm_i915_private *dev_priv)
{
	if (!i915.enable_guc_submission)
		return;

	mutex_lock(&dev_priv->drm.struct_mutex);
	guc_log_late_setup(&dev_priv->guc);
	mutex_unlock(&dev_priv->drm.struct_mutex);
}
