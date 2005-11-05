/*
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004 - 2005
 * Copyright (C) Dmitry Yusupov, 2004 - 2005
 * Copyright (C) Alex Aizman, 2004 - 2005
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <net/tcp.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/iscsi_if.h>

#define ISCSI_SESSION_ATTRS 8
#define ISCSI_CONN_ATTRS 6

struct iscsi_internal {
	struct scsi_transport_template t;
	struct iscsi_transport *iscsi_transport;
	struct list_head list;
	/*
	 * List of sessions for this transport
	 */
	struct list_head sessions;
	/*
	 * lock to serialize access to the sessions list which must
	 * be taken after the rx_queue_sema
	 */
	spinlock_t session_lock;
	/*
	 * based on transport capabilities, at register time we set these
	 * bits to tell the transport class it wants attributes displayed
	 * in sysfs or that it can support different iSCSI Data-Path
	 * capabilities
	 */
	uint32_t param_mask;

	struct class_device cdev;
	/*
	 * We do not have any private or other attrs.
	 */
	struct transport_container conn_cont;
	struct class_device_attribute *conn_attrs[ISCSI_CONN_ATTRS + 1];
	struct transport_container session_cont;
	struct class_device_attribute *session_attrs[ISCSI_SESSION_ATTRS + 1];
};

/*
 * list of registered transports and lock that must
 * be held while accessing list. The iscsi_transport_lock must
 * be acquired after the rx_queue_sema.
 */
static LIST_HEAD(iscsi_transports);
static DEFINE_SPINLOCK(iscsi_transport_lock);

#define to_iscsi_internal(tmpl) \
	container_of(tmpl, struct iscsi_internal, t)

#define cdev_to_iscsi_internal(_cdev) \
	container_of(_cdev, struct iscsi_internal, cdev)

static void iscsi_transport_release(struct class_device *cdev)
{
	struct iscsi_internal *priv = cdev_to_iscsi_internal(cdev);
	kfree(priv);
}

/*
 * iscsi_transport_class represents the iscsi_transports that are
 * registered.
 */
static struct class iscsi_transport_class = {
	.name = "iscsi_transport",
	.release = iscsi_transport_release,
};

static ssize_t
show_transport_handle(struct class_device *cdev, char *buf)
{
	struct iscsi_internal *priv = cdev_to_iscsi_internal(cdev);
	return sprintf(buf, "%llu\n", (unsigned long long)iscsi_handle(priv->iscsi_transport));
}
static CLASS_DEVICE_ATTR(handle, S_IRUGO, show_transport_handle, NULL);

#define show_transport_attr(name, format)				\
static ssize_t								\
show_transport_##name(struct class_device *cdev, char *buf)		\
{									\
	struct iscsi_internal *priv = cdev_to_iscsi_internal(cdev);	\
	return sprintf(buf, format"\n", priv->iscsi_transport->name);	\
}									\
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_transport_##name, NULL);

show_transport_attr(caps, "0x%x");
show_transport_attr(max_lun, "%d");
show_transport_attr(max_conn, "%d");
show_transport_attr(max_cmd_len, "%d");

static struct attribute *iscsi_transport_attrs[] = {
	&class_device_attr_handle.attr,
	&class_device_attr_caps.attr,
	&class_device_attr_max_lun.attr,
	&class_device_attr_max_conn.attr,
	&class_device_attr_max_cmd_len.attr,
	NULL,
};

static struct attribute_group iscsi_transport_group = {
	.attrs = iscsi_transport_attrs,
};

static DECLARE_TRANSPORT_CLASS(iscsi_session_class,
			       "iscsi_session",
			       NULL,
			       NULL,
			       NULL);

static DECLARE_TRANSPORT_CLASS(iscsi_connection_class,
			       "iscsi_connection",
			       NULL,
			       NULL,
			       NULL);

static struct sock *nls;
static int daemon_pid;
static DECLARE_MUTEX(rx_queue_sema);

struct mempool_zone {
	mempool_t *pool;
	atomic_t allocated;
	int size;
	int hiwat;
	struct list_head freequeue;
	spinlock_t freelock;
};

static struct mempool_zone z_reply;

/*
 * Z_MAX_* - actual mempool size allocated at the mempool_zone_init() time
 * Z_HIWAT_* - zone's high watermark when if_error bit will be set to -ENOMEM
 *             so daemon will notice OOM on NETLINK tranposrt level and will
 *             be able to predict or change operational behavior
 */
#define Z_MAX_REPLY	8
#define Z_HIWAT_REPLY	6
#define Z_MAX_PDU	8
#define Z_HIWAT_PDU	6
#define Z_MAX_ERROR	16
#define Z_HIWAT_ERROR	12

struct iscsi_if_conn {
	struct list_head conn_list;	/* item in connlist */
	struct list_head session_list;	/* item in session->connections */
	iscsi_connh_t connh;
	int active;			/* must be accessed with the connlock */
	struct Scsi_Host *host;		/* originated shost */
	struct device dev;		/* sysfs transport/container device */
	struct iscsi_transport *transport;
	struct mempool_zone z_error;
	struct mempool_zone z_pdu;
	struct list_head freequeue;
};

#define iscsi_dev_to_if_conn(_dev) \
	container_of(_dev, struct iscsi_if_conn, dev)

#define iscsi_cdev_to_if_conn(_cdev) \
	iscsi_dev_to_if_conn(_cdev->dev)

static LIST_HEAD(connlist);
static DEFINE_SPINLOCK(connlock);

struct iscsi_if_session {
	struct list_head list;	/* item in session_list */
	struct list_head connections;
	iscsi_sessionh_t sessionh;
	struct iscsi_transport *transport;
	struct device dev;	/* sysfs transport/container device */
};

