// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/module.h>
#include <bus.h>
#include <core.h>
#include <fwvid.h>

#include "vops.h"

static int __init brcmf_bca_init(void)
{
	return brcmf_fwvid_register_vendor(BRCMF_FWVENDOR_BCA, THIS_MODULE,
					   &brcmf_bca_ops);
}

static void __exit brcmf_bca_exit(void)
{
	brcmf_fwvid_unregister_vendor(BRCMF_FWVENDOR_BCA, THIS_MODULE);
}

MODULE_DESCRIPTION("Broadcom FullMAC WLAN driver plugin for Broadcom AP chipsets");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(BRCMFMAC);

module_init(brcmf_bca_init);
module_exit(brcmf_bca_exit);
