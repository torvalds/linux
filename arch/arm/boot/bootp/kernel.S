	.globl	kernel_start
kernel_start:
	.incbin	"arch/arm/boot/zImage"
	.globl	kernel_end
kernel_end:
	.align	2
