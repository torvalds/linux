/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *
 */

/*
 * This file contains OPA Virtual Network Interface Controller (VNIC)
 * Ethernet Management Agent (EMA) driver
 */

#include <linux/module.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_verbs.h>
#include <rdma/opa_smi.h>
#include <rdma/opa_port_info.h>

#include "opa_vnic_internal.h"

#define DRV_VERSION "1.0"
char opa_vnic_driver_name[] = "opa_vnic";
const char opa_vnic_driver_version[] = DRV_VERSION;

/*
 * The trap service level is kept in bits 3 to 7 in the trap_sl_rsvd
 * field in the class port info MAD.
 */
#define GET_TRAP_SL_FROM_CLASS_PORT_INFO(x)  (((x) >> 3) & 0x1f)

/* Cap trap bursts to a reasonable limit good for normal cases */
#define OPA_VNIC_TRAP_BURST_LIMIT 4

/*
 * VNIC trap limit timeout.
 * Inverse of cap2_mask response time out (1.0737 secs) = 0.9
 * secs approx IB spec 13.4.6.2.1 PortInfoSubnetTimeout and
 * 13.4.9 Traps.
 */
#define OPA_VNIC_TRAP_TIMEOUT  ((4096 * (1UL << 18)) / 1000)

#define OPA_VNIC_UNSUP_ATTR  \
		cpu_to_be16(IB_MGMT_MAD_STATUS_UNSUPPORTED_METHOD_ATTRIB)

#define OPA_VNIC_INVAL_ATTR  \
		cpu_to_be16(IB_MGMT_MAD_STATUS_INVALID_ATTRIB_VALUE)

#define OPA_VNIC_CLASS_CAP_TRAP   0x1

/* Maximum number of VNIC ports supported */
#define OPA_VNIC_MAX_NUM_VPORT    255

/**
 * struct opa_vnic_vema_port -- VNIC VEMA port details
 * @cport: pointer to port
 * @mad_agent: pointer to mad agent for port
 * @class_port_info: Class port info information.
 * @tid: Transaction id
 * @port_num: OPA port number
 * @vport_idr: vnic ports idr
 * @event_handler: ib event handler
 * @lock: adapter interface lock
 */
struct opa_vnic_vema_port {
	struct opa_vnic_ctrl_port      *cport;
	struct ib_mad_agent            *mad_agent;
	struct opa_class_port_info      class_port_info;
	u64                             tid;
	u8                              port_num;
	struct idr                      vport_idr;
	struct ib_event_handler         event_handler;

	/* Lock to query/update network adapter */
	struct mutex                    lock;
};

static void opa_vnic_vema_add_one(struct ib_device *device);
static void opa_vnic_vema_rem_one(struct ib_device *device,
				  void *client_data);

static struct ib_client opa_vnic_client = {
	.name   = opa_vnic_driver_name,
	.add    = opa_vnic_vema_add_one,
	.remove = opa_vnic_vema_rem_one,
};

/**
 * vema_get_vport_num -- Get the vnic from the mad
 * @recvd_mad:  Received mad
 *
 * Return: returns value of the vnic port number
 */
static inline u8 vema_get_vport_num(struct opa_vnic_vema_mad *recvd_mad)
{
	return be32_to_cpu(recvd_mad->mad_hdr.attr_mod) & 0xff;
}

/**
 * vema_get_vport_adapter -- Get vnic port adapter from recvd mad
 * @recvd_mad: received mad
 * @port: ptr to port struct on which MAD was recvd
 *
 * Return: vnic adapter
 */
static inline struct opa_vnic_adapter *
vema_get_vport_adapter(struct opa_vnic_vema_mad *recvd_mad,
		       struct opa_vnic_vema_port *port)
{
	u8 vport_num = vema_get_vport_num(recvd_mad);

	return idr_find(&port->vport_idr, vport_num);
}

/**
 * vema_mac_tbl_req_ok -- Check if mac request has correct values
 * @mac_tbl: mac table
 *
 * This function checks for the validity of the offset and number of
 * entries required.
 *
 * Return: true if offset and num_entries are valid
 */
static inline bool vema_mac_tbl_req_ok(struct opa_veswport_mactable *mac_tbl)
{
	u16 offset, num_entries;
	u16 req_entries = ((OPA_VNIC_EMA_DATA - sizeof(*mac_tbl)) /
			   sizeof(mac_tbl->tbl_entries[0]));

	offset = be16_to_cpu(mac_tbl->offset);
	num_entries = be16_to_cpu(mac_tbl->num_entries);

	return ((num_entries <= req_entries) &&
		(offset + num_entries <= OPA_VNIC_MAC_TBL_MAX_ENTRIES));
}

