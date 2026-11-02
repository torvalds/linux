/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */

#ifndef _NBL_DEF_HW_H_
#define _NBL_DEF_HW_H_

#include <linux/types.h>

struct nbl_hw_mgt;
struct nbl_adapter;

int nbl_hw_init_leonis(struct nbl_adapter *adapter);
void nbl_hw_remove_leonis(struct nbl_adapter *adapter);

#endif
