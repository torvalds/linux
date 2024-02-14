/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef INCLUDE_MMU_V1_0_H_
#define INCLUDE_MMU_V1_0_H_

#define MMU_V1_0_HOP0_MASK		0x3000000000000ull
#define MMU_V1_0_HOP1_MASK		0x0FF8000000000ull
#define MMU_V1_0_HOP2_MASK		0x0007FC0000000ull
#define MMU_V1_0_HOP3_MASK		0x000003FE00000ull
#define MMU_V1_0_HOP4_MASK		0x00000001FF000ull

#define MMU_V1_0_HOP0_SHIFT		48
#define MMU_V1_0_HOP1_SHIFT		39
#define MMU_V1_0_HOP2_SHIFT		30
#define MMU_V1_0_HOP3_SHIFT		21
#define MMU_V1_0_HOP4_SHIFT		12

#define MMU_HOP0_PA43_12		0x490004
#define MMU_HOP0_PA49_44		0x490008
#define MMU_ASID_BUSY			0x490000

#endif /* INCLUDE_MMU_V1_0_H_ */
