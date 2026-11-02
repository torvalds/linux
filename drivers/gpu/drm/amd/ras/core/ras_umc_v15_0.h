/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __RAS_UMC_V15_0_H__
#define __RAS_UMC_V15_0_H__
#include "ras.h"

/* MCA_UMC_UMC0_MCUMC_ADDRT0 */
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr__SHIFT                0x0
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved__SHIFT                 0x38
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr_MASK                  0x00FFFFFFFFFFFFFFL
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved_MASK                   0xFF00000000000000L

/* MCMP1_IPIDT0 */
#define MCMP1_IPIDT0__InstanceIdLo__SHIFT                          0x0
#define MCMP1_IPIDT0__HardwareID__SHIFT                            0x20
#define MCMP1_IPIDT0__InstanceIdHi__SHIFT                          0x2c
#define MCMP1_IPIDT0__McaType__SHIFT                               0x30

#define MCMP1_IPIDT0__InstanceIdLo_MASK                            0x00000000FFFFFFFFL
#define MCMP1_IPIDT0__HardwareID_MASK                              0x00000FFF00000000L
#define MCMP1_IPIDT0__InstanceIdHi_MASK                            0x0000F00000000000L
#define MCMP1_IPIDT0__McaType_MASK                                 0xFFFF000000000000L

#define ACA_IPID_HI_2_UMC_AID(_ipid_hi) (((_ipid_hi) >> 2) & 0x3)
#define ACA_IPID_LO_2_UMC_CH(_ipid_lo)  \
	(((((_ipid_lo) >> 20) & 0x1) * 4) + (((_ipid_lo) >> 12) & 0xF))
#define ACA_IPID_LO_2_UMC_INST(_ipid_lo) (((_ipid_lo) >> 21) & 0x7)

#define ACA_IPID_2_DIE_ID(ipid)  ((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) >> 2) & 0x03)
#define ACA_IPID_2_UMC_CH(ipid) \
	(ACA_IPID_LO_2_UMC_CH(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define ACA_IPID_2_UMC_INST(ipid) \
	(ACA_IPID_LO_2_UMC_INST(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define ACA_IPID_2_SOCKET_ID(ipid) \
	(((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo) & 0x1) << 2) | \
	 (REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) & 0x03))

#define ACA_ADDR_2_ERR_ADDR(addr) \
	REG_GET_FIELD(addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr)

/* invalid node instance value */
#define UMC_INV_AID_NODE 0xffff

extern const struct ras_umc_ip_func ras_umc_func_v15_0;

#endif
