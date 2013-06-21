#include <linux/linkage.h>
#include <linux/init.h>

        .section ".text.head", "ax"
	__CPUINIT

ENTRY(sun7i_secondary_startup)
	msr	cpsr_fsxc, #0xd3
	mov r0, #0
	ldr r1, =0xf35
        b       secondary_startup
ENDPROC(sun7i_secondary_startup)
