// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gsc_proxy.h"

#include <linux/component.h>
#include <linux/delay.h>

#include <drm/drm_managed.h>
#include <drm/intel/i915_component.h>
#include <drm/intel/i915_gsc_proxy_mei_interface.h>

#include "abi/gsc_proxy_commands_abi.h"
#include "regs/xe_gsc_regs.h"
#include "xe_bo.h"
#include "xe_force_wake.h"
#include "xe_gsc.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pm.h"

/*
 * GSC proxy:
 * The GSC uC needs to communicate with the CSME to perform certain operations.
 * Since the GSC can't perform this communication directly on platforms where it
 * is integrated in GT, the graphics driver needs to transfer the messages from
 * GSC to CSME and back. The proxy flow must be manually started after the GSC
 * is loaded to signal to GSC that we're ready to handle its messages and allow
 * it to query its init data from CSME; GSC will then trigger an HECI2 interrupt
 * if it needs to send messages to CSME again.
 * The proxy flow is as follow:
 * 1 - Xe submits a request to GSC asking for the message to CSME
 * 2 - GSC replies with the proxy header + payload for CSME
 * 3 - Xe sends the reply from GSC as-is to CSME via the mei proxy component
 * 4 - CSME replies with the proxy header + payload for GSC
 * 5 - Xe submits a request to GSC with the reply from CSME
 * 6 - GSC replies either with a new header + payload (same as step 2, so we
 *     restart from there) or with an end message.
 */

/*
 * The component should load quite quickly in most cases, but it could take
 * a bit. Using a very big timeout just to cover the worst case scenario
 */
#define GSC_PROXY_INIT_TIMEOUT_MS 20000

/* shorthand define for code compactness */
#define PROXY_HDR_SIZE (sizeof(struct xe_gsc_proxy_header))

/* the protocol supports up to 32K in each direction */
#define GSC_PROXY_BUFFER_SIZE SZ_32K
#define GSC_PROXY_CHANNEL_SIZE (GSC_PROXY_BUFFER_SIZE * 2)

static struct xe_gt *
gsc_to_gt(struct xe_gsc *gsc)
{
	return container_of(gsc, struct xe_gt, uc.gsc);
}

static inline struct xe_device *kdev_to_xe(struct device *kdev)
{
	return dev_get_drvdata(kdev);
}

bool xe_gsc_proxy_init_done(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	u32 fwsts1 = xe_mmio_read32(gt, HECI_FWSTS1(MTL_GSC_HECI1_BASE));

	return REG_FIELD_GET(HECI1_FWSTS1_CURRENT_STATE, fwsts1) ==
	       HECI1_FWSTS1_PROXY_STATE_NORMAL;
}

static void __gsc_proxy_irq_rmw(struct xe_gsc *gsc, u32 clr, u32 set)
{
	struct xe_gt *gt = gsc_to_gt(gsc);

	/* make sure we never accidentally write the RST bit */
	clr |= HECI_H_CSR_RST;

	xe_mmio_rmw32(gt, HECI_H_CSR(MTL_GSC_HECI2_BASE), clr, set);
}

static void gsc_proxy_irq_clear(struct xe_gsc *gsc)
{
	/* The status bit is cleared by writing to it */
	__gsc_proxy_irq_rmw(gsc, 0, HECI_H_CSR_IS);
}

static void gsc_proxy_irq_toggle(struct xe_gsc *gsc, bool enabled)
{
	u32 set = enabled ? HECI_H_CSR_IE : 0;
	u32 clr = enabled ? 0 : HECI_H_CSR_IE;

	__gsc_proxy_irq_rmw(gsc, clr, set);
}

static int proxy_send_to_csme(struct xe_gsc *gsc, u32 size)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct i915_gsc_proxy_component *comp = gsc->proxy.component;
	int ret;

	ret = comp->ops->send(comp->mei_dev, gsc->proxy.to_csme, size);
	if (ret < 0) {
		xe_gt_err(gt, "Failed to send CSME proxy message\n");
		return ret;
	}

	ret = comp->ops->recv(comp->mei_dev, gsc->proxy.from_csme, GSC_PROXY_BUFFER_SIZE);
	if (ret < 0) {
		xe_gt_err(gt, "Failed to receive CSME proxy message\n");
		return ret;
	}

	return ret;
}

