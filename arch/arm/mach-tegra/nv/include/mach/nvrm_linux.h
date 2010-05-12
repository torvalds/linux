/*
 * Copyright (c) 2008-2010 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*  This header file defines shared structures used by Linux drivers
 *  integrating with Tegra NvRM.
 */

#ifndef INCLUDED_nvrm_linux_H
#define INCLUDED_nvrm_linux_H

/* nvcommon.h exepcts NV_DEBUG to be defined */
#ifndef NV_DEBUG
#ifdef DEBUG
#define NV_DEBUG DEBUG
#else
#define NV_DEBUG 0
#endif
#endif 

#include <nvrm_init.h>
#include <nvrm_i2c.h>
#include <nvrm_owr.h>
#include <nvrm_gpio.h>
#include <nvodm_query_pinmux.h>
#include <nvodm_query.h>
#include "nvddk_usbphy.h"

extern NvRmDeviceHandle s_hRmGlobal;
extern NvRmGpioHandle s_hGpioGlobal;

int tegra_get_partition_info_by_name(const char *PartName,
	NvU64 *pSectorStart, NvU64 *pSectorLength, NvU32 *pSectorSize);

int tegra_get_partition_info_by_num(int PartitionNum, char **pName,
	NvU64 *pSectorStart, NvU64 *pSectorEnd, NvU32 *pSectorSize);

int tegra_was_boot_device(const char *pBootDev);

NvU32 NvRmDmaUnreservedChannels(void);

#ifndef CONFIG_SERIAL_TEGRA_UARTS
#define TEGRA_SYSTEM_DMA_CH_UART 0
#else
#define TEGRA_SYSTEM_DMA_CH_UART (2*CONFIG_SERIAL_TEGRA_UARTS)
#endif

#ifdef CONFIG_TEGRA_SYSTEM_DMA
#define TEGRA_SYSTEM_DMA_CH_NUM (1 + TEGRA_SYSTEM_DMA_CH_UART)
#else
#define TEGRA_SYSTEM_DMA_CH_NUM (0)
#endif

/* DMA channels available to system DMA driver */
#define TEGRA_SYSTEM_DMA_CH_MIN NvRmDmaUnreservedChannels()
#define TEGRA_SYSTEM_DMA_CH_MAX \
	(TEGRA_SYSTEM_DMA_CH_MIN+TEGRA_SYSTEM_DMA_CH_NUM)

#endif
