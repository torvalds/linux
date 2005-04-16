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
 * $Id: ib_mad.h 1389 2004-12-27 22:56:47Z roland $
 */

#if !defined( IB_MAD_H )
#define IB_MAD_H

#include <ib_verbs.h>

/* Management base version */
#define IB_MGMT_BASE_VERSION			1

/* Management classes */
#define IB_MGMT_CLASS_SUBN_LID_ROUTED		0x01
#define IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE	0x81
#define IB_MGMT_CLASS_SUBN_ADM			0x03
#define IB_MGMT_CLASS_PERF_MGMT			0x04
#define IB_MGMT_CLASS_BM			0x05
#define IB_MGMT_CLASS_DEVICE_MGMT		0x06
#define IB_MGMT_CLASS_CM			0x07
#define IB_MGMT_CLASS_SNMP			0x08
#define IB_MGMT_CLASS_VENDOR_RANGE2_START	0x30
#define IB_MGMT_CLASS_VENDOR_RANGE2_END		0x4F

/* Management methods */
#define IB_MGMT_METHOD_GET			0x01
#define IB_MGMT_METHOD_SET			0x02
#define IB_MGMT_METHOD_GET_RESP			0x81
#define IB_MGMT_METHOD_SEND			0x03
#define IB_MGMT_METHOD_TRAP			0x05
#define IB_MGMT_METHOD_REPORT			0x06
#define IB_MGMT_METHOD_REPORT_RESP		0x86
#define IB_MGMT_METHOD_TRAP_REPRESS		0x07

#define IB_MGMT_METHOD_RESP			0x80

#define IB_MGMT_MAX_METHODS			128

#define IB_QP0		0
#define IB_QP1		__constant_htonl(1)
#define IB_QP1_QKEY	0x80010000

struct ib_grh {
	u32		version_tclass_flow;
	u16		paylen;
	u8		next_hdr;
	u8		hop_limit;
	union ib_gid	sgid;
	union ib_gid	dgid;
} __attribute__ ((packed));

struct ib_mad_hdr {
	u8	base_version;
	u8	mgmt_class;
	u8	class_version;
	u8	method;
	u16	status;
	u16	class_specific;
	u64	tid;
	u16	attr_id;
	u16	resv;
	u32	attr_mod;
} __attribute__ ((packed));

struct ib_rmpp_hdr {
	u8	rmpp_version;
	u8	rmpp_type;
	u8	rmpp_rtime_flags;
	u8	rmpp_status;
	u32	seg_num;
	u32	paylen_newwin;
} __attribute__ ((packed));

struct ib_mad {
	struct ib_mad_hdr	mad_hdr;
	u8			data[232];
} __attribute__ ((packed));

struct ib_rmpp_mad {
	struct ib_mad_hdr	mad_hdr;
	struct ib_rmpp_hdr	rmpp_hdr;
	u8			data[220];
} __attribute__ ((packed));

struct ib_vendor_mad {
	struct ib_mad_hdr	mad_hdr;
	struct ib_rmpp_hdr	rmpp_hdr;
	u8			reserved;
	u8			oui[3];
	u8			data[216];
} __attribute__ ((packed));

struct ib_mad_agent;
struct ib_mad_send_wc;
struct ib_mad_recv_wc;

/**
 * ib_mad_send_handler - callback handler for a sent MAD.
 * @mad_agent: MAD agent that sent the MAD.
 * @mad_send_wc: Send work completion information on the sent MAD.
 */
typedef void (*ib_mad_send_handler)(struct ib_mad_agent *mad_agent,
				    struct ib_mad_send_wc *mad_send_wc);

/**
 * ib_mad_snoop_handler - Callback handler for snooping sent MADs.
 * @mad_agent: MAD agent that snooped the MAD.
 * @send_wr: Work request information on the sent MAD.
 * @mad_send_wc: Work completion information on the sent MAD.  Valid
 *   only for snooping that occurs on a send completion.
 *
 * Clients snooping MADs should not modify data referenced by the @send_wr
 * or @mad_send_wc.
 */
typedef void (*ib_mad_snoop_handler)(struct ib_mad_agent *mad_agent,
				     struct ib_send_wr *send_wr,
				     struct ib_mad_send_wc *mad_send_wc);

/**
 * ib_mad_recv_handler - callback handler for a received MAD.
 * @mad_agent: MAD agent requesting the received MAD.
 * @mad_recv_wc: Received work completion information on the received MAD.
 *
 * MADs received in response to a send request operation will be handed to
 * the user after the send operation completes.  All data buffers given
 * to registered agents through this routine are owned by the receiving
 * client, except for snooping agents.  Clients snooping MADs should not
 * modify the data referenced by @mad_recv_wc.
 */
typedef void (*ib_mad_recv_handler)(struct ib_mad_agent *mad_agent,
				    struct ib_mad_recv_wc *mad_recv_wc);

