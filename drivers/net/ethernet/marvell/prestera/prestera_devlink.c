// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <net/devlink.h>

#include "prestera_devlink.h"
#include "prestera_hw.h"

/* All driver-specific traps must be documented in
 * Documentation/networking/devlink/prestera.rst
 */
enum {
	DEVLINK_PRESTERA_TRAP_ID_BASE = DEVLINK_TRAP_GENERIC_ID_MAX,
	DEVLINK_PRESTERA_TRAP_ID_ARP_BC,
	DEVLINK_PRESTERA_TRAP_ID_IS_IS,
	DEVLINK_PRESTERA_TRAP_ID_OSPF,
	DEVLINK_PRESTERA_TRAP_ID_IP_BC_MAC,
	DEVLINK_PRESTERA_TRAP_ID_ROUTER_MC,
	DEVLINK_PRESTERA_TRAP_ID_VRRP,
	DEVLINK_PRESTERA_TRAP_ID_DHCP,
	DEVLINK_PRESTERA_TRAP_ID_MAC_TO_ME,
	DEVLINK_PRESTERA_TRAP_ID_IPV4_OPTIONS,
	DEVLINK_PRESTERA_TRAP_ID_IP_DEFAULT_ROUTE,
	DEVLINK_PRESTERA_TRAP_ID_IP_TO_ME,
	DEVLINK_PRESTERA_TRAP_ID_IPV4_ICMP_REDIRECT,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_0,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_1,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_2,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_3,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_4,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_5,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_6,
	DEVLINK_PRESTERA_TRAP_ID_ACL_CODE_7,
	DEVLINK_PRESTERA_TRAP_ID_BGP,
	DEVLINK_PRESTERA_TRAP_ID_SSH,
	DEVLINK_PRESTERA_TRAP_ID_TELNET,
	DEVLINK_PRESTERA_TRAP_ID_ICMP,
	DEVLINK_PRESTERA_TRAP_ID_MET_RED,
	DEVLINK_PRESTERA_TRAP_ID_IP_SIP_IS_ZERO,
	DEVLINK_PRESTERA_TRAP_ID_IP_UC_DIP_DA_MISMATCH,
	DEVLINK_PRESTERA_TRAP_ID_ILLEGAL_IPV4_HDR,
	DEVLINK_PRESTERA_TRAP_ID_ILLEGAL_IP_ADDR,
	DEVLINK_PRESTERA_TRAP_ID_INVALID_SA,
	DEVLINK_PRESTERA_TRAP_ID_LOCAL_PORT,
	DEVLINK_PRESTERA_TRAP_ID_PORT_NO_VLAN,
	DEVLINK_PRESTERA_TRAP_ID_RXDMA_DROP,
};

#define DEVLINK_PRESTERA_TRAP_NAME_ARP_BC \
	"arp_bc"
#define DEVLINK_PRESTERA_TRAP_NAME_IS_IS \
	"is_is"
#define DEVLINK_PRESTERA_TRAP_NAME_OSPF \
	"ospf"
#define DEVLINK_PRESTERA_TRAP_NAME_IP_BC_MAC \
	"ip_bc_mac"
#define DEVLINK_PRESTERA_TRAP_NAME_ROUTER_MC \
	"router_mc"
#define DEVLINK_PRESTERA_TRAP_NAME_VRRP \
	"vrrp"
#define DEVLINK_PRESTERA_TRAP_NAME_DHCP \
	"dhcp"
#define DEVLINK_PRESTERA_TRAP_NAME_MAC_TO_ME \
	"mac_to_me"
#define DEVLINK_PRESTERA_TRAP_NAME_IPV4_OPTIONS \
	"ipv4_options"
#define DEVLINK_PRESTERA_TRAP_NAME_IP_DEFAULT_ROUTE \
	"ip_default_route"
#define DEVLINK_PRESTERA_TRAP_NAME_IP_TO_ME \
	"ip_to_me"
#define DEVLINK_PRESTERA_TRAP_NAME_IPV4_ICMP_REDIRECT \
	"ipv4_icmp_redirect"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_0 \
	"acl_code_0"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_1 \
	"acl_code_1"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_2 \
	"acl_code_2"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_3 \
	"acl_code_3"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_4 \
	"acl_code_4"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_5 \
	"acl_code_5"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_6 \
	"acl_code_6"
