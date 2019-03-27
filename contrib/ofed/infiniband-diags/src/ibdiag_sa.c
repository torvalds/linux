/*
 * Copyright (c) 2006-2007 The Regents of the University of California.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2010 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2011 Lawrence Livermore National Security. All rights reserved.
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


#include <errno.h>
#include <infiniband/umad.h>

#include "ibdiag_common.h"
#include "ibdiag_sa.h"

/* define a common SA query structure
 * This is by no means optimal but it moves the saquery functionality out of
 * the saquery tool and provides it to other utilities.
 */

struct sa_handle * sa_get_handle(void)
{
	struct sa_handle * handle;
	handle = calloc(1, sizeof(*handle));
	if (!handle)
		IBPANIC("calloc failed");

	resolve_sm_portid(ibd_ca, ibd_ca_port, &handle->dport);
	if (!handle->dport.lid) {
		IBWARN("No SM/SA found on port %s:%d",
			ibd_ca ? "" : ibd_ca,
			ibd_ca_port);
		free(handle);
		return (NULL);
	}

	handle->dport.qp = 1;
	if (!handle->dport.qkey)
		handle->dport.qkey = IB_DEFAULT_QP1_QKEY;

	handle->fd = umad_open_port(ibd_ca, ibd_ca_port);
	handle->agent = umad_register(handle->fd, IB_SA_CLASS, 2, 1, NULL);

	return handle;
}

int sa_set_handle(struct sa_handle * handle, int grh_present, ibmad_gid_t *gid)
{
	if (grh_present) {
		if (gid == NULL) {
			return -1;
		} else {
			handle->dport.grh_present = 1;
			memcpy(handle->dport.gid, gid, 16);
		}
	}
	return 0;
}

void sa_free_handle(struct sa_handle * h)
{
	umad_unregister(h->fd, h->agent);
	umad_close_port(h->fd);
	free(h);
}

int sa_query(struct sa_handle * h, uint8_t method,
		    uint16_t attr, uint32_t mod, uint64_t comp_mask,
		    uint64_t sm_key, void *data, size_t datasz,
		    struct sa_query_result *result)
{
	ib_rpc_t rpc;
	void *umad, *mad;
	int ret, offset, len = 256;

	memset(&rpc, 0, sizeof(rpc));
	rpc.mgtclass = IB_SA_CLASS;
	rpc.method = method;
	rpc.attr.id = attr;
	rpc.attr.mod = mod;
	rpc.mask = comp_mask;
	rpc.datasz = datasz;
	rpc.dataoffs = IB_SA_DATA_OFFS;

	umad = calloc(1, len + umad_size());
	if (!umad)
		IBPANIC("cannot alloc mem for umad: %s\n", strerror(errno));

	mad_build_pkt(umad, &rpc, &h->dport, NULL, data);

	mad_set_field64(umad_get_mad(umad), 0, IB_SA_MKEY_F, sm_key);

	if (ibdebug > 1)
		xdump(stdout, "SA Request:\n", umad_get_mad(umad), len);

	if (h->dport.grh_present) {
		ib_mad_addr_t *p_mad_addr = umad_get_mad_addr(umad);
		p_mad_addr->grh_present = 1;
		p_mad_addr->gid_index = 0;
		p_mad_addr->hop_limit = 0;
		p_mad_addr->traffic_class = 0;
		memcpy(p_mad_addr->gid, h->dport.gid, 16);
	}

	ret = umad_send(h->fd, h->agent, umad, len, ibd_timeout, 0);
	if (ret < 0) {
		IBWARN("umad_send failed: attr 0x%x: %s\n",
			attr, strerror(errno));
		free(umad);
		return (-ret);
	}

recv_mad:
	ret = umad_recv(h->fd, umad, &len, ibd_timeout);
	if (ret < 0) {
		if (errno == ENOSPC) {
			umad = realloc(umad, umad_size() + len);
			goto recv_mad;
		}
		IBWARN("umad_recv failed: attr 0x%x: %s\n", attr,
			strerror(errno));
		free(umad);
		return (-ret);
	}

