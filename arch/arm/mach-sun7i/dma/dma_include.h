/*
 * arch/arm/mach-sun7i/dma/dma_include.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma header file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __DMA_INCLUDE_H
#define __DMA_INCLUDE_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/pm.h>

#include <mach/dma.h>
#include <mach/clock.h>
#include <mach/platform.h>

#include "dma_regs.h"
#include "dma_common.h"
#include "dma_csp.h"
#include "dma_interface.h"
#include "dma_core.h"

#ifdef DBG_DMA
#include <linux/delay.h>
#endif /* DBG_DMA */

#endif  /* __DMA_INCLUDE_H */

