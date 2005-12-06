#ifndef CELL_IOMMU_H
#define CELL_IOMMU_H

/* some constants */
enum {
	/* segment table entries */
	IOST_VALID_MASK	  = 0x8000000000000000ul,
	IOST_TAG_MASK     = 0x3000000000000000ul,
	IOST_PT_BASE_MASK = 0x000003fffffff000ul,
	IOST_NNPT_MASK	  = 0x0000000000000fe0ul,
	IOST_PS_MASK	  = 0x000000000000000ful,

	IOST_PS_4K	  = 0x1,
	IOST_PS_64K	  = 0x3,
	IOST_PS_1M	  = 0x5,
	IOST_PS_16M	  = 0x7,

	/* iopt tag register */
	IOPT_VALID_MASK   = 0x0000000200000000ul,
	IOPT_TAG_MASK	  = 0x00000001fffffffful,

	/* iopt cache register */
	IOPT_PROT_MASK	  = 0xc000000000000000ul,
	IOPT_PROT_NONE	  = 0x0000000000000000ul,
	IOPT_PROT_READ	  = 0x4000000000000000ul,
	IOPT_PROT_WRITE	  = 0x8000000000000000ul,
	IOPT_PROT_RW	  = 0xc000000000000000ul,
	IOPT_COHERENT	  = 0x2000000000000000ul,
	
	IOPT_ORDER_MASK	  = 0x1800000000000000ul,
	/* order access to same IOID/VC on same address */
	IOPT_ORDER_ADDR	  = 0x0800000000000000ul,
	/* similar, but only after a write access */
	IOPT_ORDER_WRITES = 0x1000000000000000ul,
	/* Order all accesses to same IOID/VC */
	IOPT_ORDER_VC	  = 0x1800000000000000ul,
	
	IOPT_RPN_MASK	  = 0x000003fffffff000ul,
	IOPT_HINT_MASK	  = 0x0000000000000800ul,
	IOPT_IOID_MASK	  = 0x00000000000007fful,

	IOSTO_ENABLE	  = 0x8000000000000000ul,
	IOSTO_ORIGIN	  = 0x000003fffffff000ul,
	IOSTO_HW	  = 0x0000000000000800ul,
	IOSTO_SW	  = 0x0000000000000400ul,

	IOCMD_CONF_TE	  = 0x0000800000000000ul,

	/* memory mapped registers */
	IOC_PT_CACHE_DIR  = 0x000,
	IOC_ST_CACHE_DIR  = 0x800,
	IOC_PT_CACHE_REG  = 0x910,
	IOC_ST_ORIGIN     = 0x918,
	IOC_CONF	  = 0x930,

	/* The high bit needs to be set on every DMA address,
	   only 2GB are addressable */
	CELL_DMA_VALID	  = 0x80000000,
	CELL_DMA_MASK	  = 0x7fffffff,
};


void cell_init_iommu(void);

#endif
