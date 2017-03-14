/*
 * Xenbus code for netif backend
 *
 * Copyright (C) 2005 Rusty Russell <rusty@rustcorp.com.au>
 * Copyright (C) 2005 XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "common.h"
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>

struct backend_info {
	struct xenbus_device *dev;
	struct xenvif *vif;

	/* This is the state that will be reflected in xenstore when any
	 * active hotplug script completes.
	 */
	enum xenbus_state state;

	enum xenbus_state frontend_state;
	struct xenbus_watch hotplug_status_watch;
	u8 have_hotplug_status_watch:1;

	const char *hotplug_script;
};

static int connect_data_rings(struct backend_info *be,
			      struct xenvif_queue *queue);
static void connect(struct backend_info *be);
static int read_xenbus_vif_flags(struct backend_info *be);
static int backend_create_xenvif(struct backend_info *be);
static void unregister_hotplug_status_watch(struct backend_info *be);
static void xen_unregister_watchers(struct xenvif *vif);
static void set_backend_state(struct backend_info *be,
			      enum xenbus_state state);

#ifdef CONFIG_DEBUG_FS
struct dentry *xen_netback_dbg_root = NULL;

static int xenvif_read_io_ring(struct seq_file *m, void *v)
{
	struct xenvif_queue *queue = m->private;
	struct xen_netif_tx_back_ring *tx_ring = &queue->tx;
	struct xen_netif_rx_back_ring *rx_ring = &queue->rx;
	struct netdev_queue *dev_queue;

	if (tx_ring->sring) {
		struct xen_netif_tx_sring *sring = tx_ring->sring;

		seq_printf(m, "Queue %d\nTX: nr_ents %u\n", queue->id,
			   tx_ring->nr_ents);
		seq_printf(m, "req prod %u (%d) cons %u (%d) event %u (%d)\n",
			   sring->req_prod,
			   sring->req_prod - sring->rsp_prod,
			   tx_ring->req_cons,
			   tx_ring->req_cons - sring->rsp_prod,
			   sring->req_event,
			   sring->req_event - sring->rsp_prod);
		seq_printf(m, "rsp prod %u (base) pvt %u (%d) event %u (%d)\n",
			   sring->rsp_prod,
			   tx_ring->rsp_prod_pvt,
			   tx_ring->rsp_prod_pvt - sring->rsp_prod,
			   sring->rsp_event,
			   sring->rsp_event - sring->rsp_prod);
		seq_printf(m, "pending prod %u pending cons %u nr_pending_reqs %u\n",
			   queue->pending_prod,
			   queue->pending_cons,
			   nr_pending_reqs(queue));
		seq_printf(m, "dealloc prod %u dealloc cons %u dealloc_queue %u\n\n",
			   queue->dealloc_prod,
			   queue->dealloc_cons,
			   queue->dealloc_prod - queue->dealloc_cons);
	}

	if (rx_ring->sring) {
		struct xen_netif_rx_sring *sring = rx_ring->sring;

		seq_printf(m, "RX: nr_ents %u\n", rx_ring->nr_ents);
		seq_printf(m, "req prod %u (%d) cons %u (%d) event %u (%d)\n",
			   sring->req_prod,
			   sring->req_prod - sring->rsp_prod,
			   rx_ring->req_cons,
			   rx_ring->req_cons - sring->rsp_prod,
			   sring->req_event,
			   sring->req_event - sring->rsp_prod);
		seq_printf(m, "rsp prod %u (base) pvt %u (%d) event %u (%d)\n\n",
			   sring->rsp_prod,
			   rx_ring->rsp_prod_pvt,
			   rx_ring->rsp_prod_pvt - sring->rsp_prod,
			   sring->rsp_event,
			   sring->rsp_event - sring->rsp_prod);
	}

	seq_printf(m, "NAPI state: %lx NAPI weight: %d TX queue len %u\n"
		   "Credit timer_pending: %d, credit: %lu, usec: %lu\n"
		   "remaining: %lu, expires: %lu, now: %lu\n",
		   queue->napi.state, queue->napi.weight,
		   skb_queue_len(&queue->tx_queue),
		   timer_pending(&queue->credit_timeout),
		   queue->credit_bytes,
		   queue->credit_usec,
		   queue->remaining_credit,
		   queue->credit_timeout.expires,
		   jiffies);

	dev_queue = netdev_get_tx_queue(queue->vif->dev, queue->id);

	seq_printf(m, "\nRx internal queue: len %u max %u pkts %u %s\n",
		   queue->rx_queue_len, queue->rx_queue_max,
		   skb_queue_len(&queue->rx_queue),
		   netif_tx_queue_stopped(dev_queue) ? "stopped" : "running");

	return 0;
}

