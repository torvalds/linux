/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_AUX_H_
#define _IONIC_AUX_H_

int ionic_auxbus_register(struct ionic_lif *lif);
void ionic_auxbus_unregister(struct ionic_lif *lif);

#endif /* _IONIC_AUX_H_ */
