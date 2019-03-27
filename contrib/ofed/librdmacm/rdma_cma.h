/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2005-2014 Intel Corporation.  All rights reserved.
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

#if !defined(RDMA_CMA_H)
#define RDMA_CMA_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <infiniband/verbs.h>
#include <infiniband/sa.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Upon receiving a device removal event, users must destroy the associated
 * RDMA identifier and release all resources allocated with the device.
 */
enum rdma_cm_event_type {
	RDMA_CM_EVENT_ADDR_RESOLVED,
	RDMA_CM_EVENT_ADDR_ERROR,
	RDMA_CM_EVENT_ROUTE_RESOLVED,
	RDMA_CM_EVENT_ROUTE_ERROR,
	RDMA_CM_EVENT_CONNECT_REQUEST,
	RDMA_CM_EVENT_CONNECT_RESPONSE,
	RDMA_CM_EVENT_CONNECT_ERROR,
	RDMA_CM_EVENT_UNREACHABLE,
	RDMA_CM_EVENT_REJECTED,
	RDMA_CM_EVENT_ESTABLISHED,
	RDMA_CM_EVENT_DISCONNECTED,
	RDMA_CM_EVENT_DEVICE_REMOVAL,
	RDMA_CM_EVENT_MULTICAST_JOIN,
	RDMA_CM_EVENT_MULTICAST_ERROR,
	RDMA_CM_EVENT_ADDR_CHANGE,
	RDMA_CM_EVENT_TIMEWAIT_EXIT
};

enum rdma_port_space {
	RDMA_PS_IPOIB = 0x0002,
	RDMA_PS_TCP   = 0x0106,
	RDMA_PS_UDP   = 0x0111,
	RDMA_PS_IB    = 0x013F,
};

#define RDMA_IB_IP_PS_MASK   0xFFFFFFFFFFFF0000ULL
#define RDMA_IB_IP_PORT_MASK 0x000000000000FFFFULL
#define RDMA_IB_IP_PS_TCP    0x0000000001060000ULL
#define RDMA_IB_IP_PS_UDP    0x0000000001110000ULL
#define RDMA_IB_PS_IB        0x00000000013F0000ULL

/*
 * Global qkey value for UDP QPs and multicast groups created via the 
 * RDMA CM.
 */
#define RDMA_UDP_QKEY 0x01234567

struct rdma_ib_addr {
	union ibv_gid	sgid;
	union ibv_gid	dgid;
	__be16		pkey;
};

struct rdma_addr {
	union {
		struct sockaddr		src_addr;
		struct sockaddr_in	src_sin;
		struct sockaddr_in6	src_sin6;
		struct sockaddr_storage src_storage;
	};
	union {
		struct sockaddr		dst_addr;
		struct sockaddr_in	dst_sin;
		struct sockaddr_in6	dst_sin6;
		struct sockaddr_storage dst_storage;
	};
	union {
		struct rdma_ib_addr	ibaddr;
	} addr;
};

struct rdma_route {
	struct rdma_addr	 addr;
	struct ibv_sa_path_rec	*path_rec;
	int			 num_paths;
};

struct rdma_event_channel {
	int			fd;
};

struct rdma_cm_id {
	struct ibv_context	*verbs;
	struct rdma_event_channel *channel;
	void			*context;
	struct ibv_qp		*qp;
	struct rdma_route	 route;
	enum rdma_port_space	 ps;
	uint8_t			 port_num;
	struct rdma_cm_event	*event;
	struct ibv_comp_channel *send_cq_channel;
	struct ibv_cq		*send_cq;
	struct ibv_comp_channel *recv_cq_channel;
	struct ibv_cq		*recv_cq;
	struct ibv_srq		*srq;
	struct ibv_pd		*pd;
	enum ibv_qp_type	qp_type;
};

enum {
	RDMA_MAX_RESP_RES = 0xFF,
	RDMA_MAX_INIT_DEPTH = 0xFF
};

struct rdma_conn_param {
	const void *private_data;
	uint8_t private_data_len;
	uint8_t responder_resources;
	uint8_t initiator_depth;
	uint8_t flow_control;
	uint8_t retry_count;		/* ignored when accepting */
	uint8_t rnr_retry_count;
	/* Fields below ignored if a QP is created on the rdma_cm_id. */
	uint8_t srq;
	uint32_t qp_num;
};

