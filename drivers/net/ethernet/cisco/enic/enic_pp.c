/*
 * Copyright 2011 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/ip.h>

#include "vnic_vic.h"
#include "enic_res.h"
#include "enic.h"
#include "enic_dev.h"
#include "enic_pp.h"

/*
 * Checks validity of vf index that came in
 * port profile request
 */
int enic_is_valid_pp_vf(struct enic *enic, int vf, int *err)
{
	if (vf != PORT_SELF_VF) {
#ifdef CONFIG_PCI_IOV
		if (enic_sriov_enabled(enic)) {
			if (vf < 0 || vf >= enic->num_vfs) {
				*err = -EINVAL;
				goto err_out;
			}
		} else {
			*err = -EOPNOTSUPP;
			goto err_out;
		}
#else
		*err = -EOPNOTSUPP;
		goto err_out;
#endif
	}

	if (vf == PORT_SELF_VF && !enic_is_dynamic(enic)) {
		*err = -EOPNOTSUPP;
		goto err_out;
	}

	*err = 0;
	return 1;

err_out:
	return 0;
}

static int enic_set_port_profile(struct enic *enic, int vf)
{
	struct net_device *netdev = enic->netdev;
	struct enic_port_profile *pp;
	struct vic_provinfo *vp;
	const u8 oui[3] = VIC_PROVINFO_CISCO_OUI;
	const u16 os_type = htons(VIC_GENERIC_PROV_OS_TYPE_LINUX);
	char uuid_str[38];
	char client_mac_str[18];
	u8 *client_mac;
	int err;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (!(pp->set & ENIC_SET_NAME) || !strlen(pp->name))
		return -EINVAL;

	vp = vic_provinfo_alloc(GFP_KERNEL, oui,
		VIC_PROVINFO_GENERIC_TYPE);
	if (!vp)
		return -ENOMEM;

	VIC_PROVINFO_ADD_TLV(vp,
		VIC_GENERIC_PROV_TLV_PORT_PROFILE_NAME_STR,
		strlen(pp->name) + 1, pp->name);

	if (!is_zero_ether_addr(pp->mac_addr)) {
		client_mac = pp->mac_addr;
	} else if (vf == PORT_SELF_VF) {
		client_mac = netdev->dev_addr;
	} else {
		netdev_err(netdev, "Cannot find pp mac address "
			"for VF %d\n", vf);
		err = -EINVAL;
		goto add_tlv_failure;
	}

	VIC_PROVINFO_ADD_TLV(vp,
		VIC_GENERIC_PROV_TLV_CLIENT_MAC_ADDR,
		ETH_ALEN, client_mac);

	snprintf(client_mac_str, sizeof(client_mac_str), "%pM", client_mac);
	VIC_PROVINFO_ADD_TLV(vp,
		VIC_GENERIC_PROV_TLV_CLUSTER_PORT_UUID_STR,
		sizeof(client_mac_str), client_mac_str);

	if (pp->set & ENIC_SET_INSTANCE) {
		sprintf(uuid_str, "%pUB", pp->instance_uuid);
		VIC_PROVINFO_ADD_TLV(vp,
			VIC_GENERIC_PROV_TLV_CLIENT_UUID_STR,
			sizeof(uuid_str), uuid_str);
	}

	if (pp->set & ENIC_SET_HOST) {
		sprintf(uuid_str, "%pUB", pp->host_uuid);
		VIC_PROVINFO_ADD_TLV(vp,
			VIC_GENERIC_PROV_TLV_HOST_UUID_STR,
			sizeof(uuid_str), uuid_str);
	}

	VIC_PROVINFO_ADD_TLV(vp,
		VIC_GENERIC_PROV_TLV_OS_TYPE,
		sizeof(os_type), &os_type);

	ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_init_prov2, (u8 *)vp,
		vic_provinfo_size(vp));
	err = enic_dev_status_to_errno(err);

add_tlv_failure:
	vic_provinfo_free(vp);

	return err;
}

static int enic_unset_port_profile(struct enic *enic, int vf)
{
	int err;

	ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_deinit);
	if (err)
		return enic_dev_status_to_errno(err);

	if (vf == PORT_SELF_VF)
		enic_reset_addr_lists(enic);

	return 0;
}

static int enic_are_pp_different(struct enic_port_profile *pp1,
		struct enic_port_profile *pp2)
{
	return strcmp(pp1->name, pp2->name) | !!memcmp(pp1->instance_uuid,
		pp2->instance_uuid, PORT_UUID_MAX) |
		!!memcmp(pp1->host_uuid, pp2->host_uuid, PORT_UUID_MAX) |
		!!memcmp(pp1->mac_addr, pp2->mac_addr, ETH_ALEN);
}

static int enic_pp_preassociate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);
static int enic_pp_disassociate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);
static int enic_pp_preassociate_rr(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);
static int enic_pp_associate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);

