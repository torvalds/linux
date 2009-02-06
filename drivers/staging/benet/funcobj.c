/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
#include "hwlib.h"
#include "bestatus.h"


int
be_function_internal_query_firmware_config(struct be_function_object *pfob,
				   struct BE_FIRMWARE_CONFIG *config)
{
	struct FWCMD_COMMON_FIRMWARE_CONFIG *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;
	struct be_mcc_wrb_response_copy rc;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_FIRMWARE_CONFIG);

	rc.length = FIELD_SIZEOF(struct FWCMD_COMMON_FIRMWARE_CONFIG,
					params.response);
	rc.fwcmd_offset = offsetof(struct FWCMD_COMMON_FIRMWARE_CONFIG,
					params.response);
	rc.va = config;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL,
					NULL, NULL, NULL, fwcmd, &rc);
error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
    This allocates and initializes a function object based on the information
    provided by upper layer drivers.

    Returns BE_SUCCESS on success and an appropriate int on failure.

    A function object represents a single BladeEngine (logical) PCI function.
    That is a function object either represents
    the networking side of BladeEngine or the iSCSI side of BladeEngine.

    This routine will also detect and create an appropriate PD object for the
    PCI function as needed.
*/
int
be_function_object_create(u8 __iomem *csr_va, u8 __iomem *db_va,
		u8 __iomem *pci_va, u32 function_type,
		struct ring_desc *mailbox, struct be_function_object *pfob)
{
	int status;

	ASSERT(pfob);	/* not a magic assert */
	ASSERT(function_type <= 2);

	TRACE(DL_INFO, "Create function object. type:%s object:0x%p",
	      (function_type == BE_FUNCTION_TYPE_ISCSI ? "iSCSI" :
	       (function_type == BE_FUNCTION_TYPE_NETWORK ? "Network" :
		"Arm")), pfob);

	memset(pfob, 0, sizeof(*pfob));

	pfob->type = function_type;
	pfob->csr_va = csr_va;
	pfob->db_va = db_va;
	pfob->pci_va = pci_va;

	spin_lock_init(&pfob->cq_lock);
	spin_lock_init(&pfob->post_lock);
	spin_lock_init(&pfob->mcc_context_lock);


	pfob->pci_function_number = 1;


	pfob->emulate = false;
	TRACE(DL_NOTE, "Non-emulation mode");
	status = be_drive_POST(pfob);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "BladeEngine POST failed.");
		goto error;
	}

	/* Initialize the mailbox */
	status = be_mpu_init_mailbox(pfob, mailbox);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "Failed to initialize mailbox.");
		goto error;
	}
	/*
	 * Cache the firmware config for ASSERTs in hwclib and later
	 * driver queries.
	 */
	status = be_function_internal_query_firmware_config(pfob,
					       &pfob->fw_config);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "Failed to query firmware config.");
		goto error;
	}

error:
	if (status != BE_SUCCESS) {
		/* No cleanup necessary */
		TRACE(DL_ERR, "Failed to create function.");
		memset(pfob, 0, sizeof(*pfob));
	}
	return status;
}

/*
    This routine drops the reference count on a given function object. Once
    the reference count falls to zero, the function object is destroyed and all
    resources held are freed.

    FunctionObject      - The function object to drop the reference to.
*/
int be_function_object_destroy(struct be_function_object *pfob)
{
	TRACE(DL_INFO, "Destroy pfob. Object:0x%p",
	      pfob);


	ASSERT(pfob->mcc == NULL);

	return BE_SUCCESS;
}