struct rdma_ud_param {
	const void *private_data;
	uint8_t private_data_len;
	struct ibv_ah_attr ah_attr;
	uint32_t qp_num;
	uint32_t qkey;
};

struct rdma_cm_event {
	struct rdma_cm_id	*id;
	struct rdma_cm_id	*listen_id;
	enum rdma_cm_event_type	 event;
	int			 status;
	union {
		struct rdma_conn_param conn;
		struct rdma_ud_param   ud;
	} param;
};

#define RAI_PASSIVE		0x00000001
#define RAI_NUMERICHOST		0x00000002
#define RAI_NOROUTE		0x00000004
#define RAI_FAMILY		0x00000008

struct rdma_addrinfo {
	int			ai_flags;
	int			ai_family;
	int			ai_qp_type;
	int			ai_port_space;
	socklen_t		ai_src_len;
	socklen_t		ai_dst_len;
	struct sockaddr		*ai_src_addr;
	struct sockaddr		*ai_dst_addr;
	char			*ai_src_canonname;
	char			*ai_dst_canonname;
	size_t			ai_route_len;
	void			*ai_route;
	size_t			ai_connect_len;
	void			*ai_connect;
	struct rdma_addrinfo	*ai_next;
};

/**
 * rdma_create_event_channel - Open a channel used to report communication events.
 * Description:
 *   Asynchronous events are reported to users through event channels.  Each
 *   event channel maps to a file descriptor.
 * Notes:
 *   All created event channels must be destroyed by calling
 *   rdma_destroy_event_channel.  Users should call rdma_get_cm_event to
 *   retrieve events on an event channel.
 * See also:
 *   rdma_get_cm_event, rdma_destroy_event_channel
 */
struct rdma_event_channel *rdma_create_event_channel(void);

/**
 * rdma_destroy_event_channel - Close an event communication channel.
 * @channel: The communication channel to destroy.
 * Description:
 *   Release all resources associated with an event channel and closes the
 *   associated file descriptor.
 * Notes:
 *   All rdma_cm_id's associated with the event channel must be destroyed,
 *   and all returned events must be acked before calling this function.
 * See also:
 *  rdma_create_event_channel, rdma_get_cm_event, rdma_ack_cm_event
 */
void rdma_destroy_event_channel(struct rdma_event_channel *channel);

/**
 * rdma_create_id - Allocate a communication identifier.
 * @channel: The communication channel that events associated with the
 *   allocated rdma_cm_id will be reported on.
 * @id: A reference where the allocated communication identifier will be
 *   returned.
 * @context: User specified context associated with the rdma_cm_id.
 * @ps: RDMA port space.
 * Description:
 *   Creates an identifier that is used to track communication information.
 * Notes:
 *   Rdma_cm_id's are conceptually equivalent to a socket for RDMA
 *   communication.  The difference is that RDMA communication requires
 *   explicitly binding to a specified RDMA device before communication
 *   can occur, and most operations are asynchronous in nature.  Communication
 *   events on an rdma_cm_id are reported through the associated event
 *   channel.  Users must release the rdma_cm_id by calling rdma_destroy_id.
 * See also:
 *   rdma_create_event_channel, rdma_destroy_id, rdma_get_devices,
 *   rdma_bind_addr, rdma_resolve_addr, rdma_connect, rdma_listen,
 */
int rdma_create_id(struct rdma_event_channel *channel,
		   struct rdma_cm_id **id, void *context,
		   enum rdma_port_space ps);