static int (*enic_pp_handlers[])(struct enic *enic, int vf,
		struct enic_port_profile *prev_state,
		int *restore_pp) = {
	[PORT_REQUEST_PREASSOCIATE]	= enic_pp_preassociate,
	[PORT_REQUEST_PREASSOCIATE_RR]	= enic_pp_preassociate_rr,
	[PORT_REQUEST_ASSOCIATE]	= enic_pp_associate,
	[PORT_REQUEST_DISASSOCIATE]	= enic_pp_disassociate,
};

static const int enic_pp_handlers_count =
			sizeof(enic_pp_handlers)/sizeof(*enic_pp_handlers);

static int enic_pp_preassociate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp)
{
	return -EOPNOTSUPP;
}

static int enic_pp_disassociate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp)
{
	struct net_device *netdev = enic->netdev;
	struct enic_port_profile *pp;
	int err;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	/* Deregister mac addresses */
	if (!is_zero_ether_addr(pp->mac_addr))
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_del_addr,
			pp->mac_addr);
	else if (!is_zero_ether_addr(netdev->dev_addr))
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_del_addr,
			netdev->dev_addr);

	return enic_unset_port_profile(enic, vf);
}

static int enic_pp_preassociate_rr(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp)
{
	struct enic_port_profile *pp;
	int err;
	int active = 0;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (pp->request != PORT_REQUEST_ASSOCIATE) {
		/* If pre-associate is not part of an associate.
		We always disassociate first */
		err = enic_pp_handlers[PORT_REQUEST_DISASSOCIATE](enic, vf,
			prev_pp, restore_pp);
		if (err)
			return err;

		*restore_pp = 0;
	}

	*restore_pp = 0;

	err = enic_set_port_profile(enic, vf);
	if (err)
		return err;

	/* If pre-associate is not part of an associate. */
	if (pp->request != PORT_REQUEST_ASSOCIATE) {
		/* Enable device as standby */
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_enable2,
			active);
		err = enic_dev_status_to_errno(err);
	}

	return err;
}

static int enic_pp_associate(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp)
{
	struct net_device *netdev = enic->netdev;
	struct enic_port_profile *pp;
	int err;
	int active = 1;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	/* Check if a pre-associate was called before */
	if (prev_pp->request != PORT_REQUEST_PREASSOCIATE_RR ||
		(prev_pp->request == PORT_REQUEST_PREASSOCIATE_RR &&
			enic_are_pp_different(prev_pp, pp))) {
		err = enic_pp_handlers[PORT_REQUEST_DISASSOCIATE](
			enic, vf, prev_pp, restore_pp);
		if (err)
			return err;

		*restore_pp = 0;
	}

	err = enic_pp_handlers[PORT_REQUEST_PREASSOCIATE_RR](
			enic, vf, prev_pp, restore_pp);
	if (err)
		return err;

	*restore_pp = 0;

	/* Enable device as active */
	ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_enable2, active);
	err = enic_dev_status_to_errno(err);
	if (err)
		return err;

	/* Register mac address */
	if (!is_zero_ether_addr(pp->mac_addr))
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_add_addr,
			pp->mac_addr);
	else if (!is_zero_ether_addr(netdev->dev_addr))
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnic_dev_add_addr,
			netdev->dev_addr);

	return 0;
}

int enic_process_set_pp_request(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp)
{
	struct enic_port_profile *pp;
	int err;

	ENIC_PP_BY_INDEX(enic, vf, pp, &err);
	if (err)
		return err;

	if (pp->request >= enic_pp_handlers_count
		|| !enic_pp_handlers[pp->request])
		return -EOPNOTSUPP;

	return enic_pp_handlers[pp->request](enic, vf, prev_pp, restore_pp);
}

int enic_process_get_pp_request(struct enic *enic, int vf,
	int request, u16 *response)
{
	int err, status = ERR_SUCCESS;

	switch (request) {

	case PORT_REQUEST_PREASSOCIATE_RR:
	case PORT_REQUEST_ASSOCIATE:
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic,
			vnic_dev_enable2_done, &status);
		break;

	case PORT_REQUEST_DISASSOCIATE:
		ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic,
			vnic_dev_deinit_done, &status);
		break;

	default:
		return -EINVAL;
	}

	if (err)
		status = err;

	switch (status) {
	case ERR_SUCCESS:
		*response = PORT_PROFILE_RESPONSE_SUCCESS;
		break;
	case ERR_EINVAL:
		*response = PORT_PROFILE_RESPONSE_INVALID;
		break;
	case ERR_EBADSTATE:
		*response = PORT_PROFILE_RESPONSE_BADSTATE;
		break;
	case ERR_ENOMEM:
		*response = PORT_PROFILE_RESPONSE_INSUFFICIENT_RESOURCES;
		break;
	case ERR_EINPROGRESS:
		*response = PORT_PROFILE_RESPONSE_INPROGRESS;
		break;
	default:
		*response = PORT_PROFILE_RESPONSE_ERROR;
		break;
	}

	return 0;
}
