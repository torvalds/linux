#include <linux/linkage.h>
#include <linux/init.h>

#include "sleep.h"

        .section ".text.head", "ax"

ENTRY(tegra_secondary_startup)
        check_cpu_part_num 0xc09, r8, r9
        bleq    v7_invalidate_l1
        b       secondary_startup
ENDPROC(tegra_secondary_startup)
