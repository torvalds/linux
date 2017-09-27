/*
 * Copyright (c) 2005 Network Appliance, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
#ifndef IWCM_H
#define IWCM_H

enum iw_cm_state {
	IW_CM_STATE_IDLE,             /* unbound, inactive */
	IW_CM_STATE_LISTEN,           /* listen waiting for connect */
	IW_CM_STATE_CONN_RECV,        /* inbound waiting for user accept */
	IW_CM_STATE_CONN_SENT,        /* outbound waiting for peer accept */
	IW_CM_STATE_ESTABLISHED,      /* established */
	IW_CM_STATE_CLOSING,	      /* disconnect */
	IW_CM_STATE_DESTROYING        /* object being deleted */
};

struct iwcm_id_private {
	struct iw_cm_id	id;
	enum iw_cm_state state;
	unsigned long flags;
	struct ib_qp *qp;
	struct completion destroy_comp;
	wait_queue_head_t connect_wait;
	struct list_head work_list;
	spinlock_t lock;
	atomic_t refcount;
	struct list_head work_free_list;
};

#define IWCM_F_DROP_EVENTS	  1
#define IWCM_F_CONNECT_WAIT       2

#endif /* IWCM_H */
