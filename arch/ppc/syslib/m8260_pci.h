
#ifndef _PPC_KERNEL_M8260_PCI_H
#define _PPC_KERNEL_M8260_PCI_H

#include <asm/m8260_pci.h>

/*
 *   Local->PCI map (from CPU)                             controlled by
 *   MPC826x master window
 *
 *   0x80000000 - 0xBFFFFFFF    Total CPU2PCI space        PCIBR0
 *                       
 *   0x80000000 - 0x9FFFFFFF    PCI Mem with prefetch      (Outbound ATU #1)
 *   0xA0000000 - 0xAFFFFFFF    PCI Mem w/o  prefetch      (Outbound ATU #2)
 *   0xB0000000 - 0xB0FFFFFF    32-bit PCI IO              (Outbound ATU #3)
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

#ifndef MPC826x_PCI_SLAVE_MEM_LOCAL
#define MPC826x_PCI_SLAVE_MEM_LOCAL	(((struct bd_info *)__res)->bi_memstart)
#define MPC826x_PCI_SLAVE_MEM_BUS	(((struct bd_info *)__res)->bi_memstart)
#define MPC826x_PCI_SLAVE_MEM_SIZE	(((struct bd_info *)__res)->bi_memsize)
#endif

/* 
 * This is the window that allows the CPU to access PCI address space.
 * It will be setup with the SIU PCIBR0 register. All three PCI master
 * windows, which allow the CPU to access PCI prefetch, non prefetch,
 * and IO space (see below), must all fit within this window. 
 */
#ifndef MPC826x_PCI_BASE
#define MPC826x_PCI_BASE	0x80000000
#define MPC826x_PCI_MASK	0xc0000000
#endif

#ifndef MPC826x_PCI_LOWER_MEM
#define MPC826x_PCI_LOWER_MEM  0x80000000
#define MPC826x_PCI_UPPER_MEM  0x9fffffff
#define MPC826x_PCI_MEM_OFFSET 0x00000000
#endif

#ifndef MPC826x_PCI_LOWER_MMIO
#define MPC826x_PCI_LOWER_MMIO  0xa0000000
#define MPC826x_PCI_UPPER_MMIO  0xafffffff
#define MPC826x_PCI_MMIO_OFFSET 0x00000000
#endif

#ifndef MPC826x_PCI_LOWER_IO
#define MPC826x_PCI_LOWER_IO   0x00000000
#define MPC826x_PCI_UPPER_IO   0x00ffffff
#define MPC826x_PCI_IO_BASE    0xb0000000
#define MPC826x_PCI_IO_SIZE    0x01000000
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