int be_function_cleanup(struct be_function_object *pfob)
{
	int status = 0;
	u32 isr;
	u32 host_intr;
	struct PCICFG_HOST_TIMER_INT_CTRL_CSR_AMAP ctrl;


	if (pfob->type == BE_FUNCTION_TYPE_NETWORK) {
		status = be_rxf_multicast_config(pfob, false, 0,
						NULL, NULL, NULL, NULL);
		ASSERT(status == BE_SUCCESS);
	}
	/* VLAN */
	status = be_rxf_vlan_config(pfob, false, 0, NULL, NULL, NULL, NULL);
	ASSERT(status == BE_SUCCESS);
	/*
	 * MCC Queue -- Switches to mailbox mode.  May want to destroy
	 * all but the MCC CQ before this call if polling CQ is much better
	 * performance than polling mailbox register.
	 */
	if (pfob->mcc)
		status = be_mcc_ring_destroy(pfob->mcc);
	/*
	 * If interrupts are disabled, clear any CEV interrupt assertions that
	 * fired after we stopped processing EQs.
	 */
	ctrl.dw[0] = PCICFG1_READ(pfob, host_timer_int_ctrl);
	host_intr = AMAP_GET_BITS_PTR(PCICFG_HOST_TIMER_INT_CTRL_CSR,
							hostintr, ctrl.dw);
	if (!host_intr)
		if (pfob->type == BE_FUNCTION_TYPE_NETWORK)
			isr = CSR_READ(pfob, cev.isr1);
		else
			isr = CSR_READ(pfob, cev.isr0);
	else
		/* This should never happen... */
		TRACE(DL_ERR, "function_cleanup called with interrupt enabled");
	/* Function object destroy */
	status = be_function_object_destroy(pfob);
	ASSERT(status == BE_SUCCESS);

	return status;
}


void *
be_function_prepare_embedded_fwcmd(struct be_function_object *pfob,
	struct MCC_WRB_AMAP *wrb, u32 payld_len, u32 request_length,
	u32 response_length, u32 opcode, u32 subsystem)
{
	struct FWCMD_REQUEST_HEADER *header = NULL;
	u32 n;

	ASSERT(wrb);

	n = offsetof(struct BE_MCC_WRB_AMAP, payload)/8;
	AMAP_SET_BITS_PTR(MCC_WRB, embedded, wrb, 1);
	AMAP_SET_BITS_PTR(MCC_WRB, payload_length, wrb, min(payld_len, n));
	header = (struct FWCMD_REQUEST_HEADER *)((u8 *)wrb + n);

	header->timeout = 0;
	header->domain = 0;
	header->request_length = max(request_length, response_length);
	header->opcode = opcode;
	header->subsystem = subsystem;

	return header;
}

void *
be_function_prepare_nonembedded_fwcmd(struct be_function_object *pfob,
	struct MCC_WRB_AMAP *wrb,
	void *fwcmd_va, u64 fwcmd_pa,
	u32 payld_len,
	u32 request_length,
	u32 response_length,
	u32 opcode, u32 subsystem)
{
	struct FWCMD_REQUEST_HEADER *header = NULL;
	u32 n;
	struct MCC_WRB_PAYLOAD_AMAP *plp;

	ASSERT(wrb);
	ASSERT(fwcmd_va);

	header = (struct FWCMD_REQUEST_HEADER *) fwcmd_va;

	AMAP_SET_BITS_PTR(MCC_WRB, embedded, wrb, 0);
	AMAP_SET_BITS_PTR(MCC_WRB, payload_length, wrb, payld_len);

	/*
	 * Assume one fragment. The caller may override the SGL by
	 * rewriting the 0th length and adding more entries.  They
	 * will also need to update the sge_count.
	 */
	AMAP_SET_BITS_PTR(MCC_WRB, sge_count, wrb, 1);

	n = offsetof(struct BE_MCC_WRB_AMAP, payload)/8;
	plp = (struct MCC_WRB_PAYLOAD_AMAP *)((u8 *)wrb + n);
	AMAP_SET_BITS_PTR(MCC_WRB_PAYLOAD, sgl[0].length, plp, payld_len);
	AMAP_SET_BITS_PTR(MCC_WRB_PAYLOAD, sgl[0].pa_lo, plp, (u32)fwcmd_pa);
	AMAP_SET_BITS_PTR(MCC_WRB_PAYLOAD, sgl[0].pa_hi, plp,
					upper_32_bits(fwcmd_pa));

	header->timeout = 0;
	header->domain = 0;
	header->request_length = max(request_length, response_length);
	header->opcode = opcode;
	header->subsystem = subsystem;

	return header;
}

struct MCC_WRB_AMAP *
be_function_peek_mcc_wrb(struct be_function_object *pfob)
{
	struct MCC_WRB_AMAP *wrb = NULL;
	u32 offset;

	if (pfob->mcc)
		wrb = _be_mpu_peek_ring_wrb(pfob->mcc, false);
	else {
		offset = offsetof(struct BE_MCC_MAILBOX_AMAP, wrb)/8;
		wrb = (struct MCC_WRB_AMAP *) ((u8 *) pfob->mailbox.va +
				offset);
	}

	if (wrb)
		memset(wrb, 0, sizeof(struct MCC_WRB_AMAP));

	return wrb;
}

