/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc.  All rights reserved.
 * Copyright (c) 2006 Intel Corporation.  All rights reserved.
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

#ifndef SA_H
#define SA_H

#include <rdma/ib_sa.h>

static inline void ib_sa_client_get(struct ib_sa_client *client)
{
	atomic_inc(&client->users);
}

static inline void ib_sa_client_put(struct ib_sa_client *client)
{
	if (atomic_dec_and_test(&client->users))
		complete(&client->comp);
}

int ib_sa_mcmember_rec_query(struct ib_sa_client *client,
			     struct ib_device *device, u8 port_num,
			     u8 method,
			     struct ib_sa_mcmember_rec *rec,
			     ib_sa_comp_mask comp_mask,
			     int timeout_ms, gfp_t gfp_mask,
			     void (*callback)(int status,
					      struct ib_sa_mcmember_rec *resp,
					      void *context),
			     void *context,
			     struct ib_sa_query **sa_query);

int mcast_init(void);
void mcast_cleanup(void);

#endif /* SA_H */
