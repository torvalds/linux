/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __ASM_ARCH_DEVICES_H__
#define __ASM_ARCH_DEVICES_H__

struct platform_device;
struct amba_device;

extern struct platform_device u5500_gpio_devs[];
extern struct platform_device u8500_gpio_devs[];

extern struct amba_device ux500_pl031_device;

extern struct platform_device ux500_hash1_device;
extern struct platform_device ux500_cryp1_device;

extern struct platform_device u8500_dma40_device;
extern struct platform_device ux500_ske_keypad_device;

#endif
