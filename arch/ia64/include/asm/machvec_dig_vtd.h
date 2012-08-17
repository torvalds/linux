#ifndef _ASM_IA64_MACHVEC_DIG_VTD_h
#define _ASM_IA64_MACHVEC_DIG_VTD_h

extern ia64_mv_setup_t			dig_setup;
extern ia64_mv_dma_init			pci_iommu_alloc;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define ia64_platform_name			"dig_vtd"
#define platform_setup				dig_setup
#define platform_dma_init			pci_iommu_alloc

#endif /* _ASM_IA64_MACHVEC_DIG_VTD_h */
