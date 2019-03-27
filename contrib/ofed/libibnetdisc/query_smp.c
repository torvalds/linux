/*
 * Copyright (c) 2010 Lawrence Livermore National Laboratory
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <errno.h>
#include <infiniband/ibnetdisc.h>
#include <infiniband/umad.h>
#include "internal.h"

extern int mlnx_ext_port_info_err(smp_engine_t * engine, ibnd_smp_t * smp,
				  uint8_t * mad, void *cb_data);

static void queue_smp(smp_engine_t * engine, ibnd_smp_t * smp)
{
	smp->qnext = NULL;
	if (!engine->smp_queue_head) {
		engine->smp_queue_head = smp;
		engine->smp_queue_tail = smp;
	} else {
		engine->smp_queue_tail->qnext = smp;
		engine->smp_queue_tail = smp;
	}
}

static ibnd_smp_t *get_smp(smp_engine_t * engine)
{
	ibnd_smp_t *head = engine->smp_queue_head;
	ibnd_smp_t *tail = engine->smp_queue_tail;
	ibnd_smp_t *rc = head;
	if (head) {
		if (tail == head)
			engine->smp_queue_tail = NULL;
		engine->smp_queue_head = head->qnext;
	}
	return rc;
}

static int send_smp(ibnd_smp_t * smp, smp_engine_t * engine)
{
	int rc = 0;
	uint8_t umad[1024];
	ib_rpc_t *rpc = &smp->rpc;
	int agent = 0;

	memset(umad, 0, umad_size() + IB_MAD_SIZE);

	if (rpc->mgtclass == IB_SMI_CLASS) {
		agent = engine->smi_agent;
	} else if (rpc->mgtclass == IB_SMI_DIRECT_CLASS) {
		agent = engine->smi_dir_agent;
	} else {
		IBND_ERROR("Invalid class for RPC\n");
		return (-EIO);
	}

	if ((rc = mad_build_pkt(umad, &smp->rpc, &smp->path, NULL, NULL))
	    < 0) {
		IBND_ERROR("mad_build_pkt failed; %d\n", rc);
		return rc;
	}

	if ((rc = umad_send(engine->umad_fd, agent, umad, IB_MAD_SIZE,
			    engine->cfg->timeout_ms, engine->cfg->retries)) < 0) {
		IBND_ERROR("send failed; %d\n", rc);
		return rc;
	}

	return 0;
}

static int process_smp_queue(smp_engine_t * engine)
{
	int rc = 0;
	ibnd_smp_t *smp;
	while (cl_qmap_count(&engine->smps_on_wire)
	       < engine->cfg->max_smps) {
		smp = get_smp(engine);
		if (!smp)
			return 0;

		if ((rc = send_smp(smp, engine)) != 0) {
			free(smp);
			return rc;
		}
		cl_qmap_insert(&engine->smps_on_wire, (uint32_t) smp->rpc.trid,
			       (cl_map_item_t *) smp);
		engine->total_smps++;
	}
	return 0;
}

int issue_smp(smp_engine_t * engine, ib_portid_t * portid,
	      unsigned attrid, unsigned mod, smp_comp_cb_t cb, void *cb_data)
{
	ibnd_smp_t *smp = calloc(1, sizeof *smp);
	if (!smp) {
		IBND_ERROR("OOM\n");
		return -ENOMEM;
	}

	smp->cb = cb;
	smp->cb_data = cb_data;
	smp->path = *portid;
	smp->rpc.method = IB_MAD_METHOD_GET;
	smp->rpc.attr.id = attrid;
	smp->rpc.attr.mod = mod;
	smp->rpc.timeout = engine->cfg->timeout_ms;
	smp->rpc.datasz = IB_SMP_DATA_SIZE;
	smp->rpc.dataoffs = IB_SMP_DATA_OFFS;
	smp->rpc.trid = mad_trid();
	smp->rpc.mkey = engine->cfg->mkey;

	if (portid->lid <= 0 || portid->drpath.drslid == 0xffff ||
	    portid->drpath.drdlid == 0xffff)
		smp->rpc.mgtclass = IB_SMI_DIRECT_CLASS;	/* direct SMI */
	else
		smp->rpc.mgtclass = IB_SMI_CLASS;	/* Lid routed SMI */

	portid->sl = 0;
	portid->qp = 0;

	queue_smp(engine, smp);
	return process_smp_queue(engine);
}

