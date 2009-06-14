#ifndef _MACH_MMC_H
#define _MACH_MMC_H

#include <mach/regs-ssp.h>

struct stmp3xxxmmc_platform_data {
	int (*get_wp)(void);
	unsigned long (*setclock)(void __iomem *base, unsigned long);
	void (*cmd_pullup)(int);
	int  (*hw_init)(void);
	void (*hw_release)(void);
};

#endif
