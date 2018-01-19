/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_b0.h: Declaration of abstract interface for Atlantic hardware
 * specific functions.
 */

#ifndef HW_ATL_B0_H
#define HW_ATL_B0_H

#include "../aq_common.h"

const struct aq_hw_ops *hw_atl_b0_get_ops_by_id(struct pci_dev *pdev);

#endif /* HW_ATL_B0_H */
