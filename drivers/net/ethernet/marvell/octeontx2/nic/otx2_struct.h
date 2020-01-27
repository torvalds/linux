/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OTX2_STRUCT_H
#define OTX2_STRUCT_H

/* NIX WQE/CQE size 128 byte or 512 byte */
enum nix_cqesz_e {
	NIX_XQESZ_W64 = 0x0,
	NIX_XQESZ_W16 = 0x1,
};

#endif /* OTX2_STRUCT_H */
