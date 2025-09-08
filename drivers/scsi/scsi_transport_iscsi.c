// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004 - 2005
 * Copyright (C) Dmitry Yusupov, 2004 - 2005
 * Copyright (C) Alex Aizman, 2004 - 2005
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/bsg-lib.h>
#include <linux/idr.h>
#include <net/tcp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/iscsi_if.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_bsg_iscsi.h>

#define ISCSI_TRANSPORT_VERSION "2.0-870"

#define ISCSI_SEND_MAX_ALLOWED  10

#define CREATE_TRACE_POINTS
#include <trace/events/iscsi.h>

/*
 * Export tracepoint symbols to be used by other modules.
 */
EXPORT_TRACEPOINT_SYMBOL_GPL(iscsi_dbg_conn);
EXPORT_TRACEPOINT_SYMBOL_GPL(iscsi_dbg_eh);
EXPORT_TRACEPOINT_SYMBOL_GPL(iscsi_dbg_session);
EXPORT_TRACEPOINT_SYMBOL_GPL(iscsi_dbg_tcp);
EXPORT_TRACEPOINT_SYMBOL_GPL(iscsi_dbg_sw_tcp);

static int dbg_session;
module_param_named(debug_session, dbg_session, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_session,
		 "Turn on debugging for sessions in scsi_transport_iscsi "
		 "module. Set to 1 to turn on, and zero to turn off. Default "
		 "is off.");

static int dbg_conn;
module_param_named(debug_conn, dbg_conn, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_conn,
		 "Turn on debugging for connections in scsi_transport_iscsi "
		 "module. Set to 1 to turn on, and zero to turn off. Default "
		 "is off.");

