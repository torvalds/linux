#ifndef _ASM_X86_IRQ_REMAPPING_H
#define _ASM_X86_IRQ_REMAPPING_H

extern int x2apic;

#define IRTE_DEST(dest) ((x2apic) ? dest : dest << 8)

#endif	/* _ASM_X86_IRQ_REMAPPING_H */
