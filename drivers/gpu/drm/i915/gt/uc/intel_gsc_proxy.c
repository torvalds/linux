// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/component.h>

#include <drm/i915_component.h>
#include <drm/i915_gsc_proxy_mei_interface.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"
#include "intel_gsc_proxy.h"
#include "intel_gsc_uc.h"
#include "intel_gsc_uc_heci_cmd_submit.h"
#include "i915_drv.h"
#include "i915_reg.h"

/*
 * GSC proxy:
 * The GSC uC needs to communicate with the CSME to perform certain operations.
 * Since the GSC can't perform this communication directly on platforms where it
 * is integrated in GT, i915 needs to transfer the messages from GSC to CSME
 * and back. i915 must manually start the proxy flow after the GSC is loaded to
 * signal to GSC that we're ready to handle its messages and allow it to query
 * its init data from CSME; GSC will then trigger an HECI2 interrupt if it needs
 * to send messages to CSME again.
 * The proxy flow is as follow:
 * 1 - i915 submits a request to GSC asking for the message to CSME
 * 2 - GSC replies with the proxy header + payload for CSME
 * 3 - i915 sends the reply from GSC as-is to CSME via the mei proxy component
 * 4 - CSME replies with the proxy header + payload for GSC
 * 5 - i915 submits a request to GSC with the reply from CSME
 * 6 - GSC replies either with a new header + payload (same as step 2, so we
 *     restart from there) or with an end message.
 */

/*
 * The component should load quite quickly in most cases, but it could take
 * a bit. Using a very big timeout just to cover the worst case scenario
 */
#define GSC_PROXY_INIT_TIMEOUT_MS 20000

/* the protocol supports up to 32K in each direction */
#define GSC_PROXY_BUFFER_SIZE SZ_32K
#define GSC_PROXY_CHANNEL_SIZE (GSC_PROXY_BUFFER_SIZE * 2)
#define GSC_PROXY_MAX_MSG_SIZE (GSC_PROXY_BUFFER_SIZE - sizeof(struct intel_gsc_mtl_header))

/* FW-defined proxy header */
struct intel_gsc_proxy_header {
	/*
	 * hdr:
	 * Bits 0-7: type of the proxy message (see enum intel_gsc_proxy_type)
	 * Bits 8-15: rsvd
	 * Bits 16-31: length in bytes of the payload following the proxy header
	 */
	u32 hdr;
#define GSC_PROXY_TYPE		 GENMASK(7, 0)
#define GSC_PROXY_PAYLOAD_LENGTH GENMASK(31, 16)

	u32 source;		/* Source of the Proxy message */
	u32 destination;	/* Destination of the Proxy message */
#define GSC_PROXY_ADDRESSING_KMD  0x10000
#define GSC_PROXY_ADDRESSING_GSC  0x20000
#define GSC_PROXY_ADDRESSING_CSME 0x30000

	u32 status;		/* Command status */
} __packed;

/* FW-defined proxy types */
enum intel_gsc_proxy_type {
	GSC_PROXY_MSG_TYPE_PROXY_INVALID = 0,
	GSC_PROXY_MSG_TYPE_PROXY_QUERY = 1,
	GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD = 2,
	GSC_PROXY_MSG_TYPE_PROXY_END = 3,
	GSC_PROXY_MSG_TYPE_PROXY_NOTIFICATION = 4,
};

struct gsc_proxy_msg {
	struct intel_gsc_mtl_header header;
	struct intel_gsc_proxy_header proxy_header;
} __packed;

static int proxy_send_to_csme(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct i915_gsc_proxy_component *comp = gsc->proxy.component;
	struct intel_gsc_mtl_header *hdr;
	void *in = gsc->proxy.to_csme;
	void *out = gsc->proxy.to_gsc;
	u32 in_size;
	int ret;

	/* CSME msg only includes the proxy */
	hdr = in;
	in += sizeof(struct intel_gsc_mtl_header);
	out += sizeof(struct intel_gsc_mtl_header);

	in_size = hdr->message_size - sizeof(struct intel_gsc_mtl_header);

	/* the message must contain at least the proxy header */
	if (in_size < sizeof(struct intel_gsc_proxy_header) ||
	    in_size > GSC_PROXY_MAX_MSG_SIZE) {
		gt_err(gt, "Invalid CSME message size: %u\n", in_size);
		return -EINVAL;
	}

	ret = comp->ops->send(comp->mei_dev, in, in_size);
	if (ret < 0) {
		gt_err(gt, "Failed to send CSME message\n");
		return ret;
	}

	ret = comp->ops->recv(comp->mei_dev, out, GSC_PROXY_MAX_MSG_SIZE);
	if (ret < 0) {
		gt_err(gt, "Failed to receive CSME message\n");
		return ret;
	}

	return ret;
}