#define ISCSI_DBG_TRANS_SESSION(_session, dbg_fmt, arg...)		\
	do {								\
		if (dbg_session)					\
			iscsi_cls_session_printk(KERN_INFO, _session,	\
						 "%s: " dbg_fmt,	\
						 __func__, ##arg);	\
		iscsi_dbg_trace(trace_iscsi_dbg_trans_session,		\
				&(_session)->dev,			\
				"%s " dbg_fmt, __func__, ##arg);	\
	} while (0);

#define ISCSI_DBG_TRANS_CONN(_conn, dbg_fmt, arg...)			\
	do {								\
		if (dbg_conn)						\
			iscsi_cls_conn_printk(KERN_INFO, _conn,		\
					      "%s: " dbg_fmt,		\
					      __func__, ##arg);		\
		iscsi_dbg_trace(trace_iscsi_dbg_trans_conn,		\
				&(_conn)->dev,				\
				"%s " dbg_fmt, __func__, ##arg);	\
	} while (0);

struct iscsi_internal {
	struct scsi_transport_template t;
	struct iscsi_transport *iscsi_transport;
	struct list_head list;
	struct device dev;

	struct transport_container conn_cont;
	struct transport_container session_cont;
};

static DEFINE_IDR(iscsi_ep_idr);
static DEFINE_MUTEX(iscsi_ep_idr_mutex);

static atomic_t iscsi_session_nr; /* sysfs session id for next new session */

static struct workqueue_struct *iscsi_conn_cleanup_workq;

static DEFINE_IDA(iscsi_sess_ida);
/*
 * list of registered transports and lock that must
 * be held while accessing list. The iscsi_transport_lock must
 * be acquired after the rx_queue_mutex.
 */
static LIST_HEAD(iscsi_transports);
static DEFINE_SPINLOCK(iscsi_transport_lock);

#define to_iscsi_internal(tmpl) \
	container_of(tmpl, struct iscsi_internal, t)

#define dev_to_iscsi_internal(_dev) \
	container_of(_dev, struct iscsi_internal, dev)

static void iscsi_transport_release(struct device *dev)
{
	struct iscsi_internal *priv = dev_to_iscsi_internal(dev);
	kfree(priv);
}

/*
 * iscsi_transport_class represents the iscsi_transports that are
 * registered.
 */
static struct class iscsi_transport_class = {
	.name = "iscsi_transport",
	.dev_release = iscsi_transport_release,
};

static ssize_t
show_transport_handle(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct iscsi_internal *priv = dev_to_iscsi_internal(dev);

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return sysfs_emit(buf, "%llu\n",
		  (unsigned long long)iscsi_handle(priv->iscsi_transport));
}
static DEVICE_ATTR(handle, S_IRUGO, show_transport_handle, NULL);

#define show_transport_attr(name, format)				\
static ssize_t								\
show_transport_##name(struct device *dev, 				\
		      struct device_attribute *attr,char *buf)		\
{									\
	struct iscsi_internal *priv = dev_to_iscsi_internal(dev);	\
	return sysfs_emit(buf, format"\n", priv->iscsi_transport->name);\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_transport_##name, NULL);

show_transport_attr(caps, "0x%x");

static struct attribute *iscsi_transport_attrs[] = {
	&dev_attr_handle.attr,
	&dev_attr_caps.attr,
	NULL,
};

static struct attribute_group iscsi_transport_group = {
	.attrs = iscsi_transport_attrs,
};

/*
 * iSCSI endpoint attrs
 */
#define iscsi_dev_to_endpoint(_dev) \
	container_of(_dev, struct iscsi_endpoint, dev)

#define ISCSI_ATTR(_prefix,_name,_mode,_show,_store)	\
struct device_attribute dev_attr_##_prefix##_##_name =	\
        __ATTR(_name,_mode,_show,_store)

static void iscsi_endpoint_release(struct device *dev)
{
	struct iscsi_endpoint *ep = iscsi_dev_to_endpoint(dev);

	mutex_lock(&iscsi_ep_idr_mutex);
	idr_remove(&iscsi_ep_idr, ep->id);
	mutex_unlock(&iscsi_ep_idr_mutex);

	kfree(ep);
}

static struct class iscsi_endpoint_class = {
	.name = "iscsi_endpoint",
	.dev_release = iscsi_endpoint_release,
};

static ssize_t
show_ep_handle(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct iscsi_endpoint *ep = iscsi_dev_to_endpoint(dev);
	return sysfs_emit(buf, "%d\n", ep->id);
}
static ISCSI_ATTR(ep, handle, S_IRUGO, show_ep_handle, NULL);

static struct attribute *iscsi_endpoint_attrs[] = {
	&dev_attr_ep_handle.attr,
	NULL,
};

static struct attribute_group iscsi_endpoint_group = {
	.attrs = iscsi_endpoint_attrs,
};

struct iscsi_endpoint *
iscsi_create_endpoint(int dd_size)
{
	struct iscsi_endpoint *ep;
	int err, id;

	ep = kzalloc(sizeof(*ep) + dd_size, GFP_KERNEL);
	if (!ep)
		return NULL;

	mutex_lock(&iscsi_ep_idr_mutex);

	/*
	 * First endpoint id should be 1 to comply with user space
	 * applications (iscsid).
	 */
	id = idr_alloc(&iscsi_ep_idr, ep, 1, -1, GFP_NOIO);
	if (id < 0) {
		mutex_unlock(&iscsi_ep_idr_mutex);
		printk(KERN_ERR "Could not allocate endpoint ID. Error %d.\n",
		       id);
		goto free_ep;
	}
	mutex_unlock(&iscsi_ep_idr_mutex);

	ep->id = id;
	ep->dev.class = &iscsi_endpoint_class;
	dev_set_name(&ep->dev, "ep-%d", id);
	err = device_register(&ep->dev);
        if (err)
		goto put_dev;

	err = sysfs_create_group(&ep->dev.kobj, &iscsi_endpoint_group);
	if (err)
		goto unregister_dev;

	if (dd_size)
		ep->dd_data = &ep[1];
	return ep;

unregister_dev:
	device_unregister(&ep->dev);
	return NULL;

put_dev:
	mutex_lock(&iscsi_ep_idr_mutex);
	idr_remove(&iscsi_ep_idr, id);
	mutex_unlock(&iscsi_ep_idr_mutex);
	put_device(&ep->dev);
	return NULL;
free_ep:
	kfree(ep);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_create_endpoint);

void iscsi_destroy_endpoint(struct iscsi_endpoint *ep)
{
	sysfs_remove_group(&ep->dev.kobj, &iscsi_endpoint_group);
	device_unregister(&ep->dev);
}
EXPORT_SYMBOL_GPL(iscsi_destroy_endpoint);

void iscsi_put_endpoint(struct iscsi_endpoint *ep)
{
	put_device(&ep->dev);
}
EXPORT_SYMBOL_GPL(iscsi_put_endpoint);

/**
 * iscsi_lookup_endpoint - get ep from handle
 * @handle: endpoint handle
 *
 * Caller must do a iscsi_put_endpoint.
 */
struct iscsi_endpoint *iscsi_lookup_endpoint(u64 handle)
{
	struct iscsi_endpoint *ep;

	mutex_lock(&iscsi_ep_idr_mutex);
	ep = idr_find(&iscsi_ep_idr, handle);
	if (!ep)
		goto unlock;

	get_device(&ep->dev);
unlock:
	mutex_unlock(&iscsi_ep_idr_mutex);
	return ep;
}
EXPORT_SYMBOL_GPL(iscsi_lookup_endpoint);

/*
 * Interface to display network param to sysfs
 */

static void iscsi_iface_release(struct device *dev)
{
	struct iscsi_iface *iface = iscsi_dev_to_iface(dev);
	struct device *parent = iface->dev.parent;

	kfree(iface);
	put_device(parent);
}


static struct class iscsi_iface_class = {
	.name = "iscsi_iface",
	.dev_release = iscsi_iface_release,
};

#define ISCSI_IFACE_ATTR(_prefix, _name, _mode, _show, _store)	\
struct device_attribute dev_attr_##_prefix##_##_name =		\
	__ATTR(_name, _mode, _show, _store)

/* iface attrs show */
#define iscsi_iface_attr_show(type, name, param_type, param)		\
static ssize_t								\
show_##type##_##name(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct iscsi_iface *iface = iscsi_dev_to_iface(dev);		\
	struct iscsi_transport *t = iface->transport;			\
	return t->get_iface_param(iface, param_type, param, buf);	\
}									\

#define iscsi_iface_net_attr(type, name, param)				\
	iscsi_iface_attr_show(type, name, ISCSI_NET_PARAM, param)	\
static ISCSI_IFACE_ATTR(type, name, S_IRUGO, show_##type##_##name, NULL);

#define iscsi_iface_attr(type, name, param)				\
	iscsi_iface_attr_show(type, name, ISCSI_IFACE_PARAM, param)	\
static ISCSI_IFACE_ATTR(type, name, S_IRUGO, show_##type##_##name, NULL);

/* generic read only ipv4 attribute */
iscsi_iface_net_attr(ipv4_iface, ipaddress, ISCSI_NET_PARAM_IPV4_ADDR);
iscsi_iface_net_attr(ipv4_iface, gateway, ISCSI_NET_PARAM_IPV4_GW);
iscsi_iface_net_attr(ipv4_iface, subnet, ISCSI_NET_PARAM_IPV4_SUBNET);
iscsi_iface_net_attr(ipv4_iface, bootproto, ISCSI_NET_PARAM_IPV4_BOOTPROTO);
iscsi_iface_net_attr(ipv4_iface, dhcp_dns_address_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN);
iscsi_iface_net_attr(ipv4_iface, dhcp_slp_da_info_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN);
iscsi_iface_net_attr(ipv4_iface, tos_en, ISCSI_NET_PARAM_IPV4_TOS_EN);
iscsi_iface_net_attr(ipv4_iface, tos, ISCSI_NET_PARAM_IPV4_TOS);
iscsi_iface_net_attr(ipv4_iface, grat_arp_en,
		     ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN);
iscsi_iface_net_attr(ipv4_iface, dhcp_alt_client_id_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN);
iscsi_iface_net_attr(ipv4_iface, dhcp_alt_client_id,
		     ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID);
iscsi_iface_net_attr(ipv4_iface, dhcp_req_vendor_id_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN);
iscsi_iface_net_attr(ipv4_iface, dhcp_use_vendor_id_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN);
iscsi_iface_net_attr(ipv4_iface, dhcp_vendor_id,
		     ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID);
iscsi_iface_net_attr(ipv4_iface, dhcp_learn_iqn_en,
		     ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN);
iscsi_iface_net_attr(ipv4_iface, fragment_disable,
		     ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE);
iscsi_iface_net_attr(ipv4_iface, incoming_forwarding_en,
		     ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN);
iscsi_iface_net_attr(ipv4_iface, ttl, ISCSI_NET_PARAM_IPV4_TTL);

/* generic read only ipv6 attribute */
iscsi_iface_net_attr(ipv6_iface, ipaddress, ISCSI_NET_PARAM_IPV6_ADDR);
iscsi_iface_net_attr(ipv6_iface, link_local_addr,
		     ISCSI_NET_PARAM_IPV6_LINKLOCAL);
iscsi_iface_net_attr(ipv6_iface, router_addr, ISCSI_NET_PARAM_IPV6_ROUTER);
iscsi_iface_net_attr(ipv6_iface, ipaddr_autocfg,
		     ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG);
iscsi_iface_net_attr(ipv6_iface, link_local_autocfg,
		     ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG);
iscsi_iface_net_attr(ipv6_iface, link_local_state,
		     ISCSI_NET_PARAM_IPV6_LINKLOCAL_STATE);
iscsi_iface_net_attr(ipv6_iface, router_state,
		     ISCSI_NET_PARAM_IPV6_ROUTER_STATE);
iscsi_iface_net_attr(ipv6_iface, grat_neighbor_adv_en,
		     ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN);
iscsi_iface_net_attr(ipv6_iface, mld_en, ISCSI_NET_PARAM_IPV6_MLD_EN);
iscsi_iface_net_attr(ipv6_iface, flow_label, ISCSI_NET_PARAM_IPV6_FLOW_LABEL);
iscsi_iface_net_attr(ipv6_iface, traffic_class,
		     ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS);
iscsi_iface_net_attr(ipv6_iface, hop_limit, ISCSI_NET_PARAM_IPV6_HOP_LIMIT);
iscsi_iface_net_attr(ipv6_iface, nd_reachable_tmo,
		     ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO);
iscsi_iface_net_attr(ipv6_iface, nd_rexmit_time,
		     ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME);
iscsi_iface_net_attr(ipv6_iface, nd_stale_tmo,
		     ISCSI_NET_PARAM_IPV6_ND_STALE_TMO);
iscsi_iface_net_attr(ipv6_iface, dup_addr_detect_cnt,
		     ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT);
iscsi_iface_net_attr(ipv6_iface, router_adv_link_mtu,
		     ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU);

/* common read only iface attribute */
iscsi_iface_net_attr(iface, enabled, ISCSI_NET_PARAM_IFACE_ENABLE);
iscsi_iface_net_attr(iface, vlan_id, ISCSI_NET_PARAM_VLAN_ID);
iscsi_iface_net_attr(iface, vlan_priority, ISCSI_NET_PARAM_VLAN_PRIORITY);
iscsi_iface_net_attr(iface, vlan_enabled, ISCSI_NET_PARAM_VLAN_ENABLED);
iscsi_iface_net_attr(iface, mtu, ISCSI_NET_PARAM_MTU);
iscsi_iface_net_attr(iface, port, ISCSI_NET_PARAM_PORT);
iscsi_iface_net_attr(iface, ipaddress_state, ISCSI_NET_PARAM_IPADDR_STATE);
iscsi_iface_net_attr(iface, delayed_ack_en, ISCSI_NET_PARAM_DELAYED_ACK_EN);
iscsi_iface_net_attr(iface, tcp_nagle_disable,
		     ISCSI_NET_PARAM_TCP_NAGLE_DISABLE);
iscsi_iface_net_attr(iface, tcp_wsf_disable, ISCSI_NET_PARAM_TCP_WSF_DISABLE);
iscsi_iface_net_attr(iface, tcp_wsf, ISCSI_NET_PARAM_TCP_WSF);
iscsi_iface_net_attr(iface, tcp_timer_scale, ISCSI_NET_PARAM_TCP_TIMER_SCALE);
iscsi_iface_net_attr(iface, tcp_timestamp_en, ISCSI_NET_PARAM_TCP_TIMESTAMP_EN);
iscsi_iface_net_attr(iface, cache_id, ISCSI_NET_PARAM_CACHE_ID);
iscsi_iface_net_attr(iface, redirect_en, ISCSI_NET_PARAM_REDIRECT_EN);

/* common iscsi specific settings attributes */
iscsi_iface_attr(iface, def_taskmgmt_tmo, ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO);
iscsi_iface_attr(iface, header_digest, ISCSI_IFACE_PARAM_HDRDGST_EN);
iscsi_iface_attr(iface, data_digest, ISCSI_IFACE_PARAM_DATADGST_EN);
iscsi_iface_attr(iface, immediate_data, ISCSI_IFACE_PARAM_IMM_DATA_EN);
iscsi_iface_attr(iface, initial_r2t, ISCSI_IFACE_PARAM_INITIAL_R2T_EN);
iscsi_iface_attr(iface, data_seq_in_order,
		 ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN);
iscsi_iface_attr(iface, data_pdu_in_order, ISCSI_IFACE_PARAM_PDU_INORDER_EN);
iscsi_iface_attr(iface, erl, ISCSI_IFACE_PARAM_ERL);
iscsi_iface_attr(iface, max_recv_dlength, ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH);
iscsi_iface_attr(iface, first_burst_len, ISCSI_IFACE_PARAM_FIRST_BURST);
iscsi_iface_attr(iface, max_outstanding_r2t, ISCSI_IFACE_PARAM_MAX_R2T);
iscsi_iface_attr(iface, max_burst_len, ISCSI_IFACE_PARAM_MAX_BURST);
iscsi_iface_attr(iface, chap_auth, ISCSI_IFACE_PARAM_CHAP_AUTH_EN);
iscsi_iface_attr(iface, bidi_chap, ISCSI_IFACE_PARAM_BIDI_CHAP_EN);
iscsi_iface_attr(iface, discovery_auth_optional,
		 ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL);
iscsi_iface_attr(iface, discovery_logout,
		 ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN);
iscsi_iface_attr(iface, strict_login_comp_en,
		 ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN);
iscsi_iface_attr(iface, initiator_name, ISCSI_IFACE_PARAM_INITIATOR_NAME);

static umode_t iscsi_iface_attr_is_visible(struct kobject *kobj,
					  struct attribute *attr, int i)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iscsi_iface *iface = iscsi_dev_to_iface(dev);
	struct iscsi_transport *t = iface->transport;
	int param = -1;

	if (attr == &dev_attr_iface_def_taskmgmt_tmo.attr)
		param = ISCSI_IFACE_PARAM_DEF_TASKMGMT_TMO;
	else if (attr == &dev_attr_iface_header_digest.attr)
		param = ISCSI_IFACE_PARAM_HDRDGST_EN;
	else if (attr == &dev_attr_iface_data_digest.attr)
		param = ISCSI_IFACE_PARAM_DATADGST_EN;
	else if (attr == &dev_attr_iface_immediate_data.attr)
		param = ISCSI_IFACE_PARAM_IMM_DATA_EN;
	else if (attr == &dev_attr_iface_initial_r2t.attr)
		param = ISCSI_IFACE_PARAM_INITIAL_R2T_EN;
	else if (attr == &dev_attr_iface_data_seq_in_order.attr)
		param = ISCSI_IFACE_PARAM_DATASEQ_INORDER_EN;
	else if (attr == &dev_attr_iface_data_pdu_in_order.attr)
		param = ISCSI_IFACE_PARAM_PDU_INORDER_EN;
	else if (attr == &dev_attr_iface_erl.attr)
		param = ISCSI_IFACE_PARAM_ERL;
	else if (attr == &dev_attr_iface_max_recv_dlength.attr)
		param = ISCSI_IFACE_PARAM_MAX_RECV_DLENGTH;
	else if (attr == &dev_attr_iface_first_burst_len.attr)
		param = ISCSI_IFACE_PARAM_FIRST_BURST;
	else if (attr == &dev_attr_iface_max_outstanding_r2t.attr)
		param = ISCSI_IFACE_PARAM_MAX_R2T;
	else if (attr == &dev_attr_iface_max_burst_len.attr)
		param = ISCSI_IFACE_PARAM_MAX_BURST;
	else if (attr == &dev_attr_iface_chap_auth.attr)
		param = ISCSI_IFACE_PARAM_CHAP_AUTH_EN;
	else if (attr == &dev_attr_iface_bidi_chap.attr)
		param = ISCSI_IFACE_PARAM_BIDI_CHAP_EN;
	else if (attr == &dev_attr_iface_discovery_auth_optional.attr)
		param = ISCSI_IFACE_PARAM_DISCOVERY_AUTH_OPTIONAL;
	else if (attr == &dev_attr_iface_discovery_logout.attr)
		param = ISCSI_IFACE_PARAM_DISCOVERY_LOGOUT_EN;
	else if (attr == &dev_attr_iface_strict_login_comp_en.attr)
		param = ISCSI_IFACE_PARAM_STRICT_LOGIN_COMP_EN;
	else if (attr == &dev_attr_iface_initiator_name.attr)
		param = ISCSI_IFACE_PARAM_INITIATOR_NAME;

	if (param != -1)
		return t->attr_is_visible(ISCSI_IFACE_PARAM, param);

	if (attr == &dev_attr_iface_enabled.attr)
		param = ISCSI_NET_PARAM_IFACE_ENABLE;
	else if (attr == &dev_attr_iface_vlan_id.attr)
		param = ISCSI_NET_PARAM_VLAN_ID;
	else if (attr == &dev_attr_iface_vlan_priority.attr)
		param = ISCSI_NET_PARAM_VLAN_PRIORITY;
	else if (attr == &dev_attr_iface_vlan_enabled.attr)
		param = ISCSI_NET_PARAM_VLAN_ENABLED;
	else if (attr == &dev_attr_iface_mtu.attr)
		param = ISCSI_NET_PARAM_MTU;
	else if (attr == &dev_attr_iface_port.attr)
		param = ISCSI_NET_PARAM_PORT;
	else if (attr == &dev_attr_iface_ipaddress_state.attr)
		param = ISCSI_NET_PARAM_IPADDR_STATE;
	else if (attr == &dev_attr_iface_delayed_ack_en.attr)
		param = ISCSI_NET_PARAM_DELAYED_ACK_EN;
	else if (attr == &dev_attr_iface_tcp_nagle_disable.attr)
		param = ISCSI_NET_PARAM_TCP_NAGLE_DISABLE;
	else if (attr == &dev_attr_iface_tcp_wsf_disable.attr)
		param = ISCSI_NET_PARAM_TCP_WSF_DISABLE;
	else if (attr == &dev_attr_iface_tcp_wsf.attr)
		param = ISCSI_NET_PARAM_TCP_WSF;
	else if (attr == &dev_attr_iface_tcp_timer_scale.attr)
		param = ISCSI_NET_PARAM_TCP_TIMER_SCALE;
	else if (attr == &dev_attr_iface_tcp_timestamp_en.attr)
		param = ISCSI_NET_PARAM_TCP_TIMESTAMP_EN;
	else if (attr == &dev_attr_iface_cache_id.attr)
		param = ISCSI_NET_PARAM_CACHE_ID;
	else if (attr == &dev_attr_iface_redirect_en.attr)
		param = ISCSI_NET_PARAM_REDIRECT_EN;
	else if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4) {
		if (attr == &dev_attr_ipv4_iface_ipaddress.attr)
			param = ISCSI_NET_PARAM_IPV4_ADDR;
		else if (attr == &dev_attr_ipv4_iface_gateway.attr)
			param = ISCSI_NET_PARAM_IPV4_GW;
		else if (attr == &dev_attr_ipv4_iface_subnet.attr)
			param = ISCSI_NET_PARAM_IPV4_SUBNET;
		else if (attr == &dev_attr_ipv4_iface_bootproto.attr)
			param = ISCSI_NET_PARAM_IPV4_BOOTPROTO;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_dns_address_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_DNS_ADDR_EN;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_slp_da_info_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_SLP_DA_EN;
		else if (attr == &dev_attr_ipv4_iface_tos_en.attr)
			param = ISCSI_NET_PARAM_IPV4_TOS_EN;
		else if (attr == &dev_attr_ipv4_iface_tos.attr)
			param = ISCSI_NET_PARAM_IPV4_TOS;
		else if (attr == &dev_attr_ipv4_iface_grat_arp_en.attr)
			param = ISCSI_NET_PARAM_IPV4_GRAT_ARP_EN;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_alt_client_id_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID_EN;
		else if (attr == &dev_attr_ipv4_iface_dhcp_alt_client_id.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_ALT_CLIENT_ID;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_req_vendor_id_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_REQ_VENDOR_ID_EN;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_use_vendor_id_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_USE_VENDOR_ID_EN;
		else if (attr == &dev_attr_ipv4_iface_dhcp_vendor_id.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_VENDOR_ID;
		else if (attr ==
			 &dev_attr_ipv4_iface_dhcp_learn_iqn_en.attr)
			param = ISCSI_NET_PARAM_IPV4_DHCP_LEARN_IQN_EN;
		else if (attr ==
			 &dev_attr_ipv4_iface_fragment_disable.attr)
			param = ISCSI_NET_PARAM_IPV4_FRAGMENT_DISABLE;
		else if (attr ==
			 &dev_attr_ipv4_iface_incoming_forwarding_en.attr)
			param = ISCSI_NET_PARAM_IPV4_IN_FORWARD_EN;
		else if (attr == &dev_attr_ipv4_iface_ttl.attr)
			param = ISCSI_NET_PARAM_IPV4_TTL;
		else
			return 0;
	} else if (iface->iface_type == ISCSI_IFACE_TYPE_IPV6) {
		if (attr == &dev_attr_ipv6_iface_ipaddress.attr)
			param = ISCSI_NET_PARAM_IPV6_ADDR;
		else if (attr == &dev_attr_ipv6_iface_link_local_addr.attr)
			param = ISCSI_NET_PARAM_IPV6_LINKLOCAL;
		else if (attr == &dev_attr_ipv6_iface_router_addr.attr)
			param = ISCSI_NET_PARAM_IPV6_ROUTER;
		else if (attr == &dev_attr_ipv6_iface_ipaddr_autocfg.attr)
			param = ISCSI_NET_PARAM_IPV6_ADDR_AUTOCFG;
		else if (attr == &dev_attr_ipv6_iface_link_local_autocfg.attr)
			param = ISCSI_NET_PARAM_IPV6_LINKLOCAL_AUTOCFG;
		else if (attr == &dev_attr_ipv6_iface_link_local_state.attr)
			param = ISCSI_NET_PARAM_IPV6_LINKLOCAL_STATE;
		else if (attr == &dev_attr_ipv6_iface_router_state.attr)
			param = ISCSI_NET_PARAM_IPV6_ROUTER_STATE;
		else if (attr ==
			 &dev_attr_ipv6_iface_grat_neighbor_adv_en.attr)
			param = ISCSI_NET_PARAM_IPV6_GRAT_NEIGHBOR_ADV_EN;
		else if (attr == &dev_attr_ipv6_iface_mld_en.attr)
			param = ISCSI_NET_PARAM_IPV6_MLD_EN;
		else if (attr == &dev_attr_ipv6_iface_flow_label.attr)
			param = ISCSI_NET_PARAM_IPV6_FLOW_LABEL;
		else if (attr == &dev_attr_ipv6_iface_traffic_class.attr)
			param = ISCSI_NET_PARAM_IPV6_TRAFFIC_CLASS;
		else if (attr == &dev_attr_ipv6_iface_hop_limit.attr)
			param = ISCSI_NET_PARAM_IPV6_HOP_LIMIT;
		else if (attr == &dev_attr_ipv6_iface_nd_reachable_tmo.attr)
			param = ISCSI_NET_PARAM_IPV6_ND_REACHABLE_TMO;
		else if (attr == &dev_attr_ipv6_iface_nd_rexmit_time.attr)
			param = ISCSI_NET_PARAM_IPV6_ND_REXMIT_TIME;
		else if (attr == &dev_attr_ipv6_iface_nd_stale_tmo.attr)
			param = ISCSI_NET_PARAM_IPV6_ND_STALE_TMO;
		else if (attr == &dev_attr_ipv6_iface_dup_addr_detect_cnt.attr)
			param = ISCSI_NET_PARAM_IPV6_DUP_ADDR_DETECT_CNT;
		else if (attr == &dev_attr_ipv6_iface_router_adv_link_mtu.attr)
			param = ISCSI_NET_PARAM_IPV6_RTR_ADV_LINK_MTU;
		else
			return 0;
	} else {
		WARN_ONCE(1, "Invalid iface attr");
		return 0;
	}

	return t->attr_is_visible(ISCSI_NET_PARAM, param);
}

static struct attribute *iscsi_iface_attrs[] = {
	&dev_attr_iface_enabled.attr,
	&dev_attr_iface_vlan_id.attr,
	&dev_attr_iface_vlan_priority.attr,
	&dev_attr_iface_vlan_enabled.attr,
	&dev_attr_ipv4_iface_ipaddress.attr,
	&dev_attr_ipv4_iface_gateway.attr,
	&dev_attr_ipv4_iface_subnet.attr,
	&dev_attr_ipv4_iface_bootproto.attr,
	&dev_attr_ipv6_iface_ipaddress.attr,
	&dev_attr_ipv6_iface_link_local_addr.attr,
	&dev_attr_ipv6_iface_router_addr.attr,
	&dev_attr_ipv6_iface_ipaddr_autocfg.attr,
	&dev_attr_ipv6_iface_link_local_autocfg.attr,
	&dev_attr_iface_mtu.attr,
	&dev_attr_iface_port.attr,
	&dev_attr_iface_ipaddress_state.attr,
	&dev_attr_iface_delayed_ack_en.attr,
	&dev_attr_iface_tcp_nagle_disable.attr,
	&dev_attr_iface_tcp_wsf_disable.attr,
	&dev_attr_iface_tcp_wsf.attr,
	&dev_attr_iface_tcp_timer_scale.attr,
	&dev_attr_iface_tcp_timestamp_en.attr,
	&dev_attr_iface_cache_id.attr,
	&dev_attr_iface_redirect_en.attr,
	&dev_attr_iface_def_taskmgmt_tmo.attr,
	&dev_attr_iface_header_digest.attr,
	&dev_attr_iface_data_digest.attr,
	&dev_attr_iface_immediate_data.attr,
	&dev_attr_iface_initial_r2t.attr,
	&dev_attr_iface_data_seq_in_order.attr,
	&dev_attr_iface_data_pdu_in_order.attr,
	&dev_attr_iface_erl.attr,
	&dev_attr_iface_max_recv_dlength.attr,
	&dev_attr_iface_first_burst_len.attr,
	&dev_attr_iface_max_outstanding_r2t.attr,
	&dev_attr_iface_max_burst_len.attr,
	&dev_attr_iface_chap_auth.attr,
	&dev_attr_iface_bidi_chap.attr,
	&dev_attr_iface_discovery_auth_optional.attr,
	&dev_attr_iface_discovery_logout.attr,
	&dev_attr_iface_strict_login_comp_en.attr,
	&dev_attr_iface_initiator_name.attr,
	&dev_attr_ipv4_iface_dhcp_dns_address_en.attr,
	&dev_attr_ipv4_iface_dhcp_slp_da_info_en.attr,
	&dev_attr_ipv4_iface_tos_en.attr,
	&dev_attr_ipv4_iface_tos.attr,
	&dev_attr_ipv4_iface_grat_arp_en.attr,
	&dev_attr_ipv4_iface_dhcp_alt_client_id_en.attr,
	&dev_attr_ipv4_iface_dhcp_alt_client_id.attr,
	&dev_attr_ipv4_iface_dhcp_req_vendor_id_en.attr,
	&dev_attr_ipv4_iface_dhcp_use_vendor_id_en.attr,
	&dev_attr_ipv4_iface_dhcp_vendor_id.attr,
	&dev_attr_ipv4_iface_dhcp_learn_iqn_en.attr,
	&dev_attr_ipv4_iface_fragment_disable.attr,
	&dev_attr_ipv4_iface_incoming_forwarding_en.attr,
	&dev_attr_ipv4_iface_ttl.attr,
	&dev_attr_ipv6_iface_link_local_state.attr,
	&dev_attr_ipv6_iface_router_state.attr,
	&dev_attr_ipv6_iface_grat_neighbor_adv_en.attr,
	&dev_attr_ipv6_iface_mld_en.attr,
	&dev_attr_ipv6_iface_flow_label.attr,
	&dev_attr_ipv6_iface_traffic_class.attr,
	&dev_attr_ipv6_iface_hop_limit.attr,
	&dev_attr_ipv6_iface_nd_reachable_tmo.attr,
	&dev_attr_ipv6_iface_nd_rexmit_time.attr,
	&dev_attr_ipv6_iface_nd_stale_tmo.attr,
	&dev_attr_ipv6_iface_dup_addr_detect_cnt.attr,
	&dev_attr_ipv6_iface_router_adv_link_mtu.attr,
	NULL,
};

static struct attribute_group iscsi_iface_group = {
	.attrs = iscsi_iface_attrs,
	.is_visible = iscsi_iface_attr_is_visible,
};

/* convert iscsi_ipaddress_state values to ascii string name */
static const struct {
	enum iscsi_ipaddress_state	value;
	char				*name;
} iscsi_ipaddress_state_names[] = {
	{ISCSI_IPDDRESS_STATE_UNCONFIGURED,	"Unconfigured" },
	{ISCSI_IPDDRESS_STATE_ACQUIRING,	"Acquiring" },
	{ISCSI_IPDDRESS_STATE_TENTATIVE,	"Tentative" },
	{ISCSI_IPDDRESS_STATE_VALID,		"Valid" },
	{ISCSI_IPDDRESS_STATE_DISABLING,	"Disabling" },
	{ISCSI_IPDDRESS_STATE_INVALID,		"Invalid" },
	{ISCSI_IPDDRESS_STATE_DEPRECATED,	"Deprecated" },
};

char *iscsi_get_ipaddress_state_name(enum iscsi_ipaddress_state port_state)
{
	int i;
	char *state = NULL;

	for (i = 0; i < ARRAY_SIZE(iscsi_ipaddress_state_names); i++) {
		if (iscsi_ipaddress_state_names[i].value == port_state) {
			state = iscsi_ipaddress_state_names[i].name;
			break;
		}
	}
	return state;
}
EXPORT_SYMBOL_GPL(iscsi_get_ipaddress_state_name);

/* convert iscsi_router_state values to ascii string name */
static const struct {
	enum iscsi_router_state	value;
	char			*name;
} iscsi_router_state_names[] = {
	{ISCSI_ROUTER_STATE_UNKNOWN,		"Unknown" },
	{ISCSI_ROUTER_STATE_ADVERTISED,		"Advertised" },
	{ISCSI_ROUTER_STATE_MANUAL,		"Manual" },
	{ISCSI_ROUTER_STATE_STALE,		"Stale" },
};

char *iscsi_get_router_state_name(enum iscsi_router_state router_state)
{
	int i;
	char *state = NULL;

	for (i = 0; i < ARRAY_SIZE(iscsi_router_state_names); i++) {
		if (iscsi_router_state_names[i].value == router_state) {
			state = iscsi_router_state_names[i].name;
			break;
		}
	}
	return state;
}
EXPORT_SYMBOL_GPL(iscsi_get_router_state_name);

struct iscsi_iface *
iscsi_create_iface(struct Scsi_Host *shost, struct iscsi_transport *transport,
		   uint32_t iface_type, uint32_t iface_num, int dd_size)
{
	struct iscsi_iface *iface;
	int err;

	iface = kzalloc(sizeof(*iface) + dd_size, GFP_KERNEL);
	if (!iface)
		return NULL;

	iface->transport = transport;
	iface->iface_type = iface_type;
	iface->iface_num = iface_num;
	iface->dev.release = iscsi_iface_release;
	iface->dev.class = &iscsi_iface_class;
	/* parent reference released in iscsi_iface_release */
	iface->dev.parent = get_device(&shost->shost_gendev);
	if (iface_type == ISCSI_IFACE_TYPE_IPV4)
		dev_set_name(&iface->dev, "ipv4-iface-%u-%u", shost->host_no,
			     iface_num);
	else
		dev_set_name(&iface->dev, "ipv6-iface-%u-%u", shost->host_no,
			     iface_num);

	err = device_register(&iface->dev);
	if (err)
		goto put_dev;

	err = sysfs_create_group(&iface->dev.kobj, &iscsi_iface_group);
	if (err)
		goto unreg_iface;

	if (dd_size)
		iface->dd_data = &iface[1];
	return iface;

unreg_iface:
	device_unregister(&iface->dev);
	return NULL;

put_dev:
	put_device(&iface->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_create_iface);

void iscsi_destroy_iface(struct iscsi_iface *iface)
{
	sysfs_remove_group(&iface->dev.kobj, &iscsi_iface_group);
	device_unregister(&iface->dev);
}
EXPORT_SYMBOL_GPL(iscsi_destroy_iface);

/*
 * Interface to display flash node params to sysfs
 */

#define ISCSI_FLASHNODE_ATTR(_prefix, _name, _mode, _show, _store)	\
struct device_attribute dev_attr_##_prefix##_##_name =			\
	__ATTR(_name, _mode, _show, _store)

/* flash node session attrs show */
#define iscsi_flashnode_sess_attr_show(type, name, param)		\
static ssize_t								\
show_##type##_##name(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct iscsi_bus_flash_session *fnode_sess =			\
					iscsi_dev_to_flash_session(dev);\
	struct iscsi_transport *t = fnode_sess->transport;		\
	return t->get_flashnode_param(fnode_sess, param, buf);		\
}									\


#define iscsi_flashnode_sess_attr(type, name, param)			\
	iscsi_flashnode_sess_attr_show(type, name, param)		\
static ISCSI_FLASHNODE_ATTR(type, name, S_IRUGO,			\
			    show_##type##_##name, NULL);

/* Flash node session attributes */

iscsi_flashnode_sess_attr(fnode, auto_snd_tgt_disable,
			  ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE);
iscsi_flashnode_sess_attr(fnode, discovery_session,
			  ISCSI_FLASHNODE_DISCOVERY_SESS);
iscsi_flashnode_sess_attr(fnode, portal_type, ISCSI_FLASHNODE_PORTAL_TYPE);
iscsi_flashnode_sess_attr(fnode, entry_enable, ISCSI_FLASHNODE_ENTRY_EN);
iscsi_flashnode_sess_attr(fnode, immediate_data, ISCSI_FLASHNODE_IMM_DATA_EN);
iscsi_flashnode_sess_attr(fnode, initial_r2t, ISCSI_FLASHNODE_INITIAL_R2T_EN);
iscsi_flashnode_sess_attr(fnode, data_seq_in_order,
			  ISCSI_FLASHNODE_DATASEQ_INORDER);
iscsi_flashnode_sess_attr(fnode, data_pdu_in_order,
			  ISCSI_FLASHNODE_PDU_INORDER);
iscsi_flashnode_sess_attr(fnode, chap_auth, ISCSI_FLASHNODE_CHAP_AUTH_EN);
iscsi_flashnode_sess_attr(fnode, discovery_logout,
			  ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN);
iscsi_flashnode_sess_attr(fnode, bidi_chap, ISCSI_FLASHNODE_BIDI_CHAP_EN);
iscsi_flashnode_sess_attr(fnode, discovery_auth_optional,
			  ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL);
iscsi_flashnode_sess_attr(fnode, erl, ISCSI_FLASHNODE_ERL);
iscsi_flashnode_sess_attr(fnode, first_burst_len, ISCSI_FLASHNODE_FIRST_BURST);
iscsi_flashnode_sess_attr(fnode, def_time2wait, ISCSI_FLASHNODE_DEF_TIME2WAIT);
iscsi_flashnode_sess_attr(fnode, def_time2retain,
			  ISCSI_FLASHNODE_DEF_TIME2RETAIN);
iscsi_flashnode_sess_attr(fnode, max_outstanding_r2t, ISCSI_FLASHNODE_MAX_R2T);
iscsi_flashnode_sess_attr(fnode, isid, ISCSI_FLASHNODE_ISID);
iscsi_flashnode_sess_attr(fnode, tsid, ISCSI_FLASHNODE_TSID);
iscsi_flashnode_sess_attr(fnode, max_burst_len, ISCSI_FLASHNODE_MAX_BURST);
iscsi_flashnode_sess_attr(fnode, def_taskmgmt_tmo,
			  ISCSI_FLASHNODE_DEF_TASKMGMT_TMO);
iscsi_flashnode_sess_attr(fnode, targetalias, ISCSI_FLASHNODE_ALIAS);
iscsi_flashnode_sess_attr(fnode, targetname, ISCSI_FLASHNODE_NAME);
iscsi_flashnode_sess_attr(fnode, tpgt, ISCSI_FLASHNODE_TPGT);
iscsi_flashnode_sess_attr(fnode, discovery_parent_idx,
			  ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX);
iscsi_flashnode_sess_attr(fnode, discovery_parent_type,
			  ISCSI_FLASHNODE_DISCOVERY_PARENT_TYPE);
iscsi_flashnode_sess_attr(fnode, chap_in_idx, ISCSI_FLASHNODE_CHAP_IN_IDX);
iscsi_flashnode_sess_attr(fnode, chap_out_idx, ISCSI_FLASHNODE_CHAP_OUT_IDX);
iscsi_flashnode_sess_attr(fnode, username, ISCSI_FLASHNODE_USERNAME);
iscsi_flashnode_sess_attr(fnode, username_in, ISCSI_FLASHNODE_USERNAME_IN);
iscsi_flashnode_sess_attr(fnode, password, ISCSI_FLASHNODE_PASSWORD);
iscsi_flashnode_sess_attr(fnode, password_in, ISCSI_FLASHNODE_PASSWORD_IN);
iscsi_flashnode_sess_attr(fnode, is_boot_target, ISCSI_FLASHNODE_IS_BOOT_TGT);

static struct attribute *iscsi_flashnode_sess_attrs[] = {
	&dev_attr_fnode_auto_snd_tgt_disable.attr,
	&dev_attr_fnode_discovery_session.attr,
	&dev_attr_fnode_portal_type.attr,
	&dev_attr_fnode_entry_enable.attr,
	&dev_attr_fnode_immediate_data.attr,
	&dev_attr_fnode_initial_r2t.attr,
	&dev_attr_fnode_data_seq_in_order.attr,
	&dev_attr_fnode_data_pdu_in_order.attr,
	&dev_attr_fnode_chap_auth.attr,
	&dev_attr_fnode_discovery_logout.attr,
	&dev_attr_fnode_bidi_chap.attr,
	&dev_attr_fnode_discovery_auth_optional.attr,
	&dev_attr_fnode_erl.attr,
	&dev_attr_fnode_first_burst_len.attr,
	&dev_attr_fnode_def_time2wait.attr,
	&dev_attr_fnode_def_time2retain.attr,
	&dev_attr_fnode_max_outstanding_r2t.attr,
	&dev_attr_fnode_isid.attr,
	&dev_attr_fnode_tsid.attr,
	&dev_attr_fnode_max_burst_len.attr,
	&dev_attr_fnode_def_taskmgmt_tmo.attr,
	&dev_attr_fnode_targetalias.attr,
	&dev_attr_fnode_targetname.attr,
	&dev_attr_fnode_tpgt.attr,
	&dev_attr_fnode_discovery_parent_idx.attr,
	&dev_attr_fnode_discovery_parent_type.attr,
	&dev_attr_fnode_chap_in_idx.attr,
	&dev_attr_fnode_chap_out_idx.attr,
	&dev_attr_fnode_username.attr,
	&dev_attr_fnode_username_in.attr,
	&dev_attr_fnode_password.attr,
	&dev_attr_fnode_password_in.attr,
	&dev_attr_fnode_is_boot_target.attr,
	NULL,
};

static umode_t iscsi_flashnode_sess_attr_is_visible(struct kobject *kobj,
						    struct attribute *attr,
						    int i)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iscsi_bus_flash_session *fnode_sess =
						iscsi_dev_to_flash_session(dev);
	struct iscsi_transport *t = fnode_sess->transport;
	int param;

	if (attr == &dev_attr_fnode_auto_snd_tgt_disable.attr) {
		param = ISCSI_FLASHNODE_AUTO_SND_TGT_DISABLE;
	} else if (attr == &dev_attr_fnode_discovery_session.attr) {
		param = ISCSI_FLASHNODE_DISCOVERY_SESS;
	} else if (attr == &dev_attr_fnode_portal_type.attr) {
		param = ISCSI_FLASHNODE_PORTAL_TYPE;
	} else if (attr == &dev_attr_fnode_entry_enable.attr) {
		param = ISCSI_FLASHNODE_ENTRY_EN;
	} else if (attr == &dev_attr_fnode_immediate_data.attr) {
		param = ISCSI_FLASHNODE_IMM_DATA_EN;
	} else if (attr == &dev_attr_fnode_initial_r2t.attr) {
		param = ISCSI_FLASHNODE_INITIAL_R2T_EN;
	} else if (attr == &dev_attr_fnode_data_seq_in_order.attr) {
		param = ISCSI_FLASHNODE_DATASEQ_INORDER;
	} else if (attr == &dev_attr_fnode_data_pdu_in_order.attr) {
		param = ISCSI_FLASHNODE_PDU_INORDER;
	} else if (attr == &dev_attr_fnode_chap_auth.attr) {
		param = ISCSI_FLASHNODE_CHAP_AUTH_EN;
	} else if (attr == &dev_attr_fnode_discovery_logout.attr) {
		param = ISCSI_FLASHNODE_DISCOVERY_LOGOUT_EN;
	} else if (attr == &dev_attr_fnode_bidi_chap.attr) {
		param = ISCSI_FLASHNODE_BIDI_CHAP_EN;
	} else if (attr == &dev_attr_fnode_discovery_auth_optional.attr) {
		param = ISCSI_FLASHNODE_DISCOVERY_AUTH_OPTIONAL;
	} else if (attr == &dev_attr_fnode_erl.attr) {
		param = ISCSI_FLASHNODE_ERL;
	} else if (attr == &dev_attr_fnode_first_burst_len.attr) {
		param = ISCSI_FLASHNODE_FIRST_BURST;
	} else if (attr == &dev_attr_fnode_def_time2wait.attr) {
		param = ISCSI_FLASHNODE_DEF_TIME2WAIT;
	} else if (attr == &dev_attr_fnode_def_time2retain.attr) {
		param = ISCSI_FLASHNODE_DEF_TIME2RETAIN;
	} else if (attr == &dev_attr_fnode_max_outstanding_r2t.attr) {
		param = ISCSI_FLASHNODE_MAX_R2T;
	} else if (attr == &dev_attr_fnode_isid.attr) {
		param = ISCSI_FLASHNODE_ISID;
	} else if (attr == &dev_attr_fnode_tsid.attr) {
		param = ISCSI_FLASHNODE_TSID;
	} else if (attr == &dev_attr_fnode_max_burst_len.attr) {
		param = ISCSI_FLASHNODE_MAX_BURST;
	} else if (attr == &dev_attr_fnode_def_taskmgmt_tmo.attr) {
		param = ISCSI_FLASHNODE_DEF_TASKMGMT_TMO;
	} else if (attr == &dev_attr_fnode_targetalias.attr) {
		param = ISCSI_FLASHNODE_ALIAS;
	} else if (attr == &dev_attr_fnode_targetname.attr) {
		param = ISCSI_FLASHNODE_NAME;
	} else if (attr == &dev_attr_fnode_tpgt.attr) {
		param = ISCSI_FLASHNODE_TPGT;
	} else if (attr == &dev_attr_fnode_discovery_parent_idx.attr) {
		param = ISCSI_FLASHNODE_DISCOVERY_PARENT_IDX;
	} else if (attr == &dev_attr_fnode_discovery_parent_type.attr) {
		param = ISCSI_FLASHNODE_DISCOVERY_PARENT_TYPE;
	} else if (attr == &dev_attr_fnode_chap_in_idx.attr) {
		param = ISCSI_FLASHNODE_CHAP_IN_IDX;
	} else if (attr == &dev_attr_fnode_chap_out_idx.attr) {
		param = ISCSI_FLASHNODE_CHAP_OUT_IDX;
	} else if (attr == &dev_attr_fnode_username.attr) {
		param = ISCSI_FLASHNODE_USERNAME;
	} else if (attr == &dev_attr_fnode_username_in.attr) {
		param = ISCSI_FLASHNODE_USERNAME_IN;
	} else if (attr == &dev_attr_fnode_password.attr) {
		param = ISCSI_FLASHNODE_PASSWORD;
	} else if (attr == &dev_attr_fnode_password_in.attr) {
		param = ISCSI_FLASHNODE_PASSWORD_IN;
	} else if (attr == &dev_attr_fnode_is_boot_target.attr) {
		param = ISCSI_FLASHNODE_IS_BOOT_TGT;
	} else {
		WARN_ONCE(1, "Invalid flashnode session attr");
		return 0;
	}

	return t->attr_is_visible(ISCSI_FLASHNODE_PARAM, param);
}

static struct attribute_group iscsi_flashnode_sess_attr_group = {
	.attrs = iscsi_flashnode_sess_attrs,
	.is_visible = iscsi_flashnode_sess_attr_is_visible,
};

static const struct attribute_group *iscsi_flashnode_sess_attr_groups[] = {
	&iscsi_flashnode_sess_attr_group,
	NULL,
};

static void iscsi_flashnode_sess_release(struct device *dev)
{
	struct iscsi_bus_flash_session *fnode_sess =
						iscsi_dev_to_flash_session(dev);

	kfree(fnode_sess->targetname);
	kfree(fnode_sess->targetalias);
	kfree(fnode_sess->portal_type);
	kfree(fnode_sess);
}

static const struct device_type iscsi_flashnode_sess_dev_type = {
	.name = "iscsi_flashnode_sess_dev_type",
	.groups = iscsi_flashnode_sess_attr_groups,
	.release = iscsi_flashnode_sess_release,
};

/* flash node connection attrs show */
#define iscsi_flashnode_conn_attr_show(type, name, param)		\
static ssize_t								\
show_##type##_##name(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct iscsi_bus_flash_conn *fnode_conn = iscsi_dev_to_flash_conn(dev);\
	struct iscsi_bus_flash_session *fnode_sess =			\
				iscsi_flash_conn_to_flash_session(fnode_conn);\
	struct iscsi_transport *t = fnode_conn->transport;		\
	return t->get_flashnode_param(fnode_sess, param, buf);		\
}									\


#define iscsi_flashnode_conn_attr(type, name, param)			\
	iscsi_flashnode_conn_attr_show(type, name, param)		\
static ISCSI_FLASHNODE_ATTR(type, name, S_IRUGO,			\
			    show_##type##_##name, NULL);

/* Flash node connection attributes */

iscsi_flashnode_conn_attr(fnode, is_fw_assigned_ipv6,
			  ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6);
iscsi_flashnode_conn_attr(fnode, header_digest, ISCSI_FLASHNODE_HDR_DGST_EN);
iscsi_flashnode_conn_attr(fnode, data_digest, ISCSI_FLASHNODE_DATA_DGST_EN);
iscsi_flashnode_conn_attr(fnode, snack_req, ISCSI_FLASHNODE_SNACK_REQ_EN);
iscsi_flashnode_conn_attr(fnode, tcp_timestamp_stat,
			  ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT);
iscsi_flashnode_conn_attr(fnode, tcp_nagle_disable,
			  ISCSI_FLASHNODE_TCP_NAGLE_DISABLE);
iscsi_flashnode_conn_attr(fnode, tcp_wsf_disable,
			  ISCSI_FLASHNODE_TCP_WSF_DISABLE);
iscsi_flashnode_conn_attr(fnode, tcp_timer_scale,
			  ISCSI_FLASHNODE_TCP_TIMER_SCALE);
iscsi_flashnode_conn_attr(fnode, tcp_timestamp_enable,
			  ISCSI_FLASHNODE_TCP_TIMESTAMP_EN);
iscsi_flashnode_conn_attr(fnode, fragment_disable,
			  ISCSI_FLASHNODE_IP_FRAG_DISABLE);
iscsi_flashnode_conn_attr(fnode, keepalive_tmo, ISCSI_FLASHNODE_KEEPALIVE_TMO);
iscsi_flashnode_conn_attr(fnode, port, ISCSI_FLASHNODE_PORT);
iscsi_flashnode_conn_attr(fnode, ipaddress, ISCSI_FLASHNODE_IPADDR);
iscsi_flashnode_conn_attr(fnode, max_recv_dlength,
			  ISCSI_FLASHNODE_MAX_RECV_DLENGTH);
iscsi_flashnode_conn_attr(fnode, max_xmit_dlength,
			  ISCSI_FLASHNODE_MAX_XMIT_DLENGTH);
iscsi_flashnode_conn_attr(fnode, local_port, ISCSI_FLASHNODE_LOCAL_PORT);
iscsi_flashnode_conn_attr(fnode, ipv4_tos, ISCSI_FLASHNODE_IPV4_TOS);
iscsi_flashnode_conn_attr(fnode, ipv6_traffic_class, ISCSI_FLASHNODE_IPV6_TC);
iscsi_flashnode_conn_attr(fnode, ipv6_flow_label,
			  ISCSI_FLASHNODE_IPV6_FLOW_LABEL);
iscsi_flashnode_conn_attr(fnode, redirect_ipaddr,
			  ISCSI_FLASHNODE_REDIRECT_IPADDR);
iscsi_flashnode_conn_attr(fnode, max_segment_size,
			  ISCSI_FLASHNODE_MAX_SEGMENT_SIZE);
iscsi_flashnode_conn_attr(fnode, link_local_ipv6,
			  ISCSI_FLASHNODE_LINK_LOCAL_IPV6);
iscsi_flashnode_conn_attr(fnode, tcp_xmit_wsf, ISCSI_FLASHNODE_TCP_XMIT_WSF);
iscsi_flashnode_conn_attr(fnode, tcp_recv_wsf, ISCSI_FLASHNODE_TCP_RECV_WSF);
iscsi_flashnode_conn_attr(fnode, statsn, ISCSI_FLASHNODE_STATSN);
iscsi_flashnode_conn_attr(fnode, exp_statsn, ISCSI_FLASHNODE_EXP_STATSN);

static struct attribute *iscsi_flashnode_conn_attrs[] = {
	&dev_attr_fnode_is_fw_assigned_ipv6.attr,
	&dev_attr_fnode_header_digest.attr,
	&dev_attr_fnode_data_digest.attr,
	&dev_attr_fnode_snack_req.attr,
	&dev_attr_fnode_tcp_timestamp_stat.attr,
	&dev_attr_fnode_tcp_nagle_disable.attr,
	&dev_attr_fnode_tcp_wsf_disable.attr,
	&dev_attr_fnode_tcp_timer_scale.attr,
	&dev_attr_fnode_tcp_timestamp_enable.attr,
	&dev_attr_fnode_fragment_disable.attr,
	&dev_attr_fnode_max_recv_dlength.attr,
	&dev_attr_fnode_max_xmit_dlength.attr,
	&dev_attr_fnode_keepalive_tmo.attr,
	&dev_attr_fnode_port.attr,
	&dev_attr_fnode_ipaddress.attr,
	&dev_attr_fnode_redirect_ipaddr.attr,
	&dev_attr_fnode_max_segment_size.attr,
	&dev_attr_fnode_local_port.attr,
	&dev_attr_fnode_ipv4_tos.attr,
	&dev_attr_fnode_ipv6_traffic_class.attr,
	&dev_attr_fnode_ipv6_flow_label.attr,
	&dev_attr_fnode_link_local_ipv6.attr,
	&dev_attr_fnode_tcp_xmit_wsf.attr,
	&dev_attr_fnode_tcp_recv_wsf.attr,
	&dev_attr_fnode_statsn.attr,
	&dev_attr_fnode_exp_statsn.attr,
	NULL,
};

static umode_t iscsi_flashnode_conn_attr_is_visible(struct kobject *kobj,
						    struct attribute *attr,
						    int i)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iscsi_bus_flash_conn *fnode_conn = iscsi_dev_to_flash_conn(dev);
	struct iscsi_transport *t = fnode_conn->transport;
	int param;

	if (attr == &dev_attr_fnode_is_fw_assigned_ipv6.attr) {
		param = ISCSI_FLASHNODE_IS_FW_ASSIGNED_IPV6;
	} else if (attr == &dev_attr_fnode_header_digest.attr) {
		param = ISCSI_FLASHNODE_HDR_DGST_EN;
	} else if (attr == &dev_attr_fnode_data_digest.attr) {
		param = ISCSI_FLASHNODE_DATA_DGST_EN;
	} else if (attr == &dev_attr_fnode_snack_req.attr) {
		param = ISCSI_FLASHNODE_SNACK_REQ_EN;
	} else if (attr == &dev_attr_fnode_tcp_timestamp_stat.attr) {
		param = ISCSI_FLASHNODE_TCP_TIMESTAMP_STAT;
	} else if (attr == &dev_attr_fnode_tcp_nagle_disable.attr) {
		param = ISCSI_FLASHNODE_TCP_NAGLE_DISABLE;
	} else if (attr == &dev_attr_fnode_tcp_wsf_disable.attr) {
		param = ISCSI_FLASHNODE_TCP_WSF_DISABLE;
	} else if (attr == &dev_attr_fnode_tcp_timer_scale.attr) {
		param = ISCSI_FLASHNODE_TCP_TIMER_SCALE;
	} else if (attr == &dev_attr_fnode_tcp_timestamp_enable.attr) {
		param = ISCSI_FLASHNODE_TCP_TIMESTAMP_EN;
	} else if (attr == &dev_attr_fnode_fragment_disable.attr) {
		param = ISCSI_FLASHNODE_IP_FRAG_DISABLE;
	} else if (attr == &dev_attr_fnode_max_recv_dlength.attr) {
		param = ISCSI_FLASHNODE_MAX_RECV_DLENGTH;
	} else if (attr == &dev_attr_fnode_max_xmit_dlength.attr) {
		param = ISCSI_FLASHNODE_MAX_XMIT_DLENGTH;
	} else if (attr == &dev_attr_fnode_keepalive_tmo.attr) {
		param = ISCSI_FLASHNODE_KEEPALIVE_TMO;
	} else if (attr == &dev_attr_fnode_port.attr) {
		param = ISCSI_FLASHNODE_PORT;
	} else if (attr == &dev_attr_fnode_ipaddress.attr) {
		param = ISCSI_FLASHNODE_IPADDR;
	} else if (attr == &dev_attr_fnode_redirect_ipaddr.attr) {
		param = ISCSI_FLASHNODE_REDIRECT_IPADDR;
	} else if (attr == &dev_attr_fnode_max_segment_size.attr) {
		param = ISCSI_FLASHNODE_MAX_SEGMENT_SIZE;
	} else if (attr == &dev_attr_fnode_local_port.attr) {
		param = ISCSI_FLASHNODE_LOCAL_PORT;
	} else if (attr == &dev_attr_fnode_ipv4_tos.attr) {
		param = ISCSI_FLASHNODE_IPV4_TOS;
	} else if (attr == &dev_attr_fnode_ipv6_traffic_class.attr) {
		param = ISCSI_FLASHNODE_IPV6_TC;
	} else if (attr == &dev_attr_fnode_ipv6_flow_label.attr) {
		param = ISCSI_FLASHNODE_IPV6_FLOW_LABEL;
	} else if (attr == &dev_attr_fnode_link_local_ipv6.attr) {
		param = ISCSI_FLASHNODE_LINK_LOCAL_IPV6;
	} else if (attr == &dev_attr_fnode_tcp_xmit_wsf.attr) {
		param = ISCSI_FLASHNODE_TCP_XMIT_WSF;
	} else if (attr == &dev_attr_fnode_tcp_recv_wsf.attr) {
		param = ISCSI_FLASHNODE_TCP_RECV_WSF;
	} else if (attr == &dev_attr_fnode_statsn.attr) {
		param = ISCSI_FLASHNODE_STATSN;
	} else if (attr == &dev_attr_fnode_exp_statsn.attr) {
		param = ISCSI_FLASHNODE_EXP_STATSN;
	} else {
		WARN_ONCE(1, "Invalid flashnode connection attr");
		return 0;
	}

	return t->attr_is_visible(ISCSI_FLASHNODE_PARAM, param);
}

static struct attribute_group iscsi_flashnode_conn_attr_group = {
	.attrs = iscsi_flashnode_conn_attrs,
	.is_visible = iscsi_flashnode_conn_attr_is_visible,
};

static const struct attribute_group *iscsi_flashnode_conn_attr_groups[] = {
	&iscsi_flashnode_conn_attr_group,
	NULL,
};

static void iscsi_flashnode_conn_release(struct device *dev)
{
	struct iscsi_bus_flash_conn *fnode_conn = iscsi_dev_to_flash_conn(dev);

	kfree(fnode_conn->ipaddress);
	kfree(fnode_conn->redirect_ipaddr);
	kfree(fnode_conn->link_local_ipv6_addr);
	kfree(fnode_conn);
}

static const struct device_type iscsi_flashnode_conn_dev_type = {
	.name = "iscsi_flashnode_conn_dev_type",
	.groups = iscsi_flashnode_conn_attr_groups,
	.release = iscsi_flashnode_conn_release,
};

static const struct bus_type iscsi_flashnode_bus;

int iscsi_flashnode_bus_match(struct device *dev,
			      const struct device_driver *drv)
{
	if (dev->bus == &iscsi_flashnode_bus)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_flashnode_bus_match);

static const struct bus_type iscsi_flashnode_bus = {
	.name = "iscsi_flashnode",
	.match = &iscsi_flashnode_bus_match,
};

/**
 * iscsi_create_flashnode_sess - Add flashnode session entry in sysfs
 * @shost: pointer to host data
 * @index: index of flashnode to add in sysfs
 * @transport: pointer to transport data
 * @dd_size: total size to allocate
 *
 * Adds a sysfs entry for the flashnode session attributes
 *
 * Returns:
 *  pointer to allocated flashnode sess on success
 *  %NULL on failure
 */
struct iscsi_bus_flash_session *
iscsi_create_flashnode_sess(struct Scsi_Host *shost, int index,
			    struct iscsi_transport *transport,
			    int dd_size)
{
	struct iscsi_bus_flash_session *fnode_sess;
	int err;

	fnode_sess = kzalloc(sizeof(*fnode_sess) + dd_size, GFP_KERNEL);
	if (!fnode_sess)
		return NULL;

	fnode_sess->transport = transport;
	fnode_sess->target_id = index;
	fnode_sess->dev.type = &iscsi_flashnode_sess_dev_type;
	fnode_sess->dev.bus = &iscsi_flashnode_bus;
	fnode_sess->dev.parent = &shost->shost_gendev;
	dev_set_name(&fnode_sess->dev, "flashnode_sess-%u:%u",
		     shost->host_no, index);

	err = device_register(&fnode_sess->dev);
	if (err)
		goto put_dev;

	if (dd_size)
		fnode_sess->dd_data = &fnode_sess[1];

	return fnode_sess;

put_dev:
	put_device(&fnode_sess->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_create_flashnode_sess);

/**
 * iscsi_create_flashnode_conn - Add flashnode conn entry in sysfs
 * @shost: pointer to host data
 * @fnode_sess: pointer to the parent flashnode session entry
 * @transport: pointer to transport data
 * @dd_size: total size to allocate
 *
 * Adds a sysfs entry for the flashnode connection attributes
 *
 * Returns:
 *  pointer to allocated flashnode conn on success
 *  %NULL on failure
 */
struct iscsi_bus_flash_conn *
iscsi_create_flashnode_conn(struct Scsi_Host *shost,
			    struct iscsi_bus_flash_session *fnode_sess,
			    struct iscsi_transport *transport,
			    int dd_size)
{
	struct iscsi_bus_flash_conn *fnode_conn;
	int err;

	fnode_conn = kzalloc(sizeof(*fnode_conn) + dd_size, GFP_KERNEL);
	if (!fnode_conn)
		return NULL;

	fnode_conn->transport = transport;
	fnode_conn->dev.type = &iscsi_flashnode_conn_dev_type;
	fnode_conn->dev.bus = &iscsi_flashnode_bus;
	fnode_conn->dev.parent = &fnode_sess->dev;
	dev_set_name(&fnode_conn->dev, "flashnode_conn-%u:%u:0",
		     shost->host_no, fnode_sess->target_id);

	err = device_register(&fnode_conn->dev);
	if (err)
		goto put_dev;

	if (dd_size)
		fnode_conn->dd_data = &fnode_conn[1];

	return fnode_conn;

put_dev:
	put_device(&fnode_conn->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_create_flashnode_conn);

/**
 * iscsi_is_flashnode_conn_dev - verify passed device is to be flashnode conn
 * @dev: device to verify
 * @data: pointer to data containing value to use for verification
 *
 * Verifies if the passed device is flashnode conn device
 *
 * Returns:
 *  1 on success
 *  0 on failure
 */
static int iscsi_is_flashnode_conn_dev(struct device *dev, const void *data)
{
	return dev->bus == &iscsi_flashnode_bus;
}

static int iscsi_destroy_flashnode_conn(struct iscsi_bus_flash_conn *fnode_conn)
{
	device_unregister(&fnode_conn->dev);
	return 0;
}

static int flashnode_match_index(struct device *dev, const void *data)
{
	struct iscsi_bus_flash_session *fnode_sess = NULL;
	int ret = 0;

	if (!iscsi_flashnode_bus_match(dev, NULL))
		goto exit_match_index;

	fnode_sess = iscsi_dev_to_flash_session(dev);
	ret = (fnode_sess->target_id == *((const int *)data)) ? 1 : 0;

exit_match_index:
	return ret;
}

/**
 * iscsi_get_flashnode_by_index -finds flashnode session entry by index
 * @shost: pointer to host data
 * @idx: index to match
 *
 * Finds the flashnode session object for the passed index
 *
 * Returns:
 *  pointer to found flashnode session object on success
 *  %NULL on failure
 */
static struct iscsi_bus_flash_session *
iscsi_get_flashnode_by_index(struct Scsi_Host *shost, uint32_t idx)
{
	struct iscsi_bus_flash_session *fnode_sess = NULL;
	struct device *dev;

	dev = device_find_child(&shost->shost_gendev, &idx,
				flashnode_match_index);
	if (dev)
		fnode_sess = iscsi_dev_to_flash_session(dev);

	return fnode_sess;
}

/**
 * iscsi_find_flashnode_sess - finds flashnode session entry
 * @shost: pointer to host data
 * @data: pointer to data containing value to use for comparison
 * @fn: function pointer that does actual comparison
 *
 * Finds the flashnode session object comparing the data passed using logic
 * defined in passed function pointer
 *
 * Returns:
 *  pointer to found flashnode session device object on success
 *  %NULL on failure
 */
struct device *
iscsi_find_flashnode_sess(struct Scsi_Host *shost, const void *data,
			  device_match_t fn)
{
	return device_find_child(&shost->shost_gendev, data, fn);
}
EXPORT_SYMBOL_GPL(iscsi_find_flashnode_sess);

/**
 * iscsi_find_flashnode_conn - finds flashnode connection entry
 * @fnode_sess: pointer to parent flashnode session entry
 *
 * Finds the flashnode connection object comparing the data passed using logic
 * defined in passed function pointer
 *
 * Returns:
 *  pointer to found flashnode connection device object on success
 *  %NULL on failure
 */
struct device *
iscsi_find_flashnode_conn(struct iscsi_bus_flash_session *fnode_sess)
{
	return device_find_child(&fnode_sess->dev, NULL,
				 iscsi_is_flashnode_conn_dev);
}
EXPORT_SYMBOL_GPL(iscsi_find_flashnode_conn);

static int iscsi_iter_destroy_flashnode_conn_fn(struct device *dev, void *data)
{
	if (!iscsi_is_flashnode_conn_dev(dev, NULL))
		return 0;

	return iscsi_destroy_flashnode_conn(iscsi_dev_to_flash_conn(dev));
}

/**
 * iscsi_destroy_flashnode_sess - destroy flashnode session entry
 * @fnode_sess: pointer to flashnode session entry to be destroyed
 *
 * Deletes the flashnode session entry and all children flashnode connection
 * entries from sysfs
 */
void iscsi_destroy_flashnode_sess(struct iscsi_bus_flash_session *fnode_sess)
{
	int err;

	err = device_for_each_child(&fnode_sess->dev, NULL,
				    iscsi_iter_destroy_flashnode_conn_fn);
	if (err)
		pr_err("Could not delete all connections for %s. Error %d.\n",
		       fnode_sess->dev.kobj.name, err);

	device_unregister(&fnode_sess->dev);
}
EXPORT_SYMBOL_GPL(iscsi_destroy_flashnode_sess);

static int iscsi_iter_destroy_flashnode_fn(struct device *dev, void *data)
{
	if (!iscsi_flashnode_bus_match(dev, NULL))
		return 0;

	iscsi_destroy_flashnode_sess(iscsi_dev_to_flash_session(dev));
	return 0;
}

/**
 * iscsi_destroy_all_flashnode - destroy all flashnode session entries
 * @shost: pointer to host data
 *
 * Destroys all the flashnode session entries and all corresponding children
 * flashnode connection entries from sysfs
 */
void iscsi_destroy_all_flashnode(struct Scsi_Host *shost)
{
	device_for_each_child(&shost->shost_gendev, NULL,
			      iscsi_iter_destroy_flashnode_fn);
}
EXPORT_SYMBOL_GPL(iscsi_destroy_all_flashnode);

/*
 * BSG support
 */
/**
 * iscsi_bsg_host_dispatch - Dispatch command to LLD.
 * @job: bsg job to be processed
 */
static int iscsi_bsg_host_dispatch(struct bsg_job *job)
{
	struct Scsi_Host *shost = iscsi_job_to_shost(job);
	struct iscsi_bsg_request *req = job->request;
	struct iscsi_bsg_reply *reply = job->reply;
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt);
	int cmdlen = sizeof(uint32_t);	/* start with length of msgcode */
	int ret;

	/* check if we have the msgcode value at least */
	if (job->request_len < sizeof(uint32_t)) {
		ret = -ENOMSG;
		goto fail_host_msg;
	}

	/* Validate the host command */
	switch (req->msgcode) {
	case ISCSI_BSG_HST_VENDOR:
		cmdlen += sizeof(struct iscsi_bsg_host_vendor);
		if ((shost->hostt->vendor_id == 0L) ||
		    (req->rqst_data.h_vendor.vendor_id !=
			shost->hostt->vendor_id)) {
			ret = -ESRCH;
			goto fail_host_msg;
		}
		break;
	default:
		ret = -EBADR;
		goto fail_host_msg;
	}

	/* check if we really have all the request data needed */
	if (job->request_len < cmdlen) {
		ret = -ENOMSG;
		goto fail_host_msg;
	}

	ret = i->iscsi_transport->bsg_request(job);
	if (!ret)
		return 0;

fail_host_msg:
	/* return the errno failure code as the only status */
	BUG_ON(job->reply_len < sizeof(uint32_t));
	reply->reply_payload_rcv_len = 0;
	reply->result = ret;
	job->reply_len = sizeof(uint32_t);
	bsg_job_done(job, ret, 0);
	return 0;
}

/**
 * iscsi_bsg_host_add - Create and add the bsg hooks to receive requests
 * @shost: shost for iscsi_host
 * @ihost: iscsi_cls_host adding the structures to
 */
static int
iscsi_bsg_host_add(struct Scsi_Host *shost, struct iscsi_cls_host *ihost)
{
	struct device *dev = &shost->shost_gendev;
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt);
	struct queue_limits lim;
	struct request_queue *q;
	char bsg_name[20];

	if (!i->iscsi_transport->bsg_request)
		return -ENOTSUPP;

	snprintf(bsg_name, sizeof(bsg_name), "iscsi_host%d", shost->host_no);
	scsi_init_limits(shost, &lim);
	q = bsg_setup_queue(dev, bsg_name, &lim, iscsi_bsg_host_dispatch, NULL,
			0);
	if (IS_ERR(q)) {
		shost_printk(KERN_ERR, shost, "bsg interface failed to "
			     "initialize - no request queue\n");
		return PTR_ERR(q);
	}

	ihost->bsg_q = q;
	return 0;
}

static int iscsi_setup_host(struct transport_container *tc, struct device *dev,
			    struct device *cdev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct iscsi_cls_host *ihost = shost->shost_data;

	memset(ihost, 0, sizeof(*ihost));
	mutex_init(&ihost->mutex);

	iscsi_bsg_host_add(shost, ihost);
	/* ignore any bsg add error - we just can't do sgio */

	return 0;
}

static int iscsi_remove_host(struct transport_container *tc,
			     struct device *dev, struct device *cdev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct iscsi_cls_host *ihost = shost->shost_data;

	bsg_remove_queue(ihost->bsg_q);
	return 0;
}

static DECLARE_TRANSPORT_CLASS(iscsi_host_class,
			       "iscsi_host",
			       iscsi_setup_host,
			       iscsi_remove_host,
			       NULL);

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
static DEFINE_MUTEX(rx_queue_mutex);

static LIST_HEAD(sesslist);
static DEFINE_SPINLOCK(sesslock);
static LIST_HEAD(connlist);
static DEFINE_SPINLOCK(connlock);

static uint32_t iscsi_conn_get_sid(struct iscsi_cls_conn *conn)
{
	struct iscsi_cls_session *sess = iscsi_dev_to_session(conn->dev.parent);
	return sess->sid;
}

/*
 * Returns the matching session to a given sid
 */
static struct iscsi_cls_session *iscsi_session_lookup(uint32_t sid)
{
	unsigned long flags;
	struct iscsi_cls_session *sess;

	spin_lock_irqsave(&sesslock, flags);
	list_for_each_entry(sess, &sesslist, sess_list) {
		if (sess->sid == sid) {
			spin_unlock_irqrestore(&sesslock, flags);
			return sess;
		}
	}
	spin_unlock_irqrestore(&sesslock, flags);
	return NULL;
}

/*
 * Returns the matching connection to a given sid / cid tuple
 */
static struct iscsi_cls_conn *iscsi_conn_lookup(uint32_t sid, uint32_t cid)
{
	unsigned long flags;
	struct iscsi_cls_conn *conn;

	spin_lock_irqsave(&connlock, flags);
	list_for_each_entry(conn, &connlist, conn_list) {
		if ((conn->cid == cid) && (iscsi_conn_get_sid(conn) == sid)) {
			spin_unlock_irqrestore(&connlock, flags);
			return conn;
		}
	}
	spin_unlock_irqrestore(&connlock, flags);
	return NULL;
}

/*
 * The following functions can be used by LLDs that allocate
 * their own scsi_hosts or by software iscsi LLDs
 */
static struct {
	int value;
	char *name;
} iscsi_session_state_names[] = {
	{ ISCSI_SESSION_LOGGED_IN,	"LOGGED_IN" },
	{ ISCSI_SESSION_FAILED,		"FAILED" },
	{ ISCSI_SESSION_FREE,		"FREE" },
};

static const char *iscsi_session_state_name(int state)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(iscsi_session_state_names); i++) {
		if (iscsi_session_state_names[i].value == state) {
			name = iscsi_session_state_names[i].name;
			break;
		}
	}
	return name;
}

static char *iscsi_session_target_state_name[] = {
	[ISCSI_SESSION_TARGET_UNBOUND]   = "UNBOUND",
	[ISCSI_SESSION_TARGET_ALLOCATED] = "ALLOCATED",
	[ISCSI_SESSION_TARGET_SCANNED]   = "SCANNED",
	[ISCSI_SESSION_TARGET_UNBINDING] = "UNBINDING",
};

int iscsi_session_chkready(struct iscsi_cls_session *session)
{
	int err;

	switch (session->state) {
	case ISCSI_SESSION_LOGGED_IN:
		err = 0;
		break;
	case ISCSI_SESSION_FAILED:
		err = DID_IMM_RETRY << 16;
		break;
	case ISCSI_SESSION_FREE:
		err = DID_TRANSPORT_FAILFAST << 16;
		break;
	default:
		err = DID_NO_CONNECT << 16;
		break;
	}
	return err;
}
EXPORT_SYMBOL_GPL(iscsi_session_chkready);

int iscsi_is_session_online(struct iscsi_cls_session *session)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&session->lock, flags);
	if (session->state == ISCSI_SESSION_LOGGED_IN)
		ret = 1;
	spin_unlock_irqrestore(&session->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(iscsi_is_session_online);

static void iscsi_session_release(struct device *dev)
{
	struct iscsi_cls_session *session = iscsi_dev_to_session(dev);
	struct Scsi_Host *shost;

	shost = iscsi_session_to_shost(session);
	scsi_host_put(shost);
	ISCSI_DBG_TRANS_SESSION(session, "Completing session release\n");
	kfree(session);
}

int iscsi_is_session_dev(const struct device *dev)
{
	return dev->release == iscsi_session_release;
}
EXPORT_SYMBOL_GPL(iscsi_is_session_dev);

static int iscsi_iter_session_fn(struct device *dev, void *data)
{
	void (* fn) (struct iscsi_cls_session *) = data;

	if (!iscsi_is_session_dev(dev))
		return 0;
	fn(iscsi_dev_to_session(dev));
	return 0;
}

void iscsi_host_for_each_session(struct Scsi_Host *shost,
				 void (*fn)(struct iscsi_cls_session *))
{
	device_for_each_child(&shost->shost_gendev, fn,
			      iscsi_iter_session_fn);
}
EXPORT_SYMBOL_GPL(iscsi_host_for_each_session);

struct iscsi_scan_data {
	unsigned int channel;
	unsigned int id;
	u64 lun;
	enum scsi_scan_mode rescan;
};

static int iscsi_user_scan_session(struct device *dev, void *data)
{
	struct iscsi_scan_data *scan_data = data;
	struct iscsi_cls_session *session;
	struct Scsi_Host *shost;
	struct iscsi_cls_host *ihost;
	unsigned long flags;
	unsigned int id;

	if (!iscsi_is_session_dev(dev))
		return 0;

	session = iscsi_dev_to_session(dev);

	ISCSI_DBG_TRANS_SESSION(session, "Scanning session\n");

	shost = iscsi_session_to_shost(session);
	ihost = shost->shost_data;

	mutex_lock(&ihost->mutex);
	spin_lock_irqsave(&session->lock, flags);
	if (session->state != ISCSI_SESSION_LOGGED_IN) {
		spin_unlock_irqrestore(&session->lock, flags);
		goto user_scan_exit;
	}
	id = session->target_id;
	spin_unlock_irqrestore(&session->lock, flags);

	if (id != ISCSI_MAX_TARGET) {
		if ((scan_data->channel == SCAN_WILD_CARD ||
		     scan_data->channel == 0) &&
		    (scan_data->id == SCAN_WILD_CARD ||
		     scan_data->id == id)) {
			scsi_scan_target(&session->dev, 0, id,
					 scan_data->lun, scan_data->rescan);
			spin_lock_irqsave(&session->lock, flags);
			session->target_state = ISCSI_SESSION_TARGET_SCANNED;
			spin_unlock_irqrestore(&session->lock, flags);
		}
	}

user_scan_exit:
	mutex_unlock(&ihost->mutex);
	ISCSI_DBG_TRANS_SESSION(session, "Completed session scan\n");
	return 0;
}

static int iscsi_user_scan(struct Scsi_Host *shost, uint channel,
			   uint id, u64 lun)
{
	struct iscsi_scan_data scan_data;

	scan_data.channel = channel;
	scan_data.id = id;
	scan_data.lun = lun;
	scan_data.rescan = SCSI_SCAN_MANUAL;

	return device_for_each_child(&shost->shost_gendev, &scan_data,
				     iscsi_user_scan_session);
}

static void iscsi_scan_session(struct work_struct *work)
{
	struct iscsi_cls_session *session =
			container_of(work, struct iscsi_cls_session, scan_work);
	struct iscsi_scan_data scan_data;

	scan_data.channel = 0;
	scan_data.id = SCAN_WILD_CARD;
	scan_data.lun = SCAN_WILD_CARD;
	scan_data.rescan = SCSI_SCAN_RESCAN;

	iscsi_user_scan_session(&session->dev, &scan_data);
}

/**
 * iscsi_block_scsi_eh - block scsi eh until session state has transistioned
 * @cmd: scsi cmd passed to scsi eh handler
 *
 * If the session is down this function will wait for the recovery
 * timer to fire or for the session to be logged back in. If the
 * recovery timer fires then FAST_IO_FAIL is returned. The caller
 * should pass this error value to the scsi eh.
 */
int iscsi_block_scsi_eh(struct scsi_cmnd *cmd)
{
	struct iscsi_cls_session *session =
			starget_to_session(scsi_target(cmd->device));
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&session->lock, flags);
	while (session->state != ISCSI_SESSION_LOGGED_IN) {
		if (session->state == ISCSI_SESSION_FREE) {
			ret = FAST_IO_FAIL;
			break;
		}
		spin_unlock_irqrestore(&session->lock, flags);
		msleep(1000);
		spin_lock_irqsave(&session->lock, flags);
	}
	spin_unlock_irqrestore(&session->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(iscsi_block_scsi_eh);

static void session_recovery_timedout(struct work_struct *work)
{
	struct iscsi_cls_session *session =
		container_of(work, struct iscsi_cls_session,
			     recovery_work.work);
	unsigned long flags;

	iscsi_cls_session_printk(KERN_INFO, session,
				 "session recovery timed out after %d secs\n",
				 session->recovery_tmo);

	spin_lock_irqsave(&session->lock, flags);
	switch (session->state) {
	case ISCSI_SESSION_FAILED:
		session->state = ISCSI_SESSION_FREE;
		break;
	case ISCSI_SESSION_LOGGED_IN:
	case ISCSI_SESSION_FREE:
		/* we raced with the unblock's flush */
		spin_unlock_irqrestore(&session->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&session->lock, flags);

	ISCSI_DBG_TRANS_SESSION(session, "Unblocking SCSI target\n");
	scsi_target_unblock(&session->dev, SDEV_TRANSPORT_OFFLINE);
	ISCSI_DBG_TRANS_SESSION(session, "Completed unblocking SCSI target\n");

	if (session->transport->session_recovery_timedout)
		session->transport->session_recovery_timedout(session);
}

static void __iscsi_unblock_session(struct work_struct *work)
{
	struct iscsi_cls_session *session =
			container_of(work, struct iscsi_cls_session,
				     unblock_work);
	unsigned long flags;

	ISCSI_DBG_TRANS_SESSION(session, "Unblocking session\n");

	cancel_delayed_work_sync(&session->recovery_work);
	spin_lock_irqsave(&session->lock, flags);
	session->state = ISCSI_SESSION_LOGGED_IN;
	spin_unlock_irqrestore(&session->lock, flags);
	/* start IO */
	scsi_target_unblock(&session->dev, SDEV_RUNNING);
	ISCSI_DBG_TRANS_SESSION(session, "Completed unblocking session\n");
}

/**
 * iscsi_unblock_session - set a session as logged in and start IO.
 * @session: iscsi session
 *
 * Mark a session as ready to accept IO.
 */
void iscsi_unblock_session(struct iscsi_cls_session *session)
{
	if (!cancel_work_sync(&session->block_work))
		cancel_delayed_work_sync(&session->recovery_work);

	queue_work(session->workq, &session->unblock_work);
	/*
	 * Blocking the session can be done from any context so we only
	 * queue the block work. Make sure the unblock work has completed
	 * because it flushes/cancels the other works and updates the state.
	 */
	flush_work(&session->unblock_work);
}
EXPORT_SYMBOL_GPL(iscsi_unblock_session);

static void __iscsi_block_session(struct work_struct *work)
{
	struct iscsi_cls_session *session =
			container_of(work, struct iscsi_cls_session,
				     block_work);
	struct Scsi_Host *shost = iscsi_session_to_shost(session);
	unsigned long flags;

	ISCSI_DBG_TRANS_SESSION(session, "Blocking session\n");
	spin_lock_irqsave(&session->lock, flags);
	session->state = ISCSI_SESSION_FAILED;
	spin_unlock_irqrestore(&session->lock, flags);
	scsi_block_targets(shost, &session->dev);
	ISCSI_DBG_TRANS_SESSION(session, "Completed SCSI target blocking\n");
	if (session->recovery_tmo >= 0)
		queue_delayed_work(session->workq,
				   &session->recovery_work,
				   session->recovery_tmo * HZ);
}

void iscsi_block_session(struct iscsi_cls_session *session)
{
	queue_work(session->workq, &session->block_work);
}
EXPORT_SYMBOL_GPL(iscsi_block_session);

static void __iscsi_unbind_session(struct work_struct *work)
{
	struct iscsi_cls_session *session =
			container_of(work, struct iscsi_cls_session,
				     unbind_work);
	struct Scsi_Host *shost = iscsi_session_to_shost(session);
	struct iscsi_cls_host *ihost = shost->shost_data;
	unsigned long flags;
	unsigned int target_id;
	bool remove_target = true;

	ISCSI_DBG_TRANS_SESSION(session, "Unbinding session\n");

	/* Prevent new scans and make sure scanning is not in progress */
	mutex_lock(&ihost->mutex);
	spin_lock_irqsave(&session->lock, flags);
	if (session->target_state == ISCSI_SESSION_TARGET_ALLOCATED) {
		remove_target = false;
	} else if (session->target_state != ISCSI_SESSION_TARGET_SCANNED) {
		spin_unlock_irqrestore(&session->lock, flags);
		mutex_unlock(&ihost->mutex);
		ISCSI_DBG_TRANS_SESSION(session,
			"Skipping target unbinding: Session is unbound/unbinding.\n");
		return;
	}

	session->target_state = ISCSI_SESSION_TARGET_UNBINDING;
	target_id = session->target_id;
	session->target_id = ISCSI_MAX_TARGET;
	spin_unlock_irqrestore(&session->lock, flags);
	mutex_unlock(&ihost->mutex);

	if (remove_target)
		scsi_remove_target(&session->dev);

	if (session->ida_used)
		ida_free(&iscsi_sess_ida, target_id);

	iscsi_session_event(session, ISCSI_KEVENT_UNBIND_SESSION);
	ISCSI_DBG_TRANS_SESSION(session, "Completed target removal\n");

	spin_lock_irqsave(&session->lock, flags);
	session->target_state = ISCSI_SESSION_TARGET_UNBOUND;
	spin_unlock_irqrestore(&session->lock, flags);
}

static void __iscsi_destroy_session(struct work_struct *work)
{
	struct iscsi_cls_session *session =
		container_of(work, struct iscsi_cls_session, destroy_work);

	session->transport->destroy_session(session);
}

struct iscsi_cls_session *
iscsi_alloc_session(struct Scsi_Host *shost, struct iscsi_transport *transport,
		    int dd_size)
{
	struct iscsi_cls_session *session;

	session = kzalloc(sizeof(*session) + dd_size,
			  GFP_KERNEL);
	if (!session)
		return NULL;

	session->transport = transport;
	session->creator = -1;
	session->recovery_tmo = 120;
	session->recovery_tmo_sysfs_override = false;
	session->state = ISCSI_SESSION_FREE;
	INIT_DELAYED_WORK(&session->recovery_work, session_recovery_timedout);
	INIT_LIST_HEAD(&session->sess_list);
	INIT_WORK(&session->unblock_work, __iscsi_unblock_session);
	INIT_WORK(&session->block_work, __iscsi_block_session);
	INIT_WORK(&session->unbind_work, __iscsi_unbind_session);
	INIT_WORK(&session->scan_work, iscsi_scan_session);
	INIT_WORK(&session->destroy_work, __iscsi_destroy_session);
	spin_lock_init(&session->lock);

	/* this is released in the dev's release function */
	scsi_host_get(shost);
	session->dev.parent = &shost->shost_gendev;
	session->dev.release = iscsi_session_release;
	device_initialize(&session->dev);
	if (dd_size)
		session->dd_data = &session[1];

	ISCSI_DBG_TRANS_SESSION(session, "Completed session allocation\n");
	return session;
}
EXPORT_SYMBOL_GPL(iscsi_alloc_session);

int iscsi_add_session(struct iscsi_cls_session *session, unsigned int target_id)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(session);
	unsigned long flags;
	int id = 0;
	int err;

	session->sid = atomic_add_return(1, &iscsi_session_nr);

	session->workq = alloc_workqueue("iscsi_ctrl_%d:%d",
			WQ_SYSFS | WQ_MEM_RECLAIM | WQ_UNBOUND, 0,
			shost->host_no, session->sid);
	if (!session->workq)
		return -ENOMEM;

	if (target_id == ISCSI_MAX_TARGET) {
		id = ida_alloc(&iscsi_sess_ida, GFP_KERNEL);

		if (id < 0) {
			iscsi_cls_session_printk(KERN_ERR, session,
					"Failure in Target ID Allocation\n");
			err = id;
			goto destroy_wq;
		}
		session->target_id = (unsigned int)id;
		session->ida_used = true;
	} else
		session->target_id = target_id;
	spin_lock_irqsave(&session->lock, flags);
	session->target_state = ISCSI_SESSION_TARGET_ALLOCATED;
	spin_unlock_irqrestore(&session->lock, flags);

	dev_set_name(&session->dev, "session%u", session->sid);
	err = device_add(&session->dev);
	if (err) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "could not register session's dev\n");
		goto release_ida;
	}
	err = transport_register_device(&session->dev);
	if (err) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "could not register transport's dev\n");
		goto release_dev;
	}

	spin_lock_irqsave(&sesslock, flags);
	list_add(&session->sess_list, &sesslist);
	spin_unlock_irqrestore(&sesslock, flags);

	iscsi_session_event(session, ISCSI_KEVENT_CREATE_SESSION);
	ISCSI_DBG_TRANS_SESSION(session, "Completed session adding\n");
	return 0;