#define DEVLINK_PRESTERA_TRAP_NAME_ACL_CODE_7 \
	"acl_code_7"
#define DEVLINK_PRESTERA_TRAP_NAME_BGP \
	"bgp"
#define DEVLINK_PRESTERA_TRAP_NAME_SSH \
	"ssh"
#define DEVLINK_PRESTERA_TRAP_NAME_TELNET \
	"telnet"
#define DEVLINK_PRESTERA_TRAP_NAME_ICMP \
	"icmp"
#define DEVLINK_PRESTERA_TRAP_NAME_RXDMA_DROP \
	"rxdma_drop"
#define DEVLINK_PRESTERA_TRAP_NAME_PORT_NO_VLAN \
	"port_no_vlan"
#define DEVLINK_PRESTERA_TRAP_NAME_LOCAL_PORT \
	"local_port"
#define DEVLINK_PRESTERA_TRAP_NAME_INVALID_SA \
	"invalid_sa"
#define DEVLINK_PRESTERA_TRAP_NAME_ILLEGAL_IP_ADDR \
	"illegal_ip_addr"
#define DEVLINK_PRESTERA_TRAP_NAME_ILLEGAL_IPV4_HDR \
	"illegal_ipv4_hdr"
#define DEVLINK_PRESTERA_TRAP_NAME_IP_UC_DIP_DA_MISMATCH \
	"ip_uc_dip_da_mismatch"
#define DEVLINK_PRESTERA_TRAP_NAME_IP_SIP_IS_ZERO \
	"ip_sip_is_zero"
#define DEVLINK_PRESTERA_TRAP_NAME_MET_RED \
	"met_red"

struct prestera_trap {
	struct devlink_trap trap;
	u8 cpu_code;
};

struct prestera_trap_item {
	enum devlink_trap_action action;
	void *trap_ctx;
};

struct prestera_trap_data {
	struct prestera_switch *sw;
	struct prestera_trap_item *trap_items_arr;
	u32 traps_count;
};

#define PRESTERA_TRAP_METADATA DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT

#define PRESTERA_TRAP_CONTROL(_id, _group_id, _action)			      \
	DEVLINK_TRAP_GENERIC(CONTROL, _action, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     PRESTERA_TRAP_METADATA)

#define PRESTERA_TRAP_DRIVER_CONTROL(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(CONTROL, TRAP, DEVLINK_PRESTERA_TRAP_ID_##_id,    \
			    DEVLINK_PRESTERA_TRAP_NAME_##_id,		      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    PRESTERA_TRAP_METADATA)

#define PRESTERA_TRAP_EXCEPTION(_id, _group_id)				      \
	DEVLINK_TRAP_GENERIC(EXCEPTION, TRAP, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     PRESTERA_TRAP_METADATA)

#define PRESTERA_TRAP_DRIVER_EXCEPTION(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(EXCEPTION, TRAP, DEVLINK_PRESTERA_TRAP_ID_##_id,  \
			    DEVLINK_PRESTERA_TRAP_NAME_##_id,		      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    PRESTERA_TRAP_METADATA)

#define PRESTERA_TRAP_DRIVER_DROP(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(DROP, DROP, DEVLINK_PRESTERA_TRAP_ID_##_id,	      \
			    DEVLINK_PRESTERA_TRAP_NAME_##_id,		      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    PRESTERA_TRAP_METADATA)

