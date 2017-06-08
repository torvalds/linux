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

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"
#include "nfp_port.h"

static int
nfp_app_nic_vnic_init_phy_port(struct nfp_pf *pf, struct nfp_app *app,
			       struct nfp_net *nn, unsigned int id)
{
	if (!pf->eth_tbl)
		return 0;

	nn->port = nfp_port_alloc(app, NFP_PORT_PHYS_PORT, nn->dp.netdev);
	if (IS_ERR(nn->port))
		return PTR_ERR(nn->port);

	nn->port->eth_id = id;
	nn->port->eth_port = nfp_net_find_port(pf->eth_tbl, id);

	/* Check if vNIC has external port associated and cfg is OK */
	if (!nn->port->eth_port) {
		nfp_err(app->cpp,
			"NSP port entries don't match vNICs (no entry for port #%d)\n",
			id);
		nfp_port_free(nn->port);
		return -EINVAL;
	}
	if (nn->port->eth_port->override_changed) {
		nfp_warn(app->cpp,
			 "Config changed for port #%d, reboot required before port will be operational\n",
			 id);
		nn->port->type = NFP_PORT_INVALID;
		return 1;
	}

	return 0;
}

int nfp_app_nic_vnic_init(struct nfp_app *app, struct nfp_net *nn,
			  unsigned int id)
{
	int err;

	err = nfp_app_nic_vnic_init_phy_port(app->pf, app, nn, id);
	if (err)
		return err < 0 ? err : 0;

	nfp_net_get_mac_addr(nn, app->cpp, id);

	return 0;
}