release_dev:
	device_del(&session->dev);
release_ida:
	if (session->ida_used)
		ida_free(&iscsi_sess_ida, session->target_id);
destroy_wq:
	destroy_workqueue(session->workq);
	return err;
}
EXPORT_SYMBOL_GPL(iscsi_add_session);

static void iscsi_conn_release(struct device *dev)
{
	struct iscsi_cls_conn *conn = iscsi_dev_to_conn(dev);
	struct device *parent = conn->dev.parent;

	ISCSI_DBG_TRANS_CONN(conn, "Releasing conn\n");
	kfree(conn);
	put_device(parent);
}

static int iscsi_is_conn_dev(const struct device *dev)
{
	return dev->release == iscsi_conn_release;
}

static int iscsi_iter_destroy_conn_fn(struct device *dev, void *data)
{
	if (!iscsi_is_conn_dev(dev))
		return 0;

	iscsi_remove_conn(iscsi_dev_to_conn(dev));
	iscsi_put_conn(iscsi_dev_to_conn(dev));

	return 0;
}

void iscsi_remove_session(struct iscsi_cls_session *session)
{
	unsigned long flags;
	int err;

	ISCSI_DBG_TRANS_SESSION(session, "Removing session\n");

	spin_lock_irqsave(&sesslock, flags);
	if (!list_empty(&session->sess_list))
		list_del(&session->sess_list);
	spin_unlock_irqrestore(&sesslock, flags);

	if (!cancel_work_sync(&session->block_work))
		cancel_delayed_work_sync(&session->recovery_work);
	cancel_work_sync(&session->unblock_work);
	/*
	 * If we are blocked let commands flow again. The lld or iscsi
	 * layer should set up the queuecommand to fail commands.
	 * We assume that LLD will not be calling block/unblock while
	 * removing the session.
	 */
	spin_lock_irqsave(&session->lock, flags);
	session->state = ISCSI_SESSION_FREE;
	spin_unlock_irqrestore(&session->lock, flags);

	scsi_target_unblock(&session->dev, SDEV_TRANSPORT_OFFLINE);
	/*
	 * qla4xxx can perform it's own scans when it runs in kernel only
	 * mode. Make sure to flush those scans.
	 */
	flush_work(&session->scan_work);
	/* flush running unbind operations */
	flush_work(&session->unbind_work);
	__iscsi_unbind_session(&session->unbind_work);

	/* hw iscsi may not have removed all connections from session */
	err = device_for_each_child(&session->dev, NULL,
				    iscsi_iter_destroy_conn_fn);
	if (err)
		iscsi_cls_session_printk(KERN_ERR, session,
					 "Could not delete all connections "
					 "for session. Error %d.\n", err);

	transport_unregister_device(&session->dev);

	destroy_workqueue(session->workq);

	ISCSI_DBG_TRANS_SESSION(session, "Completing session removal\n");
	device_del(&session->dev);
}
EXPORT_SYMBOL_GPL(iscsi_remove_session);