/**
 * rdma_create_ep - Allocate a communication identifier and qp.
 * @id: A reference where the allocated communication identifier will be
 *   returned.
 * @res: Result from rdma_getaddrinfo, which specifies the source and
 *   destination addresses, plus optional routing and connection information.
 * @pd: Optional protection domain.  This parameter is ignored if qp_init_attr
 *   is NULL.
 * @qp_init_attr: Optional attributes for a QP created on the rdma_cm_id.
 * Description:
 *   Create an identifier and option QP used for communication.
 * Notes:
 *   If qp_init_attr is provided, then a queue pair will be allocated and
 *   associated with the rdma_cm_id.  If a pd is provided, the QP will be
 *   created on that PD.  Otherwise, the QP will be allocated on a default
 *   PD.
 *   The rdma_cm_id will be set to use synchronous operations (connect,
 *   listen, and get_request).  To convert to asynchronous operation, the
 *   rdma_cm_id should be migrated to a user allocated event channel.
 * See also:
 *   rdma_create_id, rdma_create_qp, rdma_migrate_id, rdma_connect,
 *   rdma_listen
 */
int rdma_create_ep(struct rdma_cm_id **id, struct rdma_addrinfo *res,
		   struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);

/**
 * rdma_destroy_ep - Deallocates a communication identifier and qp.
 * @id: The communication identifier to destroy.
 * Description:
 *   Destroys the specified rdma_cm_id and any associated QP created
 *   on that id.
 * See also:
 *   rdma_create_ep
 */
void rdma_destroy_ep(struct rdma_cm_id *id);

/**
 * rdma_destroy_id - Release a communication identifier.
 * @id: The communication identifier to destroy.
 * Description:
 *   Destroys the specified rdma_cm_id and cancels any outstanding
 *   asynchronous operation.
 * Notes:
 *   Users must free any associated QP with the rdma_cm_id before
 *   calling this routine and ack an related events.
 * See also:
 *   rdma_create_id, rdma_destroy_qp, rdma_ack_cm_event
 */
int rdma_destroy_id(struct rdma_cm_id *id);

/**
 * rdma_bind_addr - Bind an RDMA identifier to a source address.
 * @id: RDMA identifier.
 * @addr: Local address information.  Wildcard values are permitted.
 * Description:
 *   Associates a source address with an rdma_cm_id.  The address may be
 *   wildcarded.  If binding to a specific local address, the rdma_cm_id
 *   will also be bound to a local RDMA device.
 * Notes:
 *   Typically, this routine is called before calling rdma_listen to bind
 *   to a specific port number, but it may also be called on the active side
 *   of a connection before calling rdma_resolve_addr to bind to a specific
 *   address.
 * See also:
 *   rdma_create_id, rdma_listen, rdma_resolve_addr, rdma_create_qp
 */
int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr);

/**
 * rdma_resolve_addr - Resolve destination and optional source addresses.
 * @id: RDMA identifier.
 * @src_addr: Source address information.  This parameter may be NULL.
 * @dst_addr: Destination address information.
 * @timeout_ms: Time to wait for resolution to complete.
 * Description:
 *   Resolve destination and optional source addresses from IP addresses
 *   to an RDMA address.  If successful, the specified rdma_cm_id will
 *   be bound to a local device.
 * Notes:
 *   This call is used to map a given destination IP address to a usable RDMA
 *   address.  If a source address is given, the rdma_cm_id is bound to that
 *   address, the same as if rdma_bind_addr were called.  If no source
 *   address is given, and the rdma_cm_id has not yet been bound to a device,
 *   then the rdma_cm_id will be bound to a source address based on the
 *   local routing tables.  After this call, the rdma_cm_id will be bound to
 *   an RDMA device.  This call is typically made from the active side of a
 *   connection before calling rdma_resolve_route and rdma_connect.
 * See also:
 *   rdma_create_id, rdma_resolve_route, rdma_connect, rdma_create_qp,
 *   rdma_get_cm_event, rdma_bind_addr
 */
int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms);

/**
 * rdma_resolve_route - Resolve the route information needed to establish a connection.
 * @id: RDMA identifier.
 * @timeout_ms: Time to wait for resolution to complete.
 * Description:
 *   Resolves an RDMA route to the destination address in order to establish
 *   a connection.  The destination address must have already been resolved
 *   by calling rdma_resolve_addr.
 * Notes:
 *   This is called on the client side of a connection after calling
 *   rdma_resolve_addr, but before calling rdma_connect.
 * See also:
 *   rdma_resolve_addr, rdma_connect, rdma_get_cm_event
 */
int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms);