/*
 * Return the power on default values in the port info structure
 * in big endian format as required by MAD.
 */
static inline void vema_get_pod_values(struct opa_veswport_info *port_info)
{
	memset(port_info, 0, sizeof(*port_info));
	port_info->vport.max_mac_tbl_ent =
		cpu_to_be16(OPA_VNIC_MAC_TBL_MAX_ENTRIES);
	port_info->vport.max_smac_ent =
		cpu_to_be16(OPA_VNIC_MAX_SMAC_LIMIT);
	port_info->vport.oper_state = OPA_VNIC_STATE_DROP_ALL;
	port_info->vport.config_state = OPA_VNIC_STATE_DROP_ALL;
	port_info->vesw.eth_mtu = cpu_to_be16(ETH_DATA_LEN);
}

/**
 * vema_add_vport -- Add a new vnic port
 * @port: ptr to opa_vnic_vema_port struct
 * @vport_num: vnic port number (to be added)
 *
 * Return a pointer to the vnic adapter structure
 */
static struct opa_vnic_adapter *vema_add_vport(struct opa_vnic_vema_port *port,
					       u8 vport_num)
{
	struct opa_vnic_ctrl_port *cport = port->cport;
	struct opa_vnic_adapter *adapter;

	adapter = opa_vnic_add_netdev(cport->ibdev, port->port_num, vport_num);
	if (!IS_ERR(adapter)) {
		int rc;

		adapter->cport = cport;
		rc = idr_alloc(&port->vport_idr, adapter, vport_num,
			       vport_num + 1, GFP_NOWAIT);
		if (rc < 0) {
			opa_vnic_rem_netdev(adapter);
			adapter = ERR_PTR(rc);
		}
	}

	return adapter;
}

/**
 * vema_get_class_port_info -- Get class info for port
 * @port:  Port on whic MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function copies the latest class port info value set for the
 * port and stores it for generating traps
 */
static void vema_get_class_port_info(struct opa_vnic_vema_port *port,
				     struct opa_vnic_vema_mad *recvd_mad,
				     struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_class_port_info *port_info;

	port_info = (struct opa_class_port_info *)rsp_mad->data;
	memcpy(port_info, &port->class_port_info, sizeof(*port_info));
	port_info->base_version = OPA_MGMT_BASE_VERSION,
	port_info->class_version = OPA_EMA_CLASS_VERSION;

	/*
	 * Set capability mask bit indicating agent generates traps,
	 * and set the maximum number of VNIC ports supported.
	 */
	port_info->cap_mask = cpu_to_be16((OPA_VNIC_CLASS_CAP_TRAP |
					   (OPA_VNIC_MAX_NUM_VPORT << 8)));

	/*
	 * Since a get routine is always sent by the EM first we
	 * set the expected response time to
	 * 4.096 usec * 2^18 == 1.0737 sec here.
	 */
	port_info->cap_mask2_resp_time = cpu_to_be32(18);
}

/**
 * vema_set_class_port_info -- Get class info for port
 * @port:  Port on whic MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function updates the port class info for the specific vnic
 * and sets up the response mad data
 */
static void vema_set_class_port_info(struct opa_vnic_vema_port *port,
				     struct opa_vnic_vema_mad *recvd_mad,
				     struct opa_vnic_vema_mad *rsp_mad)
{
	memcpy(&port->class_port_info, recvd_mad->data,
	       sizeof(port->class_port_info));

	vema_get_class_port_info(port, recvd_mad, rsp_mad);
}

/**
 * vema_get_veswport_info -- Get veswport info
 * @port:      source port on which MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 */
static void vema_get_veswport_info(struct opa_vnic_vema_port *port,
				   struct opa_vnic_vema_mad *recvd_mad,
				   struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_info *port_info =
				  (struct opa_veswport_info *)rsp_mad->data;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (adapter) {
		memset(port_info, 0, sizeof(*port_info));
		opa_vnic_get_vesw_info(adapter, &port_info->vesw);
		opa_vnic_get_per_veswport_info(adapter,
					       &port_info->vport);
	} else {
		vema_get_pod_values(port_info);
	}
}

/**
 * vema_set_veswport_info -- Set veswport info
 * @port:      source port on which MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function gets the port class infor for vnic
 */
