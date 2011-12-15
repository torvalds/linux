/*
 * Copyright (C) 2009 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __ASM_ARCH_DM365_H
#define __ASM_ARCH_DM665_H

#include <linux/platform_device.h>
#include <linux/davinci_emac.h>
#include <mach/hardware.h>
#include <mach/asp.h>
#include <mach/keyscan.h>
#include <media/davinci/vpfe_capture.h>

#define DM365_ASYNC_EMIF_CONTROL_BASE	0x01D10000
#define DM365_ASYNC_EMIF_DATA_CE0_BASE	0x02000000
#define DM365_ASYNC_EMIF_DATA_CE1_BASE	0x04000000

void __init dm365_init(void);
void __init dm365_init_asp(struct snd_platform_data *pdata);
void __init dm365_init_vc(struct snd_platform_data *pdata);
void __init dm365_init_ks(struct davinci_ks_platform_data *pdata);
void __init dm365_init_rtc(void);
void dm365_init_spi0(unsigned chipselect_mask,
			struct spi_board_info *info, unsigned len);

void dm365_set_vpfe_config(struct vpfe_config *cfg);
#endif /* __ASM_ARCH_DM365_H */