#define iscsi_dev_to_if_session(_dev) \
	container_of(_dev, struct iscsi_if_session, dev)

#define iscsi_cdev_to_if_session(_cdev) \
	iscsi_dev_to_if_session(_cdev->dev)

#define iscsi_if_session_to_shost(_session) \
	dev_to_shost(_session->dev.parent)

static struct iscsi_if_conn*
iscsi_if_find_conn(uint64_t key)
{
	unsigned long flags;
	struct iscsi_if_conn *conn;

	spin_lock_irqsave(&connlock, flags);
	list_for_each_entry(conn, &connlist, conn_list)
		if (conn->connh == key) {
			spin_unlock_irqrestore(&connlock, flags);
			return conn;
		}
	spin_unlock_irqrestore(&connlock, flags);
	return NULL;
}

static struct iscsi_internal *
iscsi_if_transport_lookup(struct iscsi_transport *tt)
{
	struct iscsi_internal *priv;
	unsigned long flags;

	spin_lock_irqsave(&iscsi_transport_lock, flags);
	list_for_each_entry(priv, &iscsi_transports, list) {
		if (tt == priv->iscsi_transport) {
			spin_unlock_irqrestore(&iscsi_transport_lock, flags);
			return priv;
		}
	}
	spin_unlock_irqrestore(&iscsi_transport_lock, flags);
	return NULL;
}

static inline struct list_head *skb_to_lh(struct sk_buff *skb)
{
	return (struct list_head *)&skb->cb;
}

static void*
mempool_zone_alloc_skb(unsigned int gfp_mask, void *pool_data)
{
	struct mempool_zone *zone = pool_data;

	return alloc_skb(zone->size, gfp_mask);
}

static void
mempool_zone_free_skb(void *element, void *pool_data)
{
	kfree_skb(element);
}

static void
mempool_zone_complete(struct mempool_zone *zone)
{
	unsigned long flags;
	struct list_head *lh, *n;

	spin_lock_irqsave(&zone->freelock, flags);
	list_for_each_safe(lh, n, &zone->freequeue) {
		struct sk_buff *skb = (struct sk_buff *)((char *)lh -
				offsetof(struct sk_buff, cb));
		if (!skb_shared(skb)) {
			list_del(skb_to_lh(skb));
			mempool_free(skb, zone->pool);
			atomic_dec(&zone->allocated);
		}
	}
	spin_unlock_irqrestore(&zone->freelock, flags);
}

static int
mempool_zone_init(struct mempool_zone *zp, unsigned max, unsigned size,
		unsigned hiwat)
{
	zp->pool = mempool_create(max, mempool_zone_alloc_skb,
				  mempool_zone_free_skb, zp);
	if (!zp->pool)
		return -ENOMEM;

	zp->size = size;
	zp->hiwat = hiwat;

	INIT_LIST_HEAD(&zp->freequeue);
	spin_lock_init(&zp->freelock);
	atomic_set(&zp->allocated, 0);

	return 0;
}


static struct sk_buff*
mempool_zone_get_skb(struct mempool_zone *zone)
{
	struct sk_buff *skb;

	skb = mempool_alloc(zone->pool, GFP_ATOMIC);
	if (skb)
		atomic_inc(&zone->allocated);
	return skb;
}

static int
iscsi_unicast_skb(struct mempool_zone *zone, struct sk_buff *skb)
{
	unsigned long flags;
	int rc;

	skb_get(skb);
	rc = netlink_unicast(nls, skb, daemon_pid, MSG_DONTWAIT);
	if (rc < 0) {
		mempool_free(skb, zone->pool);
		printk(KERN_ERR "iscsi: can not unicast skb (%d)\n", rc);
		return rc;
	}

	spin_lock_irqsave(&zone->freelock, flags);
	list_add(skb_to_lh(skb), &zone->freequeue);
	spin_unlock_irqrestore(&zone->freelock, flags);

	return 0;
}

int iscsi_recv_pdu(iscsi_connh_t connh, struct iscsi_hdr *hdr,
		   char *data, uint32_t data_size)
{
	struct nlmsghdr	*nlh;
	struct sk_buff *skb;
	struct iscsi_uevent *ev;
	struct iscsi_if_conn *conn;
	char *pdu;
	int len = NLMSG_SPACE(sizeof(*ev) + sizeof(struct iscsi_hdr) +
			      data_size);

	conn = iscsi_if_find_conn(connh);
	BUG_ON(!conn);

	mempool_zone_complete(&conn->z_pdu);

	skb = mempool_zone_get_skb(&conn->z_pdu);
	if (!skb) {
		iscsi_conn_error(connh, ISCSI_ERR_CONN_FAILED);
		printk(KERN_ERR "iscsi%d: can not deliver control PDU: OOM\n",
		       conn->host->host_no);
		return -ENOMEM;
	}

	nlh = __nlmsg_put(skb, daemon_pid, 0, 0, (len - sizeof(*nlh)), 0);
	ev = NLMSG_DATA(nlh);
	memset(ev, 0, sizeof(*ev));
	ev->transport_handle = iscsi_handle(conn->transport);
	ev->type = ISCSI_KEVENT_RECV_PDU;
	if (atomic_read(&conn->z_pdu.allocated) >= conn->z_pdu.hiwat)
		ev->iferror = -ENOMEM;
	ev->r.recv_req.conn_handle = connh;
	pdu = (char*)ev + sizeof(*ev);
	memcpy(pdu, hdr, sizeof(struct iscsi_hdr));
	memcpy(pdu + sizeof(struct iscsi_hdr), data, data_size);

	return iscsi_unicast_skb(&conn->z_pdu, skb);
}
EXPORT_SYMBOL_GPL(iscsi_recv_pdu);

