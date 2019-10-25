// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/mmu.h>
#include "ocxl_internal.h"

static int __init init_ocxl(void)
{
	int rc = 0;

	if (!tlbie_capable)
		return -EINVAL;

	rc = ocxl_file_init();
	if (rc)
		return rc;

	rc = pci_register_driver(&ocxl_pci_driver);
	if (rc) {
		ocxl_file_exit();
		return rc;
	}
	return 0;
}

static void exit_ocxl(void)
{
	pci_unregister_driver(&ocxl_pci_driver);
	ocxl_file_exit();
}

module_init(init_ocxl);
module_exit(exit_ocxl);

MODULE_DESCRIPTION("Open Coherent Accelerator");
MODULE_LICENSE("GPL");