	if ((ret = umad_status(umad)))
		return ret;

	mad = umad_get_mad(umad);

	if (ibdebug > 1)
		xdump(stdout, "SA Response:\n", mad, len);

	method = (uint8_t) mad_get_field(mad, 0, IB_MAD_METHOD_F);
	offset = mad_get_field(mad, 0, IB_SA_ATTROFFS_F);
	result->status = mad_get_field(mad, 0, IB_MAD_STATUS_F);
	result->p_result_madw = mad;
	if (result->status != IB_SA_MAD_STATUS_SUCCESS)
		result->result_cnt = 0;
	else if (method != IB_MAD_METHOD_GET_TABLE)
		result->result_cnt = 1;
	else if (!offset)
		result->result_cnt = 0;
	else
		result->result_cnt = (len - IB_SA_DATA_OFFS) / (offset << 3);

	return 0;
}

void sa_free_result_mad(struct sa_query_result *result)
{
	if (result->p_result_madw) {
		free((uint8_t *) result->p_result_madw - umad_size());
		result->p_result_madw = NULL;
	}
}

void *sa_get_query_rec(void *mad, unsigned i)
{
	int offset = mad_get_field(mad, 0, IB_SA_ATTROFFS_F);
	return (uint8_t *) mad + IB_SA_DATA_OFFS + i * (offset << 3);
}

static const char *ib_sa_error_str[] = {
	"SA_NO_ERROR",
	"SA_ERR_NO_RESOURCES",
	"SA_ERR_REQ_INVALID",
	"SA_ERR_NO_RECORDS",
	"SA_ERR_TOO_MANY_RECORDS",
	"SA_ERR_REQ_INVALID_GID",
	"SA_ERR_REQ_INSUFFICIENT_COMPONENTS",
	"SA_ERR_REQ_DENIED",
	"SA_ERR_STATUS_PRIO_SUGGESTED",
	"SA_ERR_UNKNOWN"
};

#define ARR_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SA_ERR_UNKNOWN (ARR_SIZE(ib_sa_error_str) - 1)

static inline const char *ib_sa_err_str(IN uint8_t status)
{
	if (status > SA_ERR_UNKNOWN)
		status = SA_ERR_UNKNOWN;
	return (ib_sa_error_str[status]);
}

static const char *ib_mad_inv_field_str[] = {
	"MAD No invalid fields",
	"MAD Bad version",
	"MAD Method specified is not supported",
	"MAD Method/Attribute combination is not supported",
	"MAD Reserved",
	"MAD Reserved",
	"MAD Reserved",
	"MAD Invalid value in Attribute field(s) or Attribute Modifier"
	"MAD UNKNOWN ERROR"
};
#define MAD_ERR_UNKNOWN (ARR_SIZE(ib_mad_inv_field_str) - 1)

static inline const char *ib_mad_inv_field_err_str(IN uint8_t f)
{
	if (f > MAD_ERR_UNKNOWN)
		f = MAD_ERR_UNKNOWN;
	return (ib_mad_inv_field_str[f]);
}

void sa_report_err(int status)
{
	int st = status & 0xff;
	char mad_err_str[64] = { 0 };
	char sa_err_str[64] = { 0 };

	if (st)
		sprintf(mad_err_str, " (%s; %s; %s)",
			(st & 0x1) ? "BUSY" : "",
			(st & 0x2) ? "Redirection Required" : "",
			ib_mad_inv_field_err_str(st>>2));


	st = status >> 8;
	if (st)
		sprintf(sa_err_str, " SA(%s)", ib_sa_err_str((uint8_t) st));

	fprintf(stderr, "ERROR: Query result returned 0x%04x, %s%s\n",
		status, mad_err_str, sa_err_str);
}
