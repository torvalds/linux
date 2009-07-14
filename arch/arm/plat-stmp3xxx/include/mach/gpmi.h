#ifndef __MACH_GPMI_H

#include <linux/mtd/partitions.h>
#include <mach/regs-gpmi.h>

struct gpmi_platform_data {
	void *pins;
	int nr_parts;
	struct mtd_partition *parts;
	const char *part_types[];
};
#endif