#define XENVIF_KICK_STR "kick"
#define BUFFER_SIZE     32

static ssize_t
xenvif_write_io_ring(struct file *filp, const char __user *buf, size_t count,
		     loff_t *ppos)
{
	struct xenvif_queue *queue =
		((struct seq_file *)filp->private_data)->private;
	int len;
	char write[BUFFER_SIZE];

	/* don't allow partial writes and check the length */
	if (*ppos != 0)
		return 0;
	if (count >= sizeof(write))
		return -ENOSPC;

	len = simple_write_to_buffer(write,
				     sizeof(write) - 1,
				     ppos,
				     buf,
				     count);
	if (len < 0)
		return len;

	write[len] = '\0';

	if (!strncmp(write, XENVIF_KICK_STR, sizeof(XENVIF_KICK_STR) - 1))
		xenvif_interrupt(0, (void *)queue);
	else {
		pr_warn("Unknown command to io_ring_q%d. Available: kick\n",
			queue->id);
		count = -EINVAL;
	}
	return count;
}

static int xenvif_io_ring_open(struct inode *inode, struct file *filp)
{
	int ret;
	void *queue = NULL;

	if (inode->i_private)
		queue = inode->i_private;
	ret = single_open(filp, xenvif_read_io_ring, queue);
	filp->f_mode |= FMODE_PWRITE;
	return ret;
}

static const struct file_operations xenvif_dbg_io_ring_ops_fops = {
	.owner = THIS_MODULE,
	.open = xenvif_io_ring_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = xenvif_write_io_ring,
};

static int xenvif_read_ctrl(struct seq_file *m, void *v)
{
	struct xenvif *vif = m->private;

	xenvif_dump_hash_info(vif, m);

	return 0;
}

static int xenvif_ctrl_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, xenvif_read_ctrl, inode->i_private);
}

static const struct file_operations xenvif_dbg_ctrl_ops_fops = {
	.owner = THIS_MODULE,
	.open = xenvif_ctrl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void xenvif_debugfs_addif(struct xenvif *vif)
{
	struct dentry *pfile;
	int i;

	if (IS_ERR_OR_NULL(xen_netback_dbg_root))
		return;

	vif->xenvif_dbg_root = debugfs_create_dir(vif->dev->name,
						  xen_netback_dbg_root);
	if (!IS_ERR_OR_NULL(vif->xenvif_dbg_root)) {
		for (i = 0; i < vif->num_queues; ++i) {
			char filename[sizeof("io_ring_q") + 4];

			snprintf(filename, sizeof(filename), "io_ring_q%d", i);
			pfile = debugfs_create_file(filename,
						    S_IRUSR | S_IWUSR,
						    vif->xenvif_dbg_root,
						    &vif->queues[i],
						    &xenvif_dbg_io_ring_ops_fops);
			if (IS_ERR_OR_NULL(pfile))
				pr_warn("Creation of io_ring file returned %ld!\n",
					PTR_ERR(pfile));
		}

		if (vif->ctrl_irq) {
			pfile = debugfs_create_file("ctrl",
						    S_IRUSR,
						    vif->xenvif_dbg_root,
						    vif,
						    &xenvif_dbg_ctrl_ops_fops);
			if (IS_ERR_OR_NULL(pfile))
				pr_warn("Creation of ctrl file returned %ld!\n",
					PTR_ERR(pfile));
		}
	} else
		netdev_warn(vif->dev,
			    "Creation of vif debugfs dir returned %ld!\n",
			    PTR_ERR(vif->xenvif_dbg_root));
}

static void xenvif_debugfs_delif(struct xenvif *vif)
{
	if (IS_ERR_OR_NULL(xen_netback_dbg_root))
		return;

	if (!IS_ERR_OR_NULL(vif->xenvif_dbg_root))
		debugfs_remove_recursive(vif->xenvif_dbg_root);
	vif->xenvif_dbg_root = NULL;
}
#endif /* CONFIG_DEBUG_FS */

static int netback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	set_backend_state(be, XenbusStateClosed);

	unregister_hotplug_status_watch(be);
	if (be->vif) {
		kobject_uevent(&dev->dev.kobj, KOBJ_OFFLINE);
		xen_unregister_watchers(be->vif);
		xenbus_rm(XBT_NIL, dev->nodename, "hotplug-status");
		xenvif_free(be->vif);
		be->vif = NULL;
	}
	kfree(be->hotplug_script);
	kfree(be);
	dev_set_drvdata(&dev->dev, NULL);
	return 0;
}


