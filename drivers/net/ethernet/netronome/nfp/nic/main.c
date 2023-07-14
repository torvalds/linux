// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017 Netronome Systems, Inc. */

#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "main.h"

static int nfp_nic_init(struct nfp_app *app)
{
	struct nfp_pf *pf = app->pf;

	if (pf->eth_tbl && pf->max_data_vnics != pf->eth_tbl->count) {
		nfp_err(pf->cpp, "ETH entries don't match vNICs (%d vs %d)\n",
			pf->max_data_vnics, pf->eth_tbl->count);
		return -EINVAL;
	}

	return 0;
}

static int nfp_nic_sriov_enable(struct nfp_app *app, int num_vfs)
{
	return 0;
}

static void nfp_nic_sriov_disable(struct nfp_app *app)
{
}

static int nfp_nic_vnic_init(struct nfp_app *app, struct nfp_net *nn)
{
	return nfp_nic_dcb_init(nn);
}

static void nfp_nic_vnic_clean(struct nfp_app *app, struct nfp_net *nn)
{
	nfp_nic_dcb_clean(nn);
}

static int nfp_nic_vnic_alloc(struct nfp_app *app, struct nfp_net *nn,
			      unsigned int id)
{
	struct nfp_app_nic_private *app_pri = nn->app_priv;
	int err;

	err = nfp_app_nic_vnic_alloc(app, nn, id);
	if (err)
		return err;

	if (sizeof(*app_pri)) {
		nn->app_priv = kzalloc(sizeof(*app_pri), GFP_KERNEL);
		if (!nn->app_priv)
			return -ENOMEM;
	}

	return 0;
}

static void nfp_nic_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	kfree(nn->app_priv);
}

const struct nfp_app_type app_nic = {
	.id		= NFP_APP_CORE_NIC,
	.name		= "nic",

	.init		= nfp_nic_init,
	.vnic_alloc	= nfp_nic_vnic_alloc,
	.vnic_free	= nfp_nic_vnic_free,
	.sriov_enable	= nfp_nic_sriov_enable,
	.sriov_disable	= nfp_nic_sriov_disable,

	.vnic_init      = nfp_nic_vnic_init,
	.vnic_clean     = nfp_nic_vnic_clean,
};
