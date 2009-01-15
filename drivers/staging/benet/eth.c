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
#include <linux/if_ether.h>
#include "hwlib.h"
#include "bestatus.h"

/*
 *---------------------------------------------------------
 * Function: be_eth_sq_create_ex
 *   Creates an ethernet send ring - extended version with
 *   additional parameters.
 * pfob -
 * rd             - ring address
 * length_in_bytes -
 * type            - The type of ring to create.
 * ulp             - The requested ULP number for the ring.
 * 		     This should be zero based, i.e. 0,1,2. This must
 * 		     be valid NIC ULP based on the firmware config.
 *                   All doorbells for this ring must be sent to
 *                   this ULP. The first network ring allocated for
 *                   each ULP are higher performance than subsequent rings.
 * cq_object       - cq object for completions
 * ex_parameters   - Additional parameters (that may increase in
 * 		     future revisions). These parameters are only used
 * 		     for certain ring types -- see
 *                   struct be_eth_sq_parameters for details.
 * eth_sq          -
 * return status   - BE_SUCCESS (0) on success. Negative error code on failure.
 *---------------------------------------------------------
 */
int
be_eth_sq_create_ex(struct be_function_object *pfob, struct ring_desc *rd,
		u32 length, u32 type, u32 ulp, struct be_cq_object *cq_object,
		struct be_eth_sq_parameters *ex_parameters,
		struct be_ethsq_object *eth_sq)
{
	struct FWCMD_COMMON_ETH_TX_CREATE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	u32 n;
	unsigned long irql;

	ASSERT(rd);
	ASSERT(eth_sq);
	ASSERT(ex_parameters);

	spin_lock_irqsave(&pfob->post_lock, irql);

	memset(eth_sq, 0, sizeof(*eth_sq));

	eth_sq->parent_function = pfob;
	eth_sq->bid = 0xFFFFFFFF;
	eth_sq->cq_object = cq_object;

	/* Translate hwlib interface to arm interface. */
	switch (type) {
	case BE_ETH_TX_RING_TYPE_FORWARDING:
		type = ETH_TX_RING_TYPE_FORWARDING;
		break;
	case BE_ETH_TX_RING_TYPE_STANDARD:
		type = ETH_TX_RING_TYPE_STANDARD;
		break;
	case BE_ETH_TX_RING_TYPE_BOUND:
		ASSERT(ex_parameters->port < 2);
		type = ETH_TX_RING_TYPE_BOUND;
		break;
	default:
		TRACE(DL_ERR, "Invalid eth tx ring type:%d", type);
		return BE_NOT_OK;
		break;
	}

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		ASSERT(wrb);
		TRACE(DL_ERR, "No free MCC WRBs in create EQ.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* NIC must be supported by the current config. */
	ASSERT(pfob->fw_config.nic_ulp_mask);

	/*
	 * The ulp parameter must select a valid NIC ULP
	 * for the current config.
	 */
	ASSERT((1 << ulp) & pfob->fw_config.nic_ulp_mask);

	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_ETH_TX_CREATE);
	fwcmd->header.request.port_number = ex_parameters->port;

	AMAP_SET_BITS_PTR(ETX_CONTEXT, pd_id,
				&fwcmd->params.request.context, 0);

	n = be_ring_length_to_encoding(length, sizeof(struct ETH_WRB_AMAP));
	AMAP_SET_BITS_PTR(ETX_CONTEXT, tx_ring_size,
					&fwcmd->params.request.context, n);

	AMAP_SET_BITS_PTR(ETX_CONTEXT, cq_id_send,
			&fwcmd->params.request.context, cq_object->cq_id);

	n = pfob->pci_function_number;
	AMAP_SET_BITS_PTR(ETX_CONTEXT, func, &fwcmd->params.request.context, n);

	fwcmd->params.request.type = type;
	fwcmd->params.request.ulp_num  = (1 << ulp);
	fwcmd->params.request.num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	ASSERT(PAGES_SPANNED(rd->va, rd->length) >=
				fwcmd->params.request.num_pages);

