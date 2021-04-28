/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#ifndef RTRS_H
#define RTRS_H

#include <linux/socket.h>
#include <linux/scatterlist.h>

struct rtrs_permit;
struct rtrs_clt;
struct rtrs_srv_ctx;
struct rtrs_srv;
struct rtrs_srv_op;

/*
 * RDMA transport (RTRS) client API
 */

/**
 * enum rtrs_clt_link_ev - Events about connectivity state of a client
 * @RTRS_CLT_LINK_EV_RECONNECTED	Client was reconnected.
 * @RTRS_CLT_LINK_EV_DISCONNECTED	Client was disconnected.
 */
enum rtrs_clt_link_ev {
	RTRS_CLT_LINK_EV_RECONNECTED,
	RTRS_CLT_LINK_EV_DISCONNECTED,
};

/**
 * Source and destination address of a path to be established
 */
struct rtrs_addr {
	struct sockaddr_storage *src;
	struct sockaddr_storage *dst;
};

/**
 * rtrs_clt_ops - it holds the link event callback and private pointer.
 * @priv: User supplied private data.
 * @link_ev: Event notification callback function for connection state changes
 *	@priv: User supplied data that was passed to rtrs_clt_open()
 *	@ev: Occurred event
 */
struct rtrs_clt_ops {
	void	*priv;
	void	(*link_ev)(void *priv, enum rtrs_clt_link_ev ev);
};

struct rtrs_clt *rtrs_clt_open(struct rtrs_clt_ops *ops,
				 const char *sessname,
				 const struct rtrs_addr *paths,
				 size_t path_cnt, u16 port,
				 size_t pdu_sz, u8 reconnect_delay_sec,
				 u16 max_segments,
				 s16 max_reconnect_attempts, u32 nr_poll_queues);

void rtrs_clt_close(struct rtrs_clt *sess);

enum wait_type {
	RTRS_PERMIT_NOWAIT = 0,
	RTRS_PERMIT_WAIT   = 1
};

/**
 * enum rtrs_clt_con_type() type of ib connection to use with a given
 * rtrs_permit
 * @ADMIN_CON - use connection reserved for "service" messages
 * @IO_CON - use a connection reserved for IO
 */
enum rtrs_clt_con_type {
	RTRS_ADMIN_CON,
	RTRS_IO_CON
};

struct rtrs_permit *rtrs_clt_get_permit(struct rtrs_clt *sess,
				    enum rtrs_clt_con_type con_type,
				    enum wait_type wait);

void rtrs_clt_put_permit(struct rtrs_clt *sess, struct rtrs_permit *permit);

/**
 * rtrs_clt_req_ops - it holds the request confirmation callback
 * and a private pointer.
 * @priv: User supplied private data.
 * @conf_fn:	callback function to be called as confirmation
 *	@priv:	User provided data, passed back with corresponding
 *		@(conf) confirmation.
 *	@errno: error number.
 */
struct rtrs_clt_req_ops {
	void	*priv;
	void	(*conf_fn)(void *priv, int errno);
};

int rtrs_clt_request(int dir, struct rtrs_clt_req_ops *ops,
		     struct rtrs_clt *sess, struct rtrs_permit *permit,
		     const struct kvec *vec, size_t nr, size_t len,
		     struct scatterlist *sg, unsigned int sg_cnt);
int rtrs_clt_rdma_cq_direct(struct rtrs_clt *clt, unsigned int index);

/**
 * rtrs_attrs - RTRS session attributes
 */
struct rtrs_attrs {
	u32		queue_depth;
	u32		max_io_size;
	u8		sessname[NAME_MAX];
	struct kobject	*sess_kobj;
};

int rtrs_clt_query(struct rtrs_clt *sess, struct rtrs_attrs *attr);

/*
 * Here goes RTRS server API
 */

/**
 * enum rtrs_srv_link_ev - Server link events
 * @RTRS_SRV_LINK_EV_CONNECTED:	Connection from client established
 * @RTRS_SRV_LINK_EV_DISCONNECTED:	Connection was disconnected, all
 *					connection RTRS resources were freed.
 */
enum rtrs_srv_link_ev {
	RTRS_SRV_LINK_EV_CONNECTED,
	RTRS_SRV_LINK_EV_DISCONNECTED,
};

struct rtrs_srv_ops {
	/**
	 * rdma_ev():		Event notification for RDMA operations
	 *			If the callback returns a value != 0, an error
	 *			message for the data transfer will be sent to
	 *			the client.

	 *	@priv:		Private data set by rtrs_srv_set_sess_priv()
	 *	@id:		internal RTRS operation id
	 *	@dir:		READ/WRITE
	 *	@data:		Pointer to (bidirectional) rdma memory area:
	 *			- in case of %RTRS_SRV_RDMA_EV_RECV contains
	 *			data sent by the client
	 *			- in case of %RTRS_SRV_RDMA_EV_WRITE_REQ points
	 *			to the memory area where the response is to be
	 *			written to
	 *	@datalen:	Size of the memory area in @data
	 *	@usr:		The extra user message sent by the client (%vec)
	 *	@usrlen:	Size of the user message
	 */
	int (*rdma_ev)(void *priv,
		       struct rtrs_srv_op *id, int dir,
		       void *data, size_t datalen, const void *usr,
		       size_t usrlen);
	/**
	 * link_ev():		Events about connectivity state changes
	 *			If the callback returns != 0 and the event
	 *			%RTRS_SRV_LINK_EV_CONNECTED the corresponding
	 *			session will be destroyed.
	 *	@sess:		Session
	 *	@ev:		event
	 *	@priv:		Private data from user if previously set with
	 *			rtrs_srv_set_sess_priv()
	 */
	int (*link_ev)(struct rtrs_srv *sess, enum rtrs_srv_link_ev ev,
		       void *priv);
};

struct rtrs_srv_ctx *rtrs_srv_open(struct rtrs_srv_ops *ops, u16 port);

void rtrs_srv_close(struct rtrs_srv_ctx *ctx);

bool rtrs_srv_resp_rdma(struct rtrs_srv_op *id, int errno);

void rtrs_srv_set_sess_priv(struct rtrs_srv *sess, void *priv);

int rtrs_srv_get_sess_name(struct rtrs_srv *sess, char *sessname, size_t len);

int rtrs_srv_get_queue_depth(struct rtrs_srv *sess);

int rtrs_addr_to_sockaddr(const char *str, size_t len, u16 port,
			  struct rtrs_addr *addr);

int sockaddr_to_str(const struct sockaddr *addr, char *buf, size_t len);
#endif
