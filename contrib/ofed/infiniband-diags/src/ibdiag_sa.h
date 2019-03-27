/*
 * Copyright (c) 2006-2007 The Regents of the University of California.
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2010 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2012 Lawrence Livermore National Security. All rights reserved.
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
 */

#ifndef _IBDIAG_SA_H_
#define _IBDIAG_SA_H_

#include <infiniband/mad.h>
#include <infiniband/iba/ib_types.h>

/* define an SA query structure to be common
 * This is by no means optimal but it moves the saquery functionality out of
 * the saquery tool and provides it to other utilities.
 */
struct sa_handle {
	int fd, agent;
	ib_portid_t dport;
	struct ibmad_port *srcport;
};

struct sa_query_result {
	uint32_t status;
	unsigned result_cnt;
	void *p_result_madw;
};

/* NOTE: umad_init must be called prior to sa_get_handle */
struct sa_handle * sa_get_handle(void);
int sa_set_handle(struct sa_handle * handle, int grh_present, ibmad_gid_t *gid);
void sa_free_handle(struct sa_handle * h);

int sa_query(struct sa_handle *h, uint8_t method,
	     uint16_t attr, uint32_t mod, uint64_t comp_mask, uint64_t sm_key,
	     void *data, size_t datasz, struct sa_query_result *result);
void sa_free_result_mad(struct sa_query_result *result);
void *sa_get_query_rec(void *mad, unsigned i);
void sa_report_err(int status);

/* Macros for setting query values and ComponentMasks */
#define cl_hton8(x) (x)
#define CHECK_AND_SET_VAL(val, size, comp_with, target, name, mask) \
	if ((int##size##_t) val != (int##size##_t) comp_with) { \
		target = cl_hton##size((uint##size##_t) val); \
		comp_mask |= IB_##name##_COMPMASK_##mask; \
	}

#define CHECK_AND_SET_GID(val, target, name, mask) \
	if (valid_gid(&(val))) { \
		memcpy(&(target), &(val), sizeof(val)); \
		comp_mask |= IB_##name##_COMPMASK_##mask; \
	}

#define CHECK_AND_SET_VAL_AND_SEL(val, target, name, mask, sel) \
	if (val) { \
		target = val; \
		comp_mask |= IB_##name##_COMPMASK_##mask##sel; \
		comp_mask |= IB_##name##_COMPMASK_##mask; \
	}

#endif /* _IBDIAG_SA_H_ */