static int proxy_send_to_gsc(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	u32 *marker = gsc->proxy.to_csme; /* first dw of the reply header */
	u64 addr_in = i915_ggtt_offset(gsc->proxy.vma);
	u64 addr_out = addr_in + GSC_PROXY_BUFFER_SIZE;
	u32 size = ((struct gsc_proxy_msg *)gsc->proxy.to_gsc)->header.message_size;
	int err;

	/* the message must contain at least the gsc and proxy headers */
	if (size < sizeof(struct gsc_proxy_msg) || size > GSC_PROXY_BUFFER_SIZE) {
		gt_err(gt, "Invalid GSC proxy message size: %u\n", size);
		return -EINVAL;
	}

	/* clear the message marker */
	*marker = 0;

	/* make sure the marker write is flushed */
	wmb();

	/* send the request */
	err = intel_gsc_uc_heci_cmd_submit_packet(gsc, addr_in, size,
						  addr_out, GSC_PROXY_BUFFER_SIZE);

	if (!err) {
		/* wait for the reply to show up */
		err = wait_for(*marker != 0, 300);
		if (err)
			gt_err(gt, "Failed to get a proxy reply from gsc\n");
	}

	return err;
}

static int validate_proxy_header(struct intel_gsc_proxy_header *header,
				 u32 source, u32 dest)
{
	u32 type = FIELD_GET(GSC_PROXY_TYPE, header->hdr);
	u32 length = FIELD_GET(GSC_PROXY_PAYLOAD_LENGTH, header->hdr);
	int ret = 0;

	if (header->destination != dest || header->source != source) {
		ret = -ENOEXEC;
		goto fail;
	}

	switch (type) {
	case GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD:
		if (length > 0)
			break;
		fallthrough;
	case GSC_PROXY_MSG_TYPE_PROXY_INVALID:
		ret = -EIO;
		goto fail;
	default:
		break;
	}

fail:
	return ret;
}

static int proxy_query(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct gsc_proxy_msg *to_gsc = gsc->proxy.to_gsc;
	struct gsc_proxy_msg *to_csme = gsc->proxy.to_csme;
	int ret;

	intel_gsc_uc_heci_cmd_emit_mtl_header(&to_gsc->header,
					      HECI_MEADDRESS_PROXY,
					      sizeof(struct gsc_proxy_msg),
					      0);

	to_gsc->proxy_header.hdr =
		FIELD_PREP(GSC_PROXY_TYPE, GSC_PROXY_MSG_TYPE_PROXY_QUERY) |
		FIELD_PREP(GSC_PROXY_PAYLOAD_LENGTH, 0);

	to_gsc->proxy_header.source = GSC_PROXY_ADDRESSING_KMD;
	to_gsc->proxy_header.destination = GSC_PROXY_ADDRESSING_GSC;
	to_gsc->proxy_header.status = 0;

	while (1) {
		/* clear the GSC response header space */
		memset(gsc->proxy.to_csme, 0, sizeof(struct gsc_proxy_msg));

		/* send proxy message to GSC */
		ret = proxy_send_to_gsc(gsc);
		if (ret) {
			gt_err(gt, "failed to send proxy message to GSC! %d\n", ret);
			goto proxy_error;
		}

		/* stop if this was the last message */
		if (FIELD_GET(GSC_PROXY_TYPE, to_csme->proxy_header.hdr) ==
				GSC_PROXY_MSG_TYPE_PROXY_END)
			break;

		/* make sure the GSC-to-CSME proxy header is sane */
		ret = validate_proxy_header(&to_csme->proxy_header,
					    GSC_PROXY_ADDRESSING_GSC,
					    GSC_PROXY_ADDRESSING_CSME);
		if (ret) {
			gt_err(gt, "invalid GSC to CSME proxy header! %d\n", ret);
			goto proxy_error;
		}

		/* send the GSC message to the CSME */
		ret = proxy_send_to_csme(gsc);
		if (ret < 0) {
			gt_err(gt, "failed to send proxy message to CSME! %d\n", ret);
			goto proxy_error;
		}

		/* update the GSC message size with the returned value from CSME */
		to_gsc->header.message_size = ret + sizeof(struct intel_gsc_mtl_header);

		/* make sure the CSME-to-GSC proxy header is sane */
		ret = validate_proxy_header(&to_gsc->proxy_header,
					    GSC_PROXY_ADDRESSING_CSME,
					    GSC_PROXY_ADDRESSING_GSC);
		if (ret) {
			gt_err(gt, "invalid CSME to GSC proxy header! %d\n", ret);
			goto proxy_error;
		}
	}

proxy_error:
	return ret < 0 ? ret : 0;
}

