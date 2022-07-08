/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_GART_H
#define _ASM_X86_GART_H

#include <asm/e820/api.h>

extern void set_up_gart_resume(u32, u32);

extern int fallback_aper_order;
extern int fallback_aper_force;
extern int fix_aperture;

/* PTE bits. */
#define GPTE_VALID	1
#define GPTE_COHERENT	2

/* Aperture control register bits. */
#define GARTEN		(1<<0)
#define DISGARTCPU	(1<<4)
#define DISGARTIO	(1<<5)
#define DISTLBWALKPRB	(1<<6)

/* GART cache control register bits. */
#define INVGART		(1<<0)
#define GARTPTEERR	(1<<1)

/* K8 On-cpu GART registers */
#define AMD64_GARTAPERTURECTL	0x90
#define AMD64_GARTAPERTUREBASE	0x94
#define AMD64_GARTTABLEBASE	0x98
#define AMD64_GARTCACHECTL	0x9c

#ifdef CONFIG_GART_IOMMU
extern int gart_iommu_aperture;
extern int gart_iommu_aperture_allowed;
extern int gart_iommu_aperture_disabled;

extern void early_gart_iommu_check(void);
extern int gart_iommu_init(void);
extern void __init gart_parse_options(char *);
void gart_iommu_hole_init(void);

#else
#define gart_iommu_aperture            0
#define gart_iommu_aperture_allowed    0
#define gart_iommu_aperture_disabled   1

static inline void early_gart_iommu_check(void)
{
}
static inline void gart_parse_options(char *options)
{
}
static inline void gart_iommu_hole_init(void)
{
}
#endif

extern int agp_amd64_init(void);

static inline void gart_set_size_and_enable(struct pci_dev *dev, u32 order)
{
	u32 ctl;

	/*
	 * Don't enable translation but enable GART IO and CPU accesses.
	 * Also, set DISTLBWALKPRB since GART tables memory is UC.
	 */
	ctl = order << 1;

	pci_write_config_dword(dev, AMD64_GARTAPERTURECTL, ctl);
}

static inline void enable_gart_translation(struct pci_dev *dev, u64 addr)
{
	u32 tmp, ctl;

	/* address of the mappings table */
	addr >>= 12;
	tmp = (u32) addr<<4;
	tmp &= ~0xf;
	pci_write_config_dword(dev, AMD64_GARTTABLEBASE, tmp);

	/* Enable GART translation for this hammer. */
	pci_read_config_dword(dev, AMD64_GARTAPERTURECTL, &ctl);
	ctl |= GARTEN | DISTLBWALKPRB;
	ctl &= ~(DISGARTCPU | DISGARTIO);
	pci_write_config_dword(dev, AMD64_GARTAPERTURECTL, ctl);
}

static inline int aperture_valid(u64 aper_base, u32 aper_size, u32 min_size)
{
	if (!aper_base)
		return 0;

	if (aper_base + aper_size > 0x100000000ULL) {
		printk(KERN_INFO "Aperture beyond 4GB. Ignoring.\n");
		return 0;
	}
	if (e820__mapped_any(aper_base, aper_base + aper_size, E820_TYPE_RAM)) {
		printk(KERN_INFO "Aperture pointing to e820 RAM. Ignoring.\n");
		return 0;
	}
	if (aper_size < min_size) {
		printk(KERN_INFO "Aperture too small (%d MB) than (%d MB)\n",
				 aper_size>>20, min_size>>20);
		return 0;
	}

	return 1;
}

#endif /* _ASM_X86_GART_H */
