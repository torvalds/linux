	.globl	resume1_bin_start
resume1_bin_start:
	.incbin	"arch/arm/mach-sun7i/pm/standby/super/resume1.bin"
	.globl	resume1_bin_end
resume1_bin_end:
	.align	4