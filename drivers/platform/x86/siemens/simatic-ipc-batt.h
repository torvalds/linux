/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Siemens SIMATIC IPC driver for CMOS battery monitoring
 *
 * Copyright (c) Siemens AG, 2023
 *
 * Author:
 *  Henning Schild <henning.schild@siemens.com>
 */

#ifndef _SIMATIC_IPC_BATT_H
#define _SIMATIC_IPC_BATT_H

int simatic_ipc_batt_probe(struct platform_device *pdev,
			   struct gpiod_lookup_table *table);

void simatic_ipc_batt_remove(struct platform_device *pdev,
			     struct gpiod_lookup_table *table);

#endif /* _SIMATIC_IPC_BATT_H */