/**
 * ib_mad_agent - Used to track MAD registration with the access layer.
 * @device: Reference to device registration is on.
 * @qp: Reference to QP used for sending and receiving MADs.
 * @recv_handler: Callback handler for a received MAD.
 * @send_handler: Callback handler for a sent MAD.
 * @snoop_handler: Callback handler for snooped sent MADs.
 * @context: User-specified context associated with this registration.
 * @hi_tid: Access layer assigned transaction ID for this client.
 *   Unsolicited MADs sent by this client will have the upper 32-bits
 *   of their TID set to this value.
 * @port_num: Port number on which QP is registered
 */
struct ib_mad_agent {
	struct ib_device	*device;
	struct ib_qp		*qp;
	ib_mad_recv_handler	recv_handler;
	ib_mad_send_handler	send_handler;
	ib_mad_snoop_handler	snoop_handler;
	void			*context;
	u32			hi_tid;
	u8			port_num;
};

/**
 * ib_mad_send_wc - MAD send completion information.
 * @wr_id: Work request identifier associated with the send MAD request.
 * @status: Completion status.
 * @vendor_err: Optional vendor error information returned with a failed
 *   request.
 */
struct ib_mad_send_wc {
	u64			wr_id;
	enum ib_wc_status	status;
	u32			vendor_err;
};

/**
 * ib_mad_recv_buf - received MAD buffer information.
 * @list: Reference to next data buffer for a received RMPP MAD.
 * @grh: References a data buffer containing the global route header.
 *   The data refereced by this buffer is only valid if the GRH is
 *   valid.
 * @mad: References the start of the received MAD.
 */
struct ib_mad_recv_buf {
	struct list_head	list;
	struct ib_grh		*grh;
	struct ib_mad		*mad;
};

/**
 * ib_mad_recv_wc - received MAD information.
 * @wc: Completion information for the received data.
 * @recv_buf: Specifies the location of the received data buffer(s).
 * @mad_len: The length of the received MAD, without duplicated headers.
 *
 * For received response, the wr_id field of the wc is set to the wr_id
 *   for the corresponding send request.
 */
struct ib_mad_recv_wc {
	struct ib_wc		*wc;
	struct ib_mad_recv_buf	recv_buf;
	int			mad_len;
};

/**
 * ib_mad_reg_req - MAD registration request
 * @mgmt_class: Indicates which management class of MADs should be receive
 *   by the caller.  This field is only required if the user wishes to
 *   receive unsolicited MADs, otherwise it should be 0.
 * @mgmt_class_version: Indicates which version of MADs for the given
 *   management class to receive.
 * @oui: Indicates IEEE OUI when mgmt_class is a vendor class
 *   in the range from 0x30 to 0x4f. Otherwise not used.
 * @method_mask: The caller will receive unsolicited MADs for any method
 *   where @method_mask = 1.
 */
struct ib_mad_reg_req {
	u8	mgmt_class;
	u8	mgmt_class_version;
	u8	oui[3];
	DECLARE_BITMAP(method_mask, IB_MGMT_MAX_METHODS);
};

/**
 * ib_register_mad_agent - Register to send/receive MADs.
 * @device: The device to register with.
 * @port_num: The port on the specified device to use.
 * @qp_type: Specifies which QP to access.  Must be either
 *   IB_QPT_SMI or IB_QPT_GSI.
 * @mad_reg_req: Specifies which unsolicited MADs should be received
 *   by the caller.  This parameter may be NULL if the caller only
 *   wishes to receive solicited responses.
 * @rmpp_version: If set, indicates that the client will send
 *   and receive MADs that contain the RMPP header for the given version.
 *   If set to 0, indicates that RMPP is not used by this client.
 * @send_handler: The completion callback routine invoked after a send
 *   request has completed.
 * @recv_handler: The completion callback routine invoked for a received
 *   MAD.
 * @context: User specified context associated with the registration.
 */
struct ib_mad_agent *ib_register_mad_agent(struct ib_device *device,
					   u8 port_num,
					   enum ib_qp_type qp_type,
					   struct ib_mad_reg_req *mad_reg_req,
					   u8 rmpp_version,
					   ib_mad_send_handler send_handler,
					   ib_mad_recv_handler recv_handler,
					   void *context);

enum ib_mad_snoop_flags {
	/*IB_MAD_SNOOP_POSTED_SENDS	   = 1,*/
	/*IB_MAD_SNOOP_RMPP_SENDS	   = (1<<1),*/
	IB_MAD_SNOOP_SEND_COMPLETIONS	   = (1<<2),
	/*IB_MAD_SNOOP_RMPP_SEND_COMPLETIONS = (1<<3),*/
	IB_MAD_SNOOP_RECVS		   = (1<<4)
	/*IB_MAD_SNOOP_RMPP_RECVS	   = (1<<5),*/
	/*IB_MAD_SNOOP_REDIRECTED_QPS	   = (1<<6)*/
};