/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and switch to InitWait.
 */
static int netback_probe(struct xenbus_device *dev,
			 const struct xenbus_device_id *id)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;
	int sg;
	const char *script;
	struct backend_info *be = kzalloc(sizeof(struct backend_info),
					  GFP_KERNEL);
	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}

	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);

	be->state = XenbusStateInitialising;
	err = xenbus_switch_state(dev, XenbusStateInitialising);
	if (err)
		goto fail;

	sg = 1;

	do {
		err = xenbus_transaction_start(&xbt);
		if (err) {
			xenbus_dev_fatal(dev, err, "starting transaction");
			goto fail;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-sg", "%d", sg);
		if (err) {
			message = "writing feature-sg";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv4",
				    "%d", sg);
		if (err) {
			message = "writing feature-gso-tcpv4";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv6",
				    "%d", sg);
		if (err) {
			message = "writing feature-gso-tcpv6";
			goto abort_transaction;
		}

		/* We support partial checksum setup for IPv6 packets */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-ipv6-csum-offload",
				    "%d", 1);
		if (err) {
			message = "writing feature-ipv6-csum-offload";
			goto abort_transaction;
		}

		/* We support rx-copy path. */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-rx-copy", "%d", 1);
		if (err) {
			message = "writing feature-rx-copy";
			goto abort_transaction;
		}

		/*
		 * We don't support rx-flip path (except old guests who don't
		 * grok this feature flag).
		 */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-rx-flip", "%d", 0);
		if (err) {
			message = "writing feature-rx-flip";
			goto abort_transaction;
		}

		/* We support dynamic multicast-control. */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-multicast-control", "%d", 1);
		if (err) {
			message = "writing feature-multicast-control";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename,
				    "feature-dynamic-multicast-control",
				    "%d", 1);
		if (err) {
			message = "writing feature-dynamic-multicast-control";
			goto abort_transaction;
		}

		err = xenbus_transaction_end(xbt, 0);
	} while (err == -EAGAIN);

	if (err) {
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto fail;
	}

	/*
	 * Split event channels support, this is optional so it is not
	 * put inside the above loop.
	 */
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-split-event-channels",
			    "%u", separate_tx_rx_irq);
	if (err)
		pr_debug("Error writing feature-split-event-channels\n");

	/* Multi-queue support: This is an optional feature. */
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "multi-queue-max-queues", "%u", xenvif_max_queues);
	if (err)
		pr_debug("Error writing multi-queue-max-queues\n");

	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-ctrl-ring",
			    "%u", true);
	if (err)
		pr_debug("Error writing feature-ctrl-ring\n");

	script = xenbus_read(XBT_NIL, dev->nodename, "script", NULL);
	if (IS_ERR(script)) {
		err = PTR_ERR(script);
		xenbus_dev_fatal(dev, err, "reading script");
		goto fail;
	}

	be->hotplug_script = script;


	/* This kicks hotplug scripts, so do it immediately. */
	err = backend_create_xenvif(be);
	if (err)
		goto fail;

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);
fail:
	pr_debug("failed\n");
	netback_remove(dev);
	return err;
}


/*
 * Handle the creation of the hotplug script environment.  We add the script
 * and vif variables to the environment, for the benefit of the vif-* hotplug
 * scripts.
 */
static int netback_uevent(struct xenbus_device *xdev,
			  struct kobj_uevent_env *env)
{
	struct backend_info *be = dev_get_drvdata(&xdev->dev);

	if (!be)
		return 0;

	if (add_uevent_var(env, "script=%s", be->hotplug_script))
		return -ENOMEM;

	if (!be->vif)
		return 0;

	return add_uevent_var(env, "vif=%s", be->vif->dev->name);
}


