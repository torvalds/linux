/*
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef INFINIBAND_CM_ABI_H
#define INFINIBAND_CM_ABI_H

#warning "This header is obsolete, use rdma/ib_user_cm.h instead"

#include <rdma/ib_user_cm.h>

#define cm_abi_cmd_hdr ib_ucm_cmd_hdr
#define cm_abi_create_id ib_ucm_create_id
#define cm_abi_create_id_resp ib_ucm_create_id_resp
#define cm_abi_destroy_id ib_ucm_destroy_id
#define cm_abi_destroy_id_resp ib_ucm_destroy_id_resp
#define cm_abi_attr_id ib_ucm_attr_id
#define cm_abi_attr_id_resp ib_ucm_attr_id_resp
#define cm_abi_init_qp_attr ib_ucm_init_qp_attr
#define cm_abi_listen ib_ucm_listen
#define cm_abi_establish ib_ucm_establish
#define cm_abi_notify ib_ucm_notify
#define cm_abi_private_data ib_ucm_private_data
#define cm_abi_req ib_ucm_req
#define cm_abi_rep ib_ucm_rep
#define cm_abi_info ib_ucm_info
#define cm_abi_mra ib_ucm_mra
#define cm_abi_lap ib_ucm_lap
#define cm_abi_sidr_req ib_ucm_sidr_req
#define cm_abi_sidr_rep ib_ucm_sidr_rep
#define cm_abi_event_get ib_ucm_event_get
#define cm_abi_req_event_resp ib_ucm_req_event_resp
#define cm_abi_rep_event_resp ib_ucm_rep_event_resp
#define cm_abi_rej_event_resp ib_ucm_rej_event_resp
#define cm_abi_mra_event_resp ib_ucm_mra_event_resp
#define cm_abi_lap_event_resp ib_ucm_lap_event_resp
#define cm_abi_apr_event_resp ib_ucm_apr_event_resp
#define cm_abi_sidr_req_event_resp ib_ucm_sidr_req_event_resp
#define cm_abi_sidr_rep_event_resp ib_ucm_sidr_rep_event_resp
#define cm_abi_event_resp ib_ucm_event_resp

#define CM_ABI_PRES_DATA IB_UCM_PRES_DATA
#define CM_ABI_PRES_INFO IB_UCM_PRES_INFO
#define CM_ABI_PRES_PRIMARY IB_UCM_PRES_PRIMARY
#define CM_ABI_PRES_ALTERNATE IB_UCM_PRES_ALTERNATE

#endif
