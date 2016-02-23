/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _CQ_EXCH_DESC_H_
#define _CQ_EXCH_DESC_H_

#include "cq_desc.h"

/* Exchange completion queue descriptor: 16B */
struct cq_exch_wq_desc {
	u16 completed_index;
	u16 q_number;
	u16 exchange_id;
	u8  tmpl;
	u8  reserved0;
	u32 reserved1;
	u8  exch_status;
	u8  reserved2[2];
	u8  type_color;
};

#define CQ_EXCH_WQ_STATUS_BITS      2
#define CQ_EXCH_WQ_STATUS_MASK      ((1 << CQ_EXCH_WQ_STATUS_BITS) - 1)

enum cq_exch_status_types {
	CQ_EXCH_WQ_STATUS_TYPE_COMPLETE = 0,
	CQ_EXCH_WQ_STATUS_TYPE_ABORT = 1,
	CQ_EXCH_WQ_STATUS_TYPE_SGL_EOF = 2,
	CQ_EXCH_WQ_STATUS_TYPE_TMPL_ERR = 3,
};

static inline void cq_exch_wq_desc_dec(struct cq_exch_wq_desc *desc_ptr,
				       u8  *type,
				       u8  *color,
				       u16 *q_number,
				       u16 *completed_index,
				       u8  *exch_status)
{
	cq_desc_dec((struct cq_desc *)desc_ptr, type,
		    color, q_number, completed_index);
	*exch_status = desc_ptr->exch_status & CQ_EXCH_WQ_STATUS_MASK;
}

struct cq_fcp_rq_desc {
	u16 completed_index_eop_sop_prt;
	u16 q_number;
	u16 exchange_id;
	u16 tmpl;
	u16 bytes_written;
	u16 vlan;
	u8  sof;
	u8  eof;
	u8  fcs_fer_fck;
	u8  type_color;
};

#define CQ_FCP_RQ_DESC_FLAGS_SOP		(1 << 15)
#define CQ_FCP_RQ_DESC_FLAGS_EOP		(1 << 14)
#define CQ_FCP_RQ_DESC_FLAGS_PRT		(1 << 12)
#define CQ_FCP_RQ_DESC_TMPL_MASK		0x1f
#define CQ_FCP_RQ_DESC_BYTES_WRITTEN_MASK	0x3fff
#define CQ_FCP_RQ_DESC_PACKET_ERR_SHIFT		14
#define CQ_FCP_RQ_DESC_PACKET_ERR_MASK (1 << CQ_FCP_RQ_DESC_PACKET_ERR_SHIFT)
#define CQ_FCP_RQ_DESC_VS_STRIPPED_SHIFT	15
#define CQ_FCP_RQ_DESC_VS_STRIPPED_MASK (1 << CQ_FCP_RQ_DESC_VS_STRIPPED_SHIFT)
#define CQ_FCP_RQ_DESC_FC_CRC_OK_MASK		0x1
#define CQ_FCP_RQ_DESC_FCOE_ERR_SHIFT		1
#define CQ_FCP_RQ_DESC_FCOE_ERR_MASK (1 << CQ_FCP_RQ_DESC_FCOE_ERR_SHIFT)
#define CQ_FCP_RQ_DESC_FCS_OK_SHIFT		7
#define CQ_FCP_RQ_DESC_FCS_OK_MASK (1 << CQ_FCP_RQ_DESC_FCS_OK_SHIFT)

