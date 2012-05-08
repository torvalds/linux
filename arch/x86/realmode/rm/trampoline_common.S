	.section ".rodata","a"

#ifdef CONFIG_X86_64
	# Duplicate the global descriptor table
	# so the kernel can live anywhere
	.balign	16
	.globl tr_gdt
tr_gdt:
	.short	tr_gdt_end - tr_gdt - 1	# gdt limit
	.long	pa_tr_gdt
	.short	0
	.quad	0x00cf9b000000ffff	# __KERNEL32_CS
	.quad	0x00af9b000000ffff	# __KERNEL_CS
	.quad	0x00cf93000000ffff	# __KERNEL_DS
tr_gdt_end:
#endif

	.balign	4
tr_idt: .fill 1, 6, 0

	.bss

	.balign	4
GLOBAL(trampoline_status)	.space	4

	.balign	8
GLOBAL(trampoline_header)
#ifdef CONFIG_X86_32
	tr_start:		.space	4
	tr_gdt_pad:		.space	2
	tr_gdt:			.space	6
#else
	tr_start:		.space	8
	GLOBAL(tr_cr4)		.space	4
	GLOBAL(tr_efer)		.space	8
#endif
END(trampoline_header)

#ifdef CONFIG_X86_64
	.balign	PAGE_SIZE
GLOBAL(trampoline_pgd)		.space	PAGE_SIZE
#endif
