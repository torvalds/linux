/* linux/arch/arm/mach-s5pv310/include/mach/sysmmu.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung sysmmu driver for S5PV310
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARM_ARCH_SYSMMU_H
#define __ASM_ARM_ARCH_SYSMMU_H __FILE__

#define S5PV310_SYSMMU_TOTAL_IPNUM	16
#define S5P_SYSMMU_TOTAL_IPNUM		S5PV310_SYSMMU_TOTAL_IPNUM

enum s5pv310_sysmmu_ips {
	SYSMMU_MDMA,
	SYSMMU_SSS,
	SYSMMU_FIMC0,
	SYSMMU_FIMC1,
	SYSMMU_FIMC2,
	SYSMMU_FIMC3,
	SYSMMU_JPEG,
	SYSMMU_FIMD0,
	SYSMMU_FIMD1,
	SYSMMU_PCIe,
	SYSMMU_G2D,
	SYSMMU_ROTATOR,
	SYSMMU_MDMA2,
	SYSMMU_TV,
	SYSMMU_MFC_L,
	SYSMMU_MFC_R,
};

static char *sysmmu_ips_name[S5PV310_SYSMMU_TOTAL_IPNUM] = {
	"SYSMMU_MDMA"	,
	"SYSMMU_SSS"	,
	"SYSMMU_FIMC0"	,
	"SYSMMU_FIMC1"	,
	"SYSMMU_FIMC2"	,
	"SYSMMU_FIMC3"	,
	"SYSMMU_JPEG"	,
	"SYSMMU_FIMD0"	,
	"SYSMMU_FIMD1"	,
	"SYSMMU_PCIe"	,
	"SYSMMU_G2D"	,
	"SYSMMU_ROTATOR",
	"SYSMMU_MDMA2"	,
	"SYSMMU_TV"	,
	"SYSMMU_MFC_L"	,
	"SYSMMU_MFC_R"	,
};

typedef enum s5pv310_sysmmu_ips sysmmu_ips;

struct sysmmu_tt_info {
	unsigned long *pgd;
	unsigned long pgd_paddr;
	unsigned long *pte;
};

struct sysmmu_controller {
	const char		*name;

	/* channels registers */
	void __iomem		*regs;

	/* channel irq */
	unsigned int		irq;

	sysmmu_ips		ips;

	/* Translation Table Info. */
	struct sysmmu_tt_info	*tt_info;

	struct resource		*mem;
	struct device		*dev;

	/* SysMMU controller enable - true : enable */
	bool			enable;
};

/**
 * s5p_sysmmu_enable() - enable system mmu of ip
 * @ips: The ip connected system mmu.
 *
 * This function enable system mmu to transfer address
 * from virtual address to physical address
 */
int s5p_sysmmu_enable(sysmmu_ips ips);

/**
 * s5p_sysmmu_disable() - disable sysmmu mmu of ip
 * @ips: The ip connected system mmu.
 *
 * This function disable system mmu to transfer address
 * from virtual address to physical address
 */
int s5p_sysmmu_disable(sysmmu_ips ips);

/**
 * s5p_sysmmu_set_tablebase_pgd() - set page table base address to refer page table
 * @ips: The ip connected system mmu.
 * @pgd: The page table base address.
 *
 * This function set page table base address
 * When system mmu transfer address from virtaul address to physical address,
 * system mmu refer address information from page table
 */
int s5p_sysmmu_set_tablebase_pgd(sysmmu_ips ips, unsigned long pgd);

/**
 * s5p_sysmmu_tlb_invalidate() - flush all TLB entry in system mmu
 * @ips: The ip connected system mmu.
 *
 * This function flush all TLB entry in system mmu
 */
int s5p_sysmmu_tlb_invalidate(sysmmu_ips ips);
#endif /* __ASM_ARM_ARCH_SYSMMU_H */
