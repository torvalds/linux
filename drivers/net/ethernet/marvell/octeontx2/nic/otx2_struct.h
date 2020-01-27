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

enum nix_sqes_e {
	NIX_SQESZ_W16 = 0x0,
	NIX_SQESZ_W8 = 0x1,
};

enum nix_send_ldtype {
	NIX_SEND_LDTYPE_LDD  = 0x0,
	NIX_SEND_LDTYPE_LDT  = 0x1,
	NIX_SEND_LDTYPE_LDWB = 0x2,
};

#endif /* OTX2_STRUCT_H */
