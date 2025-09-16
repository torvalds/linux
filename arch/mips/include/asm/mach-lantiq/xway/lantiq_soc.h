/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#ifndef _LTQ_XWAY_H__
#define _LTQ_XWAY_H__

#ifdef CONFIG_SOC_TYPE_XWAY

#include <lantiq.h>

/* Chip IDs */
#define SOC_ID_DANUBE1		0x129
#define SOC_ID_DANUBE2		0x12B
#define SOC_ID_TWINPASS		0x12D
#define SOC_ID_AMAZON_SE_1	0x152 /* 50601 */
#define SOC_ID_AMAZON_SE_2	0x153 /* 50600 */
#define SOC_ID_ARX188		0x16C
#define SOC_ID_ARX168_1		0x16D
#define SOC_ID_ARX168_2		0x16E
#define SOC_ID_ARX182		0x16F
#define SOC_ID_GRX188		0x170
#define SOC_ID_GRX168		0x171

#define SOC_ID_VRX288		0x1C0 /* v1.1 */
#define SOC_ID_VRX282		0x1C1 /* v1.1 */
#define SOC_ID_VRX268		0x1C2 /* v1.1 */
#define SOC_ID_GRX268		0x1C8 /* v1.1 */
#define SOC_ID_GRX288		0x1C9 /* v1.1 */
#define SOC_ID_VRX288_2		0x00B /* v1.2 */
#define SOC_ID_VRX268_2		0x00C /* v1.2 */
#define SOC_ID_GRX288_2		0x00D /* v1.2 */
#define SOC_ID_GRX282_2		0x00E /* v1.2 */
#define SOC_ID_VRX220		0x000

#define SOC_ID_ARX362		0x004
#define SOC_ID_ARX368		0x005
#define SOC_ID_ARX382		0x007
#define SOC_ID_ARX388		0x008
#define SOC_ID_URX388		0x009
#define SOC_ID_GRX383		0x010
#define SOC_ID_GRX369		0x011
#define SOC_ID_GRX387		0x00F
#define SOC_ID_GRX389		0x012

 /* SoC Types */
#define SOC_TYPE_DANUBE		0x01
#define SOC_TYPE_TWINPASS	0x02
#define SOC_TYPE_AR9		0x03
#define SOC_TYPE_VR9		0x04 /* v1.1 */
#define SOC_TYPE_VR9_2		0x05 /* v1.2 */
#define SOC_TYPE_AMAZON_SE	0x06
#define SOC_TYPE_AR10		0x07
#define SOC_TYPE_GRX390		0x08
#define SOC_TYPE_VRX220		0x09

/* BOOT_SEL - find what boot media we have */
#define BS_EXT_ROM		0x0
#define BS_FLASH		0x1
#define BS_MII0			0x2
#define BS_PCI			0x3
#define BS_UART1		0x4
#define BS_SPI			0x5
#define BS_NAND			0x6
#define BS_RMII0		0x7

/* helpers used to access the cgu */
#define ltq_cgu_w32(x, y)	ltq_w32((x), ltq_cgu_membase + (y))
#define ltq_cgu_r32(x)		ltq_r32(ltq_cgu_membase + (x))
extern __iomem void *ltq_cgu_membase;

/*
 * during early_printk no ioremap is possible
 * let's use KSEG1 instead
 */
#define LTQ_ASC1_BASE_ADDR	0x1E100C00
#define LTQ_EARLY_ASC		KSEG1ADDR(LTQ_ASC1_BASE_ADDR)

/* EBU - external bus unit */
#define LTQ_EBU_BUSCON0		0x0060
#define LTQ_EBU_PCC_CON		0x0090
#define LTQ_EBU_PCC_IEN		0x00A4
#define LTQ_EBU_PCC_ISTAT	0x00A0
#define LTQ_EBU_BUSCON1		0x0064
#define LTQ_EBU_ADDRSEL1	0x0024
#define EBU_WRDIS		0x80000000

/* WDT */
#define LTQ_RST_CAUSE_WDTRST	0x20

/* MPS - multi processor unit (voice) */
#define LTQ_MPS_BASE_ADDR	(KSEG1 + 0x1F107000)
#define LTQ_MPS_CHIPID		((u32 *)(LTQ_MPS_BASE_ADDR + 0x0344))

/* request a non-gpio and set the PIO config */
#define PMU_PPE			 BIT(13)
extern void ltq_pmu_enable(unsigned int module);
extern void ltq_pmu_disable(unsigned int module);

/* VMMC */
extern unsigned int *ltq_get_cp1_base(void);

#endif /* CONFIG_SOC_TYPE_XWAY */
#endif /* _LTQ_XWAY_H__ */