#if defined(BE_DEBUG)
void be_function_debug_print_wrb(struct be_function_object *pfob,
		struct MCC_WRB_AMAP *wrb, void *optional_fwcmd_va,
		struct be_mcc_wrb_context *wrb_context)
{

	struct FWCMD_REQUEST_HEADER *header = NULL;
	u8 embedded;
	u32 n;

	embedded = AMAP_GET_BITS_PTR(MCC_WRB, embedded, wrb);

	if (embedded) {
		n = offsetof(struct BE_MCC_WRB_AMAP, payload)/8;
		header = (struct FWCMD_REQUEST_HEADER *)((u8 *)wrb + n);
	} else {
		header = (struct FWCMD_REQUEST_HEADER *) optional_fwcmd_va;
	}

	/* Save the completed count before posting for a debug assert. */

	if (header) {
		wrb_context->opcode = header->opcode;
		wrb_context->subsystem = header->subsystem;

	} else {
		wrb_context->opcode = 0;
		wrb_context->subsystem = 0;
	}
}
#else
#define be_function_debug_print_wrb(a_, b_, c_, d_)
#endif

int
be_function_post_mcc_wrb(struct be_function_object *pfob,
		struct MCC_WRB_AMAP *wrb,
		struct be_generic_q_ctxt *q_ctxt,
		mcc_wrb_cqe_callback cb, void *cb_context,
		mcc_wrb_cqe_callback internal_cb,
		void *internal_cb_context, void *optional_fwcmd_va,
		struct be_mcc_wrb_response_copy *rc)
{
	int status;
	struct be_mcc_wrb_context *wrb_context = NULL;
	u64 *p;

	if (q_ctxt) {
		/* Initialize context.         */
		q_ctxt->context.internal_cb = internal_cb;
		q_ctxt->context.internal_cb_context = internal_cb_context;
		q_ctxt->context.cb = cb;
		q_ctxt->context.cb_context = cb_context;
		if (rc) {
			q_ctxt->context.copy.length = rc->length;
			q_ctxt->context.copy.fwcmd_offset = rc->fwcmd_offset;
			q_ctxt->context.copy.va = rc->va;
		} else
			q_ctxt->context.copy.length = 0;

		q_ctxt->context.optional_fwcmd_va = optional_fwcmd_va;

		/* Queue this request */
		status = be_function_queue_mcc_wrb(pfob, q_ctxt);

		goto Error;
	}
	/*
	 * Allocate a WRB context struct to hold the callback pointers,
	 * status, etc.  This is required if commands complete out of order.
	 */
	wrb_context = _be_mcc_allocate_wrb_context(pfob);
	if (!wrb_context) {
		TRACE(DL_WARN, "Failed to allocate MCC WRB context.");
		status = BE_STATUS_SYSTEM_RESOURCES;
		goto Error;
	}
	/* Initialize context. */
	memset(wrb_context, 0, sizeof(*wrb_context));
	wrb_context->internal_cb = internal_cb;
	wrb_context->internal_cb_context = internal_cb_context;
	wrb_context->cb = cb;
	wrb_context->cb_context = cb_context;
	if (rc) {
		wrb_context->copy.length = rc->length;
		wrb_context->copy.fwcmd_offset = rc->fwcmd_offset;
		wrb_context->copy.va = rc->va;
	} else
		wrb_context->copy.length = 0;
	wrb_context->wrb = wrb;

	/*
	 * Copy the context pointer into the WRB opaque tag field.
	 * Verify assumption of 64-bit tag with a compile time assert.
	 */
	p = (u64 *) ((u8 *)wrb + offsetof(struct BE_MCC_WRB_AMAP, tag)/8);
	*p = (u64)(size_t)wrb_context;

	/* Print info about this FWCMD for debug builds. */
	be_function_debug_print_wrb(pfob, wrb, optional_fwcmd_va, wrb_context);