static void iscsi_stop_conn(struct iscsi_cls_conn *conn, int flag)
{
	ISCSI_DBG_TRANS_CONN(conn, "Stopping conn.\n");

	switch (flag) {
	case STOP_CONN_RECOVER:
		WRITE_ONCE(conn->state, ISCSI_CONN_FAILED);
		break;
	case STOP_CONN_TERM:
		WRITE_ONCE(conn->state, ISCSI_CONN_DOWN);
		break;
	default:
		iscsi_cls_conn_printk(KERN_ERR, conn, "invalid stop flag %d\n",
				      flag);
		return;
	}

	conn->transport->stop_conn(conn, flag);
	ISCSI_DBG_TRANS_CONN(conn, "Stopping conn done.\n");
}

static void iscsi_ep_disconnect(struct iscsi_cls_conn *conn, bool is_active)
{
	struct iscsi_cls_session *session = iscsi_conn_to_session(conn);
	struct iscsi_endpoint *ep;

	ISCSI_DBG_TRANS_CONN(conn, "disconnect ep.\n");
	WRITE_ONCE(conn->state, ISCSI_CONN_FAILED);

	if (!conn->ep || !session->transport->ep_disconnect)
		return;

	ep = conn->ep;
	conn->ep = NULL;

	session->transport->unbind_conn(conn, is_active);
	session->transport->ep_disconnect(ep);
	ISCSI_DBG_TRANS_CONN(conn, "disconnect ep done.\n");
}

static void iscsi_if_disconnect_bound_ep(struct iscsi_cls_conn *conn,
					 struct iscsi_endpoint *ep,
					 bool is_active)
{
	/* Check if this was a conn error and the kernel took ownership */
	spin_lock_irq(&conn->lock);
	if (!test_bit(ISCSI_CLS_CONN_BIT_CLEANUP, &conn->flags)) {
		spin_unlock_irq(&conn->lock);
		iscsi_ep_disconnect(conn, is_active);
	} else {
		spin_unlock_irq(&conn->lock);
		ISCSI_DBG_TRANS_CONN(conn, "flush kernel conn cleanup.\n");
		mutex_unlock(&conn->ep_mutex);

		flush_work(&conn->cleanup_work);
		/*
		 * Userspace is now done with the EP so we can release the ref
		 * iscsi_cleanup_conn_work_fn took.
		 */
		iscsi_put_endpoint(ep);
		mutex_lock(&conn->ep_mutex);
	}
}

static int iscsi_if_stop_conn(struct iscsi_cls_conn *conn, int flag)
{
	ISCSI_DBG_TRANS_CONN(conn, "iscsi if conn stop.\n");
	/*
	 * For offload, iscsid may not know about the ep like when iscsid is
	 * restarted or for kernel based session shutdown iscsid is not even
	 * up. For these cases, we do the disconnect now.
	 */
	mutex_lock(&conn->ep_mutex);
	if (conn->ep)
		iscsi_if_disconnect_bound_ep(conn, conn->ep, true);
	mutex_unlock(&conn->ep_mutex);

	/*
	 * If this is a termination we have to call stop_conn with that flag
	 * so the correct states get set. If we haven't run the work yet try to
	 * avoid the extra run.
	 */
	if (flag == STOP_CONN_TERM) {
		cancel_work_sync(&conn->cleanup_work);
		iscsi_stop_conn(conn, flag);
	} else {
		/*
		 * Figure out if it was the kernel or userspace initiating this.
		 */
		spin_lock_irq(&conn->lock);
		if (!test_and_set_bit(ISCSI_CLS_CONN_BIT_CLEANUP, &conn->flags)) {
			spin_unlock_irq(&conn->lock);
			iscsi_stop_conn(conn, flag);
		} else {
			spin_unlock_irq(&conn->lock);
			ISCSI_DBG_TRANS_CONN(conn,
					     "flush kernel conn cleanup.\n");
			flush_work(&conn->cleanup_work);
		}
		/*
		 * Only clear for recovery to avoid extra cleanup runs during
		 * termination.
		 */
		spin_lock_irq(&conn->lock);
		clear_bit(ISCSI_CLS_CONN_BIT_CLEANUP, &conn->flags);
		spin_unlock_irq(&conn->lock);
	}
	ISCSI_DBG_TRANS_CONN(conn, "iscsi if conn stop done.\n");
	return 0;
}

