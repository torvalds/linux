/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform device definitions
 */


#ifndef __JZ4740_PLATFORM_H
#define __JZ4740_PLATFORM_H

#include <linux/platform_device.h>

extern struct platform_device jz4740_udc_device;
extern struct platform_device jz4740_udc_xceiv_device;
extern struct platform_device jz4740_mmc_device;
extern struct platform_device jz4740_i2c_device;
extern struct platform_device jz4740_nand_device;
extern struct platform_device jz4740_framebuffer_device;
extern struct platform_device jz4740_i2s_device;
extern struct platform_device jz4740_pcm_device;
extern struct platform_device jz4740_codec_device;
extern struct platform_device jz4740_adc_device;
extern struct platform_device jz4740_pwm_device;
extern struct platform_device jz4740_dma_device;

#endif
