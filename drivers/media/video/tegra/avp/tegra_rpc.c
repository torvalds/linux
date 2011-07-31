/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
 *
 * Based on original NVRM code from NVIDIA, and a partial rewrite by:
 *   Gary King <gking@nvidia.com>
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

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/tegra_rpc.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "trpc.h"

struct trpc_port;
struct trpc_endpoint {
	struct list_head	msg_list;
	wait_queue_head_t	msg_waitq;

	struct trpc_endpoint	*out;
	struct trpc_port	*port;

	struct trpc_node	*owner;

	struct completion	*connect_done;
	bool			ready;
	struct trpc_ep_ops	*ops;
	void			*priv;
};

struct trpc_port {
	char			name[TEGRA_RPC_MAX_NAME_LEN];

	/* protects peer and closed state */
	spinlock_t		lock;
	struct trpc_endpoint	peers[2];
	bool			closed;

	/* private */
	struct kref		ref;
	struct rb_node		rb_node;
};

enum {
	TRPC_TRACE_MSG	= 1U << 0,
	TRPC_TRACE_CONN	= 1U << 1,
	TRPC_TRACE_PORT	= 1U << 2,
};

static u32 trpc_debug_mask = 0;
module_param_named(debug_mask, trpc_debug_mask, uint, S_IWUSR | S_IRUGO);

#define DBG(flag, args...) \
	do { if (trpc_debug_mask & (flag)) pr_info(args); } while (0)

struct tegra_rpc_info {
	struct kmem_cache		*msg_cache;

	spinlock_t			ports_lock;
	struct rb_root			ports;

	struct list_head		node_list;
	struct mutex			node_lock;
};

struct trpc_msg {
	struct list_head		list;

	size_t				len;
	u8				payload[TEGRA_RPC_MAX_MSG_LEN];
};

static struct tegra_rpc_info *tegra_rpc;
static struct dentry *trpc_debug_root;

static struct trpc_msg *dequeue_msg_locked(struct trpc_endpoint *ep);

/* a few accessors for the outside world to keep the trpc_endpoint struct
 * definition private to this module */
void *trpc_priv(struct trpc_endpoint *ep)
{
	return ep->priv;
}

struct trpc_endpoint *trpc_peer(struct trpc_endpoint *ep)
{
	return ep->out;
}

const char *trpc_name(struct trpc_endpoint *ep)
{
	return ep->port->name;
}

static inline bool is_connected(struct trpc_port *port)
{
	return port->peers[0].ready && port->peers[1].ready;
}

static inline bool is_closed(struct trpc_port *port)
{
	return port->closed;
}

static void rpc_port_free(struct tegra_rpc_info *info, struct trpc_port *port)
{
	struct trpc_msg *msg;
	int i;

	for (i = 0; i < 2; ++i) {
		struct list_head *list = &port->peers[i].msg_list;
		while (!list_empty(list)) {
			msg = list_first_entry(list, struct trpc_msg, list);
			list_del(&msg->list);
			kmem_cache_free(info->msg_cache, msg);
		}
	}
	kfree(port);
}

static void _rpc_port_release(struct kref *kref)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_port *port = container_of(kref, struct trpc_port, ref);
	unsigned long flags;

	DBG(TRPC_TRACE_PORT, "%s: releasing port '%s' (%p)\n", __func__,
	    port->name, port);
	spin_lock_irqsave(&info->ports_lock, flags);
	rb_erase(&port->rb_node, &info->ports);
	spin_unlock_irqrestore(&info->ports_lock, flags);
	rpc_port_free(info, port);
}

/* note that the refcount is actually on the port and not on the endpoint */
void trpc_put(struct trpc_endpoint *ep)
{
	kref_put(&ep->port->ref, _rpc_port_release);
}

void trpc_get(struct trpc_endpoint *ep)
{
	kref_get(&ep->port->ref);
}

/* Searches the rb_tree for a port with the provided name. If one is not found,
 * the new port in inserted. Otherwise, the existing port is returned.
 * Must be called with the ports_lock held */
static struct trpc_port *rpc_port_find_insert(struct tegra_rpc_info *info,
					      struct trpc_port *port)
{
	struct rb_node **p;
	struct rb_node *parent;
	struct trpc_port *tmp;
	int ret = 0;

	p = &info->ports.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		tmp = rb_entry(parent, struct trpc_port, rb_node);

