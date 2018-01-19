/*
 * Copyright (C) 2017 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_MIPS_CPS_H__
# error Please include asm/mips-cps.h rather than asm/mips-gic.h
#endif

#ifndef __MIPS_ASM_MIPS_GIC_H__
#define __MIPS_ASM_MIPS_GIC_H__

#include <linux/bitops.h>

/* The base address of the GIC registers */
extern void __iomem *mips_gic_base;

/* Offsets from the GIC base address to various control blocks */
#define MIPS_GIC_SHARED_OFS	0x00000
#define MIPS_GIC_SHARED_SZ	0x08000
#define MIPS_GIC_LOCAL_OFS	0x08000
#define MIPS_GIC_LOCAL_SZ	0x04000
#define MIPS_GIC_REDIR_OFS	0x0c000
#define MIPS_GIC_REDIR_SZ	0x04000
#define MIPS_GIC_USER_OFS	0x10000
#define MIPS_GIC_USER_SZ	0x10000

/* For read-only shared registers */
#define GIC_ACCESSOR_RO(sz, off, name)					\
	CPS_ACCESSOR_RO(gic, sz, MIPS_GIC_SHARED_OFS + off, name)

/* For read-write shared registers */
#define GIC_ACCESSOR_RW(sz, off, name)					\
	CPS_ACCESSOR_RW(gic, sz, MIPS_GIC_SHARED_OFS + off, name)