static const struct devlink_trap_group prestera_trap_groups_arr[] = {
	/* No policer is associated with following groups (policerid == 0)*/
	DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS, 0),
	DEVLINK_TRAP_GROUP_GENERIC(L3_DROPS, 0),
	DEVLINK_TRAP_GROUP_GENERIC(L3_EXCEPTIONS, 0),
	DEVLINK_TRAP_GROUP_GENERIC(NEIGH_DISCOVERY, 0),
	DEVLINK_TRAP_GROUP_GENERIC(ACL_TRAP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(ACL_DROPS, 0),
	DEVLINK_TRAP_GROUP_GENERIC(ACL_SAMPLE, 0),
	DEVLINK_TRAP_GROUP_GENERIC(OSPF, 0),
	DEVLINK_TRAP_GROUP_GENERIC(STP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(LACP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(LLDP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(VRRP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(DHCP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(BGP, 0),
	DEVLINK_TRAP_GROUP_GENERIC(LOCAL_DELIVERY, 0),
	DEVLINK_TRAP_GROUP_GENERIC(BUFFER_DROPS, 0),
};

/* Initialize trap list, as well as associate CPU code with them. */
static struct prestera_trap prestera_trap_items_arr[] = {
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ARP_BC, NEIGH_DISCOVERY),
		.cpu_code = 5,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(IS_IS, LOCAL_DELIVERY),
		.cpu_code = 13,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(OSPF, OSPF),
		.cpu_code = 16,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(IP_BC_MAC, LOCAL_DELIVERY),
		.cpu_code = 19,
	},
	{
		.trap = PRESTERA_TRAP_CONTROL(STP, STP, TRAP),
		.cpu_code = 26,
	},
	{
		.trap = PRESTERA_TRAP_CONTROL(LACP, LACP, TRAP),
		.cpu_code = 27,
	},
	{
		.trap = PRESTERA_TRAP_CONTROL(LLDP, LLDP, TRAP),
		.cpu_code = 28,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ROUTER_MC, LOCAL_DELIVERY),
		.cpu_code = 29,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(VRRP, VRRP),
		.cpu_code = 30,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(DHCP, DHCP),
		.cpu_code = 33,
	},
	{
		.trap = PRESTERA_TRAP_EXCEPTION(MTU_ERROR, L3_EXCEPTIONS),
		.cpu_code = 63,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(MAC_TO_ME, LOCAL_DELIVERY),
		.cpu_code = 65,
	},
	{
		.trap = PRESTERA_TRAP_EXCEPTION(TTL_ERROR, L3_EXCEPTIONS),
		.cpu_code = 133,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_EXCEPTION(IPV4_OPTIONS,
						       L3_EXCEPTIONS),
		.cpu_code = 141,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(IP_DEFAULT_ROUTE,
						     LOCAL_DELIVERY),
		.cpu_code = 160,
	},
	{
		.trap = PRESTERA_TRAP_CONTROL(LOCAL_ROUTE, LOCAL_DELIVERY,
					      TRAP),
		.cpu_code = 161,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_EXCEPTION(IPV4_ICMP_REDIRECT,
						       L3_EXCEPTIONS),
		.cpu_code = 180,
	},
	{
		.trap = PRESTERA_TRAP_CONTROL(ARP_RESPONSE, NEIGH_DISCOVERY,
					      TRAP),
		.cpu_code = 188,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_0, ACL_TRAP),
		.cpu_code = 192,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_1, ACL_TRAP),
		.cpu_code = 193,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_2, ACL_TRAP),
		.cpu_code = 194,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_3, ACL_TRAP),
		.cpu_code = 195,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_4, ACL_TRAP),
		.cpu_code = 196,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_5, ACL_TRAP),
		.cpu_code = 197,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_6, ACL_TRAP),
		.cpu_code = 198,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ACL_CODE_7, ACL_TRAP),
		.cpu_code = 199,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(BGP, BGP),
		.cpu_code = 206,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(SSH, LOCAL_DELIVERY),
		.cpu_code = 207,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(TELNET, LOCAL_DELIVERY),
		.cpu_code = 208,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_CONTROL(ICMP, LOCAL_DELIVERY),
		.cpu_code = 209,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(RXDMA_DROP, BUFFER_DROPS),
		.cpu_code = 37,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(PORT_NO_VLAN, L2_DROPS),
		.cpu_code = 39,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(LOCAL_PORT, L2_DROPS),
		.cpu_code = 56,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(INVALID_SA, L2_DROPS),
		.cpu_code = 60,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(ILLEGAL_IP_ADDR, L3_DROPS),
		.cpu_code = 136,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(ILLEGAL_IPV4_HDR, L3_DROPS),
		.cpu_code = 137,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(IP_UC_DIP_DA_MISMATCH,
						  L3_DROPS),
		.cpu_code = 138,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(IP_SIP_IS_ZERO, L3_DROPS),
		.cpu_code = 145,
	},
	{
		.trap = PRESTERA_TRAP_DRIVER_DROP(MET_RED, BUFFER_DROPS),
		.cpu_code = 185,
	},
};

