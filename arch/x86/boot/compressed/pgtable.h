#ifndef BOOT_COMPRESSED_PAGETABLE_H
#define BOOT_COMPRESSED_PAGETABLE_H

#define TRAMPOLINE_32BIT_SIZE		(2 * PAGE_SIZE)

#ifndef __ASSEMBLER__

extern unsigned long *trampoline_32bit;

#endif /* __ASSEMBLER__ */
#endif /* BOOT_COMPRESSED_PAGETABLE_H */