	/*
	 * issue the WRB to the MPU as appropriate
	 */
	if (pfob->mcc) {
		/*
		 * we're in WRB mode, pass to the mcc layer
		 */
		status = _be_mpu_post_wrb_ring(pfob->mcc, wrb, wrb_context);
	} else {
		/*
		 * we're in mailbox mode
		 */
		status = _be_mpu_post_wrb_mailbox(pfob, wrb, wrb_context);

		/* mailbox mode always completes synchronously */
		ASSERT(status != BE_STATUS_PENDING);
	}

Error:

	return status;
}

int
be_function_ring_destroy(struct be_function_object *pfob,
		u32 id, u32 ring_type, mcc_wrb_cqe_callback cb,
		void *cb_context, mcc_wrb_cqe_callback internal_cb,
		void *internal_cb_context)
{

	struct FWCMD_COMMON_RING_DESTROY *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	TRACE(DL_INFO, "Destroy ring id:%d type:%d", id, ring_type);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		ASSERT(wrb);
		TRACE(DL_ERR, "No free MCC WRBs in destroy ring.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_RING_DESTROY);

	fwcmd->params.request.id = id;
	fwcmd->params.request.ring_type = ring_type;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, cb, cb_context,
				internal_cb, internal_cb_context, fwcmd, NULL);
	if (status != BE_SUCCESS && status != BE_PENDING) {
		TRACE(DL_ERR, "Ring destroy fwcmd failed. id:%d ring_type:%d",
			id, ring_type);
		goto Error;
	}

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

void
be_rd_to_pa_list(struct ring_desc *rd, struct PHYS_ADDR *pa_list, u32 max_num)
{
	u32 num_pages = PAGES_SPANNED(rd->va, rd->length);
	u32 i = 0;
	u64 pa = rd->pa;
	__le64 lepa;

	ASSERT(pa_list);
	ASSERT(pa);

	for (i = 0; i < min(num_pages, max_num); i++) {
		lepa = cpu_to_le64(pa);
		pa_list[i].lo = (u32)lepa;
		pa_list[i].hi = upper_32_bits(lepa);
		pa += PAGE_SIZE;
	}
}



/*-----------------------------------------------------------------------------
 * Function: be_function_get_fw_version
 *   Retrieves the firmware version on the adpater. If the callback is
 *   NULL this call executes synchronously. If the callback is not NULL,
 *   the returned status will be BE_PENDING if the command was issued
 *   successfully.
 * pfob    -
 * fwv         - Pointer to response buffer if callback is NULL.
 * cb           - Callback function invoked when the FWCMD completes.
 * cb_context   - Passed to the callback function.
 * return pend_status - BE_SUCCESS (0) on success.
 * 			BE_PENDING (postive value) if the FWCMD
 *                      completion is pending. Negative error code on failure.
 *---------------------------------------------------------------------------
 */
int
be_function_get_fw_version(struct be_function_object *pfob,
		struct FWCMD_COMMON_GET_FW_VERSION_RESPONSE_PAYLOAD *fwv,
		mcc_wrb_cqe_callback cb, void *cb_context)
{
	int status = BE_SUCCESS;
	struct MCC_WRB_AMAP *wrb = NULL;
	struct FWCMD_COMMON_GET_FW_VERSION *fwcmd = NULL;
	unsigned long irql;
	struct be_mcc_wrb_response_copy rc;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}

	if (!cb && !fwv) {
		TRACE(DL_ERR, "callback and response buffer NULL!");
		status = BE_NOT_OK;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_GET_FW_VERSION);

	rc.length = FIELD_SIZEOF(struct FWCMD_COMMON_GET_FW_VERSION,
					params.response);
	rc.fwcmd_offset = offsetof(struct FWCMD_COMMON_GET_FW_VERSION,
					params.response);
	rc.va = fwv;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, cb,
				cb_context, NULL, NULL, fwcmd, &rc);

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

int
be_function_queue_mcc_wrb(struct be_function_object *pfob,
			  struct be_generic_q_ctxt *q_ctxt)
{
	int status;

	ASSERT(q_ctxt);

	/*
	 * issue the WRB to the MPU as appropriate
	 */
	if (pfob->mcc) {

		/* We're in ring mode.  Queue this item. */
		pfob->mcc->backlog_length++;
		list_add_tail(&q_ctxt->context.list, &pfob->mcc->backlog);
		status = BE_PENDING;
	} else {
		status = BE_NOT_OK;
	}
	return status;
}