void iscsi_conn_error(iscsi_connh_t connh, enum iscsi_err error)
{
	struct nlmsghdr	*nlh;
	struct sk_buff	*skb;
	struct iscsi_uevent *ev;
	struct iscsi_if_conn *conn;
	int len = NLMSG_SPACE(sizeof(*ev));

	conn = iscsi_if_find_conn(connh);
	BUG_ON(!conn);

	mempool_zone_complete(&conn->z_error);

	skb = mempool_zone_get_skb(&conn->z_error);
	if (!skb) {
		printk(KERN_ERR "iscsi%d: gracefully ignored conn error (%d)\n",
		       conn->host->host_no, error);
		return;
	}

	nlh = __nlmsg_put(skb, daemon_pid, 0, 0, (len - sizeof(*nlh)), 0);
	ev = NLMSG_DATA(nlh);
	ev->transport_handle = iscsi_handle(conn->transport);
	ev->type = ISCSI_KEVENT_CONN_ERROR;
	if (atomic_read(&conn->z_error.allocated) >= conn->z_error.hiwat)
		ev->iferror = -ENOMEM;
	ev->r.connerror.error = error;
	ev->r.connerror.conn_handle = connh;

	iscsi_unicast_skb(&conn->z_error, skb);

	printk(KERN_INFO "iscsi%d: detected conn error (%d)\n",
	       conn->host->host_no, error);
}
EXPORT_SYMBOL_GPL(iscsi_conn_error);

static int
iscsi_if_send_reply(int pid, int seq, int type, int done, int multi,
		      void *payload, int size)
{
	struct sk_buff	*skb;
	struct nlmsghdr	*nlh;
	int len = NLMSG_SPACE(size);
	int flags = multi ? NLM_F_MULTI : 0;
	int t = done ? NLMSG_DONE : type;

	mempool_zone_complete(&z_reply);

	skb = mempool_zone_get_skb(&z_reply);
	/*
	 * FIXME:
	 * user is supposed to react on iferror == -ENOMEM;
	 * see iscsi_if_rx().
	 */
	BUG_ON(!skb);

	nlh = __nlmsg_put(skb, pid, seq, t, (len - sizeof(*nlh)), 0);
	nlh->nlmsg_flags = flags;
	memcpy(NLMSG_DATA(nlh), payload, size);
	return iscsi_unicast_skb(&z_reply, skb);
}

/*
 * iSCSI Session's hostdata organization:
 *
 *    *------------------* <== host->hostdata
 *    | transport        |
 *    |------------------| <== iscsi_hostdata(host->hostdata)
 *    | transport's data |
 *    |------------------| <== hostdata_session(host->hostdata)
 *    | interface's data |
 *    *------------------*
 */

#define hostdata_privsize(_t)	(sizeof(unsigned long) + _t->hostdata_size + \
				 _t->hostdata_size % sizeof(unsigned long) + \
				 sizeof(struct iscsi_if_session))

#define hostdata_session(_hostdata) ((void*)_hostdata + sizeof(unsigned long) + \
			((struct iscsi_transport *) \
			 iscsi_ptr(*(uint64_t *)_hostdata))->hostdata_size)

static void iscsi_if_session_dev_release(struct device *dev)
{
	struct iscsi_if_session *session = iscsi_dev_to_if_session(dev);
	struct iscsi_transport *transport = session->transport;
	struct Scsi_Host *shost = iscsi_if_session_to_shost(session);
	struct iscsi_if_conn *conn, *tmp;
	unsigned long flags;

	/* now free connections */
	spin_lock_irqsave(&connlock, flags);
	list_for_each_entry_safe(conn, tmp, &session->connections,
				 session_list) {
		list_del(&conn->session_list);
		mempool_destroy(conn->z_pdu.pool);
		mempool_destroy(conn->z_error.pool);
		kfree(conn);
	}
	spin_unlock_irqrestore(&connlock, flags);
	scsi_host_put(shost);
	module_put(transport->owner);
}

static int
iscsi_if_create_session(struct iscsi_internal *priv, struct iscsi_uevent *ev)
{
	struct iscsi_transport *transport = priv->iscsi_transport;
	struct iscsi_if_session *session;
	struct Scsi_Host *shost;
	unsigned long flags;
	int error;

	if (!try_module_get(transport->owner))
		return -EPERM;

	shost = scsi_host_alloc(transport->host_template,
				hostdata_privsize(transport));
	if (!shost) {
		ev->r.c_session_ret.session_handle = iscsi_handle(NULL);
		printk(KERN_ERR "iscsi: can not allocate SCSI host for "
		       "session\n");
		error = -ENOMEM;
		goto out_module_put;
	}
	shost->max_id = 1;
	shost->max_channel = 0;
	shost->max_lun = transport->max_lun;
	shost->max_cmd_len = transport->max_cmd_len;
	shost->transportt = &priv->t;

	/* store struct iscsi_transport in hostdata */
	*(uint64_t*)shost->hostdata = ev->transport_handle;

	ev->r.c_session_ret.session_handle = transport->create_session(
					ev->u.c_session.initial_cmdsn, shost);
	if (ev->r.c_session_ret.session_handle == iscsi_handle(NULL)) {
		error = 0;
		goto out_host_put;
	}

	/* host_no becomes assigned SID */
	ev->r.c_session_ret.sid = shost->host_no;
	/* initialize session */
	session = hostdata_session(shost->hostdata);
	INIT_LIST_HEAD(&session->connections);
	INIT_LIST_HEAD(&session->list);
	session->sessionh = ev->r.c_session_ret.session_handle;
	session->transport = transport;

	error = scsi_add_host(shost, NULL);
	if (error)
		goto out_destroy_session;

	/*
	 * this is released in the dev's release function)
	 */
	scsi_host_get(shost);
	snprintf(session->dev.bus_id, BUS_ID_SIZE, "session%u", shost->host_no);
	session->dev.parent = &shost->shost_gendev;
	session->dev.release = iscsi_if_session_dev_release;
	error = device_register(&session->dev);
	if (error) {
		printk(KERN_ERR "iscsi: could not register session%d's dev\n",
		       shost->host_no);
		goto out_remove_host;
	}
	transport_register_device(&session->dev);

	/* add this session to the list of active sessions */
	spin_lock_irqsave(&priv->session_lock, flags);
	list_add(&session->list, &priv->sessions);
	spin_unlock_irqrestore(&priv->session_lock, flags);

	return 0;

out_remove_host:
	scsi_remove_host(shost);
out_destroy_session:
	transport->destroy_session(ev->r.c_session_ret.session_handle);
	ev->r.c_session_ret.session_handle = iscsi_handle(NULL);
out_host_put:
	scsi_host_put(shost);
out_module_put:
	module_put(transport->owner);
	return error;
}