static int proxy_send_to_gsc(struct xe_gsc *gsc, u32 size)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	u64 addr_in = xe_bo_ggtt_addr(gsc->proxy.bo);
	u64 addr_out = addr_in + GSC_PROXY_BUFFER_SIZE;
	int err;

	/* the message must contain at least the gsc and proxy headers */
	if (size > GSC_PROXY_BUFFER_SIZE) {
		xe_gt_err(gt, "Invalid GSC proxy message size: %u\n", size);
		return -EINVAL;
	}

	err = xe_gsc_pkt_submit_kernel(gsc, addr_in, size,
				       addr_out, GSC_PROXY_BUFFER_SIZE);
	if (err) {
		xe_gt_err(gt, "Failed to submit gsc proxy rq (%pe)\n", ERR_PTR(err));
		return err;
	}

	return 0;
}

static int validate_proxy_header(struct xe_gsc_proxy_header *header,
				 u32 source, u32 dest, u32 max_size)
{
	u32 type = FIELD_GET(GSC_PROXY_TYPE, header->hdr);
	u32 length = FIELD_GET(GSC_PROXY_PAYLOAD_LENGTH, header->hdr);

	if (header->destination != dest || header->source != source)
		return -ENOEXEC;

	if (length + PROXY_HDR_SIZE > max_size)
		return -E2BIG;

	switch (type) {
	case GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD:
		if (length > 0)
			break;
		fallthrough;
	case GSC_PROXY_MSG_TYPE_PROXY_INVALID:
		return -EIO;
	default:
		break;
	}

	return 0;
}

#define proxy_header_wr(xe_, map_, offset_, field_, val_) \
	xe_map_wr_field(xe_, map_, offset_, struct xe_gsc_proxy_header, field_, val_)

#define proxy_header_rd(xe_, map_, offset_, field_) \
	xe_map_rd_field(xe_, map_, offset_, struct xe_gsc_proxy_header, field_)

static u32 emit_proxy_header(struct xe_device *xe, struct iosys_map *map, u32 offset)
{
	xe_map_memset(xe, map, offset, 0, PROXY_HDR_SIZE);

	proxy_header_wr(xe, map, offset, hdr,
			FIELD_PREP(GSC_PROXY_TYPE, GSC_PROXY_MSG_TYPE_PROXY_QUERY) |
			FIELD_PREP(GSC_PROXY_PAYLOAD_LENGTH, 0));

	proxy_header_wr(xe, map, offset, source, GSC_PROXY_ADDRESSING_KMD);
	proxy_header_wr(xe, map, offset, destination, GSC_PROXY_ADDRESSING_GSC);
	proxy_header_wr(xe, map, offset, status, 0);

	return offset + PROXY_HDR_SIZE;
}

static int proxy_query(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_gsc_proxy_header *to_csme_hdr = gsc->proxy.to_csme;
	void *to_csme_payload = gsc->proxy.to_csme + PROXY_HDR_SIZE;
	u32 wr_offset;
	u32 reply_offset;
	u32 size;
	int ret;

	wr_offset = xe_gsc_emit_header(xe, &gsc->proxy.to_gsc, 0,
				       HECI_MEADDRESS_PROXY, 0, PROXY_HDR_SIZE);
	wr_offset = emit_proxy_header(xe, &gsc->proxy.to_gsc, wr_offset);

	size = wr_offset;

	while (1) {
		/*
		 * Poison the GSC response header space to make sure we don't
		 * read a stale reply.
		 */
		xe_gsc_poison_header(xe, &gsc->proxy.from_gsc, 0);

		/* send proxy message to GSC */
		ret = proxy_send_to_gsc(gsc, size);
		if (ret)
			goto proxy_error;

		/* check the reply from GSC */
		ret = xe_gsc_read_out_header(xe, &gsc->proxy.from_gsc, 0,
					     PROXY_HDR_SIZE, &reply_offset);
		if (ret) {
			xe_gt_err(gt, "Invalid gsc header in proxy reply (%pe)\n",
				  ERR_PTR(ret));
			goto proxy_error;
		}

		/* copy the proxy header reply from GSC */
		xe_map_memcpy_from(xe, to_csme_hdr, &gsc->proxy.from_gsc,
				   reply_offset, PROXY_HDR_SIZE);

		/* stop if this was the last message */
		if (FIELD_GET(GSC_PROXY_TYPE, to_csme_hdr->hdr) == GSC_PROXY_MSG_TYPE_PROXY_END)
			break;

		/* make sure the GSC-to-CSME proxy header is sane */
		ret = validate_proxy_header(to_csme_hdr,
					    GSC_PROXY_ADDRESSING_GSC,
					    GSC_PROXY_ADDRESSING_CSME,
					    GSC_PROXY_BUFFER_SIZE - reply_offset);
		if (ret) {
			xe_gt_err(gt, "invalid GSC to CSME proxy header! (%pe)\n",
				  ERR_PTR(ret));
			goto proxy_error;
		}

		/* copy the rest of the message */
		size = FIELD_GET(GSC_PROXY_PAYLOAD_LENGTH, to_csme_hdr->hdr);
		xe_map_memcpy_from(xe, to_csme_payload, &gsc->proxy.from_gsc,
				   reply_offset + PROXY_HDR_SIZE, size);

		/* send the GSC message to the CSME */
		ret = proxy_send_to_csme(gsc, size + PROXY_HDR_SIZE);
		if (ret < 0)
			goto proxy_error;

		/* reply size from CSME, including the proxy header */
		size = ret;
		if (size < PROXY_HDR_SIZE) {
			xe_gt_err(gt, "CSME to GSC proxy msg too small: 0x%x\n", size);
			ret = -EPROTO;
			goto proxy_error;
		}

		/* make sure the CSME-to-GSC proxy header is sane */
		ret = validate_proxy_header(gsc->proxy.from_csme,
					    GSC_PROXY_ADDRESSING_CSME,
					    GSC_PROXY_ADDRESSING_GSC,
					    GSC_PROXY_BUFFER_SIZE - reply_offset);
		if (ret) {
			xe_gt_err(gt, "invalid CSME to GSC proxy header! %d\n", ret);
			goto proxy_error;
		}

		/* Emit a new header for sending the reply to the GSC */
		wr_offset = xe_gsc_emit_header(xe, &gsc->proxy.to_gsc, 0,
					       HECI_MEADDRESS_PROXY, 0, size);

		/* copy the CSME reply and update the total msg size to include the GSC header */
		xe_map_memcpy_to(xe, &gsc->proxy.to_gsc, wr_offset, gsc->proxy.from_csme, size);

		size += wr_offset;
	}

proxy_error:
	return ret < 0 ? ret : 0;
}