static void vema_set_veswport_info(struct opa_vnic_vema_port *port,
				   struct opa_vnic_vema_mad *recvd_mad,
				   struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_vnic_ctrl_port *cport = port->cport;
	struct opa_veswport_info *port_info;
	struct opa_vnic_adapter *adapter;
	u8 vport_num;

	vport_num = vema_get_vport_num(recvd_mad);

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (!adapter) {
		adapter = vema_add_vport(port, vport_num);
		if (IS_ERR(adapter)) {
			c_err("failed to add vport %d: %ld\n",
			      vport_num, PTR_ERR(adapter));
			goto err_exit;
		}
	}

	port_info = (struct opa_veswport_info *)recvd_mad->data;
	opa_vnic_set_vesw_info(adapter, &port_info->vesw);
	opa_vnic_set_per_veswport_info(adapter, &port_info->vport);

	/* Process the new config settings */
	opa_vnic_process_vema_config(adapter);

	vema_get_veswport_info(port, recvd_mad, rsp_mad);
	return;

err_exit:
	rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
}

/**
 * vema_get_mac_entries -- Get MAC entries in VNIC MAC table
 * @port:      source port on which MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function gets the MAC entries that are programmed into
 * the VNIC MAC forwarding table. It checks for the validity of
 * the index into the MAC table and the number of entries that
 * are to be retrieved.
 */
static void vema_get_mac_entries(struct opa_vnic_vema_port *port,
				 struct opa_vnic_vema_mad *recvd_mad,
				 struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_mactable *mac_tbl_in, *mac_tbl_out;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (!adapter) {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
		return;
	}

	mac_tbl_in = (struct opa_veswport_mactable *)recvd_mad->data;
	mac_tbl_out = (struct opa_veswport_mactable *)rsp_mad->data;

	if (vema_mac_tbl_req_ok(mac_tbl_in)) {
		mac_tbl_out->offset = mac_tbl_in->offset;
		mac_tbl_out->num_entries = mac_tbl_in->num_entries;
		opa_vnic_query_mac_tbl(adapter, mac_tbl_out);
	} else {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
	}
}

/**
 * vema_set_mac_entries -- Set MAC entries in VNIC MAC table
 * @port:      source port on which MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function sets the MAC entries in the VNIC forwarding table
 * It checks for the validity of the index and the number of forwarding
 * table entries to be programmed.
 */
static void vema_set_mac_entries(struct opa_vnic_vema_port *port,
				 struct opa_vnic_vema_mad *recvd_mad,
				 struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_mactable *mac_tbl;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (!adapter) {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
		return;
	}

	mac_tbl = (struct opa_veswport_mactable *)recvd_mad->data;
	if (vema_mac_tbl_req_ok(mac_tbl)) {
		if (opa_vnic_update_mac_tbl(adapter, mac_tbl))
			rsp_mad->mad_hdr.status = OPA_VNIC_UNSUP_ATTR;
	} else {
		rsp_mad->mad_hdr.status = OPA_VNIC_UNSUP_ATTR;
	}
	vema_get_mac_entries(port, recvd_mad, rsp_mad);
}

/**
 * vema_set_delete_vesw -- Reset VESW info to POD values
 * @port:      source port on which MAD was received
 * @recvd_mad: pointer to the received mad
 * @rsp_mad:   pointer to respose mad
 *
 * This function clears all the fields of veswport info for the requested vesw
 * and sets them back to the power-on default values. It does not delete the
 * vesw.
 */
static void vema_set_delete_vesw(struct opa_vnic_vema_port *port,
				 struct opa_vnic_vema_mad *recvd_mad,
				 struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_info *port_info =
				  (struct opa_veswport_info *)rsp_mad->data;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (!adapter) {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
		return;
	}

	vema_get_pod_values(port_info);
	opa_vnic_set_vesw_info(adapter, &port_info->vesw);
	opa_vnic_set_per_veswport_info(adapter, &port_info->vport);

	/* Process the new config settings */
	opa_vnic_process_vema_config(adapter);

	opa_vnic_release_mac_tbl(adapter);

	vema_get_veswport_info(port, recvd_mad, rsp_mad);
}

/**
 * vema_get_mac_list -- Get the unicast/multicast macs.
 * @port:      source port on which MAD was received
 * @recvd_mad: Received mad contains fields to set vnic parameters
 * @rsp_mad:   Response mad to be built
 * @attr_id:   Attribute ID indicating multicast or unicast mac list
 */
