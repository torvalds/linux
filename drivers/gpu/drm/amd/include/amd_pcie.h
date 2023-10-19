/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef __AMD_PCIE_H__
#define __AMD_PCIE_H__

/* Following flags shows PCIe link speed supported in driver which are decided by chipset and ASIC */
#define CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1        0x00010000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2        0x00020000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3        0x00040000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4        0x00080000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_GEN5        0x00100000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_MASK        0xFFFF0000
#define CAIL_PCIE_LINK_SPEED_SUPPORT_SHIFT       16

/* Following flags shows PCIe link speed supported by ASIC H/W.*/
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1   0x00000001
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2   0x00000002
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3   0x00000004
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN4   0x00000008
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN5   0x00000010
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_MASK   0x0000FFFF
#define CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_SHIFT  0

/* gen: chipset 1/2, asic 1/2/3 */
#define AMDGPU_DEFAULT_PCIE_GEN_MASK (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1 \
				      | CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 \
				      | CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1 \
				      | CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2 \
				      | CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3)

/* Following flags shows PCIe lane width switch supported in driver which are decided by chipset and ASIC */
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X1          0x00010000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X2          0x00020000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X4          0x00040000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X8          0x00080000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X12         0x00100000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X16         0x00200000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_X32         0x00400000
#define CAIL_PCIE_LINK_WIDTH_SUPPORT_SHIFT       16

/* 1/2/4/8/16 lanes */
#define AMDGPU_DEFAULT_PCIE_MLW_MASK (CAIL_PCIE_LINK_WIDTH_SUPPORT_X1 \
				      | CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 \
				      | CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 \
				      | CAIL_PCIE_LINK_WIDTH_SUPPORT_X8 \
				      | CAIL_PCIE_LINK_WIDTH_SUPPORT_X16)

#endif