static void prestera_devlink_traps_fini(struct prestera_switch *sw);

static int prestera_drop_counter_get(struct devlink *devlink,
				     const struct devlink_trap *trap,
				     u64 *p_drops);

static int prestera_dl_info_get(struct devlink *dl,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct prestera_switch *sw = devlink_priv(dl);
	char buf[16];
	int err;

	err = devlink_info_driver_name_put(req, PRESTERA_DRV_NAME);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "%d.%d.%d",
		 sw->dev->fw_rev.maj,
		 sw->dev->fw_rev.min,
		 sw->dev->fw_rev.sub);

	return devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       buf);
}

static int prestera_trap_init(struct devlink *devlink,
			      const struct devlink_trap *trap, void *trap_ctx);

static int prestera_trap_action_set(struct devlink *devlink,
				    const struct devlink_trap *trap,
				    enum devlink_trap_action action,
				    struct netlink_ext_ack *extack);

static int prestera_devlink_traps_register(struct prestera_switch *sw);

static const struct devlink_ops prestera_dl_ops = {
	.info_get = prestera_dl_info_get,
	.trap_init = prestera_trap_init,
	.trap_action_set = prestera_trap_action_set,
	.trap_drop_counter_get = prestera_drop_counter_get,
};

struct prestera_switch *prestera_devlink_alloc(void)
{
	struct devlink *dl;

	dl = devlink_alloc(&prestera_dl_ops, sizeof(struct prestera_switch));

	return devlink_priv(dl);
}

void prestera_devlink_free(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);

	devlink_free(dl);
}

int prestera_devlink_register(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);
	int err;

	err = devlink_register(dl, sw->dev->dev);
	if (err) {
		dev_err(prestera_dev(sw), "devlink_register failed: %d\n", err);
		return err;
	}

	err = prestera_devlink_traps_register(sw);
	if (err) {
		devlink_unregister(dl);
		dev_err(sw->dev->dev, "devlink_traps_register failed: %d\n",
			err);
		return err;
	}

	return 0;
}

void prestera_devlink_unregister(struct prestera_switch *sw)
{
	struct prestera_trap_data *trap_data = sw->trap_data;
	struct devlink *dl = priv_to_devlink(sw);

	prestera_devlink_traps_fini(sw);
	devlink_unregister(dl);

	kfree(trap_data->trap_items_arr);
	kfree(trap_data);
}

