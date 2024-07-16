// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"
#include "nfp_port.h"

int nfp_app_nic_vnic_init_phy_port(struct nfp_pf *pf, struct nfp_app *app,
				   struct nfp_net *nn, unsigned int id)
{
	int err;

	if (!pf->eth_tbl)
		return 0;

	nn->port = nfp_port_alloc(app, NFP_PORT_PHYS_PORT, nn->dp.netdev);
	if (IS_ERR(nn->port))
		return PTR_ERR(nn->port);

	err = nfp_port_init_phy_port(pf, app, nn->port, id);
	if (err) {
		nfp_port_free(nn->port);
		return err;
	}

	return nn->port->type == NFP_PORT_INVALID;
}

int nfp_app_nic_vnic_alloc(struct nfp_app *app, struct nfp_net *nn,
			   unsigned int id)
{
	int err;

	err = nfp_app_nic_vnic_init_phy_port(app->pf, app, nn, id);
	if (err)
		return err < 0 ? err : 0;

	nfp_net_get_mac_addr(app->pf, nn->dp.netdev, nn->port);

	return 0;
}
