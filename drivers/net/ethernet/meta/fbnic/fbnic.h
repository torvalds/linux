/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_H_
#define _FBNIC_H_

#include "fbnic_csr.h"

extern char fbnic_driver_name[];

enum fbnic_boards {
	fbnic_board_asic
};

struct fbnic_info {
	unsigned int bar_mask;
};

#endif /* _FBNIC_H_ */
