#ifndef BOOT_COMPRESSED_PAGETABLE_H
#define BOOT_COMPRESSED_PAGETABLE_H

#define TRAMPOLINE_32BIT_SIZE		(2 * PAGE_SIZE)

#define TRAMPOLINE_32BIT_CODE_OFFSET	PAGE_SIZE
#define TRAMPOLINE_32BIT_CODE_SIZE	0xA0

#ifndef __ASSEMBLER__

extern unsigned long *trampoline_32bit;

extern void trampoline_32bit_src(void *trampoline, bool enable_5lvl);

extern const u16 trampoline_ljmp_imm_offset;

#endif /* __ASSEMBLER__ */
#endif /* BOOT_COMPRESSED_PAGETABLE_H */
