	.globl	resume1_bin_start
resume1_bin_start:
	.incbin	"./super/resume/resume1.code"
	.globl	resume1_bin_end
resume1_bin_end:
	.align	4