static int process_one_recv(smp_engine_t * engine)
{
	int rc = 0;
	int status = 0;
	ibnd_smp_t *smp;
	uint8_t *mad;
	uint32_t trid;
	uint8_t umad[sizeof(struct ib_user_mad) + IB_MAD_SIZE];
	int length = umad_size() + IB_MAD_SIZE;

	memset(umad, 0, sizeof(umad));

	/* wait for the next message */
	if ((rc = umad_recv(engine->umad_fd, umad, &length,
			    -1)) < 0) {
		IBND_ERROR("umad_recv failed: %d\n", rc);
		return -1;
	}

	mad = umad_get_mad(umad);
	trid = (uint32_t) mad_get_field64(mad, 0, IB_MAD_TRID_F);

	smp = (ibnd_smp_t *) cl_qmap_remove(&engine->smps_on_wire, trid);
	if ((cl_map_item_t *) smp == cl_qmap_end(&engine->smps_on_wire)) {
		IBND_ERROR("Failed to find matching smp for trid (%x)\n", trid);
		return -1;
	}

	rc = process_smp_queue(engine);
	if (rc)
		goto error;

	if ((status = umad_status(umad))) {
		IBND_ERROR("umad (%s Attr 0x%x:%u) bad status %d; %s\n",
			   portid2str(&smp->path), smp->rpc.attr.id,
			   smp->rpc.attr.mod, status, strerror(status));
		if (smp->rpc.attr.id == IB_ATTR_MLNX_EXT_PORT_INFO)
			rc = mlnx_ext_port_info_err(engine, smp, mad,
						    smp->cb_data);
	} else if ((status = mad_get_field(mad, 0, IB_DRSMP_STATUS_F))) {
		IBND_ERROR("mad (%s Attr 0x%x:%u) bad status 0x%x\n",
			   portid2str(&smp->path), smp->rpc.attr.id,
			   smp->rpc.attr.mod, status);
		if (smp->rpc.attr.id == IB_ATTR_MLNX_EXT_PORT_INFO)
			rc = mlnx_ext_port_info_err(engine, smp, mad,
						    smp->cb_data);
	} else
		rc = smp->cb(engine, smp, mad, smp->cb_data);

error:
	free(smp);
	return rc;
}

int smp_engine_init(smp_engine_t * engine, char * ca_name, int ca_port,
		    void *user_data, ibnd_config_t *cfg)
{
	memset(engine, 0, sizeof(*engine));

	if (umad_init() < 0) {
		IBND_ERROR("umad_init failed\n");
		return -EIO;
	}

	engine->umad_fd = umad_open_port(ca_name, ca_port);
	if (engine->umad_fd < 0) {
		IBND_ERROR("can't open UMAD port (%s:%d)\n", ca_name, ca_port);
		return -EIO;
	}

	if ((engine->smi_agent = umad_register(engine->umad_fd,
	     IB_SMI_CLASS, 1, 0, 0)) < 0) {
		IBND_ERROR("Failed to register SMI agent on (%s:%d)\n",
			   ca_name, ca_port);
		goto eio_close;
	}

	if ((engine->smi_dir_agent = umad_register(engine->umad_fd,
	     IB_SMI_DIRECT_CLASS, 1, 0, 0)) < 0) {
		IBND_ERROR("Failed to register SMI_DIRECT agent on (%s:%d)\n",
			   ca_name, ca_port);
		goto eio_close;
	}

	engine->user_data = user_data;
	cl_qmap_init(&engine->smps_on_wire);
	engine->cfg = cfg;
	return (0);

eio_close:
	umad_close_port(engine->umad_fd);
	return (-EIO);
}

void smp_engine_destroy(smp_engine_t * engine)
{
	cl_map_item_t *item;
	ibnd_smp_t *smp;

	/* remove queued smps */
	smp = get_smp(engine);
	if (smp)
		IBND_ERROR("outstanding SMP's\n");
	for ( /* */ ; smp; smp = get_smp(engine))
		free(smp);

	/* remove smps from the wire queue */
	item = cl_qmap_head(&engine->smps_on_wire);
	if (item != cl_qmap_end(&engine->smps_on_wire))
		IBND_ERROR("outstanding SMP's on wire\n");
	for ( /* */ ; item != cl_qmap_end(&engine->smps_on_wire);
	     item = cl_qmap_head(&engine->smps_on_wire)) {
		cl_qmap_remove_item(&engine->smps_on_wire, item);
		free(item);
	}

	umad_close_port(engine->umad_fd);
}

int process_mads(smp_engine_t * engine)
{
	int rc;
	while (!cl_is_qmap_empty(&engine->smps_on_wire))
		if ((rc = process_one_recv(engine)) != 0)
			return rc;
	return 0;
}