static void iscsi_cleanup_conn_work_fn(struct work_struct *work)
{
	struct iscsi_cls_conn *conn = container_of(work, struct iscsi_cls_conn,
						   cleanup_work);
	struct iscsi_cls_session *session = iscsi_conn_to_session(conn);

	mutex_lock(&conn->ep_mutex);
	/*
	 * Get a ref to the ep, so we don't release its ID until after
	 * userspace is done referencing it in iscsi_if_disconnect_bound_ep.
	 */
	if (conn->ep)
		get_device(&conn->ep->dev);
	iscsi_ep_disconnect(conn, false);

	if (system_state != SYSTEM_RUNNING) {
		/*
		 * If the user has set up for the session to never timeout
		 * then hang like they wanted. For all other cases fail right
		 * away since userspace is not going to relogin.
		 */
		if (session->recovery_tmo > 0)
			session->recovery_tmo = 0;
	}

	iscsi_stop_conn(conn, STOP_CONN_RECOVER);
	mutex_unlock(&conn->ep_mutex);
	ISCSI_DBG_TRANS_CONN(conn, "cleanup done.\n");
}

static int iscsi_iter_force_destroy_conn_fn(struct device *dev, void *data)
{
	struct iscsi_transport *transport;
	struct iscsi_cls_conn *conn;

	if (!iscsi_is_conn_dev(dev))
		return 0;

	conn = iscsi_dev_to_conn(dev);
	transport = conn->transport;

	if (READ_ONCE(conn->state) != ISCSI_CONN_DOWN)
		iscsi_if_stop_conn(conn, STOP_CONN_TERM);

	transport->destroy_conn(conn);
	return 0;
}

/**
 * iscsi_force_destroy_session - destroy a session from the kernel
 * @session: session to destroy
 *
 * Force the destruction of a session from the kernel. This should only be
 * used when userspace is no longer running during system shutdown.
 */
void iscsi_force_destroy_session(struct iscsi_cls_session *session)
{
	struct iscsi_transport *transport = session->transport;
	unsigned long flags;

	WARN_ON_ONCE(system_state == SYSTEM_RUNNING);

	spin_lock_irqsave(&sesslock, flags);
	if (list_empty(&session->sess_list)) {
		spin_unlock_irqrestore(&sesslock, flags);
		/*
		 * Conn/ep is already freed. Session is being torn down via
		 * async path. For shutdown we don't care about it so return.
		 */
		return;
	}
	spin_unlock_irqrestore(&sesslock, flags);

	device_for_each_child(&session->dev, NULL,
			      iscsi_iter_force_destroy_conn_fn);
	transport->destroy_session(session);
}
EXPORT_SYMBOL_GPL(iscsi_force_destroy_session);

void iscsi_free_session(struct iscsi_cls_session *session)
{
	ISCSI_DBG_TRANS_SESSION(session, "Freeing session\n");
	iscsi_session_event(session, ISCSI_KEVENT_DESTROY_SESSION);
	put_device(&session->dev);
}
EXPORT_SYMBOL_GPL(iscsi_free_session);

/**
 * iscsi_alloc_conn - alloc iscsi class connection
 * @session: iscsi cls session
 * @dd_size: private driver data size
 * @cid: connection id
 */
struct iscsi_cls_conn *
iscsi_alloc_conn(struct iscsi_cls_session *session, int dd_size, uint32_t cid)
{
	struct iscsi_transport *transport = session->transport;
	struct iscsi_cls_conn *conn;

	conn = kzalloc(sizeof(*conn) + dd_size, GFP_KERNEL);
	if (!conn)
		return NULL;
	if (dd_size)
		conn->dd_data = &conn[1];

	mutex_init(&conn->ep_mutex);
	spin_lock_init(&conn->lock);
	INIT_LIST_HEAD(&conn->conn_list);
	INIT_WORK(&conn->cleanup_work, iscsi_cleanup_conn_work_fn);
	conn->transport = transport;
	conn->cid = cid;
	WRITE_ONCE(conn->state, ISCSI_CONN_DOWN);

	/* this is released in the dev's release function */
	if (!get_device(&session->dev))
		goto free_conn;

	dev_set_name(&conn->dev, "connection%d:%u", session->sid, cid);
	device_initialize(&conn->dev);
	conn->dev.parent = &session->dev;
	conn->dev.release = iscsi_conn_release;

	return conn;

free_conn:
	kfree(conn);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_alloc_conn);

/**
 * iscsi_add_conn - add iscsi class connection
 * @conn: iscsi cls connection
 *
 * This will expose iscsi_cls_conn to sysfs so make sure the related
 * resources for sysfs attributes are initialized before calling this.
 */
int iscsi_add_conn(struct iscsi_cls_conn *conn)
{
	int err;
	unsigned long flags;
	struct iscsi_cls_session *session = iscsi_dev_to_session(conn->dev.parent);

	err = device_add(&conn->dev);
	if (err) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "could not register connection's dev\n");
		return err;
	}
	err = transport_register_device(&conn->dev);
	if (err) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "could not register transport's dev\n");
		device_del(&conn->dev);
		return err;
	}

	spin_lock_irqsave(&connlock, flags);
	list_add(&conn->conn_list, &connlist);
	spin_unlock_irqrestore(&connlock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_add_conn);

/**
 * iscsi_remove_conn - remove iscsi class connection from sysfs
 * @conn: iscsi cls connection
 *
 * Remove iscsi_cls_conn from sysfs, and wait for previous
 * read/write of iscsi_cls_conn's attributes in sysfs to finish.
 */
void iscsi_remove_conn(struct iscsi_cls_conn *conn)
{
	unsigned long flags;

	spin_lock_irqsave(&connlock, flags);
	list_del(&conn->conn_list);
	spin_unlock_irqrestore(&connlock, flags);

	transport_unregister_device(&conn->dev);
	device_del(&conn->dev);
}
EXPORT_SYMBOL_GPL(iscsi_remove_conn);

void iscsi_put_conn(struct iscsi_cls_conn *conn)
{
	put_device(&conn->dev);
}
EXPORT_SYMBOL_GPL(iscsi_put_conn);

void iscsi_get_conn(struct iscsi_cls_conn *conn)
{
	get_device(&conn->dev);
}
EXPORT_SYMBOL_GPL(iscsi_get_conn);

/*
 * iscsi interface functions
 */
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

static int
iscsi_multicast_skb(struct sk_buff *skb, uint32_t group, gfp_t gfp)
{
	return nlmsg_multicast(nls, skb, 0, group, gfp);
}

static int
iscsi_unicast_skb(struct sk_buff *skb, u32 portid)
{
	return nlmsg_unicast(nls, skb, portid);
}

int iscsi_recv_pdu(struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
		   char *data, uint32_t data_size)
{
	struct nlmsghdr	*nlh;
	struct sk_buff *skb;
	struct iscsi_uevent *ev;
	char *pdu;
	struct iscsi_internal *priv;
	int len = nlmsg_total_size(sizeof(*ev) + sizeof(struct iscsi_hdr) +
				   data_size);

	priv = iscsi_if_transport_lookup(conn->transport);
	if (!priv)
		return -EINVAL;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		iscsi_conn_error_event(conn, ISCSI_ERR_CONN_FAILED);
		iscsi_cls_conn_printk(KERN_ERR, conn, "can not deliver "
				      "control PDU: OOM\n");
		return -ENOMEM;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	memset(ev, 0, sizeof(*ev));
	ev->transport_handle = iscsi_handle(conn->transport);
	ev->type = ISCSI_KEVENT_RECV_PDU;
	ev->r.recv_req.cid = conn->cid;
	ev->r.recv_req.sid = iscsi_conn_get_sid(conn);
	pdu = (char*)ev + sizeof(*ev);
	memcpy(pdu, hdr, sizeof(struct iscsi_hdr));
	memcpy(pdu + sizeof(struct iscsi_hdr), data, data_size);

	return iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(iscsi_recv_pdu);

int iscsi_offload_mesg(struct Scsi_Host *shost,
		       struct iscsi_transport *transport, uint32_t type,
		       char *data, uint16_t data_size)
{
	struct nlmsghdr	*nlh;
	struct sk_buff *skb;
	struct iscsi_uevent *ev;
	int len = nlmsg_total_size(sizeof(*ev) + data_size);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "can not deliver iscsi offload message:OOM\n");
		return -ENOMEM;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	memset(ev, 0, sizeof(*ev));
	ev->type = type;
	ev->transport_handle = iscsi_handle(transport);
	switch (type) {
	case ISCSI_KEVENT_PATH_REQ:
		ev->r.req_path.host_no = shost->host_no;
		break;
	case ISCSI_KEVENT_IF_DOWN:
		ev->r.notify_if_down.host_no = shost->host_no;
		break;
	}

	memcpy((char *)ev + sizeof(*ev), data, data_size);

	return iscsi_multicast_skb(skb, ISCSI_NL_GRP_UIP, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(iscsi_offload_mesg);

void iscsi_conn_error_event(struct iscsi_cls_conn *conn, enum iscsi_err error)
{
	struct nlmsghdr	*nlh;
	struct sk_buff	*skb;
	struct iscsi_uevent *ev;
	struct iscsi_internal *priv;
	int len = nlmsg_total_size(sizeof(*ev));
	unsigned long flags;
	int state;

	spin_lock_irqsave(&conn->lock, flags);
	/*
	 * Userspace will only do a stop call if we are at least bound. And, we
	 * only need to do the in kernel cleanup if in the UP state so cmds can
	 * be released to upper layers. If in other states just wait for
	 * userspace to avoid races that can leave the cleanup_work queued.
	 */
	state = READ_ONCE(conn->state);
	switch (state) {
	case ISCSI_CONN_BOUND:
	case ISCSI_CONN_UP:
		if (!test_and_set_bit(ISCSI_CLS_CONN_BIT_CLEANUP,
				      &conn->flags)) {
			queue_work(iscsi_conn_cleanup_workq,
				   &conn->cleanup_work);
		}
		break;
	default:
		ISCSI_DBG_TRANS_CONN(conn, "Got conn error in state %d\n",
				     state);
		break;
	}
	spin_unlock_irqrestore(&conn->lock, flags);

	priv = iscsi_if_transport_lookup(conn->transport);
	if (!priv)
		return;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		iscsi_cls_conn_printk(KERN_ERR, conn, "gracefully ignored "
				      "conn error (%d)\n", error);
		return;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	ev->transport_handle = iscsi_handle(conn->transport);
	ev->type = ISCSI_KEVENT_CONN_ERROR;
	ev->r.connerror.error = error;
	ev->r.connerror.cid = conn->cid;
	ev->r.connerror.sid = iscsi_conn_get_sid(conn);

	iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_ATOMIC);

	iscsi_cls_conn_printk(KERN_INFO, conn, "detected conn error (%d)\n",
			      error);
}
EXPORT_SYMBOL_GPL(iscsi_conn_error_event);

void iscsi_conn_login_event(struct iscsi_cls_conn *conn,
			    enum iscsi_conn_state state)
{
	struct nlmsghdr *nlh;
	struct sk_buff  *skb;
	struct iscsi_uevent *ev;
	struct iscsi_internal *priv;
	int len = nlmsg_total_size(sizeof(*ev));

	priv = iscsi_if_transport_lookup(conn->transport);
	if (!priv)
		return;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		iscsi_cls_conn_printk(KERN_ERR, conn, "gracefully ignored "
				      "conn login (%d)\n", state);
		return;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	ev->transport_handle = iscsi_handle(conn->transport);
	ev->type = ISCSI_KEVENT_CONN_LOGIN_STATE;
	ev->r.conn_login.state = state;
	ev->r.conn_login.cid = conn->cid;
	ev->r.conn_login.sid = iscsi_conn_get_sid(conn);
	iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_ATOMIC);

	iscsi_cls_conn_printk(KERN_INFO, conn, "detected conn login (%d)\n",
			      state);
}
EXPORT_SYMBOL_GPL(iscsi_conn_login_event);

void iscsi_post_host_event(uint32_t host_no, struct iscsi_transport *transport,
			   enum iscsi_host_event_code code, uint32_t data_size,
			   uint8_t *data)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	struct iscsi_uevent *ev;
	int len = nlmsg_total_size(sizeof(*ev) + data_size);

	skb = alloc_skb(len, GFP_NOIO);
	if (!skb) {
		printk(KERN_ERR "gracefully ignored host event (%d):%d OOM\n",
		       host_no, code);
		return;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	ev->transport_handle = iscsi_handle(transport);
	ev->type = ISCSI_KEVENT_HOST_EVENT;
	ev->r.host_event.host_no = host_no;
	ev->r.host_event.code = code;
	ev->r.host_event.data_size = data_size;

	if (data_size)
		memcpy((char *)ev + sizeof(*ev), data, data_size);

	iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_NOIO);
}
EXPORT_SYMBOL_GPL(iscsi_post_host_event);

void iscsi_ping_comp_event(uint32_t host_no, struct iscsi_transport *transport,
			   uint32_t status, uint32_t pid, uint32_t data_size,
			   uint8_t *data)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	struct iscsi_uevent *ev;
	int len = nlmsg_total_size(sizeof(*ev) + data_size);

	skb = alloc_skb(len, GFP_NOIO);
	if (!skb) {
		printk(KERN_ERR "gracefully ignored ping comp: OOM\n");
		return;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	ev->transport_handle = iscsi_handle(transport);
	ev->type = ISCSI_KEVENT_PING_COMP;
	ev->r.ping_comp.host_no = host_no;
	ev->r.ping_comp.status = status;
	ev->r.ping_comp.pid = pid;
	ev->r.ping_comp.data_size = data_size;
	memcpy((char *)ev + sizeof(*ev), data, data_size);

	iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_NOIO);
}
EXPORT_SYMBOL_GPL(iscsi_ping_comp_event);

static int
iscsi_if_send_reply(u32 portid, int type, void *payload, int size)
{
	struct sk_buff	*skb;
	struct nlmsghdr	*nlh;
	int len = nlmsg_total_size(size);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "Could not allocate skb to send reply.\n");
		return -ENOMEM;
	}

	nlh = __nlmsg_put(skb, 0, 0, type, (len - sizeof(*nlh)), 0);
	memcpy(nlmsg_data(nlh), payload, size);
	return iscsi_unicast_skb(skb, portid);
}

static int
iscsi_if_get_stats(struct iscsi_transport *transport, struct nlmsghdr *nlh)
{
	struct iscsi_uevent *ev = nlmsg_data(nlh);
	struct iscsi_stats *stats;
	struct sk_buff *skbstat;
	struct iscsi_cls_conn *conn;
	struct nlmsghdr	*nlhstat;
	struct iscsi_uevent *evstat;
	struct iscsi_internal *priv;
	int len = nlmsg_total_size(sizeof(*ev) +
				   sizeof(struct iscsi_stats) +
				   sizeof(struct iscsi_stats_custom) *
				   ISCSI_STATS_CUSTOM_MAX);
	int err = 0;

	priv = iscsi_if_transport_lookup(transport);
	if (!priv)
		return -EINVAL;

	conn = iscsi_conn_lookup(ev->u.get_stats.sid, ev->u.get_stats.cid);
	if (!conn)
		return -EEXIST;

	do {
		int actual_size;

		skbstat = alloc_skb(len, GFP_ATOMIC);
		if (!skbstat) {
			iscsi_cls_conn_printk(KERN_ERR, conn, "can not "
					      "deliver stats: OOM\n");
			return -ENOMEM;
		}

		nlhstat = __nlmsg_put(skbstat, 0, 0, 0,
				      (len - sizeof(*nlhstat)), 0);
		evstat = nlmsg_data(nlhstat);
		memset(evstat, 0, sizeof(*evstat));
		evstat->transport_handle = iscsi_handle(conn->transport);
		evstat->type = nlh->nlmsg_type;
		evstat->u.get_stats.cid =
			ev->u.get_stats.cid;
		evstat->u.get_stats.sid =
			ev->u.get_stats.sid;
		stats = (struct iscsi_stats *)
			((char*)evstat + sizeof(*evstat));
		memset(stats, 0, sizeof(*stats));

		transport->get_stats(conn, stats);
		actual_size = nlmsg_total_size(sizeof(struct iscsi_uevent) +
					       sizeof(struct iscsi_stats) +
					       sizeof(struct iscsi_stats_custom) *
					       stats->custom_length);
		actual_size -= sizeof(*nlhstat);
		actual_size = nlmsg_msg_size(actual_size);
		skb_trim(skbstat, NLMSG_ALIGN(actual_size));
		nlhstat->nlmsg_len = actual_size;

		err = iscsi_multicast_skb(skbstat, ISCSI_NL_GRP_ISCSID,
					  GFP_ATOMIC);
	} while (err < 0 && err != -ECONNREFUSED);

	return err;
}

/**
 * iscsi_session_event - send session destr. completion event
 * @session: iscsi class session
 * @event: type of event
 */
int iscsi_session_event(struct iscsi_cls_session *session,
			enum iscsi_uevent_e event)
{
	struct iscsi_internal *priv;
	struct Scsi_Host *shost;
	struct iscsi_uevent *ev;
	struct sk_buff  *skb;
	struct nlmsghdr *nlh;
	int rc, len = nlmsg_total_size(sizeof(*ev));

	priv = iscsi_if_transport_lookup(session->transport);
	if (!priv)
		return -EINVAL;
	shost = iscsi_session_to_shost(session);

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "Cannot notify userspace of session "
					 "event %u\n", event);
		return -ENOMEM;
	}

	nlh = __nlmsg_put(skb, 0, 0, 0, (len - sizeof(*nlh)), 0);
	ev = nlmsg_data(nlh);
	ev->transport_handle = iscsi_handle(session->transport);

	ev->type = event;
	switch (event) {
	case ISCSI_KEVENT_DESTROY_SESSION:
		ev->r.d_session.host_no = shost->host_no;
		ev->r.d_session.sid = session->sid;
		break;
	case ISCSI_KEVENT_CREATE_SESSION:
		ev->r.c_session_ret.host_no = shost->host_no;
		ev->r.c_session_ret.sid = session->sid;
		break;
	case ISCSI_KEVENT_UNBIND_SESSION:
		ev->r.unbind_session.host_no = shost->host_no;
		ev->r.unbind_session.sid = session->sid;
		break;
	default:
		iscsi_cls_session_printk(KERN_ERR, session, "Invalid event "
					 "%u.\n", event);
		kfree_skb(skb);
		return -EINVAL;
	}

	/*
	 * this will occur if the daemon is not up, so we just warn
	 * the user and when the daemon is restarted it will handle it
	 */
	rc = iscsi_multicast_skb(skb, ISCSI_NL_GRP_ISCSID, GFP_KERNEL);
	if (rc == -ESRCH)
		iscsi_cls_session_printk(KERN_ERR, session,
					 "Cannot notify userspace of session "
					 "event %u. Check iscsi daemon\n",
					 event);

	ISCSI_DBG_TRANS_SESSION(session, "Completed handling event %d rc %d\n",
				event, rc);
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_session_event);

static int
iscsi_if_create_session(struct iscsi_internal *priv, struct iscsi_endpoint *ep,
			struct iscsi_uevent *ev, pid_t pid,
			uint32_t initial_cmdsn,	uint16_t cmds_max,
			uint16_t queue_depth)
{
	struct iscsi_transport *transport = priv->iscsi_transport;
	struct iscsi_cls_session *session;
	struct Scsi_Host *shost;

	session = transport->create_session(ep, cmds_max, queue_depth,
					    initial_cmdsn);
	if (!session)
		return -ENOMEM;

	session->creator = pid;
	shost = iscsi_session_to_shost(session);
	ev->r.c_session_ret.host_no = shost->host_no;
	ev->r.c_session_ret.sid = session->sid;
	ISCSI_DBG_TRANS_SESSION(session,
				"Completed creating transport session\n");
	return 0;
}

static int
iscsi_if_create_conn(struct iscsi_transport *transport, struct iscsi_uevent *ev)
{
	struct iscsi_cls_conn *conn;
	struct iscsi_cls_session *session;

	session = iscsi_session_lookup(ev->u.c_conn.sid);
	if (!session) {
		printk(KERN_ERR "iscsi: invalid session %d.\n",
		       ev->u.c_conn.sid);
		return -EINVAL;
	}

	conn = transport->create_conn(session, ev->u.c_conn.cid);
	if (!conn) {
		iscsi_cls_session_printk(KERN_ERR, session,
					 "couldn't create a new connection.");
		return -ENOMEM;
	}

	ev->r.c_conn_ret.sid = session->sid;
	ev->r.c_conn_ret.cid = conn->cid;

	ISCSI_DBG_TRANS_CONN(conn, "Completed creating transport conn\n");
	return 0;
}

static int
iscsi_if_destroy_conn(struct iscsi_transport *transport, struct iscsi_uevent *ev)
{
	struct iscsi_cls_conn *conn;

	conn = iscsi_conn_lookup(ev->u.d_conn.sid, ev->u.d_conn.cid);
	if (!conn)
		return -EINVAL;

	ISCSI_DBG_TRANS_CONN(conn, "Flushing cleanup during destruction\n");
	flush_work(&conn->cleanup_work);
	ISCSI_DBG_TRANS_CONN(conn, "Destroying transport conn\n");

	if (transport->destroy_conn)
		transport->destroy_conn(conn);
	return 0;
}

static int
iscsi_if_set_param(struct iscsi_transport *transport, struct iscsi_uevent *ev, u32 rlen)
{
	char *data = (char*)ev + sizeof(*ev);
	struct iscsi_cls_conn *conn;
	struct iscsi_cls_session *session;
	int err = 0, value = 0, state;

	if (ev->u.set_param.len > rlen ||
	    ev->u.set_param.len > PAGE_SIZE)
		return -EINVAL;

	session = iscsi_session_lookup(ev->u.set_param.sid);
	conn = iscsi_conn_lookup(ev->u.set_param.sid, ev->u.set_param.cid);
	if (!conn || !session)
		return -EINVAL;

	/* data will be regarded as NULL-ended string, do length check */
	if (strlen(data) > ev->u.set_param.len)
		return -EINVAL;

	switch (ev->u.set_param.param) {
	case ISCSI_PARAM_SESS_RECOVERY_TMO:
		sscanf(data, "%d", &value);
		if (!session->recovery_tmo_sysfs_override)
			session->recovery_tmo = value;
		break;
	default:
		state = READ_ONCE(conn->state);
		if (state == ISCSI_CONN_BOUND || state == ISCSI_CONN_UP) {
			err = transport->set_param(conn, ev->u.set_param.param,
					data, ev->u.set_param.len);
		} else {
			return -ENOTCONN;
		}
	}

	return err;
}

static int iscsi_if_ep_connect(struct iscsi_transport *transport,
			       struct iscsi_uevent *ev, int msg_type)
{
	struct iscsi_endpoint *ep;
	struct sockaddr *dst_addr;
	struct Scsi_Host *shost = NULL;
	int non_blocking, err = 0;

	if (!transport->ep_connect)
		return -EINVAL;

	if (msg_type == ISCSI_UEVENT_TRANSPORT_EP_CONNECT_THROUGH_HOST) {
		shost = scsi_host_lookup(ev->u.ep_connect_through_host.host_no);
		if (!shost) {
			printk(KERN_ERR "ep connect failed. Could not find "
			       "host no %u\n",
			       ev->u.ep_connect_through_host.host_no);
			return -ENODEV;
		}
		non_blocking = ev->u.ep_connect_through_host.non_blocking;
	} else
		non_blocking = ev->u.ep_connect.non_blocking;

	dst_addr = (struct sockaddr *)((char*)ev + sizeof(*ev));
	ep = transport->ep_connect(shost, dst_addr, non_blocking);
	if (IS_ERR(ep)) {
		err = PTR_ERR(ep);
		goto release_host;
	}

	ev->r.ep_connect_ret.handle = ep->id;
release_host:
	if (shost)
		scsi_host_put(shost);
	return err;
}

static int iscsi_if_ep_disconnect(struct iscsi_transport *transport,
				  u64 ep_handle)
{
	struct iscsi_cls_conn *conn;
	struct iscsi_endpoint *ep;

	if (!transport->ep_disconnect)
		return -EINVAL;

	ep = iscsi_lookup_endpoint(ep_handle);
	if (!ep)
		return -EINVAL;

	conn = ep->conn;
	if (!conn) {
		/*
		 * conn was not even bound yet, so we can't get iscsi conn
		 * failures yet.
		 */
		transport->ep_disconnect(ep);
		goto put_ep;
	}

	mutex_lock(&conn->ep_mutex);
	iscsi_if_disconnect_bound_ep(conn, ep, false);
	mutex_unlock(&conn->ep_mutex);
put_ep:
	iscsi_put_endpoint(ep);
	return 0;
}

static int
iscsi_if_transport_ep(struct iscsi_transport *transport,
		      struct iscsi_uevent *ev, int msg_type, u32 rlen)
{
	struct iscsi_endpoint *ep;
	int rc = 0;

