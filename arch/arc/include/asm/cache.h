/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ARC_ASM_CACHE_H
#define __ARC_ASM_CACHE_H

/* In case $$ not config, setup a dummy number for rest of kernel */
#ifndef CONFIG_ARC_CACHE_LINE_SHIFT
#define L1_CACHE_SHIFT		6
#else
#define L1_CACHE_SHIFT		CONFIG_ARC_CACHE_LINE_SHIFT
#endif

#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)
#define CACHE_LINE_MASK		(~(L1_CACHE_BYTES - 1))

/*
 * ARC700 doesn't cache any access in top 1G (0xc000_0000 to 0xFFFF_FFFF)
 * Ideal for wiring memory mapped peripherals as we don't need to do
 * explicit uncached accesses (LD.di/ST.di) hence more portable drivers
 */
#define ARC_UNCACHED_ADDR_SPACE	0xc0000000

#ifndef __ASSEMBLY__

/* Uncached access macros */
#define arc_read_uncached_32(ptr)	\
({					\
	unsigned int __ret;		\
	__asm__ __volatile__(		\
	"	ld.di %0, [%1]	\n"	\
	: "=r"(__ret)			\
	: "r"(ptr));			\
	__ret;				\
})

#define arc_write_uncached_32(ptr, data)\
({					\
	__asm__ __volatile__(		\
	"	st.di %0, [%1]	\n"	\
	:				\
	: "r"(data), "r"(ptr));		\
})

/* Largest line length for either L1 or L2 is 128 bytes */
#define SMP_CACHE_BYTES		128
#define cache_line_size()	SMP_CACHE_BYTES
#define ARCH_DMA_MINALIGN	SMP_CACHE_BYTES

/*
 * Make sure slab-allocated buffers are 64-bit aligned when atomic64_t uses
 * ARCv2 64-bit atomics (LLOCKD/SCONDD). This guarantess runtime 64-bit
 * alignment for any atomic64_t embedded in buffer.
 * Default ARCH_SLAB_MINALIGN is __alignof__(long long) which has a relaxed
 * value of 4 (and not 8) in ARC ABI.
 */
#if defined(CONFIG_ARC_HAS_LL64) && defined(CONFIG_ARC_HAS_LLSC)
#define ARCH_SLAB_MINALIGN	8
#endif

extern void arc_cache_init(void);
extern char *arc_cache_mumbojumbo(int cpu_id, char *buf, int len);
extern void read_decode_cache_bcr(void);

extern int ioc_enable;
extern unsigned long perip_base, perip_end;

#endif	/* !__ASSEMBLY__ */

/* Instruction cache related Auxiliary registers */
#define ARC_REG_IC_BCR		0x77	/* Build Config reg */
#define ARC_REG_IC_IVIC		0x10
#define ARC_REG_IC_CTRL		0x11
#define ARC_REG_IC_IVIR		0x16
#define ARC_REG_IC_ENDR		0x17
#define ARC_REG_IC_IVIL		0x19
#define ARC_REG_IC_PTAG		0x1E
#define ARC_REG_IC_PTAG_HI	0x1F

/* Bit val in IC_CTRL */
#define IC_CTRL_DIS		0x1

/* Data cache related Auxiliary registers */
#define ARC_REG_DC_BCR		0x72	/* Build Config reg */
#define ARC_REG_DC_IVDC		0x47
#define ARC_REG_DC_CTRL		0x48
#define ARC_REG_DC_IVDL		0x4A
#define ARC_REG_DC_FLSH		0x4B
#define ARC_REG_DC_FLDL		0x4C
#define ARC_REG_DC_STARTR	0x4D
#define ARC_REG_DC_ENDR		0x4E
#define ARC_REG_DC_PTAG		0x5C
#define ARC_REG_DC_PTAG_HI	0x5F

/* Bit val in DC_CTRL */
#define DC_CTRL_DIS		0x001
#define DC_CTRL_INV_MODE_FLUSH	0x040
#define DC_CTRL_FLUSH_STATUS	0x100
#define DC_CTRL_RGN_OP_INV	0x200
#define DC_CTRL_RGN_OP_MSK	0x200

/*System-level cache (L2 cache) related Auxiliary registers */
#define ARC_REG_SLC_CFG		0x901
#define ARC_REG_SLC_CTRL	0x903
#define ARC_REG_SLC_FLUSH	0x904
#define ARC_REG_SLC_INVALIDATE	0x905
#define ARC_AUX_SLC_IVDL	0x910
#define ARC_AUX_SLC_FLDL	0x912
#define ARC_REG_SLC_RGN_START	0x914
#define ARC_REG_SLC_RGN_START1	0x915
#define ARC_REG_SLC_RGN_END	0x916
#define ARC_REG_SLC_RGN_END1	0x917

/* Bit val in SLC_CONTROL */
#define SLC_CTRL_DIS		0x001
#define SLC_CTRL_IM		0x040
#define SLC_CTRL_BUSY		0x100
#define SLC_CTRL_RGN_OP_INV	0x200

/* IO coherency related Auxiliary registers */
#define ARC_REG_IO_COH_ENABLE	0x500
#define ARC_IO_COH_ENABLE_BIT	BIT(0)
#define ARC_REG_IO_COH_PARTIAL	0x501
#define ARC_IO_COH_PARTIAL_BIT	BIT(0)
#define ARC_REG_IO_COH_AP0_BASE	0x508
#define ARC_REG_IO_COH_AP0_SIZE	0x509

#endif /* _ASM_CACHE_H */
