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
/*
    This routine creates an event queue based on the client completion
    queue configuration information.

    FunctionObject      - Handle to a function object
    EqBaseVa            - Base VA for a the EQ ring
    SizeEncoding        - The encoded size for the EQ entries. This value is
			either CEV_EQ_SIZE_4 or CEV_EQ_SIZE_16
    NumEntries          - CEV_CQ_CNT_* values.
    Watermark           - Enables watermark based coalescing.  This parameter
			must be of the type CEV_WMARK_* if watermarks
			are enabled.  If watermarks to to be disabled
			this value should be-1.
    TimerDelay          - If a timer delay is enabled this value should be the
			time of the delay in 8 microsecond units.  If
			delays are not used this parameter should be
			set to -1.
    ppEqObject          - Internal EQ Handle returned.

    Returns BE_SUCCESS if successfull,, otherwise a useful error code
	is returned.

    IRQL < DISPATCH_LEVEL
*/
int
be_eq_create(struct be_function_object *pfob,
		struct ring_desc *rd, u32 eqe_size, u32 num_entries,
		u32 watermark,	/* CEV_WMARK_* or -1 */
		u32 timer_delay,	/* in 8us units, or -1 */
		struct be_eq_object *eq_object)
{
	int status = BE_SUCCESS;
	u32 num_entries_encoding, eqe_size_encoding, length;
	struct FWCMD_COMMON_EQ_CREATE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	u32 n;
	unsigned long irql;

	ASSERT(rd);
	ASSERT(eq_object);

	switch (num_entries) {
	case 256:
		num_entries_encoding = CEV_EQ_CNT_256;
		break;
	case 512:
		num_entries_encoding = CEV_EQ_CNT_512;
		break;
	case 1024:
		num_entries_encoding = CEV_EQ_CNT_1024;
		break;
	case 2048:
		num_entries_encoding = CEV_EQ_CNT_2048;
		break;
	case 4096:
		num_entries_encoding = CEV_EQ_CNT_4096;
		break;
	default:
		ASSERT(0);
		return BE_STATUS_INVALID_PARAMETER;
	}

	switch (eqe_size) {
	case 4:
		eqe_size_encoding = CEV_EQ_SIZE_4;
		break;
	case 16:
		eqe_size_encoding = CEV_EQ_SIZE_16;
		break;
	default:
		ASSERT(0);
		return BE_STATUS_INVALID_PARAMETER;
	}

	if ((eqe_size == 4 && num_entries < 1024) ||
	    (eqe_size == 16 && num_entries == 4096)) {
		TRACE(DL_ERR, "Bad EQ size. eqe_size:%d num_entries:%d",
		      eqe_size, num_entries);
		ASSERT(0);
		return BE_STATUS_INVALID_PARAMETER;
	}

	memset(eq_object, 0, sizeof(*eq_object));

	atomic_set(&eq_object->ref_count, 0);
	eq_object->parent_function = pfob;
	eq_object->eq_id = 0xFFFFFFFF;

	INIT_LIST_HEAD(&eq_object->cq_list_head);

