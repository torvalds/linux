/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_main.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Alejandro Lucero <alejandro.lucero@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/vermagic.h>

#include "nfp_main.h"
#include "nfp_net.h"

static const char nfp_driver_name[] = "nfp";
const char nfp_driver_version[] = VERMAGIC_STRING;

static int __init nfp_main_init(void)
{
	int err;

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2017 Netronome Systems\n",
		nfp_driver_name);

	nfp_net_debugfs_create();

	err = pci_register_driver(&nfp_netvf_pci_driver);
	if (err)
		goto err_destroy_debugfs;

	return err;

err_destroy_debugfs:
	nfp_net_debugfs_destroy();
	return err;
}

static void __exit nfp_main_exit(void)
{
	pci_unregister_driver(&nfp_netvf_pci_driver);
	nfp_net_debugfs_destroy();
}

module_init(nfp_main_init);
module_exit(nfp_main_exit);

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Netronome Flow Processor (NFP) driver.");