	/* Create a page list for the FWCMD. */
	be_rd_to_pa_list(rd, fwcmd->params.request.pages,
			  ARRAY_SIZE(fwcmd->params.request.pages));

	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
					NULL, NULL, fwcmd, NULL);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "MCC to create etx queue failed.");
		goto Error;
	}
	/* save the butler ID */
	eth_sq->bid = fwcmd->params.response.cid;

	/* add a reference to the corresponding CQ */
	atomic_inc(&cq_object->ref_count);

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}


/*
    This routine destroys an ethernet send queue

    EthSq - EthSq Handle returned from EthSqCreate

    This function always return BE_SUCCESS.

    This function frees memory allocated by EthSqCreate for the EthSq Object.

*/
int be_eth_sq_destroy(struct be_ethsq_object *eth_sq)
{
	int status = 0;

	/* Send fwcmd to destroy the queue. */
	status = be_function_ring_destroy(eth_sq->parent_function, eth_sq->bid,
		     FWCMD_RING_TYPE_ETH_TX, NULL, NULL, NULL, NULL);
	ASSERT(status == 0);

	/* Derefence any associated CQs. */
	atomic_dec(&eth_sq->cq_object->ref_count);
	return status;
}
/*
    This routine attempts to set the transmit flow control parameters.

    FunctionObject      - Handle to a function object

    txfc_enable         - transmit flow control enable - true for
			  enable, false for disable

    rxfc_enable         - receive flow control enable - true for
				enable, false for disable

    Returns BE_SUCCESS if successfull, otherwise a useful int error
    code is returned.

    IRQL: < DISPATCH_LEVEL

    This function always fails in non-privileged machine context.
*/
int
be_eth_set_flow_control(struct be_function_object *pfob,
			bool txfc_enable, bool rxfc_enable)
{
	struct FWCMD_COMMON_SET_FLOW_CONTROL *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_SET_FLOW_CONTROL);

	fwcmd->params.request.rx_flow_control = rxfc_enable;
	fwcmd->params.request.tx_flow_control = txfc_enable;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
					NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "set flow control fwcmd failed.");
		goto error;
	}

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
    This routine attempts to get the transmit flow control parameters.

    pfob      - Handle to a function object

    txfc_enable         - transmit flow control enable - true for
			enable, false for disable

    rxfc_enable         - receive flow control enable - true for enable,
			false for disable

    Returns BE_SUCCESS if successfull, otherwise a useful int error code
			is returned.

    IRQL: < DISPATCH_LEVEL

    This function always fails in non-privileged machine context.
*/
int
be_eth_get_flow_control(struct be_function_object *pfob,
			bool *txfc_enable, bool *rxfc_enable)
{
	struct FWCMD_COMMON_GET_FLOW_CONTROL *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_GET_FLOW_CONTROL);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
						NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "get flow control fwcmd failed.");
		goto error;
	}

	*txfc_enable = fwcmd->params.response.tx_flow_control;
	*rxfc_enable = fwcmd->params.response.rx_flow_control;

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
 *---------------------------------------------------------
 * Function: be_eth_set_qos
 *   This function sets the ethernet transmit Quality of Service (QoS)
 *   characteristics of BladeEngine for the domain. All ethernet
 *   transmit rings of the domain will evenly share the bandwidth.
 *   The exeception to sharing is the host primary (super) ethernet
 *   transmit ring as well as the host ethernet forwarding ring
 *   for missed offload data.
 * pfob -
 * max_bps         - the maximum bits per second in units of
 * 			10 Mbps (valid 0-100)
 * max_pps         - the maximum packets per second in units
 * 			of 1 Kpps (0 indicates no limit)
 * return status   - BE_SUCCESS (0) on success. Negative error code on failure.
 *---------------------------------------------------------
 */
