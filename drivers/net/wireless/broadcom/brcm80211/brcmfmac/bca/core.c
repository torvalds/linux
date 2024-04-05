// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <core.h>
#include <bus.h>
#include <fwvid.h>
#include <feature.h>

#include "vops.h"

#define BRCMF_BCA_E_LAST		212

static void brcmf_bca_feat_attach(struct brcmf_if *ifp)
{
	/* SAE support not confirmed so disabling for now */
	ifp->drvr->feat_flags &= ~BIT(BRCMF_FEAT_SAE);
}

static int brcmf_bca_alloc_fweh_info(struct brcmf_pub *drvr)
{
	struct brcmf_fweh_info *fweh;

	fweh = kzalloc(struct_size(fweh, evt_handler, BRCMF_BCA_E_LAST),
		       GFP_KERNEL);
	if (!fweh)
		return -ENOMEM;

	fweh->num_event_codes = BRCMF_BCA_E_LAST;
	drvr->fweh = fweh;
	return 0;
}

const struct brcmf_fwvid_ops brcmf_bca_ops = {
	.feat_attach = brcmf_bca_feat_attach,
	.alloc_fweh_info = brcmf_bca_alloc_fweh_info,
};