		ret = strncmp(port->name, tmp->name, TEGRA_RPC_MAX_NAME_LEN);
		if (ret < 0)
			p = &(*p)->rb_left;
		else if (ret > 0)
			p = &(*p)->rb_right;
		else
			return tmp;
	}
	rb_link_node(&port->rb_node, parent, p);
	rb_insert_color(&port->rb_node, &info->ports);
	DBG(TRPC_TRACE_PORT, "%s: inserted port '%s' (%p)\n", __func__,
	    port->name, port);
	return port;
}

static int nodes_try_connect(struct tegra_rpc_info *info,
			     struct trpc_node *src,
			     struct trpc_endpoint *from)
{
	struct trpc_node *node;
	int ret;

	mutex_lock(&info->node_lock);
	list_for_each_entry(node, &info->node_list, list) {
		if (!node->try_connect)
			continue;
		ret = node->try_connect(node, src, from);
		if (!ret) {
			mutex_unlock(&info->node_lock);
			return 0;
		}
	}
	mutex_unlock(&info->node_lock);
	return -ECONNREFUSED;
}

static struct trpc_port *rpc_port_alloc(const char *name)
{
	struct trpc_port *port;
	int i;

	port = kzalloc(sizeof(struct trpc_port), GFP_KERNEL);
	if (!port) {
		pr_err("%s: can't alloc rpc_port\n", __func__);
		return NULL;
	}
	BUILD_BUG_ON(2 != ARRAY_SIZE(port->peers));

	spin_lock_init(&port->lock);
	kref_init(&port->ref);
	strlcpy(port->name, name, TEGRA_RPC_MAX_NAME_LEN);
	for (i = 0; i < 2; i++) {
		struct trpc_endpoint *ep = port->peers + i;
		INIT_LIST_HEAD(&ep->msg_list);
		init_waitqueue_head(&ep->msg_waitq);
		ep->port = port;
	}
	port->peers[0].out = &port->peers[1];
	port->peers[1].out = &port->peers[0];

	return port;
}

/* must be holding the ports lock */
static inline void handle_port_connected(struct trpc_port *port)
{
	int i;

	DBG(TRPC_TRACE_CONN, "tegra_rpc: port '%s' connected\n", port->name);

	for (i = 0; i < 2; i++)
		if (port->peers[i].connect_done)
			complete(port->peers[i].connect_done);
}

static inline void _ready_ep(struct trpc_endpoint *ep,
			     struct trpc_node *owner,
			     struct trpc_ep_ops *ops,
			     void *priv)
{
	ep->ready = true;
	ep->owner = owner;
	ep->ops = ops;
	ep->priv = priv;
}

/* this keeps a reference on the port */
static struct trpc_endpoint *_create_peer(struct tegra_rpc_info *info,
					  struct trpc_node *owner,
					  struct trpc_endpoint *ep,
					  struct trpc_ep_ops *ops,
					  void *priv)
{
	struct trpc_port *port = ep->port;
	struct trpc_endpoint *peer = ep->out;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	BUG_ON(port->closed);
	if (peer->ready || !ep->ready) {
		peer = NULL;
		goto out;
	}
	_ready_ep(peer, owner, ops, priv);
	if (WARN_ON(!is_connected(port)))
		pr_warning("%s: created peer but no connection established?!\n",
			   __func__);
	else
		handle_port_connected(port);
	trpc_get(peer);
out:
	spin_unlock_irqrestore(&port->lock, flags);
	return peer;
}

/* Exported code. This is out interface to the outside world */
struct trpc_endpoint *trpc_create(struct trpc_node *owner, const char *name,
				  struct trpc_ep_ops *ops, void *priv)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_endpoint *ep;
	struct trpc_port *new_port;
	struct trpc_port *port;
	unsigned long flags;

	BUG_ON(!owner);

	/* we always allocate a new port even if one already might exist. This
	 * is slightly inefficient, but it allows us to do the allocation
	 * without holding our ports_lock spinlock. */
	new_port = rpc_port_alloc(name);
	if (!new_port) {
		pr_err("%s: can't allocate memory for '%s'\n", __func__, name);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_irqsave(&info->ports_lock, flags);
	port = rpc_port_find_insert(info, new_port);
	if (port != new_port) {
		rpc_port_free(info, new_port);
		/* There was already a port by that name in the rb_tree,
		 * so just try to create its peer[1], i.e. peer for peer[0]
		 */
		ep = _create_peer(info, owner, &port->peers[0], ops, priv);
		if (!ep) {
			pr_err("%s: port '%s' is not in a connectable state\n",
			       __func__, port->name);
			ep = ERR_PTR(-EINVAL);
		}
		goto out;
	}
	/* don't need to grab the individual port lock here since we must be
	 * holding the ports_lock to add the new element, and never dropped
	 * it, and thus noone could have gotten a reference to this port
	 * and thus the state couldn't have been touched */
	ep = &port->peers[0];
	_ready_ep(ep, owner, ops, priv);
out:
	spin_unlock_irqrestore(&info->ports_lock, flags);
	return ep;
}

