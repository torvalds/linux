
__INITDATA

	.globl vdso_start, vdso_end
vdso_start:
	.incbin "arch/x86/um/vdso/vdso.so"
vdso_end:

__FINIT