/* For read-only local registers */
#define GIC_VX_ACCESSOR_RO(sz, off, name)				\
	CPS_ACCESSOR_RO(gic, sz, MIPS_GIC_LOCAL_OFS + off, vl_##name)	\
	CPS_ACCESSOR_RO(gic, sz, MIPS_GIC_REDIR_OFS + off, vo_##name)

/* For read-write local registers */
#define GIC_VX_ACCESSOR_RW(sz, off, name)				\
	CPS_ACCESSOR_RW(gic, sz, MIPS_GIC_LOCAL_OFS + off, vl_##name)	\
	CPS_ACCESSOR_RW(gic, sz, MIPS_GIC_REDIR_OFS + off, vo_##name)

/* For read-only shared per-interrupt registers */
#define GIC_ACCESSOR_RO_INTR_REG(sz, off, stride, name)			\
static inline void __iomem *addr_gic_##name(unsigned int intr)		\
{									\
	return mips_gic_base + (off) + (intr * (stride));		\
}									\
									\
static inline unsigned int read_gic_##name(unsigned int intr)		\
{									\
	BUILD_BUG_ON(sz != 32);						\
	return __raw_readl(addr_gic_##name(intr));			\
}

/* For read-write shared per-interrupt registers */
#define GIC_ACCESSOR_RW_INTR_REG(sz, off, stride, name)			\
	GIC_ACCESSOR_RO_INTR_REG(sz, off, stride, name)			\
									\
static inline void write_gic_##name(unsigned int intr,			\
				    unsigned int val)			\
{									\
	BUILD_BUG_ON(sz != 32);						\
	__raw_writel(val, addr_gic_##name(intr));			\
}

/* For read-only local per-interrupt registers */
#define GIC_VX_ACCESSOR_RO_INTR_REG(sz, off, stride, name)		\
	GIC_ACCESSOR_RO_INTR_REG(sz, MIPS_GIC_LOCAL_OFS + off,		\
				 stride, vl_##name)			\
	GIC_ACCESSOR_RO_INTR_REG(sz, MIPS_GIC_REDIR_OFS + off,		\
				 stride, vo_##name)

/* For read-write local per-interrupt registers */
#define GIC_VX_ACCESSOR_RW_INTR_REG(sz, off, stride, name)		\
	GIC_ACCESSOR_RW_INTR_REG(sz, MIPS_GIC_LOCAL_OFS + off,		\
				 stride, vl_##name)			\
	GIC_ACCESSOR_RW_INTR_REG(sz, MIPS_GIC_REDIR_OFS + off,		\
				 stride, vo_##name)

/* For read-only shared bit-per-interrupt registers */
#define GIC_ACCESSOR_RO_INTR_BIT(off, name)				\
static inline void __iomem *addr_gic_##name(void)			\
{									\
	return mips_gic_base + (off);					\
}									\
									\
static inline unsigned int read_gic_##name(unsigned int intr)		\
{									\
	void __iomem *addr = addr_gic_##name();				\
	unsigned int val;						\
									\
	if (mips_cm_is64) {						\
		addr += (intr / 64) * sizeof(uint64_t);			\
		val = __raw_readq(addr) >> intr % 64;			\
	} else {							\
		addr += (intr / 32) * sizeof(uint32_t);			\
		val = __raw_readl(addr) >> intr % 32;			\
	}								\
									\
	return val & 0x1;						\
}

/* For read-write shared bit-per-interrupt registers */
#define GIC_ACCESSOR_RW_INTR_BIT(off, name)				\
	GIC_ACCESSOR_RO_INTR_BIT(off, name)				\
									\
static inline void write_gic_##name(unsigned int intr)			\
{									\
	void __iomem *addr = addr_gic_##name();				\
									\
	if (mips_cm_is64) {						\
		addr += (intr / 64) * sizeof(uint64_t);			\
		__raw_writeq(BIT(intr % 64), addr);			\
	} else {							\
		addr += (intr / 32) * sizeof(uint32_t);			\
		__raw_writel(BIT(intr % 32), addr);			\
	}								\
}									\
									\
static inline void change_gic_##name(unsigned int intr,			\
				     unsigned int val)			\
{									\
	void __iomem *addr = addr_gic_##name();				\
									\
	if (mips_cm_is64) {						\
		uint64_t _val;						\
									\
		addr += (intr / 64) * sizeof(uint64_t);			\
		_val = __raw_readq(addr);				\
		_val &= ~BIT_ULL(intr % 64);				\
		_val |= (uint64_t)val << (intr % 64);			\
		__raw_writeq(_val, addr);				\
	} else {							\
		uint32_t _val;						\
									\
		addr += (intr / 32) * sizeof(uint32_t);			\
		_val = __raw_readl(addr);				\
		_val &= ~BIT(intr % 32);				\
		_val |= val << (intr % 32);				\
		__raw_writel(_val, addr);				\
	}								\
}

/* For read-only local bit-per-interrupt registers */
#define GIC_VX_ACCESSOR_RO_INTR_BIT(sz, off, name)			\
	GIC_ACCESSOR_RO_INTR_BIT(sz, MIPS_GIC_LOCAL_OFS + off,		\
				 vl_##name)				\
	GIC_ACCESSOR_RO_INTR_BIT(sz, MIPS_GIC_REDIR_OFS + off,		\
				 vo_##name)

/* For read-write local bit-per-interrupt registers */
#define GIC_VX_ACCESSOR_RW_INTR_BIT(sz, off, name)			\
	GIC_ACCESSOR_RW_INTR_BIT(sz, MIPS_GIC_LOCAL_OFS + off,		\
				 vl_##name)				\
	GIC_ACCESSOR_RW_INTR_BIT(sz, MIPS_GIC_REDIR_OFS + off,		\
				 vo_##name)

/* GIC_SH_CONFIG - Information about the GIC configuration */
GIC_ACCESSOR_RW(32, 0x000, config)
#define GIC_CONFIG_COUNTSTOP		BIT(28)
#define GIC_CONFIG_COUNTBITS		GENMASK(27, 24)
#define GIC_CONFIG_NUMINTERRUPTS	GENMASK(23, 16)
#define GIC_CONFIG_PVPS			GENMASK(6, 0)

/* GIC_SH_COUNTER - Shared global counter value */
GIC_ACCESSOR_RW(64, 0x010, counter)
GIC_ACCESSOR_RW(32, 0x010, counter_32l)
GIC_ACCESSOR_RW(32, 0x014, counter_32h)

/* GIC_SH_POL_* - Configures interrupt polarity */
GIC_ACCESSOR_RW_INTR_BIT(0x100, pol)
#define GIC_POL_ACTIVE_LOW		0	/* when level triggered */
#define GIC_POL_ACTIVE_HIGH		1	/* when level triggered */
#define GIC_POL_FALLING_EDGE		0	/* when single-edge triggered */
#define GIC_POL_RISING_EDGE		1	/* when single-edge triggered */

/* GIC_SH_TRIG_* - Configures interrupts to be edge or level triggered */
GIC_ACCESSOR_RW_INTR_BIT(0x180, trig)
#define GIC_TRIG_LEVEL			0
#define GIC_TRIG_EDGE			1

/* GIC_SH_DUAL_* - Configures whether interrupts trigger on both edges */
GIC_ACCESSOR_RW_INTR_BIT(0x200, dual)
#define GIC_DUAL_SINGLE			0	/* when edge-triggered */
#define GIC_DUAL_DUAL			1	/* when edge-triggered */

/* GIC_SH_WEDGE - Write an 'edge', ie. trigger an interrupt */
GIC_ACCESSOR_RW(32, 0x280, wedge)
#define GIC_WEDGE_RW			BIT(31)
#define GIC_WEDGE_INTR			GENMASK(7, 0)

/* GIC_SH_RMASK_* - Reset/clear shared interrupt mask bits */
GIC_ACCESSOR_RW_INTR_BIT(0x300, rmask)

/* GIC_SH_SMASK_* - Set shared interrupt mask bits */
GIC_ACCESSOR_RW_INTR_BIT(0x380, smask)

/* GIC_SH_MASK_* - Read the current shared interrupt mask */
GIC_ACCESSOR_RO_INTR_BIT(0x400, mask)

/* GIC_SH_PEND_* - Read currently pending shared interrupts */
GIC_ACCESSOR_RO_INTR_BIT(0x480, pend)

/* GIC_SH_MAPx_PIN - Map shared interrupts to a particular CPU pin */
GIC_ACCESSOR_RW_INTR_REG(32, 0x500, 0x4, map_pin)
#define GIC_MAP_PIN_MAP_TO_PIN		BIT(31)
#define GIC_MAP_PIN_MAP_TO_NMI		BIT(30)
#define GIC_MAP_PIN_MAP			GENMASK(5, 0)

/* GIC_SH_MAPx_VP - Map shared interrupts to a particular Virtual Processor */
GIC_ACCESSOR_RW_INTR_REG(32, 0x2000, 0x20, map_vp)

/* GIC_Vx_CTL - VP-level interrupt control */
GIC_VX_ACCESSOR_RW(32, 0x000, ctl)
#define GIC_VX_CTL_FDC_ROUTABLE		BIT(4)
#define GIC_VX_CTL_SWINT_ROUTABLE	BIT(3)
#define GIC_VX_CTL_PERFCNT_ROUTABLE	BIT(2)
#define GIC_VX_CTL_TIMER_ROUTABLE	BIT(1)
#define GIC_VX_CTL_EIC			BIT(0)

/* GIC_Vx_PEND - Read currently pending local interrupts */
GIC_VX_ACCESSOR_RO(32, 0x004, pend)

/* GIC_Vx_MASK - Read the current local interrupt mask */
GIC_VX_ACCESSOR_RO(32, 0x008, mask)

/* GIC_Vx_RMASK - Reset/clear local interrupt mask bits */
GIC_VX_ACCESSOR_RW(32, 0x00c, rmask)

/* GIC_Vx_SMASK - Set local interrupt mask bits */
GIC_VX_ACCESSOR_RW(32, 0x010, smask)

/* GIC_Vx_*_MAP - Route local interrupts to the desired pins */
GIC_VX_ACCESSOR_RW_INTR_REG(32, 0x040, 0x4, map)

/* GIC_Vx_WD_MAP - Route the local watchdog timer interrupt */
GIC_VX_ACCESSOR_RW(32, 0x040, wd_map)

/* GIC_Vx_COMPARE_MAP - Route the local count/compare interrupt */
GIC_VX_ACCESSOR_RW(32, 0x044, compare_map)

/* GIC_Vx_TIMER_MAP - Route the local CPU timer (cp0 count/compare) interrupt */
GIC_VX_ACCESSOR_RW(32, 0x048, timer_map)

/* GIC_Vx_FDC_MAP - Route the local fast debug channel interrupt */
GIC_VX_ACCESSOR_RW(32, 0x04c, fdc_map)

/* GIC_Vx_PERFCTR_MAP - Route the local performance counter interrupt */
GIC_VX_ACCESSOR_RW(32, 0x050, perfctr_map)

/* GIC_Vx_SWINT0_MAP - Route the local software interrupt 0 */
GIC_VX_ACCESSOR_RW(32, 0x054, swint0_map)

/* GIC_Vx_SWINT1_MAP - Route the local software interrupt 1 */
GIC_VX_ACCESSOR_RW(32, 0x058, swint1_map)

/* GIC_Vx_OTHER - Configure access to other Virtual Processor registers */
GIC_VX_ACCESSOR_RW(32, 0x080, other)
#define GIC_VX_OTHER_VPNUM		GENMASK(5, 0)

/* GIC_Vx_IDENT - Retrieve the local Virtual Processor's ID */
GIC_VX_ACCESSOR_RO(32, 0x088, ident)
#define GIC_VX_IDENT_VPNUM		GENMASK(5, 0)

/* GIC_Vx_COMPARE - Value to compare with GIC_SH_COUNTER */
GIC_VX_ACCESSOR_RW(64, 0x0a0, compare)

/* GIC_Vx_EIC_SHADOW_SET_BASE - Set shadow register set for each interrupt */
GIC_VX_ACCESSOR_RW_INTR_REG(32, 0x100, 0x4, eic_shadow_set)

/**
 * enum mips_gic_local_interrupt - GIC local interrupts
 * @GIC_LOCAL_INT_WD: GIC watchdog timer interrupt
 * @GIC_LOCAL_INT_COMPARE: GIC count/compare interrupt
 * @GIC_LOCAL_INT_TIMER: CP0 count/compare interrupt
 * @GIC_LOCAL_INT_PERFCTR: Performance counter interrupt
 * @GIC_LOCAL_INT_SWINT0: Software interrupt 0
 * @GIC_LOCAL_INT_SWINT1: Software interrupt 1
 * @GIC_LOCAL_INT_FDC: Fast debug channel interrupt
 * @GIC_NUM_LOCAL_INTRS: The number of local interrupts
 *
 * Enumerates interrupts provided by the GIC that are local to a VP.
 */
enum mips_gic_local_interrupt {
	GIC_LOCAL_INT_WD,
	GIC_LOCAL_INT_COMPARE,
	GIC_LOCAL_INT_TIMER,
	GIC_LOCAL_INT_PERFCTR,
	GIC_LOCAL_INT_SWINT0,
	GIC_LOCAL_INT_SWINT1,
	GIC_LOCAL_INT_FDC,
	GIC_NUM_LOCAL_INTRS
};

/**
 * mips_gic_present() - Determine whether a GIC is present
 *
 * Determines whether a MIPS Global Interrupt Controller (GIC) is present in
 * the system that the kernel is running on.
 *
 * Return true if a GIC is present, else false.
 */
static inline bool mips_gic_present(void)
{
	return IS_ENABLED(CONFIG_MIPS_GIC) && mips_gic_base;
}

/**
 * gic_get_c0_compare_int() - Return cp0 count/compare interrupt virq
 *
 * Determine the virq number to use for the coprocessor 0 count/compare
 * interrupt, which may be routed via the GIC.
 *
 * Returns the virq number or a negative error number.
 */
extern int gic_get_c0_compare_int(void);

/**
 * gic_get_c0_perfcount_int() - Return performance counter interrupt virq
 *
 * Determine the virq number to use for CPU performance counter interrupts,
 * which may be routed via the GIC.
 *
 * Returns the virq number or a negative error number.
 */
extern int gic_get_c0_perfcount_int(void);

/**
 * gic_get_c0_fdc_int() - Return fast debug channel interrupt virq
 *
 * Determine the virq number to use for fast debug channel (FDC) interrupts,
 * which may be routed via the GIC.
 *
 * Returns the virq number or a negative error number.
 */
extern int gic_get_c0_fdc_int(void);

#endif /* __MIPS_ASM_MIPS_CPS_H__ */
