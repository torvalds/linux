#include <asm/iommu.h>
#include <asm/machvec.h>

extern ia64_mv_send_ipi_t ia64_send_ipi;
extern ia64_mv_global_tlb_purge_t ia64_global_tlb_purge;
extern ia64_mv_dma_get_required_mask ia64_dma_get_required_mask;
extern ia64_mv_irq_to_vector __ia64_irq_to_vector;
extern ia64_mv_local_vector_to_irq __ia64_local_vector_to_irq;
extern ia64_mv_pci_get_legacy_mem_t ia64_pci_get_legacy_mem;
extern ia64_mv_pci_legacy_read_t ia64_pci_legacy_read;
extern ia64_mv_pci_legacy_write_t ia64_pci_legacy_write;

extern ia64_mv_inb_t __ia64_inb;
extern ia64_mv_inw_t __ia64_inw;
extern ia64_mv_inl_t __ia64_inl;
extern ia64_mv_outb_t __ia64_outb;
extern ia64_mv_outw_t __ia64_outw;
extern ia64_mv_outl_t __ia64_outl;
extern ia64_mv_mmiowb_t __ia64_mmiowb;
extern ia64_mv_readb_t __ia64_readb;
extern ia64_mv_readw_t __ia64_readw;
extern ia64_mv_readl_t __ia64_readl;
extern ia64_mv_readq_t __ia64_readq;
extern ia64_mv_readb_t __ia64_readb_relaxed;
extern ia64_mv_readw_t __ia64_readw_relaxed;
extern ia64_mv_readl_t __ia64_readl_relaxed;
extern ia64_mv_readq_t __ia64_readq_relaxed;

#define MACHVEC_HELPER(name)									\
 struct ia64_machine_vector machvec_##name __attribute__ ((unused, __section__ (".machvec")))	\
	= MACHVEC_INIT(name);

#define MACHVEC_DEFINE(name)	MACHVEC_HELPER(name)

MACHVEC_DEFINE(MACHVEC_PLATFORM_NAME)
