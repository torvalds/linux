/*
 * Copyright (C) 2015 Atmel
 *
 * Boris Brezillon <boris.brezillon@free-electrons.com
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */

#ifndef __AT91_SOC_H
#define __AT91_SOC_H

#include <linux/sys_soc.h>

struct at91_soc {
	u32 cidr_match;
	u32 exid_match;
	const char *name;
	const char *family;
};

#define AT91_SOC(__cidr, __exid, __name, __family)		\
	{							\
		.cidr_match = (__cidr),				\
		.exid_match = (__exid),				\
		.name = (__name),				\
		.family = (__family),				\
	}

struct soc_device * __init
at91_soc_init(const struct at91_soc *socs);

#define AT91RM9200_CIDR_MATCH		0x09290780

#define AT91SAM9260_CIDR_MATCH		0x019803a0
#define AT91SAM9261_CIDR_MATCH		0x019703a0
#define AT91SAM9263_CIDR_MATCH		0x019607a0
#define AT91SAM9G20_CIDR_MATCH		0x019905a0
#define AT91SAM9RL64_CIDR_MATCH		0x019b03a0
#define AT91SAM9G45_CIDR_MATCH		0x019b05a0
#define AT91SAM9X5_CIDR_MATCH		0x019a05a0
#define AT91SAM9N12_CIDR_MATCH		0x019a07a0

#define AT91SAM9M11_EXID_MATCH		0x00000001
#define AT91SAM9M10_EXID_MATCH		0x00000002
#define AT91SAM9G46_EXID_MATCH		0x00000003
#define AT91SAM9G45_EXID_MATCH		0x00000004

#define AT91SAM9G15_EXID_MATCH		0x00000000
#define AT91SAM9G35_EXID_MATCH		0x00000001
#define AT91SAM9X35_EXID_MATCH		0x00000002
#define AT91SAM9G25_EXID_MATCH		0x00000003
#define AT91SAM9X25_EXID_MATCH		0x00000004

#define AT91SAM9CN12_EXID_MATCH		0x00000005
#define AT91SAM9N12_EXID_MATCH		0x00000006
#define AT91SAM9CN11_EXID_MATCH		0x00000009

#define AT91SAM9XE128_CIDR_MATCH	0x329973a0
#define AT91SAM9XE256_CIDR_MATCH	0x329a93a0
#define AT91SAM9XE512_CIDR_MATCH	0x329aa3a0

#define SAMA5D3_CIDR_MATCH		0x0a5c07c0
#define SAMA5D31_EXID_MATCH		0x00444300
#define SAMA5D33_EXID_MATCH		0x00414300
#define SAMA5D34_EXID_MATCH		0x00414301
#define SAMA5D35_EXID_MATCH		0x00584300
#define SAMA5D36_EXID_MATCH		0x00004301

#define SAMA5D4_CIDR_MATCH		0x0a5c07c0
#define SAMA5D41_EXID_MATCH		0x00000001
#define SAMA5D42_EXID_MATCH		0x00000002
#define SAMA5D43_EXID_MATCH		0x00000003
#define SAMA5D44_EXID_MATCH		0x00000004

#endif /* __AT91_SOC_H */
