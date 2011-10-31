/*
 * BCM947xx nvram variable access
 *
 * Copyright (C) 2005 Broadcom Corporation
 * Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2010-2011 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/ssb/ssb.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/addrspace.h>
#include <asm/mach-bcm47xx/nvram.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

static char nvram_buf[NVRAM_SPACE];

/* Probe for NVRAM header */
static void early_nvram_init(void)
{
#ifdef CONFIG_BCM47XX_SSB
	struct ssb_mipscore *mcore_ssb;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	struct bcma_drv_cc *bcma_cc;
#endif
	struct nvram_header *header;
	int i;
	u32 base = 0;
	u32 lim = 0;
	u32 off;
	u32 *src, *dst;

	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		mcore_ssb = &bcm47xx_bus.ssb.mipscore;
		base = mcore_ssb->flash_window;
		lim = mcore_ssb->flash_window_size;
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_cc = &bcm47xx_bus.bcma.bus.drv_cc;
		base = bcma_cc->pflash.window;
		lim = bcma_cc->pflash.window_size;
		break;
#endif
	}

	off = FLASH_MIN;
	while (off <= lim) {
		/* Windowed flash access */
		header = (struct nvram_header *)
			KSEG1ADDR(base + off - NVRAM_SPACE);
		if (header->magic == NVRAM_HEADER)
			goto found;
		off <<= 1;
	}

	/* Try embedded NVRAM at 4 KB and 1 KB as last resorts */
	header = (struct nvram_header *) KSEG1ADDR(base + 4096);
	if (header->magic == NVRAM_HEADER)
		goto found;

	header = (struct nvram_header *) KSEG1ADDR(base + 1024);
	if (header->magic == NVRAM_HEADER)
		goto found;

	return;

found:
	src = (u32 *) header;
	dst = (u32 *) nvram_buf;
	for (i = 0; i < sizeof(struct nvram_header); i += 4)
		*dst++ = *src++;
	for (; i < header->len && i < NVRAM_SPACE; i += 4)
		*dst++ = le32_to_cpu(*src++);
}

int nvram_getenv(char *name, char *val, size_t val_len)
{
	char *var, *value, *end, *eq;

	if (!name)
		return NVRAM_ERR_INV_PARAM;

	if (!nvram_buf[0])
		early_nvram_init();

	/* Look for name=value and return value */
	var = &nvram_buf[sizeof(struct nvram_header)];
	end = nvram_buf + sizeof(nvram_buf) - 2;
	end[0] = end[1] = '\0';
	for (; *var; var = value + strlen(value) + 1) {
		eq = strchr(var, '=');
		if (!eq)
			break;
		value = eq + 1;
		if ((eq - var) == strlen(name) &&
			strncmp(var, name, (eq - var)) == 0) {
			snprintf(val, val_len, "%s", value);
			return 0;
		}
	}
	return NVRAM_ERR_ENVNOTFOUND;
}
EXPORT_SYMBOL(nvram_getenv);