	length = num_entries * eqe_size;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		ASSERT(wrb);
		TRACE(DL_ERR, "No free MCC WRBs in create EQ.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_EQ_CREATE);

	fwcmd->params.request.num_pages = PAGES_SPANNED(OFFSET_IN_PAGE(rd->va),
									length);
	n = pfob->pci_function_number;
	AMAP_SET_BITS_PTR(EQ_CONTEXT, Func, &fwcmd->params.request.context, n);

	AMAP_SET_BITS_PTR(EQ_CONTEXT, valid, &fwcmd->params.request.context, 1);

	AMAP_SET_BITS_PTR(EQ_CONTEXT, Size,
			&fwcmd->params.request.context, eqe_size_encoding);

	n = 0; /* Protection Domain is always 0 in  Linux  driver */
	AMAP_SET_BITS_PTR(EQ_CONTEXT, PD, &fwcmd->params.request.context, n);

	/* Let the caller ARM the EQ with the doorbell. */
	AMAP_SET_BITS_PTR(EQ_CONTEXT, Armed, &fwcmd->params.request.context, 0);

	AMAP_SET_BITS_PTR(EQ_CONTEXT, Count, &fwcmd->params.request.context,
					num_entries_encoding);

	n = pfob->pci_function_number * 32;
	AMAP_SET_BITS_PTR(EQ_CONTEXT, EventVect,
				&fwcmd->params.request.context, n);
	if (watermark != -1) {
		AMAP_SET_BITS_PTR(EQ_CONTEXT, WME,
				&fwcmd->params.request.context, 1);
		AMAP_SET_BITS_PTR(EQ_CONTEXT, Watermark,
				&fwcmd->params.request.context, watermark);
		ASSERT(watermark <= CEV_WMARK_240);
	} else
		AMAP_SET_BITS_PTR(EQ_CONTEXT, WME,
					&fwcmd->params.request.context, 0);
	if (timer_delay != -1) {
		AMAP_SET_BITS_PTR(EQ_CONTEXT, TMR,
					&fwcmd->params.request.context, 1);

		ASSERT(timer_delay <= 250);	/* max value according to EAS */
		timer_delay = min(timer_delay, (u32)250);

		AMAP_SET_BITS_PTR(EQ_CONTEXT, Delay,
				&fwcmd->params.request.context, timer_delay);
	} else {
		AMAP_SET_BITS_PTR(EQ_CONTEXT, TMR,
				&fwcmd->params.request.context, 0);
	}
	/* Create a page list for the FWCMD. */
	be_rd_to_pa_list(rd, fwcmd->params.request.pages,
			  ARRAY_SIZE(fwcmd->params.request.pages));

	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
					NULL, NULL, fwcmd, NULL);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "MCC to create EQ failed.");
		goto Error;
	}
	/* Get the EQ id.  The MPU allocates the IDs. */
	eq_object->eq_id = fwcmd->params.response.eq_id;

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
    Deferences the given object. Once the object's reference count drops to
    zero, the object is destroyed and all resources that are held by this
    object are released.  The on-chip context is also destroyed along with
    the queue ID, and any mappings made into the UT.

    eq_object            - EQ handle returned from eq_object_create.

    Returns BE_SUCCESS if successfull, otherwise a useful error code
	is returned.

    IRQL: IRQL < DISPATCH_LEVEL
*/
int be_eq_destroy(struct be_eq_object *eq_object)
{
	int status = 0;

	ASSERT(atomic_read(&eq_object->ref_count) == 0);
	/* no CQs should reference this EQ now */
	ASSERT(list_empty(&eq_object->cq_list_head));

	/* Send fwcmd to destroy the EQ. */
	status = be_function_ring_destroy(eq_object->parent_function,
			     eq_object->eq_id, FWCMD_RING_TYPE_EQ,
					NULL, NULL, NULL, NULL);
	ASSERT(status == 0);

	return BE_SUCCESS;
}
/*
 *---------------------------------------------------------------------------
 * Function: be_eq_modify_delay
 *   Changes the EQ delay for a group of EQs.
 * num_eq             - The number of EQs in the eq_array to adjust.
 * 			This also is the number of delay values in
 * 			the eq_delay_array.
 * eq_array           - Array of struct be_eq_object pointers to adjust.
 * eq_delay_array     - Array of "num_eq" timer delays in units
 * 			of microseconds. The be_eq_query_delay_range
 * 			fwcmd returns the resolution and range of
 *                      legal EQ delays.
 * cb           -
 * cb_context   -
 * q_ctxt             - Optional. Pointer to a previously allocated
 * 			struct. If the MCC WRB ring is full, this
 * 			structure is used to queue the operation. It
 *                      will be posted to the MCC ring when space
 *                      becomes available. All queued commands will
 *                      be posted to the ring in the order they are
 *                      received. It is always valid to pass a pointer to
 *                      a generic be_generic_q_cntxt. However,
 *                      the specific context structs
 *                      are generally smaller than the generic struct.
 * return pend_status - BE_SUCCESS (0) on success.
 * 			BE_PENDING (postive value) if the FWCMD
 *                      completion is pending. Negative error code on failure.
 *-------------------------------------------------------------------------
 */
int
be_eq_modify_delay(struct be_function_object *pfob,
		   u32 num_eq, struct be_eq_object **eq_array,
		   u32 *eq_delay_array, mcc_wrb_cqe_callback cb,
		   void *cb_context, struct be_eq_modify_delay_q_ctxt *q_ctxt)
{
	struct FWCMD_COMMON_MODIFY_EQ_DELAY *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	struct be_generic_q_ctxt *gen_ctxt = NULL;
	u32 i;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			gen_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			gen_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_MODIFY_EQ_DELAY);

	ASSERT(num_eq > 0);
	ASSERT(num_eq <= ARRAY_SIZE(fwcmd->params.request.delay));
	fwcmd->params.request.num_eq = num_eq;
	for (i = 0; i < num_eq; i++) {
		fwcmd->params.request.delay[i].eq_id = eq_array[i]->eq_id;
		fwcmd->params.request.delay[i].delay_in_microseconds =
		    eq_delay_array[i];
	}

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, gen_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, NULL);

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

