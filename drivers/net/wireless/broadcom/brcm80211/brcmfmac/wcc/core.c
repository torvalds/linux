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

static int brcmf_wcc_attach(struct brcmf_pub *drvr)
{
	pr_debug("%s: executing\n", __func__);
	return 0;
}

static void brcmf_wcc_detach(struct brcmf_pub *drvr)
{
	pr_debug("%s: executing\n", __func__);
}

const struct brcmf_fwvid_ops brcmf_wcc_ops = {
	.attach = brcmf_wcc_attach,
	.detach = brcmf_wcc_detach,
};