int prestera_devlink_port_register(struct prestera_port *port)
{
	struct prestera_switch *sw = port->sw;
	struct devlink *dl = priv_to_devlink(sw);
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = port->fp_id;
	attrs.switch_id.id_len = sizeof(sw->id);
	memcpy(attrs.switch_id.id, &sw->id, attrs.switch_id.id_len);

	devlink_port_attrs_set(&port->dl_port, &attrs);

	err = devlink_port_register(dl, &port->dl_port, port->fp_id);
	if (err) {
		dev_err(prestera_dev(sw), "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

void prestera_devlink_port_unregister(struct prestera_port *port)
{
	devlink_port_unregister(&port->dl_port);
}

void prestera_devlink_port_set(struct prestera_port *port)
{
	devlink_port_type_eth_set(&port->dl_port, port->dev);
}

void prestera_devlink_port_clear(struct prestera_port *port)
{
	devlink_port_type_clear(&port->dl_port);
}

struct devlink_port *prestera_devlink_get_port(struct net_device *dev)
{
	struct prestera_port *port = netdev_priv(dev);

	return &port->dl_port;
}

static int prestera_devlink_traps_register(struct prestera_switch *sw)
{
	const u32 groups_count = ARRAY_SIZE(prestera_trap_groups_arr);
	const u32 traps_count = ARRAY_SIZE(prestera_trap_items_arr);
	struct devlink *devlink = priv_to_devlink(sw);
	struct prestera_trap_data *trap_data;
	struct prestera_trap *prestera_trap;
	int err, i;

	trap_data = kzalloc(sizeof(*trap_data), GFP_KERNEL);
	if (!trap_data)
		return -ENOMEM;

	trap_data->trap_items_arr = kcalloc(traps_count,
					    sizeof(struct prestera_trap_item),
					    GFP_KERNEL);
	if (!trap_data->trap_items_arr) {
		err = -ENOMEM;
		goto err_trap_items_alloc;
	}

	trap_data->sw = sw;
	trap_data->traps_count = traps_count;
	sw->trap_data = trap_data;

	err = devlink_trap_groups_register(devlink, prestera_trap_groups_arr,
					   groups_count);
	if (err)
		goto err_groups_register;

	for (i = 0; i < traps_count; i++) {
		prestera_trap = &prestera_trap_items_arr[i];
		err = devlink_traps_register(devlink, &prestera_trap->trap, 1,
					     sw);
		if (err)
			goto err_trap_register;
	}

	return 0;

err_trap_register:
	for (i--; i >= 0; i--) {
		prestera_trap = &prestera_trap_items_arr[i];
		devlink_traps_unregister(devlink, &prestera_trap->trap, 1);
	}
	devlink_trap_groups_unregister(devlink, prestera_trap_groups_arr,
				       groups_count);
err_groups_register:
	kfree(trap_data->trap_items_arr);
err_trap_items_alloc:
	kfree(trap_data);
	return err;
}

static struct prestera_trap_item *
prestera_get_trap_item_by_cpu_code(struct prestera_switch *sw, u8 cpu_code)
{
	struct prestera_trap_data *trap_data = sw->trap_data;
	struct prestera_trap *prestera_trap;
	int i;

	for (i = 0; i < trap_data->traps_count; i++) {
		prestera_trap = &prestera_trap_items_arr[i];
		if (cpu_code == prestera_trap->cpu_code)
			return &trap_data->trap_items_arr[i];
	}

	return NULL;
}

void prestera_devlink_trap_report(struct prestera_port *port,
				  struct sk_buff *skb, u8 cpu_code)
{
	struct prestera_trap_item *trap_item;
	struct devlink *devlink;

	devlink = port->dl_port.devlink;

	trap_item = prestera_get_trap_item_by_cpu_code(port->sw, cpu_code);
	if (unlikely(!trap_item))
		return;

	devlink_trap_report(devlink, skb, trap_item->trap_ctx,
			    &port->dl_port, NULL);
}

static struct prestera_trap_item *
prestera_devlink_trap_item_lookup(struct prestera_switch *sw, u16 trap_id)
{
	struct prestera_trap_data *trap_data = sw->trap_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(prestera_trap_items_arr); i++) {
		if (prestera_trap_items_arr[i].trap.id == trap_id)
			return &trap_data->trap_items_arr[i];
	}

	return NULL;
}

static int prestera_trap_init(struct devlink *devlink,
			      const struct devlink_trap *trap, void *trap_ctx)
{
	struct prestera_switch *sw = devlink_priv(devlink);
	struct prestera_trap_item *trap_item;

	trap_item = prestera_devlink_trap_item_lookup(sw, trap->id);
	if (WARN_ON(!trap_item))
		return -EINVAL;

	trap_item->trap_ctx = trap_ctx;
	trap_item->action = trap->init_action;

	return 0;
}

static int prestera_trap_action_set(struct devlink *devlink,
				    const struct devlink_trap *trap,
				    enum devlink_trap_action action,
				    struct netlink_ext_ack *extack)
{
	/* Currently, driver does not support trap action altering */
	return -EOPNOTSUPP;
}

static int prestera_drop_counter_get(struct devlink *devlink,
				     const struct devlink_trap *trap,
				     u64 *p_drops)
{
	struct prestera_switch *sw = devlink_priv(devlink);
	enum prestera_hw_cpu_code_cnt_t cpu_code_type =
		PRESTERA_HW_CPU_CODE_CNT_TYPE_DROP;
	struct prestera_trap *prestera_trap =
		container_of(trap, struct prestera_trap, trap);

	return prestera_hw_cpu_code_counters_get(sw, prestera_trap->cpu_code,
						 cpu_code_type, p_drops);
}

static void prestera_devlink_traps_fini(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);
	const struct devlink_trap *trap;
	int i;

	for (i = 0; i < ARRAY_SIZE(prestera_trap_items_arr); ++i) {
		trap = &prestera_trap_items_arr[i].trap;
		devlink_traps_unregister(dl, trap, 1);
	}

	devlink_trap_groups_unregister(dl, prestera_trap_groups_arr,
				       ARRAY_SIZE(prestera_trap_groups_arr));
}