static int
iscsi_if_destroy_session(struct iscsi_internal *priv, struct iscsi_uevent *ev)
{
	struct iscsi_transport *transport = priv->iscsi_transport;
	struct Scsi_Host *shost;
	struct iscsi_if_session *session;
	unsigned long flags;
	struct iscsi_if_conn *conn;
	int error = 0;

	shost = scsi_host_lookup(ev->u.d_session.sid);
	if (shost == ERR_PTR(-ENXIO))
		return -EEXIST;
	session = hostdata_session(shost->hostdata);

	/* check if we have active connections */
	spin_lock_irqsave(&connlock, flags);
	list_for_each_entry(conn, &session->connections, session_list) {
		if (conn->active) {
			printk(KERN_ERR "iscsi%d: can not destroy session: "
			       "has active connection (%p)\n",
			       shost->host_no, iscsi_ptr(conn->connh));
			spin_unlock_irqrestore(&connlock, flags);
			error = EIO;
			goto out_release_ref;
		}
	}
	spin_unlock_irqrestore(&connlock, flags);

	scsi_remove_host(shost);
	transport->destroy_session(ev->u.d_session.session_handle);
	transport_unregister_device(&session->dev);
	device_unregister(&session->dev);

	/* remove this session from the list of active sessions */
	spin_lock_irqsave(&priv->session_lock, flags);
	list_del(&session->list);
	spin_unlock_irqrestore(&priv->session_lock, flags);

	/* ref from host alloc */
	scsi_host_put(shost);
out_release_ref:
	/* ref from host lookup */
	scsi_host_put(shost);
	return error;
}

static void iscsi_if_conn_dev_release(struct device *dev)
{
	struct iscsi_if_conn *conn = iscsi_dev_to_if_conn(dev);
	struct Scsi_Host *shost = conn->host;

	scsi_host_put(shost);
}

static int
iscsi_if_create_conn(struct iscsi_transport *transport, struct iscsi_uevent *ev)
{
	struct iscsi_if_session *session;
	struct Scsi_Host *shost;
	struct iscsi_if_conn *conn;
	unsigned long flags;
	int error;

	shost = scsi_host_lookup(ev->u.c_conn.sid);
	if (shost == ERR_PTR(-ENXIO))
		return -EEXIST;
	session = hostdata_session(shost->hostdata);

	conn = kmalloc(sizeof(struct iscsi_if_conn), GFP_KERNEL);
	if (!conn) {
		error = -ENOMEM;
		goto out_release_ref;
	}
	memset(conn, 0, sizeof(struct iscsi_if_conn));
	INIT_LIST_HEAD(&conn->session_list);
	INIT_LIST_HEAD(&conn->conn_list);
	conn->host = shost;
	conn->transport = transport;

	error = mempool_zone_init(&conn->z_pdu, Z_MAX_PDU,
			NLMSG_SPACE(sizeof(struct iscsi_uevent) +
				    sizeof(struct iscsi_hdr) +
				    DEFAULT_MAX_RECV_DATA_SEGMENT_LENGTH),
			Z_HIWAT_PDU);
	if (error) {
		printk(KERN_ERR "iscsi%d: can not allocate pdu zone for new "
		       "conn\n", shost->host_no);
		goto out_free_conn;
	}
	error = mempool_zone_init(&conn->z_error, Z_MAX_ERROR,
			NLMSG_SPACE(sizeof(struct iscsi_uevent)),
			Z_HIWAT_ERROR);
	if (error) {
		printk(KERN_ERR "iscsi%d: can not allocate error zone for "
		       "new conn\n", shost->host_no);
		goto out_free_pdu_pool;
	}

	ev->r.handle = transport->create_conn(ev->u.c_conn.session_handle,
					ev->u.c_conn.cid);
	if (!ev->r.handle) {
		error = -ENODEV;
		goto out_free_error_pool;
	}

	conn->connh = ev->r.handle;

	/*
	 * this is released in the dev's release function
	 */
	if (!scsi_host_get(shost))
		goto out_destroy_conn;
	snprintf(conn->dev.bus_id, BUS_ID_SIZE, "connection%d:%u",
		 shost->host_no, ev->u.c_conn.cid);
	conn->dev.parent = &session->dev;
	conn->dev.release = iscsi_if_conn_dev_release;
	error = device_register(&conn->dev);
	if (error) {
		printk(KERN_ERR "iscsi%d: could not register connections%u "
		       "dev\n", shost->host_no, ev->u.c_conn.cid);
		goto out_release_parent_ref;
	}
	transport_register_device(&conn->dev);

	spin_lock_irqsave(&connlock, flags);
	list_add(&conn->conn_list, &connlist);
	list_add(&conn->session_list, &session->connections);
	conn->active = 1;
	spin_unlock_irqrestore(&connlock, flags);

	scsi_host_put(shost);
	return 0;

out_release_parent_ref:
	scsi_host_put(shost);
out_destroy_conn:
	transport->destroy_conn(ev->r.handle);
out_free_error_pool:
	mempool_destroy(conn->z_error.pool);
out_free_pdu_pool:
	mempool_destroy(conn->z_pdu.pool);
out_free_conn:
	kfree(conn);
out_release_ref:
	scsi_host_put(shost);
	return error;
}