/**
 * rdma_create_qp - Allocate a QP.
 * @id: RDMA identifier.
 * @pd: Optional protection domain for the QP.
 * @qp_init_attr: initial QP attributes.
 * Description:
 *  Allocate a QP associated with the specified rdma_cm_id and transition it
 *  for sending and receiving.
 * Notes:
 *   The rdma_cm_id must be bound to a local RDMA device before calling this
 *   function, and the protection domain must be for that same device.
 *   QPs allocated to an rdma_cm_id are automatically transitioned by the
 *   librdmacm through their states.  After being allocated, the QP will be
 *   ready to handle posting of receives.  If the QP is unconnected, it will
 *   be ready to post sends.
 *   If pd is NULL, then the QP will be allocated using a default protection
 *   domain associated with the underlying RDMA device.
 * See also:
 *   rdma_bind_addr, rdma_resolve_addr, rdma_destroy_qp, ibv_create_qp,
 *   ibv_modify_qp
 */
int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,
		   struct ibv_qp_init_attr *qp_init_attr);
int rdma_create_qp_ex(struct rdma_cm_id *id,
		      struct ibv_qp_init_attr_ex *qp_init_attr);

/**
 * rdma_destroy_qp - Deallocate a QP.
 * @id: RDMA identifier.
 * Description:
 *   Destroy a QP allocated on the rdma_cm_id.
 * Notes:
 *   Users must destroy any QP associated with an rdma_cm_id before
 *   destroying the ID.
 * See also:
 *   rdma_create_qp, rdma_destroy_id, ibv_destroy_qp
 */
void rdma_destroy_qp(struct rdma_cm_id *id);

/**
 * rdma_connect - Initiate an active connection request.
 * @id: RDMA identifier.
 * @conn_param: optional connection parameters.
 * Description:
 *   For a connected rdma_cm_id, this call initiates a connection request
 *   to a remote destination.  For an unconnected rdma_cm_id, it initiates
 *   a lookup of the remote QP providing the datagram service.
 * Notes:
 *   Users must have resolved a route to the destination address
 *   by having called rdma_resolve_route before calling this routine.
 *   A user may override the default connection parameters and exchange
 *   private data as part of the connection by using the conn_param parameter.
 * See also:
 *   rdma_resolve_route, rdma_disconnect, rdma_listen, rdma_get_cm_event
 */
int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);

/**
 * rdma_listen - Listen for incoming connection requests.
 * @id: RDMA identifier.
 * @backlog: backlog of incoming connection requests.
 * Description:
 *   Initiates a listen for incoming connection requests or datagram service
 *   lookup.  The listen will be restricted to the locally bound source
 *   address.
 * Notes:
 *   Users must have bound the rdma_cm_id to a local address by calling
 *   rdma_bind_addr before calling this routine.  If the rdma_cm_id is
 *   bound to a specific IP address, the listen will be restricted to that
 *   address and the associated RDMA device.  If the rdma_cm_id is bound
 *   to an RDMA port number only, the listen will occur across all RDMA
 *   devices.
 * See also:
 *   rdma_bind_addr, rdma_connect, rdma_accept, rdma_reject, rdma_get_cm_event
 */
int rdma_listen(struct rdma_cm_id *id, int backlog);

/**
 * rdma_get_request
 */
int rdma_get_request(struct rdma_cm_id *listen, struct rdma_cm_id **id);

/**
 * rdma_accept - Called to accept a connection request.
 * @id: Connection identifier associated with the request.
 * @conn_param: Optional information needed to establish the connection.
 * Description:
 *   Called from the listening side to accept a connection or datagram
 *   service lookup request.
 * Notes:
 *   Unlike the socket accept routine, rdma_accept is not called on a
 *   listening rdma_cm_id.  Instead, after calling rdma_listen, the user
 *   waits for a connection request event to occur.  Connection request
 *   events give the user a newly created rdma_cm_id, similar to a new
 *   socket, but the rdma_cm_id is bound to a specific RDMA device.
 *   rdma_accept is called on the new rdma_cm_id.
 *   A user may override the default connection parameters and exchange
 *   private data as part of the connection by using the conn_param parameter.
 * See also:
 *   rdma_listen, rdma_reject, rdma_get_cm_event
 */
