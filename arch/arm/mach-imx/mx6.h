/*
 * Copyright 2004-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 *  * This program is free software; you can redistribute it and/or modify
 *   * it under the terms of the GNU General Public License version 2 as
 *    * published by the Free Software Foundation.
 *     */

#ifndef __ASM_ARCH_MXC_IOMAP_H__
#define __ASM_ARCH_MXC_IOMAP_H__

#define MX6Q_IO_P2V(x)                  IMX_IO_P2V(x)
#define MX6Q_IO_ADDRESS(x)              IOMEM(MX6Q_IO_P2V(x))

#define MX6Q_L2_BASE_ADDR       0x00a02000
#define MX6Q_L2_SIZE            0x1000
#define MX6Q_IOMUXC_BASE_ADDR       0x020e0000
#define MX6Q_IOMUXC_SIZE        0x4000
#define MX6Q_SRC_BASE_ADDR      0x020d8000
#define MX6Q_SRC_SIZE           0x4000
#define MX6Q_CCM_BASE_ADDR      0x020c4000
#define MX6Q_CCM_SIZE           0x4000
#define MX6Q_ANATOP_BASE_ADDR       0x020c8000
#define MX6Q_ANATOP_SIZE        0x1000
#define MX6Q_GPC_BASE_ADDR      0x020dc000
#define MX6Q_GPC_SIZE           0x4000
#define MX6Q_SEMA4_BASE_ADDR	0x02290000
#define MX6Q_SEMA4_SIZE		0x4000
#define MX6Q_MMDC_P0_BASE_ADDR  0x021b0000
#define MX6Q_MMDC_P0_SIZE       0x4000
#define MX6Q_MMDC_P1_BASE_ADDR  0x021b4000
#define MX6Q_MMDC_P1_SIZE       0x4000
#define MX6Q_AIPS1_BASE_ADDR        0x02000000
#define MX6Q_AIPS1_SIZE     0x100000
#define MX6Q_AIPS2_BASE_ADDR        0x02100000
#define MX6Q_AIPS2_SIZE     0x100000
#define MX6Q_AIPS3_BASE_ADDR	0x02200000
#define MX6Q_AIPS3_SIZE		0x100000

#define MX6SX_IRAM_TLB_BASE_ADDR    0x008f8000
#define MX6Q_IRAM_TLB_BASE_ADDR 0x00900000
#define MX6Q_IRAM_TLB_SIZE      0x4000
#define TT_ATTRIB_NON_CACHEABLE_1M  0x802
#define MX6_SUSPEND_IRAM_DATA_SIZE  256
#define MX6SL_WFI_IRAM_DATA_SIZE    100

#define MX6_SUSPEND_IRAM_ADDR_OFFSET        0
#define MX6_CPUIDLE_IRAM_ADDR_OFFSET        0x1000
#endif
