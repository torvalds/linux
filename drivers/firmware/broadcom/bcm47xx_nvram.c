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

#include <linux/io.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mtd/mtd.h>
#include <linux/bcm47xx_nvram.h>

#define NVRAM_MAGIC			0x48534C46	/* 'FLSH' */
#define NVRAM_SPACE			0x10000
#define NVRAM_MAX_GPIO_ENTRIES		32
#define NVRAM_MAX_GPIO_VALUE_LEN	30

#define FLASH_MIN		0x00020000	/* Minimum flash size */

struct nvram_header {
	u32 magic;
	u32 len;
	u32 crc_ver_init;	/* 0:7 crc, 8:15 ver, 16:31 sdram_init */
	u32 config_refresh;	/* 0:15 sdram_config, 16:31 sdram_refresh */
	u32 config_ncdl;	/* ncdl values for memc */
};

static char nvram_buf[NVRAM_SPACE];
static size_t nvram_len;
static const u32 nvram_sizes[] = {0x8000, 0xF000, 0x10000};

static u32 find_nvram_size(void __iomem *end)
{
	struct nvram_header __iomem *header;
	int i;

	for (i = 0; i < ARRAY_SIZE(nvram_sizes); i++) {
		header = (struct nvram_header *)(end - nvram_sizes[i]);
		if (header->magic == NVRAM_MAGIC)
			return nvram_sizes[i];
	}

	return 0;
}

/* Probe for NVRAM header */
static int nvram_find_and_copy(void __iomem *iobase, u32 lim)
{
	struct nvram_header __iomem *header;
	u32 off;
	u32 size;

	if (nvram_len) {
		pr_warn("nvram already initialized\n");
		return -EEXIST;
	}

	/* TODO: when nvram is on nand flash check for bad blocks first. */
	off = FLASH_MIN;
	while (off <= lim) {
		/* Windowed flash access */
		size = find_nvram_size(iobase + off);
		if (size) {
			header = (struct nvram_header *)(iobase + off - size);
			goto found;
		}
		off <<= 1;
	}

	/* Try embedded NVRAM at 4 KB and 1 KB as last resorts */
	header = (struct nvram_header *)(iobase + 4096);
	if (header->magic == NVRAM_MAGIC) {
		size = NVRAM_SPACE;
		goto found;
	}

	header = (struct nvram_header *)(iobase + 1024);
	if (header->magic == NVRAM_MAGIC) {
		size = NVRAM_SPACE;
		goto found;
	}

	pr_err("no nvram found\n");
	return -ENXIO;

found:
	__ioread32_copy(nvram_buf, header, sizeof(*header) / 4);
	header = (struct nvram_header *)nvram_buf;
	nvram_len = header->len;
	if (nvram_len > size) {
		pr_err("The nvram size according to the header seems to be bigger than the partition on flash\n");
		nvram_len = size;
	}
	if (nvram_len >= NVRAM_SPACE) {
		pr_err("nvram on flash (%i bytes) is bigger than the reserved space in memory, will just copy the first %i bytes\n",
		       header->len, NVRAM_SPACE - 1);
		nvram_len = NVRAM_SPACE - 1;
	}
	/* proceed reading data after header */
	__ioread32_copy(nvram_buf + sizeof(*header), header + 1,
			DIV_ROUND_UP(nvram_len, 4));
	nvram_buf[NVRAM_SPACE - 1] = '\0';

	return 0;
}

/*
 * On bcm47xx we need access to the NVRAM very early, so we can't use mtd
 * subsystem to access flash. We can't even use platform device / driver to
 * store memory offset.
 * To handle this we provide following symbol. It's supposed to be called as
 * soon as we get info about flash device, before any NVRAM entry is needed.
 */
int bcm47xx_nvram_init_from_mem(u32 base, u32 lim)
{
	void __iomem *iobase;
	int err;

	iobase = ioremap_nocache(base, lim);
	if (!iobase)
		return -ENOMEM;

	err = nvram_find_and_copy(iobase, lim);

	iounmap(iobase);

	return err;
}

static int nvram_init(void)
{
#ifdef CONFIG_MTD
	struct mtd_info *mtd;
	struct nvram_header header;
	size_t bytes_read;
	int err;

	mtd = get_mtd_device_nm("nvram");
	if (IS_ERR(mtd))
		return -ENODEV;

	err = mtd_read(mtd, 0, sizeof(header), &bytes_read, (uint8_t *)&header);
	if (!err && header.magic == NVRAM_MAGIC &&
	    header.len > sizeof(header)) {
		nvram_len = header.len;
		if (nvram_len >= NVRAM_SPACE) {
			pr_err("nvram on flash (%i bytes) is bigger than the reserved space in memory, will just copy the first %i bytes\n",
				header.len, NVRAM_SPACE);
			nvram_len = NVRAM_SPACE - 1;
		}

		err = mtd_read(mtd, 0, nvram_len, &nvram_len,
			       (u8 *)nvram_buf);
		return err;
	}
#endif

	return -ENXIO;
}

int bcm47xx_nvram_getenv(const char *name, char *val, size_t val_len)
{
	char *var, *value, *end, *eq;
	int err;

	if (!name)
		return -EINVAL;

	if (!nvram_len) {
		err = nvram_init();
		if (err)
			return err;
	}

	/* Look for name=value and return value */
	var = &nvram_buf[sizeof(struct nvram_header)];
	end = nvram_buf + sizeof(nvram_buf);
	while (var < end && *var) {
		eq = strchr(var, '=');
		if (!eq)
			break;
		value = eq + 1;
		if (eq - var == strlen(name) &&
		    strncmp(var, name, eq - var) == 0)
			return snprintf(val, val_len, "%s", value);
		var = value + strlen(value) + 1;
	}
	return -ENOENT;
}
EXPORT_SYMBOL(bcm47xx_nvram_getenv);

int bcm47xx_nvram_gpio_pin(const char *name)
{
	int i, err;
	char nvram_var[] = "gpioXX";
	char buf[NVRAM_MAX_GPIO_VALUE_LEN];

	/* TODO: Optimize it to don't call getenv so many times */
	for (i = 0; i < NVRAM_MAX_GPIO_ENTRIES; i++) {
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

char *bcm47xx_nvram_get_contents(size_t *nvram_size)
{
	int err;
	char *nvram;

	if (!nvram_len) {
		err = nvram_init();
		if (err)
			return NULL;
	}

	*nvram_size = nvram_len - sizeof(struct nvram_header);
	nvram = vmalloc(*nvram_size);
	if (!nvram)
		return NULL;
	memcpy(nvram, &nvram_buf[sizeof(struct nvram_header)], *nvram_size);

	return nvram;
}
EXPORT_SYMBOL(bcm47xx_nvram_get_contents);

MODULE_LICENSE("GPL v2");
