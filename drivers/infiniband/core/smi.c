/*
 * Copyright (c) 2004, 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004, 2005 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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
 *
 * $Id: smi.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <rdma/ib_smi.h>
#include "smi.h"

/*
 * Fixup a directed route SMP for sending
 * Return 0 if the SMP should be discarded
 */
int smi_handle_dr_smp_send(struct ib_smp *smp,
			   u8 node_type,
			   int port_num)
{
	u8 hop_ptr, hop_cnt;

	hop_ptr = smp->hop_ptr;
	hop_cnt = smp->hop_cnt;

	/* See section 14.2.2.2, Vol 1 IB spec */
	if (!ib_get_smp_direction(smp)) {
		/* C14-9:1 */
		if (hop_cnt && hop_ptr == 0) {
			smp->hop_ptr++;
			return (smp->initial_path[smp->hop_ptr] ==
				port_num);
		}

		/* C14-9:2 */
		if (hop_ptr && hop_ptr < hop_cnt) {
			if (node_type != IB_NODE_SWITCH)
				return 0;

			/* smp->return_path set when received */
			smp->hop_ptr++;
			return (smp->initial_path[smp->hop_ptr] ==
				port_num);
		}

		/* C14-9:3 -- We're at the end of the DR segment of path */
		if (hop_ptr == hop_cnt) {
			/* smp->return_path set when received */
			smp->hop_ptr++;
			return (node_type == IB_NODE_SWITCH ||
				smp->dr_dlid == IB_LID_PERMISSIVE);
		}

		/* C14-9:4 -- hop_ptr = hop_cnt + 1 -> give to SMA/SM */
		/* C14-9:5 -- Fail unreasonable hop pointer */
		return (hop_ptr == hop_cnt + 1);

	} else {
		/* C14-13:1 */
		if (hop_cnt && hop_ptr == hop_cnt + 1) {
			smp->hop_ptr--;
			return (smp->return_path[smp->hop_ptr] ==
				port_num);
		}

		/* C14-13:2 */
		if (2 <= hop_ptr && hop_ptr <= hop_cnt) {
			if (node_type != IB_NODE_SWITCH)
				return 0;

			smp->hop_ptr--;
			return (smp->return_path[smp->hop_ptr] ==
				port_num);
		}

		/* C14-13:3 -- at the end of the DR segment of path */
		if (hop_ptr == 1) {
			smp->hop_ptr--;
			/* C14-13:3 -- SMPs destined for SM shouldn't be here */
			return (node_type == IB_NODE_SWITCH ||
				smp->dr_slid == IB_LID_PERMISSIVE);
		}

		/* C14-13:4 -- hop_ptr = 0 -> should have gone to SM */
		if (hop_ptr == 0)
			return 1;

		/* C14-13:5 -- Check for unreasonable hop pointer */
		return 0;
	}
}

/*
 * Adjust information for a received SMP
 * Return 0 if the SMP should be dropped
 */
int smi_handle_dr_smp_recv(struct ib_smp *smp,
			   u8 node_type,
			   int port_num,
			   int phys_port_cnt)
{
	u8 hop_ptr, hop_cnt;

	hop_ptr = smp->hop_ptr;
	hop_cnt = smp->hop_cnt;

	/* See section 14.2.2.2, Vol 1 IB spec */
	if (!ib_get_smp_direction(smp)) {
		/* C14-9:1 -- sender should have incremented hop_ptr */
		if (hop_cnt && hop_ptr == 0)
			return 0;

		/* C14-9:2 -- intermediate hop */
		if (hop_ptr && hop_ptr < hop_cnt) {
			if (node_type != IB_NODE_SWITCH)
				return 0;

			smp->return_path[hop_ptr] = port_num;
			/* smp->hop_ptr updated when sending */
			return (smp->initial_path[hop_ptr+1] <= phys_port_cnt);
		}

		/* C14-9:3 -- We're at the end of the DR segment of path */
		if (hop_ptr == hop_cnt) {
			if (hop_cnt)
				smp->return_path[hop_ptr] = port_num;
			/* smp->hop_ptr updated when sending */

			return (node_type == IB_NODE_SWITCH ||
				smp->dr_dlid == IB_LID_PERMISSIVE);
		}

		/* C14-9:4 -- hop_ptr = hop_cnt + 1 -> give to SMA/SM */
		/* C14-9:5 -- fail unreasonable hop pointer */
		return (hop_ptr == hop_cnt + 1);

	} else {

		/* C14-13:1 */
		if (hop_cnt && hop_ptr == hop_cnt + 1) {
			smp->hop_ptr--;
			return (smp->return_path[smp->hop_ptr] ==
				port_num);
		}

		/* C14-13:2 */
		if (2 <= hop_ptr && hop_ptr <= hop_cnt) {
			if (node_type != IB_NODE_SWITCH)
				return 0;

			/* smp->hop_ptr updated when sending */
			return (smp->return_path[hop_ptr-1] <= phys_port_cnt);
		}

		/* C14-13:3 -- We're at the end of the DR segment of path */
		if (hop_ptr == 1) {
			if (smp->dr_slid == IB_LID_PERMISSIVE) {
				/* giving SMP to SM - update hop_ptr */
				smp->hop_ptr--;
				return 1;
			}
			/* smp->hop_ptr updated when sending */
			return (node_type == IB_NODE_SWITCH);
		}

		/* C14-13:4 -- hop_ptr = 0 -> give to SM */
		/* C14-13:5 -- Check for unreasonable hop pointer */
		return (hop_ptr == 0);
	}
}

/*
 * Return 1 if the received DR SMP should be forwarded to the send queue
 * Return 0 if the SMP should be completed up the stack
 */
int smi_check_forward_dr_smp(struct ib_smp *smp)
{
	u8 hop_ptr, hop_cnt;

	hop_ptr = smp->hop_ptr;
	hop_cnt = smp->hop_cnt;

	if (!ib_get_smp_direction(smp)) {
		/* C14-9:2 -- intermediate hop */
		if (hop_ptr && hop_ptr < hop_cnt)
			return 1;

		/* C14-9:3 -- at the end of the DR segment of path */
		if (hop_ptr == hop_cnt)
			return (smp->dr_dlid == IB_LID_PERMISSIVE);

		/* C14-9:4 -- hop_ptr = hop_cnt + 1 -> give to SMA/SM */
		if (hop_ptr == hop_cnt + 1)
			return 1;
	} else {
		/* C14-13:2 */
		if (2 <= hop_ptr && hop_ptr <= hop_cnt)
			return 1;

		/* C14-13:3 -- at the end of the DR segment of path */
		if (hop_ptr == 1)
			return (smp->dr_slid != IB_LID_PERMISSIVE);
	}
	return 0;
}
