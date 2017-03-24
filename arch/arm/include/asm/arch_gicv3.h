/*
 * arch/arm/include/asm/arch_gicv3.h
 *
 * Copyright (C) 2015 ARM Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_ARCH_GICV3_H
#define __ASM_ARCH_GICV3_H

#ifndef __ASSEMBLY__

#include <linux/io.h>
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

#define ICC_HSRE			__ACCESS_CP15(c12, 4, c9, 5)

#define ICH_VSEIR			__ACCESS_CP15(c12, 4, c9, 4)
#define ICH_HCR				__ACCESS_CP15(c12, 4, c11, 0)
#define ICH_VTR				__ACCESS_CP15(c12, 4, c11, 1)
#define ICH_MISR			__ACCESS_CP15(c12, 4, c11, 2)
#define ICH_EISR			__ACCESS_CP15(c12, 4, c11, 3)
#define ICH_ELSR			__ACCESS_CP15(c12, 4, c11, 5)
#define ICH_VMCR			__ACCESS_CP15(c12, 4, c11, 7)

#define __LR0(x)			__ACCESS_CP15(c12, 4, c12, x)
#define __LR8(x)			__ACCESS_CP15(c12, 4, c13, x)

#define ICH_LR0				__LR0(0)
#define ICH_LR1				__LR0(1)
#define ICH_LR2				__LR0(2)
#define ICH_LR3				__LR0(3)
#define ICH_LR4				__LR0(4)
#define ICH_LR5				__LR0(5)
#define ICH_LR6				__LR0(6)
#define ICH_LR7				__LR0(7)
#define ICH_LR8				__LR8(0)
#define ICH_LR9				__LR8(1)
#define ICH_LR10			__LR8(2)
#define ICH_LR11			__LR8(3)
#define ICH_LR12			__LR8(4)
#define ICH_LR13			__LR8(5)
#define ICH_LR14			__LR8(6)
#define ICH_LR15			__LR8(7)

/* LR top half */
#define __LRC0(x)			__ACCESS_CP15(c12, 4, c14, x)
#define __LRC8(x)			__ACCESS_CP15(c12, 4, c15, x)

#define ICH_LRC0			__LRC0(0)
#define ICH_LRC1			__LRC0(1)
#define ICH_LRC2			__LRC0(2)
#define ICH_LRC3			__LRC0(3)
#define ICH_LRC4			__LRC0(4)
#define ICH_LRC5			__LRC0(5)
#define ICH_LRC6			__LRC0(6)
#define ICH_LRC7			__LRC0(7)
#define ICH_LRC8			__LRC8(0)
#define ICH_LRC9			__LRC8(1)
#define ICH_LRC10			__LRC8(2)
#define ICH_LRC11			__LRC8(3)
#define ICH_LRC12			__LRC8(4)
#define ICH_LRC13			__LRC8(5)
#define ICH_LRC14			__LRC8(6)
#define ICH_LRC15			__LRC8(7)

#define __AP0Rx(x)			__ACCESS_CP15(c12, 4, c8, x)
#define ICH_AP0R0			__AP0Rx(0)
#define ICH_AP0R1			__AP0Rx(1)
#define ICH_AP0R2			__AP0Rx(2)
#define ICH_AP0R3			__AP0Rx(3)

#define __AP1Rx(x)			__ACCESS_CP15(c12, 4, c9, x)
#define ICH_AP1R0			__AP1Rx(0)
#define ICH_AP1R1			__AP1Rx(1)
#define ICH_AP1R2			__AP1Rx(2)
#define ICH_AP1R3			__AP1Rx(3)

/* A32-to-A64 mappings used by VGIC save/restore */

#define CPUIF_MAP(a32, a64)			\
static inline void write_ ## a64(u32 val)	\
{						\
	write_sysreg(val, a32);			\
}						\
static inline u32 read_ ## a64(void)		\
{						\
	return read_sysreg(a32); 		\
}						\

