#ifndef _ASM_X86_IRQ_REMAPPING_H
#define _ASM_X86_IRQ_REMAPPING_H

#define IRTE_DEST(dest) ((x2apic_mode) ? dest : dest << 8)

#endif	/* _ASM_X86_IRQ_REMAPPING_H */
