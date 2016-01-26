/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 *  * This program is free software; you can redistribute it and/or modify
 *   * it under the terms of the GNU General Public License version 2 as
 *    * published by the Free Software Foundation.
 *     */

#ifndef __ASM_ARCH_MX7_IOMAP_H__
#define __ASM_ARCH_MX7_IOMAP_H__

#define MX7D_IO_P2V(x)                  IMX_IO_P2V(x)
#define MX7D_IO_ADDRESS(x)              IOMEM(MX7D_IO_P2V(x))

#define MX7D_LPSR_BASE_ADDR             0x30270000
#define MX7D_LPSR_SIZE                  0x10000
#define MX7D_CCM_BASE_ADDR              0x30380000
#define MX7D_CCM_SIZE                   0x10000
#define MX7D_IOMUXC_BASE_ADDR           0x30330000
#define MX7D_IOMUXC_SIZE                0x10000
#define MX7D_IOMUXC_GPR_BASE_ADDR       0x30340000
#define MX7D_IOMUXC_GPR_SIZE            0x10000
#define MX7D_ANATOP_BASE_ADDR           0x30360000
#define MX7D_ANATOP_SIZE                0x10000
#define MX7D_SNVS_BASE_ADDR		0x30370000
#define MX7D_SNVS_SIZE			0x10000
#define MX7D_GPC_BASE_ADDR              0x303a0000
#define MX7D_GPC_SIZE                   0x10000
#define MX7D_SRC_BASE_ADDR              0x30390000
#define MX7D_SRC_SIZE                   0x10000
#define MX7D_DDRC_BASE_ADDR             0x307a0000
#define MX7D_DDRC_SIZE                  0x10000
#define MX7D_DDRC_PHY_BASE_ADDR         0x30790000
#define MX7D_DDRC_PHY_SIZE              0x10000
#define MX7D_AIPS1_BASE_ADDR            0x30000000
#define MX7D_AIPS1_SIZE                 0x400000
#define MX7D_AIPS2_BASE_ADDR            0x30400000
#define MX7D_AIPS2_SIZE                 0x400000
#define MX7D_AIPS3_BASE_ADDR            0x30900000
#define MX7D_AIPS3_SIZE                 0x300000
#define MX7D_GIC_BASE_ADDR              0x31000000
#define MX7D_GIC_SIZE                   0x100000

#define TT_ATTRIB_NON_CACHEABLE_1M	0x802
#define MX7_IRAM_TLB_SIZE		0x4000
#define MX7_SUSPEND_OCRAM_SIZE		0x1000
#define MX7_CPUIDLE_OCRAM_ADDR_OFFSET	0x1000
#define MX7_CPUIDLE_OCRAM_SIZE		0x1000
#define MX7_BUSFREQ_OCRAM_ADDR_OFFSET	0x2000
#define MX7_BUSFREQ_OCRAM_SIZE		0x1000

#endif