static int backend_create_xenvif(struct backend_info *be)
{
	int err;
	long handle;
	struct xenbus_device *dev = be->dev;
	struct xenvif *vif;

	if (be->vif != NULL)
		return 0;

	err = xenbus_scanf(XBT_NIL, dev->nodename, "handle", "%li", &handle);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading handle");
		return (err < 0) ? err : -EINVAL;
	}

	vif = xenvif_alloc(&dev->dev, dev->otherend_id, handle);
	if (IS_ERR(vif)) {
		err = PTR_ERR(vif);
		xenbus_dev_fatal(dev, err, "creating interface");
		return err;
	}
	be->vif = vif;

	kobject_uevent(&dev->dev.kobj, KOBJ_ONLINE);
	return 0;
}

static void backend_disconnect(struct backend_info *be)
{
	struct xenvif *vif = be->vif;

	if (vif) {
		unsigned int queue_index;
		struct xenvif_queue *queues;

		xen_unregister_watchers(vif);
#ifdef CONFIG_DEBUG_FS
		xenvif_debugfs_delif(vif);
#endif /* CONFIG_DEBUG_FS */
		xenvif_disconnect_data(vif);
		for (queue_index = 0;
		     queue_index < vif->num_queues;
		     ++queue_index)
			xenvif_deinit_queue(&vif->queues[queue_index]);

		spin_lock(&vif->lock);
		queues = vif->queues;
		vif->num_queues = 0;
		vif->queues = NULL;
		spin_unlock(&vif->lock);

		vfree(queues);

		xenvif_disconnect_ctrl(vif);
	}
}

static void backend_connect(struct backend_info *be)
{
	if (be->vif)
		connect(be);
}

static inline void backend_switch_state(struct backend_info *be,
					enum xenbus_state state)
{
	struct xenbus_device *dev = be->dev;

	pr_debug("%s -> %s\n", dev->nodename, xenbus_strstate(state));
	be->state = state;

	/* If we are waiting for a hotplug script then defer the
	 * actual xenbus state change.
	 */
	if (!be->have_hotplug_status_watch)
		xenbus_switch_state(dev, state);
}

/* Handle backend state transitions:
 *
 * The backend state starts in Initialising and the following transitions are
 * allowed.
 *
 * Initialising -> InitWait -> Connected
 *          \
 *           \        ^    \         |
 *            \       |     \        |
 *             \      |      \       |
 *              \     |       \      |
 *               \    |        \     |
 *                \   |         \    |
 *                 V  |          V   V
 *
 *                  Closed  <-> Closing
 *
 * The state argument specifies the eventual state of the backend and the
 * function transitions to that state via the shortest path.
 */
static void set_backend_state(struct backend_info *be,
			      enum xenbus_state state)
{
	while (be->state != state) {
		switch (be->state) {
		case XenbusStateInitialising:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
			case XenbusStateClosing:
				backend_switch_state(be, XenbusStateInitWait);
				break;
			case XenbusStateClosed:
				backend_switch_state(be, XenbusStateClosed);
				break;
			default:
				BUG();
			}
			break;
		case XenbusStateClosed:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
				backend_switch_state(be, XenbusStateInitWait);
				break;
			case XenbusStateClosing:
				backend_switch_state(be, XenbusStateClosing);
				break;
			default:
				BUG();
			}
			break;
		case XenbusStateInitWait:
			switch (state) {
			case XenbusStateConnected:
				backend_connect(be);
				backend_switch_state(be, XenbusStateConnected);
				break;
			case XenbusStateClosing:
			case XenbusStateClosed:
				backend_switch_state(be, XenbusStateClosing);
				break;
			default:
				BUG();
			}
			break;
		case XenbusStateConnected:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateClosing:
			case XenbusStateClosed:
				backend_disconnect(be);
				backend_switch_state(be, XenbusStateClosing);
				break;
			default:
				BUG();
			}
			break;
		case XenbusStateClosing:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
			case XenbusStateClosed:
				backend_switch_state(be, XenbusStateClosed);
				break;
			default:
				BUG();
			}
			break;
		default:
			BUG();
		}
	}
}

