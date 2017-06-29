/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/skbuff.h>
#include <net/devlink.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"
#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_port.h"

/**
 * nfp_flower_add_offload() - Adds a new flow to hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure.
 *
 * Adds a new flow to the repeated hash structure and action payload.
 *
 * Return: negative value on error, 0 if configured successfully.
 */
static int
nfp_flower_add_offload(struct nfp_app *app, struct net_device *netdev,
		       struct tc_cls_flower_offload *flow)
{
	return -EOPNOTSUPP;
}

/**
 * nfp_flower_del_offload() - Removes a flow from hardware.
 * @app:	Pointer to the APP handle
 * @netdev:	netdev structure.
 * @flow:	TC flower classifier offload structure
 *
 * Removes a flow from the repeated hash structure and clears the
 * action payload.
 *
 * Return: negative value on error, 0 if removed successfully.
 */
static int
nfp_flower_del_offload(struct nfp_app *app, struct net_device *netdev,
		       struct tc_cls_flower_offload *flow)
{
	return -EOPNOTSUPP;
}

/**
 * nfp_flower_get_stats() - Populates flow stats obtained from hardware.
 * @app:	Pointer to the APP handle
 * @flow:	TC flower classifier offload structure
 *
 * Populates a flow statistics structure which which corresponds to a
 * specific flow.
 *
 * Return: negative value on error, 0 if stats populated successfully.
 */
static int
nfp_flower_get_stats(struct nfp_app *app, struct tc_cls_flower_offload *flow)
{
	return -EOPNOTSUPP;
}

static int
nfp_flower_repr_offload(struct nfp_app *app, struct net_device *netdev,
			struct tc_cls_flower_offload *flower)
{
	switch (flower->command) {
	case TC_CLSFLOWER_REPLACE:
		return nfp_flower_add_offload(app, netdev, flower);
	case TC_CLSFLOWER_DESTROY:
		return nfp_flower_del_offload(app, netdev, flower);
	case TC_CLSFLOWER_STATS:
		return nfp_flower_get_stats(app, flower);
	}

	return -EOPNOTSUPP;
}

int nfp_flower_setup_tc(struct nfp_app *app, struct net_device *netdev,
			u32 handle, __be16 proto, struct tc_to_netdev *tc)
{
	if (TC_H_MAJ(handle) != TC_H_MAJ(TC_H_INGRESS))
		return -EOPNOTSUPP;

	if (!eth_proto_is_802_3(proto))
		return -EOPNOTSUPP;

	if (tc->type != TC_SETUP_CLSFLOWER)
		return -EINVAL;

	return nfp_flower_repr_offload(app, netdev, tc->cls_flower);
}
