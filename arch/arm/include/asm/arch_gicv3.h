/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/arch_gicv3.h
 *
 * Copyright (C) 2015 ARM Ltd.
 */
#ifndef __ASM_ARCH_GICV3_H
#define __ASM_ARCH_GICV3_H

#ifndef __ASSEMBLY__

#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <asm/barrier.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>

#define ICC_EOIR1			__ACCESS_CP15(c12, 0, c12, 1)
#define ICC_DIR				__ACCESS_CP15(c12, 0, c11, 1)
#define ICC_IAR1			__ACCESS_CP15(c12, 0, c12, 0)
#define ICC_SGI1R			__ACCESS_CP15_64(0, c12)
#define ICC_PMR				__ACCESS_CP15(c4, 0, c6, 0)
#define ICC_CTLR			__ACCESS_CP15(c12, 0, c12, 4)
#define ICC_SRE				__ACCESS_CP15(c12, 0, c12, 5)
#define ICC_IGRPEN1			__ACCESS_CP15(c12, 0, c12, 7)
#define ICC_BPR1			__ACCESS_CP15(c12, 0, c12, 3)
#define ICC_RPR				__ACCESS_CP15(c12, 0, c11, 3)

#define __ICC_AP0Rx(x)			__ACCESS_CP15(c12, 0, c8, 4 | x)
#define ICC_AP0R0			__ICC_AP0Rx(0)
#define ICC_AP0R1			__ICC_AP0Rx(1)
#define ICC_AP0R2			__ICC_AP0Rx(2)
#define ICC_AP0R3			__ICC_AP0Rx(3)

#define __ICC_AP1Rx(x)			__ACCESS_CP15(c12, 0, c9, x)
#define ICC_AP1R0			__ICC_AP1Rx(0)
#define ICC_AP1R1			__ICC_AP1Rx(1)
#define ICC_AP1R2			__ICC_AP1Rx(2)
#define ICC_AP1R3			__ICC_AP1Rx(3)

#define CPUIF_MAP(a32, a64)			\
static inline void write_ ## a64(u32 val)	\
{						\
	write_sysreg(val, a32);			\
}						\
static inline u32 read_ ## a64(void)		\
{						\
	return read_sysreg(a32); 		\
}						\

CPUIF_MAP(ICC_EOIR1, ICC_EOIR1_EL1)
CPUIF_MAP(ICC_PMR, ICC_PMR_EL1)
CPUIF_MAP(ICC_AP0R0, ICC_AP0R0_EL1)
CPUIF_MAP(ICC_AP0R1, ICC_AP0R1_EL1)
CPUIF_MAP(ICC_AP0R2, ICC_AP0R2_EL1)
CPUIF_MAP(ICC_AP0R3, ICC_AP0R3_EL1)
CPUIF_MAP(ICC_AP1R0, ICC_AP1R0_EL1)
CPUIF_MAP(ICC_AP1R1, ICC_AP1R1_EL1)
CPUIF_MAP(ICC_AP1R2, ICC_AP1R2_EL1)
CPUIF_MAP(ICC_AP1R3, ICC_AP1R3_EL1)

#define read_gicreg(r)                 read_##r()
#define write_gicreg(v, r)             write_##r(v)

/* Low-level accessors */

static inline void gic_write_dir(u32 val)
{
	write_sysreg(val, ICC_DIR);
	isb();
}

static inline u32 gic_read_iar(void)
{
	u32 irqstat = read_sysreg(ICC_IAR1);

	dsb(sy);

	return irqstat;
}

static inline void gic_write_ctlr(u32 val)
{
	write_sysreg(val, ICC_CTLR);
	isb();
}

static inline u32 gic_read_ctlr(void)
{
	return read_sysreg(ICC_CTLR);
}

static inline void gic_write_grpen1(u32 val)
{
	write_sysreg(val, ICC_IGRPEN1);
	isb();
}

static inline void gic_write_sgi1r(u64 val)
{
	write_sysreg(val, ICC_SGI1R);
}

static inline u32 gic_read_sre(void)
{
	return read_sysreg(ICC_SRE);
}

static inline void gic_write_sre(u32 val)
{
	write_sysreg(val, ICC_SRE);
	isb();
}

static inline void gic_write_bpr1(u32 val)
{
	write_sysreg(val, ICC_BPR1);
}

static inline u32 gic_read_pmr(void)
{
	return read_sysreg(ICC_PMR);
}

static inline void gic_write_pmr(u32 val)
{
	write_sysreg(val, ICC_PMR);
}

