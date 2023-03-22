/**
 ******************************************************************************
 *
 * @file rwnx_v7.h
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#ifndef _RWNX_V7_H_
#define _RWNX_V7_H_

#include <linux/pci.h>
#include "rwnx_platform.h"

int rwnx_v7_platform_init(struct pci_dev *pci_dev,
                          struct rwnx_plat **rwnx_plat);

#endif /* _RWNX_V7_H_ */