int intel_gsc_proxy_request_handler(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	int err;

	if (!gsc->proxy.component_added)
		return -ENODEV;

	assert_rpm_wakelock_held(gt->uncore->rpm);

	/* when GSC is loaded, we can queue this before the component is bound */
	err = wait_for(gsc->proxy.component, GSC_PROXY_INIT_TIMEOUT_MS);
	if (err) {
		gt_err(gt, "GSC proxy component didn't bind within the expected timeout\n");
		return -EIO;
	}

	mutex_lock(&gsc->proxy.mutex);
	if (!gsc->proxy.component) {
		gt_err(gt, "GSC proxy worker called without the component being bound!\n");
		err = -EIO;
	} else {
		/*
		 * write the status bit to clear it and allow new proxy
		 * interrupts to be generated while we handle the current
		 * request, but be sure not to write the reset bit
		 */
		intel_uncore_rmw(gt->uncore, HECI_H_CSR(MTL_GSC_HECI2_BASE),
				 HECI_H_CSR_RST, HECI_H_CSR_IS);
		err = proxy_query(gsc);
	}
	mutex_unlock(&gsc->proxy.mutex);
	return err;
}

void intel_gsc_proxy_irq_handler(struct intel_gsc_uc *gsc, u32 iir)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);

	if (unlikely(!iir))
		return;

	lockdep_assert_held(gt->irq_lock);

	if (!gsc->proxy.component) {
		gt_err(gt, "GSC proxy irq received without the component being bound!\n");
		return;
	}

	gsc->gsc_work_actions |= GSC_ACTION_SW_PROXY;
	queue_work(gsc->wq, &gsc->work);
}

static int i915_gsc_proxy_component_bind(struct device *i915_kdev,
					 struct device *mei_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_uc *gsc = &gt->uc.gsc;
	intel_wakeref_t wakeref;

	/* enable HECI2 IRQs */
	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		intel_uncore_rmw(gt->uncore, HECI_H_CSR(MTL_GSC_HECI2_BASE),
				 HECI_H_CSR_RST, HECI_H_CSR_IE);

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = data;
	gsc->proxy.component->mei_dev = mei_kdev;
	mutex_unlock(&gsc->proxy.mutex);
	gt_dbg(gt, "GSC proxy mei component bound\n");

	return 0;
}

static void i915_gsc_proxy_component_unbind(struct device *i915_kdev,
					    struct device *mei_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_uc *gsc = &gt->uc.gsc;
	intel_wakeref_t wakeref;

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = NULL;
	mutex_unlock(&gsc->proxy.mutex);

	/* disable HECI2 IRQs */
	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		intel_uncore_rmw(gt->uncore, HECI_H_CSR(MTL_GSC_HECI2_BASE),
				 HECI_H_CSR_IE | HECI_H_CSR_RST, 0);
	gt_dbg(gt, "GSC proxy mei component unbound\n");
}

static const struct component_ops i915_gsc_proxy_component_ops = {
	.bind   = i915_gsc_proxy_component_bind,
	.unbind = i915_gsc_proxy_component_unbind,
};

static int proxy_channel_alloc(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct i915_vma *vma;
	void *vaddr;
	int err;

	err = intel_guc_allocate_and_map_vma(gt_to_guc(gt),
					     GSC_PROXY_CHANNEL_SIZE,
					     &vma, &vaddr);
	if (err)
		return err;

	gsc->proxy.vma = vma;
	gsc->proxy.to_gsc = vaddr;
	gsc->proxy.to_csme = vaddr + GSC_PROXY_BUFFER_SIZE;

	return 0;
}

static void proxy_channel_free(struct intel_gsc_uc *gsc)
{
	if (!gsc->proxy.vma)
		return;

	gsc->proxy.to_gsc = NULL;
	gsc->proxy.to_csme = NULL;
	i915_vma_unpin_and_release(&gsc->proxy.vma, I915_VMA_RELEASE_MAP);
}

void intel_gsc_proxy_fini(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct drm_i915_private *i915 = gt->i915;

	if (fetch_and_zero(&gsc->proxy.component_added))
		component_del(i915->drm.dev, &i915_gsc_proxy_component_ops);

	proxy_channel_free(gsc);
}

int intel_gsc_proxy_init(struct intel_gsc_uc *gsc)
{
	int err;
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct drm_i915_private *i915 = gt->i915;

	mutex_init(&gsc->proxy.mutex);

	if (!IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY)) {
		gt_info(gt, "can't init GSC proxy due to missing mei component\n");
		return -ENODEV;
	}

	err = proxy_channel_alloc(gsc);
	if (err)
		return err;

	err = component_add_typed(i915->drm.dev, &i915_gsc_proxy_component_ops,
				  I915_COMPONENT_GSC_PROXY);
	if (err < 0) {
		gt_err(gt, "Failed to add GSC_PROXY component (%d)\n", err);
		goto out_free;
	}

	gsc->proxy.component_added = true;

	return 0;

out_free:
	proxy_channel_free(gsc);
	return err;
}

