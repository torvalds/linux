/*
 * Register definitions for the Power Manager (PM)
 */
#ifndef __ARCH_AVR32_MACH_AT32AP_PM_H__
#define __ARCH_AVR32_MACH_AT32AP_PM_H__

/* PM register offsets */
#define PM_MCCTRL				0x0000
#define PM_CKSEL				0x0004
#define PM_CPU_MASK				0x0008
#define PM_HSB_MASK				0x000c
#define PM_PBA_MASK				0x0010
#define PM_PBB_MASK				0x0014
#define PM_PLL0					0x0020
#define PM_PLL1					0x0024
#define PM_IER					0x0040
#define PM_IDR					0x0044
#define PM_IMR					0x0048
#define PM_ISR					0x004c
#define PM_ICR					0x0050
#define PM_GCCTRL(x)				(0x0060 + 4 * (x))
#define PM_RCAUSE				0x00c0

/* Bitfields in CKSEL */
#define PM_CPUSEL_OFFSET			0
#define PM_CPUSEL_SIZE				3
#define PM_CPUDIV_OFFSET			7
#define PM_CPUDIV_SIZE				1
#define PM_HSBSEL_OFFSET			8
#define PM_HSBSEL_SIZE				3
#define PM_HSBDIV_OFFSET			15
#define PM_HSBDIV_SIZE				1
#define PM_PBASEL_OFFSET			16
#define PM_PBASEL_SIZE				3
#define PM_PBADIV_OFFSET			23
#define PM_PBADIV_SIZE				1
#define PM_PBBSEL_OFFSET			24
#define PM_PBBSEL_SIZE				3
#define PM_PBBDIV_OFFSET			31
#define PM_PBBDIV_SIZE				1

/* Bitfields in PLL0 */
#define PM_PLLEN_OFFSET				0
#define PM_PLLEN_SIZE				1
#define PM_PLLOSC_OFFSET			1
#define PM_PLLOSC_SIZE				1
#define PM_PLLOPT_OFFSET			2
#define PM_PLLOPT_SIZE				3
#define PM_PLLDIV_OFFSET			8
#define PM_PLLDIV_SIZE				8
#define PM_PLLMUL_OFFSET			16
#define PM_PLLMUL_SIZE				8
#define PM_PLLCOUNT_OFFSET			24
#define PM_PLLCOUNT_SIZE			6
#define PM_PLLTEST_OFFSET			31
#define PM_PLLTEST_SIZE				1

/* Bitfields in ICR */
#define PM_LOCK0_OFFSET				0
#define PM_LOCK0_SIZE				1
#define PM_LOCK1_OFFSET				1
#define PM_LOCK1_SIZE				1
#define PM_WAKE_OFFSET				2
#define PM_WAKE_SIZE				1
#define PM_CKRDY_OFFSET				5
#define PM_CKRDY_SIZE				1
#define PM_MSKRDY_OFFSET			6
#define PM_MSKRDY_SIZE				1

/* Bitfields in GCCTRL0 */
#define PM_OSCSEL_OFFSET			0
#define PM_OSCSEL_SIZE				1
#define PM_PLLSEL_OFFSET			1
#define PM_PLLSEL_SIZE				1
#define PM_CEN_OFFSET				2
#define PM_CEN_SIZE				1
#define PM_DIVEN_OFFSET				4
#define PM_DIVEN_SIZE				1
#define PM_DIV_OFFSET				8
#define PM_DIV_SIZE				8

/* Bitfields in RCAUSE */
#define PM_POR_OFFSET				0
#define PM_POR_SIZE				1
#define PM_EXT_OFFSET				2
#define PM_EXT_SIZE				1
#define PM_WDT_OFFSET				3
#define PM_WDT_SIZE				1
#define PM_NTAE_OFFSET				4
#define PM_NTAE_SIZE				1

/* Bit manipulation macros */
#define PM_BIT(name)					\
	(1 << PM_##name##_OFFSET)
#define PM_BF(name,value)				\
	(((value) & ((1 << PM_##name##_SIZE) - 1))	\
	 << PM_##name##_OFFSET)
#define PM_BFEXT(name,value)				\
	(((value) >> PM_##name##_OFFSET)		\
	 & ((1 << PM_##name##_SIZE) - 1))
#define PM_BFINS(name,value,old)\
	(((old) & ~(((1 << PM_##name##_SIZE) - 1)	\
		    << PM_##name##_OFFSET))		\
	 | PM_BF(name,value))

/* Register access macros */
#define pm_readl(reg)							\
	__raw_readl((void __iomem __force *)PM_BASE + PM_##reg)
#define pm_writel(reg,value)						\
	__raw_writel((value), (void __iomem __force *)PM_BASE + PM_##reg)

#endif /* __ARCH_AVR32_MACH_AT32AP_PM_H__ */
