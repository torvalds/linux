/*
 * BCM947xx nvram variable access
 *
 * Copyright (C) 2005 Broadcom Corporation
 * Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2010-2012 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/ssb/ssb.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/addrspace.h>
#include <bcm47xx_nvram.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

static char nvram_buf[NVRAM_SPACE];
static const u32 nvram_sizes[] = {0x8000, 0xF000, 0x10000};

static u32 find_nvram_size(u32 end)
{
	struct nvram_header *header;
	int i;

	for (i = 0; i < ARRAY_SIZE(nvram_sizes); i++) {
		header = (struct nvram_header *)KSEG1ADDR(end - nvram_sizes[i]);
		if (header->magic == NVRAM_HEADER)
			return nvram_sizes[i];
	}

	return 0;
}

/* Probe for NVRAM header */
static int nvram_find_and_copy(u32 base, u32 lim)
{
	struct nvram_header *header;
	int i;
	u32 off;
	u32 *src, *dst;
	u32 size;

	/* TODO: when nvram is on nand flash check for bad blocks first. */
	off = FLASH_MIN;
	while (off <= lim) {
		/* Windowed flash access */
		size = find_nvram_size(base + off);
		if (size) {
			header = (struct nvram_header *)KSEG1ADDR(base + off -
								  size);
			goto found;
		}
		off <<= 1;
	}

	/* Try embedded NVRAM at 4 KB and 1 KB as last resorts */
	header = (struct nvram_header *) KSEG1ADDR(base + 4096);
	if (header->magic == NVRAM_HEADER) {
		size = NVRAM_SPACE;
		goto found;
	}

	header = (struct nvram_header *) KSEG1ADDR(base + 1024);
	if (header->magic == NVRAM_HEADER) {
		size = NVRAM_SPACE;
		goto found;
	}

	pr_err("no nvram found\n");
	return -ENXIO;

found:

	if (header->len > size)
		pr_err("The nvram size accoridng to the header seems to be bigger than the partition on flash\n");
	if (header->len > NVRAM_SPACE)
		pr_err("nvram on flash (%i bytes) is bigger than the reserved space in memory, will just copy the first %i bytes\n",
		       header->len, NVRAM_SPACE);

	src = (u32 *) header;
	dst = (u32 *) nvram_buf;
	for (i = 0; i < sizeof(struct nvram_header); i += 4)
		*dst++ = *src++;
	for (; i < header->len && i < NVRAM_SPACE && i < size; i += 4)
		*dst++ = le32_to_cpu(*src++);
	memset(dst, 0x0, NVRAM_SPACE - i);

	return 0;
}

#ifdef CONFIG_BCM47XX_SSB
static int nvram_init_ssb(void)
{
	struct ssb_mipscore *mcore = &bcm47xx_bus.ssb.mipscore;
	u32 base;
	u32 lim;

	if (mcore->pflash.present) {
		base = mcore->pflash.window;
		lim = mcore->pflash.window_size;
	} else {
		pr_err("Couldn't find supported flash memory\n");
		return -ENXIO;
	}

	return nvram_find_and_copy(base, lim);
}
#endif

#ifdef CONFIG_BCM47XX_BCMA
static int nvram_init_bcma(void)
{
	struct bcma_drv_cc *cc = &bcm47xx_bus.bcma.bus.drv_cc;
	u32 base;
	u32 lim;

#ifdef CONFIG_BCMA_NFLASH
	if (cc->nflash.boot) {
		base = BCMA_SOC_FLASH1;
		lim = BCMA_SOC_FLASH1_SZ;
	} else
#endif
	if (cc->pflash.present) {
		base = cc->pflash.window;
		lim = cc->pflash.window_size;
#ifdef CONFIG_BCMA_SFLASH
	} else if (cc->sflash.present) {
		base = cc->sflash.window;
		lim = cc->sflash.size;
#endif
	} else {
		pr_err("Couldn't find supported flash memory\n");
		return -ENXIO;
	}

	return nvram_find_and_copy(base, lim);
}
#endif

static int nvram_init(void)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		return nvram_init_ssb();
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		return nvram_init_bcma();
#endif
	}
	return -ENXIO;
}

int bcm47xx_nvram_getenv(char *name, char *val, size_t val_len)
{
	char *var, *value, *end, *eq;
	int err;

	if (!name)
		return -EINVAL;

	if (!nvram_buf[0]) {
		err = nvram_init();
		if (err)
			return err;
	}

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
			return snprintf(val, val_len, "%s", value);
		}
	}
	return -ENOENT;
}
EXPORT_SYMBOL(bcm47xx_nvram_getenv);

int bcm47xx_nvram_gpio_pin(const char *name)
{
	int i, err;
	char nvram_var[10];
	char buf[30];

	for (i = 0; i < 16; i++) {
		err = snprintf(nvram_var, sizeof(nvram_var), "gpio%i", i);
		if (err <= 0)
			continue;
		err = bcm47xx_nvram_getenv(nvram_var, buf, sizeof(buf));
		if (err <= 0)
			continue;
		if (!strcmp(name, buf))
			return i;
	}
	return -ENOENT;
}
EXPORT_SYMBOL(bcm47xx_nvram_gpio_pin);
