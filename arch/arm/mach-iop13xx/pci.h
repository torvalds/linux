#ifndef _IOP13XX_PCI_H_
#define _IOP13XX_PCI_H_
#include <linux/io.h>
#include <mach/irqs.h>

#include <linux/types.h>

extern void __iomem *iop13xx_atue_mem_base;
extern void __iomem *iop13xx_atux_mem_base;
extern size_t iop13xx_atue_mem_size;
extern size_t iop13xx_atux_mem_size;

struct pci_sys_data;
struct hw_pci;
int iop13xx_pci_setup(int nr, struct pci_sys_data *sys);
struct pci_bus *iop13xx_scan_bus(int nr, struct pci_sys_data *);
void iop13xx_atu_select(struct hw_pci *plat_pci);
void iop13xx_pci_init(void);
void iop13xx_map_pci_memory(void);

#define IOP_PCI_STATUS_ERROR (PCI_STATUS_PARITY |	     \
			       PCI_STATUS_SIG_TARGET_ABORT | \
			       PCI_STATUS_REC_TARGET_ABORT | \
			       PCI_STATUS_REC_TARGET_ABORT | \
			       PCI_STATUS_REC_MASTER_ABORT | \
			       PCI_STATUS_SIG_SYSTEM_ERROR | \
	 		       PCI_STATUS_DETECTED_PARITY)

#define IOP13XX_ATUE_ATUISR_ERROR (IOP13XX_ATUE_STAT_HALT_ON_ERROR |  \
				    IOP13XX_ATUE_STAT_ROOT_SYS_ERR |   \
				    IOP13XX_ATUE_STAT_PCI_IFACE_ERR |  \
				    IOP13XX_ATUE_STAT_ERR_COR |	       \
				    IOP13XX_ATUE_STAT_ERR_UNCOR |      \
				    IOP13XX_ATUE_STAT_CRS |	       \
				    IOP13XX_ATUE_STAT_DET_PAR_ERR |    \
				    IOP13XX_ATUE_STAT_EXT_REC_MABORT | \
				    IOP13XX_ATUE_STAT_SIG_TABORT |     \
				    IOP13XX_ATUE_STAT_EXT_REC_TABORT | \
				    IOP13XX_ATUE_STAT_MASTER_DATA_PAR)

#define IOP13XX_ATUX_ATUISR_ERROR (IOP13XX_ATUX_STAT_TX_SCEM |        \
				    IOP13XX_ATUX_STAT_REC_SCEM |       \
				    IOP13XX_ATUX_STAT_TX_SERR |	       \
				    IOP13XX_ATUX_STAT_DET_PAR_ERR |    \
				    IOP13XX_ATUX_STAT_INT_REC_MABORT | \
				    IOP13XX_ATUX_STAT_REC_SERR |       \
				    IOP13XX_ATUX_STAT_EXT_REC_MABORT | \
				    IOP13XX_ATUX_STAT_EXT_REC_TABORT | \
				    IOP13XX_ATUX_STAT_EXT_SIG_TABORT | \
				    IOP13XX_ATUX_STAT_MASTER_DATA_PAR)

/* PCI interrupts
 */
#define ATUX_INTA IRQ_IOP13XX_XINT0
#define ATUX_INTB IRQ_IOP13XX_XINT1
#define ATUX_INTC IRQ_IOP13XX_XINT2
#define ATUX_INTD IRQ_IOP13XX_XINT3

#define ATUE_INTA IRQ_IOP13XX_ATUE_IMA
#define ATUE_INTB IRQ_IOP13XX_ATUE_IMB
#define ATUE_INTC IRQ_IOP13XX_ATUE_IMC
#define ATUE_INTD IRQ_IOP13XX_ATUE_IMD

#endif /* _IOP13XX_PCI_H_ */
