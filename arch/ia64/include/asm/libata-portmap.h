#ifndef __ASM_IA64_LIBATA_PORTMAP_H
#define __ASM_IA64_LIBATA_PORTMAP_H

#define ATA_PRIMARY_IRQ(dev)	isa_irq_to_vector(14)

#define ATA_SECONDARY_IRQ(dev)	isa_irq_to_vector(15)

#endif