static int
iscsi_if_destroy_conn(struct iscsi_transport *transport, struct iscsi_uevent *ev)
{
	unsigned long flags;
	struct iscsi_if_conn *conn;

	conn = iscsi_if_find_conn(ev->u.d_conn.conn_handle);
	if (!conn)
		return -EEXIST;

	transport->destroy_conn(ev->u.d_conn.conn_handle);

	spin_lock_irqsave(&connlock, flags);
	conn->active = 0;
	list_del(&conn->conn_list);
	spin_unlock_irqrestore(&connlock, flags);

	transport_unregister_device(&conn->dev);
	device_unregister(&conn->dev);
	return 0;
}

static int
iscsi_if_get_stats(struct iscsi_transport *transport, struct sk_buff *skb,
		   struct nlmsghdr *nlh)
{
	struct iscsi_uevent *ev = NLMSG_DATA(nlh);
	struct iscsi_stats *stats;
	struct sk_buff *skbstat;
	struct iscsi_if_conn *conn;
	struct nlmsghdr	*nlhstat;
	struct iscsi_uevent *evstat;
	int len = NLMSG_SPACE(sizeof(*ev) +
			      sizeof(struct iscsi_stats) +
			      sizeof(struct iscsi_stats_custom) *
			      ISCSI_STATS_CUSTOM_MAX);
	int err = 0;

	conn = iscsi_if_find_conn(ev->u.get_stats.conn_handle);
	if (!conn)
		return -EEXIST;

	do {
		int actual_size;

		mempool_zone_complete(&conn->z_pdu);

		skbstat = mempool_zone_get_skb(&conn->z_pdu);
		if (!skbstat) {
			printk(KERN_ERR "iscsi%d: can not deliver stats: OOM\n",
			       conn->host->host_no);
			return -ENOMEM;
		}

		nlhstat = __nlmsg_put(skbstat, daemon_pid, 0, 0,
				      (len - sizeof(*nlhstat)), 0);
		evstat = NLMSG_DATA(nlhstat);
		memset(evstat, 0, sizeof(*evstat));
		evstat->transport_handle = iscsi_handle(conn->transport);
		evstat->type = nlh->nlmsg_type;
		if (atomic_read(&conn->z_pdu.allocated) >= conn->z_pdu.hiwat)
			evstat->iferror = -ENOMEM;
		evstat->u.get_stats.conn_handle =
			ev->u.get_stats.conn_handle;
		stats = (struct iscsi_stats *)
			((char*)evstat + sizeof(*evstat));
		memset(stats, 0, sizeof(*stats));

		transport->get_stats(ev->u.get_stats.conn_handle, stats);
		actual_size = NLMSG_SPACE(sizeof(struct iscsi_uevent) +
					  sizeof(struct iscsi_stats) +
					  sizeof(struct iscsi_stats_custom) *
					  stats->custom_length);
		actual_size -= sizeof(*nlhstat);
		actual_size = NLMSG_LENGTH(actual_size);
		skb_trim(skb, NLMSG_ALIGN(actual_size));
		nlhstat->nlmsg_len = actual_size;

		err = iscsi_unicast_skb(&conn->z_pdu, skbstat);
	} while (err < 0 && err != -ECONNREFUSED);

	return err;
}

