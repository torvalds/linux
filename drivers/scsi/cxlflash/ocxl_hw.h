/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *	       Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* OCXL hardware AFU associated with the host */
struct ocxl_hw_afu {
	struct pci_dev *pdev;		/* PCI device */
	struct device *dev;		/* Generic device */

	struct ocxl_fn_config fcfg;	/* DVSEC config of the function */
	struct ocxl_afu_config acfg;	/* AFU configuration data */

	int fn_actag_base;		/* Function acTag base */
	int fn_actag_enabled;		/* Function acTag number enabled */

	bool is_present;		/* Function has AFUs defined */
};
