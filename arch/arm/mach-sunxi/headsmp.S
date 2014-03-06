#include <linux/linkage.h>
#include <linux/init.h>

        .section ".text.head", "ax"

ENTRY(sun6i_secondary_startup)
	msr	cpsr_fsxc, #0xd3
	b	secondary_startup
ENDPROC(sun6i_secondary_startup)