int
be_eth_set_qos(struct be_function_object *pfob, u32 max_bps, u32 max_pps)
{
	struct FWCMD_COMMON_SET_QOS *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_SET_QOS);

	/* Set fields in fwcmd */
	fwcmd->params.request.max_bits_per_second_NIC = max_bps;
	fwcmd->params.request.max_packets_per_second_NIC = max_pps;
	fwcmd->params.request.valid_flags = QOS_BITS_NIC | QOS_PKTS_NIC;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
					NULL, NULL, fwcmd, NULL);

	if (status != 0)
		TRACE(DL_ERR, "network set qos fwcmd failed.");

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
 *---------------------------------------------------------
 * Function: be_eth_get_qos
 *   This function retrieves the ethernet transmit Quality of Service (QoS)
 *   characteristics for the domain.
 * max_bps         - the maximum bits per second in units of
 * 			10 Mbps (valid 0-100)
 * max_pps         - the maximum packets per second in units of
 * 			1 Kpps (0 indicates no limit)
 * return status   - BE_SUCCESS (0) on success. Negative error code on failure.
 *---------------------------------------------------------
 */
int
be_eth_get_qos(struct be_function_object *pfob, u32 *max_bps, u32 *max_pps)
{
	struct FWCMD_COMMON_GET_QOS *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_GET_QOS);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
					NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "network get qos fwcmd failed.");
		goto error;
	}

	*max_bps = fwcmd->params.response.max_bits_per_second_NIC;
	*max_pps = fwcmd->params.response.max_packets_per_second_NIC;

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
 *---------------------------------------------------------
 * Function: be_eth_set_frame_size
 *   This function sets the ethernet maximum frame size. The previous
 *   values are returned.
 * pfob -
 * tx_frame_size   - maximum transmit frame size in bytes
 * rx_frame_size   - maximum receive frame size in bytes
 * return status   - BE_SUCCESS (0) on success. Negative error code on failure.
 *---------------------------------------------------------
 */
int
be_eth_set_frame_size(struct be_function_object *pfob,
		      u32 *tx_frame_size, u32 *rx_frame_size)
{
	struct FWCMD_COMMON_SET_FRAME_SIZE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_SET_FRAME_SIZE);
	fwcmd->params.request.max_tx_frame_size = *tx_frame_size;
	fwcmd->params.request.max_rx_frame_size = *rx_frame_size;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
						NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "network set frame size fwcmd failed.");
		goto error;
	}

	*tx_frame_size = fwcmd->params.response.chip_max_tx_frame_size;
	*rx_frame_size = fwcmd->params.response.chip_max_rx_frame_size;

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);
	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}


/*
    This routine creates a Ethernet receive ring.

    pfob      - handle to a function object
    rq_base_va            - base VA for the default receive ring. this must be
			exactly 8K in length and continguous physical memory.
    cq_object            - handle to a previously created CQ to be associated
			with the RQ.
    pp_eth_rq             - pointer to an opqaue handle where an eth
			receive object is returned.
    Returns BE_SUCCESS if successfull, , otherwise a useful
    int error code is returned.

    IRQL: < DISPATCH_LEVEL
    this function allocates a struct be_ethrq_object *object.
    there must be no more than 1 of these per function object, unless the
    function object supports RSS (is networking and on the host).
    the rq_base_va must point to a buffer of exactly 8K.
    the erx::host_cqid (or host_stor_cqid) register and erx::ring_page registers
    will be updated as appropriate on return
*/
int
be_eth_rq_create(struct be_function_object *pfob,
			struct ring_desc *rd, struct be_cq_object *cq_object,
			struct be_cq_object *bcmc_cq_object,
			struct be_ethrq_object *eth_rq)
{
	int status = 0;
	struct MCC_WRB_AMAP *wrb = NULL;
	struct FWCMD_COMMON_ETH_RX_CREATE *fwcmd = NULL;
	unsigned long irql;

	/* MPU will set the  */
	ASSERT(rd);
	ASSERT(eth_rq);

