/*
 *  Copyright (C) 2005, Broadcom Corporation
 *  Copyright (C) 2006, Felix Fietkau <nbd@openwrt.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __BCM47XX_NVRAM_H
#define __BCM47XX_NVRAM_H

#include <linux/types.h>
#include <linux/kernel.h>

struct nvram_header {
	u32 magic;
	u32 len;
	u32 crc_ver_init;	/* 0:7 crc, 8:15 ver, 16:31 sdram_init */
	u32 config_refresh;	/* 0:15 sdram_config, 16:31 sdram_refresh */
	u32 config_ncdl;	/* ncdl values for memc */
};

#define NVRAM_HEADER		0x48534C46	/* 'FLSH' */
#define NVRAM_VERSION		1
#define NVRAM_HEADER_SIZE	20
#define NVRAM_SPACE		0x8000

#define FLASH_MIN		0x00020000	/* Minimum flash size */

#define NVRAM_MAX_VALUE_LEN 255
#define NVRAM_MAX_PARAM_LEN 64

extern int bcm47xx_nvram_getenv(char *name, char *val, size_t val_len);

static inline void bcm47xx_nvram_parse_macaddr(char *buf, u8 macaddr[6])
{
	if (strchr(buf, ':'))
		sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &macaddr[0],
			&macaddr[1], &macaddr[2], &macaddr[3], &macaddr[4],
			&macaddr[5]);
	else if (strchr(buf, '-'))
		sscanf(buf, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx", &macaddr[0],
			&macaddr[1], &macaddr[2], &macaddr[3], &macaddr[4],
			&macaddr[5]);
	else
		printk(KERN_WARNING "Can not parse mac address: %s\n", buf);
}

int bcm47xx_nvram_gpio_pin(const char *name);

#endif /* __BCM47XX_NVRAM_H */
