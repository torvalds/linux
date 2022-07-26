/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGSON_H
#define __ASM_LOONGSON_H

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

extern const struct plat_smp_ops loongson3_smp_ops;

#define LOONGSON_REG(x) \
	(*(volatile u32 *)((char *)TO_UNCACHE(LOONGSON_REG_BASE) + (x)))

#define LOONGSON_LIO_BASE	0x18000000
#define LOONGSON_LIO_SIZE	0x00100000	/* 1M */
#define LOONGSON_LIO_TOP	(LOONGSON_LIO_BASE+LOONGSON_LIO_SIZE-1)

#define LOONGSON_BOOT_BASE	0x1c000000
#define LOONGSON_BOOT_SIZE	0x02000000	/* 32M */
#define LOONGSON_BOOT_TOP	(LOONGSON_BOOT_BASE+LOONGSON_BOOT_SIZE-1)

#define LOONGSON_REG_BASE	0x1fe00000
#define LOONGSON_REG_SIZE	0x00100000	/* 1M */
#define LOONGSON_REG_TOP	(LOONGSON_REG_BASE+LOONGSON_REG_SIZE-1)

/* GPIO Regs - r/w */

#define LOONGSON_GPIODATA		LOONGSON_REG(0x11c)
#define LOONGSON_GPIOIE			LOONGSON_REG(0x120)
#define LOONGSON_REG_GPIO_BASE          (LOONGSON_REG_BASE + 0x11c)

#define MAX_PACKAGES 16

/* Chip Config register of each physical cpu package */
extern u64 loongson_chipcfg[MAX_PACKAGES];
#define LOONGSON_CHIPCFG(id) (*(volatile u32 *)(loongson_chipcfg[id]))

/* Chip Temperature register of each physical cpu package */
extern u64 loongson_chiptemp[MAX_PACKAGES];
#define LOONGSON_CHIPTEMP(id) (*(volatile u32 *)(loongson_chiptemp[id]))

/* Freq Control register of each physical cpu package */
extern u64 loongson_freqctrl[MAX_PACKAGES];
#define LOONGSON_FREQCTRL(id) (*(volatile u32 *)(loongson_freqctrl[id]))

#define xconf_readl(addr) readl(addr)
#define xconf_readq(addr) readq(addr)

static inline void xconf_writel(u32 val, volatile void __iomem *addr)
{
	asm volatile (
	"	st.w	%[v], %[hw], 0	\n"
	"	ld.b	$zero, %[hw], 0	\n"
	:
	: [hw] "r" (addr), [v] "r" (val)
	);
}

static inline void xconf_writeq(u64 val64, volatile void __iomem *addr)
{
	asm volatile (
	"	st.d	%[v], %[hw], 0	\n"
	"	ld.b	$zero, %[hw], 0	\n"
	:
	: [hw] "r" (addr),  [v] "r" (val64)
	);
}

/* ============== LS7A registers =============== */
#define LS7A_PCH_REG_BASE		0x10000000UL
/* LPC regs */
#define LS7A_LPC_REG_BASE		(LS7A_PCH_REG_BASE + 0x00002000)
/* CHIPCFG regs */
#define LS7A_CHIPCFG_REG_BASE		(LS7A_PCH_REG_BASE + 0x00010000)
/* MISC reg base */
#define LS7A_MISC_REG_BASE		(LS7A_PCH_REG_BASE + 0x00080000)
/* ACPI regs */
#define LS7A_ACPI_REG_BASE		(LS7A_MISC_REG_BASE + 0x00050000)
/* RTC regs */
#define LS7A_RTC_REG_BASE		(LS7A_MISC_REG_BASE + 0x00050100)

#define LS7A_DMA_CFG			(volatile void *)TO_UNCACHE(LS7A_CHIPCFG_REG_BASE + 0x041c)
#define LS7A_DMA_NODE_SHF		8
#define LS7A_DMA_NODE_MASK		0x1F00

#define LS7A_INT_MASK_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x020)
#define LS7A_INT_EDGE_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x060)
#define LS7A_INT_CLEAR_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x080)
#define LS7A_INT_HTMSI_EN_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x040)
#define LS7A_INT_ROUTE_ENTRY_REG	(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x100)
#define LS7A_INT_HTMSI_VEC_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x200)
#define LS7A_INT_STATUS_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x3a0)
#define LS7A_INT_POL_REG		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x3e0)
#define LS7A_LPC_INT_CTL		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x2000)
#define LS7A_LPC_INT_ENA		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x2004)
#define LS7A_LPC_INT_STS		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x2008)
#define LS7A_LPC_INT_CLR		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x200c)
#define LS7A_LPC_INT_POL		(volatile void *)TO_UNCACHE(LS7A_PCH_REG_BASE + 0x2010)

#define LS7A_PMCON_SOC_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x000)
#define LS7A_PMCON_RESUME_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x004)
#define LS7A_PMCON_RTC_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x008)
#define LS7A_PM1_EVT_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x00c)
#define LS7A_PM1_ENA_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x010)
#define LS7A_PM1_CNT_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x014)
#define LS7A_PM1_TMR_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x018)
#define LS7A_P_CNT_REG			(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x01c)
#define LS7A_GPE0_STS_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x028)
#define LS7A_GPE0_ENA_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x02c)
#define LS7A_RST_CNT_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x030)
#define LS7A_WD_SET_REG			(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x034)
#define LS7A_WD_TIMER_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x038)
#define LS7A_THSENS_CNT_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x04c)
#define LS7A_GEN_RTC_1_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x050)
#define LS7A_GEN_RTC_2_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x054)
#define LS7A_DPM_CFG_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x400)
#define LS7A_DPM_STS_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x404)
#define LS7A_DPM_CNT_REG		(volatile void *)TO_UNCACHE(LS7A_ACPI_REG_BASE + 0x408)

typedef enum {
	ACPI_PCI_HOTPLUG_STATUS	= 1 << 1,
	ACPI_CPU_HOTPLUG_STATUS	= 1 << 2,
	ACPI_MEM_HOTPLUG_STATUS	= 1 << 3,
	ACPI_POWERBUTTON_STATUS	= 1 << 8,
	ACPI_RTC_WAKE_STATUS	= 1 << 10,
	ACPI_PCI_WAKE_STATUS	= 1 << 14,
	ACPI_ANY_WAKE_STATUS	= 1 << 15,
} AcpiEventStatusBits;

#define HT1LO_OFFSET		0xe0000000000UL

/* PCI Configuration Space Base */
#define MCFG_EXT_PCICFG_BASE		0xefe00000000UL

/* REG ACCESS*/
#define ls7a_readb(addr)	(*(volatile unsigned char  *)TO_UNCACHE(addr))
#define ls7a_readw(addr)	(*(volatile unsigned short *)TO_UNCACHE(addr))
#define ls7a_readl(addr)	(*(volatile unsigned int   *)TO_UNCACHE(addr))
#define ls7a_readq(addr)	(*(volatile unsigned long  *)TO_UNCACHE(addr))
#define ls7a_writeb(val, addr)	*(volatile unsigned char  *)TO_UNCACHE(addr) = (val)
#define ls7a_writew(val, addr)	*(volatile unsigned short *)TO_UNCACHE(addr) = (val)
#define ls7a_writel(val, addr)	*(volatile unsigned int   *)TO_UNCACHE(addr) = (val)
#define ls7a_writeq(val, addr)	*(volatile unsigned long  *)TO_UNCACHE(addr) = (val)

#endif /* __ASM_LOONGSON_H */
