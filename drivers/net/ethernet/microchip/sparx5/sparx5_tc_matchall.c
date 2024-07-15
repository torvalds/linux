// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_tc.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"

static struct sparx5_mall_entry *
sparx5_tc_matchall_entry_find(struct list_head *entries, unsigned long cookie)
{
	struct sparx5_mall_entry *entry;

	list_for_each_entry(entry, entries, list) {
		if (entry->cookie == cookie)
			return entry;
	}

	return NULL;
}

static void sparx5_tc_matchall_parse_action(struct sparx5_port *port,
					    struct sparx5_mall_entry *entry,
					    struct flow_action_entry *action,
					    bool ingress,
					    unsigned long cookie)
{
	entry->port = port;
	entry->type = action->id;
	entry->ingress = ingress;
	entry->cookie = cookie;
}

static void
sparx5_tc_matchall_parse_mirror_action(struct sparx5_mall_entry *entry,
				       struct flow_action_entry *action)
{
	entry->mirror.port = netdev_priv(action->dev);
}

static int sparx5_tc_matchall_replace(struct net_device *ndev,
				      struct tc_cls_matchall_offload *tmo,
				      bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5_mall_entry *mall_entry;
	struct flow_action_entry *action;
	struct sparx5 *sparx5;
	int err;

	if (!flow_offload_has_one_action(&tmo->rule->action)) {
		NL_SET_ERR_MSG_MOD(tmo->common.extack,
				   "Only one action per filter is supported");
		return -EOPNOTSUPP;
	}
	action = &tmo->rule->action.entries[0];

	mall_entry = kzalloc(sizeof(*mall_entry), GFP_KERNEL);
	if (!mall_entry)
		return -ENOMEM;

	sparx5_tc_matchall_parse_action(port,
					mall_entry,
					action,
					ingress,
					tmo->cookie);

	sparx5 = port->sparx5;
	switch (action->id) {
	case FLOW_ACTION_MIRRED:
		sparx5_tc_matchall_parse_mirror_action(mall_entry, action);
		err = sparx5_mirror_add(mall_entry);
		if (err) {
			switch (err) {
			case -EEXIST:
				NL_SET_ERR_MSG_MOD(tmo->common.extack,
						   "Mirroring already exists");
				break;
			case -EINVAL:
				NL_SET_ERR_MSG_MOD(tmo->common.extack,
						   "Cannot mirror a monitor port");
				break;
			case -ENOENT:
				NL_SET_ERR_MSG_MOD(tmo->common.extack,
						   "No more mirror probes available");
				break;
			default:
				NL_SET_ERR_MSG_MOD(tmo->common.extack,
						   "Unknown error");
				break;
			}
			return err;
		}
		/* Get baseline stats for this port */
		sparx5_mirror_stats(mall_entry, &tmo->stats);
		break;
	case FLOW_ACTION_GOTO:
		err = vcap_enable_lookups(sparx5->vcap_ctrl, ndev,
					  tmo->common.chain_index,
					  action->chain_index, tmo->cookie,
					  true);
		if (err == -EFAULT) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Unsupported goto chain");
			return -EOPNOTSUPP;
		}
		if (err == -EADDRINUSE) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "VCAP already enabled");
			return -EOPNOTSUPP;
		}
		if (err == -EADDRNOTAVAIL) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Already matching this chain");
			return -EOPNOTSUPP;
		}
		if (err) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Could not enable VCAP lookups");
			return err;
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(tmo->common.extack, "Unsupported action");
		return -EOPNOTSUPP;
	}

	list_add_tail(&mall_entry->list, &sparx5->mall_entries);

	return 0;
}

static int sparx5_tc_matchall_destroy(struct net_device *ndev,
				      struct tc_cls_matchall_offload *tmo,
				      bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	struct sparx5_mall_entry *entry;
	int err = 0;

	entry = sparx5_tc_matchall_entry_find(&sparx5->mall_entries,
					      tmo->cookie);
	if (!entry)
		return -ENOENT;

	if (entry->type == FLOW_ACTION_MIRRED) {
		sparx5_mirror_del(entry);
	} else if (entry->type == FLOW_ACTION_GOTO) {
		err = vcap_enable_lookups(sparx5->vcap_ctrl, ndev,
					  0, 0, tmo->cookie, false);
	} else {
		NL_SET_ERR_MSG_MOD(tmo->common.extack, "Unsupported action");
		err = -EOPNOTSUPP;
	}

	list_del(&entry->list);

	return err;
}

static int sparx5_tc_matchall_stats(struct net_device *ndev,
				    struct tc_cls_matchall_offload *tmo,
				    bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	struct sparx5_mall_entry *entry;

	entry = sparx5_tc_matchall_entry_find(&sparx5->mall_entries,
					      tmo->cookie);
	if (!entry)
		return -ENOENT;

	if (entry->type == FLOW_ACTION_MIRRED) {
		sparx5_mirror_stats(entry, &tmo->stats);
	} else {
		NL_SET_ERR_MSG_MOD(tmo->common.extack, "Unsupported action");
		return -EOPNOTSUPP;
	}

	return 0;
}

int sparx5_tc_matchall(struct net_device *ndev,
		       struct tc_cls_matchall_offload *tmo,
		       bool ingress)
{
	switch (tmo->command) {
	case TC_CLSMATCHALL_REPLACE:
		return sparx5_tc_matchall_replace(ndev, tmo, ingress);
	case TC_CLSMATCHALL_DESTROY:
		return sparx5_tc_matchall_destroy(ndev, tmo, ingress);
	case TC_CLSMATCHALL_STATS:
		return sparx5_tc_matchall_stats(ndev, tmo, ingress);
	default:
		return -EOPNOTSUPP;
	}
}