/**
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	pr_debug("%s -> %s\n", dev->otherend, xenbus_strstate(frontend_state));

	be->frontend_state = frontend_state;

	switch (frontend_state) {
	case XenbusStateInitialising:
		set_backend_state(be, XenbusStateInitWait);
		break;

	case XenbusStateInitialised:
		break;

	case XenbusStateConnected:
		set_backend_state(be, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		set_backend_state(be, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		set_backend_state(be, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		set_backend_state(be, XenbusStateClosed);
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


static void xen_net_read_rate(struct xenbus_device *dev,
			      unsigned long *bytes, unsigned long *usec)
{
	char *s, *e;
	unsigned long b, u;
	char *ratestr;

	/* Default to unlimited bandwidth. */
	*bytes = ~0UL;
	*usec = 0;

	ratestr = xenbus_read(XBT_NIL, dev->nodename, "rate", NULL);
	if (IS_ERR(ratestr))
		return;

	s = ratestr;
	b = simple_strtoul(s, &e, 10);
	if ((s == e) || (*e != ','))
		goto fail;

	s = e + 1;
	u = simple_strtoul(s, &e, 10);
	if ((s == e) || (*e != '\0'))
		goto fail;

	*bytes = b;
	*usec = u;

	kfree(ratestr);
	return;

 fail:
	pr_warn("Failed to parse network rate limit. Traffic unlimited.\n");
	kfree(ratestr);
}

static int xen_net_read_mac(struct xenbus_device *dev, u8 mac[])
{
	char *s, *e, *macstr;
	int i;

	macstr = s = xenbus_read(XBT_NIL, dev->nodename, "mac", NULL);
	if (IS_ERR(macstr))
		return PTR_ERR(macstr);

	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = simple_strtoul(s, &e, 16);
		if ((s == e) || (*e != ((i == ETH_ALEN-1) ? '\0' : ':'))) {
			kfree(macstr);
			return -ENOENT;
		}
		s = e+1;
	}

	kfree(macstr);
	return 0;
}

static void xen_net_rate_changed(struct xenbus_watch *watch,
				 const char *path, const char *token)
{
	struct xenvif *vif = container_of(watch, struct xenvif, credit_watch);
	struct xenbus_device *dev = xenvif_to_xenbus_device(vif);
	unsigned long   credit_bytes;
	unsigned long   credit_usec;
	unsigned int queue_index;

	xen_net_read_rate(dev, &credit_bytes, &credit_usec);
	for (queue_index = 0; queue_index < vif->num_queues; queue_index++) {
		struct xenvif_queue *queue = &vif->queues[queue_index];

		queue->credit_bytes = credit_bytes;
		queue->credit_usec = credit_usec;
		if (!mod_timer_pending(&queue->credit_timeout, jiffies) &&
			queue->remaining_credit > queue->credit_bytes) {
			queue->remaining_credit = queue->credit_bytes;
		}
	}
}

static int xen_register_credit_watch(struct xenbus_device *dev,
				     struct xenvif *vif)
{
	int err = 0;
	char *node;
	unsigned maxlen = strlen(dev->nodename) + sizeof("/rate");

	if (vif->credit_watch.node)
		return -EADDRINUSE;

	node = kmalloc(maxlen, GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	snprintf(node, maxlen, "%s/rate", dev->nodename);
	vif->credit_watch.node = node;
	vif->credit_watch.callback = xen_net_rate_changed;
	err = register_xenbus_watch(&vif->credit_watch);
	if (err) {
		pr_err("Failed to set watcher %s\n", vif->credit_watch.node);
		kfree(node);
		vif->credit_watch.node = NULL;
		vif->credit_watch.callback = NULL;
	}
	return err;
}

static void xen_unregister_credit_watch(struct xenvif *vif)
{
	if (vif->credit_watch.node) {
		unregister_xenbus_watch(&vif->credit_watch);
		kfree(vif->credit_watch.node);
		vif->credit_watch.node = NULL;
	}
}

static void xen_mcast_ctrl_changed(struct xenbus_watch *watch,
				   const char *path, const char *token)
{
	struct xenvif *vif = container_of(watch, struct xenvif,
					  mcast_ctrl_watch);
	struct xenbus_device *dev = xenvif_to_xenbus_device(vif);

	vif->multicast_control = !!xenbus_read_unsigned(dev->otherend,
					"request-multicast-control", 0);
}

static int xen_register_mcast_ctrl_watch(struct xenbus_device *dev,
					 struct xenvif *vif)
{
	int err = 0;
	char *node;
	unsigned maxlen = strlen(dev->otherend) +
		sizeof("/request-multicast-control");

