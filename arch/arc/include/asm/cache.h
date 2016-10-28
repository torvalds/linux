/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#define ARCH_DMA_MINALIGN      L1_CACHE_BYTES

extern void arc_cache_init(void);
extern char *arc_cache_mumbojumbo(int cpu_id, char *buf, int len);
extern void read_decode_cache_bcr(void);

extern int ioc_exists;
extern unsigned long perip_base, perip_end;

#endif	/* !__ASSEMBLY__ */

/* Instruction cache related Auxiliary registers */
#define ARC_REG_IC_BCR		0x77	/* Build Config reg */
#define ARC_REG_IC_IVIC		0x10
#define ARC_REG_IC_CTRL		0x11
#define ARC_REG_IC_IVIL		0x19
#define ARC_REG_IC_PTAG		0x1E
#define ARC_REG_IC_PTAG_HI	0x1F

/* Bit val in IC_CTRL */
#define IC_CTRL_CACHE_DISABLE   0x1

/* Data cache related Auxiliary registers */
#define ARC_REG_DC_BCR		0x72	/* Build Config reg */
#define ARC_REG_DC_IVDC		0x47
#define ARC_REG_DC_CTRL		0x48
#define ARC_REG_DC_IVDL		0x4A
#define ARC_REG_DC_FLSH		0x4B
#define ARC_REG_DC_FLDL		0x4C
#define ARC_REG_DC_PTAG		0x5C
#define ARC_REG_DC_PTAG_HI	0x5F

/* Bit val in DC_CTRL */
#define DC_CTRL_INV_MODE_FLUSH  0x40
#define DC_CTRL_FLUSH_STATUS    0x100

/*System-level cache (L2 cache) related Auxiliary registers */
#define ARC_REG_SLC_CFG		0x901
#define ARC_REG_SLC_CTRL	0x903
#define ARC_REG_SLC_FLUSH	0x904
#define ARC_REG_SLC_INVALIDATE	0x905
#define ARC_REG_SLC_RGN_START	0x914
#define ARC_REG_SLC_RGN_END	0x916

/* Bit val in SLC_CONTROL */
#define SLC_CTRL_IM		0x040
#define SLC_CTRL_DISABLE	0x001
#define SLC_CTRL_BUSY		0x100
#define SLC_CTRL_RGN_OP_INV	0x200

/* IO coherency related Auxiliary registers */
#define ARC_REG_IO_COH_ENABLE	0x500
#define ARC_REG_IO_COH_PARTIAL	0x501
#define ARC_REG_IO_COH_AP0_BASE	0x508
#define ARC_REG_IO_COH_AP0_SIZE	0x509

#endif /* _ASM_CACHE_H */
