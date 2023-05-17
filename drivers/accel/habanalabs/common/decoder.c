// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#define VCMD_CONTROL_OFFSET			0x40	/* SWREG16 */
#define VCMD_IRQ_STATUS_OFFSET			0x44	/* SWREG17 */

#define VCMD_IRQ_STATUS_ENDCMD_MASK		0x1
#define VCMD_IRQ_STATUS_BUSERR_MASK		0x2
#define VCMD_IRQ_STATUS_TIMEOUT_MASK		0x4
#define VCMD_IRQ_STATUS_CMDERR_MASK		0x8
#define VCMD_IRQ_STATUS_ABORT_MASK		0x10
#define VCMD_IRQ_STATUS_RESET_MASK		0x20

static void dec_print_abnrm_intr_source(struct hl_device *hdev, u32 irq_status)
{
	const char *format = "abnormal interrupt source:%s%s%s%s%s%s\n";
	char *intr_source[6] = {"Unknown", "", "", "", "", ""};
	int i = 0;

	if (!irq_status)
		return;

	if (irq_status & VCMD_IRQ_STATUS_ENDCMD_MASK)
		intr_source[i++] = " ENDCMD";
	if (irq_status & VCMD_IRQ_STATUS_BUSERR_MASK)
		intr_source[i++] = " BUSERR";
	if (irq_status & VCMD_IRQ_STATUS_TIMEOUT_MASK)
		intr_source[i++] = " TIMEOUT";
	if (irq_status & VCMD_IRQ_STATUS_CMDERR_MASK)
		intr_source[i++] = " CMDERR";
	if (irq_status & VCMD_IRQ_STATUS_ABORT_MASK)
		intr_source[i++] = " ABORT";
	if (irq_status & VCMD_IRQ_STATUS_RESET_MASK)
		intr_source[i++] = " RESET";

	dev_err(hdev->dev, format, intr_source[0], intr_source[1],
		intr_source[2], intr_source[3], intr_source[4], intr_source[5]);
}

static void dec_abnrm_intr_work(struct work_struct *work)
{
	struct hl_dec *dec = container_of(work, struct hl_dec, abnrm_intr_work);
	struct hl_device *hdev = dec->hdev;
	u32 irq_status, event_mask = 0;
	bool reset_required = false;

	irq_status = RREG32(dec->base_addr + VCMD_IRQ_STATUS_OFFSET);

	dev_err(hdev->dev, "Decoder abnormal interrupt %#x, core %d\n", irq_status, dec->core_id);

	dec_print_abnrm_intr_source(hdev, irq_status);

	/* Clear the interrupt */
	WREG32(dec->base_addr + VCMD_IRQ_STATUS_OFFSET, irq_status);

	/* Flush the interrupt clear */
	RREG32(dec->base_addr + VCMD_IRQ_STATUS_OFFSET);

	if (irq_status & VCMD_IRQ_STATUS_TIMEOUT_MASK) {
		reset_required = true;
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
	}

	if (irq_status & VCMD_IRQ_STATUS_CMDERR_MASK)
		event_mask |= HL_NOTIFIER_EVENT_UNDEFINED_OPCODE;

	if (irq_status & (VCMD_IRQ_STATUS_ENDCMD_MASK |
				VCMD_IRQ_STATUS_BUSERR_MASK |
				VCMD_IRQ_STATUS_ABORT_MASK))
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;

	if (reset_required) {
		event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
		hl_device_cond_reset(hdev, 0, event_mask);
	} else if (event_mask) {
		hl_notifier_event_send_all(hdev, event_mask);
	}
}

void hl_dec_fini(struct hl_device *hdev)
{
	kfree(hdev->dec);
}

int hl_dec_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_dec *dec;
	int rc, j;

	/* if max core is 0, nothing to do*/
	if (!prop->max_dec)
		return 0;

	hdev->dec = kcalloc(prop->max_dec, sizeof(struct hl_dec), GFP_KERNEL);
	if (!hdev->dec)
		return -ENOMEM;

	for (j = 0 ; j < prop->max_dec ; j++) {
		dec = hdev->dec + j;

		dec->hdev = hdev;
		INIT_WORK(&dec->abnrm_intr_work, dec_abnrm_intr_work);
		dec->core_id = j;
		dec->base_addr = hdev->asic_funcs->get_dec_base_addr(hdev, j);
		if (!dec->base_addr) {
			dev_err(hdev->dev, "Invalid base address of decoder %d\n", j);
			rc = -EINVAL;
			goto err_dec_fini;
		}
	}

	return 0;

err_dec_fini:
	hl_dec_fini(hdev);

	return rc;
}

void hl_dec_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_dec *dec;
	int j;

	for (j = 0 ; j < prop->max_dec ; j++) {
		if (!!(prop->decoder_enabled_mask & BIT(j))) {
			dec = hdev->dec + j;
			/* Stop the decoder */
			WREG32(dec->base_addr + VCMD_CONTROL_OFFSET, 0);
		}
	}
}