	if (vif->mcast_ctrl_watch.node) {
		pr_err_ratelimited("Watch is already registered\n");
		return -EADDRINUSE;
	}

	node = kmalloc(maxlen, GFP_KERNEL);
	if (!node) {
		pr_err("Failed to allocate memory for watch\n");
		return -ENOMEM;
	}
	snprintf(node, maxlen, "%s/request-multicast-control",
		 dev->otherend);
	vif->mcast_ctrl_watch.node = node;
	vif->mcast_ctrl_watch.callback = xen_mcast_ctrl_changed;
	err = register_xenbus_watch(&vif->mcast_ctrl_watch);
	if (err) {
		pr_err("Failed to set watcher %s\n",
		       vif->mcast_ctrl_watch.node);
		kfree(node);
		vif->mcast_ctrl_watch.node = NULL;
		vif->mcast_ctrl_watch.callback = NULL;
	}
	return err;
}

static void xen_unregister_mcast_ctrl_watch(struct xenvif *vif)
{
	if (vif->mcast_ctrl_watch.node) {
		unregister_xenbus_watch(&vif->mcast_ctrl_watch);
		kfree(vif->mcast_ctrl_watch.node);
		vif->mcast_ctrl_watch.node = NULL;
	}
}

static void xen_register_watchers(struct xenbus_device *dev,
				  struct xenvif *vif)
{
	xen_register_credit_watch(dev, vif);
	xen_register_mcast_ctrl_watch(dev, vif);
}

static void xen_unregister_watchers(struct xenvif *vif)
{
	xen_unregister_mcast_ctrl_watch(vif);
	xen_unregister_credit_watch(vif);
}

static void unregister_hotplug_status_watch(struct backend_info *be)
{
	if (be->have_hotplug_status_watch) {
		unregister_xenbus_watch(&be->hotplug_status_watch);
		kfree(be->hotplug_status_watch.node);
	}
	be->have_hotplug_status_watch = 0;
}

static void hotplug_status_changed(struct xenbus_watch *watch,
				   const char *path,
				   const char *token)
{
	struct backend_info *be = container_of(watch,
					       struct backend_info,
					       hotplug_status_watch);
	char *str;
	unsigned int len;

	str = xenbus_read(XBT_NIL, be->dev->nodename, "hotplug-status", &len);
	if (IS_ERR(str))
		return;
	if (len == sizeof("connected")-1 && !memcmp(str, "connected", len)) {
		/* Complete any pending state change */
		xenbus_switch_state(be->dev, be->state);

		/* Not interested in this watch anymore. */
		unregister_hotplug_status_watch(be);
	}
	kfree(str);
}

static int connect_ctrl_ring(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	struct xenvif *vif = be->vif;
	unsigned int val;
	grant_ref_t ring_ref;
	unsigned int evtchn;
	int err;

	err = xenbus_scanf(XBT_NIL, dev->otherend,
			   "ctrl-ring-ref", "%u", &val);
	if (err < 0)
		goto done; /* The frontend does not have a control ring */

	ring_ref = val;

	err = xenbus_scanf(XBT_NIL, dev->otherend,
			   "event-channel-ctrl", "%u", &val);
	if (err < 0) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/event-channel-ctrl",
				 dev->otherend);
		goto fail;
	}

	evtchn = val;

	err = xenvif_connect_ctrl(vif, ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "mapping shared-frame %u port %u",
				 ring_ref, evtchn);
		goto fail;
	}

done:
	return 0;

fail:
	return err;
}

