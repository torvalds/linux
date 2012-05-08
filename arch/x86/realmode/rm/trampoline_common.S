	.section ".rodata","a"

	.balign	4
tr_idt: .fill 1, 6, 0

	.bss

	.balign	4
GLOBAL(trampoline_status)	.space	4

GLOBAL(trampoline_header)
#ifdef CONFIG_X86_32
	tr_start:		.space	4
	tr_gdt:			.space	6
#else
	tr_start:		.space	8
#endif
END(trampoline_header)

#ifdef CONFIG_X86_64
	.balign	PAGE_SIZE
GLOBAL(trampoline_pgd)		.space	PAGE_SIZE
#endif