int xe_gsc_proxy_request_handler(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	int slept;
	int err;

	if (!gsc->proxy.component_added)
		return -ENODEV;

	/* when GSC is loaded, we can queue this before the component is bound */
	for (slept = 0; slept < GSC_PROXY_INIT_TIMEOUT_MS; slept += 100) {
		if (gsc->proxy.component)
			break;

		msleep(100);
	}

	mutex_lock(&gsc->proxy.mutex);
	if (!gsc->proxy.component) {
		xe_gt_err(gt, "GSC proxy component not bound!\n");
		err = -EIO;
	} else {
		/*
		 * clear the pending interrupt and allow new proxy requests to
		 * be generated while we handle the current one
		 */
		gsc_proxy_irq_clear(gsc);
		err = proxy_query(gsc);
	}
	mutex_unlock(&gsc->proxy.mutex);
	return err;
}

void xe_gsc_proxy_irq_handler(struct xe_gsc *gsc, u32 iir)
{
	struct xe_gt *gt = gsc_to_gt(gsc);

	if (unlikely(!iir))
		return;

	if (!gsc->proxy.component) {
		xe_gt_err(gt, "GSC proxy irq received without the component being bound!\n");
		return;
	}

	spin_lock(&gsc->lock);
	gsc->work_actions |= GSC_ACTION_SW_PROXY;
	spin_unlock(&gsc->lock);

	queue_work(gsc->wq, &gsc->work);
}

static int xe_gsc_proxy_component_bind(struct device *xe_kdev,
				       struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe(xe_kdev);
	struct xe_gt *gt = xe->tiles[0].media_gt;
	struct xe_gsc *gsc = &gt->uc.gsc;

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = data;
	gsc->proxy.component->mei_dev = mei_kdev;
	mutex_unlock(&gsc->proxy.mutex);

	return 0;
}

static void xe_gsc_proxy_component_unbind(struct device *xe_kdev,
					  struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe(xe_kdev);
	struct xe_gt *gt = xe->tiles[0].media_gt;
	struct xe_gsc *gsc = &gt->uc.gsc;

	xe_gsc_wait_for_worker_completion(gsc);

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = NULL;
	mutex_unlock(&gsc->proxy.mutex);
}

static const struct component_ops xe_gsc_proxy_component_ops = {
	.bind   = xe_gsc_proxy_component_bind,
	.unbind = xe_gsc_proxy_component_unbind,
};