/**
 * ib_register_mad_snoop - Register to snoop sent and received MADs.
 * @device: The device to register with.
 * @port_num: The port on the specified device to use.
 * @qp_type: Specifies which QP traffic to snoop.  Must be either
 *   IB_QPT_SMI or IB_QPT_GSI.
 * @mad_snoop_flags: Specifies information where snooping occurs.
 * @send_handler: The callback routine invoked for a snooped send.
 * @recv_handler: The callback routine invoked for a snooped receive.
 * @context: User specified context associated with the registration.
 */
struct ib_mad_agent *ib_register_mad_snoop(struct ib_device *device,
					   u8 port_num,
					   enum ib_qp_type qp_type,
					   int mad_snoop_flags,
					   ib_mad_snoop_handler snoop_handler,
					   ib_mad_recv_handler recv_handler,
					   void *context);

/**
 * ib_unregister_mad_agent - Unregisters a client from using MAD services.
 * @mad_agent: Corresponding MAD registration request to deregister.
 *
 * After invoking this routine, MAD services are no longer usable by the
 * client on the associated QP.
 */
int ib_unregister_mad_agent(struct ib_mad_agent *mad_agent);

/**
 * ib_post_send_mad - Posts MAD(s) to the send queue of the QP associated
 *   with the registered client.
 * @mad_agent: Specifies the associated registration to post the send to.
 * @send_wr: Specifies the information needed to send the MAD(s).
 * @bad_send_wr: Specifies the MAD on which an error was encountered.
 *
 * Sent MADs are not guaranteed to complete in the order that they were posted.
 */
int ib_post_send_mad(struct ib_mad_agent *mad_agent,
		     struct ib_send_wr *send_wr,
		     struct ib_send_wr **bad_send_wr);

/**
 * ib_coalesce_recv_mad - Coalesces received MAD data into a single buffer.
 * @mad_recv_wc: Work completion information for a received MAD.
 * @buf: User-provided data buffer to receive the coalesced buffers.  The
 *   referenced buffer should be at least the size of the mad_len specified
 *   by @mad_recv_wc.
 *
 * This call copies a chain of received RMPP MADs into a single data buffer,
 * removing duplicated headers.
 */
void ib_coalesce_recv_mad(struct ib_mad_recv_wc *mad_recv_wc,
			  void *buf);

/**
 * ib_free_recv_mad - Returns data buffers used to receive a MAD to the
 *   access layer.
 * @mad_recv_wc: Work completion information for a received MAD.
 *
 * Clients receiving MADs through their ib_mad_recv_handler must call this
 * routine to return the work completion buffers to the access layer.
 */
void ib_free_recv_mad(struct ib_mad_recv_wc *mad_recv_wc);

/**
 * ib_cancel_mad - Cancels an outstanding send MAD operation.
 * @mad_agent: Specifies the registration associated with sent MAD.
 * @wr_id: Indicates the work request identifier of the MAD to cancel.
 *
 * MADs will be returned to the user through the corresponding
 * ib_mad_send_handler.
 */
void ib_cancel_mad(struct ib_mad_agent *mad_agent,
		   u64 wr_id);

/**
 * ib_redirect_mad_qp - Registers a QP for MAD services.
 * @qp: Reference to a QP that requires MAD services.
 * @rmpp_version: If set, indicates that the client will send
 *   and receive MADs that contain the RMPP header for the given version.
 *   If set to 0, indicates that RMPP is not used by this client.
 * @send_handler: The completion callback routine invoked after a send
 *   request has completed.
 * @recv_handler: The completion callback routine invoked for a received
 *   MAD.
 * @context: User specified context associated with the registration.
 *
 * Use of this call allows clients to use MAD services, such as RMPP,
 * on user-owned QPs.  After calling this routine, users may send
 * MADs on the specified QP by calling ib_mad_post_send.
 */
struct ib_mad_agent *ib_redirect_mad_qp(struct ib_qp *qp,
					u8 rmpp_version,
					ib_mad_send_handler send_handler,
					ib_mad_recv_handler recv_handler,
					void *context);

/**
 * ib_process_mad_wc - Processes a work completion associated with a
 *   MAD sent or received on a redirected QP.
 * @mad_agent: Specifies the registered MAD service using the redirected QP.
 * @wc: References a work completion associated with a sent or received
 *   MAD segment.
 *
 * This routine is used to complete or continue processing on a MAD request.
 * If the work completion is associated with a send operation, calling
 * this routine is required to continue an RMPP transfer or to wait for a
 * corresponding response, if it is a request.  If the work completion is
 * associated with a receive operation, calling this routine is required to
 * process an inbound or outbound RMPP transfer, or to match a response MAD
 * with its corresponding request.
 */
int ib_process_mad_wc(struct ib_mad_agent *mad_agent,
		      struct ib_wc *wc);

#endif /* IB_MAD_H */