static inline void cq_fcp_rq_desc_dec(struct cq_fcp_rq_desc *desc_ptr,
				      u8  *type,
				      u8  *color,
				      u16 *q_number,
				      u16 *completed_index,
				      u8  *eop,
				      u8  *sop,
				      u8  *fck,
				      u16 *exchange_id,
				      u16 *tmpl,
				      u32 *bytes_written,
				      u8  *sof,
				      u8  *eof,
				      u8  *ingress_port,
				      u8  *packet_err,
				      u8  *fcoe_err,
				      u8  *fcs_ok,
				      u8  *vlan_stripped,
				      u16 *vlan)
{
	cq_desc_dec((struct cq_desc *)desc_ptr, type,
		    color, q_number, completed_index);
	*eop = (desc_ptr->completed_index_eop_sop_prt &
		CQ_FCP_RQ_DESC_FLAGS_EOP) ? 1 : 0;
	*sop = (desc_ptr->completed_index_eop_sop_prt &
		CQ_FCP_RQ_DESC_FLAGS_SOP) ? 1 : 0;
	*ingress_port =
		(desc_ptr->completed_index_eop_sop_prt &
		 CQ_FCP_RQ_DESC_FLAGS_PRT) ? 1 : 0;
	*exchange_id = desc_ptr->exchange_id;
	*tmpl = desc_ptr->tmpl & CQ_FCP_RQ_DESC_TMPL_MASK;
	*bytes_written =
		desc_ptr->bytes_written & CQ_FCP_RQ_DESC_BYTES_WRITTEN_MASK;
	*packet_err =
		(desc_ptr->bytes_written & CQ_FCP_RQ_DESC_PACKET_ERR_MASK) >>
		CQ_FCP_RQ_DESC_PACKET_ERR_SHIFT;
	*vlan_stripped =
		(desc_ptr->bytes_written & CQ_FCP_RQ_DESC_VS_STRIPPED_MASK) >>
		CQ_FCP_RQ_DESC_VS_STRIPPED_SHIFT;
	*vlan = desc_ptr->vlan;
	*sof = desc_ptr->sof;
	*fck = desc_ptr->fcs_fer_fck & CQ_FCP_RQ_DESC_FC_CRC_OK_MASK;
	*fcoe_err = (desc_ptr->fcs_fer_fck & CQ_FCP_RQ_DESC_FCOE_ERR_MASK) >>
		CQ_FCP_RQ_DESC_FCOE_ERR_SHIFT;
	*eof = desc_ptr->eof;
	*fcs_ok =
		(desc_ptr->fcs_fer_fck & CQ_FCP_RQ_DESC_FCS_OK_MASK) >>
		CQ_FCP_RQ_DESC_FCS_OK_SHIFT;
}

struct cq_sgl_desc {
	u16 exchange_id;
	u16 q_number;
	u32 active_burst_offset;
	u32 tot_data_bytes;
	u16 tmpl;
	u8  sgl_err;
	u8  type_color;
};

enum cq_sgl_err_types {
	CQ_SGL_ERR_NO_ERROR = 0,
	CQ_SGL_ERR_OVERFLOW,         /* data ran beyond end of SGL */
	CQ_SGL_ERR_SGL_LCL_ADDR_ERR, /* sgl access to local vnic addr illegal*/
	CQ_SGL_ERR_ADDR_RSP_ERR,     /* sgl address error */
	CQ_SGL_ERR_DATA_RSP_ERR,     /* sgl data rsp error */
	CQ_SGL_ERR_CNT_ZERO_ERR,     /* SGL count is 0 */
	CQ_SGL_ERR_CNT_MAX_ERR,      /* SGL count is larger than supported */
	CQ_SGL_ERR_ORDER_ERR,        /* frames recv on both ports, order err */
	CQ_SGL_ERR_DATA_LCL_ADDR_ERR,/* sgl data buf to local vnic addr ill */
	CQ_SGL_ERR_HOST_CQ_ERR,      /* host cq entry to local vnic addr ill */
};

#define CQ_SGL_SGL_ERR_MASK             0x1f
#define CQ_SGL_TMPL_MASK                0x1f

static inline void cq_sgl_desc_dec(struct cq_sgl_desc *desc_ptr,
				   u8  *type,
				   u8  *color,
				   u16 *q_number,
				   u16 *exchange_id,
				   u32 *active_burst_offset,
				   u32 *tot_data_bytes,
				   u16 *tmpl,
				   u8  *sgl_err)
{
	/* Cheat a little by assuming exchange_id is the same as completed
	   index */
	cq_desc_dec((struct cq_desc *)desc_ptr, type, color, q_number,
		    exchange_id);
	*active_burst_offset = desc_ptr->active_burst_offset;
	*tot_data_bytes = desc_ptr->tot_data_bytes;
	*tmpl = desc_ptr->tmpl & CQ_SGL_TMPL_MASK;
	*sgl_err = desc_ptr->sgl_err & CQ_SGL_SGL_ERR_MASK;
}

#endif /* _CQ_EXCH_DESC_H_ */
