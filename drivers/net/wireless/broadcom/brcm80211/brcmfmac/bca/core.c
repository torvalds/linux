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

static void brcmf_bca_feat_attach(struct brcmf_if *ifp)
{
	/* SAE support not confirmed so disabling for now */
	ifp->drvr->feat_flags &= ~BIT(BRCMF_FEAT_SAE);
}

const struct brcmf_fwvid_ops brcmf_bca_ops = {
	.feat_attach = brcmf_bca_feat_attach,
};