	spin_lock_irqsave(&pfob->post_lock, irql);

	eth_rq->parent_function = pfob;
	eth_rq->cq_object = cq_object;

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_ETH_RX_CREATE);

	fwcmd->params.request.num_pages = 2;	/* required length */
	fwcmd->params.request.cq_id = cq_object->cq_id;

	if (bcmc_cq_object)
		fwcmd->params.request.bcmc_cq_id = bcmc_cq_object->cq_id;
	else
		fwcmd->params.request.bcmc_cq_id = 0xFFFF;

	/* Create a page list for the FWCMD. */
	be_rd_to_pa_list(rd, fwcmd->params.request.pages,
			  ARRAY_SIZE(fwcmd->params.request.pages));

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
						NULL, NULL, fwcmd, NULL);
	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "fwcmd to map eth rxq frags failed.");
		goto Error;
	}
	/* Save the ring ID for cleanup. */
	eth_rq->rid = fwcmd->params.response.id;

	atomic_inc(&cq_object->ref_count);

Error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
    This routine destroys an Ethernet receive queue

    eth_rq - ethernet receive queue handle returned from eth_rq_create

    Returns BE_SUCCESS on success and an appropriate int on failure.

    This function frees resourcs allocated by EthRqCreate.
    The erx::host_cqid (or host_stor_cqid) register and erx::ring_page
    registers will be updated as appropriate on return
    IRQL: < DISPATCH_LEVEL
*/

static void be_eth_rq_destroy_internal_cb(void *context, int status,
					 struct MCC_WRB_AMAP *wrb)
{
	struct be_ethrq_object *eth_rq = (struct be_ethrq_object *) context;

	if (status != BE_SUCCESS) {
		TRACE(DL_ERR, "Destroy eth rq failed in internal callback.\n");
	} else {
		/* Dereference any CQs associated with this queue. */
		atomic_dec(&eth_rq->cq_object->ref_count);
	}

	return;
}

int be_eth_rq_destroy(struct be_ethrq_object *eth_rq)
{
	int status = BE_SUCCESS;

	/* Send fwcmd to destroy the RQ. */
	status = be_function_ring_destroy(eth_rq->parent_function,
			eth_rq->rid, FWCMD_RING_TYPE_ETH_RX, NULL, NULL,
			be_eth_rq_destroy_internal_cb, eth_rq);

	return status;
}

/*
 *---------------------------------------------------------------------------
 * Function: be_eth_rq_destroy_options
 *   Destroys an ethernet receive ring with finer granularity options
 *   than the standard be_eth_rq_destroy() API function.
 * eth_rq           -
 * flush            - Set to 1 to flush the ring, set to 0 to bypass the flush
 * cb               - Callback function on completion
 * cb_context       - Callback context
 * return status    - BE_SUCCESS (0) on success. Negative error code on failure.
 *----------------------------------------------------------------------------
 */
int
be_eth_rq_destroy_options(struct be_ethrq_object *eth_rq, bool flush,
		mcc_wrb_cqe_callback cb, void *cb_context)
{
	struct FWCMD_COMMON_RING_DESTROY *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = BE_SUCCESS;
	struct be_function_object *pfob = NULL;
	unsigned long irql;

	pfob = eth_rq->parent_function;

	spin_lock_irqsave(&pfob->post_lock, irql);

	TRACE(DL_INFO, "Destroy eth_rq ring id:%d, flush:%d", eth_rq->rid,
	      flush);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		ASSERT(wrb);
		TRACE(DL_ERR, "No free MCC WRBs in destroy eth_rq ring.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_RING_DESTROY);

	fwcmd->params.request.id = eth_rq->rid;
	fwcmd->params.request.ring_type = FWCMD_RING_TYPE_ETH_RX;
	fwcmd->params.request.bypass_flush = ((0 == flush) ? 1 : 0);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, cb, cb_context,
			be_eth_rq_destroy_internal_cb, eth_rq, fwcmd, NULL);

	if (status != BE_SUCCESS && status != BE_PENDING) {
		TRACE(DL_ERR, "eth_rq ring destroy failed. id:%d, flush:%d",
		      eth_rq->rid, flush);
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

/*
    This routine queries the frag size for erx.

    pfob      - handle to a function object

    frag_size_bytes       - erx frag size in bytes that is/was set.

    Returns BE_SUCCESS if successfull, otherwise a useful int error
    code is returned.

    IRQL: < DISPATCH_LEVEL

*/
int
be_eth_rq_get_frag_size(struct be_function_object *pfob, u32 *frag_size_bytes)
{
	struct FWCMD_ETH_GET_RX_FRAG_SIZE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	ASSERT(frag_size_bytes);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		return BE_STATUS_NO_MCC_WRB;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, ETH_GET_RX_FRAG_SIZE);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
				NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "get frag size fwcmd failed.");
		goto error;
	}

	*frag_size_bytes = 1 << fwcmd->params.response.actual_fragsize_log2;

