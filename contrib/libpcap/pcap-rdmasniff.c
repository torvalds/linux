/*
 * Copyright (c) 2017 Pure Storage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pcap-int.h"
#include "pcap-rdmasniff.h"

#include <infiniband/verbs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if !defined(IBV_FLOW_ATTR_SNIFFER)
#define IBV_FLOW_ATTR_SNIFFER	3
#endif

static const int RDMASNIFF_NUM_RECEIVES = 128;
static const int RDMASNIFF_RECEIVE_SIZE = 10000;

struct pcap_rdmasniff {
	struct ibv_device *		rdma_device;
	struct ibv_context *		context;
	struct ibv_comp_channel *	channel;
	struct ibv_pd *			pd;
	struct ibv_cq *			cq;
	struct ibv_qp *			qp;
	struct ibv_flow *               flow;
	struct ibv_mr *			mr;
	u_char *			oneshot_buffer;
	unsigned			port_num;
	int                             cq_event;
	u_int                           packets_recv;
};

static int
rdmasniff_stats(pcap_t *handle, struct pcap_stat *stat)
{
	struct pcap_rdmasniff *priv = handle->priv;

	stat->ps_recv = priv->packets_recv;
	stat->ps_drop = 0;
	stat->ps_ifdrop = 0;

	return 0;
}

static void
rdmasniff_cleanup(pcap_t *handle)
{
	struct pcap_rdmasniff *priv = handle->priv;

	ibv_dereg_mr(priv->mr);
	ibv_destroy_flow(priv->flow);
	ibv_destroy_qp(priv->qp);
	ibv_destroy_cq(priv->cq);
	ibv_dealloc_pd(priv->pd);
	ibv_destroy_comp_channel(priv->channel);
	ibv_close_device(priv->context);
	free(priv->oneshot_buffer);

	pcap_cleanup_live_common(handle);
}

static void
rdmasniff_post_recv(pcap_t *handle, uint64_t wr_id)
{
	struct pcap_rdmasniff *priv = handle->priv;
	struct ibv_sge sg_entry;
	struct ibv_recv_wr wr, *bad_wr;

	sg_entry.length = RDMASNIFF_RECEIVE_SIZE;
	sg_entry.addr = (uintptr_t) handle->buffer + RDMASNIFF_RECEIVE_SIZE * wr_id;
	sg_entry.lkey = priv->mr->lkey;

	wr.wr_id = wr_id;
	wr.num_sge = 1;
	wr.sg_list = &sg_entry;
	wr.next = NULL;

	ibv_post_recv(priv->qp, &wr, &bad_wr);
}

static int
rdmasniff_read(pcap_t *handle, int max_packets, pcap_handler callback, u_char *user)
{
	struct pcap_rdmasniff *priv = handle->priv;
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	struct ibv_wc wc;
	struct pcap_pkthdr pkth;
	u_char *pktd;
	int count = 0;

	if (!priv->cq_event) {
		while (ibv_get_cq_event(priv->channel, &ev_cq, &ev_ctx) < 0) {
			if (errno != EINTR) {
				return PCAP_ERROR;
			}
			if (handle->break_loop) {
				handle->break_loop = 0;
				return PCAP_ERROR_BREAK;
			}
		}
		ibv_ack_cq_events(priv->cq, 1);
		ibv_req_notify_cq(priv->cq, 0);
		priv->cq_event = 1;
	}

	while (count < max_packets || PACKET_COUNT_IS_UNLIMITED(max_packets)) {
		if (ibv_poll_cq(priv->cq, 1, &wc) != 1) {
			priv->cq_event = 0;
			break;
		}

		if (wc.status != IBV_WC_SUCCESS) {
			fprintf(stderr, "failed WC wr_id %lld status %d/%s\n",
				(unsigned long long) wc.wr_id,
				wc.status, ibv_wc_status_str(wc.status));
			continue;
		}

		pkth.len = wc.byte_len;
		pkth.caplen = min(pkth.len, (u_int)handle->snapshot);
		gettimeofday(&pkth.ts, NULL);

		pktd = (u_char *) handle->buffer + wc.wr_id * RDMASNIFF_RECEIVE_SIZE;

		if (handle->fcode.bf_insns == NULL ||
		    bpf_filter(handle->fcode.bf_insns, pktd, pkth.len, pkth.caplen)) {
			callback(user, &pkth, pktd);
			++priv->packets_recv;
			++count;
		}

		rdmasniff_post_recv(handle, wc.wr_id);

		if (handle->break_loop) {
			handle->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
	}

	return count;
}

static void
rdmasniff_oneshot(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
{
	struct oneshot_userdata *sp = (struct oneshot_userdata *) user;
	pcap_t *handle = sp->pd;
	struct pcap_rdmasniff *priv = handle->priv;

	*sp->hdr = *h;
	memcpy(priv->oneshot_buffer, bytes, h->caplen);
	*sp->pkt = priv->oneshot_buffer;
}

static int
rdmasniff_activate(pcap_t *handle)
{
	struct pcap_rdmasniff *priv = handle->priv;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_flow_attr flow_attr;
	struct ibv_port_attr port_attr;
	int i;

	priv->context = ibv_open_device(priv->rdma_device);
	if (!priv->context) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to open device %s", handle->opt.device);
		goto error;
	}

	priv->pd = ibv_alloc_pd(priv->context);
	if (!priv->pd) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to alloc PD for device %s", handle->opt.device);
		goto error;
	}

	priv->channel = ibv_create_comp_channel(priv->context);
	if (!priv->channel) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to create comp channel for device %s", handle->opt.device);
		goto error;
	}

	priv->cq = ibv_create_cq(priv->context, RDMASNIFF_NUM_RECEIVES,
				 NULL, priv->channel, 0);
	if (!priv->cq) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to create CQ for device %s", handle->opt.device);
		goto error;
	}

	ibv_req_notify_cq(priv->cq, 0);

	memset(&qp_init_attr, 0, sizeof qp_init_attr);
	qp_init_attr.send_cq = qp_init_attr.recv_cq = priv->cq;
	qp_init_attr.cap.max_recv_wr = RDMASNIFF_NUM_RECEIVES;
	qp_init_attr.cap.max_recv_sge = 1;
	qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;
	priv->qp = ibv_create_qp(priv->pd, &qp_init_attr);
	if (!priv->qp) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to create QP for device %s", handle->opt.device);
		goto error;
	}

	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.port_num = priv->port_num;
	if (ibv_modify_qp(priv->qp, &qp_attr, IBV_QP_STATE | IBV_QP_PORT)) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to modify QP to INIT for device %s", handle->opt.device);
		goto error;
	}

	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_state = IBV_QPS_RTR;
	if (ibv_modify_qp(priv->qp, &qp_attr, IBV_QP_STATE)) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to modify QP to RTR for device %s", handle->opt.device);
		goto error;
	}

	memset(&flow_attr, 0, sizeof flow_attr);
	flow_attr.type = IBV_FLOW_ATTR_SNIFFER;
	flow_attr.size = sizeof flow_attr;
	flow_attr.port = priv->port_num;
	priv->flow = ibv_create_flow(priv->qp, &flow_attr);
	if (!priv->flow) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to create flow for device %s", handle->opt.device);
		goto error;
	}

	handle->bufsize = RDMASNIFF_NUM_RECEIVES * RDMASNIFF_RECEIVE_SIZE;
	handle->buffer = malloc(handle->bufsize);
	if (!handle->buffer) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to allocate receive buffer for device %s", handle->opt.device);
		goto error;
	}

	priv->oneshot_buffer = malloc(RDMASNIFF_RECEIVE_SIZE);
	if (!priv->oneshot_buffer) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to allocate oneshot buffer for device %s", handle->opt.device);
		goto error;
	}

	priv->mr = ibv_reg_mr(priv->pd, handle->buffer, handle->bufsize, IBV_ACCESS_LOCAL_WRITE);
	if (!priv->mr) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
			      "Failed to register MR for device %s", handle->opt.device);
		goto error;
	}


	for (i = 0; i < RDMASNIFF_NUM_RECEIVES; ++i) {
		rdmasniff_post_recv(handle, i);
	}

	if (!ibv_query_port(priv->context, priv->port_num, &port_attr) &&
	    port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND) {
		handle->linktype = DLT_INFINIBAND;
	} else {
		handle->linktype = DLT_EN10MB;
	}

	if (handle->snapshot <= 0 || handle->snapshot > RDMASNIFF_RECEIVE_SIZE)
		handle->snapshot = RDMASNIFF_RECEIVE_SIZE;

	handle->offset = 0;
	handle->read_op = rdmasniff_read;
	handle->stats_op = rdmasniff_stats;
	handle->cleanup_op = rdmasniff_cleanup;
	handle->setfilter_op = install_bpf_program;
	handle->setdirection_op = NULL;
	handle->set_datalink_op = NULL;
	handle->getnonblock_op = pcap_getnonblock_fd;
	handle->setnonblock_op = pcap_setnonblock_fd;
	handle->oneshot_callback = rdmasniff_oneshot;
	handle->selectable_fd = priv->channel->fd;

	return 0;

error:
	if (priv->mr) {
		ibv_dereg_mr(priv->mr);
	}

	if (priv->flow) {
		ibv_destroy_flow(priv->flow);
	}

	if (priv->qp) {
		ibv_destroy_qp(priv->qp);
	}

	if (priv->cq) {
		ibv_destroy_cq(priv->cq);
	}

	if (priv->channel) {
		ibv_destroy_comp_channel(priv->channel);
	}

	if (priv->pd) {
		ibv_dealloc_pd(priv->pd);
	}

	if (priv->context) {
		ibv_close_device(priv->context);
	}

	if (priv->oneshot_buffer) {
		free(priv->oneshot_buffer);
	}

	return PCAP_ERROR;
}

pcap_t *
rdmasniff_create(const char *device, char *ebuf, int *is_ours)
{
	struct pcap_rdmasniff *priv;
	struct ibv_device **dev_list;
	int numdev;
	size_t namelen;
	const char *port;
	unsigned port_num;
	int i;
	pcap_t *p = NULL;

	*is_ours = 0;

	dev_list = ibv_get_device_list(&numdev);
	if (!dev_list || !numdev) {
		return NULL;
	}

	namelen = strlen(device);

	port = strchr(device, ':');
	if (port) {
		port_num = strtoul(port + 1, NULL, 10);
		if (port_num > 0) {
			namelen = port - device;
		} else {
			port_num = 1;
		}
	} else {
		port_num = 1;
	}

	for (i = 0; i < numdev; ++i) {
		if (strlen(dev_list[i]->name) == namelen &&
		    !strncmp(device, dev_list[i]->name, namelen)) {
			*is_ours = 1;

			p = pcap_create_common(ebuf, sizeof (struct pcap_rdmasniff));
			if (p) {
				p->activate_op = rdmasniff_activate;
				priv = p->priv;
				priv->rdma_device = dev_list[i];
				priv->port_num = port_num;
			}
			break;
		}
	}

	ibv_free_device_list(dev_list);
	return p;
}

int
rdmasniff_findalldevs(pcap_if_list_t *devlistp, char *err_str)
{
	struct ibv_device **dev_list;
	int numdev;
	int i;
	int ret = 0;

	dev_list = ibv_get_device_list(&numdev);
	if (!dev_list || !numdev) {
		return 0;
	}

	for (i = 0; i < numdev; ++i) {
		/*
		 * XXX - do the notions of "up", "running", or
		 * "connected" apply here?
		 */
		if (!add_dev(devlistp, dev_list[i]->name, 0, "RDMA sniffer", err_str)) {
			ret = -1;
			goto out;
		}
	}

out:
	ibv_free_device_list(dev_list);
	return ret;
}
