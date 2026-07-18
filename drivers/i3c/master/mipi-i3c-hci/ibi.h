/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Common IBI related stuff
 */

#ifndef IBI_H
#define IBI_H

/*
 * IBI Status Descriptor bits
 */
#define IBI_STS				BIT(31)
#define IBI_ERROR			BIT(30)
#define IBI_STATUS_TYPE			BIT(29)
#define IBI_HW_CONTEXT			GENMASK(28, 26)
#define IBI_TS				BIT(25)
#define IBI_LAST_STATUS			BIT(24)
#define IBI_CHUNKS			GENMASK(23, 16)
#define IBI_ID				GENMASK(15, 8)
#define IBI_TARGET_ADDR			GENMASK(15, 9)
#define IBI_TARGET_RNW			BIT(8)
#define IBI_DATA_LENGTH			GENMASK(7, 0)

/*  handy helpers */
struct i3c_dev_desc *i3c_hci_addr_to_dev(struct i3c_hci *hci, unsigned int addr);

#endif
