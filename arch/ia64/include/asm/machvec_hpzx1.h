#ifndef _ASM_IA64_MACHVEC_HPZX1_h
#define _ASM_IA64_MACHVEC_HPZX1_h

extern ia64_mv_setup_t			dig_setup;
extern ia64_mv_dma_alloc_coherent	sba_alloc_coherent;
extern ia64_mv_dma_free_coherent	sba_free_coherent;
extern ia64_mv_dma_map_single_attrs	sba_map_single_attrs;
extern ia64_mv_dma_unmap_single_attrs	sba_unmap_single_attrs;
extern ia64_mv_dma_map_sg_attrs		sba_map_sg_attrs;
extern ia64_mv_dma_unmap_sg_attrs	sba_unmap_sg_attrs;
extern ia64_mv_dma_supported		sba_dma_supported;
extern ia64_mv_dma_mapping_error	sba_dma_mapping_error;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define platform_name				"hpzx1"
#define platform_setup				dig_setup
#define platform_dma_init			machvec_noop
#define platform_dma_alloc_coherent		sba_alloc_coherent
#define platform_dma_free_coherent		sba_free_coherent
#define platform_dma_map_single_attrs		sba_map_single_attrs
#define platform_dma_unmap_single_attrs		sba_unmap_single_attrs
#define platform_dma_map_sg_attrs		sba_map_sg_attrs
#define platform_dma_unmap_sg_attrs		sba_unmap_sg_attrs
#define platform_dma_sync_single_for_cpu	machvec_dma_sync_single
#define platform_dma_sync_sg_for_cpu		machvec_dma_sync_sg
#define platform_dma_sync_single_for_device	machvec_dma_sync_single
#define platform_dma_sync_sg_for_device		machvec_dma_sync_sg
#define platform_dma_supported			sba_dma_supported
#define platform_dma_mapping_error		sba_dma_mapping_error

#endif /* _ASM_IA64_MACHVEC_HPZX1_h */