int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);

/**
 * rdma_reject - Called to reject a connection request.
 * @id: Connection identifier associated with the request.
 * @private_data: Optional private data to send with the reject message.
 * @private_data_len: Size of the private_data to send, in bytes.
 * Description:
 *   Called from the listening side to reject a connection or datagram
 *   service lookup request.
 * Notes:
 *   After receiving a connection request event, a user may call rdma_reject
 *   to reject the request.  If the underlying RDMA transport supports
 *   private data in the reject message, the specified data will be passed to
 *   the remote side.
 * See also:
 *   rdma_listen, rdma_accept, rdma_get_cm_event
 */
int rdma_reject(struct rdma_cm_id *id, const void *private_data,
		uint8_t private_data_len);

/**
 * rdma_notify - Notifies the librdmacm of an asynchronous event.
 * @id: RDMA identifier.
 * @event: Asynchronous event.
 * Description:
 *   Used to notify the librdmacm of asynchronous events that have occurred
 *   on a QP associated with the rdma_cm_id.
 * Notes:
 *   Asynchronous events that occur on a QP are reported through the user's
 *   device event handler.  This routine is used to notify the librdmacm of
 *   communication events.  In most cases, use of this routine is not
 *   necessary, however if connection establishment is done out of band
 *   (such as done through Infiniband), it's possible to receive data on a
 *   QP that is not yet considered connected.  This routine forces the
 *   connection into an established state in this case in order to handle
 *   the rare situation where the connection never forms on its own.
 *   Events that should be reported to the CM are: IB_EVENT_COMM_EST.
 * See also:
 *   rdma_connect, rdma_accept, rdma_listen
 */
int rdma_notify(struct rdma_cm_id *id, enum ibv_event_type event);

/**
 * rdma_disconnect - This function disconnects a connection.
 * @id: RDMA identifier.
 * Description:
 *   Disconnects a connection and transitions any associated QP to the
 *   error state.
 * See also:
 *   rdma_connect, rdma_listen, rdma_accept
 */
int rdma_disconnect(struct rdma_cm_id *id);

/**
 * rdma_join_multicast - Joins a multicast group.
 * @id: Communication identifier associated with the request.
 * @addr: Multicast address identifying the group to join.
 * @context: User-defined context associated with the join request.
 * Description:
 *   Joins a multicast group and attaches an associated QP to the group.
 * Notes:
 *   Before joining a multicast group, the rdma_cm_id must be bound to
 *   an RDMA device by calling rdma_bind_addr or rdma_resolve_addr.  Use of
 *   rdma_resolve_addr requires the local routing tables to resolve the
 *   multicast address to an RDMA device.  The user must call
 *   rdma_leave_multicast to leave the multicast group and release any
 *   multicast resources.  The context is returned to the user through
 *   the private_data field in the rdma_cm_event.
 * See also:
 *   rdma_leave_multicast, rdma_bind_addr, rdma_resolve_addr, rdma_create_qp
 */
int rdma_join_multicast(struct rdma_cm_id *id, struct sockaddr *addr,
			void *context);

/**
 * rdma_leave_multicast - Leaves a multicast group.
 * @id: Communication identifier associated with the request.
 * @addr: Multicast address identifying the group to leave.
 * Description:
 *   Leaves a multicast group and detaches an associated QP from the group.
 * Notes:
 *   Calling this function before a group has been fully joined results in
 *   canceling the join operation.  Users should be aware that messages
 *   received from the multicast group may stilled be queued for
 *   completion processing immediately after leaving a multicast group.
 *   Destroying an rdma_cm_id will automatically leave all multicast groups.
 * See also:
 *   rdma_join_multicast, rdma_destroy_qp
 */
int rdma_leave_multicast(struct rdma_cm_id *id, struct sockaddr *addr);

/**
 * rdma_get_cm_event - Retrieves the next pending communication event.
 * @channel: Event channel to check for events.
 * @event: Allocated information about the next communication event.
 * Description:
 *   Retrieves a communication event.  If no events are pending, by default,
 *   the call will block until an event is received.
 * Notes:
 *   The default synchronous behavior of this routine can be changed by
 *   modifying the file descriptor associated with the given channel.  All
 *   events that are reported must be acknowledged by calling rdma_ack_cm_event.
 *   Destruction of an rdma_cm_id will block until related events have been
 *   acknowledged.
 * See also:
 *   rdma_ack_cm_event, rdma_create_event_channel, rdma_event_str
 */
