// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
#include "dpaa2-eth.h"
/* Copyright 2020 NXP
 */

#define DPAA2_ETH_TRAP_DROP(_id, _group_id)					\
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,					\
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id, 0)

static const struct devlink_trap_group dpaa2_eth_trap_groups_arr[] = {
	DEVLINK_TRAP_GROUP_GENERIC(PARSER_ERROR_DROPS, 0),
};

static const struct devlink_trap dpaa2_eth_traps_arr[] = {
	DPAA2_ETH_TRAP_DROP(VXLAN_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(LLC_SNAP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(VLAN_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(PPPOE_PPP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(MPLS_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(ARP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(IP_1_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(IP_N_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(GRE_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(UDP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(TCP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(IPSEC_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(SCTP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(DCCP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(GTP_PARSING, PARSER_ERROR_DROPS),
	DPAA2_ETH_TRAP_DROP(ESP_PARSING, PARSER_ERROR_DROPS),
};

static int dpaa2_eth_dl_info_get(struct devlink *devlink,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct dpaa2_eth_devlink_priv *dl_priv = devlink_priv(devlink);
	struct dpaa2_eth_priv *priv = dl_priv->dpaa2_priv;
	char buf[10];
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	scnprintf(buf, 10, "%d.%d", priv->dpni_ver_major, priv->dpni_ver_minor);
	err = devlink_info_version_running_put(req, "dpni", buf);
	if (err)
		return err;

	return 0;
}

static struct dpaa2_eth_trap_item *
dpaa2_eth_dl_trap_item_lookup(struct dpaa2_eth_priv *priv, u16 trap_id)
{
	struct dpaa2_eth_trap_data *dpaa2_eth_trap_data = priv->trap_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpaa2_eth_traps_arr); i++) {
		if (dpaa2_eth_traps_arr[i].id == trap_id)
			return &dpaa2_eth_trap_data->trap_items_arr[i];
	}

	return NULL;
}

struct dpaa2_eth_trap_item *dpaa2_eth_dl_get_trap(struct dpaa2_eth_priv *priv,
						  struct dpaa2_fapr *fapr)
{
	static const struct dpaa2_faf_error_bit {
		int position;
		enum devlink_trap_generic_id trap_id;
	} faf_bits[] = {
		{ .position = 5,  .trap_id = DEVLINK_TRAP_GENERIC_ID_VXLAN_PARSING },
		{ .position = 20, .trap_id = DEVLINK_TRAP_GENERIC_ID_LLC_SNAP_PARSING },
		{ .position = 24, .trap_id = DEVLINK_TRAP_GENERIC_ID_VLAN_PARSING },
		{ .position = 26, .trap_id = DEVLINK_TRAP_GENERIC_ID_PPPOE_PPP_PARSING },
		{ .position = 29, .trap_id = DEVLINK_TRAP_GENERIC_ID_MPLS_PARSING },
		{ .position = 31, .trap_id = DEVLINK_TRAP_GENERIC_ID_ARP_PARSING },
		{ .position = 52, .trap_id = DEVLINK_TRAP_GENERIC_ID_IP_1_PARSING },
		{ .position = 61, .trap_id = DEVLINK_TRAP_GENERIC_ID_IP_N_PARSING },
		{ .position = 67, .trap_id = DEVLINK_TRAP_GENERIC_ID_GRE_PARSING },
		{ .position = 71, .trap_id = DEVLINK_TRAP_GENERIC_ID_UDP_PARSING },
		{ .position = 76, .trap_id = DEVLINK_TRAP_GENERIC_ID_TCP_PARSING },
		{ .position = 80, .trap_id = DEVLINK_TRAP_GENERIC_ID_IPSEC_PARSING },
		{ .position = 82, .trap_id = DEVLINK_TRAP_GENERIC_ID_SCTP_PARSING },
		{ .position = 84, .trap_id = DEVLINK_TRAP_GENERIC_ID_DCCP_PARSING },
		{ .position = 88, .trap_id = DEVLINK_TRAP_GENERIC_ID_GTP_PARSING },
		{ .position = 90, .trap_id = DEVLINK_TRAP_GENERIC_ID_ESP_PARSING },
	};
	u64 faf_word;
	u64 mask;
	int i;

	for (i = 0; i < ARRAY_SIZE(faf_bits); i++) {
		if (faf_bits[i].position < 32) {
			/* Low part of FAF.
			 * position ranges from 31 to 0, mask from 0 to 31.
			 */
			mask = 1ull << (31 - faf_bits[i].position);
			faf_word = __le32_to_cpu(fapr->faf_lo);
		} else {
			/* High part of FAF.
			 * position ranges from 95 to 32, mask from 0 to 63.
			 */
			mask = 1ull << (63 - (faf_bits[i].position - 32));
			faf_word = __le64_to_cpu(fapr->faf_hi);
		}
		if (faf_word & mask)
			return dpaa2_eth_dl_trap_item_lookup(priv, faf_bits[i].trap_id);
	}
	return NULL;
}

static int dpaa2_eth_dl_trap_init(struct devlink *devlink,
				  const struct devlink_trap *trap,
				  void *trap_ctx)
{
	struct dpaa2_eth_devlink_priv *dl_priv = devlink_priv(devlink);
	struct dpaa2_eth_priv *priv = dl_priv->dpaa2_priv;
	struct dpaa2_eth_trap_item *dpaa2_eth_trap_item;

	dpaa2_eth_trap_item = dpaa2_eth_dl_trap_item_lookup(priv, trap->id);
	if (WARN_ON(!dpaa2_eth_trap_item))
		return -ENOENT;

	dpaa2_eth_trap_item->trap_ctx = trap_ctx;

	return 0;
}

static int dpaa2_eth_dl_trap_action_set(struct devlink *devlink,
					const struct devlink_trap *trap,
					enum devlink_trap_action action,
					struct netlink_ext_ack *extack)
{
	/* No support for changing the action of an independent packet trap,
	 * only per trap group - parser error drops
	 */
	NL_SET_ERR_MSG_MOD(extack,
			   "Cannot change trap action independently of group");
	return -EOPNOTSUPP;
}

static int dpaa2_eth_dl_trap_group_action_set(struct devlink *devlink,
					      const struct devlink_trap_group *group,
					      enum devlink_trap_action action,
					      struct netlink_ext_ack *extack)
{
	struct dpaa2_eth_devlink_priv *dl_priv = devlink_priv(devlink);
	struct dpaa2_eth_priv *priv = dl_priv->dpaa2_priv;
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	struct dpni_error_cfg err_cfg = {0};
	int err;

	if (group->id != DEVLINK_TRAP_GROUP_GENERIC_ID_PARSER_ERROR_DROPS)
		return -EOPNOTSUPP;

	/* Configure handling of frames marked as errors from the parser */
	err_cfg.errors = DPAA2_FAS_RX_ERR_MASK;
	err_cfg.set_frame_annotation = 1;

	switch (action) {
	case DEVLINK_TRAP_ACTION_DROP:
		err_cfg.error_action = DPNI_ERROR_ACTION_DISCARD;
		break;
	case DEVLINK_TRAP_ACTION_TRAP:
		err_cfg.error_action = DPNI_ERROR_ACTION_SEND_TO_ERROR_QUEUE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = dpni_set_errors_behavior(priv->mc_io, 0, priv->mc_token, &err_cfg);
	if (err) {
		dev_err(dev, "dpni_set_errors_behavior failed\n");
		return err;
	}

	return 0;
}

static const struct devlink_ops dpaa2_eth_devlink_ops = {
	.info_get = dpaa2_eth_dl_info_get,
	.trap_init = dpaa2_eth_dl_trap_init,
	.trap_action_set = dpaa2_eth_dl_trap_action_set,
	.trap_group_action_set = dpaa2_eth_dl_trap_group_action_set,
};

int dpaa2_eth_dl_register(struct dpaa2_eth_priv *priv)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	struct dpaa2_eth_devlink_priv *dl_priv;
	int err;

	priv->devlink =
		devlink_alloc(&dpaa2_eth_devlink_ops, sizeof(*dl_priv), dev);
	if (!priv->devlink) {
		dev_err(dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}
	dl_priv = devlink_priv(priv->devlink);
	dl_priv->dpaa2_priv = priv;

	err = devlink_register(priv->devlink);
	if (err) {
		dev_err(dev, "devlink_register() = %d\n", err);
		goto devlink_free;
	}

	return 0;

devlink_free:
	devlink_free(priv->devlink);

	return err;
}

void dpaa2_eth_dl_unregister(struct dpaa2_eth_priv *priv)
{
	devlink_unregister(priv->devlink);
	devlink_free(priv->devlink);
}

int dpaa2_eth_dl_port_add(struct dpaa2_eth_priv *priv)
{
	struct devlink_port *devlink_port = &priv->devlink_port;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	devlink_port_attrs_set(devlink_port, &attrs);

	err = devlink_port_register(priv->devlink, devlink_port, 0);
	if (err)
		return err;

	devlink_port_type_eth_set(devlink_port, priv->net_dev);

	return 0;
}

void dpaa2_eth_dl_port_del(struct dpaa2_eth_priv *priv)
{
	struct devlink_port *devlink_port = &priv->devlink_port;

	devlink_port_type_clear(devlink_port);
	devlink_port_unregister(devlink_port);
}

int dpaa2_eth_dl_traps_register(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_eth_trap_data *dpaa2_eth_trap_data;
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	int err;

	dpaa2_eth_trap_data = kzalloc(sizeof(*dpaa2_eth_trap_data), GFP_KERNEL);
	if (!dpaa2_eth_trap_data)
		return -ENOMEM;
	priv->trap_data = dpaa2_eth_trap_data;

	dpaa2_eth_trap_data->trap_items_arr = kcalloc(ARRAY_SIZE(dpaa2_eth_traps_arr),
						      sizeof(struct dpaa2_eth_trap_item),
						      GFP_KERNEL);
	if (!dpaa2_eth_trap_data->trap_items_arr) {
		err = -ENOMEM;
		goto trap_data_free;
	}

	err = devlink_trap_groups_register(priv->devlink, dpaa2_eth_trap_groups_arr,
					   ARRAY_SIZE(dpaa2_eth_trap_groups_arr));
	if (err) {
		dev_err(dev, "devlink_trap_groups_register() = %d\n", err);
		goto trap_items_arr_free;
	}

	err = devlink_traps_register(priv->devlink, dpaa2_eth_traps_arr,
				     ARRAY_SIZE(dpaa2_eth_traps_arr), priv);
	if (err) {
		dev_err(dev, "devlink_traps_register() = %d\n", err);
		goto trap_groups_unregiser;
	}

	return 0;

trap_groups_unregiser:
	devlink_trap_groups_unregister(priv->devlink, dpaa2_eth_trap_groups_arr,
				       ARRAY_SIZE(dpaa2_eth_trap_groups_arr));
trap_items_arr_free:
	kfree(dpaa2_eth_trap_data->trap_items_arr);
trap_data_free:
	kfree(dpaa2_eth_trap_data);
	priv->trap_data = NULL;

	return err;
}

void dpaa2_eth_dl_traps_unregister(struct dpaa2_eth_priv *priv)
{
	devlink_traps_unregister(priv->devlink, dpaa2_eth_traps_arr,
				 ARRAY_SIZE(dpaa2_eth_traps_arr));
	devlink_trap_groups_unregister(priv->devlink, dpaa2_eth_trap_groups_arr,
				       ARRAY_SIZE(dpaa2_eth_trap_groups_arr));
	kfree(priv->trap_data->trap_items_arr);
	kfree(priv->trap_data);
}