static void connect(struct backend_info *be)
{
	int err;
	struct xenbus_device *dev = be->dev;
	unsigned long credit_bytes, credit_usec;
	unsigned int queue_index;
	unsigned int requested_num_queues;
	struct xenvif_queue *queue;

	/* Check whether the frontend requested multiple queues
	 * and read the number requested.
	 */
	requested_num_queues = xenbus_read_unsigned(dev->otherend,
					"multi-queue-num-queues", 1);
	if (requested_num_queues > xenvif_max_queues) {
		/* buggy or malicious guest */
		xenbus_dev_fatal(dev, -EINVAL,
				 "guest requested %u queues, exceeding the maximum of %u.",
				 requested_num_queues, xenvif_max_queues);
		return;
	}

	err = xen_net_read_mac(dev, be->vif->fe_dev_addr);
	if (err) {
		xenbus_dev_fatal(dev, err, "parsing %s/mac", dev->nodename);
		return;
	}

	xen_net_read_rate(dev, &credit_bytes, &credit_usec);
	xen_unregister_watchers(be->vif);
	xen_register_watchers(dev, be->vif);
	read_xenbus_vif_flags(be);

	err = connect_ctrl_ring(be);
	if (err) {
		xenbus_dev_fatal(dev, err, "connecting control ring");
		return;
	}

	/* Use the number of queues requested by the frontend */
	be->vif->queues = vzalloc(requested_num_queues *
				  sizeof(struct xenvif_queue));
	if (!be->vif->queues) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating queues");
		return;
	}

	be->vif->num_queues = requested_num_queues;
	be->vif->stalled_queues = requested_num_queues;

	for (queue_index = 0; queue_index < requested_num_queues; ++queue_index) {
		queue = &be->vif->queues[queue_index];
		queue->vif = be->vif;
		queue->id = queue_index;
		snprintf(queue->name, sizeof(queue->name), "%s-q%u",
				be->vif->dev->name, queue->id);

		err = xenvif_init_queue(queue);
		if (err) {
			/* xenvif_init_queue() cleans up after itself on
			 * failure, but we need to clean up any previously
			 * initialised queues. Set num_queues to i so that
			 * earlier queues can be destroyed using the regular
			 * disconnect logic.
			 */
			be->vif->num_queues = queue_index;
			goto err;
		}

		queue->credit_bytes = credit_bytes;
		queue->remaining_credit = credit_bytes;
		queue->credit_usec = credit_usec;

		err = connect_data_rings(be, queue);
		if (err) {
			/* connect_data_rings() cleans up after itself on
			 * failure, but we need to clean up after
			 * xenvif_init_queue() here, and also clean up any
			 * previously initialised queues.
			 */
			xenvif_deinit_queue(queue);
			be->vif->num_queues = queue_index;
			goto err;
		}
	}

#ifdef CONFIG_DEBUG_FS
	xenvif_debugfs_addif(be->vif);
#endif /* CONFIG_DEBUG_FS */

	/* Initialisation completed, tell core driver the number of
	 * active queues.
	 */
	rtnl_lock();
	netif_set_real_num_tx_queues(be->vif->dev, requested_num_queues);
	netif_set_real_num_rx_queues(be->vif->dev, requested_num_queues);
	rtnl_unlock();

	xenvif_carrier_on(be->vif);

	unregister_hotplug_status_watch(be);
	err = xenbus_watch_pathfmt(dev, &be->hotplug_status_watch,
				   hotplug_status_changed,
				   "%s/%s", dev->nodename, "hotplug-status");
	if (!err)
		be->have_hotplug_status_watch = 1;

	netif_tx_wake_all_queues(be->vif->dev);

	return;

err:
	if (be->vif->num_queues > 0)
		xenvif_disconnect_data(be->vif); /* Clean up existing queues */
	for (queue_index = 0; queue_index < be->vif->num_queues; ++queue_index)
		xenvif_deinit_queue(&be->vif->queues[queue_index]);
	vfree(be->vif->queues);
	be->vif->queues = NULL;
	be->vif->num_queues = 0;
	xenvif_disconnect_ctrl(be->vif);
	return;
}


static int connect_data_rings(struct backend_info *be,
			      struct xenvif_queue *queue)
{
	struct xenbus_device *dev = be->dev;
	unsigned int num_queues = queue->vif->num_queues;
	unsigned long tx_ring_ref, rx_ring_ref;
	unsigned int tx_evtchn, rx_evtchn;
	int err;
	char *xspath;
	size_t xspathsize;
	const size_t xenstore_path_ext_size = 11; /* sufficient for "/queue-NNN" */