static int
iscsi_if_recv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int err = 0;
	struct iscsi_uevent *ev = NLMSG_DATA(nlh);
	struct iscsi_transport *transport = NULL;
	struct iscsi_internal *priv;

	if (NETLINK_CREDS(skb)->uid)
		return -EPERM;

	priv = iscsi_if_transport_lookup(iscsi_ptr(ev->transport_handle));
	if (!priv)
		return -EINVAL;
	transport = priv->iscsi_transport;

	daemon_pid = NETLINK_CREDS(skb)->pid;

	switch (nlh->nlmsg_type) {
	case ISCSI_UEVENT_CREATE_SESSION:
		err = iscsi_if_create_session(priv, ev);
		break;
	case ISCSI_UEVENT_DESTROY_SESSION:
		err = iscsi_if_destroy_session(priv, ev);
		break;
	case ISCSI_UEVENT_CREATE_CONN:
		err = iscsi_if_create_conn(transport, ev);
		break;
	case ISCSI_UEVENT_DESTROY_CONN:
		err = iscsi_if_destroy_conn(transport, ev);
		break;
	case ISCSI_UEVENT_BIND_CONN:
		if (!iscsi_if_find_conn(ev->u.b_conn.conn_handle))
			return -EEXIST;
		ev->r.retcode = transport->bind_conn(
			ev->u.b_conn.session_handle,
			ev->u.b_conn.conn_handle,
			ev->u.b_conn.transport_fd,
			ev->u.b_conn.is_leading);
		break;
	case ISCSI_UEVENT_SET_PARAM:
		if (!iscsi_if_find_conn(ev->u.set_param.conn_handle))
			return -EEXIST;
		ev->r.retcode = transport->set_param(
			ev->u.set_param.conn_handle,
			ev->u.set_param.param, ev->u.set_param.value);
		break;
	case ISCSI_UEVENT_START_CONN:
		if (!iscsi_if_find_conn(ev->u.start_conn.conn_handle))
			return -EEXIST;
		ev->r.retcode = transport->start_conn(
			ev->u.start_conn.conn_handle);
		break;
	case ISCSI_UEVENT_STOP_CONN:
		if (!iscsi_if_find_conn(ev->u.stop_conn.conn_handle))
			return -EEXIST;
		transport->stop_conn(ev->u.stop_conn.conn_handle,
			ev->u.stop_conn.flag);
		break;
	case ISCSI_UEVENT_SEND_PDU:
		if (!iscsi_if_find_conn(ev->u.send_pdu.conn_handle))
			return -EEXIST;
		ev->r.retcode = transport->send_pdu(
		       ev->u.send_pdu.conn_handle,
		       (struct iscsi_hdr*)((char*)ev + sizeof(*ev)),
		       (char*)ev + sizeof(*ev) + ev->u.send_pdu.hdr_size,
			ev->u.send_pdu.data_size);
		break;
	case ISCSI_UEVENT_GET_STATS:
		err = iscsi_if_get_stats(transport, skb, nlh);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/* Get message from skb (based on rtnetlink_rcv_skb).  Each message is
 * processed by iscsi_if_recv_msg.  Malformed skbs with wrong length are
 * discarded silently.  */
static void
iscsi_if_rx(struct sock *sk, int len)
{
	struct sk_buff *skb;

	down(&rx_queue_sema);
	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		while (skb->len >= NLMSG_SPACE(0)) {
			int err;
			uint32_t rlen;
			struct nlmsghdr	*nlh;
			struct iscsi_uevent *ev;

			nlh = (struct nlmsghdr *)skb->data;
			if (nlh->nlmsg_len < sizeof(*nlh) ||
			    skb->len < nlh->nlmsg_len) {
				break;
			}
			ev = NLMSG_DATA(nlh);
			rlen = NLMSG_ALIGN(nlh->nlmsg_len);
			if (rlen > skb->len)
				rlen = skb->len;
			err = iscsi_if_recv_msg(skb, nlh);
			if (err) {
				ev->type = ISCSI_KEVENT_IF_ERROR;
				ev->iferror = err;
			}
			do {
				/*
				 * special case for GET_STATS:
				 * on success - sending reply and stats from
				 * inside of if_recv_msg(),
				 * on error - fall through.
				 */
				if (ev->type == ISCSI_UEVENT_GET_STATS && !err)
					break;
				err = iscsi_if_send_reply(
					NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
					nlh->nlmsg_type, 0, 0, ev, sizeof(*ev));
				if (atomic_read(&z_reply.allocated) >=
						z_reply.hiwat)
					ev->iferror = -ENOMEM;
			} while (err < 0 && err != -ECONNREFUSED);
			skb_pull(skb, rlen);
		}
		kfree_skb(skb);
	}
	up(&rx_queue_sema);
}

/*
 * iSCSI connection attrs
 */
#define iscsi_conn_int_attr_show(param, format)				\
static ssize_t								\
show_conn_int_param_##param(struct class_device *cdev, char *buf)	\
{									\
	uint32_t value = 0;						\
	struct iscsi_if_conn *conn = iscsi_cdev_to_if_conn(cdev);	\
	struct iscsi_internal *priv;					\
									\
	priv = to_iscsi_internal(conn->host->transportt);		\
	if (priv->param_mask & (1 << param))				\
		priv->iscsi_transport->get_param(conn->connh, param, &value); \
	return snprintf(buf, 20, format"\n", value);			\
}

#define iscsi_conn_int_attr(field, param, format)			\
	iscsi_conn_int_attr_show(param, format)				\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_conn_int_param_##param, NULL);

iscsi_conn_int_attr(max_recv_dlength, ISCSI_PARAM_MAX_RECV_DLENGTH, "%u");
iscsi_conn_int_attr(max_xmit_dlength, ISCSI_PARAM_MAX_XMIT_DLENGTH, "%u");
iscsi_conn_int_attr(header_digest, ISCSI_PARAM_HDRDGST_EN, "%d");
iscsi_conn_int_attr(data_digest, ISCSI_PARAM_DATADGST_EN, "%d");
iscsi_conn_int_attr(ifmarker, ISCSI_PARAM_IFMARKER_EN, "%d");
iscsi_conn_int_attr(ofmarker, ISCSI_PARAM_OFMARKER_EN, "%d");

/*
 * iSCSI session attrs
 */
#define iscsi_session_int_attr_show(param, format)			\
static ssize_t								\
show_session_int_param_##param(struct class_device *cdev, char *buf)	\
{									\
	uint32_t value = 0;						\
	struct iscsi_if_session *session = iscsi_cdev_to_if_session(cdev); \
	struct Scsi_Host *shost = iscsi_if_session_to_shost(session);	\
	struct iscsi_internal *priv = to_iscsi_internal(shost->transportt); \
	struct iscsi_if_conn *conn = NULL;				\
	unsigned long  flags;						\
									\
	spin_lock_irqsave(&connlock, flags);				\
	if (!list_empty(&session->connections))				\
		conn = list_entry(session->connections.next,		\
				  struct iscsi_if_conn, session_list);	\
	spin_unlock_irqrestore(&connlock, flags);			\
									\
	if (conn && (priv->param_mask & (1 << param)))			\
		priv->iscsi_transport->get_param(conn->connh, param, &value);\
	return snprintf(buf, 20, format"\n", value);			\
}

