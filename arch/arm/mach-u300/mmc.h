/*
 *
 * arch/arm/mach-u300/mmc.h
 *
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 *
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#ifndef MMC_H
#define MMC_H

#include <linux/amba/bus.h>

int __devinit mmc_init(struct amba_device *adev);

#endif