static void vema_get_mac_list(struct opa_vnic_vema_port *port,
			      struct opa_vnic_vema_mad *recvd_mad,
			      struct opa_vnic_vema_mad *rsp_mad,
			      u16 attr_id)
{
	struct opa_veswport_iface_macs *macs_in, *macs_out;
	int max_entries = (OPA_VNIC_EMA_DATA - sizeof(*macs_out)) / ETH_ALEN;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (!adapter) {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
		return;
	}

	macs_in = (struct opa_veswport_iface_macs *)recvd_mad->data;
	macs_out = (struct opa_veswport_iface_macs *)rsp_mad->data;

	macs_out->start_idx = macs_in->start_idx;
	if (macs_in->num_macs_in_msg)
		macs_out->num_macs_in_msg = macs_in->num_macs_in_msg;
	else
		macs_out->num_macs_in_msg = cpu_to_be16(max_entries);

	if (attr_id == OPA_EM_ATTR_IFACE_MCAST_MACS)
		opa_vnic_query_mcast_macs(adapter, macs_out);
	else
		opa_vnic_query_ucast_macs(adapter, macs_out);
}

/**
 * vema_get_summary_counters -- Gets summary counters.
 * @port:      source port on which MAD was received
 * @recvd_mad: Received mad contains fields to set vnic parameters
 * @rsp_mad:   Response mad to be built
 */
static void vema_get_summary_counters(struct opa_vnic_vema_port *port,
				      struct opa_vnic_vema_mad *recvd_mad,
				      struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_summary_counters *cntrs;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (adapter) {
		cntrs = (struct opa_veswport_summary_counters *)rsp_mad->data;
		opa_vnic_get_summary_counters(adapter, cntrs);
	} else {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
	}
}

/**
 * vema_get_error_counters -- Gets summary counters.
 * @port:      source port on which MAD was received
 * @recvd_mad: Received mad contains fields to set vnic parameters
 * @rsp_mad:   Response mad to be built
 */
static void vema_get_error_counters(struct opa_vnic_vema_port *port,
				    struct opa_vnic_vema_mad *recvd_mad,
				    struct opa_vnic_vema_mad *rsp_mad)
{
	struct opa_veswport_error_counters *cntrs;
	struct opa_vnic_adapter *adapter;

	adapter = vema_get_vport_adapter(recvd_mad, port);
	if (adapter) {
		cntrs = (struct opa_veswport_error_counters *)rsp_mad->data;
		opa_vnic_get_error_counters(adapter, cntrs);
	} else {
		rsp_mad->mad_hdr.status = OPA_VNIC_INVAL_ATTR;
	}
}

/**
 * vema_get -- Process received get MAD
 * @port:      source port on which MAD was received
 * @recvd_mad: Received mad
 * @rsp_mad:   Response mad to be built
 */
static void vema_get(struct opa_vnic_vema_port *port,
		     struct opa_vnic_vema_mad *recvd_mad,
		     struct opa_vnic_vema_mad *rsp_mad)
{
	u16 attr_id = be16_to_cpu(recvd_mad->mad_hdr.attr_id);

