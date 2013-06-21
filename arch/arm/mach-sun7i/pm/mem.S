	.globl	suspend_bin_start
suspend_bin_start:
	.incbin	"arch/arm/mach-sun7i/pm/standby/super/suspend/suspend.code"
	.globl	suspend_bin_end
suspend_bin_end:
	.align	4
	.globl	resume0_bin_start
resume0_bin_start:
	.incbin	"arch/arm/mach-sun7i/pm/standby/super/resume/resume0.code"
	.globl	resume0_bin_end
resume0_bin_end:
	.align	4
	
	
