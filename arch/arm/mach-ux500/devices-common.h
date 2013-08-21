/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_COMMON_H
#define __DEVICES_COMMON_H

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/sys_soc.h>
#include <linux/amba/bus.h>

struct spi_master_cntlr;

static inline struct amba_device *
dbx500_add_spi(struct device *parent, const char *name, resource_size_t base,
	       int irq, struct spi_master_cntlr *pdata,
	       u32 periphid)
{
	return amba_ahb_device_add(parent, name, base, SZ_4K, irq, 0,
				   pdata, periphid);
}

struct amba_pl011_data;

static inline struct amba_device *
dbx500_add_uart(struct device *parent, const char *name, resource_size_t base,
		int irq, struct amba_pl011_data *pdata)
{
	return amba_ahb_device_add(parent, name, base, SZ_4K, irq, 0, pdata, 0);
}

static inline struct amba_device *
dbx500_add_rtc(struct device *parent, resource_size_t base, int irq)
{
	return amba_apb_device_add(parent, "rtc-pl031", base, SZ_4K, irq,
				0, NULL, 0);
}

struct nmk_gpio_platform_data;

void dbx500_add_gpios(struct device *parent, resource_size_t *base, int num,
		      int irq, struct nmk_gpio_platform_data *pdata);

static inline void
dbx500_add_pinctrl(struct device *parent, const char *name,
		   resource_size_t base)
{
	struct resource res[] = {
		DEFINE_RES_MEM(base, SZ_8K),
	};
	struct platform_device_info pdevinfo = {
		.parent = parent,
		.name = name,
		.id = -1,
		.res = res,
		.num_res = ARRAY_SIZE(res),
	};

	platform_device_register_full(&pdevinfo);
}

#endif