	/* If the frontend requested 1 queue, or we have fallen back
	 * to single queue due to lack of frontend support for multi-
	 * queue, expect the remaining XenStore keys in the toplevel
	 * directory. Otherwise, expect them in a subdirectory called
	 * queue-N.
	 */
	if (num_queues == 1) {
		xspath = kzalloc(strlen(dev->otherend) + 1, GFP_KERNEL);
		if (!xspath) {
			xenbus_dev_fatal(dev, -ENOMEM,
					 "reading ring references");
			return -ENOMEM;
		}
		strcpy(xspath, dev->otherend);
	} else {
		xspathsize = strlen(dev->otherend) + xenstore_path_ext_size;
		xspath = kzalloc(xspathsize, GFP_KERNEL);
		if (!xspath) {
			xenbus_dev_fatal(dev, -ENOMEM,
					 "reading ring references");
			return -ENOMEM;
		}
		snprintf(xspath, xspathsize, "%s/queue-%u", dev->otherend,
			 queue->id);
	}

	err = xenbus_gather(XBT_NIL, xspath,
			    "tx-ring-ref", "%lu", &tx_ring_ref,
			    "rx-ring-ref", "%lu", &rx_ring_ref, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref",
				 xspath);
		goto err;
	}

	/* Try split event channels first, then single event channel. */
	err = xenbus_gather(XBT_NIL, xspath,
			    "event-channel-tx", "%u", &tx_evtchn,
			    "event-channel-rx", "%u", &rx_evtchn, NULL);
	if (err < 0) {
		err = xenbus_scanf(XBT_NIL, xspath,
				   "event-channel", "%u", &tx_evtchn);
		if (err < 0) {
			xenbus_dev_fatal(dev, err,
					 "reading %s/event-channel(-tx/rx)",
					 xspath);
			goto err;
		}
		rx_evtchn = tx_evtchn;
	}

	/* Map the shared frame, irq etc. */
	err = xenvif_connect_data(queue, tx_ring_ref, rx_ring_ref,
				  tx_evtchn, rx_evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "mapping shared-frames %lu/%lu port tx %u rx %u",
				 tx_ring_ref, rx_ring_ref,
				 tx_evtchn, rx_evtchn);
		goto err;
	}

	err = 0;
err: /* Regular return falls through with err == 0 */
	kfree(xspath);
	return err;
}

static int read_xenbus_vif_flags(struct backend_info *be)
{
	struct xenvif *vif = be->vif;
	struct xenbus_device *dev = be->dev;
	unsigned int rx_copy;
	int err;

	err = xenbus_scanf(XBT_NIL, dev->otherend, "request-rx-copy", "%u",
			   &rx_copy);
	if (err == -ENOENT) {
		err = 0;
		rx_copy = 0;
	}
	if (err < 0) {
		xenbus_dev_fatal(dev, err, "reading %s/request-rx-copy",
				 dev->otherend);
		return err;
	}
	if (!rx_copy)
		return -EOPNOTSUPP;

	if (!xenbus_read_unsigned(dev->otherend, "feature-rx-notify", 0)) {
		/* - Reduce drain timeout to poll more frequently for
		 *   Rx requests.
		 * - Disable Rx stall detection.
		 */
		be->vif->drain_timeout = msecs_to_jiffies(30);
		be->vif->stall_timeout = 0;
	}

	vif->can_sg = !!xenbus_read_unsigned(dev->otherend, "feature-sg", 0);

	vif->gso_mask = 0;

	if (xenbus_read_unsigned(dev->otherend, "feature-gso-tcpv4", 0))
		vif->gso_mask |= GSO_BIT(TCPV4);

	if (xenbus_read_unsigned(dev->otherend, "feature-gso-tcpv6", 0))
		vif->gso_mask |= GSO_BIT(TCPV6);

	vif->ip_csum = !xenbus_read_unsigned(dev->otherend,
					     "feature-no-csum-offload", 0);

	vif->ipv6_csum = !!xenbus_read_unsigned(dev->otherend,
						"feature-ipv6-csum-offload", 0);

	return 0;
}

static const struct xenbus_device_id netback_ids[] = {
	{ "vif" },
	{ "" }
};

static struct xenbus_driver netback_driver = {
	.ids = netback_ids,
	.probe = netback_probe,
	.remove = netback_remove,
	.uevent = netback_uevent,
	.otherend_changed = frontend_changed,
};

int xenvif_xenbus_init(void)
{
	return xenbus_register_backend(&netback_driver);
}

void xenvif_xenbus_fini(void)
{
	return xenbus_unregister_driver(&netback_driver);
}
