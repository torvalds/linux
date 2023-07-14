// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <core.h>
#include <bus.h>
#include <fwvid.h>

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

const struct brcmf_fwvid_ops brcmf_bca_ops = {
	.attach = brcmf_bca_attach,
	.detach = brcmf_bca_detach,
};