#define iscsi_session_int_attr(field, param, format)			\
	iscsi_session_int_attr_show(param, format)			\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_session_int_param_##param, NULL);

iscsi_session_int_attr(initial_r2t, ISCSI_PARAM_INITIAL_R2T_EN, "%d");
iscsi_session_int_attr(max_outstanding_r2t, ISCSI_PARAM_MAX_R2T, "%hu");
iscsi_session_int_attr(immediate_data, ISCSI_PARAM_IMM_DATA_EN, "%d");
iscsi_session_int_attr(first_burst_len, ISCSI_PARAM_FIRST_BURST, "%u");
iscsi_session_int_attr(max_burst_len, ISCSI_PARAM_MAX_BURST, "%u");
iscsi_session_int_attr(data_pdu_in_order, ISCSI_PARAM_PDU_INORDER_EN, "%d");
iscsi_session_int_attr(data_seq_in_order, ISCSI_PARAM_DATASEQ_INORDER_EN, "%d");
iscsi_session_int_attr(erl, ISCSI_PARAM_ERL, "%d");

#define SETUP_SESSION_RD_ATTR(field, param)				\
	if (priv->param_mask & (1 << param)) {				\
		priv->session_attrs[count] = &class_device_attr_##field;\
		count++;						\
	}

#define SETUP_CONN_RD_ATTR(field, param)				\
	if (priv->param_mask & (1 << param)) {				\
		priv->conn_attrs[count] = &class_device_attr_##field;	\
		count++;						\
	}

static int iscsi_is_session_dev(const struct device *dev)
{
	return dev->release == iscsi_if_session_dev_release;
}

static int iscsi_session_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct iscsi_if_session *session;
	struct Scsi_Host *shost;
	struct iscsi_internal *priv;

	if (!iscsi_is_session_dev(dev))
		return 0;

	session = iscsi_dev_to_if_session(dev);
	shost = iscsi_if_session_to_shost(session);
	if (!shost->transportt)
		return 0;

	priv = to_iscsi_internal(shost->transportt);
	if (priv->session_cont.ac.class != &iscsi_session_class.class)
		return 0;

	return &priv->session_cont.ac == cont;
}

static int iscsi_is_conn_dev(const struct device *dev)
{
	return dev->release == iscsi_if_conn_dev_release;
}

static int iscsi_conn_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct iscsi_if_conn *conn;
	struct Scsi_Host *shost;
	struct iscsi_internal *priv;

	if (!iscsi_is_conn_dev(dev))
		return 0;

	conn = iscsi_dev_to_if_conn(dev);
	shost = conn->host;
	if (!shost->transportt)
		return 0;

	priv = to_iscsi_internal(shost->transportt);
	if (priv->conn_cont.ac.class != &iscsi_connection_class.class)
		return 0;

	return &priv->conn_cont.ac == cont;
}

int iscsi_register_transport(struct iscsi_transport *tt)
{
	struct iscsi_internal *priv;
	unsigned long flags;
	int count = 0, err;

	BUG_ON(!tt);

	priv = iscsi_if_transport_lookup(tt);
	if (priv)
		return -EEXIST;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	memset(priv, 0, sizeof(*priv));
	INIT_LIST_HEAD(&priv->list);
	INIT_LIST_HEAD(&priv->sessions);
	spin_lock_init(&priv->session_lock);
	priv->iscsi_transport = tt;

	priv->cdev.class = &iscsi_transport_class;
	snprintf(priv->cdev.class_id, BUS_ID_SIZE, "%s", tt->name);
	err = class_device_register(&priv->cdev);
	if (err)
		goto free_priv;

	err = sysfs_create_group(&priv->cdev.kobj, &iscsi_transport_group);
	if (err)
		goto unregister_cdev;

	/* setup parameters mask */
	priv->param_mask = 0xFFFFFFFF;
	if (!(tt->caps & CAP_MULTI_R2T))
		priv->param_mask &= ~(1 << ISCSI_PARAM_MAX_R2T);
	if (!(tt->caps & CAP_HDRDGST))
		priv->param_mask &= ~(1 << ISCSI_PARAM_HDRDGST_EN);
	if (!(tt->caps & CAP_DATADGST))
		priv->param_mask &= ~(1 << ISCSI_PARAM_DATADGST_EN);
	if (!(tt->caps & CAP_MARKERS)) {
		priv->param_mask &= ~(1 << ISCSI_PARAM_IFMARKER_EN);
		priv->param_mask &= ~(1 << ISCSI_PARAM_OFMARKER_EN);
	}

	/* connection parameters */
	priv->conn_cont.ac.attrs = &priv->conn_attrs[0];
	priv->conn_cont.ac.class = &iscsi_connection_class.class;
	priv->conn_cont.ac.match = iscsi_conn_match;
	transport_container_register(&priv->conn_cont);

	SETUP_CONN_RD_ATTR(max_recv_dlength, ISCSI_PARAM_MAX_RECV_DLENGTH);
	SETUP_CONN_RD_ATTR(max_xmit_dlength, ISCSI_PARAM_MAX_XMIT_DLENGTH);
	SETUP_CONN_RD_ATTR(header_digest, ISCSI_PARAM_HDRDGST_EN);
	SETUP_CONN_RD_ATTR(data_digest, ISCSI_PARAM_DATADGST_EN);
	SETUP_CONN_RD_ATTR(ifmarker, ISCSI_PARAM_IFMARKER_EN);
	SETUP_CONN_RD_ATTR(ofmarker, ISCSI_PARAM_OFMARKER_EN);

	BUG_ON(count > ISCSI_CONN_ATTRS);
	priv->conn_attrs[count] = NULL;
	count = 0;

	/* session parameters */
	priv->session_cont.ac.attrs = &priv->session_attrs[0];
	priv->session_cont.ac.class = &iscsi_session_class.class;
	priv->session_cont.ac.match = iscsi_session_match;
	transport_container_register(&priv->session_cont);

	SETUP_SESSION_RD_ATTR(initial_r2t, ISCSI_PARAM_INITIAL_R2T_EN);
	SETUP_SESSION_RD_ATTR(max_outstanding_r2t, ISCSI_PARAM_MAX_R2T);
	SETUP_SESSION_RD_ATTR(immediate_data, ISCSI_PARAM_IMM_DATA_EN);
	SETUP_SESSION_RD_ATTR(first_burst_len, ISCSI_PARAM_FIRST_BURST);
	SETUP_SESSION_RD_ATTR(max_burst_len, ISCSI_PARAM_MAX_BURST);
	SETUP_SESSION_RD_ATTR(data_pdu_in_order, ISCSI_PARAM_PDU_INORDER_EN);
	SETUP_SESSION_RD_ATTR(data_seq_in_order,ISCSI_PARAM_DATASEQ_INORDER_EN)
	SETUP_SESSION_RD_ATTR(erl, ISCSI_PARAM_ERL);

	BUG_ON(count > ISCSI_SESSION_ATTRS);
	priv->session_attrs[count] = NULL;

	spin_lock_irqsave(&iscsi_transport_lock, flags);
	list_add(&priv->list, &iscsi_transports);
	spin_unlock_irqrestore(&iscsi_transport_lock, flags);

	printk(KERN_NOTICE "iscsi: registered transport (%s)\n", tt->name);
	return 0;

unregister_cdev:
	class_device_unregister(&priv->cdev);
free_priv:
	kfree(priv);
	return err;
}
EXPORT_SYMBOL_GPL(iscsi_register_transport);

