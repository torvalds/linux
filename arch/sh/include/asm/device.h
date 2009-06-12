/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#include <asm-generic/device.h>

struct platform_device;
/* allocate contiguous memory chunk and fill in struct resource */
int platform_resource_setup_memory(struct platform_device *pdev,
				   char *name, unsigned long memsize);

void plat_early_device_setup(void);

