/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARM_MACH_TEGRA_RPC_H
#define __ARM_MACH_TEGRA_RPC_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/tegra_rpc.h>

struct trpc_endpoint;
struct trpc_ep_ops {
	/* send is allowed to sleep */
	int	(*send)(struct trpc_endpoint *ep, void *buf, size_t len);
	/* notify_recv is NOT allowed to sleep */
	void	(*notify_recv)(struct trpc_endpoint *ep);
	/* close is allowed to sleep */
	void	(*close)(struct trpc_endpoint *ep);
	/* not allowed to sleep, not allowed to call back into trpc */
	void	(*show)(struct seq_file *s, struct trpc_endpoint *ep);
};

enum {
	TRPC_NODE_LOCAL,
	TRPC_NODE_REMOTE,
};

struct trpc_node {
	struct list_head	list;
	const char		*name;
	int			type;
	void			*priv;

	int			(*try_connect)(struct trpc_node *node,
					       struct trpc_node *src,
					       struct trpc_endpoint *from);
};

struct trpc_endpoint *trpc_peer(struct trpc_endpoint *ep);
void *trpc_priv(struct trpc_endpoint *ep);
const char *trpc_name(struct trpc_endpoint *ep);

void trpc_put(struct trpc_endpoint *ep);
void trpc_get(struct trpc_endpoint *ep);

int trpc_send_msg(struct trpc_node *src, struct trpc_endpoint *ep, void *buf,
		  size_t len, gfp_t gfp_flags);
int trpc_recv_msg(struct trpc_node *src, struct trpc_endpoint *ep,
		  void *buf, size_t len, long timeout);
struct trpc_endpoint *trpc_create(struct trpc_node *owner, const char *name,
				  struct trpc_ep_ops *ops, void *priv);
struct trpc_endpoint *trpc_create_connect(struct trpc_node *src, char *name,
					  struct trpc_ep_ops *ops, void *priv,
					  long timeout);
int trpc_connect(struct trpc_endpoint *from, long timeout);
struct trpc_endpoint *trpc_create_peer(struct trpc_node *owner,
				       struct trpc_endpoint *ep,
				       struct trpc_ep_ops *ops,
				       void *priv);
void trpc_close(struct trpc_endpoint *ep);
int trpc_wait_peer(struct trpc_endpoint *ep, long timeout);

int trpc_node_register(struct trpc_node *node);
void trpc_node_unregister(struct trpc_node *node);

#endif