#define CPUIF_MAP_LO_HI(a32lo, a32hi, a64)	\
static inline void write_ ## a64(u64 val)	\
{						\
	write_sysreg(lower_32_bits(val), a32lo);\
	write_sysreg(upper_32_bits(val), a32hi);\
}						\
static inline u64 read_ ## a64(void)		\
{						\
	u64 val = read_sysreg(a32lo);		\
						\
	val |=	(u64)read_sysreg(a32hi) << 32;	\
						\
	return val; 				\
}

CPUIF_MAP(ICH_HCR, ICH_HCR_EL2)
CPUIF_MAP(ICH_VTR, ICH_VTR_EL2)
CPUIF_MAP(ICH_MISR, ICH_MISR_EL2)
CPUIF_MAP(ICH_EISR, ICH_EISR_EL2)
CPUIF_MAP(ICH_ELSR, ICH_ELSR_EL2)
CPUIF_MAP(ICH_VMCR, ICH_VMCR_EL2)
CPUIF_MAP(ICH_AP0R3, ICH_AP0R3_EL2)
CPUIF_MAP(ICH_AP0R2, ICH_AP0R2_EL2)
CPUIF_MAP(ICH_AP0R1, ICH_AP0R1_EL2)
CPUIF_MAP(ICH_AP0R0, ICH_AP0R0_EL2)
CPUIF_MAP(ICH_AP1R3, ICH_AP1R3_EL2)
CPUIF_MAP(ICH_AP1R2, ICH_AP1R2_EL2)
CPUIF_MAP(ICH_AP1R1, ICH_AP1R1_EL2)
CPUIF_MAP(ICH_AP1R0, ICH_AP1R0_EL2)
CPUIF_MAP(ICC_HSRE, ICC_SRE_EL2)
CPUIF_MAP(ICC_SRE, ICC_SRE_EL1)

CPUIF_MAP_LO_HI(ICH_LR15, ICH_LRC15, ICH_LR15_EL2)
CPUIF_MAP_LO_HI(ICH_LR14, ICH_LRC14, ICH_LR14_EL2)
CPUIF_MAP_LO_HI(ICH_LR13, ICH_LRC13, ICH_LR13_EL2)
CPUIF_MAP_LO_HI(ICH_LR12, ICH_LRC12, ICH_LR12_EL2)
CPUIF_MAP_LO_HI(ICH_LR11, ICH_LRC11, ICH_LR11_EL2)
CPUIF_MAP_LO_HI(ICH_LR10, ICH_LRC10, ICH_LR10_EL2)
CPUIF_MAP_LO_HI(ICH_LR9, ICH_LRC9, ICH_LR9_EL2)
CPUIF_MAP_LO_HI(ICH_LR8, ICH_LRC8, ICH_LR8_EL2)
CPUIF_MAP_LO_HI(ICH_LR7, ICH_LRC7, ICH_LR7_EL2)
CPUIF_MAP_LO_HI(ICH_LR6, ICH_LRC6, ICH_LR6_EL2)
CPUIF_MAP_LO_HI(ICH_LR5, ICH_LRC5, ICH_LR5_EL2)
CPUIF_MAP_LO_HI(ICH_LR4, ICH_LRC4, ICH_LR4_EL2)
CPUIF_MAP_LO_HI(ICH_LR3, ICH_LRC3, ICH_LR3_EL2)
CPUIF_MAP_LO_HI(ICH_LR2, ICH_LRC2, ICH_LR2_EL2)
CPUIF_MAP_LO_HI(ICH_LR1, ICH_LRC1, ICH_LR1_EL2)
CPUIF_MAP_LO_HI(ICH_LR0, ICH_LRC0, ICH_LR0_EL2)

#define read_gicreg(r)                 read_##r()
#define write_gicreg(v, r)             write_##r(v)

/* Low-level accessors */

static inline void gic_write_eoir(u32 irq)
{
	write_sysreg(irq, ICC_EOIR1);
	isb();
}

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

static inline void gic_write_pmr(u32 val)
{
	write_sysreg(val, ICC_PMR);
}

static inline void gic_write_ctlr(u32 val)
{
	write_sysreg(val, ICC_CTLR);
	isb();
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

#endif /* !__ASSEMBLY__ */
#endif /* !__ASM_ARCH_GICV3_H */
