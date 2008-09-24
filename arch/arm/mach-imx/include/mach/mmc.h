#ifndef ASMARM_ARCH_MMC_H
#define ASMARM_ARCH_MMC_H

#include <linux/mmc/host.h>

struct device;

struct imxmmc_platform_data {
	int (*card_present)(struct device *);
	int (*get_ro)(struct device *);
};

extern void imx_set_mmc_info(struct imxmmc_platform_data *info);

#endif
