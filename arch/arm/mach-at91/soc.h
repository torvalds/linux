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

#endif /* __AT91_SOC_H */
