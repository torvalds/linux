#ifndef __ASM_S390_PCI_H
#define __ASM_S390_PCI_H

/* S/390 systems don't have a PCI bus. This file is just here because some stupid .c code
 * includes it even if CONFIG_PCI is not set.
 */
#define PCI_DMA_BUS_IS_PHYS (0)

#endif /* __ASM_S390_PCI_H */