struct trpc_endpoint *trpc_create_peer(struct trpc_node *owner,
				       struct trpc_endpoint *ep,
				       struct trpc_ep_ops *ops,
				       void *priv)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_endpoint *peer;
	unsigned long flags;

	BUG_ON(!owner);

	spin_lock_irqsave(&info->ports_lock, flags);
	peer = _create_peer(info, owner, ep, ops, priv);
	spin_unlock_irqrestore(&info->ports_lock, flags);
	return peer;
}

/* timeout == -1, waits forever
 * timeout == 0, return immediately
 */
int trpc_connect(struct trpc_endpoint *from, long timeout)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_port *port = from->port;
	struct trpc_node *src = from->owner;
	int ret;
	bool no_retry = !timeout;
	unsigned long endtime = jiffies + msecs_to_jiffies(timeout);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	/* XXX: add state for connections and ports to prevent invalid
	 * states like multiple connections, etc. ? */
	if (unlikely(is_closed(port))) {
		ret = -ECONNRESET;
		pr_err("%s: can't connect to %s, closed\n", __func__,
		       port->name);
		goto out;
	} else if (is_connected(port)) {
		ret = 0;
		goto out;
	}
	spin_unlock_irqrestore(&port->lock, flags);

	do {
		ret = nodes_try_connect(info, src, from);

		spin_lock_irqsave(&port->lock, flags);
		if (is_connected(port)) {
			ret = 0;
			goto out;
		} else if (no_retry) {
			goto out;
		} else if (signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		spin_unlock_irqrestore(&port->lock, flags);
		usleep_range(5000, 20000);
	} while (timeout < 0 || time_before(jiffies, endtime));

	return -ETIMEDOUT;

out:
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

/* convenience function for doing this common pattern in a single call */
struct trpc_endpoint *trpc_create_connect(struct trpc_node *src,
					  char *name,
					  struct trpc_ep_ops *ops,
					  void *priv,
					  long timeout)
{
	struct trpc_endpoint *ep;
	int ret;

	ep = trpc_create(src, name, ops, priv);
	if (IS_ERR(ep))
		return ep;

	ret = trpc_connect(ep, timeout);
	if (ret) {
		trpc_close(ep);
		return ERR_PTR(ret);
	}

	return ep;
}

void trpc_close(struct trpc_endpoint *ep)
{
	struct trpc_port *port = ep->port;
	struct trpc_endpoint *peer = ep->out;
	bool need_close_op = false;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	BUG_ON(!ep->ready);
	ep->ready = false;
	port->closed = true;
	if (peer->ready) {
		need_close_op = true;
		/* the peer may be waiting for a message */
		wake_up_all(&peer->msg_waitq);
		if (peer->connect_done)
			complete(peer->connect_done);
	}
	spin_unlock_irqrestore(&port->lock, flags);
	if (need_close_op && peer->ops && peer->ops->close)
		peer->ops->close(peer);
	trpc_put(ep);
}

int trpc_wait_peer(struct trpc_endpoint *ep, long timeout)
{
	struct trpc_port *port = ep->port;
	DECLARE_COMPLETION_ONSTACK(event);
	int ret;
	unsigned long flags;

	if (timeout < 0)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else if (timeout > 0)
		timeout = msecs_to_jiffies(timeout);

	spin_lock_irqsave(&port->lock, flags);
	if (ep->connect_done) {
		ret = -EBUSY;
		goto done;
	} else if (is_connected(port)) {
		ret = 0;
		goto done;
	} else if (is_closed(port)) {
		ret = -ECONNRESET;
		goto done;
	} else if (!timeout) {
		ret = -EAGAIN;
		goto done;
	}
	ep->connect_done = &event;
	spin_unlock_irqrestore(&port->lock, flags);

	ret = wait_for_completion_interruptible_timeout(&event, timeout);

	spin_lock_irqsave(&port->lock, flags);
	ep->connect_done = NULL;

	if (is_connected(port)) {
		ret = 0;
	} else {
		if (is_closed(port))
			ret = -ECONNRESET;
		else if (ret == -ERESTARTSYS)
			ret = -EINTR;
		else if (!ret)
			ret = -ETIMEDOUT;
	}

done:
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

static inline int _ep_id(struct trpc_endpoint *ep)
{
	return ep - ep->port->peers;
}

static int queue_msg(struct trpc_node *src, struct trpc_endpoint *from,
		     void *buf, size_t len, gfp_t gfp_flags)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_endpoint *peer = from->out;
	struct trpc_port *port = from->port;
	struct trpc_msg *msg;
	unsigned long flags;
	int ret;

	BUG_ON(len > TEGRA_RPC_MAX_MSG_LEN);
	/* shouldn't be enqueueing to the endpoint */
	BUG_ON(peer->ops && peer->ops->send);

	DBG(TRPC_TRACE_MSG, "%s: queueing message for %s.%d\n", __func__,
	    port->name, _ep_id(peer));

	msg = kmem_cache_alloc(info->msg_cache, gfp_flags);
	if (!msg) {
		pr_err("%s: can't alloc memory for msg\n", __func__);
		return -ENOMEM;
	}

	memcpy(msg->payload, buf, len);
	msg->len = len;

	spin_lock_irqsave(&port->lock, flags);
	if (is_closed(port)) {
		pr_err("%s: cannot send message for closed port %s.%d\n",
		       __func__, port->name, _ep_id(peer));
		ret = -ECONNRESET;
		goto err;
	} else if (!is_connected(port)) {
		pr_err("%s: cannot send message for unconnected port %s.%d\n",
		       __func__, port->name, _ep_id(peer));
		ret = -ENOTCONN;
		goto err;
	}

	list_add_tail(&msg->list, &peer->msg_list);
	if (peer->ops && peer->ops->notify_recv)
		peer->ops->notify_recv(peer);
	wake_up_all(&peer->msg_waitq);
	spin_unlock_irqrestore(&port->lock, flags);
	return 0;

err:
	spin_unlock_irqrestore(&port->lock, flags);
	kmem_cache_free(info->msg_cache, msg);
	return ret;
}

/* Returns -ENOMEM if failed to allocate memory for the message. */
int trpc_send_msg(struct trpc_node *src, struct trpc_endpoint *from,
		  void *buf, size_t len, gfp_t gfp_flags)
{
	struct trpc_endpoint *peer = from->out;
	struct trpc_port *port = from->port;

	BUG_ON(len > TEGRA_RPC_MAX_MSG_LEN);

	DBG(TRPC_TRACE_MSG, "%s: sending message from %s.%d to %s.%d\n",
	    __func__, port->name, _ep_id(from), port->name, _ep_id(peer));

	if (peer->ops && peer->ops->send) {
		might_sleep();
		return peer->ops->send(peer, buf, len);
	} else {
		might_sleep_if(gfp_flags & __GFP_WAIT);
		return queue_msg(src, from, buf, len, gfp_flags);
	}
}

static inline struct trpc_msg *dequeue_msg_locked(struct trpc_endpoint *ep)
{
	struct trpc_msg *msg = NULL;

	if (!list_empty(&ep->msg_list)) {
		msg = list_first_entry(&ep->msg_list, struct trpc_msg, list);
		list_del_init(&msg->list);
	}

	return msg;
}

static bool __should_wake(struct trpc_endpoint *ep)
{
	struct trpc_port *port = ep->port;
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&port->lock, flags);
	ret = !list_empty(&ep->msg_list) || is_closed(port);
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

int trpc_recv_msg(struct trpc_node *src, struct trpc_endpoint *ep,
		  void *buf, size_t buf_len, long timeout)
{
	struct tegra_rpc_info *info = tegra_rpc;
	struct trpc_port *port = ep->port;
	struct trpc_msg *msg;
	size_t len;
	long ret;
	unsigned long flags;

	BUG_ON(buf_len > TEGRA_RPC_MAX_MSG_LEN);

	spin_lock_irqsave(&port->lock, flags);
	/* we allow closed ports to finish receiving already-queued messages */
	msg = dequeue_msg_locked(ep);
	if (msg) {
		goto got_msg;
	} else if (is_closed(port)) {
		ret = -ECONNRESET;
		goto out;
	} else if (!is_connected(port)) {
		ret = -ENOTCONN;
		goto out;
	}

	if (timeout == 0) {
		ret = 0;
		goto out;
	} else if (timeout < 0) {
		timeout = MAX_SCHEDULE_TIMEOUT;
	} else {
		timeout = msecs_to_jiffies(timeout);
	}
	spin_unlock_irqrestore(&port->lock, flags);
	DBG(TRPC_TRACE_MSG, "%s: waiting for message for %s.%d\n", __func__,
	    port->name, _ep_id(ep));

	ret = wait_event_interruptible_timeout(ep->msg_waitq, __should_wake(ep),
					       timeout);

	DBG(TRPC_TRACE_MSG, "%s: woke up for %s\n", __func__, port->name);
	spin_lock_irqsave(&port->lock, flags);
	msg = dequeue_msg_locked(ep);
	if (!msg) {
		if (is_closed(port))
			ret = -ECONNRESET;
		else if (!ret)
			ret = -ETIMEDOUT;
		else if (ret == -ERESTARTSYS)
			ret = -EINTR;
		else
			pr_err("%s: error (%d) while receiving msg for '%s'\n",
			       __func__, (int)ret, port->name);
		goto out;
	}

got_msg:
	spin_unlock_irqrestore(&port->lock, flags);
	len = min(buf_len, msg->len);
	memcpy(buf, msg->payload, len);
	kmem_cache_free(info->msg_cache, msg);
	return len;

out:
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

int trpc_node_register(struct trpc_node *node)
{
	struct tegra_rpc_info *info = tegra_rpc;

	if (!info)
		return -ENOMEM;

	pr_info("%s: Adding '%s' to node list\n", __func__, node->name);

	mutex_lock(&info->node_lock);
	if (node->type == TRPC_NODE_LOCAL)
		list_add(&node->list, &info->node_list);
	else
		list_add_tail(&node->list, &info->node_list);
	mutex_unlock(&info->node_lock);
	return 0;
}

void trpc_node_unregister(struct trpc_node *node)
{
	struct tegra_rpc_info *info = tegra_rpc;

	mutex_lock(&info->node_lock);
	list_del(&node->list);
	mutex_unlock(&info->node_lock);
}

static int trpc_debug_ports_show(struct seq_file *s, void *data)
{
	struct tegra_rpc_info *info = s->private;
	struct rb_node *n;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&info->ports_lock, flags);
	for (n = rb_first(&info->ports); n; n = rb_next(n)) {
		struct trpc_port *port = rb_entry(n, struct trpc_port, rb_node);
		seq_printf(s, "port: %s\n closed:%s\n", port->name,
			   port->closed ? "yes" : "no");

		spin_lock(&port->lock);
		for (i = 0; i < ARRAY_SIZE(port->peers); i++) {
			struct trpc_endpoint *ep = &port->peers[i];
			seq_printf(s, "  peer%d: %s\n    ready:%s\n", i,
				   ep->owner ? ep->owner->name: "<none>",
				   ep->ready ? "yes" : "no");
			if (ep->ops && ep->ops->show)
				ep->ops->show(s, ep);
		}
		spin_unlock(&port->lock);
	}
	spin_unlock_irqrestore(&info->ports_lock, flags);

	return 0;
}

static int trpc_debug_ports_open(struct inode *inode, struct file *file)
{
	return single_open(file, trpc_debug_ports_show, inode->i_private);
}

static struct file_operations trpc_debug_ports_fops = {
	.open = trpc_debug_ports_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void trpc_debug_init(struct tegra_rpc_info *info)
{
	trpc_debug_root = debugfs_create_dir("tegra_rpc", NULL);
	if (IS_ERR_OR_NULL(trpc_debug_root)) {
		pr_err("%s: couldn't create debug files\n", __func__);
		return;
	}

	debugfs_create_file("ports", 0664, trpc_debug_root, info,
			    &trpc_debug_ports_fops);
}

static int __init tegra_rpc_init(void)
{
	struct tegra_rpc_info *rpc_info;
	int ret;

	rpc_info = kzalloc(sizeof(struct tegra_rpc_info), GFP_KERNEL);
	if (!rpc_info) {
		pr_err("%s: error allocating rpc_info\n", __func__);
		return -ENOMEM;
	}

	rpc_info->ports = RB_ROOT;
	spin_lock_init(&rpc_info->ports_lock);
	INIT_LIST_HEAD(&rpc_info->node_list);
	mutex_init(&rpc_info->node_lock);

	rpc_info->msg_cache = KMEM_CACHE(trpc_msg, 0);
	if (!rpc_info->msg_cache) {
		pr_err("%s: unable to create message cache\n", __func__);
		ret = -ENOMEM;
		goto err_kmem_cache;
	}

	trpc_debug_init(rpc_info);
	tegra_rpc = rpc_info;

	return 0;

err_kmem_cache:
	kfree(rpc_info);
	return ret;
}

subsys_initcall(tegra_rpc_init);
