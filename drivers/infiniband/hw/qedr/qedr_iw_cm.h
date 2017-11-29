/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
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
 *        disclaimer in the documentation and /or other materials
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
#include <rdma/iw_cm.h>

int qedr_iw_connect(struct iw_cm_id *cm_id,
		    struct iw_cm_conn_param *conn_param);

int qedr_iw_create_listen(struct iw_cm_id *cm_id, int backlog);

int qedr_iw_destroy_listen(struct iw_cm_id *cm_id);

int qedr_iw_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);

int qedr_iw_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len);

void qedr_iw_qp_add_ref(struct ib_qp *qp);

void qedr_iw_qp_rem_ref(struct ib_qp *qp);

struct ib_qp *qedr_iw_get_qp(struct ib_device *dev, int qpn);