error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}

/*
    This routine attempts to set the frag size for erx.  If the frag size is
    already set, the attempt fails and the current frag size is returned.

    pfob      - Handle to a function object

    frag_size       - Erx frag size in bytes that is/was set.

    current_frag_size_bytes    - Pointer to location where currrent frag
				 is to be rturned

    Returns BE_SUCCESS if successfull, otherwise a useful int error
    code is returned.

    IRQL: < DISPATCH_LEVEL

    This function always fails in non-privileged machine context.
*/
int
be_eth_rq_set_frag_size(struct be_function_object *pfob,
			u32 frag_size, u32 *frag_size_bytes)
{
	struct FWCMD_ETH_SET_RX_FRAG_SIZE *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	unsigned long irql;

	ASSERT(frag_size_bytes);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto error;
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, ETH_SET_RX_FRAG_SIZE);

	ASSERT(frag_size >= 128 && frag_size <= 16 * 1024);

	/* This is the log2 of the fragsize.  This is not the exact
	 * ERX encoding. */
	fwcmd->params.request.new_fragsize_log2 = __ilog2_u32(frag_size);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, NULL, NULL, NULL,
				NULL, NULL, fwcmd, NULL);

	if (status != 0) {
		TRACE(DL_ERR, "set frag size fwcmd failed.");
		goto error;
	}

	*frag_size_bytes = 1 << fwcmd->params.response.actual_fragsize_log2;
error:
	spin_unlock_irqrestore(&pfob->post_lock, irql);

	if (pfob->pend_queue_driving && pfob->mcc) {
		pfob->pend_queue_driving = 0;
		be_drive_mcc_wrb_queue(pfob->mcc);
	}
	return status;
}


