/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_API_H_
#define _IONIC_API_H_

#include <linux/auxiliary_bus.h>

/**
 * struct ionic_aux_dev - Auxiliary device information
 * @lif:        Logical interface
 * @idx:        Index identifier
 * @adev:       Auxiliary device
 */
struct ionic_aux_dev {
	struct ionic_lif *lif;
	int idx;
	struct auxiliary_device adev;
};

#endif /* _IONIC_API_H_ */
