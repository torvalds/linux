// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <net/devlink.h>

#include "nfpcore/nfp_nsp.h"
#include "nfp_main.h"

static const struct devlink_param nfp_devlink_params[] = {
};

static int nfp_devlink_supports_params(struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	bool supported;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		dev_err(&pf->pdev->dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	supported = nfp_nsp_has_hwinfo_lookup(nsp) &&
		    nfp_nsp_has_hwinfo_set(nsp);

	nfp_nsp_close(nsp);
	return supported;
}

int nfp_devlink_params_register(struct nfp_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	int err;

	err = nfp_devlink_supports_params(pf);
	if (err <= 0)
		return err;

	err = devlink_params_register(devlink, nfp_devlink_params,
				      ARRAY_SIZE(nfp_devlink_params));
	if (err)
		return err;

	devlink_params_publish(devlink);
	return 0;
}

void nfp_devlink_params_unregister(struct nfp_pf *pf)
{
	int err;

	err = nfp_devlink_supports_params(pf);
	if (err <= 0)
		return;

	devlink_params_unregister(priv_to_devlink(pf), nfp_devlink_params,
				  ARRAY_SIZE(nfp_devlink_params));
}