/*
    This routine gets or sets a mac address for a domain
    given the port and mac.

    FunctionObject  - Function object handle.
    port1           - Set to TRUE if this function will set/get the Port 1
			address.  Only the host may set this to TRUE.
    mac1            - Set to TRUE if this function will set/get the
			MAC 1 address.  Only the host may set this to TRUE.
    write           - Set to TRUE if this function should write the mac address.
    mac_address      - Buffer of the mac address to read or write.

    Returns BE_SUCCESS if successfull, otherwise a useful int is returned.

    IRQL: < DISPATCH_LEVEL
*/
int be_rxf_mac_address_read_write(struct be_function_object *pfob,
		bool port1,	/* VM must always set to false */
		bool mac1,	/* VM must always set to false */
		bool mgmt, bool write,
		bool permanent, u8 *mac_address,
		mcc_wrb_cqe_callback cb,	/* optional */
		void *cb_context)	/* optional */
{
	int status = BE_SUCCESS;
	union {
		struct FWCMD_COMMON_NTWK_MAC_QUERY *query;
		struct FWCMD_COMMON_NTWK_MAC_SET *set;
	} fwcmd = {NULL};
	struct MCC_WRB_AMAP *wrb = NULL;
	u32 type = 0;
	unsigned long irql;
	struct be_mcc_wrb_response_copy rc;

	spin_lock_irqsave(&pfob->post_lock, irql);

	ASSERT(mac_address);

	ASSERT(port1 == false);
	ASSERT(mac1 == false);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		TRACE(DL_ERR, "MCC wrb peek failed.");
		status = BE_STATUS_NO_MCC_WRB;
		goto Error;
	}

	if (mgmt) {
		type = MAC_ADDRESS_TYPE_MANAGEMENT;
	} else {
		if (pfob->type == BE_FUNCTION_TYPE_NETWORK)
			type = MAC_ADDRESS_TYPE_NETWORK;
		else
			type = MAC_ADDRESS_TYPE_STORAGE;
	}

	if (write) {
		/* Prepares an embedded fwcmd, including
		 * request/response sizes.
		 */
		fwcmd.set = BE_PREPARE_EMBEDDED_FWCMD(pfob,
					       wrb, COMMON_NTWK_MAC_SET);

		fwcmd.set->params.request.invalidate = 0;
		fwcmd.set->params.request.mac1 = (mac1 ? 1 : 0);
		fwcmd.set->params.request.port = (port1 ? 1 : 0);
		fwcmd.set->params.request.type = type;

		/* Copy the mac address to set. */
		fwcmd.set->params.request.mac.SizeOfStructure =
			    sizeof(fwcmd.set->params.request.mac);
		memcpy(fwcmd.set->params.request.mac.MACAddress,
			mac_address, ETH_ALEN);

		/* Post the f/w command */
		status = be_function_post_mcc_wrb(pfob, wrb, NULL,
				cb, cb_context, NULL, NULL, fwcmd.set, NULL);

	} else {

		/*
		 * Prepares an embedded fwcmd, including
		 * request/response sizes.
		 */
		fwcmd.query = BE_PREPARE_EMBEDDED_FWCMD(pfob,
					       wrb, COMMON_NTWK_MAC_QUERY);

		fwcmd.query->params.request.mac1 = (mac1 ? 1 : 0);
		fwcmd.query->params.request.port = (port1 ? 1 : 0);
		fwcmd.query->params.request.type = type;
		fwcmd.query->params.request.permanent = permanent;

		rc.length = FIELD_SIZEOF(struct FWCMD_COMMON_NTWK_MAC_QUERY,
						params.response.mac.MACAddress);
		rc.fwcmd_offset = offsetof(struct FWCMD_COMMON_NTWK_MAC_QUERY,
						params.response.mac.MACAddress);
		rc.va = mac_address;
		/* Post the f/w command (with a copy for the response) */
		status = be_function_post_mcc_wrb(pfob, wrb, NULL, cb,
				cb_context, NULL, NULL, fwcmd.query, &rc);
	}

	if (status < 0) {
		TRACE(DL_ERR, "mac set/query failed.");
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

/*
    This routine writes data to context memory.

    pfob  - Function object handle.
    mac_table     - Set to the 128-bit multicast address hash table.

    Returns BE_SUCCESS if successfull, otherwise a useful int is returned.

    IRQL: < DISPATCH_LEVEL
*/

int be_rxf_multicast_config(struct be_function_object *pfob,
		bool promiscuous, u32 num, u8 *mac_table,
		mcc_wrb_cqe_callback cb,	/* optional */
		void *cb_context,
		struct be_multicast_q_ctxt *q_ctxt)
{
	int status = BE_SUCCESS;
	struct FWCMD_COMMON_NTWK_MULTICAST_SET *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;

	ASSERT(num <= ARRAY_SIZE(fwcmd->params.request.mac));

	if (num > ARRAY_SIZE(fwcmd->params.request.mac)) {
		TRACE(DL_ERR, "Too many multicast addresses. BE supports %d.",
		      (int) ARRAY_SIZE(fwcmd->params.request.mac));
		return BE_NOT_OK;
	}

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_NTWK_MULTICAST_SET);

	fwcmd->params.request.promiscuous = promiscuous;
	if (!promiscuous) {
		fwcmd->params.request.num_mac = num;
		if (num > 0) {
			ASSERT(mac_table);
			memcpy(fwcmd->params.request.mac,
						mac_table, ETH_ALEN * num);
		}
	}

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, NULL);
	if (status < 0) {
		TRACE(DL_ERR, "multicast fwcmd failed.");
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

/*
    This routine adds or removes a vlan tag from the rxf table.

    FunctionObject  - Function object handle.
    VLanTag         - VLan tag to add or remove.
    Add             - Set to TRUE if this will add a vlan tag

    Returns BE_SUCCESS if successfull, otherwise a useful int is returned.

    IRQL: < DISPATCH_LEVEL
*/
int be_rxf_vlan_config(struct be_function_object *pfob,
		bool promiscuous, u32 num, u16 *vlan_tag_array,
		mcc_wrb_cqe_callback cb,	/* optional */
		void *cb_context,
		struct be_vlan_q_ctxt *q_ctxt)	/* optional */
{
	int status = BE_SUCCESS;
	struct FWCMD_COMMON_NTWK_VLAN_CONFIG *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;

	if (num > ARRAY_SIZE(fwcmd->params.request.vlan_tag)) {
		TRACE(DL_ERR, "Too many VLAN tags.");
		return BE_NOT_OK;
	}

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);
	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_NTWK_VLAN_CONFIG);

	fwcmd->params.request.promiscuous = promiscuous;
	if (!promiscuous) {
		fwcmd->params.request.num_vlan = num;

		if (num > 0) {
			ASSERT(vlan_tag_array);
			memcpy(fwcmd->params.request.vlan_tag, vlan_tag_array,
				  num * sizeof(vlan_tag_array[0]));
		}
	}

	/* Post the commadn */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, NULL);
	if (status < 0) {
		TRACE(DL_ERR, "vlan fwcmd failed.");
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


int be_rxf_link_status(struct be_function_object *pfob,
		struct BE_LINK_STATUS *link_status,
		mcc_wrb_cqe_callback cb,
		void *cb_context,
		struct be_link_status_q_ctxt *q_ctxt)
{
	struct FWCMD_COMMON_NTWK_LINK_STATUS_QUERY *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;
	struct be_mcc_wrb_response_copy rc;

	ASSERT(link_status);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);

	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb,
					       COMMON_NTWK_LINK_STATUS_QUERY);

	rc.length = FIELD_SIZEOF(struct FWCMD_COMMON_NTWK_LINK_STATUS_QUERY,
					params.response);
	rc.fwcmd_offset = offsetof(struct FWCMD_COMMON_NTWK_LINK_STATUS_QUERY,
					params.response);
	rc.va = link_status;
	/* Post or queue the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, &rc);

	if (status < 0) {
		TRACE(DL_ERR, "link status fwcmd failed.");
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

int
be_rxf_query_eth_statistics(struct be_function_object *pfob,
		    struct FWCMD_ETH_GET_STATISTICS *va_for_fwcmd,
		    u64 pa_for_fwcmd, mcc_wrb_cqe_callback cb,
		    void *cb_context,
		    struct be_nonembedded_q_ctxt *q_ctxt)
{
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;

	ASSERT(va_for_fwcmd);
	ASSERT(pa_for_fwcmd);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);

	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}

	TRACE(DL_INFO, "Query eth stats. fwcmd va:%p pa:0x%08x_%08x",
	      va_for_fwcmd, upper_32_bits(pa_for_fwcmd), (u32)pa_for_fwcmd);

	/* Prepares an embedded fwcmd, including request/response sizes. */
	va_for_fwcmd = BE_PREPARE_NONEMBEDDED_FWCMD(pfob, wrb,
			  va_for_fwcmd, pa_for_fwcmd, ETH_GET_STATISTICS);

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
		cb, cb_context, NULL, NULL, va_for_fwcmd, NULL);
	if (status < 0) {
		TRACE(DL_ERR, "eth stats fwcmd failed.");
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

int
be_rxf_promiscuous(struct be_function_object *pfob,
		   bool enable_port0, bool enable_port1,
		   mcc_wrb_cqe_callback cb, void *cb_context,
		   struct be_promiscuous_q_ctxt *q_ctxt)
{
	struct FWCMD_ETH_PROMISCUOUS *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;


	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);

	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, ETH_PROMISCUOUS);

	fwcmd->params.request.port0_promiscuous = enable_port0;
	fwcmd->params.request.port1_promiscuous = enable_port1;

	/* Post the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, NULL);

	if (status < 0) {
		TRACE(DL_ERR, "promiscuous fwcmd failed.");
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


/*
 *-------------------------------------------------------------------------
 * Function: be_rxf_filter_config
 *   Configures BladeEngine ethernet receive filter settings.
 * pfob    -
 * settings           - Pointer to the requested filter settings.
 * 			The response from BladeEngine will be placed back
 * 			in this structure.
 * cb                 - optional
 * cb_context         - optional
 * q_ctxt             - Optional. Pointer to a previously allocated struct.
 * 			If the MCC WRB ring is full, this structure is
 * 			used to queue the operation. It will be posted
 * 			to the MCC ring when space becomes available. All
 *                      queued commands will be posted to the ring in
 *                      the order they are received. It is always valid
 *                      to pass a pointer to a generic
 *                      be_generic_q_ctxt. However, the specific
 *                      context structs are generally smaller than
 *                      the generic struct.
 * return pend_status - BE_SUCCESS (0) on success.
 * 			BE_PENDING (postive value) if the FWCMD
 *                      completion is pending. Negative error code on failure.
 *---------------------------------------------------------------------------
 */