	switch (msg_type) {
	case ISCSI_UEVENT_TRANSPORT_EP_CONNECT_THROUGH_HOST:
	case ISCSI_UEVENT_TRANSPORT_EP_CONNECT:
		if (rlen < sizeof(struct sockaddr))
			rc = -EINVAL;
		else
			rc = iscsi_if_ep_connect(transport, ev, msg_type);
		break;
	case ISCSI_UEVENT_TRANSPORT_EP_POLL:
		if (!transport->ep_poll)
			return -EINVAL;

		ep = iscsi_lookup_endpoint(ev->u.ep_poll.ep_handle);
		if (!ep)
			return -EINVAL;

		ev->r.retcode = transport->ep_poll(ep,
						   ev->u.ep_poll.timeout_ms);
		iscsi_put_endpoint(ep);
		break;
	case ISCSI_UEVENT_TRANSPORT_EP_DISCONNECT:
		rc = iscsi_if_ep_disconnect(transport,
					    ev->u.ep_disconnect.ep_handle);
		break;
	}
	return rc;
}

static int
iscsi_tgt_dscvr(struct iscsi_transport *transport,
		struct iscsi_uevent *ev, u32 rlen)
{
	struct Scsi_Host *shost;
	struct sockaddr *dst_addr;
	int err;

	if (rlen < sizeof(*dst_addr))
		return -EINVAL;

	if (!transport->tgt_dscvr)
		return -EINVAL;

	shost = scsi_host_lookup(ev->u.tgt_dscvr.host_no);
	if (!shost) {
		printk(KERN_ERR "target discovery could not find host no %u\n",
		       ev->u.tgt_dscvr.host_no);
		return -ENODEV;
	}


	dst_addr = (struct sockaddr *)((char*)ev + sizeof(*ev));
	err = transport->tgt_dscvr(shost, ev->u.tgt_dscvr.type,
				   ev->u.tgt_dscvr.enable, dst_addr);
	scsi_host_put(shost);
	return err;
}

static int
iscsi_set_host_param(struct iscsi_transport *transport,
		     struct iscsi_uevent *ev, u32 rlen)
{
	char *data = (char*)ev + sizeof(*ev);
	struct Scsi_Host *shost;
	int err;

	if (!transport->set_host_param)
		return -ENOSYS;

	if (ev->u.set_host_param.len > rlen ||
	    ev->u.set_host_param.len > PAGE_SIZE)
		return -EINVAL;

	shost = scsi_host_lookup(ev->u.set_host_param.host_no);
	if (!shost) {
		printk(KERN_ERR "set_host_param could not find host no %u\n",
		       ev->u.set_host_param.host_no);
		return -ENODEV;
	}

	/* see similar check in iscsi_if_set_param() */
	if (strlen(data) > ev->u.set_host_param.len) {
		err = -EINVAL;
		goto out;
	}

	err = transport->set_host_param(shost, ev->u.set_host_param.param,
					data, ev->u.set_host_param.len);
out:
	scsi_host_put(shost);
	return err;
}

static int
iscsi_set_path(struct iscsi_transport *transport, struct iscsi_uevent *ev, u32 rlen)
{
	struct Scsi_Host *shost;
	struct iscsi_path *params;
	int err;

	if (rlen < sizeof(*params))
		return -EINVAL;

	if (!transport->set_path)
		return -ENOSYS;

	shost = scsi_host_lookup(ev->u.set_path.host_no);
	if (!shost) {
		printk(KERN_ERR "set path could not find host no %u\n",
		       ev->u.set_path.host_no);
		return -ENODEV;
	}

	params = (struct iscsi_path *)((char *)ev + sizeof(*ev));
	err = transport->set_path(shost, params);

	scsi_host_put(shost);
	return err;
}

static int iscsi_session_has_conns(int sid)
{
	struct iscsi_cls_conn *conn;
	unsigned long flags;
	int found = 0;

	spin_lock_irqsave(&connlock, flags);
	list_for_each_entry(conn, &connlist, conn_list) {
		if (iscsi_conn_get_sid(conn) == sid) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&connlock, flags);

	return found;
}

static int
iscsi_set_iface_params(struct iscsi_transport *transport,
		       struct iscsi_uevent *ev, uint32_t len)
{
	char *data = (char *)ev + sizeof(*ev);
	struct Scsi_Host *shost;
	int err;

	if (!transport->set_iface_param)
		return -ENOSYS;

	shost = scsi_host_lookup(ev->u.set_iface_params.host_no);
	if (!shost) {
		printk(KERN_ERR "set_iface_params could not find host no %u\n",
		       ev->u.set_iface_params.host_no);
		return -ENODEV;
	}

	err = transport->set_iface_param(shost, data, len);
	scsi_host_put(shost);
	return err;
}

static int
iscsi_send_ping(struct iscsi_transport *transport, struct iscsi_uevent *ev, u32 rlen)
{
	struct Scsi_Host *shost;
	struct sockaddr *dst_addr;
	int err;

	if (rlen < sizeof(*dst_addr))
		return -EINVAL;

	if (!transport->send_ping)
		return -ENOSYS;

	shost = scsi_host_lookup(ev->u.iscsi_ping.host_no);
	if (!shost) {
		printk(KERN_ERR "iscsi_ping could not find host no %u\n",
		       ev->u.iscsi_ping.host_no);
		return -ENODEV;
	}

	dst_addr = (struct sockaddr *)((char *)ev + sizeof(*ev));
	err = transport->send_ping(shost, ev->u.iscsi_ping.iface_num,
				   ev->u.iscsi_ping.iface_type,
				   ev->u.iscsi_ping.payload_size,
				   ev->u.iscsi_ping.pid,
				   dst_addr);
	scsi_host_put(shost);
	return err;
}

static int
iscsi_get_chap(struct iscsi_transport *transport, struct nlmsghdr *nlh)
{
	struct iscsi_uevent *ev = nlmsg_data(nlh);
	struct Scsi_Host *shost = NULL;
	struct iscsi_chap_rec *chap_rec;
	struct iscsi_internal *priv;
	struct sk_buff *skbchap;
	struct nlmsghdr *nlhchap;
	struct iscsi_uevent *evchap;
	uint32_t chap_buf_size;
	int len, err = 0;
	char *buf;

	if (!transport->get_chap)
		return -EINVAL;

	priv = iscsi_if_transport_lookup(transport);
	if (!priv)
		return -EINVAL;

	chap_buf_size = (ev->u.get_chap.num_entries * sizeof(*chap_rec));
	len = nlmsg_total_size(sizeof(*ev) + chap_buf_size);

	shost = scsi_host_lookup(ev->u.get_chap.host_no);
	if (!shost) {
		printk(KERN_ERR "%s: failed. Could not find host no %u\n",
		       __func__, ev->u.get_chap.host_no);
		return -ENODEV;
	}

	do {
		int actual_size;

		skbchap = alloc_skb(len, GFP_KERNEL);
		if (!skbchap) {
			printk(KERN_ERR "can not deliver chap: OOM\n");
			err = -ENOMEM;
			goto exit_get_chap;
		}

		nlhchap = __nlmsg_put(skbchap, 0, 0, 0,
				      (len - sizeof(*nlhchap)), 0);
		evchap = nlmsg_data(nlhchap);
		memset(evchap, 0, sizeof(*evchap));
		evchap->transport_handle = iscsi_handle(transport);
		evchap->type = nlh->nlmsg_type;
		evchap->u.get_chap.host_no = ev->u.get_chap.host_no;
		evchap->u.get_chap.chap_tbl_idx = ev->u.get_chap.chap_tbl_idx;
		evchap->u.get_chap.num_entries = ev->u.get_chap.num_entries;
		buf = (char *)evchap + sizeof(*evchap);
		memset(buf, 0, chap_buf_size);

		err = transport->get_chap(shost, ev->u.get_chap.chap_tbl_idx,
				    &evchap->u.get_chap.num_entries, buf);

		actual_size = nlmsg_total_size(sizeof(*ev) + chap_buf_size);
		skb_trim(skbchap, NLMSG_ALIGN(actual_size));
		nlhchap->nlmsg_len = actual_size;

		err = iscsi_multicast_skb(skbchap, ISCSI_NL_GRP_ISCSID,
					  GFP_KERNEL);
	} while (err < 0 && err != -ECONNREFUSED);

exit_get_chap:
	scsi_host_put(shost);
	return err;
}

static int iscsi_set_chap(struct iscsi_transport *transport,
			  struct iscsi_uevent *ev, uint32_t len)
{
	char *data = (char *)ev + sizeof(*ev);
	struct Scsi_Host *shost;
	int err = 0;

	if (!transport->set_chap)
		return -ENOSYS;

	shost = scsi_host_lookup(ev->u.set_path.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.set_path.host_no);
		return -ENODEV;
	}

	err = transport->set_chap(shost, data, len);
	scsi_host_put(shost);
	return err;
}

static int iscsi_delete_chap(struct iscsi_transport *transport,
			     struct iscsi_uevent *ev)
{
	struct Scsi_Host *shost;
	int err = 0;

	if (!transport->delete_chap)
		return -ENOSYS;

	shost = scsi_host_lookup(ev->u.delete_chap.host_no);
	if (!shost) {
		printk(KERN_ERR "%s could not find host no %u\n",
		       __func__, ev->u.delete_chap.host_no);
		return -ENODEV;
	}

	err = transport->delete_chap(shost, ev->u.delete_chap.chap_tbl_idx);
	scsi_host_put(shost);
	return err;
}

static const struct {
	enum iscsi_discovery_parent_type value;
	char				*name;
} iscsi_discovery_parent_names[] = {
	{ISCSI_DISC_PARENT_UNKNOWN,	"Unknown" },
	{ISCSI_DISC_PARENT_SENDTGT,	"Sendtarget" },
	{ISCSI_DISC_PARENT_ISNS,	"isns" },
};

char *iscsi_get_discovery_parent_name(int parent_type)
{
	int i;
	char *state = "Unknown!";

	for (i = 0; i < ARRAY_SIZE(iscsi_discovery_parent_names); i++) {
		if (iscsi_discovery_parent_names[i].value & parent_type) {
			state = iscsi_discovery_parent_names[i].name;
			break;
		}
	}
	return state;
}
EXPORT_SYMBOL_GPL(iscsi_get_discovery_parent_name);

static int iscsi_set_flashnode_param(struct iscsi_transport *transport,
				     struct iscsi_uevent *ev, uint32_t len)
{
	char *data = (char *)ev + sizeof(*ev);
	struct Scsi_Host *shost;
	struct iscsi_bus_flash_session *fnode_sess;
	struct iscsi_bus_flash_conn *fnode_conn;
	struct device *dev;
	uint32_t idx;
	int err = 0;

	if (!transport->set_flashnode_param) {
		err = -ENOSYS;
		goto exit_set_fnode;
	}

	shost = scsi_host_lookup(ev->u.set_flashnode.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.set_flashnode.host_no);
		err = -ENODEV;
		goto exit_set_fnode;
	}

	idx = ev->u.set_flashnode.flashnode_idx;
	fnode_sess = iscsi_get_flashnode_by_index(shost, idx);
	if (!fnode_sess) {
		pr_err("%s could not find flashnode %u for host no %u\n",
		       __func__, idx, ev->u.set_flashnode.host_no);
		err = -ENODEV;
		goto put_host;
	}

	dev = iscsi_find_flashnode_conn(fnode_sess);
	if (!dev) {
		err = -ENODEV;
		goto put_sess;
	}

	fnode_conn = iscsi_dev_to_flash_conn(dev);
	err = transport->set_flashnode_param(fnode_sess, fnode_conn, data, len);
	put_device(dev);

put_sess:
	put_device(&fnode_sess->dev);

put_host:
	scsi_host_put(shost);

exit_set_fnode:
	return err;
}

static int iscsi_new_flashnode(struct iscsi_transport *transport,
			       struct iscsi_uevent *ev, uint32_t len)
{
	char *data = (char *)ev + sizeof(*ev);
	struct Scsi_Host *shost;
	int index;
	int err = 0;

	if (!transport->new_flashnode) {
		err = -ENOSYS;
		goto exit_new_fnode;
	}

	shost = scsi_host_lookup(ev->u.new_flashnode.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.new_flashnode.host_no);
		err = -ENODEV;
		goto exit_new_fnode;
	}

	index = transport->new_flashnode(shost, data, len);

	if (index >= 0)
		ev->r.new_flashnode_ret.flashnode_idx = index;
	else
		err = -EIO;

	scsi_host_put(shost);

exit_new_fnode:
	return err;
}

static int iscsi_del_flashnode(struct iscsi_transport *transport,
			       struct iscsi_uevent *ev)
{
	struct Scsi_Host *shost;
	struct iscsi_bus_flash_session *fnode_sess;
	uint32_t idx;
	int err = 0;

	if (!transport->del_flashnode) {
		err = -ENOSYS;
		goto exit_del_fnode;
	}

	shost = scsi_host_lookup(ev->u.del_flashnode.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.del_flashnode.host_no);
		err = -ENODEV;
		goto exit_del_fnode;
	}

	idx = ev->u.del_flashnode.flashnode_idx;
	fnode_sess = iscsi_get_flashnode_by_index(shost, idx);
	if (!fnode_sess) {
		pr_err("%s could not find flashnode %u for host no %u\n",
		       __func__, idx, ev->u.del_flashnode.host_no);
		err = -ENODEV;
		goto put_host;
	}

	err = transport->del_flashnode(fnode_sess);
	put_device(&fnode_sess->dev);

put_host:
	scsi_host_put(shost);

exit_del_fnode:
	return err;
}

static int iscsi_login_flashnode(struct iscsi_transport *transport,
				 struct iscsi_uevent *ev)
{
	struct Scsi_Host *shost;
	struct iscsi_bus_flash_session *fnode_sess;
	struct iscsi_bus_flash_conn *fnode_conn;
	struct device *dev;
	uint32_t idx;
	int err = 0;

	if (!transport->login_flashnode) {
		err = -ENOSYS;
		goto exit_login_fnode;
	}

	shost = scsi_host_lookup(ev->u.login_flashnode.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.login_flashnode.host_no);
		err = -ENODEV;
		goto exit_login_fnode;
	}

	idx = ev->u.login_flashnode.flashnode_idx;
	fnode_sess = iscsi_get_flashnode_by_index(shost, idx);
	if (!fnode_sess) {
		pr_err("%s could not find flashnode %u for host no %u\n",
		       __func__, idx, ev->u.login_flashnode.host_no);
		err = -ENODEV;
		goto put_host;
	}

	dev = iscsi_find_flashnode_conn(fnode_sess);
	if (!dev) {
		err = -ENODEV;
		goto put_sess;
	}

	fnode_conn = iscsi_dev_to_flash_conn(dev);
	err = transport->login_flashnode(fnode_sess, fnode_conn);
	put_device(dev);

put_sess:
	put_device(&fnode_sess->dev);

put_host:
	scsi_host_put(shost);

exit_login_fnode:
	return err;
}

static int iscsi_logout_flashnode(struct iscsi_transport *transport,
				  struct iscsi_uevent *ev)
{
	struct Scsi_Host *shost;
	struct iscsi_bus_flash_session *fnode_sess;
	struct iscsi_bus_flash_conn *fnode_conn;
	struct device *dev;
	uint32_t idx;
	int err = 0;

	if (!transport->logout_flashnode) {
		err = -ENOSYS;
		goto exit_logout_fnode;
	}

	shost = scsi_host_lookup(ev->u.logout_flashnode.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.logout_flashnode.host_no);
		err = -ENODEV;
		goto exit_logout_fnode;
	}

	idx = ev->u.logout_flashnode.flashnode_idx;
	fnode_sess = iscsi_get_flashnode_by_index(shost, idx);
	if (!fnode_sess) {
		pr_err("%s could not find flashnode %u for host no %u\n",
		       __func__, idx, ev->u.logout_flashnode.host_no);
		err = -ENODEV;
		goto put_host;
	}

	dev = iscsi_find_flashnode_conn(fnode_sess);
	if (!dev) {
		err = -ENODEV;
		goto put_sess;
	}

	fnode_conn = iscsi_dev_to_flash_conn(dev);

	err = transport->logout_flashnode(fnode_sess, fnode_conn);
	put_device(dev);

put_sess:
	put_device(&fnode_sess->dev);

put_host:
	scsi_host_put(shost);

exit_logout_fnode:
	return err;
}

static int iscsi_logout_flashnode_sid(struct iscsi_transport *transport,
				      struct iscsi_uevent *ev)
{
	struct Scsi_Host *shost;
	struct iscsi_cls_session *session;
	int err = 0;

	if (!transport->logout_flashnode_sid) {
		err = -ENOSYS;
		goto exit_logout_sid;
	}

	shost = scsi_host_lookup(ev->u.logout_flashnode_sid.host_no);
	if (!shost) {
		pr_err("%s could not find host no %u\n",
		       __func__, ev->u.logout_flashnode.host_no);
		err = -ENODEV;
		goto exit_logout_sid;
	}

	session = iscsi_session_lookup(ev->u.logout_flashnode_sid.sid);
	if (!session) {
		pr_err("%s could not find session id %u\n",
		       __func__, ev->u.logout_flashnode_sid.sid);
		err = -EINVAL;
		goto put_host;
	}

	err = transport->logout_flashnode_sid(session);

put_host:
	scsi_host_put(shost);

exit_logout_sid:
	return err;
}

static int
iscsi_get_host_stats(struct iscsi_transport *transport, struct nlmsghdr *nlh)
{
	struct iscsi_uevent *ev = nlmsg_data(nlh);
	struct Scsi_Host *shost = NULL;
	struct iscsi_internal *priv;
	struct sk_buff *skbhost_stats;
	struct nlmsghdr *nlhhost_stats;
	struct iscsi_uevent *evhost_stats;
	int host_stats_size = 0;
	int len, err = 0;
	char *buf;

	if (!transport->get_host_stats)
		return -ENOSYS;

	priv = iscsi_if_transport_lookup(transport);
	if (!priv)
		return -EINVAL;

	host_stats_size = sizeof(struct iscsi_offload_host_stats);
	len = nlmsg_total_size(sizeof(*ev) + host_stats_size);

	shost = scsi_host_lookup(ev->u.get_host_stats.host_no);
	if (!shost) {
		pr_err("%s: failed. Could not find host no %u\n",
		       __func__, ev->u.get_host_stats.host_no);
		return -ENODEV;
	}

	do {
		int actual_size;

		skbhost_stats = alloc_skb(len, GFP_KERNEL);
		if (!skbhost_stats) {
			pr_err("cannot deliver host stats: OOM\n");
			err = -ENOMEM;
			goto exit_host_stats;
		}

		nlhhost_stats = __nlmsg_put(skbhost_stats, 0, 0, 0,
				      (len - sizeof(*nlhhost_stats)), 0);
		evhost_stats = nlmsg_data(nlhhost_stats);
		memset(evhost_stats, 0, sizeof(*evhost_stats));
		evhost_stats->transport_handle = iscsi_handle(transport);
		evhost_stats->type = nlh->nlmsg_type;
		evhost_stats->u.get_host_stats.host_no =
					ev->u.get_host_stats.host_no;
		buf = (char *)evhost_stats + sizeof(*evhost_stats);
		memset(buf, 0, host_stats_size);

		err = transport->get_host_stats(shost, buf, host_stats_size);
		if (err) {
			kfree_skb(skbhost_stats);
			goto exit_host_stats;
		}

		actual_size = nlmsg_total_size(sizeof(*ev) + host_stats_size);
		skb_trim(skbhost_stats, NLMSG_ALIGN(actual_size));
		nlhhost_stats->nlmsg_len = actual_size;

		err = iscsi_multicast_skb(skbhost_stats, ISCSI_NL_GRP_ISCSID,
					  GFP_KERNEL);
	} while (err < 0 && err != -ECONNREFUSED);

exit_host_stats:
	scsi_host_put(shost);
	return err;
}

static int iscsi_if_transport_conn(struct iscsi_transport *transport,
				   struct nlmsghdr *nlh, u32 pdu_len)
{
	struct iscsi_uevent *ev = nlmsg_data(nlh);
	struct iscsi_cls_session *session;
	struct iscsi_cls_conn *conn = NULL;
	struct iscsi_endpoint *ep;
	int err = 0;

	switch (nlh->nlmsg_type) {
	case ISCSI_UEVENT_CREATE_CONN:
		return iscsi_if_create_conn(transport, ev);
	case ISCSI_UEVENT_DESTROY_CONN:
		return iscsi_if_destroy_conn(transport, ev);
	case ISCSI_UEVENT_STOP_CONN:
		conn = iscsi_conn_lookup(ev->u.stop_conn.sid,
					 ev->u.stop_conn.cid);
		if (!conn)
			return -EINVAL;

		return iscsi_if_stop_conn(conn, ev->u.stop_conn.flag);
	}

	/*
	 * The following cmds need to be run under the ep_mutex so in kernel
	 * conn cleanup (ep_disconnect + unbind and conn) is not done while
	 * these are running. They also must not run if we have just run a conn
	 * cleanup because they would set the state in a way that might allow
	 * IO or send IO themselves.
	 */
	switch (nlh->nlmsg_type) {
	case ISCSI_UEVENT_START_CONN:
		conn = iscsi_conn_lookup(ev->u.start_conn.sid,
					 ev->u.start_conn.cid);
		break;
	case ISCSI_UEVENT_BIND_CONN:
		conn = iscsi_conn_lookup(ev->u.b_conn.sid, ev->u.b_conn.cid);
		break;
	case ISCSI_UEVENT_SEND_PDU:
		conn = iscsi_conn_lookup(ev->u.send_pdu.sid, ev->u.send_pdu.cid);
		break;
	}

	if (!conn)
		return -EINVAL;

	mutex_lock(&conn->ep_mutex);
	spin_lock_irq(&conn->lock);
	if (test_bit(ISCSI_CLS_CONN_BIT_CLEANUP, &conn->flags)) {
		spin_unlock_irq(&conn->lock);
		mutex_unlock(&conn->ep_mutex);
		ev->r.retcode = -ENOTCONN;
		return 0;
	}
	spin_unlock_irq(&conn->lock);

	switch (nlh->nlmsg_type) {
	case ISCSI_UEVENT_BIND_CONN:
		session = iscsi_session_lookup(ev->u.b_conn.sid);
		if (!session) {
			err = -EINVAL;
			break;
		}

		ev->r.retcode =	transport->bind_conn(session, conn,
						ev->u.b_conn.transport_eph,
						ev->u.b_conn.is_leading);
		if (!ev->r.retcode)
			WRITE_ONCE(conn->state, ISCSI_CONN_BOUND);

		if (ev->r.retcode || !transport->ep_connect)
			break;

		ep = iscsi_lookup_endpoint(ev->u.b_conn.transport_eph);
		if (ep) {
			ep->conn = conn;
			conn->ep = ep;
			iscsi_put_endpoint(ep);
		} else {
			err = -ENOTCONN;
			iscsi_cls_conn_printk(KERN_ERR, conn,
					      "Could not set ep conn binding\n");
		}
		break;
	case ISCSI_UEVENT_START_CONN:
		ev->r.retcode = transport->start_conn(conn);
		if (!ev->r.retcode)
			WRITE_ONCE(conn->state, ISCSI_CONN_UP);

		break;
	case ISCSI_UEVENT_SEND_PDU:
		if ((ev->u.send_pdu.hdr_size > pdu_len) ||
		    (ev->u.send_pdu.data_size > (pdu_len - ev->u.send_pdu.hdr_size))) {
			err = -EINVAL;
			break;
		}

		ev->r.retcode =	transport->send_pdu(conn,
				(struct iscsi_hdr *)((char *)ev + sizeof(*ev)),
				(char *)ev + sizeof(*ev) + ev->u.send_pdu.hdr_size,
				ev->u.send_pdu.data_size);
		break;
	default:
		err = -ENOSYS;
	}

	mutex_unlock(&conn->ep_mutex);
	return err;
}

