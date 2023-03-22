/**
 ****************************************************************************************
 *
 * @file rwnx_dini.h
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#ifndef _RWNX_DINI_H_
#define _RWNX_DINI_H_

#include <linux/pci.h>
#include "rwnx_platform.h"

int rwnx_dini_platform_init(struct pci_dev *pci_dev,
                            struct rwnx_plat **rwnx_plat);

#endif /* _RWNX_DINI_H_ */
