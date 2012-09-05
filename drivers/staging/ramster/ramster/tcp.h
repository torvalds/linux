/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * tcp.h
 *
 * Function prototypes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef R2CLUSTER_TCP_H
#define R2CLUSTER_TCP_H

#include <linux/socket.h>
#ifdef __KERNEL__
#include <net/sock.h>
#include <linux/tcp.h>
#else
#include <sys/socket.h>
#endif
#include <linux/inet.h>
#include <linux/in.h>

struct r2net_msg {
	__be16 magic;
	__be16 data_len;
	__be16 msg_type;
	__be16 pad1;
	__be32 sys_status;
	__be32 status;
	__be32 key;
	__be32 msg_num;
	__u8  buf[0];
};

typedef int (r2net_msg_handler_func)(struct r2net_msg *msg, u32 len, void *data,
				     void **ret_data);
typedef void (r2net_post_msg_handler_func)(int status, void *data,
					   void *ret_data);

#define R2NET_MAX_PAYLOAD_BYTES  (4096 - sizeof(struct r2net_msg))

/* same as hb delay, we're waiting for another node to recognize our hb */
#define R2NET_RECONNECT_DELAY_MS_DEFAULT	2000

#define R2NET_KEEPALIVE_DELAY_MS_DEFAULT	2000
#define R2NET_IDLE_TIMEOUT_MS_DEFAULT		30000


/* TODO: figure this out.... */
static inline int r2net_link_down(int err, struct socket *sock)
{
	if (sock) {
		if (sock->sk->sk_state != TCP_ESTABLISHED &&
			sock->sk->sk_state != TCP_CLOSE_WAIT)
			return 1;
	}

	if (err >= 0)
		return 0;
	switch (err) {

	/* ????????????????????????? */
	case -ERESTARTSYS:
	case -EBADF:
	/* When the server has died, an ICMP port unreachable
	 * message prompts ECONNREFUSED. */
	case -ECONNREFUSED:
	case -ENOTCONN:
	case -ECONNRESET:
	case -EPIPE:
		return 1;

	}
	return 0;
}

enum {
	R2NET_DRIVER_UNINITED,
	R2NET_DRIVER_READY,
};

int r2net_send_message(u32 msg_type, u32 key, void *data, u32 len,
		       u8 target_node, int *status);
int r2net_send_message_vec(u32 msg_type, u32 key, struct kvec *vec,
			   size_t veclen, u8 target_node, int *status);

int r2net_register_handler(u32 msg_type, u32 key, u32 max_len,
			   r2net_msg_handler_func *func, void *data,
			   r2net_post_msg_handler_func *post_func,
			   struct list_head *unreg_list);
void r2net_unregister_handler_list(struct list_head *list);

void r2net_fill_node_map(unsigned long *map, unsigned bytes);

void r2net_force_data_magic(struct r2net_msg *, u16, u32);
void r2net_hb_node_up_manual(int);
struct r2net_node *r2net_nn_from_num(u8);

struct r2nm_node;
int r2net_register_hb_callbacks(void);
void r2net_unregister_hb_callbacks(void);
int r2net_start_listening(struct r2nm_node *node);
void r2net_stop_listening(struct r2nm_node *node);
void r2net_disconnect_node(struct r2nm_node *node);
int r2net_num_connected_peers(void);

int r2net_init(void);
void r2net_exit(void);

struct r2net_send_tracking;
struct r2net_sock_container;

#if 0
int r2net_debugfs_init(void);
void r2net_debugfs_exit(void);
void r2net_debug_add_nst(struct r2net_send_tracking *nst);
void r2net_debug_del_nst(struct r2net_send_tracking *nst);
void r2net_debug_add_sc(struct r2net_sock_container *sc);
void r2net_debug_del_sc(struct r2net_sock_container *sc);
#else
static inline int r2net_debugfs_init(void)
{
	return 0;
}
static inline void r2net_debugfs_exit(void)
{
}
static inline void r2net_debug_add_nst(struct r2net_send_tracking *nst)
{
}
static inline void r2net_debug_del_nst(struct r2net_send_tracking *nst)
{
}
static inline void r2net_debug_add_sc(struct r2net_sock_container *sc)
{
}
static inline void r2net_debug_del_sc(struct r2net_sock_container *sc)
{
}
#endif	/* CONFIG_DEBUG_FS */

#endif /* R2CLUSTER_TCP_H */