int
be_rxf_filter_config(struct be_function_object *pfob,
		     struct NTWK_RX_FILTER_SETTINGS *settings,
		     mcc_wrb_cqe_callback cb, void *cb_context,
		     struct be_rxf_filter_q_ctxt *q_ctxt)
{
	struct FWCMD_COMMON_NTWK_RX_FILTER *fwcmd = NULL;
	struct MCC_WRB_AMAP *wrb = NULL;
	int status = 0;
	struct be_generic_q_ctxt *generic_ctxt = NULL;
	unsigned long irql;
	struct be_mcc_wrb_response_copy rc;

	ASSERT(settings);

	spin_lock_irqsave(&pfob->post_lock, irql);

	wrb = be_function_peek_mcc_wrb(pfob);

	if (!wrb) {
		if (q_ctxt && cb) {
			wrb = (struct MCC_WRB_AMAP *) &q_ctxt->wrb_header;
			generic_ctxt = (struct be_generic_q_ctxt *) q_ctxt;
			generic_ctxt->context.bytes = sizeof(*q_ctxt);
		} else {
			status = BE_STATUS_NO_MCC_WRB;
			goto Error;
		}
	}
	/* Prepares an embedded fwcmd, including request/response sizes. */
	fwcmd = BE_PREPARE_EMBEDDED_FWCMD(pfob, wrb, COMMON_NTWK_RX_FILTER);
	memcpy(&fwcmd->params.request, settings, sizeof(*settings));

	rc.length = FIELD_SIZEOF(struct FWCMD_COMMON_NTWK_RX_FILTER,
					params.response);
	rc.fwcmd_offset = offsetof(struct FWCMD_COMMON_NTWK_RX_FILTER,
					params.response);
	rc.va = settings;
	/* Post or queue the f/w command */
	status = be_function_post_mcc_wrb(pfob, wrb, generic_ctxt,
			cb, cb_context, NULL, NULL, fwcmd, &rc);

	if (status < 0) {
		TRACE(DL_ERR, "RXF/ERX filter config fwcmd failed.");
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
