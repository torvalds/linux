// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/module.h>
#include <bus.h>
#include <core.h>
#include <fwvid.h>

#include "vops.h"

static int __init brcmf_cyw_init(void)
{
	return brcmf_fwvid_register_vendor(BRCMF_FWVENDOR_CYW, THIS_MODULE,
					   &brcmf_cyw_ops);
}

static void __exit brcmf_cyw_exit(void)
{
	brcmf_fwvid_unregister_vendor(BRCMF_FWVENDOR_CYW, THIS_MODULE);
}

MODULE_DESCRIPTION("Broadcom FullMAC WLAN driver plugin for Cypress/Infineon chipsets");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(BRCMFMAC);

module_init(brcmf_cyw_init);
module_exit(brcmf_cyw_exit);
