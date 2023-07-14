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

static int brcmf_cyw_attach(struct brcmf_pub *drvr)
{
	pr_err("%s: executing\n", __func__);
	return 0;
}

static void brcmf_cyw_detach(struct brcmf_pub *drvr)
{
	pr_err("%s: executing\n", __func__);
}

const struct brcmf_fwvid_ops brcmf_cyw_ops = {
	.attach = brcmf_cyw_attach,
	.detach = brcmf_cyw_detach,
};