	switch (attr_id) {
	case OPA_EM_ATTR_CLASS_PORT_INFO:
		vema_get_class_port_info(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_VESWPORT_INFO:
		vema_get_veswport_info(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_VESWPORT_MAC_ENTRIES:
		vema_get_mac_entries(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_IFACE_UCAST_MACS:
		/* fall through */
	case OPA_EM_ATTR_IFACE_MCAST_MACS:
		vema_get_mac_list(port, recvd_mad, rsp_mad, attr_id);
		break;
	case OPA_EM_ATTR_VESWPORT_SUMMARY_COUNTERS:
		vema_get_summary_counters(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_VESWPORT_ERROR_COUNTERS:
		vema_get_error_counters(port, recvd_mad, rsp_mad);
		break;
	default:
		rsp_mad->mad_hdr.status = OPA_VNIC_UNSUP_ATTR;
		break;
	}
}

/**
 * vema_set -- Process received set MAD
 * @port:      source port on which MAD was received
 * @recvd_mad: Received mad contains fields to set vnic parameters
 * @rsp_mad:   Response mad to be built
 */
static void vema_set(struct opa_vnic_vema_port *port,
		     struct opa_vnic_vema_mad *recvd_mad,
		     struct opa_vnic_vema_mad *rsp_mad)
{
	u16 attr_id = be16_to_cpu(recvd_mad->mad_hdr.attr_id);

	switch (attr_id) {
	case OPA_EM_ATTR_CLASS_PORT_INFO:
		vema_set_class_port_info(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_VESWPORT_INFO:
		vema_set_veswport_info(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_VESWPORT_MAC_ENTRIES:
		vema_set_mac_entries(port, recvd_mad, rsp_mad);
		break;
	case OPA_EM_ATTR_DELETE_VESW:
		vema_set_delete_vesw(port, recvd_mad, rsp_mad);
		break;
	default:
		rsp_mad->mad_hdr.status = OPA_VNIC_UNSUP_ATTR;
		break;
	}
}

/**
 * vema_send -- Send handler for VEMA MAD agent
 * @mad_agent: pointer to the mad agent
 * @mad_wc:    pointer to mad send work completion information
 *
 * Free all the data structures associated with the sent MAD
 */
static void vema_send(struct ib_mad_agent *mad_agent,
		      struct ib_mad_send_wc *mad_wc)
{
	rdma_destroy_ah(mad_wc->send_buf->ah, RDMA_DESTROY_AH_SLEEPABLE);
	ib_free_send_mad(mad_wc->send_buf);
}

/**
 * vema_recv -- Recv handler for VEMA MAD agent
 * @mad_agent: pointer to the mad agent
 * @send_buf: Send buffer if found, else NULL
 * @mad_wc:    pointer to mad send work completion information
 *
 * Handle only set and get methods and respond to other methods
 * as unsupported. Allocate response buffer and address handle
 * for the response MAD.
 */
static void vema_recv(struct ib_mad_agent *mad_agent,
		      struct ib_mad_send_buf *send_buf,
		      struct ib_mad_recv_wc *mad_wc)
{
	struct opa_vnic_vema_port *port;
	struct ib_ah              *ah;
	struct ib_mad_send_buf    *rsp;
	struct opa_vnic_vema_mad  *vema_mad;

	if (!mad_wc || !mad_wc->recv_buf.mad)
		return;

	port = mad_agent->context;
	ah = ib_create_ah_from_wc(mad_agent->qp->pd, mad_wc->wc,
				  mad_wc->recv_buf.grh, mad_agent->port_num);
	if (IS_ERR(ah))
		goto free_recv_mad;

	rsp = ib_create_send_mad(mad_agent, mad_wc->wc->src_qp,
				 mad_wc->wc->pkey_index, 0,
				 IB_MGMT_VENDOR_HDR, OPA_VNIC_EMA_DATA,
				 GFP_KERNEL, OPA_MGMT_BASE_VERSION);
	if (IS_ERR(rsp))
		goto err_rsp;

	rsp->ah = ah;
	vema_mad = rsp->mad;
	memcpy(vema_mad, mad_wc->recv_buf.mad, IB_MGMT_VENDOR_HDR);
	vema_mad->mad_hdr.method = IB_MGMT_METHOD_GET_RESP;
	vema_mad->mad_hdr.status = 0;

	/* Lock ensures network adapter is not removed */
	mutex_lock(&port->lock);

	switch (mad_wc->recv_buf.mad->mad_hdr.method) {
	case IB_MGMT_METHOD_GET:
		vema_get(port, (struct opa_vnic_vema_mad *)mad_wc->recv_buf.mad,
			 vema_mad);
		break;
	case IB_MGMT_METHOD_SET:
		vema_set(port, (struct opa_vnic_vema_mad *)mad_wc->recv_buf.mad,
			 vema_mad);
		break;
	default:
		vema_mad->mad_hdr.status = OPA_VNIC_UNSUP_ATTR;
		break;
	}
	mutex_unlock(&port->lock);

	if (!ib_post_send_mad(rsp, NULL)) {
		/*
		 * with post send successful ah and send mad
		 * will be destroyed in send handler
		 */
		goto free_recv_mad;
	}

	ib_free_send_mad(rsp);

err_rsp:
	rdma_destroy_ah(ah, RDMA_DESTROY_AH_SLEEPABLE);
free_recv_mad:
	ib_free_recv_mad(mad_wc);
}

/**
 * vema_get_port -- Gets the opa_vnic_vema_port
 * @cport: pointer to control dev
 * @port_num: Port number
 *
 * This function loops through the ports and returns
 * the opa_vnic_vema port structure that is associated
 * with the OPA port number
 *
 * Return: ptr to requested opa_vnic_vema_port strucure
 *         if success, NULL if not
 */
static struct opa_vnic_vema_port *
vema_get_port(struct opa_vnic_ctrl_port *cport, u8 port_num)
{
	struct opa_vnic_vema_port *port = (void *)cport + sizeof(*cport);

	if (port_num > cport->num_ports)
		return NULL;

	return port + (port_num - 1);
}

/**
 * opa_vnic_vema_send_trap -- This function sends a trap to the EM
 * @adapter: pointer to vnic adapter
 * @data: pointer to trap data filled by calling function
 * @lid:  issuers lid (encap_slid from vesw_port_info)
 *
 * This function is called from the VNIC driver to send a trap if there
 * is somethng the EM should be notified about. These events currently
 * are
 * 1) UNICAST INTERFACE MACADDRESS changes
 * 2) MULTICAST INTERFACE MACADDRESS changes
 * 3) ETHERNET LINK STATUS changes
 * While allocating the send mad the remote site qpn used is 1
 * as this is the well known QP.
 *
 */
void opa_vnic_vema_send_trap(struct opa_vnic_adapter *adapter,
			     struct __opa_veswport_trap *data, u32 lid)
{
	struct opa_vnic_ctrl_port *cport = adapter->cport;
	struct ib_mad_send_buf *send_buf;
	struct opa_vnic_vema_port *port;
	struct ib_device *ibp;
	struct opa_vnic_vema_mad_trap *trap_mad;
	struct opa_class_port_info *class;
	struct rdma_ah_attr ah_attr;
	struct ib_ah *ah;
	struct opa_veswport_trap *trap;
	u32 trap_lid;
	u16 pkey_idx;

	if (!cport)
		goto err_exit;
	ibp = cport->ibdev;
	port = vema_get_port(cport, data->opaportnum);
	if (!port || !port->mad_agent)
		goto err_exit;

	if (time_before(jiffies, adapter->trap_timeout)) {
		if (adapter->trap_count == OPA_VNIC_TRAP_BURST_LIMIT) {
			v_warn("Trap rate exceeded\n");
			goto err_exit;
		} else {
			adapter->trap_count++;
		}
	} else {
		adapter->trap_count = 0;
	}

	class = &port->class_port_info;
	/* Set up address handle */
	memset(&ah_attr, 0, sizeof(ah_attr));
	ah_attr.type = rdma_ah_find_type(ibp, port->port_num);
	rdma_ah_set_sl(&ah_attr,
		       GET_TRAP_SL_FROM_CLASS_PORT_INFO(class->trap_sl_rsvd));
	rdma_ah_set_port_num(&ah_attr, port->port_num);
	trap_lid = be32_to_cpu(class->trap_lid);
	/*
	 * check for trap lid validity, must not be zero
	 * The trap sink could change after we fashion the MAD but since traps
	 * are not guaranteed we won't use a lock as anyway the change will take
	 * place even with locking.
	 */
	if (!trap_lid) {
		c_err("%s: Invalid dlid\n", __func__);
		goto err_exit;
	}

	rdma_ah_set_dlid(&ah_attr, trap_lid);
	ah = rdma_create_ah(port->mad_agent->qp->pd, &ah_attr, 0);
	if (IS_ERR(ah)) {
		c_err("%s:Couldn't create new AH = %p\n", __func__, ah);
		c_err("%s:dlid = %d, sl = %d, port = %d\n", __func__,
		      rdma_ah_get_dlid(&ah_attr), rdma_ah_get_sl(&ah_attr),
		      rdma_ah_get_port_num(&ah_attr));
		goto err_exit;
	}

	if (ib_find_pkey(ibp, data->opaportnum, IB_DEFAULT_PKEY_FULL,
			 &pkey_idx) < 0) {
		c_err("%s:full key not found, defaulting to partial\n",
		      __func__);
		if (ib_find_pkey(ibp, data->opaportnum, IB_DEFAULT_PKEY_PARTIAL,
				 &pkey_idx) < 0)
			pkey_idx = 1;
	}

	send_buf = ib_create_send_mad(port->mad_agent, 1, pkey_idx, 0,
				      IB_MGMT_VENDOR_HDR, IB_MGMT_MAD_DATA,
				      GFP_ATOMIC, OPA_MGMT_BASE_VERSION);
	if (IS_ERR(send_buf)) {
		c_err("%s:Couldn't allocate send buf\n", __func__);
		goto err_sndbuf;
	}

	send_buf->ah = ah;

	/* Set up common MAD hdr */
	trap_mad = send_buf->mad;
	trap_mad->mad_hdr.base_version = OPA_MGMT_BASE_VERSION;
	trap_mad->mad_hdr.mgmt_class = OPA_MGMT_CLASS_INTEL_EMA;
	trap_mad->mad_hdr.class_version = OPA_EMA_CLASS_VERSION;
	trap_mad->mad_hdr.method = IB_MGMT_METHOD_TRAP;
	port->tid++;
	trap_mad->mad_hdr.tid = cpu_to_be64(port->tid);
	trap_mad->mad_hdr.attr_id = IB_SMP_ATTR_NOTICE;

	/* Set up vendor OUI */
	trap_mad->oui[0] = INTEL_OUI_1;
	trap_mad->oui[1] = INTEL_OUI_2;
	trap_mad->oui[2] = INTEL_OUI_3;

	/* Setup notice attribute portion */
	trap_mad->notice.gen_type = OPA_INTEL_EMA_NOTICE_TYPE_INFO << 1;
	trap_mad->notice.oui_1 = INTEL_OUI_1;
	trap_mad->notice.oui_2 = INTEL_OUI_2;
	trap_mad->notice.oui_3 = INTEL_OUI_3;
	trap_mad->notice.issuer_lid = cpu_to_be32(lid);

	/* copy the actual trap data */
	trap = (struct opa_veswport_trap *)trap_mad->notice.raw_data;
	trap->fabric_id = cpu_to_be16(data->fabric_id);
	trap->veswid = cpu_to_be16(data->veswid);
	trap->veswportnum = cpu_to_be32(data->veswportnum);
	trap->opaportnum = cpu_to_be16(data->opaportnum);
	trap->veswportindex = data->veswportindex;
	trap->opcode = data->opcode;

	/* If successful send set up rate limit timeout else bail */
	if (ib_post_send_mad(send_buf, NULL)) {
		ib_free_send_mad(send_buf);
	} else {
		if (adapter->trap_count)
			return;
		adapter->trap_timeout = jiffies +
					usecs_to_jiffies(OPA_VNIC_TRAP_TIMEOUT);
		return;
	}

err_sndbuf:
	rdma_destroy_ah(ah, 0);
err_exit:
	v_err("Aborting trap\n");
}

static int vema_rem_vport(int id, void *p, void *data)
{
	struct opa_vnic_adapter *adapter = p;

	opa_vnic_rem_netdev(adapter);
	return 0;
}

static int vema_enable_vport(int id, void *p, void *data)
{
	struct opa_vnic_adapter *adapter = p;

	netif_carrier_on(adapter->netdev);
	return 0;
}

static int vema_disable_vport(int id, void *p, void *data)
{
	struct opa_vnic_adapter *adapter = p;

	netif_carrier_off(adapter->netdev);
	return 0;
}

static void opa_vnic_event(struct ib_event_handler *handler,
			   struct ib_event *record)
{
	struct opa_vnic_vema_port *port =
		container_of(handler, struct opa_vnic_vema_port, event_handler);
	struct opa_vnic_ctrl_port *cport = port->cport;

	if (record->element.port_num != port->port_num)
		return;

	c_dbg("OPA_VNIC received event %d on device %s port %d\n",
	      record->event, dev_name(&record->device->dev),
	      record->element.port_num);

	if (record->event == IB_EVENT_PORT_ERR)
		idr_for_each(&port->vport_idr, vema_disable_vport, NULL);
	if (record->event == IB_EVENT_PORT_ACTIVE)
		idr_for_each(&port->vport_idr, vema_enable_vport, NULL);
}

/**
 * vema_unregister -- Unregisters agent
 * @cport: pointer to control port
 *
 * This deletes the registration by VEMA for MADs
 */
static void vema_unregister(struct opa_vnic_ctrl_port *cport)
{
	int i;

	for (i = 1; i <= cport->num_ports; i++) {
		struct opa_vnic_vema_port *port = vema_get_port(cport, i);

		if (!port->mad_agent)
			continue;

		/* Lock ensures no MAD is being processed */
		mutex_lock(&port->lock);
		idr_for_each(&port->vport_idr, vema_rem_vport, NULL);
		mutex_unlock(&port->lock);

		ib_unregister_mad_agent(port->mad_agent);
		port->mad_agent = NULL;
		mutex_destroy(&port->lock);
		idr_destroy(&port->vport_idr);
		ib_unregister_event_handler(&port->event_handler);
	}
}

/**
 * vema_register -- Registers agent
 * @cport: pointer to control port
 *
 * This function registers the handlers for the VEMA MADs
 *
 * Return: returns 0 on success. non zero otherwise
 */
static int vema_register(struct opa_vnic_ctrl_port *cport)
{
	struct ib_mad_reg_req reg_req = {
		.mgmt_class = OPA_MGMT_CLASS_INTEL_EMA,
		.mgmt_class_version = OPA_MGMT_BASE_VERSION,
		.oui = { INTEL_OUI_1, INTEL_OUI_2, INTEL_OUI_3 }
	};
	int i;

	set_bit(IB_MGMT_METHOD_GET, reg_req.method_mask);
	set_bit(IB_MGMT_METHOD_SET, reg_req.method_mask);

	/* register ib event handler and mad agent for each port on dev */
	for (i = 1; i <= cport->num_ports; i++) {
		struct opa_vnic_vema_port *port = vema_get_port(cport, i);
		int ret;

		port->cport = cport;
		port->port_num = i;

		INIT_IB_EVENT_HANDLER(&port->event_handler,
				      cport->ibdev, opa_vnic_event);
		ib_register_event_handler(&port->event_handler);

		idr_init(&port->vport_idr);
		mutex_init(&port->lock);
		port->mad_agent = ib_register_mad_agent(cport->ibdev, i,
							IB_QPT_GSI, &reg_req,
							IB_MGMT_RMPP_VERSION,
							vema_send, vema_recv,
							port, 0);
		if (IS_ERR(port->mad_agent)) {
			ret = PTR_ERR(port->mad_agent);
			port->mad_agent = NULL;
			mutex_destroy(&port->lock);
			idr_destroy(&port->vport_idr);
			vema_unregister(cport);
			return ret;
		}
	}

	return 0;
}

/**
 * opa_vnic_ctrl_config_dev -- This function sends a trap to the EM
 * by way of ib_modify_port to indicate support for ethernet on the
 * fabric.
 * @cport: pointer to control port
 * @en: enable or disable ethernet on fabric support
 */
static void opa_vnic_ctrl_config_dev(struct opa_vnic_ctrl_port *cport, bool en)
{
	struct ib_port_modify pm = { 0 };
	int i;

	if (en)
		pm.set_port_cap_mask = OPA_CAP_MASK3_IsEthOnFabricSupported;
	else
		pm.clr_port_cap_mask = OPA_CAP_MASK3_IsEthOnFabricSupported;

	for (i = 1; i <= cport->num_ports; i++)
		ib_modify_port(cport->ibdev, i, IB_PORT_OPA_MASK_CHG, &pm);
}

/**
 * opa_vnic_vema_add_one -- Handle new ib device
 * @device: ib device pointer
 *
 * Allocate the vnic control port and initialize it.
 */
static void opa_vnic_vema_add_one(struct ib_device *device)
{
	struct opa_vnic_ctrl_port *cport;
	int rc, size = sizeof(*cport);

	if (!rdma_cap_opa_vnic(device))
		return;

	size += device->phys_port_cnt * sizeof(struct opa_vnic_vema_port);
	cport = kzalloc(size, GFP_KERNEL);
	if (!cport)
		return;

	cport->num_ports = device->phys_port_cnt;
	cport->ibdev = device;

	/* Initialize opa vnic management agent (vema) */
	rc = vema_register(cport);
	if (!rc)
		c_info("VNIC client initialized\n");

	ib_set_client_data(device, &opa_vnic_client, cport);
	opa_vnic_ctrl_config_dev(cport, true);
}

/**
 * opa_vnic_vema_rem_one -- Handle ib device removal
 * @device: ib device pointer
 * @client_data: ib client data
 *
 * Uninitialize and free the vnic control port.
 */
static void opa_vnic_vema_rem_one(struct ib_device *device,
				  void *client_data)
{
	struct opa_vnic_ctrl_port *cport = client_data;

	if (!cport)
		return;

	c_info("removing VNIC client\n");
	opa_vnic_ctrl_config_dev(cport, false);
	vema_unregister(cport);
	kfree(cport);
}

static int __init opa_vnic_init(void)
{
	int rc;

	pr_info("OPA Virtual Network Driver - v%s\n",
		opa_vnic_driver_version);

	rc = ib_register_client(&opa_vnic_client);
	if (rc)
		pr_err("VNIC driver register failed %d\n", rc);

	return rc;
}
module_init(opa_vnic_init);

static void opa_vnic_deinit(void)
{
	ib_unregister_client(&opa_vnic_client);
}
module_exit(opa_vnic_deinit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel OPA Virtual Network driver");
