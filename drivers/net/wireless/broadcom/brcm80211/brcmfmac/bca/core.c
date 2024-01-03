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

static int brcmf_bca_attach(struct brcmf_pub *drvr)
{
	pr_err("%s: executing\n", __func__);
	return 0;
}

static void brcmf_bca_detach(struct brcmf_pub *drvr)
{
	pr_err("%s: executing\n", __func__);
}

static void brcmf_bca_feat_attach(struct brcmf_if *ifp)
{
	/* SAE support not confirmed so disabling for now */
	ifp->drvr->feat_flags &= ~BIT(BRCMF_FEAT_SAE);
}

const struct brcmf_fwvid_ops brcmf_bca_ops = {
	.attach = brcmf_bca_attach,
	.detach = brcmf_bca_detach,
	.feat_attach = brcmf_bca_feat_attach,
};
