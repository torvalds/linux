// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc_encap_actions.h"
#include "tc.h"
#include "mae.h"
#include <net/vxlan.h>
#include <net/geneve.h>

static const struct rhashtable_params efx_tc_encap_ht_params = {
	.key_len	= offsetofend(struct efx_tc_encap_action, key),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_encap_action, linkage),
};

static void efx_tc_encap_free(void *ptr, void *__unused)
{
	struct efx_tc_encap_action *enc = ptr;

	WARN_ON(refcount_read(&enc->ref));
	kfree(enc);
}

int efx_tc_init_encap_actions(struct efx_nic *efx)
{
	return rhashtable_init(&efx->tc->encap_ht, &efx_tc_encap_ht_params);
}

/* Only call this in init failure teardown.
 * Normal exit should fini instead as there may be entries in the table.
 */
void efx_tc_destroy_encap_actions(struct efx_nic *efx)
{
	rhashtable_destroy(&efx->tc->encap_ht);
}

void efx_tc_fini_encap_actions(struct efx_nic *efx)
{
	rhashtable_free_and_destroy(&efx->tc->encap_ht, efx_tc_encap_free, NULL);
}

bool efx_tc_check_ready(struct efx_nic *efx, struct efx_tc_flow_rule *rule)
{
	struct efx_tc_action_set *act;

	/* Encap actions can only be offloaded if they have valid
	 * neighbour info for the outer Ethernet header.
	 */
	list_for_each_entry(act, &rule->acts.list, list)
		if (act->encap_md) /* neigh bindings not implemented yet */
			return false;
	return true;
}

struct efx_tc_encap_action *efx_tc_flower_create_encap_md(
			struct efx_nic *efx, const struct ip_tunnel_info *info,
			struct net_device *egdev, struct netlink_ext_ack *extack)
{
	enum efx_encap_type type = efx_tc_indr_netdev_type(egdev);
	struct efx_tc_encap_action *encap, *old;
	s64 rc;

	if (type == EFX_ENCAP_TYPE_NONE) {
		/* dest is not an encap device */
		NL_SET_ERR_MSG_MOD(extack, "Not a (supported) tunnel device but tunnel_key is set");
		return ERR_PTR(-EOPNOTSUPP);
	}
	rc = efx_mae_check_encap_type_supported(efx, type);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware reports no support for this tunnel type");
		return ERR_PTR(rc);
	}
	/* No support yet for Geneve options */
	if (info->options_len) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported tunnel options");
		return ERR_PTR(-EOPNOTSUPP);
	}
	switch (info->mode) {
	case IP_TUNNEL_INFO_TX:
		break;
	case IP_TUNNEL_INFO_TX | IP_TUNNEL_INFO_IPV6:
		type |= EFX_ENCAP_FLAG_IPV6;
		break;
	default:
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported tunnel mode %u",
				       info->mode);
		return ERR_PTR(-EOPNOTSUPP);
	}
	encap = kzalloc(sizeof(*encap), GFP_KERNEL_ACCOUNT);
	if (!encap)
		return ERR_PTR(-ENOMEM);
	encap->type = type;
	encap->key = info->key;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->encap_ht,
						&encap->linkage,
						efx_tc_encap_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(encap);
		if (!refcount_inc_not_zero(&old->ref))
			return ERR_PTR(-EAGAIN);
		/* existing entry found, ref taken */
		return old;
	}

	/* ref and return */
	refcount_set(&encap->ref, 1);
	return encap;
}

void efx_tc_flower_release_encap_md(struct efx_nic *efx,
				    struct efx_tc_encap_action *encap)
{
	if (!refcount_dec_and_test(&encap->ref))
		return; /* still in use */
	rhashtable_remove_fast(&efx->tc->encap_ht, &encap->linkage,
			       efx_tc_encap_ht_params);
	kfree(encap);
}
