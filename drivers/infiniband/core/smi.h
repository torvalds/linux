/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
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
 * $Id: smi.h 1389 2004-12-27 22:56:47Z roland $
 */

#ifndef __SMI_H_
#define __SMI_H_

#include <rdma/ib_smi.h>

int smi_handle_dr_smp_recv(struct ib_smp *smp,
			   u8 node_type,
			   int port_num,
			   int phys_port_cnt);
extern int smi_check_forward_dr_smp(struct ib_smp *smp);
extern int smi_handle_dr_smp_send(struct ib_smp *smp,
				  u8 node_type,
				  int port_num);
extern int smi_check_local_dr_smp(struct ib_smp *smp,
				  struct ib_device *device,
				  int port_num);

/*
 * Return 1 if the SMP should be handled by the local SMA/SM via process_mad
 */
static inline int smi_check_local_smp(struct ib_mad_agent *mad_agent,
                         	      struct ib_smp *smp)
{
	/* C14-9:3 -- We're at the end of the DR segment of path */
	/* C14-9:4 -- Hop Pointer = Hop Count + 1 -> give to SMA/SM */
	return ((mad_agent->device->process_mad &&
		!ib_get_smp_direction(smp) &&
		(smp->hop_ptr == smp->hop_cnt + 1)));
}

#endif	/* __SMI_H_ */