static inline u32 gic_read_rpr(void)
{
	return read_sysreg(ICC_RPR);
}

/*
 * Even in 32bit systems that use LPAE, there is no guarantee that the I/O
 * interface provides true 64bit atomic accesses, so using strd/ldrd doesn't
 * make much sense.
 * Moreover, 64bit I/O emulation is extremely difficult to implement on
 * AArch32, since the syndrome register doesn't provide any information for
 * them.
 * Consequently, the following IO helpers use 32bit accesses.
 */
static inline void __gic_writeq_nonatomic(u64 val, volatile void __iomem *addr)
{
	writel_relaxed((u32)val, addr);
	writel_relaxed((u32)(val >> 32), addr + 4);
}

static inline u64 __gic_readq_nonatomic(const volatile void __iomem *addr)
{
	u64 val;

	val = readl_relaxed(addr);
	val |= (u64)readl_relaxed(addr + 4) << 32;
	return val;
}

#define gic_flush_dcache_to_poc(a,l)    __cpuc_flush_dcache_area((a), (l))

/*
 *  GICD_IROUTERn, contain the affinity values associated to each interrupt.
 *  The upper-word (aff3) will always be 0, so there is no need for a lock.
 */
#define gic_write_irouter(v, c)		__gic_writeq_nonatomic(v, c)

/*
 * GICR_TYPER is an ID register and doesn't need atomicity.
 */
#define gic_read_typer(c)		__gic_readq_nonatomic(c)

/*
 * GITS_BASER - hi and lo bits may be accessed independently.
 */
#define gits_read_baser(c)		__gic_readq_nonatomic(c)
#define gits_write_baser(v, c)		__gic_writeq_nonatomic(v, c)

/*
 * GICR_PENDBASER and GICR_PROPBASE are changed with LPIs disabled, so they
 * won't be being used during any updates and can be changed non-atomically
 */
#define gicr_read_propbaser(c)		__gic_readq_nonatomic(c)
#define gicr_write_propbaser(v, c)	__gic_writeq_nonatomic(v, c)
#define gicr_read_pendbaser(c)		__gic_readq_nonatomic(c)
#define gicr_write_pendbaser(v, c)	__gic_writeq_nonatomic(v, c)

/*
 * GICR_xLPIR - only the lower bits are significant
 */
#define gic_read_lpir(c)		readl_relaxed(c)
#define gic_write_lpir(v, c)		writel_relaxed(lower_32_bits(v), c)

/*
 * GITS_TYPER is an ID register and doesn't need atomicity.
 */
#define gits_read_typer(c)		__gic_readq_nonatomic(c)

/*
 * GITS_CBASER - hi and lo bits may be accessed independently.
 */
#define gits_read_cbaser(c)		__gic_readq_nonatomic(c)
#define gits_write_cbaser(v, c)		__gic_writeq_nonatomic(v, c)

/*
 * GITS_CWRITER - hi and lo bits may be accessed independently.
 */
#define gits_write_cwriter(v, c)	__gic_writeq_nonatomic(v, c)

/*
 * GICR_VPROPBASER - hi and lo bits may be accessed independently.
 */
#define gicr_read_vpropbaser(c)		__gic_readq_nonatomic(c)
#define gicr_write_vpropbaser(v, c)	__gic_writeq_nonatomic(v, c)

/*
 * GICR_VPENDBASER - the Valid bit must be cleared before changing
 * anything else.
 */
static inline void gicr_write_vpendbaser(u64 val, void __iomem *addr)
{
	u32 tmp;

	tmp = readl_relaxed(addr + 4);
	if (tmp & (GICR_VPENDBASER_Valid >> 32)) {
		tmp &= ~(GICR_VPENDBASER_Valid >> 32);
		writel_relaxed(tmp, addr + 4);
	}

	/*
	 * Use the fact that __gic_writeq_nonatomic writes the second
	 * half of the 64bit quantity after the first.
	 */
	__gic_writeq_nonatomic(val, addr);
}

#define gicr_read_vpendbaser(c)		__gic_readq_nonatomic(c)

static inline bool gic_prio_masking_enabled(void)
{
	return false;
}

static inline void gic_pmr_mask_irqs(void)
{
	/* Should not get called. */
	WARN_ON_ONCE(true);
}

static inline void gic_arch_enable_irqs(void)
{
	/* Should not get called. */
	WARN_ON_ONCE(true);
}

#endif /* !__ASSEMBLY__ */
#endif /* !__ASM_ARCH_GICV3_H */
