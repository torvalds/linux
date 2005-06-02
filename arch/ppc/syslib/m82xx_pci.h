
#ifndef _PPC_KERNEL_M82XX_PCI_H
#define _PPC_KERNEL_M82XX_PCI_H

#include <asm/m8260_pci.h>
/*
 *   Local->PCI map (from CPU)                             controlled by
 *   MPC826x master window
 *
 *   0xF6000000 - 0xF7FFFFFF    IO space
 *   0x80000000 - 0xBFFFFFFF    CPU2PCI memory space       PCIBR0
 *
 *   0x80000000 - 0x9FFFFFFF    PCI Mem with prefetch      (Outbound ATU #1)
 *   0xA0000000 - 0xBFFFFFFF    PCI Mem w/o  prefetch      (Outbound ATU #2)
 *   0xF6000000 - 0xF7FFFFFF    32-bit PCI IO              (Outbound ATU #3)
 *
 *   PCI->Local map (from PCI)
 *   MPC826x slave window                                  controlled by
 *
 *   0x00000000 - 0x07FFFFFF    MPC826x local memory       (Inbound ATU #1)
 */

/*
 * Slave window that allows PCI masters to access MPC826x local memory.
 * This window is set up using the first set of Inbound ATU registers
 */

#ifndef M82xx_PCI_SLAVE_MEM_LOCAL
#define M82xx_PCI_SLAVE_MEM_LOCAL	(((struct bd_info *)__res)->bi_memstart)
#define M82xx_PCI_SLAVE_MEM_BUS		(((struct bd_info *)__res)->bi_memstart)
#define M82xx_PCI_SLAVE_MEM_SIZE	(((struct bd_info *)__res)->bi_memsize)
#endif

/*
 * This is the window that allows the CPU to access PCI address space.
 * It will be setup with the SIU PCIBR0 register. All three PCI master
 * windows, which allow the CPU to access PCI prefetch, non prefetch,
 * and IO space (see below), must all fit within this window.
 */

#ifndef M82xx_PCI_LOWER_MEM
#define M82xx_PCI_LOWER_MEM		0x80000000
#define M82xx_PCI_UPPER_MEM		0x9fffffff
#define M82xx_PCI_MEM_OFFSET		0x00000000
#define M82xx_PCI_MEM_SIZE		0x20000000
#endif

#ifndef M82xx_PCI_LOWER_MMIO
#define M82xx_PCI_LOWER_MMIO		0xa0000000
#define M82xx_PCI_UPPER_MMIO		0xafffffff
#define M82xx_PCI_MMIO_OFFSET		0x00000000
#define M82xx_PCI_MMIO_SIZE		0x20000000
#endif

#ifndef M82xx_PCI_LOWER_IO
#define M82xx_PCI_LOWER_IO		0x00000000
#define M82xx_PCI_UPPER_IO		0x01ffffff
#define M82xx_PCI_IO_BASE		0xf6000000
#define M82xx_PCI_IO_SIZE		0x02000000
#endif

#ifndef M82xx_PCI_PRIM_WND_SIZE
#define M82xx_PCI_PRIM_WND_SIZE 	~(M82xx_PCI_IO_SIZE - 1U)
#define M82xx_PCI_PRIM_WND_BASE		(M82xx_PCI_IO_BASE)
#endif

#ifndef M82xx_PCI_SEC_WND_SIZE
#define M82xx_PCI_SEC_WND_SIZE 		~(M82xx_PCI_MEM_SIZE + M82xx_PCI_MMIO_SIZE - 1U)
#define M82xx_PCI_SEC_WND_BASE 		(M82xx_PCI_LOWER_MEM)
#endif

#ifndef POTA_ADDR_SHIFT
#define POTA_ADDR_SHIFT		12
#endif

#ifndef PITA_ADDR_SHIFT
#define PITA_ADDR_SHIFT		12
#endif

#ifndef _IO_BASE
#define _IO_BASE isa_io_base
#endif

#ifdef CONFIG_8260_PCI9
struct pci_controller;
extern void setup_m8260_indirect_pci(struct pci_controller* hose,
					u32 cfg_addr, u32 cfg_data);
#else
#define setup_m8260_indirect_pci setup_indirect_pci
#endif

#endif /* _PPC_KERNEL_M8260_PCI_H */
