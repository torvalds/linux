/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
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

 /* SoC Types */
#define SOC_TYPE_DANUBE		0x01
#define SOC_TYPE_TWINPASS	0x02
#define SOC_TYPE_AR9		0x03
#define SOC_TYPE_VR9		0x04 /* v1.1 */
#define SOC_TYPE_VR9_2		0x05 /* v1.2 */
#define SOC_TYPE_AMAZON_SE	0x06

/* ASC0/1 - serial port */
#define LTQ_ASC0_BASE_ADDR	0x1E100400
#define LTQ_ASC1_BASE_ADDR	0x1E100C00
#define LTQ_ASC_SIZE		0x400

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
 * lets use KSEG1 instead
 */
#define LTQ_EARLY_ASC		KSEG1ADDR(LTQ_ASC1_BASE_ADDR)

/* RCU - reset control unit */
#define LTQ_RCU_BASE_ADDR	0x1F203000
#define LTQ_RCU_SIZE		0x1000

/* GPTU - general purpose timer unit */
#define LTQ_GPTU_BASE_ADDR	0x18000300
#define LTQ_GPTU_SIZE		0x100

/* EBU - external bus unit */
#define LTQ_EBU_GPIO_START	0x14000000
#define LTQ_EBU_GPIO_SIZE	0x1000

#define LTQ_EBU_BASE_ADDR	0x1E105300
#define LTQ_EBU_SIZE		0x100

#define LTQ_EBU_BUSCON0		0x0060
#define LTQ_EBU_PCC_CON		0x0090
#define LTQ_EBU_PCC_IEN		0x00A4
#define LTQ_EBU_PCC_ISTAT	0x00A0
#define LTQ_EBU_BUSCON1		0x0064
#define LTQ_EBU_ADDRSEL1	0x0024
#define EBU_WRDIS		0x80000000

/* CGU - clock generation unit */
#define LTQ_CGU_BASE_ADDR	0x1F103000
#define LTQ_CGU_SIZE		0x1000

/* ICU - interrupt control unit */
#define LTQ_ICU_BASE_ADDR	0x1F880200
#define LTQ_ICU_SIZE		0x100

/* EIU - external interrupt unit */
#define LTQ_EIU_BASE_ADDR	0x1F101000
#define LTQ_EIU_SIZE		0x1000

/* PMU - power management unit */
#define LTQ_PMU_BASE_ADDR	0x1F102000
#define LTQ_PMU_SIZE		0x1000

#define PMU_DMA			0x0020
#define PMU_USB			0x8041
#define PMU_LED			0x0800
#define PMU_GPT			0x1000
#define PMU_PPE			0x2000
#define PMU_FPI			0x4000
#define PMU_SWITCH		0x10000000

/* ETOP - ethernet */
#define LTQ_ETOP_BASE_ADDR	0x1E180000
#define LTQ_ETOP_SIZE		0x40000

/* DMA */
#define LTQ_DMA_BASE_ADDR	0x1E104100
#define LTQ_DMA_SIZE		0x800

/* PCI */
#define PCI_CR_BASE_ADDR	0x1E105400
#define PCI_CR_SIZE		0x400

/* WDT */
#define LTQ_WDT_BASE_ADDR	0x1F8803F0
#define LTQ_WDT_SIZE		0x10

/* STP - serial to parallel conversion unit */
#define LTQ_STP_BASE_ADDR	0x1E100BB0
#define LTQ_STP_SIZE		0x40

/* GPIO */
#define LTQ_GPIO0_BASE_ADDR	0x1E100B10
#define LTQ_GPIO1_BASE_ADDR	0x1E100B40
#define LTQ_GPIO2_BASE_ADDR	0x1E100B70
#define LTQ_GPIO_SIZE		0x30

/* SSC */
#define LTQ_SSC_BASE_ADDR	0x1e100800
#define LTQ_SSC_SIZE		0x100

/* MEI - dsl core */
#define LTQ_MEI_BASE_ADDR	0x1E116000

/* DEU - data encryption unit */
#define LTQ_DEU_BASE_ADDR	0x1E103100

/* MPS - multi processor unit (voice) */
#define LTQ_MPS_BASE_ADDR	(KSEG1 + 0x1F107000)
#define LTQ_MPS_CHIPID		((u32 *)(LTQ_MPS_BASE_ADDR + 0x0344))

/* request a non-gpio and set the PIO config */
extern void ltq_pmu_enable(unsigned int module);
extern void ltq_pmu_disable(unsigned int module);

static inline int ltq_is_ar9(void)
{
	return (ltq_get_soc_type() == SOC_TYPE_AR9);
}

static inline int ltq_is_vr9(void)
{
	return (ltq_get_soc_type() == SOC_TYPE_VR9);
}

#endif /* CONFIG_SOC_TYPE_XWAY */
#endif /* _LTQ_XWAY_H__ */