static int
iscsi_if_recv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, uint32_t *group)
{
	int err = 0;
	u32 portid;
	struct iscsi_uevent *ev = nlmsg_data(nlh);
	struct iscsi_transport *transport = NULL;
	struct iscsi_internal *priv;
	struct iscsi_cls_session *session;
	struct iscsi_endpoint *ep = NULL;
	u32 rlen;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	if (nlh->nlmsg_type == ISCSI_UEVENT_PATH_UPDATE)
		*group = ISCSI_NL_GRP_UIP;
	else
		*group = ISCSI_NL_GRP_ISCSID;

	priv = iscsi_if_transport_lookup(iscsi_ptr(ev->transport_handle));
	if (!priv)
		return -EINVAL;
	transport = priv->iscsi_transport;

	if (!try_module_get(transport->owner))
		return -EINVAL;

	portid = NETLINK_CB(skb).portid;

	/*
	 * Even though the remaining payload may not be regarded as nlattr,
	 * (like address or something else), calculate the remaining length
	 * here to ease following length checks.
	 */
	rlen = nlmsg_attrlen(nlh, sizeof(*ev));

	switch (nlh->nlmsg_type) {
	case ISCSI_UEVENT_CREATE_SESSION:
		err = iscsi_if_create_session(priv, ep, ev,
					      portid,
					      ev->u.c_session.initial_cmdsn,
					      ev->u.c_session.cmds_max,
					      ev->u.c_session.queue_depth);
		break;
	case ISCSI_UEVENT_CREATE_BOUND_SESSION:
		ep = iscsi_lookup_endpoint(ev->u.c_bound_session.ep_handle);
		if (!ep) {
			err = -EINVAL;
			break;
		}

		err = iscsi_if_create_session(priv, ep, ev,
					portid,
					ev->u.c_bound_session.initial_cmdsn,
					ev->u.c_bound_session.cmds_max,
					ev->u.c_bound_session.queue_depth);
		iscsi_put_endpoint(ep);
		break;
	case ISCSI_UEVENT_DESTROY_SESSION:
		session = iscsi_session_lookup(ev->u.d_session.sid);
		if (!session)
			err = -EINVAL;
		else if (iscsi_session_has_conns(ev->u.d_session.sid))
			err = -EBUSY;
		else
			transport->destroy_session(session);
		break;
	case ISCSI_UEVENT_DESTROY_SESSION_ASYNC:
		session = iscsi_session_lookup(ev->u.d_session.sid);
		if (!session)
			err = -EINVAL;
		else if (iscsi_session_has_conns(ev->u.d_session.sid))
			err = -EBUSY;
		else {
			unsigned long flags;

			/* Prevent this session from being found again */
			spin_lock_irqsave(&sesslock, flags);
			list_del_init(&session->sess_list);
			spin_unlock_irqrestore(&sesslock, flags);

			queue_work(system_unbound_wq, &session->destroy_work);
		}
		break;
	case ISCSI_UEVENT_UNBIND_SESSION:
		session = iscsi_session_lookup(ev->u.d_session.sid);
		if (session)
			queue_work(session->workq, &session->unbind_work);
		else
			err = -EINVAL;
		break;
	case ISCSI_UEVENT_SET_PARAM:
		err = iscsi_if_set_param(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_CREATE_CONN:
	case ISCSI_UEVENT_DESTROY_CONN:
	case ISCSI_UEVENT_STOP_CONN:
	case ISCSI_UEVENT_START_CONN:
	case ISCSI_UEVENT_BIND_CONN:
	case ISCSI_UEVENT_SEND_PDU:
		err = iscsi_if_transport_conn(transport, nlh, rlen);
		break;
	case ISCSI_UEVENT_GET_STATS:
		err = iscsi_if_get_stats(transport, nlh);
		break;
	case ISCSI_UEVENT_TRANSPORT_EP_CONNECT:
	case ISCSI_UEVENT_TRANSPORT_EP_POLL:
	case ISCSI_UEVENT_TRANSPORT_EP_DISCONNECT:
	case ISCSI_UEVENT_TRANSPORT_EP_CONNECT_THROUGH_HOST:
		err = iscsi_if_transport_ep(transport, ev, nlh->nlmsg_type, rlen);
		break;
	case ISCSI_UEVENT_TGT_DSCVR:
		err = iscsi_tgt_dscvr(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_SET_HOST_PARAM:
		err = iscsi_set_host_param(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_PATH_UPDATE:
		err = iscsi_set_path(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_SET_IFACE_PARAMS:
		err = iscsi_set_iface_params(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_PING:
		err = iscsi_send_ping(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_GET_CHAP:
		err = iscsi_get_chap(transport, nlh);
		break;
	case ISCSI_UEVENT_DELETE_CHAP:
		err = iscsi_delete_chap(transport, ev);
		break;
	case ISCSI_UEVENT_SET_FLASHNODE_PARAMS:
		err = iscsi_set_flashnode_param(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_NEW_FLASHNODE:
		err = iscsi_new_flashnode(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_DEL_FLASHNODE:
		err = iscsi_del_flashnode(transport, ev);
		break;
	case ISCSI_UEVENT_LOGIN_FLASHNODE:
		err = iscsi_login_flashnode(transport, ev);
		break;
	case ISCSI_UEVENT_LOGOUT_FLASHNODE:
		err = iscsi_logout_flashnode(transport, ev);
		break;
	case ISCSI_UEVENT_LOGOUT_FLASHNODE_SID:
		err = iscsi_logout_flashnode_sid(transport, ev);
		break;
	case ISCSI_UEVENT_SET_CHAP:
		err = iscsi_set_chap(transport, ev, rlen);
		break;
	case ISCSI_UEVENT_GET_HOST_STATS:
		err = iscsi_get_host_stats(transport, nlh);
		break;
	default:
		err = -ENOSYS;
		break;
	}

	module_put(transport->owner);
	return err;
}

/*
 * Get message from skb.  Each message is processed by iscsi_if_recv_msg.
 * Malformed skbs with wrong lengths or invalid creds are not processed.
 */
static void
iscsi_if_rx(struct sk_buff *skb)
{
	u32 portid = NETLINK_CB(skb).portid;

	mutex_lock(&rx_queue_mutex);
	while (skb->len >= NLMSG_HDRLEN) {
		int err;
		uint32_t rlen;
		struct nlmsghdr	*nlh;
		struct iscsi_uevent *ev;
		uint32_t group;
		int retries = ISCSI_SEND_MAX_ALLOWED;

		nlh = nlmsg_hdr(skb);
		if (nlh->nlmsg_len < sizeof(*nlh) + sizeof(*ev) ||
		    skb->len < nlh->nlmsg_len) {
			break;
		}

		ev = nlmsg_data(nlh);
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		err = iscsi_if_recv_msg(skb, nlh, &group);
		if (err) {
			ev->type = ISCSI_KEVENT_IF_ERROR;
			ev->iferror = err;
		}
		do {
			/*
			 * special case for GET_STATS, GET_CHAP and GET_HOST_STATS:
			 * on success - sending reply and stats from
			 * inside of if_recv_msg(),
			 * on error - fall through.
			 */
			if (ev->type == ISCSI_UEVENT_GET_STATS && !err)
				break;
			if (ev->type == ISCSI_UEVENT_GET_CHAP && !err)
				break;
			if (ev->type == ISCSI_UEVENT_GET_HOST_STATS && !err)
				break;
			err = iscsi_if_send_reply(portid, nlh->nlmsg_type,
						  ev, sizeof(*ev));
			if (err == -EAGAIN && --retries < 0) {
				printk(KERN_WARNING "Send reply failed, error %d\n", err);
				break;
			}
		} while (err < 0 && err != -ECONNREFUSED && err != -ESRCH);
		skb_pull(skb, rlen);
	}
	mutex_unlock(&rx_queue_mutex);
}

#define ISCSI_CLASS_ATTR(_prefix,_name,_mode,_show,_store)		\
struct device_attribute dev_attr_##_prefix##_##_name =	\
	__ATTR(_name,_mode,_show,_store)

/*
 * iSCSI connection attrs
 */
#define iscsi_conn_attr_show(param)					\
static ssize_t								\
show_conn_param_##param(struct device *dev, 				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct iscsi_cls_conn *conn = iscsi_dev_to_conn(dev->parent);	\
	struct iscsi_transport *t = conn->transport;			\
	return t->get_conn_param(conn, param, buf);			\
}

#define iscsi_conn_attr(field, param)					\
	iscsi_conn_attr_show(param)					\
static ISCSI_CLASS_ATTR(conn, field, S_IRUGO, show_conn_param_##param,	\
			NULL);

iscsi_conn_attr(max_recv_dlength, ISCSI_PARAM_MAX_RECV_DLENGTH);
iscsi_conn_attr(max_xmit_dlength, ISCSI_PARAM_MAX_XMIT_DLENGTH);
iscsi_conn_attr(header_digest, ISCSI_PARAM_HDRDGST_EN);
iscsi_conn_attr(data_digest, ISCSI_PARAM_DATADGST_EN);
iscsi_conn_attr(ifmarker, ISCSI_PARAM_IFMARKER_EN);
iscsi_conn_attr(ofmarker, ISCSI_PARAM_OFMARKER_EN);
iscsi_conn_attr(persistent_port, ISCSI_PARAM_PERSISTENT_PORT);
iscsi_conn_attr(exp_statsn, ISCSI_PARAM_EXP_STATSN);
iscsi_conn_attr(persistent_address, ISCSI_PARAM_PERSISTENT_ADDRESS);
iscsi_conn_attr(ping_tmo, ISCSI_PARAM_PING_TMO);
iscsi_conn_attr(recv_tmo, ISCSI_PARAM_RECV_TMO);
iscsi_conn_attr(local_port, ISCSI_PARAM_LOCAL_PORT);
iscsi_conn_attr(statsn, ISCSI_PARAM_STATSN);
iscsi_conn_attr(keepalive_tmo, ISCSI_PARAM_KEEPALIVE_TMO);
iscsi_conn_attr(max_segment_size, ISCSI_PARAM_MAX_SEGMENT_SIZE);
iscsi_conn_attr(tcp_timestamp_stat, ISCSI_PARAM_TCP_TIMESTAMP_STAT);
iscsi_conn_attr(tcp_wsf_disable, ISCSI_PARAM_TCP_WSF_DISABLE);
iscsi_conn_attr(tcp_nagle_disable, ISCSI_PARAM_TCP_NAGLE_DISABLE);
iscsi_conn_attr(tcp_timer_scale, ISCSI_PARAM_TCP_TIMER_SCALE);
iscsi_conn_attr(tcp_timestamp_enable, ISCSI_PARAM_TCP_TIMESTAMP_EN);
iscsi_conn_attr(fragment_disable, ISCSI_PARAM_IP_FRAGMENT_DISABLE);
iscsi_conn_attr(ipv4_tos, ISCSI_PARAM_IPV4_TOS);
iscsi_conn_attr(ipv6_traffic_class, ISCSI_PARAM_IPV6_TC);
iscsi_conn_attr(ipv6_flow_label, ISCSI_PARAM_IPV6_FLOW_LABEL);
iscsi_conn_attr(is_fw_assigned_ipv6, ISCSI_PARAM_IS_FW_ASSIGNED_IPV6);
iscsi_conn_attr(tcp_xmit_wsf, ISCSI_PARAM_TCP_XMIT_WSF);
iscsi_conn_attr(tcp_recv_wsf, ISCSI_PARAM_TCP_RECV_WSF);
iscsi_conn_attr(local_ipaddr, ISCSI_PARAM_LOCAL_IPADDR);

static const char *const connection_state_names[] = {
	[ISCSI_CONN_UP] = "up",
	[ISCSI_CONN_DOWN] = "down",
	[ISCSI_CONN_FAILED] = "failed",
	[ISCSI_CONN_BOUND] = "bound"
};

static ssize_t show_conn_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct iscsi_cls_conn *conn = iscsi_dev_to_conn(dev->parent);
	const char *state = "unknown";
	int conn_state = READ_ONCE(conn->state);

	if (conn_state >= 0 &&
	    conn_state < ARRAY_SIZE(connection_state_names))
		state = connection_state_names[conn_state];

	return sysfs_emit(buf, "%s\n", state);
}
static ISCSI_CLASS_ATTR(conn, state, S_IRUGO, show_conn_state,
			NULL);

#define iscsi_conn_ep_attr_show(param)					\
static ssize_t show_conn_ep_param_##param(struct device *dev,		\
					  struct device_attribute *attr,\
					  char *buf)			\
{									\
	struct iscsi_cls_conn *conn = iscsi_dev_to_conn(dev->parent);	\
	struct iscsi_transport *t = conn->transport;			\
	struct iscsi_endpoint *ep;					\
	ssize_t rc;							\
									\
	/*								\
	 * Need to make sure ep_disconnect does not free the LLD's	\
	 * interconnect resources while we are trying to read them.	\
	 */								\
	mutex_lock(&conn->ep_mutex);					\
	ep = conn->ep;							\
	if (!ep && t->ep_connect) {					\
		mutex_unlock(&conn->ep_mutex);				\
		return -ENOTCONN;					\
	}								\
									\
	if (ep)								\
		rc = t->get_ep_param(ep, param, buf);			\
	else								\
		rc = t->get_conn_param(conn, param, buf);		\
	mutex_unlock(&conn->ep_mutex);					\
	return rc;							\
}

#define iscsi_conn_ep_attr(field, param)				\
	iscsi_conn_ep_attr_show(param)					\
static ISCSI_CLASS_ATTR(conn, field, S_IRUGO,				\
			show_conn_ep_param_##param, NULL);

iscsi_conn_ep_attr(address, ISCSI_PARAM_CONN_ADDRESS);
iscsi_conn_ep_attr(port, ISCSI_PARAM_CONN_PORT);

static struct attribute *iscsi_conn_attrs[] = {
	&dev_attr_conn_max_recv_dlength.attr,
	&dev_attr_conn_max_xmit_dlength.attr,
	&dev_attr_conn_header_digest.attr,
	&dev_attr_conn_data_digest.attr,
	&dev_attr_conn_ifmarker.attr,
	&dev_attr_conn_ofmarker.attr,
	&dev_attr_conn_address.attr,
	&dev_attr_conn_port.attr,
	&dev_attr_conn_exp_statsn.attr,
	&dev_attr_conn_persistent_address.attr,
	&dev_attr_conn_persistent_port.attr,
	&dev_attr_conn_ping_tmo.attr,
	&dev_attr_conn_recv_tmo.attr,
	&dev_attr_conn_local_port.attr,
	&dev_attr_conn_statsn.attr,
	&dev_attr_conn_keepalive_tmo.attr,
	&dev_attr_conn_max_segment_size.attr,
	&dev_attr_conn_tcp_timestamp_stat.attr,
	&dev_attr_conn_tcp_wsf_disable.attr,
	&dev_attr_conn_tcp_nagle_disable.attr,
	&dev_attr_conn_tcp_timer_scale.attr,
	&dev_attr_conn_tcp_timestamp_enable.attr,
	&dev_attr_conn_fragment_disable.attr,
	&dev_attr_conn_ipv4_tos.attr,
	&dev_attr_conn_ipv6_traffic_class.attr,
	&dev_attr_conn_ipv6_flow_label.attr,
	&dev_attr_conn_is_fw_assigned_ipv6.attr,
	&dev_attr_conn_tcp_xmit_wsf.attr,
	&dev_attr_conn_tcp_recv_wsf.attr,
	&dev_attr_conn_local_ipaddr.attr,
	&dev_attr_conn_state.attr,
	NULL,
};

static umode_t iscsi_conn_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int i)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct iscsi_cls_conn *conn = transport_class_to_conn(cdev);
	struct iscsi_transport *t = conn->transport;
	int param;

	if (attr == &dev_attr_conn_max_recv_dlength.attr)
		param = ISCSI_PARAM_MAX_RECV_DLENGTH;
	else if (attr == &dev_attr_conn_max_xmit_dlength.attr)
		param = ISCSI_PARAM_MAX_XMIT_DLENGTH;
	else if (attr == &dev_attr_conn_header_digest.attr)
		param = ISCSI_PARAM_HDRDGST_EN;
	else if (attr == &dev_attr_conn_data_digest.attr)
		param = ISCSI_PARAM_DATADGST_EN;
	else if (attr == &dev_attr_conn_ifmarker.attr)
		param = ISCSI_PARAM_IFMARKER_EN;
	else if (attr == &dev_attr_conn_ofmarker.attr)
		param = ISCSI_PARAM_OFMARKER_EN;
	else if (attr == &dev_attr_conn_address.attr)
		param = ISCSI_PARAM_CONN_ADDRESS;
	else if (attr == &dev_attr_conn_port.attr)
		param = ISCSI_PARAM_CONN_PORT;
	else if (attr == &dev_attr_conn_exp_statsn.attr)
		param = ISCSI_PARAM_EXP_STATSN;
	else if (attr == &dev_attr_conn_persistent_address.attr)
		param = ISCSI_PARAM_PERSISTENT_ADDRESS;
	else if (attr == &dev_attr_conn_persistent_port.attr)
		param = ISCSI_PARAM_PERSISTENT_PORT;
	else if (attr == &dev_attr_conn_ping_tmo.attr)
		param = ISCSI_PARAM_PING_TMO;
	else if (attr == &dev_attr_conn_recv_tmo.attr)
		param = ISCSI_PARAM_RECV_TMO;
	else if (attr == &dev_attr_conn_local_port.attr)
		param = ISCSI_PARAM_LOCAL_PORT;
	else if (attr == &dev_attr_conn_statsn.attr)
		param = ISCSI_PARAM_STATSN;
	else if (attr == &dev_attr_conn_keepalive_tmo.attr)
		param = ISCSI_PARAM_KEEPALIVE_TMO;
	else if (attr == &dev_attr_conn_max_segment_size.attr)
		param = ISCSI_PARAM_MAX_SEGMENT_SIZE;
	else if (attr == &dev_attr_conn_tcp_timestamp_stat.attr)
		param = ISCSI_PARAM_TCP_TIMESTAMP_STAT;
	else if (attr == &dev_attr_conn_tcp_wsf_disable.attr)
		param = ISCSI_PARAM_TCP_WSF_DISABLE;
	else if (attr == &dev_attr_conn_tcp_nagle_disable.attr)
		param = ISCSI_PARAM_TCP_NAGLE_DISABLE;
	else if (attr == &dev_attr_conn_tcp_timer_scale.attr)
		param = ISCSI_PARAM_TCP_TIMER_SCALE;
	else if (attr == &dev_attr_conn_tcp_timestamp_enable.attr)
		param = ISCSI_PARAM_TCP_TIMESTAMP_EN;
	else if (attr == &dev_attr_conn_fragment_disable.attr)
		param = ISCSI_PARAM_IP_FRAGMENT_DISABLE;
	else if (attr == &dev_attr_conn_ipv4_tos.attr)
		param = ISCSI_PARAM_IPV4_TOS;
	else if (attr == &dev_attr_conn_ipv6_traffic_class.attr)
		param = ISCSI_PARAM_IPV6_TC;
	else if (attr == &dev_attr_conn_ipv6_flow_label.attr)
		param = ISCSI_PARAM_IPV6_FLOW_LABEL;
	else if (attr == &dev_attr_conn_is_fw_assigned_ipv6.attr)
		param = ISCSI_PARAM_IS_FW_ASSIGNED_IPV6;
	else if (attr == &dev_attr_conn_tcp_xmit_wsf.attr)
		param = ISCSI_PARAM_TCP_XMIT_WSF;
	else if (attr == &dev_attr_conn_tcp_recv_wsf.attr)
		param = ISCSI_PARAM_TCP_RECV_WSF;
	else if (attr == &dev_attr_conn_local_ipaddr.attr)
		param = ISCSI_PARAM_LOCAL_IPADDR;
	else if (attr == &dev_attr_conn_state.attr)
		return S_IRUGO;
	else {
		WARN_ONCE(1, "Invalid conn attr");
		return 0;
	}

	return t->attr_is_visible(ISCSI_PARAM, param);
}

static struct attribute_group iscsi_conn_group = {
	.attrs = iscsi_conn_attrs,
	.is_visible = iscsi_conn_attr_is_visible,
};

/*
 * iSCSI session attrs
 */
#define iscsi_session_attr_show(param, perm)				\
static ssize_t								\
show_session_param_##param(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct iscsi_cls_session *session = 				\
		iscsi_dev_to_session(dev->parent);			\
	struct iscsi_transport *t = session->transport;			\
									\
	if (perm && !capable(CAP_SYS_ADMIN))				\
		return -EACCES;						\
	return t->get_session_param(session, param, buf);		\
}

#define iscsi_session_attr(field, param, perm)				\
	iscsi_session_attr_show(param, perm)				\
static ISCSI_CLASS_ATTR(sess, field, S_IRUGO, show_session_param_##param, \
			NULL);
iscsi_session_attr(targetname, ISCSI_PARAM_TARGET_NAME, 0);
iscsi_session_attr(initial_r2t, ISCSI_PARAM_INITIAL_R2T_EN, 0);
iscsi_session_attr(max_outstanding_r2t, ISCSI_PARAM_MAX_R2T, 0);
iscsi_session_attr(immediate_data, ISCSI_PARAM_IMM_DATA_EN, 0);
iscsi_session_attr(first_burst_len, ISCSI_PARAM_FIRST_BURST, 0);
iscsi_session_attr(max_burst_len, ISCSI_PARAM_MAX_BURST, 0);
iscsi_session_attr(data_pdu_in_order, ISCSI_PARAM_PDU_INORDER_EN, 0);
iscsi_session_attr(data_seq_in_order, ISCSI_PARAM_DATASEQ_INORDER_EN, 0);
iscsi_session_attr(erl, ISCSI_PARAM_ERL, 0);
iscsi_session_attr(tpgt, ISCSI_PARAM_TPGT, 0);
iscsi_session_attr(username, ISCSI_PARAM_USERNAME, 1);
iscsi_session_attr(username_in, ISCSI_PARAM_USERNAME_IN, 1);
iscsi_session_attr(password, ISCSI_PARAM_PASSWORD, 1);
iscsi_session_attr(password_in, ISCSI_PARAM_PASSWORD_IN, 1);
iscsi_session_attr(chap_out_idx, ISCSI_PARAM_CHAP_OUT_IDX, 1);
iscsi_session_attr(chap_in_idx, ISCSI_PARAM_CHAP_IN_IDX, 1);
iscsi_session_attr(fast_abort, ISCSI_PARAM_FAST_ABORT, 0);
iscsi_session_attr(abort_tmo, ISCSI_PARAM_ABORT_TMO, 0);
iscsi_session_attr(lu_reset_tmo, ISCSI_PARAM_LU_RESET_TMO, 0);
iscsi_session_attr(tgt_reset_tmo, ISCSI_PARAM_TGT_RESET_TMO, 0);
iscsi_session_attr(ifacename, ISCSI_PARAM_IFACE_NAME, 0);
iscsi_session_attr(initiatorname, ISCSI_PARAM_INITIATOR_NAME, 0);
iscsi_session_attr(targetalias, ISCSI_PARAM_TARGET_ALIAS, 0);
iscsi_session_attr(boot_root, ISCSI_PARAM_BOOT_ROOT, 0);
iscsi_session_attr(boot_nic, ISCSI_PARAM_BOOT_NIC, 0);
iscsi_session_attr(boot_target, ISCSI_PARAM_BOOT_TARGET, 0);
iscsi_session_attr(auto_snd_tgt_disable, ISCSI_PARAM_AUTO_SND_TGT_DISABLE, 0);
iscsi_session_attr(discovery_session, ISCSI_PARAM_DISCOVERY_SESS, 0);
iscsi_session_attr(portal_type, ISCSI_PARAM_PORTAL_TYPE, 0);
iscsi_session_attr(chap_auth, ISCSI_PARAM_CHAP_AUTH_EN, 0);
iscsi_session_attr(discovery_logout, ISCSI_PARAM_DISCOVERY_LOGOUT_EN, 0);
iscsi_session_attr(bidi_chap, ISCSI_PARAM_BIDI_CHAP_EN, 0);
iscsi_session_attr(discovery_auth_optional,
		   ISCSI_PARAM_DISCOVERY_AUTH_OPTIONAL, 0);
iscsi_session_attr(def_time2wait, ISCSI_PARAM_DEF_TIME2WAIT, 0);
iscsi_session_attr(def_time2retain, ISCSI_PARAM_DEF_TIME2RETAIN, 0);
iscsi_session_attr(isid, ISCSI_PARAM_ISID, 0);
iscsi_session_attr(tsid, ISCSI_PARAM_TSID, 0);
iscsi_session_attr(def_taskmgmt_tmo, ISCSI_PARAM_DEF_TASKMGMT_TMO, 0);
iscsi_session_attr(discovery_parent_idx, ISCSI_PARAM_DISCOVERY_PARENT_IDX, 0);
iscsi_session_attr(discovery_parent_type, ISCSI_PARAM_DISCOVERY_PARENT_TYPE, 0);

static ssize_t
show_priv_session_target_state(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct iscsi_cls_session *session = iscsi_dev_to_session(dev->parent);

	return sysfs_emit(buf, "%s\n",
			iscsi_session_target_state_name[session->target_state]);
}

static ISCSI_CLASS_ATTR(priv_sess, target_state, S_IRUGO,
			show_priv_session_target_state, NULL);

static ssize_t
show_priv_session_state(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct iscsi_cls_session *session = iscsi_dev_to_session(dev->parent);
	return sysfs_emit(buf, "%s\n", iscsi_session_state_name(session->state));
}
static ISCSI_CLASS_ATTR(priv_sess, state, S_IRUGO, show_priv_session_state,
			NULL);
static ssize_t
show_priv_session_creator(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct iscsi_cls_session *session = iscsi_dev_to_session(dev->parent);
	return sysfs_emit(buf, "%d\n", session->creator);
}
static ISCSI_CLASS_ATTR(priv_sess, creator, S_IRUGO, show_priv_session_creator,
			NULL);
static ssize_t
show_priv_session_target_id(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct iscsi_cls_session *session = iscsi_dev_to_session(dev->parent);
	return sysfs_emit(buf, "%d\n", session->target_id);
}
static ISCSI_CLASS_ATTR(priv_sess, target_id, S_IRUGO,
			show_priv_session_target_id, NULL);

#define iscsi_priv_session_attr_show(field, format)			\
static ssize_t								\
show_priv_session_##field(struct device *dev, 				\
			  struct device_attribute *attr, char *buf)	\
{									\
	struct iscsi_cls_session *session = 				\
			iscsi_dev_to_session(dev->parent);		\
	if (session->field == -1)					\
		return sysfs_emit(buf, "off\n");			\
	return sysfs_emit(buf, format"\n", session->field);		\
}

#define iscsi_priv_session_attr_store(field)				\
static ssize_t								\
store_priv_session_##field(struct device *dev,				\
			   struct device_attribute *attr,		\
			   const char *buf, size_t count)		\
{									\
	int val;							\
	char *cp;							\
	struct iscsi_cls_session *session =				\
		iscsi_dev_to_session(dev->parent);			\
	if ((session->state == ISCSI_SESSION_FREE) ||			\
	    (session->state == ISCSI_SESSION_FAILED))			\
		return -EBUSY;						\
	if (strncmp(buf, "off", 3) == 0) {				\
		session->field = -1;					\
		session->field##_sysfs_override = true;			\
	} else {							\
		val = simple_strtoul(buf, &cp, 0);			\
		if (*cp != '\0' && *cp != '\n')				\
			return -EINVAL;					\
		session->field = val;					\
		session->field##_sysfs_override = true;			\
	}								\
	return count;							\
}

#define iscsi_priv_session_rw_attr(field, format)			\
	iscsi_priv_session_attr_show(field, format)			\
	iscsi_priv_session_attr_store(field)				\
static ISCSI_CLASS_ATTR(priv_sess, field, S_IRUGO | S_IWUSR,		\
			show_priv_session_##field,			\
			store_priv_session_##field)

iscsi_priv_session_rw_attr(recovery_tmo, "%d");

static struct attribute *iscsi_session_attrs[] = {
	&dev_attr_sess_initial_r2t.attr,
	&dev_attr_sess_max_outstanding_r2t.attr,
	&dev_attr_sess_immediate_data.attr,
	&dev_attr_sess_first_burst_len.attr,
	&dev_attr_sess_max_burst_len.attr,
	&dev_attr_sess_data_pdu_in_order.attr,
	&dev_attr_sess_data_seq_in_order.attr,
	&dev_attr_sess_erl.attr,
	&dev_attr_sess_targetname.attr,
	&dev_attr_sess_tpgt.attr,
	&dev_attr_sess_password.attr,
	&dev_attr_sess_password_in.attr,
	&dev_attr_sess_username.attr,
	&dev_attr_sess_username_in.attr,
	&dev_attr_sess_fast_abort.attr,
	&dev_attr_sess_abort_tmo.attr,
	&dev_attr_sess_lu_reset_tmo.attr,
	&dev_attr_sess_tgt_reset_tmo.attr,
	&dev_attr_sess_ifacename.attr,
	&dev_attr_sess_initiatorname.attr,
	&dev_attr_sess_targetalias.attr,
	&dev_attr_sess_boot_root.attr,
	&dev_attr_sess_boot_nic.attr,
	&dev_attr_sess_boot_target.attr,
	&dev_attr_priv_sess_recovery_tmo.attr,
	&dev_attr_priv_sess_state.attr,
	&dev_attr_priv_sess_target_state.attr,
	&dev_attr_priv_sess_creator.attr,
	&dev_attr_sess_chap_out_idx.attr,
	&dev_attr_sess_chap_in_idx.attr,
	&dev_attr_priv_sess_target_id.attr,
	&dev_attr_sess_auto_snd_tgt_disable.attr,
	&dev_attr_sess_discovery_session.attr,
	&dev_attr_sess_portal_type.attr,
	&dev_attr_sess_chap_auth.attr,
	&dev_attr_sess_discovery_logout.attr,
	&dev_attr_sess_bidi_chap.attr,
	&dev_attr_sess_discovery_auth_optional.attr,
	&dev_attr_sess_def_time2wait.attr,
	&dev_attr_sess_def_time2retain.attr,
	&dev_attr_sess_isid.attr,
	&dev_attr_sess_tsid.attr,
	&dev_attr_sess_def_taskmgmt_tmo.attr,
	&dev_attr_sess_discovery_parent_idx.attr,
	&dev_attr_sess_discovery_parent_type.attr,
	NULL,
};

static umode_t iscsi_session_attr_is_visible(struct kobject *kobj,
					    struct attribute *attr, int i)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct iscsi_cls_session *session = transport_class_to_session(cdev);
	struct iscsi_transport *t = session->transport;
	int param;

	if (attr == &dev_attr_sess_initial_r2t.attr)
		param = ISCSI_PARAM_INITIAL_R2T_EN;
	else if (attr == &dev_attr_sess_max_outstanding_r2t.attr)
		param = ISCSI_PARAM_MAX_R2T;
	else if (attr == &dev_attr_sess_immediate_data.attr)
		param = ISCSI_PARAM_IMM_DATA_EN;
	else if (attr == &dev_attr_sess_first_burst_len.attr)
		param = ISCSI_PARAM_FIRST_BURST;
	else if (attr == &dev_attr_sess_max_burst_len.attr)
		param = ISCSI_PARAM_MAX_BURST;
	else if (attr == &dev_attr_sess_data_pdu_in_order.attr)
		param = ISCSI_PARAM_PDU_INORDER_EN;
	else if (attr == &dev_attr_sess_data_seq_in_order.attr)
		param = ISCSI_PARAM_DATASEQ_INORDER_EN;
	else if (attr == &dev_attr_sess_erl.attr)
		param = ISCSI_PARAM_ERL;
	else if (attr == &dev_attr_sess_targetname.attr)
		param = ISCSI_PARAM_TARGET_NAME;
	else if (attr == &dev_attr_sess_tpgt.attr)
		param = ISCSI_PARAM_TPGT;
	else if (attr == &dev_attr_sess_chap_in_idx.attr)
		param = ISCSI_PARAM_CHAP_IN_IDX;
	else if (attr == &dev_attr_sess_chap_out_idx.attr)
		param = ISCSI_PARAM_CHAP_OUT_IDX;
	else if (attr == &dev_attr_sess_password.attr)
		param = ISCSI_PARAM_USERNAME;
	else if (attr == &dev_attr_sess_password_in.attr)
		param = ISCSI_PARAM_USERNAME_IN;
	else if (attr == &dev_attr_sess_username.attr)
		param = ISCSI_PARAM_PASSWORD;
	else if (attr == &dev_attr_sess_username_in.attr)
		param = ISCSI_PARAM_PASSWORD_IN;
	else if (attr == &dev_attr_sess_fast_abort.attr)
		param = ISCSI_PARAM_FAST_ABORT;
	else if (attr == &dev_attr_sess_abort_tmo.attr)
		param = ISCSI_PARAM_ABORT_TMO;
	else if (attr == &dev_attr_sess_lu_reset_tmo.attr)
		param = ISCSI_PARAM_LU_RESET_TMO;
	else if (attr == &dev_attr_sess_tgt_reset_tmo.attr)
		param = ISCSI_PARAM_TGT_RESET_TMO;
	else if (attr == &dev_attr_sess_ifacename.attr)
		param = ISCSI_PARAM_IFACE_NAME;
	else if (attr == &dev_attr_sess_initiatorname.attr)
		param = ISCSI_PARAM_INITIATOR_NAME;
	else if (attr == &dev_attr_sess_targetalias.attr)
		param = ISCSI_PARAM_TARGET_ALIAS;
	else if (attr == &dev_attr_sess_boot_root.attr)
		param = ISCSI_PARAM_BOOT_ROOT;
	else if (attr == &dev_attr_sess_boot_nic.attr)
		param = ISCSI_PARAM_BOOT_NIC;
	else if (attr == &dev_attr_sess_boot_target.attr)
		param = ISCSI_PARAM_BOOT_TARGET;
	else if (attr == &dev_attr_sess_auto_snd_tgt_disable.attr)
		param = ISCSI_PARAM_AUTO_SND_TGT_DISABLE;
	else if (attr == &dev_attr_sess_discovery_session.attr)
		param = ISCSI_PARAM_DISCOVERY_SESS;
	else if (attr == &dev_attr_sess_portal_type.attr)
		param = ISCSI_PARAM_PORTAL_TYPE;
	else if (attr == &dev_attr_sess_chap_auth.attr)
		param = ISCSI_PARAM_CHAP_AUTH_EN;
	else if (attr == &dev_attr_sess_discovery_logout.attr)
		param = ISCSI_PARAM_DISCOVERY_LOGOUT_EN;
	else if (attr == &dev_attr_sess_bidi_chap.attr)
		param = ISCSI_PARAM_BIDI_CHAP_EN;
	else if (attr == &dev_attr_sess_discovery_auth_optional.attr)
		param = ISCSI_PARAM_DISCOVERY_AUTH_OPTIONAL;
	else if (attr == &dev_attr_sess_def_time2wait.attr)
		param = ISCSI_PARAM_DEF_TIME2WAIT;
	else if (attr == &dev_attr_sess_def_time2retain.attr)
		param = ISCSI_PARAM_DEF_TIME2RETAIN;
	else if (attr == &dev_attr_sess_isid.attr)
		param = ISCSI_PARAM_ISID;
	else if (attr == &dev_attr_sess_tsid.attr)
		param = ISCSI_PARAM_TSID;
	else if (attr == &dev_attr_sess_def_taskmgmt_tmo.attr)
		param = ISCSI_PARAM_DEF_TASKMGMT_TMO;
	else if (attr == &dev_attr_sess_discovery_parent_idx.attr)
		param = ISCSI_PARAM_DISCOVERY_PARENT_IDX;
	else if (attr == &dev_attr_sess_discovery_parent_type.attr)
		param = ISCSI_PARAM_DISCOVERY_PARENT_TYPE;
	else if (attr == &dev_attr_priv_sess_recovery_tmo.attr)
		return S_IRUGO | S_IWUSR;
	else if (attr == &dev_attr_priv_sess_state.attr)
		return S_IRUGO;
	else if (attr == &dev_attr_priv_sess_target_state.attr)
		return S_IRUGO;
	else if (attr == &dev_attr_priv_sess_creator.attr)
		return S_IRUGO;
	else if (attr == &dev_attr_priv_sess_target_id.attr)
		return S_IRUGO;
	else {
		WARN_ONCE(1, "Invalid session attr");
		return 0;
	}

	return t->attr_is_visible(ISCSI_PARAM, param);
}

static struct attribute_group iscsi_session_group = {
	.attrs = iscsi_session_attrs,
	.is_visible = iscsi_session_attr_is_visible,
};

/*
 * iSCSI host attrs
 */
#define iscsi_host_attr_show(param)					\
static ssize_t								\
show_host_param_##param(struct device *dev, 				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct Scsi_Host *shost = transport_class_to_shost(dev);	\
	struct iscsi_internal *priv = to_iscsi_internal(shost->transportt); \
	return priv->iscsi_transport->get_host_param(shost, param, buf); \
}