int iscsi_unregister_transport(struct iscsi_transport *tt)
{
	struct iscsi_internal *priv;
	unsigned long flags;

	BUG_ON(!tt);

	down(&rx_queue_sema);

	priv = iscsi_if_transport_lookup(tt);
	BUG_ON (!priv);

	spin_lock_irqsave(&priv->session_lock, flags);
	if (!list_empty(&priv->sessions)) {
		spin_unlock_irqrestore(&priv->session_lock, flags);
		up(&rx_queue_sema);
		return -EPERM;
	}
	spin_unlock_irqrestore(&priv->session_lock, flags);

	spin_lock_irqsave(&iscsi_transport_lock, flags);
	list_del(&priv->list);
	spin_unlock_irqrestore(&iscsi_transport_lock, flags);

	transport_container_unregister(&priv->conn_cont);
	transport_container_unregister(&priv->session_cont);

	sysfs_remove_group(&priv->cdev.kobj, &iscsi_transport_group);
	class_device_unregister(&priv->cdev);
	up(&rx_queue_sema);

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_unregister_transport);

static int
iscsi_rcv_nl_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;

	if (event == NETLINK_URELEASE &&
	    n->protocol == NETLINK_ISCSI && n->pid) {
		struct iscsi_if_conn *conn;
		unsigned long flags;

		mempool_zone_complete(&z_reply);
		spin_lock_irqsave(&connlock, flags);
		list_for_each_entry(conn, &connlist, conn_list) {
			mempool_zone_complete(&conn->z_error);
			mempool_zone_complete(&conn->z_pdu);
		}
		spin_unlock_irqrestore(&connlock, flags);
	}

	return NOTIFY_DONE;
}

static struct notifier_block iscsi_nl_notifier = {
	.notifier_call	= iscsi_rcv_nl_event,
};

static __init int iscsi_transport_init(void)
{
	int err;

	err = class_register(&iscsi_transport_class);
	if (err)
		return err;

	err = transport_class_register(&iscsi_connection_class);
	if (err)
		goto unregister_transport_class;

	err = transport_class_register(&iscsi_session_class);
	if (err)
		goto unregister_conn_class;

	err = netlink_register_notifier(&iscsi_nl_notifier);
	if (err)
		goto unregister_session_class;

	nls = netlink_kernel_create(NETLINK_ISCSI, 1, iscsi_if_rx,
				    THIS_MODULE);
	if (!nls) {
		err = -ENOBUFS;
		goto unregister_notifier;
	}

	err = mempool_zone_init(&z_reply, Z_MAX_REPLY,
		NLMSG_SPACE(sizeof(struct iscsi_uevent)), Z_HIWAT_REPLY);
	if (!err)
		return 0;

	sock_release(nls->sk_socket);
unregister_notifier:
	netlink_unregister_notifier(&iscsi_nl_notifier);
unregister_session_class:
	transport_class_unregister(&iscsi_session_class);
unregister_conn_class:
	transport_class_unregister(&iscsi_connection_class);
unregister_transport_class:
	class_unregister(&iscsi_transport_class);
	return err;
}

static void __exit iscsi_transport_exit(void)
{
	mempool_destroy(z_reply.pool);
	sock_release(nls->sk_socket);
	netlink_unregister_notifier(&iscsi_nl_notifier);
	transport_class_unregister(&iscsi_connection_class);
	transport_class_unregister(&iscsi_session_class);
	class_unregister(&iscsi_transport_class);
}

module_init(iscsi_transport_init);
module_exit(iscsi_transport_exit);

MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>, "
	      "Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI Transport Interface");
MODULE_LICENSE("GPL");
