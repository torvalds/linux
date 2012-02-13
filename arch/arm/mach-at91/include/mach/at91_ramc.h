/*
 * Header file for the Atmel RAM Controller
 *
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2 only
 */

#ifndef __AT91_RAMC_H__
#define __AT91_RAMC_H__

#ifndef __ASSEMBLY__
extern void __iomem *at91_ramc_base[];

#define at91_ramc_read(id, field) \
	__raw_readl(at91_ramc_base[id] + field)

#define at91_ramc_write(id, field, value) \
	__raw_writel(value, at91_ramc_base[id] + field)
#else
.extern at91_ramc_base
#endif

#ifdef CONFIG_ARCH_AT91RM9200
#include <mach/at91rm9200_mc.h>
#else
#include <mach/at91sam9_ddrsdr.h>
#include <mach/at91sam9_sdramc.h>
#endif

#endif /* __AT91_RAMC_H__ */
