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
 * Completion Queue Objects
 */
/*
 *============================================================================
 *                  P U B L I C  R O U T I N E S
 *============================================================================
 */

/*
    This routine creates a completion queue based on the client completion
    queue configuration information.


    FunctionObject      - Handle to a function object
    CqBaseVa            - Base VA for a the CQ ring
    NumEntries          - CEV_CQ_CNT_* values
    solEventEnable      - 0 = All CQEs can generate Events if CQ is eventable
			1 = only CQEs with solicited bit set are eventable
    eventable           - Eventable CQ, generates interrupts.
    nodelay             - 1 = Force interrupt, relevent if CQ eventable.
			Interrupt is asserted immediately after EQE
			write is confirmed, regardless of EQ Timer
			or watermark settings.
    wme                 - Enable watermark based coalescing
    wmThresh            - High watermark(CQ fullness at which event
			or interrupt should be asserted).  These are the
			CEV_WATERMARK encoded values.
    EqObject            - EQ Handle to assign to this CQ
    ppCqObject          - Internal CQ Handle returned.

    Returns BE_SUCCESS if successfull, otherwise a useful error code is
	returned.

    IRQL < DISPATCH_LEVEL

*/
int be_cq_create(struct be_function_object *pfob,
	struct ring_desc *rd, u32 length, bool solicited_eventable,
	bool no_delay, u32 wm_thresh,
	struct be_eq_object *eq_object, struct be_cq_object *cq_object)
{
	int status = BE_SUCCESS;
	u32 num_entries_encoding;
	u32 num_entries = length / sizeof(struct MCC_CQ_ENTRY_AMAP);
	struct FWCMD_COMMON_CQ_CREATE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	u32 n;
	unsigned long irql;

	ASSERT(rd);
	ASSERT(cq_object);
	ASSERT(length % sizeof(struct MCC_CQ_ENTRY_AMAP) == 0);

	switch (num_entries) {
	case 256:
		num_entries_encoding = CEV_CQ_CNT_256;
		break;
	case 512:
		num_entries_encoding = CEV_CQ_CNT_512;
		break;
	case 1024:
		num_entries_encoding = CEV_CQ_CNT_1024;
		break;
	default:
		ASSERT(0);
		return BE_STATUS_INVALID_PARAMETER;
	}

	/*
	 * All cq entries all the same size.  Use iSCSI version
	 * as a test for the proper rd length.
	 */
	memset(cq_object, 0, sizeof(*cq_object));

	atomic_set(&cq_object->ref_count, 0);
	cq_object->parent_function = pfob;
	cq_object->eq_object = eq_object;
	cq_object->num_entries = num_entries;
	/* save for MCC cq processing */
	cq_object->va = rd->va;

	/* map into UT. */
	length = num_entries * sizeof(struct MCC_CQ_ENTRY_AMAP);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		ASSERT(wrb);
		TRACE(DL_ERR, "No free MCC WRBs in create EQ.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_CQ_CREATE);

	fwcmd->params.request.num_pages = PAGES_SPANNED(OFFSET_IN_PAGE(rd->va),
									length);

	AMAP_SET_BITS_PTR(CQ_CONTEXT, valid, &fwcmd->params.request.context, 1);
	n = pfob->pci_function_number;
	AMAP_SET_BITS_PTR(CQ_CONTEXT, Func, &fwcmd->params.request.context, n);

	n = (eq_object != NULL);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, Eventable,
				&fwcmd->params.request.context, n);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, Armed, &fwcmd->params.request.context, 1);

	n = eq_object ? eq_object->eq_id : 0;
	AMAP_SET_BITS_PTR(CQ_CONTEXT, EQID, &fwcmd->params.request.context, n);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, Count,
			&fwcmd->params.request.context, num_entries_encoding);

	n = 0; /* Protection Domain is always 0 in  Linux  driver */
	AMAP_SET_BITS_PTR(CQ_CONTEXT, PD, &fwcmd->params.request.context, n);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, NoDelay,
				&fwcmd->params.request.context, no_delay);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, SolEvent,
			&fwcmd->params.request.context, solicited_eventable);

	n = (wm_thresh != 0xFFFFFFFF);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, WME, &fwcmd->params.request.context, n);

	n = (n ? wm_thresh : 0);
	AMAP_SET_BITS_PTR(CQ_CONTEXT, Watermark,
				&fwcmd->params.request.context, n);
	/* Create a page list for the FWCMD. */
	be_rd_to_pa_list(rd, fwcmd->params.request.pages,
			  ARRAY_SIZE(fwcmd->params.request.pages));

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
			NULL, NULL, fwcmd, NULL);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "MCC to create CQ failed.");
		goto Error;
	}
	/* Remember the CQ id. */
	cq_object->cq_id = fwcmd->params.response.cq_id;

	/* insert this cq into eq_object reference */
	if (eq_object) {
		atomic_inc(&eq_object->ref_count);
		list_add_tail(&cq_object->cqlist_for_eq,
					&eq_object->cq_list_head);
	}

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
    zero, the object is destroyed and all resources that are held by this object
    are released.  The on-chip context is also destroyed along with the queue
    ID, and any mappings made into the UT.

    cq_object            - CQ handle returned from cq_object_create.

    returns the current reference count on the object

    IRQL: IRQL < DISPATCH_LEVEL
*/
int be_cq_destroy(struct be_cq_object *cq_object)
{
	int status = 0;

	/* Nothing should reference this CQ at this point. */
	ASSERT(atomic_read(&cq_object->ref_count) == 0);

	/* Send fwcmd to destroy the CQ. */
	status = be_function_ring_destroy(cq_object->parent_function,
		     cq_object->cq_id, FWCMD_RING_TYPE_CQ,
					NULL, NULL, NULL, NULL);
	ASSERT(status == 0);

	/* Remove reference if this is an eventable CQ. */
	if (cq_object->eq_object) {
		atomic_dec(&cq_object->eq_object->ref_count);
		list_del(&cq_object->cqlist_for_eq);
	}
	return BE_SUCCESS;
}