int rdma_get_cm_event(struct rdma_event_channel *channel,
		      struct rdma_cm_event **event);

/**
 * rdma_ack_cm_event - Free a communication event.
 * @event: Event to be released.
 * Description:
 *   All events which are allocated by rdma_get_cm_event must be released,
 *   there should be a one-to-one correspondence between successful gets
 *   and acks.
 * See also:
 *   rdma_get_cm_event, rdma_destroy_id
 */
int rdma_ack_cm_event(struct rdma_cm_event *event);

__be16 rdma_get_src_port(struct rdma_cm_id *id);
__be16 rdma_get_dst_port(struct rdma_cm_id *id);

static inline struct sockaddr *rdma_get_local_addr(struct rdma_cm_id *id)
{
	return &id->route.addr.src_addr;
}

static inline struct sockaddr *rdma_get_peer_addr(struct rdma_cm_id *id)
{
	return &id->route.addr.dst_addr;
}

/**
 * rdma_get_devices - Get list of RDMA devices currently available.
 * @num_devices: If non-NULL, set to the number of devices returned.
 * Description:
 *   Return a NULL-terminated array of opened RDMA devices.  Callers can use
 *   this routine to allocate resources on specific RDMA devices that will be
 *   shared across multiple rdma_cm_id's.
 * Notes:
 *   The returned array must be released by calling rdma_free_devices.  Devices
 *   remain opened while the librdmacm is loaded.
 * See also:
 *   rdma_free_devices
 */
struct ibv_context **rdma_get_devices(int *num_devices);

/**
 * rdma_free_devices - Frees the list of devices returned by rdma_get_devices.
 * @list: List of devices returned from rdma_get_devices.
 * Description:
 *   Frees the device array returned by rdma_get_devices.
 * See also:
 *   rdma_get_devices
 */
void rdma_free_devices(struct ibv_context **list);

/**
 * rdma_event_str - Returns a string representation of an rdma cm event.
 * @event: Asynchronous event.
 * Description:
 *   Returns a string representation of an asynchronous event.
 * See also:
 *   rdma_get_cm_event
 */
const char *rdma_event_str(enum rdma_cm_event_type event);

/* Option levels */
enum {
	RDMA_OPTION_ID		= 0,
	RDMA_OPTION_IB		= 1
};

/* Option details */
enum {
	RDMA_OPTION_ID_TOS	 = 0,	/* uint8_t: RFC 2474 */
	RDMA_OPTION_ID_REUSEADDR = 1,   /* int: ~SO_REUSEADDR */
	RDMA_OPTION_ID_AFONLY	 = 2,   /* int: ~IPV6_V6ONLY */
	RDMA_OPTION_IB_PATH	 = 1	/* struct ibv_path_data[] */
};

/**
 * rdma_set_option - Set options for an rdma_cm_id.
 * @id: Communication identifier to set option for.
 * @level: Protocol level of the option to set.
 * @optname: Name of the option to set.
 * @optval: Reference to the option data.
 * @optlen: The size of the %optval buffer.
 */
int rdma_set_option(struct rdma_cm_id *id, int level, int optname,
		    void *optval, size_t optlen);

/**
 * rdma_migrate_id - Move an rdma_cm_id to a new event channel.
 * @id: Communication identifier to migrate.
 * @channel: New event channel for rdma_cm_id events.
 */
int rdma_migrate_id(struct rdma_cm_id *id, struct rdma_event_channel *channel);

/**
 * rdma_getaddrinfo - RDMA address and route resolution service.
 */
int rdma_getaddrinfo(const char *node, const char *service,
		     const struct rdma_addrinfo *hints,
		     struct rdma_addrinfo **res);

void rdma_freeaddrinfo(struct rdma_addrinfo *res);

#ifdef __cplusplus
}
#endif

#endif /* RDMA_CMA_H */
