/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_OS_H__
#define __NOUVEAU_OS_H__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/bitops.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/delay.h>
#include <linux/io-mapping.h>
#include <linux/acpi.h>
#include <linux/vmalloc.h>
#include <linux/dmi.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/agp_backend.h>
#include <linux/reset.h>
#include <linux/iommu.h>
#include <linux/of_device.h>

#include <asm/unaligned.h>

#include <soc/tegra/fuse.h>
#include <soc/tegra/pmc.h>

#ifndef ioread32_native
#ifdef __BIG_ENDIAN
#define ioread16_native ioread16be
#define iowrite16_native iowrite16be
#define ioread32_native  ioread32be
#define iowrite32_native iowrite32be
#else /* def __BIG_ENDIAN */
#define ioread16_native ioread16
#define iowrite16_native iowrite16
#define ioread32_native  ioread32
#define iowrite32_native iowrite32
#endif /* def __BIG_ENDIAN else */
#endif /* !ioread32_native */
#endif
