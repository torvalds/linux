#include <linux/linkage.h>
#include <linux/init.h>

#include "sleep.h"

        .section ".text.head", "ax"

ENTRY(tegra_secondary_startup)
        bl      v7_invalidate_l1
	/* Enable coresight */
	mov32	r0, 0xC5ACCE55
	mcr	p14, 0, r0, c7, c12, 6
        b       secondary_startup
ENDPROC(tegra_secondary_startup)