static void proxy_channel_free(struct drm_device *drm, void *arg)
{
	struct xe_gsc *gsc = arg;

	if (!gsc->proxy.bo)
		return;

	if (gsc->proxy.to_csme) {
		kfree(gsc->proxy.to_csme);
		gsc->proxy.to_csme = NULL;
		gsc->proxy.from_csme = NULL;
	}

	if (gsc->proxy.bo) {
		iosys_map_clear(&gsc->proxy.to_gsc);
		iosys_map_clear(&gsc->proxy.from_gsc);
		xe_bo_unpin_map_no_vm(gsc->proxy.bo);
		gsc->proxy.bo = NULL;
	}
}

static int proxy_channel_alloc(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_bo *bo;
	void *csme;

	csme = kzalloc(GSC_PROXY_CHANNEL_SIZE, GFP_KERNEL);
	if (!csme)
		return -ENOMEM;

	bo = xe_bo_create_pin_map(xe, tile, NULL, GSC_PROXY_CHANNEL_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT);
	if (IS_ERR(bo)) {
		kfree(csme);
		return PTR_ERR(bo);
	}

	gsc->proxy.bo = bo;
	gsc->proxy.to_gsc = IOSYS_MAP_INIT_OFFSET(&bo->vmap, 0);
	gsc->proxy.from_gsc = IOSYS_MAP_INIT_OFFSET(&bo->vmap, GSC_PROXY_BUFFER_SIZE);
	gsc->proxy.to_csme = csme;
	gsc->proxy.from_csme = csme + GSC_PROXY_BUFFER_SIZE;

	return drmm_add_action_or_reset(&xe->drm, proxy_channel_free, gsc);
}

/**
 * xe_gsc_proxy_init() - init objects and MEI component required by GSC proxy
 * @gsc: the GSC uC
 *
 * Return: 0 if the initialization was successful, a negative errno otherwise.
 */
int xe_gsc_proxy_init(struct xe_gsc *gsc)
{
	int err;
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);

	mutex_init(&gsc->proxy.mutex);

	if (!IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY)) {
		xe_gt_info(gt, "can't init GSC proxy due to missing mei component\n");
		return -ENODEV;
	}

	/* no multi-tile devices with this feature yet */
	if (tile->id > 0) {
		xe_gt_err(gt, "unexpected GSC proxy init on tile %u\n", tile->id);
		return -EINVAL;
	}

	err = proxy_channel_alloc(gsc);
	if (err)
		return err;

	err = component_add_typed(xe->drm.dev, &xe_gsc_proxy_component_ops,
				  I915_COMPONENT_GSC_PROXY);
	if (err < 0) {
		xe_gt_err(gt, "Failed to add GSC_PROXY component (%pe)\n", ERR_PTR(err));
		return err;
	}

	gsc->proxy.component_added = true;

	/* the component must be removed before unload, so can't use drmm for cleanup */

	return 0;
}

/**
 * xe_gsc_proxy_remove() - remove the GSC proxy MEI component
 * @gsc: the GSC uC
 */
void xe_gsc_proxy_remove(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_device *xe = gt_to_xe(gt);
	int err = 0;

	if (!gsc->proxy.component_added)
		return;

	/* disable HECI2 IRQs */
	xe_pm_runtime_get(xe);
	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GSC);
	if (err)
		xe_gt_err(gt, "failed to get forcewake to disable GSC interrupts\n");

	/* try do disable irq even if forcewake failed */
	gsc_proxy_irq_toggle(gsc, false);

	if (!err)
		xe_force_wake_put(gt_to_fw(gt), XE_FW_GSC);
	xe_pm_runtime_put(xe);

	xe_gsc_wait_for_worker_completion(gsc);

	component_del(xe->drm.dev, &xe_gsc_proxy_component_ops);
	gsc->proxy.component_added = false;
}

/**
 * xe_gsc_proxy_start() - start the proxy by submitting the first request
 * @gsc: the GSC uC
 *
 * Return: 0 if the proxy are now enabled, a negative errno otherwise.
 */
int xe_gsc_proxy_start(struct xe_gsc *gsc)
{
	int err;

	/* enable the proxy interrupt in the GSC shim layer */
	gsc_proxy_irq_toggle(gsc, true);

	/*
	 * The handling of the first proxy request must be manually triggered to
	 * notify the GSC that we're ready to support the proxy flow.
	 */
	err = xe_gsc_proxy_request_handler(gsc);
	if (err)
		return err;

	if (!xe_gsc_proxy_init_done(gsc)) {
		xe_gt_err(gsc_to_gt(gsc), "GSC FW reports proxy init not completed\n");
		return -EIO;
	}

	return 0;
}