#define iscsi_host_attr(field, param)					\
	iscsi_host_attr_show(param)					\
static ISCSI_CLASS_ATTR(host, field, S_IRUGO, show_host_param_##param,	\
			NULL);

iscsi_host_attr(netdev, ISCSI_HOST_PARAM_NETDEV_NAME);
iscsi_host_attr(hwaddress, ISCSI_HOST_PARAM_HWADDRESS);
iscsi_host_attr(ipaddress, ISCSI_HOST_PARAM_IPADDRESS);
iscsi_host_attr(initiatorname, ISCSI_HOST_PARAM_INITIATOR_NAME);
iscsi_host_attr(port_state, ISCSI_HOST_PARAM_PORT_STATE);
iscsi_host_attr(port_speed, ISCSI_HOST_PARAM_PORT_SPEED);

static struct attribute *iscsi_host_attrs[] = {
	&dev_attr_host_netdev.attr,
	&dev_attr_host_hwaddress.attr,
	&dev_attr_host_ipaddress.attr,
	&dev_attr_host_initiatorname.attr,
	&dev_attr_host_port_state.attr,
	&dev_attr_host_port_speed.attr,
	NULL,
};

static umode_t iscsi_host_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int i)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct iscsi_internal *priv = to_iscsi_internal(shost->transportt);
	int param;

	if (attr == &dev_attr_host_netdev.attr)
		param = ISCSI_HOST_PARAM_NETDEV_NAME;
	else if (attr == &dev_attr_host_hwaddress.attr)
		param = ISCSI_HOST_PARAM_HWADDRESS;
	else if (attr == &dev_attr_host_ipaddress.attr)
		param = ISCSI_HOST_PARAM_IPADDRESS;
	else if (attr == &dev_attr_host_initiatorname.attr)
		param = ISCSI_HOST_PARAM_INITIATOR_NAME;
	else if (attr == &dev_attr_host_port_state.attr)
		param = ISCSI_HOST_PARAM_PORT_STATE;
	else if (attr == &dev_attr_host_port_speed.attr)
		param = ISCSI_HOST_PARAM_PORT_SPEED;
	else {
		WARN_ONCE(1, "Invalid host attr");
		return 0;
	}

	return priv->iscsi_transport->attr_is_visible(ISCSI_HOST_PARAM, param);
}

static struct attribute_group iscsi_host_group = {
	.attrs = iscsi_host_attrs,
	.is_visible = iscsi_host_attr_is_visible,
};

/* convert iscsi_port_speed values to ascii string name */
static const struct {
	enum iscsi_port_speed	value;
	char			*name;
} iscsi_port_speed_names[] = {
	{ISCSI_PORT_SPEED_UNKNOWN,	"Unknown" },
	{ISCSI_PORT_SPEED_10MBPS,	"10 Mbps" },
	{ISCSI_PORT_SPEED_100MBPS,	"100 Mbps" },
	{ISCSI_PORT_SPEED_1GBPS,	"1 Gbps" },
	{ISCSI_PORT_SPEED_10GBPS,	"10 Gbps" },
	{ISCSI_PORT_SPEED_25GBPS,       "25 Gbps" },
	{ISCSI_PORT_SPEED_40GBPS,       "40 Gbps" },
};

char *iscsi_get_port_speed_name(struct Scsi_Host *shost)
{
	int i;
	char *speed = "Unknown!";
	struct iscsi_cls_host *ihost = shost->shost_data;
	uint32_t port_speed = ihost->port_speed;

	for (i = 0; i < ARRAY_SIZE(iscsi_port_speed_names); i++) {
		if (iscsi_port_speed_names[i].value & port_speed) {
			speed = iscsi_port_speed_names[i].name;
			break;
		}
	}
	return speed;
}
EXPORT_SYMBOL_GPL(iscsi_get_port_speed_name);

/* convert iscsi_port_state values to ascii string name */
static const struct {
	enum iscsi_port_state	value;
	char			*name;
} iscsi_port_state_names[] = {
	{ISCSI_PORT_STATE_DOWN,		"LINK DOWN" },
	{ISCSI_PORT_STATE_UP,		"LINK UP" },
};

char *iscsi_get_port_state_name(struct Scsi_Host *shost)
{
	int i;
	char *state = "Unknown!";
	struct iscsi_cls_host *ihost = shost->shost_data;
	uint32_t port_state = ihost->port_state;

	for (i = 0; i < ARRAY_SIZE(iscsi_port_state_names); i++) {
		if (iscsi_port_state_names[i].value & port_state) {
			state = iscsi_port_state_names[i].name;
			break;
		}
	}
	return state;
}
EXPORT_SYMBOL_GPL(iscsi_get_port_state_name);

static int iscsi_session_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct iscsi_cls_session *session;
	struct Scsi_Host *shost;
	struct iscsi_internal *priv;

	if (!iscsi_is_session_dev(dev))
		return 0;

	session = iscsi_dev_to_session(dev);
	shost = iscsi_session_to_shost(session);
	if (!shost->transportt)
		return 0;

	priv = to_iscsi_internal(shost->transportt);
	if (priv->session_cont.ac.class != &iscsi_session_class.class)
		return 0;

	return &priv->session_cont.ac == cont;
}

static int iscsi_conn_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct iscsi_cls_session *session;
	struct iscsi_cls_conn *conn;
	struct Scsi_Host *shost;
	struct iscsi_internal *priv;

	if (!iscsi_is_conn_dev(dev))
		return 0;

	conn = iscsi_dev_to_conn(dev);
	session = iscsi_dev_to_session(conn->dev.parent);
	shost = iscsi_session_to_shost(session);

	if (!shost->transportt)
		return 0;

	priv = to_iscsi_internal(shost->transportt);
	if (priv->conn_cont.ac.class != &iscsi_connection_class.class)
		return 0;

	return &priv->conn_cont.ac == cont;
}

static int iscsi_host_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct iscsi_internal *priv;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt  ||
	    shost->transportt->host_attrs.ac.class != &iscsi_host_class.class)
		return 0;

        priv = to_iscsi_internal(shost->transportt);
        return &priv->t.host_attrs.ac == cont;
}

struct scsi_transport_template *
iscsi_register_transport(struct iscsi_transport *tt)
{
	struct iscsi_internal *priv;
	unsigned long flags;
	int err;

	BUG_ON(!tt);
	WARN_ON(tt->ep_disconnect && !tt->unbind_conn);

	priv = iscsi_if_transport_lookup(tt);
	if (priv)
		return NULL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;
	INIT_LIST_HEAD(&priv->list);
	priv->iscsi_transport = tt;
	priv->t.user_scan = iscsi_user_scan;

	priv->dev.class = &iscsi_transport_class;
	dev_set_name(&priv->dev, "%s", tt->name);
	err = device_register(&priv->dev);
	if (err)
		goto put_dev;

	err = sysfs_create_group(&priv->dev.kobj, &iscsi_transport_group);
	if (err)
		goto unregister_dev;

	/* host parameters */
	priv->t.host_attrs.ac.class = &iscsi_host_class.class;
	priv->t.host_attrs.ac.match = iscsi_host_match;
	priv->t.host_attrs.ac.grp = &iscsi_host_group;
	priv->t.host_size = sizeof(struct iscsi_cls_host);
	transport_container_register(&priv->t.host_attrs);

	/* connection parameters */
	priv->conn_cont.ac.class = &iscsi_connection_class.class;
	priv->conn_cont.ac.match = iscsi_conn_match;
	priv->conn_cont.ac.grp = &iscsi_conn_group;
	transport_container_register(&priv->conn_cont);

	/* session parameters */
	priv->session_cont.ac.class = &iscsi_session_class.class;
	priv->session_cont.ac.match = iscsi_session_match;
	priv->session_cont.ac.grp = &iscsi_session_group;
	transport_container_register(&priv->session_cont);

	spin_lock_irqsave(&iscsi_transport_lock, flags);
	list_add(&priv->list, &iscsi_transports);
	spin_unlock_irqrestore(&iscsi_transport_lock, flags);

	printk(KERN_NOTICE "iscsi: registered transport (%s)\n", tt->name);
	return &priv->t;

unregister_dev:
	device_unregister(&priv->dev);
	return NULL;
put_dev:
	put_device(&priv->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_register_transport);

void iscsi_unregister_transport(struct iscsi_transport *tt)
{
	struct iscsi_internal *priv;
	unsigned long flags;

	BUG_ON(!tt);

	mutex_lock(&rx_queue_mutex);

	priv = iscsi_if_transport_lookup(tt);
	BUG_ON (!priv);

	spin_lock_irqsave(&iscsi_transport_lock, flags);
	list_del(&priv->list);
	spin_unlock_irqrestore(&iscsi_transport_lock, flags);

	transport_container_unregister(&priv->conn_cont);
	transport_container_unregister(&priv->session_cont);
	transport_container_unregister(&priv->t.host_attrs);

	sysfs_remove_group(&priv->dev.kobj, &iscsi_transport_group);
	device_unregister(&priv->dev);
	mutex_unlock(&rx_queue_mutex);
}
EXPORT_SYMBOL_GPL(iscsi_unregister_transport);

void iscsi_dbg_trace(void (*trace)(struct device *dev, struct va_format *),
		     struct device *dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	trace(dev, &vaf);
	va_end(args);
}
EXPORT_SYMBOL_GPL(iscsi_dbg_trace);

static __init int iscsi_transport_init(void)
{
	int err;
	struct netlink_kernel_cfg cfg = {
		.groups	= 1,
		.input	= iscsi_if_rx,
	};
	printk(KERN_INFO "Loading iSCSI transport class v%s.\n",
		ISCSI_TRANSPORT_VERSION);

	atomic_set(&iscsi_session_nr, 0);

	err = class_register(&iscsi_transport_class);
	if (err)
		return err;

	err = class_register(&iscsi_endpoint_class);
	if (err)
		goto unregister_transport_class;

	err = class_register(&iscsi_iface_class);
	if (err)
		goto unregister_endpoint_class;

	err = transport_class_register(&iscsi_host_class);
	if (err)
		goto unregister_iface_class;

	err = transport_class_register(&iscsi_connection_class);
	if (err)
		goto unregister_host_class;

	err = transport_class_register(&iscsi_session_class);
	if (err)
		goto unregister_conn_class;

	err = bus_register(&iscsi_flashnode_bus);
	if (err)
		goto unregister_session_class;

	nls = netlink_kernel_create(&init_net, NETLINK_ISCSI, &cfg);
	if (!nls) {
		err = -ENOBUFS;
		goto unregister_flashnode_bus;
	}

	iscsi_conn_cleanup_workq = alloc_workqueue("%s",
			WQ_SYSFS | WQ_MEM_RECLAIM | WQ_UNBOUND, 0,
			"iscsi_conn_cleanup");
	if (!iscsi_conn_cleanup_workq) {
		err = -ENOMEM;
		goto release_nls;
	}

	return 0;

release_nls:
	netlink_kernel_release(nls);
unregister_flashnode_bus:
	bus_unregister(&iscsi_flashnode_bus);
unregister_session_class:
	transport_class_unregister(&iscsi_session_class);
unregister_conn_class:
	transport_class_unregister(&iscsi_connection_class);
unregister_host_class:
	transport_class_unregister(&iscsi_host_class);
unregister_iface_class:
	class_unregister(&iscsi_iface_class);
unregister_endpoint_class:
	class_unregister(&iscsi_endpoint_class);
unregister_transport_class:
	class_unregister(&iscsi_transport_class);
	return err;
}

static void __exit iscsi_transport_exit(void)
{
	destroy_workqueue(iscsi_conn_cleanup_workq);
	netlink_kernel_release(nls);
	bus_unregister(&iscsi_flashnode_bus);
	transport_class_unregister(&iscsi_connection_class);
	transport_class_unregister(&iscsi_session_class);
	transport_class_unregister(&iscsi_host_class);
	class_unregister(&iscsi_endpoint_class);
	class_unregister(&iscsi_iface_class);
	class_unregister(&iscsi_transport_class);
}

module_init(iscsi_transport_init);
module_exit(iscsi_transport_exit);

MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>, "
	      "Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI Transport Interface");
MODULE_LICENSE("GPL");
MODULE_VERSION(ISCSI_TRANSPORT_VERSION);
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_ISCSI);
